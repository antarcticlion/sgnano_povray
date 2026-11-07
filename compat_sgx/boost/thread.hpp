// sgnano の SGX（M33BEATOS のタスク）の上の Boost.Thread 互換層（2026-11-06）。POV-Ray 3.7 が使う分だけ:
// thread（作る・join・detach・yield・sleep）・mutex / recursive_mutex（scoped_lock）・condition・thread_specific_ptr・xtime。
// - タスクは api->task_create（スタックは PSRAM）。バックエンドのスレッド（attributes で作られる）は 1 段低い優先度（thread の作り方の説明）
// - mutex は api->mutex_new（入れ子にできる）。1 回の実行で同期の部品の数には上限があるので、尽きたら自前の印 + 短い眠りで待つ
// - condition は「待っている数 + セマフォ」（セマフォが取れなければ短い眠りで見張る）
// - 【2026-11-06】数や印を不可分の命令（__atomic_*: LDREX / STREX）で触らない。プログラムの .bss・ヒープは PSRAM（XIP の窓）にあり、
//   RP2350 ではそこへの排他アクセスが効かず STREX が失敗し続けて止まった（condition の待ちの頭で回り続けた）。数と印は
//   全体で 1 つの SGX のミューテックス（gguard）の中で触る
// - 止める頼み（ESC）は API の待ちの中で効く（タスクはそこで終わる）
#ifndef POVRAY_SGX_COMPAT_BOOST_THREAD_HPP
#define POVRAY_SGX_COMPAT_BOOST_THREAD_HPP

#include <cstring>
#include <exception>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <cstdio>
#include <new>
#include <boost/function.hpp>
#include "sgx_api.h"

extern "C" const sgx_api_t *sgx;
// 同期の部品の数（切り分け用。povray_sgx.cpp が置く）: [0] mutex を取れた [1] 取れずに自前 [2] sem を取れた [3] 取れずに見張り
extern "C" unsigned sgx_compat_counts[4];

namespace boost
{

struct xtime
{
    typedef long xtime_sec_t;
    typedef long xtime_nsec_t;
    xtime_sec_t sec;
    xtime_nsec_t nsec;
};

enum { TIME_UTC_ = 1 };

inline int xtime_get(xtime *t, int)
{
    const uint64_t us = sgx->us();
    t->sec = (long)(us / 1000000u);
    t->nsec = (long)((us % 1000000u) * 1000u);
    return 1;
}

namespace sgx_detail
{
    // t までの µs（過ぎていれば 0、大きすぎれば SGX の待ちの上限の手前）
    inline uint32_t us_until(const xtime &t)
    {
        xtime now; xtime_get(&now, TIME_UTC_);
        const int64_t us = ((int64_t)t.sec - now.sec) * 1000000 + ((int64_t)t.nsec - now.nsec) / 1000;
        return us <= 0 ? 0u : (us >= 0xFFFFFFF0LL ? 0xFFFFFFF0u : (uint32_t)us);
    }
    // 譲る: API の yield は 1ms 眠る。POV-Ray の受け手の輪は 1 周ごとに yield するので、1 秒に 1000 通しかさばけず、
    // 描いたブロックの知らせが溜まってメモリが尽きた（2026-11-07）。20µs だけ眠る
    inline void short_yield() { sgx->sleep_us(20); }
    inline void log(const char *s) { sgx->log(s, (unsigned)std::strlen(s)); }

    // 全体で 1 つの守り（数と印のため。最初に使うときに作る。静的な初期化は入口のタスクだけが走るので取り合わない）
    inline sgx_mutex_t *glock() { static sgx_mutex_t *g = nullptr; if (!g) g = sgx->mutex_new(); return g; }
    struct gguard {
        gguard() { (void)sgx->mutex_lock(glock(), SGX_WAIT_FOREVER); }
        ~gguard() { sgx->mutex_unlock(glock()); }
    };

    // 入れ子にできるロック。API の mutex が取れなければ、持ち主と数を自分で持つ
    class lock_core
    {
    public:
        lock_core() : m(nullptr), owner(nullptr), depth(0) { (void)glock(); m = sgx->mutex_new(); sgx_compat_counts[m ? 0 : 1]++; }
        ~lock_core() { if (m) sgx->mutex_free(m); }
        void lock()
        {
            if (m) { (void)sgx->mutex_lock(m, SGX_WAIT_FOREVER); return; }
            sgx_task_t *me = sgx->task_self();
            for (;;) {
                { gguard g; if (owner == me) { depth++; return; } if (!owner) { owner = me; depth = 1; return; } }
                sgx->sleep_us(200);
            }
        }
        bool try_lock()
        {
            if (m) return sgx->mutex_lock(m, 0) == 0;
            sgx_task_t *me = sgx->task_self();
            gguard g;
            if (owner == me) { depth++; return true; }
            if (owner) return false;
            owner = me; depth = 1;
            return true;
        }
        void unlock()
        {
            if (m) { sgx->mutex_unlock(m); return; }
            gguard g;
            if (depth && --depth == 0) owner = nullptr;
        }
    private:
        lock_core(const lock_core &);
        lock_core &operator=(const lock_core &);
        sgx_mutex_t *m;
        sgx_task_t *volatile owner;
        unsigned depth;
    };
}

struct defer_lock_t {};
struct try_to_lock_t {};
struct adopt_lock_t {};

template<class M>
class unique_lock
{
public:
    explicit unique_lock(M &m_) : m(&m_), owns(false) { lock(); }
    unique_lock(M &m_, defer_lock_t) : m(&m_), owns(false) {}
    unique_lock(M &m_, try_to_lock_t) : m(&m_), owns(m_.try_lock()) {}
    unique_lock(M &m_, adopt_lock_t) : m(&m_), owns(true) {}
    ~unique_lock() { if (owns) m->unlock(); }
    void lock() { m->lock(); owns = true; }
    bool try_lock() { owns = m->try_lock(); return owns; }
    void unlock() { m->unlock(); owns = false; }
    bool owns_lock() const { return owns; }
    M *mutex() const { return m; }
private:
    unique_lock(const unique_lock &);
    unique_lock &operator=(const unique_lock &);
    M *m;
    bool owns;
};

class mutex
{
public:
    typedef unique_lock<mutex> scoped_lock;
    void lock() { c.lock(); }
    void unlock() { c.unlock(); }
    bool try_lock() { return c.try_lock(); }
private:
    sgx_detail::lock_core c;
};

class recursive_mutex
{
public:
    typedef unique_lock<recursive_mutex> scoped_lock;
    void lock() { c.lock(); }
    void unlock() { c.unlock(); }
    bool try_lock() { return c.try_lock(); }
private:
    sgx_detail::lock_core c;
};

// 待っている数を数え、知らせはセマフォで。セマフォが取れなければ世代の数を見張る
class condition
{
public:
    condition() : s(nullptr), waiters(0), gen(0) { (void)sgx_detail::glock(); s = sgx->sem_new(0, 0); sgx_compat_counts[s ? 2 : 3]++; }
    ~condition() { if (s) sgx->sem_free(s); }
    void notify_one()
    {
        bool give = false;
        { sgx_detail::gguard g; if (waiters > 0) { gen++; if (s) { waiters--; give = true; } } }
        if (give) sgx->sem_give(s);
    }
    void notify_all()
    {
        int n = 0;
        { sgx_detail::gguard g; gen++; if (s) { n = waiters; waiters = 0; } }
        while (n-- > 0) sgx->sem_give(s);
    }
    template<class L> void wait(L &l) { (void)timed_wait_us(l, SGX_WAIT_FOREVER); }
    template<class L> bool timed_wait(L &l, const xtime &t) { return timed_wait_us(l, sgx_detail::us_until(t)); }
    template<class L, class D> bool timed_wait(L &l, const D &d) { return timed_wait_us(l, (uint32_t)d.total_microseconds()); }
private:
    template<class L> bool timed_wait_us(L &l, uint32_t us)
    {
        unsigned g0;
        { sgx_detail::gguard g; g0 = gen; waiters++; }
        l.unlock();
        bool got;
        if (s) got = sgx->sem_take(s, us) == 0;
        else {
            const uint64_t t0 = sgx->us();
            for (;;) {
                { sgx_detail::gguard g; got = gen != g0; }
                if (got || (us != SGX_WAIT_FOREVER && sgx->us() - t0 >= us)) break;
                sgx->sleep_us(200);
            }
        }
        if (!got || !s) { sgx_detail::gguard g; if (waiters > 0) waiters--; }   // 時間切れ（知らせで引いた分は notify が引いた）
        l.lock();
        return got;
    }
    condition(const condition &);
    condition &operator=(const condition &);
    sgx_sem_t *s;
    volatile int waiters;
    volatile unsigned gen;
};
typedef condition condition_variable;
typedef condition condition_variable_any;

class thread
{
public:
    struct attributes
    {
        attributes() : stack(0) {}
        void set_stack_size(std::size_t n) { stack = n; }
        std::size_t stack;
    };
    class id
    {
    public:
        id() : t(nullptr) {}
        explicit id(sgx_task_t *t_) : t(t_) {}
        bool operator==(const id &o) const { return t == o.t; }
        bool operator!=(const id &o) const { return t != o.t; }
        bool operator<(const id &o) const { return t < o.t; }
    private:
        sgx_task_t *t;
    };

    thread() : t(nullptr) {}
    // 優先度（2026-11-07）: POV-Ray のバックエンドのスレッド（描く・解析・取り仕切り）は Task::NewBoostThread が attributes で作る。
    // これを 1 段低くし（SGX の 9 + 1）、フロントエンドの受け手（vfe の worker、attributes なし）が知らせを受けたらすぐ割り込んで片づけるようにする。
    // 同じ優先度だと描くスレッドが時間を使い切り、描いたブロックの知らせが受け手の前に数 MB 溜まってメモリが尽きた
    template<class F> explicit thread(F f) : t(nullptr) { start(boost::function0<void>(f), 0, 0); }
    template<class F> thread(attributes a, F f) : t(nullptr) { start(boost::function0<void>(f), a.stack, 1); }
    template<class F> thread(F f, int stack) : t(nullptr) { start(boost::function0<void>(f), (std::size_t)stack, 1); }
    ~thread() {}

    void join() { if (t) { (void)sgx->task_join(t, SGX_WAIT_FOREVER); t = nullptr; } }
    bool timed_join(const xtime &) { join(); return true; }
    void detach() { t = nullptr; }
    bool joinable() const { return t != nullptr; }
    id get_id() const { return id(t); }
    void interrupt() {}

    static void yield() { sgx_detail::short_yield(); }
    static unsigned hardware_concurrency() { return 1; }
    static void sleep(const xtime &x) { const uint32_t us = sgx_detail::us_until(x); if (us) sgx->sleep_us(us); }
private:
    struct start_arg { boost::function0<void> fn; };
    static void log_uncaught(const char *what)
    {
        char b[160];
        snprintf(b, sizeof b, "[pov] thread %p ended by an uncaught exception: %s\n", (void *)sgx->task_self(), what ? what : "?");
        sgx_detail::log(b);
    }
    // スタックの大きさ。POV-Ray の既定（configbackend.h）は数 MB を頼むことがあるので、PSRAM に合わせて 64KB〜256KB に収める。
    // 既定は 64KB（2026-11-06: 128KB ずつだと 5 本目の描画のスレッドで PSRAM が尽き、描画の仕事が積まれないまま全員が待った）。
    // バックエンド（prio 1: 描く・解析・取り仕切り）は 128KB 以上（2026-11-07: 02_arch_perspective で描くスレッドが 64KB を越え、
    // スタックの下限（PSPLIM）の STKOF で core1 が止まった。いまはファームがそのプログラムだけを終わらせる）
    static uint32_t stack_for(std::size_t asked, unsigned prio)
    {
        enum : uint32_t { MIN = 64u * 1024u, BACKEND_MIN = 128u * 1024u, MAX = 256u * 1024u };
        uint32_t st = asked ? (uint32_t)asked : MIN;
        if (prio && st < BACKEND_MIN) st = BACKEND_MIN;
        if (st < MIN) st = MIN;
        if (st > MAX) st = MAX;
        return st;
    }
    static void entry(void *p)
    {
        start_arg *a = static_cast<start_arg *>(p);
        // 受け取られない例外は、このスレッドだけを終わらせる（2026-11-07: ESC で止めたあとの POVMS の時間切れ（pov_base::Exception）が
        // スレッドの外へ出て、プログラム全体が terminate（134）になり、途中までの絵も残せなかった）
        try { a->fn(); }
        catch (const std::exception &e) { log_uncaught(e.what()); }
        catch (...) { log_uncaught("(not a std::exception)"); }
        delete a;
    }
    void start(const boost::function0<void> &f, std::size_t stack, unsigned prio)
    {
        start_arg *a = new start_arg;
        a->fn = f;
        const uint32_t st = stack_for(stack, prio);
        t = sgx->task_create(entry, a, st, prio, false, prio ? "povb" : "pov");
        char b[112];
        sgx_mem_info_t mi; std::memset(&mi, 0, sizeof mi); if (sgx->mem_info) sgx->mem_info(&mi);
        snprintf(b, sizeof b, "[pov] thread %p (prio +%u, stack %lu, PSRAM free %lu largest %lu)\n", (void *)t, prio, (unsigned long)st,
                 (unsigned long)mi.psram_free, (unsigned long)mi.psram_largest);
        sgx_detail::log(b);
        if (!t) { delete a; throw std::bad_alloc(); }
    }
    thread(const thread &);
    thread &operator=(const thread &);
    sgx_task_t *t;
};

namespace this_thread
{
    inline void yield() { sgx_detail::short_yield(); }
    inline thread::id get_id() { return thread::id(sgx->task_self()); }
    inline void sleep(const xtime &x) { thread::sleep(x); }
}

// タスクごとの値（16 タスクまで。SGX の 1 回の実行のタスクの上限と同じ）
template<class T>
class thread_specific_ptr
{
public:
    thread_specific_ptr() : cleanup(nullptr) { clear(); }
    explicit thread_specific_ptr(void (*c)(T *)) : cleanup(c) { clear(); }
    ~thread_specific_ptr() {}
    T *get() const
    {
        sgx_task_t *me = sgx->task_self();   // 読むだけは守らない（よく呼ばれる。set は値を書いてから持ち主を書く）
        for (unsigned i = 0; i < N; i++) if (slot[i].t == me) return slot[i].p;
        return nullptr;
    }
    T *operator->() const { return get(); }
    T &operator*() const { return *get(); }
    T *release() { T *q = get(); set(nullptr); return q; }
    void reset(T *q = nullptr)
    {
        T *old = get();
        if (old && old != q) { if (cleanup) cleanup(old); else delete old; }
        set(q);
    }
private:
    enum { N = 16 };
    struct ent { sgx_task_t *t; T *p; };
    void clear() { for (unsigned i = 0; i < N; i++) { slot[i].t = nullptr; slot[i].p = nullptr; } }
    // 表はスレッドどうしで分け合うので、全体の守りの中で触る（2026-11-07: 2 つのスレッドが同じ空きの枠を取りえた）
    void set(T *q)
    {
        sgx_task_t *me = sgx->task_self();
        sgx_detail::gguard g;
        for (unsigned i = 0; i < N; i++) if (slot[i].t == me) { slot[i].p = q; if (!q) slot[i].t = nullptr; return; }
        if (!q) return;
        for (unsigned i = 0; i < N; i++) if (!slot[i].t || !sgx->task_alive(slot[i].t)) { slot[i].p = q; slot[i].t = me; return; }
    }
    void (*cleanup)(T *);
    ent slot[N];
};

} // namespace boost

#endif

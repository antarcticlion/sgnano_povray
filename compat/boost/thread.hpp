// POV-Ray 3.7 のホスト移植用 Boost.Thread 互換層。
//
// POV-Ray 3.7 は Boost.Thread を直接使う。sgnano のターゲットでは単一スレッドにするが、
// ホストでまず動かすために std::thread / std::mutex / std::condition_variable_any へ
// 名前を写す。Boost の本物の thread.hpp より先に include されるよう、include パスの
// 先頭に置く（-Icompat -Ithird_party/boost）。
#ifndef POVRAY_COMPAT_BOOST_THREAD_HPP
#define POVRAY_COMPAT_BOOST_THREAD_HPP

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <ctime>
#include <map>
#include <mutex>
#include <thread>
#include <utility>

namespace boost
{

// POV-Ray が使う古い xtime。POV_TIME_UTC の値と合わせる。
struct xtime
{
    typedef long xtime_sec_t;
    typedef long xtime_nsec_t;
    xtime_sec_t sec;
    xtime_nsec_t nsec;
};

enum { TIME_UTC_ = 1 };   // TIME_UTC は C11 の <time.h> とぶつかるので定義しない

inline int xtime_get(xtime *t, int clock_id)
{
    (void)clock_id;
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
        return 0;
    t->sec = (xtime::xtime_sec_t)ts.tv_sec;
    t->nsec = (xtime::xtime_nsec_t)ts.tv_nsec;
    return 1;
}

class mutex
{
public:
    typedef std::unique_lock<mutex> scoped_lock;

    void lock() { m.lock(); }
    void unlock() { m.unlock(); }
    bool try_lock() { return m.try_lock(); }

private:
    std::mutex m;
};

class recursive_mutex
{
public:
    typedef std::unique_lock<recursive_mutex> scoped_lock;

    void lock() { m.lock(); }
    void unlock() { m.unlock(); }
    bool try_lock() { return m.try_lock(); }

private:
    std::recursive_mutex m;
};

// POV-Ray の古い boost::condition。std::condition_variable_any は
// unique_lock<mutex> / unique_lock<recursive_mutex> のどちらでも待てる。
// 古い API の timed_wait(lock, boost::xtime) も用意する。
class condition
{
public:
    void notify_one() { c.notify_one(); }
    void notify_all() { c.notify_all(); }

    template<class L> void wait(L &lock) { c.wait(lock); }

    template<class L>
    bool timed_wait(L &lock, const xtime &t)
    {
        const auto tp = std::chrono::system_clock::time_point(
            std::chrono::seconds(t.sec) + std::chrono::nanoseconds(t.nsec));
        return c.wait_until(lock, tp) == std::cv_status::no_timeout;
    }

    template<class L, class Rep, class Period>
    bool timed_wait(L &lock, const std::chrono::duration<Rep, Period> &d)
    {
        return c.wait_for(lock, d) == std::cv_status::no_timeout;
    }

private:
    std::condition_variable_any c;
};

typedef condition condition_variable;

// Boost の defer_lock_t など。std のタグをそのまま使う。
using std::defer_lock_t;
using std::try_to_lock_t;
using std::adopt_lock_t;

class thread
{
public:
    struct attributes
    {
        void set_stack_size(std::size_t) {}
    };

    thread() {}
    template<class F> explicit thread(F f) : t(std::forward<F>(f)) {}
    template<class F> thread(attributes, F f) : t(std::forward<F>(f)) {}
    template<class F> thread(F f, int) : t(std::forward<F>(f)) {}
    ~thread()
    {
        if (t.joinable())
            t.detach();
    }

    thread(const thread &) = delete;
    thread &operator=(const thread &) = delete;
    thread(thread &&) = default;
    thread &operator=(thread &&) = default;

    void join() { t.join(); }
    void detach() { t.detach(); }
    bool joinable() const { return t.joinable(); }
    std::thread::id get_id() const { return t.get_id(); }

    static void yield() { std::this_thread::yield(); }
    static unsigned hardware_concurrency() { return std::thread::hardware_concurrency(); }

    static void sleep(const xtime &t)
    {
        struct timespec ts;
        ts.tv_sec = (time_t)t.sec;
        ts.tv_nsec = (long)t.nsec;
        nanosleep(&ts, nullptr);
    }

private:
    std::thread t;
};

namespace this_thread
{
    inline void yield() { std::this_thread::yield(); }
    inline std::thread::id get_id() { return std::this_thread::get_id(); }
}

// Boost の thread_specific_ptr。ホストでは std::thread::id ごとの map で表す。
// ターゲットの単一スレッド版では、もっと単純な実装に差し替える。
template<class T>
class thread_specific_ptr
{
public:
    thread_specific_ptr() : cleanup(nullptr) {}
    explicit thread_specific_ptr(void (*fn)(T *)) : cleanup(fn) {}
    ~thread_specific_ptr()
    {
        std::lock_guard<std::mutex> lock(m);
        if (cleanup)
            for (auto &kv : values)
                if (kv.second)
                    cleanup(kv.second);
    }

    T *get() const
    {
        std::lock_guard<std::mutex> lock(m);
        auto it = values.find(std::this_thread::get_id());
        return it == values.end() ? nullptr : it->second;
    }
    T *operator->() const { return get(); }
    T &operator*() const { return *get(); }

    T *release()
    {
        std::lock_guard<std::mutex> lock(m);
        auto it = values.find(std::this_thread::get_id());
        if (it == values.end())
            return nullptr;
        T *p = it->second;
        values.erase(it);
        return p;
    }

    void reset(T *p = nullptr)
    {
        std::lock_guard<std::mutex> lock(m);
        T *&slot = values[std::this_thread::get_id()];
        if (slot && slot != p && cleanup)
            cleanup(slot);
        slot = p;
    }

private:
    mutable std::mutex m;
    std::map<std::thread::id, T *> values;
    void (*cleanup)(T *);
};

} // namespace boost

#endif // POVRAY_COMPAT_BOOST_THREAD_HPP

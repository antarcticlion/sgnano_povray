// ターゲット（RP2350 / 単一スレッド）用の Boost.Thread 互換層。
// サイズ測定と、後で M33BEATOS の単一タスクに載せるためのスタブ。
// 実際のスレッドは作らない。POV-Ray の Task クラスは単一スレッド用に
// 別途「その場で実行」へ直す前提。
#ifndef POVRAY_ARM_COMPAT_BOOST_THREAD_HPP
#define POVRAY_ARM_COMPAT_BOOST_THREAD_HPP

#include <cstddef>
#include <ctime>

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
    t->sec = 0;
    t->nsec = 0;
    return 1;
}

class mutex
{
public:
    class scoped_lock
    {
    public:
        explicit scoped_lock(mutex &) {}
        template<class Tag> scoped_lock(mutex &, Tag) {}
        void lock() {}
        void unlock() {}
        bool owns_lock() const { return false; }
    };
    void lock() {}
    void unlock() {}
    bool try_lock() { return true; }
};

class recursive_mutex
{
public:
    class scoped_lock
    {
    public:
        explicit scoped_lock(recursive_mutex &) {}
        template<class Tag> scoped_lock(recursive_mutex &, Tag) {}
        void lock() {}
        void unlock() {}
        bool owns_lock() const { return false; }
    };
    void lock() {}
    void unlock() {}
    bool try_lock() { return true; }
};

class condition
{
public:
    void notify_one() {}
    void notify_all() {}
    template<class L> void wait(L &) {}
    template<class L> bool timed_wait(L &, const xtime &) { return false; }
    template<class L, class D> bool timed_wait(L &, const D &) { return false; }
};

typedef condition condition_variable;

struct defer_lock_t {};
struct try_to_lock_t {};
struct adopt_lock_t {};

class thread
{
public:
    struct attributes
    {
        void set_stack_size(std::size_t) {}
    };

    thread() {}
    template<class F> explicit thread(F) {}
    template<class F> thread(attributes, F) {}
    template<class F> thread(F, int) {}
    ~thread() {}

    void join() {}
    void detach() {}
    bool joinable() const { return false; }
    unsigned long get_id() const { return 0; }

    static void yield() {}
    static unsigned hardware_concurrency() { return 1; }
    static void sleep(const xtime &) {}
};

namespace this_thread
{
    inline void yield() {}
    inline unsigned long get_id() { return 0; }
}

template<class T>
class thread_specific_ptr
{
public:
    thread_specific_ptr() : p(nullptr) {}
    explicit thread_specific_ptr(void (*)(T *)) : p(nullptr) {}
    T *get() const { return p; }
    T *operator->() const { return p; }
    T &operator*() const { return *p; }
    T *release() { T *q = p; p = nullptr; return q; }
    void reset(T *q = nullptr) { p = q; }
private:
    T *p;
};

} // namespace boost

#endif

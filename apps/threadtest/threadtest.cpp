// compat_sgx/boost/thread.hpp（SGX のタスクの上の Boost.Thread）の試し（2026-11-06）。POV-Ray を組まずに確かめる。
// 結果の行は THREADTEST RESULT: PASS/FAIL n/m
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include "sgx_api.h"

extern "C" const sgx_api_t *sgx;
extern "C" { unsigned sgx_compat_counts[4]; }
static unsigned ok_n, all_n;
static void say(const char *s) { sgx->term_write(s, (unsigned)strlen(s)); sgx->log(s, (unsigned)strlen(s)); }
static void check(bool c, const char *w) { char b[160]; all_n++; if (c) ok_n++; snprintf(b, sizeof b, "%s %s\n", c ? "  ok  " : "  NG  ", w); say(b); }

static boost::mutex m;
static boost::condition cv;
static volatile int stage, counter;
static boost::thread_specific_ptr<int> tsp;

static void worker_started() { say("    worker: started\n"); boost::mutex::scoped_lock l(m); stage = 1; cv.notify_all(); }
static void adder(int n) { for (int i = 0; i < n; i++) { boost::mutex::scoped_lock l(m); counter++; } }
static void thrower(int n) { for (int i = 0; i < n; i++) { try { throw std::runtime_error("x"); } catch (const std::exception &e) { if (e.what()[0] == 'x') counter++; } } }
static void tls_worker(int v) { tsp.reset(new int(v)); sgx->sleep_us(20000); stage += (*tsp.get() == v) ? 1 : 100; }

extern "C" int sgx_main(const sgx_api_t *api, int, char **)
{
	(void)api;
	say("threadtest\n");
	char b[160];
	// 0. API を直に: 上限つきの待ちが時間切れで戻るか
	{ sgx_sem_t *sm = sgx->sem_new(0, 0); uint64_t t0 = sgx->us(); int r = sgx->sem_take(sm, 300000); uint64_t dt = sgx->us() - t0;
	  snprintf(b, sizeof b, "    api sem_take(300ms) = %d after %lu us\n", r, (unsigned long)dt); say(b); check(r != 0 && dt >= 250000 && dt < 1000000, "api sem_take times out");
	  t0 = sgx->us(); r = sgx->sem_take(sm, 5000); dt = sgx->us() - t0;
	  snprintf(b, sizeof b, "    api sem_take(5ms) = %d after %lu us\n", r, (unsigned long)dt); say(b);
	  sgx->sem_free(sm);
	  t0 = sgx->us(); sgx->sleep_us(200); dt = sgx->us() - t0; snprintf(b, sizeof b, "    sleep_us(200) took %lu us\n", (unsigned long)dt); say(b);
	  t0 = sgx->us(); sgx->sleep_us(300000); dt = sgx->us() - t0; snprintf(b, sizeof b, "    sleep_us(300000) took %lu us\n", (unsigned long)dt); say(b); }
	// 1. 時間切れ: 誰も知らせない condition は 300ms で戻る
	{ boost::mutex::scoped_lock l(m); boost::xtime t; boost::xtime_get(&t, boost::TIME_UTC_); t.nsec += 300000000; if (t.nsec >= 1000000000) { t.sec++; t.nsec -= 1000000000; }
	  const uint64_t t0 = sgx->us(); const bool got = cv.timed_wait(l, t); const uint64_t dt = sgx->us() - t0;
	  snprintf(b, sizeof b, "    timed_wait returned %d after %lu us\n", (int)got, (unsigned long)dt); say(b);
	  check(!got && dt >= 250000 && dt < 1000000, "timed_wait times out in ~300ms"); }
	// 2. タスクを作り、知らせを受ける
	{ boost::mutex::scoped_lock l(m); stage = 0; boost::thread th(&worker_started);
	  boost::xtime t; boost::xtime_get(&t, boost::TIME_UTC_); t.sec += 2;
	  while (stage == 0) if (!cv.timed_wait(l, t)) break;
	  check(stage == 1, "a new thread runs and notifies");
	  l.unlock(); th.join(); check(true, "join"); }
	// 3. 4 つのスレッドで 1000 回ずつ数える（mutex）
	{ counter = 0; boost::thread *t[4]; for (int i = 0; i < 4; i++) t[i] = new boost::thread(boost::bind(&adder, 1000));
	  for (int i = 0; i < 4; i++) { t[i]->join(); delete t[i]; }
	  snprintf(b, sizeof b, "    counter %d\n", counter); say(b); check(counter == 4000, "mutex across 4 threads"); }
	// 4. タスクごとの値
	{ stage = 0; boost::thread a(boost::bind(&tls_worker, 7)), c(boost::bind(&tls_worker, 9)); a.join(); c.join(); check(stage == 2, "thread_specific_ptr per task"); }
	// 5. 部品を使い切ったあとの予備の形（mutex と condition を 60 個ずつ作る）
	{ boost::mutex *mm[60]; boost::condition *cc[60]; for (int i = 0; i < 60; i++) { mm[i] = new boost::mutex; cc[i] = new boost::condition; }
	  snprintf(b, sizeof b, "    sync objects: mutex %u (+%u own) sem %u (+%u polled)\n", sgx_compat_counts[0], sgx_compat_counts[1], sgx_compat_counts[2], sgx_compat_counts[3]); say(b);
	  boost::mutex::scoped_lock l(*mm[59]); boost::xtime t; boost::xtime_get(&t, boost::TIME_UTC_); t.nsec += 200000000; if (t.nsec >= 1000000000) { t.sec++; t.nsec -= 1000000000; }
	  const uint64_t t0 = sgx->us(); const bool got = cc[59]->timed_wait(l, t); const uint64_t dt = sgx->us() - t0;
	  snprintf(b, sizeof b, "    polled timed_wait returned %d after %lu us\n", (int)got, (unsigned long)dt); say(b);
	  check(!got && dt >= 150000 && dt < 1000000, "polled timed_wait times out in ~200ms");
	  l.unlock(); for (int i = 0; i < 60; i++) { delete mm[i]; delete cc[i]; } }
	// 6. 例外を投げて受ける（catch の後片付けは例外の頭を不可分の命令で触る。SRAM の置き場）
	{ counter = 0; boost::thread t1(boost::bind(&thrower, 200)); thrower(200); t1.join();
	  snprintf(b, sizeof b, "    caught %d\n", counter); say(b); check(counter == 400, "throw / catch in 2 threads"); }
	snprintf(b, sizeof b, "THREADTEST RESULT: %s %u/%u\n", ok_n == all_n ? "PASS" : "FAIL", ok_n, all_n); say(b);
	return ok_n == all_n ? 0 : 1;
}

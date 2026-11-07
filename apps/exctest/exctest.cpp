// SGX での C++ の例外の巻き戻しの切り分け（2026-11-06）。巻き戻しの道筋（_Unwind_Backtrace）と、投げて受けられるかを出す
#include <cstdio>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <unwind.h>
#include <cxxabi.h>
#include <typeinfo>
#include "sgx_api.h"

extern "C" const sgx_api_t *sgx;
extern "C" char __exidx_start[], __exidx_end[];
static void say(const char *s) { sgx->term_write(s, (unsigned)strlen(s)); sgx->log(s, (unsigned)strlen(s)); }
static uintptr_t base;
static int nbt;
static _Unwind_Reason_Code bt(_Unwind_Context *c, void *)
{
	char b[80]; snprintf(b, sizeof b, "    frame %d pc %08lx (image +%lx)\n", nbt, (unsigned long)_Unwind_GetIP(c), (unsigned long)(_Unwind_GetIP(c) - base)); say(b);
	return ++nbt > 12 ? _URC_END_OF_STACK : _URC_NO_REASON;
}
static volatile int stage;
static void on_term() { const std::type_info *t = abi::__cxa_current_exception_type(); char b[120]; snprintf(b, sizeof b, "EXCTEST terminate called at stage %d, current %s\n", stage, t ? t->name() : "(none)"); say(b); sgx->exit(3); for (;;) {} }
namespace __cxxabiv1 { extern std::terminate_handler __terminate_handler; std::terminate_handler __terminate_handler = on_term; }

__attribute__((noinline)) static void deep(int n) { if (n == 0) { nbt = 0; _Unwind_Reason_Code r = _Unwind_Backtrace(bt, nullptr); char b[60]; snprintf(b, sizeof b, "    backtrace rc %d\n", (int)r); say(b); return; } deep(n - 1); __asm volatile(""); }
__attribute__((noinline)) static void thrower() { throw 42; }

extern "C" int sgx_main(const sgx_api_t *, int, char **)
{
	char b[160];
	base = (uintptr_t)&sgx_main - (uintptr_t)0;   // 下で置き場の頭を出す
	snprintf(b, sizeof b, "EXCTEST exidx %p..%p (%u entries) sgx_main %p\n", (void *)__exidx_start, (void *)__exidx_end, (unsigned)((__exidx_end - __exidx_start) / 8), (void *)&sgx_main); say(b);
	deep(3);
	int got = 0;
	stage = 1; try { throw 7; } catch (...) { got += 1; }
	snprintf(b, sizeof b, "    same frame catch(...) ok\n"); say(b);
	stage = 2; try { throw 7; } catch (int v) { got += v; }
	say("    same frame catch(int) ok\n");
	stage = 3; try { thrower(); } catch (int v) { got = v; }
	snprintf(b, sizeof b, "EXCTEST RESULT: caught %d\n", got); say(b);
	return 0;
}

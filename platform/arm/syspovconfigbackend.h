#ifndef __SYSPOVCONFIGBACKEND_H__
#define __SYSPOVCONFIGBACKEND_H__

#include "syspovconfig.h"

#define POVRAY_PLATFORM_NAME "sgnano"
#define ALTMAIN
#define NEW_LINE_STRING "\n"
#define SYS_DEF_EXT     ""

/* ターゲットは単一スレッド。スタックは SGX のタスクスタックを使う。 */
#define POV_THREAD_STACK_SIZE (0)

#endif

/* sgnano_povray: ARM 用の syspovconfig.h。
 * vfe/unix の代わりに platform/arm を include パスの先頭に置く。
 * サイズ測定用で、ファイル I/O と POVMS の実体はまだ持たない。
 */
#ifndef __SYSPOVCONFIG_H__
#define __SYSPOVCONFIG_H__

#include "config.h"

#include <algorithm>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

/* POV-Ray の file-backed image は 64 ビット seek を使う。サイズ測定用に 32 ビットへ。 */
#ifndef lseek64
#define lseek64 lseek
#endif

#include <exception>
#include <stdexcept>
#include <string>
#include <vector>
#include <list>
#include <memory>

#include "vfeconf.h"

using std::max;
using std::min;
using std::string;
using std::vector;
using std::list;
using std::runtime_error;
#define POV_TR1_NAMESPACE std
using std::shared_ptr;
using std::weak_ptr;
using std::dynamic_pointer_cast;

#define POV_LONG long long
#define POV_ULONG unsigned long long

#ifndef NULL
const int NULL = 0;
#endif

#ifndef FILE_NAME_LENGTH
#define FILE_NAME_LENGTH 200
#endif
#define MAX_PATH FILE_NAME_LENGTH

#define DEFAULT_OUTPUT_FORMAT       kPOVList_FileType_PPM
#define DEFAULT_DISPLAY_GAMMA_TYPE  kPOVList_GammaType_SRGB
#define DEFAULT_DISPLAY_GAMMA       2.2
#define DEFAULT_FILE_GAMMA_TYPE     kPOVList_GammaType_SRGB
#define DEFAULT_FILE_GAMMA          2.2

#define METADATA_PLATFORM_STRING "arm-none-eabi"
#define METADATA_COMPILER_STRING "arm-none-eabi-g++"

/* 単一スレッドのターゲット: thread_local を使わない。 */
#define DECLARE_THREAD_LOCAL_PTR(ptrType, ptrName)                ptrType *ptrName
#define IMPLEMENT_THREAD_LOCAL_PTR(ptrType, ptrName, ignore)      ptrType *ptrName = NULL
#define GET_THREAD_LOCAL_PTR(ptrName)                             (ptrName)
#define SET_THREAD_LOCAL_PTR(ptrName, ptrValue)                   (ptrName = ptrValue)

/* POV-Ray のパス解析。UCS2String でも動くテンプレート。 */
namespace pov_arm
{
template<class P, class V, class C, class F>
inline bool ParsePathString(const P &path, V &volume, C &components, F &filename)
{
    typedef typename P::value_type char_t;
    if (path.empty()) return true;
    if (path[0] == (char_t)'/') volume = (char_t)'/';
    P q;
    for (size_t i = 0; i < path.length(); ++i)
    {
        if (path[i] == (char_t)'/')
        {
            if (!q.empty()) components.push_back(q);
            q.clear();
        }
        else
            q += path[i];
    }
    filename = q;
    return true;
}
}
#define POV_PARSE_PATH_STRING(p, v, c, f) pov_arm::ParsePathString((p), (v), (c), (f))

#define POV_ALLOW_FILE_READ(f, t)  (1)
#define POV_ALLOW_FILE_WRITE(f, t) (1)
#define POV_UCS2_FOPEN(name, mode) fopen(UCS2toASCIIString(UCS2String(name)).c_str(), (mode))
#define POV_UCS2_REMOVE(name) remove(UCS2toASCIIString(UCS2String(name)).c_str())

#define POV_CONVERT_TEXT_TO_UCS2(ts, tsl, as) (NULL)

#endif // __SYSPOVCONFIG_H__

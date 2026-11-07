// POVMS の圧縮されたメッセージを解く uncompress（zlib の名前）を、組み込んである miniz へつなぐ（2026-11-06、SGX 版）
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES 1
#include "miniz.h"

int uncompress(unsigned char *dest, unsigned long *destLen, const unsigned char *source, unsigned long sourceLen)
{
    mz_ulong n = *destLen;
    const int r = mz_uncompress(dest, &n, source, (mz_ulong)sourceLen);
    *destLen = n;
    return r;
}
int compress2(unsigned char *dest, unsigned long *destLen, const unsigned char *source, unsigned long sourceLen, int level)
{
    mz_ulong n = *destLen;
    const int r = mz_compress2(dest, &n, source, (mz_ulong)sourceLen, level);
    *destLen = n;
    return r;
}

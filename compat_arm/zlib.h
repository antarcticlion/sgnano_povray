// ARM サイズ測定用の最小 zlib.h。
// POVMS の圧縮ストリームはターゲットでは使わない前提。リンクはしない。
#ifndef POVRAY_ARM_COMPAT_ZLIB_H
#define POVRAY_ARM_COMPAT_ZLIB_H

typedef unsigned char Bytef;
typedef unsigned long uLongf;
typedef unsigned long uLong;

#define Z_OK 0
#define Z_BEST_COMPRESSION 9

#ifdef __cplusplus
extern "C" {
#endif

int uncompress(Bytef *dest, uLongf *destLen, const Bytef *source, uLong sourceLen);
int compress2(Bytef *dest, uLongf *destLen, const Bytef *source, uLong sourceLen, int level);

#ifdef __cplusplus
}
#endif

#endif

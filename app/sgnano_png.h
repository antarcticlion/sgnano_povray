// PNG 書き出し（可逆圧縮）。miniz の tdefl を使う。
#ifndef SGNANO_PNG_H
#define SGNANO_PNG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// rgb: width*height*3 の RGB888。成功なら malloc された PNG を返す。
// 返した領域は sgnano_png_free() で返す。
uint8_t *sgnano_png_encode(const uint8_t *rgb, uint32_t width, uint32_t height, uint32_t *out_len);
void     sgnano_png_free(void *p);

#ifdef __cplusplus
}
#endif

#endif

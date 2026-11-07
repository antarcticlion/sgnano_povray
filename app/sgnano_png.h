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
// 1 行ずつ縮める（2026-11-07）。row(user, y, rgb) が y 行目の RGB（width*3 バイト）を書く。返したものは sgnano_png_free_rows で返す
typedef void (*sgnano_png_row_fn)(void *user, uint32_t y, uint8_t *rgb);
uint8_t *sgnano_png_encode_rows(uint32_t width, uint32_t height, sgnano_png_row_fn row, void *user, uint32_t *out_len);
void     sgnano_png_free_rows(void *p);

#ifdef __cplusplus
}
#endif

#endif

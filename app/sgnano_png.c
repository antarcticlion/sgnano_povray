// PNG 書き出し。miniz の tdefl_write_image_to_png_file_in_memory_ex は
// 可逆な PNG（zlib/deflate）をメモリ上に作る。
#include "app/sgnano_png.h"

#include "miniz.h"

uint8_t *sgnano_png_encode(const uint8_t *rgb, uint32_t width, uint32_t height, uint32_t *out_len)
{
    if (!rgb || !width || !height || !out_len) return NULL;
    size_t n = 0;
    // レベル 9（最高圧縮）。PNG は可逆。
    void *p = tdefl_write_image_to_png_file_in_memory_ex(rgb, (int)width, (int)height, 3, &n, 9, MZ_FALSE);
    if (!p) return NULL;
    *out_len = (uint32_t)n;
    return (uint8_t *)p;
}

void sgnano_png_free(void *p) { if (p) mz_free(p); }

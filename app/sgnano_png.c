// PNG 書き出し。miniz の tdefl_write_image_to_png_file_in_memory_ex は
// 可逆な PNG（zlib/deflate）をメモリ上に作る。
#include "app/sgnano_png.h"

#include <stdlib.h>
#include <string.h>

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

// ---- 1 行ずつ縮める（2026-11-07）----
// 上の形は画像全体の RGB（640x480 で 921KB）・縮めの作業場・出来上がりを同時に持つ。実機では POV-Ray を止めた直後
// （まだセッションが残っている）に 921KB の続いた置き場が取れず、途中までの絵の PNG が書けなかった。
// こちらは行を 1 つずつ受け取り、出来上がりの置き場だけを伸ばしていく（縮めの作業場は tdefl_compressor の大きさ）
typedef struct { uint8_t *p; size_t n, cap; bool bad; } outbuf_t;

static bool out_put(outbuf_t *o, const void *d, size_t n)
{
    if (o->bad) return false;
    if (o->n + n > o->cap) {
        size_t c = o->cap ? o->cap : 64u * 1024u;
        while (c < o->n + n) c *= 2u;
        uint8_t *q = (uint8_t *)realloc(o->p, c);
        if (!q) { o->bad = true; return false; }
        o->p = q; o->cap = c;
    }
    memcpy(o->p + o->n, d, n);
    o->n += n;
    return true;
}
static mz_bool put_cb(const void *buf, int len, void *user) { return out_put((outbuf_t *)user, buf, (size_t)len) ? MZ_TRUE : MZ_FALSE; }
static void be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
// 塊の CRC（型の 4 バイトから中身の終わりまで）を後ろに足す
static bool chunk_crc(outbuf_t *o, size_t type_at)
{
    uint8_t c[4];
    be32(c, (uint32_t)mz_crc32(MZ_CRC32_INIT, o->p + type_at, o->n - type_at));
    return out_put(o, c, 4);
}

uint8_t *sgnano_png_encode_rows(uint32_t width, uint32_t height, sgnano_png_row_fn row, void *user, uint32_t *out_len)
{
    if (!row || !width || !height || !out_len) return NULL;
    outbuf_t o = { NULL, 0, 0, false };
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    uint8_t ihdr[25] = { 0, 0, 0, 13, 'I', 'H', 'D', 'R' };
    be32(ihdr + 8, width); be32(ihdr + 12, height);
    ihdr[16] = 8; ihdr[17] = 2;                     // 8 ビット・RGB（圧縮・フィルタ・飛び越しは 0）
    (void)out_put(&o, sig, 8);
    (void)out_put(&o, ihdr, 21);
    (void)chunk_crc(&o, 8 + 4);
    const size_t idat_at = o.n;                     // IDAT の長さは最後に書く
    static const uint8_t idat_head[8] = { 0, 0, 0, 0, 'I', 'D', 'A', 'T' };
    (void)out_put(&o, idat_head, 8);

    tdefl_compressor *c = (tdefl_compressor *)malloc(sizeof(tdefl_compressor));
    uint8_t *line = (uint8_t *)malloc((size_t)width * 3u + 1u);
    bool ok = c && line && !o.bad;
    if (ok) ok = tdefl_init(c, put_cb, &o, (int)(tdefl_create_comp_flags_from_zip_params(6, MZ_DEFAULT_WINDOW_BITS, MZ_DEFAULT_STRATEGY) | TDEFL_WRITE_ZLIB_HEADER)) == TDEFL_STATUS_OKAY;
    for (uint32_t y = 0; ok && y < height; y++) {
        line[0] = 0;                                // フィルタなし
        row(user, y, line + 1);
        ok = tdefl_compress_buffer(c, line, (size_t)width * 3u + 1u, y + 1u == height ? TDEFL_FINISH : TDEFL_NO_FLUSH) ==
             (y + 1u == height ? TDEFL_STATUS_DONE : TDEFL_STATUS_OKAY);
    }
    free(line);
    free(c);
    if (ok && !o.bad) {
        be32(o.p + idat_at, (uint32_t)(o.n - idat_at - 8u));
        ok = chunk_crc(&o, idat_at + 4u);
        static const uint8_t iend[8] = { 0, 0, 0, 0, 'I', 'E', 'N', 'D' };
        ok = ok && out_put(&o, iend, 8) && chunk_crc(&o, o.n - 4u);
    }
    if (!ok || o.bad) { free(o.p); return NULL; }
    *out_len = (uint32_t)o.n;
    return o.p;
}
void sgnano_png_free_rows(void *p) { free(p); }

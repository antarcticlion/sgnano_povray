// SGX スタブ上で sgnano_app を動かす。
// 画面は mode 2（テキスト＋RGB555 FB）、チェックポイントは SGX 4.0 の
// ブロックストア（スタブ）に置く。
#include "app/sgx/sgnano_app_sgx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform/sgx/sgx4_stub.h"

typedef struct {
    int id;
    uint32_t block_size;
    uint32_t blocks;
} sgx_store_t;

static uint16_t *g_fb;
static uint32_t g_w, g_h;
static bool g_text_cleared;

static uint32_t min_u32(uint32_t a, uint32_t b) { return a < b ? a : b; }

static void *s_open(const char *path, uint32_t size, bool create)
{
    if (!sgx4 || !sgx4->store_open) return NULL;
    sgx_store_t *h = (sgx_store_t *)malloc(sizeof *h);
    if (!h) return NULL;
    h->block_size = 4096;
    h->blocks = (size + h->block_size - 1u) / h->block_size;
    sgx4_store_config_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.path = path;
    cfg.block_size = h->block_size;
    cfg.block_count = h->blocks;
    cfg.cache_blocks = 8;
    cfg.flags = SGX4_STORE_WRITEBACK | (create ? SGX4_STORE_CREATE : 0u);
    h->id = sgx4->store_open(&cfg);
    if (h->id < 0) { free(h); return NULL; }
    return h;
}

static bool s_read_at(void *vh, uint32_t off, void *dst, uint32_t n)
{
    sgx_store_t *h = (sgx_store_t *)vh;
    if (!h || (uint64_t)off + n > (uint64_t)h->blocks * h->block_size) return false;
    uint8_t *d = (uint8_t *)dst;
    while (n) {
        const uint32_t block = off / h->block_size;
        const uint32_t bo = off % h->block_size;
        const uint32_t take = min_u32(n, h->block_size - bo);
        void *p = sgx4->store_get(h->id, block);
        if (!p) return false;
        memcpy(d, (const uint8_t *)p + bo, take);
        (void)sgx4->store_release(p, 0);
        off += take; d += take; n -= take;
    }
    return true;
}

static bool s_write_at(void *vh, uint32_t off, const void *src, uint32_t n)
{
    sgx_store_t *h = (sgx_store_t *)vh;
    if (!h || (uint64_t)off + n > (uint64_t)h->blocks * h->block_size) return false;
    const uint8_t *s = (const uint8_t *)src;
    while (n) {
        const uint32_t block = off / h->block_size;
        const uint32_t bo = off % h->block_size;
        const uint32_t take = min_u32(n, h->block_size - bo);
        void *p = sgx4->store_get(h->id, block);
        if (!p) return false;
        memcpy((uint8_t *)p + bo, s, take);
        (void)sgx4->store_release(p, 1);
        off += take; s += take; n -= take;
    }
    return true;
}

static bool s_flush(void *vh)
{
    sgx_store_t *h = (sgx_store_t *)vh;
    return h && sgx4->store_flush && sgx4->store_flush(h->id) == 0;
}

static void s_close(void *vh)
{
    sgx_store_t *h = (sgx_store_t *)vh;
    if (!h) return;
    if (sgx4->store_flush) (void)sgx4->store_flush(h->id);
    if (sgx4->store_close) (void)sgx4->store_close(h->id);
    free(h);
}

static bool s_remove(const char *path) { return sgx4 && sgx4->remove && sgx4->remove(path) == 0; }

static void s_fb_put(uint32_t x, uint32_t y, const uint16_t *pixels, uint32_t n)
{
    if (!g_fb || y >= g_h || x >= g_w) return;
    if (x + n > g_w) n = g_w - x;
    memcpy(g_fb + (size_t)y * g_w + x, pixels, (size_t)n * 2u);
}

static void s_fb_get(uint32_t x, uint32_t y, uint16_t *pixels, uint32_t n)
{
    if (!g_fb || y >= g_h || x >= g_w) return;
    if (x + n > g_w) n = g_w - x;
    memcpy(pixels, g_fb + (size_t)y * g_w + x, (size_t)n * 2u);
}

static uint32_t s_now_ms(void) { return sgx4 && sgx4->ms ? sgx4->ms() : 0u; }

static void s_status(const char *text)
{
    if (!sgx4 || !sgx4->text_puts || !text) return;
    if (!g_text_cleared) {
        if (sgx4->text_clear) (void)sgx4->text_clear();
        g_text_cleared = true;
    }
    char line[81];
    unsigned row = 0;
    const char *p = text;
    while (*p && row < 6u) {
        unsigned n = 0;
        while (p[n] && p[n] != '\n' && n < 80u) n++;
        memset(line, ' ', 80);
        memcpy(line, p, n);
        line[80] = 0;
        (void)sgx4->text_puts(0, (int)row, line, SGX4_T_BG_TRANS | SGX4_T_FG(15));
        row++;
        p += n;
        if (*p == '\n') p++;
    }
}

static const sgnano_app_platform_t g_platform = {
    .open = s_open,
    .read_at = s_read_at,
    .write_at = s_write_at,
    .flush = s_flush,
    .close = s_close,
    .remove = s_remove,
    .fb_put = s_fb_put,
    .fb_get = s_fb_get,
    .now_ms = s_now_ms,
    .status = s_status,
    .save_image = NULL,
};

bool sgnano_sgx_init(uint32_t width, uint32_t height)
{
    if (!sgx4 || !sgx4->fb_page) return false;
    if (sgx4->set_mode && sgx4->set_mode(SGX4_STUB_MODE_TEXT_FB) != 0) return false;
    g_fb = sgx4->fb_page(0);
    if (!g_fb) return false;
    g_w = width;
    g_h = height;
    g_text_cleared = false;
    return true;
}

const sgnano_app_platform_t *sgnano_sgx_platform(void) { return &g_platform; }

void sgnano_sgx_shutdown(void)
{
    g_fb = NULL;
    g_w = g_h = 0;
    g_text_cleared = false;
}

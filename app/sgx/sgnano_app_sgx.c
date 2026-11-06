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
static void *g_spare[4];
static unsigned g_spare_n;
static uint32_t g_spare_bytes;
static bool g_spare_enabled = true;

static uint32_t min_u32(uint32_t a, uint32_t b) { return a < b ? a : b; }

// UART だけへ（実機の切り分け用）。画面のステータスは消さない。
static void s_debug(const char *s)
{
    if (sgx4 && sgx4->log) sgx4->log(s, (unsigned)strlen(s));
}

static void *s_open(const char *path, uint32_t size, bool create)
{
    if (!sgx4_has_store()) return NULL;
    sgx_store_t *h = (sgx_store_t *)malloc(sizeof *h);
    if (!h) return NULL;
    h->block_size = 4096;
    // 実機の exFAT は 32KB などのクラスタ単位で書く。端数を残さないよう、
    // ファイルの大きさを 64KB の倍数まで丸める（使わない後ろは 0 のまま）。
    h->blocks = (size + h->block_size - 1u) / h->block_size;
    const uint32_t round_blocks = (64u * 1024u) / h->block_size;
    h->blocks = (h->blocks + round_blocks - 1u) / round_blocks * round_blocks;
    sgx_store_config_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.path = path;
    cfg.block_size = h->block_size;
    cfg.block_count = h->blocks;
    cfg.cache_blocks = 8;
    cfg.flags = SGX_STORE_WRITEBACK | (create ? SGX_STORE_CREATE : 0u);
    s_debug("store: open begin\n");
    h->id = sgx4->store_open(&cfg);
    if (h->id < 0) { s_debug("store: open failed\n"); free(h); return NULL; }
    s_debug("store: open ok\n");
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
    return h && sgx4_has_store() && sgx4->store_flush(h->id) == 0;
}

static void s_close(void *vh)
{
    sgx_store_t *h = (sgx_store_t *)vh;
    if (!h) return;
    if (sgx4_has_store()) {
        (void)sgx4->store_flush(h->id);
        (void)sgx4->store_close(h->id);
    }
    free(h);
}

static bool s_remove(const char *path) { return sgx4 && sgx4->remove && sgx4->remove(path) == 0; }

static bool s_save_bytes(const char *path, const void *data, uint32_t len)
{
    return sgx4 && sgx4->write_file && sgx4->write_file(path, data, len) == 0;
}

static bool spare_contains(const void *p)
{
    if (!g_spare_n || !g_spare_bytes) return false;
    const uint8_t *q = (const uint8_t *)p;
    for (unsigned i = 0; i < g_spare_n; i++)
        if (q >= (const uint8_t *)g_spare[i] && q < (const uint8_t *)g_spare[i] + g_spare_bytes)
            return true;
    return false;
}

static void *s_scratch_alloc(uint32_t bytes)
{
    // FB の余りページが連続していれば、PNG の RGB 作業場に使う。
    if (g_spare_n && g_spare_bytes && bytes <= g_spare_n * g_spare_bytes) {
        bool contig = true;
        for (unsigned i = 1; i < g_spare_n; i++)
            if ((uint8_t *)g_spare[i] != (uint8_t *)g_spare[i - 1] + g_spare_bytes) contig = false;
        if (contig) return g_spare[0];
    }
    return malloc(bytes);
}

static void s_scratch_free(void *p)
{
    if (!p) return;
    if (!spare_contains(p)) free(p);
}

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
        if (sgx4->text_clear) sgx4->text_clear(SGX4_T_BG_TRANS);
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
        (void)sgx4->text_puts(0, row, line, SGX4_T_BG_TRANS | SGX4_T_FG(15));
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
    .save_bytes = s_save_bytes,
    .scratch_alloc = s_scratch_alloc,
    .scratch_free = s_scratch_free,
};

// ストアを開く段は mode 1（テキストだけ）にする。mode 2 で FB を出したまま
// store_open すると、実機の HDD で exFAT の読み出しが止まることがあるため。
bool sgnano_sgx_init_text(uint32_t width, uint32_t height)
{
    if (!sgx4 || !sgx4->set_mode) return false;
    const char *e = sgx4->set_mode(SGX_MODE_TEXT);
    if (e) return false;
    g_fb = NULL;
    g_w = width;
    g_h = height;
    g_text_cleared = false;
    g_spare_n = 0;
    g_spare_bytes = 0;
    return true;
}

// 描画の段: mode 2（テキスト＋FB）にしてページ 0 を取る。
bool sgnano_sgx_enter_fb(uint32_t width, uint32_t height)
{
    if (!sgx4 || !sgx4->fb_page) return false;
    if (sgx4->set_mode) { const char *e = sgx4->set_mode(SGX4_STUB_MODE_TEXT_FB); if (e) return false; }
    g_fb = (uint16_t *)sgx4->fb_page(0);
    if (!g_fb) return false;
    g_w = width;
    g_h = height;
    g_text_cleared = false;
    return true;
}

bool sgnano_sgx_init(uint32_t width, uint32_t height)
{
    g_spare_n = 0;
    g_spare_bytes = 0;
    return sgnano_sgx_enter_fb(width, height);
}

// ストアを開いたあとに呼ぶ。FB の余りページを借りる（ABI 4.0）。
bool sgnano_sgx_lend_spare(void)
{
    if (!g_spare_enabled || !sgx4_has_fb_lend_spare()) return false;
    g_spare_n = sgx4->fb_lend_spare(0, g_spare, 4, &g_spare_bytes);
    // ファームはページを高い番地から返す（FB1, FB2, FB3）。番地の順に並べ、
    // 続いている塊として PNG の作業場に使えるようにする。
    for (unsigned i = 1; i < g_spare_n; i++) {
        void *p = g_spare[i];
        unsigned j = i;
        while (j > 0 && (uintptr_t)g_spare[j - 1] > (uintptr_t)p) {
            g_spare[j] = g_spare[j - 1];
            j--;
        }
        g_spare[j] = p;
    }
    return g_spare_n > 0;
}

void sgnano_sgx_set_spare_enabled(bool on) { g_spare_enabled = on; }

const sgnano_app_platform_t *sgnano_sgx_platform(void) { return &g_platform; }

void sgnano_sgx_shutdown(void)
{
    if (g_spare_n && sgx4 && sgx4->fb_return) sgx4->fb_return();
    for (unsigned i = 0; i < 4; i++) g_spare[i] = NULL;
    g_spare_n = 0;
    g_spare_bytes = 0;
    g_fb = NULL;
    g_w = g_h = 0;
    g_text_cleared = false;
}

void *sgnano_sgx_spare_base(void) { return g_spare_n ? g_spare[0] : NULL; }
uint32_t sgnano_sgx_spare_bytes(void) { return g_spare_n * g_spare_bytes; }

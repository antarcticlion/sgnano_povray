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

// ---- 途中経過の縮小（2026-11-07、ユーザー判断: 描いている間は SRAM のモード 7 に縮めて出し、終わったら FB の 640x480 を出す）----
// FB を出したままだと、画面が PSRAM から FB を取る間 core1 のタスク（スタックが PSRAM）が待たされ、描画が 20 倍遅かった。
// モード 7 のビットマップ（320x200x8）は SRAM にあり、取得の待ちが無い。fb_put のたびに縮めて書く（FB には今までどおり全部を書く）。
// 色は 6x7x6 = 252 色の立方体（0〜251）
#ifdef SGNANO_SGX_OFFICIAL_ABI
static uint8_t *g_bm;                 // モード 7 の表示の面（NULL = 縮小しない）
static uint32_t g_bm_sx, g_bm_sy;     // 元の 1 画素 -> 縮小の位置（16.16）
static uint32_t g_bm_ox, g_bm_oy;     // 縦横比を保って真ん中に置く
static uint8_t preview_index(uint16_t c)
{
    const unsigned r = c & 31u, g = (c >> 5) & 31u, b = (c >> 10) & 31u;   // ピン順の RGB555（bit0-4 R・5-9 G・10-14 B）
    return (uint8_t)(((r * 5u + 15u) / 31u) * 42u + ((g * 6u + 15u) / 31u) * 6u + (b * 5u + 15u) / 31u);
}
static void preview_put(uint32_t x, uint32_t y, const uint16_t *pixels, uint32_t n)
{
    if (!g_bm) return;
    const uint32_t ty = g_bm_oy + ((y * g_bm_sy) >> 16);
    if (ty >= 200u) return;
    uint8_t *row = g_bm + ty * 320u;
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t tx = g_bm_ox + (((x + i) * g_bm_sx) >> 16);
        if (tx < 320u) row[tx] = preview_index(pixels[i]);
    }
}
#else
static void preview_put(uint32_t x, uint32_t y, const uint16_t *pixels, uint32_t n) { (void)x; (void)y; (void)pixels; (void)n; }
#endif
static void *g_spare[4];
static unsigned g_spare_n;
static uint32_t g_spare_bytes;
static bool g_spare_enabled = true;
static bool g_spare_heap;   // 余りのページを malloc のヒープに渡した（sgnano_sgx_spare_to_heap）

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

// ストアの誤りをログへ（切り分け用、2026-11-07）。全体で最初の 8 回だけ
static void store_fail(const sgx_store_t *h, const char *what, uint32_t block, int rc)
{
    static unsigned shown;
    if (shown >= 8u || !sgx4 || !sgx4->log) return;
    shown++;
    char b[112];
    const int k = snprintf(b, sizeof b, "[pov] store %s (block %lu) failed: %d (store error %d)\n", what, (unsigned long)block, rc,
                           sgx4->store_error ? sgx4->store_error(h->id) : 0);
    if (k > 0) sgx4->log(b, (unsigned)k);
}

// off から n バイトを写す（write = true ならストアへ、false ならストアから）。ブロックの境目で区切る
static bool store_copy(void *vh, uint32_t off, void *buf, uint32_t n, bool write)
{
    sgx_store_t *h = (sgx_store_t *)vh;
    if (!h || (uint64_t)off + n > (uint64_t)h->blocks * h->block_size) return false;
    uint8_t *d = (uint8_t *)buf;
    while (n) {
        const uint32_t block = off / h->block_size;
        const uint32_t bo = off % h->block_size;
        const uint32_t take = min_u32(n, h->block_size - bo);
        uint8_t *p = (uint8_t *)sgx4->store_get(h->id, block);
        if (!p) { store_fail(h, write ? "get for writing" : "get for reading", block, 0); return false; }
        if (write) memcpy(p + bo, d, take); else memcpy(d, p + bo, take);
        (void)sgx4->store_release(p, write ? 1 : 0);
        off += take; d += take; n -= take;
    }
    return true;
}
static bool s_read_at(void *vh, uint32_t off, void *dst, uint32_t n) { return store_copy(vh, off, dst, n, false); }
static bool s_write_at(void *vh, uint32_t off, const void *src, uint32_t n) { return store_copy(vh, off, (void *)src, n, true); }

static bool s_flush(void *vh)
{
    sgx_store_t *h = (sgx_store_t *)vh;
    if (!h || !sgx4_has_store()) return false;
    const int r = sgx4->store_flush(h->id);
    if (r) store_fail(h, "flush", 0, r);
    return r == 0;
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
    // FB の余りページが連続していれば、PNG の RGB 作業場に使う（ヒープに渡したあとは malloc から。ページはヒープの一部）
    if (!g_spare_heap && g_spare_n && g_spare_bytes && bytes <= g_spare_n * g_spare_bytes) {
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
    preview_put(x, y, pixels, n);
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

// 保存の鍵（2026-11-07）: put_block（フロントエンドのスレッド）と tick・finish（入口のスレッド）の保存を 1 度に 1 つに。
// SGX のミューテックスは入れ子でよい。作るのは sgnano_sgx_platform() の初め（まだ 1 つのスレッドだけのとき）
#ifdef SGNANO_SGX_OFFICIAL_ABI
static sgx_mutex_t *g_app_mx;
static void s_lock(void) { if (g_app_mx) (void)sgx4->mutex_lock(g_app_mx, SGX_WAIT_FOREVER); }
static void s_unlock(void) { if (g_app_mx) sgx4->mutex_unlock(g_app_mx); }
#endif

static const sgnano_app_platform_t g_platform = {
#ifdef SGNANO_SGX_OFFICIAL_ABI
    .lock = s_lock,
    .unlock = s_unlock,
#endif
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

// 描画の段（縮小）: モード 7 にして、FB のページ 0 は表示せずに書き込み先として使う（2026-11-07）。
// 戻り値 false なら、呼び手は sgnano_sgx_enter_fb（モード 2）にする
bool sgnano_sgx_enter_preview(uint32_t width, uint32_t height)
{
#ifdef SGNANO_SGX_OFFICIAL_ABI
    if (!sgx4 || !sgx4->set_mode || !sgx4->fb_page || !sgx4->bm_vram || !sgx4->bm_palette || !sgx4->rgb) return false;
    // 4.3 より前のファームは、モード 7 では余りのページを貸さず、貸したあとモード 2 へも戻れない（呼び手はモード 2 で描く）
    if (sgx4->abi_major < 4 || (sgx4->abi_major == 4 && sgx4->abi_minor < 3)) return false;
    if (sgx4->set_mode(SGX_MODE_TEXT_BM8)) return false;
    g_fb = (uint16_t *)sgx4->fb_page(0);
    g_bm = sgx4->bm_vram();
    if (!g_fb || !g_bm) { g_bm = NULL; return false; }
    for (unsigned r = 0; r < 6u; r++)
        for (unsigned g = 0; g < 7u; g++)
            for (unsigned b = 0; b < 6u; b++)
                sgx4->bm_palette((int)(r * 42u + g * 6u + b), sgx4->rgb(r * 51u, g * 42u + (g == 6u ? 3u : 0u), b * 51u));
    for (unsigned i = 252; i < 256u; i++) sgx4->bm_palette((int)i, sgx4->rgb(0, 0, 0));
    memset(g_bm, 0, 320u * 200u);
    // 縦横比を保つ: 640x480 なら 266x200 を左右 27 ずつ空けて置く
    uint32_t s16 = (320u << 16) / width;
    const uint32_t sy16 = (200u << 16) / height;
    if (sy16 < s16) s16 = sy16;
    g_bm_sx = g_bm_sy = s16;
    g_bm_ox = (320u - ((width * s16) >> 16)) / 2u;
    g_bm_oy = (200u - ((height * s16) >> 16)) / 2u;
    g_w = width;
    g_h = height;
    g_text_cleared = false;
    return true;
#else
    (void)width; (void)height;
    return false;
#endif
}

// 描き終えたら FB（640x480）を出す。モードを替えられなければ縮小のまま（false）
bool sgnano_sgx_show_fb(void)
{
#ifdef SGNANO_SGX_OFFICIAL_ABI
    if (!g_bm) return true;
    if (!sgx4->set_mode || sgx4->set_mode(SGX4_STUB_MODE_TEXT_FB)) return false;
    g_bm = NULL;
    g_fb = (uint16_t *)sgx4->fb_page(0);
    g_text_cleared = false;
    return g_fb != NULL;
#else
    return true;
#endif
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
    // ABI 4.1 のファームは番地の小さい順に返す（2026-11-06。以前はここで並べ替えていた）
    g_spare_n = sgx4->fb_lend_spare(0, g_spare, 4, &g_spare_bytes);
    return g_spare_n > 0;
}

void sgnano_sgx_set_spare_enabled(bool on) { g_spare_enabled = on; }

// 借りた余りのページを malloc のヒープに足す（2026-11-06）。POV-Ray は描画のあいだ PSRAM が足りず（描画のスレッドのスタックが
// 取れなかった）、ページは最後の PNG の作業場にしか使っていなかった。続いているページは 1 つの置き場にまとめる。
// 足したページはプログラムの終わりまで返さない（ファームが終わりに返す）
unsigned sgnano_sgx_spare_to_heap(void)
{
    if (!g_spare_n || !g_spare_bytes || g_spare_heap) return 0;
    unsigned added = 0;
    for (unsigned i = 0; i < g_spare_n;) {
        unsigned j = i + 1;
        while (j < g_spare_n && (uint8_t *)g_spare[j] == (uint8_t *)g_spare[j - 1] + g_spare_bytes) j++;
        if (sgx_heap_add_region(g_spare[i], (j - i) * g_spare_bytes) == 0) added += (j - i) * g_spare_bytes;
        i = j;
    }
    g_spare_heap = added > 0;
    return added;
}

const sgnano_app_platform_t *sgnano_sgx_platform(void)
{
#ifdef SGNANO_SGX_OFFICIAL_ABI
    if (!g_app_mx && sgx4 && sgx4->mutex_new) g_app_mx = sgx4->mutex_new();
#endif
    return &g_platform;
}

void sgnano_sgx_shutdown(void)
{
    if (g_spare_n && !g_spare_heap && sgx4 && sgx4->fb_return) sgx4->fb_return();   // ヒープに渡したページはファームが終わりに返す
    for (unsigned i = 0; i < 4; i++) g_spare[i] = NULL;
    g_spare_n = 0;
    g_spare_bytes = 0;
    g_fb = NULL;
    g_w = g_h = 0;
    g_text_cleared = false;
}

void *sgnano_sgx_spare_base(void) { return g_spare_n ? g_spare[0] : NULL; }
uint32_t sgnano_sgx_spare_bytes(void) { return g_spare_n * g_spare_bytes; }

// 実機の SGX で、sgnano_app のチェックポイント・PNG・FB の余りページを試す。
//
//   povprobe.sgx DIR [fresh|resume|auto] [止めるまでに描く行数]
//
// DIR は書いてよいフォルダ（例 B:/POVTEST）。640x480 の試験模様を 1 行ずつ描き、
// チェックポイント（ABI 4.0 のブロックストア）へ書く。止めたら次回は続きから。
// 完成すると PNG（可逆）と RGB555 の生データを同じフォルダへ書く。
//
// 結果は UART（term_write）と DIR/probe_<mode>.txt へ。PNG と生データは
// ホスト側で突き合わせられる（tools/hw_probe.py が取ってくる）。
#include "sgx_api.h"
#include "platform/sgx/sgx4_stub.h"
#include "app/sgnano_app.h"
#include "app/sgx/sgnano_app_sgx.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 640u
#define H 480u
#define BLOCK 64u
#define RAW_BYTES (W * H * 2u)

static char g_dir[160];
static char g_ckp[200], g_png[200], g_raw[200], g_rep[200];
static uint16_t g_row[W];
static uint16_t g_got[W];
static char g_report[4096];
static size_t g_report_len;

static void say(const char *s)
{
    if (sgx && sgx->term_write) sgx->term_write(s, (unsigned)strlen(s));
}

static void sayf(const char *fmt, ...)
{
    char b[320];
    va_list ap;
    va_start(ap, fmt);
    const int n = sgx->vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (n > 0) say(b);
}

// UART だけへ（画面のステータスを消さない）。
static void plogf(const char *fmt, ...)
{
    char b[240];
    va_list ap;
    va_start(ap, fmt);
    const int n = sgx->vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (n > 0 && sgx->log) sgx->log(b, (unsigned)n);
}

static void rep(const char *fmt, ...)
{
    if (g_report_len >= sizeof g_report) return;
    va_list ap;
    va_start(ap, fmt);
    const int n = sgx->vsnprintf(g_report + g_report_len, sizeof g_report - g_report_len, fmt, ap);
    va_end(ap);
    if (n > 0) {
        const size_t used = (size_t)n < sizeof g_report - g_report_len ? (size_t)n : sizeof g_report - g_report_len - 1u;
        g_report_len += used;
        g_report[g_report_len] = 0;
    }
}

static uint32_t crc32_update(uint32_t c, const void *data, size_t n)
{
    const uint8_t *p = (const uint8_t *)data;
    while (n--) {
        c ^= *p++;
        for (unsigned k = 0; k < 8u; k++) c = (c >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(c & 1u)));
    }
    return c;
}

static uint16_t pattern_pixel(uint32_t x, uint32_t y)
{
    // x, y だけで決まる試験模様（再開しても同じ絵になる）。
    uint32_t r = (x * 255u) / (W - 1u);
    uint32_t g = (y * 255u) / (H - 1u);
    uint32_t b = ((x * 3u + y * 5u) / 8u) & 255u;

    const int dx = (int)x - 320, dy = (int)y - 240;
    if (dx * dx + dy * dy < 90 * 90) {          // 中央の円
        r = 255u - r; g = 255u - g; b = 255u - b;
    }
    if (((x / 32u) + (y / 32u)) & 1u) {         // 32x32 の市松
        r = (r + 255u) / 2u;
        b /= 2u;
    }
    if (x % 80u == 0u || y % 60u == 0u) {       // 目盛りの線
        r = 255u; g = 255u; b = 255u;
    }
    return (uint16_t)((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10));
}

static void fill_row(uint32_t y, uint16_t *out)
{
    for (uint32_t x = 0; x < W; x++) out[x] = pattern_pixel(x, y);
}

static uint32_t fb_crc(void)
{
    const sgnano_app_platform_t *p = sgnano_sgx_platform();
    uint32_t c = 0xFFFFFFFFu;
    for (uint32_t y = 0; y < H; y++) {
        p->fb_get(0, y, g_got, W);
        c = crc32_update(c, g_got, sizeof g_got);
    }
    return ~c;
}

static void verify_fb(uint32_t *bad_rows, uint32_t *bad_px)
{
    const sgnano_app_platform_t *p = sgnano_sgx_platform();
    *bad_rows = 0;
    *bad_px = 0;
    for (uint32_t y = 0; y < H; y++) {
        fill_row(y, g_row);
        p->fb_get(0, y, g_got, W);
        uint32_t mism = 0;
        for (uint32_t x = 0; x < W; x++) if (g_got[x] != g_row[x]) mism++;
        if (mism) { (*bad_rows)++; *bad_px += mism; }
    }
}

static bool file_size(const char *path, uint32_t *size)
{
    sgx_stat_t st;
    if (!sgx->stat || sgx->stat(path, &st) != 0 || st.is_dir) return false;
    *size = st.size;
    return true;
}

static bool file_head(const char *path, void *dst, uint32_t n)
{
    // 実機の read_file は 8/33/512 バイトのような小さな max だと失敗することがある。
    // ファイル全体を読んで、その先頭を使う（PNG は数十 KB）。
    if (!dst) return false;
    uint32_t size = 0;
    if (!file_size(path, &size) || size < n) return false;
    uint8_t *buf = (uint8_t *)malloc(size);
    if (!buf) return false;
    uint32_t got = 0;
    const bool ok = sgx->read_file && sgx->read_file(path, buf, size, &got) == 0 && got >= n;
    if (ok) memcpy(dst, buf, n);
    free(buf);
    return ok;
}

static uint32_t file_crc(const char *path, uint32_t *size)
{
    uint32_t n = 0;
    if (!file_size(path, &n) || !n) return 0;
    uint8_t *buf = (uint8_t *)malloc(n);
    if (!buf) return 0;
    uint32_t got = 0;
    uint32_t c = 0;
    if (sgx->read_file(path, buf, n, &got) == 0) c = ~crc32_update(0xFFFFFFFFu, buf, got);
    free(buf);
    if (size) *size = got;
    return c;
}

static uint32_t parse_u32(const char *s)
{
    uint32_t v = 0;
    while (s && *s >= '0' && *s <= '9') v = v * 10u + (uint32_t)(*s++ - '0');
    return v;
}

static void put_report(const char *path)
{
    if (!sgx->write_file) return;
    const int r = sgx->write_file(path, g_report, (uint32_t)g_report_len);
    sayf("report: %s (%s)\n", path, r == 0 ? "ok" : "ERROR");
}

static void report_drives(void)
{
#ifdef SGNANO_SGX_OFFICIAL_ABI
    sgx_drive_t d[16];
    const unsigned nd = sgx->drives ? sgx->drives(d, 16) : 0;
    sayf("drives: %u\n", nd);
    rep("drives=%u\n", nd);
    for (unsigned i = 0; i < nd; i++) {
        sayf("  %c: type=%u ready=%u\n", d[i].letter, d[i].type, d[i].ready ? 1u : 0u);
        rep("drive %c type=%u ready=%u\n", d[i].letter, d[i].type, d[i].ready ? 1u : 0u);
        if (g_dir[0] == d[i].letter)
            rep("target_drive=%c target_type=%u\n", d[i].letter, d[i].type);
    }
    // USB ストレージの容量（0〜3。HDD は 152D:0578）。
    for (int i = 0; i < 4; i++) {
        sgx_storage_t st;
        memset(&st, 0, sizeof st);
        if (!sgx->storage || !sgx->storage(i, &st)) continue;
        sayf("usb%u: %s %s %lu MB\n", (unsigned)i, st.vendor, st.product, (unsigned long)st.mb);
        rep("usb%d vendor=%s product=%s mb=%lu\n", i, st.vendor, st.product, (unsigned long)st.mb);
    }
#endif
}

static void report_memory(void)
{
#ifdef SGNANO_SGX_OFFICIAL_ABI
    sgx_mem_info_t m;
    memset(&m, 0, sizeof m);
    if (sgx->mem_info) sgx->mem_info(&m);
    sayf("psram free %lu largest %lu; sram free %lu largest %lu\n",
         (unsigned long)m.psram_free, (unsigned long)m.psram_largest,
         (unsigned long)m.sram_free, (unsigned long)m.sram_largest);
    rep("psram_free=%lu psram_largest=%lu sram_free=%lu sram_largest=%lu\n",
        (unsigned long)m.psram_free, (unsigned long)m.psram_largest,
        (unsigned long)m.sram_free, (unsigned long)m.sram_largest);
#endif
}

static void report_spare(void)
{
    const uint32_t bytes = sgnano_sgx_spare_bytes();
    void *base = sgnano_sgx_spare_base();
    sayf("fb_lend_spare: %lu B at %p\n", (unsigned long)bytes, base);
    rep("spare_bytes=%lu spare_base=%p\n", (unsigned long)bytes, base);

    // PNG の RGB 作業場（640x480x3）が余りページに入るか。
    const sgnano_app_platform_t *p = sgnano_sgx_platform();
    const uint32_t need = W * H * 3u;
    void *s = p->scratch_alloc ? p->scratch_alloc(need) : NULL;
    const bool on_spare = s && base && (const uint8_t *)s >= (const uint8_t *)base &&
                          (const uint8_t *)s < (const uint8_t *)base + bytes;
    sayf("png scratch %lu B: %s\n", (unsigned long)need, on_spare ? "spare page" : "heap");
    rep("scratch_bytes=%lu scratch_on_spare=%u\n", (unsigned long)need, on_spare ? 1u : 0u);
    if (p->scratch_free && s) p->scratch_free(s);
}

// ブロックストアを開く前に、同じ大きさの普通のファイル書き込みを試す。
// ここで止まれば FS/USB の側、ここが通れば store_open の中。
// チェックポイントのファイルを先に普通の書き込みで作る。
// store_open の CREATE（0 で埋める流し書き）は mode 2 で止まることがあるため、
// 既にあるファイルを開く道（create_zero を通らない）を使う。
static bool precreate_checkpoint(void)
{
    sgx_stat_t st;
    if (sgx->stat && sgx->stat(g_ckp, &st) == 0 && !st.is_dir) return true;
    const uint32_t n = 655360u;                 // 64KB の倍数（s_open の丸めと同じ）
#ifdef SGNANO_SGX_OFFICIAL_ABI
    void *p = sgx->psram_alloc ? sgx->psram_alloc(n, 8) : NULL;
#else
    void *p = malloc(n);
#endif
    if (!p) { say("precreate: no memory\n"); rep("precreate=NOMEM\n"); return false; }
    memset(p, 0, n);
    sayf("precreate: %s (%lu B) ...\n", g_ckp, (unsigned long)n);
    const uint32_t t0 = sgx->ms ? sgx->ms() : 0u;
    const int r = sgx->write_file ? sgx->write_file(g_ckp, p, n) : -1;
    const uint32_t dt = (sgx->ms ? sgx->ms() : 0u) - t0;
#ifdef SGNANO_SGX_OFFICIAL_ABI
    if (sgx->psram_free) sgx->psram_free(p);
#else
    free(p);
#endif
    sayf("precreate: %s %lu ms\n", r == 0 ? "ok" : "ERROR", (unsigned long)dt);
    rep("precreate=%s precreate_ms=%lu\n", r == 0 ? "ok" : "ERROR", (unsigned long)dt);
    return r == 0;
}

static void preflight_write(void)
{
    char path[200];
    snprintf(path, sizeof path, "%s/probe_seed.bin", g_dir);
    const uint32_t n = 655360u;                 // 64KB の倍数（s_open の丸めと同じ）
#ifdef SGNANO_SGX_OFFICIAL_ABI
    void *p = sgx->psram_alloc ? sgx->psram_alloc(n, 8) : NULL;
#else
    void *p = malloc(n);
#endif
    if (!p) { say("preflight: no memory\n"); rep("preflight_write=NOMEM\n"); return; }
    memset(p, 0, n);
    sayf("preflight: writing %lu B ...\n", (unsigned long)n);
    const uint32_t t0 = sgx->ms ? sgx->ms() : 0u;
    const int r = sgx->write_file ? sgx->write_file(path, p, n) : -1;
    const uint32_t dt = (sgx->ms ? sgx->ms() : 0u) - t0;
#ifdef SGNANO_SGX_OFFICIAL_ABI
    if (sgx->psram_free) sgx->psram_free(p);
#else
    free(p);
#endif
    (void)sgx->remove(path);
    sayf("preflight: %s %lu ms\n", r == 0 ? "ok" : "ERROR", (unsigned long)dt);
    rep("preflight_write=%s preflight_ms=%lu\n", r == 0 ? "ok" : "ERROR", (unsigned long)dt);
}

int sgx_main(const sgx_api_t *api, int argc, char **argv)
{
    (void)api;
    if (argc < 2) {
        say("usage: povprobe.sgx DIR [fresh|resume|auto] [stop_lines]\n");
        return 2;
    }
    snprintf(g_dir, sizeof g_dir, "%s", argv[1]);
    const char *mode = argc > 2 ? argv[2] : "auto";
    const uint32_t stop_lines = argc > 3 ? parse_u32(argv[3]) : 0u;
    const bool fresh = !strcmp(mode, "fresh");
    const bool resume = strcmp(mode, "fresh") != 0;
    if (argc > 4 && !strcmp(argv[4], "nospare")) sgnano_sgx_set_spare_enabled(false);
    const bool hide_fb = argc > 4 && !strcmp(argv[4], "hidefb");

    snprintf(g_ckp, sizeof g_ckp, "%s/probe.ckp", g_dir);
    snprintf(g_png, sizeof g_png, "%s/probe_%s.png", g_dir, fresh ? "fresh" : "resume");
    snprintf(g_raw, sizeof g_raw, "%s/probe_%s.raw", g_dir, fresh ? "fresh" : "resume");
    snprintf(g_rep, sizeof g_rep, "%s/probe_%s.txt", g_dir, fresh ? "fresh" : "resume");

    rep("POVPROBE ABI %u.%u\n", SGX_ABI_MAJOR, SGX_ABI_MINOR);
    rep("dir=%s mode=%s stop=%lu\n", g_dir, mode, (unsigned long)stop_lines);
    sayf("\nPOVPROBE: %s (%s)%s\n", g_dir, mode, stop_lines ? " with stop" : "");

    if (!sgx4_has_store()) {
        say("firmware has no block store (ABI 4.0)\n");
        return 1;
    }
    if (!sgnano_sgx_init_text(W, H)) {
        say("mode 1 failed\n");
        return 1;
    }

    report_drives();
    report_memory();
    preflight_write();

    if (fresh) {
        (void)sgx->remove(g_ckp);
        (void)sgx->remove(g_png);
        (void)sgx->remove(g_raw);
    }
    if (!precreate_checkpoint()) {
        say("precreate_checkpoint failed\n");
        sgnano_sgx_shutdown();
        return 1;
    }

    sgnano_app_config_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.scene_path = "povprobe";
    cfg.checkpoint_path = g_ckp;
    cfg.image_path = g_png;
    cfg.width = W;
    cfg.height = H;
    cfg.block_size = BLOCK;
    cfg.scene_hash = 0x50564F31u;      // "POV1"
    cfg.options_hash = (W << 16) | H;
    cfg.outputs = SGNANO_OUTPUT_FB | SGNANO_OUTPUT_PNG;
    cfg.resume = resume;
    cfg.defer_fb_restore = true;

#ifdef SGNANO_SGX_OFFICIAL_ABI
    if (hide_fb && sgx->set_fb_visible) {
        sgx->set_fb_visible(false);
        say("fb: hidden for store open\n");
    }
#endif
    sayf("store: opening %s ...\n", g_ckp);
    if (!sgnano_app_init(&cfg, sgnano_sgx_platform())) {
        say("sgnano_app_init failed (store_open?)\n");
        sgnano_sgx_shutdown();
        return 1;
    }
    say("store: open ok; entering mode 2\n");
    if (!sgnano_sgx_enter_fb(W, H)) {
        say("mode 2 / fb_page failed\n");
        sgnano_sgx_shutdown();
        return 1;
    }
    if (!sgnano_app_restore_fb()) {
        say("fb restore failed\n");
        sgnano_sgx_shutdown();
        return 1;
    }
#ifdef SGNANO_SGX_OFFICIAL_ABI
    if (hide_fb && sgx->set_fb_visible) {
        sgx->set_fb_visible(true);
        say("fb: shown\n");
    }
#endif

    uint32_t ckp_size = 0;
    (void)file_size(g_ckp, &ckp_size);
    const uint32_t start = sgnano_app_resume_row();
    sayf("store: open ok; start row %lu, lines done %lu, checkpoint %lu B\n",
         (unsigned long)start, (unsigned long)sgnano_app_lines_done(), (unsigned long)ckp_size);
    rep("start_row=%lu lines_done=%lu ckp_bytes=%lu\n",
        (unsigned long)start, (unsigned long)sgnano_app_lines_done(), (unsigned long)ckp_size);

    // ストアを開いたあとで FB の余りページを借りる（切り分けの順番）。
    const bool lent = sgnano_sgx_lend_spare();
    sayf("fb_lend_spare: %s\n", lent ? "ok" : "none");
    report_spare();

    uint32_t drawn = 0;
    const uint32_t t0 = sgx->ms();
    for (uint32_t y = start; y < H; y++) {
        fill_row(y, g_row);
        sgnano_app_put_block(0, y, W - 1u, y, g_row);
        drawn++;
        if ((drawn & 15u) == 0u) {
            const uint32_t done = sgnano_app_lines_done();
            const uint32_t pm = (uint32_t)(((uint64_t)done * 1000u) / H);
            const uint32_t el = sgx->ms() - t0;
            plogf("progress %lu/%u (%lu.%lu%%) %lu ms\n",
                  (unsigned long)done, H, (unsigned long)(pm / 10u), (unsigned long)(pm % 10u),
                  (unsigned long)el);
        }
        if (stop_lines && drawn >= stop_lines) {
            sayf("stopping after %lu new lines (line %lu done)\n",
                 (unsigned long)drawn, (unsigned long)sgnano_app_lines_done());
            break;
        }
        if ((drawn & 31u) == 0u && sgx->stop_requested()) {
            say("stop requested\n");
            break;
        }
    }

    const bool complete = sgnano_app_all_done();
    sayf("drawn %lu, lines done %lu/%u, %s\n", (unsigned long)drawn,
         (unsigned long)sgnano_app_lines_done(), H, complete ? "complete" : "paused");
    rep("drawn=%lu lines_done=%lu complete=%u\n", (unsigned long)drawn,
        (unsigned long)sgnano_app_lines_done(), complete ? 1u : 0u);

    if (complete) {
        uint32_t bad_rows = 0, bad_px = 0;
        verify_fb(&bad_rows, &bad_px);
        const uint32_t crc = fb_crc();
        sayf("fb verify: %lu bad rows, %lu bad pixels; crc %08lx\n",
             (unsigned long)bad_rows, (unsigned long)bad_px, (unsigned long)crc);
        rep("fb_bad_rows=%lu fb_bad_px=%lu fb_crc=%08lx\n",
            (unsigned long)bad_rows, (unsigned long)bad_px, (unsigned long)crc);

        sgnano_app_finish(true);

        // RGB555 の生データも書く（PNG と突き合わせるため）。
        uint8_t *raw = (uint8_t *)malloc(RAW_BYTES);
        if (raw) {
            const sgnano_app_platform_t *p = sgnano_sgx_platform();
            for (uint32_t y = 0; y < H; y++) {
                p->fb_get(0, y, g_row, W);
                memcpy(raw + (size_t)y * W * 2u, g_row, W * 2u);
            }
            const uint32_t raw_crc = ~crc32_update(0xFFFFFFFFu, raw, RAW_BYTES);
            const int wr = sgx->write_file ? sgx->write_file(g_raw, raw, RAW_BYTES) : -1;
            sayf("raw: %s %lu B crc %08lx\n", wr == 0 ? "ok" : "ERROR",
                 (unsigned long)RAW_BYTES, (unsigned long)raw_crc);
            rep("raw=%s bytes=%lu crc=%08lx\n", wr == 0 ? "ok" : "ERROR",
                (unsigned long)RAW_BYTES, (unsigned long)raw_crc);
            free(raw);
        } else {
            say("raw: out of memory\n");
            rep("raw=ENOMEM\n");
        }

        uint32_t png_size = 0, png_crc = 0;
        uint8_t head[33];
        const bool sig = file_head(g_png, head, 8) && head[0] == 0x89 && head[1] == 'P' &&
                         head[2] == 'N' && head[3] == 'G';
        const bool ihdr = file_head(g_png, head, 33) && !memcmp(head + 12, "IHDR", 4);
        const uint32_t pw = ihdr ? ((uint32_t)head[16] << 24) | ((uint32_t)head[17] << 16) |
                                   ((uint32_t)head[18] << 8) | head[19] : 0;
        const uint32_t ph = ihdr ? ((uint32_t)head[20] << 24) | ((uint32_t)head[21] << 16) |
                                   ((uint32_t)head[22] << 8) | head[23] : 0;
        png_crc = file_crc(g_png, &png_size);
        sayf("png: %s %lu B %lux%lu crc %08lx%s\n", sig ? "ok" : "MISSING",
             (unsigned long)png_size, (unsigned long)pw, (unsigned long)ph,
             (unsigned long)png_crc, ihdr ? "" : " (no IHDR)");
        rep("png=%s bytes=%lu w=%lu h=%lu crc=%08lx\n", sig ? "ok" : "ERROR",
            (unsigned long)png_size, (unsigned long)pw, (unsigned long)ph, (unsigned long)png_crc);

        say("holding the result on screen for 3 s...\n");
        if (sgx->sleep_us) sgx->sleep_us(3000000u);
    } else {
        sgnano_app_finish(false);
        say("checkpoint kept; run again with resume\n");
    }

    ckp_size = 0;                       // 消えていれば 0 のまま
    (void)file_size(g_ckp, &ckp_size);
    rep("final_ckp_bytes=%lu\n", (unsigned long)ckp_size);
    sayf("final checkpoint %lu B\n", (unsigned long)ckp_size);
    rep("RESULT: %s\n", complete ? "PASS" : "PAUSED");
    sayf("RESULT: %s\n", complete ? "PASS" : "PAUSED");
    put_report(g_rep);
    sgnano_sgx_shutdown();
    return 0;
}

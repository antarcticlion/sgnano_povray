// sgnano POV-Ray アプリの共通層。説明は sgnano_app.h。
//
// チェックポイントは「ヘッダ 256 バイト＋1 ラインあたり width*2 バイト」の
// 一時ファイル。確定したラインだけを書き、ヘッダのビットマップで覚える。
// 途中で終了しても、次回は最初の未完成ラインから再開できる。
#include "app/sgnano_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CKP_BITMAP_BYTES ((SGNANO_CKP_MAX_LINES + 7u) / 8u)

typedef struct {
    char     magic[8];
    uint32_t version;
    uint32_t width, height;
    uint32_t line_bytes;
    uint32_t block_size;
    uint32_t scene_hash;
    uint32_t options_hash;
    uint32_t lines_done;
    uint32_t first_missing;
    uint32_t updated_ms;
    uint8_t  bitmap[CKP_BITMAP_BYTES];
    uint8_t  reserved[SGNANO_CKP_HEADER_BYTES - 8u - 10u * 4u - CKP_BITMAP_BYTES];
} ckp_header_t;

_Static_assert(sizeof(ckp_header_t) == SGNANO_CKP_HEADER_BYTES, "checkpoint header size");

typedef struct {
    sgnano_app_config_t   cfg;
    sgnano_app_platform_t plat;
    void                 *fh;
    ckp_header_t          hdr;
    uint32_t              file_size;
    uint32_t              cov_bytes;
    uint32_t              cells_per_line;
    uint8_t              *coverage;          // height * cov_bytes
    uint16_t             *linebuf;           // width
    uint16_t              line_cells_done[SGNANO_CKP_MAX_LINES];
    uint64_t              start_ms;
    uint64_t              last_status_ms;
    uint64_t              last_line_ms;
    uint32_t              last_line_no;
    bool                  inited;
    bool                  ckp_error;
    bool                  image_saved;
    bool                  ckp_removed;
} app_state_t;

static app_state_t A;

static uint64_t now_ms(void) { return A.plat.now_ms ? (uint64_t)A.plat.now_ms() : 0u; }

static bool bit_get(const uint8_t *bm, uint32_t i) { return (bm[i >> 3] & (uint8_t)(1u << (i & 7u))) != 0; }
static void bit_set(uint8_t *bm, uint32_t i)      { bm[i >> 3] |= (uint8_t)(1u << (i & 7u)); }
static void bit_clr(uint8_t *bm, uint32_t i)      { bm[i >> 3] &= (uint8_t)~(1u << (i & 7u)); }

static const char *base_name(const char *p)
{
    if (!p) return "?";
    const char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}

static void hms(char *b, size_t n, uint64_t ms)
{
    uint64_t s = ms / 1000u;
    snprintf(b, n, "%02u:%02u:%02u", (unsigned)(s / 3600u), (unsigned)((s / 60u) % 60u), (unsigned)(s % 60u));
}

const char *sgnano_app_status_text(void)
{
    static char b[512];
    char el[16], eta[16];
    const uint32_t total = A.cfg.height;
    const uint32_t done = A.hdr.lines_done;
    const uint64_t t = now_ms();
    hms(el, sizeof el, t - A.start_ms);
    hms(eta, sizeof eta, (total > done && done) ? ((t - A.start_ms) / done) * (uint64_t)(total - done) : 0u);
    const double pct = total ? 100.0 * (double)done / (double)total : 0.0;
    const double msline = done ? (double)(t - A.start_ms) / (double)done : 0.0;
    snprintf(b, sizeof b,
             "POV-Ray 3.7  %s\n"
             "line %u/%u (%.1f%%)  first missing %u\n"
             "elapsed %s  ETA %s  %.1f ms/line\n"
             "checkpoint: %s  %s",
             base_name(A.cfg.scene_path), (unsigned)done, (unsigned)total, pct,
             (unsigned)A.hdr.first_missing, el, eta, msline,
             A.cfg.checkpoint_path ? A.cfg.checkpoint_path : "(none)",
             A.ckp_error ? "ERROR" : (done >= total ? "complete" : "ok"));
    return b;
}

static void maybe_status(bool force)
{
    const uint64_t t = now_ms();
    if (!force && t - A.last_status_ms < 1000u) return;
    A.last_status_ms = t;
    if (A.plat.status) A.plat.status(sgnano_app_status_text());
}

static bool write_header(void)
{
    A.hdr.updated_ms = now_ms();
    if (!A.plat.write_at(A.fh, 0, &A.hdr, sizeof A.hdr)) { A.ckp_error = true; return false; }
    return true;
}

// 全ラインが揃ったら、画像を保存してチェックポイントを消す。
// ここではファイルハンドルを閉じるだけで、状態とバッファは sgnano_app_finish まで残す。
static void finalize_if_done(void)
{
    if (!A.inited || A.hdr.lines_done < A.cfg.height) return;
    if (!A.image_saved) {
        if (A.cfg.image_path && A.plat.save_image) A.plat.save_image(A.cfg.image_path);
        A.image_saved = true;
    }
    if (!A.ckp_removed) {
        if (A.fh && A.plat.close) { A.plat.close(A.fh); A.fh = NULL; }
        if (A.cfg.checkpoint_path && A.plat.remove) (void)A.plat.remove(A.cfg.checkpoint_path);
        A.ckp_removed = true;
    }
}

static void checkpoint_line(uint32_t y)
{
    if (A.ckp_error || y >= A.cfg.height) return;
    if (bit_get(A.hdr.bitmap, y)) return;
    if (!A.plat.fb_get) { A.ckp_error = true; return; }
    A.plat.fb_get(0, y, A.linebuf, A.cfg.width);
    const uint32_t off = SGNANO_CKP_HEADER_BYTES + y * A.hdr.line_bytes;
    if (!A.plat.write_at(A.fh, off, A.linebuf, A.hdr.line_bytes)) { A.ckp_error = true; return; }
    bit_set(A.hdr.bitmap, y);
    A.hdr.lines_done++;
    while (A.hdr.first_missing < A.cfg.height && bit_get(A.hdr.bitmap, A.hdr.first_missing))
        A.hdr.first_missing++;
    if (!write_header() || !A.plat.flush(A.fh)) A.ckp_error = true;
    A.last_line_ms = now_ms();
    A.last_line_no = y;
    maybe_status(A.hdr.first_missing >= A.cfg.height);
    finalize_if_done();
}

static void mark_block(uint32_t x1, uint32_t y, uint32_t x2)
{
    const uint32_t c0 = x1 / A.cfg.block_size;
    const uint32_t c1 = x2 / A.cfg.block_size;
    uint8_t *cov = A.coverage + (size_t)y * A.cov_bytes;
    for (uint32_t c = c0; c <= c1 && c < A.cells_per_line; c++) {
        if (cov[c >> 3] & (uint8_t)(1u << (c & 7u))) continue;
        cov[c >> 3] |= (uint8_t)(1u << (c & 7u));
        if (A.line_cells_done[y] < A.cells_per_line) A.line_cells_done[y]++;
    }
    if (A.line_cells_done[y] >= A.cells_per_line && !bit_get(A.hdr.bitmap, y))
        checkpoint_line(y);
}

bool sgnano_app_init(const sgnano_app_config_t *cfg, const sgnano_app_platform_t *plat)
{
    if (!cfg || !plat || !plat->open || !plat->read_at || !plat->write_at || !plat->flush ||
        !plat->close || !plat->fb_put || !plat->fb_get || !plat->now_ms)
        return false;
    if (!cfg->width || !cfg->height || cfg->width > SGNANO_CKP_MAX_WIDTH || cfg->height > SGNANO_CKP_MAX_LINES)
        return false;
    if (cfg->block_size < 1u) return false;

    memset(&A, 0, sizeof A);
    A.cfg = *cfg;
    A.plat = *plat;
    A.cfg.block_size = cfg->block_size;
    A.cells_per_line = (cfg->width + cfg->block_size - 1u) / cfg->block_size;
    A.cov_bytes = (A.cells_per_line + 7u) / 8u;
    A.file_size = SGNANO_CKP_HEADER_BYTES + cfg->height * cfg->width * 2u;
    A.coverage = (uint8_t *)calloc((size_t)cfg->height * A.cov_bytes, 1);
    A.linebuf = (uint16_t *)malloc((size_t)cfg->width * 2u);
    if (!A.coverage || !A.linebuf) goto fail;

    A.fh = A.plat.open(A.cfg.checkpoint_path, A.file_size, true);
    if (!A.fh) goto fail;

    bool valid = false;
    if (cfg->resume) {
        ckp_header_t h;
        if (A.plat.read_at(A.fh, 0, &h, sizeof h) &&
            memcmp(h.magic, SGNANO_CKP_MAGIC, 8) == 0 &&
            h.version == SGNANO_CKP_VERSION &&
            h.width == cfg->width && h.height == cfg->height &&
            h.line_bytes == cfg->width * 2u &&
            h.block_size == cfg->block_size &&
            h.scene_hash == cfg->scene_hash &&
            h.options_hash == cfg->options_hash &&
            h.lines_done <= cfg->height && h.first_missing <= cfg->height) {
            A.hdr = h;
            valid = true;
        }
    }

    if (!valid) {
        memset(&A.hdr, 0, sizeof A.hdr);
        memcpy(A.hdr.magic, SGNANO_CKP_MAGIC, 8);
        A.hdr.version = SGNANO_CKP_VERSION;
        A.hdr.width = cfg->width;
        A.hdr.height = cfg->height;
        A.hdr.line_bytes = cfg->width * 2u;
        A.hdr.block_size = cfg->block_size;
        A.hdr.scene_hash = cfg->scene_hash;
        A.hdr.options_hash = cfg->options_hash;
        A.hdr.lines_done = 0;
        A.hdr.first_missing = 0;
        if (!write_header()) goto fail;
        const uint8_t z = 0;
        if (!A.plat.write_at(A.fh, A.file_size - 1u, &z, 1) || !A.plat.flush(A.fh)) goto fail;
    } else {
        // 既に描けたラインを FB へ戻す。ビットマップから数え直す。
        uint32_t cnt = 0, first = cfg->height;
        for (uint32_t y = 0; y < cfg->height; y++) {
            if (!bit_get(A.hdr.bitmap, y)) {
                if (first == cfg->height) first = y;
                continue;
            }
            cnt++;
            const uint32_t off = SGNANO_CKP_HEADER_BYTES + y * A.hdr.line_bytes;
            if (!A.plat.read_at(A.fh, off, A.linebuf, A.hdr.line_bytes)) { A.ckp_error = true; continue; }
            A.plat.fb_put(0, y, A.linebuf, cfg->width);
            memset(A.coverage + (size_t)y * A.cov_bytes, 0xFF, A.cov_bytes);
            A.line_cells_done[y] = (uint16_t)A.cells_per_line;
        }
        A.hdr.lines_done = cnt;
        A.hdr.first_missing = first;
    }

    A.start_ms = now_ms();
    A.last_status_ms = 0;
    A.inited = true;
    maybe_status(true);
    return true;

fail:
    if (A.fh && A.plat.close) A.plat.close(A.fh);
    free(A.coverage); free(A.linebuf);
    memset(&A, 0, sizeof A);
    return false;
}

uint32_t sgnano_app_resume_row(void) { return A.inited ? A.hdr.first_missing : 0u; }
uint32_t sgnano_app_lines_done(void) { return A.inited ? A.hdr.lines_done : 0u; }
bool sgnano_app_all_done(void) { return A.inited && A.hdr.lines_done >= A.cfg.height; }

void sgnano_app_put_block(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, const uint16_t *pixels)
{
    if (!A.inited || !pixels) return;
    if (x1 >= A.cfg.width || y1 >= A.cfg.height) return;
    const uint32_t src_w = x2 - x1 + 1u;   // 呼び手の行の幅（clamp 前）
    if (x2 >= A.cfg.width) x2 = A.cfg.width - 1u;
    if (y2 >= A.cfg.height) y2 = A.cfg.height - 1u;
    if (x1 > x2 || y1 > y2) return;
    const uint32_t copy_w = x2 - x1 + 1u;
    for (uint32_t y = y1; y <= y2; y++) {
        A.plat.fb_put(x1, y, pixels + (size_t)(y - y1) * src_w, copy_w);
        mark_block(x1, y, x2);
    }
    maybe_status(false);
}

void sgnano_app_put_rect(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, uint16_t colour)
{
    if (!A.inited) return;
    if (x1 >= A.cfg.width || y1 >= A.cfg.height) return;
    if (x2 >= A.cfg.width) x2 = A.cfg.width - 1u;
    if (y2 >= A.cfg.height) y2 = A.cfg.height - 1u;
    if (x1 > x2 || y1 > y2) return;
    const uint32_t w = x2 - x1 + 1u;
    for (uint32_t i = 0; i < w; i++) A.linebuf[i] = colour;
    for (uint32_t y = y1; y <= y2; y++) A.plat.fb_put(x1, y, A.linebuf, w);
    maybe_status(false);
}

void sgnano_app_tick(void)
{
    if (!A.inited) return;
    maybe_status(false);
}

void sgnano_app_finish(bool success)
{
    (void)success;
    if (!A.inited) return;
    finalize_if_done();
    maybe_status(true);
    if (A.fh) A.plat.close(A.fh);
    A.fh = NULL;
    free(A.coverage); A.coverage = NULL;
    free(A.linebuf); A.linebuf = NULL;
    A.inited = false;
}

uint32_t sgnano_app_fnv1a(const void *data, size_t n, uint32_t seed)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t h = seed ? seed : 2166136261u;
    while (n--) { h ^= *p++; h *= 16777619u; }
    return h;
}

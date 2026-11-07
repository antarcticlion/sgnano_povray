// sgnano POV-Ray アプリの共通層。説明は sgnano_app.h。
//
// チェックポイントは「ヘッダ 256 バイト＋1 ラインあたり width*2 バイト」の
// 一時ファイル。確定したラインだけを書き、ヘッダのビットマップで覚える。
// 途中で終了しても、次回は最初の未完成ラインから再開できる。
#include "app/sgnano_app.h"
#include "app/sgnano_png.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CKP_BITMAP_BYTES ((SGNANO_CKP_MAX_LINES + 7u) / 8u)

typedef struct {
    char     magic[8];
    uint32_t version;
    uint32_t generation;
    uint32_t width, height;
    uint32_t line_bytes;
    uint32_t block_size;
    uint32_t scene_hash;
    uint32_t options_hash;
    uint32_t lines_done;
    uint32_t first_missing;
    uint32_t updated_ms;
    uint8_t  bitmap[CKP_BITMAP_BYTES];
    uint32_t crc;
    uint8_t  reserved[SGNANO_CKP_COPY_BYTES - 8u - 11u * 4u - CKP_BITMAP_BYTES - 4u];
} ckp_header_t;

_Static_assert(sizeof(ckp_header_t) == SGNANO_CKP_COPY_BYTES, "checkpoint header size");

// チェックポイントが失敗した所（sgnano_app_debug の why。状態の行・ログで使う番号なので値は変えない）
enum {
    CKP_WHY_NONE = 0,
    CKP_WHY_HEADER_WRITE = 1,   // ヘッダを書けない
    CKP_WHY_NO_FB_GET = 2,      // （以前の作り）FB を読む口が無い
    CKP_WHY_LINE_WRITE = 3,     // 行の中身を書けない
    CKP_WHY_RESUME_READ = 4,    // 再開: 行を読めない（init）
    CKP_WHY_RESTORE_READ = 5,   // 再開: 行を読めない（restore_fb）
    CKP_WHY_HEADER_FLUSH = 6,   // ヘッダを書いたあとの flush
    CKP_WHY_LINE_FLUSH = 7,     // 行の中身を書いたあとの flush
};

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
    uint8_t               drawn[CKP_BITMAP_BYTES];   // 描き終えた行の印
    uint8_t               flushing[CKP_BITMAP_BYTES]; // 中身を書いて flush を待っている行（save_pending の中だけ）
    bool                  lines_new;    // 描き終えた行が増えた（put_block の終わりにまとめて保存する）
    uint32_t              lines_drawn;   // 描き終えた行（チェックポイントに書けたかによらない。進みの表示と仕上げに使う。2026-11-06）
    uint64_t              start_ms;
    uint64_t              last_status_ms;
    uint64_t              last_line_ms;
    uint32_t              last_line_no;
    bool                  inited;
    bool                  ckp_error;    // いちばん新しい保存が失敗した（次の保存が通れば下ろす。2026-11-07 から固定しない）
    int                   ckp_why;      // 切り分け用: 最初にチェックポイントが失敗した所（2026-11-06）
    int                   ckp_last_why; // いちばん新しい失敗の所
    uint32_t              ckp_fails;    // 失敗した回数
    uint64_t              ckp_retry_ms; // この時刻までは保存をやり直さない（失敗のたびに 1 -> 2 -> 4 ... 10 秒）
    uint32_t              ckp_backoff_ms;
    bool                  hdr_dirty;    // 行の中身は書けたが、ヘッダをまだ書けていない
    uint32_t              puts;         // 切り分け用: put_block の回数
    bool                  image_saved;
    bool                  image_error;
    bool                  ckp_removed;
    bool                  partial_saved;   // 途中までの PNG を書いた
} app_state_t;

static app_state_t A;

static uint64_t now_ms(void) { return A.plat.now_ms ? (uint64_t)A.plat.now_ms() : 0u; }
static void lk(void) { if (A.plat.lock) A.plat.lock(); }
static void ulk(void) { if (A.plat.unlock) A.plat.unlock(); }

static bool bit_get(const uint8_t *bm, uint32_t i) { return (bm[i >> 3] & (uint8_t)(1u << (i & 7u))) != 0; }
static void bit_set(uint8_t *bm, uint32_t i)      { bm[i >> 3] |= (uint8_t)(1u << (i & 7u)); }
static void bit_clr(uint8_t *bm, uint32_t i)      { bm[i >> 3] &= (uint8_t)~(1u << (i & 7u)); }

static uint32_t crc32_bytes(const void *data, size_t n)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *p++;
        for (unsigned k = 0; k < 8u; k++) c = (c >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(c & 1u)));
    }
    return ~c;
}

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
    const uint32_t done = A.lines_drawn;   // 描けた行（チェックポイントの行 A.hdr.lines_done とは別に数える）
    const uint64_t t = now_ms();
    hms(el, sizeof el, t - A.start_ms);
    hms(eta, sizeof eta, (total > done && done) ? ((t - A.start_ms) / done) * (uint64_t)(total - done) : 0u);
    const double pct = total ? 100.0 * (double)done / (double)total : 0.0;
    const double msline = done ? (double)(t - A.start_ms) / (double)done : 0.0;
    char bar[41];
    const unsigned filled = total ? (unsigned)((done * 40u) / total) : 0u;
    for (unsigned i = 0; i < 40u; i++) bar[i] = i < filled ? '#' : '-';
    bar[40] = 0;
    snprintf(b, sizeof b,
             "POV-Ray 3.7  %s\n"
             "line %u/%u (%.1f%%)  first missing %u\n"
             "[%s] %5.1f%%\n"
             "elapsed %s  ETA %s  %.1f ms/line\n"
             "checkpoint: %s  %s (%u saved%s)  png=%s",
             base_name(A.cfg.scene_path), (unsigned)done, (unsigned)total, pct,
             (unsigned)A.hdr.first_missing, bar, pct, el, eta, msline,
             A.cfg.checkpoint_path ? base_name(A.cfg.checkpoint_path) : "(none)",
             A.ckp_error ? "RETRYING" : (A.hdr.lines_done >= total ? "complete" : "ok"), (unsigned)A.hdr.lines_done,
             A.ckp_fails ? ", errors seen" : "",
             (const char *)((A.cfg.outputs & SGNANO_OUTPUT_PNG) ? (A.image_error ? "ERROR" : (A.image_saved ? "ok" : "-")) : "off"));
    return b;
}

static void maybe_status(bool force)
{
    const uint64_t t = now_ms();
    if (!force && t - A.last_status_ms < 1000u) return;
    A.last_status_ms = t;
    if (A.plat.status) A.plat.status(sgnano_app_status_text());
}

static bool header_valid(const ckp_header_t *h)
{
    if (memcmp(h->magic, SGNANO_CKP_MAGIC, 8) != 0) return false;
    if (h->version != SGNANO_CKP_VERSION) return false;
    if (h->width != A.cfg.width || h->height != A.cfg.height) return false;
    if (h->line_bytes != A.cfg.width * 2u || h->block_size != A.cfg.block_size) return false;
    if (h->scene_hash != A.cfg.scene_hash || h->options_hash != A.cfg.options_hash) return false;
    if (h->lines_done > A.cfg.height || h->first_missing > A.cfg.height) return false;
    ckp_header_t t = *h;
    const uint32_t want = t.crc;
    t.crc = 0;
    return crc32_bytes(&t, sizeof t) == want;
}

// 失敗を覚える（2026-11-07: 固定しない。少し待ってやり直す。行の中身は FB のページ 0 に残っているので、あとから書き直せる）
static void ckp_failed(int why)
{
    if (!A.ckp_why) A.ckp_why = why;
    A.ckp_last_why = why;
    A.ckp_error = true;
    A.ckp_fails++;
    A.ckp_backoff_ms = A.ckp_backoff_ms ? (A.ckp_backoff_ms * 2u > 10000u ? 10000u : A.ckp_backoff_ms * 2u) : 1000u;
    A.ckp_retry_ms = now_ms() + A.ckp_backoff_ms;
}
static void ckp_ok(void)
{
    A.ckp_error = false;
    A.ckp_backoff_ms = 0;
    A.ckp_retry_ms = 0;
}

static bool write_header_copy(uint32_t copy)
{
    A.hdr.crc = 0;
    A.hdr.crc = crc32_bytes(&A.hdr, sizeof A.hdr);
    const uint32_t off = copy * SGNANO_CKP_COPY_BYTES;
    if (!A.plat.write_at(A.fh, off, &A.hdr, sizeof A.hdr)) { ckp_failed(CKP_WHY_HEADER_WRITE); return false; }
    if (!A.plat.flush(A.fh)) { ckp_failed(CKP_WHY_HEADER_FLUSH); return false; }
    return true;
}

// 世代を進めて、2 部あるヘッダの片方だけを更新する。
// 更新中に電源が切れても、もう片方の古いヘッダから再開できる。
static bool commit_header(void)
{
    A.hdr.generation++;
    A.hdr.updated_ms = now_ms();
    if (!write_header_copy(A.hdr.generation & 1u)) { A.hdr_dirty = true; return false; }
    A.hdr_dirty = false;
    return true;
}

// 描き終えたのにまだチェックポイントに無い行を書く（2026-11-07）。force = やり直しの待ちを無視する（終わり・止めるとき）。
// 順番: 行の中身をすべて書く -> flush -> ビットマップに印 -> ヘッダを 1 回書く（ビットマップの印は「中身が書けて flush も済んだ」）。
// ヘッダが書けなくても中身は書けているので印は残し、次にヘッダだけをやり直す。true = 書き残しが無い
static bool save_pending(bool force)
{
    if (!A.inited || !A.fh) return false;
    if (!force && A.ckp_retry_ms && now_ms() < A.ckp_retry_ms) return false;
    uint32_t wrote = 0;
    bool ok = true;
    for (uint32_t y = 0; y < A.cfg.height; y++) {
        if (!bit_get(A.drawn, y) || bit_get(A.hdr.bitmap, y)) continue;
        A.plat.fb_get(0, y, A.linebuf, A.cfg.width);
        const uint32_t off = SGNANO_CKP_LINE_OFFSET + y * A.hdr.line_bytes;
        if (!A.plat.write_at(A.fh, off, A.linebuf, A.hdr.line_bytes)) { ckp_failed(CKP_WHY_LINE_WRITE); ok = false; break; }
        bit_set(A.flushing, y);
        wrote++;
    }
    if (wrote) {
        if (!A.plat.flush(A.fh)) { ckp_failed(CKP_WHY_LINE_FLUSH); memset(A.flushing, 0, sizeof A.flushing); return false; }
        for (uint32_t y = 0; y < A.cfg.height; y++) {
            if (!bit_get(A.flushing, y) || bit_get(A.hdr.bitmap, y)) continue;   // 2 回数えない
            bit_set(A.hdr.bitmap, y);
            A.hdr.lines_done++;
            A.last_line_no = y;
        }
        memset(A.flushing, 0, sizeof A.flushing);
        while (A.hdr.first_missing < A.cfg.height && bit_get(A.hdr.bitmap, A.hdr.first_missing))
            A.hdr.first_missing++;
        A.last_line_ms = now_ms();
        A.hdr_dirty = true;
    }
    if (A.hdr_dirty && !commit_header()) return false;
    if (ok) ckp_ok();
    return ok && A.hdr.lines_done >= A.lines_drawn;
}

// FB のページ 0 を PNG にして書く（大きさは cfg.width x cfg.height）。true = 書けた
// 【2026-11-07】1 行ずつ縮める（画像全体の RGB の置き場を取らない。止めた直後の途中までの絵でも書けるように）
static void png_row(void *user, uint32_t y, uint8_t *d)
{
    (void)user;
    A.plat.fb_get(0, y, A.linebuf, A.cfg.width);
    for (uint32_t x = 0; x < A.cfg.width; x++) {
        const uint16_t v = A.linebuf[x];
        const uint8_t r5 = (uint8_t)(v & 31u);
        const uint8_t g5 = (uint8_t)((v >> 5) & 31u);
        const uint8_t b5 = (uint8_t)((v >> 10) & 31u);
        *d++ = (uint8_t)((r5 << 3) | (r5 >> 2));
        *d++ = (uint8_t)((g5 << 3) | (g5 >> 2));
        *d++ = (uint8_t)((b5 << 3) | (b5 >> 2));
    }
}
static bool write_png(const char *path)
{
    if (!path || !A.plat.save_bytes) return false;
    uint32_t png_len = 0;
    uint8_t *png = sgnano_png_encode_rows(A.cfg.width, A.cfg.height, png_row, NULL, &png_len);
    if (!png) return false;
    const bool ok = A.plat.save_bytes(path, png, png_len);
    sgnano_png_free_rows(png);
    return ok;
}

// 全ラインが揃ったら、画像を保存してチェックポイントを消す。
// ここではファイルハンドルを閉じるだけで、状態とバッファは sgnano_app_finish まで残す。
// 【2026-11-07】PNG が書けたときだけチェックポイントを消す（書けなければ残して、再開で PNG を書き直せるように）。
// チェックポイントに書けなかった行も、PNG を書く前にもう一度書いてみる
static void finalize_if_done(void)
{
    if (!A.inited || A.lines_drawn < A.cfg.height) return;
    for (unsigned k = 0; k < 3u && !save_pending(true); k++) {}   // 一時的な誤りに 3 回まで
    if (!A.image_saved) {
        if ((A.cfg.outputs & SGNANO_OUTPUT_PNG) && A.cfg.image_path) {
            if (!write_png(A.cfg.image_path)) A.image_error = true;
        }
        A.image_saved = true;
    }
    const bool png_ok = !(A.cfg.outputs & SGNANO_OUTPUT_PNG) || !A.image_error;
    if (!A.ckp_removed && png_ok) {
        if (A.fh && A.plat.close) { A.plat.close(A.fh); A.fh = NULL; }
        if (A.cfg.checkpoint_path && A.plat.remove) (void)A.plat.remove(A.cfg.checkpoint_path);
        A.ckp_removed = true;
    }
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
    if (A.line_cells_done[y] >= A.cells_per_line && !bit_get(A.drawn, y)) {   // 初めて描き終えた行（保存は呼び手がまとめて）
        bit_set(A.drawn, y);
        A.lines_drawn++;
        A.lines_new = true;
    }
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
    if (!A.cfg.outputs) A.cfg.outputs = SGNANO_OUTPUT_FB | SGNANO_OUTPUT_PNG;
    A.cfg.block_size = cfg->block_size;
    A.cells_per_line = (cfg->width + cfg->block_size - 1u) / cfg->block_size;
    A.cov_bytes = (A.cells_per_line + 7u) / 8u;
    A.file_size = SGNANO_CKP_LINE_OFFSET + cfg->height * cfg->width * 2u;
    A.coverage = (uint8_t *)calloc((size_t)cfg->height * A.cov_bytes, 1);
    A.linebuf = (uint16_t *)malloc((size_t)cfg->width * 2u);
    if (!A.coverage || !A.linebuf) goto fail;

    A.fh = A.plat.open(A.cfg.checkpoint_path, A.file_size, true);
    if (!A.fh) goto fail;

    bool valid = false;
    if (cfg->resume) {
        ckp_header_t h0, h1;
        const bool v0 = A.plat.read_at(A.fh, 0, &h0, sizeof h0) && header_valid(&h0);
        const bool v1 = A.plat.read_at(A.fh, SGNANO_CKP_COPY_BYTES, &h1, sizeof h1) && header_valid(&h1);
        if (v0 || v1) {
            if (v0 && v1)
                A.hdr = (h0.generation >= h1.generation) ? h0 : h1;
            else
                A.hdr = v0 ? h0 : h1;
            valid = true;
        }
    }

    if (!valid) {
        memset(&A.hdr, 0, sizeof A.hdr);
        memcpy(A.hdr.magic, SGNANO_CKP_MAGIC, 8);
        A.hdr.version = SGNANO_CKP_VERSION;
        A.hdr.generation = 1;
        A.hdr.width = cfg->width;
        A.hdr.height = cfg->height;
        A.hdr.line_bytes = cfg->width * 2u;
        A.hdr.block_size = cfg->block_size;
        A.hdr.scene_hash = cfg->scene_hash;
        A.hdr.options_hash = cfg->options_hash;
        A.hdr.lines_done = 0;
        A.hdr.first_missing = 0;
        if (!write_header_copy(0) || !write_header_copy(1)) goto fail;
        const uint8_t z = 0;
        if (!A.plat.write_at(A.fh, A.file_size - 1u, &z, 1) || !A.plat.flush(A.fh)) goto fail;
    } else {
        // 既に描けたラインを FB へ戻す。ビットマップから数え直す。
        // defer_fb_restore のときは、FB が使えるようになってから
        // sgnano_app_restore_fb() で戻す（ストアを mode 1 で開くため）。
        uint32_t cnt = 0, first = cfg->height;
        for (uint32_t y = 0; y < cfg->height; y++) {
            if (!bit_get(A.hdr.bitmap, y)) {
                if (first == cfg->height) first = y;
                continue;
            }
            cnt++;
            if (!cfg->defer_fb_restore) {
                const uint32_t off = SGNANO_CKP_LINE_OFFSET + y * A.hdr.line_bytes;
                if (!A.plat.read_at(A.fh, off, A.linebuf, A.hdr.line_bytes)) { ckp_failed(CKP_WHY_RESUME_READ); continue; }
                A.plat.fb_put(0, y, A.linebuf, cfg->width);
            }
            memset(A.coverage + (size_t)y * A.cov_bytes, 0xFF, A.cov_bytes);
            A.line_cells_done[y] = (uint16_t)A.cells_per_line;
        }
        A.hdr.lines_done = cnt;
        A.hdr.first_missing = first;
        memcpy(A.drawn, A.hdr.bitmap, sizeof A.drawn);
        A.lines_drawn = cnt;
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

static bool restore_fb_locked(void);
bool sgnano_app_restore_fb(void) { lk(); const bool r = restore_fb_locked(); ulk(); return r; }
static bool restore_fb_locked(void)
{
    if (!A.inited || !A.plat.read_at || !A.plat.fb_put) return false;
    for (uint32_t y = 0; y < A.cfg.height; y++) {
        if (!bit_get(A.hdr.bitmap, y)) continue;
        const uint32_t off = SGNANO_CKP_LINE_OFFSET + y * A.hdr.line_bytes;
        if (!A.plat.read_at(A.fh, off, A.linebuf, A.hdr.line_bytes)) {
            ckp_failed(CKP_WHY_RESTORE_READ);
            maybe_status(true);
            return false;
        }
        A.plat.fb_put(0, y, A.linebuf, A.cfg.width);
    }
    maybe_status(true);
    return true;
}

uint32_t sgnano_app_resume_row(void) { return A.inited ? A.hdr.first_missing : 0u; }
uint32_t sgnano_app_lines_done(void) { return A.inited ? A.hdr.lines_done : 0u; }
uint32_t sgnano_app_lines_drawn(void) { return A.inited ? A.lines_drawn : 0u; }
void sgnano_app_debug(uint32_t *puts, int *why, bool *err, uint32_t *cells0, uint32_t *cells_per_line)
{
    if (puts) *puts = A.puts;
    if (why) *why = A.ckp_why;
    if (err) *err = A.ckp_error;
    if (cells0) *cells0 = A.inited ? A.line_cells_done[0] : 0u;
    if (cells_per_line) *cells_per_line = A.cells_per_line;
}
bool sgnano_app_all_done(void) { return A.inited && A.hdr.lines_done >= A.cfg.height; }

static void put_block_locked(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, const uint16_t *pixels);
void sgnano_app_put_block(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, const uint16_t *pixels)
{
    lk(); put_block_locked(x1, y1, x2, y2, pixels); ulk();
}
static void put_block_locked(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, const uint16_t *pixels)
{
    if (!A.inited || !pixels) return;
    if (x1 >= A.cfg.width || y1 >= A.cfg.height) return;
    const uint32_t src_w = x2 - x1 + 1u;   // 呼び手の行の幅（clamp 前）
    if (x2 >= A.cfg.width) x2 = A.cfg.width - 1u;
    if (y2 >= A.cfg.height) y2 = A.cfg.height - 1u;
    if (x1 > x2 || y1 > y2) return;
    const uint32_t copy_w = x2 - x1 + 1u;
    A.puts++;
    for (uint32_t y = y1; y <= y2; y++) {
        A.plat.fb_put(x1, y, pixels + (size_t)(y - y1) * src_w, copy_w);
        mark_block(x1, y, x2);
    }
    if (A.lines_new) {                      // 1 つのブロックで揃った行（4x4 なら 4 行）をまとめて 1 回で保存する
        A.lines_new = false;
        (void)save_pending(false);
        maybe_status(A.lines_drawn >= A.cfg.height);
        finalize_if_done();
    } else maybe_status(false);
}

static void put_rect_locked(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, uint16_t colour);
void sgnano_app_put_rect(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, uint16_t colour)
{
    lk(); put_rect_locked(x1, y1, x2, y2, colour); ulk();
}
static void put_rect_locked(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, uint16_t colour)
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
    lk();
    if (A.inited) {
        if (A.fh && (A.hdr_dirty || A.hdr.lines_done < A.lines_drawn)) (void)save_pending(false);   // 失敗したものを時間をおいてやり直す
        maybe_status(false);
    }
    ulk();
}

bool sgnano_app_checkpoint_now(void) { lk(); const bool r = save_pending(true); ulk(); return r; }
uint32_t sgnano_app_ckp_fails(void) { return A.ckp_fails; }

// 部分の PNG の名前: image_path の拡張子 .png の前に .partial を入れる（無ければ後ろに足す）
static void partial_path(char *out, size_t n, const char *path)
{
    const size_t len = strlen(path);
    if (len >= 4u && (strcmp(path + len - 4u, ".png") == 0 || strcmp(path + len - 4u, ".PNG") == 0))
        snprintf(out, n, "%.*s.partial%s", (int)(len - 4u), path, path + len - 4u);
    else
        snprintf(out, n, "%s.partial.png", path);
}

static void finish_locked(bool success);
void sgnano_app_finish(bool success)
{
    void (*u)(void) = A.plat.unlock;            // 終わりで A を消すので、先に覚えておく
    lk(); finish_locked(success); if (u) u();
}
static void finish_locked(bool success)
{
    (void)success;
    if (!A.inited) return;
    if (A.lines_drawn < A.cfg.height) {
        // 止めた・失敗した（2026-11-07）: 描けたのに書けていない行をもう一度チェックポイントへ書き、途中までの絵を .partial.png に残す
        for (unsigned k = 0; k < 3u && !save_pending(true); k++) {}   // 一時的な誤りに 3 回まで
        if ((A.cfg.outputs & SGNANO_OUTPUT_PNG) && A.cfg.image_path && A.lines_drawn > 0u) {
            char pp[512];
            partial_path(pp, sizeof pp, A.cfg.image_path);
            A.partial_saved = write_png(pp);
        }
    }
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

// チェックポイントの誤りの試し（2026-11-07）。書き込み・flush・PNG の書き出しをわざと失敗させ、
// (1) 失敗のあとも描いた行を書き続けること（失敗を固定しない）、(2) 止めたときに書き残しを救い出し .partial.png を残すこと、
// (3) PNG が書けなければチェックポイントを消さず、再開で PNG を書き直せること、を確かめる
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "app/sgnano_app.h"
#include "app/sgx/sgnano_app_sgx.h"
#include "platform/sgx/sgx4_stub_host.h"

#define W 160u
#define H 120u

static const char *CKP = "build_host/sgx_fault.ckp";
static const char *PNG = "build_host/sgx_fault.png";
static const char *PART = "build_host/sgx_fault.partial.png";
static unsigned n_ok, n_all;

static void check(int c, const char *what)
{
    n_all++;
    if (c) n_ok++;
    printf("%s %s\n", c ? "  ok  " : "  NG  ", what);
}

static uint16_t pix(uint32_t x, uint32_t y) { return (uint16_t)((y * 31u + x * 7u + 5u) & 0x7FFFu); }

// 4 行ずつのブロック（POV-Ray の +BS4 と同じ形: 4x4 を左から右へ）
static void render_band(uint32_t y0)
{
    uint16_t blk[16];
    for (uint32_t x0 = 0; x0 < W; x0 += 4u) {
        for (uint32_t dy = 0; dy < 4u; dy++)
            for (uint32_t dx = 0; dx < 4u; dx++) blk[dy * 4u + dx] = pix(x0 + dx, y0 + dy);
        sgnano_app_put_block(x0, y0, x0 + 3u, y0 + 3u, blk);
    }
}

static sgnano_app_config_t config(void)
{
    sgnano_app_config_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.scene_path = "sgx-fault";
    cfg.checkpoint_path = CKP;
    cfg.image_path = PNG;
    cfg.outputs = SGNANO_OUTPUT_FB | SGNANO_OUTPUT_PNG;
    cfg.width = W;
    cfg.height = H;
    cfg.block_size = 4;
    cfg.scene_hash = 0x13572468u;
    cfg.options_hash = 0x2468ace0u;
    cfg.resume = true;
    cfg.defer_fb_restore = true;
    return cfg;
}

// 実機と同じ順番で開く（mode 1 でストアを開く -> FB へ戻す -> 余りのページ）
static int start(void)
{
    sgnano_app_config_t cfg = config();
    if (!sgnano_sgx_host_init(W, H) || !sgnano_sgx_init_text(W, H)) return 0;
    if (!sgnano_app_init(&cfg, sgnano_sgx_platform())) return 0;
    if (!sgnano_sgx_enter_fb(W, H) || !sgnano_app_restore_fb()) return 0;
    (void)sgnano_sgx_lend_spare();
    return 1;
}
static void stop(void)
{
    sgnano_sgx_shutdown();
    sgnano_sgx_host_shutdown();
}
static int fb_rows_ok(uint32_t y0, uint32_t y1)
{
    const uint16_t *fb = sgnano_sgx_host_fb();
    if (!fb) return 0;
    for (uint32_t y = y0; y < y1; y++)
        for (uint32_t x = 0; x < W; x++)
            if (fb[y * W + x] != pix(x, y)) return 0;
    return 1;
}
static int exists(const char *p) { return access(p, F_OK) == 0; }
// 何度か保存し直せば、すべて書ける（誤りは固定されない）
static int eventually(void) { for (int k = 0; k < 3; k++) if (sgnano_app_checkpoint_now()) return 1; return 0; }

int main(void)
{
    unlink(CKP); unlink(PNG); unlink(PART);

    // (1) 一時的な誤りのあとも書き続ける
    check(start(), "start (fresh)");
    sgnano_sgx_host_inject(2, 0, 0);            // 書くための get を 2 回失敗させる
    for (uint32_t y = 0; y < 20u; y += 4u) render_band(y);
    check(sgnano_app_ckp_fails() > 0u, "a store_get failure was seen");
    check(sgnano_app_lines_drawn() == 20u, "20 lines drawn");
    sgnano_sgx_host_inject(0, 1, 0);            // flush を 1 回失敗させる
    for (uint32_t y = 20u; y < 40u; y += 4u) render_band(y);
    check(eventually(), "saving again stores everything left (the failures do not stick)");
    check(sgnano_app_lines_done() == 40u, "40 lines saved after the failures");

    // (2) 止める: 救い出しと .partial.png
    for (uint32_t y = 40u; y < 52u; y += 4u) render_band(y);
    sgnano_sgx_host_inject(1, 0, 0);            // 最後の行の保存を失敗させておく（finish が救い出す）
    render_band(52u);
    sgnano_app_finish(false);
    check(exists(CKP), "the checkpoint is kept after a stop");
    check(exists(PART), "a .partial.png is written after a stop");
    check(!exists(PNG), "no full PNG after a stop");
    stop();

    check(start(), "start (resume)");
    check(sgnano_app_resume_row() == 56u, "resume at row 56 (the rescued band is saved)");
    check(fb_rows_ok(0, 56u), "rows 0..55 are restored into the FB");

    // (3) PNG が書けなければチェックポイントを消さない
    sgnano_sgx_host_inject(0, 0, 1);            // PNG の書き出しを失敗させる
    for (uint32_t y = 56u; y < H; y += 4u) render_band(y);
    check(sgnano_app_lines_drawn() == H, "all lines drawn");
    sgnano_app_finish(true);
    check(!exists(PNG), "the PNG write failed (injected)");
    check(exists(CKP), "the checkpoint is kept when the PNG could not be written");
    stop();

    check(start(), "start (resume after the PNG failure)");
    check(sgnano_app_all_done(), "the checkpoint is complete");
    sgnano_app_finish(true);
    check(exists(PNG), "the PNG is written on resume");
    check(!exists(CKP), "the checkpoint is removed after the PNG is written");
    check(fb_rows_ok(0, H), "the whole FB matches");
    stop();

    unlink(PART); unlink(PNG);
    printf("sgx checkpoint fault test: %s %u/%u\n", n_ok == n_all ? "ok" : "FAIL", n_ok, n_all);
    return n_ok == n_all ? 0 : 1;
}

// SGX スタブのチェックポイント層をホストで試す。
// 10 ライン描いて終了し、再開して残りを描き、チェックポイントが消えることを見る。
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "app/sgnano_app.h"
#include "app/sgx/sgnano_app_sgx.h"
#include "platform/sgx/sgx4_stub_host.h"

#define W 160u
#define H 120u

static uint16_t line[W];

static int render_lines(uint32_t y0, uint32_t y1)
{
    for (uint32_t y = y0; y < y1; y++) {
        for (uint32_t x = 0; x < W; x++)
            line[x] = (uint16_t)((y * 31u + x * 7u) & 0x7FFFu);
        sgnano_app_put_block(0, y, W - 1u, y, line);
    }
    return 0;
}

int main(void)
{
    const char *ckp = "build_host/sgx_test.ckp";
    unlink(ckp);

    if (!sgnano_sgx_host_init(W, H) || !sgnano_sgx_init(W, H)) {
        fprintf(stderr, "sgx host init failed\n");
        return 1;
    }

    sgnano_app_config_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.scene_path = "sgx-test";
    cfg.checkpoint_path = ckp;
    cfg.width = W;
    cfg.height = H;
    cfg.block_size = 4;
    cfg.scene_hash = 0x12345678u;
    cfg.options_hash = 0x9abcdef0u;
    cfg.resume = true;
    cfg.verbose = false;

    if (!sgnano_app_init(&cfg, sgnano_sgx_platform())) {
        fprintf(stderr, "app init failed\n");
        return 1;
    }
    render_lines(0, 10);
    sgnano_app_finish(false);
    if (access(ckp, F_OK) != 0) {
        fprintf(stderr, "checkpoint was not kept\n");
        return 1;
    }

    // 再開。FB は作り直してもよい（チェックポイントから戻す）。
    if (!sgnano_sgx_host_init(W, H) || !sgnano_sgx_init(W, H)) {
        fprintf(stderr, "sgx host re-init failed\n");
        return 1;
    }
    if (!sgnano_app_init(&cfg, sgnano_sgx_platform())) {
        fprintf(stderr, "app resume init failed\n");
        return 1;
    }
    if (sgnano_app_resume_row() != 10u) {
        fprintf(stderr, "resume row: got %u, want 10\n", sgnano_app_resume_row());
        return 1;
    }
    render_lines(10, H);
    if (!sgnano_app_all_done()) {
        fprintf(stderr, "not all lines done: %u\n", sgnano_app_lines_done());
        return 1;
    }
    sgnano_app_finish(false);
    if (access(ckp, F_OK) == 0) {
        fprintf(stderr, "checkpoint was not removed\n");
        return 1;
    }

    // 先頭と末尾の画素が残っていること。
    const uint16_t *fb = sgnano_sgx_host_fb();
    if (!fb || fb[0] != (uint16_t)(0 * 31u + 0 * 7u) ||
        fb[(H - 1u) * W + (W - 1u)] != (uint16_t)(((H - 1u) * 31u + (W - 1u) * 7u) & 0x7FFFu)) {
        fprintf(stderr, "framebuffer check failed\n");
        return 1;
    }

    sgnano_sgx_shutdown();
    sgnano_sgx_host_shutdown();
    printf("sgx checkpoint test: ok\n");
    return 0;
}

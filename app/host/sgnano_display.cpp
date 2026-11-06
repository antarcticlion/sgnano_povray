#include "app/host/sgnano_display.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "app/sgnano_app.h"
#include "app/host/sgnano_app_host.h"

namespace pov_frontend
{

static uint32_t env_u32(const char *name, uint32_t def)
{
    const char *s = getenv(name);
    if (!s || !*s) return def;
    return (uint32_t)strtoul(s, nullptr, 0);
}

static uint16_t to555(const Display::RGBA8 &c)
{
    // sgnano のネイティブ RGB555: bit0-4=R, bit5-9=G, bit10-14=B
    return (uint16_t)(((c.red >> 3) & 31u) | (((c.green >> 3) & 31u) << 5) | (((c.blue >> 3) & 31u) << 10));
}

void SgnanoDisplay::Initialise()
{
    if (ready) return;
    if (!getenv("SGNANO_CHECKPOINT")) return;
    if (!sgnano_host_init(GetWidth(), GetHeight())) {
        fprintf(stderr, "[sgnano] could not allocate the host framebuffer\n");
        return;
    }

    sgnano_app_config_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.scene_path = getenv("SGNANO_SCENE") ? getenv("SGNANO_SCENE") : "(scene)";
    cfg.checkpoint_path = getenv("SGNANO_CHECKPOINT");
    cfg.image_path = getenv("SGNANO_IMAGE");
    cfg.width = GetWidth();
    cfg.height = GetHeight();
    cfg.block_size = env_u32("SGNANO_BLOCK_SIZE", 4);
    cfg.scene_hash = env_u32("SGNANO_SCENE_HASH", 0);
    cfg.options_hash = env_u32("SGNANO_OPTIONS_HASH", 0);
    const char *out = getenv("SGNANO_OUTPUT");
    if (!out || !*out) {
        cfg.outputs = SGNANO_OUTPUT_PNG;          // 既定は PNG ファイル
    } else {
        cfg.outputs = 0;
        if (strcmp(out, "fb") == 0 || strcmp(out, "both") == 0) cfg.outputs |= SGNANO_OUTPUT_FB;
        if (strcmp(out, "png") == 0 || strcmp(out, "both") == 0) cfg.outputs |= SGNANO_OUTPUT_PNG;
        if (!cfg.outputs) cfg.outputs = SGNANO_OUTPUT_FB;
    }
    cfg.resume = env_u32("SGNANO_RESUME", 1) != 0;
    cfg.verbose = env_u32("SGNANO_VERBOSE", 1) != 0;

    if (!sgnano_app_init(&cfg, sgnano_host_platform())) {
        fprintf(stderr, "[sgnano] checkpoint init failed: %s\n", cfg.checkpoint_path);
        sgnano_host_shutdown();
        return;
    }
    ready = true;
}

void SgnanoDisplay::Close()
{
    if (!ready) return;
    sgnano_app_finish(true);
    sgnano_host_shutdown();
    ready = false;
}

void SgnanoDisplay::DrawPixel(unsigned int x, unsigned int y, const RGBA8 &colour)
{
    if (!ready) return;
    const uint16_t c = to555(colour);
    sgnano_app_put_block(x, y, x, y, &c);
}

void SgnanoDisplay::DrawPixelBlock(unsigned int x1, unsigned int y1, unsigned int x2, unsigned int y2, const RGBA8 *colour)
{
    if (!ready || !colour) return;
    if (x2 < x1 || y2 < y1) return;
    const size_t w = (size_t)(x2 - x1 + 1), h = (size_t)(y2 - y1 + 1);
    std::vector<uint16_t> px(w * h);
    for (size_t y = 0; y < h; y++)
        for (size_t x = 0; x < w; x++)
            px[y * w + x] = to555(colour[y * w + x]);
    sgnano_app_put_block(x1, y1, x2, y2, px.data());
}

void SgnanoDisplay::DrawFilledRectangle(unsigned int x1, unsigned int y1, unsigned int x2, unsigned int y2, const RGBA8 &colour)
{
    if (!ready) return;
    sgnano_app_put_rect(x1, y1, x2, y2, to555(colour));
}

void SgnanoDisplay::UpdateScreen(bool Force)
{
    (void)Force;
    if (ready) sgnano_app_tick();
}

} // namespace pov_frontend

// ホスト用の sgnano_app プラットフォーム。
#include "app/host/sgnano_app_host.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static uint16_t *g_fb;
static uint32_t g_w, g_h;

static void *h_open(const char *path, uint32_t size, bool create)
{
    if (!path) return NULL;
    FILE *f = fopen(path, "r+b");
    if (!f && create) {
        f = fopen(path, "w+b");
        if (f) {
            // ファイルを size まで広げる（中身は未定義でよい）。
            if (size) {
                if (fseeko(f, (off_t)size - 1, SEEK_SET) != 0 || fputc(0, f) == EOF) {
                    fclose(f);
                    return NULL;
                }
                fflush(f);
            }
        }
    }
    return f;
}

static bool h_read_at(void *h, uint32_t off, void *dst, uint32_t n)
{
    FILE *f = (FILE *)h;
    if (!f) return false;
    if (fseeko(f, (off_t)off, SEEK_SET) != 0) return false;
    return fread(dst, 1, n, f) == n;
}

static bool h_write_at(void *h, uint32_t off, const void *src, uint32_t n)
{
    FILE *f = (FILE *)h;
    if (!f) return false;
    if (fseeko(f, (off_t)off, SEEK_SET) != 0) return false;
    return fwrite(src, 1, n, f) == n;
}

static bool h_flush(void *h)
{
    FILE *f = (FILE *)h;
    if (!f || fflush(f) != 0) return false;
    return fsync(fileno(f)) == 0;   // 1 ラインごとにストレージへ落とす
}

static void h_close(void *h) { if (h) fclose((FILE *)h); }

static bool h_remove(const char *path) { return path && remove(path) == 0; }

static void h_fb_put(uint32_t x, uint32_t y, const uint16_t *pixels, uint32_t n)
{
    if (!g_fb || y >= g_h) return;
    if (x >= g_w) return;
    if (x + n > g_w) n = g_w - x;
    memcpy(g_fb + (size_t)y * g_w + x, pixels, (size_t)n * 2u);
}

static void h_fb_get(uint32_t x, uint32_t y, uint16_t *pixels, uint32_t n)
{
    if (!g_fb || y >= g_h || x >= g_w) return;
    if (x + n > g_w) n = g_w - x;
    memcpy(pixels, g_fb + (size_t)y * g_w + x, (size_t)n * 2u);
}

static uint32_t h_now_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}

static void h_status(const char *text)
{
    if (!text) return;
    fprintf(stderr, "\n%s\n", text);
    fflush(stderr);
}

static void *h_scratch_alloc(uint32_t bytes) { return malloc(bytes); }
static void h_scratch_free(void *p) { free(p); }

static bool h_save_bytes(const char *path, const void *data, uint32_t len)
{
    if (!path || !data) return false;
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    const bool ok = fwrite(data, 1, len, f) == len && fflush(f) == 0;
    fclose(f);
    return ok;
}

static const sgnano_app_platform_t g_platform = {
    .open = h_open,
    .read_at = h_read_at,
    .write_at = h_write_at,
    .flush = h_flush,
    .close = h_close,
    .remove = h_remove,
    .fb_put = h_fb_put,
    .fb_get = h_fb_get,
    .now_ms = h_now_ms,
    .status = h_status,
    .save_bytes = h_save_bytes,
    .scratch_alloc = h_scratch_alloc,
    .scratch_free = h_scratch_free,
};

bool sgnano_host_init(uint32_t width, uint32_t height)
{
    if (!width || !height) return false;
    free(g_fb);
    g_fb = (uint16_t *)calloc((size_t)width * height, 2);
    if (!g_fb) return false;
    g_w = width; g_h = height;
    return true;
}

void sgnano_host_shutdown(void)
{
    free(g_fb); g_fb = NULL; g_w = g_h = 0;
}

const sgnano_app_platform_t *sgnano_host_platform(void) { return &g_platform; }
uint16_t *sgnano_host_fb(void) { return g_fb; }

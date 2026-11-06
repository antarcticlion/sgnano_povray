// ホスト上で SGX スタブを動かす（チェックポイント層のテスト用）。
#include "platform/sgx/sgx4_stub_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "platform/sgx/sgx4_stub.h"

#define MAX_STORES 8

typedef struct {
    FILE *f;
    uint32_t block_size;
    uint32_t blocks;
} host_store_t;

typedef struct {
    int id;
    uint32_t block;
} host_blk_t;

static host_store_t g_stores[MAX_STORES];
static uint16_t *g_fb;
static uint32_t g_w, g_h;

static uint32_t h_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}

static void h_sleep_us(uint32_t us)
{
    struct timespec ts = { (time_t)(us / 1000000u), (long)(us % 1000000u) * 1000L };
    nanosleep(&ts, NULL);
}

static bool h_stop(void) { return false; }

static void h_log(const char *s, unsigned n) { fwrite(s, 1, n, stderr); }

static void h_term_write(const char *s, unsigned n) { fwrite(s, 1, n, stdout); }

static const char *h_set_mode(int mode) { (void)mode; return NULL; }

static uint16_t h_rgb(unsigned r, unsigned g, unsigned b)
{
    return (uint16_t)(((r >> 3) & 31u) | (((g >> 3) & 31u) << 5) | (((b >> 3) & 31u) << 10));
}

static void *h_fb_page(int page) { return page == 0 ? g_fb : NULL; }

static unsigned h_text_puts(unsigned x, unsigned y, const char *s, uint32_t attr)
{
    (void)attr;
    fprintf(stdout, "[text %u,%u] %s\n", x, y, s);
    return 0;
}

static void h_text_clear(uint32_t attr) { (void)attr; }

static int h_remove(const char *path) { return remove(path); }

static int h_write_file(const char *path, const void *data, uint32_t len)
{
    if (!path || !data) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    const bool ok = fwrite(data, 1, len, f) == len && fflush(f) == 0;
    fclose(f);
    return ok ? 0 : -1;
}

static int h_store_open(const sgx_store_config_t *cfg)
{
    if (!cfg || !cfg->path || !cfg->block_size || !cfg->block_count) return -1;
    int id = -1;
    for (int i = 0; i < MAX_STORES; i++) if (!g_stores[i].f) { id = i; break; }
    if (id < 0) return -1;
    FILE *f = fopen(cfg->path, "r+b");
    if (!f && (cfg->flags & SGX_STORE_CREATE)) {
        f = fopen(cfg->path, "w+b");
        if (f) {
            const uint32_t size = cfg->block_size * cfg->block_count;
            if (fseeko(f, (off_t)size - 1, SEEK_SET) != 0 || fputc(0, f) == EOF) {
                fclose(f); f = NULL;
            }
            fflush(f);
        }
    }
    if (!f) return -1;
    g_stores[id].f = f;
    g_stores[id].block_size = cfg->block_size;
    g_stores[id].blocks = cfg->block_count;
    return id;
}

static void *h_store_get(int id, uint32_t block)
{
    if (id < 0 || id >= MAX_STORES || !g_stores[id].f) return NULL;
    if (block >= g_stores[id].blocks) return NULL;
    const uint32_t bs = g_stores[id].block_size;
    host_blk_t *h = (host_blk_t *)malloc(sizeof *h + bs);
    if (!h) return NULL;
    h->id = id;
    h->block = block;
    uint8_t *p = (uint8_t *)(h + 1);
    memset(p, 0, bs);
    if (fseeko(g_stores[id].f, (off_t)block * bs, SEEK_SET) == 0) {
        const size_t got = fread(p, 1, bs, g_stores[id].f);
        (void)got;   // 足りない部分は既に 0 で埋めてある
    }
    return p;
}

static int h_store_release(void *vp, int dirty)
{
    if (!vp) return -1;
    host_blk_t *h = (host_blk_t *)vp - 1;
    if (h->id < 0 || h->id >= MAX_STORES || !g_stores[h->id].f) { free(h); return -1; }
    if (dirty) {
        const uint32_t bs = g_stores[h->id].block_size;
        if (fseeko(g_stores[h->id].f, (off_t)h->block * bs, SEEK_SET) != 0 ||
            fwrite((const uint8_t *)(h + 1), 1, bs, g_stores[h->id].f) != bs) { free(h); return -1; }
    }
    free(h);
    return 0;
}

static int h_store_flush(int id)
{
    if (id < 0 || id >= MAX_STORES || !g_stores[id].f) return -1;
    if (fflush(g_stores[id].f) != 0) return -1;
    return fsync(fileno(g_stores[id].f)) == 0 ? 0 : -1;
}

static int h_store_close(int id)
{
    if (id < 0 || id >= MAX_STORES || !g_stores[id].f) return -1;
    fclose(g_stores[id].f);
    g_stores[id].f = NULL;
    g_stores[id].block_size = 0;
    g_stores[id].blocks = 0;
    return 0;
}

static int h_store_prefetch(int id, uint32_t block, uint32_t count)
{
    if (id < 0 || id >= MAX_STORES || !g_stores[id].f) return -1;
    uint32_t n = 0;
    for (uint32_t i = 0; i < count && block + i < g_stores[id].blocks; i++) {
        void *p = h_store_get(id, block + i);
        if (!p) break;
        (void)h_store_release(p, 0);
        n++;
    }
    return (int)n;
}

static int h_store_stats(int id, sgx_store_stats_t *out)
{
    if (id < 0 || id >= MAX_STORES || !g_stores[id].f || !out) return -1;
    memset(out, 0, sizeof *out);
    out->block_size = g_stores[id].block_size;
    out->block_count = g_stores[id].blocks;
    out->cache_blocks = 0;
    return 0;
}

static int h_store_error(int id) { (void)id; return 0; }

static const sgx4_stub_api_t g_api = {
    .abi_major = 4,
    .abi_minor = 0,
    .ms = h_ms,
    .sleep_us = h_sleep_us,
    .stop_requested = h_stop,
    .log = h_log,
    .term_write = h_term_write,
    .set_mode = h_set_mode,
    .rgb = h_rgb,
    .fb_page = h_fb_page,
    .text_puts = h_text_puts,
    .text_clear = h_text_clear,
    .remove = h_remove,
    .write_file = h_write_file,
    .store_open = h_store_open,
    .store_get = h_store_get,
    .store_release = h_store_release,
    .store_prefetch = h_store_prefetch,
    .store_flush = h_store_flush,
    .store_stats = h_store_stats,
    .store_error = h_store_error,
    .store_close = h_store_close,
};

const sgx4_stub_api_t *sgx4 = &g_api;

bool sgnano_sgx_host_init(uint32_t width, uint32_t height)
{
    if (!width || !height) return false;
    free(g_fb);
    g_fb = (uint16_t *)calloc((size_t)width * height, 2);
    if (!g_fb) return false;
    g_w = width; g_h = height;
    return true;
}

void sgnano_sgx_host_shutdown(void)
{
    for (int i = 0; i < MAX_STORES; i++)
        if (g_stores[i].f) { fclose(g_stores[i].f); g_stores[i].f = NULL; }
    free(g_fb); g_fb = NULL; g_w = g_h = 0;
}

uint16_t *sgnano_sgx_host_fb(void) { return g_fb; }

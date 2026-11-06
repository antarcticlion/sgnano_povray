// sgnano 本体の ABI 4.0（2026-11-06）のうち、このアプリが使う口だけを持つスタブ。
// 正式な型は third_party/sgnano/sgx_api.h。ファームの sgx_api_t と同じ並び・同じ型にそろえる。
#ifndef SGNANO_SGX4_STUB_H
#define SGNANO_SGX4_STUB_H

#include <stdbool.h>
#include <stdint.h>
#include "sgx_api.h"

#define SGX4_STUB_MODE_TEXT_FB SGX_MODE_TEXT_FB

// テキスト面の属性（src/video/text.h と同じ値。正式 ABI では SGX_T_*）
#define SGX4_T_FG(c)    ((uint32_t)(c) & 15u)
#define SGX4_T_BG(c)    (((uint32_t)(c) & 15u) << 4)
#define SGX4_T_BG_TRANS 0x00000400u
#define SGX4_T_FG_TRANS 0x00000200u

typedef struct {
    uint32_t abi_major, abi_minor;
    uint32_t (*ms)(void);
    void     (*sleep_us)(uint32_t us);
    bool     (*stop_requested)(void);
    void     (*log)(const char *s, unsigned n);

    void     (*term_write)(const char *s, unsigned n);
    const char *(*set_mode)(int mode);
    uint16_t (*rgb)(unsigned r, unsigned g, unsigned b);
    void    *(*fb_page)(int page);
    unsigned (*fb_borrow)(void **pages, unsigned max, uint32_t *page_bytes);
    void     (*fb_return)(void);
    void     (*text_clear)(uint32_t attr);
    unsigned (*text_puts)(unsigned col, unsigned row, const char *utf8, uint32_t attr);

    int      (*remove)(const char *path);
    int      (*write_file)(const char *path, const void *src, uint32_t len);

    // ABI 4.0 のブロックストア。正式な sgx_api_t と同じ型。
    int      (*store_open)(const sgx_store_config_t *cfg);
    int      (*store_close)(int id);
    void    *(*store_get)(int id, uint32_t block);
    int      (*store_release)(void *p, int dirty);
    int      (*store_prefetch)(int id, uint32_t block, uint32_t count);
    int      (*store_flush)(int id);
    int      (*store_stats)(int id, sgx_store_stats_t *out);
    int      (*store_error)(int id);

    // ABI 4.0: FB の余りのページ（mode 2 で keep 以外の 3 ページ = 約 1.8MB）。
    unsigned (*fb_lend_spare)(int keep, void **pages, unsigned max, uint32_t *page_bytes);
} sgx4_stub_api_t;

extern const sgx4_stub_api_t *sgx4;

#endif

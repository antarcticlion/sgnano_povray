// sgnano 本体の ABI 4.0（2026-11-06）のうち、このアプリが使う口だけを持つスタブ。
// 正式な型は third_party/sgnano/sgx_api.h。ファームの sgx_api_t と同じ並び・同じ型にそろえる。
//
// ホスト試験では sgx4_stub_host.c がこの小さな表を作る。
// 実機（SGNANO_SGX_OFFICIAL_ABI）では SDK の sgx_api_t をそのまま使い、sgx4 を
// sgx_crt0.c が定義する sgx に読み替える（名前だけの違い）。
#ifndef SGNANO_SGX4_STUB_H
#define SGNANO_SGX4_STUB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "sgx_api.h"

#define SGX4_STUB_MODE_TEXT_FB SGX_MODE_TEXT_FB

// テキスト面の属性（src/video/text.h と同じ値。正式 ABI では SGX_T_*）
#define SGX4_T_FG(c)    ((uint32_t)(c) & 15u)
#define SGX4_T_BG(c)    (((uint32_t)(c) & 15u) << 4)
#define SGX4_T_BG_TRANS 0x00000400u
#define SGX4_T_FG_TRANS 0x00000200u

#ifdef SGNANO_SGX_OFFICIAL_ABI

// 実機: SDK の表をそのまま使う。sgx は sgx_crt0.c が定義する。
typedef sgx_api_t sgx4_stub_api_t;
extern const sgx_api_t *sgx;
#define sgx4 sgx

static inline bool sgx4_has_store(void)
{
    // 関数の欄が表の大きさに収まっているか（3.x のファームでは無い）。
    return sgx && sgx->size >= offsetof(sgx_api_t, store_error) + sizeof(void *);
}

static inline bool sgx4_has_fb_lend_spare(void)
{
    return sgx && sgx->size >= offsetof(sgx_api_t, fb_lend_spare) + sizeof(void *);
}

#else

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

static inline bool sgx4_has_store(void)
{
    return sgx4 && sgx4->store_open && sgx4->store_close && sgx4->store_get &&
           sgx4->store_release && sgx4->store_flush && sgx4->store_stats;
}

static inline bool sgx4_has_fb_lend_spare(void)
{
    return sgx4 && sgx4->fb_lend_spare;
}

#endif

#endif

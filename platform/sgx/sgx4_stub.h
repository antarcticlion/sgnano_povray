// sgnano 本体の正式 ABI が出るまでの SGX スタブ。
// 正式 ABI 4.0 が来たら、このヘッダの関数を sgx_api_t の該当する口へ差し替える。
#ifndef SGNANO_SGX4_STUB_H
#define SGNANO_SGX4_STUB_H

#include <stdbool.h>
#include <stdint.h>
#include "include/sgx4_api.h"

#define SGX4_STUB_MODE_TEXT_FB 2

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
    int      (*set_mode)(int mode);
    uint16_t (*rgb)(unsigned r, unsigned g, unsigned b);
    uint16_t *(*fb_page)(int page);
    int      (*text_puts)(int x, int y, const char *s, uint32_t attr);
    int      (*text_clear)(void);

    int      (*remove)(const char *path);

    // SGX 4.0 のブロックストア（案）。正式 ABI が来るまでこの形で使う。
    int      (*store_open)(const sgx4_store_config_t *cfg);
    void    *(*store_get)(int id, uint32_t block);
    int      (*store_release)(void *p, int dirty);
    int      (*store_flush)(int id);
    int      (*store_close)(int id);
} sgx4_stub_api_t;

extern const sgx4_stub_api_t *sgx4;

#endif

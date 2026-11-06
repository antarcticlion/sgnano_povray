// SGX スタブ上で sgnano_app を動かすためのプラットフォーム。
#ifndef SGNANO_APP_SGX_H
#define SGNANO_APP_SGX_H

#include <stdbool.h>
#include <stdint.h>
#include "app/sgnano_app.h"

#ifdef __cplusplus
extern "C" {
#endif

// 画面を mode 2 にし、チェックポイント用のストアを開く。
bool sgnano_sgx_init(uint32_t width, uint32_t height);
const sgnano_app_platform_t *sgnano_sgx_platform(void);
void sgnano_sgx_shutdown(void);

// ABI 4.0 の fb_lend_spare で借りた FB の余りページ（連続しているとは限らない）。
void *sgnano_sgx_spare_base(void);
uint32_t sgnano_sgx_spare_bytes(void);

#ifdef __cplusplus
}
#endif

#endif

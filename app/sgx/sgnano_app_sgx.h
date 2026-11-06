// SGX スタブ上で sgnano_app を動かすためのプラットフォーム。
#ifndef SGNANO_APP_SGX_H
#define SGNANO_APP_SGX_H

#include <stdbool.h>
#include <stdint.h>
#include "app/sgnano_app.h"

#ifdef __cplusplus
extern "C" {
#endif

// mode 1（テキストだけ）: ストアを開く段。FB はまだ取らない。
bool sgnano_sgx_init_text(uint32_t width, uint32_t height);
// mode 2（テキスト＋FB）にしてページ 0 を取る。描画の段。
bool sgnano_sgx_enter_fb(uint32_t width, uint32_t height);
// mode 2 まで一度に（ホスト試験用）。
bool sgnano_sgx_init(uint32_t width, uint32_t height);
const sgnano_app_platform_t *sgnano_sgx_platform(void);
void sgnano_sgx_shutdown(void);

// ストアを開いたあとに FB の余りページを借りる（ABI 4.0 の fb_lend_spare）。
bool sgnano_sgx_lend_spare(void);

// ABI 4.0 の fb_lend_spare で借りた FB の余りページ（連続しているとは限らない）。
void *sgnano_sgx_spare_base(void);
uint32_t sgnano_sgx_spare_bytes(void);

// 切り分け用: false にすると fb_lend_spare を呼ばない（既定は true）。
void sgnano_sgx_set_spare_enabled(bool on);

#ifdef __cplusplus
}
#endif

#endif

// ホスト上で SGX スタブを動かすための実装（テスト用）。
#ifndef SGNANO_SGX4_STUB_HOST_H
#define SGNANO_SGX4_STUB_HOST_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool sgnano_sgx_host_init(uint32_t width, uint32_t height);
void sgnano_sgx_host_shutdown(void);
uint16_t *sgnano_sgx_host_fb(void);

#ifdef __cplusplus
}
#endif

#endif

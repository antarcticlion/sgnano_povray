// ホスト用の sgnano_app プラットフォーム（メモリ FB・stdio ファイル・PPM 保存）。
#ifndef SGNANO_APP_HOST_H
#define SGNANO_APP_HOST_H

#include <stdbool.h>
#include <stdint.h>
#include "app/sgnano_app.h"

#ifdef __cplusplus
extern "C" {
#endif

bool sgnano_host_init(uint32_t width, uint32_t height);
void sgnano_host_shutdown(void);
const sgnano_app_platform_t *sgnano_host_platform(void);
uint16_t *sgnano_host_fb(void);

#ifdef __cplusplus
}
#endif

#endif

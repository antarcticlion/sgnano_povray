// sgnano POV-Ray アプリの共通層（ホスト／SGX 共通）。
//
// - 1 ラインずつ完成した絵をチェックポイントファイルへ書く。
// - 途中で終了しても、次回に続きから描ける。
// - 描画中はテキスト面（ホストでは stdout）へ内部情報を出す。
//
// 画面やストレージの違いは sgnano_app_platform_t で吸収する。
#ifndef SGNANO_APP_H
#define SGNANO_APP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SGNANO_CKP_MAGIC        "SGNPCKP1"
#define SGNANO_CKP_VERSION      2u
#define SGNANO_CKP_COPY_BYTES   256u
#define SGNANO_CKP_HEADER_BYTES 512u   // ヘッダ 2 部（世代交代で壊れた方を使わない）
#define SGNANO_CKP_LINE_OFFSET  512u
#define SGNANO_CKP_MAX_LINES    480u
#define SGNANO_CKP_MAX_WIDTH    1024u

// 完成した絵の出力先。FB は画面に残す（ファイルは作らない）。
// PNG は可逆圧縮の PNG ファイルを保存する。
#define SGNANO_OUTPUT_FB  0x1u
#define SGNANO_OUTPUT_PNG 0x2u

typedef struct {
    // チェックポイントファイル。offset はファイル先頭から。
    void *(*open)(const char *path, uint32_t size, bool create);
    bool  (*read_at)(void *h, uint32_t off, void *dst, uint32_t n);
    bool  (*write_at)(void *h, uint32_t off, const void *src, uint32_t n);
    bool  (*flush)(void *h);
    void  (*close)(void *h);
    bool  (*remove)(const char *path);

    // フレームバッファ（RGB555）。1 行ずつ読み書きする。
    void (*fb_put)(uint32_t x, uint32_t y, const uint16_t *pixels, uint32_t n);
    void (*fb_get)(uint32_t x, uint32_t y, uint16_t *pixels, uint32_t n);

    // 単調増加のミリ秒（ホストは clock_gettime、SGX は api->ms）。
    uint32_t (*now_ms)(void);

    // 状態の表示（ホストは stdout、SGX はテキスト面）。
    void (*status)(const char *text);

    // 完成した PNG などのバイト列を保存する（NULL 可）。
    bool (*save_bytes)(const char *path, const void *data, uint32_t len);

    // PNG の RGB 作業場。SGX では FB の余りページを優先する（NULL 可）。
    void *(*scratch_alloc)(uint32_t bytes);
    void  (*scratch_free)(void *p);
} sgnano_app_platform_t;

typedef struct {
    const char *scene_path;      // 表示用
    const char *checkpoint_path; // 一時ファイル
    const char *image_path;      // 完成時の保存先（NULL 可）
    uint32_t width, height;
    uint32_t block_size;         // POV-Ray の +BS（4 以上）
    uint32_t scene_hash;         // シーンの同一性
    uint32_t options_hash;       // 解像度・AA などの同一性
    uint32_t outputs;            // SGNANO_OUTPUT_*（0 なら FB）
    bool resume;                 // 既存のチェックポイントを使う
    bool verbose;
} sgnano_app_config_t;

bool     sgnano_app_init(const sgnano_app_config_t *cfg, const sgnano_app_platform_t *plat);
uint32_t sgnano_app_resume_row(void);
uint32_t sgnano_app_lines_done(void);
bool     sgnano_app_all_done(void);

// POV-Ray の表示ブロック（確定画素）。x1..x2, y1..y2 は両端を含む。
void sgnano_app_put_block(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, const uint16_t *pixels);

// モザイクプレビューなどの暫定表示。チェックポイントには記録しない。
void sgnano_app_put_rect(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, uint16_t colour);

void        sgnano_app_tick(void);
const char *sgnano_app_status_text(void);
void        sgnano_app_finish(bool success);

uint32_t sgnano_app_fnv1a(const void *data, size_t n, uint32_t seed);

#ifdef __cplusplus
}
#endif

#endif // SGNANO_APP_H

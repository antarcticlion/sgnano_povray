# SGX スタブ層

sgnano 本体の ABI 4.0 はまだ正式版がないため、アプリ側では
`platform/sgx/sgx4_stub.h` の関数表を使う。

## 構成

```
app/sgnano_app.c/.h         チェックポイント、状態文字列、ライン確定（共通）
app/sgx/sgnano_app_sgx.c    SGX スタブ用のプラットフォーム実装
platform/sgx/sgx4_stub.h    正式 ABI が来るまでの関数表
```

`app/sgnano_app.c` はホストと SGX で共通。画面・ファイル・時刻の違いだけを
`sgnano_app_platform_t` で吸収する。

## SGX 側の前提

- 画面: `set_mode(SGX4_STUB_MODE_TEXT_FB)`（mode 2）。
- FB: `fb_page(0)` の 640×480 RGB555 を直接読み書きする。
- テキスト面: `text_puts` で 0〜5 行目に状態を出す。背景は透過。
- チェックポイント: SGX 4.0 のブロックストア（`store_open/get/release/flush/close`）。
  - ブロックサイズ 4096 バイト。
  - `SGX4_STORE_WRITEBACK | SGX4_STORE_CREATE`。
  - キャッシュ 8 ブロック。
- 時刻: `ms()`。
- 終了・停止: 正式 ABI では `stop_requested()` を見て、長い描画を中断できるようにする。

## 正式 ABI への差し替え

正式版 ABI 4.0 が来たら、`sgx4_stub.h` の関数を `sgx_api_t` の該当する口へ
置き換える。`sgnano_app_sgx.c` の呼び出し名を変えるだけで、`sgnano_app.c` は
そのまま使える。

想定する対応:

| スタブ | 正式 ABI（案） |
|---|---|
| `set_mode` | `set_mode` |
| `fb_page` | `fb_page` |
| `text_puts` / `text_clear` | `text_puts` / `text_clear` |
| `store_open/get/release/flush/close` | 同名の SGX 4.0 API |
| `remove` | `remove` |
| `ms` | `ms` |
| `stop_requested` | `stop_requested` |

## まだ無いもの

- 実機の SGX 4.0 ローダ（sgnano 本体側）。
- POV-Ray の常駐コア／モジュール分割。
- メッシュのブロックストア化。
- 実機のテキスト面レイアウト調整（文字数・行位置・色）。

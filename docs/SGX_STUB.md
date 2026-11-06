# SGX 層（正式 ABI 4.0）

sgnano 本体から **SGX ABI 4.0**（2026-11-06）が届いた。正式なヘッダは
`third_party/sgnano/sgx_api.h`（MIT、スナップショット）。アプリ側はこの型と
シグネチャに合わせている。sgnano 側の引き渡し文書は
[`SGX_ABI4_FORMAL.md`](SGX_ABI4_FORMAL.md)。

## ABI 4.0 の要点

- `.sgx` の入れ物（`SGX1`）は 3.x のまま。**オーバーレイ／`.SGX4` は無い**。
- 追加されたのは **ブロックストア** `store_open` / `store_close` /
  `store_get` / `store_release` / `store_prefetch` / `store_flush` /
  `store_stats` / `store_error`。
- `store_get` が取り出し（pin）、`store_release` が返す（unpin）。
  `store_pin` / `store_unpin` は無い。
- `SGX_STORE_RAW` / `SGX_STORE_COMPRESS` は予約（4.0 では `-SGX_ENOTSUP`）。
- 誤りに `SGX_EBUSY` が増えた（`SGX_E*` の続き）。
- ファームは 3.x のプログラムも読み込む（`SGX_ABI_COMPAT_MAJOR`）。
  4.0 で組んだものは 4.x のファームだけで動く。

## アプリ側の構成

```
app/sgnano_app.c/.h         チェックポイント、PNG 出力、状態文字列（共通）
app/sgx/sgnano_app_sgx.c    SGX 用プラットフォーム実装
platform/sgx/sgx4_stub.h    このアプリが使う口だけの関数表（正式 ABI と同じ型）
third_party/sgnano/sgx_api.h 正式 ABI 4.0 のヘッダ（byte-identical）
```

`app/sgnano_app.c` はホストと SGX で共通。画面・ファイル・時刻の違いだけを
`sgnano_app_platform_t` で吸収する。

## SGX 側の前提

- 画面: `set_mode(SGX_MODE_TEXT_FB)`（mode 2）。戻り値は `const char *` で、
  NULL が成功。
- FB: `fb_page(0)` の 640×480 RGB555 を直接読み書きする。
- テキスト面: `text_clear(attr)` と `text_puts(col,row,utf8,attr)` で
  0〜5 行目に状態を出す。背景は透過。
- チェックポイント: 正式 ABI 4.0 のブロックストア。
  - ブロックサイズ 4096 バイト。
  - `SGX_STORE_WRITEBACK | SGX_STORE_CREATE`。
  - キャッシュ 8 ブロック。
  - 途中経過は `store_flush` で機器へ書き戻す。
- 時刻: `ms()`。
- 終了・停止: `stop_requested()` を長い描画の区切りで見る。

## 正式 ABI との対応

| アプリの呼び出し | 正式 ABI 4.0 |
|---|---|
| `set_mode` | `const char *set_mode(int)`（NULL = 成功） |
| `fb_page` | `void *fb_page(int)` |
| `text_puts` / `text_clear` | `unsigned text_puts(unsigned,unsigned,const char*,uint32_t)` / `void text_clear(uint32_t)` |
| `store_open/get/release/prefetch/flush/stats/error/close` | 同名の `store_*` |
| `write_file` | `int write_file(const char *, const void *, uint32_t)` |
| `remove` | `int remove(const char *)` |
| `ms` | `uint32_t ms(void)` |
| `stop_requested` | `bool stop_requested(void)` |

## まだ無いもの

- 実機での起動・描画（実機が空くのを待つ）。
- POV-Ray の SGX 用フロントエンド（`vfe/unix` の置き換え）。
- メッシュ／テクスチャのブロックストア化。
- 実機のテキスト面レイアウト調整（文字数・行位置・色）。
- 実機の PNG 保存先（USB / S:）の運用。

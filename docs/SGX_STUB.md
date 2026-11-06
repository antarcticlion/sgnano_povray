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

- 画面: ストアを開く間は `set_mode(SGX_MODE_TEXT)`（mode 1）、描画の前に
  `set_mode(SGX_MODE_TEXT_FB)`（mode 2）。戻り値は `const char *` で、
  NULL が成功。
  - **実機（38GB HDD の exFAT）では mode 2 のまま `store_open` すると
    止まった**（2026-11-06）。`storetest` は mode 1 で通るため、開く間だけ
    mode 1 にし、開いてから mode 2 へ入る。再開するラインは
    `sgnano_app_restore_fb()` で FB へ戻す。
- FB の余りページ: ストアを開いたあと `fb_lend_spare(0, pages, 4, &page_bytes)`
  で mode 2 の表示ページ 0 を残し、残り 3 ページ（約 1.8MB）を借りる。
  - ページは高い番地から返るので、番地の順に並べ替えてから使う。
  - PNG の RGB 作業場が入る場合はそれを使い、入らなければ `malloc` に落とす。
  - 借りている間は `set_mode` とページ切り替えをしない（このアプリは mode 2 固定）。
  - 終了時に `fb_return()` で返す。
- FB: `fb_page(0)` の 640×480 RGB555 を直接読み書きする。
- テキスト面: `text_clear(attr)` と `text_puts(col,row,utf8,attr)` で
  0〜5 行目に状態を出す。背景は透過。
- チェックポイント: 正式 ABI 4.0 のブロックストア。
  - ブロックサイズ 4096 バイト。ファイルは 64KB の倍数に丸める。
  - `SGX_STORE_WRITEBACK | SGX_STORE_CREATE`。ただし CREATE の 0 埋めは
    実機の mode 2 で止まるため、先に `write_file` で同じ大きさの 0 の
    ファイルを作り、既存ファイルとして開く。
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
| `fb_lend_spare` / `fb_return` | 同名の ABI 4.0 |
| `write_file` | `int write_file(const char *, const void *, uint32_t)` |
| `remove` | `int remove(const char *)` |
| `ms` | `uint32_t ms(void)` |
| `stop_requested` | `bool stop_requested(void)` |

## 実機で確認できたこと（2026-11-06）

- 38GB HDD（exFAT）で `storetest` が 47/47 合格。
- `povprobe` で mode 1 → ストアを開く → mode 2 → 再開ラインを FB へ復元、
  1 ラインごとのチェックポイント、120 行で停止 → 480 行まで再開、
  PNG（可逆）と RGB555 生データの一致まで確認。
- `fb_lend_spare` の 3 ページ（1,843,200 B）を PNG の作業場に使用。
- SDK の `sgx_libc.c` の `calloc` は `-O2` で無限再帰にコンパイルされる
  ことがある。`-fno-builtin` で回避（SDK 側の修正が本筋）。

## まだ無いもの

- POV-Ray の SGX 用フロントエンド（`vfe/unix` の置き換え）。
- 借りた FB の余りページを POV-Ray のヒープ／メッシュキャッシュへ渡すこと。
- メッシュ／テクスチャのブロックストア化。
- 実機のテキスト面レイアウト調整（文字数・行位置・色）。
- 実機の PNG 保存先（USB / S:）の運用。

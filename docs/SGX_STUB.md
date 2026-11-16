# SGX 層（sgnano SGX ABI 5.11）

sgnano の **SGX ABI 5.11**（2026-11-16）に合わせている。正式なヘッダと、
`.sgx` をビルドするのに必要な SDK は `third_party/sgnano/sdk/sgx/` に
同梱してある（MIT、スナップショット）。このリポジトリだけでビルドできる。

- 正本: `third_party/sgnano/sdk/sgx/include/sgx_api.h`
- SDK: `sgx.cmake` / `sgx_crt0.c` / `sgx_libc.c` / リンカスクリプト / `sgx_link.py`
- ライセンス: [`third_party/sgnano/LICENSE`](../third_party/sgnano/LICENSE)
- 一覧とチェックサム: [`third_party/sgnano/README.md`](../third_party/sgnano/README.md)

## ABI 5.11 の要点

- ABI は**追加のみ**（append-only）。5.x のファームは 4.x で組んだ `.sgx` も
  受け付ける（`SGX_ABI_COMPAT_MAJOR 4`）。このアプリは 5.11 で組む。
- 4.0 で入った**ブロックストア** `store_open` / `store_close` / `store_get` /
  `store_release` / `store_prefetch` / `store_flush` / `store_stats` /
  `store_error` をチェックポイントに使う。
- 5.x の追加（進捗ボックス型 `sgx_progress.h` など）のうち、このアプリは
  テキスト面の進捗バーと UART の `progress` 行を使う。
- `.sgx` の入れ物（`SGX1`）は 3.x と同じ。オーバーレイは使っていない。

## アプリ側の構成

```
app/sgnano_app.c/.h          チェックポイント、PNG 出力、状態文字列（共通）
app/sgx/povray_sgx.cpp       SGX フロントエンド（VFE の置き換え、引数解析）
app/sgx/sgnano_app_sgx.c/.h  SGX 用プラットフォーム実装
app/sgx/zlib_shim.c          SGX 用 zlib シム
platform/sgx/sgx4_stub.h     このアプリが使う口だけの関数表（実機では SDK の sgx_api_t）
third_party/sgnano/sdk/sgx/  同梱 SDK（ABI 5.11）
```

`app/sgnano_app.c` はホストと SGX で共通。画面・ファイル・時刻の違いだけを
`sgnano_app_platform_t` で吸収する。

## SGX 側の前提

- **ストアを先に開く**。実機（38GB HDD の exFAT）では mode 2 のまま
  `store_open` すると止まった（2026-11-06）。そこで
  - 描画中は **mode 7 preview**（320×200、SRAM）にし、
  - ストアを開き、
  - 描画の最後だけ **mode 2**（640×480 RGB555）へ切り替えて結果を表示する。
  `-fbview` を付けたときは最初から mode 2 で描く（遅い）。
- FB の余りページ: ストアを開いたあと `fb_lend_spare(0, pages, 4, &page_bytes)`
  で表示ページ 0 を残し、残り 3 ページ（1,843,200 B）を借りる。
  - ページは高い番地から返るので、**番地の順に並べ替えて**から連続した
    ~1.8MB として使う。
  - PNG の RGB 作業場と POV-Ray のスレッドスタックに使う。
  - 借りている間は `set_mode` とページ切り替えをしない。
  - 終了時に `fb_return()` で返す。
- FB: `fb_page(0)` の 640×480 RGB555 を直接読み書きする。
- テキスト面: `text_clear(attr)` と `text_puts(col,row,utf8,attr)` で
  0〜5 行目に状態を出す。背景は透過。UTF-8 の日本語も出せる。
- チェックポイント: ABI のブロックストア。
  - ブロックサイズ 4096 バイト。ファイルは 64KB の倍数に丸める。
  - `SGX_STORE_WRITEBACK | SGX_STORE_CREATE`。ただし CREATE の 0 埋めは
    実機の mode 2 で止まるため、先に `write_file` で同じ大きさの 0 の
    ファイルを作り、既存ファイルとして開く。
  - キャッシュ 8 ブロック。
  - 途中経過は `store_flush` で機器へ書き戻す。
- 時刻: `ms()`。
- 終了・停止: `stop_requested()` を長い描画の区切りで見る。ESC は
  フロントエンドへ渡り（`SGX_F_ESC_KEY`）、描けた分を保存して
  `SCENE.partial.png` を書く。
- 描画は 1 スレッド・アンチエイリアスなし（`+WT1 -A` を強制）。ライン単位の
  チェックポイントを決定的にするため。
- PNG は miniz の deflate レベル 9（可逆）。

## ABI との対応

| アプリの呼び出し | ABI 5.11 |
|---|---|
| `set_mode` | `const char *set_mode(int)`（NULL = 成功） |
| `fb_page` | `void *fb_page(int)` |
| `text_puts` / `text_clear` | `unsigned text_puts(unsigned,unsigned,const char*,uint32_t)` / `void text_clear(uint32_t)` |
| `store_open/get/release/prefetch/flush/stats/error/close` | 同名の `store_*` |
| `fb_lend_spare` / `fb_return` | 同名の ABI |
| `write_file` | `int write_file(const char *, const void *, uint32_t)` |
| `remove` | `int remove(const char *)` |
| `ms` | `uint32_t ms(void)` |
| `stop_requested` | `bool stop_requested(void)` |

## 実機で確認できたこと

2026-11-16、ABI 5.11 のファームで:

- `povprobe.sgx`（120 行で停止 → 再開 → 480 行）:
  - FB の不一致 0 ピクセル。
  - PNG は RGB555 の生データとピクセル一致。
  - `fb_lend_spare` は 1,843,200 B。
- `povray.sgx`（ABI 5.11、1,580,872 B）:
  - `00_fast_sphere.pov` を 640×480 で描画。PNG まで **199.8 秒**（描画 193.4 秒）。
  - 320×240・5 分上限で 13 シーンを試し、12 シーンが 56.5〜283.9 秒
    （合計）で完走。`11_slow_spheres`（10,000 球）は解析が PSRAM に
    収まらず、5 分で打ち切り。
  - 完走した 12 枚の PNG は、ホスト描画を RGB555 に丸めたものと
    最大 9/255 の差（平均 0.14〜0.58）。

ホストでは 13 シーンを 640×480 で 6.2〜10.2 秒/本（5 分上限、タイムアウト
なし）。詳しくは [`TESTING.md`](TESTING.md)。

## まだ無いもの・制限

- アニメーションと POV-Ray の高度な機能は実機で未確認。
- チェックポイントはライン単位。シーン・サイズ・ブロックサイズ・オプションを
  変えると無効になる。
- メッシュ／テクスチャのブロックストア化はしていない。
- 実機の PNG 保存先は USB（`B:` など）の運用。ドライブ文字は USB の再接続で
  変わることがある。

# SGX POV-Ray フロントエンド引き継ぎ（2026-11-06）

> **歴史的な文書（ABI 4.0 時代の引き継ぎメモ）。** フロントエンドは実装済みで、
> 現在は **ABI 5.11**（2026-11-16）でビルド・実機動作する。現行の構成は
> [`SGX_STUB.md`](SGX_STUB.md)、ビルドは [`BUILD.md`](BUILD.md) を参照。

POV-Ray 3.7 本体を sgnano の SGX アプリとして動かすための、フロントエンド担当者向けの引き継ぎです。
**アプリ共通層・SGX プラットフォーム・実機プローブまでは完成・検証済み**で、
残りは POV-Ray 本体を SGX 上で走らせるフロントエンドです。

## 1. できていること

- POV-Ray 3.7.0.10 を `third_party/povray/` に取り込み、ホスト版がビルド・描画できる。
- 1 ライン単位のチェックポイント、停止→再開、可逆 PNG 出力を
  `app/sgnano_app.c` / `app/sgnano_app.h` に実装。
- SGX 側のプラットフォーム `app/sgx/sgnano_app_sgx.c` が ABI 4.0 に対応。
- 実機プローブ `apps/povprobe/povprobe.c` と自動試験 `tools/hw_probe.py` が
  38GB HDD で通っている（下記「検証済み」）。
- テストシーンと描画結果を B: ドライブに配置済み。

### 検証済み（実機、2026-11-06）

- 38,154MB の USB HDD（exFAT）で `storetest.sgx` が 47/47 合格。
- `povprobe.sgx`:
  - 120/480 で停止（6.4 秒、チェックポイント 655,360 B）
  - 再開して 480/480 完成（24.1 秒）
  - 再開後の FB 検証 0 bad rows / 0 bad pixels、FB CRC `30d0c6dd`
  - RGB555 生データ 614,400 B の CRC も `30d0c6dd`
  - PNG 42,966 B をデコードし、生データと **1 画素も違わず一致**
  - `fb_lend_spare` 1,843,200 B（3 ページ）を PNG の RGB 作業場に使用
- ホスト版 POV-Ray で全 13 シーンを 640×480・5 分上限で表示テスト。
  すべて 6.2〜10.2 秒で完走（打ち切り 0 件）。
- 実機ファイラーで 12 枚の PNG を表示確認。336KB の
  `11_slow_spheres.png` は 92%（448 行）まで表示した約 22 秒で
  ビューアが閉じた（5 分未満。ビューアのリング 28KB の制約）。

## 2. フロントエンドが作るもの

`povray_sgx.sgx`（名前は任意）: シーンファイルを読み、POV-Ray で描画し、
`app/sgnano_app.c` のチェックポイント・PNG 層へ渡す SGX 実行ファイル。

### 2.1 画面

- `pov_frontend::Display` 相当（またはホストの `UnixTextDisplay` の置き換え）を作り、
  `DrawPixelBlock` / `DrawPixel` / `DrawFilledRectangle` を実装する。
- RGBA8 を RGB555 へ:
  ```cpp
  (uint16_t)(((r >> 3) & 31) | (((g >> 3) & 31) << 5) | (((b >> 3) & 31) << 10))
  ```
- ブロックは `sgnano_app_put_block(x1, y1, x2, y2, px)` へ。
  塗りつぶしは `sgnano_app_put_rect()` へ。
- `UpdateScreen()` から `sgnano_app_tick()` を呼ぶ。

### 2.2 初期化・終了の順番（重要）

実機の 38GB exFAT では、**mode 2 で FB を出したまま `store_open` すると止まる**。
`povprobe` と同じ順番にする。

```c
sgnano_sgx_init_text(W, H);          // mode 1（テキストだけ）
sgnano_app_config_t cfg = { ... };
cfg.defer_fb_restore = true;         // 再開ラインはあとで FB へ
sgnano_app_init(&cfg, sgnano_sgx_platform());   // ここで store_open
sgnano_sgx_enter_fb(W, H);           // mode 2 + fb_page(0)
sgnano_app_restore_fb();             // 再開ラインを FB へ
sgnano_sgx_lend_spare();             // FB の余り 3 ページを借りる（任意）
...
sgnano_app_finish(true);             // PNG 書き出し・ckp 削除
sgnano_sgx_shutdown();               // fb_return
```

- `cfg.outputs = SGNANO_OUTPUT_FB | SGNANO_OUTPUT_PNG;`（既定は both）。
- `cfg.checkpoint_path` は書ける場所（例 `B:/POVTEST/render.ckp`）。
- `cfg.image_path` は PNG の保存先。
- `cfg.width/height` は 640×480。
- `cfg.block_size` は 4（POV-Ray の `+BS4`）。チェックポイントのブロックは 4096。
- `cfg.scene_hash` / `options_hash` はシーンと設定で変える。

### 2.3 チェックポイントの前提

- `app/sgx/sgnano_app_sgx.c` の `s_open` は 4096 バイトブロック、
  キャッシュ 8、`SGX_STORE_WRITEBACK | SGX_STORE_CREATE` を使う。
- ファイルサイズは **64KB の倍数**に丸める（640×480 では 655,360 B）。
- **`store_open` の CREATE（0 埋め）を避ける**ため、フロントエンド側で
  先に `sgx->write_file(path, zeros, 655360)` でファイルを作る。
  `povprobe.c` の `precreate_checkpoint()` をそのまま使える。
- 完成時は `sgnano_app_finish(true)` が PNG を書き、チェックポイントを消す。

### 2.4 スレッド／Task

POV-Ray 3.7 のバックエンドは `boost::thread`・`Task`・`TaskQueue`・POVMS を
使う。現在の `compat_arm/boost/thread.hpp` は**サイズ測定用の空スタブ**で、
スレッドを実際に作らない。フロントエンドでは次のどちらかが必要。

1. **実スレッド化**: `boost::thread` / `mutex` / `condition` /
   `thread_specific_ptr` を SGX ABI の
   `task_create` / `task_join` / `mutex_*` / `sem_*` / `yield` で実装する。
2. **単一スレッド化**: `Task::Start` をその場で `TaskThread` 実行にし、
   `TaskQueue` / POVMS を同期化する。描画中の停止・状態取得は制限される。

最初は **1 スレッド・AA なし**でよい。POV-Ray の `+WT1` / `-A` 相当。

### 2.5 ファイル I/O

SGX の SDK libc には `fopen` / `fread` が無い。POV-Ray の
`FileInput` / パーサは `FILE*` を使うため、次のどちらかが必要。

- `fopen` / `fread` / `fseek` / `ftell` / `fclose` の読み取り専用シムを作り、
  `sgx->read_file()` でファイル全体を PSRAM に読んで `FILE*` の代わりにする。
- POV-Ray の `FileInput` 層を SGX の `read_file` / `ra_*` へ差し替える。

シーンと include は読み取り専用。出力は `sgnano_app.c` の PNG 層に任せ、
POV-Ray のファイル出力（`+O` の PPM など）は使わない。

### 2.6 VFE の扱い

ホスト版は `vfe/unix`（`unixconsole.cpp`、`unixoptions.cpp`、
`platformbase.cpp`）と `unix/disp_text.cpp` を使う。これらは
POSIX（pthread、signal、sysconf、stdio）に依存するため、SGX では
そのまま使えない。次のどちらか。

- `vfeSession` / `vfeRenderOptions` は残し、`vfe/unix` の
  platform/console/options だけ SGX 版に置き換える。
- POV-Ray バックエンド（`RenderBackend`）を直接叩く最小フロントエンドを作る。

ホストの `app/host/sgnano_display.cpp` と
`third_party/povray/vfe/unix/unixconsole.cpp` の
`UnixDisplayCreator` 周辺が参考になる。

## 3. ビルド

プローブと同じ `sgx_add_program` を使う。

```sh
cmake -S . -B build_sgx -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake \
  -DSGNANO_BUILD_SGX_PROBE=ON
ninja -C build_sgx povprobe
```

POV-Ray 本体を追加するときは `CMakeLists.txt` の
`SGNANO_BUILD_SGX_PROBE` ブロックに `sgx_add_program(povray_sgx ...)` を足す。
`POVRAY_CORE_SOURCES` は `cmake/povray_sources.cmake`。
`SGNANO_PNG_SOURCES`（miniz）と `app/sgnano_app.c`、
`app/sgx/sgnano_app_sgx.c` をリンクする。

### 必須のビルドフラグ

`-fno-builtin` を付ける。付けないと SDK の `sgx_libc.c` の `calloc` が
GCC の「`malloc`＋`memset(0)` → `calloc`」変換で**無限再帰**する
（実機で GDB 確認済み）。`-fno-builtin-calloc` だけでは止まらない。

```cmake
OPTIONS -fno-builtin
```

### サイズの目安

ARM `-Os` で POV-Ray コア `source/` は約 1.41MB（libc 別）。
SGX の像の上限は 4MB。スタックは最初 64KB 程度から。

## 4. 実機の注意点

- **ドライブ文字は USB 再接続で変わる**（38GB HDD の exFAT が B: ⇔ C:）。
  `tools/hw_probe.py` の `ensure_dir()` のように毎回 `DRIVES` を見て、
  type 3（exFAT）／type 4（NTFS）の `POVTEST` を探し直す。
- `RBRUN <path>\t<arg>...` で起動。引数は最大 4 つ。
- `RSTOP` で止まらないときはもう一度 `RSTOP`。`RSTAT` が 1 のままなら
  `scripts/flash_swd.sh --reset` でリセット（USB の再走査に約 15 秒）。
- `read_file` は 8/33/512 バイトのような**小さな max で失敗**することがある。
  ヘッダ確認などはファイル全体を読む。
- `fb_lend_spare` はページを**高い番地から**返す。番地順に並べ替えないと
  「連続した 1.8MB」として使えない（`sgnano_sgx_lend_spare()` でソート済み）。
- `store_open` は mode 1 で。mode 2 のまま開くと 38GB exFAT で止まる。
- PNG のエンコードは miniz deflate level 9。640×480 で数秒。

## 5. B: ドライブの配置

- `B:/POVTEST/scenes/` — テストシーン 15 ファイル
  （13 の `.pov`、`README.md`、`generate_scenes.py`）
- `B:/POVTEST/renders/` — ホスト版で描いた 13 の PNG＋`summary.txt`
- `B:/POVTEST/renders/logs/` — 13 の描画ログ
- `B:/POVTEST/` — `povprobe.sgx`、`storetest.sgx`、
  プローブのレポート・PNG・raw

## 6. テスト

```sh
# ホスト
./tools/test_host.sh
./build_host/test_sgx_checkpoint

# 実機プローブ（3 パス＋PNG/生データ照合）
python3 tools/hw_probe.py

# ホスト版 POV-Ray で全シーン（5 分上限）
python3 -u tools/render_all_scenes.py --timeout 300 --width 640 --height 480
```

## 7. 次の具体的な順番

1. `povray_sgx` の最小 main を作り、`00_fast_sphere.pov` を 160×120 で
   `sgnano_app_put_block` まで通す（スレッドはまず 1 本）。
2. 320×240 → 640×480 に上げ、チェックポイント停止→再開を確認。
3. `11_slow_spheres.pov` を含む全シーンを実機で 5 分上限で走らせる。
4. 重いシーンでタイムアウトしたら、チェックポイントから再開できることを確認。

## 8. 参考ファイル

- `app/sgnano_app.c` / `app/sgnano_app.h` — 共通層（チェックポイント、PNG、進捗）
- `app/sgx/sgnano_app_sgx.c` / `.h` — SGX プラットフォーム（mode 1/2、store、spare）
- `apps/povprobe/povprobe.c` — 実機プローブ。初期化順・precreate・検証の実例
- `app/host/sgnano_display.cpp` — ホストの POV-Ray 表示層
- `third_party/povray/vfe/unix/unixconsole.cpp` — ホストの VFE main
- `cmake/povray_sources.cmake` — POV-Ray コアのソース一覧
- `platform/sgx/sgx4_stub.h` — 公式 ABI 4.0 とホストスタブの切り替え
- `docs/HW_PROBE.md` — 実機プローブの詳細
- `docs/SGX_STUB.md` — ABI 4.0 と SGX 側の前提
- `docs/TESTING.md` — テスト手順と結果

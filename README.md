# sgnano_povray

POV-Ray 3.7 を sgnano（RP2350 のビデオコンソール）の **SGX アプリ**として動かすための移植プロジェクト。

- 本体ファームとは分離し、AGPL の POV-Ray コードをファームにリンクしない。
- 画面は **mode 2（テキスト＋640×480 RGB555）**、フレームバッファは1ページを想定。
- 途中で終了しても **1 ライン単位のチェックポイント**から再開できる。
- 描画中はテキスト面（ホストでは stderr）に進捗・ETA・チェックポイント状態を出す。

## いまの状態

| 項目 | 状態 |
|---|---|
| POV-Ray 3.7.0.10 の取り込み | 完了 |
| ホスト版のビルド | 完了（`build_host/povray_host`） |
| 簡単なシーンの描画 | 完了（`tests/scenes/simple.pov`） |
| チェックポイント／再開 | 完了（ホストで途中終了→再開→完成画像の一致を確認） |
| SGX 4.0 ABI | 正式版を受領（`third_party/sgnano/sgx_api.h`、ブロックストア＋`fb_lend_spare`） |
| SGX 側のスタブ | 正式 ABI 4.0 の型に更新済み（`platform/sgx/sgx4_stub.h`、`app/sgx/`） |
| FB の余りページ | `fb_lend_spare` で 3 ページ（約1.8MB）を借り、PNG 作業場に使用 |
| 出力 | FB / 可逆圧縮 PNG（miniz）を選択可能 |
| 実機プローブ | `apps/povprobe`（mode 2・チェックポイント・PNG・38GB HDD） |
| 進捗表示 | テキスト面に進捗バー＋UART に `progress` 行 |
| ARM サイズ測定 | 完了（`source/` 全体で 1.41MB、libc 別） |

## クイックスタート（ホスト）

```sh
cmake -S . -B build_host -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build_host

python3 tools/run_host.py tests/scenes/simple.pov build_host/simple.png \
  --width 640 --height 480 --output png
```

`--output` は `fb`（画面に残すだけ）、`png`（可逆圧縮 PNG を保存）、
`both`（PNG＋FB）から選べる。既定は `both`。

途中で止めても、同じコマンドで続きから描画します。詳しくは
[`docs/BUILD.md`](docs/BUILD.md)。

### ARM サイズ（Phase 0）

`arm-none-eabi-g++ -Os` で `source/` 全体（142 オブジェクト）をコンパイルした結果:

| 区分 | サイズ |
|---|---:|
| `.text` | 1,234,352 B |
| `.data` | 35,750 B |
| `.bss` | 137,042 B |
| **合計** | **1,407,144 B** |

libc / libstdc++ は別。詳細は [`docs/PHASE0_SIZE.md`](docs/PHASE0_SIZE.md)。
コードは 1ページ FB の PSRAM に余裕で収まるため、最初は「コード常駐＋
メッシュ／テクスチャだけブロックストア」で進められます。

### テストシーン

`tests/scenes/` に 12 本ある（建築・車・都市・砂漠・水中・宇宙・静物・
クリスタル・森など）。色は sRGB で、反射・ガラス・発光を使う。

- `00_fast_sphere.pov`: すぐ終わるスモークテスト
- `11_slow_spheres.pov`: 10,000 球の重いテスト（640×480 では数分）
- 一覧と使い方: [`tests/scenes/README.md`](tests/scenes/README.md)
- まとめて描く: `tools/render_scenes.sh 320 240 build_host/scenes`

### 確認済みの動作

- 640×480、1 スレッド、AA なし、4×4 ブロックで描画。
- 2 秒で強制終了 → チェックポイント 97/480 ライン（v2、ヘッダ 2 部＋CRC32）。
- 再実行で 97 行目から再開し、480/480 まで完成。
- 再開して完成した PNG と、最初から描いた PNG がバイト単位で一致。

### 実機プローブ（SGX）

POV-Ray 本体の前に、実機でアプリ層を確かめる `povprobe.sgx` があります。

```sh
cmake -S . -B build_sgx -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake \
  -DSGNANO_BUILD_SGX_PROBE=ON
ninja -C build_sgx povprobe

# 実機（38GB HDD の exFAT/NTFS パーティション。文字は再接続で変わります）
RBRUN C:/POVTEST/povprobe.sgx	C:/POVTEST	fresh	120
RBRUN C:/POVTEST/povprobe.sgx	C:/POVTEST	resume
RBRUN C:/POVTEST/povprobe.sgx	C:/POVTEST	fresh

# 自動で 3 パス＋PNG/生データの照合まで
python3 tools/hw_probe.py
```

描画中は画面に `line 120/480 (25.0%)` と進捗バー、UART に
`progress 128/480 (26.6%) 1234 ms` を出します。詳しくは
[`docs/HW_PROBE.md`](docs/HW_PROBE.md)。

#### 実機テスト結果（2026-11-06）

- USB 3.0 の **38,154MB HDD**（exFAT）を使用。ドライブ文字は再接続で
  B: ⇔ C: が入れ替わるため、毎回自動検出する。
- sgnano の `storetest.sgx`: **47/47 合格**。
  4KB×64 の作成 869ms、64KB×32（2MB）の作成 3289ms。
- `povprobe`: 120/480 で停止（6.4 秒、チェックポイント 655,360 B）→
  再開して 480/480 完成（24.1 秒）。最初からの 480 行は 28.3 秒
  （実描画 18.8 秒＋PNG 符号化・生データ書き出し・3 秒表示）。
- 再開後の FB 検証: **0 bad rows / 0 bad pixels**、FB CRC `30d0c6dd`。
- 生データ 614,400 B の CRC も `30d0c6dd`。
- PNG 42,966 B（640×480）をデコードし、RGB555 生データと
  **1 画素も違わず一致**。resume と fresh の PNG・生データはバイト単位で一致。
- `fb_lend_spare`: 1,843,200 B（3 ページ）を借り、PNG の RGB 作業場に使用。
- チェックポイントは完成後に削除（`final checkpoint 0 B`）。
- テストシーン 15 ファイルを **`B:/POVTEST/scenes`** へ転送済み
  （13 の `.pov`、`README.md`、`generate_scenes.py`）。
- 全 13 シーンをホスト版 640×480 で表示テスト: **すべて完走**。
  6.2〜10.2 秒/シーン（5 分上限に対して余裕）。PNG は
  **`B:/POVTEST/renders`**、ログは `B:/POVTEST/renders/logs` へ転送済み。
- 実機のファイラーで 12 枚の PNG を表示確認（640×480、`view=1`）。
  `11_slow_spheres.png`（336KB）は開いて 92%（448 行）まで表示した約 22 秒で
  ビューアが閉じ、ファイラーへ戻った（5 分上限には達していない）。
- **SGX 版 POV-Ray の実行ファイルはまだ未完成**。実機で `.pov` を直接
  描くには SGX フロントエンド（VFE 置き換え、単一スレッド Task）が必要。
  現在実機で動くのは `povprobe.sgx`（アプリ層の検証）まで。
- 途中で見つけた SDK の `calloc` 無限再帰は `-fno-builtin` で回避。

## 設計の要点

RP2350 の Cortex-M33 には MMU がないため、一般的な透過ページングは作れない。
そこで SGX 4.0 では次の形にする。

- **プログラム**: 正式 ABI 4.0 では常駐（ARM の `-Os` で約 1.4MB、libc 別）。
  オーバーレイは 4.x で必要になったら足す。
- **ヒープ**: ハンドル付きブロックストア（メッシュ、テクスチャ、シーン DB）。
  実機の 38GB HDD（exFAT）では mode 2 のまま `store_open` すると止まるため、
  ストアを開く間だけ mode 1（テキスト）にし、開いてから mode 2 へ入る。
- **画面**: POV-Ray の画像バッファ（20B/画素）を持たず、RGB555 で FB へ直接書く。
  mode 2 の余り 3 ページ（約 1.8MB）は `fb_lend_spare` で借り、PNG の作業場と
  将来の POV-Ray ヒープに使う。ページは番地の順に並べ替えてから使う。

POV-Ray 本体の SGX フロントエンド（`vfe/unix` の置き換え、単一スレッド Task、
ファイル I/O）は別担当向けに
[`docs/HANDOFF_SGX_FRONTEND.md`](docs/HANDOFF_SGX_FRONTEND.md)
へ引き継ぎ済みです。

詳細は [`SPEC_sgx4_overlay_store.md`](SPEC_sgx4_overlay_store.md) と
[`docs/SGX_STUB.md`](docs/SGX_STUB.md)、sgnano 側の引き渡し
[`docs/SGX_ABI4_FORMAL.md`](docs/SGX_ABI4_FORMAL.md)。正式 ABI 4.0 は
[`third_party/sgnano/sgx_api.h`](third_party/sgnano/sgx_api.h)
（ブロックストア `store_*` を含む）。`include/sgx4_api.h` は以前の設計案で、
いまは正式 ABI が正。

## リポジトリ構成

```
app/                     sgnano アプリ共通層（チェックポイント、PNG、状態、ホスト FB）
app/sgx/                 SGX 用プラットフォーム実装（正式 ABI 4.0）
platform/sgx/            このアプリが使う ABI 4.0 の口（正式ヘッダと同じ型）
cmake/                   上流 Makefile.am から生成したソース一覧
compat/                  Boost.Thread / jversion のホスト互換層
compat_arm/              単一スレッドターゲット用の Boost.Thread スタブ
docs/                    ビルド手順、上流からの変更点
include/sgx4_api.h       以前の ABI 設計案（いまは正式 ABI が正）
platform/host/config.h   ホスト用 config.h
tests/scenes/            テストシーン 12 本（高速・低速を含む）
tests/test_sgx_checkpoint.c  SGX スタブのチェックポイント／PNG テスト
third_party/povray/      POV-Ray 3.7.0.10（AGPL、ローカルパッチあり）
third_party/boost/       Boost 1.62.0 ヘッダ（Boost License）
third_party/sgnano/      正式 SGX ABI 4.0 ヘッダ（MIT）
third_party/miniz/       PNG 用 miniz 3.1.2（MIT）
tools/run_host.py        チェックポイント付き実行ラッパー（PNG/FB 出力）
```

## 上流からの変更

POV-Ray 3.7.0.10 に対する変更は
[`docs/UPSTREAM_CHANGES.md`](docs/UPSTREAM_CHANGES.md) にまとめてある。
パッチは `third_party/povray/patches/` に置いている。

## ライセンス

- アプリ本体: **AGPL-3.0-or-later**（POV-Ray 3.7 に基づくため）。[`LICENSE`](LICENSE)
- `include/sgx4_api.h`（設計案）: **MIT**。[`LICENSES/MIT.txt`](LICENSES/MIT.txt)
- sgnano SGX ABI 4.0 ヘッダ: **MIT**（[`third_party/sgnano/sgx_api.h`](third_party/sgnano/sgx_api.h)）
- POV-Ray: AGPL-3.0-or-later（[`third_party/povray/COPYING`](third_party/povray/COPYING)）
- Boost: Boost Software License 1.0（[`third_party/boost/LICENSE_1_0.txt`](third_party/boost/LICENSE_1_0.txt)）
- miniz: MIT（[`third_party/miniz/LICENSE`](third_party/miniz/LICENSE)）

配布時は POV-Ray のソースと本リポジトリの変更点を含めること。詳細は
[`NOTICE`](NOTICE) を参照。開発の決まりは [`CONTRIBUTING.md`](CONTRIBUTING.md)。

## ロードマップ

1. 実機が空くのを待ち、SGX 4.0 の `store_*` と mode 2 で起動する。
2. POV-Ray の SGX 用フロントエンド（`vfe/unix` の置き換え）を作る。
3. メッシュ／テクスチャをブロックストアへ載せる。
4. Boost をさらに削り、実機のメモリと速度を詰める。
5. 実機でチェックポイント再開と PNG 保存を確認する。

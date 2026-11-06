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
| SGX 4.0 ABI の設計 | 案を作成（`SPEC_sgx4_overlay_store.md`） |
| SGX 側のスタブ | 実装済み（`platform/sgx/sgx4_stub.h`、`app/sgx/`） |
| ARM サイズ測定 | 完了（`source/` 全体で 1.41MB、libc 別） |

## クイックスタート（ホスト）

```sh
cmake -S . -B build_host -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build_host

python3 tools/run_host.py tests/scenes/simple.pov build_host/simple.ppm \
  --width 640 --height 480
```

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

### 確認済みの動作

- 640×480、1 スレッド、AA なし、4×4 ブロックで描画。
- 0.6 秒で強制終了 → チェックポイント 32/480 ライン。
- 再実行で 32 行目から再開し、480/480 まで完成。
- 再開して完成した PPM と、最初から描いた PPM の SHA-256 が一致。

## 設計の要点

RP2350 の Cortex-M33 には MMU がないため、一般的な透過ページングは作れない。
そこで SGX 4.0 では次の形にする。

- **プログラム**: 常駐コア＋固定スロットのオーバーレイ。
- **ヒープ**: ハンドル付きブロックストア（メッシュ、テクスチャ、シーン DB）。
- **画面**: POV-Ray の画像バッファ（20B/画素）を持たず、RGB555 で FB へ直接書く。

詳細は [`SPEC_sgx4_overlay_store.md`](SPEC_sgx4_overlay_store.md) と
[`include/sgx4_api.h`](include/sgx4_api.h)。SGX 側のスタブは
[`docs/SGX_STUB.md`](docs/SGX_STUB.md)。

## リポジトリ構成

```
app/                     sgnano アプリ共通層（チェックポイント、状態、ホスト FB）
app/sgx/                 SGX スタブ用のプラットフォーム実装
platform/sgx/            正式 ABI が来るまでの SGX 関数表
cmake/                   上流 Makefile.am から生成したソース一覧
compat/                  Boost.Thread / jversion のホスト互換層
compat_arm/              単一スレッドターゲット用の Boost.Thread スタブ
docs/                    ビルド手順、上流からの変更点
include/sgx4_api.h       SGX 4.0 ABI の案（MIT）
platform/host/config.h   ホスト用 config.h
tests/scenes/            テストシーン
tests/test_sgx_checkpoint.c  SGX スタブのチェックポイントテスト
third_party/povray/      POV-Ray 3.7.0.10（AGPL、ローカルパッチあり）
third_party/boost/       Boost 1.62.0 ヘッダ（Boost License）
tools/run_host.py        チェックポイント付き実行ラッパー
```

## 上流からの変更

POV-Ray 3.7.0.10 に対する変更は
[`docs/UPSTREAM_CHANGES.md`](docs/UPSTREAM_CHANGES.md) にまとめてある。
パッチは `third_party/povray/patches/` に置いている。

## ライセンス

- アプリ本体: **AGPL-3.0-or-later**（POV-Ray 3.7 に基づくため）。[`LICENSE`](LICENSE)
- `include/sgx4_api.h`: **MIT**（sgnano SDK と合わせるための提案ヘッダ）。[`LICENSES/MIT.txt`](LICENSES/MIT.txt)
- POV-Ray: AGPL-3.0-or-later（[`third_party/povray/COPYING`](third_party/povray/COPYING)）
- Boost: Boost Software License 1.0（[`third_party/boost/LICENSE_1_0.txt`](third_party/boost/LICENSE_1_0.txt)）

配布時は POV-Ray のソースと本リポジトリの変更点を含めること。詳細は
[`NOTICE`](NOTICE) を参照。開発の決まりは [`CONTRIBUTING.md`](CONTRIBUTING.md)。

## ロードマップ

1. ホストでチェックポイント／再開を完成させる。
2. `SgnanoDisplay` の FB 出力を SGX スタブへ載せ替える。
3. SGX 4.0 のオーバーレイ／ブロックストアを sgnano 本体の正式 ABI に合わせる。
4. POV-Ray の常駐コア／モジュール分割、Boost 除去、メッシュのアウトオブコア化。
5. 実機で mode 2 の描画と再開を確認する。

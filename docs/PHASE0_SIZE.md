# Phase 0: ARM サイズ測定

POV-Ray 3.7 の常駐コア／モジュール分割を決めるため、RP2350（Cortex-M33）向けに
`source/` のオブジェクトをコンパイルしてサイズを測る。

## 方法

```sh
cmake -S . -B build_arm -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake \
  -DSGNANO_BUILD_ARM_SIZE=ON
ninja -C build_arm povray_arm_size
python3 tools/arm_size.py
```

- コンパイラ: `arm-none-eabi-g++` 10.3.1
-  flags: `-mcpu=cortex-m33 -mthumb -mfloat-abi=softfp -Os -ffunction-sections -fdata-sections`
- リンクはしない。`source/` のオブジェクトの `.text` / `.data` / `.bss` を合計する。
- `vfe/unix` のフロントエンド（ホスト用）は含めない。
- PNG / JPEG / TIFF / OpenEXR / zlib は無効。Boost.Thread は `compat_arm` の
  単一スレッド・スタブ。Boost.DateTime は使わない。
- libc / libstdc++ のコードは含まれない（別途、リンク時に加算する）。

## 見るもの

- `source/` 全体の合計。
- 上位 20 オブジェクト（parser、pattern、shape、render など）。
- 常駐コアに入れる候補（math、render、scene、object 基底）と、
  モジュールに回す候補（parser、shape の一部、texture、media）の比率。

## 注意

この測定は「コードの大きさ」だけを見る。実際の PSRAM 使用量は、
- 常駐イメージ＋`.bss`
- オーバーレイ窓
- ストアキャッシュ
- ヒープ
- FB（mode 2、1ページ）

の合計で決まる。`SPEC_sgx4_overlay_store.md` の予算表と合わせて判断する。

## 2026-11-06 の実測

条件:

- POV-Ray 3.7.0.10 の `source/` 全体（142 オブジェクト）
- `arm-none-eabi-g++` 10.3.1
- `-mcpu=cortex-m33 -mthumb -mfloat-abi=softfp -Os -ffunction-sections -fdata-sections`
- PNG / JPEG / TIFF / OpenEXR / zlib 無効
- Boost.Thread は `compat_arm` の単一スレッド・スタブ
- `vfe/unix` のホスト用フロントエンドは含めない
- リンクはしていない（libc / libstdc++ のコードは含まない）

| 区分 | サイズ |
|---|---:|
| `.text`（コード・定数） | **1,234,352 B**（1,205.4 KiB） |
| `.data` | 35,750 B（34.9 KiB） |
| `.bss` | 137,042 B（133.8 KiB） |
| **合計** | **1,407,144 B**（1,374.2 KiB） |

大きいオブジェクト（上位 10）:

| `.text` | `.data` | `.bss` | ファイル |
|---:|---:|---:|---|
| 95,190 | 0 | 0 | `source/base/image/image.cpp` |
| 67,947 | 4 | 3,365 | `source/backend/pattern/pattern.cpp` |
| 67,845 | 4 | 61 | `source/backend/parser/parse.cpp` |
| 48,853 | 0 | 0 | `source/backend/render/trace.cpp` |
| 36,936 | 0 | 0 | `source/base/font/timrom.cpp` |
| 32,351 | 0 | 0 | `source/backend/parser/parstxtr.cpp` |
| 31,972 | 0 | 0 | `source/base/font/cyrvetic.cpp` |
| 31,836 | 24 | 0 | `source/frontend/renderfrontend.cpp` |
| 29,885 | 100 | 32 | `source/backend/support/randomsequences.cpp` |
| 29,545 | 0 | 0 | `source/backend/vm/fnintern.cpp` |

### 考察

- 以前の見積もり（常駐コア 2.5〜3.0MB）よりかなり小さい。`-Os` と
  PNG/JPEG/TIFF/OpenEXR の無効化、単一スレッド化が効いている。
- libc / libstdc++ をリンクすると +0.5〜1MB 程度を見込む。それでも
  **2MB 前後**で、1ページ FB の PSRAM 予算（7.7MB 空き）に十分収まる。
- 画像処理（`image.cpp` 95KB）、フォント（POV-Ray のロゴ用 85KB）、
  ホスト用フロントエンド（56KB）は、SGX 向けに削る候補。
- **コードのオーバーレイは、サイズだけなら必須ではない。** ただし
  - PSRAM をヒープ／ストアキャッシュに多く残す
  - 将来の機能有効化（PNG、media、radiosity など）
  - 起動時間の短縮
  のため、SGX 4.0 のオーバーレイ機構は残す価値がある。
- ヒープ側（メッシュ、テクスチャ、シーン DB）のブロックストアは引き続き必要。
  コードは常駐、データだけストアへ逃がす構成が最初の現実的な形。

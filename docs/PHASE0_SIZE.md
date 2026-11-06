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

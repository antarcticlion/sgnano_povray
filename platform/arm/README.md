# platform/arm

RP2350（Cortex-M33）向けのサイズ測定・将来のターゲット用プラットフォーム定義。

- `config.h`: autoconf の `config.h` の代わり。PNG/JPEG/TIFF/OpenEXR/zlib は無効。
- `syspovconfig.h`: `vfe/unix` の代わり。単一スレッド、`thread_local` なし、
  `POV_PARSE_PATH_STRING` は `pov_arm::ParsePathString`。
- `syspovconfigbase.h` / `syspovconfigbackend.h` / `syspovconfigfrontend.h`:
  `vfe/unix` の対応ファイルを置き換える。

ビルド:

```sh
cmake -S . -B build_arm -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake \
  -DSGNANO_BUILD_ARM_SIZE=ON
ninja -C build_arm povray_arm_size
python3 tools/arm_size.py
```

この構成は **サイズ測定用** で、ファイル I/O・POVMS・画面の実体はまだ持たない。
実機では `app/sgx/` のスタブと、sgnano の正式 ABI に置き換える。

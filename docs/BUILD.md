# ビルドと実行（ホスト）

## 必要なもの

- CMake 3.16 以上
- Ninja
- C++11 が使える GCC / Clang
- libpng 開発パッケージ
- libjpeg 開発パッケージ
- zlib 開発パッケージ

Ubuntu 22.04 なら:

```sh
sudo apt-get install -y cmake ninja-build g++ libpng-dev libjpeg-dev zlib1g-dev
```

## ビルド

```sh
cmake -S . -B build_host -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build_host
```

`build_host/povray_host` ができる。

## 普通に描く

```sh
./build_host/povray_host \
  +Itests/scenes/simple.pov \
  +O/tmp/simple.png \
  +W320 +H240 +FN -A -V \
  +Lthird_party/povray/include
```

## チェックポイント付きで描く（再開できる）

```sh
python3 tools/run_host.py tests/scenes/simple.pov build_host/simple.png \
  --width 640 --height 480 --output png
```

- チェックポイントは `build_host/simple.png.ckp`。
- 描画中に強制終了しても、次回同じコマンドで続きから描く。
- 完成すると **可逆圧縮 PNG** を書いて `.ckp` を消す。
- 途中の状態を見る:

  ```sh
  python3 tools/run_host.py tests/scenes/simple.pov build_host/simple.png --status
  ```

- 最初から描き直す:

  ```sh
  python3 tools/run_host.py tests/scenes/simple.pov build_host/simple.png --fresh
  ```

## 出力の選択

`run_host.py` の `--output`:

| 値 | 意味 |
|---|---|
| `png` | 可逆圧縮 PNG を保存 |
| `fb` | ファイルを作らず、FB に描くだけ |
| `both` | PNG を保存し、FB にも残す（既定） |

PNG は miniz の deflate（レベル 9）で可逆圧縮する。無圧縮ではない。

## 仕組み

- POV-Ray の表示ブロック（4×4）を `SgnanoDisplay` が受け取る。
- 1 ライン分の画素が揃ったら、RGB555 の 1 ラインをチェックポイントへ書く。
- チェックポイントは「256 バイトのヘッダ＋480 ライン×1280 バイト」。
- 再開時は `+SR<最初の未完成ライン>` を POV-Ray に渡し、既存のラインを FB に戻す。
- 状態表示はホストでは stderr。SGX ではテキスト面に進捗バーを出す。

## ターゲット（SGX）

sgnano の SGX ABI 5.11 向けに、2 つの `.sgx` をビルドできる。

- `povray.sgx` — POV-Ray 本体
- `povprobe.sgx` — 実機確認用の小さなテスト

SDK は `third_party/sgnano/sdk/sgx/` に同梱してある（MIT）。sgnano の
別チェックアウトは要らない。別の SDK を使うときは
`-DSGNANO_SGX_SDK_DIR=...` で差し替える。

```sh
cmake -S . -B build_sgx -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake \
  -DSGNANO_BUILD_SGX_POVRAY=ON \
  -DSGNANO_BUILD_SGX_PROBE=ON
ninja -C build_sgx povray povprobe
```

できるもの:

- `build_sgx/sgx/povray.sgx` — POV-Ray 3.7（約 1.58MB、再配置込み）
- `build_sgx/sgx/povprobe.sgx` — ハードウェアプローブ

実機では `povray.sgx` とシーンを USB へ置いて `RBRUN` で走らせる:

```text
RBRUN B:/POVTEST/povray.sgx	B:/POVTEST/scenes/00_fast_sphere.pov	+W160	+H120	-fresh
```

`povprobe.sgx` の 3 パス（fresh → resume → fresh）と PNG/生データ照合は
`SGNANO_ROOT=$HOME/Project/sgnano python3 tools/hw_probe.py` で自動化できる
（`SGNANO_ROOT` は ZMODEM ヘルパー `scripts/zm.py` のためだけに要る）。
ドライブ文字は USB の再接続で変わるため、スクリプトは毎回 exFAT/NTFS の
`POVTEST` を探し直す。詳しくは [`HW_PROBE.md`](HW_PROBE.md)。

**ビルドの注意**: 以前の SDK では `sgx_libc.c` の `calloc` が GCC の
`malloc`＋`memset(0)` → `calloc` 変換で無限再帰にコンパイルされることが
あった。ABI 4.1 以降の SDK は `memset` を関数ポインタ越しに呼ぶ形へ直って
いるため、このリポジトリでは `-fno-builtin` を付けていない。

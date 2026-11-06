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
  +O/tmp/simple.ppm \
  +W320 +H240 +FP -A -V \
  +Lthird_party/povray/include
```

## チェックポイント付きで描く（再開できる）

```sh
python3 tools/run_host.py tests/scenes/simple.pov build_host/simple.ppm \
  --width 640 --height 480
```

- チェックポイントは `build_host/simple.ppm.ckp`。
- 描画中に強制終了しても、次回同じコマンドで続きから描く。
- 完成すると PPM を書いて `.ckp` を消す。
- 途中の状態を見る:

  ```sh
  python3 tools/run_host.py tests/scenes/simple.pov build_host/simple.ppm --status
  ```

- 最初から描き直す:

  ```sh
  python3 tools/run_host.py tests/scenes/simple.pov build_host/simple.ppm --fresh
  ```

## 仕組み

- POV-Ray の表示ブロック（4×4）を `SgnanoDisplay` が受け取る。
- 1 ライン分の画素が揃ったら、RGB555 の 1 ラインをチェックポイントへ書く。
- チェックポイントは「256 バイトのヘッダ＋480 ライン×1280 バイト」。
- 再開時は `+SR<最初の未完成ライン>` を POV-Ray に渡し、既存のラインを FB に戻す。
- 状態表示はホストでは stderr。SGX ではテキスト面に出す予定。

## ターゲット（SGX）

まだスタブ段階。sgnano 本体の ABI が止まっているため、`include/sgx4_api.h`
のスタブ実装を用意し、ホストと同じ `app/sgnano_app.c` を使う予定。

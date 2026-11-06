# テスト

## ホストのスモークテスト

```sh
cmake -S . -B build_host -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build_host

python3 tools/run_host.py tests/scenes/simple.pov build_host/test.ppm \
  --width 160 --height 120
```

- `build_host/test.ppm` ができる。
- 完成すると `build_host/test.ppm.ckp` は消える。

## SGX スタブのチェックポイントテスト

`test_sgx_checkpoint` は、SGX スタブの関数表（メモリ FB、テキスト出力、
ブロックストア）の上で `app/sgnano_app.c` を動かす。

```sh
./build_host/test_sgx_checkpoint
```

- 160×120 で 10 ライン描いて終了し、チェックポイントが残ることを確認。
- 再初期化して 10 行目から再開し、120 ラインまで描く。
- 完成後にチェックポイントが消えること、FB の先頭と末尾の画素を確認。
- 状態表示がテキスト面（ホストでは stdout）へ 4 行で出る。

## チェックポイント／再開のテスト

1. 途中で強制終了する（0.6 秒で kill）:

   ```sh
   rm -f build_host/resume.ppm build_host/resume.ppm.ckp
   python3 tools/run_host.py tests/scenes/simple.pov build_host/resume.ppm \
     --width 640 --height 480 --timeout 0.6
   ```

   `--timeout` は POV-Ray を kill し、チェックポイントを残す。

2. 状態を確認:

   ```sh
   python3 tools/run_host.py tests/scenes/simple.pov build_host/resume.ppm \
     --width 640 --height 480 --status
   ```

3. 再開して完成させる:

   ```sh
   python3 tools/run_host.py tests/scenes/simple.pov build_host/resume.ppm \
     --width 640 --height 480
   ```

4. 最初から描いたものと比べる:

   ```sh
   python3 tools/run_host.py tests/scenes/simple.pov build_host/fresh.ppm \
     --width 640 --height 480 --fresh
   cmp build_host/resume.ppm build_host/fresh.ppm
   ```

2026-11-06 の実測では、640×480 で 32/480 ラインから再開し、
`resume.ppm` と `fresh.ppm` の SHA-256 が一致した。

## チェックポイントの形式

- ヘッダ 256 バイト。
  - magic `SGNPCKP1`
  - version, width, height, line_bytes, block_size
  - scene_hash, options_hash
  - lines_done, first_missing, updated_ms
  - 完了ラインのビットマップ（480 ライン = 60 バイト）
- その後ろに `height * width * 2` バイトの RGB555 ライン。
- ラインが揃うたびに、そのラインとヘッダを書き、flush する。
- 完成時にヘッダを閉じてファイルを削除する。

## 注意

- チェックポイントは「シーン＋解像度＋ブロックサイズ＋AA 設定」が変わると無効になる。
- POV-Ray の乱数を使う機能（AA、焦点ぼけ、放射照度など）を有効にすると、
  ライン単位の再開で結果が変わりうる。最初の版では AA なし・1 スレッドで使う。

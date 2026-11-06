# テスト

## ホストのスモークテスト

```sh
cmake -S . -B build_host -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build_host

python3 tools/run_host.py tests/scenes/simple.pov build_host/test.png \
  --width 160 --height 120 --output png
```

- `build_host/test.png` ができる。
- 完成すると `build_host/test.png.ckp` は消える。

## SGX スタブのチェックポイントテスト

`test_sgx_checkpoint` は、SGX スタブの関数表（メモリ FB、テキスト出力、
ブロックストア）の上で `app/sgnano_app.c` を動かす。

```sh
./build_host/test_sgx_checkpoint
```

- 160×120 で 10 ライン描いて終了し、チェックポイントが残ることを確認。
- 再初期化して 10 行目から再開し、120 ラインまで描く。
- 完成後にチェックポイントが消えること、FB の先頭と末尾の画素を確認。
- 後半は `--output png` 相当（`SGNANO_OUTPUT_PNG`）で走らせ、
  `build_host/sgx_test.png` が正しい PNG 署名で書かれることを確認。
- 状態表示がテキスト面（ホストでは stdout）へ 4 行で出る。

## テストシーン

`tests/scenes/` に 12 本ある。`00_fast_sphere.pov` はスモークテスト、
`11_slow_spheres.pov` は 10,000 球の重いテスト。一覧は
`tests/scenes/README.md`。

```sh
python3 tools/run_host.py tests/scenes/03_car.pov build_host/car.png \
  --width 640 --height 480 --output png
```

### 全シーンの表示テスト（1 ファイル 5 分上限）

```sh
python3 -u tools/render_all_scenes.py --timeout 300 --width 640 --height 480
```

- `build_host/scenes/<name>.png` と `<name>.log` を書く。
- 300 秒で打ち切ったシーンは `timeout` として記録し、チェックポイントを残す。
- 結果は `build_host/scenes/summary.txt`。

2026-11-06 のホスト実測（640×480、1 スレッド、AA なし）:

| シーン | 時間 | PNG |
|---|---:|---:|
| 00_fast_sphere | 6.3s | 21,635 B |
| 01_geo_landscape | 7.1s | 101,380 B |
| 02_arch_perspective | 8.5s | 81,403 B |
| 03_car | 6.5s | 39,576 B |
| 04_city_night | 9.8s | 21,595 B |
| 05_desert_monolith | 6.5s | 38,922 B |
| 06_underwater | 6.2s | 18,748 B |
| 07_space_station | 6.5s | 17,160 B |
| 08_fruit_still_life | 6.3s | 46,062 B |
| 09_abstract_crystal | 10.2s | 24,843 B |
| 10_forest_path | 7.5s | 30,487 B |
| 11_slow_spheres | 7.9s | 336,138 B |
| simple | 6.3s | 22,347 B |

すべて 5 分以内（実際は 11 秒以内）で完走。PNG とログは
`B:/POVTEST/renders`（ログは `renders/logs`）へ転送済み。
実機のファイラーでは 12 枚を 640×480 で表示確認。`11_slow_spheres.png`
（336KB）は開いて 92%（448 行）まで表示した約 22 秒でビューアが閉じ、
ファイラーへ戻った（5 分上限には達していない）。

**注意**: これはホスト版の POV-Ray。SGX 版の POV-Ray 実行ファイルはまだ
未完成（プローブはアプリ層のみ）。SGX 版ができたら同じシーンを実機で
5 分上限で走らせる。

## チェックポイント／再開のテスト

1. 途中で強制終了する（0.6 秒で kill）:

   ```sh
   rm -f build_host/resume.png build_host/resume.png.ckp
   python3 tools/run_host.py tests/scenes/simple.pov build_host/resume.png \
     --width 640 --height 480 --timeout 0.6 --output png
   ```

   `--timeout` は POV-Ray を kill し、チェックポイントを残す。

2. 状態を確認:

   ```sh
   python3 tools/run_host.py tests/scenes/simple.pov build_host/resume.png \
     --width 640 --height 480 --output png --status
   ```

3. 再開して完成させる:

   ```sh
   python3 tools/run_host.py tests/scenes/simple.pov build_host/resume.png \
     --width 640 --height 480 --output png
   ```

4. 最初から描いたものと比べる:

   ```sh
   python3 tools/run_host.py tests/scenes/simple.pov build_host/fresh.png \
     --width 640 --height 480 --fresh --output png
   cmp build_host/resume.png build_host/fresh.png
   ```

2026-11-06 の実測では、640×480 で 97/480 ラインから再開し、
`resume.png` と `fresh.png` がバイト単位で一致した。ヘッダ 2 部＋CRC32 の
v2 形式でも同じ結果を確認している。

## チェックポイントの形式

- ヘッダ 2 部（各 256 バイト、合計 512 バイト）。世代番号の大きい方を採用する。
  - magic `SGNPCKP1`
  - version, generation, width, height, line_bytes, block_size
  - scene_hash, options_hash
  - lines_done, first_missing, updated_ms
  - 完了ラインのビットマップ（480 ライン = 60 バイト）
  - CRC32
- その後ろに `height * width * 2` バイトの RGB555 ライン。
- ラインが揃うたびに、そのラインとヘッダの片方（世代で交互）を書き、
  `fflush`＋`fsync` する（SGX では `store_flush` が同じ役目）。
  ヘッダ更新中に電源が切れても、もう片方の古いヘッダから再開できる。
- 完成時にヘッダを閉じてファイルを削除する。

## 注意

- チェックポイントは「シーン＋解像度＋ブロックサイズ＋AA 設定」が変わると無効になる。
- POV-Ray の乱数を使う機能（AA、焦点ぼけ、放射照度など）を有効にすると、
  ライン単位の再開で結果が変わりうる。最初の版では AA なし・1 スレッドで使う。

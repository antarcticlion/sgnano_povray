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

`tests/scenes/` に 13 本ある（番号付き 12 本と `simple.pov`）。
`00_fast_sphere.pov` はスモークテスト、`11_slow_spheres.pov` は 10,000 球の
重いテスト。一覧は `tests/scenes/README.md`。

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

【追記 2026-11-07】上の表はホスト版。SGX 版（`povray.sgx`）の実機の結果は下の「実機（SGX 版）」。

### 実機（SGX 版、2026-11-07）

sgnano（RP2350、clk_sys 302.4MHz、PSRAM 8MB）で、`C:/POVTEST/bin/povray.sgx C:/POVTEST/scenes/<scene>.pov` を遠隔の
`RBRUN` で 1 本ずつ（上限 20 分、02 は 40 分）。640×480、描画のスレッド 1 本（`+WT1`）、AA なし、ブロック 4×4。描いている間はモード 7
（320×200 の縮小）、終わったらモード 2 で 640×480。チェックポイントは毎行、終わったら PNG。時間は `POVRAY RESULT` の秒（読み込みのあとから）。

| シーン | 実機 | ホスト | 倍 | 結果 |
|---|---:|---:|---:|---|
| 00_fast_sphere | 291.2 s | 6.3 s | 46 | 完走 |
| 01_geo_landscape | 703.2 s | 7.1 s | 99 | 完走 |
| 02_arch_perspective | 1617.7 s | 8.5 s | 190 | 完走（317 行目からチェックポイントが書けなくなった。絵と PNG は正しい。下の注） |
| 03_car | 821.5 s | 6.5 s | 126 | 完走 |
| 04_city_night | 486.1 s | 9.8 s | 50 | 完走 |
| 05_desert_monolith | 444.9 s | 6.5 s | 68 | 完走 |
| 06_underwater | 413.6 s | 6.2 s | 67 | 完走 |
| 07_space_station | 336.1 s | 6.5 s | 52 | 完走 |
| 08_fruit_still_life | 690.7 s | 6.3 s | 110 | 完走 |
| 09_abstract_crystal | 約 1184 s | 10.2 s | 116 | 完走（結果の行がログに残らず、ファームの「ended in 1184370 ms」） |
| 10_forest_path | 344.7 s | 7.5 s | 46 | 完走 |
| 11_slow_spheres | — | 7.9 s | | **解析の途中で PSRAM が尽きる**（球 10,000 個。ホストで malloc の最大 11.2MB） |
| simple | 299.4 s | 6.3 s | 48 | 完走 |

- 実機の PNG はホストの絵と RGB555 の丸め（最大 8/255）の差で一致（00 で確かめた）。
- 02 の途中のチェックポイントの失敗（`why 3` = 行を書くための `store_get` が NULL）のあと、以前の作りでは最後まで 1 行も保存しなかった。
  2026-11-07 に保存の作りを直した（`CHANGELOG.md`）: 失敗を固定せず時間をおいてやり直す・止めたときに書き残しを救い出して `.partial.png` を残す・
  PNG が書けたときだけチェックポイントを消す。ホストの試し `tests/test_sgx_ckp_faults.c`（書き込み・flush・PNG をわざと失敗させる、19/19）。
- 実機で動くまでに直したもの（`CHANGELOG.md` の 2026-11-07）: PSRAM で不可分の命令が効かない、nano の C++ に巻き戻しの表が無い、
  `sscanf` の小数、乱数の表（256KB ずつ）、SDK の malloc（鍵・小さい確保の速い道）、スレッドの優先度、FB を出すと 20 倍遅い（モード 7 の縮小）、
  描くスレッドのスタック（128KB。64KB では 02 であふれた）。ファーム側は sgnano の `docs/notes/system.md` の 2026-11-07 の節。

### 実機（SGX 版、ABI 5.11、2026-11-16）

sgnano（RP2350、PSRAM 8MB）で、ABI 5.11 の `povray.sgx` を `RBRUN` から
1 シーンずつ実行した。320×240、1 スレッド、AA なし、`+BS4`。5 分上限は
`tools/render_all_sgx.py --timeout 300`。時間は RBRUN から終了までの合計と、
`POVRAY RESULT` の描画秒。

| シーン | 合計 | 描画 | 結果 |
|---|---:|---:|---|
| 00_fast_sphere | 56.5 s | 49.9 s | 完走 |
| 01_geo_landscape | 132.5 s | 126.5 s | 完走 |
| 02_arch_perspective | 283.9 s | 278.8 s | 完走 |
| 03_car | 146.8 s | 142.0 s | 完走 |
| 04_city_night | 92.1 s | 85.6 s | 完走 |
| 05_desert_monolith | 87.1 s | 83.3 s | 完走 |
| 06_underwater | 79.3 s | 74.7 s | 完走 |
| 07_space_station | 71.7 s | 66.6 s | 完走 |
| 08_fruit_still_life | 127.7 s | 121.2 s | 完走 |
| 09_abstract_crystal | 204.4 s | 198.5 s | 完走 |
| 10_forest_path | 70.7 s | 65.5 s | 完走 |
| simple | 62.4 s | 56.2 s | 完走 |
| 11_slow_spheres | 321.3 s | — | 5 分で打ち切り（解析中の state 5、ヒープ残り約 79KB） |

- 640×480 の `00_fast_sphere` は **199.8 秒**（描画 193.4 秒）で完走（PNG あり）。
  この PNG もホスト描画を RGB555 に丸めたものと最大 9/255 の差。
- 完走した 12 枚の PNG は 320×240。ホスト版の同じシーンを RGB555 に
  丸めた画像と比べ、チャネル差の最大は 9/255、平均は 0.14〜0.58。
- `11_slow_spheres` は 10,000 球の解析が PSRAM に収まらない（2026-11-07 の
  640×480 でも同じ）。チェックポイントは残るが、5 分以内の表示は不可。

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

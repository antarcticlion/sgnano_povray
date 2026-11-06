# 実機 SGX プローブ（povprobe）

POV-Ray 本体を載せる前に、実機で次の層を確かめるための小さなアプリです。

- mode 2（テキスト＋640×480 RGB555）と 1 ページ FB
- ABI 4.0 の `fb_lend_spare`（余り 3 ページ = 1,843,200 B）
- ABI 4.0 のブロックストア `store_*` による 1 ライン単位のチェックポイント
- 停止 → 再開 → 完成（再開後の絵が最初から描いた絵と一致すること）
- 可逆 PNG（miniz deflate level 9）と RGB555 生データの一致
- 20GB 以上の USB ドライブ（38GB の HDD）での読み書き
- 描画中の進捗表示（テキスト面のバー＋UART の `progress` 行）

## ビルド上の注意（`calloc` の無限再帰）

sgnano SDK の `sgx_libc.c` の

```c
void *calloc(size_t a, size_t b) {
    void *p = malloc(a * b);
    if (p) memset(p, 0, a * b);
    return p;
}
```

は、GCC の「`malloc`＋`memset(0)` → `calloc`」変換により、**`calloc` 自身を
末尾呼び出しするコード**にコンパイルされることがあります（実機で GDB により
確認）。そのため、`calloc` を呼ぶアプリはそこで無限再帰します。

`povprobe` のビルドでは `-fno-builtin` を付けて回避しています
（`CMakeLists.txt` の `sgx_add_program(... OPTIONS -fno-builtin)`）。
`-fno-builtin-calloc` だけでは止まりません。SDK 側でも `sgx_libc.c` に
`-fno-builtin` を付けるのが本筋です。

## 実機で分かったこと（2026-11-06）

- 38GB HDD の exFAT（C:）で `storetest` は 47/47 通った。4KB×64 の
  作成は 869ms、64KB×32（2MB）は 3289ms。
- **mode 2 で FB を出したまま `store_open` すると、実機で止まった**
  （`store: opening ...` のまま 4 分以上）。同じ大きさの普通の
  `write_file` は mode 2 でも 1.2 秒で通る。`create_zero` を避けて
  既存ファイルを開いても止まったため、`store_open` の
  `rbfs_ra_open_psram`（exFAT の置き場の一覧）側と見ている。
- 対策: **ストアを開く間だけ mode 1（テキスト）にし、開いてから
  mode 2 へ入る**。再開するラインは `sgnano_app_restore_fb()` で
  FB へ戻す。`storetest` が mode 1 で通るのと同じ条件。
- チェックポイントのファイルは `write_file` で先に 655,360 B
  （64KB の倍数）作る。`store_open` の CREATE（0 埋め）を通さない。
- `fb_lend_spare` はページを高い番地から返す（FB1, FB2, FB3）。
  番地の順に並べ替えて、連続した 1.8MB として PNG の作業場に使う。

## ビルド

sgnano の SDK の `sgx.cmake` を使います。既定の SDK の場所は
`/home/antarcticlion/Project/sgnano/sdk/sgx` です。違う場所なら
`-DSGNANO_SGX_SDK_DIR=...` を渡してください。

```sh
cmake -S . -B build_sgx -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake \
  -DSGNANO_BUILD_SGX_PROBE=ON
ninja -C build_sgx povprobe
```

できるもの: `build_sgx/sgx/povprobe.sgx`（約 30KB）。

## 実機へ置く

```sh
cd ~/Project/sgnano
python3 scripts/zm.py put --remote-dir C:/POVTEST \
  ~/Project/sgnano_povray/build_sgx/sgx/povprobe.sgx
```

**ドライブ文字は USB の再接続で変わります。** 実際に、USB ホストの
作り直しのあとで 38GB HDD の exFAT が B: から C: へ移りました。
`tools/hw_probe.py` は毎回 `DRIVES` を見て、exFAT（type 3）か NTFS
（type 4）の `POVTEST` を探し直します。

## 走らせる

```sh
RBRUN C:/POVTEST/povprobe.sgx	C:/POVTEST	fresh	120
```

引数:

| 引数 | 意味 |
|---|---|
| `DIR` | 書き込み先（例 `C:/POVTEST`） |
| `fresh` | チェックポイントを消して最初から |
| `resume` | 既存のチェックポイントから続ける |
| 行数 | その数だけ新しく描いたら止める（省略時は最後まで） |

1 回目は 120 行で止まり、`DIR/probe.ckp` を残します。2 回目は
`resume` で残りを描き、`probe_resume.png` と `probe_resume.raw` を
書きます。3 回目は `fresh` で最初から描き、`probe_fresh.png` と
`probe_fresh.raw` を書きます。

## 自動試験

```sh
python3 tools/hw_probe.py
```

このスクリプトは次を行います。

1. 38GB HDD の `POVTEST` を探す（無ければ作る）
2. `povprobe.sgx` を送る
3. 3 回のパスを走らせる（fresh 120 → resume → fresh）
4. PNG と生データを `build_hw/` へ取る
5. 次を確かめる
   - `fb_lend_spare` が 1,843,200 B を返す
   - PNG の RGB 作業場が借りたページに入る（連続している場合）
   - 再開の開始行が 120
   - FB の全 480 行が試験模様と一致（`fb_bad_rows=0`）
   - 再開版と最初からの版で FB CRC・PNG・生データが一致
   - PNG をデコードした画素が RGB555 の生データと完全一致（可逆）
   - USB ストレージの容量が 20,000MB 以上

## 進捗

描画中はテキスト面の 0〜4 行目に次を出します（1 秒ごと）。

```
POV-Ray 3.7  povprobe
line 120/480 (25.0%)  first missing 120
[##########------------------------------]  25.0%
elapsed 00:00:12  ETA 00:00:36  100.0 ms/line
checkpoint: probe.ckp  ok  png=-
```

UART には 16 行ごとに `progress 128/480 (26.6%) 1234 ms` を出します。
画面のステータスは `sgnano_app.c` の共通層が作るので、POV-Ray 本体でも
同じ表示になります。

## うまくいかないとき

- `RBRUN` が「already running」を返す: 前のプログラムの状態が残って
  います。`RSTOP` を送り、`RSTAT` が 0 になってからやり直してください。
- USB のコントロール転送が詰まる（`control pipe ... wedged`）: ファームが
  USB ホストを作り直します。ドライブ文字が変わることがあるので、
  `DRIVES` を確認してからやり直してください。
- 最初の `store_open` が遅い: 38GB HDD の exFAT で 2MB の作成に約 3.3 秒
  かかります（`storetest` の実測）。618KB のチェックポイントは約 1 秒です。

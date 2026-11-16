# 実機 SGX プローブ（povprobe）

POV-Ray 本体を載せる前に、実機で次の層を確かめるための小さなアプリです。
現在は ABI 5.11 で動きます。

- mode 2（テキスト＋640×480 RGB555）と 1 ページ FB
- `fb_lend_spare`（余り 3 ページ = 1,843,200 B）
- ブロックストア `store_*` による 1 ライン単位のチェックポイント
- 停止 → 再開 → 完成（再開後の絵が最初から描いた絵と一致すること）
- 可逆 PNG（miniz deflate level 9）と RGB555 生データの一致
- 20GB 以上の USB ドライブ（38GB の HDD）での読み書き
- 描画中の進捗表示（テキスト面のバー＋UART の `progress` 行）

## ビルド

SDK は `third_party/sgnano/sdk/sgx/` に同梱してあります（MIT、ABI 5.11）。
sgnano の別チェックアウトは要りません。

```sh
cmake -S . -B build_sgx -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake \
  -DSGNANO_BUILD_SGX_PROBE=ON
ninja -C build_sgx povprobe
```

できるもの: `build_sgx/sgx/povprobe.sgx`（約 38KB）。

## 実機へ置く

`scripts/zm.py` は sgnano リポジトリにあります。`SGNANO_ROOT` をその
チェックアウトに合わせてください。

```sh
cd "$SGNANO_ROOT"
python3 scripts/zm.py put --remote-dir B:/POVTEST \
  "$OLDPWD/build_sgx/sgx/povprobe.sgx"
```

**ドライブ文字は USB の再接続で変わります。** 実際に、USB ホストの
作り直しのあとで 38GB HDD の exFAT が B: から C: へ移りました。
`tools/hw_probe.py` は毎回 `DRIVES` を見て、exFAT（type 3）か NTFS
（type 4）の `POVTEST` を探し直します。

## 走らせる

```sh
RBRUN B:/POVTEST/povprobe.sgx	B:/POVTEST	fresh	120
```

引数:

| 引数 | 意味 |
|---|---|
| `DIR` | 書き込み先（例 `B:/POVTEST`） |
| `fresh` | チェックポイントを消して最初から |
| `resume` | 既存のチェックポイントから続ける |
| 行数 | その数だけ新しく描いたら止める（省略時は最後まで） |

1 回目は 120 行で止まり、`DIR/probe.ckp` を残します。2 回目は
`resume` で残りを描き、`probe_resume.png` と `probe_resume.raw` を
書きます。3 回目は `fresh` で最初から描き、`probe_fresh.png` と
`probe_fresh.raw` を書きます。

## 自動試験

```sh
SGNANO_ROOT=$HOME/Project/sgnano python3 tools/hw_probe.py
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
- ストアを開く前に mode 2 の FB を出さない: POV-Ray 側は描画中 mode 7
  preview、最後だけ mode 2 にしています。`povprobe` も同じ順序です。

## 記録（2026-11-06 の調査）

### `calloc` の無限再帰

以前の SDK の `sgx_libc.c` は

```c
void *calloc(size_t a, size_t b) {
    void *p = malloc(a * b);
    if (p) memset(p, 0, a * b);
    return p;
}
```

で、GCC の「`malloc`＋`memset(0)` → `calloc`」変換により `calloc` 自身を
末尾呼び出しするコードにコンパイルされることがありました。ABI 4.1 以降の
SDK は `memset` を関数ポインタ越しに呼ぶ形へ直っているため、いまは
`-fno-builtin` を付けていません。

### store_open と mode 2

mode 2 の FB を出したまま `store_open` すると止まる、という現象が
2026-11-06 にありました。対策として、ストアを開く間は mode 1（または
mode 7）にし、開いてから mode 2 へ入る順序にしています。チェックポイントの
ファイルは `write_file` で先に 64KB の倍数の大きさで作り、`store_open` の
CREATE（0 埋め）を通しません。

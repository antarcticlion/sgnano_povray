# SGX ABI 4.0（正式版）— sgnano から sgnano_povray への引き渡し

2026-11-06 / sgnano 側で決めた正式な ABI 4.0。素案 `SPEC_sgx4_overlay_store.md` を読んで決めたもの。

> **歴史的な文書（ABI 4.0、2026-11-06）。** 現在の ABI は **5.11**（2026-11-16）で、
> 正本は同梱 SDK の `third_party/sgnano/sdk/sgx/include/sgx_api.h`。この文書は
> ブロックストアと `fb_lend_spare` の設計理由を残すために置いてある。

> **sgnano_povray 側の取り込みメモ（2026-11-06 当時）**: この文書の型と関数の表に合わせ、
> `third_party/sgnano/sdk/sgx/include/sgx_api.h` に公式ヘッダのスナップショットを置いた。
> `platform/sgx/sgx4_stub.h` と `app/sgx/sgnano_app_sgx.c` は公式の型・シグネチャ
> （`sgx_store_config_t`、`SGX_STORE_*`、`SGX_EBUSY`、`set_mode` の戻り値など）に更新済み。
> **追記**: ABI 4.0 に `fb_lend_spare`（mode 2 で余りの 3 ページ = 約 1.8MB を借りる）
> が追加された。アプリは mode 2 のページ 0 を残して 3 ページを借り、PNG の RGB
> 作業場と将来の POV-Ray ヒープに使う。実機テストは sgnano 本体が空いてから行う。

- **現行の正本（型と関数の表）**: `third_party/sgnano/sdk/sgx/include/sgx_api.h`（MIT、ABI 5.11 のスナップショット。この文書が扱う 4.0 当時は sgnano の `sdk/sgx/include/sgx_api.h`）
- **説明**: sgnano リポジトリの `docs/sgx_abi.md` の §5.16（ブロックストア）・§3（読み込み）・§7（版）・§9（素案からの決め方）
- **組み方**: `SGNANO_SGX_SDK_DIR` に `third_party/sgnano/sdk/sgx` を置き、`include(${SGNANO_SGX_SDK_DIR}/sgx.cmake)` と `sgx_add_program(...)`
- **見本と試し**: sgnano リポジトリの `apps/abitest/storetest.c`（ブロックストアの使い方と誤りの返し方を全部）

---

## 1. 決まったこと（素案との違い）

| 素案 | 正式 4.0 |
|---|---|
| `.SGX4`（magic `"SGX4"`、モジュール表・ストア表） | **使わない。今までの `.sgx`（`"SGX1"`）**。見出しの `abi_major = 4` |
| オーバーレイ `overlay_*` | **4.0 には無い**（POV-Ray が約 1.4MB と測れたので常駐で足りる。要るときに 4.x で足す） |
| `store_open/close/get/release/prefetch/flush/stats/error` | **ある**（下の §2） |
| （素案に無い）FB の余りのページ | **`fb_lend_spare` を足した**（§2b）。モード 2 で 1 ページを表示し、残り 3 ページ（1.8MB）を PSRAM として借りる |
| `store_pin` / `store_unpin` | 無い（`store_get` = 取り出し、`store_release` = 返す、で同じこと） |
| 型の名前 `sgx4_store_config_t` / `sgx4_store_stats_t`、印 `SGX4_STORE_*` | **`sgx_store_config_t` / `sgx_store_stats_t`、`SGX_STORE_*`**（`sgx_api.h` の中） |
| `sgx4_store_config_t.lba`・`SGX4_STORE_RAW` | `lba` は無い（`reserved[3]`、0 にする）。`SGX_STORE_RAW`・`SGX_STORE_COMPRESS` は予約で `-SGX_ENOTSUP` |
| 誤り `SGX4_EBUSY = 64` ほか | **`SGX_EBUSY = 14`**（`SGX_E*` の続き）。媒体の誤り・抜けた・時間切れは `SGX_EIO` / `SGX_ENODEV` |
| ブロック 4KB〜256KB | 512 B〜256KB（2 のべき乗） |
| 作るときに中身を書かない | **0 で埋めて作る**（中身が決まる。USB メモリで 1MB に約 2 秒） |
| FB 1 ページの別の組み方 | しない。代わりに `fb_lend_spare`（§2b）。実機の空きは §4 |

**3.x との関係**: 4.x のファームは 3.x で組んだ `.sgx` もそのまま走らせる（表は 3.5 の末尾に足しただけ）。4.0 で組んだものは 3.x のファームでは読み込まない。
`api->size` を見て新しい欄があるか確かめる（例 `api->size >= offsetof(sgx_api_t, store_error) + sizeof(void *)`）。

**大きな像**: ファームは `.sgx` をファイルから置き場へ直に読むようになった（以前のファイラーは 1MB を越える `.sgx` を断り、読めても像の 2 倍の PSRAM を使った）。
上限は像と `.bss` の合計 `SGX_LOAD_MAX`（4MB）と PSRAM の空き。

---

## 2. ブロックストア

```c
typedef struct {
    const char *path;       // "A:/dir/name" か、プログラムのフォルダからの相対
    uint32_t block_size;    // 512〜256KB の 2 のべき乗
    uint32_t block_count;   // 0 = 既存のファイルの大きさから（CREATE とは組めない）
    uint32_t cache_blocks;  // 1〜256。PSRAM に block_size x cache_blocks を取る
    uint32_t flags;         // SGX_STORE_READONLY(1) / WRITEBACK(2) / CREATE(4)。RAW(8)・COMPRESS(0x10) は予約
    uint32_t reserved[3];   // 0
} sgx_store_config_t;

int   (*store_open)(const sgx_store_config_t *cfg);            // 番号 >= 0、失敗は -SGX_E*
int   (*store_close)(int id);                                   // 書き戻して返す。取り出し中があれば -SGX_EBUSY（閉じない）
void *(*store_get)(int id, uint32_t block);                     // 中身の番地（キャッシュを通る窓）。失敗は NULL -> store_error
int   (*store_release)(void *p, int dirty);                     // p はブロックの中のどこでもよい
int   (*store_prefetch)(int id, uint32_t block, uint32_t count);// 読めた数か -SGX_E*
int   (*store_flush)(int id);                                   // 戻ったら機器に書けている
int   (*store_stats)(int id, sgx_store_stats_t *out);
int   (*store_error)(int id);                                   // 最後の誤り（-SGX_E*、無ければ 0）
```

決まり（アプリが気をつけること）:

1. **置き場はファイル 1 つ**（USB ストレージ・フロッピーの FAT12/16/32・exFAT・NTFS）。S: は読むだけ（書くストアは `-SGX_EROFS`）、CD は不可
2. **書き込みはファイルの中のその場の書き換え**。大きさ・日時・ディレクトリは変わらない。開いている間、そのファイルを別に書き換えない・消さない
3. **`CREATE`**: 無ければ 0 で埋めて作る。**あれば中身はそのまま使う**。あっても `block_size x block_count` より小さければ `-SGX_EINVAL`（作り直さないので、作り直すなら先に `remove`）
4. **書き戻すのは**: 追い出し・`store_flush`・`store_close`・**プログラムがふつうに終わったとき**（入口が戻った・`exit`・ESC）。
   **止める頼みに 2 秒答えず `SIGKILL` で終わったときは捨てる**。チェックポイントの区切りでは `store_flush` を呼ぶこと
5. **取り出しは数える**: 同じブロックを 2 回 `store_get` すると同じ番地・2 回 `store_release` が要る。キャッシュが全部取り出し中なら NULL・`-SGX_EBUSY`
6. **読むだけのストアで `dirty` を付けると `-SGX_EROFS`**（取り出しは返る）
7. 媒体の誤りのあと、そのストアは `-SGX_EIO` を返し続ける。閉じて開き直す
8. どのタスクから呼んでもよい。待つ間は眠る。長い待ちのあいだも止める頼みは `stop_requested()` で見る
9. 同時に 8 個まで（`SGX_STORE_MAX`）

---

## 2b. FB の余りのページ（`fb_lend_spare`）

```c
unsigned (*fb_lend_spare)(int keep, void **pages, unsigned max, uint32_t *page_bytes);
```

- モード 2・3・4 で、`keep` のページを表示・書き込みのページにして、残りのページ（各 614,400 B、いまは 3 ページ）を貸す。番地はキャッシュを通る窓。借りた数を返す
- 借りている間: 借りたページへの `set_disp_page` / `set_write_page` は効かない、`fb_page(借りたページ)` は NULL、`set_mode` は断る（文字列を返す）
- `fb_return()` で返す（黒で消す）。プログラムが終われば返る
- POV-Ray なら: `set_mode(SGX_MODE_TEXT_FB)` -> `fb_lend_spare(0, pages, 3, &bytes)` -> 3 ページをヒープやストアのキャッシュに使い、`fb_page(0)` に描く

---

## 3. いまのスタブ（`platform/sgx/sgx4_stub.h`）からの置き換え

| スタブ | 正式 |
|---|---|
| `#include "include/sgx4_api.h"` | `#include "sgx_api.h"`（SDK） |
| `sgx4_store_config_t` / `SGX4_STORE_WRITEBACK` / `SGX4_STORE_CREATE` | `sgx_store_config_t` / `SGX_STORE_WRITEBACK` / `SGX_STORE_CREATE`（`lba` を消し `reserved` は 0） |
| `sgx4->store_open/get/release/flush/close` | `sgx->store_open/get/release/flush/close`（同じ形） |
| `set_mode(SGX4_STUB_MODE_TEXT_FB)` で int を返す | `const char *set_mode(int mode)`（NULL = できた）。モード 2 は `SGX_MODE_TEXT_FB` |
| `text_puts(int x, int y, const char *s, uint32_t attr)` が int | `unsigned text_puts(unsigned col, unsigned row, const char *s, uint32_t attr)` |
| `text_clear(void)` | `text_clear(uint32_t attr)` |
| `SGX4_T_*` | `SGX_T_*`（`sgx_api.h`） |
| `fb_page(int)`（`void *` を返す）・`ms()`・`stop_requested()`・`remove()`・`term_write()`・`sleep_us()` | 同じ名前（引数の型は `sgx_api.h` を見る） |

`app/sgx/sgnano_app_sgx.c` の今の使い方（4KB・キャッシュ 8・`WRITEBACK | CREATE`、`get` -> 書く -> `release(p, 1)`、`flush`、`close`）は、そのまま正式版で動く。
同じ名前のファイルを前の回から使うときは、`CREATE` を付けても中身は消えない（§2 の 3）。

---

## 4. 実装の状態（sgnano 側、2026-11-06）

- ファーム: `src/sgx/sgx_store.c`（ストア）・`src/sgx/sgx.c`（API の口・直に読む読み込み・`fb_lend_spare`）・`src/ruby/rbfs.c`（`rbfs_ra_write`）・`src/input/usbfat.c`（`usbfat_raw_write`）。実機に書き込み済み
- **実機の試し**（HDD の FAT32・exFAT・NTFS、`storetest.sgx`）: 3 つとも **47/47 PASS**（作る・書き戻し・読むだけで開き直す・2MB の乱数の読み書き 200 回・先読み・誤りの返し方 17 通り・`fb_lend_spare`）。
  プログラムの終わりの書き戻し（`keep` -> `check`）も PASS。3.x で組んだ `.sgx`（`ps`・`mem`）もそのまま走る
- **大きな像**: 1.5MB の `.sgx`（`bigtest.sgx`）がファイラーから走る（以前は 1MB で断っていた）
- **速さ**（FB なし）: 4KB の外れ 1 回 最長 約 32ms、64KB の外れ 1 回 最長 約 190ms。2MB を作る（0 で埋める）のに 2.3〜3.3 秒

### PSRAM の空き（実機、2026-11-06）

| 走らせ方 | プログラムが走っている間の空き |
|---|---|
| **ファイラーから**（1.5MB の像を置いたあと） | **約 2.0MB**（いちばん大きい塊 約 1.95MB）。像を合わせると、プログラムが使えるのは約 3.5MB |
| シェル sgsh の子として | 約 0.9MB（シェルが 2MB のヒープを持っている）。**POV-Ray はファイラーから走らせる** |
| モード 2 で `fb_lend_spare(0, …)` | **さらに +1.8MB**（600KB x 3） |

POV-Ray の見積もり（像 約 2MB・スタック 192KB・ヒープ・ストアのキャッシュ）は、ファイラーから走らせて `fb_lend_spare` を使えば約 5.3MB の中に収める形になる。
ストアのキャッシュは、借りたページとは別に `cache_blocks x block_size` を PSRAM から取る（`store_open` の中で取るので、借りたページは使わない）。
ほかに exFAT・NTFS のドライブを開いていると、そのキャッシュ（約 2.2MB）が PSRAM を使う。

### sgnano_povray の実機テストで見てほしいこと

1. `store_*` をスタブから正式版へ置き換えて、チェックポイント（4KB・キャッシュ 8・`WRITEBACK | CREATE`）が書けて、もう一度走らせたとき読めるか
2. POV-Ray の `.sgx` の大きさ（libc / libstdc++ を結んだあと）と、ファイラーから走らせたときに入るか（入らなければ大きさと、`mem_info` の空きを知らせてほしい）
3. `fb_lend_spare` で借りたページをヒープに使えるか
4. 足りない口・使いにくい口（例: 先読みを裏で進めたい、ブロックの大きさの上限、C++ の実行時の部品）

---

## 5. オーバーレイ（SDK の部品、2026-11-06 追記）

ABI 4.0 には入れていないが、**SDK の部品**として使える（ファームを変えずに、4.0 の `ra_read`・`psram_alloc` だけで動く）。
sgnano_povray の実機テストが済んだら、sgnano 側で ABI に組み込む予定。

- 部品: sgnano リポジトリの `sdk/sgx/include/sgx_overlay.h`・`sgx_overlay.c`・`sgx_ovl.ld`・`sgx_ovl_pack.py`、CMake は `sgx_add_overlay_module` / `sgx_add_overlay_pack`（`sgx.cmake`）
- 説明: sgnano リポジトリの `docs/sgx_abi.md` の §10。見本: `apps/ovltest`
- 形は素案の §4.3〜4.4 とほぼ同じ: モジュールは番地 0 に結び、`init(ctx, iface, size)` が関数の表を埋める。常駐の機能は `ctx->host`。替えたら前の番地はすべて無効（pin する）。
  素案との違い: スロットの数・大きさは常駐が `sgx_ovl_add_slot` で決める（パックには持たない）。名前で引ける（`sgx_ovl_find`）。`.init_array`・例外の表は持てない（C++ の静的な初期化はモジュールに置けない）
- 確かめ: QEMU（Cortex-M33）で約 1MB のモジュールを 128KB x 2 のスロットへ 300 回入れ替え、すべて一致（`scripts/test_overlay.py`）。**実機は未確認**

---

## 6. ABI 4.1（2026-11-06 追記）

- **オーバーレイが表に入った**（`ovl_open` / `ovl_add_slot` / `ovl_add_slot_mem` / `ovl_count` / `ovl_find` / `ovl_info` / `ovl_load` / `ovl_bind` /
  `ovl_unload` / `ovl_loaded` / `ovl_pin` / `ovl_unpin` / `ovl_gen` / `ovl_stats` / `ovl_close`）。中身は SDK の `sgx_overlay.c` と同じで、ファームがパックの
  ファイルとスロットの PSRAM を受け持つ。説明は sgnano の `docs/sgx_abi.md` §10。実機の `ovltest` で SDK の道・ABI の道とも一致
- `fb_lend_spare` は番地の小さい順に返す
- SDK の `calloc` の無限再帰を直した（`-fno-builtin` は要らない。「mode 2 で store_open が止まる」の正体）
- 4.0 で組んだ `.sgx` はそのまま動く

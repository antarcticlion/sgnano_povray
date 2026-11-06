# SGX ABI 4.0（正式版）— sgnano から sgnano_povray への引き渡し

2026-11-06 / sgnano 側で決めた正式な ABI 4.0。素案 `SPEC_sgx4_overlay_store.md` を読んで決めたもの。

> **sgnano_povray 側の取り込みメモ（2026-11-06）**: この文書の型と関数の表に合わせ、
> `third_party/sgnano/sgx_api.h` に公式ヘッダのスナップショットを置いた。
> `platform/sgx/sgx4_stub.h` と `app/sgx/sgnano_app_sgx.c` は公式の型・シグネチャ
> （`sgx_store_config_t`、`SGX_STORE_*`、`SGX_EBUSY`、`set_mode` の戻り値など）に更新済み。
> 実機テストは sgnano 本体が空いてから行う。

- **正本（型と関数の表）**: `~/Project/sgnano/sdk/sgx/include/sgx_api.h`（MIT）。このリポジトリに写しは置かない（写すと食い違うので、SDK をそのまま使う）
- **説明**: `~/Project/sgnano/docs/sgx_abi.md` の §5.16（ブロックストア）・§3（読み込み）・§7（版）・§9（素案からの決め方）
- **組み方**: sgnano_tracker と同じく `SGX_SDK_DIR` に `../sgnano/sdk/sgx` を置き、`include(${SGX_SDK_DIR}/sgx.cmake)` と `sgx_add_program(...)`
- **見本と試し**: `~/Project/sgnano/apps/abitest/storetest.c`（ブロックストアの使い方と誤りの返し方を全部）

---

## 1. 決まったこと（素案との違い）

| 素案 | 正式 4.0 |
|---|---|
| `.SGX4`（magic `"SGX4"`、モジュール表・ストア表） | **使わない。今までの `.sgx`（`"SGX1"`）**。見出しの `abi_major = 4` |
| オーバーレイ `overlay_*` | **4.0 には無い**（POV-Ray が約 1.4MB と測れたので常駐で足りる。要るときに 4.x で足す） |
| `store_open/close/get/release/prefetch/flush/stats/error` | **ある**（下の §2） |
| `store_pin` / `store_unpin` | 無い（`store_get` = 取り出し、`store_release` = 返す、で同じこと） |
| 型の名前 `sgx4_store_config_t` / `sgx4_store_stats_t`、印 `SGX4_STORE_*` | **`sgx_store_config_t` / `sgx_store_stats_t`、`SGX_STORE_*`**（`sgx_api.h` の中） |
| `sgx4_store_config_t.lba`・`SGX4_STORE_RAW` | `lba` は無い（`reserved[3]`、0 にする）。`SGX_STORE_RAW`・`SGX_STORE_COMPRESS` は予約で `-SGX_ENOTSUP` |
| 誤り `SGX4_EBUSY = 64` ほか | **`SGX_EBUSY = 14`**（`SGX_E*` の続き）。媒体の誤り・抜けた・時間切れは `SGX_EIO` / `SGX_ENODEV` |
| ブロック 4KB〜256KB | 512 B〜256KB（2 のべき乗） |
| 作るときに中身を書かない | **0 で埋めて作る**（中身が決まる。USB メモリで 1MB に約 2 秒） |
| FB 1 ページの別の組み方 | しない（いまの FB 4 ページで、実機の空きを測る。§4） |

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

## 4. 実装の状態（sgnano 側）

- ファーム: `src/sgx/sgx_store.c`（ストア）・`src/sgx/sgx.c`（API の口・直に読む読み込み）・`src/ruby/rbfs.c`（`rbfs_ra_write`）・`src/input/usbfat.c`（`usbfat_raw_write`）
- 実機の試し・PSRAM の空きの実測は、この下に追記する

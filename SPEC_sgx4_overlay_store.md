# SGX 4.0 改修案 — オーバーレイ＋ブロックストア（POV-Ray 3.7 用）

2026-11-06 / 対象: sgnano の SGX（外部実行ファイル）ABI 3.5 と SDK、POV-Ray 3.7 の移植

この文書は「POV-Ray 3.7 を sgnano の SGX アプリとして動かすために、プログラム領域とヒープをストレージへ逃がす」ための ABI と SDK の改修案です。実装前の設計案であり、数値は目標値です。

---

## 0. 結論

1. **RP2350 の Cortex-M33 には MMU がありません。** MPU はありますが、仮想アドレスを物理アドレスへ変換する機能はありません。したがって、一般的な OS の「透過的なページング／スワップ」は作れません。
2. MPU のフォールトを使って固定窓のページを入れ替えることはできますが、**窓の大きさを超えるアドレス空間は作れません**。ページのアドレスは固定で、その PSRAM はそのページのために専有されます。8MB を超えるヒープを生ポインタのまま作ることは不可能です。
3. したがって、ABI は次の2つを柱にします。
   - **プログラム**: 常駐コア＋**固定スロットのオーバーレイ**。モジュールをストレージからスロットへコピーし、関数表（インターフェース）経由で呼ぶ。
   - **ヒープ**: **ハンドル付きブロックストア**。大きなデータ（メッシュ、テクスチャ、シーン DB）を固定長ブロックとしてストレージに置き、PSRAM のキャッシュスロットへ読み込んで使う。生ポインタではなく、ブロック番号で参照する。
4. この方式は **MPU の追加使用を必要としません**。既存の QMI ゲート（表示の取得中に core1 を止める MPU）と競合しません。
5. POV-Ray 3.7 の目標配置（1画面 FB）:
   - 常駐コア: 1.5〜3.0MB（形状・テクスチャを常駐させるなら 2.5〜3.0MB）
   - オーバーレイ窓: 0.5〜1.0MB（4スロット程度）
   - ストアキャッシュ: 2.0MB（64KB × 32）
   - ヒープ・スタック: 0.5〜1.0MB
   - FB: 0.6MB（mode 2、1ページ）
   - 合計: **5.1〜7.6MB** → PSRAM 8MB に収まる（4ページ FB のままなら 6.9〜9.4MB で厳しい）
   - モジュール本体とスワップファイルは USB ストレージ（または S: の空き）に置く
6. **最初に POV-Ray 3.7 の ARM 向け実サイズを測る必要があります。** 常駐コアが 2.5MB を超えるなら、機能削減かモジュール分割を増やします。

---

## 1. なぜ「透過ページング」ではないのか

### 1.1 MMU がない

Cortex-M33 の MPU は、領域ごとのアクセス許可（読み書き実行）を設定するだけで、アドレス変換はしません。

- ページをストレージへ退避しても、そのページのアドレスは PSRAM 上に残ります。
- 退避中にそのアドレスへアクセスすると、MPU で止めることはできます。
- しかし、そのアドレスを別のページに貸すことはできません。貸すと、元のページを戻したときに同じアドレスに別のデータが入っていることになります。
- したがって、**「8MB の PSRAM に 16MB のヒープを載せる」ことはできません**。できるのは「8MB のうち、いま使うページだけを PSRAM に置き、残りはストレージに置く」ことですが、PSRAM のアドレス空間は 8MB のままです。

### 1.2 コードも同じ

4MB のコードを 1MB の窓で実行するには、関数のアドレスを窓の中へ移す必要があります。生の関数ポインタをそのまま使うと、別のモジュールを窓へ読み込んだ時点でポインタが別の関数を指します。

したがって、コードは **オーバーレイ**（固定スロットへモジュールを入れ替える）にします。モジュール間の呼び出しは、常駐の関数表（インターフェース）経由にします。

### 1.3 採用する形

| 方式 | 8MB 超え | 生ポインタ | 追加 MPU | 複雑さ | 判定 |
|---|---:|---:|---:|---:|---|
| MPU 透過ページング（固定窓） | 不可 | 可（窓内） | 必要 | 高 | 窓内の退避だけ。今回の主目的には不足 |
| オーバーレイ（コード） | 可 | 関数表経由 | 不要 | 中 | **採用** |
| ブロックストア（ヒープ） | 可 | ブロック番号 | 不要 | 中 | **採用** |
| コード XIP（フラッシュ直実行） | 可 | 可 | 不要 | 低 | QMI 競合と速度で不利。補助候補 |
| 圧縮ストア | 可 | ブロック番号 | 不要 | 中 | 第2段階で追加 |

### 1.4 透過ページングを使うとしたら（採用しない）

MPU のフォールトで固定窓のページを入れ替えることは技術的には可能です。例えば 2MB のヒープ窓を 64KB ページに分け、7 スロットだけを MPU で見せる、といった形です。しかし:

- 窓（2MB）は PSRAM 上に存在し続けるので、ヒープの最大値は 2MB のままです。ストレージを足しても 2MB を超えません。
- MemManage ハンドラ、ページャタスク、LRU、書き戻し、QMI ゲートとの排他が必要になり、複雑さが大幅に増えます。
- ページフォールトはストレージ I/O を直接行えないため、タスクを止めてページャに渡す仕組みが必要です。
- 既存の QMI ゲート MPU と設定を共有する必要があります（RP2350 の MPU は 8 領域）。

今回の目的は「PSRAM に収まらない量をストレージに置く」ことなので、固定窓の透過ページングでは足りません。**オーバーレイ＋ブロックストア**を採用します。

---

## 2. 全体像

```
            ストレージ（USB / S:）
  +--------------------------------------+
  | POVRAY.SGX4                          |
  |   ヘッダ・常駐イメージ・モジュール表  |
  |   モジュール画像（parser, shapes...） |
  +--------------------------------------+
  | POVSWAP.BIN（任意の大きさ）           |
  |   64KB ブロックの配列                 |
  +--------------------------------------+
                    |
                    | ra_read / raw block I/O
                    v
  PSRAM 8MB
  +--------------------------------------+
  | 常駐コア（コード・rodata・data・bss）  |
  +--------------------------------------+
  | オーバーレイ窓（4スロット × 256KB）    |
  |   slot0: parser                      |
  |   slot1: shapes                      |
  |   slot2: texture                     |
  |   slot3: rare / media                |
  +--------------------------------------+
  | ストアキャッシュ（32 × 64KB）          |
  +--------------------------------------+
  | ヒープ・スタック                       |
  +--------------------------------------+
  | FB 1ページ（mode 2、640×480×2）        |
  +--------------------------------------+
```

- 常駐コアは起動時に PSRAM へ読み、再配置する（現在の SGX と同じ）。
- モジュールは必要なときだけ `.SGX4` からスロットへ読む。モジュールはスロット先頭を基準に再配置する。
- ストアは必要なブロックだけをキャッシュへ読む。書き込みがあるストアは、追い出すときにストレージへ書き戻す。
- POV-Ray のレンダリングは 1 行ずつ RGB555 に変換して `fb_page(0)` へ書く。画像全体のバッファ（20B/画素）は持たない。

---

## 3. メモリ配置の例

PSRAM 8MB = 0x11000000〜0x117FFFFF（キャッシュ窓）／0x15000000〜0x157FFFFF（非キャッシュ窓）。

| 用途 | 大きさの目安 | 取り方 |
|---|---:|---|
| 常駐コア（コード・定数・data・bss） | 1.5〜3.0MB | `pmem_alloc(..., VMEM_PLANE, "sgx4 resident")` |
| オーバーレイ窓 | 0.5〜1.0MB | `pmem_alloc(..., 32, VMEM_PLANE, "sgx4 overlay")` |
| ストアキャッシュ | 2.0MB | `pmem_alloc(..., 32, VMEM_PLANE, "sgx4 store cache")` |
| ヒープ・スタック | 0.5〜1.0MB | 通常の SGX ヒープ／`pmem_alloc` |
| FB（mode 2、1ページ） | 614,400 B | 起動時の固定（既存の `FB_PAGES`） |
| 予備（通常アプリ・USB の受け場） | 0.8〜3.3MB | アリーナの残り（1ページ FB のとき） |

- 現状のファームは FB 4ページ（2,457,600 B）を固定で取ります。POV-Ray 用のビルドでは **`FB_PAGES=1`** にするのが望ましいです。4ページのままだと、上の表の「予備」が 0.5MB 程度まで減り、ストアキャッシュを 2MB 取れない可能性があります。
- オーバーレイ窓とストアキャッシュは、アプリの起動時にアリーナから取ります。足りなければ `SGX_ENOMEM` で起動を断ります（黙って小さくしない）。

---

## 4. ABI 4.0

### 4.1 版と互換性

- ABI 4.0 を追加する。3.x の `.sgx` は今までどおり動く。
- ファイルの magic で見分ける。
  - 3.x: `"SGX1"`（`0x31584753`）
  - 4.x: `"SGX4"`（`0x34584753`）
- ファームは両方のローダを持つ。4.x のアプリは 4.x 対応ファームでのみ動く。
- 拡張子は `.sgx` のままでもよい（ファイラは magic で判別する）。`.sgx4` は開発時に 3.x と区別するための案。
- `sgx_api_t` は **末尾に足すだけ**。4.x のアプリは `api->size` を見て新しい関数を使う。
- `SGX_LOAD_MAX`（4MB）は **常駐イメージ** の上限として残す。モジュール画像とストアファイルはこの上限の外。

### 4.2 ファイル形式（`.SGX4`）

```
+---------------------------+
| sgx4_header_t             |  0
+---------------------------+
| 常駐イメージ               |  resident_image_offset
| （コード・rodata・data）    |
+---------------------------+
| 常駐の再配置表             |  resident_reloc_offset
+---------------------------+
| モジュール表               |  module_table_offset
+---------------------------+
| モジュール画像 0..n-1      |  各モジュールの image_offset
| （スロット先頭基準）        |
+---------------------------+
| 各モジュールの再配置表      |
+---------------------------+
| ストア表                   |  store_table_offset
+---------------------------+
| 文字列表                   |  string_table_offset
+---------------------------+
```

```c
#define SGX4_MAGIC        0x34584753u   // "SGX4"
#define SGX4_ABI_MAJOR    4
#define SGX4_ABI_MINOR    0
#define SGX4_LOAD_MAX     (4u * 1024u * 1024u)   // 常駐イメージ＋bss の上限

// 印
#define SGX4_F_OVERLAY    0x0001u   // オーバーレイを使う
#define SGX4_F_STORE      0x0002u   // ブロックストアを使う
#define SGX4_F_FIXED_BASE 0x0004u   // 常駐イメージを固定番地に置く（任意）
#define SGX4_F_NO_TEXT    0x0008u   // テキスト面を閉じる
#define SGX4_F_WAIT_KEY   0x0010u   // 終わってもキーを待つ

typedef struct {
    uint32_t magic;                 // SGX4_MAGIC
    uint16_t abi_major, abi_minor;  // 4, 0
    uint32_t header_size;
    uint32_t flags;                 // SGX4_F_*
    uint32_t entry;                 // 常駐イメージ内の入口（Thumb、奇数）
    uint32_t stack_size;            // 0 なら 16KB

    uint32_t resident_image_offset;
    uint32_t resident_image_size;
    uint32_t resident_bss_size;
    uint32_t resident_reloc_offset;
    uint32_t resident_reloc_count;

    uint32_t module_count;
    uint32_t module_table_offset;

    uint32_t slot_count;            // オーバーレイのスロット数
    uint32_t slot_size;             // 固定スロットの大きさ（可変にするなら slot_table_offset）
    uint32_t slot_table_offset;     // 0 なら固定、非0 なら可変スロット表

    uint32_t store_count;
    uint32_t store_table_offset;
    uint32_t string_table_offset;

    char     name[32];
    uint32_t reserved[4];
} sgx4_header_t;

typedef struct {
    uint32_t id;                    // モジュール番号（アプリ内で一意）
    uint32_t slot;                  // 入れ替えるスロット番号
    uint32_t image_offset;          // ファイル内の位置
    uint32_t image_size;
    uint32_t bss_size;
    uint32_t entry_offset;          // モジュールの init 関数（スロット先頭から）
    uint32_t reloc_offset;          // モジュールの再配置表
    uint32_t reloc_count;
    uint32_t flags;                 // SGX4_MOD_*
    uint32_t name_offset;           // 文字列表
} sgx4_module_t;

#define SGX4_MOD_PIN       0x0001u  // 起動時に読み込んで固定（任意）
#define SGX4_MOD_COMPRESS  0x0002u  // 画像は圧縮（第2段階）
#define SGX4_MOD_NO_RELOC  0x0004u  // スロット先頭に固定でリンク済み

typedef struct {
    uint32_t id;                    // ストア番号
    uint32_t block_size;            // 4KB〜256KB、2のべき乗
    uint32_t block_count;
    uint32_t cache_blocks;          // 要求するキャッシュ数
    uint32_t flags;                 // SGX_STORE_*
    uint32_t name_offset;           // 既定のパス（文字列表）。0 ならアプリが開く
} sgx4_store_t;

#define SGX_STORE_READONLY  0x0001u
#define SGX_STORE_WRITEBACK 0x0002u
#define SGX_STORE_CREATE    0x0004u
#define SGX_STORE_RAW       0x0008u  // ファイルではなく生の LBA 範囲
#define SGX_STORE_COMPRESS  0x0010u  // 第2段階
```

### 4.3 モジュールのリンクと再配置

- 各モジュールは **番地 0 にリンク**し、ロード時にスロット先頭との差を再配置表の語へ足す。現在の `sgx_link.py` と同じ方式。
- モジュールの `.text`・`.rodata`・`.data` は像に含め、`.bss` はロード時に 0 にする。
- モジュール内の絶対番地（定数表、関数ポインタ、vtable）は再配置表に入れる。
- **モジュールから別モジュールの関数を直接呼ばない**。呼び出しは常駐のインターフェース表経由にする。パッケージャが再配置表を検査し、モジュール間の直接参照があればエラーにする。
- `MOVW` / `MOVT` の絶対番地は今までどおり禁止（`sgx_link.py` と同じ）。

### 4.4 常駐コアとモジュールのインターフェース

モジュールは「関数表を埋める init 関数」を1つだけ export します。型は SDK のヘッダで決めます。

```c
typedef struct {
    uint32_t abi_major, abi_minor;
    uint32_t module_id;
    uint32_t slot;
    const sgx_api_t *api;        // ファームの API 表
    uint8_t *base;              // スロットの先頭
    uint32_t size;              // スロットの大きさ
} sgx4_module_ctx_t;

typedef int (*sgx4_module_init_t)(const sgx4_module_ctx_t *ctx, void *iface, uint32_t iface_size);
```

常駐コアは次のように呼びます。

```c
int load_and_bind(int slot, int module, void *iface, uint32_t iface_size) {
    const int e = api->overlay_load(slot, module);
    if (e) return e;
    sgx4_module_init_t init = (sgx4_module_init_t)api->overlay_entry(slot, module);
    if (!init) return -SGX_EINVAL;
    sgx4_module_ctx_t ctx = { 4, 0, module, slot, api, api->overlay_base(slot), api->overlay_slot_size(slot) };
    return init(&ctx, iface, iface_size);
}
```

- `iface` は常駐コア側の構造体（関数ポインタの並び）。モジュールが自分の関数の番地を入れる。
- 呼び出し側は `iface->sphere_intersect(...)` のように呼ぶ。間接呼び出しのコストは 1 回の `blx` 程度。
- モジュールを差し替える前に、そのインターフェースを使っている呼び出しが終わっていることをアプリが保証する。SDK は「スロットを pin している間は差し替え禁止」を守る。

例えば、パーサと形状のインターフェースは次のように定義します（型は実際の POV-Ray の構造体に合わせる）。

```c
typedef struct {
    int (*parse_file)(const char *path, struct pov_scene *out);
    void (*scene_free)(struct pov_scene *scene);
} pov_parser_iface_t;

typedef struct {
    int (*intersect)(const struct pov_object *obj, const struct pov_ray *ray, struct pov_hit *hit);
    int (*inside)(const struct pov_object *obj, const struct pov_vector *p);
    void (*bbox)(const struct pov_object *obj, struct pov_bbox *out);
} pov_shape_iface_t;
```

常駐コアは `pov_parser_iface_t parser;` と `pov_shape_iface_t shapes;` を持ち、`load_and_bind(SLOT_PARSER, MOD_PARSER, &parser, sizeof parser)` のように初期化します。

**C++ の vtable に注意。** モジュール内のクラスの仮想関数を指す vtable は、モジュールを差し替えると無効になります。モジュールが作ったオブジェクトが生きている間は、そのモジュールを pin しなければなりません。POV-Ray では:

- 形状モジュールは、その形状のオブジェクトがシーンに存在する間ずっと pin する。レンダー中は常駐に近い。
- パーサは、パースが終わったらアンロードできる（作ったオブジェクトの vtable がパーサを指さない場合）。
- ファイル形式・ポストプロセス・稀な機能は、オブジェクトを残さないのでオーバーレイに向く。
- どうしても形状を差し替えたい場合は、vtable を持たない「型＋データ」の表現に変え、交差判定をインターフェース表経由にする。

このため、オーバーレイ窓は「パーサ＋ファイル形式＋ポストプロセス」に使い、形状とテクスチャは常駐コアに置くのが安全です。その場合、常駐コアは 2.5〜3MB 程度まで大きくなりますが、1ページ FB なら 8MB に収まります。

### 4.5 API の追加（`sgx_api_t` の末尾）

```c
// ---- オーバーレイ（ABI 4.0）----
int      (*overlay_slots)(void);
uint32_t (*overlay_slot_size)(int slot);
uint8_t *(*overlay_base)(int slot);
int      (*overlay_load)(int slot, int module);       // 0 = 成功
int      (*overlay_unload)(int slot);
int      (*overlay_loaded)(int slot);                 // いまのモジュール番号、無ければ -1
void    *(*overlay_entry)(int slot, int module);      // init 関数の番地（ロード済みであること）
int      (*overlay_pin)(int slot);                    // 差し替え禁止
int      (*overlay_unpin)(int slot);
int      (*overlay_stats)(sgx4_overlay_stats_t *out);

// ---- ブロックストア（ABI 4.0）----
int      (*store_open)(const sgx4_store_config_t *cfg);   // id >= 0、失敗は -SGX_E*
int      (*store_close)(int id);
void    *(*store_get)(int id, uint32_t block);            // キャッシュの番地。失敗は NULL
int      (*store_release)(void *p, int dirty);            // 0 = 成功
int      (*store_pin)(void *p);                           // 追い出し禁止
int      (*store_unpin)(void *p);
int      (*store_prefetch)(int id, uint32_t block, uint32_t count);
int      (*store_flush)(int id);                           // 汚れたブロックを書き戻す
int      (*store_stats)(int id, sgx4_store_stats_t *out);
int      (*store_error)(int id);                           // 最後の誤り（-SGX_E*）
```

```c
typedef struct {
    const char *path;       // ファイルのパス。NULL なら raw
    uint32_t block_size;    // 4KB〜256KB、2のべき乗
    uint32_t block_count;
    uint32_t cache_blocks;  // このストアに割り当てるキャッシュ数
    uint32_t flags;         // SGX_STORE_*
    uint32_t lba;           // raw のときの開始 LBA
    uint32_t reserved;
} sgx4_store_config_t;

typedef struct {
    uint32_t block_size, block_count, cache_blocks;
    uint32_t resident_blocks, pinned_blocks;
    uint32_t hits, misses, evictions, writebacks, errors;
    uint32_t bytes_read, bytes_written;
    uint32_t max_wait_us;
} sgx4_store_stats_t;

typedef struct {
    uint32_t slots, slot_size;
    uint32_t loads, unloads, bytes_read;
    uint32_t current[8];    // スロットごとのモジュール番号（-1 = 空）
} sgx4_overlay_stats_t;
```

### 4.6 誤り

既存の `SGX_E*` に次を足す。

```c
SGX_EBUSY,      // スロットが pin 中／ストアが使用中
SGX_EBUS,       // ストレージが抜けた・読めない
SGX_ECORRUPT,   // ブロックの検査に失敗（第2段階の圧縮）
SGX_ETIMEDOUT,  // ストレージの時間切れ
```

ストレージの失敗は **戻り値** で返す。ページフォールトのように非同期で起きるものではないので、シグナルは増やさない。アプリは `SGX_EBUS` を受けたら描画を中止し、画面に「ストレージが読めません」と出して終了する。

---

## 5. SDK の改修

### 5.1 ビルド

```cmake
sgx_add_paged_program(povray
    NAME "POV-Ray"
    FLAGS 0
    STACK (192 * 1024)
    MANIFEST povray.modules       # 常駐／モジュール／スロットの割り当て
    STORES   povray.stores        # ストアの既定（ブロックサイズ、数、キャッシュ数）
)
```

`povray.modules` の例:

```
resident  sources.txt
module 0  parser   slot 0  size 1048576  sources_parser.txt
module 1  rare     slot 1  size 524288   sources_rare.txt
module 2  formats  slot 2  size 262144   sources_formats.txt
```

- `sgx_add_program` は 3.x のまま残す。
- `sgx_add_paged_program` は新しい `sgx4.ld` と `sgx4_pack.py` を使う。
- モジュールの割り当てはマニフェスト（`povray.modules`）で決める。オブジェクトファイル単位でも、`-ffunction-sections` の関数単位でもよい。

### 5.2 リンカ

- 常駐: `.text` / `.rodata` / `.data` / `.bss` を従来どおり。
- モジュール: `module_<id>` セクションを **スロット先頭 0 基準** でリンクする。複数のモジュールを同時にリンクするときは、モジュールごとに別のリンカ実行にするか、`--section-start` で 0 に置く。
- モジュールの `.bss` は像に含めず、サイズだけ記録する。
- `sgx4_pack.py` がモジュール表・再配置表・文字列表を書き出す。

### 5.3 パッケージャ `sgx4_pack.py`

入力:

- 常駐 ELF
- モジュールごとの ELF（または1つの ELF のセクション）
- マニフェスト（モジュール ID、スロット、大きさ、フラグ）
- ストア表（ブロックサイズ、ブロック数、キャッシュ数、パス）

出力:

- `.SGX4`（ヘッダ＋常駐＋モジュール＋表）
- `povray_modules.h`（インターフェース構造体、モジュール ID、スロット番号）
- `povray_modules.c`（`load_and_bind` の薄い包み）

検査:

- モジュールがスロットに収まる。
- モジュール間の直接参照がない。
- `MOVW`/`MOVT` の絶対番地がない。
- 常駐イメージが `SGX4_LOAD_MAX` 以下。
- スロットの合計が `slot_count * slot_size` 以下。
- `entry_offset` が Thumb（奇数）。

### 5.4 クライアントランタイム

- `sdk/sgx/sgx4_overlay.c`: `overlay_load` などの薄い包み。実際の読み込みはファームの API。
- `sdk/sgx/sgx4_store.c`: `store_get`/`release` の薄い包み。アプリが誤って release し忘れたときに備えて、デバッグビルドでは番地の記録を取る。
- `sdk/sgx/sgx4_module.c`: `load_and_bind`、スロットの pin/unpin、インターフェース表の初期化。
- `sdk/sgx/include/sgx4_api.h`: ABI 4.0 の型と関数。
- 既存の `sgx_libc.c` の `malloc` はそのまま（通常の PSRAM ヒープ）。ストアは別の確保。

### 5.5 ホスト試験

- `sgx4_pack.py` の生成物をホストで読み、モジュールの再配置と `entry_offset` を検証する。
- モジュールを擬似スロットへコピーし、`init` を呼んでインターフェースの番地がスロット内を指すことを確認する。
- ストアの LRU・pin・dirty・書き戻しを、RAM ディスクの模擬で検証する。
- QEMU では MPU を使わないので、 overlay/store のロジックをそのまま試せる。

---

## 6. ファームウェアの改修

### 6.1 ローダ `src/sgx/sgx4.c`

- `sgx_is_image` を拡張し、`"SGX4"` も受け付ける。
- ヘッダを読み、ABI 4.0 を確認。
- 常駐イメージを `pmem_alloc(resident_image_size + resident_bss_size)` で取り、コピー・再配置・bss 0 化。
- オーバーレイ窓を `pmem_alloc(slot_count * slot_size, 32, ...)` で取る。
- ストア表を読み、必要ならキャッシュ窓を取る（アプリが `store_open` する場合はそこで取る）。
- `.SGX4` ファイルを `rbfs_ra_open` で開き、`sgx_prog_t` にハンドルを保持する。**ファイル全体を PSRAM に読まない**（現在のローダの二重取りを避ける）。
- 常駐イメージの再配置は現在の `load_image` と同じ。モジュールの再配置は `overlay_load` で行う。
- 終了時にファイルを閉じ、窓とストアを返す。

### 6.2 オーバーレイマネージャ `src/sgx/overlay.c`

- スロットごとに「いまのモジュール ID」「pin 数」「最終使用時刻」を持つ。
- `overlay_load(slot, module)`:
  1. 引数を検査。
  2. 同じモジュールが入っていれば 0 を返す。
  3. pin 中なら `-SGX_EBUSY`。
  4. `.SGX4` の `module.image_offset` から `image_size` をスロットへ読む（`rbfs_ra_read`）。
  5. モジュールの再配置表を適用する（`slot_base - 0` を足す）。
  6. `.bss` を 0 にする。
  7. `xip_cache_clean_range` / `invalidate_range` でスロットのキャッシュを捨てる。
  8. モジュール ID を記録。
- `overlay_entry` は `slot_base + entry_offset` を返す。
- **番地の窓**: アリーナからは非キャッシュ窓（0x15...）で取り、`cached()` でキャッシュ窓（0x11...）に直した番地を `slot_base` にする。モジュールのコピーは非キャッシュ窓へ行い、実行前に `xip_cache_clean_range` / `invalidate_range` でキャッシュを捨てる。現在の `load_image` と同じ扱い。
- 読み込みはアプリのタスクで行う。待つ間は眠る。`stop_requested()` を区切りで見る。
- 読み込み中に QMI ゲートが閉じたら、既存の MPU の待たせで止まる（PSRAM への書き込みは `render_park_on` の範囲）。新しい MPU 設定は不要。

### 6.3 ストア `src/sgx/store.c`

- ストアごとに:
  - ブロックサイズ、ブロック数、キャッシュ数
  - キャッシュのハッシュ表（ブロック番号 → スロット）
  - LRU リスト、pin 数、dirty フラグ
  - バッキング（ファイルのエクステント列、または raw LBA）
  - 統計
- `store_get`:
  1. ハッシュを引く。あれば pin して返す。
  2. 空きスロットを探す。無ければ pin=0 の LRU を選ぶ。dirty なら書き戻す。
  3. バッキングからブロックを読む。失敗したら `SGX_EBUS` を記録し NULL。
  4. ハッシュに登録し、pin して返す。
- `store_release`: pin を下げ、dirty を立てる。0 になってもキャッシュには残す（LRU の先頭へ）。
- `store_flush`: すべての dirty を書き戻す。
- `store_close`: flush して、キャッシュ窓を返す。
- `store_prefetch`: 指定ブロックから count 個を、空きスロットへ先読みする。最初は同期。必要なら低優先度の先読みタスクを追加する。
- ブロックの読み書きは 64KB 単位。USB の受け場は既存の 16KB を使い、`rbfs_ra_read` の経路を再利用する。書き込みは新しい raw write を使う。
- **キャッシュの番地**: スロットはアリーナから非キャッシュ窓（0x15...）で取り、アプリにはキャッシュ窓（0x11...）の番地を返す。読み込み後は `xip_cache_invalidate_range`、書き戻し前は `xip_cache_clean_range` でキャッシュを整える。現在の `a_psram_alloc` と同じ扱い。
- 書き戻しの途中で USB が抜けたら、そのストアを `SGX_EBUS` にし、以降の `store_get` は NULL を返す。アプリは描画を中止する。

### 6.4 スワップバックエンド `src/sgx/swap.c`

- **ファイル方式**:
  - `swap_open(path, block_size, block_count, create)`。
  - 既存ファイルならサイズを検査し、`usbfat_extents` / `fs_extents` でエクステント列を得る。
  - 新規作成は `usbfat_allocate`（新設）でクラスタを確保する。データの 0 書きはしない（初回読み出しは未定義でも、アプリが書くまで読まない）。FAT の鎖だけ作る。**未初期化ブロックを読まないのはアプリの責任**。必要ならストアの先頭にビットマップを置く。
  - ランダム読み書きは `usbfat_raw_read` / **`usbfat_raw_write`（新設）** で、エクステントをまたぐ場合は分割する。
  - exFAT / NTFS も `fs_extents` と raw write で対応する。NTFS は書き込みの制限があるので、スワップは FAT / exFAT を推奨。
- **raw 方式**:
  - `swap_open_raw(drive, lba, block_count)`。パーティション全体をスワップに使う。FAT を経由しないので速く、断片化もない。ユーザーが USB ドライブにスワップ用の区画を作る。
- **S:（内蔵フラッシュ）**:
  - 読み出し専用のモジュール置き場には使える。書き込みスワップには使わない（消去単位 4KB、書き換え回数、表示の取得との競合）。
- **速度の目安**:
  - USB フルスピード: 0.5〜1.5MB/s。64KB ブロックで 40〜130ms。
  - キャッシュヒットが重要。architectural シーンでは、BVH/octree の局所性と先読みでヒット率を上げる。
  - 圧縮（第2段階）で I/O を 1/2〜1/4 に減らす。

### 6.5 既存の USB / ファイル層への追加

- `usbfat_raw_write(lba, n, src, done)` を追加する。`usbfat_raw_read` と対称。
- `fs_raw_write` を exFAT / NTFS に追加する（読み出しは既存の `usbfat_raw_read` を使う）。
- `usbfat_allocate(path, size)` / `fs_allocate` を追加する。FAT の鎖とディレクトリ項目だけ作り、データは書かない。
- `rbfs_ra_open_rw` を追加する。読み出し専用の `rbfs_ra_*` はそのまま。
- すべて `usb_lock` の中で行う。呼び出しはアプリのタスクから。割り込みからは呼ばない。
- `rbfs_ra_open` は 1 ハンドルにつき 16KB の SRAM を受け場に取る。SGX4 のパッケージ用に 1 つ、シーン用に 1 つで最大 32KB。SRAM が足りないときは、パッケージを S: のフラッシュ直読みにして受け場を共有する。

### 6.6 ファイラ・実行の段取り（重要）

現在の `rbrun.c` は、`.sgx` を選ぶと **ファイル全体を `R.src`（PSRAM）へ読み**、そのあと `sgx_session_start(R.src, R.src_len, ...)` を呼びます。3.x の小さなアプリはこれでよいですが、SGX4 ではファイル全体（3〜4MB）を読むと、まさに避けたかった二重取りになります。

- ファイラ／`rbrun.c` に **`is_sgx4` の道**を足す。
  - ファイルの先頭 4 バイトだけを `ra_*` で読み、magic が `"SGX4"` なら SGX4 の道へ。
  - `R.src` は確保しない。`sgx4_session_start(path, dir, argc, argv, ...)` を呼ぶ。
  - ローダが `.SGX4` を `rbfs_ra_open` で開き、ヘッダと常駐イメージだけを読む。ファイルは実行中開いたままにする。
- 3.x の `.sgx` は今までどおり `fload` で `R.src` に読む。
- ファイラの一覧・Enter の判定に `.sgx4` を足す（拡張子ではなく magic で判別してもよい）。
- `R.src` を持たないので、`free_src()` や `R.src_len` の扱いを SGX4 用に分ける。状態表示・終了コード・ESC は既存の SGX と同じ経路を使う。
- `.SGX4` を USB から抜いたときのために、ファイルハンドルを保持する。モジュールのロードに失敗したら `SGX_EBUS` を返し、アプリはエラー表示して終了する。

```c
// 新しい入口（案）。img/n ではなくパスを受け取り、内部で ra_open する。
const char *sgx4_session_start(const char *path, const char *dir,
                               int argc, const char *const *argv,
                               char *name, unsigned name_n);
```

### 6.7 タスク・表示・ウォッチドッグ

- オーバーレイ／ストアの読み書きはアプリのタスクで行い、待つ間は `mb_sleep_us` で眠る。`stop_requested()` をブロックの区切りで見る。
- ストレージの読み書きは数秒かかることがある。`watchdog_update()` を区切りで呼ぶ（既存の `rbfs` の長い読み出しと同じ）。
- `render_park_on` の間、PSRAM への書き込みは既存の MPU の待たせで止まる。新しい MPU 設定は不要。
- `usb_lock` の入れ子に注意。`store_get` の中で `api->read_file` を呼ばない。アプリがファイル API を呼ぶ前に、ストアのブロックを release する。
- 画面は mode 2、FB 1ページ。POV-Ray の出力は `fb_page(0)` へ直接書く。2ページにすれば最後に切り替えられるが、1ページでも動く。

---

## 7. POV-Ray 3.7 への当てはめ

### 7.1 常駐コアとモジュールの分割（案）

安全側の分割です。形状とテクスチャは vtable を持つオブジェクトを残すため、レンダー中は常駐させます。パーサとファイル形式だけをオーバーレイにします。

| 部分 | 置き場 | 大きさの目安 | 備考 |
|---|---|---:|---|
| 数学・ベクトル・行列・色 | 常駐 | 200〜400KB | ホット |
| シーン traversal・レンダーループ | 常駐 | 300〜500KB | ホット |
| フレームバッファ出力 | 常駐 | 20KB | RGB555 変換 |
| モジュール管理・ストアクライアント | 常駐 | 30KB | SDK |
| オブジェクト基底・交差の共通部 | 常駐 | 200〜400KB | ホット |
| 形状（球・平面・箱・三角形・mesh・CSG） | 常駐 | 800KB〜1.2MB | vtable を残すため |
| テクスチャ（pigment / pattern / normal / finish） | 常駐 | 500〜800KB | vtable を残すため |
| パーサ（tokenizer / SDL / scene 構築） | モジュール 0 | 512KB〜1MB | パース後はアンロード可 |
| 稀な形状（blob / torus / lathe / prism / sor / super / height field） | モジュール 1 または常駐 | 256〜512KB | モジュールにするなら vtable を残さない |
| media / radiosity / photons | モジュール 2 または常駐 | 256KB | 第1段階では無効化 |
| PNG / JPEG / TIFF / OpenEXR | モジュール 3（任意） | 256KB | 第1段階では無効化 |

- 常駐コアは **2.5〜3.0MB**、オーバーレイは **0.5〜1.0MB** が目標。
- パーサとレンダラは時間的に分かれるので、同じスロットを再利用できる。
- **攻めの分割**にするなら、形状をモジュール化し、オブジェクトを「型＋データ」に変えて vtable をなくす。常駐 1.5〜2.0MB、モジュール 1〜2MB にできるが、POV-Ray のソース改造が大きくなる。第1段階では安全側で始める。

### 7.2 Boost / スレッドの除去

POV-Ray 3.7 は Boost.Thread / condition / date_time / format / bind / unordered_map を使います。ベアメタルでは次に置き換えます。

- `boost::thread` / `mutex` / `condition`: 単一スレッドのスタブ。POV-Ray の `Task` クラスは「その場で実行」に変える。レンダリングはタイルを順番に処理する。
- `boost::thread_specific_ptr`（`DECLARE_THREAD_LOCAL_PTR`）: 単一スレッドなので、普通の大域／静的ポインタに置き換える。
- `boost::format`: `snprintf` に置き換える。
- `boost::date_time`: `time()` / `gmtime()` に置き換える。
- `boost::bind` / `function`: C++11 のラムダ / `std::function` に置き換える。
- `boost::unordered_map`: 自前のオープンアドレス表か `std::map` に置き換える。シーン規模では `std::map` でも許容できるが、速度を見て決める。
- `boost::scoped_ptr` / `scoped_array`: `std::unique_ptr` に置き換える（C++11）。
- `POV_THREAD_STACK_SIZE` は使わない。スタックは SGX のタスクスタック 192KB を PSRAM に取る。

### 7.3 画像バッファの廃止

POV-Ray 3.7 のフロントエンドは 20B/画素の画像バッファを持ちます。これをやめます。

- `pov_frontend::Image` の代わりに `FbImage` を作る。
- `FbImage::SetPixel(x, y, colour)` は `extract_colors` 相当で 8bit RGB にし、`RGB(r>>3, g>>3, b>>3)` で 16bit にして `fb_page(0)[y*640+x]` へ書く。
- タイル単位で書く場合は、タイルの範囲を FB へコピーする。1行ずつでもよい。
- 画像の読み返し（post-process、histogram、一部の出力）は第1段階では無効にする。
- 画面は mode 2（テキスト＋RGB555）。テキスト面は状態表示に使う。
- 1ページに直接書くので、描画途中の線が見える（ティアリング）。気になるなら FB を2ページにし、最後に切り替える（+614KB）。

### 7.4 メッシュのアウトオブコア化

建築イメージの最大のメモリ消費者はメッシュ（頂点・三角形・BVH）です。

- パース時:
  - `mesh` / `mesh2` の頂点と三角形を、ストアのブロックへ順に書く。
  - BVH / octree を構築する。ノードは小さければ通常のヒープ、大きければストアのブロックへ置く。
  - 葉ノードは「ストア ID、開始ブロック、三角形数」を持つ。
  - **BVH の構築もアウトオブコアで行う。** 頂点を Morton 順に並べ、外部ソート／分割して BVH を作る。全部をヒープに載せない。第1段階では mesh の大きさに上限を設け、ヒープで作れる範囲から始める。
- レンダー時:
  - BVH をたどり、葉に来たら `store_get(id, block)` で三角形ブロックを取る。
  - 交差判定の間だけ pin し、終わったら `store_release`。
  - 次の葉のブロックを `store_prefetch` で先読みする。
- テクスチャ:
  - イメージマップはタイル（例 256×256）に分けてストアへ置く。UV からタイルを引き、`store_get` で読む。
- シーン DB:
  - パース後のシーンがヒープに収まらない場合、オブジェクトとテクスチャをストアへ serialize し、レンダー時にブロック単位で読む。これは第3段階。

### 7.5 パーサとファイル I/O

- POV-Ray 3.7 の `ITextStream` / `IStream` を、SGX の `ra_*` または `fmemopen` の上に実装する。
- シーンファイルと include は、アプリのフォルダからの相対パスで解決する。`read_file` で丸ごと読める大きさなら `fmemopen`、大きいものは `ra_*`。
- 出力ファイル（TGA/PNG など）は使わない。`FbImage` へ直行。
- PNG / JPEG / TIFF / OpenEXR は第1段階では無効。イメージマップが必要になったら、PNG だけモジュールとして追加する。

### 7.6 性能の見込み

- 302MHz の M33、倍精度は DCP。POV-Ray 3.7 の 640×480 は、シーンによるが **数分〜数十分/フレーム** になる可能性が高い。
- ストアのキャッシュミスが 1 回 40〜130ms なので、ヒット率が重要。BVH の局所性、先読み、ブロックサイズ（64KB）で調整する。
- 最初は 320×240 で描いて 2 倍拡大する版を作り、パイプラインとストアの検証を優先する。mode 2 の 640×480 は最後にする。

---

## 8. 段階計画

### Phase 0: 測る（1〜2日）

- POV-Ray 3.7 を ARM 向けに `-Os` でクロスコンパイルし、常駐コア／モジュールのサイズを測る。
- Boost を外した場合のサイズ、例外あり／なしのサイズを比較する。
- ホストで architectural なシーン（mesh 多め）のピークメモリを測る。
- **ゲート**: 常駐コアが 2.5MB 以下、モジュール合計が 2MB 以下なら次へ。超えるなら機能削減。

### Phase 1: オーバーレイだけで動かす（1〜2週間）

- ABI 4.0 ヘッダ、`sgx4_pack.py`、`overlay_load`、`sgx4.ld` を作る。
- POV-Ray の常駐コア＋パーサモジュール＋形状モジュールを組み、簡単なシーンを mode 2 の FB へ出す。
- ストアはまだ使わない。ヒープは通常の PSRAM。
- **ゲート**: 実機で 1 フレームが出る。モジュールの差し替えが動く。FB の `fifo_stall` が増えない。

### Phase 2: メッシュのブロックストア（2〜3週間）

- `store_get` / `release` / LRU / 書き戻しを実装。
- POV-Ray の mesh / mesh2 をストアへ載せる。BVH はヒープ。
- USB の raw read/write とスワップファイルを作る。
- **ゲート**: 10〜50MB のメッシュシーンが、2MB のキャッシュで最後まで描ける。キャッシュミス率と 1 フレームの時間を記録。

### Phase 3: シーン DB とテクスチャ（2〜4週間）

- シーン DB の serialize / deserialize。
- イメージマップのタイルストア。
- 圧縮（LZ4 など）と先読み。
- **ゲート**: 建築シーンで PSRAM 使用量が目標内、ストレージの読み出しがボトルネックにならない。

### Phase 4: 仕上げ

- radiosity / media / photons の取捨選択。
- パッド・マウス・キーでカメラ操作。
- 進捗表示、エラー表示、ESC 停止。
- AGPL のソース配布物（POV-Ray のソース、ビルド手順、ライセンス）をまとめる。

---

## 9. 試験

### ホスト

- `sgx4_pack.py`: モジュールの再配置、スロットの大きさ、エントリの Thumb ビット、モジュール間参照の拒否。
- `overlay`: スロットの load/unload/pin、同じモジュールの再ロード、pin 中の差し替え拒否。
- `store`: LRU、pin、dirty、書き戻し、エクステントをまたぐ読み書き、キャッシュ満杯、I/O 失敗。
- POV-Ray のホスト版で、ストアを RAM ディスクにして同じシーンを描き、結果を比較する。

### QEMU

- MPU を使わないので、overlay/store のロジックをそのまま試せる。
- モジュールの差し替え、ストアの書き戻し、停止要求。

### 実機

- 簡単なシーン（球・平面）を mode 2 で表示。
- メッシュシーン（10MB、50MB）を USB の swap で描く。
- `fifo_stall` / `underrun` / `render_park_*` / USB のエラー / ウォッチドッグを記録。
- USB を抜く、swap ファイルを消す、容量を超える、モジュールを壊す、の失敗試験。
- ESC / Shift+ESC で長いストア読み出しを止められること。

---

## 10. ライセンスと分離

- POV-Ray 3.7 は **AGPLv3**。`.SGX4` はファームとは別のプログラムで、ファームのシンボルには結ばず、`sgx_api_t`（MIT）だけを呼ぶ。Doom（GPL-2.0）を `DOOM.SGX` にしたのと同じ分離。
- 配布するときは、POV-Ray のソース、ビルド手順、AGPL の全文、変更点を `.SGX4` と一緒に配る。ファームと SDK は MIT のまま。
- `.SGX4` のヘッダ（`include/sgx4_api.h`）と `sgx4_pack.py` は SDK 側（MIT）に置く。POV-Ray のモジュール interface ヘッダは POV-Ray 側（AGPL）に置く。
- ファームの `src/sgx/sgx4.c`・`overlay.c`・`store.c`・`swap.c` は MIT。POV-Ray のコードを含めない。

---

## 11. 未決事項

1. **POV-Ray 3.7 の実サイズ**。まず Phase 0 で測る。常駐コアが大きすぎる場合の分割を決める。
2. **ストレージの媒体**。USB メモリか HDD/SSD か。フルスピードの帯域と、抜き差しの信頼性。
3. **スワップファイルか raw 区画か**。ファイルは準備が楽、raw は速くて断片化しない。両対応にする。
4. **圧縮**。LZ4 か heatshrink か。M33 の速度と圧縮率のバランス。
5. **モジュール間インターフェースの自動生成**。最初は手書きの関数表、慣れたらパッケージャがヘッダを生成する。
6. **POV-Ray の例外処理**。`-fno-exceptions` にできるか、`setjmp/longjmp` へ置き換えるか。コードサイズに大きく効く。
7. **AGPL の配布形態**。`.SGX4` と POV-Ray のソースを一緒に配る。ファームと SDK は MIT のまま。
8. **FB のページ数**。POV-Ray 用ビルドは 1ページにするか、2ページでティアリングを避けるか。

---

## 12. まとめ

- **MMU がないので、透過ページングでは 8MB を超えられない。** これは避けられない制約です。
- プログラムは **常駐コア＋固定スロットのオーバーレイ**、ヒープは **ハンドル付きブロックストア** にすれば、PSRAM のアドレス空間を超えてストレージを使えます。
- この形は MPU の追加使用がなく、既存の QMI ゲートと衝突しません。実装も透過ページングより単純で、失敗時の扱いも明確です。
- POV-Ray 3.7 は、常駐コア 1.5〜2MB、オーバーレイ 1〜2MB、ストアキャッシュ 2MB、ヒープ 0.5〜1MB、FB 0.6MB で **PSRAM 8MB に収まる見込み**です。鍵は、画像バッファを捨てて FB へ直接書くことと、メッシュをブロックストアへ載せることです。
- 最初の作業は **POV-Ray 3.7 の ARM サイズ測定**と、**オーバーレイだけの Phase 1** です。

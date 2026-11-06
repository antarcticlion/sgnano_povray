// SGX 4.0 ABI（案）。2026-11-06。
// 設計の説明は SPEC_sgx4_overlay_store.md。このヘッダは型のたたき台で、まだファームには入っていない。
//
// 3.x との関係:
//   - `.sgx`（magic "SGX1"）は今までどおり。
//   - `.sgx4`（magic "SGX4"）は常駐イメージ＋モジュール＋ストア表を持つ。
//   - ファームは両方を受け付ける。4.x のアプリは 4.x 対応ファームでのみ動く。
#ifndef SGX4_API_H
#define SGX4_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SGX4_MAGIC        0x34584753u   // "SGX4"
#define SGX4_ABI_MAJOR    4
#define SGX4_ABI_MINOR    0
#define SGX4_LOAD_MAX     (4u * 1024u * 1024u)   // 常駐イメージ＋bss の上限
#define SGX4_MAX_SLOTS    8u

// ヘッダの印
#define SGX4_F_OVERLAY    0x0001u
#define SGX4_F_STORE      0x0002u
#define SGX4_F_FIXED_BASE 0x0004u
#define SGX4_F_NO_TEXT    0x0008u
#define SGX4_F_WAIT_KEY   0x0010u

// モジュールの印
#define SGX4_MOD_PIN      0x0001u
#define SGX4_MOD_COMPRESS 0x0002u
#define SGX4_MOD_NO_RELOC 0x0004u

// ストアの印
#define SGX4_STORE_READONLY  0x0001u
#define SGX4_STORE_WRITEBACK 0x0002u
#define SGX4_STORE_CREATE    0x0004u
#define SGX4_STORE_RAW       0x0008u
#define SGX4_STORE_COMPRESS  0x0010u

// 4.0 で足す誤り（既存の SGX_E* の後ろに置く）
enum {
    SGX4_EBUSY = 64,
    SGX4_EBUS,
    SGX4_ECORRUPT,
    SGX4_ETIMEDOUT,
};

typedef struct {
    uint32_t magic;
    uint16_t abi_major, abi_minor;
    uint32_t header_size;
    uint32_t flags;
    uint32_t entry;                 // 常駐イメージ内の入口（Thumb、奇数）
    uint32_t stack_size;            // 0 なら 16KB

    uint32_t resident_image_offset;
    uint32_t resident_image_size;
    uint32_t resident_bss_size;
    uint32_t resident_reloc_offset;
    uint32_t resident_reloc_count;

    uint32_t module_count;
    uint32_t module_table_offset;

    uint32_t slot_count;
    uint32_t slot_size;             // 固定スロット。可変なら slot_table_offset を使う
    uint32_t slot_table_offset;

    uint32_t store_count;
    uint32_t store_table_offset;
    uint32_t string_table_offset;

    char     name[32];
    uint32_t reserved[4];
} sgx4_header_t;

typedef struct {
    uint32_t id;
    uint32_t slot;
    uint32_t image_offset;
    uint32_t image_size;
    uint32_t bss_size;
    uint32_t entry_offset;
    uint32_t reloc_offset;
    uint32_t reloc_count;
    uint32_t flags;
    uint32_t name_offset;
} sgx4_module_t;

typedef struct {
    uint32_t id;
    uint32_t block_size;            // 4KB〜256KB、2のべき乗
    uint32_t block_count;
    uint32_t cache_blocks;
    uint32_t flags;
    uint32_t name_offset;
} sgx4_store_t;

typedef struct {
    const char *path;               // NULL なら raw
    uint32_t block_size;
    uint32_t block_count;
    uint32_t cache_blocks;
    uint32_t flags;
    uint32_t lba;
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
    uint32_t current[SGX4_MAX_SLOTS];   // -1 = 空
} sgx4_overlay_stats_t;

// モジュールの init に渡す文脈。iface は常駐コア側の関数表。
typedef struct {
    uint32_t abi_major, abi_minor;
    uint32_t module_id;
    uint32_t slot;
    const void *api;                // const sgx_api_t *
    uint8_t *base;
    uint32_t size;
} sgx4_module_ctx_t;

typedef int (*sgx4_module_init_t)(const sgx4_module_ctx_t *ctx, void *iface, uint32_t iface_size);

// sgx_api_t の末尾に足す関数の型（案）。実際の表は sgnano の sgx_api.h に追記する。
typedef int      (*sgx4_overlay_slots_fn)(void);
typedef uint32_t (*sgx4_overlay_slot_size_fn)(int slot);
typedef uint8_t *(*sgx4_overlay_base_fn)(int slot);
typedef int      (*sgx4_overlay_load_fn)(int slot, int module);
typedef int      (*sgx4_overlay_unload_fn)(int slot);
typedef int      (*sgx4_overlay_loaded_fn)(int slot);
typedef void    *(*sgx4_overlay_entry_fn)(int slot, int module);
typedef int      (*sgx4_overlay_pin_fn)(int slot);
typedef int      (*sgx4_overlay_unpin_fn)(int slot);
typedef int      (*sgx4_overlay_stats_fn)(sgx4_overlay_stats_t *out);

typedef int      (*sgx4_store_open_fn)(const sgx4_store_config_t *cfg);
typedef int      (*sgx4_store_close_fn)(int id);
typedef void    *(*sgx4_store_get_fn)(int id, uint32_t block);
typedef int      (*sgx4_store_release_fn)(void *p, int dirty);
typedef int      (*sgx4_store_pin_fn)(void *p);
typedef int      (*sgx4_store_unpin_fn)(void *p);
typedef int      (*sgx4_store_prefetch_fn)(int id, uint32_t block, uint32_t count);
typedef int      (*sgx4_store_flush_fn)(int id);
typedef int      (*sgx4_store_stats_fn)(int id, sgx4_store_stats_t *out);
typedef int      (*sgx4_store_error_fn)(int id);

#endif // SGX4_API_H

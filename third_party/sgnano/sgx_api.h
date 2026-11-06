// sgnano の外部実行ファイル（SGX）の ABI（2026-10-24）。使い方は docs/sgx_abi.md。
//
// 外部実行ファイル（.sgx）は再配置できる（番地 0 に結び、再配置の表を持つ）。ファームが PSRAM のどこか（キャッシュを通る窓）に置いて番地を直し、
// M33BEATOS のタスクとして走らせる。いくつものプログラム・タスクを同時に走らせ、協調させられる（spawn・task_create・mutex など）。
// ファームの機能は、入口に渡される関数の表（sgx_api_t）だけから使う（ファームのシンボルには結ばない）。
// 表は ABI の版（major.minor）を持ち、minor を上げるときは末尾に足すだけにする（並びを変えない）。major が違えば読み込まない
// （【2026-11-06】4.0 だけは例外: 3.x の表の末尾に足しただけなので、ファームは 3.x のプログラムも読み込む。SGX_ABI_COMPAT_MAJOR）。
//
// 呼び出しの決まり: AAPCS（ARMv8-M Mainline、Thumb、浮動小数点は softfp = 引数・戻り値は整数のレジスタ）。
// どの関数も、プログラムのタスク（入口を呼ばれたタスク）からだけ呼ぶ。割り込みは持てない。
// このファイルは sgnano の SDK の一部で、MIT ライセンス（プログラムのライセンスは自由に選んでよい）。
#ifndef SGX_API_H
#define SGX_API_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SGX_ABI_MAJOR 4
#define SGX_ABI_MINOR 0                          // 4.0（2026-11-06）: ブロックストア（store_*）と SGX_EBUSY を足した。表の並びは 3.5 の後ろに足しただけ。
                                                 //      ファームは 3.x のプログラムもそのまま走らせる（SGX_ABI_COMPAT_MAJOR）。4.0 で組んだものは 4.x のファームだけ
                                                 // 3.5（2026-11-06）: stderr へ書く term_write_err を末尾に足した（3.4 は request_tracker）
                                                 // 3.3（2026-11-04）: 組み込みアプリの UI の訳（translate）と SGX_F_LOCALIZE を足した
                                                 // 3.2（2026-11-03）: S: のクラスタ地図とデフラグ・エンフラグ・0 クリア（sysdisk_*）を末尾に足した
                                                 // 3.1（2026-11-02）: メモリの使用量（mem_info）とタスクの一覧（task_info）を末尾に足した
                                                 // 3.0（2026-10-31）: ローレゾのマップを 1 ます 16 ビットに（lr_map は uint16_t *）。表の並びは 2.0 と同じ。2.x で組んだものは読み込まない
                                                 // 2.0（2026-10-31）: 1.x の区切り（ユーザー判断）

#define SGX_ABI_COMPAT_MAJOR 3                   // ファームが読み込むもう 1 つの major（4.0 は 3.x の上位互換）

// ---- 実行ファイルの形 ----
#define SGX_MAGIC       0x31584753u              // "SGX1"
#define SGX_LINK_BASE   0x00000000u              // 結ぶ番地（sgx.ld）。読み込んだら置いた番地との差を表の語に足す
#define SGX_LOAD_MAX    (4u * 1024u * 1024u)     // 像（コード・定数・初めの値）と .bss の合計の上限
#define SGX_F_ESC_KEY   0x0001u                  // ESC をプログラムに渡す（ファームは ESC で止めない。止めるのは Shift+ESC）
#define SGX_F_SRAM_STACK 0x0002u                 // スタックを SRAM に（速い。取れなければ PSRAM）
#define SGX_F_WAIT_KEY  0x0004u                  // ふつうに（0 で）終わっても、画面をそのままにして何かキーを待ってからファイラーへ（Ruby と同じ。2026-10-25）
#define SGX_F_FULL_TEXT 0x0008u                  // テキスト面の 30 行すべてをアプリが使う（0〜29 行目。2026-11-04）
#define SGX_F_LOCALIZE  0x0010u                  // 組み込みアプリの内部 UI も表示言語へ（2026-11-04）

// ---- ブロックストア（ABI 4.0、2026-11-06）----
// ファイルを決まった大きさのブロックの並びとして、PSRAM のキャッシュに必要な分だけ載せて読み書きする。
// PSRAM に収まらない大きなデータ（メッシュ・テクスチャ・チェックポイント）をストレージに置くため。
// ブロックは生のポインタでなくストアの番号とブロックの番号で指し、使う間だけ get で取り出して release で返す。
//
// - 置き場はファイル 1 つ（USB ストレージの FAT12/16/32・exFAT・NTFS。S: は読むだけ。CD・フロッピーは使えない）。
//   開いている間、ファイルの大きさと置き場は変えない（ほかのプログラム・シェルから書き換えない・消さない）。
// - 書き込みは、ファイルの中のセクタをその場で書き換える（大きさ・置き場・日時は変わらない。ファイルの属性の
//   読み出し専用は開くときに断る）。書くのは、追い出すとき・store_flush・store_close・プログラムが終わるとき。
// - どの関数も、プログラムのどのタスクから呼んでもよい（中で順番に並べる）。待つ間（読み書き）は眠る。
// - ブロックの中身の番地はキャッシュを通る窓（0x11...）。release するまで動かない・追い出されない。
#define SGX_STORE_READONLY  0x0001u   // 読むだけ（release の dirty は -SGX_EROFS）
#define SGX_STORE_WRITEBACK 0x0002u   // 書いたブロックを書き戻す（READONLY でなければ同じ扱い。書き方をはっきりさせる印）
#define SGX_STORE_CREATE    0x0004u   // ファイルが無ければ 0 で埋めて作る（block_size x block_count。1MB に約 2 秒）
#define SGX_STORE_RAW       0x0008u   // 予約（生の区画。4.0 では -SGX_ENOTSUP）
#define SGX_STORE_COMPRESS  0x0010u   // 予約（圧縮。4.0 では -SGX_ENOTSUP）
#define SGX_STORE_MAX       8u        // 1 回の実行で同時に開けるストア
#define SGX_STORE_BLOCK_MIN 512u
#define SGX_STORE_BLOCK_MAX (256u * 1024u)
#define SGX_STORE_CACHE_MAX 256u      // 1 つのストアのキャッシュのブロック数の上限

typedef struct {
    const char *path;       // ファイル（"A:/dir/name" か、プログラムのフォルダからの相対）
    uint32_t block_size;    // 512〜256KB の 2 のべき乗
    uint32_t block_count;   // ブロックの数。0 = 既存のファイルの大きさから（大きさ / block_size、端は使わない。CREATE とは組めない）
    uint32_t cache_blocks;  // キャッシュのブロック数（1〜SGX_STORE_CACHE_MAX）。PSRAM に block_size x cache_blocks を取る
    uint32_t flags;         // SGX_STORE_*
    uint32_t reserved[3];   // 0 にしておく
} sgx_store_config_t;

typedef struct {
    uint32_t block_size, block_count, cache_blocks;
    uint32_t resident_blocks, pinned_blocks, dirty_blocks;
    uint32_t hits, misses, evictions, writebacks, errors;
    uint32_t bytes_read, bytes_written;
    uint32_t max_wait_us;   // 1 回の get / flush で待った最長
} sgx_store_stats_t;

typedef struct sgx_api sgx_api_t;
typedef int (*sgx_entry_t)(const sgx_api_t *api, int argc, char **argv);

typedef struct {
    uint32_t magic;                              // SGX_MAGIC
    uint16_t abi_major, abi_minor;               // 作ったときの SGX_ABI_MAJOR / MINOR（ファームの minor 以下であること）
    uint32_t header_size;                        // sizeof(sgx_header_t)
    uint32_t link_base;                          // SGX_LINK_BASE
    uint32_t image_size;                         // 像の大きさ（見出しも入れて）。ファイルはこの後ろに再配置の表（reloc_count 個の 32 ビット）が続く。像の後ろを .bss として 0 にする
    uint32_t bss_size;
    uint32_t entry;                              // sgx_entry_t の番地（Thumb なので奇数）
    uint32_t stack_size;                         // タスクのスタック（0 なら 16KB）
    uint32_t flags;                              // SGX_F_*
    char     name[32];                           // 表示に使う名前（UTF-8、0 で終わる）
    uint32_t reloc_count;                        // 再配置の表の数（像の頭からのバイトの位置。その 32 ビットの語に、置いた番地 - link_base を足す）。sgx_link.py が書く
    uint32_t reserved[3];
} sgx_header_t;

// ---- 色・画面 ----
// 画素の値（RGB555 の形。ファームの決まった並び）は rgb() で作る。CLUT8・CLUT4 の FB とビットマップは色の番号
enum {
    SGX_MODE_OFF = 0,           // 表示なし（同期だけ）
    SGX_MODE_TEXT = 1,          // テキスト（80x30、24kHz は 80x25）
    SGX_MODE_TEXT_FB = 2,       // テキスト + FB（RGB555、640x480）
    SGX_MODE_TEXT_LR_C8 = 3,    // テキスト + ローレゾ + FB（CLUT8）
    SGX_MODE_TEXT_LR_C4 = 4,    // テキスト + ローレゾ + FB（CLUT4）
    SGX_MODE_TEXT_LR = 5,       // テキスト + ローレゾ
    SGX_MODE_LR = 6,            // ローレゾだけ
    SGX_MODE_TEXT_BM8 = 7,      // テキスト（25 行）+ 320x200x8 のビットマップ
};
#define SGX_MODE_NO_TEXT 0x100                   // set_mode に足す: テキスト面を開かない・閉じる（SRAM が約 52KB 空く。ビットマップの 2 枚の面などに）
#define SGX_FB_W 640
#define SGX_FB_H 480
#define SGX_BM_W 320
#define SGX_BM_H 200
#define SGX_LR_W 320
#define SGX_LR_H 240
#define SGX_LR_OBJS 128
#define SGX_LR_MAP_W 64
#define SGX_LR_MAP_H 64
#define SGX_LR_COLORS 128
#define SGX_LR_TRANSPARENT 0x80u                 // パターンの画素の bit7 = 透過
#define SGX_LR_ME_TILE  0x03FFu                  // マップのます（16 ビット、ABI 3.0）: bit0-9 タイル（0〜1023 = パターンの先頭 64KB）、bit10 横反転、bit11 縦反転、bit12-15 パレット（0〜15）
#define SGX_LR_ME_HFLIP 0x0400u
#define SGX_LR_ME_VFLIP 0x0800u
#define SGX_LR_ME_PAL_SHIFT 12
#define SGX_LR_ME_TILES 1024u                    // BG から指せるタイルの数（オブジェクトはパターン全体）
#define SGX_LR_ME_PALS  16u                      // BG から指せるパレットの数（オブジェクトは lr_palettes() 本）
#define SGX_LR_OBJ_SHOW 0x4000u                  // obj.attr: bit0-7 高さ-1、bit8-9 幅（0=8 1=16 2=32 3=64）、bit10-11 優先度、bit12 横反転、bit13 縦反転、bit14 表示
typedef struct { int16_t x, y; uint16_t pattern, attr; uint32_t attr2; } sgx_lr_obj_t;
typedef struct { bool bg[2], obj; uint16_t sx[2], sy[2]; } sgx_lr_ctrl_t;

// テキスト面のセルの属性（32 ビット。src/video/text.h と同じ）
#define SGX_T_FG(c)      ((uint32_t)(c) & 15u)   // 文字色（16 色、PC-98 の並び）
#define SGX_T_BG(c)      (((uint32_t)(c) & 15u) << 4)
#define SGX_T_FG_TRANS   0x00000200u
#define SGX_T_BG_TRANS   0x00000400u
#define SGX_T_REVERSE    0x00000800u
#define SGX_T_BLINK      0x00001000u
#define SGX_T_UNDER      0x00002000u
#define SGX_T_STRIKE     0x00004000u
#define SGX_T_OVER       0x00008000u
#define SGX_T_BOLD       0x00010000u
#define SGX_T_ROT90      0x00020000u
#define SGX_T_ROT180     0x00040000u
#define SGX_T_ROT270     0x00060000u
#define SGX_T_DH         0x00080000u             // 縦倍角
#define SGX_T_DW         0x00100000u             // 横倍角
#define SGX_T_LEFT_LINE  0x00800000u             // 左ライン（2026-10-27）
#define SGX_T_RIGHT_LINE 0x01000000u             // 右ライン

typedef struct {
    const char *timing, *mode_name, *fb_name;
    uint32_t line_hz, frame_hz_x100;
    uint32_t frames, underrun, fifo_stall, recover, late_start, late_render;
    uint32_t pair_us[2];
} sgx_video_info_t;
typedef struct {
    uint32_t uptime_s, mem_free, mem_largest;    // 起動からの秒・SRAM のアリーナの空きの合計と最大の塊
    uint32_t usb_devices, hubs, pads, usb_reinit, usb_wedged;
    bool     kbd, mouse;
    uint16_t kbd_vid, kbd_pid;
    uint8_t  kbd_leds;
} sgx_sys_info_t;

// ---- 入力 ----
// キーのコード: 文字は ASCII、特殊キーは 0x80 以上（src/input/keycode.h と同じ）
#define SGX_KEY_F(n)     (0x80 + (n))            // F1〜F12 = 0x81〜0x8C
#define SGX_KEY_UP       0x90
#define SGX_KEY_DOWN     0x91
#define SGX_KEY_LEFT     0x92
#define SGX_KEY_RIGHT    0x93
#define SGX_KEY_PGUP     0x94
#define SGX_KEY_PGDN     0x95
#define SGX_KEY_HOME     0x96
#define SGX_KEY_END      0x97
#define SGX_KEY_INS      0x98
#define SGX_KEY_ENTER    0x0D
#define SGX_KEY_BS       0x08
#define SGX_KEY_TAB      0x09
#define SGX_KEY_ESC      0x1B
#define SGX_KEY_DEL      0x7F
#define SGX_KEY_UART     0x10000                 // key の結果に付く: UART（端末）から来たキー（押した知らせだけで、離した知らせは無い）
#define SGX_KEY_SHIFT    0x100                   // key_down で使う修飾キー
#define SGX_KEY_CTRL     0x101
#define SGX_KEY_ALT      0x102
typedef struct { bool connected; uint8_t buttons; int32_t x, y, wheel; } sgx_mouse_t;   // buttons: bit0 左・bit1 右・bit2 中。x, y は累計
typedef struct { bool connected, ready, proximity, tip, button1, button2; uint8_t tool, pad; uint16_t x, y, pressure, x_max, y_max, pressure_max;
                 int32_t wheel; const char *name; } sgx_tablet_t;
#define SGX_PAD_MAX  4
#define SGX_PAD_AXES 6
typedef struct { bool connected; uint16_t vid, pid; uint8_t hat, buttons_n, axes_mask; uint32_t buttons; int16_t axis[SGX_PAD_AXES]; } sgx_pad_t;
// hat: 0 = 上・時計回り・8 以上 = 中立。buttons: F310 の D モードの並び（bit0 X・1 A・2 B・3 Y・4 LB・5 RB・6 LT・7 RT・8 BACK・9 START）。axis は -127〜127

// ---- 音 ----
enum { SGX_WAVE_SINE = 0, SGX_WAVE_TRIANGLE, SGX_WAVE_SAW, SGX_WAVE_SQUARE, SGX_WAVE_PULSE25, SGX_WAVE_ORGAN, SGX_WAVE_NOISE };   // src/audio/audio.h と同じ
enum { SGX_SND_WT = 0, SGX_SND_PCM, SGX_SND_STREAM, SGX_SND_NOISE };   // NOISE は 4ch 目のドラム用（2026-10-29）
enum { SGX_PCM_U8 = 0, SGX_PCM_S16 = 1 };        // pcm_play の標本の形（モノラル）

// ---- ファイル ----
// パスは "A:/dir/name"（ドライブ文字・コロン・スラッシュ）か、プログラムのフォルダからの相対。UTF-8
enum {                                            // 誤り（src/ruby/rbfs.h と同じ並び）
    SGX_OK = 0, SGX_ENOENT, SGX_EEXIST, SGX_EACCES, SGX_ENOTEMPTY, SGX_ENOTDIR, SGX_EISDIR, SGX_EROFS, SGX_ENOSPC, SGX_EIO,
    SGX_ENOTSUP, SGX_EINVAL, SGX_ENOMEM, SGX_ENODEV,
    SGX_EBUSY,                                    // 4.0: 使っている（取り出し中のブロックがある・キャッシュがすべて取り出し中）
};
#define SGX_ATTR_RO   0x01u
#define SGX_ATTR_HID  0x02u
#define SGX_ATTR_SYS  0x04u
#define SGX_ATTR_DIR  0x10u
#define SGX_ATTR_ARC  0x20u
typedef struct { bool is_dir; uint8_t attr; uint32_t size, mtime, ctime, atime; } sgx_stat_t;   // 日時は 1970 年からの UTC の秒
typedef struct { const char *name; unsigned name_len; bool is_dir; uint8_t attr; uint32_t size, mtime; } sgx_dirent_t;
typedef void (*sgx_dirent_fn)(const sgx_dirent_t *e, void *user);
typedef struct sgx_file sgx_file_t;              // 大きいファイルを好きな位置から読む口（ra_*）
typedef struct { char letter; uint8_t type; bool ready; } sgx_drive_t;   // type: 1 FAT（USB ストレージ・S:）・2 FAT（フロッピー）・3 exFAT・4 NTFS・5 CD（ISO9660・音楽）・6 そのほか
typedef struct { char vendor[9], product[17]; uint32_t mb; } sgx_storage_t;

// ---- メモリとタスク（ABI 3.1、2026-11-02）----
// メモリのアリーナ（SRAM / PSRAM）と、アプリが借りている量、sgsh のヒープの状態。バイト単位
typedef struct {
    uint32_t sram_total, sram_free, sram_largest;     // SRAM のアリーナ
    uint32_t psram_total, psram_free, psram_largest;  // PSRAM のアリーナ
    uint32_t app_count, app_sram_bytes, app_psram_bytes;   // 走っているアプリが借りているもの
    uint32_t shell_heap_free, shell_heap_total;       // sgsh のヒープ（sgsh が走っていなければ 0）
    uint32_t shell_backlog_lines;                     // sgsh のバックログの行数
} sgx_mem_info_t;
// タスクの状態（src/rtos/m33beatos.h の MB_T_* と同じ並び）
enum { SGX_TASK_READY = 1, SGX_TASK_BLOCKED, SGX_TASK_SLEEP, SGX_TASK_DEAD };
typedef struct {
    char     name[16];                    // 名前（0 で終わる。長ければ切る）
    uint8_t  state;                       // SGX_TASK_*
    uint8_t  prio, base_prio;             // いまの優先度（貸し出し中は上がる）・決めた優先度
    uint8_t  current;                     // 1 = いま走っている
    uint8_t  suspended;                   // 1 = 止められている
    uint8_t  qmi;                         // QMI（フラッシュ・PSRAM）の印
    uint16_t reserved;
    uint32_t stack_size, stack_unused;    // スタックの大きさ・使っていない量
    uint32_t run_us, switches;            // 走った時間の合計（µs）・走り出した回数
} sgx_task_info_t;

// ---- タスクとプログラム（協調）----
typedef struct sgx_task sgx_task_t;               // プログラムの中のタスク（入口の main もその 1 つ）
typedef struct sgx_mutex sgx_mutex_t;
typedef struct sgx_sem sgx_sem_t;
typedef struct sgx_queue sgx_queue_t;
typedef struct sgx_socket sgx_socket_t;           // TCP / TLS のソケット（ABI 1.7）
#define SGX_WAIT_FOREVER 0xFFFFFFFFu             // 待つ関数の timeout_us に（止める頼みが来れば、待っている所で止まる）

struct sgx_api {
    uint16_t abi_major, abi_minor;
    uint32_t size;                                // この表の大きさ（バイト）。新しい関数は size を見てから使う

    // ==== 実行 ====
    void     (*exit)(int code);                   // 終わる（戻らない）。入口から戻っても同じ
    bool     (*stop_requested)(void);             // 止めてほしいと頼まれた（ESC・Shift+ESC）。長い処理の区切りで見る
    void     (*sleep_us)(uint32_t us);            // 眠る（ほかのタスクが走る）
    void     (*yield)(void);                      // 少し譲る（1ms 眠る）
    uint32_t (*ms)(void);                         // 起動からのミリ秒
    uint64_t (*us)(void);                         // 起動からのマイクロ秒
    uint64_t (*utc_us)(void);                     // いまの日時（1970 年からの UTC の µs。時計を合わせていなければ既定の日時 + 起動からの時間）
    bool     (*clock_set)(void);                  // NTP で時計を合わせてある
    uint32_t (*random_seed)(void);
    void     (*log)(const char *s, unsigned n);   // UART のログへ
    int      (*vsnprintf)(char *buf, size_t n, const char *fmt, va_list ap);   // ファームの printf の書式（%d %u %x %s %c %p %f %e %g、幅・精度。%n は無い）
    void     (*term_write)(const char *s, unsigned n);   // テキストの端末へ（Ruby の puts と同じ。ANSI のエスケープ列、UART にも）
    unsigned (*term_getline)(char *buf, unsigned max);   // 1 行打ってもらう（"\n" つき。Ctrl-D なら 0）
    const char *(*program_dir)(void);             // プログラムのフォルダ（"A:/dir"）
    const char *(*program_path)(void);            // プログラムのファイル

    // ==== メモリ ====
    // PSRAM はキャッシュを通る窓（0x11...）で返す。取ったものは終わるときにファームがまとめて返す
    void    *(*psram_alloc)(uint32_t bytes, uint32_t align);
    void     (*psram_free)(void *p);
    uint32_t (*psram_largest)(void);
    void    *(*sram_alloc)(uint32_t bytes, uint32_t align);   // SRAM（速いが少ない。数十 KB まで）
    void     (*sram_free)(void *p);
    uint32_t (*sram_largest)(void);
    // FB のページ（各 600KB）を借りる。画面モード 7 など FB を使わない間だけ。返すときは黒で消す（終わるときにも）
    unsigned (*fb_borrow)(void **pages, unsigned max, uint32_t *page_bytes);
    void     (*fb_return)(void);

    // ==== 画面 ====
    int      (*mode)(void);
    const char *(*set_mode)(int mode);            // 画面モードを替える（NULL = できた、ほかは理由）。終わるまで待つ
    bool     (*fb_visible)(void);
    void     (*set_fb_visible)(bool on);
    bool     (*text_visible)(void);
    void     (*set_text_visible)(bool on);
    void     (*wait_frames)(unsigned n);          // n フレーム待つ（vsync）
    uint32_t (*frame_count)(void);
    void     (*video_info)(sgx_video_info_t *o);
    uint16_t (*rgb)(unsigned r, unsigned g, unsigned b);   // 0..255 -> 画素の値

    // ---- FB（640x480。RGB555 = 1 画素 16 ビット、CLUT8 = 8、CLUT4 = 4 ビット（上位ニブルが左））----
    int      (*fb_pages)(void);
    int      (*disp_page)(void);
    void     (*set_disp_page)(int p);             // 表示の切り替えはフレームの境目
    int      (*write_page)(void);
    void     (*set_write_page)(int p);
    void    *(*fb_page)(int p);                   // ページの先頭（キャッシュを通らない窓。直に書いてよい）
    int      (*fb_bits)(void);
    void     (*fb_palette)(int i, uint16_t color);   // CLUT8（0〜255）・CLUT4（0〜15）
    uint16_t (*fb_palette_get)(int i);
    // 描くのは書き込みページ。c は RGB555 なら画素の値、CLUT なら色の番号。範囲の外は描かない
    void     (*fb_clear)(uint32_t c);
    void     (*fb_pset)(int x, int y, uint32_t c);
    int32_t  (*fb_pget)(int x, int y);            // 範囲の外は -1
    void     (*fb_line)(int x0, int y0, int x1, int y1, uint32_t c);
    void     (*fb_rect)(int x, int y, int w, int h, uint32_t c);
    void     (*fb_fill_rect)(int x, int y, int w, int h, uint32_t c);
    void     (*fb_triangle)(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t c);
    void     (*fb_fill_triangle)(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t c);
    void     (*fb_blit)(int src_page, int sx, int sy, int w, int h, int dx, int dy, int32_t key);   // key < 0 なら透明色なし
    void     (*fb_get_block)(int x, int y, int w, int h, void *out);   // 1 画素 2 バイト（RGB555）か 1 バイト（CLUT8・CLUT4）
    void     (*fb_put_block)(int x, int y, int w, int h, const void *in, int32_t key);

    // ---- ローレゾ面（モード 3〜6。320x240、BG 2 面 + オブジェクト 128 個）----
    // パターン・マップ・パレットは直に書く（すぐ効く）。オブジェクトとスクロール・表示は影に書いて lr_commit（次のフレームの頭に写す）
    bool     (*lr_ready)(void);
    uint8_t *(*lr_pattern)(uint32_t *bytes);      // 1 タイル = 8x8 = 64 バイト（色の番号、bit7 = 透過）
    uint16_t *(*lr_map)(int plane);               // 64 x 64 ます（1 ます 16 ビット、ABI 3.0）
    uint16_t *(*lr_palette)(int pal);             // 128 色（画素の値）
    int      (*lr_planes)(void);
    int      (*lr_palettes)(void);
    sgx_lr_obj_t  *(*lr_oam)(void);
    sgx_lr_ctrl_t *(*lr_ctrl)(void);
    void     (*lr_commit)(void);

    // ---- ビットマップ（モード 7、320x200、1 画素 1 バイト = 256 色のパレットの番号）----
    uint8_t *(*bm_vram)(void);                    // 表示している面（SRAM。いつ書いてもすぐ出る）
    void     (*bm_palette)(int i, uint16_t color);
    uint16_t (*bm_palette_get)(int i);
    // 2 枚の面: 後ろの面に描き、bm_present で表示の面へ写す（フレームの境目に。写すまで待つ）。ちらつかない
    uint8_t *(*bm_back)(void);                    // 後ろの面（SRAM 64KB）。取れなければ NULL。ファイルを書く操作のあと番地が変わることがある（呼び直す）
    void     (*bm_present)(void);

    // ---- テキスト面（80 桁。行はモードで 30・25）----
    unsigned (*text_cols)(void);
    unsigned (*text_rows)(void);
    void     (*text_clear)(uint32_t attr);
    unsigned (*text_put)(unsigned col, unsigned row, uint32_t codepoint, uint32_t attr);   // 使った桁（全角は 2）
    unsigned (*text_puts)(unsigned col, unsigned row, const char *utf8, uint32_t attr);
    bool     (*text_get)(unsigned col, unsigned row, uint32_t *codepoint, uint32_t *attr);
    uint16_t (*text_palette_get)(unsigned i);
    // 端末（term_write）の文字の属性とカーソル
    uint32_t (*term_attr)(void);
    void     (*term_set_attr)(uint32_t attr);
    void     (*term_locate)(unsigned x, unsigned y);
    void     (*term_cursor)(unsigned *x, unsigned *y);
    void     (*term_clear)(uint32_t attr);
    bool     (*font_glyph)(uint32_t codepoint, uint16_t rows[16], bool *half);   // フォント ROM（bit15 が左端。半角は上位 8 ビット）

    // ==== 入力 ====
    int      (*key)(bool wait);                   // 次のキー（届いた順）。無ければ -1
    void     (*key_clear)(void);
    bool     (*key_down)(int key);                // いま押されているか（USB キーボードだけ。SGX_KEY_SHIFT など）
    bool     (*keyboard)(void);                   // USB キーボードがつながっている
    void     (*mouse)(sgx_mouse_t *o);
    void     (*tablet)(sgx_tablet_t *o);
    bool     (*pad)(int i, sgx_pad_t *o);         // つながっていなければ false

    // ==== 音（PT8211 のポート。sndmgr・シーケンサー）====
    unsigned (*snd_ports)(void);
    bool     (*snd_open)(void);                   // 初めて使う前に（終わるときにファームが閉じて消音にする）
    bool     (*snd_note_on)(unsigned ch, unsigned note, unsigned velocity);
    bool     (*snd_note_off)(unsigned ch);
    bool     (*snd_volume)(unsigned ch, unsigned v);
    bool     (*snd_expression)(unsigned ch, unsigned v);
    bool     (*snd_adsr)(unsigned ch, unsigned a, unsigned d, unsigned s, unsigned r);
    bool     (*snd_wave)(unsigned ch, unsigned wave);   // SGX_WAVE_*
    bool     (*snd_bend)(unsigned ch, int bend, unsigned range);
    bool     (*snd_freq_mhz)(unsigned ch, uint32_t millihz);
    bool     (*snd_all_off)(void);
    bool     (*snd_master)(unsigned v);
    unsigned (*snd_mode)(unsigned ch);
    bool     (*snd_set_mode)(unsigned ch, unsigned mode);
    unsigned (*snd_level)(unsigned ch);
    unsigned (*snd_phase)(unsigned ch);
    // シーケンサー（SMF 0・1、SGT v1・v2）。データはファームが写して持つ。SGT は BGM（seq_play）だけ
    bool     (*seq_play)(const void *smf, uint32_t len, bool loop, const char **why);
    void     (*seq_stop)(void);
    bool     (*seq_pause)(bool pause);
    bool     (*seq_playing)(void);
    bool     (*seq_paused)(void);
    uint32_t (*seq_position_ms)(void);
    uint32_t (*seq_length_ms)(void);
    void     (*seq_tempo)(unsigned percent);
    bool     (*seq_sfx)(const void *smf, uint32_t len, unsigned ch, const char **why);
    void     (*seq_sfx_stop)(unsigned ch);
    bool     (*seq_sfx_playing)(unsigned ch);
    void     (*seq_reserve)(unsigned ch, bool on);
    // PCM を混ぜて鳴らす（最後のポートを流しで使う。BGM はそのポートを使わない）。標本はプログラムのメモリに置いたまま、鳴り終わるまで動かさない
    bool     (*pcm_open)(unsigned rate_hz, unsigned channels);   // 混ぜる速さ（11025〜48000）とチャンネル数（1〜8）
    void     (*pcm_close)(void);
    bool     (*pcm_play)(unsigned ch, const void *samples, uint32_t count, unsigned format, unsigned rate_hz, unsigned vol_l, unsigned vol_r);   // vol 0〜255
    void     (*pcm_set_volume)(unsigned ch, unsigned vol_l, unsigned vol_r);
    void     (*pcm_stop)(unsigned ch);
    bool     (*pcm_playing)(unsigned ch);

    // ==== ファイル ====
    const char *(*strerror)(int err);
    int      (*stat)(const char *path, sgx_stat_t *st);
    int      (*list)(const char *dir, sgx_dirent_fn fn, void *user);
    int      (*read_file)(const char *path, void *dst, uint32_t max, uint32_t *got);   // 丸ごと読む（max まで）
    int      (*write_file)(const char *path, const void *src, uint32_t len);            // 作る・置き換える
    int      (*remove)(const char *path);         // ファイル・空のフォルダ
    int      (*mkdir)(const char *path);
    int      (*rename)(const char *from, const char *to);
    int      (*set_attr)(const char *path, uint8_t attr);
    bool     (*writable)(const char *path);
    // 大きいファイルを好きな位置から読む（WAD など。開いている間、受け場に SRAM を 16KB 使う）
    sgx_file_t *(*ra_open)(const char *path, int *err);
    int      (*ra_read)(sgx_file_t *f, uint32_t offset, void *dst, uint32_t len);
    uint32_t (*ra_size)(sgx_file_t *f);
    void     (*ra_close)(sgx_file_t *f);
    unsigned (*drives)(sgx_drive_t *out, unsigned max);
    bool     (*storage)(int i, sgx_storage_t *o);

    // ==== 機械の状態 ====
    void     (*sys_info)(sgx_sys_info_t *o);

    // ==== タスクとプログラム（協調）====
    // タスク: 同じプログラムの中で並んで走る。優先度は 0（ふつう、入口と同じ）〜 20（低い）。スタックは PSRAM（sram なら SRAM、取れなければ PSRAM）
    sgx_task_t *(*task_create)(void (*fn)(void *arg), void *arg, uint32_t stack_bytes, unsigned prio, bool sram_stack, const char *name);
    bool     (*task_alive)(sgx_task_t *t);
    int      (*task_join)(sgx_task_t *t, uint32_t timeout_us);   // 終わるまで待つ（0 = 終わった、-1 = 時間切れ）
    void     (*task_exit)(void);                  // このタスクを終える（入口のタスクなら、プログラムが終わる）
    sgx_task_t *(*task_self)(void);
    // 排他・数・待ち行列（どのタスクからでも。待つ間は眠る）
    sgx_mutex_t *(*mutex_new)(void);
    int      (*mutex_lock)(sgx_mutex_t *m, uint32_t timeout_us);   // 0 = 取れた、-1 = 時間切れ（重ねて取れる）
    void     (*mutex_unlock)(sgx_mutex_t *m);
    void     (*mutex_free)(sgx_mutex_t *m);
    sgx_sem_t *(*sem_new)(uint32_t count, uint32_t max);
    int      (*sem_take)(sgx_sem_t *s, uint32_t timeout_us);
    void     (*sem_give)(sgx_sem_t *s);
    void     (*sem_free)(sgx_sem_t *s);
    sgx_queue_t *(*queue_new)(uint32_t item_bytes, uint32_t capacity);   // 中身は写して渡す
    int      (*queue_send)(sgx_queue_t *q, const void *item, uint32_t timeout_us);
    int      (*queue_recv)(sgx_queue_t *q, void *item, uint32_t timeout_us);
    void     (*queue_free)(sgx_queue_t *q);
    // ほかの外部実行ファイルを読み込んで走らせる（同じ画面・キー・音を分け合う）。戻り値はプログラムの番号（0 以上）か、-誤り
    int      (*spawn)(const char *path, int argc, const char *const *argv);
    bool     (*program_alive)(int id);
    int      (*program_wait)(int id, uint32_t timeout_us, int *exit_code);   // 0 = 終わった、-1 = 時間切れ
    void     (*program_stop)(int id);             // 止める頼み（そのプログラムのタスクは、待っている所・区切りで止まる）
    int      (*program_id)(void);                 // 自分の番号（ファイラーから走らせたものは 0）
    // 名前で指し示すもの（プログラムの間の受け渡し。16 個・名前は 15 バイトまで）。NULL を置けば消す
    bool     (*share_set)(const char *name, void *ptr);
    void    *(*share_get)(const char *name);

    // ==== 1.1 から（2026-10-25）====
    // 倍精度の計算（ファームの DCP = RP2350 の倍精度のコプロセッサの版）の関数の表。並びは sgx_dfuncs.inc、*count はその数。
    // ふつうは使わない（SDK の sgx_dmath.S が、コンパイラの __aeabi_d* と sqrt・sin などを自動でこの表へ向ける）
    const void *const *(*double_funcs)(unsigned *count);

    // ==== 1.2 から（2026-10-28）====
    // ラスタオペレーションリスト（表示ラインごとの背景色・ローレゾ BG の CLUT/スクロール）。アプリ時間に書く。
    // y は表示ライン 0..479。plane は 0 = 手前、1 = 奥。clut はマップのパレット番号に足す。sx/sy は画素の補正。
    void (*raster_clear)(void);
    void (*raster_backdrop)(int y, uint16_t color);                 // color は SGX の rgb() の値
    void (*raster_lowres)(int y, int plane, int clut, int sx, int sy);

    // ==== 1.3 から（2026-10-28）====
    // table（1 バイト 1 要素、128 = 0）から全表示ラインのスクロール補正を一括で書く。
    // ライン y は table[(phase + y) % n]。clut / sy はそのまま。毎フレーム phase を +1 すると波が動く
    void (*raster_wave)(int plane, const uint8_t *table, int n, int phase);

    // ==== 1.7 から（2026-10-30）====
    // ---- Socket / TLS ----
    // host は名前か IPv4 の文字列。timeout_ms は接続・読み書きの待ち（SGX_WAIT_FOREVER も可）。
    // 失敗は NULL。理由は sock_error(NULL) で取る。sock_open は TCP、sock_open_tls は TCP + TLS 1.2。
    // verify=true のときは ca_pem（PEM の CA 証明書、0 終わり）が要る。verify=false なら NULL でよい。
    // sock_start_tls は開いている TCP を TLS へ替える（成功したら s は TLS になる。失敗したら閉じている）。
    sgx_socket_t *(*sock_open)(const char *host, uint16_t port, uint32_t timeout_ms);
    sgx_socket_t *(*sock_open_tls)(const char *host, uint16_t port, bool verify, const char *ca_pem, uint32_t timeout_ms);
    int      (*sock_start_tls)(sgx_socket_t *s, const char *host, bool verify, const char *ca_pem, uint32_t timeout_ms);   // 0 = 成功、-1 = 失敗
    int      (*sock_write)(sgx_socket_t *s, const void *buf, uint32_t len, uint32_t timeout_ms);   // 送れたバイト数、-1 = 失敗
    int      (*sock_read)(sgx_socket_t *s, void *buf, uint32_t len, uint32_t timeout_ms);          // >0 = バイト数、0 = 相手が閉じた、-1 = 時間切れ、-2 = エラー
    void     (*sock_close)(sgx_socket_t *s);
    bool     (*sock_closed)(sgx_socket_t *s);
    const char *(*sock_error)(sgx_socket_t *s);   // s = NULL なら最後の open の理由
    unsigned (*sock_available)(sgx_socket_t *s);  // いま読めるバイト数（0 なら待つ）
    bool     (*sock_peer)(sgx_socket_t *s, char *ip, uint32_t n, uint16_t *port);   // ip は "a.b.c.d"

    // ---- FFT ----
    // 複素 FFT（その場で変換）。n は 2〜4096 の 2 のべき乗。inverse=true で逆変換（1/n の掛け算はしない）。
    // 戻り値: 0 = 成功、-SGX_EINVAL = n が悪い、-SGX_ENOMEM = 作業場が取れない。re/im は長さ n の float。
    int      (*fft)(float *re, float *im, uint32_t n, bool inverse);
    // 実数の入力 in[0..n-1] を FFT し、振幅 out[0..n/2] を返す（窓は掛けない。out は長さ n 以上）。
    // 戻り値は fft と同じ。
    int      (*fft_real)(const float *in, float *out, uint32_t n);

    // ==== 3.1 から（2026-11-02）====
    // メモリの使用量を o に書く（待たない。軽い）
    void     (*mem_info)(sgx_mem_info_t *o);
    // タスクの一覧を out に max 件まで書き、タスクの総数を返す（max より多いときは先頭から max 件）。
    // 並びは作った順。max = 0 なら数を数えるだけ
    unsigned (*task_info)(sgx_task_info_t *out, unsigned max);

    // ==== 3.2 から（2026-11-03）====
    // S: のディスク（FAT12、4KB クラスタ）。デフラグは 1 回の step で 4KB を 1 つ書き、
    // 終わるまで finished = false。地図・進みを描きながら、待つフレームに 1 回ずつ呼ぶ。
    uint32_t (*sysdisk_clusters)(void);              // データのクラスタ数
    uint32_t (*sysdisk_free_clusters)(void);
    uint64_t (*sysdisk_free_bytes)(void);
    // 1 クラスタ 1 バイトの地図（0 = 空き、1 = 使用）を out に書く。out は clusters バイト以上。
    // max が足りなければ false
    bool     (*sysdisk_cluster_map)(uint8_t *out, unsigned max);
    // back = true なら高いクラスタ側へ、false なら低いクラスタ側へ寄せる（フォルダは動かさない）
    bool     (*sysdisk_defrag_start_dir)(uint32_t *files, uint32_t *clusters, bool back);
    bool     (*sysdisk_defrag_step)(uint32_t *done, uint32_t *total, uint32_t *done_files, uint32_t *files, bool *finished);
    bool     (*sysdisk_enfrag_start)(uint32_t *files, uint32_t *clusters);
    bool     (*sysdisk_enfrag_step)(uint32_t *done, uint32_t *total, uint32_t *done_files, uint32_t *files, bool *finished);
    void     (*sysdisk_enfrag_seed)(uint32_t seed);
    // 空きクラスタを 0 で書く（消す）
    bool     (*sysdisk_zero_start)(uint32_t *clusters);
    bool     (*sysdisk_zero_step)(uint32_t *done, uint32_t *total, bool *finished);
    bool     (*sysdisk_work_active)(void);
    void     (*sysdisk_work_abort)(void);            // 途中でやめる

    // ==== 3.3 から（2026-11-04）====
    // 組み込みアプリの UI の英語の文を、いまの表示言語へ。訳が無ければ en をそのまま返す。
    // 戻り値はファームの小さな静的な Ring（次の呼び出しで上書きされる）。すぐ描くこと。
    const char *(*translate)(const char *en);

    // ==== 3.4 から（2026-11-04）====
    // シェルから組み込みトラッカーを開く。いまのシェルは終わり、ファイラーが tracker を開く。
    // path は .SGT / .SMF の絶対パス（NULL か空なら空の画面）。0 = 受け付けた。
    int (*request_tracker)(const char *path);
    // ==== ABI 3.5（2026-11-06）====
    // stderr へ書く（シェル sgsh から走らせたときは stderr の行き先へ。ファイラーから走らせたときは term_write と同じ画面の端末）。
    // 使う前に size でこの欄があるか確かめる（3.4 までのファームには無い）
    void (*term_write_err)(const char *s, unsigned n);
    // ==== ABI 4.0（2026-11-06）: ブロックストア ====
    int      (*store_open)(const sgx_store_config_t *cfg);   // ストアの番号（0 以上）。失敗は -SGX_E*
    int      (*store_close)(int id);                          // 書き戻してキャッシュを返す。取り出し中のブロックがあれば -SGX_EBUSY（閉じない）
    void    *(*store_get)(int id, uint32_t block);            // ブロックを取り出す（中身の番地）。失敗は NULL（理由は store_error）
    int      (*store_release)(void *p, int dirty);            // get で取り出したブロックを返す（p はブロックの中のどこでもよい）。dirty = 書いた
    int      (*store_prefetch)(int id, uint32_t block, uint32_t count);   // 先に読んでおく（取り出さない）。読めた数か -SGX_E*
    int      (*store_flush)(int id);                          // 書いたブロックをすべて書き戻す（戻ったら機器に書けている）
    int      (*store_stats)(int id, sgx_store_stats_t *out);
    int      (*store_error)(int id);                          // 最後の誤り（-SGX_E*、無ければ 0）
};

// ---- SDK のヒープ（2026-11-03）----
// malloc/free は 64KB から始まり、空きが 32KB を切ると 64KB 増える。空きが 128KB になると
// 空いた 64KB を返す。大きな作業場を先に確保したいときは sgx_heap_reserve() を呼ぶ。
// 戻り値は、いま確保してある合計（予約が足りなければ足りた分）。
uint32_t sgx_heap_reserve(uint32_t bytes);
uint32_t sgx_heap_total(void);
uint32_t sgx_heap_free_bytes(void);
uint32_t sgx_heap_min_bytes(void);

#endif

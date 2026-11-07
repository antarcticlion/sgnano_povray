# Changelog

## 2026-11-07 — povray.sgx renders on the hardware (sgnano SGX ABI 4.3)

- Stopping with ESC while the FB is on screen (`-fbview`) no longer ends in `std::terminate` (exit 134).
  The POVMS reply to `DisconnectFromBackend` in `~VirtualFrontEnd` timed out (10 s; everything is about 20x
  slower with the FB on screen) and the exception left a destructor. The call is now wrapped in
  `try`/`catch` (`vfe/vfe.cpp`), and the SGX build waits up to 60 s (`kDefaultTimeout` in
  `platform/sgx_pov/syspovconfig.h`). Hardware: `-fbview`, ESC after 120 s -> `POVRAY RESULT: stopped`,
  104 lines saved, `.partial.png` written. The patches for this and for `POV_RAND_HASHED` are now in
  `third_party/povray/patches/` and `docs/UPSTREAM_CHANGES.md`.
- New SGX front end `app/sgx/povray_sgx.cpp` (`povray.sgx SCENE.pov [opts] [-fresh] [-fbview] [-nofb]`):
  vfe session, `SgxDisplay` (RGB555), checkpoint/PNG next to the scene, status lines on the text plane.
  - While rendering it shows a 266x200 preview in screen mode 7 (SRAM, 6x7x6 colours) and switches to
    mode 2 at the end to show the 640x480 FB (sgnano ABI 4.3). With the FB on screen the SGX tasks wait
    for the FB fetch and rendering was about 20x slower. `-fbview` keeps the old mode-2 path.
  - Progress counts drawn lines separately from the lines saved in the checkpoint (4 lines at a time
    with 4x4 blocks). The PNG is written once every line is drawn, even if a checkpoint write failed.
- Threading layer `compat_sgx/boost/thread.hpp` on SGX tasks / mutexes / semaphores:
  - No exclusive load/store (LDREX/STREX, LDAEX/STLEX): they never succeed on PSRAM (the program image,
    .bss and heap). Counters are guarded by one SGX mutex.
  - Backend threads (`Task::NewBoostThread`, created with attributes) run one priority level below the
    front end, so render-block messages do not pile up in front of the vfe worker. `yield` sleeps 20 us
    (the API `yield` sleeps 1 ms). Default stack 64 KB.
- Linked with the full `libstdc++.a` / `libsupc++.a` through the SDK `sgx_link_cxx()`
  (the nano versions have no unwind tables, so no `catch` was ever reached) and `-u _scanf_float`
  (the tokenizer reads numbers with `sscanf("%lf")`).
- `POV_RAND_HASHED` (SGX only): `RandomDoubleSequence` and the warp/pattern tables compute values from
  the index (splitmix64) instead of holding 32768 doubles (256 KB each, one per Trace / MediaFunction /
  RadiosityFunction per thread). Host peak heap 2.4 MB -> 0.86 MB. The host build is unchanged.
- Default `+WT1`.
- The spare FB pages (1.8 MB) are given to the heap (`sgnano_sgx_spare_to_heap`).
- Hardware: `00_fast_sphere` 640x480 done in 291.2 s, PNG within RGB555 rounding of the host image.
- Checkpoint robustness (`app/sgnano_app.c`):
  - A failed save no longer sticks. Lines that are drawn but not saved are retried with a 1 s -> 10 s backoff
    (the pixels stay in FB page 0). Pending lines are saved in one batch per block: line data -> flush ->
    bitmap bits -> one header commit. A failed header write keeps the bits and retries only the header.
  - Stop / failure / uncaught exception: the pending lines are saved (3 tries) and `<scene>.partial.png` is written.
  - The checkpoint is removed only after the PNG is written; a failed PNG keeps it so a resume can write the PNG.
  - `sgnano_app_checkpoint_now()`, `sgnano_app_ckp_fails()`. Flush failures are recorded (`why` 6 / 7).
  - Host test `tests/test_sgx_ckp_faults.c` with fault injection in the host stub (`sgnano_sgx_host_inject`): 19/19.
- Backend threads get 128 KB stacks (the trace thread overflowed 64 KB in 02_arch_perspective).
- Hardware times for all scenes: `docs/TESTING.md`.

## 2026-11-06 — sgnano SGX ABI 4.1

- Moved to the sgnano SGX ABI 4.1 headers (`third_party/sgnano/sgx_api.h`,
  `sgx_overlay.h`). 4.1 adds the overlay functions `ovl_*` and returns the
  `fb_lend_spare` pages in ascending address order.
- Removed the workarounds that ABI 4.1 makes unnecessary:
  - `-fno-builtin` for the probe: the SDK `calloc` no longer recurses
    (the SDK now calls `memset` through a function pointer).
  - The page sort after `fb_lend_spare`.
  - Opening the block store in mode 1: the "store_open hangs in mode 2" was
    the `calloc` recursion right before `store_open` in `sgnano_app_init`.
    `povprobe` now opens the store in mode 2 (`text` as the 5th argument keeps
    the old mode-1 order).
- Hardware: `tools/hw_probe.py` PASS with the store opened in mode 2
  (`store_open_mode=2`, fresh 120 -> resume -> fresh identical, lossless PNG).

## 2026-11-06 — initial port scaffold

- Imported POV-Ray 3.7.0.10 source (AGPL-3.0-or-later) and Boost 1.62.0 headers.
- Added a host build with CMake/Ninja.
- Replaced Boost.Thread with a `std::thread` compatibility layer for the host.
- Removed Boost.DateTime uses from the parser and metadata.
- Added `SgnanoDisplay` to receive POV-Ray render blocks as RGB555.
- Added `app/sgnano_app.c` for line-by-line checkpointing and resume.
- Added `tools/run_host.py`.
- Verified host checkpoint/resume: killed at 32/480 lines, resumed, and the
  final PPM matched a fresh render byte-for-byte.
- Checkpoint format v2: two 256-byte header copies with generation numbers and
  CRC32, so a torn header write falls back to the previous generation.
- Checkpoint flushes each completed line with `fflush` + `fsync` (SGX:
  `store_flush`).
- Added 12 test scenes (vivid architectural, automotive, landscape, night city,
  underwater, space, still life, crystal, forest) plus a deliberately fast
  smoke-test scene and a 10,000-sphere slow scene.
- Received and integrated the official sgnano SGX ABI 4.0 header
  (`third_party/sgnano/sgx_api.h`). The stub now uses the official
  `store_*` types, `SGX_EBUSY`, and the official function signatures.
- Updated to the ABI 4.0 addition `fb_lend_spare`: mode 2 lends the three
  spare FB pages (about 1.8MB) for the PNG RGB scratch and future POV-Ray heap.
- Added selectable output: FB only, lossless PNG (miniz deflate level 9), or
  both. The default is now `both`.
  `tools/run_host.py --output fb|png|both`.
- Added the real-hardware SGX probe `apps/povprobe` and `tools/hw_probe.py`.
  It exercises mode 2, `fb_lend_spare`, block-store checkpoints, resume,
  lossless PNG, and the 38GB USB HDD.
- The render status now includes a progress bar; the probe also prints a
  `progress` line to UART every 16 lines.
- Store checkpoint files are rounded up to a 64KB multiple for the exFAT HDD.
- The real-hardware path now opens the block store in mode 1 (text only) and
  enters mode 2 afterwards. On the 38GB exFAT HDD, `store_open` stalls when the
  FB is displayed (2026-11-06); `storetest` succeeds in mode 1. Resumed lines
  are restored with `sgnano_app_restore_fb()`.
- The checkpoint file is pre-created with `write_file` to avoid the CREATE
  zero-fill path in `store_open`.
- `fb_lend_spare` pages are sorted by address so the contiguous 1.8MB block is
  used for the PNG scratch.
- `tools/hw_probe.py` re-detects the exFAT/NTFS drive letter before each pass
  (USB host restarts can move the 38GB HDD from B: to C:).
- Added `tools/render_all_scenes.py` (per-file time limit, summary).
  All 13 scenes rendered at 640x480 on the host in 6.2-10.2s each.
- Transferred the 15 scene files to `B:/POVTEST/scenes`, the rendered PNGs to
  `B:/POVTEST/renders`, and the logs to `B:/POVTEST/renders/logs`.
- Verified 12 rendered PNGs in the real device's filer image viewer; the
  336KB `11_slow_spheres.png` is measured with a 5-minute cap.
- Added `docs/HANDOFF_SGX_FRONTEND.md`: interface, build flags, real-device
  quirks, and the remaining frontend work for a separate agent.
- Added miniz 3.1.2 for portable PNG writing.
- Added SGX 4.0 overlay + block-store design notes and ABI proposal.
- Added an SGX stub platform (`platform/sgx/sgx4_stub.h`,
  `app/sgx/sgnano_app_sgx.c`) using the proposed block-store API.
- Added `test_sgx_checkpoint`, a host test of the SGX stub's checkpoint/store
  path (10 lines, stop, resume, complete).
- Added an ARM size-measurement build (`SGNANO_BUILD_ARM_SIZE`) and
  single-thread Boost compatibility stubs.
- Measured the ARM `source/` core at 1,407,144 B total (text 1,234,352 B,
  data 35,750 B, bss 137,042 B) with `-Os`; libc/libstdc++ not included.
- Documented all local changes to upstream in `docs/UPSTREAM_CHANGES.md`
  and `third_party/povray/patches/`.

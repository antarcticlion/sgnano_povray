# Changelog

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

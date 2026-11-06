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

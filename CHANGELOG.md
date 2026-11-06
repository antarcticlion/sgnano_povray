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
- Added SGX 4.0 overlay + block-store design notes and ABI proposal.
- Documented all local changes to upstream in `docs/UPSTREAM_CHANGES.md`
  and `third_party/povray/patches/`.

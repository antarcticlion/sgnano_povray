# vendored third-party code

This directory contains the upstream code used by the port. Do not edit these
files casually: local changes must be recorded in
[`../docs/UPSTREAM_CHANGES.md`](../docs/UPSTREAM_CHANGES.md) and in
`povray/patches/`.

## povray/

- Upstream: POV-Ray 3.7.0.10
- Tag: `v3.7.0.10`
- Commit: `cd074e4d2698b3faa5b157c6b05c5f3ad432fddc`
  (`https://github.com/POV-Ray/povray`)
- License: AGPL-3.0-or-later (`povray/COPYING`)
- Vendored directories: `source/`, `vfe/`, `unix/`, `include/`
- Local modifications:
  - `vfe/unix/syspovconfig.h`
  - `vfe/unix/unixconsole.cpp`
  - `source/backend/parser/parsestr.cpp`
  - `source/backend/parser/express.cpp`
  - `source/base/image/metadata.h`
- Patches: `povray/patches/*.patch`
- The upstream `.git` directory and the bundled external libraries
  (`libraries/`) are not included. On the host build, system libpng/libjpeg/zlib
  are used.

## boost/

- Upstream: Boost 1.62.0 headers (`boost/` directory only)
- License: Boost Software License 1.0 (`boost/LICENSE_1_0.txt`)
- Unmodified.

## sgnano/

- Official SGX ABI 4.0 header snapshot (`sgx_api.h`), copied from the sgnano
  working tree on 2026-11-06.
- License: MIT (same as the sgnano SDK).
- The header is byte-identical; see `sgnano/SHA256SUMS`. Replace it when the
  firmware ABI changes.

## miniz/

- miniz 3.1.2 (`miniz.c`, `miniz.h`, `miniz_tdef.c`, `miniz_tinfl.c`,
  `miniz_zip.h`/`miniz_zip.c`, headers, `miniz_export.h` shim).
- License: MIT (`miniz/LICENSE`).
- Used for lossless PNG output. Unmodified except the generated
  `miniz_export.h` shim (same contents as the upstream test script).

## What is not vendored

- sgnano firmware: separate project (`../sgnano`), MIT.
- POV-Ray documentation and sample scenes: not included except the standard
  include files used by the test scene.

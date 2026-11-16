# sgnano SGX SDK (vendored)

This directory contains the MIT-licensed part of the **sgnano SGX SDK** that
is needed to build `.sgx` executables for this project. The sgnano firmware
itself is **not** included; an SGX program only calls the function table that
the firmware passes to its entry point.

- Snapshot: **2026-11-16, ABI 5.11**
- Source: sgnano `sdk/sgx/`
- License: MIT. See [`LICENSE`](LICENSE).
- Checksums: [`SHA256SUMS`](SHA256SUMS) (every vendored file)

## Contents

```
sdk/sgx/
  sgx.cmake          CMake function sgx_add_program / sgx_link_cxx
  sgx_crt0.c         entry point and .sgx header
  sgx_libc.c         newlib glue, heap, exceptions, DCP dispatch
  sgx_dmath.S        double-precision wrapper
  sgx.ld             linker script (linked at address 0)
  sgx_link.py        image + relocation-table writer
  sgx_overlay.c      optional overlay support
  sgx_ovl.ld         overlay linker script
  sgx_ovl_pack.py    overlay packer
  include/
    sgx_api.h        ABI 5.11 function table and types
    sgx_overlay.h    overlay types
    sgx_progress.h   progress-box types (5.11)
    sgx_spectrum.h   spectrum helper types
    sgnano_heap.h    heap policy shared with the firmware
    sgx_dfuncs.inc   double-precision function list
```

## ABI compatibility

The ABI is append-only. A program built with 5.x runs on 5.x firmware; the
firmware also accepts 4.x programs (`SGX_ABI_COMPAT_MAJOR`). This project is
built against 5.11.

The snapshot is kept byte-identical. If the SDK changes, replace
`sdk/sgx/` from the sgnano tree and regenerate `SHA256SUMS`.

# sgnano SGX ABI

- Source: `~/Project/sgnano/sdk/sgx/include/sgx_api.h`
- Snapshot: 2026-11-06, sgnano working tree (ABI 4.1: block store, `fb_lend_spare`, overlay `ovl_*`)
- `sgx_api.h` includes `sgx_overlay.h` (the overlay types) since ABI 4.1; both files are kept byte-identical
- License: MIT (same as the sgnano SDK)
- The header is kept byte-identical. If the firmware ABI changes, replace
  this file and update the SHA-256 below.

SHA-256: see `SHA256SUMS`.

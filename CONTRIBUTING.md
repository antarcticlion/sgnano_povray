# Contributing

## Build and test

See [`docs/BUILD.md`](docs/BUILD.md). Before sending a change, run:

```sh
cmake -S . -B build_host -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build_host
python3 tools/run_host.py tests/scenes/simple.pov build_host/test.ppm \
  --width 160 --height 120
```

The checkpoint/resume test is described in [`docs/TESTING.md`](docs/TESTING.md).

## Vendored upstream code

`third_party/povray/` is POV-Ray 3.7.0.10 and `third_party/boost/` is Boost
1.62.0. Do not change vendored files without also:

1. Adding an entry to [`docs/UPSTREAM_CHANGES.md`](docs/UPSTREAM_CHANGES.md).
2. Adding or updating a patch in `third_party/povray/patches/`.
3. Rebuilding and running the host smoke test.

Boost headers are unmodified. If you need a Boost change, add a compatibility
header under `compat/` instead of editing `third_party/boost/`.

## License of contributions

- Application code: AGPL-3.0-or-later.
- `include/sgx4_api.h`: MIT, so that it can be adopted by the sgnano SDK.
- By contributing, you agree that your changes are licensed under the same
  terms as the file you change.

## Commit messages

Use a short imperative subject and explain the reason in the body when the
change is not obvious. Mention the upstream file or patch if the change is in
vendored code.

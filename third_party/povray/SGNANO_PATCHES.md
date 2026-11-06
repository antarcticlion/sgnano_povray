# sgnano_povray local patches for POV-Ray 3.7.0.10

The authoritative description is [`../../docs/UPSTREAM_CHANGES.md`](../../docs/UPSTREAM_CHANGES.md).

Patch files are in [`patches/`](patches/). Apply from the `third_party/povray/`
directory:

```sh
cd third_party/povray
for p in patches/*.patch; do patch -p1 < "$p"; done
```

Modified upstream files:

- `vfe/unix/syspovconfig.h`
- `vfe/unix/unixconsole.cpp`
- `source/backend/parser/parsestr.cpp`
- `source/backend/parser/express.cpp`
- `source/base/image/metadata.h`

All other files under `third_party/povray/` are unmodified upstream source.
The `.git` directory and bundled external libraries are not included.

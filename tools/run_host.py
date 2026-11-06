#!/usr/bin/env python3
"""ホストで POV-Ray をチェックポイント付きで走らせる。

使い方:
  tools/run_host.py tests/scenes/simple.pov build/simple.ppm [--width 640] [--height 480]
  tools/run_host.py tests/scenes/simple.pov build/simple.ppm --status
  tools/run_host.py tests/scenes/simple.pov build/simple.ppm --fresh

チェックポイントは <ppm>.ckp。途中で終了したら、次回は最初の未完成ラインから再開する。
完成したチェックポイントがあれば、POV-Ray を走らせずに PPM を書き出す。
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile
import zlib

COPY_BYTES = 256
HEADER_BYTES = 512
LINE_OFFSET = 512
MAGIC = b"SGNPCKP1"
CKP_VERSION = 2
MAX_LINES = 480
MAX_WIDTH = 1024


def fnv1a(data: bytes, seed: int = 2166136261) -> int:
    h = seed & 0xFFFFFFFF
    for b in data:
        h ^= b
        h = (h * 16777619) & 0xFFFFFFFF
    return h


def scene_hash(path: str) -> int:
    with open(path, "rb") as f:
        return fnv1a(f.read())


def options_hash(width: int, height: int, block: int) -> int:
    return fnv1a(f"povray37:W{width}:H{height}:BS{block}:A0".encode())


def parse_header_copy(b: bytes):
    if len(b) < COPY_BYTES or b[:8] != MAGIC:
        return None
    (magic, version, generation, width, height, line_bytes, block_size,
     shash, ohash, lines_done, first_missing, updated_ms) = \
        struct.unpack_from("<8s11I", b, 0)
    bitmap = b[52:52 + (MAX_LINES + 7) // 8]
    stored_crc, = struct.unpack_from("<I", b, 112)
    tmp = bytearray(b[:COPY_BYTES])
    tmp[112:116] = b"\0\0\0\0"
    if (zlib.crc32(tmp) & 0xFFFFFFFF) != stored_crc:
        return None
    return {
        "version": version, "generation": generation,
        "width": width, "height": height, "line_bytes": line_bytes,
        "block_size": block_size, "scene_hash": shash, "options_hash": ohash,
        "lines_done": lines_done, "first_missing": first_missing, "updated_ms": updated_ms,
        "bitmap": bitmap,
    }


def read_header(path: str):
    try:
        with open(path, "rb") as f:
            b = f.read(HEADER_BYTES)
    except OSError:
        return None
    if len(b) < HEADER_BYTES:
        return None
    h0 = parse_header_copy(b[0:COPY_BYTES])
    h1 = parse_header_copy(b[COPY_BYTES:2 * COPY_BYTES])
    if h0 and h1:
        return h0 if h0["generation"] >= h1["generation"] else h1
    return h0 or h1


def checkpoint_to_ppm(ckp: str, header: dict, out: str) -> bool:
    w, h, lb = header["width"], header["height"], header["line_bytes"]
    bitmap = header["bitmap"]
    with open(ckp, "rb") as f, open(out, "wb") as g:
        g.write(f"P6\n{w} {h}\n255\n".encode())
        for y in range(h):
            if not (bitmap[y >> 3] & (1 << (y & 7))):
                return False
            f.seek(LINE_OFFSET + y * lb)
            line = f.read(lb)
            if len(line) != lb:
                return False
            rgb = bytearray(w * 3)
            for x in range(w):
                v = line[2 * x] | (line[2 * x + 1] << 8)
                rgb[3 * x + 0] = (v & 31) << 3
                rgb[3 * x + 1] = ((v >> 5) & 31) << 3
                rgb[3 * x + 2] = ((v >> 10) & 31) << 3
            g.write(rgb)
    return True


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("scene")
    ap.add_argument("out")
    ap.add_argument("--width", type=int, default=640)
    ap.add_argument("--height", type=int, default=480)
    ap.add_argument("--block", type=int, default=4)
    ap.add_argument("--include", default="third_party/povray/include")
    ap.add_argument("--exe", default="build_host/povray_host")
    ap.add_argument("--fresh", action="store_true")
    ap.add_argument("--status", action="store_true")
    ap.add_argument("--no-run", action="store_true")
    ap.add_argument("--timeout", type=float, default=0,
                    help="kill the render after N seconds (for checkpoint testing)")
    args = ap.parse_args()

    ckp = args.out + ".ckp"
    if args.fresh and os.path.exists(ckp):
        os.unlink(ckp)

    hdr = read_header(ckp)
    shash = scene_hash(args.scene)
    ohash = options_hash(args.width, args.height, args.block)
    valid = (hdr is not None and hdr["version"] == CKP_VERSION and
             hdr["width"] == args.width and hdr["height"] == args.height and
             hdr["line_bytes"] == args.width * 2 and hdr["block_size"] == args.block and
             hdr["scene_hash"] == shash and hdr["options_hash"] == ohash)

    if args.status:
        if not valid:
            if os.path.exists(args.out):
                print(f"no checkpoint (render probably complete): {args.out}")
                return 0
            print(f"no valid checkpoint: {ckp}")
            return 1
        print(f"{ckp}: {hdr['lines_done']}/{hdr['height']} lines, first missing {hdr['first_missing']}")
        return 0

    if valid and hdr["lines_done"] >= hdr["height"]:
        if checkpoint_to_ppm(ckp, hdr, args.out):
            print(f"checkpoint complete: wrote {args.out} ({hdr['width']}x{hdr['height']})")
            return 0
        print("checkpoint complete but unreadable", file=sys.stderr)
        return 1

    start = hdr["first_missing"] if valid else 0
    env = os.environ.copy()
    env.update({
        "SGNANO_CHECKPOINT": os.path.abspath(ckp),
        "SGNANO_SCENE": os.path.abspath(args.scene),
        "SGNANO_IMAGE": os.path.abspath(args.out),
        "SGNANO_BLOCK_SIZE": str(args.block),
        "SGNANO_SCENE_HASH": str(shash),
        "SGNANO_OPTIONS_HASH": str(ohash),
        "SGNANO_RESUME": "1",
        "SGNANO_START_ROW": str(start),
        "SGNANO_WIDTH": str(args.width),
        "SGNANO_HEIGHT": str(args.height),
    })

    tmpdir = tempfile.mkdtemp(prefix="sgnano_pov_")
    internal = os.path.join(tmpdir, "povray_internal.ppm")
    cmd = [
        os.path.abspath(args.exe),
        "+I" + os.path.abspath(args.scene),
        "+O" + internal,
        f"+W{args.width}", f"+H{args.height}",
        "+FP", "-A", "+V",
        f"+L{os.path.abspath(args.include)}",
        f"+BS{args.block}",
    ]
    if start > 0:
        cmd.append(f"+SR{start}")

    print(f"[sgnano] scene={args.scene} {args.width}x{args.height} start={start} ckp={ckp}")
    if args.no_run:
        return 0
    try:
        r = subprocess.run(cmd, env=env, timeout=(args.timeout if args.timeout > 0 else None))
        rc = r.returncode
    except subprocess.TimeoutExpired:
        print(f"[sgnano] timeout after {args.timeout}s (checkpoint kept for resume)", file=sys.stderr)
        rc = 124
    try:
        os.unlink(internal)
    except OSError:
        pass
    try:
        os.rmdir(tmpdir)
    except OSError:
        pass
    return rc


if __name__ == "__main__":
    sys.exit(main())

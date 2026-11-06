#!/usr/bin/env python3
"""Compare the SGX probe's lossless PNG with its RGB555 raw dump.

    python3 tools/check_probe_output.py probe.raw probe.png

The probe writes the framebuffer as RGB555 (640x480, little endian) and the
same framebuffer as an 8-bit RGB PNG.  This script decodes the PNG without
external libraries (zlib + the five PNG filters) and checks every pixel
against the 5-to-8-bit expansion used by app/sgnano_app.c.
"""
import struct
import sys
import zlib


def load_png(path):
    d = open(path, "rb").read()
    if d[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit("%s: not a PNG" % path)
    pos = 8
    idat = bytearray()
    w = h = depth = ctype = interlace = None
    while pos + 12 <= len(d):
        n = struct.unpack(">I", d[pos:pos + 4])[0]
        typ = d[pos + 4:pos + 8]
        data = d[pos + 8:pos + 8 + n]
        if typ == b"IHDR":
            w, h, depth, ctype, _comp, _filt, interlace = struct.unpack(">IIBBBBB", data)
        elif typ == b"IDAT":
            idat += data
        elif typ == b"IEND":
            break
        pos += 12 + n
    if (w, h, depth, ctype, interlace) != (640, 480, 8, 2, 0):
        raise SystemExit("%s: expected 640x480 RGB8 non-interlaced, got %r" %
                         (path, (w, h, depth, ctype, interlace)))
    raw = zlib.decompress(bytes(idat))
    stride = w * 3
    out = bytearray(h * stride)
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        f = raw[p]
        p += 1
        line = bytearray(raw[p:p + stride])
        p += stride
        if f == 0:
            pass
        elif f == 1:
            for i in range(3, stride):
                line[i] = (line[i] + line[i - 3]) & 255
        elif f == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 255
        elif f == 3:
            for i in range(stride):
                a = line[i - 3] if i >= 3 else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 255
        elif f == 4:
            for i in range(stride):
                a = line[i - 3] if i >= 3 else 0
                b = prev[i]
                c = prev[i - 3] if i >= 3 else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        else:
            raise SystemExit("%s: bad filter %d at row %d" % (path, f, y))
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return w, h, bytes(out)


def expand5(v):
    return (v << 3) | (v >> 2)


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    raw_path, png_path = sys.argv[1], sys.argv[2]
    raw = open(raw_path, "rb").read()
    w, h, rgb = load_png(png_path)
    if len(raw) != w * h * 2:
        raise SystemExit("%s: expected %d raw bytes, got %d" % (raw_path, w * h * 2, len(raw)))
    bad = 0
    first = None
    worst = 0
    for i in range(w * h):
        v = raw[2 * i] | (raw[2 * i + 1] << 8)
        want = (expand5(v & 31), expand5((v >> 5) & 31), expand5((v >> 10) & 31))
        got = (rgb[3 * i], rgb[3 * i + 1], rgb[3 * i + 2])
        if got != want:
            bad += 1
            worst = max(worst, max(abs(got[k] - want[k]) for k in range(3)))
            if first is None:
                first = (i % w, i // w, want, got)
    print("png: %dx%d, %d pixels, %d mismatch%s" %
          (w, h, w * h, bad, "" if bad == 1 else "es"))
    if first:
        print("first mismatch at (%d,%d): want %r got %r" % first)
        print("worst channel difference: %d" % worst)
        return 1
    print("PNG is pixel-identical to the RGB555 raw dump (lossless): ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())

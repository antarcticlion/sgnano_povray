#!/usr/bin/env python3
"""Render every test scene with a per-file time limit.

    python3 tools/render_all_scenes.py [--timeout 300] [--width 640] [--height 480]

Each scene is started fresh.  When the limit is reached the render is stopped
and the checkpoint is kept (run_host.py --timeout does that).  The summary is
written to build_host/scenes/summary.txt.
"""
import argparse
import glob
import os
import struct
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def png_info(path):
    try:
        with open(path, "rb") as f:
            d = f.read(33)
    except OSError:
        return None
    if len(d) < 33 or d[:8] != b"\x89PNG\r\n\x1a\n" or d[12:16] != b"IHDR":
        return None
    w, h = struct.unpack(">II", d[16:24])
    return w, h, os.path.getsize(path)


def checkpoint_status(path):
    try:
        r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "run_host.py"),
                            path[0], path[1], "--status"],
                           cwd=ROOT, capture_output=True, text=True, timeout=20)
        return r.stdout.strip()
    except Exception as e:
        return "status error: %s" % e


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--timeout", type=int, default=300, help="seconds per scene")
    ap.add_argument("--width", type=int, default=640)
    ap.add_argument("--height", type=int, default=480)
    ap.add_argument("--pattern", default="tests/scenes/*.pov")
    ap.add_argument("--outdir", default="build_host/scenes")
    args = ap.parse_args()

    outdir = os.path.join(ROOT, args.outdir)
    os.makedirs(outdir, exist_ok=True)
    scenes = sorted(glob.glob(os.path.join(ROOT, args.pattern)))
    summary = []
    print("scenes: %d, limit: %d s/file, size: %dx%d" %
          (len(scenes), args.timeout, args.width, args.height), flush=True)

    for i, scene in enumerate(scenes, 1):
        name = os.path.splitext(os.path.basename(scene))[0]
        out = os.path.join(outdir, name + ".png")
        log = os.path.join(outdir, name + ".log")
        cmd = [sys.executable, os.path.join(ROOT, "tools", "run_host.py"),
               scene, out,
               "--width", str(args.width), "--height", str(args.height),
               "--output", "png", "--timeout", str(args.timeout), "--fresh"]
        t0 = time.time()
        print("[%d/%d] %s ..." % (i, len(scenes), name), flush=True)
        rc = 124
        try:
            with open(log, "wb") as f:
                r = subprocess.run(cmd, cwd=ROOT, stdout=f, stderr=subprocess.STDOUT,
                                   timeout=args.timeout + 45)
                rc = r.returncode
        except subprocess.TimeoutExpired:
            rc = 124
        dt = time.time() - t0
        if rc == 0:
            info = png_info(out)
            if info:
                status = "done"
                detail = "%dx%d %d B" % info
            else:
                status = "done-no-png"
                detail = "no PNG"
        elif rc == 124:
            status = "timeout"
            detail = checkpoint_status((scene, out))
        else:
            status = "error rc=%d" % rc
            detail = checkpoint_status((scene, out))
        line = "%s\t%s\t%.1fs\t%s" % (name, status, dt, detail)
        summary.append(line)
        print("    " + line, flush=True)

    summary_path = os.path.join(outdir, "summary.txt")
    with open(summary_path, "w", encoding="utf-8") as f:
        f.write("scene\tstatus\tseconds\tdetail\n")
        f.write("\n".join(summary) + "\n")
    print("\nsummary: " + summary_path, flush=True)
    for line in summary:
        print(line, flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())

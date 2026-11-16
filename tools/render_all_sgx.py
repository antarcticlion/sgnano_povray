#!/usr/bin/env python3
"""Render test scenes on a real sgnano device through the RBRUN protocol.

    python3 tools/render_all_sgx.py \
        --sgx B:/POVTEST/povray.sgx \
        --scene-dir B:/POVTEST/scenes \
        --width 320 --height 240 --timeout 300

The script runs one scene at a time, keeps the complete serial log in
build_hw/sgx_scenes/<scene>.log, and writes build_hw/sgx_scenes/summary.txt.
On a timeout it sends RSTOP, keeps the checkpoint, and continues with the
next scene.  Run the same scene again without -fresh to resume.

The device serial port is SGNANO_TTY (default /dev/ttyACM0), 921600 baud.
"""
import argparse
import os
import re
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PORT = os.environ.get("SGNANO_TTY", "/dev/ttyACM0")
BAUD = int(os.environ.get("SGNANO_BAUD", "921600"))
OUT = os.path.join(ROOT, "build_hw", "sgx_scenes")


def open_port():
    import serial
    return serial.Serial(PORT, BAUD, timeout=0.1)


def ping(s):
    s.reset_input_buffer()
    s.write(b"\x1ePING\n")
    end = time.time() + 1.0
    buf = b""
    while time.time() < end:
        b = s.read(256)
        if b:
            buf += b
    return b"@SG\tPONG" in buf


def rstat(s, wait=0.8):
    s.reset_input_buffer()
    s.write(b"\x1eRSTAT\n")
    end = time.time() + wait
    buf = b""
    while time.time() < end:
        b = s.read(4096)
        if b:
            buf += b
    m = re.search(rb"@SG\tRSTAT\t(\d+)", buf)
    return int(m.group(1)) if m else None


def wait_idle(s, timeout=15.0):
    end = time.time() + timeout
    while time.time() < end:
        if rstat(s) == 0:
            return True
        time.sleep(0.5)
    return False


def run_scene(s, sgx, scene, width, height, timeout):
    if not wait_idle(s):
        raise SystemExit("device is busy; stop the running program first")
    cmd = "\x1eRBRUN %s\t%s\t+W%d\t+H%d\t-fresh\n" % (sgx, scene, width, height)
    s.reset_input_buffer()
    s.write(cmd.encode("utf-8"))
    start = time.time()
    buf = bytearray()
    timed_out = False
    while True:
        b = s.read(8192)
        if b:
            buf.extend(b)
            text = buf.decode("utf-8", "replace")
            if "POVRAY RESULT" in text:
                # Let the filer/task footer arrive.
                end = time.time() + 1.5
                while time.time() < end:
                    b = s.read(8192)
                    if b:
                        buf.extend(b)
                break
        if time.time() - start > timeout:
            timed_out = True
            s.write(b"\x1eRSTOP\n")
            wait_idle(s, 20.0)
            break
        if not b:
            time.sleep(0.02)
    elapsed = time.time() - start
    text = buf.decode("utf-8", "replace")
    result = ""
    for line in text.splitlines():
        if line.startswith("POVRAY RESULT:"):
            result = line.strip()
    return {
        "timed_out": timed_out,
        "seconds": elapsed,
        "result": result,
        "log": text,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sgx", default="B:/POVTEST/povray.sgx")
    ap.add_argument("--scene-dir", default="B:/POVTEST/scenes")
    ap.add_argument("--width", type=int, default=320)
    ap.add_argument("--height", type=int, default=240)
    ap.add_argument("--timeout", type=float, default=300.0)
    ap.add_argument("--only", default="", help="comma-separated scene names")
    args = ap.parse_args()

    os.makedirs(OUT, exist_ok=True)
    try:
        s = open_port()
    except Exception as e:
        raise SystemExit("cannot open %s: %s" % (PORT, e))
    if not ping(s):
        raise SystemExit("no PING response on %s" % PORT)

    names = []
    for name in sorted(os.listdir(os.path.join(ROOT, "tests", "scenes"))):
        if name.endswith(".pov"):
            names.append(name)
    if args.only:
        wanted = {x.strip() for x in args.only.split(",") if x.strip()}
        names = [n for n in names if n in wanted or n[:-4] in wanted]

    print("device: %s  scenes: %d  %dx%d  timeout %.0fs"
          % (PORT, len(names), args.width, args.height, args.timeout))
    rows = []
    for name in names:
        scene = args.scene_dir.rstrip("/") + "/" + name
        print("=== %s" % name, flush=True)
        r = run_scene(s, args.sgx, scene, args.width, args.height, args.timeout)
        log_path = os.path.join(OUT, name[:-4] + ".log")
        with open(log_path, "w", encoding="utf-8") as f:
            f.write(r["log"])
        if r["timed_out"]:
            status = "timeout"
        elif " done " in r["result"]:
            status = "done"
        else:
            status = "failed"
        detail = r["result"] or ("stopped after %.0fs (checkpoint kept)" % r["seconds"])
        print("    %-7s %7.1fs  %s" % (status, r["seconds"], detail), flush=True)
        rows.append((name, status, r["seconds"], detail))

    summary = os.path.join(OUT, "summary.txt")
    with open(summary, "w", encoding="utf-8") as f:
        f.write("scene\tstatus\tseconds\tdetail\n")
        for name, status, secs, detail in rows:
            f.write("%s\t%s\t%.1f\t%s\n" % (name[:-4], status, secs, detail))
    print("summary: %s" % summary)
    s.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

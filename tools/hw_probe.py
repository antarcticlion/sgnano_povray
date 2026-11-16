#!/usr/bin/env python3
"""Run the SGX hardware probe on a real sgnano and check the results.

    python3 tools/hw_probe.py [--dir B:/POVTEST] [--local build_hw]

It uploads build_sgx/sgx/povprobe.sgx, then runs three passes:

  1. fresh, stop after 120 lines  -> a checkpoint must remain
  2. resume, render the rest      -> complete, PNG + raw written
  3. fresh, render all 480 lines  -> complete, PNG + raw written

The resume and fresh outputs are downloaded and compared byte for byte, and
the PNG is decoded and compared with the RGB555 raw dump.  The report also
shows the USB storage capacity, so the 20GB+ drive requirement is checked.
"""
import argparse
import os
import re
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# The ZMODEM upload/download helper lives in the sgnano repository.  The
# public sgnano_povray tree does not vendor it, so point SGNANO_ROOT at a
# sgnano checkout when you run the hardware probe.
SGNANO = os.environ.get("SGNANO_ROOT", "")
PORT = os.environ.get("SGNANO_TTY", "/dev/ttyACM0")
BAUD = int(os.environ.get("SGNANO_BAUD", "921600"))
PROBE = os.path.join(ROOT, "build_sgx", "sgx", "povprobe.sgx")
ZM = os.path.join(SGNANO, "scripts", "zm.py") if SGNANO else ""
CHECK = os.path.join(ROOT, "tools", "check_probe_output.py")


def serial_port():
    import serial
    return serial.Serial(PORT, BAUD, timeout=0.1, xonxoff=False, rtscts=False, dsrdtr=False)


def ping(s=None, tries=20, delay=0.4):
    own = s is None
    if own:
        s = serial_port()
    try:
        for _ in range(tries):
            s.reset_input_buffer()
            s.write(b"\x1ePING\n")
            end = time.time() + 0.5
            buf = b""
            while time.time() < end:
                b = s.read(256)
                if b:
                    buf += b
            if b"@SG\tPONG" in buf:
                return True
            time.sleep(delay)
        return False
    finally:
        if own:
            s.close()


def remote(cmd, wait=2.0):
    with serial_port() as s:
        s.reset_input_buffer()
        s.write(b"\x1e" + cmd.encode("utf-8") + b"\n")
        end = time.time() + wait
        buf = b""
        while time.time() < end:
            b = s.read(8192)
            if b:
                buf += b
        return buf.decode("utf-8", "replace")


def wait_idle(timeout=8.0):
    """Make sure no previous external program is still marked running."""
    end = time.time() + timeout
    while time.time() < end:
        t = remote("RSTAT", 1.0)
        m = re.search(r"@SG\tRSTAT\t(\d+)", t)
        if m and m.group(1) == "0":
            return True
        remote("RSTOP", 1.0)
        time.sleep(0.5)
    return False


def run_phase(remote_prog, args, timeout):
    """Start .sgx with RBRUN and wait until RSTAT says it is no longer running."""
    if not wait_idle():
        raise RuntimeError("the device is still running a previous program")
    line = "\x1eRBRUN %s\t%s\n" % (remote_prog, "\t".join(args))
    with serial_port() as s:
        s.reset_input_buffer()
        s.write(line.encode("utf-8"))
        start = time.time()
        buf = b""
        started = False
        last_stat = 0.0
        while time.time() - start < timeout:
            b = s.read(8192)
            if b:
                buf += b
            if not started and b"@SG\tOK\tRBRUN" in buf:
                started = True
            if b"@SG\tERR" in buf and b"@SG\tOK\tRBRUN" not in buf:
                raise RuntimeError("RBRUN refused: %s" % buf.decode("utf-8", "replace")[-400:])
            if started and time.time() - last_stat > 1.0:
                s.write(b"\x1eRSTAT\n")
                last_stat = time.time()
            if started:
                # 最後の RSTAT を見る（最初の 1 を拾い続けない）。
                # 応答は速いので「送信直後は無視」はしない。
                ms = re.findall(rb"@SG\tRSTAT\t(\d+)", buf)
                if ms and ms[-1] == b"0":
                    time.sleep(0.3)
                    buf += s.read(65536)
                    return buf.decode("utf-8", "replace")
        raise TimeoutError("probe did not finish in %d s: %s" % (timeout, buf.decode("utf-8", "replace")[-800:]))


def _require_zm():
    if not SGNANO or not os.path.exists(ZM):
        raise SystemExit(
            "SGNANO_ROOT is not set to a sgnano checkout with scripts/zm.py.\n"
            "Example: SGNANO_ROOT=$HOME/Project/sgnano python3 tools/hw_probe.py"
        )


def zm_put(remote_dir, local_file):
    _require_zm()
    return subprocess.run([sys.executable, ZM, "put", "--remote-dir", remote_dir, local_file],
                          check=True, cwd=SGNANO).returncode


def zm_get(remote_file, outdir):
    _require_zm()
    return subprocess.run([sys.executable, ZM, "get", "--remote-file", remote_file, outdir],
                          check=True, cwd=SGNANO).returncode


def parse_drives(text):
    out = []
    for line in text.splitlines():
        f = line.split("\t")
        if len(f) >= 5 and f[0] == "@SG" and f[1] == "DRV":
            try:
                out.append((f[2], int(f[3]), int(f[4])))
            except ValueError:
                pass
    return out


def ensure_dir(preferred=None):
    """Find a writable POVTEST folder on an exFAT/NTFS drive (the 20GB+ HDD).

    Drive letters can move when the USB host stack restarts (observed on the
    real device), so the folder is looked up again before every pass.
    """
    candidates = []
    if preferred:
        candidates.append(preferred)
    drives = parse_drives(remote("DRIVES", 2.0))
    for letter, typ, ready in drives:
        if ready and typ in (3, 4):
            d = letter + ":/POVTEST"
            if d not in candidates:
                candidates.append(d)
    for d in candidates:
        st = remote("STAT " + d, 2.0)
        if "@SG\tSTAT\tD" in st:
            return d
        mk = remote("MKDIR " + d, 4.0)
        if "@SG\tOK\tMKDIR" in mk:
            return d
    raise SystemExit("no writable exFAT/NTFS POVTEST folder (drives=%r)" % (drives,))


def parse_report(path):
    values = {}
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        text = f.read()
    for line in text.splitlines():
        for m in re.finditer(r"(\w+)=([^\s]+)", line):
            values[m.group(1)] = m.group(2)
    return text, values


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", default=None,
                    help="remote writable folder (default: auto-detect an exFAT/NTFS POVTEST folder)")
    ap.add_argument("--local", default=os.path.join(ROOT, "build_hw"), help="local download folder")
    ap.add_argument("--skip-upload", action="store_true")
    ap.add_argument("--timeout", type=int, default=300)
    args = ap.parse_args()

    local = os.path.abspath(args.local)
    os.makedirs(local, exist_ok=True)
    if not os.path.isfile(PROBE):
        raise SystemExit("missing %s (build it with -DSGNANO_BUILD_SGX_PROBE=ON)" % PROBE)

    if not ping():
        raise SystemExit("device does not answer PING on %s" % PORT)
    rdir = ensure_dir(args.dir.rstrip("/") if args.dir else None)
    print("remote folder:", rdir)
    if not args.skip_upload:
        print("== upload ==")
        zm_put(rdir, PROBE)
    rprog = rdir + "/povprobe.sgx"
    if "@SG\tSTAT\tF" not in remote("STAT " + rprog, 2.0):
        raise SystemExit("probe is not on the device: " + rprog)

    def refresh():
        """Re-check the folder before a pass (USB restarts can move letters)."""
        nonlocal rdir, rprog
        new = ensure_dir(rdir)
        if new != rdir:
            print("drive letter moved: %s -> %s" % (rdir, new))
            rdir = new
        rprog = rdir + "/povprobe.sgx"
        if "@SG\tSTAT\tF" not in remote("STAT " + rprog, 2.0):
            print("re-uploading the probe to", rdir)
            zm_put(rdir, PROBE)

    print("== pass 1: fresh, stop after 120 lines ==")
    log1 = run_phase(rprog, [rdir, "fresh", "120"], args.timeout)
    print(log1[-3000:])
    if "RESULT: PAUSED" not in log1:
        raise SystemExit("pass 1 did not pause")
    ckp = remote("STAT " + rdir + "/probe.ckp", 2.0)
    print(ckp.strip())
    if "@SG\tSTAT\tF" not in ckp:
        raise SystemExit("pass 1 did not leave a checkpoint")

    refresh()
    print("== pass 2: resume to the end ==")
    log2 = run_phase(rprog, [rdir, "resume"], args.timeout)
    print(log2[-4000:])
    if "RESULT: PASS" not in log2:
        raise SystemExit("pass 2 did not finish")
    rdir2 = rdir

    refresh()
    print("== pass 3: fresh, full render ==")
    log3 = run_phase(rprog, [rdir, "fresh"], args.timeout)
    print(log3[-4000:])
    if "RESULT: PASS" not in log3:
        raise SystemExit("pass 3 did not finish")
    rdir3 = rdir

    print("== download ==")
    for name in ("probe_resume.txt", "probe_resume.png", "probe_resume.raw"):
        zm_get(rdir2 + "/" + name, local)
    for name in ("probe_fresh.txt", "probe_fresh.png", "probe_fresh.raw"):
        zm_get(rdir3 + "/" + name, local)

    print("== checks ==")
    txt_r, rep_r = parse_report(os.path.join(local, "probe_resume.txt"))
    txt_f, rep_f = parse_report(os.path.join(local, "probe_fresh.txt"))
    failures = []
    if rep_r.get("fb_bad_rows") != "0" or rep_r.get("fb_bad_px") != "0":
        failures.append("resume FB verify: %s bad rows / %s bad pixels" %
                        (rep_r.get("fb_bad_rows"), rep_r.get("fb_bad_px")))
    if rep_f.get("fb_bad_rows") != "0" or rep_f.get("fb_bad_px") != "0":
        failures.append("fresh FB verify: %s bad rows / %s bad pixels" %
                        (rep_f.get("fb_bad_rows"), rep_f.get("fb_bad_px")))
    if rep_r.get("start_row") != "120":
        failures.append("resume start_row=%s (want 120)" % rep_r.get("start_row"))
    if rep_f.get("start_row") != "0":
        failures.append("fresh start_row=%s (want 0)" % rep_f.get("start_row"))
    if rep_r.get("fb_crc") != rep_f.get("fb_crc"):
        failures.append("FB CRC differs: resume %s fresh %s" % (rep_r.get("fb_crc"), rep_f.get("fb_crc")))
    if rep_r.get("png_crc") != rep_f.get("png_crc"):
        failures.append("PNG CRC differs: resume %s fresh %s" % (rep_r.get("png_crc"), rep_f.get("png_crc")))
    if rep_r.get("raw_crc") != rep_f.get("raw_crc"):
        failures.append("raw CRC differs: resume %s fresh %s" % (rep_r.get("raw_crc"), rep_f.get("raw_crc")))
    spare = int(rep_r.get("spare_bytes", "0"))
    if spare < 1800000:
        failures.append("fb_lend_spare returned only %d bytes" % spare)
    if rep_r.get("scratch_on_spare") != "1":
        failures.append("PNG scratch did not use the lent FB pages")
    if rep_r.get("complete") != "1" or rep_f.get("complete") != "1":
        failures.append("a completed pass is not marked complete")
    mbs = [int(m) for m in re.findall(r"mb=(\d+)", txt_r + txt_f)]
    biggest = max(mbs) if mbs else 0
    print("USB storage capacities (MB): %s" % (mbs or "none"))
    if biggest < 20000:
        failures.append("no 20GB+ USB storage reported (largest %d MB)" % biggest)

    png_r = open(os.path.join(local, "probe_resume.png"), "rb").read()
    png_f = open(os.path.join(local, "probe_fresh.png"), "rb").read()
    raw_r = open(os.path.join(local, "probe_resume.raw"), "rb").read()
    raw_f = open(os.path.join(local, "probe_fresh.raw"), "rb").read()
    print("resume PNG %d B, fresh PNG %d B, identical=%s" % (len(png_r), len(png_f), png_r == png_f))
    print("resume RAW %d B, fresh RAW %d B, identical=%s" % (len(raw_r), len(raw_f), raw_r == raw_f))
    if png_r != png_f:
        failures.append("resume and fresh PNGs differ")
    if raw_r != raw_f:
        failures.append("resume and fresh raw dumps differ")

    for tag in ("resume", "fresh"):
        png = os.path.join(local, "probe_%s.png" % tag)
        raw = os.path.join(local, "probe_%s.raw" % tag)
        r = subprocess.run([sys.executable, CHECK, raw, png])
        if r.returncode != 0:
            failures.append("%s PNG is not lossless vs raw" % tag)

    if failures:
        print("\nFAIL")
        for f in failures:
            print("  - " + f)
        return 1
    print("\nPASS: 20GB+ drive, checkpoint/resume, fb_lend_spare, lossless PNG")
    return 0


if __name__ == "__main__":
    sys.exit(main())

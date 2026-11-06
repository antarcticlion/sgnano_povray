#!/usr/bin/env python3
"""Sum the ARM object sizes built by the povray_arm_size target."""
import os
import re
import subprocess
import sys

BUILD = "build_arm"
OBJDIR = os.path.join(BUILD, "CMakeFiles", "povray_arm_size.dir")


def main() -> int:
    if not os.path.isdir(OBJDIR):
        print(f"no object directory: {OBJDIR}", file=sys.stderr)
        return 1
    objs = []
    for root, _, files in os.walk(OBJDIR):
        for f in files:
            if f.endswith((".o", ".obj")):
                objs.append(os.path.join(root, f))
    if not objs:
        print("no object files", file=sys.stderr)
        return 1
    objs.sort()
    cmd = ["arm-none-eabi-size"] + objs
    out = subprocess.check_output(cmd, text=True)
    rows = []
    for line in out.splitlines()[1:]:
        m = re.match(r"\s*(\d+)\s+(\d+)\s+(\d+)\s+\d+\s+([0-9a-f]+)\s+(.+)", line)
        if m:
            text, data, bss, _hex, name = m.groups()
            rows.append((int(text), int(data), int(bss), name))
    if not rows:
        print("could not parse size output", file=sys.stderr)
        return 1
    total_text = sum(r[0] for r in rows)
    total_data = sum(r[1] for r in rows)
    total_bss = sum(r[2] for r in rows)
    print(f"objects: {len(rows)}")
    print(f"text:    {total_text:>10,} B  ({total_text/1024:.1f} KiB)")
    print(f"data:    {total_data:>10,} B  ({total_data/1024:.1f} KiB)")
    print(f"bss:     {total_bss:>10,} B  ({total_bss/1024:.1f} KiB)")
    print(f"total:   {total_text+total_data+total_bss:>10,} B")
    print("\nlargest objects:")
    for text, data, bss, name in sorted(rows, reverse=True)[:20]:
        print(f"{text:>8,}  {data:>6,}  {bss:>6,}  {name}")
    with open(os.path.join(BUILD, "arm_size.txt"), "w") as f:
        f.write(out)
    return 0


if __name__ == "__main__":
    sys.exit(main())

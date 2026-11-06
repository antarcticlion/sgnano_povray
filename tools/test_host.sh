#!/bin/sh
# ホストのビルド＋スモークテスト。
set -eu
cd "$(dirname "$0")/.."

cmake -S . -B build_host -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build_host

./build_host/test_sgx_checkpoint

rm -f build_host/smoke.ppm build_host/smoke.ppm.ckp
python3 tools/run_host.py tests/scenes/simple.pov build_host/smoke.ppm \
    --width 160 --height 120

test -s build_host/smoke.ppm
echo "host tests: ok"

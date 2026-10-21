#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Run firmware/src/witchbox.c on this computer (tests/host.c): build/host/witchbox.wav and the screens as PNG.
#   tests/run_host.sh [script.txt]      (default: tests/demo.txt)
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
cc -std=c99 -O2 -Wall -Wno-unused-function -Wno-unused-variable -DDINGHY_HOST -DAPP_VER="\"${APP_VER:-dev}\"" \
   -Ifirmware/src -Ifirmware/gen -o build/host/witchbox firmware/src/witchbox.c
build/host/witchbox build/host/witchbox "${1:-tests/demo.txt}"
python3 tools/ppm2png.py build/host

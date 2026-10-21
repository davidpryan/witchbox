#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Fetch the three JieLi AC79 SDK files the package needs (Apache-2.0, from the public SDK on gitee) into a
# directory laid out as the build expects, and check them against the hashes in tools/build.py.
#   tools/get_sdk.sh [DEST]      (default: ~/fw-AC79_AIoT_SDK)   then: export AC79_SDK=DEST
set -e
DEST="${1:-$HOME/fw-AC79_AIoT_SDK}"
TAG=AC79NN_SDK_V1.2.1_2023-12-13
BASE="https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK/raw/$TAG/cpu/wl82/tools"
mkdir -p "$DEST/cpu/wl82/tools/cfg"
for f in uboot.boot cfg_tool.bin cfg/eq_cfg_hw.bin; do
    curl -fsSL --retry 3 -o "$DEST/cpu/wl82/tools/$f" "$BASE/$f"
done
python3 - "$DEST" <<'PY'
import hashlib, re, sys
from pathlib import Path
dest = Path(sys.argv[1]) / "cpu" / "wl82" / "tools"
table = dict(re.findall(r'"([^"]+)": "([0-9a-f]{64})"', Path("tools/build.py").read_text()))
bad = [f for f, h in table.items() if hashlib.sha256((dest / f).read_bytes()).hexdigest() != h]
if bad:
    sys.exit("get_sdk: hash mismatch for " + ", ".join(bad))
print("SDK files ok in", dest)
PY
echo "AC79_SDK=$DEST"

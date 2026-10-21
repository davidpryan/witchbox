# Licensing

WitchBox is free software under the GNU General Public License, version 3 only (`GPL-3.0-only`, full text in
`LICENSE`). You may use, study, change and share it. If you give someone a firmware built from it, give them its
complete source under the same licence too.

## Who made what

| | |
| --- | --- |
| The platform: `firmware/hal/`, `firmware/loader/`, `crt0.S`, `app.ld`, `firmware/src/` (`usb.c`, `ota.c`, `recovery.c`, `lcd.c`, `gfx.c`, `libc.c`, `bootguard.h`, `uac_fir.h`), `tools/build.py`, `tools/fm1pkg_make.py`, `tools/lz4blk.py`, `tools/fm1_install.py`, `tools/get_toolchain.sh` | Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments ([Felucca](https://github.com/hugelton/Felucca)), with the changes of [SLOOP](https://github.com/isod89/sloop-fm1) (isod89) |
| The wiring `firmware/src/platform.c`, the host mode `tests/host.c`, `docs/PLATFORM-API.md`, `docs/PLATFORM-RULES.md` | zvenson ([Dinghy](https://github.com/zvenson/dinghy)); small changes here (the project flash slots, the host script commands) |
| WitchBox: `firmware/src/witchbox.c`, everything under `firmware/src/wb/`, `tests/demo.txt`, `tests/run_host.sh`, `tests/check_sources.sh`, `tools/get_sdk.sh`, `tools/ppm2png.py`, `tools/wavcheck.py`, `.github/`, `docs/CONCEPT.md`, `docs/MANUAL.md`, `docs/RELEASING.md`, `README.md` | 2026 WitchBox contributors |

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| Terminus font 8x16 (ter-u16n), rendered into `firmware/gen/felucca_font.h` | SIL OFL 1.1 | `assets/fonts/Terminus-LICENSE.txt` |
| JieLi AC79 SDK: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` are fetched from the public SDK at build time (`tools/get_sdk.sh`) and placed in the package; no SDK files are in this tree | Apache-2.0 | <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK> |

## Trademarks

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments; "M-VAVE" and "FM-1" belong to their
owners. WitchBox is not made or endorsed by them. "Burial" is the name of an artist; WitchBox is a tribute to a
sound, not affiliated with anyone.

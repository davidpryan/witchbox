# Releasing

1. Run the host checks: `tests/run_host.sh && python3 tools/wavcheck.py build/host/witchbox.wav && tests/check_sources.sh`.
2. Build for the FM-1 with `./build.sh` (or let CI do it) and read what it prints: the image size, the RAM, the
   pool. It fails on what would brick the FM-1.
3. Tag: `git tag v0.1 && git push --tags`. The `build` workflow builds `witchbox-0.1.fwsc` with
   `./build.sh --release 0.1` and attaches it to a GitHub release.

## The package identity

Every FM-1 package carries an identity like `FM-1_950`; the FM-1 reports the one it runs and the installers and
the FM-1 Firmware Hub tell firmwares apart by it. WitchBox uses `FM-1_95N` (N = the minor version, set by
`--release X.N`; `FM-1_950` for a dev build). Taken by others: 900 SLOOP, 91N Felucca, 93N sloopDX, 97N zp12,
98N a Doom port, 99N Dinghy. There is no registry: check the hub's list before the first public release and
change `PRODUCT` in `tools/build.py` if 95N is taken by then.

## Into the FM-1 Firmware Hub

The hub installs open-source FM-1 firmwares with one click and checks nightly for releases. It wants: public
GPL-3.0 source (yes), releases on GitHub with the `.fwsc` attached (the workflow does that), a screenshot
(`tests/run_host.sh` makes them in `build/host/`), and a request through the hub's "Made an open-source
firmware?" card.

## Before anyone flashes it

WitchBox has not been run on an FM-1 yet. The first hardware test should:

- back up the FM-1 with the hub or `tools/fm1_install.py --info` first;
- know the rescue: hold **OCT-** while switching on gives the USB rescue (only the updater); two crashes at
  boot go there by themselves;
- watch `wb_pos.isr_us` (add it to the HOME page for the test) against the 5800 µs budget with everything
  playing: six engines, the plate, the echo and a SUMMONed DROP pattern are the worst case;
- try a project save (SONG page) and a power cycle: the preferences and the slot must come back.

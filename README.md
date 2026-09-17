# CH57x wired mini keyboard (`514c:8851`)

Reverse-engineered protocol notes plus a dependency-free **C++17 CLI** that rebinds the
keys, lights the LEDs, reads the stored map back, and watches presses live on this
3-key USB macro keypad (sold as a "Kuxuan / SIFESION 1051 PRO", Hangkai branded,
WCH **CH57x** RISC-V inside).

## Why

This started from a cheap mini keyboard off AliExpress: three buttons, programmable, and
the only way to configure it is the vendor's Windows GUI. That is not a great fit for a
Linux desktop. So this is an experiment in reverse-engineering the protocol and shipping a
plain Linux **C++17 command-line tool** for it, done with pi coding agent and a **local
model only**. The host, agent, extension and MCP setup behind that workflow is described in
[`doc/environment.md`](doc/environment.md); the protocol findings are in
[`doc/documentation.md`](doc/documentation.md).

| | |
|---|---|
| **Works** | `upload` `read` `watch` `led` — all verified on hardware |
| **Interface** | libusb on the vendor HID channel (`/dev/hidraw`), no kernel driver changes |
| **Deps** | libusb-1.0, CMake — no Boost, no Rust toolchain |
| **Reference** | byte-for-byte compatible with [`kriomant/ch57x-keyboard-tool`](https://github.com/kriomant/ch57x-keyboard-tool) (MIT), whose `k884x` driver this ports |
| **Also documented** | the vendor Windows GUI (`MINI_KEYBOARD.exe`) + its Qt build tree served as the RE cross-check — binaries are not committed, [`vendor/README.md`](vendor/README.md) gives the download URL and checksums, findings are in `doc/documentation.md` |

## Quick start

```bash
# build
sudo apt install -y libusb-1.0-0-dev cmake g++ make
cmake -S . -B build && cmake --build build -j

# allow non-root device access (or use sudo below)
sudo cp udev/99-ch57x.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger

# what is bound right now?
./build/ch57x-keyboard-tool read

# rebind the three keys to media transport controls
cat > config.yaml <<'YAML'
orientation: normal     # left → right
rows: 1
columns: 3
knobs: 0
layers:
  - buttons:
      - [previous, play, next]
YAML

./build/ch57x-keyboard-tool validate config.yaml   # offline sanity check
sudo ./build/ch57x-keyboard-tool upload config.yaml
./build/ch57x-keyboard-tool watch config.yaml      # confirm each press
```

Everything is written to the keyboard's own flash — the OS never sees a driver, so the
mapping follows the board to any machine.

## Documentation

| Document | What is in it |
|---|---|
| [`doc/documentation.md`](doc/documentation.md) | **Technical reference** — USB topology, vendor protocol (`0xFB` identify, `0xFA` read-back, `0xFE` bind/LED, `0xAA`/`0xFD` commit), record and key-id layouts, LED encoding, capture format, vendor-tool RE, firmware variants, open problems |
| [`doc/examples.md`](doc/examples.md) | **CLI how-to** — media buttons, LEDs, text/mouse/macros, multi-layer configs, watch, capture + offline decode, permissions, dry runs |
| [`doc/environment.md`](doc/environment.md) | **How this was built** — host hardware, toolchain versions, pi coding agent + local model, extensions, Ghidra / clangd MCP servers |
| [`include/ch57x/`](include/ch57x/) + [`src/`](src/) | the port itself — each header is annotated with the upstream Rust file/routine it mirrors (`config.rs`, `k884x.rs`, `usb.rs`, `main.rs`) and records deliberate deviations |

## Verification

```bash
ctest --test-dir build          # ctest harness
./build/ch57x-selftest          # 171 offline checks (no USB)
./build/ch57x-keyboard-tool dump config.yaml | diff -u expected_dump.txt -
```

`dump`, `validate`, `show-keys` and `decode` need no device and no root, so the whole
encoding path is testable anywhere. The wire protocol itself is confirmed against real
hardware: a fresh board types `a`/`b`/`c`, and `read` decodes exactly those bindings
from the `0xFA` stream.

## Repository layout

Standard CMake layout: sources in `src/`, public headers in `include/ch57x/`, test data
in `testdata/`, out-of-tree build in `build/`.

```
README.md                     this file
doc/documentation.md          protocol / hardware reference
doc/examples.md               CLI usage examples
doc/environment.md            dev machine + agent/MCP setup used to build this
CMakeLists.txt                build + selftest wiring (needs only libusb-1.0)
include/ch57x/*.h             library headers (codes, parser, config, keyboard, usb, capture)
src/*.cpp                     implementation + main.cpp CLI + selftest.cpp
testdata/                     reference wire capture used by `decode`
config.yaml                   example mapping (previous / play / next)
expected_dump.txt             golden `dump config.yaml` output (ctest: dump-golden)
udev/99-ch57x.rules           udev rules for non-root access
vendor/README.md              how to re-download the vendor Windows tool + its inventory
                              (the 102 MB of vendor binaries are not committed)
LICENSE                       MIT (upstream notice retained for the ported code)
```

## Known limitations

- `read` currently loses the last 11 of 72 stored records: the firmware stops answering
  `0xFA` requests mid-stream (reproducible from the bundled capture). The tool reports
  the truncation instead of hanging. See
  [open problems](doc/documentation.md#open-problems).
- LED settings are CLI-only — `config.yaml` has no `led:` section, so `upload` does not
  touch lighting.
- `ch57x-2` / `ch57x-3` models are recognised but their drivers are not ported.
- Layer switching on this 3-key board is not understood yet (the stored map has L1–L3).

## Credits

- [kriomant/ch57x-keyboard-tool](https://github.com/kriomant/ch57x-keyboard-tool) — MIT,
  the reference implementation (Rust); `514c:8851` support landed on `master` in July 2026
  (PR #188) and is not in any release tag yet, which is why this port exists.
- [jvspier/macropad-rebind](https://github.com/jvspier/macropad-rebind) — WebHID GUI with
  read-back and LED control for the same board.
- The vendor's `MINI_KEYBOARD.exe` and its shipped MinGW object files.

## License

MIT — see [LICENSE](LICENSE).

The C++ code is a port of
[kriomant/ch57x-keyboard-tool](https://github.com/kriomant/ch57x-keyboard-tool) (MIT,
Copyright (c) 2023 Mikhail Trishchenkov); that notice is kept in `LICENSE` and must
survive in derivative works.

**Not** under this license: the vendor Windows software described in
[`vendor/README.md`](vendor/README.md) is proprietary and is not redistributed here —
only its download URL, checksums and the interoperability findings derived from it are.

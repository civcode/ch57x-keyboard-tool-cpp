# Development environment

The machine, toolchain and agent setup this port was built on, so the work is
reproducible. Nothing here is required to *build* the tool — see
[README → Quick start](../README.md#quick-start); only g++, CMake and libusb-1.0 are.

## Host

| | |
|---|---|
| Board / BIOS | ASUS PRIME X870-P WIFI (ASUSTeK), BIOS 0831 |
| CPU | AMD Ryzen 9 9950X, 16 cores / 32 threads |
| RAM | 64 GiB (60.4 GiB visible) |
| GPU | **ASUS GeForce RTX 4070 Ti SUPER** — NVIDIA AD103, PCI `01:00.0`, 16 GiB GDDR6X (256-bit, 672 GB/s), NVIDIA kernel module 580.173.02 — does the local model inference |
| iGPU | AMD/ATI `13c0` (Ryzen 9 9950X), `amdgpu`, drives the monitors (`card1`); the RTX 4070 Ti SUPER is headless (`card2`) |
| OS | Ubuntu 24.04.5 LTS (Noble Numbat), kernel `7.0.0-31-generic`, x86_64 |
| Shell / session | bash under tmux; work tree `/home/chris/tmp/wired-mini-keyboard` (also `$HOME`) |

The GPU is in the inference path only — building and testing the tool is pure CPU work
(`nproc`-scale g++ + a handful of libusb transfers). The quant sizes and context lengths
configured for the agent are chosen to fit the 16 GiB of VRAM.

Memory spec per [NVIDIA's RTX 4070 family datasheet](https://www.nvidia.com/en-us/geforce/graphics-cards/40-series/rtx-4070-family/);
the PCI identity, driver binding and NVRM version were read from `lspci`,
`/sys/bus/pci/devices/0000:01:00.0/driver` and `/proc/driver/nvidia/version`.
`nvidia-smi` reports "couldn't communicate with the NVIDIA driver" from inside the
sandboxed agent shell — the `/dev/nvidia*` nodes are not exposed there, which is a
sandbox artefact, not a missing or unloaded driver.

## Build toolchain

| Component | Version |
|---|---|
| `g++` | 13.3.0 (Ubuntu 13.3.0-6ubuntu2~24.04.1) |
| CMake | 3.28.3 |
| libusb-1.0 (dev) | 1.0.27, via `pkg-config` |
| GNU Make, pkg-config, ctest | distro defaults |

```bash
cmake -S . -B build && cmake --build build -j     # C++17, -Wall -Wextra, Release
ctest --test-dir build                            # selftest + dump-golden
```

## Target hardware

`514c:8851` macro keypad (WCH CH57x RISC-V) on a USB-A port, Full Speed. See
[documentation.md → Device identity](documentation.md#device-identity).

USB I/O has to happen on the host: the sandboxed agent shell has no `/dev/bus/usb`,
`/dev/hidraw*` or `/dev/usbmon*`, so `upload` / `read` / `led` / `watch` and any live
capture were run directly on the host (root, or with
[`udev/99-ch57x.rules`](../udev/99-ch57x.rules) installed). Everything offline —
parser, encoder, `validate`, `dump`, `decode`, selftests — runs anywhere, no device.

## Agent: pi coding agent

| | |
|---|---|
| Pi | 0.85.1 (`@earendil-works/pi-coding-agent`) |
| Provider | `local-llama` — OpenAI-compatible endpoint `http://127.0.0.1:8080/v1`, served from the RTX 4070 Ti SUPER on this host (the agent's shell reaches only the sandbox proxies, so `curl` from a sandboxed command cannot hit `:8080` directly) |
| Model | `qwen3.8-flash-next-q3_k_xl-128k`, 128k context, thinking level `medium` |
| Alternates configured | Qwen3-Coder-30B (32k/128k), Qwen3.8-27B quants (32k–256k), Granite-4.2-8B, Gemma-4-26B |
| Sessions | JSONL transcripts under `~/.pi/agent/sessions/--home-chris-tmp-wired-mini-keyboard--/` |

No cloud model was used for implementation. Local-only by configuration:
`defaultProvider: local-llama` in `~/.pi/agent/settings.json`.

### Pi extensions

| Package | Version | Role in this project |
|---|---|---|
| `pi-web-access` | 0.29.0 | Web search, URL/PDF fetching, GitHub clone — upstream Rust source, USB/HID specs, vendor archive |
| `pi-mcp-adapter` | 2.34.0 | Loads the two MCP servers below as ordinary tools |
| `pi-sandbox` | 0.6.8 | OS-level sandbox for shell commands (read `.`/`~/.config`/`/usr/lib`, write `.`/`/tmp`; blocks `.env`, `*.key`) |
| `pi-advisor` | 0.3.0 | Optional escalation tool for stuck debugging — installed, **disabled** here |
| `pi-token-speed` | 0.10.1 | Tokens/sec overlay, used to sanity-check local inference speed |
| `pi-ssh-remote` | 0.1.12 | Persistent remote SSH workspace (unused for the keyboard work) |
| `pi-copilot-auto` | 0.1.0 | Copilot auto-routing provider (unused — kept cloud models out) |
| `pi-clinepass-provider` | 1.4.0 | Cline subscription provider (unused — kept cloud models out) |

Web search config (`~/.pi/agent/web-search.json`): providers `duckduckgo` + `exa`,
`workflow: auto-summary`.

## MCP servers

Declared in the repo's `.mcp.json` (project) and `~/.pi/agent/mcp.json` (global).

| Server | Backed by | Used for |
|---|---|---|
| `ghidra` | [ghidra-headless-mcp](https://github.com/mrphrazer/ghidra-headless-mcp) (`~/tools/ghidra-headless-mcp`, Python 3.12.3 venv) driving **Ghidra 12.1.3** (`~/tools/ghidra_12.1.3_PUBLIC`), 212 tools | Reverse engineering the vendor `MINI_KEYBOARD.exe` Qt build tree — `widget.o` protocol core, record/key-id layout cross-check (see [documentation.md](documentation.md#vendor-tool-reverse-engineering)) |
| `cpp-tools` | [mcp-cpp](https://github.com/mpsm/mcp-cpp) (clangd LSP front-end) in Docker; local image `mcp-cpp-server`, mounted read-only | Semantic C++ navigation while porting: symbol search, call/inheritance hierarchy, CMake compile-database discovery |

```bash
# cpp-tools runs per-invocation, read-only:
docker run -i --rm -v "$PWD:$PWD:ro" -v "$PWD:/workspace:ro" mcp-cpp-server --root /workspace
# ghidra:
~/tools/ghidra-headless-mcp/.venv/bin/python ~/tools/ghidra-headless-mcp/ghidra_headless_mcp.py \
  --ghidra-install-dir ~/tools/ghidra_12.1.3_PUBLIC
```

## Reproducing without the agent stack

```bash
sudo apt install -y libusb-1.0-0-dev cmake g++ make
cmake -S . -B build && cmake --build build -j && ctest --test-dir build
```

The Ghidra / clangd / pi tooling is development scaffolding only — it is not referenced
by the build and the vendored Windows binaries are not committed
([vendor/README.md](../vendor/README.md)).

# Technical reference — CH57x wired mini keyboard (`514c:8851`)

Protocol, descriptors, record layouts and measured firmware behaviour.
Command-line usage lives in [`examples.md`](examples.md); the
[README](../README.md) covers install and quick start.

## Contents

- [Device identity](#device-identity)
- [USB topology](#usb-topology)
- [Programming protocol](#programming-protocol)
- [Key and action encoding](#key-and-action-encoding)
- [LED encoding](#led-encoding)
- [Read-back (`0xFA`)](#read-back-0xfa)
- [Wire capture format](#wire-capture-format)
- [Config semantics](#config-semantics)
- [Verification strategy](#verification-strategy)
- [Vendor tool reverse engineering](#vendor-tool-reverse-engineering)
- [Related tooling](#related-tooling)
- [Open problems](#open-problems)
- [Environment limitations](#environment-limitations-during-development)

## Device identity

| Field | Value |
|---|---|
| USB VID:PID | `514c:8851` |
| Vendor string | `c̪USB Keyboardȉ` (sloppy embedded string, non-UTF8 padding) |
| Product string | `USB Keyboardȉ` |
| Serial | `433132353134362E` → ASCII `C125146.` |
| bcdUSB / bcdDevice | 1.10 / 1.00 |
| Speed | Full Speed (12 Mb/s) |
| MaxPower | 400 mA |
| Maker (label) | Hangzhou Hangkai Technology Co. / sikaicase.com |
| MCU | WCH (WinChipHead) **CH57x** RISC-V (2.4 GHz, optional BLE; this unit is wired-only) |

`0x514c` (and sibling `0x1189`) is **not a USB-IF-registered company** — one factory
uses both VIDs for this whole family of CH57x macro keypads, rebadged widely.
Family PIDs on the same firmware: `0x8830–0x8833, 0x8840, 0x8842, 0x8850, 0x8851, 0x8890`.

## USB topology

Two HID interfaces in one configuration (`lsusb -v -d 514c:8851`):

**Interface 0 — vendor / programming channel** (what `upload`, `read`, `led` use)

- class `3`, subclass `0`, protocol `0` — vendor-defined HID, creates no input device
- report descriptor 36 bytes: usage page `0xFF00`, one report ID `0x03`, 64-byte IN
  **and** 64-byte OUT reports
- endpoints `0x82` IN / `0x02` OUT, interrupt, 64 bytes, `bInterval` 1
- appears as `/dev/hidrawN` only (no `/dev/input` node) — pressing keys never appears
  here, however hard you press

**Interface 1 — real keyboard/mouse** (what `watch` reads)

- class `3`, subclass `1` (boot), protocol `1` (keyboard)
- report descriptor 207 bytes, four report IDs: `0x01` keyboard (6-key array),
  `0x02` mouse (3 buttons, X/Y/wheel), `0x04` keyboard (**9-key** array),
  `0x05` consumer control (16-bit usage)
- endpoint `0x81` IN only, interrupt, 16 bytes, `bInterval` 1 (1 ms → 1000 Hz poll)
- bound to `hid-generic` → `/dev/input/eventN` (two nodes: keyboard + consumer)

Consequences for the implementation:

- Report IDs `0x01`/`0x04` mean the descriptor advertises up to **9 keys**; the
  firmware family supports layouts up to 21 buttons, and the tool cannot infer the
  physical layout — `rows`/`columns`/`knobs` come from `config.yaml`.
- `Device::open()` sends a 64-byte **zero init packet** (mirrors the Rust
  `open_device`). Interface 1 has no OUT endpoint, so the init write is skipped
  there — that is why `watch` (which pins interface 1) must not attempt it.
- Claiming interface 1 auto-detaches `hid-generic`; the keys stop typing to the OS
  until the tool exits, and the driver is re-attached on close.

## Programming protocol

Transport: interrupt OUT to endpoint `0x02`, 64-byte payload, first byte is the HID
report ID `0x03` (libusb writes the payload without a separate report-ID prefix byte;
the vendor tool sends 65 bytes via hidapi, which prepends it). Every command is one
64-byte report, zero-padded. Every OUT report is answered by a **one-byte `00`** IN
report.

| Command | Bytes | Purpose |
|---|---|---|
| Init | `[0x03,0x00 ×63]` | sent once on open (vendor interface only) |
| Identify | `[0x03,0xFB,0xFB,0xFB,…]` → read 64 B | button count + board variant |
| Read config | `[0x03,0xFA,0x0F,0x03,n]` (legacy) / `[0x03,0xFA,0x19,0x00,n]` (new) → read 64 B ×N | stream stored bindings back |
| Bind key | `[0x03,0xFE,keyID,layer+1,kind,…payload]` | one binding |
| LED block | `[0x03,0xFE,0xB0,layer+1,0x08,…,code@12]` | per-layer backlight setting |
| Finish | `[0x03,0xAA,0xAA,0,…]` | per-key terminator |
| Commit | `[0x03,0xFD,0xFE,0xFF]` | save to flash (vendor tool sleeps 200 ms here) |

Per bound key, `upload` emits exactly:

```
[0]   bind            03 fe <keyID> <layer+1> <kind> …
[64]  finish          03 aa aa
[128] commit          03 fd fe ff
[192] finish          03 aa aa
```
plus a 4th 64-byte report (`kind 5`, delay) between bind and finish when the action
has a `{delay(n)}`.

**Identify response** — this unit answers `03 fb 03 00 00 …`:

| Byte | Meaning | This board |
|---|---|---|
| `[0]` | report ID | `0x03` |
| `[1]` | echo of opcode `0xFB` | `0xfb` |
| `[2]` | programmable **button count** (knobs excluded) | `3` |
| `[3]` | board variant / style byte | `0x00` |
| `[4]` | firmware status | `0x00` (legacy; `0x0a` on newer firmware) |

No key codes are returned — identify is layout metadata only.

## Key and action encoding

**Key id** (`Keyboard884x::toKeyId`, k884x `to_key_id`):

| Physical key | Key id |
|---|---|
| button `n` (0-based) | `n + 1` → 1–15 (`n ≥ 12` rejected when `knobs == 4`) |
| knob `m` (0–2), action `a` | `16 + 3·m + a` → 16–24 |
| 4th knob (`buttons ≤ 12`) | `13 + a` → 13–15 |
| LED block | `0xB0` |

`a` = 0 ccw, 1 press, 2 cw. Driver constraint:
`(buttons ≤ 15 && knobs ≤ 3) || (buttons ≤ 12 && knobs ≤ 4)`.

**Bind record layout** (byte offsets inside the 64-byte report; `kind` at `[4]`):

| kind | Meaning | Payload |
|---|---|---|
| `1` | keyboard sequence | count at `[10]` (0 when a lone modifier chord), then `(modifier, usage)` pairs from `[11]`, ≤ 18 accords |
| `2` | media / consumer | u16 consumer usage little-endian at `[11]` |
| `3` | mouse | action `[10]`, modifier `[11]`, buttons `[12]`, dx `[13]`, dy `[14]`, wheel `[15]` |
| `5` | delay | u16 ms at `[5]` (≤ 6000), attaches to the record before it |
| `8` | LED block (key id `0xB0`) | mode byte at `[12]` |

Mouse action bytes: `0x01` click, `0x03` wheel, `0x05` move (buttons `0`) / drag.

**Modifier bits** (keyboard): standard HID bitmap — `ctrl 0x01`, `shift 0x02`,
`alt 0x04`, `win 0x08`, `rctrl 0x10`, `rshift 0x20`, `ralt 0x40`, `rwin 0x80`.
**Mouse modifier bits**: `ctrl 0x01`, `shift 0x02`, `alt 0x04`.

**Consumer page (`0x0C`) usages** accepted as media actions:

| name | usage | name | usage |
|---|---|---|---|
| `screenbrightnessup` | `0x6f` | `play` | `0xcd` |
| `screenbrightnessdown` | `0x70` | `mute` | `0xe2` |
| `next` | `0xb5` | `volumeup` | `0xe9` |
| `previous` / `prev` | `0xb6` | `favorites` | `0x182` |
| `stop` | `0xb7` | `calculator` | `0x192` |
| `screenlock` | `0x19e` | `webpagehome` | `0x223` |
| `webpageback` | `0x224` | `webpageforward` | `0x225` |

## LED encoding

`ledCode = (color << 4) | mode`, written to byte `[12]` of the `0xB0` block.

| Mode string | Mode nibble | Notes |
|---|---|---|
| `off` | `0` | byte `0x00` |
| `backlight <color>` | `1` | steady backlight; **white** is special: `0x05` (mode 5 + color 0) |
| `shock <color>` | `2` | no baseline backlight, flash/ripple on keypress |
| `shock2 <color>` | `3` | alternative keypress animation |
| `press <color>` | `4` | no baseline backlight, only the pressed key lights |

| Color | Nibble | Color | Nibble |
|---|---|---|---|
| `white` (backlight only) | 0 | `green` | 4 |
| `red` | 1 | `cyan` | 5 |
| `orange` | 2 | `blue` | 6 |
| `yellow` | 3 | `purple` | 7 |

`shock2` is matched before `shock` (shared prefix). 23 valid combinations
(`off` + white backlight + 7 colours × 3 animated modes). Layer is 0-based in the
CLI, sent as `layer + 1`.

## Read-back (`0xFA`)

Measured on this board with `--capture` (`status = 0x00`, "legacy" firmware):

- `03 fa 0f 03 <n>` (`n` = 1, 2, 3 …) streams the stored map back as 64-byte reports,
  12–13 per request. The vendor tool caps that counter at `3·buttons + 15` (24 here);
  the newer firmware form `03 fa 19 00 <n>` caps at `3·buttons + 1`. `readConfig()`
  tries the preferred variant, falls back to the other if it yields nothing, stops at
  the first empty reply, and resends an unanswered request once.
- **Every OUT report is acknowledged by a one-byte `00` IN report**, so the reply
  stream alternates `00`, record, `00`, record … Those ACKs are neither records nor
  end-of-stream: they must not count against a per-request budget (doing so halves the
  records retrieved) and must not be confused with all-zero filler reports.
- Records use the bind layout above **except byte `[1]`**, which carries the *read*
  opcode `0xFA` instead of the write opcode `0xFE`. `decodeBind()` accepts both, so
  read-back records and `upload`'s own output decode identically.
- Records arrive in storage order, not key order (ids jump, layers interleave); the
  tool keeps stream order because a `delay` record belongs to the record before it.
- This firmware stores **24 slots × 3 layers** = 72 binding records (confirmed by a
  complete read). Slots 1–3 are the physical keys; slots 4–15 (beyond the grid) and
  16–24 (knob range) hold a factory default image (`a`..`r`, digits `1`..`9`) and are
  labelled `slot N` rather than pretending to be physical keys.
- Layer byte: `1`-based on the wire (`bindKey` rejects index > 15, so 16 layers max);
  this board stores 3, which `read` shows as L1/L2/L3.

Cross-check against hardware: pressing the keys typed `a`, `b`, `c` before anything
was uploaded, matching the decoded `0xFA` map — the read-back path is confirmed end to
end on a factory-default unit.

## Wire capture format

`--capture <file>` (or `CH57X_CAPTURE=<file>`) hooks the two libusb transfer calls in
`src/usb.cpp` (`Device::send`, `Device::readReport`), so the file is exactly what
the wire carried, including timed-out reads (which mark where a reply stream ends).
Writer/reader: `src/capture.cpp`.

```
frame := 'C' 'X' | dir | len(u8) | seq(u32 le) | len payload bytes
dir   := 'O' host->device OUT report | 'I' IN report received | 'T' IN timed out
```

`decode <capture-file> [config]` walks that log offline, groups `IN` reports by the
`03 fa …` request that produced them, drops one-byte ACKs and all-zero filler, reports
records per request, and prints the same table `read` prints. Capture once with a
device, then iterate on the parser with no device and no root.

Reference fixture: [`testdata/board-514c-8851.captured`](../testdata/board-514c-8851.captured)
(truncated after 37 records — see [open problems](#open-problems)).

## Config semantics

`config.yaml` fields: `model` (optional; `514c:8851` auto-detects to `ch57x-1`),
`orientation`, `rows`, `columns`, `knobs`, and `layers[]` with a `buttons` grid
(`rows` lists of `columns`) and `knobs[]` (`ccw`/`press`/`cw`).

- `orientation` maps the config grid onto the physical keys: `normal`, `upsidedown`,
  `clockwise`, `counterclockwise`. `normal`/`upsidedown` are horizontal,
  `clockwise`/`counterclockwise` transpose `rows`×`columns`.
- A key slot left empty (`~`/`null`) is skipped — no record is written for it.
- **Limited layout:** when `(rows == 1 || columns == 1) && knobs == 1`, the firmware
  only honours modifiers on the **first** key of a sequence, so `ctrl-a` is accepted
  and `a,ctrl-b` is rejected by `render()` (`src/config.cpp:106`).
- Model names are decoupled from the PID (they select driver + preferred endpoint):
  `ch57x-1` = `514c:8851` (EP `0x02`); `ch57x-2` = `1189:8890`, `ch57x-3` = `514c:8850`
  (drivers not ported — `createDriver` throws).

## Verification strategy

- **Byte-exact dry run.** `dump` prints the exact 64-byte OUT reports `upload` would
  transmit; [`expected_dump.txt`](../expected_dump.txt) is that output for the
  repo `config.yaml`. Regenerate after intentionally changing `config.yaml`:
  `./build/ch57x-keyboard-tool dump config.yaml > expected_dump.txt`
- The reports match the known-good Rust reference byte for byte — e.g. the media bind
  is `03 fe <keyID> 01 02 … <usage-low>` (`03 fe 01 01 02 … ea` for `volumedown`),
  matching `test_media_macro_bytes` in
  `kriomant/ch57x-keyboard-tool/src/keyboard/k884x.rs`, followed by the per-key
  finish/commit/finish sequence.
- **`ch57x-selftest`** (171 checks, no USB): name/code tables, parser, key-id mapping,
  wire-encoding byte offsets vs the Rust reference, record decode round-trip including
  raw hardware records carrying the `0xfa` read opcode, capture round-trip, LED codes,
  orientation/render, and the first-key-only limited-layout rule.
- `show-keys`, `validate`, `dump`, `decode` need no device and no root.

## Vendor tool reverse engineering

The vendor bundle — see [`vendor/README.md`](../vendor/README.md) for the download URL,
checksums and file inventory (the binaries themselves are not committed) — is a full
MinGW/Qt build tree: `.o` objects with unstripped symbol tables, moc sources, Makefiles.
Ghidra RE of `widget.o` (the protocol core, ~200 defined symbols) shows the protocol is
**identical** to `ch57x-keyboard-tool`'s `k884x.rs`.

- **Stack** — Qt 5.14.2 Widgets, qmake (`KEY_PRO.pro` → `MINI_KEYBOARD.exe`), MinGW GCC
  32-bit (`mingw73_32`), USB through **hidapi** (`hid_enumerate`/`hid_open`/`hid_write`/
  `hid_read_timeout`/`hid_set_nonblocking`); app sources `main.cpp`, `widget.cpp`
  (+ `dialog1/2/3`, `reminderwidget`, `thread`).
- **VID/PID** — VID `0x514c` as 16/32-bit immediates; PID support table (u16 array in
  `.data`): `0x8842 0x8840 0x8830 0x8831 0x8832 0x8833 0x8850 0x8851 0x8840 0x1189`
  (`0x8851` present). The open-source tool additionally maps `1189:8890` → `ch57x-2`
  and `514c:8850` → `ch57x-3`.
- **Auto-detect** — `Identify_KeyBoard_style()` maps `(count, variant, status)` to ~25
  `Set_Keyboard_<N>add<M>` initializers (**N** = buttons 0–21, **M** = extra knobs 0–4).
  That is why the CLI needs an explicit `rows`×`columns`/`knobs`: it does not run this
  mapping.
- **LED UI** — buttons named `LED_Mode_0` … `LED_Mode_5` and `LED_color_1` …
  `LED_color_51`: 6 mode codes (matching the table above, `5` = white backlight) and a
  swatch palette wider than the 8 protocol colour values. No mode *labels* survive as
  strings.
- The vendor tool also does **read-back** (`0xFA`) and a per-model GUI; the Rust CLI is
  write-only, this C++ port implements both directions.
- **Beyond remapping** — bindings live in the CH57x MCU flash/EEPROM. Dumping/reflashing
  the MCU is possible over WCH two-wire debug (`wchisp`/`wlink`), but read-out
  protection is likely enabled. The HID vendor channel is the supported path and is all
  that remapping requires.

## Related tooling

| Tool | Notes |
|---|---|
| [`kriomant/ch57x-keyboard-tool`](https://github.com/kriomant/ch57x-keyboard-tool) | Rust CLI, the reference implementation this C++ port follows. `514c:8851` = model `ch57x-1`, endpoint `0x02`. **Not in any release tag**: support landed on `main` 2026-07-14 (`7e2074e`, `#188`) + endpoint fix `aff3382`; latest release `v1.7.0` lacks `514c`. Build from `main`: `cargo install ch57x-keyboard-tool --git https://github.com/kriomant/ch57x-keyboard-tool`. Write-only (no `read`). |
| [`jvspier/macropad-rebind`](https://github.com/jvspier/macropad-rebind) | WebHID browser GUI (Chromium only), read-back + LED control, `514c:8851` listed as fully working. Serve `index.html` locally, needs `/dev/hidraw` access. |
| `MINI_KEYBOARD.exe` (vendor; how to fetch it: [`vendor/README.md`](../vendor/README.md)) | Windows-only; auto-detects layout; same protocol. |

## Open problems

1. **Read truncation (reproducible from the fixture).** After 37 records (layer 1
   complete, layer 2 through slot 13) the device stops taking requests: the 4th
   `03 fa 0f 03 04` OUT report times out (`send failed: Operation timed out`) and is
   never answered, so 11 records never arrive. The tool now catches the send failure,
   keeps the records already read, is bounded by `deadlineMs` (default 5 s) and reports
   truncation instead of looking hung — but the firmware behaviour is unexplained.
   Ideas: pace requests with a delay, or address layer 2 with different group numbering.
2. **Layer toggle on this 3-key board.** The stored map has L1–L3; the vendor UI exposes
   one layer — the others are probably FN-based.
3. **LED round-trip on hardware.** `setLed` encoding is byte-identical to the Rust
   reference and covered by selftest, but a `led` write followed by `read` has not been
   confirmed on the board.
4. **Newer firmware path** (`identify` status `0x0a` → `03 fa 19 00 <n>`) is implemented
   but untested against hardware.
5. **Second unit / other PIDs** (`ch57x-2`, `ch57x-3`) — drivers not ported.

## Environment limitations during development

The dev sandbox has no `/dev/bus/usb`, no `/dev/hidraw*`, no `dmesg` and no
`/dev/usbmon`, so live packet capture and control transfers cannot run there;
`read`/`upload`/`led` must be exercised on the host (`sudo`, or install
[`udev/99-ch57x.rules`](../udev/99-ch57x.rules)). Everything else came from `lsusb -v`,
sysfs (`/sys/bus/usb`, `/sys/bus/hid/*/report_descriptor`),
`/proc/bus/input/devices`, the fixture captures, and the upstream tools/docs.

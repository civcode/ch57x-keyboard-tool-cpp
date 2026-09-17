# CLI examples — `ch57x-keyboard-tool`

All commands run from the repo root after
[building](../README.md#quick-start). The commands that touch the board (`read`, `upload`,
`led`, `watch`) are written with `sudo` below — that is the recommended way to run them.
A udev rule can drop the `sudo`, but installing one is **optional and not required**; see
[Running without sudo](#running-without-sudo-optional) at the end.

Global options (before the command): `--model <ch57x-1|2|3>`, `--vid <hex>` /
`--pid <hex>`, `--address <bus> <devno>` (disambiguate several boards),
`--interface <n>` (`watch` defaults to `1`, the HID keyboard channel),
`--capture <file>`, `--raw` (`read`/`decode`: hex-dump every report too).
The config path defaults to `./config.yaml`; `-h/--help` prints the same list.
`show-keys`, `validate`, `dump` and `decode` need neither a device nor root.

## Contents

- [Media buttons](#media-buttons)
- [LEDs](#leds)
- [Inspect what is stored](#inspect-what-is-stored)
- [Watch the keys you press](#watch-the-keys-you-press)
- [Capture and decode offline](#capture-and-decode-offline)
- [Text, mouse, macros](#text-mouse-macros)
- [Multiple layers](#multiple-layers)
- [Dry run and validation](#dry-run-and-validation)
- [Running without sudo (optional)](#running-without-sudo-optional)

## Media buttons

Three-button media pad — previous / play-pause / next, left to right. The `~` entries
are empty slots; a single row of three is just one list.

```bash
cat > config.yaml <<'YAML'
orientation: normal     # left → right
rows: 1
columns: 3
knobs: 0                # this board has no knob
layers:
  - buttons:
      - [previous, play, next]
YAML

./build/ch57x-keyboard-tool validate config.yaml   # offline check
./build/ch57x-keyboard-tool dump config.yaml       # show the exact 64-byte reports
sudo ./build/ch57x-keyboard-tool upload config.yaml
```

Action names are the long spellings only: `play`, `next`, `previous` (or `prev`).
`playpause`, `nexttrack` and `previoustrack` are **not** accepted — the tool resolves
names against its own table, not the firmware's. Names are also singular events: each
button sends one media usage per press, and there is no "hold to repeat" setting.

Verify on hardware without leaving the tool:

```bash
sudo ./build/ch57x-keyboard-tool watch config.yaml
```

Each press prints one line naming the action the keyboard sent. The tool grabs
interface 1 (the keyboard/mouse interface) rather than the programming interface, so
your terminal still receives keyboard input while it runs — but the three keys stop
typing to the OS while it holds that interface.

Other media actions worth knowing: `stop`, `mute`, `volumeup`, `volumedown`,
`screenbrightnessup`, `screenbrightnessdown`, `calculator`, `favorites`,
`webpagehome`, `webpageback`, `webpageforward`, `screenlock`.
Full table: [LED/media encoding](documentation.md#key-and-action-encoding).

## LEDs

Lighting is configured per layer. The CLI is the only way to change it —
`config.yaml` has no `led:` section, so `upload` never touches lighting and
`read` never reports it.

Usage is positional: `led <layer> <mode> [color]`, **layer is 0-based** (so `led 0`
is the layer the board boots on).

```bash
# steady blue backlight, layer 1
sudo ./build/ch57x-keyboard-tool led 0 backlight blue

# same on layer 2, and pick the device explicitly
sudo ./build/ch57x-keyboard-tool --vid 0x514c --pid 0x8851 led 1 backlight green

# light only the key you press (no steady backlight)
sudo ./build/ch57x-keyboard-tool led 0 press cyan

# animation with a steady baseline
sudo ./build/ch57x-keyboard-tool led 0 shock purple

# off
sudo ./build/ch57x-keyboard-tool led 0 off
```

| Mode | Effect | Colours |
|---|---|---|
| `off` | backlight off (no colour argument) | — |
| `backlight <color>` | steady backlight | `white red orange yellow green cyan blue purple` |
| `shock <color>` | no baseline; flash/ripple animation on keypress | all but white |
| `shock2 <color>` | alternative keypress animation | all but white |
| `press <color>` | no baseline; only the pressed key lights | all but white |

Colours are single values — no gradients, brightness, or speed. White exists only as a
steady backlight (`backlight white`); the animated modes take one of the seven colours.
Mode and colour are packed as `(colour << 4) | mode` into one byte:
[LED encoding](documentation.md#led-encoding).

## Inspect what is stored

```bash
sudo ./build/ch57x-keyboard-tool read
```

```
opening device (vendor interface)...
claiming interface 0 (out EP 0x2, in EP 0x82)
identifying board (03 fb fb fb)...
  3 programmable buttons, style 0x00, status 0x00, variant ch57x-1
requesting stored config (03 fa 0f 03 <n>)...
  4 request(s) using the legacy protocol, 130 report(s) read, 37 binding record(s)
37 record(s) retrieved:

  L1
    key 01  "a"                 [HID usage 0x04 (key 'a')]
    key 02  "b"                 [HID usage 0x05 (key 'b')]
    key 03  "c"                 [HID usage 0x06 (key 'c')]
    ...
```

Notes on the output:

- Actions are quoted; the `[HID usage …]` notes are aligned in one column.
- `L1`/`L2`/`L3` group records into layers **in stream order** — the records carry no
  layer-delimiter, so this is presentation, not stored structure.
- Slots `04`–`15` are beyond this board's 1×3 grid, `16`–`24` are the knob range; both
  hold a factory default image (`a`..`r`, digits `1`..`9`) and are labelled `slot N`.
- `--verbose` prints every record as hex.
- If output ends with `… 11 record(s) never arrived (device stopped answering) …` plus a
  `TRUNCATED` line, that is firmware misbehaviour, not a parse failure:
  [open problems](documentation.md#open-problems).

## Watch the keys you press

```bash
# live names, cross-referenced against the config you just uploaded
sudo ./build/ch57x-keyboard-tool watch config.yaml

# without a config: report IDs, raw usage pages/codes, key names where known
sudo ./build/ch57x-keyboard-tool watch

# raw 10-digit bytes only
sudo ./build/ch57x-keyboard-tool watch --raw

# record the session for offline work
sudo ./build/ch57x-keyboard-tool watch --capture watch.log config.yaml
```

`watch` pins the keyboard interface (`--interface 1` by default) because the vendor
interface emits nothing when you press keys. If the framing looks wrong (no names, or
wrong ones), try `--interface 0`. Ctrl-C stops it; unclaimed interfaces are re-attached
on exit.

## Capture and decode offline

```bash
# capture a full read from the device
sudo ./build/ch57x-keyboard-tool read --capture read.log

# then iterate on the decoder with no device and no root
./build/ch57x-keyboard-tool decode read.log
./build/ch57x-keyboard-tool decode read.log config.yaml
```

Any command can capture (`--capture <file>` or `CH57X_CAPTURE=<file>`), including
`watch` — a watch capture is all `I` frames. Frame format and replay semantics:
[wire capture format](documentation.md#wire-capture-format).

## Text, mouse, macros

```yaml
layers:
  - buttons:
      - ["hello", "ctrl-a", "click(left)"]        # text, shortcut, mouse click
  - buttons:
      - ["ctrl-shift-s", "wheel(up)", "move(left,10)"]
```

| Syntax | Meaning |
|---|---|
| `a` | plain key |
| `ctrl-a`, `ctrl-shift-s` | modifier + key |
| `"ctrl-a, b, ctrl-c"` | sequence of key events |
| `"{delay(120)}a"` | 120 ms gap before/around the keys |
| `<110>` | raw HID usage code |
| `click(left\|right\|middle\|back\|forward)` | mouse click |
| `wheel(up\|down\|left\|right)` | mouse wheel |
| `move(left\|right\|up\|down,N)` | mouse move |
| `drag(left,N,M)` | mouse drag |

Quote anything containing `,`, `(`, `)` or `<`. `./build/ch57x-keyboard-tool show-keys`
prints the full key/modifier/media/mouse name tables.

## Multiple layers

```yaml
orientation: normal
rows: 1
columns: 3
knobs: 0
layers:
  - buttons: [[previous, play, next]]      # L1
  - buttons: [["ctrl-c", "ctrl-v", "ctrl-z"]]  # L2
  - buttons: [[mute, stop, "enter"]]           # L3
```

Up to 16 layer indices exist on the wire; this board stores 3 (the `read` output shows
them as L1/L2/L3). How to switch layers on hardware is still
[an open question](documentation.md#open-problems).

## Dry run and validation

```bash
./build/ch57x-keyboard-tool validate config.yaml   # grid size, names, limited layout
./build/ch57x-keyboard-tool dump config.yaml       # exact OUT reports, no USB
./build/ch57x-keyboard-tool dump --model ch57x-1 config.yaml
ctest --test-dir build            # unit tests
./build/ch57x-selftest            # 171 offline checks
```

Regenerate the golden file after intentionally changing `config.yaml`:

```bash
./build/ch57x-keyboard-tool dump config.yaml > expected_dump.txt
```

## Running without sudo (optional)

Not needed to use the tool — `sudo` is the recommended path, and this detour only pays off
if you rebind the board often enough to be bothered by it. It writes a system rules file,
adds you to `plugdev`, and needs a replug plus a re-login to take effect:

```bash
sudo cp udev/99-ch57x.rules /etc/udev/rules.d/
sudo usermod -aG plugdev $USER
sudo udevadm control --reload-rules && sudo udevadm trigger   # then unplug + replug

./build/ch57x-keyboard-tool read          # now works without sudo
```

The rule ([`udev/99-ch57x.rules`](../udev/99-ch57x.rules)) matches every `514c:*` device —
the whole rebadged CH57x keypad family, not just `514c:8851` — so it is deliberately an
opt-in rather than a setup step.

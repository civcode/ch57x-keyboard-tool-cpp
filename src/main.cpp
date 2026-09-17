// main.cpp — command-line front end for the CH57x mini-keyboard tool.
//
// Subcommands:
//   show-keys              print the key/action reference (no USB needed)
//   validate [config]      parse + render a mapping config (no USB needed)
//   dump     [config]      dry-run: print the exact OUT bytes upload would send
//   upload   [config]      write the mapping to the device
//   read     [config]      read back: identify handshake + stored reports
//   decode <capture-file>  analyse a --capture log offline (no USB needed)
//   watch                  live key-press monitor: print what a press reports
//   led      <layer> <mode> [color]
//
// Global options:
//   --model <ch57x-1>       force the model (else taken from the config)
//   --vid <hex> --pid <hex> filter by device id
//   --address <bus> <dev>   pick a specific device when several match
//   --raw                   read: additionally print the raw hex of each report
//
// `config` defaults to ./config.yaml.

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "ch57x/capture.h"
#include "ch57x/codes.h"
#include "ch57x/config.h"
#include "ch57x/keyboard.h"
#include "ch57x/usb.h"
#include "ch57x/yaml.h"

using namespace ch57x;

namespace {

std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open config file: " + path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Hex bytes with the trailing zero padding trimmed (handshake replies).
bool isZero(const std::vector<uint8_t>& d) {
    for (uint8_t b : d)
        if (b) return false;
    return true;
}

std::string hexTrimmed(const std::vector<uint8_t>& d) {
    size_t n = d.size();
    while (n > 4 && d[n - 1] == 0) --n;
    std::ostringstream ss;
    for (size_t i = 0; i < n; ++i)
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)d[i] << ' ';
    if (n < d.size())
        ss << "(+" << (d.size() - n) << " zero bytes)";
    return ss.str();
}

// Prefix every line of a multi-line block (hexdumps) so it stays aligned.
std::string indent(const std::string& block, const std::string& pad) {
    std::string out, line;
    for (char c : block) {
        if (c == '\n') { out += pad + line + "\n"; line.clear(); }
        else line += c;
    }
    if (!line.empty()) out += pad + line;
    return out;
}

std::optional<KeyboardModel> parseModelArg(const std::string& s) {
    auto m = modelFromName(s);
    if (!m) throw std::runtime_error("unknown --model value: " + s +
                                     " (expected ch57x-1 | ch57x-2 | ch57x-3)");
    return m;
}

void printHelp() {
    std::cout <<
        "ch57x-keyboard-tool — configure a CH57x wired mini-keyboard (514c:8851)\n"
        "\n"
        "usage: ch57x-keyboard-tool [options] <command> [args]\n"
        "\n"
        "commands:\n"
        "  show-keys              show the key/action reference (no USB needed)\n"
        "  validate [config]      parse + render the mapping (no USB needed)\n"
        "  dump     [config]      dry-run: print the exact OUT bytes upload would send\n"
        "  upload   [config]      write the mapping to the device\n"
        "  read     [config]      read back: identify + stored reports\n"
        "  decode <capture-file> [config]\n"
        "                         analyse a --capture file offline (no USB)\n"
        "  watch    [config]      live key-press monitor: which key sends what\n"
        "  led      <layer> <mode> [color]\n"
        "\n"
        "options:\n"
        "  --model <ch57x-1|ch57x-2|ch57x-3>   force the model\n"
        "  --vid <hex>  --pid <hex>            filter by device id\n"
        "  --address <bus> <dev>               disambiguate multiple devices\n"
        "  --capture <file>                     log every USB transfer (also CH57X_CAPTURE=...)\n"
        "  --interface <n>                       open that interface (watch defaults to 1,\n"
        "                                        the HID keyboard channel, not vendor if 0)\n"
        "  --raw                                read/decode: also dump each report as hex\n"
        "  -h, --help                            this help\n"
        "\n"
        "config defaults to ./config.yaml.\n";
}

void showKeys() {
    std::cout << "=== Modifier names (combine with '-') ===\n";
    for (Modifier m : allModifiers())
        std::cout << "  " << modifierName(m) << "   (" << modifierAliases(m) << ")\n";

    std::cout << "\n=== Well-known key names (a-z, 1-0, etc.) ===\n";
    {
        std::string line;
        for (WellKnownCode c : allWellKnown()) {
            line += wellKnownName(c) + " ";
            if (line.size() > 60) { std::cout << "  " << line << "\n"; line.clear(); }
        }
        if (!line.empty()) std::cout << "  " << line << "\n";
    }

    std::cout << "\n=== Media keys (long-spelled names) ===\n";
    {
        std::string line;
        for (MediaCode c : allMedia()) {
            line += mediaName(c) + " ";
            if (line.size() > 60) { std::cout << "  " << line << "\n"; line.clear(); }
        }
        if (!line.empty()) std::cout << "  " << line << "\n";
    }

    std::cout << "\n=== Mouse actions ===\n"
              << "  click                       left click\n"
              << "  click(left|right|middle)    a specific button\n"
              << "  click(left+right)           button combination\n"
              << "  move(x,y)                   relative move, x/y in -127..127\n"
              << "  drag(buttons,x,y)           drag while holding buttons\n"
              << "  wheel(n)                    scroll, n in -127..127 (wheelup = wheel(1))\n"
              << "  prefix with ctrl- / shift- / alt- for mouse modifiers\n";

    std::cout << "\n=== Macro syntax ===\n"
              << "  a                         plain key\n"
              << "  ctrl-a                    modifier + key\n"
              << "  ctrl-a, b, ctrl-c         a sequence of key events\n"
              << "  {delay(120)}a             insert a 120ms gap before/around the keys\n"
              << "  <110>                     a raw HID usage code\n";

    std::cout << "\n=== Key ids (how a physical key maps to the firmware) ===\n"
              << "  button n (0-based)      -> key id n+1\n"
              << "  knob m (0-based) action -> key id 16 + 3m + action   (0=ccw,1=press,2=cw)\n"
              << "  a 4th knob (<=12 buttons) -> key id 13 + action\n"
              << "  constraint: (buttons<=15 && knobs<=3) || (buttons<=12 && knobs<=4)\n";

    std::cout << "\n=== Orientation (maps your config grid to the physical keys) ===\n"
              << "  normal | upsidedown | clockwise | counterclockwise\n";
}

void validateConfig(const std::string& path) {
    Config cfg = parseYamlConfig(readFile(path));
    std::vector<FlatLayer> layers = render(cfg);

    std::cout << "config file : " << path << "\n";
    std::cout << "model       : "
              << (cfg.model ? modelToString(*cfg.model) : std::string("(auto)")) << "\n";
    std::cout << "grid        : " << (int)cfg.rows << " rows x " << (int)cfg.columns
              << " cols, knobs: " << (int)cfg.knobs << ", orientation: "
              << orientationToString(cfg.orientation) << "\n";
    std::cout << "layers      : " << layers.size() << "\n\n";

    size_t bound = 0;
    for (size_t l = 0; l < layers.size(); ++l) {
        const FlatLayer& fl = layers[l];
        std::cout << "--- layer " << l << " ---\n";
        for (size_t i = 0; i < fl.buttons.size(); ++i) {
            if (fl.buttons[i]) {
                std::string pos = positionName(static_cast<uint8_t>(i), cfg.rows, cfg.columns);
                std::cout << "  key " << (i + 1);
                if (!pos.empty()) std::cout << " (" << pos << ")";
                std::cout << "  ->  " << fl.buttons[i]->toString() << "\n";
                ++bound;
            }
        }
        for (size_t k = 0; k < fl.knobs.size(); ++k) {
            const Knob& knob = fl.knobs[k];
            if (knob.ccw) std::cout << "  knob " << (k + 1) << " ccw    -> " << knob.ccw->toString() << "\n", ++bound;
            if (knob.press) std::cout << "  knob " << (k + 1) << " press  -> " << knob.press->toString() << "\n", ++bound;
            if (knob.cw) std::cout << "  knob " << (k + 1) << " cw     -> " << knob.cw->toString() << "\n", ++bound;
        }
        if (fl.buttons.empty() && fl.knobs.empty()) std::cout << "  (empty layer)\n";
    }
    std::cout << "\nvalid: " << bound << " keys bound across " << layers.size() << " layer(s)\n";
}

// Build the exact flat OUT payload that `upload` transmits, in 64-byte report
// units (each key = bind msg [+ optional delay] + 3 commit/finish msgs). No USB
// is touched, so this is safe for a dry-run `dump`. `bound` counts buttons only
// (matching upload's progress counter).
std::vector<uint8_t> buildUploadBuffer(const std::vector<FlatLayer>& layers,
                                       const std::unique_ptr<Keyboard>& driver,
                                       size_t& bound) {
    std::vector<uint8_t> output;
    bound = 0;
    for (size_t l = 0; l < layers.size(); ++l) {
        const FlatLayer& fl = layers[l];
        for (size_t i = 0; i < fl.buttons.size(); ++i) {
            if (fl.buttons[i]) {
                driver->bindKey(static_cast<uint8_t>(l), Key::button(static_cast<uint8_t>(i)),
                                *fl.buttons[i], output);
                ++bound;
            }
        }
        for (size_t k = 0; k < fl.knobs.size(); ++k) {
            const Knob& knob = fl.knobs[k];
            if (knob.ccw)
                driver->bindKey(static_cast<uint8_t>(l), Key::knob((uint8_t)k, KnobAction::RotateCCW), *knob.ccw, output);
            if (knob.press)
                driver->bindKey(static_cast<uint8_t>(l), Key::knob((uint8_t)k, KnobAction::Press), *knob.press, output);
            if (knob.cw)
                driver->bindKey(static_cast<uint8_t>(l), Key::knob((uint8_t)k, KnobAction::RotateCW), *knob.cw, output);
        }
    }
    return output;
}

void uploadConfig(const std::string& path, const UsbOptions& opts,
                  std::optional<KeyboardModel> flagModel) {
    Config cfg = parseYamlConfig(readFile(path));
    std::vector<FlatLayer> layers = render(cfg);

    KeyboardModel model = flagModel ? *flagModel : *cfg.model;
    if (!cfg.model && !flagModel)
        throw std::runtime_error("no model: set `model:` in the config or pass --model");

    uint8_t totalButtons = static_cast<uint8_t>(cfg.rows * cfg.columns);
    auto driver = createDriver(model, totalButtons, cfg.knobs);

    size_t bound = 0;
    std::vector<uint8_t> output = buildUploadBuffer(layers, driver, bound);

    std::cout << "opening device (" << (int)cfg.rows << "x" << (int)cfg.columns
              << " grid, " << (int)cfg.knobs << " knobs)...\n";
    auto dev = Device::open(opts, model);
    std::cout << "claiming interface " << (int)dev.interfaceNumber()
              << " (out EP 0x" << std::hex << (int)dev.outEndpoint() << ", in EP 0x"
              << (int)dev.inEndpoint() << ")\n";
    std::cout << "writing " << bound << " key(s) in " << (output.size() / 64) << " report(s)...";
    dev.send(output);
    std::cout << " done.\n";
}

// Dry run: print the exact 64-byte OUT reports `upload` would send, without
// touching USB. This is the end-to-end wire artifact a config produces.
void dumpConfig(const std::string& path, std::optional<KeyboardModel> flagModel) {
    Config cfg = parseYamlConfig(readFile(path));
    std::vector<FlatLayer> layers = render(cfg);

    KeyboardModel model = flagModel ? *flagModel : *cfg.model;
    if (!cfg.model && !flagModel)
        throw std::runtime_error("no model: set `model:` in the config or pass --model");

    uint8_t totalButtons = static_cast<uint8_t>(cfg.rows * cfg.columns);
    auto driver = createDriver(model, totalButtons, cfg.knobs);

    size_t bound = 0;
    std::vector<uint8_t> output = buildUploadBuffer(layers, driver, bound);

    std::cout << "DRY RUN (no USB) — exact OUT reports `upload " << path
              << "` would transmit, 64 bytes each:\n";
    std::cout << "  grid " << (int)cfg.rows << "x" << (int)cfg.columns << ", knobs "
              << (int)cfg.knobs << ", layers " << layers.size() << ", keys " << bound
              << ", bytes " << output.size() << ", reports " << (output.size() / 64)
              << " (plus one 64-byte zero init on open)\n\n";
    for (size_t off = 0; off < output.size(); off += 64) {
        std::cout << "--- report " << (off / 64) << " ---\n";
        std::cout << "  " << hexdump(output.data() + off,
                                     std::min<size_t>(64, output.size() - off))
                  << "\n";
    }
}

void ledCommand(uint8_t layer, const std::string& mode, const std::string& color,
                const UsbOptions& opts, std::optional<KeyboardModel> flagModel) {
    std::string modeStr = color.empty() ? mode : (mode + " " + color);
    auto dev = Device::open(opts, flagModel ? *flagModel : KeyboardModel::Ch57x_1);
    auto driver = createDriver(flagModel ? *flagModel : KeyboardModel::Ch57x_1, 0, 0);
    std::vector<uint8_t> output;
    driver->setLed(layer, modeStr, output);
    dev.send(output);
    std::cout << "set LED layer " << (int)layer << " -> " << modeStr << "\n";
}


// Decode and print read-back records in stream order. The firmware streams them
// as one sequence and a `delay` record belongs to the record directly before it,
// so the order is preserved rather than sorted.
void printStoredReports(const std::vector<ReadReport>& reports, uint8_t rows, uint8_t cols,
                        uint8_t knobs, bool raw) {
    std::vector<BindInfo> binds;
    int unknown = 0;
    for (const auto& rr : reports) {
        BindInfo b = decodeBind(rr.data, rows, cols, knobs);
        if (b.valid)
            binds.push_back(std::move(b));
        else
            ++unknown;
    }
    if (raw) {
        for (const auto& rr : reports)
            std::cout << "  [r" << rr.group << " #" << (rr.index + 1) << "]\n"
                      << indent(hexdump(rr.data.data(), rr.data.size()), "    ");
    }
    // Two aligned columns: the key/slot name, and the quoted action. Quoting keeps
    // multi-word actions (`ctrl-a`, `click(left)`, `led breathing`) readable, and the
    // action is padded to the widest one so the `[HID usage ...]` notes all start at
    // the same column instead of trailing the action.
    size_t width = 0, actionWidth = 0;
    for (const auto& b : binds) {
        width = std::max(width, b.keyName.size());
        actionWidth = std::max(actionWidth, b.action.size() + 2);  // +2 for the quotes
    }
    int shownLayer = -1;
    for (const auto& b : binds) {
        if (static_cast<int>(b.layer) != shownLayer) {
            shownLayer = b.layer;
            std::cout << "\n  layer " << (int)b.layer << " (L" << (int)b.layer << "):\n";
        }
        std::cout << "    " << std::left << std::setw((int)width) << b.keyName << "  ->  "
                  << std::setw((int)actionWidth) << ("\"" + b.action + "\"");
        if (!b.detail.empty()) std::cout << "  [" << b.detail << "]";
        std::cout << std::right << "\n";
    }
    std::cout << "\n  " << binds.size() << " binding record(s) decoded";
    if (unknown) std::cout << ", " << unknown << " report(s) not recognised";
    std::cout << "\n";
}

// Grid from config.yaml when present; `fallbackCols` (the 0xFB button count)
// otherwise. Returns false when no config file exists.
bool loadGrid(const std::string& path, uint8_t& rows, uint8_t& cols, uint8_t& knobs,
              std::optional<KeyboardModel>& model) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    Config cfg = parseYamlConfig(ss.str());
    if (!model) model = cfg.model;
    rows = cfg.rows;
    cols = cfg.columns;
    knobs = cfg.knobs;
    return true;
}

void readCommand(const std::string& path, const UsbOptions& opts,
                 std::optional<KeyboardModel> flagModel, bool raw) {
    // The config only names the keys (grid size); it is not required to read.
    uint8_t rows = 0, cols = 0, knobs = 0;
    std::optional<KeyboardModel> model = flagModel;
    loadGrid(path, rows, cols, knobs, model);
    auto dev = Device::open(opts, model);

    std::cout << "== identify (0xFB handshake) ==\n";
    auto id = identify(dev);
    std::cout << "  echo=0x" << std::hex << (int)id.echo << " keys=" << (int)id.keyCount
              << " style=0x" << (int)id.style << " status=0x" << (int)id.status
              << std::dec << (id.status == 0x0a ? " (newer firmware)" : " (legacy firmware)")
              << "\n";
    std::cout << "  raw: " << hexTrimmed(id.raw) << "\n";

    // Fall back to the identify reply when there is no config to name the keys.
    if (!rows || !cols) { rows = 1; cols = id.keyCount; }

    std::cout << "\n== stored key map (0xFA) ==\n";
    auto rc = readConfig(dev, id.status == 0x0a, 700, 200, id.keyCount);

    if (!rc.error.empty())
        std::cout << "  I/O error after " << rc.requests << " request(s): " << rc.error
                  << (rc.reports.empty() ? "" : " -- showing the records that did arrive") << "\n";

    if (rc.reports.empty()) {
        std::cout << "  nothing read back after " << rc.requests << " request(s): the device sent"
                  << (rc.zeroReports ? " only all-zero filler reports" : " no reports at all")
                  << " for both\n  0xFA request variants (03 fa 0f 03 <n>, 03 fa 19 00 <n>).\n"
                  << "  Use `dump` to see what `upload` would write, and `read --capture <file>`"
                  << " to record the traffic for `decode`.\n";
        return;
    }
    std::cout << "  request 03 fa " << (rc.newProtocol ? "19 00" : "0f 03") << " <n>"
              << ": " << rc.requests << " request(s), " << rc.reports.size() << " report(s)";
    if (rc.zeroReports) std::cout << ", " << rc.zeroReports << " filler report(s) skipped";
    if (rc.acks) std::cout << ", " << rc.acks << " one-byte ack(s)";
    std::cout << "\n";
    if (rc.truncated)
        std::cout << "  note: the device kept acknowledging but stopped sending records; the"
                  << " map above may be incomplete.\n";

    printStoredReports(rc.reports, rows, cols, knobs, raw);

}

// Live key-press monitor.
//
// Interface 0 (the vendor channel) carries only the programming protocol, which is
// why reading it shows nothing while keys are pressed. Presses are reported by the
// real HID interface -- interface 1 on 514c:8851, the one bound to
// /dev/input/eventN -- with report id 0x01/0x04 (keyboard), 0x02 (mouse) and 0x05
// (consumer/media). Claiming it detaches the kernel driver, so the keys stop typing
// to the OS until this exits.
void watchCommand(const std::string& path, UsbOptions opts,
                  std::optional<KeyboardModel> flagModel) {
    uint8_t rows = 0, cols = 0, knobs = 0;
    std::optional<KeyboardModel> model = flagModel;
    loadGrid(path, rows, cols, knobs, model);
    if (!opts.interface) opts.interface = 1;  // 0 would be the vendor channel

    auto dev = Device::open(opts, model);
    std::cout << "  interface " << (int)dev.interfaceNumber() << ", IN endpoint 0x"
              << std::hex << (int)dev.inEndpoint() << std::dec << "\n"
              << "  press keys now (Ctrl-C to stop). The kernel driver is detached while\n"
              << "  this runs, so the keys do not type to the operating system.\n";

    int quiet = 0;
    for (;;) {
        auto [buf, n] = dev.readReport(2000);
        if (n == 0) {
            if (++quiet == 3) std::cout << "  waiting for a report ...\n";
            continue;
        }
        quiet = 0;
        std::vector<uint8_t> b(buf.begin(), buf.begin() + n);
        if (isZero(b)) continue;
        std::cout << "  IN " << n << " byte(s): " << hexTrimmed(b);
        switch (b[0]) {
            case 0x01:
            case 0x04: {  // keyboard: report id, modifier mask, then the key codes
                const std::string mods = modifierMaskName(b.size() > 1 ? b[1] : (uint8_t)0);
                if (!mods.empty()) std::cout << "   modifiers: " << mods;
                for (size_t i = 2; i < b.size(); ++i)
                    if (b[i]) std::cout << "   key: " << wellKnownNameForValue(b[i]);
                break;
            }
            case 0x05:  // consumer control: 16-bit usage
                if (b.size() >= 3)
                    std::cout << "   media: " << mediaNameForCode((uint16_t)(b[1] | (b[2] << 8)));
                break;
            default:
                break;  // mouse (0x02) and anything else: raw bytes above
        }
        std::cout << "\n";
    }
}

// decode <capture-file> [config] — analyse a capture taken with --capture, no USB.
void decodeCommand(const std::string& capturePath, const std::string& path, bool raw) {
    auto frames = readCapture(capturePath);
    uint8_t rows = 0, cols = 0, knobs = 0;
    std::optional<KeyboardModel> model;
    if (!loadGrid(path, rows, cols, knobs, model)) {
        std::cout << "  (no " << path << "; naming slots needs --rows/--cols or the config)\n";
        if (!rows) { rows = 1; cols = 24; }
    }
    std::cout << "== capture " << capturePath << " ==\n";
    size_t outCount = 0, inCount = 0, timeouts = 0;
    for (const auto& fr : frames) {
        if (fr.dir == 'O') ++outCount;
        else if (fr.isTimeout()) ++timeouts;
        else ++inCount;
    }
    std::cout << "  " << frames.size() << " frame(s): " << outCount << " OUT, " << inCount
              << " IN, " << timeouts << " timeout(s)\n";

    // Walk the transfer log: each 0xFA request starts a new batch, and the IN
    // frames until the next request are that batch's reply stream.
    std::vector<ReadReport> reports;
    int request = 0, index = 0, zeroReports = 0, acks = 0;
    std::vector<int> perRequest;
    bool sawFa = false;
    for (const auto& fr : frames) {
        if (fr.dir == 'O') {
            if (fr.data.size() >= 2 && fr.data[0] == 0x03 && fr.data[1] == 0xfa) {
                ++request;
                index = 0;
                sawFa = true;
                perRequest.push_back(0);
            }
            continue;
        }
        if (fr.isTimeout() || request == 0) continue;
        bool zero = true;
        for (uint8_t b : fr.data)
            if (b) { zero = false; break; }
        if (fr.data.size() == 1) { ++acks; continue; }  // one-byte acknowledgement
        if (zero) { ++zeroReports; continue; }
        ReadReport rr;
        rr.group = request;
        rr.index = index++;
        if (!perRequest.empty()) perRequest.back()++;
        rr.data = fr.data;
        reports.push_back(std::move(rr));
    }
    if (!sawFa) {
        std::cout << "  no 0xFA read requests in this capture — run"
                  << " `ch57x-keyboard-tool read --capture <file>` first\n";
        return;
    }
    std::cout << "  " << request << " 0xFA request(s), " << reports.size() << " report(s)";
    if (zeroReports) std::cout << ", " << zeroReports << " filler report(s) skipped";
    if (acks) std::cout << ", " << acks << " one-byte ack(s)";
    std::cout << "\n";
    for (size_t i = 0; i < perRequest.size(); ++i) {
        std::cout << "    request " << (i + 1) << ": " << perRequest[i] << " record(s)\n";
        // A request that ends the log with no reply means the device stopped there.
        if (perRequest[i] == 0 && i + 1 == perRequest.size())
            std::cout << "      (unanswered: the map is probably truncated)\n";
    }
    if (!reports.empty()) printStoredReports(reports, rows, cols, knobs, raw);
}

}  // namespace


int main(int argc, char** argv) {
    // Flush every write: `read > log.txt` would otherwise buffer everything and
    // lose the log if the process dies mid-transfer.
    std::cout << std::unitbuf;

    std::vector<std::string> args(argv + 1, argv + argc);
    std::string command;
    std::vector<std::string> positionals;
    std::string modelFlag;
    std::string capturePath;  // --capture: log every USB transfer to a file
    std::optional<uint16_t> vid, pid;
    std::optional<std::pair<uint8_t, uint8_t>> address;
    bool raw = false;
    std::optional<uint8_t> iface;

    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--model") {
            if (++i >= args.size()) throw std::runtime_error("--model needs a value");
            modelFlag = args[i];
        } else if (a == "--vid") {
            if (++i >= args.size()) throw std::runtime_error("--vid needs a value");
            vid = (uint16_t)std::stoul(args[i], nullptr, 16);
        } else if (a == "--pid") {
            if (++i >= args.size()) throw std::runtime_error("--pid needs a value");
            pid = (uint16_t)std::stoul(args[i], nullptr, 16);
        } else if (a == "--address") {
            if (i + 2 >= args.size()) throw std::runtime_error("--address needs bus and dev");
            uint8_t bus = (uint8_t)std::stoul(args[++i], nullptr, 0);
            uint8_t dev = (uint8_t)std::stoul(args[++i], nullptr, 0);
            address = {bus, dev};
        } else if (a == "--interface") {
            if (++i >= args.size()) throw std::runtime_error("--interface needs a number");
            iface = (uint8_t)std::stoul(args[i], nullptr, 0);
        } else if (a == "--capture") {
            if (++i >= args.size()) throw std::runtime_error("--capture needs a file name");
            capturePath = args[i];
        } else if (a == "--raw") {
            raw = true;
        } else if (a == "-h" || a == "--help") {
            printHelp();
            return 0;
        } else if (a[0] == '-') {
            throw std::runtime_error("unknown option: " + a);
        } else if (command.empty()) {
            command = a;
        } else {
            positionals.push_back(a);
        }
    }

    if (command.empty()) {
        printHelp();
        return 1;
    }

    UsbOptions opts;
    opts.vendorId = vid;
    opts.productId = pid;
    opts.address = address;
    opts.interface = iface;
    std::optional<KeyboardModel> flagModel = modelFlag.empty() ? std::nullopt
                                                               : parseModelArg(modelFlag);
    std::string configPath = positionals.empty() ? "config.yaml" : positionals[0];

    // Capture also via CH57X_CAPTURE=<file>, e.g.
    //   sudo CH57X_CAPTURE=/tmp/board.captured ./build/ch57x-keyboard-tool read
    if (capturePath.empty()) {
        if (const char* env = std::getenv("CH57X_CAPTURE")) capturePath = env;
    }
    if (!capturePath.empty() && command != "decode") {
        if (enableCapture(capturePath))
            std::cout << "capturing every USB transfer to " << capturePath << "\n";
        else
            std::cerr << "warning: cannot write capture file " << capturePath << "\n";
    }

    try {
        if (command == "show-keys") {
            showKeys();
        } else if (command == "decode") {
            if (positionals.empty())
                throw std::runtime_error("usage: decode <capture-file> [config]");
            std::string cfgPath = positionals.size() > 1 ? positionals[1] : "config.yaml";
            decodeCommand(positionals[0], cfgPath, raw);
        } else if (command == "validate") {
            validateConfig(configPath);
        } else if (command == "dump") {
            dumpConfig(configPath, flagModel);
        } else if (command == "upload") {
            uploadConfig(configPath, opts, flagModel);
        } else if (command == "read") {
            readCommand(configPath, opts, flagModel, raw);
        } else if (command == "watch") {
            watchCommand(configPath, opts, flagModel);
        } else if (command == "led") {
            if (positionals.size() < 2)
                throw std::runtime_error("usage: led <layer> <mode> [color]");
            uint8_t layer = (uint8_t)std::stoul(positionals[0], nullptr, 0);
            std::string mode = positionals[1];
            std::string color = positionals.size() > 2 ? positionals[2] : "";
            ledCommand(layer, mode, color, opts, flagModel);
        } else {
            throw std::runtime_error("unknown command: " + command);
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}

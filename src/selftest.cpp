// selftest.cpp — no-USB unit tests: parser, byte encoding, key-id mapping,
// and config parsing/rendering. Run: ./ch57x-selftest
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "ch57x/capture.h"
#include "ch57x/codes.h"
#include "ch57x/config.h"
#include "ch57x/keyboard.h"
#include "ch57x/parser.h"
#include "ch57x/yaml.h"

using namespace ch57x;

namespace {

int g_checks = 0;
int g_fail = 0;

void check(bool cond, const std::string& what) {
    ++g_checks;
    if (!cond) {
        ++g_fail;
        std::cerr << "  FAIL: " << what << "\n";
    }
}
void checkEq(uint8_t got, uint8_t want, const std::string& what) {
    std::ostringstream os;
    os << what << "  (got 0x" << std::hex << (int)got << ", want 0x" << (int)want << ")";
    check(got == want, os.str());
}

void testParser() {
    std::cout << "[parser]\n";
    {
        Macro m = parseMacro("a");
        check(m.kind == Macro::Kind::Keyboard, "\"a\" is keyboard");
        check(m.kbe.accords.size() == 1, "\"a\" has 1 accord");
        check(m.kbe.accords[0].modifiers == 0x00, "\"a\" no modifiers");
        check(m.kbe.accords[0].code && m.kbe.accords[0].code->value == 0x04,
              "\"a\" code is 0x04");
    }
    {
        Macro m = parseMacro("ctrl-a");
        check(m.kbe.accords.size() == 1, "\"ctrl-a\" 1 accord");
        check(m.kbe.accords[0].modifiers == 0x01, "\"ctrl-a\" ctrl mask");
        check(m.kbe.accords[0].code->value == 0x04, "\"ctrl-a\" code 0x04");
    }
    {
        Macro m = parseMacro("a,ctrl-b,shift-c");
        check(m.kbe.accords.size() == 3, "\"a,ctrl-b,shift-c\" 3 accords");
        check(m.kbe.accords[0].modifiers == 0x00 && m.kbe.accords[0].code->value == 0x04,
              "1st = a");
        check(m.kbe.accords[1].modifiers == 0x01 && m.kbe.accords[1].code->value == 0x05,
              "2nd = ctrl-b");
        check(m.kbe.accords[2].modifiers == 0x02 && m.kbe.accords[2].code->value == 0x06,
              "3rd = shift-c");
    }
    {
        Macro m = parseMacro("{delay(120)}a");
        check(m.kbe.opts.delay == 120, "{delay(120)} sets 120ms");
        check(m.kbe.accords.size() == 1, "{delay(120)}a has 1 accord");
    }
    {
        Macro m = parseMacro("<110>");
        check(m.kind == Macro::Kind::Keyboard, "<110> is keyboard");
        check(m.kbe.accords[0].code->custom, "<110> is custom code");
        check(m.kbe.accords[0].code->value == 110, "<110> value 110");
    }
    {
        Macro m = parseMacro("mute");
        check(m.kind == Macro::Kind::Media, "mute is media");
        check(m.media == MediaCode::Mute, "mute == Mute");
    }
    {
        Macro m = parseMacro("volumedown");
        check(m.kind == Macro::Kind::Media, "volumedown is media");
        check(m.media == MediaCode::VolumeDown, "volumedown == VolumeDown");
    }
    {
        Macro m = parseMacro("click");
        check(m.kind == Macro::Kind::Mouse, "click is mouse");
        check(m.mouse.action.kind == MouseAction::Kind::Click, "click action=Click");
        check(m.mouse.action.buttons == 0x01, "click uses left button");
        check(!m.mouse.modifier, "click no modifier");
    }
    {
        Macro m = parseMacro("click(left+right)");
        check(m.mouse.action.buttons == 0x03, "click(left+right) buttons 0x03");
    }
    {
        Macro m = parseMacro("move(10,-20)");
        check(m.mouse.action.kind == MouseAction::Kind::Move, "move action=Move");
        check(m.mouse.action.dx == 10 && m.mouse.action.dy == -20, "move dx=10 dy=-20");
    }
    {
        Macro m = parseMacro("wheel(-3)");
        check(m.mouse.action.kind == MouseAction::Kind::Wheel, "wheel action=Wheel");
        check(m.mouse.action.wheel == -3, "wheel -3");
    }
    {
        Macro m = parseMacro("ctrl-click");
        check(m.mouse.action.kind == MouseAction::Kind::Click, "ctrl-click is click");
        check(m.mouse.modifier && *m.mouse.modifier == MouseModifier::Ctrl,
              "ctrl-click has ctrl modifier");
    }
    {
        bool threw = false;
        try { parseMacro("notarealkey"); } catch (const std::exception&) { threw = true; }
        check(threw, "invalid token throws");
    }
    {
        bool threw = false;
        try { parseMacro("ctrl-a "); } catch (const std::exception&) { threw = true; }
        check(threw, "trailing space (not fully consumed) throws");
    }
}

void testKeyId() {
    std::cout << "[toKeyId]\n";
    // Not directly exposed; exercise through bindKey output (key id = byte 2).
    auto drv = createDriver(KeyboardModel::Ch57x_1, 3, 0);
    {
        std::vector<uint8_t> out;
        drv->bindKey(0, Key::button(0), parseMacro("a"), out);
        checkEq(out[2], 0x01, "button 0 -> key id 1");
    }
    {
        std::vector<uint8_t> out;
        drv->bindKey(0, Key::button(2), parseMacro("a"), out);
        checkEq(out[2], 0x03, "button 2 -> key id 3");
    }
    {
        std::vector<uint8_t> out;
        drv->bindKey(1, Key::button(0), parseMacro("a"), out);
        checkEq(out[3], 0x02, "layer 1 -> layer byte 2");
    }
}

void testKeyIdKnob() {
    std::cout << "[toKeyId knobs]\n";
    auto drv = createDriver(KeyboardModel::Ch57x_1, 3, 4);  // 3 buttons (<=12), 4 knobs
    {
        std::vector<uint8_t> out;
        drv->bindKey(0, Key::knob(0, KnobAction::RotateCCW), parseMacro("a"), out);
        checkEq(out[2], 16, "knob 0 ccw -> key id 16");
    }
    {
        std::vector<uint8_t> out;
        drv->bindKey(0, Key::knob(0, KnobAction::Press), parseMacro("a"), out);
        checkEq(out[2], 17, "knob 0 press -> key id 17");
    }
    {
        std::vector<uint8_t> out;
        drv->bindKey(0, Key::knob(0, KnobAction::RotateCW), parseMacro("a"), out);
        checkEq(out[2], 18, "knob 0 cw -> key id 18");
    }
    {
        std::vector<uint8_t> out;
        drv->bindKey(0, Key::knob(3, KnobAction::RotateCCW), parseMacro("a"), out);
        checkEq(out[2], 13, "knob 3 ccw (4th knob, <=12 buttons) -> key id 13");
    }
    {
        bool threw = false;
        try {
            std::vector<uint8_t> out;
            drv->bindKey(0, Key::knob(4, KnobAction::Press), parseMacro("a"), out);
        } catch (const std::exception&) { threw = true; }
        check(threw, "knob index 4 is invalid");
    }
}

void testEncoding() {
    std::cout << "[encoding]\n";
    auto drv = createDriver(KeyboardModel::Ch57x_1, 3, 0);
    {
        // "a" on button 0, layer 0 -> 4 reports (256 bytes).
        std::vector<uint8_t> out;
        drv->bindKey(0, Key::button(0), parseMacro("a"), out);
        check(out.size() == 256, "\"a\" produces 4 reports (256 bytes)");
        checkEq(out[0], 0x03, "bind report id 0x03");
        checkEq(out[1], 0xfe, "bind op 0xfe");
        checkEq(out[2], 0x01, "key id");
        checkEq(out[3], 0x01, "layer+1");
        checkEq(out[4], 0x01, "kind=keyboard");
        checkEq(out[10], 0x01, "n accords");
        checkEq(out[11], 0x00, "modifiers");
        checkEq(out[12], 0x04, "code A");
        checkEq(out[64], 0x03, "finish aa report id");
        checkEq(out[65], 0xaa, "finish aa[1]");
        checkEq(out[66], 0xaa, "finish aa[2]");
        checkEq(out[128], 0x03, "commit report id");
        checkEq(out[129], 0xfd, "commit op");
        checkEq(out[130], 0xfe, "commit arg");
        checkEq(out[131], 0xff, "commit arg");
        checkEq(out[192], 0x03, "final aa report id");
        checkEq(out[193], 0xaa, "final aa[1]");
    }
    {
        // "ctrl-a": modifiers byte should be 0x01.
        std::vector<uint8_t> out;
        drv->bindKey(0, Key::button(0), parseMacro("ctrl-a"), out);
        checkEq(out[11], 0x01, "\"ctrl-a\" modifiers 0x01");
    }
    {
        // "a,ctrl-b": n=2, then (0,0x04),(0x01,0x05).
        std::vector<uint8_t> out;
        drv->bindKey(0, Key::button(0), parseMacro("a,ctrl-b"), out);
        checkEq(out[10], 0x02, "\"a,ctrl-b\" n=2");
        checkEq(out[11], 0x00, "accord1 mod");
        checkEq(out[12], 0x04, "accord1 code a");
        checkEq(out[13], 0x01, "accord2 mod ctrl");
        checkEq(out[14], 0x05, "accord2 code b");
    }
    {
        // "mute" (media 0xe2): kind=2, payload [0, 0xe2, 0x00, ...].
        std::vector<uint8_t> out;
        drv->bindKey(0, Key::button(0), parseMacro("mute"), out);
        checkEq(out[4], 0x02, "media kind byte");
        checkEq(out[10], 0x00, "media payload first byte");
        checkEq(out[11], 0xe2, "media code low");
        checkEq(out[12], 0x00, "media code high");
    }
    {
        // "click" (mouse left): kind=3, payload [0x01, 0, 0x01].
        std::vector<uint8_t> out;
        drv->bindKey(0, Key::button(0), parseMacro("click"), out);
        checkEq(out[4], 0x03, "mouse kind byte");
        checkEq(out[10], 0x01, "mouse click action id");
        checkEq(out[11], 0x00, "mouse mod");
        checkEq(out[12], 0x01, "mouse buttons left");
    }
    {
        // delay: "{delay(50)}a" -> 5 reports; delay is the 2nd report (offset 64).
        std::vector<uint8_t> out;
        drv->bindKey(0, Key::button(0), parseMacro("{delay(50)}a"), out);
        check(out.size() == 320, "{delay(50)}a produces 5 reports (320 bytes)");
        checkEq(out[64], 0x03, "delay report id");
        checkEq(out[65], 0xfe, "delay op");
        checkEq(out[66], 0x01, "delay key id");
        checkEq(out[67], 0x01, "delay layer+1");
        checkEq(out[68], 0x05, "delay kind");
        checkEq(out[69], 50, "delay low byte");
        checkEq(out[70], 0x00, "delay high byte");
    }
}

// describeRecord() must invert what bindKey() encodes (encoder <-> decoder
// round-trip; no hardware needed).
void testDecode() {
    std::cout << "[decode]\n";
    auto drv = createDriver(KeyboardModel::Ch57x_1, 3, 0);
    auto first = [&](uint8_t layer, const Key& key, const std::string& expr,
                     size_t report = 0) {
        std::vector<uint8_t> out;
        drv->bindKey(layer, key, parseMacro(expr), out);
        return describeRecord(std::vector<uint8_t>(out.begin() + report * 64,
                                                   out.begin() + report * 64 + 64),
                              1, 3, 0);
    };
    check(first(0, Key::button(0), "a") == "key 1 (left) layer 1 -> a",
          "keyboard a decodes by name: " + first(0, Key::button(0), "a"));
    check(first(0, Key::button(0), "ctrl-a") == "key 1 (left) layer 1 -> ctrl-a",
          "modifier decodes: " + first(0, Key::button(0), "ctrl-a"));
    check(first(1, Key::button(2), "a,ctrl-b") == "key 3 (right) layer 2 -> a, ctrl-b",
          "sequence + layer decode: " + first(1, Key::button(2), "a,ctrl-b"));
    check(first(0, Key::button(1), "mute") == "key 2 (middle) layer 1 -> mute",
          "media decodes by name: " + first(0, Key::button(1), "mute"));
    check(first(0, Key::button(0), "click") == "key 1 (left) layer 1 -> click(left)",
          "mouse click decodes: " + first(0, Key::button(0), "click"));
    check(first(0, Key::button(0), "move(10,-20)") == "key 1 (left) layer 1 -> move(10,-20)",
          "mouse move decodes: " + first(0, Key::button(0), "move(10,-20)"));
    check(first(0, Key::button(0), "wheel(-3)") == "key 1 (left) layer 1 -> wheel(-3)",
          "mouse wheel decodes: " + first(0, Key::button(0), "wheel(-3)"));
    check(first(0, Key::button(0), "{delay(50)}a", 1) == "key 1 (left) layer 1 -> delay 50 ms",
          "delay record decodes: " + first(0, Key::button(0), "{delay(50)}a", 1));

    // Raw wire bytes -> names, independent of the encoder.
    auto raw = [](std::initializer_list<int> bytes) {
        return std::vector<uint8_t>(bytes.begin(), bytes.end());
    };
    check(describeRecord(raw({0x03, 0xfe, 0x01, 0x01, 0x02, 0, 0, 0, 0, 0, 0x00, 0xea, 0x00}),
                         1, 3, 0) == "key 1 (left) layer 1 -> volumedown",
          "raw volumedown record decodes by name");
    check(describeRecord(raw({0x03, 0xfe, 0x03, 0x01, 0x02, 0, 0, 0, 0, 0, 0x00, 0xe2, 0x00}),
                         1, 3, 0) == "key 3 (right) layer 1 -> mute",
          "raw mute record decodes by name");
    check(describeRecord(raw({0x03, 0xfe, 0x02, 0x02, 0x01, 0, 0, 0, 0, 0, 0x01, 0x01, 0x04}),
                         1, 3, 0) == "key 2 (middle) layer 2 -> ctrl-a",
          "raw ctrl-a record decodes by name");

    // Key ids 16..18 are knob 1 ccw/press/cw.
    auto drvKnob = createDriver(KeyboardModel::Ch57x_1, 2, 1);
    std::vector<uint8_t> out;
    drvKnob->bindKey(0, Key::knob(0, KnobAction::Press), parseMacro("mute"), out);
    check(describeRecord(std::vector<uint8_t>(out.begin(), out.begin() + 64), 1, 2, 1) ==
              "knob 1 press layer 1 -> mute",
          "knob key id decodes");

    // LED block record (key id 0xb0).
    out.clear();
    drv->setLed(0, "backlight red", out);
    check(describeRecord(std::vector<uint8_t>(out.begin(), out.begin() + 64), 1, 3, 0) ==
              "LED block layer 1 -> led backlight red",
          "led record decodes");

    // Hardware read-back records echo the read opcode 0xfa instead of 0xfe
    // (captured from a 514c:8851 board running the legacy firmware).
    check(describeRecord(raw({0x03, 0xfa, 0x01, 0x01, 0x01, 0, 0, 0, 0, 0, 0x01, 0x00, 0x04}),
                         1, 3, 0) == "key 1 (left) layer 1 -> a",
          "read-back record (opcode 0xfa) decodes");
    check(describeRecord(raw({0x03, 0xfa, 0x05, 0x01, 0x01, 0, 0, 0, 0, 0, 0x01, 0x00, 0x08}),
                         1, 3, 0) == "slot 5 (outside the 1x3 grid) layer 1 -> e",
          "slot outside the physical grid is named as a slot");
    check(describeRecord(raw({0x03, 0xfa, 0x10, 0x01, 0x01, 0, 0, 0, 0, 0, 0x01, 0x00, 0x20}),
                         1, 3, 0) == "slot 16 (knob range, board has no knobs) layer 1 -> 3",
          "knob-range slot is a plain slot when the board has no knobs");
    check(describeRecord(raw({0x03, 0xfa, 0x10, 0x01, 0x01, 0, 0, 0, 0, 0, 0x01, 0x00, 0x20}),
                         1, 3, 1) == "knob 1 ccw layer 1 -> 3",
          "the same id is knob 1 ccw when the board has knobs");

    // Not binding records.
    check(describeRecord(std::vector<uint8_t>(64, 0), 1, 3, 0).empty(), "zero report ignored");
    // One-byte 00 acknowledgement of an OUT report: a real transfer, not a record.
    check(describeRecord(raw({0x00}), 1, 3, 0).empty(), "one-byte ack is not a record");
    check(describeRecord(raw({0x03, 0xfa, 0x12, 0x01, 0x01, 0, 0, 0, 0, 0, 0x01, 0x00, 0x1e}),
                         1, 3, 0) == "slot 18 (knob range, board has no knobs) layer 1 -> 1",
          "record as captured on the wire decodes");
    std::vector<uint8_t> commit(64, 0);
    commit[0] = 0x03; commit[1] = 0xfd; commit[2] = 0xfe; commit[3] = 0xff;
    check(describeRecord(commit, 1, 3, 0).empty(), "commit report ignored");
}

// A capture file must round-trip: what the wire saw is what `decode` replays.
void testCapture() {
    std::cout << "[capture]\n";
    const std::string path = "ch57x-capture-selftest.bin";
    check(enableCapture(path), "capture file opens");
    const std::vector<uint8_t> req = {0x03, 0xfa, 0x0f, 0x03, 0x01};
    // Bind record as the 0xFA read returns it: kind 1 keyboard, count at 10,
    // (modifier, code) pairs from 11 -> plain "a".
    const std::vector<uint8_t> rep = {0x03, 0xfa, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00,
                                      0x00, 0x01, 0x00, 0x04};
    // The firmware acknowledges the OUT report with a one-byte 00 IN report; that
    // is a real transfer and must not be recorded like a timed-out (zero-length) one.
    const std::vector<uint8_t> ack = {0x00};
    captureOut(req.data(), req.size());
    captureIn(rep.data(), rep.size());
    captureIn(ack.data(), ack.size());
    captureIn(nullptr, 0);  // a timed-out read: marks the end of a reply stream
    closeCapture();

    auto frames = readCapture(path);
    check(frames.size() == 4, "four frames written");
    if (frames.size() == 4) {
        check(frames[0].dir == 'O' && frames[0].data == req, "OUT frame round-trips");
        check(frames[1].dir == 'I' && frames[1].data == rep, "IN frame round-trips");
        check(frames[2].dir == 'I' && frames[2].data == ack, "one-byte ack frame round-trips");
        check(!frames[2].isTimeout(), "one-byte ack is not mistaken for a timeout");
        check(frames[3].isTimeout() && frames[3].data.empty(), "timeout frame round-trips");
        check(frames[0].seq == 0 && frames[1].seq == 1 && frames[3].seq == 3,
              "frames carry a transfer sequence");
        // The recorded IN report is what the parser later sees.
        auto info = decodeBind(frames[1].data, 1, 3, 0);
        check(info.valid && info.action == "a", "captured report decodes");
    }
    std::remove(path.c_str());

    const std::string bad = "ch57x-capture-bad-selftest.bin";
    {
        std::FILE* f = std::fopen(bad.c_str(), "wb");
        if (f) {
            std::fwrite("NOTACAPTURE", 1, 11, f);
            std::fclose(f);
        }
    }
    bool threw = false;
    try {
        readCapture(bad);
    } catch (const std::exception&) {
        threw = true;
    }
    check(threw, "malformed capture rejected");
    std::remove(bad.c_str());
}

void testLed() {
    std::cout << "[led]\n";
    auto drv = createDriver(KeyboardModel::Ch57x_1, 3, 0);
    {
        // off -> code 0
        std::vector<uint8_t> out;
        drv->setLed(0, "off", out);
        checkEq(out[12], 0x00, "led off code 0x00");
        checkEq(out[2], 0xb0, "led key 0xb0");
        checkEq(out[3], 0x01, "led layer+1");
    }
    {
        // backlight red -> color 1, mode 1 -> 0x11
        std::vector<uint8_t> out;
        drv->setLed(0, "backlight red", out);
        checkEq(out[12], 0x11, "backlight red code 0x11");
    }
    {
        // backlight white -> special 0x05
        std::vector<uint8_t> out;
        drv->setLed(0, "backlight white", out);
        checkEq(out[12], 0x05, "backlight white code 0x05");
    }
    {
        // shock green -> color 4, mode 2 -> 0x42
        std::vector<uint8_t> out;
        drv->setLed(0, "shock green", out);
        checkEq(out[12], 0x42, "shock green code 0x42");
    }
    {
        // press purple -> color 7, mode 4 -> 0x74
        std::vector<uint8_t> out;
        drv->setLed(0, "press purple", out);
        checkEq(out[12], 0x74, "press purple code 0x74");
    }
}

void testRender() {
    std::cout << "[config parse + render]\n";
    {
        std::string y = "model: ch57x-1\n"
                        "orientation: normal\n"
                        "rows: 1\n"
                        "columns: 3\n"
                        "knobs: 0\n"
                        "layers:\n"
                        "  - buttons:\n"
                        "      - [volumedown, mute, volumeup]\n";
        Config cfg = parseYamlConfig(y);
        check(cfg.model && *cfg.model == KeyboardModel::Ch57x_1, "model parsed");
        check(cfg.rows == 1 && cfg.columns == 3, "grid 1x3");
        check(cfg.knobs == 0, "no knobs");
        auto layers = render(cfg);
        check(layers.size() == 1, "1 layer");
        check(layers[0].buttons.size() == 3, "3 buttons in flat layer");
        check(layers[0].buttons[0]->kind == Macro::Kind::Media &&
                  layers[0].buttons[0]->media == MediaCode::VolumeDown,
              "key1 = volumedown");
        check(layers[0].buttons[1]->media == MediaCode::Mute, "key2 = mute");
        check(layers[0].buttons[2]->media == MediaCode::VolumeUp, "key3 = volumeup");
    }
    {
        // Upside-down 1x3 reverses the row: [a,b,c] -> [c,b,a].
        std::string y = "model: ch57x-1\norientation: upsidedown\nrows: 1\ncolumns: 3\nknobs: 0\n"
                        "layers:\n  - buttons:\n      - [a, b, c]\n";
        auto layers = render(parseYamlConfig(y));
        check(layers[0].buttons[0]->kbe.accords[0].code->value == 0x06, "upsidedown key1 = c");
        check(layers[0].buttons[1]->kbe.accords[0].code->value == 0x05, "upsidedown key2 = b");
        check(layers[0].buttons[2]->kbe.accords[0].code->value == 0x04, "upsidedown key3 = a");
    }
    {
        // Clockwise on a 2x2 grid; matches the reference transform formula:
        // data [[a,b],[c,d]] -> [b, d, a, c].
        std::string y = "model: ch57x-1\norientation: clockwise\nrows: 2\ncolumns: 2\nknobs: 0\n"
                        "layers:\n  - buttons:\n      - [a, b]\n      - [c, d]\n";
        auto layers = render(parseYamlConfig(y));
        check(layers[0].buttons.size() == 4, "2x2 -> 4 flat buttons");
        check(layers[0].buttons[0]->kbe.accords[0].code->value == 0x05, "clockwise [0]=b");
        check(layers[0].buttons[1]->kbe.accords[0].code->value == 0x07, "clockwise [1]=d");
        check(layers[0].buttons[2]->kbe.accords[0].code->value == 0x04, "clockwise [2]=a");
        check(layers[0].buttons[3]->kbe.accords[0].code->value == 0x06, "clockwise [3]=c");
    }
    {
        // Sparse layout: a null cell is allowed and stays unbound.
        std::string y = "model: ch57x-1\norientation: normal\nrows: 1\ncolumns: 2\nknobs: 0\n"
                        "layers:\n  - buttons:\n      - [a, null]\n";
        auto layers = render(parseYamlConfig(y));
        check(layers[0].buttons.size() == 2, "sparse: 2 cells");
        check(layers[0].buttons[0].has_value(), "sparse: cell 0 bound");
        check(!layers[0].buttons[1].has_value(), "sparse: cell 1 null");
    }
    {
        // Invalid knob count for the layout should throw from render/createDriver.
        bool threw = false;
        try {
            createDriver(KeyboardModel::Ch57x_1, 16, 3);  // 16 buttons > 15 with 3 knobs
        } catch (const std::exception&) { threw = true; }
        check(threw, "16 buttons + 3 knobs is rejected");
    }
}

// Direct coverage of the name<->value lookup tables in codes.cpp (the old
// null-deref crash site). Exercises both directions, case handling, and aliases.
void testCodes() {
    std::cout << "[codes]\n";
    // Modifiers
    check(modifierFromName("ctrl") == Modifier::Ctrl, "mod ctrl");
    check(modifierFromName("ALT") == Modifier::Alt, "mod ALT (case)");
    check(modifierFromName("opt") == Modifier::Alt, "mod opt -> Alt");
    check(modifierFromName("rcmd") == Modifier::RightWin, "mod rcmd -> RightWin");
    check(!modifierFromName("bogus").has_value(), "mod bogus -> nullopt");
    check(modifierName(Modifier::Win) == "win", "modName Win");
    check(modifierAliases(Modifier::Alt) == "alt / opt", "modAliases Alt");
    // Well-known keys
    check(wellKnownFromName("a") == WellKnownCode::A, "wk a");
    check(wellKnownFromName("Z") == WellKnownCode::Z, "wk Z (case)");
    check(wellKnownFromName("1") == WellKnownCode::N1, "wk 1");
    check(wellKnownFromName("enter") == WellKnownCode::Enter, "wk enter");
    check(wellKnownFromName("f24") == WellKnownCode::F24, "wk f24");
    check(!wellKnownFromName("xk").has_value(), "wk bogus -> nullopt");
    check(wellKnownName(WellKnownCode::A) == "a", "wkName a");
    check(wellKnownName(WellKnownCode::F1) == "f1", "wkName f1");
    // Media (the .b alias field that was previously compared against nullptr)
    check(mediaFromName("volumedown") == MediaCode::VolumeDown, "media volumedown");
    check(mediaFromName("mute") == MediaCode::Mute, "media mute");
    check(mediaFromName("prev") == MediaCode::Previous, "media prev -> Previous (alias)");
    check(!mediaFromName("noper").has_value(), "media bogus -> nullopt");
    check(mediaName(MediaCode::VolumeDown) == "volumedown", "mediaName volumedown");
    check(mediaAliases(MediaCode::Previous) == "previous / prev", "mediaAliases previous");
    // Mouse
    check(mouseModifierFromName("shift") == MouseModifier::Shift, "mm shift");
    check(mouseButtonBitFromName("left") == MouseButton::Left, "mb left");
    check(mouseButtonBitFromName("middle") == MouseButton::Middle, "mb middle");
    check(mouseButtonName(MouseButton::Left | MouseButton::Right) == "left+right", "mbName left+right");
    // Code / Key display (exercises the code name table)
    check(Code::wellKnown(WellKnownCode::A).toString() == "a", "Code wellKnown a -> 'a'");
    check(Code::customCode(110).toString() == "<110>", "Code custom 110 -> '<110>'");
    check(Key::button(2).toString() == "button 2", "Key button 2");
    check(Key::knob(1, KnobAction::Press).toString() == "knob 1 press", "Key knob 1 press");
}

// is_limited = (rows==1 || columns==1) && knobs==1  ->  modifiers only on the
// FIRST key of a sequence (matches Rust config.rs::is_limited).
void testLimited() {
    std::cout << "[limited-layout]\n";
    auto knob = std::string("\n    knobs:\n      - ccw: a\n        press: b\n        cw: c");
    {   // 1-row + 1-knob: modifier on the 2nd key of a sequence -> rejected
        std::string y = "model: ch57x-1\norientation: normal\nrows: 1\ncolumns: 2\nknobs: 1\n"
                        "layers:\n  - buttons:\n      - [\"a,ctrl-b\", c]" + knob;
        bool threw = false;
        try { render(parseYamlConfig(y)); } catch (const std::exception&) { threw = true; }
        check(threw, "limited: a,ctrl-b rejected");
    }
    {   // 1-row + 1-knob: modifier only on the 1st key -> accepted
        std::string y = "model: ch57x-1\norientation: normal\nrows: 1\ncolumns: 2\nknobs: 1\n"
                        "layers:\n  - buttons:\n      - [\"ctrl-a,b\", c]" + knob;
        bool threw = false;
        try { render(parseYamlConfig(y)); } catch (const std::exception&) { threw = true; }
        check(!threw, "limited: ctrl-a,b accepted");
    }
    {   // 1-row + 0-knob: NOT limited -> modifier on 2nd key is fine
        std::string y = "model: ch57x-1\norientation: normal\nrows: 1\ncolumns: 2\nknobs: 0\n"
                        "layers:\n  - buttons:\n      - [\"a,ctrl-b\", c]";
        bool threw = false;
        try { render(parseYamlConfig(y)); } catch (const std::exception&) { threw = true; }
        check(!threw, "not limited: a,ctrl-b accepted");
    }
}

}  // namespace

int main() {
    testCodes();
    testParser();
    testKeyId();
    testKeyIdKnob();
    testEncoding();
    testDecode();
    testCapture();
    testLed();
    testRender();
    testLimited();
    std::cout << (g_fail == 0 ? "PASS" : "FAIL") << ": " << (g_checks - g_fail) << "/"
              << g_checks << " checks passed\n";
    return g_fail == 0 ? 0 : 1;
}

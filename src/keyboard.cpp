#include "ch57x/keyboard.h"

#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace ch57x {

namespace {

// Pad msg to 64 bytes and append to output (port of send_message).
void sendMessage(std::vector<uint8_t>& output, std::vector<uint8_t> msg) {
    if (msg.size() > 64) throw std::runtime_error("message too long (max 64)");
    msg.resize(64, 0);
    output.insert(output.end(), msg.begin(), msg.end());
}

}  // namespace

Keyboard884x::Keyboard884x(uint8_t buttons, uint8_t knobs) : buttons_(buttons), knobs_(knobs) {
    if (!((buttons <= 15 && knobs <= 3) || (buttons <= 12 && knobs <= 4)))
        throw std::runtime_error("unsupported combination of buttons and knobs count");
}

uint8_t Keyboard884x::toKeyId(const Key& key) const {
    switch (key.kind) {
        case Key::Kind::Button: {
            uint8_t n = key.index;
            if (n >= 15) throw std::runtime_error("invalid key index");
            if (n >= 12 && knobs_ == 4) throw std::runtime_error("invalid key index");
            return n + 1;
        }
        case Key::Kind::Knob: {
            uint8_t n = key.index;
            KnobAction a = key.knobAction;
            if (n == 3 && buttons_ <= 12) return 13 + static_cast<uint8_t>(a);
            if (n >= 3) throw std::runtime_error("invalid knob index");
            return static_cast<uint8_t>(16 + 3 * n + static_cast<uint8_t>(a));  // MAX(15) + 1
        }
    }
    return 0;
}

std::string keyIdName(uint8_t keyId, uint8_t rows, uint8_t cols, uint8_t knobs) {
    static const char* kAction[] = {"ccw", "press", "cw"};
    if (keyId == 0xb0) return "LED block";
    if (keyId >= 16 && keyId <= 24) {
        if (knobs == 0)  // storage slot in the knob range, no knob on this board
            return "slot " + std::to_string(keyId) + " (knob range, board has no knobs)";
        return "knob " + std::to_string((keyId - 16) / 3 + 1) + " " + kAction[(keyId - 16) % 3];
    }
    if (keyId >= 13 && keyId <= 15 && knobs == 4)
        return std::string("knob 4 ") + kAction[keyId - 13];
    if (keyId >= 1 && keyId <= 15) {
        uint8_t idx = keyId - 1;
        std::string pos = positionName(idx, rows, cols);
        if (!pos.empty())
            return "key " + std::to_string(idx + 1) + " (" + pos + ")";
        return "slot " + std::to_string(keyId) + " (outside the " + std::to_string(rows) + "x" +
               std::to_string(cols) + " grid)";
    }
    return "key id " + std::to_string(keyId);
}

void Keyboard884x::bindKey(uint8_t layer, const Key& key, const Macro& expansion,
                           std::vector<uint8_t>& output) {
    if (layer > 15) throw std::runtime_error("layer index out of range");
    std::vector<uint8_t> msg = {0x03, 0xfe, toKeyId(key), static_cast<uint8_t>(layer + 1),
                                expansion.kindByte(), 0, 0, 0, 0, 0};
    switch (expansion.kind) {
        case Macro::Kind::Keyboard: {
            const auto& kbe = expansion.kbe;
            if (kbe.accords.size() > 18)
                throw std::runtime_error("too many keys in key sequence");
            if (kbe.accords.size() == 1 && !kbe.accords[0].code)
                msg.push_back(0);
            else
                msg.push_back(static_cast<uint8_t>(kbe.accords.size()));
            for (const auto& a : kbe.accords) {
                msg.push_back(a.modifiers);
                msg.push_back(a.code ? a.code->value : 0);
            }
            break;
        }
        case Macro::Kind::Media: {
            uint16_t c = static_cast<uint16_t>(expansion.media);
            msg.push_back(0);
            msg.push_back(c & 0xff);
            msg.push_back((c >> 8) & 0xff);
            msg.push_back(0);
            msg.push_back(0);
            msg.push_back(0);
            msg.push_back(0);
            break;
        }
        case Macro::Kind::Mouse: {
            const auto& me = expansion.mouse;
            uint8_t mod = me.modifier ? static_cast<uint8_t>(*me.modifier) : 0;
            switch (me.action.kind) {
                case MouseAction::Kind::Move:
                    msg.insert(msg.end(), {0x05, mod, 0,
                                           static_cast<uint8_t>(me.action.dx),
                                           static_cast<uint8_t>(me.action.dy)});
                    break;
                case MouseAction::Kind::Drag:
                    msg.insert(msg.end(), {0x05, mod, me.action.buttons,
                                           static_cast<uint8_t>(me.action.dx),
                                           static_cast<uint8_t>(me.action.dy)});
                    break;
                case MouseAction::Kind::Click:
                    if (me.action.buttons == 0)
                        throw std::runtime_error("at least one mouse button required");
                    msg.insert(msg.end(), {0x01, mod, me.action.buttons});
                    break;
                case MouseAction::Kind::Wheel:
                    msg.insert(msg.end(), {0x03, mod, 0, 0, 0,
                                           static_cast<uint8_t>(me.action.wheel)});
                    break;
            }
            break;
        }
    }
    sendMessage(output, msg);
    if (expansion.kind == Macro::Kind::Keyboard && expansion.kbe.opts.delay != 0) {
        if (expansion.kbe.opts.delay > 6000)
            throw std::runtime_error("delay is limited to 6000ms");
        uint16_t d = expansion.kbe.opts.delay;
        sendMessage(output, {0x03, 0xfe, toKeyId(key), static_cast<uint8_t>(layer + 1), 5,
                             static_cast<uint8_t>(d & 0xff), static_cast<uint8_t>((d >> 8) & 0xff)});
    }
    // Per-key commit (matches the Rust reference exactly).
    sendMessage(output, {0x03, 0xaa, 0xaa, 0, 0, 0, 0, 0, 0});
    sendMessage(output, {0x03, 0xfd, 0xfe, 0xff});
    sendMessage(output, {0x03, 0xaa, 0xaa, 0, 0, 0, 0, 0, 0});
}

LedSpec parseLedMode(const std::string& s) {
    auto isColor = [](const std::string& c) -> uint8_t {
        if (c == "red") return 1;
        if (c == "orange") return 2;
        if (c == "yellow") return 3;
        if (c == "green") return 4;
        if (c == "cyan") return 5;
        if (c == "blue") return 6;
        if (c == "purple") return 7;
        return 255;
    };
    auto isBacklightColor = [&isColor](const std::string& c) -> uint8_t {
        if (c == "white") return 0;
        return isColor(c);
    };
    if (s == "off") return {LedMode::Off, 0};
    if (s.rfind("backlight ", 0) == 0) {
        uint8_t c = isBacklightColor(s.substr(10));
        if (c == 255) throw std::runtime_error("unsupported led color: " + s.substr(10));
        return {LedMode::Backlight, c};
    }
    // "shock2 " must be checked before "shock " (prefix).
    if (s.rfind("shock2 ", 0) == 0) {
        uint8_t c = isColor(s.substr(7));
        if (c == 255) throw std::runtime_error("unsupported led color: " + s.substr(7));
        return {LedMode::Shock2, c};
    }
    if (s.rfind("shock ", 0) == 0) {
        uint8_t c = isColor(s.substr(6));
        if (c == 255) throw std::runtime_error("unsupported led color: " + s.substr(6));
        return {LedMode::Shock, c};
    }
    if (s.rfind("press ", 0) == 0) {
        uint8_t c = isColor(s.substr(6));
        if (c == 255) throw std::runtime_error("unsupported led color: " + s.substr(6));
        return {LedMode::Press, c};
    }
    throw std::runtime_error("unsupported led mode: " + s);
}

uint8_t ledCode(const LedSpec& s) {
    switch (s.mode) {
        case LedMode::Off: return 0;
        case LedMode::Backlight:
            // White backlight uses mode 5 with color 0 (per the vendor tool).
            if (s.color == 0) return 0x05;
            return static_cast<uint8_t>((s.color << 4) | 1);
        case LedMode::Shock: return static_cast<uint8_t>((s.color << 4) | 2);
        case LedMode::Shock2: return static_cast<uint8_t>((s.color << 4) | 3);
        case LedMode::Press: return static_cast<uint8_t>((s.color << 4) | 4);
    }
    return 0;
}

void Keyboard884x::setLed(uint8_t layer, const std::string& mode,
                          std::vector<uint8_t>& output) {
    auto spec = parseLedMode(mode);
    uint8_t code = ledCode(spec);
    std::vector<uint8_t> msg = {0x03, 0xfe, 0xb0, static_cast<uint8_t>(layer + 1), 0x08,
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, code};
    sendMessage(output, msg);
    sendMessage(output, {0x03, 0xfd, 0xfe, 0xff});
}

std::unique_ptr<Keyboard> createDriver(KeyboardModel model, uint8_t buttons, uint8_t knobs) {
    switch (model) {
        case KeyboardModel::Ch57x_1:
            return std::make_unique<Keyboard884x>(buttons, knobs);
        case KeyboardModel::Ch57x_2:
        case KeyboardModel::Ch57x_3:
            throw std::runtime_error(
                "driver for this model not yet ported (ch57x-1 / 514c:8851 is supported)");
    }
    return nullptr;
}

// Reverse of ledCode(), display only.
static std::string ledModeName(uint8_t code) {
    static const char* kColor[] = {"white", "red", "orange", "yellow", "green", "cyan",
                                   "blue", "purple"};
    uint8_t mode = code & 0x0f, color = code >> 4;
    std::string cname = color < 8 ? kColor[color] : "?";
    switch (mode) {
        case 0: return "off";
        case 1: return "backlight " + cname;
        case 2: return "shock " + cname;
        case 3: return "shock2 " + cname;
        case 4: return "press " + cname;
        case 5: return "backlight white";
    }
    std::ostringstream ss;
    ss << "mode 0x" << std::hex << (int)code << std::dec;
    return ss.str();
}

// See decodeBind() below for the record layout. Read-back records carry the
// read opcode 0xfa in byte 1 (the upload/write form uses 0xfe).
static bool isBindRecord(const std::vector<uint8_t>& d) {
    return d.size() >= 5 && d[0] == 0x03 && (d[1] == 0xfe || d[1] == 0xfa);
}

BindInfo decodeBind(const std::vector<uint8_t>& d, uint8_t rows, uint8_t cols,
                    uint8_t knobs) {
    BindInfo info;
    if (!isBindRecord(d)) return info;
    info.valid = true;
    info.keyId = d[2];
    info.layer = d[3];
    info.kind = d[4];
    info.keyName = keyIdName(info.keyId, rows, cols, knobs);
    std::ostringstream ss;
    auto hexByte = [](uint8_t b) {
        std::ostringstream h;
        h << "0x" << std::hex << std::setw(2) << std::setfill('0') << (int)b << std::dec;
        return h.str();
    };
    switch (info.kind) {
        case 1: {  // keyboard sequence
            if (d.size() >= 13)
                info.detail = std::string("HID usage ") + hexByte(d[12]) + " (keyboard page)";
            if (d.size() < 13) { ss << "keyboard (short record)"; break; }
            uint8_t n = d[10];
            if (n == 0) n = 1;  // a lone modifier chord is written with count 0
            for (uint8_t i = 0; i < n && static_cast<size_t>(12) + 2 * i < d.size(); ++i) {
                std::string mods = modifierMaskName(d[11 + 2 * i]);
                if (i) ss << ", ";
                if (!mods.empty()) ss << mods << "-";
                ss << wellKnownNameForValue(d[12 + 2 * i]);
            }
            break;
        }
        case 2: {  // media key
            if (d.size() < 13) { ss << "media (short record)"; break; }
            const uint16_t consumer = static_cast<uint16_t>(d[11] | (d[12] << 8));
            ss << mediaNameForCode(consumer);
            info.detail = "HID usage " + hexByte((uint8_t)consumer) + " (consumer page)";
            break;
        }
        case 3: {  // mouse
            info.detail = "mouse action";
            if (d.size() < 13) { ss << "mouse (short record)"; break; }
            std::string prefix;
            if (d[11] & 0x01) prefix += "ctrl-";
            if (d[11] & 0x02) prefix += "shift-";
            if (d[11] & 0x04) prefix += "alt-";
            switch (d[10]) {
                case 0x01: ss << prefix << "click(" << mouseButtonName(d[12]) << ")"; break;
                case 0x03: {
                    int8_t delta = d.size() >= 16 ? static_cast<int8_t>(d[15]) : 0;
                    ss << prefix << "wheel(" << (int)delta << ")";
                    break;
                }
                case 0x05: {
                    int8_t dx = static_cast<int8_t>(d[13]), dy = static_cast<int8_t>(d[14]);
                    if (d[12])
                        ss << prefix << "drag(" << mouseButtonName(d[12]) << ","
                           << (int)dx << "," << (int)dy << ")";
                    else
                        ss << prefix << "move(" << (int)dx << "," << (int)dy << ")";
                    break;
                }
                default: ss << "mouse action " << hexByte(d[10]); break;
            }
            break;
        }
        case 5: {  // inter-key delay attached to the key above
            info.detail = "delay between the actions above";
            if (d.size() < 7) { ss << "delay (short record)"; break; }
            ss << "delay " << (d[5] | (d[6] << 8)) << " ms";
            break;
        }
        case 8: {  // LED block (keyID 0xb0): mode byte at 12
            info.detail = "LED settings block";
            if (d.size() < 13) { ss << "led (short record)"; break; }
            ss << "led " << ledModeName(d[12]);
            break;
        }
        default: ss << "kind " << hexByte(info.kind) << " (not decoded)"; break;
    }
    info.action = ss.str();
    return info;
}

std::string BindInfo::line() const {
    if (!valid) return "";
    return keyName + " layer " + std::to_string(layer) + " -> " + action;
}

std::string describeRecord(const std::vector<uint8_t>& d, uint8_t rows, uint8_t cols,
                           uint8_t knobs) {
    return decodeBind(d, rows, cols, knobs).line();
}

}  // namespace ch57x

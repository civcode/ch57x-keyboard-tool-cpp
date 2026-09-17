#include "ch57x/codes.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace ch57x {

namespace {

std::string toLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

struct Mod { Modifier c; const char* a; const char* b; };
const Mod kModifiers[] = {
    {Modifier::Ctrl, "ctrl", nullptr},
    {Modifier::Shift, "shift", nullptr},
    {Modifier::Alt, "alt", "opt"},
    {Modifier::Win, "win", "cmd"},
    {Modifier::RightCtrl, "rctrl", nullptr},
    {Modifier::RightShift, "rshift", nullptr},
    {Modifier::RightAlt, "ralt", "ropt"},
    {Modifier::RightWin, "rwin", "rcmd"},
};

struct WK { WellKnownCode c; const char* name; };
const WK kWellKnown[] = {
    {WellKnownCode::A, "a"}, {WellKnownCode::B, "b"}, {WellKnownCode::C, "c"},
    {WellKnownCode::D, "d"}, {WellKnownCode::E, "e"}, {WellKnownCode::F, "f"},
    {WellKnownCode::G, "g"}, {WellKnownCode::H, "h"}, {WellKnownCode::I, "i"},
    {WellKnownCode::J, "j"}, {WellKnownCode::K, "k"}, {WellKnownCode::L, "l"},
    {WellKnownCode::M, "m"}, {WellKnownCode::N, "n"}, {WellKnownCode::O, "o"},
    {WellKnownCode::P, "p"}, {WellKnownCode::Q, "q"}, {WellKnownCode::R, "r"},
    {WellKnownCode::S, "s"}, {WellKnownCode::T, "t"}, {WellKnownCode::U, "u"},
    {WellKnownCode::V, "v"}, {WellKnownCode::W, "w"}, {WellKnownCode::X, "x"},
    {WellKnownCode::Y, "y"}, {WellKnownCode::Z, "z"},
    {WellKnownCode::N1, "1"}, {WellKnownCode::N2, "2"}, {WellKnownCode::N3, "3"},
    {WellKnownCode::N4, "4"}, {WellKnownCode::N5, "5"}, {WellKnownCode::N6, "6"},
    {WellKnownCode::N7, "7"}, {WellKnownCode::N8, "8"}, {WellKnownCode::N9, "9"},
    {WellKnownCode::N0, "0"},
    {WellKnownCode::Enter, "enter"}, {WellKnownCode::Escape, "escape"},
    {WellKnownCode::Backspace, "backspace"}, {WellKnownCode::Tab, "tab"},
    {WellKnownCode::Space, "space"}, {WellKnownCode::Minus, "minus"},
    {WellKnownCode::Equal, "equal"}, {WellKnownCode::LeftBracket, "leftbracket"},
    {WellKnownCode::RightBracket, "rightbracket"}, {WellKnownCode::Backslash, "backslash"},
    {WellKnownCode::NonUSHash, "nonushash"}, {WellKnownCode::Semicolon, "semicolon"},
    {WellKnownCode::Quote, "quote"}, {WellKnownCode::Grave, "grave"},
    {WellKnownCode::Comma, "comma"}, {WellKnownCode::Dot, "dot"},
    {WellKnownCode::Slash, "slash"}, {WellKnownCode::CapsLock, "capslock"},
    {WellKnownCode::F1, "f1"}, {WellKnownCode::F2, "f2"}, {WellKnownCode::F3, "f3"},
    {WellKnownCode::F4, "f4"}, {WellKnownCode::F5, "f5"}, {WellKnownCode::F6, "f6"},
    {WellKnownCode::F7, "f7"}, {WellKnownCode::F8, "f8"}, {WellKnownCode::F9, "f9"},
    {WellKnownCode::F10, "f10"}, {WellKnownCode::F11, "f11"}, {WellKnownCode::F12, "f12"},
    {WellKnownCode::PrintScreen, "printscreen"}, {WellKnownCode::ScrollLock, "scrolllock"},
    {WellKnownCode::Pause, "pause"}, {WellKnownCode::Insert, "insert"},
    {WellKnownCode::Home, "home"}, {WellKnownCode::PageUp, "pageup"},
    {WellKnownCode::Delete, "delete"}, {WellKnownCode::End, "end"},
    {WellKnownCode::PageDown, "pagedown"}, {WellKnownCode::Right, "right"},
    {WellKnownCode::Left, "left"}, {WellKnownCode::Down, "down"},
    {WellKnownCode::Up, "up"}, {WellKnownCode::NumLock, "numlock"},
    {WellKnownCode::NumPadSlash, "numpadslash"}, {WellKnownCode::NumPadAsterisk, "numpadasterisk"},
    {WellKnownCode::NumPadMinus, "numpadminus"}, {WellKnownCode::NumPadPlus, "numpadplus"},
    {WellKnownCode::NumPadEnter, "numpadenter"},
    {WellKnownCode::NumPad1, "numpad1"}, {WellKnownCode::NumPad2, "numpad2"},
    {WellKnownCode::NumPad3, "numpad3"}, {WellKnownCode::NumPad4, "numpad4"},
    {WellKnownCode::NumPad5, "numpad5"}, {WellKnownCode::NumPad6, "numpad6"},
    {WellKnownCode::NumPad7, "numpad7"}, {WellKnownCode::NumPad8, "numpad8"},
    {WellKnownCode::NumPad9, "numpad9"}, {WellKnownCode::NumPad0, "numpad0"},
    {WellKnownCode::NumPadDot, "numpaddot"},
    {WellKnownCode::NonUSBackslash, "nonusbackslash"}, {WellKnownCode::Application, "application"},
    {WellKnownCode::Power, "power"}, {WellKnownCode::NumPadEqual, "numpadequal"},
    {WellKnownCode::F13, "f13"}, {WellKnownCode::F14, "f14"}, {WellKnownCode::F15, "f15"},
    {WellKnownCode::F16, "f16"}, {WellKnownCode::F17, "f17"}, {WellKnownCode::F18, "f18"},
    {WellKnownCode::F19, "f19"}, {WellKnownCode::F20, "f20"}, {WellKnownCode::F21, "f21"},
    {WellKnownCode::F22, "f22"}, {WellKnownCode::F23, "f23"}, {WellKnownCode::F24, "f24"},
};

struct Media { MediaCode c; const char* a; const char* b; };
const Media kMedia[] = {
    {MediaCode::ScreenBrightnessUp, "screenbrightnessup", nullptr},
    {MediaCode::ScreenBrightnessDown, "screenbrightnessdown", nullptr},
    {MediaCode::Next, "next", nullptr},
    {MediaCode::Previous, "previous", "prev"},
    {MediaCode::Stop, "stop", nullptr},
    {MediaCode::Play, "play", nullptr},
    {MediaCode::Mute, "mute", nullptr},
    {MediaCode::VolumeUp, "volumeup", nullptr},
    {MediaCode::VolumeDown, "volumedown", nullptr},
    {MediaCode::Favorites, "favorites", nullptr},
    {MediaCode::Calculator, "calculator", nullptr},
    {MediaCode::ScreenLock, "screenlock", nullptr},
    {MediaCode::WebPageHome, "webpagehome", nullptr},
    {MediaCode::WebPageBack, "webpageback", nullptr},
    {MediaCode::WebPageForward, "webpageforward", nullptr},
};

}  // namespace

// --- Modifiers ----------------------------------------------------------
std::vector<Modifier> allModifiers() {
    std::vector<Modifier> v;
    for (const auto& e : kModifiers) v.push_back(e.c);
    return v;
}
std::string modifierName(Modifier m) {
    for (const auto& e : kModifiers)
        if (e.c == m) return e.a;
    return "<unknown>";
}
std::string modifierAliases(Modifier m) {
    for (const auto& e : kModifiers)
        if (e.c == m) {
            std::string r = e.a;
            if (e.b) { r += " / "; r += e.b; }
            return r;
        }
    return "<unknown>";
}
std::optional<Modifier> modifierFromName(const std::string& name) {
    std::string n = toLower(name);
    for (const auto& e : kModifiers)
        if (n == e.a || (e.b && n == e.b)) return e.c;
    return std::nullopt;
}

// --- Well-known keys ----------------------------------------------------
std::vector<WellKnownCode> allWellKnown() {
    std::vector<WellKnownCode> v;
    for (const auto& e : kWellKnown) v.push_back(e.c);
    return v;
}
std::string wellKnownName(WellKnownCode c) {
    for (const auto& e : kWellKnown)
        if (e.c == c) return e.name;
    return "<unknown>";
}
std::optional<WellKnownCode> wellKnownFromName(const std::string& name) {
    std::string n = toLower(name);
    for (const auto& e : kWellKnown)
        if (n == e.name) return e.c;
    return std::nullopt;
}
std::optional<WellKnownCode> wellKnownFromValue(uint8_t value) {
    for (const auto& e : kWellKnown)
        if (static_cast<uint8_t>(e.c) == value) return e.c;
    return std::nullopt;
}
std::string wellKnownNameForValue(uint8_t value) {
    if (auto c = wellKnownFromValue(value)) return wellKnownName(*c);
    char buf[8];
    snprintf(buf, sizeof(buf), "<0x%02x>", value);
    return buf;
}
std::string modifierMaskName(Modifiers mask) {
    std::string r;
    for (Modifier m : allModifiers()) {
        if (mask & static_cast<uint8_t>(m)) {
            if (!r.empty()) r += "+";
            r += modifierName(m);
        }
    }
    return r;
}

// --- Media --------------------------------------------------------------
std::vector<MediaCode> allMedia() {
    std::vector<MediaCode> v;
    for (const auto& e : kMedia) v.push_back(e.c);
    return v;
}
std::string mediaName(MediaCode c) {
    for (const auto& e : kMedia)
        if (e.c == c) return e.a;
    return "<unknown>";
}
std::string mediaAliases(MediaCode c) {
    for (const auto& e : kMedia)
        if (e.c == c) {
            std::string r = e.a;
            if (e.b) { r += " / "; r += e.b; }
            return r;
        }
    return "<unknown>";
}
std::optional<MediaCode> mediaFromName(const std::string& name) {
    std::string n = toLower(name);
    for (const auto& e : kMedia)
        if (n == e.a || (e.b && n == e.b)) return e.c;
    return std::nullopt;
}
std::optional<MediaCode> mediaFromCode(uint16_t code) {
    for (const auto& e : kMedia)
        if (static_cast<uint16_t>(e.c) == code) return e.c;
    return std::nullopt;
}
std::string mediaNameForCode(uint16_t code) {
    if (auto c = mediaFromCode(code)) return mediaName(*c);
    char buf[16];
    snprintf(buf, sizeof(buf), "media<0x%04x>", code);
    return buf;
}

// --- Mouse --------------------------------------------------------------
std::string mouseModifierName(MouseModifier m) {
    switch (m) {
        case MouseModifier::Ctrl: return "ctrl";
        case MouseModifier::Shift: return "shift";
        case MouseModifier::Alt: return "alt";
    }
    return "";
}
std::optional<MouseModifier> mouseModifierFromName(const std::string& name) {
    std::string n = toLower(name);
    if (n == "ctrl") return MouseModifier::Ctrl;
    if (n == "shift") return MouseModifier::Shift;
    if (n == "alt") return MouseModifier::Alt;
    return std::nullopt;
}
std::optional<MouseButtons> mouseButtonBitFromName(const std::string& name) {
    std::string n = toLower(name);
    if (n == "left") return MouseButton::Left;
    if (n == "right") return MouseButton::Right;
    if (n == "middle") return MouseButton::Middle;
    return std::nullopt;
}
std::string mouseButtonName(MouseButtons bits) {
    std::string r;
    auto add = [&](uint8_t b, const char* s) {
        if (bits & b) { if (!r.empty()) r += "+"; r += s; }
    };
    add(MouseButton::Left, "left");
    add(MouseButton::Right, "right");
    add(MouseButton::Middle, "middle");
    return r.empty() ? "" : r;
}

// --- Physical position name ---------------------------------------------
std::string positionName(uint8_t index, uint8_t rows, uint8_t cols) {
    if (!rows || !cols || index >= rows * cols) return "";
    uint8_t r = static_cast<uint8_t>(index / cols);
    uint8_t c = static_cast<uint8_t>(index % cols);
    auto pick = [](uint8_t i, uint8_t n, const char* first, const char* mid,
                   const char* last) -> std::string {
        if (i == 0) return first;
        if (i + 1 == n) return last;
        return mid;
    };
    if (rows == 1 && cols == 2) return c == 0 ? "left" : "right";
    if (rows == 1 && cols == 3) return pick(c, cols, "left", "middle", "right");
    if (rows == 2 && cols == 1) return r == 0 ? "top" : "bottom";
    if (rows == 3 && cols == 1) return pick(r, rows, "top", "middle", "bottom");
    if (rows == 1) return "col " + std::to_string(c + 1);
    return "row " + std::to_string(r + 1) + " col " + std::to_string(c + 1);
}

// --- Key / Code display -------------------------------------------------
std::string Key::toString() const {
    if (kind == Kind::Button) return "button " + std::to_string(index);
    static const char* kAction[] = {"ccw", "press", "cw"};
    return "knob " + std::to_string(index) + " " + kAction[static_cast<int>(knobAction)];
}
std::string Code::toString() const {
    if (custom) return "<" + std::to_string(value) + ">";
    return wellKnownNameForValue(value);
}

}  // namespace ch57x

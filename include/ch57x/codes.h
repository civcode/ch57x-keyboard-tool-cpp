// codes.h — key/media/modifier enums and their name<->value lookup tables.
//
// Faithful port of the enums and lookup tables in the reference Rust tool
// (kriomant/ch57x-keyboard-tool, src/keyboard/mod.rs). HID usage values are
// taken from the USB HID Usage Tables (Keyboard/Keypad page 0x07 and
// Consumer page 0x0C), which is what the CH57x firmware expects on the wire.
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

namespace ch57x {

// ---------------------------------------------------------------------------
// Keyboard modifier bit field (byte 0 of a standard HID keyboard report).
// Multiple modifiers are OR'ed together into a Modifiers mask.
// ---------------------------------------------------------------------------
enum class Modifier : uint8_t {
    Ctrl       = 0x01,
    Shift      = 0x02,
    Alt        = 0x04,
    Win        = 0x08,
    RightCtrl  = 0x10,
    RightShift = 0x20,
    RightAlt   = 0x40,
    RightWin   = 0x80,
};
using Modifiers = uint8_t;  // bitmask of Modifier

// ---------------------------------------------------------------------------
// The three sub-actions a knob can be programmed for. The numeric values are
// part of the wire protocol (they become the low bits of the key ID).
// ---------------------------------------------------------------------------
enum class KnobAction : uint8_t {
    RotateCCW = 0,
    Press     = 1,
    RotateCW  = 2,
};

// A physical key: either a numbered button or a knob sub-action.
struct Key {
    enum class Kind : uint8_t { Button, Knob };
    Kind kind = Kind::Button;
    uint8_t index = 0;                 // button number, or knob number
    KnobAction knobAction = KnobAction::RotateCCW;

    static Key button(uint8_t n)            { return {Kind::Button, n, KnobAction::RotateCCW}; }
    static Key knob(uint8_t n, KnobAction a){ return {Kind::Knob, n, a}; }
    std::string toString() const;
};

// Where button `index` (0-based, key-id order) sits on a `rows` x `cols` board:
// "left" / "middle" / "right" for one row, "top" / "middle" / "bottom" for one
// column, otherwise "row 2 col 3". Empty string if the index is out of range.
std::string positionName(uint8_t index, uint8_t rows, uint8_t cols);

// ---------------------------------------------------------------------------
// HID usage values for well-known keys (Keyboard/Keypad usage page 0x07).
// These are the byte values sent in the binding payload.
// ---------------------------------------------------------------------------
enum class WellKnownCode : uint8_t {
    A = 0x04, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    N1 = 0x1E, N2, N3, N4, N5, N6, N7, N8, N9, N0,
    Enter = 0x28, Escape, Backspace, Tab, Space, Minus, Equal,
    LeftBracket, RightBracket, Backslash, NonUSHash, Semicolon, Quote, Grave,
    Comma, Dot, Slash, CapsLock,
    F1 = 0x3A, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    PrintScreen = 0x46, ScrollLock, Pause, Insert, Home, PageUp, Delete, End,
    PageDown, Right, Left, Down, Up, NumLock,
    NumPadSlash = 0x54, NumPadAsterisk, NumPadMinus, NumPadPlus, NumPadEnter,
    NumPad1 = 0x59, NumPad2, NumPad3, NumPad4, NumPad5, NumPad6, NumPad7, NumPad8,
    NumPad9, NumPad0, NumPadDot,
    NonUSBackslash = 0x64, Application, Power, NumPadEqual,
    F13 = 0x68, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24,
};

// A single key: either a well-known HID usage value, or a raw custom code.
struct Code {
    bool custom = false;
    uint8_t value = 0;
    static Code wellKnown(WellKnownCode c) { return {false, static_cast<uint8_t>(c)}; }
    static Code customCode(uint8_t v)      { return {true, v}; }
    std::string toString() const;
};

// ---------------------------------------------------------------------------
// Consumer page usage values (0x0C) for media keys.
// ---------------------------------------------------------------------------
enum class MediaCode : uint16_t {
    ScreenBrightnessUp   = 0x6f,
    ScreenBrightnessDown = 0x70,
    Next                 = 0xb5,
    Previous             = 0xb6,
    Stop                 = 0xb7,
    Play                 = 0xcd,
    Mute                 = 0xe2,
    VolumeUp             = 0xe9,
    VolumeDown           = 0xea,
    Favorites            = 0x182,
    Calculator           = 0x192,
    ScreenLock           = 0x19e,
    WebPageHome          = 0x223,
    WebPageBack          = 0x224,
    WebPageForward       = 0x225,
};

// Mouse modifier bit field (sent in mouse macros).
enum class MouseModifier : uint8_t {
    Ctrl  = 0x01,
    Shift = 0x02,
    Alt   = 0x04,
};

// Mouse button bitmask.
namespace MouseButton {
    constexpr uint8_t Left   = 0x01;
    constexpr uint8_t Right  = 0x02;
    constexpr uint8_t Middle = 0x04;
}
using MouseButtons = uint8_t;

// ---------------------------------------------------------------------------
// Lookup helpers (case-insensitive on the name).
// ---------------------------------------------------------------------------
std::vector<Modifier> allModifiers();
std::string modifierName(Modifier m);                 // canonical name
std::string modifierAliases(Modifier m);              // "alt / opt"
std::optional<Modifier> modifierFromName(const std::string& name);

std::vector<WellKnownCode> allWellKnown();
std::string wellKnownName(WellKnownCode c);
std::optional<WellKnownCode> wellKnownFromName(const std::string& name);
// Reverse direction (raw wire byte -> name), used when decoding a device dump.
std::optional<WellKnownCode> wellKnownFromValue(uint8_t value);
std::string wellKnownNameForValue(uint8_t value);      // "a", or "<0x80>" if unmapped

std::vector<MediaCode> allMedia();
std::string mediaName(MediaCode c);                   // canonical name
std::string mediaAliases(MediaCode c);                // "previous / prev"
std::optional<MediaCode> mediaFromName(const std::string& name);
std::optional<MediaCode> mediaFromCode(uint16_t code);
std::string mediaNameForCode(uint16_t code);           // "volumeup", or "<0x0182>"

// "ctrl+shift" for a modifier bitmask; "" for 0.
std::string modifierMaskName(Modifiers mask);

std::string mouseModifierName(MouseModifier m);
std::optional<MouseModifier> mouseModifierFromName(const std::string& name);
std::optional<MouseButtons> mouseButtonBitFromName(const std::string& name);  // single button bit
std::string mouseButtonName(MouseButtons bits);        // "left+right"

}  // namespace ch57x

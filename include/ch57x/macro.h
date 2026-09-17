// macro.h — the macro/action data model (port of src/keyboard/mod.rs types).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ch57x/codes.h"

namespace ch57x {

// A single keypress inside a keyboard macro: a modifier mask plus an optional
// key (a key with no code is used to represent "modifiers-only" chords).
struct Accord {
    Modifiers modifiers = 0;
    std::optional<Code> code;

    std::string toString() const {
        std::string r;
        // modifiers are printed in canonical order
        for (Modifier m : allModifiers()) {
            if (modifiers & static_cast<uint8_t>(m)) {
                if (!r.empty()) r += "-";
                r += modifierName(m);
            }
        }
        if (code) {
            if (!r.empty()) r += "-";
            r += code->toString();
        }
        return r;
    }
};

// Mouse actions. `kind` selects which fields are meaningful.
struct MouseAction {
    enum class Kind : uint8_t { Move, Drag, Click, Wheel };
    Kind kind = Kind::Click;
    MouseButtons buttons = 0;  // for Click / Drag
    int8_t dx = 0;             // for Move / Drag
    int8_t dy = 0;             // for Move / Drag
    int8_t wheel = 0;          // for Wheel

    std::string toString() const {
        std::string r;
        switch (kind) {
            case Kind::Move:   r = "move(" + std::to_string(dx) + "," + std::to_string(dy) + ")"; break;
            case Kind::Drag:   r = "drag(" + mouseButtonName(buttons) + "," +
                                 std::to_string(dx) + "," + std::to_string(dy) + ")"; break;
            case Kind::Click:  r = mouseButtonName(buttons); break;
            case Kind::Wheel:  r = "wheel(" + std::to_string(wheel) + ")"; break;
        }
        return r;
    }
};

struct MouseEvent {
    MouseAction action;
    std::optional<MouseModifier> modifier;

    std::string toString() const {
        std::string r;
        if (modifier) {
            r += mouseModifierName(*modifier) + "-";
        }
        r += action.toString();
        return r;
    }
};

struct MacroOptions {
    uint16_t delay = 0;  // milliseconds between presses
};

struct KeyboardEvent {
    MacroOptions opts;
    std::vector<Accord> accords;
};

// A full macro: one of keyboard sequence, media key, or mouse event.
struct Macro {
    enum class Kind : uint8_t { Keyboard, Media, Mouse };
    Kind kind = Kind::Keyboard;

    KeyboardEvent kbe;      // used when kind == Keyboard
    MediaCode media = MediaCode::Play;  // used when kind == Media
    MouseEvent mouse;       // used when kind == Mouse

    // The "kind" byte used on the wire (1 = keyboard, 2 = media, 3 = mouse).
    uint8_t kindByte() const {
        return kind == Kind::Keyboard ? 1 : kind == Kind::Media ? 2 : 3;
    }

    static Macro keyboard(KeyboardEvent e) {
        Macro m; m.kind = Kind::Keyboard; m.kbe = std::move(e); return m;
    }
    static Macro makeMedia(MediaCode c) {
        Macro m; m.kind = Kind::Media; m.media = c; return m;
    }
    static Macro makeMouse(MouseEvent e) {
        Macro m; m.kind = Kind::Mouse; m.mouse = std::move(e); return m;
    }

    std::string toString() const {
        switch (kind) {
            case Kind::Keyboard: {
                std::string r;
                if (kbe.opts.delay != 0) r += "{delay(" + std::to_string(kbe.opts.delay) + ")}";
                for (size_t i = 0; i < kbe.accords.size(); ++i) {
                    if (i) r += ",";
                    r += kbe.accords[i].toString();
                }
                return r;
            }
            case Kind::Media:  return mediaName(media);
            case Kind::Mouse:  return mouse.toString();
        }
        return "";
    }
};

}  // namespace ch57x

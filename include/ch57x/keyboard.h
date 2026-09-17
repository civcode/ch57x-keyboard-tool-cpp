// keyboard.h — the CH57x keypad driver (port of src/keyboard/k884x.rs).
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ch57x/config.h"
#include "ch57x/macro.h"

namespace ch57x {

// Mirrors the Rust `Keyboard` trait: methods append the bytes to program the
// device into an `output` buffer, which the caller sends in 64-byte chunks.
class Keyboard {
public:
    virtual ~Keyboard() = default;
    virtual void bindKey(uint8_t layer, const Key& key, const Macro& expansion,
                         std::vector<uint8_t>& output) = 0;
    virtual void setLed(uint8_t layer, const std::string& mode,
                        std::vector<uint8_t>& output) = 0;
};

// CH57x keypad driver for model `ch57x-1` (514c:8851).
class Keyboard884x : public Keyboard {
public:
    Keyboard884x(uint8_t buttons, uint8_t knobs);
    void bindKey(uint8_t layer, const Key& key, const Macro& expansion,
                 std::vector<uint8_t>& output) override;
    void setLed(uint8_t layer, const std::string& mode,
                std::vector<uint8_t>& output) override;

private:
    uint8_t buttons_, knobs_;
    uint8_t toKeyId(const Key& key) const;
};

// Reverse of Keyboard884x::toKeyId(): a wire key id -> the physical key it
// belongs to, e.g. "key 2 (middle)" or "knob 1 press". `rows`/`cols`/`knobs`
// come from config.yaml (or from the 0xFB identify reply for the button count).
std::string keyIdName(uint8_t keyId, uint8_t rows, uint8_t cols, uint8_t knobs);

// One decoded binding record. `upload` writes records that start 03 fe ...;
// the 0xFA read request returns the same layout but echoes the read opcode
// (03 fa ...) instead. Layout:
//   [2] key id, [3] layer (1-based on the wire), [4] action kind
//   kind 1 keyboard: count at 10, then (modifier, code) pairs from 11
//   kind 2 media:    u16 consumer usage at 11 (little endian)
//   kind 3 mouse:    action 10, modifier 11, buttons 12, dx 13, dy 14, wheel 15
//   kind 5 delay:    u16 milliseconds at 5 (attaches to the record before it)
//   kind 8 LED block (key id 0xb0): mode byte at 12
struct BindInfo {
    bool valid = false;
    uint8_t keyId = 0, layer = 0, kind = 0;
    std::string keyName;  // "key 1 (left)", "knob 1 press", "LED block", ...
    std::string action;   // "a", "volumeup", "click(left)", "delay 50 ms", ...
    std::string detail;   // what the action bytes mean: "HID usage 0xe2 (consumer)" ...
    std::string line() const;  // "<keyName> layer <layer> -> <action>"
};
// rows/cols/knobs come from config.yaml (or the 0xFB reply) so the physical key
// can be named. `valid` is false when `d` is not a bind record.
BindInfo decodeBind(const std::vector<uint8_t>& d, uint8_t rows, uint8_t cols, uint8_t knobs);

// decodeBind() + line(); "" when `d` is not a bind record.
std::string describeRecord(const std::vector<uint8_t>& d, uint8_t rows, uint8_t cols,
                           uint8_t knobs);

// LED color/mode parsing (port of the LedColor/LedMode logic in k884x.rs).
enum class LedMode : uint8_t { Off = 0, Backlight = 1, Shock = 2, Shock2 = 3, Press = 4 };

struct LedSpec {
    LedMode mode;
    uint8_t color;  // 0-7 (0 = white, only valid for Backlight)
};
LedSpec parseLedMode(const std::string& s);  // "off" | "backlight <c>" | "shock <c>" | "shock2 <c>" | "press <c>"
uint8_t ledCode(const LedSpec& s);           // (color << 4) | mode  (white backlight -> 0x05)

// Factory (port of main.rs create_driver).
std::unique_ptr<Keyboard> createDriver(KeyboardModel model, uint8_t buttons, uint8_t knobs);

}  // namespace ch57x

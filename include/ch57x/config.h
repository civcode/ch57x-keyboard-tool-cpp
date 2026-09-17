// config.h — mapping-config model and orientation/rendering (port of config.rs).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ch57x/macro.h"

namespace ch57x {

// Keyboard model. Names are deliberately decoupled from the PID (the same PID
// can back different models); they pick the driver + preferred endpoint.
enum class KeyboardModel : uint8_t {
    Ch57x_1 = 0,
    Ch57x_2 = 1,
    Ch57x_3 = 2,
};

struct DeviceInfo {
    KeyboardModel model;
    uint8_t preferred_endpoint;
};

// Physical orientation of the keys relative to the logical grid.
enum class Orientation : uint8_t {
    Normal = 0,
    UpsideDown = 1,
    Clockwise = 2,
    CounterClockwise = 3,
};

inline bool isHorizontal(Orientation o) {
    return o == Orientation::Normal || o == Orientation::UpsideDown;
}

struct Knob {
    std::optional<Macro> ccw;
    std::optional<Macro> press;
    std::optional<Macro> cw;
};

struct Layer {
    std::vector<std::vector<std::optional<Macro>>> buttons;
    std::vector<Knob> knobs;
};

struct Config {
    std::optional<KeyboardModel> model;
    Orientation orientation = Orientation::Normal;
    uint8_t rows = 0;
    uint8_t columns = 0;
    uint8_t knobs = 0;
    std::vector<Layer> layers;
};

// A rendered layer: flat (orientation-applied) button list + knobs.
struct FlatLayer {
    std::vector<std::optional<Macro>> buttons;
    std::vector<Knob> knobs;
};

// Model -> supported (VID, PID, preferred endpoint) table.
// (port of config.rs SUPPORTED_DEVICES)
std::vector<DeviceInfo> devicesForModel(KeyboardModel model);
std::vector<DeviceInfo> devicesForVidPid(uint16_t vid, uint16_t pid);
std::optional<DeviceInfo> deviceInfo(KeyboardModel model, uint16_t vid, uint16_t pid);

std::string modelToString(KeyboardModel m);   // "ch57x-1"
std::optional<KeyboardModel> modelFromName(const std::string& name);
std::optional<Orientation> orientationFromName(const std::string& name);
std::string orientationToString(Orientation o);

// Validate + render the config to flat layers, applying orientation.
// Throws std::runtime_error on invalid input.
std::vector<FlatLayer> render(const Config& cfg);

}  // namespace ch57x

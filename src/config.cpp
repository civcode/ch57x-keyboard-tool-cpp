#include "ch57x/config.h"

#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace ch57x {

namespace {
// (model, vid, pid, preferred endpoint) — same table as config.rs.
struct DeviceEntry { KeyboardModel model; uint16_t vid; uint16_t pid; uint8_t endpoint; };
const DeviceEntry kSupportedDevices[] = {
    {KeyboardModel::Ch57x_1, 0x1189, 0x8840, 0x04},
    {KeyboardModel::Ch57x_1, 0x1189, 0x8842, 0x04},
    {KeyboardModel::Ch57x_1, 0x1189, 0x8850, 0x04},
    {KeyboardModel::Ch57x_1, 0x514c, 0x8851, 0x02},
    {KeyboardModel::Ch57x_2, 0x1189, 0x8890, 0x02},
    {KeyboardModel::Ch57x_3, 0x514c, 0x8850, 0x04},
};
}  // namespace

std::vector<DeviceInfo> devicesForModel(KeyboardModel model) {
    std::vector<DeviceInfo> v;
    for (const auto& e : kSupportedDevices)
        if (e.model == model) v.push_back({e.model, e.endpoint});
    return v;
}
std::vector<DeviceInfo> devicesForVidPid(uint16_t vid, uint16_t pid) {
    std::vector<DeviceInfo> v;
    for (const auto& e : kSupportedDevices)
        if (e.vid == vid && e.pid == pid) v.push_back({e.model, e.endpoint});
    return v;
}
std::optional<DeviceInfo> deviceInfo(KeyboardModel model, uint16_t vid, uint16_t pid) {
    for (const auto& e : kSupportedDevices)
        if (e.model == model && e.vid == vid && e.pid == pid)
            return DeviceInfo{e.model, e.endpoint};
    return std::nullopt;
}

std::string modelToString(KeyboardModel m) {
    switch (m) {
        case KeyboardModel::Ch57x_1: return "ch57x-1";
        case KeyboardModel::Ch57x_2: return "ch57x-2";
        case KeyboardModel::Ch57x_3: return "ch57x-3";
    }
    return "";
}
std::optional<KeyboardModel> modelFromName(const std::string& name) {
    if (name == "ch57x-1") return KeyboardModel::Ch57x_1;
    if (name == "ch57x-2") return KeyboardModel::Ch57x_2;
    if (name == "ch57x-3") return KeyboardModel::Ch57x_3;
    return std::nullopt;
}
std::string orientationToString(Orientation o) {
    switch (o) {
        case Orientation::Normal:            return "normal";
        case Orientation::UpsideDown:        return "upsidedown";
        case Orientation::Clockwise:         return "clockwise";
        case Orientation::CounterClockwise:  return "counterclockwise";
    }
    return "";
}
std::optional<Orientation> orientationFromName(const std::string& name) {
    if (name == "normal")            return Orientation::Normal;
    if (name == "upsidedown")        return Orientation::UpsideDown;
    if (name == "clockwise")         return Orientation::Clockwise;
    if (name == "counterclockwise")  return Orientation::CounterClockwise;
    return std::nullopt;
}

// Transforms a physical (row, col) position to a virtual index.
// (port of config.rs reorient_grid; `rows`/`cols` are the PHYSICAL dims and
//  `data` is laid out as orows x ocols.)
template <class T>
std::vector<T> reorientGrid(Orientation orientation, int rows, int cols,
                            const std::vector<std::vector<T>>& data) {
    std::function<std::pair<int,int>(int,int)> tr;
    switch (orientation) {
        case Orientation::Normal:           tr = [](int r, int c) { return std::make_pair(r, c); }; break;
        case Orientation::UpsideDown:       tr = [rows, cols](int r, int c) { return std::make_pair(rows - r - 1, cols - c - 1); }; break;
        case Orientation::Clockwise:        tr = [rows](int r, int c) { return std::make_pair(c, rows - r - 1); }; break;
        case Orientation::CounterClockwise: tr = [cols](int r, int c) { return std::make_pair(cols - c - 1, r); }; break;
    }
    std::vector<T> out(rows * cols);
    for (int i = 0; i < rows * cols; ++i) {
        int r = i / cols;
        int c = i % cols;
        auto [rr, cc] = tr(r, c);
        out[i] = data[rr][cc];
    }
    return out;
}

template <class T>
std::vector<T> reorientRow(Orientation orientation, std::vector<T> data) {
    bool reverse =
        orientation == Orientation::UpsideDown || orientation == Orientation::Clockwise;
    if (reverse) std::reverse(data.begin(), data.end());
    return data;
}

std::vector<FlatLayer> render(const Config& cfg) {
    // A 3x1 keys + 1 knob board has a limitation (see config.rs comment).
    const bool is_limited = (cfg.rows == 1 || cfg.columns == 1) && cfg.knobs == 1;

    std::vector<FlatLayer> out;
    for (size_t i = 0; i < cfg.layers.size(); ++i) {
        const Layer& layer = cfg.layers[i];
        int orows = isHorizontal(cfg.orientation) ? cfg.rows : cfg.columns;
        int ocols = isHorizontal(cfg.orientation) ? cfg.columns : cfg.rows;

        if (static_cast<int>(layer.buttons.size()) != orows)
            throw std::runtime_error(
                "layer " + std::to_string(i) + ": expected " + std::to_string(orows) +
                " button row(s), got " + std::to_string(layer.buttons.size()));
        for (const auto& row : layer.buttons)
            if (static_cast<int>(row.size()) != ocols)
                throw std::runtime_error(
                    "layer " + std::to_string(i) + ": button row has " +
                    std::to_string(row.size()) + " entries, expected " + std::to_string(ocols));
        if (static_cast<int>(layer.knobs.size()) != cfg.knobs)
            throw std::runtime_error(
                "layer " + std::to_string(i) + ": expected " + std::to_string(cfg.knobs) +
                " knob(s), got " + std::to_string(layer.knobs.size()));

        auto buttons = reorientGrid(cfg.orientation, cfg.rows, cfg.columns, layer.buttons);
        auto knobs = reorientRow(cfg.orientation, layer.knobs);

        if (is_limited) {
            for (const auto& macro_opt : buttons) {
                if (!macro_opt) continue;
                const Macro& m = *macro_opt;
                if (m.kind == Macro::Kind::Keyboard) {
                    bool bad = false;
                    // modifiers are only allowed on the FIRST key of the sequence
                    for (size_t k = 1; k < m.kbe.accords.size(); ++k)
                        if (m.kbe.accords[k].modifiers != 0) { bad = true; break; }
                    if (bad)
                        throw std::runtime_error(
                            "1-row keyboard with 1 knob can handle modifiers for the first "
                            "key in a sequence only: " + m.toString());
                }
            }
        }

        out.push_back(FlatLayer{std::move(buttons), std::move(knobs)});
    }
    return out;
}

}  // namespace ch57x

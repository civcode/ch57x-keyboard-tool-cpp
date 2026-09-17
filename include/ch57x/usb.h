// usb.h — libusb transport for the CH57x vendor HID channel.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ch57x/config.h"

namespace ch57x {

class UsbError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct UsbOptions {
    std::optional<uint16_t> vendorId;
    std::optional<uint16_t> productId;
    std::optional<std::pair<uint8_t, uint8_t>> address;  // (bus, device number)
    std::optional<uint8_t> endpoint;   // preferred endpoint override
    std::optional<uint8_t> interface;  // interface number override
};

// An open, claimed device on its vendor (programming) interface.
class Device {
public:
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    Device(Device&&) noexcept = default;
    Device& operator=(Device&&) noexcept = default;
    ~Device();

    // Find, open, claim, and initialize the device (sends the 64-byte zero
    // init packet, mirroring the Rust `open_device`). Throws UsbError.
    static Device open(const UsbOptions& opts, std::optional<KeyboardModel> model);

    uint16_t vendorId() const;
    uint16_t productId() const;
    uint8_t outEndpoint() const;
    uint8_t inEndpoint() const;
    uint8_t interfaceNumber() const;
    std::optional<KeyboardModel> model() const;

    // Send `data` in 64-byte interrupt-OUT chunks.
    void send(const std::vector<uint8_t>& data);

    // Read one 64-byte interrupt-IN report. Returns (report bytes, transferred).
    std::pair<std::vector<uint8_t>, int> readReport(size_t timeoutMs = 1000);

private:
    struct Impl;
    explicit Device(std::unique_ptr<Impl> i);
    std::unique_ptr<Impl> impl_;
};

// Identify (vendor 0xFB handshake): 03 fb fb fb -> 03 fb <keys> <style> <status> ...
struct IdentifyResult {
    uint8_t echo = 0;         // raw[1] = 0xfb, echo of the request opcode
    uint8_t keyCount = 0;     // raw[2] = programmable buttons (knobs excluded)
    uint8_t style = 0;        // raw[3] = board variant byte
    uint8_t status = 0;       // raw[4]; 0x0a on newer firmware, 0x00 on this board
    std::vector<uint8_t> raw; // full 64-byte response
};
IdentifyResult identify(Device& dev);

// A single read-back report from the 0xFA read-config command.
struct ReadReport {
    int group = 0;
    int index = 0;
    std::vector<uint8_t> data;  // 64 bytes
};
// Ask for the stored key map (0xFA). The firmware streams the whole map back as
// one continuous sequence of 64-byte reports, ~12 per request, and the request
// counter (byte 4) selects the next batch. Two request variants exist in the
// vendor tool; `preferNewProtocol` picks which is tried first, and the other is
// tried when the first yields nothing:
//   older firmware (identify status 0x00, measured): 03 fa 0f 03 <n>
//   newer firmware (identify status 0x0a)           : 03 fa 19 00 <n>
// Requests stop at the first one that returns nothing (end of map), so an
// unresponsive device finishes in under a second instead of blocking on 75
// fixed slots.
struct ReadConfigResult {
    std::vector<ReadReport> reports;
    bool newProtocol = false;  // request variant that worked (of the ones tried)
    int requests = 0;          // 0xFA requests sent
    int zeroReports = 0;       // all-zero filler reports skipped
    int acks = 0;              // one-byte acknowledgement reports skipped
    bool truncated = false;    // stopped on the deadline / ack stall, not an empty reply
    std::string error;         // I/O failure that ended the read (reports still usable)
};
// `buttons` comes from the identify handshake: the request counter is capped at
// 3*buttons+15 (legacy) or 3*buttons+1 (newer firmware). `deadlineMs` bounds the
// whole read so an unresponsive device cannot make the tool appear to hang.
ReadConfigResult readConfig(Device& dev, bool preferNewProtocol,
                            size_t firstTimeoutMs = 700, size_t idleTimeoutMs = 200,
                            int buttons = 0, int deadlineMs = 5000);

// "0f1a2b" style hex dump, 16 bytes per line.
std::string hexdump(const uint8_t* data, size_t n);

}  // namespace ch57x

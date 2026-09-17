#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Wire capture. Every USB transfer the tool makes can be logged to a file so a
// capture taken on real hardware (with sudo) can be analysed — and the record
// parser fixed — offline, without the device.
//
// File format: a flat sequence of frames. Each frame is an 8-byte header
// followed by `len` payload bytes:
//   'C' 'X' | dir | len (1 byte) | seq (uint32 little endian) | payload
// dir is 'O' for a host->device OUT report, 'I' for an IN report actually
// received, and 'T' for an IN transfer that timed out (len 0). The 'T' frames
// matter: they mark where one request's reply stream ends.
namespace ch57x {

struct Frame {
    char dir = 'I';
    uint32_t seq = 0;
    std::vector<uint8_t> data;
    bool isTimeout() const { return dir == 'T'; }
};

// Start capturing to `path` (existing file is replaced). Safe to call once per
// process; returns false when the file cannot be opened.
bool enableCapture(const std::string& path);
bool captureEnabled();
void closeCapture();  // flush + close (mainly so tests can re-read the file)

void captureOut(const uint8_t* data, size_t len);
void captureIn(const uint8_t* data, size_t len);  // len 0 records a timeout

// Read back a capture file. Throws std::runtime_error on a malformed file.
std::vector<Frame> readCapture(const std::string& path);

}  // namespace ch57x

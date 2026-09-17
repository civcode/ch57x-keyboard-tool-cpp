#include "ch57x/capture.h"

#include <cstdio>
#include <fstream>
#include <stdexcept>

namespace ch57x {
namespace {

FILE* g_file = nullptr;
uint32_t g_seq = 0;

void writeFrame(char dir, const uint8_t* data, size_t len) {
    if (!g_file) return;
    uint8_t head[8] = {'C', 'X', static_cast<uint8_t>(dir), static_cast<uint8_t>(len)};
    const uint32_t seq = g_seq++;
    head[4] = static_cast<uint8_t>(seq & 0xff);
    head[5] = static_cast<uint8_t>(seq >> 8);
    head[6] = static_cast<uint8_t>(seq >> 16);
    head[7] = static_cast<uint8_t>(seq >> 24);
    std::fwrite(head, 1, sizeof(head), g_file);
    if (len) std::fwrite(data, 1, len, g_file);
    std::fflush(g_file);  // keep the capture usable even if we are interrupted
}

}  // namespace

bool enableCapture(const std::string& path) {
    if (g_file) return true;
    g_file = std::fopen(path.c_str(), "wb");
    return g_file != nullptr;
}

bool captureEnabled() { return g_file != nullptr; }

void closeCapture() {
    if (g_file) {
        std::fclose(g_file);
        g_file = nullptr;
    }
}

void captureOut(const uint8_t* data, size_t len) { writeFrame('O', data, len); }

void captureIn(const uint8_t* data, size_t len) { writeFrame(len ? 'I' : 'T', data, len); }

std::vector<Frame> readCapture(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open capture file: " + path);
    std::vector<Frame> frames;
    while (in.peek() != EOF) {
        uint8_t head[8];
        if (!in.read(reinterpret_cast<char*>(head), 8))
            throw std::runtime_error("truncated frame header in " + path);
        if (head[0] != 'C' || head[1] != 'X')
            throw std::runtime_error("not a ch57x capture file (bad magic): " + path);
        if (head[2] != 'O' && head[2] != 'I' && head[2] != 'T')
            throw std::runtime_error("unknown frame direction in " + path);
        Frame f;
        f.dir = static_cast<char>(head[2]);
        const size_t len = head[3];
        f.seq = static_cast<uint32_t>(head[4]) | (static_cast<uint32_t>(head[5]) << 8) |
                (static_cast<uint32_t>(head[6]) << 16) | (static_cast<uint32_t>(head[7]) << 24);
        f.data.resize(len);
        if (len && !in.read(reinterpret_cast<char*>(f.data.data()), static_cast<std::streamsize>(len)))
            throw std::runtime_error("truncated frame payload in " + path);
        frames.push_back(std::move(f));
    }
    return frames;
}

}  // namespace ch57x

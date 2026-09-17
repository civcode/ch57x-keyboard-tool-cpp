#include "ch57x/usb.h"

#include "ch57x/capture.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

#include <libusb.h>

namespace ch57x {

std::string hexdump(const uint8_t* data, size_t n) {
    std::string out;
    char line[256];
    for (size_t i = 0; i < n; i += 16) {
        int w = snprintf(line, sizeof(line), "%04zX  ", i);
        for (size_t j = 0; j < 16; ++j) {
            if (i + j < n) {
                w += snprintf(line + w, sizeof(line) - w, "%02x ", data[i + j]);
                if (j == 7) {
                    // 8-byte column gutter: one space to line up the two halves
                    w += snprintf(line + w, sizeof(line) - w, " ");
                }
            } else {
                w += snprintf(line + w, sizeof(line) - w, "   ");
                if (j == 7) w += snprintf(line + w, sizeof(line) - w, " ");
            }
        }
        // ASCII gutter (skip it for the trailing partial line to keep width even)
        for (size_t j = 0; j < 16 && i + j < n; ++j) {
            uint8_t c = data[i + j];
            w += snprintf(line + w, sizeof(line) - w, "%c",
                          (c >= 0x20 && c < 0x7f) ? c : '.');
        }
        out += line;
        if (i + 16 < n) out += "\n";
    }
    return out;
}

namespace {
void check(int rc, const std::string& what) {
    if (rc < 0) throw UsbError(what + ": " + libusb_strerror(rc));
}
}  // namespace

struct Device::Impl {
    libusb_context* ctx = nullptr;
    libusb_device_handle* handle = nullptr;
    uint16_t vid = 0, pid = 0;
    uint8_t outEp = 0, inEp = 0, iface = 0;
    std::optional<KeyboardModel> model;
};

Device::Device(std::unique_ptr<Impl> i) : impl_(std::move(i)) {}

Device::~Device() {
    if (!impl_) return;
    if (impl_->handle) {
        libusb_release_interface(impl_->handle, impl_->iface);
        // open() auto-detaches hid-generic; give the interface back so the keyboard
        // keeps working without a replug after `watch` on the HID interface. A driver
        // that is still bound answers BUSY, which is not an error for us.
        libusb_attach_kernel_driver(impl_->handle, impl_->iface);
        libusb_close(impl_->handle);
    }
    if (impl_->ctx) libusb_exit(impl_->ctx);
}

uint16_t Device::vendorId() const { return impl_->vid; }
uint16_t Device::productId() const { return impl_->pid; }
uint8_t Device::outEndpoint() const { return impl_->outEp; }
uint8_t Device::inEndpoint() const { return impl_->inEp; }
uint8_t Device::interfaceNumber() const { return impl_->iface; }
std::optional<KeyboardModel> Device::model() const { return impl_->model; }

void Device::send(const std::vector<uint8_t>& data) {
    if (!impl_->outEp)
        throw UsbError("this interface has no OUT endpoint: open the vendor interface"
                       " (interface 0 on 514c:8851) to write to the device");
    for (size_t off = 0; off < data.size(); off += 64) {
        size_t len = std::min<size_t>(64, data.size() - off);
        int n = 0;
        int rc = libusb_interrupt_transfer(impl_->handle, impl_->outEp,
                                           const_cast<uint8_t*>(data.data() + off),
                                           static_cast<int>(len), &n, 1000);
        captureOut(data.data() + off, len);  // logged even when the transfer failed
        check(rc, "send failed");
        if (static_cast<size_t>(n) != len)
            throw UsbError("short write: sent " + std::to_string(n) + " of " +
                           std::to_string(len) + " bytes");
    }
}

std::pair<std::vector<uint8_t>, int> Device::readReport(size_t timeoutMs) {
    std::vector<uint8_t> buf(64, 0);
    int n = 0;
    int rc = libusb_interrupt_transfer(impl_->handle, impl_->inEp, buf.data(), 64, &n,
                                       static_cast<unsigned int>(timeoutMs));
    // Everything the device actually sent is logged here, before any parsing,
    // so a capture file is a faithful record of the wire.
    captureIn(rc == LIBUSB_ERROR_TIMEOUT ? nullptr : buf.data(),
              rc == LIBUSB_ERROR_TIMEOUT ? 0 : static_cast<size_t>(n));
    if (rc == LIBUSB_ERROR_TIMEOUT) return {buf, 0};
    check(rc, "read failed");
    return {buf, n};
}

Device Device::open(const UsbOptions& opts, std::optional<KeyboardModel> model) {
    libusb_context* ctx = nullptr;
    check(libusb_init(&ctx), "libusb init failed");

    struct Cand {
        libusb_device* dev;
        uint16_t vid, pid;
        uint8_t bus, devnum;
        std::vector<DeviceInfo> infos;
    };
    std::vector<Cand> cands;

    // Cleanup guard so every throw path releases the context, the device
    // list, and any extra reference on the candidate device.
    struct Ctl {
        libusb_context* ctx = nullptr;
        libusb_device** devs = nullptr;
        libusb_device* dev = nullptr;  // extra-ref'd candidate
        bool done = false;             // true once ownership transfers to Device
        ~Ctl() {
            if (done) return;
            if (dev) libusb_unref_device(dev);
            if (devs) libusb_free_device_list(devs, 1);
            if (ctx) libusb_exit(ctx);
        }
    } ctl;
    ctl.ctx = ctx;

    libusb_device** devs = nullptr;
    ssize_t ndev = libusb_get_device_list(ctx, &devs);
    ctl.devs = devs;
    if (ndev < 0)
        throw UsbError(std::string("enumerate devices: ") + libusb_strerror((int)ndev));

    for (ssize_t i = 0; devs && devs[i]; ++i) {
        auto* dev = devs[i];
        libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(dev, &desc) != 0) continue;
        if (opts.vendorId && desc.idVendor != *opts.vendorId) continue;
        if (opts.productId && desc.idProduct != *opts.productId) continue;
        if (opts.address) {
            uint8_t bus = libusb_get_bus_number(dev);
            uint8_t dn = libusb_get_device_address(dev);
            if (std::make_pair(bus, dn) != *opts.address) continue;
        }
        std::vector<DeviceInfo> infos;
        if (model) {
            auto di = deviceInfo(*model, desc.idVendor, desc.idProduct);
            if (di) infos.push_back(*di);
        } else {
            infos = devicesForVidPid(desc.idVendor, desc.idProduct);
        }
        if (infos.empty()) continue;
        cands.push_back({dev, desc.idVendor, desc.idProduct,
                         libusb_get_bus_number(dev), libusb_get_device_address(dev),
                         std::move(infos)});
    }

    if (cands.empty())
        throw UsbError("no matching CH57x device found (is it attached and accessible?)");
    if (cands.size() > 1)
        throw UsbError("multiple devices found; specify which one with --address <bus> <dev>");
    if (cands.front().infos.size() != 1)
        throw UsbError("ambiguous model for this device; set `model:` in the config "
                       "or pass the model explicitly");

    const Cand& c = cands.front();
    DeviceInfo info = c.infos.front();
    std::optional<KeyboardModel> m = info.model;

    // Keep the candidate alive across freeing the enumeration list.
    libusb_ref_device(c.dev);
    ctl.dev = c.dev;
    libusb_free_device_list(devs, 1);
    ctl.devs = nullptr;

    libusb_config_descriptor* config = nullptr;
    int rc = libusb_get_active_config_descriptor(c.dev, &config);
    if (rc != 0) rc = libusb_get_config_descriptor(c.dev, 0, &config);
    if (rc != 0 || !config)
        throw UsbError("failed to read the USB configuration descriptor");

    uint8_t wantEp = opts.endpoint ? *opts.endpoint : info.preferred_endpoint;
    // By default only the vendor (programming) interface is accepted: it is the one
    // that speaks the 0xFB/0xFA/0xFE protocol and has a 64-byte OUT endpoint. Asking
    // for --interface <n> opens that interface instead: interface 1 is the real HID
    // keyboard/mouse channel, whose IN endpoint carries what the operating system
    // sees when a key is pressed (and has no OUT endpoint at all).
    const bool vendorOnly = !opts.interface;
    int foundIface = -1;
    uint8_t foundIn = 0;
    uint8_t foundOut = 0;
    for (uint8_t i = 0; i < config->bNumInterfaces; ++i) {
        const libusb_interface_descriptor* alt = &config->interface[i].altsetting[0];
        const bool want =
            opts.interface
                ? alt->bInterfaceNumber == *opts.interface
                : (alt->bInterfaceClass == 3 && alt->bInterfaceSubClass == 0 &&
                   alt->bInterfaceProtocol == 0);
        if (!want) continue;
        uint8_t inEp = 0, outEp = 0;
        for (uint8_t j = 0; j < alt->bNumEndpoints; ++j) {
            const auto* ep = &alt->endpoint[j];
            if (ep->bEndpointAddress & 0x80u) {
                if (!inEp) inEp = ep->bEndpointAddress;
            } else if (!outEp && (!vendorOnly || ep->bEndpointAddress == wantEp)) {
                outEp = ep->bEndpointAddress;
            }
        }
        if (!inEp) continue;
        if (vendorOnly && !outEp) continue;  // programming channel needs its OUT endpoint
        foundIface = alt->bInterfaceNumber;
        foundIn = inEp;
        foundOut = outEp;
        break;
    }
    libusb_free_config_descriptor(config);
    if (foundIface < 0)
        throw UsbError(opts.interface
                           ? "interface " + std::to_string(*opts.interface) +
                                 " has no usable endpoint on this device"
                           : "no vendor (programming) interface with endpoint 0x" +
                                 std::to_string(wantEp) + " found");

    libusb_device_handle* handle = nullptr;
    int ro = libusb_open(c.dev, &handle);
    if (ro != 0)
        throw UsbError(std::string("failed to open device: ") + libusb_strerror(ro));
    libusb_set_auto_detach_kernel_driver(handle, 1);
    int rcl = libusb_claim_interface(handle, foundIface);
    if (rcl != 0) {
        libusb_close(handle);
        throw UsbError(std::string("failed to claim interface (needs udev rule/root): ") +
                       libusb_strerror(rcl));
    }

    // Success: the handle now holds a reference to the device; drop ours and
    // hand the context off to the Device object.
    libusb_unref_device(c.dev);
    ctl.done = true;

    auto impl = std::unique_ptr<Impl>(new Impl());
    impl->ctx = ctx;
    impl->handle = handle;
    impl->vid = c.vid;
    impl->pid = c.pid;
    impl->outEp = foundOut;
    impl->inEp = foundIn;
    impl->iface = static_cast<uint8_t>(foundIface);
    impl->model = m;
    Device dev(std::move(impl));

    // Init packet (64 zero bytes), mirroring the Rust `open_device`. It is a write on
    // the vendor channel, so only send it when the opened interface has an OUT
    // endpoint: the HID interface (1 on 514c:8851) is IN-only and `watch` opens that
    // one, where the same write fails with "this interface has no OUT endpoint".
    if (dev.outEndpoint()) dev.send(std::vector<uint8_t>(64, 0));
    return dev;
}

IdentifyResult identify(Device& dev) {
    std::vector<uint8_t> cmd(64, 0);
    cmd[0] = 0x03;
    cmd[1] = 0xFB;
    cmd[2] = 0xFB;
    cmd[3] = 0xFB;
    dev.send(cmd);
    auto [buf, n] = dev.readReport(1000);
    IdentifyResult r;
    r.raw = buf;
    if (n >= 2) r.echo = buf[1];
    if (n >= 3) r.keyCount = buf[2];
    if (n >= 4) r.style = buf[3];
    if (n >= 5) r.status = buf[4];
    return r;
}

namespace {

// One 0xFA read request; see the two variants documented in usb.h.
std::vector<uint8_t> readRequest(bool newProtocol, int group) {
    std::vector<uint8_t> cmd(64, 0);
    cmd[0] = 0x03;
    cmd[1] = 0xFA;
    cmd[2] = newProtocol ? 0x19 : 0x0F;
    cmd[3] = newProtocol ? 0x00 : 0x03;
    cmd[4] = static_cast<uint8_t>(group);
    return cmd;
}

// Boards that have nothing to return answer with an all-zero report.
bool isZeroReport(const std::vector<uint8_t>& d) {
    for (uint8_t b : d)
        if (b != 0) return false;
    return true;
}

// The firmware acknowledges every 64-byte OUT report with a one-byte IN report.
// Captured on the wire: the read-back stream alternates 0x00(1 byte) and the
// 64-byte binding records, so an ACK is neither a record nor end-of-stream.
bool isAckReport(const std::vector<uint8_t>& d) { return d.size() == 1; }

// Requests stop at the first one that returns no record. The vendor tool caps the
// request counter at 3*buttons+15 (older firmware) or 3*buttons+1; with 3
// buttons that is 24 / 10 requests.
ReadConfigResult readVariant(Device& dev, bool newProtocol, size_t firstTimeoutMs,
                             size_t idleTimeoutMs, int buttons, int deadlineMs) {
    ReadConfigResult res;
    res.newProtocol = newProtocol;
    const int maxRequests =
        newProtocol ? 3 * buttons + 1 : 3 * buttons + 15;
    const size_t kMaxReports = 512;
    const auto stop = std::chrono::steady_clock::now() + std::chrono::milliseconds(deadlineMs);

    for (int request = 1; request <= maxRequests; ++request) {
        int produced = 0;
        // A request the device ignores is resent once: the reply stream restarts
        // per request, so a dropped request would silently truncate the map.
        for (int attempt = 0; attempt < 2 && produced == 0; ++attempt) {
            try {
                dev.send(readRequest(newProtocol, request));
                ++res.requests;
            } catch (const UsbError& e) {
                // A board can stop servicing its OUT endpoint once it has dumped the
                // map. Keep the records already read instead of losing the whole read.
                res.error = e.what();
                res.truncated = true;
                return res;
            }
            int quiet = 0;  // consecutive ack/filler replies with no record between them
            while (res.reports.size() < kMaxReports) {
                auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                    stop - std::chrono::steady_clock::now());
                if (left.count() <= 0) {
                    res.truncated = true;
                    break;
                }
                // ACKs arrive between records, so only an idle stream may use the
                // short timeout; the wait for the first record of a batch uses the long one.
                const size_t want = produced == 0 ? firstTimeoutMs : idleTimeoutMs;
                const size_t timeout = static_cast<size_t>(std::min<long long>(left.count(), want));
                auto [buf, n] = dev.readReport(timeout);
                if (n == 0) break;  // stream idle: this batch is complete
                // readReport zero-pads to 64 bytes, so only the transfer length can
                // tell a one-byte acknowledgement from a full-size report.
                if (isAckReport(buf) || n == 1) {
                    ++res.acks;
                    if (++quiet >= 8) {  // acknowledgements but no records: give up on it
                        res.truncated = true;
                        break;
                    }
                    continue;
                }
                if (isZeroReport(buf)) {
                    ++res.zeroReports;
                    if (++quiet >= 8) {
                        res.truncated = true;
                        break;
                    }
                    continue;
                }
                quiet = 0;
                ReadReport rr;
                rr.group = request;
                rr.index = produced++;
                rr.data = std::move(buf);
                res.reports.push_back(std::move(rr));
            }
        }
        if (produced == 0) break;  // end of the stored map
        if (res.truncated) break;
    }
    return res;
}

}  // namespace

ReadConfigResult readConfig(Device& dev, bool preferNewProtocol, size_t firstTimeoutMs,
                            size_t idleTimeoutMs, int buttons, int deadlineMs) {
    if (buttons <= 0) buttons = 3;  // request-count cap needs the button count
    ReadConfigResult res;
    for (bool fresh : {preferNewProtocol, !preferNewProtocol}) {
        res = readVariant(dev, fresh, firstTimeoutMs, idleTimeoutMs, buttons, deadlineMs);
        if (!res.reports.empty() || !res.error.empty()) break;  // device wedged: do not retry
    }
    return res;
}

}  // namespace ch57x

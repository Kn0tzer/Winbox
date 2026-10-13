#ifndef XWIRE_INPUT2_H
#define XWIRE_INPUT2_H

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <algorithm>

// XI2 wire encoding, independent of host Xlib and guest pointer size.
// Protocol: https://www.x.org/releases/X11R7.7/doc/inputproto/XI2proto.txt
class XWireInput2 {
public:
    static constexpr uint8_t opcode = 152;
    static constexpr uint16_t pointer = 2, keyboard = 3, slavePointer = 4, slaveKeyboard = 5, rawMotion = 17;
    struct Response { std::vector<uint8_t> bytes; uint8_t error = 0; };

    Response request(const uint8_t* req, uint32_t len, uint16_t sequence) {
        if (len < 4) return {{}, 16}; // BadLength
        const uint8_t minor = req[1];
        auto reply = header(minor, sequence);
        switch (minor) {
        case 1: // XI1 GetExtensionVersion (libXi initialization)
            if (len < 8 || 8u + read16(req + 4) > len) return {{}, 16};
            put16(reply, 8, 2); reply[12] = 1;
            break;
        case 47: // XIQueryVersion: XI 2.1 raw-event delivery during grabs
            if (len != 8) return {{}, 16};
            if (read16(req + 4) < 2) return {{}, 2}; // BadValue
            put16(reply, 8, 2);
            minorVersion = read16(req + 4) > 2 ? 1 : std::min<uint16_t>(read16(req + 6), 1);
            put16(reply, 10, minorVersion);
            break;
        case 45: // XIGetClientPointer
            if (len != 8) return {{}, 16};
            reply[8] = 1; put16(reply, 10, pointer);
            break;
        case 48: { // XIQueryDevice
            if (len != 8) return {{}, 16};
            const auto device = read16(req + 4);
            if (device > slaveKeyboard) return {{}, 128}; // BadDevice
            uint16_t count = 0;
            if (device <= 1 || device == pointer) { appendDevice(reply, pointer); ++count; }
            if (device <= 1 || device == keyboard) { appendDevice(reply, keyboard); ++count; }
            if (device == 0 || device == slavePointer) { appendDevice(reply, slavePointer); ++count; }
            if (device == 0 || device == slaveKeyboard) { appendDevice(reply, slaveKeyboard); ++count; }
            put16(reply, 8, count); put32(reply, 4, (reply.size() - 32) / 4);
            break;
        }
        case 46: { // XISelectEvents: each mask length is in four-byte units
            if (len < 12) return {{}, 16};
            const auto window = read32(req + 4);
            const auto count = read16(req + 8);
            size_t offset = 12;
            std::map<uint16_t, uint32_t> changes;
            for (unsigned i = 0; i < count; ++i) {
                if (offset + 4 > len) return {{}, 16};
                const auto device = read16(req + offset);
                const size_t bytes = size_t(read16(req + offset + 2)) * 4;
                if (device > slaveKeyboard) return {{}, 128};
                offset += 4;
                if (bytes > len - offset) return {{}, 16};
                uint32_t mask = bytes ? read32(req + offset) : 0;
                for (size_t j = 4; j < bytes; ++j) if (req[offset + j]) return {{}, 2};
                changes[device] = mask; offset += bytes;
            }
            if (offset != len) return {{}, 16};
            // Commit only after every mask has been validated.
            for (auto& entry : changes) selections[{window, entry.first}] = entry.second;
            return {};
        }
        default: return {{}, 1}; // BadRequest, never leave a reply reader parked
        }
        return {std::move(reply), 0};
    }

    bool selectsRawMotion(uint32_t root, uint16_t device = pointer) const {
        for (uint16_t selector : {uint16_t(0), uint16_t(1), device}) {
            if (selector == 1 && device > keyboard) continue; // AllMasterDevices
            auto it = selections.find({root, selector});
            if (it != selections.end() && (it->second & (1u << rawMotion))) return true;
        }
        return false;
    }

    std::vector<uint16_t> rawDevices(uint32_t root, bool corePointerGrabbed) const {
        std::vector<uint16_t> devices;
        // Xorg processes the physical event first. A core master-pointer grab
        // filters XI 2.0 master raw events from root delivery; slave events are
        // unaffected. XI 2.1 clients receive root raw events during grabs.
        if (selectsRawMotion(root, slavePointer)) devices.push_back(slavePointer);
        if (selectsRawMotion(root, pointer) && (!corePointerGrabbed || minorVersion >= 1)) devices.push_back(pointer);
        return devices;
    }

    static std::vector<uint8_t> motion(uint16_t sequence, uint32_t time, int32_t dx, int32_t dy, uint16_t device = pointer) {
        // GenericEvent + one mask word + two processed and two raw FP3232 axes.
        std::vector<uint8_t> event(68, 0);
        event[0] = 35; event[1] = opcode;
        put16(event, 2, sequence); put32(event, 4, 9);
        put16(event, 8, rawMotion); put16(event, 10, device);
        put32(event, 12, time); put16(event, 20, slavePointer); put16(event, 22, 1);
        put32(event, 32, 3); // valuators 0 and 1
        put32(event, 36, uint32_t(dx)); put32(event, 44, uint32_t(dy));
        put32(event, 52, uint32_t(dx)); put32(event, 60, uint32_t(dy));
        return event;
    }

private:
    uint16_t minorVersion = 0;
    std::map<std::pair<uint32_t, uint16_t>, uint32_t> selections;
    static uint16_t read16(const uint8_t* p) { return p[0] | uint16_t(p[1]) << 8; }
    static uint32_t read32(const uint8_t* p) { return read16(p) | uint32_t(read16(p + 2)) << 16; }
    static void put16(std::vector<uint8_t>& b, size_t at, uint16_t value) {
        b[at] = uint8_t(value); b[at + 1] = uint8_t(value >> 8);
    }
    static void put32(std::vector<uint8_t>& b, size_t at, uint32_t value) {
        put16(b, at, uint16_t(value)); put16(b, at + 2, uint16_t(value >> 16));
    }
    static std::vector<uint8_t> header(uint8_t minor, uint16_t sequence) {
        std::vector<uint8_t> reply(32, 0); reply[0] = 1; reply[1] = minor;
        put16(reply, 2, sequence); return reply;
    }
    static void appendDevice(std::vector<uint8_t>& b, uint16_t device) {
        const bool mouse = device == pointer || device == slavePointer;
        const bool master = device == pointer || device == keyboard;
        const uint16_t source = mouse ? slavePointer : slaveKeyboard;
        const std::string name = std::string(master ? "Boxedwine master " : "Boxedwine physical ") + (mouse ? "pointer" : "keyboard");
        size_t at = b.size(); b.resize(at + 12 + ((name.size() + 3) & ~size_t(3)), 0);
        put16(b, at, device); put16(b, at + 2, mouse ? (master ? 1 : 3) : (master ? 2 : 4));
        put16(b, at + 4, master ? (mouse ? keyboard : pointer) : (mouse ? pointer : keyboard));
        put16(b, at + 6, mouse ? 3 : 1);
        put16(b, at + 8, name.size()); b[at + 10] = 1;
        std::memcpy(b.data() + at + 12, name.data(), name.size());
        if (mouse) {
            at = b.size(); b.resize(at + 24, 0); // ButtonClass: 3 buttons, mask, 3 labels
            put16(b, at, 1); put16(b, at + 2, 6); put16(b, at + 4, source); put16(b, at + 6, 3);
            for (uint16_t axis = 0; axis < 2; ++axis) {
                at = b.size(); b.resize(at + 44, 0);
                put16(b, at, 2); put16(b, at + 2, 11); put16(b, at + 4, source); put16(b, at + 6, axis);
                // min=max=0: unbounded relative axes, one unit per pixel.
                put32(b, at + 36, 1); b[at + 40] = 0; // XIModeRelative
            }
        } else {
            at = b.size(); b.resize(at + 8 + 248 * 4, 0);
            put16(b, at, 0); put16(b, at + 2, 250); put16(b, at + 4, source); put16(b, at + 6, 248);
            for (unsigned key = 8; key < 256; ++key) put32(b, at + 8 + (key - 8) * 4, key);
        }
    }
};
#endif

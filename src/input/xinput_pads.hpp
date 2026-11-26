#pragma once
// Xbox One / Series and other XInput controllers. They are offered to the game
// through the same path as the Sony pads, with each button in the position of
// its PlayStation counterpart (A = cross, B = circle, X = square, Y = triangle,
// LB/RB = L1/R1, LT/RT = L2/R2, View/Menu = Create/Options, LS/RS = L3/R3),
// so the PS2 layout presets apply unchanged.
#include "sony_raw_input.h"
#include <vector>

namespace xinput_pads {

struct Gamepad { WORD buttons; BYTE left_trigger, right_trigger; SHORT lx, ly, rx, ry; };
struct XState { DWORD packet; Gamepad pad; };
using GetStateFn = DWORD(WINAPI*)(DWORD, XState*);

inline GetStateFn get_state() {
    static GetStateFn function = [] {
        for (const wchar_t* name : {L"xinput1_4.dll", L"xinput9_1_0.dll", L"xinput1_3.dll"}) {
            wchar_t path[MAX_PATH];
            const UINT length = GetSystemDirectoryW(path, MAX_PATH);
            if (!length || length + 20 >= MAX_PATH) return GetStateFn(nullptr);
            wcscat_s(path, L"\\");
            wcscat_s(path, name);
            if (HMODULE module = LoadLibraryW(path))
                if (auto f = reinterpret_cast<GetStateFn>(GetProcAddress(module, "XInputGetState"))) return f;
        }
        return GetStateFn(nullptr);
    }();
    return function;
}

inline float stick(SHORT value) { return value < 0 ? value / 32768.f : value / 32767.f; }

// Sony HID bit numbers (sony_layout::buttons[].hid_bit).
inline uint32_t sony_buttons(const Gamepad& pad) {
    struct Map { WORD xinput; unsigned bit; };
    static constexpr Map map[] = {
        {0x1000, 1}, {0x2000, 2}, {0x4000, 0}, {0x8000, 3},   // A B X Y
        {0x0100, 4}, {0x0200, 5},                             // LB RB
        {0x0020, 8}, {0x0010, 9},                             // View (Back), Menu (Start)
        {0x0040, 10}, {0x0080, 11},                           // LS RS
    };
    uint32_t bits = 0;
    for (const auto& entry : map) if (pad.buttons & entry.xinput) bits |= 1u << entry.bit;
    if (pad.left_trigger > 40) bits |= 1u << 6;
    if (pad.right_trigger > 40) bits |= 1u << 7;
    return bits;
}

inline int hat(WORD buttons) {
    const bool up = buttons & 1, down = buttons & 2, left = buttons & 4, right = buttons & 8;
    if (up && right) return 1;
    if (right && down) return 3;
    if (down && left) return 5;
    if (left && up) return 7;
    if (up) return 0;
    if (right) return 2;
    if (down) return 4;
    if (left) return 6;
    return -1;
}

// Polled from the hub; disconnected slots are re-probed at most once a second
// (XInputGetState on an empty slot is slow).
class Pads {
    bool connected_[4] = {};
    ULONGLONG next_probe_[4] = {};
    uint64_t sequence_[4] = {};
public:
    void append(std::vector<sonyraw::Device>& out) {
        const auto function = get_state();
        if (!function) return;
        const ULONGLONG now = GetTickCount64();
        for (DWORD slot = 0; slot < 4; ++slot) {
            if (!connected_[slot] && now < next_probe_[slot]) continue;
            XState state = {};
            connected_[slot] = function(slot, &state) == ERROR_SUCCESS;
            if (!connected_[slot]) { next_probe_[slot] = now + 1000; continue; }
            sonyraw::Device device;
            device.vid = 0x045e;
            device.pid = 0x02ff;
            device.instance_key = 0x58494e5055540000ull | slot;  // "XINPUT" + slot, stable
            device.instance_key_stable = true;
            device.family = sonyraw::Family::Xbox;
            auto& s = device.state;
            s.valid = true;
            s.lx = stick(state.pad.lx);
            s.ly = stick(state.pad.ly);
            s.rx = stick(state.pad.rx);
            s.ry = stick(state.pad.ry);
            s.l2 = state.pad.left_trigger / 255.f;
            s.r2 = state.pad.right_trigger / 255.f;
            s.buttons = sony_buttons(state.pad);
            s.hat = hat(state.pad.buttons);
            s.sequence = ++sequence_[slot];
            out.push_back(device);
        }
    }
};

}  // namespace xinput_pads

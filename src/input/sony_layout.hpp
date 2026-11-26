#pragma once
#include <array>
#include <cstdint>
#include <cstring>

namespace sony_layout {

constexpr unsigned sony_slot = 3;
constexpr unsigned web_attack = 8, web_swing = 9, pause = 12;
constexpr unsigned left_trigger = 27, right_trigger = 28, options = 30;
// PS2 layout: SELECT = overhead map, on the touch pad click, (PC "BackButton", action 13) and
// R3 = toggle lock-on camera (PC "CameraCenter", action 15). The PC defaults
// put them on generic buttons 7 and 10, which on a Sony pad are L2 (cleared
// for web) and Options (already pause).
constexpr unsigned overhead_map = 13, camera_lock = 15, touch_pad = 34, right_stick_click = 32, create_share = 29;

struct Control { unsigned hid_bit; const char* name; const char* tutorial_token; };

// Keep the game's default action order while reading Sony's HID usage order.
inline constexpr std::array<Control, 14> buttons{{
    {1, "Cross", "~CROSS"}, {2, "Circle", "~CIRCLE"},
    {0, "Square", "~SQUARE"}, {3, "Triangle", "~TRIANGLE"},
    {4, "L1", nullptr}, {5, "R1", nullptr}, {6, "L2", "~L2"}, {7, "R2", "~R2"},
    {8, "Create / Share", nullptr}, {9, "Options", nullptr},
    {10, "L3", nullptr}, {11, "R3", nullptr}, {12, "PlayStation", nullptr},
    {13, "Touch pad", nullptr},
}};

struct Binding { std::uint32_t device; std::uint32_t code; std::uint32_t reserved; };
using BindingRow = std::array<Binding, 6>;
struct BindingMap {
    std::uint32_t count;
    std::array<BindingRow, 50> actions;
};
static_assert(sizeof(BindingMap) == 3604);

inline unsigned tutorial_action(const char* token) {
    if (!std::strcmp(token, "~cross")) return 4;
    if (!std::strcmp(token, "~circle")) return 5;
    if (!std::strcmp(token, "~square")) return 6;
    if (!std::strcmp(token, "~triangle")) return 7;
    if (!std::strcmp(token, "~l2")) return web_attack;
    if (!std::strcmp(token, "~r2")) return web_swing;
    return 0;
}

inline const char* prompt_token(const Binding* bindings, unsigned count, unsigned device_type) {
    for (unsigned i = 0; i < count; ++i) {
        const auto& binding = bindings[i];
        if (binding.device != device_type || binding.code < 21 || binding.code >= 21 + buttons.size()) continue;
        if (const char* token = buttons[binding.code - 21].tutorial_token) return token;
    }
    return nullptr;
}

struct DefaultBinding { unsigned action, code; };
inline constexpr DefaultBinding ps2_defaults[] = {
    {4, 21}, {5, 22}, {6, 23}, {7, 24},   // face buttons
    {pause, options},
    {overhead_map, touch_pad}, {camera_lock, right_stick_click},
    {16, 4}, {17, 3}, {18, 2}, {19, 1},    // left stick
    {20, 10}, {21, 9}, {22, 8}, {23, 7},   // right stick
};

inline bool fill_missing_binding(Binding* bindings, unsigned count, unsigned device, unsigned code) {
    for (unsigned i = 0; i < count; ++i) if (bindings[i].device == device) return false;
    for (unsigned i = count; i > 0; --i) {
        if (bindings[i - 1].device != 0) continue;
        bindings[i - 1] = {device, code, 0};
        return true;
    }
    return false;
}

inline bool is_trigger(unsigned code) {
    // Includes both halves of the legacy Z and Rz axes and the two
    // independent, digital Sony buttons.
    return code == 5 || code == 6 || code == 11 || code == 12 ||
           code == left_trigger || code == right_trigger;
}

inline void bind_web_trigger(BindingRow& row, unsigned code) {
    Binding* first = nullptr;
    for (auto& binding : row) {
        if (binding.device != sony_slot) continue;
        if (!first) {
            first = &binding;
            if (binding.code != code) binding = {sony_slot, code, 0};
        } else {
            binding = {};
        }
    }
    if (!first) fill_missing_binding(row.data(), static_cast<unsigned>(row.size()), sony_slot, code);
}

inline void apply_web_controls(BindingMap& map, bool gameplay, bool xbox = false) {
    if (map.count > map.actions.size()) return;
    for (unsigned action = 0; action < map.count; ++action) {
        auto& row = map.actions[action];
        if (gameplay && (action == web_attack || action == web_swing)) {
            bind_web_trigger(row, action == web_attack ? left_trigger : right_trigger);
            continue;
        }
        for (auto& binding : row) {
            if (binding.device == sony_slot && is_trigger(binding.code)) binding = {};
            // Options belongs to pause only; the PC default also left camera
            // centre on it (generic button 10).
            if (gameplay && action != pause && binding.device == sony_slot && binding.code == options) binding = {};
            // First v7 build put the map on Create/Share (29); the game may
            // have saved it. The map now lives on the touch pad click.
            // On Xbox pads (no touch pad) the map is on View, Create's position.
            if (gameplay && action == overhead_map && binding.device == sony_slot &&
                binding.code == (xbox ? touch_pad : create_share)) binding = {};
        }
    }
}

}

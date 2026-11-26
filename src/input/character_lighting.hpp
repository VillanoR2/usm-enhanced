#pragma once
#include "lighting_capture.hpp"
#include "mask_ab.hpp"

// Local A/B experiment for the audited PC build. The game's ordinary renderer
// still chooses lights, textures, shader variant and outlines.
namespace character_lighting {

using Render = void(__thiscall*)(void*);
inline Render original_person = nullptr;
inline Render original_solid = nullptr;
inline const unsigned char* executable = nullptr;
inline bool adjusted = false;
inline bool f8_down = false;
inline bool capture_enabled = false;
inline bool seen[4][2] = {};

constexpr unsigned materials[] = {0x7a6b6091, 0x5ec62c24, 0x7bb44a0e, 0xb77e4a53};
constexpr float trial_gain = .80f;

inline int material_index(unsigned name) {
    for (unsigned i = 0; i < _countof(materials); ++i)
        if (materials[i] == name) return static_cast<int>(i);
    return -1;
}

class ColorScope {
    float* color_;
    float saved_[3];
public:
    explicit ColorScope(float* color) : color_(color), saved_{color[0], color[1], color[2]} {
        for (unsigned channel = 0; channel < 3; ++channel) color_[channel] *= trial_gain;
    }
    ~ColorScope() { memcpy(color_, saved_, sizeof(saved_)); }
    ColorScope(const ColorScope&) = delete;
    ColorScope& operator=(const ColorScope&) = delete;
};

inline void record(unsigned material, const float* color) {
    auto& recorded = seen[material][adjusted];
    if (recorded) return;
    HANDLE file = CreateFileW(L"lighting-trial.csv", FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                             OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    const bool shader = executable[0x572ab0] != 0;
    char row[160] = {};
    const int length = sprintf_s(row, "%08X,%s,%s,%.6f,%.6f,%.6f\r\n", materials[material],
        adjusted ? "darkened" : "original", shader ? "shader" : "fixed",
        color[0], color[1], color[2]);
    DWORD written = 0;
    if (length > 0) recorded = WriteFile(file, row, static_cast<DWORD>(length), &written, nullptr) &&
                                written == static_cast<DWORD>(length);
    CloseHandle(file);
}

inline void render(void* node, Render original) {
    const auto* material_ptr = static_cast<const unsigned char*>(node) + 0x18;
    unsigned char* material = nullptr;
    memcpy(&material, material_ptr, sizeof(material));
    if (!sony_prompts::readable(material, 0x38)) { original(node); return; }

    const unsigned* name = nullptr;
    memcpy(&name, material, sizeof(name));
    if (!sony_prompts::readable(name, sizeof(*name))) { original(node); return; }
    const int index = material_index(*name);
    if (index < 0) { original(node); return; }
    if (mask_ab::target_first >= 0) {
        mask_ab::render(node, original, index, executable);
        return;
    }
    if (capture_enabled) {
        original(node);
        if (index >= 2) lighting_capture::mark_venom(executable);
        return;
    }

    DWORD foreground_pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &foreground_pid);
    const bool pressed = foreground_pid == GetCurrentProcessId() && (GetAsyncKeyState(VK_F8) & 0x8000);
    if (pressed && !f8_down) adjusted = !adjusted;
    f8_down = pressed;

    auto* color = reinterpret_cast<float*>(material + 0x28);
    if (!adjusted) {
        record(static_cast<unsigned>(index), color);
        original(node);
        return;
    }
    MEMORY_BASIC_INFORMATION info = {};
    if (!VirtualQuery(color, &info, sizeof(info)) ||
        (info.Protect != PAGE_READWRITE && info.Protect != PAGE_WRITECOPY)) {
        original(node);
        return;
    }
    ColorScope scope(color);
    record(static_cast<unsigned>(index), color);
    original(node);
}

inline void __fastcall person(void* node, void*) { render(node, original_person); }
inline void __fastcall solid(void* node, void*) { render(node, original_solid); }

inline BOOL CALLBACK install_once(PINIT_ONCE, PVOID, PVOID*) {
    const auto* image = sony_prompts::executable;
    if (!image) return TRUE;
    auto* person_slot = reinterpret_cast<ULONG_PTR*>(const_cast<unsigned char*>(image + 0x471d1c));
    auto* solid_slot = reinterpret_cast<ULONG_PTR*>(const_cast<unsigned char*>(image + 0x471d3c));
    if (*person_slot != reinterpret_cast<ULONG_PTR>(image + 0x1c4c0) ||
        *solid_slot != reinterpret_cast<ULONG_PTR>(image + 0x1e4b0)) return TRUE;
    constexpr size_t span = 0x20 + sizeof(ULONG_PTR);
    DWORD previous = 0;
    if (!VirtualProtect(person_slot, span, PAGE_READWRITE, &previous)) return TRUE;
    executable = image;
    world_filter::install(image);
    if (city_probe::enabled || facade_probe::extend_filter) facade_probe::install(image);
    original_person = reinterpret_cast<Render>(*person_slot);
    original_solid = reinterpret_cast<Render>(*solid_slot);
    *person_slot = reinterpret_cast<ULONG_PTR>(&person);
    *solid_slot = reinterpret_cast<ULONG_PTR>(&solid);
    DWORD ignored = 0;
    VirtualProtect(person_slot, span, previous, &ignored);
    return TRUE;
}

inline void install() {
    wchar_t enabled[2] = {};
    capture_enabled = GetEnvironmentVariableW(L"USM_PC_COLOR_CAPTURE", enabled, _countof(enabled)) == 1 &&
                      enabled[0] == L'1';
    if (!mask_ab::enabled() && !capture_enabled && (GetEnvironmentVariableW(L"USM_PC_LIGHTING_TRIAL", enabled, _countof(enabled)) != 1 ||
        enabled[0] != L'1')) return;
    static INIT_ONCE once = INIT_ONCE_STATIC_INIT;
    InitOnceExecuteOnce(&once, install_once, nullptr, nullptr);
}

}

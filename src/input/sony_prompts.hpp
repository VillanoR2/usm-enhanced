#pragma once
#include "sony_glyph_texture.hpp"

// Compatibility shim for the audited PC executable. The launcher also checks
// its full SHA-256. Selecting an icon does not change any gameplay binding.
namespace sony_prompts {
using ReplaceToken = char* (__cdecl*)(char*, const char*, const char*);
inline ReplaceToken original_replace = nullptr;
inline const unsigned char* executable = nullptr;

inline bool readable(const void* address, size_t size) {
    MEMORY_BASIC_INFORMATION info = {};
    if (!VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT
        || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    const auto offset = reinterpret_cast<uintptr_t>(address) - reinterpret_cast<uintptr_t>(info.BaseAddress);
    return offset < info.RegionSize && size <= info.RegionSize - offset;
}

// Family of the controller in use: one connected pad with a valid state.
inline sonyraw::Family pad_family() {
    std::shared_ptr<sony_di::Hub> hub;
    {
        std::lock_guard<std::mutex> guard(sony_di::hub_lock);
        hub = sony_di::shared_hub.lock();
    }
    if (!hub) return sonyraw::Family::Unsupported;
    const auto devices = hub->devices();
    return devices.size() == 1 && devices.front().state.valid ? devices.front().family : sonyraw::Family::Unsupported;
}

inline bool single_sony_active() {
    const auto family = pad_family();
    return family != sonyraw::Family::Unsupported && family != sonyraw::Family::Xbox;
}

inline bool pad_active() { return pad_family() != sonyraw::Family::Unsupported; }

inline unsigned char* profile_from_global(unsigned rva) {
    unsigned char* profile = nullptr;
    memcpy(&profile, executable + rva, sizeof(profile));
    return profile;
}

inline unsigned char* active_profile() {
    const auto* input = profile_from_global(0x587948);
    if (!input || !readable(input + 0x129d8, sizeof(void*))) return nullptr;
    unsigned char* profile = nullptr;
    memcpy(&profile, input + 0x129d8, sizeof(profile));
    return profile;
}

inline sony_layout::BindingMap* binding_map(unsigned char* profile) {
    if (!profile) return nullptr;
    auto* map = reinterpret_cast<sony_layout::BindingMap*>(profile + 0x18);
    return readable(map, sizeof(*map)) && map->count <= map->actions.size() ? map : nullptr;
}

inline const char* current_token(unsigned action) {
    if (!single_sony_active()) return nullptr;
    auto* profile = active_profile();
    if (profile == profile_from_global(0x565c0c)) profile = profile_from_global(0x565c10);
    const auto* map = binding_map(profile);
    if (!map || action >= map->count) return nullptr;
    const auto& row = map->actions[action];
    return sony_layout::prompt_token(row.data(), static_cast<unsigned>(row.size()), sony_layout::sony_slot);
}

// First keyboard (1) or mouse (2) binding of an action, as device<<16|code.
inline uint32_t key_binding(unsigned char* profile, unsigned action) {
    const auto* map = binding_map(profile);
    if (!map || action >= map->count) return 0;
    for (const auto& binding : map->actions[action]) {
        if ((binding.device == 1 && binding.code && binding.code < 0x100) ||
            (binding.device == 2 && binding.code && binding.code < 0x20))
            return (binding.device << 16) | binding.code;
    }
    return 0;
}

// Key bound to an action now: the active map first (menus have their own),
// then the gameplay map, where tutorial prompts are defined.
inline uint32_t current_key(unsigned action) {
    if (uint32_t key = key_binding(active_profile(), action)) return key;
    return key_binding(profile_from_global(0x565c10), action);
}

inline bool icons_enabled() {
    wchar_t value[2] = {};
    static const bool enabled = !(GetEnvironmentVariableW(L"USM_PC_KEY_ICONS", value, 2) == 1 && value[0] == L'0');
    return enabled;
}

inline bool keyboard_prompts() { return icons_enabled() && !pad_active(); }

// First controller binding of an action (device 3..12 = joystick slots).
inline unsigned joystick_binding(unsigned char* profile, unsigned action) {
    const auto* map = binding_map(profile);
    if (!map || action >= map->count) return 0;
    for (const auto& binding : map->actions[action])
        if (binding.device >= 3 && binding.device <= 12 && binding.code && binding.code < 0x40) return binding.code;
    return 0;
}

inline unsigned current_button(unsigned action) {
    if (unsigned code = joystick_binding(active_profile(), action)) return code;
    return joystick_binding(profile_from_global(0x565c10), action);
}

// Icon available for an action with the device in use (key id or pad icon).
inline bool has_icon(sonyraw::Family family, unsigned action) {
    if (family == sonyraw::Family::Unsupported) return current_key(action) != 0;
    return pad_glyph::icon_for(family, current_button(action)) != pad_icons::none;
}

inline void apply_controller_defaults() {
    if (!executable) return;
    const auto* active = active_profile();
    const bool xbox = pad_family() == sonyraw::Family::Xbox;
    if (auto* menu = binding_map(profile_from_global(0x565c0c))) {
        sony_layout::apply_web_controls(*menu, false, xbox);
        // The PC menu puts its default back command on DirectInput button 3.
        if (menu->count > 7) {
            auto& back = menu->actions[7][3];
            if (back.device == sony_layout::sony_slot && back.code == 24) back.code = 22;
        }
    }
    // Prepare the game maps while the menu is still active. The game can
    // construct its first prompt before it switches to the gameplay map.
    for (unsigned rva : {0x565c10u, 0x565c14u, 0x565c1cu}) {
        auto* profile = profile_from_global(rva);
        if (rva == 0x565c1c && profile != active) continue;
        auto* map = binding_map(profile);
        if (!map) continue;
        sony_layout::apply_web_controls(*map, true, xbox);
        for (const auto& binding : sony_layout::ps2_defaults) {
            if (binding.action >= map->count) continue;
            auto& row = map->actions[binding.action];
            const unsigned code = xbox && binding.action == sony_layout::overhead_map
                ? sony_layout::create_share : binding.code;
            sony_layout::fill_missing_binding(row.data(), static_cast<unsigned>(row.size()),
                                              sony_layout::sony_slot, code);
        }
    }
}

inline char* __cdecl replace(char* text, const char* token, const char* fallback) {
    try {
        // Hand the token back in upper case so the glyph pass (0x609bf0)
        // emits a button-font glyph; draw_font_node picks the icon for the
        // keyboard, DualSense, DualShock or Xbox pad in use.
        if (icons_enabled()) {
            const auto family = pad_family();
            if (!std::strcmp(token, "~both_lr")) {
                if (has_icon(family, 8) && has_icon(family, 9)) fallback = "~L2 & ~R2";
            } else {
                for (const auto& entry : key_glyph::tokens)
                    if (!std::strcmp(token, entry.token)) {
                        if (has_icon(family, entry.action)) fallback = entry.glyph_token;
                        break;
                    }
            }
        }
    } catch (const std::bad_alloc&) {}
    return original_replace(text, token, fallback);
}

// PC-only localized strings (Data\usm_lt?.usm) with a hard-wired key name.
// "RESUME" is shown by 11 pause / cut-scene screens through multi-line text,
// which runs the glyph pass, so the key phrase becomes an icon token.
using Lookup = const char* (__cdecl*)(void*, const char*);
inline Lookup original_lookup = nullptr;

inline const char* __cdecl localized(void* table, const char* key) {
    const char* text = original_lookup(table, key);
    try {
        if (!text || !key || std::strcmp(key, "RESUME")) return text;
        if (!icons_enabled()) return text;
        const char* token = "~NAVIG_ALL";  // glyph '0': space key, or the pad's confirm button
        static const char* const phrases[] = {"LA BARRA ESP.", "LA BARRA SPAZIATRICE", "\"SPACE\"",
                                               "\"ESPACE\"", "LEERTASTE"};
        for (const char* phrase : phrases) {
            const char* at = std::strstr(text, phrase);
            if (!at) continue;
            static char buffer[2][256];
            static unsigned which = 0;
            char* out = buffer[which ^= 1];
            const size_t head = static_cast<size_t>(at - text);
            const char* tail = at + std::strlen(phrase);
            if (head + std::strlen(token) + std::strlen(tail) + 1 > sizeof(buffer[0])) return text;
            memcpy(out, text, head);
            strcpy_s(out + head, sizeof(buffer[0]) - head, token);
            strcat_s(out, sizeof(buffer[0]), tail);
            return out;
        }
    } catch (const std::bad_alloc&) {}
    return text;
}

inline void __fastcall draw_font_node(void* node, void*) {
    const char previous = sony_glyph::glyph_code;
    const int previous_key = key_glyph::current_cell;
    const int previous_pad = pad_glyph::current_cell;
    sony_glyph::glyph_code = 0;
    key_glyph::current_cell = -1;
    pad_glyph::current_cell = -1;
    const auto* bytes = static_cast<const unsigned char*>(node);
    const char* text = nullptr;
    const void* font = nullptr;
    memcpy(&text, bytes + 0xc, sizeof(text));
    memcpy(&font, bytes + 0x10, sizeof(font));
    if (icons_enabled() && font == profile_from_global(0x537b10) && text && text[0] && !text[1]) {
        try {
            const auto family = pad_family();
            const uint32_t fixed = key_glyph::fixed_key_for_glyph(text[0]);
            const int action = key_glyph::action_for_glyph(text[0]);
            if (family == sonyraw::Family::Unsupported) {
                uint32_t key = fixed;
                if (!key && action >= 0) key = current_key(static_cast<unsigned>(action));
                if (key) key_glyph::current_cell = key_glyph::cell_for(key);
            } else if (fixed) {
                pad_glyph::current_cell = pad_glyph::confirm_icon(family);
            } else if (action >= 0) {
                pad_glyph::current_cell = pad_glyph::icon_for(family, current_button(static_cast<unsigned>(action)));
            }
        } catch (const std::bad_alloc&) {}
    }
    sony_glyph::original_font_draw(node);
    sony_glyph::glyph_code = previous;
    key_glyph::current_cell = previous_key;
    pad_glyph::current_cell = previous_pad;
}

inline BOOL CALLBACK install_once(PINIT_ONCE, PVOID, PVOID*) {
    const auto* image = reinterpret_cast<const unsigned char*>(GetModuleHandleW(nullptr));
    if (!image) return TRUE;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0xf00) return TRUE;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(image + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386
        || nt->OptionalHeader.SizeOfImage != 0x591000) return TRUE;

    constexpr unsigned replace_rva = 0x209580;
    // cross, triangle, square, circle, l2, r2, r3, both_lr, select, forward, left, start
    constexpr unsigned call_sites[] = {0x20a51c, 0x20a54a, 0x20a578, 0x20a5a5, 0x20a5ff, 0x20a62d, 0x20a65a,
                                       0x20a6a4, 0x20a6ff, 0x20a72d, 0x20a75b, 0x20a789};
    for (unsigned site : call_sites) {
        const auto* code = image + site;
        if (code[0] != 0xe8) return TRUE;
        uint32_t displacement = 0;
        memcpy(&displacement, code + 1, sizeof(displacement));
        if (site + 5 + displacement != replace_rva) return TRUE;
    }

    constexpr unsigned draw_rva = 0x37e3e0;
    auto* draw_vtable = reinterpret_cast<ULONG_PTR*>(const_cast<unsigned char*>(image + 0x48ebb4));
    const auto draw_address = reinterpret_cast<ULONG_PTR>(image + draw_rva);
    if (*draw_vtable != draw_address) return TRUE;
    auto* draw_call = const_cast<unsigned char*>(image + 0x37e778);
    constexpr BYTE draw_signature[] = {0xff, 0x92, 0x4c, 0x01, 0, 0};
    if (memcmp(draw_call, draw_signature, sizeof(draw_signature)) != 0) return TRUE;
    sony_glyph::load_atlas();  // optional: keyboard icons need no game assets

    auto* text_patch = const_cast<unsigned char*>(image + call_sites[0]);
    constexpr size_t span = call_sites[_countof(call_sites) - 1] + 5 - call_sites[0];
    DWORD text_old = 0, draw_old = 0, table_old = 0, ignored = 0;
    if (!VirtualProtect(text_patch, span, PAGE_EXECUTE_READWRITE, &text_old)) return TRUE;
    if (!VirtualProtect(draw_call, sizeof(draw_signature), PAGE_EXECUTE_READWRITE, &draw_old)) {
        VirtualProtect(text_patch, span, text_old, &ignored);
        return TRUE;
    }
    if (!VirtualProtect(draw_vtable, sizeof(*draw_vtable), PAGE_READWRITE, &table_old)) {
        VirtualProtect(draw_call, sizeof(draw_signature), draw_old, &ignored);
        VirtualProtect(text_patch, span, text_old, &ignored);
        return TRUE;
    }
    executable = image;
    original_replace = reinterpret_cast<ReplaceToken>(image + replace_rva);
    sony_glyph::original_font_draw = reinterpret_cast<sony_glyph::FontDraw>(draw_address);
    for (unsigned site : call_sites) {
        auto* code = const_cast<unsigned char*>(image + site);
        const uint32_t displacement = reinterpret_cast<uintptr_t>(&replace) - reinterpret_cast<uintptr_t>(code + 5);
        memcpy(code + 1, &displacement, sizeof(displacement));
    }
    const uint32_t draw_displacement = reinterpret_cast<uintptr_t>(&sony_glyph::draw_quad) - reinterpret_cast<uintptr_t>(draw_call + 5);
    draw_call[0] = 0xe8;
    memcpy(draw_call + 1, &draw_displacement, sizeof(draw_displacement));
    draw_call[5] = 0x90;
    *draw_vtable = reinterpret_cast<ULONG_PTR>(&draw_font_node);

    // Lookups of "RESUME" (push 0x8955f4; push table; call 0x81c580).
    constexpr unsigned lookup_rva = 0x41c580;
    constexpr unsigned resume_calls[] = {0x225972, 0x22624f, 0x226de2, 0x227993, 0x22a8a5, 0x22ac29,
                                         0x22ca75, 0x22d5d3, 0x241f90, 0x2423f0, 0x242743};
    bool resume_ok = true;
    for (unsigned site : resume_calls) {
        const auto* code = image + site;
        uint32_t displacement = 0, key = 0;
        memcpy(&displacement, code + 1, sizeof(displacement));
        memcpy(&key, code - 5, sizeof(key));
        if (code[0] != 0xe8 || site + 5 + displacement != lookup_rva || code[-6] != 0x68 ||
            key != 0x8955f4) resume_ok = false;
    }
    if (resume_ok) {
        original_lookup = reinterpret_cast<Lookup>(image + lookup_rva);
        for (unsigned site : resume_calls) {
            auto* code = const_cast<unsigned char*>(image + site);
            DWORD old = 0;
            if (!VirtualProtect(code, 5, PAGE_EXECUTE_READWRITE, &old)) continue;
            const uint32_t displacement = reinterpret_cast<uintptr_t>(&localized) - reinterpret_cast<uintptr_t>(code + 5);
            memcpy(code + 1, &displacement, sizeof(displacement));
            VirtualProtect(code, 5, old, &old);
            FlushInstructionCache(GetCurrentProcess(), code, 5);
        }
    }
    FlushInstructionCache(GetCurrentProcess(), text_patch, span);
    FlushInstructionCache(GetCurrentProcess(), draw_call, sizeof(draw_signature));
    VirtualProtect(draw_vtable, sizeof(*draw_vtable), table_old, &ignored);
    VirtualProtect(draw_call, sizeof(draw_signature), draw_old, &ignored);
    VirtualProtect(text_patch, span, text_old, &ignored);
    return TRUE;
}

inline void install() {
    wchar_t enabled[2] = {};
    if (GetEnvironmentVariableW(L"USM_PC_SONY_PROMPTS", enabled, _countof(enabled)) != 1 ||
        enabled[0] != L'1') return;
    static INIT_ONCE once = INIT_ONCE_STATIC_INIT;
    InitOnceExecuteOnce(&once, install_once, nullptr, nullptr);
}

}

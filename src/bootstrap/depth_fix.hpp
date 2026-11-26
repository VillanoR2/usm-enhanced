#pragma once
// Depth precision fixes against distant flicker (z-fighting):
//  - World projections (far >= 1000) get a minimum near plane (stock 0.1).
//  - A 16-bit auto depth buffer is upgraded to 24-bit (see d3d9_refresh.hpp).
// usm_mod.ini [Graficos] PlanoCercano=0.3  (0.1 = original, max 1.0)

namespace depth_fix {

inline float near_min = 0.3f;
inline DWORD anisotropy_requested = 16;  // usm_mod.ini FiltradoAnisotropico (0/1 = off)

inline void log_line(const char* text) {
    wchar_t path[MAX_PATH];
    DWORD n = GetTempPathW(MAX_PATH, path);
    const wchar_t name[] = L"usm_mod.log";
    if (!n || n + sizeof(name) / sizeof(wchar_t) >= MAX_PATH) return;
    for (unsigned i = 0; i < sizeof(name) / sizeof(wchar_t); ++i) path[n + i] = name[i];
    HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD length = 0, written = 0;
    while (text[length]) ++length;
    WriteFile(file, text, length, &written, nullptr);
    WriteFile(file, "\r\n", 2, &written, nullptr);
    CloseHandle(file);
}

// Same body as USM 0x76B820 (cdecl, plain ret), with the near plane clamp.
inline void __cdecl set_perspective(float fov, float near_plane, float far_plane) {
    if (far_plane >= 1000.0f && near_plane < near_min) near_plane = near_min;
    char* camera = *reinterpret_cast<char**>(0x971F00);
    *reinterpret_cast<int*>(camera + 0x33C) = 1;
    *reinterpret_cast<float*>(camera + 0x3EC) = fov;
    *reinterpret_cast<float*>(camera + 0x3F0) = near_plane;
    *reinterpret_cast<float*>(camera + 0x3F4) = far_plane;
    *reinterpret_cast<char*>(camera + 0x3E4) = 1;
}

inline void install(float configured) {
    if (configured < 0.1f) return;                 // invalid: keep stock
    if (configured > 1.0f) configured = 1.0f;
    near_min = configured;
    if (near_min <= 0.1f) return;                  // 0.1 = stock
    static const BYTE entry[] = {0x8B, 0x0D, 0x00, 0x1F, 0x97, 0x00, 0xD9, 0x44, 0x24, 0x04};
    static const BYTE inline_a[] = {0xC7, 0x81, 0xF0, 0x03, 0x00, 0x00, 0xCD, 0xCC, 0xCC, 0x3D};
    static const BYTE inline_b[] = {0xC7, 0x80, 0xF0, 0x03, 0x00, 0x00, 0xCD, 0xCC, 0xCC, 0x3D};
    if (!draw_distance::same(0x76B820, entry, sizeof(entry)) ||
        !draw_distance::same(0x76C4C2, inline_a, sizeof(inline_a)) ||
        !draw_distance::same(0x76C506, inline_b, sizeof(inline_b))) {
        log_line("depth_fix: unexpected code, near plane left unchanged");
        return;
    }
    BYTE jump[5] = {0xE9};
    const LONG relative = static_cast<LONG>(reinterpret_cast<ULONG_PTR>(&set_perspective) - (0x76B820 + 5));
    for (int i = 0; i < 4; ++i) jump[1 + i] = static_cast<BYTE>(relative >> (8 * i));
    draw_distance::write(0x76B820, jump, sizeof(jump));
    draw_distance::write(0x76C4C8, &near_min, 4);
    draw_distance::write(0x76C50C, &near_min, 4);
}

}  // namespace depth_fix

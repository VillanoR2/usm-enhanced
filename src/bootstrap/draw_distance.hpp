#pragma once
// Draw distance: scales the stock visibility/LOD distance classes and the
// 167-unit mesh/"lego" distance of USM.exe 1.0. Offsets match the patterns
// documented by ThirteenAG's UltimateSpiderMan.FusionFix (GPL-3.0); every value is
// checked against the stock one before it is written.
//
// usm_mod.ini (next to USM.exe):
//   [Graficos]
//   DistanciaDibujado=6.0   ; 1.0 = original, max 6.0
// The environment variable USM_PC_DRAW_DISTANCE overrides the file.

extern "C" int _fltused;

namespace draw_distance {

// No CRT: tiny float parser for "2", "2.5", "2,5".
inline float parse(const wchar_t* s) {
    float value = 0.0f, scale = 0.0f;
    bool any = false;
    for (; *s; ++s) {
        if (*s >= L'0' && *s <= L'9') {
            any = true;
            if (scale == 0.0f) value = value * 10.0f + float(*s - L'0');
            else { value += float(*s - L'0') * scale; scale *= 0.1f; }
        } else if ((*s == L'.' || *s == L',') && scale == 0.0f) {
            scale = 0.1f;
        } else if (*s != L' ' && *s != L'\t') {
            break;
        }
    }
    return any ? value : -1.0f;
}

// Reads [Graficos] key from usm_mod.ini next to the module; env overrides it.
inline float setting(HMODULE self, const wchar_t* key, const wchar_t* env, const wchar_t* fallback) {
    wchar_t text[32];
    text[0] = 0;
    if (GetEnvironmentVariableW(env, text, 32) - 1u < 31u) return parse(text);
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(self, path, MAX_PATH);
    if (!n || n >= MAX_PATH) return parse(fallback);
    while (n && path[n - 1] != L'\\') --n;
    const wchar_t name[] = L"usm_mod.ini";
    if (n + sizeof(name) / sizeof(wchar_t) >= MAX_PATH) return parse(fallback);
    for (unsigned i = 0; i < sizeof(name) / sizeof(wchar_t); ++i) path[n + i] = name[i];
    GetPrivateProfileStringW(L"Graficos", key, fallback, text, 32, path);
    return parse(text);
}

inline float configured_scale(HMODULE self) {
    return setting(self, L"DistanciaDibujado", L"USM_PC_DRAW_DISTANCE", L"6.0");
}

inline bool write(ULONG_PTR address, const void* data, DWORD size) {
    void* target = reinterpret_cast<void*>(address);
    DWORD old = 0;
    if (!VirtualProtect(target, size, PAGE_EXECUTE_READWRITE, &old)) return false;
    const BYTE* from = static_cast<const BYTE*>(data);
    BYTE* to = static_cast<BYTE*>(target);
    for (DWORD i = 0; i < size; ++i) to[i] = from[i];
    DWORD ignored = 0;
    VirtualProtect(target, size, old, &ignored);
    FlushInstructionCache(GetCurrentProcess(), target, size);
    return true;
}

inline bool same(ULONG_PTR address, const BYTE* bytes, unsigned size) {
    const BYTE* p = reinterpret_cast<const BYTE*>(address);
    for (unsigned i = 0; i < size; ++i) if (p[i] != bytes[i]) return false;
    return true;
}

inline float at(ULONG_PTR address) { return *reinterpret_cast<const float*>(address); }

inline float render_linear[16];
inline float render_squared[16];
inline float applied_scale = 1.0f;

// City objects fade out over the last 10 units before their distance class L
// (0x53D1B6: start = min(L, X - 10), alpha = 1 - (d - start) * 0.1). Designed
// for the stock distances; at x6 the fade is ~1% of the distance and reads as
// popping. Make it proportional to what is actually visible:
// length = clamp(min(L, X) * 0.25, 10, 150). X (view distance, [esp+0x20]) is
// short in cut-scenes; a fade based on L alone started before the camera and
// left nearby buildings half transparent there.
inline float fade_fraction = 0.25f, fade_maximum = 150.0f, fade_minimum = 10.0f, fade_inverse = 0.1f;
inline DWORD fade_continue = 0x53D1C0;
inline bool fade_installed = false;

__declspec(naked) inline void proportional_fade() {   // st0 = L
    __asm {
        fld dword ptr [esp + 0x20]      // X, L
        fld st(1)                       // L, X, L
        fcomi st, st(1)
        fcmovnb st, st(1)               // L >= X -> X: min(L, X), X, L
        fmul dword ptr [fade_fraction]  // len, X, L
        fld dword ptr [fade_maximum]    // max, len, X, L
        fcomi st, st(1)
        fcmovnb st, st(1)               // max >= len -> len
        fstp st(1)                      // len, X, L
        fld dword ptr [fade_minimum]    // min, len, X, L
        fcomi st, st(1)
        fcmovb st, st(1)                // min < len -> len
        fstp st(1)                      // len, X, L
        fld1
        fdiv st, st(1)                  // 1/len, len, X, L
        fstp dword ptr [fade_inverse]   // len, X, L
        fsubp st(1), st                 // X - len, L
        jmp dword ptr [fade_continue]
    }
}

inline void install_fade() {
    static const BYTE fade_code[] = {0xD9, 0x44, 0x24, 0x20, 0xD8, 0x25, 0x34, 0x1D, 0x87, 0x00};
    static const BYTE fade_rate[] = {0xD8, 0x0D, 0xA4, 0x20, 0x88, 0x00};
    if (!same(0x53D1B6, fade_code, sizeof(fade_code)) || !same(0x53D1DB, fade_rate, sizeof(fade_rate)) ||
        at(0x871D34) != 10.0f)
        return;
    BYTE jump[10] = {0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90, 0x90};
    const DWORD relative = reinterpret_cast<ULONG_PTR>(&proportional_fade) - (0x53D1B6 + 5);
    for (int i = 0; i < 4; ++i) jump[1 + i] = static_cast<BYTE>(relative >> (8 * i));
    const DWORD inverse = reinterpret_cast<ULONG_PTR>(&fade_inverse);
    write(0x53D1DD, &inverse, 4);
    write(0x53D1B6, jump, sizeof(jump));
    fade_installed = true;
}

inline void install(HMODULE self) {
    float scale = configured_scale(self);
    if (!(scale > 1.0f)) return;            // 1.0, invalid or "0": keep stock
    if (scale > 6.0f) scale = 6.0f;         // tested limit ("Alto"); 7.0 ("Ultra") could stall new games
    const ULONG_PTR base = reinterpret_cast<ULONG_PTR>(GetModuleHandleW(nullptr));
    if (base != 0x400000) return;           // offsets are for the stock image base

    // Instruction contexts that must be intact before anything is written.
    static const BYTE mesh_op[] = {0xD8, 0x1D, 0x30, 0xA4, 0x88, 0x00, 0x89, 0x44, 0x24, 0x1C};
    static const BYTE lego_op[] = {0xD8, 0x1D, 0x30, 0xA4, 0x88, 0x00, 0xC7, 0x44, 0x24, 0x10};
    static const BYTE mesh_imm[] = {0xC7, 0x44, 0x24, 0x14, 0x00, 0x00, 0x27, 0x43};
    static const BYTE lego_imm[] = {0xC7, 0x44, 0x24, 0x10, 0x00, 0x00, 0x27, 0x43};
    static const BYTE region_op[] = {0xD8, 0x05, 0x6C, 0xA4, 0x88, 0x00, 0xD9, 0xFA};
    static const BYTE linear_op[] = {0xD9, 0x04, 0x8D, 0x48, 0x1E, 0x92, 0x00};
    static const BYTE squared_op[] = {0xD9, 0x04, 0x8D, 0x88, 0x1E, 0x92, 0x00};
    static const BYTE entity_op[] = {0xD9, 0x04, 0x85, 0x88, 0x1E, 0x92, 0x00};
    static const BYTE lod_op[] = {0xD9, 0x04, 0x8D, 0x88, 0x1E, 0x92, 0x00};
    static const float linear_stock[16] = {10, 15, 20, 30, 40, 50, 60, 70, 80, 90, 100, 150, 200, 280, 500, 100000};
    if (!same(0x53CEFC, mesh_op, sizeof(mesh_op)) || !same(0x53D377, lego_op, sizeof(lego_op)) ||
        !same(0x53CF06, mesh_imm, sizeof(mesh_imm)) || !same(0x53D37D, lego_imm, sizeof(lego_imm)) ||
        !same(0x5470B6, region_op, sizeof(region_op)) || !same(0x53D1AF, linear_op, sizeof(linear_op)) ||
        !same(0x51E8B6, squared_op, sizeof(squared_op)) || !same(0x51E8E3, entity_op, sizeof(entity_op)) ||
        !same(0x53A2E4, lod_op, sizeof(lod_op)) ||
        at(0x88A430) != 167.0f || at(0x88A46C) != 27889.0f)
        return;
    for (unsigned i = 0; i < 16; ++i)
        if (at(0x921E48 + 4 * i) != linear_stock[i] || at(0x921E88 + 4 * i) != linear_stock[i] * linear_stock[i])
            return;

    // Distance classes. The stock tables stay intact: the loader uses them to
    // assign each entity its authored class (0x4CB45E). Only the render-side
    // reads are redirected to scaled copies; scaling in place cancelled out.
    for (unsigned i = 0; i < 16; ++i) {
        const float linear = i < 15 ? linear_stock[i] * scale : linear_stock[i];
        render_linear[i] = linear;
        render_squared[i] = linear * linear;
    }
    const DWORD linear_table = reinterpret_cast<ULONG_PTR>(render_linear);
    const DWORD squared_table = reinterpret_cast<ULONG_PTR>(render_squared);
    write(0x53D1B2, &linear_table, 4);   // render fade, linear
    write(0x51E8B9, &squared_table, 4);  // visibility, squared
    write(0x51E8E6, &squared_table, 4);  // entity visibility, squared
    write(0x53A2E7, &squared_table, 4);  // LOD switch, squared
    // City mesh / "lego" building distance and its squared region radius.
    const float mesh = 167.0f * scale, region = mesh * mesh;
    write(0x88A430, &mesh, 4);
    write(0x53CF0A, &mesh, 4);
    write(0x53D381, &mesh, 4);
    write(0x88A46C, &region, 4);
    install_fade();
    applied_scale = scale;
}

}  // namespace draw_distance

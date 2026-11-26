#pragma once
// Keyboard and mouse prompt icons. Original art drawn at run time (no game
// assets). Every glyph character of the game's button font stands for one
// gameplay action; with no Sony pad active the glyph is drawn as the key or
// mouse button currently bound to that action, so remapping is reflected.
#include <d3d9.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwchar>

namespace key_glyph {

inline thread_local int current_cell = -1;  // set while a font node draws

// Glyph character (USM.exe token table at 0x609bf0) -> gameplay action
// (record table 0x922940 / label switch 0x595a1c).
inline int action_for_glyph(char glyph) {
    switch (glyph) {
    case '"': return 4;   // ~cross / ~jump        Jump
    case '%': return 5;   // ~circle / ~counter    StickToWalls
    case '#': return 6;   // ~square / ~attack     Punch
    case '$': return 7;   // ~triangle / ~web      Kick
    case '&': return 8;   // ~l2 / ~modifier
    case '!': return 9;   // ~r2 / ~swing
    case '-': return 12;  // ~start                Pause
    case '1': return 13;  // ~select               BackButton (map)
    case '(': return 15;  // ~recenter             CameraCenter
    case '5': return 15;  // ~click_right_stick    CameraCenter
    case ')': return 16;  // ~forward
    case ',': return 17;  // ~back
    case '+': return 18;  // ~left
    case '*': return 19;  // ~right
    default: return -1;
    }
}

// Glyphs repurposed for fixed keys: '0' (~navig_all) is the hard-wired
// "press space to resume" of the PC pause/cut-scene screens.
inline uint32_t fixed_key_for_glyph(char glyph) {
    return glyph == '0' ? 0x10039u : 0u;
}

// Lower-case text token (expanded by 0x60a4a0) -> action and the upper-case
// token that 0x609bf0 turns into the matching glyph.
struct TokenMap { const char* token; unsigned action; const char* glyph_token; };
inline constexpr TokenMap tokens[] = {
    {"~cross", 4, "~CROSS"}, {"~circle", 5, "~CIRCLE"}, {"~square", 6, "~SQUARE"},
    {"~triangle", 7, "~TRIANGLE"}, {"~l2", 8, "~L2"}, {"~r2", 9, "~R2"},
    {"~start", 12, "~START"}, {"~select", 13, "~SELECT"}, {"~r3", 15, "~CLICK_RIGHT_STICK"},
    {"~forward", 16, "~FORWARD"}, {"~left", 18, "~LEFT"},
};

constexpr unsigned cell = 64, columns = 8, rows = 4, width = cell * columns, height = cell * rows;
inline std::array<DWORD, width * height> pixels = {};
inline std::array<uint32_t, columns * rows> cell_key = {};    // 0 = empty
inline std::array<uint32_t, columns * rows> cell_used = {};
inline uint32_t clock_tick = 0;
inline bool dirty = false;

struct Look { const wchar_t* text; const wchar_t* sub; int mouse; bool symbol; };
// mouse: 0 = key, 1 left, 2 right, 3 middle, 4 wheel up, 5 wheel down, 6 extra

inline Look describe(uint32_t id, wchar_t* buffer, size_t size) {
    const unsigned device = id >> 16, code = id & 0xffff;
    if (device == 2) {
        switch (code) {
        case 9: return {L"", nullptr, 1, false};
        case 10: return {L"", nullptr, 2, false};
        case 11: return {L"", nullptr, 3, false};
        case 1: return {L"", nullptr, 4, false};
        case 2: return {L"", nullptr, 5, false};
        default:
            swprintf(buffer, size, L"%u", code >= 12 && code <= 16 ? code - 8 : code);
            return {buffer, nullptr, 6, false};
        }
    }
    switch (code) {
    case 0x01: return {L"ESC", nullptr, 0, false};
    case 0x0E: return {L"\x232B", nullptr, 0, true};          // backspace
    case 0x0F: return {L"TAB", nullptr, 0, false};
    case 0x1C: case 0x9C: return {L"\x21B5", nullptr, 0, true};  // enter
    case 0x1D: case 0x9D: return {L"CTRL", nullptr, 0, false};
    case 0x2A: case 0x36: return {L"\x21E7", nullptr, 0, true};  // shift
    case 0x38: return {L"ALT", nullptr, 0, false};
    case 0xB8: return {L"ALT", L"GR", 0, false};
    case 0x39: return {L"\x2423", nullptr, 0, true};         // space
    case 0x3A: return {L"BLOQ", L"MAY", 0, false};
    case 0xC8: return {L"\x2191", nullptr, 0, true};
    case 0xD0: return {L"\x2193", nullptr, 0, true};
    case 0xCB: return {L"\x2190", nullptr, 0, true};
    case 0xCD: return {L"\x2192", nullptr, 0, true};
    case 0xC7: return {L"INIC", nullptr, 0, false};
    case 0xCF: return {L"FIN", nullptr, 0, false};
    case 0xC9: return {L"RE", L"P\xC1G", 0, false};
    case 0xD1: return {L"AV", L"P\xC1G", 0, false};
    case 0xD2: return {L"INS", nullptr, 0, false};
    case 0xD3: return {L"SUPR", nullptr, 0, false};
    case 0x57: return {L"F11", nullptr, 0, false};
    case 0x58: return {L"F12", nullptr, 0, false};
    case 0x37: return {L"*", L"NUM", 0, false};
    case 0x4A: return {L"-", L"NUM", 0, false};
    case 0x4E: return {L"+", L"NUM", 0, false};
    case 0xB5: return {L"/", L"NUM", 0, false};
    default: break;
    }
    if (code >= 0x3B && code <= 0x44) {
        swprintf(buffer, size, L"F%u", code - 0x3A);
        return {buffer, nullptr, 0, false};
    }
    // Numeric keypad digits (scan codes 0x47-0x53, non-extended).
    static const wchar_t pad[] = L"789-456+1230.";
    if (code >= 0x47 && code <= 0x53 && pad[code - 0x47] != L'-' && pad[code - 0x47] != L'+') {
        buffer[0] = pad[code - 0x47]; buffer[1] = 0;
        return {buffer, L"NUM", 0, false};
    }
    // Letters, digits and punctuation follow the user's keyboard layout.
    const UINT scan = (code & 0x7f) | (code & 0x80 ? 0xE000 : 0);
    const UINT vk = MapVirtualKeyW(scan, MAPVK_VSC_TO_VK_EX);
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) {
        buffer[0] = static_cast<wchar_t>(vk); buffer[1] = 0;
        return {buffer, nullptr, 0, false};
    }
    const UINT ch = vk ? MapVirtualKeyW(vk, MAPVK_VK_TO_CHAR) & 0x7fffffff : 0;
    if (ch > 0x20) {
        buffer[0] = static_cast<wchar_t>(ch);
        buffer[1] = 0;
        CharUpperW(buffer);
        return {buffer, nullptr, 0, false};
    }
    const LONG lparam = static_cast<LONG>(((code & 0x7f) << 16) | (code & 0x80 ? 1 << 24 : 0));
    wchar_t name[32] = {};
    if (GetKeyNameTextW(lparam, name, 32) > 0) {
        CharUpperW(name);
        size_t n = 0;
        while (name[n] && n < 4 && n + 1 < size) { buffer[n] = name[n]; ++n; }
        buffer[n] = 0;
        return {buffer, nullptr, 0, false};
    }
    swprintf(buffer, size, L"%02X", code);
    return {buffer, nullptr, 0, false};
}

// ---- software drawing -------------------------------------------------
struct Canvas { DWORD* p; };

inline float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

inline float rounded_rect(float x, float y, float x0, float y0, float x1, float y1, float r) {
    const float cx = (x0 + x1) * .5f, cy = (y0 + y1) * .5f;
    const float hx = (x1 - x0) * .5f - r, hy = (y1 - y0) * .5f - r;
    const float dx = (std::max)(std::fabs(x - cx) - hx, 0.f), dy = (std::max)(std::fabs(y - cy) - hy, 0.f);
    const float outside = std::sqrt(dx * dx + dy * dy) - r;
    const float inside = (std::min)((std::max)(std::fabs(x - cx) - hx, std::fabs(y - cy) - hy), 0.f);
    return clamp01(.5f - (outside + inside));
}

inline void blend(DWORD& dst, DWORD rgb, float a) {
    if (a <= 0.f) return;
    const float da = ((dst >> 24) & 255) / 255.f;
    const float oa = a + da * (1.f - a);
    auto mix = [&](int shift) {
        const float s = ((rgb >> shift) & 255) / 255.f, d = ((dst >> shift) & 255) / 255.f;
        const float c = oa > 0.f ? (s * a + d * da * (1.f - a)) / oa : 0.f;
        return static_cast<DWORD>(c * 255.f + .5f) << shift;
    };
    dst = (static_cast<DWORD>(oa * 255.f + .5f) << 24) | mix(16) | mix(8) | mix(0);
}

inline DWORD lerp_rgb(DWORD a, DWORD b, float t) {
    auto c = [&](int s) {
        const float x = ((a >> s) & 255) * (1.f - t) + ((b >> s) & 255) * t;
        return static_cast<DWORD>(x + .5f) << s;
    };
    return c(16) | c(8) | c(0);
}

// Grey-scale text coverage from GDI, composited in the given colour.
inline void draw_text(DWORD* cellp, const wchar_t* text, bool symbol, int box_x, int box_y, int box_w, int box_h,
                      int max_height, DWORD rgb) {
    if (!text || !*text) return;
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return;
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = cell;
    info.bmiHeader.biHeight = -static_cast<LONG>(cell);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits) { if (bitmap) DeleteObject(bitmap); DeleteDC(dc); return; }
    HGDIOBJ old_bitmap = SelectObject(dc, bitmap);
    const int length = static_cast<int>(wcslen(text));
    HFONT font = nullptr;
    for (int size = max_height; size >= 8; size -= 2) {
        if (font) DeleteObject(font);
        font = CreateFontW(-size, 0, 0, 0, symbol ? FW_BOLD : FW_HEAVY, 0, 0, 0, DEFAULT_CHARSET,
                           OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH,
                           symbol ? L"Segoe UI Symbol" : L"Arial");
        SelectObject(dc, font);
        SIZE extent = {};
        GetTextExtentPoint32W(dc, text, length, &extent);
        if (extent.cx <= box_w && extent.cy <= box_h + size / 3) break;
    }
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    RECT area = {box_x, box_y, box_x + box_w, box_y + box_h};
    DrawTextW(dc, text, length, &area, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    GdiFlush();
    const DWORD* mask = static_cast<const DWORD*>(bits);
    for (unsigned y = 0; y < cell; ++y)
        for (unsigned x = 0; x < cell; ++x) {
            const DWORD m = mask[y * cell + x];
            const unsigned coverage = (std::max)({(m >> 16) & 255u, (m >> 8) & 255u, m & 255u});
            if (coverage) blend(cellp[y * width + x], rgb, coverage / 255.f);
        }
    SelectObject(dc, old_bitmap);
    if (font) DeleteObject(font);
    DeleteObject(bitmap);
    DeleteDC(dc);
}

constexpr DWORD ink = 0x161616, edge = 0x1c1c1c, face_top = 0xfafafa, face_bottom = 0xcfd3d8,
                accent = 0xff8a1e, shadow = 0x3a3d42;

inline void render(uint32_t id, unsigned index) {
    DWORD* cellp = pixels.data() + (index / columns) * cell * width + (index % columns) * cell;
    for (unsigned y = 0; y < cell; ++y) for (unsigned x = 0; x < cell; ++x) cellp[y * width + x] = 0;
    wchar_t buffer[16] = {};
    const Look look = describe(id, buffer, 16);
    if (look.mouse == 0) {
        for (unsigned y = 0; y < cell; ++y)
            for (unsigned x = 0; x < cell; ++x) {
                const float px = x + .5f, py = y + .5f;
                DWORD& d = cellp[y * width + x];
                blend(d, shadow, rounded_rect(px, py, 3, 7, 61, 62, 11));          // key side
                blend(d, edge, rounded_rect(px, py, 3, 2, 61, 57, 11));            // outline
                const float t = clamp01((py - 5.f) / 48.f);
                blend(d, lerp_rgb(face_top, face_bottom, t), rounded_rect(px, py, 6, 5, 58, 54, 8));
            }
        if (look.sub) {
            draw_text(cellp, look.text, look.symbol, 8, 6, 48, 28, 26, ink);
            draw_text(cellp, look.sub, false, 8, 32, 48, 18, 15, ink);
        } else {
            if (look.symbol && look.text[0] == L'\x2423') {   // space bar: drawn bracket
                for (unsigned y = 0; y < cell; ++y)
                    for (unsigned x = 0; x < cell; ++x) {
                        const float px = x + .5f, py = y + .5f;
                        const float a = (std::max)({rounded_rect(px, py, 13, 35, 51, 41, 2),
                                                    rounded_rect(px, py, 13, 22, 19, 41, 2),
                                                    rounded_rect(px, py, 45, 22, 51, 41, 2)});
                        blend(cellp[y * width + x], ink, a);
                    }
            } else {
                draw_text(cellp, look.text, look.symbol, 6, 4, 52, 48, look.symbol ? 40 : 36, ink);
            }
        }
    } else {
        const float x0 = 14, y0 = 3, x1 = 50, y1 = 61, split_y = 27;
        for (unsigned y = 0; y < cell; ++y)
            for (unsigned x = 0; x < cell; ++x) {
                const float px = x + .5f, py = y + .5f;
                DWORD& d = cellp[y * width + x];
                blend(d, edge, rounded_rect(px, py, x0, y0, x1, y1, 17));
                const float body = rounded_rect(px, py, x0 + 3, y0 + 3, x1 - 3, y1 - 3, 14);
                if (body <= 0.f) continue;
                DWORD color = lerp_rgb(face_top, face_bottom, clamp01((py - 5.f) / 56.f));
                const bool top = py < split_y;
                const bool left = px < 32.f;
                if ((look.mouse == 1 && top && left) || (look.mouse == 2 && top && !left)) color = accent;
                blend(d, color, body);
                // Button seams.
                if ((top && std::fabs(px - 32.f) < 1.2f) || std::fabs(py - split_y) < 1.2f)
                    blend(d, edge, body);
            }
        // Wheel.
        const bool wheel = look.mouse >= 3 && look.mouse <= 5;
        for (unsigned y = 0; y < cell; ++y)
            for (unsigned x = 0; x < cell; ++x)
                blend(cellp[y * width + x], wheel ? accent : edge, rounded_rect(x + .5f, y + .5f, 28, 9, 36, 23, 4));
        if (look.mouse == 4) draw_text(cellp, L"\x25B2", true, 14, 30, 36, 26, 22, accent);
        if (look.mouse == 5) draw_text(cellp, L"\x25BC", true, 14, 30, 36, 26, 22, accent);
        if (look.mouse == 6) draw_text(cellp, look.text, false, 14, 30, 36, 26, 22, ink);
    }
    dirty = true;
}

inline int cell_for(uint32_t id) {
    ++clock_tick;
    unsigned victim = 0;
    for (unsigned i = 0; i < cell_key.size(); ++i) {
        if (cell_key[i] == id) { cell_used[i] = clock_tick; return static_cast<int>(i); }
        if (cell_used[i] < cell_used[victim]) victim = i;
    }
    cell_key[victim] = id;
    cell_used[victim] = clock_tick;
    render(id, victim);
    return static_cast<int>(victim);
}

inline IDirect3DTexture9* texture_for(IDirect3DDevice9* device) {
    static IDirect3DDevice9* owner = nullptr;
    static IDirect3DTexture9* texture = nullptr;
    if (device != owner) {
        if (texture) texture->Release();
        texture = nullptr;
        owner = device;
        dirty = true;
    }
    if (!texture && FAILED(device->CreateTexture(width, height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
                                                 &texture, nullptr))) {
        texture = nullptr;
        return nullptr;
    }
    if (dirty) {
        D3DLOCKED_RECT lock = {};
        if (FAILED(texture->LockRect(0, &lock, nullptr, 0))) return nullptr;
        for (unsigned y = 0; y < height; ++y)
            memcpy(static_cast<BYTE*>(lock.pBits) + y * lock.Pitch, pixels.data() + y * width, width * sizeof(DWORD));
        texture->UnlockRect(0);
        dirty = false;
    }
    return texture;
}

}  // namespace key_glyph

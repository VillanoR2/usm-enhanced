#pragma once
#include <d3d9.h>
#include "key_glyph.hpp"
#include "pad_glyph.hpp"

namespace sony_glyph {
inline bool atlas_loaded = false;  // PS2 icons are optional (need the user's ISO)

using FontDraw = void(__thiscall*)(void*);
inline FontDraw original_font_draw = nullptr;
inline thread_local char glyph_code = 0;

constexpr unsigned atlas_width = 256, atlas_height = 128;
inline std::array<DWORD, atlas_width * atlas_height> atlas_pixels = {};

// PS2 i_button_icons.fdf. The glyphs for codes 33–38 have consecutive
// 28×28 cells in the first row, beginning at (12, 6).
inline bool supported(char code) { return code >= '!' && code <= '&'; }

inline bool load_atlas() {
    HANDLE file = CreateFileW(L"sony-ps2-atlas.bgra", GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION info = {};
    DWORD received = 0;
    constexpr DWORD size = static_cast<DWORD>(sizeof(atlas_pixels));
    const bool loaded = GetFileInformationByHandle(file, &info) &&
        !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
        info.nFileSizeHigh == 0 && info.nFileSizeLow == size &&
        ReadFile(file, atlas_pixels.data(), size, &received, nullptr) && received == size;
    CloseHandle(file);
    atlas_loaded = loaded;
    return loaded;
}

inline IDirect3DTexture9* texture_for(IDirect3DDevice9* device) {
    static IDirect3DDevice9* owner = nullptr;
    static IDirect3DTexture9* texture = nullptr;
    if (device != owner) {
        if (texture) texture->Release();
        texture = nullptr;
        owner = device;
    }
    if (texture) return texture;
    IDirect3DTexture9* candidate = nullptr;
    if (FAILED(device->CreateTexture(atlas_width, atlas_height, 1, 0, D3DFMT_A8R8G8B8,
                                     D3DPOOL_MANAGED, &candidate, nullptr))) return nullptr;
    D3DLOCKED_RECT lock = {};
    if (FAILED(candidate->LockRect(0, &lock, nullptr, 0))) {
        candidate->Release();
        return nullptr;
    }
    for (unsigned y = 0; y < atlas_height; ++y)
        memcpy(static_cast<BYTE*>(lock.pBits) + y * lock.Pitch, atlas_pixels.data() + y * atlas_width,
               atlas_width * sizeof(DWORD));
    if (FAILED(candidate->UnlockRect(0))) {
        candidate->Release();
        return nullptr;
    }
    texture = candidate;
    return texture;
}

struct GlyphVertex { float x, y, z; DWORD diffuse; float u, v; };
static_assert(sizeof(GlyphVertex) == 24);

inline HRESULT STDMETHODCALLTYPE draw_quad(IDirect3DDevice9* device, D3DPRIMITIVETYPE kind,
                                          UINT count, const void* data, UINT stride) {
    const bool pad_icon = pad_glyph::current_cell >= 0;
    if ((pad_icon || key_glyph::current_cell >= 0) && kind == D3DPT_TRIANGLESTRIP && count == 2
        && data && stride == sizeof(GlyphVertex)) {
        GlyphVertex quad[4];
        memcpy(quad, data, sizeof(quad));
        float u_min = quad[0].u, u_max = quad[0].u, v_min = quad[0].v, v_max = quad[0].v;
        float x_min = quad[0].x, x_max = quad[0].x;
        for (const auto& vertex : quad) {
            u_min = std::min(u_min, vertex.u); u_max = std::max(u_max, vertex.u);
            v_min = std::min(v_min, vertex.v); v_max = std::max(v_max, vertex.v);
            x_min = std::min(x_min, vertex.x); x_max = std::max(x_max, vertex.x);
        }
        IDirect3DBaseTexture9* previous = nullptr;
        if (u_min < u_max && v_min < v_max && SUCCEEDED(device->GetTexture(0, &previous))) {
            // Keep the key square even where the stock glyph is wider.
            float aspect = 1.f;
            if (previous && previous->GetType() == D3DRTYPE_TEXTURE) {
                D3DSURFACE_DESC desc = {};
                if (SUCCEEDED(static_cast<IDirect3DTexture9*>(previous)->GetLevelDesc(0, &desc)) && desc.Height)
                    aspect = ((u_max - u_min) * desc.Width) / ((v_max - v_min) * desc.Height);
            }
            auto* atlas = pad_icon ? pad_glyph::texture_for(device) : key_glyph::texture_for(device);
            if (atlas && SUCCEEDED(device->SetTexture(0, atlas))) {
                const float center = (x_min + x_max) * .5f;
                const float shrink = aspect > 1.1f ? 1.f / aspect : 1.f;
                const unsigned index = static_cast<unsigned>(pad_icon ? pad_glyph::current_cell : key_glyph::current_cell);
                const unsigned columns = pad_icon ? pad_icons::columns : key_glyph::columns;
                const float size = static_cast<float>(pad_icon ? pad_icons::cell : key_glyph::cell);
                const float atlas_w = static_cast<float>(pad_icon ? pad_icons::width : key_glyph::width);
                const float atlas_h = static_cast<float>(pad_icon ? pad_icons::height : key_glyph::height);
                const float cell_x = (index % columns) * size, cell_y = (index / columns) * size;
                for (auto& vertex : quad) {
                    const float horizontal = (vertex.u - u_min) / (u_max - u_min);
                    const float vertical = (vertex.v - v_min) / (v_max - v_min);
                    vertex.u = (cell_x + .5f + horizontal * (size - 1)) / atlas_w;
                    vertex.v = (cell_y + .5f + vertical * (size - 1)) / atlas_h;
                    vertex.x = center + (vertex.x - center) * shrink;
                    vertex.diffuse |= 0x00ffffff;  // keep the key colours, only the fade
                }
                const HRESULT result = device->DrawPrimitiveUP(kind, count, quad, stride);
                device->SetTexture(0, previous);
                if (previous) previous->Release();
                if (SUCCEEDED(result)) return result;
                return device->DrawPrimitiveUP(kind, count, data, stride);
            }
            if (previous) previous->Release();
        }
        return device->DrawPrimitiveUP(kind, count, data, stride);
    }
    if (atlas_loaded && supported(glyph_code) && kind == D3DPT_TRIANGLESTRIP && count == 2
        && data && stride == sizeof(GlyphVertex)) {
        GlyphVertex quad[4];
        memcpy(quad, data, sizeof(quad));
        float u_min = quad[0].u, u_max = quad[0].u, v_min = quad[0].v, v_max = quad[0].v;
        for (const auto& vertex : quad) {
            u_min = std::min(u_min, vertex.u); u_max = std::max(u_max, vertex.u);
            v_min = std::min(v_min, vertex.v); v_max = std::max(v_max, vertex.v);
        }
        if (u_min < u_max && v_min < v_max) {
            IDirect3DBaseTexture9* previous = nullptr;
            if (SUCCEEDED(device->GetTexture(0, &previous))) {
                auto* atlas = texture_for(device);
                if (atlas && SUCCEEDED(device->SetTexture(0, atlas))) {
                    const float cell_x = 12.f + 29.f * (glyph_code - '!');
                    constexpr float cell_y = 6.f, cell_size = 28.f;
                    for (auto& vertex : quad) {
                        const float horizontal = (vertex.u - u_min) / (u_max - u_min);
                        const float vertical = (vertex.v - v_min) / (v_max - v_min);
                        vertex.u = (cell_x + horizontal * cell_size) / atlas_width;
                        vertex.v = (cell_y + vertical * cell_size) / atlas_height;
                    }
                    const HRESULT result = device->DrawPrimitiveUP(kind, count, quad, stride);
                    device->SetTexture(0, previous);
                    if (previous) previous->Release();
                    if (SUCCEEDED(result)) return result;
                    return device->DrawPrimitiveUP(kind, count, data, stride);
                }
                if (previous) previous->Release();
            }
        }
    }
    return device->DrawPrimitiveUP(kind, count, data, stride);
}

}

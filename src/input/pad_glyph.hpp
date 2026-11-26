#pragma once
// Controller prompt icons: Kenney "Input Prompts" 1.5 (CC0), embedded in the
// DLL as resource 101 (pad_icons.bin, see make_pad_atlas.py).
//   DualSense / Edge  -> PS5 white symbols, Create, Options, touch pad
//   DualShock 4       -> classic colour symbols (as on PS2), Share, Options
//   Xbox / XInput     -> Xbox One colour A/B/X/Y, LB/RB/LT/RT, View, Menu
#include <d3d9.h>
#include "pad_icons.hpp"
#include "sony_raw_input.h"

namespace pad_glyph {

inline thread_local int current_cell = -1;

// Game joystick binding code (0x820890): 1-16 axis halves, 17-20 POV,
// 21+ buttons in Sony HID order (sony_layout::buttons).
inline int icon_for(sonyraw::Family family, unsigned code) {
    using namespace pad_icons;
    const bool xbox = family == sonyraw::Family::Xbox;
    const bool dualsense = family == sonyraw::Family::DualSense || family == sonyraw::Family::DualSenseEdge;
    if (code >= 21 && code <= 34) {
        static const int dualsense_icons[] = {ds_cross, ds_circle, ds_square, ds_triangle, ps_l1, ps_r1, ps_l2,
                                              ps_r2, ds_create, ds_options, ps_l3, ps_r3, none, ds_touchpad};
        static const int dualshock_icons[] = {d4_cross, d4_circle, d4_square, d4_triangle, ps_l1, ps_r1, ps_l2,
                                              ps_r2, d4_share, d4_options, ps_l3, ps_r3, none, d4_touchpad};
        static const int xbox_icons[] = {xb_a, xb_b, xb_x, xb_y, xb_lb, xb_rb, xb_lt, xb_rt,
                                         xb_view, xb_menu, xb_ls, xb_rs, none, none};
        const int* table = xbox ? xbox_icons : dualsense ? dualsense_icons : dualshock_icons;
        return table[code - 21];
    }
    if (code >= 1 && code <= 12) {
        static const int ps_axes[] = {ps_ls_right, ps_ls_left, ps_ls_down, ps_ls_up, ps_l2, ps_l2,
                                      ps_rs_right, ps_rs_left, ps_rs_down, ps_rs_up, ps_r2, ps_r2};
        static const int xb_axes[] = {xb_ls_right, xb_ls_left, xb_ls_down, xb_ls_up, xb_lt, xb_lt,
                                      xb_rs_right, xb_rs_left, xb_rs_down, xb_rs_up, xb_rt, xb_rt};
        return (xbox ? xb_axes : ps_axes)[code - 1];
    }
    if (code >= 17 && code <= 20) {
        static const int ps_pov[] = {ps_dpad_up, ps_dpad_right, ps_dpad_down, ps_dpad_left};
        static const int xb_pov[] = {xb_dpad_up, xb_dpad_right, xb_dpad_down, xb_dpad_left};
        return (xbox ? xb_pov : ps_pov)[code - 17];
    }
    return none;
}

inline int confirm_icon(sonyraw::Family family) { return icon_for(family, 21); }

inline const BYTE* atlas_pixels() {
    static const BYTE* pixels = [] {
        HMODULE self = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCWSTR>(&atlas_pixels), &self)) return static_cast<const BYTE*>(nullptr);
        HRSRC info = FindResourceW(self, MAKEINTRESOURCEW(101), MAKEINTRESOURCEW(10));  // RT_RCDATA
        if (!info || SizeofResource(self, info) != pad_icons::width * pad_icons::height * 4) return static_cast<const BYTE*>(nullptr);
        HGLOBAL data = LoadResource(self, info);
        return data ? static_cast<const BYTE*>(LockResource(data)) : static_cast<const BYTE*>(nullptr);
    }();
    return pixels;
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
    const BYTE* pixels = atlas_pixels();
    if (!pixels) return nullptr;
    IDirect3DTexture9* candidate = nullptr;
    if (FAILED(device->CreateTexture(pad_icons::width, pad_icons::height, 1, 0, D3DFMT_A8R8G8B8,
                                     D3DPOOL_MANAGED, &candidate, nullptr))) return nullptr;
    D3DLOCKED_RECT lock = {};
    if (FAILED(candidate->LockRect(0, &lock, nullptr, 0))) { candidate->Release(); return nullptr; }
    for (unsigned y = 0; y < pad_icons::height; ++y)
        memcpy(static_cast<BYTE*>(lock.pBits) + y * lock.Pitch, pixels + y * pad_icons::width * 4, pad_icons::width * 4);
    candidate->UnlockRect(0);
    texture = candidate;
    return texture;
}

}  // namespace pad_glyph

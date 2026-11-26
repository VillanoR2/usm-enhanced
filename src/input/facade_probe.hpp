#pragma once

namespace facade_probe {

enum class Route : unsigned { none, simple, trilinear, building };
using Render = void (__thiscall*)(void*);
inline Render trilinear_render = nullptr;
inline Render building_render = nullptr;
inline thread_local Route route = Route::none;
inline Route hidden = Route::none;
inline unsigned affected_draws = 0;
inline bool installed = false;
inline bool extend_filter = false;

inline void configure() {
    wchar_t value[2] = {};
    extend_filter = world_filter::available &&
        GetEnvironmentVariableW(L"USM_PC_CITY_FILTER_EXTENDED", value, _countof(value)) == 1 && value[0] == L'1';
}

inline Route current() {
    return route != Route::none ? route : (world_filter::simple_depth ? Route::simple : Route::none);
}

inline void render(void* node, Render original, Route kind) {
    const Route saved = route;
    route = kind;
    original(node);
    route = saved;
}

inline void __fastcall trilinear(void* node, void*) { render(node, trilinear_render, Route::trilinear); }
inline void __fastcall building(void* node, void*) { render(node, building_render, Route::building); }

inline bool hook(const unsigned char* image, size_t offset, size_t original_offset, ULONG_PTR replacement, Render& original) {
    auto* slot = reinterpret_cast<ULONG_PTR*>(const_cast<unsigned char*>(image + offset));
    const auto expected = reinterpret_cast<ULONG_PTR>(image + original_offset);
    if (*slot != expected) return false;
    DWORD protection = 0;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &protection)) return false;
    original = reinterpret_cast<Render>(expected);
    *slot = replacement;
    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(*slot), protection, &ignored);
    return true;
}

inline void install(const unsigned char* image) {
    const bool detailed = hook(image, 0x471c30, 0x16e70, reinterpret_cast<ULONG_PTR>(&trilinear), trilinear_render);
    const bool buildings = hook(image, 0x471c10, 0x16060, reinterpret_cast<ULONG_PTR>(&building), building_render);
    installed = detailed && buildings && world_filter::original_render;
}

inline bool filter_stage_zero(IDirect3DDevice9* device) {
    if (!installed || !extend_filter || !world_filter::selected || route == Route::none) return false;
    DWORD blend = 0, write_depth = 0;
    return SUCCEEDED(device->GetRenderState(D3DRS_ALPHABLENDENABLE, &blend)) && !blend &&
           SUCCEEDED(device->GetRenderState(D3DRS_ZWRITEENABLE, &write_depth)) && write_depth;
}

template<class Draw>
HRESULT apply(IDirect3DDevice9* device, Draw call) {
    if (!installed || hidden == Route::none || hidden != current()) return call();
    DWORD mask = 0;
    if (FAILED(device->GetRenderState(D3DRS_COLORWRITEENABLE, &mask)) || !mask ||
        FAILED(device->SetRenderState(D3DRS_COLORWRITEENABLE, 0))) return call();
    const HRESULT result = call();
    if (SUCCEEDED(device->SetRenderState(D3DRS_COLORWRITEENABLE, mask))) ++affected_draws;
    else hidden = Route::none;
    return result;
}

}

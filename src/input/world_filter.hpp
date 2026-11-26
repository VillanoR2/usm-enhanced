#pragma once

namespace world_filter {

using Render = void (__thiscall*)(void*);
inline Render original_render = nullptr;
inline bool available = false;
inline bool selected = false;
inline bool f6_down = false;
inline thread_local unsigned simple_depth = 0;
inline IDirect3DDevice9* caps_device = nullptr;
inline DWORD max_anisotropy = 0;
inline unsigned filtered_draws = 0;
inline unsigned extra_filtered_draws = 0;

inline void __fastcall render(void* node, void*) {
    ++simple_depth;
    original_render(node);
    --simple_depth;
}

inline void configure() {
    wchar_t text[2] = {};
    available = GetEnvironmentVariableW(L"USM_PC_CITY_FILTER", text, _countof(text)) == 1 && text[0] == L'1';
    selected = available;
}

inline void install(const unsigned char* image) {
    if (!available) return;
    auto* slot = reinterpret_cast<ULONG_PTR*>(const_cast<unsigned char*>(image + 0x471c28));
    const auto expected = reinterpret_cast<ULONG_PTR>(image + 0x169e0);
    if (*slot != expected) { available = selected = false; return; }
    DWORD protection = 0;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &protection)) {
        available = selected = false;
        return;
    }
    original_render = reinterpret_cast<Render>(expected);
    *slot = reinterpret_cast<ULONG_PTR>(&render);
    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(*slot), protection, &ignored);
}

inline void set_mode(bool active) {
    if (!available || selected == active) return;
    selected = active;
    FILE* file = nullptr;
    if (fopen_s(&file, "city-filter-status.txt", "a") == 0) {
        std::fprintf(file, "%llu %s\n", GetTickCount64(), selected ? "ANISOTROPIC" : "PC");
        std::fclose(file);
    }
}

inline void update_key(bool pressed, bool foreground) {
    if (pressed && !f6_down && foreground) set_mode(!selected);
    f6_down = pressed;
}

inline bool eligible(IDirect3DDevice9* device, DWORD& original_min, DWORD& original_mip, bool extra_route) {
    if (!selected || (!simple_depth && !extra_route)) return false;
    if (caps_device != device) {
        caps_device = device;
        max_anisotropy = 0;
        D3DCAPS9 caps = {};
        constexpr DWORD filters = D3DPTFILTERCAPS_MINFANISOTROPIC | D3DPTFILTERCAPS_MIPFLINEAR;
        if (SUCCEEDED(device->GetDeviceCaps(&caps)) && (caps.TextureFilterCaps & filters) == filters)
            max_anisotropy = caps.MaxAnisotropy;
    }
    if (max_anisotropy < 2) return false;
    DWORD levels = 0, anisotropy = 0;
    if (FAILED(device->GetSamplerState(0, D3DSAMP_MINFILTER, &original_min)) || original_min != D3DTEXF_LINEAR ||
        FAILED(device->GetSamplerState(0, D3DSAMP_MIPFILTER, &original_mip)) ||
        (original_mip != D3DTEXF_LINEAR && original_mip != D3DTEXF_POINT) ||
        FAILED(device->GetSamplerState(0, D3DSAMP_MAXANISOTROPY, &anisotropy)) || anisotropy < 2 ||
        anisotropy > max_anisotropy) return false;
    IDirect3DBaseTexture9* texture = nullptr;
    if (FAILED(device->GetTexture(0, &texture)) || !texture) return false;
    if (texture->GetType() == D3DRTYPE_TEXTURE) levels = texture->GetLevelCount();
    texture->Release();
    return levels > 1;
}

template<class Draw>
HRESULT apply(IDirect3DDevice9* device, Draw call, bool extra_route = false) {
    DWORD min = 0, mip = 0;
    if (!eligible(device, min, mip, extra_route) || FAILED(device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_ANISOTROPIC)))
        return call();
    const bool change_mip = mip != D3DTEXF_LINEAR;
    if (change_mip && FAILED(device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR))) {
        if (FAILED(device->SetSamplerState(0, D3DSAMP_MINFILTER, min))) set_mode(false);
        return call();
    }
    ++filtered_draws;
    if (extra_route) ++extra_filtered_draws;
    const HRESULT result = call();
    const bool mip_restored = !change_mip || SUCCEEDED(device->SetSamplerState(0, D3DSAMP_MIPFILTER, mip));
    if (FAILED(device->SetSamplerState(0, D3DSAMP_MINFILTER, min)) || !mip_restored) set_mode(false);
    return result;
}

}

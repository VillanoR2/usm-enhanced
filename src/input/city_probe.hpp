#pragma once

#include "lighting_capture.hpp"

namespace city_probe {

struct DrawState {
    DWORD route, shader, format, width, height, levels, min, mag, mip, anisotropy, lod_bias;
    DWORD z_enable, z_write, depth_func, depth_bias, slope_bias, alpha_blend, alpha_test;
};

struct CountedState {
    DrawState state;
    unsigned draws;
};

inline bool enabled = false;
inline ULONGLONG next_poll = 0;
inline unsigned sequence = 0;
inline unsigned remaining = 0;
inline unsigned sample_number = 0;
inline unsigned sampled = 0;
inline unsigned unmatched = 0;
inline CountedState states[384] = {};
inline unsigned state_count = 0;

inline void configure() {
    wchar_t text[2] = {};
    enabled = GetEnvironmentVariableW(L"USM_PC_CITY_PROBE", text, _countof(text)) == 1 && text[0] == L'1';
}

inline void count_draw(IDirect3DDevice9* device, const unsigned char* image) {
    if (!remaining || sample_number || sampled >= 6000) return;
    ++sampled;
    IDirect3DBaseTexture9* base = nullptr;
    if (FAILED(device->GetTexture(0, &base)) || !base) return;
    if (base->GetType() != D3DRTYPE_TEXTURE) { base->Release(); return; }
    auto* texture = static_cast<IDirect3DTexture9*>(base);
    D3DSURFACE_DESC desc = {};
    const HRESULT info = texture->GetLevelDesc(0, &desc);
    const DWORD levels = texture->GetLevelCount();
    texture->Release();
    if (FAILED(info)) return;

    DrawState state = {};
    state.route = static_cast<DWORD>(facade_probe::current());
    IDirect3DPixelShader9* shader = nullptr;
    state.shader = 0xffffffffu;
    if (SUCCEEDED(device->GetPixelShader(&shader)) && shader) {
        auto* slots = reinterpret_cast<IDirect3DPixelShader9* const*>(image + 0x5707c0);
        for (DWORD slot = 0; slot < 8; ++slot) if (slots[slot] == shader) { state.shader = slot; break; }
        shader->Release();
    }
    state.format = desc.Format;
    state.width = desc.Width;
    state.height = desc.Height;
    state.levels = levels;
    device->GetSamplerState(0, D3DSAMP_MINFILTER, &state.min);
    device->GetSamplerState(0, D3DSAMP_MAGFILTER, &state.mag);
    device->GetSamplerState(0, D3DSAMP_MIPFILTER, &state.mip);
    device->GetSamplerState(0, D3DSAMP_MAXANISOTROPY, &state.anisotropy);
    device->GetSamplerState(0, D3DSAMP_MIPMAPLODBIAS, &state.lod_bias);
    device->GetRenderState(D3DRS_ZENABLE, &state.z_enable);
    device->GetRenderState(D3DRS_ZWRITEENABLE, &state.z_write);
    device->GetRenderState(D3DRS_ZFUNC, &state.depth_func);
    device->GetRenderState(D3DRS_DEPTHBIAS, &state.depth_bias);
    device->GetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, &state.slope_bias);
    device->GetRenderState(D3DRS_ALPHABLENDENABLE, &state.alpha_blend);
    device->GetRenderState(D3DRS_ALPHATESTENABLE, &state.alpha_test);
    for (unsigned index = 0; index < state_count; ++index) {
        if (std::memcmp(&states[index].state, &state, sizeof(state)) == 0) {
            ++states[index].draws;
            return;
        }
    }
    if (state_count == _countof(states)) { ++unmatched; return; }
    states[state_count++] = {state, 1};
}

inline void write_draws(IDirect3DDevice9* device) {
    wchar_t path[96] = {};
    swprintf_s(path, L"Screenshot\\city-%u-draws.csv", sequence);
    FILE* file = nullptr;
    if (_wfopen_s(&file, path, L"w") != 0) return;
    IDirect3DSurface9* depth = nullptr;
    D3DSURFACE_DESC desc = {};
    if (SUCCEEDED(device->GetDepthStencilSurface(&depth)) && depth) {
        depth->GetDesc(&desc);
        depth->Release();
    }
    std::fprintf(file, "# depth_format=%lu, multisample=%lu, sampled=%u, overflow=%u, filtered=%u, extra_filtered=%u, hidden=%u, masked=%u\n",
                 static_cast<DWORD>(desc.Format), static_cast<DWORD>(desc.MultiSampleType), sampled, unmatched,
                 world_filter::filtered_draws, world_filter::extra_filtered_draws,
                 static_cast<unsigned>(facade_probe::hidden), facade_probe::affected_draws);
    std::fputs("draws,route,shader,format,width,height,levels,min,mag,mip,aniso,lodbias,z,zwrite,zfunc,depthbias,slopebias,blend,alphatest\n", file);
    for (unsigned index = 0; index < state_count; ++index) {
        const auto& entry = states[index];
        const auto& s = entry.state;
        std::fprintf(file, "%u,%lu,%lu,%08lx,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%08lx,%lu,%lu,%lu,%08lx,%08lx,%lu,%lu\n",
                     entry.draws, s.route, s.shader, s.format, s.width, s.height, s.levels, s.min, s.mag, s.mip,
                     s.anisotropy, s.lod_bias, s.z_enable, s.z_write, s.depth_func, s.depth_bias,
                     s.slope_bias, s.alpha_blend, s.alpha_test);
    }
    std::fclose(file);
}

inline void read_request() {
    const auto now = GetTickCount64();
    if (now < next_poll || remaining) return;
    next_poll = now + 80;
    FILE* file = nullptr;
    if (fopen_s(&file, "city-probe-request.txt", "r") != 0) return;
    unsigned number = 0, count = 0, mode = 0;
    const int fields = fscanf_s(file, "%u %u %u", &number, &count, &mode);
    std::fclose(file);
    if (fields < 2 || number <= sequence || !count || count > 18 || mode > 4) return;
    if (mode > 1 && (!facade_probe::installed || count != 1)) return;
    if (fields == 3) world_filter::set_mode(mode == 1);
    if (mode > 1) facade_probe::hidden = static_cast<facade_probe::Route>(mode - 1);
    sequence = number;
    remaining = count;
    sample_number = sampled = unmatched = state_count = 0;
}

inline void before_present(IDirect3DDevice9* device) {
    if (remaining) {
        if (!sample_number) write_draws(device);
        wchar_t path[96] = {};
        swprintf_s(path, L"Screenshot\\city-%u-%02u.bmp", sequence, sample_number++);
        const HRESULT hr = lighting_capture::save_frame(device, path);
        if (FAILED(hr)) {
            FILE* file = nullptr;
            if (fopen_s(&file, "city-probe-errors.txt", "a") == 0) {
                std::fprintf(file, "%u %u %08lx\n", sequence, sample_number - 1, static_cast<DWORD>(hr));
                std::fclose(file);
            }
        }
        if (!--remaining) facade_probe::hidden = facade_probe::Route::none;
    }
    read_request();
}

}

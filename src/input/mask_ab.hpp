#pragma once
#include "mask_generated.hpp"
#include "lighting_capture.hpp"
#include "same_draw.hpp"
#include "world_filter.hpp"
#include "facade_probe.hpp"
#include "city_probe.hpp"

namespace mask_ab {

using Draw = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT);
using DrawIndexed = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);

inline const unsigned char* image = nullptr;
inline IDirect3DDevice9* renderer = nullptr;
inline lighting_capture::Present original_present = nullptr;
inline Draw original_draw = nullptr;
inline DrawIndexed original_indexed = nullptr;
inline IDirect3DTexture9* replacement[2] = {};
inline IDirect3DTexture9* replacement_base[2] = {};
inline IDirect3DTexture9* suit_mask_base[2] = {};
inline int target_first = -1;
inline int active_material = -1;
inline IDirect3DBaseTexture9* active_mask = nullptr;
inline unsigned matches = 0;
inline unsigned swaps = 0;
inline unsigned materials = 0;
inline unsigned sample = 0;
inline bool failed = false;
inline bool same_frame = false;
inline bool gs_filter_sample = false;
inline bool gs_filter_present = false;
inline bool manual = false;
inline bool manual_base = false;
inline bool manual_ps2 = false;
inline bool f8_down = false;
inline bool suit_experiment = false;
inline bool suit_ps2 = false;
inline bool f7_down = false;
inline unsigned manual_applied = 0;
inline HWND game_window = nullptr;
inline ULONGLONG set = 0;
inline ULONGLONG started = 0;
inline ULONGLONG presents = 0;
constexpr size_t delays[] = {460, 605, 750, 895, 1040, 1185, 5000, 30000};

template<class T>
T read(const void* bytes, size_t offset) {
    T value;
    std::memcpy(&value, static_cast<const unsigned char*>(bytes) + offset, sizeof(value));
    return value;
}

inline void note(const char* message, HRESULT result) {
    FILE* output = nullptr;
    if (fopen_s(&output, "mask-ab-status.txt", "a") == 0) {
        std::fprintf(output, "%s 0x%08lx\n", message, static_cast<unsigned long>(result));
        std::fclose(output);
    }
}

inline void update_title() {
    if (!game_window) return;
    if (suit_experiment) {
        wchar_t title[120] = {};
        swprintf_s(title, L"Ultimate Spider-Man [Traje: %ls | Venom: %ls%ls]",
                   suit_ps2 ? L"sombras PS2" : L"PC", manual_ps2 ? L"sombras PS2" : L"PC",
                   world_filter::available ? (world_filter::selected ? L" | Ciudad: filtro nuevo" : L" | Ciudad: PC") : L"");
        SetWindowTextW(game_window, title);
        return;
    }
    if (manual_base) {
        SetWindowTextW(game_window, manual_ps2 ? L"Ultimate Spider-Man [Sombras PS2 64x64]" :
                                               L"Ultimate Spider-Man [Sombras PC]");
    } else {
        SetWindowTextW(game_window, manual_ps2 ? L"Ultimate Spider-Man [Mascara PS2]" :
                                               L"Ultimate Spider-Man [Mascara PC]");
    }
}

inline void update_manual_mode(bool down, bool foreground) {
    if (down && !f8_down && foreground) {
        manual_ps2 = !manual_ps2;
        manual_applied &= suit_experiment ? 0x3u : 0u;
        update_title();
        note(manual_ps2 ? "Manual toggle: PS2" : "Manual toggle: PC", S_OK);
    }
    f8_down = down;
}

inline void update_suit_mode(bool down, bool foreground) {
    if (suit_experiment && down && !f7_down && foreground) {
        suit_ps2 = !suit_ps2;
        manual_applied &= ~0x3u;
        update_title();
        note(suit_ps2 ? "Spider-Man suit toggle: PS2" : "Spider-Man suit toggle: PC", S_OK);
    }
    f7_down = down;
}

inline bool uses_suit_switch(int index) { return suit_experiment && index >= 0 && index < 2; }

inline bool manual_enabled_for(int index) { return uses_suit_switch(index) ? suit_ps2 : manual_ps2; }

inline bool sample_due() {
    const unsigned group = same_frame ? sample : sample / 3;
    return group < _countof(delays) && started && GetTickCount64() - started >= delays[group];
}

inline IDirect3DTexture9* mask(IDirect3DDevice9* device, bool second, bool only_base = false,
                              bool alpha_only = false) {
    auto& cached = alpha_only ? suit_mask_base[second] : (only_base ? replacement_base[second] : replacement[second]);
    if (cached) return cached;
    const char* filename = second ? "mask-6064aa59.bgra" : "mask-0dc218aa.bgra";
    const unsigned level_count = only_base ? 1u : 7u;
    constexpr unsigned texel_count = 5461;
    DWORD texels[texel_count];
    FILE* file = nullptr;
    if (fopen_s(&file, filename, "rb") == 0) {
        const bool complete = std::fread(texels, sizeof(DWORD), texel_count, file) == texel_count &&
                              std::fgetc(file) == EOF && std::feof(file);
        std::fclose(file);
        if (!complete) return nullptr;
    } else {
        // No PS2 ISO: procedural stand-in. USM_PC_MASK_GENERATED=0 disables it.
        wchar_t value[2] = {};
        if (GetEnvironmentVariableW(L"USM_PC_MASK_GENERATED", value, 2) == 1 && value[0] == L'0') return nullptr;
        mask_generated::generate(second, reinterpret_cast<uint32_t*>(texels));
    }
    // The GS shadow blend multiplies the destination by alpha. D3D9 also
    // adds the mask RGB, so clear the additive term for this suit trial.
    if (alpha_only) {
        for (unsigned pixel = 0; pixel < 64 * 64; ++pixel) texels[pixel] &= 0xff000000u;
    }
    IDirect3DTexture9* texture = nullptr;
    const HRESULT create = device->CreateTexture(64, 64, level_count, 0, D3DFMT_A8R8G8B8,
                                                  D3DPOOL_MANAGED, &texture, nullptr);
    if (FAILED(create)) { note("CreateTexture", create); return nullptr; }
    unsigned offset = 0;
    for (unsigned level = 0, size = 64; level < level_count; ++level, size >>= 1) {
        D3DLOCKED_RECT rect = {};
        HRESULT status = texture->LockRect(level, &rect, nullptr, 0);
        if (SUCCEEDED(status)) {
            for (unsigned row = 0; row < size; ++row)
                std::memcpy(static_cast<BYTE*>(rect.pBits) + row * rect.Pitch,
                            texels + offset + row * size, size * sizeof(DWORD));
            status = texture->UnlockRect(level);
        }
        if (FAILED(status)) { note("LockTexture", status); texture->Release(); return nullptr; }
        offset += size * size;
    }
    cached = texture;
    return cached;
}

inline void record_sampler(IDirect3DDevice9* device, IDirect3DBaseTexture9* current) {
    static unsigned recorded = 0;
    const auto material_bit = 1u << active_material;
    if (recorded & material_bit || current->GetType() != D3DRTYPE_TEXTURE) return;
    DWORD mag = 0, min = 0, mip = 0, bias = 0, max_level = 0, u = 0, v = 0;
    if (FAILED(device->GetSamplerState(1, D3DSAMP_MAGFILTER, &mag)) ||
        FAILED(device->GetSamplerState(1, D3DSAMP_MINFILTER, &min)) ||
        FAILED(device->GetSamplerState(1, D3DSAMP_MIPFILTER, &mip)) ||
        FAILED(device->GetSamplerState(1, D3DSAMP_MIPMAPLODBIAS, &bias)) ||
        FAILED(device->GetSamplerState(1, D3DSAMP_MAXMIPLEVEL, &max_level)) ||
        FAILED(device->GetSamplerState(1, D3DSAMP_ADDRESSU, &u)) ||
        FAILED(device->GetSamplerState(1, D3DSAMP_ADDRESSV, &v))) return;
    auto* texture = static_cast<IDirect3DTexture9*>(current);
    D3DSURFACE_DESC top = {};
    if (FAILED(texture->GetLevelDesc(0, &top))) return;
    FILE* file = nullptr;
    if (fopen_s(&file, "gs-filter-sampler.csv", "a") != 0) return;
    std::fprintf(file, "%llu,%d,%u,%u,%u,%u,%lu,%lu,%lu,%lu,%.6f,%lu,%lu\n",
                 set, active_material, top.Width, top.Height, static_cast<unsigned>(top.Format),
                 texture->GetLevelCount(), mag, min, mip, max_level, read<float>(&bias, 0), u, v);
    std::fclose(file);
    recorded |= material_bit;
}

template<class Call>
HRESULT draw(IDirect3DDevice9* device, Call original) {
    if (city_probe::enabled && device == renderer) city_probe::count_draw(device, image);
    const bool due = manual ? manual_enabled_for(active_material) : sample_due();
    if (active_material < 0 || device != renderer || !active_mask || !due) return original();
    auto* shaders = reinterpret_cast<IDirect3DPixelShader9* const*>(image + 0x5707c0);
    IDirect3DPixelShader9* shader = nullptr;
    if (FAILED(device->GetPixelShader(&shader)) || !shader) return original();
    const bool matching_shader = shader == shaders[6] || shader == shaders[7];
    shader->Release();
    if (!matching_shader) return original();
    IDirect3DBaseTexture9* previous = nullptr;
    if (FAILED(device->GetTexture(1, &previous)) || !previous) return original();
    if (previous != active_mask) {
        previous->Release();
        return original();
    }
    if (!manual) {
        ++matches;
        materials |= 1u << active_material;
    }
    if (same_frame) {
        const bool second = active_material == 1;
        if (gs_filter_sample) record_sampler(device, previous);
        auto* base = gs_filter_sample ? mask(device, second, true) : nullptr;
        const bool replayed = (!gs_filter_sample || base) &&
            same_draw::replay(device, previous, mask(device, second), original, base);
        failed |= !replayed;
        swaps += replayed ? 1u : 0u;
        previous->Release();
        return original();
    }
    const bool swap = manual || sample % 3 == 1;
    bool bound = false;
    if (swap) {
        auto* next = mask(device, active_material == 1, manual_base || gs_filter_present,
                          uses_suit_switch(active_material));
        bound = next && SUCCEEDED(device->SetTexture(1, next));
    }
    const HRESULT result = original();
    const bool restored = !bound || SUCCEEDED(device->SetTexture(1, previous));
    previous->Release();
    if (manual) {
        if (!bound || !restored) {
            if (uses_suit_switch(active_material)) suit_ps2 = false;
            else manual_ps2 = false;
            update_title();
            note("Manual texture switch failed", E_FAIL);
        } else if (!(manual_applied & (1u << active_material)) && SUCCEEDED(result)) {
            manual_applied |= 1u << active_material;
            const char* applied = uses_suit_switch(active_material)
                ? (active_material == 0 ? "Spider-Man suit material 7a6b6091: PS2 alpha-only level-zero mask"
                                        : "Spider-Man suit material 5ec62c24: PS2 alpha-only level-zero mask")
                : (manual_base ? "PS2 level-zero mask applied to a live draw" : "PS2 mask applied to a live draw");
            note(applied, S_OK);
        }
    } else {
        swaps += bound ? 1u : 0u;
        failed |= (swap && !bound) || !restored || FAILED(result);
    }
    return result;
}

inline HRESULT STDMETHODCALLTYPE primitive(IDirect3DDevice9* device, D3DPRIMITIVETYPE kind, UINT start, UINT count) {
    return draw(device, [&] {
        return world_filter::apply(device, [&] {
            return facade_probe::apply(device, [&] { return original_draw(device, kind, start, count); });
        }, facade_probe::filter_stage_zero(device));
    });
}

inline HRESULT STDMETHODCALLTYPE indexed(IDirect3DDevice9* device, D3DPRIMITIVETYPE kind, INT base, UINT min,
                                          UINT vertices, UINT start, UINT count) {
    return draw(device, [&] {
        return world_filter::apply(device, [&] {
            return facade_probe::apply(device, [&] { return original_indexed(device, kind, base, min, vertices, start, count); });
        }, facade_probe::filter_stage_zero(device));
    });
}

inline HRESULT STDMETHODCALLTYPE present(IDirect3DDevice9* device, const RECT* src, const RECT* dst,
                                          HWND window, const RGNDATA* dirty) {
    const bool previous_city_filter = world_filter::selected;
    if (city_probe::enabled && device == renderer) city_probe::before_present(device);
    world_filter::filtered_draws = 0;
    world_filter::extra_filtered_draws = 0;
    facade_probe::affected_draws = 0;
    if (manual) {
        if (device == renderer && game_window) {
            const bool foreground = GetForegroundWindow() == game_window;
            world_filter::update_key((GetAsyncKeyState(VK_F6) & 0x8000) != 0, foreground);
            if (previous_city_filter != world_filter::selected) update_title();
            if (suit_experiment) update_suit_mode((GetAsyncKeyState(VK_F7) & 0x8000) != 0, foreground);
            update_manual_mode((GetAsyncKeyState(VK_F8) & 0x8000) != 0, foreground);
        }
        return original_present(device, src, dst, window, dirty);
    }
    ++presents;
    if (device == renderer && matches && same_frame) {
        HRESULT captures[] = {E_FAIL, E_FAIL, E_FAIL, E_FAIL};
        constexpr const wchar_t* paired_tags[] = {L"a", L"b", L"scene"};
        constexpr const wchar_t* filter_tags[] = {L"pc", L"ps2-mips", L"ps2-base", L"scene"};
        const auto tags = gs_filter_sample ? filter_tags : paired_tags;
        const unsigned count = gs_filter_sample ? _countof(filter_tags) : _countof(paired_tags);
        if (!failed && swaps == matches) {
            for (unsigned i = 0; i < count; ++i) {
                wchar_t name[96] = {};
                swprintf_s(name, L"Screenshot\\%ls-%llu-%02u-%ls.bmp", gs_filter_sample ? L"gs-filter" : L"mask-pair",
                           set, sample, tags[i]);
                captures[i] = i + 1 < count ? lighting_capture::save_surface(device, same_draw::surfaces[i], name)
                                            : lighting_capture::save_frame(device, name);
            }
        }
        FILE* csv = nullptr;
        if (fopen_s(&csv, gs_filter_sample ? "gs-filter-frames.csv" : "mask-pair-frames.csv", "a") == 0) {
            std::fprintf(csv, "%llu,%u,%llu,%llu,%u,%u,%x",
                         set, sample, presents, GetTickCount64() - started, matches, swaps, materials);
            for (unsigned i = 0; i < count; ++i)
                std::fprintf(csv, ",0x%08lx", static_cast<unsigned long>(captures[i]));
            std::fputc('\n', csv);
            std::fclose(csv);
        }
        same_draw::clear = true;
        ++sample;
    } else if (device == renderer && matches && sample < 3 * _countof(delays)) {
        wchar_t filename[96] = {};
        swprintf_s(filename, L"Screenshot\\%ls-%llu-%02u-%u.bmp",
                   gs_filter_present ? L"gs-screen" : L"mask-ab", set, sample / 3, sample % 3);
        const HRESULT capture = failed ? E_FAIL : lighting_capture::save_frame(device, filename);
        FILE* csv = nullptr;
        if (fopen_s(&csv, gs_filter_present ? "gs-screen-frames.csv" : "mask-ab-frames.csv", "a") == 0) {
            std::fprintf(csv, "%llu,%u,%u,%llu,%llu,%u,%u,%x,0x%08lx\n", set, sample / 3, sample % 3, presents,
                         GetTickCount64() - started, matches, swaps, materials, static_cast<unsigned long>(capture));
            std::fclose(csv);
        }
        ++sample;
    }
    matches = swaps = materials = 0;
    failed = false;
    return original_present(device, src, dst, window, dirty);
}

inline bool belongs_to_d3d(LONG entry) {
    MEMORY_BASIC_INFORMATION region = {};
    return VirtualQuery(reinterpret_cast<void*>(entry), &region, sizeof(region)) &&
           region.AllocationBase == GetModuleHandleW(L"d3d9.dll");
}

inline bool install(const unsigned char* exe) {
    auto* device = read<IDirect3DDevice9*>(exe, 0x571f94);
    if (!device) return false;
    auto* slots = *reinterpret_cast<LONG**>(device);
    if (renderer) return renderer == device;
    for (unsigned slot : {17u, 81u, 82u})
        if (!belongs_to_d3d(slots[slot])) return false;
    DWORD protection = 0;
    if (!VirtualProtect(slots + 17, (82 - 17 + 1) * sizeof(*slots), PAGE_READWRITE, &protection)) return false;
    original_present = reinterpret_cast<lighting_capture::Present>(slots[17]);
    original_draw = reinterpret_cast<Draw>(slots[81]);
    original_indexed = reinterpret_cast<DrawIndexed>(slots[82]);
    renderer = device;
    image = exe;
    set = GetTickCount64();
    InterlockedExchange(slots + 17, reinterpret_cast<LONG>(&present));
    InterlockedExchange(slots + 81, reinterpret_cast<LONG>(&primitive));
    InterlockedExchange(slots + 82, reinterpret_cast<LONG>(&indexed));
    DWORD unused = 0;
    VirtualProtect(slots + 17, (82 - 17 + 1) * sizeof(*slots), protection, &unused);
    if (manual) {
        D3DDEVICE_CREATION_PARAMETERS params = {};
        DWORD pid = 0;
        if (SUCCEEDED(device->GetCreationParameters(&params)) && params.hFocusWindow &&
            GetWindowThreadProcessId(params.hFocusWindow, &pid) && pid == GetCurrentProcessId()) {
            game_window = params.hFocusWindow;
            update_title();
        }
    }
    note("D3D9 hooks installed", S_OK);
    return true;
}

template<class Render>
void render(void* node, Render original, int index, const unsigned char* exe) {
    const bool limit_materials = !manual || manual_base;
    if ((limit_materials && !uses_suit_switch(index) && (index < target_first || index >= target_first + 2)) ||
        (!manual && sample >= 3 * _countof(delays)) ||
        !install(exe) || (manual && !manual_enabled_for(index))) {
        original(node);
        return;
    }
    if (!started) started = GetTickCount64();
    auto* texture = read<const unsigned char*>(node, 0x20);
    const auto prior = active_material;
    auto* prior_mask = active_mask;
    active_material = index;
    active_mask = sony_prompts::readable(texture, 0x40) ? read<IDirect3DBaseTexture9*>(texture, 0x3c) : nullptr;
    original(node);
    active_mask = prior_mask;
    active_material = prior;
}

inline bool enabled() {
    city_probe::configure();
    world_filter::configure();
    facade_probe::configure();
    wchar_t value[2] = {};
    const auto count = GetEnvironmentVariableW(L"USM_PC_MASK_AB", value, _countof(value));
    manual_base = count == 1 && value[0] == L'7';
    manual = manual_base || (count == 1 && value[0] == L'5');
    manual_ps2 = manual_base;
    wchar_t suit[2] = {};
    suit_experiment = manual_base && GetEnvironmentVariableW(L"USM_PC_SUIT_SHADOWS", suit, _countof(suit)) == 1 &&
                      suit[0] == L'1';
    suit_ps2 = suit_experiment;
    gs_filter_present = count == 1 && value[0] == L'8';
    gs_filter_sample = count == 1 && value[0] == L'6';
    same_frame = gs_filter_sample || (count == 1 && (value[0] == L'3' || value[0] == L'4'));
    target_first = (!manual_base && manual) || (count == 1 && (value[0] == L'1' || value[0] == L'4')) ? 0
                 : manual_base || gs_filter_sample || gs_filter_present ||
                   (count == 1 && (value[0] == L'2' || value[0] == L'3')) ? 2 : -1;
    return target_first >= 0;
}

}

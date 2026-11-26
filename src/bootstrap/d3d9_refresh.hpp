#pragma once
#include <d3d9.h>
#include <new>

namespace {
using ResetDevice = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
static ResetDevice original_reset;
static LONG* reset_slot;
static SRWLOCK reset_lock = SRWLOCK_INIT;
static bool windowed_session;
static bool borderless_session;  // windowed presentation covering the whole monitor

static void use_windowed_presentation(D3DPRESENT_PARAMETERS& presentation) {
    presentation.Windowed = TRUE;
    presentation.FullScreen_RefreshRateInHz = D3DPRESENT_RATE_DEFAULT;
    presentation.BackBufferFormat = D3DFMT_UNKNOWN;
}

static void fit_game_window(HWND window, UINT buffer_width, UINT buffer_height) {
    DWORD owner = 0;
    if (!window || !GetWindowThreadProcessId(window, &owner) || owner != GetCurrentProcessId()) return;
    if (GetAncestor(window, GA_ROOT) != window || !buffer_width || !buffer_height) return;

    MONITORINFO monitor = {};
    monitor.cbSize = sizeof(monitor);
    if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor)) return;
    const LONG work_width = monitor.rcWork.right - monitor.rcWork.left;
    const LONG work_height = monitor.rcWork.bottom - monitor.rcWork.top;
    if (work_width < 320 || work_height < 240) return;

    if (borderless_session) {
        // Borderless: the window covers the monitor; Direct3D scales the render resolution to it.
        const DWORD old_style = static_cast<DWORD>(GetWindowLongW(window, GWL_STYLE));
        const DWORD style = WS_POPUP | (old_style & (WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN));
        if (IsZoomed(window)) ShowWindow(window, SW_RESTORE);
        SetWindowLongW(window, GWL_STYLE, static_cast<LONG>(style));
        SetWindowPos(window, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top,
                     monitor.rcMonitor.right - monitor.rcMonitor.left, monitor.rcMonitor.bottom - monitor.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_NOACTIVATE);
        return;
    }

    // Preserve the game's render resolution. Direct3D fits it to this client area,
    // which is as large as the render resolution allows within the work area.
    const UINT max_width = static_cast<UINT>(work_width - 64);
    const UINT max_height = static_cast<UINT>(work_height - 80);
    UINT width = buffer_width < max_width ? buffer_width : max_width;
    UINT height = static_cast<UINT>(MulDiv(width, buffer_height, buffer_width));
    if (height > max_height) {
        height = max_height;
        width = static_cast<UINT>(MulDiv(height, buffer_width, buffer_height));
    }

    constexpr DWORD frame = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    const DWORD old_style = static_cast<DWORD>(GetWindowLongW(window, GWL_STYLE));
    const DWORD style = frame | (old_style & (WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN));
    const DWORD ex_style = static_cast<DWORD>(GetWindowLongW(window, GWL_EXSTYLE));
    RECT bounds = {0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    if (!AdjustWindowRectEx(&bounds, style, FALSE, ex_style)) return;
    const LONG outer_width = bounds.right - bounds.left;
    const LONG outer_height = bounds.bottom - bounds.top;

    if (IsZoomed(window)) ShowWindow(window, SW_RESTORE);
    SetWindowLongW(window, GWL_STYLE, static_cast<LONG>(style));
    SetWindowPos(window, HWND_NOTOPMOST,
                 monitor.rcWork.left + (work_width - outer_width) / 2,
                 monitor.rcWork.top + (work_height - outer_height) / 2,
                 outer_width, outer_height, SWP_FRAMECHANGED | SWP_NOACTIVATE);
}

static bool requests_fixed_fullscreen_rate(const D3DPRESENT_PARAMETERS* presentation) {
    return presentation && !presentation->Windowed && presentation->FullScreen_RefreshRateInHz == 60;
}

static HRESULT STDMETHODCALLTYPE reset_with_default_rate(
    IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* presentation) {
    if (windowed_session && presentation) {
        use_windowed_presentation(*presentation);
        return original_reset(device, presentation);
    }
    const bool adjust = requests_fixed_fullscreen_rate(presentation);
    if (adjust) presentation->FullScreen_RefreshRateInHz = D3DPRESENT_RATE_DEFAULT;
    const HRESULT result = original_reset(device, presentation);
    if (adjust && FAILED(result)) presentation->FullScreen_RefreshRateInHz = 60;
    return result;
}

// Anisotropic filtering: the game asks for linear minification, which blurs
// textures seen at an angle or mid distance ("clay" look).
using SetSamplerStateFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*, DWORD, D3DSAMPLERSTATETYPE, DWORD);
static SetSamplerStateFn original_set_sampler_state;
static DWORD anisotropy_level;

static HRESULT STDMETHODCALLTYPE set_sampler_state_anisotropic(
    IDirect3DDevice9* device, DWORD sampler, D3DSAMPLERSTATETYPE type, DWORD value) {
    if (anisotropy_level > 1 && type == D3DSAMP_MINFILTER && value == D3DTEXF_LINEAR) {
        original_set_sampler_state(device, sampler, D3DSAMP_MAXANISOTROPY, anisotropy_level);
        return original_set_sampler_state(device, sampler, type, D3DTEXF_ANISOTROPIC);
    }
    return original_set_sampler_state(device, sampler, type, value);
}

static void install_anisotropic_filtering(IDirect3DDevice9* device) {
    if (original_set_sampler_state || depth_fix::anisotropy_requested < 2) return;
    D3DCAPS9 caps;
    if (FAILED(device->GetDeviceCaps(&caps)) || !(caps.TextureFilterCaps & D3DPTFILTERCAPS_MINFANISOTROPIC)) return;
    anisotropy_level = depth_fix::anisotropy_requested < caps.MaxAnisotropy ? depth_fix::anisotropy_requested
                                                                            : caps.MaxAnisotropy;
    if (anisotropy_level < 2) return;
    constexpr unsigned set_sampler_state_index = 69;
    LONG* slot = *reinterpret_cast<LONG**>(device) + set_sampler_state_index;
    DWORD previous = 0;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &previous)) return;
    const LONG original = *slot;
    if (original) {
        original_set_sampler_state = reinterpret_cast<SetSamplerStateFn>(original);
        InterlockedExchange(slot, reinterpret_cast<LONG>(set_sampler_state_anisotropic));
    }
    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(*slot), previous, &ignored);
    char line[64];
    wsprintfA(line, "anisotropic filtering x%lu", static_cast<unsigned long>(anisotropy_level));
    depth_fix::log_line(line);
}

static void install_reset_compatibility(IDirect3DDevice9* device) {
    // IDirect3DDevice9::Reset is slot 16 including the three IUnknown methods.
    // This table is in the private process. Keep the real D3D object and its ABI.
    constexpr unsigned reset_index = 16;
    LONG* slot = *reinterpret_cast<LONG**>(device) + reset_index;
    AcquireSRWLockExclusive(&reset_lock);
    if (!reset_slot) {
        DWORD previous_protection = 0;
        if (VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &previous_protection)) {
            const LONG original = *slot;
            if (original) {
                original_reset = reinterpret_cast<ResetDevice>(original);
                if (InterlockedCompareExchange(slot, reinterpret_cast<LONG>(reset_with_default_rate), original) == original)
                    reset_slot = slot;
            }
            DWORD ignored = 0;
            VirtualProtect(slot, sizeof(*slot), previous_protection, &ignored);
        }
    }
    ReleaseSRWLockExclusive(&reset_lock);
}
}

// Direct3D still owns the adapter and creates the real device. This proxy
// lets the runtime select a refresh rate for the legacy 60 Hz fullscreen request.
class D3D9Refresh final : public IDirect3D9 {
    IDirect3D9* inner_;
    volatile LONG references_ = 1;

    static bool same_iid(const GUID& a, const GUID& b) {
        if (a.Data1 != b.Data1 || a.Data2 != b.Data2 || a.Data3 != b.Data3) return false;
        for (unsigned i = 0; i < 8; ++i) if (a.Data4[i] != b.Data4[i]) return false;
        return true;
    }

public:
    explicit D3D9Refresh(IDirect3D9* inner) : inner_(inner) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        const GUID unknown = {0,0,0,{0xc0,0,0,0,0,0,0,0x46}};
        if (!same_iid(iid, unknown) && !same_iid(iid, IID_IDirect3D9)) return E_NOINTERFACE;
        *out = static_cast<IDirect3D9*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&references_); }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = InterlockedDecrement(&references_);
        if (!remaining) {
            inner_->Release();
            HANDLE heap = GetProcessHeap();
            this->~D3D9Refresh();
            HeapFree(heap, 0, this);
        }
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE RegisterSoftwareDevice(void* initialize) override {
        return inner_->RegisterSoftwareDevice(initialize);
    }
    UINT STDMETHODCALLTYPE GetAdapterCount() override { return inner_->GetAdapterCount(); }
    HRESULT STDMETHODCALLTYPE GetAdapterIdentifier(UINT adapter, DWORD flags, D3DADAPTER_IDENTIFIER9* info) override {
        return inner_->GetAdapterIdentifier(adapter, flags, info);
    }
    UINT STDMETHODCALLTYPE GetAdapterModeCount(UINT adapter, D3DFORMAT format) override {
        return inner_->GetAdapterModeCount(adapter, format);
    }
    HRESULT STDMETHODCALLTYPE EnumAdapterModes(UINT adapter, D3DFORMAT format, UINT mode, D3DDISPLAYMODE* info) override {
        return inner_->EnumAdapterModes(adapter, format, mode, info);
    }
    HRESULT STDMETHODCALLTYPE GetAdapterDisplayMode(UINT adapter, D3DDISPLAYMODE* mode) override {
        return inner_->GetAdapterDisplayMode(adapter, mode);
    }
    HRESULT STDMETHODCALLTYPE CheckDeviceType(UINT adapter, D3DDEVTYPE type, D3DFORMAT display, D3DFORMAT buffer, BOOL windowed) override {
        if (windowed_session)
            return inner_->CheckDeviceType(adapter, type, display, D3DFMT_UNKNOWN, TRUE);
        return inner_->CheckDeviceType(adapter, type, display, buffer, windowed);
    }
    HRESULT STDMETHODCALLTYPE CheckDeviceFormat(UINT adapter, D3DDEVTYPE type, D3DFORMAT display, DWORD usage, D3DRESOURCETYPE resource, D3DFORMAT format) override {
        return inner_->CheckDeviceFormat(adapter, type, display, usage, resource, format);
    }
    HRESULT STDMETHODCALLTYPE CheckDeviceMultiSampleType(UINT adapter, D3DDEVTYPE type, D3DFORMAT format, BOOL windowed, D3DMULTISAMPLE_TYPE multisample, DWORD* quality) override {
        return inner_->CheckDeviceMultiSampleType(adapter, type, format, windowed, multisample, quality);
    }
    HRESULT STDMETHODCALLTYPE CheckDepthStencilMatch(UINT adapter, D3DDEVTYPE type, D3DFORMAT display, D3DFORMAT target, D3DFORMAT depth) override {
        return inner_->CheckDepthStencilMatch(adapter, type, display, target, depth);
    }
    HRESULT STDMETHODCALLTYPE CheckDeviceFormatConversion(UINT adapter, D3DDEVTYPE type, D3DFORMAT source, D3DFORMAT target) override {
        return inner_->CheckDeviceFormatConversion(adapter, type, source, target);
    }
    HRESULT STDMETHODCALLTYPE GetDeviceCaps(UINT adapter, D3DDEVTYPE type, D3DCAPS9* caps) override {
        return inner_->GetDeviceCaps(adapter, type, caps);
    }
    HMONITOR STDMETHODCALLTYPE GetAdapterMonitor(UINT adapter) override { return inner_->GetAdapterMonitor(adapter); }

    HRESULT STDMETHODCALLTYPE CreateDevice(UINT adapter, D3DDEVTYPE type, HWND focus, DWORD flags,
                                           D3DPRESENT_PARAMETERS* presentation, IDirect3DDevice9** device) override {
        if (windowed_session && presentation) {
            HWND target = presentation->hDeviceWindow ? presentation->hDeviceWindow : focus;
            fit_game_window(target, presentation->BackBufferWidth, presentation->BackBufferHeight);
            use_windowed_presentation(*presentation);
        }
        const bool adjust = requests_fixed_fullscreen_rate(presentation);
        if (adjust) presentation->FullScreen_RefreshRateInHz = D3DPRESENT_RATE_DEFAULT;
        // 16-bit depth makes distant geometry flicker; ask for 24-bit first.
        const D3DFORMAT requested_depth = presentation ? presentation->AutoDepthStencilFormat : D3DFMT_UNKNOWN;
        const bool upgrade_depth = presentation && presentation->EnableAutoDepthStencil &&
            (requested_depth == D3DFMT_D16 || requested_depth == D3DFMT_D15S1 || requested_depth == D3DFMT_D16_LOCKABLE);
        if (upgrade_depth) presentation->AutoDepthStencilFormat = D3DFMT_D24S8;
        HRESULT hr = inner_->CreateDevice(adapter, type, focus, flags, presentation, device);
        if (upgrade_depth && FAILED(hr)) {
            presentation->AutoDepthStencilFormat = requested_depth;
            hr = inner_->CreateDevice(adapter, type, focus, flags, presentation, device);
        }
        if (presentation) {
            char line[160];
            wsprintfA(line, "CreateDevice hr=%08lx depth_requested=%d depth_used=%d auto=%d %ux%u windowed=%d",
                      static_cast<unsigned long>(hr), static_cast<int>(requested_depth),
                      static_cast<int>(presentation->AutoDepthStencilFormat),
                      static_cast<int>(presentation->EnableAutoDepthStencil), presentation->BackBufferWidth,
                      presentation->BackBufferHeight, static_cast<int>(presentation->Windowed));
            depth_fix::log_line(line);
        }
        if (adjust && FAILED(hr)) presentation->FullScreen_RefreshRateInHz = 60;
        if (SUCCEEDED(hr) && device && *device) {
            install_reset_compatibility(*device);
            install_anisotropic_filtering(*device);
        }
        return hr;
    }
};

static decltype(&Direct3DCreate9) original_direct3d_create9;

static IDirect3D9* WINAPI compatibility_direct3d_create9(UINT sdk_version) {
    wchar_t configured[2] = {};
    windowed_session = GetEnvironmentVariableW(L"USM_PC_WINDOWED", configured, 2) == 1 && configured[0] == L'1';
    borderless_session = windowed_session &&
        GetEnvironmentVariableW(L"USM_PC_BORDERLESS", configured, 2) == 1 && configured[0] == L'1';
    IDirect3D9* inner = original_direct3d_create9(sdk_version);
    if (!inner) return nullptr;
    void* storage = HeapAlloc(GetProcessHeap(), 0, sizeof(D3D9Refresh));
    if (!storage) return inner;
    return new (storage) D3D9Refresh(inner);
}

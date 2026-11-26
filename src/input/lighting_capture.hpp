#pragma once

namespace lighting_capture {

using Present = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
inline Present original_present = nullptr;
inline bool saw_venom = false;
inline ULONGLONG next_capture = 0;
inline unsigned attempts = 0;
inline ULONGLONG capture_set = 0;
constexpr unsigned limit = 8;

inline HRESULT save_surface(IDirect3DDevice9* device, IDirect3DSurface9* back_buffer,
                            const wchar_t* filename) {
    D3DSURFACE_DESC desc = {};
    HRESULT result = back_buffer->GetDesc(&desc);
    if (SUCCEEDED(result) && (desc.Width == 0 || desc.Height == 0 || desc.Width > 8192 || desc.Height > 8192 ||
        (desc.Format != D3DFMT_X8R8G8B8 && desc.Format != D3DFMT_A8R8G8B8) ||
        desc.MultiSampleType != D3DMULTISAMPLE_NONE)) result = D3DERR_INVALIDCALL;
    IDirect3DSurface9* staging = nullptr;
    if (SUCCEEDED(result))
        result = device->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &staging, nullptr);
    if (SUCCEEDED(result)) result = device->GetRenderTargetData(back_buffer, staging);
    if (FAILED(result)) {
        if (staging) staging->Release();
        return result;
    }

    D3DLOCKED_RECT pixels = {};
    result = staging->LockRect(&pixels, nullptr, D3DLOCK_READONLY);
    if (FAILED(result)) { staging->Release(); return result; }
    const DWORD row_bytes = desc.Width * 4;
    BITMAPFILEHEADER file_header = {};
    BITMAPINFOHEADER info = {};
    file_header.bfType = 0x4d42;
    file_header.bfOffBits = sizeof(file_header) + sizeof(info);
    file_header.bfSize = file_header.bfOffBits + row_bytes * desc.Height;
    info.biSize = sizeof(info);
    info.biWidth = static_cast<LONG>(desc.Width);
    info.biHeight = -static_cast<LONG>(desc.Height);
    info.biPlanes = 1;
    info.biBitCount = 32;
    info.biCompression = BI_RGB;
    HANDLE file = CreateFileW(filename, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        result = HRESULT_FROM_WIN32(GetLastError());
    } else {
        const auto write = [&](const void* buffer, DWORD bytes) {
            DWORD written = 0;
            return WriteFile(file, buffer, bytes, &written, nullptr) && written == bytes;
        };
        bool complete = write(&file_header, sizeof(file_header)) && write(&info, sizeof(info));
        for (unsigned row = 0; complete && row < desc.Height; ++row)
            complete = write(static_cast<const BYTE*>(pixels.pBits) + row * pixels.Pitch, row_bytes);
        CloseHandle(file);
        if (!complete) {
            DeleteFileW(filename);
            result = HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
        }
    }
    staging->UnlockRect();
    staging->Release();
    return result;
}

inline HRESULT save_frame(IDirect3DDevice9* device, const wchar_t* filename) {
    IDirect3DSurface9* back_buffer = nullptr;
    HRESULT result = device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back_buffer);
    if (FAILED(result)) return result;
    result = save_surface(device, back_buffer, filename);
    back_buffer->Release();
    return result;
}

inline HRESULT STDMETHODCALLTYPE present(IDirect3DDevice9* device, const RECT* src, const RECT* dst,
                                         HWND window, const RGNDATA* dirty) {
    const bool eligible = saw_venom;
    saw_venom = false;
    const ULONGLONG now = GetTickCount64();
    if (eligible && attempts < limit && now >= next_capture) {
        wchar_t filename[64] = {};
        swprintf_s(filename, L"Screenshot\\pc-color-%llu-%02u.bmp", capture_set, ++attempts);
        save_frame(device, filename);
        next_capture = now + 650;
    }
    return original_present(device, src, dst, window, dirty);
}

inline void mark_venom(const unsigned char* image) {
    if (attempts >= limit) return;
    saw_venom = true;
    if (original_present) return;
    IDirect3DDevice9* device = nullptr;
    memcpy(&device, image + 0x571f94, sizeof(device));
    if (!device) return;
    auto* slot = *reinterpret_cast<LONG**>(device) + 17;
    MEMORY_BASIC_INFORMATION region = {};
    if (!VirtualQuery(reinterpret_cast<void*>(*slot), &region, sizeof(region)) ||
        region.AllocationBase != GetModuleHandleW(L"d3d9.dll")) return;
    DWORD previous = 0;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &previous)) return;
    original_present = reinterpret_cast<Present>(*slot);
    InterlockedExchange(slot, reinterpret_cast<LONG>(&present));
    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(*slot), previous, &ignored);
    capture_set = GetTickCount64();
    next_capture = capture_set + 700;
}

}

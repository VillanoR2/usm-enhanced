#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#include "draw_distance.hpp"
#include "depth_fix.hpp"
#include "population.hpp"
#include "streaming.hpp"
#include "stability.hpp"
#include "d3d9_refresh.hpp"

extern "C" int _fltused = 0;  // no CRT: floating point is used by draw_distance

// No CRT: the optimizer turns small copy/fill loops into these calls.
#pragma function(memset, memcpy)
extern "C" void* __cdecl memset(void* destination, int value, size_t size) {
    volatile unsigned char* p = static_cast<unsigned char*>(destination);
    while (size--) *p++ = static_cast<unsigned char>(value);
    return destination;
}
extern "C" void* __cdecl memcpy(void* destination, const void* source, size_t size) {
    volatile unsigned char* d = static_cast<unsigned char*>(destination);
    const volatile unsigned char* s = static_cast<const unsigned char*>(source);
    while (size--) *d++ = *s++;
    return destination;
}

// Process lifetime: statically imported bootstrap. No CRT, logging, threads,
// LoadLibrary, registry access or path construction in DllMain.
static HMODULE bootstrap_module;

static bool equal(const char* left, const char* right) {
    while (*left && *left == *right) { ++left; ++right; }
    return *left == *right;
}

static decltype(&RegQueryValueExA) original_query_value;
static ULONG_PTR game_image_base;

// usm_mod.ini [Graficos] Resolucion=1920x1080 (or "auto"): render resolution.
static char chosen_resolution[24];

static void read_resolution(HMODULE self) {
    wchar_t text[24] = {};
    if (GetEnvironmentVariableW(L"USM_PC_RESOLUTION", text, 24) - 1u >= 23u) {
        wchar_t path[MAX_PATH];
        DWORD n = GetModuleFileNameW(self, path, MAX_PATH);
        if (!n || n >= MAX_PATH) return;
        while (n && path[n - 1] != L'\\') --n;
        const wchar_t name[] = L"usm_mod.ini";
        if (n + sizeof(name) / sizeof(wchar_t) >= MAX_PATH) return;
        for (unsigned i = 0; i < sizeof(name) / sizeof(wchar_t); ++i) path[n + i] = name[i];
        GetPrivateProfileStringW(L"Graficos", L"Resolucion", L"auto", text, 24, path);
    }
    // Accept "WIDTHxHEIGHT" with sane bounds; anything else keeps "auto".
    DWORD width = 0, height = 0, i = 0;
    while (text[i] >= L'0' && text[i] <= L'9' && width < 100000) width = width * 10 + (text[i++] - L'0');
    if (text[i] != L'x' && text[i] != L'X') return;
    ++i;
    while (text[i] >= L'0' && text[i] <= L'9' && height < 100000) height = height * 10 + (text[i++] - L'0');
    if (text[i] || width < 640 || width > 7680 || height < 360 || height > 4320) return;
    wsprintfA(chosen_resolution, "%lux%lu", static_cast<unsigned long>(width), static_cast<unsigned long>(height));
}

static LSTATUS WINAPI query_private_video_setting(HKEY key, LPCSTR name, LPDWORD reserved,
                                                 LPDWORD type, LPBYTE data, LPDWORD bytes) {
    const ULONG_PTR caller = reinterpret_cast<ULONG_PTR>(_ReturnAddress());
    // Only the two queries in the verified game settings reader qualify.
    const bool settings_reader = caller == game_image_base + 0x41ced0 ||
                                 caller == game_image_base + 0x41cf15;
    if (!settings_reader || !key || reserved || !name || !equal(name, "Resolution") || !bytes)
        return original_query_value(key, name, reserved, type, data, bytes);

    RECT area = {};
    const char* resolution = "1280x720";
    char native[24];
    if (chosen_resolution[0]) {
        resolution = chosen_resolution;
    } else if (borderless_session) {
        wsprintfA(native, "%dx%d", GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
        resolution = native;
    } else if (!windowed_session) {
        return original_query_value(key, name, reserved, type, data, bytes);  // full screen: game's own setting
    } else if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0)) {
        const LONG width = area.right - area.left;
        const LONG height = area.bottom - area.top;
        if (width < 1344 || height < 800) resolution = "1024x576";
        if (width < 1088 || height < 656) resolution = "800x450";
        if (width < 864 || height < 530) resolution = "640x360";
    }
    DWORD required = 1;
    while (resolution[required - 1]) ++required;
    const DWORD capacity = *bytes;
    *bytes = required;
    if (type) *type = REG_SZ;
    if (!data) return ERROR_SUCCESS;
    if (capacity < required) return ERROR_MORE_DATA;
    for (DWORD i = 0; i < required; ++i) data[i] = static_cast<BYTE>(resolution[i]);
    return ERROR_SUCCESS;
}

static bool append(char* path, DWORD length, const char* suffix) {
    while (*suffix) {
        if (length + 1 >= MAX_PATH) return false;
        path[length++] = *suffix++;
    }
    path[length] = 0;
    return true;
}

static HMODULE WINAPI compatibility_load_library(LPCSTR requested) {
    const DWORD previous_error = GetLastError();
    char system_path[MAX_PATH];
    const UINT length = GetSystemDirectoryA(system_path, MAX_PATH);
    const bool target = requested && length && length < MAX_PATH &&
        append(system_path, length, "\\dinput8.dll") && equal(requested, system_path);
    if (!target) {
        SetLastError(previous_error);
        return LoadLibraryA(requested);
    }
    char native_path[MAX_PATH];
    const DWORD native_length = GetModuleFileNameA(bootstrap_module, native_path, MAX_PATH);
    if (!native_length || native_length >= MAX_PATH) {
        SetLastError(ERROR_FILENAME_EXCED_RANGE);
        return nullptr;
    }
    DWORD directory_length = native_length;
    while (directory_length && native_path[directory_length - 1] != '\\') --directory_length;
    if (!directory_length || !append(native_path, directory_length, "usm_native_input.dll")) {
        SetLastError(ERROR_BAD_PATHNAME);
        return nullptr;
    }
    SetLastError(previous_error);
    return LoadLibraryA(native_path);
}

static bool inside(DWORD rva, DWORD bytes, DWORD image_size) {
    return rva <= image_size && bytes <= image_size - rva;
}

static bool image_name(const BYTE* image, DWORD size, DWORD rva, const char* expected) {
    for (DWORD index = 0; index < 256; ++index) {
        if (!inside(rva, index + 1, size) || image[rva + index] != expected[index]) return false;
        if (!expected[index]) return true;
    }
    return false;
}

static LONG* find_import(const char* upper_module, const char* lower_module, const char* symbol) {
    BYTE* const image = reinterpret_cast<BYTE*>(GetModuleHandleA(nullptr));
    if (!image) return nullptr;
    const IMAGE_DOS_HEADER* const dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < sizeof(IMAGE_DOS_HEADER) || dos->e_lfanew > 4096) return nullptr;
    const IMAGE_NT_HEADERS32* const nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(image + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        nt->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_IMPORT) return nullptr;
    const DWORD size = nt->OptionalHeader.SizeOfImage;
    const IMAGE_DATA_DIRECTORY imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!imports.VirtualAddress || !inside(imports.VirtualAddress, imports.Size, size)) return nullptr;
    LONG* match = nullptr;
    bool terminated = false;
    for (DWORD offset = 0; offset + sizeof(IMAGE_IMPORT_DESCRIPTOR) <= imports.Size; offset += sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
        const IMAGE_IMPORT_DESCRIPTOR* const entry = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(image + imports.VirtualAddress + offset);
        if (!entry->Name && !entry->FirstThunk && !entry->OriginalFirstThunk) { terminated = true; break; }
        if (!image_name(image, size, entry->Name, upper_module) && !image_name(image, size, entry->Name, lower_module)) continue;
        if (!entry->OriginalFirstThunk || !entry->FirstThunk) return nullptr;
        for (DWORD index = 0; ; ++index) {
            if (index >= size / sizeof(DWORD)) return nullptr;
            const DWORD offset32 = index * sizeof(DWORD);
            if (entry->OriginalFirstThunk > size || offset32 > size - entry->OriginalFirstThunk ||
                entry->FirstThunk > size || offset32 > size - entry->FirstThunk) return nullptr;
            const DWORD original_rva = entry->OriginalFirstThunk + offset32;
            const DWORD actual_rva = entry->FirstThunk + offset32;
            if (!inside(original_rva, sizeof(DWORD), size) || !inside(actual_rva, sizeof(DWORD), size)) return nullptr;
            const DWORD value = *reinterpret_cast<const DWORD*>(image + original_rva);
            if (!value) break;
            if (!(value & IMAGE_ORDINAL_FLAG32) && value <= size - sizeof(WORD) &&
                image_name(image, size, value + sizeof(WORD), symbol)) {
                if (match) return nullptr;
                match = reinterpret_cast<LONG*>(image + actual_rva);
            }
        }
    }
    return terminated ? match : nullptr;
}

static bool replace_import(LONG* match, LONG original, LONG replacement) {
    if (!match || *match != original) return false;
    DWORD old_protection = 0;
    if (!VirtualProtect(match, sizeof(*match), PAGE_READWRITE, &old_protection)) return false;
    const LONG previous = InterlockedCompareExchange(match, replacement, original);
    DWORD ignored = 0;
    const BOOL restored = VirtualProtect(match, sizeof(*match), old_protection, &ignored);
    return previous == original && restored;
}

static bool install() {
    LONG* input = find_import("KERNEL32.dll", "kernel32.dll", "LoadLibraryA");
    if (!replace_import(input, reinterpret_cast<LONG>(LoadLibraryA),
                        reinterpret_cast<LONG>(compatibility_load_library))) return false;
    LONG* graphics = find_import("D3D9.dll", "d3d9.dll", "Direct3DCreate9");
    if (!graphics) return true;
    const LONG original = *graphics;
    if (!original) return false;
    // Preserve any forwarding already installed by the Windows loader.
    original_direct3d_create9 = reinterpret_cast<decltype(original_direct3d_create9)>(original);
    if (!replace_import(graphics, original, reinterpret_cast<LONG>(compatibility_direct3d_create9))) return false;

    wchar_t configured[2] = {};
    const bool windowed = GetEnvironmentVariableW(L"USM_PC_WINDOWED", configured, 2) == 1 && configured[0] == L'1';
    if (!windowed && !chosen_resolution[0]) return true;
    windowed_session = windowed;  // d3d9_refresh reads it again when Direct3D is created
    borderless_session = windowed &&
        GetEnvironmentVariableW(L"USM_PC_BORDERLESS", configured, 2) == 1 && configured[0] == L'1';
    LONG* settings = find_import("ADVAPI32.dll", "advapi32.dll", "RegQueryValueExA");
    if (!settings || !*settings) return false;
    const LONG actual = *settings;
    game_image_base = reinterpret_cast<ULONG_PTR>(GetModuleHandleW(nullptr));
    original_query_value = reinterpret_cast<decltype(original_query_value)>(actual);
    return replace_import(settings, actual, reinterpret_cast<LONG>(query_private_video_setting));
}

// Packaged build: the approved modifications are active without a launcher.
// A value already present in the environment (test launchers) is kept.
static void default_setting(const wchar_t* name, const wchar_t* value) {
    wchar_t current[2] = {};
    if (!GetEnvironmentVariableW(name, current, 2) && GetLastError() == ERROR_ENVVAR_NOT_FOUND)
        SetEnvironmentVariableW(name, value);
}

static void package_defaults() {
    default_setting(L"USM_PC_WINDOWED", L"1");
    default_setting(L"USM_PC_SONY_PROMPTS", L"1");
    default_setting(L"USM_PC_MASK_AB", L"7");
    default_setting(L"USM_PC_SUIT_SHADOWS", L"1");
    default_setting(L"USM_PC_CITY_FILTER", L"1");
    default_setting(L"USM_PC_CITY_FILTER_EXTENDED", L"1");
}

extern "C" BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        bootstrap_module = module;
        {
            // usm_mod.ini [Graficos] ModoPantalla: 0 ventana, 1 ventana sin bordes, 2 pantalla completa.
            const float mode = draw_distance::setting(module, L"ModoPantalla", L"USM_PC_SCREEN_MODE", L"0");
            if (mode >= 1.5f) default_setting(L"USM_PC_WINDOWED", L"0");
            else if (mode >= 0.5f) default_setting(L"USM_PC_BORDERLESS", L"1");
            read_resolution(module);
        }
        package_defaults();
        draw_distance::install(module);
        streaming::grow_pools();
        stability::install();
        depth_fix::log_line(draw_distance::fade_installed ? "draw distance: proportional fade installed"
                                                          : "draw distance: stock fade kept");
        if (draw_distance::setting(module, L"AmpliarCarga", L"USM_PC_STREAMING", L"1") >= 1.0f)
            streaming::install(draw_distance::applied_scale);
        depth_fix::install(draw_distance::setting(module, L"PlanoCercano", L"USM_PC_NEAR_PLANE", L"0.3"));
        population::install(
            draw_distance::setting(module, L"RadioAparicionMinimo", L"USM_PC_SPAWN_MIN", L"60"),
            draw_distance::setting(module, L"RadioAparicionMaximo", L"USM_PC_SPAWN_MAX", L"150"));
        population::install_counts(
            draw_distance::setting(module, L"CantidadPeatones", L"USM_PC_PEDESTRIANS", L"24"),
            draw_distance::setting(module, L"CantidadTrafico", L"USM_PC_TRAFFIC", L"45"));
        {
            const float anisotropy = draw_distance::setting(module, L"FiltradoAnisotropico", L"USM_PC_ANISOTROPY", L"16");
            DWORD level = 0;  // no CRT: avoid float-to-int conversion helpers
            while (level < 16 && static_cast<float>(level + 1) <= anisotropy) ++level;
            depth_fix::anisotropy_requested = level;
        }
        return install() ? TRUE : FALSE;
    }
    return TRUE;
}

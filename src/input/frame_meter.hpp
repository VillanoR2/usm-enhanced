#pragma once
// Frame-rate meter: counted in the mask module's Present hook (one call per
// frame); every 5 s the average FPS and the slowest frame are appended to
// %TEMP%\usm_mod.log, the same log the bootstrap writes.
#include <windows.h>
#include <cstdio>

namespace frame_meter {

inline LARGE_INTEGER frequency, window_start, last;
inline unsigned frames = 0;
inline LONGLONG worst = 0;

inline void write_line(const char* text) {
    wchar_t path[MAX_PATH];
    const DWORD n = GetTempPathW(MAX_PATH, path);
    if (!n || n + 12 >= MAX_PATH) return;
    wcscat_s(path, L"usm_mod.log");
    HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(file, text, static_cast<DWORD>(strlen(text)), &written, nullptr);
    CloseHandle(file);
}

inline void tick() {
    LARGE_INTEGER now;
    if (!frequency.QuadPart && !QueryPerformanceFrequency(&frequency)) return;
    if (!QueryPerformanceCounter(&now)) return;
    if (!window_start.QuadPart) { window_start = last = now; return; }
    const LONGLONG frame = now.QuadPart - last.QuadPart;
    last = now;
    ++frames;
    if (frame > worst) worst = frame;
    const double elapsed = double(now.QuadPart - window_start.QuadPart) / double(frequency.QuadPart);
    if (elapsed < 5.0) return;
    char line[96];
    sprintf_s(line, "fps: %.1f average, slowest frame %.0f ms\r\n", frames / elapsed,
              1000.0 * double(worst) / double(frequency.QuadPart));
    write_line(line);
    window_start = now;
    frames = 0;
    worst = 0;
}

}  // namespace frame_meter

#pragma once
// Frame rate. The engine builds its frame timer (0x582100: rate, minimum
// rate; it waits while less than 1/rate has passed and clamps a step to
// 1/minimum) at 60 / frameLock FPS, and the game asks for a lock of 2, so it
// runs at 30. Both construction sites (start-up 0x5AC399 and the re-creation
// after a frame-lock change, 0x76E7DF) get the configured rate instead, as
// UltimateSpiderMan.FusionFix (ThirteenAG, GPL-3.0) does with its FPSLimit
// (default 60 there).
//
// usm_mod.ini [Graficos] FPSMaximo=60   ; 30 = original, 30-60

namespace frame_rate {

using Create = void*(__thiscall*)(void* timer, float rate, float minimum_rate);
inline float configured = 60.0f;
inline bool installed = false;

inline void* __fastcall create(void* timer, void*, float, float) {
    return reinterpret_cast<Create>(0x582100)(timer, configured, configured);
}

inline bool calls_timer(ULONG_PTR call) {
    const BYTE* code = reinterpret_cast<const BYTE*>(call);
    if (code[0] != 0xE8) return false;
    DWORD relative = 0;
    for (int i = 0; i < 4; ++i) relative |= static_cast<DWORD>(code[1 + i]) << (8 * i);
    return call + 5 + relative == 0x582100;
}

inline void install(float fps) {
    if (!(fps > 30.5f)) return;               // 30 or invalid: stock timer
    configured = fps > 60.0f ? 60.0f : fps;   // above 60 needs render-only frames (not ported)
    static const BYTE constructor[] = {0x56, 0x8B, 0xF1, 0x56, 0xFF, 0x15};
    static const BYTE startup_rate[] = {0x68, 0x00, 0x00, 0xF0, 0x41, 0x68, 0x00, 0x00, 0xF0, 0x41};  // push 30.0 x2
    if (!draw_distance::same(0x582100, constructor, sizeof(constructor)) ||
        !draw_distance::same(0x5AC38D, startup_rate, sizeof(startup_rate)) ||
        !calls_timer(0x5AC399) || !calls_timer(0x76E7DF)) {
        depth_fix::log_line("frame rate: unexpected code, stock 30 FPS kept");
        return;
    }
    static const ULONG_PTR calls[] = {0x5AC399, 0x76E7DF};
    for (ULONG_PTR call : calls) {
        const DWORD relative = reinterpret_cast<ULONG_PTR>(&create) - (call + 5);
        draw_distance::write(call + 1, &relative, 4);
    }
    installed = true;
}

}  // namespace frame_rate

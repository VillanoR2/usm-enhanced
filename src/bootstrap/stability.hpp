#pragma once
// Crash guards for situations the stock game never reached at its original
// draw distance and crowd size.
//
// 0x4D6050 refreshes a bone-attached node's world matrix from the owning
// skeleton's pose array ([[node+0x34]+0xE0], bone [node+0x42]-1). The caller
// (0x4DB677) takes this path when the bone index is past the pose count, and a
// skeleton whose pose was never built (far away / not animated) has count 0 and
// a null array: AV at 0x4D607E (crash 2026-11-25 22:01, bone 82). Skip the
// refresh in that case; the node keeps its last matrix and stays dirty, so it
// updates once the pose exists.

namespace stability {

inline DWORD bone_continue = 0x4D6056;

__declspec(naked) inline void bone_guard() {
    __asm {
        mov eax, [ecx + 0x34]
        test eax, eax
        jz skip
        mov eax, [eax + 0xE0]
        test eax, eax
        jz skip
        sub esp, 8              // original 6 bytes
        push ebx
        mov ebx, ecx
        jmp dword ptr [bone_continue]
    skip:
        ret
    }
}

// Two fixed global lists are filled without a bound (crash 2026-11-25 22:23,
// AV at 0x52A1EA reading entry 32): with 24 pedestrians and 45 cars they
// overflow into their own counters and the memory after them.
//   characters: 0x52A130 copies every active character into 0x95CB50[32]
//               (count 0x95CBD0); read at 0x52A1E0 and 0x51E850.
//   flagged:    0x565BA0 adds characters passing 0x52BD30 to 0x95D420[15]
//               (count 0x95D45C); read at 0x565BB1, 0x52FD23, 0x54E600.
// Both move to larger arrays and the appends get a capacity check.
constexpr DWORD kCharacters = 512, kFlagged = 256;
inline DWORD characters[kCharacters];
inline DWORD flagged[kFlagged];
inline DWORD characters_done = 0x52A174, flagged_done = 0x565BE1;

__declspec(naked) inline void append_character() {   // ecx = character
    __asm {
        mov edx, dword ptr ds:[0x95CBD0]
        cmp edx, kCharacters
        jae full
        mov dword ptr [characters + edx * 4], ecx
        inc dword ptr ds:[0x95CBD0]
    full:
        jmp dword ptr [characters_done]
    }
}

__declspec(naked) inline void append_flagged() {     // esi = character
    __asm {
        mov eax, dword ptr ds:[0x95D45C]
        cmp eax, kFlagged
        jae full
        mov dword ptr [flagged + eax * 4], esi
        inc dword ptr ds:[0x95D45C]
    full:
        jmp dword ptr [flagged_done]
    }
}

inline void jump_to(ULONG_PTR from, const void* to, unsigned length) {
    BYTE code[24];
    code[0] = 0xE9;
    const DWORD relative = reinterpret_cast<ULONG_PTR>(to) - (from + 5);
    for (int i = 0; i < 4; ++i) code[1 + i] = static_cast<BYTE>(relative >> (8 * i));
    for (unsigned i = 5; i < length; ++i) code[i] = 0x90;
    draw_distance::write(from, code, length);
}

inline void install_lists() {
    static const BYTE characters_write[] = {0x8B, 0x15, 0xD0, 0xCB, 0x95, 0x00, 0x89, 0x0C, 0x95, 0x50, 0xCB,
                                            0x95, 0x00, 0xFF, 0x05, 0xD0, 0xCB, 0x95, 0x00};
    static const BYTE characters_read1[] = {0x8B, 0x1C, 0xAD, 0x50, 0xCB, 0x95, 0x00};
    static const BYTE characters_read2[] = {0x3B, 0x14, 0x85, 0x50, 0xCB, 0x95, 0x00};
    static const BYTE flagged_write[] = {0xA1, 0x5C, 0xD4, 0x95, 0x00, 0x89, 0x34, 0x85, 0x20, 0xD4, 0x95, 0x00,
                                         0xFF, 0x05, 0x5C, 0xD4, 0x95, 0x00};
    static const BYTE flagged_read1[] = {0x39, 0x34, 0x85, 0x20, 0xD4, 0x95, 0x00};
    static const BYTE flagged_read2[] = {0x39, 0x2C, 0x85, 0x20, 0xD4, 0x95, 0x00};
    static const BYTE flagged_read3[] = {0x39, 0x14, 0x85, 0x20, 0xD4, 0x95, 0x00};
    if (!draw_distance::same(0x52A161, characters_write, sizeof(characters_write)) ||
        !draw_distance::same(0x52A1E0, characters_read1, sizeof(characters_read1)) ||
        !draw_distance::same(0x51E850, characters_read2, sizeof(characters_read2)) ||
        !draw_distance::same(0x565BCF, flagged_write, sizeof(flagged_write)) ||
        !draw_distance::same(0x565BB1, flagged_read1, sizeof(flagged_read1)) ||
        !draw_distance::same(0x52FD23, flagged_read2, sizeof(flagged_read2)) ||
        !draw_distance::same(0x54E600, flagged_read3, sizeof(flagged_read3))) {
        depth_fix::log_line("stability: unexpected code, character lists not enlarged");
        return;
    }
    const DWORD characters_at = reinterpret_cast<ULONG_PTR>(characters);
    const DWORD flagged_at = reinterpret_cast<ULONG_PTR>(flagged);
    draw_distance::write(0x52A1E3, &characters_at, 4);
    draw_distance::write(0x51E853, &characters_at, 4);
    draw_distance::write(0x565BB4, &flagged_at, 4);
    draw_distance::write(0x52FD26, &flagged_at, 4);
    draw_distance::write(0x54E603, &flagged_at, 4);
    jump_to(0x52A161, reinterpret_cast<const void*>(&append_character), sizeof(characters_write));
    jump_to(0x565BCF, reinterpret_cast<const void*>(&append_flagged), sizeof(flagged_write));
    depth_fix::log_line("stability: character lists enlarged (32->512, 15->256) and bounded");
}

inline void install() {
    install_lists();
    static const BYTE prologue[] = {0x83, 0xEC, 0x08, 0x53, 0x8B, 0xD9, 0x8B, 0x4B, 0x20};
    if (!draw_distance::same(0x4D6050, prologue, sizeof(prologue))) {
        depth_fix::log_line("stability: unexpected code, bone guard not installed");
        return;
    }
    jump_to(0x4D6050, reinterpret_cast<const void*>(&bone_guard), 6);
    depth_fix::log_line("stability: bone matrix guard installed");
}

}  // namespace stability

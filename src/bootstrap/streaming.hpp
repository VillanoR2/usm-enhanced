#pragma once
// Extended city streaming for USM.exe 1.0 (no CRT).
// Port of ExtremeDrawDistance.ixx from ThirteenAG's UltimateSpiderMan.FusionFix
// (WidescreenFixesPack, GPL-3.0): more resident city regions (8 -> 32),
// doubled streaming partitions, larger terrain pack search radius and the
// fixed-size tables that depend on them. Every site is verified before any
// byte is written; if one differs nothing is applied.

namespace streaming {

// ------------------------------------------------------------------ helpers
inline int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// "8B 44 ? 04": '?' matches any byte.
inline bool matches(ULONG_PTR address, const char* pattern) {
    const BYTE* p = reinterpret_cast<const BYTE*>(address);
    unsigned i = 0;
    for (const char* s = pattern; *s;) {
        if (*s == ' ') { ++s; continue; }
        if (*s == '?') { ++s; ++i; continue; }
        const int hi = hex_digit(s[0]), lo = hex_digit(s[1]);
        if (hi < 0 || lo < 0 || p[i] != static_cast<BYTE>(hi * 16 + lo)) return false;
        s += 2; ++i;
    }
    return true;
}

inline void put32(ULONG_PTR address, DWORD value) { draw_distance::write(address, &value, 4); }
inline void put8(ULONG_PTR address, BYTE value) { draw_distance::write(address, &value, 1); }
inline DWORD get32(ULONG_PTR address) { return *reinterpret_cast<const DWORD*>(address); }

inline void put_jump(ULONG_PTR from, const void* to, unsigned length, BYTE opcode = 0xE9) {
    BYTE code[16];
    code[0] = opcode;
    const DWORD relative = reinterpret_cast<ULONG_PTR>(to) - (from + 5);
    for (int i = 0; i < 4; ++i) code[1 + i] = static_cast<BYTE>(relative >> (8 * i));
    for (unsigned i = 5; i < length; ++i) code[i] = 0x90;
    draw_distance::write(from, code, length < 5 ? 5 : length);
}

// Executable copies of overwritten prologues followed by a jump back.
inline BYTE* trampolines;
inline unsigned trampoline_used;
inline void* trampoline(ULONG_PTR function, unsigned length) {
    BYTE* t = trampolines + trampoline_used;
    const BYTE* source = reinterpret_cast<const BYTE*>(function);
    for (unsigned i = 0; i < length; ++i) t[i] = source[i];
    t[length] = 0xE9;
    const DWORD relative = (function + length) - reinterpret_cast<ULONG_PTR>(t + length + 5);
    for (int i = 0; i < 4; ++i) t[length + 1 + i] = static_cast<BYTE>(relative >> (8 * i));
    trampoline_used += (length + 5 + 15) & ~15u;
    return t;
}

// ------------------------------------------------------------------ state
constexpr unsigned kRegions = 32;           // resident city regions (stock 8)
constexpr unsigned kSlotFactor = kRegions / 8;  // streaming region slots (parts 5 and 6) multiplier
// Terrain pack search radius grows with the area the resident regions cover:
// 600 * sqrt(kRegions / 8). 16 regions were measured full at 900.
constexpr float kPackRadius = 1200.0f;
constexpr unsigned kLoaded = kRegions + 1;  // loaded region cache (stock 9)
constexpr ULONG_PTR kLoadedBase = 0x9222F8, kLoadedStockSize = 9 * 0x1C;

struct LoadedRegion { float minimum[3], maximum[3]; WORD allocation, padding; };
static_assert(sizeof(LoadedRegion) == 0x1C, "LoadedRegion layout");

inline void* region_allocators[kRegions];
inline ULONG_PTR district_tables[kRegions];
inline LoadedRegion loaded_regions[kLoaded];
inline bool capacity_increased;
inline float detail_distance = 167.0f;

// Continuation addresses used by the naked stubs.
inline DWORD c_lookup = 0x5E14A2, c_sort = 0x5DBCDA, c_frame_sort = 0x5E1AC1;
inline DWORD c_box = 0x52EB46, c_sphere = 0x565E16, c_neighbour = 0x53BA0E, c_neighbour_full = 0x53BA7B;
inline DWORD c_shadow = 0x53D907, c_entity_count = 0x4F5517, c_entity_cap = 0x4F5371, c_entity_full = 0x4F537F;

// ------------------------------------------------------------------ region lists
struct RadarRegionList { ULONG_PTR regions[30]; int count; };
using BuildRegionListFn = void(__cdecl*)(RadarRegionList*, ULONG_PTR, const float*, float, int);
inline BuildRegionListFn build_region_list_original;

inline void __cdecl build_region_list(RadarRegionList* list, ULONG_PTR region, const float* position, float radius, int loaded_only) {
    // The result array is also the visited set: once full, recursion never ends.
    if (!list || !region || list->count >= 30) return;
    build_region_list_original(list, region, position, radius, loaded_only);
}

inline void __cdecl build_radar_region_list(RadarRegionList* list, ULONG_PTR region, const float* position, float radius, int loaded_only) {
    build_region_list(list, region, position, radius, loaded_only);
    using Bounds = void(__thiscall*)(ULONG_PTR, float*, float*);
    const Bounds bounds = reinterpret_cast<Bounds>(0x52E770);
    ULONG_PTR keep[30];
    float distance[30];
    int count = 0;
    for (int i = 0; i < list->count && i < 30; ++i) {
        const ULONG_PTR current = list->regions[i];
        if (!current) continue;
        const DWORD flags = get32(current + 0x50);
        if (!(flags & 0x10) || (flags & (0x100 | 0x40000))) continue;
        float minimum[3], maximum[3];
        bounds(current, minimum, maximum);
        float d = 0.0f;
        for (int axis = 0; axis < 3; ++axis) {
            float delta = minimum[axis] - position[axis];
            const float other = position[axis] - maximum[axis];
            if (other > delta) delta = other;
            if (delta < 0.0f) delta = 0.0f;
            d += delta * delta;
        }
        // stable insertion: only reorders when more than 12 are resident
        keep[count] = current; distance[count] = d; ++count;
    }
    if (count > 12) {
        for (int i = 1; i < count; ++i) {
            const ULONG_PTR r = keep[i]; const float d = distance[i];
            int j = i - 1;
            while (j >= 0 && distance[j] > d) { keep[j + 1] = keep[j]; distance[j + 1] = distance[j]; --j; }
            keep[j + 1] = r; distance[j + 1] = d;
        }
    }
    list->count = count < 12 ? count : 12;  // renderer has twelve inline slots
    for (int i = 0; i < list->count; ++i) list->regions[i] = keep[i];
}

// ------------------------------------------------------------------ district mission tables
using CreateManagerFn = ULONG_PTR(__thiscall*)(ULONG_PTR);
inline CreateManagerFn create_manager_original;

inline ULONG_PTR __fastcall create_mission_manager(ULONG_PTR manager, void*) {
    const ULONG_PTR result = create_manager_original(manager);
    for (unsigned i = 0; i < kRegions; ++i) district_tables[i] = 0;
    *reinterpret_cast<ULONG_PTR**>(manager + 0x18) = district_tables;  // inline array -> pointer
    return result;
}

inline int __fastcall add_district_table(ULONG_PTR manager, void*, void* source, ULONG_PTR region) {
    ULONG_PTR* tables = *reinterpret_cast<ULONG_PTR**>(manager + 0x18);
    int& count = *reinterpret_cast<int*>(manager + 0x38);
    if (count >= static_cast<int>(kRegions)) return count;
    using Create = void(__cdecl*)(ULONG_PTR*, void*, int, int, int, int, int, int);
    reinterpret_cast<Create>(0x5E4020)(&tables[count], source, 0, 0, 0, 0, 0, 0);
    *reinterpret_cast<ULONG_PTR*>(tables[count] + 0x44) = region;
    return ++count;
}

inline int __fastcall remove_district_table(ULONG_PTR manager, void*, ULONG_PTR region) {
    ULONG_PTR* tables = *reinterpret_cast<ULONG_PTR**>(manager + 0x18);
    int& count = *reinterpret_cast<int*>(manager + 0x38);
    for (int i = 0; i < count; ++i)
        if (*reinterpret_cast<ULONG_PTR*>(tables[i] + 0x44) == region) { tables[i] = tables[count - 1]; return --count; }
    return count;
}

inline ULONG_PTR __fastcall get_district_table(ULONG_PTR manager, void*, int index) {
    return (*reinterpret_cast<ULONG_PTR**>(manager + 0x18))[index];
}

// mov eax,[ebp+eax*4+18h] / mov edx,[eax+44h]  ->  through the table pointer
__declspec(naked) inline void stub_mission_lookup() {
    __asm {
        mov edx, dword ptr [ebp + 0x18]
        mov eax, dword ptr [edx + eax * 4]
        mov edx, dword ptr [eax + 0x44]
        jmp dword ptr [c_lookup]
    }
}
// lea eax,[ecx+eax*4+18h] / add ecx,18h
__declspec(naked) inline void stub_mission_sort() {
    __asm {
        mov ecx, dword ptr [ecx + 0x18]
        lea eax, [ecx + eax * 4]
        jmp dword ptr [c_sort]
    }
}
// lea eax,[esi+eax*4+18h] / lea ecx,[esi+18h]
__declspec(naked) inline void stub_mission_frame_sort() {
    __asm {
        mov ecx, dword ptr [esi + 0x18]
        lea eax, [ecx + eax * 4]
        jmp dword ptr [c_frame_sort]
    }
}

// ------------------------------------------------------------------ loaded region cache
inline int __cdecl count_loaded_regions() {
    int count = 0;
    for (unsigned i = 0; i < kLoaded; ++i) if (loaded_regions[i].allocation != 0xFFFF) ++count;
    return count;
}

inline ULONG_PTR __cdecl get_loaded_region(int index) {
    if (index < 0) return 0;
    const ULONG_PTR pool = get32(0x95C924);
    for (unsigned i = 0; i < kLoaded; ++i)
        if (loaded_regions[i].allocation != 0xFFFF && index-- == 0) return pool + 0x134 * loaded_regions[i].allocation;
    return 0;
}

// Query result vectors hold 15 entries; neighbour scratch holds 32.
__declspec(naked) inline void stub_box_capacity() {      // 8B 4B 3C 89 04 8B FF 43 3C
    __asm {
        pushfd
        cmp dword ptr [ebx + 0x3C], 15
        jae full
        popfd
        mov ecx, dword ptr [ebx + 0x3C]
        mov dword ptr [ebx + ecx * 4], eax
        inc dword ptr [ebx + 0x3C]
        jmp dword ptr [c_box]
    full:
        popfd
        jmp dword ptr [c_box]
    }
}
__declspec(naked) inline void stub_sphere_capacity() {   // 8B 53 3C 89 0C 93 FF 43 3C
    __asm {
        pushfd
        cmp dword ptr [ebx + 0x3C], 15
        jae full
        popfd
        mov edx, dword ptr [ebx + 0x3C]
        mov dword ptr [ebx + edx * 4], ecx
        inc dword ptr [ebx + 0x3C]
        jmp dword ptr [c_sphere]
    full:
        popfd
        jmp dword ptr [c_sphere]
    }
}
__declspec(naked) inline void stub_neighbour_capacity() { // 89 44 94 34 FF 84 24 B4 00 00 00
    __asm {
        pushfd
        cmp edx, 32
        jae full
        popfd
        mov dword ptr [esp + edx * 4 + 0x34], eax
        inc dword ptr [esp + 0xB4]
        jmp dword ptr [c_neighbour]
    full:
        popfd
        jmp dword ptr [c_neighbour_full]
    }
}

// ------------------------------------------------------------------ stencil shadow regions
struct ShadowRegionList { ULONG_PTR vtable; int count; ULONG_PTR regions[kLoaded]; };
inline ShadowRegionList* shadow_regions;  // rendering is single threaded

inline int __fastcall visit_shadow_region(ShadowRegionList* list, void*, ULONG_PTR region) {
    if ((get32(region + 0x50) & 0x10) == 0) return 0;
    for (int i = 0; i < list->count; ++i) if (list->regions[i] == region) return 0;
    if (list->count < static_cast<int>(kLoaded)) list->regions[list->count++] = region;
    return 0;
}
inline ULONG_PTR shadow_vtable[1];

using RenderShadowsFn = void(__thiscall*)(ULONG_PTR, ULONG_PTR);
inline RenderShadowsFn render_shadows_original;
inline void __fastcall render_stencil_shadows(ULONG_PTR renderer, void*, ULONG_PTR camera) {
    ShadowRegionList regions;
    regions.vtable = reinterpret_cast<ULONG_PTR>(shadow_vtable);
    regions.count = 0;
    ShadowRegionList* previous = shadow_regions;
    shadow_regions = &regions;
    render_shadows_original(renderer, camera);
    shadow_regions = previous;
}

using CollectShadowsFn = int(__thiscall*)(ULONG_PTR, void*, void*);
inline CollectShadowsFn collect_shadows_original;
inline int __fastcall collect_shadow_regions(ULONG_PTR map, void*, void* polygon, ShadowRegionList* native_list) {
    if (!shadow_regions) return collect_shadows_original(map, polygon, native_list);
    const int result = collect_shadows_original(map, polygon, shadow_regions);
    native_list->count = shadow_regions->count;  // never write into its nine-entry array
    return result;
}

__declspec(naked) inline void stub_shadow_read() {       // 8B BC B4 84 00 00 00
    __asm {
        mov edi, dword ptr [shadow_regions]
        test edi, edi
        jz native
        mov edi, dword ptr [edi + esi * 4 + 8]
        jmp dword ptr [c_shadow]
    native:
        mov edi, dword ptr [esp + esi * 4 + 0x84]
        jmp dword ptr [c_shadow]
    }
}

// ------------------------------------------------------------------ entity region membership
__declspec(naked) inline void stub_entity_count() {      // 8B 44 24 08 83 F8 01
    __asm {
        cmp dword ptr [esp + 8], 9
        jle ok
        mov dword ptr [esp + 8], 9
    ok:
        mov eax, dword ptr [esp + 8]
        cmp eax, 1
        jmp dword ptr [c_entity_count]
    }
}
__declspec(naked) inline void stub_entity_capacity() {   // 8B 46 50 8B 50 1C
    __asm {
        pushfd
        push eax
        mov eax, dword ptr [esi + 0x50]
        test eax, eax
        jz full
        cmp dword ptr [eax + 0x1C], 7
        jae full
        pop eax
        popfd
        mov eax, dword ptr [esi + 0x50]
        mov edx, dword ptr [eax + 0x1C]
        jmp dword ptr [c_entity_cap]
    full:
        pop eax
        popfd
        jmp dword ptr [c_entity_full]
    }
}

// ------------------------------------------------------------------ streaming buffer
struct MemoryMap { DWORD header[4]; struct { int id, type, slot_size, slot_count; } part[8]; };
static_assert(sizeof(MemoryMap) == 0x90, "MemoryMap layout");

inline DWORD expanded_size(MemoryMap* maps, int count, DWORD original) {
    DWORD buffer = original;
    for (int i = 0; i < count; ++i) {
        MemoryMap& map = maps[i];
        if (map.part[6].type != 0 || map.part[6].slot_count <= 1) continue;  // mission layouts
        for (int index = 5; index <= 6; ++index) {
            auto& part = map.part[index];
            if (part.type != 0 || part.slot_count <= 0 || part.slot_count > 128 || part.slot_size <= 0 || part.slot_size % 4096)
                return 0;
            part.slot_count *= kSlotFactor;
            if (part.slot_count > static_cast<int>(kRegions)) return 0;
        }
        DWORD size = 0;
        for (const auto& part : map.part) {
            if (part.slot_size < 0 || part.slot_count < 0) return 0;
            const DWORD slot = static_cast<DWORD>(part.slot_size), n = static_cast<DWORD>(part.slot_count);
            if (n && slot > 0xFFFFFFFFu / n) return 0;
            if (size > 0xFFFFFFFFu - slot * n) return 0;
            size += slot * n;
        }
        if (size > buffer) buffer = size;
    }
    if (buffer > 512u * 1024 * 1024 || buffer - original > 256u * 1024 * 1024) return 0;
    return buffer;
}

inline void* __cdecl allocate_streaming_buffer(DWORD alignment, DWORD size) {
    using Allocate = void*(__cdecl*)(DWORD, DWORD);
    const Allocate allocate = reinterpret_cast<Allocate>(0x58EC30);
    MemoryMap* maps = *reinterpret_cast<MemoryMap**>(0x95C2F0);
    const int count = *reinterpret_cast<int*>(0x95C7F4);
    capacity_increased = false;
    if (maps && count > 0 && count <= 128) {
        const DWORD bytes = static_cast<DWORD>(count) * sizeof(MemoryMap);
        BYTE* copy = static_cast<BYTE*>(HeapAlloc(GetProcessHeap(), 0, bytes));
        if (copy) {
            const BYTE* from = reinterpret_cast<const BYTE*>(maps);
            for (DWORD i = 0; i < bytes; ++i) copy[i] = from[i];
            const DWORD bigger = expanded_size(reinterpret_cast<MemoryMap*>(copy), count, size);
            if (bigger > size) {
                if (void* buffer = allocate(alignment, bigger)) {
                    BYTE* to = reinterpret_cast<BYTE*>(maps);
                    for (DWORD i = 0; i < bytes; ++i) to[i] = copy[i];
                    *reinterpret_cast<DWORD*>(0x95C1C8) = bigger;
                    capacity_increased = true;
                    HeapFree(GetProcessHeap(), 0, copy);
                    char line[96];
                    wsprintfA(line, "streaming: buffer %lu MB, region slots x%u (%u regions)",
                              static_cast<unsigned long>(bigger >> 20), kSlotFactor, kRegions);
                    depth_fix::log_line(line);
                    return buffer;
                }
            }
            HeapFree(GetProcessHeap(), 0, copy);
        }
    }
    depth_fix::log_line("streaming: stock buffer kept");
    return allocate(alignment, size);
}

using FindPacksFn = void(__thiscall*)(void*, const void*, void*, void*, void*);
inline FindPacksFn find_packs_original;
inline void __fastcall find_terrain_packs(void* terrain, void*, const void* position, void* region, void* extra, void* packs) {
    float& distance = *reinterpret_cast<float*>(0x921DA4);
    const float original = distance;
    const MemoryMap* maps = *reinterpret_cast<MemoryMap**>(0x95C2F0);
    const int count = *reinterpret_cast<int*>(0x95C7F4);
    const int active = *reinterpret_cast<int*>(0x921CB0);
    const bool city = maps && active >= 0 && active < count && maps[active].part[6].slot_count > 1;
    if (capacity_increased && city && distance >= 600.0f) {
        float extended = distance + (detail_distance > 167.0f ? detail_distance - 167.0f : 0.0f);
        distance = extended < kPackRadius ? extended : kPackRadius;
    }
    find_packs_original(terrain, position, region, extra, packs);
    distance = original;
}

// ------------------------------------------------------------------ install
struct Site { ULONG_PTR address; const char* bytes; };
inline const Site kSites[] = {
    {0x52E770, "8B 49 64 E9"},
    {0x536FA0, "51 8B 4C 24 0C 55 33 ED 3B CD 0F 84"},
    {0x5E4020, "83 EC 0C 8B 4C 24 2C 8B 54 24 28"},
    {0x58EC30, "8B 44 24 04 8B 4C 24 08 56 50 51 FF 15"},
    {0x55898F, "A1 B0 1C 92 00 8D 1C C6"},
    {0x5146B4, "A3 A4 1D 92 00 C2 04 00"},
    {0x537872, "A3 C8 C1 95 00 B8 39 8E E3 38"},
    {0x50DFCB, "8B 0D F0 C2 95 00 8D 44 10 01"},
    {0x51EDEA, "A1 F4 C7 95 00 33 FF 85 C0"},
    {0x5DA010, "6A FF 68 ? ? ? ? 64 A1 00 00 00 00 50 64 89 25 00 00 00 00 83 EC 6C 53 55 8B E9"},
    {0x5D1EE0, "8B 44 24 04 56 6A 00 6A 00 6A 00 6A 00 8B F1 8B 4E 38"},
    {0x5BB080, "56 8B 71 38 33 C0 85 F6 7E ? 57 8B 7C 24 0C 53"},
    {0x5BB4E0, "8B 44 24 04 8B 44 81 18 C2 04 00"},
    {0x5D1F31, "8D 7B 18 8B 0F 8B 41 44"},
    {0x5DA71C, "8D 5D 18 90 8B 0B 8B 41 44"},
    {0x5DA7FC, "8D 5D 18 90 8B 0B 8B 41 44"},
    {0x5DBEE3, "8D 73 18 8B 0E 8B 41 44"},
    {0x5DD637, "8D 6B 18 8D 9B 00 00 00 00 8B 7D 00"},
    {0x5E149B, "8B 44 85 18 8B 50 44 8B 92 C0 00 00 00"},
    {0x5DBCD3, "8D 44 81 18 83 C1 18 8B D0 2B D1 68"},
    {0x5E1ABA, "8D 44 86 18 8D 4E 18 8B D0 2B D1 68"},
    {0x519A40, "8B 0D 24 C9 95 00 03 F2 6B F6 1C"},
    {0x5199B0, "B9 FF FF 00 00 33 C0 66 39 0D"},
    {0x519A20, "8B 54 24 04 56 33 F6 85 D2 7E ? B8"},
    {0x519A93, "B8 10 23 92 00 0F B7 10"},
    {0x519AA3, "3D 0C 24 92 00 7C"},
    {0x519AB4, "8D 88 F8 22 92 00 C7 44 24 04 FF FF 7F 7F"},
    {0x519AE3, "66 C7 80 10 23 92 00 FF FF"},
    {0x519AFB, "8D 80 04 23 92 00 89 08"},
    {0x52E9F2, "BE 00 23 92 00 EB"},
    {0x52EB49, "81 FE FC 23 92 00 0F 8C"},
    {0x53B92E, "B8 10 23 92 00 8D 6F 20"},
    {0x53BA94, "3D 0C 24 92 00 89 44 24 14"},
    {0x5401CA, "BD 10 23 92 00 90 66 8B 5D 00"},
    {0x540201, "81 FD 0C 24 92 00 7C"},
    {0x540214, "81 C7 F8 22 92 00 8D 57 0C"},
    {0x565CBE, "BE 00 23 92 00 EB"},
    {0x565E19, "81 FE FC 23 92 00 0F 8C"},
    {0x52EB3D, "8B 4B 3C 89 04 8B FF 43 3C 83 C6 1C"},
    {0x565E0D, "8B 53 3C 89 0C 93 FF 43 3C 83 C6 1C"},
    {0x53BA03, "89 44 94 34 FF 84 24 B4 00 00 00 8D 4C 24 28"},
    {0x53D5E0, "55 8B EC 83 E4 F0 81 EC 14 01 00 00 A0"},
    {0x53D89C, "E8 ? ? ? ? E8 ? ? ? ? 8B CB E8"},
    {0x53D900, "8B BC B4 84 00 00 00 8D 4C 24 60"},
    {0x4F5510, "8B 44 24 08 83 F8 01 53 55 8B 6C 24 0C"},
    {0x4F536B, "8B 46 50 8B 50 1C 89 3C 90 FF 40 1C 56 8B CF"},
    {0x5198A0, "8B 15 40 1E 92 00 33 C9 85 D2"},
    {0x5346BC, "3D D4 22 92 00 89 44 24 10"},
    {0x55544B, "BB 09 00 00 00 6A 28 E8"},
    {0x5198D5, "8B 04 8D 28 C9 95 00 C3"},
    {0x5198F0, "39 14 8D 28 C9 95 00 74"},
    {0x52E710, "8B 34 BD 28 C9 95 00 3B F3"},
    {0x52E72A, "8B 0C BD 28 C9 95 00 51 E8"},
    {0x52E73A, "89 1C BD 28 C9 95 00 A1"},
    {0x53B860, "A1 28 C9 95 00 53 33 DB 3B C3"},
    {0x53B8A6, "89 34 BD 28 C9 95 00 89 6E 0C"},
    {0x5456C7, "39 04 8D 28 C9 95 00 74"},
    {0x619729, "E8 ? ? ? ? 8B 8C 24 D4 00 00 00 33 C0 83 C4 14"},
    {0x55BAD3, "E8 ? ? ? ? 53 A3 ? ? ? ? 89 1D"},
    {0x54EC50, "55 8B EC 83 E4 F8 6A FF 68"},
};

inline ULONG_PTR call_target(ULONG_PTR call) { return call + 5 + get32(call + 1); }

inline ULONG_PTR loaded_operand(ULONG_PTR stock) {
    const ULONG_PTR offset = stock - kLoadedBase;
    const ULONG_PTR base = reinterpret_cast<ULONG_PTR>(loaded_regions);
    return offset >= kLoadedStockSize ? base + sizeof(loaded_regions) + (offset - kLoadedStockSize) : base + offset;
}

inline void install(float draw_scale) {
    for (const Site& site : kSites)
        if (!matches(site.address, site.bytes)) {
            char line[64];
            wsprintfA(line, "streaming: unexpected code at %08lx, not applied", static_cast<unsigned long>(site.address));
            depth_fix::log_line(line);
            return;
        }
    if (call_target(0x55BAD3) != 0x58EC30 || call_target(0x619729) != 0x536FA0) {
        depth_fix::log_line("streaming: unexpected call targets, not applied");
        return;
    }
    trampolines = static_cast<BYTE*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!trampolines) return;
    detail_distance = 167.0f * (draw_scale > 1.0f ? draw_scale : 1.0f);
    for (unsigned i = 0; i < kLoaded; ++i) {
        for (int a = 0; a < 3; ++a) { loaded_regions[i].minimum[a] = 3.402823466e+38f; loaded_regions[i].maximum[a] = -3.402823466e+38f; }
        loaded_regions[i].allocation = 0xFFFF;
        loaded_regions[i].padding = 0;
    }
    shadow_vtable[0] = reinterpret_cast<ULONG_PTR>(&visit_shadow_region);

    // Region allocator pool: one allocator per resident region.
    put32(0x921E40, kRegions);
    put32(0x9222D4 + 0x10, kRegions);
    put32(0x55544C, kLoaded);
    const DWORD allocators = reinterpret_cast<ULONG_PTR>(region_allocators);
    static const ULONG_PTR allocator_operands[] = {0x5198D8, 0x5198F3, 0x52E713, 0x52E72D, 0x52E73D, 0x53B861, 0x53B8A9, 0x5456CA};
    for (ULONG_PTR operand : allocator_operands) put32(operand, allocators);

    // District mission tables become a pointer to kRegions entries.
    create_manager_original = reinterpret_cast<CreateManagerFn>(trampoline(0x5DA010, 7));
    put_jump(0x5DA010, reinterpret_cast<void*>(&create_mission_manager), 7);
    put_jump(0x5D1EE0, reinterpret_cast<void*>(&add_district_table), 5);
    put_jump(0x5BB080, reinterpret_cast<void*>(&remove_district_table), 5);
    put_jump(0x5BB4E0, reinterpret_cast<void*>(&get_district_table), 5);
    static const ULONG_PTR lea_sites[] = {0x5D1F31, 0x5DA71C, 0x5DA7FC, 0x5DBEE3, 0x5DD637};
    for (ULONG_PTR lea : lea_sites) put8(lea, 0x8B);
    put_jump(0x5E149B, reinterpret_cast<void*>(&stub_mission_lookup), 7);
    put_jump(0x5DBCD3, reinterpret_cast<void*>(&stub_mission_sort), 7);
    put_jump(0x5E1ABA, reinterpret_cast<void*>(&stub_mission_frame_sort), 7);

    // Loaded region cache (9 -> 17 entries).
    put_jump(0x5199B0, reinterpret_cast<void*>(&count_loaded_regions), 5);
    put_jump(0x519A20, reinterpret_cast<void*>(&get_loaded_region), 5);
    static const ULONG_PTR loaded_operands[] = {0x519A94, 0x519AA4, 0x519AB6, 0x519AE6, 0x519AFD, 0x52E9F3, 0x52EB4B,
                                                0x53B92F, 0x53BA95, 0x5401CB, 0x540203, 0x540216, 0x565CBF, 0x565E1B};
    for (ULONG_PTR operand : loaded_operands) put32(operand, loaded_operand(get32(operand)));
    put_jump(0x52EB3D, reinterpret_cast<void*>(&stub_box_capacity), 9);
    put_jump(0x565E0D, reinterpret_cast<void*>(&stub_sphere_capacity), 9);
    put_jump(0x53BA03, reinterpret_cast<void*>(&stub_neighbour_capacity), 11);

    // Entity region membership limits.
    put_jump(0x4F5510, reinterpret_cast<void*>(&stub_entity_count), 7);
    put_jump(0x4F536B, reinterpret_cast<void*>(&stub_entity_capacity), 6);

    // Stencil shadow region list.
    render_shadows_original = reinterpret_cast<RenderShadowsFn>(trampoline(0x53D5E0, 6));
    put_jump(0x53D5E0, reinterpret_cast<void*>(&render_stencil_shadows), 6);
    collect_shadows_original = reinterpret_cast<CollectShadowsFn>(call_target(0x53D89C));
    put_jump(0x53D89C, reinterpret_cast<void*>(&collect_shadow_regions), 5, 0xE8);
    put_jump(0x53D900, reinterpret_cast<void*>(&stub_shadow_read), 7);

    // Region list traversal cap, radar list, streaming buffer, terrain packs.
    build_region_list_original = reinterpret_cast<BuildRegionListFn>(trampoline(0x536FA0, 6));
    put_jump(0x536FA0, reinterpret_cast<void*>(&build_region_list), 6);
    put_jump(0x619729, reinterpret_cast<void*>(&build_radar_region_list), 5, 0xE8);
    put_jump(0x55BAD3, reinterpret_cast<void*>(&allocate_streaming_buffer), 5, 0xE8);
    find_packs_original = reinterpret_cast<FindPacksFn>(trampoline(0x54EC50, 6));
    put_jump(0x54EC50, reinterpret_cast<void*>(&find_terrain_packs), 6);

    DWORD previous = 0;
    VirtualProtect(trampolines, 4096, PAGE_EXECUTE_READ, &previous);
    depth_fix::log_line("streaming: extended region cache installed");
}

// Two fixed pools only ever allocate one block, then hand out null:
//   0x544300 per-object cache, 24 entries of 0x38 (freed after 10 idle frames);
//   0x502F60 286 entries of 0x64.
// With 16 resident regions and larger crowds the 24-entry cache ran dry and
// 0x544382 dereferenced null (crash 2026-11-25). Blocks are never freed, so
// allowing more of them only costs memory: 16*0x540 + 8*0x7000 bytes at most.
inline void grow_pools() {
    static const BYTE cache_check[] = {0x83, 0x3D, 0xEC, 0x0A, 0x96, 0x00, 0x01, 0x7C};
    static const BYTE list_check[] = {0x83, 0x3D, 0x70, 0xBB, 0x95, 0x00, 0x01, 0x7C};
    const BYTE cache_blocks = 16, list_blocks = 8;
    bool cache = draw_distance::same(0x544344, cache_check, sizeof(cache_check));
    bool list = draw_distance::same(0x502F60, list_check, sizeof(list_check));
    if (cache) cache = draw_distance::write(0x54434A, &cache_blocks, 1);
    if (list) list = draw_distance::write(0x502F66, &list_blocks, 1);
    depth_fix::log_line(cache && list ? "pools: object cache and list pool can grow"
                                      : "pools: unexpected code, pool limits left unchanged");
}

}  // namespace streaming

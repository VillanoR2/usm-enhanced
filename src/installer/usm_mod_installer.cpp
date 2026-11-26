// Ultimate Spider-Man (PC) - instalador del mod.
// Parchea USM.exe del usuario, instala las DLL del mod y, si se indica la ISO
// de PS2 del propio usuario, extrae de ella las texturas de sombras y mascaras.
// No contiene archivos del juego.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")

using Bytes = std::vector<uint8_t>;

namespace {

const wchar_t* const BACKUP_EXE = L"USM.exe.mod-backup";
const wchar_t* const ORIGINAL_BINK = L"binkw32_original.dll";
const wchar_t* const MARKER = L"usm-mod-instalado.txt";
const char* const ORIGINAL_EXE_SHA = "d31ff9ce63b43e17b8c12d125fe901d5cc101f0655253b2e636482a4db664719";
const char* const ORIGINAL_BINK_SHA = "c6c06e2d21f2179f0b7fbe829a4b17a7b371440d2c679253ec86b64d421185bd";

struct Patch { const char* name; uint32_t va; const char* before; const char* after; bool optional_text; };
const Patch PATCHES[] = {
    {"voces en ingles", 0x50EF30, "8b44240483f804", "b8348b8800c390", false},
    {"textos en espanol", 0x62EF5E, "e8ade01e0083f804", "83c40831c0b00390", true},
};

struct Error : std::runtime_error { using std::runtime_error::runtime_error; };

// Messages and progress: console in command-line mode, the window in GUI mode.
void console_say(const std::wstring& text) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!out || out == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    if (GetFileType(out) == FILE_TYPE_CHAR &&
        WriteConsoleW(out, text.c_str(), static_cast<DWORD>(text.size()), &written, nullptr))
        return;
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string bytes(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), bytes.data(), size, nullptr, nullptr);
    WriteFile(out, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
}
void console_step(int, const std::wstring&) {}
void (*say)(const std::wstring&) = console_say;
void (*step)(int percent, const std::wstring& stage) = console_step;

// Adjustable settings (usm_mod.ini [Graficos]), chosen in the installer window.
struct Settings {
    double draw_distance = 6.0;   // DistanciaDibujado 1.0-7.0
    double near_plane = 0.3;      // PlanoCercano 0.1-1.0
    int anisotropy = 16;          // FiltradoAnisotropico 0-16
    int spawn_min = 60;           // RadioAparicionMinimo 10-100
    int spawn_max = 150;          // RadioAparicionMaximo 90-200
    bool streaming = true;        // AmpliarCarga
    int pedestrians = 24;         // CantidadPeatones 10-40
    int traffic = 45;             // CantidadTrafico 30-60
    int screen_mode = 0;          // ModoPantalla: 0 ventana, 1 sin bordes, 2 pantalla completa
    std::wstring resolution = L"auto";  // Resolucion: "auto" o "1920x1080"
};

Bytes hex(const char* text) {
    Bytes out;
    for (size_t i = 0; text[i] && text[i + 1]; i += 2) {
        char pair[3] = {text[i], text[i + 1], 0};
        out.push_back(static_cast<uint8_t>(std::strtoul(pair, nullptr, 16)));
    }
    return out;
}

std::string sha256(const uint8_t* data, size_t size) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    uint8_t digest[32];
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0 ||
        BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) != 0 ||
        BCryptHashData(hash, const_cast<uint8_t*>(data), static_cast<ULONG>(size), 0) != 0 ||
        BCryptFinishHash(hash, digest, sizeof(digest), 0) != 0)
        throw Error("No se pudo calcular SHA-256");
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    static const char* digits = "0123456789abcdef";
    std::string out;
    for (uint8_t b : digest) { out += digits[b >> 4]; out += digits[b & 15]; }
    return out;
}
std::string sha256(const Bytes& b) { return sha256(b.data(), b.size()); }

bool exists(const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

Bytes read_file(const std::wstring& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw Error("No se pudo leer un archivo necesario");
    return Bytes(std::istreambuf_iterator<char>(f), {});
}

void write_file(const std::wstring& path, const Bytes& data) {
    const std::wstring temp = path + L".tmp";
    {
        std::ofstream f(temp, std::ios::binary | std::ios::trunc);
        if (!f) throw Error("No se pudo escribir en la carpeta del juego (ejecuta como administrador)");
        f.write(reinterpret_cast<const char*>(data.data()), data.size());
        if (!f) throw Error("Fallo al escribir un archivo");
    }
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
        throw Error("No se pudo reemplazar un archivo del juego");
}

uint32_t u32(const Bytes& b, size_t o) {
    if (o + 4 > b.size()) throw Error("Datos de la ISO truncados");
    return b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (uint32_t(b[o + 3]) << 24);
}
uint16_t u16(const Bytes& b, size_t o) {
    if (o + 2 > b.size()) throw Error("Datos de la ISO truncados");
    return static_cast<uint16_t>(b[o] | (b[o + 1] << 8));
}

std::wstring exe_dir() {
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring s(path, n);
    return s.substr(0, s.find_last_of(L"\\/"));
}

// ---------------------------------------------------------------- USM.exe

size_t exe_offset(uint32_t va) {
    struct Section { uint32_t rva, raw, size; };
    const Section sections[] = {{0x1000, 0x1000, 0x46E000}, {0x46F000, 0x46F000, 0xAC000}, {0x51B000, 0x51B000, 0x3C000}};
    for (const auto& s : sections)
        if (va >= 0x400000 + s.rva && va < 0x400000 + s.rva + s.size) return va - 0x400000 - s.rva + s.raw;
    throw Error("Direccion fuera del ejecutable");
}

enum class SiteState { original, patched, other };
SiteState site_state(const Bytes& exe, const Patch& p) {
    const size_t o = exe_offset(p.va);
    const Bytes before = hex(p.before), after = hex(p.after);
    if (o + before.size() > exe.size()) return SiteState::other;
    if (std::equal(before.begin(), before.end(), exe.begin() + o)) return SiteState::original;
    if (std::equal(after.begin(), after.end(), exe.begin() + o)) return SiteState::patched;
    return SiteState::other;
}

// ---------------------------------------------------------------- ISO de PS2

class Iso {
public:
    explicit Iso(const std::wstring& path) {
        file_ = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (file_ == INVALID_HANDLE_VALUE) throw Error("No se pudo abrir la ISO");
    }
    ~Iso() { CloseHandle(file_); }
    Bytes read(uint64_t offset, size_t size) const {
        Bytes out(size);
        LARGE_INTEGER at; at.QuadPart = static_cast<LONGLONG>(offset);
        DWORD got = 0;
        if (!SetFilePointerEx(file_, at, nullptr, FILE_BEGIN) ||
            !ReadFile(file_, out.data(), static_cast<DWORD>(size), &got, nullptr) || got != size)
            throw Error("Lectura de la ISO incompleta");
        return out;
    }
    // ISO9660: devuelve (sector, tamano) de un archivo en la ruta indicada.
    std::pair<uint32_t, uint32_t> find(const std::vector<std::string>& parts) const {
        const Bytes pvd = read(16 * 2048ull, 2048);
        if (pvd[0] != 1 || std::memcmp(&pvd[1], "CD001", 5) != 0) throw Error("La ISO no tiene sistema ISO9660");
        uint32_t extent = u32(pvd, 156 + 2), size = u32(pvd, 156 + 10);
        for (size_t i = 0; i < parts.size(); ++i) {
            const Bytes dir = read(uint64_t(extent) * 2048, size);
            bool found = false;
            for (size_t o = 0; o < dir.size();) {
                const uint8_t len = dir[o];
                if (!len) { o = (o / 2048 + 1) * 2048; continue; }
                const uint8_t name_len = dir[o + 32];
                std::string name(reinterpret_cast<const char*>(&dir[o + 33]), name_len);
                if (auto semi = name.find(';'); semi != std::string::npos) name.resize(semi);
                if (_stricmp(name.c_str(), parts[i].c_str()) == 0) {
                    extent = u32(dir, o + 2); size = u32(dir, o + 10); found = true; break;
                }
                o += len;
            }
            if (!found) throw Error("La ISO no contiene PACKS/AMALGA.PAK (no es Ultimate Spider-Man de PS2)");
        }
        return {extent, size};
    }
private:
    HANDLE file_;
};

// LZO1X: descompresion de un bloque con tamano de salida conocido.
Bytes lzo1x(const uint8_t* ip, size_t in_size, size_t out_size) {
    Bytes out; out.reserve(out_size);
    const uint8_t* const end = ip + in_size;
    auto need = [&](size_t n) { if (static_cast<size_t>(end - ip) < n) throw Error("Bloque LZO corrupto"); };
    auto literal = [&](size_t n) { need(n); out.insert(out.end(), ip, ip + n); ip += n; };
    auto copy = [&](size_t distance, size_t n) {
        if (distance == 0 || distance > out.size()) throw Error("Bloque LZO corrupto");
        size_t from = out.size() - distance;
        for (size_t i = 0; i < n; ++i) out.push_back(out[from + i]);
    };
    auto run = [&](size_t t, size_t base) {
        if (t == 0) { need(1); while (*ip == 0) { t += 255; ++ip; need(1); } t += base + *ip++; }
        return t;
    };
    size_t t = 0;
    need(1);
    enum { LOOP, FIRST, MATCH, NEXT } state = LOOP;
    if (*ip > 17) {
        t = *ip++ - 17;
        if (t < 4) state = NEXT; else { literal(t); state = FIRST; }
    }
    for (;;) {
        if (state == LOOP) {
            need(1); t = *ip++;
            if (t >= 16) { state = MATCH; continue; }
            literal(run(t, 15) + 3);
            state = FIRST;
        }
        if (state == FIRST) {
            need(1); t = *ip++;
            if (t < 16) {
                need(1); size_t distance = 1 + 0x800 + (t >> 2) + (size_t(*ip++) << 2);
                copy(distance, 3);
                t = ip[-2] & 3;
                if (t == 0) { state = LOOP; continue; }
                state = NEXT;
            } else state = MATCH;
        }
        if (state == NEXT) { literal(t); need(1); t = *ip++; state = MATCH; }
        // MATCH
        size_t distance = 0, length = 0;
        if (t >= 64) {
            need(1); distance = 1 + ((t >> 2) & 7) + (size_t(*ip++) << 3); length = (t >> 5) - 1 + 2;
        } else if (t >= 32) {
            t = run(t & 31, 31); need(2);
            distance = 1 + (ip[0] >> 2) + (size_t(ip[1]) << 6); ip += 2; length = t + 2;
        } else if (t >= 16) {
            size_t high = (t & 8) << 11;
            t = run(t & 7, 7); need(2);
            distance = high + (ip[0] >> 2) + (size_t(ip[1]) << 6); ip += 2;
            if (distance == 0) break;  // fin del flujo
            distance += 0x4000; length = t + 2;
        } else {
            need(1); distance = 1 + (t >> 2) + (size_t(*ip++) << 2); length = 2;
        }
        copy(distance, length);
        t = ip[-2] & 3;
        state = t == 0 ? LOOP : NEXT;
    }
    if (out.size() != out_size) throw Error("Bloque LZO de tamano inesperado");
    return out;
}

Bytes nflc(const Bytes& raw) {
    if (raw.size() < 64 || std::memcmp(raw.data(), "nFlC", 4) != 0) throw Error("Paquete PS2 no reconocido");
    const uint32_t count = u32(raw, 8) & 0xFFFF;
    Bytes out;
    for (uint32_t i = 0; i < count; ++i) {
        const size_t at = size_t(i) * 32768;
        if (std::memcmp(&raw[at], "nFlC", 4) != 0 || u32(raw, at + 44) != out.size()) throw Error("Paquete PS2 corrupto");
        const uint16_t zsize = u16(raw, at + 18);
        const uint32_t size = u32(raw, at + 24);
        if (at + 64 + zsize > raw.size()) throw Error("Paquete PS2 truncado");
        Bytes part = lzo1x(&raw[at + 64], zsize, size);
        out.insert(out.end(), part.begin(), part.end());
    }
    if (out.size() != u32(raw, 40)) throw Error("Paquete PS2 de tamano inesperado");
    return out;
}

struct Amalga {
    const Iso& iso;
    uint64_t start;
    uint32_t base = 0;
    std::vector<std::pair<std::string, std::pair<uint32_t, uint32_t>>> entries;
    explicit Amalga(const Iso& i) : iso(i) {
        auto [extent, size] = iso.find({"PACKS", "AMALGA.PAK"});
        start = uint64_t(extent) * 2048;
        const Bytes head = iso.read(start, 56);
        const uint32_t table_start = u32(head, 28), table_end = u32(head, 36);
        base = u32(head, 24);
        if (table_end <= table_start || (table_end - table_start) % 80 || table_end > size) throw Error("AMALGA.PAK no reconocido");
        const Bytes table = iso.read(start + table_start, table_end - table_start);
        for (size_t o = 0; o < table.size(); o += 80) {
            std::string name(reinterpret_cast<const char*>(&table[o + 48]), strnlen(reinterpret_cast<const char*>(&table[o + 48]), 32));
            entries.push_back({name, {base + u32(table, o + 8), u32(table, o + 12)}});
        }
    }
    Bytes pack(const std::string& name) const {
        for (const auto& [n, loc] : entries)
            if (n == name) return nflc(iso.read(start + loc.first, loc.second));
        throw Error("AMALGA.PAK no contiene el paquete " + name);
    }
};

// Devuelve el recurso (hash, tipo) de un PS2PACK descomprimido.
Bytes resource(const Bytes& data, uint32_t hash, uint32_t type) {
    const uint32_t dir_off = u32(data, 24), data_base = u32(data, 28);
    const size_t s = dir_off + 16;
    uint32_t counts[2];
    for (int i = 0; i < 2; ++i) counts[i] = u32(data, s + i * 8 + 4) & 0xFFFF;
    size_t pos = s + 15 * 8 + 5 * 4 + 70 * 4 * 2;
    auto take = [&](size_t n, size_t size) {
        while (pos % 8 || u32(data, pos) == 0xE3E3E3E3u) {
            if (u32(data, pos) != 0xE3E3E3E3u) throw Error("Directorio PS2PACK no reconocido");
            pos += 4;
        }
        size_t at = pos; pos += n * size; return at;
    };
    take(counts[0], 4);
    const size_t at = take(counts[1], 16);
    for (uint32_t i = 0; i < counts[1]; ++i) {
        const size_t r = at + size_t(i) * 16;
        if (u32(data, r) == hash && u32(data, r + 4) == type) {
            const size_t off = size_t(data_base) + u32(data, r + 8), size = u32(data, r + 12);
            if (off + size > data.size()) throw Error("Recurso PS2 fuera de rango");
            return Bytes(data.begin() + off, data.begin() + off + size);
        }
    }
    throw Error("Recurso PS2 no encontrado");
}

// TIM2: CLUT de 32 bits (alfa PS2 0x80 = opaco) con orden CSM1 para 256 colores.
std::vector<uint32_t> clut32(const Bytes& b, size_t at, unsigned count, bool csm2) {
    std::vector<uint32_t> raw(count), out(count);
    for (unsigned i = 0; i < count; ++i) {
        const uint8_t* c = &b.at(at + i * 4);
        const unsigned a = std::min(unsigned(c[3]) * 2, 255u);
        raw[i] = (a << 24) | (c[0] << 16) | (c[1] << 8) | c[2];  // BGRA en memoria = ARGB en DWORD
    }
    for (unsigned i = 0; i < count; ++i)
        out[i] = (count == 256 && !csm2) ? raw[(i & ~0x18u) | ((i & 0x08u) << 1) | ((i & 0x10u) >> 1)] : raw[i];
    return out;
}

struct Tim2 { unsigned width, height, mips, itype, ctype, colors; size_t header, image, clut, image_size; };
Tim2 tim2(const Bytes& b) {
    if (b.size() < 64 || std::memcmp(b.data(), "TIM2", 4) != 0) throw Error("Textura PS2 no reconocida");
    Tim2 t{};
    t.header = b[5] ? 128 : 16;
    t.image_size = u32(b, t.header + 8);
    const uint16_t hsize = u16(b, t.header + 12);
    t.colors = u16(b, t.header + 14);
    t.mips = b.at(t.header + 17); t.ctype = b.at(t.header + 18); t.itype = b.at(t.header + 19);
    t.width = u16(b, t.header + 20); t.height = u16(b, t.header + 22);
    t.image = t.header + hsize; t.clut = t.image + t.image_size;
    return t;
}

void append(Bytes& out, uint32_t bgra) {
    out.push_back(bgra & 255); out.push_back((bgra >> 8) & 255); out.push_back((bgra >> 16) & 255); out.push_back(bgra >> 24);
}

// The PS2 textures stop at 4x4 or 8x8; the remaining mip levels down to 1x1 are
// computed here by averaging (no game data is embedded in the installer).
Bytes shadow_mask(const Bytes& tm2, unsigned levels) {
    const Tim2 t = tim2(tm2);
    if (t.mips != levels || t.itype != 5 || t.ctype != 3 || t.width != 64 || t.height != 64 || t.colors != 256)
        throw Error("Formato inesperado de la textura de sombra");
    const auto palette = clut32(tm2, t.clut, t.colors, false);
    Bytes out;
    size_t offset = t.image;
    for (unsigned level = 0; level < levels; ++level) {
        const unsigned side = 64 >> level;
        if (u32(tm2, t.header + 0x40 + 4 * level) != side * side) throw Error("Mipmap PS2 inesperado");
        for (unsigned i = 0; i < side * side; ++i) append(out, palette[tm2.at(offset + i)]);
        offset += side * side;
    }
    size_t parent = out.size() - static_cast<size_t>(4u * (64u >> (levels - 1)) * (64u >> (levels - 1)));
    for (unsigned side = 64u >> levels; side >= 1; side >>= 1) {
        const unsigned parent_side = side * 2;
        const size_t level = out.size();
        for (unsigned y = 0; y < side; ++y)
            for (unsigned x = 0; x < side; ++x)
                for (unsigned c = 0; c < 4; ++c) {
                    unsigned sum = 2;
                    for (unsigned dy = 0; dy < 2; ++dy)
                        for (unsigned dx = 0; dx < 2; ++dx)
                            sum += out[parent + ((2 * y + dy) * parent_side + 2 * x + dx) * 4 + c];
                    out.push_back(static_cast<uint8_t>(sum / 4));
                }
        parent = level;
        if (side == 1) break;
    }
    return out;
}

struct Asset { const wchar_t* file; const char* pack; uint32_t hash; const char* source_sha; const char* result_sha; };
const Asset ASSETS[] = {
    {L"mask-0dc218aa.bgra", "GAME", 230824106u, "bb42ac16bb8a8ec00845cfe0f398183f58655f3799250977f89260cd362c159e",
     "1438b3f3d24f8904db93a89616ef223b581be65648281bc5b1a60b24dac6f760"},
    {L"mask-6064aa59.bgra", "ARACHNO_MAN_COSTUME", 1617209945u, "10afb2076bd3f94c95e5325c82afc83c5f469d16cdc48703de6716a094620740",
     "f70ea059f592c081ac5a5823f2158d1251750d4bbbee15021023257237ac1028"},
};

int extract_assets(const std::wstring& iso_path, const std::wstring& game) {
    say(L"Leyendo la ISO de PS2...\n");
    Iso iso(iso_path);
    Amalga amalga(iso);
    Bytes game_pack, costume_pack;
    int written = 0;
    for (const Asset& a : ASSETS) {
        Bytes& pack = std::strcmp(a.pack, "GAME") == 0 ? game_pack : costume_pack;
        if (pack.empty()) pack = amalga.pack(a.pack);
        const Bytes source = resource(pack, a.hash, 6);
        if (sha256(source) != a.source_sha) {
            say(L"  " + std::wstring(a.file) + L": la textura de esta ISO es distinta; se omite.\n");
            continue;
        }
        Bytes result;
        result = shadow_mask(source, a.hash == 230824106u ? 5 : 4);
        if (sha256(result) != a.result_sha) throw Error("La textura extraida no coincide con la esperada");
        write_file(game + L"\\" + a.file, result);
        say(L"  " + std::wstring(a.file) + L" listo.\n");
        ++written;
    }
    return written;
}

// ---------------------------------------------------------------- instalar / desinstalar

std::optional<std::wstring> registry_install_path() {
    for (const wchar_t* key : {L"SOFTWARE\\WOW6432Node\\Activision\\Ultimate Spider-Man", L"SOFTWARE\\Activision\\Ultimate Spider-Man"}) {
        wchar_t value[MAX_PATH]; DWORD size = sizeof(value);
        if (RegGetValueW(HKEY_LOCAL_MACHINE, key, L"InstallPath", RRF_RT_REG_SZ, nullptr, value, &size) == ERROR_SUCCESS)
            return std::wstring(value);
    }
    return std::nullopt;
}

std::wstring trim_quotes(std::wstring s) {
    while (!s.empty() && (s.back() == L' ' || s.back() == L'"' || s.back() == L'\\')) s.pop_back();
    while (!s.empty() && (s.front() == L' ' || s.front() == L'"')) s.erase(s.begin());
    return s;
}

void copy_mod_file(const std::wstring& name, const std::wstring& game) {
    const std::wstring source = exe_dir() + L"\\archivos\\" + name;
    if (!exists(source)) throw Error("Falta un archivo del mod junto al instalador (carpeta archivos)");
    write_file(game + L"\\" + name, read_file(source));
}

std::wstring ini_number(double value) {
    wchar_t text[32];
    swprintf_s(text, L"%.1f", value);
    return text;
}

// Writes the chosen values over the keys of usm_mod.ini, keeping its comments.
void apply_settings(const std::wstring& ini, const Settings& s) {
    auto put = [&](const wchar_t* key, const std::wstring& value) {
        if (!WritePrivateProfileStringW(L"Graficos", key, value.c_str(), ini.c_str()))
            throw Error("No se pudieron guardar los ajustes en usm_mod.ini");
    };
    put(L"DistanciaDibujado", ini_number(s.draw_distance));
    put(L"PlanoCercano", ini_number(s.near_plane));
    put(L"FiltradoAnisotropico", std::to_wstring(s.anisotropy));
    put(L"RadioAparicionMinimo", std::to_wstring(s.spawn_min));
    put(L"RadioAparicionMaximo", std::to_wstring(s.spawn_max));
    put(L"AmpliarCarga", s.streaming ? L"1" : L"0");
    put(L"CantidadPeatones", std::to_wstring(s.pedestrians));
    put(L"CantidadTrafico", std::to_wstring(s.traffic));
    put(L"ModoPantalla", std::to_wstring(s.screen_mode));
    put(L"Resolucion", s.resolution.empty() ? L"auto" : s.resolution);
}

Settings read_settings(const std::wstring& ini) {
    Settings s;
    auto get = [&](const wchar_t* key, double fallback) {
        wchar_t text[32];
        GetPrivateProfileStringW(L"Graficos", key, L"", text, 32, ini.c_str());
        for (wchar_t* c = text; *c; ++c) if (*c == L',') *c = L'.';
        wchar_t* end = nullptr;
        const double value = wcstod(text, &end);
        return end != text ? value : fallback;
    };
    s.draw_distance = get(L"DistanciaDibujado", s.draw_distance);
    s.near_plane = get(L"PlanoCercano", s.near_plane);
    s.anisotropy = static_cast<int>(get(L"FiltradoAnisotropico", s.anisotropy));
    s.spawn_min = static_cast<int>(get(L"RadioAparicionMinimo", s.spawn_min));
    s.spawn_max = static_cast<int>(get(L"RadioAparicionMaximo", s.spawn_max));
    s.streaming = get(L"AmpliarCarga", 1) >= 1;
    s.pedestrians = static_cast<int>(get(L"CantidadPeatones", s.pedestrians));
    s.traffic = static_cast<int>(get(L"CantidadTrafico", s.traffic));
    s.screen_mode = static_cast<int>(get(L"ModoPantalla", s.screen_mode));
    wchar_t resolution[24];
    GetPrivateProfileStringW(L"Graficos", L"Resolucion", L"auto", resolution, 24, ini.c_str());
    s.resolution = resolution;
    // Tested limits ("Alto"); higher values ("Ultra") could stall new games.
    auto clamp = [](auto value, auto low, auto high) { return value < low ? low : value > high ? high : value; };
    s.draw_distance = clamp(s.draw_distance, 1.0, 6.0);
    s.pedestrians = clamp(s.pedestrians, 10, 24);
    s.traffic = clamp(s.traffic, 30, 45);
    s.spawn_max = clamp(s.spawn_max, 90, 150);
    s.spawn_min = clamp(s.spawn_min, 10, s.spawn_max - 20 < 60 ? s.spawn_max - 20 : 60);
    return s;
}

// Which languages this copy of the game really contains. Editions differ: the
// European one ships voices EN/SP/FR/IT and text in five languages, the US one
// English only. Forcing a language whose files are missing leaves the game
// stuck on the first loading screen, so each patch depends on its files.
struct Edition {
    std::vector<std::wstring> voices, texts;   // e.g. {L"EN", L"SP"} / {L"inglés", L"español"}
    bool english_voices = false, spanish_text = false;
    std::wstring label;
};

Edition scan_edition(const std::wstring& game) {
    Edition e;
    const std::wstring streams = game + L"\\Data\\sound\\pc\\STREAMS\\";
    for (const wchar_t* code : {L"EN", L"SP", L"FR", L"IT", L"GE"})
        if (exists(streams + L"STREAMS_VOICE_" + code + L".WBK") && exists(streams + L"STREAMS_SCENE_" + code + L".WBK"))
            e.voices.push_back(code);
    struct Text { const wchar_t* pack; const wchar_t* table; const wchar_t* name; };
    const Text texts[] = {{L"ENGLISH", L"e", L"inglés"}, {L"SPANISH", L"s", L"español"}, {L"FRENCH", L"f", L"francés"},
                          {L"GERMAN", L"g", L"alemán"}, {L"ITALIAN", L"i", L"italiano"}};
    for (const Text& t : texts)
        if (exists(game + L"\\Data\\packs\\pc\\GLOBALTEXT_" + t.pack + L".PCPACK") &&
            exists(game + L"\\Data\\usm_lt" + t.table + L".usm"))
            e.texts.push_back(t.name);
    e.english_voices = std::find(e.voices.begin(), e.voices.end(), L"EN") != e.voices.end();
    e.spanish_text = std::find(e.texts.begin(), e.texts.end(), L"español") != e.texts.end();
    auto join = [](const std::vector<std::wstring>& list) {
        std::wstring out;
        for (const auto& item : list) out += (out.empty() ? L"" : L", ") + item;
        return out.empty() ? std::wstring(L"ninguno reconocido") : out;
    };
    if (e.voices.size() <= 1 && e.texts.size() <= 1)
        e.label = L"Edición de un idioma (voces: " + join(e.voices) + L"; textos: " + join(e.texts) + L")";
    else
        e.label = L"Edición multilingüe (voces: " + join(e.voices) + L"; textos: " + join(e.texts) + L")";
    return e;
}

void install(const std::wstring& game, const std::optional<std::wstring>& iso, bool spanish_text,
             const Settings* settings = nullptr) {
    step(5, L"Comprobando la instalación del juego");
    const std::wstring exe_path = game + L"\\USM.exe", bink_path = game + L"\\binkw32.dll";
    if (!exists(exe_path) || !exists(bink_path)) throw Error("En esa carpeta no esta Ultimate Spider-Man (falta USM.exe o binkw32.dll)");
    Bytes exe = read_file(exe_path);
    const bool installed = exists(game + L"\\" + BACKUP_EXE);
    Bytes original = installed ? read_file(game + L"\\" + BACKUP_EXE) : exe;
    for (const Patch& p : PATCHES)
        if (site_state(original, p) != SiteState::original)
            throw Error("Esta version de USM.exe no es compatible con el mod (se esperaba la version 1.0 de PC)");
    if (sha256(original) != ORIGINAL_EXE_SHA)
        say(L"Aviso: USM.exe no es identico al probado, pero el codigo a parchear coincide.\n");

    step(20, installed ? L"Actualizando el mod" : L"Guardando copias de seguridad de los originales");
    if (!installed) {
        const Bytes bink = read_file(bink_path);
        if (sha256(bink) != ORIGINAL_BINK_SHA)
            throw Error("binkw32.dll no es el original del juego; desinstala otros mods primero");
        write_file(game + L"\\" + BACKUP_EXE, original);
        write_file(game + L"\\" + ORIGINAL_BINK, bink);
    }
    step(35, L"Parcheando USM.exe (voces, textos y memoria)");
    const Edition edition = scan_edition(game);
    say(edition.label + L".\n");
    for (const Patch& p : PATCHES) {
        if (p.optional_text) {
            if (!spanish_text) continue;
            if (!edition.spanish_text) {
                say(L"Esta edición no trae los textos en español: se mantienen los textos originales.\n");
                continue;
            }
        } else if (!edition.english_voices) {
            say(L"Esta edición no trae las voces en inglés: se mantienen las voces originales.\n");
            continue;
        }
        const Bytes after = hex(p.after);
        std::copy(after.begin(), after.end(), original.begin() + exe_offset(p.va));
    }
    {
        // Large address aware: up to 4 GB on 64-bit Windows instead of 2 GB,
        // headroom for the 32 resident city regions.
        const size_t pe = original.size() > 0x40 ? *reinterpret_cast<const uint32_t*>(&original[0x3C]) : 0;
        if (pe + 24 > original.size() || original[pe] != 'P' || original[pe + 1] != 'E')
            throw Error("Cabecera de USM.exe inesperada");
        original[pe + 22] |= 0x20;
    }
    write_file(exe_path, original);
    say(L"USM.exe parcheado (copia original en USM.exe.mod-backup).\n");
    step(50, L"Copiando los archivos del mod");
    copy_mod_file(L"binkw32.dll", game);
    copy_mod_file(L"usm_native_input.dll", game);
    say(L"Archivos del mod copiados: binkw32.dll y usm_native_input.dll.\n");
    step(65, L"Guardando los ajustes");
    const std::wstring ini = game + L"\\usm_mod.ini";
    const std::string v2_default =
        "; Ultimate Spider-Man - ajustes del mod\r\n"
        "[Graficos]\r\n"
        "; Multiplica la distancia de dibujado de edificios, personajes, coches y objetos.\r\n"
        "; 1.0 = original del juego, 2.0 = doble (recomendado), maximo 3.5.\r\n"
        "DistanciaDibujado=2.0\r\n";
    const std::string v3_default =
        "; Ultimate Spider-Man - ajustes del mod\r\n"
        "[Graficos]\r\n"
        "; Multiplica la distancia de dibujado de edificios, personajes, coches y objetos.\r\n"
        "; 1.0 = original del juego, 3.0 = recomendado, maximo 3.5.\r\n"
        "DistanciaDibujado=3.0\r\n"
        "; Plano cercano de la camara. Valores mayores evitan el parpadeo de\r\n"
        "; texturas y modelos lejanos. 0.1 = original, 0.3 = recomendado, maximo 1.0.\r\n"
        "PlanoCercano=0.3\r\n";
    bool untouched_default = false;
    if (exists(ini)) {
        const Bytes current = read_file(ini);
        untouched_default = current == Bytes(v2_default.begin(), v2_default.end()) ||
                            current == Bytes(v3_default.begin(), v3_default.end());
    }
    if (!exists(ini) || untouched_default) {
        const std::string text = v3_default +
            "; Filtrado anisotropico de texturas (nitidez a media distancia y en angulo).\r\n"
            "; 0 = original del juego, 16 = recomendado.\r\n"
            "FiltradoAnisotropico=16\r\n"
            "; Distancia (metros) a la que aparecen peatones y trafico alrededor del jugador.\r\n"
            "; Original: minimo 10 (sube hasta 40) y maximo 90. Recomendado: 45 y 120.\r\n"
            "RadioAparicionMinimo=45\r\n"
            "RadioAparicionMaximo=120\r\n";
        write_file(ini, Bytes(text.begin(), text.end()));
    }
    {
        // Later versions: add new keys and move untouched old defaults, keeping user edits.
        const Bytes stored = read_file(ini);
        std::string text(stored.begin(), stored.end());
        auto has = [&](const std::string& s) { return text.find(s) != std::string::npos; };
        auto replace = [&](const std::string& from, const std::string& to) {
            const size_t at = text.find(from);
            if (at != std::string::npos) text.replace(at, from.size(), to);
        };
        if (!has("AmpliarCarga="))
            text += "; Mantiene cargadas mas zonas de la ciudad (32 en vez de 8) para ver\r\n"
                    "; edificios con detalle a mas distancia. 1 = activado, 0 = original.\r\n"
                    "AmpliarCarga=1\r\n";
        replace("(16 en vez de 8)", "(32 en vez de 8)");
        // v14: the draw distance limit moved from 3.5 to 7 (more regions loaded).
        replace("; 1.0 = original del juego, 3.0 = recomendado, maximo 3.5.",
                "; 1.0 = original del juego, 6.0 = recomendado y maximo.");
        replace("DistanciaDibujado=3.0\r\n", "DistanciaDibujado=6.0\r\n");
        // v1.0.2: "Ultra" removed; the mod caps values at "Alto" (6.0, 24, 45, 60-150 m).
        replace("6.0 = recomendado, maximo 7.0.", "6.0 = recomendado y maximo.");
        replace("recomendado 24, maximo 40.", "recomendado y maximo 24.");
        replace("recomendado 45, maximo 60.", "recomendado y maximo 45.");
        if (has("RadioAparicionMinimo=45\r\n") && has("RadioAparicionMaximo=120\r\n")) {
            replace("Recomendado: 45 y 120.", "Recomendado: 60 y 150.");
            replace("RadioAparicionMinimo=45\r\n", "RadioAparicionMinimo=60\r\n");
            replace("RadioAparicionMaximo=120\r\n", "RadioAparicionMaximo=150\r\n");
        }
        if (!has("CantidadPeatones="))
            text += "; Peatones simultaneos en la calle. Original 10, recomendado y maximo 24.\r\n"
                    "CantidadPeatones=24\r\n";
        if (!has("CantidadTrafico="))
            text += "; Coches simultaneos. Original 30, recomendado y maximo 45.\r\n"
                    "CantidadTrafico=45\r\n";
        write_file(ini, Bytes(text.begin(), text.end()));
    }

    if (settings) apply_settings(ini, *settings);
    say(L"Ajustes guardados en usm_mod.ini.\n");
    int assets = 0;
    if (iso) {
        step(75, L"Extrayendo sombras y máscaras de tu ISO de PS2");
        assets = extract_assets(*iso, game);
    }
    step(95, L"Terminando");
    std::ofstream marker(game + L"\\" + MARKER, std::ios::trunc);
    marker << "USM PC mod v1 instalado. Para quitarlo ejecuta el instalador y elige desinstalar.\n";

    say(L"\nInstalado en " + game + L"\n" +
        std::wstring(edition.english_voices ? L"  - Voces en inglés" : L"  - Voces originales de tu edición") +
        std::wstring(spanish_text && edition.spanish_text ? L", textos en español\n" : L", textos originales\n") +
        L"  - Mandos DualSense, DualShock 4 y Xbox, filtro de ciudad (F6)\n"
        L"  - Distancia de dibujado, zonas cargadas, peatones y tráfico según tus ajustes\n"
        L"  - Iconos de botones: teclado y raton, DualSense, DualShock y Xbox\n");
    say(assets == 2 ? L"  - Sombras y máscaras estilo PS2 (desde tu ISO)\n"
                    : L"  - Sombras y máscaras estilo PS2 generadas por el mod\n");
    step(100, L"Instalación completada");
}

void uninstall(const std::wstring& game) {
    const std::wstring backup = game + L"\\" + BACKUP_EXE, bink = game + L"\\" + ORIGINAL_BINK;
    if (!exists(backup) || !exists(bink)) throw Error("El mod no esta instalado en esa carpeta");
    write_file(game + L"\\USM.exe", read_file(backup));
    write_file(game + L"\\binkw32.dll", read_file(bink));
    for (const wchar_t* f : {BACKUP_EXE, ORIGINAL_BINK, L"usm_native_input.dll", MARKER, L"usm_mod.ini",
                             L"mask-ab-status.txt", L"city-filter-status.txt",
                             L"sony-ps2-atlas.bgra", L"mask-0dc218aa.bgra", L"mask-6064aa59.bgra"})
        DeleteFileW((game + L"\\" + f).c_str());
    say(L"Mod desinstalado. El juego vuelve a su estado original.\n");
}

}  // namespace

int run_command_line(int argc, wchar_t** argv) {
    AttachConsole(ATTACH_PARENT_PROCESS);
    say(L"Ultimate Spider-Man (PC) - mod: instalador en modo línea de comandos\n\n");
    std::optional<std::wstring> game, iso;
    bool remove = false, spanish_text = true;
    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i];
        if (a == L"--desinstalar") remove = true;
        else if (a == L"--sin-iso" || a == L"--silencioso") {}
        else if (a == L"--texto-original") spanish_text = false;
        else if (a == L"--juego" && i + 1 < argc) game = trim_quotes(argv[++i]);
        else if (a == L"--iso" && i + 1 < argc) iso = trim_quotes(argv[++i]);
        else { say(L"Opcion desconocida: " + a + L"\n"); return 2; }
    }
    try {
        if (!game) {
            auto detected = registry_install_path();
            if (!detected || !exists(*detected + L"\\USM.exe")) throw Error("No se encontro el juego; usa --juego \"carpeta\"");
            game = *detected;
        }
        if (remove) uninstall(*game);
        else install(*game, iso, spanish_text);
    } catch (const std::exception& e) {
        const std::string what = e.what();
        say(L"\nERROR: " + std::wstring(what.begin(), what.end()) + L"\n");
        return 1;
    }
    return 0;
}

#include "installer_gui.hpp"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    const int status = argc > 1 ? run_command_line(argc, argv) : run_gui(instance, show);
    LocalFree(argv);
    return status;
}

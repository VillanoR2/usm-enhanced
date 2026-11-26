#pragma once
// Graphical installer: loading screen, wizard pages (welcome, game, settings,
// confirmation, progress, done) and per-PC presets. Win32 only, no external
// libraries, so the installer stays a single executable.
#include <commctrl.h>
#include <shobjidl.h>
#include <dxgi.h>
#include <memory>
#include <thread>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace gui {

// ------------------------------------------------------------------ look
constexpr COLORREF kSidebar = RGB(21, 24, 33), kSidebarText = RGB(150, 156, 172), kSidebarActive = RGB(255, 255, 255);
constexpr COLORREF kAccent = RGB(226, 54, 54), kAccentBlue = RGB(43, 89, 195);
constexpr COLORREF kPage = RGB(250, 250, 252), kText = RGB(30, 32, 40), kMuted = RGB(96, 102, 118);
constexpr COLORREF kGood = RGB(22, 140, 72), kBad = RGB(200, 40, 40), kPanel = RGB(239, 241, 247);

int dpi = 96;
int S(int value) { return MulDiv(value, dpi, 96); }

HFONT font_body, font_bold, font_title, font_subtitle, font_small, font_big;
HBRUSH brush_page, brush_panel, brush_sidebar;

HFONT make_font(int points, int weight, const wchar_t* face = L"Segoe UI") {
    return CreateFontW(-MulDiv(points, dpi, 72), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
}

// ------------------------------------------------------------------ PC profile
struct Hardware { double ram_gb = 0; unsigned cores = 0; double vram_gb = 0; std::wstring gpu; };

Hardware detect_hardware() {
    Hardware h;
    MEMORYSTATUSEX memory = {};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory)) h.ram_gb = static_cast<double>(memory.ullTotalPhys) / (1024.0 * 1024 * 1024);
    h.cores = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    IDXGIFactory1* factory = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) {
        IDXGIAdapter1* adapter = nullptr;
        for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 desc = {};
            if (SUCCEEDED(adapter->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
                const double vram = static_cast<double>(desc.DedicatedVideoMemory) / (1024.0 * 1024 * 1024);
                if (vram > h.vram_gb) { h.vram_gb = vram; h.gpu = desc.Description; }
            }
            adapter->Release();
        }
        factory->Release();
    }
    return h;
}

struct Preset { const wchar_t* name; const wchar_t* who; Settings values; };
const Preset kPresets[] = {
    {L"Básico", L"PC antiguo o portátil sencillo",      {2.0, 0.3, 4, 30, 100, false, 12, 30}},
    {L"Equilibrado", L"PC de gama media",                {4.0, 0.3, 8, 50, 130, true, 18, 38}},
    {L"Alto", L"PC moderno (recomendado)",              {6.0, 0.3, 16, 60, 150, true, 24, 45}},
    {L"Ultra", L"PC potente con mucha memoria",          {7.0, 0.3, 16, 70, 170, true, 32, 55}},
};

bool same_settings(const Settings& a, const Settings& b) {
    auto close = [](double x, double y) { return x - y < 0.05 && y - x < 0.05; };
    return close(a.draw_distance, b.draw_distance) && close(a.near_plane, b.near_plane) &&
           a.anisotropy == b.anisotropy && a.spawn_min == b.spawn_min && a.spawn_max == b.spawn_max &&
           a.streaming == b.streaming && a.pedestrians == b.pedestrians && a.traffic == b.traffic;
}

int recommended_preset(const Hardware& h) {
    if (h.ram_gb >= 15 && h.cores >= 8 && h.vram_gb >= 4) return 3;
    if (h.ram_gb >= 7.5 && h.cores >= 4 && h.vram_gb >= 1.5) return 2;
    if (h.ram_gb >= 3.5 && h.cores >= 2) return 1;
    return 0;
}

// ------------------------------------------------------------------ state
enum Mode { kInstall, kSettingsOnly, kUninstall };
enum Page { kWelcome, kGame, kSettings, kConfirm, kProgress, kDone, kPageCount };
const wchar_t* const kSteps[kPageCount] = {L"Bienvenida", L"Juego", L"Ajustes", L"Confirmar", L"Instalación", L"Listo"};

struct State {
    HINSTANCE instance = nullptr;
    HWND window = nullptr;
    Page page = kWelcome;
    Mode mode = kInstall;
    std::wstring game;
    bool game_ok = false, installed = false;
    Edition edition;
    std::wstring iso;
    bool spanish = true;
    Settings settings;
    int preset = 2;
    int recommended = 2;
    Hardware hardware;
    bool busy = false, succeeded = false;
    std::wstring error;
    std::wstring status_line;
    int progress = 0;
} state;

constexpr UINT WM_APP_LOG = WM_APP + 1, WM_APP_STEP = WM_APP + 2, WM_APP_DONE = WM_APP + 3;

// ------------------------------------------------------------------ controls
enum Id {
    ID_BACK = 100, ID_NEXT, ID_CANCEL,
    ID_MODE_INSTALL, ID_MODE_SETTINGS, ID_MODE_UNINSTALL,
    ID_GAME_EDIT, ID_GAME_BROWSE, ID_ISO_EDIT, ID_ISO_BROWSE, ID_SPANISH,
    ID_PRESET0, ID_PRESET1, ID_PRESET2, ID_PRESET3,
    ID_STREAMING, ID_SCREEN, ID_RESOLUTION,
    ID_SLIDER0 = 200,  // + index
    ID_PLAY = 300, ID_FOLDER,
};

struct Control { HWND hwnd; Page page; };
std::vector<Control> controls;

HWND add(Page page, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id = 0,
         HFONT font = nullptr, DWORD ex = 0) {
    HWND hwnd = CreateWindowExW(ex, cls, text, WS_CHILD | style, S(x), S(y), S(w), S(h), state.window,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), state.instance, nullptr);
    SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font ? font : font_body), TRUE);
    controls.push_back({hwnd, page});
    return hwnd;
}
constexpr Page kAll = kPageCount;  // shown on every page

HWND back_button, next_button, cancel_button;
HWND mode_install, mode_settings, mode_uninstall, welcome_text;
HWND game_edit, game_status, edition_label, iso_edit, iso_label, iso_browse, iso_help, spanish_box;
HWND preset_buttons[4], recommended_label, streaming_box, help_box, screen_combo, resolution_combo;

// Resolutions the primary monitor supports (largest first), as "WIDTHxHEIGHT".
std::vector<std::wstring> display_modes() {
    std::vector<std::pair<DWORD, DWORD>> modes;
    DEVMODEW mode = {};
    mode.dmSize = sizeof(mode);
    for (DWORD i = 0; EnumDisplaySettingsW(nullptr, i, &mode); ++i) {
        if (mode.dmPelsWidth < 800 || mode.dmPelsHeight < 600 || mode.dmBitsPerPel < 32) continue;
        const std::pair<DWORD, DWORD> size{mode.dmPelsWidth, mode.dmPelsHeight};
        if (std::find(modes.begin(), modes.end(), size) == modes.end()) modes.push_back(size);
    }
    std::sort(modes.begin(), modes.end(), [](const auto& a, const auto& b) {
        return a.first * a.second != b.first * b.second ? a.first * a.second > b.first * b.second : a.first > b.first;
    });
    std::vector<std::wstring> names;
    for (const auto& m : modes) names.push_back(std::to_wstring(m.first) + L"x" + std::to_wstring(m.second));
    return names;
}
HWND confirm_text, progress_bar, progress_stage, log_box;
HWND done_title, done_text, play_button, folder_button;

struct Slider {
    const wchar_t* name; int minimum, maximum, step; double scale;  // value = position * scale
    const wchar_t* unit; const wchar_t* help;
    HWND bar = nullptr, value = nullptr;
};
Slider sliders[] = {
    {L"Distancia de dibujado", 10, 70, 5, 0.1, L"x",
     L"Distancia de dibujado: hasta donde se ven edificios, personajes, coches y objetos. 1.0 es la del juego "
     L"original; 6.0 deja ver la ciudad desde las azoteas. Valores altos piden más a la tarjeta gráfica."},
    {L"Peatones", 10, 40, 1, 1, L"",
     L"Peatones: cuanta gente camina por las calles a la vez. El original tiene 10. Más gente da vida a la "
     L"ciudad pero usa más procesador."},
    {L"Tráfico", 30, 60, 1, 1, L"coches",
     L"Tráfico: coches circulando a la vez. El original tiene 30. Más coches llenan las avenidas pero usan "
     L"más procesador."},
    {L"Aparición mínima", 10, 100, 5, 1, L"m",
     L"Aparición mínima: distancia mínima a la que se crean peatones y coches. Más alta evita que surjan "
     L"delante de Spider-Man."},
    {L"Aparición máxima", 90, 200, 5, 1, L"m",
     L"Aparición máxima: radio hasta el que hay peatones y coches; más allá desaparecen. Más alto mantiene "
     L"las calles llenas a lo lejos."},
    {L"Filtrado de texturas", 0, 4, 1, 1, L"",
     L"Filtrado anisotrópico: nitidez de las texturas del suelo y fachadas vistas en ángulo. 16x es lo más "
     L"nítido y apenas cuesta en tarjetas actuales."},
    {L"Plano cercano", 1, 10, 1, 0.1, L"",
     L"Plano cercano: evita el parpadeo de texturas lejanas. 0.3 es el recomendado; valores mayores pueden "
     L"recortar objetos muy pegados a la cámara."},
};
constexpr int kAniso[] = {0, 2, 4, 8, 16};

int slider_position(int index) {
    const Settings& s = state.settings;
    switch (index) {
    case 0: return static_cast<int>(s.draw_distance * 10 + 0.5);
    case 1: return s.pedestrians;
    case 2: return s.traffic;
    case 3: return s.spawn_min;
    case 4: return s.spawn_max;
    case 5: { int p = 0; while (p < 4 && kAniso[p] < s.anisotropy) ++p; return p; }
    default: return static_cast<int>(s.near_plane * 10 + 0.5);
    }
}

void store_slider(int index, int position) {
    Settings& s = state.settings;
    switch (index) {
    case 0: s.draw_distance = position / 10.0; break;
    case 1: s.pedestrians = position; break;
    case 2: s.traffic = position; break;
    case 3: s.spawn_min = position; if (s.spawn_max < s.spawn_min + 20) s.spawn_max = s.spawn_min + 20; break;
    case 4: s.spawn_max = position; if (s.spawn_min > s.spawn_max - 20) s.spawn_min = s.spawn_max - 20; break;
    case 5: s.anisotropy = kAniso[position]; break;
    default: s.near_plane = position / 10.0; break;
    }
}

std::wstring slider_text(int index) {
    const Settings& s = state.settings;
    wchar_t text[48];
    switch (index) {
    case 0: swprintf_s(text, L"%.1fx", s.draw_distance); break;
    case 1: swprintf_s(text, L"%d", s.pedestrians); break;
    case 2: swprintf_s(text, L"%d coches", s.traffic); break;
    case 3: swprintf_s(text, L"%d m", s.spawn_min); break;
    case 4: swprintf_s(text, L"%d m", s.spawn_max); break;
    case 5: if (s.anisotropy) swprintf_s(text, L"%dx", s.anisotropy); else wcscpy_s(text, L"Original"); break;
    default: swprintf_s(text, L"%.1f", s.near_plane); break;
    }
    return text;
}

void sync_settings_controls() {
    for (int i = 0; i < static_cast<int>(std::size(sliders)); ++i) {
        SendMessageW(sliders[i].bar, TBM_SETPOS, TRUE, slider_position(i));
        SetWindowTextW(sliders[i].value, slider_text(i).c_str());
    }
    SendMessageW(streaming_box, BM_SETCHECK, state.settings.streaming ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(screen_combo, CB_SETCURSEL, static_cast<WPARAM>(std::clamp(state.settings.screen_mode, 0, 2)), 0);
    LRESULT found = SendMessageW(resolution_combo, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
                                 reinterpret_cast<LPARAM>(state.settings.resolution.c_str()));
    if (found == CB_ERR && state.settings.resolution != L"auto" && !state.settings.resolution.empty())
        found = SendMessageW(resolution_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(state.settings.resolution.c_str()));
    SendMessageW(resolution_combo, CB_SETCURSEL, found == CB_ERR ? 0 : static_cast<WPARAM>(found), 0);
    for (int i = 0; i < 4; ++i)
        SendMessageW(preset_buttons[i], BM_SETCHECK, i == state.preset ? BST_CHECKED : BST_UNCHECKED, 0);
}

// ------------------------------------------------------------------ game folder checks
void check_game() {
    wchar_t text[MAX_PATH] = {};
    GetWindowTextW(game_edit, text, MAX_PATH);
    state.game = trim_quotes(text);
    state.game_ok = !state.game.empty() && exists(state.game + L"\\USM.exe") && exists(state.game + L"\\binkw32.dll");
    state.installed = state.game_ok && exists(state.game + L"\\" + BACKUP_EXE);
    if (!state.game_ok) state.status_line = L"No se encuentra USM.exe en esa carpeta.";
    else if (state.installed) state.status_line = L"Juego encontrado. El mod ya está instalado: se actualizará.";
    else state.status_line = L"Juego encontrado. Se guardarán copias de los archivos originales.";
    SetWindowTextW(game_status, state.status_line.c_str());
    // Edition: which voices and texts this copy really has.
    state.edition = state.game_ok ? scan_edition(state.game) : Edition{};
    std::wstring edition_text;
    if (state.game_ok) {
        edition_text = state.edition.label + L".";
        if (!state.edition.spanish_text) edition_text += L" No incluye textos en español: se mantendrán los originales.";
        if (!state.edition.english_voices) edition_text += L" No incluye voces en inglés: se mantendrán las originales.";
    }
    SetWindowTextW(edition_label, edition_text.c_str());
    EnableWindow(spanish_box, state.edition.spanish_text);
    if (!state.edition.spanish_text) SendMessageW(spanish_box, BM_SETCHECK, BST_UNCHECKED, 0);
    SetWindowTextW(spanish_box, state.edition.english_voices ? L"Textos en español (las voces quedan en inglés)"
                                                              : L"Textos en español");
    InvalidateRect(game_status, nullptr, TRUE);
    EnableWindow(mode_settings, state.installed);
    EnableWindow(mode_uninstall, state.installed);
    if (!state.installed && state.mode != kInstall && state.page == kWelcome) {
        state.mode = kInstall;
        SendMessageW(mode_install, BM_SETCHECK, BST_CHECKED, 0);
        SendMessageW(mode_settings, BM_SETCHECK, BST_UNCHECKED, 0);
        SendMessageW(mode_uninstall, BM_SETCHECK, BST_UNCHECKED, 0);
    }
}

std::wstring pick(bool folder) {
    std::wstring result;
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
        return result;
    DWORD options = 0;
    dialog->GetOptions(&options);
    if (folder) {
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        dialog->SetTitle(L"Carpeta de Ultimate Spider-Man (donde está USM.exe)");
    } else {
        const COMDLG_FILTERSPEC filters[] = {{L"Imagen de disco (*.iso)", L"*.iso"}, {L"Todos los archivos", L"*.*"}};
        dialog->SetFileTypes(2, filters);
        dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
        dialog->SetTitle(L"ISO de Ultimate Spider-Man para PS2 (opcional)");
    }
    if (SUCCEEDED(dialog->Show(state.window))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) { result = path; CoTaskMemFree(path); }
            item->Release();
        }
    }
    dialog->Release();
    return result;
}

// ------------------------------------------------------------------ pages
std::wstring confirm_summary() {
    std::wstring t;
    if (state.mode == kUninstall) {
        t = L"Se quitará el mod de:\r\n    " + state.game + L"\r\n\r\n"
            L"  -  USM.exe y binkw32.dll vuelven a ser los originales (desde las copias guardadas).\r\n"
            L"  -  Se borran los archivos del mod y usm_mod.ini.\r\n"
            L"  -  Tus partidas guardadas no se tocan.";
        return t;
    }
    const Settings& s = state.settings;
    wchar_t line[256];
    if (state.mode == kSettingsOnly) {
        t = L"Solo se guardarán los ajustes en usm_mod.ini (el mod ya está instalado).\r\n\r\n";
    } else {
        t = L"Se instalará el mod en:\r\n    " + state.game + L"\r\n\r\n";
        t += state.installed ? L"  -  Actualización: se conservan tus copias originales.\r\n"
                             : L"  -  Copias de seguridad: USM.exe.mod-backup y binkw32_original.dll.\r\n";
        t += state.edition.english_voices ? L"  -  Voces en inglés" : L"  -  Voces originales de tu edición";
        t += state.spanish && state.edition.spanish_text ? L" y textos en español.\r\n" : L", textos originales.\r\n";
        t += L"  -  Mandos DualSense, DualShock 4 y Xbox con sus iconos; iconos de teclado y raton.\r\n";
        t += state.iso.empty() ? L"  -  Sombras y máscaras estilo PS2 generadas por el mod (sin ISO).\r\n"
                               : L"  -  Sombras y máscaras originales de PS2 extraidas de tu ISO.\r\n";
        t += L"  -  USM.exe podra usar hasta 4 GB de memoria.\r\n\r\n";
    }
    swprintf_s(line, L"Ajustes (perfil %s):\r\n", state.preset >= 0 ? kPresets[state.preset].name : L"personalizado");
    t += line;
    swprintf_s(line, L"    Distancia de dibujado %.1fx  |  zonas cargadas %s\r\n", s.draw_distance,
               s.streaming ? L"32" : L"8 (original)");
    t += line;
    swprintf_s(line, L"    %d peatones, %d coches  |  aparecen entre %d y %d m\r\n", s.pedestrians, s.traffic,
               s.spawn_min, s.spawn_max);
    t += line;
    swprintf_s(line, L"    Filtrado %s  |  plano cercano %.1f\r\n", s.anisotropy ? (std::to_wstring(s.anisotropy) + L"x").c_str()
               : L"original", s.near_plane);
    t += line;
    static const wchar_t* const screens[] = {L"Ventana", L"Ventana sin bordes", L"Pantalla completa"};
    swprintf_s(line, L"    Pantalla: %s  |  resolución %s", screens[std::clamp(s.screen_mode, 0, 2)],
               s.resolution == L"auto" || s.resolution.empty() ? L"automática" : s.resolution.c_str());
    t += line;
    return t;
}

Page next_page(Page p) {
    if (p == kGame && state.mode == kUninstall) return kConfirm;
    return static_cast<Page>(p + 1);
}
Page previous_page(Page p) {
    if (p == kConfirm && state.mode == kUninstall) return kGame;
    return static_cast<Page>(p - 1);
}

const wchar_t* page_title() {
    switch (state.page) {
    case kWelcome: return L"Mod de Ultimate Spider-Man para PC";
    case kGame: return L"Dónde está el juego";
    case kSettings: return L"Ajustes para tu PC";
    case kConfirm: return state.mode == kUninstall ? L"Confirmar desinstalación" : L"Confirmar";
    case kProgress: return state.mode == kUninstall ? L"Desinstalando" : L"Instalando";
    default: return state.succeeded ? L"Listo" : L"No se pudo completar";
    }
}
const wchar_t* page_subtitle() {
    switch (state.page) {
    case kWelcome: return L"Mejoras para la version de PC con el estilo de PS2. Elige que quieres hacer.";
    case kGame: return L"Indica la carpeta del juego y, si quieres, tu ISO de PS2.";
    case kSettings: return L"Elige un perfil o ajusta cada opción. Puedes cambiarlo después sin reinstalar.";
    case kConfirm: return L"Revisa lo que se va a hacer antes de continuar.";
    case kProgress: return L"No cierres esta ventana.";
    default: return state.succeeded ? L"Todo hecho." : L"No se ha modificado nada que no se pueda recuperar.";
    }
}

void show_page(Page page) {
    state.page = page;
    for (const Control& c : controls) ShowWindow(c.hwnd, (c.page == page || c.page == kAll) ? SW_SHOW : SW_HIDE);
    const bool install_like = state.mode == kInstall;
    ShowWindow(iso_label, page == kGame && install_like ? SW_SHOW : SW_HIDE);
    ShowWindow(iso_edit, page == kGame && install_like ? SW_SHOW : SW_HIDE);
    ShowWindow(iso_browse, page == kGame && install_like ? SW_SHOW : SW_HIDE);
    ShowWindow(iso_help, page == kGame && install_like ? SW_SHOW : SW_HIDE);
    ShowWindow(spanish_box, page == kGame && install_like ? SW_SHOW : SW_HIDE);
    if (page == kConfirm) SetWindowTextW(confirm_text, confirm_summary().c_str());
    if (page == kSettings) sync_settings_controls();
    if (page == kGame) check_game();

    ShowWindow(back_button, page > kWelcome && page < kProgress ? SW_SHOW : SW_HIDE);
    ShowWindow(cancel_button, page < kProgress ? SW_SHOW : SW_HIDE);
    const wchar_t* next = L"Siguiente  >";
    if (page == kConfirm) next = state.mode == kUninstall ? L"Desinstalar" : state.mode == kSettingsOnly ? L"Guardar ajustes" : L"Instalar";
    if (page == kDone) next = L"Cerrar";
    SetWindowTextW(next_button, next);
    ShowWindow(next_button, page == kProgress ? SW_HIDE : SW_SHOW);
    EnableWindow(next_button, page != kGame || state.game_ok);
    ShowWindow(play_button, page == kDone && state.succeeded && state.mode != kUninstall ? SW_SHOW : SW_HIDE);
    ShowWindow(folder_button, page == kDone && state.succeeded ? SW_SHOW : SW_HIDE);
    SetWindowPos(folder_button, nullptr, S(state.mode != kUninstall ? 436 : 250), S(360), 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER);
    InvalidateRect(state.window, nullptr, TRUE);
}

// ------------------------------------------------------------------ work thread
void gui_say(const std::wstring& text) {
    PostMessageW(state.window, WM_APP_LOG, 0, reinterpret_cast<LPARAM>(new std::wstring(text)));
}
void gui_step(int percent, const std::wstring& stage) {
    PostMessageW(state.window, WM_APP_STEP, static_cast<WPARAM>(percent), reinterpret_cast<LPARAM>(new std::wstring(stage)));
    Sleep(320);  // runs on the work thread: lets each step be read on screen
}

void start_work() {
    state.busy = true;
    say = gui_say;
    step = gui_step;
    const Mode mode = state.mode;
    const std::wstring game = state.game, iso = state.iso;
    const bool spanish = state.spanish;
    const Settings settings = state.settings;
    std::thread([mode, game, iso, spanish, settings] {
        std::wstring* error = nullptr;
        try {
            if (mode == kUninstall) {
                step(30, L"Restaurando los archivos originales");
                uninstall(game);
                step(100, L"Desinstalación completada");
            } else if (mode == kSettingsOnly) {
                step(40, L"Guardando los ajustes");
                apply_settings(game + L"\\usm_mod.ini", settings);
                say(L"Ajustes guardados en usm_mod.ini.\n");
                step(100, L"Ajustes guardados");
            } else {
                install(game, iso.empty() ? std::nullopt : std::optional<std::wstring>(iso), spanish, &settings);
            }
        } catch (const std::exception& e) {
            const std::string what = e.what();
            error = new std::wstring(what.begin(), what.end());
        }
        PostMessageW(state.window, WM_APP_DONE, error ? 0 : 1, reinterpret_cast<LPARAM>(error));
    }).detach();
}

void append_log(const std::wstring& text) {
    std::wstring line;
    for (wchar_t c : text) { if (c == L'\n') line += L"\r\n"; else line += c; }
    const int length = GetWindowTextLengthW(log_box);
    SendMessageW(log_box, EM_SETSEL, length, length);
    SendMessageW(log_box, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(line.c_str()));
}

// ------------------------------------------------------------------ painting
void paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT client;
    GetClientRect(hwnd, &client);
    FillRect(dc, &client, brush_page);
    RECT side = {0, 0, S(220), client.bottom};
    FillRect(dc, &side, brush_sidebar);
    SetBkMode(dc, TRANSPARENT);

    // Brand block.
    RECT stripe = {S(24), S(28), S(30), S(76)};
    HBRUSH accent = CreateSolidBrush(kAccent);
    FillRect(dc, &stripe, accent);
    SelectObject(dc, font_big);
    SetTextColor(dc, kSidebarActive);
    RECT brand = {S(40), S(24), S(215), S(54)};
    DrawTextW(dc, L"USM Mod", -1, &brand, DT_LEFT | DT_SINGLELINE);
    SelectObject(dc, font_small);
    SetTextColor(dc, kSidebarText);
    RECT brand2 = {S(40), S(56), S(215), S(80)};
    DrawTextW(dc, L"Ultimate Spider-Man  PC", -1, &brand2, DT_LEFT | DT_SINGLELINE);

    // Steps.
    for (int i = 0, row = 0; i < kPageCount; ++i) {
        if (i == kSettings && state.mode == kUninstall) continue;
        const int y = S(120 + row++ * 46);
        const bool active = i == state.page, done = i < state.page;
        HBRUSH dot = CreateSolidBrush(active ? kAccent : done ? kAccentBlue : RGB(55, 60, 75));
        HGDIOBJ old_brush = SelectObject(dc, dot);
        HGDIOBJ old_pen = SelectObject(dc, GetStockObject(NULL_PEN));
        Ellipse(dc, S(28), y, S(28) + S(24), y + S(24));
        SelectObject(dc, old_pen);
        SelectObject(dc, old_brush);
        DeleteObject(dot);
        wchar_t number[4];
        swprintf_s(number, done ? L"\x2713" : L"%d", row);
        SelectObject(dc, font_bold);
        SetTextColor(dc, RGB(255, 255, 255));
        RECT n = {S(28), y, S(52), y + S(24)};
        DrawTextW(dc, number, -1, &n, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, active ? font_bold : font_body);
        SetTextColor(dc, active ? kSidebarActive : kSidebarText);
        RECT label = {S(64), y, S(215), y + S(24)};
        const wchar_t* name = i == kProgress && state.mode == kUninstall ? L"Desinstalación"
                            : i == kProgress && state.mode == kSettingsOnly ? L"Guardado" : kSteps[i];
        DrawTextW(dc, name, -1, &label, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    SelectObject(dc, font_small);
    SetTextColor(dc, RGB(110, 116, 132));
    RECT credit = {S(24), client.bottom - S(54), S(215), client.bottom - S(12)};
    DrawTextW(dc, L"No incluye archivos del juego.\nIconos: Kenney (CC0).", -1, &credit, DT_LEFT | DT_WORDBREAK);

    // Page header.
    SelectObject(dc, font_title);
    SetTextColor(dc, kText);
    RECT title = {S(250), S(22), client.right - S(24), S(60)};
    DrawTextW(dc, page_title(), -1, &title, DT_LEFT | DT_SINGLELINE);
    SelectObject(dc, font_subtitle);
    SetTextColor(dc, kMuted);
    RECT subtitle = {S(250), S(60), client.right - S(24), S(86)};
    DrawTextW(dc, page_subtitle(), -1, &subtitle, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    RECT line = {S(250), S(94), client.right - S(24), S(95)};
    HBRUSH rule = CreateSolidBrush(RGB(222, 225, 233));
    FillRect(dc, &line, rule);
    RECT bottom_line = {S(220), client.bottom - S(64), client.right, client.bottom - S(63)};
    FillRect(dc, &bottom_line, rule);
    DeleteObject(rule);
    DeleteObject(accent);
    EndPaint(hwnd, &ps);
}

// ------------------------------------------------------------------ building
void build() {
    const int x = 250, w = 590;
    // Navigation.
    cancel_button = add(kAll, L"BUTTON", L"Cancelar", WS_TABSTOP | BS_PUSHBUTTON, 250, 512, 110, 34, ID_CANCEL);
    back_button = add(kAll, L"BUTTON", L"<  Atrás", WS_TABSTOP | BS_PUSHBUTTON, 600, 512, 110, 34, ID_BACK);
    next_button = add(kAll, L"BUTTON", L"Siguiente  >", WS_TABSTOP | BS_DEFPUSHBUTTON, 720, 512, 120, 34, ID_NEXT, font_bold);

    // Welcome.
    welcome_text = add(kWelcome, L"STATIC",
        L"Este instalador mejora tu Ultimate Spider-Man de PC:\r\n\r\n"
        L"  -  Voces en inglés con textos en español.\r\n"
        L"  -  Mandos DualSense, DualShock 4 y Xbox, con los iconos de cada mando en los avisos.\r\n"
        L"  -  Iconos de teclado y raton en lugar de texto.\r\n"
        L"  -  Ciudad más lejana y detallada, con más peatones y tráfico (ajustable).\r\n"
        L"  -  Sombras y máscaras estilo PS2, juego en ventana y correcciones de estabilidad.\r\n\r\n"
        L"Se guardan copias de los archivos originales y puedes desinstalarlo en cualquier momento.",
        SS_LEFT, x, 110, w, 190);
    add(kWelcome, L"STATIC", L"¿Qué quieres hacer?", SS_LEFT, x, 312, w, 22, 0, font_bold);
    mode_install = add(kWelcome, L"BUTTON", L"Instalar o actualizar el mod", WS_TABSTOP | BS_AUTORADIOBUTTON | WS_GROUP,
                       x + 10, 340, w - 10, 26, ID_MODE_INSTALL);
    mode_settings = add(kWelcome, L"BUTTON", L"Cambiar solo los ajustes (el mod ya está instalado)",
                        WS_TABSTOP | BS_AUTORADIOBUTTON, x + 10, 370, w - 10, 26, ID_MODE_SETTINGS);
    mode_uninstall = add(kWelcome, L"BUTTON", L"Desinstalar el mod y dejar el juego como estaba",
                         WS_TABSTOP | BS_AUTORADIOBUTTON, x + 10, 400, w - 10, 26, ID_MODE_UNINSTALL);
    SendMessageW(mode_install, BM_SETCHECK, BST_CHECKED, 0);

    // Game.
    add(kGame, L"STATIC", L"Carpeta de Ultimate Spider-Man (donde está USM.exe)", SS_LEFT, x, 112, w, 22, 0, font_bold);
    game_edit = add(kGame, L"EDIT", state.game.c_str(), WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, x, 138, w - 120, 28, ID_GAME_EDIT);
    add(kGame, L"BUTTON", L"Examinar...", WS_TABSTOP | BS_PUSHBUTTON, x + w - 110, 137, 110, 30, ID_GAME_BROWSE);
    game_status = add(kGame, L"STATIC", L"", SS_LEFT, x, 172, w, 22);
    edition_label = add(kGame, L"STATIC", L"", SS_LEFT, x, 194, w, 38, 0, font_small);
    iso_label = add(kGame, L"STATIC", L"ISO de Ultimate Spider-Man para PS2 (opcional)", SS_LEFT, x, 240, w, 22, 0, font_bold);
    iso_edit = add(kGame, L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, x, 266, w - 120, 28, ID_ISO_EDIT);
    iso_browse = add(kGame, L"BUTTON", L"Examinar...", WS_TABSTOP | BS_PUSHBUTTON, x + w - 110, 265, 110, 30, ID_ISO_BROWSE);
    iso_help = add(kGame, L"STATIC",
        L"Solo si quieres las texturas originales de PS2 para sombras y máscaras. Sin ISO, el mod usa unas "
        L"propias muy parecidas. La ISO se lee una vez y no se modifica.", SS_LEFT, x, 300, w, 44, 0, font_small);
    spanish_box = add(kGame, L"BUTTON", L"Textos en español (las voces quedan en inglés)",
                      WS_TABSTOP | BS_AUTOCHECKBOX, x, 362, w, 26, ID_SPANISH);
    SendMessageW(spanish_box, BM_SETCHECK, BST_CHECKED, 0);

    // Settings: presets.
    add(kSettings, L"STATIC", L"Perfil", SS_LEFT, x, 106, 60, 22, 0, font_bold);
    for (int i = 0; i < 4; ++i)
        preset_buttons[i] = add(kSettings, L"BUTTON", kPresets[i].name, WS_TABSTOP | BS_AUTORADIOBUTTON | BS_PUSHLIKE |
                                (i == 0 ? WS_GROUP : 0), x + 60 + i * 132, 102, 126, 30, ID_PRESET0 + i);
    recommended_label = add(kSettings, L"STATIC", L"", SS_LEFT, x, 136, w, 20, 0, font_small);
    // Settings: sliders in two columns.
    for (int i = 0; i < static_cast<int>(std::size(sliders)); ++i) {
        const int column = i % 2, row = i / 2;
        const int cx = x + column * 300, cy = 162 + row * 52;
        add(kSettings, L"STATIC", sliders[i].name, SS_LEFT, cx, cy, 190, 20);
        sliders[i].value = add(kSettings, L"STATIC", L"", SS_RIGHT, cx + 190, cy, 90, 20, 0, font_bold);
        sliders[i].bar = add(kSettings, TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_HORZ | TBS_NOTICKS, cx - 4, cy + 22, 290, 28,
                             ID_SLIDER0 + i);
        SendMessageW(sliders[i].bar, TBM_SETRANGE, TRUE, MAKELPARAM(sliders[i].minimum, sliders[i].maximum));
        SendMessageW(sliders[i].bar, TBM_SETPAGESIZE, 0, sliders[i].step);
        SendMessageW(sliders[i].bar, TBM_SETLINESIZE, 0, sliders[i].step);
    }
    streaming_box = add(kSettings, L"BUTTON", L"Más zonas de la ciudad cargadas (32)",
                        WS_TABSTOP | BS_AUTOCHECKBOX, x + 300, 162 + 3 * 52 + 20, 290, 26, ID_STREAMING);
    // Screen mode and resolution.
    add(kSettings, L"STATIC", L"Pantalla", SS_LEFT, x, 376, 80, 22);
    screen_combo = add(kSettings, WC_COMBOBOXW, L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, x + 80, 372, 200, 200,
                       ID_SCREEN);
    for (const wchar_t* mode : {L"Ventana", L"Ventana sin bordes", L"Pantalla completa"})
        SendMessageW(screen_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(mode));
    add(kSettings, L"STATIC", L"Resolución", SS_LEFT, x + 300, 376, 90, 22);
    resolution_combo = add(kSettings, WC_COMBOBOXW, L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, x + 390, 372, 200, 300,
                           ID_RESOLUTION);
    SendMessageW(resolution_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Automática"));
    for (const std::wstring& mode : display_modes())
        SendMessageW(resolution_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(mode.c_str()));
    help_box = add(kSettings, L"STATIC",
        L"Mueve un control para ver qué hace. Los perfiles son un punto de partida: el recomendado para tu "
        L"PC está marcado.", SS_LEFT, x + 10, 414, w - 20, 74, 0, font_small);

    // Confirm.
    confirm_text = add(kConfirm, L"STATIC", L"", SS_LEFT, x, 112, w, 360);

    // Progress.
    progress_stage = add(kProgress, L"STATIC", L"", SS_LEFT, x, 112, w, 24, 0, font_bold);
    progress_bar = add(kProgress, PROGRESS_CLASSW, L"", PBS_SMOOTH, x, 142, w, 18);
    SendMessageW(progress_bar, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    log_box = add(kProgress, L"EDIT", L"", WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_BORDER,
                  x, 176, w, 300, 0, font_small);

    // Done.
    done_text = add(kDone, L"STATIC", L"", SS_LEFT, x, 112, w, 230);
    play_button = add(kDone, L"BUTTON", L"Jugar ahora", WS_TABSTOP | BS_PUSHBUTTON, x, 360, 170, 38, ID_PLAY, font_bold);
    folder_button = add(kDone, L"BUTTON", L"Abrir carpeta del juego", WS_TABSTOP | BS_PUSHBUTTON, x + 186, 360, 210, 38,
                        ID_FOLDER);
}

void set_help(const wchar_t* text) { SetWindowTextW(help_box, text); }

void choose_preset(int index) {
    state.preset = index;
    // Profiles set performance options; the screen choice stays as it is.
    const int screen_mode = state.settings.screen_mode;
    const std::wstring resolution = state.settings.resolution;
    state.settings = kPresets[index].values;
    state.settings.screen_mode = screen_mode;
    state.settings.resolution = resolution;
    sync_settings_controls();
    std::wstring help = std::wstring(L"Perfil ") + kPresets[index].name + L": " + kPresets[index].who + L".";
    if (index == state.recommended) help += L" Es el recomendado para tu PC.";
    set_help(help.c_str());
}

void finish(bool ok, std::wstring* error) {
    state.busy = false;
    state.succeeded = ok;
    say = console_say;
    step = console_step;
    std::wstring text;
    if (ok && state.mode == kUninstall) {
        text = L"El mod se ha desinstalado y el juego vuelve a estar como antes.\r\n\r\nTus partidas guardadas siguen intactas.";
    } else if (ok && state.mode == kSettingsOnly) {
        text = L"Los ajustes se han guardado. Se aplican la próxima vez que abras el juego.\r\n\r\n"
               L"Puedes volver a cambiarlos cuando quieras abriendo este instalador.";
    } else if (ok) {
        text = L"El mod está instalado. Abre el juego como siempre: los cambios se aplican solos.\r\n\r\n"
               L"Consejos:\r\n"
               L"  -  Para cambiar distancia, peatones o tráfico, abre de nuevo este instalador y elige "
               L"\"Cambiar solo los ajustes\".\r\n"
               L"  -  F6 activa o desactiva el filtro de fachadas de la ciudad.\r\n"
               L"  -  Para quitar el mod, abre el instalador y elige \"Desinstalar\".";
    } else {
        text = L"Ha ocurrido un problema:\r\n\r\n    " + (error ? *error : std::wstring(L"error desconocido")) +
               L"\r\n\r\nSi el juego está en Archivos de programa, ejecuta el instalador como administrador. "
               L"Los archivos originales siguen guardados, puedes volver a intentarlo.";
    }
    delete error;
    SetWindowTextW(done_text, text.c_str());
    show_page(kDone);
}

LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_PAINT: paint(hwnd); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wparam);
        HWND control = reinterpret_cast<HWND>(lparam);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, control == game_status ? (state.game_ok ? kGood : kBad)
                         : control == help_box || control == iso_help || control == recommended_label || control == edition_label
                             ? kMuted : kText);
        if (control == help_box) { SetBkColor(dc, kPanel); return reinterpret_cast<LRESULT>(brush_panel); }
        SetBkColor(dc, kPage);
        return reinterpret_cast<LRESULT>(brush_page);
    }
    case WM_HSCROLL: {
        HWND bar = reinterpret_cast<HWND>(lparam);
        for (int i = 0; i < static_cast<int>(std::size(sliders)); ++i) {
            if (sliders[i].bar != bar) continue;
            store_slider(i, static_cast<int>(SendMessageW(bar, TBM_GETPOS, 0, 0)));
            state.preset = -1;
            sync_settings_controls();
            set_help(sliders[i].help);
        }
        return 0;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wparam);
        if (id == ID_CANCEL) { if (!state.busy) DestroyWindow(hwnd); return 0; }
        if (id == ID_MODE_INSTALL) state.mode = kInstall;
        if (id == ID_MODE_SETTINGS) state.mode = kSettingsOnly;
        if (id == ID_MODE_UNINSTALL) state.mode = kUninstall;
        if (id >= ID_MODE_INSTALL && id <= ID_MODE_UNINSTALL) InvalidateRect(hwnd, nullptr, TRUE);
        if (id == ID_GAME_EDIT && HIWORD(wparam) == EN_CHANGE) { check_game(); EnableWindow(next_button, state.game_ok); }
        if (id == ID_GAME_BROWSE) {
            const std::wstring path = pick(true);
            if (!path.empty()) SetWindowTextW(game_edit, path.c_str());
        }
        if (id == ID_ISO_BROWSE) {
            const std::wstring path = pick(false);
            if (!path.empty()) SetWindowTextW(iso_edit, path.c_str());
        }
        if (id >= ID_PRESET0 && id <= ID_PRESET3) choose_preset(id - ID_PRESET0);
        if (id == ID_SCREEN && HIWORD(wparam) == CBN_SELCHANGE) {
            state.settings.screen_mode = static_cast<int>(SendMessageW(screen_combo, CB_GETCURSEL, 0, 0));
            static const wchar_t* const help[] = {
                L"Ventana: el juego se abre en una ventana con bordes, centrada y ajustada a tu pantalla. "
                L"Cómodo para cambiar a otras aplicaciones.",
                L"Ventana sin bordes: ocupa toda la pantalla como si fuera completa, pero cambias de "
                L"aplicación al instante y sin parpadeos. Recomendado.",
                L"Pantalla completa: el modo exclusivo original del juego. Usa una resolución de la lista; "
                L"cambiar de aplicación puede tardar."};
            set_help(help[std::clamp(state.settings.screen_mode, 0, 2)]);
        }
        if (id == ID_RESOLUTION && HIWORD(wparam) == CBN_SELCHANGE) {
            const LRESULT index = SendMessageW(resolution_combo, CB_GETCURSEL, 0, 0);
            wchar_t text[24] = {};
            if (index > 0) SendMessageW(resolution_combo, CB_GETLBTEXT, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(text));
            state.settings.resolution = index > 0 ? text : L"auto";
            set_help(L"Resolución: tamaño de la imagen que dibuja el juego. Automática usa 1280x720 en ventana y la "
                     L"de tu monitor sin bordes. Más resolución da una imagen más nítida y pide más a la gráfica.");
        }
        if (id == ID_STREAMING) {
            state.settings.streaming = SendMessageW(streaming_box, BM_GETCHECK, 0, 0) == BST_CHECKED;
            state.preset = -1;
            sync_settings_controls();
            set_help(L"Zonas cargadas: cuantas partes de la ciudad se mantienen en memoria. 32 permite ver edificios "
                     L"lejanos y que el tráfico no desaparezca; 8 es lo del juego original y usa menos memoria.");
        }
        if (id == ID_PLAY) {
            ShellExecuteW(hwnd, L"open", (state.game + L"\\USM.exe").c_str(), nullptr, state.game.c_str(), SW_SHOWNORMAL);
            DestroyWindow(hwnd);
        }
        if (id == ID_FOLDER) ShellExecuteW(hwnd, L"open", state.game.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        if (id == ID_BACK && state.page > kWelcome) show_page(previous_page(state.page));
        if (id == ID_NEXT) {
            if (state.page == kDone) { DestroyWindow(hwnd); return 0; }
            if (state.page == kGame) {
                check_game();
                if (!state.game_ok) return 0;
                if (state.mode != kInstall && !state.installed) {
                    MessageBoxW(hwnd, L"El mod no está instalado en esa carpeta. Elige otra carpeta o vuelve atras y "
                                L"selecciona \"Instalar o actualizar el mod\".", L"Mod no instalado", MB_ICONINFORMATION);
                    return 0;
                }
                if ((state.mode == kSettingsOnly || state.installed) && state.preset == state.recommended &&
                    exists(state.game + L"\\usm_mod.ini")) {
                    // Start from the values already in use.
                    state.settings = read_settings(state.game + L"\\usm_mod.ini");
                    state.preset = -1;
                    for (int i = 0; i < 4; ++i)
                        if (same_settings(kPresets[i].values, state.settings)) state.preset = i;
                }
                wchar_t iso[MAX_PATH] = {};
                GetWindowTextW(iso_edit, iso, MAX_PATH);
                state.iso = trim_quotes(iso);
                state.spanish = SendMessageW(spanish_box, BM_GETCHECK, 0, 0) == BST_CHECKED;
                if (!state.iso.empty() && !exists(state.iso)) {
                    MessageBoxW(hwnd, L"No se encuentra el archivo ISO indicado. Corrige la ruta o déjala vacia.",
                                L"ISO de PS2", MB_ICONWARNING);
                    return 0;
                }
            }
            if (state.page == kConfirm) {
                show_page(kProgress);
                SendMessageW(progress_bar, PBM_SETPOS, 0, 0);
                SetWindowTextW(log_box, L"");
                start_work();
                return 0;
            }
            show_page(next_page(state.page));
        }
        return 0;
    }
    case WM_APP_LOG: {
        std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lparam));
        append_log(*text);
        return 0;
    }
    case WM_APP_STEP: {
        std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lparam));
        SendMessageW(progress_bar, PBM_SETPOS, wparam, 0);
        SetWindowTextW(progress_stage, (*text + L"...").c_str());
        append_log(L"> " + *text + L"\n");
        return 0;
    }
    case WM_APP_DONE: finish(wparam != 0, reinterpret_cast<std::wstring*>(lparam)); return 0;
    case WM_CLOSE: if (!state.busy) DestroyWindow(hwnd); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    default: return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}

// ------------------------------------------------------------------ loading screen
struct Splash { HWND hwnd; int tick; std::wstring status; } splash;

LRESULT CALLBACK splash_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_TIMER) { ++splash.tick; InvalidateRect(hwnd, nullptr, FALSE); return 0; }
    if (message == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC window_dc = BeginPaint(hwnd, &ps);
        RECT r;
        GetClientRect(hwnd, &r);
        HDC dc = CreateCompatibleDC(window_dc);
        HBITMAP bitmap = CreateCompatibleBitmap(window_dc, r.right, r.bottom);
        HGDIOBJ old = SelectObject(dc, bitmap);
        FillRect(dc, &r, brush_sidebar);
        RECT stripe = {S(36), S(40), S(44), S(98)};
        HBRUSH accent = CreateSolidBrush(kAccent);
        FillRect(dc, &stripe, accent);
        SetBkMode(dc, TRANSPARENT);
        SelectObject(dc, font_title);
        SetTextColor(dc, RGB(255, 255, 255));
        RECT title = {S(58), S(36), r.right - S(30), S(74)};
        DrawTextW(dc, L"Ultimate Spider-Man", -1, &title, DT_LEFT | DT_SINGLELINE);
        SelectObject(dc, font_subtitle);
        SetTextColor(dc, kSidebarText);
        RECT subtitle = {S(58), S(74), r.right - S(30), S(100)};
        DrawTextW(dc, L"Mod para PC  -  instalador", -1, &subtitle, DT_LEFT | DT_SINGLELINE);
        // Indeterminate bar.
        RECT track = {S(36), S(150), r.right - S(36), S(156)};
        HBRUSH track_brush = CreateSolidBrush(RGB(45, 50, 64));
        FillRect(dc, &track, track_brush);
        const int width = track.right - track.left, block = width / 3;
        const int offset = (splash.tick * S(9)) % (width + block) - block;
        RECT moving = {track.left + std::max(0, offset), track.top, track.left + std::min(width, offset + block), track.bottom};
        if (moving.right > moving.left) FillRect(dc, &moving, accent);
        SelectObject(dc, font_small);
        SetTextColor(dc, kSidebarText);
        RECT status = {S(36), S(166), r.right - S(36), S(190)};
        DrawTextW(dc, splash.status.c_str(), -1, &status, DT_LEFT | DT_SINGLELINE);
        BitBlt(window_dc, 0, 0, r.right, r.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, old);
        DeleteObject(bitmap);
        DeleteDC(dc);
        DeleteObject(track_brush);
        DeleteObject(accent);
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

void pump_for(DWORD milliseconds) {
    const ULONGLONG end = GetTickCount64() + milliseconds;
    MSG msg;
    while (GetTickCount64() < end) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        Sleep(10);
    }
}

void splash_status(const wchar_t* text, DWORD hold) {
    splash.status = text;
    InvalidateRect(splash.hwnd, nullptr, FALSE);
    pump_for(hold);
}

int run(HINSTANCE instance, int show) {
    SetProcessDPIAware();
    HDC screen = GetDC(nullptr);
    dpi = GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(nullptr, screen);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_BAR_CLASSES | ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    state.instance = instance;
    font_body = make_font(10, FW_NORMAL);
    font_bold = make_font(10, FW_SEMIBOLD);
    font_small = make_font(9, FW_NORMAL);
    font_title = make_font(18, FW_SEMIBOLD, L"Segoe UI Semibold");
    font_subtitle = make_font(10, FW_NORMAL);
    font_big = make_font(16, FW_BOLD);
    brush_page = CreateSolidBrush(kPage);
    brush_panel = CreateSolidBrush(kPanel);
    brush_sidebar = CreateSolidBrush(kSidebar);
    HICON icon = LoadIconW(nullptr, IDI_APPLICATION);

    // Loading screen while the game and the PC are examined.
    WNDCLASSW splash_class = {};
    splash_class.lpfnWndProc = splash_proc;
    splash_class.hInstance = instance;
    splash_class.hCursor = LoadCursorW(nullptr, IDC_APPSTARTING);
    splash_class.lpszClassName = L"USMModSplash";
    RegisterClassW(&splash_class);
    const int sw = S(480), sh = S(220);
    splash.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"USMModSplash", L"", WS_POPUP,
                                  (GetSystemMetrics(SM_CXSCREEN) - sw) / 2, (GetSystemMetrics(SM_CYSCREEN) - sh) / 2,
                                  sw, sh, nullptr, nullptr, instance, nullptr);
    ShowWindow(splash.hwnd, SW_SHOW);
    SetTimer(splash.hwnd, 1, 16, nullptr);
    splash_status(L"Preparando el instalador...", 400);
    splash_status(L"Buscando Ultimate Spider-Man...", 350);
    if (auto detected = registry_install_path(); detected && exists(*detected + L"\\USM.exe")) state.game = *detected;
    else if (exists(L"C:\\Program Files (x86)\\Activision\\Ultimate Spider-Man\\USM.exe"))
        state.game = L"C:\\Program Files (x86)\\Activision\\Ultimate Spider-Man";
    splash_status(L"Analizando tu PC...", 350);
    state.hardware = detect_hardware();
    state.recommended = recommended_preset(state.hardware);
    state.preset = state.recommended;
    state.settings = kPresets[state.recommended].values;
    splash_status(L"Listo", 300);

    WNDCLASSW main_class = {};
    main_class.lpfnWndProc = window_proc;
    main_class.hInstance = instance;
    main_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    main_class.hIcon = icon;
    main_class.lpszClassName = L"USMModInstaller";
    RegisterClassW(&main_class);
    RECT frame = {0, 0, S(860), S(566)};
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    AdjustWindowRect(&frame, style, FALSE);
    const int ww = frame.right - frame.left, wh = frame.bottom - frame.top;
    state.window = CreateWindowExW(0, L"USMModInstaller", L"Ultimate Spider-Man - Mod para PC", style,
                                   (GetSystemMetrics(SM_CXSCREEN) - ww) / 2, (GetSystemMetrics(SM_CYSCREEN) - wh) / 2,
                                   ww, wh, nullptr, nullptr, instance, nullptr);
    build();
    wchar_t rec[200];
    swprintf_s(rec, L"Recomendado para tu PC: %s  (%.0f GB de RAM, %u núcleos%s%s)", kPresets[state.recommended].name,
               state.hardware.ram_gb, state.hardware.cores, state.hardware.gpu.empty() ? L"" : L", ",
               state.hardware.gpu.c_str());
    SetWindowTextW(recommended_label, rec);
    check_game();  // enables "change settings" / "uninstall" when the mod is already there
    KillTimer(splash.hwnd, 1);
    DestroyWindow(splash.hwnd);
    show_page(kWelcome);
    ShowWindow(state.window, show);
    UpdateWindow(state.window);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(state.window, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    CoUninitialize();
    return state.succeeded ? 0 : 1;
}

}  // namespace gui

int run_gui(HINSTANCE instance, int show) { return gui::run(instance, show); }

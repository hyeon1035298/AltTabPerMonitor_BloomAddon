// SPDX-License-Identifier: GPL-3.0-only
// ==WindhawkMod==
// @id              alt-tab-per-monitor
// @name            Alt+Tab per monitor
// @description     Alt+Tab on the monitor under the cursor, with that monitor's windows, in Windows' switcher or in a switcher of window previews or icons
// @version         1.2.1
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -lversion -ld2d1 -ldwmapi -ldwrite -lgdi32 -lruntimeobject -lshcore -lshell32 -lshlwapi -luuid -luxtheme -lwindowscodecs
// ==/WindhawkMod==

// ==WindhawkModSettings==
/*
- switcher:
  - style: windows
    $name: 표시 방식
    $description: 창 미리보기와 아이콘 전환기는 이 애드온이 Bloom 모양 판에 그립니다. 관리자 권한 창이 앞에 있을 때는 Windows 전환기가 뜹니다
    $options:
    - windows: Windows 전환기
    - previews: 창 미리보기
    - icons: 아이콘 전환기
  - iconSize: medium
    $name: 크기
    $description: 창 미리보기와 아이콘 전환기에서
    $options:
    - small: 작게
    - medium: 보통
    - large: 크게
    - huge: 아주 크게
  - titles: selected
    $name: 창 이름
    $description: 창 미리보기와 아이콘 전환기에서
    $options:
    - none: 보이지 않음
    - selected: 고른 창만
    - all: 모두
  $name: 전환기
- monitor:
  - altTabLocation: cursor
    $name: "Alt+Tab: 나타날 모니터"
    $options:
    - cursor: 커서가 있는 모니터
    - primary: 주 모니터
  - altTabWindows: cursorMonitor
    $name: "Alt+Tab: 보일 창"
    $options:
    - cursorMonitor: 커서가 있는 모니터의 창
    - allMonitors: 모든 모니터의 창
    - cursorMonitorExceptPrimary: 커서가 있는 모니터의 창, 주 모니터에서는 모든 창
  - winAltTabLocation: cursor
    $name: "Win+Alt+Tab: 나타날 모니터"
    $options:
    - cursor: 커서가 있는 모니터
    - primary: 주 모니터
  - winAltTabWindows: cursorMonitor
    $name: "Win+Alt+Tab: 보일 창"
    $options:
    - cursorMonitor: 커서가 있는 모니터의 창
    - allMonitors: 모든 모니터의 창
    - cursorMonitorExceptPrimary: 커서가 있는 모니터의 창, 주 모니터에서는 모든 창
  $name: 모니터
- pill:
  - theme: system
    $name: 테마
    $description: 창 미리보기와 아이콘 전환기의 라이트·다크. Bloom 애드온으로 쓰면 Bloom의 테마를 따릅니다
    $options:
    - system: Windows 앱 테마
    - dark: 다크
    - light: 라이트
  - glass: true
    $name: 유리 효과
    $description: 켜면 전환기 판 뒤가 살짝 비치고, 끄면 테마 색으로 꽉 칠해집니다. Bloom 애드온으로 쓰면 Bloom의 유리 효과를 따릅니다
  $name: 모양
*/
// ==/WindhawkModSettings==

// The add-on's mod: the Windhawk mod "Alt+Tab per monitor" (alt-tab-per-monitor.wh.cpp,
// MIT, included below unchanged) and a switcher of the add-on's own that can stand in for
// Windows' switcher. The settings sit in groups so that Bloom draws a page for them; the
// included mod reads its four keys from the "monitor" group.
//
// The switcher: a low-level keyboard hook on a thread of its own takes Alt+Tab,
// Alt+Shift+Tab and Win+Alt+Tab (Ctrl+Alt+Tab stays Windows') and hands every key to a
// second thread that lists the windows, draws them and switches. While an elevated window
// is in front the hook gets no keys, and Windows' switcher comes as before. It shows the
// windows as live previews (DWM thumbnails) or as app icons, on a panel with Bloom's G2
// corners drawn on a see-through window, in the order they were used. The window under the
// mouse has a close button. The look is Bloom's: its colors and the accent picked in Bloom,
// Segoe UI. It holds memory only while it is open (see Switcher), apart from the order of
// use (g_used).

#include <appmodel.h>
#include <d2d1_1.h>
#include <dwmapi.h>
#include <dwrite.h>
#include <propsys.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <uxtheme.h>
#include <wincodec.h>
#include <windowsx.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.ViewManagement.h>

// Included first so that the renaming below reaches only the mod's own code.
#include <windhawk_utils.h>

#define Wh_ModInit AltTabPerMonitor_ModInit
#define Wh_ModSettingsChanged AltTabPerMonitor_ModSettingsChanged
#define Wh_GetStringSetting MonitorSetting
PCWSTR MonitorSetting(PCWSTR name);
#include "alt-tab-per-monitor.wh.cpp"
#undef Wh_GetStringSetting
#undef Wh_ModSettingsChanged
#undef Wh_ModInit

// The included mod's keys live in the "monitor" group.
PCWSTR MonitorSetting(PCWSTR name) {
    return Wh_GetStringSetting(L"monitor.%s", name);
}

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace sw {

using winrt::com_ptr;

// ---- settings -------------------------------------------------------------------------

enum class Style { windows, previews, icons };
enum class Titles { none, selected, all };
enum class Theme { system, dark, light };

// By "switcher.iconSize": icons in pixels, previews as the height of their box.
constexpr int kIconSizes[] = {32, 48, 64, 96};
constexpr int kPreviewHeights[] = {104, 144, 192, 256};

struct Options {
    Style style = Style::windows;
    int size = 1;  // small, medium, large, huge
    Titles titles = Titles::selected;
    Theme theme = Theme::system;
    bool glass = true;
};

SRWLOCK g_optionsLock = SRWLOCK_INIT;
Options g_options;

bool Is(PCWSTR value, PCWSTR expected) {
    return wcscmp(value, expected) == 0;
}

Options ReadOptions() {
    Options o;
    PCWSTR v = Wh_GetStringSetting(L"switcher.style");
    o.style = Is(v, L"icons")      ? Style::icons
              : Is(v, L"previews") ? Style::previews
                                   : Style::windows;
    Wh_FreeStringSetting(v);
    v = Wh_GetStringSetting(L"switcher.iconSize");
    o.size = Is(v, L"small") ? 0 : Is(v, L"large") ? 2 : Is(v, L"huge") ? 3 : 1;
    Wh_FreeStringSetting(v);
    v = Wh_GetStringSetting(L"switcher.titles");
    o.titles = Is(v, L"none") ? Titles::none : Is(v, L"all") ? Titles::all : Titles::selected;
    Wh_FreeStringSetting(v);
    v = Wh_GetStringSetting(L"pill.theme");
    o.theme = Is(v, L"dark") ? Theme::dark : Is(v, L"light") ? Theme::light : Theme::system;
    Wh_FreeStringSetting(v);
    o.glass = Wh_GetIntSetting(L"pill.glass") != 0;
    return o;
}

Options CurrentOptions() {
    AcquireSRWLockShared(&g_optionsLock);
    Options o = g_options;
    ReleaseSRWLockShared(&g_optionsLock);
    return o;
}

DWORD PersonalizeValue(PCWSTR name, DWORD fallback) {
    DWORD value = fallback, size = sizeof(value);
    RegGetValueW(HKEY_CURRENT_USER,
                 L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", name,
                 RRF_RT_REG_DWORD, nullptr, &value, &size);
    return value;
}

bool DarkTheme(Theme theme) {
    if (theme != Theme::system) return theme == Theme::dark;
    return PersonalizeValue(L"AppsUseLightTheme", 0) == 0;
}

// ---- Bloom's look ---------------------------------------------------------------------

// Bloom's tokens (Ui/Theme/Palette.cs): zinc grays, and an accent used only for what is
// on (the chosen window). The accent here is Bloom's default rose; each opening puts in
// the one picked in Bloom (CurrentAccent).
struct Look {
    bool dark;
    uint32_t solid, text, selected, hover, accent;
    // The close button: Bloom's button, its edge, and its red, kept for what cannot be
    // undone, under the mouse; the cross on the red is white in both themes.
    uint32_t button, edge, danger;
    COLORREF border;  // the panel's hairline
};
constexpr Look kDark{true,       0xFF18181B, 0xFFECEDEE, 0x1AFFFFFF, 0x14FFFFFF, 0xFFFF7A9E,
                     0xFF3F3F46, 0x26FFFFFF, 0xFFC62F35, RGB(0x35, 0x2E, 0x31)};
constexpr Look kLight{false,      0xFFF4F4F5, 0xFF11181C, 0x1A111111, 0x14111111, 0xFFE04E78,
                      0xFFFFFFFF, 0x26111111, 0xFFDC3E42, RGB(0xD9, 0xD2, 0xD5)};
constexpr uint32_t kDangerInk = 0xFFFFFFFF;

// ---- Bloom's accent -------------------------------------------------------------------

// Bloom keeps its accent in its settings.json ("accent": a preset, or "custom" with
// "accentCustom" #RRGGBB, the light theme's color) and does not hand it to add-ons, so it
// is read from there with Bloom's own pairs, as the Keep Awake add-on reads it. Without
// Bloom's settings the Windows accent is used.
struct AccentPair {
    uint32_t dark, light;
};

// HSL, as Bloom lifts a typed color for the dark theme.
void ToHsl(uint32_t argb, double& h, double& s, double& l) {
    double r = ((argb >> 16) & 0xFF) / 255.0, g = ((argb >> 8) & 0xFF) / 255.0,
           b = (argb & 0xFF) / 255.0;
    double hi = std::max({r, g, b}), lo = std::min({r, g, b}), d = hi - lo;
    l = (hi + lo) / 2;
    s = d == 0 ? 0 : d / (1 - std::fabs(2 * l - 1));
    h = d == 0     ? 0
        : hi == r  ? 60 * std::fmod((g - b) / d + 6, 6)
        : hi == g  ? 60 * ((b - r) / d + 2)
                   : 60 * ((r - g) / d + 4);
}

uint32_t FromHsl(double h, double s, double l) {
    double c = (1 - std::fabs(2 * l - 1)) * s;
    double x = c * (1 - std::fabs(std::fmod(h / 60, 2) - 1)), m = l - c / 2;
    double r = 0, g = 0, b = 0;
    switch (static_cast<int>(h / 60) % 6) {
        case 0: r = c, g = x; break;
        case 1: r = x, g = c; break;
        case 2: g = c, b = x; break;
        case 3: g = x, b = c; break;
        case 4: r = x, b = c; break;
        default: r = c, b = x; break;
    }
    auto byte = [&](double v) {
        return static_cast<uint32_t>(std::lround(std::clamp(v + m, 0.0, 1.0) * 255));
    };
    return 0xFF000000u | byte(r) << 16 | byte(g) << 8 | byte(b);
}

std::optional<AccentPair> BloomAccent(const std::wstring& key, const std::wstring& custom) {
    static constexpr struct {
        PCWSTR key;
        AccentPair pair;
    } kPresets[] = {
        {L"rose", {0xFFFF7A9E, 0xFFE04E78}},      {L"lavender", {0xFFB49CFF, 0xFF7B5BDB}},
        {L"sky", {0xFF72B6FF, 0xFF2F72D6}},       {L"mint", {0xFF5ED3AE, 0xFF168F6C}},
        {L"tangerine", {0xFFFF9B5E, 0xFFD9652A}}, {L"lemon", {0xFFEDC94F, 0xFFA87C00}},
        {L"gray", {0xFFE4E4E4, 0xFF2E2E2E}},
    };
    if (key == L"custom") {
        std::wstring t = custom;
        if (!t.empty() && t[0] == L'#') t.erase(0, 1);
        if (t.size() != 6 || t.find_first_not_of(L"0123456789abcdefABCDEF") != std::wstring::npos) {
            return std::nullopt;
        }
        uint32_t light = 0xFF000000u | static_cast<uint32_t>(wcstoul(t.c_str(), nullptr, 16));
        double h, s, l;
        ToHsl(light, h, s, l);
        return AccentPair{FromHsl(h, s, std::max(l, std::min(0.86, l + 0.18))), light};
    }
    for (const auto& p : kPresets) {
        if (key == p.key) return p.pair;
    }
    return std::nullopt;
}

std::wstring BloomSettingsPath() {
    PWSTR folder = nullptr;
    std::wstring path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &folder))) {
        path = std::wstring(folder) + L"\\Bloom\\settings.json";
    }
    CoTaskMemFree(folder);
    return path;
}

std::optional<std::wstring> ReadUtf8File(const std::wstring& path, DWORD maxBytes) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return std::nullopt;
    LARGE_INTEGER size{};
    std::string bytes;
    DWORD read = 0;
    bool ok = GetFileSizeEx(file, &size) && size.QuadPart <= maxBytes;
    if (ok) {
        bytes.resize(static_cast<size_t>(size.QuadPart));
        ok = bytes.empty() || (ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()),
                                        &read, nullptr) &&
                               read == bytes.size());
    }
    CloseHandle(file);
    if (!ok) return std::nullopt;
    int n = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()),
                                nullptr, 0);
    std::wstring text(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), text.data(),
                        n);
    if (!text.empty() && text[0] == 0xFEFF) text.erase(0, 1);
    return text;
}

AccentPair WindowsAccent() {
    try {
        using winrt::Windows::UI::ViewManagement::UIColorType;
        winrt::Windows::UI::ViewManagement::UISettings ui;
        auto argb = [](winrt::Windows::UI::Color c) {
            return static_cast<uint32_t>(c.A) << 24 | c.R << 16 | c.G << 8 | c.B;
        };
        return {argb(ui.GetColorValue(UIColorType::AccentLight2)),
                argb(ui.GetColorValue(UIColorType::AccentDark1))};
    } catch (...) {
        return {kDark.accent, kLight.accent};
    }
}

AccentPair ReadAccent(const std::wstring& path) {
    if (auto text = ReadUtf8File(path, 4 * 1024 * 1024)) {
        try {
            winrt::Windows::Data::Json::JsonObject o{nullptr};
            if (winrt::Windows::Data::Json::JsonObject::TryParse(*text, o)) {
                auto key = std::wstring(o.GetNamedString(L"accent", L"rose"));
                auto custom = std::wstring(o.GetNamedString(L"accentCustom", L""));
                return BloomAccent(key, custom).value_or(AccentPair{kDark.accent, kLight.accent});
            }
        } catch (...) {
        }
    }
    return WindowsAccent();
}

// The accent for one opening, on the switcher's thread. Bloom's file is read again only
// when its time changed, so a color picked in Bloom shows from the next Alt+Tab on; all
// that is kept between openings is the two colors and the file's time.
AccentPair CurrentAccent() {
    static FILETIME seen{};
    static AccentPair accent{kDark.accent, kLight.accent};
    std::wstring path = BloomSettingsPath();
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        seen = {};
        return accent = WindowsAccent();  // no Bloom: follow Windows as it changes
    }
    if (CompareFileTime(&data.ftLastWriteTime, &seen) != 0) {
        seen = data.ftLastWriteTime;
        accent = ReadAccent(path);
    }
    return accent;
}
// With glass on the panel lets 5 % of what is behind through; no blur can follow its G2
// corners, so more would cost the icons and titles their contrast.
constexpr uint32_t kGlassAlpha = 0xF2;

D2D1_COLOR_F Color(uint32_t argb) {
    return {((argb >> 16) & 0xFF) / 255.f, ((argb >> 8) & 0xFF) / 255.f, (argb & 0xFF) / 255.f,
            (argb >> 24) / 255.f};
}

// Sizes in DIPs.
constexpr float kPanelPad = 10, kGap = 6, kItemPad = 12, kRadius = 11, kRing = 2;
// Window titles grow with "switcher.iconSize", in Bloom's type sizes (12 at medium), each
// on a line 4/3 of its size. With every title shown a cell is as wide as about 140 DIPs
// are at 12.
constexpr float kFontSizes[] = {11, 12, 14, 16};
constexpr float kTitleGap = 8, kAllTitlesWidth = 140.f / 12;
// Previews: the box a thumbnail fits in is 16:10; the title above it after an icon as tall
// as its line.
constexpr float kPreviewPad = 6, kPreviewGap = 12, kFallbackIcon = 48;
// The close button: its size by "switcher.iconSize", in a preview's title row as tall as
// the row allows; its inset from a cell's corner; its cross.
constexpr float kButtonSizes[] = {16, 18, 20, 22};
constexpr float kButtonInset = 4, kButtonGrace = 2, kCrossArm = 0.4f, kCrossWidth = 1.5f;

// ---- windows to list -------------------------------------------------------------------

bool IsAltTabWindow(HWND w, HWND self) {
    if (w == self || !IsWindowVisible(w)) return false;
    LONG_PTR ex = GetWindowLongPtrW(w, GWL_EXSTYLE);
    bool app = ex & WS_EX_APPWINDOW;
    if (!app && (ex & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE))) return false;
    // Windows' rule: from the root owner down the visible last-active popups lands here.
    HWND walk = GetAncestor(w, GA_ROOTOWNER);
    for (HWND next; (next = GetLastActivePopup(walk)) != walk;) {
        if (IsWindowVisible(next)) {
            walk = next;
            break;
        }
        walk = next;
    }
    if (walk != w && !app) return false;
    DWORD cloaked = 0;  // other virtual desktops, suspended apps
    if (SUCCEEDED(DwmGetWindowAttribute(w, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) &&
        cloaked) {
        return false;
    }
    if (w == GetShellWindow()) return false;
    wchar_t name[32];
    if (GetClassNameW(w, name, ARRAYSIZE(name)) &&
        (Is(name, L"Progman") || Is(name, L"WorkerW") || Is(name, L"Shell_TrayWnd") ||
         Is(name, L"Shell_SecondaryTrayWnd"))) {
        return false;
    }
    return true;
}

// A minimized window counts on the monitor it comes back to.
HMONITOR WindowMonitor(HWND w) {
    WINDOWPLACEMENT p{sizeof(p)};
    if (IsIconic(w) && GetWindowPlacement(w, &p)) {
        return MonitorFromRect(&p.rcNormalPosition, MONITOR_DEFAULTTONEAREST);
    }
    return MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST);
}

struct Entry {
    HWND hwnd;
    std::wstring title;
    com_ptr<ID2D1Bitmap> icon;  // previews: the title's small icon
    com_ptr<ID2D1Bitmap> big;   // previews: in the box of a window without a thumbnail
};

// ---- the order windows were used in ------------------------------------------------------

// Windows' switcher lists the window used last first. Z order mostly says the same, but a
// window kept on top ("always on top") stays above every other one however long ago it was
// used, so in Z order it would always be offered first. The switcher's thread therefore
// notes each window that comes to the front (OnWindowEvent, from the time the switcher is
// on) and lists those first, newest first; a minimized one leaves the note and so goes to
// the back, as in Z order. Windows not seen in front since then follow in Z order.
// Dialogs and other owned windows count as their owner. This, a few handles, is all the
// switcher keeps while it is closed.
std::vector<HWND> g_used;  // newest first; touched on the switcher's thread only
constexpr size_t kUsedMax = 64;

HWND RootOwner(HWND w) {
    HWND root = GetAncestor(w, GA_ROOTOWNER);
    return root ? root : w;
}

void CALLBACK OnWindowEvent(HWINEVENTHOOK, DWORD event, HWND w, LONG object, LONG child, DWORD,
                            DWORD) {
    if (!w || object != OBJID_WINDOW || child != CHILDID_SELF) return;
    if (GetWindowThreadProcessId(w, nullptr) == GetCurrentThreadId()) return;  // the switcher
    HWND owner = RootOwner(w);
    std::erase(g_used, owner);
    if (event == EVENT_SYSTEM_FOREGROUND) {
        g_used.insert(g_used.begin(), owner);
        if (g_used.size() > kUsedMax) g_used.pop_back();
    }
}

std::vector<Entry> ListWindows(bool win, HWND self) {
    POINT pt{};
    GetCursorPos(&pt);
    HMONITOR cursor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    HMONITOR primary = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
    WindowsToShow mode = win ? g_settings.winAltTabWindows : g_settings.altTabWindows;
    bool all = mode == WindowsToShow::allMonitors ||
               (mode == WindowsToShow::cursorMonitorExceptPrimary && cursor == primary);

    struct Ctx {
        HWND self;
        HMONITOR monitor;
        std::vector<HWND>* out;
    };
    std::vector<HWND> windows;
    Ctx ctx{self, all ? nullptr : cursor, &windows};
    // In Z order, minimized ones last.
    EnumWindows(
        [](HWND w, LPARAM lp) -> BOOL {
            auto& c = *reinterpret_cast<Ctx*>(lp);
            if (!IsAltTabWindow(w, c.self)) return TRUE;
            if (c.monitor && WindowMonitor(w) != c.monitor) return TRUE;
            c.out->push_back(w);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&ctx));
    // Then the ones used since the switcher is on, newest first. Sorted before the titles
    // are read, which may let window events through.
    std::erase_if(g_used, [](HWND w) { return !IsWindow(w); });
    auto used = [](HWND w) {
        return std::find(g_used.begin(), g_used.end(), RootOwner(w)) - g_used.begin();
    };
    std::stable_sort(windows.begin(), windows.end(),
                     [&](HWND a, HWND b) { return used(a) < used(b); });

    std::vector<Entry> out;
    out.reserve(windows.size());
    for (HWND w : windows) {
        wchar_t title[256];
        int n = GetWindowTextW(w, title, ARRAYSIZE(title));
        out.push_back({w, std::wstring(title, std::max(n, 0)), nullptr, nullptr});
    }
    return out;
}

// ---- icons -----------------------------------------------------------------------------

// The shell's per-window keys (propkey.h), spelled out.
constexpr GUID kAppUserModelKeys = {
    0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}};
constexpr PROPERTYKEY kRelaunchIconResource{kAppUserModelKeys, 3};
constexpr PROPERTYKEY kAppUserModelId{kAppUserModelKeys, 5};

std::wstring WindowProperty(HWND w, const PROPERTYKEY& key) {
    com_ptr<IPropertyStore> store;
    if (FAILED(SHGetPropertyStoreForWindow(w, IID_PPV_ARGS(store.put())))) return {};
    PROPVARIANT v;
    PropVariantInit(&v);
    std::wstring result;
    if (SUCCEEDED(store->GetValue(key, &v)) && v.vt == VT_LPWSTR && v.pwszVal) {
        result = v.pwszVal;
    }
    PropVariantClear(&v);
    return result;
}

std::wstring ProcessPath(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return {};
    wchar_t path[MAX_PATH];
    DWORD size = ARRAYSIZE(path);
    std::wstring result;
    if (QueryFullProcessImageNameW(process, 0, path, &size)) result.assign(path, size);
    CloseHandle(process);
    return result;
}

// A packaged app in ApplicationFrameHost's frame: its id from the app's own window inside.
std::wstring FramedAppId(HWND frame) {
    std::wstring id;
    EnumChildWindows(
        frame,
        [](HWND child, LPARAM lp) -> BOOL {
            wchar_t name[64];
            if (!GetClassNameW(child, name, ARRAYSIZE(name)) ||
                !Is(name, L"Windows.UI.Core.CoreWindow")) {
                return TRUE;
            }
            DWORD pid = 0;
            GetWindowThreadProcessId(child, &pid);
            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (!process) return TRUE;
            wchar_t aumid[130];  // APPLICATION_USER_MODEL_ID_MAX_LENGTH
            UINT32 length = ARRAYSIZE(aumid);
            if (GetApplicationUserModelId(process, &length, aumid) == ERROR_SUCCESS) {
                *reinterpret_cast<std::wstring*>(lp) = aumid;
            }
            CloseHandle(process);
            return FALSE;
        },
        reinterpret_cast<LPARAM>(&id));
    return id;
}

// A busy program (an editor in the middle of its work) answers late: both questions share
// 20 ms, after which its class icon, or its program's, stands in.
HICON WindowIcon(HWND w) {
    using namespace std::chrono;
    auto until = steady_clock::now() + milliseconds(20);
    DWORD_PTR icon = 0;
    for (WPARAM kind : {ICON_BIG, ICON_SMALL2}) {
        auto left = duration_cast<milliseconds>(until - steady_clock::now()).count();
        if (left <= 0 || !SendMessageTimeoutW(w, WM_GETICON, kind, 0, SMTO_ABORTIFHUNG,
                                              static_cast<UINT>(left), &icon)) {
            break;
        }
        if (icon) return reinterpret_cast<HICON>(icon);
    }
    if ((icon = GetClassLongPtrW(w, GCLP_HICON))) return reinterpret_cast<HICON>(icon);
    return reinterpret_cast<HICON>(GetClassLongPtrW(w, GCLP_HICONSM));
}

int IconWidth(HICON icon) {
    ICONINFO info{};
    if (!GetIconInfo(icon, &info)) return 0;
    BITMAP bm{};
    GetObjectW(info.hbmColor ? info.hbmColor : info.hbmMask, sizeof(bm), &bm);
    if (info.hbmColor) DeleteObject(info.hbmColor);
    if (info.hbmMask) DeleteObject(info.hbmMask);
    return bm.bmWidth;
}

// Processes whose windows are someone else's: their exe icon would be the wrong one.
bool IsHostProcess(const std::wstring& path) {
    PCWSTR name = PathFindFileNameW(path.c_str());
    for (PCWSTR host : {L"explorer.exe", L"ApplicationFrameHost.exe", L"rundll32.exe",
                        L"dllhost.exe", L"mmc.exe", L"java.exe", L"javaw.exe", L"python.exe",
                        L"pythonw.exe"}) {
        if (_wcsicmp(name, host) == 0) return true;
    }
    return false;
}

// Brings a window to the front. Explorer did not get the key itself (the hook did), so
// Windows may refuse it the foreground; joined to the front window's input it may take it.
void Activate(HWND w, bool alttab) {
    HWND front = GetForegroundWindow();
    DWORD me = GetCurrentThreadId(), theirs = front ? GetWindowThreadProcessId(front, nullptr) : 0;
    bool joined = theirs && theirs != me && AttachThreadInput(me, theirs, TRUE);
    if (alttab) {
        SwitchToThisWindow(w, TRUE);  // as Alt+Tab: restores it, then its last active popup
    } else {
        SetForegroundWindow(w);
    }
    if (joined) AttachThreadInput(me, theirs, FALSE);
}

// ---- drawing ---------------------------------------------------------------------------

// A rounded rectangle whose corners are superellipse arcs (exponent 2.5 over 1.2 r), the
// continuous-curvature corner Bloom draws (SmoothBorder), closely enough at these sizes.
com_ptr<ID2D1PathGeometry> Squircle(ID2D1Factory* factory, D2D1_RECT_F r, float radius) {
    com_ptr<ID2D1PathGeometry> path;
    com_ptr<ID2D1GeometrySink> sink;
    if (FAILED(factory->CreatePathGeometry(path.put())) || FAILED(path->Open(sink.put()))) {
        return nullptr;
    }
    float a = std::min({radius * 1.2f, (r.right - r.left) / 2, (r.bottom - r.top) / 2});
    constexpr int kSteps = 12;
    constexpr float kExponent = 2 / 2.5f;
    // Corner centers and the quadrant each one sweeps, clockwise from the top left.
    const struct {
        float cx, cy, sx, sy;
        bool swap;
    } corners[] = {
        {r.left + a, r.top + a, -1, -1, false},
        {r.right - a, r.top + a, 1, -1, true},
        {r.right - a, r.bottom - a, 1, 1, false},
        {r.left + a, r.bottom - a, -1, 1, true},
    };
    bool first = true;
    for (const auto& c : corners) {
        for (int i = 0; i <= kSteps; i++) {
            // t = 0 is on the side edge, t = pi/2 on the top or bottom edge; the top
            // right and bottom left corners run from the latter to the former.
            // cos(pi/2) comes out a hair below zero in float, and pow of a negative
            // number is NaN, which would void the whole shape.
            float t = (c.swap ? kSteps - i : i) * 1.5707964f / kSteps;
            D2D1_POINT_2F p{
                c.cx + c.sx * a * std::pow(std::max(0.f, std::cos(t)), kExponent),
                c.cy + c.sy * a * std::pow(std::max(0.f, std::sin(t)), kExponent)};
            if (first) {
                sink->BeginFigure(p, D2D1_FIGURE_BEGIN_FILLED);
                first = false;
            } else {
                sink->AddLine(p);
            }
        }
    }
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    return path;
}

// Everything the switcher draws with exists only while it is open: the window, the
// Direct2D software target, the text and imaging factories and the icons are made on
// Alt+Tab and let go when it closes. Nothing is kept between openings and no timer runs
// while it is closed. The target draws on the CPU, so no graphics device is made; the
// previews are DWM thumbnails, which DWM draws, so they cost the process only a handle.
class Switcher {
   public:
    void Start(bool back, bool win);
    void Step(int delta);
    void Commit(int index);
    void Cancel();
    void Close();
    void CloseSelected() { CloseWindowAt(selected_); }
    bool IsOpen() const { return hwnd_ != nullptr; }
    int Cols() const { return cols_; }

   private:
    static LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);

    bool Open();
    bool Make();
    void Free();
    com_ptr<ID2D1Bitmap> IconFor(HWND w, float dips);
    com_ptr<ID2D1Bitmap> FromIcon(HICON icon);
    com_ptr<ID2D1Bitmap> FromBitmap(HBITMAP bitmap);
    RECT Layout(HMONITOR monitor);
    void PlaceCells();
    void Relayout();
    void ShowThumbnails();
    void DropThumbnails();
    void ApplyBackdrop();
    void Paint();
    void Draw();
    void DrawIcon(ID2D1Bitmap* bitmap, D2D1_RECT_F dest);
    void DrawPreview(size_t cell, ID2D1SolidColorBrush* brush);
    D2D1_RECT_F PreviewBox(const D2D1_RECT_F& cell) const;
    int HitTest(POINT pt) const;
    void Hover(POINT pt, bool again = false);
    void HoverAtCursor();
    D2D1_ELLIPSE ButtonAt(const D2D1_RECT_F& cell) const;
    bool OnButton(int index, POINT pt) const;
    void ShowButton();
    void DrawButton(D2D1_POINT_2F center, float radius, bool hot);
    void CloseWindowAt(int index);
    void DropClosed();

    bool Previews() const { return options_.style == Style::previews; }
    float TitleRow() const { return line_ + 8; }  // previews: the title's row

    HWND hwnd_ = nullptr;
    HMONITOR monitor_ = nullptr;
    Options options_;
    Look look_ = kDark;
    bool glass_ = false;
    std::vector<Entry> entries_;
    int selected_ = 0, hover_ = -1;
    bool switching_ = false;
    HWND before_ = nullptr;

    // The close button on the window under the mouse: a window of its own (ShowButton), lit
    // while the mouse is on it.
    HWND button_ = nullptr;
    bool buttonHot_ = false;
    // Windows asked to close and not gone yet, and since when.
    std::vector<HWND> closing_;
    ULONGLONG closingSince_ = 0;

    // Layout in DIPs.
    float dpi_ = 96, cellW_ = 0, cellH_ = 0, footer_ = 0, width_ = 0, height_ = 0, gap_ = 0;
    float typeSize_ = kFontSizes[1], line_ = 16;  // the titles' type and line
    int cols_ = 1, perPage_ = 1;
    std::vector<D2D1_RECT_F> cells_;  // the current page's cells
    int first_ = 0;                   // the entry in the first cell
    // Previews: one per cell, null where the window gives no thumbnail.
    struct Thumbnail {
        HTHUMBNAIL id = nullptr;
        RECT dest{};  // in pixels of the switcher's client area
    };
    std::vector<Thumbnail> thumbnails_;

    com_ptr<ID2D1Factory> d2d_;
    com_ptr<ID2D1DCRenderTarget> target_;
    com_ptr<ID2D1DeviceContext> context_;  // the same target, for its better scaling
    com_ptr<ID2D1StrokeStyle> round_;      // the cross's round ends
    com_ptr<IDWriteFactory> dwrite_;
    com_ptr<IDWriteTextFormat> font_;
    com_ptr<IDWriteInlineObject> ellipsis_;
    com_ptr<IWICImagingFactory> wic_;
    // One opening's icons by source, so windows of one program share theirs.
    std::unordered_map<std::wstring, com_ptr<ID2D1Bitmap>> icons_;
};

// Thread messages from the hook thread to the switcher's thread.
enum : UINT {
    kStart = WM_APP + 1,  // wParam: 1 back (Shift), lParam: 1 Win held
    kStep,                // wParam: steps as int, lParam: 1 by rows
    kCommit,
    kCancel,
    kClose,  // the selected window
    kStopUi,
};
enum : UINT_PTR { kAltTimer = 1, kCloseTimer };
// How often a window asked to close is looked for, and how long before one that stays (a
// program asking to save first) is left in the list.
constexpr UINT kCloseCheckMs = 100;
constexpr ULONGLONG kCloseWaitMs = 5000;
constexpr wchar_t kClassName[] = L"AltTabPerMonitor.IconSwitcher";

// Set by the hook thread when it takes a Tab, cleared once the switcher is done.
std::atomic<bool> g_active{false};

LRESULT CALLBACK Switcher::Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(
                              reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
    }
    auto* self = reinterpret_cast<Switcher*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self && self->hwnd_ == hwnd ? self->Handle(msg, wp, lp)
                                       : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT Switcher::Handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT:
            Paint();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_TIMER:
            if (wp == kAltTimer && GetAsyncKeyState(VK_MENU) >= 0) {
                // The hook missed Alt going up (an elevated window took the keys).
                Commit(-1);
            } else if (wp == kCloseTimer) {
                DropClosed();
            }
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE && !switching_) Close();
            return 0;
        case WM_MOUSEMOVE:
            Hover({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            return 0;
        case WM_MOUSELEAVE:
            Hover({-1, -1});
            return 0;
        case WM_LBUTTONUP: {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            if (int hit = HitTest(pt); hit >= 0) {
                if (OnButton(hit, pt)) {
                    CloseWindowAt(hit);
                } else {
                    Commit(hit);
                }
            }
            return 0;
        }
        case WM_RBUTTONUP:  // closes the window clicked, with no menu
            if (int hit = HitTest({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}); hit >= 0) {
                CloseWindowAt(hit);
            }
            return 0;
        case WM_SYSCOMMAND:
            if ((wp & 0xFFF0) == SC_KEYMENU) return 0;  // Alt going up
            break;
        case WM_DPICHANGED:
            return 0;  // each opening lays itself out for its monitor
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

bool Switcher::Make() {
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d_.put()))) return false;
    auto props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpi_, dpi_);
    if (FAILED(d2d_->CreateDCRenderTarget(&props, target_.put()))) return false;
    context_ = target_.try_as<ID2D1DeviceContext>();
    d2d_->CreateStrokeStyle(
        D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND), nullptr, 0,
        round_.put());
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(dwrite_.put()))) ||
        FAILED(dwrite_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                         DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                         typeSize_, L"", font_.put())) ||
        FAILED(dwrite_->CreateEllipsisTrimmingSign(font_.get(), ellipsis_.put()))) {
        return false;
    }
    // Icons have their titles centered under them, previews after a small icon.
    font_->SetTextAlignment(Previews() ? DWRITE_TEXT_ALIGNMENT_LEADING
                                       : DWRITE_TEXT_ALIGNMENT_CENTER);
    font_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    font_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    font_->SetTrimming(&trimming, ellipsis_.get());
    return SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(wic_.put())));
}

void Switcher::Free() {
    DropThumbnails();
    for (auto& e : entries_) e.icon = e.big = nullptr;
    icons_.clear();
    round_ = nullptr;
    context_ = nullptr;
    target_ = nullptr;
    d2d_ = nullptr;
    ellipsis_ = nullptr;
    font_ = nullptr;
    dwrite_ = nullptr;
    wic_ = nullptr;
}

com_ptr<ID2D1Bitmap> Switcher::FromIcon(HICON icon) {
    com_ptr<IWICBitmap> source;
    com_ptr<IWICFormatConverter> converter;
    com_ptr<ID2D1Bitmap> bitmap;
    if (icon && SUCCEEDED(wic_->CreateBitmapFromHICON(icon, source.put())) &&
        SUCCEEDED(wic_->CreateFormatConverter(converter.put())) &&
        SUCCEEDED(converter->Initialize(source.get(), GUID_WICPixelFormat32bppPBGRA,
                                        WICBitmapDitherTypeNone, nullptr, 0,
                                        WICBitmapPaletteTypeCustom))) {
        target_->CreateBitmapFromWicBitmap(converter.get(), nullptr, bitmap.put());
    }
    return bitmap;
}

com_ptr<ID2D1Bitmap> Switcher::FromBitmap(HBITMAP hbitmap) {
    com_ptr<IWICBitmap> source;
    com_ptr<IWICFormatConverter> converter;
    com_ptr<ID2D1Bitmap> bitmap;
    if (SUCCEEDED(wic_->CreateBitmapFromHBITMAP(hbitmap, nullptr, WICBitmapUsePremultipliedAlpha,
                                                source.put())) &&
        SUCCEEDED(wic_->CreateFormatConverter(converter.put())) &&
        SUCCEEDED(converter->Initialize(source.get(), GUID_WICPixelFormat32bppPBGRA,
                                        WICBitmapDitherTypeNone, nullptr, 0,
                                        WICBitmapPaletteTypeCustom))) {
        target_->CreateBitmapFromWicBitmap(converter.get(), nullptr, bitmap.put());
    }
    return bitmap;
}

// "path,index" as the shell writes an icon location, at px pixels.
HICON LoadIconLocation(const std::wstring& location, int px) {
    std::wstring path = location;
    int index = PathParseIconLocationW(path.data());
    path.resize(wcslen(path.c_str()));
    HICON icon = nullptr;
    if (SHDefExtractIconW(path.c_str(), index, 0, &icon, nullptr, MAKELONG(px, px)) != S_OK) {
        return nullptr;
    }
    return icon;
}

com_ptr<ID2D1Bitmap> Switcher::IconFor(HWND w, float dips) {
    int px = static_cast<int>(std::lround(dips * dpi_ / 96));
    auto shared = [&](std::wstring key, auto&& make) -> com_ptr<ID2D1Bitmap> {
        key += L'@' + std::to_wstring(px);
        if (auto it = icons_.find(key); it != icons_.end()) return it->second;
        com_ptr<ID2D1Bitmap> bitmap = make();
        if (bitmap) icons_.emplace(std::move(key), bitmap);
        return bitmap;
    };
    auto fromLocation = [&](const std::wstring& location) {
        return shared(L"icon:" + location, [&] {
            com_ptr<ID2D1Bitmap> bitmap;
            if (HICON icon = LoadIconLocation(location, px)) {
                bitmap = FromIcon(icon);
                DestroyIcon(icon);
            }
            return bitmap;
        });
    };

    // An icon the window names for the taskbar (web apps, apps that host others).
    if (auto location = WindowProperty(w, kRelaunchIconResource); !location.empty()) {
        if (auto bitmap = fromLocation(location)) return bitmap;
    }
    // A packaged app: its logo from the Apps folder.
    wchar_t name[64];
    std::wstring aumid;
    if (GetClassNameW(w, name, ARRAYSIZE(name)) && Is(name, L"ApplicationFrameWindow")) {
        aumid = FramedAppId(w);
    }
    if (aumid.empty()) aumid = WindowProperty(w, kAppUserModelId);
    if (!aumid.empty() && aumid.find(L'!') != std::wstring::npos) {
        auto bitmap = shared(L"app:" + aumid, [&] {
            com_ptr<ID2D1Bitmap> result;
            com_ptr<IShellItemImageFactory> factory;
            HBITMAP hbitmap = nullptr;
            if (SUCCEEDED(SHCreateItemFromParsingName((L"shell:AppsFolder\\" + aumid).c_str(),
                                                      nullptr,
                                                      IID_PPV_ARGS(factory.put()))) &&
                SUCCEEDED(factory->GetImage({px, px}, SIIGBF_ICONONLY, &hbitmap))) {
                result = FromBitmap(hbitmap);
                DeleteObject(hbitmap);
            }
            return result;
        });
        if (bitmap) return bitmap;
    }
    // The window's own icon when it is big enough, else its program's at the right size.
    HICON own = WindowIcon(w);
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    std::wstring exe = ProcessPath(pid);
    if (!exe.empty() && !IsHostProcess(exe) && (!own || IconWidth(own) < px)) {
        if (auto bitmap = fromLocation(exe + L",0")) return bitmap;
    }
    if (own) return FromIcon(own);
    return shared(L"default", [&] { return FromIcon(LoadIconW(nullptr, IDI_APPLICATION)); });
}

// Sizes the panel for the entries on the monitor and centers it in the work area.
RECT Switcher::Layout(HMONITOR monitor) {
    UINT dx = 96, dy = 96;
    GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dx, &dy);
    dpi_ = static_cast<float>(dx);
    MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(monitor, &info);
    const RECT& work = info.rcWork;
    float scale = dpi_ / 96;
    float workW = (work.right - work.left) / scale, workH = (work.bottom - work.top) / scale;
    typeSize_ = kFontSizes[options_.size];
    line_ = std::ceil(typeSize_ * 4 / 3);

    if (Previews()) {
        // A box per window with its title plate above, every title in the same row
        // whether shown or not, so that the boxes line up.
        float box = static_cast<float>(kPreviewHeights[options_.size]);
        cellW_ = std::round(box * 1.6f) + 2 * kPreviewPad;
        cellH_ = box + 2 * kPreviewPad;
        if (options_.titles != Titles::none) cellH_ += TitleRow() + kPreviewPad;
        footer_ = 0;
        gap_ = kPreviewGap;
    } else {
        float icon = static_cast<float>(kIconSizes[options_.size]);
        cellW_ = icon + 2 * kItemPad;
        cellH_ = icon + 2 * kItemPad;
        if (options_.titles == Titles::all) {
            cellW_ = std::max(cellW_, std::round(kAllTitlesWidth * typeSize_));
            cellH_ += kTitleGap + line_;
        }
        footer_ = options_.titles == Titles::selected ? kTitleGap + line_ + 4 : 0;
        gap_ = kGap;
    }

    int n = static_cast<int>(entries_.size());
    float maxW = workW * 0.9f - 2 * kPanelPad, maxH = workH * 0.8f - 2 * kPanelPad - footer_;
    cols_ = std::clamp(static_cast<int>((maxW + gap_) / (cellW_ + gap_)), 1, n);
    int rows = (n + cols_ - 1) / cols_;
    int maxRows = std::max(1, static_cast<int>((maxH + gap_) / (cellH_ + gap_)));
    rows = std::min(rows, maxRows);
    perPage_ = cols_ * rows;

    width_ = 2 * kPanelPad + cols_ * cellW_ + (cols_ - 1) * gap_;
    height_ = 2 * kPanelPad + rows * cellH_ + (rows - 1) * gap_ + footer_;
    int w = static_cast<int>(std::lround(width_ * scale));
    int h = static_cast<int>(std::lround(height_ * scale));
    int x = work.left + (work.right - work.left - w) / 2;
    int y = work.top + (work.bottom - work.top - h) / 2;
    return {x, y, x + w, y + h};
}

// Puts the selected entry's page in the cells.
void Switcher::PlaceCells() {
    int count = static_cast<int>(entries_.size());
    first_ = selected_ / perPage_ * perPage_;
    int shown = std::min(perPage_, count - first_);
    cells_.clear();
    for (int i = 0; i < shown; i++) {
        int row = i / cols_, col = i % cols_;
        // A short last row sits in the middle.
        int inRow = std::min(cols_, shown - row * cols_);
        float x = kPanelPad + (cols_ - inRow) * (cellW_ + gap_) / 2 + col * (cellW_ + gap_);
        float y = kPanelPad + row * (cellH_ + gap_);
        cells_.push_back(D2D1::RectF(x, y, x + cellW_, y + cellH_));
    }
}

// The box a cell's thumbnail fits in, under its title.
D2D1_RECT_F Switcher::PreviewBox(const D2D1_RECT_F& cell) const {
    float top = cell.top + kPreviewPad;
    if (options_.titles != Titles::none) top += TitleRow() + kPreviewPad;
    return {cell.left + kPreviewPad, top, cell.right - kPreviewPad, cell.bottom - kPreviewPad};
}

// The panel again for the windows left after some closed, on the same monitor.
void Switcher::Relayout() {
    RECT r = Layout(monitor_);
    PlaceCells();
    SetWindowPos(hwnd_, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    ShowThumbnails();
    HoverAtCursor();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// Previews: asks DWM to draw each cell's window, fitted in its box and never enlarged.
// DWM draws them over the switcher's own pixels.
void Switcher::ShowThumbnails() {
    DropThumbnails();
    if (!Previews() || !hwnd_) return;
    float scale = dpi_ / 96;
    thumbnails_.resize(cells_.size());
    for (size_t i = 0; i < cells_.size(); i++) {
        Thumbnail& t = thumbnails_[i];
        SIZE source{};
        if (FAILED(DwmRegisterThumbnail(hwnd_, entries_[first_ + i].hwnd, &t.id))) {
            t.id = nullptr;
            continue;
        }
        // A minimized window may give only its caption's sliver; it gets its icon instead.
        if (FAILED(DwmQueryThumbnailSourceSize(t.id, &source)) || source.cx < 48 ||
            source.cy < 48) {
            DwmUnregisterThumbnail(t.id);
            t.id = nullptr;
            continue;
        }
        D2D1_RECT_F box = PreviewBox(cells_[i]);
        float bw = (box.right - box.left) * scale, bh = (box.bottom - box.top) * scale;
        float fit = std::min({bw / source.cx, bh / source.cy, 1.f});
        int w = static_cast<int>(std::lround(source.cx * fit));
        int h = static_cast<int>(std::lround(source.cy * fit));
        int x = static_cast<int>(std::lround(box.left * scale + (bw - w) / 2));
        int y = static_cast<int>(std::lround(box.top * scale + (bh - h) / 2));
        t.dest = {x, y, x + w, y + h};
        DWM_THUMBNAIL_PROPERTIES p{};
        p.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE | DWM_TNP_OPACITY |
                    DWM_TNP_SOURCECLIENTAREAONLY;
        p.rcDestination = t.dest;
        p.opacity = 255;
        p.fVisible = TRUE;
        p.fSourceClientAreaOnly = FALSE;
        DwmUpdateThumbnailProperties(t.id, &p);
    }
}

void Switcher::DropThumbnails() {
    for (auto& t : thumbnails_) {
        if (t.id) DwmUnregisterThumbnail(t.id);
    }
    std::vector<Thumbnail>().swap(thumbnails_);
}

// A see-through window: no backdrop, corners or border of DWM's, the frame spread over the
// whole window so that its pixels' alpha counts. The panel is drawn on it (Draw), so its
// corners can be Bloom's G2 curve, which DWM's rounding and acrylic cannot follow.
void Switcher::ApplyBackdrop() {
    BOOL noAnimation = TRUE;
    DwmSetWindowAttribute(hwnd_, DWMWA_TRANSITIONS_FORCEDISABLED, &noAnimation,
                          sizeof(noAnimation));
    DWM_WINDOW_CORNER_PREFERENCE square = DWMWCP_DONOTROUND;
    DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &square, sizeof(square));
    COLORREF none = DWMWA_COLOR_NONE;
    DwmSetWindowAttribute(hwnd_, DWMWA_BORDER_COLOR, &none, sizeof(none));
    DWM_SYSTEMBACKDROP_TYPE backdrop = DWMSBT_NONE;
    DwmSetWindowAttribute(hwnd_, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
    MARGINS all{-1, -1, -1, -1};
    DwmExtendFrameIntoClientArea(hwnd_, &all);
    // Glass, where Windows' transparency effects are on too: the panel lets a little through.
    glass_ = options_.glass && PersonalizeValue(L"EnableTransparency", 1) != 0;
}

// Draws through a buffered paint, which keeps the alpha the see-through window needs.
void Switcher::Paint() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd_, &ps);
    RECT rc;
    GetClientRect(hwnd_, &rc);
    BP_PAINTPARAMS params{sizeof(params), BPPF_ERASE, nullptr, nullptr};
    HDC buffer = nullptr;
    if (HPAINTBUFFER paint = BeginBufferedPaint(hdc, &rc, BPBF_TOPDOWNDIB, &params, &buffer)) {
        if (target_ && SUCCEEDED(target_->BindDC(buffer, &rc))) Draw();
        EndBufferedPaint(paint, TRUE);
    }
    EndPaint(hwnd_, &ps);
}

void Switcher::DrawIcon(ID2D1Bitmap* bitmap, D2D1_RECT_F dest) {
    if (!bitmap) return;
    if (context_) {
        context_->DrawBitmap(bitmap, &dest, 1.f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC,
                             nullptr, nullptr);
    } else {
        target_->DrawBitmap(bitmap, &dest, 1.f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, nullptr);
    }
}

// A preview cell: DWM puts the thumbnail in the box; this draws the title above it, and
// the app's icon on a plate where the window gives no thumbnail.
void Switcher::DrawPreview(size_t i, ID2D1SolidColorBrush* brush) {
    int index = first_ + static_cast<int>(i);
    const D2D1_RECT_F& cell = cells_[i];
    Entry& e = entries_[index];
    D2D1_RECT_F box = PreviewBox(cell);
    if (i >= thumbnails_.size() || !thumbnails_[i].id) {
        if (auto shape = Squircle(d2d_.get(), box, kRadius)) {
            brush->SetColor(Color(look_.hover));
            target_->FillGeometry(shape.get(), brush);
        }
        if (!e.big) e.big = IconFor(e.hwnd, kFallbackIcon);
        float x = (box.left + box.right - kFallbackIcon) / 2;
        float y = (box.top + box.bottom - kFallbackIcon) / 2;
        DrawIcon(e.big.get(), {x, y, x + kFallbackIcon, y + kFallbackIcon});
    }
    bool title = options_.titles == Titles::all ||
                 (options_.titles == Titles::selected && index == selected_);
    if (!title) return;
    float top = cell.top + kPreviewPad;
    D2D1_RECT_F bar{box.left, top, box.right, top + TitleRow()};
    if (!e.icon) e.icon = IconFor(e.hwnd, line_);
    float iconTop = (bar.top + bar.bottom - line_) / 2;
    DrawIcon(e.icon.get(), {bar.left + 4, iconTop, bar.left + 4 + line_, iconTop + line_});
    brush->SetColor(Color(look_.text));
    // Under the mouse the row's right end holds the close button.
    float right = bar.right - 4;
    if (index == hover_) {
        D2D1_ELLIPSE b = ButtonAt(cell);
        right = b.point.x - b.radiusX - 6;
    }
    D2D1_RECT_F line{bar.left + 4 + line_ + 6, bar.top, right, bar.bottom};
    target_->DrawText(e.title.c_str(), static_cast<UINT32>(e.title.size()), font_.get(), &line,
                      brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void Switcher::Draw() {
    target_->BeginDraw();
    target_->Clear(Color(0));
    target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

    com_ptr<ID2D1SolidColorBrush> brush;
    target_->CreateSolidColorBrush(Color(look_.text), brush.put());
    // The panel: G2 corners concentric with the cells' (their radius plus the padding),
    // Bloom's window color and its hairline.
    if (auto panel = Squircle(d2d_.get(), {0.5f, 0.5f, width_ - 0.5f, height_ - 0.5f},
                              kRadius + kPanelPad)) {
        uint32_t alpha = glass_ ? kGlassAlpha : 0xFF;
        brush->SetColor(Color((look_.solid & 0x00FFFFFF) | (alpha << 24)));
        target_->FillGeometry(panel.get(), brush.get());
        COLORREF c = look_.border;
        brush->SetColor(Color(0xFF000000u | GetRValue(c) << 16 | GetGValue(c) << 8 | GetBValue(c)));
        target_->DrawGeometry(panel.get(), brush.get(), 1.f);
    }
    float icon = static_cast<float>(kIconSizes[options_.size]);
    for (size_t i = 0; i < cells_.size(); i++) {
        int index = first_ + static_cast<int>(i);
        const D2D1_RECT_F& cell = cells_[i];
        Entry& e = entries_[index];
        // The chosen window: a fill and an accent outline all round, as Bloom marks what is on.
        if (index == selected_ || index == hover_) {
            auto shape = Squircle(d2d_.get(), cell, kRadius);
            brush->SetColor(Color(index == selected_ ? look_.selected : look_.hover));
            if (shape) target_->FillGeometry(shape.get(), brush.get());
            if (index == selected_) {
                float in = kRing / 2;
                auto ring = Squircle(
                    d2d_.get(),
                    {cell.left + in, cell.top + in, cell.right - in, cell.bottom - in},
                    kRadius - in);
                brush->SetColor(Color(look_.accent));
                if (ring) target_->DrawGeometry(ring.get(), brush.get(), kRing);
            }
        }
        if (Previews()) {
            DrawPreview(i, brush.get());
            continue;
        }
        if (!e.icon) e.icon = IconFor(e.hwnd, icon);
        float left = (cell.left + cell.right - icon) / 2, top = cell.top + kItemPad;
        DrawIcon(e.icon.get(), {left, top, left + icon, top + icon});
        if (options_.titles == Titles::all) {
            brush->SetColor(Color(look_.text));
            float y = top + icon + kTitleGap;
            D2D1_RECT_F line{cell.left + 6, y, cell.right - 6, y + line_};
            target_->DrawText(e.title.c_str(), static_cast<UINT32>(e.title.size()), font_.get(),
                              &line, brush.get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
    }
    if (!Previews() && options_.titles == Titles::selected) {
        const auto& title = entries_[selected_].title;
        float y = height_ - kPanelPad - line_ - 2;
        brush->SetColor(Color(look_.text));
        D2D1_RECT_F line{kPanelPad + 8, y, width_ - kPanelPad - 8, y + line_};
        target_->DrawText(title.c_str(), static_cast<UINT32>(title.size()), font_.get(), &line,
                          brush.get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    if (target_->EndDraw() == D2DERR_RECREATE_TARGET) {
        // A software target should never lose its device; drop the icons drawn on it.
        for (auto& e : entries_) e.icon = e.big = nullptr;
        icons_.clear();
    }
}

// Makes everything for one opening and shows the panel once painted, without activating it.
bool Switcher::Open() {
    look_ = DarkTheme(options_.theme) ? kDark : kLight;
    AccentPair accent = CurrentAccent();
    look_.accent = look_.dark ? accent.dark : accent.light;
    HMONITOR monitor = GetAltTabTargetMonitor();
    if (!monitor) monitor = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
    monitor_ = monitor;
    RECT r = Layout(monitor);
    PlaceCells();
    if (!Make()) return false;
    HINSTANCE module = reinterpret_cast<HINSTANCE>(&__ImageBase);
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = Proc;
    wc.hInstance = module;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);  // fails harmlessly when it is there from the last opening
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kClassName, L"", WS_POPUP, r.left,
                            r.top, r.right - r.left, r.bottom - r.top, nullptr, nullptr, module,
                            this);
    if (!hwnd_) return false;
    ApplyBackdrop();
    ShowThumbnails();
    // Hidden from the screen until its first frame is there.
    BOOL cloak = TRUE;
    DwmSetWindowAttribute(hwnd_, DWMWA_CLOAK, &cloak, sizeof(cloak));
    ShowWindow(hwnd_, SW_SHOWNA);
    UpdateWindow(hwnd_);
    // Painted: out at once, before it is made active, which waits on a busy front window.
    // Window previews first take a frame of DWM's, else their boxes would show empty for one.
    GdiFlush();
    if (!thumbnails_.empty()) DwmFlush();
    cloak = FALSE;
    DwmSetWindowAttribute(hwnd_, DWMWA_CLOAK, &cloak, sizeof(cloak));
    return true;
}

void Switcher::Start(bool back, bool win) {
    if (hwnd_) return;
    options_ = CurrentOptions();
    entries_ = ListWindows(win, nullptr);
    if (entries_.empty()) {
        g_active = false;
        return;
    }
    // Windows starts on the window before the one in front.
    HWND front = GetForegroundWindow();
    int n = static_cast<int>(entries_.size());
    selected_ = entries_[0].hwnd == front && n > 1 ? 1 : 0;
    if (back) selected_ = n - 1;
    hover_ = -1;
    before_ = front;

    // Alt already up: a quick Alt+Tab switches without making anything.
    if (GetAsyncKeyState(VK_MENU) >= 0) {
        Activate(entries_[selected_].hwnd, true);
        Close();
        return;
    }
    if (!Open()) {
        Close();
        return;
    }
    // Active, as Windows' switcher is, so that it closes when another window takes over.
    Activate(hwnd_, false);
    SetTimer(hwnd_, kAltTimer, 50, nullptr);
}

void Switcher::Step(int delta) {
    int n = static_cast<int>(entries_.size());
    selected_ = ((selected_ + delta) % n + n) % n;
    if (selected_ < first_ || selected_ >= first_ + static_cast<int>(cells_.size())) {
        PlaceCells();
        ShowThumbnails();  // the new page's windows
        HoverAtCursor();
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// index < 0: the selected one.
void Switcher::Commit(int index) {
    HWND target = entries_[index < 0 ? selected_ : index].hwnd;
    switching_ = true;
    if (IsWindow(target)) Activate(target, true);
    Close();
}

void Switcher::Cancel() {
    switching_ = true;
    if (before_ && IsWindow(before_)) Activate(before_, false);
    Close();
}

// Lets go of everything the opening made.
void Switcher::Close() {
    DropThumbnails();
    if (HWND w = hwnd_) {
        hwnd_ = nullptr;  // the window's last messages go to DefWindowProc
        DestroyWindow(w);  // its timers and the close button's window go with it
    }
    button_ = nullptr;
    buttonHot_ = false;
    hover_ = -1;
    std::vector<HWND>().swap(closing_);
    Free();
    std::vector<Entry>().swap(entries_);
    std::vector<D2D1_RECT_F>().swap(cells_);
    monitor_ = nullptr;
    before_ = nullptr;
    switching_ = false;
    g_active = false;
}

// pt in client pixels.
int Switcher::HitTest(POINT pt) const {
    float scale = dpi_ / 96;
    float x = pt.x / scale, y = pt.y / scale;
    for (size_t i = 0; i < cells_.size(); i++) {
        const auto& c = cells_[i];
        if (x >= c.left && x < c.right && y >= c.top && y < c.bottom) {
            return first_ + static_cast<int>(i);
        }
    }
    return -1;
}

// ---- closing windows -------------------------------------------------------------------

// The window under the mouse (pt in client pixels, off the panel for none): its cell lit
// and its close button out, the button lit while the mouse is on it. again: the cells
// moved, so the button goes where they are now.
void Switcher::Hover(POINT pt, bool again) {
    int hit = HitTest(pt);
    bool hot = hit >= 0 && OnButton(hit, pt);
    if (hit != hover_) {
        hover_ = hit;
        if (hit >= 0) {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd_, 0};
            TrackMouseEvent(&track);
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        again = true;
    }
    if (again || hot != buttonHot_) {
        buttonHot_ = hot;
        ShowButton();
    }
}

// After the cells moved under a mouse that may not move.
void Switcher::HoverAtCursor() {
    POINT pt{};
    RECT client{};
    GetCursorPos(&pt);
    ScreenToClient(hwnd_, &pt);
    GetClientRect(hwnd_, &client);
    if (!PtInRect(&client, pt)) pt = {-1, -1};
    Hover(pt, true);
}

// A cell's close button, in DIPs: at the right end of a preview's title row, else in the
// cell's top right corner, over a preview's thumbnail where it has no title row.
D2D1_ELLIPSE Switcher::ButtonAt(const D2D1_RECT_F& cell) const {
    if (Previews() && options_.titles != Titles::none) {
        float r = (TitleRow() - 4) / 2;
        return {{cell.right - kPreviewPad - r, cell.top + kPreviewPad + TitleRow() / 2}, r, r};
    }
    float r = kButtonSizes[options_.size] / 2;
    float in = (Previews() ? kPreviewPad : 0) + kButtonInset;
    return {{cell.right - in - r, cell.top + in + r}, r, r};
}

// pt in client pixels; the button takes a little more than it shows.
bool Switcher::OnButton(int index, POINT pt) const {
    int i = index - first_;
    if (i < 0 || i >= static_cast<int>(cells_.size())) return false;
    D2D1_ELLIPSE b = ButtonAt(cells_[i]);
    float scale = dpi_ / 96, r = b.radiusX + kButtonGrace;
    float dx = pt.x / scale - b.point.x, dy = pt.y / scale - b.point.y;
    return dx * dx + dy * dy <= r * r;
}

// A round button with a cross: Bloom's button color and edge, red with a white cross while
// the mouse is on it, as for anything that cannot be undone.
void Switcher::DrawButton(D2D1_POINT_2F center, float radius, bool hot) {
    com_ptr<ID2D1SolidColorBrush> brush;
    target_->CreateSolidColorBrush(Color(hot ? look_.danger : look_.button), brush.put());
    if (!brush) return;
    target_->FillEllipse({center, radius, radius}, brush.get());
    if (!hot) {
        brush->SetColor(Color(look_.edge));
        target_->DrawEllipse({center, radius - 0.5f, radius - 0.5f}, brush.get(), 1.f);
    }
    brush->SetColor(Color(hot ? kDangerInk : look_.text));
    float a = radius * kCrossArm;
    target_->DrawLine({center.x - a, center.y - a}, {center.x + a, center.y + a}, brush.get(),
                      kCrossWidth, round_.get());
    target_->DrawLine({center.x - a, center.y + a}, {center.x + a, center.y - a}, brush.get(),
                      kCrossWidth, round_.get());
}

// The close button sits in a small window of its own above the switcher's: DWM draws the
// window previews over the switcher's own pixels, and the button has to show above them.
// The mouse goes through it to the switcher, which knows where the button is (OnButton).
// It is made on the first hover and goes with the switcher's window.
void Switcher::ShowButton() {
    int i = hover_ - first_;
    if (hover_ < 0 || i < 0 || i >= static_cast<int>(cells_.size()) || !target_) {
        if (button_) ShowWindow(button_, SW_HIDE);
        return;
    }
    D2D1_ELLIPSE b = ButtonAt(cells_[i]);
    float scale = dpi_ / 96;
    // The button's square in pixels, with a pixel to spare all round for its smoothed edge.
    int x = static_cast<int>(std::floor((b.point.x - b.radiusX) * scale)) - 1;
    int y = static_cast<int>(std::floor((b.point.y - b.radiusY) * scale)) - 1;
    int size = static_cast<int>(std::ceil(2 * b.radiusX * scale)) + 3;
    if (!button_) {
        button_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW |
                                      WS_EX_NOACTIVATE | WS_EX_TOPMOST,
                                  kClassName, L"", WS_POPUP, 0, 0, 0, 0, hwnd_, nullptr,
                                  reinterpret_cast<HINSTANCE>(&__ImageBase), nullptr);
        if (!button_) return;
    }
    BITMAPINFO bi{};
    bi.bmiHeader = {sizeof(bi.bmiHeader), size, -size, 1, 32, BI_RGB};
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    if (HBITMAP dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0)) {
        HGDIOBJ old = SelectObject(dc, dib);
        RECT rc{0, 0, size, size};
        if (SUCCEEDED(target_->BindDC(dc, &rc))) {
            target_->BeginDraw();
            target_->Clear(Color(0));
            DrawButton({b.point.x - x / scale, b.point.y - y / scale}, b.radiusX, buttonHot_);
            target_->EndDraw();
            GdiFlush();
            POINT at{x, y}, from{};
            ClientToScreen(hwnd_, &at);
            SIZE extent{size, size};
            BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
            UpdateLayeredWindow(button_, screen, &at, &extent, dc, &from, 0, &blend, ULW_ALPHA);
            SetWindowPos(button_, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
        SelectObject(dc, old);
        DeleteObject(dib);
    }
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
}

// Asks a window to close as the taskbar's "Close window" does, so a program with unsaved
// work can ask first; nothing is ended by force. An elevated window does not take the
// message from Explorer. The window leaves the panel once it is gone (DropClosed).
void Switcher::CloseWindowAt(int index) {
    if (index < 0 || index >= static_cast<int>(entries_.size())) return;
    HWND w = entries_[index].hwnd;
    if (!PostMessageW(w, WM_SYSCOMMAND, SC_CLOSE, 0)) return;
    if (std::find(closing_.begin(), closing_.end(), w) == closing_.end()) closing_.push_back(w);
    closingSince_ = GetTickCount64();
    SetTimer(hwnd_, kCloseTimer, kCloseCheckMs, nullptr);
}

// Takes the windows that closed (or hid, as programs that stay in the tray do) off the
// panel and lays it out again for the rest; with none left the switcher closes.
void Switcher::DropClosed() {
    bool dropped = false;
    std::erase_if(closing_, [&](HWND w) {
        DWORD cloaked = 0;
        if (IsWindow(w) && IsWindowVisible(w) &&
            !(SUCCEEDED(DwmGetWindowAttribute(w, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) &&
              cloaked)) {
            return false;
        }
        auto it = std::find_if(entries_.begin(), entries_.end(),
                               [&](const Entry& e) { return e.hwnd == w; });
        if (it != entries_.end()) {
            if (it - entries_.begin() < selected_) selected_--;
            entries_.erase(it);
            dropped = true;
        }
        return true;
    });
    if (closing_.empty() || GetTickCount64() - closingSince_ > kCloseWaitMs) {
        closing_.clear();
        KillTimer(hwnd_, kCloseTimer);
    }
    if (!dropped) return;
    if (entries_.empty()) {
        Cancel();
        return;
    }
    selected_ = std::min(selected_, static_cast<int>(entries_.size()) - 1);
    Relayout();
}

// ---- threads ---------------------------------------------------------------------------

// The switcher runs on a thread of its own and the hook on another, so that listing
// windows and loading icons never holds up the keyboard. While the switcher is closed
// both threads only wait for messages.
HANDLE g_uiThread, g_hookThread;
DWORD g_uiThreadId, g_hookThreadId;
HANDLE g_ready;

void Post(UINT msg, WPARAM wp = 0, LPARAM lp = 0) {
    if (DWORD id = g_uiThreadId) PostThreadMessageW(id, msg, wp, lp);
}

bool Down(int vk) {
    return GetAsyncKeyState(vk) < 0;
}

// Takes a key for the switcher (true) or lets it through.
bool OnKey(WPARAM msg, const KBDLLHOOKSTRUCT& k) {
    bool down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
    if (k.vkCode == VK_TAB && down) {
        if (!g_active) {
            if (!Down(VK_MENU) || Down(VK_CONTROL) || !g_uiThreadId) return false;
            g_active = true;
            Post(kStart, Down(VK_SHIFT), Down(VK_LWIN) || Down(VK_RWIN));
            return true;
        }
        Post(kStep, static_cast<WPARAM>(Down(VK_SHIFT) ? -1 : 1));
        return true;
    }
    if (!g_active) return false;
    switch (k.vkCode) {
        case VK_MENU:
        case VK_LMENU:
        case VK_RMENU:
            if (!down) Post(kCommit);
            return false;  // Alt itself goes through
        case VK_ESCAPE:
            if (down) Post(kCancel);
            return true;
        case VK_DELETE:  // closes the chosen window, as in Windows' switcher
            if (down) Post(kClose);
            return true;
        case VK_RETURN:
        case VK_SPACE:
            if (down) Post(kCommit);
            return true;
        case VK_LEFT:
        case VK_RIGHT:
            if (down) Post(kStep, static_cast<WPARAM>(k.vkCode == VK_LEFT ? -1 : 1));
            return true;
        case VK_UP:
        case VK_DOWN:
            if (down) Post(kStep, static_cast<WPARAM>(k.vkCode == VK_UP ? -1 : 1), 1);
            return true;
    }
    return false;
}

LRESULT CALLBACK KeyboardHook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION) {
        const auto& k = *reinterpret_cast<KBDLLHOOKSTRUCT*>(lp);
        if (!(k.flags & LLKHF_INJECTED) && OnKey(wp, k)) return 1;
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

DWORD WINAPI HookThread(void*) {
    HHOOK hook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardHook,
                                   reinterpret_cast<HINSTANCE>(&__ImageBase), 0);
    if (!hook) Wh_Log(L"keyboard hook failed: %u", GetLastError());
    MSG msg;
    PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE);  // the queue Stop posts to
    SetEvent(g_ready);
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    }
    if (hook) UnhookWindowsHookEx(hook);
    return 0;
}

DWORD WINAPI UiThread(void*) {
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    BufferedPaintInit();
    Switcher switcher;
    // The order windows are used in (g_used), from the one in front now. The events come
    // as messages to this thread.
    g_used.clear();
    if (HWND front = GetForegroundWindow()) g_used.push_back(RootOwner(front));
    HWINEVENTHOOK events[] = {
        SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, OnWindowEvent,
                        0, 0, WINEVENT_OUTOFCONTEXT),
        SetWinEventHook(EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZESTART, nullptr,
                        OnWindowEvent, 0, 0, WINEVENT_OUTOFCONTEXT),
    };
    MSG msg;
    PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE);  // the queue the hook posts to
    SetEvent(g_ready);
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.hwnd) {
            DispatchMessageW(&msg);
            continue;
        }
        if (msg.message == kStopUi) break;
        switch (msg.message) {
            case kStart:
                switcher.Start(msg.wParam != 0, msg.lParam != 0);
                break;
            case kStep:
                if (switcher.IsOpen()) {
                    switcher.Step(static_cast<int>(msg.wParam) *
                                  (msg.lParam ? switcher.Cols() : 1));
                }
                break;
            case kCommit:
                if (switcher.IsOpen()) switcher.Commit(-1);
                break;
            case kCancel:
                if (switcher.IsOpen()) switcher.Cancel();
                break;
            case kClose:
                if (switcher.IsOpen()) switcher.CloseSelected();
                break;
        }
    }
    for (HWINEVENTHOOK h : events) {
        if (h) UnhookWinEvent(h);
    }
    std::vector<HWND>().swap(g_used);
    switcher.Close();
    UnregisterClassW(kClassName, reinterpret_cast<HINSTANCE>(&__ImageBase));
    BufferedPaintUnInit();
    g_active = false;
    if (SUCCEEDED(com)) CoUninitialize();
    return 0;
}

// Starts a thread and waits until its message queue is there.
HANDLE StartThread(LPTHREAD_START_ROUTINE proc, DWORD* id) {
    g_ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE thread = CreateThread(nullptr, 0, proc, nullptr, 0, id);
    if (thread) WaitForSingleObject(g_ready, 5000);
    CloseHandle(g_ready);
    g_ready = nullptr;
    return thread;
}

void StopThread(HANDLE& thread, DWORD& id, UINT quit) {
    if (!thread) return;
    PostThreadMessageW(id, quit, 0, 0);
    WaitForSingleObject(thread, 5000);
    CloseHandle(thread);
    thread = nullptr;
    id = 0;
}

void Stop() {
    StopThread(g_hookThread, g_hookThreadId, WM_QUIT);
    StopThread(g_uiThread, g_uiThreadId, kStopUi);
}

void Start() {
    if (g_uiThread) return;
    DWORD id = 0;
    g_uiThread = StartThread(UiThread, &id);
    g_uiThreadId = g_uiThread ? id : 0;
    if (g_uiThread) g_hookThread = StartThread(HookThread, &g_hookThreadId);
}

// Runs the switcher while the settings ask for it.
void Apply() {
    Options o = ReadOptions();
    AcquireSRWLockExclusive(&g_optionsLock);
    g_options = o;
    ReleaseSRWLockExclusive(&g_optionsLock);
    if (o.style != Style::windows) {
        Start();
    } else {
        Stop();
    }
}

}  // namespace sw

BOOL Wh_ModInit() {
    if (!AltTabPerMonitor_ModInit()) return FALSE;
    sw::Apply();
    return TRUE;
}

void Wh_ModUninit() {
    sw::Stop();
}

void Wh_ModSettingsChanged() {
    AltTabPerMonitor_ModSettingsChanged();
    sw::Apply();
}

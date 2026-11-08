// SPDX-License-Identifier: GPL-3.0-only
// ==WindhawkMod==
// @id              alt-tab-per-monitor
// @name            Alt+Tab per monitor
// @description     Alt+Tab on the monitor under the cursor, with that monitor's windows, in Windows' switcher or in a switcher of window previews or icons
// @version         1.2.1
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -lversion -ld2d1 -ldwmapi -ldwrite -lgdi32 -lshcore -lshell32 -lshlwapi -luuid -luxtheme -lwindowscodecs
// ==/WindhawkMod==

// ==WindhawkModSettings==
/*
- switcher:
  - style: windows
    $name: 표시 방식
    $description: 창 미리보기와 아이콘 전환기는 화면을 덮는 배경 없이 이 애드온이 그립니다. 관리자 권한 창이 앞에 있을 때는 Windows 전환기가 뜹니다
    $options:
    - windows: Windows 전환기
    - previews: 창 미리보기 (배경 없이 창만)
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
    $description: 끄면 아이콘 전환기가 블러 없이 테마 색으로 칠해집니다. Bloom 애드온으로 쓰면 Bloom의 유리 효과를 따릅니다
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
// windows as live previews (DWM thumbnails on a see-through window, nothing behind them)
// or as app icons on a panel. The look is Bloom's: its colors, the DWM acrylic Bloom's
// windows use, rounded corners, Segoe UI. It holds memory only while it is open (see
// Switcher).

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
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

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

// Bloom's tokens (Ui/Theme/Palette.cs): zinc grays and its default rose accent, used only
// for what is on (the chosen window).
struct Look {
    bool dark;
    uint32_t solid, text, selected, hover, accent;
    COLORREF border;  // the hairline DWM draws, as Bloom's glass windows have it
};
constexpr Look kDark{true,       0xFF18181B, 0xFFECEDEE, 0x1AFFFFFF,
                     0x14FFFFFF, 0xFFFF7A9E, RGB(0x35, 0x2E, 0x31)};
constexpr Look kLight{false,      0xFFF4F4F5, 0xFF11181C, 0x1A111111,
                      0x14111111, 0xFFE04E78, RGB(0xD9, 0xD2, 0xD5)};
// Over the acrylic the window color shows at 70 % (Bloom's 유리 투명도 default of 30 %).
constexpr uint32_t kGlassAlpha = 0xB3;
// The previews have no acrylic behind them; their title plates take the window color at
// 85 % so that the text reads over any desktop.
constexpr uint32_t kPlateAlpha = 0xD9;

D2D1_COLOR_F Color(uint32_t argb) {
    return {((argb >> 16) & 0xFF) / 255.f, ((argb >> 8) & 0xFF) / 255.f, (argb & 0xFF) / 255.f,
            (argb >> 24) / 255.f};
}

// Sizes in DIPs.
constexpr float kPanelPad = 10, kGap = 6, kItemPad = 12, kRadius = 11, kRing = 2;
constexpr float kFontSize = 12, kLineHeight = 16, kTitleGap = 8, kAllTitlesMinWidth = 140;
constexpr float kFooterHeight = kTitleGap + kLineHeight + 4;
// Previews: the box a thumbnail fits in is 16:10; a title plate with a small icon above it.
constexpr float kPreviewPad = 6, kPreviewGap = 12, kPlateHeight = 26, kPlateRadius = 8;
constexpr float kTitleIcon = 16, kFallbackIcon = 48;

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
        std::vector<Entry>* out;
    };
    std::vector<Entry> out;
    Ctx ctx{self, all ? nullptr : cursor, &out};
    // Z order: the windows used last come first, minimized ones last.
    EnumWindows(
        [](HWND w, LPARAM lp) -> BOOL {
            auto& c = *reinterpret_cast<Ctx*>(lp);
            if (!IsAltTabWindow(w, c.self)) return TRUE;
            if (c.monitor && WindowMonitor(w) != c.monitor) return TRUE;
            wchar_t title[256];
            int n = GetWindowTextW(w, title, ARRAYSIZE(title));
            c.out->push_back({w, std::wstring(title, std::max(n, 0)), nullptr, nullptr});
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&ctx));
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

HICON WindowIcon(HWND w) {
    DWORD_PTR icon = 0;
    for (WPARAM kind : {ICON_BIG, ICON_SMALL2}) {
        if (SendMessageTimeoutW(w, WM_GETICON, kind, 0, SMTO_ABORTIFHUNG, 100, &icon) && icon) {
            return reinterpret_cast<HICON>(icon);
        }
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
    void ShowThumbnails();
    void DropThumbnails();
    void ApplyBackdrop();
    void Paint();
    void Draw();
    void DrawIcon(ID2D1Bitmap* bitmap, D2D1_RECT_F dest);
    void DrawPreview(size_t cell, ID2D1SolidColorBrush* brush);
    D2D1_RECT_F PreviewBox(const D2D1_RECT_F& cell) const;
    int HitTest(LPARAM lp) const;

    bool Previews() const { return options_.style == Style::previews; }

    HWND hwnd_ = nullptr;
    Options options_;
    Look look_ = kDark;
    bool glass_ = false;
    std::vector<Entry> entries_;
    int selected_ = 0, hover_ = -1;
    bool switching_ = false;
    HWND before_ = nullptr;

    // Layout in DIPs.
    float dpi_ = 96, cellW_ = 0, cellH_ = 0, footer_ = 0, width_ = 0, height_ = 0, gap_ = 0;
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
    kStopUi,
};
enum : UINT_PTR { kRevealTimer = 1, kAltTimer = 2 };
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
            if (wp == kRevealTimer) {
                KillTimer(hwnd_, kRevealTimer);
                BOOL cloak = FALSE;
                DwmSetWindowAttribute(hwnd_, DWMWA_CLOAK, &cloak, sizeof(cloak));
            } else if (wp == kAltTimer && GetAsyncKeyState(VK_MENU) >= 0) {
                // The hook missed Alt going up (an elevated window took the keys).
                Commit(-1);
            }
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE && !switching_) Close();
            return 0;
        case WM_MOUSEMOVE: {
            int hit = HitTest(lp);
            if (hit != hover_) {
                hover_ = hit;
                TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd_, 0};
                TrackMouseEvent(&track);
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            if (hover_ != -1) {
                hover_ = -1;
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONUP:
            if (int hit = HitTest(lp); hit >= 0) Commit(hit);
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
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(dwrite_.put()))) ||
        FAILED(dwrite_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                         DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                         kFontSize, L"", font_.put())) ||
        FAILED(dwrite_->CreateEllipsisTrimmingSign(font_.get(), ellipsis_.put()))) {
        return false;
    }
    // Icons have their titles centered under them, previews on a plate after a small icon.
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

    if (Previews()) {
        // A box per window with its title plate above, every title in the same row
        // whether shown or not, so that the boxes line up.
        float box = static_cast<float>(kPreviewHeights[options_.size]);
        cellW_ = std::round(box * 1.6f) + 2 * kPreviewPad;
        cellH_ = box + 2 * kPreviewPad;
        if (options_.titles != Titles::none) cellH_ += kPlateHeight + kPreviewPad;
        footer_ = 0;
        gap_ = kPreviewGap;
    } else {
        float icon = static_cast<float>(kIconSizes[options_.size]);
        cellW_ = icon + 2 * kItemPad;
        cellH_ = icon + 2 * kItemPad;
        if (options_.titles == Titles::all) {
            cellW_ = std::max(cellW_, kAllTitlesMinWidth);
            cellH_ += kTitleGap + kLineHeight;
        }
        footer_ = options_.titles == Titles::selected ? kFooterHeight : 0;
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

// The box a cell's thumbnail fits in, under its title plate.
D2D1_RECT_F Switcher::PreviewBox(const D2D1_RECT_F& cell) const {
    float top = cell.top + kPreviewPad;
    if (options_.titles != Titles::none) top += kPlateHeight + kPreviewPad;
    return {cell.left + kPreviewPad, top, cell.right - kPreviewPad, cell.bottom - kPreviewPad};
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

void Switcher::ApplyBackdrop() {
    BOOL noAnimation = TRUE;
    DwmSetWindowAttribute(hwnd_, DWMWA_TRANSITIONS_FORCEDISABLED, &noAnimation,
                          sizeof(noAnimation));
    if (Previews()) {
        // See-through: no backdrop, corners or border, the frame spread over the whole
        // window so that its pixels' alpha counts. Only the windows and their marks show.
        glass_ = false;
        DWM_WINDOW_CORNER_PREFERENCE square = DWMWCP_DONOTROUND;
        DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &square, sizeof(square));
        COLORREF none = DWMWA_COLOR_NONE;
        DwmSetWindowAttribute(hwnd_, DWMWA_BORDER_COLOR, &none, sizeof(none));
        DWM_SYSTEMBACKDROP_TYPE backdrop = DWMSBT_NONE;
        DwmSetWindowAttribute(hwnd_, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
        MARGINS all{-1, -1, -1, -1};
        DwmExtendFrameIntoClientArea(hwnd_, &all);
        return;
    }
    DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    BOOL dark = look_.dark;
    DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    COLORREF border = look_.border;
    DwmSetWindowAttribute(hwnd_, DWMWA_BORDER_COLOR, &border, sizeof(border));
    // Acrylic as Bloom's windows have it, where Windows' transparency effects are on.
    glass_ = options_.glass && PersonalizeValue(L"EnableTransparency", 1) != 0;
    DWM_SYSTEMBACKDROP_TYPE backdrop = glass_ ? DWMSBT_TRANSIENTWINDOW : DWMSBT_NONE;
    if (FAILED(DwmSetWindowAttribute(hwnd_, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop,
                                     sizeof(backdrop)))) {
        glass_ = false;  // before Windows 11 22H2
    }
    MARGINS margins = glass_ ? MARGINS{-1, -1, -1, -1} : MARGINS{};
    DwmExtendFrameIntoClientArea(hwnd_, &margins);
}

// Draws through a buffered paint, which keeps the alpha the acrylic shows through.
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

// A preview cell: DWM puts the thumbnail in the box; this draws the title plate above it,
// and the app's icon on a plate where the window gives no thumbnail.
void Switcher::DrawPreview(size_t i, ID2D1SolidColorBrush* brush) {
    int index = first_ + static_cast<int>(i);
    const D2D1_RECT_F& cell = cells_[i];
    Entry& e = entries_[index];
    uint32_t plate = (look_.solid & 0x00FFFFFF) | (kPlateAlpha << 24);
    D2D1_RECT_F box = PreviewBox(cell);
    if (i >= thumbnails_.size() || !thumbnails_[i].id) {
        if (auto shape = Squircle(d2d_.get(), box, kRadius)) {
            brush->SetColor(Color(plate));
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
    D2D1_RECT_F bar{box.left, top, box.right, top + kPlateHeight};
    if (auto shape = Squircle(d2d_.get(), bar, kPlateRadius)) {
        brush->SetColor(Color(plate));
        target_->FillGeometry(shape.get(), brush);
    }
    if (!e.icon) e.icon = IconFor(e.hwnd, kTitleIcon);
    float iconTop = (bar.top + bar.bottom - kTitleIcon) / 2;
    DrawIcon(e.icon.get(),
             {bar.left + 6, iconTop, bar.left + 6 + kTitleIcon, iconTop + kTitleIcon});
    brush->SetColor(Color(look_.text));
    D2D1_RECT_F line{bar.left + 6 + kTitleIcon + 6, bar.top, bar.right - 8, bar.bottom};
    target_->DrawText(e.title.c_str(), static_cast<UINT32>(e.title.size()), font_.get(), &line,
                      brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void Switcher::Draw() {
    target_->BeginDraw();
    // Previews: nothing behind the windows.
    uint32_t ground = Previews() ? 0
                      : glass_   ? (look_.solid & 0x00FFFFFF) | (kGlassAlpha << 24)
                                 : look_.solid;
    target_->Clear(Color(ground));
    target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

    com_ptr<ID2D1SolidColorBrush> brush;
    target_->CreateSolidColorBrush(Color(look_.text), brush.put());
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
            D2D1_RECT_F line{cell.left + 6, y, cell.right - 6, y + kLineHeight};
            target_->DrawText(e.title.c_str(), static_cast<UINT32>(e.title.size()), font_.get(),
                              &line, brush.get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
    }
    if (!Previews() && options_.titles == Titles::selected) {
        const auto& title = entries_[selected_].title;
        float y = height_ - kPanelPad - kLineHeight - 2;
        brush->SetColor(Color(look_.text));
        D2D1_RECT_F line{kPanelPad + 8, y, width_ - kPanelPad - 8, y + kLineHeight};
        target_->DrawText(title.c_str(), static_cast<UINT32>(title.size()), font_.get(), &line,
                          brush.get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    if (target_->EndDraw() == D2DERR_RECREATE_TARGET) {
        // A software target should never lose its device; drop the icons drawn on it.
        for (auto& e : entries_) e.icon = e.big = nullptr;
        icons_.clear();
    }
}

// Makes everything for one opening and shows the panel cloaked, without activating it.
bool Switcher::Open() {
    look_ = DarkTheme(options_.theme) ? kDark : kLight;
    HMONITOR monitor = GetAltTabTargetMonitor();
    if (!monitor) monitor = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
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
    // Active, as Windows' switcher is: the acrylic draws only on an active window.
    Activate(hwnd_, false);
    SetTimer(hwnd_, kRevealTimer, 30, nullptr);
    SetTimer(hwnd_, kAltTimer, 50, nullptr);
}

void Switcher::Step(int delta) {
    int n = static_cast<int>(entries_.size());
    selected_ = ((selected_ + delta) % n + n) % n;
    if (selected_ < first_ || selected_ >= first_ + static_cast<int>(cells_.size())) {
        PlaceCells();
        ShowThumbnails();  // the new page's windows
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
        DestroyWindow(w);  // its timers go with it
    }
    Free();
    std::vector<Entry>().swap(entries_);
    std::vector<D2D1_RECT_F>().swap(cells_);
    before_ = nullptr;
    switching_ = false;
    g_active = false;
}

int Switcher::HitTest(LPARAM lp) const {
    float scale = dpi_ / 96;
    float x = GET_X_LPARAM(lp) / scale, y = GET_Y_LPARAM(lp) / scale;
    for (size_t i = 0; i < cells_.size(); i++) {
        const auto& c = cells_[i];
        if (x >= c.left && x < c.right && y >= c.top && y < c.bottom) {
            return first_ + static_cast<int>(i);
        }
    }
    return -1;
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
        }
    }
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

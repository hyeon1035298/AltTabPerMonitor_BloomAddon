// SPDX-License-Identifier: GPL-3.0-only
// ==WindhawkMod==
// @id              alt-tab-per-monitor
// @name            Alt+Tab per monitor
// @description     Alt+Tab on the monitor under the cursor, with that monitor's windows, in Windows' switcher or in an icon switcher
// @version         1.2.1
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -lversion -ld2d1 -ld3d11 -ldcomp -ldwmapi -ldwrite -ldxgi -lgdi32 -lshcore -lshell32 -lshlwapi -luuid -lwindowscodecs
// ==/WindhawkMod==

// ==WindhawkModSettings==
/*
- switcher:
  - style: windows
    $name: 표시 방식
    $description: 아이콘 전환기는 창 미리보기 대신 앱 아이콘을 보입니다. 관리자 권한 창이 앞에 있을 때는 Windows 전환기가 뜹니다
    $options:
    - windows: Windows 전환기 (창 미리보기)
    - icons: 아이콘 전환기
  - iconSize: medium
    $name: 아이콘 크기
    $description: 아이콘 전환기에서
    $options:
    - small: 작게 (32)
    - medium: 보통 (48)
    - large: 크게 (64)
    - huge: 아주 크게 (96)
  - titles: selected
    $name: 창 이름
    $description: 아이콘 전환기에서
    $options:
    - none: 보이지 않음
    - selected: 고른 창만 (아이콘 아래 한 줄)
    - all: 모두 (아이콘마다)
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
    $description: 아이콘 전환기의 라이트·다크. Bloom 애드온으로 쓰면 Bloom의 테마를 따릅니다
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
// MIT, included below unchanged) and an icon switcher of the add-on's own that can stand
// in for Windows' switcher. The settings sit in groups so that Bloom draws a page for them;
// the included mod reads its four keys from the "monitor" group.
//
// The icon switcher: a low-level keyboard hook on a thread of its own takes Alt+Tab,
// Alt+Shift+Tab and Win+Alt+Tab (Ctrl+Alt+Tab stays Windows') and hands every key to a
// second thread that lists the windows, draws them and switches. While an elevated window
// is in front the hook gets no keys, and Windows' switcher comes as before. The look is
// Bloom's: its colors, the DWM acrylic Bloom's windows use, rounded corners, Segoe UI.

#include <appmodel.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dwmapi.h>
#include <dwrite.h>
#include <dxgi1_2.h>
#include <propsys.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <shlobj.h>
#include <shlwapi.h>
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

enum class Titles { none, selected, all };
enum class Theme { system, dark, light };

struct Options {
    bool icons = false;
    int iconSize = 48;
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
    o.icons = Is(v, L"icons");
    Wh_FreeStringSetting(v);
    v = Wh_GetStringSetting(L"switcher.iconSize");
    o.iconSize = Is(v, L"small") ? 32 : Is(v, L"large") ? 64 : Is(v, L"huge") ? 96 : 48;
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

D2D1_COLOR_F Color(uint32_t argb) {
    return {((argb >> 16) & 0xFF) / 255.f, ((argb >> 8) & 0xFF) / 255.f, (argb & 0xFF) / 255.f,
            (argb >> 24) / 255.f};
}

// Sizes in DIPs.
constexpr float kPanelPad = 10, kGap = 6, kItemPad = 12, kRadius = 11, kRing = 2;
constexpr float kFontSize = 12, kLineHeight = 16, kTitleGap = 8, kAllTitlesMinWidth = 140;
constexpr float kFooterHeight = kTitleGap + kLineHeight + 4;

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
    com_ptr<ID2D1Bitmap> icon;
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
            c.out->push_back({w, std::wstring(title, std::max(n, 0)), nullptr});
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
            float t = (c.swap ? kSteps - i : i) * 1.5707964f / kSteps;
            D2D1_POINT_2F p{c.cx + c.sx * a * std::pow(std::cos(t), kExponent),
                            c.cy + c.sy * a * std::pow(std::sin(t), kExponent)};
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

class Switcher {
   public:
    bool Create();
    void Destroy();
    HWND Hwnd() const { return hwnd_; }

   private:
    static LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);

    void Start(bool back, bool win);
    void Step(int delta);
    void Commit(int index);
    void Cancel();
    void Hide();
    int HitTest(LPARAM lp) const;

    bool EnsureDevice();
    void DropDevice();
    com_ptr<ID2D1Bitmap> IconFor(HWND w);
    com_ptr<ID2D1Bitmap> FromIcon(HICON icon);
    com_ptr<ID2D1Bitmap> FromBitmap(HBITMAP bitmap);
    void Layout(HMONITOR monitor);
    void Draw();
    void ApplyBackdrop();

    HWND hwnd_ = nullptr;
    Options options_;
    Look look_ = kDark;
    bool glass_ = false;
    std::vector<Entry> entries_;
    int selected_ = 0, hover_ = -1;
    bool shown_ = false, switching_ = false;
    HWND before_ = nullptr;

    // Layout in DIPs.
    float dpi_ = 96, cellW_ = 0, cellH_ = 0, footer_ = 0;
    int cols_ = 1, perPage_ = 1;
    std::vector<D2D1_RECT_F> cells_;  // the current page's cells
    int first_ = 0;                   // the entry in the first cell
    UINT width_ = 0, height_ = 0;     // pixels

    com_ptr<ID3D11Device> d3d_;
    com_ptr<ID2D1Factory1> d2d_;
    com_ptr<ID2D1DeviceContext> dc_;
    com_ptr<IDXGISwapChain1> chain_;
    com_ptr<IDCompositionDevice> comp_;
    com_ptr<IDCompositionTarget> target_;
    com_ptr<IDCompositionVisual> visual_;
    com_ptr<IDWriteFactory> dwrite_;
    com_ptr<IDWriteTextFormat> font_;
    com_ptr<IDWriteInlineObject> ellipsis_;
    com_ptr<IWICImagingFactory> wic_;
    std::unordered_map<std::wstring, com_ptr<ID2D1Bitmap>> cache_;
};

enum : UINT {
    kStart = WM_APP + 1,  // wParam: 1 back (Shift), lParam: 1 Win held
    kStep,                // wParam: steps as int, lParam: 1 by rows
    kCommit,
    kCancel,
    kReload,
};
enum : UINT_PTR { kRevealTimer = 1, kAltTimer = 2 };
constexpr wchar_t kClassName[] = L"AltTabPerMonitor.IconSwitcher";

bool Switcher::Create() {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = Proc;
    wc.hInstance = reinterpret_cast<HINSTANCE>(&__ImageBase);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOREDIRECTIONBITMAP,
                            kClassName, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr,
                            wc.hInstance, this);
    if (!hwnd_) return false;
    BOOL noAnimation = TRUE;
    DwmSetWindowAttribute(hwnd_, DWMWA_TRANSITIONS_FORCEDISABLED, &noAnimation,
                          sizeof(noAnimation));
    DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    return true;
}

void Switcher::Destroy() {
    DropDevice();
    if (hwnd_) DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    UnregisterClassW(kClassName, reinterpret_cast<HINSTANCE>(&__ImageBase));
}

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

// Set by the hook thread when it takes a Tab, cleared here once the switcher is done.
std::atomic<bool> g_active{false};

LRESULT Switcher::Handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case kStart:
            Start(wp != 0, lp != 0);
            return 0;
        case kStep:
            if (shown_) Step(static_cast<int>(wp) * (lp ? cols_ : 1));
            return 0;
        case kCommit:
            if (shown_) Commit(selected_);
            return 0;
        case kCancel:
            if (shown_) Cancel();
            return 0;
        case kReload:
            cache_.clear();
            return 0;
        case WM_TIMER:
            if (wp == kRevealTimer) {
                KillTimer(hwnd_, kRevealTimer);
                BOOL cloak = FALSE;
                DwmSetWindowAttribute(hwnd_, DWMWA_CLOAK, &cloak, sizeof(cloak));
            } else if (wp == kAltTimer && shown_ && GetAsyncKeyState(VK_MENU) >= 0) {
                // The hook missed Alt going up (an elevated window took the keys).
                Commit(selected_);
            }
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE && shown_ && !switching_) Hide();
            return 0;
        case WM_MOUSEMOVE: {
            int hit = HitTest(lp);
            if (hit != hover_) {
                hover_ = hit;
                TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd_, 0};
                TrackMouseEvent(&track);
                Draw();
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            if (hover_ != -1) {
                hover_ = -1;
                Draw();
            }
            return 0;
        case WM_LBUTTONUP: {
            int hit = HitTest(lp);
            if (shown_ && hit >= 0) Commit(hit);
            return 0;
        }
        case WM_SYSCOMMAND:
            if ((wp & 0xFFF0) == SC_KEYMENU) return 0;  // Alt going up
            break;
        case WM_DPICHANGED:
            return 0;  // each opening lays itself out for its monitor
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

bool Switcher::EnsureDevice() {
    if (dc_) return true;
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0,
                                 D3D11_SDK_VERSION, d3d_.put(), nullptr, nullptr)) &&
        FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0,
                                 D3D11_SDK_VERSION, d3d_.put(), nullptr, nullptr))) {
        return false;
    }
    auto dxgi = d3d_.try_as<IDXGIDevice>();
    if (!dxgi) {
        DropDevice();
        return false;
    }
    D2D1_FACTORY_OPTIONS factoryOptions{};
    com_ptr<ID2D1Device> device;
    com_ptr<IDXGIAdapter> adapter;
    com_ptr<IDXGIFactory2> dxgiFactory;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1),
                                 &factoryOptions, d2d_.put_void())) ||
        FAILED(d2d_->CreateDevice(dxgi.get(), device.put())) ||
        FAILED(device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, dc_.put())) ||
        FAILED(dxgi->GetAdapter(adapter.put())) ||
        FAILED(adapter->GetParent(IID_PPV_ARGS(dxgiFactory.put())))) {
        DropDevice();
        return false;
    }
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = desc.Height = 8;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    if (FAILED(dxgiFactory->CreateSwapChainForComposition(d3d_.get(), &desc, nullptr,
                                                          chain_.put())) ||
        FAILED(DCompositionCreateDevice(dxgi.get(), IID_PPV_ARGS(comp_.put()))) ||
        FAILED(comp_->CreateTargetForHwnd(hwnd_, TRUE, target_.put())) ||
        FAILED(comp_->CreateVisual(visual_.put())) ||
        FAILED(visual_->SetContent(chain_.get())) || FAILED(target_->SetRoot(visual_.get())) ||
        FAILED(comp_->Commit())) {
        DropDevice();
        return false;
    }
    width_ = height_ = 8;
    if (!dwrite_) {
        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                       reinterpret_cast<IUnknown**>(dwrite_.put()))) ||
            FAILED(dwrite_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                             DWRITE_FONT_STYLE_NORMAL,
                                             DWRITE_FONT_STRETCH_NORMAL, kFontSize, L"",
                                             font_.put())) ||
            FAILED(dwrite_->CreateEllipsisTrimmingSign(font_.get(), ellipsis_.put()))) {
            dwrite_ = nullptr;
            DropDevice();
            return false;
        }
        font_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        font_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        font_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        font_->SetTrimming(&trimming, ellipsis_.get());
    }
    if (!wic_ && FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                         IID_PPV_ARGS(wic_.put())))) {
        DropDevice();
        return false;
    }
    return true;
}

void Switcher::DropDevice() {
    cache_.clear();
    for (auto& e : entries_) e.icon = nullptr;
    visual_ = nullptr;
    target_ = nullptr;
    comp_ = nullptr;
    chain_ = nullptr;
    dc_ = nullptr;
    d2d_ = nullptr;
    d3d_ = nullptr;
    width_ = height_ = 0;
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
        static_cast<ID2D1RenderTarget*>(dc_.get())
            ->CreateBitmapFromWicBitmap(converter.get(), nullptr, bitmap.put());
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
        static_cast<ID2D1RenderTarget*>(dc_.get())
            ->CreateBitmapFromWicBitmap(converter.get(), nullptr, bitmap.put());
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

com_ptr<ID2D1Bitmap> Switcher::IconFor(HWND w) {
    int px = static_cast<int>(std::lround(options_.iconSize * dpi_ / 96));
    auto cached = [&](std::wstring key, auto&& make) -> com_ptr<ID2D1Bitmap> {
        key += L'|' + std::to_wstring(px);
        if (auto it = cache_.find(key); it != cache_.end()) return it->second;
        com_ptr<ID2D1Bitmap> bitmap = make();
        if (bitmap) cache_.emplace(std::move(key), bitmap);
        return bitmap;
    };
    auto fromLocation = [&](const std::wstring& location) {
        return cached(L"icon:" + location, [&] {
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
        auto bitmap = cached(L"app:" + aumid, [&] {
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
    return cached(L"default", [&] { return FromIcon(LoadIconW(nullptr, IDI_APPLICATION)); });
}

void Switcher::Layout(HMONITOR monitor) {
    UINT dx = 96, dy = 96;
    GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dx, &dy);
    dpi_ = static_cast<float>(dx);
    MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(monitor, &info);
    const RECT& work = info.rcWork;
    float scale = dpi_ / 96;
    float workW = (work.right - work.left) / scale, workH = (work.bottom - work.top) / scale;

    float icon = static_cast<float>(options_.iconSize);
    cellW_ = icon + 2 * kItemPad;
    cellH_ = icon + 2 * kItemPad;
    if (options_.titles == Titles::all) {
        cellW_ = std::max(cellW_, kAllTitlesMinWidth);
        cellH_ += kTitleGap + kLineHeight;
    }
    footer_ = options_.titles == Titles::selected ? kFooterHeight : 0;

    int n = static_cast<int>(entries_.size());
    float maxW = workW * 0.9f - 2 * kPanelPad, maxH = workH * 0.8f - 2 * kPanelPad - footer_;
    cols_ = std::clamp(static_cast<int>((maxW + kGap) / (cellW_ + kGap)), 1, n);
    int rows = (n + cols_ - 1) / cols_;
    int maxRows = std::max(1, static_cast<int>((maxH + kGap) / (cellH_ + kGap)));
    rows = std::min(rows, maxRows);
    perPage_ = cols_ * rows;

    float w = 2 * kPanelPad + cols_ * cellW_ + (cols_ - 1) * kGap;
    float h = 2 * kPanelPad + rows * cellH_ + (rows - 1) * kGap + footer_;
    width_ = static_cast<UINT>(std::lround(w * scale));
    height_ = static_cast<UINT>(std::lround(h * scale));
    int x = work.left + (work.right - work.left - static_cast<int>(width_)) / 2;
    int y = work.top + (work.bottom - work.top - static_cast<int>(height_)) / 2;
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, width_, height_, SWP_NOACTIVATE);
    dc_->SetTarget(nullptr);
    chain_->ResizeBuffers(2, width_, height_, DXGI_FORMAT_UNKNOWN, 0);
}

void Switcher::ApplyBackdrop() {
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

void Switcher::Draw() {
    if (!shown_ || !dc_) return;
    com_ptr<IDXGISurface> surface;
    com_ptr<ID2D1Bitmap1> target;
    D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpi_,
        dpi_);
    if (FAILED(chain_->GetBuffer(0, IID_PPV_ARGS(surface.put()))) ||
        FAILED(dc_->CreateBitmapFromDxgiSurface(surface.get(), &props, target.put()))) {
        return;
    }
    dc_->SetTarget(target.get());
    dc_->BeginDraw();
    uint32_t ground = glass_ ? (look_.solid & 0x00FFFFFF) | (kGlassAlpha << 24) : look_.solid;
    dc_->Clear(Color(ground));
    dc_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

    com_ptr<ID2D1SolidColorBrush> brush;
    dc_->CreateSolidColorBrush(Color(look_.text), brush.put());
    float icon = static_cast<float>(options_.iconSize);
    for (size_t i = 0; i < cells_.size(); i++) {
        int index = first_ + static_cast<int>(i);
        const D2D1_RECT_F& cell = cells_[i];
        Entry& e = entries_[index];
        if (index == selected_ || index == hover_) {
            auto shape = Squircle(d2d_.get(), cell, kRadius);
            brush->SetColor(Color(index == selected_ ? look_.selected : look_.hover));
            if (shape) dc_->FillGeometry(shape.get(), brush.get());
            if (index == selected_) {
                float in = kRing / 2;
                auto ring = Squircle(
                    d2d_.get(),
                    {cell.left + in, cell.top + in, cell.right - in, cell.bottom - in},
                    kRadius - in);
                brush->SetColor(Color(look_.accent));
                if (ring) dc_->DrawGeometry(ring.get(), brush.get(), kRing);
            }
        }
        if (!e.icon) e.icon = IconFor(e.hwnd);
        float left = (cell.left + cell.right - icon) / 2, top = cell.top + kItemPad;
        if (e.icon) {
            D2D1_RECT_F dest{left, top, left + icon, top + icon};
            dc_->DrawBitmap(e.icon.get(), &dest, 1.f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC,
                            nullptr, nullptr);
        }
        if (options_.titles == Titles::all) {
            brush->SetColor(Color(look_.text));
            float y = top + icon + kTitleGap;
            D2D1_RECT_F line{cell.left + 6, y, cell.right - 6, y + kLineHeight};
            dc_->DrawText(e.title.c_str(), static_cast<UINT32>(e.title.size()), font_.get(),
                          &line, brush.get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
    }
    if (options_.titles == Titles::selected) {
        const auto& title = entries_[selected_].title;
        float scale = dpi_ / 96, w = width_ / scale, h = height_ / scale;
        float y = h - kPanelPad - kLineHeight - 2;
        brush->SetColor(Color(look_.text));
        D2D1_RECT_F line{kPanelPad + 8, y, w - kPanelPad - 8, y + kLineHeight};
        dc_->DrawText(title.c_str(), static_cast<UINT32>(title.size()), font_.get(), &line,
                      brush.get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    HRESULT hr = dc_->EndDraw();
    dc_->SetTarget(nullptr);
    if (hr == D2DERR_RECREATE_TARGET) {
        DropDevice();
        return;
    }
    chain_->Present(1, 0);
}

// Puts the selected entry's page in the cells.
void PlaceCells(std::vector<D2D1_RECT_F>& cells, int& first, int selected, int count, int cols,
                int perPage, float cellW, float cellH) {
    first = selected / perPage * perPage;
    int shown = std::min(perPage, count - first);
    cells.clear();
    for (int i = 0; i < shown; i++) {
        int row = i / cols, col = i % cols;
        // A short last row sits in the middle.
        int inRow = std::min(cols, shown - row * cols);
        float x = kPanelPad + (cols - inRow) * (cellW + kGap) / 2 + col * (cellW + kGap);
        float y = kPanelPad + row * (cellH + kGap);
        cells.push_back(D2D1::RectF(x, y, x + cellW, y + cellH));
    }
}

void Switcher::Start(bool back, bool win) {
    if (shown_) return;
    options_ = CurrentOptions();
    look_ = DarkTheme(options_.theme) ? kDark : kLight;
    entries_ = ListWindows(win, hwnd_);
    if (entries_.empty() || !EnsureDevice()) {
        entries_.clear();
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

    // Alt already up: a quick Alt+Tab switches without showing anything.
    if (GetAsyncKeyState(VK_MENU) >= 0) {
        shown_ = true;
        Commit(selected_);
        return;
    }

    HMONITOR monitor = GetAltTabTargetMonitor();
    if (!monitor) monitor = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
    Layout(monitor);
    PlaceCells(cells_, first_, selected_, n, cols_, perPage_, cellW_, cellH_);
    ApplyBackdrop();
    shown_ = true;
    Draw();
    if (!dc_) {  // the device went away while drawing
        Hide();
        return;
    }
    // Hidden from the screen until its first frame is there, then active, as the acrylic
    // draws only on an active window.
    BOOL cloak = TRUE;
    DwmSetWindowAttribute(hwnd_, DWMWA_CLOAK, &cloak, sizeof(cloak));
    ShowWindow(hwnd_, SW_SHOW);
    Activate(hwnd_, false);
    SetTimer(hwnd_, kRevealTimer, 30, nullptr);
    SetTimer(hwnd_, kAltTimer, 50, nullptr);
}

void Switcher::Step(int delta) {
    int n = static_cast<int>(entries_.size());
    selected_ = ((selected_ + delta) % n + n) % n;
    if (selected_ < first_ || selected_ >= first_ + static_cast<int>(cells_.size())) {
        PlaceCells(cells_, first_, selected_, n, cols_, perPage_, cellW_, cellH_);
    }
    Draw();
}

void Switcher::Commit(int index) {
    HWND target = entries_[index].hwnd;
    switching_ = true;
    if (IsWindow(target)) Activate(target, true);
    Hide();
    switching_ = false;
}

void Switcher::Cancel() {
    switching_ = true;
    if (before_ && IsWindow(before_)) Activate(before_, false);
    Hide();
    switching_ = false;
}

void Switcher::Hide() {
    KillTimer(hwnd_, kRevealTimer);
    KillTimer(hwnd_, kAltTimer);
    if (IsWindowVisible(hwnd_)) ShowWindow(hwnd_, SW_HIDE);
    BOOL cloak = FALSE;
    DwmSetWindowAttribute(hwnd_, DWMWA_CLOAK, &cloak, sizeof(cloak));
    shown_ = false;
    entries_.clear();
    cells_.clear();
    before_ = nullptr;
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

// The switcher's window lives on its own thread; the hook on another, so that listing
// windows and loading icons never holds up the keyboard.
HANDLE g_uiThread, g_hookThread;
DWORD g_hookThreadId;
std::atomic<HWND> g_window{nullptr};
HANDLE g_uiReady, g_hookReady;
constexpr UINT kStopUi = WM_APP + 100;

void Post(UINT msg, WPARAM wp = 0, LPARAM lp = 0) {
    if (HWND w = g_window) PostMessageW(w, msg, wp, lp);
}

bool Down(int vk) {
    return GetAsyncKeyState(vk) < 0;
}

// Takes a key for the switcher (true) or lets it through.
bool OnKey(WPARAM msg, const KBDLLHOOKSTRUCT& k) {
    bool down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
    if (k.vkCode == VK_TAB && down) {
        if (!g_active) {
            if (!Down(VK_MENU) || Down(VK_CONTROL) || !g_window) return false;
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
    SetEvent(g_hookReady);
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    }
    if (hook) UnhookWindowsHookEx(hook);
    return 0;
}

DWORD WINAPI UiThread(void*) {
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Switcher switcher;
    if (switcher.Create()) g_window = switcher.Hwnd();
    SetEvent(g_uiReady);
    if (g_window) {
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (msg.message == kStopUi) break;
            DispatchMessageW(&msg);
        }
    }
    g_window = nullptr;
    switcher.Destroy();
    g_active = false;
    if (SUCCEEDED(com)) CoUninitialize();
    return 0;
}

void Stop() {
    if (g_hookThread) {
        PostThreadMessageW(g_hookThreadId, WM_QUIT, 0, 0);
        WaitForSingleObject(g_hookThread, 5000);
        CloseHandle(g_hookThread);
        g_hookThread = nullptr;
    }
    if (g_uiThread) {
        PostThreadMessageW(GetThreadId(g_uiThread), kStopUi, 0, 0);
        WaitForSingleObject(g_uiThread, 5000);
        CloseHandle(g_uiThread);
        g_uiThread = nullptr;
    }
}

void Start() {
    if (g_uiThread) return;
    g_uiReady = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_uiThread = CreateThread(nullptr, 0, UiThread, nullptr, 0, nullptr);
    if (g_uiThread) WaitForSingleObject(g_uiReady, 5000);
    CloseHandle(g_uiReady);
    g_uiReady = nullptr;
    if (!g_window) {
        Wh_Log(L"icon switcher window failed");
        Stop();
        return;
    }
    g_hookReady = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_hookThread = CreateThread(nullptr, 0, HookThread, nullptr, 0, &g_hookThreadId);
    if (g_hookThread) WaitForSingleObject(g_hookReady, 5000);
    CloseHandle(g_hookReady);
    g_hookReady = nullptr;
}

// Runs the switcher while the settings ask for it.
void Apply() {
    Options o = ReadOptions();
    AcquireSRWLockExclusive(&g_optionsLock);
    g_options = o;
    ReleaseSRWLockExclusive(&g_optionsLock);
    if (o.icons) {
        Start();
        Post(kReload);
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

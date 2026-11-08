# Alt+Tab per monitor (Bloom add-on)

[한국어](README.ko.md)

With more than one monitor, Windows opens the Alt+Tab switcher on the primary monitor and lists the windows of every monitor. This add-on for the [Bloom](https://github.com/hyeon1035298/Bloom) launcher opens the switcher on the monitor under the mouse cursor and lists only the windows on that monitor. Win+Alt+Tab does the same.

It can also replace Windows' switcher with an icon switcher: app icons instead of window previews, in Bloom's look (its colors, light or dark and glass as Bloom has them, rounded corners), with a choice of icon size and of which window titles to show.

The monitor part is the Windhawk mod [Alt+Tab per monitor](https://github.com/ramensoftware/windhawk-mods/blob/main/mods/alt-tab-per-monitor.wh.cpp) by L3r0y and Michael Maltsev, running without Windhawk. `mod/alt-tab-per-monitor.wh.cpp` is the upstream file, unchanged; `mod/bloom-alt-tab.wh.cpp` is this add-on's own code, which includes it, puts its settings in groups for Bloom's settings page and adds the icon switcher. The add-on runtime and host from [PillTaskbar](https://github.com/hyeon1035298/PillTaskbar_BloomAddon) load them into Explorer: the host finds the internal functions the mod hooks, injects a small runtime that provides the Windhawk API the mod uses, and keeps it running.

## Requirements

- Windows 11, 64-bit (the mod also supports Windows 10, but the add-on has only been used on Windows 11)
- Bloom 0.19.0 or later
- An internet connection on the first start (see below)
- If you use Windhawk: the same mod turned off there (see below)

## Install

1. Download `AltTabPerMonitor_BloomAddon-<version>.zip` from [Releases](https://github.com/hyeon1035298/AltTabPerMonitor_BloomAddon/releases).
2. Unzip it into `%LOCALAPPDATA%\Bloom\user-addons\alt-tab-per-monitor\`. The folder name must be `alt-tab-per-monitor`.
3. Restart Bloom, open Settings › Add-ons and turn on "Alt+Tab per monitor".

Bloom checks every file against the SHA-256 hashes in `addon.json` before it starts the add-on. To remove the add-on, turn it off and delete the folder.

### First start

The mod hooks internal functions of `twinui.pcshell.dll`, so the host needs that DLL's symbols. On the first start, and again after a Windows update changes the DLL, it downloads the matching public symbol file from Microsoft's symbol server and keeps only the offsets. Nothing from Microsoft ships with the add-on.

### Windhawk

The add-on does not start while the same mod is on in Windhawk, so the two never hook the same functions. Bloom then shows "On in Windhawk". Turn the mod off in Windhawk. If you no longer use Windhawk, you can instead set its service to manual start and close Windhawk, or uninstall it.

## Settings

The settings are on the add-on's page in Bloom (Settings › Add-ons › Alt+Tab per monitor) and apply at once.

| Key | Built-in | Other values |
|---|---|---|
| `switcher.style` | `windows` | `icons` |
| `switcher.iconSize` | `medium` (48) | `small` (32), `large` (64), `huge` (96) |
| `switcher.titles` | `selected` | `none`, `all` |
| `monitor.altTabLocation` | `cursor` | `primary` |
| `monitor.altTabWindows` | `cursorMonitor` | `allMonitors`, `cursorMonitorExceptPrimary` |
| `monitor.winAltTabLocation` | `cursor` | `primary` |
| `monitor.winAltTabWindows` | `cursorMonitor` | `allMonitors`, `cursorMonitorExceptPrimary` |

`switcher.style` picks Windows' switcher with window previews or the icon switcher. The other two `switcher` keys shape the icon switcher; `titles` shows no titles, the chosen window's title under the icons, or a title under every icon. `Location` picks the monitor the switcher appears on. `Windows` picks which windows it lists; `cursorMonitorExceptPrimary` lists every window while the cursor is on the primary monitor. The two `winAltTab` keys apply to Win+Alt+Tab. The built-in values differ from the Windhawk mod's own defaults only in that Win+Alt+Tab also follows the cursor. The icon switcher's theme and glass follow Bloom's (`pill.theme` and `pill.glass`, which Bloom sets).

Version 1.2.1-3 and earlier read the four monitor keys without the `monitor.` prefix; set them again on the page after updating.

### The icon switcher

It takes Alt+Tab, Alt+Shift+Tab and Win+Alt+Tab with a low-level keyboard hook in Explorer. While Alt is held, Tab, Shift+Tab and the arrow keys move, Enter or Space switches, Esc closes it, and a click picks a window. Releasing Alt switches to the chosen window. Ctrl+Alt+Tab stays with Windows. The switcher's window, drawing and icons exist only while it is open and are freed when it closes; while closed it keeps no cache and runs no timer. While an elevated (administrator) window is in front, Windows does not hand Explorer's hook the keys, so Windows' own switcher appears, on the monitor set above. The accent is the one picked in Bloom (Settings › General › Look › Color), read from Bloom's `settings.json` with Bloom's own light and dark pair; a change shows from the next Alt+Tab. Without Bloom's settings the Windows accent is used.

## Build from source

You need a clang that targets `x86_64-w64-mingw32` (llvm-mingw, or the compiler that ships with Windhawk), Python 3, and a bash shell such as Git Bash. The build uses PillTaskbar's add-on tools, so clone both repositories side by side:

```
git clone https://github.com/hyeon1035298/AltTabPerMonitor_BloomAddon
git clone https://github.com/hyeon1035298/PillTaskbar_BloomAddon
cd AltTabPerMonitor_BloomAddon
export CXX=/path/to/llvm-mingw/bin/clang++.exe   # optional, defaults to Windhawk's compiler
bash build.sh
```

`PILL_ROOT` points `build.sh` at a PillTaskbar_BloomAddon checkout elsewhere. llvm-mingw has no C++/WinRT headers; set `CPPWINRT_INCLUDE` to a folder that the Windows SDK's `cppwinrt.exe` wrote, as the workflow does. The result lands in `build/addon`.

The workflow in `.github/workflows/build.yml` builds every push against the PillTaskbar_BloomAddon commit pinned in `PILL_REF`. A `v*` tag publishes a release with the zip, its SHA-256, and a `SOURCE.txt` that names the commits of both repositories.

## License

The add-on is free software under the GNU General Public License, version 3 only. See `LICENSE`.

`mod/alt-tab-per-monitor.wh.cpp` is under the MIT license. The runtime and host bundle MinHook (BSD-2-Clause) and Zydis (MIT). Credits and license texts are in `THIRD_PARTY.md` and, in the release zip, `THIRD_PARTY_NOTICES.txt`.

This program comes with no warranty. It patches code inside explorer.exe, so use it at your own risk. Windows and Windhawk are trademarks of their owners. This project is not affiliated with Microsoft, with the Windhawk authors, or with the authors of the mod.

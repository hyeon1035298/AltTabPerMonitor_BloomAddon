# Alt+Tab per monitor add-on: sources and licenses

The add-on as a whole (this repository and its release zip) is distributed under the
GNU General Public License, version 3 only (`LICENSE`). It combines the parts below.

## The mod: Alt+Tab per monitor (MIT)

`mod/alt-tab-per-monitor.wh.cpp` is version 1.2.1 of the Windhawk mod
"Alt+Tab per monitor" from the windhawk-mods collection, unchanged:
https://github.com/ramensoftware/windhawk-mods/blob/main/mods/alt-tab-per-monitor.wh.cpp

`mod/bloom-alt-tab.wh.cpp` includes it, and the two are built into `AltTabPerMonitor.dll`. L3r0y (https://github.com/L3r0yThingz) wrote
the mod; Michael Maltsev (https://github.com/m417z) made the changes in versions 1.2
and 1.2.1.

The mod's header names no license. The windhawk-mods README states that mods which
don't specify a license are submitted under the MIT license, so the mod is used under
these terms:

```
MIT License

Copyright (c) 2024-2026 the authors of the Alt+Tab per monitor mod
(L3r0y, Michael Maltsev)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## The add-on's own mod code (GPL-3.0-only)

`mod/bloom-alt-tab.wh.cpp` was written for this add-on: the settings in groups for
Bloom's settings page and the icon switcher. It uses only Windows APIs (Direct2D,
DirectWrite, DWM, WIC, buffered paint, the shell, the Windows Runtime's JSON and UI
settings) and contains no code from other projects. Its colors and sizes are Bloom's
(https://github.com/hyeon1035298/Bloom, GPL-3.0-only), including the accent presets and
how a typed accent is lifted for the dark theme, which follow the color picked in
Bloom's settings. The Windhawk mod "Simple Window Switcher" suggested the icon switcher;
none of its code is used.

## The add-on runtime and host (GPL-3.0-only)

`AltTabPerMonitorRuntime.dll`, `AltTabPerMonitorHost.exe` and the add-on entry code
built into `AltTabPerMonitor.dll` come from PillTaskbar
(https://github.com/hyeon1035298/PillTaskbar_BloomAddon), GPL-3.0-only, built with this
add-on's names from `addon.conf`. A release zip's `SOURCE.txt` names the commit of this
repository and of PillTaskbar_BloomAddon it was built from.

The runtime implements the part of the Windhawk mod API that the mod uses. It was
written for PillTaskbar and follows the behavior of Windhawk
(https://github.com/ramensoftware/windhawk, GPL-3.0).

The runtime and host contain or link these libraries. Their license texts are in
`THIRD_PARTY_NOTICES.txt` next to the binaries.

| Library | License | Used for |
|---|---|---|
| [MinHook](https://github.com/TsudaKageyu/minhook) 1.3.4 (with HDE64) | BSD-2-Clause | function hooks |
| [Zydis](https://github.com/zyantific/zydis) / Zycore 4.1.0 amalgamation | MIT | `Wh_Disasm` |
| mingw-w64 runtime | see `COPYING.MinGW-w64-runtime.txt` | C runtime startup code |
| libc++, libunwind, compiler-rt (LLVM 20) | Apache-2.0 WITH LLVM-exception | the statically linked C++ runtime |
| [C++/WinRT](https://github.com/microsoft/cppwinrt) | MIT | headers the mod includes |

## Microsoft symbols

The host downloads Microsoft's public symbol file for `twinui.pcshell.dll` from the
Microsoft symbol server on first start and keeps only the offsets the mod needs.
Nothing from Microsoft is included in this repository or in the release zip.

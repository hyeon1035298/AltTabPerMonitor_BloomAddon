# 모니터별 Alt+Tab (Bloom 애드온)

[English](README.md)

모니터가 여럿일 때 Windows는 Alt+Tab 창을 주 모니터에 띄우고 모든 모니터의 창을 보여 줍니다. [Bloom](https://github.com/hyeon1035298/Bloom) 런처용 이 애드온은 Alt+Tab 창을 마우스 커서가 있는 모니터에 띄우고, 그 모니터의 창만 보여 줍니다. Win+Alt+Tab도 같습니다.

L3r0y와 Michael Maltsev가 만든 Windhawk 모드 [Alt+Tab per monitor](https://github.com/ramensoftware/windhawk-mods/blob/main/mods/alt-tab-per-monitor.wh.cpp)를 Windhawk 없이 돌립니다. `mod/`의 모드 소스는 원본 그대로입니다. [PillTaskbar](https://github.com/hyeon1035298/PillTaskbar_BloomAddon)의 애드온 런타임과 호스트가 이 모드를 탐색기에 올립니다.

## 필요한 것

- Windows 11, 64비트(모드는 Windows 10도 지원하지만 이 애드온은 Windows 11에서만 써 봤습니다)
- Bloom 0.19.0 이상
- 처음 켤 때 인터넷 연결(Microsoft 기호 파일을 받아 필요한 위치만 남깁니다)
- Windhawk를 쓴다면 거기서 같은 모드 끄기(켜져 있으면 이 애드온이 시작하지 않고 Bloom에 "Windhawk에서 켜져 있음"이 보입니다)

## 설치

1. [Releases](https://github.com/hyeon1035298/AltTabPerMonitor_BloomAddon/releases)에서 `AltTabPerMonitor_BloomAddon-<버전>.zip`을 받습니다.
2. `%LOCALAPPDATA%\Bloom\user-addons\alt-tab-per-monitor\`에 풉니다. 폴더 이름은 `alt-tab-per-monitor`여야 합니다.
3. Bloom을 다시 시작하고 설정 › 애드온에서 "모니터별 Alt+Tab"을 켭니다.

## 설정

Bloom에는 이 애드온의 설정 화면이 없습니다. 기본값은 Alt+Tab과 Win+Alt+Tab 모두 "커서가 있는 모니터에, 그 모니터의 창만"입니다(원래 모드는 Win+Alt+Tab이 주 모니터, 모든 창). 바꾸려면 `%APPDATA%\Bloom\addons\alt-tab-per-monitor\settings.json`에 바꿀 키만 적고 Bloom에서 애드온을 껐다 켭니다. 키와 값은 [README.md](README.md#settings)의 표에 있습니다.

```json
{ "winAltTabLocation": "primary", "winAltTabWindows": "allMonitors" }
```

## 빌드

[README.md](README.md#build-from-source)를 보세요. 이 저장소와 PillTaskbar_BloomAddon을 나란히 받고 `bash build.sh`를 실행합니다.

## 라이선스

GNU General Public License 3.0 only(`LICENSE`). `mod/`의 모드 소스는 MIT이고, 출처와 라이선스 전문은 `THIRD_PARTY.md`에 있습니다. 보증은 없습니다. 탐색기(explorer.exe) 안의 코드를 바꾸므로 본인 책임으로 쓰세요. Microsoft, Windhawk 개발자, 모드 작성자와 관계없는 프로젝트입니다.

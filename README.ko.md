# 모니터별 Alt+Tab (Bloom 애드온)

[English](README.md)

모니터가 여럿일 때 Windows는 Alt+Tab 창을 주 모니터에 띄우고 모든 모니터의 창을 보여 줍니다. [Bloom](https://github.com/hyeon1035298/Bloom) 런처용 이 애드온은 Alt+Tab 창을 마우스 커서가 있는 모니터에 띄우고, 그 모니터의 창만 보여 줍니다. Win+Alt+Tab도 같습니다.

Windows 전환기 대신 이 애드온이 Bloom 모양(Bloom의 색, Bloom을 따르는 라이트·다크, 둥근 모서리)으로 그리는 전환기를 쓸 수도 있습니다. 화면을 덮는 배경 없이 창 미리보기만 띄우는 방식과, Bloom의 유리 판 위에 앱 아이콘을 놓는 방식이 있고, 둘 다 크기와 창 이름 보이기를 고릅니다.

모니터 기능은 L3r0y와 Michael Maltsev가 만든 Windhawk 모드 [Alt+Tab per monitor](https://github.com/ramensoftware/windhawk-mods/blob/main/mods/alt-tab-per-monitor.wh.cpp)를 Windhawk 없이 돌린 것입니다. `mod/alt-tab-per-monitor.wh.cpp`는 원본 그대로이고, `mod/bloom-alt-tab.wh.cpp`는 이 애드온의 코드로 원본을 포함해 설정을 Bloom 설정 화면용 묶음으로 나누고 애드온의 전환기를 더합니다. [PillTaskbar](https://github.com/hyeon1035298/PillTaskbar_BloomAddon)의 애드온 런타임과 호스트가 이것을 탐색기에 올립니다.

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

Bloom 설정 › 애드온 › 모니터별 Alt+Tab 화면에서 바꾸며 바로 적용됩니다.

- 전환기: 표시 방식(Windows 전환기 / 창 미리보기 / 아이콘 전환기), 크기(작게·보통·크게·아주 크게, 기본 보통. 아이콘은 32·48·64·96픽셀, 미리보기는 높이 104·144·192·256픽셀), 창 이름(보이지 않음 / 고른 창만 / 모두, 기본 고른 창만). 크기와 창 이름은 창 미리보기와 아이콘 전환기가 함께 씁니다.
- 모니터: Alt+Tab과 Win+Alt+Tab 각각 나타날 모니터와 보일 창. 기본값은 둘 다 "커서가 있는 모니터에, 그 모니터의 창만"입니다(원래 모드는 Win+Alt+Tab이 주 모니터, 모든 창).
- 두 방식의 테마와 아이콘 전환기의 유리 효과는 Bloom 설정을 따릅니다. 강조색은 Bloom에서 고른 색(설정 › 일반 › 모양 › 색)을 Bloom의 `settings.json`에서 읽어 Bloom과 같은 라이트·다크 쌍으로 쓰며, 바꾸면 다음 Alt+Tab부터 보입니다. Bloom 설정이 없으면 Windows 강조색을 씁니다.

키와 값은 [README.md](README.md#settings)의 표에 있습니다. 1.2.1-3까지는 모니터 키 4개에 `monitor.`가 붙지 않았으므로, 업데이트 뒤 설정 화면에서 다시 골라 주세요.

### 애드온의 전환기

창 미리보기나 아이콘 전환기를 고르면 탐색기 안의 저수준 키보드 훅으로 Alt+Tab, Alt+Shift+Tab, Win+Alt+Tab을 받습니다. Alt를 누른 채 Tab, Shift+Tab, 화살표로 옮기고, Enter나 스페이스로 바로 넘어가고, Esc로 닫고, 클릭으로 고릅니다. Alt를 떼면 고른 창으로 넘어갑니다. Ctrl+Alt+Tab은 Windows 그대로입니다. 창 미리보기는 Windows가 그리는 DWM 썸네일(실시간 창 모습)을 투명한 창 위에 놓아 창과 제목, 고른 창의 표시만 보이고, 썸네일을 주지 않는 창(일부 최소화 창)은 아이콘으로 보입니다. 아이콘 전환기는 Bloom의 유리 판을 그립니다. 고른 창에는 옅은 채움 바탕과 강조색 윤곽이 붙습니다. 전환기의 창, 그리기 자원, 아이콘은 열려 있는 동안만 있고 닫으면 모두 풀며, 닫혀 있을 때는 캐시도 타이머도 없습니다. 관리자 권한 창이 앞에 있으면 Windows가 탐색기의 훅에 키를 주지 않아 Windows 전환기가 위 설정의 모니터에 뜹니다.

## 빌드

[README.md](README.md#build-from-source)를 보세요. 이 저장소와 PillTaskbar_BloomAddon을 나란히 받고 `bash build.sh`를 실행합니다.

## 라이선스

GNU General Public License 3.0 only(`LICENSE`). `mod/alt-tab-per-monitor.wh.cpp`는 MIT이고, 출처와 라이선스 전문은 `THIRD_PARTY.md`에 있습니다. 보증은 없습니다. 탐색기(explorer.exe) 안의 코드를 바꾸므로 본인 책임으로 쓰세요. Microsoft, Windhawk 개발자, 모드 작성자와 관계없는 프로젝트입니다.

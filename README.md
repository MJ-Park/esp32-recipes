# esp32-recipes

ESP32로 자주 하는 작업을 **검증된 레시피**로 모아 둔 저장소입니다.
Codex, Claude Code, Cursor 같은 AI 코딩 도구에 **레시피 이름만 알려 주면**, AI가 처음부터 새로 짜지 않고 이 코드를 그대로 가져다 붙입니다.
그래서 결과가 빠르고, 토큰도 적게 들고, 이미 실제 보드에서 확인된 코드라 믿을 수 있습니다.

| 레시피 | 하는 일 | 보드 |
|---|---|---|
| [lan-discovery](recipes/lan-discovery/) | 노트북이 **팀 ID로 ESP32의 IP를 자동으로 찾음**. IP 설정이 필요 없음 | ESP32 전 계열 |
| [camera-stream](recipes/camera-stream/) | ESP32-CAM 저지연 영상 스트리밍(QVGA 최대 26 fps, 기본 예제 대비 수 배) + 브라우저 대시보드 | AI Thinker ESP32-CAM |

---

## AI로 사용하기 (가장 빠른 방법)

아래 프롬프트를 복사해서 AI 도구에 붙여 넣으세요.
`team07`은 **우리 팀만의 ID**로 바꾸세요. 행사장에서 다른 팀과 겹치면 안 됩니다. 영문, 숫자, `-`, `_`만 쓸 수 있습니다.

**ESP32의 IP를 몰라도 노트북에서 찾고 싶을 때**
```text
https://github.com/MJ-Park/esp32-recipes 의 lan-discovery 레시피를 내 스케치에 적용해줘. 팀 ID는 team07.
```

**ESP32-CAM 영상을 노트북에서 보고 싶을 때**
```text
https://github.com/MJ-Park/esp32-recipes 의 camera-stream 레시피로 내 ESP32-CAM을 설정하고 대시보드까지 실행해줘. 팀 ID는 team07-cam.
```

**어떤 레시피가 필요한지 모를 때**
```text
https://github.com/MJ-Park/esp32-recipes 레시피 목록을 보고, 내 프로젝트(여기에 한 줄 설명)에 필요한 것만 골라 적용해줘.
```

**잘 안 될 때**
```text
esp32-recipes 의 lan-discovery 로 ESP32가 안 찾아져. 레시피 AGENTS.md 의 문제 해결 순서대로 점검해줘.
```

> **AI가 인터넷에 접속하지 못할 때** (예: Codex 기본 설정)
> 먼저 터미널에서 내려받은 뒤, 프롬프트에서 주소 대신 폴더를 가리키세요.
> ```bash
> git clone https://github.com/MJ-Park/esp32-recipes.git
> ```
> ```text
> ../esp32-recipes 의 lan-discovery 레시피를 이 스케치에 적용해줘. 팀 ID는 team07.
> ```

---

## 준비물

- **Arduino IDE 2.x** 또는 **arduino-cli**와 ESP32 보드 패키지 **3.x** (보드 매니저에서 `esp32 by Espressif` 설치)
- 노트북: **Python 3.8 이상**. 별도로 설치할 패키지는 없습니다.
- ESP32와 노트북이 **같은 2.4 GHz Wi-Fi**에 연결되어 있어야 합니다.

## AI 없이 직접 쓰기

각 레시피 폴더의 README에 3~4단계로 정리되어 있습니다.
- [lan-discovery 사용법](recipes/lan-discovery/README.md)
- [camera-stream 사용법](recipes/camera-stream/README.md)

## 메이커톤에서 자주 막히는 것

| 증상 | 해결 |
|---|---|
| 노트북이 ESP32를 못 찾음 | 둘 다 **같은 Wi-Fi**인지, **팀 ID가 똑같은지**(대소문자 포함) 확인하세요. |
| 같은 Wi-Fi인데도 전혀 안 됨 | 행사장 Wi-Fi의 **기기 간 통신 차단(클라이언트 격리)** 때문일 수 있습니다. **휴대폰 핫스팟**에 노트북과 ESP32를 둘 다 연결하세요. |
| ESP32가 Wi-Fi에 연결되지 않음 | ESP32는 **2.4 GHz만** 지원합니다. 5 GHz 전용 Wi-Fi에는 붙지 않습니다. |
| Windows에서 찾지 못함 | 처음 실행할 때 나오는 **방화벽 허용** 창에서 허용하세요. 네트워크 프로필은 '개인'이어야 합니다. |
| `UDP port 38271 is busy` | 노트북에서 탐색 프로그램(대시보드 포함)이 **이미 하나 실행 중**입니다. 하나만 켜 두세요. |
| 시리얼 모니터를 열면 보드가 재부팅됨 | 대부분의 ESP32 보드에서 정상 동작입니다(DTR/RTS 리셋). |

## 저장소 구조 (레시피 추가하기)

```
esp32-recipes/
├─ AGENTS.md            ← AI가 처음 읽는 레시피 목록과 공통 규칙
├─ recipes/<이름>/
│   ├─ README.md        ← 사람용 (한국어)
│   ├─ AGENTS.md        ← AI용: 어떤 파일을, 어디에, 몇 줄 넣는지
│   └─ src/, examples/, host/ …
└─ tests/               ← 하드웨어 없이 돌아가는 테스트
```

새 레시피를 추가할 때는 `recipes/<이름>/`에 README.md와 AGENTS.md를 만들고, 루트의 이 README와 AGENTS.md 목록에 한 줄씩 추가하면 됩니다.
실제 보드에서 확인한 결과(보드 종류, 코어 버전, 측정값)를 레시피 README에 적어 주세요.

## 라이선스

[MIT](LICENSE). 수업, 대회, 개인과 상업 프로젝트 모두 자유롭게 쓰고 고칠 수 있습니다.

# camera-stream: ESP32-CAM 저지연 스트리밍 + 대시보드

AI Thinker ESP32-CAM 영상을 노트북 브라우저에서 바로 봅니다.
IP를 몰라도 **팀 ID로 카메라를 자동으로 찾고**([lan-discovery](../lan-discovery/)), 기본 예제보다 훨씬 빠르게 끊김 없이 나옵니다.

| 측정 (QVGA 320×240, 같은 보드와 같은 공유기) | 기본 CameraWebServer 예제 | 이 레시피 |
|---|---|---|
| 프레임레이트 | 1~5 fps (크게 흔들림) | **18~26 fps** (Wi-Fi가 붐비는 시간대에는 6~10 fps) |
| 스트리밍 중 ping | 평균 1955 ms, 손실 10% | **평균 37~52 ms**, 손실 0% |
| 프레임 간격 p95 | 0.5~1.5초 | **약 100 ms** |

Wi-Fi 상태(채널 혼잡, 거리)에 따라 결과가 크게 달라집니다. 같은 날 같은 자리에서도 시간대에 따라 2~3배 차이가 났습니다.
LanDiscovery를 켰을 때와 껐을 때를 번갈아 측정해 봤는데 차이가 없었습니다. 탐색 기능은 영상 속도에 영향을 주지 않습니다.

## AI로 적용하기

```text
https://github.com/MJ-Park/esp32-recipes 의 camera-stream 레시피로 내 ESP32-CAM을 설정하고 대시보드까지 실행해줘. 팀 ID는 team07-cam.
```

이미 쓰고 있는 카메라 코드가 있을 때:
```text
https://github.com/MJ-Park/esp32-recipes 의 camera-stream 레시피에서 속도 개선 부분만 내 카메라 스케치에 적용해줘.
```

## 직접 적용하기

**1. 펌웨어 설정**: [firmware/CameraStream/CameraStream.ino](firmware/CameraStream/CameraStream.ino) 맨 위 값을 고칩니다.
```cpp
const char *WIFI_SSID = "";          // 비워 두면 보드에 저장된 Wi-Fi 사용
const char *WIFI_PASS = "";
const char *TEAM_ID = "team07-cam";  // 우리 팀 ID
```

**2. 업로드**: Arduino IDE에서 보드를 **AI Thinker ESP32-CAM**으로 선택하고 업로드합니다. 폴더 안의 파일 4개는 함께 있어야 합니다.

**3. 대시보드 실행** (노트북)
```bash
python3 dashboard/server.py --id team07-cam
```
브라우저에서 **http://localhost:8000** 을 엽니다.

## 대시보드에서 할 수 있는 것

- 실시간 영상, 프레임레이트, 해상도, 대역폭, Wi-Fi 신호, 응답 시간, 최근 60초 FPS 그래프
- 해상도, JPEG 품질, 밝기, 대비, 채도, 플래시 LED, 좌우/상하 반전, 자동 노출과 화이트밸런스 조절
- 스냅샷 저장
- 브라우저 탭을 여러 개 열어도 됩니다. 카메라 연결은 대시보드 서버가 하나만 유지하고 나눠 줍니다.

## 무엇이 빨라졌나

1. **Wi-Fi 절전 끄기**: 절전 모드에서는 패킷마다 최대 약 100 ms씩 기다립니다. 실측으로 ping이 20 ms에서 145 ms로 늘었습니다.
2. **프레임 버퍼 2개(PSRAM)와 `GRAB_LATEST`**: 항상 가장 최근 프레임을 보냅니다.
3. **프레임당 한 번에 전송, TCP_NODELAY**: 기본 예제는 프레임마다 작은 조각 3개를 따로 보냅니다.
4. XCLK는 20 MHz를 유지합니다. 10 MHz로 낮추면 오히려 나빠졌습니다.

## 카메라 HTTP 주소

| 주소 | 내용 |
|---|---|
| `http://<IP>/status` | 설정과 상태 (JSON) |
| `http://<IP>/control?var=framesize&val=10` | 설정 변경 (CameraWebServer와 같은 이름) |
| `http://<IP>/capture` | 사진 한 장 |
| `http://<IP>:81/stream` | MJPEG 영상. 한 번에 한 곳만 연결할 수 있으니 여러 곳에서 보려면 대시보드를 쓰세요. |

## 문제 해결

| 증상 | 확인할 것 |
|---|---|
| `Camera init failed` | 리본 케이블이 제대로 꽂혔는지, 보드 종류가 맞는지, 전원이 5 V 2 A 이상인지 |
| 영상이 나오다 보드가 재부팅됨 | 전원 부족(브라운아웃). 짧고 굵은 USB 케이블이나 별도 전원을 쓰세요. |
| fps가 낮거나 자주 끊김 | Wi-Fi 신호(대시보드의 dBm 값), 해상도 낮추기, 공유기 가까이 두기 |
| 대시보드가 카메라를 못 찾음 | [lan-discovery 문제 해결](../lan-discovery/README.md#한계)과 팀 ID 일치 여부 |

카메라에서 직접 성능을 재려면 대시보드를 끈 상태에서 아래 명령을 실행합니다:
```bash
python3 tools/streambench.py <카메라IP> 10
```

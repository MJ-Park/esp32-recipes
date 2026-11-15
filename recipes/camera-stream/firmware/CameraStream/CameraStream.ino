// esp32-recipes / camera-stream — low-latency MJPEG streaming for AI Thinker ESP32-CAM
// MIT License. https://github.com/MJ-Park/esp32-recipes
//
//   :80  /status  /control?var=..&val=..  /capture   (CameraWebServer-compatible)
//   :81  /stream  (multipart MJPEG)
//   UDP 38271     LanDiscovery: the laptop finds this board by TEAM_ID, no IP setup
//
// Why it is faster than the stock CameraWebServer example (measured QVGA: 1-5 fps -> 18-26 fps):
//   - Wi-Fi modem sleep OFF (sleep adds up to ~100 ms per packet; ping 20 -> 145 ms with it on)
//   - 2 frame buffers in PSRAM + CAMERA_GRAB_LATEST: always send the newest frame
//   - one socket write per frame with TCP_NODELAY instead of 3 small chunked writes

#include <WiFi.h>
#include "esp_camera.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include "LanDiscovery.h"   // copied from recipes/lan-discovery/src

// ------------------------------------------------------------ settings
const char *WIFI_SSID = "";          // empty = use the Wi-Fi already saved on this board
const char *WIFI_PASS = "";
const char *TEAM_ID = "team00-cam";  // unique per team; the laptop uses the same id
#define FW_VERSION "camera-stream/1.0.0"
#define LED_PIN 4                    // on-board flash LED

// AI Thinker ESP32-CAM pin map
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

LanDiscovery disc(TEAM_ID);
static httpd_handle_t http80 = NULL, http81 = NULL;
static int led_duty = 0;
static int fb_count_used = 1;
static volatile uint32_t stream_frames = 0;

// ------------------------------------------------------------ camera
static bool camera_init() {
  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer = LEDC_TIMER_0;
  c.pin_d0 = Y2_GPIO_NUM; c.pin_d1 = Y3_GPIO_NUM; c.pin_d2 = Y4_GPIO_NUM; c.pin_d3 = Y5_GPIO_NUM;
  c.pin_d4 = Y6_GPIO_NUM; c.pin_d5 = Y7_GPIO_NUM; c.pin_d6 = Y8_GPIO_NUM; c.pin_d7 = Y9_GPIO_NUM;
  c.pin_xclk = XCLK_GPIO_NUM; c.pin_pclk = PCLK_GPIO_NUM; c.pin_vsync = VSYNC_GPIO_NUM;
  c.pin_href = HREF_GPIO_NUM; c.pin_sccb_sda = SIOD_GPIO_NUM; c.pin_sccb_scl = SIOC_GPIO_NUM;
  c.pin_pwdn = PWDN_GPIO_NUM; c.pin_reset = RESET_GPIO_NUM;
  c.xclk_freq_hz = 20000000;  // 10 MHz measured worse for Wi-Fi, keep 20
  c.pixel_format = PIXFORMAT_JPEG;
  c.jpeg_quality = 12;
  if (psramFound()) {
    c.frame_size = FRAMESIZE_UXGA;  // allocate for the largest size once
    c.fb_count = 2;
    c.fb_location = CAMERA_FB_IN_PSRAM;
    c.grab_mode = CAMERA_GRAB_LATEST;
  } else {
    c.frame_size = FRAMESIZE_SVGA;
    c.fb_count = 1;
    c.fb_location = CAMERA_FB_IN_DRAM;
    c.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  }
  fb_count_used = c.fb_count;
  if (esp_camera_init(&c) != ESP_OK) return false;
  sensor_t *s = esp_camera_sensor_get();
  s->set_framesize(s, FRAMESIZE_QVGA);  // start small and fast; change from the dashboard
  return true;
}

// ------------------------------------------------------------ http :80
static esp_err_t send_json(httpd_req_t *req, const char *json) {
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t status_handler(httpd_req_t *req) {
  static char buf[1100];
  sensor_t *s = esp_camera_sensor_get();
  String ip = WiFi.localIP().toString();
  String host = disc.hasHost() ? disc.hostIP().toString() : String("");
  snprintf(buf, sizeof(buf),
    "{\"fw\":\"%s\",\"id\":\"%s\",\"ip\":\"%s\",\"rssi\":%d,\"channel\":%d,"
    "\"psram\":%s,\"fb_count\":%d,\"heap\":%u,\"uptime_s\":%llu,\"stream_frames\":%u,\"host\":\"%s\","
    "\"xclk\":%u,\"pixformat\":%u,\"framesize\":%u,\"quality\":%u,"
    "\"brightness\":%d,\"contrast\":%d,\"saturation\":%d,\"sharpness\":%d,"
    "\"special_effect\":%u,\"wb_mode\":%u,\"awb\":%u,\"awb_gain\":%u,\"aec\":%u,"
    "\"aec2\":%u,\"ae_level\":%d,\"aec_value\":%u,\"agc\":%u,\"agc_gain\":%u,"
    "\"gainceiling\":%u,\"bpc\":%u,\"wpc\":%u,\"raw_gma\":%u,\"lenc\":%u,"
    "\"hmirror\":%u,\"vflip\":%u,\"dcw\":%u,\"colorbar\":%u,\"led_intensity\":%d}",
    FW_VERSION, disc.id(), ip.c_str(), (int)WiFi.RSSI(), (int)WiFi.channel(),
    psramFound() ? "true" : "false", fb_count_used, (unsigned)ESP.getFreeHeap(),
    (unsigned long long)(esp_timer_get_time() / 1000000), (unsigned)stream_frames, host.c_str(),
    s->xclk_freq_hz / 1000000, s->pixformat, s->status.framesize, s->status.quality,
    s->status.brightness, s->status.contrast, s->status.saturation, s->status.sharpness,
    s->status.special_effect, s->status.wb_mode, s->status.awb, s->status.awb_gain, s->status.aec,
    s->status.aec2, s->status.ae_level, s->status.aec_value, s->status.agc, s->status.agc_gain,
    s->status.gainceiling, s->status.bpc, s->status.wpc, s->status.raw_gma, s->status.lenc,
    s->status.hmirror, s->status.vflip, s->status.dcw, s->status.colorbar, led_duty);
  return send_json(req, buf);
}

static esp_err_t control_handler(httpd_req_t *req) {
  char q[128], var[32], val[16];
  if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK ||
      httpd_query_key_value(q, "var", var, sizeof(var)) != ESP_OK ||
      httpd_query_key_value(q, "val", val, sizeof(val)) != ESP_OK) {
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }
  int v = atoi(val);
  sensor_t *s = esp_camera_sensor_get();
  int r = 0;
  if (!strcmp(var, "framesize")) { if (s->pixformat == PIXFORMAT_JPEG) r = s->set_framesize(s, (framesize_t)v); }
  else if (!strcmp(var, "quality")) r = s->set_quality(s, v);
  else if (!strcmp(var, "contrast")) r = s->set_contrast(s, v);
  else if (!strcmp(var, "brightness")) r = s->set_brightness(s, v);
  else if (!strcmp(var, "saturation")) r = s->set_saturation(s, v);
  else if (!strcmp(var, "sharpness")) r = s->set_sharpness(s, v);
  else if (!strcmp(var, "gainceiling")) r = s->set_gainceiling(s, (gainceiling_t)v);
  else if (!strcmp(var, "colorbar")) r = s->set_colorbar(s, v);
  else if (!strcmp(var, "awb")) r = s->set_whitebal(s, v);
  else if (!strcmp(var, "agc")) r = s->set_gain_ctrl(s, v);
  else if (!strcmp(var, "aec")) r = s->set_exposure_ctrl(s, v);
  else if (!strcmp(var, "hmirror")) r = s->set_hmirror(s, v);
  else if (!strcmp(var, "vflip")) r = s->set_vflip(s, v);
  else if (!strcmp(var, "awb_gain")) r = s->set_awb_gain(s, v);
  else if (!strcmp(var, "agc_gain")) r = s->set_agc_gain(s, v);
  else if (!strcmp(var, "aec_value")) r = s->set_aec_value(s, v);
  else if (!strcmp(var, "aec2")) r = s->set_aec2(s, v);
  else if (!strcmp(var, "dcw")) r = s->set_dcw(s, v);
  else if (!strcmp(var, "bpc")) r = s->set_bpc(s, v);
  else if (!strcmp(var, "wpc")) r = s->set_wpc(s, v);
  else if (!strcmp(var, "raw_gma")) r = s->set_raw_gma(s, v);
  else if (!strcmp(var, "lenc")) r = s->set_lenc(s, v);
  else if (!strcmp(var, "special_effect")) r = s->set_special_effect(s, v);
  else if (!strcmp(var, "wb_mode")) r = s->set_wb_mode(s, v);
  else if (!strcmp(var, "ae_level")) r = s->set_ae_level(s, v);
  else if (!strcmp(var, "led_intensity")) { led_duty = constrain(v, 0, 255); ledcWrite(LED_PIN, led_duty); }
  else r = -1;
  if (r < 0) { httpd_resp_send_500(req); return ESP_FAIL; }
  return send_json(req, "{\"ok\":true}");
}

static esp_err_t capture_handler(httpd_req_t *req) {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) { httpd_resp_send_500(req); return ESP_FAIL; }
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
  esp_err_t r = httpd_resp_send(req, (const char *)fb->buf, fb->len);
  esp_camera_fb_return(fb);
  return r;
}

static esp_err_t index_handler(httpd_req_t *req) {
  static const char html[] =
    "<!doctype html><meta name=viewport content='width=device-width'><title>ESP32-CAM</title>"
    "<body style='margin:0;background:#000'><img style='width:100%' id=s><script>"
    "s.src=location.protocol+'//'+location.hostname+':81/stream'</script>";
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

// ------------------------------------------------------------ stream :81
#define BOUNDARY "esp32camframe"

static bool send_all(int fd, const void *data, size_t len) {
  const uint8_t *p = (const uint8_t *)data;
  while (len) {
    int n = send(fd, p, len, 0);
    if (n <= 0) return false;
    p += n;
    len -= n;
  }
  return true;
}

// Writes the HTTP response itself on the raw socket (no chunked encoding).
static esp_err_t stream_handler(httpd_req_t *req) {
  int fd = httpd_req_to_sockfd(req);
  int one = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
  struct timeval tv = {5, 0};
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

  static const char hdr[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: multipart/x-mixed-replace;boundary=" BOUNDARY "\r\n"
    "Access-Control-Allow-Origin: *\r\n"
    "Cache-Control: no-store\r\n"
    "Connection: close\r\n\r\n";
  if (!send_all(fd, hdr, sizeof(hdr) - 1)) return ESP_FAIL;

  char part[160];
  for (;;) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) { delay(5); continue; }
    int64_t ts = fb->timestamp.tv_sec * 1000000LL + fb->timestamp.tv_usec;
    int n = snprintf(part, sizeof(part),
      "--" BOUNDARY "\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n"
      "X-Timestamp: %lld.%06lld\r\n\r\n",
      (unsigned)fb->len, ts / 1000000, ts % 1000000);
    bool ok = send_all(fd, part, n) && send_all(fd, fb->buf, fb->len) && send_all(fd, "\r\n", 2);
    esp_camera_fb_return(fb);
    if (!ok) break;
    stream_frames++;
  }
  return ESP_FAIL;  // tell httpd to close the socket we wrote to directly
}

static void start_servers() {
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.max_uri_handlers = 8;
  cfg.lru_purge_enable = true;
  httpd_uri_t u_index = {"/", HTTP_GET, index_handler, NULL};
  httpd_uri_t u_status = {"/status", HTTP_GET, status_handler, NULL};
  httpd_uri_t u_control = {"/control", HTTP_GET, control_handler, NULL};
  httpd_uri_t u_capture = {"/capture", HTTP_GET, capture_handler, NULL};
  if (httpd_start(&http80, &cfg) == ESP_OK) {
    httpd_register_uri_handler(http80, &u_index);
    httpd_register_uri_handler(http80, &u_status);
    httpd_register_uri_handler(http80, &u_control);
    httpd_register_uri_handler(http80, &u_capture);
  }
  cfg.server_port = 81;
  cfg.ctrl_port += 1;
  cfg.max_open_sockets = 3;
  cfg.task_priority = tskIDLE_PRIORITY + 6;
  httpd_uri_t u_stream = {"/stream", HTTP_GET, stream_handler, NULL};
  if (httpd_start(&http81, &cfg) == ESP_OK) httpd_register_uri_handler(http81, &u_stream);
}

// ------------------------------------------------------------ setup
void setup() {
  Serial.begin(115200);
  Serial.printf("\n%s id=%s\n", FW_VERSION, TEAM_ID);
  ledcAttach(LED_PIN, 5000, 8);
  ledcWrite(LED_PIN, 0);

  if (!camera_init()) {
    Serial.println("Camera init failed (check the ribbon cable / board type). Rebooting.");
    delay(2000);
    ESP.restart();
  }
  Serial.printf("psram=%d fb_count=%d\n", psramFound(), fb_count_used);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);  // the single biggest latency win
  WiFi.setAutoReconnect(true);
  if (strlen(WIFI_SSID)) WiFi.begin(WIFI_SSID, WIFI_PASS);
  else WiFi.begin();
  Serial.print("WiFi connecting");
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print('.');
    if (millis() - t0 > 30000) {
      Serial.println("\nWiFi timeout: set WIFI_SSID / WIFI_PASS at the top. Rebooting.");
      ESP.restart();
    }
  }
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  Serial.printf("\nWiFi connected ip=%s rssi=%d\n", WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());

  start_servers();
  disc.addField("http", 80);
  disc.addField("stream", 81);
  disc.addField("fw", FW_VERSION);
  disc.begin();
  Serial.printf("Ready. Laptop: python3 server.py --id %s\n", TEAM_ID);
}

void loop() { delay(10000); }

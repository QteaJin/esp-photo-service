#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <WebServer.h>
#include <time.h>
#include "camera.h"
#include "config.h"
#include "uploader.h"
#include "web_assets.h"

static Config cfg;
static WebServer server(80);
static String PRODUCT;                 // аналог get_pi_serial(): eFuse MAC
static volatile bool autoRunning = false;
static TaskHandle_t autoTask = nullptr;

static String deviceId() {
  char buf[17];
  snprintf(buf, sizeof(buf), "%012llx", (unsigned long long)ESP.getEfuseMac());
  return String(buf);
}

static String makeFilename() {
  struct tm t;
  if (getLocalTime(&t, 0)) {
    char buf[40];
    strftime(buf, sizeof(buf), "%Y-%m-%d_%H-%M-%S", &t);
    return String(buf) + "_" + String((uint32_t)(micros() % 1000000)) + ".jpg";
  }
  return "uptime_" + String(millis()) + ".jpg";   // время ещё не синхронизировано
}

// auto_capture_loop
static void autoCaptureLoop(void *) {
  while (autoRunning) {
    camera_fb_t *fb = cameraAcquire(false);
    if (fb) {
      sendToServer(cfg.serverUrl, PRODUCT, makeFilename(), fb);
      cameraRelease(fb, false);
    }
    for (int ms = 0; ms < cfg.interval * 1000 && autoRunning; ms += 100) delay(100);
  }
  autoTask = nullptr;
  vTaskDelete(nullptr);
}

static void handlePreview() {
  camera_fb_t *fb = cameraAcquire(true);
  if (!fb) {
    server.send(500, "text/plain; charset=utf-8", "Ошибка камеры");
    return;
  }
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "image/jpeg", (const char *)fb->buf, fb->len);
  cameraRelease(fb, true);
}

static void handleAutoStart() {
  if (autoRunning) {
    server.send(200, "text/plain; charset=utf-8", "Уже запущено");
    return;
  }
  if (server.hasArg("interval")) {
    cfg.interval = max(1, (int)server.arg("interval").toInt());
    configSave(cfg);
  }
  autoRunning = true;
  xTaskCreate(autoCaptureLoop, "autocap", 8192, nullptr, 1, &autoTask);
  server.send(200, "text/plain; charset=utf-8", "Автосъёмка запущена");
}

static void handleAutoStop() {
  autoRunning = false;
  server.send(200, "text/plain; charset=utf-8", "Автосъёмка остановлена");
}

static void handleCameraSettings() {
  ColorSettings c = cameraGetColor();
  server.send(200, "application/json",
              String("{\"brightness\":") + c.brightness + ",\"contrast\":" + c.contrast +
                  ",\"saturation\":" + c.saturation + ",\"ae_level\":" + c.aeLevel +
                  ",\"wb_mode\":" + c.wbMode + ",\"lenc\":" + (c.lenc ? 1 : 0) + "}");
}

// /control?var=<имя>&val=<число>: применить параметр сразу, без сохранения в память
static void handleControl() {
  if (!server.hasArg("var") || !server.hasArg("val")) {
    server.send(400, "text/plain; charset=utf-8", "Нужны параметры var и val");
    return;
  }
  String var = server.arg("var");
  int val = server.arg("val").toInt();
  ColorSettings c = cameraGetColor();
  if (var == "brightness") c.brightness = val;
  else if (var == "contrast") c.contrast = val;
  else if (var == "saturation") c.saturation = val;
  else if (var == "ae_level") c.aeLevel = val;
  else if (var == "wb_mode") c.wbMode = val;
  else if (var == "lenc") c.lenc = (val != 0);
  else {
    server.send(400, "text/plain; charset=utf-8", "Неизвестный параметр");
    return;
  }
  cameraSetColor(c);
  handleCameraSettings();
}

static void handleCameraSave() {
  cfg.color = cameraGetColor();
  configSave(cfg);
  server.send(200, "text/plain; charset=utf-8", "Сохранено");
}

static void handleCameraReset() {
  cameraSetColor(ColorSettings());
  handleCameraSettings();
}

static void handleStatus() {
  server.send(200, "application/json",
              String("{\"running\":") + (autoRunning ? "true" : "false") + ",\"interval\":" + cfg.interval + "}");
}

void setup() {
  Serial.begin(115200);
  delay(500);
  PRODUCT = deviceId();
  Serial.println("Device ID: " + PRODUCT);

  configLoad(cfg);

  if (!cameraInit(cfg.color)) {
    Serial.println("Camera init failed, restart in 5s");
    delay(5000);
    ESP.restart();
  }

  // Wi-Fi: captive portal при первом запуске / если сеть недоступна
  WiFiManager wm;
  WiFiManagerParameter urlParam("server_url", "URL сервера (upload)", cfg.serverUrl.c_str(), 200);
  wm.addParameter(&urlParam);
  wm.setConfigPortalTimeout(180);
  if (!wm.autoConnect("ESP32-Cam-Setup")) {
    Serial.println("Wi-Fi connect failed, restart");
    ESP.restart();
  }
  String newUrl = urlParam.getValue();
  if (newUrl.length() > 0 && newUrl != cfg.serverUrl) {   // значение меняется только после портала
    cfg.serverUrl = newUrl;
    configSave(cfg);
  }
  Serial.println("IP: " + WiFi.localIP().toString());
  WiFi.setSleep(false);   // без энергосбережения Wi-Fi соединение стабильнее

  configTzTime("UTC0", "pool.ntp.org", "time.google.com");   // часовой пояс поправьте при необходимости

  server.on("/", HTTP_GET, []() { server.send_P(200, "text/html; charset=utf-8", INDEX_HTML); });
  server.on("/main.js", HTTP_GET, []() { server.send_P(200, "application/javascript; charset=utf-8", MAIN_JS); });
  server.on("/preview", HTTP_GET, handlePreview);
  server.on("/auto_start", HTTP_GET, handleAutoStart);
  server.on("/auto_stop", HTTP_GET, handleAutoStop);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/camera_settings", HTTP_GET, handleCameraSettings);
  server.on("/control", HTTP_GET, handleControl);
  server.on("/camera_save", HTTP_GET, handleCameraSave);
  server.on("/camera_reset", HTTP_GET, handleCameraReset);
  server.begin();
}

void loop() {
  server.handleClient();
  delay(2);
}

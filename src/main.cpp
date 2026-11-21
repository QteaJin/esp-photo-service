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
      String filename = makeFilename();
      UploadBody body;
      bool ready = uploadPrepare(filename, fb, body);
      cameraRelease(fb, false);   // камера свободна, пока идёт отправка (панель не зависает)
      if (ready) uploadSend(cfg.serverUrl, PRODUCT, filename, body);
    }
    for (int ms = 0; ms < cfg.interval * 1000 && autoRunning; ms += 100) delay(100);
  }
  autoTask = nullptr;
  vTaskDelete(nullptr);
}

static void handlePreview() {
  camera_fb_t *fb = cameraAcquire(true);
  if (!fb) {
    server.send(500, "text/plain; charset=utf-8", "Camera error");
    return;
  }
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "image/jpeg", (const char *)fb->buf, fb->len);
  cameraRelease(fb, true);
}

static void handleAutoStart() {
  if (autoRunning) {
    server.send(200, "text/plain; charset=utf-8", "Already running");
    return;
  }
  if (server.hasArg("interval")) {
    cfg.interval = max(1, (int)server.arg("interval").toInt());
    configSave(cfg);
  }
  autoRunning = true;
  xTaskCreate(autoCaptureLoop, "autocap", 8192, nullptr, 1, &autoTask);
  server.send(200, "text/plain; charset=utf-8", "Auto capture started");
}

static void handleAutoStop() {
  autoRunning = false;
  server.send(200, "text/plain; charset=utf-8", "Auto capture stopped");
}

static String colorJson(const ColorSettings &c) {
  return String("{\"brightness\":") + c.brightness + ",\"contrast\":" + c.contrast +
         ",\"saturation\":" + c.saturation + ",\"ae_level\":" + c.aeLevel +
         ",\"wb_mode\":" + c.wbMode + ",\"lenc\":" + (c.lenc ? 1 : 0) + "}";
}

static void handleCameraSettings() {
  server.send(200, "application/json", colorJson(cameraGetColor()));
}

// /control?var=<имя>&val=<число>: применить параметр сразу, без сохранения в память
static void handleControl() {
  if (!server.hasArg("var") || !server.hasArg("val")) {
    server.send(400, "text/plain; charset=utf-8", "Parameters var and val are required");
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
    server.send(400, "text/plain; charset=utf-8", "Unknown parameter");
    return;
  }
  cameraSetColor(c);
  handleCameraSettings();
}

static void handleCameraSave() {
  cfg.color = cameraGetColor();
  configSave(cfg);
  server.send(200, "text/plain; charset=utf-8", "Saved");
}

static void handleCameraReset() {
  cameraSetColor(ColorSettings());
  handleCameraSettings();
}

static const char *resetReasonText() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power on";
    case ESP_RST_SW: return "software restart";
    case ESP_RST_PANIC: return "program crash";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_BROWNOUT: return "brownout (power dip)";
    case ESP_RST_DEEPSLEEP: return "wake from deep sleep";
    case ESP_RST_EXT: return "reset button";
    default: return "other";
  }
}

// /sysinfo: техническое состояние платы (запрашивается по кнопке в панели, без опроса)
static void handleSysInfo() {
  String json = "{";
  json += "\"temp\":" + String(temperatureRead(), 1);
  json += ",\"rssi\":" + String(WiFi.RSSI());
  json += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
  json += ",\"uptime\":" + String((uint32_t)(millis() / 1000));
  json += ",\"heap\":" + String(ESP.getFreeHeap());
  json += ",\"heap_total\":" + String(ESP.getHeapSize());
  json += ",\"psram\":" + String(ESP.getFreePsram());
  json += ",\"psram_total\":" + String(ESP.getPsramSize());
  json += ",\"reset\":\"" + String(resetReasonText()) + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

static String jsonEscape(const String &s) {
  String out;
  for (unsigned int i = 0; i < s.length(); i++) {
    char ch = s[i];
    if (ch == '"' || ch == '\\') {
      out += '\\';
      out += ch;
    } else if ((uint8_t)ch < ' ') {
      out += ' ';
    } else {
      out += ch;
    }
  }
  return out;
}

// Четыре числа 0..255 через точки
static bool isValidIPv4(const String &s) {
  int parts = 0, value = 0, digits = 0;
  for (unsigned int i = 0; i <= s.length(); i++) {
    char ch = (i < s.length()) ? s[i] : '.';
    if (ch >= '0' && ch <= '9') {
      value = value * 10 + (ch - '0');
      digits++;
      if (digits > 3 || value > 255) return false;
    } else if (ch == '.') {
      if (digits == 0) return false;
      parts++;
      value = 0;
      digits = 0;
    } else {
      return false;
    }
  }
  return parts == 4;
}

static String serverJson() {
  String host;
  int port;
  parseServerUrl(cfg.serverUrl, host, port);
  return String("{\"ip\":\"") + jsonEscape(host) + "\",\"port\":" + port + ",\"url\":\"" +
         jsonEscape(cfg.serverUrl) + "\"}";
}

// /settings: что хранится на плате (сервер, интервал) и цвет: сохранённый и текущий
static void handleSettings() {
  String json = "{\"server\":" + serverJson();
  json += ",\"interval\":" + String(cfg.interval);
  json += ",\"running\":" + String(autoRunning ? "true" : "false");
  json += ",\"saved\":" + colorJson(cfg.color);
  json += ",\"live\":" + colorJson(cameraGetColor()) + "}";
  server.send(200, "application/json", json);
}

// /set_server?ip=<IPv4>&port=<порт>: сохраняет адрес приёма фото в NVS (путь фиксирован)
static void handleSetServer() {
  if (autoRunning) {
    server.send(409, "text/plain; charset=utf-8", "Stop auto capture before changing the server");
    return;
  }
  String ip = server.arg("ip");
  ip.trim();
  int port = (server.hasArg("port") && server.arg("port").length() > 0) ? server.arg("port").toInt()
                                                                        : DEFAULT_UPLOAD_PORT;
  if (!isValidIPv4(ip)) {
    server.send(400, "text/plain; charset=utf-8", "Invalid IP address (expected format: 192.168.0.212)");
    return;
  }
  if (port < 1 || port > 65535) {
    server.send(400, "text/plain; charset=utf-8", "Invalid port (1-65535)");
    return;
  }
  cfg.serverUrl = buildServerUrl(ip, port);
  configSave(cfg);
  server.send(200, "application/json", serverJson());
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
  String savedHost;
  int savedPort;
  parseServerUrl(cfg.serverUrl, savedHost, savedPort);
  WiFiManagerParameter ipParam("server_ip", "Server IP", savedHost.c_str(), 15);
  String savedPortStr(savedPort);
  WiFiManagerParameter portParam("server_port", "Server port", savedPortStr.c_str(), 5);
  wm.addParameter(&ipParam);
  wm.addParameter(&portParam);
  wm.setConfigPortalTimeout(180);
  if (!wm.autoConnect("ESP32-Cam-Setup")) {
    Serial.println("Wi-Fi connect failed, restart");
    ESP.restart();
  }
  String newIp = ipParam.getValue();
  int newPort = String(portParam.getValue()).toInt();
  if (isValidIPv4(newIp) && newPort >= 1 && newPort <= 65535) {
    String newUrl = buildServerUrl(newIp, newPort);
    if (newUrl != cfg.serverUrl) {   // значение меняется только после портала
      cfg.serverUrl = newUrl;
      configSave(cfg);
    }
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
  server.on("/sysinfo", HTTP_GET, handleSysInfo);
  server.on("/settings", HTTP_GET, handleSettings);
  server.on("/set_server", HTTP_GET, handleSetServer);
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

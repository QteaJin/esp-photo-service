#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <WebServer.h>
#include <ArduinoOTA.h>
#include <esp_ota_ops.h>
#include <time.h>
#include "camera.h"
#include "config.h"
#include "fwupdate.h"
#include "log.h"
#include "uploader.h"
#include "web_assets.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Нет include/secrets.h: скопируйте include/secrets.example.h в include/secrets.h"
#endif

// Версию подставляет scripts/pre_version.py (дата, время сборки и хэш коммита)
#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

// ---- Настройки диагностики ----
static const float TEMP_WARN_C = 70.0f;                   // выше: предупреждение в журнал
static const float TEMP_OK_C = 65.0f;                     // ниже: предупреждение снято
static const uint32_t WIFI_LOST_RESTART_MS = 10UL * 60 * 1000;   // сети нет столько: перезагрузка
static const uint32_t WIFI_RETRY_MS = 60UL * 1000;        // как часто напоминать Wi-Fi о переподключении
static const uint32_t HOUSEKEEP_MS = 10UL * 1000;         // период фоновых проверок
static const uint32_t CLIENT_IDLE_MS = 5UL * 60 * 1000;   // клиент молчит столько: считаем отключившимся
static const int MAX_CLIENTS = 6;

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

// ---- Причина перезагрузки ----
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

// Перезагрузка из-за сбоя (а не по желанию пользователя)
static bool resetWasAbnormal() {
  switch (esp_reset_reason()) {
    case ESP_RST_PANIC:
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
    case ESP_RST_BROWNOUT: return true;
    default: return false;
  }
}

// ---- Wi-Fi: события и контроль связи ----
static bool wifiUp = false;
static bool wifiLost = false;
static uint32_t wifiLostAtMs = 0;

static void onWifiEvent(arduino_event_id_t ev, arduino_event_info_t info) {
  switch (ev) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      if (wifiLost) LOG("wifi: back after %lu s", (unsigned long)((millis() - wifiLostAtMs) / 1000));
      LOG("wifi: connected, IP %s, %d dBm", WiFi.localIP().toString().c_str(), WiFi.RSSI());
      wifiUp = true;
      wifiLost = false;
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      if (wifiUp) {   // пишем только момент потери связи, а не каждую неудачную попытку
        LOGW("wifi: disconnected, reason %d", info.wifi_sta_disconnected.reason);
        wifiUp = false;
        wifiLost = true;
        wifiLostAtMs = millis();
      }
      break;
    default: break;
  }
}

// ---- Клиенты веб-панели: подключения и отключения (по запросам, у WebServer нет постоянных соединений) ----
struct ClientSeen {
  uint32_t ip;
  uint32_t lastMs;
};
static ClientSeen clients[MAX_CLIENTS];

static void noteClient() {
  uint32_t ip = (uint32_t)server.client().remoteIP();
  uint32_t now = millis();
  int freeSlot = -1;
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (clients[i].ip == ip) {
      clients[i].lastMs = now;
      return;
    }
    if (clients[i].ip == 0 && freeSlot < 0) freeSlot = i;
  }
  if (freeSlot < 0) freeSlot = 0;   // таблица полна: вытесняем первую запись
  clients[freeSlot] = {ip, now};
  LOG("client connected: %s", server.client().remoteIP().toString().c_str());
}

static void checkClientsGone() {
  uint32_t now = millis();
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (clients[i].ip != 0 && now - clients[i].lastMs > CLIENT_IDLE_MS) {
      LOG("client left: %s", IPAddress(clients[i].ip).toString().c_str());
      clients[i].ip = 0;
    }
  }
}

// Регистрирует маршрут и заодно отмечает клиента
static void route(const char *path, void (*handler)()) {
  server.on(path, HTTP_GET, [handler]() {
    noteClient();
    handler();
  });
}

// ---- Автосъёмка ----
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

// Останавливает автосъёмку и ждёт завершения задачи (перед обновлением прошивки)
static void stopAutoAndWait() {
  autoRunning = false;
  for (int i = 0; i < 200 && autoTask != nullptr; i++) delay(100);
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
  if (fwInstallRunning()) {
    server.send(409, "text/plain; charset=utf-8", "Firmware update in progress");
    return;
  }
  if (server.hasArg("interval")) {
    cfg.interval = max(1, (int)server.arg("interval").toInt());
    configSave(cfg);
  }
  autoRunning = true;
  xTaskCreate(autoCaptureLoop, "autocap", 8192, nullptr, 1, &autoTask);
  LOG("auto capture started, interval %d s", cfg.interval);
  server.send(200, "text/plain; charset=utf-8", "Auto capture started");
}

static void handleAutoStop() {
  if (autoRunning) LOG("auto capture stopped");
  autoRunning = false;
  server.send(200, "text/plain; charset=utf-8", "Auto capture stopped");
}

// ---- Цвет ----
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

// ---- Состояние платы ----
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

// /health.json (и прежний /sysinfo): состояние платы. Панель запрашивает его по кнопке, при открытии
// и, если включено, раз в 15 секунд.
static void handleHealth() {
  UploadStats u = uploadStats();
  String json = "{";
  json += "\"temp\":" + String(temperatureRead(), 1);
  json += ",\"rssi\":" + String(WiFi.RSSI());
  json += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
  json += ",\"host\":\"" OTA_HOSTNAME "\"";
  json += ",\"uptime\":" + String((uint32_t)(millis() / 1000));
  json += ",\"heap\":" + String(ESP.getFreeHeap());
  json += ",\"heap_total\":" + String(ESP.getHeapSize());
  json += ",\"psram\":" + String(ESP.getFreePsram());
  json += ",\"psram_total\":" + String(ESP.getPsramSize());
  json += ",\"reset\":\"" + String(resetReasonText()) + "\"";
  json += ",\"reset_bad\":" + String(resetWasAbnormal() ? "true" : "false");
  json += ",\"up_ok\":" + String(u.ok);
  json += ",\"up_fail\":" + String(u.fail);
  json += ",\"up_code\":" + String(u.lastCode);
  json += ",\"up_ago\":" + String(u.lastAgoSec);
  json += ",\"up_rate\":" + String(u.perMin, 2);
  json += "}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

// /log: журнал в ОЗУ, text/plain. ?src=now | prev отдаёт только один из журналов (для панели).
static void handleLog() {
  String src = server.arg("src");
  String out;
  if (src == "now") {
    out = logDump(false);
  } else if (src == "prev") {
    out = logDump(true);
  } else {
    out = String("=== This boot (started after: ") + resetReasonText() + ") ===\n" + logDump(false);
    out += "\n=== Before the last restart ===\n";
    out += logHasPrevious() ? logDump(true) : String("(nothing kept: the board was powered on)\n");
  }
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/plain; charset=utf-8", out);
}

// ---- Сервер фото ----
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
  LOG("photo server set to %s", cfg.serverUrl.c_str());
  server.send(200, "application/json", serverJson());
}

static void handleStatus() {
  server.send(200, "application/json",
              String("{\"running\":") + (autoRunning ? "true" : "false") + ",\"interval\":" + cfg.interval + "}");
}

// ---- Обновление прошивки ----
// /fw_info: что сейчас установлено и куда ходить за обновлением
static void handleFwInfo() {
  const esp_partition_t *part = esp_ota_get_running_partition();
  String json = "{\"version\":\"" FW_VERSION "\"";
  json += ",\"built\":\"" __DATE__ " " __TIME__ "\"";
  json += ",\"slot\":\"" + String(part ? part->label : "?") + "\"";
  json += ",\"size\":" + String(ESP.getSketchSize());
  json += ",\"host\":\"" OTA_HOSTNAME "\"";
  json += ",\"fw_ip\":\"" + jsonEscape(cfg.fwHost) + "\"";
  json += ",\"fw_port\":" + String(cfg.fwPort);
  json += "}";
  server.send(200, "application/json", json);
}

// /fw_server_set?ip=<IPv4>&port=<порт>: адрес сервера прошивок
static void handleFwServerSet() {
  if (fwInstallRunning()) {
    server.send(409, "text/plain; charset=utf-8", "Firmware update in progress");
    return;
  }
  String ip = server.arg("ip");
  ip.trim();
  int port = server.arg("port").toInt();
  if (!isValidIPv4(ip)) {
    server.send(400, "text/plain; charset=utf-8", "Invalid IP address (expected format: 192.168.0.220)");
    return;
  }
  if (port < 1 || port > 65535) {
    server.send(400, "text/plain; charset=utf-8", "Invalid port (1-65535)");
    return;
  }
  cfg.fwHost = ip;
  cfg.fwPort = port;
  configSave(cfg);
  LOG("firmware server set to %s:%d", ip.c_str(), port);
  handleFwInfo();
}

// /fw_check: спрашивает сервер, какая версия там лежит. Ничего не скачивает и не устанавливает.
static void handleFwCheck() {
  FwRemote r = fwFetchRemote(cfg.fwHost, cfg.fwPort);
  String json;
  if (!r.ok) {
    LOGW("fw check failed: %s", r.error.c_str());
    json = "{\"ok\":false,\"error\":\"" + jsonEscape(r.error) + "\"}";
  } else {
    bool differs = r.version != FW_VERSION;
    LOG("fw check: server has %s, installed %s", r.version.c_str(), FW_VERSION);
    json = "{\"ok\":true,\"installed\":\"" FW_VERSION "\",\"available\":\"" + jsonEscape(r.version) +
           "\",\"size\":" + String((uint32_t)r.size) + ",\"sha256\":\"" + r.sha256 + "\",\"newer\":" +
           (differs ? "true" : "false") + "}";
  }
  server.send(200, "application/json", json);
}

// /fw_install: запускает установку (кнопка в панели просит подтверждение заранее)
static void handleFwInstall() {
  if (!fwInstallStart(cfg.fwHost, cfg.fwPort, stopAutoAndWait)) {
    server.send(409, "text/plain; charset=utf-8", "Update is already running");
    return;
  }
  server.send(200, "application/json", "{\"started\":true}");
}

static void handleFwStatus() {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", fwStatusJson());
}

// ---- Загрузка по Wi-Fi с компьютера (ArduinoOTA) ----
static void setupOta() {
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([]() {
    autoRunning = false;   // освободить камеру и сеть на время загрузки
    LOG("ota: start (%s)", ArduinoOTA.getCommand() == U_FLASH ? "firmware" : "filesystem");
  });
  ArduinoOTA.onEnd([]() { LOG("ota: finished, restarting"); });
  ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
    static int lastStep = -1;
    int pct = total ? (int)((uint64_t)done * 100 / total) : 0;
    if (pct / 25 != lastStep) {   // в журнал раз в 25 %
      lastStep = pct / 25;
      LOG("ota: %d%%", pct);
    }
  });
  ArduinoOTA.onError([](ota_error_t e) {
    const char *why = e == OTA_AUTH_ERROR      ? "auth failed (wrong password)"
                      : e == OTA_BEGIN_ERROR   ? "begin failed"
                      : e == OTA_CONNECT_ERROR ? "connect failed"
                      : e == OTA_RECEIVE_ERROR ? "receive failed"
                      : e == OTA_END_ERROR     ? "end failed"
                                               : "unknown";
    LOGE("ota: error %u, %s", (unsigned)e, why);
  });
  ArduinoOTA.begin();
  LOG("ota: ready, %s.local", OTA_HOSTNAME);
}

// ---- Фоновые проверки: температура, связь, клиенты ----
static bool tempWarned = false;

static void housekeeping() {
  static uint32_t lastRun = 0, lastRetry = 0;
  uint32_t now = millis();
  if (now - lastRun < HOUSEKEEP_MS) return;
  lastRun = now;

  float t = temperatureRead();
  if (!tempWarned && t >= TEMP_WARN_C) {
    tempWarned = true;
    LOGW("temp %.1f C above %.0f C", t, TEMP_WARN_C);
  } else if (tempWarned && t <= TEMP_OK_C) {
    tempWarned = false;
    LOG("temp %.1f C back to normal", t);
  }

  if (WiFi.status() != WL_CONNECTED) {
    if (!wifiLost) {   // потеря связи без события (на всякий случай)
      wifiLost = true;
      wifiLostAtMs = now;
    }
    if (now - wifiLostAtMs > WIFI_LOST_RESTART_MS) {
      LOGE("wifi: no connection for %lu min, restarting", (unsigned long)(WIFI_LOST_RESTART_MS / 60000));
      delay(200);
      ESP.restart();
    }
    if (now - lastRetry > WIFI_RETRY_MS) {
      lastRetry = now;
      WiFi.reconnect();
    }
  }

  checkClientsGone();
}

void setup() {
  Serial.begin(115200);
  delay(500);
  logInit();
  LOG("boot: firmware %s, reset reason: %s", FW_VERSION, resetReasonText());
  if (resetWasAbnormal()) LOGW("previous run ended abnormally: %s", resetReasonText());
  PRODUCT = deviceId();
  LOG("device id %s", PRODUCT.c_str());

  configLoad(cfg);

  if (!cameraInit(cfg.color)) {
    LOGE("camera init failed, restart in 5 s");
    delay(5000);
    ESP.restart();
  }

  // Wi-Fi: captive portal при первом запуске / если сеть недоступна
  WiFi.onEvent(onWifiEvent);
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
    LOGE("wifi: connect failed, restart");
    delay(200);
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
  WiFi.setSleep(false);   // без энергосбережения Wi-Fi соединение стабильнее
  WiFi.setAutoReconnect(true);

  configTzTime("UTC0", "pool.ntp.org", "time.google.com");   // часовой пояс поправьте при необходимости

  setupOta();

  server.on("/", HTTP_GET, []() {
    noteClient();
    server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
  });
  server.on("/main.js", HTTP_GET, []() { server.send_P(200, "application/javascript; charset=utf-8", MAIN_JS); });
  route("/preview", handlePreview);
  route("/auto_start", handleAutoStart);
  route("/auto_stop", handleAutoStop);
  route("/status", handleStatus);
  route("/health.json", handleHealth);
  route("/sysinfo", handleHealth);   // прежнее имя
  route("/log", handleLog);
  route("/settings", handleSettings);
  route("/set_server", handleSetServer);
  route("/camera_settings", handleCameraSettings);
  route("/control", handleControl);
  route("/camera_save", handleCameraSave);
  route("/camera_reset", handleCameraReset);
  route("/fw_info", handleFwInfo);
  route("/fw_server_set", handleFwServerSet);
  route("/fw_check", handleFwCheck);
  route("/fw_install", handleFwInstall);
  route("/fw_status", handleFwStatus);
  server.begin();
  LOG("web panel ready on port 80");
}

void loop() {
  ArduinoOTA.handle();   // запросы на загрузку по Wi-Fi
  server.handleClient();
  housekeeping();
  delay(2);
}

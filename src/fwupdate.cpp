#include "fwupdate.h"
#include <HTTPClient.h>
#include <Update.h>
#include "mbedtls/sha256.h"
#include "log.h"

enum FwState { FW_IDLE, FW_RUNNING, FW_DONE, FW_ERROR };

static volatile FwState fwState = FW_IDLE;
static volatile int fwPercent = 0;
static char fwMessage[96] = "";
static portMUX_TYPE fwMux = portMUX_INITIALIZER_UNLOCKED;

struct InstallParams {
  String host;
  int port;
  void (*before)();
};

static void setState(FwState st, int percent, const char *msg) {
  portENTER_CRITICAL(&fwMux);
  fwState = st;
  fwPercent = percent;
  strlcpy(fwMessage, msg, sizeof(fwMessage));
  portEXIT_CRITICAL(&fwMux);
}

// Достаёт значение поля из плоского JSON вида {"key":"text"} или {"key":123}
static String jsonField(const String &js, const char *key) {
  int k = js.indexOf(String("\"") + key + "\"");
  if (k < 0) return "";
  int colon = js.indexOf(':', k);
  if (colon < 0) return "";
  int i = colon + 1;
  while (i < (int)js.length() && (js[i] == ' ' || js[i] == '\t')) i++;
  if (i < (int)js.length() && js[i] == '"') {
    int end = js.indexOf('"', i + 1);
    return end < 0 ? "" : js.substring(i + 1, end);
  }
  int end = i;
  while (end < (int)js.length() && js[end] != ',' && js[end] != '}' && js[end] != ' ') end++;
  return js.substring(i, end);
}

FwRemote fwFetchRemote(const String &host, int port) {
  FwRemote r;
  if (host.length() == 0) {
    r.error = "Firmware server is not set";
    return r;
  }
  HTTPClient http;
  http.setTimeout(5000);
  String url = "http://" + host + ":" + String(port) + "/version.json";
  if (!http.begin(url)) {
    r.error = "Bad server address";
    return r;
  }
  int code = http.GET();
  if (code != 200) {
    r.error = code > 0 ? "Server answered HTTP " + String(code) : "Server is not reachable (" + http.errorToString(code) + ")";
    http.end();
    return r;
  }
  if (http.getSize() > 2048) {
    r.error = "version.json is too large";
    http.end();
    return r;
  }
  String body = http.getString();
  http.end();
  r.version = jsonField(body, "version");
  r.sha256 = jsonField(body, "sha256");
  r.sha256.toLowerCase();
  r.size = (size_t)jsonField(body, "size").toInt();
  if (r.version.length() == 0 || r.sha256.length() != 64 || r.size == 0) {
    r.error = "version.json must contain version, sha256 (64 hex) and size";
    return r;
  }
  r.ok = true;
  return r;
}

static void fail(const String &why) {
  LOGE("fw update: %s", why.c_str());
  setState(FW_ERROR, fwPercent, why.c_str());
}

static void installTask(void *arg) {
  InstallParams *p = (InstallParams *)arg;
  String host = p->host;
  int port = p->port;
  void (*before)() = p->before;
  delete p;

  LOG("fw update: start from %s:%d", host.c_str(), port);
  setState(FW_RUNNING, 0, "Preparing");
  if (before) before();

  FwRemote remote = fwFetchRemote(host, port);
  if (!remote.ok) {
    fail(remote.error);
    vTaskDelete(nullptr);
    return;
  }
  LOG("fw update: server has %s, %u bytes", remote.version.c_str(), (unsigned)remote.size);

  HTTPClient http;
  http.setTimeout(10000);
  if (!http.begin("http://" + host + ":" + String(port) + "/firmware.bin")) {
    fail("Bad server address");
    vTaskDelete(nullptr);
    return;
  }
  int code = http.GET();
  if (code != 200) {
    http.end();
    fail("firmware.bin: HTTP " + String(code));
    vTaskDelete(nullptr);
    return;
  }
  if (http.getSize() > 0 && (size_t)http.getSize() != remote.size) {
    http.end();
    fail("File size does not match version.json");
    vTaskDelete(nullptr);
    return;
  }
  if (!Update.begin(remote.size)) {
    http.end();
    fail(String("Not enough space: ") + Update.errorString());
    vTaskDelete(nullptr);
    return;
  }

  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts_ret(&sha, 0);

  WiFiClient *stream = http.getStreamPtr();
  static uint8_t buf[1024];
  size_t done = 0;
  uint32_t lastData = millis();
  int lastLogged = -1;
  String err;
  setState(FW_RUNNING, 0, "Downloading");

  while (done < remote.size && err.length() == 0) {
    size_t avail = stream->available();
    if (avail) {
      size_t want = min(min(avail, sizeof(buf)), remote.size - done);
      size_t n = stream->readBytes(buf, want);
      if (Update.write(buf, n) != n) {
        err = String("Write failed: ") + Update.errorString();
        break;
      }
      mbedtls_sha256_update_ret(&sha, buf, n);
      done += n;
      lastData = millis();
      int pct = (int)(done * 100 / remote.size);
      setState(FW_RUNNING, pct, "Downloading");
      if (pct / 25 != lastLogged) {   // в журнал раз в 25 %
        lastLogged = pct / 25;
        LOG("fw update: %d%%", pct);
      }
    } else if (!http.connected()) {
      break;
    } else if (millis() - lastData > 15000) {
      err = "Download stalled";
    } else {
      delay(5);
    }
  }
  http.end();

  uint8_t digest[32];
  mbedtls_sha256_finish_ret(&sha, digest);
  mbedtls_sha256_free(&sha);

  if (err.length() == 0 && done != remote.size) err = "Download incomplete";
  if (err.length() == 0) {
    setState(FW_RUNNING, 100, "Verifying");
    String hex;
    for (int i = 0; i < 32; i++) {
      char h[3];
      snprintf(h, sizeof(h), "%02x", digest[i]);
      hex += h;
    }
    if (hex != remote.sha256) err = "Checksum mismatch, update cancelled";
  }
  if (err.length() > 0) {
    Update.abort();   // не переключаем загрузку на повреждённый образ
    fail(err);
    vTaskDelete(nullptr);
    return;
  }
  if (!Update.end(true)) {
    fail(String("Image rejected: ") + Update.errorString());
    vTaskDelete(nullptr);
    return;
  }

  LOG("fw update: %s installed, restarting", remote.version.c_str());
  setState(FW_DONE, 100, "Installed, restarting");
  delay(1500);   // дать панели прочитать состояние
  ESP.restart();
}

bool fwInstallStart(const String &host, int port, void (*beforeStart)()) {
  if (fwState == FW_RUNNING) return false;
  setState(FW_RUNNING, 0, "Starting");
  InstallParams *p = new InstallParams{host, port, beforeStart};
  if (xTaskCreate(installTask, "fwupd", 10240, p, 1, nullptr) != pdPASS) {
    delete p;
    fail("Not enough memory to start");
    return false;
  }
  return true;
}

bool fwInstallRunning() {
  return fwState == FW_RUNNING;
}

String fwStatusJson() {
  char msg[96];
  FwState st;
  int pct;
  portENTER_CRITICAL(&fwMux);
  st = fwState;
  pct = fwPercent;
  strlcpy(msg, fwMessage, sizeof(msg));
  portEXIT_CRITICAL(&fwMux);
  const char *names[] = {"idle", "running", "done", "error"};
  String m;
  for (const char *c = msg; *c; c++) {   // сообщение без кавычек, чтобы не ломать JSON
    m += (*c == '"' || *c == '\\' || (uint8_t)*c < ' ') ? ' ' : *c;
  }
  return String("{\"state\":\"") + names[st] + "\",\"percent\":" + pct + ",\"message\":\"" + m + "\"}";
}

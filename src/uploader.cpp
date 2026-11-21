#include "uploader.h"
#include <HTTPClient.h>
#include <WiFi.h>

static bool postOnce(const String &url, const String &boundary, const uint8_t *body, size_t len) {
  HTTPClient http;
  http.setTimeout(5000);
  if (!http.begin(url)) return false;
  http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
  int code = http.POST(const_cast<uint8_t *>(body), len);
  http.end();
  Serial.printf("[UPLOAD] HTTP %d\n", code);
  return code >= 200 && code < 300;
}

bool sendToServer(const String &serverUrl, const String &product, const String &filename, const camera_fb_t *fb) {
  if (WiFi.status() != WL_CONNECTED || !fb) return false;

  String url = serverUrl + (serverUrl.indexOf('?') >= 0 ? "&" : "?") + "product=" + product;
  String boundary = "----esp32cam" + String((uint32_t)esp_random(), HEX);
  String head = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"" + filename +
                "\"\r\nContent-Type: image/jpeg\r\n\r\n";
  String tail = "\r\n--" + boundary + "--\r\n";

  size_t total = head.length() + fb->len + tail.length();
  uint8_t *body = (uint8_t *)ps_malloc(total);   // только RAM, на диск не пишем
  if (!body) {
    Serial.println("[ERR] no memory for upload body");
    return false;
  }
  memcpy(body, head.c_str(), head.length());
  memcpy(body + head.length(), fb->buf, fb->len);
  memcpy(body + head.length() + fb->len, tail.c_str(), tail.length());

  bool ok = postOnce(url, boundary, body, total);
  if (!ok) {
    delay(2000);
    ok = postOnce(url, boundary, body, total);
  }
  free(body);
  Serial.println(ok ? "[OK] sent " + filename : "[FAIL] " + filename);
  return ok;
}

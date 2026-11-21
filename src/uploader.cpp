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

bool uploadPrepare(const String &filename, const camera_fb_t *fb, UploadBody &body) {
  if (WiFi.status() != WL_CONNECTED || !fb) return false;

  body.boundary = "----esp32cam" + String((uint32_t)esp_random(), HEX);
  String head = "--" + body.boundary + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"" + filename +
                "\"\r\nContent-Type: image/jpeg\r\n\r\n";
  String tail = "\r\n--" + body.boundary + "--\r\n";

  body.len = head.length() + fb->len + tail.length();
  body.data = (uint8_t *)ps_malloc(body.len);   // только RAM, на диск не пишем
  if (!body.data) {
    body.len = 0;
    Serial.println("[ERR] no memory for upload body");
    return false;
  }
  memcpy(body.data, head.c_str(), head.length());
  memcpy(body.data + head.length(), fb->buf, fb->len);
  memcpy(body.data + head.length() + fb->len, tail.c_str(), tail.length());
  return true;
}

bool uploadSend(const String &serverUrl, const String &product, const String &filename, UploadBody &body) {
  if (!body.data) return false;

  String url = serverUrl + (serverUrl.indexOf('?') >= 0 ? "&" : "?") + "product=" + product;
  bool ok = postOnce(url, body.boundary, body.data, body.len);
  if (!ok) {
    delay(5000);
    ok = postOnce(url, body.boundary, body.data, body.len);
  }
  free(body.data);
  body.data = nullptr;
  body.len = 0;
  Serial.println(ok ? "[OK] sent " + filename : "[FAIL] " + filename);
  return ok;
}

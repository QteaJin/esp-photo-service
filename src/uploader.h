#pragma once
#include <Arduino.h>
#include "esp_camera.h"

// Тело multipart-запроса. Лежит в PSRAM, на диск ничего не пишется.
struct UploadBody {
  uint8_t *data = nullptr;
  size_t len = 0;
  String boundary;
};

// Копирует кадр в тело запроса. После успешного вызова кадр больше не нужен,
// камеру можно освобождать до отправки. false: нет Wi-Fi или не хватило памяти.
bool uploadPrepare(const String &filename, const camera_fb_t *fb, UploadBody &body);

// POST multipart: поле "file", параметр ?product=<id>. 5 с таймаут, одна повторная попытка через 5 с.
// Освобождает body.data в любом случае. Неудача не ставится в очередь: не отправили - не отправили.
bool uploadSend(const String &serverUrl, const String &product, const String &filename, UploadBody &body);

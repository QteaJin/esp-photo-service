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
// Статистика отправки для страницы состояния (только в ОЗУ, сбрасывается при перезагрузке).
struct UploadStats {
  uint32_t ok = 0;        // успешно отправлено кадров
  uint32_t fail = 0;      // не удалось отправить
  int lastCode = 0;       // последний HTTP-код (0 или меньше: ошибка соединения), 0 если ещё не отправляли
  int32_t lastAgoSec = -1;  // сколько секунд назад была последняя попытка, -1 если не было
  float perMin = 0;       // средняя скорость успешных отправок, кадров в минуту
};
UploadStats uploadStats();

bool uploadSend(const String &serverUrl, const String &product, const String &filename, UploadBody &body);

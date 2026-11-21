#pragma once
#include <Arduino.h>
#include "esp_camera.h"

// POST multipart: поле "file", параметр ?product=<id>. 5 с таймаут, одна повторная попытка через 2 с.
bool sendToServer(const String &serverUrl, const String &product, const String &filename, const camera_fb_t *fb);

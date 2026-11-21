#pragma once
#include <Arduino.h>
#include "esp_camera.h"
#include "config.h"

bool cameraInit(const ColorSettings &color);
// Текущая цветопередача (применяется и к превью, и к фото для сервера).
ColorSettings cameraGetColor();
void cameraSetColor(const ColorSettings &color);
// Берёт кадр (удерживает мьютекс). Обязательно вызвать cameraRelease().
// preview=true - уменьшенный кадр (VGA) с поворотом на 180 градусов.
camera_fb_t *cameraAcquire(bool preview);
void cameraRelease(camera_fb_t *fb, bool preview);

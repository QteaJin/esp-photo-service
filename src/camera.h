#pragma once
#include <Arduino.h>
#include "esp_camera.h"

bool cameraInit();
// Берёт кадр (удерживает мьютекс). Обязательно вызвать cameraRelease().
// preview=true - уменьшенный кадр (VGA) с поворотом на 180 градусов.
camera_fb_t *cameraAcquire(bool preview);
void cameraRelease(camera_fb_t *fb, bool preview);

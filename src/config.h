#pragma once
#include <Arduino.h>

// Цветопередача сенсора. Диапазон brightness/contrast/saturation/aeLevel: -2..2, wbMode: 0..4
struct ColorSettings {
  int brightness = 1;
  int contrast = 0;
  int saturation = 0;
  int aeLevel = 1;
  int wbMode = 0;      // 0 авто, 1 солнце, 2 облачно, 3 офис, 4 дом
  bool lenc = true;    // коррекция затенения объектива
};

// Аналог config.json: хранится в NVS (Preferences)
struct Config {
  String serverUrl = "http://192.168.0.100:8000/upload";
  int interval = 30;
  ColorSettings color;
};

void configLoad(Config &cfg);
void configSave(const Config &cfg);

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
  // Сервер прошивок для кнопки "Check for update": http://<хост>:<порт>/version.json и /firmware.bin
  String fwHost = "192.168.0.220";
  int fwPort = 8080;
};

// Адрес приёма фото собирается как http://<IP>:<порт><путь>
static const int DEFAULT_UPLOAD_PORT = 8000;
static const char UPLOAD_PATH[] = "/upload";

// Разбирает сохранённый URL на хост и порт (порт по умолчанию - DEFAULT_UPLOAD_PORT).
bool parseServerUrl(const String &url, String &host, int &port);
String buildServerUrl(const String &host, int port);

void configLoad(Config &cfg);
void configSave(const Config &cfg);

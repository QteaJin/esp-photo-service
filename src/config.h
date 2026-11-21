#pragma once
#include <Arduino.h>

// Аналог config.json: хранится в NVS (Preferences)
struct Config {
  String serverUrl = "http://192.168.0.100:8000/upload";
  int interval = 30;
};

void configLoad(Config &cfg);
void configSave(const Config &cfg);

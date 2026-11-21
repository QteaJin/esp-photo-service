#include "config.h"
#include <Preferences.h>

void configLoad(Config &cfg) {
  Preferences p;
  p.begin("cam", true);
  cfg.serverUrl = p.getString("server_url", cfg.serverUrl);
  cfg.interval = p.getInt("interval", cfg.interval);
  p.end();
}

void configSave(const Config &cfg) {
  Preferences p;
  p.begin("cam", false);
  p.putString("server_url", cfg.serverUrl);
  p.putInt("interval", cfg.interval);
  p.end();
}

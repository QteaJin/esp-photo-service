#include "config.h"
#include <Preferences.h>

void configLoad(Config &cfg) {
  Preferences p;
  p.begin("cam", true);
  cfg.serverUrl = p.getString("server_url", cfg.serverUrl);
  cfg.interval = p.getInt("interval", cfg.interval);
  cfg.color.brightness = p.getInt("c_bright", cfg.color.brightness);
  cfg.color.contrast = p.getInt("c_contr", cfg.color.contrast);
  cfg.color.saturation = p.getInt("c_sat", cfg.color.saturation);
  cfg.color.aeLevel = p.getInt("c_ael", cfg.color.aeLevel);
  cfg.color.wbMode = p.getInt("c_wb", cfg.color.wbMode);
  cfg.color.lenc = p.getBool("c_lenc", cfg.color.lenc);
  p.end();
}

void configSave(const Config &cfg) {
  Preferences p;
  p.begin("cam", false);
  p.putString("server_url", cfg.serverUrl);
  p.putInt("interval", cfg.interval);
  p.putInt("c_bright", cfg.color.brightness);
  p.putInt("c_contr", cfg.color.contrast);
  p.putInt("c_sat", cfg.color.saturation);
  p.putInt("c_ael", cfg.color.aeLevel);
  p.putInt("c_wb", cfg.color.wbMode);
  p.putBool("c_lenc", cfg.color.lenc);
  p.end();
}

#include "config.h"
#include <Preferences.h>

bool parseServerUrl(const String &url, String &host, int &port) {
  port = DEFAULT_UPLOAD_PORT;
  int start = url.indexOf("://");
  start = (start < 0) ? 0 : start + 3;
  int end = url.indexOf('/', start);
  if (end < 0) end = url.length();
  String hostPort = url.substring(start, end);
  int colon = hostPort.indexOf(':');
  if (colon >= 0) {
    host = hostPort.substring(0, colon);
    int p = hostPort.substring(colon + 1).toInt();
    if (p > 0 && p <= 65535) port = p;
  } else {
    host = hostPort;
  }
  return host.length() > 0;
}

String buildServerUrl(const String &host, int port) {
  return String("http://") + host + ":" + String(port) + UPLOAD_PATH;
}

void configLoad(Config &cfg) {
  Preferences p;
  p.begin("cam", true);
  cfg.serverUrl = p.getString("server_url", cfg.serverUrl);
  cfg.interval = p.getInt("interval", cfg.interval);
  cfg.fwHost = p.getString("fw_host", cfg.fwHost);
  cfg.fwPort = p.getInt("fw_port", cfg.fwPort);
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
  p.putString("fw_host", cfg.fwHost);
  p.putInt("fw_port", cfg.fwPort);
  p.putInt("c_bright", cfg.color.brightness);
  p.putInt("c_contr", cfg.color.contrast);
  p.putInt("c_sat", cfg.color.saturation);
  p.putInt("c_ael", cfg.color.aeLevel);
  p.putInt("c_wb", cfg.color.wbMode);
  p.putBool("c_lenc", cfg.color.lenc);
  p.end();
}

#pragma once
#include <Arduino.h>

// Обновление прошивки по кнопке: плата сама скачивает файл с сервера прошивок в локальной сети.
// На сервере должны лежать два файла:
//   http://<хост>:<порт>/version.json  {"version":"...","sha256":"<64 hex>","size":<байт>}
//   http://<хост>:<порт>/firmware.bin
// Их кладёт в папку firmware_out скрипт сборки (scripts/post_ota.py).

struct FwRemote {
  bool ok = false;
  String version;
  String sha256;
  size_t size = 0;
  String error;   // причина, если ok == false
};

// Читает version.json с сервера (блокирует до 5 с).
FwRemote fwFetchRemote(const String &host, int port);

// Запускает скачивание и установку в отдельной задаче. false, если установка уже идёт.
// beforeStart вызывается в этой задаче перед скачиванием (остановить автосъёмку и т.п.).
bool fwInstallStart(const String &host, int port, void (*beforeStart)());

// Состояние установки для панели: {"state":"idle|running|done|error","percent":N,"message":"..."}
String fwStatusJson();
bool fwInstallRunning();

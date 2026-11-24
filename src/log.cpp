#include "log.h"
#include <stdarg.h>
#include <esp_system.h>

static const uint32_t LOG_MAGIC = 0x4C4F4732;   // признак того, что RTC-память содержит наш журнал

struct LogStore {
  uint32_t magic;
  uint16_t head;    // куда писать следующую строку
  uint16_t count;   // сколько строк заполнено
  char lines[LOG_LINES][LOG_LINE_LEN];
};

// RTC_NOINIT_ATTR: значение не обнуляется при перезапуске, пока есть питание
static RTC_NOINIT_ATTR LogStore store;
static char prevLines[LOG_LINES][LOG_LINE_LEN];   // копия журнала прошлого запуска (обычное ОЗУ)
static int prevCount = 0;
static SemaphoreHandle_t mtx = nullptr;

// Копирует строки в хронологическом порядке (от старой к новой). Вызывать под mutex.
static String dumpStore(const char (*lines)[LOG_LINE_LEN], int count, int head) {
  String out;
  out.reserve(count * 48);
  int start = (count < LOG_LINES) ? 0 : head;
  for (int i = 0; i < count; i++) {
    out += lines[(start + i) % LOG_LINES];
    out += '\n';
  }
  return out;
}

void logInit() {
  mtx = xSemaphoreCreateMutex();
  bool valid = store.magic == LOG_MAGIC && store.head < LOG_LINES && store.count <= LOG_LINES;
  // После включения питания RTC-память содержит мусор, ей верить нельзя
  if (valid && esp_reset_reason() != ESP_RST_POWERON) {
    int start = (store.count < LOG_LINES) ? 0 : store.head;
    for (int i = 0; i < store.count; i++) {
      strlcpy(prevLines[i], store.lines[(start + i) % LOG_LINES], LOG_LINE_LEN);
    }
    prevCount = store.count;
  }
  memset(&store, 0, sizeof(store));
  store.magic = LOG_MAGIC;
}

void logWrite(char level, const char *fmt, ...) {
  char msg[LOG_LINE_LEN];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  for (char *p = msg; *p; p++) {
    if (*p == '\n' || *p == '\r') *p = ' ';   // одна запись = одна строка
  }

  uint32_t sec = millis() / 1000;
  char line[LOG_LINE_LEN];
  snprintf(line, sizeof(line), "%02lu:%02lu:%02lu %c %s", (unsigned long)(sec / 3600), (unsigned long)(sec / 60 % 60),
           (unsigned long)(sec % 60), level, msg);

  if (mtx) xSemaphoreTake(mtx, portMAX_DELAY);
  strlcpy(store.lines[store.head], line, LOG_LINE_LEN);
  store.head = (store.head + 1) % LOG_LINES;
  if (store.count < LOG_LINES) store.count++;
  Serial.println(line);
  if (mtx) xSemaphoreGive(mtx);
}

String logDump(bool previous) {
  if (previous) return dumpStore(prevLines, prevCount, 0);   // прошлый журнал уже расположен по порядку
  if (mtx) xSemaphoreTake(mtx, portMAX_DELAY);
  String out = dumpStore(store.lines, store.count, store.head);
  if (mtx) xSemaphoreGive(mtx);
  return out;
}

bool logHasPrevious() {
  return prevCount > 0;
}

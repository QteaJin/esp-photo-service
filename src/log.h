#pragma once
#include <Arduino.h>

// Журнал событий только в ОЗУ: кольцевой буфер последних строк. Во флеш (NVS, SPIFFS, SD) ничего не пишется.
// Буфер лежит в RTC-памяти (это тоже ОЗУ), поэтому переживает аварийный перезапуск
// (паника, watchdog, программный рестарт), но не полное отключение питания.
static const int LOG_LINES = 50;      // сколько последних строк хранить
static const int LOG_LINE_LEN = 128;  // максимум байт в строке вместе с меткой времени

// Вызвать один раз в самом начале setup(). Сохраняет журнал предыдущего запуска и начинает новый.
void logInit();

// Пишет строку в буфер и дублирует в Serial. Потокобезопасно (mutex). Уровень: 'I' инфо, 'W' предупреждение, 'E' ошибка.
void logWrite(char level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

// Текст журнала, по строке на запись: "ЧЧ:ММ:СС У сообщение". previous=true: запуск до последней перезагрузки.
String logDump(bool previous);
bool logHasPrevious();

// Короткие формы: LOG(...) инфо, LOGW(...) предупреждение, LOGE(...) ошибка. Синтаксис как у printf.
#define LOG(...) logWrite('I', __VA_ARGS__)
#define LOGW(...) logWrite('W', __VA_ARGS__)
#define LOGE(...) logWrite('E', __VA_ARGS__)

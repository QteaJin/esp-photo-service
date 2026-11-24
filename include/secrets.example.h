#pragma once
// Скопируйте этот файл в include/secrets.h и задайте свои значения.
// secrets.h добавлен в .gitignore и в репозиторий не попадает.

// Имя платы в сети: <имя>.local (mDNS) и адрес для загрузки по Wi-Fi
#define OTA_HOSTNAME "esp32cam"
// Пароль загрузки по Wi-Fi (ArduinoOTA). Тот же пароль подставит скрипт PlatformIO.
#define OTA_PASSWORD "change-me"

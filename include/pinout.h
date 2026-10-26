#pragma once

// ===================================================================
// Распайка Geeetech / Sparkfun / Стандартного VS1053 MP3 Shield для Arduino Uno
// ===================================================================

// Аппаратные пины шины SPI:
// MOSI -> Digital 11
// MISO -> Digital 12
// SCK  -> Digital 13

#define SHIELD_RESET  -1     // Аппаратный сброс подключен к пину Reset Arduino (-1)
#define SHIELD_CS      9     // Выбор чипа команд VS1053 (XCS)
#define SHIELD_DCS     8     // Выбор чипа данных VS1053 (XDCS / BSYNC)
#define CARDCS         4     // Выбор MicroSD карты (SD_CS)
#define DREQ           2     // Прерывание запроса данных VS1053 (DREQ -> INT0)

// Дополнительные параметры
#define DEFAULT_VOLUME 40    // Громкость по умолчанию (меньше число = громче в VS1053, 0..254)
#define SERIAL_BAUD    115200

#pragma once

// ===================================================================
// Распайка самого частого красного VS1053 MP3 Shield (SparkFun / Geeetech)
// для Arduino Uno
// ===================================================================
//
// Пины соответствуют типичному красному шилду:
//   XCS  = D6, XDCS = D7, SD_CS = D9, DREQ = D2, RESET = D8
// SPI: MOSI=11, MISO=12, SCK=13 (аппаратный SPI Arduino Uno)

// Аппаратные пины шины SPI (фиксированные на Uno):
// MOSI -> Digital 11
// MISO -> Digital 12
// SCK  -> Digital 13

#define SHIELD_RESET  8     // Аппаратный сброс VS1053 (XRST) — пин D8
#define SHIELD_CS     6     // Выбор чипа команд VS1053 (XCS / MCS)
#define SHIELD_DCS    7     // Выбор чипа данных VS1053 (XDCS / BSYNC)
#define CARDCS        9     // Выбор MicroSD карты (SD_CS)
#define DREQ          2     // Запрос данных VS1053 (DREQ -> INT0)

// Дополнительные параметры
#define DEFAULT_VOLUME 40   // Громкость по умолчанию (меньше число = громче, 0..254)
#define SERIAL_BAUD    115200

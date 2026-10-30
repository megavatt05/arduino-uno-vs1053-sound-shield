#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_VS1053.h>
#include "pinout.h"

// Экземпляр плеера с поддержкой прерываний DREQ
Adafruit_VS1053_FilePlayer musicPlayer = 
    Adafruit_VS1053_FilePlayer(SHIELD_RESET, SHIELD_CS, SHIELD_DCS, DREQ, CARDCS);

// Глобальные переменные состояния
uint8_t currentVolume = DEFAULT_VOLUME; // 0 = макс. громкость, 254 = тишина (спецификация VS1053)
uint8_t currentBassBoost = 0;          // 0..15 дБ
uint8_t currentEarSpeaker = 0;         // 0..3 (3D Spatializer)
bool isPlayingTrack = false;
int8_t currentTrackIdx = -1;

const char track0[] PROGMEM = "BEEP.WAV";
const char track1[] PROGMEM = "TRACK01.MP3";
const char track2[] PROGMEM = "TRACK02.MP3";
const char track3[] PROGMEM = "TRACK03.MP3";
const char track4[] PROGMEM = "TRACK04.MP3";
const char track5[] PROGMEM = "TRACK05.MP3";

const char* const playlist[] PROGMEM = {
    track0, track1, track2, track3, track4, track5
};

// Прототипы функций
void printHelp(void);
void printStatus(void);
void listDirectory(File dir, int numTabs);
void playTrack(uint8_t index);
void applyToneControl(uint8_t bassGainDb, uint8_t bassFreqHz, int8_t trebleGainDb, uint8_t trebleFreqKhz);
void applyEarSpeaker(uint8_t level);
void runSineTest(uint8_t n, uint16_t durationMs);

void setup() {
    Serial.begin(SERIAL_BAUD);
    while (!Serial && millis() < 3000);

    Serial.println(F("\r\n=================================================="));
    Serial.println(F("     Arduino Uno + VS1053 MP3 Audio Shield Demo   "));
    Serial.println(F("=================================================="));

    // Инициализация VS1053
    Serial.print(F("[INFO] Инициализация аудио декодера VS1053... "));
    if (!musicPlayer.begin()) {
        Serial.println(F("ОШИБКА! VS1053 не отвечает."));
        Serial.println(F("[СОВЕТ] Проверьте плотность посадки шилда на Arduino Uno."));
        while (1) {
            delay(1000);
        }
    }
    Serial.println(F("OK!"));

    // Инициализация MicroSD карты
    Serial.print(F("[INFO] Инициализация MicroSD карты (CS=9)... "));
    if (!SD.begin(CARDCS)) {
        Serial.println(F("ОШИБКА!"));
        Serial.println(F("[СОВЕТ] Вставьте MicroSD (Drive E:) в слот шилда."));
    } else {
        Serial.println(F("OK!"));
    }

    // Установка прерывания DREQ для неблокирующего фонового воспроизведения
    musicPlayer.useInterrupt(VS1053_FILEPLAYER_PIN_INT);

    // Установка громкости по умолчанию (левый и правый каналы)
    musicPlayer.setVolume(currentVolume, currentVolume);

    printHelp();
}

void loop() {
    // Автопереход на следующий трек при окончании
    if (isPlayingTrack && musicPlayer.stopped()) {
        Serial.println(F("\r\n[ПЛЕЕР] Воспроизведение трека завершено."));
        isPlayingTrack = false;
        currentTrackIdx = -1;
    }

    // Обработка команд из Serial Monitor
    if (Serial.available()) {
        char ch = (char)Serial.read();
        if (ch == '\r' || ch == '\n' || ch == ' ') return;

        switch (ch) {
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5': {
                uint8_t idx = ch - '0';
                playTrack(idx);
                break;
            }

            case 'p':
            case 'P': {
                if (musicPlayer.paused()) {
                    musicPlayer.pausePlaying(false);
                    Serial.println(F("[ПЛЕЕР] Возобновление (Resume)"));
                } else if (isPlayingTrack && !musicPlayer.stopped()) {
                    musicPlayer.pausePlaying(true);
                    Serial.println(F("[ПЛЕЕР] Пауза (Pause)"));
                } else {
                    Serial.println(F("[ПЛЕЕР] Ничего не играет."));
                }
                break;
            }

            case 's':
            case 'S': {
                if (isPlayingTrack || !musicPlayer.stopped()) {
                    musicPlayer.stopPlaying();
                    isPlayingTrack = false;
                    currentTrackIdx = -1;
                    Serial.println(F("[ПЛЕЕР] Воспроизведение остановлено (Stop)."));
                } else {
                    Serial.println(F("[ПЛЕЕР] Уже остановлен."));
                }
                break;
            }

            case '+': {
                if (currentVolume > 10) {
                    currentVolume -= 10;
                    musicPlayer.setVolume(currentVolume, currentVolume);
                    Serial.print(F("[ГРОМКОСТЬ] "));
                    Serial.print(100 - (currentVolume * 100 / 254));
                    Serial.println(F("%"));
                }
                break;
            }

            case '-': {
                if (currentVolume < 240) {
                    currentVolume += 10;
                    musicPlayer.setVolume(currentVolume, currentVolume);
                    Serial.print(F("[ГРОМКОСТЬ] "));
                    Serial.print(100 - (currentVolume * 100 / 254));
                    Serial.println(F("%"));
                }
                break;
            }

            case 'b':
            case 'B': {
                // Цикл: 0 -> 6 -> 12 -> 15 -> 0
                if (currentBassBoost == 0) currentBassBoost = 6;
                else if (currentBassBoost == 6) currentBassBoost = 12;
                else if (currentBassBoost == 12) currentBassBoost = 15;
                else currentBassBoost = 0;
                applyToneControl(currentBassBoost, 150, 0, 15);
                Serial.print(F("[ЭКВАЛАЙЗЕР] Bass Boost: +"));
                Serial.print(currentBassBoost);
                Serial.println(F(" dB"));
                break;
            }

            case 'e':
            case 'E': {
                currentEarSpeaker = (currentEarSpeaker + 1) % 4;
                applyEarSpeaker(currentEarSpeaker);
                Serial.print(F("[3D] EarSpeaker уровень: "));
                Serial.println(currentEarSpeaker);
                break;
            }

            case 't':
            case 'T': {
                Serial.println(F("[ТЕСТ] Запуск аппаратного синус-теста 440 Гц..."));
                runSineTest(0x44, 2000); // 440 Гц примерно
                Serial.println(F("[ТЕСТ] Синус-тест завершён."));
                break;
            }

            case 'l':
            case 'L': {
                Serial.println(F("\r\n--- Список файлов на MicroSD ---"));
                File root = SD.open("/");
                if (root) {
                    listDirectory(root, 0);
                    root.close();
                } else {
                    Serial.println(F("Не удалось открыть корневой каталог SD."));
                }
                Serial.println(F("-------------------------------"));
                break;
            }

            case 'i':
            case 'I': {
                printStatus();
                break;
            }

            case '?':
            case 'h':
            case 'H': {
                printHelp();
                break;
            }

            default:
                Serial.print(F("[?] Неизвестная команда: '"));
                Serial.print(ch);
                Serial.println(F("' (нажмите ? для справки)"));
                break;
        }
    }
}

void playTrack(uint8_t index) {
    if (index > 5) return;

    char name[13];
    strcpy_P(name, (char*)pgm_read_word(&(playlist[index])));

    if (isPlayingTrack || !musicPlayer.stopped()) {
        musicPlayer.stopPlaying();
        delay(50);
    }

    Serial.print(F("[ПЛЕЕР] Запуск: "));
    Serial.println(name);

    if (!musicPlayer.startPlayingFile(name)) {
        Serial.println(F("[ОШИБКА] Файл не найден или не удалось начать воспроизведение."));
        isPlayingTrack = false;
        currentTrackIdx = -1;
        return;
    }

    isPlayingTrack = true;
    currentTrackIdx = index;
}

void applyToneControl(uint8_t bassGainDb, uint8_t bassFreqHz, int8_t trebleGainDb, uint8_t trebleFreqKhz) {
    // SCI_BASS: биты 15..12 = Treble gain (-8..+7), 11..8 = Treble freq (kHz),
    //           7..4 = Bass gain (0..15), 3..0 = Bass freq (10 Hz units)
    uint16_t bass = 0;
    bass |= ((uint16_t)(trebleGainDb & 0x0F) << 12);
    bass |= ((uint16_t)(trebleFreqKhz & 0x0F) << 8);
    bass |= ((uint16_t)(bassGainDb & 0x0F) << 4);
    bass |= ((uint16_t)((bassFreqHz / 10) & 0x0F));
    musicPlayer.sciWrite(VS1053_REG_BASS, bass);
}

void applyEarSpeaker(uint8_t level) {
    // Режим EarSpeaker через SCI_MODE бит SM_EARSPEAKER_*
    // Библиотека Adafruit предоставляет setEarSpeakerLevel
    musicPlayer.setEarSpeakerLevel(level);
}

void runSineTest(uint8_t n, uint16_t durationMs) {
    musicPlayer.sineTest(n, durationMs);
}

void printStatus(void) {
    Serial.println(F("\r\n---------- СТАТУС ПЛАТЫ ----------"));
    Serial.print(F("Плеер: "));
    if (musicPlayer.paused()) Serial.println(F("ПАУЗА"));
    else if (isPlayingTrack && !musicPlayer.stopped()) {
        Serial.print(F("ИГРАЕТ (Трек "));
        Serial.print(currentTrackIdx);
        Serial.println(F(")"));
    } else {
        Serial.println(F("ОСТАНОВЛЕН"));
    }

    Serial.print(F("Громкость: "));
    Serial.print(100 - (currentVolume * 100 / 254));
    Serial.println(F("%"));

    Serial.print(F("Эквалайзер Bass Boost: +"));
    Serial.print(currentBassBoost);
    Serial.println(F(" dB"));

    Serial.print(F("3D EarSpeaker (пространственный звук): "));
    Serial.println(currentEarSpeaker);

    uint16_t status = musicPlayer.sciRead(VS1053_REG_STATUS);
    Serial.print(F("Регистр SCI_STATUS: 0x"));
    Serial.println(status, HEX);

    uint16_t clockf = musicPlayer.sciRead(VS1053_REG_CLOCKF);
    Serial.print(F("Регистр SCI_CLOCKF: 0x"));
    Serial.println(clockf, HEX);
    Serial.println(F("-------------------------------"));
}

void printHelp(void) {
    Serial.println(F("\r\n================= КОМАНДЫ УПРАВЛЕНИЯ ================="));
    Serial.println(F(" [0..5] : Запуск трека (0=BEEP.WAV, 1..5=TRACK01..05.MP3)"));
    Serial.println(F("   p    : Пауза / Возобновление воспроизведения"));
    Serial.println(F("   s    : Остановить воспроизведение"));
    Serial.println(F("   +    : Увеличить громкость"));
    Serial.println(F("   -    : Уменьшить громкость"));
    Serial.println(F("   b    : Переключить Bass Boost (+0 / +6 / +12 / +15 dB)"));
    Serial.println(F("   e    : Переключить 3D Spatial Audio (EarSpeaker 0..3)"));
    Serial.println(F("   t    : Запустить встроенный аппаратный синус-тест"));
    Serial.println(F("   l    : Показать список файлов на MicroSD"));
    Serial.println(F("   i    : Показать состояние регистров и статус платы"));
    Serial.println(F("   ?    : Показать это меню"));
    Serial.println(F("======================================================"));
}

void listDirectory(File dir, int numTabs) {
    while (true) {
        File entry = dir.openNextFile();
        if (!entry) break;

        for (uint8_t i = 0; i < numTabs; i++) {
            Serial.print(F("  "));
        }
        Serial.print(entry.name());

        if (entry.isDirectory()) {
            Serial.println(F("/"));
            listDirectory(entry, numTabs + 1);
        } else {
            Serial.print(F(" \t\t"));
            Serial.print(entry.size(), DEC);
            Serial.println(F(" байт"));
        }
        entry.close();
    }
}

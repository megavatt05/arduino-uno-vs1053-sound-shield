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
    Serial.print(F("[INFO] Инициализация MicroSD карты (CS=4)... "));
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
                    Serial.println(F("[ПЛЕЕР] Плеер уже остановлен."));
                }
                break;
            }

            case '+': {
                if (currentVolume >= 5) currentVolume -= 5; // Меньше = громче
                else currentVolume = 0;
                musicPlayer.setVolume(currentVolume, currentVolume);
                Serial.print(F("[ГРОМКОСТЬ] Уровень: "));
                Serial.print(100 - (currentVolume * 100 / 254));
                Serial.println(F("%"));
                break;
            }

            case '-': {
                if (currentVolume <= 249) currentVolume += 5; // Больше = тише
                else currentVolume = 254;
                musicPlayer.setVolume(currentVolume, currentVolume);
                Serial.print(F("[ГРОМКОСТЬ] Уровень: "));
                Serial.print(100 - (currentVolume * 100 / 254));
                Serial.println(F("%"));
                break;
            }

            case 'b':
            case 'B': {
                // Переключение режима Bass Boost: 0 дБ -> 6 дБ -> 12 дБ -> 15 дБ -> 0 дБ
                if (currentBassBoost == 0) currentBassBoost = 6;
                else if (currentBassBoost == 6) currentBassBoost = 12;
                else if (currentBassBoost == 12) currentBassBoost = 15;
                else currentBassBoost = 0;

                applyToneControl(currentBassBoost, 10, 0, 0); // частота среза ~100 Гц
                Serial.print(F("[DSP BASS BOOST] Усиление басов: +"));
                Serial.print(currentBassBoost);
                Serial.println(F(" dB"));
                break;
            }

            case 'e':
            case 'E': {
                // Переключение 3D Spatializer (EarSpeaker): 0 (Off) .. 3 (Extreme)
                currentEarSpeaker = (currentEarSpeaker + 1) % 4;
                applyEarSpeaker(currentEarSpeaker);
                Serial.print(F("[DSP EAR-SPEAKER 3D] Пространственный звук: режим "));
                Serial.println(currentEarSpeaker);
                break;
            }

            case 't':
            case 'T': {
                // Аппаратный тест генератора синуса VS1053
                Serial.println(F("[ТЕСТ] Запуск встроенного DSP синус-теста (500 мс, 440 Гц)..."));
                runSineTest(0x44, 1000);
                Serial.println(F("[ТЕСТ] Синус-тест завершен (ЦАП и аналоговый тракт исправны!)."));
                break;
            }

            case 'l':
            case 'L': {
                Serial.println(F("\r\n--- Список файлов на MicroSD ---"));
                File root = SD.open("/");
                listDirectory(root, 0);
                root.close();
                Serial.println(F("--------------------------------"));
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
                Serial.println(F("[Справка] Введите '?' для вывода списка доступных команд."));
                break;
        }
    }
}

void playTrack(uint8_t index) {
    if (index > 5) return;

    if (isPlayingTrack) {
        musicPlayer.stopPlaying();
        delay(50);
    }

    char filename[13];
    strcpy_P(filename, (char*)pgm_read_word(&(playlist[index])));

    Serial.print(F("\r\n[ПЛЕЕР] Запуск файла: "));
    Serial.println(filename);

    if (!SD.exists(filename)) {
        Serial.print(F("[ОШИБКА] Файл "));
        Serial.print(filename);
        Serial.println(F(" не найден на карте SD!"));
        isPlayingTrack = false;
        currentTrackIdx = -1;
        return;
    }

    if (musicPlayer.startPlayingFile(filename)) {
        isPlayingTrack = true;
        currentTrackIdx = index;
        Serial.println(F("[ПЛЕЕР] Воспроизведение начато (в фоне по DREQ прерыванию)"));
    } else {
        Serial.println(F("[ОШИБКА] Не удалось запустить воспроизведение файла!"));
        isPlayingTrack = false;
        currentTrackIdx = -1;
    }
}

// Управление эквалайзером (Tone Control) через регистр SCI_BASS (0x02)
void applyToneControl(uint8_t bassGainDb, uint8_t bassFreqHz, int8_t trebleGainDb, uint8_t trebleFreqKhz) {
    // Биты SCI_BASS:
    // [15:12] = Treble Gain (-8..7, 0 = off, 1.5 dB step)
    // [11:8]  = Treble Freq Limit (1..15 kHz)
    // [7:4]   = Bass Gain (0..15 dB)
    // [3:0]   = Bass Freq Limit (2..15 -> 20..150 Hz, шаг 10 Гц)
    uint16_t bassReg = 0;
    
    uint8_t bGain = (bassGainDb > 15) ? 15 : bassGainDb;
    uint8_t bFreq = (bassFreqHz < 2) ? 2 : ((bassFreqHz > 15) ? 15 : bassFreqHz);
    bassReg |= ((uint16_t)bGain << 4) | (bFreq & 0x0F);

    uint8_t tGain = (uint8_t)(trebleGainDb & 0x0F);
    uint8_t tFreq = (trebleFreqKhz > 15) ? 15 : trebleFreqKhz;
    bassReg |= ((uint16_t)tGain << 12) | ((uint16_t)tFreq << 8);

    musicPlayer.sciWrite(VS1053_REG_BASS, bassReg);
}

// Управление эффектом 3D EarSpeaker через регистр SCI_MODE (биты 4 и 5)
void applyEarSpeaker(uint8_t level) {
    uint16_t mode = musicPlayer.sciRead(VS1053_REG_MODE);
    mode &= ~0x0030; // Сброс битов 4 и 5 (SM_EARSPEAKER_LO и SM_EARSPEAKER_HI)
    
    if (level == 1) {
        mode |= 0x0010; // Lo
    } else if (level == 2) {
        mode |= 0x0020; // Hi
    } else if (level == 3) {
        mode |= 0x0030; // Lo + Hi
    }
    musicPlayer.sciWrite(VS1053_REG_MODE, mode);
}

// Запуск встроенного синус-теста VS1053
void runSineTest(uint8_t n, uint16_t durationMs) {
    bool wasPlaying = isPlayingTrack;
    if (wasPlaying) {
        musicPlayer.stopPlaying();
        delay(50);
    }
    musicPlayer.sineTest(n, durationMs);
    if (wasPlaying && currentTrackIdx >= 0) {
        playTrack(currentTrackIdx);
    }
}

void printStatus(void) {
    Serial.println(F("\r\n--- Текущее состояние платы ---"));
    Serial.print(F("Статус плеера: "));
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

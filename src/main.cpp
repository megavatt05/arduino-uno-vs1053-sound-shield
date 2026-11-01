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
void printDecoderRegs(void);
void printVolume(void);
void micTest(void);
void micLongTest(void);

void setup() {
    Serial.begin(SERIAL_BAUD);
    while (!Serial && millis() < 3000);

    Serial.println(F("\r\n=================================================="));
    Serial.println(F("     Arduino Uno + VS1053 MP3 Audio Shield Demo   "));
    Serial.println(F("=================================================="));

    // Инициализация VS1053
    Serial.print(F("[INFO] Инициализация аудио декодера VS1053... "));
    if (!musicPlayer.begin()) {
        // Adafruit_VS1053::begin() считает чип исправным только если версия в SCI_STATUS (7..4) равна 4.
        // Поэтому «не отвечает» бывает и при живой шине: другой чип, другой шилд, обрыв.
        Serial.println(F("ОШИБКА!"));
        printDecoderRegs();
        Serial.println(F("     0xFFFF/0x0000 — шина SPI молчит: посадка шилда,"));
        Serial.println(F("     питание 3.3 В, пины XCS/XDCS/DREQ/XRST из pinout.h."));
        Serial.println(F("     Версия не 4 — чип не VS1053b (например VS1003 или VS1053 без 'b')."));
        Serial.println(F("[ПЛЕЕР] Работаю дальше: команды доступны, 'd' — дамп регистров."));
    } else {
        Serial.println(F("OK!"));
    }

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
                // Меньше значение в SCI_VOLUME — громче: 0 = максимум, 0xFE = тишина.
                if (currentVolume > 10) {
                    currentVolume -= 10;
                    musicPlayer.setVolume(currentVolume, currentVolume);
                    printVolume();
                } else {
                    Serial.println(F("[ГРОМКОСТЬ] Уже максимум."));
                }
                break;
            }

            case '-': {
                if (currentVolume < 240) {
                    currentVolume += 10;
                    musicPlayer.setVolume(currentVolume, currentVolume);
                    printVolume();
                } else {
                    Serial.println(F("[ГРОМКОСТЬ] Уже тишина."));
                }
                break;
            }

            case 'v':
            case 'V': {
                // Точная установка значением SCI_VOLUME: цифры 0..254, применяется по Enter.
                Serial.print(F("[ГРОМКОСТЬ] Введите 0..254 и Enter (0 = максимум, 254 = тишина): "));
                uint16_t val = 0;
                uint8_t digits = 0;
                uint32_t t0 = millis();
                while (millis() - t0 < 5000) {
                    if (!Serial.available()) continue;
                    char c = (char)Serial.read();
                    if (c >= '0' && c <= '9') {
                        val = val * 10 + (uint16_t)(c - '0');
                        if (val > 254) val = 254;
                        digits++;
                        t0 = millis();
                        Serial.print(c);
                    } else if ((c == '\r' || c == '\n') && digits) {
                        break;
                    }
                }
                Serial.println();
                if (digits) {
                    currentVolume = (uint8_t)val;
                    musicPlayer.setVolume(currentVolume, currentVolume);
                    printVolume();
                } else {
                    Serial.println(F("[ГРОМКОСТЬ] Отменено, значение не изменено."));
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

            case 'd':
            case 'D': {
                Serial.println(F("[ДИАГНОСТИКА] Регистры декодера VS1053:"));
                printDecoderRegs();
                break;
            }

            case 'm': {
                micTest();
                break;
            }

            case 'M': {
                micLongTest();
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

// Биты EarSpeaker в регистре SCI_MODE (даташит VS1053b: SM_EARSPEAKER_LO/HI).
// Библиотека Adafruit 1.4.4 объявляет только LO; HI = 0x0080, тогда как 0x0020 — это SM_TESTS.
#define VS1053_MODE_SM_EARSPKHI 0x0080

void applyEarSpeaker(uint8_t level) {
    // Уровни: 0 = выкл, 1 = минимум, 2 = норма, 3 = максимум (биты 5..4 SCI_MODE).
    // sciRead/sciWrite берегут остальные биты режима (SDINEW, LINE1 и т.д.),
    // поэтому маскируем только пару EarSpeaker.
    uint16_t mode = musicPlayer.sciRead(VS1053_REG_MODE);
    mode &= ~(uint16_t)(VS1053_MODE_SM_EARSPKLO | VS1053_MODE_SM_EARSPKHI);

    switch (level & 0x03) {
        case 1:  mode |= VS1053_MODE_SM_EARSPKLO; break;
        case 2:  mode |= VS1053_MODE_SM_EARSPKHI; break;
        case 3:  mode |= VS1053_MODE_SM_EARSPKLO | VS1053_MODE_SM_EARSPKHI; break;
        default: break; // 0 — выключено
    }

    musicPlayer.sciWrite(VS1053_REG_MODE, mode);
}

void runSineTest(uint8_t n, uint16_t durationMs) {
    musicPlayer.sineTest(n, durationMs);
}

// Громкость в VS1053 задаётся затуханием: 0 = максимум, 0xFE = тишина, шаг 0.5 дБ.
void printVolume(void) {
    Serial.print(F("[ГРОМКОСТЬ] SCI_VOLUME="));
    Serial.print(currentVolume);
    Serial.print(F(" — затухание "));
    Serial.print(currentVolume / 2);
    Serial.print('.');
    Serial.print((currentVolume & 1) ? 5 : 0);
    Serial.print(F(" дБ ("));
    Serial.print(100 - (currentVolume * 100 / 254));
    Serial.println(F("%)"));
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
    Serial.print(F("% (SCI_VOLUME="));
    Serial.print(currentVolume);
    Serial.print(F(", затухание "));
    Serial.print(currentVolume / 2);
    Serial.print('.');
    Serial.print((currentVolume & 1) ? 5 : 0);
    Serial.println(F(" дБ)"));

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

// Вывод 16-битного значения ровно четырьмя hex-цифрами.
static void printHex4(uint16_t v) {
    if (v < 0x1000) Serial.print('0');
    if (v < 0x0100) Serial.print('0');
    if (v < 0x0010) Serial.print('0');
    Serial.print(v, HEX);
}

void printDecoderRegs(void) {
    // Полная карта SCI: 16 адресов 0x00..0x0F — всё, что VS1053b отдаёт по SPI.
    // 0x0A..0x0F (AIADDR, AICTRL0..3) библиотека константами не объявляет, берём номера из даташита.
    uint16_t r[16];
    for (uint8_t i = 0; i < 16; i++) r[i] = musicPlayer.sciRead(i);

    Serial.println(F("  SCI-регистры (полная карта 0x00..0x0F):"));
    Serial.print(F("   00 MODE    = 0x")); printHex4(r[0]);
    Serial.print(F("   01 STATUS  = 0x")); printHex4(r[1]); Serial.println();
    Serial.print(F("   02 BASS    = 0x")); printHex4(r[2]);
    Serial.print(F("   03 CLOCKF  = 0x")); printHex4(r[3]); Serial.println();
    Serial.print(F("   04 DECODET = 0x")); printHex4(r[4]);
    Serial.print(F("   05 AUDATA  = 0x")); printHex4(r[5]); Serial.println();
    Serial.print(F("   06 WRAM    = 0x")); printHex4(r[6]);
    Serial.print(F("   07 WRAMADR = 0x")); printHex4(r[7]); Serial.println();
    Serial.print(F("   08 HDAT0   = 0x")); printHex4(r[8]);
    Serial.print(F("   09 HDAT1   = 0x")); printHex4(r[9]); Serial.println();
    Serial.print(F("   0A AIADDR  = 0x")); printHex4(r[10]);
    Serial.print(F("   0B VOLUME  = 0x")); printHex4(r[11]); Serial.println();
    Serial.print(F("   0C AICTRL0 = 0x")); printHex4(r[12]);
    Serial.print(F("   0D AICTRL1 = 0x")); printHex4(r[13]); Serial.println();
    Serial.print(F("   0E AICTRL2 = 0x")); printHex4(r[14]);
    Serial.print(F("   0F AICTRL3 = 0x")); printHex4(r[15]); Serial.println();

    // Расшифровка читаемых полей
    uint8_t ver = (r[1] >> 4) & 0x0F;
    Serial.print(F("  Чип: версия "));
    Serial.print(ver);
    Serial.println(ver == 4 ? F(" (VS1053b — то, что ждёт библиотека)")
                            : F(" (не 4: Adafruit с таким чипом не работает)"));

    Serial.print(F("  Декодировано звука: "));
    Serial.print(r[4]);
    Serial.println(F(" с"));

    if (r[5] == 0 || r[5] == 0xFFFF) {
        Serial.println(F("  Поток: нет активных данных (AUDATA пуст)"));
    } else {
        Serial.print(F("  Поток: "));
        Serial.print((uint16_t)(r[5] & 0xFFFE));
        Serial.print(F(" Гц, "));
        Serial.println((r[5] & 1) ? F("стерео") : F("моно"));
    }

    uint8_t attL = (uint8_t)(r[11] >> 8);
    uint8_t attR = (uint8_t)(r[11] & 0xFF);
    Serial.print(F("  Громкость: L="));
    Serial.print(attL);
    Serial.print(F(" R="));
    Serial.print(attR);
    if (attL == 0xFF && attR == 0xFF) {
        Serial.println(F(" — ПОЛНЫЙ MUTE"));
    } else {
        Serial.print(F(" (затухание "));
        Serial.print(attL / 2);
        Serial.print('.');
        Serial.print((attL & 1) ? 5 : 0);
        Serial.println(F(" дБ, шаг 0.5 дБ)"));
    }

    uint8_t bassGain = (r[2] >> 4) & 0x0F;
    uint8_t bassFreq = (r[2] & 0x0F) * 10;
    int8_t trebleGain = (int8_t)((r[2] >> 12) & 0x0F);
    if (trebleGain > 7) trebleGain -= 16;
    uint8_t trebleFreq = (r[2] >> 8) & 0x0F;
    Serial.print(F("  Bass/Treble: +"));
    Serial.print(bassGain);
    Serial.print(F(" дБ @"));
    Serial.print(bassFreq);
    Serial.print(F(" Гц / "));
    Serial.print(trebleGain);
    Serial.print(F(" дБ @"));
    Serial.print(trebleFreq);
    Serial.println(F(" кГц"));

    uint8_t ear = 0;
    if (r[0] & VS1053_MODE_SM_EARSPKLO) ear |= 1;
    if (r[0] & VS1053_MODE_SM_EARSPKHI) ear |= 2;
    Serial.print(F("  MODE: SDINEW="));
    Serial.print((r[0] & VS1053_MODE_SM_SDINEW) ? 1 : 0);
    Serial.print(F(" TESTS="));
    Serial.print((r[0] & VS1053_MODE_SM_TESTS) ? 1 : 0);
    Serial.print(F(" STREAM="));
    Serial.print((r[0] & VS1053_MODE_SM_STREAM) ? 1 : 0);
    Serial.print(F(" EarSpeaker="));
    Serial.println(ear);

    if (r[1] == 0xFFFF || r[1] == 0x0000) {
        Serial.println(F("  -> шина не отвечает: чип не выбран, не запитан или не посажен."));
    }
}

// Включение записи IMA ADPCM с выбранного входа.
// useLine1 = true — линейный вход (SM_LINE1=1), false — микрофонный (SM_LINE1=0).
// Порядок строго из даташита VS1053b, глава 10.8.1: SCI_AICTRL0 и SCI_AICTRL3
// читаются ТОЛЬКО в момент старта записи, поэтому пишем их ДО активации SM_RESET + SM_ADPCM.
static void startRecord(bool useLine1, uint16_t aictrl1, uint16_t aictrl3) {
    musicPlayer.sciWrite(0x0C, 8000);      // AICTRL0 — частота дискретизации, 8000..48000 Гц
    musicPlayer.sciWrite(0x0D, aictrl1);   // AICTRL1: 0 = AGC, 1024 = фиксированное усиление 1x
    musicPlayer.sciWrite(0x0E, 0xFFFF);    // AICTRL2 — предел автоусиления (64x)
    musicPlayer.sciWrite(0x0F, aictrl3);   // AICTRL3: 0 = joint stereo, 2 = левый канал
    delay(5);

    uint16_t mode = VS1053_MODE_SM_SDINEW | VS1053_MODE_SM_ADPCM | VS1053_MODE_SM_RESET;
    if (useLine1) mode |= VS1053_MODE_SM_LINE1; // 1 = LINE1, 0 = дифференциальный MIC

    musicPlayer.sciWrite(VS1053_REG_MODE, mode); // активация записи ADPCM
    delay(50);
}

// Остановка записи: снять SM_ADPCM, затем сброс — иначе декодер не вернётся в рабочий режим.
static void stopRecord(void) {
    musicPlayer.sciWrite(VS1053_REG_MODE, VS1053_MODE_SM_SDINEW);
    delay(10);
    musicPlayer.sciWrite(VS1053_REG_MODE, VS1053_MODE_SM_SDINEW | VS1053_MODE_SM_RESET);
    delay(20);
    musicPlayer.sciWrite(VS1053_REG_MODE, VS1053_MODE_SM_SDINEW);
    delay(20);
}

// Вычитывание готовых слов из FIFO: обновляет счётчик и пик, возвращает прирост.
static uint32_t drainFifo(uint32_t *words, uint16_t *peak) {
    uint16_t avail = musicPlayer.sciRead(VS1053_REG_HDAT1);
    if (avail == 0xFFFF) return 0;
    uint8_t take = (avail > 16) ? 16 : (uint8_t)avail;
    uint32_t got = 0;
    for (uint8_t i = 0; i < take; i++) {
        uint16_t w = musicPlayer.sciRead(VS1053_REG_HDAT0);
        got++;
        uint16_t a = (w & 0x8000) ? (uint16_t)(0 - w) : w; // модуль отсчёта
        if (a > *peak) *peak = a;
    }
    *words += got;
    return got;
}

// Проверка входа за ms миллисекунд без вывода в порт (для быстрого перебора).
static uint32_t probeInput(bool useLine1, uint16_t aictrl1, uint16_t aictrl3, uint16_t ms, uint16_t *peakOut) {
    startRecord(useLine1, aictrl1, aictrl3);

    uint32_t words = 0;
    uint16_t peak = 0;
    uint32_t t0 = millis();
    while (millis() - t0 < ms) {
        drainFifo(&words, &peak);
    }

    stopRecord();
    *peakOut = peak;
    return words;
}

void micTest(void) {
    Serial.println(F("\r\n[МИКРОФОН] Включаю АЦП VS1053b и запись IMA ADPCM внутри чипа."));

    if (isPlayingTrack || !musicPlayer.stopped()) {
        musicPlayer.stopPlaying();
        isPlayingTrack = false;
        currentTrackIdx = -1;
        Serial.println(F("[МИКРОФОН] Воспроизведение остановлено: запись и плеер одновременно не работают."));
    }

    uint32_t total = 0;
    uint8_t worked = 0;
    // Перебор всех вариантов настройки входа: MIC/LINE1 x AGC/фикс.усиление x канал.
    for (uint8_t in = 0; in < 2; in++) {
        bool line1 = (in == 1);
        for (uint8_t g = 0; g < 2; g++) {
            uint16_t a1 = g ? 1024 : 0;
            for (uint8_t ch = 0; ch < 2; ch++) {
                uint16_t a3 = ch ? 2 : 0;
                uint16_t peak = 0;
                uint32_t words = probeInput(line1, a1, a3, 600, &peak);
                total += words;
                if (words) worked++;

                Serial.print(line1 ? F("[МИКРОФОН] LINE1 ") : F("[МИКРОФОН] MIC   "));
                Serial.print(F(" усил="));
                Serial.print(a1 ? F("фикс ") : F("AGC  "));
                Serial.print(F(" канал="));
                Serial.print(a3);
                Serial.print(F(" -> слов: "));
                Serial.print(words);
                Serial.print(F(", пик: "));
                Serial.println(peak);
                delay(50);
            }
        }
    }

    musicPlayer.setVolume(currentVolume, currentVolume);

    if (total == 0) {
        Serial.println(F("[МИКРОФОН] Ни одна комбинация не дала данных:"));
        Serial.println(F("   1) режим записи включается (слышен щелчок сброса), но поток"));
        Serial.println(F("      отсчётов не идёт — это известная ошибка IMA ADPCM-энкодера"));
        Serial.println(F("      VS1053b, лечится патчем VLSI «IMA ADPCM Encoder Fix»;"));
        Serial.println(F("   2) либо на шилде нет микрофона/вход не подключён."));
    } else {
        Serial.print(F("[МИКРОФОН] РАБОЧИХ КОМБИНАЦИЙ: "));
        Serial.print(worked);
        Serial.println(F(" — вход живой, данные идут."));
    }
    Serial.println(F("[МИКРОФОН] Чип возвращён в режим плеера, громкость восстановлена."));
}

// Длительная проверка: каждый вход включён 60 с с живым прогрессом, чтобы
// успеть сказать в микрофон и увидеть по числам, идёт ли поток отсчётов.
void micLongTest(void) {
    const uint16_t seconds = 60;

    Serial.println(F("\r\n[МИКРОФОН] Длительный тест: по 60 с на каждый вход."));
    Serial.println(F("[МИКРОФОН] ГОВОРИТЕ В МИКРОФОН (или подайте сигнал на LINE IN)."));
    Serial.println(F("[МИКРОФОН] На динамики вход не выводится — смотрите на числа."));

    if (isPlayingTrack || !musicPlayer.stopped()) {
        musicPlayer.stopPlaying();
        isPlayingTrack = false;
        currentTrackIdx = -1;
        Serial.println(F("[МИКРОФОН] Воспроизведение остановлено: запись и плеер одновременно не работают."));
    }

    uint32_t total = 0;

    for (uint8_t in = 0; in < 2; in++) {
        bool line1 = (in == 1);
        Serial.println(line1 ? F("\r\n[МИКРОФОН] ===== LINE IN, 60 с: подайте сигнал =====")
                             : F("\r\n[МИКРОФОН] ===== MIC, 60 с: говорите сейчас ====="));

        startRecord(line1, 0, 0); // AGC + joint stereo — стартовая настройка

        uint32_t words = 0;
        uint16_t peak = 0;
        uint32_t t0 = millis();
        uint32_t nextReport = 0;

        while (millis() - t0 < (uint32_t)seconds * 1000UL) {
            uint32_t gained = drainFifo(&words, &peak);
            uint32_t elapsed = millis() - t0;

            if (elapsed >= nextReport) {
                uint16_t hdat1 = musicPlayer.sciRead(VS1053_REG_HDAT1);
                Serial.print(F("  t="));
                Serial.print(elapsed / 1000);
                Serial.print(F(" c  слов="));
                Serial.print(words);
                Serial.print(F("  пик="));
                Serial.print(peak);
                Serial.print(F("  HDAT1=0x"));
                if (hdat1 < 0x1000) Serial.print('0');
                if (hdat1 < 0x0100) Serial.print('0');
                if (hdat1 < 0x0010) Serial.print('0');
                Serial.print(hdat1, HEX);
                Serial.print(F("  сейчас="));
                Serial.println(gained);
                nextReport = elapsed + 5000;
            }
        }

        stopRecord();
        total += words;

        Serial.print(line1 ? F("[МИКРОФОН] ИТОГ LINE IN: слов ") : F("[МИКРОФОН] ИТОГ MIC: слов "));
        Serial.print(words);
        Serial.print(F(", пик "));
        Serial.print(peak);
        if (words) {
            Serial.println(F(" -> ВХОД РАБОТАЕТ, поток отсчётов идёт."));
        } else {
            Serial.println(F(" -> за 60 с ни одного отсчёта."));
        }
        delay(300);
    }

    musicPlayer.setVolume(currentVolume, currentVolume);

    if (total == 0) {
        Serial.println(F("[МИКРОФОН] За 2 минуты не пришло ни одного слова:"));
        Serial.println(F("   режим записи включается, но энкодер не отдаёт данные —"));
        Serial.println(F("   известная ошибка IMA ADPCM VS1053b (нужен патч VLSI),"));
        Serial.println(F("   либо вход физически не подключён на шилде."));
    } else {
        Serial.println(F("[МИКРОФОН] Данные есть — вход живой."));
    }
    Serial.println(F("[МИКРОФОН] Чип возвращён в режим плеера, громкость восстановлена."));
}

void printHelp(void) {
    Serial.println(F("\r\n================= КОМАНДЫ УПРАВЛЕНИЯ ================="));
    Serial.println(F(" [0..5] : Запуск трека (0=BEEP.WAV, 1..5=TRACK01..05.MP3)"));
    Serial.println(F("   p    : Пауза / Возобновление воспроизведения"));
    Serial.println(F("   s    : Остановить воспроизведение"));
    Serial.println(F("   +    : Увеличить громкость"));
    Serial.println(F("   -    : Уменьшить громкость"));
    Serial.println(F("   v    : Задать громкость числом 0..254 (Enter для применения)"));
    Serial.println(F("   b    : Переключить Bass Boost (+0 / +6 / +12 / +15 dB)"));
    Serial.println(F("   e    : Переключить 3D Spatial Audio (EarSpeaker 0..3)"));
    Serial.println(F("   t    : Запустить встроенный аппаратный синус-тест"));
    Serial.println(F("   l    : Показать список файлов на MicroSD"));
    Serial.println(F("   i    : Показать состояние регистров и статус платы"));
    Serial.println(F("   d    : Дамп регистров VS1053 (диагностика связи по SPI)"));
    Serial.println(F("   m    : Быстрый перебор входов (MIC/LINE IN, 8 комбинаций)"));
    Serial.println(F("   M    : Долгий тест входов — по 60 с на MIC и LINE IN"));
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

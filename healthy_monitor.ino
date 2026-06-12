/*
 * ================================================================
 *        HEALTHY MONITOR — IoT HEALTH MONITORING SYSTEM
 * ================================================================
 *
 *  Deskripsi  : Sistem pemantauan kesehatan berbasis IoT yang
 *               mengukur detak jantung dan suhu tubuh secara
 *               real-time, dengan notifikasi otomatis melalui
 *               Telegram Bot.
 *
 *  Mikrokontroler : ESP32
 *
 *  Komponen   :
 *    - MAX30102   → Sensor detak jantung & SpO₂ (I2C: 0x57)
 *    - DS18B20    → Sensor suhu tubuh (OneWire, GPIO 4)
 *                   * Pakai versi MODUL — pull-up sudah built-in
 *    - OLED 0.96" → Display SSD1306 128x64 (I2C: 0x3C)
 *    - LED Merah  → Indikator kondisi abnormal (GPIO 2)
 *    - Buzzer     → Alarm suara kondisi abnormal (GPIO 15)
 *
 *  Notifikasi : Telegram Bot API
 *
 *  Library yang dibutuhkan (install via Library Manager):
 *    - UniversalTelegramBot  by Brian Lough
 *    - ArduinoJson           by Benoit Blanchon
 *    - MAX30105              by SparkFun Electronics
 *    - heartRate             by SparkFun Electronics
 *    - OneWire               by Paul Stoffregen
 *    - DallasTemperature     by Miles Burton
 *    - Adafruit_SSD1306      by Adafruit
 *    - Adafruit_GFX          by Adafruit
 *
 *  Wiring Ringkas:
 *    MAX30102  → SDA: GPIO21 | SCL: GPIO22 | VCC: 3.3V | GND
 *    DS18B20   → OUT: GPIO4  | VCC: 3.3V   | GND
 *               (pull-up sudah ada di modul, tidak perlu resistor)
 *    OLED      → SDA: GPIO21 | SCL: GPIO22 | VCC: 3.3V | GND
 *    LED Merah → Anoda: GPIO2 | Katoda: GND (via resistor 220Ω)
 *    Buzzer    → (+): GPIO15 | (-): GND
 *
 * ================================================================
 */


// ================================================================
//  BAGIAN 1 — INCLUDE LIBRARY
// ================================================================

#include <Wire.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>

#include <MAX30105.h>
#include <heartRate.h>

#include <OneWire.h>
#include <DallasTemperature.h>

#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>


// ================================================================
//  BAGIAN 2 — KONFIGURASI WiFi & TELEGRAM
// ================================================================

#define WIFI_SSID       "Papoy Id"
#define WIFI_PASSWORD   "qwerty123"
#define BOT_TOKEN       "8728590053:AAHFRIiLE5s0c55vH_BHHpl_-QLFfLelLwI"
#define CHAT_ID         "6427348734"


// ================================================================
//  BAGIAN 3 — DEFINISI PIN
// ================================================================

#define PIN_DS18B20     4
#define PIN_LED_MERAH   2
#define PIN_BUZZER      15


// ================================================================
//  BAGIAN 4 — KONFIGURASI OLED
// ================================================================

#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT   64
#define OLED_RESET      -1
#define OLED_ADDRESS    0x3C


// ================================================================
//  BAGIAN 5 — NILAI BATAS (THRESHOLD) KESEHATAN
// ================================================================

#define BPM_MIN         60
#define BPM_MAX         100
#define SUHU_MIN        36.0
#define SUHU_MAX        37.5


// ================================================================
//  BAGIAN 6 — INTERVAL WAKTU (MILLISECOND)
// ================================================================

#define INTERVAL_SENSOR     2000
#define INTERVAL_TELEGRAM   10000
#define INTERVAL_NOTIF      30000


// ================================================================
//  BAGIAN 7 — DEKLARASI OBJEK
// ================================================================

WiFiClientSecure      client;
UniversalTelegramBot  bot(BOT_TOKEN, client);

MAX30105              particleSensor;

OneWire               oneWire(PIN_DS18B20);
DallasTemperature     sensorSuhu(&oneWire);

Adafruit_SSD1306      display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);


// ================================================================
//  BAGIAN 8 — VARIABEL GLOBAL DATA SENSOR
// ================================================================

float   bpm             = 0;
float   suhu            = 0;
bool    fingerDetected  = false;
String  statusKesehatan = "Mulai...";


// ================================================================
//  BAGIAN 9 — VARIABEL ALGORITMA DETEKSI BPM
// ================================================================

const byte RATE_SIZE    = 4;
byte    rates[RATE_SIZE];
byte    rateSpot        = 0;
long    lastBeat        = 0;
float   beatsPerMinute  = 0;
int     beatAvg         = 0;


// ================================================================
//  BAGIAN 10 — VARIABEL TIMER
// ================================================================

unsigned long lastSensorTime    = 0;
unsigned long lastTelegramCheck = 0;
unsigned long lastNotifTime     = 0;


// ================================================================
//  BAGIAN 11 — FLAG NOTIFIKASI
// ================================================================

bool notifBPMTerkirim   = false;
bool notifSuhuTerkirim  = false;


// ================================================================
//  BAGIAN 12 — DEKLARASI FUNGSI
// ================================================================

void bacaMAX30102();
void bacaDS18B20();
void tentukanStatus();
void updateLEDdanBuzzer();
void updateOLED();
void tampilkanPesan(String baris1, String baris2);
String bangunPesanStatus();
void kirimNotifikasiOtomatis();
void cekPesanTelegram();


// ================================================================
//  BAGIAN 13 — SETUP
// ================================================================

void setup() {
  Serial.begin(115200);
  delay(3000); // Tunggu Serial Monitor siap

  Serial.println();
  Serial.println("================================");
  Serial.println("     HEALTHY MONITOR v1.0");
  Serial.println("  IoT Health Monitoring System");
  Serial.println("================================");
  Serial.println();
  Serial.println("[HEALTHY MONITOR] Memulai inisialisasi...");

  // ── GPIO ───────────────────────────────────────────────────
  pinMode(PIN_LED_MERAH, OUTPUT);
  pinMode(PIN_BUZZER,    OUTPUT);
  digitalWrite(PIN_LED_MERAH, LOW);
  digitalWrite(PIN_BUZZER,    LOW);

  // ── OLED ───────────────────────────────────────────────────
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("[ERROR] OLED tidak ditemukan di 0x3C!");
    while (true);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  tampilkanPesan("Healthy Monitor", "Memulai sistem...");
  Serial.println("[OK] OLED siap.");

  // ── WiFi ───────────────────────────────────────────────────
  Serial.print("[WiFi] Menghubungkan ke: ");
  Serial.println(WIFI_SSID);
  tampilkanPesan("Menghubungkan", "ke WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int wifiRetry = 0;
  while (WiFi.status() != WL_CONNECTED && wifiRetry < 20) {
    delay(500);
    Serial.print(".");
    wifiRetry++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] Terhubung! IP: " + WiFi.localIP().toString());
    tampilkanPesan("WiFi Terhubung!", WiFi.localIP().toString());
  } else {
    Serial.println("\n[WiFi] GAGAL — periksa SSID/password.");
    tampilkanPesan("WiFi GAGAL", "Cek SSID/Password");
  }

  client.setInsecure();

  // ── MAX30102 ───────────────────────────────────────────────
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("[ERROR] MAX30102 tidak ditemukan di I2C!");
    Serial.println("Periksa: SDA=GPIO21, SCL=GPIO22, VCC=3.3V");
    tampilkanPesan("ERROR:", "MAX30102 gagal");
    while (true);
  }
  particleSensor.setup();
  particleSensor.setPulseAmplitudeRed(0x0A);
  particleSensor.setPulseAmplitudeGreen(0);
  Serial.println("[OK] MAX30102 siap.");

  // ── DS18B20 ────────────────────────────────────────────────
  sensorSuhu.begin();
  Serial.println("[OK] DS18B20 siap.");

  // ── Selesai ────────────────────────────────────────────────
  delay(500);
  tampilkanPesan("Semua Siap!", "Tempel jari...");
  Serial.println("[HEALTHY MONITOR] Inisialisasi selesai.\n");

  // ── Kirim notifikasi awal ke Telegram ─────────────────────
  if (WiFi.status() == WL_CONNECTED) {
    String msg  = "🟢 *HEALTHY MONITOR v1.0 AKTIF*\n";
    msg        += "━━━━━━━━━━━━━━━━━━━━\n";
    msg        += "Perangkat berhasil dinyalakan.\n";
    msg        += "Notifikasi otomatis akan dikirim\n";
    msg        += "jika terdeteksi kondisi abnormal.\n\n";
    msg        += "Ketik /bantuan untuk daftar perintah.";
    bot.sendMessage(CHAT_ID, msg, "Markdown");
  }
}


// ================================================================
//  BAGIAN 14 — LOOP UTAMA
// ================================================================

void loop() {
  unsigned long sekarang = millis();

  // MAX30102 dibaca setiap saat
  bacaMAX30102();

  // Sensor lain & output setiap INTERVAL_SENSOR
  if (sekarang - lastSensorTime >= INTERVAL_SENSOR) {
    lastSensorTime = sekarang;

    bacaDS18B20();
    tentukanStatus();
    updateOLED();
    updateLEDdanBuzzer();

    Serial.printf(
      "[DATA] BPM: %.0f | Suhu: %.1f°C | Status: %s\n",
      bpm, suhu, statusKesehatan.c_str()
    );
  }

  // Cek pesan Telegram masuk
  if (sekarang - lastTelegramCheck >= INTERVAL_TELEGRAM) {
    lastTelegramCheck = sekarang;
    if (WiFi.status() == WL_CONNECTED) {
      cekPesanTelegram();
    }
  }

  // Kirim notifikasi otomatis
  if (sekarang - lastNotifTime >= INTERVAL_NOTIF) {
    lastNotifTime = sekarang;
    if (WiFi.status() == WL_CONNECTED) {
      kirimNotifikasiOtomatis();
    }
  }
}


// ================================================================
//  BAGIAN 15 — FUNGSI BACA SENSOR
// ================================================================

void bacaMAX30102() {
  long irValue   = particleSensor.getIR();
  fingerDetected = (irValue > 50000);

  if (fingerDetected) {
    if (checkForBeat(irValue)) {
      long delta     = millis() - lastBeat;
      lastBeat       = millis();
      beatsPerMinute = 60.0 / (delta / 1000.0);

      if (beatsPerMinute > 20 && beatsPerMinute < 255) {
        rates[rateSpot++] = (byte)beatsPerMinute;
        rateSpot %= RATE_SIZE;

        beatAvg = 0;
        for (byte x = 0; x < RATE_SIZE; x++) beatAvg += rates[x];
        beatAvg /= RATE_SIZE;
      }
    }
    bpm = beatAvg;

  } else {
    bpm              = 0;
    beatAvg          = 0;
    notifBPMTerkirim = false;
  }
}

void bacaDS18B20() {
  sensorSuhu.requestTemperatures();
  float temp = sensorSuhu.getTempCByIndex(0);

  if (temp != DEVICE_DISCONNECTED_C) {
    suhu = temp;
  } else {
    suhu = -1;
    Serial.println("[WARN] DS18B20 tidak terbaca — periksa koneksi.");
  }
}


// ================================================================
//  BAGIAN 16 — FUNGSI STATUS & OUTPUT
// ================================================================

void tentukanStatus() {
  if (!fingerDetected || bpm == 0) {
    statusKesehatan = "Tempel Jari";
    return;
  }

  if (bpm < BPM_MIN) {
    statusKesehatan = "Bradikardia";
  } else if (bpm > BPM_MAX) {
    statusKesehatan = "Takikardia";
  } else if (suhu > SUHU_MAX) {
    statusKesehatan = "Demam";
  } else if (suhu < SUHU_MIN && suhu > 0) {
    statusKesehatan = "Hipotermia";
  } else {
    statusKesehatan   = "Normal";
    notifBPMTerkirim  = false;
    notifSuhuTerkirim = false;
  }
}

void updateLEDdanBuzzer() {
  bool kondisiKritis = (
    statusKesehatan == "Bradikardia" ||
    statusKesehatan == "Takikardia"  ||
    statusKesehatan == "Demam"       ||
    statusKesehatan == "Hipotermia"
  );

  if (kondisiKritis) {
    digitalWrite(PIN_LED_MERAH, HIGH);
    for (int i = 0; i < 3; i++) {
      digitalWrite(PIN_BUZZER, HIGH);
      delay(150);
      digitalWrite(PIN_BUZZER, LOW);
      delay(100);
    }
  } else {
    digitalWrite(PIN_LED_MERAH, LOW);
    digitalWrite(PIN_BUZZER,    LOW);
  }
}


// ================================================================
//  BAGIAN 17 — FUNGSI TAMPILAN OLED
// ================================================================

void updateOLED() {
  display.clearDisplay();

  // Header
  display.setTextSize(1);
  display.setCursor(15, 0);
  display.println("HEALTHY MONITOR");
  display.drawLine(0, 9, 127, 9, SSD1306_WHITE);

  // BPM
  display.setCursor(0, 14);
  display.print("BPM  : ");
  if (fingerDetected && bpm > 0) {
    display.print((int)bpm);
    display.println(" bpm");
  } else {
    display.println("-- tempel jari");
  }

  // Suhu
  display.setCursor(0, 28);
  display.print("Suhu : ");
  if (suhu > 0) {
    display.print(suhu, 1);
    display.println(" C");
  } else {
    display.println("Sensor Error");
  }

  // Status
  display.drawLine(0, 46, 127, 46, SSD1306_WHITE);
  display.setCursor(0, 50);
  display.print("Status: ");
  display.println(statusKesehatan);

  display.display();
}

void tampilkanPesan(String baris1, String baris2) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 20);
  display.println(baris1);
  display.setCursor(0, 35);
  display.println(baris2);
  display.display();
  delay(1500);
}


// ================================================================
//  BAGIAN 18 — FUNGSI TELEGRAM
// ================================================================

String bangunPesanStatus() {
  String msg = "📊 *STATUS KESEHATAN TERKINI*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";

  msg += "❤️ *Detak Jantung* : ";
  if (fingerDetected && bpm > 0) {
    msg += String((int)bpm) + " bpm";
    if      (bpm < BPM_MIN) msg += " ⚠️ Bradikardia";
    else if (bpm > BPM_MAX) msg += " ⚠️ Takikardia";
    else                    msg += " ✅ Normal";
  } else {
    msg += "Jari tidak terdeteksi";
  }
  msg += "\n";

  msg += "🌡️ *Suhu Tubuh*    : ";
  if (suhu > 0) {
    msg += String(suhu, 1) + " °C";
    if      (suhu > SUHU_MAX)             msg += " 🔴 Demam";
    else if (suhu < SUHU_MIN && suhu > 0) msg += " 🔵 Hipotermia";
    else                                  msg += " ✅ Normal";
  } else {
    msg += "Sensor Error";
  }
  msg += "\n";

  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "📋 *Status*  : " + statusKesehatan + "\n";
  msg += "🕐 *Uptime*  : " + String(millis() / 1000) + " detik";

  return msg;
}

void kirimNotifikasiOtomatis() {

  // Bradikardia
  if (fingerDetected && bpm > 0 && bpm < BPM_MIN && !notifBPMTerkirim) {
    String msg  = "🚨 *PERINGATAN DETAK JANTUNG*\n";
    msg += "━━━━━━━━━━━━━━━━━━━━\n";
    msg += "Detak jantung terlalu *rendah*!\n";
    msg += "BPM Terdeteksi : *" + String((int)bpm) + " bpm*\n";
    msg += "Kondisi        : Bradikardia\n";
    msg += "Batas normal   : " + String(BPM_MIN) + "–" + String(BPM_MAX) + " bpm\n\n";
    msg += "Segera periksa kondisi Anda!";
    bot.sendMessage(CHAT_ID, msg, "Markdown");
    notifBPMTerkirim = true;
    Serial.println("[TELEGRAM] Notif bradikardia terkirim.");
  }

  // Takikardia
  if (fingerDetected && bpm > BPM_MAX && !notifBPMTerkirim) {
    String msg  = "🚨 *PERINGATAN DETAK JANTUNG*\n";
    msg += "━━━━━━━━━━━━━━━━━━━━\n";
    msg += "Detak jantung terlalu *tinggi*!\n";
    msg += "BPM Terdeteksi : *" + String((int)bpm) + " bpm*\n";
    msg += "Kondisi        : Takikardia\n";
    msg += "Batas normal   : " + String(BPM_MIN) + "–" + String(BPM_MAX) + " bpm\n\n";
    msg += "Cobalah beristirahat sejenak.";
    bot.sendMessage(CHAT_ID, msg, "Markdown");
    notifBPMTerkirim = true;
    Serial.println("[TELEGRAM] Notif takikardia terkirim.");
  }

  // Demam
  if (suhu > SUHU_MAX && !notifSuhuTerkirim) {
    String msg  = "🌡️ *PERINGATAN SUHU TUBUH*\n";
    msg += "━━━━━━━━━━━━━━━━━━━━\n";
    msg += "Suhu tubuh terdeteksi *tinggi*!\n";
    msg += "Suhu Terdeteksi : *" + String(suhu, 1) + " °C*\n";
    msg += "Kondisi         : Demam\n";
    msg += "Batas normal    : " + String(SUHU_MIN, 1) + "–" + String(SUHU_MAX, 1) + " °C\n\n";
    msg += "Istirahatlah dan minum air yang cukup.";
    bot.sendMessage(CHAT_ID, msg, "Markdown");
    notifSuhuTerkirim = true;
    Serial.println("[TELEGRAM] Notif demam terkirim.");
  }

  // Hipotermia
  if (suhu > 0 && suhu < SUHU_MIN && !notifSuhuTerkirim) {
    String msg  = "🌡️ *PERINGATAN SUHU TUBUH*\n";
    msg += "━━━━━━━━━━━━━━━━━━━━\n";
    msg += "Suhu tubuh terdeteksi *rendah*!\n";
    msg += "Suhu Terdeteksi : *" + String(suhu, 1) + " °C*\n";
    msg += "Kondisi         : Hipotermia\n";
    msg += "Batas normal    : " + String(SUHU_MIN, 1) + "–" + String(SUHU_MAX, 1) + " °C\n\n";
    msg += "Segera hangatkan tubuh Anda!";
    bot.sendMessage(CHAT_ID, msg, "Markdown");
    notifSuhuTerkirim = true;
    Serial.println("[TELEGRAM] Notif hipotermia terkirim.");
  }
}

void cekPesanTelegram() {
  int jumlahPesan = bot.getUpdates(bot.last_message_received + 1);

  while (jumlahPesan > 0) {
    for (int i = 0; i < jumlahPesan; i++) {
      String chat_id = bot.messages[i].chat_id;
      String teks    = bot.messages[i].text;
      teks.toLowerCase();

      Serial.println("[TELEGRAM] Pesan masuk: " + teks + " dari " + chat_id);

      if (teks == "/status") {
        bot.sendMessage(chat_id, bangunPesanStatus(), "Markdown");
      }
      else if (teks == "/bpm") {
        String msg = "❤️ *Detak Jantung*\n";
        msg += "━━━━━━━━━━━━━━━━━━━━\n";
        if (fingerDetected && bpm > 0) {
          msg += "BPM    : *" + String((int)bpm) + " bpm*\n";
          if      (bpm < BPM_MIN) msg += "Status : ⚠️ Bradikardia (< " + String(BPM_MIN) + " bpm)";
          else if (bpm > BPM_MAX) msg += "Status : ⚠️ Takikardia  (> " + String(BPM_MAX) + " bpm)";
          else                    msg += "Status : ✅ Normal";
        } else {
          msg += "Jari tidak terdeteksi.\n";
          msg += "Tempelkan jari pada sensor MAX30102.";
        }
        bot.sendMessage(chat_id, msg, "Markdown");
      }
      else if (teks == "/suhu") {
        String msg = "🌡️ *Suhu Tubuh*\n";
        msg += "━━━━━━━━━━━━━━━━━━━━\n";
        if (suhu > 0) {
          msg += "Suhu   : *" + String(suhu, 1) + " °C*\n";
          if      (suhu > SUHU_MAX)             msg += "Status : 🔴 Demam       (> " + String(SUHU_MAX, 1) + " °C)";
          else if (suhu < SUHU_MIN && suhu > 0) msg += "Status : 🔵 Hipotermia  (< " + String(SUHU_MIN, 1) + " °C)";
          else                                  msg += "Status : ✅ Normal";
        } else {
          msg += "Sensor DS18B20 tidak terbaca.\n";
          msg += "Periksa koneksi kabel sensor.";
        }
        bot.sendMessage(chat_id, msg, "Markdown");
      }
      else if (teks == "/info") {
        String msg = "ℹ️ *Info Perangkat*\n";
        msg += "━━━━━━━━━━━━━━━━━━━━\n";
        msg += "Nama    : Healthy Monitor v1.0\n";
        msg += "MCU     : ESP32\n";
        msg += "Sensor  : MAX30102 | DS18B20\n";
        msg += "Output  : OLED | LED | Buzzer | Telegram\n";
        msg += "IP      : " + WiFi.localIP().toString() + "\n";
        msg += "WiFi    : " + String(WIFI_SSID) + "\n";
        msg += "Uptime  : " + String(millis() / 1000) + " detik";
        bot.sendMessage(chat_id, msg, "Markdown");
      }
      else if (teks == "/bantuan" || teks == "/start") {
        String msg = "🤖 *Healthy Monitor Bot*\n";
        msg += "━━━━━━━━━━━━━━━━━━━━\n";
        msg += "Daftar perintah:\n\n";
        msg += "📊 /status   — Semua data kesehatan\n";
        msg += "❤️ /bpm      — Detak jantung\n";
        msg += "🌡️ /suhu     — Suhu tubuh\n";
        msg += "ℹ️ /info     — Info perangkat\n";
        msg += "🆘 /bantuan  — Menu ini\n";
        msg += "━━━━━━━━━━━━━━━━━━━━\n";
        msg += "Notifikasi otomatis aktif untuk\n";
        msg += "kondisi Bradikardia, Takikardia,\n";
        msg += "Demam, dan Hipotermia.";
        bot.sendMessage(chat_id, msg, "Markdown");
      }
      else {
        String msg = "❓ Perintah tidak dikenali.\n";
        msg += "Ketik /bantuan untuk melihat\n";
        msg += "daftar perintah yang tersedia.";
        bot.sendMessage(chat_id, msg, "");
      }
    }
    jumlahPesan = bot.getUpdates(bot.last_message_received + 1);
  }
}

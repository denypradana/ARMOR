/*
ARMOR(Automatically Restart Offline Modem or Router with Arduino)
Kode program Arduino untuk restart modem secara otomatis ketika internet offline.

Dibuat oleh Deny Pradana (dp@denypradana.com)
IG: @denypradana

Alat yang digunakan:
- Arduino UNO R3
- LCD 16x2 I2C
- Arduino Ethernet Shield
- Modul RTC DS3231
- Relay Board (Penulis menggunakan relay board aktif HIGH, sesuaikan logic
  digitalWrite(RELAY_PIN_X, LOW/HIGH) sesuai dengan relay board yang digunakan
  apakah aktif LOW atau HIGH).

Catatan: Sertakan kredit kepada pembuat program jika ingin menggunakan sebagian
atau keseluruhan kode program yang ada di sini. Terima kasih!
*/

// --- Library yang Digunakan ---
#include <SPI.h>          // Untuk komunikasi SPI, penting untuk Ethernet Shield
#include <Ethernet.h>     // Library Arduino Ethernet untuk konektivitas jaringan
#include <Wire.h>         // Untuk komunikasi I2C, digunakan oleh LCD dan RTC
#include <RTClib.h>       // Library untuk modul Real Time Clock (RTC) DS3231
#include <LiquidCrystal_I2C.h> // Library untuk LCD 16x2 dengan modul I2C
#include <NTPClient.h>    // Library untuk sinkronisasi waktu dengan Network Time Protocol (NTP)
#include <EthernetUdp.h>  // Untuk komunikasi UDP, dibutuhkan oleh NTPClient
#include <EEPROM.h>       // Library untuk menyimpan data secara persisten di EEPROM Arduino

// --- Konfigurasi Hardware ---
// MAC address Arduino Ethernet Shield Anda. Pastikan unik di jaringan.
// Jangan sentuh bagian ini sesuai instruksi.
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

// Server dan port yang akan di-ping untuk mengecek koneksi internet.
// Google DNS (8.8.8.8) port 53 adalah pilihan umum.
const IPAddress SERVER_TO_TEST(8, 8, 8, 8);
const int PORT_TO_TEST = 53;

// Karena menggunakan Ethernet shield W5100, pastikan menghindari penggunaan pin berikut :
// Pin 4 (SD Card), Pin 9 (w5100_RST), Pin 10 (W5100_CS), Pin 11 (SPI), Pin 12 (SPI), Pin 13 (SPI)

// Pin Arduino yang terhubung ke relay board.
// Sesuaikan dengan koneksi fisik Anda.
const int RELAY_PIN_1 = 2;
const int RELAY_PIN_2 = 3;

// Pin Arduino yang terhubung ke tombol SELECT.
// Menggunakan INPUT_PULLUP berarti pin akan HIGH secara default dan LOW saat ditekan.
const int SELECT_BUTTON_PIN = 5;

// NTP Server dan Offset Waktu
// NTP_SERVER disimpan di PROGMEM untuk menghemat RAM.
const char NTP_SERVER[] PROGMEM = "pool.ntp.org";
// Offset waktu UTC+7 untuk WIB (Jakarta). Sesuaikan jika Anda di zona waktu lain.
const long UTC_OFFSET_IN_SECONDS = 7L * 3600L;

// --- Inisialisasi Objek Global ---
EthernetClient ethClient; // Objek client Ethernet untuk pengecekan koneksi
EthernetUDP ntpUdp;       // Objek UDP untuk komunikasi NTP

// Buffer sementara untuk nama server NTP.
// NTPClient mengharapkan char* dari RAM, sehingga kita perlu menyalin nama server dari PROGMEM ke sini.
char ntpServerBuffer[30];

// Objek NTPClient, diinisialisasi dengan objek UDP dan offset waktu.
// Nama server NTP akan diset kemudian di setup() dari ntpServerBuffer.
NTPClient timeClient(ntpUdp, UTC_OFFSET_IN_SECONDS);
RTC_DS3231 rtc;           // Objek RTC untuk mengakses modul DS3231
// Objek LCD I2C dengan alamat 0x27, 16 kolom, 2 baris. Sesuaikan alamat jika berbeda.
LiquidCrystal_I2C lcd(0x27, 16, 2);

// --- Variabel State ---
uint8_t offlineAttemptCount = 0; // Menghitung berapa kali modem sudah direstart
unsigned long lastOfflineCheckTime = 0; // Waktu terakhir internet terdeteksi offline
unsigned long lastISPOfflineMessageTime = 0; // Waktu terakhir pesan "ISP Offline" ditampilkan
bool isInISPOfflineMode = false; // Flag jika sistem berada dalam mode "ISP Offline" (setelah 3x restart gagal)
bool ntpSyncDoneToday = false; // Flag untuk memastikan sinkronisasi NTP hanya sekali sehari
unsigned long lastDisplayUpdateTime = 0; // Waktu terakhir LCD diperbarui
const unsigned long LCD_UPDATE_INTERVAL = 1000; // Interval pembaruan LCD (1 detik)
bool isInternetOnline = false; // Status koneksi internet saat ini (true = online, false = offline)

// --- Struktur dan Variabel untuk EEPROM & Tampilan ---
// Struktur data untuk menyimpan log waktu offline terakhir di EEPROM
struct OfflineLog {
  uint8_t day;        // Hari saat offline dimulai
  uint8_t month;      // Bulan saat offline dimulai
  uint8_t hourStart;  // Jam saat offline dimulai
  uint8_t minuteStart;// Menit saat offline dimulai
  uint8_t hourEnd;    // Jam saat online kembali (offline berakhir)
  uint8_t minuteEnd;  // Menit saat online kembali (offline berakhir)
  bool isValid;       // Flag untuk menandakan apakah data log valid atau kosong
};

OfflineLog lastOfflineLog; // Variabel untuk menyimpan log offline terakhir
const int EEPROM_ADDRESS = 0; // Alamat awal di EEPROM untuk menyimpan struktur OfflineLog

bool displayLastOffline = false; // Flag untuk beralih antara tampilan status normal dan log offline
unsigned long offlineStartTime = 0; // Waktu (millis()) saat internet mulai offline

// --- Deklarasi Prototipe Fungsi ---
// Deklarasi fungsi-fungsi kustom yang digunakan dalam program.
// Ini membantu compiler dan meningkatkan keterbacaan kode.
void displayCurrentTimeAndStatus();
void displayLastOfflineTime();
bool checkInternetConnection(bool verbose);
void handleButtons();

// --- Fungsi Setup() ---
// Dijalankan sekali saat Arduino dinyalakan atau direset
void setup() {
  delay(1000); // Jeda awal untuk stabilitas hardware
  Serial.begin(9600); // Inisialisasi komunikasi serial untuk debugging
  Serial.println(F("Setup dimulai...")); // Pesan log di Serial Monitor

  // Konfigurasi pin relay sebagai OUTPUT dan pastikan nonaktif di awal
  pinMode(RELAY_PIN_1, OUTPUT);
  pinMode(RELAY_PIN_2, OUTPUT);
  digitalWrite(RELAY_PIN_1, LOW); // Relay aktif HIGH, jadi LOW = nonaktif
  digitalWrite(RELAY_PIN_2, LOW); // Relay aktif HIGH, jadi LOW = nonaktif

  // Konfigurasi pin tombol sebagai INPUT dengan internal pull-up resistor
  pinMode(SELECT_BUTTON_PIN, INPUT_PULLUP);

  // Inisialisasi LCD I2C
  lcd.init();      // Inisialisasi LCD
  lcd.backlight(); // Nyalakan backlight LCD
  lcd.setCursor(0, 0);
  lcd.print(F("   **ARMOR**   ")); // Tampilkan pesan awal di LCD
  lcd.setCursor(0, 1);
  lcd.print(F("By Deny Pradana")); // Tampilkan pesan awal di LCD

  // Inisialisasi RTC DS3231
  if (!rtc.begin()) { // Cek apakah RTC terdeteksi dan merespons
    Serial.println(F("RTC tidak ditemukan atau tidak merespons!"));
    lcd.clear();
    lcd.print(F("Error RTC!"));
    while (1) delay(100); // Berhenti jika RTC tidak ditemukan
  }

  // Baca log offline terakhir dari EEPROM
  EEPROM.get(EEPROM_ADDRESS, lastOfflineLog);
  // Jika data di EEPROM tidak valid (misal, pertama kali di-flash atau rusak)
  if (!lastOfflineLog.isValid) {
    // Inisialisasi struct dengan nilai nol atau default dan tandai tidak valid
    memset(&lastOfflineLog, 0, sizeof(lastOfflineLog)); // Mengisi semua byte dengan 0
    lastOfflineLog.isValid = false; // Pastikan flag validitas diset false
    EEPROM.put(EEPROM_ADDRESS, lastOfflineLog); // Simpan kembali ke EEPROM
  }

  delay(1000); // Jeda singkat

  // --- Jeda 3 Menit di Awal ---
  // Memberi waktu modem dan router untuk boot up dan mendapatkan IP address
  lcd.clear();
  lcd.print(F("Tunggu 3 menit..."));
  Serial.println(F("Menunggu 3 menit sebelum cek jaringan..."));

  unsigned long initialDelayStartTime = millis();
  const unsigned long initialDelayTime = 3UL * 60UL * 1000UL; // 3 menit dalam milidetik

  // Loop untuk menampilkan hitung mundur di LCD selama jeda awal
  while (millis() - initialDelayStartTime < initialDelayTime) {
    if (millis() - lastDisplayUpdateTime >= LCD_UPDATE_INTERVAL) {
      unsigned long remainingTimeSec = (initialDelayTime - (millis() - initialDelayStartTime)) / 1000UL;
      int minutes = remainingTimeSec / 60;
      int seconds = remainingTimeSec % 60;

      lcd.setCursor(0, 1);
      char countdownBuffer[17];
      // Menggunakan snprintf_P untuk memformat string dari PROGMEM
      snprintf_P(countdownBuffer, sizeof(countdownBuffer), PSTR("Sisa: %02d:%02d        "), minutes, seconds);
      lcd.print(countdownBuffer);
      lastDisplayUpdateTime = millis();
    }
    delay(10); // Jeda singkat agar tidak terlalu membebani CPU
  }

  lcd.clear();
  Serial.println(F("Jeda 3 menit selesai. Memulai pengecekan jaringan."));
  lcd.setCursor(0, 0);
  lcd.print(F("Inisialisasi"));
  lcd.setCursor(0, 1);
  lcd.print(F("Ethernet...."));

  // --- Inisialisasi Ethernet ---
  Serial.print(F("Menginisialisasi Ethernet..."));
  // Mencoba mendapatkan IP address via DHCP
  if (Ethernet.begin(mac) == 0) {
    // Penanganan error jika DHCP gagal atau hardware Ethernet tidak ditemukan
    Serial.println(F("Gagal mengkonfigurasi Ethernet (DHCP)."));
    lcd.clear();
    lcd.print(F("ETH DHCP Gagal!"));
    Serial.print(F("Ethernet Hardware Status: "));
    if (Ethernet.hardwareStatus() == EthernetNoHardware) {
      Serial.println(F("Tidak ada hardware Ethernet."));
      lcd.setCursor(0, 1);
      lcd.print(F("No ETH H/W!"));
    } else {
      Serial.print(F("Hardware ditemukan. Link Status: "));
      if (Ethernet.linkStatus() == LinkOFF) {
        Serial.println(F("Kabel Ethernet tidak terhubung/Link OFF."));
        lcd.setCursor(0, 1);
        lcd.print(F("Kabel ETH Putus!"));
      } else if (Ethernet.linkStatus() == LinkON) {
        Serial.println(F("Link ON, tapi DHCP gagal."));
        lcd.setCursor(0, 1);
        lcd.print(F("Link OK, DHCP KO"));
      } else {
        Serial.println(F("Status link tidak diketahui."));
        lcd.setCursor(0, 1);
        lcd.print(F("Link ??, DHCP KO"));
      }
    }
    while (true) delay(100); // Berhenti jika Ethernet gagal diinisialisasi
  }
  Serial.print(F("Ethernet terhubung. IP: "));
  Serial.println(Ethernet.localIP()); // Tampilkan IP yang didapat
  delay(1000); // Beri waktu setelah Ethernet terhubung

  // --- Inisialisasi dan Sinkronisasi NTP Client ---
  // Salin nama server dari PROGMEM ke buffer di RAM
  strcpy_P(ntpServerBuffer, NTP_SERVER);
  // Mengatur nama server NTP dari buffer di RAM
  timeClient.setPoolServerName(ntpServerBuffer);
  timeClient.begin(); // Memulai NTP Client
  Serial.println(F("NTP Client dimulai dan dikonfigurasi."));

  Serial.println(F("Mencoba sinkronisasi NTP awal..."));
  lcd.clear();
  lcd.print(F("Sync NTP Awal..."));

  timeClient.update(); // Minta update waktu dari NTP server
  unsigned long ntpEpochTime = timeClient.getEpochTime();

  if (ntpEpochTime > 0) { // Jika berhasil mendapatkan waktu
    DateTime ntpDateTime(ntpEpochTime); // Buat objek DateTime dari waktu Epoch NTP
    rtc.adjust(ntpDateTime); // Atur waktu RTC
    Serial.print(F("RTC diupdate dari NTP: "));
    Serial.println(ntpDateTime.timestamp(DateTime::TIMESTAMP_FULL)); // Tampilkan waktu baru
    lcd.clear();
    lcd.print(F("NTP Sync Sukses!"));
    delay(2000);
  } else {
    Serial.println(F("Gagal mendapatkan waktu dari NTP pada sinkronisasi awal."));
    lcd.clear();
    lcd.print(F("NTP Sync Gagal!"));
    delay(2000);
  }
  // Perbarui status internet awal setelah sinkronisasi NTP
  isInternetOnline = checkInternetConnection(true);
}

// --- Fungsi Loop() ---
// Dijalankan berulang kali setelah Setup() selesai
void loop() {
  // Tangani penekanan tombol untuk beralih tampilan LCD
  handleButtons();

  // Perbarui tampilan LCD secara periodik
  if (millis() - lastDisplayUpdateTime >= LCD_UPDATE_INTERVAL) {
    if (displayLastOffline) {
      displayLastOfflineTime(); // Tampilkan log offline terakhir
    } else {
      displayCurrentTimeAndStatus(); // Tampilkan waktu dan status internet saat ini
    }
    lastDisplayUpdateTime = millis(); // Reset timer update LCD
  }

  // Hanya jalankan logika pengecekan koneksi jika tidak sedang menampilkan log offline
  if (!displayLastOffline) {
    // --- Sinkronisasi Waktu NTP (hanya jika internet online) ---
    // Akan sinkronisasi setiap hari di sekitar pergantian hari (23:55 - 00:00)
    if (!isInISPOfflineMode && isInternetOnline) {
      DateTime now = rtc.now(); // Dapatkan waktu dari RTC
      int currentHour = now.hour();
      int currentMinute = now.minute();

      // Cek jendela waktu untuk sinkronisasi NTP harian
      if ((currentHour == 23 && currentMinute >= 55) || (currentHour == 0 && currentMinute == 0)) {
        if (!ntpSyncDoneToday) { // Hanya sync jika belum dilakukan hari ini
          Serial.println(F("Memulai sinkronisasi waktu NTP..."));
          lcd.clear();
          lcd.print(F("Sync NTP..."));

          timeClient.update(); // Minta update waktu dari NTP server
          unsigned long ntpEpochTime = timeClient.getEpochTime();

          if (ntpEpochTime > 0) { // Jika berhasil mendapatkan waktu
            DateTime ntpDateTime(ntpEpochTime);
            rtc.adjust(ntpDateTime); // Atur waktu RTC
            Serial.print(F("RTC diupdate dari NTP: "));
            Serial.println(ntpDateTime.timestamp(DateTime::TIMESTAMP_FULL));
            ntpSyncDoneToday = true; // Set flag sudah sync
            lcd.clear();
            lcd.print(F("NTP Sync Sukses!"));
            delay(2000);
          } else {
            Serial.println(F("Gagal mendapatkan waktu dari NTP."));
            lcd.clear();
            lcd.print(F("NTP Sync Gagal!"));
            delay(2000);
          }
        }
      } else if (currentHour == 0 && currentMinute == 1) {
        // Reset flag ntpSyncDoneToday setelah melewati tengah malam (jam 00:01)
        ntpSyncDoneToday = false;
      }
    }

    // --- Logika Penanganan Mode "ISP Offline" ---
    // Jika sistem dalam mode ISP Offline, hanya akan mencoba cek koneksi setiap 30 menit
    if (isInISPOfflineMode) {
      // Cek apakah sudah 30 menit sejak cek terakhir dalam mode ISP Offline
      if (millis() - lastISPOfflineMessageTime >= 30UL * 60UL * 1000UL) {
        Serial.println(F("Mencoba cek internet lagi setelah 30 menit..."));
        isInternetOnline = checkInternetConnection(true); // Coba cek koneksi lagi
        if (isInternetOnline) {
          // Jika internet online kembali, keluar dari mode ISP Offline
          Serial.println(F("Internet ONLINE lagi setelah mode ISP Offline."));
          isInISPOfflineMode = false;
          offlineAttemptCount = 0; // Reset hitungan percobaan
          lastOfflineCheckTime = 0;
          lcd.clear();
        } else {
          // Jika masih offline, tetap di mode ISP Offline dan reset timer
          Serial.println(F("Masih OFFLINE setelah 30 menit. Tetap di mode ISP Offline."));
          lastISPOfflineMessageTime = millis();
        }
      }
      return; // Keluar dari loop() untuk saat ini, akan diulang lagi
    }

    // --- Logika Pengecekan Koneksi Normal dan Penanganan Restart Modem ---
    bool currentInternetStatus = checkInternetConnection(true); // Cek status internet saat ini

    // Deteksi perubahan status: Offline -> Online
    if (!isInternetOnline && currentInternetStatus) {
      if (offlineStartTime != 0) { // Jika ada waktu offline yang tercatat
        DateTime now = rtc.now();
        lastOfflineLog.hourEnd = now.hour();
        lastOfflineLog.minuteEnd = now.minute();
        lastOfflineLog.isValid = true; // Tandai log valid
        EEPROM.put(EEPROM_ADDRESS, lastOfflineLog); // Simpan durasi offline ke EEPROM
        Serial.println(F("Durasi offline dicatat ke EEPROM."));
      }
      offlineStartTime = 0; // Reset waktu mulai offline
    }

    // Deteksi perubahan status: Online -> Offline
    if (isInternetOnline && !currentInternetStatus) {
      // Internet baru saja offline, catat waktu mulai offline
      DateTime now = rtc.now();
      lastOfflineLog.day = now.day();
      lastOfflineLog.month = now.month();
      lastOfflineLog.hourStart = now.hour();
      lastOfflineLog.minuteStart = now.minute();
      offlineStartTime = millis(); // Mulai hitung durasi offline
      lastOfflineLog.isValid = false; // Set invalid sampai online kembali
      EEPROM.put(EEPROM_ADDRESS, lastOfflineLog); // Simpan sementara waktu mulai offline
    }

    isInternetOnline = currentInternetStatus; // Perbarui status internet global

    if (isInternetOnline) {
      // Jika internet online, reset semua hitungan offline
      offlineAttemptCount = 0;
      lastOfflineCheckTime = 0;
      isInISPOfflineMode = false;
    } else {
      // Jika internet offline, mulai proses pemulihan
      Serial.println(F("Internet OFFLINE. Memulai proses pemulihan..."));

      // Jika ini deteksi offline pertama kali (setelah online), mulai hitung mundur 1 menit
      if (lastOfflineCheckTime == 0) {
        lastOfflineCheckTime = millis();
        Serial.println(F("Tunggu 1 menit sebelum restart modem..."));
      }

      // Setelah 1 menit atau jika sudah melewati 1 menit, cek percobaan restart
      if (millis() - lastOfflineCheckTime >= 60UL * 1000UL) {
        if (offlineAttemptCount < 3) { // Batasi percobaan restart modem hingga 3 kali
          offlineAttemptCount++; // Tambah hitungan percobaan
          Serial.print(F("Percobaan restart modem ke-"));
          Serial.println(offlineAttemptCount);
          lcd.clear();
          lcd.print(F("OFF: Percobaan "));
          lcd.print(offlineAttemptCount);
          lcd.setCursor(0, 1);
          lcd.print(F("Restart Modem..."));
          delay(3000); // Jeda sebelum aktivasi relay

          // Aktifkan relay untuk memutus/menyambung daya modem
          digitalWrite(RELAY_PIN_1, HIGH); // Nyalakan relay (sesuai aktif HIGH)
          digitalWrite(RELAY_PIN_2, HIGH); // Nyalakan relay
          Serial.println(F("Relay ON"));
          delay(5000); // Tunggu

          digitalWrite(RELAY_PIN_1, LOW); // Matikan relay (modem hidup kembali)
          digitalWrite(RELAY_PIN_2, LOW); // Matikan relay
          Serial.println(F("Relay OFF"));
          delay(3000); // Jeda setelah mati

          lcd.clear();
          lcd.print(F("Menunggu 3 Menit"));
          lcd.setCursor(0, 1);
          lcd.print(F("Cek Ulang..."));
          Serial.println(F("Menunggu 3 menit untuk cek status internet..."));

          // Tampilkan hitung mundur 3 menit di LCD setelah restart
          unsigned long restartWaitStartTime = millis();
          const unsigned long threeMinuteDelay = 3UL * 60UL * 1000UL;
          while (millis() - restartWaitStartTime < threeMinuteDelay) {
            if (millis() - lastDisplayUpdateTime >= LCD_UPDATE_INTERVAL) {
              unsigned long remainingTimeSec = (threeMinuteDelay - (millis() - restartWaitStartTime)) / 1000UL;
              int minutes = remainingTimeSec / 60;
              int seconds = remainingTimeSec % 60;
              lcd.setCursor(0, 1);
              char countdownBuffer[17];
              snprintf_P(countdownBuffer, sizeof(countdownBuffer), PSTR("Sisa: %02d:%02d        "), minutes, seconds);
              lcd.print(countdownBuffer);
              lastDisplayUpdateTime = millis();
            }
            delay(10); // Jeda singkat
          }
          lastOfflineCheckTime = 0; // Reset waktu untuk memulai jeda 1 menit lagi jika masih offline
          Serial.println(F("Jeda 3 menit selesai, akan cek internet lagi."));
        } else {
          // Setelah 3 kali percobaan restart dan masih offline, masuk mode ISP Offline
          Serial.println(F("3 kali percobaan restart modem gagal. Masuk mode ISP Offline."));
          isInISPOfflineMode = true;
          offlineAttemptCount = 0; // Reset hitungan percobaan
          lastISPOfflineMessageTime = millis(); // Set waktu masuk mode ISP Offline
        }
      }
    }
  }
}

// --- Fungsi displayCurrentTimeAndStatus() ---
// Menampilkan waktu saat ini dan status koneksi internet di LCD.
void displayCurrentTimeAndStatus() {
  DateTime now = rtc.now(); // Dapatkan waktu dari RTC
  char dateTimeBuffer[17]; // Buffer untuk string tanggal dan waktu
  // Format string tanggal dan waktu: DD/MM HH:MM
  snprintf_P(dateTimeBuffer, sizeof(dateTimeBuffer), PSTR("%02d/%02d %02d:%02d"),
             now.day(), now.month(), now.hour(), now.minute());

  lcd.clear(); // Bersihkan LCD setiap kali update
  lcd.setCursor(0, 0); // Pindah kursor ke baris pertama

  if (isInISPOfflineMode) {
    lcd.print(F("ISP Offline        "));
    lcd.setCursor(0, 1);
    // Pesan khusus jika dalam mode "ISP Offline"
    lcd.print(F("Kontak ISP (188)")); // (Contact Center Telkomsel/Indihome)
  } else {
    if (isInternetOnline) {
      lcd.print(F("Status: ONLINE   "));
      lcd.setCursor(0, 1);
      lcd.print(dateTimeBuffer); // Tampilkan tanggal dan waktu
    } else { // Jika internet OFFLINE dan BUKAN dalam mode ISP Offline
      lcd.print(F("Status: OFFLINE "));
      lcd.setCursor(0, 1); // Pindahkan kursor ke baris kedua

      unsigned long remainingTimeSec = 0;
      if (lastOfflineCheckTime != 0) { // Pastikan lastOfflineCheckTime sudah diinisialisasi
        long elapsed = millis() - lastOfflineCheckTime;
        if (elapsed < 60UL * 1000UL) { // Hitung waktu yang tersisa dari 1 menit
          remainingTimeSec = (60UL * 1000UL - elapsed) / 1000UL;
        } else {
          remainingTimeSec = 0; // Waktu 1 menit sudah habis
        }
      }

      char countdownBuffer[17];
      if (remainingTimeSec > 0) {
        int minutes = remainingTimeSec / 60;
        int seconds = remainingTimeSec % 60;
        // Format string "Tunggu MM:SS"
        snprintf_P(countdownBuffer, sizeof(countdownBuffer), PSTR("Tunggu %02d:%02d        "), minutes, seconds);
      } else {
        // Jika countdown sudah habis atau belum dimulai, tampilkan pesan "Restarting..."
        strcpy_P(countdownBuffer, PSTR("Restarting...   "));
      }
      lcd.print(countdownBuffer); // Tampilkan countdown atau pesan restart
    }
  }
}

// --- Fungsi displayLastOfflineTime() ---
// Menampilkan waktu terakhir internet offline dari data yang tersimpan di EEPROM.
void displayLastOfflineTime() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Last Offline:")); // Label untuk tampilan log offline
  lcd.setCursor(0, 1);

  if (lastOfflineLog.isValid) { // Cek apakah ada data log yang valid
    char offlineTimeBuffer[17]; // Buffer untuk string log waktu offline
    // Format: DD/MM hh:mm-hh:mm (start hh:mm end hh:mm)
    snprintf_P(offlineTimeBuffer, sizeof(offlineTimeBuffer), PSTR("%02d%02d %02d:%02d-%02d:%02d"),
               lastOfflineLog.day, lastOfflineLog.month,
               lastOfflineLog.hourStart, lastOfflineLog.minuteStart,
               lastOfflineLog.hourEnd, lastOfflineLog.minuteEnd);
    lcd.print(offlineTimeBuffer);
  } else {
    lcd.print(F("Belum ada data")); // Pesan jika belum ada log offline yang valid
  }
}

// --- Fungsi checkInternetConnection(bool verbose) ---
// Memeriksa koneksi internet dengan mencoba terhubung ke server yang ditentukan.
// `verbose` (true/false) mengontrol apakah pesan detail ditampilkan di Serial Monitor.
bool checkInternetConnection(bool verbose) {
  if (verbose) {
    Serial.print(F("Mencoba koneksi ke "));
    Serial.print(SERVER_TO_TEST);
    Serial.print(F(":"));
    Serial.println(PORT_TO_TEST);
  }

  // Periksa status link Ethernet fisik (apakah kabel terhubung)
  if (Ethernet.linkStatus() == LinkOFF) {
    if (verbose) Serial.println(F("Kabel Ethernet terputus/Link OFF."));
    return false; // Kabel putus, berarti offline
  }

  // Coba buat koneksi TCP ke server DNS Google (atau server lain yang ditentukan)
  if (ethClient.connect(SERVER_TO_TEST, PORT_TO_TEST)) {
    if (verbose) Serial.println(F("Koneksi berhasil!"));
    ethClient.stop(); // Putuskan koneksi setelah berhasil
    return true; // Koneksi online
  } else {
    if (verbose) Serial.println(F("Koneksi gagal."));
    ethClient.stop(); // Pastikan koneksi ditutup jika gagal
    return false; // Koneksi offline
  }
}

// --- Fungsi handleButtons() ---
// Mengelola input dari tombol SELECT untuk mengubah tampilan LCD.
void handleButtons() {
  static unsigned long lastSelectPressTime = 0; // Waktu terakhir tombol ditekan
  // Cek apakah tombol SELECT sedang ditekan (pin LOW karena INPUT_PULLUP)
  if (digitalRead(SELECT_BUTTON_PIN) == LOW) {
    // Implementasi debounce untuk mencegah multiple trigger dari satu penekanan
    if (millis() - lastSelectPressTime > 200) { // Jeda 200ms untuk debounce
      displayLastOffline = !displayLastOffline; // Toggle status tampilan
      Serial.print(F("Tombol SELECT ditekan, tampilkan Last offline: "));
      Serial.println(displayLastOffline ? F("TRUE") : F("FALSE"));
      lastDisplayUpdateTime = 0; // Paksa update LCD segera setelah tombol ditekan
      lastSelectPressTime = millis(); // Perbarui waktu penekanan terakhir
    }
  }
}
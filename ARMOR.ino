/*
ARMOR (Automatically Restart Offline Modem or Router with Arduino)
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
#include <SPI.h>       // Untuk komunikasi SPI, penting untuk Ethernet Shield
#include <Ethernet.h>  // Library Arduino Ethernet untuk konektivitas jaringan
#include <Wire.h>      // Untuk komunikasi I2C, digunakan oleh LCD dan RTC
#include <RTClib.h>    // Library untuk modul Real Time Clock (RTC) DS3231
#include <LiquidCrystal_I2C.h> // Library untuk LCD 16x2 dengan modul I2C
#include <NTPClient.h> // Library untuk sinkronisasi waktu dengan Network Time Protocol (NTP)
#include <EthernetUdp.h> // Untuk komunikasi UDP, dibutuhkan oleh NTPClient
#include <EEPROM.h>    // Library untuk menyimpan data secara persisten di EEPROM Arduino
#include <avr/wdt.h>   // Library untuk Watchdog Timer (fungsi reset)

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
EthernetUDP ntpUdp;      // Objek UDP untuk komunikasi NTP

// Buffer sementara untuk nama server NTP.
// NTPClient mengharapkan char* dari RAM, sehingga kita perlu menyalin nama server dari PROGMEM ke sini.
char ntpServerBuffer[30];

// Objek NTPClient, diinisialisasi dengan objek UDP dan offset waktu.
// Nama server NTP akan diset kemudian di setup() dari ntpServerBuffer.
NTPClient timeClient(ntpUdp, UTC_OFFSET_IN_SECONDS);
RTC_DS3231 rtc;                  // Objek RTC untuk mengakses modul DS3231
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
  uint8_t day;       // Hari saat offline dimulai
  uint8_t month;     // Bulan saat offline dimulai
  uint8_t hourStart; // Jam saat offline dimulai
  uint8_t minuteStart;// Menit saat offline dimulai
  uint8_t hourEnd;   // Jam saat online kembali (offline berakhir)
  uint8_t minuteEnd; // Menit saat online kembali (offline berakhir)
  bool isValid;      // Flag untuk menandakan apakah data log valid atau kosong
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
  // Matikan Watchdog jika sebelumnya diaktifkan (agar tidak reset lagi setelah boot)
  wdt_disable(); // Selalu matikan watchdog di awal setup

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
  lcd.print(F("    **ARMOR** ")); // Tampilkan pesan awal di LCD
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

  delay(3000); // Jeda singkat

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

  Serial.print(F("Menginisialisasi Ethernet (DHCP)..."));
  
  int dhcpRetryCount = 0;
  const int MAX_DHCP_RETRIES = 3; // Jumlah percobaan maksimal
  const unsigned long DHCP_RETRY_INTERVAL = 1000; // Jeda 1 detik antar percobaan

  while (Ethernet.begin(mac) == 0) {
    dhcpRetryCount++;
    Serial.print(F("Gagal DHCP (Percobaan "));
    Serial.print(dhcpRetryCount);
    Serial.println(F(")"));

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("DHCP Gagal !"));
    lcd.setCursor(0, 1);
    char statusBuffer[17];
    snprintf_P(statusBuffer, sizeof(statusBuffer), PSTR("Coba %d/%d...     "), dhcpRetryCount, MAX_DHCP_RETRIES);
    lcd.print(statusBuffer);

    // Cek status hardware untuk membedakan kegagalan
    if (Ethernet.hardwareStatus() == EthernetNoHardware) {
      Serial.println(F("Fatal: Tidak ada hardware Ethernet. Berhenti."));
      lcd.clear();
      lcd.print(F("Error Fatal:"));
      lcd.setCursor(0, 1);
      lcd.print(F("No ETH H/W!"));
      while (true) delay(100); // Hentikan jika hardware tidak ada
    }

    if (dhcpRetryCount >= MAX_DHCP_RETRIES) {
      // Melakukan reset menggunakan Watchdog Timer
      Serial.println(F("Gagal mendapatkan IP setelah banyak percobaan. Reset Arduino!"));
      lcd.clear();
      lcd.print(F("DHCP Gagal !!"));
      lcd.setCursor(0, 1);
      lcd.print(F("RESET ARDUINO !"));
      delay(3000); // Jeda sebelum reset
      wdt_enable(WDTO_15MS); // Aktifkan watchdog dengan timeout minimum
      while(true); // Loop tak terbatas akan memicu reset oleh watchdog
    }

    
    delay(DHCP_RETRY_INTERVAL); // Tunggu sebelum mencoba lagi
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
    // Jika sistem dalam mode ISP Offline, lakukan pengecekan setiap 5 detik.
    if (isInISPOfflineMode) {
      // Selalu coba cek koneksi internet setiap 5 detik di mode ISP Offline
      // Ini agar bisa langsung kembali ke layar status jika online
      if (millis() - lastOfflineCheckTime >= 5000 || lastOfflineCheckTime == 0) {
        Serial.println(F("Mencoba cek internet dalam mode ISP Offline..."));
        bool currentInternetStatus = checkInternetConnection(true); // Coba cek koneksi lagi
        lastOfflineCheckTime = millis(); // Reset waktu cek

        if (currentInternetStatus) {
          // Jika internet online kembali, keluar dari mode ISP Offline
          Serial.println(F("Internet ONLINE lagi setelah mode ISP Offline. Kembali ke status normal."));
          isInternetOnline = true; // Set status global
          isInISPOfflineMode = false;
          offlineAttemptCount = 0; // Reset hitungan percobaan

          // Logika pencatatan log offline
          if (offlineStartTime != 0) {
            unsigned long offlineDurationSec = (millis() - offlineStartTime) / 1000UL;
            const unsigned long MIN_LOG_DURATION = 60UL; // 1 menit

            if (offlineDurationSec >= MIN_LOG_DURATION) {
              DateTime now = rtc.now();
              lastOfflineLog.hourEnd = now.hour();
              lastOfflineLog.minuteEnd = now.minute();
              lastOfflineLog.isValid = true;
              EEPROM.put(EEPROM_ADDRESS, lastOfflineLog);
              Serial.println(F("Durasi offline (> 1 menit) dicatat ke EEPROM."));
            }
          }
          offlineStartTime = 0; // Reset waktu mulai offline
          
          lastDisplayUpdateTime = 0; // Paksa refresh LCD
          return; // Keluar dari loop() untuk segera menampilkan status online normal
        } else {
          Serial.println(F("Masih OFFLINE."));
        }
      }

      // Logika ini hanya untuk logging/pesan, bukan untuk memicu pengecekan
      if (millis() - lastISPOfflineMessageTime >= 30UL * 60UL * 1000UL) {
        Serial.println(F("Peringatan: 30 menit berlalu, masih di mode ISP Offline."));
        lastISPOfflineMessageTime = millis(); // Reset timer pesan 30 menit
      }
      return; // Keluar dari loop() untuk saat ini, akan diulang lagi
    }

    // --- Logika Pengecekan Koneksi Normal dan Penanganan Restart Modem ---
    // Pengecekan koneksi dilakukan setiap 5 detik agar tidak membebani
    if (millis() - lastOfflineCheckTime >= 5000 || lastOfflineCheckTime == 0) {
      bool currentInternetStatus = checkInternetConnection(true); // Cek status internet saat ini
      lastOfflineCheckTime = millis(); // Reset waktu cek

      // Deteksi perubahan status: Offline -> Online
      if (!isInternetOnline && currentInternetStatus) {
        if (offlineStartTime != 0) { // Jika ada waktu offline yang tercatat
          unsigned long offlineDurationSec = (millis() - offlineStartTime) / 1000UL;
          const unsigned long MIN_LOG_DURATION = 60UL; // 1 menit

          if (offlineDurationSec >= MIN_LOG_DURATION) {
            DateTime now = rtc.now();
            lastOfflineLog.hourEnd = now.hour();
            lastOfflineLog.minuteEnd = now.minute();
            lastOfflineLog.isValid = true; // Tandai log valid
            EEPROM.put(EEPROM_ADDRESS, lastOfflineLog); // Simpan durasi offline ke EEPROM
            Serial.println(F("Durasi offline (> 1 menit) dicatat ke EEPROM."));
          } else {
            Serial.println(F("Durasi offline kurang dari 1 menit. Log tidak disimpan."));
          }
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
    }

    // Lanjutkan logika restart jika internet offline
    if (!isInternetOnline) {
      // Jika internet offline, mulai proses pemulihan
      Serial.println(F("Internet OFFLINE. Memulai proses pemulihan..."));

      // Jika ini deteksi offline pertama kali (setelah online), mulai hitung mundur 1 menit
      if (offlineAttemptCount == 0 && millis() - offlineStartTime < 60UL * 1000UL) {
        // Belum mencapai 1 menit offline, lewati logika restart
        Serial.println(F("Menunggu 1 menit sejak offline terdeteksi..."));
        return;
      }

      // Setelah 1 menit atau jika sudah melewati 1 menit, cek percobaan restart
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
        
        bool restartSucceededEarly = false; // Flag baru untuk mendeteksi keberhasilan awal

        while (millis() - restartWaitStartTime < threeMinuteDelay) {
            
            // Logika baru: Cek koneksi secara berkala dan keluar jika online
            if (millis() - lastOfflineCheckTime >= 5000) { 
              if (checkInternetConnection(true)) { // Cek koneksi
                Serial.println(F("INTERNET ONLINE terdeteksi saat hitung mundur!"));
                restartSucceededEarly = true;
                break; // Keluar dari loop hitung mundur
              }
              lastOfflineCheckTime = millis(); // Reset waktu cek koneksi
            }

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
        
        if (restartSucceededEarly) {
            // Atur status agar sistem tahu internet sudah online
            isInternetOnline = true;
            offlineAttemptCount = 0; // Reset hitungan percobaan karena sudah berhasil
            
            // Catat log offline yang berakhir
            if (offlineStartTime != 0) { 
              unsigned long offlineDurationSec = (millis() - offlineStartTime) / 1000UL;
              const unsigned long MIN_LOG_DURATION = 60UL; // 1 menit

              if (offlineDurationSec >= MIN_LOG_DURATION) {
                DateTime now = rtc.now();
                lastOfflineLog.hourEnd = now.hour();
                lastOfflineLog.minuteEnd = now.minute();
                lastOfflineLog.isValid = true;
                EEPROM.put(EEPROM_ADDRESS, lastOfflineLog);
                Serial.println(F("Durasi offline (> 1 menit) dicatat ke EEPROM."));
              } else {
                Serial.println(F("Durasi offline kurang dari 1 menit. Log tidak disimpan."));
              }
            }
            offlineStartTime = 0;
            
            // Kembali ke awal loop() untuk menampilkan status online normal
            return; 
        }
        
        lastOfflineCheckTime = 0; // Reset waktu untuk cek internet segera
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

// --- Fungsi displayCurrentTimeAndStatus() ---
// Menampilkan waktu saat ini dan status koneksi internet di LCD.
void displayCurrentTimeAndStatus() {
  // Dapatkan waktu dari RTC
  DateTime now = rtc.now(); 
  char dateTimeBuffer[17]; 
  // Format string tanggal dan waktu: DD/MM HH:MM
  snprintf_P(dateTimeBuffer, sizeof(dateTimeBuffer), PSTR("%02d/%02d %02d:%02d"),
             now.day(), now.month(), now.hour(), now.minute());

  lcd.clear(); // Bersihkan LCD

  // Baris 1: Tanggal & Waktu
  lcd.setCursor(0, 0);

  if (isInISPOfflineMode) {
    // Tampilkan status ISP OFFLINE di baris 1 saat mode aktif
    lcd.print(F("ISP OFFLINE!!")); 
  } else {
    // Tampilkan waktu dan tanggal normal di baris 1
    lcd.print(dateTimeBuffer);
  }

  // Baris 2: Status Koneksi / Hitung Mundur
  lcd.setCursor(0, 1);

  if (isInISPOfflineMode) {
    // Tampilkan hitung mundur 30 menit
    const unsigned long MAX_ISP_OFFLINE_TIME = 30UL * 60UL * 1000UL; // 30 menit
    unsigned long elapsedTime = millis() - lastISPOfflineMessageTime;
    
    // Hitung sisa waktu hingga pengecekan 30 menit berikutnya
    // Note: Karena pengecekan internet terjadi setiap 5 detik,
    // hitung mundur ini menunjukkan sisa waktu hingga interval 30 menit tercapai.
    unsigned long timeSinceLast30MinCheck = elapsedTime % MAX_ISP_OFFLINE_TIME;
    unsigned long remainingTimeMs = MAX_ISP_OFFLINE_TIME - timeSinceLast30MinCheck;
    
    unsigned long remainingTimeSec = remainingTimeMs / 1000UL;
    int minutes = remainingTimeSec / 60;
    int seconds = remainingTimeSec % 60;

    char countdownBuffer[17];
    // Format: Cek: MM:SS     
    snprintf_P(countdownBuffer, sizeof(countdownBuffer), PSTR("Cek: %02d:%02d    "), minutes, seconds);
    lcd.print(countdownBuffer);

  } else if (isInternetOnline) {
    lcd.print(F("Status: ONLINE  ")); // Status online
  } else {
    // Status offline (pra-ISP Offline mode), hitung durasi
    unsigned long offlineDurationSec = (millis() - offlineStartTime) / 1000UL;
    int minutes = offlineDurationSec / 60;
    int seconds = offlineDurationSec % 60;

    char durationBuffer[17];
    // Format durasi offline: OFF: MM:SS
    snprintf_P(durationBuffer, sizeof(durationBuffer), PSTR("OFF: %02d:%02d      "), minutes, seconds);
    lcd.print(durationBuffer);
  }
}

// --- Fungsi displayLastOfflineTime() ---
// Menampilkan log waktu offline terakhir yang disimpan di EEPROM.
void displayLastOfflineTime() {
  lcd.clear(); // Bersihkan LCD

  if (lastOfflineLog.isValid) {
    // Tampilkan waktu mulai offline (Baris 1)
    lcd.setCursor(0, 0);
    char startBuffer[17];
    snprintf_P(startBuffer, sizeof(startBuffer), PSTR("OFF: %02d/%02d %02d:%02d"),
               lastOfflineLog.day, lastOfflineLog.month, lastOfflineLog.hourStart, lastOfflineLog.minuteStart);
    lcd.print(startBuffer);

    // Tampilkan waktu online kembali (Baris 2)
    lcd.setCursor(0, 1);
    char endBuffer[17];
    snprintf_P(endBuffer, sizeof(endBuffer), PSTR("ON:  %02d:%02d"),
               lastOfflineLog.hourEnd, lastOfflineLog.minuteEnd);
    lcd.print(endBuffer);
  } else {
    lcd.setCursor(0, 0);
    lcd.print(F("Tidak ada Log"));
    lcd.setCursor(0, 1);
    lcd.print(F("Offline Valid"));
  }
}

// --- Fungsi checkInternetConnection() ---
// Mencoba membuat koneksi ke SERVER_TO_TEST untuk memastikan koneksi internet.
bool checkInternetConnection(bool verbose) {
  if (verbose) {
    Serial.print(F("Mencoba cek koneksi ke "));
    Serial.print(SERVER_TO_TEST);
    Serial.print(F("... "));
  }

  // Coba hubungkan ke server dan port yang ditentukan
  if (ethClient.connect(SERVER_TO_TEST, PORT_TO_TEST)) {
    // Jika koneksi berhasil, itu berarti internet online
    ethClient.stop(); // Tutup koneksi
    if (verbose) Serial.println(F("ONLINE!"));
    return true;
  } else {
    // Jika koneksi gagal (timeout atau server tidak merespons), anggap offline
    ethClient.stop(); // Pastikan koneksi ditutup jika gagal
    if (verbose) Serial.println(F("OFFLINE."));
    return false;
  }
}

// --- Fungsi handleButtons() ---
// Memproses input dari tombol SELECT untuk beralih mode tampilan LCD.
void handleButtons() {
  // Tombol menggunakan INPUT_PULLUP, jadi LOW saat ditekan
  if (digitalRead(SELECT_BUTTON_PIN) == LOW) {
    delay(50); // Debounce
    if (digitalRead(SELECT_BUTTON_PIN) == LOW) {
      // Tombol ditekan, ganti mode tampilan
      displayLastOffline = !displayLastOffline;
      Serial.print(F("Tombol SELECT ditekan. Tampilan beralih ke: "));
      Serial.println(displayLastOffline ? F("Log Offline") : F("Status Normal"));
      lastDisplayUpdateTime = 0; // Paksa update LCD segera

      // Tunggu hingga tombol dilepas
      while (digitalRead(SELECT_BUTTON_PIN) == LOW) {
        delay(10);
      }
    }
  }
}

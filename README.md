# ARMOR
Automatically Restart Offline Modem or Router with Arduino

[![Build Status](https://img.shields.io/badge/Status-Stable-brightgreen)](https://github.com/denypradana/ARMOR)
[![Arduino IDE](https://img.shields.io/badge/Arduino%20IDE-1.8%2B-blue)](https://www.arduino.cc/en/software)
[![License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

---

## Deskripsi Proyek

Proyek ini adalah solusi berbasis **Arduino** untuk mengatasi masalah koneksi internet yang sering terputus pada modem Anda. Dengan menggunakan **Arduino Uno** yang dilengkapi **Ethernet Shield** dan **Modul RTC DS3231**, sistem ini akan secara otomatis mendeteksi ketika koneksi internet *offline* dan me-restart modem Anda menggunakan **modul relay**. Jika restart berkali-kali gagal, sistem akan menampilkan pesan "ISP Offline" di LCD, memberikan indikasi jelas bahwa masalah mungkin ada pada penyedia layanan internet Anda.

Proyek ini sangat cocok untuk penggunaan di rumah atau kantor kecil yang membutuhkan koneksi internet stabil 24/7 tanpa intervensi manual.

---

## Fitur Utama

* **Deteksi Koneksi Otomatis:** Memeriksa koneksi internet secara berkala dengan melakukan ping ke server yang ditentukan (misalnya Google DNS).
* **Restart Modem Otomatis:** Menggunakan relay untuk memutus dan menyambung daya modem ketika internet terdeteksi *offline*.
* **Log Waktu Offline:** Mencatat durasi modem *offline* terakhir ke EEPROM dan dapat ditampilkan di LCD.
* **Sinkronisasi Waktu NTP:** Memastikan waktu sistem akurat dengan melakukan sinkronisasi dengan Network Time Protocol (NTP).
* **Mode ISP Offline:** Mencegah *reboot loop* yang tidak perlu dengan beralih ke mode pasif setelah 3 kali percobaan restart gagal.
* **Tampilan LCD:** Menampilkan status koneksi, waktu, dan log *offline* terakhir.
* **Tombol Navigasi:** Tombol fisik untuk beralih tampilan informasi di LCD.
* **Tombol Reset:** Tombol fisik untuk reset/restart alat.

---

## Komponen yang Dibutuhkan

* **Arduino Uno R3**
* **Arduino Ethernet Shield W5100**
* **Modul RTC DS3231**
* **LCD 16x2 dengan Modul I2C**
* **Relay Board 2 Channel** (sesuaikan logic aktif HIGH/LOW dengan relay Anda)
* **Tombol Tekan** (Push Button)
* **Elco 220uF minimal 16V**
* **Resistor 220 Ohm**
* Kabel Jumper
* Kabel Ethernet
* Power Supply untuk Arduino

---

## Skema Pengkabelan (Wiring Diagram)

![Wiring Diagram Placeholder](ARMOR_SCHEMATIC.png)

*Pastikan koneksi berikut:*
* **Ethernet Shield:** Langsung tumpuk di atas Arduino Uno.
    * **Penting:** Hindari penggunaan Pin **4** (SD Card), Pin **9** (W5100\_RST), Pin **10** (W5100\_CS), Pin **11** (SPI), Pin **12** (SPI), Pin **13** (SPI) untuk komponen lain agar tidak terjadi konflik.
* **LCD I2C:**
    * SDA ke **A4** (Arduino Uno)
    * SCL ke **A5** (Arduino Uno)
    * VCC ke **5V**
    * GND ke **GND**
* **Modul RTC DS3231:**
    * SDA ke **A4** (Arduino Uno)
    * SCL ke **A5** (Arduino Uno)
    * VCC ke **5V**
    * GND ke **GND**
* **Relay Board:**
    * IN1 ke **Pin 2** (Arduino Uno)
    * IN2 ke **Pin 3** (Arduino Uno)
    * VCC ke **5V** (Arduino Uno)
    * GND ke **GND** (Arduino Uno)
    * Hubungkan output relay ke kabel daya modem Anda.
* **Tombol SELECT:**
    * Satu kaki ke **Pin 5** (Arduino Uno)
    * Kaki lainnya ke **GND** (Gunakan `INPUT_PULLUP` pada kode, jadi tidak perlu resistor pull-up eksternal)
* **Tombol RESET:**
    * Satu kaki ke **Pin RES** (Arduino Uno)
    * Kaki lainnya ke **GND**
* **Rangkaian POR (Power-On Reset):**
    * Kaki Negatif Elco ke **GND** Ethernet Shield
    * Kaki Positif Elco diseri dengan **Resistor 220 Ohm**
    * Kaki Resistor yang lain ke **Pin RES** Ethernet Shield

---

## Instalasi dan Konfigurasi

1.  **Instalasi Arduino IDE:** Unduh dan instal [Arduino IDE](https://www.arduino.cc/en/software).
2.  **Instalasi Library:**
    Buka Arduino IDE, pergi ke `Sketch > Include Library > Manage Libraries...` dan cari serta instal library berikut:
    * `Ethernet` (Biasanya sudah terinstal)
    * `Wire` (Biasanya sudah terinstal)
    * `RTClib` by Adafruit
    * `LiquidCrystal I2C` by Frank de Brabander
    * `NTPClient` by Fabrice Weinberg
    * `EEPROM` (Biasanya sudah terinstal)
3.  **Buka Kode Program:** Unduh file `.ino` dari repositori ini dan buka di Arduino IDE.
4.  **Konfigurasi MAC Address:**
    Pastikan `byte mac[]` pada baris `byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };` unik di jaringan Anda. Anda bisa menggunakan nilai default ini jika tidak ada perangkat lain dengan MAC yang sama.
5.  **Konfigurasi NTP Server dan Offset:**
    * `const char NTP_SERVER[] PROGMEM = "pool.ntp.org";` (Anda bisa mengganti server NTP jika perlu).
    * `const long UTC_OFFSET_IN_SECONDS = 7L * 3600L;` (Ubah `7L` sesuai dengan zona waktu Anda. Untuk WIB adalah UTC+7).
6.  **Konfigurasi Pin Relay:**
    Pastikan `RELAY_PIN_1` dan `RELAY_PIN_2` sesuai dengan pin yang Anda gunakan (default 2 dan 3). Sesuaikan juga *logic* `HIGH` atau `LOW` pada `digitalWrite()` di fungsi `setup()` dan `loop()` agar sesuai dengan jenis relay board Anda (aktif HIGH atau aktif LOW).
7.  **Upload Kode:** Pilih board Arduino Uno Anda dan port yang benar, lalu upload kode ke Arduino.
 
---

## Cara Kerja

Setelah dinyalakan, Arduino akan:
1.  **Memulai Inisialisasi:** Menginisialisasi semua komponen (LCD, RTC, Ethernet).
2.  **Menunggu Modem Boot:** Memberi jeda 3 menit di awal agar modem memiliki waktu untuk *boot up* sepenuhnya dan mendapatkan IP.
3.  **Sinkronisasi Waktu:** Mencoba sinkronisasi waktu dengan server NTP untuk mengatur RTC.
4.  **Memantau Koneksi:** Secara terus-menerus melakukan ping ke `SERVER_TO_TEST` (default: 8.8.8.8) di *background*.
5.  **Penanganan Offline:**
    * Jika internet *offline*, sistem akan menunggu 1 menit.
    * Jika masih *offline* setelah 1 menit, Arduino akan mengaktifkan relay untuk me-restart modem (memutus dan menyambung daya). Ini akan diulang hingga 3 kali.
    * Setelah modem di-restart, sistem akan menunggu 3 menit untuk modem *boot* kembali sebelum mengecek koneksi lagi.
6.  **Mode ISP Offline:** Jika ketiga percobaan restart gagal, sistem akan beralih ke mode "ISP Offline" dan hanya akan mencoba cek koneksi setiap 30 menit, menunjukkan bahwa masalah mungkin di luar kendali sistem (misalnya, gangguan dari ISP).
7.  **Pencatatan Log:** Ketika internet *offline* dan kemudian *online* kembali, durasi *offline* akan dicatat dan disimpan di EEPROM.
8.  **Tampilan LCD:** LCD akan terus memperbarui status internet, waktu saat ini. Dengan menekan tombol SELECT, Anda bisa beralih untuk melihat log waktu *offline* terakhir.

---

## Riwayat Revisi

### Versi [28 September 2025] (Berdasarkan Kode Program dan Gambar Schematic Terlampir)

Revisi ini fokus pada peningkatan stabilitas, efisiensi, dan keandalan sistem.

* **Peningkatan Stabilitas Sistem (Watchdog Timer):**
    * Implementasi **Watchdog Timer (WDT)** (`<avr/wdt.h>`) untuk mendeteksi dan secara otomatis mereset Arduino dari kondisi *hang* (seperti *infinite loop* atau kegagalan inisialisasi kritis).
    * Menambahkan mekanisme reset sistem menggunakan WDT jika **inisialisasi DHCP gagal** setelah beberapa kali percobaan.
* **Penyempurnaan Logika Restart Modem:**
    * Mengoptimalkan waktu tunggu 3 menit setelah *restart* dengan melakukan **pengecekan koneksi secara berkala** (setiap 5 detik). Jika internet *online* kembali lebih awal, *countdown* 3 menit akan langsung dihentikan, sehingga sistem lebih responsif.
* **Penyimpanan Log EEPROM yang Lebih Detail:**
    * Log durasi *offline* hanya disimpan ke EEPROM jika durasi *offline* mencapai **minimal 1 menit**, menghindari pencatatan *log* yang tidak signifikan.
* **Peningkatan Tampilan LCD:**
    * Tampilan LCD dalam mode **ISP Offline** kini menampilkan hitungan mundur menuju pengecekan koneksi berikutnya (setiap 30 menit).
* **Penambahan Rangkaian POR (Power-On Reset):**
    * Memperbaiki error Ethernet Shield tidak respon ketika pertama kali menghidupkan alat.

---

## Kontribusi

Kami sangat menyambut kontribusi dari komunitas! Jika Anda memiliki ide untuk peningkatan, menemukan *bug*, atau ingin menambahkan fitur baru, silakan:
1.  **Fork** repositori ini.
2.  Buat *branch* baru (`git checkout -b feature/nama-fitur-baru`).
3.  Lakukan perubahan Anda.
4.  **Commit** perubahan Anda (`git commit -am 'Add new feature'`).
5.  **Push** ke *branch* (`git push origin feature/nama-fitur-baru`).
6.  Buat **Pull Request**.

---

## Lisensi

Proyek ini dilisensikan di bawah [Lisensi MIT](LICENSE). Lihat file `LICENSE` untuk detail lebih lanjut.

---

## Kontak

Untuk pertanyaan atau umpan balik, silakan hubungi:

**Deny Pradana**
* Email: [dp@denypradana.com](mailto:dp@denypradana.com)
* Instagram: [@denypradana](...)

---

Anda bisa melihat penjelasan tentang Watchdog Timer yang digunakan pada kode program ini untuk membantu mencegah Arduino *hang* di video berikut: [The Watchdog Timer on Arduino](https://www.youtube.com/watch?v=boMHq9PBy9M).

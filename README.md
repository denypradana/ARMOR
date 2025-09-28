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

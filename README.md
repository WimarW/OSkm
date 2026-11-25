# OSkm

![Nairo, maskot OSkm](mascot.jpeg)

*Nairo, maskot OSkm.*

> Sistem operasi 32-bit x86 yang dibangun dari nol: dari bootloader sampai shell di user mode.
> Tugas Besar IF2130 Sistem Operasi, 2026.

```
 GRUB -> kernel (higher half) -> GDT/IDT/PIC -> paging + swap
      -> EXT2 di ATA disk -> TSS + user mode -> syscall -> scheduler -> shell
```

## Kontributor

| Nama | NIM | Akun Git |
|------|-----|----------|
| Wimar Widiarto | 13525009 | WimarW |
| Naufal Hasbialhaq | 13525120 | Naufal Hasbialhaq |
| Muhammad Atallah Ramadhan | 13525015 | atarmdhnn |
| Gede Pranajayanta Suputra | 13525099 | soepoetra |

## Fitur

| Area | Yang diimplementasikan |
|------|------------------------|
| Boot | GRUB Legacy (multiboot), kernel ELF32 di-link di 1 MiB, higher half kernel |
| Loading screen | Splash screen dengan judul ASCII dan maskot Nairo berwarna, progress bar bertahap yang mengikuti tahap inisialisasi kernel |
| CPU | GDT (segmen kernel, user, dan TSS), IDT, remap PIC 8259 |
| Driver | Framebuffer teks, keyboard (PS/2), disk ATA PIO (drive utama dan drive swap), CMOS RTC |
| File system | EXT2 versi IF2130 (superblock, block group, bitmap, inode, direktori bersarang, blok indirect dan doubly indirect untuk file besar) |
| Memori | Paging 4 KiB dengan recursive mapping, frame allocator, swap space ke disk dengan page replacement algoritma Clock |
| Proses | User mode (ring 3), process control block, scheduler round robin berbasis timer (IRQ0), context switch |
| System call | `int 0x30`, 17 syscall (lihat tabel di bawah) |
| Shell | Shell interaktif di user mode dengan pipe dan regex |
| Aplikasi | `clock` (jam di pojok kanan bawah), `hello` (Hello World!), dan `badapple` (animasi ASCII), semuanya dijalankan lewat `exec` |

## Cara Menjalankan

Kebutuhan: `nasm`, `qemu-system-i386`, `make`, dan pembuat ISO (`genisoimage` atau `mkisofs`).
Linux dan WSL memakai `gcc` dan `ld`. macOS (Apple Silicon) memakai `clang` dan `ld.lld` (dari Homebrew `llvm`/`lld`) serta `i386-elf-gdb` untuk debugging.

```
make run
```

Satu perintah ini membangun kernel dan ISO, membuat disk kosong 4 MiB, memformatnya sebagai EXT2, memasukkan `shell`, `clock`, dan `hello`, lalu menjalankan QEMU. Loading screen tampil sebentar, kemudian prompt shell muncul.

Perhatian:
- **Disk dibuat ulang setiap `make run`**, jadi file yang kamu buat di dalam OS tidak disimpan antar sesi.
- Tutup QEMU yang sedang berjalan sebelum menjalankannya lagi, karena image disk terkunci selama QEMU hidup.
- Jangan memakai `sample-image.bin` bawaan `.kit/`. Formatnya berbeda dari implementasi EXT2 kami.

### Perintah Make

| Perintah | Fungsi |
|----------|--------|
| `make run` | Build, buat disk baru, masukkan program, lalu jalankan di QEMU |
| `make debug` | Sama seperti `run`, tapi QEMU berhenti menunggu gdb (`-s -S`) |
| `make run-badapple` | Seperti `run`, tapi juga memasukkan Bad Apple (butuh `bin/badapple.bin`) |
| `make install-apps` | Buat disk kosong, lalu masukkan `shell`, `clock`, dan `hello` |
| `make insert-shell` / `insert-clock` / `insert-hello` | Masukkan satu program ke disk |
| `make insert-badapple` | Masukkan pemutar Bad Apple dan datanya |
| `make clean` | Hapus hasil build, termasuk semua `bin/*.bin` (disk, swap, dan data Bad Apple) |

## Loading Screen

Saat booting, kernel menampilkan splash screen (`src/loadscreen.c`): judul ASCII "NAIRO" (nama maskot), maskot berwarna 13 baris, dan progress bar yang terisi seiring tahap inisialisasi:

1. Menginisialisasi interrupt
2. Me-mount file system EXT2
3. Mengatur TSS dan memori
4. Menjalankan shell

Bar memakai karakter blok CP437 (`0xDB` untuk bagian terisi, `0xB0` untuk sisa), dan kursor VGA disembunyikan selama splash. Jeda antar tahap berupa busy loop (`splash_delay`), jadi lamanya bergantung pada kecepatan emulasi QEMU.

## Shell

Setelah booting, prompt `user@os:/$` muncul.

| Command | Fungsi |
|---------|--------|
| `ls [dir]`, `cd [dir]`, `pwd` | Navigasi direktori |
| `mkdir`, `cp`, `mv`, `rm`, `cat` | Operasi file dan folder |
| `find <nama>` | Cari file dari root |
| `grep [-Eivn] <pola> [file]` | Cari teks (mendukung regex dan pipe) |
| `clear`, `help` | Bersihkan layar, bantuan |
| `exec <path>` | Jalankan program dari disk sebagai proses baru |
| `ps` | Daftar proses yang berjalan |
| `kill <pid>` | Hentikan proses |

Contoh:

```
user@os:/$ exec clock       # jam muncul di pojok kanan bawah
user@os:/$ ps
PID  NAME
1    shell
2    clock
user@os:/$ kill 2           # jam berhenti
user@os:/$ exec hello
Hello World!
```

## Bad Apple (opsional)

Pemutar animasi ASCII yang berjalan sebagai program user. Fitur ini sepenuhnya opsional: `make run` biasa tetap berjalan normal tanpa data Bad Apple.

Videonya tidak disertakan di repository, jadi kamu perlu menyediakan sendiri, ditambah `ffmpeg` dan Python dengan Pillow (di Linux/WSL: `sudo apt install ffmpeg`; di macOS: `python3 -m venv .venv`, `source .venv/bin/activate`, lalu `python3 -m pip install pillow` — `Makefile` otomatis memakai `.venv/bin/python3` jika tersedia).

Resolusi efektif 80x50: satu sel layar menampilkan dua piksel bertumpuk memakai karakter setengah blok.

```bash
# Pertama kali: otomatis mengonversi video ke bin/badapple.bin, memasang, lalu menjalankan QEMU
make run BADAPPLE_VIDEO=path/to/badapple.mp4

# Jalankan berikutnya: otomatis memakai bin/badapple.bin yang sudah ada
make run
```

Lalu di shell, jalankan `exec badapple`. Tekan `ESC` atau `Ctrl+C` kapan saja untuk menghentikan animasi lebih awal.

- FPS dan durasi bisa diatur lewat variabel Make (default `BADAPPLE_FPS=10`, `BADAPPLE_SEC=30`):
  ```bash
  make run BADAPPLE_VIDEO=path/to/badapple.mp4 BADAPPLE_FPS=10 BADAPPLE_SEC=30
  ```
- Untuk melihat preview hasil konversi langsung di terminal sebelum masuk ke OS:
  ```bash
  make preview-badapple BADAPPLE_VIDEO=path/to/badapple.mp4
  ```
- `make run-badapple` juga tersedia jika ingin memaksa build dengan Bad Apple (berhenti dengan pesan error kalau `bin/badapple.bin` belum ada dan `BADAPPLE_VIDEO` tidak diberikan).
- Data disimpan 1 bit per piksel (500 byte per frame), jadi ukurannya `8 + 500 x fps x detik` byte. `DATA_MAX` di `src/badapple.c` harus sama atau lebih besar dari itu.
- `make clean` menghapus semua `bin/*.bin` termasuk `bin/badapple.bin`, jadi jalankan lagi dengan `BADAPPLE_VIDEO=...` setelah `make clean`.

## System Call

Dipanggil dengan `int 0x30`. Nomor syscall di `eax`, argumen di `ebx`, `ecx`, `edx`.

| No | Nama | Fungsi |
|----|------|--------|
| 0 | read | Baca file |
| 1 | read_directory | Baca isi direktori |
| 2 | write | Tulis file atau buat folder |
| 3 | delete | Hapus file atau folder kosong |
| 4 | getc | Baca satu karakter keyboard |
| 5 | putc | Cetak satu karakter |
| 6 | print | Cetak string berwarna |
| 7 | keyboard activate | Aktifkan input keyboard |
| 8 | clear screen | Bersihkan layar |
| 9 | get time | Baca jam dari CMOS |
| 10 | exit | Akhiri proses yang memanggil |
| 11 | exec | Buat proses baru dari file |
| 12 | kill | Hentikan proses |
| 13 | process list | Daftar proses aktif (`ps`) |
| 14 | putc at | Cetak karakter di posisi tertentu |
| 15 | get ticks | Baca penghitung tick timer (1 tick = 1 ms) |
| 16 | draw frame | Salin 2000 karakter (80x25) ke layar sekaligus |

## Debugging

Tekan F5 di Visual Studio Code, atau jalankan:

```
make debug
```

Di terminal lain, dari root project:

```
gdb bin/kernel          # macOS: i386-elf-gdb bin/kernel
(gdb) target remote localhost:1234
```

## Struktur Repository

```
src/            kernel, driver, EXT2, paging, swap, scheduler, loading screen, dan program user
src/header/     header per subsistem (cpu, driver, filesystem, memory, process, text, stdlib)
tests/          tes scheduler, context switch, dan round trip EXT2 (tests/ext2_roundtrip.c)
tools/          skrip konversi video ke frame ASCII (untuk Bad Apple)
docs/           jurnal milestone (M0, M1, M2)
other/          bootloader GRUB
.kit/           kit milestone dari asisten
Makefile        build, ISO, dan pemasangan program ke disk
```

## Catatan Teknis

- Ukuran RAM yang dikelola allocator diatur di `SYSTEM_MEMORY_MB` (`src/header/memory/paging.h`). Kecilkan nilainya untuk menguji swap, dan samakan dengan RAM yang diberikan ke QEMU.
- Waktu pada `clock` mengikuti RTC QEMU. `make run` memakai `-rtc base=localtime`, jadi jam sesuai waktu lokal komputer.
- Setiap program user di-link dengan `user-linker.ld` dan `crt0.s`, yang memanggil syscall `exit` setelah `main` selesai.
- File sampai beberapa MB bisa disimpan di EXT2 (blok direct, indirect, dan doubly indirect). `tests/ext2_roundtrip.c` menulis, membaca ulang, dan menghapus file berbagai ukuran di komputer untuk membuktikan isinya identik dan tidak ada blok yang bocor.

---
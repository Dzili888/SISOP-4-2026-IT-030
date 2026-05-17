# Laporan Resmi Praktikum Sistem Operasi - Modul 4

## Disusun oleh
| No  | Nama                          | NRP        |
| --- | ----------------------------- | ---------- |
| 1   | Muhammad Syadzili Abdul Muhyi | 5027251030 |

## Soal 1 - Save Asisten Kenz

### Deskripsi Soal
Pada soal ini, Sebastian menemukan flashdisk milik Asisten Kenz yang berisi tujuh file catatan ekspedisi Mas Amba, yaitu `1.txt` sampai `7.txt`. Setiap file memiliki satu baris berawalan `KOORD:` yang merupakan potongan koordinat ritual. Tugas utama pada soal ini adalah membuat filesystem FUSE bernama `kenz_rescue.c` yang menerima dua argumen:

```bash
./kenz_rescue <source_directory> <mount_directory>
```

Filesystem yang dibuat harus menampilkan isi `source_directory` secara persis pada `mount_directory` dengan sifat passthrough untuk operasi `getattr`, `readdir`, `open`, dan `read`. Selain itu, filesystem juga harus menambahkan satu file virtual bernama `tujuan.txt` pada root mount. File ini tidak boleh benar-benar dibuat di source directory, tetapi harus muncul ketika user menjalankan `ls` pada mount directory.

Isi `tujuan.txt` dibangkitkan secara on-the-fly dengan menggabungkan seluruh fragmen `KOORD:` dari `1.txt` sampai `7.txt`, lalu ditulis dalam format:

```text
Tujuan Mas Amba: <gabungan_fragmen>
```

### Struktur File
```text
soal_1/
├── amba_files/
│   ├── 1.txt
│   ├── 2.txt
│   ├── 3.txt
│   ├── 4.txt
│   ├── 5.txt
│   ├── 6.txt
│   └── 7.txt
├── kenz_rescue.c
├── kenz_rescue
└── mnt/
    ├── 1.txt
    ├── 2.txt
    ├── 3.txt
    ├── 4.txt
    ├── 5.txt
    ├── 6.txt
    ├── 7.txt
    └── tujuan.txt
```

Keterangan:
- `amba_files` adalah source directory berisi tujuh file catatan asli.
- `mnt` adalah mount directory untuk filesystem FUSE.
- `kenz_rescue.c` adalah source code FUSE.
- `kenz_rescue` adalah binary hasil compile.
- `tujuan.txt` hanya muncul pada mount directory sebagai file virtual.

---

## Implementasi FUSE

### Inisialisasi Program
Program menggunakan FUSE versi 28 dan menyimpan source directory ke variabel global `dirpath`.

```c
#define FUSE_USE_VERSION 28
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#include <stdlib.h>

char dirpath[1000];
```

Pada fungsi `main`, program memastikan argumen minimal berjumlah tiga, yaitu nama program, source directory, dan mount directory. Path source disimpan dalam bentuk absolut menggunakan `realpath()`.

```c
int main(int argc, char *argv[])
{
    if (argc < 3) {
        printf("Cara pakai: %s <source_directory> <mount_directory>\n", argv[0]);
        return 1;
    }

    realpath(argv[1], dirpath);

    char *fuse_argv[] = {argv[0], argv[2]};

    umask(0);
    return fuse_main(2, fuse_argv, &xmp_oper, NULL);
}
```

Karena `fuse_main()` hanya membutuhkan argumen program dan mount point, argumen source directory dibuang dari `argv` FUSE dengan membuat array baru `fuse_argv`.

### Membuat Isi File Virtual `tujuan.txt`
File `tujuan.txt` tidak dibuat pada direktori asli. Isinya dibangkitkan saat file diakses dengan membaca file `1.txt` sampai `7.txt`, mencari baris yang diawali `KOORD:`, lalu menggabungkan fragmennya.

```c
void get_tujuan_content(char *buffer) {
    strcpy(buffer, "Tujuan Mas Amba: ");
    char temp_koord[1024] = "";

    for (int i = 1; i <= 7; i++) {
        char filepath[1024];
        sprintf(filepath, "%s/%d.txt", dirpath, i);
        FILE *file = fopen(filepath, "r");
        if (file) {
            char line[256];
            while (fgets(line, sizeof(line), file)) {
                if (strncmp(line, "KOORD: ", 7) == 0) {
                    line[strcspn(line, "\r\n")] = 0;
                    strcat(temp_koord, line + 7);
                    break;
                }
            }
            fclose(file);
        }
    }
    strcat(buffer, temp_koord);
    strcat(buffer, "\n");
}
```

`line + 7` digunakan agar prefix `KOORD: ` tidak ikut masuk ke hasil akhir. Dengan begitu, output `tujuan.txt` hanya berisi gabungan koordinatnya.

### Operasi `getattr`
Callback `xmp_getattr()` digunakan untuk mengambil metadata file. Untuk file biasa, path dari mount directory diterjemahkan ke path asli di `amba_files`, lalu metadata diambil menggunakan `lstat()`.

```c
static int xmp_getattr(const char *path, struct stat *stbuf)
{
    int res;
    char fpath[1000];

    if (strcmp(path, "/tujuan.txt") == 0) {
        memset(stbuf, 0, sizeof(struct stat));
        stbuf->st_mode = S_IFREG | 0444;
        stbuf->st_nlink = 1;

        char content[2048];
        get_tujuan_content(content);
        stbuf->st_size = strlen(content);
        return 0;
    }

    sprintf(fpath,"%s%s",dirpath,path);
    res = lstat(fpath, stbuf);

    if (res == -1) return -errno;

    return 0;
}
```

Khusus `/tujuan.txt`, metadata dibuat manual sebagai regular file read-only dengan permission `0444`. Ukuran file juga dihitung dari panjang string hasil `get_tujuan_content()`, sehingga output `stat` dan `wc -c` tetap konsisten.

### Operasi `readdir`
Callback `xmp_readdir()` digunakan ketika user melihat isi direktori, misalnya dengan `ls mnt/`. Program membaca isi direktori asli menggunakan `opendir()` dan `readdir()`, lalu memasukkan setiap entry ke buffer FUSE.

```c
static int xmp_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
    char fpath[1000];

    if(strcmp(path,"/") == 0)
    {
        sprintf(fpath,"%s",dirpath);
    } else sprintf(fpath, "%s%s",dirpath,path);

    int res = 0;
    DIR *dp;
    struct dirent *de;
    (void) offset;
    (void) fi;

    dp = opendir(fpath);
    if (dp == NULL) return -errno;

    while ((de = readdir(dp)) != NULL) {
        struct stat st;
        memset(&st, 0, sizeof(st));
        st.st_ino = de->d_ino;
        st.st_mode = de->d_type << 12;
        res = (filler(buf, de->d_name, &st, 0));

        if(res!=0) break;
    }
    closedir(dp);

    if (strcmp(path, "/") == 0) {
        filler(buf, "tujuan.txt", NULL, 0);
    }

    return 0;
}
```

Setelah semua entry asli dimasukkan, program menambahkan `tujuan.txt` hanya pada root mount. Karena penambahan ini dilakukan melalui `filler()`, file tersebut terlihat oleh user, tetapi tidak muncul di direktori `amba_files`.

### Operasi `open`
Callback `xmp_open()` memastikan file yang diakses memang dapat dibuka. Untuk `/tujuan.txt`, program langsung mengembalikan `0` karena file tersebut virtual.

```c
static int xmp_open(const char *path, struct fuse_file_info *fi)
{
    if (strcmp(path, "/tujuan.txt") == 0) return 0;

    char fpath[1000];
    sprintf(fpath, "%s%s", dirpath, path);

    int fd = open(fpath, fi->flags);
    if (fd == -1) return -errno;
    close(fd);
    return 0;
}
```

Untuk file lain, program membuka file asli di source directory dan mengembalikan error `-errno` jika gagal.

### Operasi `read`
Callback `xmp_read()` bertugas mengirim isi file ke user. Untuk file biasa, program membaca isi file source menggunakan `pread()` sehingga hasilnya byte-identical dengan file asli.

```c
static int xmp_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    if (strcmp(path, "/tujuan.txt") == 0) {
        char content[2048];
        get_tujuan_content(content);

        size_t len = strlen(content);
        if (offset < len) {
            if (offset + size > len) size = len - offset;
            memcpy(buf, content + offset, size);
        } else {
            size = 0;
        }
        return size;
    }

    char fpath[1000];
    if(strcmp(path,"/") == 0)
    {
        sprintf(fpath,"%s",dirpath);
    }
    else sprintf(fpath, "%s%s",dirpath,path);

    int res = 0;
    int fd = 0 ;

    (void) fi;

    fd = open(fpath, O_RDONLY);
    if (fd == -1) return -errno;

    res = pread(fd, buf, size, offset);
    if (res == -1) res = -errno;

    close(fd);
    return res;
}
```

Untuk `/tujuan.txt`, program tidak membaca dari disk, tetapi membuat isi file terlebih dahulu lewat `get_tujuan_content()`. Implementasi juga memperhatikan `offset` dan `size`, sehingga file virtual tetap dapat dibaca bertahap seperti file normal.

### Mapping Operasi FUSE
Empat callback utama dipasang ke dalam `struct fuse_operations`.

```c
static struct fuse_operations xmp_oper = {
    .getattr = xmp_getattr,
    .readdir = xmp_readdir,
    .open    = xmp_open,
    .read    = xmp_read,
};
```

Dengan mapping tersebut, filesystem dapat menampilkan direktori, membaca metadata, membuka file, dan membaca isi file sesuai kebutuhan soal.

---

## Cara Menjalankan

Compile program:

```bash
cd soal_1
gcc -Wall `pkg-config fuse --cflags` kenz_rescue.c -o kenz_rescue `pkg-config fuse --libs`
```

Jalankan FUSE:

```bash
mkdir -p mnt
./kenz_rescue amba_files mnt
```

Untuk menjalankan dalam foreground saat debugging:

```bash
./kenz_rescue -f amba_files mnt
```

Unmount filesystem:

```bash
fusermount -u mnt
```

atau:

```bash
sudo umount mnt
```

## Pengujian

### 1. Passthrough File Source ke Mount Directory
Pengujian dilakukan dengan membandingkan isi file pada mount directory dan source directory.

```bash
for i in 1 2 3 4 5 6 7; do
    diff mnt/$i.txt amba_files/$i.txt && echo "$i.txt OK"
done
```

Jika seluruh output menampilkan `OK`, maka operasi passthrough berhasil dan semua file pada mount directory memiliki isi yang sama persis dengan source directory.

### 2. File Virtual Muncul di Mount Directory
Ketika menjalankan:

```bash
ls mnt/
```

Mount directory menampilkan delapan entry:

```text
1.txt  2.txt  3.txt  4.txt  5.txt  6.txt  7.txt  tujuan.txt
```

Sedangkan source directory tetap hanya menampilkan tujuh file asli:

```bash
ls amba_files/
```

```text
1.txt  2.txt  3.txt  4.txt  5.txt  6.txt  7.txt
```

Hal ini membuktikan bahwa `tujuan.txt` benar-benar file virtual dan tidak dibuat di `amba_files`.

### 3. Isi `tujuan.txt`
Ketika command berikut dijalankan:

```bash
cat mnt/tujuan.txt
```

Program akan membaca fragmen `KOORD:` dari `1.txt` sampai `7.txt`, menggabungkannya sesuai urutan nama file, lalu menghasilkan:

```text
Tujuan Mas Amba: <gabungan_koordinat>
```

Ukuran file juga dapat dicek dengan:

```bash
stat mnt/tujuan.txt
wc -c mnt/tujuan.txt
```

Ukuran yang ditampilkan konsisten dengan isi string yang dibangkitkan oleh program, yaitu gabungan teks `Tujuan Mas Amba: `, seluruh fragmen koordinat, dan satu newline di akhir.

## Soal 2 - Poke MOO

### Deskripsi Soal
MOO sedang membuat mini database service yang dapat diakses melalui koneksi TCP pada port `9000`. Struktur penyimpanan database dibuat sederhana, yaitu folder merepresentasikan database dan file `.csv` merepresentasikan table. Program server bekerja pada direktori kerja `/app/db` saat dijalankan di dalam container.

Pada soal ini, sistem tidak hanya diminta menjalankan service database, tetapi juga mengamankan isi file menggunakan FUSE. Direktori `fuse_mount` berperan sebagai mount point yang terlihat normal oleh user, sedangkan data asli disimpan pada `encrypted_storage` dalam bentuk terenkripsi dengan ekstensi `.enc`. Dengan begitu, file yang dibaca melalui `fuse_mount` akan tampil sebagai plaintext, tetapi file yang tersimpan pada `encrypted_storage` tetap terenkripsi.

Selain FUSE, aplikasi juga dikontainerisasi menggunakan Docker. Image dibuat dari base image `ubuntu:latest`, program server disalin ke direktori `/app`, port `9000` diekspos, lalu container dijalankan dengan bind mount dari `fuse_mount` ke `/app/db`.

### Struktur File
```text
soal_2/
├── Dockerfile
├── client.c
├── fuse.c
├── server
├── encrypted_storage/
│   └── tests/
│       └── notes.csv.enc
└── fuse_mount/
```

Keterangan:
- `fuse.c` berisi implementasi filesystem FUSE dengan fitur enkripsi dan dekripsi XOR.
- `client.c` berisi client TCP untuk berinteraksi dengan server database pada port `9000`.
- `server` adalah binary mini database service yang dijalankan di dalam Docker container.
- `Dockerfile` digunakan untuk membuat image `soal-2-modul-4-sisop`.
- `encrypted_storage` adalah direktori penyimpanan asli dalam bentuk terenkripsi.
- `fuse_mount` adalah mount point yang menampilkan file dalam bentuk normal/decrypted.

---

## Implementasi FUSE

### Source Code (`fuse.c`)

Program FUSE diawali dengan konfigurasi versi FUSE dan deklarasi direktori dasar penyimpanan.

```c
#define FUSE_USE_VERSION 28
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/time.h>

static char base_dir[PATH_MAX];
static const unsigned char KEY = 0x76;
```

`base_dir` digunakan untuk menyimpan path absolut menuju direktori asli, yaitu `encrypted_storage`. Konstanta `KEY` bernilai `0x76` dipakai sebagai kunci algoritma XOR. Karena XOR bersifat simetris, fungsi yang sama dapat digunakan untuk proses enkripsi dan dekripsi.

```c
static void xor_transform(char *data, size_t len) {
    if (!data) return;
    for (size_t i = 0; i < len; i++) {
        data[i] ^= KEY;
    }
}
```

Fungsi `xor_transform()` melakukan operasi XOR pada setiap byte data. Saat file ditulis melalui mount point, isi file akan di-XOR terlebih dahulu sebelum disimpan ke `encrypted_storage`. Saat file dibaca, data dari file `.enc` akan di-XOR kembali sehingga tampil sebagai plaintext di `fuse_mount`.

```c
static int has_enc_suffix(const char *name) {
    size_t len = strlen(name);
    return len > 4 && strcmp(name + len - 4, ".enc") == 0;
}

static void strip_enc_suffix(char *name) {
    size_t len = strlen(name);
    if (len > 4 && strcmp(name + len - 4, ".enc") == 0) {
        name[len - 4] = '\0';
    }
}
```

Dua fungsi ini mengatur tampilan nama file. Di `encrypted_storage`, file asli disimpan dengan suffix `.enc`, misalnya `notes.csv.enc`. Namun saat dilihat melalui `fuse_mount`, suffix tersebut dihapus sehingga user hanya melihat `notes.csv`.

```c
static int build_actual_path(char *output, const char *logical_path) {
    char temp_path[PATH_MAX];
    struct stat info;

    if (strcmp(logical_path, "/") == 0) {
        return make_path(output, base_dir, NULL, NULL);
    } else {
        int res = make_path(temp_path, base_dir, logical_path, NULL);
        if (res != 0) return res;

        if (lstat(temp_path, &info) == 0 && S_ISDIR(info.st_mode)) {
            return make_path(output, temp_path, NULL, NULL);
        } else {
            return make_path(output, base_dir, logical_path, ".enc");
        }
    }
}
```

Fungsi `build_actual_path()` adalah translator utama antara path virtual dan path asli. Jika path yang diakses adalah direktori, maka path diarahkan langsung ke direktori di `encrypted_storage`. Jika path yang diakses adalah file, maka program otomatis menambahkan ekstensi `.enc`.

Contoh translasi:

```text
fuse_mount/tests/notes.csv
```

akan diarahkan ke:

```text
encrypted_storage/tests/notes.csv.enc
```

### Operasi Metadata dan Directory Listing

```c
static int lab_getattr(const char *path, struct stat *st) {
    char fpath[PATH_MAX];
    int res = build_actual_path(fpath, path);
    if (res != 0) return res;
    return lstat(fpath, st) == -1 ? -errno : 0;
}
```

`lab_getattr()` dipakai saat sistem meminta metadata file atau folder, seperti ukuran, permission, dan tipe file. Fungsi ini terlebih dahulu menerjemahkan path virtual ke path asli, lalu memanggil `lstat()`.

```c
static int lab_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                       off_t offset, struct fuse_file_info *fi) {
    ...
    while ((entry = readdir(dir)) != NULL) {
        ...
        if (!S_ISDIR(s.st_mode) && !has_enc_suffix(entry->d_name)) {
            continue;
        }

        char display_name[NAME_MAX + 1];
        snprintf(display_name, sizeof(display_name), "%s", entry->d_name);
        strip_enc_suffix(display_name);

        if (filler(buf, display_name, &s, 0)) break;
    }
    ...
}
```

`lab_readdir()` digunakan ketika user menjalankan command seperti `ls fuse_mount`. Fungsi ini hanya menampilkan direktori dan file yang memiliki ekstensi `.enc`. Setelah itu, ekstensi `.enc` dihapus dari nama tampilan agar file terlihat normal pada mount point.

### Operasi Read dan Write

```c
static int lab_read(const char *path, char *buf, size_t size,
                    off_t offset, struct fuse_file_info *fi) {
    ...
    ssize_t bytes = pread(fd, buf, size, offset);
    if (bytes >= 0) xor_transform(buf, bytes);
    ...
    return bytes;
}
```

`lab_read()` membuka file `.enc` dari `encrypted_storage`, membaca isinya menggunakan `pread()`, lalu melakukan XOR sebelum data dikirim ke user. Hasilnya, file terenkripsi tetap terbaca normal dari `fuse_mount`.

```c
static int lab_write(const char *path, const char *buf, size_t size,
                     off_t offset, struct fuse_file_info *fi) {
    ...
    char *secret_buffer = malloc(size);
    ...
    memcpy(secret_buffer, buf, size);
    xor_transform(secret_buffer, size);

    ssize_t written = pwrite(fd, secret_buffer, size, offset);
    ...
    return written;
}
```

`lab_write()` bekerja kebalikan dari `lab_read()`. Data plaintext yang ditulis user melalui `fuse_mount` disalin ke buffer sementara, dienkripsi dengan XOR, lalu disimpan ke file `.enc` di `encrypted_storage`.

### Operasi File System Lain

```c
static struct fuse_operations lab_oper = {
    .getattr  = lab_getattr,
    .readdir  = lab_readdir,
    .open     = lab_open,
    .create   = lab_create,
    .read     = lab_read,
    .write    = lab_write,
    .truncate = lab_truncate,
    .mkdir    = lab_mkdir,
    .rmdir    = lab_rmdir,
    .unlink   = lab_unlink,
    .access   = lab_access,
    .utimens  = lab_utimens,
};
```

Filesystem ini mengimplementasikan operasi umum seperti `mkdir`, `rmdir`, `create`, `unlink`, `truncate`, `access`, dan `utimens`. Dengan operasi tersebut, user dapat membuat folder, membuat file, menghapus file, menghapus folder, membaca metadata, dan memodifikasi file melalui `fuse_mount`.

```c
int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Gunakan: %s <source_dir> <mount_dir>\n", argv[0]);
        return 1;
    }

    if (!realpath(argv[1], base_dir)) {
        perror("realpath");
        return 1;
    }

    char *new_argv[] = { argv[0], argv[2] };

    umask(0);
    return fuse_main(2, new_argv, &lab_oper, NULL);
}
```

Program menerima dua argumen, yaitu `<source_dir>` dan `<mount_dir>`. `source_dir` adalah direktori penyimpanan terenkripsi, sedangkan `mount_dir` adalah direktori yang akan menjadi mount point FUSE.

---

## Implementasi Client

### Source Code (`client.c`)

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 9000
```

Client menggunakan socket TCP untuk terhubung ke server pada port `9000`. Library `<arpa/inet.h>` digunakan untuk konfigurasi alamat IPv4.

```c
if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    printf("\n Error membuat socket \n");
    return -1;
}
```

Bagian ini membuat socket dengan domain `AF_INET` dan tipe `SOCK_STREAM`, yaitu TCP socket.

```c
serv_addr.sin_family = AF_INET;
serv_addr.sin_port = htons(PORT);

if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
    printf("\nAlamat IP tidak valid/tidak didukung \n");
    return -1;
}
```

Client diarahkan ke alamat `127.0.0.1` karena container Docker akan memetakan port `9000` milik container ke port `9000` milik host.

```c
if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
    printf("\nKoneksi Gagal! Pastikan container Docker sudah berjalan.\n");
    return -1;
}
```

Jika koneksi gagal, kemungkinan container belum berjalan, port belum dipublish, atau server di dalam container tidak aktif.

```c
while (1) {
    printf("\n> ");
    fgets(input, sizeof(input), stdin);
    input[strcspn(input, "\n")] = 0;

    if (strcmp(input, "exit") == 0) {
        break;
    }

    send(sock, input, strlen(input), 0);
    memset(buffer, 0, sizeof(buffer));

    int valread = read(sock, buffer, 1024);
    if (valread > 0) {
        printf("%s\n", buffer);
    } else {
        printf("Server terputus.\n");
        break;
    }
}
```

Loop utama client membaca command dari terminal, mengirim command tersebut ke server, lalu menampilkan response dari server. Jika user mengetik `exit`, client akan keluar dari loop dan menutup koneksi.

---

## Implementasi Docker

### Source Code (`Dockerfile`)

```dockerfile
FROM ubuntu:latest
WORKDIR /app
COPY server /app/server
RUN chmod +x /app/server
EXPOSE 9000
CMD ["./server"]
```

Dockerfile menggunakan `ubuntu:latest` sebagai base image. Binary `server` disalin ke direktori `/app/server`, lalu diberi permission executable. Port `9000` diekspos karena service database menerima koneksi TCP pada port tersebut. Saat container berjalan, command default yang dijalankan adalah `./server`.

Pada saat container dijalankan, direktori `fuse_mount` dari host akan di-bind mount ke `/app/db`. Dengan konfigurasi tersebut, server database membaca dan menulis data ke `/app/db`, tetapi secara transparan data tersebut diproses oleh FUSE dan disimpan terenkripsi di `encrypted_storage`.

---

## Command untuk Menjalankan

### 1. Menyiapkan Direktori

```bash
mkdir -p encrypted_storage fuse_mount
```

Jika mount point sebelumnya masih terkunci atau muncul `Permission denied`, unmount terlebih dahulu:

```bash
fusermount -u fuse_mount
```

Jika masih gagal, gunakan:

```bash
sudo umount fuse_mount
```

### 2. Compile Program FUSE

```bash
gcc -Wall `pkg-config fuse --cflags` fuse.c -o fuse `pkg-config fuse --libs`
```

### 3. Menjalankan FUSE

```bash
./fuse encrypted_storage fuse_mount
```

Setelah FUSE berjalan, buka terminal lain untuk melakukan pengecekan.

### 4. Mengecek Isi Mount Point

```bash
ls fuse_mount
ls encrypted_storage
```

Jika terdapat file `encrypted_storage/tests/notes.csv.enc`, file tersebut akan tampil sebagai `fuse_mount/tests/notes.csv`.

```bash
ls fuse_mount/tests
cat fuse_mount/tests/notes.csv
```

### 5. Menguji Enkripsi Saat Write

```bash
mkdir -p fuse_mount/demo
echo "hello moo" > fuse_mount/demo/sample.csv
cat fuse_mount/demo/sample.csv
cat encrypted_storage/demo/sample.csv.enc
```

File pada `fuse_mount` akan terbaca normal, sedangkan file pada `encrypted_storage` tersimpan dalam bentuk terenkripsi.

### 6. Build Docker Image

```bash
docker build -t soal-2-modul-4-sisop .
```

### 7. Menjalankan Container Database

```bash
docker run -d \
  --name db_app \
  -p 9000:9000 \
  -v "$(pwd)/fuse_mount:/app/db" \
  soal-2-modul-4-sisop
```

Jika nama container sudah pernah dipakai, hapus container lama terlebih dahulu:

```bash
docker stop db_app
docker rm db_app
```

Lalu jalankan ulang command `docker run`.

### 8. Mengecek Container

```bash
docker ps
docker logs db_app
```

### 9. Compile Client

```bash
gcc client.c -o client
```

### 10. Menjalankan Client

```bash
./client
```

Contoh command yang dapat dikirim dari client:

```text
HELP
CREATE DATABASE tests
CREATE TABLE tests users id name
INSERT tests users 1 moo
SELECT tests users
LIST DATABASE
LIST TABLE tests
exit
```

### 11. Mengecek Hasil Data dari Host

```bash
tree fuse_mount
tree encrypted_storage
cat fuse_mount/tests/users.csv
cat encrypted_storage/tests/users.csv.enc
```

Data yang terlihat melalui `fuse_mount` akan terbaca normal, sedangkan data di `encrypted_storage` akan berbentuk terenkripsi dengan ekstensi `.enc`.

### 12. Menghentikan Program

Hentikan container:

```bash
docker stop db_app
docker rm db_app
```

Unmount FUSE:

```bash
fusermount -u fuse_mount
```

Jika diperlukan:

```bash
sudo umount fuse_mount
```

---

## Catatan Kendala

Jika Docker tidak bisa berjalan atau client gagal tersambung, beberapa hal yang perlu dicek adalah:

1. Pastikan FUSE sudah berjalan sebelum container dijalankan, karena container memakai bind mount dari `fuse_mount`.
2. Pastikan port `9000` tidak sedang dipakai proses lain.
3. Pastikan container dibuat dengan opsi `-p 9000:9000`.
4. Pastikan bind mount mengarah ke path absolut `$(pwd)/fuse_mount:/app/db`.
5. Jika `fuse_mount` menampilkan `Permission denied`, kemungkinan FUSE mount sebelumnya belum dilepas dengan benar. Jalankan `fusermount -u fuse_mount` atau `sudo umount fuse_mount`.

---

## Kesimpulan

Program pada soal ini berhasil menggabungkan konsep FUSE dan Docker. FUSE digunakan sebagai lapisan filesystem yang menerjemahkan file plaintext pada `fuse_mount` menjadi file terenkripsi `.enc` pada `encrypted_storage`. Docker digunakan untuk menjalankan mini database service secara terisolasi, sedangkan client TCP digunakan untuk mengirim command database ke server melalui port `9000`.

Dengan integrasi ini, service database tetap dapat bekerja seperti biasa menggunakan file CSV, tetapi data yang tersimpan di storage asli tetap terlindungi melalui proses enkripsi transparan dari FUSE.

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

char source_dir[1000];
const char XOR_KEY = 0x76;

// Helper: Untuk direktori, tidak pakai .enc
void get_dir_path(const char *path, char *fpath) {
    if (strcmp(path, "/") == 0) {
        sprintf(fpath, "%s", source_dir);
    } else {
        sprintf(fpath, "%s%s", source_dir, path);
    }
}

// Helper: Untuk file, selalu tambahkan .enc
void get_file_path(const char *path, char *fpath) {
    sprintf(fpath, "%s%s.enc", source_dir, path);
}

// Helper: Cek apakah path itu direktori (tanpa .enc) atau file (dengan .enc)
void get_generic_path(const char *path, char *fpath) {
    if (strcmp(path, "/") == 0) {
        sprintf(fpath, "%s", source_dir);
        return;
    }
    
    char temp[1024];
    sprintf(temp, "%s%s", source_dir, path);
    
    struct stat st;
    if (lstat(temp, &st) == 0 && S_ISDIR(st.st_mode)) {
        strcpy(fpath, temp); // Ini adalah direktori
    } else {
        sprintf(fpath, "%s%s.enc", source_dir, path); // Asumsikan ini file
    }
}

// Fungsi Enkripsi/Dekripsi XOR
void do_xor(char *buf, size_t size) {
    for (size_t i = 0; i < size; i++) {
        buf[i] ^= XOR_KEY;
    }
}

static int xmp_getattr(const char *path, struct stat *stbuf) {
    char fpath[1024];
    get_generic_path(path, fpath);
    int res = lstat(fpath, stbuf);
    if (res == -1) return -errno;
    return 0;
}

static int xmp_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
    char fpath[1024];
    get_dir_path(path, fpath);

    DIR *dp = opendir(fpath);
    if (dp == NULL) return -errno;

    struct dirent *de;
    (void) offset;
    (void) fi;

    while ((de = readdir(dp)) != NULL) {
        struct stat st;
        memset(&st, 0, sizeof(st));
        st.st_ino = de->d_ino;
        st.st_mode = de->d_type << 12;

        char name[256];
        strcpy(name, de->d_name);
        
        // Hapus ekstensi .enc agar di fuse_mount terlihat normal
        char *ext = strrchr(name, '.');
        if (ext != NULL && strcmp(ext, ".enc") == 0) {
            *ext = '\0';
        }

        if (filler(buf, name, &st, 0)) break;
    }
    closedir(dp);
    return 0;
}

static int xmp_mkdir(const char *path, mode_t mode) {
    char fpath[1024];
    get_dir_path(path, fpath);
    int res = mkdir(fpath, mode);
    if (res == -1) return -errno;
    return 0;
}

static int xmp_rmdir(const char *path) {
    char fpath[1024];
    get_dir_path(path, fpath);
    int res = rmdir(fpath);
    if (res == -1) return -errno;
    return 0;
}

static int xmp_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    char fpath[1024];
    get_file_path(path, fpath);
    int res = creat(fpath, mode);
    if (res == -1) return -errno;
    close(res);
    return 0;
}

static int xmp_open(const char *path, struct fuse_file_info *fi) {
    char fpath[1024];
    get_file_path(path, fpath);
    int res = open(fpath, fi->flags);
    if (res == -1) return -errno;
    close(res);
    return 0;
}

static int xmp_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    char fpath[1024];
    get_file_path(path, fpath);
    
    int fd = open(fpath, O_RDONLY);
    if (fd == -1) return -errno;

    int res = pread(fd, buf, size, offset);
    if (res == -1) res = -errno;
    else do_xor(buf, res); // DEKRIPSI saat dibaca di fuse_mount

    close(fd);
    return res;
}

static int xmp_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    char fpath[1024];
    get_file_path(path, fpath);
    
    int fd = open(fpath, O_WRONLY);
    if (fd == -1) return -errno;

    char *enc_buf = malloc(size);
    memcpy(enc_buf, buf, size);
    do_xor(enc_buf, size); // ENKRIPSI sebelum ditulis ke disk (encrypted_storage)

    int res = pwrite(fd, enc_buf, size, offset);
    if (res == -1) res = -errno;

    free(enc_buf);
    close(fd);
    return res;
}

static int xmp_truncate(const char *path, off_t size) {
    char fpath[1024];
    get_file_path(path, fpath);
    int res = truncate(fpath, size);
    if (res == -1) return -errno;
    return 0;
}

static int xmp_unlink(const char *path) {
    char fpath[1024];
    get_file_path(path, fpath);
    int res = unlink(fpath);
    if (res == -1) return -errno;
    return 0;
}

static int xmp_access(const char *path, int mask) {
    char fpath[1024];
    get_generic_path(path, fpath);
    int res = access(fpath, mask);
    if (res == -1) return -errno;
    return 0;
}

static int xmp_utimens(const char *path, const struct timespec ts[2]) {
    char fpath[1024];
    get_generic_path(path, fpath);
    int res = utimensat(0, fpath, ts, AT_SYMLINK_NOFOLLOW);
    if (res == -1) return -errno;
    return 0;
}

static struct fuse_operations xmp_oper = {
    .getattr  = xmp_getattr,
    .readdir  = xmp_readdir,
    .mkdir    = xmp_mkdir,
    .rmdir    = xmp_rmdir,
    .create   = xmp_create,
    .open     = xmp_open,
    .read     = xmp_read,
    .write    = xmp_write,
    .truncate = xmp_truncate,
    .unlink   = xmp_unlink,
    .access   = xmp_access,
    .utimens  = xmp_utimens,
};

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Cara pakai: %s <encrypted_storage> <fuse_mount>\n", argv[0]);
        return 1;
    }

    realpath(argv[1], source_dir);

    char *fuse_argv[] = {argv[0], argv[2]};

    umask(0);
    return fuse_main(2, fuse_argv, &xmp_oper, NULL);
}

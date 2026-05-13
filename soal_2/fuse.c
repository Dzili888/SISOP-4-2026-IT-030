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

/* Variabel untuk menyimpan base directory */
static char base_dir[PATH_MAX];
static const unsigned char KEY = 0x76;

/* Fungsi enkripsi/dekripsi XOR sesuai binari */
static void xor_transform(char *data, size_t len) {
    if (!data) return;
    for (size_t i = 0; i < len; i++) {
        data[i] ^= KEY;
    }
}

/* Fungsi untuk menentukan path asli di disk (encrypted_storage) */
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

static int make_path(char *output, const char *a, const char *b, const char *c) {
    size_t a_len = strlen(a);
    size_t b_len = b ? strlen(b) : 0;
    size_t c_len = c ? strlen(c) : 0;
    size_t total = a_len + b_len + c_len;

    if (total >= PATH_MAX) {
        return -ENAMETOOLONG;
    }

    memcpy(output, a, a_len);
    if (b_len > 0) memcpy(output + a_len, b, b_len);
    if (c_len > 0) memcpy(output + a_len + b_len, c, c_len);
    output[total] = '\0';
    return 0;
}

/* Fungsi untuk menentukan path asli di disk (encrypted_storage) */
static int build_actual_path(char *output, const char *logical_path) {
    char temp_path[PATH_MAX];
    struct stat info;

    if (strcmp(logical_path, "/") == 0) {
        return make_path(output, base_dir, NULL, NULL);
    } else {
        int res = make_path(temp_path, base_dir, logical_path, NULL);
        if (res != 0) return res;
        
        /* Jika itu direktori, akses langsung tanpa .enc */
        if (lstat(temp_path, &info) == 0 && S_ISDIR(info.st_mode)) {
            return make_path(output, temp_path, NULL, NULL);
        } else {
            /* Jika file, arahkan ke file .enc */
            return make_path(output, base_dir, logical_path, ".enc");
        }
    }
}

static int lab_getattr(const char *path, struct stat *st) {
    char fpath[PATH_MAX];
    int res = build_actual_path(fpath, path);
    if (res != 0) return res;
    return lstat(fpath, st) == -1 ? -errno : 0;
}

static int lab_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
    (void) offset;
    (void) fi;

    char fpath[PATH_MAX];
    int res = build_actual_path(fpath, path);
    if (res != 0) return res;

    DIR *dir = opendir(fpath);
    if (!dir) return -errno;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        struct stat s;
        memset(&s, 0, sizeof(s));
        s.st_ino = entry->d_ino;
        s.st_mode = entry->d_type << 12;

        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            char entry_path[PATH_MAX];
            if (make_path(entry_path, fpath, "/", entry->d_name) != 0) {
                continue;
            }

            if (lstat(entry_path, &s) == -1) {
                continue;
            }

            if (!S_ISDIR(s.st_mode) && !has_enc_suffix(entry->d_name)) {
                continue;
            }
        }

        char display_name[NAME_MAX + 1];
        snprintf(display_name, sizeof(display_name), "%s", entry->d_name);
        strip_enc_suffix(display_name);

        if (filler(buf, display_name, &s, 0)) break;
    }
    closedir(dir);
    return 0;
}

static int lab_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void) fi;

    char fpath[PATH_MAX];
    int res = build_actual_path(fpath, path);
    if (res != 0) return res;
    
    int fd = open(fpath, O_RDONLY);
    if (fd == -1) return -errno;

    ssize_t bytes = pread(fd, buf, size, offset);
    if (bytes >= 0) xor_transform(buf, bytes);
    else bytes = -errno;

    close(fd);
    return bytes;
}

static int lab_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void) fi;

    char fpath[PATH_MAX];
    int res = build_actual_path(fpath, path);
    if (res != 0) return res;
    
    int fd = open(fpath, O_WRONLY | O_CREAT, 0644);
    if (fd == -1) return -errno;

    if (size == 0) {
        close(fd);
        return 0;
    }

    char *secret_buffer = malloc(size);
    if (!secret_buffer) {
        close(fd);
        return -ENOMEM;
    }
    memcpy(secret_buffer, buf, size);
    xor_transform(secret_buffer, size);

    ssize_t written = pwrite(fd, secret_buffer, size, offset);
    if (written == -1) written = -errno;

    free(secret_buffer);
    close(fd);
    return written;
}

static int lab_mkdir(const char *path, mode_t mode) {
    char fpath[PATH_MAX];
    int res = make_path(fpath, base_dir, path, NULL);
    if (res != 0) return res;
    return mkdir(fpath, mode) == -1 ? -errno : 0;
}

static int lab_unlink(const char *path) {
    char fpath[PATH_MAX];
    int res = build_actual_path(fpath, path);
    if (res != 0) return res;
    return unlink(fpath) == -1 ? -errno : 0;
}

static int lab_open(const char *path, struct fuse_file_info *fi) {
    char fpath[PATH_MAX];
    int res = build_actual_path(fpath, path);
    if (res != 0) return res;

    int fd = open(fpath, fi->flags);
    if (fd == -1) return -errno;
    close(fd);
    return 0;
}

static int lab_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    (void) fi;

    char fpath[PATH_MAX];
    int res = build_actual_path(fpath, path);
    if (res != 0) return res;

    int fd = open(fpath, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (fd == -1) return -errno;
    close(fd);
    return 0;
}

static int lab_truncate(const char *path, off_t size) {
    char fpath[PATH_MAX];
    int res = build_actual_path(fpath, path);
    int saved_errno = 0;
    if (res != 0) return res;

    struct stat st;
    if (lstat(fpath, &st) == -1) return -errno;
    if (S_ISDIR(st.st_mode)) return -EISDIR;

    int fd = open(fpath, O_WRONLY);
    if (fd == -1) return -errno;

    res = 0;
    if (size <= st.st_size) {
        res = ftruncate(fd, size);
        if (res == -1) saved_errno = errno;
    } else {
        unsigned char pad[4096];
        memset(pad, KEY, sizeof(pad));

        off_t written_total = st.st_size;
        while (written_total < size) {
            size_t chunk = size - written_total;
            if (chunk > sizeof(pad)) chunk = sizeof(pad);

            ssize_t written = pwrite(fd, pad, chunk, written_total);
            if (written <= 0) {
                res = -1;
                saved_errno = written == -1 ? errno : EIO;
                break;
            }
            written_total += written;
        }
    }

    close(fd);
    return res == -1 ? -saved_errno : 0;
}

static int lab_rmdir(const char *path) {
    char fpath[PATH_MAX];
    int res = make_path(fpath, base_dir, path, NULL);
    if (res != 0) return res;
    return rmdir(fpath) == -1 ? -errno : 0;
}

static int lab_utimens(const char *path, const struct timespec ts[2]) {
    char fpath[PATH_MAX];
    int res = build_actual_path(fpath, path);
    if (res != 0) return res;
    return utimensat(AT_FDCWD, fpath, ts, AT_SYMLINK_NOFOLLOW) == -1 ? -errno : 0;
}

static int lab_access(const char *path, int mask) {
    char fpath[PATH_MAX];
    int res = build_actual_path(fpath, path);
    if (res != 0) return res;
    return access(fpath, mask) == -1 ? -errno : 0;
}

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

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Gunakan: %s <source_dir> <mount_dir>\n", argv[0]);
        return 1;
    }

    if (!realpath(argv[1], base_dir)) {
        perror("realpath");
        return 1;
    }
    
    /* Gunakan argumen hanya untuk mountpoint */
    char *new_argv[] = { argv[0], argv[2] };
    
    umask(0);
    return fuse_main(2, new_argv, &lab_oper, NULL);
}

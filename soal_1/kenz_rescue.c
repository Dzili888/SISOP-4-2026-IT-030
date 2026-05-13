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

//sumtxt dari 1.txt s/d 7.txt on-the-fly
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
                    line[strcspn(line, "\r\n")] = 0; // del nw line
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

//dianuanuin dlu kali, liat modul ae
static int xmp_getattr(const char *path, struct stat *stbuf)
{
    int res;
    char fpath[1000];

    // Intersep file virtual tujuan.txt
    if (strcmp(path, "/tujuan.txt") == 0) {
        memset(stbuf, 0, sizeof(struct stat));
        stbuf->st_mode = S_IFREG | 0444; // File reguler, Read-Only
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

    // Sisipkan file virtual ke directory root FUSE
    if (strcmp(path, "/") == 0) {
        filler(buf, "tujuan.txt", NULL, 0);
    }

    return 0;
}

// Tambahan xmp_open buat throughtpass
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

static int xmp_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    // Intersep read untuk membaca file virtual
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

// Mapping kek di modul
static struct fuse_operations xmp_oper = {
    .getattr = xmp_getattr,
    .readdir = xmp_readdir,
    .open    = xmp_open,
    .read    = xmp_read,
};

int main(int argc, char *argv[])
{
    if (argc < 3) {
        printf("Cara pakai: %s <source_directory> <mount_directory>\n", argv[0]);
        return 1;
    }

    // Menyimpan path sumber ke variabel global (dirpath) untuk dipakai callback
    realpath(argv[1], dirpath);

    // Membuang argv[1] agar fuse_main hanya menerima "./nama_program" dan "mount_point"
    char *fuse_argv[] = {argv[0], argv[2]};

    umask(0);
    return fuse_main(2, fuse_argv, &xmp_oper, NULL);
}

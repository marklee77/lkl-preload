#define _GNU_SOURCE

#include <stdio.h>
#include <time.h>
#include <argp.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <libgen.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <fnmatch.h>
#undef st_atime
#undef st_mtime
#undef st_ctime
#include <dirent.h>
#include <lkl.h>
#include <lkl_host.h>

#include <dlfcn.h>

void printk(const char *str, int len) {
    int ret __attribute__((unused));
    ret = write(STDOUT_FILENO, str, len);
}

union lkl_disk_backstore bs;
char mpoint[32];

int __libc_start_main(int (*main)(int,char **,char **), int argc, char **argv,
                      void (*init)(void), void (*fini)(void),
                      void (*rtld_fini)(void), void *stack_end) {
    int (*orig_libc_start_main)(int (*)(int,char **,char **), int, char **,
                                    void (*)(void), void (*)(void),
                                    void (*)(void), void *);

    int (*orig_open)(const char *, int, ...);
    unsigned int disk_id;
    long ret;

    fprintf(stderr, "PRELOAD: __libc_start_main\n");

    orig_open = dlsym(RTLD_NEXT, "open");

    bs.fd = orig_open("./disk.img", O_RDONLY);
    if (bs.fd < 0) {
        fprintf(stderr, "can't open fsimg %s: %s\n", "./hello.img",
            strerror(errno));
        exit(1);
    }

    ret = lkl_disk_add(bs);
    if (ret < 0) {
        fprintf(stderr, "can't add disk: %s\n", lkl_strerror(ret));
        close(bs.fd);
        lkl_sys_halt();
        exit(ret);
    }
    disk_id = ret;

    lkl_host_ops.print = NULL;
    lkl_start_kernel(&lkl_host_ops, 100 * 1024 * 1024, "");

    ret = lkl_mount_dev(disk_id, "ext2", LKL_MS_RDONLY, NULL,
                        mpoint, sizeof(mpoint));
    if (ret) {
        fprintf(stderr, "can't mount disk: %s\n", lkl_strerror(ret));
        close(bs.fd);
        lkl_sys_halt();
        exit(ret);
    }

    orig_libc_start_main = dlsym(RTLD_NEXT, "__libc_start_main");
    return orig_libc_start_main(main, argc, argv, init, fini, rtld_fini, stack_end);
}

void exit(int status) {
    void (*orig_exit)(int) __attribute__((noreturn));
    fprintf(stderr, "PRELOAD: exit\n");
    orig_exit = dlsym(RTLD_NEXT, "exit");
    close(bs.fd);
    lkl_sys_halt();
    orig_exit(status);
}

int open(const char *path, int flags, ...) {
    char fullpath[PATH_MAX];
    int lkl_flags = 0;

    if (!fnmatch("/dev/*", path, FNM_PATHNAME) ||
        !fnmatch("/sys/*", path, FNM_PATHNAME)) {
        snprintf(fullpath, sizeof(fullpath), "%s", path);
    } else {
        while('/' == *path) path++;
        snprintf(fullpath, sizeof(fullpath), "%s/%s", mpoint, path);
    }
    
    lkl_flags |= (flags & O_RDONLY) ? LKL_O_RDONLY : 0;
    lkl_flags |= (flags & O_WRONLY) ? LKL_O_WRONLY : 0;
    lkl_flags |= (flags & O_RDWR) ? LKL_O_RDWR : 0;
    lkl_flags |= (flags & O_CREAT) ? LKL_O_CREAT : 0;
    lkl_flags |= (flags & O_EXCL) ? LKL_O_EXCL : 0;
    lkl_flags |= (flags & O_NOCTTY) ? LKL_O_NOCTTY : 0;
    lkl_flags |= (flags & O_TRUNC) ? LKL_O_TRUNC : 0;
    lkl_flags |= (flags & O_APPEND) ? LKL_O_APPEND : 0;
    lkl_flags |= (flags & O_NONBLOCK) ? LKL_O_NONBLOCK : 0;
    lkl_flags |= (flags & O_DSYNC) ? LKL_O_DSYNC : 0;
    lkl_flags |= (flags & FASYNC) ? LKL_FASYNC : 0;
    lkl_flags |= (flags & O_DIRECT) ? LKL_O_DIRECT : 0;
    lkl_flags |= (flags & O_LARGEFILE) ? LKL_O_LARGEFILE : 0;
    lkl_flags |= (flags & O_DIRECTORY) ? LKL_O_DIRECTORY : 0;
    lkl_flags |= (flags & O_NOFOLLOW) ? LKL_O_NOFOLLOW : 0;
    lkl_flags |= (flags & O_NOATIME) ? LKL_O_NOATIME : 0;
    lkl_flags |= (flags & O_CLOEXEC) ? LKL_O_CLOEXEC : 0;

    fprintf(stderr, "PRELOAD: open %s flags: 0%07o/0%07o\n", fullpath, flags, lkl_flags);

    return lkl_sys_open(fullpath, LKL_O_RDONLY, 0);
}

int close(int fd) {
    fprintf(stderr, "PRELOAD: close\n");
    return lkl_sys_close(fd);
}

int __fxstat64(int ver, int fd, struct stat64 *buf) {
    struct lkl_stat lkl_stat;
    int ret;
    printf("PRELOAD: __fxstat64\n");
    ret = lkl_sys_fstat(fd, &lkl_stat);
    buf->st_dev = lkl_stat.st_dev;
    buf->st_ino = lkl_stat.st_ino;
    buf->st_mode = lkl_stat.st_mode;
    buf->st_nlink = lkl_stat.st_nlink;
    buf->st_uid = lkl_stat.st_uid;
    buf->st_gid = lkl_stat.st_gid;
    buf->st_rdev = lkl_stat.st_rdev;
    buf->st_size = lkl_stat.st_size;
    buf->st_blksize = lkl_stat.st_blksize;
    buf->st_blocks = lkl_stat.st_blocks;
    buf->st_atim.tv_sec = lkl_stat.st_atime;;
    buf->st_atim.tv_nsec = lkl_stat.st_atime_nsec;
    buf->st_mtim.tv_sec = lkl_stat.st_mtime;
    buf->st_mtim.tv_nsec = lkl_stat.st_mtime_nsec;
    buf->st_ctim.tv_sec = lkl_stat.st_ctime;
    buf->st_ctim.tv_nsec = lkl_stat.st_ctime_nsec;
    return ret;
}

ssize_t read(int fd, void *buf, size_t count) {
    fprintf(stderr, "PRELOAD: read\n");
    return lkl_sys_read(fd, buf, count);
}

ssize_t pread64(int fd, void *buf, size_t count, off_t offset) {
    fprintf(stderr, "PRELOAD: pread64\n");    
    return lkl_sys_pread64(fd, buf, count, offset);
}

// readdir64, readv

// opendir, closedir, readdir64, mkdir, chdir 

// write, writev, pwrite64


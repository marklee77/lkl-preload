// FIXME: debug environment variable, also use libseccomp to capture system
// calls
#define _GNU_SOURCE

#include <stdio.h>
#include <time.h>
#include <argp.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
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


// globals
union lkl_disk_backstore bs;
unsigned int disk_id;
char mpoint[32];
int (*global_main)(int, char**, char**);


// FIXME: do not export
void printk(const char *str, int len) {
    int ret __attribute__((unused));
    if (NULL != getenv("LKL_PRELOAD_VERBOSE")) {
        ret = write(STDERR_FILENO, str, len);
    }
}


void lkl_preload_init() {
    char *disk = getenv("LKL_PRELOAD_DISK");
    int (*orig_open)(const char *, int, ...) = dlsym(RTLD_NEXT, "open");
    long ret;

    fprintf(stderr, "PRELOAD: lkl_preload_init \n");

    if (NULL == disk) {
        disk = "./disk.img";
    }

    bs.fd = orig_open(disk, O_RDWR);
    if (bs.fd < 0) {
        fprintf(stderr, "can't open disk %s: %s\n", disk, strerror(errno));
        exit(1);
    }

    ret = lkl_disk_add(bs);
    if (ret < 0) {
        fprintf(stderr, "can't add disk: %s\n", lkl_strerror(ret));
        close(bs.fd);
        exit(1);
    }
    disk_id = ret;

    lkl_host_ops.print = printk;
    lkl_start_kernel(&lkl_host_ops, 100 * 1024 * 1024, "");

    lkl_sys_mknod("/dev/null", 0666, makedev(1,3));

    ret = lkl_mount_dev(disk_id, "ext2", 0, NULL, mpoint, sizeof(mpoint));

    if (ret) {
        fprintf(stderr, "can't mount disk: %s\n", lkl_strerror(ret));
        close(bs.fd);
        lkl_sys_halt();
        exit(ret);
    }
}


void lkl_preload_cleanup() {
    int (*orig_close)(int) = dlsym(RTLD_NEXT, "close");
    fprintf(stderr, "PRELOAD: lkl_preload_cleanup \n");

    lkl_umount_dev(disk_id, 0, 1000);
    orig_close(bs.fd);
    lkl_sys_halt();
}


int main_wrapper(int argc, char ** argv, char **envp) {
    int ret;

    fprintf(stderr, "PRELOAD: main_wrapper\n");

    lkl_preload_init();
    ret = (*global_main)(argc, argv, envp);
    lkl_preload_cleanup();

    return ret;
}


void exit(int status) {
    void (*orig_exit)(int) __attribute__((noreturn)) = dlsym(RTLD_NEXT, "exit");
    fprintf(stderr, "PRELOAD: exit\n");
    lkl_preload_cleanup();
    orig_exit(status);
}


int __libc_start_main(int (*main)(int,char **,char **), int argc, char **argv,
                      void (*init)(void), void (*fini)(void),
                      void (*rtld_fini)(void), void *stack_end) {

    int (*orig_libc_start_main)(int (*)(int,char **,char **), int, char **,
                                void (*)(void), void (*)(void),
                                void (*)(void), void *) = 
            dlsym(RTLD_NEXT, "__libc_start_main");

    global_main = main;

    fprintf(stderr, "PRELOAD: __libc_start_main\n");
    return orig_libc_start_main(main_wrapper, argc, argv, init, 
                                fini, rtld_fini, stack_end);
}




int open(const char *path, int flags, ...) {
    char fullpath[PATH_MAX];
    int lkl_flags = 0;
    
    va_list args;
    mode_t mode = 0;

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

    if (flags & O_CREAT) {
        va_start(args, flags);
        mode = va_arg(args, mode_t);
        va_end(args);
    }

    fprintf(stderr, "PRELOAD: open %s flags: 0%07o/0%07o\n", fullpath, flags, lkl_flags);

    return lkl_sys_open(fullpath, lkl_flags, mode);
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
    fprintf(stderr, "PRELOAD: pread64 %d, %zd, %zd\n", fd, count, offset);    
    return lkl_sys_pread64(fd, buf, count, offset);
}


ssize_t pwrite64(int fd, const void *buf, size_t count, off_t offset) {
    ssize_t ret = lkl_sys_pwrite64(fd, buf, count, offset);

    if (0 > ret) {
        errno = -ret;
        ret = -1;
    }

    fprintf(stderr, "PRELOAD: pwrite64 %d, %zd, %zd, %zd, %d\n", fd, count, offset, ret, errno);    
    return ret;
}

/*
off_t lseek(int fd, off_t offset, int whence) {

}

DIR *opendir(const char *name) {
    return NULL;
}

int closedir(DIR *dirp) {
    return -1;
}

int readdir64(DIR *dirp, struct dirent *entry, struct dirent **result) {

}

int mkdir(const char *pathname, mode_t mode) {

}

int chdir(const char *path) {

}
*/

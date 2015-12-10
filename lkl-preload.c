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


#define DEBUGMSG if (getenv("LKL_PRELOAD_DEBUG")) fprintf


// globals
union lkl_disk_backstore bs __attribute__ ((visibility ("hidden")));
unsigned int disk_id __attribute__ ((visibility ("hidden")));
char mpoint[32] __attribute__ ((visibility ("hidden")));
int (*global_main)(int, char**, char**) __attribute__ ((visibility ("hidden")));


void printk(const char *, int) __attribute__ ((visibility ("hidden")));

void printk(const char *str, int len) {

    int ret __attribute__((unused));

    if (NULL != getenv("LKL_PRELOAD_VERBOSE")) {
        ret = write(STDERR_FILENO, str, len);
    }

}


void lkl_preload_init() __attribute__ ((visibility ("hidden")));

void lkl_preload_init() {

    char *disk = getenv("LKL_PRELOAD_DISK");
    int (*orig_open)(const char *, int, ...) = dlsym(RTLD_NEXT, "open");
    long ret;

    DEBUGMSG(stderr, "PRELOAD: lkl_preload_init \n");

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


void lkl_preload_cleanup() __attribute__ ((visibility ("hidden")));

void lkl_preload_cleanup() {

    int (*orig_close)(int) = dlsym(RTLD_NEXT, "close");

    DEBUGMSG(stderr, "PRELOAD: lkl_preload_cleanup \n");

    lkl_umount_dev(disk_id, 0, 1000);
    orig_close(bs.fd);
    lkl_sys_halt();

}


int main_wrapper(int, char **, char **) __attribute__ ((visibility ("hidden")));

int main_wrapper(int argc, char **argv, char **envp) {

    int ret;

    DEBUGMSG(stderr, "PRELOAD: main_wrapper\n");

    lkl_preload_init();
    ret = (*global_main)(argc, argv, envp);
    lkl_preload_cleanup();

    return ret;
}


void exit(int status) {

    void (*orig_exit)(int) __attribute__((noreturn)) = dlsym(RTLD_NEXT, "exit");

    DEBUGMSG(stderr, "PRELOAD: exit %d\n", status);

    lkl_preload_cleanup();
    orig_exit(status);

}


int __libc_start_main(int (*main)(int,char **,char **), int argc, char **argv,
                      void (*init)(void), void (*fini)(void),
                      void (*rtld_fini)(void), void *stack_end) {

    int (*orig_libc_start_main)(
            int (*)(int,char **,char **), 
            int, 
            char **, 
            void (*)(void), 
            void (*)(void), 
            void (*)(void), 
            void *
        ) = dlsym(RTLD_NEXT, "__libc_start_main");

    DEBUGMSG(stderr, "PRELOAD: __libc_start_main\n");

    global_main = main;

    return orig_libc_start_main(main_wrapper, argc, argv, 
            init, fini, rtld_fini, stack_end);

}


// FIXME: realpath wants getcwd...
// maybe it just makes more sense to mount rootfs?
void lkl_preload_remap_path(const char *, char *) __attribute__ ((visibility ("hidden")));

void lkl_preload_remap_path(const char *path, char *remapped_path) {
    
    char resolved_path[PATH_MAX];
    char * ret __attribute__((unused));

    ret = realpath(path, resolved_path);

    // FIXME: deliberately just using path here...
    strcpy(resolved_path, path);

    if (!fnmatch("/dev/*", resolved_path, FNM_PATHNAME) ||
        !fnmatch("/sys/*", resolved_path, FNM_PATHNAME)) {
        snprintf(remapped_path, sizeof(char) * PATH_MAX, "%s", resolved_path);
    } else {
        int i = 0;
        while ('/' == resolved_path[i]) i++;
        snprintf(remapped_path, sizeof(char) * PATH_MAX, "%s/%s", mpoint, resolved_path+i);
    }

    DEBUGMSG(stderr, "PRELOAD: lkl_preload_remap_path %s %s\n", path, remapped_path);
 
}


int open(const char *path, int flags, ...) {

    char remapped_path[PATH_MAX];
    int lkl_flags = 0;
    
    va_list args;
    mode_t mode = 0;

    DEBUGMSG(stderr, "PRELOAD: open %s\n", path);

    lkl_preload_remap_path(path, remapped_path);
 
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

    return lkl_sys_open(remapped_path, lkl_flags, mode);

}


int close(int fd) {

    DEBUGMSG(stderr, "PRELOAD: close\n");

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

    DEBUGMSG(stderr, "PRELOAD: read\n");

    return lkl_sys_read(fd, buf, count);

}


ssize_t pread64(int fd, void *buf, size_t count, off_t offset) {

    DEBUGMSG(stderr, "PRELOAD: pread64\n");    

    return lkl_sys_pread64(fd, buf, count, offset);

}


ssize_t pwrite64(int fd, const void *buf, size_t count, off_t offset) {

    ssize_t ret = lkl_sys_pwrite64(fd, buf, count, offset);

    DEBUGMSG(stderr, "PRELOAD: pwrite64\n");    

    // where is this documented?
    if (0 > ret) {
        errno = -ret;
        ret = -1;
    }

    return ret;

}


/*
off64_t lseek64(int fd, off64_t offset, int whence) {

    int lkl_whence = LKL_SEEK_CUR;

    DEBUGMSG(stderr, "PRELOAD: lseek64\n");    

    switch(whence) {
        case SEEK_SET:
            lkl_whence = LKL_SEEK_SET;
            break;
        case SEEK_CUR:
            lkl_whence = LKL_SEEK_CUR;
            break;
        case SEEK_END:
            lkl_whence = LKL_SEEK_END;
            break;
    }

    return lkl_sys_lseek(fd, offset, lkl_whence);
    
}
*/


DIR *opendir(const char *path) {
    
    char remapped_path[PATH_MAX];
    DIR *dir;
    int err;

    DEBUGMSG(stderr, "PRELOAD: opendir %s\n", path);

    lkl_preload_remap_path(path, remapped_path);

    dir = (DIR *)lkl_opendir(remapped_path, &err);
    if (NULL == dir) {
        errno = err;
    }

    return dir;

}


int closedir(DIR *dir) {

    DEBUGMSG(stderr, "PRELOAD: closedir\n");    

    // FIXME: check for return -errno as in pwrite
    return lkl_closedir((struct lkl_dir *)dir);

}


struct dirent64 *readdir64(DIR *dirp) {

    DEBUGMSG(stderr, "PRELOAD: readdir64\n");    

    return (struct dirent64 *)lkl_readdir((struct lkl_dir *)dirp);

}


int mkdir(const char *path, mode_t mode) {

    char remapped_path[PATH_MAX];

    DEBUGMSG(stderr, "PRELOAD: mkdir %s\n", path);

    lkl_preload_remap_path(path, remapped_path);
    
    return lkl_sys_mkdir(remapped_path, mode);
}

/*
char *getwd(char *buf, ...) {

}

int chdir(const char *path) {

    char remapped_path[PATH_MAX];

    DEBUGMSG(stderr, "PRELOAD: chdir %s\n", path);

    lkl_preload_remap_path(path, remapped_path);

    return lkl_sys_chdir(remapped_path);

}
*/

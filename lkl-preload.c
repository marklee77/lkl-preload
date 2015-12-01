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

void myprintk(const char *str, int len)
{
    int ret __attribute__((unused));
    ret = write(STDOUT_FILENO, str, len);
}

union lkl_disk_backstore bs;
char mpoint[32];

int __libc_start_main(int (*main)(int,char **,char **), int argc, char **argv,
                      void (*init)(void), void (*fini)(void),
                      void (*rtld_fini)(void), void *stack_end) {
    int (*original_libc_start_main)(int (*)(int,char **,char **), int, char **,
                                    void (*)(void), void (*)(void),
                                    void (*)(void), void *);

    int (*original_open)(const char *, int);
    unsigned int disk_id;
    long ret;

    original_open = dlsym(RTLD_NEXT, "open");

    bs.fd = original_open("./disk.img", O_RDONLY);
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

    lkl_host_ops.print = myprintk;
    lkl_start_kernel(&lkl_host_ops, 100 * 1024 * 1024, "");

    ret = lkl_mount_dev(disk_id, "ext2", LKL_MS_RDONLY, NULL,
                        mpoint, sizeof(mpoint));
    if (ret) {
        fprintf(stderr, "can't mount disk: %s\n", lkl_strerror(ret));
        close(bs.fd);
        lkl_sys_halt();
        exit(ret);
    }

    original_libc_start_main = dlsym(RTLD_NEXT, "__libc_start_main");
    return original_libc_start_main(main, argc, argv, init, fini, rtld_fini, stack_end);
}

void _exit(int status) {
    void (*original_exit)(int);
    original_exit = dlsym(RTLD_NEXT, "_exit");
    close(bs.fd);
    lkl_sys_halt();
    original_exit(status);
}

int open(const char *subpath, int flags, ...) {
    char path[PATH_MAX];

    snprintf(path, sizeof(path), "%s/%s", mpoint, subpath);

    return lkl_sys_open(path, LKL_O_RDONLY, 0);
}

ssize_t read(int fd, void *buf, size_t count) {
    return lkl_sys_read(fd, buf, count);
}

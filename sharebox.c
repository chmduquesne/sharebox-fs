/*
 * sharebox
 */

#define SHAREBOX_VERSION "0.0.1"
#define FUSE_USE_VERSION 26

#ifdef linux
/* For pread()/pwrite() */
#define _XOPEN_SOURCE 500
#endif

#include <fuse.h>
#include <fuse_opt.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#include <glib.h>

#include "git-annex.h"

/*
 * FS attributes
 */

struct sharebox
{
    pthread_mutex_t rwlock;
    const char *reporoot;
    const char *mountpoint;
    bool deep_replicate;
    const char *write_callback;
    GHashTable *opened_copies;
};

static struct sharebox sharebox;

/*
 * Options parsing
 */

#define SHAREBOX_OPT(t, p, v) { t, offsetof(struct sharebox, p), v }

enum {
    KEY_HELP,
    KEY_VERSION,
};

static struct fuse_opt sharebox_opts[] = {
    SHAREBOX_OPT("deep_replicate",      deep_replicate, false),
    SHAREBOX_OPT("write_callback=%s",   write_callback, 0),
    FUSE_OPT_KEY("-V",                  KEY_VERSION),
    FUSE_OPT_KEY("--version",           KEY_VERSION),
    FUSE_OPT_KEY("-h",                  KEY_HELP),
    FUSE_OPT_KEY("--help",              KEY_HELP),
    FUSE_OPT_END
};


static void sharebox_path(char spath[PATH_MAX], const char *path)
{
    strcpy(spath, sharebox.reporoot);
    strncat(spath, "/files", PATH_MAX);
    strncat(spath, path, PATH_MAX);
}

/*
 * FS operations
 */

static int sharebox_getattr(const char *path, struct stat *stbuf)
{
    int res;

    char spath[PATH_MAX];
    sharebox_path(spath, path);

    res = lstat(spath, stbuf);
    if (annexed(sharebox.reporoot, spath)) {
        /* we set the file type mask to 0 */
        stbuf->st_mode &= ~S_IFMT;
        /* we then force regular file */
        stbuf->st_mode |= S_IFREG;
        /* we also force writable */
        stbuf->st_mode |= S_IWUSR;
    }
    if (res == -1)
        return -errno;

    return 0;
}

static int sharebox_access(const char *path, int mask)
{
    int res;

    char spath[PATH_MAX];
    sharebox_path(spath, path);

    res = access(spath, mask);
    if (res == -1)
        return -errno;

    return 0;
}

static int sharebox_readlink(const char *path, char *buf, size_t size)
{
    int res;

    char spath[PATH_MAX];
    sharebox_path(spath, path);

    res = readlink(spath, buf, size - 1);
    if (res == -1)
        return -errno;

    buf[res] = '\0';
    return 0;
}


static int sharebox_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
               off_t offset, struct fuse_file_info *fi)
{
    DIR *dp;
    struct dirent *de;
    (void) offset;
    (void) fi;

    char spath[PATH_MAX];
    sharebox_path(spath, path);

    dp = opendir(spath);
    if (dp == NULL)
        return -errno;

    while ((de = readdir(dp)) != NULL) {
        struct stat st;
        memset(&st, 0, sizeof(st));
        st.st_ino = de->d_ino;
        st.st_mode = de->d_type << 12;
        if (filler(buf, de->d_name, &st, 0))
            break;
    }

    closedir(dp);
    return 0;
}

static int sharebox_mknod(const char *path, mode_t mode, dev_t rdev)
{
    int res;

    char spath[PATH_MAX];
    sharebox_path(spath, path);

    /* On Linux this could just be 'mknod(path, mode, rdev)' but this
       is more portable */
    if (S_ISREG(mode)) {
        res = open(spath, O_CREAT | O_EXCL | O_WRONLY, mode);
        if (res >= 0)
            res = close(res);
    } else if (S_ISFIFO(mode))
        res = mkfifo(spath, mode);
    else
        res = mknod(spath, mode, rdev);
    if (res == -1)
        return -errno;

    return 0;
}

static int sharebox_mkdir(const char *path, mode_t mode)
{
    int res;

    char spath[PATH_MAX];
    sharebox_path(spath, path);

    res = mkdir(spath, mode);
    if (res == -1)
        return -errno;

    return 0;
}

static int sharebox_unlink(const char *path)
{
    int res;

    char spath[PATH_MAX];
    sharebox_path(spath, path);

    res = unlink(spath);
    if (res == -1)
        return -errno;

    return 0;
}

static int sharebox_rmdir(const char *path)
{
    int res;

    char spath[PATH_MAX];
    sharebox_path(spath, path);

    res = rmdir(spath);
    if (res == -1)
        return -errno;

    return 0;
}

static int sharebox_symlink(const char *target, const char *linkname)
{
    int res;

    char slinkname[PATH_MAX];
    sharebox_path(slinkname, linkname);

    res = symlink(target, slinkname);
    if (res == -1)
        return -errno;

    return 0;
}

static int sharebox_rename(const char *from, const char *to)
{
    int res;

    char sfrom[PATH_MAX];
    char sto[PATH_MAX];

    sharebox_path(sfrom, from);
    sharebox_path(sto, to);

    res = rename(sfrom, sto);
    if (res == -1)
        return -errno;

    return 0;
}

static int sharebox_chmod(const char *path, mode_t mode)
{
    int res;

    char spath[PATH_MAX];
    sharebox_path(spath, path);

    res = chmod(spath, mode);
    if (res == -1)
        return -errno;

    return 0;
}

static int sharebox_chown(const char *path, uid_t uid, gid_t gid)
{
    int res;

    char spath[PATH_MAX];
    sharebox_path(spath, path);

    res = lchown(spath, uid, gid);
    if (res == -1)
        return -errno;

    return 0;
}

static int sharebox_truncate(const char *path, off_t size)
{
    int res;

    char spath[PATH_MAX];
    sharebox_path(spath, path);

    res = truncate(spath, size);
    if (res == -1)
        return -errno;

    return 0;
}

static int sharebox_utimens(const char *path, const struct timespec ts[2])
{
    int res;
    struct timeval tv[2];
    char spath[PATH_MAX];

    tv[0].tv_sec = ts[0].tv_sec;
    tv[0].tv_usec = ts[0].tv_nsec / 1000;
    tv[1].tv_sec = ts[1].tv_sec;
    tv[1].tv_usec = ts[1].tv_nsec / 1000;

    sharebox_path(spath, path);

    res = utimes(spath, tv);
    if (res == -1)
        return -errno;

    return 0;
}

static int sharebox_open(const char *path, struct fuse_file_info *fi)
{
    int res;

    char spath[PATH_MAX];
    sharebox_path(spath, path);

    res = open(spath, fi->flags);
    if (res == -1)
        return -errno;

    close(res);
    return 0;
}

static int sharebox_read(const char *path, char *buf, size_t size, off_t offset,
            struct fuse_file_info *fi)
{
    int fd;
    int res;
    (void) fi;

    char spath[PATH_MAX];
    sharebox_path(spath, path);

    fd = open(spath, O_RDONLY);
    if (fd == -1)
        return -errno;

    res = pread(fd, buf, size, offset);
    if (res == -1)
        res = -errno;

    close(fd);
    return res;
}

static int sharebox_write(const char *path, const char *buf, size_t size,
             off_t offset, struct fuse_file_info *fi)
{
    int fd;
    int res;
    (void) fi;

    char spath[PATH_MAX];
    sharebox_path(spath, path);

    fd = open(spath, O_WRONLY);
    if (fd == -1)
        return -errno;

    res = pwrite(fd, buf, size, offset);
    if (res == -1)
        res = -errno;

    close(fd);
    return res;
}

static int sharebox_statfs(const char *path, struct statvfs *stbuf)
{
    int res;

    char spath[PATH_MAX];
    sharebox_path(spath, path);

    res = statvfs(spath, stbuf);
    if (res == -1)
        return -errno;

    return 0;
}

static int sharebox_release(const char *path, struct fuse_file_info *fi)
{
    /* Just a stub.  This method is optional and can safely be left
       unimplemented */

    (void) path;
    (void) fi;
    return 0;
}

static int sharebox_fsync(const char *path, int isdatasync,
             struct fuse_file_info *fi)
{
    /* Just a stub.  This method is optional and can safely be left
       unimplemented */

    (void) path;
    (void) isdatasync;
    (void) fi;
    return 0;
}

static struct fuse_operations sharebox_oper = {
    .getattr    = sharebox_getattr,
    .access     = sharebox_access,
    .readlink   = sharebox_readlink,
    .readdir    = sharebox_readdir,
    .mknod      = sharebox_mknod,
    .mkdir      = sharebox_mkdir,
    .symlink    = sharebox_symlink,
    .unlink     = sharebox_unlink,
    .rmdir      = sharebox_rmdir,
    .rename     = sharebox_rename,
    .chmod      = sharebox_chmod,
    .chown      = sharebox_chown,
    .truncate   = sharebox_truncate,
    .utimens    = sharebox_utimens,
    .open       = sharebox_open,
    .read       = sharebox_read,
    .write      = sharebox_write,
    .statfs     = sharebox_statfs,
    .release    = sharebox_release,
    .fsync      = sharebox_fsync,
};

static int
sharebox_opt_proc
(void *data, const char *arg, int key, struct fuse_args *outargs)
{
    struct stat st;
    int res;
    switch (key) {
        case KEY_HELP:
            fprintf(stderr,
                    "usage: %s mountpoint [options]\n"
                    "\n"
                    "general options:\n"
                    "    -o opt,[opt...]        mount options\n"
                    "    -h   --help            print help\n"
                    "    -V   --version         print version\n"
                    "\n"
                    "sharebox options:\n"
                    "    -o deep_replicate      replicate deeply\n"
                    "    -o write_callback      program to call when a file has been written\n"
                    "\n", outargs->argv[0]);
            fuse_opt_add_arg(outargs, "-ho");
            fuse_main(outargs->argc, outargs->argv, &sharebox_oper, NULL);
            exit(1);
        case KEY_VERSION:
            fprintf(stderr, "sharebox version %s\n", SHAREBOX_VERSION);
            fuse_opt_add_arg(outargs, "--version");
            fuse_main(outargs->argc, outargs->argv, &sharebox_oper, NULL);
            exit(0);
        case FUSE_OPT_KEY_NONOPT:
            if (!sharebox.reporoot) {
                res = stat(arg, &st);
                if (res == -1){
                    perror(arg);
                    exit(1);
                }
                sharebox.reporoot = realpath(arg, NULL);
                return 0;
            }
            return 1;
    }
    return 1;
}

int main(int argc, char *argv[])
{
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    memset(&sharebox, 0, sizeof(sharebox));
    fuse_opt_parse(&args, &sharebox, sharebox_opts, sharebox_opt_proc);
    umask(0);
    return fuse_main(args.argc, args.argv, &sharebox_oper, NULL);
}

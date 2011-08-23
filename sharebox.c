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
#include <pthread.h>
#include "git-annex.h"

#ifdef DEBUG
#define debug(...) printf(__VA_ARGS__)
#else
#define debug(...)
#endif
// TODO: fix the errno (save them as soon as they happen)

/*
 * FS attributes
 */

struct sharebox
{
    pthread_mutex_t rwlock;
    const char *reporoot;
    bool deep_replicate;
    const char *write_callback;
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


static void fullpath(char fpath[FILENAME_MAX], const char *path)
{
    strcpy(fpath, sharebox.reporoot);
    strncat(fpath, "/files", FILENAME_MAX);
    strncat(fpath, path, FILENAME_MAX);
}

static int ondisk(const char *lnk)
{
    struct stat st;
    return (stat(lnk, &st) != -1);
}

/*
 * FS operations
 */

static int sharebox_getattr(const char *path, struct stat *stbuf)
{
    debug("sharebox_getattr(%s, stbuf)\n", path);

    int res;
    char fpath[FILENAME_MAX];
    fullpath(fpath, path);

    res = lstat(fpath, stbuf);
    if (git_annexed(sharebox.reporoot, fpath)) {
        if (ondisk(fpath)) {
            res = stat(fpath, stbuf);
        } else {
            stbuf->st_mode &= ~S_IFMT;
            stbuf->st_mode |= S_IFREG; /* fake regular file */
            stbuf->st_size = 0;        /* fake size = 0 */
        }
        stbuf->st_mode |= S_IWUSR;     /* fake writable */
    }
    if (res == -1)
        return -errno;

    return 0;
}

static int sharebox_access(const char *path, int mask)
{
    debug("sharebox_access(%s, %d)\n", path, mask);

    int res;

    char fpath[FILENAME_MAX];
    fullpath(fpath, path);

    if (git_annexed(sharebox.reporoot, fpath)) {
        if (ondisk(fpath))
            res = access(fpath, mask & ~W_OK);
        else
            res = -EACCES;
    }
    else
        res = access(fpath, mask);

    if (res == -1)
        res = -errno;

    return res;
}

static int sharebox_readlink(const char *path, char *buf, size_t size)
{
    debug("sharebox_readlink(%s, buf, size)\n", path);

    int res;

    char fpath[FILENAME_MAX];
    fullpath(fpath, path);

    res = readlink(fpath, buf, size - 1);
    if (res == -1)
        return -errno;

    buf[res] = '\0';
    return 0;
}


static int sharebox_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
               off_t offset, struct fuse_file_info *fi)
{
    debug("sharebox_readdir(%s, buf, filler, offset, fi)\n", path);

    DIR *dp;
    struct dirent *de;
    namelist *branch, *b;
    (void) offset;
    (void) fi;

    char fpath[FILENAME_MAX];
    fullpath(fpath, path);

    dp = opendir(fpath);
    if (dp == NULL)
        return -errno;

    while ((de = readdir(dp)) != NULL) {
        if (filler(buf, de->d_name, NULL, 0))
            break;
    }
    closedir(dp);

    /* We then list conflicting files */
    branch = git_branches(sharebox.reporoot);
    for (b = branch; b != NULL; b = b->next) {
        namelist *files, *f;
        files = conflicting_files(sharebox.reporoot, fpath, b->name);
        for (f = files; f != NULL; f = f->next) {
            char name[FILENAME_MAX];
            strncpy(name, ".", FILENAME_MAX);
            strncat(name, branch->name, FILENAME_MAX);
            strncat(name, ".", FILENAME_MAX);
            strncat(name, f->name, FILENAME_MAX);
            strncat(name, ".conflict", FILENAME_MAX);
            if (filler(buf, name, NULL, 0))
                break;
        }
        free_namelist(files);
    }
    free_namelist(branch);

    return 0;
}

static int sharebox_mknod(const char *path, mode_t mode, dev_t rdev)
{
    debug("sharebox_mknod(%s, mode, rdev)\n", path);

    pthread_mutex_lock(&sharebox.rwlock);

    int res;

    char fpath[FILENAME_MAX];
    fullpath(fpath, path);

    /* On Linux this could just be 'mknod(path, mode, rdev)' but this
       is more portable */
    if (S_ISREG(mode)) {
        res = open(fpath, O_CREAT | O_EXCL | O_WRONLY, mode);
        if (res >= 0)
            res = close(res);
    } else if (S_ISFIFO(mode))
        res = mkfifo(fpath, mode);
    else
        res = mknod(fpath, mode, rdev);

    pthread_mutex_unlock(&sharebox.rwlock);

    if (res == -1)
        return -errno;
    return 0;
}

static int sharebox_mkdir(const char *path, mode_t mode)
{
    debug("sharebox_mkdir(%s, mode)\n", path);

    int res;

    char fpath[FILENAME_MAX];
    fullpath(fpath, path);

    res = mkdir(fpath, mode);

    if (res == -1)
        return -errno;
    return 0;
}

static int sharebox_unlink(const char *path)
{
    debug("sharebox_unlink(%s)\n", path);

    pthread_mutex_lock(&sharebox.rwlock);

    int res;

    char fpath[FILENAME_MAX];
    fullpath(fpath, path);

    res = unlink(fpath);

    if (!git_ignored(sharebox.reporoot, fpath)){
        git_rm(sharebox.reporoot, fpath);
        git_commit(sharebox.reporoot, "removed %s", path + 1);
    }

    pthread_mutex_unlock(&sharebox.rwlock);

    if (res == -1)
        return -errno;

    return 0;
}

static int sharebox_rmdir(const char *path)
{
    debug("sharebox_rmdir(%s)\n", path);

    int res;

    char fpath[FILENAME_MAX];
    fullpath(fpath, path);

    res = rmdir(fpath);
    if (res == -1)
        return -errno;

    return 0;
}

static int sharebox_symlink(const char *target, const char *linkname)
{
    debug("sharebox_symlink(%s, %s)\n", target, linkname);

    pthread_mutex_lock(&sharebox.rwlock);

    int res;

    char flinkname[FILENAME_MAX];
    fullpath(flinkname, linkname);

    res = symlink(target, flinkname);

    if (!git_ignored(sharebox.reporoot, flinkname)){
        git_add(sharebox.reporoot, flinkname);
        git_commit(sharebox.reporoot, "created symlink %s->%s", linkname + 1, target);
    }

    pthread_mutex_unlock(&sharebox.rwlock);

    if (res == -1)
        return -errno;

    return 0;
}

static int sharebox_rename(const char *from, const char *to)
{
    debug("sharebox_rename(%s, %s)\n", from, to);

    pthread_mutex_lock(&sharebox.rwlock);

    int res;
    bool from_ignored;
    bool to_ignored;

    char ffrom[FILENAME_MAX];
    char fto[FILENAME_MAX];

    fullpath(ffrom, from);
    fullpath(fto, to);

    /* proceed to rename */
    from_ignored = git_ignored(sharebox.reporoot, ffrom);
    res = rename(ffrom, fto);
    to_ignored = git_ignored(sharebox.reporoot, fto);

    if (res != -1) {
        /* moved ignored to ignored (nothing) */

        /* moved ignored to non ignored*/
        if (from_ignored && !to_ignored){
            git_annex_add(sharebox.reporoot, fto);
            git_add(sharebox.reporoot, fto); /* this ensures links will be added too */
        }
        /* moved non ignored to ignored */
        if (!from_ignored && to_ignored){
            git_rm(sharebox.reporoot, ffrom);
        }
        /* moved non ignored to non ignored */
        if (!from_ignored && !to_ignored){
            git_mv(sharebox.reporoot, ffrom, fto);
        }

        git_commit(sharebox.reporoot, "moved %s to %s", from+1, to+1);
    }

    pthread_mutex_unlock(&sharebox.rwlock);

    if (res == -1)
        return -errno;

    return 0;
}

static int sharebox_chmod(const char *path, mode_t mode)
{
    debug("sharebox_chmod(%s, mode)\n", path);

    pthread_mutex_lock(&sharebox.rwlock);

    int res;

    char fpath[FILENAME_MAX];
    fullpath(fpath, path);

    git_annex_unlock(sharebox.reporoot, fpath);

    res = chmod(fpath, mode);

    git_annex_add(sharebox.reporoot, fpath);
    git_commit(sharebox.reporoot, "chmoded %s to %o", path+1, mode);

    pthread_mutex_unlock(&sharebox.rwlock);

    if (res == -1)
        return -errno;

    return 0;
}

static int sharebox_chown(const char *path, uid_t uid, gid_t gid)
{
    debug("sharebox_chown(%s, uid, gid)\n", path);

    pthread_mutex_lock(&sharebox.rwlock);

    int res;

    char fpath[FILENAME_MAX];
    fullpath(fpath, path);

    git_annex_unlock(sharebox.reporoot, fpath);

    res = lchown(fpath, uid, gid);

    git_annex_add(sharebox.reporoot, fpath);
    git_commit(sharebox.reporoot, "chmown on %s", path+1);

    pthread_mutex_unlock(&sharebox.rwlock);

    if (res == -1)
        return -errno;

    return 0;
}

static int sharebox_truncate(const char *path, off_t size)
{
    debug("sharebox_truncate(%s, size)\n", path);

    pthread_mutex_lock(&sharebox.rwlock);

    int res;

    char fpath[FILENAME_MAX];
    fullpath(fpath, path);

    git_annex_unlock(sharebox.reporoot, fpath);

    res = truncate(fpath, size);

    git_annex_add(sharebox.reporoot, fpath);
    git_commit(sharebox.reporoot, "truncated on %s", path+1);

    pthread_mutex_unlock(&sharebox.rwlock);

    if (res == -1)
        return -errno;

    return 0;
}

static int sharebox_utimens(const char *path, const struct timespec ts[2])
{
    debug("sharebox_utimens(%s, ts)\n", path);

    pthread_mutex_lock(&sharebox.rwlock);

    int res;
    struct timeval tv[2];
    char fpath[FILENAME_MAX];

    tv[0].tv_sec = ts[0].tv_sec;
    tv[0].tv_usec = ts[0].tv_nsec / 1000;
    tv[1].tv_sec = ts[1].tv_sec;
    tv[1].tv_usec = ts[1].tv_nsec / 1000;

    fullpath(fpath, path);

    git_annex_unlock(sharebox.reporoot, fpath);

    res = utimes(fpath, tv);

    git_annex_add(sharebox.reporoot, fpath);
    git_commit(sharebox.reporoot, "utimens on %s", path+1);

    pthread_mutex_unlock(&sharebox.rwlock);

    if (res == -1)
        return -errno;

    return 0;
}

static int sharebox_open(const char *path, struct fuse_file_info *fi)
{
    debug("sharebox_open(%s, fi)\n", path);

    int res;
    int flags;

    char fpath[FILENAME_MAX];
    fullpath(fpath, path);

    flags=fi->flags;

    if (git_annexed(sharebox.reporoot, fpath))
        /* Get the file on the fly, remove W_OK if it was requested */
        if (!ondisk(fpath))
            git_annex_get(sharebox.reporoot, fpath, NULL);
        if (!ondisk(fpath))
            return -EACCES;
        flags &= ~W_OK;

    res = open(fpath, flags);

    if (res == 1)
        return -errno;

    close(res);

    return 0;
}

static int sharebox_read(const char *path, char *buf, size_t size, off_t offset,
            struct fuse_file_info *fi)
{
    debug("sharebox_read(%s, buf, offset, fi)\n", path);

    pthread_mutex_lock(&sharebox.rwlock);

    int fd;
    int res;
    (void) fi;

    char fpath[FILENAME_MAX];
    fullpath(fpath, path);

    if ((fd = open(fpath, O_RDONLY)) != -1)
        if ((res = pread(fd, buf, size, offset)) != -1)
            close(fd);

    pthread_mutex_unlock(&sharebox.rwlock);

    if (fd == -1 || res == -1)
        return -errno;
    return res;
}

static int sharebox_write(const char *path, const char *buf, size_t size,
             off_t offset, struct fuse_file_info *fi)
{
    debug("sharebox_write(%s, buf, size, offset, fi)\n", path);

    pthread_mutex_lock(&sharebox.rwlock);

    int fd;
    int res;
    (void) fi;

    char fpath[FILENAME_MAX];
    fullpath(fpath, path);

    if (git_annexed(sharebox.reporoot, fpath))
        git_annex_unlock(sharebox.reporoot, fpath);

    if ((fd = open(fpath, O_WRONLY)) != -1)
        if((res = pwrite(fd, buf, size, offset)) != -1)
            close(fd);

    pthread_mutex_unlock(&sharebox.rwlock);

    if (fd == -1 || res == -1)
        return -errno;
    return res;
}

static int sharebox_release(const char *path, struct fuse_file_info *fi)
{
    debug("sharebox_release(%s, fi)\n", path);

    pthread_mutex_lock(&sharebox.rwlock);

    char fpath[FILENAME_MAX];
    fullpath(fpath, path);

    if (!git_ignored(sharebox.reporoot, fpath)){
        git_annex_add(sharebox.reporoot, fpath);
        git_commit(sharebox.reporoot, "released %s", path+1);
    }

    pthread_mutex_unlock(&sharebox.rwlock);

    return 0;
}

static int sharebox_statfs(const char *path, struct statvfs *stbuf)
{
    debug("sharebox_statfs(%s, stbuf)\n", path);

    int res;

    char fpath[FILENAME_MAX];
    fullpath(fpath, path);

    res = statvfs(fpath, stbuf);
    if (res == -1)
        return -errno;

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
    .release    = sharebox_release,
    .statfs     = sharebox_statfs,
};

static int
sharebox_opt_proc
(void *data, const char *arg, int key, struct fuse_args *outargs)
{
    struct stat st;
    char files[FILENAME_MAX];
    switch (key) {
        case KEY_HELP:
            fprintf(stderr,
                    "usage: %s <fsdir> <mountpoint> [options]\n"
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
                if (stat(arg, &st) == -1){
                    perror(arg);
                    exit(1);
                }
                if ((sharebox.reporoot = realpath(arg, NULL)) == NULL){
                    perror(sharebox.reporoot);
                    exit(1);
                }
                snprintf(files, FILENAME_MAX, "%s/files", sharebox.reporoot);
                if (stat(files, &st) == -1){
                    perror(files);
                    fprintf(stderr, "Missing /files/ (did mkfs do its job?)\n");
                    exit(1);
                }
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

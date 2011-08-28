/*
 * sharebox
 */

#include "common.h"
#include "slash.h"

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

/*
 * FS operations
 *
 * sharebox-fs embeds more than one filesystem.
 *
 * "/.sharebox/remotes/" allows to manage the remotes
 * "/.sharebox/revisions/" allows to browse the history
 * "/.sharebox/unreferenced/" allows to see what files you can trash
 * "/" contains the real versionning.
 *
 * To separate the logic, we match the path of the files we operate on and
 * switch to the relevant operation. To do so, we go through the "dirlist"
 * attribute of sharebox until we see a directory that match.
 *
 * A special case for "rename": We refuse moving files outside a
 * filesystem. As a consequence, if the files do not both match, it is an
 * error.
 */

struct sharebox sharebox;

static int sharebox_getattr(const char *path, struct stat *stbuf)
{
    dirlist *l;
    dir *d;
    for (l = sharebox.dirs; l != NULL; l = l->next) {
        d = l->dir;
        if (strncmp(path, d->name, strlen(d->name)) == 0)
            return d->operations.getattr(path, stbuf);
    }
    return -EACCES;
}

static int sharebox_access(const char *path, int mask)
{
    dirlist *l;
    dir *d;
    for (l = sharebox.dirs; l != NULL; l = l->next) {
        d = l->dir;
        if (strncmp(path, d->name, strlen(d->name)) == 0)
            return d->operations.access(path, mask);
    }
    return -EACCES;
}

static int sharebox_readlink(const char *path, char *buf, size_t size)
{
    dirlist *l;
    dir *d;
    for (l = sharebox.dirs; l != NULL; l = l->next) {
        d = l->dir;
        if (strncmp(path, d->name, strlen(d->name)) == 0)
            return d->operations.readlink(path, buf, size);
    }
    return -EACCES;
}


static int sharebox_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
               off_t offset, struct fuse_file_info *fi)
{
    dirlist *l;
    dir *d;
    for (l = sharebox.dirs; l != NULL; l = l->next) {
        d = l->dir;
        if (strncmp(path, d->name, strlen(d->name)) == 0)
            return d->operations.readdir(path, buf, filler, offset, fi);
    }
    return -EACCES;
}

static int sharebox_mknod(const char *path, mode_t mode, dev_t rdev)
{
    dirlist *l;
    dir *d;
    for (l = sharebox.dirs; l != NULL; l = l->next) {
        d = l->dir;
        if (strncmp(path, d->name, strlen(d->name)) == 0)
            return d->operations.mknod(path, mode, rdev);
    }
    return -EACCES;
}

static int sharebox_mkdir(const char *path, mode_t mode)
{
    dirlist *l;
    dir *d;
    for (l = sharebox.dirs; l != NULL; l = l->next) {
        d = l->dir;
        if (strncmp(path, d->name, strlen(d->name)) == 0)
            return d->operations.mkdir(path, mode);
    }
    return -EACCES;
}

static int sharebox_unlink(const char *path)
{
    dirlist *l;
    dir *d;
    for (l = sharebox.dirs; l != NULL; l = l->next) {
        d = l->dir;
        if (strncmp(path, d->name, strlen(d->name)) == 0)
            return d->operations.unlink(path);
    }
    return -EACCES;
}

static int sharebox_rmdir(const char *path)
{
    dirlist *l;
    dir *d;
    for (l = sharebox.dirs; l != NULL; l = l->next) {
        d = l->dir;
        if (strncmp(path, d->name, strlen(d->name)) == 0)
            return d->operations.rmdir(path);
    }
    return -EACCES;
}

static int sharebox_symlink(const char *target, const char *linkname)
{
    dirlist *l;
    dir *d;
    for (l = sharebox.dirs; l != NULL; l = l->next) {
        d = l->dir;
        if (strncmp(linkname, d->name, strlen(d->name)) == 0)
            return d->operations.symlink(target, linkname);
    }
    return -EACCES;
}

static int sharebox_rename(const char *from, const char *to)
{
    dirlist *l;
    dir *d;
    for (l = sharebox.dirs; l != NULL; l = l->next) {
        d = l->dir;
        /* /!\ we only accept renaming inside the same fs */
        if ((strncmp(from, d->name, strlen(d->name)) == 0) &&
            (strncmp(to, d->name, strlen(d->name) == 0)))
            return d->operations.rename(from, to);
    }
    return -EACCES;
}

static int sharebox_chmod(const char *path, mode_t mode)
{
    dirlist *l;
    dir *d;
    for (l = sharebox.dirs; l != NULL; l = l->next) {
        d = l->dir;
        if (strncmp(path, d->name, strlen(d->name)) == 0)
            return d->operations.chmod(path, mode);
    }
    return -EACCES;
}

static int sharebox_chown(const char *path, uid_t uid, gid_t gid)
{
    dirlist *l;
    dir *d;
    for (l = sharebox.dirs; l != NULL; l = l->next) {
        d = l->dir;
        if (strncmp(path, d->name, strlen(d->name)) == 0)
            return d->operations.chown(path, uid, gid);
    }
    return -EACCES;
}

static int sharebox_truncate(const char *path, off_t size)
{
    dirlist *l;
    dir *d;
    for (l = sharebox.dirs; l != NULL; l = l->next) {
        d = l->dir;
        if (strncmp(path, d->name, strlen(d->name)) == 0)
            return d->operations.truncate(path, size);
    }
    return -EACCES;
}

static int sharebox_utimens(const char *path, const struct timespec ts[2])
{
    dirlist *l;
    dir *d;
    for (l = sharebox.dirs; l != NULL; l = l->next) {
        d = l->dir;
        if (strncmp(path, d->name, strlen(d->name)) == 0)
            return d->operations.utimens(path, ts);
    }
    return -EACCES;
}

static int sharebox_open(const char *path, struct fuse_file_info *fi)
{
    dirlist *l;
    dir *d;
    for (l = sharebox.dirs; l != NULL; l = l->next) {
        d = l->dir;
        if (strncmp(path, d->name, strlen(d->name)) == 0)
            return d->operations.open(path, fi);
    }
    return -EACCES;
}

static int sharebox_read(const char *path, char *buf, size_t size, off_t offset,
            struct fuse_file_info *fi)
{
    dirlist *l;
    dir *d;
    for (l = sharebox.dirs; l != NULL; l = l->next) {
        d = l->dir;
        if (strncmp(path, d->name, strlen(d->name)) == 0)
            return d->operations.read(path, buf, size, offset, fi);
    }
    return -EACCES;
}

static int sharebox_write(const char *path, const char *buf, size_t size,
             off_t offset, struct fuse_file_info *fi)
{
    dirlist *l;
    dir *d;
    for (l = sharebox.dirs; l != NULL; l = l->next) {
        d = l->dir;
        if (strncmp(path, d->name, strlen(d->name)) == 0)
            return d->operations.write(path, buf, size, offset, fi);
    }
    return -EACCES;
}

static int sharebox_release(const char *path, struct fuse_file_info *fi)
{
    dirlist *l;
    dir *d;
    for (l = sharebox.dirs; l != NULL; l = l->next) {
        d = l->dir;
        if (strncmp(path, d->name, strlen(d->name)) == 0)
            return d->operations.release(path, fi);
    }
    return 0;
}

static int sharebox_statfs(const char *path, struct statvfs *stbuf)
{
    dirlist *l;
    dir *d;
    for (l = sharebox.dirs; l != NULL; l = l->next) {
        d = l->dir;
        if (strncmp(path, d->name, strlen(d->name)) == 0)
            return d->operations.statfs(path, stbuf);
    }
    return -EACCES;
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

static dirlist *init_dirlist()
{
    dirlist *l;
    dir *slash;

    l = malloc(sizeof (dirlist));
    slash = malloc (sizeof (dir));
    init_slash(slash);

    l->dir = slash;
    l->next = NULL;

    return l;
}

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
            if (!sharebox.dirs)
                sharebox.dirs = init_dirlist();
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

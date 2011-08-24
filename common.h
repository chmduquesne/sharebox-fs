#ifndef __COMMON_H__
#define __COMMON_H__

#define SHAREBOX_VERSION "0.0.1"
#define FUSE_USE_VERSION 26

#ifdef linux
/* For pread()/pwrite() */
#define _XOPEN_SOURCE 500
#endif

#include <stdio.h>
#include <fuse.h>
#include <fuse_opt.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#include <stdbool.h>
#include <pthread.h>

#ifdef DEBUG
#define debug(...) printf(__VA_ARGS__)
#else
#define debug(...)
#endif

typedef struct fs fs;
struct fs
{
    char dir[FILENAME_MAX];
    struct fuse_operations operations;
};

typedef struct fslist fslist;
struct fslist
{
    fs *fs;
    fslist *next;
};

struct sharebox
{
    pthread_mutex_t rwlock;
    const char *reporoot;
    bool deep_replicate;
    const char *write_callback;
    fslist *fslist;
};

static struct sharebox sharebox;

#endif /*__COMMON_H__ */

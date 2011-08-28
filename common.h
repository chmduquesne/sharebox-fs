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

typedef struct dir dir;
struct dir
{
    char name[FILENAME_MAX];
    struct fuse_operations operations;
};

typedef struct dirlist dirlist;
struct dirlist
{
    dir *dir;
    dirlist *next;
};

struct sharebox
{
    pthread_mutex_t rwlock;
    const char *reporoot;
    bool deep_replicate;
    const char *write_callback;
    dirlist *dirs;
};

extern struct sharebox sharebox;

#endif /*__COMMON_H__ */

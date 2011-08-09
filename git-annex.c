#include "git-annex.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>

static int fmt_system(const char *format, ...)
{
    va_list ap;
    size_t arg_max;
    char *command;
    int status;

    arg_max = sysconf(_SC_ARG_MAX);
    command = malloc(arg_max);

    va_start(ap, format);
    vsnprintf(command, arg_max, format, ap);
    va_end(ap);

    status = system(command);
    free(command);
    return status;
}

int git_annex_unlock(const char *repodir, const char *path)
{
    chdir(repodir);
    return fmt_system("git annex unlock -- \"%s\"", path);
}

int git_annex_add(const char *repodir, const char *path)
{
    chdir(repodir);
    return fmt_system("git annex add -- \"%s\"", path);
}

int git_annex_get(const char *repodir, const char *path)
{
    chdir(repodir);
    return fmt_system("git annex get -- \"%s\"", path);
}

int git_commit(const char *repodir, const char *message)
{
    chdir(repodir);
    return fmt_system("git ci -m \"%s\"", message);
}

int git_rm(const char *repodir, const char *path)
{
    chdir(repodir);
    return fmt_system("git rm -- \"%s\"", path);
}

int git_mv(const char *repodir, const char *old, const char *new)
{
    chdir(repodir);
    return fmt_system("git mv -- \"%s\" \"%s\"", old, new);
}

int annexed(const char *repodir, const char *path)
{
    struct stat st;
    char buf[PATH_MAX];
    if (stat(path, &st) == -1)
        perror(path);
    if (!S_ISLNK(st.st_mode))
        return 0;
    if (readlink(path, buf, PATH_MAX) == -1)
        perror(path);
    return (strstr(buf, ".git/annex/objects") != NULL);
}


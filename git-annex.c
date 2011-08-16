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

/*
 * wrapper around system() to execute a formatted command. Used as:
 * fmt_system("mycommand %s", "my args");
 */
static int fmt_system(const char *format, ...)
{
    va_list ap;
    size_t ARG_MAX;
    char *command;
    int status;

    ARG_MAX = sysconf(_SC_ARG_MAX);
    command = malloc(ARG_MAX);

    va_start(ap, format);
    vsnprintf(command, ARG_MAX, format, ap);
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

int git_add(const char *repodir, const char *path)
{
    chdir(repodir);
    return fmt_system("git add -- \"%s\"", path);
}

int git_commit(const char *repodir, const char *format, ...)
{
    va_list ap;
    size_t ARG_MAX;
    char *message;

    ARG_MAX = sysconf(_SC_ARG_MAX);
    message = malloc(ARG_MAX - 16);

    va_start(ap, format);
    vsnprintf(message, ARG_MAX, format, ap);
    va_end(ap);

    chdir(repodir);
    return fmt_system("git commit -m \"%s\"", message);
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

int git_annexed(const char *repodir, const char *path)
{
    struct stat st;
    char realpathbuf[PATH_MAX];
    char gitannexpathbuf[PATH_MAX];
    snprintf(gitannexpathbuf, PATH_MAX, "%s/.git/annex/objects", repodir);
    if (stat(path, &st) == -1)
        perror(path);
    if (!S_ISLNK(st.st_mode))
        return 0;
    if (realpath(path, realpathbuf) == NULL)
        perror(path);
    return (strstr(realpathbuf, gitannexpathbuf) == realpathbuf);
}

int git_ignored(const char *repodir, const char *path)
{
    FILE *pipe;
    char buf[PATH_MAX], command[PATH_MAX + 40], *p;
    int res;

    strcpy(command, "git ls-files -c -o -d -m --full-name -- ");
    strncat(command, path, PATH_MAX);

    res = 1;
    if ((pipe = popen(command, "r" )) == NULL)
        return -1;
    while (fgets(buf, sizeof buf, pipe) != NULL || !feof(pipe)) {
        if ((p = strchr(buf, '\n')))
            *p = '\0';
        if (strcmp(buf, path) == 0)
            res = 0;
    }
    if (pclose(pipe) == -1)
        return -1;
    return res;
}

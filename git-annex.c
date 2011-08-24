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

    printf("%s\n", command);
    status = system(command);
    free(command);
    return status;
}

int git_annex_unlock(const char *repodir, const char *path)
{
    chdir(repodir);
    return fmt_system("git annex unlock -- \"%s\"",
            path + strlen(repodir) + 1);
}

int git_annex_add(const char *repodir, const char *path)
{
    chdir(repodir);
    return fmt_system("git annex add -- \"%s\"",
            path + strlen(repodir) + 1);
}

int git_annex_get(const char *repodir, const char *path,
        const char *branch)
{
    int res;
    chdir(repodir);
    if (branch)
        fmt_system("git checkout %s", branch);
    res = fmt_system("git annex get -- \"%s\"",
            path + strlen(repodir) + 1);
    if (branch)
        fmt_system("git checkout git-annex");
    return res;
}

int git_add(const char *repodir, const char *path)
{
    chdir(repodir);
    return fmt_system("git add -- \"%s\"",
            path + strlen(repodir) + 1);
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
    return fmt_system("git rm -- \"%s\"", path + strlen(repodir) + 1);
}

int git_mv(const char *repodir, const char *old, const char *new)
{
    chdir(repodir);
    return fmt_system("git mv -- \"%s\" \"%s\"",
            old + strlen(repodir) + 1, new + strlen(repodir) + 1);
}

int git_annexed(const char *repodir, const char *path)
{
    struct stat st;
    char realpathbuf[FILENAME_MAX];
    char gitannexpathbuf[FILENAME_MAX];
    snprintf(gitannexpathbuf, FILENAME_MAX, "%s/.git/annex/objects", repodir);
    if (lstat(path, &st) == -1)
        perror(path);
    if (!S_ISLNK(st.st_mode))
        return 0;
    if (realpath(path, realpathbuf) == NULL)
        perror(path);
    return (strncmp(realpathbuf, gitannexpathbuf, strlen(gitannexpathbuf)) == 0);
}

int git_ignored(const char *repodir, const char *path)
{
    FILE *pipe;
    char buf[BUFSIZ], command[256 + FILENAME_MAX], *p;
    int res;

    chdir(repodir);

    snprintf(command, sizeof command,
            "git ls-files -c -o -d -m --full-name -- \"%s\"",
            path + strlen(repodir) + 1);

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

namelist* git_branches(const char *repodir)
{
    FILE *pipe;
    char buf[BUFSIZ], *p;
    namelist *res, *curr;
    res = curr = NULL;

    chdir(repodir);
    if ((pipe = popen("git branch", "r")) == NULL)
        return NULL;

    while (fgets(buf, sizeof buf, pipe) != NULL || !feof(pipe)) {
        if ((p = strchr(buf, '\n')))
            *p = '\0';
        if (strcmp(buf + 2, "git-annex") != 0) {
            namelist *b = malloc(sizeof(namelist));
            strcpy(b->name, buf + 2);
            b->next = NULL;
            if (curr) {
                curr->next = b;
                curr = b;
            }
            else
                res = curr = b;
        }
    }
    if (pclose(pipe) == -1)
        return NULL;

    return res;
}

namelist* conflicting_files(const char *repodir, const char *path,
        const char *branch)
{
    FILE *pipe;
    char buf[BUFSIZ], *p, *s;
    namelist *res, *curr, *check, *n;

    if (strncmp(branch, "git-annex", strlen("git-annex") == 0))
        return NULL;

    chdir(repodir);
    fmt_system("git checkout %s", branch);
    fmt_system("git merge git-annex");

    if ((pipe = popen("git ls-files -u", "r")) == NULL)
        return NULL;

    res = curr = NULL;
    while (fgets(buf, sizeof buf, pipe) != NULL || !feof(pipe)) {
        /* remove trailing \n */
        if ((p = strchr(buf, '\n')))
            *p = '\0';
        /* move after the \t */
        if (!(s = strchr(buf, '\t')))
            break;
        n = malloc(sizeof(namelist));
        /* skip the directories */
        if (strstr(s + 1, path) && !strchr((s + 1 + strlen(path)), '/')) {
            strncpy(n->name, s + 1, sizeof(n->name));
        }
        /* remove if already there */
        for (check = res; check->next != NULL; check = check->next) {
            if (strcmp(curr->name, check->name) == 0) {
                free(n);
                n = NULL;
            }
        }
        if (n) {
            if (!res)
                res = curr = n;
            else {
                curr->next = n;
                curr = curr->next;
            }
        }
    }

    if (pclose(pipe) == -1)
        return NULL;
    fmt_system("git reset --hard");
    fmt_system("git checkout git-annex");

    return res;
}

void free_namelist(namelist *l)
{
    namelist *curr, *next;
    if (l != NULL) {
        curr = l;
        next = l->next;
        free(curr);
        while (next != NULL) {
            curr = next;
            next = curr->next;
            free(curr);
        }
    }
}

void target(char target[FILENAME_MAX], const char *repodir,
        const char *path, const char *branch)
{
    int res;
    chdir(repodir);
    fmt_system("git checkout %s", branch);
    res = readlink(path, target, FILENAME_MAX - 1);
    target[res] = '\0';
    fmt_system("git checkout master");
}

/*
 * git-annex.h
 */

#include <stdio.h>

int git_annex_unlock(const char *repodir, const char *path);
int git_annex_add(const char *repodir, const char *path);
int git_annex_get(const char *repodir, const char *path, const char *branch);
int git_add(const char *repodir, const char *path);
int git_commit(const char *repodir, const char *format, ...);
int git_rm(const char *repodir, const char *path);
int git_mv(const char *repodir, const char *old, const char *new);
int git_annexed(const char *repodir, const char *path);
int git_ignored(const char *repodir, const char *path);

typedef struct namelist namelist;
struct namelist {
    char name[FILENAME_MAX];
    namelist* next;
};

namelist* git_branches(const char *repodir);
namelist* conflicting_files(const char *repodir, const char *path,
        const char *branch);
void free_namelist(namelist *l);
void target(char target[FILENAME_MAX], const char *repodir,
        const char *path, const char *branch);

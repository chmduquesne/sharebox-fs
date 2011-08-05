/*
 * git-annex.h
 */

int git_annex_unlock(const char *gitdir, const char *path);
int git_annex_add(const char *gitdir, const char *path);
int git_annex_get(const char *gitdir, const char *path);
int git_commit(const char *gitdir, const char *message);
int git_rm(const char *gitdir, const char *path);
int git_mv(const char *gitdir, const char *old, const char *new);
int annexed(const char *gitdir, const char *path);

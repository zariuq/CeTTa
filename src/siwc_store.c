/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "siwc_store.h"
#include "siwc_crypto.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

bool cetta_siwc_id(const char *s) {
    if (!s || strncmp(s, "r_", 2) || strlen(s) != 45) return false;
    for (s += 2; *s; ++s)
        if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') ||
              (*s >= '0' && *s <= '9') || *s == '-' || *s == '_')) return false;
    return true;
}
static bool private_fd(int fd, bool directory) {
    struct stat st;
    return fd >= 0 && !fstat(fd, &st) && st.st_uid == geteuid() && !(st.st_mode & 0077) &&
        (directory ? S_ISDIR(st.st_mode) : S_ISREG(st.st_mode));
}
int cetta_siwc_store_open(const char *path) {
    if (!path || !*path) return -1;
    char *parents = strdup(path);
    if (!parents) return -1;
    for (char *p = parents + 1; *p; ++p) if (*p == '/') {
        *p = 0;
        if (mkdir(parents, 0700) && errno != EEXIST) { free(parents); return -1; }
        *p = '/';
    }
    free(parents);
    if (mkdir(path, 0700) && errno != EEXIST) return -1;
    int fd = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (!private_fd(fd, true)) { if (fd >= 0) close(fd); return -1; }
    return fd;
}
typedef struct LocalLock {
    dev_t device; ino_t inode; char id[46]; pthread_mutex_t mutex;
    struct LocalLock *next;
} LocalLock;
static pthread_mutex_t locks_mutex = PTHREAD_MUTEX_INITIALIZER;
static LocalLock *locks;
static pthread_mutex_t *local_lock(int fd, const char *id) {
    struct stat st;
    if (fstat(fd, &st)) return NULL;
    pthread_mutex_lock(&locks_mutex);
    LocalLock *p = locks;
    while (p && !(p->device == st.st_dev && p->inode == st.st_ino && !strcmp(p->id, id))) p = p->next;
    if (!p) {
        p = calloc(1, sizeof(*p));
        if (p) {
            p->device = st.st_dev; p->inode = st.st_ino; strcpy(p->id, id);
            pthread_mutex_init(&p->mutex, NULL); p->next = locks; locks = p;
        }
    }
    pthread_mutex_unlock(&locks_mutex);
    return p ? &p->mutex : NULL;
}
int cetta_siwc_store_lock(int directory, const char *id, CettaSiwcLock *out) {
    memset(out, 0, sizeof(*out)); out->file = -1; out->directory = directory;
    if (!cetta_siwc_id(id)) return -1;
    pthread_mutex_t *local = local_lock(directory, id);
    if (!local) return -1;
    int status = pthread_mutex_trylock(local);
    if (status) return status == EBUSY ? 0 : -1;
    char name[64]; snprintf(name, sizeof(name), "%s.lock", id);
    int fd = openat(directory, name, O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (!private_fd(fd, false)) {
        if (fd >= 0) close(fd);
        pthread_mutex_unlock(local); return -1;
    }
    struct flock f = {.l_type = F_WRLCK, .l_whence = SEEK_SET};
    if (fcntl(fd, F_SETLK, &f)) {
        int error = errno; close(fd); pthread_mutex_unlock(local);
        return error == EACCES || error == EAGAIN ? 0 : -1;
    }
    out->file = fd; out->local = local; return 1;
}
void cetta_siwc_store_unlock(CettaSiwcLock *lock) {
    if (lock->file < 0) return;
    struct flock f = {.l_type = F_UNLCK, .l_whence = SEEK_SET};
    (void)fcntl(lock->file, F_SETLK, &f); close(lock->file); lock->file = -1;
    pthread_mutex_unlock(lock->local); lock->local = NULL;
}
static bool filename(const char *s) {
    if (!s || !*s || strlen(s) > 64 || *s == '.') return false;
    for (; *s; ++s) if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') ||
                           (*s >= '0' && *s <= '9') || *s == '-' || *s == '_' || *s == '.')) return false;
    return true;
}
json_t *cetta_siwc_store_read(int directory, const char *name) {
    if (!filename(name)) return NULL;
    int fd = openat(directory, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    struct stat st;
    if (!private_fd(fd, false) || fstat(fd, &st) || st.st_size > 1048576) {
        if (fd >= 0) close(fd);
        return NULL;
    }
    json_t *value = json_loadfd(fd, JSON_REJECT_DUPLICATES, NULL);
    close(fd); return value;
}
bool cetta_siwc_store_write(int directory, const char *name, const json_t *value) {
    if (!filename(name)) return false;
    struct stat st;
    if (!fstatat(directory, name, &st, AT_SYMLINK_NOFOLLOW) &&
        (!S_ISREG(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 0077))) return false;
    char nonce[44], temporary[64];
    if (!cetta_siwc_random(nonce)) return false;
    snprintf(temporary, sizeof(temporary), ".pending-%s", nonce);
    int fd = openat(directory, temporary, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) return false;
#ifdef CETTA_SIWC_TEST
    const char *fault = getenv("CETTA_SIWC_TEST_STORE_FAULT");
    bool inject = fault && json_integer_value(json_object_get(value, "generation")) >= 2 &&
        json_is_string(json_object_get(value, "access_token"));
    if (inject && !strcmp(fault, "partial-temp")) {
        ssize_t n = write(fd, "{", 1); (void)n; _exit(86);
    }
#endif
    bool ok = !json_dumpfd(value, fd, JSON_COMPACT) && !fsync(fd);
    if (close(fd)) ok = false;
#ifdef CETTA_SIWC_TEST
    if (ok && inject && !strcmp(fault, "before-rename")) _exit(86);
#endif
    if (ok && renameat(directory, temporary, directory, name)) ok = false;
    if (ok && fsync(directory)) ok = false;
#ifdef CETTA_SIWC_TEST
    if (ok && inject && !strcmp(fault, "after-commit")) _exit(86);
#endif
    if (!ok) unlinkat(directory, temporary, 0);
    return ok;
}
json_t *cetta_siwc_public_account(const json_t *record) {
    json_t *out = json_object();
    const char *keys[] = {"registration_id", "client_id", "email", "name", "scopes", "access_expires_at", "renewal_state", NULL};
    for (size_t i = 0; keys[i]; ++i) {
        json_t *v = json_object_get(record, keys[i]);
        if (v) json_object_set(out, keys[i], v);
    }
    const char *id = json_string_value(json_object_get(record, "registration_id"));
    if (id) {
        char alias[64]; snprintf(alias, sizeof(alias), "SIWC:%s", id);
        json_object_set_new(out, "credential_alias", json_string(alias));
    }
    json_object_set_new(out, "signed_in", json_boolean(json_is_string(json_object_get(record, "access_token"))));
    return out;
}
json_t *cetta_siwc_store_accounts(int directory) {
    int copy = openat(directory, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    DIR *dir = copy < 0 ? NULL : fdopendir(copy);
    if (!dir) { if (copy >= 0) close(copy); return NULL; }
    json_t *out = json_array(); struct dirent *entry;
    while ((entry = readdir(dir))) {
        size_t n = strlen(entry->d_name);
        if (n != 50 || strcmp(entry->d_name + 45, ".json")) continue;
        char id[46]; memcpy(id, entry->d_name, 45); id[45] = 0;
        if (!cetta_siwc_id(id)) continue;
        json_t *record = cetta_siwc_store_read(directory, entry->d_name);
        if (record) json_array_append_new(out, cetta_siwc_public_account(record));
        json_decref(record);
    }
    closedir(dir); return out;
}

#define _GNU_SOURCE
#include "library_fs.h"
#include "library.h"
#include "eval.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    int fd;
    DIR *directory;
    char *path;
    int fault;
    uint64_t observed;
    bool directory_end;
} FsCursor;

static const char *file_kind = "file-reader";
static const char *writer_kind = "file-writer";
static const char *directory_kind = "directory-reader";
static const char *walk_kind = "directory-walker";

typedef struct FsWalkDirectory {
    struct FsWalkDirectory *parent;
    DIR *directory;
    char *path, *relative;
    dev_t device;
    ino_t inode;
} FsWalkDirectory;

typedef struct {
    FsWalkDirectory *top;
    char *root, *name, *path, *relative;
    unsigned char hint;
    uint64_t observed;
    int fault;
    bool ended, fault_position_known;
} FsWalk;

static const char *path_arg(Atom *value) {
    if (!value) return NULL;
    if (value->kind == ATOM_SYMBOL) return atom_name_cstr(value);
    if (value->kind == ATOM_GROUNDED && value->ground.gkind == GV_STRING &&
        !memchr(value->ground.sval, 0, atom_string_len(value)))
        return value->ground.sval;
    return NULL;
}

static const char *fault_kind(int code) {
    switch (code) {
    case ENOENT: case ENOTDIR: return "fs:missing";
    case EACCES: case EPERM: return "fs:permission";
    case ENOMEM: case EMFILE: case ENFILE: case EOVERFLOW: return "fs:capacity";
    default: return "fs:io";
    }
}

static Atom *failed(Arena *arena, const char *kind, const char *operation,
                     const char *path, int code, uint64_t observed,
                     bool position_known, const char *message) {
    Atom *progress = atom_expr(arena, (Atom *[]){atom_symbol(arena, "fs:progress"),
        atom_int(arena, (int64_t)observed),
        atom_symbol(arena, position_known ? "known-position" : "unknown-position")}, 3u);
    return atom_expr(arena, (Atom *[]){atom_symbol(arena, "fs:failed"),
        atom_symbol(arena, kind), atom_symbol(arena, operation),
        atom_string(arena, path ? path : ""), atom_int(arena, code), progress,
        atom_string(arena, message)}, 7u);
}

static Atom *os_failed(Arena *arena, const char *operation, const char *path,
                        int code, uint64_t observed, bool position_known) {
    return failed(arena, fault_kind(code), operation, path, code, observed,
                   position_known, strerror(code));
}

static void release_cursor(void *resource) {
    FsCursor *cursor = resource;
    if (cursor->directory) (void)closedir(cursor->directory);
    else if (cursor->fd >= 0) (void)close(cursor->fd);
    free(cursor->path);
    free(cursor);
}

/* Reserve standard FILE streams while an internal descriptor could occupy
 * their numbers. Do not retry close: on Linux even an interrupted close has
 * released the descriptor and its number may already have been reused. */
static int open_owned_at(int directory, const char *path, int flags, mode_t mode) {
    flockfile(stdout);
    flockfile(stderr);
    int fd;
    /* A signal resumes the open, as os.open does, unless it interrupted the
     * evaluation. */
    do fd = openat(directory, path, flags | O_CLOEXEC, mode);
    while (fd < 0 && errno == EINTR && !eval_native_interrupted());
    if (fd >= 0 && fd < 3) {
        int moved = fcntl(fd, F_DUPFD_CLOEXEC, 3);
        int saved = errno;
        (void)close(fd);
        fd = moved;
        errno = saved;
    }
    int saved = errno;
    funlockfile(stderr);
    funlockfile(stdout);
    errno = saved;
    return fd;
}

static int open_owned(const char *path, int flags, mode_t mode) {
    return open_owned_at(AT_FDCWD, path, flags, mode);
}

static Atom *open_cursor(CettaLibraryContext *ctx, Arena *arena, const char *path,
                          const char *kind, int flags, mode_t mode) {
    bool directory = kind == directory_kind;
    const char *operation = directory ? "open-directory" :
        kind == writer_kind ? "open-write" : "open-read";
    FsCursor *cursor = calloc(1u, sizeof(*cursor));
    if (!cursor) return os_failed(arena, operation, path, ENOMEM, 0u, true);
    cursor->fd = -1;
    cursor->path = strdup(path);
    if (!cursor->path) {
        release_cursor(cursor);
        return os_failed(arena, operation, path, ENOMEM, 0u, true);
    }
    cursor->fd = open_owned(path, flags, mode);
    if (cursor->fd < 0) goto refused;
    if (directory) {
        cursor->directory = fdopendir(cursor->fd);
        if (!cursor->directory) goto refused;
    }
    uint64_t id;
    if (!cetta_native_handle_alloc(ctx, kind, cursor, release_cursor, &id)) {
        errno = ENOMEM;
        goto refused;
    }
    return cetta_native_handle_owned_atom(ctx, arena, kind, id);
refused: {
    int code = errno;
    release_cursor(cursor);
    return os_failed(arena, operation, path, code, 0u, true);
}
}

static const char *mode_kind(mode_t mode) {
    if (S_ISREG(mode)) return "regular";
    if (S_ISDIR(mode)) return "directory";
    if (S_ISLNK(mode)) return "symlink";
    if (S_ISFIFO(mode)) return "fifo";
    if (S_ISSOCK(mode)) return "socket";
    if (S_ISCHR(mode)) return "character-device";
    if (S_ISBLK(mode)) return "block-device";
    return "unknown";
}

static const char *entry_hint(unsigned char type) {
    switch (type) {
    case DT_REG: return "regular";
    case DT_DIR: return "directory";
    case DT_LNK: return "symlink";
    case DT_FIFO: return "fifo";
    case DT_SOCK: return "socket";
    case DT_CHR: return "character-device";
    case DT_BLK: return "block-device";
    default: return "unknown";
    }
}

/* What a metadata answer adds after the identity: nothing, the modification
 * time, or the permission bits. */
typedef enum { METADATA_PLAIN, METADATA_MODIFIED, METADATA_PERMISSIONS } MetadataExtra;

static Atom *metadata_value(Arena *arena, const struct stat *info,
                             const char *operation, const char *path, uint64_t observed,
                             MetadataExtra extra) {
    if (info->st_size < 0 || (uintmax_t)info->st_size > INT64_MAX)
        return os_failed(arena, operation, path, EOVERFLOW, observed, true);
    /* Device and inode are opaque equality keys, not signed integers. Their
     * decimal byte spellings avoid narrowing the host's unsigned identities. */
    char device[3u * sizeof(uintmax_t) + 1u], inode[sizeof(device)];
    snprintf(device, sizeof(device), "%ju", (uintmax_t)info->st_dev);
    snprintf(inode, sizeof(inode), "%ju", (uintmax_t)info->st_ino);
    Atom *identity = atom_expr3(arena, atom_symbol(arena, "fs:identity"),
        atom_string(arena, device), atom_string(arena, inode));
    if (extra == METADATA_PLAIN)
        return atom_expr(arena, (Atom *[]){atom_symbol(arena, "fs:info"),
            atom_symbol(arena, mode_kind(info->st_mode)), atom_int(arena, info->st_size),
            identity}, 4u);
    /* The *-permissions link modes add the permission bits, stat.S_IMODE of
     * the mode, as a check that a secret is not readable by others needs them. */
    if (extra == METADATA_PERMISSIONS)
        return atom_expr(arena, (Atom *[]){atom_symbol(arena, "fs:info"),
            atom_symbol(arena, mode_kind(info->st_mode)), atom_int(arena, info->st_size),
            identity, atom_expr2(arena, atom_symbol(arena, "fs:permissions"),
                atom_int(arena, (int64_t)(info->st_mode & 07777)))}, 5u);
    /* The *-modified link modes add the modification time, in whole
     * seconds and nanoseconds, as a cache key needs it. */
#ifdef __APPLE__
    struct timespec when = info->st_mtimespec;
#else
    struct timespec when = info->st_mtim;
#endif
    return atom_expr(arena, (Atom *[]){atom_symbol(arena, "fs:info"),
        atom_symbol(arena, mode_kind(info->st_mode)), atom_int(arena, info->st_size),
        identity, atom_expr3(arena, atom_symbol(arena, "fs:modified"),
            atom_int(arena, (int64_t)when.tv_sec), atom_int(arena, (int64_t)when.tv_nsec))}, 5u);
}

static Atom *metadata(Arena *arena, const char *path, bool follow, MetadataExtra extra) {
    struct stat info;
    if ((follow ? stat(path, &info) : lstat(path, &info)) < 0)
        return os_failed(arena, "metadata", path, errno, 0u, true);
    return metadata_value(arena, &info, "metadata", path, 0u, extra);
}

static char *joined_path(const char *parent, const char *name) {
    size_t left = strlen(parent), right = strlen(name);
    bool slash = left && parent[left - 1u] != '/';
    if (right > UINT32_MAX - 1u - (size_t)slash ||
        left > UINT32_MAX - 1u - right - (size_t)slash) {
        errno = EOVERFLOW;
        return NULL;
    }
    char *joined = malloc(left + slash + right + 1u);
    if (!joined) { errno = ENOMEM; return NULL; }
    memcpy(joined, parent, left);
    if (slash) joined[left] = '/';
    memcpy(joined + left + slash, name, right + 1u);
    return joined;
}

static void clear_walk_entry(FsWalk *walk) {
    free(walk->name); free(walk->path); free(walk->relative);
    walk->name = walk->path = walk->relative = NULL;
}

static void release_walk(void *resource) {
    FsWalk *walk = resource;
    while (walk->top) {
        FsWalkDirectory *top = walk->top;
        walk->top = top->parent;
        if (top->directory) (void)closedir(top->directory);
        free(top->path); free(top->relative); free(top);
    }
    clear_walk_entry(walk);
    free(walk->root); free(walk);
}

/* Allocate before opening. Each successful descent adds one owned directory.
 * Check the opened object's identity, not an earlier path observation. Only
 * ancestors are excluded: distinct paths to the same non-ancestor survive. */
static int enter_walk(FsWalk *walk, const char *name, const char *path,
                       const char *relative, bool follow, bool *cycle) {
    *cycle = false;
    FsWalkDirectory *next = calloc(1u, sizeof(*next));
    if (!next) return ENOMEM;
    next->path = strdup(path); next->relative = strdup(relative);
    int code = ENOMEM;
    if (!next->path || !next->relative) goto refused;
    int fd = open_owned_at(walk->top ? dirfd(walk->top->directory) : AT_FDCWD,
                          name, O_RDONLY | O_DIRECTORY | (follow ? 0 : O_NOFOLLOW), 0);
    if (fd < 0) { code = errno; goto refused; }
    struct stat info;
    if (fstat(fd, &info) < 0) { code = errno; (void)close(fd); goto refused; }
    for (FsWalkDirectory *ancestor = walk->top; ancestor; ancestor = ancestor->parent)
        if (ancestor->device == info.st_dev && ancestor->inode == info.st_ino) {
            *cycle = true; code = ELOOP; (void)close(fd); goto refused;
        }
    next->directory = fdopendir(fd);
    if (!next->directory) { code = errno; (void)close(fd); goto refused; }
    next->device = info.st_dev; next->inode = info.st_ino;
    next->parent = walk->top; walk->top = next;
    return 0;
refused:
    free(next->path); free(next->relative); free(next);
    return code;
}

static Atom *open_walk(CettaLibraryContext *ctx, Arena *arena, const char *path) {
    FsWalk *walk = calloc(1u, sizeof(*walk));
    if (!walk) return os_failed(arena, "walk-open", path, ENOMEM, 0u, true);
    walk->root = strdup(path);
    bool cycle;
    int code = walk->root ? enter_walk(walk, path, path, "", true, &cycle) : ENOMEM;
    uint64_t id;
    if (!code && !cetta_native_handle_alloc(ctx, walk_kind, walk, release_walk, &id))
        code = ENOMEM;
    if (code) { release_walk(walk); return os_failed(arena, "walk-open", path, code, 0u, true); }
    return cetta_native_handle_owned_atom(ctx, arena, walk_kind, id);
}

static Atom *walk_current(Arena *arena, FsWalk *walk) {
    if (walk->fault)
        return os_failed(arena, "walk-step", walk->top ? walk->top->path : walk->root,
                         walk->fault, walk->observed, walk->fault_position_known);
    if (walk->ended)
        return atom_expr2(arena, atom_symbol(arena, "fs:end"), atom_int(arena, walk->observed));
    if (!walk->name) return atom_symbol(arena, "fs:walk-ready");
    return atom_expr(arena, (Atom *[]){atom_symbol(arena, "fs:walk-entry"),
        atom_string(arena, walk->path), atom_string(arena, walk->relative),
        atom_string(arena, walk->name), atom_symbol(arena, entry_hint(walk->hint)),
        atom_int(arena, (int64_t)(walk->observed - 1u))}, 6u);
}

static Atom *walk_step(Arena *arena, FsWalk *walk, Atom *action) {
    bool skip = atom_is_symbol(action, "skip");
    bool follow = atom_is_symbol(action, "descend-follow");
    if (!skip && !follow && !atom_is_symbol(action, "descend"))
        return failed(arena, "fs:invalid-argument", "walk-step", walk->root, 0,
                      walk->observed, !walk->fault || walk->fault_position_known,
                      "expected skip, descend or descend-follow");
    if (walk->fault || walk->ended) return walk_current(arena, walk);
    if (!skip) {
        if (!walk->name)
            return failed(arena, "fs:invalid-argument", "walk-step", walk->root, 0,
                          walk->observed, true, "descent requires a pending entry");
        bool cycle;
        int code = enter_walk(walk, walk->name, walk->path, walk->relative, follow, &cycle);
        if (code)
            return failed(arena, cycle ? "fs:cycle" : fault_kind(code), "walk-step",
                          walk->path, code, walk->observed, true, strerror(code));
    }
    clear_walk_entry(walk);
    while (walk->top) {
        if (walk->observed == INT64_MAX) {
            walk->fault = EOVERFLOW; walk->fault_position_known = true; break;
        }
        errno = 0;
        struct dirent *entry = readdir(walk->top->directory);
        if (!entry) {
            if (errno) { walk->fault = errno; break; }
            FsWalkDirectory *top = walk->top;
            int status = closedir(top->directory), code = errno;
            top->directory = NULL;
            if (status < 0) {
                walk->fault = code; walk->fault_position_known = true; break;
            }
            walk->top = top->parent;
            free(top->path); free(top->relative); free(top);
            continue;
        }
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        walk->name = strdup(entry->d_name);
        walk->path = joined_path(walk->top->path, entry->d_name);
        int path_code = walk->path ? 0 : errno;
        walk->relative = joined_path(walk->top->relative, entry->d_name);
        if (!walk->name || !walk->path || !walk->relative) {
            walk->fault = !walk->name ? ENOMEM : path_code ? path_code : errno;
            clear_walk_entry(walk);
            break; /* readdir consumed a name which was not published */
        }
        walk->hint = entry->d_type;
        walk->observed++;
        return walk_current(arena, walk);
    }
    if (!walk->fault) walk->ended = true;
    return walk_current(arena, walk);
}

static Atom *walk_metadata(Arena *arena, FsWalk *walk, bool follow, MetadataExtra extra) {
    if (walk->fault || walk->ended) return walk_current(arena, walk);
    if (!walk->name)
        return failed(arena, "fs:invalid-argument", "walk-metadata", walk->root, 0,
                      walk->observed, true, "metadata requires a pending entry");
    struct stat info;
    if (fstatat(dirfd(walk->top->directory), walk->name, &info,
                follow ? 0 : AT_SYMLINK_NOFOLLOW) < 0)
        return os_failed(arena, "walk-metadata", walk->path, errno, walk->observed, true);
    return metadata_value(arena, &info, "walk-metadata", walk->path, walk->observed, extra);
}

static Atom *close_walk(CettaLibraryContext *ctx, Arena *arena, FsWalk *walk, uint64_t id) {
    Atom *result = atom_symbol(arena, "fs:closed");
    bool failed_close = false;
    for (FsWalkDirectory *top = walk->top; top; top = top->parent) {
        if (top->directory && closedir(top->directory) < 0 && !failed_close) {
            result = os_failed(arena, "close", top->path, errno, walk->observed, true);
            failed_close = true;
        }
        top->directory = NULL;
    }
    (void)cetta_native_handle_close(ctx, walk_kind, id);
    return result;
}

static Atom *next_entry(Arena *arena, FsCursor *cursor) {
    if (cursor->fault)
        return os_failed(arena, "next-entry", cursor->path, cursor->fault,
                         cursor->observed, false);
    if (cursor->directory_end)
        return atom_expr2(arena, atom_symbol(arena, "fs:end"),
                          atom_int(arena, (int64_t)cursor->observed));
    if (cursor->observed == INT64_MAX)
        return os_failed(arena, "next-entry", cursor->path, EOVERFLOW,
                         cursor->observed, true);
    struct dirent *entry;
    for (;;) {
        errno = 0;
        entry = readdir(cursor->directory);
        if (!entry) break;
        if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..")) break;
    }
    if (!entry) {
        if (errno) {
            cursor->fault = errno;
            return os_failed(arena, "next-entry", cursor->path, cursor->fault,
                             cursor->observed, false);
        }
        cursor->directory_end = true;
        return atom_expr2(arena, atom_symbol(arena, "fs:end"),
                          atom_int(arena, (int64_t)cursor->observed));
    }
    return atom_expr(arena, (Atom *[]){atom_symbol(arena, "fs:entry"),
        atom_string(arena, entry->d_name), atom_symbol(arena, entry_hint(entry->d_type)),
        atom_int(arena, (int64_t)cursor->observed++)}, 4u);
}

static Atom *read_chunk(Arena *arena, FsCursor *cursor, int64_t requested) {
    if (cursor->fault)
        return os_failed(arena, "read-chunk", cursor->path, cursor->fault,
                         cursor->observed, false);
    if (!requested)
        return atom_expr3(arena, atom_symbol(arena, "fs:chunk"),
            atom_string(arena, ""), atom_int(arena, (int64_t)cursor->observed));
    /* A chunk is at most one native byte-string's representable length.
     * Large requests are allowed to return a short chunk; this is not EOF. */
    size_t size = (uint64_t)requested > UINT32_MAX ? UINT32_MAX : (size_t)requested;
    if (size > (uint64_t)SSIZE_MAX) size = (size_t)SSIZE_MAX;
    if (size > INT64_MAX - cursor->observed) size = (size_t)(INT64_MAX - cursor->observed);
    if (!size)
        return os_failed(arena, "read-chunk", cursor->path, EOVERFLOW,
                         cursor->observed, true);
    char *bytes = malloc(size);
    if (!bytes) return os_failed(arena, "read-chunk", cursor->path, ENOMEM,
                                 cursor->observed, true);
    ssize_t count = read(cursor->fd, bytes, size);
    if (count < 0) {
        cursor->fault = errno;
        free(bytes);
        return os_failed(arena, "read-chunk", cursor->path, cursor->fault,
                         cursor->observed, false);
    }
    Atom *result = count ? atom_expr3(arena, atom_symbol(arena, "fs:chunk"),
        atom_string_n(arena, bytes, (size_t)count),
        atom_int(arena, (int64_t)cursor->observed))
        : atom_expr2(arena, atom_symbol(arena, "fs:end"),
            atom_int(arena, (int64_t)cursor->observed));
    cursor->observed += (uint64_t)count;
    free(bytes);
    return result;
}

/* Every byte of a text, as a buffered Python stream's write and flush leave
 * it: short writes continue and signals resume. A fault keeps the owner open
 * and reports how many bytes the owner has written in all. */
static Atom *write_all(Arena *arena, FsCursor *cursor, const char *bytes, size_t len) {
    size_t done = 0u;
    while (done < len) {
        size_t size = len - done > (size_t)SSIZE_MAX ? (size_t)SSIZE_MAX : len - done;
        ssize_t count = write(cursor->fd, bytes + done, size);
        if (count < 0 && errno == EINTR && !eval_native_interrupted()) continue;
        if (count <= 0)
            return os_failed(arena, "write-all", cursor->path, count < 0 ? errno : EIO,
                             cursor->observed, true);
        done += (size_t)count;
        cursor->observed += (uint64_t)count;
    }
    return atom_expr2(arena, atom_symbol(arena, "fs:written"), atom_int(arena, (int64_t)len));
}

static Atom *sync_cursor(Arena *arena, FsCursor *cursor) {
    int status;
    do status = fsync(cursor->fd);
    while (status < 0 && errno == EINTR && !eval_native_interrupted());
    return status < 0 ? os_failed(arena, "sync", cursor->path, errno, cursor->observed, true)
                      : atom_symbol(arena, "fs:synced");
}

/* fchmod, as os.fchmod sets an open file's mode, resuming after a signal. */
static Atom *set_mode_cursor(Arena *arena, FsCursor *cursor, mode_t mode) {
    int status;
    do status = fchmod(cursor->fd, mode);
    while (status < 0 && errno == EINTR && !eval_native_interrupted());
    return status < 0 ? os_failed(arena, "set-mode", cursor->path, errno, cursor->observed, true)
                      : atom_symbol(arena, "fs:mode-set");
}

/* A whole-file flock on the owner's open file description, as Python's
 * fcntl.flock takes it: it waits for a conflicting holder, and closing the
 * owner releases it. */
static Atom *lock_cursor(Arena *arena, FsCursor *cursor, int operation) {
    const char *name = operation == LOCK_UN ? "unlock" : "lock";
    int status;
    do status = flock(cursor->fd, operation);
    while (status < 0 && errno == EINTR && !eval_native_interrupted());
    if (status < 0) return os_failed(arena, name, cursor->path, errno, cursor->observed, true);
    return atom_symbol(arena, operation == LOCK_UN ? "fs:unlocked" : "fs:locked");
}

/* os.open's flags for writing, as a list of "append", "create", "truncate" and
 * "exclusive". Strings, since a PeTTa list headed by a function name such as
 * append would be called. */
static bool writer_flags(Atom *list, int *flags) {
    if (!list || list->kind != ATOM_EXPR) return false;
    *flags = O_WRONLY;
    for (CettaExprLen i = 0u; i < list->expr.len; i++) {
        Atom *flag = list->expr.elems[i];
        if (flag->kind != ATOM_GROUNDED || flag->ground.gkind != GV_STRING) return false;
        if (atom_string_equals_cstr(flag, "append")) *flags |= O_APPEND;
        else if (atom_string_equals_cstr(flag, "create")) *flags |= O_CREAT;
        else if (atom_string_equals_cstr(flag, "truncate")) *flags |= O_TRUNC;
        else if (atom_string_equals_cstr(flag, "exclusive")) *flags |= O_EXCL;
        else return false;
    }
    return true;
}

/* os.access's mode: F_OK when no permission is named, else the named "read",
 * "write" and "execute" permissions together. */
static bool access_mode(Atom *list, int *mode) {
    if (!list || list->kind != ATOM_EXPR) return false;
    *mode = F_OK;
    for (CettaExprLen i = 0u; i < list->expr.len; i++) {
        Atom *permission = list->expr.elems[i];
        if (permission->kind != ATOM_GROUNDED || permission->ground.gkind != GV_STRING) return false;
        if (atom_string_equals_cstr(permission, "read")) *mode |= R_OK;
        else if (atom_string_equals_cstr(permission, "write")) *mode |= W_OK;
        else if (atom_string_equals_cstr(permission, "execute")) *mode |= X_OK;
        else return false;
    }
    return true;
}

static bool mode_arg(Atom *value, mode_t *mode) {
    if (!value || value->kind != ATOM_GROUNDED || value->ground.gkind != GV_INT ||
        value->ground.ival < 0 || value->ground.ival > 07777) return false;
    *mode = (mode_t)value->ground.ival;
    return true;
}

/* The path as pathlib spells it: the anchor ("/", or exactly "//"), then the
 * parts without empty or "." parts, or "." when no part remains. */
static char *pathlib_spelling(const char *path) {
    size_t at = 0u, n = 0u;
    char *out = malloc(strlen(path) + 2u);
    if (!out) return NULL;
    if (path[0] == '/') {
        out[n++] = '/';
        if (path[1] == '/' && path[2] != '/') out[n++] = '/';
    }
    size_t root = n;
    while (path[at]) {
        while (path[at] == '/') at++;
        size_t start = at;
        while (path[at] && path[at] != '/') at++;
        size_t part = at - start;
        if (!part || (part == 1u && path[start] == '.')) continue;
        if (n > root) out[n++] = '/';
        memcpy(out + n, path + start, part);
        n += part;
    }
    if (!n) out[n++] = '.';
    out[n] = '\0';
    return out;
}

/* pathlib's parent of such a spelling, or NULL when the path is its own. */
static char *pathlib_parent(const char *path) {
    size_t root = path[0] != '/' ? 0u : path[1] == '/' ? 2u : 1u;
    size_t cut = strlen(path);
    if (cut == root || !strcmp(path, ".")) return NULL;
    while (cut > root && path[cut - 1u] != '/') cut--;
    if (cut == root) return strdup(root == 2u ? "//" : root ? "/" : ".");
    return strndup(path, cut - 1u);
}

static bool is_directory(const char *path) {
    struct stat info;
    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

/* Path.mkdir(mode, parents=True, exist_ok) as Python 3.12 runs it. A missing
 * parent is made with mode 0777 before the path is tried once more; an
 * existing directory is accepted; any other refusal keeps its errno and
 * names the path whose mkdir refused. The umask narrows both modes. */
static int make_directory(const char *path, mode_t mode, bool exist_ok, char **refused) {
    if (mkdir(path, mode) == 0) return 0;
    int code = errno;
    if (code == ENOENT) {
        char *parent = pathlib_parent(path);
        if (!parent) goto refuse;
        int status = make_directory(parent, 0777, true, refused);
        free(parent);
        if (status) return status;
        if (mkdir(path, mode) == 0) return 0;
        code = errno;
        if (code == ENOENT) goto refuse;
    }
    if (exist_ok && is_directory(path)) return 0;
refuse:
    *refused = strdup(path);
    return code;
}

static Atom *make_directories(Arena *arena, const char *path, mode_t mode) {
    char *spelling = pathlib_spelling(path), *refused = NULL;
    if (!spelling) return os_failed(arena, "make-directories", path, ENOMEM, 0u, true);
    int code = make_directory(spelling, mode, true, &refused);
    Atom *value = code ? os_failed(arena, "make-directories", refused ? refused : spelling,
                                   code, 0u, true) : atom_unit(arena);
    free(refused);
    free(spelling);
    return value;
}

/* follow and no-follow, each also with a -modified variant that adds the
 * modification time to the answer and a -permissions variant that adds the
 * permission bits. */
static bool link_mode(Atom *atom, bool *follow, MetadataExtra *extra) {
    static const struct { const char *name; bool follow; MetadataExtra extra; } modes[] = {
        {"follow", true, METADATA_PLAIN}, {"no-follow", false, METADATA_PLAIN},
        {"follow-modified", true, METADATA_MODIFIED}, {"no-follow-modified", false, METADATA_MODIFIED},
        {"follow-permissions", true, METADATA_PERMISSIONS},
        {"no-follow-permissions", false, METADATA_PERMISSIONS},
    };
    for (size_t i = 0u; i < sizeof(modes) / sizeof(modes[0]); i++)
        if (atom_is_symbol(atom, modes[i].name)) {
            *follow = modes[i].follow;
            *extra = modes[i].extra;
            return true;
        }
    return false;
}

bool cetta_fs_call(CettaLibraryContext *ctx, Arena *arena, Atom *head,
                   Atom **args, uint32_t nargs, CettaCallOutcome *out) {
    if (!head || head->kind != ATOM_SYMBOL) return false;
    SymbolId op = atom_symbol_current_id(head);
    const char *name = op == g_builtin_syms.lib_fs_metadata ? "metadata" :
        op == g_builtin_syms.lib_fs_rename ? "rename" :
        op == g_builtin_syms.lib_fs_walk_open ? "walk-open" :
        op == g_builtin_syms.lib_fs_walk_current ? "walk-current" :
        op == g_builtin_syms.lib_fs_walk_step ? "walk-step" :
        op == g_builtin_syms.lib_fs_walk_metadata ? "walk-metadata" :
        op == g_builtin_syms.lib_fs_open_directory ? "open-directory" :
        op == g_builtin_syms.lib_fs_next_entry ? "next-entry" :
        op == g_builtin_syms.lib_fs_open_read ? "open-read" :
        op == g_builtin_syms.lib_fs_read_chunk ? "read-chunk" :
        op == g_builtin_syms.lib_fs_open_write ? "open-write" :
        op == g_builtin_syms.lib_fs_write_all ? "write-all" :
        op == g_builtin_syms.lib_fs_sync ? "sync" :
        op == g_builtin_syms.lib_fs_lock ? "lock" :
        op == g_builtin_syms.lib_fs_unlock ? "unlock" :
        op == g_builtin_syms.lib_fs_make_directories ? "make-directories" :
        op == g_builtin_syms.lib_fs_set_mode ? "set-mode" :
        op == g_builtin_syms.lib_fs_remove ? "remove" :
        op == g_builtin_syms.lib_fs_access ? "access" :
        op == g_builtin_syms.lib_fs_close ? "close" : NULL;
    if (!name) return false;
    if (eval_native_interrupted()) { *out = cetta_call_interrupted(); return true; }
    Atom *value;
    uint32_t arity = op == g_builtin_syms.lib_fs_open_write ? 3u :
        op == g_builtin_syms.lib_fs_metadata || op == g_builtin_syms.lib_fs_rename ||
        op == g_builtin_syms.lib_fs_read_chunk || op == g_builtin_syms.lib_fs_walk_step ||
        op == g_builtin_syms.lib_fs_walk_metadata || op == g_builtin_syms.lib_fs_write_all ||
        op == g_builtin_syms.lib_fs_lock || op == g_builtin_syms.lib_fs_make_directories ||
        op == g_builtin_syms.lib_fs_set_mode || op == g_builtin_syms.lib_fs_access ? 2u : 1u;
    const char *path = nargs ? path_arg(args[0]) : NULL;
    uint64_t observed = 0u;
    bool position_known = true;
    if (nargs != arity) goto argument;
    if (op == g_builtin_syms.lib_fs_rename) {
        const char *target = path_arg(args[1]);
        if (!path || !target) goto argument;
        value = rename(path, target) == 0 ? atom_unit(arena)
            : os_failed(arena, name, path, errno, 0u, true);
        goto completed;
    }
    /* os.unlink, and os.access, which reports refusal as False, never as an
     * error. */
    if (op == g_builtin_syms.lib_fs_remove) {
        if (!path) goto argument;
        value = unlink(path) == 0 ? atom_unit(arena)
            : os_failed(arena, name, path, errno, 0u, true);
        goto completed;
    }
    if (op == g_builtin_syms.lib_fs_access) {
        int permissions;
        if (!path || !access_mode(args[1], &permissions)) goto argument;
        value = access(path, permissions) == 0 ? atom_true(arena) : atom_false(arena);
        goto completed;
    }
    if (op == g_builtin_syms.lib_fs_open_write || op == g_builtin_syms.lib_fs_make_directories) {
        int flags = 0;
        mode_t mode;
        bool writer = op == g_builtin_syms.lib_fs_open_write;
        if (!path || (writer && !writer_flags(args[1], &flags)) ||
            !mode_arg(args[writer ? 2 : 1], &mode)) goto argument;
        value = writer ? open_cursor(ctx, arena, path, writer_kind, flags, mode)
                       : make_directories(arena, path, mode);
        goto completed;
    }
    if (op == g_builtin_syms.lib_fs_metadata || op == g_builtin_syms.lib_fs_open_read ||
        op == g_builtin_syms.lib_fs_open_directory || op == g_builtin_syms.lib_fs_walk_open) {
        if (!path) goto argument;
        if (op == g_builtin_syms.lib_fs_metadata) {
            bool follow;
            MetadataExtra extra;
            if (!link_mode(args[1], &follow, &extra)) goto argument;
            value = metadata(arena, path, follow, extra);
        } else if (op == g_builtin_syms.lib_fs_walk_open) value = open_walk(ctx, arena, path);
        else if (op == g_builtin_syms.lib_fs_open_directory)
            value = open_cursor(ctx, arena, path, directory_kind, O_RDONLY | O_DIRECTORY, 0);
        else value = open_cursor(ctx, arena, path, file_kind, O_RDONLY, 0);
        goto completed;
    }
    uint64_t id;
    if (op == g_builtin_syms.lib_fs_walk_current || op == g_builtin_syms.lib_fs_walk_step ||
        op == g_builtin_syms.lib_fs_walk_metadata ||
        (op == g_builtin_syms.lib_fs_close && cetta_native_handle_arg(args[0], walk_kind, &id))) {
        if (!cetta_native_handle_arg(args[0], walk_kind, &id)) goto handle;
        FsWalk *walk = cetta_native_handle_get(ctx, walk_kind, id);
        if (!walk) goto handle;
        path = walk->root; observed = walk->observed;
        position_known = !walk->fault || walk->fault_position_known;
        if (op == g_builtin_syms.lib_fs_close) value = close_walk(ctx, arena, walk, id);
        else if (op == g_builtin_syms.lib_fs_walk_step) value = walk_step(arena, walk, args[1]);
        else if (op == g_builtin_syms.lib_fs_walk_metadata) {
            bool follow;
            MetadataExtra extra;
            if (!link_mode(args[1], &follow, &extra)) goto argument;
            value = walk_metadata(arena, walk, follow, extra);
        } else value = walk_current(arena, walk);
        goto completed;
    }
    const char *kind = op == g_builtin_syms.lib_fs_next_entry ? directory_kind :
        op == g_builtin_syms.lib_fs_write_all ? writer_kind : file_kind;
    /* Closing, syncing, mode setting and locking take a reader or a writer alike. */
    if ((op == g_builtin_syms.lib_fs_close || op == g_builtin_syms.lib_fs_sync ||
         op == g_builtin_syms.lib_fs_set_mode ||
         op == g_builtin_syms.lib_fs_lock || op == g_builtin_syms.lib_fs_unlock) &&
        cetta_native_handle_arg(args[0], writer_kind, &id))
        kind = writer_kind;
    if (op == g_builtin_syms.lib_fs_close && cetta_native_handle_arg(args[0], directory_kind, &id))
        kind = directory_kind;
    if (!cetta_native_handle_arg(args[0], kind, &id)) goto handle;
    FsCursor *cursor = cetta_native_handle_get(ctx, kind, id);
    if (!cursor) goto handle;
    path = cursor->path;
    observed = cursor->observed;
    position_known = !cursor->fault;
    if (op == g_builtin_syms.lib_fs_close) {
        int status = cursor->directory ? closedir(cursor->directory) : close(cursor->fd);
        int code = errno;
        cursor->directory = NULL;
        cursor->fd = -1;
        value = status < 0 ? os_failed(arena, name, cursor->path, code, cursor->observed, true)
                          : atom_symbol(arena, "fs:closed");
        (void)cetta_native_handle_close(ctx, kind, id);
    } else if (op == g_builtin_syms.lib_fs_next_entry) value = next_entry(arena, cursor);
    else if (op == g_builtin_syms.lib_fs_write_all) {
        const char *text = NULL;
        size_t len = 0u;
        if (args[1]->kind == ATOM_GROUNDED && args[1]->ground.gkind == GV_STRING) {
            text = args[1]->ground.sval;
            len = atom_string_len(args[1]);
        } else if (args[1]->kind == ATOM_SYMBOL && (text = atom_name_cstr(args[1]))) {
            len = strlen(text);
        }
        if (!text) goto argument;
        value = write_all(arena, cursor, text, len);
    } else if (op == g_builtin_syms.lib_fs_sync) value = sync_cursor(arena, cursor);
    else if (op == g_builtin_syms.lib_fs_set_mode) {
        mode_t mode;
        if (!mode_arg(args[1], &mode)) goto argument;
        value = set_mode_cursor(arena, cursor, mode);
    }
    else if (op == g_builtin_syms.lib_fs_lock) {
        bool exclusive = atom_is_symbol(args[1], "exclusive");
        if (!exclusive && !atom_is_symbol(args[1], "shared")) goto argument;
        value = lock_cursor(arena, cursor, exclusive ? LOCK_EX : LOCK_SH);
    } else if (op == g_builtin_syms.lib_fs_unlock) value = lock_cursor(arena, cursor, LOCK_UN);
    else {
        if (args[1]->kind != ATOM_GROUNDED || args[1]->ground.gkind != GV_INT ||
            args[1]->ground.ival < 0) goto argument;
        value = read_chunk(arena, cursor, args[1]->ground.ival);
    }
    goto completed;
handle:
    value = failed(arena, "fs:invalid-handle", name, "", 0, 0u, false,
                   "expected a live filesystem owner of the requested kind");
    goto completed;
argument:
    value = failed(arena, "fs:invalid-argument", name, path, 0, observed, position_known,
                   "expected a path, owner, nonnegative byte count, text, write flags, "
                   "access permissions, mode, lock mode or follow/no-follow (or -modified, -permissions) policy");
completed:
    *out = cetta_call_completed_value(value);
    return true;
}

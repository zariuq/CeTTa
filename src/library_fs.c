#define _GNU_SOURCE
#include "library_fs.h"
#include "library.h"
#include "eval.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
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
static int open_owned_at(int directory, const char *path, int flags) {
    flockfile(stdout);
    flockfile(stderr);
    int fd = openat(directory, path, flags | O_CLOEXEC);
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

static int open_owned(const char *path, int flags) {
    return open_owned_at(AT_FDCWD, path, flags);
}

static Atom *open_cursor(CettaLibraryContext *ctx, Arena *arena,
                          const char *path, bool directory) {
    const char *operation = directory ? "open-directory" : "open-read";
    FsCursor *cursor = calloc(1u, sizeof(*cursor));
    if (!cursor) return os_failed(arena, operation, path, ENOMEM, 0u, true);
    cursor->fd = -1;
    cursor->path = strdup(path);
    if (!cursor->path) {
        release_cursor(cursor);
        return os_failed(arena, operation, path, ENOMEM, 0u, true);
    }
    cursor->fd = open_owned(path, O_RDONLY | (directory ? O_DIRECTORY : 0));
    if (cursor->fd < 0) goto refused;
    if (directory) {
        cursor->directory = fdopendir(cursor->fd);
        if (!cursor->directory) goto refused;
    }
    uint64_t id;
    const char *kind = directory ? directory_kind : file_kind;
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

static Atom *metadata_value(Arena *arena, const struct stat *info,
                             const char *operation, const char *path, uint64_t observed) {
    if (info->st_size < 0 || (uintmax_t)info->st_size > INT64_MAX)
        return os_failed(arena, operation, path, EOVERFLOW, observed, true);
    /* Device and inode are opaque equality keys, not signed integers. Their
     * decimal byte spellings avoid narrowing the host's unsigned identities. */
    char device[3u * sizeof(uintmax_t) + 1u], inode[sizeof(device)];
    snprintf(device, sizeof(device), "%ju", (uintmax_t)info->st_dev);
    snprintf(inode, sizeof(inode), "%ju", (uintmax_t)info->st_ino);
    return atom_expr(arena, (Atom *[]){atom_symbol(arena, "fs:info"),
        atom_symbol(arena, mode_kind(info->st_mode)), atom_int(arena, info->st_size),
        atom_expr3(arena, atom_symbol(arena, "fs:identity"),
            atom_string(arena, device), atom_string(arena, inode))}, 4u);
}

static Atom *metadata(Arena *arena, const char *path, bool follow) {
    struct stat info;
    if ((follow ? stat(path, &info) : lstat(path, &info)) < 0)
        return os_failed(arena, "metadata", path, errno, 0u, true);
    return metadata_value(arena, &info, "metadata", path, 0u);
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
                          name, O_RDONLY | O_DIRECTORY | (follow ? 0 : O_NOFOLLOW));
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

static Atom *walk_metadata(Arena *arena, FsWalk *walk, bool follow) {
    if (walk->fault || walk->ended) return walk_current(arena, walk);
    if (!walk->name)
        return failed(arena, "fs:invalid-argument", "walk-metadata", walk->root, 0,
                      walk->observed, true, "metadata requires a pending entry");
    struct stat info;
    if (fstatat(dirfd(walk->top->directory), walk->name, &info,
                follow ? 0 : AT_SYMLINK_NOFOLLOW) < 0)
        return os_failed(arena, "walk-metadata", walk->path, errno, walk->observed, true);
    return metadata_value(arena, &info, "walk-metadata", walk->path, walk->observed);
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

bool cetta_fs_call(CettaLibraryContext *ctx, Arena *arena, Atom *head,
                   Atom **args, uint32_t nargs, CettaCallOutcome *out) {
    if (!head || head->kind != ATOM_SYMBOL) return false;
    SymbolId op = head->sym_id;
    const char *name = op == g_builtin_syms.lib_fs_metadata ? "metadata" :
        op == g_builtin_syms.lib_fs_walk_open ? "walk-open" :
        op == g_builtin_syms.lib_fs_walk_current ? "walk-current" :
        op == g_builtin_syms.lib_fs_walk_step ? "walk-step" :
        op == g_builtin_syms.lib_fs_walk_metadata ? "walk-metadata" :
        op == g_builtin_syms.lib_fs_open_directory ? "open-directory" :
        op == g_builtin_syms.lib_fs_next_entry ? "next-entry" :
        op == g_builtin_syms.lib_fs_open_read ? "open-read" :
        op == g_builtin_syms.lib_fs_read_chunk ? "read-chunk" :
        op == g_builtin_syms.lib_fs_close ? "close" : NULL;
    if (!name) return false;
    if (eval_native_interrupted()) { *out = cetta_call_interrupted(); return true; }
    Atom *value;
    uint32_t arity = op == g_builtin_syms.lib_fs_metadata ||
        op == g_builtin_syms.lib_fs_read_chunk || op == g_builtin_syms.lib_fs_walk_step ||
        op == g_builtin_syms.lib_fs_walk_metadata ? 2u : 1u;
    const char *path = nargs ? path_arg(args[0]) : NULL;
    uint64_t observed = 0u;
    bool position_known = true;
    if (nargs != arity) goto argument;
    if (op == g_builtin_syms.lib_fs_metadata || op == g_builtin_syms.lib_fs_open_read ||
        op == g_builtin_syms.lib_fs_open_directory || op == g_builtin_syms.lib_fs_walk_open) {
        if (!path) goto argument;
        if (op == g_builtin_syms.lib_fs_metadata) {
            if (!atom_is_symbol(args[1], "follow") && !atom_is_symbol(args[1], "no-follow"))
                goto argument;
            value = metadata(arena, path, atom_is_symbol(args[1], "follow"));
        } else if (op == g_builtin_syms.lib_fs_walk_open) value = open_walk(ctx, arena, path);
        else value = open_cursor(ctx, arena, path, op == g_builtin_syms.lib_fs_open_directory);
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
            if (!atom_is_symbol(args[1], "follow") && !atom_is_symbol(args[1], "no-follow"))
                goto argument;
            value = walk_metadata(arena, walk, atom_is_symbol(args[1], "follow"));
        } else value = walk_current(arena, walk);
        goto completed;
    }
    const char *kind = op == g_builtin_syms.lib_fs_next_entry ? directory_kind : file_kind;
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
                   "expected a path, owner, nonnegative byte count or follow/no-follow policy");
completed:
    *out = cetta_call_completed_value(value);
    return true;
}

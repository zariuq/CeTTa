#define _GNU_SOURCE
#include "library_fs.h"
#include "library.h"
#include "stats.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static bool interrupted, read_fault, directory_fault, unknown_hint, allocation_fault;
static unsigned reads, checks;
static int last_read_fd;
static int last_directory_fd;
static unsigned directory_reads;
static const char *refused_strdup;
static bool metadata_missing, directory_close_fault;
static bool rename_cross_device;

int __real_rename(const char *source, const char *target);
int __wrap_rename(const char *source, const char *target) {
    if (rename_cross_device) { errno = EXDEV; return -1; }
    return __real_rename(source, target);
}

bool eval_native_interrupted(void) { return interrupted; }
ssize_t __real_read(int fd, void *bytes, size_t size);
ssize_t __wrap_read(int fd, void *bytes, size_t size) {
    reads++;
    last_read_fd = fd;
    if (read_fault) { errno = EIO; return -1; }
    return __real_read(fd, bytes, size);
}
/* Fortified builds may lower read to the checked libc entry point. Inject
 * through that actual boundary too, without disabling the build's checks. */
ssize_t __real___read_chk(int fd, void *bytes, size_t size, size_t available);
ssize_t __wrap___read_chk(int fd, void *bytes, size_t size, size_t available) {
    reads++;
    last_read_fd = fd;
    if (read_fault) { errno = EIO; return -1; }
    return __real___read_chk(fd, bytes, size, available);
}
struct dirent *__real_readdir(DIR *directory);
struct dirent *__wrap_readdir(DIR *directory) {
    last_directory_fd = dirfd(directory);
    directory_reads++;
    if (directory_fault) { errno = EIO; return NULL; }
    struct dirent *entry = __real_readdir(directory);
    if (entry && unknown_hint) entry->d_type = DT_UNKNOWN;
    return entry;
}
char *__real_strdup(const char *text);
char *__wrap_strdup(const char *text) {
    if (refused_strdup && !strcmp(text, refused_strdup)) {
        refused_strdup = NULL; errno = ENOMEM; return NULL;
    }
    return __real_strdup(text);
}
int __real_fstatat(int fd, const char *name, struct stat *info, int flags);
int __wrap_fstatat(int fd, const char *name, struct stat *info, int flags) {
    if (metadata_missing) { errno = ENOENT; return -1; }
    return __real_fstatat(fd, name, info, flags);
}
int __real_closedir(DIR *directory);
int __wrap_closedir(DIR *directory) {
    int status = __real_closedir(directory);
    if (directory_close_fault) { directory_close_fault = false; errno = EIO; return -1; }
    return status;
}
void *__real_malloc(size_t size);
void *__wrap_malloc(size_t size) {
    if (allocation_fault && size == 17u) {
        allocation_fault = false;
        errno = ENOMEM;
        return NULL;
    }
    return __real_malloc(size);
}

#define check(condition) do { \
    checks++; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL at line %d: %s\n", __LINE__, #condition); \
        abort(); \
    } \
} while (0)

static Atom *call(CettaLibraryContext *ctx, Arena *arena, const char *operation,
                   Atom **args, uint32_t nargs) {
    char name[100];
    snprintf(name, sizeof(name), "__cetta_lib_fs_%s", operation);
    CettaCallOutcome outcome;
    check(cetta_fs_call(ctx, arena, atom_symbol(arena, name), args, nargs, &outcome));
    check(outcome.kind == CETTA_CALL_VALUE);
    check(outcome.result_form == CETTA_CALL_RESULT_COMPLETED_VALUE);
    return outcome.term;
}

static bool headed(Atom *value, const char *name, uint32_t arity) {
    return value && value->kind == ATOM_EXPR && value->expr.len == arity &&
        atom_is_symbol(value->expr.elems[0], name);
}

static Atom *open_reader(CettaLibraryContext *ctx, Arena *arena, const char *path) {
    Atom *arg = atom_string(arena, path);
    return call(ctx, arena, "open_read", &arg, 1u);
}

static Atom *chunk(CettaLibraryContext *ctx, Arena *arena, Atom *owner, int64_t size) {
    return call(ctx, arena, "read_chunk", (Atom *[]){owner, atom_int(arena, size)}, 2u);
}

static void close_reader(CettaLibraryContext *ctx, Arena *arena, Atom *owner) {
    check(atom_is_symbol(call(ctx, arena, "close", &owner, 1u), "fs:closed"));
}

static void rename_controls(CettaLibraryContext *ctx, Arena *arena, const char *base) {
    char source[4096], target[4096];
    assert(snprintf(source, sizeof(source), "%s/rename-source", base) < (int)sizeof(source));
    assert(snprintf(target, sizeof(target), "%s/rename-target", base) < (int)sizeof(target));
    int fd = open(source, O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0600);
    assert(fd >= 0 && write(fd, "n\0w", 3u) == 3 && fchmod(fd, 0640) == 0);
    assert(close(fd) == 0);
    fd = open(target, O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0600);
    assert(fd >= 0 && write(fd, "old", 3u) == 3 && close(fd) == 0);
    Atom *old = open_reader(ctx, arena, target);
    Atom *args[] = {atom_string(arena, source), atom_string(arena, target)};
    CettaCallOutcome outcome;
    interrupted = true;
    check(cetta_fs_call(ctx, arena, atom_symbol(arena, "__cetta_lib_fs_rename"), args, 2u, &outcome));
    check(outcome.kind == CETTA_CALL_INTERRUPTED && access(source, F_OK) == 0);
    interrupted = false;
    rename_cross_device = true;
    Atom *refused = call(ctx, arena, "rename", args, 2u);
    check(headed(refused, "fs:failed", 7u) && refused->expr.elems[4]->ground.ival == EXDEV);
    check(access(source, F_OK) == 0);
    rename_cross_device = false;
    Atom *result = call(ctx, arena, "rename", args, 2u);
    check(result->kind == ATOM_EXPR && result->expr.len == 0u);
    check(access(source, F_OK) == -1 && errno == ENOENT);
    struct stat info;
    check(stat(target, &info) == 0 && (info.st_mode & 0777) == 0640);
    check(atom_string_equals_cstr(chunk(ctx, arena, old, 3)->expr.elems[1], "old"));
    close_reader(ctx, arena, old);
    Atom *current = open_reader(ctx, arena, target);
    Atom *bytes = chunk(ctx, arena, current, 4)->expr.elems[1];
    check(atom_string_len(bytes) == 3u && memcmp(bytes->ground.sval, "n\0w", 3u) == 0);
    close_reader(ctx, arena, current);
    refused = call(ctx, arena, "rename", args, 2u);
    check(headed(refused, "fs:failed", 7u) && atom_is_symbol(refused->expr.elems[1], "fs:missing"));
    args[0] = atom_string(arena, target);
    args[1] = atom_string_n(arena, "bad\0path", 8u);
    refused = call(ctx, arena, "rename", args, 2u);
    check(headed(refused, "fs:failed", 7u) && atom_is_symbol(refused->expr.elems[1], "fs:invalid-argument"));
    check(access(target, F_OK) == 0);
}

static Atom *walk_advance(CettaLibraryContext *ctx, Arena *arena, Atom *owner, const char *action) {
    return call(ctx, arena, "walk_step", (Atom *[]){owner, atom_symbol(arena, action)}, 2u);
}

static Atom *walk_find(CettaLibraryContext *ctx, Arena *arena, Atom *owner, const char *name) {
    for (unsigned i = 0; i < 1000u; i++) {
        Atom *entry = walk_advance(ctx, arena, owner, "skip");
        check(headed(entry, "fs:walk-entry", 6u));
        if (atom_string_equals_cstr(entry->expr.elems[3], name)) return entry;
    }
    abort();
}

static void walk_controls(CettaLibraryContext *ctx, Arena *arena, Arena *survivor, const char *base) {
    char root[4096], child[4096], leaf[4096], cycle[4096], deep[4096];
    assert(snprintf(root, sizeof(root), "%s/walk", base) < (int)sizeof(root));
    assert(snprintf(child, sizeof(child), "%s/child", root) < (int)sizeof(child));
    assert(snprintf(leaf, sizeof(leaf), "%s/leaf", child) < (int)sizeof(leaf));
    assert(snprintf(cycle, sizeof(cycle), "%s/back", child) < (int)sizeof(cycle));
    assert(mkdir(root, 0700) == 0 || errno == EEXIST);
    assert(mkdir(child, 0700) == 0 || errno == EEXIST);
    int fd = open(leaf, O_WRONLY | O_CREAT | O_CLOEXEC, 0600);
    assert(fd >= 0); assert(close(fd) == 0);
    assert(symlink("..", cycle) == 0 || errno == EEXIST);
    Atom *path = atom_string(arena, root);
    Atom *owner = call(ctx, arena, "walk_open", &path, 1u);
    check(atom_is_symbol(call(ctx, arena, "walk_current", &owner, 1u), "fs:walk-ready"));
    CettaCallOutcome interrupted_step;
    Atom *step_args[] = {owner, atom_symbol(arena, "skip")};
    interrupted = true;
    check(cetta_fs_call(ctx, arena, atom_symbol(arena, "__cetta_lib_fs_walk_step"),
                       step_args, 2u, &interrupted_step));
    check(interrupted_step.kind == CETTA_CALL_INTERRUPTED);
    interrupted = false;
    check(atom_is_symbol(call(ctx, arena, "walk_current", &owner, 1u), "fs:walk-ready"));
    Atom *second = call(ctx, arena, "walk_open", &path, 1u);
    Atom *entry = walk_find(ctx, arena, owner, "child");
    int root_fd = last_directory_fd;
    check(root_fd >= 3 && (fcntl(root_fd, F_GETFD) & FD_CLOEXEC));
    check(atom_string_equals_cstr(entry->expr.elems[2], "child"));
    unsigned before = directory_reads;
    check(atom_eq(entry, call(ctx, arena, "walk_current", &owner, 1u)));
    check(directory_reads == before);
    metadata_missing = true;
    Atom *missing = call(ctx, arena, "walk_metadata", (Atom *[]){owner, atom_symbol(arena, "no-follow")}, 2u);
    check(headed(missing, "fs:failed", 7u) && atom_is_symbol(missing->expr.elems[1], "fs:missing"));
    check(atom_is_symbol(missing->expr.elems[5]->expr.elems[2], "known-position"));
    metadata_missing = false;
    check(atom_eq(entry, call(ctx, arena, "walk_current", &owner, 1u)));
    Atom *info = call(ctx, arena, "walk_metadata", (Atom *[]){owner, atom_symbol(arena, "no-follow")}, 2u);
    check(headed(info, "fs:info", 4u) && atom_is_symbol(info->expr.elems[1], "directory"));
    /* Allocation before descent leaves the pending decision intact. */
    refused_strdup = child;
    Atom *failure = walk_advance(ctx, arena, owner, "descend");
    check(headed(failure, "fs:failed", 7u) && atom_is_symbol(failure->expr.elems[1], "fs:capacity"));
    check(atom_eq(entry, call(ctx, arena, "walk_current", &owner, 1u)));
    unknown_hint = true;
    entry = walk_advance(ctx, arena, owner, "descend");
    int child_fd = last_directory_fd;
    check(child_fd != root_fd && child_fd >= 3 && (fcntl(child_fd, F_GETFD) & FD_CLOEXEC));
    check(headed(entry, "fs:walk-entry", 6u) && atom_is_symbol(entry->expr.elems[4], "unknown"));
    unknown_hint = false;
    if (!atom_string_equals_cstr(entry->expr.elems[3], "back")) entry = walk_find(ctx, arena, owner, "back");
    info = call(ctx, arena, "walk_metadata", (Atom *[]){owner, atom_symbol(arena, "no-follow")}, 2u);
    check(atom_is_symbol(info->expr.elems[1], "symlink"));
    failure = walk_advance(ctx, arena, owner, "descend-follow");
    check(headed(failure, "fs:failed", 7u) && atom_is_symbol(failure->expr.elems[1], "fs:cycle"));
    check(atom_is_symbol(failure->expr.elems[5]->expr.elems[2], "known-position"));
    /* A caller can explicitly skip a declined descent. It is not erased by
     * the provider or confused with end. Independent owners still start anew. */
    Atom *alias = atom_deep_copy(survivor, owner);
    directory_close_fault = true;
    failure = call(ctx, arena, "close", &alias, 1u);
    check(headed(failure, "fs:failed", 7u) && failure->expr.elems[4]->ground.ival == EIO);
    check(fcntl(root_fd, F_GETFD) == -1 && errno == EBADF);
    check(fcntl(child_fd, F_GETFD) == -1 && errno == EBADF);
    check(headed(walk_advance(ctx, arena, owner, "skip"), "fs:failed", 7u));
    entry = walk_find(ctx, arena, second, "child");
    check(entry->expr.elems[5]->ground.ival == 0);
    close_reader(ctx, arena, second);

    owner = call(ctx, arena, "walk_open", &path, 1u);
    refused_strdup = "child";
    failure = walk_advance(ctx, arena, owner, "skip");
    check(headed(failure, "fs:failed", 7u) && atom_is_symbol(failure->expr.elems[1], "fs:capacity"));
    check(atom_is_symbol(failure->expr.elems[5]->expr.elems[2], "unknown-position"));
    before = directory_reads;
    check(headed(walk_advance(ctx, arena, owner, "skip"), "fs:failed", 7u));
    check(directory_reads == before);
    close_reader(ctx, arena, owner);

    owner = call(ctx, arena, "walk_open", &path, 1u);
    directory_fault = true;
    check(headed(walk_advance(ctx, arena, owner, "skip"), "fs:failed", 7u));
    directory_fault = false;
    check(headed(walk_advance(ctx, arena, owner, "skip"), "fs:failed", 7u));
    close_reader(ctx, arena, owner);

    /* The explicit heap stack, not C recursion, carries a deep traversal. */
    assert(snprintf(deep, sizeof(deep), "%s/deep", base) < (int)sizeof(deep));
    assert(mkdir(deep, 0700) == 0 || errno == EEXIST);
    fd = open(deep, O_RDONLY | O_DIRECTORY | O_CLOEXEC); assert(fd >= 0);
    for (unsigned i = 0; i < 128u; i++) {
        assert(mkdirat(fd, "d", 0700) == 0 || errno == EEXIST);
        int next = openat(fd, "d", O_RDONLY | O_DIRECTORY | O_CLOEXEC); assert(next >= 0);
        assert(close(fd) == 0); fd = next;
    }
    assert(close(fd) == 0);
    path = atom_string(arena, deep);
    owner = call(ctx, arena, "walk_open", &path, 1u);
    entry = walk_advance(ctx, arena, owner, "skip");
    for (unsigned i = 0; i < 128u; i++) {
        check(headed(entry, "fs:walk-entry", 6u) && entry->expr.elems[5]->ground.ival == i);
        entry = walk_advance(ctx, arena, owner, "descend");
    }
    check(headed(entry, "fs:end", 2u) && entry->expr.elems[1]->ground.ival == 128);
    check(atom_eq(entry, walk_advance(ctx, arena, owner, "skip")));
    close_reader(ctx, arena, owner);

    path = atom_string(arena, root);
    owner = call(ctx, arena, "walk_open", &path, 1u);
    (void)walk_find(ctx, arena, owner, "child"); root_fd = last_directory_fd;
    entry = walk_advance(ctx, arena, owner, "descend"); child_fd = last_directory_fd;
    alias = atom_deep_copy(survivor, owner);
    Atom *copied_entry = atom_deep_copy(survivor, entry);
    arena_free(arena); arena_init_detached(arena);
    check(atom_eq(copied_entry, call(ctx, arena, "walk_current", &alias, 1u)));
    check(fcntl(root_fd, F_GETFD) >= 0 && fcntl(child_fd, F_GETFD) >= 0);
    arena_free(survivor); arena_init_detached(survivor);
    check(fcntl(root_fd, F_GETFD) == -1 && errno == EBADF);
    check(fcntl(child_fd, F_GETFD) == -1 && errno == EBADF);
    path = atom_string(arena, root);
    owner = call(ctx, arena, "walk_open", &path, 1u);
    (void)walk_advance(ctx, arena, owner, "skip"); root_fd = last_directory_fd;
    cetta_native_handle_cleanup_all(ctx);
    check(fcntl(root_fd, F_GETFD) == -1 && errno == EBADF);
    check(headed(call(ctx, arena, "walk_current", &owner, 1u), "fs:failed", 7u));
}

int main(int argc, char **argv) {
    assert(argc == 2);
#if CETTA_BUILD_WITH_RUNTIME_STATS
    cetta_runtime_stats_enable();
#endif
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    CettaLibraryContext *ctx = calloc(1u, sizeof(*ctx));
    ctx->native_handle_next_id = 1u;
    Arena arena, survivor;
    arena_init_detached(&arena);
    arena_init_detached(&survivor);
#if CETTA_BUILD_WITH_RUNTIME_STATS
    arena_set_runtime_kind(&arena, CETTA_ARENA_RUNTIME_KIND_EVAL);
#endif
    char file[4096], link[4096];
    snprintf(file, sizeof(file), "%s/bytes", argv[1]);
    snprintf(link, sizeof(link), "%s/link", argv[1]);
    int fd = open(file, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    assert(fd >= 0 && write(fd, "a\0\xffz", 4u) == 4);
    assert(close(fd) == 0);
    if (symlink("bytes", link) < 0) assert(errno == EEXIST);
    walk_controls(ctx, &arena, &survivor, argv[1]);
    Atom *owner = open_reader(ctx, &arena, file);
    Atom *second = open_reader(ctx, &arena, file);
    Atom *zero = chunk(ctx, &arena, owner, 0);
    check(headed(zero, "fs:chunk", 3u) && atom_string_len(zero->expr.elems[1]) == 0u);
    check(reads == 0u);
    Atom *first = chunk(ctx, &arena, owner, 2);
    check(headed(first, "fs:chunk", 3u));
    check(atom_string_len(first->expr.elems[1]) == 2u &&
          memcmp(first->expr.elems[1]->ground.sval, "a\0", 2u) == 0);
    int flags = fcntl(last_read_fd, F_GETFD);
    check(flags >= 0 && (flags & FD_CLOEXEC));
    Atom *alias = atom_deep_copy(&survivor, owner);
    Atom *tail = chunk(ctx, &arena, alias, 9);
    check(atom_string_len(tail->expr.elems[1]) == 2u &&
          memcmp(tail->expr.elems[1]->ground.sval, "\xffz", 2u) == 0);
    check(tail->expr.elems[2]->ground.ival == 2);
    check(atom_string_len(chunk(ctx, &arena, second, 1)->expr.elems[1]) == 1u);
    check(headed(chunk(ctx, &arena, alias, 1), "fs:end", 2u));
    check(headed(chunk(ctx, &arena, alias, 0), "fs:chunk", 3u));
    close_reader(ctx, &arena, owner);
    check(headed(chunk(ctx, &arena, alias, 1), "fs:failed", 7u));
    check(headed(call(ctx, &arena, "close", &owner, 1u), "fs:failed", 7u));
    close_reader(ctx, &arena, second);

    owner = open_reader(ctx, &arena, file);
    allocation_fault = true;
    Atom *failure = chunk(ctx, &arena, owner, 17);
    check(headed(failure, "fs:failed", 7u) && atom_is_symbol(failure->expr.elems[1], "fs:capacity"));
    check(atom_string_equals_cstr(chunk(ctx, &arena, owner, 1)->expr.elems[1], "a"));
    read_fault = true;
    failure = chunk(ctx, &arena, owner, 1);
    check(failure->expr.elems[4]->ground.ival == EIO);
    check(atom_is_symbol(failure->expr.elems[5]->expr.elems[2], "unknown-position"));
    unsigned fault_reads = reads;
    read_fault = false;
    check(headed(chunk(ctx, &arena, owner, 1), "fs:failed", 7u));
    check(reads == fault_reads);
    close_reader(ctx, &arena, owner);

    Atom *path = atom_string(&arena, argv[1]);
    owner = call(ctx, &arena, "open_directory", &path, 1u);
    unknown_hint = true;
    Atom *entry = call(ctx, &arena, "next_entry", &owner, 1u);
    check(headed(entry, "fs:entry", 4u));
    check(atom_is_symbol(entry->expr.elems[2], "unknown"));
    Atom *saved_name = atom_deep_copy(&survivor, entry->expr.elems[1]);
    uint64_t entries = 1u;
    for (;;) {
        Atom *next = call(ctx, &arena, "next_entry", &owner, 1u);
        if (headed(next, "fs:end", 2u)) { check(next->expr.elems[1]->ground.ival == (int64_t)entries); break; }
        check(headed(next, "fs:entry", 4u));
        entries++;
    }
    check(atom_eq(saved_name, entry->expr.elems[1]));
    check(entries >= 2u);
    close_reader(ctx, &arena, owner);
    owner = call(ctx, &arena, "open_directory", &path, 1u);
    directory_fault = true;
    failure = call(ctx, &arena, "next_entry", &owner, 1u);
    check(headed(failure, "fs:failed", 7u) && failure->expr.elems[4]->ground.ival == EIO);
    directory_fault = false;
    check(headed(call(ctx, &arena, "next_entry", &owner, 1u), "fs:failed", 7u));
    close_reader(ctx, &arena, owner);
    unknown_hint = false;

    Atom *info = call(ctx, &arena, "metadata", (Atom *[]){atom_string(&arena, link), atom_symbol(&arena, "no-follow")}, 2u);
    check(headed(info, "fs:info", 4u) && atom_is_symbol(info->expr.elems[1], "symlink"));
    info = call(ctx, &arena, "metadata", (Atom *[]){atom_string(&arena, link), atom_symbol(&arena, "follow")}, 2u);
    check(atom_is_symbol(info->expr.elems[1], "regular") && info->expr.elems[2]->ground.ival == 4);
    Atom *nul = atom_string_n(&arena, "bad\0path", 8u);
    check(headed(call(ctx, &arena, "open_read", &nul, 1u), "fs:failed", 7u));
    CettaCallOutcome outcome;
    interrupted = true;
    check(cetta_fs_call(ctx, &arena, atom_symbol(&arena, "__cetta_lib_fs_open_read"), &path, 1u, &outcome));
    check(outcome.kind == CETTA_CALL_INTERRUPTED);
    interrupted = false;

    /* Retiring the allocating arena retains a promoted owner and its cursor. */
    owner = open_reader(ctx, &arena, file);
    alias = atom_deep_copy(&survivor, owner);
    arena_free(&arena);
    arena_init_detached(&arena);
    check(atom_string_equals_cstr(chunk(ctx, &arena, alias, 1)->expr.elems[1], "a"));
    int kept_fd = last_read_fd;
    arena_free(&survivor);
    check(fcntl(kept_fd, F_GETFD) == -1 && errno == EBADF);
    owner = open_reader(ctx, &arena, file);
    (void)chunk(ctx, &arena, owner, 1);
    kept_fd = last_read_fd;
    cetta_native_handle_cleanup_all(ctx);
    check(fcntl(kept_fd, F_GETFD) == -1 && errno == EBADF);
    check(headed(chunk(ctx, &arena, owner, 1), "fs:failed", 7u));

    rename_controls(ctx, &arena, argv[1]);
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        (void)close(STDOUT_FILENO);
        (void)close(STDERR_FILENO);
        owner = open_reader(ctx, &arena, file);
        (void)chunk(ctx, &arena, owner, 1);
        check(last_read_fd >= 3 && (fcntl(last_read_fd, F_GETFD) & FD_CLOEXEC));
        close_reader(ctx, &arena, owner);
        _exit(0);
    }
    int status;
    assert(waitpid(child, &status, 0) == child);
    check(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    arena_free(&arena);
    cetta_native_handle_cleanup_all(ctx);
    free(ctx);
    symbol_table_free(&symbols);
#if CETTA_BUILD_WITH_RUNTIME_STATS
    CettaRuntimeStats stats;
    cetta_runtime_stats_snapshot(&stats);
    assert(stats.counters[CETTA_RUNTIME_COUNTER_EVAL_ARENA_ALLOC_BYTES] > 0u);
    puts("PASS: filesystem allocations are observed by runtime statistics");
#endif
    printf("(FilesystemNativeV1Summary %u 0)\n", checks);
    return 0;
}

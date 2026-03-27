#define _GNU_SOURCE
#include "library.h"

#include "eval.h"
#include "native/native_modules.h"
#include "parser.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

enum {
    CETTA_LIBRARY_SYSTEM = 1u << 0,
    CETTA_LIBRARY_FS = 1u << 1,
    CETTA_LIBRARY_STR = 1u << 2
};

typedef struct {
    const char *name;
    uint32_t bit;
} CettaLibrarySpec;

static const CettaLibrarySpec CETTA_LIBRARIES[] = {
    {"system", CETTA_LIBRARY_SYSTEM},
    {"fs", CETTA_LIBRARY_FS},
    {"str", CETTA_LIBRARY_STR},
};

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} CettaStringBuf;

static void cetta_sb_init(CettaStringBuf *sb) {
    sb->buf = NULL;
    sb->len = 0;
    sb->cap = 0;
}

static void cetta_sb_ensure(CettaStringBuf *sb, size_t extra) {
    size_t need = sb->len + extra + 1;
    if (need <= sb->cap) return;
    size_t next = sb->cap ? sb->cap * 2 : 64;
    while (next < need) next *= 2;
    sb->buf = cetta_realloc(sb->buf, next);
    sb->cap = next;
}

static void cetta_sb_append_n(CettaStringBuf *sb, const char *s, size_t n) {
    cetta_sb_ensure(sb, n);
    memcpy(sb->buf + sb->len, s, n);
    sb->len += n;
    sb->buf[sb->len] = '\0';
}

static void cetta_sb_append(CettaStringBuf *sb, const char *s) {
    cetta_sb_append_n(sb, s, strlen(s));
}

static void cetta_sb_free(CettaStringBuf *sb) {
    free(sb->buf);
    sb->buf = NULL;
    sb->len = 0;
    sb->cap = 0;
}

static bool ascii_ieq(const char *lhs, const char *rhs) {
    while (*lhs && *rhs) {
        unsigned char a = (unsigned char)*lhs;
        unsigned char b = (unsigned char)*rhs;
        if (a >= 'A' && a <= 'Z') a = (unsigned char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (unsigned char)(b - 'A' + 'a');
        if (a != b) return false;
        lhs++;
        rhs++;
    }
    return *lhs == '\0' && *rhs == '\0';
}

static const CettaLibrarySpec *cetta_library_lookup(const char *name) {
    size_t count = sizeof(CETTA_LIBRARIES) / sizeof(CETTA_LIBRARIES[0]);
    for (size_t i = 0; i < count; i++) {
        if (ascii_ieq(CETTA_LIBRARIES[i].name, name)) {
            return &CETTA_LIBRARIES[i];
        }
    }
    return NULL;
}

void cetta_library_context_init(CettaLibraryContext *ctx) {
    cetta_library_context_init_with_profile(ctx, cetta_profile_he_extended());
}

void cetta_library_context_init_with_profile(CettaLibraryContext *ctx,
                                             const CettaProfile *profile) {
    cetta_eval_session_init(&ctx->session, profile);
    ctx->active_mask = 0;
    ctx->root_dir[0] = '\0';
    ctx->script_dir[0] = '\0';
    ctx->import_dir_len = 0;
    ctx->module_mount_len = 0;
    ctx->imported_file_len = 0;
    ctx->import_space_alias_len = 0;
    ctx->cmdline_arg_len = 0;
    ctx->loaded_module_len = 0;
    ctx->native_handle_len = 0;
    ctx->native_handle_next_id = 1;
    ctx->foreign_runtime = cetta_foreign_runtime_new();
}

void cetta_library_context_free(CettaLibraryContext *ctx) {
    if (!ctx) return;
    for (uint32_t i = 0; i < ctx->loaded_module_len; i++) {
        Space *space = ctx->loaded_modules[i].space;
        if (!space) continue;
        space_free(space);
        free(space);
        ctx->loaded_modules[i].space = NULL;
    }
    ctx->loaded_module_len = 0;
    cetta_native_handle_cleanup_all(ctx);
    if (ctx->foreign_runtime) {
        cetta_foreign_runtime_free(ctx->foreign_runtime);
        ctx->foreign_runtime = NULL;
    }
}

void cetta_library_context_set_exec_path(CettaLibraryContext *ctx, const char *argv0) {
    char resolved[PATH_MAX];
    char *slash;

    ctx->root_dir[0] = '\0';
    if (!argv0) return;
    if (!realpath(argv0, resolved)) return;
    slash = strrchr(resolved, '/');
    if (!slash) return;
    *slash = '\0';
    snprintf(ctx->root_dir, sizeof(ctx->root_dir), "%s", resolved);
}

static void copy_parent_dir(char *dst, size_t dst_sz, const char *path) {
    size_t len;
    if (!dst_sz) return;
    if (!path || !*path) {
        snprintf(dst, dst_sz, ".");
        return;
    }
    snprintf(dst, dst_sz, "%s", path);
    len = strlen(dst);
    while (len > 1 && dst[len - 1] == '/') {
        dst[--len] = '\0';
    }
    char *slash = strrchr(dst, '/');
    if (!slash) {
        snprintf(dst, dst_sz, ".");
        return;
    }
    if (slash == dst) {
        slash[1] = '\0';
    } else {
        *slash = '\0';
    }
}

void cetta_library_context_set_script_path(CettaLibraryContext *ctx, const char *filename) {
    char resolved[PATH_MAX];

    ctx->script_dir[0] = '\0';
    if (!filename) return;
    if (!realpath(filename, resolved)) return;
    copy_parent_dir(ctx->script_dir, sizeof(ctx->script_dir), resolved);
}

void cetta_library_context_set_cli_args(CettaLibraryContext *ctx, int argc,
                                        char **argv, int arg_start) {
    if (!ctx) return;
    ctx->cmdline_arg_len = 0;
    if (!argv || argc <= 0) return;
    if (arg_start < 0) arg_start = 0;
    for (int i = arg_start; i < argc &&
                          ctx->cmdline_arg_len < CETTA_MAX_CMDLINE_ARGS; i++) {
        ctx->cmdline_args[ctx->cmdline_arg_len++] = argv[i];
    }
}

static const char *cetta_library_current_dir(CettaLibraryContext *ctx) {
    if (ctx->import_dir_len > 0) {
        return ctx->import_dirs[ctx->import_dir_len - 1];
    }
    if (ctx->script_dir[0] != '\0') {
        return ctx->script_dir;
    }
    return ".";
}

static Space *logical_import_space(CettaLibraryContext *ctx, Space *space) {
    for (uint32_t i = ctx->import_space_alias_len; i > 0; i--) {
        if (ctx->import_space_aliases[i - 1].work_space == space) {
            return ctx->import_space_aliases[i - 1].logical_space;
        }
    }
    return space;
}

static bool cetta_library_push_import_alias(CettaLibraryContext *ctx,
                                            Space *work_space,
                                            Space *logical_space) {
    if (ctx->import_space_alias_len >= CETTA_MAX_IMPORT_TRANSACTION_SPACES) {
        return false;
    }
    ctx->import_space_aliases[ctx->import_space_alias_len].work_space = work_space;
    ctx->import_space_aliases[ctx->import_space_alias_len].logical_space = logical_space;
    ctx->import_space_alias_len++;
    return true;
}

static void cetta_library_pop_import_alias(CettaLibraryContext *ctx) {
    if (ctx->import_space_alias_len > 0) {
        ctx->import_space_alias_len--;
    }
}

static void rollback_imported_files(CettaLibraryContext *ctx, uint32_t rollback_len) {
    while (ctx->imported_file_len > rollback_len) {
        ctx->imported_file_len--;
        ctx->imported_files[ctx->imported_file_len].space = NULL;
        ctx->imported_files[ctx->imported_file_len].loading = false;
        ctx->imported_files[ctx->imported_file_len].path[0] = '\0';
    }
}

static void rollback_loaded_modules(CettaLibraryContext *ctx, uint32_t rollback_len) {
    while (ctx->loaded_module_len > rollback_len) {
        ctx->loaded_module_len--;
        if (ctx->loaded_modules[ctx->loaded_module_len].space) {
            space_free(ctx->loaded_modules[ctx->loaded_module_len].space);
            free(ctx->loaded_modules[ctx->loaded_module_len].space);
            ctx->loaded_modules[ctx->loaded_module_len].space = NULL;
        }
        ctx->loaded_modules[ctx->loaded_module_len].display_name[0] = '\0';
        ctx->loaded_modules[ctx->loaded_module_len].canonical_path[0] = '\0';
        ctx->loaded_modules[ctx->loaded_module_len].format.kind = CETTA_MODULE_FORMAT_METTA;
        ctx->loaded_modules[ctx->loaded_module_len].format.foreign_backend = CETTA_FOREIGN_BACKEND_NONE;
        ctx->loaded_modules[ctx->loaded_module_len].loading = false;
    }
}

static bool module_provider_visible(const CettaLibraryContext *ctx,
                                    CettaModuleProviderKind provider_kind) {
    CettaModuleProviderFlags flag = cetta_module_provider_flag(provider_kind);
    return ctx && flag != 0 &&
           cetta_profile_allows_provider_kind(ctx->session.profile, provider_kind) &&
           cetta_module_resolver_allows(&ctx->session.module_resolver, flag);
}

static bool module_mount_visible(const CettaLibraryContext *ctx,
                                 const CettaModuleMount *mount) {
    return ctx && mount &&
           module_provider_visible(ctx, mount->provider_kind) &&
           cetta_profile_visible_in(ctx->session.profile, mount->profile_visibility_mask);
}

static bool loaded_module_visible(const CettaLibraryContext *ctx,
                                  const CettaLoadedModule *module) {
    return ctx && module && module_provider_visible(ctx, module->provider_kind);
}

static CettaModuleMount *module_mount_lookup_mutable(CettaLibraryContext *ctx,
                                                     const char *namespace_name) {
    for (uint32_t i = 0; i < ctx->module_mount_len; i++) {
        if (strcmp(ctx->module_mounts[i].namespace_name, namespace_name) == 0) {
            return &ctx->module_mounts[i];
        }
    }
    return NULL;
}

static const CettaModuleMount *module_mount_lookup_any(const CettaLibraryContext *ctx,
                                                       const char *namespace_name) {
    for (uint32_t i = 0; i < ctx->module_mount_len; i++) {
        if (strcmp(ctx->module_mounts[i].namespace_name, namespace_name) == 0) {
            return &ctx->module_mounts[i];
        }
    }
    return NULL;
}

uint32_t cetta_library_module_mount_count(const CettaLibraryContext *ctx) {
    uint32_t count = 0;
    if (!ctx) return 0;
    for (uint32_t i = 0; i < ctx->module_mount_len; i++) {
        if (module_mount_visible(ctx, &ctx->module_mounts[i])) {
            count++;
        }
    }
    return count;
}

const CettaModuleMount *cetta_library_module_mount_at(const CettaLibraryContext *ctx,
                                                      uint32_t index) {
    if (!ctx) return NULL;
    uint32_t visible = 0;
    for (uint32_t i = 0; i < ctx->module_mount_len; i++) {
        const CettaModuleMount *mount = &ctx->module_mounts[i];
        if (!module_mount_visible(ctx, mount)) continue;
        if (visible == index) return mount;
        visible++;
    }
    return NULL;
}

const CettaModuleMount *cetta_library_find_module_mount(const CettaLibraryContext *ctx,
                                                        const char *namespace_name) {
    const CettaModuleMount *mount = module_mount_lookup_any(ctx, namespace_name);
    if (!module_mount_visible(ctx, mount)) {
        return NULL;
    }
    return mount;
}

uint32_t cetta_library_loaded_module_count(const CettaLibraryContext *ctx) {
    uint32_t count = 0;
    if (!ctx) return 0;
    for (uint32_t i = 0; i < ctx->loaded_module_len; i++) {
        if (loaded_module_visible(ctx, &ctx->loaded_modules[i])) {
            count++;
        }
    }
    return count;
}

const CettaLoadedModule *cetta_library_loaded_module_at(const CettaLibraryContext *ctx,
                                                        uint32_t index) {
    if (!ctx) return NULL;
    uint32_t visible = 0;
    for (uint32_t i = 0; i < ctx->loaded_module_len; i++) {
        const CettaLoadedModule *module = &ctx->loaded_modules[i];
        if (!loaded_module_visible(ctx, module)) continue;
        if (visible == index) return module;
        visible++;
    }
    return NULL;
}

static void cetta_library_push_dir(CettaLibraryContext *ctx, const char *dir) {
    if (ctx->import_dir_len >= CETTA_MAX_IMPORT_DIR_DEPTH) return;
    snprintf(ctx->import_dirs[ctx->import_dir_len],
             sizeof(ctx->import_dirs[ctx->import_dir_len]), "%s", dir);
    ctx->import_dir_len++;
}

static void cetta_library_pop_dir(CettaLibraryContext *ctx) {
    if (ctx->import_dir_len > 0) ctx->import_dir_len--;
}

static bool path_has_suffix(const char *path, const char *suffix);
static CettaLoadedModule *loaded_module_lookup(CettaLibraryContext *ctx,
                                               const char *canonical_path);
static CettaLoadedModule *remember_loaded_module(CettaLibraryContext *ctx,
                                                 const CettaImportPlan *plan,
                                                 Space *space,
                                                 Arena *eval_arena,
                                                 Atom **error_out);

static bool module_name_is_legal(const char *name) {
    if (!name || !*name) return false;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
        bool alpha = (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z');
        bool digit = (*p >= '0' && *p <= '9');
        if (!(alpha || digit || *p == '_' || *p == '-')) return false;
    }
    return true;
}

static bool module_spec_looks_like_relative_path(const char *spec) {
    return spec && (*spec == '.' || strchr(spec, '/') != NULL || path_has_suffix(spec, ".metta"));
}

static bool path_has_suffix(const char *path, const char *suffix) {
    size_t path_len = strlen(path);
    size_t suffix_len = strlen(suffix);
    if (path_len < suffix_len) return false;
    return strcmp(path + path_len - suffix_len, suffix) == 0;
}

static bool path_join2(char *out, size_t out_sz,
                       const char *lhs, const char *rhs) {
    int n = snprintf(out, out_sz, "%s/%s", lhs, rhs);
    return n > 0 && (size_t)n < out_sz;
}

static bool ensure_directory_path(const char *path) {
    char scratch[PATH_MAX];
    struct stat st;

    if (!path || !*path) return false;
    if (snprintf(scratch, sizeof(scratch), "%s", path) >= (int)sizeof(scratch)) {
        return false;
    }

    size_t len = strlen(scratch);
    while (len > 1 && scratch[len - 1] == '/') {
        scratch[--len] = '\0';
    }

    for (char *p = scratch + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(scratch, 0777) != 0 && errno != EEXIST) {
            return false;
        }
        *p = '/';
    }

    if (mkdir(scratch, 0777) != 0 && errno != EEXIST) {
        return false;
    }
    if (stat(scratch, &st) != 0 || !S_ISDIR(st.st_mode)) {
        return false;
    }
    return true;
}

static bool remove_tree_recursive(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) {
        return errno == ENOENT;
    }
    if (S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(path);
        if (!dir) return false;
        struct dirent *entry;
        bool ok = true;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            char child[PATH_MAX];
            if (!path_join2(child, sizeof(child), path, entry->d_name)) {
                ok = false;
                break;
            }
            if (!remove_tree_recursive(child)) {
                ok = false;
                break;
            }
        }
        closedir(dir);
        if (!ok) return false;
        return rmdir(path) == 0;
    }
    return unlink(path) == 0;
}

static bool module_name_make_legal(const char *raw, char *out, size_t out_sz) {
    size_t wi = 0;
    bool last_was_underscore = false;
    if (!raw || !*raw || out_sz == 0) return false;
    for (const unsigned char *p = (const unsigned char *)raw; *p; p++) {
        bool alpha = (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z');
        bool digit = (*p >= '0' && *p <= '9');
        bool keep = alpha || digit || *p == '_' || *p == '-';
        char next = keep ? (char)*p : '_';
        if (next == '_' && last_was_underscore) continue;
        if (wi + 1 >= out_sz) return false;
        out[wi++] = next;
        last_was_underscore = (next == '_');
    }
    while (wi > 0 && out[wi - 1] == '_') {
        wi--;
    }
    if (wi == 0) return false;
    out[wi] = '\0';
    return true;
}

static bool git_module_name_from_url(const char *url, char *out, size_t out_sz) {
    char trimmed[PATH_MAX];
    if (!url || !*url) return false;
    if (snprintf(trimmed, sizeof(trimmed), "%s", url) >= (int)sizeof(trimmed)) {
        return false;
    }

    size_t len = strlen(trimmed);
    while (len > 0 && trimmed[len - 1] == '/') {
        trimmed[--len] = '\0';
    }
    if (len >= 4 && strcmp(trimmed + len - 4, ".git") == 0) {
        trimmed[len - 4] = '\0';
    }

    const char *base = strrchr(trimmed, '/');
    const char *name = base ? base + 1 : trimmed;
    return module_name_make_legal(name, out, out_sz);
}

static uint64_t stable_fnv1a64(const char *text) {
    uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        hash ^= (uint64_t)(*p);
        hash *= 1099511628211ULL;
    }
    return hash;
}

static bool git_cache_entry_valid(const char *path) {
    char git_marker[PATH_MAX];
    return path_join2(git_marker, sizeof(git_marker), path, ".git") &&
           access(git_marker, R_OK) == 0;
}

static bool git_cache_root(CettaLibraryContext *ctx, char *out, size_t out_sz,
                           Arena *eval_arena, Atom **error_out) {
    const char *override = getenv("CETTA_GIT_MODULE_CACHE_DIR");
    const char *xdg = getenv("XDG_CACHE_HOME");
    const char *home = getenv("HOME");
    char cwd[PATH_MAX];

    if (override && *override) {
        if (snprintf(out, out_sz, "%s", override) >= (int)out_sz) {
            *error_out = atom_symbol(eval_arena, "git module cache path too long");
            return false;
        }
    } else if (xdg && *xdg) {
        if (snprintf(out, out_sz, "%s/cetta/git-modules", xdg) >= (int)out_sz) {
            *error_out = atom_symbol(eval_arena, "git module cache path too long");
            return false;
        }
    } else if (home && *home) {
        if (snprintf(out, out_sz, "%s/.cache/cetta/git-modules", home) >= (int)out_sz) {
            *error_out = atom_symbol(eval_arena, "git module cache path too long");
            return false;
        }
    } else if (ctx && ctx->root_dir[0] != '\0') {
        if (snprintf(out, out_sz, "%s/runtime/git-module-cache", ctx->root_dir) >= (int)out_sz) {
            *error_out = atom_symbol(eval_arena, "git module cache path too long");
            return false;
        }
    } else if (getcwd(cwd, sizeof(cwd))) {
        if (snprintf(out, out_sz, "%s/runtime/git-module-cache", cwd) >= (int)out_sz) {
            *error_out = atom_symbol(eval_arena, "git module cache path too long");
            return false;
        }
    } else {
        *error_out = atom_symbol(eval_arena, "git module cache unavailable");
        return false;
    }

    if (!ensure_directory_path(out)) {
        *error_out = atom_symbol(eval_arena, "git module cache unavailable");
        return false;
    }
    return true;
}

static bool run_git_clone(const char *url, const char *dst,
                          char *errbuf, size_t errbuf_sz) {
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        if (errbuf_sz > 0) snprintf(errbuf, errbuf_sz, "git-module! failed to create pipe");
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        if (errbuf_sz > 0) snprintf(errbuf, errbuf_sz, "git-module! failed to spawn git");
        return false;
    }

    if (pid == 0) {
        setenv("GIT_TERMINAL_PROMPT", "0", 1);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        char *const argv[] = {
            "git", "-c", "protocol.file.allow=always",
            "clone", "--depth", "1",
            (char *)url, (char *)dst,
            NULL
        };
        execvp("git", argv);
        _exit(127);
    }

    close(pipefd[1]);
    if (errbuf_sz > 0) errbuf[0] = '\0';
    size_t used = 0;
    ssize_t got = 0;
    while ((got = read(pipefd[0], errbuf + used,
                       errbuf_sz > used + 1 ? errbuf_sz - used - 1 : 0)) > 0) {
        used += (size_t)got;
        if (errbuf_sz <= used + 1) break;
    }
    close(pipefd[0]);
    if (errbuf_sz > 0) errbuf[used] = '\0';

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        if (errbuf_sz > 0) snprintf(errbuf, errbuf_sz, "git-module! failed to wait for git");
        return false;
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        return true;
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) == 127) {
        if (errbuf_sz > 0) snprintf(errbuf, errbuf_sz, "git executable not found");
        return false;
    }
    if (errbuf_sz > 0) snprintf(errbuf, errbuf_sz, "git-module! failed to clone repository");
    return false;
}

static bool run_git_try_fetch_latest(const char *repo_path,
                                     char *errbuf, size_t errbuf_sz) {
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        if (errbuf_sz > 0) snprintf(errbuf, errbuf_sz, "git-module! failed to create pipe");
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        if (errbuf_sz > 0) snprintf(errbuf, errbuf_sz, "git-module! failed to spawn git");
        return false;
    }

    if (pid == 0) {
        setenv("GIT_TERMINAL_PROMPT", "0", 1);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        char *const argv[] = {
            "git", "-C", (char *)repo_path,
            "-c", "protocol.file.allow=always",
            "fetch", "--depth", "1", "origin",
            NULL
        };
        execvp("git", argv);
        _exit(127);
    }

    close(pipefd[1]);
    if (errbuf_sz > 0) errbuf[0] = '\0';
    size_t used = 0;
    ssize_t got = 0;
    while ((got = read(pipefd[0], errbuf + used,
                       errbuf_sz > used + 1 ? errbuf_sz - used - 1 : 0)) > 0) {
        used += (size_t)got;
        if (errbuf_sz <= used + 1) break;
    }
    close(pipefd[0]);
    if (errbuf_sz > 0) errbuf[used] = '\0';

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        if (errbuf_sz > 0) snprintf(errbuf, errbuf_sz, "git-module! failed to wait for git");
        return false;
    }
    if (!(WIFEXITED(status) && WEXITSTATUS(status) == 0)) {
        return false;
    }

    if (pipe(pipefd) != 0) {
        if (errbuf_sz > 0) snprintf(errbuf, errbuf_sz, "git-module! failed to create pipe");
        return false;
    }

    pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        if (errbuf_sz > 0) snprintf(errbuf, errbuf_sz, "git-module! failed to spawn git");
        return false;
    }

    if (pid == 0) {
        setenv("GIT_TERMINAL_PROMPT", "0", 1);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        char *const argv[] = {
            "git", "-C", (char *)repo_path,
            "reset", "--hard", "FETCH_HEAD",
            NULL
        };
        execvp("git", argv);
        _exit(127);
    }

    close(pipefd[1]);
    if (errbuf_sz > 0) errbuf[0] = '\0';
    used = 0;
    got = 0;
    while ((got = read(pipefd[0], errbuf + used,
                       errbuf_sz > used + 1 ? errbuf_sz - used - 1 : 0)) > 0) {
        used += (size_t)got;
        if (errbuf_sz <= used + 1) break;
    }
    close(pipefd[0]);
    if (errbuf_sz > 0) errbuf[used] = '\0';

    status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        if (errbuf_sz > 0) snprintf(errbuf, errbuf_sz, "git-module! failed to wait for git");
        return false;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static bool upsert_module_mount(CettaLibraryContext *ctx, const char *namespace_name,
                                const char *root_path,
                                CettaModuleProviderKind provider_kind,
                                CettaModuleLocatorKind locator_kind,
                                const char *source_locator,
                                CettaRemoteRevisionPolicy revision_policy,
                                const char *revision_value,
                                uint32_t visibility_mask,
                                Arena *eval_arena, Atom **error_out) {
    if (!module_name_is_legal(namespace_name)) {
        *error_out = atom_symbol(eval_arena, "illegal module name");
        return false;
    }
    if (strlen(namespace_name) >= CETTA_MAX_MODULE_NAMESPACE) {
        *error_out = atom_symbol(eval_arena, "module name too long");
        return false;
    }
    if (strlen(root_path) >= PATH_MAX) {
        *error_out = atom_symbol(eval_arena, "module path too long");
        return false;
    }

    CettaModuleMount *existing = module_mount_lookup_mutable(ctx, namespace_name);
    if (existing) {
        existing->provider_kind = provider_kind;
        snprintf(existing->root_path, sizeof(existing->root_path), "%s", root_path);
        existing->locator_kind = locator_kind;
        snprintf(existing->source_locator, sizeof(existing->source_locator), "%s",
                 source_locator ? source_locator : root_path);
        existing->revision_policy = revision_policy;
        snprintf(existing->revision_value, sizeof(existing->revision_value), "%s",
                 revision_value ? revision_value : "");
        existing->profile_visibility_mask = visibility_mask;
        return true;
    }

    if (ctx->module_mount_len >= CETTA_MAX_MODULE_ROOTS) {
        *error_out = atom_symbol(eval_arena, "too many module roots");
        return false;
    }

    CettaModuleMount *mount = &ctx->module_mounts[ctx->module_mount_len++];
    memset(mount, 0, sizeof(*mount));
    mount->provider_kind = provider_kind;
    snprintf(mount->namespace_name, sizeof(mount->namespace_name), "%s", namespace_name);
    snprintf(mount->root_path, sizeof(mount->root_path), "%s", root_path);
    mount->locator_kind = locator_kind;
    snprintf(mount->source_locator, sizeof(mount->source_locator), "%s",
             source_locator ? source_locator : root_path);
    mount->revision_policy = revision_policy;
    snprintf(mount->revision_value, sizeof(mount->revision_value), "%s",
             revision_value ? revision_value : "");
    mount->profile_visibility_mask = visibility_mask;
    return true;
}

static bool ensure_git_cached_repo(CettaLibraryContext *ctx, const char *url,
                                   char *module_name, size_t module_name_sz,
                                   char *root_path, size_t root_path_sz,
                                   Arena *eval_arena, Atom **error_out) {
    char cache_root[PATH_MAX];
    char final_path[PATH_MAX];
    char tmp_template[PATH_MAX];
    char clone_error[256];
    char update_error[256];
    struct stat st;
    uint64_t url_hash = stable_fnv1a64(url);

    if (!git_module_name_from_url(url, module_name, module_name_sz)) {
        *error_out = atom_symbol(eval_arena, "git-module! error extracting module name from URL");
        return false;
    }
    if (!git_cache_root(ctx, cache_root, sizeof(cache_root), eval_arena, error_out)) {
        return false;
    }
    if (snprintf(final_path, sizeof(final_path), "%s/%s-%016llx",
                 cache_root, module_name, (unsigned long long)url_hash) >= (int)sizeof(final_path)) {
        *error_out = atom_symbol(eval_arena, "git module cache path too long");
        return false;
    }

    if (stat(final_path, &st) == 0) {
        if (!S_ISDIR(st.st_mode) || !git_cache_entry_valid(final_path)) {
            *error_out = atom_symbol(eval_arena, "git module cache entry invalid");
            return false;
        }
        /* HE uses TryFetchLatest here: attempt to refresh, but keep the cached
           repo on soft failures. */
        (void)run_git_try_fetch_latest(final_path, update_error, sizeof(update_error));
        snprintf(root_path, root_path_sz, "%s", final_path);
        return true;
    }

    if (snprintf(tmp_template, sizeof(tmp_template), "%s/%s-%016llx.tmp.XXXXXX",
                 cache_root, module_name, (unsigned long long)url_hash) >= (int)sizeof(tmp_template)) {
        *error_out = atom_symbol(eval_arena, "git module cache path too long");
        return false;
    }
    if (!mkdtemp(tmp_template)) {
        *error_out = atom_symbol(eval_arena, "git module cache unavailable");
        return false;
    }

    if (!run_git_clone(url, tmp_template, clone_error, sizeof(clone_error))) {
        remove_tree_recursive(tmp_template);
        *error_out = atom_symbol(eval_arena, clone_error);
        return false;
    }

    if (rename(tmp_template, final_path) != 0) {
        if (stat(final_path, &st) == 0 && S_ISDIR(st.st_mode) && git_cache_entry_valid(final_path)) {
            remove_tree_recursive(tmp_template);
            snprintf(root_path, root_path_sz, "%s", final_path);
            return true;
        }
        remove_tree_recursive(tmp_template);
        *error_out = atom_symbol(eval_arena, "git module cache rename failed");
        return false;
    }

    snprintf(root_path, root_path_sz, "%s", final_path);
    return true;
}

static bool directory_has_visible_entries(const char *dir_path) {
    DIR *dir = opendir(dir_path);
    if (!dir) return false;
    bool found = false;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        const char *name = ent->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        found = true;
        break;
    }
    closedir(dir);
    return found;
}

static bool resolve_module_candidate_metta(const char *candidate,
                                           char *out, size_t out_sz,
                                           char *reason, size_t reason_sz) {
    char path[PATH_MAX];
    struct stat st;

    if (reason && reason_sz > 0) reason[0] = '\0';
    snprintf(path, sizeof(path), "%s", candidate);
    if (!path_has_suffix(path, ".metta") && access(path, R_OK) != 0) {
        int n = snprintf(path, sizeof(path), "%s.metta", candidate);
        if (!(n > 0 && (size_t)n < sizeof(path))) return false;
    }
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        if (reason && reason_sz > 0) {
            snprintf(reason, reason_sz, "module directory missing module.metta");
        }
        if (directory_has_visible_entries(path) && reason && reason_sz > 0) {
            snprintf(reason, reason_sz,
                     "unsupported non-MeTTa module directory (missing module.metta)");
        }
        int n = snprintf(path, sizeof(path), "%s/module.metta", candidate);
        if (!(n > 0 && (size_t)n < sizeof(path))) return false;
    }
    if (access(path, R_OK) != 0) return false;
    if (!realpath(path, out)) return false;
    return strlen(out) < out_sz;
}

static bool resolve_module_candidate_with_format(const char *candidate,
                                                 char *out, size_t out_sz,
                                                 CettaModuleFormat *format_out,
                                                 char *reason, size_t reason_sz) {
    char metta_reason[160] = {0};
    if (resolve_module_candidate_metta(candidate, out, out_sz,
                                       metta_reason, sizeof(metta_reason))) {
        if (format_out) {
            format_out->kind = CETTA_MODULE_FORMAT_METTA;
            format_out->foreign_backend = CETTA_FOREIGN_BACKEND_NONE;
        }
        if (reason && reason_sz > 0) reason[0] = '\0';
        return true;
    }

    char foreign_reason[160] = {0};
    if (cetta_foreign_resolve_candidate(candidate, out, out_sz,
                                        format_out,
                                        foreign_reason, sizeof(foreign_reason))) {
        return true;
    }

    if (reason && reason_sz > 0) {
        const char *chosen = metta_reason[0] ? metta_reason :
                             (foreign_reason[0] ? foreign_reason : "");
        snprintf(reason, reason_sz, "%s", chosen);
    }
    return false;
}

static bool build_library_path(CettaLibraryContext *ctx, const char *name,
                               char *out, size_t out_sz);

static bool parse_module_spec(const char *spec, CettaModuleSpec *out,
                              Arena *eval_arena, Atom **error_out) {
    memset(out, 0, sizeof(*out));
    if (!spec || !*spec) {
        *error_out = atom_symbol(eval_arena, "empty module name");
        return false;
    }
    if (spec[0] == '/') {
        *error_out = atom_symbol(eval_arena, "illegal module name");
        return false;
    }

    snprintf(out->raw_spec, sizeof(out->raw_spec), "%s", spec);
    const char *sep = strchr(spec, ':');
    if (!sep) {
        out->kind = module_spec_looks_like_relative_path(spec) ?
            CETTA_MODULE_SPEC_RELATIVE_FILE :
            CETTA_MODULE_SPEC_MODULE_NAME;
        snprintf(out->path_or_member, sizeof(out->path_or_member), "%s", spec);
        return true;
    }

    size_t ns_len = (size_t)(sep - spec);
    if (ns_len == 0 || ns_len >= sizeof(out->namespace_name)) {
        *error_out = atom_symbol(eval_arena, "illegal module name");
        return false;
    }
    memcpy(out->namespace_name, spec, ns_len);
    out->namespace_name[ns_len] = '\0';
    out->kind = CETTA_MODULE_SPEC_REGISTERED_ROOT;

    size_t ri = 0;
    for (const char *p = sep + 1; *p && ri + 1 < sizeof(out->path_or_member); p++) {
        out->path_or_member[ri++] = (*p == ':') ? '/' : *p;
    }
    out->path_or_member[ri] = '\0';
    return true;
}

static bool resolve_import_plan(CettaLibraryContext *ctx, const CettaModuleSpec *spec,
                                Space *logical_target_space,
                                Space *execution_target_space,
                                bool target_is_fresh,
                                CettaImportPlan *plan,
                                Arena *eval_arena, Atom **error_out) {
    char candidate[PATH_MAX];

    memset(plan, 0, sizeof(*plan));
    plan->spec = *spec;
    plan->logical_target_space = logical_target_space;
    plan->execution_target_space = execution_target_space;
    plan->target_is_fresh = target_is_fresh;
    plan->transactional = ctx->session.module_resolver.transactional_imports && !target_is_fresh;
    plan->format.kind = CETTA_MODULE_FORMAT_METTA;
    plan->format.foreign_backend = CETTA_FOREIGN_BACKEND_NONE;

    switch (spec->kind) {
    case CETTA_MODULE_SPEC_REGISTERED_ROOT: {
        if (!cetta_module_resolver_allows(&ctx->session.module_resolver,
                                          CETTA_MODULE_PROVIDER_REGISTERED_ROOTS)) {
            *error_out = atom_symbol(eval_arena, "registered module roots disabled");
            return false;
        }
        const CettaModuleMount *mount = cetta_library_find_module_mount(ctx, spec->namespace_name);
        if (!mount) {
            *error_out = atom_symbol(eval_arena, "unknown module root");
            return false;
        }
        plan->provider_kind = mount->provider_kind;
        if (spec->path_or_member[0] == '\0') {
            int n = snprintf(candidate, sizeof(candidate), "%s", mount->root_path);
            if (!(n > 0 && (size_t)n < sizeof(candidate))) {
                *error_out = atom_symbol(eval_arena, "module path too long");
                return false;
            }
        } else {
            int n = snprintf(candidate, sizeof(candidate), "%s/%s",
                             mount->root_path, spec->path_or_member);
            if (!(n > 0 && (size_t)n < sizeof(candidate))) {
                *error_out = atom_symbol(eval_arena, "module path too long");
                return false;
            }
        }
        break;
    }
    case CETTA_MODULE_SPEC_RELATIVE_FILE: {
        if (!cetta_module_resolver_allows(&ctx->session.module_resolver,
                                          CETTA_MODULE_PROVIDER_RELATIVE_FILES)) {
            *error_out = atom_symbol(eval_arena, "relative module imports disabled");
            return false;
        }
        plan->provider_kind = CETTA_MODULE_PROVIDER_RELATIVE_FILE;
        int n = snprintf(candidate, sizeof(candidate), "%s/%s",
                         cetta_library_current_dir(ctx), spec->path_or_member);
        if (!(n > 0 && (size_t)n < sizeof(candidate))) {
            *error_out = atom_symbol(eval_arena, "module path too long");
            return false;
        }
        break;
    }
    case CETTA_MODULE_SPEC_MODULE_NAME: {
        if (cetta_module_resolver_allows(&ctx->session.module_resolver,
                                         CETTA_MODULE_PROVIDER_REGISTERED_ROOTS)) {
            const CettaModuleMount *mount = cetta_library_find_module_mount(ctx, spec->path_or_member);
            if (mount) {
                plan->provider_kind = mount->provider_kind;
                int n = snprintf(candidate, sizeof(candidate), "%s", mount->root_path);
                if (!(n > 0 && (size_t)n < sizeof(candidate))) {
                    *error_out = atom_symbol(eval_arena, "module path too long");
                    return false;
                }
                break;
            }
        }
        if (cetta_module_resolver_allows(&ctx->session.module_resolver,
                                         CETTA_MODULE_PROVIDER_STDLIB) &&
            build_library_path(ctx, spec->path_or_member, candidate, sizeof(candidate))) {
            plan->provider_kind = CETTA_MODULE_PROVIDER_STDLIB_FILE;
            break;
        }
        if (!cetta_module_resolver_allows(&ctx->session.module_resolver,
                                          CETTA_MODULE_PROVIDER_RELATIVE_FILES)) {
            *error_out = atom_symbol(eval_arena, "relative module imports disabled");
            return false;
        }
        plan->provider_kind = CETTA_MODULE_PROVIDER_RELATIVE_FILE;
        if (!module_provider_visible(ctx, plan->provider_kind)) {
            *error_out = atom_symbol(eval_arena, "module provider disabled");
            return false;
        }
        int n = snprintf(candidate, sizeof(candidate), "%s/%s",
                         cetta_library_current_dir(ctx), spec->path_or_member);
        if (!(n > 0 && (size_t)n < sizeof(candidate))) {
            *error_out = atom_symbol(eval_arena, "module path too long");
            return false;
        }
        char reason[160];
        if (!resolve_module_candidate_with_format(candidate, plan->canonical_path,
                                                  sizeof(plan->canonical_path),
                                                  &plan->format,
                                                  reason, sizeof(reason))) {
            char *msg = arena_alloc(eval_arena,
                                    strlen("Failed to resolve module ") +
                                    strlen(spec->path_or_member) +
                                    (reason[0] ? strlen(": ") + strlen(reason) : 0) + 1);
            sprintf(msg, "Failed to resolve module %s%s%s",
                    spec->path_or_member,
                    reason[0] ? ": " : "",
                    reason[0] ? reason : "");
            *error_out = atom_symbol(eval_arena, msg);
            return false;
        }
        return true;
    }
    case CETTA_MODULE_SPEC_STDLIB:
        if (!cetta_module_resolver_allows(&ctx->session.module_resolver,
                                          CETTA_MODULE_PROVIDER_STDLIB)) {
            *error_out = atom_symbol(eval_arena, "stdlib module imports disabled");
            return false;
        }
        plan->provider_kind = CETTA_MODULE_PROVIDER_STDLIB_FILE;
        if (!build_library_path(ctx, spec->path_or_member, candidate, sizeof(candidate))) {
            *error_out = atom_symbol(eval_arena, "library file not found");
            return false;
        }
        break;
    }

    if (!module_provider_visible(ctx, plan->provider_kind)) {
        *error_out = atom_symbol(eval_arena, "module provider disabled");
        return false;
    }

    char reason[160];
    if (!resolve_module_candidate_with_format(candidate, plan->canonical_path,
                                              sizeof(plan->canonical_path),
                                              &plan->format,
                                              reason, sizeof(reason))) {
        if (reason[0]) {
            *error_out = atom_symbol(eval_arena, reason);
        } else {
            *error_out = atom_symbol(eval_arena, "module file not found");
        }
        return false;
    }
    return true;
}

static int imported_file_lookup(CettaLibraryContext *ctx, Space *space,
                                const char *path) {
    for (uint32_t i = 0; i < ctx->imported_file_len; i++) {
        if (ctx->imported_files[i].path[0] == '\0') continue;
        if (ctx->imported_files[i].space == space &&
            strcmp(ctx->imported_files[i].path, path) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static bool build_library_path(CettaLibraryContext *ctx, const char *name,
                               char *out, size_t out_sz) {
    if (!cetta_module_resolver_allows(&ctx->session.module_resolver,
                                      CETTA_MODULE_PROVIDER_STDLIB)) {
        return false;
    }
    if (ctx->root_dir[0] != '\0') {
        int n = snprintf(out, out_sz, "%s/lib/%s.metta", ctx->root_dir, name);
        if (n > 0 && (size_t)n < out_sz && access(out, R_OK) == 0) return true;
    }
    {
        int n = snprintf(out, out_sz, "lib/%s.metta", name);
        if (n > 0 && (size_t)n < out_sz && access(out, R_OK) == 0) return true;
    }
    return false;
}

static Atom *library_call_expr(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    Atom **elems = arena_alloc(a, sizeof(Atom *) * (nargs + 1));
    elems[0] = head;
    for (uint32_t i = 0; i < nargs; i++) elems[i + 1] = args[i];
    return atom_expr(a, elems, nargs + 1);
}

static Atom *library_signature_error(Arena *a, Atom *head, Atom **args,
                                     uint32_t nargs, const char *message) {
    return atom_error(a, library_call_expr(a, head, args, nargs),
                      atom_symbol(a, message));
}

static const char *library_text_arg(Atom *arg) {
    if (!arg) return NULL;
    if (arg->kind == ATOM_SYMBOL) return arg->name;
    if (arg->kind == ATOM_GROUNDED && arg->ground.gkind == GV_STRING) {
        return arg->ground.sval;
    }
    return NULL;
}

static bool library_int_arg(Atom *arg, int *out) {
    if (!arg || !out) return false;
    if (arg->kind == ATOM_GROUNDED && arg->ground.gkind == GV_INT) {
        *out = (int)arg->ground.ival;
        return true;
    }
    return false;
}

static bool library_expr_of_texts(Atom *arg) {
    if (!arg || arg->kind != ATOM_EXPR) return false;
    for (uint32_t i = 0; i < arg->expr.len; i++) {
        if (!library_text_arg(arg->expr.elems[i])) return false;
    }
    return true;
}

static Atom *library_string_list(Arena *a, char **items, uint32_t nitems) {
    Atom **atoms = arena_alloc(a, sizeof(Atom *) * (nitems ? nitems : 1));
    for (uint32_t i = 0; i < nitems; i++) {
        atoms[i] = atom_string(a, items[i]);
    }
    return atom_expr(a, atoms, nitems);
}

static bool library_read_text_file(const char *path, CettaStringBuf *out,
                                   char *errbuf, size_t errbuf_sz) {
    FILE *fp = fopen(path, "rb");
    char chunk[4096];
    size_t nread;

    if (!fp) {
        if (errbuf && errbuf_sz > 0) {
            snprintf(errbuf, errbuf_sz, "cannot open file: %s", strerror(errno));
        }
        return false;
    }

    cetta_sb_init(out);
    while ((nread = fread(chunk, 1, sizeof(chunk), fp)) > 0) {
        cetta_sb_append_n(out, chunk, nread);
    }
    if (ferror(fp)) {
        if (errbuf && errbuf_sz > 0) {
            snprintf(errbuf, errbuf_sz, "cannot read file: %s", strerror(errno));
        }
        fclose(fp);
        cetta_sb_free(out);
        return false;
    }

    fclose(fp);
    return true;
}

static bool library_write_text_file(const char *path, const char *text, bool append,
                                    char *errbuf, size_t errbuf_sz) {
    FILE *fp = fopen(path, append ? "ab" : "wb");
    size_t len = strlen(text);

    if (!fp) {
        if (errbuf && errbuf_sz > 0) {
            snprintf(errbuf, errbuf_sz, "cannot open file: %s", strerror(errno));
        }
        return false;
    }

    if (len > 0 && fwrite(text, 1, len, fp) != len) {
        if (errbuf && errbuf_sz > 0) {
            snprintf(errbuf, errbuf_sz, "cannot write file: %s", strerror(errno));
        }
        fclose(fp);
        return false;
    }
    if (fclose(fp) != 0) {
        if (errbuf && errbuf_sz > 0) {
            snprintf(errbuf, errbuf_sz, "cannot close file: %s", strerror(errno));
        }
        return false;
    }
    return true;
}

static bool system_zero_arg_ok(Atom **args, uint32_t nargs) {
    return nargs == 0 || (nargs == 1 && args[0] && atom_is_expr(args[0]) &&
                          args[0]->expr.len == 0);
}

static Atom *system_cli_args(const CettaLibraryContext *ctx, Arena *a, Atom *head,
                             Atom **args, uint32_t nargs) {
    if (!system_zero_arg_ok(args, nargs)) {
        return atom_error(a, library_call_expr(a, head, args, nargs),
                          atom_symbol(a, "expected: (system-args)"));
    }
    Atom **items = arena_alloc(a, sizeof(Atom *) *
                                  (ctx && ctx->cmdline_arg_len ? ctx->cmdline_arg_len : 1));
    uint32_t nitems = ctx ? ctx->cmdline_arg_len : 0;
    for (uint32_t i = 0; i < nitems; i++) {
        items[i] = atom_symbol(a, ctx->cmdline_args[i]);
    }
    return atom_expr(a, items, nitems);
}

static Atom *system_cli_arg(const CettaLibraryContext *ctx, Arena *a, Atom *head,
                            Atom **args, uint32_t nargs) {
    int index = 0;
    if (nargs != 1 || !library_int_arg(args[0], &index) || index < 0) {
        return library_signature_error(a, head, args, nargs,
                                       "expected non-negative integer index");
    }
    if (!ctx || (uint32_t)index >= ctx->cmdline_arg_len) {
        return atom_empty(a);
    }
    return atom_symbol(a, ctx->cmdline_args[index]);
}

static Atom *system_cli_arg_count(const CettaLibraryContext *ctx, Arena *a, Atom *head,
                                  Atom **args, uint32_t nargs) {
    if (!system_zero_arg_ok(args, nargs)) {
        return library_signature_error(a, head, args, nargs, "expected: (system-argc)");
    }
    return atom_int(a, ctx ? (int64_t)ctx->cmdline_arg_len : 0);
}

static Atom *system_has_cli_args(const CettaLibraryContext *ctx, Arena *a, Atom *head,
                                 Atom **args, uint32_t nargs) {
    if (!system_zero_arg_ok(args, nargs)) {
        return library_signature_error(a, head, args, nargs,
                                       "expected: (system-has-args)");
    }
    return (ctx && ctx->cmdline_arg_len > 0) ? atom_true(a) : atom_false(a);
}

static Atom *system_getenv_or_default(Arena *a, Atom *head,
                                      Atom **args, uint32_t nargs) {
    const char *name;
    const char *env_value;
    if (nargs != 2 || !(name = library_text_arg(args[0]))) {
        return library_signature_error(a, head, args, nargs,
                                       "expected env var name and default");
    }
    env_value = getenv(name);
    if (env_value) return atom_string(a, env_value);
    return atom_deep_copy(a, args[1]);
}

static Atom *system_is_flag_arg(Arena *a, Atom *head,
                                Atom **args, uint32_t nargs) {
    const char *value;
    if (nargs != 1 || !(value = library_text_arg(args[0]))) {
        return library_signature_error(a, head, args, nargs,
                                       "expected atom/text argument");
    }
    return (value[0] == '-' && value[1] == '-') ? atom_true(a) : atom_false(a);
}

static Atom *system_exit_with_code(Arena *a, Atom *head,
                                   Atom **args, uint32_t nargs) {
    int code = 0;
    if (nargs != 1 || !library_int_arg(args[0], &code)) {
        return library_signature_error(a, head, args, nargs,
                                       "expected integer exit code");
    }
    fflush(stdout);
    fflush(stderr);
    exit(code);
}

static Atom *system_cwd(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    char path[PATH_MAX];
    if (!system_zero_arg_ok(args, nargs)) {
        return library_signature_error(a, head, args, nargs, "expected: (system-cwd)");
    }
    if (!getcwd(path, sizeof(path))) {
        return library_signature_error(a, head, args, nargs,
                                       "cannot determine current directory");
    }
    return atom_string(a, path);
}

static Atom *cetta_library_dispatch_system(const CettaLibraryContext *ctx,
                                           Arena *a, Atom *head,
                                           Atom **args, uint32_t nargs) {
    if (head->kind != ATOM_SYMBOL) return NULL;
    if (strcmp(head->name, "__cetta_lib_system_args") == 0) {
        return system_cli_args(ctx, a, head, args, nargs);
    }
    if (strcmp(head->name, "__cetta_lib_system_arg") == 0) {
        return system_cli_arg(ctx, a, head, args, nargs);
    }
    if (strcmp(head->name, "__cetta_lib_system_arg_count") == 0) {
        return system_cli_arg_count(ctx, a, head, args, nargs);
    }
    if (strcmp(head->name, "__cetta_lib_system_has_args") == 0) {
        return system_has_cli_args(ctx, a, head, args, nargs);
    }
    if (strcmp(head->name, "__cetta_lib_system_getenv_or_default") == 0) {
        return system_getenv_or_default(a, head, args, nargs);
    }
    if (strcmp(head->name, "__cetta_lib_system_is_flag_arg") == 0) {
        return system_is_flag_arg(a, head, args, nargs);
    }
    if (strcmp(head->name, "__cetta_lib_system_exit_with_code") == 0) {
        return system_exit_with_code(a, head, args, nargs);
    }
    if (strcmp(head->name, "__cetta_lib_system_cwd") == 0) {
        return system_cwd(a, head, args, nargs);
    }
    return NULL;
}

static Atom *fs_exists(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    const char *path;
    struct stat st;
    if (nargs != 1 || !(path = library_text_arg(args[0]))) {
        return library_signature_error(a, head, args, nargs, "expected filename");
    }
    return stat(path, &st) == 0 ? atom_true(a) : atom_false(a);
}

static Atom *fs_read_text(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    const char *path;
    CettaStringBuf text;
    char errbuf[160];
    Atom *result;
    if (nargs != 1 || !(path = library_text_arg(args[0]))) {
        return library_signature_error(a, head, args, nargs, "expected filename");
    }
    if (!library_read_text_file(path, &text, errbuf, sizeof(errbuf))) {
        return atom_error(a, library_call_expr(a, head, args, nargs), atom_string(a, errbuf));
    }
    result = atom_string(a, text.buf ? text.buf : "");
    cetta_sb_free(&text);
    return result;
}

static Atom *fs_write_like(Arena *a, Atom *head, Atom **args, uint32_t nargs, bool append) {
    const char *path;
    const char *text;
    char errbuf[160];
    if (nargs != 2 || !(path = library_text_arg(args[0])) ||
        !(text = library_text_arg(args[1]))) {
        return library_signature_error(a, head, args, nargs,
                                       "expected filename and text");
    }
    if (!library_write_text_file(path, text, append, errbuf, sizeof(errbuf))) {
        return atom_error(a, library_call_expr(a, head, args, nargs), atom_string(a, errbuf));
    }
    return atom_unit(a);
}

static Atom *fs_write_text(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    return fs_write_like(a, head, args, nargs, false);
}

static Atom *fs_append_text(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    return fs_write_like(a, head, args, nargs, true);
}

static Atom *fs_read_lines(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    const char *path;
    CettaStringBuf text;
    char errbuf[160];
    char **items = NULL;
    uint32_t nitems = 0;
    uint32_t cap = 0;
    size_t start = 0;
    size_t len;
    Atom *result;

    if (nargs != 1 || !(path = library_text_arg(args[0]))) {
        return library_signature_error(a, head, args, nargs, "expected filename");
    }
    if (!library_read_text_file(path, &text, errbuf, sizeof(errbuf))) {
        return atom_error(a, library_call_expr(a, head, args, nargs), atom_string(a, errbuf));
    }

    len = text.len;
    if (len == 0) {
        result = atom_expr(a, NULL, 0);
        cetta_sb_free(&text);
        return result;
    }

    for (size_t i = 0; i <= len; i++) {
        bool at_end = (i == len);
        bool at_break = (!at_end && text.buf[i] == '\n');
        if (!at_end && !at_break) continue;
        size_t stop = i;
        if (stop > start && text.buf[stop - 1] == '\r') {
            stop--;
        }
        if (!(at_end && start == len)) {
            if (nitems >= cap) {
                cap = cap ? cap * 2 : 8;
                items = cetta_realloc(items, sizeof(char *) * cap);
            }
            size_t seg_len = stop - start;
            char *line = cetta_malloc(seg_len + 1);
            memcpy(line, text.buf + start, seg_len);
            line[seg_len] = '\0';
            items[nitems++] = line;
        }
        start = i + 1;
    }

    if (nitems > 0 && text.buf[len - 1] == '\n' && items[nitems - 1][0] == '\0') {
        free(items[nitems - 1]);
        nitems--;
    }

    result = library_string_list(a, items, nitems);
    for (uint32_t i = 0; i < nitems; i++) {
        free(items[i]);
    }
    free(items);
    cetta_sb_free(&text);
    return result;
}

static Atom *cetta_library_dispatch_fs(Arena *a, Atom *head,
                                       Atom **args, uint32_t nargs) {
    if (head->kind != ATOM_SYMBOL) return NULL;
    if (strcmp(head->name, "__cetta_lib_fs_exists") == 0) {
        return fs_exists(a, head, args, nargs);
    }
    if (strcmp(head->name, "__cetta_lib_fs_read_text") == 0) {
        return fs_read_text(a, head, args, nargs);
    }
    if (strcmp(head->name, "__cetta_lib_fs_write_text") == 0) {
        return fs_write_text(a, head, args, nargs);
    }
    if (strcmp(head->name, "__cetta_lib_fs_append_text") == 0) {
        return fs_append_text(a, head, args, nargs);
    }
    if (strcmp(head->name, "__cetta_lib_fs_read_lines") == 0) {
        return fs_read_lines(a, head, args, nargs);
    }
    return NULL;
}

static Atom *str_length(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    const char *text;
    if (nargs != 1 || !(text = library_text_arg(args[0]))) {
        return library_signature_error(a, head, args, nargs, "expected text argument");
    }
    return atom_int(a, (int64_t)strlen(text));
}

static Atom *str_concat(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    const char *lhs;
    const char *rhs;
    CettaStringBuf out;
    Atom *result;
    if (nargs != 2 || !(lhs = library_text_arg(args[0])) || !(rhs = library_text_arg(args[1]))) {
        return library_signature_error(a, head, args, nargs, "expected two text arguments");
    }
    cetta_sb_init(&out);
    cetta_sb_append(&out, lhs);
    cetta_sb_append(&out, rhs);
    result = atom_string(a, out.buf ? out.buf : "");
    cetta_sb_free(&out);
    return result;
}

static Atom *str_split(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    const char *sep;
    const char *text;
    size_t sep_len;
    size_t text_len;
    size_t start = 0;
    char **items = NULL;
    uint32_t nitems = 0;
    uint32_t cap = 0;
    Atom *result;

    if (nargs != 2 || !(sep = library_text_arg(args[0])) || !(text = library_text_arg(args[1]))) {
        return library_signature_error(a, head, args, nargs, "expected separator and text");
    }
    sep_len = strlen(sep);
    text_len = strlen(text);
    if (sep_len == 0) {
        return library_signature_error(a, head, args, nargs,
                                       "separator must be non-empty");
    }

    while (1) {
        const char *found = strstr(text + start, sep);
        size_t stop = found ? (size_t)(found - text) : text_len;
        if (nitems >= cap) {
            cap = cap ? cap * 2 : 8;
            items = cetta_realloc(items, sizeof(char *) * cap);
        }
        size_t seg_len = stop - start;
        char *piece = cetta_malloc(seg_len + 1);
        memcpy(piece, text + start, seg_len);
        piece[seg_len] = '\0';
        items[nitems++] = piece;
        if (!found) break;
        start = stop + sep_len;
    }

    result = library_string_list(a, items, nitems);
    for (uint32_t i = 0; i < nitems; i++) {
        free(items[i]);
    }
    free(items);
    return result;
}

static Atom *str_split_whitespace(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    const char *text;
    char **items = NULL;
    uint32_t nitems = 0;
    uint32_t cap = 0;
    Atom *result;

    if (nargs != 1 || !(text = library_text_arg(args[0]))) {
        return library_signature_error(a, head, args, nargs, "expected text argument");
    }

    while (*text) {
        while (*text && isspace((unsigned char)*text)) text++;
        if (!*text) break;
        const char *start = text;
        while (*text && !isspace((unsigned char)*text)) text++;
        size_t seg_len = (size_t)(text - start);
        char *piece = cetta_malloc(seg_len + 1);
        memcpy(piece, start, seg_len);
        piece[seg_len] = '\0';
        if (nitems >= cap) {
            cap = cap ? cap * 2 : 8;
            items = cetta_realloc(items, sizeof(char *) * cap);
        }
        items[nitems++] = piece;
    }

    result = library_string_list(a, items, nitems);
    for (uint32_t i = 0; i < nitems; i++) {
        free(items[i]);
    }
    free(items);
    return result;
}

static Atom *str_join(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    const char *sep;
    CettaStringBuf out;
    Atom *result;
    if (nargs != 2 || !(sep = library_text_arg(args[0])) || !library_expr_of_texts(args[1])) {
        return library_signature_error(a, head, args, nargs,
                                       "expected separator and expression of text");
    }
    cetta_sb_init(&out);
    for (uint32_t i = 0; i < args[1]->expr.len; i++) {
        if (i > 0) cetta_sb_append(&out, sep);
        cetta_sb_append(&out, library_text_arg(args[1]->expr.elems[i]));
    }
    result = atom_string(a, out.buf ? out.buf : "");
    cetta_sb_free(&out);
    return result;
}

static Atom *str_slice(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    const char *text;
    int start;
    int stop;
    size_t len;
    if (nargs != 3 || !(text = library_text_arg(args[0])) ||
        !library_int_arg(args[1], &start) || !library_int_arg(args[2], &stop) ||
        start < 0 || stop < 0) {
        return library_signature_error(a, head, args, nargs,
                                       "expected text, non-negative start, non-negative end");
    }
    len = strlen(text);
    if ((size_t)start > len) start = (int)len;
    if ((size_t)stop > len) stop = (int)len;
    if (stop < start) stop = start;
    char *slice = cetta_malloc((size_t)(stop - start) + 1);
    memcpy(slice, text + start, (size_t)(stop - start));
    slice[stop - start] = '\0';
    Atom *result = atom_string(a, slice);
    free(slice);
    return result;
}

static Atom *str_find(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    const char *haystack;
    const char *needle;
    const char *found;
    if (nargs != 2 || !(haystack = library_text_arg(args[0])) ||
        !(needle = library_text_arg(args[1]))) {
        return library_signature_error(a, head, args, nargs,
                                       "expected text and search text");
    }
    found = strstr(haystack, needle);
    if (!found) return atom_empty(a);
    return atom_int(a, (int64_t)(found - haystack));
}

static Atom *str_starts_with(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    const char *text;
    const char *prefix;
    size_t prefix_len;
    if (nargs != 2 || !(text = library_text_arg(args[0])) ||
        !(prefix = library_text_arg(args[1]))) {
        return library_signature_error(a, head, args, nargs,
                                       "expected text and prefix");
    }
    prefix_len = strlen(prefix);
    return strncmp(text, prefix, prefix_len) == 0 ? atom_true(a) : atom_false(a);
}

static Atom *str_ends_with(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    const char *text;
    const char *suffix;
    size_t text_len;
    size_t suffix_len;
    if (nargs != 2 || !(text = library_text_arg(args[0])) ||
        !(suffix = library_text_arg(args[1]))) {
        return library_signature_error(a, head, args, nargs,
                                       "expected text and suffix");
    }
    text_len = strlen(text);
    suffix_len = strlen(suffix);
    if (suffix_len > text_len) return atom_false(a);
    return strcmp(text + text_len - suffix_len, suffix) == 0 ? atom_true(a) : atom_false(a);
}

static Atom *str_trim(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    const char *text;
    size_t len;
    size_t start = 0;
    size_t stop;
    if (nargs != 1 || !(text = library_text_arg(args[0]))) {
        return library_signature_error(a, head, args, nargs, "expected text argument");
    }
    len = strlen(text);
    while (start < len && isspace((unsigned char)text[start])) start++;
    stop = len;
    while (stop > start && isspace((unsigned char)text[stop - 1])) stop--;
    char *trimmed = cetta_malloc(stop - start + 1);
    memcpy(trimmed, text + start, stop - start);
    trimmed[stop - start] = '\0';
    Atom *result = atom_string(a, trimmed);
    free(trimmed);
    return result;
}

static Atom *cetta_library_dispatch_str(Arena *a, Atom *head,
                                        Atom **args, uint32_t nargs) {
    if (head->kind != ATOM_SYMBOL) return NULL;
    if (strcmp(head->name, "__cetta_lib_str_length") == 0) {
        return str_length(a, head, args, nargs);
    }
    if (strcmp(head->name, "__cetta_lib_str_concat") == 0) {
        return str_concat(a, head, args, nargs);
    }
    if (strcmp(head->name, "__cetta_lib_str_split") == 0) {
        return str_split(a, head, args, nargs);
    }
    if (strcmp(head->name, "__cetta_lib_str_split_whitespace") == 0) {
        return str_split_whitespace(a, head, args, nargs);
    }
    if (strcmp(head->name, "__cetta_lib_str_join") == 0) {
        return str_join(a, head, args, nargs);
    }
    if (strcmp(head->name, "__cetta_lib_str_slice") == 0) {
        return str_slice(a, head, args, nargs);
    }
    if (strcmp(head->name, "__cetta_lib_str_find") == 0) {
        return str_find(a, head, args, nargs);
    }
    if (strcmp(head->name, "__cetta_lib_str_starts_with") == 0) {
        return str_starts_with(a, head, args, nargs);
    }
    if (strcmp(head->name, "__cetta_lib_str_ends_with") == 0) {
        return str_ends_with(a, head, args, nargs);
    }
    if (strcmp(head->name, "__cetta_lib_str_trim") == 0) {
        return str_trim(a, head, args, nargs);
    }
    return NULL;
}

static bool result_set_has_error(ResultSet *rs) {
    for (uint32_t i = 0; i < rs->len; i++) {
        if (atom_is_error(rs->items[i])) return true;
    }
    return false;
}

static Atom *result_set_first_error(Arena *dst, ResultSet *rs) {
    for (uint32_t i = 0; i < rs->len; i++) {
        if (atom_is_error(rs->items[i])) {
            return atom_deep_copy(dst, rs->items[i]);
        }
    }
    return NULL;
}

static Atom *module_reason(Arena *a, const char *tag, const char *path) {
    return atom_expr(a, (Atom *[]){
        atom_symbol(a, tag),
        atom_string(a, path)
    }, 2);
}

static Atom *module_reason_with_detail(Arena *a, const char *tag,
                                       const char *path, Atom *detail) {
    return atom_expr(a, (Atom *[]){
        atom_symbol(a, tag),
        atom_string(a, path),
        detail
    }, 3);
}

static bool load_module_file(CettaLibraryContext *ctx, const char *path,
                             Space *logical_space, Space *work_space,
                             Arena *eval_arena,
                             Arena *persistent_arena, Registry *registry,
                             int fuel, Atom **error_out) {
    int slot = imported_file_lookup(ctx, logical_space, path);
    if (slot >= 0) {
        if (ctx->imported_files[slot].loading) {
            *error_out = module_reason(eval_arena, "ModuleImportCycle", path);
            return false;
        }
        return true;
    }
    if (ctx->imported_file_len >= CETTA_MAX_IMPORTED_FILES) {
        *error_out = atom_symbol(eval_arena, "too many imported files");
        return false;
    }

    slot = (int)ctx->imported_file_len++;
    ctx->imported_files[slot].space = logical_space;
    ctx->imported_files[slot].loading = true;
    snprintf(ctx->imported_files[slot].path,
             sizeof(ctx->imported_files[slot].path), "%s", path);

    char import_dir[PATH_MAX];
    copy_parent_dir(import_dir, sizeof(import_dir), path);
    cetta_library_push_dir(ctx, import_dir);

    Atom *prev_self = NULL;
    if (registry) {
        prev_self = registry_lookup(registry, "&self");
        registry_bind(registry, "&self", atom_space(persistent_arena, work_space));
    }

    Atom **atoms = NULL;
    CettaModuleFormat format = {
        .kind = CETTA_MODULE_FORMAT_METTA,
        .foreign_backend = CETTA_FOREIGN_BACKEND_NONE,
    };
    CettaLoadedModule *loaded = loaded_module_lookup(ctx, path);
    if (loaded) {
        format = loaded->format;
    }

    bool ok = true;
    if (format.kind == CETTA_MODULE_FORMAT_FOREIGN) {
        ok = cetta_foreign_load_module(ctx->foreign_runtime, path, work_space,
                                       persistent_arena, error_out);
        if (!ok && error_out && *error_out) {
            *error_out = module_reason_with_detail(
                eval_arena, "ModuleForeignLoadFailed", path, *error_out);
        }
    } else {
        int n = parse_metta_file(path, persistent_arena, &atoms);
        if (n < 0) {
            ok = false;
            *error_out = module_reason(eval_arena, "ModuleParseFailed", path);
        } else {
            for (int i = 0; i < n; i++) {
                Atom *at = atoms[i];
                if (at->kind == ATOM_SYMBOL && strcmp(at->name, "!") == 0 && i + 1 < n) {
                    ResultSet rs;
                    result_set_init(&rs);
                    eval_top_with_registry(work_space, eval_arena, persistent_arena, registry,
                                           atoms[i + 1], &rs);
                    Atom *first_error = result_set_first_error(eval_arena, &rs);
                    bool stop_after_error = first_error != NULL || result_set_has_error(&rs);
                    free(rs.items);
                    eval_release_temporary_spaces();
                    if (stop_after_error) {
                        *error_out = first_error ? first_error :
                            module_reason(eval_arena, "ModuleInitError", path);
                        ok = false;
                        break;
                    }
                    i++;
                    continue;
                }
                space_add(work_space, at);
            }
        }
    }

    if (registry) {
        if (prev_self) {
            registry_bind(registry, "&self", prev_self);
        }
    }
    cetta_library_pop_dir(ctx);
    free(atoms);

    if (!ok) {
        ctx->imported_files[slot].loading = false;
        ctx->imported_files[slot].path[0] = '\0';
        return false;
    }

    ctx->imported_files[slot].loading = false;
    return true;
}

static bool load_module_file_transactional(CettaLibraryContext *ctx, const char *path,
                                           Space *target, Arena *eval_arena,
                                           Arena *persistent_arena, Registry *registry,
                                           int fuel, Atom **error_out) {
    uint32_t imported_len_before = ctx->imported_file_len;
    Space *work_space = space_heap_clone_shallow(target);
    if (!cetta_library_push_import_alias(ctx, work_space, target)) {
        space_free(work_space);
        free(work_space);
        *error_out = atom_symbol(eval_arena, "too many nested import transactions");
        return false;
    }

    bool ok = load_module_file(ctx, path, target, work_space, eval_arena,
                               persistent_arena, registry, fuel, error_out);
    cetta_library_pop_import_alias(ctx);

    if (!ok) {
        rollback_imported_files(ctx, imported_len_before);
        space_free(work_space);
        free(work_space);
        return false;
    }

    space_replace_contents(target, work_space);
    space_free(work_space);
    free(work_space);
    return true;
}

static bool execute_import_plan(CettaLibraryContext *ctx, const CettaImportPlan *plan,
                                Arena *eval_arena,
                                Arena *persistent_arena, Registry *registry,
                                int fuel, Atom **error_out) {
    uint32_t imported_len_before = ctx->imported_file_len;
    uint32_t loaded_len_before = ctx->loaded_module_len;
    if (!remember_loaded_module(ctx, plan, NULL, eval_arena, error_out)) {
        return false;
    }
    if (plan->transactional &&
        plan->logical_target_space == plan->execution_target_space) {
        bool ok = load_module_file_transactional(ctx, plan->canonical_path,
                                                 plan->execution_target_space,
                                                 eval_arena, persistent_arena,
                                                 registry, fuel, error_out);
        if (!ok) {
            rollback_loaded_modules(ctx, loaded_len_before);
        }
        return ok;
    }
    if (!load_module_file(ctx, plan->canonical_path, plan->logical_target_space,
                          plan->execution_target_space, eval_arena,
                          persistent_arena, registry, fuel, error_out)) {
        if (plan->target_is_fresh) {
            rollback_imported_files(ctx, imported_len_before);
        }
        rollback_loaded_modules(ctx, loaded_len_before);
        return false;
    }
    return true;
}

static CettaLoadedModule *loaded_module_lookup(CettaLibraryContext *ctx,
                                               const char *canonical_path) {
    for (uint32_t i = 0; i < ctx->loaded_module_len; i++) {
        if (strcmp(ctx->loaded_modules[i].canonical_path, canonical_path) == 0) {
            return &ctx->loaded_modules[i];
        }
    }
    return NULL;
}

static CettaLoadedModule *remember_loaded_module(CettaLibraryContext *ctx,
                                                 const CettaImportPlan *plan,
                                                 Space *space,
                                                 Arena *eval_arena,
                                                 Atom **error_out) {
    CettaLoadedModule *entry = loaded_module_lookup(ctx, plan->canonical_path);
    if (entry) {
        if (space && !entry->space) {
            entry->space = space;
        }
        entry->format = plan->format;
        return entry;
    }
    if (ctx->loaded_module_len >= CETTA_MAX_LOADED_MODULES) {
        *error_out = atom_symbol(eval_arena, "too many loaded modules");
        return NULL;
    }
    entry = &ctx->loaded_modules[ctx->loaded_module_len++];
    memset(entry, 0, sizeof(*entry));
    snprintf(entry->display_name, sizeof(entry->display_name), "%s", plan->spec.raw_spec);
    snprintf(entry->canonical_path, sizeof(entry->canonical_path), "%s", plan->canonical_path);
    entry->provider_kind = plan->provider_kind;
    entry->format = plan->format;
    entry->space = space;
    entry->loading = false;
    return entry;
}

static CettaLoadedModule *ensure_loaded_module(CettaLibraryContext *ctx,
                                               const CettaImportPlan *plan,
                                               Arena *eval_arena,
                                               Arena *persistent_arena,
                                               Registry *registry,
                                               int fuel, Atom **error_out) {
    uint32_t loaded_len_before = ctx->loaded_module_len;
    CettaLoadedModule *entry = remember_loaded_module(ctx, plan, NULL, eval_arena, error_out);
    if (!entry) {
        return NULL;
    }
    if (entry->loading) {
        *error_out = atom_symbol(eval_arena, "module already loading");
        return NULL;
    }
    if (entry->space) {
        return entry;
    }
    entry->space = malloc(sizeof(Space));
    if (!entry->space) {
        rollback_loaded_modules(ctx, loaded_len_before);
        *error_out = atom_symbol(eval_arena, "module space allocation failed");
        return NULL;
    }
    space_init(entry->space);
    entry->loading = true;

    bool ok = load_module_file(ctx, plan->canonical_path, entry->space, entry->space,
                               eval_arena, persistent_arena, registry, fuel, error_out);
    entry->loading = false;
    if (!ok) {
        rollback_loaded_modules(ctx, loaded_len_before);
        return NULL;
    }
    return entry;
}

Atom *cetta_library_mod_space(CettaLibraryContext *ctx, const char *spec,
                              Arena *eval_arena, Arena *persistent_arena,
                              Registry *registry, int fuel, Atom **error_out) {
    CettaModuleSpec parsed_spec;
    CettaImportPlan plan;
    if (!parse_module_spec(spec, &parsed_spec, eval_arena, error_out)) {
        return NULL;
    }
    if (!resolve_import_plan(ctx, &parsed_spec, NULL, NULL, false,
                             &plan, eval_arena, error_out)) {
        return NULL;
    }
    CettaLoadedModule *entry = ensure_loaded_module(ctx, &plan, eval_arena,
                                                    persistent_arena, registry,
                                                    fuel, error_out);
    if (!entry) return NULL;
    Arena *dst = persistent_arena ? persistent_arena : eval_arena;
    return atom_space(dst, entry->space);
}

Atom *cetta_library_module_inventory_space(CettaLibraryContext *ctx,
                                           Arena *eval_arena,
                                           Arena *persistent_arena,
                                           Atom **error_out) {
    (void)error_out;
    if (!ctx) return NULL;

    Arena *dst = persistent_arena ? persistent_arena : eval_arena;
    Space *inventory = arena_alloc(dst, sizeof(Space));
    space_init(inventory);

    const char *profile_name = (ctx->session.profile && ctx->session.profile->name) ?
        ctx->session.profile->name : "unknown";
    space_add(inventory, atom_expr2(dst,
        atom_symbol(dst, "module-profile"),
        atom_symbol(dst, profile_name)));

    for (uint32_t i = 0; i < cetta_module_provider_count(); i++) {
        const CettaModuleProviderDescriptor *desc = cetta_module_provider_at(i);
        if (!desc) continue;
        const char *provider_name = desc->name;
        bool enabled = module_provider_visible(ctx, desc->kind);
        space_add(inventory, atom_expr3(dst,
            atom_symbol(dst, "module-provider"),
            atom_symbol(dst, provider_name),
            atom_symbol(dst, enabled ? "enabled" : "disabled")));
        space_add(inventory, atom_expr3(dst,
            atom_symbol(dst, "module-provider-implementation"),
            atom_symbol(dst, provider_name),
            atom_symbol(dst, desc->implemented ? "implemented" : "deferred")));
        space_add(inventory, atom_expr3(dst,
            atom_symbol(dst, "module-provider-transport"),
            atom_symbol(dst, provider_name),
            atom_symbol(dst, desc->remote_source ? "remote" : "local")));
        space_add(inventory, atom_expr3(dst,
            atom_symbol(dst, "module-provider-cache-policy"),
            atom_symbol(dst, provider_name),
            atom_symbol(dst, desc->cache_backed ? "cache-backed" : "no-cache")));
        space_add(inventory, atom_expr3(dst,
            atom_symbol(dst, "module-provider-locator-kind"),
            atom_symbol(dst, provider_name),
            atom_symbol(dst, cetta_module_locator_kind_name(desc->locator_kind))));
        space_add(inventory, atom_expr3(dst,
            atom_symbol(dst, "module-provider-update-policy"),
            atom_symbol(dst, provider_name),
            atom_symbol(dst, desc->update_policy ? desc->update_policy : "unknown")));
        space_add(inventory, atom_expr3(dst,
            atom_symbol(dst, "module-provider-revision-policy"),
            atom_symbol(dst, provider_name),
            atom_symbol(dst, cetta_remote_revision_policy_name(desc->revision_policy))));
    }

    for (uint32_t i = 0; i < ctx->module_mount_len; i++) {
        const CettaModuleMount *mount = &ctx->module_mounts[i];
        if (!module_mount_visible(ctx, mount)) continue;
        Atom *elems[4] = {
            atom_symbol(dst, "module-mount"),
            atom_symbol(dst, mount->namespace_name),
            atom_string(dst, mount->root_path),
            atom_symbol(dst, cetta_module_provider_name(mount->provider_kind))
        };
        space_add(inventory, atom_expr(dst, elems, 4));
        Atom *source_fact[4] = {
            atom_symbol(dst, "module-mount-source"),
            atom_symbol(dst, mount->namespace_name),
            atom_string(dst, mount->source_locator),
            atom_symbol(dst, cetta_module_locator_kind_name(mount->locator_kind))
        };
        space_add(inventory, atom_expr(dst, source_fact, 4));
        Atom *revision_fact[4] = {
            atom_symbol(dst, "module-mount-revision-policy"),
            atom_symbol(dst, mount->namespace_name),
            atom_symbol(dst, cetta_remote_revision_policy_name(mount->revision_policy)),
            atom_string(dst, mount->revision_value[0] ? mount->revision_value : "none")
        };
        space_add(inventory, atom_expr(dst, revision_fact, 4));
    }

    for (uint32_t i = 0; i < ctx->loaded_module_len; i++) {
        const CettaLoadedModule *module = &ctx->loaded_modules[i];
        if (!loaded_module_visible(ctx, module)) continue;
        Atom *module_fact[4] = {
            atom_symbol(dst, "loaded-module"),
            atom_string(dst, module->display_name),
            atom_string(dst, module->canonical_path),
            atom_symbol(dst, cetta_module_provider_name(module->provider_kind))
        };
        space_add(inventory, atom_expr(dst, module_fact, 4));
        Atom *format_fact[4] = {
            atom_symbol(dst, "loaded-module-format"),
            atom_string(dst, module->display_name),
            atom_symbol(dst, cetta_module_format_name(module->format.kind)),
            atom_symbol(dst, cetta_foreign_backend_name(module->format.foreign_backend))
        };
        space_add(inventory, atom_expr(dst, format_fact, 4));
        if (module->space) {
            Atom *space_fact[4] = {
                atom_symbol(dst, "loaded-module-space"),
                atom_string(dst, module->display_name),
                atom_space(dst, module->space),
                atom_symbol(dst, cetta_module_provider_name(module->provider_kind))
            };
            space_add(inventory, atom_expr(dst, space_fact, 4));
        }
    }

    return atom_space(dst, inventory);
}

bool cetta_library_include_module(CettaLibraryContext *ctx, const char *spec,
                                  Space *space, Arena *eval_arena,
                                  Arena *persistent_arena, Registry *registry,
                                  int fuel, Atom **error_out) {
    CettaModuleSpec parsed_spec;
    CettaImportPlan plan;
    if (!parse_module_spec(spec, &parsed_spec, eval_arena, error_out)) {
        return false;
    }
    if (!resolve_import_plan(ctx, &parsed_spec, logical_import_space(ctx, space), space,
                             false, &plan, eval_arena, error_out)) {
        return false;
    }
    return execute_import_plan(ctx, &plan, eval_arena, persistent_arena,
                               registry, fuel, error_out);
}

bool cetta_library_print_loaded_modules(CettaLibraryContext *ctx, FILE *out,
                                        Arena *eval_arena, Atom **error_out) {
    (void)eval_arena;
    (void)error_out;
    if (!ctx || !out) return false;
    fprintf(out, "top = 0\n");
    uint32_t visible_count = cetta_library_loaded_module_count(ctx);
    for (uint32_t i = 0; i < visible_count; i++) {
        const CettaLoadedModule *module = cetta_library_loaded_module_at(ctx, i);
        const char *branch = (i + 1 == visible_count) ? "└" : "├";
        fprintf(out, " %s─%s = %u\n", branch, module->display_name, i + 1);
    }
    fflush(out);
    return true;
}

bool cetta_library_import(CettaLibraryContext *ctx, const char *name,
                          Space *space, Arena *eval_arena,
                          Arena *persistent_arena, Registry *registry,
                          int fuel, Atom **error_out) {
    const CettaLibrarySpec *spec = cetta_library_lookup(name);
    const CettaNativeBuiltinModule *native_spec = spec ? NULL : cetta_native_module_lookup(name);
    CettaModuleSpec parsed_spec;
    CettaImportPlan plan;
    uint32_t import_bit;
    const char *import_name;

    if (!spec && !native_spec) {
        *error_out = atom_symbol(eval_arena, "unknown library");
        return false;
    }
    import_bit = spec ? spec->bit : native_spec->import_bit;
    import_name = spec ? spec->name : native_spec->name;
    if (ctx->active_mask & import_bit) return true;

    ctx->active_mask |= import_bit;
    memset(&parsed_spec, 0, sizeof(parsed_spec));
    parsed_spec.kind = CETTA_MODULE_SPEC_STDLIB;
    snprintf(parsed_spec.raw_spec, sizeof(parsed_spec.raw_spec), "%s", import_name);
    snprintf(parsed_spec.path_or_member, sizeof(parsed_spec.path_or_member), "%s", import_name);
    if (!resolve_import_plan(ctx, &parsed_spec, space, space, false,
                             &plan, eval_arena, error_out) ||
        !execute_import_plan(ctx, &plan, eval_arena, persistent_arena,
                             registry, fuel, error_out)) {
        ctx->active_mask &= ~import_bit;
        return false;
    }
    return true;
}

bool cetta_library_register_module(CettaLibraryContext *ctx, const char *path,
                                   Arena *eval_arena, Atom **error_out) {
    char candidate[PATH_MAX];
    char resolved[PATH_MAX];
    struct stat st;

    if (!cetta_module_resolver_allows(&ctx->session.module_resolver,
                                      CETTA_MODULE_PROVIDER_REGISTERED_ROOTS)) {
        *error_out = atom_symbol(eval_arena, "registered module roots disabled");
        return false;
    }

    if (!path || !*path) {
        *error_out = atom_symbol(eval_arena, "empty module path");
        return false;
    }

    int n;
    if (path[0] == '/') {
        n = snprintf(candidate, sizeof(candidate), "%s", path);
    } else {
        n = snprintf(candidate, sizeof(candidate), "%s/%s",
                     cetta_library_current_dir(ctx), path);
    }
    if (!(n > 0 && (size_t)n < sizeof(candidate))) {
        *error_out = atom_symbol(eval_arena, "module path too long");
        return false;
    }

    if (!realpath(candidate, resolved) || stat(resolved, &st) != 0 || !S_ISDIR(st.st_mode)) {
        *error_out = atom_symbol(eval_arena, "module root not found");
        return false;
    }

    const char *base = strrchr(resolved, '/');
    const char *name = base ? base + 1 : resolved;
    return upsert_module_mount(ctx, name, resolved,
                               CETTA_MODULE_PROVIDER_REGISTERED_ROOT,
                               CETTA_MODULE_LOCATOR_FILESYSTEM_PATH,
                               resolved,
                               CETTA_REMOTE_REVISION_NONE,
                               "",
                               CETTA_PROFILE_MASK_ALL,
                               eval_arena, error_out);
}

bool cetta_library_register_git_module(CettaLibraryContext *ctx, const char *url,
                                       Arena *eval_arena, Atom **error_out) {
    char module_name[CETTA_MAX_MODULE_NAMESPACE];
    char cached_root[PATH_MAX];

    if (!cetta_module_resolver_allows(&ctx->session.module_resolver,
                                      CETTA_MODULE_PROVIDER_GIT)) {
        *error_out = atom_symbol(eval_arena, "git module provider disabled");
        return false;
    }
    if (!url || !*url) {
        *error_out = atom_symbol(eval_arena, "empty git module URL");
        return false;
    }
    if (!ensure_git_cached_repo(ctx, url, module_name, sizeof(module_name),
                                cached_root, sizeof(cached_root),
                                eval_arena, error_out)) {
        return false;
    }
    return upsert_module_mount(ctx, module_name, cached_root,
                               CETTA_MODULE_PROVIDER_GIT_REMOTE,
                               CETTA_MODULE_LOCATOR_GIT_URL,
                               url,
                               CETTA_REMOTE_REVISION_DEFAULT_BRANCH_ONLY,
                               "",
                               CETTA_PROFILE_MASK_ALL,
                               eval_arena, error_out);
}

bool cetta_library_import_module(CettaLibraryContext *ctx, const char *spec,
                                 Space *space, bool target_is_fresh,
                                 Arena *eval_arena,
                                 Arena *persistent_arena, Registry *registry,
                                 int fuel, Atom **error_out) {
    Space *logical_space = logical_import_space(ctx, space);
    CettaModuleSpec parsed_spec;
    CettaImportPlan plan;

    if (spec && (cetta_library_lookup(spec) || cetta_native_module_lookup(spec))) {
        if (target_is_fresh) {
            *error_out = atom_symbol(eval_arena,
                                     "builtin libraries can only import into existing spaces");
            return false;
        }
        return cetta_library_import(ctx, spec, space, eval_arena,
                                    persistent_arena, registry, fuel, error_out);
    }

    if (!parse_module_spec(spec, &parsed_spec, eval_arena, error_out)) {
        return false;
    }
    if (!resolve_import_plan(ctx, &parsed_spec, logical_space, space,
                             target_is_fresh, &plan, eval_arena, error_out)) {
        return false;
    }
    return execute_import_plan(ctx, &plan, eval_arena, persistent_arena,
                               registry, fuel, error_out);
}

Atom *cetta_library_dispatch_native(CettaLibraryContext *ctx, Space *space,
                                    Arena *a,
                                    Atom *head, Atom **args, uint32_t nargs) {
    if (!ctx || !head || head->kind != ATOM_SYMBOL) return NULL;
    if (ctx->active_mask & CETTA_LIBRARY_SYSTEM) {
        Atom *result = cetta_library_dispatch_system(ctx, a, head, args, nargs);
        if (result) return result;
    }
    if (ctx->active_mask & CETTA_LIBRARY_FS) {
        Atom *result = cetta_library_dispatch_fs(a, head, args, nargs);
        if (result) return result;
    }
    if (ctx->active_mask & CETTA_LIBRARY_STR) {
        Atom *result = cetta_library_dispatch_str(a, head, args, nargs);
        if (result) return result;
    }
    {
        Atom *result = cetta_native_module_dispatch_active(ctx, space, a, head, args,
                                                           nargs, ctx->active_mask);
        if (result) return result;
    }
    if (ctx->foreign_runtime) {
        Atom *result = cetta_foreign_dispatch_native(ctx->foreign_runtime,
                                                     space, a, head, args, nargs);
        if (result) return result;
    }
    return NULL;
}

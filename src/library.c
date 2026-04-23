#define _GNU_SOURCE
#include "library.h"

#include "eval.h"
#include "lang_adapter.h"
#include "mm2_lower.h"
#include "mork_space_bridge_runtime.h"
#include "native/native_modules.h"
#include "parser.h"
#include "stats.h"
#include "text_source.h"
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
#include <time.h>
#include <unistd.h>

enum {
    CETTA_LIBRARY_SYSTEM = 1u << 0,
    CETTA_LIBRARY_FS = 1u << 1,
    CETTA_LIBRARY_STR = 1u << 2,
    CETTA_LIBRARY_MORK = 1u << 3
};

typedef struct {
    const char *name;
    uint32_t bit;
} CettaLibrarySpec;

static const CettaLibrarySpec CETTA_LIBRARIES[] = {
    {"system", CETTA_LIBRARY_SYSTEM},
    {"fs", CETTA_LIBRARY_FS},
    {"str", CETTA_LIBRARY_STR},
    {"mork", CETTA_LIBRARY_MORK},
};

static const char *CETTA_MM2_PROGRAM_HANDLE_KIND = "mork-program";
static const char *CETTA_MM2_CONTEXT_HANDLE_KIND = "mork-context";
static const char *CETTA_MORK_SPACE_HANDLE_KIND = "mork-space";
static const char *CETTA_MORK_CURSOR_HANDLE_KIND = "mork-cursor";
static const char *CETTA_MORK_PRODUCT_CURSOR_HANDLE_KIND = "mork-product-cursor";
static const char *CETTA_MORK_OVERLAY_CURSOR_HANDLE_KIND = "mork-overlay-cursor";
static const uint64_t CETTA_MM2_DEFAULT_RUN_STEPS = 1000000000000000ULL;

typedef struct {
    CettaMorkSpaceHandle *bridge_space;
    SpaceKind kind;
} CettaMorkSpaceResource;

typedef struct {
    CettaMorkCursorHandle *cursor;
    SpaceKind kind;
} CettaMorkCursorResource;

typedef struct {
    CettaMorkProductCursorHandle *cursor;
} CettaMorkProductCursorResource;

typedef struct {
    CettaMorkOverlayCursorHandle *cursor;
} CettaMorkOverlayCursorResource;

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} CettaStringBuf;

static uint64_t library_monotonic_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void library_mork_cursor_free_resource(void *resource);
static void library_mork_product_cursor_free_resource(void *resource);
static void library_mork_overlay_cursor_free_resource(void *resource);
static void library_mork_space_free_resource(void *resource);
static bool load_act_dump_text_into_space(CettaLibraryContext *ctx,
                                          const char *path,
                                          const uint8_t *bytes, size_t len,
                                          Space *target,
                                          Arena *persistent_arena,
                                          Arena *eval_arena,
                                          Atom **error_out);

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

static void cetta_sb_append_u32_be(CettaStringBuf *sb, uint32_t value) {
    char bytes[4];
    bytes[0] = (char)((value >> 24) & 0xff);
    bytes[1] = (char)((value >> 16) & 0xff);
    bytes[2] = (char)((value >> 8) & 0xff);
    bytes[3] = (char)(value & 0xff);
    cetta_sb_append_n(sb, bytes, sizeof(bytes));
}

static void cetta_sb_free(CettaStringBuf *sb) {
    free(sb->buf);
    sb->buf = NULL;
    sb->len = 0;
    sb->cap = 0;
}

bool cetta_library_pack_mork_expr_batch(Arena *scratch, Atom **items,
                                        uint32_t item_count,
                                        uint8_t **packet_out,
                                        size_t *packet_len_out,
                                        uint64_t *packet_bytes_out,
                                        uint64_t *pack_ns_out,
                                        const char **error_out) {
    CettaStringBuf packet;
    uint64_t total_pack_ns = 0;
    uint64_t total_packet_bytes = 0;

    if (!packet_out || !packet_len_out) {
        if (error_out)
            *error_out = "missing packet output for MORK expr-byte batch";
        return false;
    }

    *packet_out = NULL;
    *packet_len_out = 0;
    if (packet_bytes_out)
        *packet_bytes_out = 0;
    if (pack_ns_out)
        *pack_ns_out = 0;
    if (error_out)
        *error_out = NULL;

    cetta_sb_init(&packet);
    for (uint32_t i = 0; i < item_count; i++) {
        uint8_t *expr_bytes = NULL;
        size_t expr_len = 0;
        const char *encode_error = NULL;
        uint64_t item_started_ns = pack_ns_out ? library_monotonic_ns() : 0;
        if (!cetta_mm2_atom_to_bridge_expr_bytes(
                scratch, items[i], &expr_bytes, &expr_len, &encode_error)) {
            free(expr_bytes);
            cetta_sb_free(&packet);
            if (error_out) {
                *error_out = encode_error ? encode_error
                                          : "MORK expr-byte lowering failed";
            }
            return false;
        }
        cetta_sb_append_u32_be(&packet, (uint32_t)expr_len);
        cetta_sb_append_n(&packet, (const char *)expr_bytes, expr_len);
        free(expr_bytes);
        total_packet_bytes += (uint64_t)expr_len + 4u;
        if (pack_ns_out) {
            uint64_t item_finished_ns = library_monotonic_ns();
            if (item_finished_ns >= item_started_ns) {
                total_pack_ns += item_finished_ns - item_started_ns;
            }
        }
    }

    *packet_out = (uint8_t *)packet.buf;
    *packet_len_out = packet.len;
    if (packet_bytes_out)
        *packet_bytes_out = total_packet_bytes;
    if (pack_ns_out)
        *pack_ns_out = total_pack_ns;
    return true;
}

static bool cetta_library_pack_mork_expr_batch_from_ids(
    Arena *scratch,
    const TermUniverse *universe,
    const AtomId *items,
    uint32_t item_count,
    uint8_t **packet_out,
    size_t *packet_len_out,
    const char **error_out
) {
    CettaStringBuf packet;

    if (!packet_out || !packet_len_out) {
        if (error_out)
            *error_out = "missing packet output for MORK expr-byte batch";
        return false;
    }

    *packet_out = NULL;
    *packet_len_out = 0;
    if (error_out)
        *error_out = NULL;

    cetta_sb_init(&packet);
    for (uint32_t i = 0; i < item_count; i++) {
        uint8_t *expr_bytes = NULL;
        size_t expr_len = 0;
        const char *encode_error = NULL;
        if (!cetta_mm2_atom_id_to_bridge_expr_bytes(
                scratch, universe, items[i], &expr_bytes, &expr_len, &encode_error)) {
            free(expr_bytes);
            cetta_sb_free(&packet);
            if (error_out) {
                *error_out = encode_error ? encode_error
                                          : "MORK expr-byte lowering failed";
            }
            return false;
        }
        cetta_sb_append_u32_be(&packet, (uint32_t)expr_len);
        cetta_sb_append_n(&packet, (const char *)expr_bytes, expr_len);
        free(expr_bytes);
    }

    *packet_out = (uint8_t *)packet.buf;
    *packet_len_out = packet.len;
    return true;
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
    cetta_library_context_init_with_profile(ctx, cetta_profile_he_extended(),
                                            cetta_language_lookup("he"));
}

void cetta_library_context_init_with_profile(CettaLibraryContext *ctx,
                                             const CettaProfile *profile,
                                             const CettaLanguageSpec *language) {
    memset(ctx, 0, sizeof(*ctx));
    cetta_eval_session_init(&ctx->session, profile, language);
    term_universe_init(&ctx->term_universe);
    if (!getcwd(ctx->working_dir, sizeof(ctx->working_dir))) {
        snprintf(ctx->working_dir, sizeof(ctx->working_dir), ".");
    }
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
    term_universe_free(&ctx->term_universe);
    cetta_native_handle_cleanup_all(ctx);
    if (ctx->foreign_runtime) {
        cetta_foreign_runtime_free(ctx->foreign_runtime);
        ctx->foreign_runtime = NULL;
    }
}

void cetta_library_context_set_exec_path(CettaLibraryContext *ctx, const char *argv0) {
    char resolved[PATH_MAX];
    char *slash;

    if (ctx && ctx->foreign_runtime)
        cetta_foreign_runtime_set_exec_root(ctx->foreign_runtime, NULL);
    ctx->root_dir[0] = '\0';
    if (!argv0) return;
    if (!realpath(argv0, resolved)) return;
    slash = strrchr(resolved, '/');
    if (!slash) return;
    *slash = '\0';
    snprintf(ctx->root_dir, sizeof(ctx->root_dir), "%s", resolved);
    if (ctx && ctx->foreign_runtime)
        cetta_foreign_runtime_set_exec_root(ctx->foreign_runtime, ctx->root_dir);
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
    if (ctx->working_dir[0] != '\0') {
        return ctx->working_dir;
    }
    return ".";
}

static CettaRelativeModulePolicy
cetta_library_relative_module_policy(CettaLibraryContext *ctx) {
    if (!ctx)
        return CETTA_RELATIVE_MODULE_POLICY_CURRENT_DIR_ONLY;
    return cetta_eval_session_relative_module_policy(&ctx->session);
}

static const char *cetta_library_relative_base_dir(CettaLibraryContext *ctx) {
    if (!ctx)
        return ".";
    if (cetta_library_relative_module_policy(ctx) ==
            CETTA_RELATIVE_MODULE_POLICY_WORKING_DIR_ONLY &&
        ctx->working_dir[0] != '\0') {
        return ctx->working_dir;
    }
    return cetta_library_current_dir(ctx);
}

static TermUniverse *cetta_library_space_universe(CettaLibraryContext *ctx,
                                                  Arena *persistent_arena) {
    if (!ctx || !persistent_arena)
        return NULL;
    if (!ctx->term_universe.persistent_arena)
        term_universe_set_persistent_arena(&ctx->term_universe, persistent_arena);
    if (ctx->term_universe.persistent_arena != persistent_arena)
        return NULL;
    return &ctx->term_universe;
}

static bool cetta_path_strip_prefix(const char *path, const char *prefix,
                                    const char **relative_out) {
    size_t prefix_len;

    if (!path || !prefix || !*prefix) return false;
    prefix_len = strlen(prefix);
    while (prefix_len > 1 && prefix[prefix_len - 1] == '/') {
        prefix_len--;
    }
    if (strncmp(path, prefix, prefix_len) != 0) return false;
    if (path[prefix_len] == '/') {
        if (relative_out) *relative_out = path + prefix_len + 1;
        return true;
    }
    if (path[prefix_len] == '\0') {
        if (relative_out) *relative_out = ".";
        return true;
    }
    return false;
}

static const char *cetta_library_display_path(CettaLibraryContext *ctx,
                                              const char *path,
                                              char *out, size_t out_sz) {
    const char *relative = NULL;
    char cwd[PATH_MAX];

    if (!out || out_sz == 0) return path ? path : "";
    if (!path || !*path) {
        out[0] = '\0';
        return out;
    }
    if (path[0] != '/') {
        snprintf(out, out_sz, "%s", path);
        return out;
    }
    if (ctx && ctx->script_dir[0] != '\0' &&
        cetta_path_strip_prefix(path, ctx->script_dir, &relative)) {
        snprintf(out, out_sz, "%s", relative);
        return out;
    }
    if (ctx && ctx->working_dir[0] != '\0' &&
        cetta_path_strip_prefix(path, ctx->working_dir, &relative)) {
        snprintf(out, out_sz, "%s", relative);
        return out;
    }
    if (ctx && ctx->root_dir[0] != '\0' &&
        cetta_path_strip_prefix(path, ctx->root_dir, &relative)) {
        snprintf(out, out_sz, "%s", relative);
        return out;
    }
    if (getcwd(cwd, sizeof(cwd)) &&
        cetta_path_strip_prefix(path, cwd, &relative)) {
        snprintf(out, out_sz, "%s", relative);
        return out;
    }
    snprintf(out, out_sz, "%s", path);
    return out;
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
    return spec && (*spec == '.' || strchr(spec, '/') != NULL ||
                    path_has_suffix(spec, ".metta") ||
                    path_has_suffix(spec, ".act") ||
                    path_has_suffix(spec, ".mm2"));
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
    if (!path_has_suffix(path, ".metta")) {
        if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
            int n = snprintf(path, sizeof(path), "%s.metta", candidate);
            if (!(n > 0 && (size_t)n < sizeof(path))) return false;
        }
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

static bool resolve_module_candidate_act(const char *candidate,
                                         char *out, size_t out_sz,
                                         char *reason, size_t reason_sz) {
    char path[PATH_MAX];

    if (reason && reason_sz > 0) reason[0] = '\0';
    if (path_has_suffix(candidate, ".act")) {
        snprintf(path, sizeof(path), "%s", candidate);
    } else {
        int n = snprintf(path, sizeof(path), "%s.act", candidate);
        if (!(n > 0 && (size_t)n < sizeof(path))) return false;
    }
    if (access(path, R_OK) != 0) return false;
    if (!realpath(path, out)) return false;
    return strlen(out) < out_sz;
}

static bool resolve_module_candidate_mm2(const char *candidate,
                                         char *out, size_t out_sz,
                                         char *reason, size_t reason_sz) {
    char path[PATH_MAX];

    if (reason && reason_sz > 0) reason[0] = '\0';
    if (path_has_suffix(candidate, ".mm2")) {
        snprintf(path, sizeof(path), "%s", candidate);
    } else {
        int n = snprintf(path, sizeof(path), "%s.mm2", candidate);
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

    char act_reason[160] = {0};
    if (resolve_module_candidate_act(candidate, out, out_sz,
                                     act_reason, sizeof(act_reason))) {
        if (format_out) {
            format_out->kind = CETTA_MODULE_FORMAT_MORK_ACT;
            format_out->foreign_backend = CETTA_FOREIGN_BACKEND_NONE;
        }
        if (reason && reason_sz > 0) reason[0] = '\0';
        return true;
    }

    char mm2_reason[160] = {0};
    if (resolve_module_candidate_mm2(candidate, out, out_sz,
                                     mm2_reason, sizeof(mm2_reason))) {
        if (format_out) {
            format_out->kind = CETTA_MODULE_FORMAT_MM2;
            format_out->foreign_backend = CETTA_FOREIGN_BACKEND_NONE;
        }
        if (reason && reason_sz > 0) reason[0] = '\0';
        return true;
    }

    if (reason && reason_sz > 0) {
        const char *chosen = metta_reason[0] ? metta_reason :
                             (act_reason[0] ? act_reason :
                              (mm2_reason[0] ? mm2_reason :
                               (foreign_reason[0] ? foreign_reason : "")));
        snprintf(reason, reason_sz, "%s", chosen);
    }
    return false;
}

static bool resolve_relative_module_candidate_for_language(
    CettaLibraryContext *ctx,
    const char *path,
    char *out, size_t out_sz,
    CettaModuleFormat *format_out,
    char *reason, size_t reason_sz
) {
    char base_dir[PATH_MAX];
    char candidate[PATH_MAX];

    if (reason && reason_sz > 0)
        reason[0] = '\0';
    if (!path || !*path)
        return false;
    if (path[0] == '/') {
        return resolve_module_candidate_with_format(path, out, out_sz, format_out,
                                                    reason, reason_sz);
    }
    snprintf(base_dir, sizeof(base_dir), "%s", cetta_library_relative_base_dir(ctx));
    while (true) {
        int n = snprintf(candidate, sizeof(candidate), "%s/%s", base_dir, path);
        if (!(n > 0 && (size_t)n < sizeof(candidate)))
            return false;
        if (resolve_module_candidate_with_format(candidate, out, out_sz, format_out,
                                                 reason, reason_sz)) {
            return true;
        }
        if (cetta_library_relative_module_policy(ctx) !=
            CETTA_RELATIVE_MODULE_POLICY_ANCESTOR_WALK) {
            break;
        }
        char parent[PATH_MAX];
        if (!cetta_text_path_parent_dir(parent, sizeof(parent), base_dir))
            break;
        if (strcmp(parent, base_dir) == 0)
            break;
        snprintf(base_dir, sizeof(base_dir), "%s", parent);
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
        if (!module_provider_visible(ctx, plan->provider_kind)) {
            *error_out = atom_symbol(eval_arena, "module provider disabled");
            return false;
        }
        char reason[160];
        if (!resolve_relative_module_candidate_for_language(
                ctx, spec->path_or_member, plan->canonical_path,
                sizeof(plan->canonical_path), &plan->format,
                reason, sizeof(reason))) {
            *error_out = atom_symbol(eval_arena,
                                     reason[0] ? reason : "module file not found");
            return false;
        }
        return true;
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
        char reason[160];
        if (!resolve_relative_module_candidate_for_language(
                ctx, spec->path_or_member, plan->canonical_path,
                sizeof(plan->canonical_path), &plan->format,
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

static bool library_starts_with(const char *s, const char *prefix) {
    if (!s || !prefix) return false;
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static bool library_mork_suffix_needs_bang(const char *suffix) {
    if (!suffix) return false;
    if (strcmp(suffix, "space_include") == 0 ||
        strcmp(suffix, "space_step") == 0 ||
        strcmp(suffix, "space_add_atom") == 0 ||
        strcmp(suffix, "space_remove_atom") == 0 ||
        strcmp(suffix, "space_dump_act") == 0 ||
        strcmp(suffix, "space_import_act") == 0) {
        return true;
    }
    if (strstr(suffix, "zipper_close") || strstr(suffix, "zipper_reset") ||
        strstr(suffix, "zipper_ascend") || strstr(suffix, "zipper_descend") ||
        strstr(suffix, "zipper_next_") || strstr(suffix, "zipper_prev_")) {
        return true;
    }
    return false;
}

static bool library_mork_public_name(const char *head_name, char *out, size_t out_sz) {
    const char *suffix = NULL;
    const char *base = NULL;
    size_t n = 0;
    bool bang = false;

    if (!head_name || !out || out_sz == 0) return false;
    if (!library_starts_with(head_name, "__cetta_lib_mork_")) return false;
    suffix = head_name + strlen("__cetta_lib_mork_");
    if (!suffix[0]) return false;

    if (strcmp(suffix, "space_new") == 0) {
        base = "new-space";
    } else if (strcmp(suffix, "space_include") == 0) {
        base = "include";
    } else if (strcmp(suffix, "space_open_act") == 0) {
        base = "open-act";
    } else if (strcmp(suffix, "space_dump_act") == 0) {
        base = "dump";
    } else if (strcmp(suffix, "space_import_act") == 0) {
        base = "load-act";
    } else if (strcmp(suffix, "space_step") == 0) {
        base = "step";
    } else if (strcmp(suffix, "space_add_atom") == 0) {
        base = "add-atom";
    } else if (strcmp(suffix, "space_remove_atom") == 0) {
        base = "remove-atom";
    } else if (strcmp(suffix, "space_atoms") == 0) {
        base = "get-atoms";
    } else if (strcmp(suffix, "space_count_atoms") == 0) {
        base = "size";
    } else if (strcmp(suffix, "space_match") == 0) {
        base = "match";
    } else if (strcmp(suffix, "restrict") == 0) {
        base = "prefix-restrict";
    }

    if (snprintf(out, out_sz, "mork:") >= (int)out_sz) return false;
    n = strlen(out);
    if (base) {
        if (snprintf(out + n, out_sz - n, "%s", base) >= (int)(out_sz - n)) {
            return false;
        }
    } else {
        size_t i = 0;
        const char *name = suffix;
        if (library_starts_with(name, "space_")) name += strlen("space_");
        while (name[i] != '\0' && n + 1 < out_sz) {
            out[n++] = (name[i] == '_') ? '-' : name[i];
            i++;
        }
        if (name[i] != '\0') return false;
        out[n] = '\0';
    }

    bang = library_mork_suffix_needs_bang(suffix);
    if (bang) {
        n = strlen(out);
        if (n + 1 >= out_sz) return false;
        out[n] = '!';
        out[n + 1] = '\0';
    }
    return true;
}

static Atom *library_call_expr(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    Atom **elems = arena_alloc(a, sizeof(Atom *) * (nargs + 1));
    elems[0] = head;
    if (head && head->kind == ATOM_SYMBOL) {
        const char *head_name = atom_name_cstr(head);
        char mork_name[192];
        if (library_mork_public_name(head_name, mork_name, sizeof(mork_name))) {
            elems[0] = atom_symbol(a, mork_name);
        }
    }
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
    if (arg->kind == ATOM_SYMBOL) return atom_name_cstr(arg);
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

static Atom *library_atoms_from_text_impl(Arena *a, const uint8_t *bytes, size_t len,
                                          Atom *call, bool quote_atoms) {
    if (!bytes || len == 0) {
        return atom_expr(a, NULL, 0);
    }
    char *text = cetta_malloc(len + 1);
    memcpy(text, bytes, len);
    text[len] = '\0';

    Atom **atoms = NULL;
    uint32_t natoms = 0;
    uint32_t cap = 0;
    size_t pos = 0;
    while (text[pos]) {
        Atom *atom = parse_sexpr(a, text, &pos);
        if (!atom) break;
        if (quote_atoms) {
            atom = atom_expr(a, (Atom *[]){
                atom_symbol_id(a, g_builtin_syms.quote),
                atom
            }, 2);
        }
        if (natoms >= cap) {
            cap = cap ? cap * 2 : 8;
            atoms = cetta_realloc(atoms, sizeof(Atom *) * cap);
        }
        atoms[natoms++] = atom;
    }
    free(text);

    if (natoms == 0) {
        free(atoms);
        return atom_expr(a, NULL, 0);
    }
    if (atoms == NULL) {
        return atom_error(a, call, atom_string(a, "MM2 dump parse failed"));
    }

    Atom **out = arena_alloc(a, sizeof(Atom *) * natoms);
    memcpy(out, atoms, sizeof(Atom *) * natoms);
    free(atoms);
    return atom_expr(a, out, natoms);
}

static Atom *library_atoms_from_text(Arena *a, const uint8_t *bytes, size_t len,
                                     Atom *call) {
    return library_atoms_from_text_impl(a, bytes, len, call, true);
}

static char *library_mm2_surface_text(Arena *a, Atom *atom) {
    if (atom && atom->kind == ATOM_EXPR && atom->expr.len == 2 &&
        atom->expr.elems[0]->kind == ATOM_SYMBOL &&
        atom_is_symbol_id(atom->expr.elems[0], g_builtin_syms.quote)) {
        atom = atom->expr.elems[1];
    }
    return cetta_mm2_atom_to_surface_string(a, atom);
}

static Atom *library_unquote_atom(Atom *atom) {
    if (atom && atom->kind == ATOM_EXPR && atom->expr.len == 2 &&
        atom->expr.elems[0]->kind == ATOM_SYMBOL &&
        atom_is_symbol_id(atom->expr.elems[0], g_builtin_syms.quote)) {
        return atom->expr.elems[1];
    }
    return atom;
}

static Atom *library_quote_atom(Arena *a, Atom *atom) {
    return atom_expr(a, (Atom *[]){
        atom_symbol_id(a, g_builtin_syms.quote),
        atom
    }, 2);
}

static bool library_resolve_current_path(CettaLibraryContext *ctx, const char *path,
                                         char *resolved, size_t resolved_sz) {
    return cetta_text_path_resolve(cetta_library_relative_base_dir(ctx), path,
                                   resolved, resolved_sz);
}

static CettaMorkProgramHandle *library_mm2_program_handle(CettaLibraryContext *ctx,
                                                          Atom *arg) {
    uint64_t id = 0;
    if (!ctx || !cetta_native_handle_arg(arg, CETTA_MM2_PROGRAM_HANDLE_KIND, &id))
        return NULL;
    return (CettaMorkProgramHandle *)cetta_native_handle_get(
        ctx, CETTA_MM2_PROGRAM_HANDLE_KIND, id);
}

static CettaMorkContextHandle *library_mm2_context_handle(CettaLibraryContext *ctx,
                                                          Atom *arg) {
    uint64_t id = 0;
    if (!ctx || !cetta_native_handle_arg(arg, CETTA_MM2_CONTEXT_HANDLE_KIND, &id))
        return NULL;
    return (CettaMorkContextHandle *)cetta_native_handle_get(
        ctx, CETTA_MM2_CONTEXT_HANDLE_KIND, id);
}

static CettaMorkCursorResource *library_mork_cursor_handle(CettaLibraryContext *ctx,
                                                           Atom *arg) {
    uint64_t id = 0;
    if (!ctx || !cetta_native_handle_arg(arg, CETTA_MORK_CURSOR_HANDLE_KIND, &id))
        return NULL;
    return (CettaMorkCursorResource *)cetta_native_handle_get(
        ctx, CETTA_MORK_CURSOR_HANDLE_KIND, id);
}

static CettaMorkProductCursorResource *library_mork_product_cursor_handle(
    CettaLibraryContext *ctx, Atom *arg) {
    uint64_t id = 0;
    if (!ctx ||
        !cetta_native_handle_arg(arg, CETTA_MORK_PRODUCT_CURSOR_HANDLE_KIND, &id))
        return NULL;
    return (CettaMorkProductCursorResource *)cetta_native_handle_get(
        ctx, CETTA_MORK_PRODUCT_CURSOR_HANDLE_KIND, id);
}

static CettaMorkOverlayCursorResource *library_mork_overlay_cursor_handle(
    CettaLibraryContext *ctx, Atom *arg) {
    uint64_t id = 0;
    if (!ctx ||
        !cetta_native_handle_arg(arg, CETTA_MORK_OVERLAY_CURSOR_HANDLE_KIND, &id))
        return NULL;
    return (CettaMorkOverlayCursorResource *)cetta_native_handle_get(
        ctx, CETTA_MORK_OVERLAY_CURSOR_HANDLE_KIND, id);
}

static Atom *library_byte_list_atom(Arena *a, const uint8_t *bytes, size_t len) {
    Atom **elems = NULL;
    if (len > 0) {
        elems = arena_alloc(a, sizeof(Atom *) * len);
        for (size_t i = 0; i < len; i++) {
            elems[i] = atom_int(a, (int64_t)bytes[i]);
        }
    }
    return atom_expr(a, elems, (uint32_t)len);
}

static Atom *library_u64_be_list_atom(Arena *a, const uint8_t *bytes, size_t len) {
    Atom **elems = NULL;
    size_t count = 0;
    if ((len % 8u) != 0u)
        return atom_expr(a, NULL, 0);
    count = len / 8u;
    if (count > 0) {
        elems = arena_alloc(a, sizeof(Atom *) * count);
        for (size_t i = 0; i < count; i++) {
            const uint8_t *p = bytes + (i * 8u);
            uint64_t value = ((uint64_t)p[0] << 56) |
                             ((uint64_t)p[1] << 48) |
                             ((uint64_t)p[2] << 40) |
                             ((uint64_t)p[3] << 32) |
                             ((uint64_t)p[4] << 24) |
                             ((uint64_t)p[5] << 16) |
                             ((uint64_t)p[6] << 8) |
                             (uint64_t)p[7];
            elems[i] = atom_int(a, (int64_t)value);
        }
    }
    return atom_expr(a, elems, (uint32_t)count);
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
    SymbolId head_id = head->sym_id;
    if (head_id == g_builtin_syms.lib_system_args) {
        return system_cli_args(ctx, a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_system_arg) {
        return system_cli_arg(ctx, a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_system_arg_count) {
        return system_cli_arg_count(ctx, a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_system_has_args) {
        return system_has_cli_args(ctx, a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_system_getenv_or_default) {
        return system_getenv_or_default(a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_system_is_flag_arg) {
        return system_is_flag_arg(a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_system_exit_with_code) {
        return system_exit_with_code(a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_system_cwd) {
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
    SymbolId head_id = head->sym_id;
    if (head_id == g_builtin_syms.lib_fs_exists) {
        return fs_exists(a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_fs_read_text) {
        return fs_read_text(a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_fs_write_text) {
        return fs_write_text(a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_fs_append_text) {
        return fs_append_text(a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_fs_read_lines) {
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
    SymbolId head_id = head->sym_id;
    if (head_id == g_builtin_syms.lib_str_length) {
        return str_length(a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_str_concat) {
        return str_concat(a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_str_split) {
        return str_split(a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_str_split_whitespace) {
        return str_split_whitespace(a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_str_join) {
        return str_join(a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_str_slice) {
        return str_slice(a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_str_find) {
        return str_find(a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_str_starts_with) {
        return str_starts_with(a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_str_ends_with) {
        return str_ends_with(a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_str_trim) {
        return str_trim(a, head, args, nargs);
    }
    return NULL;
}

static CettaMorkSpaceResource *library_mork_space_resource(CettaLibraryContext *ctx,
                                                           Atom *atom) {
    uint64_t id = 0;
    if (!ctx || !atom ||
        !cetta_native_handle_arg(atom, CETTA_MORK_SPACE_HANDLE_KIND, &id)) {
        return NULL;
    }
    return (CettaMorkSpaceResource *)cetta_native_handle_get(
        ctx, CETTA_MORK_SPACE_HANDLE_KIND, id);
}

static Atom *library_mork_space_handle_atom(CettaLibraryContext *ctx,
                                            Arena *a,
                                            Atom *head,
                                            Atom **args,
                                            uint32_t nargs,
                                            CettaMorkSpaceHandle *bridge_space,
                                            SpaceKind kind,
                                            const char *fallback_error) {
    CettaMorkSpaceResource *resource;
    uint64_t id = 0;

    if (!ctx || !bridge_space) {
        return atom_error(a, library_call_expr(a, head, args, nargs),
                          atom_string(a, fallback_error));
    }
    resource = cetta_malloc(sizeof(CettaMorkSpaceResource));
    resource->bridge_space = bridge_space;
    resource->kind = kind;
    if (!cetta_native_handle_alloc(ctx, CETTA_MORK_SPACE_HANDLE_KIND, resource,
                                   library_mork_space_free_resource, &id)) {
        cetta_mork_bridge_space_free(bridge_space);
        free(resource);
        return atom_error(a, library_call_expr(a, head, args, nargs),
                          atom_string(a, fallback_error));
    }
    return cetta_native_handle_atom(a, CETTA_MORK_SPACE_HANDLE_KIND, id);
}

static Atom *library_mork_bridge_error(Arena *a, Atom *head, Atom **args,
                                       uint32_t nargs, const char *prefix) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%s%s", prefix, cetta_mork_bridge_last_error());
    return atom_error(a, library_call_expr(a, head, args, nargs), atom_string(a, buf));
}

static CettaMorkSpaceResource *library_explicit_mork_space_arg(CettaLibraryContext *ctx,
                                                               Atom *atom) {
    CettaMorkSpaceResource *resource = library_mork_space_resource(ctx, atom);
    if (!resource || !resource->bridge_space) {
        return NULL;
    }
    return resource;
}

static bool library_mork_space_bridge(CettaMorkSpaceResource *resource,
                                      CettaMorkSpaceHandle **out_bridge) {
    if (out_bridge)
        *out_bridge = NULL;
    if (!resource || !resource->bridge_space)
        return false;
    if (out_bridge)
        *out_bridge = resource->bridge_space;
    return true;
}

static bool library_mork_expr_bytes_need_text_fallback(const char *encode_error) {
    return encode_error &&
           strstr(encode_error, "MORK bridge tokens must be at most 63 bytes") != NULL;
}

static bool library_mork_space_add_atom_text_fallback(Arena *a,
                                                      CettaMorkSpaceHandle *bridge,
                                                      Atom *item) {
    char *text;
    if (!a || !bridge || !item)
        return false;
    text = atom_to_string(a, item);
    if (!text)
        return false;
    return cetta_mork_bridge_space_add_sexpr(
        bridge, (const uint8_t *)text, strlen(text), NULL);
}

static bool library_mork_space_remove_atom_text_fallback(Arena *a,
                                                         CettaMorkSpaceHandle *bridge,
                                                         Atom *item) {
    char *text;
    if (!a || !bridge || !item)
        return false;
    text = atom_to_string(a, item);
    if (!text)
        return false;
    return cetta_mork_bridge_space_remove_sexpr(
        bridge, (const uint8_t *)text, strlen(text), NULL);
}

static bool library_mork_space_add_atoms_text_fallback(Arena *a,
                                                       CettaMorkSpaceHandle *bridge,
                                                       Atom **items,
                                                       uint32_t len) {
    char **texts;
    char *joined;
    size_t total = 0;
    size_t off = 0;

    if (!a || !bridge || (!items && len != 0))
        return false;
    texts = arena_alloc(a, sizeof(char *) * (len ? len : 1u));
    if (!texts)
        return false;
    for (uint32_t i = 0; i < len; i++) {
        texts[i] = atom_to_string(a, items[i]);
        if (!texts[i])
            return false;
        total += strlen(texts[i]) + 1u;
    }
    joined = arena_alloc(a, total ? total : 1u);
    if (!joined)
        return false;
    for (uint32_t i = 0; i < len; i++) {
        size_t n = strlen(texts[i]);
        memcpy(joined + off, texts[i], n);
        off += n;
        joined[off++] = '\n';
    }
    if (total == 0)
        joined[0] = '\0';
    return cetta_mork_bridge_space_add_sexpr(
        bridge, (const uint8_t *)joined, off, NULL);
}

bool cetta_library_lookup_explicit_mork_bridge(CettaLibraryContext *ctx,
                                               Atom *space_arg,
                                               CettaMorkSpaceHandle **bridge_out) {
    return library_mork_space_bridge(library_explicit_mork_space_arg(ctx, space_arg),
                                     bridge_out);
}

static __attribute__((unused)) bool library_mork_materialize_temp_space(CettaLibraryContext *ctx,
                                                                        Arena *a,
                                                                        CettaMorkSpaceResource *resource,
                                                                        SpaceEngine engine,
                                                                        const char *debug_path,
                                                                        Space *out_space,
                                                                        Atom **error_out) {
    uint8_t *packet = NULL;
    size_t packet_len = 0;
    uint32_t packet_rows = 0;
    Arena *persistent_arena;
    bool ok = false;

    if (error_out)
        *error_out = NULL;
    if (!ctx || !resource || !resource->bridge_space || !out_space)
        return false;
    if (!cetta_mork_bridge_space_dump(resource->bridge_space, &packet, &packet_len,
                                      &packet_rows)) {
        return false;
    }

    persistent_arena = eval_current_persistent_arena();
    if (!persistent_arena)
        persistent_arena = a;

    space_init_with_universe(out_space, cetta_library_space_universe(ctx, persistent_arena));
    out_space->kind = resource->kind;
    if (!space_match_backend_try_set(out_space, engine)) {
        if (error_out) {
            *error_out = atom_error(a, atom_symbol(a, "mork:materialize"),
                                    atom_string(a, "MORK temp materialization backend setup failed"));
        }
        goto cleanup;
    }
    if (!load_act_dump_text_into_space(ctx, debug_path, packet, packet_len, out_space,
                                       persistent_arena, a, error_out)) {
        goto cleanup;
    }
    ok = true;

cleanup:
    cetta_mork_bridge_bytes_free(packet, packet_len);
    (void)packet_rows;
    if (!ok)
        space_free(out_space);
    return ok;
}

static bool load_module_act_file(CettaLibraryContext *ctx, const char *path,
                                 Space *work_space,
                                 Arena *eval_arena,
                                 Arena *persistent_arena,
                                 Atom **error_out);

static bool load_module_act_file_pathmap_materialized(CettaLibraryContext *ctx,
                                                      const char *path,
                                                      Space *target_space,
                                                      Arena *eval_arena,
                                                      Arena *persistent_arena,
                                                      Atom **error_out);

static Atom *module_reason_with_detail(CettaLibraryContext *ctx, Arena *a,
                                       const char *tag,
                                       const char *path, Atom *detail);

static bool load_module_mm2_file_into_mork_bridge(CettaLibraryContext *ctx,
                                                  const char *path,
                                                  CettaMorkSpaceHandle *bridge,
                                                  Arena *eval_arena,
                                                  Arena *persistent_arena,
                                                  Atom **error_out);

static __attribute__((unused)) bool library_mork_build_space_bridge_snapshot(
    Space *space,
    CettaMorkSpaceHandle **out_bridge,
    uint64_t *out_unique_count
) {
    Arena scratch;
    arena_init(&scratch);
    arena_set_runtime_kind(&scratch, CETTA_ARENA_RUNTIME_KIND_SCRATCH);

    *out_bridge = NULL;
    if (out_unique_count)
        *out_unique_count = 0;

    if (!space_match_backend_materialize_attached(
            space, eval_current_persistent_arena())) {
        goto fail;
    }

    if (!cetta_mork_bridge_is_available())
        goto fail;

    CettaMorkSpaceHandle *bridge = cetta_mork_bridge_space_new();
    if (!bridge)
        goto fail;

    uint32_t n = space_length(space);
    if (space->native.universe) {
        AtomId *atom_ids = arena_alloc(&scratch, sizeof(AtomId) * (n ? n : 1u));
        uint8_t *packet = NULL;
        size_t packet_len = 0;
        const char *pack_error = NULL;
        for (uint32_t i = 0; i < n; i++)
            atom_ids[i] = space_get_atom_id_at(space, i);
        if (cetta_library_pack_mork_expr_batch_from_ids(
                &scratch, space->native.universe, atom_ids, n,
                &packet, &packet_len, &pack_error) &&
            cetta_mork_bridge_space_add_expr_bytes_batch(
                bridge, packet, packet_len, NULL)) {
            free(packet);
        } else {
            free(packet);
            cetta_mork_bridge_space_free(bridge);
            goto fail;
        }
    } else {
        for (uint32_t i = 0; i < n; i++) {
            ArenaMark mark = arena_mark(&scratch);
            uint8_t *expr_bytes = NULL;
            size_t expr_len = 0;
            const char *encode_error = NULL;
            bool ok = cetta_mm2_atom_to_bridge_expr_bytes(
                &scratch, space_get_at(space, i), &expr_bytes, &expr_len,
                &encode_error) &&
                cetta_mork_bridge_space_add_expr_bytes(
                    bridge, expr_bytes, expr_len, NULL);
            free(expr_bytes);
            arena_reset(&scratch, mark);
            if (!ok) {
                (void)encode_error;
                cetta_mork_bridge_space_free(bridge);
                goto fail;
            }
        }
    }

    if (!cetta_mork_bridge_space_unique_size(bridge, out_unique_count)) {
        cetta_mork_bridge_space_free(bridge);
        goto fail;
    }

    arena_free(&scratch);
    *out_bridge = bridge;
    return true;

fail:
    arena_free(&scratch);
    return false;
}

static Atom *mork_space_new_native(CettaLibraryContext *ctx, Arena *a,
                                   Atom *head, Atom **args, uint32_t nargs) {
    const char *kind_name = NULL;
    SpaceKind kind = SPACE_KIND_ATOM;
    CettaMorkSpaceHandle *bridge_space = NULL;

    if (nargs == 1) {
        kind_name = library_text_arg(args[0]);
    } else if (nargs != 0) {
        return library_signature_error(a, head, args, nargs,
                                       "expected optional unordered discipline");
    }
    if (kind_name && !space_kind_from_name(kind_name, &kind)) {
        return atom_error(a, library_call_expr(a, head, args, nargs),
                          atom_string(a, "mork:new-space expects atom or hash discipline"));
    }
    if (kind == SPACE_KIND_STACK || kind == SPACE_KIND_QUEUE) {
        return atom_error(a, library_call_expr(a, head, args, nargs),
                          atom_string(a, "mork:new-space currently supports only unordered disciplines"));
    }

    bridge_space = cetta_mork_bridge_space_new();
    if (!bridge_space) {
        return library_mork_bridge_error(a, head, args, nargs,
                                         "MORK space allocation failed: ");
    }
    return library_mork_space_handle_atom(ctx, a, head, args, nargs,
                                          bridge_space, kind,
                                          "MorkSpace handle allocation failed");
}

static Atom *mork_space_open_act_native(CettaLibraryContext *ctx, Arena *a,
                                        Atom *head, Atom **args, uint32_t nargs) {
    const char *path = NULL;
    const char *kind_name = NULL;
    char resolved[PATH_MAX];
    SpaceKind kind = SPACE_KIND_HASH;
    CettaMorkSpaceHandle *bridge_space = NULL;
    uint64_t loaded = 0;

    if (nargs == 1) {
        path = library_text_arg(args[0]);
    } else if (nargs == 2) {
        kind_name = library_text_arg(args[0]);
        path = library_text_arg(args[1]);
    } else {
        return library_signature_error(a, head, args, nargs,
                                       "expected ACT filename or discipline plus ACT filename");
    }
    if (!path) {
        return library_signature_error(a, head, args, nargs,
                                       "expected ACT filename");
    }
    if (kind_name && !space_kind_from_name(kind_name, &kind)) {
        return atom_error(a, library_call_expr(a, head, args, nargs),
                          atom_string(a, "mork-space-open-act expects atom or hash discipline"));
    }
    if (kind == SPACE_KIND_STACK || kind == SPACE_KIND_QUEUE) {
        return atom_error(a, library_call_expr(a, head, args, nargs),
                          atom_string(a, "mork-space-open-act currently supports only unordered disciplines"));
    }
    if (!library_resolve_current_path(ctx, path, resolved, sizeof(resolved))) {
        return atom_error(a, library_call_expr(a, head, args, nargs),
                          atom_string(a, "MORK ACT open path resolution failed"));
    }
    bridge_space = cetta_mork_bridge_space_new();
    if (!bridge_space) {
        return library_mork_bridge_error(a, head, args, nargs,
                                         "MORK ACT open allocation failed: ");
    }
    if (!cetta_mork_bridge_space_load_act_file(
            bridge_space, (const uint8_t *)resolved, strlen(resolved), &loaded)) {
        cetta_mork_bridge_space_free(bridge_space);
        return library_mork_bridge_error(a, head, args, nargs,
                                         "MORK ACT open failed: ");
    }
    (void)loaded;
    return library_mork_space_handle_atom(ctx, a, head, args, nargs,
                                          bridge_space, kind,
                                          "MORK ACT open handle allocation failed");
}

static Atom *mork_space_dump_act_native(CettaLibraryContext *ctx, Arena *a,
                                        Atom *head, Atom **args, uint32_t nargs) {
    CettaMorkSpaceResource *space;
    const char *path;
    char resolved[PATH_MAX];
    CettaMorkSpaceHandle *bridge = NULL;
    uint64_t saved = 0;

    if (nargs != 2 || !(path = library_text_arg(args[1]))) {
        return library_signature_error(a, head, args, nargs,
                                       "expected space and ACT filename");
    }
    space = library_explicit_mork_space_arg(ctx, args[0]);
    if (!space) {
        return library_signature_error(a, head, args, nargs,
                                       "expected MorkSpace as first argument");
    }
    if (!library_resolve_current_path(ctx, path, resolved, sizeof(resolved))) {
        return atom_error(a, library_call_expr(a, head, args, nargs),
                          atom_string(a, "MORK ACT dump path resolution failed"));
    }
    if (!library_mork_space_bridge(space, &bridge) || !bridge) {
        return library_mork_bridge_error(a, head, args, nargs,
                                         "MORK ACT dump bridge unavailable: ");
    }

    if (!cetta_mork_bridge_space_dump_act_file(
            bridge, (const uint8_t *)resolved, strlen(resolved), &saved)) {
        return library_mork_bridge_error(a, head, args, nargs,
                                         "MORK ACT dump failed: ");
    }
    (void)saved;
    return atom_unit(a);
}

static Atom *mork_space_import_act_native(CettaLibraryContext *ctx, Arena *a,
                                          Atom *head, Atom **args, uint32_t nargs) {
    CettaMorkSpaceResource *space;
    const char *path;
    char resolved[PATH_MAX];
    CettaMorkSpaceHandle *bridge = NULL;

    if (nargs != 2 || !(path = library_text_arg(args[1]))) {
        return library_signature_error(a, head, args, nargs,
                                       "expected space and ACT filename");
    }
    space = library_explicit_mork_space_arg(ctx, args[0]);
    if (!space) {
        return library_signature_error(a, head, args, nargs,
                                       "expected MorkSpace as first argument");
    }
    if (!library_resolve_current_path(ctx, path, resolved, sizeof(resolved))) {
        return atom_error(a, library_call_expr(a, head, args, nargs),
                          atom_string(a, "MORK ACT import path resolution failed"));
    }
    if (!library_mork_space_bridge(space, &bridge) || !bridge) {
        return library_mork_bridge_error(a, head, args, nargs,
                                         "MORK ACT import bridge unavailable: ");
    }
    if (!cetta_mork_bridge_space_load_act_file(
            bridge, (const uint8_t *)resolved, strlen(resolved), NULL)) {
        return library_mork_bridge_error(a, head, args, nargs,
                                         "MORK ACT import failed: ");
    }
    return atom_unit(a);
}

static Atom *mork_space_algebra_native(CettaLibraryContext *ctx, Arena *a,
                                       Atom *head, Atom **args, uint32_t nargs,
                                       SymbolId which) {
    CettaMorkSpaceResource *lhs;
    CettaMorkSpaceResource *rhs;
    CettaMorkSpaceHandle *lhs_bridge = NULL;
    CettaMorkSpaceHandle *rhs_bridge = NULL;
    const char *missing = "expected two MorkSpace handles";
    const char *bridge_prefix = "MORK algebra failed: ";
    bool ok = false;

    (void)ctx;

    if (nargs != 2) {
        return library_signature_error(a, head, args, nargs, missing);
    }
    lhs = library_explicit_mork_space_arg(ctx, args[0]);
    rhs = library_explicit_mork_space_arg(ctx, args[1]);
    if (!lhs || !rhs) {
        return library_signature_error(a, head, args, nargs, missing);
    }
    lhs_bridge = lhs->bridge_space;
    rhs_bridge = rhs->bridge_space;

    if (which == g_builtin_syms.lib_mork_join) {
        bridge_prefix = "MORK join failed: ";
        ok = cetta_mork_bridge_space_join_into(lhs_bridge, rhs_bridge);
    } else if (which == g_builtin_syms.lib_mork_meet) {
        bridge_prefix = "MORK meet failed: ";
        ok = cetta_mork_bridge_space_meet_into(lhs_bridge, rhs_bridge);
    } else if (which == g_builtin_syms.lib_mork_subtract) {
        bridge_prefix = "MORK subtract failed: ";
        ok = cetta_mork_bridge_space_subtract_into(lhs_bridge, rhs_bridge);
    } else {
        bridge_prefix = "MORK restrict failed: ";
        ok = cetta_mork_bridge_space_restrict_into(lhs_bridge, rhs_bridge);
    }
    if (!ok) {
        return library_mork_bridge_error(a, head, args, nargs, bridge_prefix);
    }
    return atom_unit(a);
}

static Atom *mork_space_clone_native(CettaLibraryContext *ctx, Arena *a,
                                     Atom *head, Atom **args, uint32_t nargs) {
    CettaMorkSpaceResource *space;
    CettaMorkSpaceHandle *bridge = NULL;
    CettaMorkSpaceHandle *clone = NULL;

    if (nargs != 1) {
        return library_signature_error(a, head, args, nargs,
                                       "expected MorkSpace");
    }
    space = library_explicit_mork_space_arg(ctx, args[0]);
    if (!space) {
        return library_signature_error(a, head, args, nargs,
                                       "expected MorkSpace");
    }
    if (!library_mork_space_bridge(space, &bridge) || !bridge) {
        return library_mork_bridge_error(a, head, args, nargs,
                                         "MORK clone bridge unavailable: ");
    }
    clone = cetta_mork_bridge_space_clone(bridge);
    if (!clone) {
        return library_mork_bridge_error(a, head, args, nargs,
                                         "MORK clone failed: ");
    }
    return library_mork_space_handle_atom(ctx, a, head, args, nargs, clone,
                                          space->kind,
                                          "MORK clone handle allocation failed");
}

static Atom *mork_space_surface_native(CettaLibraryContext *ctx,
                                       Arena *a, Atom *head, Atom **args,
                                       uint32_t nargs, SymbolId which) {
    CettaMorkSpaceResource *target = NULL;
    CettaMorkSpaceHandle *bridge = NULL;

    if (which == g_builtin_syms.lib_mork_space_match) {
        Atom **results = NULL;
        uint32_t len = 0;
        uint32_t cap = 0;
        uint64_t logical_size = 0;

        if (nargs != 3) {
            return library_signature_error(a, head, args, nargs,
                                           "expected MorkSpace, pattern, and template");
        }
        target = library_explicit_mork_space_arg(ctx, args[0]);
        if (!target) {
            return library_signature_error(a, head, args, nargs,
                                           "expected MorkSpace as first argument");
        }

        Atom *pattern = args[1];
        Atom *template = args[2];
        BindingSet matches;
        binding_set_init(&matches);
        if (!library_mork_space_bridge(target, &bridge) || !bridge) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK match bridge unavailable: ");
        }
        if (!cetta_mork_bridge_space_unique_size(bridge, &logical_size) ||
            logical_size > UINT32_MAX) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK match size query failed: ");
        }
        bool direct_ok = false;
        if (pattern->kind == ATOM_EXPR && pattern->expr.len >= 3 &&
            atom_is_symbol_id(pattern->expr.elems[0], g_builtin_syms.comma)) {
            direct_ok = space_match_backend_mork_query_conjunction_direct(
                    bridge, a, pattern->expr.elems + 1,
                    pattern->expr.len - 1, NULL, &matches);
        } else {
            direct_ok = space_match_backend_mork_query_bindings_direct(
                bridge, a, pattern, &matches);
        }

        if (!direct_ok) {
            binding_set_free(&matches);
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK direct match failed: ");
        }
        for (uint32_t i = 0; i < matches.len; i++) {
            if (len >= cap) {
                cap = cap ? cap * 2 : 8;
                results = cetta_realloc(results, sizeof(Atom *) * cap);
            }
            results[len++] = library_quote_atom(
                a, bindings_apply(&matches.items[i], a, template));
        }
        binding_set_free(&matches);

        Atom *result = atom_expr(a, NULL, 0);
        if (len > 0) {
            Atom **elems = arena_alloc(a, sizeof(Atom *) * len);
            memcpy(elems, results, sizeof(Atom *) * len);
            result = atom_expr(a, elems, len);
        }
        free(results);
        return library_quote_atom(a, result);
    }

    if (which == g_builtin_syms.lib_mork_space_step) {
        uint64_t performed = 0;
        uint64_t limit = 1;

        if (nargs != 1 && nargs != 2) {
            return library_signature_error(a, head, args, nargs,
                                           "expected MorkSpace and optional non-negative step count");
        }
        target = library_explicit_mork_space_arg(ctx, args[0]);
        if (!target) {
            return library_signature_error(a, head, args, nargs,
                                           "expected MorkSpace as first argument");
        }
        if (nargs == 2) {
            int steps = 0;
            if (!library_int_arg(args[1], &steps) || steps < 0) {
                return library_signature_error(a, head, args, nargs,
                                               "expected MorkSpace and optional non-negative step count");
            }
            limit = (uint64_t)steps;
        }
        if (!library_mork_space_bridge(target, &bridge) || !bridge) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK step bridge unavailable: ");
        }
        if (!cetta_mork_bridge_space_step(bridge, limit, &performed)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK step failed: ");
        }
        return atom_int(a, (int64_t)performed);
    }

    if (nargs != 1 && nargs != 2) {
        return library_signature_error(a, head, args, nargs, "expected MorkSpace");
    }
    target = library_explicit_mork_space_arg(ctx, args[0]);
    if (!target) {
        return library_signature_error(a, head, args, nargs,
                                       nargs == 1
                                           ? "expected MorkSpace"
                                           : "expected MorkSpace as first argument");
    }

    if (which == g_builtin_syms.mork_add_atoms ||
        which == g_builtin_syms.lib_mork_space_add_atoms ||
        which == g_builtin_syms.lib_mork_space_add_stream) {
        Arena scratch;
        Atom *items;
        uint8_t *packet = NULL;
        size_t packet_len = 0;
        uint64_t packet_bytes = 0;
        uint64_t pack_ns = 0;
        uint64_t added = 0;
        const char *pack_error = NULL;
        const bool emit_stats = cetta_runtime_stats_is_enabled();
        const bool emit_timing = cetta_runtime_timing_is_enabled();
        uint64_t native_started_ns = 0;
        uint64_t ffi_started_ns = 0;

        if (nargs != 2) {
            return library_signature_error(a, head, args, nargs,
                                           "expected MorkSpace and expression of atoms");
        }
        items = args[1];
        if (items->kind != ATOM_EXPR) {
            return library_signature_error(a, head, args, nargs,
                                           "expected MorkSpace and expression of atoms");
        }
        if (!library_mork_space_bridge(target, &bridge) || !bridge) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK bridge unavailable: ");
        }
        arena_init(&scratch);
        arena_set_runtime_kind(&scratch, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
        if (emit_stats) {
            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_MORK_ADD_BATCH_CALL);
        }
        if (emit_timing) {
            native_started_ns = library_monotonic_ns();
        }
        if (!cetta_library_pack_mork_expr_batch(
                &scratch, items->expr.elems, items->expr.len,
                &packet, &packet_len, &packet_bytes,
                emit_timing ? &pack_ns : NULL, &pack_error)) {
            if (library_mork_space_bridge(target, &bridge) && bridge &&
                library_mork_expr_bytes_need_text_fallback(pack_error) &&
                library_mork_space_add_atoms_text_fallback(
                    &scratch, bridge, items->expr.elems, items->expr.len)) {
                free(packet);
                arena_free(&scratch);
                return atom_unit(a);
            }
            Atom *error = atom_error(
                a, library_call_expr(a, head, args, nargs),
                atom_string(a, pack_error ? pack_error
                                          : "MORK expr-byte lowering failed"));
            free(packet);
            arena_free(&scratch);
            return error;
        }
        if (emit_stats) {
            cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_MORK_ADD_BATCH_ITEMS,
                                    items->expr.len);
            cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_MORK_ADD_BATCH_PACKET_BYTES,
                                    packet_bytes);
        }
        if (emit_timing) {
            cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_MORK_ADD_BATCH_PACK_NS,
                                    pack_ns);
        }
        if (emit_timing) {
            ffi_started_ns = library_monotonic_ns();
        }
        bool ok = cetta_mork_bridge_space_add_expr_bytes_batch(
            bridge, packet, packet_len, &added);
        if (emit_timing) {
            uint64_t ffi_finished_ns = library_monotonic_ns();
            if (ffi_finished_ns >= ffi_started_ns) {
                cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_MORK_ADD_BATCH_FFI_NS,
                                        ffi_finished_ns - ffi_started_ns);
            }
            if (ffi_finished_ns >= native_started_ns) {
                cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_MORK_ADD_BATCH_NATIVE_NS,
                                        ffi_finished_ns - native_started_ns);
            }
        }
        free(packet);
        arena_free(&scratch);
        if (!ok) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK bulk add failed: ");
        }
        return atom_unit(a);
    }

    if (which == g_builtin_syms.lib_mork_space_add_atom ||
        which == g_builtin_syms.mork_add_atom) {
        Arena scratch;
        Atom *item;
        uint8_t *expr_bytes = NULL;
        size_t expr_len = 0;
        const char *encode_error = NULL;
        const bool emit_stats = cetta_runtime_stats_is_enabled();
        const bool emit_timing = cetta_runtime_timing_is_enabled();
        uint64_t started_ns = 0;
        uint64_t lowered_ns = 0;

        if (nargs != 2) {
            return library_signature_error(a, head, args, nargs,
                                           "expected MorkSpace and atom");
        }
        item = (which == g_builtin_syms.mork_add_atom)
                   ? args[1]
                   : library_unquote_atom(args[1]);
        if (emit_stats) {
            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_MORK_ADD_CALL);
        }
        if (emit_timing) {
            started_ns = library_monotonic_ns();
        }
        arena_init(&scratch);
        arena_set_runtime_kind(&scratch, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
        if (!cetta_mm2_atom_to_bridge_expr_bytes(
                &scratch, item, &expr_bytes, &expr_len, &encode_error)) {
            if (library_mork_space_bridge(target, &bridge) && bridge &&
                library_mork_expr_bytes_need_text_fallback(encode_error) &&
                library_mork_space_add_atom_text_fallback(&scratch, bridge, item)) {
                if (emit_timing && started_ns) {
                    lowered_ns = library_monotonic_ns();
                    if (lowered_ns >= started_ns) {
                        cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_MORK_ADD_LOWER_NS,
                                                lowered_ns - started_ns);
                    }
                }
                arena_free(&scratch);
                return atom_unit(a);
            }
            Atom *error = atom_error(
                a, library_call_expr(a, head, args, nargs),
                atom_string(a, encode_error ? encode_error
                                            : "MORK expr-byte lowering failed"));
            if (emit_timing && started_ns) {
                lowered_ns = library_monotonic_ns();
                if (lowered_ns >= started_ns) {
                    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_MORK_ADD_LOWER_NS,
                                            lowered_ns - started_ns);
                }
            }
            arena_free(&scratch);
            return error;
        }
        if (emit_timing) {
            lowered_ns = library_monotonic_ns();
            if (lowered_ns >= started_ns) {
                cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_MORK_ADD_LOWER_NS,
                                        lowered_ns - started_ns);
            }
        }
        if (emit_stats) {
            cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_MORK_ADD_EXPR_BYTES,
                                    (uint64_t)expr_len);
        }
        bool ok = library_mork_space_bridge(target, &bridge) &&
                  cetta_mork_bridge_space_add_expr_bytes(
                      bridge, expr_bytes, expr_len, NULL);
        if (emit_timing && lowered_ns) {
            uint64_t finished_ns = library_monotonic_ns();
            if (finished_ns >= lowered_ns) {
                cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_MORK_ADD_FFI_NS,
                                        finished_ns - lowered_ns);
            }
        }
        free(expr_bytes);
        arena_free(&scratch);
        if (!ok) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK add failed: ");
        }
        return atom_unit(a);
    }

    if (which == g_builtin_syms.lib_mork_space_remove_atom ||
        which == g_builtin_syms.mork_remove_atom) {
        Arena scratch;
        Atom *item;
        uint8_t *expr_bytes = NULL;
        size_t expr_len = 0;
        const char *encode_error = NULL;

        if (nargs != 2) {
            return library_signature_error(a, head, args, nargs,
                                           "expected MorkSpace and atom");
        }
        item = (which == g_builtin_syms.mork_remove_atom)
                   ? args[1]
                   : library_unquote_atom(args[1]);
        arena_init(&scratch);
        arena_set_runtime_kind(&scratch, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
        if (!cetta_mm2_atom_to_bridge_expr_bytes(
                &scratch, item, &expr_bytes, &expr_len, &encode_error)) {
            if (library_mork_space_bridge(target, &bridge) && bridge &&
                library_mork_expr_bytes_need_text_fallback(encode_error) &&
                library_mork_space_remove_atom_text_fallback(&scratch, bridge, item)) {
                arena_free(&scratch);
                return atom_unit(a);
            }
            Atom *error = atom_error(
                a, library_call_expr(a, head, args, nargs),
                atom_string(a, encode_error ? encode_error
                                            : "MORK expr-byte lowering failed"));
            arena_free(&scratch);
            return error;
        }
        bool ok = library_mork_space_bridge(target, &bridge) &&
                  cetta_mork_bridge_space_remove_expr_bytes(
                      bridge, expr_bytes, expr_len, NULL);
        free(expr_bytes);
        arena_free(&scratch);
        if (!ok) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK remove failed: ");
        }
        return atom_unit(a);
    }

    if (which == g_builtin_syms.lib_mork_space_atoms) {
        uint8_t *packet = NULL;
        size_t len = 0;
        uint32_t rows = 0;

        /* Intentional textual inspection/export surface: callers here asked to
           see the bridge dump as atoms, not to reuse the structural import
           seam that PATHMAP/MORK materialization uses internally. */
        if (nargs != 1) {
            return library_signature_error(a, head, args, nargs,
                                           "expected MorkSpace");
        }
        if (!library_mork_space_bridge(target, &bridge) || !bridge) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK get-atoms bridge unavailable: ");
        }
        if (!cetta_mork_bridge_space_dump(bridge, &packet, &len, &rows)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK get-atoms failed: ");
        }
        Atom *result = library_atoms_from_text(
            a, packet, len, library_call_expr(a, head, args, nargs));
        cetta_mork_bridge_bytes_free(packet, len);
        (void)rows;
        return library_quote_atom(a, result);
    }

    if (which == g_builtin_syms.lib_mork_space_size ||
        which == g_builtin_syms.lib_mork_space_count_atoms) {
        uint64_t size = 0;

        if (nargs != 1) {
            return library_signature_error(a, head, args, nargs,
                                           "expected MorkSpace");
        }
        if (!library_mork_space_bridge(target, &bridge) || !bridge) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK size bridge unavailable: ");
        }
        if (!cetta_mork_bridge_space_unique_size(bridge, &size)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK size failed: ");
        }
        return atom_int(a, (int64_t)size);
    }

    return atom_error(a, library_call_expr(a, head, args, nargs),
                      atom_string(a, "unknown mork surface helper"));
}

static Atom *mork_space_include_native(CettaLibraryContext *ctx, Arena *a,
                                       Atom *head, Atom **args, uint32_t nargs) {
    CettaMorkSpaceResource *space;
    const char *spec;
    Atom *error = NULL;
    Arena *persistent = NULL;
    CettaMorkSpaceHandle *bridge = NULL;
    CettaModuleSpec parsed_spec;
    CettaImportPlan plan;
    bool ok = false;

    if (nargs != 2 || !(spec = library_text_arg(args[1]))) {
        return library_signature_error(a, head, args, nargs,
                                       "expected MorkSpace and module spec");
    }
    space = library_explicit_mork_space_arg(ctx, args[0]);
    if (!space) {
        return library_signature_error(a, head, args, nargs,
                                       "expected MorkSpace as first argument");
    }

    persistent = eval_current_persistent_arena();
    if (!persistent)
        persistent = a;

    if (cetta_library_lookup(spec) || cetta_native_module_lookup(spec)) {
        error = atom_string(a,
                            "mork:include! supports direct MM2 or compiled ACT modules, not builtin libraries");
        goto cleanup;
    }
    if (!parse_module_spec(spec, &parsed_spec, a, &error)) {
        goto cleanup;
    }
    if (!resolve_import_plan(ctx, &parsed_spec, NULL, NULL, false,
                             &plan, a, &error)) {
        goto cleanup;
    }
    if (!library_mork_space_bridge(space, &bridge) || !bridge) {
        error = atom_string(a, "MORK bridge unavailable");
        goto cleanup;
    }

    if (plan.format.kind == CETTA_MODULE_FORMAT_MORK_ACT) {
        if (!cetta_mork_bridge_space_load_act_file(
                bridge,
                (const uint8_t *)plan.canonical_path,
                strlen(plan.canonical_path),
                NULL)) {
            error = module_reason_with_detail(
                ctx, a, "ModuleCompiledLoadFailed", plan.canonical_path,
                atom_string(a, cetta_mork_bridge_last_error()));
            goto cleanup;
        }
        ok = true;
        goto cleanup;
    }

    if (plan.format.kind == CETTA_MODULE_FORMAT_MM2) {
        ok = load_module_mm2_file_into_mork_bridge(ctx, plan.canonical_path,
                                                   bridge, a, persistent, &error);
        goto cleanup;
    }

    error = module_reason_with_detail(
        ctx, a, "MorkIncludeUnsupportedFormat", plan.canonical_path,
        atom_string(a,
                    "mork:include! currently supports only direct .mm2 or .act modules"));

cleanup:
    if (!ok) {
        return atom_error(a, library_call_expr(a, head, args, nargs),
                          error ? error : atom_symbol(a, "mork:include! failed"));
    }
    return atom_unit(a);
}

static Atom *mork_zipper_new_native(CettaLibraryContext *ctx, Arena *a,
                                    Atom *head, Atom **args, uint32_t nargs) {
    CettaMorkSpaceResource *space;
    CettaMorkSpaceHandle *bridge = NULL;
    CettaMorkCursorHandle *cursor = NULL;
    CettaMorkCursorResource *resource = NULL;
    uint64_t id = 0;

    /* The zipper family is an explicit bridge-native inspection seam. It is
       intentionally cursor/path oriented rather than another AtomId transport
       path, so future optimizations should not try to fold it into PATHMAP
       materialization. */
    if (nargs != 1) {
        return library_signature_error(a, head, args, nargs,
                                       "expected MorkSpace");
    }
    space = library_explicit_mork_space_arg(ctx, args[0]);
    if (!space) {
        return library_signature_error(a, head, args, nargs,
                                       "expected MorkSpace as first argument");
    }
    if (!library_mork_space_bridge(space, &bridge) || !bridge) {
        return library_mork_bridge_error(a, head, args, nargs,
                                         "MORK zipper bridge unavailable: ");
    }
    cursor = cetta_mork_bridge_cursor_new(bridge);
    if (!cursor) {
        return library_mork_bridge_error(a, head, args, nargs,
                                         "MORK zipper allocation failed: ");
    }

    resource = cetta_malloc(sizeof(CettaMorkCursorResource));
    resource->cursor = cursor;
    resource->kind = space->kind;
    if (!cetta_native_handle_alloc(ctx, CETTA_MORK_CURSOR_HANDLE_KIND, resource,
                                   library_mork_cursor_free_resource, &id)) {
        library_mork_cursor_free_resource(resource);
        return atom_error(a, library_call_expr(a, head, args, nargs),
                          atom_string(a, "MORK zipper handle allocation failed"));
    }
    return cetta_native_handle_atom(a, CETTA_MORK_CURSOR_HANDLE_KIND, id);
}

static Atom *mork_zipper_close_native(CettaLibraryContext *ctx, Arena *a,
                                      Atom *head, Atom **args, uint32_t nargs) {
    uint64_t id = 0;
    if (nargs != 1 || !cetta_native_handle_arg(args[0], CETTA_MORK_CURSOR_HANDLE_KIND, &id)) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-cursor handle");
    }
    if (!cetta_native_handle_close(ctx, CETTA_MORK_CURSOR_HANDLE_KIND, id)) {
        return atom_error(a, library_call_expr(a, head, args, nargs),
                          atom_string(a, "unknown or closed mork-cursor handle"));
    }
    return atom_unit(a);
}

static Atom *mork_zipper_bool_native(CettaLibraryContext *ctx, Arena *a,
                                     Atom *head, Atom **args, uint32_t nargs,
                                     SymbolId which) {
    CettaMorkCursorResource *resource;
    bool value = false;

    if (nargs != 1) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-cursor handle");
    }
    resource = library_mork_cursor_handle(ctx, args[0]);
    if (!resource || !resource->cursor) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-cursor handle");
    }

    if (which == g_builtin_syms.lib_mork_zipper_path_exists) {
        if (!cetta_mork_bridge_cursor_path_exists(resource->cursor, &value)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK zipper path-exists failed: ");
        }
    } else {
        if (!cetta_mork_bridge_cursor_is_val(resource->cursor, &value)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK zipper is-val failed: ");
        }
    }
    return atom_bool(a, value);
}

static Atom *mork_zipper_bytes_native(CettaLibraryContext *ctx, Arena *a,
                                      Atom *head, Atom **args, uint32_t nargs,
                                      SymbolId which) {
    CettaMorkCursorResource *resource;
    uint8_t *bytes = NULL;
    size_t len = 0;
    Atom *result;

    if (nargs != 1) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-cursor handle");
    }
    resource = library_mork_cursor_handle(ctx, args[0]);
    if (!resource || !resource->cursor) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-cursor handle");
    }

    if (which == g_builtin_syms.lib_mork_zipper_path_bytes) {
        if (!cetta_mork_bridge_cursor_path_bytes(resource->cursor, &bytes, &len)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK zipper path-bytes failed: ");
        }
    } else {
        if (!cetta_mork_bridge_cursor_child_bytes(resource->cursor, &bytes, &len)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK zipper child-bytes failed: ");
        }
    }
    result = library_byte_list_atom(a, bytes, len);
    cetta_mork_bridge_bytes_free(bytes, len);
    return result;
}

static Atom *mork_zipper_count_native(CettaLibraryContext *ctx, Arena *a,
                                      Atom *head, Atom **args, uint32_t nargs,
                                      SymbolId which) {
    CettaMorkCursorResource *resource;
    uint64_t value = 0;

    if (nargs != 1) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-cursor handle");
    }
    resource = library_mork_cursor_handle(ctx, args[0]);
    if (!resource || !resource->cursor) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-cursor handle");
    }

    if (which == g_builtin_syms.lib_mork_zipper_child_count) {
        if (!cetta_mork_bridge_cursor_child_count(resource->cursor, &value)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK zipper child-count failed: ");
        }
    } else if (which == g_builtin_syms.lib_mork_zipper_val_count) {
        if (!cetta_mork_bridge_cursor_val_count(resource->cursor, &value)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK zipper val-count failed: ");
        }
    } else {
        if (!cetta_mork_bridge_cursor_depth(resource->cursor, &value)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK zipper depth failed: ");
        }
    }
    return atom_int(a, (int64_t)value);
}

static Atom *mork_zipper_reset_native(CettaLibraryContext *ctx, Arena *a,
                                      Atom *head, Atom **args, uint32_t nargs) {
    CettaMorkCursorResource *resource;
    if (nargs != 1) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-cursor handle");
    }
    resource = library_mork_cursor_handle(ctx, args[0]);
    if (!resource || !resource->cursor) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-cursor handle");
    }
    if (!cetta_mork_bridge_cursor_reset(resource->cursor)) {
        return library_mork_bridge_error(a, head, args, nargs,
                                         "MORK zipper reset failed: ");
    }
    return atom_unit(a);
}

static Atom *mork_zipper_ascend_native(CettaLibraryContext *ctx, Arena *a,
                                       Atom *head, Atom **args, uint32_t nargs) {
    CettaMorkCursorResource *resource;
    bool moved = false;
    int steps = 0;

    if (nargs != 2 || !library_int_arg(args[1], &steps) || steps < 0) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-cursor handle and non-negative step count");
    }
    resource = library_mork_cursor_handle(ctx, args[0]);
    if (!resource || !resource->cursor) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-cursor handle as first argument");
    }
    if (!cetta_mork_bridge_cursor_ascend(resource->cursor, (uint64_t)steps, &moved)) {
        return library_mork_bridge_error(a, head, args, nargs,
                                         "MORK zipper ascend failed: ");
    }
    return atom_bool(a, moved);
}

static Atom *mork_zipper_descend_byte_native(CettaLibraryContext *ctx, Arena *a,
                                             Atom *head, Atom **args, uint32_t nargs) {
    CettaMorkCursorResource *resource;
    bool moved = false;
    int byte = 0;

    if (nargs != 2 || !library_int_arg(args[1], &byte) || byte < 0 || byte > 255) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-cursor handle and byte in 0..255");
    }
    resource = library_mork_cursor_handle(ctx, args[0]);
    if (!resource || !resource->cursor) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-cursor handle as first argument");
    }
    if (!cetta_mork_bridge_cursor_descend_byte(resource->cursor, (uint32_t)byte, &moved)) {
        return library_mork_bridge_error(a, head, args, nargs,
                                         "MORK zipper descend-byte failed: ");
    }
    return atom_bool(a, moved);
}

static Atom *mork_zipper_descend_index_native(CettaLibraryContext *ctx, Arena *a,
                                              Atom *head, Atom **args, uint32_t nargs) {
    CettaMorkCursorResource *resource;
    bool moved = false;
    int index = 0;

    if (nargs != 2 || !library_int_arg(args[1], &index) || index < 0) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-cursor handle and non-negative child index");
    }
    resource = library_mork_cursor_handle(ctx, args[0]);
    if (!resource || !resource->cursor) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-cursor handle as first argument");
    }
    if (!cetta_mork_bridge_cursor_descend_index(resource->cursor, (uint64_t)index, &moved)) {
        return library_mork_bridge_error(a, head, args, nargs,
                                         "MORK zipper descend-index failed: ");
    }
    return atom_bool(a, moved);
}

static Atom *mork_zipper_descend_simple_native(CettaLibraryContext *ctx, Arena *a,
                                               Atom *head, Atom **args, uint32_t nargs,
                                               SymbolId which) {
    CettaMorkCursorResource *resource;
    bool moved = false;

    if (nargs != 1) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-cursor handle");
    }
    resource = library_mork_cursor_handle(ctx, args[0]);
    if (!resource || !resource->cursor) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-cursor handle");
    }

    if (which == g_builtin_syms.lib_mork_zipper_descend_first) {
        if (!cetta_mork_bridge_cursor_descend_first(resource->cursor, &moved)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK zipper descend-first failed: ");
        }
    } else if (which == g_builtin_syms.lib_mork_zipper_descend_last) {
        if (!cetta_mork_bridge_cursor_descend_last(resource->cursor, &moved)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK zipper descend-last failed: ");
        }
    } else if (which == g_builtin_syms.lib_mork_zipper_descend_until) {
        if (!cetta_mork_bridge_cursor_descend_until(resource->cursor, &moved)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK zipper descend-until failed: ");
        }
    } else if (which == g_builtin_syms.lib_mork_zipper_ascend_until) {
        if (!cetta_mork_bridge_cursor_ascend_until(resource->cursor, &moved)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK zipper ascend-until failed: ");
        }
    } else if (which == g_builtin_syms.lib_mork_zipper_ascend_until_branch) {
        if (!cetta_mork_bridge_cursor_ascend_until_branch(resource->cursor, &moved)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK zipper ascend-until-branch failed: ");
        }
    } else if (which == g_builtin_syms.lib_mork_zipper_next_sibling_byte) {
        if (!cetta_mork_bridge_cursor_next_sibling_byte(resource->cursor, &moved)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK zipper next-sibling-byte failed: ");
        }
    } else if (which == g_builtin_syms.lib_mork_zipper_prev_sibling_byte) {
        if (!cetta_mork_bridge_cursor_prev_sibling_byte(resource->cursor, &moved)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK zipper prev-sibling-byte failed: ");
        }
    } else if (which == g_builtin_syms.lib_mork_zipper_next_step) {
        if (!cetta_mork_bridge_cursor_next_step(resource->cursor, &moved)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK zipper next-step failed: ");
        }
    } else {
        if (!cetta_mork_bridge_cursor_next_val(resource->cursor, &moved)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK zipper next-val failed: ");
        }
    }
    return atom_bool(a, moved);
}

static Atom *mork_zipper_descend_until_max_bytes_native(CettaLibraryContext *ctx, Arena *a,
                                                        Atom *head, Atom **args,
                                                        uint32_t nargs) {
    CettaMorkCursorResource *resource;
    bool moved = false;
    int max_bytes = 0;

    if (nargs != 2 || !library_int_arg(args[1], &max_bytes) || max_bytes < 0) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-cursor handle and non-negative max byte count");
    }
    resource = library_mork_cursor_handle(ctx, args[0]);
    if (!resource || !resource->cursor) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-cursor handle as first argument");
    }
    if (!cetta_mork_bridge_cursor_descend_until_max_bytes(resource->cursor,
                                                          (uint64_t)max_bytes,
                                                          &moved)) {
        return library_mork_bridge_error(a, head, args, nargs,
                                         "MORK zipper descend-until-max-bytes failed: ");
    }
    return atom_bool(a, moved);
}

static Atom *mork_zipper_fork_native(CettaLibraryContext *ctx, Arena *a,
                                     Atom *head, Atom **args, uint32_t nargs) {
    CettaMorkCursorResource *resource;
    CettaMorkCursorHandle *forked = NULL;
    CettaMorkCursorResource *forked_resource = NULL;
    uint64_t id = 0;

    if (nargs != 1) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-cursor handle");
    }
    resource = library_mork_cursor_handle(ctx, args[0]);
    if (!resource || !resource->cursor) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-cursor handle");
    }
    forked = cetta_mork_bridge_cursor_fork(resource->cursor);
    if (!forked) {
        return library_mork_bridge_error(a, head, args, nargs,
                                         "MORK zipper fork failed: ");
    }
    forked_resource = cetta_malloc(sizeof(CettaMorkCursorResource));
    forked_resource->cursor = forked;
    forked_resource->kind = resource->kind;
    if (!cetta_native_handle_alloc(ctx, CETTA_MORK_CURSOR_HANDLE_KIND, forked_resource,
                                   library_mork_cursor_free_resource, &id)) {
        library_mork_cursor_free_resource(forked_resource);
        return atom_error(a, library_call_expr(a, head, args, nargs),
                          atom_string(a, "MORK zipper fork handle allocation failed"));
    }
    return cetta_native_handle_atom(a, CETTA_MORK_CURSOR_HANDLE_KIND, id);
}

static Atom *mork_zipper_materialize_native(CettaLibraryContext *ctx, Arena *a,
                                            Atom *head, Atom **args, uint32_t nargs,
                                            SymbolId which) {
    CettaMorkCursorResource *resource;
    CettaMorkSpaceHandle *bridge_space = NULL;

    if (nargs != 1) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-cursor handle");
    }
    resource = library_mork_cursor_handle(ctx, args[0]);
    if (!resource || !resource->cursor) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-cursor handle");
    }
    if (which == g_builtin_syms.lib_mork_zipper_make_map) {
        bridge_space = cetta_mork_bridge_cursor_make_map(resource->cursor);
    } else {
        bridge_space = cetta_mork_bridge_cursor_make_snapshot_map(resource->cursor);
    }
    if (!bridge_space) {
        return library_mork_bridge_error(a, head, args, nargs,
                                         "MORK zipper materialization failed: ");
    }
    return library_mork_space_handle_atom(ctx, a, head, args, nargs,
                                          bridge_space, resource->kind,
                                          "MORK zipper materialization failed");
}

static Atom *mork_product_zipper_new_native(CettaLibraryContext *ctx, Arena *a,
                                            Atom *head, Atom **args, uint32_t nargs) {
    CettaMorkSpaceHandle **bridge_spaces = NULL;
    CettaMorkProductCursorHandle *cursor = NULL;
    CettaMorkProductCursorResource *resource = NULL;
    uint64_t id = 0;

    if (nargs < 2) {
        return library_signature_error(a, head, args, nargs,
                                       "expected at least two MorkSpace handles");
    }

    bridge_spaces = arena_alloc(a, sizeof(CettaMorkSpaceHandle *) * nargs);
    for (uint32_t i = 0; i < nargs; i++) {
        CettaMorkSpaceResource *space = library_explicit_mork_space_arg(ctx, args[i]);
        if (!space) {
            return library_signature_error(a, head, args, nargs,
                                           "expected MorkSpace arguments");
        }
        if (!library_mork_space_bridge(space, &bridge_spaces[i]) ||
            !bridge_spaces[i]) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK product-zipper bridge unavailable: ");
        }
    }

    cursor = cetta_mork_bridge_product_cursor_new(
        (const CettaMorkSpaceHandle *const *)bridge_spaces, nargs);
    if (!cursor) {
        return library_mork_bridge_error(a, head, args, nargs,
                                         "MORK product-zipper allocation failed: ");
    }

    resource = cetta_malloc(sizeof(CettaMorkProductCursorResource));
    resource->cursor = cursor;
    if (!cetta_native_handle_alloc(ctx, CETTA_MORK_PRODUCT_CURSOR_HANDLE_KIND,
                                   resource, library_mork_product_cursor_free_resource,
                                   &id)) {
        library_mork_product_cursor_free_resource(resource);
        return atom_error(a, library_call_expr(a, head, args, nargs),
                          atom_string(a,
                                      "MORK product-zipper handle allocation failed"));
    }
    return cetta_native_handle_atom(a, CETTA_MORK_PRODUCT_CURSOR_HANDLE_KIND, id);
}

static Atom *mork_product_zipper_close_native(CettaLibraryContext *ctx, Arena *a,
                                              Atom *head, Atom **args, uint32_t nargs) {
    uint64_t id = 0;
    if (nargs != 1 ||
        !cetta_native_handle_arg(args[0], CETTA_MORK_PRODUCT_CURSOR_HANDLE_KIND, &id)) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-product-cursor handle");
    }
    if (!cetta_native_handle_close(ctx, CETTA_MORK_PRODUCT_CURSOR_HANDLE_KIND, id)) {
        return atom_error(a, library_call_expr(a, head, args, nargs),
                          atom_string(a,
                                      "unknown or closed mork-product-cursor handle"));
    }
    return atom_unit(a);
}

static Atom *mork_product_zipper_bool_native(CettaLibraryContext *ctx, Arena *a,
                                             Atom *head, Atom **args, uint32_t nargs,
                                             SymbolId which) {
    CettaMorkProductCursorResource *resource;
    bool value = false;

    if (nargs != 1) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-product-cursor handle");
    }
    resource = library_mork_product_cursor_handle(ctx, args[0]);
    if (!resource || !resource->cursor) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-product-cursor handle");
    }
    if (which == g_builtin_syms.lib_mork_product_zipper_path_exists) {
        if (!cetta_mork_bridge_product_cursor_path_exists(resource->cursor, &value)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK product-zipper path-exists failed: ");
        }
    } else {
        if (!cetta_mork_bridge_product_cursor_is_val(resource->cursor, &value)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK product-zipper is-val failed: ");
        }
    }
    return atom_bool(a, value);
}

static Atom *mork_product_zipper_bytes_native(CettaLibraryContext *ctx, Arena *a,
                                              Atom *head, Atom **args, uint32_t nargs,
                                              SymbolId which) {
    CettaMorkProductCursorResource *resource;
    uint8_t *bytes = NULL;
    size_t len = 0;
    uint32_t count = 0;
    Atom *result;

    if (nargs != 1) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-product-cursor handle");
    }
    resource = library_mork_product_cursor_handle(ctx, args[0]);
    if (!resource || !resource->cursor) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-product-cursor handle");
    }

    if (which == g_builtin_syms.lib_mork_product_zipper_path_bytes) {
        if (!cetta_mork_bridge_product_cursor_path_bytes(resource->cursor, &bytes, &len)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK product-zipper path-bytes failed: ");
        }
        result = library_byte_list_atom(a, bytes, len);
    } else if (which == g_builtin_syms.lib_mork_product_zipper_child_bytes) {
        if (!cetta_mork_bridge_product_cursor_child_bytes(resource->cursor, &bytes, &len)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK product-zipper child-bytes failed: ");
        }
        result = library_byte_list_atom(a, bytes, len);
    } else {
        if (!cetta_mork_bridge_product_cursor_path_indices(resource->cursor, &bytes, &len,
                                                           &count)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK product-zipper path-indices failed: ");
        }
        (void)count;
        result = library_u64_be_list_atom(a, bytes, len);
    }
    cetta_mork_bridge_bytes_free(bytes, len);
    return result;
}

static Atom *mork_product_zipper_count_native(CettaLibraryContext *ctx, Arena *a,
                                              Atom *head, Atom **args, uint32_t nargs,
                                              SymbolId which) {
    CettaMorkProductCursorResource *resource;
    uint64_t value = 0;

    if (nargs != 1) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-product-cursor handle");
    }
    resource = library_mork_product_cursor_handle(ctx, args[0]);
    if (!resource || !resource->cursor) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-product-cursor handle");
    }

    if (which == g_builtin_syms.lib_mork_product_zipper_child_count) {
        if (!cetta_mork_bridge_product_cursor_child_count(resource->cursor, &value)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK product-zipper child-count failed: ");
        }
    } else if (which == g_builtin_syms.lib_mork_product_zipper_val_count) {
        if (!cetta_mork_bridge_product_cursor_val_count(resource->cursor, &value)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK product-zipper val-count failed: ");
        }
    } else if (which == g_builtin_syms.lib_mork_product_zipper_depth) {
        if (!cetta_mork_bridge_product_cursor_depth(resource->cursor, &value)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK product-zipper depth failed: ");
        }
    } else if (which == g_builtin_syms.lib_mork_product_zipper_factor_count) {
        if (!cetta_mork_bridge_product_cursor_factor_count(resource->cursor, &value)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK product-zipper factor-count failed: ");
        }
    } else {
        if (!cetta_mork_bridge_product_cursor_focus_factor(resource->cursor, &value)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK product-zipper focus-factor failed: ");
        }
    }
    return atom_int(a, (int64_t)value);
}

static Atom *mork_product_zipper_reset_native(CettaLibraryContext *ctx, Arena *a,
                                              Atom *head, Atom **args, uint32_t nargs) {
    CettaMorkProductCursorResource *resource;
    if (nargs != 1) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-product-cursor handle");
    }
    resource = library_mork_product_cursor_handle(ctx, args[0]);
    if (!resource || !resource->cursor) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-product-cursor handle");
    }
    if (!cetta_mork_bridge_product_cursor_reset(resource->cursor)) {
        return library_mork_bridge_error(a, head, args, nargs,
                                         "MORK product-zipper reset failed: ");
    }
    return atom_unit(a);
}

static Atom *mork_product_zipper_ascend_native(CettaLibraryContext *ctx, Arena *a,
                                               Atom *head, Atom **args, uint32_t nargs) {
    CettaMorkProductCursorResource *resource;
    bool moved = false;
    int steps = 0;

    if (nargs != 2 || !library_int_arg(args[1], &steps) || steps < 0) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-product-cursor handle and non-negative step count");
    }
    resource = library_mork_product_cursor_handle(ctx, args[0]);
    if (!resource || !resource->cursor) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-product-cursor handle as first argument");
    }
    if (!cetta_mork_bridge_product_cursor_ascend(resource->cursor, (uint64_t)steps, &moved)) {
        return library_mork_bridge_error(a, head, args, nargs,
                                         "MORK product-zipper ascend failed: ");
    }
    return atom_bool(a, moved);
}

static Atom *mork_product_zipper_descend_byte_native(CettaLibraryContext *ctx, Arena *a,
                                                     Atom *head, Atom **args,
                                                     uint32_t nargs) {
    CettaMorkProductCursorResource *resource;
    bool moved = false;
    int byte = 0;

    if (nargs != 2 || !library_int_arg(args[1], &byte) || byte < 0 || byte > 255) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-product-cursor handle and byte in 0..255");
    }
    resource = library_mork_product_cursor_handle(ctx, args[0]);
    if (!resource || !resource->cursor) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-product-cursor handle as first argument");
    }
    if (!cetta_mork_bridge_product_cursor_descend_byte(resource->cursor, (uint32_t)byte,
                                                       &moved)) {
        return library_mork_bridge_error(a, head, args, nargs,
                                         "MORK product-zipper descend-byte failed: ");
    }
    return atom_bool(a, moved);
}

static Atom *mork_product_zipper_descend_index_native(CettaLibraryContext *ctx, Arena *a,
                                                      Atom *head, Atom **args,
                                                      uint32_t nargs) {
    CettaMorkProductCursorResource *resource;
    bool moved = false;
    int index = 0;

    if (nargs != 2 || !library_int_arg(args[1], &index) || index < 0) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-product-cursor handle and non-negative child index");
    }
    resource = library_mork_product_cursor_handle(ctx, args[0]);
    if (!resource || !resource->cursor) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-product-cursor handle as first argument");
    }
    if (!cetta_mork_bridge_product_cursor_descend_index(resource->cursor, (uint64_t)index,
                                                        &moved)) {
        return library_mork_bridge_error(a, head, args, nargs,
                                         "MORK product-zipper descend-index failed: ");
    }
    return atom_bool(a, moved);
}

static Atom *mork_product_zipper_descend_simple_native(CettaLibraryContext *ctx, Arena *a,
                                                       Atom *head, Atom **args,
                                                       uint32_t nargs,
                                                       SymbolId which) {
    CettaMorkProductCursorResource *resource;
    bool moved = false;

    if (nargs != 1) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-product-cursor handle");
    }
    resource = library_mork_product_cursor_handle(ctx, args[0]);
    if (!resource || !resource->cursor) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-product-cursor handle");
    }
    if (which == g_builtin_syms.lib_mork_product_zipper_descend_first) {
        if (!cetta_mork_bridge_product_cursor_descend_first(resource->cursor, &moved)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK product-zipper descend-first failed: ");
        }
    } else if (which == g_builtin_syms.lib_mork_product_zipper_descend_last) {
        if (!cetta_mork_bridge_product_cursor_descend_last(resource->cursor, &moved)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK product-zipper descend-last failed: ");
        }
    } else if (which == g_builtin_syms.lib_mork_product_zipper_descend_until) {
        if (!cetta_mork_bridge_product_cursor_descend_until(resource->cursor, &moved)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK product-zipper descend-until failed: ");
        }
    } else if (which == g_builtin_syms.lib_mork_product_zipper_ascend_until) {
        if (!cetta_mork_bridge_product_cursor_ascend_until(resource->cursor, &moved)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK product-zipper ascend-until failed: ");
        }
    } else if (which == g_builtin_syms.lib_mork_product_zipper_ascend_until_branch) {
        if (!cetta_mork_bridge_product_cursor_ascend_until_branch(resource->cursor, &moved)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK product-zipper ascend-until-branch failed: ");
        }
    } else if (which == g_builtin_syms.lib_mork_product_zipper_next_sibling_byte) {
        if (!cetta_mork_bridge_product_cursor_next_sibling_byte(resource->cursor, &moved)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK product-zipper next-sibling-byte failed: ");
        }
    } else if (which == g_builtin_syms.lib_mork_product_zipper_prev_sibling_byte) {
        if (!cetta_mork_bridge_product_cursor_prev_sibling_byte(resource->cursor, &moved)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK product-zipper prev-sibling-byte failed: ");
        }
    } else if (which == g_builtin_syms.lib_mork_product_zipper_next_step) {
        if (!cetta_mork_bridge_product_cursor_next_step(resource->cursor, &moved)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK product-zipper next-step failed: ");
        }
    } else {
        if (!cetta_mork_bridge_product_cursor_next_val(resource->cursor, &moved)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK product-zipper next-val failed: ");
        }
    }
    return atom_bool(a, moved);
}

static Atom *mork_product_zipper_descend_until_max_bytes_native(
    CettaLibraryContext *ctx,
    Arena *a,
    Atom *head,
    Atom **args,
    uint32_t nargs) {
    CettaMorkProductCursorResource *resource;
    bool moved = false;
    int max_bytes = 0;

    if (nargs != 2 || !library_int_arg(args[1], &max_bytes) || max_bytes < 0) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-product-cursor handle and non-negative max byte count");
    }
    resource = library_mork_product_cursor_handle(ctx, args[0]);
    if (!resource || !resource->cursor) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-product-cursor handle as first argument");
    }
    if (!cetta_mork_bridge_product_cursor_descend_until_max_bytes(resource->cursor,
                                                                  (uint64_t)max_bytes,
                                                                  &moved)) {
        return library_mork_bridge_error(a, head, args, nargs,
                                         "MORK product-zipper descend-until-max-bytes failed: ");
    }
    return atom_bool(a, moved);
}

static Atom *mork_overlay_zipper_new_native(CettaLibraryContext *ctx, Arena *a,
                                            Atom *head, Atom **args, uint32_t nargs) {
    CettaMorkSpaceResource *base_space = NULL;
    CettaMorkSpaceResource *overlay_space = NULL;
    CettaMorkSpaceHandle *base_bridge = NULL;
    CettaMorkSpaceHandle *overlay_bridge = NULL;
    CettaMorkOverlayCursorHandle *cursor = NULL;
    CettaMorkOverlayCursorResource *resource = NULL;
    uint64_t id = 0;

    if (nargs != 2) {
        return library_signature_error(a, head, args, nargs,
                                       "expected two MorkSpace handles");
    }

    base_space = library_explicit_mork_space_arg(ctx, args[0]);
    overlay_space = library_explicit_mork_space_arg(ctx, args[1]);
    if (!base_space || !overlay_space) {
        return library_signature_error(a, head, args, nargs,
                                       "expected MorkSpace arguments");
    }
    if (!library_mork_space_bridge(base_space, &base_bridge) ||
        !base_bridge ||
        !library_mork_space_bridge(overlay_space, &overlay_bridge) ||
        !overlay_bridge) {
        return library_mork_bridge_error(a, head, args, nargs,
                                         "MORK overlay-zipper bridge unavailable: ");
    }

    cursor = cetta_mork_bridge_overlay_cursor_new(base_bridge, overlay_bridge);
    if (!cursor) {
        return library_mork_bridge_error(a, head, args, nargs,
                                         "MORK overlay-zipper allocation failed: ");
    }

    resource = cetta_malloc(sizeof(CettaMorkOverlayCursorResource));
    resource->cursor = cursor;
    if (!cetta_native_handle_alloc(ctx, CETTA_MORK_OVERLAY_CURSOR_HANDLE_KIND,
                                   resource, library_mork_overlay_cursor_free_resource,
                                   &id)) {
        library_mork_overlay_cursor_free_resource(resource);
        return atom_error(a, library_call_expr(a, head, args, nargs),
                          atom_string(a,
                                      "MORK overlay-zipper handle allocation failed"));
    }
    return cetta_native_handle_atom(a, CETTA_MORK_OVERLAY_CURSOR_HANDLE_KIND, id);
}

static Atom *mork_overlay_zipper_close_native(CettaLibraryContext *ctx, Arena *a,
                                              Atom *head, Atom **args, uint32_t nargs) {
    uint64_t id = 0;
    if (nargs != 1 ||
        !cetta_native_handle_arg(args[0], CETTA_MORK_OVERLAY_CURSOR_HANDLE_KIND, &id)) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-overlay-cursor handle");
    }
    if (!cetta_native_handle_close(ctx, CETTA_MORK_OVERLAY_CURSOR_HANDLE_KIND, id)) {
        return atom_error(a, library_call_expr(a, head, args, nargs),
                          atom_string(a,
                                      "unknown or closed mork-overlay-cursor handle"));
    }
    return atom_unit(a);
}

static Atom *mork_overlay_zipper_bool_native(CettaLibraryContext *ctx, Arena *a,
                                             Atom *head, Atom **args, uint32_t nargs,
                                             SymbolId which) {
    CettaMorkOverlayCursorResource *resource;
    bool value = false;

    if (nargs != 1) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-overlay-cursor handle");
    }
    resource = library_mork_overlay_cursor_handle(ctx, args[0]);
    if (!resource || !resource->cursor) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-overlay-cursor handle");
    }
    if (which == g_builtin_syms.lib_mork_overlay_zipper_path_exists) {
        if (!cetta_mork_bridge_overlay_cursor_path_exists(resource->cursor, &value)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK overlay-zipper path-exists failed: ");
        }
    } else {
        if (!cetta_mork_bridge_overlay_cursor_is_val(resource->cursor, &value)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK overlay-zipper is-val failed: ");
        }
    }
    return atom_bool(a, value);
}

static Atom *mork_overlay_zipper_bytes_native(CettaLibraryContext *ctx, Arena *a,
                                              Atom *head, Atom **args, uint32_t nargs,
                                              SymbolId which) {
    CettaMorkOverlayCursorResource *resource;
    uint8_t *bytes = NULL;
    size_t len = 0;
    Atom *result;

    if (nargs != 1) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-overlay-cursor handle");
    }
    resource = library_mork_overlay_cursor_handle(ctx, args[0]);
    if (!resource || !resource->cursor) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-overlay-cursor handle");
    }

    if (which == g_builtin_syms.lib_mork_overlay_zipper_path_bytes) {
        if (!cetta_mork_bridge_overlay_cursor_path_bytes(resource->cursor, &bytes, &len)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK overlay-zipper path-bytes failed: ");
        }
    } else {
        if (!cetta_mork_bridge_overlay_cursor_child_bytes(resource->cursor, &bytes, &len)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK overlay-zipper child-bytes failed: ");
        }
    }
    result = library_byte_list_atom(a, bytes, len);
    cetta_mork_bridge_bytes_free(bytes, len);
    return result;
}

static Atom *mork_overlay_zipper_count_native(CettaLibraryContext *ctx, Arena *a,
                                              Atom *head, Atom **args, uint32_t nargs,
                                              SymbolId which) {
    CettaMorkOverlayCursorResource *resource;
    uint64_t value = 0;

    if (nargs != 1) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-overlay-cursor handle");
    }
    resource = library_mork_overlay_cursor_handle(ctx, args[0]);
    if (!resource || !resource->cursor) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-overlay-cursor handle");
    }

    if (which == g_builtin_syms.lib_mork_overlay_zipper_child_count) {
        if (!cetta_mork_bridge_overlay_cursor_child_count(resource->cursor, &value)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK overlay-zipper child-count failed: ");
        }
    } else {
        if (!cetta_mork_bridge_overlay_cursor_depth(resource->cursor, &value)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK overlay-zipper depth failed: ");
        }
    }
    return atom_int(a, (int64_t)value);
}

static Atom *mork_overlay_zipper_reset_native(CettaLibraryContext *ctx, Arena *a,
                                              Atom *head, Atom **args, uint32_t nargs) {
    CettaMorkOverlayCursorResource *resource;
    if (nargs != 1) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-overlay-cursor handle");
    }
    resource = library_mork_overlay_cursor_handle(ctx, args[0]);
    if (!resource || !resource->cursor) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-overlay-cursor handle");
    }
    if (!cetta_mork_bridge_overlay_cursor_reset(resource->cursor)) {
        return library_mork_bridge_error(a, head, args, nargs,
                                         "MORK overlay-zipper reset failed: ");
    }
    return atom_unit(a);
}

static Atom *mork_overlay_zipper_ascend_native(CettaLibraryContext *ctx, Arena *a,
                                               Atom *head, Atom **args, uint32_t nargs) {
    CettaMorkOverlayCursorResource *resource;
    bool moved = false;
    int steps = 0;

    if (nargs != 2 || !library_int_arg(args[1], &steps) || steps < 0) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-overlay-cursor handle and non-negative step count");
    }
    resource = library_mork_overlay_cursor_handle(ctx, args[0]);
    if (!resource || !resource->cursor) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-overlay-cursor handle as first argument");
    }
    if (!cetta_mork_bridge_overlay_cursor_ascend(resource->cursor, (uint64_t)steps, &moved)) {
        return library_mork_bridge_error(a, head, args, nargs,
                                         "MORK overlay-zipper ascend failed: ");
    }
    return atom_bool(a, moved);
}

static Atom *mork_overlay_zipper_descend_byte_native(CettaLibraryContext *ctx, Arena *a,
                                                     Atom *head, Atom **args,
                                                     uint32_t nargs) {
    CettaMorkOverlayCursorResource *resource;
    bool moved = false;
    int byte = 0;

    if (nargs != 2 || !library_int_arg(args[1], &byte) || byte < 0 || byte > 255) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-overlay-cursor handle and byte in 0..255");
    }
    resource = library_mork_overlay_cursor_handle(ctx, args[0]);
    if (!resource || !resource->cursor) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-overlay-cursor handle as first argument");
    }
    if (!cetta_mork_bridge_overlay_cursor_descend_byte(resource->cursor, (uint32_t)byte,
                                                       &moved)) {
        return library_mork_bridge_error(a, head, args, nargs,
                                         "MORK overlay-zipper descend-byte failed: ");
    }
    return atom_bool(a, moved);
}

static Atom *mork_overlay_zipper_descend_index_native(CettaLibraryContext *ctx, Arena *a,
                                                      Atom *head, Atom **args,
                                                      uint32_t nargs) {
    CettaMorkOverlayCursorResource *resource;
    bool moved = false;
    int index = 0;

    if (nargs != 2 || !library_int_arg(args[1], &index) || index < 0) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-overlay-cursor handle and non-negative child index");
    }
    resource = library_mork_overlay_cursor_handle(ctx, args[0]);
    if (!resource || !resource->cursor) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-overlay-cursor handle as first argument");
    }
    if (!cetta_mork_bridge_overlay_cursor_descend_index(resource->cursor, (uint64_t)index,
                                                        &moved)) {
        return library_mork_bridge_error(a, head, args, nargs,
                                         "MORK overlay-zipper descend-index failed: ");
    }
    return atom_bool(a, moved);
}

static Atom *mork_overlay_zipper_descend_simple_native(CettaLibraryContext *ctx, Arena *a,
                                                       Atom *head, Atom **args,
                                                       uint32_t nargs,
                                                       SymbolId which) {
    CettaMorkOverlayCursorResource *resource;
    bool moved = false;

    if (nargs != 1) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-overlay-cursor handle");
    }
    resource = library_mork_overlay_cursor_handle(ctx, args[0]);
    if (!resource || !resource->cursor) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-overlay-cursor handle");
    }
    if (which == g_builtin_syms.lib_mork_overlay_zipper_descend_first) {
        if (!cetta_mork_bridge_overlay_cursor_descend_first(resource->cursor, &moved)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK overlay-zipper descend-first failed: ");
        }
    } else if (which == g_builtin_syms.lib_mork_overlay_zipper_descend_last) {
        if (!cetta_mork_bridge_overlay_cursor_descend_last(resource->cursor, &moved)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK overlay-zipper descend-last failed: ");
        }
    } else if (which == g_builtin_syms.lib_mork_overlay_zipper_descend_until) {
        if (!cetta_mork_bridge_overlay_cursor_descend_until(resource->cursor, &moved)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK overlay-zipper descend-until failed: ");
        }
    } else if (which == g_builtin_syms.lib_mork_overlay_zipper_ascend_until) {
        if (!cetta_mork_bridge_overlay_cursor_ascend_until(resource->cursor, &moved)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK overlay-zipper ascend-until failed: ");
        }
    } else if (which == g_builtin_syms.lib_mork_overlay_zipper_ascend_until_branch) {
        if (!cetta_mork_bridge_overlay_cursor_ascend_until_branch(resource->cursor, &moved)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK overlay-zipper ascend-until-branch failed: ");
        }
    } else if (which == g_builtin_syms.lib_mork_overlay_zipper_next_sibling_byte) {
        if (!cetta_mork_bridge_overlay_cursor_next_sibling_byte(resource->cursor, &moved)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK overlay-zipper next-sibling-byte failed: ");
        }
    } else if (which == g_builtin_syms.lib_mork_overlay_zipper_prev_sibling_byte) {
        if (!cetta_mork_bridge_overlay_cursor_prev_sibling_byte(resource->cursor, &moved)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK overlay-zipper prev-sibling-byte failed: ");
        }
    } else if (which == g_builtin_syms.lib_mork_overlay_zipper_next_step) {
        if (!cetta_mork_bridge_overlay_cursor_next_step(resource->cursor, &moved)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK overlay-zipper next-step failed: ");
        }
    } else {
        return atom_error(a, library_call_expr(a, head, args, nargs),
                          atom_string(a, "unsupported overlay zipper movement"));
    }
    return atom_bool(a, moved);
}

static Atom *mork_overlay_zipper_descend_until_max_bytes_native(
    CettaLibraryContext *ctx,
    Arena *a,
    Atom *head,
    Atom **args,
    uint32_t nargs) {
    CettaMorkOverlayCursorResource *resource;
    bool moved = false;
    int max_bytes = 0;

    if (nargs != 2 || !library_int_arg(args[1], &max_bytes) || max_bytes < 0) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-overlay-cursor handle and non-negative max byte count");
    }
    resource = library_mork_overlay_cursor_handle(ctx, args[0]);
    if (!resource || !resource->cursor) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-overlay-cursor handle as first argument");
    }
    if (!cetta_mork_bridge_overlay_cursor_descend_until_max_bytes(resource->cursor,
                                                                  (uint64_t)max_bytes,
                                                                  &moved)) {
        return library_mork_bridge_error(a, head, args, nargs,
                                         "MORK overlay-zipper descend-until-max-bytes failed: ");
    }
    return atom_bool(a, moved);
}

static Atom *mork_path_of_atom_native(CettaLibraryContext *ctx, Arena *a,
                                      Atom *head, Atom **args, uint32_t nargs) {
    Arena scratch;
    char *text = NULL;
    CettaMorkSpaceHandle *bridge_space = NULL;
    CettaMorkCursorHandle *cursor = NULL;
    uint8_t *bytes = NULL;
    size_t len = 0;
    Atom *result = NULL;
    bool moved = false;

    /* Explicit bridge-byte inspection helper: this answers "what bridge path
       does this atom lower to?" and is intentionally separate from the
       structural bridge->AtomId import seam. */
    if (nargs != 1) {
        return library_signature_error(a, head, args, nargs, "expected atom");
    }

    arena_init(&scratch);
    arena_set_runtime_kind(&scratch, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    text = atom_to_parseable_string(&scratch, args[0]);
    if (!text) {
        arena_free(&scratch);
        return atom_error(a, library_call_expr(a, head, args, nargs),
                          atom_string(a, "mork:path-of-atom could not render atom"));
    }
    bridge_space = cetta_mork_bridge_space_new();
    if (!bridge_space) {
        arena_free(&scratch);
        return library_mork_bridge_error(a, head, args, nargs,
                                         "MORK path-of-atom bridge allocation failed: ");
    }
    /* This helper should expose the bridge/storage path the live MORK parser
       would use, not the narrower compact expr-byte lowering shortcut. */
    if (!cetta_mork_bridge_space_add_sexpr(
            bridge_space, (const uint8_t *)text, strlen(text), NULL)) {
        cetta_mork_bridge_space_free(bridge_space);
        arena_free(&scratch);
        return library_mork_bridge_error(a, head, args, nargs,
                                         "MORK path-of-atom encoding failed: ");
    }
    cursor = cetta_mork_bridge_cursor_new(bridge_space);
    if (!cursor) {
        cetta_mork_bridge_space_free(bridge_space);
        arena_free(&scratch);
        return library_mork_bridge_error(a, head, args, nargs,
                                         "MORK path-of-atom cursor allocation failed: ");
    }
    if (!cetta_mork_bridge_cursor_descend_until(cursor, &moved)) {
        cetta_mork_bridge_cursor_free(cursor);
        cetta_mork_bridge_space_free(bridge_space);
        arena_free(&scratch);
        return library_mork_bridge_error(a, head, args, nargs,
                                         "MORK path-of-atom traversal failed: ");
    }
    if (!cetta_mork_bridge_cursor_path_bytes(cursor, &bytes, &len)) {
        cetta_mork_bridge_cursor_free(cursor);
        cetta_mork_bridge_space_free(bridge_space);
        arena_free(&scratch);
        return library_mork_bridge_error(a, head, args, nargs,
                                         "MORK path-of-atom extraction failed: ");
    }

    result = library_byte_list_atom(a, bytes, len);
    cetta_mork_bridge_bytes_free(bytes, len);
    cetta_mork_bridge_cursor_free(cursor);
    cetta_mork_bridge_space_free(bridge_space);
    arena_free(&scratch);
    return result;
}

static Atom *cetta_library_dispatch_mork(CettaLibraryContext *ctx, Arena *a,
                                         Atom *head, Atom **args, uint32_t nargs) {
    if (head->kind != ATOM_SYMBOL) return NULL;
    SymbolId head_id = head->sym_id;
    if (head_id == g_builtin_syms.lib_mork_space_new) {
        return mork_space_new_native(ctx, a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_mork_space_include) {
        return mork_space_include_native(ctx, a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_mork_space_dump_act) {
        return mork_space_dump_act_native(ctx, a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_mork_space_open_act) {
        return mork_space_open_act_native(ctx, a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_mork_space_import_act) {
        return mork_space_import_act_native(ctx, a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_mork_clone) {
        return mork_space_clone_native(ctx, a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_mork_space_step ||
        head_id == g_builtin_syms.lib_mork_space_add_atoms ||
        head_id == g_builtin_syms.lib_mork_space_add_stream ||
        head_id == g_builtin_syms.mork_add_atoms ||
        head_id == g_builtin_syms.lib_mork_space_add_atom ||
        head_id == g_builtin_syms.mork_add_atom ||
        head_id == g_builtin_syms.lib_mork_space_remove_atom ||
        head_id == g_builtin_syms.mork_remove_atom ||
        head_id == g_builtin_syms.lib_mork_space_atoms ||
        head_id == g_builtin_syms.lib_mork_space_size ||
        head_id == g_builtin_syms.lib_mork_space_count_atoms ||
        head_id == g_builtin_syms.lib_mork_space_match) {
        return mork_space_surface_native(ctx, a, head, args, nargs, head_id);
    }
    if (head_id == g_builtin_syms.lib_mork_join ||
        head_id == g_builtin_syms.lib_mork_meet ||
        head_id == g_builtin_syms.lib_mork_subtract ||
        head_id == g_builtin_syms.lib_mork_restrict) {
        return mork_space_algebra_native(ctx, a, head, args, nargs, head_id);
    }
    if (head_id == g_builtin_syms.lib_mork_zipper_new) {
        return mork_zipper_new_native(ctx, a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_mork_zipper_close) {
        return mork_zipper_close_native(ctx, a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_mork_zipper_path_exists ||
        head_id == g_builtin_syms.lib_mork_zipper_is_val) {
        return mork_zipper_bool_native(ctx, a, head, args, nargs, head_id);
    }
    if (head_id == g_builtin_syms.lib_mork_path_of_atom) {
        return mork_path_of_atom_native(ctx, a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_mork_zipper_path_bytes ||
        head_id == g_builtin_syms.lib_mork_zipper_child_bytes) {
        return mork_zipper_bytes_native(ctx, a, head, args, nargs, head_id);
    }
    if (head_id == g_builtin_syms.lib_mork_zipper_child_count ||
        head_id == g_builtin_syms.lib_mork_zipper_val_count ||
        head_id == g_builtin_syms.lib_mork_zipper_depth) {
        return mork_zipper_count_native(ctx, a, head, args, nargs, head_id);
    }
    if (head_id == g_builtin_syms.lib_mork_zipper_reset) {
        return mork_zipper_reset_native(ctx, a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_mork_zipper_ascend) {
        return mork_zipper_ascend_native(ctx, a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_mork_zipper_descend_byte) {
        return mork_zipper_descend_byte_native(ctx, a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_mork_zipper_descend_index) {
        return mork_zipper_descend_index_native(ctx, a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_mork_zipper_descend_first ||
        head_id == g_builtin_syms.lib_mork_zipper_descend_last ||
        head_id == g_builtin_syms.lib_mork_zipper_descend_until ||
        head_id == g_builtin_syms.lib_mork_zipper_ascend_until ||
        head_id == g_builtin_syms.lib_mork_zipper_ascend_until_branch ||
        head_id == g_builtin_syms.lib_mork_zipper_next_sibling_byte ||
        head_id == g_builtin_syms.lib_mork_zipper_prev_sibling_byte ||
        head_id == g_builtin_syms.lib_mork_zipper_next_step ||
        head_id == g_builtin_syms.lib_mork_zipper_next_val) {
        return mork_zipper_descend_simple_native(ctx, a, head, args, nargs, head_id);
    }
    if (head_id == g_builtin_syms.lib_mork_zipper_descend_until_max_bytes) {
        return mork_zipper_descend_until_max_bytes_native(ctx, a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_mork_zipper_fork) {
        return mork_zipper_fork_native(ctx, a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_mork_zipper_make_map ||
        head_id == g_builtin_syms.lib_mork_zipper_make_snapshot_map) {
        return mork_zipper_materialize_native(ctx, a, head, args, nargs, head_id);
    }
    if (head_id == g_builtin_syms.lib_mork_product_zipper_new) {
        return mork_product_zipper_new_native(ctx, a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_mork_product_zipper_close) {
        return mork_product_zipper_close_native(ctx, a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_mork_product_zipper_path_exists ||
        head_id == g_builtin_syms.lib_mork_product_zipper_is_val) {
        return mork_product_zipper_bool_native(ctx, a, head, args, nargs, head_id);
    }
    if (head_id == g_builtin_syms.lib_mork_product_zipper_path_bytes ||
        head_id == g_builtin_syms.lib_mork_product_zipper_child_bytes ||
        head_id == g_builtin_syms.lib_mork_product_zipper_path_indices) {
        return mork_product_zipper_bytes_native(ctx, a, head, args, nargs, head_id);
    }
    if (head_id == g_builtin_syms.lib_mork_product_zipper_child_count ||
        head_id == g_builtin_syms.lib_mork_product_zipper_val_count ||
        head_id == g_builtin_syms.lib_mork_product_zipper_depth ||
        head_id == g_builtin_syms.lib_mork_product_zipper_factor_count ||
        head_id == g_builtin_syms.lib_mork_product_zipper_focus_factor) {
        return mork_product_zipper_count_native(ctx, a, head, args, nargs, head_id);
    }
    if (head_id == g_builtin_syms.lib_mork_product_zipper_reset) {
        return mork_product_zipper_reset_native(ctx, a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_mork_product_zipper_ascend) {
        return mork_product_zipper_ascend_native(ctx, a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_mork_product_zipper_descend_byte) {
        return mork_product_zipper_descend_byte_native(ctx, a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_mork_product_zipper_descend_index) {
        return mork_product_zipper_descend_index_native(ctx, a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_mork_product_zipper_descend_first ||
        head_id == g_builtin_syms.lib_mork_product_zipper_descend_last ||
        head_id == g_builtin_syms.lib_mork_product_zipper_descend_until ||
        head_id == g_builtin_syms.lib_mork_product_zipper_ascend_until ||
        head_id == g_builtin_syms.lib_mork_product_zipper_ascend_until_branch ||
        head_id == g_builtin_syms.lib_mork_product_zipper_next_sibling_byte ||
        head_id == g_builtin_syms.lib_mork_product_zipper_prev_sibling_byte ||
        head_id == g_builtin_syms.lib_mork_product_zipper_next_step ||
        head_id == g_builtin_syms.lib_mork_product_zipper_next_val) {
        return mork_product_zipper_descend_simple_native(ctx, a, head, args, nargs,
                                                         head_id);
    }
    if (head_id == g_builtin_syms.lib_mork_product_zipper_descend_until_max_bytes) {
        return mork_product_zipper_descend_until_max_bytes_native(ctx, a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_mork_overlay_zipper_new) {
        return mork_overlay_zipper_new_native(ctx, a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_mork_overlay_zipper_close) {
        return mork_overlay_zipper_close_native(ctx, a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_mork_overlay_zipper_path_exists ||
        head_id == g_builtin_syms.lib_mork_overlay_zipper_is_val) {
        return mork_overlay_zipper_bool_native(ctx, a, head, args, nargs, head_id);
    }
    if (head_id == g_builtin_syms.lib_mork_overlay_zipper_path_bytes ||
        head_id == g_builtin_syms.lib_mork_overlay_zipper_child_bytes) {
        return mork_overlay_zipper_bytes_native(ctx, a, head, args, nargs, head_id);
    }
    if (head_id == g_builtin_syms.lib_mork_overlay_zipper_child_count ||
        head_id == g_builtin_syms.lib_mork_overlay_zipper_depth) {
        return mork_overlay_zipper_count_native(ctx, a, head, args, nargs, head_id);
    }
    if (head_id == g_builtin_syms.lib_mork_overlay_zipper_reset) {
        return mork_overlay_zipper_reset_native(ctx, a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_mork_overlay_zipper_ascend) {
        return mork_overlay_zipper_ascend_native(ctx, a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_mork_overlay_zipper_descend_byte) {
        return mork_overlay_zipper_descend_byte_native(ctx, a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_mork_overlay_zipper_descend_index) {
        return mork_overlay_zipper_descend_index_native(ctx, a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_mork_overlay_zipper_descend_first ||
        head_id == g_builtin_syms.lib_mork_overlay_zipper_descend_last ||
        head_id == g_builtin_syms.lib_mork_overlay_zipper_descend_until ||
        head_id == g_builtin_syms.lib_mork_overlay_zipper_ascend_until ||
        head_id == g_builtin_syms.lib_mork_overlay_zipper_ascend_until_branch ||
        head_id == g_builtin_syms.lib_mork_overlay_zipper_next_sibling_byte ||
        head_id == g_builtin_syms.lib_mork_overlay_zipper_prev_sibling_byte ||
        head_id == g_builtin_syms.lib_mork_overlay_zipper_next_step) {
        return mork_overlay_zipper_descend_simple_native(ctx, a, head, args, nargs,
                                                         head_id);
    }
    if (head_id == g_builtin_syms.lib_mork_overlay_zipper_descend_until_max_bytes) {
        return mork_overlay_zipper_descend_until_max_bytes_native(ctx, a, head, args, nargs);
    }
    return NULL;
}

static void library_mm2_program_free_resource(void *resource) {
    cetta_mork_bridge_program_free((CettaMorkProgramHandle *)resource);
}

static void library_mm2_context_free_resource(void *resource) {
    cetta_mork_bridge_context_free((CettaMorkContextHandle *)resource);
}

static void library_mork_cursor_free_resource(void *resource) {
    CettaMorkCursorResource *cursor_resource = (CettaMorkCursorResource *)resource;
    if (!cursor_resource) return;
    if (cursor_resource->cursor)
        cetta_mork_bridge_cursor_free(cursor_resource->cursor);
    free(cursor_resource);
}

static void library_mork_space_free_resource(void *resource) {
    CettaMorkSpaceResource *space_resource = (CettaMorkSpaceResource *)resource;
    if (!space_resource)
        return;
    if (space_resource->bridge_space)
        cetta_mork_bridge_space_free(space_resource->bridge_space);
    free(space_resource);
}

static void library_mork_product_cursor_free_resource(void *resource) {
    CettaMorkProductCursorResource *cursor_resource =
        (CettaMorkProductCursorResource *)resource;
    if (!cursor_resource) return;
    if (cursor_resource->cursor)
        cetta_mork_bridge_product_cursor_free(cursor_resource->cursor);
    free(cursor_resource);
}

static void library_mork_overlay_cursor_free_resource(void *resource) {
    CettaMorkOverlayCursorResource *cursor_resource =
        (CettaMorkOverlayCursorResource *)resource;
    if (!cursor_resource) return;
    if (cursor_resource->cursor)
        cetta_mork_bridge_overlay_cursor_free(cursor_resource->cursor);
    free(cursor_resource);
}

static Atom *library_mm2_bridge_error(Arena *a, Atom *head, Atom **args,
                                      uint32_t nargs, const char *prefix) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%s%s", prefix, cetta_mork_bridge_last_error());
    return atom_error(a, library_call_expr(a, head, args, nargs), atom_string(a, buf));
}

static Atom *mm2_program_new_native(CettaLibraryContext *ctx, Arena *a,
                                    Atom *head, Atom **args, uint32_t nargs) {
    uint64_t id = 0;
    CettaMorkProgramHandle *program;
    if (!system_zero_arg_ok(args, nargs)) {
        return library_signature_error(a, head, args, nargs,
                                       "expected: (__cetta_lib_mm2_program_new)");
    }
    if (!cetta_mork_bridge_is_available()) {
        return library_mm2_bridge_error(a, head, args, nargs,
                                        "MM2 MORK bridge unavailable: ");
    }
    program = cetta_mork_bridge_program_new();
    if (!program) {
        return library_mm2_bridge_error(a, head, args, nargs,
                                        "MM2 program allocation failed: ");
    }
    if (!cetta_native_handle_alloc(ctx, CETTA_MM2_PROGRAM_HANDLE_KIND, program,
                                   library_mm2_program_free_resource, &id)) {
        cetta_mork_bridge_program_free(program);
        return atom_error(a, library_call_expr(a, head, args, nargs),
                          atom_string(a, "MM2 program handle allocation failed"));
    }
    return cetta_native_handle_atom(a, CETTA_MM2_PROGRAM_HANDLE_KIND, id);
}

static Atom *mm2_program_clear_native(CettaLibraryContext *ctx, Arena *a,
                                      Atom *head, Atom **args, uint32_t nargs) {
    CettaMorkProgramHandle *program;
    if (nargs != 1) {
        return library_signature_error(a, head, args, nargs,
                                       "expected program handle");
    }
    program = library_mm2_program_handle(ctx, args[0]);
    if (!program) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-program handle");
    }
    if (!cetta_mork_bridge_program_clear(program)) {
        return library_mm2_bridge_error(a, head, args, nargs,
                                        "MM2 program clear failed: ");
    }
    return atom_unit(a);
}

static Atom *mm2_program_add_native(CettaLibraryContext *ctx, Arena *a,
                                    Atom *head, Atom **args, uint32_t nargs) {
    CettaMorkProgramHandle *program;
    char *text;
    if (nargs != 2) {
        return library_signature_error(a, head, args, nargs,
                                       "expected program handle and MM2 atom");
    }
    program = library_mm2_program_handle(ctx, args[0]);
    if (!program) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-program handle as first argument");
    }
    text = library_mm2_surface_text(a, args[1]);
    if (!cetta_mork_bridge_program_add_sexpr(program, (const uint8_t *)text,
                                             strlen(text), NULL)) {
        return library_mm2_bridge_error(a, head, args, nargs,
                                        "MM2 program add failed: ");
    }
    return atom_unit(a);
}

static Atom *mm2_load_file_native(CettaLibraryContext *ctx, Arena *a,
                                  Atom *head, Atom **args, uint32_t nargs) {
    CettaMorkProgramHandle *program;
    CettaMorkContextHandle *context;
    const char *path;
    char resolved[PATH_MAX];
    Arena parse_arena;
    Atom **atoms = NULL;
    int n = 0;
    Atom *error = NULL;

    if (nargs != 3 || !(path = library_text_arg(args[2]))) {
        return library_signature_error(a, head, args, nargs,
                                       "expected program handle, context handle, and MM2 filename");
    }
    program = library_mm2_program_handle(ctx, args[0]);
    context = library_mm2_context_handle(ctx, args[1]);
    if (!program || !context) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-program handle and mork-context handle");
    }
    if (!library_resolve_current_path(ctx, path, resolved, sizeof(resolved))) {
        return atom_error(a, library_call_expr(a, head, args, nargs),
                          atom_string(a, "MM2 file path resolution failed"));
    }

    arena_init(&parse_arena);
    arena_set_runtime_kind(&parse_arena, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    n = parse_metta_file(resolved, &parse_arena, &atoms);
    if (n < 0) {
        error = atom_error(a, library_call_expr(a, head, args, nargs),
                           atom_string(a, "MM2 file parse failed"));
        goto done;
    }

    cetta_mm2_lower_atoms(&parse_arena, atoms, n);
    if (cetta_mm2_atoms_have_top_level_eval(atoms, n)) {
        error = atom_error(a, library_call_expr(a, head, args, nargs),
                           atom_string(a, "MM2 file loader does not accept top-level ! forms"));
        goto done;
    }

    for (int i = 0; i < n; i++) {
        char *surface = cetta_mm2_atom_to_surface_string(&parse_arena, atoms[i]);
        bool ok;
        if (cetta_mm2_atom_is_exec_rule(atoms[i])) {
            ok = cetta_mork_bridge_program_add_sexpr(program,
                                                     (const uint8_t *)surface,
                                                     strlen(surface), NULL);
        } else {
            ok = cetta_mork_bridge_context_add_sexpr(context,
                                                     (const uint8_t *)surface,
                                                     strlen(surface), NULL);
        }
        if (!ok) {
            error = library_mm2_bridge_error(
                a, head, args, nargs,
                cetta_mm2_atom_is_exec_rule(atoms[i]) ?
                    "MM2 file load program add failed: " :
                    "MM2 file load context add failed: ");
            goto done;
        }
    }

done:
    free(atoms);
    arena_free(&parse_arena);
    if (error) return error;
    return atom_unit(a);
}

static Atom *mm2_program_size_native(CettaLibraryContext *ctx, Arena *a,
                                     Atom *head, Atom **args, uint32_t nargs) {
    CettaMorkProgramHandle *program;
    uint64_t size = 0;
    if (nargs != 1) {
        return library_signature_error(a, head, args, nargs,
                                       "expected program handle");
    }
    program = library_mm2_program_handle(ctx, args[0]);
    if (!program) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-program handle");
    }
    if (!cetta_mork_bridge_program_size(program, &size)) {
        return library_mm2_bridge_error(a, head, args, nargs,
                                        "MM2 program size failed: ");
    }
    return atom_int(a, (int64_t)size);
}

static Atom *mm2_program_atoms_native(CettaLibraryContext *ctx, Arena *a,
                                      Atom *head, Atom **args, uint32_t nargs) {
    CettaMorkProgramHandle *program;
    uint8_t *packet = NULL;
    size_t len = 0;
    uint32_t rows = 0;
    Atom *result;
    if (nargs != 1) {
        return library_signature_error(a, head, args, nargs,
                                       "expected program handle");
    }
    program = library_mm2_program_handle(ctx, args[0]);
    if (!program) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-program handle");
    }
    if (!cetta_mork_bridge_program_dump(program, &packet, &len, &rows)) {
        return library_mm2_bridge_error(a, head, args, nargs,
                                        "MM2 program dump failed: ");
    }
    result = library_atoms_from_text(a, packet, len, library_call_expr(a, head, args, nargs));
    cetta_mork_bridge_bytes_free(packet, len);
    (void)rows;
    return result;
}

static Atom *mm2_context_new_native(CettaLibraryContext *ctx, Arena *a,
                                    Atom *head, Atom **args, uint32_t nargs) {
    uint64_t id = 0;
    CettaMorkContextHandle *context;
    if (!system_zero_arg_ok(args, nargs)) {
        return library_signature_error(a, head, args, nargs,
                                       "expected: (__cetta_lib_mm2_context_new)");
    }
    if (!cetta_mork_bridge_is_available()) {
        return library_mm2_bridge_error(a, head, args, nargs,
                                        "MM2 MORK bridge unavailable: ");
    }
    context = cetta_mork_bridge_context_new();
    if (!context) {
        return library_mm2_bridge_error(a, head, args, nargs,
                                        "MM2 context allocation failed: ");
    }
    if (!cetta_native_handle_alloc(ctx, CETTA_MM2_CONTEXT_HANDLE_KIND, context,
                                   library_mm2_context_free_resource, &id)) {
        cetta_mork_bridge_context_free(context);
        return atom_error(a, library_call_expr(a, head, args, nargs),
                          atom_string(a, "MM2 context handle allocation failed"));
    }
    return cetta_native_handle_atom(a, CETTA_MM2_CONTEXT_HANDLE_KIND, id);
}

static Atom *mm2_context_clear_native(CettaLibraryContext *ctx, Arena *a,
                                      Atom *head, Atom **args, uint32_t nargs) {
    CettaMorkContextHandle *context;
    if (nargs != 1) {
        return library_signature_error(a, head, args, nargs,
                                       "expected context handle");
    }
    context = library_mm2_context_handle(ctx, args[0]);
    if (!context) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-context handle");
    }
    if (!cetta_mork_bridge_context_clear(context)) {
        return library_mm2_bridge_error(a, head, args, nargs,
                                        "MM2 context clear failed: ");
    }
    return atom_unit(a);
}

static Atom *mm2_context_load_program_native(CettaLibraryContext *ctx, Arena *a,
                                             Atom *head, Atom **args, uint32_t nargs) {
    CettaMorkContextHandle *context;
    CettaMorkProgramHandle *program;
    if (nargs != 2) {
        return library_signature_error(a, head, args, nargs,
                                       "expected context handle and program handle");
    }
    context = library_mm2_context_handle(ctx, args[0]);
    program = library_mm2_program_handle(ctx, args[1]);
    if (!context || !program) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-context handle and mork-program handle");
    }
    if (!cetta_mork_bridge_context_load_program(context, program, NULL)) {
        return library_mm2_bridge_error(a, head, args, nargs,
                                        "MM2 context load-program failed: ");
    }
    return atom_unit(a);
}

static Atom *mm2_context_add_like_native(CettaLibraryContext *ctx, Arena *a,
                                         Atom *head, Atom **args, uint32_t nargs,
                                         bool remove_mode) {
    CettaMorkContextHandle *context;
    char *text;
    bool ok;
    if (nargs != 2) {
        return library_signature_error(a, head, args, nargs,
                                       "expected context handle and MM2 atom");
    }
    context = library_mm2_context_handle(ctx, args[0]);
    if (!context) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-context handle as first argument");
    }
    text = library_mm2_surface_text(a, args[1]);
    if (remove_mode) {
        ok = cetta_mork_bridge_context_remove_sexpr(context, (const uint8_t *)text,
                                                    strlen(text), NULL);
    } else {
        ok = cetta_mork_bridge_context_add_sexpr(context, (const uint8_t *)text,
                                                 strlen(text), NULL);
    }
    if (!ok) {
        return library_mm2_bridge_error(
            a, head, args, nargs,
            remove_mode ? "MM2 context remove failed: " : "MM2 context add failed: ");
    }
    return atom_unit(a);
}

static Atom *mm2_context_step_native(CettaLibraryContext *ctx, Arena *a,
                                     Atom *head, Atom **args, uint32_t nargs) {
    CettaMorkContextHandle *context;
    uint64_t performed = 0;
    if (nargs != 1) {
        return library_signature_error(a, head, args, nargs,
                                       "expected context handle");
    }
    context = library_mm2_context_handle(ctx, args[0]);
    if (!context) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-context handle");
    }
    if (!cetta_mork_bridge_context_run(context, 1, &performed)) {
        return library_mm2_bridge_error(a, head, args, nargs,
                                        "MM2 context step failed: ");
    }
    return atom_int(a, (int64_t)performed);
}

static Atom *mm2_context_run_native(CettaLibraryContext *ctx, Arena *a,
                                    Atom *head, Atom **args, uint32_t nargs) {
    CettaMorkContextHandle *context;
    uint64_t steps = CETTA_MM2_DEFAULT_RUN_STEPS;
    uint64_t performed = 0;
    int parsed_steps = 0;
    if (nargs != 1 && nargs != 2) {
        return library_signature_error(a, head, args, nargs,
                                       "expected context handle and optional non-negative steps");
    }
    context = library_mm2_context_handle(ctx, args[0]);
    if (!context) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-context handle");
    }
    if (nargs == 2) {
        if (!library_int_arg(args[1], &parsed_steps) || parsed_steps < 0) {
            return library_signature_error(a, head, args, nargs,
                                           "expected non-negative step count");
        }
        steps = (uint64_t)parsed_steps;
    }
    if (!cetta_mork_bridge_context_run(context, steps, &performed)) {
        return library_mm2_bridge_error(a, head, args, nargs,
                                        "MM2 context run failed: ");
    }
    return atom_int(a, (int64_t)performed);
}

static Atom *mm2_context_size_native(CettaLibraryContext *ctx, Arena *a,
                                     Atom *head, Atom **args, uint32_t nargs) {
    CettaMorkContextHandle *context;
    uint64_t size = 0;
    if (nargs != 1) {
        return library_signature_error(a, head, args, nargs,
                                       "expected context handle");
    }
    context = library_mm2_context_handle(ctx, args[0]);
    if (!context) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-context handle");
    }
    if (!cetta_mork_bridge_context_size(context, &size)) {
        return library_mm2_bridge_error(a, head, args, nargs,
                                        "MM2 context size failed: ");
    }
    return atom_int(a, (int64_t)size);
}

static Atom *mm2_context_atoms_native(CettaLibraryContext *ctx, Arena *a,
                                      Atom *head, Atom **args, uint32_t nargs) {
    CettaMorkContextHandle *context;
    uint8_t *packet = NULL;
    size_t len = 0;
    uint32_t rows = 0;
    Atom *result;
    if (nargs != 1) {
        return library_signature_error(a, head, args, nargs,
                                       "expected context handle");
    }
    context = library_mm2_context_handle(ctx, args[0]);
    if (!context) {
        return library_signature_error(a, head, args, nargs,
                                       "expected mork-context handle");
    }
    if (!cetta_mork_bridge_context_dump(context, &packet, &len, &rows)) {
        return library_mm2_bridge_error(a, head, args, nargs,
                                        "MM2 context dump failed: ");
    }
    result = library_atoms_from_text(a, packet, len, library_call_expr(a, head, args, nargs));
    cetta_mork_bridge_bytes_free(packet, len);
    (void)rows;
    return result;
}

static Atom *cetta_library_dispatch_mm2(CettaLibraryContext *ctx, Arena *a,
                                        Atom *head, Atom **args, uint32_t nargs) {
    if (head->kind != ATOM_SYMBOL) return NULL;
    SymbolId head_id = head->sym_id;
    if (head_id == g_builtin_syms.lib_mm2_program_new) {
        return mm2_program_new_native(ctx, a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_mm2_program_clear) {
        return mm2_program_clear_native(ctx, a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_mm2_program_add) {
        return mm2_program_add_native(ctx, a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_mm2_load_file) {
        return mm2_load_file_native(ctx, a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_mm2_program_size) {
        return mm2_program_size_native(ctx, a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_mm2_program_atoms) {
        return mm2_program_atoms_native(ctx, a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_mm2_context_new) {
        return mm2_context_new_native(ctx, a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_mm2_context_clear) {
        return mm2_context_clear_native(ctx, a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_mm2_context_load_program) {
        return mm2_context_load_program_native(ctx, a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_mm2_context_add) {
        return mm2_context_add_like_native(ctx, a, head, args, nargs, false);
    }
    if (head_id == g_builtin_syms.lib_mm2_context_remove) {
        return mm2_context_add_like_native(ctx, a, head, args, nargs, true);
    }
    if (head_id == g_builtin_syms.lib_mm2_context_run) {
        return mm2_context_run_native(ctx, a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_mm2_context_step) {
        return mm2_context_step_native(ctx, a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_mm2_context_size) {
        return mm2_context_size_native(ctx, a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_mm2_context_atoms) {
        return mm2_context_atoms_native(ctx, a, head, args, nargs);
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

static Atom *module_reason(CettaLibraryContext *ctx, Arena *a,
                           const char *tag, const char *path) {
    char display[PATH_MAX];
    return atom_expr(a, (Atom *[]){
        atom_symbol(a, tag),
        atom_string(a, cetta_library_display_path(ctx, path, display, sizeof(display)))
    }, 2);
}

static Atom *module_reason_with_detail(CettaLibraryContext *ctx, Arena *a,
                                       const char *tag,
                                       const char *path, Atom *detail) {
    char display[PATH_MAX];
    return atom_expr(a, (Atom *[]){
        atom_symbol(a, tag),
        atom_string(a, cetta_library_display_path(ctx, path, display, sizeof(display))),
        detail
    }, 3);
}

static bool load_act_dump_text_into_space(CettaLibraryContext *ctx,
                                          const char *path,
                                          const uint8_t *bytes, size_t len,
                                          Space *target,
                                          Arena *persistent_arena,
                                          Arena *eval_arena,
                                          Atom **error_out) {
    char *text = cetta_malloc(len + 1);
    memcpy(text, bytes, len);
    text[len] = '\0';

    if (target && target->native.universe) {
        AtomId *atom_ids = NULL;
        int n = parse_metta_text_ids(text, target->native.universe, &atom_ids);
        if (n < 0) {
            free(text);
            *error_out = module_reason(ctx, eval_arena, "ModuleCompiledParseFailed", path);
            return false;
        }
        for (int i = 0; i < n; i++) {
            space_add_atom_id(target, atom_ids[i]);
        }
        free(atom_ids);
        free(text);
        return true;
    }

    Arena *dst = persistent_arena ? persistent_arena : eval_arena;
    size_t pos = 0;
    while (text[pos]) {
        size_t before = pos;
        Atom *atom = parse_sexpr(dst, text, &pos);
        if (!atom) {
            while (text[pos] && isspace((unsigned char)text[pos])) pos++;
            if (text[pos] == '\0')
                break;
            free(text);
            *error_out = module_reason(ctx, eval_arena, "ModuleCompiledParseFailed", path);
            return false;
        }
        if (pos == before) {
            free(text);
            *error_out = module_reason(ctx, eval_arena, "ModuleCompiledParseStuck", path);
            return false;
        }
        if (!space_admit_atom(target, dst, atom)) {
            free(text);
            *error_out = module_reason(ctx, eval_arena, "ModuleCompiledLoadFailed", path);
            return false;
        }
    }

    free(text);
    return true;
}

static bool load_module_act_file(CettaLibraryContext *ctx, const char *path,
                                 Space *work_space,
                                 Arena *eval_arena,
                                 Arena *persistent_arena,
                                 Atom **error_out) {
    CettaMorkSpaceHandle *bridge = NULL;
    Space imported;
    uint8_t *packet = NULL;
    size_t packet_len = 0;
    uint32_t rows = 0;
    bool imported_init = false;
    bool ok = false;

    if (space_is_ordered(work_space)) {
        *error_out = module_reason(ctx, eval_arena, "ModuleCompiledOrderedSpaceUnsupported", path);
        return false;
    }
    if (!cetta_mork_bridge_is_available()) {
        *error_out = module_reason_with_detail(
            ctx, eval_arena, "ModuleCompiledBridgeUnavailable", path,
            atom_string(eval_arena, cetta_mork_bridge_last_error()));
        return false;
    }
    bridge = cetta_mork_bridge_space_new();
    if (!bridge) {
        *error_out = module_reason_with_detail(
            ctx, eval_arena, "ModuleCompiledBridgeAllocationFailed", path,
            atom_string(eval_arena, cetta_mork_bridge_last_error()));
        return false;
    }
    if (!cetta_mork_bridge_space_load_act_file(
            bridge, (const uint8_t *)path, strlen(path), NULL)) {
        *error_out = module_reason_with_detail(
            ctx, eval_arena, "ModuleCompiledLoadFailed", path,
            atom_string(eval_arena, cetta_mork_bridge_last_error()));
        goto cleanup;
    }

    space_init_with_universe(
        &imported, work_space ? work_space->native.universe : NULL);
    imported_init = true;
    switch (space_match_backend_import_bridge_space(&imported, bridge, NULL)) {
    case SPACE_BRIDGE_IMPORT_OK:
        for (uint32_t i = 0, n = space_length(&imported); i < n; i++) {
            space_add_atom_id(work_space, space_get_atom_id_at(&imported, i));
        }
        break;
    case SPACE_BRIDGE_IMPORT_NEEDS_TEXT_FALLBACK:
        if (!cetta_mork_bridge_space_dump(bridge, &packet, &packet_len, &rows)) {
            *error_out = module_reason_with_detail(
                ctx, eval_arena, "ModuleCompiledDumpFailed", path,
                atom_string(eval_arena, cetta_mork_bridge_last_error()));
            goto cleanup;
        }
        if (!load_act_dump_text_into_space(ctx, path, packet, packet_len, work_space,
                                           persistent_arena, eval_arena, error_out)) {
            goto cleanup;
        }
        break;
    case SPACE_BRIDGE_IMPORT_ERROR:
    default:
        *error_out = module_reason_with_detail(
            ctx, eval_arena, "ModuleCompiledImportFailed", path,
            atom_string(eval_arena, "structural bridge import failed"));
        goto cleanup;
    }

    ok = true;

cleanup:
    cetta_mork_bridge_bytes_free(packet, packet_len);
    if (imported_init)
        space_free(&imported);
    cetta_mork_bridge_space_free(bridge);
    (void)rows;
    return ok;
}

static bool load_module_act_file_pathmap_materialized(CettaLibraryContext *ctx,
                                                      const char *path,
                                                      Space *target_space,
                                                      Arena *eval_arena,
                                                      Arena *persistent_arena,
                                                      Atom **error_out) {
    if (space_is_ordered(target_space)) {
        *error_out = module_reason(ctx, eval_arena, "ModuleCompiledOrderedSpaceUnsupported", path);
        return false;
    }
    if (space_match_backend_logical_len(target_space) != 0) {
        *error_out = module_reason(ctx, eval_arena, "ModuleCompiledAttachRequiresFreshSpace", path);
        return false;
    }
    const char *backend_reason =
        space_match_backend_unavailable_reason(SPACE_ENGINE_PATHMAP);
    if (backend_reason) {
        *error_out = module_reason_with_detail(
            ctx, eval_arena, "ModuleCompiledAttachBackendUnavailable", path,
            atom_string(eval_arena, backend_reason));
        return false;
    }
    if (!space_match_backend_try_set(target_space, SPACE_ENGINE_PATHMAP)) {
        *error_out = module_reason(ctx, eval_arena, "ModuleCompiledAttachBackendUnavailable", path);
        return false;
    }
    if (!load_module_act_file(ctx, path, target_space, eval_arena,
                              persistent_arena, error_out)) {
        return false;
    }
    return true;
}

static bool load_module_mm2_file(CettaLibraryContext *ctx, const char *path,
                                 Space *work_space,
                                 Arena *eval_arena,
                                 Arena *persistent_arena,
                                 Atom **error_out) {
    (void)work_space;
    (void)persistent_arena;

    if (error_out) {
        *error_out = module_reason_with_detail(
            ctx, eval_arena, "ModuleMm2LoadFailed", path,
            atom_string(eval_arena,
                        "generic import/include does not accept .mm2; use "
                        "(mork:include! <MorkSpace> spec)"));
    }
    return false;
}

static void skip_mork_include_whitespace_and_comments(const char *text, size_t *pos) {
    for (;;) {
        while (text[*pos] && isspace((unsigned char)text[*pos])) {
            (*pos)++;
        }
        if (text[*pos] == ';') {
            while (text[*pos] && text[*pos] != '\n') {
                (*pos)++;
            }
        } else {
            break;
        }
    }
}

static bool read_mork_include_text_file(const char *path, char **text_out) {
    FILE *f = fopen(path, "r");
    char *text = NULL;
    size_t cap = 4096;
    size_t nread = 0;

    *text_out = NULL;
    if (!f) {
        return false;
    }

    text = cetta_malloc(cap + 1);
    for (;;) {
        size_t need = cap - nread;
        size_t got = fread(text + nread, 1, need, f);
        nread += got;
        if (got < need) {
            if (ferror(f)) {
                fclose(f);
                free(text);
                return false;
            }
            if (feof(f)) {
                break;
            }
        }
        if (nread == cap) {
            cap *= 2;
            text = cetta_realloc(text, cap + 1);
        }
    }

    fclose(f);
    text[nread] = '\0';
    *text_out = text;
    return true;
}

static bool load_module_mm2_file_into_mork_bridge(CettaLibraryContext *ctx,
                                                  const char *path,
                                                  CettaMorkSpaceHandle *bridge,
                                                  Arena *eval_arena,
                                                  Arena *persistent_arena,
                                                  Atom **error_out) {
    Arena *parse_arena = persistent_arena ? persistent_arena : eval_arena;
    char *text = NULL;
    size_t pos = 0;
    bool ok = false;

    if (!bridge) {
        *error_out = module_reason(ctx, eval_arena, "ModuleMm2BridgeUnavailable", path);
        return false;
    }

    if (!read_mork_include_text_file(path, &text)) {
        *error_out = module_reason(ctx, eval_arena, "ModuleMm2ReadFailed", path);
        return false;
    }

    ok = true;
    for (;;) {
        skip_mork_include_whitespace_and_comments(text, &pos);
        if (!text[pos]) {
            break;
        }
        size_t start = pos;
        Atom *atom = parse_sexpr(parse_arena, text, &pos);
        if (!atom) {
            *error_out = module_reason(ctx, eval_arena, "ModuleMm2LoadFailed", path);
            ok = false;
            break;
        }
        if (atom_is_symbol_id(atom, g_builtin_syms.bang)) {
            *error_out = module_reason(ctx, eval_arena, "ModuleMm2LoadFailed", path);
            ok = false;
            break;
        }
        if (!cetta_mork_bridge_space_add_sexpr(
                bridge, (const uint8_t *)(text + start), pos - start, NULL)) {
            *error_out = module_reason_with_detail(
                ctx, eval_arena, "ModuleMm2BridgeLoadFailed", path,
                atom_string(eval_arena, cetta_mork_bridge_last_error()));
            ok = false;
            break;
        }
    }
    free(text);
    return ok;
}

static bool load_module_file(CettaLibraryContext *ctx, const char *path,
                             Space *logical_space, Space *work_space,
                             Arena *eval_arena,
                             Arena *persistent_arena, Registry *registry,
                             int fuel, Atom **error_out) {
    int slot = imported_file_lookup(ctx, logical_space, path);
    if (slot >= 0) {
        if (ctx->imported_files[slot].loading) {
            *error_out = module_reason(ctx, eval_arena, "ModuleImportCycle", path);
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
        prev_self = registry_lookup_id(registry, g_builtin_syms.self);
        registry_bind_id(registry, g_builtin_syms.self,
                         atom_space(persistent_arena, work_space));
    }

    Atom **atoms = NULL;
    AtomId *atom_ids = NULL;
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
                ctx, eval_arena, "ModuleForeignLoadFailed", path, *error_out);
        }
    } else if (format.kind == CETTA_MODULE_FORMAT_MORK_ACT) {
        ok = load_module_act_file(ctx, path, work_space, eval_arena,
                                  persistent_arena, error_out);
    } else if (format.kind == CETTA_MODULE_FORMAT_MM2) {
        ok = load_module_mm2_file(ctx, path, work_space, eval_arena,
                                  persistent_arena, error_out);
    } else {
        int n = cetta_language_parse_file_ids_for_session(
            &ctx->session, path, eval_arena,
            work_space ? work_space->native.universe : NULL, &atom_ids);
        if (n < 0) {
            ok = false;
            *error_out = module_reason(ctx, eval_arena, "ModuleParseFailed", path);
        } else {
            for (int i = 0; i < n; i++) {
                AtomId at_id = atom_ids[i];
                if (tu_kind(work_space->native.universe, at_id) == ATOM_SYMBOL &&
                    tu_sym(work_space->native.universe, at_id) == g_builtin_syms.bang &&
                    i + 1 < n) {
                    ResultSet rs;
                    result_set_init(&rs);
                    Atom *eval_form = term_universe_copy_atom(
                        work_space->native.universe,
                        persistent_arena ? persistent_arena : eval_arena,
                        atom_ids[i + 1]);
                    if (!eval_form) {
                        free(rs.items);
                        ok = false;
                        *error_out = module_reason(ctx, eval_arena,
                                                   "ModuleParseFailed", path);
                        break;
                    }
                    eval_top_with_registry(work_space, eval_arena, persistent_arena, registry,
                                           eval_form, &rs);
                    Atom *first_error = result_set_first_error(eval_arena, &rs);
                    bool stop_after_error = first_error != NULL || result_set_has_error(&rs);
                    free(rs.items);
                    eval_release_temporary_spaces();
                    if (stop_after_error) {
                        *error_out = first_error ? first_error :
                            module_reason(ctx, eval_arena, "ModuleInitError", path);
                        ok = false;
                        break;
                    }
                    i++;
                    continue;
                }
                space_add_atom_id(work_space, at_id);
            }
        }
    }

    if (registry) {
        if (prev_self) {
            registry_bind_id(registry, g_builtin_syms.self, prev_self);
        }
    }
    cetta_library_pop_dir(ctx);
    free(atoms);
    free(atom_ids);

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
    if (plan->format.kind == CETTA_MODULE_FORMAT_MORK_ACT &&
        plan->target_is_fresh &&
        !plan->transactional) {
        if (!load_module_act_file_pathmap_materialized(
                ctx, plan->canonical_path, plan->execution_target_space,
                eval_arena, persistent_arena, error_out)) {
            rollback_imported_files(ctx, imported_len_before);
            rollback_loaded_modules(ctx, loaded_len_before);
            return false;
        }
        return true;
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
    space_init_with_universe(entry->space,
                             cetta_library_space_universe(ctx, persistent_arena));
    entry->loading = true;

    bool ok = false;
    if (plan->format.kind == CETTA_MODULE_FORMAT_MORK_ACT) {
        ok = load_module_act_file_pathmap_materialized(
            ctx, plan->canonical_path, entry->space,
            eval_arena, persistent_arena, error_out);
    } else {
        ok = load_module_file(ctx, plan->canonical_path, entry->space, entry->space,
                              eval_arena, persistent_arena, registry, fuel, error_out);
    }
    entry->loading = false;
    if (!ok) {
        rollback_loaded_modules(ctx, loaded_len_before);
        return NULL;
    }
    return entry;
}

static const char *loaded_module_storage_name(const CettaLoadedModule *module) {
    if (!module) return "unknown";
    if (module->format.kind == CETTA_MODULE_FORMAT_MORK_ACT &&
        module->space &&
        space_match_backend_is_attached_compiled(module->space)) {
        return "attached-compiled";
    }
    return "materialized";
}

static AtomId library_inventory_symbol_id(Space *inventory, const char *name) {
    if (!inventory || !inventory->native.universe || !name)
        return CETTA_ATOM_ID_NONE;
    return tu_intern_symbol(
        inventory->native.universe, symbol_intern_cstr(g_symbols, name));
}

static AtomId library_inventory_string_id(Space *inventory, const char *value) {
    if (!inventory || !inventory->native.universe || !value)
        return CETTA_ATOM_ID_NONE;
    return tu_intern_string(inventory->native.universe, value);
}

static bool library_inventory_add_ids(Space *inventory, const AtomId *items,
                                      uint32_t nitems) {
    if (!inventory || !inventory->native.universe || !items || nitems == 0)
        return false;
    AtomId expr_id = tu_expr_from_ids(inventory->native.universe, items, nitems);
    if (expr_id == CETTA_ATOM_ID_NONE)
        return false;
    space_add_atom_id(inventory, expr_id);
    return true;
}

static bool library_inventory_try_add_symbol_fact2(Space *inventory,
                                                   const char *head,
                                                   const char *arg1) {
    AtomId items[2] = {
        library_inventory_symbol_id(inventory, head),
        library_inventory_symbol_id(inventory, arg1),
    };
    return items[0] != CETTA_ATOM_ID_NONE &&
           items[1] != CETTA_ATOM_ID_NONE &&
           library_inventory_add_ids(inventory, items, 2);
}

static bool library_inventory_try_add_symbol_fact3(Space *inventory,
                                                   const char *head,
                                                   const char *arg1,
                                                   const char *arg2) {
    AtomId items[3] = {
        library_inventory_symbol_id(inventory, head),
        library_inventory_symbol_id(inventory, arg1),
        library_inventory_symbol_id(inventory, arg2),
    };
    return items[0] != CETTA_ATOM_ID_NONE &&
           items[1] != CETTA_ATOM_ID_NONE &&
           items[2] != CETTA_ATOM_ID_NONE &&
           library_inventory_add_ids(inventory, items, 3);
}

static bool library_inventory_try_add_string_symbol_fact3(
    Space *inventory, const char *head, const char *arg1,
    const char *arg2) {
    AtomId items[3] = {
        library_inventory_symbol_id(inventory, head),
        library_inventory_string_id(inventory, arg1),
        library_inventory_symbol_id(inventory, arg2),
    };
    return items[0] != CETTA_ATOM_ID_NONE &&
           items[1] != CETTA_ATOM_ID_NONE &&
           items[2] != CETTA_ATOM_ID_NONE &&
           library_inventory_add_ids(inventory, items, 3);
}

static bool library_inventory_try_add_symbol_string_symbol_fact4(
    Space *inventory, const char *head, const char *arg1,
    const char *arg2, const char *arg3) {
    AtomId items[4] = {
        library_inventory_symbol_id(inventory, head),
        library_inventory_symbol_id(inventory, arg1),
        library_inventory_string_id(inventory, arg2),
        library_inventory_symbol_id(inventory, arg3),
    };
    return items[0] != CETTA_ATOM_ID_NONE &&
           items[1] != CETTA_ATOM_ID_NONE &&
           items[2] != CETTA_ATOM_ID_NONE &&
           items[3] != CETTA_ATOM_ID_NONE &&
           library_inventory_add_ids(inventory, items, 4);
}

static bool library_inventory_try_add_symbol_symbol_string_fact4(
    Space *inventory, const char *head, const char *arg1,
    const char *arg2, const char *arg3) {
    AtomId items[4] = {
        library_inventory_symbol_id(inventory, head),
        library_inventory_symbol_id(inventory, arg1),
        library_inventory_symbol_id(inventory, arg2),
        library_inventory_string_id(inventory, arg3),
    };
    return items[0] != CETTA_ATOM_ID_NONE &&
           items[1] != CETTA_ATOM_ID_NONE &&
           items[2] != CETTA_ATOM_ID_NONE &&
           items[3] != CETTA_ATOM_ID_NONE &&
           library_inventory_add_ids(inventory, items, 4);
}

static bool library_inventory_try_add_string_string_symbol_fact4(
    Space *inventory, const char *head, const char *arg1,
    const char *arg2, const char *arg3) {
    AtomId items[4] = {
        library_inventory_symbol_id(inventory, head),
        library_inventory_string_id(inventory, arg1),
        library_inventory_string_id(inventory, arg2),
        library_inventory_symbol_id(inventory, arg3),
    };
    return items[0] != CETTA_ATOM_ID_NONE &&
           items[1] != CETTA_ATOM_ID_NONE &&
           items[2] != CETTA_ATOM_ID_NONE &&
           items[3] != CETTA_ATOM_ID_NONE &&
           library_inventory_add_ids(inventory, items, 4);
}

static bool library_inventory_try_add_string_symbol_symbol_fact4(
    Space *inventory, const char *head, const char *arg1,
    const char *arg2, const char *arg3) {
    AtomId items[4] = {
        library_inventory_symbol_id(inventory, head),
        library_inventory_string_id(inventory, arg1),
        library_inventory_symbol_id(inventory, arg2),
        library_inventory_symbol_id(inventory, arg3),
    };
    return items[0] != CETTA_ATOM_ID_NONE &&
           items[1] != CETTA_ATOM_ID_NONE &&
           items[2] != CETTA_ATOM_ID_NONE &&
           items[3] != CETTA_ATOM_ID_NONE &&
           library_inventory_add_ids(inventory, items, 4);
}

static void library_inventory_add_fallback_atom(Space *inventory, Arena *dst,
                                                Atom *atom) {
    /* Inventory facts should still cross the normal admission seam even when
       a direct bottom-up helper bailed out. Keep raw add as the last resort. */
    if (!space_admit_atom(inventory, dst, atom))
        space_add(inventory, atom);
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
    space_init_with_universe(inventory, cetta_library_space_universe(ctx, dst));

    const char *profile_name = (ctx->session.profile && ctx->session.profile->name) ?
        ctx->session.profile->name : "unknown";
    if (!library_inventory_try_add_symbol_fact2(inventory, "module-profile",
                                                profile_name)) {
        library_inventory_add_fallback_atom(
            inventory, dst,
            atom_expr2(dst, atom_symbol(dst, "module-profile"),
                       atom_symbol(dst, profile_name)));
    }

    for (uint32_t i = 0; i < cetta_module_provider_count(); i++) {
        const CettaModuleProviderDescriptor *desc = cetta_module_provider_at(i);
        if (!desc) continue;
        const char *provider_name = desc->name;
        bool enabled = module_provider_visible(ctx, desc->kind);
        if (!library_inventory_try_add_symbol_fact3(
                inventory, "module-provider", provider_name,
                enabled ? "enabled" : "disabled")) {
            library_inventory_add_fallback_atom(
                inventory, dst,
                atom_expr3(dst, atom_symbol(dst, "module-provider"),
                           atom_symbol(dst, provider_name),
                           atom_symbol(dst, enabled ? "enabled" : "disabled")));
        }
        if (!library_inventory_try_add_symbol_fact3(
                inventory, "module-provider-implementation", provider_name,
                desc->implemented ? "implemented" : "deferred")) {
            library_inventory_add_fallback_atom(
                inventory, dst,
                atom_expr3(dst, atom_symbol(dst, "module-provider-implementation"),
                           atom_symbol(dst, provider_name),
                           atom_symbol(dst, desc->implemented ? "implemented" : "deferred")));
        }
        if (!library_inventory_try_add_symbol_fact3(
                inventory, "module-provider-transport", provider_name,
                desc->remote_source ? "remote" : "local")) {
            library_inventory_add_fallback_atom(
                inventory, dst,
                atom_expr3(dst, atom_symbol(dst, "module-provider-transport"),
                           atom_symbol(dst, provider_name),
                           atom_symbol(dst, desc->remote_source ? "remote" : "local")));
        }
        if (!library_inventory_try_add_symbol_fact3(
                inventory, "module-provider-cache-policy", provider_name,
                desc->cache_backed ? "cache-backed" : "no-cache")) {
            library_inventory_add_fallback_atom(
                inventory, dst,
                atom_expr3(dst, atom_symbol(dst, "module-provider-cache-policy"),
                           atom_symbol(dst, provider_name),
                           atom_symbol(dst, desc->cache_backed ? "cache-backed" : "no-cache")));
        }
        if (!library_inventory_try_add_symbol_fact3(
                inventory, "module-provider-locator-kind", provider_name,
                cetta_module_locator_kind_name(desc->locator_kind))) {
            library_inventory_add_fallback_atom(
                inventory, dst,
                atom_expr3(dst, atom_symbol(dst, "module-provider-locator-kind"),
                           atom_symbol(dst, provider_name),
                           atom_symbol(dst, cetta_module_locator_kind_name(desc->locator_kind))));
        }
        if (!library_inventory_try_add_symbol_fact3(
                inventory, "module-provider-update-policy", provider_name,
                desc->update_policy ? desc->update_policy : "unknown")) {
            library_inventory_add_fallback_atom(
                inventory, dst,
                atom_expr3(dst, atom_symbol(dst, "module-provider-update-policy"),
                           atom_symbol(dst, provider_name),
                           atom_symbol(dst, desc->update_policy ? desc->update_policy : "unknown")));
        }
        if (!library_inventory_try_add_symbol_fact3(
                inventory, "module-provider-revision-policy", provider_name,
                cetta_remote_revision_policy_name(desc->revision_policy))) {
            library_inventory_add_fallback_atom(
                inventory, dst,
                atom_expr3(dst, atom_symbol(dst, "module-provider-revision-policy"),
                           atom_symbol(dst, provider_name),
                           atom_symbol(dst, cetta_remote_revision_policy_name(desc->revision_policy))));
        }
    }

    for (uint32_t i = 0; i < ctx->module_mount_len; i++) {
        const CettaModuleMount *mount = &ctx->module_mounts[i];
        if (!module_mount_visible(ctx, mount)) continue;
        if (!library_inventory_try_add_symbol_string_symbol_fact4(
                inventory, "module-mount", mount->namespace_name,
                mount->root_path,
                cetta_module_provider_name(mount->provider_kind))) {
            Atom *elems[4] = {
                atom_symbol(dst, "module-mount"),
                atom_symbol(dst, mount->namespace_name),
                atom_string(dst, mount->root_path),
                atom_symbol(dst, cetta_module_provider_name(mount->provider_kind))
            };
            library_inventory_add_fallback_atom(inventory, dst,
                                                atom_expr(dst, elems, 4));
        }
        if (!library_inventory_try_add_symbol_string_symbol_fact4(
                inventory, "module-mount-source", mount->namespace_name,
                mount->source_locator,
                cetta_module_locator_kind_name(mount->locator_kind))) {
            Atom *source_fact[4] = {
                atom_symbol(dst, "module-mount-source"),
                atom_symbol(dst, mount->namespace_name),
                atom_string(dst, mount->source_locator),
                atom_symbol(dst, cetta_module_locator_kind_name(mount->locator_kind))
            };
            library_inventory_add_fallback_atom(inventory, dst,
                                                atom_expr(dst, source_fact, 4));
        }
        if (!library_inventory_try_add_symbol_symbol_string_fact4(
                inventory, "module-mount-revision-policy",
                mount->namespace_name,
                cetta_remote_revision_policy_name(mount->revision_policy),
                mount->revision_value[0] ? mount->revision_value : "none")) {
            Atom *revision_fact[4] = {
                atom_symbol(dst, "module-mount-revision-policy"),
                atom_symbol(dst, mount->namespace_name),
                atom_symbol(dst, cetta_remote_revision_policy_name(mount->revision_policy)),
                atom_string(dst, mount->revision_value[0] ? mount->revision_value : "none")
            };
            library_inventory_add_fallback_atom(inventory, dst,
                                                atom_expr(dst, revision_fact, 4));
        }
    }

    for (uint32_t i = 0; i < ctx->loaded_module_len; i++) {
        const CettaLoadedModule *module = &ctx->loaded_modules[i];
        if (!loaded_module_visible(ctx, module)) continue;
        if (!library_inventory_try_add_string_string_symbol_fact4(
                inventory, "loaded-module", module->display_name,
                module->canonical_path,
                cetta_module_provider_name(module->provider_kind))) {
            Atom *module_fact[4] = {
                atom_symbol(dst, "loaded-module"),
                atom_string(dst, module->display_name),
                atom_string(dst, module->canonical_path),
                atom_symbol(dst, cetta_module_provider_name(module->provider_kind))
            };
            library_inventory_add_fallback_atom(inventory, dst,
                                                atom_expr(dst, module_fact, 4));
        }
        if (!library_inventory_try_add_string_symbol_symbol_fact4(
                inventory, "loaded-module-format", module->display_name,
                cetta_module_format_name(module->format.kind),
                cetta_foreign_backend_name(module->format.foreign_backend))) {
            Atom *format_fact[4] = {
                atom_symbol(dst, "loaded-module-format"),
                atom_string(dst, module->display_name),
                atom_symbol(dst, cetta_module_format_name(module->format.kind)),
                atom_symbol(dst, cetta_foreign_backend_name(module->format.foreign_backend))
            };
            library_inventory_add_fallback_atom(inventory, dst,
                                                atom_expr(dst, format_fact, 4));
        }
        if (!library_inventory_try_add_string_symbol_fact3(
                inventory, "loaded-module-storage", module->display_name,
                loaded_module_storage_name(module))) {
            Atom *storage_fact[3] = {
                atom_symbol(dst, "loaded-module-storage"),
                atom_string(dst, module->display_name),
                atom_symbol(dst, loaded_module_storage_name(module))
            };
            library_inventory_add_fallback_atom(inventory, dst,
                                                atom_expr(dst, storage_fact, 3));
        }
        if (module->space) {
            Atom *space_fact[4] = {
                atom_symbol(dst, "loaded-module-space"),
                atom_string(dst, module->display_name),
                atom_space(dst, module->space),
                atom_symbol(dst, cetta_module_provider_name(module->provider_kind))
            };
            /* atom_space is intentionally unstable and cannot enter the
               canonical store as a byte-backed term. */
            space_add(inventory, atom_expr(dst, space_fact, 4));
        }
    }

    return atom_space(dst, inventory);
}

bool cetta_library_include_module(CettaLibraryContext *ctx, const char *spec,
                                  Space *space, Arena *eval_arena,
                                  Arena *persistent_arena, Registry *registry,
                                  int fuel, Atom **error_out) {
    if (spec && (cetta_library_lookup(spec) || cetta_native_module_lookup(spec))) {
        return cetta_library_import(ctx, spec, space, eval_arena,
                                    persistent_arena, registry, fuel, error_out);
    }
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
                     cetta_library_relative_base_dir(ctx), path);
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
    if (ctx->active_mask & CETTA_LIBRARY_MORK) {
        Atom *result = cetta_library_dispatch_mork(ctx, a, head, args, nargs);
        if (result) return result;
    }
    {
        Atom *result = cetta_library_dispatch_mm2(ctx, a, head, args, nargs);
        if (result) return result;
    }
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

#define _GNU_SOURCE
#include "library.h"
#include "expand_user.h"
#include "errno_names.h"
#include "effect_policy.h"

#include "eval.h"
#include "mm2_lower.h"
#include "mork_space_bridge_runtime.h"
#include "native/native_modules.h"
#include "nik_runtime.h"
#include "parser.h"
#include "petta_search_machine.h"
#include "petta_runtime.h"
#include "petta_typecheck.h"
#include "petta_typecheck_v3.h"
#include "prime_scoped_judgments.h"
#include "prime_semantics.h"
#include "rhocalc_core.h"
#include "rhocalc_syntax.h"
#include "rule_machine.h"
#include "stats.h"
#include "text_source.h"
#include "langdef_pack.h"
#include "library_io.h"
#include "library_cwp.h"
#include "library_space_service.h"
#include "library_store.h"
#include "library_math.h"
#include "library_proc.h"
#include "proc_child.h"
#include "proc_capture.h"
#include "library_fs.h"
#include "library_postings.h"
#include "library_tensor.h"
#include "library_crypto.h"
#include "library_toml.h"
#include "library_group.h"
#include "library_re.h"
#include "library_proc_run.h"
#include "library_sqlite.h"
#include "library_durable.h"
#include "library_json.h"
#include "str_natives.h"
#include "utf8.h"
#include <math.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#ifndef CETTA_BUILD_WITH_JSON_GSLT
#define CETTA_BUILD_WITH_JSON_GSLT 0
#endif

typedef struct {
    uint8_t *bytes;
    size_t length;
} CettaModuleSource;

/* Reserve refusal ownership before initialization executes external effects.
 * he-compat remembers no failure: its records last only while an attempt and
 * the attempts inside it are running. */
typedef struct CettaHEModuleFailure {
    struct CettaHEModuleFailure *next;
    struct CettaHEModuleFailure *initialization_parent;
    uint64_t importer_instance;
    CettaModuleProviderKind provider;
    CettaModuleFormat format;
    bool initializing;
    bool completed;
    CettaModuleSource source;
    Arena storage;
    Atom *reason;
    char path[PATH_MAX];
    char name[];
} CettaHEModuleFailure;

/* One importer's request for the names of a Python-provided module. Linking a
 * module does not name it: each importer runs the register functions once. */
typedef struct CettaHEProviderExport {
    struct CettaHEProviderExport *next;
    uint64_t importer_instance;
    bool naming;                /* its register functions are running */
    bool named;
    Arena storage;              /* owns a remembered refusal */
    Atom *refusal;
} CettaHEProviderExport;

static void library_he_provider_exports_free(CettaHEProviderExport *record) {
    while (record) {
        CettaHEProviderExport *next = record->next;
        arena_free(&record->storage);
        free(record);
        record = next;
    }
}

static void library_he_failure_free(CettaHEModuleFailure *failure) {
    free(failure->source.bytes);
    arena_free(&failure->storage);
    free(failure);
}

static void library_he_failures_free(CettaHEModuleFailure *failure) {
    while (failure) {
        CettaHEModuleFailure *next = failure->next;
        library_he_failure_free(failure);
        failure = next;
    }
}

void cetta_library_he_run_begin(CettaLibraryContext *ctx,
                                CettaHEModuleRunScope *scope) {
    *scope = (CettaHEModuleRunScope){0};
    if (!ctx || !cetta_language_uses_rust_he_compat_semantics(
            ctx->session.language_id, ctx->session.profile))
        return;
    scope->context = ctx;
    scope->previous = ctx->he_module_run_scope;
    ctx->he_module_run_scope = scope;
}

void cetta_library_he_run_end(CettaHEModuleRunScope *scope) {
    if (!scope || !scope->context)
        return;
    scope->context->he_module_run_scope = scope->previous;
    library_he_failures_free(scope->failures);
    *scope = (CettaHEModuleRunScope){0};
}

enum {
    CETTA_LIBRARY_SYSTEM = 1u << 0,
    CETTA_LIBRARY_FS = 1u << 1,
    CETTA_LIBRARY_STR = 1u << 2,
    CETTA_LIBRARY_LTS = 1u << 3,
    CETTA_LIBRARY_MORK = 1u << 4,
    CETTA_LIBRARY_RHO = 1u << 5,
    CETTA_LIBRARY_RHOMETTA = 1u << 6,
    CETTA_LIBRARY_PETTA_IMPORT = 1u << 7,
    CETTA_LIBRARY_LIB_PROLOG = 1u << 8,
    CETTA_LIBRARY_PETTA_MEMO = 1u << 9,
    CETTA_LIBRARY_IO = 1u << 10,
    CETTA_LIBRARY_DURABLE = 1u << 13,
    CETTA_LIBRARY_CWP = 1u << 14,
    CETTA_LIBRARY_PROC = 1u << 15,
    CETTA_LIBRARY_LLM = 1u << 16,
    CETTA_LIBRARY_SPACE_SERVICE = 1u << 17,
    CETTA_LIBRARY_PETTA_TABLING = 1u << 11,
    CETTA_LIBRARY_MATH = 1u << 18,
    CETTA_LIBRARY_MAILBOX = 1u << 19,
    CETTA_LIBRARY_SQLITE = 1u << 20,
    CETTA_LIBRARY_SIWC = 1u << 21,
    CETTA_LIBRARY_RE = 1u << 22,
    CETTA_LIBRARY_POSTINGS = 1u << 23,
    CETTA_LIBRARY_TENSOR = 1u << 24,
#if CETTA_BUILD_WITH_JSON_GSLT
    CETTA_LIBRARY_JSON = 1u << 12
#endif
};
/* Native modules and the store reserve the lower 44 import bits. */
#define CETTA_LIBRARY_CRYPTO (UINT64_C(1) << 44)
#define CETTA_LIBRARY_TOML (UINT64_C(1) << 45)

#define CETTA_LIBRARY_STORE (UINT64_C(1) << 36)

_Static_assert((CETTA_NATIVE_IMPORT_GPARSE &
                (((UINT64_C(1) << 25) - 1) | CETTA_LIBRARY_STORE |
                 CETTA_LIBRARY_CRYPTO | CETTA_LIBRARY_TOML)) == 0,
               "native grammar import aliases the shared library catalog");
_Static_assert((CETTA_LIBRARY_TOML & CETTA_LIBRARY_STORE) == 0,
               "TOML and native store imports must be independent");

static bool library_prime_host_intrinsic(SymbolId head) {
    return cetta_prime_host_intrinsic_head(head) ||
           eval_native_weight_head(head);
}

typedef struct {
    const char *name;
    CettaLibraryMask bit;
    const char *he_words;
} CettaLibrarySpec;

static const CettaLibrarySpec CETTA_LIBRARIES[] = {
    {"system", CETTA_LIBRARY_SYSTEM, NULL},
    {"fs", CETTA_LIBRARY_FS, NULL},
    {"str", CETTA_LIBRARY_STR, NULL},
    {"re", CETTA_LIBRARY_RE, NULL},
    {"postings", CETTA_LIBRARY_POSTINGS, NULL},
    {"tensor", CETTA_LIBRARY_TENSOR, NULL},
    {"crypto", CETTA_LIBRARY_CRYPTO, NULL},
    {"toml", CETTA_LIBRARY_TOML, NULL},
    {"lts", CETTA_LIBRARY_LTS, NULL},
    {"mork", CETTA_LIBRARY_MORK, NULL},
    {"rho", CETTA_LIBRARY_RHO, NULL},
    {"math", CETTA_LIBRARY_MATH, "he/math"},
    {"rhometta", CETTA_LIBRARY_RHOMETTA, NULL},
    {"lib_import", CETTA_LIBRARY_PETTA_IMPORT, NULL},
    {"lib_prolog", CETTA_LIBRARY_LIB_PROLOG, NULL},
    {"lib_memo", CETTA_LIBRARY_PETTA_MEMO, NULL},
    {"lib_tabling", CETTA_LIBRARY_PETTA_TABLING, NULL},
    {"io", CETTA_LIBRARY_IO, NULL},
    {"durable", CETTA_LIBRARY_DURABLE, NULL},
    {"cwp", CETTA_LIBRARY_CWP, NULL},
    {"proc", CETTA_LIBRARY_PROC, NULL},
    {"llm", CETTA_LIBRARY_LLM, NULL},
    {"space_service", CETTA_LIBRARY_SPACE_SERVICE, NULL},
    {"store", CETTA_LIBRARY_STORE, NULL},
    {"mailbox", CETTA_LIBRARY_MAILBOX, NULL},
    {"sqlite", CETTA_LIBRARY_SQLITE, NULL},
    {"siwc", CETTA_LIBRARY_SIWC, NULL},
#if CETTA_BUILD_WITH_JSON_GSLT
    {"json", CETTA_LIBRARY_JSON, NULL},
#endif
};

static bool library_catalog_valid(void) {
    CettaLibraryMask mask = 0;
    for (size_t i = 0; i < sizeof(CETTA_LIBRARIES) / sizeof(CETTA_LIBRARIES[0]); ++i) {
        CettaLibraryMask bit = CETTA_LIBRARIES[i].bit;
        if (!bit || (bit & (bit - 1)) || (mask & bit))
            return false;
        mask |= bit;
    }
    return cetta_native_module_catalog_valid(mask);
}

/* These namespaces dispatch through structural mechanisms, without a
 * separate mutable runtime owner. Availability is not permission: the
 * retained caller still checks every resolved operation before invocation. */
static const CettaLibraryMask PRIME_SNAPSHOT_MECHANISMS =
    CETTA_LIBRARY_STR | CETTA_LIBRARY_LTS | CETTA_LIBRARY_RHO | CETTA_LIBRARY_RHOMETTA;

static bool library_prime_snapshot_common(const CettaLibraryContext *ctx) {
    if (!ctx || ctx->session.language_id!=CETTA_LANGUAGE_PRIME ||
        ctx->session.profile!=cetta_profile_prime_default() ||
        ctx->session.test_verdict || ctx->session.test_verdict_context ||
        ctx->session.process_control.exit_requested || !ctx->petta_program ||
        ctx->nik_runtime || (ctx->active_mask & ~PRIME_SNAPSHOT_MECHANISMS) ||
        ctx->petta_trusted_library_import_depth || ctx->import_space_alias_len ||
        ctx->module_token_scope || ctx->he_module_run_scope || ctx->he_module_failures ||
        ctx->top_module_space || ctx->top_module_registry || ctx->module_own_view_len ||
        ctx->retired_module_len || ctx->petta_translator_rules ||
        ctx->petta_translator_rule_len || ctx->petta_translator_rule_cap ||
        ctx->petta_tabled_relations || ctx->petta_tabled_relation_len || ctx->petta_tabled_relation_cap ||
        ctx->petta_memo.all_arities || ctx->petta_memo.all_arity_len || ctx->petta_memo.all_arity_cap ||
        ctx->petta_memo.exact_arities || ctx->petta_memo.exact_arity_len || ctx->petta_memo.exact_arity_cap ||
        ctx->petta_shared_table || ctx->petta_open_programs || ctx->petta_open_programs_free ||
        ctx->petta_match_decisions || ctx->petta_match_decisions_free ||
        ctx->petta_static_imports || ctx->petta_static_imports_free ||
        ctx->petta_runtime || ctx->petta_typecheck_v3 || ctx->petta_token_space_clause_registry ||
        ctx->io_runtime || ctx->durable_runtime || ctx->json_runtime)
        return false;
    for (size_t i=0u;i<CETTA_PETTA_MEMO_CONTROL_COUNT;i++)
        if (ctx->petta_memo.imported_controls[i]) return false;
    return true;
}

bool cetta_library_context_prime_snapshot_source_admitted(const CettaLibraryContext *source) {
    /* Search paths and completed import inventories affect future loading,
     * which this source profile does not execute. The caller captures and
     * compares the actual linked code, values and resolved-call meanings. */
    return library_prime_snapshot_common(source);
}

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

static bool library_read_monotonic_ns(uint64_t *out) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return false;
    *out = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
    return true;
}

static uint64_t library_monotonic_ns(void) {
    uint64_t ns = 0;
    (void)library_read_monotonic_ns(&ns);
    return ns;
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
    if (extra > SIZE_MAX - sb->len - 1) abort();
    size_t need = sb->len + extra + 1;
    if (need <= sb->cap) return;
    size_t next = sb->cap ? (sb->cap <= SIZE_MAX / 2 ? sb->cap * 2 : need) : 64;
    while (next < need) {
        if (next > SIZE_MAX / 2) { next = need; break; }
        next *= 2;
    }
    sb->buf = cetta_realloc(sb->buf, next);
    sb->cap = next;
}

static void cetta_sb_append_n(CettaStringBuf *sb, const char *s, size_t n) {
    cetta_sb_ensure(sb, n);
    if (n) memcpy(sb->buf + sb->len, s, n);
    sb->len += n;
    sb->buf[sb->len] = '\0';
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
    for (CettaIndex i = 0; i < item_count; i++) {
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
        if (expr_len > UINT32_MAX) {
            free(expr_bytes);
            cetta_sb_free(&packet);
            if (error_out)
                *error_out = "MORK expr-byte packet item too large";
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
    CettaCount item_count,
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
    for (CettaIndex i = 0; i < item_count; i++) {
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
        if (expr_len > UINT32_MAX) {
            free(expr_bytes);
            cetta_sb_free(&packet);
            if (error_out)
                *error_out = "MORK expr-byte packet item too large";
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

static bool library_atom_expr_len_checked(CettaCount len, CettaExprLen *out_len) {
    if (!cetta_expr_len_mul_fits_size(len, sizeof(Atom *)))
        return false;
    if (out_len)
        *out_len = (CettaExprLen)len;
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

static const char *library_he_words(const CettaLibraryContext *ctx,
                                    CettaModuleProviderKind provider,
                                    const char *name) {
    if (ctx->session.language_id != CETTA_LANGUAGE_HE ||
        provider != CETTA_MODULE_PROVIDER_STDLIB_FILE)
        return NULL;
    const CettaLibrarySpec *spec = cetta_library_lookup(name);
    return spec ? spec->he_words : NULL;
}

void cetta_library_context_init(CettaLibraryContext *ctx) {
    cetta_library_context_init_for_language_profile(ctx, CETTA_LANGUAGE_HE, NULL);
}

static void library_context_init_services(CettaLibraryContext *ctx,
        CettaLanguageId language_id, const CettaProfile *profile,
        bool foreign_services) {
    if (!library_catalog_valid()) {
        fputs("cetta: conflicting library import bits\n", stderr);
        abort();
    }
    cetta_eval_session_init(&ctx->session, language_id, profile);
    ctx->nik_runtime_mutex_ready =
        pthread_mutex_init(&ctx->nik_runtime_mutex, NULL) == 0;
    ctx->nik_runtime = NULL;
    ctx->parser_cache_mutex_ready =
        pthread_mutex_init(&ctx->parser_cache_mutex, NULL) == 0;
    ctx->parser_cache = NULL;
    term_universe_init(&ctx->term_universe);
    ctx->active_mask = 0;
    ctx->root_dir[0] = '\0';
    if (!getcwd(ctx->working_dir, sizeof(ctx->working_dir))) {
        ctx->working_dir[0] = '\0';
    }
    ctx->script_path[0] = '\0';
    ctx->script_dir[0] = '\0';
    ctx->import_dirs = NULL;
    ctx->import_dir_len = ctx->import_dir_cap = 0u;
    ctx->module_mounts = NULL;
    ctx->module_mount_len = ctx->module_mount_cap = 0u;
    memset(&ctx->petta_library_paths, 0, sizeof(ctx->petta_library_paths));
    ctx->petta_library_paths.revision = 1u;
    ctx->imported_files = NULL;
    ctx->imported_file_len = ctx->imported_file_cap = 0u;
    ctx->petta_trusted_library_import_depth = 0u;
    ctx->import_space_aliases = NULL;
    ctx->import_space_alias_len = ctx->import_space_alias_cap = 0u;
    ctx->he_module_aliases = NULL;
    ctx->he_module_alias_len = ctx->he_module_alias_cap = 0u;
    ctx->include_fault_keeps_prefix = false;
    ctx->cmdline_arg_len = 0;
    ctx->loaded_modules = NULL;
    ctx->loaded_module_len = ctx->loaded_module_cap = 0u;
    ctx->module_token_scope = NULL;
    ctx->he_module_failures = NULL;
    ctx->he_module_run_scope = NULL;
    ctx->top_module_space = NULL;
    ctx->top_module_registry = NULL;
    ctx->module_own_views = NULL;
    ctx->module_own_view_len = ctx->module_own_view_cap = 0u;
    ctx->retired_modules = NULL;
    ctx->retired_module_len = 0u;
    ctx->retired_module_cap = 0u;
    ctx->native_handle_registry = NULL;
    ctx->native_handle_next_id = 1;
    ctx->petta_translator_rules = NULL;
    ctx->petta_translator_rule_len = 0u;
    ctx->petta_translator_rule_cap = 0u;
    ctx->petta_translator_symbol_table_instance =
        symbol_table_instance_id(g_symbols);
    ctx->petta_translator_rule_revision = 1u;
    ctx->petta_admission_revision = 1u;
    ctx->petta_tabled_relations = NULL;
    ctx->petta_tabled_relation_len = 0u;
    ctx->petta_tabled_relation_cap = 0u;
    ctx->petta_tabled_symbol_table_instance =
        symbol_table_instance_id(g_symbols);
    memset(&ctx->petta_memo, 0, sizeof(ctx->petta_memo));
    ctx->petta_memo.symbol_table_instance =
        symbol_table_instance_id(g_symbols);
    ctx->petta_memo.strategy =
        CETTA_PETTA_MEMO_STRATEGY_WTINYLFU;
    ctx->petta_memo.unique_limit = 100u;
    ctx->petta_memo.size_limit_bytes = UINT64_C(5368709120);
    ctx->petta_memo.float_precision = 12u;
    ctx->petta_memo.answer_limit = 2048u;
    ctx->petta_memo.aggregate =
        CETTA_PETTA_MEMO_AGGREGATE_NONE;
    ctx->petta_shared_table = language_id == CETTA_LANGUAGE_PETTA
        ? petta_machine_table_new() : NULL;
    ctx->petta_open_programs = NULL;
    ctx->petta_open_programs_free = NULL;
    ctx->petta_match_decisions = NULL;
    ctx->petta_match_decisions_free = NULL;
    ctx->petta_static_imports = NULL;
    ctx->petta_static_imports_free = NULL;
    ctx->prime_hol_guest = NULL;
    ctx->prime_hol_guest_free = NULL;
    ctx->prime_lf_guest = NULL;
    ctx->prime_lf_guest_free = NULL;
    ctx->prime_standard_library = NULL;
    ctx->prime_standard_library_free = NULL;
    if (ctx->petta_shared_table) {
        PettaTableMutationPolicy table_policy =
            profile && profile->enable_cetta_extensions
                ? PETTA_TABLE_MUTATION_REVISION_GUARDED
                : PETTA_TABLE_MUTATION_STATIC_WORLD;
        (void)petta_machine_table_set_mutation_policy(
            ctx->petta_shared_table, table_policy);
    }
    /* Prime's guarded relational plan is the default execution strategy.
       The environment switch exists only so the differential gate can retain
       the canonical evaluator as an executable reference implementation. */
    ctx->prime_relational_plan_enabled =
        language_id == CETTA_LANGUAGE_PRIME &&
        getenv("CETTA_PRIME_RELATIONAL_PLAN_REFERENCE") == NULL;
    ctx->petta_runtime = language_id == CETTA_LANGUAGE_PETTA
        ? cetta_petta_runtime_state_new() : NULL;
    CettaSearchControllerPolicy requested_controller;
    const char *controller = getenv("CETTA_SEARCH_CONTROLLER");
    bool portable_relational_control_requested =
        cetta_language_portable_relational_contract(language_id) !=
            CETTA_PORTABLE_RELATIONAL_NONE &&
        controller &&
        cetta_search_controller_policy_parse(
            controller, &requested_controller);
    /* HE's relations run on the compiled open-equation machine under HE's
     * rules, which compiles them from this program's occurrence plans. */
    bool needs_occurrence_program =
        language_id == CETTA_LANGUAGE_PETTA ||
        language_id == CETTA_LANGUAGE_HE ||
        ctx->prime_relational_plan_enabled ||
        portable_relational_control_requested;
    bool native_weight_profile = false;
    native_weight_profile = language_id == CETTA_LANGUAGE_PRIME ||
        (profile && (profile->id == CETTA_PROFILE_PETTA_EXTENDED ||
                     profile->id == CETTA_PROFILE_HE_EXTENDED ||
                     cetta_profile_uses_petta_typing(profile)));
    ctx->petta_program = needs_occurrence_program
        ? petta_program_new_with_rules_and_host_intrinsics(
            CETTA_RULE_SYNTAX_ORDINARY |
            ((profile && (profile->id == CETTA_PROFILE_PETTA_EXTENDED ||
                          profile->id == CETTA_PROFILE_HE_EXTENDED))
                ? CETTA_RULE_SYNTAX_DIRECTIONAL : 0u),
            language_id == CETTA_LANGUAGE_PRIME
                ? (native_weight_profile ? library_prime_host_intrinsic
                                         : cetta_prime_host_intrinsic_head)
                : (native_weight_profile ? eval_native_weight_head : NULL))
        : NULL;
    if (ctx->petta_program && native_weight_profile)
        petta_program_enable_graded_equations(ctx->petta_program);
    const CettaLanguageVersion *program_version =
        cetta_language_version_of(language_id, profile);
    /* HE resolves a call's head when the call runs, and PeTTa's typed line
     * does too: a plan compiled before a head had equations is rebuilt when
     * the heads it read change.  PeTTa otherwise compiles an equation
     * against the program as it stood when the equation was defined. */
    if (ctx->petta_program &&
        (language_id == CETTA_LANGUAGE_HE ||
         (language_id == CETTA_LANGUAGE_PETTA &&
          cetta_language_version_petta_typed_line(program_version))))
        (void)petta_program_enable_callability_refresh(ctx->petta_program);
    if (ctx->petta_program && cetta_profile_uses_petta_typing(profile)) {
        (void)petta_program_enable_analysis(ctx->petta_program);
    }
    ctx->petta_typecheck_v3 = NULL;
    if (profile && profile->id == CETTA_PROFILE_PETTA_TYPECHECK_V3) {
        char error[256] = {0};
        ctx->petta_typecheck_v3 =
            cetta_petta_typecheck_v3_create(error, sizeof error);
    }
    ctx->petta_token_space_clause_registry =
        language_id == CETTA_LANGUAGE_PETTA
            ? cetta_petta_token_space_clause_registry_new()
            : NULL;
    ctx->lib_prolog = foreign_services ? cetta_lib_prolog_runtime_new() : NULL;
    if (ctx->lib_prolog && ctx->working_dir[0] != '\0') {
        (void)cetta_lib_prolog_runtime_set_working_dir(
            ctx->lib_prolog, ctx->working_dir);
    }
    ctx->foreign_runtime = foreign_services ? cetta_foreign_runtime_new() : NULL;
    ctx->io_runtime = NULL;
    ctx->durable_runtime = NULL;
    ctx->json_runtime = NULL;
}

void cetta_library_context_init_for_language_profile(CettaLibraryContext *ctx,
        CettaLanguageId language_id, const CettaProfile *profile) {
    library_context_init_services(ctx, language_id, profile, true);
}

bool cetta_library_context_init_prime_snapshot(CettaLibraryContext *ctx) {
    if (!ctx) return false;
    HashConsTable *previous_hashcons = g_hashcons;
    g_hashcons = NULL;
    library_context_init_services(ctx, CETTA_LANGUAGE_PRIME,
        cetta_profile_prime_default(), false);
    g_hashcons = previous_hashcons;
    return ctx->nik_runtime_mutex_ready && ctx->petta_program != NULL;
}

bool cetta_library_context_init_prime_snapshot_from(CettaLibraryContext *ctx,
        const CettaLibraryContext *source) {
    if (!ctx || !source || ctx==source) return false;
    bool initialized=cetta_library_context_init_prime_snapshot(ctx);
    if (!initialized || !cetta_library_context_prime_snapshot_source_admitted(source)) return false;
    ctx->session=source->session;
    ctx->active_mask=source->active_mask;
    ctx->prime_relational_plan_enabled=source->prime_relational_plan_enabled;
    memcpy(ctx->root_dir,source->root_dir,sizeof(ctx->root_dir));
    memcpy(ctx->working_dir,source->working_dir,sizeof(ctx->working_dir));
    memcpy(ctx->script_path,source->script_path,sizeof(ctx->script_path));
    memcpy(ctx->script_dir,source->script_dir,sizeof(ctx->script_dir));
    return true;
}

void cetta_library_context_free(CettaLibraryContext *ctx) {
    if (!ctx) return;
    /* Captured native work may borrow session resources and program views.
     * Retire it before any of those dependencies, including loaded spaces. */
    cetta_native_handle_cleanup_all(ctx);
    ctx->top_module_space = NULL;
    ctx->top_module_registry = NULL;
    free(ctx->import_dirs);
    ctx->import_dirs = NULL;
    ctx->import_dir_len = ctx->import_dir_cap = 0u;
    library_he_failures_free(ctx->he_module_failures);
    ctx->he_module_failures = NULL;
    for (uint32_t i = 0u; i < ctx->module_own_view_len; i++) {
        space_free(ctx->module_own_views[i].view);
        free(ctx->module_own_views[i].view);
    }
    free(ctx->module_own_views);
    ctx->module_own_views = NULL;
    ctx->module_own_view_len = ctx->module_own_view_cap = 0u;
    if (ctx->petta_open_programs_free)
        ctx->petta_open_programs_free(ctx->petta_open_programs);
    ctx->petta_open_programs = NULL;
    ctx->petta_open_programs_free = NULL;
    if (ctx->petta_match_decisions_free)
        ctx->petta_match_decisions_free(ctx->petta_match_decisions);
    ctx->petta_match_decisions = NULL;
    ctx->petta_match_decisions_free = NULL;
    if (ctx->petta_static_imports_free)
        ctx->petta_static_imports_free(ctx->petta_static_imports);
    ctx->petta_static_imports = NULL;
    ctx->petta_static_imports_free = NULL;
    if (ctx->prime_hol_guest_free)
        ctx->prime_hol_guest_free(ctx->prime_hol_guest);
    ctx->prime_hol_guest = NULL;
    ctx->prime_hol_guest_free = NULL;
    if (ctx->prime_lf_guest_free)
        ctx->prime_lf_guest_free(ctx->prime_lf_guest);
    ctx->prime_lf_guest = NULL;
    ctx->prime_lf_guest_free = NULL;
    if (ctx->prime_standard_library_free)
        ctx->prime_standard_library_free(ctx->prime_standard_library);
    ctx->prime_standard_library = NULL;
    ctx->prime_standard_library_free = NULL;
    cetta_nik_runtime_v1_free(ctx->nik_runtime);
    ctx->nik_runtime = NULL;
    if (ctx->nik_runtime_mutex_ready) {
        pthread_mutex_destroy(&ctx->nik_runtime_mutex);
        ctx->nik_runtime_mutex_ready = false;
    }
    for (uint32_t i = 0; i < ctx->loaded_module_len; i++) {
        CettaLoadedModule *module = ctx->loaded_modules[i];
        cetta_foreign_he_module_release(module->provider);
        library_he_provider_exports_free(module->exports);
        if (module->tokens) {
            registry_free(module->tokens);
            free(module->tokens);
        }
        Space *space = module->space;
        free(module->module_name);
        if (space) {
            if (ctx->petta_program)
                petta_program_forget_space(ctx->petta_program, space);
            space_free(space);
            free(space);
        }
        free(module);
    }
    free(ctx->loaded_modules);
    ctx->loaded_modules = NULL;
    ctx->loaded_module_len = ctx->loaded_module_cap = 0u;
    free(ctx->imported_files);
    ctx->imported_files = NULL;
    ctx->imported_file_len = ctx->imported_file_cap = 0u;
    for (uint32_t i = 0u; i < ctx->module_mount_len; i++)
        free(ctx->module_mounts[i]);
    free(ctx->module_mounts);
    ctx->module_mounts = NULL;
    ctx->module_mount_len = ctx->module_mount_cap = 0u;
    free(ctx->import_space_aliases);
    ctx->import_space_aliases = NULL;
    ctx->import_space_alias_len = ctx->import_space_alias_cap = 0u;
    for (uint32_t i = 0u; i < ctx->he_module_alias_len; i++)
        free(ctx->he_module_aliases[i]);
    free(ctx->he_module_aliases);
    ctx->he_module_aliases = NULL;
    ctx->he_module_alias_len = ctx->he_module_alias_cap = 0u;
    for (uint32_t i = 0u; i < ctx->retired_module_len; i++) {
        if (ctx->retired_modules[i].tokens) {
            registry_free(ctx->retired_modules[i].tokens);
            free(ctx->retired_modules[i].tokens);
        }
        if (ctx->petta_program)
            petta_program_forget_space(ctx->petta_program,
                                       ctx->retired_modules[i].space);
        space_free(ctx->retired_modules[i].space);
        free(ctx->retired_modules[i].space);
        free(ctx->retired_modules[i].module_name);
    }
    free(ctx->retired_modules);
    ctx->retired_modules = NULL;
    ctx->retired_module_len = ctx->retired_module_cap = 0u;
    free(ctx->petta_library_paths.items);
    memset(&ctx->petta_library_paths, 0, sizeof(ctx->petta_library_paths));
    term_universe_free(&ctx->term_universe);
    /* The cache holds no reference of its own. Closing the last native
     * holder removes its entry before destroying the compilation. */
    ctx->parser_cache = NULL;
    if (ctx->parser_cache_mutex_ready) {
        pthread_mutex_destroy(&ctx->parser_cache_mutex);
        ctx->parser_cache_mutex_ready = false;
    }
    free(ctx->petta_translator_rules);
    ctx->petta_translator_rules = NULL;
    ctx->petta_translator_rule_len = 0u;
    ctx->petta_translator_rule_cap = 0u;
    ctx->petta_translator_symbol_table_instance = 0u;
    ctx->petta_translator_rule_revision = 0u;
    free(ctx->petta_tabled_relations);
    ctx->petta_tabled_relations = NULL;
    ctx->petta_tabled_relation_len = 0u;
    ctx->petta_tabled_relation_cap = 0u;
    ctx->petta_tabled_symbol_table_instance = 0u;
    free(ctx->petta_memo.all_arities);
    free(ctx->petta_memo.exact_arities);
    ctx->petta_memo.all_arities = NULL;
    ctx->petta_memo.exact_arities = NULL;
    ctx->petta_memo.all_arity_len = 0u;
    ctx->petta_memo.all_arity_cap = 0u;
    ctx->petta_memo.exact_arity_len = 0u;
    ctx->petta_memo.exact_arity_cap = 0u;
    petta_machine_table_free(ctx->petta_shared_table);
    ctx->petta_shared_table = NULL;
    petta_program_free(ctx->petta_program);
    ctx->petta_program = NULL;
    cetta_petta_typecheck_v3_free(ctx->petta_typecheck_v3);
    ctx->petta_typecheck_v3 = NULL;
    ctx->prime_relational_plan_enabled = false;
    cetta_petta_runtime_state_free(ctx->petta_runtime);
    ctx->petta_runtime = NULL;
    cetta_petta_token_space_clause_registry_free(
        ctx->petta_token_space_clause_registry);
    ctx->petta_token_space_clause_registry = NULL;
    cetta_lib_prolog_runtime_free(ctx->lib_prolog);
    ctx->lib_prolog = NULL;
    if (ctx->foreign_runtime) {
        cetta_foreign_runtime_free(ctx->foreign_runtime);
        ctx->foreign_runtime = NULL;
    }
    cetta_durable_runtime_free(ctx->durable_runtime);
    ctx->durable_runtime = NULL;
    cetta_io_runtime_free(ctx->io_runtime);
    ctx->io_runtime = NULL;
#if CETTA_BUILD_WITH_JSON_GSLT
    cetta_json_library_runtime_v1_free(ctx->json_runtime);
#endif
    ctx->json_runtime = NULL;
}

struct CettaNikRuntimeV1 *cetta_library_context_nik_runtime(
    CettaLibraryContext *ctx,
    char *error_buf,
    size_t error_buf_size) {
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!ctx) {
        if (error_buf && error_buf_size > 0u)
            (void)snprintf(
                error_buf, error_buf_size,
                "NIK runtime requires a library context");
        return NULL;
    }
    if (!ctx->nik_runtime_mutex_ready) {
        if (error_buf && error_buf_size > 0u)
            (void)snprintf(
                error_buf, error_buf_size,
                "NIK runtime session mutex is unavailable");
        return NULL;
    }
    pthread_mutex_lock(&ctx->nik_runtime_mutex);
    if (!ctx->nik_runtime)
        ctx->nik_runtime = cetta_nik_runtime_v1_new(
            error_buf, error_buf_size);
    struct CettaNikRuntimeV1 *runtime = ctx->nik_runtime;
    pthread_mutex_unlock(&ctx->nik_runtime_mutex);
    return runtime;
}

static void cetta_library_petta_admission_advance(
    CettaLibraryContext *ctx) {
    if (!ctx)
        return;
    ctx->petta_admission_revision =
        ctx->petta_admission_revision == UINT64_MAX
            ? 1u : ctx->petta_admission_revision + 1u;
}

static void cetta_library_petta_translator_rule_advance(
    CettaLibraryContext *ctx) {
    if (!ctx)
        return;
    ctx->petta_translator_rule_revision =
        ctx->petta_translator_rule_revision == UINT64_MAX
            ? 1u : ctx->petta_translator_rule_revision + 1u;
    cetta_library_petta_admission_advance(ctx);
}

static void cetta_library_petta_translator_rule_sync(
    CettaLibraryContext *ctx) {
    if (!ctx)
        return;
    uint64_t instance = symbol_table_instance_id(g_symbols);
    if (ctx->petta_translator_symbol_table_instance == instance)
        return;
    ctx->petta_translator_rule_len = 0u;
    ctx->petta_translator_symbol_table_instance = instance;
    cetta_library_petta_translator_rule_advance(ctx);
}

static uint32_t cetta_library_petta_translator_rule_lower_bound(
    const CettaLibraryContext *ctx, SymbolId head) {
    uint32_t low = 0u;
    uint32_t high = ctx ? ctx->petta_translator_rule_len : 0u;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        if (ctx->petta_translator_rules[middle] < head)
            low = middle + 1u;
        else
            high = middle;
    }
    return low;
}

bool cetta_library_petta_translator_rule_contains(
    CettaLibraryContext *ctx, SymbolId head) {
    if (!ctx || head == SYMBOL_ID_NONE)
        return false;
    cetta_library_petta_translator_rule_sync(ctx);
    uint32_t index =
        cetta_library_petta_translator_rule_lower_bound(ctx, head);
    return index < ctx->petta_translator_rule_len &&
           ctx->petta_translator_rules[index] == head;
}

bool cetta_library_petta_translator_rule_set(
    CettaLibraryContext *ctx, SymbolId head, bool enabled) {
    if (!ctx || head == SYMBOL_ID_NONE)
        return false;
    if (enabled)
        (void)symbol_pin(g_symbols, head);
    cetta_library_petta_translator_rule_sync(ctx);
    uint32_t index =
        cetta_library_petta_translator_rule_lower_bound(ctx, head);
    bool present =
        index < ctx->petta_translator_rule_len &&
        ctx->petta_translator_rules[index] == head;
    if (present == enabled)
        return true;
    if (!enabled) {
        memmove(
            ctx->petta_translator_rules + index,
            ctx->petta_translator_rules + index + 1u,
            sizeof(*ctx->petta_translator_rules) *
                (size_t)(ctx->petta_translator_rule_len - index - 1u));
        ctx->petta_translator_rule_len--;
        cetta_library_petta_translator_rule_advance(ctx);
        return true;
    }
    if (ctx->petta_translator_rule_len ==
        ctx->petta_translator_rule_cap) {
        uint32_t next = ctx->petta_translator_rule_cap
            ? ctx->petta_translator_rule_cap * 2u : 8u;
        if (next <= ctx->petta_translator_rule_cap ||
            (size_t)next >
                SIZE_MAX / sizeof(*ctx->petta_translator_rules)) {
            return false;
        }
        ctx->petta_translator_rules = cetta_realloc(
            ctx->petta_translator_rules,
            sizeof(*ctx->petta_translator_rules) * (size_t)next);
        ctx->petta_translator_rule_cap = next;
    }
    memmove(
        ctx->petta_translator_rules + index + 1u,
        ctx->petta_translator_rules + index,
        sizeof(*ctx->petta_translator_rules) *
            (size_t)(ctx->petta_translator_rule_len - index));
    ctx->petta_translator_rules[index] = head;
    ctx->petta_translator_rule_len++;
    cetta_library_petta_translator_rule_advance(ctx);
    return true;
}

uint64_t cetta_library_petta_translator_rule_revision(
    CettaLibraryContext *ctx) {
    if (!ctx)
        return 0u;
    cetta_library_petta_translator_rule_sync(ctx);
    return ctx->petta_translator_rule_revision;
}

static void cetta_library_petta_tabled_relation_sync(
    CettaLibraryContext *ctx);
static void cetta_library_petta_memo_sync(CettaLibraryContext *ctx);

uint64_t cetta_library_petta_admission_revision(CettaLibraryContext *ctx) {
    if (!ctx)
        return 0u;
    cetta_library_petta_translator_rule_sync(ctx);
    cetta_library_petta_tabled_relation_sync(ctx);
    cetta_library_petta_memo_sync(ctx);
    return ctx->petta_admission_revision;
}

static void cetta_library_petta_tabled_relation_sync(
    CettaLibraryContext *ctx) {
    if (!ctx)
        return;
    uint64_t instance = symbol_table_instance_id(g_symbols);
    if (ctx->petta_tabled_symbol_table_instance == instance)
        return;
    ctx->petta_tabled_relation_len = 0u;
    ctx->petta_tabled_symbol_table_instance = instance;
    cetta_library_petta_admission_advance(ctx);
}

static int cetta_library_petta_relation_key_compare(
    CettaPettaRelationKey left, CettaPettaRelationKey right) {
    if (left.head != right.head)
        return left.head < right.head ? -1 : 1;
    if (left.arity != right.arity)
        return left.arity < right.arity ? -1 : 1;
    return 0;
}

static uint32_t cetta_library_petta_tabled_relation_lower_bound(
    const CettaLibraryContext *ctx, CettaPettaRelationKey key) {
    uint32_t low = 0u;
    uint32_t high = ctx ? ctx->petta_tabled_relation_len : 0u;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        if (cetta_library_petta_relation_key_compare(
                ctx->petta_tabled_relations[middle], key) < 0) {
            low = middle + 1u;
        } else {
            high = middle;
        }
    }
    return low;
}

bool cetta_library_petta_tabled_relation_contains(
    CettaLibraryContext *ctx, SymbolId head, CettaExprLen arity) {
    if (!ctx || head == SYMBOL_ID_NONE)
        return false;
    cetta_library_petta_tabled_relation_sync(ctx);
    CettaPettaRelationKey key = {
        .head = head,
        .arity = arity,
    };
    uint32_t index =
        cetta_library_petta_tabled_relation_lower_bound(ctx, key);
    return index < ctx->petta_tabled_relation_len &&
           cetta_library_petta_relation_key_compare(
               ctx->petta_tabled_relations[index], key) == 0;
}

bool cetta_library_petta_tabled_relation_set(
    CettaLibraryContext *ctx, SymbolId head, CettaExprLen arity,
    bool enabled) {
    if (!ctx || head == SYMBOL_ID_NONE)
        return false;
    cetta_library_petta_tabled_relation_sync(ctx);
    CettaPettaRelationKey key = {
        .head = head,
        .arity = arity,
    };
    uint32_t index =
        cetta_library_petta_tabled_relation_lower_bound(ctx, key);
    bool present =
        index < ctx->petta_tabled_relation_len &&
        cetta_library_petta_relation_key_compare(
            ctx->petta_tabled_relations[index], key) == 0;
    if (present == enabled)
        return true;
    cetta_library_petta_admission_advance(ctx);
    if (!enabled) {
        memmove(
            ctx->petta_tabled_relations + index,
            ctx->petta_tabled_relations + index + 1u,
            sizeof(*ctx->petta_tabled_relations) *
                (size_t)(ctx->petta_tabled_relation_len - index - 1u));
        ctx->petta_tabled_relation_len--;
        return true;
    }
    if (ctx->petta_tabled_relation_len ==
        ctx->petta_tabled_relation_cap) {
        uint32_t next = ctx->petta_tabled_relation_cap
            ? ctx->petta_tabled_relation_cap * 2u : 8u;
        if (next <= ctx->petta_tabled_relation_cap ||
            (size_t)next >
                SIZE_MAX / sizeof(*ctx->petta_tabled_relations)) {
            return false;
        }
        ctx->petta_tabled_relations = cetta_realloc(
            ctx->petta_tabled_relations,
            sizeof(*ctx->petta_tabled_relations) * (size_t)next);
        ctx->petta_tabled_relation_cap = next;
    }
    memmove(
        ctx->petta_tabled_relations + index + 1u,
        ctx->petta_tabled_relations + index,
        sizeof(*ctx->petta_tabled_relations) *
            (size_t)(ctx->petta_tabled_relation_len - index));
    ctx->petta_tabled_relations[index] = key;
    ctx->petta_tabled_relation_len++;
    return true;
}

static void cetta_library_petta_memo_sync(
    CettaLibraryContext *ctx) {
    if (!ctx)
        return;
    uint64_t instance = symbol_table_instance_id(g_symbols);
    if (ctx->petta_memo.symbol_table_instance == instance)
        return;
    ctx->petta_memo.all_arity_len = 0u;
    ctx->petta_memo.exact_arity_len = 0u;
    ctx->petta_memo.symbol_table_instance = instance;
    petta_machine_table_reset(ctx->petta_shared_table);
    cetta_library_petta_admission_advance(ctx);
}

static uint32_t cetta_library_symbol_lower_bound(
    const SymbolId *items, uint32_t length, SymbolId key) {
    uint32_t low = 0u;
    uint32_t high = length;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        if (items[middle] < key)
            low = middle + 1u;
        else
            high = middle;
    }
    return low;
}

static uint32_t cetta_library_relation_lower_bound(
    const CettaPettaRelationKey *items, uint32_t length,
    CettaPettaRelationKey key) {
    uint32_t low = 0u;
    uint32_t high = length;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        if (cetta_library_petta_relation_key_compare(
                items[middle], key) < 0) {
            low = middle + 1u;
        } else {
            high = middle;
        }
    }
    return low;
}

typedef struct {
    const char *name;
    CettaExprLen minimum_arity;
    CettaExprLen maximum_arity;
} CettaPettaMemoControlSpec;

static const CettaPettaMemoControlSpec CETTA_PETTA_MEMO_CONTROLS[] = {
    [CETTA_PETTA_MEMO_CONTROL_MEMOIZE] =
        {"memoize", 1u, 2u},
    [CETTA_PETTA_MEMO_CONTROL_CONFIGURE] =
        {"config-memoize", 1u, 3u},
    [CETTA_PETTA_MEMO_CONTROL_GET_CONFIG] =
        {"get-memoize-config", 0u, 0u},
    [CETTA_PETTA_MEMO_CONTROL_CLEAR] =
        {"clear-memoize", 0u, 0u},
    [CETTA_PETTA_MEMO_CONTROL_INVALIDATE] =
        {"invalidate-memoize", 1u, 1u},
    [CETTA_PETTA_MEMO_CONTROL_IS_MEMOIZED] =
        {"is-memoized", 1u, 2u},
    [CETTA_PETTA_MEMO_CONTROL_GET_STATS] =
        {"get-memoize-stats", 0u, 0u},
    [CETTA_PETTA_MEMO_CONTROL_CLEAR_STATS] =
        {"clear-memoize-stats", 0u, 0u},
};

enum { CETTA_PETTA_MEMO_CONTROL_CACHE_SLOTS = 16 };

typedef struct {
    SymbolId symbol;
    CettaPettaMemoControl control;
} CettaPettaMemoControlCacheSlot;

typedef struct {
    const SymbolTable *table;
    uint64_t table_instance;
    CettaPettaMemoControlCacheSlot
        slots[CETTA_PETTA_MEMO_CONTROL_CACHE_SLOTS];
} CettaPettaMemoControlCache;

static _Thread_local CettaPettaMemoControlCache
    g_petta_memo_control_cache;

static void cetta_library_petta_memo_control_cache_refresh(void) {
    CettaPettaMemoControlCache cache = {
        .table = g_symbols,
        .table_instance = symbol_table_instance_id(g_symbols),
    };
    if (g_symbols) {
        for (size_t index = 0u;
             index < CETTA_PETTA_MEMO_CONTROL_COUNT; index++) {
            SymbolId symbol = symbol_intern_cstr(
                g_symbols, CETTA_PETTA_MEMO_CONTROLS[index].name);
            size_t slot =
                ((size_t)symbol * UINT32_C(2654435761)) &
                (CETTA_PETTA_MEMO_CONTROL_CACHE_SLOTS - 1u);
            while (cache.slots[slot].symbol != SYMBOL_ID_NONE)
                slot = (slot + 1u) &
                    (CETTA_PETTA_MEMO_CONTROL_CACHE_SLOTS - 1u);
            cache.slots[slot] = (CettaPettaMemoControlCacheSlot){
                .symbol = symbol,
                .control = (CettaPettaMemoControl)index,
            };
        }
    }
    g_petta_memo_control_cache = cache;
}

static bool cetta_library_petta_memo_control_lookup(
    SymbolId head, CettaPettaMemoControl *control_out) {
    if (head == SYMBOL_ID_NONE || !g_symbols)
        return false;
    uint64_t table_instance = symbol_table_instance_id(g_symbols);
    if (g_petta_memo_control_cache.table != g_symbols ||
        g_petta_memo_control_cache.table_instance != table_instance) {
        cetta_library_petta_memo_control_cache_refresh();
    }
    size_t slot = ((size_t)head * UINT32_C(2654435761)) &
        (CETTA_PETTA_MEMO_CONTROL_CACHE_SLOTS - 1u);
    for (size_t probe = 0u;
         probe < CETTA_PETTA_MEMO_CONTROL_CACHE_SLOTS; probe++) {
        CettaPettaMemoControlCacheSlot *candidate =
            &g_petta_memo_control_cache.slots[slot];
        if (candidate->symbol == SYMBOL_ID_NONE)
            return false;
        if (candidate->symbol == head) {
            if (control_out)
                *control_out = candidate->control;
            return true;
        }
        slot = (slot + 1u) &
            (CETTA_PETTA_MEMO_CONTROL_CACHE_SLOTS - 1u);
    }
    return false;
}

bool cetta_library_petta_memo_control_import(
    CettaLibraryContext *ctx, SymbolId head) {
    CettaPettaMemoControl control;
    if (!ctx ||
        ctx->session.language_id != CETTA_LANGUAGE_PETTA ||
        !(ctx->active_mask & CETTA_LIBRARY_PETTA_MEMO) ||
        !cetta_library_petta_memo_control_lookup(head, &control)) {
        return false;
    }
    if (!ctx->petta_memo.imported_controls[control]) {
        ctx->petta_memo.imported_controls[control] = true;
        cetta_library_petta_admission_advance(ctx);
    }
    return true;
}

bool cetta_library_petta_memo_control_imported(
    CettaLibraryContext *ctx, SymbolId head) {
    CettaPettaMemoControl control;
    return ctx &&
        ctx->session.language_id == CETTA_LANGUAGE_PETTA &&
        cetta_library_petta_memo_control_lookup(head, &control) &&
        ctx->petta_memo.imported_controls[control];
}

PeTTaNamedArity cetta_library_petta_memo_control_named_arity(
    CettaLibraryContext *ctx, SymbolId head,
    CettaExprLen supplied) {
    CettaPettaMemoControl control;
    if (!ctx ||
        ctx->session.language_id != CETTA_LANGUAGE_PETTA ||
        !cetta_library_petta_memo_control_lookup(head, &control) ||
        !ctx->petta_memo.imported_controls[control]) {
        return (PeTTaNamedArity){0};
    }
    const CettaPettaMemoControlSpec *spec =
        &CETTA_PETTA_MEMO_CONTROLS[control];
    return (PeTTaNamedArity){
        .known = true,
        .exact = supplied >= spec->minimum_arity &&
                 supplied <= spec->maximum_arity,
        .larger = supplied < spec->maximum_arity,
        .smaller = supplied > spec->minimum_arity,
    };
}

bool cetta_library_petta_memo_contains(
    CettaLibraryContext *ctx, SymbolId head,
    CettaExprLen arity) {
    if (!ctx || head == SYMBOL_ID_NONE)
        return false;
    cetta_library_petta_memo_sync(ctx);
    uint32_t all = cetta_library_symbol_lower_bound(
        ctx->petta_memo.all_arities,
        ctx->petta_memo.all_arity_len, head);
    if (all < ctx->petta_memo.all_arity_len &&
        ctx->petta_memo.all_arities[all] == head) {
        return true;
    }
    CettaPettaRelationKey key = {
        .head = head,
        .arity = arity,
    };
    uint32_t exact = cetta_library_relation_lower_bound(
        ctx->petta_memo.exact_arities,
        ctx->petta_memo.exact_arity_len, key);
    return exact < ctx->petta_memo.exact_arity_len &&
        cetta_library_petta_relation_key_compare(
            ctx->petta_memo.exact_arities[exact], key) == 0;
}

bool cetta_library_petta_memo_enable(
    CettaLibraryContext *ctx, SymbolId head,
    bool every_arity, CettaExprLen arity) {
    if (!ctx || head == SYMBOL_ID_NONE)
        return false;
    cetta_library_petta_memo_sync(ctx);
    if (every_arity) {
        uint32_t index = cetta_library_symbol_lower_bound(
            ctx->petta_memo.all_arities,
            ctx->petta_memo.all_arity_len, head);
        if (index < ctx->petta_memo.all_arity_len &&
            ctx->petta_memo.all_arities[index] == head) {
            return true;
        }
        if (ctx->petta_memo.all_arity_len ==
            ctx->petta_memo.all_arity_cap) {
            uint32_t next = ctx->petta_memo.all_arity_cap
                ? ctx->petta_memo.all_arity_cap * 2u : 8u;
            if (next <= ctx->petta_memo.all_arity_cap ||
                (size_t)next > SIZE_MAX / sizeof(SymbolId)) {
                return false;
            }
            ctx->petta_memo.all_arities = cetta_realloc(
                ctx->petta_memo.all_arities,
                sizeof(SymbolId) * (size_t)next);
            ctx->petta_memo.all_arity_cap = next;
        }
        memmove(
            ctx->petta_memo.all_arities + index + 1u,
            ctx->petta_memo.all_arities + index,
            sizeof(SymbolId) *
                (size_t)(ctx->petta_memo.all_arity_len - index));
        ctx->petta_memo.all_arities[index] = head;
        ctx->petta_memo.all_arity_len++;
        petta_machine_table_reset(ctx->petta_shared_table);
        cetta_library_petta_admission_advance(ctx);
        return true;
    }

    CettaPettaRelationKey key = {
        .head = head,
        .arity = arity,
    };
    uint32_t index = cetta_library_relation_lower_bound(
        ctx->petta_memo.exact_arities,
        ctx->petta_memo.exact_arity_len, key);
    if (index < ctx->petta_memo.exact_arity_len &&
        cetta_library_petta_relation_key_compare(
            ctx->petta_memo.exact_arities[index], key) == 0) {
        return true;
    }
    if (ctx->petta_memo.exact_arity_len ==
        ctx->petta_memo.exact_arity_cap) {
        uint32_t next = ctx->petta_memo.exact_arity_cap
            ? ctx->petta_memo.exact_arity_cap * 2u : 8u;
        if (next <= ctx->petta_memo.exact_arity_cap ||
            (size_t)next > SIZE_MAX /
                sizeof(*ctx->petta_memo.exact_arities)) {
            return false;
        }
        ctx->petta_memo.exact_arities = cetta_realloc(
            ctx->petta_memo.exact_arities,
            sizeof(*ctx->petta_memo.exact_arities) *
                (size_t)next);
        ctx->petta_memo.exact_arity_cap = next;
    }
    memmove(
        ctx->petta_memo.exact_arities + index + 1u,
        ctx->petta_memo.exact_arities + index,
        sizeof(*ctx->petta_memo.exact_arities) *
            (size_t)(ctx->petta_memo.exact_arity_len - index));
    ctx->petta_memo.exact_arities[index] = key;
    ctx->petta_memo.exact_arity_len++;
    petta_machine_table_reset(ctx->petta_shared_table);
    cetta_library_petta_admission_advance(ctx);
    return true;
}

bool cetta_library_petta_memo_is_enabled(
    CettaLibraryContext *ctx, SymbolId head,
    bool exact_arity, CettaExprLen arity) {
    if (!ctx || head == SYMBOL_ID_NONE)
        return false;
    if (exact_arity)
        return cetta_library_petta_memo_contains(
            ctx, head, arity);
    cetta_library_petta_memo_sync(ctx);
    uint32_t all = cetta_library_symbol_lower_bound(
        ctx->petta_memo.all_arities,
        ctx->petta_memo.all_arity_len, head);
    if (all < ctx->petta_memo.all_arity_len &&
        ctx->petta_memo.all_arities[all] == head) {
        return true;
    }
    CettaPettaRelationKey low = {
        .head = head,
        .arity = 0u,
    };
    uint32_t exact = cetta_library_relation_lower_bound(
        ctx->petta_memo.exact_arities,
        ctx->petta_memo.exact_arity_len, low);
    return exact < ctx->petta_memo.exact_arity_len &&
        ctx->petta_memo.exact_arities[exact].head == head;
}

void cetta_library_petta_memo_clear_stats(
    CettaLibraryContext *ctx) {
    if (!ctx)
        return;
    ctx->petta_memo.stat_tick = 0u;
    ctx->petta_memo.cache_hits = 0u;
    ctx->petta_memo.cache_hits_stamp = 0u;
    ctx->petta_memo.cache_misses = 0u;
    ctx->petta_memo.cache_misses_stamp = 0u;
    ctx->petta_memo.cache_bypasses = 0u;
    ctx->petta_memo.cache_bypasses_stamp = 0u;
    ctx->petta_memo.answer_limit_truncated = 0u;
    ctx->petta_memo.answer_limit_truncated_stamp = 0u;
}

void cetta_library_petta_memo_clear(
    CettaLibraryContext *ctx) {
    if (!ctx)
        return;
    petta_machine_table_reset(ctx->petta_shared_table);
    cetta_library_petta_memo_clear_stats(ctx);
}

void cetta_library_petta_memo_invalidate(
    CettaLibraryContext *ctx, SymbolId head) {
    (void)head;
    if (!ctx)
        return;
    /* Conservative v1 invalidation is whole-table.  The cache is a physical
     * accelerator, so broader invalidation changes work but never answers;
     * dependency-scoped generations can refine this later. */
    petta_machine_table_reset(ctx->petta_shared_table);
}

void cetta_library_petta_memo_observe(
    CettaLibraryContext *ctx, SymbolId head,
    CettaExprLen arity, bool cache_hit) {
    if (!ctx || !cetta_library_petta_memo_contains(
                    ctx, head, arity)) {
        return;
    }
    uint64_t *counter = cache_hit
        ? &ctx->petta_memo.cache_hits
        : &ctx->petta_memo.cache_misses;
    uint64_t *stamp = cache_hit
        ? &ctx->petta_memo.cache_hits_stamp
        : &ctx->petta_memo.cache_misses_stamp;
    if (*counter != UINT64_MAX)
        (*counter)++;
    if (ctx->petta_memo.stat_tick != UINT64_MAX)
        ctx->petta_memo.stat_tick++;
    *stamp = ctx->petta_memo.stat_tick;
}

void cetta_library_petta_memo_observe_bypass(
    CettaLibraryContext *ctx, SymbolId head,
    CettaExprLen arity) {
    if (!ctx || !cetta_library_petta_memo_contains(
                    ctx, head, arity)) {
        return;
    }
    if (ctx->petta_memo.cache_bypasses != UINT64_MAX)
        ctx->petta_memo.cache_bypasses++;
    if (ctx->petta_memo.stat_tick != UINT64_MAX)
        ctx->petta_memo.stat_tick++;
    ctx->petta_memo.cache_bypasses_stamp =
        ctx->petta_memo.stat_tick;
}

void cetta_library_petta_memo_observe_truncation(
    CettaLibraryContext *ctx, SymbolId head,
    CettaExprLen arity) {
    if (!ctx || !cetta_library_petta_memo_contains(
                    ctx, head, arity)) {
        return;
    }
    if (ctx->petta_memo.answer_limit_truncated != UINT64_MAX)
        ctx->petta_memo.answer_limit_truncated++;
    if (ctx->petta_memo.stat_tick != UINT64_MAX)
        ctx->petta_memo.stat_tick++;
    ctx->petta_memo.answer_limit_truncated_stamp =
        ctx->petta_memo.stat_tick;
}

static void copy_parent_dir(char *dst, size_t dst_sz, const char *path);
static bool cetta_path_strip_prefix(const char *path, const char *prefix,
                                    const char **relative_out);

static bool library_root_has_lib_dir(const char *root) {
    char path[PATH_MAX];
    struct stat st;

    if (!root || !*root) return false;
    int n = snprintf(path, sizeof(path), "%s/lib", root);
    return n > 0 && (size_t)n < sizeof(path) &&
           stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool library_resolve_exec_path(const char *argv0, char resolved[PATH_MAX]) {
    const char *search = getenv("PATH");
    if (strchr(argv0, '/') || !search)
        return realpath(argv0, resolved) != NULL;
    while (true) {
        const char *end = strchr(search, ':');
        size_t length = end ? (size_t)(end - search) : strlen(search);
        char candidate[PATH_MAX];
        struct stat st;
        if (length < sizeof(candidate)) {
            int n = length
                ? snprintf(candidate, sizeof(candidate), "%.*s/%s",
                           (int)length, search, argv0)
                : snprintf(candidate, sizeof(candidate), "%s", argv0);
            if (n > 0 && (size_t)n < sizeof(candidate) &&
                stat(candidate, &st) == 0 && S_ISREG(st.st_mode) &&
                access(candidate, X_OK) == 0 && realpath(candidate, resolved))
                return true;
        }
        if (!end)
            return false;
        search = end + 1;
    }
}

bool cetta_library_root_for_exec_path(const char *argv0,
                                      char *output, size_t output_size) {
    char resolved[PATH_MAX];
    char parent[PATH_MAX];
    char *slash;

    if (!output || output_size == 0u)
        return false;
    output[0] = '\0';
    if (!argv0 || !*argv0 || !library_resolve_exec_path(argv0, resolved))
        return false;
    slash = strrchr(resolved, '/');
    if (!slash)
        return false;
    *slash = '\0';
    if (!library_root_has_lib_dir(resolved)) {
        copy_parent_dir(parent, sizeof(parent), resolved);
        if (strcmp(parent, resolved) == 0 ||
            !library_root_has_lib_dir(parent))
            return false;
        snprintf(resolved, sizeof(resolved), "%s", parent);
    }
    if (strlen(resolved) >= output_size)
        return false;
    memcpy(output, resolved, strlen(resolved) + 1u);
    return true;
}

void cetta_library_context_set_exec_path(CettaLibraryContext *ctx, const char *argv0) {
    if (!ctx)
        return;
    if (!cetta_library_root_for_exec_path(
            argv0, ctx->root_dir, sizeof(ctx->root_dir)))
        ctx->root_dir[0] = '\0';
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

static void copy_language_source_dir(CettaLibraryContext *ctx, char *out,
                                      size_t out_sz, const char *path) {
    /* A dialect spelling occupies the shared module's namespace. Resolve
     * its relative imports and resources beside the shared source, then
     * select each dependency's own dialect spelling independently. */
    const char *source_path = path;
    char shared_path[PATH_MAX], dialect_root[PATH_MAX], canonical[PATH_MAX];
    const char *relative = NULL;
    if (ctx->session.language_id == CETTA_LANGUAGE_PRIME) {
        int n = snprintf(dialect_root, sizeof(dialect_root), "%s/lib/prime",
                         ctx->root_dir[0] ? ctx->root_dir : ".");
        if (n > 0 && (size_t)n < sizeof(dialect_root) &&
            realpath(dialect_root, canonical) &&
            cetta_path_strip_prefix(path, canonical, &relative)) {
            n = snprintf(shared_path, sizeof(shared_path), "%s/lib/%s",
                         ctx->root_dir[0] ? ctx->root_dir : ".", relative);
            if (n > 0 && (size_t)n < sizeof(shared_path) &&
                access(shared_path, R_OK) == 0)
                source_path = shared_path;
        }
    }
    copy_parent_dir(out, out_sz, source_path);
}

void cetta_library_context_set_script_path(CettaLibraryContext *ctx, const char *filename) {
    char resolved[PATH_MAX];

    ctx->script_path[0] = '\0';
    ctx->script_dir[0] = '\0';
    if (!filename) return;
    snprintf(ctx->script_path, sizeof(ctx->script_path), "%s", filename);
    if (!realpath(filename, resolved)) return;
    copy_language_source_dir(ctx, ctx->script_dir, sizeof(ctx->script_dir), resolved);
    if (ctx->lib_prolog && ctx->script_dir[0] != '\0') {
        (void)cetta_lib_prolog_runtime_set_working_dir(
            ctx->lib_prolog, ctx->script_dir);
    }
}

static int64_t imported_file_lookup(CettaLibraryContext *ctx, Space *space,
                                    const char *path);

static bool library_reserve_imported_file(CettaLibraryContext *ctx) {
    if (ctx->imported_file_len < ctx->imported_file_cap)
        return true;
    if (ctx->imported_file_len == UINT32_MAX)
        return false;
    uint32_t next = ctx->imported_file_cap
        ? (ctx->imported_file_cap > UINT32_MAX / 2u
            ? UINT32_MAX : ctx->imported_file_cap * 2u) : 16u;
    if ((size_t)next > SIZE_MAX / sizeof(*ctx->imported_files))
        return false;
    CettaImportedFile *records = realloc(
        ctx->imported_files, (size_t)next * sizeof(*records));
    if (!records)
        return false;
    ctx->imported_files = records;
    ctx->imported_file_cap = next;
    return true;
}

static bool library_reserve_module_mount(CettaLibraryContext *ctx) {
    if (ctx->module_mount_len < ctx->module_mount_cap)
        return true;
    if (ctx->module_mount_len == UINT32_MAX)
        return false;
    uint32_t next = ctx->module_mount_cap
        ? (ctx->module_mount_cap > UINT32_MAX / 2u
            ? UINT32_MAX : ctx->module_mount_cap * 2u) : 8u;
    if ((size_t)next > SIZE_MAX / sizeof(*ctx->module_mounts))
        return false;
    CettaModuleMount **records = realloc(
        ctx->module_mounts, (size_t)next * sizeof(*records));
    if (!records)
        return false;
    ctx->module_mounts = records;
    ctx->module_mount_cap = next;
    return true;
}

static bool library_reserve_import_alias(CettaLibraryContext *ctx) {
    if (ctx->import_space_alias_len < ctx->import_space_alias_cap)
        return true;
    if (ctx->import_space_alias_len == UINT32_MAX)
        return false;
    uint32_t next = ctx->import_space_alias_cap
        ? (ctx->import_space_alias_cap > UINT32_MAX / 2u
            ? UINT32_MAX : ctx->import_space_alias_cap * 2u) : 8u;
    if ((size_t)next > SIZE_MAX / sizeof(*ctx->import_space_aliases))
        return false;
    void *records = realloc(ctx->import_space_aliases,
                            (size_t)next * sizeof(*ctx->import_space_aliases));
    if (!records)
        return false;
    ctx->import_space_aliases = records;
    ctx->import_space_alias_cap = next;
    return true;
}

static bool library_reserve_loaded_module(CettaLibraryContext *ctx) {
    if (ctx->loaded_module_len < ctx->loaded_module_cap)
        return true;
    if (ctx->loaded_module_len == UINT32_MAX)
        return false;
    uint32_t next = ctx->loaded_module_cap
        ? (ctx->loaded_module_cap > UINT32_MAX / 2u
            ? UINT32_MAX : ctx->loaded_module_cap * 2u) : 8u;
    if ((size_t)next > SIZE_MAX / sizeof(*ctx->loaded_modules))
        return false;
    CettaLoadedModule **records = realloc(
        ctx->loaded_modules, (size_t)next * sizeof(*records));
    if (!records)
        return false;
    ctx->loaded_modules = records;
    ctx->loaded_module_cap = next;
    return true;
}

void cetta_library_context_note_document_file(CettaLibraryContext *ctx, Space *space,
                                              const char *filename) {
    char resolved[PATH_MAX];
    uint32_t slot;

    if (!ctx || !space || !filename) return;
    if (!realpath(filename, resolved)) return;
    if (imported_file_lookup(ctx, space, resolved) >= 0) return;
    if (!library_reserve_imported_file(ctx)) return;
    slot = ctx->imported_file_len++;
    ctx->imported_files[slot].space = space;
    ctx->imported_files[slot].loading = false;
    snprintf(ctx->imported_files[slot].path,
             sizeof(ctx->imported_files[slot].path), "%s", resolved);
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

static const char *cetta_library_relative_base_dir(CettaLibraryContext *ctx) {
    if (!ctx) {
        return ".";
    }
    if (cetta_eval_session_relative_module_policy(&ctx->session) ==
            CETTA_RELATIVE_MODULE_POLICY_WORKING_DIR_ONLY) {
        /*
         * Upstream PeTTa calls this its working directory, but initializes
         * it to the directory of the top-level source file.  It remains
         * fixed while nested imports run.  Prefer the corresponding script
         * root here; the process cwd is only the inline/source-less fallback.
         */
        if (ctx->script_dir[0] != '\0')
            return ctx->script_dir;
        if (ctx->working_dir[0] != '\0')
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
    if (ctx && ctx->root_dir[0] != '\0' &&
        cetta_path_strip_prefix(path, ctx->root_dir, &relative)) {
        snprintf(out, out_sz, "%s", relative);
        return out;
    }
    if (ctx && ctx->working_dir[0] != '\0' &&
        cetta_path_strip_prefix(path, ctx->working_dir, &relative)) {
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
        if (ctx->import_space_aliases[i - 1].work_space == space)
            space = ctx->import_space_aliases[i - 1].logical_space;
    }
    return space;
}

Space *cetta_library_logical_module_space(CettaLibraryContext *ctx, Space *space) {
    return ctx ? logical_import_space(ctx, space) : space;
}

Space *cetta_library_import_execution_space(const CettaLibraryContext *ctx,
                                           Space *space) {
    if (!ctx || !space)
        return space;
    for (uint32_t i = 0u; i < ctx->import_space_alias_len; i++) {
        if (ctx->import_space_aliases[i].logical_space == space)
            space = ctx->import_space_aliases[i].work_space;
    }
    return space;
}

static bool cetta_library_push_import_alias(CettaLibraryContext *ctx,
                                            Space *work_space,
                                            Space *logical_space) {
    if (!library_reserve_import_alias(ctx))
        return false;
    Space *source = logical_import_space(ctx, logical_space);
    for (uint32_t i = 0u; i < ctx->module_own_view_len; i++) {
        if (ctx->module_own_views[i].source != source)
            continue;
        if (space_module_own_view_select(ctx->module_own_views[i].view, work_space))
            continue;
        /* No module code has run. Previously switched views still watch the
           enclosing storage, so rollback here cannot require allocation. */
        Space *previous = cetta_library_import_execution_space(ctx, source);
        for (uint32_t j = 0u; j < i; j++) {
            if (ctx->module_own_views[j].source != source)
                continue;
            bool restored = space_module_own_view_select(
                ctx->module_own_views[j].view, previous);
            assert(restored);
            space_module_own_view_release(ctx->module_own_views[j].view, work_space);
        }
        return false;
    }
    ctx->import_space_aliases[ctx->import_space_alias_len].work_space = work_space;
    ctx->import_space_aliases[ctx->import_space_alias_len].logical_space = logical_space;
    ctx->import_space_alias_len++;
    return true;
}

static void cetta_library_pop_import_alias(CettaLibraryContext *ctx) {
    if (ctx->import_space_alias_len > 0) {
        Space *work = ctx->import_space_aliases[ctx->import_space_alias_len - 1].work_space;
        Space *source = logical_import_space(ctx, work);
        ctx->import_space_alias_len--;
        Space *previous = cetta_library_import_execution_space(ctx, source);
        for (uint32_t i = 0u; i < ctx->module_own_view_len; i++) {
            if (ctx->module_own_views[i].source != source)
                continue;
            bool restored = space_module_own_view_select(
                ctx->module_own_views[i].view, previous);
            assert(restored);
            space_module_own_view_release(ctx->module_own_views[i].view, work);
        }
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

/* Reserve rollback records before HE module code can expose space handles.
 * Include all loaded slots so nested failures can retire without allocating. */
static bool library_reserve_module_retirement(CettaLibraryContext *ctx) {
    if (ctx->loaded_module_len > UINT32_MAX - ctx->retired_module_len)
        return false;
    uint32_t required = ctx->retired_module_len + ctx->loaded_module_len;
    if (required <= ctx->retired_module_cap)
        return true;
    uint32_t next = ctx->retired_module_cap ? ctx->retired_module_cap : 4u;
    while (next < required) {
        if (next > UINT32_MAX / 2u) {
            next = required;
            break;
        }
        next *= 2u;
    }
    if (sizeof(*ctx->retired_modules) > SIZE_MAX / (size_t)next)
        return false;
    void *grown = realloc(ctx->retired_modules,
                          sizeof(*ctx->retired_modules) * (size_t)next);
    if (!grown)
        return false;
    ctx->retired_modules = grown;
    ctx->retired_module_cap = next;
    return true;
}

/* Keep a failed HE module's space and name until the context ends. This
 * path cannot fail for lack of memory after a handle has escaped. */
static void library_retire_module_space(CettaLibraryContext *ctx,
                                        Space *space, char *module_name,
                                        Registry *tokens, const char *source_path) {
    if (ctx->retired_module_len >= ctx->retired_module_cap) {
        fputs("fatal: module rollback lost its reserved storage\n", stderr);
        abort();
    }
    ctx->retired_modules[ctx->retired_module_len].space = space;
    ctx->retired_modules[ctx->retired_module_len].module_name = module_name;
    ctx->retired_modules[ctx->retired_module_len].tokens = tokens;
    copy_parent_dir(ctx->retired_modules[ctx->retired_module_len].resource_dir,
                    sizeof(ctx->retired_modules[ctx->retired_module_len].resource_dir),
                    source_path);
    ctx->retired_module_len++;
}

static void discard_loaded_module(CettaLibraryContext *ctx,
                                  CettaLoadedModule *module) {
    cetta_foreign_he_module_release(module->provider);
    library_he_provider_exports_free(module->exports);
    if (module->space) {
        if (module->module_name) {
            /* Publication rollback does not erase storage retained by an
             * escaped callback or value. Its private identity stays live. */
            library_retire_module_space(ctx, module->space, module->module_name,
                                         module->tokens, module->canonical_path);
            module->module_name = NULL;
            module->tokens = NULL;
        } else {
            if (ctx->petta_program)
                petta_program_forget_space(ctx->petta_program, module->space);
            space_free(module->space);
            free(module->space);
        }
    }
    if (module->tokens) {
        registry_free(module->tokens);
        free(module->tokens);
    }
    free(module->module_name);
    free(module);
}

static void rollback_loaded_modules(CettaLibraryContext *ctx, uint32_t rollback_len) {
    while (ctx->loaded_module_len > rollback_len) {
        ctx->loaded_module_len--;
        discard_loaded_module(ctx, ctx->loaded_modules[ctx->loaded_module_len]);
        ctx->loaded_modules[ctx->loaded_module_len] = NULL;
    }
}

/* A default-profile failed initializer does not invalidate independently
 * completed children. Records are stable; only inventory pointers move. */
static void rollback_one_loaded_module(CettaLibraryContext *ctx,
                                       CettaLoadedModule *module) {
    for (uint32_t i = 0u; i < ctx->imported_file_len;) {
        if (ctx->imported_files[i].space != module->space) {
            i++;
            continue;
        }
        ctx->imported_file_len--;
        memmove(&ctx->imported_files[i], &ctx->imported_files[i + 1u],
                (size_t)(ctx->imported_file_len - i) * sizeof(*ctx->imported_files));
    }
    for (uint32_t i = 0u; i < ctx->loaded_module_len; i++) {
        if (ctx->loaded_modules[i] != module)
            continue;
        ctx->loaded_module_len--;
        memmove(&ctx->loaded_modules[i], &ctx->loaded_modules[i + 1u],
                (size_t)(ctx->loaded_module_len - i) * sizeof(*ctx->loaded_modules));
        ctx->loaded_modules[ctx->loaded_module_len] = NULL;
        discard_loaded_module(ctx, module);
        return;
    }
}

static bool module_provider_visible(const CettaLibraryContext *ctx,
                                    CettaModuleProviderKind provider_kind) {
    CettaModuleProviderFlags flag = cetta_module_provider_flag(provider_kind);
    return ctx && flag != 0 &&
           cetta_language_allows_provider_kind(ctx->session.language_id,
                                              ctx->session.profile,
                                              provider_kind) &&
           cetta_module_policy_allows(&ctx->session.module_policy, flag);
}

static bool module_mount_visible(const CettaLibraryContext *ctx,
                                 const CettaModuleMount *mount) {
    return ctx && mount &&
           module_provider_visible(ctx, mount->provider_kind) &&
           cetta_language_visible_in(ctx->session.language_id,
                                     ctx->session.profile,
                                     mount->profile_visibility_mask);
}

static bool loaded_module_visible(const CettaLibraryContext *ctx,
                                  const CettaLoadedModule *module) {
    return ctx && module && module_provider_visible(ctx, module->provider_kind);
}

static CettaModuleMount *module_mount_lookup_mutable(CettaLibraryContext *ctx,
                                                     const char *namespace_name) {
    for (uint32_t i = 0; i < ctx->module_mount_len; i++) {
        if (strcmp(ctx->module_mounts[i]->namespace_name, namespace_name) == 0) {
            return ctx->module_mounts[i];
        }
    }
    return NULL;
}

static const CettaModuleMount *module_mount_lookup_any(const CettaLibraryContext *ctx,
                                                       const char *namespace_name) {
    for (uint32_t i = 0; i < ctx->module_mount_len; i++) {
        if (strcmp(ctx->module_mounts[i]->namespace_name, namespace_name) == 0) {
            return ctx->module_mounts[i];
        }
    }
    return NULL;
}

static void petta_library_path_revision_advance(
    CettaPettaLibraryPathRelation *relation) {
    if (!relation)
        return;
    relation->revision = relation->revision == UINT64_MAX
        ? 1u : relation->revision + 1u;
}

static bool petta_library_path_relation_reserve(
    CettaPettaLibraryPathRelation *relation, uint32_t needed) {
    if (!relation)
        return false;
    if (needed <= relation->cap)
        return true;
    uint32_t capacity = relation->cap ? relation->cap : 8u;
    while (capacity < needed) {
        if (capacity > UINT32_MAX / 2u)
            return false;
        capacity *= 2u;
    }
    if ((size_t)capacity >
        SIZE_MAX / sizeof(*relation->items)) {
        return false;
    }
    relation->items = cetta_realloc(
        relation->items,
        (size_t)capacity * sizeof(*relation->items));
    relation->cap = capacity;
    return true;
}

bool cetta_library_petta_library_path_apply(
    CettaLibraryContext *ctx,
    const PeTTaLibraryPathEffect *effect) {
    if (!ctx || !effect || !effect->path || effect->path[0] == '\0' ||
        strlen(effect->path) >= PATH_MAX ||
        ctx->session.language_id != CETTA_LANGUAGE_PETTA) {
        return false;
    }
    CettaPettaLibraryPathRelation *relation = &ctx->petta_library_paths;
    switch (effect->kind) {
    case PETTA_LIBRARY_PATH_EFFECT_PREPEND:
    case PETTA_LIBRARY_PATH_EFFECT_APPEND: {
        if (relation->len == UINT32_MAX ||
            !petta_library_path_relation_reserve(
                relation, relation->len + 1u)) {
            return false;
        }
        uint32_t index = relation->len;
        if (effect->kind == PETTA_LIBRARY_PATH_EFFECT_PREPEND) {
            memmove(
                relation->items + 1u, relation->items,
                (size_t)relation->len * sizeof(*relation->items));
            index = 0u;
        }
        snprintf(
            relation->items[index].path,
            sizeof(relation->items[index].path),
            "%s", effect->path);
        relation->len++;
        petta_library_path_revision_advance(relation);
        return true;
    }
    case PETTA_LIBRARY_PATH_EFFECT_RETRACT_FIRST:
        for (uint32_t index = 0u; index < relation->len; index++) {
            if (strcmp(relation->items[index].path, effect->path) != 0)
                continue;
            memmove(
                relation->items + index,
                relation->items + index + 1u,
                (size_t)(relation->len - index - 1u) *
                    sizeof(*relation->items));
            relation->len--;
            petta_library_path_revision_advance(relation);
            return true;
        }
        return false;
    case PETTA_LIBRARY_PATH_EFFECT_NONE:
        return false;
    }
    return false;
}

uint32_t cetta_library_petta_library_path_count(
    const CettaLibraryContext *ctx) {
    return ctx && ctx->session.language_id == CETTA_LANGUAGE_PETTA
        ? ctx->petta_library_paths.len : 0u;
}

const char *cetta_library_petta_library_path_at(
    const CettaLibraryContext *ctx, uint32_t index) {
    return ctx && ctx->session.language_id == CETTA_LANGUAGE_PETTA &&
           index < ctx->petta_library_paths.len
        ? ctx->petta_library_paths.items[index].path : NULL;
}

static bool petta_library_path_has_suffix(
    const char *path, const char *suffix) {
    if (!path || !suffix)
        return false;
    size_t path_len = strlen(path);
    size_t suffix_len = strlen(suffix);
    return suffix_len <= path_len &&
           memcmp(path + path_len - suffix_len, suffix, suffix_len) == 0;
}

bool cetta_library_petta_root_path_at(
    const CettaLibraryContext *ctx, const char *root,
    uint32_t occurrence, char *path, size_t path_size) {
    if (path && path_size > 0u)
        path[0] = '\0';
    if (!ctx || ctx->session.language_id != CETTA_LANGUAGE_PETTA ||
        !root || !path || path_size == 0u) {
        return false;
    }
    for (uint32_t index = 0u;
         index < ctx->petta_library_paths.len; index++) {
        const char *candidate =
            ctx->petta_library_paths.items[index].path;
        if (!petta_library_path_has_suffix(candidate, root))
            continue;
        if (occurrence != 0u) {
            occurrence--;
            continue;
        }
        if (strlen(candidate) >= path_size)
            return false;
        memcpy(path, candidate, strlen(candidate) + 1u);
        return true;
    }
    return false;
}

static bool path_join2(
    char *out, size_t out_sz,
    const char *lhs, const char *rhs);
static bool resolve_standard_library_reference(
    CettaLibraryContext *ctx, const char *member,
    char *canonical_path, size_t canonical_path_size);

/* Render the one standard-library descriptor denoted by PeTTa's library/2.
 * An available member is rendered through the selected language overlay so
 * the descriptor remains usable as a direct import path.  Absence is still a
 * valid descriptor observation: SWI constructs this path before import!
 * performs its later existence check. */
static bool render_standard_library_reference(
    CettaLibraryContext *ctx, const char *member,
    char *path, size_t path_size) {
    if (!ctx || !member || !path || path_size == 0u)
        return false;

    char canonical[PATH_MAX];
    if (resolve_standard_library_reference(
            ctx, member, canonical, sizeof(canonical))) {
        size_t length = strlen(canonical);
        size_t suffix = strlen(".metta");
        bool authored_suffix =
            petta_library_path_has_suffix(member, ".metta");
        if (!authored_suffix && length >= suffix &&
            petta_library_path_has_suffix(canonical, ".metta")) {
            length -= suffix;
        }
        if (length >= path_size)
            return false;
        memcpy(path, canonical, length);
        path[length] = '\0';
        return true;
    }

    const char *language_name =
        cetta_language_canonical_name(ctx->session.language_id);
    int written = ctx->root_dir[0] != '\0'
        ? snprintf(path, path_size, "%s/lib/%s/%s",
                   ctx->root_dir, language_name, member)
        : snprintf(path, path_size, "lib/%s/%s",
                   language_name, member);
    if (written <= 0 || (size_t)written >= path_size) {
        path[0] = '\0';
        return false;
    }
    return true;
}

static CettaPettaLibraryReferenceObservation
petta_library_reference_observation(
    CettaPettaLibraryReferenceObservationKind kind,
    const char *reason) {
    CettaPettaLibraryReferenceObservation observation = {
        .kind = kind,
        .reason = reason,
    };
    return observation;
}

CettaPettaLibraryReferenceObservation
cetta_library_petta_reference_observe(
    CettaLibraryContext *ctx,
    const PeTTaLibraryReference *reference,
    uint32_t occurrence, char *path, size_t path_size) {
    if (path && path_size > 0u)
        path[0] = '\0';
    if (!ctx || !reference || !path || path_size == 0u) {
        return petta_library_reference_observation(
            CETTA_PETTA_LIBRARY_REFERENCE_REFUSED,
            "invalid library reference observation");
    }
    if (reference->kind == PETTA_LIBRARY_REFERENCE_ROOTED) {
        if (!reference->root || !reference->member) {
            return petta_library_reference_observation(
                CETTA_PETTA_LIBRARY_REFERENCE_REFUSED,
                "invalid rooted library reference");
        }
        if (!cetta_module_policy_allows(
                &ctx->session.module_policy,
                CETTA_MODULE_PROVIDER_REGISTERED_ROOTS)) {
            return petta_library_reference_observation(
                CETTA_PETTA_LIBRARY_REFERENCE_REFUSED,
                "registered module roots disabled");
        }
        char root_path[PATH_MAX];
        if (!cetta_library_petta_root_path_at(
                ctx, reference->root, occurrence,
                root_path, sizeof(root_path))) {
            return petta_library_reference_observation(
                CETTA_PETTA_LIBRARY_REFERENCE_EXHAUSTED, NULL);
        }
        if (!path_join2(
                path, path_size, root_path, reference->member)) {
            return petta_library_reference_observation(
                CETTA_PETTA_LIBRARY_REFERENCE_REFUSED,
                "library reference path too long");
        }
        return petta_library_reference_observation(
            CETTA_PETTA_LIBRARY_REFERENCE_YIELD, NULL);
    }
    if (reference->kind != PETTA_LIBRARY_REFERENCE_STANDARD ||
        reference->root != NULL) {
        return petta_library_reference_observation(
            CETTA_PETTA_LIBRARY_REFERENCE_REFUSED,
            "invalid library reference");
    }
    if (!reference->member) {
        return petta_library_reference_observation(
            CETTA_PETTA_LIBRARY_REFERENCE_REFUSED,
            "invalid standard library reference");
    }
    if (!cetta_module_policy_allows(
            &ctx->session.module_policy,
            CETTA_MODULE_PROVIDER_STDLIB)) {
        return petta_library_reference_observation(
            CETTA_PETTA_LIBRARY_REFERENCE_REFUSED,
            "standard library disabled");
    }
    if (occurrence != 0u) {
        return petta_library_reference_observation(
            CETTA_PETTA_LIBRARY_REFERENCE_EXHAUSTED, NULL);
    }

    if (!render_standard_library_reference(
            ctx, reference->member, path, path_size)) {
        return petta_library_reference_observation(
            CETTA_PETTA_LIBRARY_REFERENCE_REFUSED,
            "library reference path too long");
    }
    return petta_library_reference_observation(
        CETTA_PETTA_LIBRARY_REFERENCE_YIELD, NULL);
}

uint32_t cetta_library_module_mount_count(const CettaLibraryContext *ctx) {
    uint32_t count = 0;
    if (!ctx) return 0;
    for (uint32_t i = 0; i < ctx->module_mount_len; i++) {
        if (module_mount_visible(ctx, ctx->module_mounts[i])) {
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
        const CettaModuleMount *mount = ctx->module_mounts[i];
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
        if (loaded_module_visible(ctx, ctx->loaded_modules[i])) {
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
        const CettaLoadedModule *module = ctx->loaded_modules[i];
        if (!loaded_module_visible(ctx, module)) continue;
        if (visible == index) return module;
        visible++;
    }
    return NULL;
}

static bool cetta_library_push_dir(CettaLibraryContext *ctx, const char *dir) {
    if (ctx->import_dir_len == ctx->import_dir_cap) {
        if (ctx->import_dir_len == UINT32_MAX)
            return false;
        uint32_t next = ctx->import_dir_cap
            ? (ctx->import_dir_cap > UINT32_MAX / 2u
                ? UINT32_MAX : ctx->import_dir_cap * 2u) : 8u;
        if ((size_t)next > SIZE_MAX / sizeof(*ctx->import_dirs))
            return false;
        void *grown = realloc(ctx->import_dirs,
            (size_t)next * sizeof(*ctx->import_dirs));
        if (!grown)
            return false;
        ctx->import_dirs = grown;
        ctx->import_dir_cap = next;
    }
    snprintf(ctx->import_dirs[ctx->import_dir_len],
             sizeof(ctx->import_dirs[ctx->import_dir_len]), "%s", dir);
    ctx->import_dir_len++;
    return true;
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

    const char *slash = strrchr(trimmed, '/');
    const char *colon = strrchr(trimmed, ':');
    const char *base = slash;
    if (colon && (!base || colon > base))
        base = colon;
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

/* Helper launches (git, build commands) go through the shared child setup
 * (proc_child.h): nothing allocates after fork, every descriptor but the
 * output pipe is closed on exec, signals are reset and stdin reads
 * /dev/null; the child stays in the caller's process group.  The program is
 * found on PATH and the environment built before the fork. */
extern char **environ;

static bool library_find_program(const char *name, char *out,
                                 size_t out_size) {
    if (strchr(name, '/')) {
        int n = snprintf(out, out_size, "%s", name);
        return n > 0 && (size_t)n < out_size && access(out, X_OK) == 0;
    }
    const char *path = getenv("PATH");
    if (!path || !*path)
        path = "/usr/local/bin:/usr/bin:/bin";
    for (const char *entry = path;;) {
        const char *colon = strchr(entry, ':');
        size_t length = colon ? (size_t)(colon - entry) : strlen(entry);
        int n = length
            ? snprintf(out, out_size, "%.*s/%s", (int)length, entry, name)
            : snprintf(out, out_size, "./%s", name);
        if (n > 0 && (size_t)n < out_size && access(out, X_OK) == 0)
            return true;
        if (!colon)
            return false;
        entry = colon + 1;
    }
}

/* The inherited environment with `extra` ("NAME=VALUE") in place of any
 * entry of that name.  The caller frees the array, not its strings. */
static char **library_child_environment(const char *extra) {
    size_t name_length = extra ? (size_t)(strchr(extra, '=') - extra) : 0u;
    size_t count = 0u;
    while (environ && environ[count])
        count++;
    char **env = malloc((count + 2u) * sizeof(*env));
    if (!env)
        return NULL;
    size_t kept = 0u;
    for (size_t i = 0u; i < count; i++) {
        if (extra && strncmp(environ[i], extra, name_length + 1u) == 0)
            continue;
        env[kept++] = environ[i];
    }
    if (extra)
        env[kept++] = (char *)extra;
    env[kept] = NULL;
    return env;
}

/* Start `name` with `argv` in `cwd`, its output and errors into one pipe.
 * Answers the child, or -1 with *not_found set when `name` is not on PATH.
 * The child's setup report is read after reaping (library_reap_captured). */
static pid_t library_spawn_captured(const char *name, char *argv[],
                                    const char *cwd, const char *extra_env,
                                    int *output_out, int *report_out,
                                    bool *not_found_out) {
    *output_out = *report_out = -1;
    *not_found_out = false;
    char program[PATH_MAX];
    if (!library_find_program(name, program, sizeof program)) {
        *not_found_out = true;
        return -1;
    }
    char **env = library_child_environment(extra_env);
    if (!env)
        return -1;
    int output[2] = {-1, -1}, report[2] = {-1, -1};
    if (pipe2(output, O_CLOEXEC) != 0 ||
        pipe2(report, O_CLOEXEC | O_NONBLOCK) != 0) {
        if (output[0] >= 0) { close(output[0]); close(output[1]); }
        free(env);
        return -1;
    }
    sigset_t blocked, previous;
    sigfillset(&blocked);
    pthread_sigmask(SIG_SETMASK, &blocked, &previous);
    pid_t pid = fork();
    if (pid == 0)
        cetta_proc_child_exec_group(program, argv, env, cwd ? cwd : ".",
                                    -1, output[1], output[1], report[1], 0);
    pthread_sigmask(SIG_SETMASK, &previous, NULL);
    free(env);
    close(output[1]);
    close(report[1]);
    if (pid < 0) {
        close(output[0]);
        close(report[0]);
        return -1;
    }
    *output_out = output[0];
    *report_out = report[0];
    return pid;
}

/* Reap `pid`; a setup failure the child reported reads as the exit codes
 * a plain fork's child gave: 126 when its directory was unavailable, 127
 * otherwise. */
static bool library_reap_captured(pid_t pid, int report, int *status_out) {
    int status = 0;
    pid_t waited;
    do waited = waitpid(pid, &status, 0); while (waited < 0 && errno == EINTR);
    int stopped[2] = {0, 0};
    ssize_t count = 0;
    if (waited == pid)
        do count = read(report, stopped, sizeof stopped);
        while (count < 0 && errno == EINTR);
    close(report);
    if (waited != pid)
        return false;
    if (count == (ssize_t)sizeof stopped)
        status = (stopped[0] == CETTA_PROC_CHILD_CWD ? 126 : 127) << 8;
    *status_out = status;
    return true;
}

/* Run git with `argv`, its output and errors into `errbuf`; false, with the
 * reason in `errbuf`, when git could not be started or reaped. */
static bool library_git_captured(char *argv[], char *errbuf,
                                 size_t errbuf_sz, int *status_out) {
    int output = -1, report = -1;
    bool not_found = false;
    pid_t pid = library_spawn_captured("git", argv, NULL,
                                       "GIT_TERMINAL_PROMPT=0",
                                       &output, &report, &not_found);
    if (pid < 0) {
        if (errbuf_sz > 0)
            snprintf(errbuf, errbuf_sz, not_found
                ? "git executable not found"
                : "git-module! failed to spawn git");
        return false;
    }
    bool output_ok = cetta_proc_capture_prefix_and_drain(output, errbuf, errbuf_sz);
    close(output);
    if (!library_reap_captured(pid, report, status_out)) {
        if (errbuf_sz > 0)
            snprintf(errbuf, errbuf_sz, "git-module! failed to wait for git");
        return false;
    }
    if (!output_ok) {
        if (errbuf_sz > 0)
            snprintf(errbuf, errbuf_sz, "git-module! failed to read git output");
        return false;
    }
    return true;
}

static bool run_git_clone(const char *url, const char *dst,
                          char *errbuf, size_t errbuf_sz) {
    char *argv[] = {
        "git", "-c", "protocol.file.allow=always",
        "clone", "--depth", "1",
        (char *)url, (char *)dst,
        NULL
    };
    int status = 0;
    if (!library_git_captured(argv, errbuf, errbuf_sz, &status))
        return false;
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

static bool run_petta_build_command(const char *repo_path,
                                    const char *build_command,
                                    char *errbuf, size_t errbuf_sz) {
    pid_t pid;

    if (!build_command || !*build_command)
        return true;
    char *argv[] = {"sh", (char *)build_command, NULL};
    int output = -1, report = -1;
    bool not_found = false;
    pid = library_spawn_captured("sh", argv, repo_path, NULL,
                                 &output, &report, &not_found);
    if (pid < 0) {
        if (errbuf_sz > 0)
            snprintf(errbuf, errbuf_sz, not_found
                ? "git-import! shell executable not found"
                : "git-import! failed to spawn build");
        return false;
    }

    bool output_ok = cetta_proc_capture_prefix_and_drain(output, errbuf, errbuf_sz);
    close(output);

    int status = 0;
    if (!library_reap_captured(pid, report, &status)) {
        if (errbuf_sz > 0)
            snprintf(errbuf, errbuf_sz,
                     "git-import! failed to wait for build");
        return false;
    }
    if (!output_ok) {
        if (errbuf_sz > 0)
            snprintf(errbuf, errbuf_sz, "git-import! failed to read build output");
        return false;
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
        return true;
    if (errbuf_sz > 0) {
        if (WIFEXITED(status) && WEXITSTATUS(status) == 126) {
            snprintf(errbuf, errbuf_sz,
                     "git-import! build directory unavailable");
        } else if (WIFEXITED(status) && WEXITSTATUS(status) == 127) {
            snprintf(errbuf, errbuf_sz,
                     "git-import! shell executable not found");
        } else {
            snprintf(errbuf, errbuf_sz,
                     "git-import! build step failed");
        }
    }
    return false;
}

static void trim_git_output(char *output) {
    if (!output)
        return;
    size_t length = strlen(output);
    while (length > 0u &&
           isspace((unsigned char)output[length - 1u])) {
        output[--length] = '\0';
    }
}

static bool run_git_arguments(
    const char *repo_path,
    const char *const *arguments, size_t argument_count,
    char *output, size_t output_size) {
    enum { MAX_GIT_ARGUMENTS = 16 };
    if (!arguments || argument_count > MAX_GIT_ARGUMENTS) {
        if (output && output_size > 0u)
            snprintf(output, output_size, "invalid git argument vector");
        return false;
    }

    char *argv[MAX_GIT_ARGUMENTS + 8u];
    size_t argc = 0u;
    argv[argc++] = (char *)"git";
    argv[argc++] = (char *)"-c";
    argv[argc++] = (char *)"protocol.file.allow=always";
    if (repo_path) {
        argv[argc++] = (char *)"-C";
        argv[argc++] = (char *)repo_path;
    }
    for (size_t index = 0u; index < argument_count; index++)
        argv[argc++] = (char *)arguments[index];
    argv[argc] = NULL;
    int pipe_out = -1, report = -1;
    bool not_found = false;
    pid_t pid = library_spawn_captured("git", argv, NULL,
                                       "GIT_TERMINAL_PROMPT=0",
                                       &pipe_out, &report, &not_found);
    if (pid < 0) {
        if (output && output_size > 0u)
            snprintf(output, output_size, not_found
                ? "git executable not found" : "failed to spawn git");
        return false;
    }

    if (output && output_size > 0u)
        output[0] = '\0';
    size_t used = 0u;
    char chunk[512];
    ssize_t received;
    while ((received = read(pipe_out, chunk, sizeof(chunk))) > 0) {
        if (output && output_size > used + 1u) {
            size_t available = output_size - used - 1u;
            size_t copied = (size_t)received < available
                ? (size_t)received : available;
            memcpy(output + used, chunk, copied);
            used += copied;
            output[used] = '\0';
        }
    }
    close(pipe_out);

    int status = 0;
    if (!library_reap_captured(pid, report, &status)) {
        if (output && output_size > 0u)
            snprintf(output, output_size, "failed to wait for git");
        return false;
    }
    trim_git_output(output);
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
        return true;
    if (output && output_size > 0u && output[0] == '\0') {
        snprintf(output, output_size,
                 WIFEXITED(status) && WEXITSTATUS(status) == 127
                     ? "git executable not found"
                     : "git command failed");
    }
    return false;
}

static bool normalize_full_git_commit_sha(
    const char *source, char normalized[41]) {
    if (!source || strlen(source) != 40u)
        return false;
    for (size_t index = 0u; index < 40u; index++) {
        unsigned char byte = (unsigned char)source[index];
        if (!isxdigit(byte))
            return false;
        normalized[index] = (char)tolower(byte);
    }
    normalized[40] = '\0';
    return true;
}

static bool petta_git_verify_head(
    const char *repo_path, const char *commit_sha,
    char *detail, size_t detail_size) {
    const char *arguments[] = {
        "rev-parse", "--verify", "HEAD^{commit}"
    };
    return run_git_arguments(
               repo_path, arguments, 3u, detail, detail_size) &&
           strcmp(detail, commit_sha) == 0;
}

static bool petta_git_checkout_commit(
    const char *repo_path, const char *commit_sha,
    char *detail, size_t detail_size) {
    const char *fetch_arguments[] = {
        "fetch", "--no-tags", "origin", commit_sha
    };
    if (!run_git_arguments(
            repo_path, fetch_arguments, 4u,
            detail, detail_size)) {
        return false;
    }
    char commit_object[64];
    int written = snprintf(
        commit_object, sizeof(commit_object),
        "%s^{commit}", commit_sha);
    if (!(written > 0 && (size_t)written < sizeof(commit_object)))
        return false;
    const char *resolve_arguments[] = {
        "cat-file", "-e", commit_object
    };
    if (!run_git_arguments(
            repo_path, resolve_arguments, 3u,
            detail, detail_size)) {
        return false;
    }
    const char *checkout_arguments[] = {
        "checkout", "--detach", commit_sha
    };
    if (!run_git_arguments(
            repo_path, checkout_arguments, 3u,
            detail, detail_size)) {
        return false;
    }
    return petta_git_verify_head(
        repo_path, commit_sha, detail, detail_size);
}

static bool petta_git_existing_checkout_is_admissible(
    const char *repo_path, const char *git_path,
    char *detail, size_t detail_size) {
    const char *inside_arguments[] = {
        "rev-parse", "--is-inside-work-tree"
    };
    if (!run_git_arguments(
            repo_path, inside_arguments, 2u,
            detail, detail_size) ||
        strcmp(detail, "true") != 0) {
        return false;
    }
    const char *origin_arguments[] = {
        "remote", "get-url", "origin"
    };
    return run_git_arguments(
               repo_path, origin_arguments, 3u,
               detail, detail_size) &&
           strcmp(detail, git_path) == 0;
}

static bool petta_git_checkout_is_clean(
    const char *repo_path, char *detail, size_t detail_size) {
    const char *arguments[] = {
        "status", "--porcelain", "--untracked-files=all"
    };
    return run_git_arguments(
               repo_path, arguments, 3u,
               detail, detail_size) &&
           detail[0] == '\0';
}

static bool ensure_petta_pinned_git_checkout(
    const char *git_path, const char *build_command,
    const char *base_resolved, const char *module_name,
    const char *local_path, const char *commit_sha,
    Arena *eval_arena, Atom **error_out) {
    char lock_path[PATH_MAX];
    char temporary_path[PATH_MAX];
    char detail[512];
    char build_error[256];
    struct stat st;

    detail[0] = '\0';
    build_error[0] = '\0';

    int written = snprintf(
        lock_path, sizeof(lock_path), "%s.lock", local_path);
    if (!(written > 0 && (size_t)written < sizeof(lock_path))) {
        *error_out = atom_symbol(
            eval_arena, "git-import! lock path too long");
        return false;
    }
    int lock_descriptor = open(
        lock_path, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (lock_descriptor < 0 || flock(lock_descriptor, LOCK_EX) != 0) {
        if (lock_descriptor >= 0)
            close(lock_descriptor);
        *error_out = atom_symbol(
            eval_arena, "git-import! cannot lock repository");
        return false;
    }

    bool ok = false;
    if (stat(local_path, &st) == 0) {
        if (!S_ISDIR(st.st_mode) ||
            !petta_git_existing_checkout_is_admissible(
                local_path, git_path, detail, sizeof(detail))) {
            *error_out = atom_symbol(
                eval_arena,
                "git-import! existing target is not the requested checkout");
            goto done;
        }
        if (petta_git_verify_head(
                local_path, commit_sha,
                detail, sizeof(detail))) {
            ok = true;
            goto done;
        }
        if (!petta_git_checkout_is_clean(
                local_path, detail, sizeof(detail))) {
            *error_out = atom_symbol(
                eval_arena,
                "git-import! existing checkout is dirty");
            goto done;
        }
        if (!petta_git_checkout_commit(
                local_path, commit_sha,
                detail, sizeof(detail))) {
            *error_out = atom_symbol(
                eval_arena,
                "git-import! cannot select requested commit");
            goto done;
        }
        if (!run_petta_build_command(
                local_path, build_command,
                build_error, sizeof(build_error))) {
            *error_out = atom_symbol(
                eval_arena,
                build_error[0]
                    ? build_error : "git-import! build failed");
            goto done;
        }
        ok = true;
        goto done;
    }
    if (errno != ENOENT) {
        *error_out = atom_symbol(
            eval_arena, "git-import! repository path unavailable");
        goto done;
    }

    written = snprintf(
        temporary_path, sizeof(temporary_path),
        "%s/.%s.git-import.XXXXXX",
        base_resolved, module_name);
    if (!(written > 0 && (size_t)written < sizeof(temporary_path)) ||
        !mkdtemp(temporary_path)) {
        *error_out = atom_symbol(
            eval_arena, "git-import! temporary path unavailable");
        goto done;
    }
    const char *clone_arguments[] = {
        "clone", "--no-checkout", git_path, temporary_path
    };
    if (!run_git_arguments(
            NULL, clone_arguments, 4u,
            detail, sizeof(detail)) ||
        !petta_git_checkout_commit(
            temporary_path, commit_sha,
            detail, sizeof(detail)) ||
        !run_petta_build_command(
            temporary_path, build_command,
            build_error, sizeof(build_error))) {
        (void)remove_tree_recursive(temporary_path);
        *error_out = atom_symbol(
            eval_arena,
            build_error[0]
                ? build_error
                : "git-import! cannot prepare requested commit");
        goto done;
    }
    if (rename(temporary_path, local_path) != 0) {
        (void)remove_tree_recursive(temporary_path);
        *error_out = atom_symbol(
            eval_arena, "git-import! repository publication failed");
        goto done;
    }
    ok = true;

done:
    (void)flock(lock_descriptor, LOCK_UN);
    close(lock_descriptor);
    return ok;
}

static bool run_git_try_fetch_latest(const char *repo_path,
                                     char *errbuf, size_t errbuf_sz) {
    char *fetch[] = {
        "git", "-C", (char *)repo_path,
        "-c", "protocol.file.allow=always",
        "fetch", "--depth", "1", "origin",
        NULL
    };
    int status = 0;
    if (!library_git_captured(fetch, errbuf, errbuf_sz, &status) ||
        !(WIFEXITED(status) && WEXITSTATUS(status) == 0))
        return false;
    char *reset[] = {
        "git", "-C", (char *)repo_path,
        "reset", "--hard", "FETCH_HEAD",
        NULL
    };
    return library_git_captured(reset, errbuf, errbuf_sz, &status) &&
           WIFEXITED(status) && WEXITSTATUS(status) == 0;
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
    if (strlen(namespace_name) > NAME_MAX) {
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

    if (!library_reserve_module_mount(ctx)) {
        *error_out = atom_symbol(eval_arena, "module root inventory allocation failed");
        return false;
    }
    CettaModuleMount *mount = calloc(1u, sizeof(*mount));
    if (!mount) {
        *error_out = atom_symbol(eval_arena, "module root record allocation failed");
        return false;
    }
    ctx->module_mounts[ctx->module_mount_len++] = mount;
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

/* A successful PeTTa package registration has two synchronized views: the
 * module-mount provenance record and one authored library_path occurrence.
 * Reserve the only fallible root storage first, then publish both views as
 * one transaction.  No failed registration may leave a mount that PeTTa's
 * ordered resolver cannot observe, or vice versa. */
static bool commit_module_mount_and_petta_root(
    CettaLibraryContext *ctx, const char *namespace_name,
    const char *root_path,
    CettaModuleProviderKind provider_kind,
    CettaModuleLocatorKind locator_kind,
    const char *source_locator,
    CettaRemoteRevisionPolicy revision_policy,
    const char *revision_value,
    uint32_t visibility_mask,
    Arena *eval_arena, Atom **error_out) {
    if (!ctx)
        return false;

    bool publish_root =
        ctx->session.language_id == CETTA_LANGUAGE_PETTA;
    if (publish_root) {
        CettaPettaLibraryPathRelation *relation =
            &ctx->petta_library_paths;
        if (relation->len == UINT32_MAX ||
            !petta_library_path_relation_reserve(
                relation, relation->len + 1u)) {
            *error_out = atom_symbol(
                eval_arena, "library path registration failed");
            return false;
        }
    }

    CettaModuleMount *existing =
        module_mount_lookup_mutable(ctx, namespace_name);
    uint32_t previous_mount_len = ctx->module_mount_len;
    CettaModuleMount previous_mount;
    if (existing)
        previous_mount = *existing;

    if (!upsert_module_mount(
            ctx, namespace_name, root_path,
            provider_kind, locator_kind, source_locator,
            revision_policy, revision_value,
            visibility_mask, eval_arena, error_out)) {
        return false;
    }
    if (!publish_root)
        return true;

    PeTTaLibraryPathEffect effect = {
        .kind = PETTA_LIBRARY_PATH_EFFECT_PREPEND,
        .path = root_path,
    };
    if (cetta_library_petta_library_path_apply(ctx, &effect))
        return true;

    if (existing) {
        *existing = previous_mount;
    } else {
        ctx->module_mount_len = previous_mount_len;
        free(ctx->module_mounts[previous_mount_len]);
        ctx->module_mounts[previous_mount_len] = NULL;
    }
    *error_out = atom_symbol(
        eval_arena, "library path registration failed");
    return false;
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

/* HE finds a single file NAME.metta before a directory NAME, and a directory
 * is a module with or without a module.metta. */
static bool resolve_module_candidate_he_metta(const char *candidate,
                                              char *out, size_t out_sz) {
    char path[PATH_MAX];
    struct stat st;
    int n = snprintf(path, sizeof(path),
                     path_has_suffix(candidate, ".metta") ? "%s" : "%s.metta",
                     candidate);
    if (!(n > 0 && (size_t)n < sizeof(path)))
        return false;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        if (stat(candidate, &st) != 0 || !S_ISDIR(st.st_mode))
            return false;
        n = snprintf(path, sizeof(path), "%s/module.metta", candidate);
        if (!(n > 0 && (size_t)n < sizeof(path)))
            return false;
        if (access(path, R_OK) != 0)
            snprintf(path, sizeof(path), "%s", candidate);
    }
    if (access(path, R_OK) != 0 || !realpath(path, out))
        return false;
    return strlen(out) < out_sz;
}

static bool resolve_module_candidate_with_format(CettaLibraryContext *ctx,
                                                 const char *candidate,
                                                 char *out, size_t out_sz,
                                                 CettaModuleFormat *format_out,
                                                 char *reason, size_t reason_sz) {
    /* HE's Python host registers its filesystem format before the built-in
       MeTTa formats. Keep that preference local to HE; other dialects retain
       their established source lookup. */
    bool foreign_first = ctx && ctx->session.language_id == CETTA_LANGUAGE_HE;
    char foreign_reason[160] = {0};
    if (foreign_first &&
        cetta_foreign_resolve_candidate(candidate, out, out_sz, format_out,
                                         foreign_reason, sizeof(foreign_reason)))
        return true;
    char metta_reason[160] = {0};
    if (foreign_first
            ? resolve_module_candidate_he_metta(candidate, out, out_sz)
            : resolve_module_candidate_metta(candidate, out, out_sz,
                                             metta_reason, sizeof(metta_reason))) {
        if (format_out) {
            format_out->kind = CETTA_MODULE_FORMAT_METTA;
            format_out->foreign_backend = CETTA_FOREIGN_BACKEND_NONE;
        }
        if (reason && reason_sz > 0) reason[0] = '\0';
        return true;
    }

    if (!foreign_first && cetta_foreign_resolve_candidate(candidate, out, out_sz,
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

/* Explicit imports use a language spelling of a shared library when it
 * exists. Generated Prime spellings retain the shared source's bindings
 * under lexical-fresh; HE and PeTTa keep their own sources. */
static void prefer_language_spelling(CettaLibraryContext *ctx,
                                     char *out, size_t out_sz,
                                     const CettaModuleFormat *format) {
    char candidate[PATH_MAX];
    char resolved[PATH_MAX];
    int n;
    if (!ctx || (format && format->kind != CETTA_MODULE_FORMAT_METTA)) return;
    if (ctx->session.language_id == CETTA_LANGUAGE_PRIME) {
        char root[PATH_MAX], canonical[PATH_MAX];
        const char *relative = NULL;
        n = snprintf(root, sizeof(root), "%s/lib", ctx->root_dir[0] ? ctx->root_dir : ".");
        if (n <= 0 || (size_t)n >= sizeof(root) || !realpath(root, canonical) ||
            !cetta_path_strip_prefix(out, canonical, &relative) ||
            strncmp(relative, "prime/", 6u) == 0 ||
            strncmp(relative, "petta/", 6u) == 0) return;
        n = snprintf(candidate, sizeof(candidate), "%s/prime/%s", canonical, relative);
    } else if (ctx->session.language_id == CETTA_LANGUAGE_PETTA) {
        const char *slash = strrchr(out, '/');
        if (!slash) return;
        size_t dir_len = (size_t)(slash - out);
        if (dir_len < 3u || strncmp(out + dir_len - 3u, "lib", 3u) != 0 ||
            (dir_len > 3u && out[dir_len - 4u] != '/')) return;
        n = snprintf(candidate, sizeof(candidate), "%.*s/petta/%s",
                     (int)dir_len, out, slash + 1);
    } else return;
    if (n > 0 && (size_t)n < sizeof(candidate) &&
        access(candidate, R_OK) == 0 && realpath(candidate, resolved) &&
        strlen(resolved) < out_sz)
        snprintf(out, out_sz, "%s", resolved);
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

    if (reason && reason_sz > 0) {
        reason[0] = '\0';
    }
    if (!path || !*path) {
        return false;
    }
    if (path[0] == '/') {
        if (!resolve_module_candidate_with_format(ctx, path, out, out_sz, format_out,
                                                  reason, reason_sz)) {
            return false;
        }
        prefer_language_spelling(ctx, out, out_sz, format_out);
        return true;
    }

    /*
     * PeTTa's importer tries the path as written before resolving it against
     * the top-level source directory.  This lets a repository-root command
     * import `lib/foo` while a source-relative import such as `../lib/foo`
     * remains available as the fallback.
     */
    if (ctx->session.language_id == CETTA_LANGUAGE_PETTA &&
        cetta_eval_session_relative_module_policy(&ctx->session) ==
            CETTA_RELATIVE_MODULE_POLICY_WORKING_DIR_ONLY &&
        resolve_module_candidate_with_format(
            ctx, path, out, out_sz, format_out, reason, reason_sz)) {
        prefer_language_spelling(ctx, out, out_sz, format_out);
        return true;
    }

    snprintf(base_dir, sizeof(base_dir), "%s", cetta_library_relative_base_dir(ctx));
    while (true) {
        int n = snprintf(candidate, sizeof(candidate), "%s/%s", base_dir, path);
        if (!(n > 0 && (size_t)n < sizeof(candidate))) {
            return false;
        }
        if (resolve_module_candidate_with_format(ctx, candidate, out, out_sz, format_out,
                                                 reason, reason_sz)) {
            prefer_language_spelling(ctx, out, out_sz, format_out);
            return true;
        }
        if (cetta_eval_session_relative_module_policy(&ctx->session) !=
            CETTA_RELATIVE_MODULE_POLICY_ANCESTOR_WALK) {
            break;
        }
        char parent[PATH_MAX];
        if (!cetta_text_path_parent_dir(parent, sizeof(parent), base_dir)) {
            break;
        }
        if (strcmp(parent, base_dir) == 0) {
            break;
        }
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
    snprintf(out->raw_spec, sizeof(out->raw_spec), "%s", spec);
    /* `a:b:` names `a:b`. */
    size_t raw_len = strlen(out->raw_spec);
    if (raw_len > 1u && out->raw_spec[raw_len - 1u] == ':' &&
        strchr(out->raw_spec, ':') != out->raw_spec + raw_len - 1u)
        out->raw_spec[raw_len - 1u] = '\0';
    spec = out->raw_spec;
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

/* The name of the module whose code is importing. */
static const char *he_importer_module_name(CettaLibraryContext *ctx) {
    Atom *self = ctx->module_token_scope
        ? registry_lookup_id(ctx->module_token_scope, g_builtin_syms.self)
        : NULL;
    const char *name = self && self->kind == ATOM_GROUNDED &&
            self->ground.gkind == GV_SPACE
        ? cetta_library_module_space_name(ctx, self->ground.ptr) : NULL;
    return name ? name : "top";
}

/* Sub-modules are looked up inside a directory module and beside the file of
 * a single-file module. */
static bool he_module_resource_dir(const char *canonical_path,
                                   char *out, size_t out_sz) {
    struct stat st;
    if (stat(canonical_path, &st) == 0 && S_ISDIR(st.st_mode)) {
        int n = snprintf(out, out_sz, "%s", canonical_path);
        return n > 0 && (size_t)n < out_sz;
    }
    copy_parent_dir(out, out_sz, canonical_path);
    return out[0] != '\0';
}

/* Walk an HE module-tree name. `self:a:b` and `a:b` name sub-modules of the
 * importing module, `top:a:b` those of the program, and every component is a
 * module. `out` receives the place where the last component is looked up;
 * `name` receives the full name of the module reached, which on failure is
 * the missing one. */
static bool he_module_tree_candidate(CettaLibraryContext *ctx,
                                     const CettaModuleSpec *spec,
                                     char *out, size_t out_sz,
                                     char *name, size_t name_sz) {
    char dir[PATH_MAX];
    char rest[PATH_MAX];
    const char *first = spec->namespace_name;
    bool from_top = strcmp(first, "top") == 0;
    const char *start = from_top ? "top" : he_importer_module_name(ctx);
    int n = snprintf(name, name_sz, "%s", start);
    if (!(n > 0 && (size_t)n < name_sz))
        return false;
    n = snprintf(dir, sizeof(dir), "%s",
                 from_top ? (ctx->script_dir[0] ? ctx->script_dir
                                                : ctx->working_dir)
                          : cetta_library_relative_base_dir(ctx));
    if (!(n > 0 && (size_t)n < sizeof(dir)))
        return false;
    n = from_top || strcmp(first, "self") == 0
        ? snprintf(rest, sizeof(rest), "%s", spec->path_or_member)
        : snprintf(rest, sizeof(rest), "%s/%s", first, spec->path_or_member);
    if (!(n > 0 && (size_t)n < sizeof(rest)))
        return false;
    bool under_top = strcmp(start, "top") == 0;
    for (char *component = rest;;) {
        char *slash = strchr(component, '/');
        if (slash)
            *slash = '\0';
        size_t name_len = strlen(name);
        n = snprintf(name + name_len, name_sz - name_len, ":%s", component);
        if (!(n > 0 && (size_t)n < name_sz - name_len))
            return false;
        n = snprintf(out, out_sz, "%s/%s", dir, component);
        if (!*component || !(n > 0 && (size_t)n < out_sz))
            return false;
        if (!slash)
            return true;
        /* A registered root is a module of the program wherever it lies. */
        const CettaModuleMount *mount = under_top
            ? cetta_library_find_module_mount(ctx, component) : NULL;
        char resolved[PATH_MAX];
        CettaModuleFormat format;
        char reason[160];
        if (mount) {
            if (!he_module_resource_dir(mount->root_path, dir, sizeof(dir)))
                return false;
        } else if (!resolve_module_candidate_with_format(
                       ctx, out, resolved, sizeof(resolved), &format,
                       reason, sizeof(reason)) ||
                   !he_module_resource_dir(resolved, dir, sizeof(dir))) {
            return false;
        }
        under_top = false;
        component = slash + 1;
    }
}

static Atom *he_unresolved_module(Arena *arena, const char *name) {
    size_t size = strlen("Failed to resolve module ") + strlen(name) + 1u;
    char *message = arena_alloc(arena, size);
    snprintf(message, size, "Failed to resolve module %s", name);
    return atom_symbol(arena, message);
}

/* The reason an import is refused when no file answers the requested module
 * (cetta_library_import_error_is_missing_module). */
static const char CETTA_LIBRARY_MISSING_MODULE[] = "module file not found";

static bool resolve_import_plan(CettaLibraryContext *ctx, const CettaModuleSpec *spec,
                                Space *logical_target_space,
                                Space *execution_target_space,
                                bool target_is_fresh,
                                CettaImportPlan *plan,
                                Arena *eval_arena, Atom **error_out) {
    char candidate[PATH_MAX];
    /* A registered root names its own path, not a name to look up. */
    bool he_exact_root = false;

    memset(plan, 0, sizeof(*plan));
    plan->spec = *spec;
    plan->logical_target_space = logical_target_space;
    plan->execution_target_space = execution_target_space;
    plan->target_is_fresh = target_is_fresh;
    plan->transactional = ctx->session.module_policy.transactional_imports && !target_is_fresh;
    plan->format.kind = CETTA_MODULE_FORMAT_METTA;
    plan->format.foreign_backend = CETTA_FOREIGN_BACKEND_NONE;

    switch (spec->kind) {
    case CETTA_MODULE_SPEC_REGISTERED_ROOT: {
        const CettaModuleMount *mount = cetta_library_find_module_mount(ctx, spec->namespace_name);
        if (mount) {
            if (!cetta_module_policy_allows(&ctx->session.module_policy,
                                              CETTA_MODULE_PROVIDER_REGISTERED_ROOTS)) {
                *error_out = atom_symbol(eval_arena, "registered module roots disabled");
                return false;
            }
            plan->provider_kind = mount->provider_kind;
            /* A member of a root is looked up where the root keeps its
               sub-modules: inside a directory, beside a single file. */
            if (ctx->session.language_id == CETTA_LANGUAGE_HE &&
                spec->path_or_member[0] != '\0') {
                CettaModuleSpec rooted = *spec;
                char name[PATH_MAX];
                int n = snprintf(rooted.path_or_member,
                                 sizeof(rooted.path_or_member), "%s/%s",
                                 spec->namespace_name, spec->path_or_member);
                snprintf(rooted.namespace_name, sizeof(rooted.namespace_name),
                         "top");
                if (!(n > 0 && (size_t)n < sizeof(rooted.path_or_member)) ||
                    !he_module_tree_candidate(ctx, &rooted, candidate,
                                              sizeof(candidate), name,
                                              sizeof(name))) {
                    *error_out = he_unresolved_module(eval_arena, name);
                    return false;
                }
                break;
            }
            if (spec->path_or_member[0] == '\0') {
                he_exact_root = ctx->session.language_id == CETTA_LANGUAGE_HE;
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

        if (cetta_module_policy_allows(&ctx->session.module_policy,
                                         CETTA_MODULE_PROVIDER_STDLIB)) {
            char nested_name[PATH_MAX];
            int n;

            if (spec->path_or_member[0] == '\0') {
                n = snprintf(nested_name, sizeof(nested_name), "%s",
                             spec->namespace_name);
            } else {
                n = snprintf(nested_name, sizeof(nested_name), "%s/%s",
                             spec->namespace_name, spec->path_or_member);
            }
            if (n > 0 && (size_t)n < sizeof(nested_name) &&
                build_library_path(ctx, nested_name, candidate, sizeof(candidate))) {
                plan->provider_kind = CETTA_MODULE_PROVIDER_STDLIB_FILE;
                break;
            }
        }

        if (ctx->session.language_id == CETTA_LANGUAGE_HE) {
            if (!cetta_module_policy_allows(&ctx->session.module_policy,
                                              CETTA_MODULE_PROVIDER_RELATIVE_FILES)) {
                *error_out = atom_symbol(eval_arena, "relative module imports disabled");
                return false;
            }
            plan->provider_kind = CETTA_MODULE_PROVIDER_RELATIVE_FILE;
            char name[PATH_MAX] = {0};
            char reason[160];
            if (!he_module_tree_candidate(ctx, spec, candidate,
                                          sizeof(candidate), name,
                                          sizeof(name)) ||
                !resolve_module_candidate_with_format(
                    ctx, candidate, plan->canonical_path,
                    sizeof(plan->canonical_path), &plan->format,
                    reason, sizeof(reason))) {
                *error_out = he_unresolved_module(
                    eval_arena, name[0] ? name : spec->raw_spec);
                return false;
            }
            return true;
        }

        *error_out = atom_symbol(eval_arena, "unknown module root");
        return false;
    }
    case CETTA_MODULE_SPEC_RELATIVE_FILE: {
        if (!cetta_module_policy_allows(&ctx->session.module_policy,
                                          CETTA_MODULE_PROVIDER_RELATIVE_FILES)) {
            *error_out = atom_symbol(eval_arena, "relative module imports disabled");
            return false;
        }
        plan->provider_kind = CETTA_MODULE_PROVIDER_RELATIVE_FILE;
        char reason[160];
        if (!resolve_relative_module_candidate_for_language(
                ctx, spec->path_or_member,
                plan->canonical_path, sizeof(plan->canonical_path),
                &plan->format, reason, sizeof(reason))) {
            if (reason[0]) {
                *error_out = atom_symbol(eval_arena, reason);
            } else {
                *error_out = atom_symbol(eval_arena, CETTA_LIBRARY_MISSING_MODULE);
            }
            return false;
        }
        return true;
    }
    case CETTA_MODULE_SPEC_MODULE_NAME: {
        /*
         * Plain PeTTa imports are source-relative.  Package lookup is an
         * explicit, separate syntax: `(library member)`.  Searching CeTTa's
         * stdlib or registered mounts for a bare PeTTa name would turn an
         * upstream failed import into a different program and can duplicate
         * every equation when the same file was also imported by path.
         */
        bool plain_petta_import =
            ctx->session.language_id == CETTA_LANGUAGE_PETTA;
        if (!plain_petta_import &&
            cetta_module_policy_allows(&ctx->session.module_policy,
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
        if (!plain_petta_import &&
            cetta_module_policy_allows(&ctx->session.module_policy,
                                         CETTA_MODULE_PROVIDER_STDLIB) &&
            build_library_path(ctx, spec->path_or_member, candidate, sizeof(candidate))) {
            plan->provider_kind = CETTA_MODULE_PROVIDER_STDLIB_FILE;
            break;
        }
        if (!cetta_module_policy_allows(&ctx->session.module_policy,
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
                ctx, spec->path_or_member,
                plan->canonical_path, sizeof(plan->canonical_path),
                &plan->format, reason, sizeof(reason))) {
            if (ctx->session.language_id == CETTA_LANGUAGE_HE &&
                !spec->reports_written_name) {
                const char *importer = he_importer_module_name(ctx);
                size_t size = strlen(importer) + 1u +
                    strlen(spec->path_or_member) + 1u;
                char *name = arena_alloc(eval_arena, size);
                snprintf(name, size, "%s:%s", importer, spec->path_or_member);
                *error_out = he_unresolved_module(eval_arena, name);
                return false;
            }
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
        if (!cetta_module_policy_allows(&ctx->session.module_policy,
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

    /* An explicitly registered HE namespace can be an empty directory.
       Registration already established its identity and format. */
    if (ctx->session.language_id == CETTA_LANGUAGE_HE &&
        plan->provider_kind == CETTA_MODULE_PROVIDER_REGISTERED_ROOT) {
        CettaLoadedModule *registered = loaded_module_lookup(ctx, candidate);
        if (registered && registered->space) {
            snprintf(plan->canonical_path, sizeof(plan->canonical_path),
                     "%s", registered->canonical_path);
            plan->format = registered->format;
            return true;
        }
    }

    char reason[160];
    if (he_exact_root &&
        resolve_module_candidate_metta(candidate, plan->canonical_path,
                                       sizeof(plan->canonical_path),
                                       reason, sizeof(reason))) {
        plan->format.kind = CETTA_MODULE_FORMAT_METTA;
        plan->format.foreign_backend = CETTA_FOREIGN_BACKEND_NONE;
        return true;
    }
    if (!resolve_module_candidate_with_format(ctx, candidate, plan->canonical_path,
                                              sizeof(plan->canonical_path),
                                              &plan->format,
                                              reason, sizeof(reason))) {
        if (reason[0]) {
            *error_out = atom_symbol(eval_arena, reason);
        } else {
            *error_out = atom_symbol(eval_arena, CETTA_LIBRARY_MISSING_MODULE);
        }
        return false;
    }
    return true;
}

static int64_t imported_file_lookup(CettaLibraryContext *ctx, Space *space,
                                    const char *path) {
    for (uint32_t i = 0; i < ctx->imported_file_len; i++) {
        if (ctx->imported_files[i].path[0] == '\0') continue;
        if (ctx->imported_files[i].space == space &&
            strcmp(ctx->imported_files[i].path, path) == 0) {
            return (int64_t)i;
        }
    }
    return -1;
}

static bool build_library_resource_path(
    CettaLibraryContext *ctx, const char *name,
    const char *suffix, char *out, size_t out_sz
) {
    const char *language_name;

    if (!ctx || !name || !suffix || !out || out_sz == 0u ||
        !cetta_module_policy_allows(
            &ctx->session.module_policy,
            CETTA_MODULE_PROVIDER_STDLIB)) {
        return false;
    }
    /* Explicit imports select a language overlay when present, then fall back
       to the shared library. Merely selecting a language never loads either.
       A typecheck profile reads the language's typed spelling of a library
       first, where one exists: the same library with the effect types its
       checker reads, so the plain spelling stays the reference's.  The
       language version names that directory (PeTTa 1.1's typecheck/). */
    language_name = cetta_language_canonical_name(ctx->session.language_id);
    const CettaLibrarySpec *spec = cetta_library_lookup(name);
    bool shared_body = ctx->session.language_id == CETTA_LANGUAGE_HE &&
        spec && spec->he_words;
    const CettaLanguageVersion *version = cetta_language_version_of(
        ctx->session.language_id, ctx->session.profile);
    const char *overlay = version ? version->library_overlay : NULL;
    if (ctx->root_dir[0] != '\0') {
        int n;
        if (overlay) {
            n = snprintf(out, out_sz, "%s/lib/%s/%s/%s%s",
                         ctx->root_dir, language_name, overlay, name, suffix);
            if (n > 0 && (size_t)n < out_sz && access(out, R_OK) == 0)
                return true;
        }
        if (!shared_body) {
            n = snprintf(out, out_sz, "%s/lib/%s/%s%s",
                         ctx->root_dir, language_name, name, suffix);
            if (n > 0 && (size_t)n < out_sz && access(out, R_OK) == 0) return true;
        }
        n = snprintf(out, out_sz, "%s/lib/%s%s",
                     ctx->root_dir, name, suffix);
        if (n > 0 && (size_t)n < out_sz && access(out, R_OK) == 0) return true;
    }
    {
        int n;
        if (overlay) {
            n = snprintf(out, out_sz, "lib/%s/%s/%s%s",
                         language_name, overlay, name, suffix);
            if (n > 0 && (size_t)n < out_sz && access(out, R_OK) == 0)
                return true;
        }
        if (!shared_body) {
            n = snprintf(out, out_sz, "lib/%s/%s%s",
                         language_name, name, suffix);
            if (n > 0 && (size_t)n < out_sz && access(out, R_OK) == 0) return true;
        }
        n = snprintf(out, out_sz, "lib/%s%s", name, suffix);
        if (n > 0 && (size_t)n < out_sz && access(out, R_OK) == 0) return true;
    }
    return false;
}

static bool build_library_path(CettaLibraryContext *ctx, const char *name,
                               char *out, size_t out_sz) {
    return build_library_resource_path(
        ctx, name, ".metta", out, out_sz);
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

/* Native services receive atom values, without evaluator-private carriers.
 * This projection inspects syntax only; demand and activation belong to
 * the caller's declaration and to the service's explicit interpreter. */
static Atom *library_native_value(Arena *arena, Atom *value) {
    return eval_current_language_id() == CETTA_LANGUAGE_PRIME
        ? atom_prime_held_strip(arena, value) : value;
}

static Atom **library_native_arguments(Arena *arena, Atom **arguments,
                                       uint32_t count) {
    Atom **visible = arguments;
    for (uint32_t i = 0u; i < count; i++) {
        Atom *value = library_native_value(arena, arguments[i]);
        if (!value) return NULL;
        if (value != arguments[i]) {
            if (visible == arguments) {
                visible = arena_alloc(arena, sizeof(*visible) * count);
                memcpy(visible, arguments, sizeof(*visible) * count);
            }
            visible[i] = value;
        }
    }
    return visible;
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
        /* A C text cannot hold NUL: such a string is refused, not cut. */
        return memchr(arg->ground.sval, '\0', arg->ground.slen)
            ? NULL : arg->ground.sval;
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
    while (pos < len) {
        Atom *atom = parse_sexpr_n(a, text, len, &pos);
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

static char *library_mm2_syntax_text(Arena *a, Atom *atom) {
    if (atom && atom->kind == ATOM_EXPR && atom->expr.len == 2 &&
        atom->expr.elems[0]->kind == ATOM_SYMBOL &&
        atom_is_symbol_id(atom->expr.elems[0], g_builtin_syms.quote)) {
        atom = atom->expr.elems[1];
    }
    return cetta_mm2_atom_to_syntax_string(a, atom);
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
    return atom_expr(a, elems, (CettaExprLen)len);
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
    return atom_expr(a, elems, (CettaExprLen)count);
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

static bool library_write_file(const char *path, const char *text, size_t len,
                               bool append, char *errbuf, size_t errbuf_sz) {
    FILE *fp = fopen(path, append ? "ab" : "wb");

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
                          atom_symbol(a, "expected: (system:args)"));
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
        if (ctx && ctx->session.language_id == CETTA_LANGUAGE_PRIME) {
            return atom_expr2(a, atom_symbol(a, "CliArgumentNotFound"),
                              atom_int(a, index));
        }
        return ctx && ctx->session.language_id == CETTA_LANGUAGE_PETTA
            ? atom_petta_no_result(a) : atom_empty(a);
    }
    return atom_symbol(a, ctx->cmdline_args[index]);
}

static Atom *system_cli_arg_count(const CettaLibraryContext *ctx, Arena *a, Atom *head,
                                  Atom **args, uint32_t nargs) {
    if (!system_zero_arg_ok(args, nargs)) {
        return library_signature_error(a, head, args, nargs, "expected: (system:argc)");
    }
    return atom_int(a, ctx ? (int64_t)ctx->cmdline_arg_len : 0);
}

static Atom *system_has_cli_args(const CettaLibraryContext *ctx, Arena *a, Atom *head,
                                 Atom **args, uint32_t nargs) {
    if (!system_zero_arg_ok(args, nargs)) {
        return library_signature_error(a, head, args, nargs,
                                       "expected: (system:has-args)");
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

static Atom *system_exit_with_code(CettaLibraryContext *ctx, Arena *a, Atom *head,
                                   Atom **args, uint32_t nargs) {
    int code = 0;
    if (nargs != 1 || !library_int_arg(args[0], &code)) {
        return library_signature_error(a, head, args, nargs,
                                       "expected integer exit code");
    }
    if (code < 0 || code > 255)
        return library_signature_error(a, head, args, nargs,
                                       "exit code must be between 0 and 255");
    cetta_eval_session_request_process_exit_with_answer_policy(
        &ctx->session, code, CETTA_EXIT_PUBLISH_PRIOR_ANSWERS);
    return atom_unit(a);
}

static Atom *system_write(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    if (nargs != 2u || args[1]->kind != ATOM_GROUNDED ||
        args[1]->ground.gkind != GV_STRING ||
        (!atom_is_symbol(args[0], "stdout") && !atom_is_symbol(args[0], "stderr")))
        return library_signature_error(a, head, args, nargs,
            "expected stdout or stderr and a byte string");
    FILE *stream = atom_is_symbol(args[0], "stdout") ? stdout : stderr;
    size_t length = atom_string_len(args[1]);
    sigset_t blocked, previous, pending;
    sigemptyset(&blocked); sigaddset(&blocked, SIGPIPE);
    if (pthread_sigmask(SIG_BLOCK, &blocked, &previous))
        return library_signature_error(a, head, args, nargs, "cannot protect output boundary");
    bool already_pending = sigpending(&pending) == 0 && sigismember(&pending, SIGPIPE);
    bool failed = fwrite(args[1]->ground.sval, 1, length, stream) != length ||
        fflush(stream) != 0 || ferror(stream);
    if (!already_pending) {
        struct timespec now = {0};
        /* Consume only a signal raised by this write before restoring the
         * caller's mask. The error remains sticky on the FILE stream. */
        while (sigtimedwait(&blocked, NULL, &now) < 0 && errno == EINTR) {}
    }
    int restored = pthread_sigmask(SIG_SETMASK, &previous, NULL);
    if (failed || restored)
        return library_signature_error(a, head, args, nargs, "output failed");
    return atom_unit(a);
}

/* The working directory at any length, as CPython's os.getcwd reads it: the
 * buffer grows while getcwd answers ERANGE. On failure errno is kept and NULL
 * returned; ENOMEM also covers a directory longer than a string can be. */
static char *library_getcwd(size_t *length) {
    size_t capacity = PATH_MAX;
    char *buffer = NULL;
    for (;;) {
        char *grown = realloc(buffer, capacity);
        if (!grown) {
            free(buffer);
            errno = ENOMEM;
            return NULL;
        }
        buffer = grown;
        if (getcwd(buffer, capacity)) {
            *length = strlen(buffer);
            return buffer;
        }
        if (errno != ERANGE || capacity > CETTA_STRING_BYTES_MAX) {
            int code = errno == ERANGE ? ENOMEM : errno;
            free(buffer);
            errno = code;
            return NULL;
        }
        capacity *= 2;
    }
}

static Atom *system_cwd(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    if (!system_zero_arg_ok(args, nargs)) {
        return library_signature_error(a, head, args, nargs, "expected: (system:cwd)");
    }
    size_t length;
    char *path = library_getcwd(&length);
    if (!path) {
        return library_signature_error(a, head, args, nargs,
                                       "cannot determine current directory");
    }
    Atom *value = atom_string_n(a, path, length);
    free(path);
    return value;
}

static Atom *system_monotonic_ns(Arena *a, Atom *head, Atom **args,
                                 uint32_t nargs) {
    uint64_t ns = 0;
    if (!system_zero_arg_ok(args, nargs)) {
        return library_signature_error(a, head, args, nargs,
                                       "expected: (system:monotonic-ns)");
    }
    if (!library_read_monotonic_ns(&ns)) {
        return library_signature_error(a, head, args, nargs,
                                       "cannot read monotonic clock");
    }
    if (ns > (uint64_t)INT64_MAX) {
        return library_signature_error(a, head, args, nargs,
                                       "monotonic clock value exceeds signed integer range");
    }
    return atom_int(a, (int64_t)ns);
}

/* Standard input as lines, through a buffer of the runtime's own rather than
 * stdio's, so a timed read can tell whether a line is already buffered. The
 * blocking and the timed read share it. */
static struct {
    char *bytes;
    size_t start, end, capacity;
    bool eof;
} g_stdin_lines;

/* One read into the buffer, waiting at most TIMEOUT_MS, or indefinitely when
 * it is negative: 1 when bytes or the end of input arrived, 0 when nothing
 * did in time, -1 on an error. */
static int stdin_lines_fill(int64_t timeout_ms) {
    size_t held = g_stdin_lines.end - g_stdin_lines.start;
    if (g_stdin_lines.start) {
        memmove(g_stdin_lines.bytes, g_stdin_lines.bytes + g_stdin_lines.start, held);
        g_stdin_lines.start = 0;
        g_stdin_lines.end = held;
    }
    if (g_stdin_lines.end == g_stdin_lines.capacity) {
        size_t capacity = g_stdin_lines.capacity ? g_stdin_lines.capacity * 2u : 65536u;
        char *bytes = capacity > g_stdin_lines.capacity ? realloc(g_stdin_lines.bytes, capacity) : NULL;
        if (!bytes) return -1;
        g_stdin_lines.bytes = bytes;
        g_stdin_lines.capacity = capacity;
    }
    uint64_t started = 0;
    if (timeout_ms >= 0) {
        if (!library_read_monotonic_ns(&started)) return -1;
    }
    for (;;) {
        int wait = -1;
        if (timeout_ms >= 0) {
            uint64_t now;
            if (!library_read_monotonic_ns(&now)) return -1;
            /* Subtract elapsed time rather than multiply an arbitrary
             * millisecond duration into a wrapping absolute deadline. */
            uint64_t elapsed = (now - started) / 1000000u;
            uint64_t left = elapsed < (uint64_t)timeout_ms
                ? (uint64_t)timeout_ms - elapsed : 0;
            wait = left > INT_MAX ? INT_MAX : (int)left;
        }
        struct pollfd input = {STDIN_FILENO, POLLIN, 0};
        int ready = poll(&input, 1, wait);
        if (ready < 0 && errno != EINTR) return -1;
        if (ready <= 0) {
            if (ready == 0 && timeout_ms >= 0) {
                uint64_t now;
                if (!library_read_monotonic_ns(&now)) return -1;
                if ((now - started) / 1000000u >= (uint64_t)timeout_ms) return 0;
            }
            continue;
        }
        ssize_t count = read(STDIN_FILENO, g_stdin_lines.bytes + g_stdin_lines.end,
                             g_stdin_lines.capacity - g_stdin_lines.end);
        if (count > 0) { g_stdin_lines.end += (size_t)count; return 1; }
        if (!count) { g_stdin_lines.eof = true; return 1; }
        /* A descriptor set nonblocking by someone else polls again. */
        if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) return -1;
    }
}

/* (system:read-line) waits for a line; (system:read-line MS) waits at most MS
 * milliseconds and answers () when no line has begun arriving. A line that has
 * begun is read to its end, so () leaves nothing buffered. The line comes
 * without its newline and one carriage return before it; end_of_file follows
 * the last line. */
static Atom *system_read_line(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    int64_t timeout_ms = -1;
    if (!system_zero_arg_ok(args, nargs)) {
        if (nargs != 1 || !args[0] || args[0]->kind != ATOM_GROUNDED ||
            args[0]->ground.gkind != GV_INT || args[0]->ground.ival < 0)
            return library_signature_error(a, head, args, nargs,
                "expected (system:read-line) or (system:read-line milliseconds)");
        timeout_ms = args[0]->ground.ival;
    }
    for (;;) {
        size_t held = g_stdin_lines.end - g_stdin_lines.start;
        char *bytes = held ? g_stdin_lines.bytes + g_stdin_lines.start : NULL;
        char *newline = held ? memchr(bytes, '\n', held) : NULL;
        if (newline || (held && g_stdin_lines.eof)) {
            size_t size = newline ? (size_t)(newline - bytes) : held;
            g_stdin_lines.start += newline ? size + 1u : size;
            if (size && bytes[size - 1] == '\r') --size;
            return atom_string_n(a, bytes, size);
        }
        if (g_stdin_lines.eof) return atom_symbol(a, "end_of_file");
        int filled = stdin_lines_fill(held ? -1 : timeout_ms);
        if (filled < 0)
            return library_signature_error(a, head, args, nargs, "cannot read standard input");
        if (!filled) return atom_unit(a);
    }
}

/* The running executable's path with symbolic links resolved, or NULL with
 * errno set. A binary replaced since it started is named by its path. */
static char *library_executable_path(void) {
#if defined(__APPLE__)
    uint32_t size = 0;
    (void)_NSGetExecutablePath(NULL, &size);
    char *path = malloc(size ? size : 1u);
    if (!path) return NULL;
    if (_NSGetExecutablePath(path, &size)) { free(path); errno = ENAMETOOLONG; return NULL; }
    char *resolved = realpath(path, NULL);
    free(path);
    return resolved;
#elif defined(__linux__)
    for (size_t capacity = 256;; capacity *= 2u) {
        char *path = malloc(capacity);
        if (!path) return NULL;
        ssize_t size = readlink("/proc/self/exe", path, capacity);
        if (size < 0) { free(path); return NULL; }
        if ((size_t)size < capacity) {
            const char deleted[] = " (deleted)";
            struct stat executable;
            if (!stat("/proc/self/exe", &executable) && !executable.st_nlink &&
                (size_t)size >= sizeof deleted - 1u &&
                !memcmp(path + size - (sizeof deleted - 1u), deleted, sizeof deleted - 1u))
                size -= (ssize_t)(sizeof deleted - 1u);
            path[size] = '\0';
            return path;
        }
        free(path);
        if (capacity > SIZE_MAX / 4u) { errno = ENAMETOOLONG; return NULL; }
    }
#else
    errno = ENOSYS;
    return NULL;
#endif
}

static Atom *cetta_library_dispatch_system(CettaLibraryContext *ctx,
                                           Arena *a, Atom *head,
                                           Atom **args, uint32_t nargs) {
    if (head->kind != ATOM_SYMBOL) return NULL;
    SymbolId head_id = atom_symbol_current_id(head);
    if (head_id == g_builtin_syms.lib_system_host) {
        const char *operation = nargs ? library_text_arg(args[0]) : NULL;
        struct timespec now;
        if (!operation) return library_signature_error(a, head, args, nargs, "expected system operation");
        if (!strcmp(operation, "wall-time") && nargs == 1) {
            int64_t ns;
            if (clock_gettime(CLOCK_REALTIME, &now) ||
                __builtin_mul_overflow((int64_t)now.tv_sec, INT64_C(1000000000), &ns) ||
                __builtin_add_overflow(ns, (int64_t)now.tv_nsec, &ns))
                return library_signature_error(a, head, args, nargs, "cannot read wall clock");
            /* time.time() as CPython 3.12 converts the clock's nanoseconds
             * (_PyTime_AsSecondsDouble): whole seconds by integer division,
             * any other count divided by 1e9 as a double. */
            return atom_float(a, ns % 1000000000 == 0 ? (double)(ns / 1000000000)
                                                      : (double)ns / 1e9);
        }
        if (!strcmp(operation, "pid") && nargs == 1) return atom_int(a, getpid());
        /* The running executable, for a program that starts another process
         * of the same runtime, or the errno and message when it cannot be
         * named. */
        if (!strcmp(operation, "executable") && nargs == 1) {
            char *path = library_executable_path();
            if (!path) {
                int code = errno;
                return atom_expr3(a, atom_symbol(a, "system:os-error"), atom_int(a, code),
                                  atom_string(a, strerror(code)));
            }
            Atom *value = atom_string(a, path);
            free(path);
            return value;
        }
        if (!strcmp(operation, "uuid4-hex") && nargs == 1) {
            /* Version 4 in the high nibble of byte 6, the RFC 4122 variant in
             * the two high bits of byte 8, as uuid.UUID(version=4) sets them. */
            unsigned char bytes[16];
            char hex[33];
            if (getentropy(bytes, sizeof(bytes)))
                return library_signature_error(a, head, args, nargs, "cannot read random bytes");
            bytes[6] = (unsigned char)((bytes[6] & 0x0fu) | 0x40u);
            bytes[8] = (unsigned char)((bytes[8] & 0x3fu) | 0x80u);
            for (size_t i = 0u; i < sizeof(bytes); i++)
                snprintf(hex + 2u * i, 3u, "%02x", bytes[i]);
            return atom_string(a, hex);
        }
        if (!strcmp(operation, "pid-alive") && nargs == 2) {
            int pid;
            if (!library_int_arg(args[1], &pid) || pid <= 0)
                return library_signature_error(a, head, args, nargs, "expected positive process ID");
            return !kill(pid, 0) || errno == EPERM ? atom_true(a) : atom_false(a);
        }
        /* os.kill(pid, 0): every refusal is reported, EPERM included, and 0
         * and negative IDs address process groups as kill(2) does. */
        if (!strcmp(operation, "signal-probe") && nargs == 2) {
            Atom *pid = args[1];
            if (!pid || pid->kind != ATOM_GROUNDED || pid->ground.gkind != GV_INT ||
                (int64_t)(pid_t)pid->ground.ival != pid->ground.ival)
                return library_signature_error(a, head, args, nargs, "expected a process ID within pid_t");
            if (!kill((pid_t)pid->ground.ival, 0)) return atom_true(a);
            int code = errno;
            return atom_expr3(a, atom_symbol(a, "system:os-error"), atom_int(a, code),
                              atom_string(a, strerror(code)));
        }
        /* os.getcwd: the directory at any length, or the refusal's errno and
         * message, as CPython raises OSError (ENOENT for a removed directory,
         * EACCES when an unreadable ancestor hides a long one's name). */
        if (!strcmp(operation, "getcwd") && nargs == 1) {
            size_t length;
            char *path = library_getcwd(&length);
            if (!path) {
                int code = errno;
                return atom_expr3(a, atom_symbol(a, "system:os-error"), atom_int(a, code),
                                  atom_string(a, strerror(code)));
            }
            Atom *value = atom_string_n(a, path, length);
            free(path);
            return value;
        }
        /* os.TMP_MAX: the C platform constant. Python 3.12's tempfile profile
         * uses it; later Python versions can choose their own retry policy. */
        if (!strcmp(operation, "tmp-max") && nargs == 1) {
#ifdef TMP_MAX
            return atom_int(a, TMP_MAX);
#else
            return atom_int(a, 10000);
#endif
        }
        if (!strcmp(operation, "errno-value") && nargs == 2) {
            const char *name = library_text_arg(args[1]);
            if (!name) return library_signature_error(a, head, args, nargs, "expected an errno name");
            for (const CettaErrnoName *entry = cetta_errno_names; entry->name; entry++)
                if (!strcmp(entry->name, name)) return atom_int(a, entry->value);
            return atom_unit(a);
        }
        /* os.path.expanduser as CPython 3.12's posixpath has it; see
         * src/expand_user.c. MemoryError is reported, as CPython raises it. */
        if (!strcmp(operation, "expanduser") && nargs == 2) {
            const char *path = library_text_arg(args[1]);
            if (!path) return library_signature_error(a, head, args, nargs, "expected a path as text");
            char *expanded = NULL;
            size_t expanded_length = 0u;
            switch (cetta_expand_user(path, strlen(path), CETTA_STRING_BYTES_MAX, &expanded, &expanded_length)) {
            case CETTA_EXPAND_USER_EXPANDED: {
                Atom *value = atom_string_n(a, expanded, expanded_length);
                free(expanded);
                return value;
            }
            case CETTA_EXPAND_USER_UNCHANGED:
                return atom_string(a, path);
            case CETTA_EXPAND_USER_TOO_LONG:
                return library_signature_error(a, head, args, nargs, "the expanded path is longer than a string can be");
            case CETTA_EXPAND_USER_NO_MEMORY:
                break;
            }
            return library_signature_error(a, head, args, nargs, "out of memory reading the account database");
        }
        if (!strcmp(operation, "format-time") && nargs == 4) {
            const char *format = library_text_arg(args[1]), *zone = library_text_arg(args[3]);
            Atom *value = args[2];
            double seconds = value && value->kind == ATOM_GROUNDED && value->ground.gkind == GV_FLOAT
                ? value->ground.fval : value && value->kind == ATOM_GROUNDED && value->ground.gkind == GV_INT
                ? (double)value->ground.ival : NAN;
            if (!format || !zone || !isfinite(seconds) || seconds < -0x1p63 || seconds >= 0x1p63 ||
                (strcmp(zone, "local") && strcmp(zone, "utc")))
                return library_signature_error(a, head, args, nargs, "expected format, timestamp and local or utc");
            time_t timestamp = (time_t)seconds;
            struct tm date;
            if (!(strcmp(zone, "utc") ? localtime_r(&timestamp, &date) : gmtime_r(&timestamp, &date)))
                return library_signature_error(a, head, args, nargs, "timestamp outside calendar range");
            /* A leading sentinel distinguishes an empty formatted value from
             * strftime's insufficient-space return. Grow without a text cap. */
            size_t length = strlen(format);
            char *pattern = cetta_malloc(length + 2);
            pattern[0] = '@'; memcpy(pattern + 1, format, length + 1);
            size_t capacity = length + 128;
            char *output = cetta_malloc(capacity);
            size_t size;
            while (!(size = strftime(output, capacity, pattern, &date))) {
                if (capacity > SIZE_MAX / 2) { free(pattern); free(output); return library_signature_error(a, head, args, nargs, "formatted time too large"); }
                capacity *= 2; output = cetta_realloc(output, capacity);
            }
            Atom *result = atom_string_n(a, output + 1, size - 1);
            free(pattern); free(output); return result;
        }
        return library_signature_error(a, head, args, nargs, "unsupported system operation or arguments");
    }
    if (head_id == g_builtin_syms.lib_system_read_line)
        return system_read_line(a, head, args, nargs);
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
        return system_exit_with_code(ctx, a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_system_write) {
        return system_write(a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_system_cwd) {
        return system_cwd(a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_system_entry_directory) {
        if (!system_zero_arg_ok(args, nargs))
            return library_signature_error(a, head, args, nargs, "expected: (system:entry-directory)");
        return ctx->script_dir[0] ? atom_string(a, ctx->script_dir)
            : library_signature_error(a, head, args, nargs, "entry directory is not established");
    }
    if (atom_is_symbol(head, "__cetta_lib_system_runtime_root")) {
        if (!system_zero_arg_ok(args, nargs))
            return library_signature_error(a, head, args, nargs, "expected: (system:runtime-root)");
        return ctx->root_dir[0] ? atom_string(a, ctx->root_dir)
            : library_signature_error(a, head, args, nargs, "runtime root is not established");
    }
    if (head_id == g_builtin_syms.lib_system_monotonic_ns) {
        return system_monotonic_ns(a, head, args, nargs);
    }
    return NULL;
}

static Atom *library_prolog_extension_absent(Arena *a) {
    Atom *extension =
        atom_symbol(a, "prolog:extension");
    Atom *absent = atom_symbol(a, "absent");
    return extension && absent
        ? atom_expr2(a, extension, absent)
        : NULL;
}

static Atom *library_prolog_query_failed(
    Arena *a, Atom *goal) {
    Atom *failed =
        atom_symbol(a, "prolog:query-failed");
    return failed && goal
        ? atom_expr2(a, failed, goal)
        : NULL;
}

static Atom *cetta_library_dispatch_lib_prolog(
    CettaLibraryContext *ctx, Arena *a,
    Atom *head, Atom **args, uint32_t nargs) {
    if (!ctx || !a || !head ||
        head->kind != ATOM_SYMBOL) {
        return NULL;
    }
    SymbolId head_id = atom_symbol_current_id(head);
    if (head_id == g_builtin_syms.lib_prolog_available) {
        if (!system_zero_arg_ok(args, nargs)) {
            return library_signature_error(
                a, head, args, nargs,
                "expected: (prolog:available?)");
        }
        return cetta_lib_prolog_runtime_available(
                   ctx->lib_prolog)
            ? atom_true(a) : atom_false(a);
    }
    if (head_id != g_builtin_syms.lib_prolog_query)
        return NULL;
    if (nargs != 2u) {
        return library_signature_error(
            a, head, args, nargs,
            "expected: (prolog:query Goal Projection)");
    }
    Atom *answers = NULL;
    CettaLibPrologQueryStatus status =
        cetta_lib_prolog_query(
            ctx->lib_prolog, a, args[0], args[1],
            &answers);
    if (status == CETTA_LIB_PROLOG_QUERY_UNAVAILABLE) {
        return library_prolog_extension_absent(a);
    }
    if (status != CETTA_LIB_PROLOG_QUERY_OK) {
        return library_prolog_query_failed(a, args[0]);
    }
    return answers;
}

static Atom *lts_rho_transitions_call_expr(Arena *a, Atom **args, uint32_t nargs) {
    Atom **elems = arena_alloc(a, sizeof(Atom *) * (nargs + 1));
    elems[0] = atom_symbol(a, "lts:rho:transitions");
    for (uint32_t i = 0; i < nargs; i++) elems[i + 1] = args[i];
    return atom_expr(a, elems, nargs + 1);
}

static Atom *lts_rho_transitions_error(Arena *a, Atom **args, uint32_t nargs,
                                       Atom *reason) {
    return atom_error(a, lts_rho_transitions_call_expr(a, args, nargs), reason);
}

static Atom *lts_he_transitions_call_expr(Arena *a, Atom **args,
                                          uint32_t nargs) {
    Atom **elems = arena_alloc(a, sizeof(Atom *) * (nargs + 1));
    elems[0] = atom_symbol(a, "lts:he:transitions");
    for (uint32_t i = 0; i < nargs; i++) elems[i + 1] = args[i];
    return atom_expr(a, elems, nargs + 1);
}

static Atom *lts_he_transitions_error(Arena *a, Atom **args, uint32_t nargs,
                                      Atom *reason) {
    return atom_error(a, lts_he_transitions_call_expr(a, args, nargs), reason);
}

static Atom *lts_rho_cost_steps_call_expr(Arena *a, Atom **args,
                                          uint32_t nargs) {
    Atom **elems = arena_alloc(a, sizeof(Atom *) * (nargs + 1));
    elems[0] = atom_symbol(a, "lts:rho:cost:steps");
    for (uint32_t i = 0; i < nargs; i++) elems[i + 1] = args[i];
    return atom_expr(a, elems, nargs + 1);
}

static Atom *lts_rho_cost_steps_error(Arena *a, Atom **args, uint32_t nargs,
                                      Atom *reason) {
    return atom_error(a, lts_rho_cost_steps_call_expr(a, args, nargs), reason);
}

static Atom *lts_rho_cost_causal_trace_call_expr(Arena *a, Atom **args,
                                                 uint32_t nargs) {
    Atom **elems = arena_alloc(a, sizeof(Atom *) * (nargs + 1));
    elems[0] = atom_symbol(a, "lts:rho:cost:causal-trace");
    for (uint32_t i = 0; i < nargs; i++) elems[i + 1] = args[i];
    return atom_expr(a, elems, nargs + 1);
}

static Atom *lts_rho_cost_causal_trace_error(Arena *a, Atom **args,
                                             uint32_t nargs, Atom *reason) {
    return atom_error(a,
                      lts_rho_cost_causal_trace_call_expr(a, args, nargs),
                      reason);
}

static Atom *lts_rho_cost_causal_prefix_call_expr(Arena *a, Atom **args,
                                                  uint32_t nargs) {
    Atom **elems = arena_alloc(a, sizeof(Atom *) * (nargs + 1));
    elems[0] = atom_symbol(a, "lts:rho:cost:causal-prefix");
    for (uint32_t i = 0; i < nargs; i++) elems[i + 1] = args[i];
    return atom_expr(a, elems, nargs + 1);
}

static Atom *lts_rho_cost_causal_prefix_error(Arena *a, Atom **args,
                                              uint32_t nargs, Atom *reason) {
    return atom_error(a,
                      lts_rho_cost_causal_prefix_call_expr(a, args, nargs),
                      reason);
}

static Atom *lts_he_transitions_frontier(Arena *a, Atom *state,
                                         ResultSet *results) {
    Atom *first_error = NULL;
    Atom **changed = NULL;
    uint32_t changed_len = 0;

    if (results->len > 0) {
        changed = arena_alloc(a, sizeof(Atom *) * results->len);
    }

    for (uint32_t i = 0; i < results->len; i++) {
        Atom *candidate = results->items[i];
        if (atom_is_empty(candidate))
            continue;
        if (atom_is_error(candidate)) {
            if (!first_error) first_error = candidate;
            continue;
        }
        if (atom_eq(candidate, state))
            continue;
        changed[changed_len++] =
            atom_expr2(a, atom_symbol_id(a, g_builtin_syms.quote), candidate);
    }

    if (changed_len == 0) {
        if (first_error)
            return first_error;
        return atom_expr2(a, atom_symbol_id(a, g_builtin_syms.superpose),
                          atom_expr(a, changed, 0));
    }
    if (changed_len == 1)
        return changed[0];
    return atom_expr2(a, atom_symbol_id(a, g_builtin_syms.superpose),
                      atom_expr(a, changed, changed_len));
}

static Atom *cetta_library_dispatch_lts(const CettaLibraryContext *ctx,
                                        Space *space, Arena *a, Atom *head,
                                        Atom **args, uint32_t nargs) {
    (void)ctx;
    if (head->kind != ATOM_SYMBOL) return NULL;
    if (atom_symbol_current_id(head) == g_builtin_syms.lib_lts_he_step_rules) {
        if (nargs != 1) {
            return atom_error(a, head,
                              atom_symbol(a, "IncorrectNumberOfArguments"));
        }
        if (args[0]->kind == ATOM_SYMBOL &&
            strcmp(atom_symbol_bytes(args[0]), "he-small-step") == 0) {
            return cetta_langdef_step_rules_atom(cetta_langdef_pack_he_small_step(),
                                                 a);
        }
        return atom_error(a, atom_expr2(a, head, args[0]),
                          atom_string(a, "unknown rule table"));
    }
    if (atom_symbol_current_id(head) == g_builtin_syms.lib_lts_rho_transitions) {
        Atom *result;
        const char *detail;

        if (nargs != 1) {
            return lts_rho_transitions_error(
                a, args, nargs, atom_symbol(a, "IncorrectNumberOfArguments"));
        }
        if (!rhocalc_process_well_formed(args[0])) {
            detail = rhocalc_last_validation_error();
            return lts_rho_transitions_error(
                a, args, nargs,
                atom_string(a, detail ? detail : "expected strict-core rho process"));
        }
        result = rhocalc_successor_frontier_expr(a, args[0]);
        if (!result) {
            detail = rhocalc_last_validation_error();
            return lts_rho_transitions_error(
                a, args, nargs,
                atom_string(a, detail ? detail : "rho successor frontier construction failed"));
        }
        return result;
    }
    if (atom_symbol_current_id(head) == g_builtin_syms.lib_lts_he_transitions) {
        ResultSet results;
        Atom *frontier;
        Atom *raw_state;

        if (nargs != 1) {
            return lts_he_transitions_error(
                a, args, nargs, atom_symbol(a, "IncorrectNumberOfArguments"));
        }

        raw_state = args[0];
        if (raw_state->kind == ATOM_EXPR &&
            raw_state->expr.len == 2 &&
            atom_is_symbol_id(raw_state->expr.elems[0], g_builtin_syms.quote)) {
            raw_state = raw_state->expr.elems[1];
        }

        result_set_init(&results);
        eval_top_one_step(space, a, raw_state, &results);
        frontier = lts_he_transitions_frontier(a, raw_state, &results);
        result_set_free(&results);
        return frontier;
    }
    if (atom_symbol_current_id(head) == g_builtin_syms.lib_lts_rho_cost_steps) {
        Atom *result;
        const char *detail;

        if (nargs != 1) {
            return lts_rho_cost_steps_error(
                a, args, nargs, atom_symbol(a, "IncorrectNumberOfArguments"));
        }
        if (!rhocalc_process_well_formed_with_semantic_profile(
                args[0], RHOCALC_SEMANTIC_PROFILE_COST)) {
            detail = rhocalc_last_validation_error();
            return lts_rho_cost_steps_error(
                a, args, nargs,
                atom_string(a, detail ? detail : "expected cost-profile rho term"));
        }
        result = rhocalc_cost_step_frontier_expr(a, args[0]);
        if (!result) {
            detail = rhocalc_last_validation_error();
            return lts_rho_cost_steps_error(
                a, args, nargs,
                atom_string(a, detail ? detail : "rho cost step frontier construction failed"));
        }
        return result;
    }
    if (atom_symbol_current_id(head) == g_builtin_syms.lib_lts_rho_cost_causal_trace) {
        Atom *result;
        const char *detail;
        uint32_t thread_count;

        if (nargs != 1) {
            return lts_rho_cost_causal_trace_error(
                a, args, nargs, atom_symbol(a, "IncorrectNumberOfArguments"));
        }
        if (!rhocalc_process_well_formed_with_semantic_profile(
                args[0], RHOCALC_SEMANTIC_PROFILE_COST)) {
            detail = rhocalc_last_validation_error();
            return lts_rho_cost_causal_trace_error(
                a, args, nargs,
                atom_string(a, detail ? detail : "expected cost-profile rho term"));
        }
        thread_count = eval_current_num_threads();
        result = thread_count > 1u
            ? rhocalc_cost_causal_trace_parallel_expr(
                  a, args[0], thread_count)
            : rhocalc_cost_causal_trace_expr(a, args[0]);
        if (!result) {
            detail = rhocalc_last_validation_error();
            return lts_rho_cost_causal_trace_error(
                a, args, nargs,
                atom_string(
                    a, detail ? detail : "rho cost causal trace construction failed"));
        }
        return result;
    }
    if (atom_symbol_current_id(head) == g_builtin_syms.lib_lts_rho_cost_causal_prefix) {
        Atom *result;
        const char *detail;
        uint64_t fuel;
        uint64_t search_budget;
        uint32_t thread_count;

        if (nargs != 3) {
            return lts_rho_cost_causal_prefix_error(
                a, args, nargs, atom_symbol(a, "IncorrectNumberOfArguments"));
        }
        if (args[0]->kind != ATOM_GROUNDED ||
            args[0]->ground.gkind != GV_INT || args[0]->ground.ival < 0) {
            return lts_rho_cost_causal_prefix_error(
                a, args, nargs,
                atom_string(a, "expected a nonnegative integer fuel bound"));
        }
        if (args[1]->kind != ATOM_GROUNDED ||
            args[1]->ground.gkind != GV_INT || args[1]->ground.ival < 0) {
            return lts_rho_cost_causal_prefix_error(
                a, args, nargs,
                atom_string(
                    a, "expected a nonnegative integer search bound"));
        }
        if (!rhocalc_process_well_formed_with_semantic_profile(
                args[2], RHOCALC_SEMANTIC_PROFILE_COST)) {
            detail = rhocalc_last_validation_error();
            return lts_rho_cost_causal_prefix_error(
                a, args, nargs,
                atom_string(a, detail ? detail : "expected cost-profile rho term"));
        }
        fuel = (uint64_t)args[0]->ground.ival;
        search_budget = (uint64_t)args[1]->ground.ival;
        thread_count = eval_current_num_threads();
        result = thread_count > 1u
            ? rhocalc_cost_causal_prefix_parallel_expr(
                  a, args[2], fuel, search_budget, thread_count)
            : rhocalc_cost_causal_prefix_expr(
                  a, args[2], fuel, search_budget);
        if (!result) {
            detail = rhocalc_last_validation_error();
            return lts_rho_cost_causal_prefix_error(
                a, args, nargs,
                atom_string(
                    a, detail ? detail : "rho cost causal prefix construction failed"));
        }
        return result;
    }
    return NULL;
}

static bool rhometta_eval_context_from_current(
    const CettaLibraryContext *ctx, Space *space, RhocalcEvalContext *out) {
    if (!ctx || !space || !out) return false;
    *out=(RhocalcEvalContext){.space=space,.registry=eval_current_registry(),
        .persistent_arena=eval_current_persistent_arena(),
        .library_context=(CettaLibraryContext *)ctx};
    return true;
}

static Atom *cetta_library_dispatch_rhometta(const CettaLibraryContext *ctx,
                                             Space *space, Arena *a, Atom *head,
                                             Atom **args, uint32_t nargs,
                                             CettaCallResultForm *result_form) {
    RhocalcEvalContext eval_context;
    _Atomic(RhocalcPayloadFailure) payload_failure = RHOCALC_PAYLOAD_FAILURE_NONE;
    const char *detail;

    if (head->kind != ATOM_SYMBOL) return NULL;
    if (!rhometta_eval_context_from_current(ctx, space, &eval_context)) {
        return atom_error(a, library_call_expr(a, head, args, nargs),
                          atom_string(a, "rhometta requires an active evaluation space"));
    }

    eval_context.payload_failure = &payload_failure;

    if (atom_symbol_current_id(head) == g_builtin_syms.lib_rhometta_transitions) {
        Atom *result;
        if (nargs != 1) {
            return library_signature_error(
                a, head, args, nargs, "expected one rhometta process");
        }
        if (!rhocalc_process_well_formed_with_eval_payloads(args[0])) {
            detail = rhocalc_last_validation_error();
            return atom_error(
                a, library_call_expr(a, head, args, nargs),
                atom_string(a, detail ? detail
                                      : "expected rhometta process"));
        }
        result = rhocalc_successor_frontier_expr_with_eval_context(
            a, args[0], &eval_context);
        if (!result) {
            if (payload_failure == RHOCALC_PAYLOAD_FAILURE_OWNED_EXPORT_ALIASED)
                return atom_error(a, atom_symbol(a, "rhometta:eval"),
                                  atom_symbol(a, "PayloadOwnedExportAliased"));
            detail = rhocalc_last_validation_error();
            return atom_error(
                a, library_call_expr(a, head, args, nargs),
                atom_string(a, detail ? detail
                                      : "rhometta successor frontier construction failed"));
        }
        *result_form = CETTA_CALL_RESULT_COMPLETED_FRONTIER;
        return result->expr.elems[1];
    }

    if (atom_symbol_current_id(head) == g_builtin_syms.lib_rhometta_run) {
        Atom *result;
        if (nargs != 1) {
            return library_signature_error(
                a, head, args, nargs, "expected one rhometta process");
        }
        if (!rhocalc_process_well_formed_with_eval_payloads(args[0])) {
            detail = rhocalc_last_validation_error();
            return atom_error(
                a, library_call_expr(a, head, args, nargs),
                atom_string(a, detail ? detail
                                      : "expected rhometta process"));
        }
        result = rhocalc_quiescent_frontier_expr_with_eval_context(
            a, args[0], &eval_context);
        if (!result) {
            if (payload_failure == RHOCALC_PAYLOAD_FAILURE_OWNED_EXPORT_ALIASED)
                return atom_error(a, atom_symbol(a, "rhometta:eval"),
                                  atom_symbol(a, "PayloadOwnedExportAliased"));
            detail = rhocalc_last_validation_error();
            return atom_error(
                a, library_call_expr(a, head, args, nargs),
                atom_string(a, detail ? detail : "rhometta reduction failed"));
        }
        *result_form = CETTA_CALL_RESULT_COMPLETED_FRONTIER;
        return result->expr.elems[1];
    }

    if (atom_symbol_current_id(head) == g_builtin_syms.lib_rhometta_run_canonical) {
        RhoRuntimeProfile profile = {
            .scheduler_policy = RHO_SCHEDULER_CANONICAL,
            .reduction_limit = 100000u,
            .thread_count = 1u,
            .threaded = false,
        };
        RhoReductionResult reduction;
        if (nargs != 1) {
            return library_signature_error(
                a, head, args, nargs, "expected one rhometta process");
        }
        if (!rhocalc_process_well_formed_with_eval_payloads(args[0])) {
            detail = rhocalc_last_validation_error();
            return atom_error(
                a, library_call_expr(a, head, args, nargs),
                atom_string(a, detail ? detail
                                      : "expected rhometta process"));
        }
        if (!rhocalc_reduce_to_quiescence_with_eval_context(
                a, args[0], &profile, &eval_context, &reduction)) {
            if (payload_failure == RHOCALC_PAYLOAD_FAILURE_OWNED_EXPORT_ALIASED)
                return atom_error(a, atom_symbol(a, "rhometta:eval"),
                                  atom_symbol(a, "PayloadOwnedExportAliased"));
            detail = rhocalc_last_validation_error();
            return atom_error(
                a, library_call_expr(a, head, args, nargs),
                atom_string(a, detail ? detail
                                      : "canonical rhometta reduction failed"));
        }
        if (reduction.status == RHOCALC_REDUCTION_LIMIT_EXHAUSTED) {
            return atom_error(
                a, library_call_expr(a, head, args, nargs),
                atom_string(a, "canonical rhometta reduction limit exhausted"));
        }
        *result_form = CETTA_CALL_RESULT_COMPLETED_VALUE;
        return reduction.residual;
    }

    if (atom_symbol_current_id(head) == g_builtin_syms.lib_rhometta_values) {
        Atom *result;
        if (nargs != 1) {
            return library_signature_error(
                a, head, args, nargs, "expected one rhometta residual");
        }
        result = rhocalc_observe_top_level_values(a, args[0]);
        if (!result) {
            return atom_error(
                a, library_call_expr(a, head, args, nargs),
                atom_string(a, "cannot inspect rhometta residual"));
        }
        *result_form = CETTA_CALL_RESULT_COMPLETED_VALUE;
        return result;
    }

    return NULL;
}

typedef struct {
    CettaLibraryContext *library;
    RhocalcEvalContext context;
    Arena *arena;
    uint64_t arena_epoch, symbol_instance;
    uint32_t arena_identity;
    CettaLanguageId language;
    const CettaProfile *profile;
    Atom *call;
    RhocalcRun *run;
    RhocalcValueObservation *values;
    bool started, returned;
} LibraryNativeExecution;

bool cetta_library_native_execution_supported(const CettaLibraryContext *ctx, SymbolId head) {
    return ctx && (ctx->active_mask & CETTA_LIBRARY_RHOMETTA) &&
        (head == g_builtin_syms.lib_rhometta_run_canonical ||
         head == g_builtin_syms.lib_rhometta_values);
}

static bool library_native_execution_current(void *raw) {
    LibraryNativeExecution *job = raw;
    return job && job->arena->identity == job->arena_identity &&
        job->arena->reset_epoch == job->arena_epoch &&
        symbol_table_instance_id(g_symbols) == job->symbol_instance &&
        eval_current_library_context() == job->library &&
        job->library->session.language_id == job->language &&
        job->library->session.profile == job->profile &&
        eval_current_registry() == job->context.registry &&
        eval_current_persistent_arena() == job->context.persistent_arena &&
        (job->library->active_mask & CETTA_LIBRARY_RHOMETTA);
}

static void library_native_execution_release(void *raw) {
    LibraryNativeExecution *job = raw;
    if (!job) return;
    rhocalc_run_close(job->run);
    rhocalc_values_close(job->values);
    free(job);
}

static CettaOwnedReleaseStep library_native_execution_release_poll(void *raw,
    uint64_t allowance, bool allow_atomic, uint64_t *used) {
    LibraryNativeExecution *job = raw;
    uint64_t spent = 0u;
    if (used) *used = 0u;
    if (job->run) {
        CettaOwnedReleaseStep status = rhocalc_run_release_poll(&job->run,
            allowance, allow_atomic, &spent);
        if (status != CETTA_OWNED_RELEASE_COMPLETE) {
            if (used) *used = spent;
            return status;
        }
    }
    if (job->values) {
        if (spent == allowance) {
            if (used) *used = spent;
            return CETTA_OWNED_RELEASE_PENDING;
        }
        if (!allow_atomic) {
            if (used) *used = spent;
            return CETTA_OWNED_RELEASE_ATOMIC;
        }
        rhocalc_values_close(job->values);
        job->values = NULL;
        spent++;
    }
    if (used) *used = spent;
    return CETTA_OWNED_RELEASE_COMPLETE;
}

static CettaOwnedExecutionStep library_native_execution_next(void *raw,
    CettaOwnedReturn *returned, CettaEvalCompletion *completion) {
    LibraryNativeExecution *job = raw;
    if (!library_native_execution_current(job)) {
        *completion = CETTA_EVAL_INCOMPLETE_INVALIDATED;
        return CETTA_OWNED_EXECUTION_INTERRUPTED;
    }
    if (job->returned) return CETTA_OWNED_EXECUTION_COMPLETE;
    if (!job->started) {
        job->started = true;
        if (job->call->expr.len != 2u) {
            Atom *error = atom_error(job->arena, job->call,
                atom_symbol(job->arena, "IncorrectNumberOfArguments"));
            *returned = (CettaOwnedReturn){.outcome = cetta_call_completed_value(error),
                .final = true};
            job->returned = true;
            return CETTA_OWNED_EXECUTION_RETURN;
        }
        Atom *input = library_native_value(job->arena, job->call->expr.elems[1]);
        if (!input) {
            *completion = CETTA_EVAL_INCOMPLETE_CAPACITY;
            return CETTA_OWNED_EXECUTION_INTERRUPTED;
        }
        if (atom_is_symbol_id(job->call->expr.elems[0], g_builtin_syms.lib_rhometta_values))
            job->values = rhocalc_values_open(job->arena, input);
        else {
            RhoRuntimeProfile profile = {.scheduler_policy = RHO_SCHEDULER_CANONICAL,
                .reduction_limit = 100000u, .thread_count = 1u};
            job->run = rhocalc_run_open_held(job->arena, input, &profile, &job->context);
        }
        if (!job->run && !job->values) {
            *completion = CETTA_EVAL_INCOMPLETE_HOST_FAILURE;
            return CETTA_OWNED_EXECUTION_INTERRUPTED;
        }
        return CETTA_OWNED_EXECUTION_PAUSED;
    }
    RhocalcRunStatus status = job->run
        ? rhocalc_run_poll(job->run, 1u, NULL)
        : rhocalc_values_poll(job->values, 1u, NULL);
    if (status == RHOCALC_RUN_PENDING) return CETTA_OWNED_EXECUTION_PAUSED;
    if (status != RHOCALC_RUN_DONE) {
        *completion = CETTA_EVAL_INCOMPLETE_HOST_FAILURE;
        return CETTA_OWNED_EXECUTION_INTERRUPTED;
    }
    Atom *value = NULL;
    if (job->run) {
        RhoReductionResult reduction;
        if (rhocalc_run_result(job->run, &reduction)) {
            if (reduction.status == RHOCALC_REDUCTION_LIMIT_EXHAUSTED) {
                *completion = CETTA_EVAL_INCOMPLETE_FUEL;
                return CETTA_OWNED_EXECUTION_INTERRUPTED;
            }
            value = reduction.residual;
        }
    } else value = rhocalc_values_result(job->values);
    if (!value) {
        *completion = CETTA_EVAL_INCOMPLETE_HOST_FAILURE;
        return CETTA_OWNED_EXECUTION_INTERRUPTED;
    }
    *returned = (CettaOwnedReturn){.outcome = cetta_call_completed_value(value), .final = true};
    job->returned = true;
    return CETTA_OWNED_EXECUTION_RETURN;
}

bool cetta_library_native_execution_open(CettaLibraryContext *ctx, Space *space,
    Arena *arena, Atom *call, const RhocalcPayloadProvider *payload,
    CettaOwnedExecution *out) {
    if (!ctx || !(ctx->active_mask & CETTA_LIBRARY_RHOMETTA) || !space || !arena ||
        !call || call->kind != ATOM_EXPR || !call->expr.len ||
        !call->expr.elems[0] || call->expr.elems[0]->kind != ATOM_SYMBOL ||
        !out || out->computation ||
        !cetta_library_native_execution_supported(ctx, atom_symbol_current_id(call->expr.elems[0])) ||
        (!payload && atom_is_symbol_id(call->expr.elems[0], g_builtin_syms.lib_rhometta_run_canonical)))
        return false;
    LibraryNativeExecution *job = calloc(1u, sizeof(*job));
    if (!job) return false;
    job->library = ctx; job->arena = arena; job->call = call;
    job->arena_identity = arena->identity; job->arena_epoch = arena->reset_epoch;
    job->symbol_instance = symbol_table_instance_id(g_symbols);
    job->language = ctx->session.language_id; job->profile = ctx->session.profile;
    if (!rhometta_eval_context_from_current(ctx, space, &job->context)) {
        free(job); return false;
    }
    job->context.payload_provider = payload;
    cetta_owned_execution_init(out, job, library_native_execution_next,
        library_native_execution_release);
    bool cleanup_installed = cetta_owned_execution_set_release_poll(
        out, library_native_execution_release_poll);
    assert(cleanup_installed); (void)cleanup_installed;
    out->final_is_current = library_native_execution_current;
    return true;
}

static Atom *fs_exists(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    const char *path;
    struct stat st;
    if (nargs != 1 || !(path = library_text_arg(args[0]))) {
        return library_signature_error(a, head, args, nargs, "expected filename");
    }
    return stat(path, &st) == 0 ? atom_true(a) : atom_false(a);
}

static Atom *fs_canonical_path(Arena *a, Atom *head, Atom **args,
                               uint32_t nargs) {
    const char *path;
    char *canonical;
    Atom *result;
    char error[192];

    if (nargs != 1 || !(path = library_text_arg(args[0]))) {
        return library_signature_error(a, head, args, nargs,
                                       "expected filename");
    }
    canonical = realpath(path, NULL);
    if (!canonical) {
        (void)snprintf(error, sizeof(error),
                       "cannot canonicalize path: %s", strerror(errno));
        return atom_error(a, library_call_expr(a, head, args, nargs),
                          atom_string(a, error));
    }
    result = atom_string(a, canonical);
    free(canonical);
    return result;
}

static Atom *fs_try_canonical_path(Arena *a, Atom *head, Atom **args,
                                   uint32_t nargs) {
    const char *path;
    char *canonical;
    Atom *result;
    char error[192];

    if (nargs != 1 || !(path = library_text_arg(args[0]))) {
        return library_signature_error(a, head, args, nargs,
                                       "expected filename");
    }
    canonical = realpath(path, NULL);
    if (!canonical) {
        (void)snprintf(error, sizeof(error),
                       "cannot canonicalize path: %s", strerror(errno));
        return atom_expr2(a, atom_symbol(a, "FS:CanonicalPathFault"),
                          atom_string(a, error));
    }
    result = atom_expr2(a, atom_symbol(a, "FS:CanonicalPath"),
                        atom_string(a, canonical));
    free(canonical);
    return result;
}

static Atom *fs_try_canonical_candidate(Arena *a, const char *candidate) {
    char *canonical;
    Atom *result;
    char error[192];

    canonical = realpath(candidate, NULL);
    if (!canonical) {
        (void)snprintf(error, sizeof(error),
                       "cannot canonicalize path: %s", strerror(errno));
        return atom_expr2(a, atom_symbol(a, "FS:CanonicalPathFault"),
                          atom_string(a, error));
    }
    result = atom_expr2(a, atom_symbol(a, "FS:CanonicalPath"),
                        atom_string(a, canonical));
    free(canonical);
    return result;
}

static Atom *fs_try_canonical_from_file(Arena *a, Atom *head, Atom **args,
                                        uint32_t nargs) {
    const char *base_file;
    const char *relative;
    char directory[PATH_MAX];
    char candidate[PATH_MAX];
    char *slash;

    if (nargs != 2 || !(base_file = library_text_arg(args[0])) ||
        !(relative = library_text_arg(args[1]))) {
        return library_signature_error(
            a, head, args, nargs, "expected base filename and relative path");
    }
    if (relative[0] == '/') {
        return fs_try_canonical_candidate(a, relative);
    }
    if (snprintf(directory, sizeof(directory), "%s", base_file) >=
        (int)sizeof(directory)) {
        return atom_expr2(a, atom_symbol(a, "FS:CanonicalPathFault"),
                          atom_string(a, "base filename is too long"));
    }
    slash = strrchr(directory, '/');
    if (!slash) {
        (void)snprintf(directory, sizeof(directory), ".");
    } else if (slash == directory) {
        slash[1] = '\0';
    } else {
        *slash = '\0';
    }
    if (!path_join2(candidate, sizeof(candidate), directory, relative)) {
        return atom_expr2(a, atom_symbol(a, "FS:CanonicalPathFault"),
                          atom_string(a, "resolved path is too long"));
    }
    return fs_try_canonical_candidate(a, candidate);
}

static bool fs_canonical_path_is_under(const char *directory,
                                       const char *candidate) {
    size_t directory_len;

    if (!directory || !candidate) return false;
    if (strcmp(directory, "/") == 0) return candidate[0] == '/';
    directory_len = strlen(directory);
    return strncmp(directory, candidate, directory_len) == 0 &&
           (candidate[directory_len] == '\0' ||
            candidate[directory_len] == '/');
}

static Atom *fs_try_canonical_under(Arena *a, Atom *head, Atom **args,
                                    uint32_t nargs) {
    const char *directory;
    const char *relative;
    char candidate[PATH_MAX];
    char error[192];
    char *canonical_directory;
    char *canonical_candidate;
    Atom *result;

    if (nargs != 2 || !(directory = library_text_arg(args[0])) ||
        !(relative = library_text_arg(args[1]))) {
        return library_signature_error(
            a, head, args, nargs, "expected directory and relative path");
    }
    canonical_directory = realpath(directory, NULL);
    if (!canonical_directory) {
        (void)snprintf(error, sizeof(error),
                       "cannot canonicalize directory: %s", strerror(errno));
        return atom_expr2(a, atom_symbol(a, "FS:CanonicalPathFault"),
                          atom_string(a, error));
    }
    if (relative[0] == '/') {
        if (snprintf(candidate, sizeof(candidate), "%s", relative) >=
            (int)sizeof(candidate)) {
            free(canonical_directory);
            return atom_expr2(a, atom_symbol(a, "FS:CanonicalPathFault"),
                              atom_string(a, "requested path is too long"));
        }
    } else if (!path_join2(candidate, sizeof(candidate), canonical_directory,
                           relative)) {
        free(canonical_directory);
        return atom_expr2(a, atom_symbol(a, "FS:CanonicalPathFault"),
                          atom_string(a, "resolved path is too long"));
    }
    canonical_candidate = realpath(candidate, NULL);
    if (!canonical_candidate) {
        (void)snprintf(error, sizeof(error),
                       "cannot canonicalize path: %s", strerror(errno));
        free(canonical_directory);
        return atom_expr2(a, atom_symbol(a, "FS:CanonicalPathFault"),
                          atom_string(a, error));
    }
    if (!fs_canonical_path_is_under(canonical_directory,
                                    canonical_candidate)) {
        free(canonical_candidate);
        free(canonical_directory);
        return atom_expr2(a, atom_symbol(a, "FS:CanonicalPathFault"),
                          atom_string(a,
                                      "canonical path escapes directory"));
    }
    result = atom_expr2(a, atom_symbol(a, "FS:CanonicalPath"),
                        atom_string(a, canonical_candidate));
    free(canonical_candidate);
    free(canonical_directory);
    return result;
}

/* Replace each malformed UTF-8 subsequence, preserving valid prefixes of a
 * truncated sequence as one replacement, as Python's errors="replace" does.
 * Valid files retain the original buffer; allocation starts at the first error. */
static void fs_replace_utf8(CettaStringBuf *text) {
    const uint8_t *bytes = (const uint8_t *)text->buf;
    CettaStringBuf repaired;
    size_t pos = 0u, copied = 0u;
    cetta_sb_init(&repaired);
    while (pos < text->len) {
        uint32_t scalar, width;
        if (bytes[pos] < 0x80u) { pos++; continue; }
        if (cetta_utf8_decode_one(bytes, text->len, pos, &scalar, &width)) {
            pos += width;
            continue;
        }
        cetta_sb_append_n(&repaired, text->buf + copied, pos - copied);
        cetta_sb_append_n(&repaired, "\xef\xbf\xbd", 3u);
        uint8_t first = bytes[pos];
        size_t expected = first >= 0xc2u && first <= 0xdfu ? 2u :
                          first >= 0xe0u && first <= 0xefu ? 3u :
                          first >= 0xf0u && first <= 0xf4u ? 4u : 1u;
        size_t consumed = 1u;
        while (consumed < expected && pos + consumed < text->len) {
            uint8_t next = bytes[pos + consumed];
            if ((next & 0xc0u) != 0x80u) break;
            if (consumed == 1u &&
                ((first == 0xe0u && next < 0xa0u) ||
                 (first == 0xedu && next > 0x9fu) ||
                 (first == 0xf0u && next < 0x90u) ||
                 (first == 0xf4u && next > 0x8fu))) break;
            consumed++;
        }
        pos += consumed;
        copied = pos;
    }
    if (repaired.buf) {
        cetta_sb_append_n(&repaired, text->buf + copied, text->len - copied);
        cetta_sb_free(text);
        *text = repaired;
    }
}

static bool fs_byte_limit(Atom *arg, size_t *limit) {
    int64_t value;
    if (arg && arg->kind == ATOM_GROUNDED && arg->ground.gkind == GV_INT) {
        value = arg->ground.ival;
    } else if (arg && arg->kind == ATOM_GROUNDED && arg->ground.gkind == GV_FLOAT) {
        double number = arg->ground.fval;
        if (!isfinite(number) || number >= (double)INT64_MAX ||
            number < (double)INT64_MIN) return false;
        value = (int64_t)number;
    } else {
        const char *number = library_text_arg(arg);
        char *end;
        if (!number) return false;
        errno = 0;
        value = strtoll(number, &end, 10);
        if (number == end || errno == ERANGE) return false;
        while (isspace((unsigned char)*end)) end++;
        if (*end) return false;
    }
    if (value > 0 && (uint64_t)value >= SIZE_MAX) return false;
    *limit = value > 0 ? (size_t)value : 0u;
    return true;
}

static Atom *fs_text_failure(Arena *a, const char *path, int error) {
    if (error == ENOENT) {
        char *cwd = getcwd(NULL, 0);
        Atom *result = atom_expr2(a, atom_string(a, "missing"),
                                 atom_string(a, cwd ? cwd : "unavailable"));
        free(cwd);
        return result;
    }
    if (error == EISDIR || error == EACCES || error == EPERM)
        return atom_expr2(a, atom_string(a, error == EISDIR ? "directory" : "denied"),
                          atom_string(a, ""));
    char prefix[64];
    snprintf(prefix, sizeof(prefix), "[Errno %d] ", error);
    CettaStringBuf detail;
    cetta_sb_init(&detail);
    cetta_sb_append_n(&detail, prefix, strlen(prefix));
    cetta_sb_append_n(&detail, strerror(error), strlen(strerror(error)));
    cetta_sb_append_n(&detail, ": ", 2u);
    cetta_sb_append_n(&detail, path, strlen(path));
    Atom *result = atom_expr3(a, atom_string(a, "error"),
                             atom_string(a, error == ENOTDIR ? "NotADirectoryError" : "OSError"),
                             atom_string_n(a, detail.buf, detail.len));
    cetta_sb_free(&detail);
    return result;
}

/* Structured I/O results let MeTTa choose how to present success and failure.
 * Read at most limit+1 bytes to distinguish a complete file from an oversized
 * one; zero means unlimited. No result is silently truncated. */
static Atom *fs_read_text_result(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    const char *path, *errors;
    size_t limit;
    if (nargs != 3 || !(path = library_text_arg(args[0])) ||
        !fs_byte_limit(args[1], &limit) || !(errors = library_text_arg(args[2])) ||
        (strcmp(errors, "raw") != 0 && strcmp(errors, "replace") != 0))
        return library_signature_error(a, head, args, nargs,
                                       "expected filename, byte limit (0: unlimited), and raw or replace");
    FILE *fp = fopen(path, "rb");
    if (!fp) return fs_text_failure(a, path, errno);
    struct stat st;
    CettaStringBuf text;
    cetta_sb_init(&text);
    if (fstat(fileno(fp), &st) == 0) {
        if (S_ISDIR(st.st_mode)) { fclose(fp); return fs_text_failure(a, path, EISDIR); }
        if (S_ISREG(st.st_mode) && st.st_size > 0) {
            uint64_t reserve = (uint64_t)st.st_size;
            if (limit && reserve > limit + 1u) reserve = limit + 1u;
            if (reserve < SIZE_MAX) cetta_sb_ensure(&text, (size_t)reserve);
        }
    }
    for (;;) {
        size_t count = 65536u;
        if (limit && count > limit + 1u - text.len) count = limit + 1u - text.len;
        cetta_sb_ensure(&text, count);
        size_t got = fread(text.buf + text.len, 1u, count, fp);
        text.len += got;
        text.buf[text.len] = '\0';
        if (limit && text.len > limit) {
            fclose(fp);
            cetta_sb_free(&text);
            return atom_expr2(a, atom_string(a, "limit"), atom_int(a, (int64_t)limit));
        }
        if (got < count) {
            if (ferror(fp)) {
                int error = errno ? errno : EIO;
                fclose(fp);
                cetta_sb_free(&text);
                return fs_text_failure(a, path, error);
            }
            break;
        }
    }
    fclose(fp);
    if (strcmp(errors, "replace") == 0) fs_replace_utf8(&text);
    Atom *result = atom_expr2(a, atom_string(a, "ok"),
                             atom_string_n(a, text.buf, text.len));
    cetta_sb_free(&text);
    return result;
}

static Atom *fs_tail_text(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    const char *path;
    size_t limit;
    if (nargs != 2 || !(path = library_text_arg(args[0])) || !fs_byte_limit(args[1], &limit))
        return library_signature_error(a, head, args, nargs, "expected filename and tail bytes (0: whole file)");
    FILE *fp = fopen(path, "rb");
    if (!fp) return fs_text_failure(a, path, errno);
    struct stat st;
    if (fstat(fileno(fp), &st) || st.st_size < 0 || !S_ISREG(st.st_mode)) {
        int error = errno ? errno : EINVAL;
        fclose(fp); return fs_text_failure(a, path, error);
    }
    uint64_t size = (uint64_t)st.st_size;
    size_t count = limit && size > limit ? limit : (size_t)size;
    if (size > INT64_MAX || (uint64_t)count != (limit && size > limit ? limit : size) || count == SIZE_MAX) {
        fclose(fp); return library_signature_error(a, head, args, nargs, "file size exceeds representable range");
    }
    if (fseeko(fp, (off_t)(size - count), SEEK_SET)) {
        int error = errno; fclose(fp); return fs_text_failure(a, path, error);
    }
    CettaStringBuf text;
    cetta_sb_init(&text); cetta_sb_ensure(&text, count);
    text.len = fread(text.buf, 1, count, fp); text.buf[text.len] = '\0';
    if (ferror(fp)) {
        int error = errno ? errno : EIO; fclose(fp); cetta_sb_free(&text); return fs_text_failure(a, path, error);
    }
    fclose(fp); fs_replace_utf8(&text);
    Atom *result = atom_expr(a, (Atom *[]){atom_symbol(a, "fs:tail"), atom_int(a, (int64_t)size),
        size > count ? atom_true(a) : atom_false(a), atom_string_n(a, text.buf, text.len)}, 4);
    cetta_sb_free(&text); return result;
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
    result = atom_string_n(a, text.buf ? text.buf : "", text.len);
    cetta_sb_free(&text);
    return result;
}

/* A file's text may hold any bytes, NUL included; its name may not. */
static Atom *fs_write_like(Arena *a, Atom *head, Atom **args, uint32_t nargs, bool append) {
    const char *path;
    const char *text = NULL;
    size_t len = 0u;
    char errbuf[160];
    if (nargs == 2 && args[1] && args[1]->kind == ATOM_GROUNDED &&
        args[1]->ground.gkind == GV_STRING) {
        text = args[1]->ground.sval;
        len = args[1]->ground.slen;
    } else if (nargs == 2 && args[1] && args[1]->kind == ATOM_SYMBOL &&
               (text = atom_name_cstr(args[1]))) {
        len = strlen(text);
    }
    if (nargs != 2 || !(path = library_text_arg(args[0])) || !text) {
        return library_signature_error(a, head, args, nargs,
                                       "expected filename and text");
    }
    if (!library_write_file(path, text, len, append, errbuf, sizeof(errbuf))) {
        return atom_error(a, library_call_expr(a, head, args, nargs), atom_string(a, errbuf));
    }
    return atom_unit(a);
}

static Atom *fs_write(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    return fs_write_like(a, head, args, nargs, false);
}

static Atom *fs_append_text(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    return fs_write_like(a, head, args, nargs, true);
}

static Atom *fs_read_lines(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    const char *path;
    CettaStringBuf text;
    char errbuf[160];
    Atom **items = NULL;
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
                items = cetta_realloc(items, sizeof(Atom *) * cap);
            }
            items[nitems++] = atom_string_n(a, text.buf + start, stop - start);
        }
        start = i + 1;
    }

    result = atom_expr(a, items, nitems);
    free(items);
    cetta_sb_free(&text);
    return result;
}

static Atom *library_string_list(Arena *a, char **items, uint32_t nitems) {
    Atom **atoms = arena_alloc(a, sizeof(Atom *) * (nitems ? nitems : 1));
    for (uint32_t i = 0; i < nitems; i++) {
        atoms[i] = atom_string(a, items[i]);
    }
    return atom_expr(a, atoms, nitems);
}

/* The complete lines within bytes [start, end) of a file; end < 0 or past
 * the file means its end. A range that starts inside the file begins at the
 * next line: the line cut by start is not returned. */
static Atom *fs_read_lines_between(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    const char *path;
    int64_t from, to;
    if (nargs != 3 || !(path = library_text_arg(args[0])) ||
        args[1]->kind != ATOM_GROUNDED || args[1]->ground.gkind != GV_INT ||
        args[2]->kind != ATOM_GROUNDED || args[2]->ground.gkind != GV_INT) {
        return library_signature_error(a, head, args, nargs, "expected filename, start and end");
    }
    from = args[1]->ground.ival; to = args[2]->ground.ival;
    FILE *f = fopen(path, "rb");
    if (!f) {
        return atom_error(a, library_call_expr(a, head, args, nargs),
                          atom_string(a, strerror(errno)));
    }
    if (fseeko(f, 0, SEEK_END) != 0) { fclose(f); return atom_expr(a, NULL, 0); }
    int64_t size = (int64_t)ftello(f);
    if (to < 0 || to > size) to = size;
    if (from < 0) from = 0;
    if (from > to) from = to;
    size_t len = (size_t)(to - from);
    char *buf = cetta_malloc(len + 1);
    if (fseeko(f, (off_t)from, SEEK_SET) != 0 || fread(buf, 1, len, f) != len) {
        fclose(f); free(buf);
        return atom_error(a, library_call_expr(a, head, args, nargs), atom_string(a, "read failed"));
    }
    fclose(f);
    buf[len] = '\0';
    size_t start = 0;
    if (from > 0) {
        char *newline = memchr(buf, '\n', len);
        start = newline ? (size_t)(newline - buf) + 1 : len;
    }
    char **items = NULL; uint32_t nitems = 0, cap = 0;
    for (size_t i = start; i < len; ) {
        char *newline = memchr(buf + i, '\n', len - i);
        size_t stop = newline ? (size_t)(newline - buf) : len;
        size_t end_of_line = stop;
        if (end_of_line > i && buf[end_of_line - 1] == '\r') end_of_line--;
        if (nitems >= cap) {
            cap = cap ? cap * 2 : 64;
            items = cetta_realloc(items, sizeof(char *) * cap);
        }
        char *line = cetta_malloc(end_of_line - i + 1);
        memcpy(line, buf + i, end_of_line - i);
        line[end_of_line - i] = '\0';
        items[nitems++] = line;
        i = stop + 1;
    }
    Atom *result = library_string_list(a, items, nitems);
    for (uint32_t i = 0; i < nitems; i++) free(items[i]);
    free(items); free(buf);
    return result;
}

#include "library_fs_tools.inc"

static Atom *cetta_library_dispatch_fs(Arena *a, Atom *head,
                                       Atom **args, uint32_t nargs) {
    if (head->kind != ATOM_SYMBOL) return NULL;
    SymbolId head_id = atom_symbol_current_id(head);
    if (head_id == g_builtin_syms.lib_fs_line_window) return fs_line_window(a, head, args, nargs);
    if (head_id == g_builtin_syms.lib_fs_tree_page) return fs_tree_page(a, head, args, nargs);
    if (head_id == g_builtin_syms.lib_fs_grep_page) return fs_grep_page(a, head, args, nargs);
    if (head_id == g_builtin_syms.lib_fs_update_text) return fs_update_text(a, head, args, nargs);
    if (head_id == g_builtin_syms.lib_fs_is_file) return fs_is_file(a, head, args, nargs);
    if (head_id == g_builtin_syms.lib_fs_exists) {
        return fs_exists(a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_fs_canonical_path) {
        return fs_canonical_path(a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_fs_try_canonical_path) {
        return fs_try_canonical_path(a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_fs_try_canonical_from_file) {
        return fs_try_canonical_from_file(a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_fs_try_canonical_under) {
        return fs_try_canonical_under(a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_fs_read_text) {
        return fs_read_text(a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_fs_read_text_result) {
        return fs_read_text_result(a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_fs_tail_text) {
        return fs_tail_text(a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_fs_entries) {
        const char *path = nargs == 1 ? library_text_arg(args[0]) : NULL;
        if (!path) return library_signature_error(a, head, args, nargs, "expected directory");
        DIR *directory = opendir(path);
        if (!directory) return fs_text_failure(a, path, errno);
        size_t count = 0, capacity = 32;
        Atom **entries = cetta_malloc(capacity * sizeof(*entries));
        struct dirent *entry;
        int error;
        for (;;) {
            errno = 0; entry = readdir(directory); error = errno;
            if (!entry) break;
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
            if (count == capacity) { capacity *= 2; entries = cetta_realloc(entries, capacity * sizeof(*entries)); }
            entries[count++] = atom_string(a, entry->d_name);
        }
        closedir(directory);
        Atom *result = error ? fs_text_failure(a, path, error)
                             : atom_expr(a, entries, count);
        free(entries); return result;
    }
    if (head_id == g_builtin_syms.lib_fs_write) {
        return fs_write(a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_fs_append_text) {
        return fs_append_text(a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_fs_read_lines) {
        return fs_read_lines(a, head, args, nargs);
    }
    if (head_id == g_builtin_syms.lib_fs_read_lines_between) {
        return fs_read_lines_between(a, head, args, nargs);
    }
    return NULL;
}

/* ── lib/str: strings are bytes plus a length ───────────────────────────
 * (__cetta_lib_str name args...) is the str: operation `name` of the shared
 * native table, which prepared equation runners call under the same names.
 * A refusal is (Error (str:name args...) (Reason offset)); a call outside an
 * operation's signature is (Error (str:name args...) expectation). */

static Atom *str_operation_refusal(Arena *a, Atom *call, Atom *detail, bool data) {
    return data ? atom_expr3(a, atom_symbol(a, "StrRefusedV1"), call, detail)
                : atom_error(a, call, detail);
}

static Atom *cetta_library_dispatch_str(Arena *a, Atom *head,
                                        Atom **args, uint32_t nargs,
                                        CettaCallResultForm *result_form) {
    if (head->kind != ATOM_SYMBOL || (atom_symbol_current_id(head) != g_builtin_syms.lib_str &&
        atom_symbol_current_id(head) != g_builtin_syms.lib_str_value))
        return NULL;
    bool values = atom_symbol_current_id(head) == g_builtin_syms.lib_str_value;
    if (values) *result_form = CETTA_CALL_RESULT_COMPLETED_VALUE;
    const char *name = nargs > 0u && args[0] && args[0]->kind == ATOM_SYMBOL
        ? atom_name_cstr(args[0]) : NULL;
    const CettaStrNative *native = cetta_str_native(name);
    if (!native)
        return str_operation_refusal(a, library_call_expr(a, head, args, nargs),
                                     atom_symbol(a, "unknown string operation"), values);
    Atom **operands = args + 1u;
    uint32_t count = nargs - 1u;
    if (values) {
        if (nargs != 2u || !args[1] || args[1]->kind != ATOM_EXPR)
            return str_operation_refusal(a, library_call_expr(a, head, args, nargs),
                atom_symbol(a, "expected string operation and inert argument tuple"), values);
        if (args[1]->expr.len > UINT32_MAX)
            return str_operation_refusal(a, library_call_expr(a, head, args, nargs),
                atom_symbol(a, "argument tuple exceeds native arity"), values);
        operands = args[1]->expr.elems;
        count = (uint32_t)args[1]->expr.len;
    }
    CettaStrRefusal refusal = {0};
    Atom *result = count == native->arity ? native->fn(a, operands, &refusal) : NULL;
    if (result) {
        /* A successful string operation has produced data, even when that
         * data is a sequence or a symbol naming an executable expression. */
        *result_form = CETTA_CALL_RESULT_COMPLETED_VALUE;
        return result;
    }
    size_t public_len = strlen(name) + 5u;
    char *public_name = arena_alloc(a, public_len);
    snprintf(public_name, public_len, "str:%s", name);
    Atom *call = library_call_expr(a, atom_symbol(a, public_name), operands, count);
    if (count != native->arity)
        return str_operation_refusal(a, call, atom_symbol(a, "wrong number of arguments"), values);
    if (refusal.reason)
        return str_operation_refusal(a, call,
                          atom_expr2(a, atom_symbol(a, refusal.reason),
                                     atom_int(a, (int64_t)refusal.offset)), values);
    return str_operation_refusal(a, call, atom_symbol(a, refusal.expected), values);
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

static Atom *library_space_backend_error(Arena *a, Atom *head, Atom **args,
                                         uint32_t nargs) {
    SpaceMatchBackendError code = space_match_backend_last_error_code();
    if (code == SPACE_MATCH_BACKEND_ERROR_NONE)
        return NULL;
    return atom_error(a, library_call_expr(a, head, args, nargs),
                      atom_symbol(a, space_match_backend_error_name(code)));
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
    uint64_t packet_rows = 0;
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
    space_match_backend_clear_error();

    if (!space_match_backend_materialize_attached(
            space, eval_current_persistent_arena())) {
        goto fail;
    }

    if (!cetta_mork_bridge_is_available())
        goto fail;

    CettaMorkSpaceHandle *bridge = cetta_mork_bridge_space_new();
    if (!bridge)
        goto fail;

    CettaCount logical_len = space_length64(space);
    CettaCount n = logical_len;
    if (logical_len > space_match_backend_native_materialization_limit()) {
        space_match_backend_set_error(SPACE_MATCH_BACKEND_ERROR_NATIVE_SPACE_TOO_LARGE);
        goto fail;
    }
    if (space->native.universe) {
        AtomId *atom_ids = arena_alloc(&scratch, sizeof(AtomId) * (size_t)(n ? n : 1u));
        uint8_t *packet = NULL;
        size_t packet_len = 0;
        const char *pack_error = NULL;
        for (CettaIndex i = 0; i < n; i++)
            atom_ids[i] = space_get_atom_id_at64(space, i);
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
        for (CettaIndex i = 0; i < n; i++) {
            ArenaMark mark = arena_mark(&scratch);
            uint8_t *expr_bytes = NULL;
            size_t expr_len = 0;
            const char *encode_error = NULL;
            bool ok = cetta_mm2_atom_to_bridge_expr_bytes(
                &scratch, space_get_at64(space, i), &expr_bytes, &expr_len,
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

static bool mork_bridge_import_act_additive(CettaMorkSpaceHandle *dst_bridge,
                                            const char *path) {
    CettaMorkSpaceHandle *loaded_bridge = NULL;
    uint64_t loaded = 0;
    uint64_t added = 0;
    bool ok = false;

    if (!dst_bridge)
        return false;

    loaded_bridge = cetta_mork_bridge_space_new();
    if (!loaded_bridge)
        return false;

    if (!cetta_mork_bridge_space_load_act_file(
            loaded_bridge, (const uint8_t *)path, strlen(path), &loaded)) {
        goto cleanup;
    }
    if (!cetta_mork_bridge_space_add_logical_rows_from(
            dst_bridge, loaded_bridge, &added)) {
        goto cleanup;
    }

    ok = true;

cleanup:
    cetta_mork_bridge_space_free(loaded_bridge);
    (void)loaded;
    (void)added;
    return ok;
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
    if (!mork_bridge_import_act_additive(bridge, resolved)) {
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

typedef struct {
    Arena *arena;
    Atom *template_atom;
    Atom **items;
    CettaCount len;
    CettaCount cap;
} LibraryMorkMatchRows;

static CettaCount library_mork_atom_rows_capacity_limit(void) {
    return (CettaCount)(SIZE_MAX / sizeof(Atom *));
}

static bool library_mork_match_rows_push(LibraryMorkMatchRows *rows,
                                         Atom *atom) {
    CettaCount limit;
    CettaCount next;
    if (!rows || !atom)
        return false;
    limit = library_mork_atom_rows_capacity_limit();
    if (rows->len >= limit) {
        space_match_backend_set_error(SPACE_MATCH_BACKEND_ERROR_PACKET_TOO_LARGE);
        return false;
    }
    if (rows->len >= rows->cap) {
        next = rows->cap ? rows->cap * 2u : 8u;
        if (next <= rows->cap || next < rows->len + 1u)
            next = rows->len + 1u;
        if (next > limit)
            next = limit;
        if (next < rows->len + 1u) {
            space_match_backend_set_error(SPACE_MATCH_BACKEND_ERROR_PACKET_TOO_LARGE);
            return false;
        }
        rows->items = cetta_realloc(rows->items, sizeof(Atom *) * (size_t)next);
        rows->cap = next;
    }
    rows->items[rows->len++] = atom;
    return true;
}

static bool library_mork_match_visit_row(const Bindings *bindings, void *ctx) {
    LibraryMorkMatchRows *rows = ctx;
    if (!rows || !bindings)
        return false;
    Atom *applied = bindings_apply_if_vars(bindings, rows->arena,
                                           rows->template_atom);
    return library_mork_match_rows_push(rows,
                                        library_quote_atom(rows->arena, applied));
}

typedef struct {
    Atom **items;
    CettaCount len;
    CettaCount cap;
} LibraryMorkAtomRows;

static bool library_mork_atom_rows_push(LibraryMorkAtomRows *rows, Atom *atom) {
    CettaCount limit;
    CettaCount next;
    if (!rows || !atom)
        return false;
    limit = library_mork_atom_rows_capacity_limit();
    if (rows->len >= limit) {
        space_match_backend_set_error(SPACE_MATCH_BACKEND_ERROR_PACKET_TOO_LARGE);
        return false;
    }
    if (rows->len >= rows->cap) {
        next = rows->cap ? rows->cap * 2u : 8u;
        if (next <= rows->cap || next < rows->len + 1u)
            next = rows->len + 1u;
        if (next > limit)
            next = limit;
        if (next < rows->len + 1u) {
            space_match_backend_set_error(SPACE_MATCH_BACKEND_ERROR_PACKET_TOO_LARGE);
            return false;
        }
        rows->items = cetta_realloc(rows->items, sizeof(Atom *) * (size_t)next);
        rows->cap = next;
    }
    rows->items[rows->len++] = atom;
    return true;
}

static bool library_mork_atoms_visit_row(Atom *atom, void *ctx) {
    return library_mork_atom_rows_push((LibraryMorkAtomRows *)ctx, atom);
}

static Atom *mork_space_syntax_native(CettaLibraryContext *ctx,
                                       Arena *a, Atom *head, Atom **args,
                                       uint32_t nargs, SymbolId which) {
    CettaMorkSpaceResource *target = NULL;
    CettaMorkSpaceHandle *bridge = NULL;

    if (which == g_builtin_syms.lib_mork_space_match) {
        LibraryMorkMatchRows rows = {
            .arena = a,
            .template_atom = NULL,
            .items = NULL,
            .len = 0,
            .cap = 0,
        };

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
        rows.template_atom = template;
        if (!library_mork_space_bridge(target, &bridge) || !bridge) {
            Atom *space_error = library_space_backend_error(a, head, args, nargs);
            if (space_error)
                return space_error;
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK match bridge unavailable: ");
        }
        bool direct_ok = false;
        if (pattern->kind == ATOM_EXPR && pattern->expr.len >= 3 &&
            atom_is_symbol_id(pattern->expr.elems[0], g_builtin_syms.comma)) {
            direct_ok = space_match_backend_mork_visit_conjunction_direct(
                    bridge, a, pattern->expr.elems + 1,
                    pattern->expr.len - 1, NULL,
                    library_mork_match_visit_row, &rows);
        } else {
            direct_ok = space_match_backend_mork_visit_bindings_direct(
                bridge, a, pattern, library_mork_match_visit_row, &rows);
        }

        if (!direct_ok) {
            Atom *space_error = library_space_backend_error(a, head, args, nargs);
            free(rows.items);
            if (space_error)
                return space_error;
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK direct match failed: ");
        }

        Atom *result = atom_expr(a, NULL, 0);
        if (rows.len > 0) {
            CettaExprLen result_len = 0;
            if (!library_atom_expr_len_checked(rows.len, &result_len)) {
                free(rows.items);
                return atom_error(a, library_call_expr(a, head, args, nargs),
                                  atom_symbol(a, "ArityTooLarge"));
            }
            Atom **elems = arena_alloc(a, sizeof(Atom *) * rows.len);
            memcpy(elems, rows.items, sizeof(Atom *) * rows.len);
            result = atom_expr(a, elems, result_len);
        }
        free(rows.items);
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
        uint64_t rows = 0;
        LibraryMorkAtomRows atom_rows = {0};

        if (nargs != 1) {
            return library_signature_error(a, head, args, nargs,
                                           "expected MorkSpace");
        }
        if (!library_mork_space_bridge(target, &bridge) || !bridge) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK get-atoms bridge unavailable: ");
        }
        Arena *persistent = eval_current_persistent_arena();
        TermUniverse *universe = cetta_library_space_universe(ctx, persistent);
        if (universe) {
            Arena scratch;
            arena_init(&scratch);
            arena_set_runtime_kind(&scratch, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
            bool ok = space_match_backend_mork_visit_atoms_direct(
                bridge, universe, &scratch, library_mork_atoms_visit_row,
                &atom_rows);
            arena_free(&scratch);
            if (!ok) {
                Atom *space_error = library_space_backend_error(a, head, args, nargs);
                free(atom_rows.items);
                if (space_error)
                    return space_error;
                return library_mork_bridge_error(a, head, args, nargs,
                                                 "MORK get-atoms failed: ");
            }
            Atom *result = atom_expr(a, NULL, 0);
            if (atom_rows.len > 0) {
                CettaExprLen result_len = 0;
                if (!library_atom_expr_len_checked(atom_rows.len, &result_len)) {
                    free(atom_rows.items);
                    return atom_error(a, library_call_expr(a, head, args, nargs),
                                      atom_symbol(a, "ArityTooLarge"));
                }
                Atom **elems = arena_alloc(a, sizeof(Atom *) * atom_rows.len);
                memcpy(elems, atom_rows.items, sizeof(Atom *) * atom_rows.len);
                result = atom_expr(a, elems, result_len);
            }
            free(atom_rows.items);
            return library_quote_atom(a, result);
        }
        if (!cetta_mork_bridge_space_dump(bridge, &packet, &len, &rows)) {
            return library_mork_bridge_error(a, head, args, nargs,
                                             "MORK get-atoms failed: ");
        }
        if (!library_atom_expr_len_checked(rows, NULL)) {
            cetta_mork_bridge_bytes_free(packet, len);
            return atom_error(a, library_call_expr(a, head, args, nargs),
                              atom_symbol(a, "ArityTooLarge"));
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
                      atom_string(a, "unknown mork syntax helper"));
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
        if (!mork_bridge_import_act_additive(bridge, plan.canonical_path)) {
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
                                             "MORK zipper validity check failed: ");
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
                                             "MORK product-zipper validity check failed: ");
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
    uint64_t count = 0;
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
                                             "MORK overlay-zipper validity check failed: ");
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
    SymbolId head_id = atom_symbol_current_id(head);
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
        return mork_space_syntax_native(ctx, a, head, args, nargs, head_id);
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
    text = library_mm2_syntax_text(a, args[1]);
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
        char *syntax = cetta_mm2_atom_to_syntax_string(&parse_arena, atoms[i]);
        bool ok;
        if (cetta_mm2_atom_is_exec_rule(atoms[i])) {
            ok = cetta_mork_bridge_program_add_sexpr(program,
                                                     (const uint8_t *)syntax,
                                                     strlen(syntax), NULL);
        } else {
            ok = cetta_mork_bridge_context_add_sexpr(context,
                                                     (const uint8_t *)syntax,
                                                     strlen(syntax), NULL);
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
    uint64_t rows = 0;
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
    text = library_mm2_syntax_text(a, args[1]);
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
    uint64_t rows = 0;
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
    SymbolId head_id = atom_symbol_current_id(head);
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

typedef enum {
    CETTA_PETTA_DOCUMENT_OK = 0,
    CETTA_PETTA_DOCUMENT_PLAN_FAILED,
    CETTA_PETTA_DOCUMENT_COPY_FAILED,
    CETTA_PETTA_DOCUMENT_EVAL_FAILED,
    CETTA_PETTA_DOCUMENT_CAPACITY,
    /* Not a failure: the run returned at a language Error value. */
    CETTA_PETTA_DOCUMENT_STOPPED_AT_ERROR,
} CettaPettaDocumentFailure;

static bool cetta_library_petta_check_segment(
    CettaLibraryContext *ctx, Space *space, Registry *registry,
    Arena *eval_arena, AtomId *atom_ids, int atom_count,
    bool declaration_admission,
    CettaPettaDocumentFailure *failure_out, Atom **detail_out) {
#if CETTA_BUILD_WITH_PETTA_TYPECHECK_V2
    if (!ctx || !ctx->session.profile ||
        !cetta_profile_uses_petta_typing(ctx->session.profile) ||
        ctx->petta_trusted_library_import_depth > 0u)
        return true;
    if (!space || !space->native.universe || !eval_arena ||
        !atom_ids || atom_count <= 0)
        return false;
    Atom **forms = cetta_malloc(
        sizeof(*forms) * (size_t)atom_count);
    if (!forms) {
        if (failure_out)
            *failure_out = CETTA_PETTA_DOCUMENT_EVAL_FAILED;
        return false;
    }
    for (int index = 0; index < atom_count; index++)
        forms[index] = term_universe_get_atom(
            space->native.universe, atom_ids[index]);
    PettaTypecheckPolicy policy = PETTA_TYPECHECK_POLICY_DEFAULT;
    /* Strictness is a policy of the authored requesting unit.  Imported
     * units receive baseline typecheck-v2 validation and do not inherit
     * a caller's --strict/--strict-det lint policy retroactively. */
    bool use_v3 = ctx->session.profile->id ==
        CETTA_PROFILE_PETTA_TYPECHECK_V3;
    PettaTypecheckBlockResult checked = {0};
    CettaPettaTypecheckV3CompatibilityResult checked_v3 = {0};
    bool judged = use_v3
        ? cetta_petta_typecheck_v3_compatibility_block(
              ctx->petta_typecheck_v3, ctx->petta_program, space, registry,
              forms, (size_t)atom_count, policy, declaration_admission,
              &checked_v3)
        : declaration_admission
            ? petta_typecheck_declaration_admission_selected(
                  ctx->petta_program, space, registry,
                  forms, (size_t)atom_count, policy, &checked)
            : petta_typecheck_declaration_block_selected(
                  ctx->petta_program, space, registry,
                  forms, (size_t)atom_count, policy, &checked);
    PettaTypecheckVerdict verdict = use_v3
        ? checked_v3.verdict : checked.verdict;
    const char *diagnostic = use_v3
        ? checked_v3.diagnostic : checked.diagnostic;
    Atom *source = forms[0];
    free(forms);
    if (judged && verdict != PETTA_TYPECHECK_REFUTED)
        return true;
    if (failure_out)
        *failure_out = CETTA_PETTA_DOCUMENT_EVAL_FAILED;
    if (detail_out) {
        *detail_out = petta_typecheck_error_atom(
            eval_arena, source,
            judged ? 2 : 1,
            diagnostic[0]
                ? diagnostic
                : judged
                    ? "imported declaration block rejected"
                    : "imported declaration analysis failed");
    }
    return false;
#else
    (void)ctx;
    (void)space;
    (void)registry;
    (void)eval_arena;
    (void)atom_ids;
    (void)atom_count;
    (void)declaration_admission;
    (void)failure_out;
    (void)detail_out;
    return true;
#endif
}

/*
 * Execute an already parsed PeTTa document.  The first pass makes equation
 * arities visible to every source plan; the second pass preserves source
 * order for declarations, effects, and runnable answer bags.  File imports
 * and process_metta_string deliberately share this one semantic path.
 */
/* Read the tokens of the HE module being loaded into one statement, as HE's
 * tokenizer does when it reads the module's text: its `&self` and the tokens
 * its code has bound before the statement. */
static AtomId library_module_capture_tokens(CettaLibraryContext *ctx,
                                            Space *work_space,
                                            Arena *eval_arena,
                                            Arena *persistent_arena,
                                            Registry *registry,
                                            AtomId statement) {
    if (statement == CETTA_ATOM_ID_NONE)
        return statement;
    TermUniverse *universe = work_space->native.universe;
    /* Text read outside a module being loaded, such as an included file,
     * is read with the words of the registry in effect. */
    if (ctx->session.language_id != CETTA_LANGUAGE_HE)
        return statement;
    Registry *scope = ctx->module_token_scope ? ctx->module_token_scope : registry;
    return eval_read_registry_words_id(scope, universe, eval_arena,
        persistent_arena ? persistent_arena : eval_arena, statement);
}

static bool cetta_library_petta_execute_listed_document_ids(
    CettaLibraryContext *ctx, Space *work_space,
    Arena *eval_arena, Arena *persistent_arena,
    Registry *registry, AtomId *atom_ids, int atom_count,
    ResultSet *runnable_results, ResultSet *last_results,
    CettaDocumentResultObserver observer, void *observer_context,
    CettaPettaDocumentFailure *failure_out,
    Atom **detail_out);

typedef struct {
    const AtomId *ids;
    int count;
} CettaLibraryDocumentRoots;

/* A module's forms stay in the universe while its document runs. */
static void cetta_library_document_roots(const TermUniverse *universe,
                                         TermUniverseRootVisit visit,
                                         void *visit_context, void *context) {
    const CettaLibraryDocumentRoots *document = context;
    (void)universe;
    for (int i = 0; i < document->count; i++)
        visit(visit_context, document->ids[i]);
}

static bool cetta_library_petta_execute_document_ids(
    CettaLibraryContext *ctx, Space *work_space,
    Arena *eval_arena, Arena *persistent_arena,
    Registry *registry, AtomId *atom_ids, int atom_count,
    ResultSet *runnable_results, ResultSet *last_results,
    CettaDocumentResultObserver observer, void *observer_context,
    CettaPettaDocumentFailure *failure_out,
    Atom **detail_out) {
    TermUniverse *universe = work_space ? work_space->native.universe : NULL;
    CettaLibraryDocumentRoots roots = {.ids = atom_ids,
                                       .count = atom_ids ? atom_count : 0};
    if (universe)
        (void)term_universe_add_root_provider(
            universe, cetta_library_document_roots, &roots);
    bool ok = cetta_library_petta_execute_listed_document_ids(
        ctx, work_space, eval_arena, persistent_arena, registry, atom_ids,
        atom_count, runnable_results, last_results, observer,
        observer_context, failure_out, detail_out);
    if (universe)
        term_universe_remove_root_provider(
            universe, cetta_library_document_roots, &roots);
    return ok;
}

static bool cetta_library_petta_execute_listed_document_ids(
    CettaLibraryContext *ctx, Space *work_space,
    Arena *eval_arena, Arena *persistent_arena,
    Registry *registry, AtomId *atom_ids, int atom_count,
    ResultSet *runnable_results, ResultSet *last_results,
    CettaDocumentResultObserver observer, void *observer_context,
    CettaPettaDocumentFailure *failure_out,
    Atom **detail_out) {
    if (failure_out)
        *failure_out = CETTA_PETTA_DOCUMENT_OK;
    if (detail_out)
        *detail_out = NULL;
    if (last_results)
        last_results->len = 0u;
    if (!ctx || !work_space || !work_space->native.universe ||
        !eval_arena || atom_count < 0 ||
        (atom_count > 0 && !atom_ids)) {
        if (failure_out)
            *failure_out = CETTA_PETTA_DOCUMENT_COPY_FAILED;
        return false;
    }

    if (ctx->petta_program) {
        for (int declaration_index = 0;
             declaration_index < atom_count;
             declaration_index++) {
            AtomId declaration_id = atom_ids[declaration_index];
            if (tu_kind(work_space->native.universe, declaration_id) ==
                    ATOM_SYMBOL &&
                tu_sym(work_space->native.universe, declaration_id) ==
                    g_builtin_syms.bang &&
                declaration_index + 1 < atom_count) {
                declaration_index++;
                continue;
            }
            Atom *declaration = term_universe_get_atom(
                work_space->native.universe, declaration_id);
            if (petta_program_admits_equation(ctx->petta_program, declaration) &&
                !petta_program_predeclare_equation(
                    ctx->petta_program, work_space, declaration)) {
                if (failure_out)
                    *failure_out =
                        CETTA_PETTA_DOCUMENT_PLAN_FAILED;
                return false;
            }
        }
    }

    int index = 0;
    while (index < atom_count) {
        AtomId atom_id = atom_ids[index];
        if (tu_kind(work_space->native.universe, atom_id) ==
                ATOM_SYMBOL &&
            tu_sym(work_space->native.universe, atom_id) ==
                g_builtin_syms.bang &&
            index + 1 < atom_count) {
            atom_ids[index + 1] = library_module_capture_tokens(
                ctx, work_space, eval_arena, persistent_arena, registry,
                atom_ids[index + 1]);
            if (!cetta_library_petta_check_segment(
                    ctx, work_space, registry, eval_arena,
                    atom_ids + index, 2, false,
                    failure_out, detail_out))
                return false;
            cetta_petta_prepare_document_forms(
                work_space->native.universe, atom_ids + index, 2);
            /* A directive's answer list claims there are no further
             * answers, so it is observed with a completion tracker.  An
             * incomplete observation is a failure of the document, never a
             * shorter answer list. */
            EvalOutcome observed;
            eval_outcome_init(&observed);
            const PettaPlanNode *source_plan = NULL;
            if (ctx->petta_program) {
                Atom *source = term_universe_get_atom(
                    work_space->native.universe,
                    atom_ids[index + 1]);
                source_plan = source
                    ? petta_program_plan_current(
                          ctx->petta_program, source)
                    : NULL;
                if (!source_plan) {
                    eval_outcome_free(&observed);
                    if (failure_out)
                        *failure_out =
                            CETTA_PETTA_DOCUMENT_PLAN_FAILED;
                    return false;
                }
            }
            Atom *eval_form = term_universe_copy_atom(
                work_space->native.universe,
                persistent_arena ? persistent_arena : eval_arena,
                atom_ids[index + 1]);
            if (!eval_form) {
                eval_outcome_free(&observed);
                if (failure_out)
                    *failure_out =
                        CETTA_PETTA_DOCUMENT_COPY_FAILED;
                return false;
            }
            eval_top_with_registry_petta_plan_outcome(
                work_space, eval_arena,
                persistent_arena, registry,
                eval_form, source_plan, &observed);
            ResultSet *results = &observed.results;

            Atom *first_error =
                observed.completion != CETTA_EVAL_COMPLETE
                    ? atom_error(
                          eval_arena, eval_form,
                          atom_symbol(
                              eval_arena,
                              eval_completion_reason(
                                  observed.completion)))
                    : result_set_first_error(eval_arena, results);
            bool has_error =
                first_error != NULL ||
                result_set_has_error(results);
            /* HE compatibility include and Python runner observations retain
               complete directive frontiers, including language Error values.
               Imports and PeTTa text processing keep their failure policy. */
            if (last_results && observed.completion == CETTA_EVAL_COMPLETE) {
                last_results->len = 0u;
                for (CettaCount result_index = 0u;
                     result_index < results->len; result_index++) {
                    Atom *copy = atom_deep_copy_shared(
                        persistent_arena ? persistent_arena : eval_arena,
                        results->items[result_index]);
                    CettaCount previous_len = last_results->len;
                    if (copy)
                        result_set_add(last_results, copy);
                    if (!copy || last_results->len != previous_len + 1u) {
                        eval_outcome_free(&observed);
                        eval_release_temporary_spaces();
                        if (failure_out)
                            *failure_out = CETTA_PETTA_DOCUMENT_COPY_FAILED;
                        return false;
                    }
                }
            }
            bool language_error_return = (last_results || observer) && has_error &&
                observed.completion == CETTA_EVAL_COMPLETE;
            if (observer && observed.completion == CETTA_EVAL_COMPLETE &&
                !observer(observer_context, results)) {
                eval_outcome_free(&observed);
                eval_release_temporary_spaces();
                if (failure_out)
                    *failure_out = CETTA_PETTA_DOCUMENT_COPY_FAILED;
                return false;
            }
            if (!has_error && runnable_results) {
                for (CettaCount result_index = 0u;
                     result_index < results->len;
                     result_index++) {
                    CettaCount previous_len =
                        runnable_results->len;
                    result_set_add(
                        runnable_results,
                        results->items[result_index]);
                    if (runnable_results->len !=
                            previous_len + 1u) {
                        eval_outcome_free(&observed);
                        eval_release_temporary_spaces();
                        if (failure_out)
                            *failure_out =
                                CETTA_PETTA_DOCUMENT_CAPACITY;
                        return false;
                    }
                }
            }
            eval_outcome_free(&observed);
            eval_release_temporary_spaces();
            if (language_error_return) {
                if (failure_out)
                    *failure_out = CETTA_PETTA_DOCUMENT_STOPPED_AT_ERROR;
                return true;
            }
            if (has_error) {
                if (failure_out)
                    *failure_out =
                        CETTA_PETTA_DOCUMENT_EVAL_FAILED;
                if (detail_out)
                    *detail_out = first_error;
                return false;
            }
            index += 2;
            continue;
        }

        int block_end = index + 1;
        while (block_end < atom_count) {
            AtomId candidate = atom_ids[block_end];
            if (tu_kind(
                    work_space->native.universe,
                    candidate) == ATOM_SYMBOL &&
                tu_sym(
                    work_space->native.universe,
                    candidate) == g_builtin_syms.bang &&
                block_end + 1 < atom_count) {
                break;
            }
            block_end++;
        }
        /* No statement in the block binds a token, so the whole block
           reads the tokens bound before it. */
        for (int block_index = index; block_index < block_end;
             block_index++) {
            atom_ids[block_index] = library_module_capture_tokens(
                ctx, work_space, eval_arena, persistent_arena, registry,
                atom_ids[block_index]);
        }
        if (!cetta_library_petta_check_segment(
                ctx, work_space, registry, eval_arena,
                atom_ids + index, block_end - index,
                true,
                failure_out, detail_out))
            return false;
        cetta_petta_prepare_document_forms(
            work_space->native.universe, atom_ids + index,
            block_end - index);
        PettaDeclarationBlock *block = NULL;
        if (ctx->petta_program) {
            block = petta_program_declaration_block_new(
                ctx->petta_program,
                work_space->native.universe,
                atom_ids + index, block_end - index);
            if (!block) {
                if (failure_out)
                    *failure_out =
                        CETTA_PETTA_DOCUMENT_PLAN_FAILED;
                return false;
            }
        }
        for (int block_index = index;
             block_index < block_end;
             block_index++) {
            AtomId block_atom_id = atom_ids[block_index];
            Atom *source = NULL;
            const PettaPlanNode *source_plan = NULL;
            if (block) {
                source = term_universe_get_atom(
                    work_space->native.universe,
                    block_atom_id);
                source_plan =
                    petta_program_declaration_block_plan_at(
                        block, block_index - index);
                if (!source) {
                    petta_program_declaration_block_free(block);
                    if (failure_out)
                        *failure_out =
                            CETTA_PETTA_DOCUMENT_PLAN_FAILED;
                    return false;
                }
            }
            if (!source)
                source = term_universe_get_atom(
                    work_space->native.universe, block_atom_id);
            Atom *definition_error = eval_petta_builtin_definition_error(
                eval_arena, registry, work_space, source);
            if (definition_error) {
                /* PeTTa stores the equation row before its compiler's
                 * assertz rejects the protected executable predicate. */
                space_add_atom_id(work_space, block_atom_id);
                petta_program_declaration_block_free(block);
                if (failure_out)
                    *failure_out = CETTA_PETTA_DOCUMENT_EVAL_FAILED;
                if (detail_out)
                    *detail_out = definition_error;
                return false;
            }
            space_add_atom_id(work_space, block_atom_id);
            if (block &&
                !petta_program_note_add(
                    ctx->petta_program, work_space,
                    source, source_plan)) {
                petta_program_declaration_block_free(block);
                if (failure_out)
                    *failure_out =
                        CETTA_PETTA_DOCUMENT_PLAN_FAILED;
                return false;
            }
        }
        if (ctx->petta_program &&
            cetta_profile_uses_petta_typing(ctx->session.profile) &&
            ctx->petta_trusted_library_import_depth == 0u) {
#if CETTA_BUILD_WITH_PETTA_TYPECHECK_V2
            petta_typecheck_inferred_signatures_rebase_selected(
                ctx->petta_program, work_space,
                PETTA_TYPECHECK_POLICY_DEFAULT);
#else
            petta_program_inferred_signatures_rebase(
                ctx->petta_program, work_space);
#endif
        }
        petta_program_declaration_block_free(block);
        index = block_end;
    }
    return true;
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

bool cetta_library_petta_process_text(
    CettaLibraryContext *ctx, Space *space,
    Arena *eval_arena, Arena *persistent_arena,
    Registry *registry, const char *text,
    Atom **result_out, Atom **error_out) {
    if (result_out)
        *result_out = NULL;
    if (error_out)
        *error_out = NULL;
    if (!ctx || !space || !eval_arena || !text ||
        !result_out || !error_out) {
        if (error_out)
            *error_out = atom_symbol(
                eval_arena, "PettaProcessTextInvalidArgument");
        return false;
    }

    AtomId *atom_ids = NULL;
    char diagnostic[512];
    int atom_count = ctx->session.language_id == CETTA_LANGUAGE_PRIME
        ? prime_semantics_read_source_text_ids(NULL, text, space->native.universe,
            &atom_ids, diagnostic, sizeof(diagnostic))
        : parse_metta_text_ids_diagnostic(text, space->native.universe,
            &atom_ids, diagnostic, sizeof(diagnostic));
    if (atom_count < 0) {
        const char *message = diagnostic[0] != '\0'
            ? diagnostic : "invalid PeTTa source";
        *error_out = atom_expr2(
            eval_arena,
            atom_symbol(
                eval_arena, "PettaProcessTextParseFailed"),
            atom_string(eval_arena, message));
        free(atom_ids);
        return false;
    }

    ResultSet runnable_results;
    result_set_init(&runnable_results);
    CettaPettaDocumentFailure failure =
        CETTA_PETTA_DOCUMENT_OK;
    Atom *detail = NULL;
    bool ok = cetta_library_petta_execute_document_ids(
        ctx, space, eval_arena, persistent_arena,
        registry, atom_ids, atom_count,
        &runnable_results, NULL, NULL, NULL, &failure, &detail);
    free(atom_ids);
    if (!ok) {
        if (failure == CETTA_PETTA_DOCUMENT_EVAL_FAILED &&
            detail) {
            *error_out = detail;
        } else {
            const char *tag =
                failure == CETTA_PETTA_DOCUMENT_PLAN_FAILED
                    ? "PettaProcessTextPlanFailed"
                    : failure == CETTA_PETTA_DOCUMENT_COPY_FAILED
                        ? "PettaProcessTextCopyFailed"
                        : failure == CETTA_PETTA_DOCUMENT_CAPACITY
                            ? "PettaProcessTextCapacity"
                            : "PettaProcessTextEvaluationFailed";
            *error_out = atom_symbol(eval_arena, tag);
        }
        result_set_free(&runnable_results);
        return false;
    }

    *result_out = atom_expr(
        eval_arena, runnable_results.items,
        runnable_results.len);
    result_set_free(&runnable_results);
    if (!*result_out) {
        *error_out = atom_symbol(
            eval_arena, "PettaProcessTextCapacity");
        return false;
    }
    return true;
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
    uint64_t rows = 0;
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
    SpaceTransferEndpoint import_dst = {
        .kind = SPACE_TRANSFER_ENDPOINT_SPACE,
        .space = &imported,
    };
    SpaceTransferEndpoint import_src = {
        .kind = SPACE_TRANSFER_ENDPOINT_MORK_BRIDGE,
        .bridge = bridge,
    };
    switch (space_match_backend_transfer_resolved_result(import_dst, import_src,
                                                         persistent_arena, NULL)) {
    case SPACE_TRANSFER_OK:
        {
            uint32_t n = 0;
            if (!space_length_u32_checked(&imported, &n)) {
                *error_out = module_reason(
                    ctx, eval_arena, space_match_backend_last_error(), path);
                goto cleanup;
            }
            for (uint32_t i = 0; i < n; i++) {
                space_add_atom_id(work_space, space_get_atom_id_at(&imported, i));
            }
        }
        break;
    case SPACE_TRANSFER_NEEDS_TEXT_FALLBACK:
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
    case SPACE_TRANSFER_ERROR:
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
    if (space_match_backend_logical_len64(target_space) != 0) {
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

static bool read_module_source(const char *path, CettaModuleSource *source) {
    FILE *f = fopen(path, "rb");
    uint8_t *text = NULL;
    size_t cap = 4096;
    size_t nread = 0;

    *source = (CettaModuleSource){0};
    if (!f)
        return false;

    text = malloc(cap + 1u);
    if (!text) {
        fclose(f);
        return false;
    }
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
            if (cap > (SIZE_MAX - 1u) / 2u) {
                fclose(f);
                free(text);
                return false;
            }
            cap *= 2u;
            uint8_t *grown = realloc(text, cap + 1u);
            if (!grown) {
                fclose(f);
                free(text);
                return false;
            }
            text = grown;
        }
    }

    fclose(f);
    text[nread] = '\0';
    source->bytes = text;
    source->length = nread;
    return true;
}

static bool read_mork_include_text_file(const char *path, char **text_out) {
    CettaModuleSource source;
    bool ok = read_module_source(path, &source);
    *text_out = (char *)source.bytes;
    return ok;
}

/* A library's definitions and dialect vocabulary are one initialization
 * source. Capture both before effects, including both in failure versioning;
 * adding the vocabulary does not introduce a nested include transaction. */
static bool read_he_module_source(CettaLibraryContext *ctx,
                                  const CettaImportPlan *plan,
                                  CettaModuleSource *source) {
    if (!read_module_source(plan->canonical_path, source))
        return false;
    const char *words = library_he_words(ctx, plan->provider_kind,
                                         plan->spec.raw_spec);
    if (!words)
        return true;
    char path[PATH_MAX];
    CettaModuleSource vocabulary = {0};
    bool ok = build_library_path(ctx, words, path, sizeof(path)) &&
        read_module_source(path, &vocabulary);
    if (ok && source->length <= SIZE_MAX - 2u &&
        vocabulary.length <= SIZE_MAX - source->length - 2u) {
        size_t length = source->length + 1u + vocabulary.length;
        uint8_t *bytes = realloc(source->bytes, length + 1u);
        if (bytes) {
            bytes[source->length] = '\n';
            memcpy(bytes + source->length + 1u, vocabulary.bytes,
                   vocabulary.length + 1u);
            *source = (CettaModuleSource){bytes, length};
            free(vocabulary.bytes);
            return true;
        }
    }
    free(vocabulary.bytes);
    free(source->bytes);
    *source = (CettaModuleSource){0};
    return false;
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

/* A syntax failure does not suppress effects of the complete prefix.
 * Parsing and statement execution retain their established implementations. */
static bool load_he_document(CettaLibraryContext *ctx, const char *path,
                              Space *space, Arena *eval_arena,
                              Arena *persistent_arena, Registry *registry,
                              const CettaModuleSource *source,
                              ResultSet *last_results, Atom **error_out) {
    char diagnostic[512] = {0};
    AtomId *ids = NULL;
    int count = 0;
    int parsed = source ? parse_metta_bytes_prefix_ids(
        source->bytes, source->length, space->native.universe,
        &ids, &count, diagnostic, sizeof(diagnostic)) : parse_metta_file_prefix_ids(
        path, space->native.universe, &ids, &count, diagnostic, sizeof(diagnostic));
    /* A trailing directive marker has no complete body to execute. */
    if (count > 0 && tu_kind(space->native.universe, ids[count - 1]) == ATOM_SYMBOL &&
        tu_sym(space->native.universe, ids[count - 1]) == g_builtin_syms.bang)
        count--;
    CettaPettaDocumentFailure failure = CETTA_PETTA_DOCUMENT_OK;
    Atom *detail = NULL;
    bool ok = cetta_library_petta_execute_document_ids(
        ctx, space, eval_arena, persistent_arena, registry,
        ids, count, NULL, last_results, NULL, NULL, &failure, &detail);
    free(ids);
    if (!ok) {
        *error_out = detail ? detail : module_reason(
            ctx, eval_arena,
            failure == CETTA_PETTA_DOCUMENT_COPY_FAILED ? "ModuleParseFailed"
                : failure == CETTA_PETTA_DOCUMENT_EVAL_FAILED ? "ModuleInitError"
                : "ModuleCompilePlanFailed", path);
    } else if (parsed < 0 &&
               failure != CETTA_PETTA_DOCUMENT_STOPPED_AT_ERROR) {
        *error_out = module_reason(ctx, eval_arena, "ModuleParseFailed", path);
        ok = false;
        /* Upstream's include has added the statements before the fault. */
        if (last_results)
            ctx->include_fault_keeps_prefix = true;
    }
    return ok;
}

static bool load_module_file(CettaLibraryContext *ctx, const char *path,
                             Space *logical_space, Space *work_space,
                             Arena *eval_arena,
                             Arena *persistent_arena, Registry *registry,
                             int fuel, const CettaModuleSource *source,
                             ResultSet *last_results, Atom **error_out) {
    int64_t slot = imported_file_lookup(ctx, logical_space, path);
    if (slot >= 0) {
        if (ctx->imported_files[slot].loading) {
            const CettaLanguageVersion *version = cetta_language_version_of(
                ctx->session.language_id, ctx->session.profile);
            if (version && version->loading_import_is_noop)
                return true;
            *error_out = module_reason(ctx, eval_arena, "ModuleImportCycle", path);
            return false;
        }
        if (!last_results)
            return true;
    }
    if (slot < 0) {
        if (!library_reserve_imported_file(ctx)) {
            *error_out = atom_symbol(eval_arena, "module file inventory allocation failed");
            return false;
        }
        slot = ctx->imported_file_len++;
    }
    ctx->imported_files[slot].space = logical_space;
    ctx->imported_files[slot].loading = true;
    snprintf(ctx->imported_files[slot].path,
             sizeof(ctx->imported_files[slot].path), "%s", path);

    char import_dir[PATH_MAX];
    copy_language_source_dir(ctx, import_dir, sizeof(import_dir), path);
    if (!cetta_library_push_dir(ctx, import_dir)) {
        ctx->imported_files[slot].loading = false;
        ctx->imported_files[slot].path[0] = '\0';
        *error_out = atom_symbol(eval_arena, "module resource scope allocation failed");
        return false;
    }

    Atom *prev_self = NULL;
    if (registry) {
        prev_self = registry_lookup_id(registry, g_builtin_syms.self);
        /* Token capture must never publish the temporary transaction owner.
         * Reads/writes resolve this stable identity through the active alias. */
        Atom *self_value = atom_space(persistent_arena,
                                      logical_import_space(ctx, logical_space));
        cetta_provenance_assert_not_transient(self_value,
                                             "library.registry.self");
        registry_bind_id(registry, g_builtin_syms.self, self_value);
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
    } else if (ctx->session.language_id == CETTA_LANGUAGE_HE &&
               (ctx->module_token_scope || !loaded ||
                loaded->provider_kind != CETTA_MODULE_PROVIDER_STDLIB_FILE)) {
        ok = load_he_document(ctx, path, work_space, eval_arena,
                              persistent_arena, registry, source, last_results, error_out);
    } else {
        int n = ctx->session.language_id == CETTA_LANGUAGE_PRIME
            ? prime_semantics_read_source_file_ids(NULL, path,
                work_space ? work_space->native.universe : NULL, &atom_ids, NULL, 0u)
            : parse_metta_file_ids(path,
                work_space ? work_space->native.universe : NULL, &atom_ids);
        if (n < 0) {
            ok = false;
            *error_out = module_reason(ctx, eval_arena, "ModuleParseFailed", path);
        } else {
            CettaPettaDocumentFailure failure =
                CETTA_PETTA_DOCUMENT_OK;
            Atom *detail = NULL;
            ok = cetta_library_petta_execute_document_ids(
                ctx, work_space, eval_arena,
                persistent_arena, registry,
                atom_ids, n, NULL, last_results, NULL, NULL, &failure, &detail);
            if (!ok) {
                if (failure == CETTA_PETTA_DOCUMENT_EVAL_FAILED &&
                    detail) {
                    *error_out = detail;
                } else {
                    const char *tag =
                        failure == CETTA_PETTA_DOCUMENT_COPY_FAILED
                            ? "ModuleParseFailed"
                            : failure == CETTA_PETTA_DOCUMENT_EVAL_FAILED
                                ? "ModuleInitError"
                                : "ModuleCompilePlanFailed";
                    *error_out =
                        module_reason(ctx, eval_arena, tag, path);
                }
            }
        }
    }

    if (registry) {
        if (prev_self) {
            cetta_provenance_assert_not_transient(
                prev_self, "library.registry.restore-self");
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
                                           int fuel, ResultSet *last_results,
                                           Atom **error_out) {
    uint32_t imported_len_before = ctx->imported_file_len;
    Space *work_space = space_heap_clone_shallow(target);
    if (!work_space) {
        *error_out = atom_symbol(eval_arena, "module transaction allocation failed");
        return false;
    }
    if (work_space && ctx->petta_program &&
        !petta_program_clone_space(
            ctx->petta_program, target, work_space)) {
        space_free(work_space);
        free(work_space);
        *error_out = atom_symbol(
            eval_arena, "PeTTaProgramCloneFailed");
        return false;
    }
    if (!cetta_library_push_import_alias(ctx, work_space, target)) {
        if (ctx->petta_program)
            petta_program_forget_space(
                ctx->petta_program, work_space);
        space_free(work_space);
        free(work_space);
        *error_out = atom_symbol(eval_arena, "module transaction allocation failed");
        return false;
    }

    ctx->include_fault_keeps_prefix = false;
    bool ok = load_module_file(ctx, path, target, work_space, eval_arena,
                               persistent_arena, registry, fuel, NULL, last_results, error_out);
    cetta_library_pop_import_alias(ctx);
    bool keeps_prefix = !ok && ctx->include_fault_keeps_prefix;
    ctx->include_fault_keeps_prefix = false;

    if (!ok && !keeps_prefix) {
        rollback_imported_files(ctx, imported_len_before);
        if (ctx->petta_program)
            petta_program_forget_space(
                ctx->petta_program, work_space);
        space_free(work_space);
        free(work_space);
        return false;
    }

    if (ctx->petta_program &&
        !petta_program_replace_space(
            ctx->petta_program, target, work_space)) {
        rollback_imported_files(ctx, imported_len_before);
        petta_program_forget_space(
            ctx->petta_program, work_space);
        space_free(work_space);
        free(work_space);
        *error_out = atom_symbol(
            eval_arena, "PeTTaProgramCommitFailed");
        return false;
    }
    space_replace_contents(target, work_space);
    if (ctx->petta_program)
        petta_program_forget_space(
            ctx->petta_program, work_space);
    space_free(work_space);
    free(work_space);
    return ok;
}

static bool execute_import_plan(CettaLibraryContext *ctx, const CettaImportPlan *plan,
                                Arena *eval_arena,
                                Arena *persistent_arena, Registry *registry,
                                int fuel, ResultSet *last_results, Atom **error_out) {
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
                                                 registry, fuel, last_results, error_out);
        if (!ok) {
            rollback_loaded_modules(ctx, loaded_len_before);
        }
        return ok;
    }
    if (!load_module_file(ctx, plan->canonical_path, plan->logical_target_space,
                          plan->execution_target_space, eval_arena,
                          persistent_arena, registry, fuel, NULL, last_results, error_out)) {
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
        if (strcmp(ctx->loaded_modules[i]->canonical_path, canonical_path) == 0) {
            return ctx->loaded_modules[i];
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
    if (!library_reserve_loaded_module(ctx)) {
        *error_out = atom_symbol(eval_arena, "module inventory allocation failed");
        return NULL;
    }
    entry = calloc(1u, sizeof(*entry));
    if (!entry) {
        *error_out = atom_symbol(eval_arena, "module record allocation failed");
        return NULL;
    }
    ctx->loaded_modules[ctx->loaded_module_len++] = entry;
    snprintf(entry->display_name, sizeof(entry->display_name), "%s", plan->spec.raw_spec);
    snprintf(entry->canonical_path, sizeof(entry->canonical_path), "%s", plan->canonical_path);
    entry->provider_kind = plan->provider_kind;
    entry->format = plan->format;
    entry->space = space;
    entry->loading = false;
    return entry;
}

/* HE gives a MeTTa module its own space and tokens (HE's MettaMod). */
static bool library_loads_he_module(const CettaLibraryContext *ctx,
                                    const CettaImportPlan *plan) {
    return ctx->session.language_id == CETTA_LANGUAGE_HE &&
           plan->format.kind == CETTA_MODULE_FORMAT_METTA;
}

static bool library_loads_he_provider(const CettaLibraryContext *ctx,
                                      const CettaImportPlan *plan) {
    return ctx->session.language_id == CETTA_LANGUAGE_HE &&
           plan->format.kind == CETTA_MODULE_FORMAT_FOREIGN &&
           plan->format.foreign_backend == CETTA_FOREIGN_BACKEND_PYTHON;
}

/* Load an HE module into its own space.  The space reads the library module
 * as the space importing it does; its text is read with its own tokens, so
 * `&self` in it denotes its space wherever its code later runs, and the
 * tokens its code binds stay its own. */
static bool prepare_he_module_entry(CettaLibraryContext *ctx,
                                    CettaLoadedModule *entry, Arena *eval_arena,
                                    Arena *persistent_arena, Registry *registry,
                                    Atom **error_out) {
    entry->export_words = library_he_words(ctx, entry->provider_kind,
                                           entry->display_name) != NULL;
    if (!library_reserve_module_retirement(ctx)) {
        *error_out = atom_symbol(eval_arena, "module rollback allocation failed");
        return false;
    }
    Arena *storage = persistent_arena ? persistent_arena : eval_arena;
    Atom *importer_self = registry
        ? registry_lookup_id(registry, g_builtin_syms.self) : NULL;
    Space *importer = importer_self &&
            importer_self->kind == ATOM_GROUNDED &&
            importer_self->ground.gkind == GV_SPACE
        ? (Space *)importer_self->ground.ptr : NULL;
    CettaIndex library_len = importer ? space_imported_length(importer) : 0u;
    bool same_universe = importer &&
        importer->native.universe == entry->space->native.universe;
    for (CettaIndex i = 0u; i < library_len; i++) {
        AtomId id = same_universe
            ? space_get_atom_id_at64(importer, i) : CETTA_ATOM_ID_NONE;
        if (id != CETTA_ATOM_ID_NONE)
            space_add_atom_id(entry->space, id);
        else
            space_add(entry->space, space_get_at64(importer, i));
    }
    if (library_len != 0u && !space_mark_imported_prefix(entry->space)) {
        *error_out = atom_symbol(eval_arena, "module library setup failed");
        return false;
    }
    /* Its name extends that of the module importing it first; `self:`
       says so explicitly and `top:` names it from the program. */
    const char *parent = cetta_library_module_space_name(ctx, importer);
    const char *leaf = entry->display_name;
    if (!parent)
        parent = "top";
    if (strncmp(leaf, "self:", 5u) == 0) {
        leaf += 5u;
    } else if (strncmp(leaf, "top:", 4u) == 0) {
        parent = "top";
        leaf += 4u;
    }
    size_t name_size = strlen(parent) + 1u + strlen(leaf) + 1u;
    entry->module_name = malloc(name_size);
    if (!entry->module_name) {
        *error_out = atom_symbol(eval_arena, "module name allocation failed");
        return false;
    }
    snprintf(entry->module_name, name_size, "%s:%s", parent, leaf);
    entry->tokens = malloc(sizeof(*entry->tokens));
    if (!entry->tokens) {
        *error_out = atom_symbol(eval_arena, "module token table allocation failed");
        return false;
    }
    registry_init(entry->tokens);
    Atom *module_self = atom_space(storage, entry->space);
    cetta_provenance_assert_not_transient(module_self,
                                         "library.module.self");
    registry_bind_id(entry->tokens, g_builtin_syms.self, module_self);
    return true;
}

static bool load_he_module(CettaLibraryContext *ctx, CettaLoadedModule *entry,
                           const char *path, Arena *eval_arena,
                           Arena *persistent_arena, Registry *registry,
                           int fuel, const CettaModuleSource *source,
                           Atom **error_out) {
    if (!prepare_he_module_entry(ctx, entry, eval_arena, persistent_arena,
                                 registry, error_out))
        return false;
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
        return true;
    Registry *outer_scope = ctx->module_token_scope;
    ctx->module_token_scope = entry->tokens;
    bool ok = load_module_file(ctx, path, entry->space, entry->space,
                               eval_arena, persistent_arena, entry->tokens,
                               fuel, source, NULL, error_out);
    ctx->module_token_scope = outer_scope;
    return ok;
}

/* Load a Python module that provides an HE module. Finding the module
 * executes its file, and loading by name executes it again. An explicit
 * registered path has no preceding name search. Its register functions run
 * once for the module's own tokens. `source` holds the bytes
 * captured for this attempt, or NULL where Python's import machinery reads
 * the file as upstream does. */
static bool load_he_provider(CettaLibraryContext *ctx, CettaLoadedModule *entry,
                             const char *path, Arena *eval_arena,
                             Arena *persistent_arena, Registry *registry,
                             const CettaModuleSource *source, bool name_search,
                             Atom **error_out) {
    if (!prepare_he_module_entry(ctx, entry, eval_arena, persistent_arena,
                                 registry, error_out))
        return false;
    const uint8_t *bytes = source ? source->bytes : NULL;
    size_t length = source ? source->length : 0u;
    CettaForeignHEExport *exports = NULL;
    size_t count = 0u;
    bool ok = (!name_search || cetta_foreign_he_module_execute(
            ctx->foreign_runtime, path, bytes, length, eval_arena, NULL, error_out)) &&
        cetta_foreign_he_module_execute(ctx->foreign_runtime, path, bytes,
            length, eval_arena, &entry->provider, error_out) &&
        cetta_foreign_he_module_exports(entry->provider,
            persistent_arena ? persistent_arena : eval_arena, &exports, &count,
            error_out);
    if (!ok) {
        *error_out = module_reason_with_detail(ctx, eval_arena,
            "ModuleForeignLoadFailed", path, *error_out);
        return false;
    }
    for (size_t i = 0u; i < count; i++)
        registry_bind(entry->tokens, exports[i].name, exports[i].atom);
    cetta_foreign_he_exports_free(exports, count);
    return true;
}

/* Link a loaded HE module, then the modules it imports, as dependencies of
 * `importer`: HE flattens an import's dependencies into the importer, each
 * module once, in the order of first import. */
static bool library_link_he_module(Space *importer, Space *module,
                                   Arena *eval_arena, Atom **error_out) {
    if (importer == module)
        return true;
    /* HE's contains_imported_dep check precedes transitive flattening: a
     * repeated import does not add a module's later dependencies. */
    uint32_t imported = space_dependency_count(importer);
    for (uint32_t d = 0u; d < imported; d++) {
        if (space_dependency_at(importer, d) == module)
            return true;
    }
    bool ok = space_add_dependency(importer, module);
    uint32_t dependencies = space_dependency_count(module);
    for (uint32_t d = 0u; ok && d < dependencies; d++) {
        Space *dependency = space_dependency_at(module, d);
        if (dependency != importer)
            ok = space_add_dependency(importer, dependency);
    }
    if (!ok)
        *error_out = atom_symbol(eval_arena, "module dependency link failed");
    return ok;
}

/* Give `importer` the names of a token-providing module, then link it. The
 * register functions run once for this importer; their names are published
 * together or not at all, and a failed request leaves no link. A request made
 * from inside the running register functions is already being served. The
 * compatibility profile treats a later request as a new attempt; the other
 * profiles remember the refusal for this importer. */
static bool library_name_he_provider(CettaLibraryContext *ctx, Space *importer,
                                     Registry *registry, CettaLoadedModule *entry,
                                     Arena *eval_arena, Arena *persistent_arena,
                                     Atom **error_out) {
    uint64_t instance = space_instance_id(logical_import_space(ctx, importer));
    CettaHEProviderExport **link = &entry->exports;
    while (*link && (*link)->importer_instance != instance)
        link = &(*link)->next;
    CettaHEProviderExport *record = *link;
    if (record && record->naming)
        return true;
    if (record && record->named)
        return library_link_he_module(importer, entry->space, eval_arena, error_out);
    if (record && record->refusal) {
        *error_out = atom_deep_copy(eval_arena, record->refusal);
        return false;
    }
    if (!record) {
        record = calloc(1u, sizeof(*record));
        if (!record) {
            *error_out = atom_symbol(eval_arena, "module export record allocation failed");
            return false;
        }
        record->importer_instance = instance;
        arena_init_detached(&record->storage);
        arena_set_block_capacity(&record->storage, 1024u);
        *link = record;
    }
    record->naming = true;
    CettaForeignHEExport *exports = NULL;
    size_t count = 0u;
    bool ok = !entry->provider || cetta_foreign_he_module_exports(entry->provider,
        persistent_arena ? persistent_arena : eval_arena, &exports, &count, error_out);
    /* The list may have changed while the register functions ran. */
    link = &entry->exports;
    while (*link && *link != record)
        link = &(*link)->next;
    record->naming = false;
    if (ok && !library_link_he_module(importer, entry->space, eval_arena, error_out)) {
        cetta_foreign_he_exports_free(exports, count);
        ok = false;
    } else if (!ok) {
        *error_out = module_reason_with_detail(ctx, eval_arena,
            "ModuleForeignLoadFailed", entry->canonical_path, *error_out);
    }
    if (!ok) {
        if (cetta_language_uses_rust_he_compat_semantics(
                ctx->session.language_id, ctx->session.profile)) {
            if (*link)
                *link = record->next;
            arena_free(&record->storage);
            free(record);
        } else {
            record->refusal = atom_deep_copy(&record->storage, *error_out);
        }
        return false;
    }
    for (size_t i = 0u; registry && i < count; i++)
        registry_bind(registry, exports[i].name, exports[i].atom);
    cetta_foreign_he_exports_free(exports, count);
    /* A declared vocabulary interface exports its whole-word aliases;
       ordinary MeTTa modules still keep their bind! names private. */
    if (registry && entry->export_words && entry->tokens) {
        for (uint32_t i = 0u; i < entry->tokens->len; i++) {
            RegistryEntry *word = &entry->tokens->entries[i];
            if (word->key != SYMBOL_ID_NONE &&
                symbol_bytes(g_symbols, word->key)[0] != '&')
                registry_bind_id(registry, word->key, word->value);
        }
    }
    record->named = true;
    return true;
}

bool cetta_library_owns_space(const CettaLibraryContext *ctx,
                              const Space *space) {
    if (!ctx || !space)
        return false;
    for (uint32_t i = 0u; i < ctx->module_own_view_len; i++) {
        if (ctx->module_own_views[i].view == space)
            return true;
    }
    for (uint32_t i = 0u; i < ctx->loaded_module_len; i++) {
        if (ctx->loaded_modules[i]->space == space)
            return true;
    }
    for (uint32_t i = 0u; i < ctx->retired_module_len; i++) {
        if (ctx->retired_modules[i].space == space)
            return true;
    }
    return false;
}

void cetta_library_set_top_module_space(CettaLibraryContext *ctx,
                                        const Space *space) {
    if (ctx)
        ctx->top_module_space = space;
}

void cetta_library_set_top_module_registry(CettaLibraryContext *ctx,
                                           Registry *registry) {
    if (ctx)
        ctx->top_module_registry = registry;
}

bool cetta_library_module_context(CettaLibraryContext *ctx, uint64_t instance,
                                  Space **space_out, Registry **registry_out) {
    if (!space_out || !registry_out)
        return false;
    *space_out = NULL;
    *registry_out = NULL;
    if (!ctx || !instance || !ctx->top_module_space)
        return false;
    if (ctx->top_module_registry && ctx->top_module_space &&
        space_instance_id(ctx->top_module_space) == instance) {
        *space_out = (Space *)ctx->top_module_space;
        *registry_out = ctx->top_module_registry;
        return true;
    }
    for (uint32_t i = 0u; i < ctx->loaded_module_len; i++) {
        CettaLoadedModule *module = ctx->loaded_modules[i];
        if (module->tokens && module->space && space_instance_id(module->space) == instance) {
            *space_out = module->space;
            *registry_out = module->tokens;
            return true;
        }
    }
    for (uint32_t i = 0u; i < ctx->retired_module_len; i++) {
        if (ctx->retired_modules[i].tokens &&
            space_instance_id(ctx->retired_modules[i].space) == instance) {
            *space_out = ctx->retired_modules[i].space;
            *registry_out = ctx->retired_modules[i].tokens;
            return true;
        }
    }
    return false;
}

bool cetta_library_he_run_text(CettaLibraryContext *ctx, Space *space,
    Registry *registry, const char *text, Arena *arena, Arena *persistent,
    CettaDocumentResultObserver observer, void *observer_context, Atom **error_out) {
    if (error_out)
        *error_out = NULL;
    if (!ctx || !space || !text || !arena)
        return false;
    Space *work = cetta_library_import_execution_space(ctx, space);
    if (!work || !work->native.universe)
        return false;
    char resource_dir[PATH_MAX] = {0};
    if (space == ctx->top_module_space) {
        snprintf(resource_dir, sizeof(resource_dir), "%s",
            ctx->script_dir[0] ? ctx->script_dir : ctx->working_dir);
    } else {
        for (uint32_t i = 0u; i < ctx->loaded_module_len; i++) {
            if (ctx->loaded_modules[i]->space == space) {
                copy_parent_dir(resource_dir, sizeof(resource_dir),
                                 ctx->loaded_modules[i]->canonical_path);
                break;
            }
        }
        for (uint32_t i = 0u; !resource_dir[0] && i < ctx->retired_module_len; i++) {
            if (ctx->retired_modules[i].space == space)
                snprintf(resource_dir, sizeof(resource_dir), "%s",
                    ctx->retired_modules[i].resource_dir);
        }
    }
    if (!cetta_library_push_dir(ctx, resource_dir[0] ? resource_dir : ".")) {
        if (error_out)
            *error_out = atom_symbol(arena, "runner resource scope allocation failed");
        return false;
    }
    AtomId *ids = NULL;
    int count = 0;
    char diagnostic[512] = {0};
    int parsed = parse_metta_bytes_prefix_ids(
        (const uint8_t *)text, strlen(text), work->native.universe,
        &ids, &count, diagnostic, sizeof(diagnostic));
    if (count > 0 && tu_kind(work->native.universe, ids[count - 1]) == ATOM_SYMBOL &&
        tu_sym(work->native.universe, ids[count - 1]) == g_builtin_syms.bang)
        count--;
    Registry *outer_scope = ctx->module_token_scope;
    ctx->module_token_scope = registry;
    CettaPettaDocumentFailure failure = CETTA_PETTA_DOCUMENT_OK;
    bool ok = cetta_library_petta_execute_document_ids(
        ctx, work, arena, persistent, registry, ids, count,
        NULL, NULL, observer, observer_context, &failure, error_out);
    ctx->module_token_scope = outer_scope;
    cetta_library_pop_dir(ctx);
    free(ids);
    /* A run that returned at an Error value never read the later text. */
    if (ok && parsed < 0 && failure != CETTA_PETTA_DOCUMENT_STOPPED_AT_ERROR) {
        if (error_out)
            *error_out = atom_symbol(arena,
                diagnostic[0] ? diagnostic : "MeTTa source parse failed");
        return false;
    }
    return ok;
}

const char *cetta_library_module_space_name(const CettaLibraryContext *ctx,
                                            const void *space) {
    if (!ctx || !space)
        return NULL;
    if (space == ctx->top_module_space)
        return "top";
    for (uint32_t i = 0u; i < ctx->loaded_module_len; i++) {
        if (ctx->loaded_modules[i]->space == space)
            return ctx->loaded_modules[i]->module_name;
    }
    for (uint32_t i = 0u; i < ctx->retired_module_len; i++) {
        if (ctx->retired_modules[i].space == space)
            return ctx->retired_modules[i].module_name;
    }
    return NULL;
}

Space *cetta_library_module_space_no_deps(
        CettaLibraryContext *ctx, Space *space) {
    if (!ctx || !space || space->module_own_view)
        return space;
    space = logical_import_space(ctx, space);
    if (space != ctx->top_module_space &&
        !cetta_library_owns_space(ctx, space))
        return space;
    for (uint32_t i = 0u; i < ctx->module_own_view_len; i++) {
        if (ctx->module_own_views[i].source == space &&
            ctx->module_own_views[i].source_instance_id ==
                space_instance_id(space))
            return ctx->module_own_views[i].view;
    }
    if (ctx->module_own_view_len == ctx->module_own_view_cap) {
        uint32_t next = ctx->module_own_view_cap
            ? ctx->module_own_view_cap * 2u : 4u;
        if (next <= ctx->module_own_view_cap ||
            (size_t)next > SIZE_MAX / sizeof(*ctx->module_own_views))
            return NULL;
        void *views = realloc(ctx->module_own_views,
                               (size_t)next * sizeof(*ctx->module_own_views));
        if (!views)
            return NULL;
        ctx->module_own_views = views;
        ctx->module_own_view_cap = next;
    }
    Space *view = malloc(sizeof(*view));
    if (!view)
        return NULL;
    if (!space_init_module_own_view(view, space)) {
        free(view);
        return NULL;
    }
    /* A view first created in a nested include must retain every enclosing
       storage observation. Unwinding then restores it without allocation. */
    for (uint32_t i = 0u; i < ctx->import_space_alias_len; i++) {
        if (logical_import_space(ctx, ctx->import_space_aliases[i].work_space) == space &&
            !space_module_own_view_select(view, ctx->import_space_aliases[i].work_space)) {
            space_free(view);
            free(view);
            return NULL;
        }
    }
    uint32_t index = ctx->module_own_view_len++;
    ctx->module_own_views[index].source = space;
    ctx->module_own_views[index].source_instance_id = space_instance_id(space);
    ctx->module_own_views[index].view = view;
    return view;
}

static CettaHEModuleFailure **library_he_failure_list(CettaLibraryContext *ctx) {
    return ctx->he_module_run_scope
        ? &ctx->he_module_run_scope->failures : &ctx->he_module_failures;
}

static void library_he_forget_failure(CettaLibraryContext *ctx,
                                      CettaHEModuleFailure *failure) {
    CettaHEModuleFailure **link = library_he_failure_list(ctx);
    while (*link && *link != failure)
        link = &(*link)->next;
    if (*link) {
        *link = failure->next;
        library_he_failure_free(failure);
    }
}

static bool library_he_begin_initializer(CettaLibraryContext *ctx,
    const CettaImportPlan *plan, Registry *registry, Arena *arena,
    CettaHEModuleFailure **attempt_out, Atom **error_out) {
    *attempt_out = NULL;
    bool compat = cetta_language_uses_rust_he_compat_semantics(
        ctx->session.language_id, ctx->session.profile);
    Atom *self = registry ? registry_lookup_id(registry, g_builtin_syms.self) : NULL;
    Space *parent = self && self->kind == ATOM_GROUNDED && self->ground.gkind == GV_SPACE
        ? logical_import_space(ctx, self->ground.ptr) : (Space *)ctx->top_module_space;
    uint64_t parent_instance = space_instance_id(parent);
    CettaHEModuleFailure **head = library_he_failure_list(ctx);
    CettaModuleSource source = {0};
    struct stat st;
    bool directory = stat(plan->canonical_path, &st) == 0 && S_ISDIR(st.st_mode);
    bool captured = false;
    for (CettaHEModuleFailure *failure = *head; failure; failure = failure->next) {
        if (failure->provider != plan->provider_kind ||
            failure->format.kind != plan->format.kind ||
            failure->format.foreign_backend != plan->format.foreign_backend ||
            strcmp(failure->path, plan->canonical_path) != 0 ||
            (compat &&
             (failure->importer_instance != parent_instance ||
              strcmp(failure->name, plan->spec.raw_spec) != 0)))
            continue;
        if (failure->completed)
            continue;
        if (failure->initializing) {
            *error_out = module_reason(ctx, arena, "ModuleImportCycle", plan->canonical_path);
            return false;
        }
        if (!compat) {
            captured = !directory && read_he_module_source(ctx, plan, &source);
            /* Missing bytes do not establish a changed version. Metadata
             * and elapsed time cannot authorize replaying completed effects. */
            if (captured && (!failure->source.bytes ||
                source.length != failure->source.length ||
                memcmp(source.bytes, failure->source.bytes, source.length) != 0)) {
                library_he_forget_failure(ctx, failure);
                break;
            }
        }
        free(source.bytes);
        *error_out = atom_deep_copy(arena, failure->reason);
        if (!*error_out)
            *error_out = atom_symbol(arena, "module failure copy failed");
        return false;
    }
    if (!captured && !directory)
        captured = read_he_module_source(ctx, plan, &source);
    if (!captured && library_he_words(ctx, plan->provider_kind,
                                      plan->spec.raw_spec)) {
        *error_out = atom_symbol(arena, "library vocabulary read failed");
        return false;
    }
    size_t name_size = strlen(plan->spec.raw_spec) + 1u;
    if (name_size > SIZE_MAX - sizeof(CettaHEModuleFailure)) {
        free(source.bytes);
        *error_out = atom_symbol(arena, "module refusal record too large");
        return false;
    }
    CettaHEModuleFailure *failure = calloc(1u, sizeof(*failure) + name_size);
    if (!failure) {
        free(source.bytes);
        *error_out = atom_symbol(arena, "module refusal allocation failed");
        return false;
    }
    failure->importer_instance = parent_instance;
    failure->provider = plan->provider_kind;
    failure->format = plan->format;
    failure->initializing = true;
    failure->source = source;
    snprintf(failure->path, sizeof(failure->path), "%s", plan->canonical_path);
    memcpy(failure->name, plan->spec.raw_spec, name_size);
    arena_init_detached(&failure->storage);
    arena_set_block_capacity(&failure->storage, 1024u);
    failure->reason = atom_symbol(&failure->storage, "module initialization failed");
    if (compat) {
        for (CettaHEModuleFailure *pending = *head; pending; pending = pending->next) {
            if (pending->initializing) {
                failure->initialization_parent = pending;
                break;
            }
        }
    }
    failure->next = *head;
    *head = failure;
    *attempt_out = failure;
    if (!captured && !directory) {
        failure->initializing = false;
        *error_out = module_reason(ctx, arena, "ModuleParseFailed", plan->canonical_path);
        Atom *owned = atom_deep_copy(&failure->storage, *error_out);
        if (owned)
            failure->reason = owned;
        return false;
    }
    return true;
}

static void library_he_finish_initializer(CettaLibraryContext *ctx,
    CettaHEModuleFailure *failure, Atom *reason) {
    if (!failure)
        return;
    bool compat = cetta_language_uses_rust_he_compat_semantics(
        ctx->session.language_id, ctx->session.profile);
    if (compat) {
        CettaHEModuleFailure **link = library_he_failure_list(ctx);
        while (*link) {
            CettaHEModuleFailure *child = *link;
            if (child->initialization_parent != failure) {
                link = &child->next;
                continue;
            }
            child->initialization_parent = failure->initialization_parent;
            /* A finished child belongs to its parent's attempt. When the
               parent fails the child is unloaded with it, and when the
               outermost attempt ends nothing is left to track. */
            if (child->completed && (reason || !failure->initialization_parent)) {
                *link = child->next;
                library_he_failure_free(child);
                continue;
            }
            link = &child->next;
        }
        if (!reason && failure->initialization_parent) {
            failure->initializing = false;
            failure->completed = true;
            return;
        }
        /* Upstream forgets a failed attempt: the next import is a new one. */
        if (reason) {
            library_he_forget_failure(ctx, failure);
            return;
        }
    }
    if (!reason) {
        library_he_forget_failure(ctx, failure);
        return;
    }
    Atom *owned = atom_deep_copy(&failure->storage, reason);
    if (owned)
        failure->reason = owned;
    failure->initializing = false;
}

/* Whether a module is loaded under this `top:` name. */
static bool he_top_name_known(const CettaLibraryContext *ctx, const char *name) {
    for (uint32_t i = 0u; i < ctx->loaded_module_len; i++) {
        const CettaLoadedModule *module = ctx->loaded_modules[i];
        if (module->module_name && (module->space || module->loading) &&
            strcmp(module->module_name, name) == 0)
            return true;
    }
    for (uint32_t i = 0u; i < ctx->he_module_alias_len; i++) {
        if (strcmp(ctx->he_module_aliases[i], name) == 0)
            return true;
    }
    return false;
}

/* A `top:` name is remembered, so that it counts as loaded afterwards. A
 * module that is still loading may give a loaded module a further name only
 * among its own sub-modules. */
static bool admit_he_top_name(CettaLibraryContext *ctx, const char *raw,
                              Registry *registry, bool renames_loaded,
                              Arena *eval_arena, Atom **error_out) {
    if (he_top_name_known(ctx, raw))
        return true;
    Atom *self = registry && renames_loaded
        ? registry_lookup_id(registry, g_builtin_syms.self) : NULL;
    Space *importer = self && self->kind == ATOM_GROUNDED &&
            self->ground.gkind == GV_SPACE
        ? (Space *)self->ground.ptr : NULL;
    for (uint32_t i = 0u; importer && i < ctx->loaded_module_len; i++) {
        const CettaLoadedModule *loader = ctx->loaded_modules[i];
        if (loader->space != importer || !loader->loading ||
            !loader->module_name)
            continue;
        size_t loader_len = strlen(loader->module_name);
        if (strncmp(raw, loader->module_name, loader_len) == 0 &&
            raw[loader_len] == ':')
            break;
        const char *format = "Cannot load module %s from loader of %s.  "
            "Module loaders may only load sub-modules";
        size_t size = strlen(format) + strlen(raw) + loader_len + 1u;
        char *message = arena_alloc(eval_arena, size);
        snprintf(message, size, format, raw, loader->module_name);
        *error_out = atom_symbol(eval_arena, message);
        return false;
    }
    if (ctx->he_module_alias_len == ctx->he_module_alias_cap) {
        uint32_t next = ctx->he_module_alias_cap
            ? ctx->he_module_alias_cap * 2u : 8u;
        char **names = next > ctx->he_module_alias_cap
            ? realloc(ctx->he_module_aliases, (size_t)next * sizeof(*names))
            : NULL;
        if (!names) {
            *error_out = atom_symbol(eval_arena, "module name allocation failed");
            return false;
        }
        ctx->he_module_aliases = names;
        ctx->he_module_alias_cap = next;
    }
    char *copy = strdup(raw);
    if (!copy) {
        *error_out = atom_symbol(eval_arena, "module name allocation failed");
        return false;
    }
    ctx->he_module_aliases[ctx->he_module_alias_len++] = copy;
    return true;
}

/* Before a module named through HE's module tree is loaded, its parent is:
 * every component of the name is a module. */
static bool prepare_he_tree_module(CettaLibraryContext *ctx,
                                   const CettaImportPlan *plan,
                                   Arena *eval_arena, Arena *persistent_arena,
                                   Registry *registry, int fuel,
                                   Atom **error_out) {
    char parent[PATH_MAX];
    snprintf(parent, sizeof(parent), "%s", plan->spec.raw_spec);
    char *last = strrchr(parent, ':');
    if (!last)
        return true;
    *last = '\0';
    if (strcmp(parent, "self") == 0 || strcmp(parent, "top") == 0)
        return true;
    Space *parent_space = NULL;
    return cetta_library_import_module(ctx, parent, NULL, true, eval_arena,
                                       persistent_arena, registry, fuel,
                                       &parent_space, error_out);
}

static CettaLoadedModule *ensure_loaded_module(CettaLibraryContext *ctx,
                                               const CettaImportPlan *plan,
                                               Arena *eval_arena,
                                               Arena *persistent_arena,
                                               Registry *registry,
                                               int fuel, Atom **error_out) {
    if (ctx->session.language_id == CETTA_LANGUAGE_HE &&
        plan->spec.kind == CETTA_MODULE_SPEC_REGISTERED_ROOT &&
        (plan->provider_kind == CETTA_MODULE_PROVIDER_RELATIVE_FILE ||
         strchr(plan->spec.path_or_member, '/'))) {
        const CettaLoadedModule *known =
            loaded_module_lookup(ctx, plan->canonical_path);
        if (strncmp(plan->spec.raw_spec, "top:", 4u) == 0 &&
            !admit_he_top_name(ctx, plan->spec.raw_spec, registry,
                               known && known->space, eval_arena, error_out))
            return NULL;
        if (!(known && (known->space || known->loading)) &&
            !prepare_he_tree_module(ctx, plan, eval_arena, persistent_arena,
                                    registry, fuel, error_out))
            return NULL;
    }
    uint32_t loaded_len_before = ctx->loaded_module_len;
    uint32_t imported_len_before = ctx->imported_file_len;
    CettaLoadedModule *entry = remember_loaded_module(ctx, plan, NULL, eval_arena, error_out);
    if (!entry) {
        return NULL;
    }
    if (entry->loading) {
        *error_out = module_reason(ctx, eval_arena, "ModuleImportCycle",
                                   plan->canonical_path);
        return NULL;
    }
    if (entry->space) {
        return entry;
    }
    CettaHEModuleFailure *attempt = NULL;
    bool he_provider = library_loads_he_provider(ctx, plan);
    bool he_initializer = he_provider || library_loads_he_module(ctx, plan);
    bool compat = cetta_language_uses_rust_he_compat_semantics(
        ctx->session.language_id, ctx->session.profile);
    if (he_initializer && !library_he_begin_initializer(
            ctx, plan, registry, eval_arena, &attempt, error_out)) {
        rollback_one_loaded_module(ctx, entry);
        return NULL;
    }
    entry->space = malloc(sizeof(Space));
    if (!entry->space) {
        *error_out = atom_symbol(eval_arena, "module space allocation failed");
        library_he_finish_initializer(ctx, attempt, *error_out);
        rollback_one_loaded_module(ctx, entry);
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
    } else if (he_provider) {
        /* Upstream reads a Python file through Python's import machinery;
         * the other profiles execute the bytes captured for this attempt. */
        ok = load_he_provider(ctx, entry, plan->canonical_path, eval_arena,
                              persistent_arena, registry,
                              !compat && attempt && attempt->source.bytes
                                  ? &attempt->source : NULL,
                              plan->spec.kind != CETTA_MODULE_SPEC_REGISTERED_ROOT ||
                                  plan->spec.path_or_member[0] != '\0',
                              error_out);
    } else if (library_loads_he_module(ctx, plan)) {
        ok = load_he_module(ctx, entry, plan->canonical_path, eval_arena,
                            persistent_arena, registry, fuel,
                            attempt && attempt->source.bytes ? &attempt->source : NULL,
                            error_out);
    } else {
        ok = load_module_file(ctx, plan->canonical_path, entry->space, entry->space,
                               eval_arena, persistent_arena, registry, fuel,
                               NULL, NULL, error_out);
    }
    entry->loading = false;
    library_he_finish_initializer(ctx, attempt, ok ? NULL : *error_out);
    if (!ok) {
        if (he_initializer && !compat) {
            rollback_one_loaded_module(ctx, entry);
        } else {
            rollback_imported_files(ctx, imported_len_before);
            rollback_loaded_modules(ctx, loaded_len_before);
        }
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
    Arena *dst = persistent_arena ? persistent_arena : eval_arena;
    if (ctx && spec && ctx->session.language_id == CETTA_LANGUAGE_HE) {
        if (strcmp(spec, "top") == 0 && ctx->top_module_space)
            return atom_space(dst, (Space *)ctx->top_module_space);
        if (strcmp(spec, "self") == 0) {
            Atom *self = registry
                ? registry_lookup_id(registry, g_builtin_syms.self) : NULL;
            if (self && self->kind == ATOM_GROUNDED &&
                self->ground.gkind == GV_SPACE)
                return atom_space(dst, (Space *)self->ground.ptr);
        }
        for (uint32_t i = 0u; i < ctx->loaded_module_len; i++) {
            CettaLoadedModule *module = ctx->loaded_modules[i];
            if (module->space && !module->loading && module->module_name &&
                strcmp(module->module_name, spec) == 0)
                return atom_space(dst, module->space);
        }
    }
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

    const char *language_name =
        cetta_language_canonical_name(ctx->session.language_id);
    if (!library_inventory_try_add_symbol_fact2(inventory, "module-language",
                                                language_name)) {
        library_inventory_add_fallback_atom(
            inventory, dst,
            atom_expr2(dst, atom_symbol(dst, "module-language"),
                       atom_symbol(dst, language_name)));
    }

    if (ctx->session.profile && ctx->session.profile->name &&
        !library_inventory_try_add_symbol_fact2(inventory, "module-profile",
                                                ctx->session.profile->name)) {
        library_inventory_add_fallback_atom(
            inventory, dst,
            atom_expr2(dst, atom_symbol(dst, "module-profile"),
                       atom_symbol(dst, ctx->session.profile->name)));
    }

    const char *import_mode_name = cetta_relative_module_policy_name(
        cetta_eval_session_relative_module_policy(&ctx->session));
    if (!library_inventory_try_add_symbol_fact2(inventory, "module-import-mode",
                                                import_mode_name)) {
        library_inventory_add_fallback_atom(
            inventory, dst,
            atom_expr2(dst, atom_symbol(dst, "module-import-mode"),
                       atom_symbol(dst, import_mode_name)));
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
        const CettaModuleMount *mount = ctx->module_mounts[i];
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
        const CettaLoadedModule *module = ctx->loaded_modules[i];
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

bool cetta_library_module_source_path(CettaLibraryContext *ctx,
                                      const char *spec, char *out,
                                      size_t out_size) {
    if (!ctx || !spec || !*spec || !out || out_size == 0u) return false;
    Arena scratch;
    arena_init(&scratch);
    Atom *error = NULL;
    CettaModuleSpec parsed;
    CettaImportPlan plan;
    bool ok;
    /* A named library (native or not) is read from the standard library,
     * as cetta_library_import reads it. */
    const CettaLibrarySpec *library = cetta_library_lookup(spec);
    const CettaNativeBuiltinModule *native =
        library ? NULL : cetta_native_module_lookup(spec);
    if (library || native) {
        memset(&parsed, 0, sizeof(parsed));
        parsed.kind = CETTA_MODULE_SPEC_STDLIB;
        const char *name = library ? library->name : native->name;
        snprintf(parsed.raw_spec, sizeof(parsed.raw_spec), "%s", name);
        snprintf(parsed.path_or_member, sizeof(parsed.path_or_member), "%s",
                 name);
        for (char *part = parsed.path_or_member; *part; ++part)
            if (*part == ':') *part = '/';
        ok = true;
    } else {
        ok = parse_module_spec(spec, &parsed, &scratch, &error);
    }
    ok = ok && resolve_import_plan(ctx, &parsed, NULL, NULL, false, &plan,
                                   &scratch, &error) &&
         plan.format.kind == CETTA_MODULE_FORMAT_METTA;
    if (ok) {
        int n = snprintf(out, out_size, "%s", plan.canonical_path);
        ok = n > 0 && (size_t)n < out_size;
    }
    arena_free(&scratch);
    return ok;
}

bool cetta_library_include_module(CettaLibraryContext *ctx, const char *spec,
                                  Space *space, Arena *eval_arena,
                                  Arena *persistent_arena, Registry *registry,
                                  int fuel, ResultSet *last_results, Atom **error_out) {
    space = space_mutation_target(space);
    if (spec && (cetta_library_lookup(spec) || cetta_native_module_lookup(spec))) {
        return cetta_library_import(ctx, spec, space, eval_arena,
                                    persistent_arena, registry, fuel, error_out);
    }
    CettaModuleSpec parsed_spec;
    CettaImportPlan plan;
    if (!parse_module_spec(spec, &parsed_spec, eval_arena, error_out)) {
        return false;
    }
    parsed_spec.reports_written_name = true;
    if (!resolve_import_plan(ctx, &parsed_spec, logical_import_space(ctx, space), space,
                             false, &plan, eval_arena, error_out)) {
        return false;
    }
    /* HE's include takes a module's MeTTa text; a Python module has none. */
    if (ctx->session.language_id == CETTA_LANGUAGE_HE &&
        plan.format.kind == CETTA_MODULE_FORMAT_FOREIGN) {
        *error_out = atom_symbol(eval_arena, "resource not found");
        return false;
    }
    struct stat st;
    if (ctx->session.language_id == CETTA_LANGUAGE_HE &&
        stat(plan.canonical_path, &st) == 0 && S_ISDIR(st.st_mode)) {
        const char *format = "no module.metta file found in %s%s%s dir module";
        bool by_name = parsed_spec.kind == CETTA_MODULE_SPEC_MODULE_NAME;
        const char *base = by_name ? cetta_library_relative_base_dir(ctx) : "";
        const char *leaf = by_name ? parsed_spec.path_or_member
                                   : plan.canonical_path;
        size_t size = strlen(format) + strlen(base) + 1u + strlen(leaf) + 1u;
        char *message = arena_alloc(eval_arena, size);
        snprintf(message, size, format, base, by_name ? "/" : "", leaf);
        *error_out = atom_symbol(eval_arena, message);
        return false;
    }
    return execute_import_plan(ctx, &plan, eval_arena, persistent_arena,
                               registry, fuel, last_results, error_out);
}

/* A loaded module's name in the tree; a module without one hangs below the
 * program under its import spelling. */
static const char *loaded_module_tree_name(const CettaLoadedModule *module,
                                           char *buffer, size_t buffer_sz) {
    if (module->module_name)
        return module->module_name;
    snprintf(buffer, buffer_sz, "top:%s", module->display_name);
    return buffer;
}

/* The longest loaded proper prefix of a tree name, "top" if there is none. */
static size_t loaded_module_tree_parent(CettaLibraryContext *ctx,
                                        uint32_t count, const char *name) {
    size_t best = strlen("top");
    char buffer[PATH_MAX + 8];
    for (uint32_t i = 0u; i < count; i++) {
        const char *other = loaded_module_tree_name(
            cetta_library_loaded_module_at(ctx, i), buffer, sizeof(buffer));
        size_t length = strlen(other);
        if (length > best && strncmp(name, other, length) == 0 &&
            name[length] == ':')
            best = length;
    }
    return best;
}

static void print_loaded_module_children(CettaLibraryContext *ctx, FILE *out,
                                         uint32_t count, const char *parent,
                                         const char *indent) {
    size_t parent_len = strlen(parent);
    char buffer[PATH_MAX + 8];
    uint32_t children = 0u;
    for (int pass = 0; pass < 2; pass++) {
        uint32_t seen = 0u;
        for (uint32_t i = 0u; i < count; i++) {
            const char *name = loaded_module_tree_name(
                cetta_library_loaded_module_at(ctx, i), buffer, sizeof(buffer));
            if (strncmp(name, parent, parent_len) != 0 ||
                name[parent_len] != ':' ||
                loaded_module_tree_parent(ctx, count, name) != parent_len)
                continue;
            seen++;
            if (pass == 0)
                continue;
            bool last = seen == children;
            fprintf(out, " %s%s─%s = %u\n", indent, last ? "└" : "├",
                    name + parent_len + 1u, i + 1u);
            size_t indent_size = strlen(indent) + strlen("│  ") + 1u;
            char *deeper = malloc(indent_size);
            char *own = strdup(name);
            if (deeper && own) {
                snprintf(deeper, indent_size, "%s%s", indent,
                         last ? "   " : "│  ");
                print_loaded_module_children(ctx, out, count, own, deeper);
            }
            free(deeper);
            free(own);
        }
        children = seen;
    }
}

bool cetta_library_print_loaded_modules(CettaLibraryContext *ctx, FILE *out,
                                        Arena *eval_arena, Atom **error_out) {
    (void)eval_arena;
    (void)error_out;
    if (!ctx || !out) return false;
    fprintf(out, "top = 0\n");
    print_loaded_module_children(
        ctx, out, cetta_library_loaded_module_count(ctx), "top", "");
    if (ctx->session.language_id == CETTA_LANGUAGE_HE)
        fputc('\n', out);
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
    CettaLibraryMask import_bit;
    const char *import_name;

    if (!spec && !native_spec) {
        *error_out = atom_symbol(eval_arena, "unknown library");
        return false;
    }
    import_bit = spec ? spec->bit : native_spec->import_bit;
    import_name = spec ? spec->name : native_spec->name;
    /* Native availability belongs to the session; source definitions belong
     * to each importing space. The file inventory prevents replay in that
     * space, including cycles, without skipping another space's import. */
    bool previously_active = (ctx->active_mask & import_bit) != 0u;
    /* HE's native modules have session-wide initialization; its vocabulary
     * libraries alone republish words into each reader. PeTTa source modules
     * use the per-space file inventory below. */
    if (previously_active && ctx->session.language_id != CETTA_LANGUAGE_PETTA &&
        import_bit != CETTA_LIBRARY_RHO)
        return true;

#if CETTA_BUILD_WITH_JSON_GSLT
    if (import_bit == CETTA_LIBRARY_JSON && !ctx->json_runtime) {
        char json_error[512] = {0};
        ctx->json_runtime = cetta_json_library_runtime_v1_new(
            json_error, sizeof(json_error));
        if (!ctx->json_runtime) {
            *error_out = atom_string(
                eval_arena,
                json_error[0] ? json_error
                              : "failed to prepare authored JSON language");
            return false;
        }
    }
#endif

    ctx->active_mask |= import_bit;
    memset(&parsed_spec, 0, sizeof(parsed_spec));
    parsed_spec.kind = CETTA_MODULE_SPEC_STDLIB;
    snprintf(parsed_spec.raw_spec, sizeof(parsed_spec.raw_spec), "%s", import_name);
    snprintf(parsed_spec.path_or_member, sizeof(parsed_spec.path_or_member), "%s", import_name);
    /* A registered native submodule has the same member path as an authored
     * namespace import; its provider remains the standard library. */
    for (char *part = parsed_spec.path_or_member; *part; ++part)
        if (*part == ':') *part = '/';
    if (!resolve_import_plan(ctx, &parsed_spec, logical_import_space(ctx, space),
                             space, false,
                             &plan, eval_arena, error_out) ||
        !execute_import_plan(ctx, &plan, eval_arena, persistent_arena,
                             registry, fuel, NULL, error_out)) {
        if (!previously_active)
            ctx->active_mask &= ~import_bit;
        return false;
    }
    if (import_bit == CETTA_LIBRARY_DURABLE && !ctx->durable_runtime)
        ctx->durable_runtime = cetta_durable_runtime_new();
    if (import_bit == CETTA_LIBRARY_IO && !ctx->io_runtime)
        ctx->io_runtime = cetta_io_runtime_new();
    return true;
}

/* An HE module root is a module from the moment it is registered: its code
 * runs now. */
static bool load_he_module_root(CettaLibraryContext *ctx, const char *name,
                                const char *root_path,
                                CettaModuleProviderKind provider_kind,
                                Arena *eval_arena, Arena *persistent_arena,
                                Registry *registry, int fuel,
                                Atom **error_out) {
    CettaImportPlan plan = {0};
    char reason[160];
    plan.spec.kind = CETTA_MODULE_SPEC_REGISTERED_ROOT;
    snprintf(plan.spec.raw_spec, sizeof(plan.spec.raw_spec), "%s", name);
    plan.provider_kind = provider_kind;
    if (!cetta_foreign_resolve_candidate(
            root_path, plan.canonical_path, sizeof(plan.canonical_path),
            &plan.format, reason, sizeof(reason))) {
        plan.format.kind = CETTA_MODULE_FORMAT_METTA;
        if (!resolve_module_candidate_metta(
                root_path, plan.canonical_path, sizeof(plan.canonical_path),
                reason, sizeof(reason)))
            snprintf(plan.canonical_path, sizeof(plan.canonical_path), "%s",
                     root_path);
    }
    /* Registration initializes a root in the runner's context, even when
       requested by code in another module. Keep its written name unchanged. */
    Registry *root_registry = ctx->top_module_registry
        ? ctx->top_module_registry : registry;
    return ensure_loaded_module(ctx, &plan, eval_arena, persistent_arena,
                                root_registry, fuel, error_out) != NULL;
}

bool cetta_library_register_module(CettaLibraryContext *ctx, const char *path,
                                   Arena *eval_arena, Arena *persistent_arena,
                                   Registry *registry, int fuel,
                                   Atom **error_out) {
    char candidate[PATH_MAX];
    char resolved[PATH_MAX];
    struct stat st;

    if (!cetta_module_policy_allows(&ctx->session.module_policy,
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

    bool he = ctx->session.language_id == CETTA_LANGUAGE_HE;
    if (!realpath(candidate, resolved) || stat(resolved, &st) != 0 ||
        (!S_ISDIR(st.st_mode) &&
         !(he && S_ISREG(st.st_mode) &&
           (path_has_suffix(resolved, ".metta") ||
            path_has_suffix(resolved, ".py"))))) {
        *error_out = atom_symbol(eval_arena, "module root not found");
        return false;
    }

    const char *base = strrchr(resolved, '/');
    base = base ? base + 1 : resolved;
    char name[NAME_MAX + 1];
    size_t name_len = strlen(base);
    if (he && S_ISREG(st.st_mode))
        name_len -= path_has_suffix(resolved, ".py")
            ? strlen(".py") : strlen(".metta");
    if (name_len >= sizeof(name)) {
        *error_out = atom_symbol(eval_arena, "module name too long");
        return false;
    }
    memcpy(name, base, name_len);
    name[name_len] = '\0';
    if (!module_name_is_legal(name)) {
        *error_out = atom_symbol(eval_arena, "illegal module name");
        return false;
    }
    CettaModuleMount *existing = module_mount_lookup_mutable(ctx, name);
    if (he && ctx->session.profile &&
        ctx->session.profile->rust_he_compat_semantics && existing &&
        strcmp(existing->source_locator, resolved) == 0) {
        *error_out = atom_symbol(eval_arena, "module already registered");
        return false;
    }
    /* The root's storage is reserved before its module code runs. */
    if (!existing && !library_reserve_module_mount(ctx)) {
        *error_out = atom_symbol(eval_arena, "module root inventory allocation failed");
        return false;
    }
    if (he && !load_he_module_root(ctx, name, resolved,
                                   CETTA_MODULE_PROVIDER_REGISTERED_ROOT,
                                   eval_arena, persistent_arena, registry,
                                   fuel, error_out))
        return false;
    return commit_module_mount_and_petta_root(
        ctx, name, resolved,
        CETTA_MODULE_PROVIDER_REGISTERED_ROOT,
        CETTA_MODULE_LOCATOR_FILESYSTEM_PATH,
        resolved,
        CETTA_REMOTE_REVISION_NONE,
        "",
        CETTA_PROFILE_MASK_ALL,
        eval_arena, error_out);
}

bool cetta_library_register_git_module(CettaLibraryContext *ctx, const char *url,
                                       Arena *eval_arena,
                                       Arena *persistent_arena,
                                       Registry *registry, int fuel,
                                       Atom **error_out) {
    char module_name[NAME_MAX + 1];
    char cached_root[PATH_MAX];

    if (!cetta_module_policy_allows(&ctx->session.module_policy,
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
    if (ctx->session.language_id == CETTA_LANGUAGE_HE) {
        /* As for register-module!: he-compat refuses the same source a
           second time, and the module's code runs now. */
        const CettaModuleMount *existing =
            module_mount_lookup_any(ctx, module_name);
        if (ctx->session.profile &&
            ctx->session.profile->rust_he_compat_semantics && existing &&
            strcmp(existing->source_locator, url) == 0) {
            *error_out = atom_symbol(eval_arena, "module already registered");
            return false;
        }
        if (!existing && !library_reserve_module_mount(ctx)) {
            *error_out = atom_symbol(eval_arena,
                                     "module root inventory allocation failed");
            return false;
        }
        if (!load_he_module_root(ctx, module_name, cached_root,
                                 CETTA_MODULE_PROVIDER_GIT_REMOTE, eval_arena,
                                 persistent_arena, registry, fuel, error_out))
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

bool cetta_library_petta_git_import(CettaLibraryContext *ctx,
                                    const char *git_path,
                                    const char *build_command,
                                    const char *base_directory,
                                    const char *commit_sha,
                                    Arena *eval_arena,
                                    Atom **error_out) {
    char module_name[NAME_MAX + 1];
    char base_candidate[PATH_MAX];
    char base_resolved[PATH_MAX];
    char local_path[PATH_MAX];
    char temporary_path[PATH_MAX];
    char clone_error[256];
    char build_error[256];
    char normalized_commit[41] = {0};
    struct stat st;
    bool created_repository = false;
    bool pinned_request = commit_sha != NULL;

    if (!ctx || !eval_arena || !error_out)
        return false;
    if (!cetta_module_policy_allows(
            &ctx->session.module_policy,
            CETTA_MODULE_PROVIDER_GIT) ||
        !cetta_module_policy_allows(
            &ctx->session.module_policy,
            CETTA_MODULE_PROVIDER_REGISTERED_ROOTS)) {
        *error_out = atom_symbol(
            eval_arena, "git import provider disabled");
        return false;
    }
    if (pinned_request &&
        !normalize_full_git_commit_sha(
            commit_sha, normalized_commit)) {
        *error_out = atom_symbol(
            eval_arena,
            "git-import! commit SHA must be exactly 40 hexadecimal characters");
        return false;
    }
    if (!git_path || !*git_path ||
        !base_directory || !*base_directory ||
        !git_module_name_from_url(
            git_path, module_name, sizeof(module_name))) {
        *error_out = atom_symbol(
            eval_arena, "invalid git-import! arguments");
        return false;
    }

    int n = base_directory[0] == '/'
        ? snprintf(base_candidate, sizeof(base_candidate),
                   "%s", base_directory)
        : snprintf(base_candidate, sizeof(base_candidate),
                   "%s/%s",
                   ctx->working_dir[0] ? ctx->working_dir : ".",
                   base_directory);
    if (!(n > 0 && (size_t)n < sizeof(base_candidate))) {
        *error_out = atom_symbol(
            eval_arena, "git-import! base path too long");
        return false;
    }

    if (stat(base_candidate, &st) != 0) {
        if (errno != ENOENT) {
            *error_out = atom_symbol(
                eval_arena, "git-import! base path unavailable");
            return false;
        }
        printf("What %s\n", base_directory);
        fflush(stdout);
        if (!ensure_directory_path(base_candidate)) {
            *error_out = atom_symbol(
                eval_arena, "git-import! cannot create base directory");
            return false;
        }
    } else if (!S_ISDIR(st.st_mode)) {
        *error_out = atom_symbol(
            eval_arena, "git-import! base path is not a directory");
        return false;
    }
    if (!realpath(base_candidate, base_resolved)) {
        *error_out = atom_symbol(
            eval_arena, "git-import! cannot resolve base directory");
        return false;
    }
    if (!path_join2(
            local_path, sizeof(local_path),
            base_resolved, module_name)) {
        *error_out = atom_symbol(
            eval_arena, "git-import! repository path too long");
        return false;
    }

    if (pinned_request) {
        if (!ensure_petta_pinned_git_checkout(
                git_path, build_command,
                base_resolved, module_name,
                local_path, normalized_commit,
                eval_arena, error_out)) {
            return false;
        }
    } else if (stat(local_path, &st) == 0) {
        if (!S_ISDIR(st.st_mode)) {
            *error_out = atom_symbol(
                eval_arena,
                "git-import! repository path is not a directory");
            return false;
        }
    } else {
        if (errno != ENOENT) {
            *error_out = atom_symbol(
                eval_arena, "git-import! repository path unavailable");
            return false;
        }
        n = snprintf(
            temporary_path, sizeof(temporary_path),
            "%s/.%s.tmp.XXXXXX", base_resolved, module_name);
        if (!(n > 0 && (size_t)n < sizeof(temporary_path)) ||
            !mkdtemp(temporary_path)) {
            *error_out = atom_symbol(
                eval_arena, "git-import! temporary path unavailable");
            return false;
        }
        printf("Cloning %s into %s/%s\n",
               git_path, base_directory, module_name);
        fflush(stdout);
        if (!run_git_clone(
                git_path, temporary_path,
                clone_error, sizeof(clone_error))) {
            (void)remove_tree_recursive(temporary_path);
            *error_out = atom_symbol(
                eval_arena,
                clone_error[0]
                    ? clone_error
                    : "git-import! clone failed");
            return false;
        }
        if (rename(temporary_path, local_path) != 0) {
            if (stat(local_path, &st) != 0 ||
                !S_ISDIR(st.st_mode)) {
                (void)remove_tree_recursive(temporary_path);
                *error_out = atom_symbol(
                    eval_arena,
                    "git-import! repository publication failed");
                return false;
            }
            (void)remove_tree_recursive(temporary_path);
        } else {
            created_repository = true;
        }

        if (build_command && *build_command) {
            printf("Running build: %s in %s/%s\n",
                   build_command, base_directory, module_name);
            fflush(stdout);
            if (!run_petta_build_command(
                    local_path, build_command,
                    build_error, sizeof(build_error))) {
                if (created_repository)
                    (void)remove_tree_recursive(local_path);
                *error_out = atom_symbol(
                    eval_arena,
                    build_error[0]
                        ? build_error
                        : "git-import! build failed");
                return false;
            }
        }
    }

    char canonical_path[PATH_MAX];
    if (!realpath(local_path, canonical_path)) {
        *error_out = atom_symbol(
            eval_arena, "git-import! repository root unavailable");
        return false;
    }
    return commit_module_mount_and_petta_root(
        ctx, module_name, canonical_path,
        CETTA_MODULE_PROVIDER_GIT_REMOTE,
        CETTA_MODULE_LOCATOR_GIT_URL,
        git_path,
        pinned_request
            ? CETTA_REMOTE_REVISION_EXPLICIT_COMMIT
            : CETTA_REMOTE_REVISION_DEFAULT_BRANCH_ONLY,
        pinned_request ? normalized_commit : "",
        CETTA_PROFILE_MASK_ALL,
        eval_arena, error_out);
}

bool cetta_library_petta_lib_import_enabled(
    const CettaLibraryContext *ctx) {
    return ctx &&
           ctx->session.language_id == CETTA_LANGUAGE_PETTA &&
           (ctx->active_mask & CETTA_LIBRARY_PETTA_IMPORT) != 0u;
}

bool cetta_library_petta_tabling_enabled(
    const CettaLibraryContext *ctx) {
    return ctx &&
           ctx->session.language_id == CETTA_LANGUAGE_PETTA &&
           (ctx->active_mask & CETTA_LIBRARY_PETTA_TABLING) != 0u;
}

/* A completed name is a module identity, not another provider search. Match
 * within the importing module's scope; registered mounts retain their own
 * rebinding policy and are resolved through the normal import plan. */
static CettaLoadedModule *loaded_he_named_module(CettaLibraryContext *ctx,
                                                const char *spec,
                                                Registry *registry) {
    if (!ctx || ctx->session.language_id != CETTA_LANGUAGE_HE ||
        ctx->loaded_module_len == 0u ||
        !module_name_is_legal(spec) ||
        module_mount_lookup_any(ctx, spec))
        return NULL;
    Atom *self = registry
        ? registry_lookup_id(registry, g_builtin_syms.self) : NULL;
    Space *importer = self && self->kind == ATOM_GROUNDED &&
            self->ground.gkind == GV_SPACE
        ? (Space *)self->ground.ptr : NULL;
    const char *parent = cetta_library_module_space_name(ctx, importer);
    if (!parent)
        parent = "top";
    size_t parent_len = strlen(parent);
    size_t spec_len = strlen(spec);
    for (uint32_t i = 0u; i < ctx->loaded_module_len; i++) {
        CettaLoadedModule *entry = ctx->loaded_modules[i];
        if (!entry->space || entry->loading || !entry->module_name ||
            !loaded_module_visible(ctx, entry))
            continue;
        if (strlen(entry->module_name) == parent_len + 1u + spec_len &&
            memcmp(entry->module_name, parent, parent_len) == 0 &&
            entry->module_name[parent_len] == ':' &&
            strcmp(entry->module_name + parent_len + 1u, spec) == 0)
            return entry;
    }
    return NULL;
}

bool cetta_library_import_module(CettaLibraryContext *ctx, const char *spec,
                                 Space *space, bool target_is_fresh,
                                 Arena *eval_arena,
                                 Arena *persistent_arena, Registry *registry,
                                 int fuel, Space **module_space_out,
                                 Atom **error_out) {
    Space *logical_space = logical_import_space(ctx, space);
    CettaModuleSpec parsed_spec;
    CettaImportPlan plan;
    if (module_space_out)
        *module_space_out = NULL;

    const CettaLibrarySpec *interface = spec ? cetta_library_lookup(spec) : NULL;
    bool word_interface = interface && interface->he_words;
    if (spec && ((interface && !word_interface) || cetta_native_module_lookup(spec))) {
        if (target_is_fresh) {
            *error_out = atom_symbol(eval_arena,
                                     "builtin libraries can only import into existing spaces");
            return false;
        }
        return cetta_library_import(ctx, spec, space, eval_arena,
                                    persistent_arena, registry, fuel, error_out);
    }

    CettaLoadedModule *entry = loaded_he_named_module(ctx, spec, registry);
    if (!entry) {
        if (!parse_module_spec(spec, &parsed_spec, eval_arena, error_out))
            return false;
        if (!resolve_import_plan(ctx, &parsed_spec, logical_space, space,
                                 target_is_fresh, &plan, eval_arena, error_out))
            return false;
    }
    if (entry || library_loads_he_module(ctx, &plan) ||
        library_loads_he_provider(ctx, &plan)) {
        /* HE: the module is loaded once, into its own space.  `&self` reads
           it live as a dependency; a fresh token denotes its space. */
        if (!entry)
            entry = ensure_loaded_module(ctx, &plan, eval_arena,
                persistent_arena, registry, fuel, error_out);
        if (!entry)
            return false;
        if (target_is_fresh) {
            if (module_space_out)
                *module_space_out = entry->space;
            if (word_interface && entry->provider_kind == CETTA_MODULE_PROVIDER_STDLIB_FILE)
                ctx->active_mask |= interface->bit;
            return true;
        }
        bool imported;
        if (entry->provider || entry->export_words)
            imported = library_name_he_provider(ctx, space, registry, entry,
                                               eval_arena, persistent_arena, error_out);
        else
            imported = library_link_he_module(space, entry->space, eval_arena,
                                             error_out);
        /* A vocabulary library may also own pure native operations. Activate
         * them only after its standard-library import actually succeeded. */
        if (imported && word_interface && entry->provider_kind == CETTA_MODULE_PROVIDER_STDLIB_FILE)
            ctx->active_mask |= interface->bit;
        return imported;
    }
    bool imported = execute_import_plan(ctx, &plan, eval_arena, persistent_arena,
                                       registry, fuel, NULL, error_out);
    if (imported && word_interface && plan.provider_kind == CETTA_MODULE_PROVIDER_STDLIB_FILE)
        ctx->active_mask |= interface->bit;
    return imported;
}

bool cetta_library_import_error_is_missing_module(Atom *error) {
    return error && atom_is_symbol(error, CETTA_LIBRARY_MISSING_MODULE);
}

static bool resolve_library_member_candidate(
    CettaLibraryContext *ctx, const char *member,
    char *canonical_path, size_t canonical_path_size,
    CettaModuleFormat *format) {
    char candidate[PATH_MAX];
    char reason[160];

    if (!ctx || !member ||
        !cetta_module_policy_allows(
            &ctx->session.module_policy,
            CETTA_MODULE_PROVIDER_STDLIB) ||
        !build_library_path(
            ctx, member, candidate, sizeof(candidate))) {
        return false;
    }
    return resolve_module_candidate_with_format(
        ctx, candidate, canonical_path, canonical_path_size,
        format, reason, sizeof(reason));
}

static bool resolve_standard_library_reference(
    CettaLibraryContext *ctx, const char *member,
    char *canonical_path, size_t canonical_path_size) {
    CettaModuleFormat format = {
        .kind = CETTA_MODULE_FORMAT_METTA,
        .foreign_backend = CETTA_FOREIGN_BACKEND_NONE,
    };
    if (canonical_path && canonical_path_size > 0u)
        canonical_path[0] = '\0';
    if (!ctx || !canonical_path || canonical_path_size == 0u ||
        !member ||
        !cetta_module_policy_allows(
            &ctx->session.module_policy,
            CETTA_MODULE_PROVIDER_STDLIB)) {
        return false;
    }
    if (resolve_library_member_candidate(
            ctx, member, canonical_path,
            canonical_path_size, &format)) {
        return true;
    }

    /* A standard-library descriptor may also name a non-MeTTa resource for a
     * separate consumer.  Exact resources retain their suffix; importable
     * MeTTa members above receive the ordinary .metta completion. */
    char candidate[PATH_MAX];
    char resolved[PATH_MAX];
    struct stat st;
    if (!build_library_resource_path(
            ctx, member, "", candidate, sizeof(candidate)) ||
        stat(candidate, &st) != 0 || !S_ISREG(st.st_mode) ||
        !realpath(candidate, resolved) ||
        strlen(resolved) >= canonical_path_size) {
        return false;
    }
    memcpy(canonical_path, resolved, strlen(resolved) + 1u);
    return true;
}

static bool resolve_rooted_library_reference(
    CettaLibraryContext *ctx, const char *root,
    const char *member, char *canonical_path,
    size_t canonical_path_size) {
    char root_path[PATH_MAX];
    char candidate[PATH_MAX];
    char resolved[PATH_MAX];

    if (canonical_path && canonical_path_size > 0u)
        canonical_path[0] = '\0';
    if (!ctx || !root || !member ||
        !canonical_path || canonical_path_size == 0u ||
        !cetta_module_policy_allows(
            &ctx->session.module_policy,
            CETTA_MODULE_PROVIDER_REGISTERED_ROOTS)) {
        return false;
    }
    if (!cetta_library_petta_root_path_at(
            ctx, root, 0u, root_path, sizeof(root_path)) ||
        !path_join2(
            candidate, sizeof(candidate),
            root_path, member) ||
        access(candidate, R_OK) != 0 ||
        !realpath(candidate, resolved) ||
        strlen(resolved) >= canonical_path_size) {
        return false;
    }
    memcpy(canonical_path, resolved, strlen(resolved) + 1u);
    return true;
}

bool cetta_library_petta_resolve_reference(
    CettaLibraryContext *ctx,
    const PeTTaLibraryReference *reference,
    char *canonical_path, size_t canonical_path_size) {
    if (canonical_path && canonical_path_size > 0u)
        canonical_path[0] = '\0';
    if (!reference)
        return false;
    switch (reference->kind) {
    case PETTA_LIBRARY_REFERENCE_STANDARD:
        return reference->root == NULL &&
               resolve_standard_library_reference(
                   ctx, reference->member,
                   canonical_path, canonical_path_size);
    case PETTA_LIBRARY_REFERENCE_ROOTED:
        return resolve_rooted_library_reference(
            ctx, reference->root, reference->member,
            canonical_path, canonical_path_size);
    case PETTA_LIBRARY_REFERENCE_NONE:
        return false;
    }
    return false;
}

static bool import_standard_library_reference(
    CettaLibraryContext *ctx, const char *member,
    Space *space, bool target_is_fresh,
    Arena *eval_arena, Arena *persistent_arena,
    Registry *registry, int fuel, Atom **error_out) {
    if (!ctx || !space || !eval_arena || !persistent_arena ||
        !registry || !error_out) {
        return false;
    }
    /* PeTTa's import! keeps an authored .metta extension and supplies a
     * missing one, so `X` and `X.metta` name the same library member. */
    char stem[PATH_MAX];
    size_t member_length = strlen(member);
    size_t suffix_length = strlen(".metta");
    if (member_length > suffix_length &&
        member_length < sizeof(stem) &&
        path_has_suffix(member, ".metta")) {
        memcpy(stem, member, member_length - suffix_length);
        stem[member_length - suffix_length] = '\0';
        member = stem;
    }

    /* Native libraries activate a capability as well as loading their source
     * presentation.  Pure source libraries use the ordinary standard-library
     * reference below and therefore need no entry in the native mask. */
    if (cetta_library_lookup(member)) {
        return cetta_library_import(
            ctx, member, space, eval_arena,
            persistent_arena, registry, fuel, error_out);
    }

    CettaModuleSpec spec;
    memset(&spec, 0, sizeof(spec));
    spec.kind = CETTA_MODULE_SPEC_STDLIB;
    snprintf(spec.raw_spec, sizeof(spec.raw_spec), "%s", member);
    snprintf(spec.path_or_member,
             sizeof(spec.path_or_member), "%s", member);

    CettaImportPlan plan;
    ctx->petta_trusted_library_import_depth++;
    bool imported =
        resolve_import_plan(
            ctx, &spec, logical_import_space(ctx, space), space,
            target_is_fresh, &plan, eval_arena, error_out) &&
        execute_import_plan(
            ctx, &plan, eval_arena, persistent_arena,
            registry, fuel, NULL, error_out);
    ctx->petta_trusted_library_import_depth--;
    return imported;
}

static bool import_rooted_library_reference(
    CettaLibraryContext *ctx, const char *root,
    const char *member, uint32_t occurrence,
    Space *space, bool target_is_fresh,
    Arena *eval_arena, Arena *persistent_arena,
    Registry *registry, int fuel, Atom **error_out) {
    if (!ctx || !root || !member || !space || !eval_arena ||
        !persistent_arena || !registry || !error_out) {
        return false;
    }
    if (!cetta_module_policy_allows(
            &ctx->session.module_policy,
            CETTA_MODULE_PROVIDER_REGISTERED_ROOTS)) {
        *error_out = atom_symbol(
            eval_arena, "registered module roots disabled");
        return false;
    }

    char root_path[PATH_MAX];
    if (!cetta_library_petta_root_path_at(
            ctx, root, occurrence, root_path, sizeof(root_path))) {
        *error_out = atom_symbol(
            eval_arena, "unknown library root");
        return false;
    }

    CettaImportPlan plan;
    memset(&plan, 0, sizeof(plan));
    plan.spec.kind = CETTA_MODULE_SPEC_REGISTERED_ROOT;
    snprintf(plan.spec.raw_spec, sizeof(plan.spec.raw_spec),
             "%s:%s", root, member);
    snprintf(plan.spec.namespace_name,
             sizeof(plan.spec.namespace_name), "%s", root);
    snprintf(plan.spec.path_or_member,
             sizeof(plan.spec.path_or_member), "%s", member);
    plan.logical_target_space = logical_import_space(ctx, space);
    plan.execution_target_space = space;
    plan.target_is_fresh = target_is_fresh;
    plan.transactional =
        ctx->session.module_policy.transactional_imports &&
        !target_is_fresh;
    const CettaModuleMount *mount =
        cetta_library_find_module_mount(ctx, root);
    plan.provider_kind = mount && module_mount_visible(ctx, mount)
        ? mount->provider_kind
        : CETTA_MODULE_PROVIDER_REGISTERED_ROOT;

    char candidate[PATH_MAX];
    char reason[160];
    if (!path_join2(
            candidate, sizeof(candidate),
            root_path, member) ||
        !resolve_module_candidate_with_format(
            ctx, candidate, plan.canonical_path,
            sizeof(plan.canonical_path), &plan.format,
            reason, sizeof(reason))) {
        *error_out = atom_symbol(
            eval_arena,
            reason[0] ? reason : "library member unavailable");
        return false;
    }

    ctx->petta_trusted_library_import_depth++;
    bool imported = execute_import_plan(
        ctx, &plan, eval_arena, persistent_arena,
        registry, fuel, NULL, error_out);
    ctx->petta_trusted_library_import_depth--;
    return imported;
}

bool cetta_library_import_petta_reference_at(
    CettaLibraryContext *ctx,
    const PeTTaLibraryReference *reference,
    uint32_t occurrence,
    Space *space, bool target_is_fresh,
    Arena *eval_arena, Arena *persistent_arena,
    Registry *registry, int fuel, Atom **error_out) {
    if (!reference)
        return false;
    switch (reference->kind) {
    case PETTA_LIBRARY_REFERENCE_STANDARD:
        return occurrence == 0u && reference->root == NULL &&
               import_standard_library_reference(
                   ctx, reference->member, space, target_is_fresh,
                   eval_arena, persistent_arena, registry, fuel,
                   error_out);
    case PETTA_LIBRARY_REFERENCE_ROOTED:
        return import_rooted_library_reference(
            ctx, reference->root, reference->member,
            occurrence,
            space, target_is_fresh,
            eval_arena, persistent_arena, registry, fuel,
            error_out);
    case PETTA_LIBRARY_REFERENCE_NONE:
        return false;
    }
    return false;
}

/* Administrative directives are data, not expressions to reduce. Keeping
 * this entry separate means no evaluator path (including nested eval, imports,
 * superpose, or rho payloads) can manufacture durable commit authority. */
Atom *cetta_library_durable_admin(CettaLibraryContext *ctx, Arena *a, Atom *form) {
    if (!ctx || !(ctx->active_mask & CETTA_LIBRARY_DURABLE) ||
        !form || form->kind!=ATOM_EXPR || !form->expr.len ||
        form->expr.elems[0]->kind!=ATOM_SYMBOL) return NULL;
    const char *name=atom_name_cstr(form->expr.elems[0]);
    if (strncmp(name,"durable:",8)) return NULL;
    const char *operations[]={"open","read","commit","checkpoint","close"};
    for (size_t i=0;i<sizeof(operations)/sizeof(*operations);++i) {
        if (strcmp(name+8,operations[i])) continue;
        char native[64]; snprintf(native,sizeof(native),"__cetta_lib_durable_%s",operations[i]);
        return cetta_durable_admin_dispatch(ctx->durable_runtime,a,atom_symbol(a,native),
            form->expr.elems+1,form->expr.len-1);
    }
    return NULL;
}

/* The active libraries' native operations, as values; a foreign call
 * reports its own outcome (cetta_foreign_call_native). */
static Atom *cetta_library_dispatch_native_value(
    CettaLibraryContext *ctx, Space *space, Arena *a,
    Atom *head, Atom **args, uint32_t nargs, CettaCallResultForm *result_form) {
    *result_form = CETTA_CALL_RESULT_EXPRESSION;
    if (!ctx || !head || head->kind != ATOM_SYMBOL) return NULL;
    if (ctx->session.speculative) {
        if (!cetta_speculative_op_allowed(atom_symbol_current_id(head)))
            return is_grounded_op(atom_symbol_current_id(head)) ? cetta_effect_denied(a) : NULL;
        /* Only these audited mechanisms are reachable. Do not fall through
         * to dynamically registered modules or a foreign runtime. */
        Atom *result=NULL;
        if (ctx->active_mask & CETTA_LIBRARY_STR)
            result=cetta_library_dispatch_str(a,head,args,nargs,result_form);
        if (result) return result;
        if (ctx->active_mask & CETTA_LIBRARY_MATH) {
            result=cetta_math_dispatch(a,head,args,nargs);
            if (result) {
                *result_form=CETTA_CALL_RESULT_COMPLETED_VALUE;
                return result;
            }
        }
#if CETTA_BUILD_WITH_JSON_GSLT
        if (ctx->active_mask & CETTA_LIBRARY_JSON)
            result=cetta_json_library_dispatch_v1(ctx->json_runtime,a,head,args,nargs);
        if (result) return result;
#endif
        if (ctx->active_mask & CETTA_LIBRARY_RHOMETTA)
            result=cetta_library_dispatch_rhometta(ctx,space,a,head,args,nargs,result_form);
        if (result) return result;
        if (atom_symbol_current_id(head)==g_builtin_syms.lib_lts_rho_transitions &&
            (ctx->active_mask & CETTA_LIBRARY_LTS))
            return cetta_library_dispatch_lts(ctx,space,a,head,args,nargs);
        return NULL;
    }
    {
        Atom *result = cetta_rule_machine_dispatch(a, head, args, nargs);
        if (result) return result;
    }
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
    if (ctx->active_mask & CETTA_LIBRARY_LIB_PROLOG) {
        Atom *result = cetta_library_dispatch_lib_prolog(
            ctx, a, head, args, nargs);
        if (result) return result;
    }
    if (ctx->active_mask & CETTA_LIBRARY_LTS) {
        Atom *result = cetta_library_dispatch_lts(ctx, space, a, head, args, nargs);
        if (result) return result;
    }
    if (ctx->active_mask & CETTA_LIBRARY_FS) {
        Atom *result = cetta_library_dispatch_fs(a, head, args, nargs);
        if (result) return result;
    }
    if (ctx->active_mask & CETTA_LIBRARY_DURABLE) {
        Atom *result = cetta_durable_dispatch(ctx->durable_runtime, a, head, args, nargs);
        if (result) {
            *result_form = CETTA_CALL_RESULT_COMPLETED_VALUE;
            return result;
        }
    }
    if (ctx->active_mask & CETTA_LIBRARY_IO) {
        Atom *result = cetta_io_dispatch(ctx->io_runtime, a, head, args, nargs);
        if (result) return result;
    }
    if (ctx->active_mask & CETTA_LIBRARY_CWP) {
        Atom *result = cetta_cwp_dispatch(a, head, args, nargs);
        if (result) return result;
    }
    if (ctx->active_mask & CETTA_LIBRARY_SPACE_SERVICE) {
        Atom *result = cetta_space_service_dispatch(a, head, args, nargs);
        if (result) {
            *result_form = CETTA_CALL_RESULT_COMPLETED_VALUE;
            return result;
        }
    }
    if (ctx->active_mask & CETTA_LIBRARY_STORE) {
        Atom *result=cetta_store_dispatch(ctx,a,head,args,nargs);
        if (result) {
            *result_form=CETTA_CALL_RESULT_COMPLETED_VALUE;
            return result;
        }
    }
    if (ctx->active_mask & CETTA_LIBRARY_MATH) {
        Atom *result=cetta_math_dispatch(a,head,args,nargs);
        if (result) {
            *result_form=CETTA_CALL_RESULT_COMPLETED_VALUE;
            return result;
        }
    }
    if (ctx->active_mask & CETTA_LIBRARY_PROC) {
        Atom *result = cetta_proc_dispatch(a, head, args, nargs);
        if (result) return result;
    }
    if (ctx->active_mask & CETTA_LIBRARY_SQLITE) {
        Atom *result = cetta_sqlite_dispatch(a, head, args, nargs);
        if (result) return result;
    }
#if CETTA_BUILD_WITH_JSON_GSLT
    if (ctx->active_mask & CETTA_LIBRARY_JSON) {
        Atom *result = cetta_json_library_dispatch_v1(
            ctx->json_runtime, a, head, args, nargs);
        if (result) {
            *result_form = CETTA_CALL_RESULT_COMPLETED_VALUE;
            return result;
        }
    }
#endif
    if (ctx->active_mask & CETTA_LIBRARY_STR) {
        Atom *result = cetta_library_dispatch_str(a, head, args, nargs, result_form);
        if (result) return result;
    }
    if (ctx->active_mask & CETTA_LIBRARY_RHOMETTA) {
        Atom *result = cetta_library_dispatch_rhometta(ctx, space, a, head,
                                                       args, nargs, result_form);
        if (result) return result;
    }
    return NULL;
}

bool cetta_library_call_native(CettaLibraryContext *ctx, Space *space,
                               Arena *a, Atom *head, Atom **args,
                               uint32_t nargs, CettaCallOutcome *out) {
    Atom **visible = library_native_arguments(a, args, nargs);
    if (nargs && !visible) {
        *out = cetta_call_interrupted();
        return true;
    }
    args = visible;
    if (ctx && cetta_group_sum_call(a, head, args, nargs, out)) return true;
    if (ctx && cetta_key_factorize_call(space, a, head, args, nargs, out)) return true;
    if (ctx && (ctx->active_mask & CETTA_LIBRARY_TENSOR) &&
        cetta_tensor_call(ctx, space, a, head, args, nargs, out)) return true;
    if (ctx && (ctx->active_mask & CETTA_LIBRARY_POSTINGS) &&
        cetta_postings_call(ctx, a, head, args, nargs, out)) return true;
    if (ctx && (ctx->active_mask & CETTA_LIBRARY_RE) &&
        cetta_re_call(ctx, a, head, args, nargs, out)) return true;
    if (ctx && (ctx->active_mask & CETTA_LIBRARY_CRYPTO) &&
        cetta_crypto_call(ctx, a, head, args, nargs, out)) return true;
    if (ctx && (ctx->active_mask & CETTA_LIBRARY_TOML) &&
        cetta_toml_call(ctx, a, head, args, nargs, out)) return true;
    if (ctx && (ctx->active_mask & CETTA_LIBRARY_FS) &&
        cetta_fs_call(ctx, a, head, args, nargs, out)) return true;
    /* lib/proc offers two families: owned process jobs (spawn, poll, wait,
     * status, cancel, close, await, and run with argv and options), and the
     * capturing runner proc:run with cwd, environment, timeout and capture
     * limit, five or six arguments, which cetta_proc_dispatch answers. */
    if (ctx && (ctx->active_mask & CETTA_LIBRARY_PROC) &&
        !(atom_symbol_current_id(head) == g_builtin_syms.lib_proc_run &&
          nargs != 2u) &&
        cetta_proc_call(ctx, a, head, args, nargs, out)) return true;
    CettaCallResultForm result_form = CETTA_CALL_RESULT_EXPRESSION;
    Atom *value = cetta_library_dispatch_native_value(
        ctx, space, a, head, args, nargs, &result_form);
    if (value) {
        *out = atom_is_petta_no_result(value)
            ? cetta_call_failure()
            : (CettaCallOutcome){.kind = CETTA_CALL_VALUE,
                                 .term = value, .result_form = result_form};
        return true;
    }
    if (ctx && cetta_native_module_call_active(ctx, space, a, head, args, nargs,
                                               ctx->active_mask, out))
        return true;
    return ctx && ctx->foreign_runtime &&
           cetta_foreign_call_native(ctx->foreign_runtime,
                                     space, a, head, args, nargs, out);
}

void cetta_library_context_prime_snapshot_storage(const CettaLibraryContext *ctx,
    CettaStorageCensus *census) {
    if (!ctx || !cetta_storage_object(census, ctx, sizeof(*ctx), CETTA_STORAGE_BLOCK)) return;
    term_universe_storage_census(&ctx->term_universe, census);
    petta_program_storage_census(ctx->petta_program, census);
    /* This constructor/profile has no such owners. Unexpected state is
     * an incomplete census, never a zero-byte approximation of a service. */
    if (!library_prime_snapshot_common(ctx) || !ctx->nik_runtime_mutex_ready ||
        ctx->import_dirs || ctx->import_dir_len || ctx->import_dir_cap ||
        ctx->module_mounts || ctx->module_mount_len || ctx->module_mount_cap ||
        ctx->petta_library_paths.items || ctx->petta_library_paths.len || ctx->petta_library_paths.cap ||
        ctx->imported_files || ctx->imported_file_len || ctx->imported_file_cap || ctx->cmdline_arg_len ||
        ctx->loaded_modules || ctx->loaded_module_len || ctx->loaded_module_cap ||
        ctx->module_own_views || ctx->module_own_view_cap ||
        ctx->retired_modules || ctx->retired_module_cap ||
        ctx->he_module_aliases || ctx->he_module_alias_len || ctx->he_module_alias_cap ||
        ctx->native_handle_registry || ctx->native_handle_next_id!=1u ||
        ctx->lib_prolog || ctx->foreign_runtime || ctx->parser_cache ||
        ctx->prime_hol_guest || ctx->prime_hol_guest_free || ctx->prime_lf_guest || ctx->prime_lf_guest_free)
        cetta_storage_refuse(census, "PrivateServiceProfileChanged");
}

bool cetta_library_call_native_prepared_view(
    CettaLibraryContext *ctx, Space *space,
    Arena *a, Atom *head, Atom **args,
    uint32_t nargs, CettaCallOutcome *out) {
    if (!ctx || !a || !head || !out)
        return false;
    Atom **visible = library_native_arguments(a, args, nargs);
    if (nargs && !visible) {
        *out = cetta_call_interrupted();
        return true;
    }
    args = visible;
#if CETTA_BUILD_WITH_JSON_GSLT
    if (ctx->active_mask & CETTA_LIBRARY_JSON) {
        Atom *value = NULL;
        if (cetta_json_library_borrowed_value_v1(
                ctx->json_runtime, a, head, args, nargs, &value)) {
            *out = cetta_call_completed_value(value);
            return true;
        }
    }
#endif
    return cetta_library_call_native(
        ctx, space, a, head, args, nargs, out);
}

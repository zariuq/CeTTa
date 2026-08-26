#ifndef CETTA_LIBRARY_H
#define CETTA_LIBRARY_H

#include "atom.h"
#include "foreign.h"
#include "mork_space_bridge_runtime.h"
#include "native_handle.h"
#include "lib_prolog.h"
#include "petta_program.h"
#include "petta_libpl.h"
#include "session.h"
#include "space.h"
#include <stdbool.h>
#include <limits.h>
#include <pthread.h>

#define CETTA_MAX_MODULE_ROOTS 32
#define CETTA_MAX_IMPORT_DIR_DEPTH 32
#define CETTA_MAX_IMPORTED_FILES 256
#define CETTA_MAX_IMPORT_TRANSACTION_SPACES 32
#define CETTA_MAX_LOADED_MODULES 64
#define CETTA_MAX_CMDLINE_ARGS 256
#define CETTA_MAX_NATIVE_HANDLES 64

typedef struct {
    char display_name[PATH_MAX];
    char canonical_path[PATH_MAX];
    CettaModuleProviderKind provider_kind;
    CettaModuleFormat format;
    Space *space;
    bool loading;
} CettaLoadedModule;

typedef struct {
    SymbolId head;
    CettaExprLen arity;
} CettaPettaRelationKey;

typedef enum {
    CETTA_PETTA_MEMO_STRATEGY_WTINYLFU = 0,
    CETTA_PETTA_MEMO_STRATEGY_LRU,
} CettaPettaMemoStrategy;

typedef enum {
    CETTA_PETTA_MEMO_AGGREGATE_NONE = 0,
    CETTA_PETTA_MEMO_AGGREGATE_MIN,
    CETTA_PETTA_MEMO_AGGREGATE_MAX,
    CETTA_PETTA_MEMO_AGGREGATE_SUM,
    CETTA_PETTA_MEMO_AGGREGATE_COUNT,
} CettaPettaMemoAggregate;

typedef enum {
    CETTA_PETTA_MEMO_CONTROL_MEMOIZE = 0,
    CETTA_PETTA_MEMO_CONTROL_CONFIGURE,
    CETTA_PETTA_MEMO_CONTROL_GET_CONFIG,
    CETTA_PETTA_MEMO_CONTROL_CLEAR,
    CETTA_PETTA_MEMO_CONTROL_INVALIDATE,
    CETTA_PETTA_MEMO_CONTROL_IS_MEMOIZED,
    CETTA_PETTA_MEMO_CONTROL_GET_STATS,
    CETTA_PETTA_MEMO_CONTROL_CLEAR_STATS,
    CETTA_PETTA_MEMO_CONTROL_COUNT,
} CettaPettaMemoControl;

typedef struct {
    SymbolId *all_arities;
    uint32_t all_arity_len;
    uint32_t all_arity_cap;
    CettaPettaRelationKey *exact_arities;
    uint32_t exact_arity_len;
    uint32_t exact_arity_cap;
    uint64_t symbol_table_instance;
    CettaPettaMemoStrategy strategy;
    uint32_t unique_limit;
    uint64_t size_limit_bytes;
    uint32_t float_precision;
    uint32_t answer_limit;
    CettaPettaMemoAggregate aggregate;
    uint64_t cache_hits;
    uint64_t cache_misses;
    uint64_t answer_limit_truncated;
    bool imported_controls[CETTA_PETTA_MEMO_CONTROL_COUNT];
} CettaPettaMemoState;

struct PettaMachineTable;

struct CettaPettaTokenSpaceClauseRegistry;

struct CettaNikRuntimeV1;
struct CettaPettaTypecheckV3;
struct CettaPettaRuntimeState;
struct CettaIoRuntime;
struct CettaJsonLibraryRuntimeV1;

typedef struct CettaLibraryContext {
    CettaEvalSession session;
    pthread_mutex_t nik_runtime_mutex;
    bool nik_runtime_mutex_ready;
    struct CettaNikRuntimeV1 *nik_runtime;
    TermUniverse term_universe;
    uint32_t active_mask;
    char root_dir[4096];
    char working_dir[PATH_MAX];
    char script_path[PATH_MAX];
    char script_dir[PATH_MAX];
    char import_dirs[CETTA_MAX_IMPORT_DIR_DEPTH][PATH_MAX];
    uint32_t import_dir_len;
    CettaModuleMount module_mounts[CETTA_MAX_MODULE_ROOTS];
    uint32_t module_mount_len;
    struct {
        Space *space;
        bool loading;
        char path[PATH_MAX];
    } imported_files[CETTA_MAX_IMPORTED_FILES];
    uint32_t imported_file_len;
    uint32_t petta_trusted_library_import_depth;
    struct {
        Space *work_space;
        Space *logical_space;
    } import_space_aliases[CETTA_MAX_IMPORT_TRANSACTION_SPACES];
    uint32_t import_space_alias_len;
    const char *cmdline_args[CETTA_MAX_CMDLINE_ARGS];
    uint32_t cmdline_arg_len;
    CettaLoadedModule loaded_modules[CETTA_MAX_LOADED_MODULES];
    uint32_t loaded_module_len;
    CettaNativeHandleSlot native_handles[CETTA_MAX_NATIVE_HANDLES];
    uint32_t native_handle_len;
    uint64_t native_handle_next_id;
    SymbolId *petta_translator_rules;
    uint32_t petta_translator_rule_len;
    uint32_t petta_translator_rule_cap;
    uint64_t petta_translator_symbol_table_instance;
    CettaPettaRelationKey *petta_tabled_relations;
    uint32_t petta_tabled_relation_len;
    uint32_t petta_tabled_relation_cap;
    uint64_t petta_tabled_symbol_table_instance;
    CettaPettaMemoState petta_memo;
    struct PettaMachineTable *petta_shared_table;
    bool prime_relational_plan_enabled;
    struct CettaPettaRuntimeState *petta_runtime;
    PettaProgram *petta_program;
    struct CettaPettaTypecheckV3 *petta_typecheck_v3;
    struct CettaPettaTokenSpaceClauseRegistry *
        petta_token_space_clause_registry;
    CettaLibPrologRuntime *lib_prolog;
    CettaForeignRuntime *foreign_runtime;
    struct CettaIoRuntime *io_runtime;
    struct CettaJsonLibraryRuntimeV1 *json_runtime;
} CettaLibraryContext;

void cetta_library_context_init(CettaLibraryContext *ctx);
void cetta_library_context_init_for_language_profile(CettaLibraryContext *ctx,
                                                     CettaLanguageId language_id,
                                                     const CettaProfile *profile);
void cetta_library_context_free(CettaLibraryContext *ctx);
struct CettaNikRuntimeV1 *cetta_library_context_nik_runtime(
    CettaLibraryContext *ctx,
    char *error_buf,
    size_t error_buf_size);
bool cetta_library_root_for_exec_path(const char *argv0,
                                      char *output, size_t output_size);
void cetta_library_context_set_exec_path(CettaLibraryContext *ctx, const char *argv0);
void cetta_library_context_set_script_path(CettaLibraryContext *ctx, const char *filename);
void cetta_library_context_set_cli_args(CettaLibraryContext *ctx, int argc,
                                        char **argv, int arg_start);
uint32_t cetta_library_module_mount_count(const CettaLibraryContext *ctx);
const CettaModuleMount *cetta_library_module_mount_at(const CettaLibraryContext *ctx,
                                                      uint32_t index);
const CettaModuleMount *cetta_library_find_module_mount(const CettaLibraryContext *ctx,
                                                        const char *namespace_name);
uint32_t cetta_library_loaded_module_count(const CettaLibraryContext *ctx);
const CettaLoadedModule *cetta_library_loaded_module_at(const CettaLibraryContext *ctx,
                                                        uint32_t index);

bool cetta_library_import(CettaLibraryContext *ctx, const char *name,
                          Space *space, Arena *eval_arena,
                          Arena *persistent_arena, Registry *registry,
                          int fuel, Atom **error_out);

bool cetta_library_register_module(CettaLibraryContext *ctx, const char *path,
                                   Arena *eval_arena, Atom **error_out);
bool cetta_library_register_git_module(CettaLibraryContext *ctx, const char *url,
                                       Arena *eval_arena, Atom **error_out);
bool cetta_library_petta_git_import(CettaLibraryContext *ctx,
                                    const char *git_path,
                                    const char *build_command,
                                    const char *base_directory,
                                    Arena *eval_arena,
                                    Atom **error_out);
bool cetta_library_petta_git_import_enabled(
    const CettaLibraryContext *ctx);

bool cetta_library_import_module(CettaLibraryContext *ctx, const char *spec,
                                 Space *space, bool target_is_fresh,
                                 Arena *eval_arena,
                                 Arena *persistent_arena, Registry *registry,
                                 int fuel, Atom **error_out);
bool cetta_library_import_library_member(
    CettaLibraryContext *ctx, const char *member,
    Space *space, bool target_is_fresh,
    Arena *eval_arena, Arena *persistent_arena,
    Registry *registry, int fuel, Atom **error_out);
bool cetta_library_import_rooted_library_member(
    CettaLibraryContext *ctx, const char *root,
    const char *member, Space *space, bool target_is_fresh,
    Arena *eval_arena, Arena *persistent_arena,
    Registry *registry, int fuel, Atom **error_out);
/*
 * Resolve a PeTTa `(library Member)` value through the same package mounts
 * and ancestor search used by descriptor imports, without loading it.
 */
bool cetta_library_petta_resolve_library_member(
    CettaLibraryContext *ctx, const char *member,
    char *canonical_path, size_t canonical_path_size);
bool cetta_library_petta_resolve_library_file(
    CettaLibraryContext *ctx, const char *root,
    const char *member, char *canonical_path,
    size_t canonical_path_size);
bool cetta_library_include_module(CettaLibraryContext *ctx, const char *spec,
                                  Space *space, Arena *eval_arena,
                                  Arena *persistent_arena, Registry *registry,
                                  int fuel, Atom **error_out);
Atom *cetta_library_mod_space(CettaLibraryContext *ctx, const char *spec,
                              Arena *eval_arena, Arena *persistent_arena,
                              Registry *registry, int fuel, Atom **error_out);
Atom *cetta_library_module_inventory_space(CettaLibraryContext *ctx,
                                           Arena *eval_arena,
                                           Arena *persistent_arena,
                                           Atom **error_out);
bool cetta_library_print_loaded_modules(CettaLibraryContext *ctx, FILE *out,
                                        Arena *eval_arena, Atom **error_out);

Atom *cetta_library_dispatch_native(CettaLibraryContext *ctx, Space *space,
                                    Arena *a,
                                    Atom *head, Atom **args, uint32_t nargs);
bool cetta_library_lookup_explicit_mork_bridge(CettaLibraryContext *ctx,
                                               Atom *space_arg,
                                               CettaMorkSpaceHandle **bridge_out);
bool cetta_library_petta_translator_rule_contains(
    CettaLibraryContext *ctx, SymbolId head);
bool cetta_library_petta_translator_rule_set(
    CettaLibraryContext *ctx, SymbolId head, bool enabled);
bool cetta_library_petta_tabled_relation_contains(
    CettaLibraryContext *ctx, SymbolId head, CettaExprLen arity);
bool cetta_library_petta_tabled_relation_set(
    CettaLibraryContext *ctx, SymbolId head, CettaExprLen arity,
    bool enabled);
bool cetta_library_petta_memo_contains(
    CettaLibraryContext *ctx, SymbolId head, CettaExprLen arity);
bool cetta_library_petta_memo_enable(
    CettaLibraryContext *ctx, SymbolId head,
    bool every_arity, CettaExprLen arity);
bool cetta_library_petta_memo_is_enabled(
    CettaLibraryContext *ctx, SymbolId head,
    bool exact_arity, CettaExprLen arity);
void cetta_library_petta_memo_clear(CettaLibraryContext *ctx);
void cetta_library_petta_memo_invalidate(
    CettaLibraryContext *ctx, SymbolId head);
void cetta_library_petta_memo_clear_stats(
    CettaLibraryContext *ctx);
void cetta_library_petta_memo_observe(
    CettaLibraryContext *ctx, SymbolId head,
    CettaExprLen arity, bool cache_hit);
void cetta_library_petta_memo_observe_truncation(
    CettaLibraryContext *ctx, SymbolId head,
    CettaExprLen arity);
bool cetta_library_petta_memo_control_import(
    CettaLibraryContext *ctx, SymbolId head);
bool cetta_library_petta_memo_control_imported(
    CettaLibraryContext *ctx, SymbolId head);
PeTTaNamedArity cetta_library_petta_memo_control_named_arity(
    CettaLibraryContext *ctx, SymbolId head,
    CettaExprLen supplied);
bool cetta_library_petta_process_text(
    CettaLibraryContext *ctx, Space *space,
    Arena *eval_arena, Arena *persistent_arena,
    Registry *registry, const char *text,
    Atom **result_out, Atom **error_out);
bool cetta_library_pack_mork_expr_batch(Arena *scratch, Atom **items,
                                        uint32_t item_count,
                                        uint8_t **packet_out,
                                        size_t *packet_len_out,
                                        uint64_t *packet_bytes_out,
                                        uint64_t *pack_ns_out,
                                        const char **error_out);

#endif /* CETTA_LIBRARY_H */

#ifndef CETTA_EXECUTION_CONTRACTS_GENERATED_H
#define CETTA_EXECUTION_CONTRACTS_GENERATED_H

/* Generated from lib/gslt_execution_contracts.metta.
 * Source SHA-256: 8e3db2f8e052811ff5db19e12178a66506b96a88e4906b935ee79f8d40081bf6
 */

#include <stdbool.h>
#include <stdint.h>

#include "../exact_integer.h"

typedef enum {
    CETTA_GSLT_QUERY_EFFECT_PURE = 0u,
    CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY = 1u << 0,
    CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE = 1u << 1,
} CettaGsltQueryEffect;

typedef enum {
    CETTA_GSLT_PLACEMENT_LOCAL = 0,
    CETTA_GSLT_PLACEMENT_COORDINATOR = 1,
    CETTA_GSLT_PLACEMENT_UNRESOLVED = 2,
} CettaGsltWorkerPlacementAdvice;

typedef enum {
    CETTA_GSLT_PROGRESS_BOUNDED = 0,
    CETTA_GSLT_PROGRESS_YIELDING = 1,
    CETTA_GSLT_PROGRESS_ISOLATED = 2,
    CETTA_GSLT_PROGRESS_UNRESOLVED_PROGRESS = 3,
} CettaGsltProgressAdvice;

typedef struct {
    CettaGsltWorkerPlacementAdvice placement;
    CettaGsltProgressAdvice progress;
    uint64_t bound;
} CettaGsltNativeCallAdvice;

#define CETTA_GSLT_NATIVE_CALL_HEAD_ROWS(X) \
    X(decons_atom, CETTA_GSLT_PLACEMENT_LOCAL, CETTA_GSLT_PROGRESS_BOUNDED, 1u) \
    X(cons_atom, CETTA_GSLT_PLACEMENT_LOCAL, CETTA_GSLT_PROGRESS_BOUNDED, 1u) \
    X(car_atom, CETTA_GSLT_PLACEMENT_LOCAL, CETTA_GSLT_PROGRESS_BOUNDED, 1u) \
    X(cdr_atom, CETTA_GSLT_PLACEMENT_LOCAL, CETTA_GSLT_PROGRESS_BOUNDED, 1u) \
    X(get_metatype, CETTA_GSLT_PLACEMENT_LOCAL, CETTA_GSLT_PROGRESS_BOUNDED, 1u) \
    X(context_space, CETTA_GSLT_PLACEMENT_LOCAL, CETTA_GSLT_PROGRESS_BOUNDED, 1u) \
    X(select, CETTA_GSLT_PLACEMENT_COORDINATOR, CETTA_GSLT_PROGRESS_YIELDING, 0u) \
    X(readln_bang, CETTA_GSLT_PLACEMENT_COORDINATOR, CETTA_GSLT_PROGRESS_ISOLATED, 0u) \
    X(lib_rhometta_run_canonical, CETTA_GSLT_PLACEMENT_COORDINATOR, CETTA_GSLT_PROGRESS_YIELDING, 0u) \
    X(lib_rhometta_values, CETTA_GSLT_PLACEMENT_COORDINATOR, CETTA_GSLT_PROGRESS_YIELDING, 0u) \
    X(lib_json_capabilities_v1, CETTA_GSLT_PLACEMENT_COORDINATOR, CETTA_GSLT_PROGRESS_UNRESOLVED_PROGRESS, 0u) \
    X(lib_json_parse_v1, CETTA_GSLT_PLACEMENT_COORDINATOR, CETTA_GSLT_PROGRESS_UNRESOLVED_PROGRESS, 0u) \
    X(lib_json_to_legacy_v1, CETTA_GSLT_PLACEMENT_COORDINATOR, CETTA_GSLT_PROGRESS_UNRESOLVED_PROGRESS, 0u) \
    X(lib_json_from_inspection_v1, CETTA_GSLT_PLACEMENT_COORDINATOR, CETTA_GSLT_PROGRESS_UNRESOLVED_PROGRESS, 0u) \
    X(lib_json_parse, CETTA_GSLT_PLACEMENT_COORDINATOR, CETTA_GSLT_PROGRESS_UNRESOLVED_PROGRESS, 0u) \
    X(lib_json_project_v1, CETTA_GSLT_PLACEMENT_COORDINATOR, CETTA_GSLT_PROGRESS_UNRESOLVED_PROGRESS, 0u) \
    X(lib_json_stringify_v1, CETTA_GSLT_PLACEMENT_COORDINATOR, CETTA_GSLT_PROGRESS_UNRESOLVED_PROGRESS, 0u) \
    X(lib_json_stringify, CETTA_GSLT_PLACEMENT_COORDINATOR, CETTA_GSLT_PROGRESS_UNRESOLVED_PROGRESS, 0u) \
    X(lib_json_members_v1, CETTA_GSLT_PLACEMENT_COORDINATOR, CETTA_GSLT_PROGRESS_UNRESOLVED_PROGRESS, 0u) \
    X(lib_json_get_all_v1, CETTA_GSLT_PLACEMENT_COORDINATOR, CETTA_GSLT_PROGRESS_UNRESOLVED_PROGRESS, 0u) \
    X(lib_json_get_first_v1, CETTA_GSLT_PLACEMENT_COORDINATOR, CETTA_GSLT_PROGRESS_UNRESOLVED_PROGRESS, 0u) \
    X(lib_json_get_last_v1, CETTA_GSLT_PLACEMENT_COORDINATOR, CETTA_GSLT_PROGRESS_UNRESOLVED_PROGRESS, 0u) \
    X(lib_json_object_get, CETTA_GSLT_PLACEMENT_COORDINATOR, CETTA_GSLT_PROGRESS_UNRESOLVED_PROGRESS, 0u) \
    X(lib_proc_spawn, CETTA_GSLT_PLACEMENT_COORDINATOR, CETTA_GSLT_PROGRESS_UNRESOLVED_PROGRESS, 0u) \
    X(lib_proc_poll, CETTA_GSLT_PLACEMENT_COORDINATOR, CETTA_GSLT_PROGRESS_UNRESOLVED_PROGRESS, 0u) \
    X(lib_proc_wait, CETTA_GSLT_PLACEMENT_COORDINATOR, CETTA_GSLT_PROGRESS_UNRESOLVED_PROGRESS, 0u) \
    X(lib_proc_status, CETTA_GSLT_PLACEMENT_COORDINATOR, CETTA_GSLT_PROGRESS_UNRESOLVED_PROGRESS, 0u) \
    X(lib_proc_cancel, CETTA_GSLT_PLACEMENT_COORDINATOR, CETTA_GSLT_PROGRESS_UNRESOLVED_PROGRESS, 0u) \
    X(lib_proc_close, CETTA_GSLT_PLACEMENT_COORDINATOR, CETTA_GSLT_PROGRESS_UNRESOLVED_PROGRESS, 0u) \
    X(lib_proc_await, CETTA_GSLT_PLACEMENT_COORDINATOR, CETTA_GSLT_PROGRESS_UNRESOLVED_PROGRESS, 0u) \
    X(lib_proc_run, CETTA_GSLT_PLACEMENT_COORDINATOR, CETTA_GSLT_PROGRESS_UNRESOLVED_PROGRESS, 0u) \
    X(lib_proc_write, CETTA_GSLT_PLACEMENT_COORDINATOR, CETTA_GSLT_PROGRESS_UNRESOLVED_PROGRESS, 0u) \
    X(lib_proc_read, CETTA_GSLT_PLACEMENT_COORDINATOR, CETTA_GSLT_PROGRESS_UNRESOLVED_PROGRESS, 0u) \
    X(lib_proc_close_input, CETTA_GSLT_PLACEMENT_COORDINATOR, CETTA_GSLT_PROGRESS_UNRESOLVED_PROGRESS, 0u) \
    X(exec_saturate, CETTA_GSLT_PLACEMENT_COORDINATOR, CETTA_GSLT_PROGRESS_UNRESOLVED_PROGRESS, 0u)

#define CETTA_GSLT_NATIVE_CALL_DEFAULT \
    ((CettaGsltNativeCallAdvice){CETTA_GSLT_PLACEMENT_UNRESOLVED, \
        CETTA_GSLT_PROGRESS_UNRESOLVED_PROGRESS, 0u})
#define CETTA_GSLT_NATIVE_CALL_CONSTRUCTOR \
    ((CettaGsltNativeCallAdvice){CETTA_GSLT_PLACEMENT_LOCAL, \
        CETTA_GSLT_PROGRESS_BOUNDED, 1u})
#define CETTA_GSLT_NATIVE_CALL_USER_EQUATION \
    ((CettaGsltNativeCallAdvice){CETTA_GSLT_PLACEMENT_LOCAL, \
        CETTA_GSLT_PROGRESS_YIELDING, 0u})

static inline bool cetta_gslt_native_call_head_advice(
    SymbolId head, const BuiltinSyms *builtins, CettaGsltNativeCallAdvice *out) {
    if (!builtins || !out) return false;
#define CETTA_GSLT_NATIVE_CALL_ADVICE_CASE(field, placement, progress, bound) \
    if (head == builtins->field) { \
        *out = (CettaGsltNativeCallAdvice){placement, progress, bound}; \
        return true; \
    }
    CETTA_GSLT_NATIVE_CALL_HEAD_ROWS(CETTA_GSLT_NATIVE_CALL_ADVICE_CASE)
#undef CETTA_GSLT_NATIVE_CALL_ADVICE_CASE
    *out = CETTA_GSLT_NATIVE_CALL_DEFAULT;
    return false;
}

static inline bool cetta_gslt_progress_inline(CettaGsltProgressAdvice progress) {
    return progress == CETTA_GSLT_PROGRESS_BOUNDED ||
           progress == CETTA_GSLT_PROGRESS_YIELDING;
}

#define CETTA_GSLT_QUERY_EFFECT_HEAD_ROWS(X) \
    X(match, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(get_atoms, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(mork_match_syntax, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(get_type, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(get_type_space, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(add_atom, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(add_atom_nodup, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(exec_saturate, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(add_atoms, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(add_reduct, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(remove_atom, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(mork_add_atom, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(mork_add_atoms, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(mork_remove_atom, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(bind_bang, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(new_space, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(new_state, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(change_state_bang, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(println_bang, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(readln_bang, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(flush_output_bang, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(trace_bang, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(print_alternatives_bang, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(import_bang, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(include, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(register_module_bang, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(mod_space_bang, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(git_import_bang, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(git_module_bang, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(print_mods_bang, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(module_inventory_bang, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(pragma_bang, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(space_set_backend_bang, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(space_set_match_backend_bang, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(reset_runtime_stats_bang, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(step_bang, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(petta_transaction, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(get_state, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(space_push, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(space_pop, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(space_truncate, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(space_get, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(space_len, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(space_peek, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(size, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(count_atoms, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(minimal_space_contains_exact, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(minimal_space_revision, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(runtime_stats_bang, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_system_exit_with_code, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_system_write, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_system_cwd, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_system_monotonic_ns, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_proc_spawn, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_postings_prepare, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_postings_own, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_postings_counts, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_postings_sources_current, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_postings_inspect, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_postings_close, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(tensor_from_values, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(tensor_from_json, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_list_group_sums, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_list_group_sums_blocks, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_list_factorize, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(tensor_from_coo, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(tensor_from_coordinates, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(tensor_inspect, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(tensor_entries, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(tensor_gather, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(tensor_gather_rows, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(tensor_merge, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(tensor_zip_map, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_tensor_try_zip_map, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(tensor_shape, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(tensor_dtype, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(tensor_layout, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(tensor_at, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(tensor_copy, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(tensor_convert, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(tensor_cast, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(tensor_promote, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(tensor_view, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(tensor_reshape, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(tensor_sum, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(tensor_matvec, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(tensor_top_k, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(tensor_encode, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(tensor_decode, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(tensor_builder, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(tensor_put, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(tensor_freeze, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(tensor_freeze_log, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(tensor_close, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(tensor_all_finite, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(tensor_normalize, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(tensor_cosine_index, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(tensor_cosine_scores, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(tensor_cosine_retrieve, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(tensor_index_source, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(tensor_screen_index, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(tensor_screen, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(tensor_screen_candidates, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(tensor_screen_info, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(tensor_rerank, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_proc_poll, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_proc_wait, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_proc_status, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_proc_cancel, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_proc_close, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_proc_abort_signal, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_proc_await, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_proc_run, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_proc_write, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_proc_read, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_proc_close_input, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_prolog_query, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_fs_exists, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_fs_canonical_path, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_fs_try_canonical_path, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_fs_try_canonical_from_file, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_fs_try_canonical_under, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_fs_read_text, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_fs_write, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_fs_rename, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_fs_append_text, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_fs_read_lines, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_fs_metadata, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_fs_walk_open, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_fs_walk_current, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_fs_walk_step, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_fs_walk_metadata, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_fs_open_directory, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_fs_next_entry, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_fs_open_read, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_fs_read_chunk, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_fs_close, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_fs_open_write, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_fs_write_all, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_fs_sync, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_fs_lock, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_fs_unlock, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_fs_make_directories, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_fs_set_mode, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_fs_remove, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_fs_access, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_io_submit, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_io_poll, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_io_http_read, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_io_cancel, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_space_new, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_space_include, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_space_open_act, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_space_dump_act, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_space_import_act, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_space_step, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_space_add_atoms, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_space_add_stream, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_space_add_atom, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_space_remove_atom, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_space_atoms, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_mork_space_size, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_mork_space_count_atoms, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_mork_space_match, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_mork_clone, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_join, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_meet, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_subtract, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_restrict, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_zipper_new, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_zipper_close, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_zipper_path_exists, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_mork_zipper_is_val, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_mork_zipper_child_count, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_mork_zipper_path_bytes, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_mork_zipper_child_bytes, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_mork_zipper_val_count, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_mork_zipper_depth, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_mork_zipper_reset, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_zipper_ascend, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_zipper_descend_byte, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_zipper_descend_index, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_zipper_descend_first, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_zipper_descend_last, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_zipper_descend_until, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_zipper_descend_until_max_bytes, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_zipper_ascend_until, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_zipper_ascend_until_branch, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_zipper_next_sibling_byte, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_zipper_prev_sibling_byte, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_zipper_next_step, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_zipper_next_val, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_zipper_fork, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_zipper_make_map, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_zipper_make_snapshot_map, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_product_zipper_new, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_product_zipper_close, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_product_zipper_path_exists, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_mork_product_zipper_is_val, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_mork_product_zipper_child_count, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_mork_product_zipper_path_bytes, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_mork_product_zipper_child_bytes, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_mork_product_zipper_val_count, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_mork_product_zipper_depth, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_mork_product_zipper_factor_count, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_mork_product_zipper_focus_factor, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_mork_product_zipper_path_indices, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_mork_product_zipper_reset, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_product_zipper_ascend, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_product_zipper_descend_byte, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_product_zipper_descend_index, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_product_zipper_descend_first, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_product_zipper_descend_last, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_product_zipper_descend_until, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_product_zipper_descend_until_max_bytes, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_product_zipper_ascend_until, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_product_zipper_ascend_until_branch, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_product_zipper_next_sibling_byte, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_product_zipper_prev_sibling_byte, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_product_zipper_next_step, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_product_zipper_next_val, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_overlay_zipper_new, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_overlay_zipper_close, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_overlay_zipper_path_exists, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_mork_overlay_zipper_is_val, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_mork_overlay_zipper_child_count, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_mork_overlay_zipper_path_bytes, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_mork_overlay_zipper_child_bytes, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_mork_overlay_zipper_depth, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_mork_overlay_zipper_reset, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_overlay_zipper_ascend, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_overlay_zipper_descend_byte, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_overlay_zipper_descend_index, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_overlay_zipper_descend_first, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_overlay_zipper_descend_last, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_overlay_zipper_descend_until, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_overlay_zipper_descend_until_max_bytes, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_overlay_zipper_ascend_until, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_overlay_zipper_ascend_until_branch, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_overlay_zipper_next_sibling_byte, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_overlay_zipper_prev_sibling_byte, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mork_overlay_zipper_next_step, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mm2_program_new, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mm2_program_clear, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mm2_program_add, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mm2_load_file, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mm2_program_size, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_mm2_program_atoms, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_mm2_context_new, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mm2_context_clear, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mm2_context_load_program, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mm2_context_add, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mm2_context_remove, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mm2_context_run, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mm2_context_step, CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) \
    X(lib_mm2_context_size, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(lib_mm2_context_atoms, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY)

static inline CettaGsltQueryEffect cetta_gslt_query_effect_join(
    CettaGsltQueryEffect left, CettaGsltQueryEffect right) {
    return (CettaGsltQueryEffect)((unsigned)left | (unsigned)right);
}

/* The analysis policy is generated with the effect algebra.  Native code
 * supplies the revision-pinned equation graph; uncertainty is never allowed
 * to become accelerator authority. */
#define CETTA_GSLT_USER_HEAD_EFFECT_REVISION_KEYED 1
#define CETTA_GSLT_USER_HEAD_EFFECT_LEAST_FIXED_POINT 1
#define CETTA_GSLT_QUERY_EFFECT_UNCERTAIN_HEAD     ((CettaGsltQueryEffect)(CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY | CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE))
#define CETTA_GSLT_QUERY_EFFECT_INERT_SYMBOL     CETTA_GSLT_QUERY_EFFECT_PURE

#define CETTA_GSLT_OPAQUE_EXPRESSION_HEAD_ROWS(X)     X(quote) \
    X(colon) \
    X(arrow)

static inline bool cetta_gslt_query_effect_children_opaque(
    SymbolId head, const BuiltinSyms *builtins) {
    return builtins &&
           (head == builtins->quote ||
           head == builtins->colon ||
           head == builtins->arrow);
}

/* Operands a head compares with a pattern and never evaluates, in the
 * language each row names. */
#define CETTA_GSLT_MATCHED_OPERAND_ROWS(X)     X(unify, 4u, 0u, he) \
    X(unify, 4u, 1u, he) \
    X(decons_atom, 1u, 0u, he) \
    X(cons_atom, 2u, 0u, he) \
    X(cons_atom, 2u, 1u, he) \
    X(error, 2u, 0u, he) \
    X(error, 2u, 1u, he)

static inline bool cetta_gslt_query_effect_operand_matched(
    SymbolId head, uint32_t arity, uint32_t operand_index,
    bool he_language, const BuiltinSyms *builtins) {
    return builtins &&
           ((he_language && head == builtins->unify && arity == 4u && operand_index == 0u) ||
           (he_language && head == builtins->unify && arity == 4u && operand_index == 1u) ||
           (he_language && head == builtins->decons_atom && arity == 1u && operand_index == 0u) ||
           (he_language && head == builtins->cons_atom && arity == 2u && operand_index == 0u) ||
           (he_language && head == builtins->cons_atom && arity == 2u && operand_index == 1u) ||
           (he_language && head == builtins->error && arity == 2u && operand_index == 0u) ||
           (he_language && head == builtins->error && arity == 2u && operand_index == 1u));
}

/* Operands a head reads as a list of (pattern value) pairs: a pattern is
 * matched and never evaluated; a value is evaluated where its pattern
 * matches, so its calls count. */
#define CETTA_GSLT_PATTERN_PAIR_OPERAND_ROWS(X)     X(case_text, 2u, 1u, all) \
    X(switch_text, 2u, 1u, all) \
    X(switch_minimal, 2u, 1u, all)

static inline bool cetta_gslt_query_effect_operand_pattern_pairs(
    SymbolId head, uint32_t arity, uint32_t operand_index,
    bool he_language, const BuiltinSyms *builtins) {
    (void)he_language;
    return builtins &&
           ((head == builtins->case_text && arity == 2u && operand_index == 1u) ||
           (head == builtins->switch_text && arity == 2u && operand_index == 1u) ||
           (head == builtins->switch_minimal && arity == 2u && operand_index == 1u));
}

#define CETTA_GSLT_TOTAL_INTEGER_HEAD_ROWS(X)     X(op_plus, 2u) \
    X(op_minus, 2u) \
    X(op_mul, 2u)

/* Default construction keeps these generated leaf classes arena-owned.
 * Explicit publication may still hash-cons them through hashcons_get. */
#define CETTA_GSLT_ARENA_OWNED_GROUNDED_KIND_ROWS(X)     X(GV_INT) \
    X(GV_FLOAT) \
    X(GV_BIGINT) \
    X(GV_RATIONAL)

/* Executable resource-leaf classifiers generated from resource-lifetime.
 * Atom constructors fold these judgments into compositional summary bits;
 * evaluators consume the summaries without recursively rescanning terms. */
#define CETTA_GSLT_IDENTITY_BEARING_GROUNDED_KIND_ROWS(X)     X(GV_SPACE) \
    X(GV_STATE) \
    X(GV_CAPTURE) \
    X(GV_FOREIGN) \
    X(GV_BINDINGS)

static inline bool cetta_gslt_identity_bearing_grounded_kind(
    GroundedKind kind) {
    switch (kind) {
    case GV_SPACE:
        return true;
    case GV_STATE:
        return true;
    case GV_CAPTURE:
        return true;
    case GV_FOREIGN:
        return true;
    case GV_BINDINGS:
        return true;
    default:
        return false;
    }
}

#define CETTA_GSLT_THREAD_LOCAL_RESOURCE_SYMBOL_ROWS(X)     X(capture)

static inline bool cetta_gslt_thread_local_resource_symbol(
    SymbolId symbol, const BuiltinSyms *builtins) {
    return builtins &&
           (symbol == builtins->capture);
}

typedef enum {
    CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER = 0,
    CETTA_GSLT_REGISTER_RESULT_BOOLEAN = 1,
} CettaGsltRegisterResultKind;

typedef enum {
    CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_ADD = 0,
    CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_SUBTRACT = 1,
    CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_MULTIPLY = 2,
    CETTA_GSLT_REGISTER_INSTRUCTION_ATOM_EQUAL = 3,
    CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_EQUAL = 4,
    CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_LESS = 5,
    CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_GREATER = 6,
    CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_LESS_EQUAL = 7,
    CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_GREATER_EQUAL = 8,
    CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_REMAINDER = 9,
    CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_FLOOR_MODULO = 10,
} CettaGsltRegisterInstruction;

typedef enum {
    CETTA_GSLT_REGISTER_OPERANDS_EXACT_INTEGER_OPERANDS = 0,
    CETTA_GSLT_REGISTER_OPERANDS_TYPED_STRUCTURAL_OPERANDS = 1,
} CettaGsltRegisterOperandDiscipline;

static inline bool cetta_gslt_register_operand_discipline(
    CettaGsltRegisterInstruction instruction,
    CettaGsltRegisterOperandDiscipline *discipline_out) {
    if (!discipline_out)
        return false;
    switch (instruction) {
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_ADD:
        *discipline_out = CETTA_GSLT_REGISTER_OPERANDS_EXACT_INTEGER_OPERANDS;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_SUBTRACT:
        *discipline_out = CETTA_GSLT_REGISTER_OPERANDS_EXACT_INTEGER_OPERANDS;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_MULTIPLY:
        *discipline_out = CETTA_GSLT_REGISTER_OPERANDS_EXACT_INTEGER_OPERANDS;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_ATOM_EQUAL:
        *discipline_out = CETTA_GSLT_REGISTER_OPERANDS_TYPED_STRUCTURAL_OPERANDS;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_EQUAL:
        *discipline_out = CETTA_GSLT_REGISTER_OPERANDS_EXACT_INTEGER_OPERANDS;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_LESS:
        *discipline_out = CETTA_GSLT_REGISTER_OPERANDS_EXACT_INTEGER_OPERANDS;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_GREATER:
        *discipline_out = CETTA_GSLT_REGISTER_OPERANDS_EXACT_INTEGER_OPERANDS;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_LESS_EQUAL:
        *discipline_out = CETTA_GSLT_REGISTER_OPERANDS_EXACT_INTEGER_OPERANDS;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_GREATER_EQUAL:
        *discipline_out = CETTA_GSLT_REGISTER_OPERANDS_EXACT_INTEGER_OPERANDS;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_REMAINDER:
        *discipline_out = CETTA_GSLT_REGISTER_OPERANDS_EXACT_INTEGER_OPERANDS;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_FLOOR_MODULO:
        *discipline_out = CETTA_GSLT_REGISTER_OPERANDS_EXACT_INTEGER_OPERANDS;
        return true;
    default:
        return false;
    }
}

#define CETTA_GSLT_REGISTER_HEAD_ROWS(X)     X(op_plus, 2u, CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER, CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_ADD) \
    X(op_minus, 2u, CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER, CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_SUBTRACT) \
    X(op_mul, 2u, CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER, CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_MULTIPLY) \
    X(op_eq, 2u, CETTA_GSLT_REGISTER_RESULT_BOOLEAN, CETTA_GSLT_REGISTER_INSTRUCTION_ATOM_EQUAL) \
    X(op_lt, 2u, CETTA_GSLT_REGISTER_RESULT_BOOLEAN, CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_LESS) \
    X(op_gt, 2u, CETTA_GSLT_REGISTER_RESULT_BOOLEAN, CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_GREATER) \
    X(op_le, 2u, CETTA_GSLT_REGISTER_RESULT_BOOLEAN, CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_LESS_EQUAL) \
    X(op_ge, 2u, CETTA_GSLT_REGISTER_RESULT_BOOLEAN, CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_GREATER_EQUAL) \
    X(numeric_eq, 2u, CETTA_GSLT_REGISTER_RESULT_BOOLEAN, CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_EQUAL)

/* Decode immutable operation metadata. Dialect availability, source authority
 * and operand readiness are checked independently by each consumer. */
static inline bool cetta_gslt_register_head_program(
    SymbolId head, CettaExprLen arity,
    CettaGsltRegisterResultKind *kind_out,
    CettaGsltRegisterInstruction *instruction_out,
    const BuiltinSyms *builtins) {
    if (!builtins || head == SYMBOL_ID_NONE)
        return false;
#define CETTA_REGISTER_HEAD(field, expected_arity, result_kind, instruction) \
    if (head == builtins->field && arity == (expected_arity)) { \
        if (kind_out) *kind_out = (result_kind); \
        if (instruction_out) *instruction_out = (instruction); \
        return true; \
    }
    CETTA_GSLT_REGISTER_HEAD_ROWS(CETTA_REGISTER_HEAD)
#undef CETTA_REGISTER_HEAD
    return false;
}

typedef enum {
    CETTA_GSLT_PREPARED_PURE_INTRINSIC_GROUNDED_DISPATCH = 0,
    CETTA_GSLT_PREPARED_PURE_INTRINSIC_DECONSTRUCT_NONEMPTY_EXPRESSION = 1,
    CETTA_GSLT_PREPARED_PURE_INTRINSIC_CONSTRUCT_EXPRESSION_CONS = 2,
    CETTA_GSLT_PREPARED_PURE_INTRINSIC_CONCATENATE_EXPRESSIONS = 3,
    CETTA_GSLT_PREPARED_PURE_INTRINSIC_LIBRARY_VALUE_DISPATCH = 4,
    CETTA_GSLT_PREPARED_PURE_INTRINSIC_BOOLEAN_DISPATCH = 5,
} CettaGsltPreparedPureIntrinsicInstruction;

typedef enum {
    CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL = 0,
    CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_SHORT_CIRCUIT = 1,
} CettaGsltPreparedPureIntrinsicOperandDiscipline;

typedef enum {
    CETTA_GSLT_INTRINSIC_DIALECT_HE = 1u,
    CETTA_GSLT_INTRINSIC_DIALECT_PETTA = 2u,
    CETTA_GSLT_INTRINSIC_DIALECT_PRIME = 4u,
    CETTA_GSLT_INTRINSIC_DIALECT_ALL = 7u,
} CettaGsltPreparedPureIntrinsicDialect;

/* X(head, arity, operand discipline, instruction, dialects): no dialect
 * reads two rows for one head and arity. */
#define CETTA_GSLT_PREPARED_PURE_INTRINSIC_HEAD_ROWS(X)     X(op_plus, 2u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_GROUNDED_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(op_minus, 2u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_GROUNDED_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(op_mul, 2u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_GROUNDED_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(op_lt, 2u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_GROUNDED_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(op_gt, 2u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_GROUNDED_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(op_le, 2u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_GROUNDED_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(op_ge, 2u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_GROUNDED_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(numeric_eq, 2u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_GROUNDED_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(op_div, 2u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_GROUNDED_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(op_and, 2u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_BOOLEAN_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_HE | CETTA_GSLT_INTRINSIC_DIALECT_PETTA) \
    X(op_and, 2u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_SHORT_CIRCUIT, CETTA_GSLT_PREPARED_PURE_INTRINSIC_BOOLEAN_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_PRIME) \
    X(op_or, 2u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_BOOLEAN_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_HE | CETTA_GSLT_INTRINSIC_DIALECT_PETTA) \
    X(op_or, 2u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_SHORT_CIRCUIT, CETTA_GSLT_PREPARED_PURE_INTRINSIC_BOOLEAN_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_PRIME) \
    X(op_not, 1u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_BOOLEAN_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(abs_math, 1u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_GROUNDED_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(log_math, 2u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_GROUNDED_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(car_atom, 1u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_GROUNDED_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(cdr_atom, 1u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_GROUNDED_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(size_atom, 1u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_GROUNDED_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(index_atom, 2u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_GROUNDED_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(unique_atom, 1u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_GROUNDED_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(intersection_atom, 2u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_GROUNDED_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_PETTA) \
    X(remove_all_atom, 2u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_GROUNDED_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(sort_atom, 1u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_GROUNDED_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(sort_numbers_atom, 1u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_GROUNDED_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(retain_top_k_keyed_atom, 2u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_GROUNDED_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(unkey_atom, 1u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_GROUNDED_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(range_atom, 1u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_GROUNDED_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(range_atom, 2u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_GROUNDED_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(lib_list_range, 1u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_GROUNDED_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(lib_list_range, 2u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_GROUNDED_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(decons_atom, 1u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_DECONSTRUCT_NONEMPTY_EXPRESSION, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(cons_atom, 2u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_CONSTRUCT_EXPRESSION_CONS, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(union_atom, 2u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_CONCATENATE_EXPRESSIONS, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(lib_json_parse_v1, 1u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_LIBRARY_VALUE_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(lib_json_parse_v1, 2u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_LIBRARY_VALUE_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(lib_json_to_legacy_v1, 1u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_LIBRARY_VALUE_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(lib_json_from_inspection_v1, 1u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_LIBRARY_VALUE_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(lib_json_parse, 1u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_LIBRARY_VALUE_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(lib_json_parse, 2u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_LIBRARY_VALUE_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(lib_json_project_v1, 2u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_LIBRARY_VALUE_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(lib_json_stringify_v1, 1u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_LIBRARY_VALUE_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(lib_json_stringify_v1, 2u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_LIBRARY_VALUE_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(lib_json_stringify, 1u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_LIBRARY_VALUE_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(lib_json_stringify, 2u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_LIBRARY_VALUE_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(lib_json_members_v1, 1u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_LIBRARY_VALUE_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(lib_json_members_v1, 2u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_LIBRARY_VALUE_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(lib_json_get_all_v1, 2u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_LIBRARY_VALUE_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(lib_json_get_all_v1, 3u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_LIBRARY_VALUE_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(lib_json_get_first_v1, 2u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_LIBRARY_VALUE_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(lib_json_get_first_v1, 3u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_LIBRARY_VALUE_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(lib_json_get_last_v1, 2u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_LIBRARY_VALUE_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(lib_json_get_last_v1, 3u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_LIBRARY_VALUE_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(lib_json_object_get, 2u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_LIBRARY_VALUE_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(lib_str, 1u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_LIBRARY_VALUE_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(lib_str, 2u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_LIBRARY_VALUE_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(lib_str, 3u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_LIBRARY_VALUE_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(lib_str, 4u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_LIBRARY_VALUE_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL) \
    X(lib_str_value, 2u, CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL, CETTA_GSLT_PREPARED_PURE_INTRINSIC_LIBRARY_VALUE_DISPATCH, CETTA_GSLT_INTRINSIC_DIALECT_ALL)

/* X(head, arity, absorbing truth value) for every short-circuit row. */
#define CETTA_GSLT_PREPARED_PURE_SHORT_CIRCUIT_ROWS(X) \
    X(op_and, 2u, false) \
    X(op_or, 2u, true)

/* The row that `dialect`, one CETTA_GSLT_INTRINSIC_DIALECT_* bit, reads for
 * `head` at `arity`: its operand discipline and instruction.  A row scoped
 * to every dialect holds in every language.  This is operation metadata,
 * not source authority or permission to speculate. */
static inline bool cetta_gslt_prepared_intrinsic_program(
    SymbolId head, CettaExprLen arity, uint32_t dialect,
    CettaGsltPreparedPureIntrinsicOperandDiscipline *discipline_out,
    CettaGsltPreparedPureIntrinsicInstruction *instruction_out,
    const BuiltinSyms *builtins) {
    if (!builtins || head == SYMBOL_ID_NONE)
        return false;
#define CETTA_INTRINSIC_HEAD(field, expected_arity, discipline, instruction, \
                             dialects) \
    if (head == builtins->field && arity == (expected_arity) && \
        ((dialects) == CETTA_GSLT_INTRINSIC_DIALECT_ALL || \
         ((dialects) & dialect) != 0u)) { \
        if (discipline_out) *discipline_out = (discipline); \
        if (instruction_out) *instruction_out = (instruction); \
        return true; \
    }
    CETTA_GSLT_PREPARED_PURE_INTRINSIC_HEAD_ROWS(CETTA_INTRINSIC_HEAD)
#undef CETTA_INTRINSIC_HEAD
    return false;
}

#define CETTA_GSLT_SEQUENCE_VALUE_CONSTRUCTOR_ROWS(X)     X(range_atom, 6u, 0u) \
    X(lib_list_range, 6u, 0u) \
    X(repeat_atom, 4u, 2u) \
    X(lib_list_repeat, 4u, 2u)

typedef enum {
    CETTA_GSLT_FOLD_CONTROL_BIND = 0,
    CETTA_GSLT_FOLD_CONTROL_BRANCH = 1,
    CETTA_GSLT_FOLD_CONTROL_EVALUATE = 2,
    CETTA_GSLT_FOLD_CONTROL_BIND_SEQUENCE = 3,
} CettaGsltFoldControl;

typedef enum {
    CETTA_GSLT_CONTROL_OPERAND_EVALUATED = 0,
    CETTA_GSLT_CONTROL_OPERAND_PATTERN = 1,
    CETTA_GSLT_CONTROL_OPERAND_PATTERN_PAIRS = 2,
} CettaGsltControlOperandRole;

static inline bool cetta_gslt_fold_control_operand_role(
    CettaGsltFoldControl control, uint32_t operand_index,
    CettaGsltControlOperandRole *role_out) {
    if (!role_out)
        return false;
    switch (control) {
    case CETTA_GSLT_FOLD_CONTROL_BIND:
        switch (operand_index) {
        case 0u:
            *role_out = CETTA_GSLT_CONTROL_OPERAND_PATTERN;
            return true;
        case 1u:
            *role_out = CETTA_GSLT_CONTROL_OPERAND_EVALUATED;
            return true;
        case 2u:
            *role_out = CETTA_GSLT_CONTROL_OPERAND_EVALUATED;
            return true;
        default:
            return false;
        }
    case CETTA_GSLT_FOLD_CONTROL_BRANCH:
        switch (operand_index) {
        case 0u:
            *role_out = CETTA_GSLT_CONTROL_OPERAND_EVALUATED;
            return true;
        case 1u:
            *role_out = CETTA_GSLT_CONTROL_OPERAND_EVALUATED;
            return true;
        case 2u:
            *role_out = CETTA_GSLT_CONTROL_OPERAND_EVALUATED;
            return true;
        default:
            return false;
        }
    case CETTA_GSLT_FOLD_CONTROL_EVALUATE:
        switch (operand_index) {
        case 0u:
            *role_out = CETTA_GSLT_CONTROL_OPERAND_EVALUATED;
            return true;
        default:
            return false;
        }
    case CETTA_GSLT_FOLD_CONTROL_BIND_SEQUENCE:
        switch (operand_index) {
        case 0u:
            *role_out = CETTA_GSLT_CONTROL_OPERAND_PATTERN_PAIRS;
            return true;
        case 1u:
            *role_out = CETTA_GSLT_CONTROL_OPERAND_EVALUATED;
            return true;
        default:
            return false;
        }
    default:
        return false;
    }
}

typedef enum {
    CETTA_GSLT_PURE_CALL_EAGER = 0,
    CETTA_GSLT_PURE_CALL_CALL_BY_NEED = 1,
} CettaGsltPureCallMode;

typedef enum {
    CETTA_GSLT_PATTERN_VARIABLE = 0,
    CETTA_GSLT_PATTERN_ATOM = 1,
    CETTA_GSLT_PATTERN_EXPRESSION = 2,
} CettaGsltPatternKind;

/* Executable unique-match evidence generated from the pure-call component.
 * The runtime projects representation facts into this judgment; uncertainty
 * (including differences below a shared constructor) is deliberately not
 * evidence of determinacy. */
static inline bool cetta_gslt_pure_call_whnf_disjoint(
    CettaGsltPatternKind left_kind,
    CettaGsltPatternKind right_kind,
    bool atoms_equal,
    uint64_t left_arity,
    uint64_t right_arity,
    bool left_head_rigid,
    bool right_head_rigid,
    bool heads_equal) {
    if (left_kind == CETTA_GSLT_PATTERN_VARIABLE ||
        right_kind == CETTA_GSLT_PATTERN_VARIABLE)
        return false;
    if (left_kind != right_kind)
        return true;
    if (left_kind == CETTA_GSLT_PATTERN_ATOM)
        return !atoms_equal;
    if (left_kind != CETTA_GSLT_PATTERN_EXPRESSION)
        return false;
    if (left_arity != right_arity)
        return true;
    return left_arity > 0u && left_head_rigid &&
           right_head_rigid && !heads_equal;
}

/* Source control plans carry only operation and operand coordinates. Source
 * authority, lexical owners, demand and result completion remain independent.
 * Absence is metadata, never an ordinary Empty value. */
typedef enum {
    CETTA_CONTROL_NONE = 0,
    CETTA_CONTROL_SEQUENCE = 1,
    CETTA_CONTROL_BRANCH = 2,
    CETTA_CONTROL_SEQUENCE_MANY = 3,
    CETTA_CONTROL_FORCE = 4,
    CETTA_CONTROL_RETURN_SCOPE = 5,
    CETTA_CONTROL_ORDERED_CHOICE = 6,
    CETTA_CONTROL_FAILURE = 7,
    CETTA_CONTROL_SCOPED_COMMIT = 8,
    CETTA_CONTROL_HANDLE = 9,
    CETTA_CONTROL_RETURN_VALUE = 10,
    CETTA_CONTROL_CONSTRUCT = 11,
    CETTA_CONTROL_INVOKE = 12,
    CETTA_CONTROL_SEQUENCE_FIRST = 13,
    CETTA_CONTROL_SEQUENCE_LAST = 14,
    CETTA_CONTROL_COLLECT = 15,
    CETTA_CONTROL_MATCH_BRANCHES = 16,
} CettaControlOperation;

#define CETTA_CONTROL_OPERAND_ABSENT UINT8_MAX
typedef struct {
    uint8_t operation;
    uint8_t source;
    uint8_t first;
    uint8_t last;
} CettaControlOperationPlan;
_Static_assert(sizeof(CettaControlOperationPlan) == 4u, "control plan must remain four bytes");

#define CETTA_CONTROL_RETURN_VALUE_PLAN ((CettaControlOperationPlan){CETTA_CONTROL_RETURN_VALUE, CETTA_CONTROL_OPERAND_ABSENT, CETTA_CONTROL_OPERAND_ABSENT, CETTA_CONTROL_OPERAND_ABSENT})
#define CETTA_CONTROL_CONSTRUCT_PLAN ((CettaControlOperationPlan){CETTA_CONTROL_CONSTRUCT, CETTA_CONTROL_OPERAND_ABSENT, CETTA_CONTROL_OPERAND_ABSENT, CETTA_CONTROL_OPERAND_ABSENT})
#define CETTA_CONTROL_INVOKE_PLAN ((CettaControlOperationPlan){CETTA_CONTROL_INVOKE, CETTA_CONTROL_OPERAND_ABSENT, CETTA_CONTROL_OPERAND_ABSENT, CETTA_CONTROL_OPERAND_ABSENT})

/* Control-form plans are distinct from occurrence-level value,
 * construction and invocation plans. This grants no callee admission. */
static inline bool cetta_control_operation_is_control_form(CettaControlOperationPlan plan) {
    switch (plan.operation) {
    case CETTA_CONTROL_SEQUENCE:
    case CETTA_CONTROL_BRANCH:
    case CETTA_CONTROL_SEQUENCE_MANY:
    case CETTA_CONTROL_FORCE:
    case CETTA_CONTROL_RETURN_SCOPE:
    case CETTA_CONTROL_ORDERED_CHOICE:
    case CETTA_CONTROL_FAILURE:
    case CETTA_CONTROL_SCOPED_COMMIT:
    case CETTA_CONTROL_HANDLE:
    case CETTA_CONTROL_SEQUENCE_FIRST:
    case CETTA_CONTROL_SEQUENCE_LAST:
    case CETTA_CONTROL_COLLECT:
    case CETTA_CONTROL_MATCH_BRANCHES:
        return true;
    default:
        return false;
    }
}

/* The two three-operand sequence layouts have different source positions.
 * Physical lowerings can admit one layout without changing the operation. */
static inline bool cetta_control_operation_binding_layout(
    CettaControlOperationPlan plan, uint8_t producer_index) {
    return producer_index <= 1u && plan.operation == CETTA_CONTROL_SEQUENCE &&
        plan.source == producer_index && plan.first == 1u - producer_index &&
        plan.last == 2u;
}

/* Match selection retains two distinct source occurrences. */
static inline bool cetta_control_operation_match_branches_layout(
        CettaControlOperationPlan plan, CettaExprLen arity) {
    return plan.operation == CETTA_CONTROL_MATCH_BRANCHES &&
        plan.source < arity && plan.first < arity &&
        plan.source != plan.first && plan.last == CETTA_CONTROL_OPERAND_ABSENT;
}

/* Variadic series select a result without bounding their argument count.
 * Zero denotes absent metadata, never a source argument coordinate. */
static inline CettaExprIndex cetta_control_operation_series_result(
        CettaControlOperationPlan plan, CettaExprLen arity) {
    if (!arity) return 0u;
    if (plan.operation == CETTA_CONTROL_SEQUENCE_FIRST) return 1u;
    if (plan.operation == CETTA_CONTROL_SEQUENCE_LAST) return arity;
    return 0u;
}

/* Execution order is selected by admitted source policy. The same program
 * lowers to ordinary goals or activation goals without rereading syntax. */
typedef enum {
    CETTA_SEQUENCE_SOURCE_FIRST = 0,
    CETTA_SEQUENCE_BINDER_FIRST = 1,
} CettaSequenceOrder;

typedef struct { uint8_t children[3]; } CettaSequenceOrderPlan;

/* A preparation plan selects stages, independently of values or ownership.
 * Type installation and completed-value checking retain distinct positions. */
typedef enum {
    CETTA_CALL_PREP_INSTALL_TYPE = 1u << 0u,
    CETTA_CALL_PREP_COMPUTE = 1u << 1u,
    CETTA_CALL_PREP_CHECK_RETURN = 1u << 2u,
    CETTA_CALL_PREP_CHECK_TYPE = 1u << 3u,
    CETTA_CALL_PREP_BIND = 1u << 4u,
} CettaCallPreparationStep;
typedef uint8_t CettaCallPreparationPlan;
_Static_assert(sizeof(CettaCallPreparationPlan) == 1u, "preparation plan must remain one byte");

static inline bool cetta_call_preparation_valid(CettaCallPreparationPlan plan) {
    return (plan & (uint8_t)~31u) == 0u;
}

#define CETTA_CALL_PREPARATION_REVERSE_ROWS(X)     X(CETTA_CALL_PREP_BIND) \
    X(CETTA_CALL_PREP_CHECK_TYPE) \
    X(CETTA_CALL_PREP_CHECK_RETURN) \
    X(CETTA_CALL_PREP_COMPUTE) \
    X(CETTA_CALL_PREP_INSTALL_TYPE)

/* Argument preparation and body entry are independent of callee admission. */
typedef enum {
    CETTA_INVOCATION_NONE = 0,
    CETTA_INVOCATION_HOST = 1,
    CETTA_INVOCATION_PREDICATE_VALUE = 2,
    CETTA_INVOCATION_RELATIONAL_MEMBER = 3,
    CETTA_INVOCATION_LIST_UNARY = 4,
    CETTA_INVOCATION_LIST_SET = 5,
    CETTA_INVOCATION_READ_TERM = 6,
    CETTA_INVOCATION_EXTENSION = 7,
    CETTA_INVOCATION_PREDICATE_CALL = 8,
} CettaInvocationOperation;

typedef struct { uint8_t operation, first, last; } CettaInvocationPlan;
_Static_assert(sizeof(CettaInvocationPlan) == 3u, "invocation plan must remain three bytes");

/* One computation carries its control operation and optional native body.
 * Source authority, live demand and ownership are supplied by its context. */
typedef struct {
    CettaControlOperationPlan control;
    CettaInvocationPlan invocation;
} CettaComputationPlan;
_Static_assert(sizeof(CettaComputationPlan) == 7u, "computation plan must remain seven bytes");

static inline uint32_t cetta_invocation_plan_key(CettaInvocationPlan plan) {
    return (uint32_t)plan.operation | (uint32_t)plan.first << 8u |
        (uint32_t)plan.last << 16u;
}

#define CETTA_INVOCATION_HEAD_ROWS(X)     X(repr, 1u, CETTA_INVOCATION_HOST, 0u, 1u) \
    X(petta_member, 2u, CETTA_INVOCATION_RELATIONAL_MEMBER, 0u, 2u) \
    X(car_atom, 1u, CETTA_INVOCATION_LIST_UNARY, 0u, 1u) \
    X(cdr_atom, 1u, CETTA_INVOCATION_LIST_UNARY, 0u, 1u) \
    X(decons_atom, 1u, CETTA_INVOCATION_LIST_UNARY, 0u, 1u) \
    X(petta_decons, 1u, CETTA_INVOCATION_LIST_UNARY, 0u, 1u) \
    X(petta_last, 1u, CETTA_INVOCATION_LIST_UNARY, 0u, 1u) \
    X(reverse, 1u, CETTA_INVOCATION_LIST_UNARY, 0u, 1u) \
    X(petta_msort, 1u, CETTA_INVOCATION_LIST_UNARY, 0u, 1u) \
    X(sort_atom, 1u, CETTA_INVOCATION_LIST_UNARY, 0u, 1u)

#define CETTA_PETTA_INVOCATION_FORM_ROWS(X)     X(PETTA_FORM_PREDICATE, 1u, CETTA_INVOCATION_PREDICATE_VALUE, 0u, 1u) \
    X(PETTA_FORM_LIST_TO_SET, 1u, CETTA_INVOCATION_LIST_SET, 0u, 1u) \
    X(PETTA_FORM_ALPHA_UNIQUE, 1u, CETTA_INVOCATION_LIST_SET, 0u, 1u) \
    X(PETTA_FORM_EXCLUDE_ITEM, 2u, CETTA_INVOCATION_LIST_SET, 0u, 2u) \
    X(PETTA_FORM_SREAD, 1u, CETTA_INVOCATION_READ_TERM, 0u, 1u) \
    X(PETTA_FORM_IMPORT_PROLOG_FUNCTION, 1u, CETTA_INVOCATION_EXTENSION, 0u, 1u) \
    X(PETTA_FORM_CALL_PREDICATE, 1u, CETTA_INVOCATION_PREDICATE_CALL, 0u, 1u) \
    X(PETTA_FORM_ASSERTA_PREDICATE, 1u, CETTA_INVOCATION_PREDICATE_CALL, 0u, 1u) \
    X(PETTA_FORM_ASSERTZ_PREDICATE, 1u, CETTA_INVOCATION_PREDICATE_CALL, 0u, 1u) \
    X(PETTA_FORM_RETRACT_PREDICATE, 1u, CETTA_INVOCATION_PREDICATE_CALL, 0u, 1u)

/* Refusal neither reads operands nor changes the supplied plan. */
static inline bool cetta_invocation_plan_decode(
    SymbolId head, CettaExprLen arity, CettaInvocationPlan *out,
    const BuiltinSyms *builtins) {
    if (!builtins || head == SYMBOL_ID_NONE)
        return false;
#define CETTA_INVOCATION_HEAD(field, count, body, start, end) \
    if (head == builtins->field && arity == (count)) { \
        if (out) *out = (CettaInvocationPlan){(body), (start), (end)}; \
        return true; \
    }
    CETTA_INVOCATION_HEAD_ROWS(CETTA_INVOCATION_HEAD)
#undef CETTA_INVOCATION_HEAD
    return false;
}

static inline bool cetta_control_sequence_plan(CettaControlOperationPlan plan,
        CettaSequenceOrder order, CettaSequenceOrderPlan *out) {
    if (!out || !cetta_control_operation_binding_layout(plan, plan.source))
        return false;
    CettaSequenceOrderPlan result;
    switch (order) {
    case CETTA_SEQUENCE_SOURCE_FIRST:
        result = (CettaSequenceOrderPlan){.children = {plan.source, plan.first, plan.last}}; break;
    case CETTA_SEQUENCE_BINDER_FIRST:
        result = (CettaSequenceOrderPlan){.children = {plan.first, plan.source, plan.last}}; break;
    default: return false;
    }
    *out = result;
    return true;
}

static inline uint32_t cetta_control_operation_key(CettaControlOperationPlan plan) {
    return (uint32_t)plan.operation | (uint32_t)plan.source << 8u |
        (uint32_t)plan.first << 16u | (uint32_t)plan.last << 24u;
}

#define CETTA_CONTROL_PLAN_HEAD_ROWS(X)     X(let, 3u, CETTA_CONTROL_SEQUENCE, 1u, 0u, 2u) \
    X(chain, 3u, CETTA_CONTROL_SEQUENCE, 0u, 1u, 2u) \
    X(if_text, 2u, CETTA_CONTROL_BRANCH, 0u, 1u, CETTA_CONTROL_OPERAND_ABSENT) \
    X(if_text, 3u, CETTA_CONTROL_BRANCH, 0u, 1u, 2u) \
    X(let_star, 2u, CETTA_CONTROL_SEQUENCE_MANY, 0u, CETTA_CONTROL_OPERAND_ABSENT, 1u) \
    X(eval, 1u, CETTA_CONTROL_FORCE, 0u, CETTA_CONTROL_OPERAND_ABSENT, CETTA_CONTROL_OPERAND_ABSENT) \
    X(function, 1u, CETTA_CONTROL_RETURN_SCOPE, 0u, CETTA_CONTROL_OPERAND_ABSENT, CETTA_CONTROL_OPERAND_ABSENT) \
    X(superpose, 1u, CETTA_CONTROL_ORDERED_CHOICE, 0u, CETTA_CONTROL_OPERAND_ABSENT, CETTA_CONTROL_OPERAND_ABSENT) \
    X(collapse, 1u, CETTA_CONTROL_COLLECT, 0u, CETTA_CONTROL_OPERAND_ABSENT, CETTA_CONTROL_OPERAND_ABSENT) \
    X(case_text, 2u, CETTA_CONTROL_MATCH_BRANCHES, 0u, 1u, CETTA_CONTROL_OPERAND_ABSENT) \
    X(empty_form, 0u, CETTA_CONTROL_FAILURE, CETTA_CONTROL_OPERAND_ABSENT, CETTA_CONTROL_OPERAND_ABSENT, CETTA_CONTROL_OPERAND_ABSENT)

/* Dialect classification admits these forms before reading their operands.
 * The enum names are expanded only by the owning dialect adapter. */
#define CETTA_PETTA_CONTROL_FORM_ROWS(X)     X(PETTA_FORM_CUT, 0u, CETTA_CONTROL_SCOPED_COMMIT, CETTA_CONTROL_OPERAND_ABSENT, CETTA_CONTROL_OPERAND_ABSENT, CETTA_CONTROL_OPERAND_ABSENT) \
    X(PETTA_FORM_CATCH, 1u, CETTA_CONTROL_HANDLE, 0u, CETTA_CONTROL_OPERAND_ABSENT, CETTA_CONTROL_OPERAND_ABSENT)

#define CETTA_PETTA_VARIADIC_FORM_ROWS(X) X(PETTA_FORM_PROG1, CETTA_CONTROL_SEQUENCE_FIRST) X(PETTA_FORM_PROGN, CETTA_CONTROL_SEQUENCE_LAST)

/* Arity admission performs no operand read. Refusal leaves the output intact. */
static inline bool cetta_control_operation_decode(
    SymbolId head, CettaExprLen arity, CettaControlOperationPlan *plan_out,
    const BuiltinSyms *builtins) {
    if (!builtins || head == SYMBOL_ID_NONE)
        return false;
#define CETTA_CONTROL_HEAD(field, expected_arity, op, source_arg, first_arg, last_arg) \
    if (head == builtins->field && arity == (expected_arity)) { \
        if (plan_out) *plan_out = (CettaControlOperationPlan){ \
            .operation = (op), .source = (source_arg), \
            .first = (first_arg), .last = (last_arg)}; \
        return true; \
    }
    CETTA_CONTROL_PLAN_HEAD_ROWS(CETTA_CONTROL_HEAD)
#undef CETTA_CONTROL_HEAD
    return false;
}

/* This lowering is used only after the caller admits the pure fold fragment.
 * Its direct body entry does not erase a general function's return scope. */
static inline bool cetta_control_operation_lower_fold(
    CettaControlOperationPlan plan, CettaGsltFoldControl *control_out) {
    if (plan.operation == CETTA_CONTROL_SEQUENCE &&
        plan.source == 1u &&
        plan.first == 0u &&
        plan.last == 2u) {
        if (control_out) *control_out = CETTA_GSLT_FOLD_CONTROL_BIND;
        return true;
    }
    if (plan.operation == CETTA_CONTROL_BRANCH &&
        plan.source == 0u &&
        plan.first == 1u &&
        plan.last == 2u) {
        if (control_out) *control_out = CETTA_GSLT_FOLD_CONTROL_BRANCH;
        return true;
    }
    if (plan.operation == CETTA_CONTROL_SEQUENCE_MANY &&
        plan.source == 0u &&
        plan.first == CETTA_CONTROL_OPERAND_ABSENT &&
        plan.last == 1u) {
        if (control_out) *control_out = CETTA_GSLT_FOLD_CONTROL_BIND_SEQUENCE;
        return true;
    }
    if (plan.operation == CETTA_CONTROL_FORCE &&
        plan.source == 0u &&
        plan.first == CETTA_CONTROL_OPERAND_ABSENT &&
        plan.last == CETTA_CONTROL_OPERAND_ABSENT) {
        if (control_out) *control_out = CETTA_GSLT_FOLD_CONTROL_EVALUATE;
        return true;
    }
    if (plan.operation == CETTA_CONTROL_RETURN_SCOPE &&
        plan.source == 0u &&
        plan.first == CETTA_CONTROL_OPERAND_ABSENT &&
        plan.last == CETTA_CONTROL_OPERAND_ABSENT) {
        if (control_out) *control_out = CETTA_GSLT_FOLD_CONTROL_EVALUATE;
        return true;
    }
    return false;
}

/* The physical fold instruction inventory is derived from the same plans. */
#define CETTA_GSLT_FOLD_CONTROL_HEAD_ROWS(X)     X(let, 3u, CETTA_GSLT_FOLD_CONTROL_BIND) \
    X(if_text, 3u, CETTA_GSLT_FOLD_CONTROL_BRANCH) \
    X(let_star, 2u, CETTA_GSLT_FOLD_CONTROL_BIND_SEQUENCE) \
    X(eval, 1u, CETTA_GSLT_FOLD_CONTROL_EVALUATE) \
    X(function, 1u, CETTA_GSLT_FOLD_CONTROL_EVALUATE)

static inline bool cetta_gslt_fold_control_program(
    SymbolId head, CettaExprLen arity, CettaGsltFoldControl *control_out,
    const BuiltinSyms *builtins) {
    CettaControlOperationPlan plan;
    return cetta_control_operation_decode(head, arity, &plan, builtins) &&
        cetta_control_operation_lower_fold(plan, control_out);
}

/* Representation rows for sequence consumers.  The runtime resolves these
 * names in the active symbol table; library constructors therefore remain
 * authored data rather than evaluator vocabulary. */
#define CETTA_GSLT_SEQUENCE_REPRESENTATION_ROWS(X)     X("clist", "clist:to-list", "ClistNil", "ClistCons")

/* A represented operand remains source syntax until its sequence consumer
 * exposes the generated fold handler.  This prevents eager pure-call
 * compilation from materializing a representation that the handler can erase
 * by an observer-commuting homomorphism. */
#define CETTA_GSLT_SEQUENCE_FOLD_CONSUMER_ROWS(X)     X("clist", "clist:foldl", 5u, 0u)

typedef enum {
    CETTA_GSLT_SEQUENCE_ERASURE_UNWRAP = 0,
    CETTA_GSLT_SEQUENCE_ERASURE_RECURSIVE_ALL = 1,
    CETTA_GSLT_SEQUENCE_ERASURE_SCALAR_ALL = 2,
} CettaGsltSequenceErasureMode;

/* Canonical fold target used only after a prepared sequence path declines. */
#define CETTA_GSLT_SEQUENCE_FOLD_TARGET_ROWS(X)     X("clist", "list:foldl")

/* Observer-commuting erasures from authored sequence producers to the flat
 * free-monoid representation consumed by the prepared fold. */
#define CETTA_GSLT_SEQUENCE_ERASURE_ROWS(X)     X("clist", "clist:from-list", "identity", CETTA_GSLT_SEQUENCE_ERASURE_UNWRAP, 1u, 1u) \
    X("clist", "clist:append", "list:append", CETTA_GSLT_SEQUENCE_ERASURE_RECURSIVE_ALL, 2u, 2u) \
    X("clist", "clist:range", "list:range", CETTA_GSLT_SEQUENCE_ERASURE_SCALAR_ALL, 1u, 2u) \
    X("clist", "clist:repeat", "list:repeat", CETTA_GSLT_SEQUENCE_ERASURE_SCALAR_ALL, 2u, 2u)

/* ── Evaluator root frames: generated evacuation arms ─────────────────────
 * Frame kinds and their field traversal sequences come from the
 * presentation.  The collector core defines ONE visitor macro per root
 * field kind and stays language-ignorant:
 *   CETTA_GC_VISIT_STRONG_ATOM_SLOT(SESSION, SLOT_PTR, FAIL)
 *   CETTA_GC_VISIT_STRONG_ATOM_SPAN(SESSION, SPAN_PTR, FAIL)
 *   CETTA_GC_VISIT_LOGICAL_BINDINGS(SESSION, ENV_PTR, FAIL)
 *   CETTA_GC_VISIT_OUTCOME_SET(SESSION, OS_PTR, FAIL)
 *   CETTA_GC_VISIT_EPHEMERON_ATOM_MAP(SESSION, MAP, FAIL)
 * A spec field kind without a core visitor macro fails to compile; an
 * omitted arm is exercised by the skip-arm negative gate. */
typedef enum {
    CETTA_EVAL_GC_FRAME_LEXICAL = 0,
    CETTA_EVAL_GC_FRAME_OUTCOME_SET = 1,
    CETTA_EVAL_GC_FRAME_OUTCOME_CONTINUATION = 2,
    CETTA_EVAL_GC_FRAME_EVALUATION_RETRY = 3,
    CETTA_EVAL_GC_FRAME_TYPE_CAST_CONTINUATION = 4,
    CETTA_EVAL_GC_FRAME_LET_BRANCH_CONTINUATION = 5,
    CETTA_EVAL_GC_FRAME_CASE_BRANCH_CONTINUATION = 6,
    CETTA_EVAL_GC_FRAME_STATIC_BRANCH_WALK = 7,
    CETTA_EVAL_GC_FRAME_FUNCTION_ARGS = 8,
    CETTA_EVAL_GC_FRAME_FUNCTION_ARGS_MACHINE = 9,
    CETTA_EVAL_GC_FRAME_TYPED_APPLICATION_CONTINUATION = 10,
    CETTA_EVAL_GC_FRAME_OBSERVATION_NORMALIZATION = 11,
    CETTA_EVAL_GC_FRAME_TUPLE_FRAME = 12,
    CETTA_EVAL_GC_FRAME_TUPLE_MACHINE = 13,
    CETTA_EVAL_GC_FRAME_PRIME_TASK = 14,
    CETTA_EVAL_GC_FRAME_PRIME_FRAME = 15,
    CETTA_EVAL_GC_FRAME_PRIME_DRIVER = 16,
    CETTA_EVAL_GC_FRAME_PREPARED_PURE_MACHINE = 17,
    CETTA_EVAL_GC_FRAME_KIND_COUNT
} CettaEvalGcFrameKind;

#define CETTA_EVAL_GC_FRAME_KIND_ROWS(X)     X(lexical, LEXICAL) \
    X(outcome_set, OUTCOME_SET) \
    X(outcome_continuation, OUTCOME_CONTINUATION) \
    X(evaluation_retry, EVALUATION_RETRY) \
    X(type_cast_continuation, TYPE_CAST_CONTINUATION) \
    X(let_branch_continuation, LET_BRANCH_CONTINUATION) \
    X(case_branch_continuation, CASE_BRANCH_CONTINUATION) \
    X(static_branch_walk, STATIC_BRANCH_WALK) \
    X(function_args, FUNCTION_ARGS) \
    X(function_args_machine, FUNCTION_ARGS_MACHINE) \
    X(typed_application_continuation, TYPED_APPLICATION_CONTINUATION) \
    X(observation_normalization, OBSERVATION_NORMALIZATION) \
    X(tuple_frame, TUPLE_FRAME) \
    X(tuple_machine, TUPLE_MACHINE) \
    X(prime_task, PRIME_TASK) \
    X(prime_frame, PRIME_FRAME) \
    X(prime_driver, PRIME_DRIVER) \
    X(prepared_pure_machine, PREPARED_PURE_MACHINE)

/* Expanding these rows with C type emitters produces the root payload
 * structures themselves.  The presentation therefore owns both layout and
 * traversal order; the collector supplies only the representation
 * types and their language-ignorant visitors. */
#define CETTA_EVAL_GC_FRAME_FIELDS_lexical(\
    STRONG_ATOM_SLOT, STRONG_ATOM_SPAN, \
    LOGICAL_BINDINGS, OUTCOME_SET, VARIANT_INSTANCE, \
    EPHEMERON_ATOM_MAP) \
    STRONG_ATOM_SLOT(atom_io) \
    LOGICAL_BINDINGS(env) \
    STRONG_ATOM_SLOT(etype_io)

#define CETTA_EVAL_GC_FRAME_FIELDS_outcome_set(\
    STRONG_ATOM_SLOT, STRONG_ATOM_SPAN, \
    LOGICAL_BINDINGS, OUTCOME_SET, VARIANT_INSTANCE, \
    EPHEMERON_ATOM_MAP) \
    OUTCOME_SET(outcomes)

#define CETTA_EVAL_GC_FRAME_FIELDS_outcome_continuation(\
    STRONG_ATOM_SLOT, STRONG_ATOM_SPAN, \
    LOGICAL_BINDINGS, OUTCOME_SET, VARIANT_INSTANCE, \
    EPHEMERON_ATOM_MAP) \
    STRONG_ATOM_SPAN(live_atoms) \
    OUTCOME_SET(child_outcomes) \
    OUTCOME_SET(parent_outcomes)

#define CETTA_EVAL_GC_FRAME_FIELDS_evaluation_retry(\
    STRONG_ATOM_SLOT, STRONG_ATOM_SPAN, \
    LOGICAL_BINDINGS, OUTCOME_SET, VARIANT_INSTANCE, \
    EPHEMERON_ATOM_MAP) \
    STRONG_ATOM_SLOT(input)

#define CETTA_EVAL_GC_FRAME_FIELDS_type_cast_continuation(\
    STRONG_ATOM_SLOT, STRONG_ATOM_SPAN, \
    LOGICAL_BINDINGS, OUTCOME_SET, VARIANT_INSTANCE, \
    EPHEMERON_ATOM_MAP) \
    STRONG_ATOM_SLOT(subject) \
    STRONG_ATOM_SLOT(expected_type)

#define CETTA_EVAL_GC_FRAME_FIELDS_let_branch_continuation(\
    STRONG_ATOM_SLOT, STRONG_ATOM_SPAN, \
    LOGICAL_BINDINGS, OUTCOME_SET, VARIANT_INSTANCE, \
    EPHEMERON_ATOM_MAP) \
    STRONG_ATOM_SLOT(canonical) \
    STRONG_ATOM_SLOT(pattern) \
    STRONG_ATOM_SLOT(source) \
    STRONG_ATOM_SLOT(scoped_body) \
    STRONG_ATOM_SPAN(fresh) \
    LOGICAL_BINDINGS(prefix) \
    OUTCOME_SET(source_outcomes) \
    OUTCOME_SET(parent_outcomes)

#define CETTA_EVAL_GC_FRAME_FIELDS_case_branch_continuation(\
    STRONG_ATOM_SLOT, STRONG_ATOM_SPAN, \
    LOGICAL_BINDINGS, OUTCOME_SET, VARIANT_INSTANCE, \
    EPHEMERON_ATOM_MAP) \
    STRONG_ATOM_SPAN(live_atoms) \
    LOGICAL_BINDINGS(lexical_env) \
    LOGICAL_BINDINGS(branch_env) \
    OUTCOME_SET(source_outcomes) \
    OUTCOME_SET(parent_outcomes)

#define CETTA_EVAL_GC_FRAME_FIELDS_static_branch_walk(\
    STRONG_ATOM_SLOT, STRONG_ATOM_SPAN, \
    LOGICAL_BINDINGS, OUTCOME_SET, VARIANT_INSTANCE, \
    EPHEMERON_ATOM_MAP) \
    STRONG_ATOM_SPAN(pending)

#define CETTA_EVAL_GC_FRAME_FIELDS_function_args(\
    STRONG_ATOM_SLOT, STRONG_ATOM_SPAN, \
    LOGICAL_BINDINGS, OUTCOME_SET, VARIANT_INSTANCE, \
    EPHEMERON_ATOM_MAP) \
    STRONG_ATOM_SLOT(orig_arg) \
    STRONG_ATOM_SLOT(arg_type) \
    LOGICAL_BINDINGS(env) \
    OUTCOME_SET(arg_os) \
    LOGICAL_BINDINGS(merged) \
    LOGICAL_BINDINGS(active) \
    LOGICAL_BINDINGS(child)

#define CETTA_EVAL_GC_FRAME_FIELDS_function_args_machine(\
    STRONG_ATOM_SLOT, STRONG_ATOM_SPAN, \
    LOGICAL_BINDINGS, OUTCOME_SET, VARIANT_INSTANCE, \
    EPHEMERON_ATOM_MAP) \
    STRONG_ATOM_SLOT(op) \
    STRONG_ATOM_SPAN(orig_args) \
    STRONG_ATOM_SPAN(arg_types) \
    STRONG_ATOM_SPAN(prefix) \
    OUTCOME_SET(outcomes)

#define CETTA_EVAL_GC_FRAME_FIELDS_typed_application_continuation(\
    STRONG_ATOM_SLOT, STRONG_ATOM_SPAN, \
    LOGICAL_BINDINGS, OUTCOME_SET, VARIANT_INSTANCE, \
    EPHEMERON_ATOM_MAP) \
    STRONG_ATOM_SPAN(live_atoms) \
    STRONG_ATOM_SPAN(overload_types) \
    STRONG_ATOM_SPAN(return_contracts) \
    STRONG_ATOM_SPAN(applicability_errors) \
    STRONG_ATOM_SPAN(argument_types) \
    OUTCOME_SET(function_results) \
    OUTCOME_SET(heads) \
    OUTCOME_SET(parent_outcomes)

#define CETTA_EVAL_GC_FRAME_FIELDS_observation_normalization(\
    STRONG_ATOM_SLOT, STRONG_ATOM_SPAN, \
    LOGICAL_BINDINGS, OUTCOME_SET, VARIANT_INSTANCE, \
    EPHEMERON_ATOM_MAP) \
    STRONG_ATOM_SLOT(source) \
    STRONG_ATOM_SLOT(value) \
    STRONG_ATOM_SPAN(children) \
    LOGICAL_BINDINGS(env) \
    OUTCOME_SET(child_outcomes) \
    OUTCOME_SET(parent_outcomes)

#define CETTA_EVAL_GC_FRAME_FIELDS_tuple_frame(\
    STRONG_ATOM_SLOT, STRONG_ATOM_SPAN, \
    LOGICAL_BINDINGS, OUTCOME_SET, VARIANT_INSTANCE, \
    EPHEMERON_ATOM_MAP) \
    LOGICAL_BINDINGS(env) \
    OUTCOME_SET(sub) \
    LOGICAL_BINDINGS(merged) \
    LOGICAL_BINDINGS(active) \
    VARIANT_INSTANCE(prefix_variant) \
    VARIANT_INSTANCE(active_variant)

#define CETTA_EVAL_GC_FRAME_FIELDS_tuple_machine(\
    STRONG_ATOM_SLOT, STRONG_ATOM_SPAN, \
    LOGICAL_BINDINGS, OUTCOME_SET, VARIANT_INSTANCE, \
    EPHEMERON_ATOM_MAP) \
    STRONG_ATOM_SPAN(continuation_atoms) \
    STRONG_ATOM_SPAN(orig_elems) \
    STRONG_ATOM_SPAN(prefix) \
    LOGICAL_BINDINGS(env) \
    OUTCOME_SET(outcomes) \
    OUTCOME_SET(parent_outcomes)

#define CETTA_EVAL_GC_FRAME_FIELDS_prime_task(\
    STRONG_ATOM_SLOT, STRONG_ATOM_SPAN, \
    LOGICAL_BINDINGS, OUTCOME_SET, VARIANT_INSTANCE, \
    EPHEMERON_ATOM_MAP) \
    STRONG_ATOM_SLOT(atom) \
    STRONG_ATOM_SLOT(etype) \
    LOGICAL_BINDINGS(dynamic_env) \
    LOGICAL_BINDINGS(seed_env) \
    OUTCOME_SET(target)

#define CETTA_EVAL_GC_FRAME_FIELDS_prime_frame(\
    STRONG_ATOM_SLOT, STRONG_ATOM_SPAN, \
    LOGICAL_BINDINGS, OUTCOME_SET, VARIANT_INSTANCE, \
    EPHEMERON_ATOM_MAP) \
    STRONG_ATOM_SLOT(atom) \
    STRONG_ATOM_SLOT(etype) \
    STRONG_ATOM_SLOT(strict_source) \
    STRONG_ATOM_SLOT(then_branch) \
    STRONG_ATOM_SLOT(else_branch) \
    STRONG_ATOM_SLOT(weight_subject) \
    STRONG_ATOM_SLOT(weight_algebra) \
    STRONG_ATOM_SLOT(weight_trace) \
    STRONG_ATOM_SLOT(ref) \
    STRONG_ATOM_SPAN(normalization_children) \
    LOGICAL_BINDINGS(env) \
    OUTCOME_SET(child) \
    OUTCOME_SET(force_pending) \
    STRONG_ATOM_SPAN(equation_roots) \
    OUTCOME_SET(equation_work) \
    OUTCOME_SET(equation_current) \
    OUTCOME_SET(equation_raw) \
    OUTCOME_SET(equation_residuals) \
    OUTCOME_SET(equation_publication) \
    OUTCOME_SET(equation_worlds) \
    OUTCOME_SET(target)

#define CETTA_EVAL_GC_FRAME_FIELDS_prime_driver(\
    STRONG_ATOM_SLOT, STRONG_ATOM_SPAN, \
    LOGICAL_BINDINGS, OUTCOME_SET, VARIANT_INSTANCE, \
    EPHEMERON_ATOM_MAP) \
    OUTCOME_SET(root_target)

#define CETTA_EVAL_GC_FRAME_FIELDS_prepared_pure_machine(\
    STRONG_ATOM_SLOT, STRONG_ATOM_SPAN, \
    LOGICAL_BINDINGS, OUTCOME_SET, VARIANT_INSTANCE, \
    EPHEMERON_ATOM_MAP) \
    STRONG_ATOM_SPAN(values) \
    STRONG_ATOM_SPAN(live_slots) \
    STRONG_ATOM_SPAN(runtime_frames) \
    EPHEMERON_ATOM_MAP(thunk_updates)

#define CETTA_EVAL_GC_ARM_lexical(SESSION, PAYLOAD, FAIL) do { \
    CETTA_GC_VISIT_STRONG_ATOM_SLOT((SESSION), (PAYLOAD)->atom_io, FAIL); \
    CETTA_GC_VISIT_LOGICAL_BINDINGS((SESSION), (PAYLOAD)->env, FAIL); \
    CETTA_GC_VISIT_STRONG_ATOM_SLOT((SESSION), (PAYLOAD)->etype_io, FAIL); \
} while (0)

#define CETTA_EVAL_GC_ARM_outcome_set(SESSION, PAYLOAD, FAIL) do { \
    CETTA_GC_VISIT_OUTCOME_SET((SESSION), (PAYLOAD)->outcomes, FAIL); \
} while (0)

#define CETTA_EVAL_GC_ARM_outcome_continuation(SESSION, PAYLOAD, FAIL) do { \
    CETTA_GC_VISIT_STRONG_ATOM_SPAN((SESSION), (PAYLOAD)->live_atoms, FAIL); \
    CETTA_GC_VISIT_OUTCOME_SET((SESSION), (PAYLOAD)->child_outcomes, FAIL); \
    CETTA_GC_VISIT_OUTCOME_SET((SESSION), (PAYLOAD)->parent_outcomes, FAIL); \
} while (0)

#define CETTA_EVAL_GC_ARM_evaluation_retry(SESSION, PAYLOAD, FAIL) do { \
    CETTA_GC_VISIT_STRONG_ATOM_SLOT((SESSION), (PAYLOAD)->input, FAIL); \
} while (0)

#define CETTA_EVAL_GC_ARM_type_cast_continuation(SESSION, PAYLOAD, FAIL) do { \
    CETTA_GC_VISIT_STRONG_ATOM_SLOT((SESSION), (PAYLOAD)->subject, FAIL); \
    CETTA_GC_VISIT_STRONG_ATOM_SLOT((SESSION), (PAYLOAD)->expected_type, FAIL); \
} while (0)

#define CETTA_EVAL_GC_ARM_let_branch_continuation(SESSION, PAYLOAD, FAIL) do { \
    CETTA_GC_VISIT_STRONG_ATOM_SLOT((SESSION), (PAYLOAD)->canonical, FAIL); \
    CETTA_GC_VISIT_STRONG_ATOM_SLOT((SESSION), (PAYLOAD)->pattern, FAIL); \
    CETTA_GC_VISIT_STRONG_ATOM_SLOT((SESSION), (PAYLOAD)->source, FAIL); \
    CETTA_GC_VISIT_STRONG_ATOM_SLOT((SESSION), (PAYLOAD)->scoped_body, FAIL); \
    CETTA_GC_VISIT_STRONG_ATOM_SPAN((SESSION), (PAYLOAD)->fresh, FAIL); \
    CETTA_GC_VISIT_LOGICAL_BINDINGS((SESSION), (PAYLOAD)->prefix, FAIL); \
    CETTA_GC_VISIT_OUTCOME_SET((SESSION), (PAYLOAD)->source_outcomes, FAIL); \
    CETTA_GC_VISIT_OUTCOME_SET((SESSION), (PAYLOAD)->parent_outcomes, FAIL); \
} while (0)

#define CETTA_EVAL_GC_ARM_case_branch_continuation(SESSION, PAYLOAD, FAIL) do { \
    CETTA_GC_VISIT_STRONG_ATOM_SPAN((SESSION), (PAYLOAD)->live_atoms, FAIL); \
    CETTA_GC_VISIT_LOGICAL_BINDINGS((SESSION), (PAYLOAD)->lexical_env, FAIL); \
    CETTA_GC_VISIT_LOGICAL_BINDINGS((SESSION), (PAYLOAD)->branch_env, FAIL); \
    CETTA_GC_VISIT_OUTCOME_SET((SESSION), (PAYLOAD)->source_outcomes, FAIL); \
    CETTA_GC_VISIT_OUTCOME_SET((SESSION), (PAYLOAD)->parent_outcomes, FAIL); \
} while (0)

#define CETTA_EVAL_GC_ARM_static_branch_walk(SESSION, PAYLOAD, FAIL) do { \
    CETTA_GC_VISIT_STRONG_ATOM_SPAN((SESSION), (PAYLOAD)->pending, FAIL); \
} while (0)

#define CETTA_EVAL_GC_ARM_function_args(SESSION, PAYLOAD, FAIL) do { \
    CETTA_GC_VISIT_STRONG_ATOM_SLOT((SESSION), (PAYLOAD)->orig_arg, FAIL); \
    CETTA_GC_VISIT_STRONG_ATOM_SLOT((SESSION), (PAYLOAD)->arg_type, FAIL); \
    CETTA_GC_VISIT_LOGICAL_BINDINGS((SESSION), (PAYLOAD)->env, FAIL); \
    CETTA_GC_VISIT_OUTCOME_SET((SESSION), (PAYLOAD)->arg_os, FAIL); \
    CETTA_GC_VISIT_LOGICAL_BINDINGS((SESSION), (PAYLOAD)->merged, FAIL); \
    CETTA_GC_VISIT_LOGICAL_BINDINGS((SESSION), (PAYLOAD)->active, FAIL); \
    CETTA_GC_VISIT_LOGICAL_BINDINGS((SESSION), (PAYLOAD)->child, FAIL); \
} while (0)

#define CETTA_EVAL_GC_ARM_function_args_machine(SESSION, PAYLOAD, FAIL) do { \
    CETTA_GC_VISIT_STRONG_ATOM_SLOT((SESSION), (PAYLOAD)->op, FAIL); \
    CETTA_GC_VISIT_STRONG_ATOM_SPAN((SESSION), (PAYLOAD)->orig_args, FAIL); \
    CETTA_GC_VISIT_STRONG_ATOM_SPAN((SESSION), (PAYLOAD)->arg_types, FAIL); \
    CETTA_GC_VISIT_STRONG_ATOM_SPAN((SESSION), (PAYLOAD)->prefix, FAIL); \
    CETTA_GC_VISIT_OUTCOME_SET((SESSION), (PAYLOAD)->outcomes, FAIL); \
} while (0)

#define CETTA_EVAL_GC_ARM_typed_application_continuation(SESSION, PAYLOAD, FAIL) do { \
    CETTA_GC_VISIT_STRONG_ATOM_SPAN((SESSION), (PAYLOAD)->live_atoms, FAIL); \
    CETTA_GC_VISIT_STRONG_ATOM_SPAN((SESSION), (PAYLOAD)->overload_types, FAIL); \
    CETTA_GC_VISIT_STRONG_ATOM_SPAN((SESSION), (PAYLOAD)->return_contracts, FAIL); \
    CETTA_GC_VISIT_STRONG_ATOM_SPAN((SESSION), (PAYLOAD)->applicability_errors, FAIL); \
    CETTA_GC_VISIT_STRONG_ATOM_SPAN((SESSION), (PAYLOAD)->argument_types, FAIL); \
    CETTA_GC_VISIT_OUTCOME_SET((SESSION), (PAYLOAD)->function_results, FAIL); \
    CETTA_GC_VISIT_OUTCOME_SET((SESSION), (PAYLOAD)->heads, FAIL); \
    CETTA_GC_VISIT_OUTCOME_SET((SESSION), (PAYLOAD)->parent_outcomes, FAIL); \
} while (0)

#define CETTA_EVAL_GC_ARM_observation_normalization(SESSION, PAYLOAD, FAIL) do { \
    CETTA_GC_VISIT_STRONG_ATOM_SLOT((SESSION), (PAYLOAD)->source, FAIL); \
    CETTA_GC_VISIT_STRONG_ATOM_SLOT((SESSION), (PAYLOAD)->value, FAIL); \
    CETTA_GC_VISIT_STRONG_ATOM_SPAN((SESSION), (PAYLOAD)->children, FAIL); \
    CETTA_GC_VISIT_LOGICAL_BINDINGS((SESSION), (PAYLOAD)->env, FAIL); \
    CETTA_GC_VISIT_OUTCOME_SET((SESSION), (PAYLOAD)->child_outcomes, FAIL); \
    CETTA_GC_VISIT_OUTCOME_SET((SESSION), (PAYLOAD)->parent_outcomes, FAIL); \
} while (0)

#define CETTA_EVAL_GC_ARM_tuple_frame(SESSION, PAYLOAD, FAIL) do { \
    CETTA_GC_VISIT_LOGICAL_BINDINGS((SESSION), (PAYLOAD)->env, FAIL); \
    CETTA_GC_VISIT_OUTCOME_SET((SESSION), (PAYLOAD)->sub, FAIL); \
    CETTA_GC_VISIT_LOGICAL_BINDINGS((SESSION), (PAYLOAD)->merged, FAIL); \
    CETTA_GC_VISIT_LOGICAL_BINDINGS((SESSION), (PAYLOAD)->active, FAIL); \
    CETTA_GC_VISIT_VARIANT_INSTANCE((SESSION), (PAYLOAD)->prefix_variant, FAIL); \
    CETTA_GC_VISIT_VARIANT_INSTANCE((SESSION), (PAYLOAD)->active_variant, FAIL); \
} while (0)

#define CETTA_EVAL_GC_ARM_tuple_machine(SESSION, PAYLOAD, FAIL) do { \
    CETTA_GC_VISIT_STRONG_ATOM_SPAN((SESSION), (PAYLOAD)->continuation_atoms, FAIL); \
    CETTA_GC_VISIT_STRONG_ATOM_SPAN((SESSION), (PAYLOAD)->orig_elems, FAIL); \
    CETTA_GC_VISIT_STRONG_ATOM_SPAN((SESSION), (PAYLOAD)->prefix, FAIL); \
    CETTA_GC_VISIT_LOGICAL_BINDINGS((SESSION), (PAYLOAD)->env, FAIL); \
    CETTA_GC_VISIT_OUTCOME_SET((SESSION), (PAYLOAD)->outcomes, FAIL); \
    CETTA_GC_VISIT_OUTCOME_SET((SESSION), (PAYLOAD)->parent_outcomes, FAIL); \
} while (0)

#define CETTA_EVAL_GC_ARM_prime_task(SESSION, PAYLOAD, FAIL) do { \
    CETTA_GC_VISIT_STRONG_ATOM_SLOT((SESSION), (PAYLOAD)->atom, FAIL); \
    CETTA_GC_VISIT_STRONG_ATOM_SLOT((SESSION), (PAYLOAD)->etype, FAIL); \
    CETTA_GC_VISIT_LOGICAL_BINDINGS((SESSION), (PAYLOAD)->dynamic_env, FAIL); \
    CETTA_GC_VISIT_LOGICAL_BINDINGS((SESSION), (PAYLOAD)->seed_env, FAIL); \
    CETTA_GC_VISIT_OUTCOME_SET((SESSION), (PAYLOAD)->target, FAIL); \
} while (0)

#define CETTA_EVAL_GC_ARM_prime_frame(SESSION, PAYLOAD, FAIL) do { \
    CETTA_GC_VISIT_STRONG_ATOM_SLOT((SESSION), (PAYLOAD)->atom, FAIL); \
    CETTA_GC_VISIT_STRONG_ATOM_SLOT((SESSION), (PAYLOAD)->etype, FAIL); \
    CETTA_GC_VISIT_STRONG_ATOM_SLOT((SESSION), (PAYLOAD)->strict_source, FAIL); \
    CETTA_GC_VISIT_STRONG_ATOM_SLOT((SESSION), (PAYLOAD)->then_branch, FAIL); \
    CETTA_GC_VISIT_STRONG_ATOM_SLOT((SESSION), (PAYLOAD)->else_branch, FAIL); \
    CETTA_GC_VISIT_STRONG_ATOM_SLOT((SESSION), (PAYLOAD)->weight_subject, FAIL); \
    CETTA_GC_VISIT_STRONG_ATOM_SLOT((SESSION), (PAYLOAD)->weight_algebra, FAIL); \
    CETTA_GC_VISIT_STRONG_ATOM_SLOT((SESSION), (PAYLOAD)->weight_trace, FAIL); \
    CETTA_GC_VISIT_STRONG_ATOM_SLOT((SESSION), (PAYLOAD)->ref, FAIL); \
    CETTA_GC_VISIT_STRONG_ATOM_SPAN((SESSION), (PAYLOAD)->normalization_children, FAIL); \
    CETTA_GC_VISIT_LOGICAL_BINDINGS((SESSION), (PAYLOAD)->env, FAIL); \
    CETTA_GC_VISIT_OUTCOME_SET((SESSION), (PAYLOAD)->child, FAIL); \
    CETTA_GC_VISIT_OUTCOME_SET((SESSION), (PAYLOAD)->force_pending, FAIL); \
    CETTA_GC_VISIT_STRONG_ATOM_SPAN((SESSION), (PAYLOAD)->equation_roots, FAIL); \
    CETTA_GC_VISIT_OUTCOME_SET((SESSION), (PAYLOAD)->equation_work, FAIL); \
    CETTA_GC_VISIT_OUTCOME_SET((SESSION), (PAYLOAD)->equation_current, FAIL); \
    CETTA_GC_VISIT_OUTCOME_SET((SESSION), (PAYLOAD)->equation_raw, FAIL); \
    CETTA_GC_VISIT_OUTCOME_SET((SESSION), (PAYLOAD)->equation_residuals, FAIL); \
    CETTA_GC_VISIT_OUTCOME_SET((SESSION), (PAYLOAD)->equation_publication, FAIL); \
    CETTA_GC_VISIT_OUTCOME_SET((SESSION), (PAYLOAD)->equation_worlds, FAIL); \
    CETTA_GC_VISIT_OUTCOME_SET((SESSION), (PAYLOAD)->target, FAIL); \
} while (0)

#define CETTA_EVAL_GC_ARM_prime_driver(SESSION, PAYLOAD, FAIL) do { \
    CETTA_GC_VISIT_OUTCOME_SET((SESSION), (PAYLOAD)->root_target, FAIL); \
} while (0)

#define CETTA_EVAL_GC_ARM_prepared_pure_machine(SESSION, PAYLOAD, FAIL) do { \
    CETTA_GC_VISIT_STRONG_ATOM_SPAN((SESSION), (PAYLOAD)->values, FAIL); \
    CETTA_GC_VISIT_STRONG_ATOM_SPAN((SESSION), (PAYLOAD)->live_slots, FAIL); \
    CETTA_GC_VISIT_STRONG_ATOM_SPAN((SESSION), (PAYLOAD)->runtime_frames, FAIL); \
    CETTA_GC_VISIT_EPHEMERON_ATOM_MAP((SESSION), (PAYLOAD)->thunk_updates, FAIL); \
} while (0)

/* Representation-polymorphic register arms generated from the presentation. */
static inline bool cetta_gslt_register_execute_atom_binary(
    CettaGsltRegisterInstruction instruction,
    Atom *left, Atom *right, bool *boolean_out,
    CettaGsltRegisterResultKind *kind_out) {
    if (!left || !right || !boolean_out || !kind_out)
        return false;
    switch (instruction) {
    case CETTA_GSLT_REGISTER_INSTRUCTION_ATOM_EQUAL:
        *boolean_out = atom_value_eq(left, right);
        *kind_out = CETTA_GSLT_REGISTER_RESULT_BOOLEAN;
        return true;
    default:
        return false;
    }
}

/* Tagged-small-integer interpretation generated from the same register
 * program.  Exact arithmetic requests GMP promotion only on overflow. */
static inline bool cetta_gslt_register_execute_small_binary(
    CettaGsltRegisterInstruction instruction,
    int64_t *integer_out, bool *boolean_out,
    int64_t left, int64_t right,
    CettaGsltRegisterResultKind *kind_out, bool *promote_out) {
    if (!integer_out || !boolean_out || !kind_out || !promote_out)
        return false;
    *promote_out = false;
    switch (instruction) {
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_ADD:
        switch (cetta_exact_integer_small(CETTA_EXACT_INTEGER_V1_ADD, left, right, integer_out)) { case CETTA_EXACT_INTEGER_UNDEFINED: return false; case CETTA_EXACT_INTEGER_PROMOTE: *promote_out = true; break; case CETTA_EXACT_INTEGER_VALUE: break; }
        *kind_out = CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_SUBTRACT:
        switch (cetta_exact_integer_small(CETTA_EXACT_INTEGER_V1_SUB, left, right, integer_out)) { case CETTA_EXACT_INTEGER_UNDEFINED: return false; case CETTA_EXACT_INTEGER_PROMOTE: *promote_out = true; break; case CETTA_EXACT_INTEGER_VALUE: break; }
        *kind_out = CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_MULTIPLY:
        switch (cetta_exact_integer_small(CETTA_EXACT_INTEGER_V1_MUL, left, right, integer_out)) { case CETTA_EXACT_INTEGER_UNDEFINED: return false; case CETTA_EXACT_INTEGER_PROMOTE: *promote_out = true; break; case CETTA_EXACT_INTEGER_VALUE: break; }
        *kind_out = CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_EQUAL:
        *boolean_out = left == right;
        *kind_out = CETTA_GSLT_REGISTER_RESULT_BOOLEAN;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_LESS:
        *boolean_out = left < right;
        *kind_out = CETTA_GSLT_REGISTER_RESULT_BOOLEAN;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_GREATER:
        *boolean_out = left > right;
        *kind_out = CETTA_GSLT_REGISTER_RESULT_BOOLEAN;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_LESS_EQUAL:
        *boolean_out = left <= right;
        *kind_out = CETTA_GSLT_REGISTER_RESULT_BOOLEAN;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_GREATER_EQUAL:
        *boolean_out = left >= right;
        *kind_out = CETTA_GSLT_REGISTER_RESULT_BOOLEAN;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_REMAINDER:
        switch (cetta_exact_integer_small(CETTA_EXACT_INTEGER_V1_TREM, left, right, integer_out)) { case CETTA_EXACT_INTEGER_UNDEFINED: return false; case CETTA_EXACT_INTEGER_PROMOTE: *promote_out = true; break; case CETTA_EXACT_INTEGER_VALUE: break; }
        *kind_out = CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_FLOOR_MODULO:
        switch (cetta_exact_integer_small(CETTA_EXACT_INTEGER_V1_FREM, left, right, integer_out)) { case CETTA_EXACT_INTEGER_UNDEFINED: return false; case CETTA_EXACT_INTEGER_PROMOTE: *promote_out = true; break; case CETTA_EXACT_INTEGER_VALUE: break; }
        *kind_out = CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER;
        return true;
    default:
        return false;
    }
}

#if CETTA_BUILD_WITH_GMP
#include <gmp.h>

/* Executable interpretation generated from register-expression-execution. */
static inline bool cetta_gslt_register_execute_binary(
    CettaGsltRegisterInstruction instruction,
    mpz_ptr integer_out, bool *boolean_out,
    mpz_srcptr left, mpz_srcptr right,
    CettaGsltRegisterResultKind *kind_out) {
    if (!integer_out || !boolean_out || !left || !right || !kind_out)
        return false;
    switch (instruction) {
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_ADD:
        if (!cetta_exact_integer_big(CETTA_EXACT_INTEGER_V1_ADD, left, right, integer_out)) { return false; }
        *kind_out = CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_SUBTRACT:
        if (!cetta_exact_integer_big(CETTA_EXACT_INTEGER_V1_SUB, left, right, integer_out)) { return false; }
        *kind_out = CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_MULTIPLY:
        if (!cetta_exact_integer_big(CETTA_EXACT_INTEGER_V1_MUL, left, right, integer_out)) { return false; }
        *kind_out = CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_EQUAL:
        *boolean_out = mpz_cmp(left, right) == 0;
        *kind_out = CETTA_GSLT_REGISTER_RESULT_BOOLEAN;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_LESS:
        *boolean_out = mpz_cmp(left, right) < 0;
        *kind_out = CETTA_GSLT_REGISTER_RESULT_BOOLEAN;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_GREATER:
        *boolean_out = mpz_cmp(left, right) > 0;
        *kind_out = CETTA_GSLT_REGISTER_RESULT_BOOLEAN;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_LESS_EQUAL:
        *boolean_out = mpz_cmp(left, right) <= 0;
        *kind_out = CETTA_GSLT_REGISTER_RESULT_BOOLEAN;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_GREATER_EQUAL:
        *boolean_out = mpz_cmp(left, right) >= 0;
        *kind_out = CETTA_GSLT_REGISTER_RESULT_BOOLEAN;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_REMAINDER:
        if (!cetta_exact_integer_big(CETTA_EXACT_INTEGER_V1_TREM, left, right, integer_out)) { return false; }
        *kind_out = CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_FLOOR_MODULO:
        if (!cetta_exact_integer_big(CETTA_EXACT_INTEGER_V1_FREM, left, right, integer_out)) { return false; }
        *kind_out = CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER;
        return true;
    default:
        return false;
    }
}
#endif

typedef enum {
    CETTA_GSLT_ROW_CONTRIBUTE = 0,
    CETTA_GSLT_ROW_ADDITIVE_ZERO = 1,
    CETTA_GSLT_ROW_TRANSPORT_FAULT = 2,
    CETTA_GSLT_ROW_CONSUMER_STOP = 3,
} CettaGsltRowDisposition;

static inline CettaGsltRowDisposition cetta_gslt_classify_streamed_row(
    bool decoded, bool cyclic) {
    if (!decoded)
        return CETTA_GSLT_ROW_TRANSPORT_FAULT;
    if (cyclic)
        return CETTA_GSLT_ROW_ADDITIVE_ZERO;
    return CETTA_GSLT_ROW_CONTRIBUTE;
}

typedef enum {
    CETTA_GSLT_OBSERVATION_MATCH_CHAIN_PROGRESS = 0,
    CETTA_GSLT_OBSERVATION_DETERMINATE_RULE_USE = 1,
} CettaGsltObservationEvent;

static inline bool cetta_gslt_observation_visible(
    CettaGsltObservationEvent event, bool diagnostic_profile) {
    return diagnostic_profile &&
           (event == CETTA_GSLT_OBSERVATION_MATCH_CHAIN_PROGRESS ||
           event == CETTA_GSLT_OBSERVATION_DETERMINATE_RULE_USE);
}

#define CETTA_GSLT_MATCH_CHAIN_TRACE_ENV "CETTA_MATCH_CHAIN_TRACE"
#define CETTA_GSLT_MATCH_CHAIN_TRACE_INTERVAL_ENV "CETTA_MATCH_CHAIN_TRACE_INTERVAL"

typedef enum {
    CETTA_GSLT_LIFETIME_INITIALIZE = 0,
    CETTA_GSLT_LIFETIME_CLEANUP = 1,
    CETTA_GSLT_LIFETIME_ARENA_RESET = 2,
    CETTA_GSLT_LIFETIME_ARENA_FREE = 3,
    CETTA_GSLT_LIFETIME_ARENA_REUSE = 4,
} CettaGsltLifetimeTransition;

static inline bool cetta_gslt_lifetime_invalidates_allocations(
    CettaGsltLifetimeTransition transition) {
    return transition == CETTA_GSLT_LIFETIME_ARENA_RESET ||
           transition == CETTA_GSLT_LIFETIME_ARENA_FREE;
}

typedef enum {
    CETTA_GSLT_EVIDENCE_SINGLETON_HEAD = 1u << 0,
    CETTA_GSLT_EVIDENCE_FLAT_LINEAR_LHS = 1u << 1,
    CETTA_GSLT_EVIDENCE_RANGE_RESTRICTED_RHS = 1u << 2,
    CETTA_GSLT_EVIDENCE_REVISION_CURRENT = 1u << 3,
    CETTA_GSLT_EVIDENCE_GROUND_CALL = 1u << 4,
    CETTA_GSLT_EVIDENCE_REGISTER_GUARD = 1u << 5,
    CETTA_GSLT_EVIDENCE_REGISTER_BASE = 1u << 6,
    CETTA_GSLT_EVIDENCE_REGISTER_TAIL = 1u << 7,
    CETTA_GSLT_EVIDENCE_REGISTER_RECURSIVE = 1u << 8,
    CETTA_GSLT_EVIDENCE_CALL_POLICY_SUPPORTED = 1u << 9,
} CettaGsltPreparedEquationEvidence;

#define CETTA_GSLT_PREPARED_EQUATION_MAX_REGISTERS     16u

static inline bool cetta_gslt_prepared_equation_plan_admitted(
    uint32_t evidence) {
    const uint32_t required =
        CETTA_GSLT_EVIDENCE_SINGLETON_HEAD |
        CETTA_GSLT_EVIDENCE_FLAT_LINEAR_LHS |
        CETTA_GSLT_EVIDENCE_RANGE_RESTRICTED_RHS |
        CETTA_GSLT_EVIDENCE_REVISION_CURRENT |
        CETTA_GSLT_EVIDENCE_CALL_POLICY_SUPPORTED;
    return (evidence & required) == required;
}

/* Shared accelerator admission for call-policy preservation: a call of a
 * head the dialect types falls back to canonical dispatch unless the
 * accelerator implements the head's policy itself, as the prepared machine
 * does for HE's value-class signatures. */
#define CETTA_GSLT_ACCELERATOR_CALL_POLICY_SUPPORTED(space, head, arity)     (space_head_call_untyped((space), (head), (arity)))

static inline bool cetta_gslt_prepared_equation_call_admitted(
    uint32_t evidence) {
    const uint32_t required =
        CETTA_GSLT_EVIDENCE_SINGLETON_HEAD |
        CETTA_GSLT_EVIDENCE_FLAT_LINEAR_LHS |
        CETTA_GSLT_EVIDENCE_RANGE_RESTRICTED_RHS |
        CETTA_GSLT_EVIDENCE_REVISION_CURRENT |
        CETTA_GSLT_EVIDENCE_CALL_POLICY_SUPPORTED |
        CETTA_GSLT_EVIDENCE_GROUND_CALL;
    return (evidence & required) == required;
}

static inline bool cetta_gslt_prepared_register_step_admitted(
    uint32_t evidence) {
    const uint32_t required =
        CETTA_GSLT_EVIDENCE_SINGLETON_HEAD |
        CETTA_GSLT_EVIDENCE_FLAT_LINEAR_LHS |
        CETTA_GSLT_EVIDENCE_RANGE_RESTRICTED_RHS |
        CETTA_GSLT_EVIDENCE_REVISION_CURRENT |
        CETTA_GSLT_EVIDENCE_CALL_POLICY_SUPPORTED |
        CETTA_GSLT_EVIDENCE_GROUND_CALL |
        CETTA_GSLT_EVIDENCE_REGISTER_GUARD |
        CETTA_GSLT_EVIDENCE_REGISTER_BASE |
        CETTA_GSLT_EVIDENCE_REGISTER_TAIL;
    return (evidence & required) == required;
}

static inline bool cetta_gslt_prepared_register_recursion_admitted(
    uint32_t evidence) {
    const uint32_t required =
        CETTA_GSLT_EVIDENCE_SINGLETON_HEAD |
        CETTA_GSLT_EVIDENCE_FLAT_LINEAR_LHS |
        CETTA_GSLT_EVIDENCE_RANGE_RESTRICTED_RHS |
        CETTA_GSLT_EVIDENCE_REVISION_CURRENT |
        CETTA_GSLT_EVIDENCE_CALL_POLICY_SUPPORTED |
        CETTA_GSLT_EVIDENCE_GROUND_CALL |
        CETTA_GSLT_EVIDENCE_REGISTER_RECURSIVE;
    return (evidence & required) == required;
}

/* This interpretation makes the uninitialized -> live transition explicit
 * at every cleanup-owned local.  The owning header must be visible at
 * expansion. */
#define CETTA_GSLT_OWNED_BINDINGS(name) \
    __attribute__((cleanup(bindings_free))) Bindings name; \
    bindings_init(&(name))

#endif /* CETTA_EXECUTION_CONTRACTS_GENERATED_H */

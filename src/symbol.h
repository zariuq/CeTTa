#ifndef CETTA_SYMBOL_H
#define CETTA_SYMBOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>
#include <stdint.h>
#include <pthread.h>

typedef uint32_t SymbolId;

#define SYMBOL_ID_NONE ((SymbolId)0)

enum {
    CETTA_SYMBOL_FLAG_STATIC_GROUNDED_OP = 1u << 0,
};

typedef struct {
    const char *bytes;
    uint32_t len;
    uint64_t hash;
    uint32_t flags;
} SymbolEntry;

typedef struct {
    uint64_t hash;
    uint32_t len;
    SymbolId id;
} SymbolSlot;

#define SYMBOL_ENTRY_CHUNK_BITS 14u
#define SYMBOL_ENTRY_CHUNK_SIZE (1u << SYMBOL_ENTRY_CHUNK_BITS)
#define SYMBOL_ENTRY_CHUNK_MASK (SYMBOL_ENTRY_CHUNK_SIZE - 1u)
#define SYMBOL_ENTRY_CHUNK_COUNT (1u << (32u - SYMBOL_ENTRY_CHUNK_BITS))

typedef struct {
    SymbolSlot *slots;
    uint32_t slot_cap;
    uint32_t slot_used;
    uint64_t instance_id;

    SymbolEntry **entry_chunks;
    _Atomic uint32_t entry_len;
    pthread_mutex_t write_mutex;
} SymbolTable;

static inline uint64_t symbol_table_instance_id(const SymbolTable *st) {
    return st ? st->instance_id : 0u;
}

#define CETTA_BUILTIN_SYMBOLS(X) \
    X(empty, "Empty") \
    X(error, "Error") \
    X(equals, "=") \
    X(atom, "Atom") \
    X(symbol, "Symbol") \
    X(variable, "Variable") \
    X(expression, "Expression") \
    X(grounded, "Grounded") \
    X(undefined_type, "%Undefined%") \
    X(true_text, "True") \
    X(false_text, "False") \
    X(bindings, "Bindings") \
    X(bang, "!") \
    X(quote, "quote") \
    X(capture, "capture") \
    X(function, "function") \
    X(colon, ":") \
    X(arrow, "->") \
    X(self, "&self") \
    X(search_policy, "search-policy") \
    X(order, "order") \
    X(native, "native") \
    X(mork_text, "mork") \
    X(mork_get_atoms_surface, "mork:get-atoms") \
    X(mork_match_surface, "mork:match") \
    X(reverse, "reverse") \
    X(lex, "lex") \
    X(shortlex, "shortlex") \
    X(recursive_dependent_proof, "recursive-dependent-proof") \
    X(atp_guided_inhabitation, "atp-guided-inhabitation") \
    X(atp_saturation, "atp-saturation") \
    X(solver_oracle, "solver-oracle") \
    X(comma, ",") \
    X(pipe, "|") \
    X(match, "match") \
    X(superpose, "superpose") \
    X(hyperpose, "hyperpose") \
    X(collapse, "collapse") \
    X(cons_atom, "cons-atom") \
    X(union_atom, "union-atom") \
    X(decons_atom, "decons-atom") \
    X(car_atom, "car-atom") \
    X(cdr_atom, "cdr-atom") \
    X(unify, "unify") \
    X(case_text, "case") \
    X(switch_text, "switch") \
    X(switch_minimal, "switch-minimal") \
    X(let_star, "let*") \
    X(let, "let") \
    X(chain, "chain") \
    X(abt_chain_v1, "ABTChainV1") \
    X(abt_pattern_var_v1, "ABTPatternVarV1") \
    X(abt_let_scope_v1, "ABTLetScopeV1") \
    X(abt_let_v1, "ABTLetV1") \
    X(abt_default_signatures, "__cetta_abt_default_signatures") \
    X(abt_signature_admitted, "__cetta_abt_signature_admitted") \
    X(abt_shift, "__cetta_abt_shift") \
    X(abt_subst, "__cetta_abt_subst") \
    X(abt_close, "__cetta_abt_close") \
    X(abt_open, "__cetta_abt_open") \
    X(abt_bind, "__cetta_abt_bind") \
    X(abt_print, "__cetta_abt_print") \
    X(abt_parse, "__cetta_abt_parse") \
    X(abt_scope_check, "__cetta_abt_scope_check") \
    X(abt_alpha_eq, "__cetta_abt_alpha_eq") \
    X(collect, "collect") \
    X(fold, "fold") \
    X(fold_by_key, "fold-by-key") \
    X(reduce, "reduce") \
    X(select, "select") \
    X(once, "once") \
    X(assert_text, "assert") \
    X(return_text, "return") \
    X(eval, "eval") \
    X(foldl_atom_in_space, "foldl-atom-in-space") \
    X(new_space, "new-space") \
    X(space_union, "space-union") \
    X(space_intersection, "space-intersection") \
    X(context_space, "context-space") \
    X(call_native, "call-native") \
    X(git_module_bang, "git-module!") \
    X(git_import_bang, "git-import!") \
    X(register_module_bang, "register-module!") \
    X(import_bang, "import!") \
    X(include, "include") \
    X(mod_space_bang, "mod-space!") \
    X(print_mods_bang, "print-mods!") \
    X(module_inventory_bang, "module-inventory!") \
    X(reset_runtime_stats_bang, "reset-runtime-stats!") \
    X(runtime_stats_bang, "runtime-stats!") \
    X(with_space_snapshot, "with-space-snapshot") \
    X(space_set_backend_bang, "space-set-backend!") \
    X(space_set_match_backend_bang, "space-set-match-backend!") \
    X(space_len, "space-len") \
    X(space_push, "space-push") \
    X(space_peek, "space-peek") \
    X(space_pop, "space-pop") \
    X(space_get, "space-get") \
    X(space_truncate, "space-truncate") \
    X(step_bang, "step!") \
    X(bind_bang, "bind!") \
    X(add_reduct, "add-reduct") \
    X(add_atom, "add-atom") \
    X(add_atoms, "add-atoms") \
    X(add_atom_nodup, "add-atom-nodup") \
    X(mork_add_atoms, "mork:add-atoms") \
    X(mork_add_atom, "mork:add-atom") \
    X(mork_remove_atom, "mork:remove-atom") \
    X(remove_atom, "remove-atom") \
    X(get_atoms, "get-atoms") \
    X(count_atoms, "count-atoms") \
    X(collapse_bind, "collapse-bind") \
    X(singleton_visible_witness, "singleton-visible-witness") \
    X(superpose_bind, "superpose-bind") \
    X(metta, "metta") \
    X(evalc, "evalc") \
    X(new_state, "new-state") \
    X(get_state, "get-state") \
    X(change_state_bang, "change-state!") \
    X(pragma_bang, "pragma!") \
    X(nop, "nop") \
    X(get_metatype, "get-metatype") \
    X(get_type, "get-type") \
    X(get_type_space, "get-type-space") \
    X(prime_package, "prime-package") \
    X(prime_judge, "prime-judge") \
    X(assertEqual, "assertEqual") \
    X(assertEqualToResult, "assertEqualToResult") \
    X(assertEqualMsg, "assertEqualMsg") \
    X(assertEqualToResultMsg, "assertEqualToResultMsg") \
    X(assertAlphaEqual, "assertAlphaEqual") \
    X(assertAlphaEqualMsg, "assertAlphaEqualMsg") \
    X(assertAlphaEqualToResult, "assertAlphaEqualToResult") \
    X(assertAlphaEqualToResultMsg, "assertAlphaEqualToResultMsg") \
    X(assertIncludes, "assertIncludes") \
    X(type_check, "type-check") \
    X(auto_text, "auto") \
    X(interpreter, "interpreter") \
    X(bare_minimal, "bare-minimal") \
    X(max_stack_depth, "max-stack-depth") \
    /* ── Grounded arithmetic/comparison operators ── */ \
    X(op_plus, "+") \
    X(op_minus, "-") \
    X(op_mul, "*") \
    X(op_div, "/") \
    X(op_floor_div, "//") \
    X(op_mod, "%") \
    X(op_lt, "<") \
    X(op_gt, ">") \
    X(op_le, "<=") \
    X(op_ge, ">=") \
    X(op_eq, "==") \
    X(numeric_eq, "numeric-eq") \
    X(alpha_eq, "=alpha") \
    X(if_equal, "if-equal") \
    X(sealed_text, "sealed") \
    X(op_and, "and") \
    X(op_or, "or") \
    X(op_not, "not") \
    X(op_xor, "xor") \
    /* ── Grounded I/O and formatting ── */ \
    X(println_bang, "println!") \
    X(readln_bang, "readln!") \
    X(flush_output_bang, "flush-output!") \
    X(trace_bang, "trace!") \
    X(format_args, "format-args") \
    X(repr, "repr") \
    X(sha256, "sha256") \
    X(parse, "parse") \
    X(parse_first, "parse-first") \
    X(print_alternatives_bang, "print-alternatives!") \
    /* ── Grounded collection/list operations ── */ \
    X(size, "size") \
    X(size_atom, "size-atom") \
    X(index_atom, "index-atom") \
    X(range_atom, "range-atom") \
    X(repeat_atom, "repeat-atom") \
    X(map_atom, "map-atom") \
    X(filter_atom, "filter-atom") \
    X(foldl_atom, "foldl-atom") \
    X(unique_atom, "unique-atom") \
    X(intersection_atom, "intersection-atom") \
    X(subtraction_atom, "subtraction-atom") \
    X(max_atom, "max-atom") \
    X(min_atom, "min-atom") \
    X(sort_strings, "sort-strings") \
    /* ── Grounded math functions ── */ \
    X(pow_math, "pow-math") \
    X(sqrt_math, "sqrt-math") \
    X(abs_math, "abs-math") \
    X(log_math, "log-math") \
    X(ceil_math, "ceil-math") \
    X(floor_math, "floor-math") \
    X(round_math, "round-math") \
    X(trunc_math, "trunc-math") \
    X(sin_math, "sin-math") \
    X(cos_math, "cos-math") \
    X(tan_math, "tan-math") \
    X(asin_math, "asin-math") \
    X(acos_math, "acos-math") \
    X(atan_math, "atan-math") \
    X(isnan_math, "isnan-math") \
    X(isinf_math, "isinf-math") \
    /* ── Grounded internal helpers ── */ \
    X(grounded_placeholder, "__grounded__") \
    X(llist_nil, "LNil") \
    X(llist_cons, "LCons") \
    X(minimal_foldl_atom, "_minimal-foldl-atom") \
    X(minimal_foldl_llist, "_minimal-foldl-llist") \
    X(minimal_space_contains_exact, "_minimal-space-contains-exact") \
    X(minimal_space_revision, "_minimal-space-revision") \
    X(collapse_add_next, "_collapse-add-next-atom-from-collapse-bind-result") \
    X(cetta_surface_available, "__cetta_surface-available") \
    /* ── Python FFI ── */ \
    X(py_atom, "py-atom") \
    X(py_call, "py-call") \
    X(py_dot, "py-dot") \
    /* ── Library extension hooks ── */ \
    X(lib_system_args, "__cetta_lib_system_args") \
    X(lib_system_arg, "__cetta_lib_system_arg") \
    X(lib_system_arg_count, "__cetta_lib_system_arg_count") \
    X(lib_system_has_args, "__cetta_lib_system_has_args") \
    X(lib_system_getenv_or_default, "__cetta_lib_system_getenv_or_default") \
    X(lib_system_is_flag_arg, "__cetta_lib_system_is_flag_arg") \
    X(lib_system_exit_with_code, "__cetta_lib_system_exit_with_code") \
    X(lib_system_cwd, "__cetta_lib_system_cwd") \
    X(lib_system_monotonic_ns, "__cetta_lib_system_monotonic_ns") \
    X(lib_prolog_available, "__cetta_lib_prolog_available") \
    X(lib_prolog_query, "__cetta_lib_prolog_query") \
    X(lib_fs_exists, "__cetta_lib_fs_exists") \
    X(lib_fs_read_text, "__cetta_lib_fs_read_text") \
    X(lib_fs_write_text, "__cetta_lib_fs_write_text") \
    X(lib_fs_append_text, "__cetta_lib_fs_append_text") \
    X(lib_fs_read_lines, "__cetta_lib_fs_read_lines") \
    X(lib_str_length, "__cetta_lib_str_length") \
    X(lib_str_concat, "__cetta_lib_str_concat") \
    X(lib_str_split, "__cetta_lib_str_split") \
    X(lib_str_split_whitespace, "__cetta_lib_str_split_whitespace") \
    X(lib_str_join, "__cetta_lib_str_join") \
    X(lib_str_slice, "__cetta_lib_str_slice") \
    X(lib_str_find, "__cetta_lib_str_find") \
    X(lib_str_starts_with, "__cetta_lib_str_starts_with") \
    X(lib_str_ends_with, "__cetta_lib_str_ends_with") \
    X(lib_str_trim, "__cetta_lib_str_trim") \
    X(lib_lts_he_transitions, "__cetta_lib_lts_he_transitions") \
    X(lib_lts_he_step_rules, "__cetta_lib_lts_he_step_rules") \
    X(lib_lts_rho_transitions, "__cetta_lib_lts_rho_transitions") \
    X(lib_lts_rho_cost_steps, "__cetta_lib_lts_rho_cost_steps") \
    X(lib_lts_rho_cost_causal_trace, "__cetta_lib_lts_rho_cost_causal_trace") \
    X(lib_lts_rho_cost_causal_prefix, "__cetta_lib_lts_rho_cost_causal_prefix") \
    X(lib_gparse_inference_presentation, "__cetta_lib_gparse_inference_presentation") \
    X(lib_gparse_inference_dag, "__cetta_lib_gparse_inference_dag") \
    X(lib_gparse_inference_dag_presentation, "__cetta_lib_gparse_inference_dag_presentation") \
    X(lib_gparse_inference_dag_proof, "__cetta_lib_gparse_inference_dag_proof") \
    X(lib_rhometta_run, "__cetta_lib_rhometta_run") \
    X(lib_rhometta_transitions, "__cetta_lib_rhometta_transitions") \
    X(lib_mork_space_new, "__cetta_lib_mork_space_new") \
    X(lib_mork_space_include, "__cetta_lib_mork_space_include") \
    X(lib_mork_space_open_act, "__cetta_lib_mork_space_open_act") \
    X(lib_mork_space_dump_act, "__cetta_lib_mork_space_dump_act") \
    X(lib_mork_space_import_act, "__cetta_lib_mork_space_import_act") \
    X(lib_mork_space_step, "__cetta_lib_mork_space_step") \
    X(lib_mork_space_add_atoms, "__cetta_lib_mork_space_add_atoms") \
    X(lib_mork_space_add_stream, "__cetta_lib_mork_space_add_stream") \
    X(lib_mork_space_add_atom, "__cetta_lib_mork_space_add_atom") \
    X(lib_mork_space_remove_atom, "__cetta_lib_mork_space_remove_atom") \
    X(lib_mork_space_atoms, "__cetta_lib_mork_space_atoms") \
    X(lib_mork_space_size, "__cetta_lib_mork_space_size") \
    X(lib_mork_space_count_atoms, "__cetta_lib_mork_space_count_atoms") \
    X(lib_mork_space_match, "__cetta_lib_mork_space_match") \
    X(lib_mork_clone, "__cetta_lib_mork_clone") \
    X(lib_mork_join, "__cetta_lib_mork_join") \
    X(lib_mork_meet, "__cetta_lib_mork_meet") \
    X(lib_mork_subtract, "__cetta_lib_mork_subtract") \
    X(lib_mork_restrict, "__cetta_lib_mork_restrict") \
    X(lib_mork_zipper_new, "__cetta_lib_mork_zipper_new") \
    X(lib_mork_zipper_close, "__cetta_lib_mork_zipper_close") \
    X(lib_mork_zipper_path_exists, "__cetta_lib_mork_zipper_path_exists") \
    X(lib_mork_zipper_is_val, "__cetta_lib_mork_zipper_is_val") \
    X(lib_mork_zipper_child_count, "__cetta_lib_mork_zipper_child_count") \
    X(lib_mork_path_of_atom, "__cetta_lib_mork_path_of_atom") \
    X(lib_mork_zipper_path_bytes, "__cetta_lib_mork_zipper_path_bytes") \
    X(lib_mork_zipper_child_bytes, "__cetta_lib_mork_zipper_child_bytes") \
    X(lib_mork_zipper_val_count, "__cetta_lib_mork_zipper_val_count") \
    X(lib_mork_zipper_depth, "__cetta_lib_mork_zipper_depth") \
    X(lib_mork_zipper_reset, "__cetta_lib_mork_zipper_reset") \
    X(lib_mork_zipper_ascend, "__cetta_lib_mork_zipper_ascend") \
    X(lib_mork_zipper_descend_byte, "__cetta_lib_mork_zipper_descend_byte") \
    X(lib_mork_zipper_descend_index, "__cetta_lib_mork_zipper_descend_index") \
    X(lib_mork_zipper_descend_first, "__cetta_lib_mork_zipper_descend_first") \
    X(lib_mork_zipper_descend_last, "__cetta_lib_mork_zipper_descend_last") \
    X(lib_mork_zipper_descend_until, "__cetta_lib_mork_zipper_descend_until") \
    X(lib_mork_zipper_descend_until_max_bytes, "__cetta_lib_mork_zipper_descend_until_max_bytes") \
    X(lib_mork_zipper_ascend_until, "__cetta_lib_mork_zipper_ascend_until") \
    X(lib_mork_zipper_ascend_until_branch, "__cetta_lib_mork_zipper_ascend_until_branch") \
    X(lib_mork_zipper_next_sibling_byte, "__cetta_lib_mork_zipper_next_sibling_byte") \
    X(lib_mork_zipper_prev_sibling_byte, "__cetta_lib_mork_zipper_prev_sibling_byte") \
    X(lib_mork_zipper_next_step, "__cetta_lib_mork_zipper_next_step") \
    X(lib_mork_zipper_next_val, "__cetta_lib_mork_zipper_next_val") \
    X(lib_mork_zipper_fork, "__cetta_lib_mork_zipper_fork") \
    X(lib_mork_zipper_make_map, "__cetta_lib_mork_zipper_make_map") \
    X(lib_mork_zipper_make_snapshot_map, "__cetta_lib_mork_zipper_make_snapshot_map") \
    X(lib_mork_product_zipper_new, "__cetta_lib_mork_product_zipper_new") \
    X(lib_mork_product_zipper_close, "__cetta_lib_mork_product_zipper_close") \
    X(lib_mork_product_zipper_path_exists, "__cetta_lib_mork_product_zipper_path_exists") \
    X(lib_mork_product_zipper_is_val, "__cetta_lib_mork_product_zipper_is_val") \
    X(lib_mork_product_zipper_child_count, "__cetta_lib_mork_product_zipper_child_count") \
    X(lib_mork_product_zipper_path_bytes, "__cetta_lib_mork_product_zipper_path_bytes") \
    X(lib_mork_product_zipper_child_bytes, "__cetta_lib_mork_product_zipper_child_bytes") \
    X(lib_mork_product_zipper_val_count, "__cetta_lib_mork_product_zipper_val_count") \
    X(lib_mork_product_zipper_depth, "__cetta_lib_mork_product_zipper_depth") \
    X(lib_mork_product_zipper_factor_count, "__cetta_lib_mork_product_zipper_factor_count") \
    X(lib_mork_product_zipper_focus_factor, "__cetta_lib_mork_product_zipper_focus_factor") \
    X(lib_mork_product_zipper_path_indices, "__cetta_lib_mork_product_zipper_path_indices") \
    X(lib_mork_product_zipper_reset, "__cetta_lib_mork_product_zipper_reset") \
    X(lib_mork_product_zipper_ascend, "__cetta_lib_mork_product_zipper_ascend") \
    X(lib_mork_product_zipper_descend_byte, "__cetta_lib_mork_product_zipper_descend_byte") \
    X(lib_mork_product_zipper_descend_index, "__cetta_lib_mork_product_zipper_descend_index") \
    X(lib_mork_product_zipper_descend_first, "__cetta_lib_mork_product_zipper_descend_first") \
    X(lib_mork_product_zipper_descend_last, "__cetta_lib_mork_product_zipper_descend_last") \
    X(lib_mork_product_zipper_descend_until, "__cetta_lib_mork_product_zipper_descend_until") \
    X(lib_mork_product_zipper_descend_until_max_bytes, "__cetta_lib_mork_product_zipper_descend_until_max_bytes") \
    X(lib_mork_product_zipper_ascend_until, "__cetta_lib_mork_product_zipper_ascend_until") \
    X(lib_mork_product_zipper_ascend_until_branch, "__cetta_lib_mork_product_zipper_ascend_until_branch") \
    X(lib_mork_product_zipper_next_sibling_byte, "__cetta_lib_mork_product_zipper_next_sibling_byte") \
    X(lib_mork_product_zipper_prev_sibling_byte, "__cetta_lib_mork_product_zipper_prev_sibling_byte") \
    X(lib_mork_product_zipper_next_step, "__cetta_lib_mork_product_zipper_next_step") \
    X(lib_mork_product_zipper_next_val, "__cetta_lib_mork_product_zipper_next_val") \
    X(lib_mork_overlay_zipper_new, "__cetta_lib_mork_overlay_zipper_new") \
    X(lib_mork_overlay_zipper_close, "__cetta_lib_mork_overlay_zipper_close") \
    X(lib_mork_overlay_zipper_path_exists, "__cetta_lib_mork_overlay_zipper_path_exists") \
    X(lib_mork_overlay_zipper_is_val, "__cetta_lib_mork_overlay_zipper_is_val") \
    X(lib_mork_overlay_zipper_child_count, "__cetta_lib_mork_overlay_zipper_child_count") \
    X(lib_mork_overlay_zipper_path_bytes, "__cetta_lib_mork_overlay_zipper_path_bytes") \
    X(lib_mork_overlay_zipper_child_bytes, "__cetta_lib_mork_overlay_zipper_child_bytes") \
    X(lib_mork_overlay_zipper_depth, "__cetta_lib_mork_overlay_zipper_depth") \
    X(lib_mork_overlay_zipper_reset, "__cetta_lib_mork_overlay_zipper_reset") \
    X(lib_mork_overlay_zipper_ascend, "__cetta_lib_mork_overlay_zipper_ascend") \
    X(lib_mork_overlay_zipper_descend_byte, "__cetta_lib_mork_overlay_zipper_descend_byte") \
    X(lib_mork_overlay_zipper_descend_index, "__cetta_lib_mork_overlay_zipper_descend_index") \
    X(lib_mork_overlay_zipper_descend_first, "__cetta_lib_mork_overlay_zipper_descend_first") \
    X(lib_mork_overlay_zipper_descend_last, "__cetta_lib_mork_overlay_zipper_descend_last") \
    X(lib_mork_overlay_zipper_descend_until, "__cetta_lib_mork_overlay_zipper_descend_until") \
    X(lib_mork_overlay_zipper_descend_until_max_bytes, "__cetta_lib_mork_overlay_zipper_descend_until_max_bytes") \
    X(lib_mork_overlay_zipper_ascend_until, "__cetta_lib_mork_overlay_zipper_ascend_until") \
    X(lib_mork_overlay_zipper_ascend_until_branch, "__cetta_lib_mork_overlay_zipper_ascend_until_branch") \
    X(lib_mork_overlay_zipper_next_sibling_byte, "__cetta_lib_mork_overlay_zipper_next_sibling_byte") \
    X(lib_mork_overlay_zipper_prev_sibling_byte, "__cetta_lib_mork_overlay_zipper_prev_sibling_byte") \
    X(lib_mork_overlay_zipper_next_step, "__cetta_lib_mork_overlay_zipper_next_step") \
    X(lib_mm2_program_new, "__cetta_lib_mm2_program_new") \
    X(lib_mm2_program_clear, "__cetta_lib_mm2_program_clear") \
    X(lib_mm2_program_add, "__cetta_lib_mm2_program_add") \
    X(lib_mm2_load_file, "__cetta_lib_mm2_load_file") \
    X(lib_mm2_program_size, "__cetta_lib_mm2_program_size") \
    X(lib_mm2_program_atoms, "__cetta_lib_mm2_program_atoms") \
    X(lib_mm2_context_new, "__cetta_lib_mm2_context_new") \
    X(lib_mm2_context_clear, "__cetta_lib_mm2_context_clear") \
    X(lib_mm2_context_load_program, "__cetta_lib_mm2_context_load_program") \
    X(lib_mm2_context_add, "__cetta_lib_mm2_context_add") \
    X(lib_mm2_context_remove, "__cetta_lib_mm2_context_remove") \
    X(lib_mm2_context_run, "__cetta_lib_mm2_context_run") \
    X(lib_mm2_context_step, "__cetta_lib_mm2_context_step") \
    X(lib_mm2_context_size, "__cetta_lib_mm2_context_size") \
    X(lib_mm2_context_atoms, "__cetta_lib_mm2_context_atoms") \
    /* ── Native handle; final member of the builtin-surface ID interval ── */ \
    X(native_handle, "NativeHandle") \
    /* Shared control syntax has an interned identity but is not a value. */ \
    X(if_text, "if")

/* Builtins whose grounded-operation capability is independent of language
   and profile.  symbol_table_init_builtins compiles this declaration into
   SymbolEntry flags, so evaluator dispatch reads one capability bit instead
   of reclassifying an interned opcode by name. */
#define CETTA_STATIC_GROUNDED_SYMBOL_FIELDS(X) \
    X(abt_default_signatures) \
    X(abt_signature_admitted) \
    X(abt_shift) \
    X(abt_subst) \
    X(abt_close) \
    X(abt_open) \
    X(abt_bind) \
    X(abt_print) \
    X(abt_parse) \
    X(abt_scope_check) \
    X(abt_alpha_eq) \
    X(mork_add_atoms) \
    X(mork_add_atom) \
    X(mork_remove_atom) \
    X(add_atom) \
    X(remove_atom) \
    X(op_plus) \
    X(op_minus) \
    X(op_mul) \
    X(op_div) \
    X(op_floor_div) \
    X(op_mod) \
    X(op_lt) \
    X(op_gt) \
    X(op_le) \
    X(op_ge) \
    X(op_eq) \
    X(numeric_eq) \
    X(alpha_eq) \
    X(if_equal) \
    X(sealed_text) \
    X(minimal_foldl_atom) \
    X(minimal_foldl_llist) \
    X(minimal_space_contains_exact) \
    X(minimal_space_revision) \
    X(collapse_add_next) \
    X(foldl_atom_in_space) \
    X(op_and) \
    X(op_or) \
    X(op_not) \
    X(op_xor) \
    X(println_bang) \
    X(readln_bang) \
    X(flush_output_bang) \
    X(trace_bang) \
    X(format_args) \
    X(repr) \
    X(sha256) \
    X(parse) \
    X(parse_first) \
    X(py_atom) \
    X(py_dot) \
    X(py_call) \
    X(sort_strings) \
    X(print_alternatives_bang) \
    X(unique_atom) \
    X(intersection_atom) \
    X(subtraction_atom) \
    X(max_atom) \
    X(min_atom) \
    X(pow_math) \
    X(sqrt_math) \
    X(abs_math) \
    X(log_math) \
    X(trunc_math) \
    X(ceil_math) \
    X(floor_math) \
    X(round_math) \
    X(sin_math) \
    X(asin_math) \
    X(cos_math) \
    X(acos_math) \
    X(tan_math) \
    X(atan_math) \
    X(isnan_math) \
    X(isinf_math) \
    X(size) \
    X(size_atom) \
    X(index_atom) \
    X(range_atom) \
    X(repeat_atom)

typedef struct {
#define CETTA_BUILTIN_SYMBOL_FIELD(field, text) SymbolId field;
    CETTA_BUILTIN_SYMBOLS(CETTA_BUILTIN_SYMBOL_FIELD)
#undef CETTA_BUILTIN_SYMBOL_FIELD
} BuiltinSyms;

extern SymbolTable *g_symbols;
extern BuiltinSyms g_builtin_syms;

/* Builtin surface symbols occupy the table's initial contiguous interval.
 * This distinguishes evaluator syntax from later interned constructor/data
 * heads without spelling the builtin set again in each consumer. */
bool symbol_id_is_builtin_surface(SymbolId id);

void symbol_table_init(SymbolTable *st);
void symbol_table_free(SymbolTable *st);
void symbol_table_init_builtins(SymbolTable *st, BuiltinSyms *builtins);

SymbolId symbol_intern_bytes(SymbolTable *st, const uint8_t *bytes, uint32_t len);
SymbolId symbol_intern_span_hashed(SymbolTable *st, const uint8_t *bytes,
                                   uint32_t len, uint64_t hash);
SymbolId symbol_intern_cstr(SymbolTable *st, const char *text);

const char *symbol_bytes(const SymbolTable *st, SymbolId id);
uint32_t symbol_len(const SymbolTable *st, SymbolId id);
uint64_t symbol_hash_value(const SymbolTable *st, SymbolId id);
uint32_t symbol_flags(const SymbolTable *st, SymbolId id);
bool symbol_eq_cstr(const SymbolTable *st, SymbolId id, const char *text);


/* PeTTa's determinism-annotated arrows are written `-[mode]->`.  The mode is
   a syntactic property of the arrow symbol; whether it carries authority is a
   language/profile decision made by the caller. */
typedef enum {
    CETTA_PETTA_ARROW_MODE_NONE = 0,
    CETTA_PETTA_ARROW_MODE_DET,
    CETTA_PETTA_ARROW_MODE_NONDET,
    CETTA_PETTA_ARROW_MODE_OTHER,
} CettaPettaArrowMode;

CettaPettaArrowMode cetta_petta_arrow_mode(SymbolId id);

#endif /* CETTA_SYMBOL_H */

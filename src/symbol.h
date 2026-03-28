#ifndef CETTA_SYMBOL_H
#define CETTA_SYMBOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint32_t SymbolId;

#define SYMBOL_ID_NONE ((SymbolId)0)

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

typedef struct {
    SymbolSlot *slots;
    uint32_t slot_cap;
    uint32_t slot_used;

    SymbolEntry *entries;
    uint32_t entry_len;
    uint32_t entry_cap;
} SymbolTable;

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
    X(reverse, "reverse") \
    X(lex, "lex") \
    X(shortlex, "shortlex") \
    X(recursive_dependent_proof, "recursive-dependent-proof") \
    X(atp_saturation, "atp-saturation") \
    X(solver_oracle, "solver-oracle") \
    X(comma, ",") \
    X(match, "match") \
    X(superpose, "superpose") \
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
    X(collect, "collect") \
    X(select, "select") \
    X(once, "once") \
    X(assert_text, "assert") \
    X(return_text, "return") \
    X(eval, "eval") \
    X(foldl_atom_in_space, "foldl-atom-in-space") \
    X(new_space, "new-space") \
    X(context_space, "context-space") \
    X(call_native, "call-native") \
    X(git_module_bang, "git-module!") \
    X(register_module_bang, "register-module!") \
    X(import_bang, "import!") \
    X(include, "include") \
    X(mod_space_bang, "mod-space!") \
    X(print_mods_bang, "print-mods!") \
    X(module_inventory_bang, "module-inventory!") \
    X(reset_runtime_stats_bang, "reset-runtime-stats!") \
    X(runtime_stats_bang, "runtime-stats!") \
    X(with_space_snapshot, "with-space-snapshot") \
    X(space_kind, "space-kind") \
    X(space_match_backend, "space-match-backend") \
    X(space_capabilities, "space-capabilities") \
    X(space_len, "space-len") \
    X(space_push, "space-push") \
    X(space_peek, "space-peek") \
    X(space_pop, "space-pop") \
    X(space_get, "space-get") \
    X(space_truncate, "space-truncate") \
    X(bind_bang, "bind!") \
    X(add_reduct, "add-reduct") \
    X(add_atom, "add-atom") \
    X(add_atom_nodup, "add-atom-nodup") \
    X(remove_atom, "remove-atom") \
    X(get_atoms, "get-atoms") \
    X(count_atoms, "count-atoms") \
    X(collapse_bind, "collapse-bind") \
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
    X(op_mod, "%") \
    X(op_lt, "<") \
    X(op_gt, ">") \
    X(op_le, "<=") \
    X(op_ge, ">=") \
    X(op_eq, "==") \
    X(alpha_eq, "=alpha") \
    X(if_equal, "if-equal") \
    X(sealed_text, "sealed") \
    X(op_and, "and") \
    X(op_or, "or") \
    X(op_not, "not") \
    X(op_xor, "xor") \
    /* ── Grounded I/O and formatting ── */ \
    X(println_bang, "println!") \
    X(trace_bang, "trace!") \
    X(format_args, "format-args") \
    X(print_alternatives_bang, "print-alternatives!") \
    /* ── Grounded collection/list operations ── */ \
    X(size_atom, "size-atom") \
    X(index_atom, "index-atom") \
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
    X(minimal_foldl_atom, "_minimal-foldl-atom") \
    X(collapse_add_next, "_collapse-add-next-atom-from-collapse-bind-result") \
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
    /* ── Native handle ── */ \
    X(native_handle, "NativeHandle")

typedef struct {
#define CETTA_BUILTIN_SYMBOL_FIELD(field, text) SymbolId field;
    CETTA_BUILTIN_SYMBOLS(CETTA_BUILTIN_SYMBOL_FIELD)
#undef CETTA_BUILTIN_SYMBOL_FIELD
} BuiltinSyms;

extern SymbolTable *g_symbols;
extern BuiltinSyms g_builtin_syms;

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
bool symbol_eq_cstr(const SymbolTable *st, SymbolId id, const char *text);

#endif /* CETTA_SYMBOL_H */

#ifndef CETTA_SYMBOL_H
#define CETTA_SYMBOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>
#include <stdint.h>
#include <pthread.h>
#include "scalar_trie.h"
#include "lang.h"

#ifndef CETTA_SYMBOL_READ_COVERAGE
#define CETTA_SYMBOL_READ_COVERAGE 0
#endif

typedef uint32_t SymbolId;

#define SYMBOL_ID_NONE ((SymbolId)0)

enum {
    CETTA_SYMBOL_FLAG_STATIC_GROUNDED_OP = 1u << 0,
    CETTA_SYMBOL_FLAG_TYPE_PURE_GROUNDED_OP = 1u << 1,
    /* Only explicitly data-born entries may be considered for collection.
     * The legacy intern API owns its result permanently. */
    CETTA_SYMBOL_FLAG_RUNTIME_DATA = 1u << 4,
    CETTA_SYMBOL_FLAG_SPECULATIVE_OP = 1u << 2,
    CETTA_SYMBOL_FLAG_BUILTIN = 1u << 3,
    CETTA_SYMBOL_FLAG_PETTA_ONLY_BUILTIN = 1u << 5,
    CETTA_SYMBOL_FLAG_BUILTIN_CONSTRUCTOR = 1u << 6,
    CETTA_SYMBOL_FLAG_LIBRARY_NATIVE_OP = 1u << 7,
    CETTA_SYMBOL_FLAG_CORE_GROUNDED_OP = 1u << 8,
};

typedef struct {
    const char *bytes;
    uint32_t len;
    uint64_t hash;
    _Atomic uint32_t flags;
    /* The builtin policy of the name, resolved on first use: zero until
     * then, otherwise the policy's position plus one (position zero: none).
     * Read and written only through the accessors below. */
    uint32_t builtin_policy;
    /* The symbol's canonical atom, owned by the table rather than an arena:
     * made on first request (atom_symbol_canonical), freed with the table. */
    _Atomic(void *) canonical_atom;
} SymbolEntry;

/* Payload bytes include the terminating NUL. Metadata includes reserved
 * entry chunks and index slots, rather than only populated entries. These
 * gauges describe table storage; resetting runtime counters does not reset
 * them. No entries are reclaimed in the instrumentation phase. */
typedef struct {
    uint64_t owned_count;
    uint64_t data_count;
    uint64_t retired_count;
    uint64_t owned_bytes;
    uint64_t data_bytes;
    uint64_t metadata_bytes;
    uint64_t peak_payload_bytes;
    uint64_t peak_metadata_bytes;
    uint64_t data_allocated_bytes;
    uint64_t promotions;
} SymbolTableStats;

typedef struct {
    uint64_t hash;
    uint32_t len;
    SymbolId id;
} SymbolSlot;

typedef struct SymbolCanonicalChunk SymbolCanonicalChunk;

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
    /* Immutable table-owned atoms in publication order. Chunks and entries
     * are append-only until table destruction; canonical_count publishes a
     * complete prefix. This lets checkpoint domains enumerate actual global
     * atoms without scanning every interned spelling. */
    SymbolCanonicalChunk *canonical_first, *canonical_last;
    _Atomic uint32_t canonical_count;
    pthread_mutex_t write_mutex;
    SymbolTableStats stats;
    /* Lazily populated spelling index, protected by write_mutex. Entries are
     * immutable scalar sequences; opaque non-UTF-8 spellings are counted. */
    CettaScalarTrie *name_trie;
    uint32_t name_trie_count, name_trie_invalid;
    /* The spellings, packed into chunks freed with the table: a symbol lives
     * as long as its table, so its spelling costs its bytes and no more.
     * Protected by write_mutex. */
    struct SymbolSpellingChunk *spelling_chunks;
    char *spelling_free;
    size_t spelling_left;
} SymbolTable;

/* Non-destructive marker substrate. Unmarked bytes are diagnostics, never
 * permission to free: the caller must still qualify every root owner and
 * mutator/borrow boundary. Late births invalidate the snapshot for sweeping.
 * This API contains no retirement or payload mutation operation. */
typedef struct {
    SymbolTable *table;
    uint64_t instance_id;
    uint32_t entry_limit;
    uint8_t *marked;
    bool late_mark;
} SymbolShadowScan;

typedef struct {
    uint64_t marked_data_count;
    uint64_t marked_data_bytes;
    uint64_t unmarked_data_count;
    uint64_t unmarked_data_bytes;
    uint64_t late_births;
    bool snapshot_outdated;
} SymbolShadowResult;

bool symbol_shadow_begin(SymbolTable *st, SymbolShadowScan *scan);
bool symbol_shadow_mark(SymbolShadowScan *scan, SymbolId id);
bool symbol_shadow_finish(SymbolShadowScan *scan, SymbolShadowResult *out);
void symbol_shadow_destroy(SymbolShadowScan *scan);

#if CETTA_SYMBOL_READ_COVERAGE
/* Test-only, thread-local read diagnostic for one pre-existing ID. The
 * collector marks copied atoms; selected symbol accessors check that mark.
 * This samples neither every holder nor direct ID reads, and cannot see a
 * borrowed bytes pointer retained across the boundary. It is not permission
 * to reclaim an entry. */
bool symbol_read_coverage_watch(SymbolTable *st, SymbolId id);
void symbol_read_coverage_begin(SymbolTable *st);
void symbol_read_coverage_mark_copy(SymbolTable *st, SymbolId id);
void symbol_read_coverage_commit(SymbolTable *st);
void symbol_read_coverage_cancel(SymbolTable *st);
void symbol_read_coverage_clear(void);
void symbol_read_coverage_omit_marks_for_test(bool omit);
uint64_t symbol_read_coverage_collections(void);
uint64_t symbol_read_coverage_marked_copies(void);
uint64_t symbol_read_coverage_checked_reads(void);
#endif

static inline uint64_t symbol_table_instance_id(const SymbolTable *st) {
    return st ? st->instance_id : 0u;
}

typedef struct {
    const char *bytes;
    uint32_t len;
} SymbolSpelling;

typedef struct {
    SymbolId *ids;
    size_t len;
    /* Exclusive upper bound on the captured table: IDs 1..count-1. */
    uint32_t snapshot_count, invalid_utf8_count;
    CettaTrieWalkStats stats;
} SymbolNameQueryResult;
/* NULL automaton obtains the valid-text snapshot for an independently
 * interpreted fallback. Product callbacks must not intern into this table.
 * IDs are returned once each in interning order. Invalid UTF-8 names remain
 * in the symbol table and are reported, not interpreted as absent matches.
 * Replacement is atomic; initialize result to zero before first use. */
bool symbol_table_query_names(SymbolTable *, const CettaTrieAutomaton *,
                             const void *initial, SymbolNameQueryResult *);
void symbol_name_query_free(SymbolNameQueryResult *);

/* One publication check for both parts of a spelling. Interned bytes and
 * lengths never change; the bytes remain borrowed from the table until it
 * is freed. The length includes bytes following an embedded zero. */
static inline SymbolSpelling symbol_spelling(
    const SymbolTable *st, SymbolId id) {
    SymbolSpelling spelling = { .bytes = "", .len = 0u };
    if (!st || id == SYMBOL_ID_NONE)
        return spelling;
    uint32_t count = atomic_load_explicit(
        &st->entry_len, memory_order_acquire);
    if (id >= count || !st->entry_chunks)
        return spelling;
    const SymbolEntry *chunk =
        st->entry_chunks[id >> SYMBOL_ENTRY_CHUNK_BITS];
    if (!chunk)
        return spelling;
    const SymbolEntry *entry = &chunk[id & SYMBOL_ENTRY_CHUNK_MASK];
    spelling.bytes = entry->bytes ? entry->bytes : "";
    spelling.len = entry->len;
    return spelling;
}

#define CETTA_BUILTIN_SYMBOLS(X) \
    X(empty, "Empty") \
    X(not_reducible, "NotReducible") \
    X(error, "Error") \
    X(equals, "=") \
    X(atom, "Atom") \
    X(symbol, "Symbol") \
    X(variable, "Variable") \
    X(expression, "Expression") \
    X(grounded, "Grounded") \
    X(list_type, "List") \
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
    X(mork_get_atoms_syntax, "mork:get-atoms") \
    X(mork_match_syntax, "mork:match") \
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
    X(reify, "reify") \
    X(cons_atom, "cons-atom") \
    X(union_atom, "union-atom") \
    X(decons_atom, "decons-atom") \
    X(car_atom, "car-atom") \
    X(cdr_atom, "cdr-atom") \
    X(unify, "unify") \
    X(case_text, "case") \
    X(case_value_text, "%case-value%") \
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
    X(abt_alpha_eq, "__cetta_abt_alpha_eq")     X(abt_free_names, "__cetta_abt_free_names")     X(abt_substitute, "__cetta_abt_substitute")     X(abt_rename_bound, "__cetta_abt_rename_bound")     X(abt_named_alpha_eq, "__cetta_abt_named_alpha_eq") \
    X(abt_instantiate, "__cetta_abt_instantiate") \
    X(abt_contextual_canonical, "__cetta_abt_contextual_canonical") \
    X(abt_quote_syntax, "__cetta_abt_quote_syntax") \
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
    X(module_parse_failed, "ModuleParseFailed") \
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
    X(exec_saturate, "exec:saturate") \
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
    X(amp_here, "&here") \
    X(amp_home, "&home") \
    X(ctx_colon_capture, "ctx:capture") \
    X(captured, "captured") \
    X(run, "run") \
    X(context_colon_self, "context:self") \
    X(new_state, "new-state") \
    X(get_state, "get-state") \
    X(change_state_bang, "change-state!") \
    X(pragma_bang, "pragma!") \
    X(nop, "nop") \
    X(get_metatype, "get-metatype") \
    X(get_type, "get-type") \
    X(get_type_space, "get-type-space") \
    X(prime_package, "prime-package") \
    X(prime_elaborate, "prime:elaborate") \
    X(nik_colon_check, "nik:check") \
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
    X(type_level_function, "type-level-function") \
    X(chaining_rule, "chaining-rule") \
    X(type_scheme, "type-scheme") \
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
    X(sort_atom, "sort-atom") \
    X(retain_top_k_keyed_atom, "retain-top-k-keyed-atom") \
    /* Compact native tensor operations, wrapped by lib_tensor.metta. */ \
    X(dense_v1_tensor_from, "__cetta_lib_tensor_from") \
    X(dense_v1_tensor_full, "__cetta_lib_tensor_full") \
    X(dense_v1_tensor_shape, "__cetta_lib_tensor_shape") \
    X(dense_v1_tensor_dtype, "__cetta_lib_tensor_dtype") \
    X(dense_v1_tensor_to_expr, "__cetta_lib_tensor_to_expr") \
    X(dense_v1_tensor_reshape, "__cetta_lib_tensor_reshape") \
    X(dense_v1_tensor_copy, "__cetta_lib_tensor_copy") \
    X(dense_v1_tensor_with, "__cetta_lib_tensor_with") \
    X(dense_v1_tensor_add, "__cetta_lib_tensor_add") \
    X(dense_v1_tensor_mul, "__cetta_lib_tensor_mul") \
    X(dense_v1_tensor_sum, "__cetta_lib_tensor_sum") \
    X(dense_v1_tensor_dot, "__cetta_lib_tensor_dot") \
    X(dense_v1_tensor_encode, "__cetta_lib_tensor_encode") \
    X(dense_v1_tensor_decode, "__cetta_lib_tensor_decode") \
    X(unkey_atom, "unkey-atom") \
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
    X(minimal_map_atom, "_minimal-map-atom") \
    X(minimal_retain_top_k_by_number, "_minimal-retain-top-k-by-number") \
    X(minimal_sum_by, "_minimal-sum-by") \
    X(minimal_foldl_atom, "_minimal-foldl-atom") \
    X(minimal_foldl_llist, "_minimal-foldl-llist") \
    X(minimal_foldl_sequence, "_minimal-foldl-sequence") \
    X(minimal_space_contains_exact, "_minimal-space-contains-exact") \
    X(minimal_space_revision, "_minimal-space-revision") \
    X(collapse_add_next, "_collapse-add-next-atom-from-collapse-bind-result") \
    X(cetta_builtin_available, "__cetta_builtin-available") \
    /* ── Python FFI ── */ \
    X(py_atom, "py-atom") \
    X(py_call, "py-call") \
    X(py_dot, "py-dot") \
    /* ── Library extension hooks ── */ \
    X(lib_list_from_expression, "__cetta_lib_list_from_expression") \
    X(lib_list_to_expression, "__cetta_lib_list_to_expression") \
    X(lib_list_range, "__cetta_lib_list_range") \
    X(lib_list_repeat, "__cetta_lib_list_repeat") \
    X(lib_list_group_sums, "__cetta_lib_list_group_sums") \
    X(lib_list_group_sums_blocks, "__cetta_lib_list_group_sums_blocks") \
    X(lib_list_factorize, "__cetta_lib_list_factorize") \
    X(lib_postings_prepare, "__cetta_lib_postings_prepare") \
    X(lib_postings_own, "__cetta_lib_postings_own") \
    X(lib_postings_counts, "__cetta_lib_postings_counts") \
    X(lib_postings_sources_current, "__cetta_lib_postings_sources_current") \
    X(lib_postings_inspect, "__cetta_lib_postings_inspect") \
    X(lib_postings_close, "__cetta_lib_postings_close") \
    X(binary_pack_fields, "binary:pack-fields") \
    X(binary_unpack_fields, "binary:unpack-fields") \
    X(binary_packed_length, "binary:packed-length") \
    X(binary_utf8, "binary:utf8") \
    X(binary_from_utf8, "binary:from-utf8") \
    X(binary_unsigned, "binary:unsigned") \
    X(binary_from_unsigned, "binary:from-unsigned") \
    X(binary_symbol_utf8, "binary:symbol-utf8") \
    X(binary_from_symbol_utf8, "binary:from-symbol-utf8") \
    X(binary_value_kind, "binary:value-kind") \
    X(binary_portable_kind, "binary:portable-kind") \
    X(binary_is_unsigned, "binary:unsigned?") \
    X(heap_empty, "heap:empty") \
    X(heap_insert, "heap:insert") \
    X(heap_take, "heap:take") \
    X(heap_size_public, "heap:size") \
    X(heap_valid, "heap:valid?") \
    X(work_epoch, "work:epoch") \
    X(work_source_epoch, "work:source-epoch") \
    X(work_source_status, "work:source-status") \
    X(dag_from_forest, "dag:from-forest") \
    X(dag_from_records, "dag:from-records") \
    X(dag_export, "dag:export") \
    X(dag_roots, "dag:roots") \
    X(dag_node, "dag:node") \
    X(dag_unfold, "dag:unfold") \
    X(dag_stats, "dag:stats") \
    X(dag_close, "dag:close") \
    X(syntax_read, "syntax:read") \
    X(syntax_restore, "syntax:restore") \
    X(syntax_freeze, "syntax:freeze") \
    X(syntax_write, "syntax:write") \
    X(syntax_intrinsic_head, "syntax:intrinsic-head?") \
    X(tensor_from_values, "tensor:from-values") \
    X(tensor_from_json, "tensor:from-json") \
    X(tensor_from_coo, "tensor:from-coo") \
    X(tensor_from_coordinates, "tensor:from-coordinates") \
    X(tensor_inspect, "tensor:inspect") \
    X(tensor_is_tensor, "tensor:is-tensor") \
    X(tensor_entries, "tensor:entries") \
    X(tensor_gather, "tensor:gather") \
    X(tensor_gather_rows, "tensor:gather-rows") \
    X(tensor_scan_sum, "tensor:scan-sum") \
    X(tensor_concatenate, "tensor:concatenate") \
    X(tensor_permute, "tensor:permute") \
    X(tensor_merge, "tensor:merge") \
    X(tensor_zip_map, "tensor:zip-map") \
    X(lib_tensor_try_zip_map, "__cetta_lib_tensor_try_zip_map") \
    X(tensor_shape, "tensor:shape") \
    X(tensor_dtype, "tensor:dtype") \
    X(tensor_layout, "tensor:layout") \
    X(tensor_at, "tensor:at") \
    X(tensor_copy, "tensor:copy") \
    X(tensor_convert, "tensor:convert") \
    X(tensor_cast, "tensor:cast") \
    X(tensor_promote, "tensor:promote") \
    X(tensor_view, "tensor:view") \
    X(tensor_reshape, "tensor:reshape") \
    X(tensor_sum, "tensor:sum") \
    X(tensor_matvec, "tensor:matvec") \
    X(tensor_top_k, "tensor:top-k") \
    X(tensor_encode, "tensor:encode") \
    X(tensor_decode, "tensor:decode") \
    X(tensor_encode_bytes, "tensor:encode-bytes") \
    X(tensor_decode_bytes, "tensor:decode-bytes") \
    X(tensor_builder, "tensor:builder") \
    X(tensor_put, "tensor:put") \
    X(tensor_freeze, "tensor:freeze") \
    X(tensor_freeze_log, "tensor:freeze-log") \
    X(tensor_close, "tensor:close") \
    X(tensor_all_finite, "tensor:all-finite") \
    X(tensor_normalize, "tensor:normalize") \
    X(tensor_cosine_index, "tensor:cosine-index") \
    X(tensor_cosine_scores, "tensor:cosine-scores") \
    X(tensor_cosine_retrieve, "tensor:cosine-retrieve") \
    X(tensor_index_source, "tensor:index-source") \
    X(tensor_screen_index, "tensor:screen-index") \
    X(tensor_screen, "tensor:screen") \
    X(tensor_screen_candidates, "tensor:screen-candidates") \
    X(tensor_screen_info, "tensor:screen-info") \
    X(tensor_rerank, "tensor:rerank") \
    X(lib_system_args, "__cetta_lib_system_args") \
    X(lib_system_arg, "__cetta_lib_system_arg") \
    X(lib_system_arg_count, "__cetta_lib_system_arg_count") \
    X(lib_system_has_args, "__cetta_lib_system_has_args") \
    X(lib_system_getenv_or_default, "__cetta_lib_system_getenv_or_default") \
    X(lib_system_is_flag_arg, "__cetta_lib_system_is_flag_arg") \
    X(lib_system_exit_with_code, "__cetta_lib_system_exit_with_code") \
    X(lib_system_write, "__cetta_lib_system_write") \
    X(lib_system_cwd, "__cetta_lib_system_cwd") \
    X(lib_system_entry_directory, "__cetta_lib_system_entry_directory") \
    X(lib_system_monotonic_ns, "__cetta_lib_system_monotonic_ns") \
    X(lib_system_host, "__cetta_lib_system_host") \
    X(lib_system_read_line, "__cetta_lib_system_read_line") \
    X(lib_fs_tail_text, "__cetta_lib_fs_tail_text") \
    X(lib_fs_entries, "__cetta_lib_fs_entries") \
    X(lib_proc_spawn, "__cetta_lib_proc_spawn") \
    X(lib_proc_poll, "__cetta_lib_proc_poll") \
    X(lib_proc_wait, "__cetta_lib_proc_wait") \
    X(lib_proc_status, "__cetta_lib_proc_status") \
    X(lib_proc_cancel, "__cetta_lib_proc_cancel") \
    X(lib_proc_close, "__cetta_lib_proc_close") \
    X(lib_proc_abort_signal, "__cetta_lib_proc_abort_signal") \
    X(lib_proc_await, "__cetta_lib_proc_await") \
    X(lib_prolog_available, "__cetta_lib_prolog_available") \
    X(lib_prolog_query, "__cetta_lib_prolog_query") \
    X(lib_fs_exists, "__cetta_lib_fs_exists") \
    X(lib_fs_canonical_path, "__cetta_lib_fs_canonical_path") \
    X(lib_fs_try_canonical_path, "__cetta_lib_fs_try_canonical_path") \
    X(lib_fs_try_canonical_from_file, "__cetta_lib_fs_try_canonical_from_file") \
    X(lib_fs_try_canonical_under, "__cetta_lib_fs_try_canonical_under") \
    X(lib_fs_read_text, "__cetta_lib_fs_read_text") \
    X(lib_fs_read_text_result, "__cetta_lib_fs_read_text_result") \
    X(lib_fs_line_window, "__cetta_lib_fs_line_window") \
    X(lib_fs_tree_page, "__cetta_lib_fs_tree_page") \
    X(lib_fs_grep_page, "__cetta_lib_fs_grep_page") \
    X(lib_fs_update_text, "__cetta_lib_fs_update_text") \
    X(lib_fs_is_file, "__cetta_lib_fs_is_file") \
    X(lib_fs_write, "__cetta_lib_fs_write") \
    X(lib_fs_rename, "__cetta_lib_fs_rename") \
    X(lib_fs_append_text, "__cetta_lib_fs_append_text") \
    X(lib_fs_read_lines, "__cetta_lib_fs_read_lines") \
    X(lib_fs_read_lines_between, "__cetta_lib_fs_read_lines_between") \
    X(lib_fs_metadata, "__cetta_lib_fs_metadata") \
    X(lib_fs_walk_open, "__cetta_lib_fs_walk_open") \
    X(lib_fs_walk_current, "__cetta_lib_fs_walk_current") \
    X(lib_fs_walk_step, "__cetta_lib_fs_walk_step") \
    X(lib_fs_walk_metadata, "__cetta_lib_fs_walk_metadata") \
    X(lib_fs_open_directory, "__cetta_lib_fs_open_directory") \
    X(lib_fs_next_entry, "__cetta_lib_fs_next_entry") \
    X(lib_fs_open_read, "__cetta_lib_fs_open_read") \
    X(lib_fs_read_chunk, "__cetta_lib_fs_read_chunk") \
    X(lib_fs_close, "__cetta_lib_fs_close") \
    X(lib_fs_open_write, "__cetta_lib_fs_open_write") \
    X(lib_fs_write_all, "__cetta_lib_fs_write_all") \
    X(lib_fs_sync, "__cetta_lib_fs_sync") \
    X(lib_fs_lock, "__cetta_lib_fs_lock") \
    X(lib_fs_unlock, "__cetta_lib_fs_unlock") \
    X(lib_fs_make_directories, "__cetta_lib_fs_make_directories") \
    X(lib_fs_set_mode, "__cetta_lib_fs_set_mode") \
    X(lib_fs_remove, "__cetta_lib_fs_remove") \
    X(lib_fs_access, "__cetta_lib_fs_access") \
    X(lib_io_capabilities, "__cetta_lib_io_capabilities") \
    X(lib_durable_open, "__cetta_lib_durable_open") \
    X(lib_durable_read, "__cetta_lib_durable_read") \
    X(lib_durable_commit, "__cetta_lib_durable_commit") \
    X(lib_durable_checkpoint, "__cetta_lib_durable_checkpoint") \
    X(lib_durable_close, "__cetta_lib_durable_close") \
    X(lib_io_submit, "__cetta_lib_io_submit") \
    X(lib_io_poll, "__cetta_lib_io_poll") \
    X(lib_io_status, "__cetta_lib_io_status") \
    X(lib_io_http_read, "__cetta_lib_io_http_read") \
    X(lib_io_wait, "__cetta_lib_io_wait") \
    X(lib_io_wait_ready, "__cetta_lib_io_wait_ready") \
    X(lib_io_cancel, "__cetta_lib_io_cancel") \
    X(lib_io_signal, "__cetta_lib_io_signal") \
    X(lib_cwp_call, "__cetta_lib_cwp_call") \
    X(lib_proc_run, "__cetta_lib_proc_run") \
    X(lib_sqlite_query, "__cetta_lib_sqlite_query") \
    X(lib_sqlite_transaction, "__cetta_lib_sqlite_transaction") \
    X(lib_sqlite_observe, "__cetta_lib_sqlite_observe") \
    X(lib_json_capabilities_v1, "__cetta_lib_json_capabilities_v1") \
    X(lib_json_parse_v1, "__cetta_lib_json_parse_v1") \
    X(lib_json_to_legacy_v1, "__cetta_lib_json_to_legacy_v1") \
    X(lib_json_from_inspection_v1, "__cetta_lib_json_from_inspection_v1") \
    X(lib_json_parse, "__cetta_lib_json_parse") \
    X(lib_json_project_v1, "__cetta_lib_json_project_v1") \
    X(lib_json_stringify_v1, "__cetta_lib_json_stringify_v1") \
    X(lib_json_stringify, "__cetta_lib_json_stringify") \
    X(lib_json_members_v1, "__cetta_lib_json_members_v1") \
    X(lib_json_get_all_v1, "__cetta_lib_json_get_all_v1") \
    X(lib_json_get_first_v1, "__cetta_lib_json_get_first_v1") \
    X(lib_json_get_last_v1, "__cetta_lib_json_get_last_v1") \
    X(lib_json_object_get, "__cetta_lib_json_object_get") \
    X(lib_str, "__cetta_lib_str") \
    X(lib_str_value, "__cetta_lib_str_value") \
    X(lib_re, "__cetta_lib_re") \
    X(lib_crypto, "__cetta_lib_crypto") \
    X(lib_toml, "__cetta_lib_toml") \
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
    X(lib_rhometta_run_canonical, "__cetta_lib_rhometta_run_canonical") \
    X(lib_rhometta_transitions, "__cetta_lib_rhometta_transitions") \
    X(lib_rhometta_values, "__cetta_lib_rhometta_values") \
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
    /* ── Bounded RuleMachineCoreV1 compilation syntax ── */ \
    X(compile_rule_package, "compile:rule-package") \
    X(compile_link_rule, "compile:link-rule") \
    X(compile_run, "compile:run") \
    X(compile_artifact_info, "compile:artifact-info") \
    X(compile_rule_program, "compile:rule-program") \
    X(compile_rule_program_info, "compile:rule-program-info") \
    X(compile_rule_program_link, "compile:rule-program-link") \
    X(compile_rule_program_run, "compile:rule-program-run") \
    X(compile_rule_program_run_native, "compile:rule-program-run-native") \
    /* ── Native handle; final member of the legacy contiguous ID interval ── */ \
    X(native_handle, "NativeHandle") \
    /* Append-only builtin extension: preserve every established SymbolId. */ \
    X(if_text, "if") \
    X(empty_form, "empty") \
    X(petta_member, "member") \
    X(petta_last, "last") \
    X(petta_the, "the") \
    X(petta_make_list, "make-list") \
    X(petta_msort, "msort") \
    X(petta_transaction, "transaction") \
    X(petta_with_mutex, "with_mutex") \
    X(petta_min, "min") \
    X(petta_max, "max") \
    X(type_colon_of, "type:of") \
    X(type_colon_normalize, "type:normalize") \
    X(type_colon_check, "type:check") \
    X(type_colon_analyze, "type:analyze") \
    X(type_colon_eq, "type:eq") \
    X(type_colon_formed, "type:formed") \
    X(type_colon_refine, "type:refine") \
    X(type_colon_may, "type:may") \
    X(type_colon_must, "type:must") \
    X(type_colon_theorem, "type:theorem") \
    X(type_colon_kernel_query, "type:kernel-query") \
    X(type_colon_prove, "type:prove") \
    X(cost_colon_firings, "cost:firings") \
    X(set_colon_signature, "set:signature") \
    X(set_colon_formed, "set:formed") \
    X(set_colon_of, "set:of") \
    X(set_colon_check, "set:check") \
    X(set_colon_eq, "set:eq") \
    X(set_colon_proves, "set:proves") \
    X(set_colon_axiom, "set:axiom") \
    X(set_colon_define, "set:define") \
    X(set_colon_inductive, "set:inductive") \
    X(set_colon_family, "set:family") \
    X(set_colon_theorem, "set:theorem") \
    X(set_colon_recheck, "set:recheck") \
    X(set_colon_known_proof, "set:known-proof") \
    X(set_colon_known_proposition, "set:known-proposition") \
    X(set_colon_signature_digest, "set:signature-digest") \
    X(set_colon_numerals, "set:numerals") \
    X(set_colon_oracle, "set:oracle") \
    X(set_colon_irrelevant, "set:irrelevant") \
    X(lang_colon_languages, "lang:languages") \
    X(lang_colon_parse, "lang:parse") \
    X(lang_colon_print, "lang:print") \
    X(lang_colon_native_type, "lang:native-type") \
    X(lang_colon_step, "lang:step") \
    X(try_judgment, "try") \
    X(id_colon_region, "id:region") \
    X(id_colon_policy, "id:policy") \
    X(compile_link_package, "compile:link-package") \
    X(unquote, "unquote") \
    X(remove_all_atom, "remove-all-atom") \
    X(sort_numbers_atom, "sort-numbers-atom") \
    X(set_colon_native_proof, "set:native-proof") \
    X(set_colon_native_use, "set:native-use") \
    X(set_colon_native_normalize, "set:native-normalize") \
    X(set_colon_native_link, "set:native-link") \
    X(set_colon_interpret, "set:interpret") \
    X(hol_colon_signature, "hol:signature") \
    X(hol_colon_formed, "hol:formed") \
    X(hol_colon_of, "hol:of") \
    X(hol_colon_check, "hol:check") \
    X(hol_colon_eq, "hol:eq") \
    X(hol_colon_proves, "hol:proves") \
    X(hol_colon_axiom, "hol:axiom") \
    X(hol_colon_define, "hol:define") \
    X(hol_colon_inductive, "hol:inductive") \
    X(hol_colon_family, "hol:family") \
    X(hol_colon_theorem, "hol:theorem") \
    X(hol_colon_recheck, "hol:recheck") \
    X(hol_colon_known_proof, "hol:known-proof") \
    X(hol_colon_known_proposition, "hol:known-proposition") \
    X(hol_colon_signature_digest, "hol:signature-digest") \
    X(hol_colon_numerals, "hol:numerals") \
    X(hol_colon_oracle, "hol:oracle") \
    X(hol_colon_irrelevant, "hol:irrelevant") \
    X(hol_colon_native_proof, "hol:native-proof") \
    X(hol_colon_native_use, "hol:native-use") \
    X(hol_colon_native_normalize, "hol:native-normalize") \
    X(hol_colon_native_link, "hol:native-link") \
    X(hol_colon_interpret, "hol:interpret") \
    X(petta_decons, "decons") \
    X(prime_data, "Data") \
    X(prime_proc, "Proc") \
    X(prime_lam, "lam") \
    X(prime_meta, "meta") \
    X(prime_new, "new") \
    X(term_lt, "@<") \
    X(term_gt, "@>") \
    X(term_le, "@<=") \
    X(term_ge, "@>=") \
    X(equals_percent, "=%") \
    X(pat_solve, "pat:solve") \
    X(pat_apply, "pat:apply") \
    X(pat_view, "pat:view") \
    X(pat_query, "pat:query") \
    X(pat_match_forward, "pat:match%") \
    X(pat_match_reverse, "pat:%match") \
    X(pat_unify_forward, "pat:unify%") \
    X(pat_let_forward, "pat:let%") \
    X(pat_case_forward, "pat:case%") \
    X(pat_is_instance, "pat:is-instance") \
    X(pat_is_variant, "pat:is-variant") \
    X(subtype, ":<") \
    X(work_new, "work-new") \
    X(work_take, "work-take") \
    X(work_budget, "work-budget") \
    X(work_cancel, "work-cancel") \
    X(work_record, "work-record") \
    X(work_route, "work-route") \
    X(work_count, "work-count") \
    X(work_exists, "work-exists") \
    X(lib_proc_write, "__cetta_lib_proc_write") \
    X(lib_proc_read, "__cetta_lib_proc_read") \
    X(lib_proc_close_input, "__cetta_lib_proc_close_input")

/* Identity, dialect ownership and constructor role are separate facts.
 * These columns supplement the registered vocabulary without changing IDs. */
#define CETTA_PETTA_ONLY_BUILTIN_SYMBOL_FIELDS(X) \
    X(petta_member) X(petta_last) X(petta_the) X(petta_make_list) \
    X(petta_msort) X(petta_transaction) X(petta_with_mutex) \
    X(petta_min) X(petta_max) X(petta_decons)
#define CETTA_BUILTIN_CONSTRUCTOR_SYMBOL_FIELDS(X) \
    X(prime_data) X(prime_proc) X(subtype)

/* Libraries may publish native primitives under their public names. The
 * shared registry supplies dispatch and operand-observation capability;
 * no evaluator needs to recognize a library's spelling or value type. */
#define CETTA_PUBLIC_LIBRARY_NATIVE_SYMBOL_FIELDS(X) \
    X(binary_pack_fields) X(binary_unpack_fields) X(binary_packed_length) \
    X(binary_unsigned) X(binary_from_unsigned) \
    X(binary_symbol_utf8) X(binary_from_symbol_utf8) \
    X(binary_value_kind) \
    X(binary_portable_kind) \
    X(binary_is_unsigned) \
    X(binary_utf8) X(binary_from_utf8) \
    X(heap_empty) X(heap_insert) X(heap_take) X(heap_size_public) X(heap_valid) \
    X(work_epoch) X(work_source_epoch) X(work_source_status) \
    X(dag_from_forest) X(dag_from_records) X(dag_export) X(dag_roots) \
    X(dag_node) X(dag_unfold) X(dag_stats) X(dag_close) \
    X(syntax_read) X(syntax_restore) X(syntax_freeze) X(syntax_write) X(syntax_intrinsic_head) \
    X(tensor_from_values) X(tensor_from_json) X(tensor_from_coo) X(tensor_from_coordinates) X(tensor_inspect) \
    X(tensor_shape) X(tensor_dtype) X(tensor_layout) X(tensor_at) X(tensor_is_tensor) \
    X(tensor_entries) X(tensor_gather) X(tensor_gather_rows) X(tensor_merge) \
    X(tensor_scan_sum) X(tensor_concatenate) X(tensor_permute) \
    X(tensor_copy) X(tensor_convert) X(tensor_cast) X(tensor_promote) X(tensor_view) X(tensor_reshape) X(tensor_sum) \
    X(tensor_matvec) X(tensor_top_k) X(tensor_encode) X(tensor_decode) \
    X(tensor_encode_bytes) X(tensor_decode_bytes) \
    X(tensor_builder) X(tensor_put) X(tensor_freeze) X(tensor_freeze_log) X(tensor_close) \
    X(tensor_all_finite) X(tensor_normalize) X(tensor_cosine_index) \
    X(tensor_cosine_scores) X(tensor_cosine_retrieve) X(tensor_index_source) \
    X(tensor_screen_index) X(tensor_screen) X(tensor_screen_candidates) \
    X(tensor_screen_info) X(tensor_rerank)

/* Builtins whose grounded-operation capability is independent of language
   and profile.  symbol_table_init_builtins compiles this declaration into
   SymbolEntry flags, so evaluator dispatch reads one capability bit instead
   of reclassifying an interned opcode by name. */
#define CETTA_STATIC_GROUNDED_SYMBOL_FIELDS(X) \
    X(abt_quote_syntax) \
    X(abt_instantiate) \
    X(abt_contextual_canonical) \
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
    X(abt_alpha_eq)     X(abt_free_names)     X(abt_substitute)     X(abt_rename_bound)     X(abt_named_alpha_eq) \
    X(mork_add_atoms) \
    X(mork_add_atom) \
    X(mork_remove_atom) \
    X(compile_rule_package) \
    X(compile_link_rule) \
    X(compile_link_package) \
    X(compile_run) \
    X(compile_artifact_info) \
    X(compile_rule_program) \
    X(compile_rule_program_info) \
    X(compile_rule_program_link) \
    X(compile_rule_program_run) \
    X(compile_rule_program_run_native) \
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
    X(minimal_retain_top_k_by_number) \
    X(minimal_sum_by) \
    X(minimal_foldl_atom) \
    X(minimal_foldl_llist) \
    X(minimal_foldl_sequence) \
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
    X(sort_atom) \
    X(sort_numbers_atom) \
    X(retain_top_k_keyed_atom) \
    X(unkey_atom) \
    X(sort_strings) \
    X(print_alternatives_bang) \
    X(unique_atom) \
    X(remove_all_atom) \
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
    X(repeat_atom) \
    X(lib_list_range) \
    X(lib_list_repeat) \
    X(lib_list_from_expression) \
    X(lib_list_to_expression)

/* Grounded value operations without ambient observations or mutation, given
 * closed values from the host. This is also an execution permission list:
 * additions require an effect audit, not merely a predictable result type. */
#define CETTA_PURE_GROUNDED_SYMBOL_FIELDS(X) \
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
    X(abt_alpha_eq)     X(abt_free_names)     X(abt_substitute)     X(abt_rename_bound)     X(abt_named_alpha_eq) \
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
    X(repr) \
    X(op_and) \
    X(op_or) \
    X(op_not) \
    X(op_xor) \
    X(sha256) \
    X(sort_atom) \
    X(sort_numbers_atom) \
    X(retain_top_k_keyed_atom) \
    X(unkey_atom) \
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
    X(car_atom) \
    X(cdr_atom) \
    X(size_atom) \
    X(index_atom) \
    X(range_atom) \
    X(lib_list_range) \
    X(repeat_atom) \
    X(unique_atom) \
    X(remove_all_atom) \
    X(intersection_atom) \
    X(subtraction_atom)

/* Result-type purity is weaker than effect freedom. Additional type-only
 * licenses belong here, not in the shared execution-permission list above. */
#define CETTA_TYPE_PURE_GROUNDED_SYMBOL_FIELDS(X) \
    CETTA_PURE_GROUNDED_SYMBOL_FIELDS(X) \
    X(abt_quote_syntax)

/* Additional operations admitted in a host-owned speculative HE context.
 * Query spaces must be fresh in-memory projections of the supplied observation.
 * Changes are returned as proposals, never applied through mutation builtins.
 * Native extensions and future builtins are excluded until explicitly audited. */
#define CETTA_SPECULATIVE_SYMBOL_FIELDS(X) \
    X(quote) \
    X(capture) \
    X(function) \
    X(colon) \
    X(arrow) \
    X(comma) \
    X(pipe) \
    X(match) \
    X(superpose) \
    X(hyperpose) \
    X(collapse) \
    X(reify) \
    X(cons_atom) \
    X(union_atom) \
    X(decons_atom) \
    X(car_atom) \
    X(cdr_atom) \
    X(unify) \
    X(case_text) \
    X(switch_text) \
    X(switch_minimal) \
    X(let_star) \
    X(let) \
    X(chain) \
    X(abt_chain_v1) \
    X(abt_let_v1) \
    X(collect) \
    X(fold) \
    X(fold_by_key) \
    X(reduce) \
    X(select) \
    X(once) \
    X(return_text) \
    X(eval) \
    X(foldl_atom_in_space) \
    X(get_atoms) \
    X(count_atoms) \
    X(collapse_bind) \
    X(superpose_bind) \
    X(metta) \
    X(evalc) \
    X(nop) \
    X(get_metatype) \
    X(get_type) \
    X(get_type_space) \
    X(if_text) \
    X(sealed_text) \
    X(minimal_foldl_atom) \
    X(minimal_foldl_llist) \
    X(minimal_foldl_sequence) \
    X(minimal_retain_top_k_by_number) \
    X(minimal_space_contains_exact) \
    X(minimal_space_revision) \
    X(size) \
    X(format_args) \
    X(parse) \
    X(parse_first) \
    X(lib_json_parse_v1) \
    X(lib_json_from_inspection_v1) \
    X(lib_json_parse) \
    X(lib_json_stringify_v1) \
    X(lib_json_stringify) \
    X(lib_json_members_v1) \
    X(lib_json_get_all_v1) \
    X(lib_json_get_first_v1) \
    X(lib_json_get_last_v1) \
    X(lib_json_object_get) \
    X(lib_str) \
    X(lib_lts_rho_transitions) \
    X(lib_rhometta_run) \
    X(lib_rhometta_run_canonical) \
    X(lib_rhometta_transitions) \
    X(lib_rhometta_values)

typedef struct {
#define CETTA_BUILTIN_SYMBOL_FIELD(field, text) SymbolId field;
    CETTA_BUILTIN_SYMBOLS(CETTA_BUILTIN_SYMBOL_FIELD)
#undef CETTA_BUILTIN_SYMBOL_FIELD
} BuiltinSyms;

extern SymbolTable *g_symbols;
extern BuiltinSyms g_builtin_syms;

/* Registration flags identify the builtin vocabulary independently of IDs.
 * Executable membership additionally depends on dialect and constructor role;
 * callers that dispatch operations use symbol_id_is_builtin_for instead. */
bool symbol_id_is_builtin(SymbolId id);
/* Executable builtin membership; registered type constructors and names owned
 * by another dialect keep ordinary data/equation resolution in that dialect. */
bool symbol_id_is_builtin_for(SymbolId id, CettaLanguageId language);

void symbol_table_init(SymbolTable *st);
void symbol_table_free(SymbolTable *st);
void symbol_table_init_builtins(SymbolTable *st, BuiltinSyms *builtins);

SymbolId symbol_intern_bytes(SymbolTable *st, const uint8_t *bytes, uint32_t len);
SymbolId symbol_intern_span_hashed(SymbolTable *st, const uint8_t *bytes,
                                   uint32_t len, uint64_t hash);
SymbolId symbol_intern_cstr(SymbolTable *st, const char *text);
/* Data births preserve the same spelling, ID equality and hash as owned
 * interning. An owned intern of an existing data spelling promotes it;
 * subsequent data interns never downgrade it. Interning does not collect. */
SymbolId symbol_intern_data_bytes(SymbolTable *st, const uint8_t *bytes,
                                  uint32_t len);
SymbolId symbol_intern_data_cstr(SymbolTable *st, const char *text);
/* Registration of a raw ID gives it permanent ownership. */
bool symbol_pin(SymbolTable *st, SymbolId id);
void symbol_table_stats_snapshot(SymbolTable *st, SymbolTableStats *out);
/* The symbol already interned for text, or SYMBOL_ID_NONE.  Never interns. */
SymbolId symbol_lookup_cstr(SymbolTable *st, const char *text);
/* A lookup never acquires ownership or inserts a missing spelling. */
SymbolId symbol_lookup_span_hashed(SymbolTable *st, const uint8_t *bytes,
                                    uint32_t len, uint64_t hash);
uint64_t symbol_hash_span(const uint8_t *bytes, uint32_t len);

/* Borrowed text: currently valid until table destruction. A future sweep
 * must restrict data-text borrows to protected roots/leases or the interval
 * before the next eligible quiescent boundary. The table mutex alone cannot
 * protect this unlocked borrow. Owned spellings remain permanent. */
const char *symbol_bytes(const SymbolTable *st, SymbolId id);
uint32_t symbol_len(const SymbolTable *st, SymbolId id);
/* Bytewise spelling order, including bytes after embedded NUL. Equal IDs
 * require no spelling read; no table entry address escapes to the caller. */
int symbol_compare(const SymbolTable *st, SymbolId left, SymbolId right);
uint64_t symbol_hash_value(const SymbolTable *st, SymbolId id);
uint32_t symbol_flags(const SymbolTable *st, SymbolId id);
uint32_t symbol_builtin_policy(const SymbolTable *st, SymbolId id);
/* The value depends only on the name, so concurrent callers record the same. */
void symbol_set_builtin_policy(SymbolTable *st, SymbolId id, uint32_t value);
/* The symbol's canonical atom, or NULL until one is published. */
void *symbol_canonical_atom(const SymbolTable *st, SymbolId id);
/* Publish `atom` as the symbol's canonical atom unless one is already:
 * the published atom, which is `atom` only when it won; NULL for an id the
 * table does not hold.  The table frees the published atom. */
void *symbol_publish_canonical_atom(SymbolTable *st, SymbolId id, void *atom);
uint32_t symbol_canonical_count(const SymbolTable *st);
typedef bool (*SymbolCanonicalVisitor)(void *atom, void *context);
/* Visit exactly one stable published prefix. Concurrent later publication
 * makes the visit fail rather than silently widening its checkpoint domain. */
bool symbol_canonical_visit(const SymbolTable *st, uint64_t instance,
    uint32_t count, SymbolCanonicalVisitor visit, void *context);
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

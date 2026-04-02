#ifndef CETTA_SPACE_MATCH_BACKEND_H
#define CETTA_SPACE_MATCH_BACKEND_H

#include "atom.h"
#include "subst_tree.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct Space Space;
typedef struct DiscNode DiscNode;

typedef enum {
    SPACE_MATCH_BACKEND_NATIVE = 0,
    SPACE_MATCH_BACKEND_NATIVE_CANDIDATE_EXACT = 1,
    SPACE_MATCH_BACKEND_PATHMAP_IMPORTED = 2,
} SpaceMatchBackendKind;

typedef struct {
    DiscNode *match_trie;
    bool match_trie_dirty;
    SubstTree *stree;
    bool stree_dirty;
} SpaceMatchNativeState;

typedef enum {
    IMPORTED_FLAT_SYMBOL = 0,
    IMPORTED_FLAT_VAR = 1,
    IMPORTED_FLAT_EXPR = 2,
    IMPORTED_FLAT_INT = 3,
    IMPORTED_FLAT_GROUNDED_OTHER = 4,
} ImportedFlatTokenKind;

typedef struct {
    ImportedFlatTokenKind kind;
    Atom *origin;
    uint32_t span;
    VarId var_id;
    union {
        SymbolId sym_id;
        uint32_t arity;
        int64_t ival;
    };
} ImportedFlatToken;

typedef struct {
    uint32_t atom_idx;
    uint32_t epoch;
    ImportedFlatToken *tokens;
    uint32_t len;
} ImportedFlatEntry;

typedef struct {
    ImportedFlatEntry *entries;
    uint32_t len, cap;
} ImportedFlatBucket;

typedef struct {
    ImportedFlatBucket buckets[STREE_BUCKETS];
    ImportedFlatBucket wildcard;
    bool built;
    bool dirty;
    bool bridge_active;
    bool attached_compiled;
    uint32_t attached_count;
    void *bridge_space;
} PathmapImportedState;

typedef struct SpaceMatchBackendOps {
    const char *name;
    bool supports_direct_bindings;
    void (*free)(Space *s);
    void (*note_add)(Space *s, Atom *atom, uint32_t atom_idx);
    void (*note_remove)(Space *s);
    uint32_t (*candidates)(Space *s, Atom *pattern, uint32_t **out);
    void (*query)(Space *s, Arena *a, Atom *query, SubstMatchSet *out);
    void (*query_conjunction)(Space *s, Arena *a, Atom **patterns, uint32_t npatterns,
                              const Bindings *seed, BindingSet *out);
} SpaceMatchBackendOps;

typedef struct {
    SpaceMatchBackendKind kind;
    const SpaceMatchBackendOps *ops;
    SpaceMatchNativeState native;
    PathmapImportedState imported;
} SpaceMatchBackend;

/*
 * Optional compiled-space capability.
 *
 * Positive example:
 *   - A backend can expose durable attach/materialize/step support without
 *     teaching eval.c or library.c its enum identity.
 *
 * Negative example:
 *   - Every core call site hard-codes `SPACE_MATCH_BACKEND_PATHMAP_IMPORTED`
 *     as if one backend name were the long-term contract.
 */
bool space_match_backend_kind_supports_compiled_space(SpaceMatchBackendKind kind);
bool space_match_backend_supports_compiled_space(const Space *s);
bool space_match_backend_try_enable_compiled_space(Space *s);

void space_match_backend_init(Space *s);
void space_match_backend_free(Space *s);
bool space_match_backend_try_set(Space *s, SpaceMatchBackendKind kind);
void space_match_backend_note_add(Space *s, Atom *atom, uint32_t atom_idx);
void space_match_backend_note_remove(Space *s);
uint32_t space_match_backend_candidates(Space *s, Atom *pattern, uint32_t **out);
void space_match_backend_query(Space *s, Arena *a, Atom *query, SubstMatchSet *out);
const char *space_match_backend_name(const Space *s);
bool space_match_backend_supports_direct_bindings(const Space *s);
const char *space_match_backend_kind_name(SpaceMatchBackendKind kind);
bool space_match_backend_kind_from_name(const char *name, SpaceMatchBackendKind *out);
bool space_match_backend_attach_act_file(Space *s, const char *path, uint64_t *out_loaded);
bool space_match_backend_materialize_attached(Space *s, Arena *persistent_arena);
bool space_match_backend_load_sexpr_chunk(Space *s, Arena *persistent_arena,
                                          const uint8_t *text, size_t len,
                                          uint64_t *out_added);
bool space_match_backend_step(Space *s, Arena *persistent_arena,
                              uint64_t steps, uint64_t *out_performed);
bool space_match_backend_is_attached_compiled(const Space *s);
uint32_t space_match_backend_logical_len(const Space *s);
void space_match_backend_print_inventory(FILE *out);

#endif /* CETTA_SPACE_MATCH_BACKEND_H */

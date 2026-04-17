#ifndef CETTA_SPACE_MATCH_BACKEND_H
#define CETTA_SPACE_MATCH_BACKEND_H

#include "atom.h"
#include "subst_tree.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct Space Space;
typedef struct DiscNode DiscNode;
typedef struct CettaMorkSpaceHandle CettaMorkSpaceHandle;

typedef enum {
    SPACE_ENGINE_NATIVE = 0,
    SPACE_ENGINE_NATIVE_CANDIDATE_EXACT = 1,
    SPACE_ENGINE_PATHMAP = 2,
    SPACE_ENGINE_MORK = 3,
} SpaceEngine;

static inline bool space_engine_uses_pathmap(SpaceEngine engine) {
    return engine == SPACE_ENGINE_PATHMAP || engine == SPACE_ENGINE_MORK;
}

static inline bool space_engine_supports_exec(SpaceEngine engine) {
    return engine == SPACE_ENGINE_MORK;
}

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
    IMPORTED_FLAT_FLOAT = 4,
    IMPORTED_FLAT_BOOL = 5,
    IMPORTED_FLAT_STRING = 6,
    IMPORTED_FLAT_GROUNDED_OTHER = 7,
} ImportedFlatTokenKind;

typedef struct {
    ImportedFlatTokenKind kind;
    Atom *origin;
    AtomId origin_id;
    uint32_t span;
    VarId var_id;
    union {
        SymbolId sym_id;
        uint32_t arity;
        int64_t ival;
        double fval;
        bool bval;
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
    void (*note_add)(Space *s, AtomId atom_id, Atom *atom, uint32_t atom_idx);
    void (*note_remove)(Space *s);
    uint32_t (*candidates)(Space *s, Atom *pattern, uint32_t **out);
    void (*query)(Space *s, Arena *a, Atom *query, SubstMatchSet *out);
    void (*query_conjunction)(Space *s, Arena *a, Atom **patterns, uint32_t npatterns,
                              const Bindings *seed, BindingSet *out);
} SpaceMatchBackendOps;

typedef struct {
    SpaceEngine kind;
    const SpaceMatchBackendOps *ops;
    SpaceMatchNativeState native;
    PathmapImportedState imported;
} SpaceMatchBackend;

typedef bool (*CettaMorkBindingsVisitor)(const Bindings *bindings, void *ctx);

typedef enum {
    SPACE_BRIDGE_IMPORT_ERROR = 0,
    SPACE_BRIDGE_IMPORT_OK = 1,
    SPACE_BRIDGE_IMPORT_NEEDS_TEXT_FALLBACK = 2,
} SpaceBridgeImportResult;

void space_match_backend_init(Space *s);
void space_match_backend_free(Space *s);
bool space_match_backend_try_set(Space *s, SpaceEngine kind);
bool space_match_backend_needs_atom_on_add(const Space *s, AtomId atom_id);
void space_match_backend_note_add(Space *s, AtomId atom_id, Atom *atom,
                                  uint32_t atom_idx);
void space_match_backend_note_remove(Space *s);
uint32_t space_match_backend_candidates(Space *s, Atom *pattern, uint32_t **out);
void space_match_backend_query(Space *s, Arena *a, Atom *query, SubstMatchSet *out);
const char *space_match_backend_name(const Space *s);
bool space_match_backend_supports_direct_bindings(const Space *s);
const char *space_match_backend_kind_name(SpaceEngine kind);
bool space_match_backend_kind_from_name(const char *name, SpaceEngine *out);
const char *space_match_backend_unavailable_reason(SpaceEngine kind);
bool space_match_backend_attach_act_file(Space *s, const char *path, uint64_t *out_loaded);
bool space_match_backend_materialize_attached(Space *s, Arena *persistent_arena);
bool space_match_backend_load_sexpr_chunk(Space *s, Arena *persistent_arena,
                                          const uint8_t *text, size_t len,
                                          uint64_t *out_added);
bool space_match_backend_remove_sexpr_chunk(Space *s, Arena *persistent_arena,
                                            const uint8_t *text, size_t len,
                                            uint64_t *out_removed);
bool space_match_backend_step(Space *s, Arena *persistent_arena,
                              uint64_t steps, uint64_t *out_performed);
bool space_match_backend_is_attached_compiled(const Space *s);
bool space_match_backend_bridge_space(Space *s,
                                      CettaMorkSpaceHandle **out_bridge);
uint32_t space_match_backend_logical_len(const Space *s);
/*
 * Structural bridge import contract:
 *   - Positive example: a ground bridge payload with a live TermUniverse
 *     materializes directly into canonical AtomIds.
 *   - Negative example: a var-bearing or no-universe payload pretending the
 *     structural bridge bytes preserve parser-level spelling semantics.
 */
SpaceBridgeImportResult space_match_backend_import_bridge_space(
    Space *dst,
    CettaMorkSpaceHandle *bridge,
    uint64_t *out_loaded);
bool space_match_backend_mork_query_bindings_direct(
    CettaMorkSpaceHandle *bridge,
    Arena *a,
    Atom *query,
    BindingSet *out);
bool space_match_backend_mork_visit_bindings_direct(
    CettaMorkSpaceHandle *bridge,
    Arena *a,
    Atom *query,
    CettaMorkBindingsVisitor visitor,
    void *ctx);
bool space_match_backend_mork_visit_conjunction_direct(
    CettaMorkSpaceHandle *bridge,
    Arena *a,
    Atom **patterns,
    uint32_t npatterns,
    const Bindings *seed,
    CettaMorkBindingsVisitor visitor,
    void *ctx);
bool space_match_backend_mork_query_conjunction_direct(
    CettaMorkSpaceHandle *bridge,
    Arena *a,
    Atom **patterns,
    uint32_t npatterns,
    const Bindings *seed,
    BindingSet *out);
void space_match_backend_print_inventory(FILE *out);

#endif /* CETTA_SPACE_MATCH_BACKEND_H */

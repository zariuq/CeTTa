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
        const char *name;
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
void space_match_backend_print_inventory(FILE *out);

#endif /* CETTA_SPACE_MATCH_BACKEND_H */

#ifndef CETTA_TERM_UNIVERSE_H
#define CETTA_TERM_UNIVERSE_H

#include "atom.h"
#include <stddef.h>
#include <stdint.h>

#ifndef CETTA_BUILD_WITH_TERM_UNIVERSE_DIAGNOSTICS
#define CETTA_BUILD_WITH_TERM_UNIVERSE_DIAGNOSTICS 0
#endif

/*
 * TermUniverse isolates persistent term ownership from evaluator-local
 * scratch allocation.
 *
 * Positive example:
 *   - `bind!`, `add-atom`, and persistent space cloning all store atoms
 *     through one policy seam.
 *
 * Negative example:
 *   - Open-coding `persistent ? persistent : a` and copy policy at every
 *     storage boundary in eval.c and backend code.
 */

typedef struct {
    uint64_t direct_constructor_leaf_hits;
    uint64_t direct_constructor_expr_hits;
    uint64_t legacy_top_down_stable_admissions;
    uint64_t direct_lookup_hits;
    uint64_t direct_lookup_misses;
    uint64_t lazy_decode_count;
    uint64_t legacy_hash_recompute_count;
    uint64_t store_format_migrations;
} CettaTermUniverseDiagnostics;

typedef enum {
    TERM_UNIVERSE_ERROR_NONE = 0,
    TERM_UNIVERSE_ERROR_ATOM_ID_EXHAUSTED = 1,
    TERM_UNIVERSE_ERROR_STORAGE_TOO_LARGE = 2,
    TERM_UNIVERSE_ERROR_ALLOCATION_FAILED = 3,
    TERM_UNIVERSE_ERROR_UNSUPPORTED_STORE_FORMAT = 4,
    TERM_UNIVERSE_ERROR_STORE_FORMAT_MIGRATION_FAILED = 5,
} TermUniverseError;

typedef enum {
    TERM_UNIVERSE_STORE_FORMAT_COMPACT32_V1 = 1,
    TERM_UNIVERSE_STORE_FORMAT_WIDE64_V1 = 2,
} TermUniverseStoreFormat;

typedef struct TermUniverse {
    Arena *persistent_arena;
    uint8_t *blob_pool;
    size_t blob_len, blob_cap;
    struct TermEntry *entries;
    size_t len, cap;
    uint64_t *intern_slots;
    size_t intern_mask;
    size_t intern_used;
    uint64_t *ptr_slots;
    size_t ptr_mask;
    size_t ptr_used;
    TermUniverseError last_error;
    TermUniverseStoreFormat store_format;
#if CETTA_BUILD_WITH_TERM_UNIVERSE_DIAGNOSTICS
    CettaTermUniverseDiagnostics diagnostics;
    uint64_t diag_atom_id_capacity_override;
    uint64_t diag_logical_atom_id_base_override;
#endif
} TermUniverse;

typedef uint64_t CettaAtomId;
typedef CettaAtomId AtomId;
typedef uint64_t CettaCount;
typedef uint64_t CettaIndex;

#define CETTA_ATOM_ID_NONE UINT64_MAX
#define CETTA_ATOM_ID_MAX ((AtomId)(UINT64_MAX - 1u))
#define CETTA_TERM_ENTRY_BLOB_NONE UINT64_MAX

typedef struct {
    uint8_t tag;
    uint8_t subtag;
    uint16_t arity_or_len;
    uint32_t sym_or_head;
    uint32_t aux32;
    uint32_t hash32;
} CettaTermHdr;

typedef struct TermEntry {
    uint64_t byte_off;
    uint64_t byte_len;
    Atom *decoded_cache;
} TermEntry;

_Static_assert(sizeof(AtomId) == 8, "AtomId must remain 64-bit");
_Static_assert(CETTA_ATOM_ID_NONE == UINT64_MAX, "AtomId sentinel must stay all-ones");
_Static_assert(sizeof(VarId) == 8, "VarId must remain 64-bit");
_Static_assert(sizeof(CettaTermHdr) == 16, "CettaTermHdr ABI drifted");
_Static_assert(offsetof(CettaTermHdr, tag) == 0, "CettaTermHdr.tag offset drifted");
_Static_assert(offsetof(CettaTermHdr, subtag) == 1, "CettaTermHdr.subtag offset drifted");
_Static_assert(offsetof(CettaTermHdr, arity_or_len) == 2,
               "CettaTermHdr.arity_or_len offset drifted");
_Static_assert(offsetof(CettaTermHdr, sym_or_head) == 4,
               "CettaTermHdr.sym_or_head offset drifted");
_Static_assert(offsetof(CettaTermHdr, aux32) == 8, "CettaTermHdr.aux32 offset drifted");
_Static_assert(offsetof(CettaTermHdr, hash32) == 12, "CettaTermHdr.hash32 offset drifted");

void term_universe_init(TermUniverse *universe);
bool term_universe_init_with_store_format(TermUniverse *universe,
                                          TermUniverseStoreFormat format);
void term_universe_free(TermUniverse *universe);
void term_universe_set_persistent_arena(TermUniverse *universe,
                                        Arena *persistent_arena);
void term_universe_diag_reset(TermUniverse *universe);
void term_universe_diag_snapshot(const TermUniverse *universe,
                                 CettaTermUniverseDiagnostics *out);
#if CETTA_BUILD_WITH_TERM_UNIVERSE_DIAGNOSTICS
void term_universe_diag_set_atom_id_capacity_override(TermUniverse *universe,
                                                      uint64_t capacity);
void term_universe_diag_set_logical_atom_id_base_override(
    TermUniverse *universe, uint64_t base);
#endif
void term_universe_clear_error(TermUniverse *universe);
TermUniverseError term_universe_last_error_code(const TermUniverse *universe);
const char *term_universe_error_name(TermUniverseError error);
TermUniverseStoreFormat term_universe_store_format(const TermUniverse *universe);
const char *term_universe_store_format_name(TermUniverseStoreFormat format);
uint32_t term_universe_store_format_version(TermUniverseStoreFormat format);
uint32_t term_universe_store_format_atom_id_width_bits(TermUniverseStoreFormat format);
uint64_t term_universe_atom_id_capacity(const TermUniverse *universe);
bool term_universe_migrate_store_format(TermUniverse *universe,
                                        TermUniverseStoreFormat format);
Atom *term_universe_canonicalize_atom(Arena *dst, Atom *src);
AtomId term_universe_store_atom_id(TermUniverse *universe, Arena *fallback,
                                   Atom *src);
AtomId term_universe_lookup_atom_id(const TermUniverse *universe, Atom *src);
bool term_universe_atom_id_eq(const TermUniverse *universe, AtomId id,
                              Atom *src);
Atom *term_universe_get_atom(const TermUniverse *universe, AtomId id);
Atom *term_universe_copy_atom(const TermUniverse *universe, Arena *dst,
                              AtomId id);
Atom *term_universe_copy_atom_epoch(const TermUniverse *universe, Arena *dst,
                                    AtomId id, uint32_t epoch);
char *term_universe_atom_to_string(Arena *a, const TermUniverse *universe,
                                   AtomId id);
char *term_universe_atom_to_parseable_string(Arena *a,
                                             const TermUniverse *universe,
                                             AtomId id);
Atom *term_universe_store_atom(TermUniverse *universe, Arena *fallback,
                               Atom *src);
const CettaTermHdr *tu_hdr(const TermUniverse *universe, AtomId id);
AtomKind tu_kind(const TermUniverse *universe, AtomId id);
uint32_t tu_arity(const TermUniverse *universe, AtomId id);
uint32_t tu_hash32(const TermUniverse *universe, AtomId id);
SymbolId tu_sym(const TermUniverse *universe, AtomId id);
VarId tu_var_id(const TermUniverse *universe, AtomId id);
SymbolId tu_head_sym(const TermUniverse *universe, AtomId id);
GroundedKind tu_ground_kind(const TermUniverse *universe, AtomId id);
int64_t tu_int(const TermUniverse *universe, AtomId id);
double tu_float(const TermUniverse *universe, AtomId id);
bool tu_bool(const TermUniverse *universe, AtomId id);
const char *tu_string_cstr(const TermUniverse *universe, AtomId id);
const char *tu_bigint_cstr(const TermUniverse *universe, AtomId id);
const char *tu_rational_cstr(const TermUniverse *universe, AtomId id);
AtomId tu_child(const TermUniverse *universe, AtomId id, uint32_t idx);
bool tu_has_vars(const TermUniverse *universe, AtomId id);

AtomId tu_intern_symbol(TermUniverse *universe, SymbolId sym_id);
AtomId tu_intern_var(TermUniverse *universe, SymbolId sym_id, VarId var_id);
AtomId tu_intern_int(TermUniverse *universe, int64_t value);
AtomId tu_intern_float(TermUniverse *universe, double value);
AtomId tu_intern_bool(TermUniverse *universe, bool value);
AtomId tu_intern_string(TermUniverse *universe, const char *value);
AtomId tu_intern_bigint(TermUniverse *universe, const char *value);
AtomId tu_intern_rational(TermUniverse *universe, const char *value);
AtomId tu_expr_from_ids(TermUniverse *universe, const AtomId *child_ids,
                        uint32_t arity);

#endif /* CETTA_TERM_UNIVERSE_H */

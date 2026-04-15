#ifndef CETTA_TERM_UNIVERSE_H
#define CETTA_TERM_UNIVERSE_H

#include "atom.h"
#include <stddef.h>

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

typedef struct TermUniverse {
    Arena *persistent_arena;
    uint8_t *blob_pool;
    uint32_t blob_len, blob_cap;
    struct TermEntry *entries;
    uint32_t len, cap;
    uint32_t *intern_slots;
    uint32_t intern_mask;
    uint32_t intern_used;
    uint32_t *ptr_slots;
    uint32_t ptr_mask;
    uint32_t ptr_used;
} TermUniverse;

typedef uint32_t AtomId;

#define CETTA_ATOM_ID_NONE UINT32_MAX
#define CETTA_TERM_ENTRY_BLOB_NONE UINT32_MAX

typedef struct {
    uint8_t tag;
    uint8_t subtag;
    uint16_t arity_or_len;
    uint32_t sym_or_head;
    uint32_t aux32;
    uint32_t hash32;
} CettaTermHdr;

typedef struct TermEntry {
    uint32_t byte_off;
    uint32_t byte_len;
    Atom *decoded_cache;
} TermEntry;

_Static_assert(sizeof(AtomId) == 4, "AtomId must remain 32-bit");
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
void term_universe_free(TermUniverse *universe);
void term_universe_set_persistent_arena(TermUniverse *universe,
                                        Arena *persistent_arena);
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
AtomId tu_child(const TermUniverse *universe, AtomId id, uint32_t idx);
bool tu_has_vars(const TermUniverse *universe, AtomId id);

#endif /* CETTA_TERM_UNIVERSE_H */

#ifndef CETTA_GSLT2PARSE_PARSER_PACK_GUARD_RELATION_V1_H
#define CETTA_GSLT2PARSE_PARSER_PACK_GUARD_RELATION_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "atom.h"
#include "lib_parse_native_grammar.h"

/*
 * One successful body derivation for a positive, zero-width parser guard.
 * The parser observes scalar_left/byte_left only.  The complete body extent,
 * semantic value, and proof identity remain in the evidence relation.
 */
typedef struct {
    uint32_t guard_terminal_id;
    uint32_t scalar_left;
    uint32_t body_scalar_right;
    uint32_t byte_left;
    uint32_t body_byte_right;
    const Atom *value;
    const Atom *proof;
} PPGuardRelationV1Match;

typedef struct {
    uint32_t guard_terminal_id;
    uint32_t scalar_left;
    uint32_t body_scalar_right;
    uint32_t byte_left;
    uint32_t body_byte_right;
    uint32_t witness_id;
    Atom *value;
    Atom *proof;
} PPGuardRelationV1Evidence;

typedef enum {
    PPGUARD_RELATION_V1_ATOMS_SELF_CONTAINED = 0,
    PPGUARD_RELATION_V1_ATOMS_BORROWED = 1
} PPGuardRelationV1AtomStorage;

/*
 * A lattice obtained without decoding the source again.  Exact duplicate
 * body rows collapse as relation-set duplicates.  Distinct proof identities
 * retain distinct witness IDs even when their values agree.
 *
 * The ordinary builder makes the Atom graphs self-contained.  The borrowed
 * builder owns every carrier/topology array but retains Atom graphs in the
 * declared owner arenas.  The caller guarantees that each immutable Atom
 * graph resides transitively in one declared owner; the builder checks every
 * root and checks groundness before sealing.  Those arenas must remain live
 * and unreset until the relation is freed.  Validation rejects a borrowed
 * root for which no declared owner remains active.
 * Storage mode is not semantic and therefore does not contribute to
 * relation_digest.
 */
typedef struct {
    Arena arena;
    CettaLpNativeUtf8Lattice lattice;
    uint32_t *terminal_ids;
    CettaLpNativeUtf8LatticeEdge *edges;
    uint32_t *start_offsets;
    uint32_t *codepoints;
    uint32_t *byte_offsets;
    Atom **witness_values;
    PPGuardRelationV1Evidence *evidence;
    const Arena **borrowed_atom_owners;
    uint32_t borrowed_atom_owner_len;
    PPGuardRelationV1AtomStorage atom_storage;
    uint32_t witness_len;
    uint32_t base_witness_len;
    uint32_t evidence_len;
    char source_digest[65];
    char relation_digest[65];
} PPGuardRelationV1;

void ppguard_relation_v1_init(PPGuardRelationV1 *relation);
void ppguard_relation_v1_free(PPGuardRelationV1 *relation);

bool ppguard_relation_v1_validate(
    const PPGuardRelationV1 *relation,
    char *error_buf,
    size_t error_buf_size);

bool ppguard_relation_v1_build(
    const CettaLpNativeUtf8Lattice *base_lattice,
    Atom *const *base_witness_values,
    uint32_t base_witness_len,
    const uint32_t *guard_terminal_ids,
    uint32_t guard_terminal_len,
    const PPGuardRelationV1Match *matches,
    uint32_t match_len,
    const char *source_digest,
    PPGuardRelationV1 *out,
    char *error_buf,
    size_t error_buf_size);

bool ppguard_relation_v1_build_borrowed_atoms(
    const CettaLpNativeUtf8Lattice *base_lattice,
    Atom *const *base_witness_values,
    uint32_t base_witness_len,
    const uint32_t *guard_terminal_ids,
    uint32_t guard_terminal_len,
    const PPGuardRelationV1Match *matches,
    uint32_t match_len,
    const Arena *const *atom_owners,
    uint32_t atom_owner_len,
    const char *source_digest,
    PPGuardRelationV1 *out,
    char *error_buf,
    size_t error_buf_size);

#endif

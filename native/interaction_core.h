#ifndef CETTA_NATIVE_INTERACTION_CORE_H
#define CETTA_NATIVE_INTERACTION_CORE_H

#include "atom.h"
#include "call_outcome.h"
#include "space.h"

#include <stdbool.h>
#include <stddef.h>

/*
 * One commit engine for every lane (src/interaction_core.c).
 *
 * A firing (transform REGION SOURCES TEMPLATE [DEST]) compiles its sources
 * to rows over the live store, each row a choice of occurrences: matched
 * reads keep or take one occurrence each, predicate queries (absent, unique, all)
 * dispose of nothing, `let` binds the values of code, `every` puts all rows
 * of its suffix into one commit.  A row is linear: nothing taken twice,
 * nothing both taken and kept, and every suffix row is linear on its own
 * while the whole commit must be linear too.  The commit is Enabled, then
 * fire: every region read is unchanged, the footprint is linear and
 * permitted, the takes leave by position, the makes arrive.
 *
 * A lane supplies how values reach code (held data on the Prime lane,
 * quoted on the PeTTa lane, as lib_transform quotes them) and, optionally,
 * how a `let` expression is evaluated now.  Without an evaluator the firing
 * suspends at a `let` and its caller resumes it (the Prime library driver).
 */

typedef enum {
    CETTA_INTERACTION_LANE_PRIME = 0,
    CETTA_INTERACTION_LANE_PETTA = 1,
} CettaInteractionLane;

typedef struct {
    CettaInteractionLane lane;
    void *context;
    /* Every value of a `let` expression, in order; false with *error_out
     * set when evaluation answers an error.  NULL: the firing suspends. */
    bool (*evaluate)(void *context, Arena *a, Atom *expression,
                     Atom ***values_out, size_t *len_out, Atom **error_out);
    /* The rows' template instances, one per row in row order, are the
     * answer, and the caller routes them (lib_transform's tf:fire answers
     * (tf:fired INSTANCES)); template items are not made by the commit.
     * Otherwise an item (add-atom R a) is a make of the commit and the
     * other items are the continuation. */
    bool instances_only;
    /* Values are substituted as they are: the caller has already protected
     * its code positions (lib_transform quotes the variables of its `let`
     * code and caller-evaluated templates before the sources run). */
    bool raw_values;
    /* The space a location names, the lane's way; false for an operand that
     * names no space, true with *space_out NULL for a location that holds
     * no space yet (it reads as empty).  NULL: spaces, handles and bound
     * symbols are resolved by the engine. */
    bool (*resolve_space)(void *context, Atom *location, Space **space_out);
} CettaInteractionHost;

typedef enum {
    CETTA_INTERACTION_FIRED = 0, /* *continuation_out: code to run after the commit,
                                  * or with instances_only the list of instances */
    CETTA_INTERACTION_NONE,      /* no candidate is enabled */
    CETTA_INTERACTION_ERROR,     /* *continuation_out is the error */
    CETTA_INTERACTION_SUSPENDED, /* *continuation_out is (ia:need MACHINE E) */
} CettaInteractionOutcome;

/* One firing now.  REGION is a value (a space or a handle); SOURCES and
 * TEMPLATE are syntax; DEST is a space, or NULL for the caller.  With a
 * destination the continuation answers how many instances were made. */
CettaInteractionOutcome cetta_interaction_fire(
    const CettaInteractionHost *host, Arena *a, Atom *region, Atom *sources,
    Atom *template_atom, Atom *dest, Atom **continuation_out);

/* ── helpers shared with other firing realizations ─────────────────────── */

/* Remove the occurrences of `space` at `positions` (ascending logical
 * positions of the current store; `atoms` the atoms there): by marks where
 * the space admits them, else by one stable contraction, else, for other
 * engines, by their exact atoms.  The survivors keep their relative order. */
bool cetta_interaction_remove_positions(Space *space, const CettaIndex *positions,
                                        Atom *const *atoms, size_t len);

/* A footprint's identification condition, by sorting: the first occurrence
 * (space, position) that is taken twice or both taken and kept, or none.
 * `spaces`, `positions` and `takes` describe len picks. */
bool cetta_interaction_picks_linear(Space *const *spaces, const CettaIndex *positions,
                                    const bool *takes, size_t len, size_t *conflict_out);

/* ── the library's operations ─────────────────────────────────────────── */

/* Operation OP of the interaction library, which names it
 * __cetta_lib_interaction_OP (lib/prime/interaction.metta,
 * native/interaction_module.c): regions and handles, the walk and the
 * commit, firing now, later or always, and the agenda.  False when OP is
 * not an operation or its arity is not one of OP's. */
bool cetta_interaction_core_call(Space *space, Arena *a, const char *op,
                                 Atom **args, uint32_t nargs,
                                 CettaCallOutcome *out);

#endif /* CETTA_NATIVE_INTERACTION_CORE_H */

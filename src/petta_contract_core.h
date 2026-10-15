#ifndef CETTA_PETTA_CONTRACT_CORE_H
#define CETTA_PETTA_CONTRACT_CORE_H

#include <stdbool.h>
#include <stdint.h>

#include "atom.h"
#include "petta_program.h"
#include "space.h"

/*
 * Versions of typed relations.
 *
 * A call of a typed relation runs its protocol: a guard per argument, the
 * body, a guard per result.  Each guard is a query of the relational type
 * relation; it filters, may bind the signature's private variables, and
 * multiplies answers by its number of derivations.  A trusted version runs
 * the relation's calls to itself without that protocol.  It is exact when
 * every skipped guard would succeed exactly once with an unread refinement:
 *
 *  - the declarations are functional (each symbol declared at most once,
 *    symbol subjects only, no authored get-type equation), so the relation
 *    has at most one answer in every mode;
 *  - entry facts, checked once where a checked protocol enters the
 *    relation, make each guard succeed and keep it succeeding: a ground
 *    argument keeps its accepted type under later bindings, and an
 *    uninterpreted argument (no symbol of it has a declaration, an equation
 *    or a builtin meaning) has a purely structural type, so the guard pair
 *    of a polymorphic parameter shared with the result accepts any instance
 *    of the argument exactly once;
 *  - the relation's equations preserve the entry facts at every call to
 *    themselves, return an instance of the polymorphic argument, and perform
 *    no mutation, so the facts stay valid while a trusted activation runs.
 */
typedef enum {
    PETTA_CONTRACT_ENTRY_ANY = 0,
    PETTA_CONTRACT_ENTRY_GROUND,
    PETTA_CONTRACT_ENTRY_UNINTERPRETED,
    PETTA_CONTRACT_ENTRY_UNINTERPRETED_SPACE,
    /* The argument is the function name constants[i], whose one declaration
     * fixes the signature's type variables it meets. */
    PETTA_CONTRACT_ENTRY_CONSTANT,
} PettaContractEntryFact;

enum {
    PETTA_CONTRACT_MAX_ARITY = 16u,
    PETTA_CONTRACT_MAX_SPACES = 4u,
    PETTA_CONTRACT_MAX_DISCHARGED = 32u,
};

/*
 * Beyond calls to itself, a version's equations may call other typed
 * functions.  A call whose arguments the facts prove to meet the callee's
 * own version (each closed-typed argument a ground value of that type, each
 * argument at a type variable the callee's signature names once anything)
 * passes every guard of the callee's protocol exactly once with a refinement
 * nobody reads, and the callee's version proves its result type: the call
 * enters that version directly.  Other typed calls to functions whose
 * equations do the same kinds of work keep their protocols.
 */
typedef struct {
    uint32_t arity;
    PettaContractEntryFact facts[PETTA_CONTRACT_MAX_ARITY];
    SymbolId constants[PETTA_CONTRACT_MAX_ARITY];
    /* Spaces the equations match by name; at entry, every symbol of every
     * row of each is uninterpreted. */
    SymbolId spaces[PETTA_CONTRACT_MAX_SPACES];
    uint32_t space_count;
    /* The typed calls of the equations that enter their callee's version:
     * occurrences of the program's equation atoms, read while the version
     * compiles. */
    const Atom *discharged[PETTA_CONTRACT_MAX_DISCHARGED];
    uint32_t discharged_count;
} PettaContractVersion;

/* Whether `call`, an occurrence in the version's equations, enters its
 * callee's version. */
bool petta_contract_version_discharges(const PettaContractVersion *version,
                                       const Atom *call);

/* Whether `head`/`arity`, a typed relation of `space`, has a trusted version;
 * fills `version` with its entry facts.  The answer describes the program
 * projection read now; callers hold it under that projection's token. */
bool petta_contract_trusted_version(PettaProgram *program, Space *space,
                                    SymbolId head, uint32_t arity,
                                    PettaContractVersion *version);

/* Whether `symbol` is uninterpreted in `space`: no type declaration, no
 * equation and no meaning the engine gives it at any arity.  The pairing
 * constructors `:` and `->` are uninterpreted when undeclared.  `scratch`
 * holds the probe atom. */
bool petta_contract_symbol_uninterpreted(Space *space, Arena *scratch,
                                         SymbolId symbol);

/* Whether every symbol of every atom of `data` is uninterpreted in
 * `program_space`. */
bool petta_contract_space_uninterpreted(Space *program_space, Space *data);

/* The largest number of arguments with which a call through a variable can
 * reach a callee of `space` that decides more about them than their values:
 * a function with an Atom parameter, which receives that argument as written
 * even when a partial application holds the arguments before it, or a
 * function declared several times, whose alternatives each evaluate their
 * own arguments.  A call with more arguments, or with none of these callees,
 * may apply its callee to evaluated arguments.  0 when no function of the
 * space is such a callee; UINT32_MAX when the space cannot answer.  The
 * answer describes the program projection read now. */
uint32_t petta_contract_callee_source_arity(Space *space);

/* Verdicts an evaluation reuses while it checks entry facts.  They hold for
 * one program projection of `program_space` and, for a data space, one of its
 * revisions; a changed projection or revision is judged again. */
typedef struct PettaContractMemo PettaContractMemo;
PettaContractMemo *petta_contract_memo_new(void);
void petta_contract_memo_free(PettaContractMemo *memo);
bool petta_contract_memo_symbol(PettaContractMemo *memo, Space *program_space,
                                SymbolId symbol);
bool petta_contract_memo_space(PettaContractMemo *memo, Space *program_space,
                               Space *data);

#endif

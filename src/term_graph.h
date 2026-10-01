#ifndef CETTA_TERM_GRAPH_H
#define CETTA_TERM_GRAPH_H

#include "atom.h"

#include <stdio.h>

/*
 * A rational term: a finite graph of Prolog cells whose unfolding may be
 * infinite (RationalTermGraph).  A node is a list cell (head, tail), a
 * compound (a name and ordered arguments; zero arguments is a compound,
 * never the atom of that name), the empty list, a leaf (a symbol, a number,
 * a string), or a parameter, one of the term's free variables.  Children are
 * ordered and repeats count.  A graph is built once, then sealed and shared
 * by reference count: an arena that holds an atom of one of its nodes keeps
 * it alive.
 *
 * A node's value is its node atom when the node reaches no parameter, and
 * otherwise its carrier (tag node value...), the values of the parameters it
 * reaches, in ascending order, as the carrier's own children.  So binding,
 * substitution and renaming reach a rational term's free variables as they
 * reach any other atom's, and the graph itself never changes.
 *
 * Only a term that is not a finite tree is a graph; a finite term stays an
 * ordinary atom, and nothing on its path pays for graphs.  Code that does
 * not read graphs sees a node as an opaque ground value.
 */
typedef enum {
    CETTA_TERM_GRAPH_LEAF = 0,
    CETTA_TERM_GRAPH_NIL,
    CETTA_TERM_GRAPH_CELL,
    CETTA_TERM_GRAPH_COMPOUND,
    CETTA_TERM_GRAPH_PARAM,
} CettaTermGraphKind;

#define CETTA_TERM_GRAPH_NO_NODE UINT32_MAX

CettaTermGraph *term_graph_new(void);
void term_graph_retain(void *graph);
void term_graph_release(void *graph);

/* Building.  A node's children are set after it is added, so a cycle can
 * point back at a node whose children are not set yet.  Each returns
 * CETTA_TERM_GRAPH_NO_NODE, or false, when out of memory. */
uint32_t term_graph_add_leaf(CettaTermGraph *graph, Atom *leaf);
uint32_t term_graph_add_param(CettaTermGraph *graph, uint32_t index);
uint32_t term_graph_add_nil(CettaTermGraph *graph);
uint32_t term_graph_add_cell(CettaTermGraph *graph);
uint32_t term_graph_add_compound(CettaTermGraph *graph, SymbolId name,
                                 uint32_t arity);
bool term_graph_set_child(CettaTermGraph *graph, uint32_t node,
                          uint32_t index, uint32_t child);
/* Every child is set; marks the nodes whose unfolding is infinite, those
 * that reach a cycle.  False when a child is missing. */
bool term_graph_seal(CettaTermGraph *graph);

uint32_t term_graph_node_count(const CettaTermGraph *graph);
CettaTermGraphKind term_graph_kind(const CettaTermGraph *graph,
                                   uint32_t node);
Atom *term_graph_leaf(const CettaTermGraph *graph, uint32_t node);
SymbolId term_graph_name(const CettaTermGraph *graph, uint32_t node);
uint32_t term_graph_arity(const CettaTermGraph *graph, uint32_t node);
uint32_t term_graph_child(const CettaTermGraph *graph, uint32_t node,
                          uint32_t index);
bool term_graph_infinite(const CettaTermGraph *graph, uint32_t node);
uint32_t term_graph_param(const CettaTermGraph *graph, uint32_t node);
/* One more than the greatest parameter index. */
uint32_t term_graph_param_count(const CettaTermGraph *graph);
/* The parameters a sealed graph's node reaches, ascending. */
const uint32_t *term_graph_node_params(const CettaTermGraph *graph,
                                       uint32_t node, uint32_t *count);

/* A sealed graph's reference record for `node`; NULL before sealing. */
const CettaTermGraphRef *term_graph_ref(const CettaTermGraph *graph,
                                        uint32_t node);

/* A node's value, `values[p]` the value of parameter p (NULL when the node
 * reaches none). */
Atom *term_graph_node_value(Arena *arena, CettaTermGraph *graph,
                            uint32_t node, Atom *const *values);
/* The node of a value, a node atom or a carrier, and the carrier's
 * arguments. */
bool term_graph_value_node(const Atom *atom, const CettaTermGraphRef **ref,
                           Atom *const **args, uint32_t *nargs);
/* The value of parameter `param` in a carrier; NULL when there is none. */
Atom *term_graph_value_param(const Atom *value, uint32_t param);

/* A node's term one level deep: a list cell whose spine ends in the empty
 * list is the expression of its elements, a spine that does not is its cons
 * cells, a compound is its PeTTa compound, the empty list (), a parameter
 * its value; below that level a node whose unfolding is finite is its
 * ordinary atom, and a node whose unfolding is infinite is its value. */
Atom *term_graph_open(Arena *arena, CettaTermGraph *graph, uint32_t node,
                      Atom *const *values);
/* `atom` opened one level when it is a node's value, else `atom` itself:
 * what a reader of term structure sees. */
Atom *term_graph_open_value(Arena *arena, Atom *atom);

/* Equality of two atoms that may hold rational terms at any depth:
 * bisimulation of their Prolog cells, leaves compared by `leaf_eq`
 * (RationalTermGraph.Bisimilar; PeTTa's == with its leaf equality is
 * RationalTermGraph.PeTTaEq).  Each atom is read as its cells: an expression
 * is its list, a PeTTa compound its compound, a cons its cell, a parameter
 * its value. */
bool term_graph_value_eq(Atom *left, Atom *right,
                         bool (*leaf_eq)(Atom *, Atom *));

/* A hash of an atom's unfolding, `leaf_hash` on its leaves: atoms that
 * term_graph_value_eq equates under a leaf equality `leaf_hash` respects
 * hash alike. */
uint32_t term_graph_view_hash(Atom *atom, uint32_t (*leaf_hash)(Atom *));

/* A set of node pairs assumed equal while two terms are unified: a pair met
 * again holds by that assumption, as in unification of rational trees. */
typedef struct {
    const void **slots;
    size_t cap;
    size_t len;
    /* The carriers met, each the key of the node, argument atoms and
     * binding context it has. */
    const Atom **carriers;
    uint64_t *carrier_contexts;
    size_t carrier_cap;
    size_t carrier_len;
} CettaTermGraphAssumptions;

/* The key a rational value is assumed by: its node when it reaches no free
 * variable, else the first carrier met of the same node with the same
 * argument atoms in the same binding context, so a carrier opened again is
 * the same key.  NULL when out of memory. */
const void *term_graph_assumption_key(CettaTermGraphAssumptions *assumptions,
                                      const Atom *value, uint64_t context);
/* Record the pair of keys; `*fresh` tells whether it was new.  False when
 * out of memory. */
bool term_graph_assume(CettaTermGraphAssumptions *assumptions,
                       const void *left, const void *right, bool *fresh);
void term_graph_assumptions_free(CettaTermGraphAssumptions *assumptions);

/* A node's term, finite: a node met again on its own path prints as ... */
void term_graph_print(const CettaTermGraphRef *ref, FILE *out, bool petta);

#endif /* CETTA_TERM_GRAPH_H */

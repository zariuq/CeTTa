#include "native_query.h"

#include "dense_relation.h"
#include "match.h"
#include "relational_plan.h"
#include "shared_transition.h"
#include "stats.h"
#include "term_universe.h"

#include <stdlib.h>
#include <string.h>

enum {
    NATIVE_QUERY_TEMPLATE_NODES = 64u,
    NATIVE_QUERY_VARIABLES = 64u,
    NATIVE_QUERY_LEGS = 64u,
    NATIVE_QUERY_LEG_ARITY = 16u,
    NATIVE_QUERY_EFFECTS = 16u,
    /* The variables of one stored row a one-way match may bind, and how
     * deep it follows the row's terms. */
    NATIVE_QUERY_ROW_VARIABLES = 32u,
    NATIVE_QUERY_ROW_DEPTH = 64u,
    /* Instances handed to the dialect per call while only additions run. */
    NATIVE_QUERY_CHUNK = 4096u,
};

typedef enum {
    NATIVE_QUERY_NODE_CONSTANT = 0,
    NATIVE_QUERY_NODE_VARIABLE,
    NATIVE_QUERY_NODE_EXPRESSION,
} NativeQueryNodeKind;

/* A template in preorder: an expression node is followed by its children. */
typedef struct {
    NativeQueryNodeKind kind;
    CettaExprLen arity;
    /* A constant's interned id (effects) and its atom (answers). */
    AtomId id;
    Atom *atom;
    /* A variable's position among the plan's variables. */
    size_t slot;
} NativeQueryNode;

typedef struct {
    NativeQueryNode nodes[NATIVE_QUERY_TEMPLATE_NODES];
    size_t len;
    bool ground;
} NativeQueryTemplate;

/* The query's variables, in the order a solution's values follow. */
typedef struct {
    VarId ids[NATIVE_QUERY_VARIABLES];
    size_t len;
} NativeQueryVariables;

static size_t native_query_variable(const NativeQueryVariables *variables,
                                    VarId id) {
    for (size_t i = 0u; i < variables->len; i++) {
        if (var_id_eq(variables->ids[i], id))
            return i;
    }
    return SIZE_MAX;
}

bool cetta_native_query_enabled(void) {
    static _Thread_local int enabled = -1;
    if (enabled < 0) {
        const char *reference = getenv("CETTA_NATIVE_QUERY_REFERENCE");
        enabled = !reference || !*reference || strcmp(reference, "0") == 0 ||
            strcmp(reference, "false") == 0 || strcmp(reference, "off") == 0;
    }
    return enabled != 0;
}

/* Every variable of the template is a variable of the plan; every other
 * leaf is a constant the term universe can hold. */
static bool native_query_compile_node(NativeQueryTemplate *template,
                                      const NativeQueryVariables *variables,
                                      TermUniverse *universe, Arena *arena,
                                      bool intern, Atom *atom) {
    if (!atom || template->len == NATIVE_QUERY_TEMPLATE_NODES)
        return false;
    NativeQueryNode *node = &template->nodes[template->len++];
    *node = (NativeQueryNode){.atom = atom};
    /* A ground part of the template, however deep, is one constant. */
    if (atom->kind == ATOM_EXPR && !atom_has_vars(atom)) {
        node->kind = NATIVE_QUERY_NODE_CONSTANT;
        if (!term_universe_atom_is_stable(atom))
            return false;
        if (intern) {
            node->id = term_universe_store_atom_id(universe, arena, atom);
            return node->id != CETTA_ATOM_ID_NONE;
        }
        return true;
    }
    switch (atom->kind) {
    case ATOM_VAR:
        template->ground = false;
        node->kind = NATIVE_QUERY_NODE_VARIABLE;
        node->slot = native_query_variable(variables, atom->var_id);
        return node->slot != SIZE_MAX;
    case ATOM_EXPR:
        node->kind = NATIVE_QUERY_NODE_EXPRESSION;
        node->arity = atom->expr.len;
        for (CettaExprIndex i = 0u; i < atom->expr.len; i++) {
            if (!native_query_compile_node(template, variables, universe,
                                           arena, intern, atom->expr.elems[i]))
                return false;
        }
        return true;
    default:
        node->kind = NATIVE_QUERY_NODE_CONSTANT;
        if (!term_universe_atom_is_stable(atom))
            return false;
        if (intern) {
            node->id = term_universe_store_atom_id(universe, arena, atom);
            return node->id != CETTA_ATOM_ID_NONE;
        }
        return true;
    }
}

static bool native_query_compile(NativeQueryTemplate *template,
                                 const NativeQueryVariables *variables,
                                 TermUniverse *universe, Arena *arena,
                                 bool intern, Atom *atom) {
    template->len = 0u;
    template->ground = true;
    return native_query_compile_node(template, variables, universe, arena,
                                     intern, atom);
}

/* ── Index nested loops ─────────────────────────────────────────────────
 * An acyclic query joins well one pattern at a time: each pattern, in an
 * order that reads bound variables first, asks the space's own indexes for
 * the rows that may match its bound form, and its rows bind the rest.  The
 * live space answers, so effects wait for the end of the enumeration. */

/* A pattern of the join: per element, the variable it binds or reads, or
 * the id of the atom it fixes (NONE when no stored row can hold it). */
/* A column whose pattern holds variables inside a term: a row's term there
 * is matched against it node by node. */
#define NATIVE_QUERY_NESTED (SIZE_MAX - 1u)

typedef struct {
    Atom *pattern;
    /* The space whose rows the leg reads. */
    Space *space;
    CettaExprLen len;
    size_t slot[NATIVE_QUERY_LEG_ARITY];
    AtomId fixed[NATIVE_QUERY_LEG_ARITY];
    /* Per nested column, its pattern in preorder. */
    NativeQueryTemplate *nested[NATIVE_QUERY_LEG_ARITY];
    bool nested_any;
} NativeQueryLeg;

/* The keys a leg read afresh (CettaNativeQuery.afresh) was read under: at
 * each visit, the values of its columns that the legs before it bound,
 * each distinct key once. */
typedef struct {
    /* The leg's columns holding a variable bound before it, in order; and
     * per column, the first column holding its variable. */
    CettaExprIndex columns[NATIVE_QUERY_LEG_ARITY];
    size_t width;
    CettaExprIndex first[NATIVE_QUERY_LEG_ARITY];
    /* The keys met, WIDTH ids each, and an open-addressed index of their
     * positions, SIZE_MAX marking a free slot; and whether the leg was
     * read at all. */
    AtomId *ids;
    size_t len, cap;
    size_t *slots;
    size_t slot_cap;
    bool read;
} NativeQueryKeys;

typedef struct {
    Space *space;
    TermUniverse *universe;
    Arena *arena;
    /* The probes, each reset once its leg has its rows, freed with the
     * query: none outlives it. */
    Arena probes;
    size_t order[NATIVE_QUERY_LEGS];
    size_t leg_count;
    AtomId values[NATIVE_QUERY_VARIABLES];
    bool bound[NATIVE_QUERY_VARIABLES];
    CettaRelationalVisit visit;
    void *context;
    uint64_t answers;
    /* A candidate row the loops cannot read by ids: one holding a variable,
     * or a value whose equality is not its identity. */
    bool declined;
    /* Expression ids already known to be exact values: an open-addressed
     * set, CETTA_ATOM_ID_NONE marking a free slot. */
    AtomId *exact;
    size_t exact_cap;
    size_t exact_len;
    /* Rows given as a list that a variable meets whole. */
    bool whole_rows;
    /* Per leg kept in a relation: whether rows of the space may match it
     * too (0 not yet asked, 1 none, 2 some). */
    uint8_t kept_rows[NATIVE_QUERY_LEGS];
    /* Per leg, the relation it reads (native_query_leg_form), found under
     * its space's dense epoch: 0 not yet looked for, 1 none, 2 FORMS. */
    uint8_t form_found[NATIVE_QUERY_LEGS];
    uint64_t form_epochs[NATIVE_QUERY_LEGS];
    SpaceDenseForm forms[NATIVE_QUERY_LEGS];
    /* Per depth: whether order[depth] is the leg of every visit, and the
     * visits sampled so far (native_query_loops_decide). */
    bool decided[NATIVE_QUERY_LEGS];
    uint8_t samples[NATIVE_QUERY_LEGS];
    /* Per depth: whether it was decided by legs meeting one row each, a
     * premise native_query_order_keep checks at every visit. */
    bool tentative[NATIVE_QUERY_LEGS];
    /* The legs read afresh, and per leg the keys such a leg was read
     * under (native_query_afresh_prepare). */
    uint64_t afresh;
    NativeQueryKeys *keys;
    /* The query's legs, as many as it has patterns. */
    NativeQueryLeg legs[];
} NativeQueryLoops;

/* Loops with room for `legs` legs, cleared: a query clears its own legs,
 * not the most a query may have. */
static NativeQueryLoops *native_query_loops_new(size_t legs) {
    if (legs == 0u || legs > NATIVE_QUERY_LEGS)
        return NULL;
    return calloc(1u, sizeof(NativeQueryLoops) + legs * sizeof(NativeQueryLeg));
}

static size_t native_query_exact_slot(AtomId id, size_t cap) {
    return (size_t)(((uint64_t)id * UINT64_C(0x9e3779b97f4a7c15)) >> 32) &
        (cap - 1u);
}

static bool native_query_exact_known(const NativeQueryLoops *loops, AtomId id) {
    if (loops->exact_cap == 0u)
        return false;
    for (size_t slot = native_query_exact_slot(id, loops->exact_cap);;
         slot = (slot + 1u) & (loops->exact_cap - 1u)) {
        if (loops->exact[slot] == id)
            return true;
        if (loops->exact[slot] == CETTA_ATOM_ID_NONE)
            return false;
    }
}

/* Remember an expression known to be an exact value; the set doubles at half
 * load.  Without room it remembers nothing, which costs only a recheck. */
static void native_query_exact_note(NativeQueryLoops *loops, AtomId id) {
    if (loops->exact_len + 1u > loops->exact_cap / 2u) {
        size_t cap = loops->exact_cap ? loops->exact_cap * 2u : 64u;
        AtomId *grown = malloc(cap * sizeof(*grown));
        if (!grown)
            return;
        for (size_t slot = 0u; slot < cap; slot++)
            grown[slot] = CETTA_ATOM_ID_NONE;
        for (size_t old = 0u; old < loops->exact_cap; old++) {
            AtomId kept = loops->exact[old];
            if (kept == CETTA_ATOM_ID_NONE)
                continue;
            size_t slot = native_query_exact_slot(kept, cap);
            while (grown[slot] != CETTA_ATOM_ID_NONE)
                slot = (slot + 1u) & (cap - 1u);
            grown[slot] = kept;
        }
        free(loops->exact);
        loops->exact = grown;
        loops->exact_cap = cap;
    }
    size_t slot = native_query_exact_slot(id, loops->exact_cap);
    while (loops->exact[slot] != CETTA_ATOM_ID_NONE) {
        if (loops->exact[slot] == id)
            return;
        slot = (slot + 1u) & (loops->exact_cap - 1u);
    }
    loops->exact[slot] = id;
    loops->exact_len++;
}

/* Whether the atom an id names is equal only to itself: a symbol, an
 * integer, a truth value, a string, or a ground expression of them. */
static bool native_query_exact_value(NativeQueryLoops *loops, AtomId id) {
    const CettaTermHdr *header = tu_hdr(loops->universe, id);
    if (!header)
        return false;
    if (header->tag == ATOM_SYMBOL)
        return true;
    if (header->tag == ATOM_GROUNDED)
        return header->subtag == GV_INT || header->subtag == GV_BOOL ||
            header->subtag == GV_STRING;
    if (header->tag != ATOM_EXPR || tu_has_vars(loops->universe, id))
        return false;
    if (native_query_exact_known(loops, id))
        return true;
    for (CettaExprIndex c = 0u; c < tu_arity(loops->universe, id); c++) {
        if (!native_query_exact_value(loops, tu_child(loops->universe, id, c)))
            return false;
    }
    native_query_exact_note(loops, id);
    return true;
}

/* Whether an atom a pattern fixes is equal only to itself. */
static bool native_query_exact_atom(Atom *atom) {
    if (atom->kind == ATOM_SYMBOL)
        return true;
    if (atom->kind == ATOM_GROUNDED)
        return atom->ground.gkind == GV_INT || atom->ground.gkind == GV_BOOL ||
            atom->ground.gkind == GV_STRING;
    if (atom->kind != ATOM_EXPR)
        return false;
    for (CettaExprIndex i = 0u; i < atom->expr.len; i++) {
        if (!native_query_exact_atom(atom->expr.elems[i]))
            return false;
    }
    return true;
}

/* A column pattern's variables, added to the query's in the order they
 * occur. */
static bool native_query_nested_variables(NativeQueryVariables *variables,
                                          Atom *atom) {
    if (atom->kind == ATOM_VAR) {
        if (native_query_variable(variables, atom->var_id) != SIZE_MAX)
            return true;
        if (variables->len == NATIVE_QUERY_VARIABLES)
            return false;
        variables->ids[variables->len++] = atom->var_id;
        return true;
    }
    if (atom->kind != ATOM_EXPR)
        return true;
    for (CettaExprIndex i = 0u; i < atom->expr.len; i++) {
        if (!native_query_nested_variables(variables, atom->expr.elems[i]))
            return false;
    }
    return true;
}

/* A column whose pattern holds variables inside a term, compiled to its
 * preorder: every part it fixes is an atom equal only to itself. */
static bool native_query_nested_prepare(NativeQueryLoops *loops,
                                        NativeQueryVariables *variables,
                                        NativeQueryLeg *leg,
                                        CettaExprIndex column, Atom *item) {
    if (atom_structural_may_have_list_carrier(item) ||
        atom_structural_may_have_internal_tag(item))
        return false;
    NativeQueryTemplate *nested = arena_alloc(loops->arena, sizeof(*nested));
    if (!nested || !native_query_nested_variables(variables, item) ||
        !native_query_compile(nested, variables, loops->universe, loops->arena,
                              true, item))
        return false;
    for (size_t n = 0u; n < nested->len; n++) {
        if (nested->nodes[n].kind == NATIVE_QUERY_NODE_CONSTANT &&
            !native_query_exact_atom(nested->nodes[n].atom))
            return false;
    }
    leg->nested[column] = nested;
    leg->slot[column] = NATIVE_QUERY_NESTED;
    leg->fixed[column] = CETTA_ATOM_ID_NONE;
    leg->nested_any = true;
    return true;
}

/* The legs of the patterns, and their variables; false when an element is
 * neither a variable, a ground exact atom, nor (where `nested` admits it) a
 * term holding variables whose fixed parts are exact atoms. */
static bool native_query_loops_prepare(NativeQueryLoops *loops,
                                       NativeQueryVariables *variables,
                                       Atom *const *patterns,
                                       Space *const *sources, size_t count,
                                       bool nested) {
    if (count > NATIVE_QUERY_LEGS)
        return false;
    for (size_t i = 0u; i < count; i++) {
        Atom *pattern = patterns[i];
        if (!pattern || pattern->kind != ATOM_EXPR || pattern->expr.len == 0u ||
            pattern->expr.len > NATIVE_QUERY_LEG_ARITY)
            return false;
        NativeQueryLeg *leg = &loops->legs[i];
        *leg = (NativeQueryLeg){
            .pattern = pattern, .len = pattern->expr.len,
            .space = sources ? sources[i] : loops->space,
        };
        for (CettaExprIndex c = 0u; c < pattern->expr.len; c++) {
            Atom *item = pattern->expr.elems[c];
            if (item->kind == ATOM_VAR) {
                size_t slot = native_query_variable(variables, item->var_id);
                if (slot == SIZE_MAX) {
                    if (variables->len == NATIVE_QUERY_VARIABLES)
                        return false;
                    slot = variables->len;
                    variables->ids[variables->len++] = item->var_id;
                }
                leg->slot[c] = slot;
                continue;
            }
            if (item->kind == ATOM_EXPR && atom_has_vars(item)) {
                if (!nested ||
                    !native_query_nested_prepare(loops, variables, leg, c, item))
                    return false;
                continue;
            }
            if (atom_has_vars(item) || !native_query_exact_atom(item))
                return false;
            leg->slot[c] = SIZE_MAX;
            /* Stored, not only looked up: a row holding variables may match
             * a ground atom no row holds, which then has no id to compare. */
            leg->fixed[c] = term_universe_store_atom_id(loops->universe, NULL,
                                                        item);
        }
    }
    loops->leg_count = count;
    /* Read first the leg most fixed by atoms and bound variables. */
    bool used[NATIVE_QUERY_LEGS] = {false};
    bool bound[NATIVE_QUERY_VARIABLES] = {false};
    for (size_t depth = 0u; depth < count; depth++) {
        size_t best = SIZE_MAX;
        size_t best_fixed = 0u;
        for (size_t i = 0u; i < count; i++) {
            if (used[i])
                continue;
            size_t fixed = 0u;
            for (CettaExprIndex c = 0u; c < loops->legs[i].len; c++) {
                size_t slot = loops->legs[i].slot[c];
                fixed += slot == SIZE_MAX || slot == NATIVE_QUERY_NESTED ||
                    bound[slot];
            }
            if (best == SIZE_MAX || fixed > best_fixed) {
                best = i;
                best_fixed = fixed;
            }
        }
        used[best] = true;
        loops->order[depth] = best;
        for (CettaExprIndex c = 0u; c < loops->legs[best].len; c++) {
            size_t slot = loops->legs[best].slot[c];
            if (slot == NATIVE_QUERY_NESTED) {
                const NativeQueryTemplate *nested = loops->legs[best].nested[c];
                for (size_t n = 0u; n < nested->len; n++) {
                    if (nested->nodes[n].kind == NATIVE_QUERY_NODE_VARIABLE)
                        bound[nested->nodes[n].slot] = true;
                }
            } else if (slot != SIZE_MAX) {
                bound[slot] = true;
            }
        }
    }
    return true;
}

/* A stored row's variables, as a one-way match binds them: each variable's
 * id in the universe and the ground value it took. */
typedef struct {
    AtomId vars[NATIVE_QUERY_ROW_VARIABLES];
    AtomId values[NATIVE_QUERY_ROW_VARIABLES];
    size_t len;
} NativeQueryRowBindings;

/* Match a stored term that may hold variables against a ground value,
 * binding the term's variables.  It is unification with a ground term,
 * which binds nothing in that term, so no occurs check is needed.  False
 * when they differ, or with *FULL when the bindings outgrow their room. */
static bool native_query_match_ground(const TermUniverse *universe,
                                      AtomId pattern, AtomId value,
                                      NativeQueryRowBindings *bindings,
                                      unsigned depth, bool *full) {
    if (pattern == value)
        return true;
    const CettaTermHdr *header = tu_hdr(universe, pattern);
    if (!header)
        return false;
    if (header->tag == ATOM_VAR) {
        for (size_t i = 0u; i < bindings->len; i++) {
            if (bindings->vars[i] == pattern)
                return bindings->values[i] == value;
        }
        if (bindings->len == NATIVE_QUERY_ROW_VARIABLES) {
            *full = true;
            return false;
        }
        bindings->vars[bindings->len] = pattern;
        bindings->values[bindings->len++] = value;
        return true;
    }
    /* Ground atoms equal only by their ids, and those differ. */
    if (header->tag != ATOM_EXPR || !tu_has_vars(universe, pattern))
        return false;
    if (depth == NATIVE_QUERY_ROW_DEPTH) {
        *full = true;
        return false;
    }
    const CettaTermHdr *ground = tu_hdr(universe, value);
    CettaExprLen arity = tu_arity(universe, pattern);
    if (!ground || ground->tag != ATOM_EXPR ||
        tu_arity(universe, value) != arity)
        return false;
    for (CettaExprIndex c = 0u; c < arity; c++) {
        if (!native_query_match_ground(universe, tu_child(universe, pattern, c),
                                       tu_child(universe, value, c), bindings,
                                       depth + 1u, full))
            return false;
    }
    return true;
}

/* A stored term under a one-way match's bindings, interned: NONE while a
 * variable of it is unbound. */
static AtomId native_query_row_instance(TermUniverse *universe, AtomId pattern,
                                        const NativeQueryRowBindings *bindings,
                                        unsigned depth) {
    const CettaTermHdr *header = tu_hdr(universe, pattern);
    if (!header)
        return CETTA_ATOM_ID_NONE;
    if (header->tag == ATOM_VAR) {
        for (size_t i = 0u; i < bindings->len; i++) {
            if (bindings->vars[i] == pattern)
                return bindings->values[i];
        }
        return CETTA_ATOM_ID_NONE;
    }
    if (header->tag != ATOM_EXPR || !tu_has_vars(universe, pattern))
        return pattern;
    CettaExprLen arity = tu_arity(universe, pattern);
    if (depth == NATIVE_QUERY_ROW_DEPTH || arity > NATIVE_QUERY_TEMPLATE_NODES)
        return CETTA_ATOM_ID_NONE;
    AtomId children[NATIVE_QUERY_TEMPLATE_NODES];
    for (CettaExprIndex c = 0u; c < arity; c++) {
        children[c] = native_query_row_instance(
            universe, tu_child(universe, pattern, c), bindings, depth + 1u);
        if (children[c] == CETTA_ATOM_ID_NONE)
            return CETTA_ATOM_ID_NONE;
    }
    return tu_expr_from_ids(universe, children, arity);
}

/* A ground row's term in a nested column against the column's pattern,
 * from node *NODE on: every part the pattern fixes is the term's there, by
 * id, and every variable takes the term's subterm there, which must be
 * equal only to itself; a variable bound before must meet the same
 * subterm.  Each variable it binds is listed in FRESH.  A subterm that is
 * not such a value declines the query. */
static bool native_query_nested_match(NativeQueryLoops *loops,
                                      const NativeQueryTemplate *pattern,
                                      size_t *node, AtomId term,
                                      size_t *fresh, size_t *fresh_len) {
    const NativeQueryNode *at = &pattern->nodes[(*node)++];
    switch (at->kind) {
    case NATIVE_QUERY_NODE_CONSTANT:
        return term == at->id;
    case NATIVE_QUERY_NODE_VARIABLE:
        if (loops->bound[at->slot])
            return loops->values[at->slot] == term;
        if (!native_query_exact_value(loops, term)) {
            loops->declined = true;
            return false;
        }
        loops->values[at->slot] = term;
        loops->bound[at->slot] = true;
        fresh[(*fresh_len)++] = at->slot;
        return true;
    case NATIVE_QUERY_NODE_EXPRESSION: {
        /* A term kept only in decoded form is not read by ids. */
        const CettaTermHdr *header = tu_hdr(loops->universe, term);
        if (!header) {
            loops->declined = true;
            return false;
        }
        if (header->tag != ATOM_EXPR ||
            tu_arity(loops->universe, term) != at->arity)
            return false;
        /* A private carrier's logical shape is not its encoding. */
        if (at->arity != 0u &&
            tu_internal_tag(loops->universe,
                            tu_child(loops->universe, term, 0u)) != 0) {
            loops->declined = true;
            return false;
        }
        for (CettaExprIndex c = 0u; c < at->arity; c++) {
            if (!native_query_nested_match(loops, pattern, node,
                                           tu_child(loops->universe, term, c),
                                           fresh, fresh_len))
                return false;
        }
        return true;
    }
    }
    return false;
}

/* A nested column's pattern from node *NODE on, the variables bound so far
 * standing for their values: a probe the space's indexes select by. */
static Atom *native_query_nested_probe(NativeQueryLoops *loops,
                                       const NativeQueryTemplate *pattern,
                                       size_t *node) {
    const NativeQueryNode *at = &pattern->nodes[(*node)++];
    if (at->kind == NATIVE_QUERY_NODE_CONSTANT)
        return at->atom;
    if (at->kind == NATIVE_QUERY_NODE_VARIABLE)
        return loops->bound[at->slot]
            ? term_universe_view_atom(loops->universe, &loops->probes,
                                      loops->values[at->slot])
            : at->atom;
    Atom *children[NATIVE_QUERY_TEMPLATE_NODES];
    for (CettaExprLen c = 0u; c < at->arity; c++) {
        children[c] = native_query_nested_probe(loops, pattern, node);
        if (!children[c])
            return NULL;
    }
    return atom_expr(&loops->probes, children, at->arity);
}

static bool native_query_loops_leg(NativeQueryLoops *loops, size_t depth);

/* A row holding variables meets the leg one way.  Each column the leg
 * fixes or has bound is ground, and the row's column must match it,
 * binding the row's variables; each column the leg leaves free takes the
 * row's column under those bindings, which must come out ground and equal
 * only to itself.  A row that leaves a free column open declines the
 * query: its answer would hold a variable, which flat bindings cannot
 * carry. */
static bool native_query_open_row(NativeQueryLoops *loops,
                                  const NativeQueryLeg *leg, AtomId row,
                                  size_t depth) {
    NativeQueryRowBindings bindings;
    bindings.len = 0u;
    bool full = false;
    for (CettaExprIndex c = 0u; c < leg->len; c++) {
        size_t slot = leg->slot[c];
        AtomId value = slot == SIZE_MAX ? leg->fixed[c]
            : loops->bound[slot] ? loops->values[slot] : CETTA_ATOM_ID_NONE;
        if (value == CETTA_ATOM_ID_NONE)
            continue;
        if (!native_query_match_ground(loops->universe,
                                       tu_child(loops->universe, row, c),
                                       value, &bindings, 0u, &full)) {
            if (!full)
                return true;
            loops->declined = true;
            return false;
        }
    }
    size_t fresh[NATIVE_QUERY_LEG_ARITY];
    size_t fresh_len = 0u;
    bool ok = true;
    bool matched = true;
    for (CettaExprIndex c = 0u; matched && c < leg->len; c++) {
        size_t slot = leg->slot[c];
        if (slot == SIZE_MAX)
            continue;
        bool own = false;
        for (size_t i = 0u; i < fresh_len; i++)
            own = own || fresh[i] == slot;
        if (loops->bound[slot] && !own)
            continue;
        AtomId value = native_query_row_instance(
            loops->universe, tu_child(loops->universe, row, c), &bindings, 0u);
        if (value == CETTA_ATOM_ID_NONE ||
            !native_query_exact_value(loops, value)) {
            loops->declined = true;
            ok = false;
            matched = false;
        } else if (own) {
            /* The leg repeats the variable: the row's columns must agree. */
            matched = loops->values[slot] == value;
        } else {
            loops->values[slot] = value;
            loops->bound[slot] = true;
            fresh[fresh_len++] = slot;
        }
    }
    if (matched)
        ok = native_query_loops_leg(loops, depth + 1u);
    for (size_t i = 0u; i < fresh_len; i++)
        loops->bound[fresh[i]] = false;
    return ok;
}

/* The relation a leg reads when its space keeps its head's facts in one: a
 * leg (h x1 ... xk) whose head is fixed, of the relation's columns.  Kept
 * per leg while its space's dense epoch stands: the relation's place moves
 * only with the epoch. */
static bool native_query_leg_form(NativeQueryLoops *loops,
                                  const NativeQueryLeg *leg,
                                  SpaceDenseForm *form) {
    size_t index = (size_t)(leg - loops->legs);
    uint64_t epoch = space_dense_epoch(leg->space);
    if (cetta_shared_transition_scope_active())
        return false;
    if (loops->form_found[index] != 0u && loops->form_epochs[index] == epoch) {
        *form = loops->forms[index];
        return loops->form_found[index] == 2u;
    }
    bool found = leg->len >= 2u && leg->len <= CETTA_TUPLE_MAX_ARITY + 1u &&
        !leg->nested_any && leg->slot[0] == SIZE_MAX &&
        leg->fixed[0] != CETTA_ATOM_ID_NONE &&
        tu_kind(loops->universe, leg->fixed[0]) == ATOM_SYMBOL &&
        space_dense_form(leg->space, tu_sym(loops->universe, leg->fixed[0]),
                         form) &&
        (CettaExprLen)form->arity + 1u == leg->len;
    loops->form_found[index] = found ? 2u : 1u;
    loops->form_epochs[index] = epoch;
    if (found)
        loops->forms[index] = *form;
    return found;
}

enum { NATIVE_QUERY_INLINE_FACTS = 32u };

/* Facts of a relation, ARITY ids each. */
typedef struct {
    CettaDenseId inline_items[NATIVE_QUERY_INLINE_FACTS];
    CettaDenseId *items;
    size_t len, cap;
    uint32_t arity;
    bool failed;
} NativeQueryFacts;

static void native_query_facts_init(NativeQueryFacts *facts, uint32_t arity) {
    facts->items = facts->inline_items;
    facts->len = 0u;
    facts->cap = NATIVE_QUERY_INLINE_FACTS;
    facts->arity = arity;
    facts->failed = false;
}

static void native_query_facts_free(NativeQueryFacts *facts) {
    if (facts->items != facts->inline_items)
        free(facts->items);
}

static size_t native_query_facts_count(const NativeQueryFacts *facts) {
    return facts->arity ? facts->len / facts->arity : 0u;
}

static bool native_query_facts_push(void *context, const CettaDenseId *tuple) {
    NativeQueryFacts *facts = context;
    if (facts->len + facts->arity > facts->cap) {
        size_t cap = facts->cap * 2u;
        CettaDenseId *items = facts->items == facts->inline_items
            ? malloc(cap * sizeof(*items))
            : realloc(facts->items, cap * sizeof(*items));
        if (!items) {
            facts->failed = true;
            return false;
        }
        if (facts->items == facts->inline_items)
            memcpy(items, facts->inline_items, facts->len * sizeof(*items));
        facts->items = items;
        facts->cap = cap;
    }
    memcpy(facts->items + facts->len, tuple, facts->arity * sizeof(*tuple));
    facts->len += facts->arity;
    return true;
}

/* The columns the leg's fixed and bound places select in FIXED, one per
 * column of the relation; false when an atom no row holds selects
 * nothing. */
static bool native_query_kept_columns(const NativeQueryLoops *loops,
                                      const NativeQueryLeg *leg,
                                      CettaDenseId *fixed) {
    for (CettaExprIndex c = 1u; c < leg->len; c++) {
        size_t slot = leg->slot[c];
        fixed[c - 1u] = CETTA_DENSE_ANY;
        if (slot == SIZE_MAX) {
            if (leg->fixed[c] == CETTA_ATOM_ID_NONE)
                return false;
            fixed[c - 1u] = leg->fixed[c];
        } else if (loops->bound[slot]) {
            fixed[c - 1u] = loops->values[slot];
        }
    }
    return true;
}

/* The facts of FORM the leg's fixed and bound columns select, taken before
 * any is visited: a visit's effects may change the relation. */
static bool native_query_kept_select(const NativeQueryLoops *loops,
                                     const NativeQueryLeg *leg,
                                     SpaceDenseForm form,
                                     NativeQueryFacts *facts) {
    if (form.bits)
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_NATIVE_QUERY_BITS_LEG);
    CettaDenseId fixed[CETTA_TUPLE_MAX_ARITY];
    if (!native_query_kept_columns(loops, leg, fixed))
        return true;
    return space_dense_form_each(form, fixed, native_query_facts_push,
                                 facts) &&
           !facts->failed;
}

/* Visit the selected facts as rows (h c1 ... ck) of the leg. */
static bool native_query_kept_visit(NativeQueryLoops *loops,
                                    const NativeQueryLeg *leg,
                                    const NativeQueryFacts *facts,
                                    size_t depth) {
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_NATIVE_QUERY_LOOP_ROW,
                            native_query_facts_count(facts));
    bool ok = true;
    for (size_t i = 0u; ok && i < facts->len; i += facts->arity) {
        const CettaDenseId *tuple = facts->items + i;
        size_t fresh[CETTA_TUPLE_MAX_ARITY];
        size_t fresh_len = 0u;
        bool matched = true;
        for (CettaExprIndex c = 1u; matched && c < leg->len; c++) {
            size_t slot = leg->slot[c];
            AtomId column = (AtomId)tuple[c - 1u];
            if (slot == SIZE_MAX)
                continue;
            if (loops->bound[slot]) {
                matched = loops->values[slot] == column;
            } else {
                loops->values[slot] = column;
                loops->bound[slot] = true;
                fresh[fresh_len++] = slot;
            }
        }
        if (matched)
            ok = native_query_loops_leg(loops, depth + 1u);
        for (size_t f = 0u; f < fresh_len; f++)
            loops->bound[fresh[f]] = false;
    }
    return ok;
}

/* Whether rows of the space may match a leg kept in a relation besides its
 * facts.  Asked once a query: no effect is made while its loops run. */
static bool native_query_kept_rows(NativeQueryLoops *loops, size_t leg) {
    if (loops->kept_rows[leg] == 0u) {
        /* A head none of whose facts is a row, in a space with no open row,
         * answers at once; the candidates would demand a full index. */
        const NativeQueryLeg *l = &loops->legs[leg];
        if (space_dense_head_rows_absent(l->space,
                                         tu_sym(loops->universe, l->fixed[0]))) {
            loops->kept_rows[leg] = 1u;
            return false;
        }
        CettaIndex *rows = NULL;
        space_dense_reading_begin();
        CettaIndex count = space_match_candidates64(
            l->space, l->pattern, &rows);
        space_dense_reading_end();
        free(rows);
        loops->kept_rows[leg] = count != 0u ? 2u : 1u;
    }
    return loops->kept_rows[leg] == 2u;
}

/* The candidate rows of LEG under the bindings in hand, which fix every
 * atom the leg names: a flat leg asks its space by the ids of its columns
 * (space_match_candidates_ids64), and a leg the ids do not answer asks by
 * its pattern, each bound variable standing for its value.  False when the
 * probe cannot be made. */
static bool native_query_leg_rows(NativeQueryLoops *loops,
                                  const NativeQueryLeg *leg,
                                  CettaIndex **rows, CettaIndex *count) {
    if (!leg->nested_any && leg->slot[0] == SIZE_MAX) {
        AtomId columns[NATIVE_QUERY_LEG_ARITY];
        for (CettaExprIndex c = 0u; c < leg->len; c++) {
            size_t slot = leg->slot[c];
            columns[c] = slot == SIZE_MAX ? leg->fixed[c]
                : loops->bound[slot] ? loops->values[slot]
                : CETTA_ATOM_ID_NONE;
        }
        if (space_match_candidates_ids64(leg->space, loops->universe, columns,
                                         leg->len, rows, count))
            return true;
    }
    ArenaMark mark = arena_mark(&loops->probes);
    Atom **elements =
        arena_alloc(&loops->probes, sizeof(*elements) * leg->len);
    if (!elements)
        return false;
    for (CettaExprIndex c = 0u; c < leg->len; c++) {
        size_t slot = leg->slot[c];
        size_t node = 0u;
        elements[c] = slot == NATIVE_QUERY_NESTED
            ? native_query_nested_probe(loops, leg->nested[c], &node)
            : slot != SIZE_MAX && loops->bound[slot]
            /* A probe lives as long as its leg's rows: a bound value is a
             * view in the probes' arena, which the universe keeps no copy
             * of. */
            ? term_universe_view_atom(loops->universe, &loops->probes,
                                      loops->values[slot])
            : leg->pattern->expr.elems[c];
        if (!elements[c]) {
            arena_reset(&loops->probes, mark);
            return false;
        }
    }
    Atom *probe = atom_expr(&loops->probes, elements, leg->len);
    *count = probe ? space_match_candidates64(leg->space, probe, rows) : 0u;
    arena_reset(&loops->probes, mark);
    return probe != NULL;
}

#define NATIVE_QUERY_NO_COUNT ((CettaIndex)-1)

/* How many candidate rows LEG's probe under the bindings in hand meets
 * (native_query_leg_rows): none when a column fixes an atom no row holds;
 * NATIVE_QUERY_NO_COUNT when the probe cannot be made. */
static CettaIndex native_query_leg_candidates(NativeQueryLoops *loops,
                                              const NativeQueryLeg *leg) {
    for (CettaExprIndex c = 0u; c < leg->len; c++) {
        if (leg->slot[c] == SIZE_MAX && leg->fixed[c] == CETTA_ATOM_ID_NONE)
            return 0u;
    }
    CettaIndex *rows = NULL;
    CettaIndex count = 0u;
    if (!native_query_leg_rows(loops, leg, &rows, &count))
        return NATIVE_QUERY_NO_COUNT;
    /* A row holding a variable binds the leg's variables to open values,
     * which every later leg then meets by unification: such a leg counts
     * nothing, and is never preferred for its count.  The first sixteen
     * candidates tell, as a head's rows are ground or open alike. */
    bool open = false;
    for (CettaIndex r = 0u; !open && count != NATIVE_QUERY_NO_COUNT &&
         r < count && r < 16u; r++) {
        const CettaTermHdr *hdr = tu_hdr(
            loops->universe, space_get_atom_id_at64(leg->space, rows[r]));
        open = !hdr || (hdr->aux32 & CETTA_TERM_HDR_HAS_VARS) != 0u;
    }
    free(rows);
    if (count != NATIVE_QUERY_NO_COUNT)
        cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_NATIVE_QUERY_LOOP_ROW,
                                count);
    return open ? NATIVE_QUERY_NO_COUNT : count;
}

static bool native_query_kept_rows(NativeQueryLoops *loops, size_t leg);

/* The rows leg INDEX meets under the bindings in hand.  A leg kept in a
 * relation counts the facts its fixed and bound columns select, which reads
 * no row, and the rows besides them when its space keeps copies of the
 * head's facts as rows. */
static CettaIndex native_query_leg_count(NativeQueryLoops *loops,
                                         size_t index) {
    const NativeQueryLeg *leg = &loops->legs[index];
    SpaceDenseForm form;
    if (!native_query_leg_form(loops, leg, &form))
        return native_query_leg_candidates(loops, leg);
    CettaDenseId fixed[CETTA_TUPLE_MAX_ARITY];
    /* An atom no row holds selects nothing. */
    if (!native_query_kept_columns(loops, leg, fixed))
        return 0u;
    uint64_t facts = space_dense_form_count(form, fixed);
    if (native_query_kept_rows(loops, index)) {
        CettaIndex rows = native_query_leg_candidates(loops, leg);
        if (rows == NATIVE_QUERY_NO_COUNT)
            return NATIVE_QUERY_NO_COUNT;
        facts += rows;
    }
    return facts >= (uint64_t)NATIVE_QUERY_NO_COUNT
        ? NATIVE_QUERY_NO_COUNT - 1u : (CettaIndex)facts;
}

/* Whether legs A and B meet alike under the bindings in hand: the same
 * space and head, atoms in the same columns, and variables bound and free
 * in the same columns, whatever atoms besides the head they fix. */
static bool native_query_legs_alike(const NativeQueryLoops *loops,
                                    const NativeQueryLeg *a,
                                    const NativeQueryLeg *b) {
    if (a->space != b->space || a->len != b->len || a->nested_any ||
        b->nested_any)
        return false;
    for (CettaExprIndex c = 0u; c < a->len; c++) {
        size_t slot_a = a->slot[c];
        size_t slot_b = b->slot[c];
        if ((slot_a == SIZE_MAX) != (slot_b == SIZE_MAX))
            return false;
        if (slot_a == SIZE_MAX) {
            if (c == 0u && a->fixed[0] != b->fixed[0])
                return false;
        } else if (loops->bound[slot_a] != loops->bound[slot_b]) {
            return false;
        }
    }
    return true;
}

/* Visits a depth's probes may sample before its order is decided. */
#define NATIVE_QUERY_ORDER_SAMPLES 8u

static bool native_query_kept_rows(NativeQueryLoops *loops, size_t leg);

/* Whether legs A and B leave the same variables unbound: whichever is read,
 * the depths below see the same bindings. */
static bool native_query_legs_bind_alike(const NativeQueryLoops *loops,
                                         const NativeQueryLeg *a,
                                         const NativeQueryLeg *b) {
    const NativeQueryLeg *legs[2] = {a, b};
    for (size_t side = 0u; side < 2u; side++) {
        const NativeQueryLeg *leg = legs[side];
        const NativeQueryLeg *other = legs[1u - side];
        for (CettaExprIndex c = 0u; c < leg->len; c++) {
            size_t slot = leg->slot[c];
            if (slot == SIZE_MAX || slot == NATIVE_QUERY_NESTED ||
                loops->bound[slot])
                continue;
            bool held = false;
            for (CettaExprIndex d = 0u; d < other->len && !held; d++)
                held = other->slot[d] == slot;
            if (!held)
                return false;
        }
    }
    return true;
}

/* Tied legs all kept in relations, with no copies as rows, that leave the same
 * variables unbound are chosen again at every visit: their counts are
 * cheap, each visit reads the leg meeting the fewest rows and tests its
 * answers against the others (generic join), and the depths below keep
 * their decisions, since they see the same bindings either way.  A choice
 * kept from the first visit pays, on skewed data, the large leg of every
 * visit that meets it: on a star, the hub's degree at each of its edges. */
static bool native_query_tied_per_visit(NativeQueryLoops *loops,
                                        const size_t *tied, size_t tied_len) {
    if (tied_len < 2u)
        return false;
    const NativeQueryLeg *first = &loops->legs[loops->order[tied[0]]];
    for (size_t t = 0u; t < tied_len; t++) {
        size_t index = loops->order[tied[t]];
        const NativeQueryLeg *leg = &loops->legs[index];
        SpaceDenseForm form;
        if (!native_query_leg_form(loops, leg, &form) ||
            native_query_kept_rows(loops, index))
            return false;
        if (t > 0u && !native_query_legs_bind_alike(loops, first, leg))
            return false;
    }
    return true;
}

/* The leg the loops read at DEPTH, of those no shallower depth reads.  It
 * is one most fixed by atoms and bound variables, as
 * native_query_loops_prepare orders them; among several fixed alike, each
 * visit probes them under the bindings in hand (native_query_leg_count:
 * a leg kept in a relation counts its facts).  A probe meeting no row means the
 * visit answers nothing.  A probe meeting fewer rows than every other's
 * decides: its leg is the leg of every later visit, and each deeper depth
 * decides again.  Otherwise the visit reads the first of them, which
 * NATIVE_QUERY_ORDER_SAMPLES such visits decide.  A leg alike one already
 * probed (native_query_legs_alike) takes its count.  A leg holding a nested
 * column keeps the first.  False when the visit answers nothing. */
static bool native_query_loops_decide(NativeQueryLoops *loops, size_t depth) {
    size_t tied[NATIVE_QUERY_LEGS];
    size_t tied_len = 0u;
    size_t most = 0u;
    bool probe = true;
    for (size_t at = depth; at < loops->leg_count; at++) {
        const NativeQueryLeg *leg = &loops->legs[loops->order[at]];
        size_t fixed = 0u;
        for (CettaExprIndex c = 0u; c < leg->len; c++) {
            size_t slot = leg->slot[c];
            fixed += slot == SIZE_MAX || slot == NATIVE_QUERY_NESTED ||
                loops->bound[slot];
        }
        if (tied_len > 0u && fixed < most)
            continue;
        if (fixed > most)
            tied_len = 0u;
        most = fixed;
        tied[tied_len++] = at;
        probe = probe && !leg->nested_any;
    }
    size_t best = tied_len > 0u ? tied[0] : depth;
    bool clear = !probe || tied_len < 2u;
    bool single = false;
    bool per_visit = !clear && native_query_tied_per_visit(loops, tied, tied_len);
    if (!clear) {
        CettaIndex fewest = NATIVE_QUERY_NO_COUNT;
        CettaIndex next = NATIVE_QUERY_NO_COUNT;
        CettaIndex counts[NATIVE_QUERY_LEGS];
        for (size_t t = 0u; t < tied_len; t++) {
            /* A leg alike one probed this visit meets as many rows. */
            size_t alike = t;
            for (size_t u = 0u; alike == t && u < t; u++) {
                if (native_query_legs_alike(
                        loops, &loops->legs[loops->order[tied[u]]],
                        &loops->legs[loops->order[tied[t]]]))
                    alike = u;
            }
            CettaIndex count = alike < t
                ? counts[alike]
                : native_query_leg_count(loops, loops->order[tied[t]]);
            counts[t] = count;
            if (count == 0u)
                return false;
            if (count < fewest) {
                next = fewest;
                fewest = count;
                best = tied[t];
            } else if (count < next) {
                next = count;
            }
            /* No leg does better than one row short of none: the probes
             * stop, and the leg is kept while it meets one row. */
            if (fewest <= 1u)
                break;
        }
        if (per_visit) {
            if (best != depth) {
                size_t chosen = loops->order[best];
                loops->order[best] = loops->order[depth];
                loops->order[depth] = chosen;
            }
            loops->decided[depth] = false;
            loops->tentative[depth] = false;
            return true;
        }
        /* A leg meeting one row: as good as any, while it meets one row
         * (native_query_order_keep). */
        single = fewest <= 1u;
        clear = fewest < next || single ||
            ++loops->samples[depth] >= NATIVE_QUERY_ORDER_SAMPLES;
        if (!clear)
            best = tied[0];
    }
    if (best != depth) {
        size_t chosen = loops->order[best];
        loops->order[best] = loops->order[depth];
        loops->order[depth] = chosen;
        for (size_t deeper = depth + 1u; deeper < loops->leg_count; deeper++) {
            loops->decided[deeper] = false;
            loops->tentative[deeper] = false;
            loops->samples[deeper] = 0u;
        }
    }
    loops->decided[depth] = clear;
    loops->tentative[depth] = clear && single;
    return true;
}

/* Join orders the loops decided, by the shape of their query: the legs'
 * spaces and heads, the columns an atom fixes and the variables, numbered
 * as the legs first read them; queries differing in their constants alone
 * share an order, as a prepared statement does.
 * A query of a remembered shape starts from its order, which answers as
 * any order does; one execution in NATIVE_QUERY_ORDER_REFRESH samples
 * again, as the rows change.  Two shapes meeting in one entry cost an
 * order, never an answer. */
#define NATIVE_QUERY_ORDER_MEMO 64u
#define NATIVE_QUERY_ORDER_REFRESH 64u

typedef struct {
    uint64_t shape;
    uint32_t uses;
    size_t leg_count;
    uint8_t order[NATIVE_QUERY_LEGS];
    bool decided[NATIVE_QUERY_LEGS];
    bool tentative[NATIVE_QUERY_LEGS];
} NativeQueryOrderMemo;

static _Thread_local NativeQueryOrderMemo
    native_query_order_memo[NATIVE_QUERY_ORDER_MEMO];

static uint64_t native_query_order_mix(uint64_t h, uint64_t v) {
    h ^= v + UINT64_C(0x9e3779b97f4a7c15) + (h << 6) + (h >> 2);
    return h;
}

/* The loops' shape, and their remembered order when one is; zero for one
 * leg, which has no order to choose. */
static uint64_t native_query_order_recall(NativeQueryLoops *loops) {
    if (loops->leg_count < 2u) {
        loops->decided[0] = true;
        return 0u;
    }
    uint64_t shape = native_query_order_mix(0u, loops->leg_count);
    for (size_t i = 0u; i < loops->leg_count; i++) {
        const NativeQueryLeg *leg = &loops->legs[i];
        shape = native_query_order_mix(shape, (uint64_t)(uintptr_t)leg->space);
        shape = native_query_order_mix(shape, leg->len);
        for (CettaExprIndex c = 0u; c < leg->len; c++) {
            size_t slot = leg->slot[c];
            shape = native_query_order_mix(
                shape, slot == SIZE_MAX ? (c == 0u ? ((uint64_t)leg->fixed[c] << 2) | 1u : 1u)
                     : slot == NATIVE_QUERY_NESTED
                         ? ((uint64_t)leg->nested[c]->len << 2) | 2u
                         : ((uint64_t)slot << 2) | 3u);
        }
    }
    shape |= 1u;
    NativeQueryOrderMemo *memo =
        &native_query_order_memo[shape % NATIVE_QUERY_ORDER_MEMO];
    if (memo->shape == shape && memo->leg_count == loops->leg_count &&
        ++memo->uses % NATIVE_QUERY_ORDER_REFRESH != 0u) {
        for (size_t depth = 0u; depth < loops->leg_count; depth++) {
            loops->order[depth] = memo->order[depth];
            loops->decided[depth] = memo->decided[depth];
            loops->tentative[depth] = memo->tentative[depth];
        }
    }
    return shape;
}

static void native_query_order_remember(const NativeQueryLoops *loops,
                                        uint64_t shape) {
    if (shape == 0u)
        return;
    NativeQueryOrderMemo *memo =
        &native_query_order_memo[shape % NATIVE_QUERY_ORDER_MEMO];
    if (memo->shape != shape) {
        memo->shape = shape;
        memo->uses = 0u;
    }
    memo->leg_count = loops->leg_count;
    for (size_t depth = 0u; depth < loops->leg_count; depth++) {
        memo->order[depth] = (uint8_t)loops->order[depth];
        memo->decided[depth] = loops->decided[depth];
        memo->tentative[depth] = loops->tentative[depth];
    }
}

/* A depth decided by legs meeting one row each keeps its leg while the leg
 * meets one row; a visit where it meets more decides the depth again at
 * its next visit. */
static void native_query_order_keep(NativeQueryLoops *loops, size_t depth,
                                    uint64_t met) {
    if (loops->tentative[depth] && met > 1u) {
        loops->decided[depth] = false;
        loops->tentative[depth] = false;
        loops->samples[depth] = 0u;
    }
}

static size_t native_query_keys_hash(const AtomId *key, size_t width) {
    uint64_t hash = UINT64_C(0x9e3779b97f4a7c15);
    for (size_t k = 0u; k < width; k++)
        hash = (hash ^ (uint64_t)key[k]) * UINT64_C(0xff51afd7ed558ccd);
    return (size_t)(hash ^ (hash >> 32));
}

/* The slot of KEY in the index of KEYS: the slot holding it, *found
 * true, or the free slot it would take. */
static size_t native_query_keys_slot(const NativeQueryKeys *keys,
                                     const AtomId *key, bool *found) {
    size_t mask = keys->slot_cap - 1u;
    for (size_t slot = native_query_keys_hash(key, keys->width) & mask;;
         slot = (slot + 1u) & mask) {
        size_t at = keys->slots[slot];
        if (at == SIZE_MAX ||
            memcmp(keys->ids + at * keys->width, key,
                   keys->width * sizeof(*key)) == 0) {
            *found = at != SIZE_MAX;
            return slot;
        }
    }
}

static bool native_query_keys_contain(const NativeQueryKeys *keys,
                                      const AtomId *key) {
    if (keys->width == 0u)
        return keys->read;
    bool found = false;
    if (keys->slot_cap != 0u)
        (void)native_query_keys_slot(keys, key, &found);
    return found;
}

/* Remember KEY once; the index doubles at half load.  False when it
 * cannot grow. */
static bool native_query_keys_add(NativeQueryKeys *keys, const AtomId *key) {
    bool found = false;
    if (keys->slot_cap != 0u) {
        (void)native_query_keys_slot(keys, key, &found);
        if (found)
            return true;
    }
    if ((keys->len + 1u) * 2u > keys->slot_cap) {
        size_t cap = keys->slot_cap ? keys->slot_cap * 2u : 64u;
        size_t *slots = malloc(cap * sizeof(*slots));
        if (!slots)
            return false;
        for (size_t slot = 0u; slot < cap; slot++)
            slots[slot] = SIZE_MAX;
        free(keys->slots);
        keys->slots = slots;
        keys->slot_cap = cap;
        for (size_t at = 0u; at < keys->len; at++) {
            bool again = false;
            keys->slots[native_query_keys_slot(
                keys, keys->ids + at * keys->width, &again)] = at;
        }
    }
    if (keys->len == keys->cap) {
        size_t cap = keys->cap ? keys->cap * 2u : 64u;
        AtomId *ids = realloc(keys->ids,
                              cap * keys->width * sizeof(*keys->ids));
        if (!ids)
            return false;
        keys->ids = ids;
        keys->cap = cap;
    }
    memcpy(keys->ids + keys->len * keys->width, key,
           keys->width * sizeof(*key));
    keys->slots[native_query_keys_slot(keys, key, &found)] = keys->len++;
    return true;
}

/* A leg read afresh is read in turn under the bindings in hand: note its
 * key. */
static bool native_query_afresh_note(NativeQueryLoops *loops, size_t leg) {
    NativeQueryKeys *keys = &loops->keys[leg];
    keys->read = true;
    if (keys->width == 0u)
        return true;
    const NativeQueryLeg *read = &loops->legs[leg];
    AtomId key[NATIVE_QUERY_LEG_ARITY];
    for (size_t k = 0u; k < keys->width; k++)
        key[k] = loops->values[read->slot[keys->columns[k]]];
    return native_query_keys_add(keys, key);
}

/* Legs read afresh are read as the dialect reads them: each after the
 * legs before it, in their order, whose bindings its key records.  The
 * legs after the last of them are ordered as any.  False when such a leg
 * holds a nested column, whose instances the key does not describe. */
static bool native_query_afresh_prepare(NativeQueryLoops *loops,
                                        uint64_t afresh) {
    loops->keys = calloc(loops->leg_count, sizeof(*loops->keys));
    if (!loops->keys)
        return false;
    loops->afresh = afresh;
    size_t last = 63u - (size_t)__builtin_clzll(afresh);
    bool bound[NATIVE_QUERY_VARIABLES] = {false};
    for (size_t leg = 0u; leg < loops->leg_count; leg++) {
        loops->order[leg] = leg;
        loops->decided[leg] = leg <= last;
        loops->tentative[leg] = false;
        loops->samples[leg] = 0u;
        const NativeQueryLeg *read = &loops->legs[leg];
        if (afresh >> leg & 1u) {
            if (read->nested_any)
                return false;
            NativeQueryKeys *keys = &loops->keys[leg];
            for (CettaExprIndex c = 0u; c < read->len; c++) {
                size_t slot = read->slot[c];
                keys->first[c] = c;
                if (slot == SIZE_MAX)
                    continue;
                if (bound[slot]) {
                    keys->columns[keys->width++] = c;
                    continue;
                }
                for (CettaExprIndex d = 0u; d < c; d++) {
                    if (read->slot[d] == slot) {
                        keys->first[c] = d;
                        break;
                    }
                }
            }
        }
        for (CettaExprIndex c = 0u; c < read->len; c++) {
            size_t slot = read->slot[c];
            if (slot == NATIVE_QUERY_NESTED) {
                const NativeQueryTemplate *nested = read->nested[c];
                for (size_t n = 0u; n < nested->len; n++) {
                    if (nested->nodes[n].kind == NATIVE_QUERY_NODE_VARIABLE)
                        bound[nested->nodes[n].slot] = true;
                }
            } else if (slot != SIZE_MAX) {
                bound[slot] = true;
            }
        }
    }
    return true;
}

static void native_query_afresh_free(NativeQueryLoops *loops) {
    for (size_t leg = 0u; loops->keys && leg < loops->leg_count; leg++) {
        free(loops->keys[leg].ids);
        free(loops->keys[leg].slots);
    }
    free(loops->keys);
    loops->keys = NULL;
}

static bool native_query_loops_leg(NativeQueryLoops *loops, size_t depth) {
    if (depth == loops->leg_count) {
        if (loops->answers == UINT64_MAX)
            return false;
        loops->answers++;
        return loops->visit(loops->context, loops->values, 1u);
    }
    if ((loops->afresh >> depth & 1u) && depth > 0u &&
        !native_query_afresh_note(loops, depth))
        return false;
    if (!loops->decided[depth] && !native_query_loops_decide(loops, depth))
        return true;
    const NativeQueryLeg *leg = &loops->legs[loops->order[depth]];
    /* A leg kept in a relation that no row can match reads its facts
     * alone. */
    SpaceDenseForm form;
    bool kept = native_query_leg_form(loops, leg, &form);
    if (kept && !native_query_kept_rows(loops, loops->order[depth])) {
        NativeQueryFacts facts;
        native_query_facts_init(&facts, form.arity);
        bool ok = native_query_kept_select(loops, leg, form, &facts);
        native_query_order_keep(loops, depth, native_query_facts_count(&facts));
        ok = ok && native_query_kept_visit(loops, leg, &facts, depth);
        native_query_facts_free(&facts);
        return ok;
    }
    for (CettaExprIndex c = 0u; c < leg->len; c++) {
        if (leg->slot[c] == SIZE_MAX && leg->fixed[c] == CETTA_ATOM_ID_NONE)
            return true;
    }
    /* A head the space keeps in a relation: its facts, and any copies kept
     * as rows, both taken before either is visited. */
    NativeQueryFacts facts;
    native_query_facts_init(&facts, kept ? form.arity : 1u);
    if (kept && !native_query_kept_select(loops, leg, form, &facts)) {
        native_query_facts_free(&facts);
        return false;
    }
    if (kept)
        space_dense_reading_begin();
    CettaIndex *rows = NULL;
    CettaIndex row_count = 0u;
    bool probed = native_query_leg_rows(loops, leg, &rows, &row_count);
    if (kept)
        space_dense_reading_end();
    native_query_order_keep(loops, depth,
                            (uint64_t)row_count +
                                (kept ? native_query_facts_count(&facts) : 0u));
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_NATIVE_QUERY_LOOP_ROW,
                            row_count);
    if (!probed) {
        native_query_facts_free(&facts);
        free(rows);
        return false;
    }
    bool ok = !kept || native_query_kept_visit(loops, leg, &facts, depth);
    native_query_facts_free(&facts);
    size_t fresh[NATIVE_QUERY_VARIABLES];
    for (CettaIndex r = 0u; ok && r < row_count; r++) {
        AtomId id = space_get_atom_id_at64(leg->space, rows[r]);
        if (!tu_hdr(loops->universe, id) ||
            tu_kind(loops->universe, id) == ATOM_VAR) {
            loops->declined = true;
            ok = false;
            break;
        }
        if (tu_kind(loops->universe, id) != ATOM_EXPR ||
            tu_arity(loops->universe, id) != leg->len)
            continue;
        /* A row an atom the pattern fixes refutes cannot match, whatever
         * variables it holds; a row holding one that could unify with the
         * pattern is not read by ids. */
        bool refuted = false;
        for (CettaExprIndex c = 0u; !refuted && c < leg->len; c++) {
            AtomId child = tu_child(loops->universe, id, c);
            refuted = leg->slot[c] == SIZE_MAX && child != leg->fixed[c] &&
                !tu_has_vars(loops->universe, child);
        }
        if (refuted)
            continue;
        if (tu_has_vars(loops->universe, id)) {
            /* A nested column meets a row holding variables both ways. */
            if (leg->nested_any) {
                loops->declined = true;
                ok = false;
                break;
            }
            ok = native_query_open_row(loops, leg, id, depth);
            continue;
        }
        size_t fresh_len = 0u;
        bool matched = true;
        for (CettaExprIndex c = 0u; matched && c < leg->len; c++) {
            AtomId child = tu_child(loops->universe, id, c);
            size_t slot = leg->slot[c];
            if (slot == SIZE_MAX) {
                matched = child == leg->fixed[c];
            } else if (slot == NATIVE_QUERY_NESTED) {
                size_t node = 0u;
                matched = native_query_nested_match(loops, leg->nested[c], &node,
                                                    child, fresh, &fresh_len);
                ok = !loops->declined;
            } else if (loops->bound[slot]) {
                matched = child == loops->values[slot];
            } else if (!native_query_exact_value(loops, child)) {
                loops->declined = true;
                ok = false;
                matched = false;
            } else {
                loops->values[slot] = child;
                loops->bound[slot] = true;
                fresh[fresh_len++] = slot;
            }
        }
        if (matched)
            ok = native_query_loops_leg(loops, depth + 1u);
        for (size_t i = 0u; i < fresh_len; i++)
            loops->bound[fresh[i]] = false;
    }
    free(rows);
    return ok;
}

/* The rows given as a list: each element the one leg fits binds its
 * variables, and the loop past the last leg answers. */
static bool native_query_rows_leg(NativeQueryLoops *loops, Atom *rows) {
    const NativeQueryLeg *leg = &loops->legs[0];
    size_t fresh[NATIVE_QUERY_LEG_ARITY];
    for (CettaExprIndex r = 0u; r < rows->expr.len; r++) {
        Atom *row = rows->expr.elems[r];
        if (loops->whole_rows) {
            loops->values[0] = term_universe_store_atom_id(
                loops->universe, loops->arena, row);
            if (loops->values[0] == CETTA_ATOM_ID_NONE) {
                loops->declined = true;
                return false;
            }
            loops->bound[0] = true;
            bool ok = native_query_loops_leg(loops, 1u);
            loops->bound[0] = false;
            if (!ok)
                return false;
            continue;
        }
        if (row->kind != ATOM_EXPR || row->expr.len != leg->len)
            continue;
        size_t fresh_len = 0u;
        bool matched = true;
        for (CettaExprIndex c = 0u; matched && c < leg->len; c++) {
            AtomId child = term_universe_store_atom_id(
                loops->universe, loops->arena, row->expr.elems[c]);
            size_t slot = leg->slot[c];
            if (child == CETTA_ATOM_ID_NONE) {
                loops->declined = true;
                return false;
            } else if (slot == SIZE_MAX) {
                matched = child == leg->fixed[c];
            } else if (loops->bound[slot]) {
                matched = child == loops->values[slot];
            } else {
                loops->values[slot] = child;
                loops->bound[slot] = true;
                fresh[fresh_len++] = slot;
            }
        }
        bool ok = !matched || native_query_loops_leg(loops, 1u);
        for (size_t i = 0u; i < fresh_len; i++)
            loops->bound[fresh[i]] = false;
        if (!ok)
            return false;
    }
    return true;
}

/* Whether every element of a list of rows is an atom equal only to itself,
 * so meeting a pattern compares ids. */
static bool native_query_rows_exact(Atom *rows) {
    if (!rows || rows->kind != ATOM_EXPR || atom_has_vars(rows))
        return false;
    for (CettaExprIndex r = 0u; r < rows->expr.len; r++) {
        if (!native_query_exact_atom(rows->expr.elems[r]))
            return false;
    }
    return true;
}

static AtomId native_query_instance_id(const NativeQueryTemplate *template,
                                       size_t *position,
                                       TermUniverse *universe,
                                       const AtomId *values) {
    const NativeQueryNode *node = &template->nodes[(*position)++];
    switch (node->kind) {
    case NATIVE_QUERY_NODE_CONSTANT:
        return node->id;
    case NATIVE_QUERY_NODE_VARIABLE:
        return values[node->slot];
    case NATIVE_QUERY_NODE_EXPRESSION:
        break;
    }
    AtomId children[NATIVE_QUERY_TEMPLATE_NODES];
    for (CettaExprIndex i = 0u; i < node->arity; i++) {
        children[i] = native_query_instance_id(template, position, universe,
                                               values);
        if (children[i] == CETTA_ATOM_ID_NONE)
            return CETTA_ATOM_ID_NONE;
    }
    return tu_expr_from_ids(universe, children, node->arity);
}

/* The id of the instance of `template` under `values` when the universe
 * holds it, CETTA_ATOM_ID_NONE when it does not: nothing is stored. */
static AtomId native_query_lookup_id(const NativeQueryTemplate *template,
                                     size_t *position,
                                     const TermUniverse *universe,
                                     const AtomId *values) {
    const NativeQueryNode *node = &template->nodes[(*position)++];
    switch (node->kind) {
    case NATIVE_QUERY_NODE_CONSTANT:
        return node->id;
    case NATIVE_QUERY_NODE_VARIABLE:
        return values[node->slot];
    case NATIVE_QUERY_NODE_EXPRESSION:
        break;
    }
    AtomId children[NATIVE_QUERY_TEMPLATE_NODES];
    bool absent = false;
    for (CettaExprIndex i = 0u; i < node->arity; i++) {
        children[i] = native_query_lookup_id(template, position, universe,
                                             values);
        absent |= children[i] == CETTA_ATOM_ID_NONE;
    }
    return absent ? CETTA_ATOM_ID_NONE
                  : tu_expr_lookup_ids(universe, children, node->arity);
}

static Atom *native_query_instance_atom(const NativeQueryTemplate *template,
                                        size_t *position,
                                        TermUniverse *universe, Arena *arena,
                                        const AtomId *values) {
    const NativeQueryNode *node = &template->nodes[(*position)++];
    switch (node->kind) {
    case NATIVE_QUERY_NODE_CONSTANT:
        return node->atom;
    case NATIVE_QUERY_NODE_VARIABLE:
        return term_universe_get_atom(universe, values[node->slot]);
    case NATIVE_QUERY_NODE_EXPRESSION:
        break;
    }
    Atom **elements = node->arity
        ? arena_alloc(arena, sizeof(*elements) * node->arity) : NULL;
    if (node->arity && !elements)
        return NULL;
    for (CettaExprIndex i = 0u; i < node->arity; i++) {
        elements[i] = native_query_instance_atom(template, position, universe,
                                                 arena, values);
        if (!elements[i])
            return NULL;
    }
    return atom_expr(arena, elements, node->arity);
}

typedef struct {
    AtomId *ids;
    size_t len;
    size_t cap;
} NativeQueryIds;

static bool native_query_ids_push(NativeQueryIds *ids, AtomId id) {
    if (ids->len == ids->cap) {
        size_t cap = ids->cap ? ids->cap * 2u : 256u;
        if (cap > SIZE_MAX / sizeof(*ids->ids))
            return false;
        AtomId *grown = realloc(ids->ids, cap * sizeof(*grown));
        if (!grown)
            return false;
        ids->ids = grown;
        ids->cap = cap;
    }
    ids->ids[ids->len++] = id;
    return true;
}

typedef struct {
    const CettaNativeQuery *query;
    const NativeQueryTemplate *answer;
    const NativeQueryTemplate *effects;
    /* A guarded query's guard, and the answer of the branch a row meeting
     * the guard takes (CettaNativeQuery.guard_space); NULL otherwise. */
    const NativeQueryTemplate *guard;
    const NativeQueryTemplate *else_answer;
    TermUniverse *universe;
    Arena *arena;
    /* Additions alone are handed to the dialect in chunks as they come;
     * otherwise each effect's instances wait for the end of the
     * enumeration, in `deferred`. */
    bool deferred;
    NativeQueryIds *pending;
    NativeQueryIds *waiting;
    /* The additions made by pairs, whose instances wait as the two ids of
     * their columns. */
    bool *by_tuples;
    /* Answers that are not one ground atom. */
    Atom **items;
    size_t item_len;
    size_t item_cap;
    /* Whether any effect has been handed to the dialect. */
    bool made;
    /* The slots of the template's variable heads, whose values each
     * solution asks the host about (CettaNativeQuery.inert_heads), and the
     * values last answered, by their ids' low bits. */
    size_t inert_slots[4];
    size_t inert_slot_count;
    AtomId inert_seen[16];
    bool inert_ok[16];
    /* A distinct query's key (CettaNativeQuery.distinct_key), and the ids
     * of the instances it has met, open-addressed. */
    const NativeQueryTemplate *distinct;
    AtomId *met;
    size_t met_len, met_cap;
    /* The loops, when they enumerate: legs read afresh keep their keys
     * there. */
    NativeQueryLoops *loops;
} NativeQueryRun;

/* Whether the instance `id` of a distinct query's key is new: recorded,
 * *fresh is true; met before, false.  False when the set cannot grow. */
static bool native_query_distinct_fresh(NativeQueryRun *run, AtomId id,
                                        bool *fresh) {
    if ((run->met_len + 1u) * 2u > run->met_cap) {
        size_t cap = run->met_cap ? run->met_cap * 2u : 256u;
        AtomId *grown = malloc(sizeof(*grown) * cap);
        if (!grown)
            return false;
        for (size_t i = 0u; i < cap; i++)
            grown[i] = CETTA_ATOM_ID_NONE;
        for (size_t i = 0u; i < run->met_cap; i++) {
            AtomId old = run->met[i];
            if (old == CETTA_ATOM_ID_NONE)
                continue;
            size_t slot = (size_t)((old * UINT64_C(0x9e3779b97f4a7c15)) >> 32) &
                (cap - 1u);
            while (grown[slot] != CETTA_ATOM_ID_NONE)
                slot = (slot + 1u) & (cap - 1u);
            grown[slot] = old;
        }
        free(run->met);
        run->met = grown;
        run->met_cap = cap;
    }
    size_t slot = (size_t)((id * UINT64_C(0x9e3779b97f4a7c15)) >> 32) &
        (run->met_cap - 1u);
    while (run->met[slot] != CETTA_ATOM_ID_NONE) {
        if (run->met[slot] == id) {
            *fresh = false;
            return true;
        }
        slot = (slot + 1u) & (run->met_cap - 1u);
    }
    run->met[slot] = id;
    run->met_len++;
    *fresh = true;
    return true;
}

/* Whether every variable head of the template has a value that cannot be
 * applied in this solution. */
static bool native_query_heads_inert(NativeQueryRun *run,
                                     const AtomId *values) {
    for (size_t i = 0u; i < run->inert_slot_count; i++) {
        AtomId value = values[run->inert_slots[i]];
        size_t seen = (size_t)(value & 15u);
        if (run->inert_seen[seen] != value) {
            run->inert_seen[seen] = value;
            run->inert_ok[seen] = run->query->head_inert(
                run->query->head_inert_context, run->universe, value);
        }
        if (!run->inert_ok[seen])
            return false;
    }
    return true;
}

/* The columns of each instance of an addition by tuples. */
static inline uint32_t native_query_effect_arity(const NativeQueryRun *run,
                                                 size_t effect) {
    return run->effects[effect].nodes[0].arity - 1u;
}

/* Hand COUNT tuples of an addition by tuples to the dialect. */
static bool native_query_hand_tuples(NativeQueryRun *run, size_t effect,
                                     const AtomId *tuples, size_t count) {
    const CettaNativeQuery *query = run->query;
    const NativeQueryNode *head = &run->effects[effect].nodes[1];
    run->made = true;
    return query->apply_tuples(query->apply_context, &query->effects[effect],
                               head->atom, head->id,
                               native_query_effect_arity(run, effect),
                               tuples, count);
}

static int native_query_id_order(const void *left, const void *right) {
    AtomId a = *(const AtomId *)left;
    AtomId b = *(const AtomId *)right;
    return a < b ? -1 : a > b;
}

/* Make effect EFFECT for COUNT instances: an insert adds each distinct
 * instance its target does not hold, which the instances made by earlier
 * chunks are then. */
static bool native_query_apply_ids(NativeQueryRun *run, size_t effect,
                                   const AtomId *ids, size_t count) {
    const CettaNativeQuery *query = run->query;
    const CettaNativeQueryEffect *made = &query->effects[effect];
    run->made = true;
    if (!made->insert)
        return query->apply(query->apply_context, made, ids, count);
    AtomId *fresh = malloc(sizeof(*fresh) * (count ? count : 1u));
    if (!fresh)
        return false;
    memcpy(fresh, ids, sizeof(*fresh) * count);
    qsort(fresh, count, sizeof(*fresh), native_query_id_order);
    size_t kept = 0u;
    for (size_t i = 0u; i < count; i++) {
        if ((i == 0u || fresh[i] != fresh[i - 1u]) &&
            !space_holds_atom_id_exact(made->target, fresh[i]))
            fresh[kept++] = fresh[i];
    }
    bool applied = kept == 0u ||
        query->apply(query->apply_context, made, fresh, kept);
    free(fresh);
    return applied;
}

static bool native_query_flush(NativeQueryRun *run, size_t effect) {
    if (run->by_tuples[effect]) {
        NativeQueryIds *tuples = &run->pending[effect];
        uint32_t arity = native_query_effect_arity(run, effect);
        bool made = tuples->len == 0u ||
            native_query_hand_tuples(run, effect, tuples->ids,
                                     tuples->len / arity);
        tuples->len = 0u;
        return made;
    }
    NativeQueryIds *ids = &run->pending[effect];
    if (ids->len == 0u)
        return true;
    bool made = native_query_apply_ids(run, effect, ids->ids, ids->len);
    ids->len = 0u;
    return made;
}

static bool native_query_act(NativeQueryRun *run, uint32_t effect, AtomId id) {
    if (run->deferred)
        return native_query_ids_push(&run->waiting[effect], id);
    if (run->pending[effect].len == NATIVE_QUERY_CHUNK &&
        !native_query_flush(run, effect))
        return false;
    return native_query_ids_push(&run->pending[effect], id);
}

/* A guarded query's answers differ by branch, so a ground answer is kept
 * as an item there, as any other is. */
static bool native_query_answer_of(NativeQueryRun *run,
                                   const NativeQueryTemplate *answer,
                                   const AtomId *values, uint64_t weight) {
    if (run->query->count_only || (answer->ground && !run->guard))
        return true;
    size_t position = 0u;
    Atom *instance = native_query_instance_atom(
        answer, &position, run->universe, run->arena, values);
    if (!instance || weight > SIZE_MAX - run->item_len)
        return false;
    size_t need = run->item_len + (size_t)weight;
    if (need > run->item_cap) {
        size_t cap = run->item_cap ? run->item_cap : 64u;
        while (cap < need) {
            if (cap > SIZE_MAX / 2u / sizeof(*run->items))
                return false;
            cap *= 2u;
        }
        Atom **items = realloc(run->items, cap * sizeof(*items));
        if (!items)
            return false;
        run->items = items;
        run->item_cap = cap;
    }
    for (uint64_t copy = 0u; copy < weight; copy++)
        run->items[run->item_len++] = instance;
    return true;
}

static bool native_query_answer(NativeQueryRun *run, const AtomId *values,
                                uint64_t weight) {
    return native_query_answer_of(run, run->answer, values, weight);
}

/* Whether an addition makes its instances by tuples: its data is
 * (h X1 ... Xk), h a symbol and each Xi an atom or a variable, into a set
 * that may keep h in a relation.  The space takes each tuple into the
 * relation once h has one, and as a row before (space_add_dense_tuples). */
static bool native_query_adds_tuples(const CettaNativeQuery *query,
                                     const CettaNativeQueryEffect *effect,
                                     const NativeQueryTemplate *data) {
    const NativeQueryNode *nodes = data->nodes;
    if (!query->apply_tuples || effect->removal || effect->insert ||
        !space_is_set(effect->target) || data->len < 3u ||
        data->len > CETTA_TUPLE_MAX_ARITY + 2u ||
        nodes[0].kind != NATIVE_QUERY_NODE_EXPRESSION ||
        nodes[0].arity + 1u != data->len ||
        nodes[1].kind != NATIVE_QUERY_NODE_CONSTANT || !nodes[1].atom ||
        nodes[1].atom->kind != ATOM_SYMBOL)
        return false;
    for (size_t c = 2u; c < data->len; c++)
        if (nodes[c].kind == NATIVE_QUERY_NODE_EXPRESSION)
            return false;
    return space_dense_may_hold(effect->target, nodes[1].atom->sym_id);
}

static bool native_query_visit(void *context, const AtomId *values,
                               uint64_t weight) {
    NativeQueryRun *run = context;
    const CettaNativeQuery *query = run->query;
    /* A head that can be applied declines the run, which has made no
     * effect. */
    if (run->inert_slot_count && !native_query_heads_inert(run, values))
        return false;
    /* A distinct query answers each instance of its key once, with the
     * first solution that makes it; an instance holding a variable
     * declines the run, which has made no effect. */
    if (run->distinct) {
        size_t position = 0u;
        AtomId key = native_query_instance_id(run->distinct, &position,
                                              run->universe, values);
        bool fresh = false;
        if (key == CETTA_ATOM_ID_NONE || tu_has_vars(run->universe, key) ||
            !native_query_distinct_fresh(run, key, &fresh))
            return false;
        if (!fresh)
            return true;
        weight = 1u;
    }
    /* A guarded solution reads its guard: a row that is the guard's
     * instance takes the other branch, which makes no effect. */
    if (run->guard) {
        size_t position = 0u;
        AtomId probe = native_query_lookup_id(run->guard, &position,
                                              run->universe, values);
        if (probe != CETTA_ATOM_ID_NONE &&
            space_holds_atom_id_exact(query->guard_space, probe))
            return native_query_answer_of(run, run->else_answer, values,
                                          weight);
    }
    for (size_t effect = 0u; effect < query->effect_count; effect++) {
        const CettaNativeQueryEffect *made = &query->effects[effect];
        if (run->by_tuples[effect]) {
            /* A set holds the instance once, however many answers give it. */
            const NativeQueryNode *nodes = run->effects[effect].nodes;
            uint32_t arity = native_query_effect_arity(run, effect);
            NativeQueryIds *tuples = run->deferred ? &run->waiting[effect]
                                                   : &run->pending[effect];
            if (!run->deferred &&
                tuples->len == (size_t)arity * NATIVE_QUERY_CHUNK &&
                !native_query_flush(run, effect))
                return false;
            for (uint32_t c = 0u; c < arity; c++) {
                const NativeQueryNode *column = &nodes[c + 2u];
                AtomId id = column->kind == NATIVE_QUERY_NODE_CONSTANT
                    ? column->id : values[column->slot];
                if (id == CETTA_ATOM_ID_NONE ||
                    !native_query_ids_push(tuples, id))
                    return false;
            }
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_NATIVE_QUERY_EFFECT_INSTANCE);
            continue;
        }
        size_t position = 0u;
        AtomId id = native_query_instance_id(&run->effects[effect], &position,
                                             run->universe, values);
        if (id == CETTA_ATOM_ID_NONE)
            return false;
        if (made->refused_head_count) {
            /* Every change waits for the end, so declining here makes
             * none. */
            const CettaTermHdr *hdr = tu_hdr(run->universe, id);
            if (!hdr)
                return false;
            for (uint8_t refused = 0u; hdr->tag == ATOM_EXPR &&
                 refused < made->refused_head_count; refused++) {
                if (hdr->sym_or_head == made->refused_heads[refused])
                    return false;
            }
        }
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_NATIVE_QUERY_EFFECT_INSTANCE);
        /* A removal takes every equal row and a set holds one copy, so
         * each needs the instance once; a bag holds a copy per answer. */
        uint64_t copies =
            made->removal || made->insert || space_is_set(made->target)
            ? 1u : weight;
        for (uint64_t copy = 0u; copy < copies; copy++) {
            if (!native_query_act(run, (uint32_t)effect, id))
                return false;
        }
    }
    return native_query_answer(run, values, weight);
}

/* Whether no instance one effect adds is an instance another removes from
 * the same space: then making every removal first and every addition after
 * is one of the orders the solutions may take. */
static bool native_query_disjoint(const NativeQueryRun *run) {
    const CettaNativeQuery *query = run->query;
    for (size_t removal = 0u; removal < query->effect_count; removal++) {
        if (!query->effects[removal].removal)
            continue;
        const NativeQueryIds *removed_ids = &run->waiting[removal];
        size_t capacity = 16u;
        while (capacity < removed_ids->len * 2u)
            capacity <<= 1u;
        AtomId *removed = malloc(sizeof(*removed) * capacity);
        if (!removed)
            return false;
        for (size_t i = 0u; i < capacity; i++)
            removed[i] = CETTA_ATOM_ID_NONE;
        for (size_t i = 0u; i < removed_ids->len; i++) {
            AtomId id = removed_ids->ids[i];
            size_t slot = (size_t)((id * UINT64_C(0x9e3779b97f4a7c15)) >> 32) &
                (capacity - 1u);
            while (removed[slot] != CETTA_ATOM_ID_NONE && removed[slot] != id)
                slot = (slot + 1u) & (capacity - 1u);
            removed[slot] = id;
        }
        bool disjoint = true;
        for (size_t addition = 0u; disjoint && addition < query->effect_count;
             addition++) {
            const CettaNativeQueryEffect *made = &query->effects[addition];
            if (made->removal || made->target != query->effects[removal].target)
                continue;
            const NativeQueryIds *added = &run->waiting[addition];
            for (size_t i = 0u; disjoint && i < added->len; i++) {
                AtomId id = added->ids[i];
                size_t slot =
                    (size_t)((id * UINT64_C(0x9e3779b97f4a7c15)) >> 32) &
                    (capacity - 1u);
                while (removed[slot] != CETTA_ATOM_ID_NONE &&
                       removed[slot] != id)
                    slot = (slot + 1u) & (capacity - 1u);
                disjoint = removed[slot] != id;
            }
        }
        free(removed);
        if (!disjoint)
            return false;
    }
    return true;
}

/* Whether a pattern reads a head the source space keeps in a relation: the
 * loops and the dense query read relations; the relational plan reads rows,
 * and would give the relations back to them. */
static bool native_query_meets_kept(const CettaNativeQuery *query) {
    return space_dense_meets_patterns(query->source, query->patterns,
                                      query->pattern_count);
}

static bool native_query_closed_node(const Atom *atom, void *context) {
    (void)context;
    return !atom_has_vars(atom);
}

static bool native_query_variable_node(const Atom *atom, void *context) {
    return atom->kind == ATOM_VAR &&
        var_id_eq(atom->var_id, *(const VarId *)context);
}

typedef struct {
    const CettaNativeQuery *query;
    bool complete;
} NativeQueryProjectionRead;

/* Read-only support inspection has no path-dependent binding context. The
 * shared graph visitor therefore asks each subexpression once, including
 * effect templates the bounded compiler will subsequently decline. */
static bool native_query_unread_variable(const Atom *atom, void *context) {
    if (atom->kind != ATOM_VAR)
        return false;
    NativeQueryProjectionRead *read = context;
    const CettaNativeQuery *query = read->query;
    VarId variable = atom->var_id;
    for (size_t e = 0u; e < query->effect_count; e++) {
        bool complete;
        bool found = atom_graph_any_checked(query->effects[e].data,
            native_query_variable_node, native_query_closed_node, &variable,
            NATIVE_QUERY_TEMPLATE_NODES, &complete);
        if (!complete) {
            read->complete = false;
            return true;
        }
        if (found)
            return false;
    }
    return true;
}

/* Whether the effects sum a variable out: every effect adds into a set,
 * the answers read no variable, and some variable of the patterns is read
 * by no effect.  Only then does the dense query make each instance once for
 * many solutions; over relations kept as bits it copies their pairs, which
 * the loops read in place. */
static bool native_query_projects(const CettaNativeQuery *query) {
    if (query->effect_count == 0u ||
        (!query->count_only && query->template_atom &&
         atom_has_vars(query->template_atom)))
        return false;
    for (size_t e = 0u; e < query->effect_count; e++) {
        if (query->effects[e].removal || !space_is_set(query->effects[e].target))
            return false;
    }
    NativeQueryProjectionRead read = {.query = query, .complete = true};
    for (size_t i = 0u; i < query->pattern_count; i++) {
        bool complete;
        bool unread = atom_graph_any_checked(query->patterns[i],
            native_query_unread_variable, native_query_closed_node, &read,
            NATIVE_QUERY_TEMPLATE_NODES, &complete);
        if (!complete || !read.complete)
            return false;
        if (unread)
            return true;
    }
    return false;
}

/* Whether an instance, the ids of its LEN columns, meets leg LEG read
 * under some key it was read with: a column the leg fixes holds the same
 * atom, the columns bound before it form such a key, and the columns of
 * one of its own variables agree.  Values whose equality is not their
 * identity are not told apart: a key holding one meets. */
static bool native_query_afresh_meets(NativeQueryLoops *loops, size_t leg,
                                      const AtomId *columns, size_t len) {
    const NativeQueryLeg *read = &loops->legs[leg];
    const NativeQueryKeys *keys = &loops->keys[leg];
    if (len != read->len)
        return false;
    AtomId key[NATIVE_QUERY_LEG_ARITY];
    size_t width = 0u;
    bool unknown = false;
    for (CettaExprIndex c = 0u; c < read->len; c++) {
        bool exact = native_query_exact_value(loops, columns[c]);
        CettaExprIndex first = keys->first[c];
        if (read->slot[c] == SIZE_MAX) {
            if (exact && read->fixed[c] != CETTA_ATOM_ID_NONE &&
                columns[c] != read->fixed[c])
                return false;
        } else if (width < keys->width && keys->columns[width] == c) {
            unknown |= !exact;
            key[width++] = columns[c];
        } else if (first != c && exact && columns[c] != columns[first] &&
                   native_query_exact_value(loops, columns[first])) {
            return false;
        }
    }
    return unknown || native_query_keys_contain(keys, key);
}

/* Whether no instance of an effect meets a leg read afresh under a key it
 * was read with: then every such read saw the rows the query began with,
 * as the loops did, and the effects are those the dialect makes
 * (CettaNativeQuery.afresh).  The first leg is read once, before any
 * effect. */
static bool native_query_afresh_unmet(NativeQueryRun *run) {
    NativeQueryLoops *loops = run->loops;
    const CettaNativeQuery *query = run->query;
    if (!query->afresh)
        return true;
    if (!loops || !loops->keys)
        return false;
    AtomId columns[NATIVE_QUERY_LEG_ARITY];
    for (size_t effect = 0u; effect < query->effect_count; effect++) {
        const NativeQueryIds *made = &run->waiting[effect];
        for (size_t leg = 1u; leg < loops->leg_count; leg++) {
            if (!(loops->afresh >> leg & 1u) || !loops->keys[leg].read ||
                loops->legs[leg].space != query->effects[effect].target)
                continue;
            if (run->by_tuples[effect]) {
                uint32_t arity = native_query_effect_arity(run, effect);
                if (arity + 1u > NATIVE_QUERY_LEG_ARITY)
                    return false;
                columns[0] = run->effects[effect].nodes[1].id;
                for (size_t i = 0u; i + arity <= made->len; i += arity) {
                    memcpy(columns + 1u, made->ids + i,
                           arity * sizeof(*columns));
                    if (native_query_afresh_meets(loops, leg, columns,
                                                  arity + 1u))
                        return false;
                }
                continue;
            }
            for (size_t i = 0u; i < made->len; i++) {
                AtomId id = made->ids[i];
                const CettaTermHdr *header = tu_hdr(loops->universe, id);
                if (!header)
                    return false;
                if (header->tag != ATOM_EXPR) {
                    if (header->tag == ATOM_VAR)
                        return false;
                    continue;
                }
                if (tu_has_vars(loops->universe, id))
                    return false;
                size_t len = tu_arity(loops->universe, id);
                if (len != loops->legs[leg].len)
                    continue;
                for (size_t c = 0u; c < len; c++)
                    columns[c] = tu_child(loops->universe, id, c);
                if (native_query_afresh_meets(loops, leg, columns, len))
                    return false;
            }
        }
    }
    return true;
}

/* Make the deferred effects: every removal, then every addition, in
 * chunks.  That is one of the solutions' orders only when no addition
 * undoes a removal; otherwise the outcome depends on the order, and the
 * ordinary path makes the effects in its own (false, having made none).
 * So it is when an effect may have changed a row a leg read afresh. */
static bool native_query_make_deferred(NativeQueryRun *run) {
    const CettaNativeQuery *query = run->query;
    if (!native_query_disjoint(run) || !native_query_afresh_unmet(run))
        return false;
    for (int pass = 0; pass < 2; pass++) {
        for (size_t effect = 0u; effect < query->effect_count; effect++) {
            if (query->effects[effect].removal != (pass == 0))
                continue;
            const NativeQueryIds *ids = &run->waiting[effect];
            if (run->by_tuples[effect]) {
                uint32_t arity = native_query_effect_arity(run, effect);
                size_t facts = ids->len / arity;
                for (size_t first = 0u; first < facts;
                     first += NATIVE_QUERY_CHUNK) {
                    size_t count = facts - first < NATIVE_QUERY_CHUNK
                        ? facts - first : NATIVE_QUERY_CHUNK;
                    if (!native_query_hand_tuples(run, effect,
                                                  ids->ids + (size_t)arity * first,
                                                  count))
                        return false;
                }
                continue;
            }
            for (size_t first = 0u; first < ids->len;
                 first += NATIVE_QUERY_CHUNK) {
                size_t count = ids->len - first < NATIVE_QUERY_CHUNK
                    ? ids->len - first : NATIVE_QUERY_CHUNK;
                if (!native_query_apply_ids(run, effect, ids->ids + first,
                                            count))
                    return false;
            }
        }
    }
    return true;
}

bool cetta_native_query_count(Space *space, Arena *scratch,
                               Atom *const *patterns, size_t pattern_count,
                               const Bindings *seed, uint64_t *count_out) {
    if (!space || !scratch || !patterns || !count_out || pattern_count == 0u ||
        pattern_count > NATIVE_QUERY_LEGS || space_has_dependencies(space))
        return false;
    Atom *bound[NATIVE_QUERY_LEGS];
    for (size_t p = 0u; p < pattern_count; p++) {
        bound[p] = bindings_apply_if_vars(seed, scratch, patterns[p]);
        if (!bound[p])
            return false;
    }
    CettaNativeQuery query = {
        .source = space,
        .patterns = bound,
        .pattern_count = pattern_count,
        .template_atom = bound[0],
        .consumer = CETTA_NATIVE_QUERY_COLLECT,
        .count_only = true,
    };
    CettaNativeQueryResult result;
    if (cetta_native_query_run(&query, scratch, &result) !=
        CETTA_NATIVE_QUERY_DONE)
        return false;
    *count_out = result.answers;
    return true;
}

CettaNativeQueryStatus cetta_native_query_run(
    const CettaNativeQuery *query, Arena *arena,
    CettaNativeQueryResult *result) {
    if (result)
        *result = (CettaNativeQueryResult){0};
    if (!query || !arena || !result || !query->source ||
        !query->patterns || query->pattern_count == 0u ||
        !query->template_atom || !cetta_native_query_enabled() ||
        query->effect_count > NATIVE_QUERY_EFFECTS ||
        (query->consumer == CETTA_NATIVE_QUERY_EFFECTS &&
         (query->effect_count == 0u || !query->effects || !query->apply)))
        return CETTA_NATIVE_QUERY_DECLINED;
    TermUniverse *universe = query->source->native.universe;
    if (!universe)
        return CETTA_NATIVE_QUERY_DECLINED;
    bool effects = query->consumer == CETTA_NATIVE_QUERY_EFFECTS;
    if (query->guard_space &&
        (!effects || query->rows || !query->guard_pattern ||
         !query->else_atom ||
         query->guard_space->native.universe != universe))
        return CETTA_NATIVE_QUERY_DECLINED;
    /* Legs read afresh are read by the loops alone, over rows. */
    if (query->afresh && (!effects || query->rows))
        return CETTA_NATIVE_QUERY_DECLINED;
    bool deferred = false;
    for (size_t effect = 0u; effects && effect < query->effect_count;
         effect++) {
        const CettaNativeQueryEffect *made = &query->effects[effect];
        if (!made->target || !made->data ||
            made->target->native.universe != universe)
            return CETTA_NATIVE_QUERY_DECLINED;
        deferred |= made->removal || made->refused_head_count != 0u;
    }
    deferred |= query->snapshot;
    /* Patterns over several spaces join by the index loops alone, each leg
     * reading its own space's rows. */
    bool one_space = true;
    for (size_t i = 0u; query->sources && i < query->pattern_count; i++) {
        if (!query->sources[i] ||
            query->sources[i]->native.universe != universe ||
            (query->rows && query->sources[i] != query->source))
            return CETTA_NATIVE_QUERY_DECLINED;
        one_space = one_space && query->sources[i] == query->source;
    }

    /* A query with a pattern that an atom besides its head selects joins
     * pattern by pattern, from there, over the space's own indexes, which
     * nothing need build for it: it reads only the rows its bindings reach.
     * Any other reads every row of its relations anyway, and joins variable
     * by variable over sorted views of them, which bounds a dense cyclic
     * query's work by its output whatever the data.  A single pattern that
     * only its head selects joins nothing: its rows are read as they stand. */
    bool cyclic = true;
    if (!query->rows &&
        !cetta_relational_patterns_cyclic(query->patterns, query->pattern_count,
                                          &cyclic))
        return CETTA_NATIVE_QUERY_DECLINED;
    bool dense = cyclic;
    bool selective = false;
    for (size_t i = 0u; !query->rows && i < query->pattern_count; i++) {
        size_t fixed = 0u;
        for (CettaExprIndex c = 0u; c < query->patterns[i]->expr.len; c++)
            fixed += query->patterns[i]->expr.elems[c]->kind != ATOM_VAR;
        dense = dense && fixed <= 1u;
        selective = selective || fixed >= 2u;
    }
    bool meets_kept = !query->rows && one_space &&
        native_query_meets_kept(query);
    NativeQueryVariables variables = {0};
    NativeQueryLoops *loops = NULL;
    CettaRelationalPlan *plan = NULL;
    CettaDenseQuery *dense_query = NULL;
    if (query->rows) {
        /* Rows given as a list are a value: no effect changes them. */
        if (query->pattern_count != 1u ||
            !native_query_rows_exact(query->rows))
            return CETTA_NATIVE_QUERY_DECLINED;
        loops = native_query_loops_new(1u);
        if (!loops)
            return CETTA_NATIVE_QUERY_DECLINED;
        loops->space = query->source;
        loops->universe = universe;
        loops->arena = arena;
        arena_init_detached(&loops->probes);
        if (query->patterns[0]->kind == ATOM_VAR) {
            loops->whole_rows = true;
            loops->leg_count = 1u;
            variables.ids[variables.len++] = query->patterns[0]->var_id;
        } else if (!native_query_loops_prepare(loops, &variables,
                                               query->patterns, NULL, 1u,
                                               false)) {
            arena_free(&loops->probes);
            free(loops);
            return CETTA_NATIVE_QUERY_DECLINED;
        }
        /* A row may hold an atom the universe has not met yet. */
        NativeQueryLeg *leg = &loops->legs[0];
        for (CettaExprIndex c = 0u; c < leg->len; c++) {
            if (leg->slot[c] == SIZE_MAX)
                leg->fixed[c] = term_universe_store_atom_id(
                    universe, arena, leg->pattern->expr.elems[c]);
        }
    } else if (one_space && !query->afresh && (dense || !selective) &&
               query->pattern_count >= 2u &&
               (!meets_kept || native_query_projects(query)) &&
               cetta_dense_query_prepare(query->source, arena, query->patterns,
                                         query->pattern_count, &dense_query)) {
        /* Binary relations dense enough for rows of bits join by them.  A
         * guard reads alike in any order of the solutions: no effect
         * changes a row it reads, or an insert adds each instance once. */
        variables.len = cetta_dense_query_variable_count(dense_query);
        for (size_t i = 0u; i < variables.len; i++)
            variables.ids[i] = cetta_dense_query_variable_at(dense_query, i);
    } else if (one_space && !query->guard_space && !query->afresh &&
               (dense || !selective) && !meets_kept &&
               cetta_relational_plan_prepare(
                   query->source, arena, query->patterns,
                   query->pattern_count, NULL,
                   CETTA_RELATIONAL_ADMIT_ENUMERATION, &plan)) {
        variables.len = cetta_relational_plan_variable_count(plan);
        for (size_t i = 0u; i < variables.len; i++)
            variables.ids[i] = cetta_relational_plan_variable_at(plan, i);
    } else {
        /* A selective query, or one whose rows the plan cannot key, such as
         * rows holding variables inside a column, which the loops read one
         * way. */
        loops = native_query_loops_new(query->pattern_count);
        if (!loops)
            return CETTA_NATIVE_QUERY_DECLINED;
        loops->space = query->source;
        loops->universe = universe;
        loops->arena = arena;
        arena_init_detached(&loops->probes);
        if (!native_query_loops_prepare(loops, &variables, query->patterns,
                                        query->sources, query->pattern_count,
                                        true) ||
            (query->afresh &&
             !native_query_afresh_prepare(loops, query->afresh))) {
            native_query_afresh_free(loops);
            arena_free(&loops->probes);
            free(loops);
            return CETTA_NATIVE_QUERY_DECLINED;
        }
        /* A row met halfway may decline the whole query, which must then
         * have made no effect. */
        deferred = effects;
    }
    size_t effect_count = effects ? query->effect_count : 0u;
    NativeQueryTemplate *templates =
        malloc(sizeof(*templates) * (effect_count + 4u));
    NativeQueryIds *pending =
        calloc(effect_count ? effect_count : 1u, sizeof(*pending));
    NativeQueryIds *waiting =
        calloc(effect_count ? effect_count : 1u, sizeof(*waiting));
    bool *by_tuples = calloc(effect_count ? effect_count : 1u,
                             sizeof(*by_tuples));
    bool admitted = templates && pending && waiting && by_tuples &&
        native_query_compile(&templates[effect_count], &variables, universe,
                             arena, false, query->template_atom);
    /* A guard's constants are stored, so its instances are looked up by
     * ids; its other branch answers as a template does. */
    if (admitted && query->guard_space)
        admitted =
            native_query_compile(&templates[effect_count + 1u], &variables,
                                 universe, arena, true,
                                 query->guard_pattern) &&
            native_query_compile(&templates[effect_count + 2u], &variables,
                                 universe, arena, false, query->else_atom);
    /* A distinct query's key is made as its instances' ids, as an
     * effect's data is. */
    if (admitted && query->distinct_key)
        admitted = native_query_compile(&templates[effect_count + 3u],
                                        &variables, universe, arena, true,
                                        query->distinct_key);
    bool removes = false;
    for (size_t effect = 0u; admitted && effect < effect_count; effect++) {
        admitted = native_query_compile(&templates[effect], &variables,
                                        universe, arena, true,
                                        query->effects[effect].data);
        removes |= query->effects[effect].removal;
    }
    for (size_t effect = 0u; admitted && !removes && effect < effect_count;
         effect++) {
        by_tuples[effect] = native_query_adds_tuples(
            query, &query->effects[effect], &templates[effect]);
    }
    /* Variable heads are asked about where a declined run has made no
     * effect (it collects, or its effects wait for the end), each by the
     * slot its variable holds. */
    size_t inert_slots[4];
    size_t inert_slot_count = 0u;
    if (admitted && query->inert_head_count)
        admitted = query->head_inert && query->inert_head_count <= 4u &&
            (effect_count == 0u || deferred);
    for (size_t i = 0u; admitted && i < query->inert_head_count; i++) {
        size_t slot = query->inert_heads[i]->kind == ATOM_VAR
            ? native_query_variable(&variables,
                                    query->inert_heads[i]->var_id)
            : SIZE_MAX;
        admitted = slot != SIZE_MAX;
        inert_slots[inert_slot_count++] = slot;
    }
    if (!admitted) {
        free(templates);
        free(pending);
        free(waiting);
        free(by_tuples);
        if (loops) {
            native_query_afresh_free(loops);
            arena_free(&loops->probes);
        }
        free(loops);
        cetta_relational_plan_free(plan);
        cetta_dense_query_free(dense_query);
        return CETTA_NATIVE_QUERY_DECLINED;
    }

    NativeQueryRun run = {
        .query = query,
        .answer = &templates[effect_count],
        .effects = templates,
        .guard = query->guard_space ? &templates[effect_count + 1u] : NULL,
        .else_answer =
            query->guard_space ? &templates[effect_count + 2u] : NULL,
        .universe = universe,
        .arena = arena,
        .deferred = deferred,
        .pending = pending,
        .waiting = waiting,
        .by_tuples = by_tuples,
        .inert_slot_count = inert_slot_count,
        .distinct = query->distinct_key ? &templates[effect_count + 3u]
                                        : NULL,
        .loops = loops,
    };
    for (size_t i = 0u; i < inert_slot_count; i++)
        run.inert_slots[i] = inert_slots[i];
    for (size_t i = 0u; i < 16u; i++)
        run.inert_seen[i] = CETTA_ATOM_ID_NONE;
    uint64_t answers = 0u;
    bool done = false;
    bool declined = false;
    if (dense_query) {
        /* When every effect adds into a set, or inserts (each distinct
         * instance once), and no answer is read but its count, a variable
         * no effect reads is summed out: each instance is made once however
         * many solutions give it.  A guard then reads each instance once,
         * for all of them: alike when no effect changes what it reads, and
         * under an insert the first takes one branch and the rest the
         * other, which a count does not tell apart. */
        bool summed = query->count_only || run.answer->ground;
        for (size_t effect = 0u; summed && effect < effect_count; effect++) {
            summed = !query->effects[effect].removal &&
                (space_is_set(query->effects[effect].target) ||
                 query->effects[effect].insert);
        }
        /* A distinct query answers each instance of its key once, so a
         * variable that neither the key, the effects nor the answer reads
         * is summed out too. */
        bool distinct_summed = run.distinct && !summed;
        summed = summed || run.distinct;
        bool kept[NATIVE_QUERY_VARIABLES] = {false};
        bool any_kept = false;
        for (size_t effect = 0u; summed && effect < effect_count + 2u;
             effect++) {
            const NativeQueryTemplate *made =
                effect < effect_count ? &templates[effect]
                : effect == effect_count
                    ? (distinct_summed ? run.answer : NULL)
                    : run.distinct;
            for (size_t node = 0u; made && node < made->len; node++) {
                if (made->nodes[node].kind == NATIVE_QUERY_NODE_VARIABLE) {
                    kept[made->nodes[node].slot] = true;
                    any_kept = true;
                }
            }
        }
        CettaDenseWork work = {0};
        done = cetta_dense_query_run(dense_query, summed ? kept : NULL,
                                     native_query_visit, &run, &answers,
                                     &work);
        /* Effects that read no variable are made once for every solution:
         * into a set, once if there is one. */
        if (done && summed && effect_count && !any_kept && answers) {
            AtomId none[1] = {CETTA_ATOM_ID_NONE};
            done = native_query_visit(&run, none, 1u);
        }
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_DENSE_RELATION_ADMISSION);
        cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_DENSE_RELATION_WORD,
                                work.words);
    } else if (plan) {
        CettaRelationalWork work = {0};
        done = cetta_relational_plan_enumerate(
            plan, query->source, native_query_visit, &run, &answers, &work);
        cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_MATCH_RELATIONAL_KEY_SEEKS,
                                work.key_seeks);
        cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_MATCH_RELATIONAL_PREFIXES,
                                work.candidate_prefixes);
        cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_MATCH_RELATIONAL_PREPARE_ROWS,
                                cetta_relational_plan_info(plan)->scanned_rows);
    } else if (query->rows) {
        loops->visit = native_query_visit;
        loops->context = &run;
        done = native_query_rows_leg(loops, query->rows);
        answers = loops->answers;
        declined = loops->declined;
    } else {
        loops->visit = native_query_visit;
        loops->context = &run;
        /* Legs read afresh keep the order they were given. */
        uint64_t shape = query->afresh ? 0u : native_query_order_recall(loops);
        done = native_query_loops_leg(loops, 0u);
        native_query_order_remember(loops, shape);
        answers = loops->answers;
        declined = loops->declined;
    }
    /* A distinct query answered once for each instance of its key it met,
     * whatever the enumeration counted. */
    if (run.distinct)
        answers = run.met_len;
    for (size_t effect = 0u; done && !deferred && effect < effect_count;
         effect++)
        done = native_query_flush(&run, effect);
    if (done && deferred)
        done = native_query_make_deferred(&run);
    if (done && !query->count_only) {
        /* The answers' expression, built in place: a ground template is
         * every answer. */
        bool repeated = run.answer->ground && !run.guard;
        uint64_t len = repeated ? answers : run.item_len;
        Atom *collection = cetta_expr_len_mul_fits_size((CettaExprLen)len,
                                                        sizeof(Atom *))
            ? atom_expr_builder_begin(arena, (CettaExprLen)len) : NULL;
        if (collection) {
            Atom **elems = collection->expr.elems;
            if (repeated) {
                for (uint64_t index = 0u; index < len; index++)
                    elems[index] = query->template_atom;
            } else if (len) {
                memcpy(elems, run.items, sizeof(*elems) * (size_t)len);
            }
            collection = atom_expr_builder_finish(arena, collection);
        }
        result->collection = collection;
        done = collection != NULL;
    }
    bool made_effects = run.made;
    for (size_t effect = 0u; effect < effect_count; effect++) {
        free(pending[effect].ids);
        free(waiting[effect].ids);
    }
    free(pending);
    free(waiting);
    free(by_tuples);
    free(run.items);
    free(run.met);
    free(templates);
    if (loops) {
        free(loops->exact);
        native_query_afresh_free(loops);
        arena_free(&loops->probes);
    }
    free(loops);
    cetta_relational_plan_free(plan);
    cetta_dense_query_free(dense_query);
    /* A query without effects can still be answered by the ordinary path. */
    if (!done)
        return made_effects && !declined ? CETTA_NATIVE_QUERY_FAILED
                                         : CETTA_NATIVE_QUERY_DECLINED;
    result->answers = answers;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_NATIVE_QUERY_ADMISSION);
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_NATIVE_QUERY_ANSWER, answers);
    return CETTA_NATIVE_QUERY_DONE;
}

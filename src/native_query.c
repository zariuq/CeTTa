#include "native_query.h"

#include "dense_relation.h"
#include "match.h"
#include "relational_plan.h"
#include "stats.h"
#include "term_universe.h"

#include <stdlib.h>
#include <string.h>

enum {
    NATIVE_QUERY_TEMPLATE_NODES = 64u,
    NATIVE_QUERY_VARIABLES = 64u,
    NATIVE_QUERY_LEGS = 32u,
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
typedef struct {
    Atom *pattern;
    CettaExprLen len;
    size_t slot[NATIVE_QUERY_LEG_ARITY];
    AtomId fixed[NATIVE_QUERY_LEG_ARITY];
} NativeQueryLeg;

typedef struct {
    Space *space;
    TermUniverse *universe;
    Arena *arena;
    /* The probes, each reset once its leg has its rows, freed with the
     * query: none outlives it. */
    Arena probes;
    NativeQueryLeg legs[NATIVE_QUERY_LEGS];
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
    /* Ids already known to be exact values. */
    AtomId *exact;
    size_t exact_cap;
    size_t exact_len;
    /* Rows given as a list that a variable meets whole. */
    bool whole_rows;
    /* Per leg kept as bits: whether rows of the space may match it too
     * (0 not yet asked, 1 none, 2 some). */
    uint8_t bits_rows[NATIVE_QUERY_LEGS];
} NativeQueryLoops;

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
    for (size_t i = 0u; i < loops->exact_len; i++) {
        if (loops->exact[i] == id)
            return true;
    }
    for (CettaExprIndex c = 0u; c < tu_arity(loops->universe, id); c++) {
        if (!native_query_exact_value(loops, tu_child(loops->universe, id, c)))
            return false;
    }
    if (loops->exact_len == loops->exact_cap) {
        size_t cap = loops->exact_cap ? loops->exact_cap * 2u : 64u;
        AtomId *grown = realloc(loops->exact, cap * sizeof(*grown));
        if (!grown)
            return true;
        loops->exact = grown;
        loops->exact_cap = cap;
    }
    loops->exact[loops->exact_len++] = id;
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

/* The legs of the patterns, and their variables; false when a pattern is
 * not flat: an element is a variable or a ground exact atom. */
static bool native_query_loops_prepare(NativeQueryLoops *loops,
                                       NativeQueryVariables *variables,
                                       Atom *const *patterns, size_t count) {
    if (count > NATIVE_QUERY_LEGS)
        return false;
    for (size_t i = 0u; i < count; i++) {
        Atom *pattern = patterns[i];
        if (!pattern || pattern->kind != ATOM_EXPR || pattern->expr.len == 0u ||
            pattern->expr.len > NATIVE_QUERY_LEG_ARITY)
            return false;
        NativeQueryLeg *leg = &loops->legs[i];
        *leg = (NativeQueryLeg){.pattern = pattern, .len = pattern->expr.len};
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
                fixed += slot == SIZE_MAX || bound[slot];
            }
            if (best == SIZE_MAX || fixed > best_fixed) {
                best = i;
                best_fixed = fixed;
            }
        }
        used[best] = true;
        loops->order[depth] = best;
        for (CettaExprIndex c = 0u; c < loops->legs[best].len; c++) {
            if (loops->legs[best].slot[c] != SIZE_MAX)
                bound[loops->legs[best].slot[c]] = true;
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

/* The bits a leg reads when its space keeps its head's facts as bits: a
 * leg (h x y) whose head is fixed. */
static const CettaDenseRelation *native_query_leg_bits(
    const NativeQueryLoops *loops, const NativeQueryLeg *leg) {
    if (leg->len != 3u || leg->slot[0] != SIZE_MAX ||
        leg->fixed[0] == CETTA_ATOM_ID_NONE ||
        tu_kind(loops->universe, leg->fixed[0]) != ATOM_SYMBOL)
        return NULL;
    return space_dense_relation(loops->space,
                                tu_sym(loops->universe, leg->fixed[0]));
}

enum { NATIVE_QUERY_INLINE_PAIRS = 16u };

typedef struct {
    CettaDenseId inline_items[2u * NATIVE_QUERY_INLINE_PAIRS];
    CettaDenseId *items;
    size_t len, cap;
    bool failed;
} NativeQueryPairs;

static void native_query_pairs_init(NativeQueryPairs *pairs) {
    pairs->items = pairs->inline_items;
    pairs->len = 0u;
    pairs->cap = 2u * NATIVE_QUERY_INLINE_PAIRS;
    pairs->failed = false;
}

static void native_query_pairs_free(NativeQueryPairs *pairs) {
    if (pairs->items != pairs->inline_items)
        free(pairs->items);
}

static bool native_query_pairs_push(void *context, CettaDenseId a,
                                    CettaDenseId b) {
    NativeQueryPairs *pairs = context;
    if (pairs->len + 2u > pairs->cap) {
        size_t cap = pairs->cap * 2u;
        CettaDenseId *items = pairs->items == pairs->inline_items
            ? malloc(cap * sizeof(*items))
            : realloc(pairs->items, cap * sizeof(*items));
        if (!items) {
            pairs->failed = true;
            return false;
        }
        if (pairs->items == pairs->inline_items)
            memcpy(items, pairs->inline_items, pairs->len * sizeof(*items));
        pairs->items = items;
        pairs->cap = cap;
    }
    pairs->items[pairs->len++] = a;
    pairs->items[pairs->len++] = b;
    return true;
}

/* The pairs of BITS the leg's fixed and bound columns select, taken before
 * any is visited: a visit's effects may change the bits. */
static bool native_query_bits_select(const NativeQueryLoops *loops,
                                     const NativeQueryLeg *leg,
                                     const CettaDenseRelation *bits,
                                     NativeQueryPairs *pairs) {
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_NATIVE_QUERY_BITS_LEG);
    CettaDenseId columns[3] = {CETTA_DENSE_ANY, CETTA_DENSE_ANY,
                               CETTA_DENSE_ANY};
    for (CettaExprIndex c = 1u; c < 3u; c++) {
        size_t slot = leg->slot[c];
        if (slot == SIZE_MAX) {
            /* An atom no row holds selects nothing. */
            if (leg->fixed[c] == CETTA_ATOM_ID_NONE)
                return true;
            columns[c] = leg->fixed[c];
        } else if (loops->bound[slot]) {
            columns[c] = loops->values[slot];
        }
    }
    return cetta_dense_relation_each(bits, columns[1], columns[2],
                                     native_query_pairs_push, pairs) &&
           !pairs->failed;
}

/* Visit the selected pairs as rows (h a b) of the leg. */
static bool native_query_bits_visit(NativeQueryLoops *loops,
                                    const NativeQueryLeg *leg,
                                    const NativeQueryPairs *pairs,
                                    size_t depth) {
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_NATIVE_QUERY_LOOP_ROW,
                            pairs->len / 2u);
    bool ok = true;
    for (size_t i = 0u; ok && i < pairs->len; i += 2u) {
        AtomId columns[3] = {CETTA_ATOM_ID_NONE, (AtomId)pairs->items[i],
                             (AtomId)pairs->items[i + 1u]};
        size_t fresh[2];
        size_t fresh_len = 0u;
        bool matched = true;
        for (CettaExprIndex c = 1u; matched && c < 3u; c++) {
            size_t slot = leg->slot[c];
            if (slot == SIZE_MAX)
                continue;
            if (loops->bound[slot]) {
                matched = loops->values[slot] == columns[c];
            } else {
                loops->values[slot] = columns[c];
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

/* Whether rows of the space may match a leg kept as bits besides its pairs.
 * Asked once a query: no effect is made while its loops run. */
static bool native_query_bits_rows(NativeQueryLoops *loops, size_t leg) {
    if (loops->bits_rows[leg] == 0u) {
        /* A head none of whose facts is a row, in a space with no open row,
         * answers at once; the candidates would demand a full index. */
        const NativeQueryLeg *l = &loops->legs[leg];
        if (space_dense_head_rows_absent(loops->space,
                                         tu_sym(loops->universe, l->fixed[0]))) {
            loops->bits_rows[leg] = 1u;
            return false;
        }
        CettaIndex *rows = NULL;
        space_dense_reading_bits_begin();
        CettaIndex count = space_match_candidates64(
            loops->space, loops->legs[leg].pattern, &rows);
        space_dense_reading_bits_end();
        free(rows);
        loops->bits_rows[leg] = count != 0u ? 2u : 1u;
    }
    return loops->bits_rows[leg] == 2u;
}

static bool native_query_loops_leg(NativeQueryLoops *loops, size_t depth) {
    if (depth == loops->leg_count) {
        if (loops->answers == UINT64_MAX)
            return false;
        loops->answers++;
        return loops->visit(loops->context, loops->values, 1u);
    }
    const NativeQueryLeg *leg = &loops->legs[loops->order[depth]];
    /* A leg kept as bits that no row can match reads its pairs alone. */
    const CettaDenseRelation *bits = native_query_leg_bits(loops, leg);
    if (bits && !native_query_bits_rows(loops, loops->order[depth])) {
        NativeQueryPairs pairs;
        native_query_pairs_init(&pairs);
        bool ok = native_query_bits_select(loops, leg, bits, &pairs) &&
                  native_query_bits_visit(loops, leg, &pairs, depth);
        native_query_pairs_free(&pairs);
        return ok;
    }
    ArenaMark mark = arena_mark(&loops->probes);
    Atom **elements =
        arena_alloc(&loops->probes, sizeof(*elements) * leg->len);
    if (!elements)
        return false;
    for (CettaExprIndex c = 0u; c < leg->len; c++) {
        size_t slot = leg->slot[c];
        if (slot == SIZE_MAX && leg->fixed[c] == CETTA_ATOM_ID_NONE) {
            arena_reset(&loops->probes, mark);
            return true;
        }
        elements[c] = slot != SIZE_MAX && loops->bound[slot]
            ? term_universe_get_atom(loops->universe, loops->values[slot])
            : leg->pattern->expr.elems[c];
        if (!elements[c]) {
            arena_reset(&loops->probes, mark);
            return false;
        }
    }
    Atom *probe = atom_expr(&loops->probes, elements, leg->len);
    /* A head the space keeps as bits: its pairs, and any copies kept as
     * rows, both taken before either is visited. */
    NativeQueryPairs pairs;
    native_query_pairs_init(&pairs);
    if (bits && !native_query_bits_select(loops, leg, bits, &pairs)) {
        native_query_pairs_free(&pairs);
        arena_reset(&loops->probes, mark);
        return false;
    }
    if (bits)
        space_dense_reading_bits_begin();
    CettaIndex *rows = NULL;
    CettaIndex row_count = probe
        ? space_match_candidates64(loops->space, probe, &rows) : 0u;
    if (bits)
        space_dense_reading_bits_end();
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_NATIVE_QUERY_LOOP_ROW,
                            row_count);
    arena_reset(&loops->probes, mark);
    if (!probe) {
        native_query_pairs_free(&pairs);
        free(rows);
        return false;
    }
    bool ok = !bits || native_query_bits_visit(loops, leg, &pairs, depth);
    native_query_pairs_free(&pairs);
    size_t fresh[NATIVE_QUERY_LEG_ARITY];
    for (CettaIndex r = 0u; ok && r < row_count; r++) {
        AtomId id = space_get_atom_id_at64(loops->space, rows[r]);
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
    bool *by_pairs;
    /* Answers that are not one ground atom. */
    Atom **items;
    size_t item_len;
    size_t item_cap;
    /* Whether any effect has been handed to the dialect. */
    bool made;
} NativeQueryRun;

/* Hand COUNT pairs of an addition by pairs to the dialect. */
static bool native_query_hand_pairs(NativeQueryRun *run, size_t effect,
                                    const AtomId *pairs, size_t count) {
    const CettaNativeQuery *query = run->query;
    const NativeQueryNode *head = &run->effects[effect].nodes[1];
    run->made = true;
    return query->apply_pairs(query->apply_context, &query->effects[effect],
                              head->atom, head->id, pairs, count);
}

static bool native_query_flush(NativeQueryRun *run, size_t effect) {
    if (run->by_pairs[effect]) {
        NativeQueryIds *pairs = &run->pending[effect];
        bool made = pairs->len == 0u ||
            native_query_hand_pairs(run, effect, pairs->ids, pairs->len / 2u);
        pairs->len = 0u;
        return made;
    }
    NativeQueryIds *ids = &run->pending[effect];
    if (ids->len == 0u)
        return true;
    const CettaNativeQuery *query = run->query;
    run->made = true;
    bool made = query->apply(query->apply_context, &query->effects[effect],
                             ids->ids, ids->len);
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

static bool native_query_answer(NativeQueryRun *run, const AtomId *values,
                                uint64_t weight) {
    if (run->answer->ground || run->query->count_only)
        return true;
    size_t position = 0u;
    Atom *instance = native_query_instance_atom(
        run->answer, &position, run->universe, run->arena, values);
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

/* Whether an addition makes its instances by pairs: its data is (h X Y),
 * h a symbol and X, Y atoms or variables, into a set that may keep h as
 * bits.  The space takes each pair as bits once h has them, and as a row
 * before (space_add_dense_pairs). */
static bool native_query_adds_pairs(const CettaNativeQuery *query,
                                    const CettaNativeQueryEffect *effect,
                                    const NativeQueryTemplate *data) {
    const NativeQueryNode *nodes = data->nodes;
    return query->apply_pairs && !effect->removal &&
           space_is_set(effect->target) && data->len == 4u &&
           nodes[0].kind == NATIVE_QUERY_NODE_EXPRESSION &&
           nodes[0].arity == 3u &&
           nodes[1].kind == NATIVE_QUERY_NODE_CONSTANT && nodes[1].atom &&
           nodes[1].atom->kind == ATOM_SYMBOL &&
           nodes[2].kind != NATIVE_QUERY_NODE_EXPRESSION &&
           nodes[3].kind != NATIVE_QUERY_NODE_EXPRESSION &&
           space_dense_may_hold(effect->target, atom_symbol_current_id(nodes[1].atom));
}

static bool native_query_visit(void *context, const AtomId *values,
                               uint64_t weight) {
    NativeQueryRun *run = context;
    const CettaNativeQuery *query = run->query;
    for (size_t effect = 0u; effect < query->effect_count; effect++) {
        const CettaNativeQueryEffect *made = &query->effects[effect];
        if (run->by_pairs[effect]) {
            /* A set holds the instance once, however many answers give it. */
            const NativeQueryNode *nodes = run->effects[effect].nodes;
            AtomId a = nodes[2].kind == NATIVE_QUERY_NODE_CONSTANT
                ? nodes[2].id : values[nodes[2].slot];
            AtomId b = nodes[3].kind == NATIVE_QUERY_NODE_CONSTANT
                ? nodes[3].id : values[nodes[3].slot];
            NativeQueryIds *pairs = run->deferred ? &run->waiting[effect]
                                                  : &run->pending[effect];
            if (a == CETTA_ATOM_ID_NONE || b == CETTA_ATOM_ID_NONE ||
                (!run->deferred && pairs->len == 2u * NATIVE_QUERY_CHUNK &&
                 !native_query_flush(run, effect)) ||
                !native_query_ids_push(pairs, a) ||
                !native_query_ids_push(pairs, b))
                return false;
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_NATIVE_QUERY_EFFECT_INSTANCE);
            continue;
        }
        size_t position = 0u;
        AtomId id = native_query_instance_id(&run->effects[effect], &position,
                                             run->universe, values);
        if (id == CETTA_ATOM_ID_NONE)
            return false;
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_NATIVE_QUERY_EFFECT_INSTANCE);
        /* A removal takes every equal row and a set holds one copy, so
         * each needs the instance once; a bag holds a copy per answer. */
        uint64_t copies = made->removal || space_is_set(made->target)
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

/* Whether a pattern reads a head the source space keeps as bits: the
 * loops and the dense query read the bits; the relational plan reads rows,
 * and would give the bits back to them. */
static bool native_query_meets_bits(const CettaNativeQuery *query) {
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

/* Make the deferred effects: every removal, then every addition, in
 * chunks.  That is one of the solutions' orders only when no addition
 * undoes a removal; otherwise the outcome depends on the order, and the
 * ordinary path makes the effects in its own (false, having made none). */
static bool native_query_make_deferred(NativeQueryRun *run) {
    const CettaNativeQuery *query = run->query;
    if (!native_query_disjoint(run))
        return false;
    for (int pass = 0; pass < 2; pass++) {
        for (size_t effect = 0u; effect < query->effect_count; effect++) {
            if (query->effects[effect].removal != (pass == 0))
                continue;
            const NativeQueryIds *ids = &run->waiting[effect];
            if (run->by_pairs[effect]) {
                for (size_t first = 0u; first < ids->len / 2u;
                     first += NATIVE_QUERY_CHUNK) {
                    size_t count = ids->len / 2u - first < NATIVE_QUERY_CHUNK
                        ? ids->len / 2u - first : NATIVE_QUERY_CHUNK;
                    if (!native_query_hand_pairs(run, effect,
                                                 ids->ids + 2u * first, count))
                        return false;
                }
                continue;
            }
            for (size_t first = 0u; first < ids->len;
                 first += NATIVE_QUERY_CHUNK) {
                size_t count = ids->len - first < NATIVE_QUERY_CHUNK
                    ? ids->len - first : NATIVE_QUERY_CHUNK;
                run->made = true;
                if (!query->apply(query->apply_context,
                                  &query->effects[effect], ids->ids + first,
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
    bool deferred = false;
    for (size_t effect = 0u; effects && effect < query->effect_count;
         effect++) {
        const CettaNativeQueryEffect *made = &query->effects[effect];
        if (!made->target || !made->data ||
            made->target->native.universe != universe)
            return CETTA_NATIVE_QUERY_DECLINED;
        deferred |= made->removal;
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
    bool meets_bits = !query->rows && native_query_meets_bits(query);
    NativeQueryVariables variables = {0};
    NativeQueryLoops *loops = NULL;
    CettaRelationalPlan *plan = NULL;
    CettaDenseQuery *dense_query = NULL;
    if (query->rows) {
        /* Rows given as a list are a value: no effect changes them. */
        if (query->pattern_count != 1u ||
            !native_query_rows_exact(query->rows))
            return CETTA_NATIVE_QUERY_DECLINED;
        loops = calloc(1u, sizeof(*loops));
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
                                               query->patterns, 1u)) {
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
    } else if ((dense || !selective) && query->pattern_count >= 2u &&
               (!meets_bits || native_query_projects(query)) &&
               cetta_dense_query_prepare(query->source, arena, query->patterns,
                                         query->pattern_count, &dense_query)) {
        /* Binary relations dense enough for rows of bits join by them. */
        variables.len = cetta_dense_query_variable_count(dense_query);
        for (size_t i = 0u; i < variables.len; i++)
            variables.ids[i] = cetta_dense_query_variable_at(dense_query, i);
    } else if ((dense || !selective) && !meets_bits &&
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
        loops = calloc(1u, sizeof(*loops));
        if (!loops)
            return CETTA_NATIVE_QUERY_DECLINED;
        loops->space = query->source;
        loops->universe = universe;
        loops->arena = arena;
        arena_init_detached(&loops->probes);
        if (!native_query_loops_prepare(loops, &variables, query->patterns,
                                        query->pattern_count)) {
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
        malloc(sizeof(*templates) * (effect_count + 1u));
    NativeQueryIds *pending =
        calloc(effect_count ? effect_count : 1u, sizeof(*pending));
    NativeQueryIds *waiting =
        calloc(effect_count ? effect_count : 1u, sizeof(*waiting));
    bool *by_pairs = calloc(effect_count ? effect_count : 1u,
                            sizeof(*by_pairs));
    bool admitted = templates && pending && waiting && by_pairs &&
        native_query_compile(&templates[effect_count], &variables, universe,
                             arena, false, query->template_atom);
    bool removes = false;
    for (size_t effect = 0u; admitted && effect < effect_count; effect++) {
        admitted = native_query_compile(&templates[effect], &variables,
                                        universe, arena, true,
                                        query->effects[effect].data);
        removes |= query->effects[effect].removal;
    }
    for (size_t effect = 0u; admitted && !removes && effect < effect_count;
         effect++) {
        by_pairs[effect] = native_query_adds_pairs(
            query, &query->effects[effect], &templates[effect]);
    }
    if (!admitted) {
        free(templates);
        free(pending);
        free(waiting);
        free(by_pairs);
        if (loops)
            arena_free(&loops->probes);
        free(loops);
        cetta_relational_plan_free(plan);
        cetta_dense_query_free(dense_query);
        return CETTA_NATIVE_QUERY_DECLINED;
    }

    NativeQueryRun run = {
        .query = query,
        .answer = &templates[effect_count],
        .effects = templates,
        .universe = universe,
        .arena = arena,
        .deferred = deferred,
        .pending = pending,
        .waiting = waiting,
        .by_pairs = by_pairs,
    };
    uint64_t answers = 0u;
    bool done = false;
    bool declined = false;
    if (dense_query) {
        /* When every effect adds into a set and no answer is read but its
         * count, a variable no effect reads is summed out: each instance
         * is made once however many solutions give it. */
        bool summed = query->count_only || run.answer->ground;
        for (size_t effect = 0u; summed && effect < effect_count; effect++) {
            summed = !query->effects[effect].removal &&
                space_is_set(query->effects[effect].target);
        }
        bool kept[NATIVE_QUERY_VARIABLES] = {false};
        bool any_kept = false;
        for (size_t effect = 0u; summed && effect < effect_count; effect++) {
            const NativeQueryTemplate *made = &templates[effect];
            for (size_t node = 0u; node < made->len; node++) {
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
        done = native_query_loops_leg(loops, 0u);
        answers = loops->answers;
        declined = loops->declined;
    }
    for (size_t effect = 0u; done && !deferred && effect < effect_count;
         effect++)
        done = native_query_flush(&run, effect);
    if (done && deferred)
        done = native_query_make_deferred(&run);
    if (done && !query->count_only) {
        /* The answers' expression, built in place: a ground template is
         * every answer. */
        uint64_t len = run.answer->ground ? answers : run.item_len;
        Atom *collection = cetta_expr_len_mul_fits_size((CettaExprLen)len,
                                                        sizeof(Atom *))
            ? atom_expr_builder_begin(arena, (CettaExprLen)len) : NULL;
        if (collection) {
            Atom **elems = collection->expr.elems;
            if (run.answer->ground) {
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
    free(by_pairs);
    free(run.items);
    free(templates);
    if (loops) {
        free(loops->exact);
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

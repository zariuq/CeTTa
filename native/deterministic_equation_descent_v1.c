#include "deterministic_equation_descent_v1.h"

#include "src/symbol.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Structural descent of deterministic equations.
 *
 * The norm of a value: an atom is 1; a cell (LCons h t) is 1 + |h| + |t|; a
 * value view (W x), W a constructor of langdef:value-view, is |x|; any other
 * expression or list value of n elements is 1 + n + the norms of its
 * elements.  So the norm-keeping primitives of the vocabulary
 * (langdef:expression->list, langdef:list->expression,
 * langdef:list->list-value, langdef:value-view) keep it, and every value has
 * norm at least 1.
 *
 * A pattern with a symbol at the head of each of its expressions (a headed
 * pattern) determines the kind of every value it matches, so the norm of the
 * value is the pattern's overhead, its norm with variables counted 0, plus
 * the norms of the values of its variables.
 *
 * A call g(a1 ... am) that an equation f(p1 ... pn) = r makes, at a level with
 * sets S(f), S(g) and potentials phi, is bounded as follows.  Each argument at
 * S(g) has a cost: a use of a variable of a pattern at S(f) costs 0 and uses
 * the variable, reached also through let binders that carry a variable or a
 * norm-keeping view of one; an atom and an atom-valued primitive cost 1; a
 * value view and a norm-keeping primitive cost their argument; a cell, a list
 * value and any other constructor cost their overhead plus their elements.
 * The uses of all these arguments are distinct.  Then
 *
 *   sum_{j in S(g)} |a_j| - sum_{i in S(f)} |p_i|
 *     <= sum_j cost(a_j) - sum_i (overhead(p_i) + #unused variables of p_i)
 *
 * since each unused variable has norm at least 1.  Call the right side w.
 * The call descends at the level when w + phi(g) - phi(f) <= -1, and does not
 * grow when it is <= 0.  A certificate has levels; every call must not grow at
 * each level before one where it descends.  The measure of a call, the vector
 * of its per-level sums plus potentials, then decreases lexicographically
 * along every call, so every evaluation ends.
 *
 * The search: level 0 ranks the strongly connected components of the call
 * graph (callees below callers).  Within a component, a measure level takes
 * the largest sets whose arguments are bounded (first counting only uses,
 * atoms, views and primitives, then constructors too) and the longest-path
 * potentials; its descending calls are decided, and the calls left at zero
 * form components of their own, ranked at the next level and measured at the
 * one after.  The certificate found is then checked call by call as stated. */

typedef struct {
    SymbolId head;
    uint32_t arity;
    uint64_t headed; /* positions headed in every equation of the function */
} DescentFunction;

/* A let binder in scope at a call: the variable it carries, if any. */
typedef struct {
    VarId var;
    bool carries;
    VarId carried;
} DescentLet;

typedef struct {
    uint32_t f;
    uint32_t g;
    uint32_t rule;
    const Atom *const *params;
    uint32_t param_count;
    const Atom *const *args;
    uint32_t arg_count;
    DescentLet *env;
    uint32_t env_count;
    int64_t weight;
} DescentEdge;

typedef struct {
    uint64_t *sets;
    int64_t *phi;
} DescentLevel;

typedef struct {
    VarId *items;
    uint32_t len;
    uint32_t cap;
    bool oom;
} DescentUses;

typedef struct {
    const CettaDescentRuleV1 *rules;
    uint32_t rule_count;
    const CettaDeterministicVocabularyV1 *vocabulary;
    DescentFunction *functions;
    uint32_t function_count;
    DescentEdge *edges;
    uint32_t edge_count;
    uint32_t edge_cap;
    uint64_t *sets;
    int64_t *phi;
    bool structural;
    DescentLevel *levels;
    uint32_t level_count;
    bool oom;
    char *error;
    size_t error_size;
} DescentContext;

static bool descent_fail(DescentContext *ctx, const char *format, ...) {
    if (ctx->error && ctx->error_size > 0u) {
        va_list arguments;
        va_start(arguments, format);
        (void)vsnprintf(ctx->error, ctx->error_size, format, arguments);
        va_end(arguments);
    }
    return false;
}

static bool descent_out_of_memory(DescentContext *ctx) {
    ctx->oom = true;
    return descent_fail(ctx, "the descent check ran out of memory");
}

static bool descent_symbol_is(const Atom *atom, const char *name) {
    return atom && atom->kind == ATOM_SYMBOL &&
           strcmp(symbol_bytes(g_symbols, atom->sym_id), name) == 0;
}

static const char *const descent_views[] = {
    "LangDef:ExpressionValue", "LangDef:ListValue", "LangDef:SymbolValue",
    "LangDef:StringValue", "LangDef:IntegerValue", "LangDef:FloatValue", NULL};

static bool descent_is_view_symbol(const Atom *atom) {
    if (!atom || atom->kind != ATOM_SYMBOL)
        return false;
    const char *name = symbol_bytes(g_symbols, atom->sym_id);
    for (uint32_t i = 0u; descent_views[i]; i++) {
        if (strcmp(name, descent_views[i]) == 0)
            return true;
    }
    return false;
}

static bool descent_symbol_headed(const Atom *atom) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len > 0u &&
           atom->expr.elems[0] && atom->expr.elems[0]->kind == ATOM_SYMBOL;
}

/* ------------------------------------------------------------ functions */

static int32_t descent_function_at(const DescentContext *ctx, SymbolId head,
                                   uint32_t arity) {
    for (uint32_t i = 0u; i < ctx->function_count; i++) {
        if (ctx->functions[i].head == head && ctx->functions[i].arity == arity)
            return (int32_t)i;
    }
    return -1;
}

static bool descent_defines(const DescentContext *ctx, SymbolId head) {
    for (uint32_t i = 0u; i < ctx->function_count; i++) {
        if (ctx->functions[i].head == head)
            return true;
    }
    return false;
}

typedef enum {
    DESCENT_HEAD_CALL,
    DESCENT_HEAD_FAILS,
    DESCENT_HEAD_ATOM,
    DESCENT_HEAD_PRESERVING,
    DESCENT_HEAD_STRUCTURE,
    DESCENT_HEAD_CELL,
    DESCENT_HEAD_VIEW,
    DESCENT_HEAD_CONSTRUCTOR
} DescentHeadKind;

/* What evaluation does with a symbol-headed call, as the evaluator decides:
 * an equation of the head at the call's arity, a failure for a head defined
 * at other arities only, the vocabulary's primitive, else a constructor. */
static DescentHeadKind descent_head_kind(const DescentContext *ctx,
                                         const Atom *term) {
    SymbolId head = term->expr.elems[0]->sym_id;
    uint32_t arity = (uint32_t)(term->expr.len - 1u);
    if (descent_function_at(ctx, head, arity) >= 0)
        return DESCENT_HEAD_CALL;
    if (descent_defines(ctx, head))
        return DESCENT_HEAD_FAILS;
    CettaDeterministicPrimitiveClassV1 class_ =
        CETTA_DETERMINISTIC_PRIMITIVE_CLASS_V1_NONE;
    if (ctx->vocabulary && ctx->vocabulary->classify)
        class_ = ctx->vocabulary->classify(
            ctx->vocabulary->context, symbol_bytes(g_symbols, head), arity);
    switch (class_) {
    case CETTA_DETERMINISTIC_PRIMITIVE_CLASS_V1_ATOM:
        return DESCENT_HEAD_ATOM;
    case CETTA_DETERMINISTIC_PRIMITIVE_CLASS_V1_PRESERVING:
        return arity == 1u ? DESCENT_HEAD_PRESERVING : DESCENT_HEAD_STRUCTURE;
    case CETTA_DETERMINISTIC_PRIMITIVE_CLASS_V1_STRUCTURE:
        return DESCENT_HEAD_STRUCTURE;
    case CETTA_DETERMINISTIC_PRIMITIVE_CLASS_V1_NONE:
        break;
    }
    if (arity == 2u && descent_symbol_is(term->expr.elems[0], "LCons"))
        return DESCENT_HEAD_CELL;
    if (arity == 1u && descent_is_view_symbol(term->expr.elems[0]))
        return DESCENT_HEAD_VIEW;
    return DESCENT_HEAD_CONSTRUCTOR;
}

/* ------------------------------------------------------------- patterns */

/* A symbol at the head of each of the pattern's expressions. */
static bool descent_headed(const Atom *p) {
    if (!p)
        return false;
    if (p->kind != ATOM_EXPR || p->expr.len == 0u)
        return true;
    if (atom_is_list(p)) {
        for (CettaExprIndex k = 1u; k < p->expr.len; k++) {
            if (!descent_headed(p->expr.elems[k]))
                return false;
        }
        return true;
    }
    if (!descent_symbol_headed(p))
        return false;
    for (CettaExprIndex k = 1u; k < p->expr.len; k++) {
        if (!descent_headed(p->expr.elems[k]))
            return false;
    }
    return true;
}

/* The norm of a headed pattern with its variables counted 0. */
static int64_t descent_overhead(const Atom *p) {
    if (p->kind == ATOM_VAR)
        return 0;
    if (p->kind != ATOM_EXPR || p->expr.len == 0u)
        return 1;
    int64_t total;
    CettaExprIndex first;
    if (atom_is_list(p)) {
        total = 1 + (int64_t)(p->expr.len - 1u);
        first = 1u;
    } else if (p->expr.len == 3u && descent_symbol_is(p->expr.elems[0], "LCons")) {
        total = 1;
        first = 1u;
    } else if (p->expr.len == 2u && descent_is_view_symbol(p->expr.elems[0])) {
        total = 0;
        first = 1u;
    } else {
        total = 1 + (int64_t)p->expr.len + 1;
        first = 1u;
    }
    for (CettaExprIndex k = first; k < p->expr.len; k++)
        total += descent_overhead(p->expr.elems[k]);
    return total;
}

static bool descent_pattern_has_var(const Atom *p, VarId var) {
    if (!p)
        return false;
    if (p->kind == ATOM_VAR)
        return p->var_id == var;
    if (p->kind != ATOM_EXPR)
        return false;
    for (CettaExprIndex k = 0u; k < p->expr.len; k++) {
        if (descent_pattern_has_var(p->expr.elems[k], var))
            return true;
    }
    return false;
}

static bool descent_uses_has(const DescentUses *uses, VarId var) {
    for (uint32_t i = 0u; i < uses->len; i++) {
        if (uses->items[i] == var)
            return true;
    }
    return false;
}

/* The variables of a pattern that no argument used. */
static int64_t descent_unused(const Atom *p, const DescentUses *uses) {
    if (!p)
        return 0;
    if (p->kind == ATOM_VAR)
        return descent_uses_has(uses, p->var_id) ? 0 : 1;
    if (p->kind != ATOM_EXPR)
        return 0;
    int64_t total = 0;
    for (CettaExprIndex k = 0u; k < p->expr.len; k++)
        total += descent_unused(p->expr.elems[k], uses);
    return total;
}

/* ------------------------------------------------------------- the calls */

static const DescentLet *descent_env_lookup(const DescentLet *env,
                                            uint32_t count, VarId var) {
    for (uint32_t i = count; i > 0u; i--) {
        if (env[i - 1u].var == var)
            return &env[i - 1u];
    }
    return NULL;
}

/* A view of a value that keeps its norm: a value-view constructor or a
 * norm-keeping primitive of one argument. */
static const Atom *descent_view_argument(const DescentContext *ctx,
                                         const Atom *term) {
    if (!descent_symbol_headed(term) || term->expr.len != 2u)
        return NULL;
    DescentHeadKind kind = descent_head_kind(ctx, term);
    return kind == DESCENT_HEAD_VIEW || kind == DESCENT_HEAD_PRESERVING
        ? term->expr.elems[1] : NULL;
}

/* The variable a term carries through views, resolved through the binders in
 * scope, or false when it carries none. */
static bool descent_carried(const DescentContext *ctx, const Atom *term,
                            const DescentLet *env, uint32_t count,
                            VarId *carried) {
    for (;;) {
        const Atom *inner = descent_view_argument(ctx, term);
        if (!inner)
            break;
        term = inner;
    }
    if (!term || term->kind != ATOM_VAR)
        return false;
    const DescentLet *outer = descent_env_lookup(env, count, term->var_id);
    if (!outer) {
        *carried = term->var_id;
        return true;
    }
    if (!outer->carries)
        return false;
    *carried = outer->carried;
    return true;
}

typedef struct {
    DescentLet *items;
    uint32_t len;
    uint32_t cap;
} DescentEnvStack;

static bool descent_add_edge(DescentContext *ctx, uint32_t f, uint32_t rule,
                             const Atom *call, const DescentEnvStack *env) {
    int32_t g = descent_function_at(ctx, call->expr.elems[0]->sym_id,
                                    (uint32_t)(call->expr.len - 1u));
    if (g < 0)
        return true;
    if (ctx->edge_count == ctx->edge_cap) {
        uint32_t cap = ctx->edge_cap ? ctx->edge_cap * 2u : 64u;
        DescentEdge *grown = realloc(ctx->edges, (size_t)cap * sizeof(*grown));
        if (!grown)
            return descent_out_of_memory(ctx);
        ctx->edges = grown;
        ctx->edge_cap = cap;
    }
    DescentLet *copy = NULL;
    if (env->len > 0u) {
        copy = malloc((size_t)env->len * sizeof(*copy));
        if (!copy)
            return descent_out_of_memory(ctx);
        memcpy(copy, env->items, (size_t)env->len * sizeof(*copy));
    }
    const Atom *left = ctx->rules[rule].left;
    ctx->edges[ctx->edge_count++] = (DescentEdge){
        .f = f,
        .g = (uint32_t)g,
        .rule = rule,
        .params = (const Atom *const *)left->expr.elems + 1,
        .param_count = (uint32_t)(left->expr.len - 1u),
        .args = (const Atom *const *)call->expr.elems + 1,
        .arg_count = (uint32_t)(call->expr.len - 1u),
        .env = copy,
        .env_count = env->len,
    };
    return true;
}

/* Every call evaluation of the right side can make: evaluation is call by
 * value, so every subterm is evaluated, the binder of a let excepted, and a
 * let's body sees its binder. */
static bool descent_collect_calls(DescentContext *ctx, uint32_t f, uint32_t rule,
                                  const Atom *term, DescentEnvStack *env) {
    if (!term || term->kind != ATOM_EXPR || term->expr.len == 0u)
        return true;
    if (term->expr.len == 4u && descent_symbol_is(term->expr.elems[0], "let")) {
        if (!descent_collect_calls(ctx, f, rule, term->expr.elems[2], env))
            return false;
        if (!term->expr.elems[1] || term->expr.elems[1]->kind != ATOM_VAR)
            return true; /* evaluation fails before the body */
        if (env->len == env->cap) {
            uint32_t cap = env->cap ? env->cap * 2u : 8u;
            DescentLet *grown = realloc(env->items, (size_t)cap * sizeof(*grown));
            if (!grown)
                return descent_out_of_memory(ctx);
            env->items = grown;
            env->cap = cap;
        }
        VarId carried = 0;
        bool carries = descent_carried(ctx, term->expr.elems[2], env->items,
                                       env->len, &carried);
        env->items[env->len++] = (DescentLet){
            .var = term->expr.elems[1]->var_id,
            .carries = carries,
            .carried = carried,
        };
        bool ok = descent_collect_calls(ctx, f, rule, term->expr.elems[3], env);
        env->len--;
        return ok;
    }
    if (term->expr.len == 2u &&
        descent_symbol_is(term->expr.elems[0], "metta-nullary"))
        return true;
    CettaExprIndex first = 0u;
    if (descent_symbol_headed(term)) {
        if (!descent_add_edge(ctx, f, rule, term, env))
            return false;
        first = 1u;
    }
    for (CettaExprIndex k = first; k < term->expr.len; k++) {
        if (!descent_collect_calls(ctx, f, rule, term->expr.elems[k], env))
            return false;
    }
    return true;
}

/* ---------------------------------------------------------------- costs */

static bool descent_use(const DescentEdge *edge, uint64_t set_f, VarId var,
                        DescentUses *uses) {
    bool found = false;
    for (uint32_t i = 0u; i < edge->param_count && i < 64u && !found; i++) {
        if ((set_f & (UINT64_C(1) << i)) != 0u &&
            descent_pattern_has_var(edge->params[i], var))
            found = true;
    }
    if (!found || descent_uses_has(uses, var))
        return false;
    if (uses->len == uses->cap) {
        uint32_t cap = uses->cap ? uses->cap * 2u : 16u;
        VarId *grown = realloc(uses->items, (size_t)cap * sizeof(*grown));
        if (!grown) {
            uses->oom = true;
            return false;
        }
        uses->items = grown;
        uses->cap = cap;
    }
    uses->items[uses->len++] = var;
    return true;
}

/* The cost of an argument: the norm its value may have beyond the norms of
 * the variables it uses, recorded in uses; false when it has no bound. */
static bool descent_cost(const DescentContext *ctx, const DescentEdge *edge,
                         uint64_t set_f, bool structural, const Atom *a,
                         DescentUses *uses, int64_t *cost) {
    if (!a)
        return false;
    if (a->kind == ATOM_VAR) {
        VarId var = a->var_id;
        const DescentLet *bound = descent_env_lookup(edge->env, edge->env_count, var);
        if (bound) {
            if (!bound->carries)
                return false;
            var = bound->carried;
        }
        if (!descent_use(edge, set_f, var, uses))
            return false;
        *cost = 0;
        return true;
    }
    if (a->kind != ATOM_EXPR || a->expr.len == 0u) {
        *cost = 1;
        return true;
    }
    int64_t total = 0;
    CettaExprIndex first = 0u;
    if (atom_is_list(a)) {
        if (structural)
            return false;
        total = 1 + (int64_t)(a->expr.len - 1u);
        first = 1u;
    } else if (atom_is_list_rest(a)) {
        return false;
    } else if (!descent_symbol_headed(a)) {
        if (structural)
            return false;
        total = 1 + (int64_t)a->expr.len;
        first = 0u;
    } else if (a->expr.len == 4u && descent_symbol_is(a->expr.elems[0], "let")) {
        return false;
    } else if (a->expr.len == 2u &&
               descent_symbol_is(a->expr.elems[0], "metta-nullary")) {
        if (!a->expr.elems[1] || a->expr.elems[1]->kind != ATOM_SYMBOL)
            return false;
        *cost = 3;
        return true;
    } else {
        switch (descent_head_kind(ctx, a)) {
        case DESCENT_HEAD_CALL:
        case DESCENT_HEAD_FAILS:
        case DESCENT_HEAD_STRUCTURE:
            return false;
        case DESCENT_HEAD_ATOM:
            *cost = 1;
            return true;
        case DESCENT_HEAD_PRESERVING:
        case DESCENT_HEAD_VIEW:
            return descent_cost(ctx, edge, set_f, structural, a->expr.elems[1],
                                uses, cost);
        case DESCENT_HEAD_CELL:
            if (structural)
                return false;
            total = 1;
            first = 1u;
            break;
        case DESCENT_HEAD_CONSTRUCTOR:
            if (structural)
                return false;
            total = 1 + (int64_t)a->expr.len + 1;
            first = 1u;
            break;
        }
    }
    for (CettaExprIndex k = first; k < a->expr.len; k++) {
        int64_t child = 0;
        if (!descent_cost(ctx, edge, set_f, structural, a->expr.elems[k], uses,
                          &child))
            return false;
        total += child;
    }
    *cost = total;
    return true;
}

/* A use of a variable, through views and carrying binders. */
static bool descent_plain(const DescentContext *ctx, const DescentEdge *edge,
                          const Atom *a) {
    VarId carried = 0;
    return descent_carried(ctx, a, edge->env, edge->env_count, &carried);
}

/* The bound w of a call under sets set_f and set_g, and the positions of
 * set_g whose arguments have no bound.  Plain uses claim their variables
 * before constructed arguments do. */
static bool descent_weigh(const DescentContext *ctx, const DescentEdge *edge,
                          uint64_t set_f, uint64_t set_g, bool structural,
                          int64_t *weight, uint64_t *uncovered) {
    DescentUses uses = {0};
    int64_t total = 0;
    *uncovered = 0u;
    for (int pass = 0; pass < 2; pass++) {
        for (uint32_t j = 0u; j < edge->arg_count && j < 64u; j++) {
            if ((set_g & (UINT64_C(1) << j)) == 0u)
                continue;
            if ((pass == 0) != descent_plain(ctx, edge, edge->args[j]))
                continue;
            int64_t cost = 0;
            if (!descent_cost(ctx, edge, set_f, structural, edge->args[j], &uses,
                              &cost)) {
                *uncovered |= UINT64_C(1) << j;
                continue;
            }
            total += cost;
        }
    }
    if (uses.oom)
        *uncovered = set_g;
    int64_t slack = 0;
    for (uint32_t i = 0u; i < edge->param_count && i < 64u; i++) {
        if ((set_f & (UINT64_C(1) << i)) == 0u)
            continue;
        slack += descent_overhead(edge->params[i]) +
                 descent_unused(edge->params[i], &uses);
    }
    *weight = total - slack;
    free(uses.items);
    return !uses.oom;
}

/* ------------------------------------------------------------ components */

/* Components of the functions in member over the edges edge_on selects;
 * ids are given callees first. */
static bool descent_components(const DescentContext *ctx, const bool *member,
                               const bool *edge_on, uint32_t *component,
                               uint32_t *component_count) {
    uint32_t n = ctx->function_count;
    uint32_t *index = malloc((size_t)(n ? n : 1u) * sizeof(*index));
    uint32_t *low = malloc((size_t)(n ? n : 1u) * sizeof(*low));
    bool *on = calloc(n ? n : 1u, sizeof(*on));
    uint32_t *stack = malloc((size_t)(n ? n : 1u) * sizeof(*stack));
    uint32_t *call_stack = malloc((size_t)(n ? n : 1u) * sizeof(*call_stack));
    uint32_t *next_edge = malloc((size_t)(n ? n : 1u) * sizeof(*next_edge));
    if (!index || !low || !on || !stack || !call_stack || !next_edge) {
        free(index); free(low); free(on); free(stack); free(call_stack); free(next_edge);
        return false;
    }
    for (uint32_t v = 0u; v < n; v++) {
        index[v] = UINT32_MAX;
        component[v] = UINT32_MAX;
    }
    uint32_t counter = 0u, stack_len = 0u, count = 0u;
    for (uint32_t root = 0u; root < n; root++) {
        if (!member[root] || index[root] != UINT32_MAX)
            continue;
        uint32_t depth = 0u;
        call_stack[depth] = root;
        next_edge[depth] = 0u;
        index[root] = low[root] = counter++;
        stack[stack_len++] = root;
        on[root] = true;
        while (depth != UINT32_MAX) {
            uint32_t v = call_stack[depth];
            bool descended = false;
            while (next_edge[depth] < ctx->edge_count) {
                uint32_t e = next_edge[depth]++;
                const DescentEdge *edge = &ctx->edges[e];
                if (edge->f != v || !edge_on[e] || !member[edge->g])
                    continue;
                uint32_t w = edge->g;
                if (index[w] == UINT32_MAX) {
                    index[w] = low[w] = counter++;
                    stack[stack_len++] = w;
                    on[w] = true;
                    depth++;
                    call_stack[depth] = w;
                    next_edge[depth] = 0u;
                    descended = true;
                    break;
                }
                if (on[w] && index[w] < low[v])
                    low[v] = index[w];
            }
            if (descended)
                continue;
            if (low[v] == index[v]) {
                uint32_t w;
                do {
                    w = stack[--stack_len];
                    on[w] = false;
                    component[w] = count;
                } while (w != v);
                count++;
            }
            if (depth == 0u) {
                depth = UINT32_MAX;
            } else {
                depth--;
                uint32_t parent = call_stack[depth];
                if (low[v] < low[parent])
                    low[parent] = low[v];
            }
        }
    }
    *component_count = count;
    free(index); free(low); free(on); free(stack); free(call_stack); free(next_edge);
    return true;
}

/* ----------------------------------------------------------- certificate */

static bool descent_level_at(DescentContext *ctx, uint32_t level) {
    while (ctx->level_count <= level) {
        DescentLevel *grown = realloc(
            ctx->levels, (size_t)(ctx->level_count + 1u) * sizeof(*grown));
        if (!grown)
            return descent_out_of_memory(ctx);
        ctx->levels = grown;
        uint32_t n = ctx->function_count ? ctx->function_count : 1u;
        DescentLevel *fresh = &ctx->levels[ctx->level_count];
        fresh->sets = calloc(n, sizeof(*fresh->sets));
        fresh->phi = calloc(n, sizeof(*fresh->phi));
        if (!fresh->sets || !fresh->phi) {
            free(fresh->sets);
            free(fresh->phi);
            return descent_out_of_memory(ctx);
        }
        ctx->level_count++;
    }
    return true;
}

static bool descent_record(DescentContext *ctx, uint32_t level,
                           const bool *members, const uint64_t *sets,
                           const int64_t *phi) {
    if (!descent_level_at(ctx, level))
        return false;
    for (uint32_t v = 0u; v < ctx->function_count; v++) {
        if (!members[v])
            continue;
        ctx->levels[level].sets[v] = sets ? sets[v] : 0u;
        ctx->levels[level].phi[v] = phi[v];
    }
    return true;
}

static void descent_report(DescentContext *ctx, const DescentEdge *edge) {
    const CettaDescentRuleV1 *rule = &ctx->rules[edge->rule];
    (void)descent_fail(
        ctx, "%s: its call of %s does not descend",
        rule->name ? rule->name : "an equation rule",
        symbol_bytes(g_symbols, ctx->functions[edge->g].head));
}

/* --------------------------------------------------------------- search */

static bool descent_level(DescentContext *ctx, const bool *members, bool *edge_in,
                          uint32_t level);

/* Under the sets and potentials of one mode, the descending calls of
 * edge_in, then the calls left at zero, ranked at level + 1 and measured as
 * components of their own from level + 2. */
static bool descent_level_mode(DescentContext *ctx, const bool *members,
                               bool *edge_in, uint32_t level, bool *failed_here) {
    uint32_t n = ctx->function_count;
    *failed_here = true;
    for (uint32_t v = 0u; v < n; v++) {
        if (members[v])
            ctx->sets[v] = ctx->functions[v].headed;
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (uint32_t e = 0u; e < ctx->edge_count; e++) {
            if (!edge_in[e])
                continue;
            DescentEdge *edge = &ctx->edges[e];
            uint64_t uncovered = 0u;
            if (!descent_weigh(ctx, edge, ctx->sets[edge->f], ctx->sets[edge->g],
                               ctx->structural, &edge->weight, &uncovered))
                return descent_out_of_memory(ctx);
            if (uncovered) {
                ctx->sets[edge->g] &= ~uncovered;
                changed = true;
            }
        }
    }
    for (uint32_t e = 0u; e < ctx->edge_count; e++) {
        if (!edge_in[e])
            continue;
        DescentEdge *edge = &ctx->edges[e];
        uint64_t uncovered = 0u;
        if (!descent_weigh(ctx, edge, ctx->sets[edge->f], ctx->sets[edge->g],
                           ctx->structural, &edge->weight, &uncovered))
            return descent_out_of_memory(ctx);
    }
    /* potentials: phi(f) >= w + phi(g), the longest paths */
    for (uint32_t v = 0u; v < n; v++)
        ctx->phi[v] = 0;
    bool stable = false;
    for (uint32_t round = 0u; round <= n + 1u && !stable; round++) {
        stable = true;
        for (uint32_t e = 0u; e < ctx->edge_count; e++) {
            if (!edge_in[e])
                continue;
            const DescentEdge *edge = &ctx->edges[e];
            if (ctx->phi[edge->f] < edge->weight + ctx->phi[edge->g]) {
                ctx->phi[edge->f] = edge->weight + ctx->phi[edge->g];
                stable = false;
            }
        }
    }
    if (!stable)
        return false; /* a cycle of calls grows under these sets */
    bool *rest = calloc(ctx->edge_count ? ctx->edge_count : 1u, sizeof(*rest));
    if (!rest)
        return descent_out_of_memory(ctx);
    bool some_strict = false;
    for (uint32_t e = 0u; e < ctx->edge_count; e++) {
        if (!edge_in[e])
            continue;
        const DescentEdge *edge = &ctx->edges[e];
        bool strict = edge->weight + ctx->phi[edge->g] - ctx->phi[edge->f] <= -1;
        some_strict |= strict;
        rest[e] = !strict;
    }
    if (!some_strict) {
        free(rest);
        return false;
    }
    *failed_here = false;
    if (!descent_record(ctx, level, members, ctx->sets, ctx->phi)) {
        free(rest);
        return false;
    }
    uint32_t *component = malloc((size_t)(n ? n : 1u) * sizeof(*component));
    int64_t *rank = malloc((size_t)(n ? n : 1u) * sizeof(*rank));
    bool *sub_members = calloc(n ? n : 1u, sizeof(*sub_members));
    bool *sub_edges = calloc(ctx->edge_count ? ctx->edge_count : 1u, sizeof(*sub_edges));
    uint32_t count = 0u;
    bool ok = component && rank && sub_members && sub_edges;
    if (!ok)
        (void)descent_out_of_memory(ctx);
    if (ok && !descent_components(ctx, members, rest, component, &count))
        ok = descent_out_of_memory(ctx);
    if (ok) {
        for (uint32_t v = 0u; v < n; v++)
            rank[v] = members[v] ? (int64_t)component[v] : 0;
        ok = descent_record(ctx, level + 1u, members, NULL, rank);
    }
    for (uint32_t c = 0u; ok && c < count; c++) {
        bool inner = false;
        for (uint32_t v = 0u; v < n; v++)
            sub_members[v] = members[v] && component[v] == c;
        for (uint32_t e = 0u; e < ctx->edge_count; e++) {
            const DescentEdge *edge = &ctx->edges[e];
            sub_edges[e] = rest[e] && sub_members[edge->f] && sub_members[edge->g];
            inner |= sub_edges[e];
        }
        if (inner)
            ok = descent_level(ctx, sub_members, sub_edges, level + 2u);
    }
    free(rest); free(component); free(rank); free(sub_members); free(sub_edges);
    return ok;
}

/* The calls in edge_in all lie within one component (members).  True when a
 * measure level decides some of them, with uses only, or else with
 * constructed arguments counted, and the rest descend at later levels. */
static bool descent_level(DescentContext *ctx, const bool *members, bool *edge_in,
                          uint32_t level) {
    bool any = false;
    for (uint32_t e = 0u; e < ctx->edge_count && !any; e++)
        any = edge_in[e];
    if (!any)
        return true;
    if (level > 2u * ctx->function_count + 2u)
        return descent_fail(ctx, "descent levels exhausted");
    /* A mode that fails leaves records only for functions whose calls a later
     * attempt either records again or decides at an earlier level. */
    bool failed_here = true;
    for (int mode = 0; mode < 2; mode++) {
        ctx->structural = mode == 0;
        bool ok = descent_level_mode(ctx, members, edge_in, level, &failed_here);
        if (ok)
            return true;
        if (ctx->oom)
            return false;
    }
    if (failed_here) {
        /* Name a call that is a cycle by itself and does not decrease, when
         * there is one: a function calling itself with bound w >= 0. */
        const DescentEdge *blamed = NULL;
        for (uint32_t e = 0u; e < ctx->edge_count && !blamed; e++) {
            const DescentEdge *edge = &ctx->edges[e];
            if (edge_in[e] && edge->f == edge->g && edge->weight >= 0)
                blamed = edge;
        }
        for (uint32_t e = 0u; e < ctx->edge_count && !blamed; e++) {
            if (edge_in[e])
                blamed = &ctx->edges[e];
        }
        if (blamed)
            descent_report(ctx, blamed);
    }
    return false;
}

/* --------------------------------------------------------- verification */

/* Every call must not grow at each level before one where it descends, by
 * the bound of its syntax under the certificate's sets and potentials. */
static bool descent_verify(DescentContext *ctx) {
    for (uint32_t e = 0u; e < ctx->edge_count; e++) {
        const DescentEdge *edge = &ctx->edges[e];
        bool decided = false;
        for (uint32_t level = 0u; level < ctx->level_count && !decided; level++) {
            const DescentLevel *l = &ctx->levels[level];
            uint64_t set_f = l->sets[edge->f];
            uint64_t set_g = l->sets[edge->g];
            if ((set_f & ~ctx->functions[edge->f].headed) != 0u ||
                (set_g & ~ctx->functions[edge->g].headed) != 0u) {
                descent_report(ctx, edge);
                return false;
            }
            int64_t weight = 0;
            uint64_t uncovered = 0u;
            if (!descent_weigh(ctx, edge, set_f, set_g, false, &weight, &uncovered))
                return descent_out_of_memory(ctx);
            if (uncovered) {
                descent_report(ctx, edge);
                return false;
            }
            int64_t bound = weight + l->phi[edge->g] - l->phi[edge->f];
            if (bound <= -1) {
                decided = true;
            } else if (bound > 0) {
                descent_report(ctx, edge);
                return false;
            }
        }
        if (!decided) {
            descent_report(ctx, edge);
            return false;
        }
    }
    return true;
}

bool cetta_deterministic_equation_descends_v1(
    const CettaDescentRuleV1 *rules, uint32_t rule_count,
    const CettaDeterministicVocabularyV1 *vocabulary,
    bool *resource_failure, char *error, size_t error_size) {
    DescentContext ctx = {0};
    ctx.rules = rules;
    ctx.rule_count = rule_count;
    ctx.vocabulary = vocabulary;
    ctx.error = error;
    ctx.error_size = error_size;
    if (resource_failure)
        *resource_failure = false;
    bool ok = false;
    uint32_t *rule_function = NULL;
    bool *members = NULL;
    bool *all_edges = NULL;
    uint32_t *component = NULL;
    int64_t *rank = NULL;
    bool *comp_members = NULL;
    bool *comp_edges = NULL;
    DescentEnvStack env = {0};

    ctx.functions = calloc(rule_count ? rule_count : 1u, sizeof(*ctx.functions));
    rule_function = calloc(rule_count ? rule_count : 1u, sizeof(*rule_function));
    if (!ctx.functions || !rule_function) {
        (void)descent_out_of_memory(&ctx);
        goto done;
    }
    for (uint32_t r = 0u; r < rule_count; r++) {
        const Atom *left = rules[r].left;
        if (!descent_symbol_headed(left)) {
            (void)descent_fail(&ctx, "%s: its left side is not a call",
                               rules[r].name ? rules[r].name : "an equation rule");
            goto done;
        }
        SymbolId head = left->expr.elems[0]->sym_id;
        uint32_t arity = (uint32_t)(left->expr.len - 1u);
        int32_t f = descent_function_at(&ctx, head, arity);
        if (f < 0) {
            uint64_t all = arity >= 64u ? UINT64_MAX
                                        : (UINT64_C(1) << arity) - 1u;
            ctx.functions[ctx.function_count] =
                (DescentFunction){head, arity, all};
            f = (int32_t)ctx.function_count++;
        }
        rule_function[r] = (uint32_t)f;
        for (uint32_t i = 0u; i < arity && i < 64u; i++) {
            if (!descent_headed(left->expr.elems[i + 1u]))
                ctx.functions[f].headed &= ~(UINT64_C(1) << i);
        }
    }
    for (uint32_t r = 0u; r < rule_count; r++) {
        if (!descent_collect_calls(&ctx, rule_function[r], r, rules[r].right, &env))
            goto done;
    }
    uint32_t n = ctx.function_count ? ctx.function_count : 1u;
    ctx.sets = calloc(n, sizeof(*ctx.sets));
    ctx.phi = calloc(n, sizeof(*ctx.phi));
    members = calloc(n, sizeof(*members));
    all_edges = calloc(ctx.edge_count ? ctx.edge_count : 1u, sizeof(*all_edges));
    component = malloc((size_t)n * sizeof(*component));
    rank = malloc((size_t)n * sizeof(*rank));
    comp_members = calloc(n, sizeof(*comp_members));
    comp_edges = calloc(ctx.edge_count ? ctx.edge_count : 1u, sizeof(*comp_edges));
    if (!ctx.sets || !ctx.phi || !members || !all_edges || !component || !rank ||
        !comp_members || !comp_edges) {
        (void)descent_out_of_memory(&ctx);
        goto done;
    }
    for (uint32_t v = 0u; v < ctx.function_count; v++)
        members[v] = true;
    for (uint32_t e = 0u; e < ctx.edge_count; e++)
        all_edges[e] = true;
    uint32_t count = 0u;
    if (!descent_components(&ctx, members, all_edges, component, &count)) {
        (void)descent_out_of_memory(&ctx);
        goto done;
    }
    /* Level 0 ranks the components, callees below callers. */
    for (uint32_t v = 0u; v < ctx.function_count; v++)
        rank[v] = (int64_t)component[v];
    if (!descent_record(&ctx, 0u, members, NULL, rank))
        goto done;
    ok = true;
    for (uint32_t c = 0u; ok && c < count; c++) {
        bool inner = false;
        for (uint32_t v = 0u; v < ctx.function_count; v++)
            comp_members[v] = component[v] == c;
        for (uint32_t e = 0u; e < ctx.edge_count; e++) {
            const DescentEdge *edge = &ctx.edges[e];
            comp_edges[e] = comp_members[edge->f] && comp_members[edge->g];
            inner |= comp_edges[e];
        }
        if (inner)
            ok = descent_level(&ctx, comp_members, comp_edges, 1u);
    }
    if (ok)
        ok = descent_verify(&ctx);

done:
    if (resource_failure && ctx.oom)
        *resource_failure = true;
    free(env.items);
    for (uint32_t e = 0u; e < ctx.edge_count; e++)
        free(ctx.edges[e].env);
    free(ctx.edges);
    for (uint32_t l = 0u; l < ctx.level_count; l++) {
        free(ctx.levels[l].sets);
        free(ctx.levels[l].phi);
    }
    free(ctx.levels);
    free(ctx.functions);
    free(rule_function);
    free(ctx.sets);
    free(ctx.phi);
    free(members);
    free(all_edges);
    free(component);
    free(rank);
    free(comp_members);
    free(comp_edges);
    return ok && !ctx.oom;
}

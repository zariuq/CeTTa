#include "native_fold.h"

#include "eval.h"
#include "fold_algebra.h"
#include "match.h"
#include "petta_semantics.h"
#include "stats.h"

#include <stdlib.h>
#include <string.h>

static bool native_fold_match_space(const Atom *reference, Space *space) {
    if (!reference || !space)
        return false;
    if (reference->kind == ATOM_SYMBOL &&
        reference->sym_id == g_builtin_syms.self)
        return true;
    return reference->kind == ATOM_GROUNDED &&
        reference->ground.gkind == GV_SPACE &&
        reference->ground.ptr == space;
}

/* CETTA_FOLD_ARITH_REFERENCE forces the ordinary fold. */
static bool native_fold_arith_reference_forced(void) {
    const char *setting = getenv("CETTA_FOLD_ARITH_REFERENCE");
    return setting && setting[0] != '\0' && strcmp(setting, "0") != 0;
}

/* PeTTa's builtin binary `+ - * % min max`.  PeTTa arithmetic heads are
 * not user-definable, so the builtin symbol identifies the operation; its
 * `%` floors. */
static bool native_fold_operation_kind(SymbolId head,
                                       CettaFoldExprKind *kind) {
    if (head == g_builtin_syms.op_plus)
        *kind = CETTA_FOLD_EXPR_ADD;
    else if (head == g_builtin_syms.op_minus)
        *kind = CETTA_FOLD_EXPR_SUB;
    else if (head == g_builtin_syms.op_mul)
        *kind = CETTA_FOLD_EXPR_MUL;
    else if (head == g_builtin_syms.op_mod)
        *kind = CETTA_FOLD_EXPR_FREM;
    else if (head == g_builtin_syms.petta_min)
        *kind = CETTA_FOLD_EXPR_MIN;
    else if (head == g_builtin_syms.petta_max)
        *kind = CETTA_FOLD_EXPR_MAX;
    else
        return false;
    return true;
}

/* Integer literals, the item and accumulator variables, and PeTTa's builtin
 * binary `+ - * % min max`.  PeTTa arithmetic heads are not user-definable,
 * so the builtin symbol identifies the operation. */
static bool native_fold_decode(
    const Atom *atom, VarId item, VarId accumulator, uint32_t depth,
    CettaFoldExpr *expr, uint8_t *index_out) {
    if (!atom || depth > CETTA_FOLD_EXPR_CAPACITY)
        return false;
    if (atom->kind == ATOM_VAR) {
        if (var_id_eq(atom->var_id, item))
            return cetta_fold_expr_leaf(
                expr, CETTA_FOLD_EXPR_ITEM, 0, index_out);
        if (var_id_eq(atom->var_id, accumulator))
            return cetta_fold_expr_leaf(
                expr, CETTA_FOLD_EXPR_ACC, 0, index_out);
        return false;
    }
    if (atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_INT)
        return cetta_fold_expr_leaf(
            expr, CETTA_FOLD_EXPR_LITERAL, atom->ground.ival, index_out);
    if (atom->kind != ATOM_EXPR || atom->expr.len != 3u ||
        !atom->expr.elems[0] || atom->expr.elems[0]->kind != ATOM_SYMBOL)
        return false;
    CettaFoldExprKind kind;
    if (!native_fold_operation_kind(atom->expr.elems[0]->sym_id, &kind))
        return false;
    uint8_t left = 0u;
    uint8_t right = 0u;
    return native_fold_decode(atom->expr.elems[1], item, accumulator,
                            depth + 1u, expr, &left) &&
           native_fold_decode(atom->expr.elems[2], item, accumulator,
                            depth + 1u, expr, &right) &&
           cetta_fold_expr_binary(expr, kind, left, right, index_out);
}

static bool native_fold_one_equation(
    Space *space, SymbolId head, Atom **body,
    VarId *accumulator, VarId *item) {
    size_t found = 0u;
    /* The scan reads one space's rows: a module space's imports could add
       an equation for the head. */
    if (!space || !body || !accumulator || !item ||
        space_has_dependencies(space))
        return false;
    CettaCount rows = space_length64(space);
    for (CettaIndex row = 0u; row < rows; row++) {
        Atom *fact = space_get_at64(space, row);
        Atom *lhs = NULL;
        Atom *left = NULL;
        Atom *right = NULL;
        if (!fact || fact->kind != ATOM_EXPR || fact->expr.len != 3u ||
            !atom_is_symbol_id(fact->expr.elems[0], g_builtin_syms.equals))
            continue;
        lhs = fact->expr.elems[1];
        if (!lhs)
            continue;
        /* A variable head may supply another result for this call.  A named
         * equation is counted before checking whether it can be inlined:
         * literal and repeated-variable heads are still alternatives. */
        if (lhs->kind == ATOM_VAR)
            return false;
        if (lhs->kind != ATOM_EXPR || lhs->expr.len == 0u)
            continue;
        if (!lhs->expr.elems[0] || lhs->expr.elems[0]->kind == ATOM_VAR)
            return false;
        if (!atom_is_symbol_id(lhs->expr.elems[0], head))
            continue;
        found++;
        if (found > 1u || lhs->expr.len != 3u)
            return false;
        left = lhs->expr.elems[1];
        right = lhs->expr.elems[2];
        if (!left || !right || left->kind != ATOM_VAR ||
            right->kind != ATOM_VAR || var_id_eq(left->var_id, right->var_id))
            return false;
        *body = fact->expr.elems[2];
        /* foldall applies (function item accumulator). */
        *item = left->var_id;
        *accumulator = right->var_id;
    }
    return found == 1u && *body;
}

/* Whether a named step's declared arrows at arity two all read
 * (-> Number Number Number).  PeTTa checks a declared step's arguments and
 * result on every application; the native fold applies none, so it stands
 * in only for steps those checks cannot refuse: undeclared ones, or ones
 * whose declarations integer items, accumulators and results all meet. */
static bool native_fold_step_declarations_numeric(Space *space, Arena *arena,
                                                  SymbolId head) {
    if (!space_head_has_arrow_signature(space, head, 2u))
        return true;
    Atom *type = atom_var(arena, "t");
    Atom *pattern = atom_expr3(
        arena, atom_symbol_id(arena, g_builtin_syms.colon),
        atom_symbol_id(arena, head), type);
    bool numeric = true;
    for (uint32_t member = 0u;
         numeric && member <= space_dependency_count(space); member++) {
        Space *source = member == 0u
            ? space : space_dependency_at(space, member - 1u);
        BindingSet declared;
        binding_set_init(&declared);
        space_query_conjunction(source, arena, &pattern, 1u, NULL, &declared);
        for (CettaCount i = 0u; numeric && i < declared.len; i++) {
            Atom *arrow = bindings_apply(&declared.items[i], arena, type);
            if (!arrow || arrow->kind != ATOM_EXPR || arrow->expr.len != 4u ||
                !atom_is_symbol_id(arrow->expr.elems[0], g_builtin_syms.arrow))
                continue;
            for (CettaExprIndex k = 1u; numeric && k < 4u; k++)
                numeric = atom_is_symbol(arrow->expr.elems[k], "Number");
        }
        binding_set_free(&declared);
    }
    return numeric;
}

/* A lambda of two places as PeTTa evaluates `(|-> ($item $acc) body)`:
 * a registered callable whose two nested layers share one identity, its
 * body canonical locally-nameless syntax.  Opened, the body reads the item
 * and the accumulator; a lambda capturing variables declines. */
static Atom *native_fold_open_lambda(Arena *arena, Atom *function,
                                     VarId *item, VarId *accumulator) {
    int64_t identity = 0;
    if (!arena || !atom_petta_callable_identity(function, &identity) ||
        atom_has_vars(function))
        return NULL;
    Atom *places[2] = {
        atom_var_with_id(arena, "$__fold_item", fresh_var_id()),
        atom_var_with_id(arena, "$__fold_acc", fresh_var_id()),
    };
    Atom *body = function;
    for (size_t place = 0u; place < 2u; place++) {
        int64_t layer = 0;
        if (!places[place] || !body ||
            !atom_petta_callable_identity(body, &layer) ||
            layer != identity || !petta_semantics_lambda_body(body, NULL))
            return NULL;
        body = cetta_petta_open_callable(arena, body, places[place]);
    }
    if (!body || petta_semantics_lambda_body(body, NULL))
        return NULL;
    *item = places[0]->var_id;
    *accumulator = places[1]->var_id;
    return body;
}

/* `function`, applied as (function item accumulator): a builtin operation,
 * the one pure equation of a named function, or a lambda of two places. */
static bool native_fold_function_decode(
    Space *space, Arena *arena, Atom *function, CettaFoldStep *out) {
    if (!function || !out ||
        (function->kind != ATOM_SYMBOL && function->kind != ATOM_EXPR))
        return false;
    CettaFoldExpr expr = {0};
    uint8_t root = 0u;
    CettaFoldExprKind kind;
    if (function->kind == ATOM_EXPR) {
        VarId param_acc = VAR_ID_NONE;
        VarId param_item = VAR_ID_NONE;
        Atom *body = native_fold_open_lambda(arena, function, &param_item,
                                             &param_acc);
        if (!body ||
            !native_fold_decode(body, param_item, param_acc, 0u, &expr,
                                &root))
            return false;
    } else if (native_fold_operation_kind(function->sym_id, &kind)) {
        uint8_t item = 0u;
        uint8_t accumulator = 0u;
        if (!cetta_fold_expr_leaf(&expr, CETTA_FOLD_EXPR_ITEM, 0, &item) ||
            !cetta_fold_expr_leaf(&expr, CETTA_FOLD_EXPR_ACC, 0,
                                  &accumulator) ||
            !cetta_fold_expr_binary(&expr, kind, item, accumulator, &root))
            return false;
    } else {
        Atom *body = NULL;
        VarId param_acc = VAR_ID_NONE;
        VarId param_item = VAR_ID_NONE;
        if (!native_fold_one_equation(
                space, function->sym_id, &body, &param_acc, &param_item) ||
            !native_fold_decode(body, param_item, param_acc, 0u, &expr,
                                &root))
            return false;
    }
    return cetta_fold_step_classify(&expr, root, out);
}

/* The decoded step, admitted only where the ordinary fold's per-step checks
 * cannot refuse it: a builtin operation's grounded arrows are numeric; a
 * named step's declarations must be (see above). */
static bool native_fold_step_admitted(Space *space, Arena *arena,
                                      Atom *function, CettaFoldStep *out) {
    CettaFoldExprKind kind;
    if (!native_fold_function_decode(space, arena, function, out))
        return false;
    /* A lambda declares nothing. */
    return function->kind == ATOM_EXPR ||
           native_fold_operation_kind(function->sym_id, &kind) ||
           native_fold_step_declarations_numeric(space, arena,
                                                 function->sym_id);
}

/* Whether every application of the step has a value: only a floor
 * remainder by what may be zero has none.  An accumulator leaving int64
 * continues exactly. */
static bool native_fold_step_total(const CettaFoldStep *step) {
#if CETTA_BUILD_WITH_GMP
    for (uint8_t node = 0u; node < step->source.len; node++) {
        const CettaFoldExprNode *at = &step->source.nodes[node];
        if (at->kind != CETTA_FOLD_EXPR_FREM)
            continue;
        const CettaFoldExprNode *divisor = &step->source.nodes[at->right];
        if (divisor->kind != CETTA_FOLD_EXPR_LITERAL || divisor->literal == 0)
            return false;
    }
    return true;
#else
    (void)step;
    return false;
#endif
}

static bool native_fold_column_of(
    Atom *const *patterns, size_t pattern_count, VarId id,
    size_t *pattern_index, size_t *column) {
    bool found = false;
    if (!patterns || !pattern_index || !column)
        return false;
    for (size_t pattern = 0u; pattern < pattern_count; pattern++) {
        Atom *shape = patterns[pattern];
        if (!shape || shape->kind != ATOM_EXPR)
            return false;
        for (CettaExprIndex index = 0u; index < shape->expr.len; index++) {
            Atom *item = shape->expr.elems[index];
            if (!item || item->kind != ATOM_VAR || !var_id_eq(item->var_id, id))
                continue;
            if (!found) {
                *pattern_index = pattern;
                *column = (size_t)index;
                found = true;
            }
        }
    }
    return found;
}

static bool native_fold_publish(Arena *arena, Atom **answer, int64_t value) {
    *answer = atom_int(arena, value);
    return *answer != NULL;
}

typedef struct {
    const CettaFoldStep *step;
    int64_t accumulator;
#if CETTA_BUILD_WITH_GMP
    CettaFoldBig big;
#endif
    bool ok;
} NativeFoldScan;

/* One item.  An accumulator leaving int64 continues exactly on a bigint;
 * a zero divisor or an unsupported form stops the scan so the ordinary
 * fold can report it. */
static bool native_fold_scan_step(NativeFoldScan *scan, int64_t value) {
    CettaFoldStatus status = CETTA_FOLD_UNSUPPORTED;
#if CETTA_BUILD_WITH_GMP
    if (scan->big.active) {
        status = cetta_fold_step_apply_big(scan->step, value, &scan->big);
    } else {
        status = cetta_fold_step_apply(scan->step, value, scan->accumulator,
                                       &scan->accumulator);
        if (status == CETTA_FOLD_OVERFLOW) {
            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_NATIVE_FOLD_BIGINT);
            cetta_fold_big_start(&scan->big, scan->accumulator);
            status = cetta_fold_step_apply_big(scan->step, value,
                                               &scan->big);
        }
    }
#else
    status = cetta_fold_step_apply(scan->step, value, scan->accumulator,
                                   &scan->accumulator);
#endif
    return status == CETTA_FOLD_OK;
}

static bool native_fold_scan_visit(int64_t value, void *raw) {
    NativeFoldScan *scan = raw;
    if (!scan->ok || !native_fold_scan_step(scan, value)) {
        scan->ok = false;
        return false;
    }
    return true;
}

static void native_fold_scan_free(NativeFoldScan *scan) {
#if CETTA_BUILD_WITH_GMP
    cetta_fold_big_clear(&scan->big);
#else
    (void)scan;
#endif
}

static bool native_fold_publish_scan(
    Arena *arena, Atom **answer, NativeFoldScan *scan) {
#if CETTA_BUILD_WITH_GMP
    if (scan->big.active) {
        *answer = atom_bigint_from_mpz(arena, scan->big.acc);
        return *answer != NULL;
    }
#endif
    return native_fold_publish(arena, answer, scan->accumulator);
}

/* A commutative fold over the occurrence bag of an integer column, from
 * the bag's count, sum and product. */
static bool native_fold_from_moments(
    const CettaFoldStep *step, int64_t init, uint64_t count,
    __int128 sum, int64_t product, bool product_ok, int64_t *out) {
    if (step->translation && step->offset_linear) {
        /* init + slope * sum + intercept * count */
        __int128 slope_part = 0;
        __int128 intercept_part = 0;
        if (__builtin_mul_overflow((__int128)step->offset_slope, sum,
                                   &slope_part) ||
            __builtin_mul_overflow((__int128)step->offset_intercept,
                                   (__int128)count, &intercept_part))
            return false;
        __int128 total = 0;
        if (__builtin_add_overflow(slope_part, intercept_part, &total) ||
            __builtin_add_overflow(total, (__int128)init, &total) ||
            total > (__int128)INT64_MAX || total < (__int128)INT64_MIN)
            return false;
        *out = (int64_t)total;
        return true;
    }
    if (!step->scaling || !step->scale_linear)
        return false;
    if (init == 0) {
        *out = 0;
        return true;
    }
    if (step->scale_slope == 1 && step->scale_intercept == 0) {
        __int128 total = (__int128)init * product;
        if (!product_ok || total > (__int128)INT64_MAX ||
            total < (__int128)INT64_MIN)
            return false;
        *out = (int64_t)total;
        return true;
    }
    if (step->scale_slope == 0)
        return cetta_fold_step_repeat(step, 0, count, init, out) ==
            CETTA_FOLD_OK;
    return false;
}

static CettaNativeFoldRoute native_fold_route(
    Space *space, Arena *arena, Atom *function, Atom *generator, Atom *init,
    Atom **answer) {
    /* A typed profile checks each step application; the native fold has no
     * such evidence, so it runs only where steps are untyped. */
    if (eval_petta_steps_are_typed && eval_petta_steps_are_typed())
        return CETTA_NATIVE_FOLD_NOT_ADMITTED;
    if (!space || !arena || !function || !generator || !init || !answer ||
        init->kind != ATOM_GROUNDED || init->ground.gkind != GV_INT ||
        generator->kind != ATOM_EXPR || generator->expr.len != 4u ||
        !atom_is_symbol_id(generator->expr.elems[0], g_builtin_syms.match) ||
        !native_fold_match_space(generator->expr.elems[1], space))
        return CETTA_NATIVE_FOLD_NOT_ADMITTED;
    Atom *pattern = generator->expr.elems[2];
    Atom *templ = generator->expr.elems[3];
    CettaFoldStep fold;
    if (!pattern || !templ ||
        !native_fold_step_admitted(space, arena, function, &fold))
        return CETTA_NATIVE_FOLD_NOT_ADMITTED;
    CettaObservationContract contract = cetta_fold_step_contract(&fold);

    Atom *single_pattern = pattern;
    Atom **patterns = &single_pattern;
    size_t pattern_count = 1u;
    if (pattern->kind == ATOM_EXPR && pattern->expr.len >= 2u &&
        atom_is_symbol_id(pattern->expr.elems[0], g_builtin_syms.comma)) {
        pattern_count = (size_t)pattern->expr.len - 1u;
        patterns = pattern->expr.elems + 1u;
    }

    int64_t initial = init->ground.ival;
    int64_t reduced = 0;
    bool item_constant =
        templ->kind == ATOM_GROUNDED && templ->ground.gkind == GV_INT;
    if (!fold.uses_item || item_constant) {
        /* The chain still evaluates the template.  A ground integer is the
         * only template whose evaluation is the identity, so an update that
         * ignores the item cannot skip any other template. */
        uint64_t matches = 0u;
        /* A run needs only the number of occurrences, which no order
         * affects: use the admitted relational count realization. */
        bool counted = item_constant && pattern_count >= 3u &&
            space_native_relational_count(
                space, arena, patterns, pattern_count, NULL, &matches);
        /* One flat pattern is counted as a collection of it is. */
        CettaIndex examined = 0u;
        if (item_constant && !counted && pattern_count == 1u)
            counted = space_match_count_flat_linear64(
                space, arena, patterns[0], &matches, &examined);
        if (!item_constant ||
            (!counted &&
             !space_native_flat_conjunction_count(
                 space, patterns, pattern_count, &matches)))
            return CETTA_NATIVE_FOLD_DECLINED;
        CettaFoldStatus status = cetta_fold_step_repeat(
            &fold, templ->ground.ival, matches, initial, &reduced);
        if (status == CETTA_FOLD_OK)
            return native_fold_publish(arena, answer, reduced)
                ? CETTA_NATIVE_FOLD_RUN : CETTA_NATIVE_FOLD_DECLINED;
        if (status != CETTA_FOLD_OVERFLOW)
            return CETTA_NATIVE_FOLD_DECLINED;
        /* The closed form left int64: take the steps one by one, exactly. */
        NativeFoldScan scan = {.step = &fold, .accumulator = initial, .ok = true};
        for (uint64_t index = 0u; scan.ok && index < matches; index++)
            scan.ok = native_fold_scan_step(&scan, templ->ground.ival);
        bool published =
            scan.ok && native_fold_publish_scan(arena, answer, &scan);
        native_fold_scan_free(&scan);
        return published ? CETTA_NATIVE_FOLD_RUN : CETTA_NATIVE_FOLD_DECLINED;
    }

    if (templ->kind != ATOM_VAR)
        return CETTA_NATIVE_FOLD_DECLINED;
    if (pattern_count == 1u) {
        NativeFoldScan scan = {.step = &fold, .accumulator = initial, .ok = true};
        bool published =
            space_native_pattern_each_int(
                space, patterns[0], templ->var_id, native_fold_scan_visit,
                &scan) &&
            scan.ok && native_fold_publish_scan(arena, answer, &scan);
        native_fold_scan_free(&scan);
        return published ? CETTA_NATIVE_FOLD_ORDERED_SCAN : CETTA_NATIVE_FOLD_DECLINED;
    }
    size_t column_pattern = 0u;
    size_t column = 0u;
    if (!native_fold_column_of(patterns, pattern_count, templ->var_id,
                             &column_pattern, &column))
        return CETTA_NATIVE_FOLD_DECLINED;
    /* A join's rows arrive without the authored order: only a consumer that
     * observes the occurrence bag may read them. */
    if (contract.algebra != CETTA_OBSERVATION_ALGEBRA_COMMUTATIVE_FOLD)
        return CETTA_NATIVE_FOLD_DECLINED;
    __int128 chain_count = 0;
    __int128 chain_sum = 0;
    if (fold.translation && fold.offset_linear &&
        space_native_flat_chain_aggregate(
            space, patterns, pattern_count, true, column_pattern, column,
            &chain_count, &chain_sum)) {
        if (chain_count < 0 || chain_count > (__int128)UINT64_MAX ||
            !native_fold_from_moments(&fold, initial, (uint64_t)chain_count,
                                    chain_sum, 1, false, &reduced))
            return CETTA_NATIVE_FOLD_DECLINED;
        return native_fold_publish(arena, answer, reduced)
            ? CETTA_NATIVE_FOLD_MOMENTS : CETTA_NATIVE_FOLD_DECLINED;
    }
    uint64_t matches = 0u;
    __int128 column_sum = 0;
    int64_t column_product = 1;
    bool product_ok = true;
    if (!space_native_flat_conjunction_int_moments(
            space, patterns, pattern_count, column_pattern, column,
            &matches, &column_sum, &column_product, &product_ok) ||
        !native_fold_from_moments(&fold, initial, matches, column_sum,
                                column_product, product_ok, &reduced))
        return CETTA_NATIVE_FOLD_DECLINED;
    return native_fold_publish(arena, answer, reduced)
        ? CETTA_NATIVE_FOLD_MOMENTS : CETTA_NATIVE_FOLD_DECLINED;
}

bool cetta_native_fold_counts_answers(Space *space, Arena *arena,
                                      Atom *function, Atom *initial) {
    CettaFoldStep fold;
    return !native_fold_arith_reference_forced() &&
           !(eval_petta_steps_are_typed && eval_petta_steps_are_typed()) &&
           space && arena && function && initial &&
           initial->kind == ATOM_GROUNDED && initial->ground.gkind == GV_INT &&
           native_fold_step_admitted(space, arena, function, &fold) &&
           !fold.uses_item && native_fold_step_total(&fold);
}

bool cetta_native_fold_repeat(Space *space, Arena *arena, Atom *function,
                              Atom *initial, uint64_t count, Atom **answer) {
    CettaFoldStep fold;
    if (!cetta_native_fold_counts_answers(space, arena, function, initial) ||
        !native_fold_step_admitted(space, arena, function, &fold) || !answer)
        return false;
    int64_t reduced = 0;
    bool published = false;
    if (cetta_fold_step_repeat(&fold, 0, count, initial->ground.ival,
                               &reduced) == CETTA_FOLD_OK) {
        published = native_fold_publish(arena, answer, reduced);
    } else {
        /* The closed form left int64: take the steps one by one, exactly. */
        NativeFoldScan scan = {
            .step = &fold, .accumulator = initial->ground.ival, .ok = true};
        for (uint64_t index = 0u; scan.ok && index < count; index++)
            scan.ok = native_fold_scan_step(&scan, 0);
        published = scan.ok && native_fold_publish_scan(arena, answer, &scan);
        native_fold_scan_free(&scan);
    }
    if (published)
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_NATIVE_FOLD_RUN);
    return published;
}

CettaNativeFoldRoute cetta_native_foldall(
    Space *space, Arena *arena, Atom *function, Atom *generator, Atom *init,
    Atom **answer) {
    if (native_fold_arith_reference_forced())
        return CETTA_NATIVE_FOLD_NOT_ADMITTED;
    CettaNativeFoldRoute route = native_fold_route(
        space, arena, function, generator, init, answer);
    switch (route) {
    case CETTA_NATIVE_FOLD_NOT_ADMITTED:
        break;
    case CETTA_NATIVE_FOLD_DECLINED:
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_NATIVE_FOLD_DECLINED);
        break;
    case CETTA_NATIVE_FOLD_ORDERED_SCAN:
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_NATIVE_FOLD_ORDERED_SCAN);
        break;
    case CETTA_NATIVE_FOLD_RUN:
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_NATIVE_FOLD_RUN);
        break;
    case CETTA_NATIVE_FOLD_MOMENTS:
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_NATIVE_FOLD_MOMENTS);
        break;
    }
    return route;
}

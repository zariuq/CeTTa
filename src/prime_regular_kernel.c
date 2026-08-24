#include "prime_regular_kernel.h"
#include "prime_level.h"
#include "stats.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

typedef struct {
    CettaPrimeRegularKernelStatus status;
    Atom *type;
    bool type_is_sort;
    const char *reason;
} PrimeRegularKernelInfer;

typedef struct {
    CettaPrimeRegularKernelStatus status;
    Atom *term;
    const char *reason;
} PrimeRegularKernelNormal;

static bool regular_symbol(Atom *atom, const char *name) {
    return atom && atom_is_symbol(atom, name);
}

static bool regular_expr(Atom *atom, const char *head, CettaExprIndex len) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len == len &&
           regular_symbol(atom->expr.elems[0], head);
}

/* Internal occurrence of a global declaration.  Universe arguments are
 * explicit here even though the authored syntax elides them.  This keeps a
 * constant's global identity separate from each fresh polymorphic use. */
static bool regular_decl_const_shape(
    Atom *term, Atom **name_out, size_t *level_count_out) {
    if (!term || term->kind != ATOM_EXPR || term->expr.len < 2u ||
        !regular_symbol(term->expr.elems[0], "DeclConst") ||
        !term->expr.elems[1] ||
        term->expr.elems[1]->kind != ATOM_SYMBOL) {
        return false;
    }
    if (name_out) *name_out = term->expr.elems[1];
    if (level_count_out)
        *level_count_out = (size_t)term->expr.len - 2u;
    return true;
}

static Atom *regular_expr4(
    Arena *arena, Atom *head, Atom *first, Atom *second, Atom *third) {
    Atom *elems[4] = {head, first, second, third};
    return atom_expr(arena, elems, 4u);
}

static bool regular_spend(CettaPrimeRegularKernelBudget *budget) {
    if (!budget || !budget->limited) return true;
    if (budget->remaining == 0u) return false;
    budget->remaining--;
    if (budget->spent != UINT64_MAX) budget->spent++;
    return true;
}

typedef struct {
    const uint64_t *parameters;
    Atom **assignments;
    size_t count;
} PrimeRegularLevelInstantiation;

static bool regular_level_instantiation_find(
    const PrimeRegularLevelInstantiation *instantiation,
    uint64_t parameter, size_t *index_out) {
    if (!instantiation) return false;
    for (size_t index = 0u; index < instantiation->count; index++) {
        if (instantiation->parameters[index] != parameter) continue;
        if (index_out) *index_out = index;
        return true;
    }
    return false;
}

static bool regular_level_parameter_syntax(
    Atom *level, uint64_t *parameter_out) {
    if (!regular_expr(level, "LevelParam", 2u) ||
        !level->expr.elems[1] ||
        level->expr.elems[1]->kind != ATOM_GROUNDED ||
        level->expr.elems[1]->ground.gkind != GV_INT ||
        level->expr.elems[1]->ground.ival < 0) {
        return false;
    }
    if (parameter_out)
        *parameter_out = (uint64_t)level->expr.elems[1]->ground.ival;
    return true;
}

static Atom *regular_level_apply_instantiation(
    Arena *arena, Atom *level,
    const PrimeRegularLevelInstantiation *instantiation,
    CettaPrimeRegularKernelBudget *budget, bool *complete) {
    if (!complete || !*complete || !regular_spend(budget) || !level) {
        if (complete) *complete = false;
        return NULL;
    }
    uint64_t parameter = 0u;
    size_t index = 0u;
    if (regular_level_parameter_syntax(level, &parameter) &&
        regular_level_instantiation_find(
            instantiation, parameter, &index) &&
        instantiation->assignments[index]) {
        Atom *replacement = instantiation->assignments[index];
        if (atom_eq(replacement, level)) {
            *complete = false;
            return NULL;
        }
        return regular_level_apply_instantiation(
            arena, replacement, instantiation, budget, complete);
    }
    if (regular_expr(level, "LevelConst", 2u) ||
        regular_expr(level, "LevelParam", 2u)) {
        return level;
    }
    if (regular_expr(level, "LevelSucc", 2u)) {
        Atom *inner = regular_level_apply_instantiation(
            arena, level->expr.elems[1], instantiation, budget, complete);
        return inner ? atom_expr2(arena, level->expr.elems[0], inner) : NULL;
    }
    if (regular_expr(level, "LevelMax", 3u)) {
        Atom *left = regular_level_apply_instantiation(
            arena, level->expr.elems[1], instantiation, budget, complete);
        Atom *right = left ? regular_level_apply_instantiation(
            arena, level->expr.elems[2], instantiation, budget, complete)
            : NULL;
        return left && right
            ? atom_expr3(arena, level->expr.elems[0], left, right)
            : NULL;
    }
    *complete = false;
    return NULL;
}

static bool regular_level_mentions_instantiation_parameter(
    Atom *level, const PrimeRegularLevelInstantiation *instantiation) {
    uint64_t parameter = 0u;
    if (regular_level_parameter_syntax(level, &parameter))
        return regular_level_instantiation_find(
            instantiation, parameter, NULL);
    if (regular_expr(level, "LevelSucc", 2u))
        return regular_level_mentions_instantiation_parameter(
            level->expr.elems[1], instantiation);
    if (regular_expr(level, "LevelMax", 3u))
        return regular_level_mentions_instantiation_parameter(
                   level->expr.elems[1], instantiation) ||
               regular_level_mentions_instantiation_parameter(
                   level->expr.elems[2], instantiation);
    return false;
}

static bool regular_level_raise_instantiation_parameters(
    Arena *arena, Atom *level, Atom *lower_bound,
    PrimeRegularLevelInstantiation *instantiation,
    CettaPrimeRegularKernelBudget *budget) {
    if (!regular_spend(budget) || !level || !lower_bound ||
        !instantiation)
        return false;
    uint64_t parameter = 0u;
    size_t index = 0u;
    if (regular_level_parameter_syntax(level, &parameter)) {
        if (!regular_level_instantiation_find(
                instantiation, parameter, &index))
            return true;
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_DECLARATION_LEVEL_CONSTRAINT);
        Atom *prior = instantiation->assignments[index];
        instantiation->assignments[index] = prior
            ? atom_expr3(
                  arena, atom_symbol(arena, "LevelMax"),
                  prior, lower_bound)
            : lower_bound;
        return instantiation->assignments[index] != NULL;
    }
    if (regular_expr(level, "LevelConst", 2u)) return true;
    if (regular_expr(level, "LevelSucc", 2u))
        return regular_level_raise_instantiation_parameters(
            arena, level->expr.elems[1], lower_bound,
            instantiation, budget);
    if (regular_expr(level, "LevelMax", 3u))
        return regular_level_raise_instantiation_parameters(
                   arena, level->expr.elems[1], lower_bound,
                   instantiation, budget) &&
               regular_level_raise_instantiation_parameters(
                   arena, level->expr.elems[2], lower_bound,
                   instantiation, budget);
    return false;
}

static bool regular_level_instantiation_default_zero(
    Arena *arena, PrimeRegularLevelInstantiation *instantiation) {
    if (!arena || !instantiation) return false;
    Atom *zero = NULL;
    for (size_t index = 0u; index < instantiation->count; index++) {
        if (instantiation->assignments[index]) continue;
        if (!zero)
            zero = atom_expr2(
                arena, atom_symbol(arena, "LevelConst"),
                atom_int(arena, 0));
        instantiation->assignments[index] = zero;
    }
    return true;
}

static bool regular_level_instantiation_init(
    Arena *arena, const uint64_t *parameters, size_t parameter_count,
    PrimeRegularLevelInstantiation *instantiation) {
    if (!arena || !instantiation ||
        (parameter_count != 0u && !parameters) ||
        parameter_count > SIZE_MAX / sizeof(Atom *))
        return false;
    for (size_t left = 0u; left < parameter_count; left++) {
        if (parameters[left] > (uint64_t)INT64_MAX) return false;
        for (size_t right = 0u; right < left; right++)
            if (parameters[left] == parameters[right]) return false;
    }
    Atom **assignments = parameter_count == 0u
        ? NULL : arena_alloc(arena, parameter_count * sizeof(*assignments));
    if (parameter_count != 0u)
        memset(assignments, 0, parameter_count * sizeof(*assignments));
    *instantiation = (PrimeRegularLevelInstantiation){
        .parameters = parameters,
        .assignments = assignments,
        .count = parameter_count,
    };
    return true;
}

static Atom *regular_term_apply_level_instantiation(
    Arena *arena, Atom *term,
    const PrimeRegularLevelInstantiation *instantiation,
    CettaPrimeRegularKernelBudget *budget, bool *complete) {
    if (!complete || !*complete || !regular_spend(budget) || !term) {
        if (complete) *complete = false;
        return NULL;
    }
    uint64_t parameter = 0u;
    size_t parameter_index = 0u;
    if (regular_level_parameter_syntax(term, &parameter) &&
        regular_level_instantiation_find(
            instantiation, parameter, &parameter_index)) {
        Atom *replacement = instantiation->assignments[parameter_index];
        if (!replacement) {
            *complete = false;
            return NULL;
        }
        if (atom_eq(replacement, term)) {
            *complete = false;
            return NULL;
        }
        return regular_term_apply_level_instantiation(
            arena, replacement, instantiation, budget, complete);
    }
    if (term->kind != ATOM_EXPR) return term;
    if (term->expr.len > SIZE_MAX / sizeof(Atom *)) {
        *complete = false;
        return NULL;
    }
    Atom **items = arena_alloc(
        arena, sizeof(*items) * (size_t)term->expr.len);
    for (CettaExprIndex index = 0u; index < term->expr.len; index++) {
        items[index] = regular_term_apply_level_instantiation(
            arena, term->expr.elems[index], instantiation,
            budget, complete);
        if (!items[index]) return NULL;
    }
    return atom_expr(arena, items, term->expr.len);
}

void cetta_prime_regular_kernel_budget_init(
    CettaPrimeRegularKernelBudget *budget, bool limited, uint64_t steps) {
    if (!budget) return;
    budget->limited = limited;
    budget->remaining = limited ? steps : 0u;
    budget->spent = 0u;
}

/* `Sort` and `Level*` are the declaration-free internal tower wire.  They are
 * not CeTTa source spellings: the authored language elaborator owns that
 * interface.  U1 remains the canonical spelling of the embedded legacy
 * marker, namely sort zero. */
static bool regular_level_natural(Atom *atom, uint64_t *value_out) {
    if (!atom || atom->kind != ATOM_GROUNDED ||
        atom->ground.gkind != GV_INT || atom->ground.ival < 0) {
        return false;
    }
    if (value_out) *value_out = (uint64_t)atom->ground.ival;
    return true;
}

static bool regular_level_syntax_check(
    Atom *level, CettaPrimeRegularKernelBudget *budget, bool *complete) {
    if (!complete || !*complete) return false;
    if (!regular_spend(budget)) {
        *complete = false;
        return false;
    }
    if (!level) return false;
    uint64_t ignored = 0u;
    if ((regular_expr(level, "LevelConst", 2u) ||
         regular_expr(level, "LevelParam", 2u)) &&
        regular_level_natural(level->expr.elems[1], &ignored)) {
        return true;
    }
    if (regular_expr(level, "LevelSucc", 2u))
        return regular_level_syntax_check(
            level->expr.elems[1], budget, complete);
    if (regular_expr(level, "LevelMax", 3u))
        return regular_level_syntax_check(
                   level->expr.elems[1], budget, complete) &&
               regular_level_syntax_check(
                   level->expr.elems[2], budget, complete);
    return false;
}

/* A closed admission may contain computed constants, successors, and maxima,
 * but no schematic level parameters.  Parameterized declarations are a
 * distinct fragment: they must be freshly instantiated before they enter a
 * closed judgment rather than borrowing the closed fragment's authority. */
static bool regular_closed_level_syntax_check(
    Atom *level, CettaPrimeRegularKernelBudget *budget, bool *complete) {
    if (!complete || !*complete) return false;
    if (!regular_spend(budget)) {
        *complete = false;
        return false;
    }
    if (!level) return false;
    uint64_t ignored = 0u;
    if (regular_expr(level, "LevelConst", 2u) &&
        regular_level_natural(level->expr.elems[1], &ignored)) {
        return true;
    }
    if (regular_expr(level, "LevelSucc", 2u))
        return regular_closed_level_syntax_check(
            level->expr.elems[1], budget, complete);
    if (regular_expr(level, "LevelMax", 3u))
        return regular_closed_level_syntax_check(
                   level->expr.elems[1], budget, complete) &&
               regular_closed_level_syntax_check(
                   level->expr.elems[2], budget, complete);
    return false;
}

static bool regular_sort_syntax(
    Atom *term, CettaPrimeRegularKernelBudget *budget, bool *complete) {
    if (regular_symbol(term, "U1")) return true;
    if (!regular_expr(term, "Sort", 2u)) return false;
    return regular_level_syntax_check(term->expr.elems[1], budget, complete);
}

static CettaPrimeRegularKernelStatus regular_level_status(
    CettaPrimeLevelStatusV1 status, const char **reason_out) {
    switch (status) {
    case CETTA_PRIME_LEVEL_OK_V1:
        return CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED;
    case CETTA_PRIME_LEVEL_REPRESENTATION_LIMIT_V1:
        if (reason_out) *reason_out = "level-representation-limit";
        return CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED;
    case CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1:
        if (reason_out) *reason_out = "invalid-level-expression";
        return CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS;
    }
    if (reason_out) *reason_out = "unknown-level-status";
    return CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE;
}

static CettaPrimeRegularKernelStatus regular_decode_level(
    Arena *arena, Atom *syntax, CettaPrimeRegularKernelBudget *budget,
    const CettaPrimeLevelV1 **level_out, const char **reason_out) {
    if (level_out) *level_out = NULL;
    if (!arena || !syntax || !budget || !level_out) {
        if (reason_out) *reason_out = "invalid-level-decoder-input";
        return CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE;
    }
    if (!regular_spend(budget)) {
        if (reason_out) *reason_out = "level-normalization-budget";
        return CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED;
    }
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PRIME_LEVEL_NORMALIZATION_STEP);
    uint64_t natural = 0u;
    CettaPrimeLevelStatusV1 level_status;
    if (regular_expr(syntax, "LevelConst", 2u) &&
        regular_level_natural(syntax->expr.elems[1], &natural)) {
        level_status = cetta_prime_level_constant_v1(
            arena, natural, level_out);
        return regular_level_status(level_status, reason_out);
    }
    if (regular_expr(syntax, "LevelParam", 2u) &&
        regular_level_natural(syntax->expr.elems[1], &natural)) {
        level_status = cetta_prime_level_parameter_v1(
            arena, natural, level_out);
        return regular_level_status(level_status, reason_out);
    }
    if (regular_expr(syntax, "LevelSucc", 2u)) {
        const CettaPrimeLevelV1 *inner = NULL;
        CettaPrimeRegularKernelStatus status = regular_decode_level(
            arena, syntax->expr.elems[1], budget, &inner, reason_out);
        if (status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) return status;
        level_status = cetta_prime_level_successor_v1(
            arena, inner, level_out);
        return regular_level_status(level_status, reason_out);
    }
    if (regular_expr(syntax, "LevelMax", 3u)) {
        const CettaPrimeLevelV1 *left = NULL;
        const CettaPrimeLevelV1 *right = NULL;
        CettaPrimeRegularKernelStatus status = regular_decode_level(
            arena, syntax->expr.elems[1], budget, &left, reason_out);
        if (status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) return status;
        status = regular_decode_level(
            arena, syntax->expr.elems[2], budget, &right, reason_out);
        if (status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) return status;
        level_status = cetta_prime_level_maximum_v1(
            arena, left, right, level_out);
        return regular_level_status(level_status, reason_out);
    }
    if (reason_out) *reason_out = "invalid-level-expression";
    return CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS;
}

static Atom *regular_level_zero_syntax(Arena *arena) {
    return atom_expr2(
        arena, atom_symbol(arena, "LevelConst"), atom_int(arena, 0));
}

static Atom *regular_sort_level_syntax(Arena *arena, Atom *sort) {
    if (regular_symbol(sort, "U1")) return regular_level_zero_syntax(arena);
    return regular_expr(sort, "Sort", 2u) ? sort->expr.elems[1] : NULL;
}

static Atom *regular_sort_successor(Arena *arena, Atom *sort) {
    Atom *level = regular_sort_level_syntax(arena, sort);
    return level ? atom_expr2(
        arena, atom_symbol(arena, "Sort"),
        atom_expr2(arena, atom_symbol(arena, "LevelSucc"), level)) : NULL;
}

static Atom *regular_sort_maximum(Arena *arena, Atom *left, Atom *right) {
    if (regular_symbol(left, "U1") && regular_symbol(right, "U1"))
        return atom_symbol(arena, "U1");
    Atom *left_level = regular_sort_level_syntax(arena, left);
    Atom *right_level = regular_sort_level_syntax(arena, right);
    return left_level && right_level
        ? atom_expr2(
              arena, atom_symbol(arena, "Sort"),
              atom_expr3(
                  arena, atom_symbol(arena, "LevelMax"),
                  left_level, right_level))
        : NULL;
}

static CettaPrimeRegularKernelStatus regular_decode_sort(
    Arena *arena, Atom *sort, CettaPrimeRegularKernelBudget *budget,
    const CettaPrimeLevelV1 **level_out, const char **reason_out) {
    if (regular_symbol(sort, "U1")) {
        CettaPrimeLevelStatusV1 status = cetta_prime_level_constant_v1(
            arena, 0u, level_out);
        return regular_level_status(status, reason_out);
    }
    if (!regular_expr(sort, "Sort", 2u)) {
        if (reason_out) *reason_out = "expected-universe-sort";
        return CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS;
    }
    return regular_decode_level(
        arena, sort->expr.elems[1], budget, level_out, reason_out);
}

bool cetta_prime_regular_kernel_unwrap_scoped(
    Atom *scoped, Atom **context_out, Atom **term_out) {
    if (!regular_expr(scoped, "PrimeScoped", 3u)) return false;
    if (context_out) *context_out = scoped->expr.elems[1];
    if (term_out) *term_out = scoped->expr.elems[2];
    return true;
}

static CettaPrimeRegularKernelResult regular_result(
    CettaPrimeRegularKernelStatus status, Atom *type, const char *reason) {
    return (CettaPrimeRegularKernelResult){
        .status = status,
        .type = type,
        .reason = reason,
    };
}

static PrimeRegularKernelInfer regular_infer_result(
    CettaPrimeRegularKernelStatus status, Atom *type, bool type_is_sort,
    const char *reason) {
    return (PrimeRegularKernelInfer){
        .status = status,
        .type = type,
        .type_is_sort = type_is_sort,
        .reason = reason,
    };
}

static PrimeRegularKernelNormal regular_normal_result(
    CettaPrimeRegularKernelStatus status, Atom *term, const char *reason) {
    return (PrimeRegularKernelNormal){
        .status = status,
        .term = term,
        .reason = reason,
    };
}

static bool regular_index(Atom *term, uint64_t *index_out) {
    if (!regular_expr(term, "idx", 2u)) return false;
    Atom *value = term->expr.elems[1];
    if (!value || value->kind != ATOM_GROUNDED ||
        value->ground.gkind != GV_INT || value->ground.ival < 0) {
        return false;
    }
    if (index_out) *index_out = (uint64_t)value->ground.ival;
    return true;
}

static Atom *regular_make_index(Arena *arena, uint64_t index) {
    if (!arena || index > (uint64_t)INT64_MAX) return NULL;
    return atom_expr2(
        arena, atom_symbol(arena, "idx"), atom_int(arena, (int64_t)index));
}

static bool regular_term_shape(Atom *term) {
    if (!term) return false;
    if (term->kind == ATOM_SYMBOL)
        return regular_symbol(term, "U0") || regular_symbol(term, "U1");
    if (term->kind != ATOM_EXPR) return false;
    if (regular_expr(term, "Sort", 2u)) return true;
    uint64_t ignored = 0u;
    if (term->expr.len == 2u)
        return regular_index(term, &ignored) || regular_expr(term, "Fst", 2u) ||
               regular_expr(term, "Snd", 2u) || regular_expr(term, "Refl", 2u);
    if (term->expr.len == 3u)
        return regular_expr(term, "Pi", 3u) ||
               regular_expr(term, "Sigma", 3u) ||
               regular_expr(term, "Lam", 3u) ||
               regular_expr(term, "App", 3u) ||
               regular_expr(term, "Pair", 3u);
    return regular_expr(term, "Id", 4u);
}

/* The proved Pattern boundary uses the intrinsic, unannotated `(Lam body)`
 * constructor.  The admitted Prime term representation above additionally supports its
 * annotated `(Lam domain body)` extension.  Keep the admitted root predicate
 * narrow while allowing the intrinsic checker to consume the proved form. */
static bool regular_intrinsic_term_shape(Atom *term) {
    return regular_term_shape(term) || regular_expr(term, "Lam", 2u) ||
           regular_decl_const_shape(term, NULL, NULL);
}

bool cetta_prime_regular_kernel_term_maybe_syntax(Atom *term) {
    return regular_term_shape(term);
}

bool cetta_prime_regular_kernel_intrinsic_term_maybe_syntax(Atom *term) {
    return regular_intrinsic_term_shape(term);
}

bool cetta_prime_regular_kernel_term_is_universe_sort_v1(Atom *term) {
    CettaPrimeRegularKernelBudget budget;
    cetta_prime_regular_kernel_budget_init(&budget, false, 0u);
    bool complete = true;
    return regular_sort_syntax(term, &budget, &complete) && complete;
}

Atom *cetta_prime_regular_kernel_quote_closed_universe_sort_v1(
    Arena *arena, Atom *term) {
    if (!arena || !regular_expr(term, "Sort", 2u)) return NULL;
    CettaPrimeRegularKernelBudget budget;
    cetta_prime_regular_kernel_budget_init(&budget, false, 0u);
    const CettaPrimeLevelV1 *level = NULL;
    const char *reason = NULL;
    if (regular_decode_level(
            arena, term->expr.elems[1], &budget, &level, &reason) !=
            CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
        return NULL;
    CettaPrimeLevelViewV1 view = {0};
    if (!cetta_prime_level_view_v1(level, &view) ||
        view.parameter_count != 0u || view.constant > (uint64_t)INT64_MAX)
        return NULL;
    return atom_expr2(
        arena, atom_symbol(arena, "u"),
        atom_int(arena, (int64_t)view.constant));
}

static bool regular_decl_const_levels_check(
    Atom *term, bool closed,
    CettaPrimeRegularKernelBudget *budget, bool *complete) {
    if (!regular_decl_const_shape(term, NULL, NULL)) return false;
    for (CettaExprIndex index = 2u; index < term->expr.len; index++) {
        bool valid = closed
            ? regular_closed_level_syntax_check(
                  term->expr.elems[index], budget, complete)
            : regular_level_syntax_check(
                  term->expr.elems[index], budget, complete);
        if (!valid) return false;
    }
    return true;
}

static bool regular_decl_const_schema_key(
    Atom *term, CettaPrimeRegularKernelBudget *budget, bool *complete) {
    size_t level_count = 0u;
    if (!regular_decl_const_shape(term, NULL, &level_count)) return false;
    for (size_t index = 0u; index < level_count; index++) {
        if (!regular_spend(budget)) {
            if (complete) *complete = false;
            return false;
        }
        uint64_t parameter = 0u;
        if (!regular_level_parameter_syntax(
                term->expr.elems[index + 2u], &parameter) ||
            parameter != (uint64_t)index)
            return false;
    }
    return true;
}

static bool regular_scope_check(Atom *term, uint64_t depth,
                           CettaPrimeRegularKernelBudget *budget,
                           bool *complete) {
    if (!complete || !*complete) return false;
    if (!regular_spend(budget)) {
        *complete = false;
        return false;
    }
    if (!term) return false;
    if (regular_symbol(term, "U0") || regular_symbol(term, "U1")) return true;
    if (regular_expr(term, "Sort", 2u))
        return regular_level_syntax_check(
            term->expr.elems[1], budget, complete);
    if (regular_decl_const_shape(term, NULL, NULL))
        return regular_decl_const_levels_check(
            term, false, budget, complete);
    uint64_t index = 0u;
    if (regular_index(term, &index)) return index < depth;
    if (regular_expr(term, "Pi", 3u) || regular_expr(term, "Sigma", 3u) ||
        regular_expr(term, "Lam", 3u)) {
        return regular_scope_check(
                   term->expr.elems[1], depth, budget, complete) &&
               depth != UINT64_MAX &&
               regular_scope_check(
                   term->expr.elems[2], depth + 1u, budget, complete);
    }
    if (regular_expr(term, "App", 3u) || regular_expr(term, "Pair", 3u)) {
        return regular_scope_check(
                   term->expr.elems[1], depth, budget, complete) &&
               regular_scope_check(
                   term->expr.elems[2], depth, budget, complete);
    }
    if (regular_expr(term, "Fst", 2u) || regular_expr(term, "Snd", 2u) ||
        regular_expr(term, "Refl", 2u)) {
        return regular_scope_check(
            term->expr.elems[1], depth, budget, complete);
    }
    if (regular_expr(term, "Id", 4u)) {
        return regular_scope_check(
                   term->expr.elems[1], depth, budget, complete) &&
               regular_scope_check(
                   term->expr.elems[2], depth, budget, complete) &&
               regular_scope_check(
                   term->expr.elems[3], depth, budget, complete);
    }
    return false;
}

static bool regular_intrinsic_scope_check(
    Atom *term, uint64_t depth, CettaPrimeRegularKernelBudget *budget,
    bool *complete) {
    if (!complete || !*complete) return false;
    if (!regular_spend(budget)) {
        *complete = false;
        return false;
    }
    if (!term) return false;
    if (regular_symbol(term, "U0") || regular_symbol(term, "U1")) return true;
    if (regular_expr(term, "Sort", 2u))
        return regular_level_syntax_check(
            term->expr.elems[1], budget, complete);
    if (regular_decl_const_shape(term, NULL, NULL))
        return regular_decl_const_levels_check(
            term, false, budget, complete);
    uint64_t index = 0u;
    if (regular_index(term, &index)) return index < depth;
    if (regular_expr(term, "Lam", 2u)) {
        return depth != UINT64_MAX && regular_intrinsic_scope_check(
            term->expr.elems[1], depth + 1u, budget, complete);
    }
    if (regular_expr(term, "Pi", 3u) || regular_expr(term, "Sigma", 3u) ||
        regular_expr(term, "Lam", 3u)) {
        return regular_intrinsic_scope_check(
                   term->expr.elems[1], depth, budget, complete) &&
               depth != UINT64_MAX &&
               regular_intrinsic_scope_check(
                   term->expr.elems[2], depth + 1u, budget, complete);
    }
    if (regular_expr(term, "App", 3u) || regular_expr(term, "Pair", 3u)) {
        return regular_intrinsic_scope_check(
                   term->expr.elems[1], depth, budget, complete) &&
               regular_intrinsic_scope_check(
                   term->expr.elems[2], depth, budget, complete);
    }
    if (regular_expr(term, "Fst", 2u) || regular_expr(term, "Snd", 2u) ||
        regular_expr(term, "Refl", 2u)) {
        return regular_intrinsic_scope_check(
            term->expr.elems[1], depth, budget, complete);
    }
    if (regular_expr(term, "Id", 4u)) {
        return regular_intrinsic_scope_check(
                   term->expr.elems[1], depth, budget, complete) &&
               regular_intrinsic_scope_check(
                   term->expr.elems[2], depth, budget, complete) &&
               regular_intrinsic_scope_check(
                   term->expr.elems[3], depth, budget, complete);
    }
    return false;
}

static bool regular_intrinsic_levels_closed(
    Atom *term, CettaPrimeRegularKernelBudget *budget, bool *complete) {
    if (!complete || !*complete) return false;
    if (!regular_spend(budget)) {
        *complete = false;
        return false;
    }
    if (!term) return false;
    if (term->kind == ATOM_SYMBOL)
        return regular_symbol(term, "U0") || regular_symbol(term, "U1");
    if (regular_expr(term, "Sort", 2u))
        return regular_closed_level_syntax_check(
            term->expr.elems[1], budget, complete);
    if (regular_decl_const_shape(term, NULL, NULL))
        return regular_decl_const_levels_check(
            term, true, budget, complete);
    uint64_t ignored = 0u;
    if (regular_index(term, &ignored)) return true;
    if (regular_expr(term, "Lam", 2u) || regular_expr(term, "Fst", 2u) ||
        regular_expr(term, "Snd", 2u) || regular_expr(term, "Refl", 2u)) {
        return regular_intrinsic_levels_closed(
            term->expr.elems[1], budget, complete);
    }
    if (regular_expr(term, "Pi", 3u) || regular_expr(term, "Sigma", 3u) ||
        regular_expr(term, "Lam", 3u) || regular_expr(term, "App", 3u) ||
        regular_expr(term, "Pair", 3u)) {
        return regular_intrinsic_levels_closed(
                   term->expr.elems[1], budget, complete) &&
               regular_intrinsic_levels_closed(
                   term->expr.elems[2], budget, complete);
    }
    if (regular_expr(term, "Id", 4u)) {
        return regular_intrinsic_levels_closed(
                   term->expr.elems[1], budget, complete) &&
               regular_intrinsic_levels_closed(
                   term->expr.elems[2], budget, complete) &&
               regular_intrinsic_levels_closed(
                   term->expr.elems[3], budget, complete);
    }
    return false;
}

static bool regular_context_contains_decl_key(
    Atom *context, Atom *key,
    CettaPrimeRegularKernelBudget *budget, bool *complete) {
    if (!complete || !*complete || !key) return false;
    Atom *wanted_name = NULL;
    if (!regular_decl_const_shape(key, &wanted_name, NULL)) return false;
    Atom *cursor = context;
    while (cursor && !regular_symbol(cursor, "PrimeCtxNil")) {
        if (!regular_spend(budget)) {
            *complete = false;
            return false;
        }
        if (regular_expr(cursor, "PrimeCtxCons", 3u)) {
            cursor = cursor->expr.elems[2];
            continue;
        }
        if (regular_expr(cursor, "PrimeCtxDecl", 4u)) {
            Atom *candidate_name = NULL;
            if (!regular_decl_const_shape(
                    cursor->expr.elems[1], &candidate_name, NULL))
                return false;
            if (atom_eq(candidate_name, wanted_name)) return true;
            cursor = cursor->expr.elems[3];
            continue;
        }
        return false;
    }
    return false;
}

static CettaPrimeRegularKernelStatus regular_context_syntax(
    Atom *context, CettaPrimeRegularKernelBudget *budget,
    uint64_t *length_out, const char **reason_out) {
    if (!regular_spend(budget)) {
        if (reason_out) *reason_out = "scoped-recognition-budget";
        return CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED;
    }
    if (regular_symbol(context, "PrimeCtxNil")) {
        if (length_out) *length_out = 0u;
        return CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED;
    }
    bool local_entry = regular_expr(context, "PrimeCtxCons", 3u);
    bool declaration_entry = regular_expr(context, "PrimeCtxDecl", 4u);
    if (!local_entry && !declaration_entry) {
        if (reason_out) *reason_out = "outside-regular-context-syntax";
        return CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS;
    }

    Atom *key = declaration_entry ? context->expr.elems[1] : NULL;
    Atom *domain = declaration_entry
        ? context->expr.elems[2] : context->expr.elems[1];
    Atom *tail = declaration_entry
        ? context->expr.elems[3] : context->expr.elems[2];
    uint64_t tail_length = 0u;
    CettaPrimeRegularKernelStatus tail_status = regular_context_syntax(
        tail, budget, &tail_length, reason_out);
    if (tail_status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) return tail_status;
    if (local_entry && tail_length == UINT64_MAX) {
        if (reason_out) *reason_out = "regular-context-too-deep";
        return CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS;
    }

    if (declaration_entry) {
        bool key_complete = true;
        if (!regular_decl_const_schema_key(
                key, budget, &key_complete)) {
            if (reason_out)
                *reason_out = key_complete
                    ? "outside-declaration-constant-key"
                    : "scoped-recognition-budget";
            return key_complete
                ? CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS
                : CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED;
        }
        bool duplicate_complete = true;
        if (regular_context_contains_decl_key(
                tail, key, budget, &duplicate_complete)) {
            if (reason_out) *reason_out = "duplicate-declaration-constant";
            return CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS;
        }
        if (!duplicate_complete) {
            if (reason_out) *reason_out = "scoped-recognition-budget";
            return CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED;
        }
    }

    bool complete = true;
    if (!regular_scope_check(domain, tail_length, budget, &complete)) {
        if (reason_out)
            *reason_out = complete
                ? "outside-regular-context-domain"
                : "scoped-recognition-budget";
        return complete
            ? CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS
            : CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED;
    }
    if (length_out)
        *length_out = local_entry ? tail_length + 1u : tail_length;
    return CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED;
}

static CettaPrimeRegularKernelStatus regular_classify_scoped_syntax(
    Atom *scoped, Atom *additional_term,
    Atom **context_out, Atom **term_out, uint64_t *length_out,
    CettaPrimeRegularKernelBudget *budget, const char **reason_out) {
    Atom *context = NULL;
    Atom *term = NULL;
    if (!cetta_prime_regular_kernel_unwrap_scoped(scoped, &context, &term)) {
        if (reason_out) *reason_out = "not-prime-scoped";
        return CETTA_PRIME_REGULAR_KERNEL_NOT_SCOPED;
    }

    uint64_t length = 0u;
    CettaPrimeRegularKernelStatus context_status = regular_context_syntax(
        context, budget, &length, reason_out);
    if (context_status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
        return context_status;

    bool complete = true;
    if (!regular_scope_check(term, length, budget, &complete)) {
        if (reason_out)
            *reason_out = complete
                ? "outside-regular-scoped-term"
                : "scoped-recognition-budget";
        return complete
            ? CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS
            : CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED;
    }
    if (additional_term &&
        !regular_scope_check(additional_term, length, budget, &complete)) {
        if (reason_out)
            *reason_out = complete
                ? "outside-regular-expected-type"
                : "scoped-recognition-budget";
        return complete
            ? CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS
            : CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED;
    }

    if (context_out) *context_out = context;
    if (term_out) *term_out = term;
    if (length_out) *length_out = length;
    return CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED;
}

CettaPrimeRegularKernelResult cetta_prime_regular_kernel_classify_closed_syntax(
    Atom *term, CettaPrimeRegularKernelBudget *budget) {
    bool complete = true;
    if (regular_scope_check(term, 0u, budget, &complete)) {
        bool levels_complete = true;
        if (regular_intrinsic_levels_closed(
                term, budget, &levels_complete))
            return regular_result(
                CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED, NULL, NULL);
        if (!levels_complete)
            return regular_result(
                CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED, NULL,
                "closed-level-recognition-budget");
        return regular_result(
            CETTA_PRIME_REGULAR_KERNEL_NOT_SCOPED, NULL,
            "schematic-level-outside-closed-fragment");
    }
    if (!complete)
        return regular_result(
            CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED, NULL,
            "closed-regular-kernel-recognition-budget");
    return regular_result(
        CETTA_PRIME_REGULAR_KERNEL_NOT_SCOPED, NULL,
        "outside-closed-regular-kernel-syntax");
}

CettaPrimeRegularKernelResult
cetta_prime_regular_kernel_classify_closed_intrinsic_syntax(
    Atom *term, CettaPrimeRegularKernelBudget *budget) {
    bool complete = true;
    if (regular_intrinsic_scope_check(term, 0u, budget, &complete)) {
        bool levels_complete = true;
        if (regular_intrinsic_levels_closed(
                term, budget, &levels_complete))
            return regular_result(
                CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED, NULL, NULL);
        if (!levels_complete)
            return regular_result(
                CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED, NULL,
                "closed-level-recognition-budget");
        return regular_result(
            CETTA_PRIME_REGULAR_KERNEL_NOT_SCOPED, NULL,
            "schematic-level-outside-closed-fragment");
    }
    if (!complete)
        return regular_result(
            CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED, NULL,
            "closed-intrinsic-recognition-budget");
    return regular_result(
        CETTA_PRIME_REGULAR_KERNEL_NOT_SCOPED, NULL,
        "outside-closed-intrinsic-syntax");
}

CettaPrimeRegularKernelResult cetta_prime_regular_kernel_classify_scoped_syntax(
    Atom *scoped, CettaPrimeRegularKernelBudget *budget) {
    const char *reason = NULL;
    CettaPrimeRegularKernelStatus status = regular_classify_scoped_syntax(
        scoped, NULL, NULL, NULL, NULL, budget, &reason);
    return regular_result(status, NULL, reason);
}

CettaPrimeRegularKernelResult cetta_prime_regular_kernel_classify_scoped_check_syntax(
    Atom *scoped, Atom *expected, CettaPrimeRegularKernelBudget *budget) {
    const char *reason = NULL;
    CettaPrimeRegularKernelStatus status = regular_classify_scoped_syntax(
        scoped, expected, NULL, NULL, NULL, budget, &reason);
    return regular_result(status, NULL, reason);
}

static Atom *regular_shift(Arena *arena, Atom *term, int64_t amount,
                      uint64_t cutoff, bool *ok,
                      CettaPrimeRegularKernelBudget *budget) {
    if (!ok || !*ok || !regular_spend(budget) || !term) {
        if (ok) *ok = false;
        return NULL;
    }
    if (regular_symbol(term, "U0") || regular_symbol(term, "U1") ||
        regular_expr(term, "Sort", 2u) ||
        regular_decl_const_shape(term, NULL, NULL)) return term;
    uint64_t index = 0u;
    if (regular_index(term, &index)) {
        if (index < cutoff) return term;
        if (amount < 0) {
            uint64_t magnitude = (uint64_t)(-(amount + 1)) + 1u;
            if (index < magnitude) {
                *ok = false;
                return NULL;
            }
            index -= magnitude;
        } else {
            uint64_t magnitude = (uint64_t)amount;
            if (UINT64_MAX - index < magnitude) {
                *ok = false;
                return NULL;
            }
            index += magnitude;
        }
        Atom *shifted = regular_make_index(arena, index);
        if (!shifted) *ok = false;
        return shifted;
    }
    if (regular_expr(term, "Lam", 2u)) {
        if (cutoff == UINT64_MAX) {
            *ok = false;
            return NULL;
        }
        Atom *body = regular_shift(
            arena, term->expr.elems[1], amount, cutoff + 1u, ok, budget);
        return body ? atom_expr2(arena, term->expr.elems[0], body) : NULL;
    }
    if (regular_expr(term, "Pi", 3u) || regular_expr(term, "Sigma", 3u) ||
        regular_expr(term, "Lam", 3u)) {
        Atom *domain = regular_shift(
            arena, term->expr.elems[1], amount, cutoff, ok, budget);
        if (!domain || cutoff == UINT64_MAX) {
            *ok = false;
            return NULL;
        }
        Atom *body = regular_shift(
            arena, term->expr.elems[2], amount, cutoff + 1u, ok, budget);
        if (!body) return NULL;
        return atom_expr3(arena, term->expr.elems[0], domain, body);
    }
    if (regular_expr(term, "App", 3u) || regular_expr(term, "Pair", 3u)) {
        Atom *function = regular_shift(
            arena, term->expr.elems[1], amount, cutoff, ok, budget);
        Atom *argument = function ? regular_shift(
            arena, term->expr.elems[2], amount, cutoff, ok, budget) : NULL;
        return function && argument
            ? atom_expr3(arena, term->expr.elems[0], function, argument)
            : NULL;
    }
    if (regular_expr(term, "Fst", 2u) || regular_expr(term, "Snd", 2u) ||
        regular_expr(term, "Refl", 2u)) {
        Atom *nested = regular_shift(
            arena, term->expr.elems[1], amount, cutoff, ok, budget);
        return nested ? atom_expr2(arena, term->expr.elems[0], nested) : NULL;
    }
    if (regular_expr(term, "Id", 4u)) {
        Atom *carrier = regular_shift(
            arena, term->expr.elems[1], amount, cutoff, ok, budget);
        Atom *left = carrier ? regular_shift(
            arena, term->expr.elems[2], amount, cutoff, ok, budget) : NULL;
        Atom *right = left ? regular_shift(
            arena, term->expr.elems[3], amount, cutoff, ok, budget) : NULL;
        return carrier && left && right
            ? regular_expr4(
                  arena, term->expr.elems[0], carrier, left, right)
            : NULL;
    }
    *ok = false;
    return NULL;
}

CettaPrimeRegularKernelResult
cetta_prime_regular_kernel_weaken_intrinsic_type_v1(
    Arena *arena, Atom *type, uint64_t assumptions,
    CettaPrimeRegularKernelBudget *budget) {
    if (!arena || !type || !budget || assumptions > (uint64_t)INT64_MAX)
        return regular_result(
            CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE, NULL,
            "invalid-intrinsic-weakening-input");
    bool ok = true;
    Atom *weakened = regular_shift(
        arena, type, (int64_t)assumptions, 0u, &ok, budget);
    if (weakened && ok)
        return regular_result(
            CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED, weakened, NULL);
    if (budget->limited && budget->remaining == 0u)
        return regular_result(
            CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED, NULL,
            "intrinsic-weakening-budget");
    return regular_result(
        CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE, NULL,
        "intrinsic-weakening-failed");
}

static Atom *regular_substitute_zero_rec(
    Arena *arena, Atom *body, Atom *argument, uint64_t binder_depth,
    bool *ok, CettaPrimeRegularKernelBudget *budget) {
    if (!ok || !*ok || !regular_spend(budget) || !body) {
        if (ok) *ok = false;
        return NULL;
    }
    if (regular_symbol(body, "U0") || regular_symbol(body, "U1") ||
        regular_expr(body, "Sort", 2u) ||
        regular_decl_const_shape(body, NULL, NULL)) return body;
    uint64_t index = 0u;
    if (regular_index(body, &index)) {
        if (index < binder_depth) return body;
        if (index == binder_depth) {
            return regular_shift(
                arena, argument, (int64_t)binder_depth, 0u, ok, budget);
        }
        return regular_make_index(arena, index - 1u);
    }
    if (regular_expr(body, "Lam", 2u)) {
        if (binder_depth == UINT64_MAX) {
            *ok = false;
            return NULL;
        }
        Atom *nested = regular_substitute_zero_rec(
            arena, body->expr.elems[1], argument, binder_depth + 1u,
            ok, budget);
        return nested ? atom_expr2(arena, body->expr.elems[0], nested) : NULL;
    }
    if (regular_expr(body, "Pi", 3u) || regular_expr(body, "Sigma", 3u) ||
        regular_expr(body, "Lam", 3u)) {
        Atom *domain = regular_substitute_zero_rec(
            arena, body->expr.elems[1], argument, binder_depth, ok, budget);
        if (!domain || binder_depth == UINT64_MAX) {
            *ok = false;
            return NULL;
        }
        Atom *nested = regular_substitute_zero_rec(
            arena, body->expr.elems[2], argument, binder_depth + 1u,
            ok, budget);
        return nested
            ? atom_expr3(arena, body->expr.elems[0], domain, nested)
            : NULL;
    }
    if (regular_expr(body, "App", 3u) || regular_expr(body, "Pair", 3u)) {
        Atom *function = regular_substitute_zero_rec(
            arena, body->expr.elems[1], argument, binder_depth, ok, budget);
        Atom *value = function ? regular_substitute_zero_rec(
            arena, body->expr.elems[2], argument, binder_depth, ok, budget)
            : NULL;
        return function && value
            ? atom_expr3(arena, body->expr.elems[0], function, value)
            : NULL;
    }
    if (regular_expr(body, "Fst", 2u) || regular_expr(body, "Snd", 2u) ||
        regular_expr(body, "Refl", 2u)) {
        Atom *nested = regular_substitute_zero_rec(
            arena, body->expr.elems[1], argument, binder_depth, ok, budget);
        return nested ? atom_expr2(arena, body->expr.elems[0], nested) : NULL;
    }
    if (regular_expr(body, "Id", 4u)) {
        Atom *carrier = regular_substitute_zero_rec(
            arena, body->expr.elems[1], argument, binder_depth, ok, budget);
        Atom *left = carrier ? regular_substitute_zero_rec(
            arena, body->expr.elems[2], argument, binder_depth, ok, budget)
            : NULL;
        Atom *right = left ? regular_substitute_zero_rec(
            arena, body->expr.elems[3], argument, binder_depth, ok, budget)
            : NULL;
        return carrier && left && right
            ? regular_expr4(
                  arena, body->expr.elems[0], carrier, left, right)
            : NULL;
    }
    *ok = false;
    return NULL;
}

static Atom *regular_substitute_zero(
    Arena *arena, Atom *body, Atom *argument, bool *ok,
    CettaPrimeRegularKernelBudget *budget) {
    return regular_substitute_zero_rec(
        arena, body, argument, 0u, ok, budget);
}

static bool regular_uses_outer_zero(
    Atom *term, uint64_t binder_depth, CettaPrimeRegularKernelBudget *budget,
    bool *complete) {
    if (!complete || !*complete || !regular_spend(budget) || !term) {
        if (complete) *complete = false;
        return false;
    }
    if (regular_symbol(term, "U0") || regular_symbol(term, "U1") ||
        regular_expr(term, "Sort", 2u) ||
        regular_decl_const_shape(term, NULL, NULL)) return false;
    uint64_t index = 0u;
    if (regular_index(term, &index)) return index == binder_depth;
    if (regular_expr(term, "Lam", 2u)) {
        if (binder_depth == UINT64_MAX) {
            *complete = false;
            return false;
        }
        return regular_uses_outer_zero(
            term->expr.elems[1], binder_depth + 1u, budget, complete);
    }
    if (regular_expr(term, "Pi", 3u) || regular_expr(term, "Sigma", 3u) ||
        regular_expr(term, "Lam", 3u)) {
        if (regular_uses_outer_zero(
                term->expr.elems[1], binder_depth, budget, complete)) {
            return true;
        }
        if (binder_depth == UINT64_MAX) {
            *complete = false;
            return false;
        }
        return regular_uses_outer_zero(
            term->expr.elems[2], binder_depth + 1u, budget, complete);
    }
    if (regular_expr(term, "App", 3u) || regular_expr(term, "Pair", 3u)) {
        return regular_uses_outer_zero(
                   term->expr.elems[1], binder_depth, budget, complete) ||
               regular_uses_outer_zero(
                   term->expr.elems[2], binder_depth, budget, complete);
    }
    if (regular_expr(term, "Fst", 2u) || regular_expr(term, "Snd", 2u) ||
        regular_expr(term, "Refl", 2u)) {
        return regular_uses_outer_zero(
            term->expr.elems[1], binder_depth, budget, complete);
    }
    if (regular_expr(term, "Id", 4u)) {
        return regular_uses_outer_zero(
                   term->expr.elems[1], binder_depth, budget, complete) ||
               regular_uses_outer_zero(
                   term->expr.elems[2], binder_depth, budget, complete) ||
               regular_uses_outer_zero(
                   term->expr.elems[3], binder_depth, budget, complete);
    }
    *complete = false;
    return false;
}

static PrimeRegularKernelNormal regular_normalize(
    Arena *arena, Atom *term, CettaPrimeRegularKernelBudget *budget) {
    if (!regular_spend(budget))
        return regular_normal_result(
            CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED, NULL, "normalization-budget");
    if (!term || !regular_intrinsic_term_shape(term))
        return regular_normal_result(
            CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS, NULL,
            "outside-regular-kernel-term");
    if (regular_symbol(term, "U0") || regular_symbol(term, "U1") ||
        regular_expr(term, "Sort", 2u) || regular_expr(term, "idx", 2u) ||
        regular_decl_const_shape(term, NULL, NULL)) {
        if (regular_expr(term, "Sort", 2u)) {
            const CettaPrimeLevelV1 *level = NULL;
            const char *reason = NULL;
            CettaPrimeRegularKernelStatus status = regular_decode_sort(
                arena, term, budget, &level, &reason);
            if (status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
                return regular_normal_result(status, NULL, reason);
        }
        if (regular_decl_const_shape(term, NULL, NULL)) {
            for (CettaExprIndex index = 2u;
                 index < term->expr.len; index++) {
                const CettaPrimeLevelV1 *level = NULL;
                const char *reason = NULL;
                CettaPrimeRegularKernelStatus status = regular_decode_level(
                    arena, term->expr.elems[index], budget, &level, &reason);
                if (status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
                    return regular_normal_result(status, NULL, reason);
            }
        }
        return regular_normal_result(CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED, term, NULL);
    }
    if (regular_expr(term, "Lam", 2u)) {
        PrimeRegularKernelNormal body = regular_normalize(
            arena, term->expr.elems[1], budget);
        if (body.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) return body;
        if (regular_expr(body.term, "App", 3u)) {
            uint64_t argument_index = 0u;
            if (regular_index(body.term->expr.elems[2], &argument_index) &&
                argument_index == 0u) {
                bool complete = true;
                bool used = regular_uses_outer_zero(
                    body.term->expr.elems[1], 0u, budget, &complete);
                if (!complete)
                    return regular_normal_result(
                        CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED, NULL,
                        "normalization-budget");
                if (!used) {
                    bool ok = true;
                    Atom *lowered = regular_shift(
                        arena, body.term->expr.elems[1], -1, 0u,
                        &ok, budget);
                    if (!ok || !lowered)
                        return regular_normal_result(
                            CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE, NULL,
                            "eta-lowering-failed");
                    return regular_normal_result(
                        CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED, lowered, NULL);
                }
            }
        }
        return regular_normal_result(
            CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
            atom_expr2(arena, term->expr.elems[0], body.term), NULL);
    }
    if (regular_expr(term, "Pi", 3u) || regular_expr(term, "Sigma", 3u) ||
        regular_expr(term, "Lam", 3u)) {
        PrimeRegularKernelNormal domain = regular_normalize(
            arena, term->expr.elems[1], budget);
        if (domain.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) return domain;
        PrimeRegularKernelNormal body = regular_normalize(
            arena, term->expr.elems[2], budget);
        if (body.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) return body;
        if (regular_expr(term, "Lam", 3u) && regular_expr(body.term, "App", 3u)) {
            uint64_t argument_index = 0u;
            if (regular_index(body.term->expr.elems[2], &argument_index) &&
                argument_index == 0u) {
                bool complete = true;
                bool used = regular_uses_outer_zero(
                    body.term->expr.elems[1], 0u, budget, &complete);
                if (!complete)
                    return regular_normal_result(
                        CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED, NULL,
                        "normalization-budget");
                if (!used) {
                    bool ok = true;
                    Atom *lowered = regular_shift(
                        arena, body.term->expr.elems[1], -1, 0u,
                        &ok, budget);
                    if (!ok || !lowered)
                        return regular_normal_result(
                            CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE, NULL,
                            "eta-lowering-failed");
                    return regular_normal_result(
                        CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED, lowered, NULL);
                }
            }
        }
        return regular_normal_result(
            CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
            atom_expr3(arena, term->expr.elems[0], domain.term, body.term),
            NULL);
    }
    if (regular_expr(term, "Id", 4u)) {
        PrimeRegularKernelNormal carrier = regular_normalize(
            arena, term->expr.elems[1], budget);
        if (carrier.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
            return carrier;
        PrimeRegularKernelNormal left = regular_normalize(
            arena, term->expr.elems[2], budget);
        if (left.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) return left;
        PrimeRegularKernelNormal right = regular_normalize(
            arena, term->expr.elems[3], budget);
        if (right.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) return right;
        return regular_normal_result(
            CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
            regular_expr4(
                arena, atom_symbol(arena, "Id"), carrier.term,
                left.term, right.term),
            NULL);
    }
    if (regular_expr(term, "Fst", 2u) || regular_expr(term, "Snd", 2u) ||
        regular_expr(term, "Refl", 2u)) {
        PrimeRegularKernelNormal nested = regular_normalize(
            arena, term->expr.elems[1], budget);
        if (nested.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
            return nested;
        if (regular_expr(term, "Fst", 2u) &&
            regular_expr(nested.term, "Pair", 3u)) {
            return regular_normal_result(
                CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
                nested.term->expr.elems[1], NULL);
        }
        if (regular_expr(term, "Snd", 2u) &&
            regular_expr(nested.term, "Pair", 3u)) {
            return regular_normal_result(
                CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
                nested.term->expr.elems[2], NULL);
        }
        return regular_normal_result(
            CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
            atom_expr2(arena, term->expr.elems[0], nested.term), NULL);
    }
    if (regular_expr(term, "App", 3u) || regular_expr(term, "Pair", 3u)) {
        PrimeRegularKernelNormal first = regular_normalize(
            arena, term->expr.elems[1], budget);
        if (first.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) return first;
        PrimeRegularKernelNormal second = regular_normalize(
            arena, term->expr.elems[2], budget);
        if (second.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) return second;
        if (regular_expr(term, "App", 3u) &&
            (regular_expr(first.term, "Lam", 2u) ||
             regular_expr(first.term, "Lam", 3u))) {
            bool ok = true;
            Atom *lambda_body = first.term->expr.elems[
                first.term->expr.len == 2u ? 1u : 2u];
            Atom *substituted = regular_substitute_zero(
                arena, lambda_body, second.term, &ok, budget);
            if (!ok || !substituted)
                return regular_normal_result(
                    CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE, NULL,
                    "beta-substitution-failed");
            return regular_normalize(arena, substituted, budget);
        }
        return regular_normal_result(
            CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
            atom_expr3(arena, term->expr.elems[0], first.term, second.term),
            NULL);
    }
    return regular_normal_result(
        CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE, NULL,
        "regular-normalizer-missing-constructor");
}

static CettaPrimeRegularKernelStatus regular_equal_normal_terms(
    Arena *arena, Atom *left, Atom *right,
    CettaPrimeRegularKernelBudget *budget, bool *equal_out,
    const char **reason_out,
    PrimeRegularLevelInstantiation *instantiation);

/* Exact level instantiation used inside conversion.  The deliberately small
 * solved fragment is one bare, fresh declaration parameter against one
 * closed level normal form.  More general level equations remain outside the
 * native fragment rather than becoming a false type mismatch. */
static CettaPrimeRegularKernelStatus
regular_equal_sort_levels_instantiating(
    Arena *arena, Atom *left, Atom *right,
    CettaPrimeRegularKernelBudget *budget, bool *equal_out,
    const char **reason_out,
    PrimeRegularLevelInstantiation *instantiation) {
    Atom *left_level = regular_sort_level_syntax(arena, left);
    Atom *right_level = regular_sort_level_syntax(arena, right);
    if (!left_level || !right_level || !instantiation) {
        if (reason_out) *reason_out = "invalid-level-equality-instantiation";
        return CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE;
    }

    bool complete = true;
    Atom *instantiated_left = regular_level_apply_instantiation(
        arena, left_level, instantiation, budget, &complete);
    Atom *instantiated_right = complete
        ? regular_level_apply_instantiation(
              arena, right_level, instantiation, budget, &complete)
        : NULL;
    if (!complete || !instantiated_left || !instantiated_right) {
        if (reason_out) *reason_out = "level-instantiation-budget";
        return budget && budget->limited && budget->remaining == 0u
            ? CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED
            : CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE;
    }
    if (atom_eq(instantiated_left, instantiated_right)) {
        *equal_out = true;
        return CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED;
    }

    uint64_t left_parameter = 0u;
    uint64_t right_parameter = 0u;
    size_t left_parameter_index = 0u;
    size_t right_parameter_index = 0u;
    bool left_is_fresh_parameter =
        regular_level_parameter_syntax(
            instantiated_left, &left_parameter) &&
        regular_level_instantiation_find(
            instantiation, left_parameter, &left_parameter_index) &&
        !instantiation->assignments[left_parameter_index];
    bool right_is_fresh_parameter =
        regular_level_parameter_syntax(
            instantiated_right, &right_parameter) &&
        regular_level_instantiation_find(
            instantiation, right_parameter, &right_parameter_index) &&
        !instantiation->assignments[right_parameter_index];
    if (left_is_fresh_parameter && right_is_fresh_parameter) {
        size_t assigned_index = left_parameter_index > right_parameter_index
            ? left_parameter_index : right_parameter_index;
        Atom *representative = left_parameter_index > right_parameter_index
            ? instantiated_right : instantiated_left;
        instantiation->assignments[assigned_index] = representative;
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_DECLARATION_LEVEL_CONSTRAINT);
        *equal_out = true;
        return CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED;
    }
    if (left_is_fresh_parameter &&
        !regular_level_mentions_instantiation_parameter(
            instantiated_right, instantiation)) {
        instantiation->assignments[left_parameter_index] =
            instantiated_right;
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_DECLARATION_LEVEL_CONSTRAINT);
        *equal_out = true;
        return CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED;
    }

    if (right_is_fresh_parameter &&
        !regular_level_mentions_instantiation_parameter(
            instantiated_left, instantiation)) {
        instantiation->assignments[right_parameter_index] =
            instantiated_left;
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_DECLARATION_LEVEL_CONSTRAINT);
        *equal_out = true;
        return CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED;
    }

    if (regular_level_mentions_instantiation_parameter(
            instantiated_left, instantiation) ||
        regular_level_mentions_instantiation_parameter(
            instantiated_right, instantiation)) {
        if (reason_out)
            *reason_out = "level-equality-instantiation-outside-fragment";
        return CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS;
    }

    const CettaPrimeLevelV1 *left_normal = NULL;
    const CettaPrimeLevelV1 *right_normal = NULL;
    CettaPrimeRegularKernelStatus status = regular_decode_level(
        arena, instantiated_left, budget, &left_normal, reason_out);
    if (status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) return status;
    status = regular_decode_level(
        arena, instantiated_right, budget, &right_normal, reason_out);
    if (status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) return status;
    *equal_out = cetta_prime_level_equal_v1(left_normal, right_normal);
    return CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED;
}

static CettaPrimeRegularKernelStatus regular_equal_decl_constants(
    Arena *arena, Atom *left, Atom *right,
    CettaPrimeRegularKernelBudget *budget, bool *equal_out,
    const char **reason_out,
    PrimeRegularLevelInstantiation *instantiation) {
    Atom *left_name = NULL;
    Atom *right_name = NULL;
    size_t left_levels = 0u;
    size_t right_levels = 0u;
    if (!regular_decl_const_shape(left, &left_name, &left_levels) ||
        !regular_decl_const_shape(right, &right_name, &right_levels)) {
        if (reason_out) *reason_out = "invalid-declaration-constant";
        return CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE;
    }
    if (!atom_eq(left_name, right_name) || left_levels != right_levels) {
        *equal_out = false;
        return CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED;
    }
    for (size_t index = 0u; index < left_levels; index++) {
        Atom *left_sort = atom_expr2(
            arena, atom_symbol(arena, "Sort"),
            left->expr.elems[index + 2u]);
        Atom *right_sort = atom_expr2(
            arena, atom_symbol(arena, "Sort"),
            right->expr.elems[index + 2u]);
        bool level_equal = false;
        CettaPrimeRegularKernelStatus status;
        if (instantiation) {
            status = regular_equal_sort_levels_instantiating(
                arena, left_sort, right_sort, budget, &level_equal,
                reason_out, instantiation);
        } else {
            const CettaPrimeLevelV1 *left_level = NULL;
            const CettaPrimeLevelV1 *right_level = NULL;
            status = regular_decode_level(
                arena, left->expr.elems[index + 2u], budget,
                &left_level, reason_out);
            if (status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
                status = regular_decode_level(
                    arena, right->expr.elems[index + 2u], budget,
                    &right_level, reason_out);
            if (status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
                level_equal = cetta_prime_level_equal_v1(
                    left_level, right_level);
        }
        if (status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
            return status;
        if (!level_equal) {
            *equal_out = false;
            return CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED;
        }
    }
    *equal_out = true;
    return CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED;
}

static CettaPrimeRegularKernelStatus regular_equal_normal_terms(
    Arena *arena, Atom *left, Atom *right,
    CettaPrimeRegularKernelBudget *budget, bool *equal_out,
    const char **reason_out,
    PrimeRegularLevelInstantiation *instantiation) {
    if (!equal_out) {
        if (reason_out) *reason_out = "invalid-conversion-comparison-output";
        return CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE;
    }
    if (!regular_spend(budget)) {
        if (reason_out) *reason_out = "conversion-comparison-budget";
        return CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED;
    }
    bool left_sort = regular_symbol(left, "U1") ||
        regular_expr(left, "Sort", 2u);
    bool right_sort = regular_symbol(right, "U1") ||
        regular_expr(right, "Sort", 2u);
    if (left_sort || right_sort) {
        if (!left_sort || !right_sort) {
            *equal_out = false;
            return CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED;
        }
        if (instantiation)
            return regular_equal_sort_levels_instantiating(
                arena, left, right, budget, equal_out, reason_out,
                instantiation);
        const CettaPrimeLevelV1 *left_level = NULL;
        const CettaPrimeLevelV1 *right_level = NULL;
        CettaPrimeRegularKernelStatus status = regular_decode_sort(
            arena, left, budget, &left_level, reason_out);
        if (status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) return status;
        status = regular_decode_sort(
            arena, right, budget, &right_level, reason_out);
        if (status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) return status;
        *equal_out = cetta_prime_level_equal_v1(left_level, right_level);
        return CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED;
    }
    bool left_decl = regular_decl_const_shape(left, NULL, NULL);
    bool right_decl = regular_decl_const_shape(right, NULL, NULL);
    if (left_decl || right_decl) {
        if (!left_decl || !right_decl) {
            *equal_out = false;
            return CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED;
        }
        return regular_equal_decl_constants(
            arena, left, right, budget, equal_out, reason_out,
            instantiation);
    }
    if (!left || !right || left->kind != ATOM_EXPR ||
        right->kind != ATOM_EXPR || left->expr.len != right->expr.len) {
        *equal_out = atom_eq(left, right);
        return CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED;
    }
    for (CettaExprIndex index = 0u; index < left->expr.len; index++) {
        bool children_equal = false;
        CettaPrimeRegularKernelStatus status = regular_equal_normal_terms(
            arena, left->expr.elems[index], right->expr.elems[index],
            budget, &children_equal, reason_out, instantiation);
        if (status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) return status;
        if (!children_equal) {
            *equal_out = false;
            return CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED;
        }
    }
    *equal_out = true;
    return CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED;
}

static CettaPrimeRegularKernelStatus regular_convert_terms(
    Arena *arena, Atom *left, Atom *right,
    CettaPrimeRegularKernelBudget *budget, bool *equal_out,
    const char **reason_out,
    PrimeRegularLevelInstantiation *instantiation) {
    PrimeRegularKernelNormal left_normal = regular_normalize(arena, left, budget);
    if (left_normal.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
        if (reason_out) *reason_out = left_normal.reason;
        return left_normal.status;
    }
    PrimeRegularKernelNormal right_normal = regular_normalize(arena, right, budget);
    if (right_normal.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
        if (reason_out) *reason_out = right_normal.reason;
        return right_normal.status;
    }
    bool equal = false;
    CettaPrimeRegularKernelStatus status = regular_equal_normal_terms(
        arena, left_normal.term, right_normal.term, budget,
        &equal, reason_out, instantiation);
    if (status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED && equal_out)
        *equal_out = equal;
    return status;
}

static Atom *regular_context_extend(Arena *arena, Atom *context, Atom *domain) {
    return atom_expr3(
        arena, atom_symbol(arena, "PrimeCtxCons"), domain, context);
}

static Atom *regular_context_lookup(
    Arena *arena, Atom *context, uint64_t index,
    CettaPrimeRegularKernelBudget *budget, bool *complete) {
    if (!complete || !*complete) return NULL;
    Atom *cursor = context;
    uint64_t current = 0u;
    while (cursor) {
        if (!regular_spend(budget)) {
            *complete = false;
            return NULL;
        }
        if (regular_expr(cursor, "PrimeCtxDecl", 4u)) {
            cursor = cursor->expr.elems[3];
            continue;
        }
        if (!regular_expr(cursor, "PrimeCtxCons", 3u)) return NULL;
        if (current == index) {
            if (index >= (uint64_t)INT64_MAX) return NULL;
            bool ok = true;
            Atom *shifted = regular_shift(
                arena, cursor->expr.elems[1], (int64_t)(index + 1u),
                0u, &ok, budget);
            if (!shifted && !ok && budget && budget->limited &&
                budget->remaining == 0u) {
                *complete = false;
            }
            return shifted;
        }
        if (current == UINT64_MAX) return NULL;
        current++;
        cursor = cursor->expr.elems[2];
    }
    return NULL;
}

static Atom *regular_context_lookup_declaration(
    Arena *arena, Atom *context, Atom *key,
    CettaPrimeRegularKernelBudget *budget, bool *complete) {
    if (!arena || !context || !key || !complete || !*complete) return NULL;
    Atom *wanted_name = NULL;
    size_t wanted_levels = 0u;
    if (!regular_decl_const_shape(
            key, &wanted_name, &wanted_levels))
        return NULL;
    Atom *cursor = context;
    uint64_t local_depth = 0u;
    while (cursor) {
        if (!regular_spend(budget)) {
            *complete = false;
            return NULL;
        }
        if (regular_expr(cursor, "PrimeCtxCons", 3u)) {
            if (local_depth == UINT64_MAX) return NULL;
            local_depth++;
            cursor = cursor->expr.elems[2];
            continue;
        }
        if (regular_expr(cursor, "PrimeCtxDecl", 4u)) {
            Atom *schema_key = cursor->expr.elems[1];
            Atom *schema_name = NULL;
            size_t schema_levels = 0u;
            if (!regular_decl_const_shape(
                    schema_key, &schema_name, &schema_levels))
                return NULL;
            if (atom_eq(schema_name, wanted_name)) {
                if (schema_levels != wanted_levels ||
                    wanted_levels > SIZE_MAX / sizeof(uint64_t) ||
                    wanted_levels > SIZE_MAX / sizeof(Atom *))
                    return NULL;
                Atom *instantiated_type = cursor->expr.elems[2];
                if (wanted_levels != 0u) {
                    uint64_t *parameters = arena_alloc(
                        arena, wanted_levels * sizeof(*parameters));
                    Atom **assignments = arena_alloc(
                        arena, wanted_levels * sizeof(*assignments));
                    size_t replacement_count = 0u;
                    for (size_t index = 0u;
                         index < wanted_levels; index++) {
                        Atom *argument = key->expr.elems[index + 2u];
                        if (atom_eq(
                                schema_key->expr.elems[index + 2u],
                                argument))
                            continue;
                        parameters[replacement_count] = (uint64_t)index;
                        assignments[replacement_count] = argument;
                        replacement_count++;
                    }
                    PrimeRegularLevelInstantiation schema_instance = {
                        .parameters = parameters,
                        .assignments = assignments,
                        .count = replacement_count,
                    };
                    instantiated_type = replacement_count == 0u
                        ? instantiated_type :
                        regular_term_apply_level_instantiation(
                            arena, instantiated_type, &schema_instance,
                            budget, complete);
                    if (!instantiated_type || !*complete) return NULL;
                }
                if (local_depth > (uint64_t)INT64_MAX) return NULL;
                bool ok = true;
                Atom *shifted = regular_shift(
                    arena, instantiated_type, (int64_t)local_depth,
                    0u, &ok, budget);
                if (!shifted && !ok && budget && budget->limited &&
                    budget->remaining == 0u) {
                    *complete = false;
                }
                return shifted;
            }
            cursor = cursor->expr.elems[3];
            continue;
        }
        return NULL;
    }
    return NULL;
}

static Atom *regular_context_declaration_type_in_block(
    Atom *context, Atom *key, CettaPrimeRegularKernelBudget *budget,
    bool *complete) {
    if (!context || !key || !complete || !*complete) return NULL;
    Atom *cursor = context;
    while (regular_expr(cursor, "PrimeCtxDecl", 4u)) {
        if (!regular_spend(budget)) {
            *complete = false;
            return NULL;
        }
        if (atom_eq(cursor->expr.elems[1], key))
            return cursor->expr.elems[2];
        cursor = cursor->expr.elems[3];
    }
    return NULL;
}

/* Fresh polymorphic occurrences may solve to the same closed universe
 * instance.  A declaration context is a finite map, so canonicalize those
 * coincident entries after level solving instead of replaying a redundant
 * binding.  Different types at one closed key are an impossible result of
 * instantiation and therefore fail construction. */
static Atom *regular_context_deduplicate_declarations(
    Arena *arena, Atom *context, CettaPrimeRegularKernelBudget *budget,
    bool *complete) {
    if (!arena || !context || !complete || !*complete ||
        !regular_spend(budget)) {
        if (complete) *complete = false;
        return NULL;
    }
    if (regular_symbol(context, "PrimeCtxNil")) return context;
    if (regular_expr(context, "PrimeCtxCons", 3u)) {
        Atom *tail = regular_context_deduplicate_declarations(
            arena, context->expr.elems[2], budget, complete);
        return tail && *complete
            ? atom_expr3(
                  arena, atom_symbol(arena, "PrimeCtxCons"),
                  context->expr.elems[1], tail)
            : NULL;
    }
    if (!regular_expr(context, "PrimeCtxDecl", 4u)) {
        *complete = false;
        return NULL;
    }
    Atom *tail = regular_context_deduplicate_declarations(
        arena, context->expr.elems[3], budget, complete);
    if (!tail || !*complete) return NULL;
    Atom *existing_type = regular_context_declaration_type_in_block(
        tail, context->expr.elems[1], budget, complete);
    if (!*complete) return NULL;
    if (existing_type) {
        if (!atom_eq(existing_type, context->expr.elems[2])) {
            *complete = false;
            return NULL;
        }
        return tail;
    }
    return regular_expr4(
        arena, atom_symbol(arena, "PrimeCtxDecl"),
        context->expr.elems[1], context->expr.elems[2], tail);
}

static PrimeRegularKernelInfer regular_infer(
    Arena *arena, Atom *context, Atom *term,
    CettaPrimeRegularKernelBudget *budget,
    PrimeRegularLevelInstantiation *instantiation);

static CettaPrimeRegularKernelStatus regular_type_sort(
    Arena *arena, Atom *context, Atom *type,
    CettaPrimeRegularKernelBudget *budget, Atom **sort_out,
    const char **reason_out,
    PrimeRegularLevelInstantiation *instantiation) {
    PrimeRegularKernelInfer inferred = regular_infer(
        arena, context, type, budget, instantiation);
    if (inferred.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
        if (reason_out) *reason_out = inferred.reason;
        return inferred.status;
    }
    const CettaPrimeLevelV1 *level = NULL;
    CettaPrimeRegularKernelStatus decoded = regular_decode_sort(
        arena, inferred.type, budget, &level, reason_out);
    if (decoded != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
        if (decoded == CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED ||
            decoded == CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE)
            return decoded;
        /* A position that must hold a formed type of the fragment holds
         * something whose inferred type is not a universe sort (for example,
         * a legacy-ground inhabitant used as a type).  This is a coverage
         * boundary, not a counterexample. */
        if (reason_out) *reason_out = "expected-formed-type";
        return CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS;
    }
    if (sort_out) *sort_out = inferred.type;
    return CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED;
}

static CettaPrimeRegularKernelStatus regular_ordinary_type(
    Arena *arena, Atom *context, Atom *type,
    CettaPrimeRegularKernelBudget *budget, const char **reason_out,
    PrimeRegularLevelInstantiation *instantiation) {
    return regular_type_sort(
        arena, context, type, budget, NULL, reason_out, instantiation);
}

static CettaPrimeRegularKernelStatus regular_sort_le(
    Arena *arena, Atom *left, Atom *right,
    CettaPrimeRegularKernelBudget *budget, bool *le_out,
    const char **reason_out,
    PrimeRegularLevelInstantiation *instantiation) {
    if (le_out) *le_out = false;
    if (!le_out) {
        if (reason_out) *reason_out = "invalid-level-order-output";
        return CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE;
    }
    Atom *left_level_syntax = regular_sort_level_syntax(arena, left);
    Atom *right_level_syntax = regular_sort_level_syntax(arena, right);
    if (!left_level_syntax || !right_level_syntax) {
        if (reason_out) *reason_out = "expected-universe-sort";
        return CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS;
    }
    bool complete = true;
    Atom *instantiated_left = instantiation
        ? regular_level_apply_instantiation(
              arena, left_level_syntax, instantiation, budget, &complete)
        : left_level_syntax;
    Atom *instantiated_right = complete && instantiation
        ? regular_level_apply_instantiation(
              arena, right_level_syntax, instantiation, budget, &complete)
        : right_level_syntax;
    if (!complete || !instantiated_left || !instantiated_right) {
        if (reason_out) *reason_out = "level-instantiation-budget";
        return CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED;
    }
    const CettaPrimeLevelV1 *left_level = NULL;
    const CettaPrimeLevelV1 *right_level = NULL;
    CettaPrimeRegularKernelStatus status = regular_decode_sort(
        arena, atom_expr2(
            arena, atom_symbol(arena, "Sort"), instantiated_left),
        budget, &left_level, reason_out);
    if (status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) return status;
    status = regular_decode_sort(
        arena, atom_expr2(
            arena, atom_symbol(arena, "Sort"), instantiated_right),
        budget, &right_level, reason_out);
    if (status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) return status;
    *le_out = cetta_prime_level_le_v1(left_level, right_level);
    if (*le_out || !instantiation ||
        !regular_level_mentions_instantiation_parameter(
            right_level_syntax, instantiation))
        return CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED;
    /* A schema-rigid parameter is closed relative to this instantiation and
     * is therefore a valid lower bound for a freshly looked-up declaration.
     * Only a bound mentioning one of the variables being solved would create
     * a cyclic or mutually recursive assignment. */
    if (regular_level_mentions_instantiation_parameter(
            instantiated_left, instantiation)) {
        if (reason_out)
            *reason_out =
                "level-instantiation-lower-bound-mentions-solved-parameter";
        return CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS;
    }
    if (!regular_level_raise_instantiation_parameters(
            arena, right_level_syntax, instantiated_left,
            instantiation, budget)) {
        if (reason_out) *reason_out = "level-instantiation-budget";
        return budget && budget->limited && budget->remaining == 0u
            ? CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED
            : CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE;
    }
    complete = true;
    instantiated_right = regular_level_apply_instantiation(
        arena, right_level_syntax, instantiation, budget, &complete);
    if (!complete || !instantiated_right) {
        if (reason_out) *reason_out = "level-instantiation-budget";
        return CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED;
    }
    status = regular_decode_sort(
        arena, atom_expr2(
            arena, atom_symbol(arena, "Sort"), instantiated_right),
        budget, &right_level, reason_out);
    if (status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) return status;
    *le_out = cetta_prime_level_le_v1(left_level, right_level);
    return CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED;
}

static CettaPrimeRegularKernelStatus regular_check_term(
    Arena *arena, Atom *context, Atom *term, Atom *expected,
    CettaPrimeRegularKernelBudget *budget, const char **reason_out,
    PrimeRegularLevelInstantiation *instantiation) {
    if (!regular_spend(budget)) {
        if (reason_out) *reason_out = "checking-budget";
        return CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED;
    }
    if (regular_expr(term, "Lam", 2u)) {
        PrimeRegularKernelNormal expected_normal = regular_normalize(
            arena, expected, budget);
        if (expected_normal.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
            if (reason_out) *reason_out = expected_normal.reason;
            return expected_normal.status;
        }
        if (!regular_expr(expected_normal.term, "Pi", 3u)) {
            if (reason_out) *reason_out = "lambda-needs-function-type";
            return CETTA_PRIME_REGULAR_KERNEL_REFUTED;
        }
        Atom *extended = regular_context_extend(
            arena, context, expected_normal.term->expr.elems[1]);
        return regular_check_term(
            arena, extended, term->expr.elems[1],
            expected_normal.term->expr.elems[2], budget, reason_out,
            instantiation);
    }
    if (regular_expr(term, "Lam", 3u)) {
        CettaPrimeRegularKernelStatus annotation_status = regular_ordinary_type(
            arena, context, term->expr.elems[1], budget, reason_out,
            instantiation);
        if (annotation_status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
            return annotation_status;
        PrimeRegularKernelNormal expected_normal = regular_normalize(
            arena, expected, budget);
        if (expected_normal.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
            if (reason_out) *reason_out = expected_normal.reason;
            return expected_normal.status;
        }
        if (!regular_expr(expected_normal.term, "Pi", 3u)) {
            if (reason_out) *reason_out = "lambda-needs-function-type";
            return CETTA_PRIME_REGULAR_KERNEL_REFUTED;
        }
        bool domains_equal = false;
        CettaPrimeRegularKernelStatus converted = regular_convert_terms(
            arena, term->expr.elems[1],
            expected_normal.term->expr.elems[1], budget,
            &domains_equal, reason_out, instantiation);
        if (converted != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) return converted;
        if (!domains_equal) {
            if (reason_out) *reason_out = "lambda-domain-mismatch";
            return CETTA_PRIME_REGULAR_KERNEL_REFUTED;
        }
        Atom *extended = regular_context_extend(
            arena, context, expected_normal.term->expr.elems[1]);
        return regular_check_term(
            arena, extended, term->expr.elems[2],
            expected_normal.term->expr.elems[2], budget, reason_out,
            instantiation);
    }
    if (regular_expr(term, "Pair", 3u)) {
        PrimeRegularKernelNormal expected_normal = regular_normalize(
            arena, expected, budget);
        if (expected_normal.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
            if (reason_out) *reason_out = expected_normal.reason;
            return expected_normal.status;
        }
        if (!regular_expr(expected_normal.term, "Sigma", 3u)) {
            if (reason_out) *reason_out = "pair-needs-pair-type";
            return CETTA_PRIME_REGULAR_KERNEL_REFUTED;
        }
        Atom *domain = expected_normal.term->expr.elems[1];
        Atom *codomain = expected_normal.term->expr.elems[2];
        CettaPrimeRegularKernelStatus first_status = regular_check_term(
            arena, context, term->expr.elems[1], domain, budget, reason_out,
            instantiation);
        if (first_status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
            return first_status;
        bool ok = true;
        Atom *second_expected = regular_substitute_zero(
            arena, codomain, term->expr.elems[1], &ok, budget);
        if (!ok || !second_expected) {
            if (reason_out) *reason_out = "pair-codomain-substitution-failed";
            return CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE;
        }
        return regular_check_term(
            arena, context, term->expr.elems[2], second_expected,
            budget, reason_out, instantiation);
    }
    PrimeRegularKernelInfer inferred = regular_infer(
        arena, context, term, budget, instantiation);
    if (inferred.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
        if (reason_out) *reason_out = inferred.reason;
        return inferred.status;
    }
    bool equal = false;
    CettaPrimeRegularKernelStatus converted = regular_convert_terms(
        arena, inferred.type, expected, budget, &equal, reason_out,
        instantiation);
    bool inferred_is_sort = regular_symbol(inferred.type, "U1") ||
        regular_expr(inferred.type, "Sort", 2u);
    bool expected_is_sort = regular_symbol(expected, "U1") ||
        regular_expr(expected, "Sort", 2u);
    if (converted != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
        if (converted != CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS ||
            !inferred_is_sort || !expected_is_sort || !instantiation)
            return converted;
        equal = false;
    }
    if (!equal) {
        if (inferred_is_sort && expected_is_sort) {
            bool cumulative = false;
            CettaPrimeRegularKernelStatus ordered = regular_sort_le(
                arena, inferred.type, expected, budget,
                &cumulative, reason_out, instantiation);
            if (ordered != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
                return ordered;
            if (cumulative)
                return CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED;
        }
        if (reason_out) *reason_out = "type-mismatch";
        return CETTA_PRIME_REGULAR_KERNEL_REFUTED;
    }
    return CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED;
}

static PrimeRegularKernelInfer regular_infer(
    Arena *arena, Atom *context, Atom *term,
    CettaPrimeRegularKernelBudget *budget,
    PrimeRegularLevelInstantiation *instantiation) {
    if (!regular_spend(budget))
        return regular_infer_result(
            CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED, NULL, false,
            "synthesis-budget");
    if (!term || !regular_intrinsic_term_shape(term))
        return regular_infer_result(
            CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS, NULL, false,
            "outside-regular-kernel-term");
    if (regular_symbol(term, "U0"))
        return regular_infer_result(
            CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
            atom_symbol(arena, "U1"), true, NULL);
    if (regular_symbol(term, "U1") || regular_expr(term, "Sort", 2u)) {
        bool complete = true;
        if (!regular_sort_syntax(term, budget, &complete))
            return regular_infer_result(
                complete ? CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS
                         : CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED,
                NULL, false,
                complete ? "invalid-universe-sort"
                         : "level-normalization-budget");
        Atom *successor = regular_sort_successor(arena, term);
        return successor
            ? regular_infer_result(
                  CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
                  successor, true, NULL)
            : regular_infer_result(
                  CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE,
                NULL, false, "universe-successor-construction-failed");
    }
    if (regular_decl_const_shape(term, NULL, NULL)) {
        bool complete = true;
        Atom *type = regular_context_lookup_declaration(
            arena, context, term, budget, &complete);
        return type
            ? regular_infer_result(
                  CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED, type,
                  regular_symbol(type, "U1") ||
                      regular_expr(type, "Sort", 2u),
                  NULL)
            : regular_infer_result(
                  complete ? CETTA_PRIME_REGULAR_KERNEL_REFUTED
                           : CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED,
                  NULL, false,
                  complete ? "undeclared-constant" : "synthesis-budget");
    }
    uint64_t index = 0u;
    if (regular_index(term, &index)) {
        bool complete = true;
        Atom *type = regular_context_lookup(
            arena, context, index, budget, &complete);
        return type
            ? regular_infer_result(
                  CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED, type,
                  regular_symbol(type, "U1") ||
                      regular_expr(type, "Sort", 2u),
                  NULL)
            : regular_infer_result(
                  complete ? CETTA_PRIME_REGULAR_KERNEL_REFUTED
                           : CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED,
                  NULL, false,
                  complete ? "loose-index" : "synthesis-budget");
    }
    if (regular_expr(term, "Pi", 3u) || regular_expr(term, "Sigma", 3u)) {
        const char *reason = NULL;
        Atom *domain_sort = NULL;
        CettaPrimeRegularKernelStatus domain_status = regular_type_sort(
            arena, context, term->expr.elems[1], budget,
            &domain_sort, &reason, instantiation);
        if (domain_status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
            return regular_infer_result(domain_status, NULL, false, reason);
        Atom *extended = regular_context_extend(
            arena, context, term->expr.elems[1]);
        Atom *body_sort = NULL;
        CettaPrimeRegularKernelStatus body_status = regular_type_sort(
            arena, extended, term->expr.elems[2], budget,
            &body_sort, &reason, instantiation);
        if (body_status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
            return regular_infer_result(body_status, NULL, false, reason);
        Atom *joined_sort = regular_sort_maximum(
            arena, domain_sort, body_sort);
        if (!joined_sort)
            return regular_infer_result(
                CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE,
                NULL, false, "universe-join-construction-failed");
        return regular_infer_result(
            CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
            joined_sort, true, NULL);
    }
    if (regular_expr(term, "Lam", 2u))
        return regular_infer_result(
            CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS, NULL, false,
            "cannot-synthesize-lambda");
    if (regular_expr(term, "Lam", 3u)) {
        const char *reason = NULL;
        CettaPrimeRegularKernelStatus domain_status = regular_ordinary_type(
            arena, context, term->expr.elems[1], budget, &reason,
            instantiation);
        if (domain_status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
            return regular_infer_result(domain_status, NULL, false, reason);
        Atom *extended = regular_context_extend(
            arena, context, term->expr.elems[1]);
        PrimeRegularKernelInfer body = regular_infer(
            arena, extended, term->expr.elems[2], budget, instantiation);
        if (body.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) return body;
        return regular_infer_result(
            CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
            atom_expr3(
                arena, atom_symbol(arena, "Pi"),
                term->expr.elems[1], body.type),
            false, NULL);
    }
    if (regular_expr(term, "Id", 4u)) {
        const char *reason = NULL;
        Atom *carrier_sort = NULL;
        CettaPrimeRegularKernelStatus carrier_status = regular_type_sort(
            arena, context, term->expr.elems[1], budget,
            &carrier_sort, &reason, instantiation);
        if (carrier_status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
            return regular_infer_result(
                carrier_status, NULL, false, reason);
        CettaPrimeRegularKernelStatus left_status = regular_check_term(
            arena, context, term->expr.elems[2], term->expr.elems[1],
            budget, &reason, instantiation);
        if (left_status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
            return regular_infer_result(left_status, NULL, false, reason);
        CettaPrimeRegularKernelStatus right_status = regular_check_term(
            arena, context, term->expr.elems[3], term->expr.elems[1],
            budget, &reason, instantiation);
        if (right_status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
            return regular_infer_result(right_status, NULL, false, reason);
        return regular_infer_result(
            CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
            carrier_sort, true, NULL);
    }
    if (regular_expr(term, "Pair", 3u))
        return regular_infer_result(
            CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS, NULL, false,
            "pair-needs-pair-type");
    if (regular_expr(term, "Fst", 2u) || regular_expr(term, "Snd", 2u)) {
        PrimeRegularKernelInfer pair = regular_infer(
            arena, context, term->expr.elems[1], budget, instantiation);
        if (pair.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) return pair;
        PrimeRegularKernelNormal pair_type = regular_normalize(
            arena, pair.type, budget);
        if (pair_type.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
            return regular_infer_result(
                pair_type.status, NULL, false, pair_type.reason);
        if (!regular_expr(pair_type.term, "Sigma", 3u))
            return regular_infer_result(
                CETTA_PRIME_REGULAR_KERNEL_REFUTED, NULL, false,
                "expected-pair-type");
        if (regular_expr(term, "Fst", 2u))
            return regular_infer_result(
                CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
                pair_type.term->expr.elems[1],
                regular_symbol(pair_type.term->expr.elems[1], "U1") ||
                    regular_expr(
                        pair_type.term->expr.elems[1], "Sort", 2u),
                NULL);
        Atom *first_projection = atom_expr2(
            arena, atom_symbol(arena, "Fst"), term->expr.elems[1]);
        bool ok = true;
        Atom *result_type = regular_substitute_zero(
            arena, pair_type.term->expr.elems[2], first_projection,
            &ok, budget);
        if (!ok || !result_type)
            return regular_infer_result(
                CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE, NULL, false,
                "projection-codomain-substitution-failed");
        return regular_infer_result(
            CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
            result_type,
            regular_symbol(result_type, "U1") ||
                regular_expr(result_type, "Sort", 2u),
            NULL);
    }
    if (regular_expr(term, "Refl", 2u)) {
        PrimeRegularKernelInfer reflected = regular_infer(
            arena, context, term->expr.elems[1], budget, instantiation);
        if (reflected.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
            return reflected;
        return regular_infer_result(
            CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
            regular_expr4(
                arena, atom_symbol(arena, "Id"), reflected.type,
                term->expr.elems[1], term->expr.elems[1]),
            false, NULL);
    }
    if (regular_expr(term, "App", 3u)) {
        PrimeRegularKernelInfer function = regular_infer(
            arena, context, term->expr.elems[1], budget, instantiation);
        if (function.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
            return function;
        if (function.type_is_sort)
            return regular_infer_result(
                CETTA_PRIME_REGULAR_KERNEL_REFUTED, NULL, false,
                "application-function-has-upper-sort");
        PrimeRegularKernelNormal function_type = regular_normalize(
            arena, function.type, budget);
        if (function_type.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
            return regular_infer_result(
                function_type.status, NULL, false, function_type.reason);
        if (!regular_expr(function_type.term, "Pi", 3u))
            return regular_infer_result(
                CETTA_PRIME_REGULAR_KERNEL_REFUTED, NULL, false,
                "expected-function-type");
        const char *reason = NULL;
        CettaPrimeRegularKernelStatus argument_status = regular_check_term(
            arena, context, term->expr.elems[2],
            function_type.term->expr.elems[1], budget, &reason,
            instantiation);
        if (argument_status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
            return regular_infer_result(argument_status, NULL, false, reason);
        bool ok = true;
        Atom *result_type = regular_substitute_zero(
            arena, function_type.term->expr.elems[2],
            term->expr.elems[2], &ok, budget);
        if (!ok || !result_type)
            return regular_infer_result(
                CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE, NULL, false,
                "result-substitution-failed");
        return regular_infer_result(
            CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED, result_type,
            regular_symbol(result_type, "U1") ||
                regular_expr(result_type, "Sort", 2u),
            NULL);
    }
    return regular_infer_result(
        CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE, NULL, false,
        "regular-inference-missing-constructor");
}

static CettaPrimeRegularKernelStatus regular_context_valid(
    Arena *arena, Atom *context, CettaPrimeRegularKernelBudget *budget,
    uint64_t *length_out, const char **reason_out,
    PrimeRegularLevelInstantiation *instantiation) {
    if (!regular_spend(budget)) {
        if (reason_out) *reason_out = "context-budget";
        return CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED;
    }
    if (regular_symbol(context, "PrimeCtxNil")) {
        if (length_out) *length_out = 0u;
        return CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED;
    }
    bool local_entry = regular_expr(context, "PrimeCtxCons", 3u);
    bool declaration_entry = regular_expr(context, "PrimeCtxDecl", 4u);
    if (!local_entry && !declaration_entry) {
        if (reason_out) *reason_out = "outside-regular-context-syntax";
        return CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS;
    }
    Atom *key = declaration_entry ? context->expr.elems[1] : NULL;
    Atom *domain = declaration_entry
        ? context->expr.elems[2] : context->expr.elems[1];
    Atom *tail = declaration_entry
        ? context->expr.elems[3] : context->expr.elems[2];
    uint64_t tail_length = 0u;
    CettaPrimeRegularKernelStatus tail_status = regular_context_valid(
        arena, tail, budget, &tail_length, reason_out, instantiation);
    if (tail_status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) return tail_status;
    if (local_entry && tail_length == UINT64_MAX) {
        if (reason_out) *reason_out = "regular-context-too-deep";
        return CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS;
    }
    if (declaration_entry) {
        bool key_complete = true;
        if (!regular_decl_const_schema_key(
                key, budget, &key_complete)) {
            if (reason_out)
                *reason_out = key_complete
                    ? "outside-declaration-constant-key"
                    : "context-budget";
            return key_complete
                ? CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS
                : CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED;
        }
        bool duplicate_complete = true;
        if (regular_context_contains_decl_key(
                tail, key, budget, &duplicate_complete)) {
            if (reason_out) *reason_out = "duplicate-declaration-constant";
            return CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS;
        }
        if (!duplicate_complete) {
            if (reason_out) *reason_out = "context-budget";
            return CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED;
        }
    }
    bool scope_complete = true;
    if (!regular_scope_check(domain, tail_length, budget, &scope_complete)) {
        if (reason_out)
            *reason_out = scope_complete
                ? "outside-regular-context-domain" : "context-budget";
        return scope_complete
            ? CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS
            : CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED;
    }
    CettaPrimeRegularKernelStatus domain_status = regular_ordinary_type(
        arena, tail, domain, budget, reason_out, instantiation);
    if (domain_status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED && length_out)
        *length_out = local_entry ? tail_length + 1u : tail_length;
    return domain_status;
}

struct CettaPrimeRegularKernelPreparedExpectedV1 {
    Atom *context;
    Atom *expected;
    uint64_t context_length;
};

CettaPrimeRegularKernelPreparedExpectedResultV1
cetta_prime_regular_kernel_prepare_intrinsic_expected_v1(
    Arena *arena, Atom *context, Atom *expected,
    CettaPrimeRegularKernelBudget *budget) {
    if (!arena || !context || !expected || !budget)
        return (CettaPrimeRegularKernelPreparedExpectedResultV1){
            .status = CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE,
            .reason = "invalid-intrinsic-expected-input",
        };
    uint64_t length = 0u;
    const char *reason = NULL;
    CettaPrimeRegularKernelStatus context_status = regular_context_valid(
        arena, context, budget, &length, &reason, NULL);
    if (context_status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
        return (CettaPrimeRegularKernelPreparedExpectedResultV1){
            .status = context_status,
            .reason = reason,
        };
    bool complete = true;
    if (!regular_intrinsic_scope_check(expected, length, budget, &complete))
        return (CettaPrimeRegularKernelPreparedExpectedResultV1){
            .status = complete ? CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS
                               : CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED,
            .reason = complete ? "outside-intrinsic-expected"
                               : "intrinsic-scope-budget",
        };
    CettaPrimeRegularKernelStatus formed = regular_ordinary_type(
        arena, context, expected, budget, &reason, NULL);
    if (formed != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
        return (CettaPrimeRegularKernelPreparedExpectedResultV1){
            .status = formed,
            .reason = reason,
        };
    CettaPrimeRegularKernelPreparedExpectedV1 *prepared = arena_alloc(
        arena, sizeof(*prepared));
    prepared->context = context;
    prepared->expected = expected;
    prepared->context_length = length;
    return (CettaPrimeRegularKernelPreparedExpectedResultV1){
        .status = CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED,
        .prepared = prepared,
    };
}

CettaPrimeRegularKernelResult
cetta_prime_regular_kernel_check_prepared_intrinsic_v1(
    Arena *arena, const CettaPrimeRegularKernelPreparedExpectedV1 *prepared,
    Atom *term, CettaPrimeRegularKernelBudget *budget) {
    if (!arena || !prepared || !term || !budget)
        return regular_result(
            CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE, NULL,
            "invalid-prepared-intrinsic-input");
    bool complete = true;
    if (!regular_intrinsic_scope_check(
            term, prepared->context_length, budget, &complete))
        return regular_result(
            complete ? CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS
                     : CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED,
            NULL, complete ? "outside-intrinsic-term"
                           : "intrinsic-scope-budget");
    const char *reason = NULL;
    CettaPrimeRegularKernelStatus checked = regular_check_term(
        arena, prepared->context, term, prepared->expected, budget, &reason,
        NULL);
    return regular_result(
        checked,
        checked == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED
            ? prepared->expected : NULL,
        reason);
}

CettaPrimeRegularKernelResult cetta_prime_regular_kernel_check_intrinsic(
    Arena *arena, Atom *context, Atom *term, Atom *expected,
    CettaPrimeRegularKernelBudget *budget) {
    if (!arena || !context || !term || !expected || !budget)
        return regular_result(
            CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE, NULL,
            "invalid-intrinsic-check-input");

    CettaPrimeRegularKernelPreparedExpectedResultV1 prepared =
        cetta_prime_regular_kernel_prepare_intrinsic_expected_v1(
            arena, context, expected, budget);
    if (prepared.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
        return regular_result(prepared.status, NULL, prepared.reason);
    return cetta_prime_regular_kernel_check_prepared_intrinsic_v1(
        arena, prepared.prepared, term, budget);
}

CettaPrimeRegularKernelResult cetta_prime_regular_kernel_synth_intrinsic_v1(
    Arena *arena, Atom *context, Atom *term,
    CettaPrimeRegularKernelBudget *budget) {
    if (!arena || !context || !term || !budget)
        return regular_result(
            CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE, NULL,
            "invalid-intrinsic-synthesis-input");
    uint64_t context_length = 0u;
    const char *reason = NULL;
    CettaPrimeRegularKernelStatus context_status = regular_context_valid(
        arena, context, budget, &context_length, &reason, NULL);
    if (context_status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
        return regular_result(context_status, NULL, reason);
    bool complete = true;
    if (!regular_intrinsic_scope_check(
            term, context_length, budget, &complete))
        return regular_result(
            complete ? CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS
                     : CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED,
            NULL, complete ? "outside-intrinsic-term"
                           : "intrinsic-scope-budget");
    PrimeRegularKernelInfer inferred = regular_infer(
        arena, context, term, budget, NULL);
    return regular_result(
        inferred.status, inferred.type, inferred.reason);
}

static CettaPrimeRegularKernelResult
regular_level_instantiation_result(
    CettaPrimeRegularKernelStatus status, const char *reason) {
    return regular_result(status, NULL, reason);
}

static CettaPrimeRegularKernelFormedSchemaV1 regular_formed_schema_result(
    CettaPrimeRegularKernelStatus status, Atom *term,
    const char *reason) {
    return (CettaPrimeRegularKernelFormedSchemaV1){
        .status = status,
        .term = term,
        .reason = reason,
    };
}

static CettaPrimeRegularKernelResult
regular_level_instantiation_substitution_failure(
    const CettaPrimeRegularKernelBudget *budget) {
    return regular_level_instantiation_result(
        budget && budget->limited && budget->remaining == 0u
            ? CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED
            : CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE,
        budget && budget->limited && budget->remaining == 0u
            ? "level-instantiation-budget"
            : "level-instantiation-substitution-failed");
}

CettaPrimeRegularKernelResult
cetta_prime_regular_kernel_synth_intrinsic_instantiating_levels_v1(
    Arena *arena, Atom *context, Atom *term,
    const uint64_t *parameters, size_t parameter_count,
    CettaPrimeRegularKernelBudget *budget) {
    if (!arena || !context || !term || !budget)
        return regular_level_instantiation_result(
            CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE,
            "invalid-level-instantiating-synthesis-input");
    if (parameter_count == 0u)
        return cetta_prime_regular_kernel_synth_intrinsic_v1(
            arena, context, term, budget);

    PrimeRegularLevelInstantiation instantiation;
    if (!regular_level_instantiation_init(
            arena, parameters, parameter_count, &instantiation))
        return regular_level_instantiation_result(
            CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE,
            "invalid-level-instantiation-parameters");

    uint64_t context_length = 0u;
    const char *reason = NULL;
    CettaPrimeRegularKernelStatus context_status = regular_context_valid(
        arena, context, budget, &context_length, &reason, &instantiation);
    if (context_status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
        return regular_level_instantiation_result(context_status, reason);
    bool complete = true;
    if (!regular_intrinsic_scope_check(
            term, context_length, budget, &complete))
        return regular_level_instantiation_result(
            complete ? CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS
                     : CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED,
            complete ? "outside-intrinsic-term"
                     : "intrinsic-scope-budget");
    PrimeRegularKernelInfer proposed = regular_infer(
        arena, context, term, budget, &instantiation);
    if (proposed.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
        return regular_result(
            proposed.status, proposed.type, proposed.reason);
    if (!regular_level_instantiation_default_zero(arena, &instantiation))
        return regular_level_instantiation_result(
            CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE,
            "level-instantiation-default-failed");

    complete = true;
    Atom *instantiated_context = regular_term_apply_level_instantiation(
        arena, context, &instantiation, budget, &complete);
    instantiated_context = complete && instantiated_context
        ? regular_context_deduplicate_declarations(
              arena, instantiated_context, budget, &complete)
        : NULL;
    Atom *instantiated_term = complete
        ? regular_term_apply_level_instantiation(
              arena, term, &instantiation, budget, &complete)
        : NULL;
    if (!complete || !instantiated_context || !instantiated_term)
        return regular_level_instantiation_substitution_failure(budget);
    return cetta_prime_regular_kernel_synth_intrinsic_v1(
        arena, instantiated_context, instantiated_term, budget);
}

CettaPrimeRegularKernelResult
cetta_prime_regular_kernel_check_intrinsic_instantiating_levels_v1(
    Arena *arena, Atom *context, Atom *term, Atom *expected,
    const uint64_t *parameters, size_t parameter_count,
    CettaPrimeRegularKernelBudget *budget) {
    if (!arena || !context || !term || !expected || !budget)
        return regular_level_instantiation_result(
            CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE,
            "invalid-level-instantiating-check-input");
    if (parameter_count == 0u)
        return cetta_prime_regular_kernel_check_intrinsic(
            arena, context, term, expected, budget);

    PrimeRegularLevelInstantiation instantiation;
    if (!regular_level_instantiation_init(
            arena, parameters, parameter_count, &instantiation))
        return regular_level_instantiation_result(
            CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE,
            "invalid-level-instantiation-parameters");
    uint64_t context_length = 0u;
    const char *reason = NULL;
    CettaPrimeRegularKernelStatus status = regular_context_valid(
        arena, context, budget, &context_length, &reason, &instantiation);
    if (status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
        return regular_level_instantiation_result(status, reason);
    bool complete = true;
    if (!regular_intrinsic_scope_check(
            expected, context_length, budget, &complete) ||
        !regular_intrinsic_scope_check(
            term, context_length, budget, &complete))
        return regular_level_instantiation_result(
            complete ? CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS
                     : CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED,
            complete ? "outside-intrinsic-check"
                     : "intrinsic-scope-budget");
    status = regular_ordinary_type(
        arena, context, expected, budget, &reason, &instantiation);
    if (status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
        status = regular_check_term(
            arena, context, term, expected, budget, &reason,
            &instantiation);
    if (status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
        return regular_level_instantiation_result(status, reason);
    if (!regular_level_instantiation_default_zero(arena, &instantiation))
        return regular_level_instantiation_result(
            CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE,
            "level-instantiation-default-failed");

    complete = true;
    Atom *instantiated_context = regular_term_apply_level_instantiation(
        arena, context, &instantiation, budget, &complete);
    instantiated_context = complete && instantiated_context
        ? regular_context_deduplicate_declarations(
              arena, instantiated_context, budget, &complete)
        : NULL;
    Atom *instantiated_term = complete
        ? regular_term_apply_level_instantiation(
              arena, term, &instantiation, budget, &complete)
        : NULL;
    Atom *instantiated_expected = complete
        ? regular_term_apply_level_instantiation(
              arena, expected, &instantiation, budget, &complete)
        : NULL;
    if (!complete || !instantiated_context || !instantiated_term ||
        !instantiated_expected)
        return regular_level_instantiation_substitution_failure(budget);
    return cetta_prime_regular_kernel_check_intrinsic(
        arena, instantiated_context, instantiated_term,
        instantiated_expected, budget);
}

CettaPrimeRegularKernelFormedSchemaV1
cetta_prime_regular_kernel_form_intrinsic_level_schema_v1(
    Arena *arena, Atom *context, Atom *expected,
    const uint64_t *parameters, size_t parameter_count,
    CettaPrimeRegularKernelBudget *budget) {
    if (!arena || !context || !expected || !budget)
        return regular_formed_schema_result(
            CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE,
            NULL,
            "invalid-level-instantiating-formation-input");

    PrimeRegularLevelInstantiation storage = {0};
    PrimeRegularLevelInstantiation *instantiation = NULL;
    if (parameter_count != 0u) {
        if (!regular_level_instantiation_init(
                arena, parameters, parameter_count, &storage))
            return regular_formed_schema_result(
                CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE, NULL,
                "invalid-level-instantiation-parameters");
        instantiation = &storage;
    }
    uint64_t context_length = 0u;
    const char *reason = NULL;
    CettaPrimeRegularKernelStatus status = regular_context_valid(
        arena, context, budget, &context_length, &reason, instantiation);
    if (status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
        return regular_formed_schema_result(status, NULL, reason);
    bool complete = true;
    if (!regular_intrinsic_scope_check(
            expected, context_length, budget, &complete))
        return regular_formed_schema_result(
            complete ? CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS
                     : CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED,
            NULL,
            complete ? "outside-intrinsic-expected"
                     : "intrinsic-scope-budget");
    status = regular_ordinary_type(
        arena, context, expected, budget, &reason, instantiation);
    if (status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
        return regular_formed_schema_result(status, NULL, reason);
    if (instantiation &&
        !regular_level_instantiation_default_zero(arena, instantiation))
        return regular_formed_schema_result(
            CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE,
            NULL,
            "level-instantiation-default-failed");

    Atom *instantiated_context = context;
    Atom *instantiated_expected = expected;
    if (instantiation) {
        complete = true;
        instantiated_context = regular_term_apply_level_instantiation(
            arena, context, instantiation, budget, &complete);
        instantiated_context = complete && instantiated_context
            ? regular_context_deduplicate_declarations(
                  arena, instantiated_context, budget, &complete)
            : NULL;
        instantiated_expected = complete
            ? regular_term_apply_level_instantiation(
                  arena, expected, instantiation, budget, &complete)
            : NULL;
    }
    if (!complete || !instantiated_context || !instantiated_expected)
        return regular_formed_schema_result(
            budget && budget->limited && budget->remaining == 0u
                ? CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED
                : CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE,
            NULL,
            budget && budget->limited && budget->remaining == 0u
                ? "level-instantiation-budget"
                : "level-instantiation-substitution-failed");
    CettaPrimeRegularKernelPreparedExpectedResultV1 replay =
        cetta_prime_regular_kernel_prepare_intrinsic_expected_v1(
            arena, instantiated_context, instantiated_expected, budget);
    return regular_formed_schema_result(
        replay.status,
        replay.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED
            ? instantiated_expected : NULL,
        replay.reason);
}

CettaPrimeRegularKernelResult
cetta_prime_regular_kernel_form_intrinsic_instantiating_levels_v1(
    Arena *arena, Atom *context, Atom *expected,
    const uint64_t *parameters, size_t parameter_count,
    CettaPrimeRegularKernelBudget *budget) {
    CettaPrimeRegularKernelFormedSchemaV1 formed =
        cetta_prime_regular_kernel_form_intrinsic_level_schema_v1(
            arena, context, expected, parameters, parameter_count,
            budget);
    return regular_level_instantiation_result(formed.status, formed.reason);
}

static CettaPrimeRegularKernelStatus regular_prepare_scoped(
    Arena *arena, Atom *scoped, Atom **context_out, Atom **term_out,
    uint64_t *length_out, CettaPrimeRegularKernelBudget *budget,
    const char **reason_out) {
    uint64_t length = 0u;
    Atom *context = NULL;
    Atom *term = NULL;
    CettaPrimeRegularKernelStatus syntax_status = regular_classify_scoped_syntax(
        scoped, NULL, &context, &term, &length, budget, reason_out);
    if (syntax_status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
        return syntax_status;

    uint64_t semantic_length = 0u;
    CettaPrimeRegularKernelStatus context_status = regular_context_valid(
        arena, context, budget, &semantic_length, reason_out, NULL);
    if (context_status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
        return context_status;
    if (semantic_length != length) {
        if (reason_out) *reason_out = "context-length-engine-mismatch";
        return CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE;
    }
    if (context_out) *context_out = context;
    if (term_out) *term_out = term;
    if (length_out) *length_out = length;
    return CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED;
}

CettaPrimeRegularKernelResult cetta_prime_regular_kernel_synth(
    Arena *arena, Atom *scoped, CettaPrimeRegularKernelBudget *budget) {
    Atom *context = NULL;
    Atom *term = NULL;
    const char *reason = NULL;
    CettaPrimeRegularKernelStatus prepared = regular_prepare_scoped(
        arena, scoped, &context, &term, NULL, budget, &reason);
    if (prepared != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
        return regular_result(prepared, NULL, reason);
    PrimeRegularKernelInfer inferred = regular_infer(
        arena, context, term, budget, NULL);
    return regular_result(inferred.status, inferred.type, inferred.reason);
}

CettaPrimeRegularKernelResult cetta_prime_regular_kernel_check(
    Arena *arena, Atom *scoped, Atom *expected,
    CettaPrimeRegularKernelBudget *budget) {
    Atom *context = NULL;
    Atom *term = NULL;
    uint64_t length = 0u;
    const char *reason = NULL;
    CettaPrimeRegularKernelStatus syntax_status = regular_classify_scoped_syntax(
        scoped, expected, &context, &term, &length, budget, &reason);
    if (syntax_status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
        return regular_result(syntax_status, NULL, reason);
    uint64_t semantic_length = 0u;
    CettaPrimeRegularKernelStatus context_status = regular_context_valid(
        arena, context, budget, &semantic_length, &reason, NULL);
    if (context_status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
        return regular_result(context_status, NULL, reason);
    if (semantic_length != length) {
        return regular_result(
            CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE, NULL,
            "context-length-engine-mismatch");
    }
    CettaPrimeRegularKernelStatus formed = regular_ordinary_type(
        arena, context, expected, budget, &reason, NULL);
    if (formed != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
        return regular_result(formed, NULL, reason);
    CettaPrimeRegularKernelStatus checked = regular_check_term(
        arena, context, term, expected, budget, &reason, NULL);
    return regular_result(checked, checked == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED
        ? expected : NULL, reason);
}

static CettaPrimeRegularKernelConversionDecision
regular_decide_prepared_conversion(
    Arena *arena, Atom *left_context, Atom *left,
    Atom *right_context, Atom *right,
    CettaPrimeRegularKernelBudget *budget,
    PrimeRegularLevelInstantiation *instantiation) {
    const char *reason = NULL;
    if (!atom_eq(left_context, right_context))
        return (CettaPrimeRegularKernelConversionDecision){
            .status = CETTA_PRIME_REGULAR_KERNEL_REFUTED,
            .operands_admitted = true,
            .reason = "conversion-context-mismatch",
        };

    Atom *left_inferred_type = NULL;
    Atom *right_inferred_type = NULL;
    PrimeRegularKernelInfer left_type = regular_infer(
        arena, left_context, left, budget, instantiation);
    if (left_type.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
        return (CettaPrimeRegularKernelConversionDecision){
            .status = left_type.status,
            .reason = left_type.reason,
        };
    PrimeRegularKernelInfer right_type = regular_infer(
        arena, right_context, right, budget, instantiation);
    if (right_type.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
        return (CettaPrimeRegularKernelConversionDecision){
            .status = right_type.status,
            .left_type = left_type.type,
            .reason = right_type.reason,
        };
    left_inferred_type = left_type.type;
    right_inferred_type = right_type.type;
    bool types_equal = false;
    CettaPrimeRegularKernelStatus type_conversion = regular_convert_terms(
        arena, left_type.type, right_type.type, budget,
        &types_equal, &reason, instantiation);
    if (type_conversion != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
        return (CettaPrimeRegularKernelConversionDecision){
            .status = type_conversion,
            .operands_admitted = true,
            .left_type = left_inferred_type,
            .right_type = right_inferred_type,
            .reason = reason,
        };
    if (!types_equal)
        return (CettaPrimeRegularKernelConversionDecision){
            .status = CETTA_PRIME_REGULAR_KERNEL_REFUTED,
            .operands_admitted = true,
            .left_type = left_inferred_type,
            .right_type = right_inferred_type,
            .reason = "conversion-type-mismatch",
        };

    bool equal = false;
    CettaPrimeRegularKernelStatus converted = regular_convert_terms(
        arena, left, right, budget, &equal, &reason, instantiation);
    if (converted != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
        return (CettaPrimeRegularKernelConversionDecision){
            .status = converted,
            .operands_admitted = true,
            .left_type = left_inferred_type,
            .right_type = right_inferred_type,
            .reason = reason,
        };
    return (CettaPrimeRegularKernelConversionDecision){
        .status = equal ? CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED
                        : CETTA_PRIME_REGULAR_KERNEL_REFUTED,
        .operands_admitted = true,
        .equal = equal,
        .left_type = left_inferred_type,
        .right_type = right_inferred_type,
        .reason = equal ? NULL : "distinct-normal-forms",
    };
}

CettaPrimeRegularKernelConversionDecision
cetta_prime_regular_kernel_decide_conversion(
    Arena *arena, Atom *left_scoped, Atom *right_scoped,
    CettaPrimeRegularKernelBudget *budget) {
    Atom *left_context = NULL;
    Atom *left = NULL;
    Atom *right_context = NULL;
    Atom *right = NULL;
    const char *reason = NULL;
    CettaPrimeRegularKernelStatus left_prepared = regular_prepare_scoped(
        arena, left_scoped, &left_context, &left, NULL, budget, &reason);
    if (left_prepared != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
        return (CettaPrimeRegularKernelConversionDecision){
            .status = left_prepared,
            .reason = reason,
        };
    CettaPrimeRegularKernelStatus right_prepared = regular_prepare_scoped(
        arena, right_scoped, &right_context, &right, NULL, budget, &reason);
    if (right_prepared != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
        return (CettaPrimeRegularKernelConversionDecision){
            .status = right_prepared,
            .reason = reason,
        };
    return regular_decide_prepared_conversion(
        arena, left_context, left, right_context, right, budget, NULL);
}

CettaPrimeRegularKernelConversionDecision
cetta_prime_regular_kernel_decide_intrinsic_conversion_v1(
    Arena *arena, Atom *context, Atom *left, Atom *right,
    CettaPrimeRegularKernelBudget *budget) {
    if (!arena || !context || !left || !right || !budget)
        return (CettaPrimeRegularKernelConversionDecision){
            .status = CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE,
            .reason = "invalid-intrinsic-conversion-input",
        };
    uint64_t context_length = 0u;
    const char *reason = NULL;
    CettaPrimeRegularKernelStatus context_status = regular_context_valid(
        arena, context, budget, &context_length, &reason, NULL);
    if (context_status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
        return (CettaPrimeRegularKernelConversionDecision){
            .status = context_status,
            .reason = reason,
        };
    bool left_complete = true;
    if (!regular_intrinsic_scope_check(
            left, context_length, budget, &left_complete))
        return (CettaPrimeRegularKernelConversionDecision){
            .status = left_complete
                ? CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS
                : CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED,
            .reason = left_complete ? "outside-intrinsic-left"
                                    : "intrinsic-conversion-budget",
        };
    bool right_complete = true;
    if (!regular_intrinsic_scope_check(
            right, context_length, budget, &right_complete))
        return (CettaPrimeRegularKernelConversionDecision){
            .status = right_complete
                ? CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS
                : CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED,
            .reason = right_complete ? "outside-intrinsic-right"
                                     : "intrinsic-conversion-budget",
        };
    return regular_decide_prepared_conversion(
        arena, context, left, context, right, budget, NULL);
}

CettaPrimeRegularKernelConversionDecision
cetta_prime_regular_kernel_decide_intrinsic_conversion_instantiating_levels_v1(
    Arena *arena, Atom *context, Atom *left, Atom *right,
    const uint64_t *parameters, size_t parameter_count,
    CettaPrimeRegularKernelBudget *budget) {
    if (!arena || !context || !left || !right || !budget)
        return (CettaPrimeRegularKernelConversionDecision){
            .status = CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE,
            .reason = "invalid-level-instantiating-conversion-input",
        };
    if (parameter_count == 0u)
        return cetta_prime_regular_kernel_decide_intrinsic_conversion_v1(
            arena, context, left, right, budget);

    PrimeRegularLevelInstantiation instantiation;
    if (!regular_level_instantiation_init(
            arena, parameters, parameter_count, &instantiation))
        return (CettaPrimeRegularKernelConversionDecision){
            .status = CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE,
            .reason = "invalid-level-instantiation-parameters",
        };
    uint64_t context_length = 0u;
    const char *reason = NULL;
    CettaPrimeRegularKernelStatus context_status = regular_context_valid(
        arena, context, budget, &context_length, &reason, &instantiation);
    if (context_status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
        return (CettaPrimeRegularKernelConversionDecision){
            .status = context_status,
            .reason = reason,
        };
    bool complete = true;
    if (!regular_intrinsic_scope_check(
            left, context_length, budget, &complete) ||
        !regular_intrinsic_scope_check(
            right, context_length, budget, &complete))
        return (CettaPrimeRegularKernelConversionDecision){
            .status = complete
                ? CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS
                : CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED,
            .reason = complete ? "outside-intrinsic-conversion"
                               : "intrinsic-conversion-budget",
        };

    CettaPrimeRegularKernelConversionDecision proposed =
        regular_decide_prepared_conversion(
            arena, context, left, context, right, budget, &instantiation);
    if (proposed.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED &&
        proposed.status != CETTA_PRIME_REGULAR_KERNEL_REFUTED)
        return proposed;
    if (!regular_level_instantiation_default_zero(arena, &instantiation))
        return (CettaPrimeRegularKernelConversionDecision){
            .status = CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE,
            .reason = "level-instantiation-default-failed",
        };

    complete = true;
    Atom *instantiated_context = regular_term_apply_level_instantiation(
        arena, context, &instantiation, budget, &complete);
    instantiated_context = complete && instantiated_context
        ? regular_context_deduplicate_declarations(
              arena, instantiated_context, budget, &complete)
        : NULL;
    Atom *instantiated_left = complete
        ? regular_term_apply_level_instantiation(
              arena, left, &instantiation, budget, &complete)
        : NULL;
    Atom *instantiated_right = complete
        ? regular_term_apply_level_instantiation(
              arena, right, &instantiation, budget, &complete)
        : NULL;
    if (!complete || !instantiated_context || !instantiated_left ||
        !instantiated_right)
        return (CettaPrimeRegularKernelConversionDecision){
            .status = budget->limited && budget->remaining == 0u
                ? CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED
                : CETTA_PRIME_REGULAR_KERNEL_ENGINE_FAILURE,
            .reason = budget->limited && budget->remaining == 0u
                ? "level-instantiation-budget"
                : "level-instantiation-substitution-failed",
        };
    return cetta_prime_regular_kernel_decide_intrinsic_conversion_v1(
        arena, instantiated_context, instantiated_left,
        instantiated_right, budget);
}

CettaPrimeRegularKernelResult cetta_prime_regular_kernel_convert(
    Arena *arena, Atom *left_scoped, Atom *right_scoped,
    CettaPrimeRegularKernelBudget *budget) {
    CettaPrimeRegularKernelConversionDecision decision =
        cetta_prime_regular_kernel_decide_conversion(
            arena, left_scoped, right_scoped, budget);
    return regular_result(decision.status, NULL, decision.reason);
}

#include "prime_regular_pattern.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "name_key.h"

typedef enum {
    PATTERN_SCAN_OK = 0,
    PATTERN_SCAN_FOUND,
    PATTERN_SCAN_BUDGET,
    PATTERN_SCAN_INVALID
} PatternScanStatus;

typedef enum {
    PATTERN_LIST_OK = 0,
    PATTERN_LIST_BUDGET,
    PATTERN_LIST_INVALID,
    PATTERN_LIST_RESOURCE
} PatternListStatus;

static bool pattern_spend(CettaPrimeRegularKernelBudget *budget) {
    if (!budget || !budget->limited) return true;
    if (budget->remaining == 0u) return false;
    budget->remaining--;
    if (budget->spent != UINT64_MAX) budget->spent++;
    return true;
}

static bool pattern_tag(const Atom *atom, const char *tag, CettaExprLen len) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len == len &&
           atom_is_symbol(atom->expr.elems[0], tag);
}

static bool pattern_string(const Atom *atom, const char **text_out) {
    if (!atom || atom->kind != ATOM_GROUNDED ||
        atom->ground.gkind != GV_STRING || !atom->ground.sval)
        return false;
    if (text_out) *text_out = atom->ground.sval;
    return true;
}

static bool pattern_natural(const Atom *atom, uint64_t *value_out) {
    if (!atom || atom->kind != ATOM_GROUNDED ||
        atom->ground.gkind != GV_INT || atom->ground.ival < 0)
        return false;
    if (value_out) *value_out = (uint64_t)atom->ground.ival;
    return true;
}

static bool pattern_head_named(const Atom *atom, const char *name) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len != 0u &&
           atom_is_symbol(atom->expr.elems[0], name);
}

/* Declaration occurrences are introduced only after authored syntax has
 * been lowered to Pattern.  They retain the global source name while making
 * each occurrence's fresh universe arguments explicit.  Validate this
 * private wire recursively; it is a literal leaf for binder elaboration. */
static PatternScanStatus pattern_validate_decl_const_level(
    Atom *level, CettaPrimeRegularKernelBudget *budget) {
    if (!pattern_spend(budget)) return PATTERN_SCAN_BUDGET;
    if ((pattern_tag(level, "LevelConst", 2u) ||
         pattern_tag(level, "LevelParam", 2u)) &&
        pattern_natural(level->expr.elems[1], NULL)) {
        return PATTERN_SCAN_OK;
    }
    if (pattern_tag(level, "LevelSucc", 2u))
        return pattern_validate_decl_const_level(
            level->expr.elems[1], budget);
    if (pattern_tag(level, "LevelMax", 3u)) {
        PatternScanStatus left = pattern_validate_decl_const_level(
            level->expr.elems[1], budget);
        return left == PATTERN_SCAN_OK
            ? pattern_validate_decl_const_level(
                  level->expr.elems[2], budget)
            : left;
    }
    return PATTERN_SCAN_INVALID;
}

static PatternScanStatus pattern_validate_decl_const(
    Atom *pattern, CettaPrimeRegularKernelBudget *budget) {
    if (!pattern_head_named(pattern, "DeclConst") ||
        pattern->expr.len < 2u || !pattern->expr.elems[1] ||
        pattern->expr.elems[1]->kind != ATOM_SYMBOL) {
        return PATTERN_SCAN_INVALID;
    }
    for (CettaExprIndex index = 2u; index < pattern->expr.len; index++) {
        PatternScanStatus level = pattern_validate_decl_const_level(
            pattern->expr.elems[index], budget);
        if (level != PATTERN_SCAN_OK) return level;
    }
    return PATTERN_SCAN_OK;
}

Atom *cetta_prime_regular_level_parameter_marker_v1(
    Arena *arena, uint64_t parameter) {
    if (!arena || parameter > (uint64_t)INT64_MAX) return NULL;
    Atom *tag = atom_internal_tag(
        arena, CETTA_INTERNAL_TAG_PRIME_LEVEL_PARAMETER);
    return tag ? atom_expr2(
        arena, tag, atom_int(arena, (int64_t)parameter)) : NULL;
}

static bool regular_level_parameter_marker(
    const Atom *atom, uint64_t *parameter_out) {
    if (!atom || atom->kind != ATOM_EXPR || atom->expr.len != 2u ||
        atom->expr.elems[0]->kind != ATOM_GROUNDED ||
        atom->expr.elems[0]->ground.gkind != GV_INTERNAL_TAG ||
        atom->expr.elems[0]->ground.ival !=
            (int64_t)CETTA_INTERNAL_TAG_PRIME_LEVEL_PARAMETER) {
        return false;
    }
    return pattern_natural(atom->expr.elems[1], parameter_out);
}

static CettaPrimeRegularPatternElaborationV1 pattern_failure(
    CettaPrimeRegularPatternStatusV1 status, const char *reason) {
    return (CettaPrimeRegularPatternElaborationV1){
        .status = status,
        .reason = reason,
    };
}

static CettaPrimeRegularPatternElaborationV1 pattern_syntax_failure(
    CettaPrimeRegularPatternSyntaxErrorV1 error, uint64_t index,
    const char *name, size_t arity) {
    return (CettaPrimeRegularPatternElaborationV1){
        .status = CETTA_PRIME_REGULAR_PATTERN_SYNTAX_ERROR,
        .syntax_error = error,
        .index = index,
        .name = name,
        .arity = arity,
    };
}

static CettaPrimeRegularPatternElaborationV1 pattern_success(Atom *term) {
    return (CettaPrimeRegularPatternElaborationV1){
        .status = CETTA_PRIME_REGULAR_PATTERN_OK,
        .term = term,
    };
}

typedef struct RegularTermBinding RegularTermBinding;
struct RegularTermBinding {
    Atom *key;
    bool referencable;
    bool declaration;
    size_t declaration_index;
    const RegularTermBinding *outer;
};

typedef enum {
    REGULAR_TERM_NAME_OK = 0,
    REGULAR_TERM_NAME_ANONYMOUS,
    REGULAR_TERM_NAME_MATCHER,
    REGULAR_TERM_NAME_INVALID
} RegularTermNameStatus;

typedef struct {
    Atom *syntax;
    size_t names_start;
    size_t names_count;
    size_t types_start;
    size_t types_count;
} RegularTypedBinderGroup;

typedef enum {
    REGULAR_TERM_GROUP_NOT_GROUP = 0,
    REGULAR_TERM_GROUP_OK,
    REGULAR_TERM_GROUP_ERROR
} RegularBinderGroupStatus;

static CettaPrimeRegularTermElaborationV1 regular_term_failure(
    CettaPrimeRegularTermStatusV1 status, const char *reason) {
    return (CettaPrimeRegularTermElaborationV1){
        .status = status,
        .reason = reason,
    };
}

static CettaPrimeRegularTermElaborationV1 regular_term_syntax_failure(
    CettaPrimeRegularTermSyntaxErrorV1 error, size_t position,
    size_t arity, const char *reason) {
    return (CettaPrimeRegularTermElaborationV1){
        .status = CETTA_PRIME_REGULAR_TERM_SYNTAX_ERROR,
        .syntax_error = error,
        .position = position,
        .arity = arity,
        .reason = reason,
    };
}

static CettaPrimeRegularTermElaborationV1 regular_term_success(Atom *pattern) {
    return (CettaPrimeRegularTermElaborationV1){
        .status = CETTA_PRIME_REGULAR_TERM_OK,
        .pattern = pattern,
    };
}

static bool regular_term_quote_key(Atom *syntax, Atom **key_out) {
    if (!syntax || syntax->kind != ATOM_EXPR || syntax->expr.len != 2u ||
        !atom_is_symbol(syntax->expr.elems[0], "quote"))
        return false;
    if (key_out) *key_out = syntax->expr.elems[1];
    return true;
}

static RegularTermNameStatus regular_term_name_key(
    Atom *syntax, bool binder_position, Atom **key_out,
    bool *explicit_quote_out) {
    if (key_out) *key_out = NULL;
    if (explicit_quote_out) *explicit_quote_out = false;
    if (!syntax) return REGULAR_TERM_NAME_INVALID;
    if (syntax->kind == ATOM_VAR) return REGULAR_TERM_NAME_MATCHER;
    if (syntax->kind == ATOM_SYMBOL) {
        if (atom_is_symbol(syntax, ":")) return REGULAR_TERM_NAME_INVALID;
        if (binder_position && atom_is_symbol(syntax, "_"))
            return REGULAR_TERM_NAME_ANONYMOUS;
        if (key_out) *key_out = syntax;
        return REGULAR_TERM_NAME_OK;
    }
    Atom *key = NULL;
    if (!regular_term_quote_key(syntax, &key) || !name_key_is_admissible(key))
        return REGULAR_TERM_NAME_INVALID;
    if (key_out) *key_out = key;
    if (explicit_quote_out) *explicit_quote_out = true;
    return REGULAR_TERM_NAME_OK;
}

static const RegularTermBinding *regular_term_binding_find(
    const RegularTermBinding *environment, Atom *key, bool explicit_quote,
    uint64_t *index_out) {
    uint64_t index = 0u;
    for (const RegularTermBinding *binding = environment;
         binding; binding = binding->outer) {
        /* Bare `_` always means anonymous/hole syntax.  An explicitly quoted
         * @_ remains an ordinary structural name and must also be referenced
         * explicitly. */
        bool bare_anonymous = atom_is_symbol(key, "_") && !explicit_quote;
        if (binding->referencable && !bare_anonymous &&
            atom_eq(binding->key, key)) {
            if (index_out) *index_out = index;
            return binding;
        }
        if (binding->declaration) continue;
        if (index == UINT64_MAX) return NULL;
        index++;
    }
    return NULL;
}

static bool regular_term_binding_has_index(
    const RegularTermBinding *environment, uint64_t index) {
    const RegularTermBinding *binding = environment;
    while (binding && binding->declaration) binding = binding->outer;
    while (binding && index > 0u) {
        binding = binding->outer;
        while (binding && binding->declaration) binding = binding->outer;
        index--;
    }
    return binding && !binding->declaration;
}

static Atom *regular_term_pattern_var(Arena *arena, uint64_t index) {
    if (index > (uint64_t)INT64_MAX) return NULL;
    return atom_expr2(
        arena, atom_symbol(arena, "Var"),
        atom_int(arena, (int64_t)index));
}

static Atom *regular_term_pattern_fvar(Arena *arena, Atom *name) {
    const char *text = name && name->kind == ATOM_SYMBOL
        ? atom_name_cstr(name) : NULL;
    return text
        ? atom_expr2(
              arena, atom_symbol(arena, "FVar"), atom_string(arena, text))
        : NULL;
}

static Atom *regular_term_pattern_application(
    Arena *arena, const char *constructor, Atom **arguments, size_t arity) {
    Atom *list = atom_symbol(arena, "LNil");
    for (size_t i = arity; i > 0u; i--)
        list = atom_expr3(
            arena, atom_symbol(arena, "LCons"), arguments[i - 1u], list);
    return atom_expr3(
        arena, atom_symbol(arena, "PApp"),
        atom_string(arena, constructor), list);
}

static Atom *regular_term_pattern_lambda(Arena *arena, Atom *body) {
    Atom *binder = atom_expr3(
        arena, atom_symbol(arena, "PLam"),
        atom_symbol(arena, "BNone"), body);
    Atom *arguments[1] = {binder};
    return regular_term_pattern_application(arena, "Lam", arguments, 1u);
}

static Atom *regular_term_pattern_pi(
    Arena *arena, const char *constructor, Atom *domain, Atom *body) {
    Atom *binder = atom_expr3(
        arena, atom_symbol(arena, "PLam"),
        atom_symbol(arena, "BNone"), body);
    Atom *arguments[2] = {domain, binder};
    return regular_term_pattern_application(arena, constructor, arguments, 2u);
}

/* Numeric universe notation is syntax sugar for the ordinary inductive
 * level language.  Keeping this as Pattern structure avoids adding a
 * grounded-literal escape hatch to the shared inference carrier. */
static CettaPrimeRegularTermElaborationV1
regular_term_pattern_closed_universe(
    Arena *arena, uint64_t level,
    CettaPrimeRegularKernelBudget *budget) {
    Atom *level_pattern = regular_term_pattern_application(
        arena, "LevelZero", NULL, 0u);
    for (uint64_t index = 0u; index < level; index++) {
        if (!pattern_spend(budget))
            return regular_term_failure(
                CETTA_PRIME_REGULAR_TERM_BUDGET_EXHAUSTED,
                "universe-level-elaboration-budget");
        Atom *arguments[1] = {level_pattern};
        level_pattern = regular_term_pattern_application(
            arena, "LevelSucc", arguments, 1u);
    }
    Atom *arguments[1] = {level_pattern};
    return regular_term_success(regular_term_pattern_application(
        arena, "Sort", arguments, 1u));
}

static bool regular_term_known_head(Atom *head) {
    static const char *const heads[] = {
        "lam", "->", "sigma", "u", "idx", "app", "pair",
        "fst", "snd", "id", "refl",
    };
    if (!head || head->kind != ATOM_SYMBOL) return false;
    for (size_t i = 0u; i < sizeof(heads) / sizeof(heads[0]); i++)
        if (atom_is_symbol(head, heads[i])) return true;
    return false;
}

static bool regular_term_root_candidate(Atom *syntax) {
    if (!syntax) return false;
    if (atom_is_symbol(syntax, "u0") || atom_is_symbol(syntax, "u1"))
        return true;
    if (syntax->kind != ATOM_EXPR || syntax->expr.len == 0u) return false;
    if (regular_term_known_head(syntax->expr.elems[0])) return true;
    /* Natural application is admitted only when its function position is
     * itself visibly part of this syntax.  Bound-variable applications are
     * recognized recursively beneath a lambda, where the environment exists. */
    return syntax->expr.len >= 2u &&
           regular_term_root_candidate(syntax->expr.elems[0]);
}

static bool regular_term_contains_colon(Atom *syntax) {
    if (!syntax) return false;
    if (atom_is_symbol(syntax, ":")) return true;
    if (regular_term_quote_key(syntax, NULL)) return false;
    if (syntax->kind != ATOM_EXPR) return false;
    for (CettaExprIndex i = 0u; i < syntax->expr.len; i++)
        if (regular_term_contains_colon(syntax->expr.elems[i])) return true;
    return false;
}

static RegularBinderGroupStatus regular_term_direct_group(
    Atom *syntax, RegularTypedBinderGroup *group_out,
    CettaPrimeRegularTermElaborationV1 *error_out) {
    if (!syntax || syntax->kind != ATOM_EXPR || syntax->expr.len == 0u)
        return REGULAR_TERM_GROUP_NOT_GROUP;
    CettaExprIndex colon = UINT64_MAX;
    size_t colon_count = 0u;
    for (CettaExprIndex i = 0u; i < syntax->expr.len; i++) {
        if (!atom_is_symbol(syntax->expr.elems[i], ":")) continue;
        colon = i;
        colon_count++;
    }
    if (colon_count == 0u) return REGULAR_TERM_GROUP_NOT_GROUP;
    if (colon_count != 1u || !cetta_expr_len_fits_size(syntax->expr.len)) {
        if (error_out)
            *error_out = regular_term_syntax_failure(
                CETTA_PRIME_REGULAR_TERM_BINDER_TYPE_ARITY_MISMATCH,
                0u, colon_count,
                "typed-binder-requires-one-colon");
        return REGULAR_TERM_GROUP_ERROR;
    }
    size_t length = (size_t)syntax->expr.len;
    size_t names_start = 0u;
    size_t names_count = (size_t)colon;
    size_t types_start = (size_t)colon + 1u;
    size_t types_count = length - types_start;
    if (colon == 0u) {
        /* Prefix ascription remains accepted for compatibility, but only as
         * the unambiguous unary (: name type) spelling. */
        names_start = 1u;
        names_count = length == 3u ? 1u : 0u;
        types_start = 2u;
        types_count = length == 3u ? 1u : 0u;
    }
    if (names_count == 0u || types_count == 0u ||
        (types_count != 1u && types_count != names_count)) {
        if (error_out)
            *error_out = regular_term_syntax_failure(
                CETTA_PRIME_REGULAR_TERM_BINDER_TYPE_ARITY_MISMATCH,
                names_count, types_count,
                "typed-binder-name-type-count-mismatch");
        return REGULAR_TERM_GROUP_ERROR;
    }
    if (group_out)
        *group_out = (RegularTypedBinderGroup){
            .syntax = syntax,
            .names_start = names_start,
            .names_count = names_count,
            .types_start = types_start,
            .types_count = types_count,
        };
    return REGULAR_TERM_GROUP_OK;
}

static RegularBinderGroupStatus regular_term_group_spec_count(
    Atom *syntax, size_t *count_out,
    CettaPrimeRegularTermElaborationV1 *error_out) {
    RegularTypedBinderGroup direct;
    RegularBinderGroupStatus direct_status = regular_term_direct_group(
        syntax, &direct, error_out);
    if (direct_status != REGULAR_TERM_GROUP_NOT_GROUP) {
        if (direct_status == REGULAR_TERM_GROUP_OK && count_out) *count_out = 1u;
        return direct_status;
    }
    if (!syntax || syntax->kind != ATOM_EXPR || syntax->expr.len == 0u ||
        !cetta_expr_len_fits_size(syntax->expr.len))
        return REGULAR_TERM_GROUP_NOT_GROUP;
    size_t count = (size_t)syntax->expr.len;
    for (size_t i = 0u; i < count; i++) {
        RegularBinderGroupStatus nested = regular_term_direct_group(
            syntax->expr.elems[i], NULL, error_out);
        if (nested == REGULAR_TERM_GROUP_ERROR) return nested;
        if (nested != REGULAR_TERM_GROUP_OK) return REGULAR_TERM_GROUP_NOT_GROUP;
    }
    if (count_out) *count_out = count;
    return REGULAR_TERM_GROUP_OK;
}

static CettaPrimeRegularTermElaborationV1 regular_term_collect_group_specs(
    Arena *arena, Atom *syntax, RegularTypedBinderGroup **groups_out,
    size_t *group_count_out) {
    size_t group_count = 0u;
    for (CettaExprIndex i = 1u; i + 1u < syntax->expr.len; i++) {
        size_t current = 0u;
        CettaPrimeRegularTermElaborationV1 error = {0};
        RegularBinderGroupStatus status = regular_term_group_spec_count(
            syntax->expr.elems[i], &current, &error);
        if (status == REGULAR_TERM_GROUP_ERROR) return error;
        if (status != REGULAR_TERM_GROUP_OK)
            return regular_term_syntax_failure(
                CETTA_PRIME_REGULAR_TERM_INVALID_BINDER_NAME,
                (size_t)i - 1u, 0u,
                "typed-telescope-contains-non-binder");
        if (SIZE_MAX - group_count < current)
            return regular_term_failure(
                CETTA_PRIME_REGULAR_TERM_RESOURCE_LIMIT,
                "typed-telescope-too-large");
        group_count += current;
    }
    if (group_count == 0u ||
        group_count > SIZE_MAX / sizeof(RegularTypedBinderGroup))
        return regular_term_failure(
            CETTA_PRIME_REGULAR_TERM_RESOURCE_LIMIT,
            "typed-telescope-too-large");
    RegularTypedBinderGroup *groups = arena_alloc(
        arena, sizeof(*groups) * group_count);
    size_t next = 0u;
    for (CettaExprIndex i = 1u; i + 1u < syntax->expr.len; i++) {
        Atom *group_syntax = syntax->expr.elems[i];
        RegularTypedBinderGroup direct;
        if (regular_term_direct_group(group_syntax, &direct, NULL) ==
            REGULAR_TERM_GROUP_OK) {
            groups[next++] = direct;
            continue;
        }
        for (CettaExprIndex j = 0u; j < group_syntax->expr.len; j++) {
            RegularBinderGroupStatus nested = regular_term_direct_group(
                group_syntax->expr.elems[j], &groups[next], NULL);
            if (nested != REGULAR_TERM_GROUP_OK)
                return regular_term_failure(
                    CETTA_PRIME_REGULAR_TERM_OUT_OF_CLASS,
                    "typed-telescope-collection-drift");
            next++;
        }
    }
    if (groups_out) *groups_out = groups;
    if (group_count_out) *group_count_out = group_count;
    return regular_term_success(NULL);
}

static CettaPrimeRegularTermElaborationV1 regular_term_lower_rec(
    Arena *arena, Atom *syntax, const RegularTermBinding *environment,
    CettaPrimeRegularKernelBudget *budget);

static CettaPrimeRegularTermElaborationV1 regular_term_lower_lambda(
    Arena *arena, Atom *syntax, const RegularTermBinding *environment,
    CettaPrimeRegularKernelBudget *budget) {
    if (syntax->expr.len != 3u)
        return regular_term_syntax_failure(
            CETTA_PRIME_REGULAR_TERM_WRONG_ARITY, 0u,
            cetta_expr_len_fits_size(syntax->expr.len)
                ? (size_t)syntax->expr.len - 1u : SIZE_MAX,
            "lam-expects-binder-and-body");
    Atom *binder_syntax = syntax->expr.elems[1];
    if (regular_term_contains_colon(binder_syntax))
        return regular_term_failure(
            CETTA_PRIME_REGULAR_TERM_OUT_OF_CLASS,
            "typed-lambda-awaits-typed-pattern-authority");

    if (binder_syntax->kind != ATOM_EXPR ||
        regular_term_quote_key(binder_syntax, NULL)) {
        Atom *key = NULL;
        RegularTermNameStatus name_status = regular_term_name_key(
            binder_syntax, true, &key, NULL);
        if (name_status == REGULAR_TERM_NAME_MATCHER)
            return regular_term_syntax_failure(
                CETTA_PRIME_REGULAR_TERM_MATCHER_BINDER, 0u, 1u,
                "matcher-variable-is-not-a-lexical-binder");
        if (name_status == REGULAR_TERM_NAME_INVALID)
            return regular_term_syntax_failure(
                CETTA_PRIME_REGULAR_TERM_INVALID_BINDER_NAME, 0u, 1u,
                "invalid-lexical-binder-name");
        RegularTermBinding binding = {
            .key = key,
            .referencable = name_status == REGULAR_TERM_NAME_OK,
            .outer = environment,
        };
        CettaPrimeRegularTermElaborationV1 body = regular_term_lower_rec(
            arena, syntax->expr.elems[2], &binding, budget);
        if (body.status != CETTA_PRIME_REGULAR_TERM_OK) return body;
        return regular_term_success(regular_term_pattern_lambda(arena, body.pattern));
    }

    if (binder_syntax->expr.len == 0u)
        return regular_term_syntax_failure(
            CETTA_PRIME_REGULAR_TERM_EMPTY_BINDER_LIST, 0u, 0u,
            "lambda-binder-list-is-empty");
    if (!cetta_expr_len_fits_size(binder_syntax->expr.len) ||
        !cetta_expr_len_mul_fits_size(
            binder_syntax->expr.len, sizeof(RegularTermBinding)))
        return regular_term_failure(
            CETTA_PRIME_REGULAR_TERM_RESOURCE_LIMIT,
            "lambda-binder-list-too-large");
    size_t count = (size_t)binder_syntax->expr.len;
    RegularTermBinding *bindings = arena_alloc(
        arena, sizeof(*bindings) * count);
    for (size_t i = 0u; i < count; i++) {
        Atom *key = NULL;
        RegularTermNameStatus name_status = regular_term_name_key(
            binder_syntax->expr.elems[i], true, &key, NULL);
        if (name_status == REGULAR_TERM_NAME_MATCHER)
            return regular_term_syntax_failure(
                CETTA_PRIME_REGULAR_TERM_MATCHER_BINDER, i, count,
                "matcher-variable-is-not-a-lexical-binder");
        if (name_status == REGULAR_TERM_NAME_INVALID)
            return regular_term_syntax_failure(
                CETTA_PRIME_REGULAR_TERM_INVALID_BINDER_NAME, i, count,
                "invalid-lexical-binder-name");
        bindings[i] = (RegularTermBinding){
            .key = key,
            .referencable = name_status == REGULAR_TERM_NAME_OK,
            .outer = i == 0u ? environment : &bindings[i - 1u],
        };
    }
    CettaPrimeRegularTermElaborationV1 body = regular_term_lower_rec(
        arena, syntax->expr.elems[2], &bindings[count - 1u], budget);
    if (body.status != CETTA_PRIME_REGULAR_TERM_OK) return body;
    Atom *nested = body.pattern;
    for (size_t i = count; i > 0u; i--)
        nested = regular_term_pattern_lambda(arena, nested);
    return regular_term_success(nested);
}

static CettaPrimeRegularTermElaborationV1 regular_term_lower_arrow_rec(
    Arena *arena, Atom *syntax, CettaExprIndex argument,
    const RegularTermBinding *environment,
    CettaPrimeRegularKernelBudget *budget, const char *constructor) {
    CettaPrimeRegularTermElaborationV1 domain = regular_term_lower_rec(
        arena, syntax->expr.elems[argument], environment, budget);
    if (domain.status != CETTA_PRIME_REGULAR_TERM_OK) return domain;
    RegularTermBinding anonymous = {
        .key = NULL,
        .referencable = false,
        .outer = environment,
    };
    CettaPrimeRegularTermElaborationV1 codomain;
    if (argument + 2u == syntax->expr.len) {
        codomain = regular_term_lower_rec(
            arena, syntax->expr.elems[argument + 1u],
            &anonymous, budget);
    } else {
        codomain = regular_term_lower_arrow_rec(
            arena, syntax, argument + 1u, &anonymous,
            budget, constructor);
    }
    if (codomain.status != CETTA_PRIME_REGULAR_TERM_OK) return codomain;
    return regular_term_success(regular_term_pattern_pi(
        arena, constructor, domain.pattern, codomain.pattern));
}

static CettaPrimeRegularTermElaborationV1 regular_term_lower_telescope_rec(
    Arena *arena, RegularTypedBinderGroup *groups, size_t group_count,
    size_t group_index, Atom *body_syntax,
    const RegularTermBinding *environment,
    CettaPrimeRegularKernelBudget *budget, const char *constructor) {
    if (group_index == group_count)
        return regular_term_lower_rec(
            arena, body_syntax, environment, budget);
    RegularTypedBinderGroup *group = &groups[group_index];
    if (group->names_count > SIZE_MAX / sizeof(Atom *) ||
        group->names_count > SIZE_MAX / sizeof(RegularTermBinding))
        return regular_term_failure(
            CETTA_PRIME_REGULAR_TERM_RESOURCE_LIMIT,
            "typed-binder-group-too-large");
    Atom **domains = arena_alloc(
        arena, sizeof(*domains) * group->names_count);
    RegularTermBinding *bindings = arena_alloc(
        arena, sizeof(*bindings) * group->names_count);

    /* Every type in one group is read in the preceding telescope context.
     * Later groups, but not sibling names in this group, see these binders. */
    for (size_t i = 0u; i < group->names_count; i++) {
        size_t type_offset = group->types_count == 1u ? 0u : i;
        CettaPrimeRegularTermElaborationV1 domain = regular_term_lower_rec(
            arena,
            group->syntax->expr.elems[group->types_start + type_offset],
            environment, budget);
        if (domain.status != CETTA_PRIME_REGULAR_TERM_OK) return domain;
        domains[i] = domain.pattern;
    }
    for (size_t i = 0u; i < group->names_count; i++) {
        Atom *key = NULL;
        RegularTermNameStatus name_status = regular_term_name_key(
            group->syntax->expr.elems[group->names_start + i],
            true, &key, NULL);
        if (name_status == REGULAR_TERM_NAME_MATCHER)
            return regular_term_syntax_failure(
                CETTA_PRIME_REGULAR_TERM_MATCHER_BINDER,
                i, group->names_count,
                "matcher-variable-is-not-a-lexical-binder");
        if (name_status == REGULAR_TERM_NAME_INVALID)
            return regular_term_syntax_failure(
                CETTA_PRIME_REGULAR_TERM_INVALID_BINDER_NAME,
                i, group->names_count,
                "invalid-lexical-binder-name");
        bindings[i] = (RegularTermBinding){
            .key = key,
            .referencable = name_status == REGULAR_TERM_NAME_OK,
            .outer = i == 0u ? environment : &bindings[i - 1u],
        };
    }
    CettaPrimeRegularTermElaborationV1 body =
        regular_term_lower_telescope_rec(
            arena, groups, group_count, group_index + 1u, body_syntax,
            &bindings[group->names_count - 1u], budget, constructor);
    if (body.status != CETTA_PRIME_REGULAR_TERM_OK) return body;
    Atom *nested = body.pattern;
    for (size_t i = group->names_count; i > 0u; i--)
        nested = regular_term_pattern_pi(
            arena, constructor, domains[i - 1u], nested);
    return regular_term_success(nested);
}

static CettaPrimeRegularTermElaborationV1 regular_term_lower_fixed_application(
    Arena *arena, Atom *syntax, const RegularTermBinding *environment,
    CettaPrimeRegularKernelBudget *budget, const char *constructor,
    size_t arity) {
    if (!cetta_expr_len_fits_size(syntax->expr.len) ||
        (size_t)syntax->expr.len != arity + 1u)
        return regular_term_syntax_failure(
            CETTA_PRIME_REGULAR_TERM_WRONG_ARITY, 0u,
            cetta_expr_len_fits_size(syntax->expr.len)
                ? (size_t)syntax->expr.len - 1u : SIZE_MAX,
            "regular-syntax-constructor-arity");
    Atom *arguments[3] = {NULL, NULL, NULL};
    for (size_t i = 0u; i < arity; i++) {
        CettaPrimeRegularTermElaborationV1 nested = regular_term_lower_rec(
            arena, syntax->expr.elems[i + 1u], environment, budget);
        if (nested.status != CETTA_PRIME_REGULAR_TERM_OK) return nested;
        arguments[i] = nested.pattern;
    }
    return regular_term_success(regular_term_pattern_application(
        arena, constructor, arguments, arity));
}

static CettaPrimeRegularTermElaborationV1 regular_term_lower_rec(
    Arena *arena, Atom *syntax, const RegularTermBinding *environment,
    CettaPrimeRegularKernelBudget *budget) {
    if (!pattern_spend(budget))
        return regular_term_failure(
            CETTA_PRIME_REGULAR_TERM_BUDGET_EXHAUSTED,
            "regular-syntax-elaboration-budget");
    if (!syntax)
        return regular_term_failure(
            CETTA_PRIME_REGULAR_TERM_OUT_OF_CLASS,
            "null-regular-syntax-term");

    Atom *name_key = NULL;
    bool explicit_quote = false;
    RegularTermNameStatus name_status = regular_term_name_key(
        syntax, false, &name_key, &explicit_quote);
    uint64_t index = 0u;
    const RegularTermBinding *binding = name_status == REGULAR_TERM_NAME_OK
        ? regular_term_binding_find(
              environment, name_key, explicit_quote, &index)
        : NULL;
    if (binding) {
        Atom *variable = binding->declaration
            ? regular_term_pattern_fvar(arena, binding->key)
            : regular_term_pattern_var(arena, index);
        return variable
            ? regular_term_success(variable)
            : regular_term_failure(
                  CETTA_PRIME_REGULAR_TERM_RESOURCE_LIMIT,
                  "regular-syntax-index-range");
    }

    if (atom_is_symbol(syntax, "u0") || atom_is_symbol(syntax, "u1")) {
        Atom *arguments[1] = {NULL};
        return regular_term_success(regular_term_pattern_application(
            arena, atom_is_symbol(syntax, "u0") ? "U0" : "U1",
            arguments, 0u));
    }
    if (syntax->kind != ATOM_EXPR || syntax->expr.len == 0u)
        return (CettaPrimeRegularTermElaborationV1){
            .status = CETTA_PRIME_REGULAR_TERM_OUT_OF_CLASS,
            .unresolved_name = name_status == REGULAR_TERM_NAME_OK
                ? syntax : NULL,
            .reason = "term-outside-regular-syntax",
        };

    Atom *head = syntax->expr.elems[0];
    if (atom_is_symbol(head, "lam"))
        return regular_term_lower_lambda(arena, syntax, environment, budget);
    if (atom_is_symbol(head, "u")) {
        uint64_t level = 0u;
        if (syntax->expr.len == 2u &&
            regular_level_parameter_marker(
                syntax->expr.elems[1], &level)) {
            Atom *parameter = atom_int(arena, (int64_t)level);
            Atom *level_pattern = regular_term_pattern_application(
                arena, "LevelParam", &parameter, 1u);
            Atom *arguments[1] = {level_pattern};
            return regular_term_success(regular_term_pattern_application(
                arena, "Sort", arguments, 1u));
        }
        if (syntax->expr.len != 2u ||
            !pattern_natural(syntax->expr.elems[1], &level))
            return regular_term_syntax_failure(
                CETTA_PRIME_REGULAR_TERM_INVALID_LEVEL, 0u,
                cetta_expr_len_fits_size(syntax->expr.len)
                    ? (size_t)syntax->expr.len - 1u : SIZE_MAX,
                "u-expects-one-natural-level");
        return regular_term_pattern_closed_universe(arena, level, budget);
    }
    if (atom_is_symbol(head, "idx")) {
        uint64_t direct_index = 0u;
        if (syntax->expr.len != 2u ||
            !pattern_natural(syntax->expr.elems[1], &direct_index))
            return regular_term_syntax_failure(
                CETTA_PRIME_REGULAR_TERM_INVALID_INDEX, 0u,
                cetta_expr_len_fits_size(syntax->expr.len)
                    ? (size_t)syntax->expr.len - 1u : SIZE_MAX,
                "idx-expects-one-natural-number");
        /* A natural index denotes a variable only when this authored term
         * supplies the corresponding lexical binder.  A loose index may be
         * meaningful in a separately supplied context, so the unscoped
         * syntax authority abstains instead of refuting it. */
        if (!regular_term_binding_has_index(environment, direct_index))
            return regular_term_failure(
                CETTA_PRIME_REGULAR_TERM_OUT_OF_CLASS,
                "loose-regular-syntax-index");
        Atom *variable = regular_term_pattern_var(arena, direct_index);
        return variable
            ? regular_term_success(variable)
            : regular_term_failure(
                  CETTA_PRIME_REGULAR_TERM_RESOURCE_LIMIT,
                  "regular-syntax-index-range");
    }
    if (atom_is_symbol(head, "->") || atom_is_symbol(head, "sigma")) {
        if (syntax->expr.len < 3u)
            return regular_term_syntax_failure(
                CETTA_PRIME_REGULAR_TERM_WRONG_ARITY, 0u,
                cetta_expr_len_fits_size(syntax->expr.len)
                    ? (size_t)syntax->expr.len - 1u : SIZE_MAX,
                "regular-syntax-binder-expects-domain-and-codomain");
        size_t ignored_group_count = 0u;
        CettaPrimeRegularTermElaborationV1 group_error = {0};
        RegularBinderGroupStatus first_group = regular_term_group_spec_count(
            syntax->expr.elems[1], &ignored_group_count, &group_error);
        if (first_group == REGULAR_TERM_GROUP_ERROR) return group_error;
        const char *constructor =
            atom_is_symbol(head, "->") ? "Pi" : "Sigma";
        if (first_group == REGULAR_TERM_GROUP_OK) {
            RegularTypedBinderGroup *groups = NULL;
            size_t group_count = 0u;
            CettaPrimeRegularTermElaborationV1 collected =
                regular_term_collect_group_specs(
                    arena, syntax, &groups, &group_count);
            if (collected.status != CETTA_PRIME_REGULAR_TERM_OK)
                return collected;
            return regular_term_lower_telescope_rec(
                arena, groups, group_count, 0u,
                syntax->expr.elems[syntax->expr.len - 1u],
                environment, budget, constructor);
        }
        return regular_term_lower_arrow_rec(
            arena, syntax, 1u, environment, budget, constructor);
    }
    if (atom_is_symbol(head, "app"))
        return regular_term_lower_fixed_application(
            arena, syntax, environment, budget, "App", 2u);
    if (atom_is_symbol(head, "pair"))
        return regular_term_lower_fixed_application(
            arena, syntax, environment, budget, "Pair", 2u);
    if (atom_is_symbol(head, "fst"))
        return regular_term_lower_fixed_application(
            arena, syntax, environment, budget, "Fst", 1u);
    if (atom_is_symbol(head, "snd"))
        return regular_term_lower_fixed_application(
            arena, syntax, environment, budget, "Snd", 1u);
    if (atom_is_symbol(head, "id"))
        return regular_term_lower_fixed_application(
            arena, syntax, environment, budget, "Id", 3u);
    if (atom_is_symbol(head, "refl"))
        return regular_term_lower_fixed_application(
            arena, syntax, environment, budget, "Refl", 1u);

    /* Ordinary MeTTa application syntax is left-associated in the regular
     * syntax.  It becomes eligible only when the head itself elaborates, so
     * unknown guest constructors are never captured by this fragment. */
    CettaPrimeRegularTermElaborationV1 function = regular_term_lower_rec(
        arena, head, environment, budget);
    if (function.status != CETTA_PRIME_REGULAR_TERM_OK) return function;
    Atom *application = function.pattern;
    for (CettaExprIndex i = 1u; i < syntax->expr.len; i++) {
        CettaPrimeRegularTermElaborationV1 argument = regular_term_lower_rec(
            arena, syntax->expr.elems[i], environment, budget);
        if (argument.status != CETTA_PRIME_REGULAR_TERM_OK) return argument;
        Atom *arguments[2] = {application, argument.pattern};
        application = regular_term_pattern_application(
            arena, "App", arguments, 2u);
    }
    return regular_term_success(application);
}

CettaPrimeRegularTermElaborationV1
cetta_prime_regular_term_to_pattern_v1(
    Arena *arena, Atom *syntax, CettaPrimeRegularKernelBudget *budget) {
    if (!arena || !syntax || !budget)
        return regular_term_failure(
            CETTA_PRIME_REGULAR_TERM_OUT_OF_CLASS,
            "invalid-regular-syntax-input");
    if (!regular_term_root_candidate(syntax))
        return regular_term_failure(
            CETTA_PRIME_REGULAR_TERM_NOT_SYNTAX,
            "not-regular-term-syntax");
    CettaPrimeRegularTermElaborationV1 result = regular_term_lower_rec(
        arena, syntax, NULL, budget);
    if (result.status == CETTA_PRIME_REGULAR_TERM_NOT_SYNTAX)
        result.status = CETTA_PRIME_REGULAR_TERM_OUT_OF_CLASS;
    return result;
}

CettaPrimeRegularTermElaborationV1
cetta_prime_regular_term_to_pattern_in_environment_v1(
    Arena *arena, CettaPrimeRegularTermEnvironmentV1 environment,
    Atom *syntax, CettaPrimeRegularKernelBudget *budget) {
    if (!arena || !syntax || !budget ||
        (environment.count > 0u && !environment.names) ||
        environment.count > SIZE_MAX / sizeof(RegularTermBinding))
        return regular_term_failure(
            CETTA_PRIME_REGULAR_TERM_OUT_OF_CLASS,
            "invalid-regular-term-environment");

    RegularTermBinding *bindings = environment.count > 0u
        ? arena_alloc(arena, sizeof(*bindings) * environment.count)
        : NULL;
    const RegularTermBinding *outer = NULL;
    for (size_t offset = environment.count; offset > 0u; offset--) {
        size_t index = offset - 1u;
        Atom *name = (Atom *)environment.names[index];
        if (!name || name->kind != ATOM_SYMBOL)
            return regular_term_failure(
                CETTA_PRIME_REGULAR_TERM_OUT_OF_CLASS,
                "declaration-name-is-not-a-symbol");
        bindings[index] = (RegularTermBinding){
            .key = name,
            .referencable = true,
            .declaration = true,
            .declaration_index = index,
            .outer = outer,
        };
        outer = &bindings[index];
    }
    CettaPrimeRegularTermElaborationV1 result = regular_term_lower_rec(
        arena, syntax, outer, budget);
    if (result.status == CETTA_PRIME_REGULAR_TERM_NOT_SYNTAX)
        result.status = CETTA_PRIME_REGULAR_TERM_OUT_OF_CLASS;
    return result;
}

static CettaPrimeRegularPatternElaborationV1 pattern_elaborate_validated(
    Arena *arena, CettaPrimeRegularPatternEnvironmentV1 environment,
    Atom *pattern, CettaPrimeRegularKernelBudget *budget);

bool cetta_prime_regular_term_maybe_syntax_v1(Atom *syntax) {
    return regular_term_root_candidate(syntax);
}

Atom *cetta_prime_regular_term_authored_symbol_v1(
    Arena *arena, Atom *intrinsic) {
    static const struct {
        const char *intrinsic;
        const char *authored;
    } names[] = {
        {"U0", "u0"}, {"U1", "u1"}, {"Pi", "->"},
        {"Sigma", "sigma"}, {"Lam", "lam"}, {"App", "app"},
        {"Pair", "pair"}, {"Fst", "fst"}, {"Snd", "snd"},
        {"Id", "id"}, {"Refl", "refl"},
    };
    if (!arena || !intrinsic || intrinsic->kind != ATOM_SYMBOL)
        return intrinsic;
    for (size_t i = 0u; i < sizeof(names) / sizeof(names[0]); i++)
        if (atom_is_symbol(intrinsic, names[i].intrinsic))
            return atom_symbol(arena, names[i].authored);
    return intrinsic;
}

static Atom *regular_term_quote_intrinsic_rec(
    Arena *arena, Atom *intrinsic) {
    if (!arena || !intrinsic) return NULL;
    if (intrinsic->kind == ATOM_EXPR && intrinsic->expr.len == 2u &&
        atom_is_symbol(intrinsic->expr.elems[0], "Sort")) {
        return cetta_prime_regular_kernel_quote_closed_universe_sort_v1(
            arena, intrinsic);
    }
    if (intrinsic->kind == ATOM_SYMBOL) {
        Atom *authored = cetta_prime_regular_term_authored_symbol_v1(
            arena, intrinsic);
        return authored == intrinsic ? atom_deep_copy(arena, intrinsic)
                                     : authored;
    }
    if (intrinsic->kind != ATOM_EXPR)
        return atom_deep_copy(arena, intrinsic);
    if (intrinsic->expr.len > SIZE_MAX / sizeof(Atom *)) return NULL;
    Atom **items = arena_alloc(
        arena, sizeof(*items) * (size_t)intrinsic->expr.len);
    for (CettaExprIndex index = 0u;
         index < intrinsic->expr.len; index++) {
        items[index] = regular_term_quote_intrinsic_rec(
            arena, intrinsic->expr.elems[index]);
        if (!items[index]) return NULL;
    }
    return atom_expr(arena, items, intrinsic->expr.len);
}

Atom *cetta_prime_regular_term_quote_intrinsic_v1(
    Arena *arena, Atom *intrinsic) {
    return regular_term_quote_intrinsic_rec(arena, intrinsic);
}

CettaPrimeRegularTermCheckV1
cetta_prime_regular_term_form_v1(
    Arena *arena, Atom *context, Atom *expected_syntax,
    CettaPrimeRegularKernelBudget *budget) {
    CettaPrimeRegularTermCheckV1 result = {0};
    CettaPrimeRegularTermElaborationV1 expected_syntax_result =
        cetta_prime_regular_term_to_pattern_v1(
            arena, expected_syntax, budget);
    if (expected_syntax_result.status != CETTA_PRIME_REGULAR_TERM_OK) {
        result.phase = CETTA_PRIME_REGULAR_TERM_PHASE_EXPECTED_SYNTAX;
        result.syntax = expected_syntax_result;
        return result;
    }
    CettaPrimeRegularPatternEnvironmentV1 empty_environment = {0};
    CettaPrimeRegularPatternElaborationV1 expected_pattern_result =
        pattern_elaborate_validated(
            arena, empty_environment,
            expected_syntax_result.pattern, budget);
    if (expected_pattern_result.status != CETTA_PRIME_REGULAR_PATTERN_OK) {
        result.phase = CETTA_PRIME_REGULAR_TERM_PHASE_EXPECTED_PATTERN;
        result.pattern = expected_pattern_result;
        return result;
    }
    CettaPrimeRegularKernelPreparedExpectedResultV1 prepared =
        cetta_prime_regular_kernel_prepare_intrinsic_expected_v1(
            arena, context, expected_pattern_result.term, budget);
    result.expected = expected_pattern_result.term;
    result.judgment = (CettaPrimeRegularKernelResult){
        .status = prepared.status,
        .reason = prepared.reason,
    };
    result.phase = prepared.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED
        ? CETTA_PRIME_REGULAR_TERM_PHASE_NONE
        : CETTA_PRIME_REGULAR_TERM_PHASE_EXPECTED_FORMATION;
    return result;
}

CettaPrimeRegularTermCheckV1
cetta_prime_regular_term_synth_v1(
    Arena *arena, Atom *context, Atom *term_syntax,
    CettaPrimeRegularKernelBudget *budget) {
    CettaPrimeRegularTermCheckV1 result = {0};
    CettaPrimeRegularTermElaborationV1 term_syntax_result =
        cetta_prime_regular_term_to_pattern_v1(
            arena, term_syntax, budget);
    if (term_syntax_result.status != CETTA_PRIME_REGULAR_TERM_OK) {
        result.phase = CETTA_PRIME_REGULAR_TERM_PHASE_TERM_SYNTAX;
        result.syntax = term_syntax_result;
        return result;
    }
    CettaPrimeRegularPatternEnvironmentV1 empty_environment = {0};
    CettaPrimeRegularPatternElaborationV1 term_pattern_result =
        pattern_elaborate_validated(
            arena, empty_environment, term_syntax_result.pattern, budget);
    if (term_pattern_result.status != CETTA_PRIME_REGULAR_PATTERN_OK) {
        result.phase = CETTA_PRIME_REGULAR_TERM_PHASE_TERM_PATTERN;
        result.pattern = term_pattern_result;
        return result;
    }
    result.judgment = cetta_prime_regular_kernel_synth_intrinsic_v1(
        arena, context, term_pattern_result.term, budget);
    result.term = term_pattern_result.term;
    result.phase = result.judgment.status ==
                           CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED
        ? CETTA_PRIME_REGULAR_TERM_PHASE_NONE
        : CETTA_PRIME_REGULAR_TERM_PHASE_TERM_TYPING;
    return result;
}

CettaPrimeRegularTermCheckV1
cetta_prime_regular_term_elaborate_and_check_v1(
    Arena *arena, Atom *context, Atom *term_syntax, Atom *expected_syntax,
    CettaPrimeRegularKernelBudget *budget) {
    CettaPrimeRegularTermCheckV1 result = {0};
    CettaPrimeRegularTermElaborationV1 expected_syntax_result =
        cetta_prime_regular_term_to_pattern_v1(
            arena, expected_syntax, budget);
    if (expected_syntax_result.status != CETTA_PRIME_REGULAR_TERM_OK) {
        result.phase = CETTA_PRIME_REGULAR_TERM_PHASE_EXPECTED_SYNTAX;
        result.syntax = expected_syntax_result;
        return result;
    }

    CettaPrimeRegularPatternEnvironmentV1 empty_environment = {0};
    CettaPrimeRegularPatternElaborationV1 expected_pattern_result =
        pattern_elaborate_validated(
            arena, empty_environment,
            expected_syntax_result.pattern, budget);
    if (expected_pattern_result.status != CETTA_PRIME_REGULAR_PATTERN_OK) {
        result.phase = CETTA_PRIME_REGULAR_TERM_PHASE_EXPECTED_PATTERN;
        result.pattern = expected_pattern_result;
        return result;
    }

    CettaPrimeRegularKernelPreparedExpectedResultV1 prepared =
        cetta_prime_regular_kernel_prepare_intrinsic_expected_v1(
            arena, context, expected_pattern_result.term, budget);
    if (prepared.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
        result.phase = CETTA_PRIME_REGULAR_TERM_PHASE_EXPECTED_FORMATION;
        result.expected = expected_pattern_result.term;
        result.judgment = (CettaPrimeRegularKernelResult){
            .status = prepared.status,
            .reason = prepared.reason,
        };
        return result;
    }

    CettaPrimeRegularTermElaborationV1 term_syntax_result =
        cetta_prime_regular_term_to_pattern_v1(
            arena, term_syntax, budget);
    if (term_syntax_result.status != CETTA_PRIME_REGULAR_TERM_OK) {
        result.phase = CETTA_PRIME_REGULAR_TERM_PHASE_TERM_SYNTAX;
        result.syntax = term_syntax_result;
        result.expected = expected_pattern_result.term;
        return result;
    }
    CettaPrimeRegularPatternElaborationV1 term_pattern_result =
        pattern_elaborate_validated(
            arena, empty_environment, term_syntax_result.pattern, budget);
    if (term_pattern_result.status != CETTA_PRIME_REGULAR_PATTERN_OK) {
        result.phase = CETTA_PRIME_REGULAR_TERM_PHASE_TERM_PATTERN;
        result.pattern = term_pattern_result;
        result.expected = expected_pattern_result.term;
        return result;
    }

    result.judgment = cetta_prime_regular_kernel_check_prepared_intrinsic_v1(
        arena, prepared.prepared, term_pattern_result.term, budget);
    result.term = term_pattern_result.term;
    result.expected = expected_pattern_result.term;
    result.phase = result.judgment.status ==
                           CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED
        ? CETTA_PRIME_REGULAR_TERM_PHASE_NONE
        : CETTA_PRIME_REGULAR_TERM_PHASE_TERM_TYPING;
    return result;
}

static bool pattern_generated_binder_name(const char *name) {
    static const char prefix[] = "__pk_";
    if (!name || strncmp(name, prefix, sizeof(prefix) - 1u) != 0) return false;
    const char *digits = name + sizeof(prefix) - 1u;
    if (!*digits || (digits[0] == '0' && digits[1] != '\0')) return false;
    for (const char *cursor = digits; *cursor; cursor++)
        if (*cursor < '0' || *cursor > '9') return false;
    return true;
}

static CettaPrimeRegularPatternStatusV1 pattern_validate_environment(
    CettaPrimeRegularPatternEnvironmentV1 environment,
    CettaPrimeRegularKernelBudget *budget) {
    if (environment.count > 0u && !environment.names)
        return CETTA_PRIME_REGULAR_PATTERN_INVALID_ENVIRONMENT;
    for (size_t i = 0u; i < environment.count; i++) {
        if (!pattern_spend(budget))
            return CETTA_PRIME_REGULAR_PATTERN_BUDGET_EXHAUSTED;
        const char *name = NULL;
        if (!pattern_string(environment.names[i], &name) ||
            pattern_generated_binder_name(name))
            return CETTA_PRIME_REGULAR_PATTERN_INVALID_ENVIRONMENT;
        for (size_t j = 0u; j < i; j++) {
            const char *preceding = NULL;
            if (!pattern_string(environment.names[j], &preceding) ||
                strcmp(name, preceding) == 0)
                return CETTA_PRIME_REGULAR_PATTERN_INVALID_ENVIRONMENT;
        }
    }
    return CETTA_PRIME_REGULAR_PATTERN_OK;
}

static PatternListStatus pattern_collect_arguments(
    Atom *list, Atom **arguments, size_t capacity, size_t *count_out,
    CettaPrimeRegularKernelBudget *budget) {
    size_t count = 0u;
    Atom *cursor = list;
    while (!atom_is_symbol(cursor, "LNil")) {
        if (!pattern_spend(budget)) return PATTERN_LIST_BUDGET;
        if (!pattern_tag(cursor, "LCons", 3u)) return PATTERN_LIST_INVALID;
        if (count < capacity) arguments[count] = cursor->expr.elems[1];
        if (count == SIZE_MAX) return PATTERN_LIST_RESOURCE;
        count++;
        cursor = cursor->expr.elems[2];
    }
    if (count_out) *count_out = count;
    return PATTERN_LIST_OK;
}

static PatternScanStatus pattern_contains_fvar(
    Atom *pattern, const char *wanted, CettaPrimeRegularKernelBudget *budget);

static PatternScanStatus pattern_list_contains_fvar(
    Atom *list, const char *wanted, CettaPrimeRegularKernelBudget *budget) {
    Atom *cursor = list;
    while (!atom_is_symbol(cursor, "LNil")) {
        if (!pattern_spend(budget)) return PATTERN_SCAN_BUDGET;
        if (!pattern_tag(cursor, "LCons", 3u)) return PATTERN_SCAN_INVALID;
        PatternScanStatus nested = pattern_contains_fvar(
            cursor->expr.elems[1], wanted, budget);
        if (nested != PATTERN_SCAN_OK) return nested;
        cursor = cursor->expr.elems[2];
    }
    return PATTERN_SCAN_OK;
}

static PatternScanStatus pattern_contains_fvar(
    Atom *pattern, const char *wanted, CettaPrimeRegularKernelBudget *budget) {
    if (!pattern_spend(budget)) return PATTERN_SCAN_BUDGET;
    /* Constructor metadata such as a private LevelParam identifier is a
     * literal leaf, not a free-variable occurrence.  The elaboration pass
     * still decides whether that literal is legal at its exact position. */
    if (pattern_natural(pattern, NULL)) return PATTERN_SCAN_OK;
    if (pattern_head_named(pattern, "DeclConst"))
        return pattern_validate_decl_const(pattern, budget);
    if (pattern_tag(pattern, "Var", 2u)) {
        return pattern_natural(pattern->expr.elems[1], NULL)
            ? PATTERN_SCAN_OK : PATTERN_SCAN_INVALID;
    }
    if (pattern_tag(pattern, "FVar", 2u)) {
        const char *name = NULL;
        if (!pattern_string(pattern->expr.elems[1], &name))
            return PATTERN_SCAN_INVALID;
        return strcmp(name, wanted) == 0 ? PATTERN_SCAN_FOUND : PATTERN_SCAN_OK;
    }
    if (pattern_tag(pattern, "PApp", 3u)) {
        if (!pattern_string(pattern->expr.elems[1], NULL))
            return PATTERN_SCAN_INVALID;
        return pattern_list_contains_fvar(
            pattern->expr.elems[2], wanted, budget);
    }
    if (pattern_tag(pattern, "PLam", 3u))
        return pattern_contains_fvar(pattern->expr.elems[2], wanted, budget);
    if (pattern_tag(pattern, "PMultiLam", 4u))
        return pattern_contains_fvar(pattern->expr.elems[3], wanted, budget);
    if (pattern_tag(pattern, "PSubst", 3u)) {
        PatternScanStatus body = pattern_contains_fvar(
            pattern->expr.elems[1], wanted, budget);
        return body == PATTERN_SCAN_OK
            ? pattern_contains_fvar(pattern->expr.elems[2], wanted, budget)
            : body;
    }
    if (pattern_tag(pattern, "PCollection", 4u))
        return pattern_list_contains_fvar(
            pattern->expr.elems[2], wanted, budget);
    return PATTERN_SCAN_INVALID;
}

static CettaPrimeRegularPatternElaborationV1 pattern_elaborate_rec(
    Arena *arena, CettaPrimeRegularPatternEnvironmentV1 environment,
    uint64_t binder_depth, uint64_t naming_depth, Atom *pattern,
    CettaPrimeRegularKernelBudget *budget);

static CettaPrimeRegularPatternElaborationV1 pattern_elaborate_binder(
    Arena *arena, CettaPrimeRegularPatternEnvironmentV1 environment,
    uint64_t binder_depth, uint64_t naming_depth, Atom *body,
    CettaPrimeRegularKernelBudget *budget) {
    if (binder_depth == UINT64_MAX || naming_depth == UINT64_MAX)
        return pattern_failure(
            CETTA_PRIME_REGULAR_PATTERN_RESOURCE_LIMIT,
            "regular-pattern-binder-depth");
    char buffer[64];
    int length = snprintf(
        buffer, sizeof(buffer), "__pk_%llu",
        (unsigned long long)naming_depth);
    if (length < 0 || (size_t)length >= sizeof(buffer))
        return pattern_failure(
            CETTA_PRIME_REGULAR_PATTERN_RESOURCE_LIMIT,
            "regular-pattern-binder-name");
    PatternScanStatus freshness = pattern_contains_fvar(body, buffer, budget);
    if (freshness == PATTERN_SCAN_FOUND)
        return pattern_syntax_failure(
            CETTA_PRIME_REGULAR_PATTERN_BINDER_NAME_COLLISION, 0u,
            arena_strdup(arena, buffer), 0u);
    if (freshness == PATTERN_SCAN_BUDGET)
        return pattern_failure(
            CETTA_PRIME_REGULAR_PATTERN_BUDGET_EXHAUSTED,
            "regular-pattern-freshness-budget");
    if (freshness == PATTERN_SCAN_INVALID)
        return pattern_failure(
            CETTA_PRIME_REGULAR_PATTERN_INVALID_WIRE,
            "malformed-pattern-under-binder");
    return pattern_elaborate_rec(
        arena, environment, binder_depth + 1u, naming_depth + 1u,
        body, budget);
}

static CettaPrimeRegularPatternElaborationV1 pattern_elaborate_rec(
    Arena *arena, CettaPrimeRegularPatternEnvironmentV1 environment,
    uint64_t binder_depth, uint64_t naming_depth, Atom *pattern,
    CettaPrimeRegularKernelBudget *budget) {
    if (!pattern_spend(budget))
        return pattern_failure(
            CETTA_PRIME_REGULAR_PATTERN_BUDGET_EXHAUSTED,
            "regular-pattern-elaboration-budget");
    if (pattern_head_named(pattern, "DeclConst")) {
        PatternScanStatus declaration = pattern_validate_decl_const(
            pattern, budget);
        if (declaration == PATTERN_SCAN_BUDGET)
            return pattern_failure(
                CETTA_PRIME_REGULAR_PATTERN_BUDGET_EXHAUSTED,
                "declaration-constant-elaboration-budget");
        if (declaration != PATTERN_SCAN_OK)
            return pattern_failure(
                CETTA_PRIME_REGULAR_PATTERN_INVALID_WIRE,
                "malformed-declaration-constant");
        return pattern_success(pattern);
    }
    if (pattern_tag(pattern, "Var", 2u)) {
        uint64_t index = 0u;
        if (!pattern_natural(pattern->expr.elems[1], &index))
            return pattern_failure(
                CETTA_PRIME_REGULAR_PATTERN_INVALID_WIRE,
                "malformed-bound-variable");
        if (index >= binder_depth)
            return pattern_syntax_failure(
                CETTA_PRIME_REGULAR_PATTERN_DANGLING_BOUND_VARIABLE,
                index, NULL, 0u);
        if (index > (uint64_t)INT64_MAX)
            return pattern_failure(
                CETTA_PRIME_REGULAR_PATTERN_RESOURCE_LIMIT,
                "regular-pattern-index-range");
        return pattern_success(atom_expr2(
            arena, atom_symbol(arena, "idx"),
            atom_int(arena, (int64_t)index)));
    }
    if (pattern_tag(pattern, "FVar", 2u)) {
        const char *name = NULL;
        if (!pattern_string(pattern->expr.elems[1], &name))
            return pattern_failure(
                CETTA_PRIME_REGULAR_PATTERN_INVALID_WIRE,
                "malformed-free-variable");
        for (size_t i = 0u; i < environment.count; i++) {
            const char *candidate = NULL;
            if (!pattern_string(environment.names[i], &candidate))
                return pattern_failure(
                    CETTA_PRIME_REGULAR_PATTERN_INVALID_ENVIRONMENT,
                    "malformed-quote-environment");
            if (strcmp(name, candidate) != 0) continue;
            if (binder_depth > UINT64_MAX - (uint64_t)i ||
                binder_depth + (uint64_t)i > (uint64_t)INT64_MAX)
                return pattern_failure(
                    CETTA_PRIME_REGULAR_PATTERN_RESOURCE_LIMIT,
                    "regular-pattern-index-range");
            return pattern_success(atom_expr2(
                arena, atom_symbol(arena, "idx"),
                atom_int(arena, (int64_t)(binder_depth + (uint64_t)i))));
        }
        return pattern_syntax_failure(
            CETTA_PRIME_REGULAR_PATTERN_UNKNOWN_FREE_VARIABLE,
            0u, name, 0u);
    }
    if (pattern_tag(pattern, "PApp", 3u)) {
        const char *name = NULL;
        Atom *arguments[3] = {NULL, NULL, NULL};
        size_t arity = 0u;
        if (!pattern_string(pattern->expr.elems[1], &name))
            return pattern_failure(
                CETTA_PRIME_REGULAR_PATTERN_INVALID_WIRE,
                "malformed-pattern-application");
        PatternListStatus arguments_status = pattern_collect_arguments(
            pattern->expr.elems[2], arguments, 3u, &arity, budget);
        if (arguments_status != PATTERN_LIST_OK) {
            CettaPrimeRegularPatternStatusV1 status =
                CETTA_PRIME_REGULAR_PATTERN_INVALID_WIRE;
            if (arguments_status == PATTERN_LIST_BUDGET)
                status = CETTA_PRIME_REGULAR_PATTERN_BUDGET_EXHAUSTED;
            else if (arguments_status == PATTERN_LIST_RESOURCE)
                status = CETTA_PRIME_REGULAR_PATTERN_RESOURCE_LIMIT;
            return pattern_failure(status, "malformed-pattern-application");
        }
        if ((strcmp(name, "U0") == 0 || strcmp(name, "U1") == 0) && arity == 0u)
            return pattern_success(atom_symbol(arena, name));
        if (strcmp(name, "LevelZero") == 0 && arity == 0u)
            return pattern_success(atom_expr2(
                arena, atom_symbol(arena, "LevelConst"),
                atom_int(arena, 0)));
        if (strcmp(name, "LevelParam") == 0 && arity == 1u) {
            uint64_t parameter = 0u;
            if (!pattern_natural(arguments[0], &parameter) ||
                parameter > (uint64_t)INT64_MAX)
                return pattern_syntax_failure(
                    CETTA_PRIME_REGULAR_PATTERN_MALFORMED_CONSTRUCTOR,
                    0u, name, arity);
            return pattern_success(atom_expr2(
                arena, atom_symbol(arena, "LevelParam"),
                atom_int(arena, (int64_t)parameter)));
        }
        if (strcmp(name, "LevelSucc") == 0 && arity == 1u) {
            CettaPrimeRegularPatternElaborationV1 level =
                pattern_elaborate_rec(
                    arena, environment, binder_depth, naming_depth,
                    arguments[0], budget);
            if (level.status != CETTA_PRIME_REGULAR_PATTERN_OK)
                return level;
            bool level_shape =
                pattern_tag(level.term, "LevelConst", 2u) ||
                pattern_tag(level.term, "LevelParam", 2u) ||
                pattern_tag(level.term, "LevelSucc", 2u) ||
                pattern_tag(level.term, "LevelMax", 3u);
            if (!level_shape)
                return pattern_syntax_failure(
                    CETTA_PRIME_REGULAR_PATTERN_MALFORMED_CONSTRUCTOR,
                    0u, name, arity);
            return pattern_success(atom_expr2(
                arena, atom_symbol(arena, "LevelSucc"), level.term));
        }
        if (strcmp(name, "Sort") == 0 && arity == 1u) {
            CettaPrimeRegularPatternElaborationV1 level =
                pattern_elaborate_rec(
                    arena, environment, binder_depth, naming_depth,
                    arguments[0], budget);
            if (level.status != CETTA_PRIME_REGULAR_PATTERN_OK)
                return level;
            bool level_shape =
                pattern_tag(level.term, "LevelConst", 2u) ||
                pattern_tag(level.term, "LevelParam", 2u) ||
                pattern_tag(level.term, "LevelSucc", 2u) ||
                pattern_tag(level.term, "LevelMax", 3u);
            if (!level_shape)
                return pattern_syntax_failure(
                    CETTA_PRIME_REGULAR_PATTERN_MALFORMED_CONSTRUCTOR,
                    0u, name, arity);
            return pattern_success(atom_expr2(
                arena, atom_symbol(arena, "Sort"), level.term));
        }
        if ((strcmp(name, "Pi") == 0 || strcmp(name, "Sigma") == 0) &&
            arity == 2u && pattern_tag(arguments[1], "PLam", 3u) &&
            atom_is_symbol(arguments[1]->expr.elems[1], "BNone")) {
            CettaPrimeRegularPatternElaborationV1 domain = pattern_elaborate_rec(
                arena, environment, binder_depth, naming_depth,
                arguments[0], budget);
            if (domain.status != CETTA_PRIME_REGULAR_PATTERN_OK) return domain;
            CettaPrimeRegularPatternElaborationV1 body = pattern_elaborate_binder(
                arena, environment, binder_depth, naming_depth,
                arguments[1]->expr.elems[2], budget);
            if (body.status != CETTA_PRIME_REGULAR_PATTERN_OK) return body;
            return pattern_success(atom_expr3(
                arena, atom_symbol(arena, name), domain.term, body.term));
        }
        if (strcmp(name, "Lam") == 0 && arity == 1u &&
            pattern_tag(arguments[0], "PLam", 3u) &&
            atom_is_symbol(arguments[0]->expr.elems[1], "BNone")) {
            CettaPrimeRegularPatternElaborationV1 body = pattern_elaborate_binder(
                arena, environment, binder_depth, naming_depth,
                arguments[0]->expr.elems[2], budget);
            if (body.status != CETTA_PRIME_REGULAR_PATTERN_OK) return body;
            return pattern_success(atom_expr2(
                arena, atom_symbol(arena, "Lam"), body.term));
        }
        size_t expected_arity = 0u;
        if (strcmp(name, "Id") == 0) expected_arity = 3u;
        else if (strcmp(name, "App") == 0 || strcmp(name, "Pair") == 0)
            expected_arity = 2u;
        else if (strcmp(name, "Fst") == 0 || strcmp(name, "Snd") == 0 ||
                 strcmp(name, "Refl") == 0)
            expected_arity = 1u;
        if (expected_arity != 0u && arity == expected_arity) {
            Atom *terms[3] = {NULL, NULL, NULL};
            for (size_t i = 0u; i < arity; i++) {
                CettaPrimeRegularPatternElaborationV1 nested =
                    pattern_elaborate_rec(
                        arena, environment, binder_depth, naming_depth,
                        arguments[i], budget);
                if (nested.status != CETTA_PRIME_REGULAR_PATTERN_OK)
                    return nested;
                terms[i] = nested.term;
            }
            Atom *head = atom_symbol(arena, name);
            if (arity == 1u) return pattern_success(atom_expr2(arena, head, terms[0]));
            if (arity == 2u)
                return pattern_success(atom_expr3(arena, head, terms[0], terms[1]));
            Atom *items[4] = {head, terms[0], terms[1], terms[2]};
            return pattern_success(atom_expr(arena, items, 4u));
        }
        return pattern_syntax_failure(
            CETTA_PRIME_REGULAR_PATTERN_MALFORMED_CONSTRUCTOR,
            0u, name, arity);
    }
    if (pattern_tag(pattern, "PLam", 3u))
        return pattern_syntax_failure(
            CETTA_PRIME_REGULAR_PATTERN_UNEXPECTED_BINDER, 0u, NULL, 0u);
    if (pattern_tag(pattern, "PMultiLam", 4u))
        return pattern_syntax_failure(
            CETTA_PRIME_REGULAR_PATTERN_UNSUPPORTED_MULTI_BINDER,
            0u, NULL, 0u);
    if (pattern_tag(pattern, "PSubst", 3u))
        return pattern_syntax_failure(
            CETTA_PRIME_REGULAR_PATTERN_UNSUPPORTED_EXPLICIT_SUBSTITUTION,
            0u, NULL, 0u);
    if (pattern_tag(pattern, "PCollection", 4u))
        return pattern_syntax_failure(
            CETTA_PRIME_REGULAR_PATTERN_UNSUPPORTED_COLLECTION,
            0u, NULL, 0u);
    return pattern_failure(
        CETTA_PRIME_REGULAR_PATTERN_INVALID_WIRE,
        "unknown-pattern-wire-constructor");
}

static CettaPrimeRegularPatternElaborationV1 pattern_elaborate_validated(
    Arena *arena, CettaPrimeRegularPatternEnvironmentV1 environment,
    Atom *pattern, CettaPrimeRegularKernelBudget *budget) {
    if (!arena || !pattern || !budget)
        return pattern_failure(
            CETTA_PRIME_REGULAR_PATTERN_INVALID_WIRE,
            "invalid-pattern-elaboration-input");
    return pattern_elaborate_rec(
        arena, environment, 0u, 0u, pattern, budget);
}

CettaPrimeRegularPatternElaborationV1
cetta_prime_regular_pattern_elaborate_v1(
    Arena *arena, CettaPrimeRegularPatternEnvironmentV1 environment,
    Atom *pattern, CettaPrimeRegularKernelBudget *budget) {
    CettaPrimeRegularPatternStatusV1 environment_status =
        pattern_validate_environment(environment, budget);
    if (environment_status != CETTA_PRIME_REGULAR_PATTERN_OK)
        return pattern_failure(
            environment_status,
            environment_status == CETTA_PRIME_REGULAR_PATTERN_BUDGET_EXHAUSTED
                ? "regular-pattern-environment-budget"
                : "invalid-regular-pattern-environment");
    return pattern_elaborate_validated(arena, environment, pattern, budget);
}

CettaPrimeRegularPatternCheckV1
cetta_prime_regular_pattern_elaborate_and_check_v1(
    Arena *arena, Atom *context,
    CettaPrimeRegularPatternEnvironmentV1 environment,
    Atom *term_pattern, Atom *expected_pattern,
    CettaPrimeRegularKernelBudget *budget) {
    CettaPrimeRegularPatternCheckV1 result = {0};
    CettaPrimeRegularPatternStatusV1 environment_status =
        pattern_validate_environment(environment, budget);
    if (environment_status != CETTA_PRIME_REGULAR_PATTERN_OK) {
        result.phase = CETTA_PRIME_REGULAR_PATTERN_PHASE_EXPECTED_SYNTAX;
        result.syntax = pattern_failure(
            environment_status, "invalid-regular-pattern-environment");
        return result;
    }
    CettaPrimeRegularPatternElaborationV1 expected =
        pattern_elaborate_validated(
            arena, environment, expected_pattern, budget);
    if (expected.status != CETTA_PRIME_REGULAR_PATTERN_OK) {
        result.phase = CETTA_PRIME_REGULAR_PATTERN_PHASE_EXPECTED_SYNTAX;
        result.syntax = expected;
        return result;
    }
    CettaPrimeRegularKernelPreparedExpectedResultV1 prepared =
        cetta_prime_regular_kernel_prepare_intrinsic_expected_v1(
            arena, context, expected.term, budget);
    if (prepared.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
        result.phase = CETTA_PRIME_REGULAR_PATTERN_PHASE_EXPECTED_FORMATION;
        result.expected = expected.term;
        result.judgment = (CettaPrimeRegularKernelResult){
            .status = prepared.status,
            .reason = prepared.reason,
        };
        return result;
    }
    CettaPrimeRegularPatternElaborationV1 term =
        pattern_elaborate_validated(
            arena, environment, term_pattern, budget);
    if (term.status != CETTA_PRIME_REGULAR_PATTERN_OK) {
        result.phase = CETTA_PRIME_REGULAR_PATTERN_PHASE_TERM_SYNTAX;
        result.syntax = term;
        result.expected = expected.term;
        return result;
    }
    result.judgment = cetta_prime_regular_kernel_check_prepared_intrinsic_v1(
        arena, prepared.prepared, term.term, budget);
    result.term = term.term;
    result.expected = expected.term;
    result.phase = result.judgment.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED
        ? CETTA_PRIME_REGULAR_PATTERN_PHASE_NONE
        : CETTA_PRIME_REGULAR_PATTERN_PHASE_TERM_TYPING;
    return result;
}

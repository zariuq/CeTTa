#include "inference_side_condition_provider.h"

#include "abt.h"
#include "symbol.h"

#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    AbtSignature signature;
    Arena *arena;
    CettaInferencePatternAbtStatusV1 status;
} PatternAbtContextV1;

static bool pattern_abt_tag(const Atom *atom, const char *tag, size_t len) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len == len &&
        atom_is_symbol(atom->expr.elems[0], tag);
}

static CettaInferencePatternAbtStatusV1 pattern_abt_natural(
    const Atom *atom, uint64_t *value) {
    const char *text;
    char *end = NULL;
    unsigned long long parsed;

    if (!atom || atom->kind != ATOM_GROUNDED)
        return CETTA_INFERENCE_PATTERN_ABT_INVALID;
    if (atom->ground.gkind == GV_INT) {
        if (atom->ground.ival < 0)
            return CETTA_INFERENCE_PATTERN_ABT_INVALID;
        *value = (uint64_t)atom->ground.ival;
        return CETTA_INFERENCE_PATTERN_ABT_OK;
    }
    if (atom->ground.gkind != GV_BIGINT)
        return CETTA_INFERENCE_PATTERN_ABT_INVALID;
    text = atom_bigint_cstr(atom);
    if (!text || !*text || *text == '-')
        return CETTA_INFERENCE_PATTERN_ABT_INVALID;
    errno = 0;
    parsed = strtoull(text, &end, 10);
    if (errno == ERANGE)
        return CETTA_INFERENCE_PATTERN_ABT_RESOURCE_LIMIT;
    if (!end || *end != '\0')
        return CETTA_INFERENCE_PATTERN_ABT_INVALID;
    *value = (uint64_t)parsed;
    return CETTA_INFERENCE_PATTERN_ABT_OK;
}

bool cetta_inference_pattern_collection_type_valid_v1(
    const Atom *collection_type) {
    const char *name = NULL;
    if (!collection_type || collection_type->kind != ATOM_GROUNDED ||
        collection_type->ground.gkind != GV_STRING)
        return false;
    name = collection_type->ground.sval;
    return name &&
        (strcmp(name, "Mettapedia.OSLF.MeTTaIL.Syntax.CollType.vec") == 0 ||
         strcmp(name, "Mettapedia.OSLF.MeTTaIL.Syntax.CollType.hashBag") == 0 ||
         strcmp(name, "Mettapedia.OSLF.MeTTaIL.Syntax.CollType.hashSet") == 0);
}

static bool pattern_abt_signature_add(
    PatternAbtContextV1 *context,
    SymbolId head,
    uint32_t arity,
    const uint32_t *depths) {
    AbtSignature *signature = &context->signature;
    if (abt_signature_lookup(signature, head, arity))
        return true;
    if (signature->len == signature->cap) {
        uint32_t next = signature->cap ? signature->cap * 2u : 8u;
        if (next < signature->cap ||
            (size_t)next > SIZE_MAX / sizeof(*signature->entries)) {
            context->status = CETTA_INFERENCE_PATTERN_ABT_RESOURCE_LIMIT;
            return false;
        }
        signature->entries = cetta_realloc(
            signature->entries, sizeof(*signature->entries) * (size_t)next);
        signature->cap = next;
    }
    AbtSignatureEntry *entry = &signature->entries[signature->len];
    entry->head = head;
    entry->arity = arity;
    entry->depths = arity
        ? cetta_malloc(sizeof(*entry->depths) * (size_t)arity) : NULL;
    if (arity)
        memcpy(entry->depths, depths,
               sizeof(*entry->depths) * (size_t)arity);
    signature->len++;
    return true;
}

static bool pattern_abt_context_init(PatternAbtContextV1 *context,
                                     Arena *arena) {
    const uint32_t lambda_depths[] = {0u, 1u};
    const uint32_t substitution_depths[] = {1u, 0u};
    memset(context, 0, sizeof(*context));
    abt_signature_init(&context->signature);
    context->arena = arena;
    context->status = CETTA_INFERENCE_PATTERN_ABT_OK;
    return pattern_abt_signature_add(
               context, symbol_intern_cstr(g_symbols, "PLam"), 2u,
               lambda_depths) &&
        pattern_abt_signature_add(
               context, symbol_intern_cstr(g_symbols, "PSubst"), 2u,
               substitution_depths);
}

static void pattern_abt_context_free(PatternAbtContextV1 *context) {
    abt_signature_free(&context->signature);
}

static Atom *pattern_abt_multi_head(PatternAbtContextV1 *context,
                                    uint32_t arity) {
    char name[64];
    int written = snprintf(
        name, sizeof(name), "$nik-abt-multi-body-v1-%" PRIu32, arity);
    if (written < 0 || (size_t)written >= sizeof(name)) {
        context->status = CETTA_INFERENCE_PATTERN_ABT_RESOURCE_LIMIT;
        return NULL;
    }
    Atom *head = atom_symbol(context->arena, name);
    const uint32_t depths[] = {arity};
    if (!head || !pattern_abt_signature_add(
            context, head->sym_id, 1u, depths))
        return NULL;
    return head;
}

static Atom *pattern_to_abt(PatternAbtContextV1 *context, Atom *pattern);

static Atom *pattern_list_to_abt(PatternAbtContextV1 *context, Atom *list) {
    if (atom_is_symbol(list, "LNil"))
        return list;
    if (!pattern_abt_tag(list, "LCons", 3u)) {
        context->status = CETTA_INFERENCE_PATTERN_ABT_INVALID;
        return NULL;
    }
    Atom *head = pattern_to_abt(context, list->expr.elems[1]);
    Atom *tail = pattern_list_to_abt(context, list->expr.elems[2]);
    if (!head || !tail)
        return NULL;
    return atom_expr3(
        context->arena, atom_symbol(context->arena, "LCons"), head, tail);
}

static Atom *pattern_to_abt(PatternAbtContextV1 *context, Atom *pattern) {
    uint64_t number;
    if (pattern_abt_tag(pattern, "Var", 2u)) {
        CettaInferencePatternAbtStatusV1 natural_status =
            pattern_abt_natural(pattern->expr.elems[1], &number);
        if (natural_status != CETTA_INFERENCE_PATTERN_ABT_OK) {
            context->status = natural_status;
            return NULL;
        }
        if (number > (uint64_t)INT64_MAX) {
            context->status = CETTA_INFERENCE_PATTERN_ABT_RESOURCE_LIMIT;
            return NULL;
        }
        return atom_expr2(
            context->arena, atom_symbol(context->arena, "idx"),
            atom_int(context->arena, (int64_t)number));
    }
    if (pattern_abt_tag(pattern, "FVar", 2u)) {
        context->status = CETTA_INFERENCE_PATTERN_ABT_INVALID;
        return NULL;
    }
    if (pattern_abt_tag(pattern, "PApp", 3u)) {
        Atom *arguments = pattern_list_to_abt(
            context, pattern->expr.elems[2]);
        if (!arguments)
            return NULL;
        return atom_expr3(
            context->arena, atom_symbol(context->arena, "PApp"),
            pattern->expr.elems[1], arguments);
    }
    if (pattern_abt_tag(pattern, "PLam", 3u)) {
        if (!atom_is_symbol(pattern->expr.elems[1], "BNone")) {
            context->status = CETTA_INFERENCE_PATTERN_ABT_INVALID;
            return NULL;
        }
        Atom *body = pattern_to_abt(context, pattern->expr.elems[2]);
        if (!body)
            return NULL;
        return atom_expr3(
            context->arena, atom_symbol(context->arena, "PLam"),
            pattern->expr.elems[1], body);
    }
    if (pattern_abt_tag(pattern, "PMultiLam", 4u)) {
        if (!atom_is_symbol(pattern->expr.elems[2], "LNil")) {
            context->status = CETTA_INFERENCE_PATTERN_ABT_INVALID;
            return NULL;
        }
        CettaInferencePatternAbtStatusV1 natural_status =
            pattern_abt_natural(pattern->expr.elems[1], &number);
        if (natural_status != CETTA_INFERENCE_PATTERN_ABT_OK) {
            context->status = natural_status;
            return NULL;
        }
        if (number > UINT32_MAX) {
            context->status = CETTA_INFERENCE_PATTERN_ABT_RESOURCE_LIMIT;
            return NULL;
        }
        Atom *dynamic_head = pattern_abt_multi_head(
            context, (uint32_t)number);
        Atom *body = pattern_to_abt(context, pattern->expr.elems[3]);
        if (!dynamic_head || !body)
            return NULL;
        Atom *bound = atom_expr2(
            context->arena, dynamic_head, body);
        Atom *items[] = {
            atom_symbol(context->arena, "$nik-abt-multi-v1"),
            pattern->expr.elems[1],
            bound,
        };
        return atom_expr(context->arena, items, 3u);
    }
    if (pattern_abt_tag(pattern, "PSubst", 3u)) {
        Atom *body = pattern_to_abt(context, pattern->expr.elems[1]);
        Atom *replacement = pattern_to_abt(context, pattern->expr.elems[2]);
        if (!body || !replacement)
            return NULL;
        return atom_expr3(
            context->arena, atom_symbol(context->arena, "PSubst"),
            body, replacement);
    }
    if (pattern_abt_tag(pattern, "PCollection", 4u)) {
        if (!cetta_inference_pattern_collection_type_valid_v1(
                pattern->expr.elems[1]) ||
            !atom_is_symbol(pattern->expr.elems[3], "RNone")) {
            context->status = CETTA_INFERENCE_PATTERN_ABT_INVALID;
            return NULL;
        }
        Atom *elements = pattern_list_to_abt(
            context, pattern->expr.elems[2]);
        if (!elements)
            return NULL;
        Atom *items[] = {
            atom_symbol(context->arena, "PCollection"),
            pattern->expr.elems[1],
            elements,
            pattern->expr.elems[3],
        };
        return atom_expr(context->arena, items, 4u);
    }
    context->status = CETTA_INFERENCE_PATTERN_ABT_INVALID;
    return NULL;
}

static Atom *abt_to_pattern(PatternAbtContextV1 *context, Atom *term);

static Atom *abt_list_to_pattern(PatternAbtContextV1 *context, Atom *list) {
    if (atom_is_symbol(list, "LNil"))
        return list;
    if (!pattern_abt_tag(list, "LCons", 3u)) {
        context->status = CETTA_INFERENCE_PATTERN_ABT_INVALID;
        return NULL;
    }
    Atom *head = abt_to_pattern(context, list->expr.elems[1]);
    Atom *tail = abt_list_to_pattern(context, list->expr.elems[2]);
    if (!head || !tail)
        return NULL;
    return atom_expr3(
        context->arena, atom_symbol(context->arena, "LCons"), head, tail);
}

static Atom *abt_to_pattern(PatternAbtContextV1 *context, Atom *term) {
    uint64_t number;
    if (pattern_abt_tag(term, "idx", 2u)) {
        CettaInferencePatternAbtStatusV1 natural_status =
            pattern_abt_natural(term->expr.elems[1], &number);
        if (natural_status != CETTA_INFERENCE_PATTERN_ABT_OK) {
            context->status = natural_status;
            return NULL;
        }
        if (number > (uint64_t)INT64_MAX) {
            context->status = CETTA_INFERENCE_PATTERN_ABT_RESOURCE_LIMIT;
            return NULL;
        }
        return atom_expr2(
            context->arena, atom_symbol(context->arena, "Var"),
            atom_int(context->arena, (int64_t)number));
    }
    if (pattern_abt_tag(term, "PApp", 3u)) {
        Atom *arguments = abt_list_to_pattern(
            context, term->expr.elems[2]);
        if (!arguments)
            return NULL;
        return atom_expr3(
            context->arena, atom_symbol(context->arena, "PApp"),
            term->expr.elems[1], arguments);
    }
    if (pattern_abt_tag(term, "PLam", 3u)) {
        if (!atom_is_symbol(term->expr.elems[1], "BNone")) {
            context->status = CETTA_INFERENCE_PATTERN_ABT_INVALID;
            return NULL;
        }
        Atom *body = abt_to_pattern(context, term->expr.elems[2]);
        if (!body)
            return NULL;
        return atom_expr3(
            context->arena, atom_symbol(context->arena, "PLam"),
            term->expr.elems[1], body);
    }
    if (pattern_abt_tag(term, "$nik-abt-multi-v1", 3u)) {
        CettaInferencePatternAbtStatusV1 natural_status =
            pattern_abt_natural(term->expr.elems[1], &number);
        if (natural_status != CETTA_INFERENCE_PATTERN_ABT_OK) {
            context->status = natural_status;
            return NULL;
        }
        if (number > UINT32_MAX) {
            context->status = CETTA_INFERENCE_PATTERN_ABT_RESOURCE_LIMIT;
            return NULL;
        }
        Atom *expected_head = pattern_abt_multi_head(
            context, (uint32_t)number);
        Atom *bound = term->expr.elems[2];
        if (!expected_head || !bound ||
            bound->kind != ATOM_EXPR || bound->expr.len != 2u ||
            bound->expr.elems[0]->kind != ATOM_SYMBOL ||
            bound->expr.elems[0]->sym_id != expected_head->sym_id) {
            context->status = CETTA_INFERENCE_PATTERN_ABT_INVALID;
            return NULL;
        }
        Atom *body = abt_to_pattern(context, bound->expr.elems[1]);
        if (!body)
            return NULL;
        Atom *items[] = {
            atom_symbol(context->arena, "PMultiLam"),
            term->expr.elems[1],
            atom_symbol(context->arena, "LNil"),
            body,
        };
        return atom_expr(context->arena, items, 4u);
    }
    if (pattern_abt_tag(term, "PSubst", 3u)) {
        Atom *body = abt_to_pattern(context, term->expr.elems[1]);
        Atom *replacement = abt_to_pattern(context, term->expr.elems[2]);
        if (!body || !replacement)
            return NULL;
        return atom_expr3(
            context->arena, atom_symbol(context->arena, "PSubst"),
            body, replacement);
    }
    if (pattern_abt_tag(term, "PCollection", 4u)) {
        if (!cetta_inference_pattern_collection_type_valid_v1(
                term->expr.elems[1]) ||
            !atom_is_symbol(term->expr.elems[3], "RNone")) {
            context->status = CETTA_INFERENCE_PATTERN_ABT_INVALID;
            return NULL;
        }
        Atom *elements = abt_list_to_pattern(
            context, term->expr.elems[2]);
        if (!elements)
            return NULL;
        Atom *items[] = {
            atom_symbol(context->arena, "PCollection"),
            term->expr.elems[1],
            elements,
            term->expr.elems[3],
        };
        return atom_expr(context->arena, items, 4u);
    }
    context->status = CETTA_INFERENCE_PATTERN_ABT_INVALID;
    return NULL;
}

CettaInferencePatternAbtStatusV1
cetta_inference_pattern_explicit_substitution_v1(
    Arena *arena,
    Atom *body,
    Atom *replacement,
    Atom **result) {
    PatternAbtContextV1 context;
    Atom *encoded_body;
    Atom *encoded_replacement;
    Atom *substituted;

    if (result)
        *result = NULL;
    if (!arena || !body || !replacement || !result || !g_symbols)
        return CETTA_INFERENCE_PATTERN_ABT_INVALID;
    if (!pattern_abt_context_init(&context, arena)) {
        CettaInferencePatternAbtStatusV1 status = context.status;
        pattern_abt_context_free(&context);
        return status;
    }
    encoded_body = pattern_to_abt(&context, body);
    encoded_replacement = pattern_to_abt(&context, replacement);
    if (!encoded_body || !encoded_replacement)
        goto done;
    substituted = abt_subst(
        &context.signature, arena, 0u, encoded_replacement, encoded_body);
    if (!substituted) {
        context.status = CETTA_INFERENCE_PATTERN_ABT_RESOURCE_LIMIT;
        goto done;
    }
    *result = abt_to_pattern(&context, substituted);

done:
    pattern_abt_context_free(&context);
    return *result ? CETTA_INFERENCE_PATTERN_ABT_OK : context.status;
}

CettaInferencePatternAbtStatusV1
cetta_inference_pattern_unused_binder_elimination_v1(
    Arena *arena,
    Atom *body,
    Atom **result) {
    PatternAbtContextV1 context;
    Atom *encoded_body;
    Atom *dropped;

    if (result)
        *result = NULL;
    if (!arena || !body || !result || !g_symbols)
        return CETTA_INFERENCE_PATTERN_ABT_INVALID;
    if (!pattern_abt_context_init(&context, arena)) {
        CettaInferencePatternAbtStatusV1 status = context.status;
        pattern_abt_context_free(&context);
        return status;
    }
    encoded_body = pattern_to_abt(&context, body);
    if (!encoded_body)
        goto done;
    dropped = abt_shift(&context.signature, arena, -1, 0u, encoded_body);
    if (!dropped) {
        context.status = CETTA_INFERENCE_PATTERN_ABT_NO_RESULT;
        goto done;
    }
    *result = abt_to_pattern(&context, dropped);

done:
    pattern_abt_context_free(&context);
    return *result ? CETTA_INFERENCE_PATTERN_ABT_OK : context.status;
}

CettaInferencePatternAbtStatusV1
cetta_inference_pattern_supported_at_v1(
    Arena *arena,
    uint64_t depth,
    Atom *pattern) {
    PatternAbtContextV1 context;
    Atom *encoded;

    if (!arena || !pattern || !g_symbols)
        return CETTA_INFERENCE_PATTERN_ABT_INVALID;
    if (!pattern_abt_context_init(&context, arena)) {
        CettaInferencePatternAbtStatusV1 status = context.status;
        pattern_abt_context_free(&context);
        return status;
    }
    encoded = pattern_to_abt(&context, pattern);
    if (encoded && !abt_scope_check(&context.signature, depth, encoded))
        context.status = CETTA_INFERENCE_PATTERN_ABT_INVALID;
    pattern_abt_context_free(&context);
    return encoded && context.status == CETTA_INFERENCE_PATTERN_ABT_OK
        ? CETTA_INFERENCE_PATTERN_ABT_OK : context.status;
}

static void side_condition_error(
    char *error, size_t error_size, const char *format, ...) {
    if (error && error_size > 0u) {
        va_list arguments;
        va_start(arguments, format);
        (void)vsnprintf(error, error_size, format, arguments);
        va_end(arguments);
    }
}

static CettaGsltProviderOutcomeV1 side_condition_answer(
    Arena *answer_arena,
    const Atom *goal,
    CettaGsltProviderAnswersV1 *answers,
    char *error,
    size_t error_size) {
    Atom *answer = atom_expr(
        answer_arena, goal->expr.elems, goal->expr.len);
    if (!answer || !cetta_gslt_provider_answers_push_v1(answers, answer)) {
        side_condition_error(
            error, error_size, "cannot allocate inference side-condition answer");
        return CETTA_GSLT_PROVIDER_FAULT;
    }
    return CETTA_GSLT_PROVIDER_COMPLETED;
}

static CettaGsltProviderOutcomeV1 name_distinct_query(
    void *context,
    Arena *answer_arena,
    const Atom *goal,
    uint64_t answer_limit,
    CettaGsltProviderAnswersV1 *answers,
    char *error,
    size_t error_size) {
    const Atom *left;
    const Atom *right;
    (void)context;
    if (!answer_arena || !answers ||
        !pattern_abt_tag(goal, "nik-name-distinct", 3u)) {
        side_condition_error(
            error, error_size, "invalid name-distinct request");
        return CETTA_GSLT_PROVIDER_FAULT;
    }
    left = goal->expr.elems[1];
    right = goal->expr.elems[2];
    if (!left || left->kind != ATOM_GROUNDED ||
        left->ground.gkind != GV_STRING || !left->ground.sval ||
        !right || right->kind != ATOM_GROUNDED ||
        right->ground.gkind != GV_STRING || !right->ground.sval) {
        side_condition_error(
            error, error_size, "name-distinct operands must be strings");
        return CETTA_GSLT_PROVIDER_FAULT;
    }
    if (answer_limit == 0u)
        return CETTA_GSLT_PROVIDER_ANSWER_LIMIT;
    if (strcmp(left->ground.sval, right->ground.sval) == 0)
        return CETTA_GSLT_PROVIDER_COMPLETED;
    return side_condition_answer(
        answer_arena, goal, answers, error, error_size);
}

static CettaGsltProviderOutcomeV1 explicit_substitution_query(
    void *context,
    Arena *answer_arena,
    const Atom *goal,
    uint64_t answer_limit,
    CettaGsltProviderAnswersV1 *answers,
    char *error,
    size_t error_size) {
    (void)context;
    if (!answer_arena || !answers ||
        !pattern_abt_tag(goal, "nik-explicit-substitution", 4u)) {
        side_condition_error(
            error, error_size, "invalid explicit-substitution request");
        return CETTA_GSLT_PROVIDER_FAULT;
    }
    if (answer_limit == 0u)
        return CETTA_GSLT_PROVIDER_ANSWER_LIMIT;
    Atom *result = NULL;
    CettaInferencePatternAbtStatusV1 status =
        cetta_inference_pattern_explicit_substitution_v1(
            answer_arena, goal->expr.elems[1], goal->expr.elems[2], &result);
    if (status == CETTA_INFERENCE_PATTERN_ABT_RESOURCE_LIMIT) {
        side_condition_error(
            error, error_size, "explicit substitution exceeds the ABT carrier");
        return CETTA_GSLT_PROVIDER_FAULT;
    }
    if (status == CETTA_INFERENCE_PATTERN_ABT_INVALID) {
        side_condition_error(
            error, error_size, "explicit substitution has malformed Pattern data");
        return CETTA_GSLT_PROVIDER_FAULT;
    }
    if (status != CETTA_INFERENCE_PATTERN_ABT_OK ||
        !atom_eq_fast(result, goal->expr.elems[3]))
        return CETTA_GSLT_PROVIDER_COMPLETED;
    return side_condition_answer(
        answer_arena, goal, answers, error, error_size);
}

static CettaGsltProviderOutcomeV1 unused_binder_query(
    void *context,
    Arena *answer_arena,
    const Atom *goal,
    uint64_t answer_limit,
    CettaGsltProviderAnswersV1 *answers,
    char *error,
    size_t error_size) {
    (void)context;
    if (!answer_arena || !answers ||
        !pattern_abt_tag(goal, "nik-unused-binder-elimination", 3u)) {
        side_condition_error(
            error, error_size, "invalid unused-binder request");
        return CETTA_GSLT_PROVIDER_FAULT;
    }
    if (answer_limit == 0u)
        return CETTA_GSLT_PROVIDER_ANSWER_LIMIT;
    Atom *result = NULL;
    CettaInferencePatternAbtStatusV1 status =
        cetta_inference_pattern_unused_binder_elimination_v1(
            answer_arena, goal->expr.elems[1], &result);
    if (status == CETTA_INFERENCE_PATTERN_ABT_RESOURCE_LIMIT) {
        side_condition_error(
            error, error_size, "unused-binder elimination exceeds the ABT carrier");
        return CETTA_GSLT_PROVIDER_FAULT;
    }
    if (status == CETTA_INFERENCE_PATTERN_ABT_INVALID) {
        side_condition_error(
            error, error_size, "unused-binder elimination has malformed Pattern data");
        return CETTA_GSLT_PROVIDER_FAULT;
    }
    if (status != CETTA_INFERENCE_PATTERN_ABT_OK ||
        !atom_eq_fast(result, goal->expr.elems[2]))
        return CETTA_GSLT_PROVIDER_COMPLETED;
    return side_condition_answer(
        answer_arena, goal, answers, error, error_size);
}

static CettaGsltProviderOutcomeV1 pattern_supported_at_query(
    void *context,
    Arena *answer_arena,
    const Atom *goal,
    uint64_t answer_limit,
    CettaGsltProviderAnswersV1 *answers,
    char *error,
    size_t error_size) {
    uint64_t depth = 0u;
    (void)context;
    if (!answer_arena || !answers ||
        !pattern_abt_tag(goal, "nik-pattern-supported-at", 3u)) {
        side_condition_error(
            error, error_size, "invalid Pattern-support request");
        return CETTA_GSLT_PROVIDER_FAULT;
    }
    CettaInferencePatternAbtStatusV1 natural_status =
        pattern_abt_natural(goal->expr.elems[1], &depth);
    if (natural_status == CETTA_INFERENCE_PATTERN_ABT_RESOURCE_LIMIT) {
        side_condition_error(
            error, error_size, "Pattern support depth exceeds the native carrier");
        return CETTA_GSLT_PROVIDER_FAULT;
    }
    if (natural_status != CETTA_INFERENCE_PATTERN_ABT_OK)
        return CETTA_GSLT_PROVIDER_COMPLETED;
    if (answer_limit == 0u)
        return CETTA_GSLT_PROVIDER_ANSWER_LIMIT;
    CettaInferencePatternAbtStatusV1 status =
        cetta_inference_pattern_supported_at_v1(
            answer_arena, depth, goal->expr.elems[2]);
    if (status == CETTA_INFERENCE_PATTERN_ABT_RESOURCE_LIMIT) {
        side_condition_error(
            error, error_size, "Pattern support check exceeds the ABT carrier");
        return CETTA_GSLT_PROVIDER_FAULT;
    }
    if (status != CETTA_INFERENCE_PATTERN_ABT_OK)
        return CETTA_GSLT_PROVIDER_COMPLETED;
    return side_condition_answer(
        answer_arena, goal, answers, error, error_size);
}

const CettaGsltProviderRegistryV1 *
cetta_inference_side_condition_provider_registry_v1(void) {
    static const CettaGsltProviderV1 providers[] = {
        {
            .relation = "nik-explicit-substitution",
            .arity = 3u,
            .semantic_id = "nik.pattern.explicit-substitution.v1",
            .context = NULL,
            .query = explicit_substitution_query,
        },
        {
            .relation = "nik-unused-binder-elimination",
            .arity = 2u,
            .semantic_id = "nik.pattern.unused-binder-elimination.v1",
            .context = NULL,
            .query = unused_binder_query,
        },
        {
            .relation = "nik-pattern-supported-at",
            .arity = 2u,
            .semantic_id = "nik.pattern.supported-at.v1",
            .context = NULL,
            .query = pattern_supported_at_query,
        },
        {
            .relation = "nik-name-distinct",
            .arity = 2u,
            .semantic_id = "nik.name.distinct.v1",
            .context = NULL,
            .query = name_distinct_query,
        },
    };
    static const CettaGsltProviderRegistryV1 registry = {
        .providers = providers,
        .provider_count = sizeof(providers) / sizeof(providers[0]),
    };
    return &registry;
}

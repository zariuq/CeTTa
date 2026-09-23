#include "inference_side_condition_provider.h"

#include "abt.h"
#include "symbol.h"

#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    PATTERN_ABT_ENCODE, PATTERN_ABT_ENCODE_LIST,
    PATTERN_ABT_DECODE, PATTERN_ABT_DECODE_LIST,
} PatternAbtMode;

typedef struct {
    Atom *source;
    Atom *result; /* NULL while this node is on the active traversal path. */
    PatternAbtMode mode;
} PatternAbtMemoEntry;

typedef struct {
    AbtSignature signature;
    Arena *arena;
    CettaInferencePatternAbtStatusV1 status;
    PatternAbtMemoEntry *memo;
    size_t memo_count, memo_capacity;
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
    free(context->memo);
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

/* Conversion is structural, independent of the ambient binder depth.  Cache
 * both list and Pattern nodes, separately in each direction.  Entries live only
 * for this operation: they neither survive arena resets nor certify admission. */
static size_t pattern_abt_hash(Atom *source, PatternAbtMode mode) {
    uint64_t h = (uint64_t)(uintptr_t)source ^
        ((uint64_t)mode * UINT64_C(0x9e3779b97f4a7c15));
    h ^= h >> 33;
    h *= UINT64_C(0xff51afd7ed558ccd);
    return (size_t)(h ^ (h >> 33));
}

static PatternAbtMemoEntry *pattern_abt_find(
    PatternAbtContextV1 *context, Atom *source, PatternAbtMode mode) {
    if (!context->memo_capacity)
        return NULL;
    size_t slot = pattern_abt_hash(source, mode) & (context->memo_capacity - 1u);
    while (context->memo[slot].source) {
        if (context->memo[slot].source == source &&
            context->memo[slot].mode == mode)
            return &context->memo[slot];
        slot = (slot + 1u) & (context->memo_capacity - 1u);
    }
    return NULL;
}

static bool pattern_abt_begin(
    PatternAbtContextV1 *context, Atom *source, PatternAbtMode mode) {
    if (!context->memo_capacity ||
        context->memo_count >= context->memo_capacity / 2u) {
        size_t next = context->memo_capacity ? context->memo_capacity * 2u : 64u;
        if (next < context->memo_capacity ||
            next > SIZE_MAX / sizeof(*context->memo)) {
            context->status = CETTA_INFERENCE_PATTERN_ABT_RESOURCE_LIMIT;
            return false;
        }
        PatternAbtMemoEntry *entries = cetta_malloc(next * sizeof(*entries));
        memset(entries, 0, next * sizeof(*entries));
        for (size_t i = 0u; i < context->memo_capacity; ++i) {
            PatternAbtMemoEntry old = context->memo[i];
            if (!old.source)
                continue;
            size_t slot = pattern_abt_hash(old.source, old.mode) & (next - 1u);
            while (entries[slot].source)
                slot = (slot + 1u) & (next - 1u);
            entries[slot] = old;
        }
        free(context->memo);
        context->memo = entries;
        context->memo_capacity = next;
    }
    size_t slot = pattern_abt_hash(source, mode) & (context->memo_capacity - 1u);
    while (context->memo[slot].source)
        slot = (slot + 1u) & (context->memo_capacity - 1u);
    context->memo[slot] = (PatternAbtMemoEntry){source, NULL, mode};
    context->memo_count++;
    return true;
}

typedef enum {
    PATTERN_ABT_LEAF, PATTERN_ABT_EXPR, PATTERN_ABT_MULTI,
} PatternAbtRecipe;

typedef struct {
    Atom *source;
    PatternAbtMode mode;
    PatternAbtRecipe recipe;
    Atom *items[4];
    Atom *children[2];
    PatternAbtMode child_modes[2];
    unsigned positions[2];
    unsigned arity, child_count, next_child;
    bool prepared;
} PatternAbtFrame;

static void pattern_abt_child(PatternAbtFrame *frame, unsigned position,
                               Atom *child, PatternAbtMode mode) {
    unsigned i = frame->child_count++;
    frame->positions[i] = position;
    frame->children[i] = child;
    frame->child_modes[i] = mode;
}

static bool pattern_abt_prepare(PatternAbtContextV1 *context,
                                 PatternAbtFrame *frame) {
    Atom *source = frame->source;
    bool decode = frame->mode >= PATTERN_ABT_DECODE;
    bool list = frame->mode == PATTERN_ABT_ENCODE_LIST ||
        frame->mode == PATTERN_ABT_DECODE_LIST;
    PatternAbtMode term_mode = decode ? PATTERN_ABT_DECODE : PATTERN_ABT_ENCODE;
    PatternAbtMode list_mode = decode ? PATTERN_ABT_DECODE_LIST : PATTERN_ABT_ENCODE_LIST;
    uint64_t number;
    frame->recipe = PATTERN_ABT_EXPR;
    if (list) {
        if (atom_is_symbol(source, "LNil")) {
            frame->recipe = PATTERN_ABT_LEAF;
            frame->items[0] = source;
            return true;
        }
        if (!pattern_abt_tag(source, "LCons", 3u))
            goto invalid;
        frame->arity = 3u;
        frame->items[0] = source->expr.elems[0];
        pattern_abt_child(frame, 1u, source->expr.elems[1], term_mode);
        pattern_abt_child(frame, 2u, source->expr.elems[2], list_mode);
        return true;
    }
    if (pattern_abt_tag(source, decode ? "idx" : "Var", 2u)) {
        context->status = pattern_abt_natural(source->expr.elems[1], &number);
        if (context->status != CETTA_INFERENCE_PATTERN_ABT_OK)
            return false;
        if (number > (uint64_t)INT64_MAX)
            goto resource;
        frame->arity = 2u;
        frame->items[0] = atom_symbol(context->arena, decode ? "Var" : "idx");
        frame->items[1] = atom_int(context->arena, (int64_t)number);
        return true;
    }
    if (pattern_abt_tag(source, "PApp", 3u)) {
        frame->arity = 3u;
        frame->items[0] = source->expr.elems[0];
        frame->items[1] = source->expr.elems[1];
        pattern_abt_child(frame, 2u, source->expr.elems[2], list_mode);
        return true;
    }
    if (pattern_abt_tag(source, "PLam", 3u)) {
        if (!atom_is_symbol(source->expr.elems[1], "BNone"))
            goto invalid;
        frame->arity = 3u;
        frame->items[0] = source->expr.elems[0];
        frame->items[1] = source->expr.elems[1];
        pattern_abt_child(frame, 2u, source->expr.elems[2], term_mode);
        return true;
    }
    if (pattern_abt_tag(source, decode ? "$nik-abt-multi-v1" : "PMultiLam",
                        decode ? 3u : 4u)) {
        if (!decode && !atom_is_symbol(source->expr.elems[2], "LNil"))
            goto invalid;
        context->status = pattern_abt_natural(source->expr.elems[1], &number);
        if (context->status != CETTA_INFERENCE_PATTERN_ABT_OK)
            return false;
        if (number > UINT32_MAX)
            goto resource;
        Atom *head = pattern_abt_multi_head(context, (uint32_t)number);
        if (!head)
            return false;
        frame->items[1] = source->expr.elems[1];
        if (decode) {
            Atom *bound = source->expr.elems[2];
            if (!bound || bound->kind != ATOM_EXPR || bound->expr.len != 2u ||
                !bound->expr.elems[0] || bound->expr.elems[0]->kind != ATOM_SYMBOL ||
                bound->expr.elems[0]->sym_id != head->sym_id)
                goto invalid;
            frame->arity = 4u;
            frame->items[0] = atom_symbol(context->arena, "PMultiLam");
            frame->items[2] = atom_symbol(context->arena, "LNil");
            pattern_abt_child(frame, 3u, bound->expr.elems[1], term_mode);
        } else {
            frame->recipe = PATTERN_ABT_MULTI;
            frame->arity = 3u;
            frame->items[0] = atom_symbol(context->arena, "$nik-abt-multi-v1");
            frame->items[3] = head;
            pattern_abt_child(frame, 2u, source->expr.elems[3], term_mode);
        }
        return true;
    }
    if (pattern_abt_tag(source, "PSubst", 3u)) {
        frame->arity = 3u;
        frame->items[0] = source->expr.elems[0];
        pattern_abt_child(frame, 1u, source->expr.elems[1], term_mode);
        pattern_abt_child(frame, 2u, source->expr.elems[2], term_mode);
        return true;
    }
    if (pattern_abt_tag(source, "PCollection", 4u)) {
        if (!cetta_inference_pattern_collection_type_valid_v1(source->expr.elems[1]) ||
            !atom_is_symbol(source->expr.elems[3], "RNone"))
            goto invalid;
        frame->arity = 4u;
        frame->items[0] = source->expr.elems[0];
        frame->items[1] = source->expr.elems[1];
        frame->items[3] = source->expr.elems[3];
        pattern_abt_child(frame, 2u, source->expr.elems[2], list_mode);
        return true;
    }
invalid:
    context->status = CETTA_INFERENCE_PATTERN_ABT_INVALID;
    return false;
resource:
    context->status = CETTA_INFERENCE_PATTERN_ABT_RESOURCE_LIMIT;
    return false;
}

static Atom *pattern_abt_convert(PatternAbtContextV1 *context, Atom *source,
                                 PatternAbtMode mode) {
    size_t count = 1u, capacity = 32u;
    PatternAbtFrame *frames = cetta_malloc(capacity * sizeof(*frames));
    frames[0] = (PatternAbtFrame){.source = source, .mode = mode};
    Atom *result = NULL;
    if (context->status != CETTA_INFERENCE_PATTERN_ABT_OK)
        goto done;
    while (count) {
        PatternAbtFrame *frame = &frames[count - 1u];
        if (!frame->prepared) {
            if (!frame->source) {
                context->status = CETTA_INFERENCE_PATTERN_ABT_INVALID;
                goto done;
            }
            PatternAbtMemoEntry *old = pattern_abt_find(
                context, frame->source, frame->mode);
            if (old) {
                if (!old->result) { /* A back edge, not a reusable result. */
                    context->status = CETTA_INFERENCE_PATTERN_ABT_INVALID;
                    goto done;
                }
                count--;
                continue;
            }
            if (!pattern_abt_begin(context, frame->source, frame->mode) ||
                !pattern_abt_prepare(context, frame))
                goto done;
            frame->prepared = true;
        }
        if (frame->next_child < frame->child_count) {
            unsigned i = frame->next_child++;
            PatternAbtFrame child = {
                .source = frame->children[i], .mode = frame->child_modes[i],
            };
            if (count == capacity) {
                if (capacity > SIZE_MAX / 2u / sizeof(*frames)) {
                    context->status = CETTA_INFERENCE_PATTERN_ABT_RESOURCE_LIMIT;
                    goto done;
                }
                capacity *= 2u;
                frames = cetta_realloc(frames, capacity * sizeof(*frames));
            }
            frames[count++] = child;
            continue;
        }
        for (unsigned i = 0u; i < frame->child_count; ++i)
            frame->items[frame->positions[i]] = pattern_abt_find(
                context, frame->children[i], frame->child_modes[i])->result;
        if (frame->recipe == PATTERN_ABT_MULTI)
            frame->items[2] = atom_expr2(
                context->arena, frame->items[3], frame->items[2]);
        Atom *converted = frame->recipe == PATTERN_ABT_LEAF ? frame->items[0] :
            atom_expr(context->arena, frame->items, frame->arity);
        pattern_abt_find(context, frame->source, frame->mode)->result = converted;
        count--;
    }
    result = pattern_abt_find(context, source, mode)->result;
done:
    free(frames);
    return result;
}

static Atom *pattern_to_abt(PatternAbtContextV1 *context, Atom *pattern) {
    return pattern_abt_convert(context, pattern, PATTERN_ABT_ENCODE);
}

static Atom *abt_to_pattern(PatternAbtContextV1 *context, Atom *term) {
    return pattern_abt_convert(context, term, PATTERN_ABT_DECODE);
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

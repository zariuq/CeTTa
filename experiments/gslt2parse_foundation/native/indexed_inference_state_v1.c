#include "indexed_inference_state_v1.h"

#include "native_sha256.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    PPINDEXED_INFERENCE_V1_NONE = UINT32_MAX,
    PPINDEXED_INFERENCE_V1_INITIAL_CAP = 16u
};

typedef enum {
    PPINDEXED_INFERENCE_V1_OBJECT_CONSTANT = 0,
    PPINDEXED_INFERENCE_V1_OBJECT_VARIABLE = 1,
    PPINDEXED_INFERENCE_V1_OBJECT_FLOATING = 2,
    PPINDEXED_INFERENCE_V1_OBJECT_ESSENTIAL = 3,
    PPINDEXED_INFERENCE_V1_OBJECT_RULE = 4
} PPIndexedInferenceV1ObjectKind;

typedef struct {
    uint8_t *bytes;
    uint32_t len;
    uint64_t hash;
} PPIndexedInferenceV1Atom;

typedef struct {
    uint32_t *symbols;
    uint32_t len;
} PPIndexedInferenceV1Formula;

typedef struct {
    uint32_t left;
    uint32_t right;
} PPIndexedInferenceV1Pair;

typedef struct {
    uint32_t *hypotheses;
    uint32_t hypothesis_len;
    PPIndexedInferenceV1Pair *distinct;
    uint32_t distinct_len;
} PPIndexedInferenceV1Frame;

typedef struct {
    PPIndexedInferenceV1ObjectKind kind;
    uint32_t label;
    union {
        PPIndexedInferenceV1Formula hypothesis;
        struct {
            PPIndexedInferenceV1Formula formula;
            PPIndexedInferenceV1Frame frame;
        } rule;
    } payload;
} PPIndexedInferenceV1Object;

typedef struct {
    uint32_t hypothesis_len;
    uint32_t distinct_len;
} PPIndexedInferenceV1Scope;

typedef struct {
    PPIndexedInferenceV1Atom *atoms;
    uint32_t atom_len;
    uint32_t atom_cap;
    uint32_t *atom_buckets;
    uint32_t atom_bucket_len;
    uint32_t *object_by_atom;
    PPIndexedInferenceV1Object *objects;
    uint32_t object_len;
    uint32_t object_cap;
    uint32_t *active_hypotheses;
    uint32_t active_hypothesis_len;
    uint32_t active_hypothesis_cap;
    PPIndexedInferenceV1Pair *active_distinct;
    uint32_t active_distinct_len;
    uint32_t active_distinct_cap;
    PPIndexedInferenceV1Scope *scopes;
    uint32_t scope_len;
    uint32_t scope_cap;
} PPIndexedInferenceV1Impl;

typedef struct {
    PPIndexedInferenceV1Formula *items;
    uint32_t len;
    uint32_t cap;
} PPIndexedInferenceV1FormulaStack;

typedef enum {
    PPINDEXED_INFERENCE_V1_HEAP_FORMULA = 0,
    PPINDEXED_INFERENCE_V1_HEAP_RULE = 1
} PPIndexedInferenceV1HeapKind;

typedef struct {
    PPIndexedInferenceV1HeapKind kind;
    PPIndexedInferenceV1Formula formula;
    uint32_t rule_object;
    uint32_t source_object;
} PPIndexedInferenceV1HeapEntry;

typedef struct {
    PPIndexedInferenceV1HeapEntry *items;
    uint32_t len;
    uint32_t cap;
} PPIndexedInferenceV1Heap;

typedef struct {
    const PPGuardedLexCursorV1Program *program;
    const PPOccurrenceFoldV1Plan *occurrence_plan;
    const PPIndexedCheckerEffectV1Plan *effect_plan;
    PPIndexedInferenceV1Config config;
    PPIndexedInferenceV1SourceResolver source_resolver;
    PPIndexedInferenceV1State *target;
    PPIndexedInferenceV1State staged;
    PPOccurrenceFoldV1Backend trace_backend;
    CettaNativeSha256 proof_trace;
} PPIndexedInferenceV1FoldImpl;

static bool ppinf_v1_fold_apply(
    void *context,
    const PPOccurrenceFoldV1Step *step,
    char *error_buf,
    size_t error_buf_size);

static bool ppinf_v1_nested_commit(
    void *context,
    char *error_buf,
    size_t error_buf_size) {
    (void)context;
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    return true;
}

static void ppinf_v1_nested_abort(void *context) {
    (void)context;
}

static void ppinf_v1_set_error(char *buf,
                               size_t size,
                               const char *format,
                               ...) {
    va_list arguments;

    if (!buf || size == 0u)
        return;
    va_start(arguments, format);
    (void)vsnprintf(buf, size, format, arguments);
    va_end(arguments);
}

static bool ppinf_v1_array_fits(size_t count, size_t item_size) {
    return item_size == 0u || count <= SIZE_MAX / item_size;
}

static uint64_t ppinf_v1_hash_bytes(
    const uint8_t *bytes,
    size_t len) {
    uint64_t hash = UINT64_C(1469598103934665603);
    size_t index;

    for (index = 0u; index < len; index++) {
        hash ^= bytes[index];
        hash *= UINT64_C(1099511628211);
    }
    hash ^= (uint64_t)len;
    hash *= UINT64_C(1099511628211);
    return hash;
}

static bool ppinf_v1_grow_raw(void **data,
                              uint32_t *cap,
                              uint32_t needed,
                              size_t item_size) {
    uint32_t next_cap;
    void *next;

    if (needed <= *cap)
        return true;
    next_cap = *cap ? *cap : PPINDEXED_INFERENCE_V1_INITIAL_CAP;
    while (next_cap < needed) {
        if (next_cap > UINT32_MAX / 2u)
            return false;
        next_cap *= 2u;
    }
    if (!ppinf_v1_array_fits(next_cap, item_size))
        return false;
    next = realloc(*data, (size_t)next_cap * item_size);
    if (!next)
        return false;
    *data = next;
    *cap = next_cap;
    return true;
}

static PPIndexedInferenceV1Impl *ppinf_v1_impl_new(void) {
    return calloc(1u, sizeof(PPIndexedInferenceV1Impl));
}

static void ppinf_v1_formula_free(
    PPIndexedInferenceV1Formula *formula) {
    if (!formula)
        return;
    free(formula->symbols);
    memset(formula, 0, sizeof(*formula));
}

static void ppinf_v1_frame_free(PPIndexedInferenceV1Frame *frame) {
    if (!frame)
        return;
    free(frame->hypotheses);
    free(frame->distinct);
    memset(frame, 0, sizeof(*frame));
}

static void ppinf_v1_object_free(
    PPIndexedInferenceV1Object *object) {
    if (!object)
        return;
    if (object->kind == PPINDEXED_INFERENCE_V1_OBJECT_FLOATING ||
        object->kind == PPINDEXED_INFERENCE_V1_OBJECT_ESSENTIAL) {
        ppinf_v1_formula_free(&object->payload.hypothesis);
    } else if (object->kind == PPINDEXED_INFERENCE_V1_OBJECT_RULE) {
        ppinf_v1_formula_free(&object->payload.rule.formula);
        ppinf_v1_frame_free(&object->payload.rule.frame);
    }
    memset(object, 0, sizeof(*object));
}

static void ppinf_v1_impl_free(PPIndexedInferenceV1Impl *impl) {
    uint32_t index;

    if (!impl)
        return;
    for (index = 0u; index < impl->atom_len; index++)
        free(impl->atoms[index].bytes);
    for (index = 0u; index < impl->object_len; index++)
        ppinf_v1_object_free(&impl->objects[index]);
    free(impl->atoms);
    free(impl->atom_buckets);
    free(impl->object_by_atom);
    free(impl->objects);
    free(impl->active_hypotheses);
    free(impl->active_distinct);
    free(impl->scopes);
    free(impl);
}

void ppindexed_inference_v1_state_init(
    PPIndexedInferenceV1State *state) {
    if (state)
        memset(state, 0, sizeof(*state));
}

void ppindexed_inference_v1_state_free(
    PPIndexedInferenceV1State *state) {
    if (!state)
        return;
    ppinf_v1_impl_free(state->implementation);
    memset(state, 0, sizeof(*state));
}

static bool ppinf_v1_rehash(
    PPIndexedInferenceV1Impl *impl,
    uint32_t bucket_len) {
    uint32_t *buckets;
    uint32_t index;

    if (bucket_len < PPINDEXED_INFERENCE_V1_INITIAL_CAP ||
        (bucket_len & (bucket_len - 1u)) != 0u ||
        !ppinf_v1_array_fits(bucket_len, sizeof(*buckets))) {
        return false;
    }
    buckets = calloc(bucket_len, sizeof(*buckets));
    if (!buckets)
        return false;
    for (index = 0u; index < impl->atom_len; index++) {
        uint32_t slot =
            (uint32_t)impl->atoms[index].hash & (bucket_len - 1u);
        while (buckets[slot] != 0u)
            slot = (slot + 1u) & (bucket_len - 1u);
        buckets[slot] = index + 1u;
    }
    free(impl->atom_buckets);
    impl->atom_buckets = buckets;
    impl->atom_bucket_len = bucket_len;
    return true;
}

static int32_t ppinf_v1_atom_find(
    const PPIndexedInferenceV1Impl *impl,
    const uint8_t *bytes,
    uint32_t len,
    uint64_t hash) {
    uint32_t slot;
    uint32_t probes;

    if (!impl->atom_bucket_len)
        return -1;
    slot = (uint32_t)hash & (impl->atom_bucket_len - 1u);
    for (probes = 0u;
         probes < impl->atom_bucket_len;
         probes++) {
        uint32_t encoded = impl->atom_buckets[slot];
        const PPIndexedInferenceV1Atom *atom;

        if (encoded == 0u)
            return -1;
        atom = &impl->atoms[encoded - 1u];
        if (atom->hash == hash && atom->len == len &&
            (len == 0u || memcmp(atom->bytes, bytes, len) == 0)) {
            return (int32_t)(encoded - 1u);
        }
        slot = (slot + 1u) & (impl->atom_bucket_len - 1u);
    }
    return -1;
}

static bool ppinf_v1_atom_grow(
    PPIndexedInferenceV1Impl *impl,
    uint32_t needed) {
    uint32_t old_cap = impl->atom_cap;

    if (!ppinf_v1_grow_raw(
            (void **)&impl->atoms, &impl->atom_cap,
            needed, sizeof(*impl->atoms))) {
        return false;
    }
    if (impl->atom_cap != old_cap) {
        uint32_t *next = realloc(
            impl->object_by_atom,
            (size_t)impl->atom_cap *
                sizeof(*impl->object_by_atom));
        uint32_t index;

        if (!next)
            return false;
        impl->object_by_atom = next;
        for (index = old_cap; index < impl->atom_cap; index++)
            impl->object_by_atom[index] = PPINDEXED_INFERENCE_V1_NONE;
    }
    return true;
}

static bool ppinf_v1_intern(
    PPIndexedInferenceV1Impl *impl,
    const uint8_t *bytes,
    size_t raw_len,
    uint32_t *out) {
    uint64_t hash;
    int32_t found;
    uint8_t *copy;
    uint32_t slot;

    if (!impl || !out || (!bytes && raw_len > 0u) ||
        raw_len == 0u || raw_len > UINT32_MAX) {
        return false;
    }
    hash = ppinf_v1_hash_bytes(bytes, raw_len);
    found = ppinf_v1_atom_find(
        impl, bytes, (uint32_t)raw_len, hash);
    if (found >= 0) {
        *out = (uint32_t)found;
        return true;
    }
    if (!impl->atom_bucket_len &&
        !ppinf_v1_rehash(
            impl, PPINDEXED_INFERENCE_V1_INITIAL_CAP)) {
        return false;
    }
    if ((uint64_t)(impl->atom_len + 1u) * 10u >=
            (uint64_t)impl->atom_bucket_len * 7u &&
        !ppinf_v1_rehash(impl, impl->atom_bucket_len * 2u)) {
        return false;
    }
    if (!ppinf_v1_atom_grow(impl, impl->atom_len + 1u))
        return false;
    copy = malloc(raw_len);
    if (!copy)
        return false;
    memcpy(copy, bytes, raw_len);
    impl->atoms[impl->atom_len] = (PPIndexedInferenceV1Atom){
        .bytes = copy,
        .len = (uint32_t)raw_len,
        .hash = hash,
    };
    slot = (uint32_t)hash & (impl->atom_bucket_len - 1u);
    while (impl->atom_buckets[slot] != 0u)
        slot = (slot + 1u) & (impl->atom_bucket_len - 1u);
    impl->atom_buckets[slot] = impl->atom_len + 1u;
    *out = impl->atom_len++;
    return true;
}

static bool ppinf_v1_formula_copy(
    const PPIndexedInferenceV1Formula *source,
    PPIndexedInferenceV1Formula *out) {
    memset(out, 0, sizeof(*out));
    if (!source->len)
        return true;
    out->symbols = malloc(
        (size_t)source->len * sizeof(*out->symbols));
    if (!out->symbols)
        return false;
    memcpy(
        out->symbols, source->symbols,
        (size_t)source->len * sizeof(*out->symbols));
    out->len = source->len;
    return true;
}

static bool ppinf_v1_frame_copy(
    const PPIndexedInferenceV1Frame *source,
    PPIndexedInferenceV1Frame *out) {
    memset(out, 0, sizeof(*out));
    if (source->hypothesis_len) {
        out->hypotheses = malloc(
            (size_t)source->hypothesis_len *
                sizeof(*out->hypotheses));
        if (!out->hypotheses)
            goto fail;
        memcpy(
            out->hypotheses, source->hypotheses,
            (size_t)source->hypothesis_len *
                sizeof(*out->hypotheses));
        out->hypothesis_len = source->hypothesis_len;
    }
    if (source->distinct_len) {
        out->distinct = malloc(
            (size_t)source->distinct_len *
                sizeof(*out->distinct));
        if (!out->distinct)
            goto fail;
        memcpy(
            out->distinct, source->distinct,
            (size_t)source->distinct_len *
                sizeof(*out->distinct));
        out->distinct_len = source->distinct_len;
    }
    return true;

fail:
    ppinf_v1_frame_free(out);
    return false;
}

static bool ppinf_v1_object_copy(
    const PPIndexedInferenceV1Object *source,
    PPIndexedInferenceV1Object *out) {
    memset(out, 0, sizeof(*out));
    out->kind = source->kind;
    out->label = source->label;
    if (source->kind == PPINDEXED_INFERENCE_V1_OBJECT_FLOATING ||
        source->kind == PPINDEXED_INFERENCE_V1_OBJECT_ESSENTIAL) {
        return ppinf_v1_formula_copy(
            &source->payload.hypothesis,
            &out->payload.hypothesis);
    }
    if (source->kind == PPINDEXED_INFERENCE_V1_OBJECT_RULE) {
        if (!ppinf_v1_formula_copy(
                &source->payload.rule.formula,
                &out->payload.rule.formula) ||
            !ppinf_v1_frame_copy(
                &source->payload.rule.frame,
                &out->payload.rule.frame)) {
            ppinf_v1_object_free(out);
            return false;
        }
    }
    return true;
}

static bool ppinf_v1_copy_array(
    void **out,
    const void *source,
    uint32_t len,
    size_t item_size) {
    if (!len) {
        *out = NULL;
        return true;
    }
    *out = malloc((size_t)len * item_size);
    if (!*out)
        return false;
    memcpy(*out, source, (size_t)len * item_size);
    return true;
}

static bool ppinf_v1_state_clone(
    const PPIndexedInferenceV1State *source,
    PPIndexedInferenceV1State *out) {
    const PPIndexedInferenceV1Impl *from =
        source ? source->implementation : NULL;
    PPIndexedInferenceV1Impl *to = NULL;
    uint32_t index;

    ppindexed_inference_v1_state_init(out);
    to = ppinf_v1_impl_new();
    if (!to)
        goto fail;
    if (!from) {
        out->implementation = to;
        return true;
    }
    if (!ppinf_v1_atom_grow(to, from->atom_len))
        goto fail;
    for (index = 0u; index < from->atom_len; index++) {
        uint32_t copied;
        if (!ppinf_v1_intern(
                to, from->atoms[index].bytes,
                from->atoms[index].len, &copied) ||
            copied != index) {
            goto fail;
        }
    }
    if (!ppinf_v1_grow_raw(
            (void **)&to->objects, &to->object_cap,
            from->object_len, sizeof(*to->objects))) {
        goto fail;
    }
    for (index = 0u; index < from->object_len; index++) {
        if (!ppinf_v1_object_copy(
                &from->objects[index], &to->objects[index])) {
            goto fail;
        }
        to->object_len++;
    }
    memcpy(
        to->object_by_atom, from->object_by_atom,
        (size_t)from->atom_len * sizeof(*to->object_by_atom));
    if (!ppinf_v1_copy_array(
            (void **)&to->active_hypotheses,
            from->active_hypotheses,
            from->active_hypothesis_len,
            sizeof(*to->active_hypotheses)) ||
        !ppinf_v1_copy_array(
            (void **)&to->active_distinct,
            from->active_distinct,
            from->active_distinct_len,
            sizeof(*to->active_distinct)) ||
        !ppinf_v1_copy_array(
            (void **)&to->scopes,
            from->scopes,
            from->scope_len,
            sizeof(*to->scopes))) {
        goto fail;
    }
    to->active_hypothesis_len = from->active_hypothesis_len;
    to->active_hypothesis_cap = from->active_hypothesis_len;
    to->active_distinct_len = from->active_distinct_len;
    to->active_distinct_cap = from->active_distinct_len;
    to->scope_len = from->scope_len;
    to->scope_cap = from->scope_len;
    out->implementation = to;
    memcpy(out->digest, source->digest, sizeof(out->digest));
    return true;

fail:
    ppinf_v1_impl_free(to);
    return false;
}

static bool ppinf_v1_object_is(
    const PPIndexedInferenceV1Impl *impl,
    uint32_t atom,
    PPIndexedInferenceV1ObjectKind kind) {
    uint32_t object;

    if (!impl || atom >= impl->atom_len)
        return false;
    object = impl->object_by_atom[atom];
    return object != PPINDEXED_INFERENCE_V1_NONE &&
        object < impl->object_len &&
        impl->objects[object].kind == kind;
}

static bool ppinf_v1_object_add(
    PPIndexedInferenceV1Impl *impl,
    PPIndexedInferenceV1Object object,
    uint32_t *out) {
    uint32_t index;

    if (!impl || object.label >= impl->atom_len ||
        impl->object_by_atom[object.label] !=
            PPINDEXED_INFERENCE_V1_NONE ||
        !ppinf_v1_grow_raw(
            (void **)&impl->objects, &impl->object_cap,
            impl->object_len + 1u,
            sizeof(*impl->objects))) {
        return false;
    }
    index = impl->object_len++;
    impl->objects[index] = object;
    impl->object_by_atom[object.label] = index;
    if (out)
        *out = index;
    return true;
}

static bool ppinf_v1_u32_push(
    uint32_t **data,
    uint32_t *len,
    uint32_t *cap,
    uint32_t value) {
    if (!ppinf_v1_grow_raw(
            (void **)data, cap, *len + 1u, sizeof(**data))) {
        return false;
    }
    (*data)[(*len)++] = value;
    return true;
}

static bool ppinf_v1_pair_push(
    PPIndexedInferenceV1Pair **data,
    uint32_t *len,
    uint32_t *cap,
    PPIndexedInferenceV1Pair value) {
    if (!ppinf_v1_grow_raw(
            (void **)data, cap, *len + 1u, sizeof(**data))) {
        return false;
    }
    (*data)[(*len)++] = value;
    return true;
}

static bool ppinf_v1_scope_push(
    PPIndexedInferenceV1Impl *impl) {
    if (!ppinf_v1_grow_raw(
            (void **)&impl->scopes, &impl->scope_cap,
            impl->scope_len + 1u, sizeof(*impl->scopes))) {
        return false;
    }
    impl->scopes[impl->scope_len++] =
        (PPIndexedInferenceV1Scope){
            .hypothesis_len = impl->active_hypothesis_len,
            .distinct_len = impl->active_distinct_len,
        };
    return true;
}

static bool ppinf_v1_scope_pop(
    PPIndexedInferenceV1Impl *impl) {
    PPIndexedInferenceV1Scope scope;

    if (!impl || impl->scope_len == 0u)
        return false;
    scope = impl->scopes[--impl->scope_len];
    if (scope.hypothesis_len > impl->active_hypothesis_len ||
        scope.distinct_len > impl->active_distinct_len) {
        return false;
    }
    impl->active_hypothesis_len = scope.hypothesis_len;
    impl->active_distinct_len = scope.distinct_len;
    return true;
}

static bool ppinf_v1_intern_value(
    PPIndexedInferenceV1Impl *impl,
    const PPOccurrenceFoldV1Value *value,
    uint32_t *out) {
    return value &&
        value->byte_len <= UINT32_MAX &&
        ppinf_v1_intern(impl, value->bytes, value->byte_len, out);
}

static bool ppinf_v1_formula_from_values(
    PPIndexedInferenceV1Impl *impl,
    const PPOccurrenceFoldV1Value *values,
    uint32_t len,
    PPIndexedInferenceV1Formula *out,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t index;

    memset(out, 0, sizeof(*out));
    if (!values || len == 0u ||
        !ppinf_v1_array_fits(len, sizeof(*out->symbols))) {
        ppinf_v1_set_error(
            error_buf, error_buf_size,
            "inference formula is empty or too large");
        return false;
    }
    out->symbols = malloc((size_t)len * sizeof(*out->symbols));
    if (!out->symbols)
        return false;
    out->len = len;
    for (index = 0u; index < len; index++) {
        uint32_t object;
        if (!ppinf_v1_intern_value(
                impl, &values[index], &out->symbols[index])) {
            goto fail;
        }
        object = impl->object_by_atom[out->symbols[index]];
        if (object == PPINDEXED_INFERENCE_V1_NONE ||
            object >= impl->object_len ||
            (impl->objects[object].kind !=
                 PPINDEXED_INFERENCE_V1_OBJECT_CONSTANT &&
             impl->objects[object].kind !=
                 PPINDEXED_INFERENCE_V1_OBJECT_VARIABLE)) {
            ppinf_v1_set_error(
                error_buf, error_buf_size,
                "formula contains an undeclared constructor or "
                "metavariable");
            goto fail;
        }
    }
    if (!ppinf_v1_object_is(
            impl, out->symbols[0],
            PPINDEXED_INFERENCE_V1_OBJECT_CONSTANT)) {
        ppinf_v1_set_error(
            error_buf, error_buf_size,
            "formula head is not a declared constructor");
        goto fail;
    }
    return true;

fail:
    ppinf_v1_formula_free(out);
    return false;
}

static bool ppinf_v1_active_floating_for(
    const PPIndexedInferenceV1Impl *impl,
    uint32_t variable,
    uint32_t *object_out) {
    uint32_t index;

    for (index = 0u;
         index < impl->active_hypothesis_len;
         index++) {
        uint32_t object_id = impl->active_hypotheses[index];
        const PPIndexedInferenceV1Object *object =
            &impl->objects[object_id];
        if (object->kind ==
                PPINDEXED_INFERENCE_V1_OBJECT_FLOATING &&
            object->payload.hypothesis.len == 2u &&
            object->payload.hypothesis.symbols[1] == variable) {
            if (object_out)
                *object_out = object_id;
            return true;
        }
    }
    return false;
}

static bool ppinf_v1_formula_respects_frame(
    const PPIndexedInferenceV1Impl *impl,
    const PPIndexedInferenceV1Formula *formula) {
    uint32_t index;

    for (index = 1u; index < formula->len; index++) {
        uint32_t symbol = formula->symbols[index];
        if (ppinf_v1_object_is(
                impl, symbol,
                PPINDEXED_INFERENCE_V1_OBJECT_VARIABLE) &&
            !ppinf_v1_active_floating_for(
                impl, symbol, NULL)) {
            return false;
        }
    }
    return true;
}

static bool ppinf_v1_declare_symbols(
    PPIndexedInferenceV1Impl *impl,
    const PPOccurrenceFoldV1Step *step,
    PPIndexedInferenceV1ObjectKind kind,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t index;

    if (!step->values || step->value_len == 0u)
        return false;
    for (index = 0u; index < step->value_len; index++) {
        uint32_t atom;
        uint32_t existing;
        PPIndexedInferenceV1Object object;

        if (!ppinf_v1_intern_value(impl, &step->values[index], &atom))
            return false;
        existing = impl->object_by_atom[atom];
        if (existing != PPINDEXED_INFERENCE_V1_NONE) {
            if (kind == PPINDEXED_INFERENCE_V1_OBJECT_VARIABLE &&
                existing < impl->object_len &&
                impl->objects[existing].kind == kind) {
                continue;
            }
            ppinf_v1_set_error(
                error_buf, error_buf_size,
                "declaration repeats an existing object identity");
            return false;
        }
        memset(&object, 0, sizeof(object));
        object.kind = kind;
        object.label = atom;
        if (!ppinf_v1_object_add(impl, object, NULL))
            return false;
    }
    return true;
}

static bool ppinf_v1_declare_distinct(
    PPIndexedInferenceV1Impl *impl,
    const PPOccurrenceFoldV1Step *step,
    uint32_t *added,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t *variables = NULL;
    uint32_t left;
    bool ok = false;

    *added = 0u;
    if (!step->values || step->value_len < 2u ||
        !ppinf_v1_array_fits(
            step->value_len, sizeof(*variables))) {
        ppinf_v1_set_error(
            error_buf, error_buf_size,
            "distinctness declaration needs two metavariables");
        return false;
    }
    variables = malloc(
        (size_t)step->value_len * sizeof(*variables));
    if (!variables)
        return false;
    for (left = 0u; left < step->value_len; left++) {
        uint32_t prior;
        if (!ppinf_v1_intern_value(
                impl, &step->values[left], &variables[left]) ||
            !ppinf_v1_object_is(
                impl, variables[left],
                PPINDEXED_INFERENCE_V1_OBJECT_VARIABLE)) {
            ppinf_v1_set_error(
                error_buf, error_buf_size,
                "distinctness operand is not a declared metavariable");
            goto done;
        }
        for (prior = 0u; prior < left; prior++) {
            if (variables[prior] == variables[left]) {
                ppinf_v1_set_error(
                    error_buf, error_buf_size,
                    "distinctness declaration repeats a metavariable");
                goto done;
            }
        }
    }
    for (left = 0u; left < step->value_len; left++) {
        uint32_t right;
        for (right = left + 1u; right < step->value_len; right++) {
            PPIndexedInferenceV1Pair pair = {
                .left = variables[left] < variables[right]
                    ? variables[left] : variables[right],
                .right = variables[left] < variables[right]
                    ? variables[right] : variables[left],
            };
            if (!ppinf_v1_pair_push(
                    &impl->active_distinct,
                    &impl->active_distinct_len,
                    &impl->active_distinct_cap,
                    pair)) {
                goto done;
            }
            (*added)++;
        }
    }
    ok = true;

done:
    free(variables);
    return ok;
}

static bool ppinf_v1_declare_hypothesis(
    PPIndexedInferenceV1Impl *impl,
    const PPOccurrenceFoldV1Step *step,
    bool essential,
    const PPIndexedInferenceV1Config *config,
    char *error_buf,
    size_t error_buf_size) {
    PPIndexedInferenceV1Object object;
    uint32_t label;
    uint32_t object_id;

    memset(&object, 0, sizeof(object));
    if (!step->values ||
        step->value_len < (essential ? 2u : 3u) ||
        (!essential && step->value_len != 3u) ||
        !ppinf_v1_intern_value(impl, &step->values[0], &label) ||
        impl->object_by_atom[label] !=
            PPINDEXED_INFERENCE_V1_NONE ||
        !ppinf_v1_formula_from_values(
            impl, step->values + 1u, step->value_len - 1u,
            &object.payload.hypothesis,
            error_buf, error_buf_size)) {
        goto fail;
    }
    if (essential) {
        if (!ppinf_v1_formula_respects_frame(
                impl, &object.payload.hypothesis)) {
            ppinf_v1_set_error(
                error_buf, error_buf_size,
                "essential hypothesis escapes the active frame");
            goto fail;
        }
        object.kind = PPINDEXED_INFERENCE_V1_OBJECT_ESSENTIAL;
    } else {
        uint32_t variable = object.payload.hypothesis.symbols[1];
        if (object.payload.hypothesis.len != 2u ||
            !ppinf_v1_object_is(
                impl, variable,
                PPINDEXED_INFERENCE_V1_OBJECT_VARIABLE)) {
            ppinf_v1_set_error(
                error_buf, error_buf_size,
                "floating hypothesis is not constructor/metavariable");
            goto fail;
        }
        if (!config->allow_duplicate_floating &&
            ppinf_v1_active_floating_for(impl, variable, NULL)) {
            ppinf_v1_set_error(
                error_buf, error_buf_size,
                "active frame already binds this metavariable");
            goto fail;
        }
        object.kind = PPINDEXED_INFERENCE_V1_OBJECT_FLOATING;
    }
    object.label = label;
    if (!ppinf_v1_object_add(impl, object, &object_id))
        goto fail;
    memset(&object, 0, sizeof(object));
    if (!ppinf_v1_u32_push(
            &impl->active_hypotheses,
            &impl->active_hypothesis_len,
            &impl->active_hypothesis_cap,
            object_id)) {
        return false;
    }
    return true;

fail:
    ppinf_v1_object_free(&object);
    return false;
}

static bool ppinf_v1_formula_mark_variables(
    const PPIndexedInferenceV1Impl *impl,
    const PPIndexedInferenceV1Formula *formula,
    bool *marked) {
    uint32_t index;

    for (index = 1u; index < formula->len; index++) {
        uint32_t symbol = formula->symbols[index];
        if (symbol >= impl->atom_len)
            return false;
        if (ppinf_v1_object_is(
                impl, symbol,
                PPINDEXED_INFERENCE_V1_OBJECT_VARIABLE)) {
            marked[symbol] = true;
        }
    }
    return true;
}

static bool ppinf_v1_trim_frame(
    const PPIndexedInferenceV1Impl *impl,
    const PPIndexedInferenceV1Formula *formula,
    PPIndexedInferenceV1Frame *out,
    char *error_buf,
    size_t error_buf_size) {
    bool *marked = NULL;
    bool *has_floating = NULL;
    uint32_t index;
    uint32_t hypothesis_cap = 0u;
    bool ok = false;

    memset(out, 0, sizeof(*out));
    marked = calloc(
        impl->atom_len ? impl->atom_len : 1u, sizeof(*marked));
    has_floating = calloc(
        impl->atom_len ? impl->atom_len : 1u,
        sizeof(*has_floating));
    if (!marked || !has_floating ||
        !ppinf_v1_formula_mark_variables(impl, formula, marked)) {
        goto done;
    }
    for (index = 0u;
         index < impl->active_hypothesis_len;
         index++) {
        const PPIndexedInferenceV1Object *hypothesis =
            &impl->objects[impl->active_hypotheses[index]];
        if (hypothesis->kind ==
                PPINDEXED_INFERENCE_V1_OBJECT_ESSENTIAL &&
            !ppinf_v1_formula_mark_variables(
                impl, &hypothesis->payload.hypothesis, marked)) {
            goto done;
        }
    }
    for (index = 0u;
         index < impl->active_hypothesis_len;
         index++) {
        uint32_t object_id = impl->active_hypotheses[index];
        const PPIndexedInferenceV1Object *hypothesis =
            &impl->objects[object_id];
        bool keep =
            hypothesis->kind ==
                PPINDEXED_INFERENCE_V1_OBJECT_ESSENTIAL;

        if (hypothesis->kind ==
                PPINDEXED_INFERENCE_V1_OBJECT_FLOATING) {
            uint32_t variable =
                hypothesis->payload.hypothesis.symbols[1];
            if (marked[variable]) {
                keep = true;
                has_floating[variable] = true;
            }
        }
        if (keep && !ppinf_v1_u32_push(
                &out->hypotheses, &out->hypothesis_len,
                &hypothesis_cap, object_id)) {
            goto done;
        }
    }
    for (index = 0u; index < impl->atom_len; index++) {
        if (marked[index] && !has_floating[index]) {
            ppinf_v1_set_error(
                error_buf, error_buf_size,
                "rule metavariable has no active floating hypothesis");
            goto done;
        }
    }
    if (impl->active_distinct_len) {
        uint32_t distinct_cap = 0u;
        for (index = 0u; index < impl->active_distinct_len; index++) {
            PPIndexedInferenceV1Pair pair =
                impl->active_distinct[index];
            if (marked[pair.left] && marked[pair.right] &&
                !ppinf_v1_pair_push(
                    &out->distinct, &out->distinct_len,
                    &distinct_cap, pair)) {
                goto done;
            }
        }
    }
    ok = true;

done:
    if (!ok)
        ppinf_v1_frame_free(out);
    free(has_floating);
    free(marked);
    return ok;
}

static bool ppinf_v1_declare_rule(
    PPIndexedInferenceV1Impl *impl,
    const PPOccurrenceFoldV1Step *step,
    char *error_buf,
    size_t error_buf_size) {
    PPIndexedInferenceV1Object object;
    uint32_t label;

    memset(&object, 0, sizeof(object));
    object.kind = PPINDEXED_INFERENCE_V1_OBJECT_RULE;
    if (!step->values || step->value_len < 2u ||
        !ppinf_v1_intern_value(impl, &step->values[0], &label) ||
        impl->object_by_atom[label] !=
            PPINDEXED_INFERENCE_V1_NONE ||
        !ppinf_v1_formula_from_values(
            impl, step->values + 1u, step->value_len - 1u,
            &object.payload.rule.formula,
            error_buf, error_buf_size) ||
        !ppinf_v1_trim_frame(
            impl, &object.payload.rule.formula,
            &object.payload.rule.frame,
            error_buf, error_buf_size)) {
        ppinf_v1_object_free(&object);
        return false;
    }
    object.label = label;
    if (!ppinf_v1_object_add(impl, object, NULL)) {
        ppinf_v1_object_free(&object);
        return false;
    }
    return true;
}

static void ppinf_v1_sha_u32(CettaNativeSha256 *sha, uint32_t value) {
    const uint8_t bytes[4] = {
        (uint8_t)(value >> 24u),
        (uint8_t)(value >> 16u),
        (uint8_t)(value >> 8u),
        (uint8_t)value,
    };
    cetta_native_sha256_update(sha, bytes, sizeof(bytes));
}

static void ppinf_v1_sha_atom(
    CettaNativeSha256 *sha,
    const PPIndexedInferenceV1Atom *atom) {
    ppinf_v1_sha_u32(sha, atom->len);
    cetta_native_sha256_update(sha, atom->bytes, atom->len);
}

static void ppinf_v1_sha_formula(
    CettaNativeSha256 *sha,
    const PPIndexedInferenceV1Formula *formula) {
    uint32_t index;

    ppinf_v1_sha_u32(sha, formula->len);
    for (index = 0u; index < formula->len; index++)
        ppinf_v1_sha_u32(sha, formula->symbols[index]);
}

static bool ppinf_v1_state_digest(
    const PPIndexedInferenceV1Impl *impl,
    char out[65]) {
    static const char domain[] = "IndexedInferenceStateV1";
    CettaNativeSha256 sha;
    uint32_t index;

    cetta_native_sha256_init(&sha);
    cetta_native_sha256_update(
        &sha, (const uint8_t *)domain, sizeof(domain) - 1u);
    ppinf_v1_sha_u32(&sha, impl->atom_len);
    for (index = 0u; index < impl->atom_len; index++)
        ppinf_v1_sha_atom(&sha, &impl->atoms[index]);
    ppinf_v1_sha_u32(&sha, impl->object_len);
    for (index = 0u; index < impl->object_len; index++) {
        const PPIndexedInferenceV1Object *object =
            &impl->objects[index];
        uint32_t item;

        ppinf_v1_sha_u32(&sha, (uint32_t)object->kind);
        ppinf_v1_sha_u32(&sha, object->label);
        if (object->kind ==
                PPINDEXED_INFERENCE_V1_OBJECT_FLOATING ||
            object->kind ==
                PPINDEXED_INFERENCE_V1_OBJECT_ESSENTIAL) {
            ppinf_v1_sha_formula(
                &sha, &object->payload.hypothesis);
        } else if (object->kind ==
                       PPINDEXED_INFERENCE_V1_OBJECT_RULE) {
            ppinf_v1_sha_formula(
                &sha, &object->payload.rule.formula);
            ppinf_v1_sha_u32(
                &sha,
                object->payload.rule.frame.hypothesis_len);
            for (item = 0u;
                 item <
                     object->payload.rule.frame.hypothesis_len;
                 item++) {
                ppinf_v1_sha_u32(
                    &sha,
                    object->payload.rule.frame.hypotheses[item]);
            }
            ppinf_v1_sha_u32(
                &sha, object->payload.rule.frame.distinct_len);
            for (item = 0u;
                 item < object->payload.rule.frame.distinct_len;
                 item++) {
                ppinf_v1_sha_u32(
                    &sha,
                    object->payload.rule.frame.distinct[item].left);
                ppinf_v1_sha_u32(
                    &sha,
                    object->payload.rule.frame.distinct[item].right);
            }
        }
    }
    ppinf_v1_sha_u32(&sha, impl->active_hypothesis_len);
    for (index = 0u;
         index < impl->active_hypothesis_len;
         index++) {
        ppinf_v1_sha_u32(&sha, impl->active_hypotheses[index]);
    }
    ppinf_v1_sha_u32(&sha, impl->active_distinct_len);
    for (index = 0u;
         index < impl->active_distinct_len;
         index++) {
        ppinf_v1_sha_u32(
            &sha, impl->active_distinct[index].left);
        ppinf_v1_sha_u32(
            &sha, impl->active_distinct[index].right);
    }
    ppinf_v1_sha_u32(&sha, impl->scope_len);
    for (index = 0u; index < impl->scope_len; index++) {
        ppinf_v1_sha_u32(
            &sha, impl->scopes[index].hypothesis_len);
        ppinf_v1_sha_u32(
            &sha, impl->scopes[index].distinct_len);
    }
    cetta_native_sha256_finish_hex(&sha, out);
    return true;
}

bool ppindexed_inference_v1_state_validate(
    const PPIndexedInferenceV1State *state,
    char *error_buf,
    size_t error_buf_size) {
    const PPIndexedInferenceV1Impl *impl;
    uint32_t index;
    char digest[65];

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!state || !(impl = state->implementation)) {
        ppinf_v1_set_error(
            error_buf, error_buf_size,
            "indexed inference state is absent");
        return false;
    }
    for (index = 0u; index < impl->atom_len; index++) {
        uint32_t object = impl->object_by_atom[index];
        if (!impl->atoms[index].bytes ||
            impl->atoms[index].len == 0u ||
            (object >= impl->object_len &&
             object != PPINDEXED_INFERENCE_V1_NONE)) {
            goto invalid;
        }
        if (object != PPINDEXED_INFERENCE_V1_NONE &&
            impl->objects[object].label != index) {
            goto invalid;
        }
    }
    for (index = 0u; index < impl->object_len; index++) {
        const PPIndexedInferenceV1Object *object =
            &impl->objects[index];
        uint32_t item;
        const PPIndexedInferenceV1Formula *formula = NULL;

        if (object->label >= impl->atom_len ||
            impl->object_by_atom[object->label] != index ||
            (uint32_t)object->kind >
                PPINDEXED_INFERENCE_V1_OBJECT_RULE) {
            goto invalid;
        }
        if (object->kind ==
                PPINDEXED_INFERENCE_V1_OBJECT_FLOATING ||
            object->kind ==
                PPINDEXED_INFERENCE_V1_OBJECT_ESSENTIAL) {
            formula = &object->payload.hypothesis;
        } else if (object->kind ==
                       PPINDEXED_INFERENCE_V1_OBJECT_RULE) {
            formula = &object->payload.rule.formula;
            for (item = 0u;
                 item <
                     object->payload.rule.frame.hypothesis_len;
                 item++) {
                uint32_t hypothesis =
                    object->payload.rule.frame.hypotheses[item];
                if (hypothesis >= impl->object_len ||
                    (impl->objects[hypothesis].kind !=
                         PPINDEXED_INFERENCE_V1_OBJECT_FLOATING &&
                     impl->objects[hypothesis].kind !=
                         PPINDEXED_INFERENCE_V1_OBJECT_ESSENTIAL)) {
                    goto invalid;
                }
            }
            for (item = 0u;
                 item < object->payload.rule.frame.distinct_len;
                 item++) {
                PPIndexedInferenceV1Pair pair =
                    object->payload.rule.frame.distinct[item];
                if (pair.left >= impl->atom_len ||
                    pair.right >= impl->atom_len ||
                    pair.left >= pair.right ||
                    !ppinf_v1_object_is(
                        impl, pair.left,
                        PPINDEXED_INFERENCE_V1_OBJECT_VARIABLE) ||
                    !ppinf_v1_object_is(
                        impl, pair.right,
                        PPINDEXED_INFERENCE_V1_OBJECT_VARIABLE)) {
                    goto invalid;
                }
            }
        }
        if (formula) {
            if (!formula->symbols || formula->len == 0u)
                goto invalid;
            for (item = 0u; item < formula->len; item++) {
                if (formula->symbols[item] >= impl->atom_len)
                    goto invalid;
            }
        }
    }
    for (index = 0u;
         index < impl->active_hypothesis_len;
         index++) {
        uint32_t object = impl->active_hypotheses[index];
        if (object >= impl->object_len ||
            (impl->objects[object].kind !=
                 PPINDEXED_INFERENCE_V1_OBJECT_FLOATING &&
             impl->objects[object].kind !=
                 PPINDEXED_INFERENCE_V1_OBJECT_ESSENTIAL)) {
            goto invalid;
        }
    }
    for (index = 0u; index < impl->active_distinct_len; index++) {
        PPIndexedInferenceV1Pair pair =
            impl->active_distinct[index];
        if (pair.left >= impl->atom_len ||
            pair.right >= impl->atom_len ||
            pair.left >= pair.right ||
            !ppinf_v1_object_is(
                impl, pair.left,
                PPINDEXED_INFERENCE_V1_OBJECT_VARIABLE) ||
            !ppinf_v1_object_is(
                impl, pair.right,
                PPINDEXED_INFERENCE_V1_OBJECT_VARIABLE)) {
            goto invalid;
        }
    }
    for (index = 0u; index < impl->scope_len; index++) {
        if (impl->scopes[index].hypothesis_len >
                impl->active_hypothesis_len ||
            impl->scopes[index].distinct_len >
                impl->active_distinct_len ||
            (index > 0u &&
             (impl->scopes[index - 1u].hypothesis_len >
                  impl->scopes[index].hypothesis_len ||
              impl->scopes[index - 1u].distinct_len >
                  impl->scopes[index].distinct_len))) {
            goto invalid;
        }
    }
    if (state->digest[0] != '\0' &&
        (!ppinf_v1_state_digest(impl, digest) ||
         strcmp(digest, state->digest) != 0)) {
        ppinf_v1_set_error(
            error_buf, error_buf_size,
            "indexed inference state digest changed");
        return false;
    }
    return true;

invalid:
    ppinf_v1_set_error(
        error_buf, error_buf_size,
        "indexed inference state is structurally invalid");
    return false;
}

bool ppindexed_inference_v1_state_summary(
    const PPIndexedInferenceV1State *state,
    PPIndexedInferenceV1Summary *out) {
    const PPIndexedInferenceV1Impl *impl;
    uint32_t index;

    if (!state || !out || !(impl = state->implementation))
        return false;
    memset(out, 0, sizeof(*out));
    out->atom_len = impl->atom_len;
    out->object_len = impl->object_len;
    out->active_hypothesis_len = impl->active_hypothesis_len;
    out->active_distinct_len = impl->active_distinct_len;
    out->scope_depth = impl->scope_len;
    for (index = 0u; index < impl->object_len; index++) {
        switch (impl->objects[index].kind) {
        case PPINDEXED_INFERENCE_V1_OBJECT_CONSTANT:
            out->constant_len++;
            break;
        case PPINDEXED_INFERENCE_V1_OBJECT_VARIABLE:
            out->variable_len++;
            break;
        case PPINDEXED_INFERENCE_V1_OBJECT_FLOATING:
        case PPINDEXED_INFERENCE_V1_OBJECT_ESSENTIAL:
            out->hypothesis_len++;
            break;
        case PPINDEXED_INFERENCE_V1_OBJECT_RULE:
            out->rule_len++;
            break;
        }
    }
    return true;
}

static bool ppinf_v1_formula_equal(
    const PPIndexedInferenceV1Formula *left,
    const PPIndexedInferenceV1Formula *right) {
    return left && right &&
        left->len == right->len &&
        (left->len == 0u ||
         memcmp(
             left->symbols, right->symbols,
             (size_t)left->len * sizeof(*left->symbols)) == 0);
}

static void ppinf_v1_stack_free(
    PPIndexedInferenceV1FormulaStack *stack) {
    uint32_t index;

    if (!stack)
        return;
    for (index = 0u; index < stack->len; index++)
        ppinf_v1_formula_free(&stack->items[index]);
    free(stack->items);
    memset(stack, 0, sizeof(*stack));
}

static bool ppinf_v1_stack_grow(
    PPIndexedInferenceV1FormulaStack *stack,
    uint32_t needed) {
    return ppinf_v1_grow_raw(
        (void **)&stack->items, &stack->cap,
        needed, sizeof(*stack->items));
}

static bool ppinf_v1_stack_push_copy(
    PPIndexedInferenceV1FormulaStack *stack,
    const PPIndexedInferenceV1Formula *formula) {
    if (!ppinf_v1_stack_grow(stack, stack->len + 1u) ||
        !ppinf_v1_formula_copy(
            formula, &stack->items[stack->len])) {
        return false;
    }
    stack->len++;
    return true;
}

static bool ppinf_v1_stack_push_take(
    PPIndexedInferenceV1FormulaStack *stack,
    PPIndexedInferenceV1Formula *formula) {
    if (!ppinf_v1_stack_grow(stack, stack->len + 1u))
        return false;
    stack->items[stack->len++] = *formula;
    memset(formula, 0, sizeof(*formula));
    return true;
}

static void ppinf_v1_stack_shrink(
    PPIndexedInferenceV1FormulaStack *stack,
    uint32_t len) {
    if (!stack || len > stack->len)
        return;
    while (stack->len > len)
        ppinf_v1_formula_free(&stack->items[--stack->len]);
}

static void ppinf_v1_heap_free(
    PPIndexedInferenceV1Heap *heap) {
    uint32_t index;

    if (!heap)
        return;
    for (index = 0u; index < heap->len; index++) {
        if (heap->items[index].kind ==
                PPINDEXED_INFERENCE_V1_HEAP_FORMULA) {
            ppinf_v1_formula_free(&heap->items[index].formula);
        }
    }
    free(heap->items);
    memset(heap, 0, sizeof(*heap));
}

static bool ppinf_v1_heap_push_formula(
    PPIndexedInferenceV1Heap *heap,
    const PPIndexedInferenceV1Formula *formula,
    uint32_t source_object) {
    PPIndexedInferenceV1HeapEntry entry;

    memset(&entry, 0, sizeof(entry));
    entry.kind = PPINDEXED_INFERENCE_V1_HEAP_FORMULA;
    entry.source_object = source_object;
    entry.rule_object = PPINDEXED_INFERENCE_V1_NONE;
    if (!ppinf_v1_formula_copy(formula, &entry.formula) ||
        !ppinf_v1_grow_raw(
            (void **)&heap->items, &heap->cap,
            heap->len + 1u, sizeof(*heap->items))) {
        ppinf_v1_formula_free(&entry.formula);
        return false;
    }
    heap->items[heap->len++] = entry;
    return true;
}

static bool ppinf_v1_heap_push_rule(
    PPIndexedInferenceV1Heap *heap,
    uint32_t object_id) {
    PPIndexedInferenceV1HeapEntry entry;

    memset(&entry, 0, sizeof(entry));
    entry.kind = PPINDEXED_INFERENCE_V1_HEAP_RULE;
    entry.rule_object = object_id;
    entry.source_object = object_id;
    if (!ppinf_v1_grow_raw(
            (void **)&heap->items, &heap->cap,
            heap->len + 1u, sizeof(*heap->items))) {
        return false;
    }
    heap->items[heap->len++] = entry;
    return true;
}

static bool ppinf_v1_active_hypothesis_contains(
    const PPIndexedInferenceV1Impl *impl,
    uint32_t object_id) {
    uint32_t index;

    for (index = 0u;
         index < impl->active_hypothesis_len;
         index++) {
        if (impl->active_hypotheses[index] == object_id)
            return true;
    }
    return false;
}

static bool ppinf_v1_active_distinct_contains(
    const PPIndexedInferenceV1Impl *impl,
    uint32_t left,
    uint32_t right) {
    uint32_t index;
    PPIndexedInferenceV1Pair wanted;

    if (left == right)
        return false;
    wanted = (PPIndexedInferenceV1Pair){
        .left = left < right ? left : right,
        .right = left < right ? right : left,
    };
    for (index = 0u; index < impl->active_distinct_len; index++) {
        PPIndexedInferenceV1Pair pair =
            impl->active_distinct[index];
        if (pair.left == wanted.left &&
            pair.right == wanted.right) {
            return true;
        }
    }
    return false;
}

static bool ppinf_v1_formula_substitute(
    const PPIndexedInferenceV1Impl *impl,
    const PPIndexedInferenceV1Formula *source,
    const int32_t *substitution,
    const PPIndexedInferenceV1FormulaStack *stack,
    PPIndexedInferenceV1Formula *out,
    char *error_buf,
    size_t error_buf_size) {
    uint64_t output_len = 0u;
    uint32_t source_index;
    uint32_t write = 0u;

    memset(out, 0, sizeof(*out));
    for (source_index = 0u;
         source_index < source->len;
         source_index++) {
        uint32_t symbol = source->symbols[source_index];
        if (ppinf_v1_object_is(
                impl, symbol,
                PPINDEXED_INFERENCE_V1_OBJECT_VARIABLE)) {
            int32_t stack_index = substitution[symbol];
            const PPIndexedInferenceV1Formula *replacement;

            if (stack_index < 0 ||
                (uint32_t)stack_index >= stack->len) {
                ppinf_v1_set_error(
                    error_buf, error_buf_size,
                    "inference substitution omits a metavariable");
                return false;
            }
            replacement = &stack->items[stack_index];
            if (replacement->len == 0u) {
                ppinf_v1_set_error(
                    error_buf, error_buf_size,
                    "inference substitution contains an empty formula");
                return false;
            }
            output_len += replacement->len - 1u;
        } else {
            output_len++;
        }
        if (output_len > UINT32_MAX)
            return false;
    }
    if (output_len == 0u ||
        !ppinf_v1_array_fits(
            (size_t)output_len, sizeof(*out->symbols))) {
        return false;
    }
    out->symbols = malloc(
        (size_t)output_len * sizeof(*out->symbols));
    if (!out->symbols)
        return false;
    out->len = (uint32_t)output_len;
    for (source_index = 0u;
         source_index < source->len;
         source_index++) {
        uint32_t symbol = source->symbols[source_index];
        if (ppinf_v1_object_is(
                impl, symbol,
                PPINDEXED_INFERENCE_V1_OBJECT_VARIABLE)) {
            const PPIndexedInferenceV1Formula *replacement =
                &stack->items[substitution[symbol]];
            uint32_t replacement_index;

            for (replacement_index = 1u;
                 replacement_index < replacement->len;
                 replacement_index++) {
                out->symbols[write++] =
                    replacement->symbols[replacement_index];
            }
        } else {
            out->symbols[write++] = symbol;
        }
    }
    return write == out->len;
}

static bool ppinf_v1_substitution_respects_distinct(
    const PPIndexedInferenceV1Impl *impl,
    const PPIndexedInferenceV1Frame *source_frame,
    const int32_t *substitution,
    const PPIndexedInferenceV1FormulaStack *stack,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t pair_index;

    for (pair_index = 0u;
         pair_index < source_frame->distinct_len;
         pair_index++) {
        PPIndexedInferenceV1Pair pair =
            source_frame->distinct[pair_index];
        int32_t left_index = substitution[pair.left];
        int32_t right_index = substitution[pair.right];
        const PPIndexedInferenceV1Formula *left;
        const PPIndexedInferenceV1Formula *right;
        uint32_t left_item;

        if (left_index < 0 || right_index < 0 ||
            (uint32_t)left_index >= stack->len ||
            (uint32_t)right_index >= stack->len) {
            ppinf_v1_set_error(
                error_buf, error_buf_size,
                "distinctness premise has no substitution image");
            return false;
        }
        left = &stack->items[left_index];
        right = &stack->items[right_index];
        for (left_item = 1u;
             left_item < left->len;
             left_item++) {
            uint32_t left_symbol = left->symbols[left_item];
            uint32_t right_item;

            if (!ppinf_v1_object_is(
                    impl, left_symbol,
                    PPINDEXED_INFERENCE_V1_OBJECT_VARIABLE) ||
                !ppinf_v1_active_floating_for(
                    impl, left_symbol, NULL)) {
                continue;
            }
            for (right_item = 1u;
                 right_item < right->len;
                 right_item++) {
                uint32_t right_symbol =
                    right->symbols[right_item];
                if (ppinf_v1_object_is(
                        impl, right_symbol,
                        PPINDEXED_INFERENCE_V1_OBJECT_VARIABLE) &&
                    ppinf_v1_active_floating_for(
                        impl, right_symbol, NULL) &&
                    !ppinf_v1_active_distinct_contains(
                        impl, left_symbol, right_symbol)) {
                    ppinf_v1_set_error(
                        error_buf, error_buf_size,
                        "distinctness side condition is not preserved");
                    return false;
                }
            }
        }
    }
    return true;
}

static bool ppinf_v1_apply_rule_object(
    PPIndexedInferenceV1Impl *impl,
    uint32_t object_id,
    PPIndexedInferenceV1FormulaStack *stack,
    char *error_buf,
    size_t error_buf_size) {
    const PPIndexedInferenceV1Object *rule;
    const PPIndexedInferenceV1Frame *frame;
    int32_t *substitution = NULL;
    uint32_t offset;
    uint32_t index;
    PPIndexedInferenceV1Formula conclusion;
    bool ok = false;

    memset(&conclusion, 0, sizeof(conclusion));
    if (object_id >= impl->object_len ||
        impl->objects[object_id].kind !=
            PPINDEXED_INFERENCE_V1_OBJECT_RULE) {
        ppinf_v1_set_error(
            error_buf, error_buf_size,
            "proof occurrence does not select an inference rule");
        return false;
    }
    rule = &impl->objects[object_id];
    frame = &rule->payload.rule.frame;
    if (frame->hypothesis_len > stack->len) {
        ppinf_v1_set_error(
            error_buf, error_buf_size,
            "inference rule underflows the proof stack");
        return false;
    }
    substitution = malloc(
        (size_t)(impl->atom_len ? impl->atom_len : 1u) *
            sizeof(*substitution));
    if (!substitution)
        return false;
    for (index = 0u; index < impl->atom_len; index++)
        substitution[index] = -1;
    offset = stack->len - frame->hypothesis_len;
    for (index = 0u; index < frame->hypothesis_len; index++) {
        uint32_t hypothesis_id = frame->hypotheses[index];
        const PPIndexedInferenceV1Object *hypothesis;
        const PPIndexedInferenceV1Formula *actual =
            &stack->items[offset + index];
        const PPIndexedInferenceV1Formula *expected;

        if (hypothesis_id >= impl->object_len ||
            (impl->objects[hypothesis_id].kind !=
                 PPINDEXED_INFERENCE_V1_OBJECT_FLOATING &&
             impl->objects[hypothesis_id].kind !=
                 PPINDEXED_INFERENCE_V1_OBJECT_ESSENTIAL) ||
            actual->len == 0u) {
            goto done;
        }
        hypothesis = &impl->objects[hypothesis_id];
        expected = &hypothesis->payload.hypothesis;
        if (expected->len == 0u ||
            actual->symbols[0] != expected->symbols[0]) {
            ppinf_v1_set_error(
                error_buf, error_buf_size,
                "inference hypothesis has the wrong constructor");
            goto done;
        }
        if (hypothesis->kind ==
                PPINDEXED_INFERENCE_V1_OBJECT_FLOATING) {
            uint32_t variable;

            if (expected->len != 2u)
                goto done;
            variable = expected->symbols[1];
            if (variable >= impl->atom_len ||
                substitution[variable] >= 0) {
                ppinf_v1_set_error(
                    error_buf, error_buf_size,
                    "inference rule binds a metavariable twice");
                goto done;
            }
            substitution[variable] = (int32_t)(offset + index);
        } else {
            PPIndexedInferenceV1Formula instantiated;

            memset(&instantiated, 0, sizeof(instantiated));
            if (!ppinf_v1_formula_substitute(
                    impl, expected, substitution, stack,
                    &instantiated, error_buf, error_buf_size) ||
                !ppinf_v1_formula_equal(
                    &instantiated, actual)) {
                ppinf_v1_formula_free(&instantiated);
                if (error_buf && error_buf_size > 0u &&
                    error_buf[0] == '\0') {
                    ppinf_v1_set_error(
                        error_buf, error_buf_size,
                        "essential inference premise does not match");
                }
                goto done;
            }
            ppinf_v1_formula_free(&instantiated);
        }
    }
    if (!ppinf_v1_substitution_respects_distinct(
            impl, frame, substitution, stack,
            error_buf, error_buf_size) ||
        !ppinf_v1_formula_substitute(
            impl, &rule->payload.rule.formula,
            substitution, stack, &conclusion,
            error_buf, error_buf_size)) {
        goto done;
    }
    ppinf_v1_stack_shrink(stack, offset);
    if (!ppinf_v1_stack_push_take(stack, &conclusion))
        goto done;
    ok = true;

done:
    ppinf_v1_formula_free(&conclusion);
    free(substitution);
    return ok;
}

static void ppinf_v1_proof_trace_step(
    PPIndexedInferenceV1Fold *fold,
    PPIndexedInferenceV1FoldImpl *implementation,
    uint32_t action_kind,
    uint32_t object_id,
    const PPOccurrenceFoldV1Value *value,
    uint32_t byte_offset,
    uint32_t stack_before,
    uint32_t stack_after) {
    uint8_t action_byte = (uint8_t)action_kind;

    cetta_native_sha256_update(
        &implementation->proof_trace, &action_byte, 1u);
    ppinf_v1_sha_u32(
        &implementation->proof_trace, object_id);
    ppinf_v1_sha_u32(
        &implementation->proof_trace,
        value ? value->source_index : UINT32_MAX);
    ppinf_v1_sha_u32(
        &implementation->proof_trace,
        value ? value->left_byte : UINT32_MAX);
    ppinf_v1_sha_u32(
        &implementation->proof_trace,
        value ? value->right_byte : UINT32_MAX);
    ppinf_v1_sha_u32(
        &implementation->proof_trace, byte_offset);
    ppinf_v1_sha_u32(
        &implementation->proof_trace, stack_before);
    ppinf_v1_sha_u32(
        &implementation->proof_trace, stack_after);
    if (value) {
        ppinf_v1_sha_u32(
            &implementation->proof_trace,
            (uint32_t)value->byte_len);
        cetta_native_sha256_update(
            &implementation->proof_trace,
            value->bytes, value->byte_len);
    } else {
        ppinf_v1_sha_u32(&implementation->proof_trace, 0u);
    }
    fold->receipt.proof_step_len++;
    if (action_kind == 1u)
        fold->receipt.proof_application_len++;
}

static bool ppinf_v1_apply_proof_object(
    PPIndexedInferenceV1Fold *fold,
    PPIndexedInferenceV1FoldImpl *implementation,
    uint32_t object_id,
    PPIndexedInferenceV1FormulaStack *stack,
    const PPOccurrenceFoldV1Value *value,
    uint32_t byte_offset,
    char *error_buf,
    size_t error_buf_size) {
    PPIndexedInferenceV1Impl *impl =
        implementation->staged.implementation;
    const PPIndexedInferenceV1Object *object;
    uint32_t before = stack->len;
    uint32_t action_kind;
    bool ok;

    if (object_id >= impl->object_len) {
        ppinf_v1_set_error(
            error_buf, error_buf_size,
            "proof label does not name a known object");
        return false;
    }
    object = &impl->objects[object_id];
    if (object->kind ==
            PPINDEXED_INFERENCE_V1_OBJECT_FLOATING ||
        object->kind ==
            PPINDEXED_INFERENCE_V1_OBJECT_ESSENTIAL) {
        if (!ppinf_v1_active_hypothesis_contains(
                impl, object_id)) {
            ppinf_v1_set_error(
                error_buf, error_buf_size,
                "proof hypothesis is outside the active scope");
            return false;
        }
        ok = ppinf_v1_stack_push_copy(
            stack, &object->payload.hypothesis);
        action_kind = 0u;
    } else if (object->kind ==
                   PPINDEXED_INFERENCE_V1_OBJECT_RULE) {
        ok = ppinf_v1_apply_rule_object(
            impl, object_id, stack,
            error_buf, error_buf_size);
        action_kind = 1u;
    } else {
        ppinf_v1_set_error(
            error_buf, error_buf_size,
            "proof label names a constructor or metavariable");
        return false;
    }
    if (!ok)
        return false;
    ppinf_v1_proof_trace_step(
        fold, implementation, action_kind, object_id,
        value, byte_offset, before, stack->len);
    return true;
}

static bool ppinf_v1_proof_label_object(
    PPIndexedInferenceV1Impl *impl,
    const PPOccurrenceFoldV1Value *value,
    uint32_t *object_id,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t atom;

    if (!ppinf_v1_intern_value(impl, value, &atom) ||
        atom >= impl->atom_len ||
        impl->object_by_atom[atom] ==
            PPINDEXED_INFERENCE_V1_NONE) {
        ppinf_v1_set_error(
            error_buf, error_buf_size,
            "proof label does not name an earlier occurrence");
        return false;
    }
    *object_id = impl->object_by_atom[atom];
    return true;
}

static bool ppinf_v1_value_is_unknown(
    const PPOccurrenceFoldV1Value *value) {
    return value && value->byte_len == 1u &&
        value->bytes && value->bytes[0] == (uint8_t)'?';
}

static bool ppinf_v1_apply_unknown(
    PPIndexedInferenceV1Fold *fold,
    PPIndexedInferenceV1FoldImpl *implementation,
    const PPIndexedInferenceV1Formula *claim,
    PPIndexedInferenceV1FormulaStack *stack,
    const PPOccurrenceFoldV1Value *value,
    uint32_t byte_offset,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t before = stack->len;

    if (implementation->config.reject_unknown_steps) {
        ppinf_v1_set_error(
            error_buf, error_buf_size,
            "unknown proof occurrence is rejected by policy");
        return false;
    }
    if (!ppinf_v1_stack_push_copy(stack, claim))
        return false;
    ppinf_v1_proof_trace_step(
        fold, implementation, 3u,
        PPINDEXED_INFERENCE_V1_NONE,
        value, byte_offset, before, stack->len);
    fold->receipt.unknown_step_len++;
    return true;
}

static bool ppinf_v1_insert_checked_rule(
    PPIndexedInferenceV1Impl *impl,
    uint32_t label,
    PPIndexedInferenceV1Formula *claim,
    PPIndexedInferenceV1Frame *frame) {
    PPIndexedInferenceV1Object object;

    memset(&object, 0, sizeof(object));
    object.kind = PPINDEXED_INFERENCE_V1_OBJECT_RULE;
    object.label = label;
    object.payload.rule.formula = *claim;
    object.payload.rule.frame = *frame;
    if (!ppinf_v1_object_add(impl, object, NULL))
        return false;
    memset(claim, 0, sizeof(*claim));
    memset(frame, 0, sizeof(*frame));
    return true;
}

static bool ppinf_v1_prepare_theorem(
    PPIndexedInferenceV1Impl *impl,
    const PPOccurrenceFoldV1Step *step,
    uint32_t formula_end,
    uint32_t *label,
    PPIndexedInferenceV1Formula *claim,
    PPIndexedInferenceV1Frame *frame,
    char *error_buf,
    size_t error_buf_size) {
    memset(claim, 0, sizeof(*claim));
    memset(frame, 0, sizeof(*frame));
    if (!step->values || formula_end <= 1u ||
        formula_end > step->value_len ||
        !ppinf_v1_intern_value(impl, &step->values[0], label) ||
        impl->object_by_atom[*label] !=
            PPINDEXED_INFERENCE_V1_NONE ||
        !ppinf_v1_formula_from_values(
            impl, step->values + 1u, formula_end - 1u,
            claim, error_buf, error_buf_size) ||
        !ppinf_v1_formula_respects_frame(impl, claim) ||
        !ppinf_v1_trim_frame(
            impl, claim, frame, error_buf, error_buf_size)) {
        if (error_buf && error_buf_size > 0u &&
            error_buf[0] == '\0') {
            ppinf_v1_set_error(
                error_buf, error_buf_size,
                "theorem occurrence has an invalid claim or frame");
        }
        ppinf_v1_formula_free(claim);
        ppinf_v1_frame_free(frame);
        return false;
    }
    return true;
}

static bool ppinf_v1_finish_theorem(
    PPIndexedInferenceV1Impl *impl,
    uint32_t label,
    PPIndexedInferenceV1Formula *claim,
    PPIndexedInferenceV1Frame *frame,
    PPIndexedInferenceV1FormulaStack *stack,
    char *error_buf,
    size_t error_buf_size) {
    if (stack->len != 1u ||
        !ppinf_v1_formula_equal(&stack->items[0], claim)) {
        ppinf_v1_set_error(
            error_buf, error_buf_size,
            stack->len == 1u
                ? "proof result differs from its theorem claim"
                : "proof leaves a non-singleton stack");
        return false;
    }
    if (!ppinf_v1_insert_checked_rule(
            impl, label, claim, frame)) {
        ppinf_v1_set_error(
            error_buf, error_buf_size,
            "cannot publish the checked theorem rule");
        return false;
    }
    return true;
}

static bool ppinf_v1_check_normal(
    PPIndexedInferenceV1Fold *fold,
    PPIndexedInferenceV1FoldImpl *implementation,
    const PPOccurrenceFoldV1Step *step,
    char *error_buf,
    size_t error_buf_size) {
    PPIndexedInferenceV1Impl *impl =
        implementation->staged.implementation;
    PPIndexedInferenceV1Formula claim;
    PPIndexedInferenceV1Frame frame;
    PPIndexedInferenceV1FormulaStack stack = {0};
    uint32_t formula_end;
    uint32_t proof_index;
    uint32_t label = 0u;
    uint32_t label_role;
    uint32_t formula_role;
    uint32_t proof_role;
    bool ok = false;

    memset(&claim, 0, sizeof(claim));
    memset(&frame, 0, sizeof(frame));
    if (!step->values || step->value_len < 3u)
        goto done;
    label_role = step->values[0].role_id;
    formula_role = step->values[1].role_id;
    if (label_role == formula_role)
        goto done;
    formula_end = 2u;
    while (formula_end < step->value_len &&
           step->values[formula_end].role_id == formula_role) {
        formula_end++;
    }
    if (formula_end >= step->value_len)
        goto done;
    proof_role = step->values[formula_end].role_id;
    if (proof_role == label_role || proof_role == formula_role)
        goto done;
    for (proof_index = formula_end;
         proof_index < step->value_len;
         proof_index++) {
        if (step->values[proof_index].role_id != proof_role)
            goto done;
    }
    if (!ppinf_v1_prepare_theorem(
            impl, step, formula_end, &label, &claim, &frame,
            error_buf, error_buf_size)) {
        goto done;
    }
    for (proof_index = formula_end;
         proof_index < step->value_len;
         proof_index++) {
        const PPOccurrenceFoldV1Value *value =
            &step->values[proof_index];
        uint32_t object_id;

        if (ppinf_v1_value_is_unknown(value)) {
            if (!ppinf_v1_apply_unknown(
                    fold, implementation, &claim, &stack,
                    value, 0u, error_buf, error_buf_size)) {
                goto done;
            }
        } else if (!ppinf_v1_proof_label_object(
                       impl, value, &object_id,
                       error_buf, error_buf_size) ||
                   !ppinf_v1_apply_proof_object(
                       fold, implementation, object_id,
                       &stack, value, 0u,
                       error_buf, error_buf_size)) {
            goto done;
        }
    }
    ok = ppinf_v1_finish_theorem(
        impl, label, &claim, &frame, &stack,
        error_buf, error_buf_size);

done:
    if (!ok && error_buf && error_buf_size > 0u &&
        error_buf[0] == '\0') {
        ppinf_v1_set_error(
            error_buf, error_buf_size,
            "normal theorem occurrence violates its checker schema");
    }
    ppinf_v1_stack_free(&stack);
    ppinf_v1_frame_free(&frame);
    ppinf_v1_formula_free(&claim);
    return ok;
}

static bool ppinf_v1_heap_preload_frame(
    const PPIndexedInferenceV1Impl *impl,
    const PPIndexedInferenceV1Frame *frame,
    PPIndexedInferenceV1Heap *heap) {
    uint32_t index;

    for (index = 0u; index < frame->hypothesis_len; index++) {
        uint32_t object_id = frame->hypotheses[index];
        if (object_id >= impl->object_len ||
            (impl->objects[object_id].kind !=
                 PPINDEXED_INFERENCE_V1_OBJECT_FLOATING &&
             impl->objects[object_id].kind !=
                 PPINDEXED_INFERENCE_V1_OBJECT_ESSENTIAL) ||
            !ppinf_v1_heap_push_formula(
                heap,
                &impl->objects[object_id].payload.hypothesis,
                object_id)) {
            return false;
        }
    }
    return true;
}

static bool ppinf_v1_heap_preload_label(
    PPIndexedInferenceV1Impl *impl,
    const PPOccurrenceFoldV1Value *value,
    PPIndexedInferenceV1Heap *heap,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t object_id;
    const PPIndexedInferenceV1Object *object;

    if (!ppinf_v1_proof_label_object(
            impl, value, &object_id,
            error_buf, error_buf_size)) {
        return false;
    }
    object = &impl->objects[object_id];
    if (object->kind ==
            PPINDEXED_INFERENCE_V1_OBJECT_FLOATING ||
        object->kind ==
            PPINDEXED_INFERENCE_V1_OBJECT_ESSENTIAL) {
        if (!ppinf_v1_active_hypothesis_contains(
                impl, object_id)) {
            ppinf_v1_set_error(
                error_buf, error_buf_size,
                "compressed proof preloads an inactive hypothesis");
            return false;
        }
        return ppinf_v1_heap_push_formula(
            heap, &object->payload.hypothesis, object_id);
    }
    if (object->kind ==
            PPINDEXED_INFERENCE_V1_OBJECT_RULE) {
        return ppinf_v1_heap_push_rule(heap, object_id);
    }
    ppinf_v1_set_error(
        error_buf, error_buf_size,
        "compressed proof label is not a hypothesis or rule");
    return false;
}

static bool ppinf_v1_apply_heap_entry(
    PPIndexedInferenceV1Fold *fold,
    PPIndexedInferenceV1FoldImpl *implementation,
    const PPIndexedInferenceV1HeapEntry *entry,
    PPIndexedInferenceV1FormulaStack *stack,
    const PPOccurrenceFoldV1Value *value,
    uint32_t byte_offset,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t before = stack->len;

    if (entry->kind == PPINDEXED_INFERENCE_V1_HEAP_RULE) {
        return ppinf_v1_apply_proof_object(
            fold, implementation, entry->rule_object,
            stack, value, byte_offset,
            error_buf, error_buf_size);
    }
    if (!ppinf_v1_stack_push_copy(stack, &entry->formula))
        return false;
    ppinf_v1_proof_trace_step(
        fold, implementation, 0u, entry->source_object,
        value, byte_offset, before, stack->len);
    return true;
}

static bool ppinf_v1_check_compressed(
    PPIndexedInferenceV1Fold *fold,
    PPIndexedInferenceV1FoldImpl *implementation,
    const PPOccurrenceFoldV1Step *step,
    char *error_buf,
    size_t error_buf_size) {
    PPIndexedInferenceV1Impl *impl =
        implementation->staged.implementation;
    PPIndexedInferenceV1Formula claim;
    PPIndexedInferenceV1Frame frame;
    PPIndexedInferenceV1FormulaStack stack = {0};
    PPIndexedInferenceV1Heap heap = {0};
    uint32_t formula_end;
    uint32_t compressed_begin;
    uint32_t label_end;
    uint32_t index;
    uint32_t label = 0u;
    uint32_t label_role;
    uint32_t formula_role;
    uint32_t trailing_role;
    uint64_t accumulator = 0u;
    bool ok = false;

    memset(&claim, 0, sizeof(claim));
    memset(&frame, 0, sizeof(frame));
    if (!step->values || step->value_len < 3u)
        goto done;
    label_role = step->values[0].role_id;
    formula_role = step->values[1].role_id;
    if (label_role == formula_role)
        goto done;
    formula_end = 2u;
    while (formula_end < step->value_len &&
           step->values[formula_end].role_id == formula_role) {
        formula_end++;
    }
    if (formula_end >= step->value_len)
        goto done;
    trailing_role = step->values[formula_end].role_id;
    if (trailing_role == label_role ||
        trailing_role == formula_role) {
        goto done;
    }
    compressed_begin = formula_end;
    while (compressed_begin < step->value_len &&
           step->values[compressed_begin].role_id ==
               trailing_role) {
        compressed_begin++;
    }
    if (compressed_begin == step->value_len) {
        label_end = formula_end;
        compressed_begin = formula_end;
    } else {
        uint32_t compressed_role =
            step->values[compressed_begin].role_id;
        label_end = compressed_begin;
        if (compressed_role == label_role ||
            compressed_role == formula_role ||
            compressed_role == trailing_role) {
            goto done;
        }
        for (index = compressed_begin;
             index < step->value_len;
             index++) {
            if (step->values[index].role_id != compressed_role)
                goto done;
        }
    }
    if (!ppinf_v1_prepare_theorem(
            impl, step, formula_end, &label, &claim, &frame,
            error_buf, error_buf_size) ||
        !ppinf_v1_heap_preload_frame(impl, &frame, &heap)) {
        goto done;
    }
    for (index = formula_end; index < label_end; index++) {
        if (!ppinf_v1_heap_preload_label(
                impl, &step->values[index], &heap,
                error_buf, error_buf_size)) {
            goto done;
        }
    }
    for (index = compressed_begin;
         index < step->value_len;
         index++) {
        const PPOccurrenceFoldV1Value *value =
            &step->values[index];
        uint32_t byte_index;

        for (byte_index = 0u;
             byte_index < value->byte_len;
             byte_index++) {
            uint8_t byte = value->bytes[byte_index];

            if (byte >= (uint8_t)'A' &&
                byte <= (uint8_t)'T') {
                uint64_t heap_index =
                    20u * accumulator +
                    (uint64_t)(byte - (uint8_t)'A');
                if (heap_index >= heap.len ||
                    !ppinf_v1_apply_heap_entry(
                        fold, implementation,
                        &heap.items[heap_index], &stack,
                        value, byte_index,
                        error_buf, error_buf_size)) {
                    if (error_buf && error_buf_size > 0u &&
                        error_buf[0] == '\0') {
                        ppinf_v1_set_error(
                            error_buf, error_buf_size,
                            "compressed proof index is out of range");
                    }
                    goto done;
                }
                accumulator = 0u;
            } else if (byte >= (uint8_t)'U' &&
                       byte <= (uint8_t)'Y') {
                uint32_t digit =
                    (uint32_t)(byte - (uint8_t)'T');
                if (accumulator >
                    (UINT64_MAX - digit) / 5u) {
                    ppinf_v1_set_error(
                        error_buf, error_buf_size,
                        "compressed proof index overflows");
                    goto done;
                }
                accumulator = 5u * accumulator + digit;
            } else if (byte == (uint8_t)'Z') {
                uint32_t before = stack.len;

                if (stack.len == 0u ||
                    !ppinf_v1_heap_push_formula(
                        &heap, &stack.items[stack.len - 1u],
                        PPINDEXED_INFERENCE_V1_NONE)) {
                    ppinf_v1_set_error(
                        error_buf, error_buf_size,
                        "compressed proof cannot save an empty stack");
                    goto done;
                }
                accumulator = 0u;
                ppinf_v1_proof_trace_step(
                    fold, implementation, 2u,
                    PPINDEXED_INFERENCE_V1_NONE,
                    value, byte_index, before, stack.len);
            } else if (byte == (uint8_t)'?') {
                accumulator = 0u;
                if (!ppinf_v1_apply_unknown(
                        fold, implementation, &claim, &stack,
                        value, byte_index,
                        error_buf, error_buf_size)) {
                    goto done;
                }
            } else {
                ppinf_v1_set_error(
                    error_buf, error_buf_size,
                    "compressed proof contains non-code byte 0x%02x "
                    "at token offset %u",
                    (unsigned)byte, byte_index);
                goto done;
            }
        }
    }
    if (accumulator != 0u) {
        ppinf_v1_set_error(
            error_buf, error_buf_size,
            "compressed proof ends inside an index");
        goto done;
    }
    ok = ppinf_v1_finish_theorem(
        impl, label, &claim, &frame, &stack,
        error_buf, error_buf_size);

done:
    if (!ok && error_buf && error_buf_size > 0u &&
        error_buf[0] == '\0') {
        ppinf_v1_set_error(
            error_buf, error_buf_size,
            "compressed theorem occurrence violates its checker schema");
    }
    ppinf_v1_heap_free(&heap);
    ppinf_v1_stack_free(&stack);
    ppinf_v1_frame_free(&frame);
    ppinf_v1_formula_free(&claim);
    return ok;
}

static bool ppinf_v1_apply_effect(
    PPIndexedInferenceV1Fold *fold,
    PPIndexedInferenceV1FoldImpl *implementation,
    PPIndexedCheckerEffectV1Kind effect,
    const PPOccurrenceFoldV1Step *step,
    char *error_buf,
    size_t error_buf_size) {
    PPIndexedInferenceV1Impl *impl =
        implementation->staged.implementation;
    uint32_t before_objects = impl->object_len;
    uint32_t added_pairs = 0u;
    bool ok = false;

    switch (effect) {
    case PPINDEXED_CHECKER_EFFECT_V1_OPEN_SCOPE:
        ok = ppinf_v1_scope_push(impl);
        if (ok)
            fold->receipt.scope_open_len++;
        break;
    case PPINDEXED_CHECKER_EFFECT_V1_CLOSE_SCOPE:
        ok = ppinf_v1_scope_pop(impl);
        if (ok)
            fold->receipt.scope_close_len++;
        else
            ppinf_v1_set_error(
                error_buf, error_buf_size,
                "cannot close the root inference scope");
        break;
    case PPINDEXED_CHECKER_EFFECT_V1_DECLARE_CONSTANTS:
        if (!implementation->config.allow_inner_constants &&
            impl->scope_len > 0u) {
            ppinf_v1_set_error(
                error_buf, error_buf_size,
                "constructor declaration is not allowed in an inner scope");
            break;
        }
        ok = ppinf_v1_declare_symbols(
            impl, step, PPINDEXED_INFERENCE_V1_OBJECT_CONSTANT,
            error_buf, error_buf_size);
        break;
    case PPINDEXED_CHECKER_EFFECT_V1_DECLARE_VARIABLES:
        ok = ppinf_v1_declare_symbols(
            impl, step, PPINDEXED_INFERENCE_V1_OBJECT_VARIABLE,
            error_buf, error_buf_size);
        break;
    case PPINDEXED_CHECKER_EFFECT_V1_DECLARE_DISTINCT:
        ok = ppinf_v1_declare_distinct(
            impl, step, &added_pairs,
            error_buf, error_buf_size);
        if (ok)
            fold->receipt.distinct_pair_len += added_pairs;
        break;
    case PPINDEXED_CHECKER_EFFECT_V1_DECLARE_FLOATING:
        ok = ppinf_v1_declare_hypothesis(
            impl, step, false, &implementation->config,
            error_buf, error_buf_size);
        if (ok)
            fold->receipt.hypothesis_len++;
        break;
    case PPINDEXED_CHECKER_EFFECT_V1_DECLARE_ESSENTIAL:
        if (!implementation->config.allow_toplevel_essential &&
            impl->scope_len == 0u) {
            ppinf_v1_set_error(
                error_buf, error_buf_size,
                "essential hypothesis is not allowed at top level");
            break;
        }
        ok = ppinf_v1_declare_hypothesis(
            impl, step, true, &implementation->config,
            error_buf, error_buf_size);
        if (ok)
            fold->receipt.hypothesis_len++;
        break;
    case PPINDEXED_CHECKER_EFFECT_V1_DECLARE_RULE:
        ok = ppinf_v1_declare_rule(
            impl, step, error_buf, error_buf_size);
        if (ok)
            fold->receipt.rule_len++;
        break;
    case PPINDEXED_CHECKER_EFFECT_V1_COMPLETE_SCOPE:
        ok = fold->receipt.scope_complete_len <
            fold->receipt.scope_close_len;
        if (ok) {
            fold->receipt.scope_complete_len++;
        } else {
            ppinf_v1_set_error(
                error_buf, error_buf_size,
                "scope completion has no preceding close occurrence");
        }
        break;
    case PPINDEXED_CHECKER_EFFECT_V1_CHECK_NORMAL:
        ok = ppinf_v1_check_normal(
            fold, implementation, step,
            error_buf, error_buf_size);
        if (ok)
            fold->receipt.rule_len++;
        break;
    case PPINDEXED_CHECKER_EFFECT_V1_CHECK_COMPRESSED:
        ok = ppinf_v1_check_compressed(
            fold, implementation, step,
            error_buf, error_buf_size);
        if (ok)
            fold->receipt.rule_len++;
        break;
    case PPINDEXED_CHECKER_EFFECT_V1_RESOLVE_SOURCE:
        if (step->value_len != 1u ||
            step->values[0].byte_len == 0u ||
            !implementation->source_resolver.resolve) {
            ppinf_v1_set_error(
                error_buf, error_buf_size,
                "source-resolution occurrence has no path or resolver");
            break;
        }
        {
            const PPOccurrenceFoldV1Backend nested_backend = {
                .context = fold,
                .apply = ppinf_v1_fold_apply,
                .commit = ppinf_v1_nested_commit,
                .abort = ppinf_v1_nested_abort,
            };
            ok = implementation->source_resolver.resolve(
                implementation->source_resolver.context,
                &step->values[0],
                implementation->config.skip_completed_sources,
                implementation->config.reject_active_source_cycles,
                &nested_backend,
                error_buf, error_buf_size) ==
                PPOCCURRENCE_SOURCE_RESOLUTION_V1_ACCEPTED;
        }
        break;
    case PPINDEXED_CHECKER_EFFECT_V1_COUNT:
        break;
    }
    if (ok && impl->object_len > before_objects) {
        fold->receipt.declaration_len +=
            impl->object_len - before_objects;
    }
    if (ok)
        fold->receipt.effect_len++;
    return ok;
}

static bool ppinf_v1_fold_apply(
    void *context,
    const PPOccurrenceFoldV1Step *step,
    char *error_buf,
    size_t error_buf_size) {
    PPIndexedInferenceV1Fold *fold = context;
    PPIndexedInferenceV1FoldImpl *implementation;
    PPIndexedCheckerEffectV1Kind effect;

    if (!fold || !fold->active || !step ||
        !(implementation = fold->implementation) ||
        !ppindexed_checker_effect_v1_lookup(
            implementation->effect_plan,
            step->operation_id, &effect) ||
        !implementation->trace_backend.apply(
            implementation->trace_backend.context, step,
            error_buf, error_buf_size) ||
        !ppinf_v1_apply_effect(
            fold, implementation, effect, step,
            error_buf, error_buf_size)) {
        return false;
    }
    return true;
}

static bool ppinf_v1_fold_commit(
    void *context,
    char *error_buf,
    size_t error_buf_size) {
    PPIndexedInferenceV1Fold *fold = context;
    PPIndexedInferenceV1FoldImpl *implementation;
    PPIndexedInferenceV1Impl *staged_impl;
    PPIndexedInferenceV1State old;
    CettaNativeSha256 proof_trace;

    if (!fold || !fold->active ||
        !(implementation = fold->implementation) ||
        !(staged_impl = implementation->staged.implementation)) {
        return false;
    }
    if (staged_impl->scope_len != 0u ||
        fold->receipt.scope_open_len != fold->receipt.scope_close_len ||
        fold->receipt.scope_close_len !=
            fold->receipt.scope_complete_len ||
        (implementation->source_resolver.digest &&
         !implementation->source_resolver.digest(
             implementation->source_resolver.context,
             fold->receipt.source_digest,
             error_buf, error_buf_size)) ||
        !ppindexed_inference_v1_state_validate(
            &implementation->staged,
            error_buf, error_buf_size) ||
        !ppinf_v1_state_digest(
            implementation->staged.implementation,
            implementation->staged.digest) ||
        !implementation->trace_backend.commit(
            implementation->trace_backend.context,
            error_buf, error_buf_size)) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            ppinf_v1_set_error(
                error_buf, error_buf_size,
                "scope occurrence ledger is incomplete at source commit");
        }
        return false;
    }
    proof_trace = implementation->proof_trace;
    {
        const uint8_t has_source_digest =
            fold->receipt.source_digest[0] ? 1u : 0u;
        cetta_native_sha256_update(
            &proof_trace, &has_source_digest, 1u);
        if (has_source_digest) {
            cetta_native_sha256_update(
                &proof_trace,
                (const uint8_t *)fold->receipt.source_digest, 64u);
        }
    }
    cetta_native_sha256_finish_hex(
        &proof_trace, fold->receipt.proof_digest);
    old = *implementation->target;
    *implementation->target = implementation->staged;
    ppindexed_inference_v1_state_init(&implementation->staged);
    ppindexed_inference_v1_state_free(&old);
    memcpy(
        fold->receipt.state_digest,
        implementation->target->digest,
        sizeof(fold->receipt.state_digest));
    memcpy(
        fold->receipt.occurrence_digest,
        fold->occurrence_trace.digest,
        sizeof(fold->receipt.occurrence_digest));
    fold->receipt.committed = true;
    fold->active = false;
    fold->committed = true;
    return true;
}

static void ppinf_v1_fold_abort(void *context) {
    PPIndexedInferenceV1Fold *fold = context;
    PPIndexedInferenceV1FoldImpl *implementation;

    if (!fold || !fold->active ||
        !(implementation = fold->implementation)) {
        return;
    }
    implementation->trace_backend.abort(
        implementation->trace_backend.context);
    ppindexed_inference_v1_state_free(&implementation->staged);
    fold->active = false;
    fold->aborted = true;
}

bool ppindexed_inference_v1_fold_init(
    PPIndexedInferenceV1Fold *fold,
    const PPGuardedLexCursorV1Program *program,
    const PPOccurrenceFoldV1Plan *occurrence_plan,
    const PPIndexedCheckerEffectV1Plan *effect_plan,
    const PPIndexedInferenceV1SourceResolver *source_resolver,
    PPIndexedInferenceV1State *target,
    char *error_buf,
    size_t error_buf_size) {
    PPIndexedInferenceV1FoldImpl *implementation = NULL;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!fold || !program || !occurrence_plan || !effect_plan ||
        !target ||
        (source_resolver &&
         (!source_resolver->resolve || !source_resolver->digest)) ||
        !ppindexed_checker_effect_v1_plan_validate(
            program, occurrence_plan, effect_plan,
            error_buf, error_buf_size)) {
        return false;
    }
    memset(fold, 0, sizeof(*fold));
    implementation = calloc(1u, sizeof(*implementation));
    if (!implementation ||
        !ppinf_v1_state_clone(target, &implementation->staged)) {
        free(implementation);
        ppinf_v1_set_error(
            error_buf, error_buf_size,
            "cannot allocate indexed inference transaction");
        return false;
    }
    /*
     * The clone's digest authenticates the pre-transaction state.  The staged
     * copy is about to change, so validation must recompute its identity at
     * commit rather than compare it with that old digest.
     */
    implementation->staged.digest[0] = '\0';
    implementation->program = program;
    implementation->occurrence_plan = occurrence_plan;
    implementation->effect_plan = effect_plan;
    if (source_resolver)
        implementation->source_resolver = *source_resolver;
    implementation->target = target;
    implementation->config = effect_plan->policy;
    ppoccurrence_fold_v1_trace_init(
        &fold->occurrence_trace, occurrence_plan);
    implementation->trace_backend =
        ppoccurrence_fold_v1_trace_backend(
            &fold->occurrence_trace);
    {
        static const char domain[] =
            "IndexedInferenceProofOccurrenceV1";
        const uint8_t config_bytes[6] = {
            implementation->config.allow_inner_constants ? 1u : 0u,
            implementation->config.allow_duplicate_floating ? 1u : 0u,
            implementation->config.allow_toplevel_essential ? 1u : 0u,
            implementation->config.reject_unknown_steps ? 1u : 0u,
            implementation->config.skip_completed_sources ? 1u : 0u,
            implementation->config.reject_active_source_cycles
                ? 1u : 0u,
        };

        cetta_native_sha256_init(&implementation->proof_trace);
        cetta_native_sha256_update(
            &implementation->proof_trace,
            (const uint8_t *)domain, sizeof(domain) - 1u);
        cetta_native_sha256_update(
            &implementation->proof_trace,
            (const uint8_t *)effect_plan->plan_digest, 64u);
        cetta_native_sha256_update(
            &implementation->proof_trace,
            (const uint8_t *)target->digest,
            strlen(target->digest));
        cetta_native_sha256_update(
            &implementation->proof_trace,
            config_bytes, sizeof(config_bytes));
    }
    fold->implementation = implementation;
    fold->active = true;
    return true;
}

void ppindexed_inference_v1_fold_free(
    PPIndexedInferenceV1Fold *fold) {
    PPIndexedInferenceV1FoldImpl *implementation;

    if (!fold)
        return;
    if (fold->active)
        ppinf_v1_fold_abort(fold);
    implementation = fold->implementation;
    if (implementation) {
        ppindexed_inference_v1_state_free(
            &implementation->staged);
        free(implementation);
    }
    memset(fold, 0, sizeof(*fold));
}

PPOccurrenceFoldV1Backend ppindexed_inference_v1_fold_backend(
    PPIndexedInferenceV1Fold *fold) {
    return (PPOccurrenceFoldV1Backend){
        .context = fold,
        .apply = ppinf_v1_fold_apply,
        .commit = ppinf_v1_fold_commit,
        .abort = ppinf_v1_fold_abort,
    };
}

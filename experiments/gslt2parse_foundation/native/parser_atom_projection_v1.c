#include "parser_atom_projection_v1.h"

#include "finite_horn_ground_term_v1.h"
#include "native_sha256.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    PPATOM_V1_MAX_PLAN_ITEMS = 65536u,
    PPATOM_V1_MAX_TEXT_BYTES = 16u * 1024u * 1024u,
    PPATOM_V1_MAX_TEXT_SCALARS = PPATOM_V1_MAX_TEXT_BYTES,
    PPATOM_V1_MAX_TEXT_NODES =
        2u * PPATOM_V1_MAX_TEXT_SCALARS + 1024u
};

typedef enum {
    PPATOM_V1_RULE_TEXT = 0,
    PPATOM_V1_RULE_VARIABLE = 1
} PPAtomV1RuleKind;

typedef enum {
    PPATOM_V1_OUTPUT_SYMBOL = 0,
    PPATOM_V1_OUTPUT_STRING = 1
} PPAtomV1OutputKind;

typedef enum {
    PPATOM_V1_TEXT_RAW = 0,
    PPATOM_V1_TEXT_DECODED = 1,
    PPATOM_V1_TEXT_DECODED_QUOTED = 2
} PPAtomV1TextMode;

typedef enum {
    PPATOM_V1_WRAPPER_MAP = 0,
    PPATOM_V1_WRAPPER_HEX = 1
} PPAtomV1WrapperKind;

typedef enum {
    PPATOM_V1_MAP_REJECT = 0,
    PPATOM_V1_MAP_IDENTITY = 1
} PPAtomV1MapDefault;

typedef PPAtomProjectionV1CodepointMapView PPAtomV1CodepointMap;

typedef struct {
    PPAtomV1WrapperKind kind;
    char *label;
    PPAtomV1MapDefault map_default;
    PPAtomV1CodepointMap *mappings;
    uint32_t mapping_len;
    uint32_t min_digits;
    uint32_t max_digits;
    uint32_t max_value;
} PPAtomV1Wrapper;

typedef struct {
    PPAtomV1RuleKind kind;
    char *source;
    PPAtomV1OutputKind output_kind;
    PPAtomV1TextMode text_mode;
} PPAtomV1Rule;

typedef struct {
    char *profile;
    char source_digest[65];
    char reader_digest[65];
    char compiler_digest[65];
    char plan_digest[65];
    char *node_label;
    char *pair_label;
    char *cons_label;
    char *nil_label;
    char *codepoint_label;
    char *document_label;
    char *expression_label;
    PPAtomV1Rule *rules;
    uint32_t rule_len;
    PPAtomV1Wrapper *wrappers;
    uint32_t wrapper_len;
} PPAtomV1PlanImpl;

typedef struct {
    Atom **data;
    uint32_t len;
    uint32_t cap;
} PPAtomV1AtomVec;

typedef struct {
    uint32_t *data;
    uint32_t len;
    uint32_t cap;
} PPAtomV1ScalarVec;

typedef struct {
    const Atom **data;
    uint32_t len;
    uint32_t cap;
} PPAtomV1ConstAtomVec;

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} PPAtomV1ByteVec;

typedef struct {
    SymbolId spelling;
    Atom *variable;
} PPAtomV1VarBinding;

typedef struct {
    PPAtomV1VarBinding *data;
    uint32_t len;
    uint32_t cap;
} PPAtomV1VarEnv;

typedef struct {
    const PPAtomV1PlanImpl *plan;
    Arena *arena;
    PPAtomV1VarEnv *variables;
    uint32_t depth_limit;
    char *error;
    size_t error_cap;
} PPAtomV1Context;

static void ppatom_v1_set_error(char *buf,
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

static char *ppatom_v1_strdup(const char *text) {
    size_t len;
    char *copy;

    if (!text)
        return NULL;
    len = strlen(text);
    copy = malloc(len + 1u);
    if (!copy)
        return NULL;
    memcpy(copy, text, len + 1u);
    return copy;
}

static bool ppatom_v1_expr_head(const Atom *atom,
                                const char *head,
                                CettaExprLen argument_len) {
    return atom && atom->kind == ATOM_EXPR &&
        atom->expr.len == argument_len + 1u &&
        atom_is_symbol(atom->expr.elems[0], head);
}

static const char *ppatom_v1_symbol_name(const Atom *atom) {
    if (!atom || atom->kind != ATOM_SYMBOL)
        return NULL;
    return atom_name_cstr((Atom *)atom);
}

static char *ppatom_v1_copy_symbol(const Atom *atom,
                                   const char *context,
                                   char *error,
                                   size_t error_cap) {
    const char *name = ppatom_v1_symbol_name(atom);
    char *copy;

    if (!name || name[0] == '\0') {
        ppatom_v1_set_error(error, error_cap,
                            "%s must be a nonempty symbol", context);
        return NULL;
    }
    copy = ppatom_v1_strdup(name);
    if (!copy) {
        ppatom_v1_set_error(error, error_cap,
                            "out of memory copying %s", context);
    }
    return copy;
}

static bool ppatom_v1_copy_digest(const Atom *atom,
                                  char out[65],
                                  const char *context,
                                  char *error,
                                  size_t error_cap) {
    const char *text = ppatom_v1_symbol_name(atom);
    uint32_t index;

    if (!text || strlen(text) != 64u) {
        ppatom_v1_set_error(error, error_cap,
                            "%s must be a lowercase SHA-256 digest",
                            context);
        return false;
    }
    for (index = 0u; index < 64u; index++) {
        if (!((text[index] >= '0' && text[index] <= '9') ||
              (text[index] >= 'a' && text[index] <= 'f'))) {
            ppatom_v1_set_error(error, error_cap,
                                "%s must be a lowercase SHA-256 digest",
                                context);
            return false;
        }
    }
    memcpy(out, text, 65u);
    return true;
}

static bool ppatom_v1_plan_digest(const Atom *plan,
                                  char out[65],
                                  char *error,
                                  size_t error_cap) {
    uint8_t *canonical = NULL;
    size_t canonical_len = 0u;
    bool ok;

    ok = fh_ground_term_v1_render(
        plan, &canonical, &canonical_len, error, error_cap);
    if (ok)
        cetta_native_sha256_hex(canonical, canonical_len, out);
    free(canonical);
    return ok;
}

static bool ppatom_v1_atom_u32(const Atom *atom,
                               uint32_t *out,
                               const char *context,
                               char *error,
                               size_t error_cap) {
    int64_t value;

    if (!atom || atom->kind != ATOM_GROUNDED ||
        atom->ground.gkind != GV_INT) {
        ppatom_v1_set_error(error, error_cap,
                            "%s must be an integer", context);
        return false;
    }
    value = atom->ground.ival;
    if (value < 0 || (uint64_t)value > UINT32_MAX) {
        ppatom_v1_set_error(error, error_cap,
                            "%s is outside uint32", context);
        return false;
    }
    *out = (uint32_t)value;
    return true;
}

static bool ppatom_v1_unicode_scalar(uint32_t value) {
    return value <= 0x10ffffu &&
        !(value >= 0xd800u && value <= 0xdfffu);
}

static bool ppatom_v1_atom_vec_push(PPAtomV1AtomVec *vec, Atom *atom) {
    Atom **next;
    uint32_t cap;

    if (vec->len == vec->cap) {
        cap = vec->cap ? vec->cap * 2u : 8u;
        if (cap < vec->cap || cap > PPATOM_V1_MAX_PLAN_ITEMS)
            cap = PPATOM_V1_MAX_PLAN_ITEMS;
        if (cap == vec->cap)
            return false;
        next = realloc(vec->data, sizeof(*next) * (size_t)cap);
        if (!next)
            return false;
        vec->data = next;
        vec->cap = cap;
    }
    vec->data[vec->len++] = atom;
    return true;
}

static bool ppatom_v1_decode_list(const Atom *term,
                                  const char *cons_a,
                                  const char *cons_b,
                                  const char *nil,
                                  PPAtomV1AtomVec *out,
                                  const char *context,
                                  char *error,
                                  size_t error_cap) {
    const Atom *cursor = term;

    memset(out, 0, sizeof(*out));
    while (!atom_is_symbol((Atom *)cursor, nil)) {
        if (!cursor || cursor->kind != ATOM_EXPR ||
            cursor->expr.len != 3u ||
            !(atom_is_symbol(cursor->expr.elems[0], cons_a) ||
              (cons_b && atom_is_symbol(cursor->expr.elems[0], cons_b)))) {
            ppatom_v1_set_error(error, error_cap,
                                "%s is not a proper list", context);
            free(out->data);
            memset(out, 0, sizeof(*out));
            return false;
        }
        if (out->len >= PPATOM_V1_MAX_PLAN_ITEMS ||
            !ppatom_v1_atom_vec_push(out, cursor->expr.elems[1])) {
            ppatom_v1_set_error(error, error_cap,
                                "%s exceeds the list limit", context);
            free(out->data);
            memset(out, 0, sizeof(*out));
            return false;
        }
        cursor = cursor->expr.elems[2];
    }
    return true;
}

static bool ppatom_v1_scalar_push(PPAtomV1ScalarVec *vec,
                                  uint32_t value) {
    uint32_t *next;
    uint32_t cap;

    if (vec->len == vec->cap) {
        cap = vec->cap ? vec->cap * 2u : 16u;
        if (cap < vec->cap || cap > PPATOM_V1_MAX_TEXT_SCALARS)
            cap = PPATOM_V1_MAX_TEXT_SCALARS;
        if (cap == vec->cap)
            return false;
        next = realloc(vec->data, sizeof(*next) * (size_t)cap);
        if (!next)
            return false;
        vec->data = next;
        vec->cap = cap;
    }
    vec->data[vec->len++] = value;
    return true;
}

static bool ppatom_v1_const_atom_push(PPAtomV1ConstAtomVec *vec,
                                      const Atom *atom) {
    const Atom **next;
    uint32_t cap;

    if (vec->len == vec->cap) {
        cap = vec->cap ? vec->cap * 2u : 64u;
        if (cap < vec->cap || cap > PPATOM_V1_MAX_TEXT_NODES)
            cap = PPATOM_V1_MAX_TEXT_NODES;
        if (cap == vec->cap)
            return false;
        next = realloc(vec->data, sizeof(*next) * (size_t)cap);
        if (!next)
            return false;
        vec->data = next;
        vec->cap = cap;
    }
    vec->data[vec->len++] = atom;
    return true;
}

static bool ppatom_v1_bytes_reserve(PPAtomV1ByteVec *vec, size_t added) {
    size_t required;
    size_t cap;
    char *next;

    if (added > SIZE_MAX - vec->len - 1u)
        return false;
    required = vec->len + added + 1u;
    if (required > PPATOM_V1_MAX_TEXT_BYTES)
        return false;
    if (required <= vec->cap)
        return true;
    cap = vec->cap ? vec->cap : 64u;
    while (cap < required) {
        if (cap > PPATOM_V1_MAX_TEXT_BYTES / 2u) {
            cap = PPATOM_V1_MAX_TEXT_BYTES;
            break;
        }
        cap *= 2u;
    }
    next = realloc(vec->data, cap);
    if (!next)
        return false;
    vec->data = next;
    vec->cap = cap;
    return true;
}

static bool ppatom_v1_bytes_append(PPAtomV1ByteVec *vec,
                                   const char *bytes,
                                   size_t len) {
    if (!ppatom_v1_bytes_reserve(vec, len))
        return false;
    if (len)
        memcpy(vec->data + vec->len, bytes, len);
    vec->len += len;
    vec->data[vec->len] = '\0';
    return true;
}

static bool ppatom_v1_utf8_append(PPAtomV1ByteVec *text,
                                  uint32_t codepoint) {
    char bytes[4];
    size_t len;

    if (!ppatom_v1_unicode_scalar(codepoint))
        return false;
    if (codepoint <= 0x7fu) {
        bytes[0] = (char)codepoint;
        len = 1u;
    } else if (codepoint <= 0x7ffu) {
        bytes[0] = (char)(0xc0u | (codepoint >> 6));
        bytes[1] = (char)(0x80u | (codepoint & 0x3fu));
        len = 2u;
    } else if (codepoint <= 0xffffu) {
        bytes[0] = (char)(0xe0u | (codepoint >> 12));
        bytes[1] = (char)(0x80u | ((codepoint >> 6) & 0x3fu));
        bytes[2] = (char)(0x80u | (codepoint & 0x3fu));
        len = 3u;
    } else {
        bytes[0] = (char)(0xf0u | (codepoint >> 18));
        bytes[1] = (char)(0x80u | ((codepoint >> 12) & 0x3fu));
        bytes[2] = (char)(0x80u | ((codepoint >> 6) & 0x3fu));
        bytes[3] = (char)(0x80u | (codepoint & 0x3fu));
        len = 4u;
    }
    return ppatom_v1_bytes_append(text, bytes, len);
}

static void ppatom_v1_rule_free(PPAtomV1Rule *rule) {
    free(rule->source);
    memset(rule, 0, sizeof(*rule));
}

static void ppatom_v1_wrapper_free(PPAtomV1Wrapper *wrapper) {
    free(wrapper->label);
    free(wrapper->mappings);
    memset(wrapper, 0, sizeof(*wrapper));
}

static void ppatom_v1_impl_free(PPAtomV1PlanImpl *impl) {
    uint32_t index;

    if (!impl)
        return;
    free(impl->profile);
    free(impl->node_label);
    free(impl->pair_label);
    free(impl->cons_label);
    free(impl->nil_label);
    free(impl->codepoint_label);
    free(impl->document_label);
    free(impl->expression_label);
    for (index = 0u; index < impl->rule_len; index++)
        ppatom_v1_rule_free(&impl->rules[index]);
    free(impl->rules);
    for (index = 0u; index < impl->wrapper_len; index++)
        ppatom_v1_wrapper_free(&impl->wrappers[index]);
    free(impl->wrappers);
    free(impl);
}

static bool ppatom_v1_parse_text_mode(const Atom *atom,
                                      PPAtomV1TextMode *out) {
    if (atom_is_symbol((Atom *)atom, "raw")) {
        *out = PPATOM_V1_TEXT_RAW;
        return true;
    }
    if (atom_is_symbol((Atom *)atom, "decoded")) {
        *out = PPATOM_V1_TEXT_DECODED;
        return true;
    }
    if (atom_is_symbol((Atom *)atom, "decoded-quoted")) {
        *out = PPATOM_V1_TEXT_DECODED_QUOTED;
        return true;
    }
    return false;
}

static bool ppatom_v1_parse_rule(const Atom *atom,
                                 PPAtomV1Rule *rule,
                                 char *error,
                                 size_t error_cap) {
    memset(rule, 0, sizeof(*rule));
    if (ppatom_v1_expr_head(atom, "sap-text-rule", 3u)) {
        rule->kind = PPATOM_V1_RULE_TEXT;
        rule->source = ppatom_v1_copy_symbol(
            atom->expr.elems[1], "text-rule source", error, error_cap);
        if (atom_is_symbol(atom->expr.elems[2], "symbol"))
            rule->output_kind = PPATOM_V1_OUTPUT_SYMBOL;
        else if (atom_is_symbol(atom->expr.elems[2], "string"))
            rule->output_kind = PPATOM_V1_OUTPUT_STRING;
        else {
            ppatom_v1_set_error(error, error_cap,
                                "text-rule output kind is invalid");
            goto fail;
        }
        if (!ppatom_v1_parse_text_mode(atom->expr.elems[3],
                                       &rule->text_mode)) {
            ppatom_v1_set_error(error, error_cap,
                                "text-rule decode mode is invalid");
            goto fail;
        }
    } else if (ppatom_v1_expr_head(atom, "sap-variable-rule", 3u)) {
        rule->kind = PPATOM_V1_RULE_VARIABLE;
        rule->source = ppatom_v1_copy_symbol(
            atom->expr.elems[1], "variable-rule source", error, error_cap);
        if (!atom_is_symbol(atom->expr.elems[2], "raw") ||
            !atom_is_symbol(atom->expr.elems[3], "no-anonymous")) {
            ppatom_v1_set_error(error, error_cap,
                                "unsupported variable-rule policy");
            goto fail;
        }
        rule->text_mode = PPATOM_V1_TEXT_RAW;
    } else {
        ppatom_v1_set_error(error, error_cap,
                            "unknown or malformed Atom-projection rule");
        return false;
    }
    if (!rule->source)
        goto fail;
    return true;

fail:
    ppatom_v1_rule_free(rule);
    return false;
}

static bool ppatom_v1_parse_wrapper(const Atom *atom,
                                    PPAtomV1Wrapper *wrapper,
                                    char *error,
                                    size_t error_cap) {
    memset(wrapper, 0, sizeof(*wrapper));
    if (ppatom_v1_expr_head(atom, "sap-map-wrapper", 3u)) {
        PPAtomV1AtomVec mappings;
        uint32_t index;

        wrapper->kind = PPATOM_V1_WRAPPER_MAP;
        wrapper->label = ppatom_v1_copy_symbol(
            atom->expr.elems[1], "map-wrapper label", error, error_cap);
        if (atom_is_symbol(atom->expr.elems[2], "reject"))
            wrapper->map_default = PPATOM_V1_MAP_REJECT;
        else if (atom_is_symbol(atom->expr.elems[2], "identity"))
            wrapper->map_default = PPATOM_V1_MAP_IDENTITY;
        else {
            ppatom_v1_set_error(error, error_cap,
                                "map-wrapper default is invalid");
            goto fail;
        }
        if (!ppatom_v1_decode_list(
                atom->expr.elems[3], "sap-cons", NULL, "sap-nil",
                &mappings, "map-wrapper mappings", error, error_cap))
            goto fail;
        if (mappings.len == 0u) {
            ppatom_v1_set_error(error, error_cap,
                                "map-wrapper mapping is empty");
            free(mappings.data);
            goto fail;
        }
        wrapper->mappings = calloc(
            mappings.len, sizeof(*wrapper->mappings));
        if (!wrapper->mappings) {
            free(mappings.data);
            goto fail;
        }
        wrapper->mapping_len = mappings.len;
        for (index = 0u; index < mappings.len; index++) {
            uint32_t prior;
            const Atom *mapping = mappings.data[index];
            if (!ppatom_v1_expr_head(
                    mapping, "sap-codepoint-map", 2u) ||
                !ppatom_v1_atom_u32(
                    mapping->expr.elems[1],
                    &wrapper->mappings[index].source,
                    "codepoint-map source", error, error_cap) ||
                !ppatom_v1_atom_u32(
                    mapping->expr.elems[2],
                    &wrapper->mappings[index].target,
                    "codepoint-map target", error, error_cap) ||
                !ppatom_v1_unicode_scalar(
                    wrapper->mappings[index].source) ||
                !ppatom_v1_unicode_scalar(
                    wrapper->mappings[index].target)) {
                if (!error || !error[0]) {
                    ppatom_v1_set_error(error, error_cap,
                                        "invalid codepoint mapping");
                }
                free(mappings.data);
                goto fail;
            }
            for (prior = 0u; prior < index; prior++) {
                if (wrapper->mappings[prior].source ==
                    wrapper->mappings[index].source) {
                    ppatom_v1_set_error(error, error_cap,
                                        "duplicate codepoint-map source");
                    free(mappings.data);
                    goto fail;
                }
            }
        }
        free(mappings.data);
    } else if (ppatom_v1_expr_head(atom, "sap-hex-wrapper", 4u)) {
        wrapper->kind = PPATOM_V1_WRAPPER_HEX;
        wrapper->label = ppatom_v1_copy_symbol(
            atom->expr.elems[1], "hex-wrapper label", error, error_cap);
        if (!ppatom_v1_atom_u32(
                atom->expr.elems[2], &wrapper->min_digits,
                "hex-wrapper minimum digits", error, error_cap) ||
            !ppatom_v1_atom_u32(
                atom->expr.elems[3], &wrapper->max_digits,
                "hex-wrapper maximum digits", error, error_cap) ||
            !ppatom_v1_atom_u32(
                atom->expr.elems[4], &wrapper->max_value,
                "hex-wrapper maximum value", error, error_cap) ||
            wrapper->min_digits == 0u ||
            wrapper->min_digits > wrapper->max_digits ||
            wrapper->max_digits > 8u ||
            wrapper->max_value > 0x10ffffu) {
            if (!error || !error[0]) {
                ppatom_v1_set_error(error, error_cap,
                                    "invalid hex-wrapper bounds");
            }
            goto fail;
        }
    } else {
        ppatom_v1_set_error(error, error_cap,
                            "unknown or malformed text wrapper");
        return false;
    }
    if (!wrapper->label)
        goto fail;
    return true;

fail:
    ppatom_v1_wrapper_free(wrapper);
    return false;
}

void ppatom_projection_v1_plan_init(PPAtomProjectionV1Plan *plan) {
    if (plan)
        plan->implementation = NULL;
}

void ppatom_projection_v1_plan_free(PPAtomProjectionV1Plan *plan) {
    if (!plan)
        return;
    ppatom_v1_impl_free((PPAtomV1PlanImpl *)plan->implementation);
    plan->implementation = NULL;
}

bool ppatom_projection_v1_plan_load(PPAtomProjectionV1Plan *plan,
                                    const Atom *compiled_plan,
                                    char *error_buf,
                                    size_t error_buf_size) {
    PPAtomV1PlanImpl *impl = NULL;
    const Atom *plan_term;
    PPAtomV1AtomVec rules = {0};
    PPAtomV1AtomVec wrappers = {0};
    const char *structural_labels[7];
    char observed_plan_digest[65];
    uint32_t index;

    if (error_buf && error_buf_size)
        error_buf[0] = '\0';
    if (!plan || !ppatom_v1_expr_head(
            compiled_plan, "sap-artifact-v1", 6u)) {
        ppatom_v1_set_error(error_buf, error_buf_size,
                            "expected sap-artifact-v1 with six fields");
        return false;
    }
    plan_term = compiled_plan->expr.elems[6];
    if (!ppatom_v1_expr_head(plan_term, "sap-plan-v1", 9u)) {
        ppatom_v1_set_error(error_buf, error_buf_size,
                            "artifact lacks a nine-field sap-plan-v1");
        return false;
    }
    impl = calloc(1u, sizeof(*impl));
    if (!impl) {
        ppatom_v1_set_error(error_buf, error_buf_size,
                            "out of memory allocating Atom projection plan");
        return false;
    }
    impl->profile = ppatom_v1_copy_symbol(
        compiled_plan->expr.elems[1], "projection profile",
        error_buf, error_buf_size);
    if (!impl->profile ||
        !ppatom_v1_copy_digest(
            compiled_plan->expr.elems[2], impl->source_digest,
            "projection source digest", error_buf, error_buf_size) ||
        !ppatom_v1_copy_digest(
            compiled_plan->expr.elems[3], impl->reader_digest,
            "reader source digest", error_buf, error_buf_size) ||
        !ppatom_v1_copy_digest(
            compiled_plan->expr.elems[4], impl->compiler_digest,
            "projection compiler digest", error_buf, error_buf_size) ||
        !ppatom_v1_copy_digest(
            compiled_plan->expr.elems[5], impl->plan_digest,
            "projection plan digest", error_buf, error_buf_size) ||
        !ppatom_v1_plan_digest(
            plan_term, observed_plan_digest, error_buf, error_buf_size)) {
        goto fail;
    }
    if (strcmp(impl->plan_digest, observed_plan_digest) != 0) {
        ppatom_v1_set_error(error_buf, error_buf_size,
                            "projection plan digest mismatch");
        goto fail;
    }
#define PPATOM_COPY_FIELD(field, position, label)                         \
    do {                                                                  \
        impl->field = ppatom_v1_copy_symbol(                              \
            plan_term->expr.elems[position], label,                       \
            error_buf, error_buf_size);                                   \
        if (!impl->field)                                                 \
            goto fail;                                                    \
    } while (0)
    PPATOM_COPY_FIELD(node_label, 1u, "node label");
    PPATOM_COPY_FIELD(pair_label, 2u, "pair label");
    PPATOM_COPY_FIELD(cons_label, 3u, "cons label");
    PPATOM_COPY_FIELD(nil_label, 4u, "nil label");
    PPATOM_COPY_FIELD(codepoint_label, 5u, "codepoint label");
    PPATOM_COPY_FIELD(document_label, 6u, "document label");
    PPATOM_COPY_FIELD(expression_label, 7u, "expression label");
#undef PPATOM_COPY_FIELD
    structural_labels[0] = impl->node_label;
    structural_labels[1] = impl->pair_label;
    structural_labels[2] = impl->cons_label;
    structural_labels[3] = impl->nil_label;
    structural_labels[4] = impl->codepoint_label;
    structural_labels[5] = impl->document_label;
    structural_labels[6] = impl->expression_label;
    for (index = 0u; index < 7u; index++) {
        uint32_t prior;
        for (prior = 0u; prior < index; prior++) {
            if (strcmp(structural_labels[prior],
                       structural_labels[index]) == 0) {
                ppatom_v1_set_error(
                    error_buf, error_buf_size,
                    "Atom-projection structural labels must be distinct");
                goto fail;
            }
        }
    }
    if (!ppatom_v1_decode_list(
            plan_term->expr.elems[8], "sap-cons", NULL, "sap-nil",
            &rules, "Atom projection rules", error_buf, error_buf_size)) {
        goto fail;
    }
    if (!ppatom_v1_decode_list(
            plan_term->expr.elems[9], "sap-cons", NULL, "sap-nil",
            &wrappers, "Atom projection wrappers",
            error_buf, error_buf_size)) {
        goto fail;
    }
    if (rules.len == 0u) {
        ppatom_v1_set_error(error_buf, error_buf_size,
                            "Atom projection has no leaf rules");
        goto fail;
    }
    impl->rules = calloc(rules.len, sizeof(*impl->rules));
    impl->wrappers = calloc(
        wrappers.len ? wrappers.len : 1u, sizeof(*impl->wrappers));
    if (!impl->rules || !impl->wrappers) {
        ppatom_v1_set_error(error_buf, error_buf_size,
                            "out of memory allocating projection tables");
        goto fail;
    }
    impl->rule_len = rules.len;
    impl->wrapper_len = wrappers.len;
    for (index = 0u; index < rules.len; index++) {
        uint32_t prior;
        uint32_t structural_index;
        if (!ppatom_v1_parse_rule(
                rules.data[index], &impl->rules[index],
                error_buf, error_buf_size)) {
            goto fail;
        }
        for (structural_index = 0u; structural_index < 7u;
             structural_index++) {
            if (strcmp(impl->rules[index].source,
                       structural_labels[structural_index]) == 0) {
                ppatom_v1_set_error(
                    error_buf, error_buf_size,
                    "leaf rule overlaps a structural label");
                goto fail;
            }
        }
        for (prior = 0u; prior < index; prior++) {
            if (strcmp(impl->rules[prior].source,
                       impl->rules[index].source) == 0) {
                ppatom_v1_set_error(error_buf, error_buf_size,
                                    "duplicate Atom-projection source");
                goto fail;
            }
        }
    }
    for (index = 0u; index < wrappers.len; index++) {
        uint32_t prior;
        uint32_t rule_index;
        uint32_t structural_index;
        if (!ppatom_v1_parse_wrapper(
                wrappers.data[index], &impl->wrappers[index],
                error_buf, error_buf_size)) {
            goto fail;
        }
        for (structural_index = 0u; structural_index < 7u;
             structural_index++) {
            if (strcmp(impl->wrappers[index].label,
                       structural_labels[structural_index]) == 0) {
                ppatom_v1_set_error(
                    error_buf, error_buf_size,
                    "text wrapper overlaps a structural label");
                goto fail;
            }
        }
        for (rule_index = 0u; rule_index < impl->rule_len; rule_index++) {
            if (strcmp(impl->wrappers[index].label,
                       impl->rules[rule_index].source) == 0) {
                ppatom_v1_set_error(error_buf, error_buf_size,
                                    "text wrapper overlaps a leaf label");
                goto fail;
            }
        }
        for (prior = 0u; prior < index; prior++) {
            if (strcmp(impl->wrappers[prior].label,
                       impl->wrappers[index].label) == 0) {
                ppatom_v1_set_error(error_buf, error_buf_size,
                                    "duplicate text-wrapper label");
                goto fail;
            }
        }
    }
    free(rules.data);
    free(wrappers.data);
    ppatom_v1_impl_free((PPAtomV1PlanImpl *)plan->implementation);
    plan->implementation = impl;
    return true;

fail:
    free(rules.data);
    free(wrappers.data);
    ppatom_v1_impl_free(impl);
    return false;
}

bool ppatom_projection_v1_plan_provenance(
    const PPAtomProjectionV1Plan *plan,
    PPAtomProjectionV1ProvenanceView *out,
    char *error_buf,
    size_t error_buf_size) {
    const PPAtomV1PlanImpl *impl;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!plan || !(impl = plan->implementation) || !out) {
        ppatom_v1_set_error(error_buf, error_buf_size,
                            "Atom-projection plan is not loaded");
        return false;
    }
    *out = (PPAtomProjectionV1ProvenanceView){
        .profile = impl->profile,
        .source_digest = impl->source_digest,
        .reader_digest = impl->reader_digest,
        .compiler_digest = impl->compiler_digest,
        .plan_digest = impl->plan_digest,
    };
    return true;
}

bool ppatom_projection_v1_plan_carriers(
    const PPAtomProjectionV1Plan *plan,
    PPAtomProjectionV1CarrierView *out,
    char *error_buf,
    size_t error_buf_size) {
    const PPAtomV1PlanImpl *impl;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!plan || !(impl = plan->implementation) || !out) {
        ppatom_v1_set_error(error_buf, error_buf_size,
                            "Atom-projection plan is not loaded");
        return false;
    }
    *out = (PPAtomProjectionV1CarrierView){
        .node_label = impl->node_label,
        .pair_label = impl->pair_label,
        .cons_label = impl->cons_label,
        .nil_label = impl->nil_label,
        .codepoint_label = impl->codepoint_label,
        .document_label = impl->document_label,
        .expression_label = impl->expression_label,
    };
    return true;
}

bool ppatom_projection_v1_plan_rule_count(
    const PPAtomProjectionV1Plan *plan,
    uint32_t *out,
    char *error_buf,
    size_t error_buf_size) {
    const PPAtomV1PlanImpl *impl;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!plan || !(impl = plan->implementation) || !out) {
        ppatom_v1_set_error(error_buf, error_buf_size,
                            "Atom-projection plan is not loaded");
        return false;
    }
    *out = impl->rule_len;
    return true;
}

bool ppatom_projection_v1_plan_rule(
    const PPAtomProjectionV1Plan *plan,
    uint32_t index,
    PPAtomProjectionV1RuleView *out,
    char *error_buf,
    size_t error_buf_size) {
    const PPAtomV1PlanImpl *impl;
    const PPAtomV1Rule *rule;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!plan || !(impl = plan->implementation) || !out ||
        index >= impl->rule_len) {
        ppatom_v1_set_error(error_buf, error_buf_size,
                            "bad Atom-projection rule-view arguments");
        return false;
    }
    rule = &impl->rules[index];
    *out = (PPAtomProjectionV1RuleView){
        .kind = rule->kind == PPATOM_V1_RULE_TEXT
            ? PPATOM_PROJECTION_V1_RULE_TEXT
            : PPATOM_PROJECTION_V1_RULE_VARIABLE,
        .source_label = rule->source,
        .output_kind = rule->output_kind == PPATOM_V1_OUTPUT_SYMBOL
            ? PPATOM_PROJECTION_V1_OUTPUT_SYMBOL
            : PPATOM_PROJECTION_V1_OUTPUT_STRING,
        .text_mode = rule->text_mode == PPATOM_V1_TEXT_RAW
            ? PPATOM_PROJECTION_V1_TEXT_RAW
            : (rule->text_mode == PPATOM_V1_TEXT_DECODED
                   ? PPATOM_PROJECTION_V1_TEXT_DECODED
                   : PPATOM_PROJECTION_V1_TEXT_DECODED_QUOTED),
    };
    return true;
}

bool ppatom_projection_v1_plan_wrapper_count(
    const PPAtomProjectionV1Plan *plan,
    uint32_t *out,
    char *error_buf,
    size_t error_buf_size) {
    const PPAtomV1PlanImpl *impl;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!plan || !(impl = plan->implementation) || !out) {
        ppatom_v1_set_error(error_buf, error_buf_size,
                            "Atom-projection plan is not loaded");
        return false;
    }
    *out = impl->wrapper_len;
    return true;
}

bool ppatom_projection_v1_plan_wrapper(
    const PPAtomProjectionV1Plan *plan,
    uint32_t index,
    PPAtomProjectionV1WrapperView *out,
    char *error_buf,
    size_t error_buf_size) {
    const PPAtomV1PlanImpl *impl;
    const PPAtomV1Wrapper *wrapper;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!plan || !(impl = plan->implementation) || !out ||
        index >= impl->wrapper_len) {
        ppatom_v1_set_error(error_buf, error_buf_size,
                            "bad Atom-projection wrapper-view arguments");
        return false;
    }
    wrapper = &impl->wrappers[index];
    *out = (PPAtomProjectionV1WrapperView){
        .kind = wrapper->kind == PPATOM_V1_WRAPPER_MAP
            ? PPATOM_PROJECTION_V1_WRAPPER_MAP
            : PPATOM_PROJECTION_V1_WRAPPER_HEX,
        .label = wrapper->label,
        .map_default = wrapper->map_default == PPATOM_V1_MAP_REJECT
            ? PPATOM_PROJECTION_V1_MAP_REJECT
            : PPATOM_PROJECTION_V1_MAP_IDENTITY,
        .mappings = wrapper->mappings,
        .mapping_len = wrapper->mapping_len,
        .min_digits = wrapper->min_digits,
        .max_digits = wrapper->max_digits,
        .max_value = wrapper->max_value,
    };
    return true;
}

static const PPAtomV1Rule *ppatom_v1_find_rule(
    const PPAtomV1PlanImpl *plan,
    const char *source) {
    uint32_t index;

    for (index = 0u; index < plan->rule_len; index++) {
        if (strcmp(plan->rules[index].source, source) == 0)
            return &plan->rules[index];
    }
    return NULL;
}

static const PPAtomV1Wrapper *ppatom_v1_find_wrapper(
    const PPAtomV1PlanImpl *plan,
    const char *label) {
    uint32_t index;

    for (index = 0u; index < plan->wrapper_len; index++) {
        if (strcmp(plan->wrappers[index].label, label) == 0)
            return &plan->wrappers[index];
    }
    return NULL;
}

static bool ppatom_v1_collect_text(PPAtomV1Context *context,
                                   const Atom *term,
                                   bool allow_wrappers,
                                   PPAtomV1ScalarVec *out,
                                   uint32_t depth);

static int32_t ppatom_v1_hex_digit(uint32_t codepoint) {
    if (codepoint >= (uint32_t)'0' && codepoint <= (uint32_t)'9')
        return (int32_t)(codepoint - (uint32_t)'0');
    if (codepoint >= (uint32_t)'a' && codepoint <= (uint32_t)'f')
        return (int32_t)(codepoint - (uint32_t)'a' + 10u);
    if (codepoint >= (uint32_t)'A' && codepoint <= (uint32_t)'F')
        return (int32_t)(codepoint - (uint32_t)'A' + 10u);
    return -1;
}

static bool ppatom_v1_apply_wrapper(
    const PPAtomV1Wrapper *wrapper,
    const uint32_t *source,
    uint32_t source_len,
    uint32_t *out_scalar,
    char *error,
    size_t error_cap) {
    uint32_t result;
    uint32_t index;

    if (!wrapper || !out_scalar || (source_len > 0u && !source)) {
        ppatom_v1_set_error(error, error_cap,
                            "bad text-wrapper scalar input");
        return false;
    }
    if (wrapper->kind == PPATOM_V1_WRAPPER_MAP) {
        if (source_len != 1u) {
            ppatom_v1_set_error(error, error_cap,
                                "mapped escape must contain one scalar");
            return false;
        }
        result = source[0];
        for (index = 0u; index < wrapper->mapping_len; index++) {
            if (wrapper->mappings[index].source == result) {
                result = wrapper->mappings[index].target;
                break;
            }
        }
        if (index == wrapper->mapping_len &&
            wrapper->map_default == PPATOM_V1_MAP_REJECT) {
            ppatom_v1_set_error(error, error_cap,
                                "unmapped escape scalar");
            return false;
        }
    } else {
        uint64_t value = 0u;
        if (source_len < wrapper->min_digits ||
            source_len > wrapper->max_digits) {
            ppatom_v1_set_error(error, error_cap,
                                "hex escape has the wrong digit count");
            return false;
        }
        for (index = 0u; index < source_len; index++) {
            int32_t digit = ppatom_v1_hex_digit(source[index]);
            if (digit < 0) {
                ppatom_v1_set_error(error, error_cap,
                                    "hex escape contains a non-hex scalar");
                return false;
            }
            value = value * 16u + (uint32_t)digit;
            if (value > wrapper->max_value) {
                ppatom_v1_set_error(error, error_cap,
                                    "hex escape exceeds its value bound");
                return false;
            }
        }
        result = (uint32_t)value;
        if (!ppatom_v1_unicode_scalar(result)) {
            ppatom_v1_set_error(error, error_cap,
                                "hex escape is not a Unicode scalar");
            return false;
        }
    }
    *out_scalar = result;
    return true;
}

static bool ppatom_v1_collect_wrapper(PPAtomV1Context *context,
                                      const PPAtomV1Wrapper *wrapper,
                                      const Atom *payload,
                                      PPAtomV1ScalarVec *out,
                                      uint32_t depth) {
    PPAtomV1ScalarVec source;
    uint32_t result;

    memset(&source, 0, sizeof(source));
    if (!ppatom_v1_collect_text(
            context, payload, false, &source, depth + 1u)) {
        free(source.data);
        return false;
    }
    if (!ppatom_v1_apply_wrapper(
            wrapper, source.data, source.len, &result,
            context->error, context->error_cap)) {
        free(source.data);
        return false;
    }
    free(source.data);
    if (!ppatom_v1_scalar_push(out, result)) {
        ppatom_v1_set_error(context->error, context->error_cap,
                            "decoded text exceeds its scalar limit");
        return false;
    }
    return true;
}

bool ppatom_projection_v1_apply_wrapper_scalars_prevalidated(
    const PPAtomProjectionV1Plan *plan,
    uint32_t wrapper_index,
    const uint32_t *source,
    uint32_t source_len,
    uint32_t *out_scalar,
    char *error_buf,
    size_t error_buf_size) {
    const PPAtomV1PlanImpl *impl = plan ? plan->implementation : NULL;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!impl || wrapper_index >= impl->wrapper_len) {
        ppatom_v1_set_error(error_buf, error_buf_size,
                            "text-wrapper index escaped its projection plan");
        return false;
    }
    return ppatom_v1_apply_wrapper(
        &impl->wrappers[wrapper_index], source, source_len,
        out_scalar, error_buf, error_buf_size);
}

static bool ppatom_v1_collect_text(PPAtomV1Context *context,
                                   const Atom *term,
                                   bool allow_wrappers,
                                   PPAtomV1ScalarVec *out,
                                   uint32_t depth) {
    PPAtomV1ConstAtomVec stack = {0};
    uint32_t node_count = 0u;

    if (depth > context->depth_limit) {
        ppatom_v1_set_error(context->error, context->error_cap,
                            "text projection exceeded the depth limit");
        return false;
    }
    if (!ppatom_v1_const_atom_push(&stack, term)) {
        ppatom_v1_set_error(context->error, context->error_cap,
                            "out of memory starting text projection");
        return false;
    }
    while (stack.len > 0u) {
        const Atom *current = stack.data[--stack.len];
        uint32_t codepoint;

        if (node_count++ >= PPATOM_V1_MAX_TEXT_NODES) {
            ppatom_v1_set_error(context->error, context->error_cap,
                                "text projection exceeds its node limit");
            goto fail;
        }
        if (atom_is_symbol((Atom *)current, context->plan->nil_label))
            continue;
        if (ppatom_v1_expr_head(
                current, context->plan->codepoint_label, 1u)) {
            if (!ppatom_v1_atom_u32(
                    current->expr.elems[1], &codepoint, "text codepoint",
                    context->error, context->error_cap) ||
                !ppatom_v1_unicode_scalar(codepoint) ||
                !ppatom_v1_scalar_push(out, codepoint)) {
                if (!context->error || !context->error[0]) {
                    ppatom_v1_set_error(context->error, context->error_cap,
                                        "invalid scalar in text spine");
                }
                goto fail;
            }
            continue;
        }
        if (current && current->kind == ATOM_EXPR &&
            current->expr.len == 3u &&
            (atom_is_symbol(current->expr.elems[0],
                            context->plan->pair_label) ||
             atom_is_symbol(current->expr.elems[0],
                            context->plan->cons_label))) {
            if (!ppatom_v1_const_atom_push(
                    &stack, current->expr.elems[2]) ||
                !ppatom_v1_const_atom_push(
                    &stack, current->expr.elems[1])) {
                ppatom_v1_set_error(context->error, context->error_cap,
                                    "text projection stack exceeds its limit");
                goto fail;
            }
            continue;
        }
        if (allow_wrappers && current && current->kind == ATOM_EXPR &&
            current->expr.len == 3u &&
            atom_is_symbol(current->expr.elems[0],
                           context->plan->node_label)) {
            const char *label =
                ppatom_v1_symbol_name(current->expr.elems[1]);
            const PPAtomV1Wrapper *wrapper = label
                ? ppatom_v1_find_wrapper(context->plan, label)
                : NULL;
            if (!wrapper) {
                ppatom_v1_set_error(context->error, context->error_cap,
                                    "unknown text-wrapper node");
                goto fail;
            }
            if (!ppatom_v1_collect_wrapper(
                    context, wrapper, current->expr.elems[2],
                    out, depth + 1u)) {
                goto fail;
            }
            continue;
        }
        ppatom_v1_set_error(context->error, context->error_cap,
                            "malformed codepoint text spine");
        goto fail;
    }
    free(stack.data);
    return true;

fail:
    free(stack.data);
    return false;
}

static bool ppatom_v1_decode_text(PPAtomV1Context *context,
                                  const Atom *term,
                                  PPAtomV1TextMode mode,
                                  uint32_t depth,
                                  PPAtomV1ByteVec *out) {
    PPAtomV1ScalarVec scalars;
    PPAtomV1ByteVec bytes;
    uint32_t index;

    memset(&scalars, 0, sizeof(scalars));
    memset(&bytes, 0, sizeof(bytes));
    memset(out, 0, sizeof(*out));
    if (!ppatom_v1_collect_text(
            context, term, mode != PPATOM_V1_TEXT_RAW,
            &scalars, depth + 1u)) {
        free(scalars.data);
        return false;
    }
    if (mode == PPATOM_V1_TEXT_DECODED_QUOTED &&
        !ppatom_v1_utf8_append(&bytes, (uint32_t)'"')) {
        goto fail;
    }
    for (index = 0u; index < scalars.len; index++) {
        if (!ppatom_v1_utf8_append(&bytes, scalars.data[index]))
            goto fail;
    }
    if (mode == PPATOM_V1_TEXT_DECODED_QUOTED &&
        !ppatom_v1_utf8_append(&bytes, (uint32_t)'"')) {
        goto fail;
    }
    free(scalars.data);
    if (!bytes.data) {
        bytes.data = ppatom_v1_strdup("");
        if (!bytes.data) {
            ppatom_v1_set_error(context->error, context->error_cap,
                                "out of memory decoding empty text");
            return false;
        }
    }
    *out = bytes;
    return true;

fail:
    free(scalars.data);
    free(bytes.data);
    ppatom_v1_set_error(context->error, context->error_cap,
                        "text cannot be represented as a host Atom spelling");
    return false;
}

static Atom *ppatom_v1_project_node(PPAtomV1Context *context,
                                    const Atom *term,
                                    uint32_t depth);

static Atom *ppatom_v1_project_expression(PPAtomV1Context *context,
                                          const Atom *payload,
                                          uint32_t depth) {
    PPAtomV1AtomVec source;
    Atom **children = NULL;
    Atom *result = NULL;
    uint32_t index;

    if (!ppatom_v1_decode_list(
            payload, context->plan->pair_label,
            context->plan->cons_label, context->plan->nil_label,
            &source, "expression payload", context->error,
            context->error_cap)) {
        return NULL;
    }
    if (source.len > 0u) {
        children = arena_alloc(
            context->arena, sizeof(*children) * (size_t)source.len);
        if (!children) {
            ppatom_v1_set_error(context->error, context->error_cap,
                                "out of memory materializing expression");
            free(source.data);
            return NULL;
        }
    }
    for (index = 0u; index < source.len; index++) {
        children[index] = ppatom_v1_project_node(
            context, source.data[index], depth + 1u);
        if (!children[index]) {
            free(source.data);
            return NULL;
        }
    }
    result = atom_expr(context->arena, children, source.len);
    free(source.data);
    return result;
}

static Atom *ppatom_v1_variable(PPAtomV1Context *context,
                                const uint8_t *spelling,
                                uint32_t spelling_len) {
    PPAtomV1VarEnv *environment = context->variables;
    SymbolId spelling_id = spelling_len == 0u
        ? SYMBOL_ID_NONE
        : symbol_intern_bytes(g_symbols, spelling, spelling_len);
    uint32_t index;

    for (index = 0u; index < environment->len; index++) {
        if (environment->data[index].spelling == spelling_id)
            return environment->data[index].variable;
    }
    if (environment->len == environment->cap) {
        uint32_t cap = environment->cap ? environment->cap * 2u : 8u;
        PPAtomV1VarBinding *next;
        if (cap < environment->cap || cap > PPATOM_V1_MAX_PLAN_ITEMS)
            cap = PPATOM_V1_MAX_PLAN_ITEMS;
        if (cap == environment->cap) {
            ppatom_v1_set_error(context->error, context->error_cap,
                                "variable environment exceeds its limit");
            return NULL;
        }
        next = realloc(environment->data,
                       sizeof(*next) * (size_t)cap);
        if (!next) {
            ppatom_v1_set_error(context->error, context->error_cap,
                                "out of memory extending variable scope");
            return NULL;
        }
        environment->data = next;
        environment->cap = cap;
    }
    environment->data[environment->len].spelling = spelling_id;
    environment->data[environment->len].variable = atom_var_with_spelling(
        context->arena, spelling_id, fresh_var_id());
    if (!environment->data[environment->len].variable)
        return NULL;
    environment->len++;
    return environment->data[environment->len - 1u].variable;
}

static Atom *ppatom_v1_project_node(PPAtomV1Context *context,
                                    const Atom *term,
                                    uint32_t depth) {
    const char *label;
    const PPAtomV1Rule *rule;
    const Atom *payload;
    PPAtomV1ByteVec text;
    Atom *result;

    if (depth > context->depth_limit) {
        ppatom_v1_set_error(context->error, context->error_cap,
                            "Atom projection exceeded the depth limit");
        return NULL;
    }
    if (!term || term->kind != ATOM_EXPR || term->expr.len != 3u ||
        !atom_is_symbol(term->expr.elems[0], context->plan->node_label) ||
        !(label = ppatom_v1_symbol_name(term->expr.elems[1]))) {
        ppatom_v1_set_error(context->error, context->error_cap,
                            "expected a neutral syntax node");
        return NULL;
    }
    payload = term->expr.elems[2];
    if (strcmp(label, context->plan->expression_label) == 0)
        return ppatom_v1_project_expression(context, payload, depth + 1u);
    rule = ppatom_v1_find_rule(context->plan, label);
    if (!rule) {
        ppatom_v1_set_error(context->error, context->error_cap,
                            "neutral syntax node has no projection rule");
        return NULL;
    }
    if (!ppatom_v1_decode_text(
            context, payload, rule->text_mode, depth + 1u, &text)) {
        return NULL;
    }
    if (text.len == 0u && rule->kind == PPATOM_V1_RULE_TEXT &&
        rule->output_kind == PPATOM_V1_OUTPUT_SYMBOL) {
        ppatom_v1_set_error(context->error, context->error_cap,
                            "host symbol spelling must not be empty");
        free(text.data);
        return NULL;
    }
    if (text.len > UINT32_MAX) {
        ppatom_v1_set_error(context->error, context->error_cap,
                            "host Atom spelling is too large");
        free(text.data);
        return NULL;
    }
    if (rule->kind == PPATOM_V1_RULE_VARIABLE) {
        result = ppatom_v1_variable(
            context, (const uint8_t *)text.data, (uint32_t)text.len);
    } else if (rule->output_kind == PPATOM_V1_OUTPUT_SYMBOL) {
        SymbolId spelling = symbol_intern_bytes(
            g_symbols, (const uint8_t *)text.data, (uint32_t)text.len);
        result = spelling == SYMBOL_ID_NONE
            ? NULL
            : atom_symbol_id(context->arena, spelling);
    } else {
        if (memchr(text.data, '\0', text.len)) {
            ppatom_v1_set_error(
                context->error, context->error_cap,
                "grounded string spelling contains an embedded NUL");
            free(text.data);
            return NULL;
        }
        result = atom_string(context->arena, text.data);
    }
    free(text.data);
    if (!result) {
        ppatom_v1_set_error(context->error, context->error_cap,
                            "failed to materialize host Atom");
    }
    return result;
}

bool ppatom_projection_v1_project_document(
    const PPAtomProjectionV1Plan *plan,
    const Atom *semantic_result,
    Arena *output_arena,
    uint32_t depth_limit,
    Atom ***out_atoms,
    uint32_t *out_len,
    char *error_buf,
    size_t error_buf_size) {
    const PPAtomV1PlanImpl *impl;
    const Atom *document;
    PPAtomV1AtomVec source;
    Atom **atoms = NULL;
    ArenaMark mark;
    uint32_t source_len;
    uint32_t index;

    if (error_buf && error_buf_size)
        error_buf[0] = '\0';
    if (out_atoms)
        *out_atoms = NULL;
    if (out_len)
        *out_len = 0u;
    if (!plan || !(impl = plan->implementation) || !semantic_result ||
        !output_arena || !out_atoms || !out_len || depth_limit == 0u) {
        ppatom_v1_set_error(error_buf, error_buf_size,
                            "invalid Atom-projection arguments");
        return false;
    }
    if (!ppatom_v1_expr_head(semantic_result, "result", 2u) ||
        !atom_is_symbol(semantic_result->expr.elems[2], impl->nil_label)) {
        ppatom_v1_set_error(error_buf, error_buf_size,
                            "malformed or trailing semantic result");
        return false;
    }
    document = semantic_result->expr.elems[1];
    if (!document || document->kind != ATOM_EXPR ||
        document->expr.len != 3u ||
        !atom_is_symbol(document->expr.elems[0], impl->node_label) ||
        !atom_is_symbol(document->expr.elems[1], impl->document_label) ||
        !ppatom_v1_decode_list(
            document->expr.elems[2], impl->pair_label, impl->cons_label,
            impl->nil_label, &source, "document payload",
            error_buf, error_buf_size)) {
        if (!error_buf || !error_buf[0]) {
            ppatom_v1_set_error(error_buf, error_buf_size,
                                "semantic result lacks a document sequence");
        }
        return false;
    }
    source_len = source.len;
    mark = arena_mark(output_arena);
    if (source.len > 0u) {
        atoms = arena_alloc(
            output_arena, sizeof(*atoms) * (size_t)source.len);
        if (!atoms) {
            ppatom_v1_set_error(error_buf, error_buf_size,
                                "out of memory allocating document Atoms");
            free(source.data);
            return false;
        }
    }
    for (index = 0u; index < source.len; index++) {
        PPAtomV1VarEnv variables;
        PPAtomV1Context context;

        memset(&variables, 0, sizeof(variables));
        context = (PPAtomV1Context){
            .plan = impl,
            .arena = output_arena,
            .variables = &variables,
            .depth_limit = depth_limit,
            .error = error_buf,
            .error_cap = error_buf_size,
        };
        atoms[index] = ppatom_v1_project_node(
            &context, source.data[index], 1u);
        free(variables.data);
        if (!atoms[index]) {
            free(source.data);
            arena_reset(output_arena, mark);
            return false;
        }
    }
    free(source.data);
    *out_atoms = atoms;
    *out_len = source_len;
    return true;
}

#include "parser_atom_projection_events_v1.h"

#include "symbol.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    PPATOM_EVENTS_V1_MAX_PLAN_ITEMS = 65536u,
    PPATOM_EVENTS_V1_MAX_CONTAINER_ITEMS = 16u * 1024u * 1024u,
    PPATOM_EVENTS_V1_MAX_TEXT_BYTES = 16u * 1024u * 1024u,
    PPATOM_EVENTS_V1_MAX_WRAPPER_SCALARS = 8u
};

typedef struct {
    Atom **data;
    uint32_t len;
    uint32_t cap;
} PPAtomEventsV1AtomVec;

typedef struct {
    uint8_t *data;
    uint32_t len;
    uint32_t cap;
} PPAtomEventsV1ByteVec;

typedef struct {
    uint32_t data[PPATOM_EVENTS_V1_MAX_WRAPPER_SCALARS];
    uint32_t len;
} PPAtomEventsV1ScalarVec;

typedef struct {
    SymbolId spelling;
    Atom *variable;
} PPAtomEventsV1VarBinding;

typedef struct {
    PPAtomEventsV1VarBinding *data;
    uint32_t len;
    uint32_t cap;
} PPAtomEventsV1VarEnv;

typedef enum {
    PPATOM_EVENTS_V1_FRAME_DOCUMENT = 0,
    PPATOM_EVENTS_V1_FRAME_EXPRESSION = 1,
    PPATOM_EVENTS_V1_FRAME_LEAF = 2,
    PPATOM_EVENTS_V1_FRAME_WRAPPER = 3
} PPAtomEventsV1FrameKind;

typedef struct {
    PPAtomEventsV1FrameKind kind;
    uint32_t operand;
    union {
        PPAtomEventsV1AtomVec atoms;
        PPAtomEventsV1ByteVec bytes;
        PPAtomEventsV1ScalarVec scalars;
    } payload;
} PPAtomEventsV1Frame;

typedef struct {
    const PPAtomProjectionV1Plan *plan;
    Arena *arena;
    ArenaMark mark;
    PPAtomProjectionV1RuleView *rules;
    uint32_t rule_len;
    PPAtomProjectionV1WrapperView *wrappers;
    uint32_t wrapper_len;
    PPAtomEventsV1Frame *frames;
    uint32_t frame_len;
    uint32_t frame_cap;
    PPAtomEventsV1AtomVec document;
    PPAtomEventsV1VarEnv variables;
    uint32_t depth_limit;
    bool active;
    bool document_started;
    bool document_closed;
} PPAtomEventsV1Impl;

static void ppae_v1_set_error(char *buf,
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

static bool ppae_v1_unicode_scalar(uint32_t scalar) {
    return scalar <= 0x10ffffu &&
        !(scalar >= 0xd800u && scalar <= 0xdfffu);
}

static void ppae_v1_frame_release(PPAtomEventsV1Frame *frame) {
    if (!frame)
        return;
    if (frame->kind == PPATOM_EVENTS_V1_FRAME_DOCUMENT ||
        frame->kind == PPATOM_EVENTS_V1_FRAME_EXPRESSION) {
        free(frame->payload.atoms.data);
    } else if (frame->kind == PPATOM_EVENTS_V1_FRAME_LEAF) {
        free(frame->payload.bytes.data);
    }
    memset(frame, 0, sizeof(*frame));
}

static void ppae_v1_heap_release(PPAtomEventsV1Impl *impl) {
    uint32_t index;

    if (!impl)
        return;
    for (index = 0u; index < impl->frame_len; index++)
        ppae_v1_frame_release(&impl->frames[index]);
    free(impl->frames);
    free(impl->document.data);
    free(impl->variables.data);
    free(impl->rules);
    free(impl->wrappers);
    impl->frames = NULL;
    impl->frame_len = 0u;
    impl->frame_cap = 0u;
    memset(&impl->document, 0, sizeof(impl->document));
    memset(&impl->variables, 0, sizeof(impl->variables));
    impl->rules = NULL;
    impl->rule_len = 0u;
    impl->wrappers = NULL;
    impl->wrapper_len = 0u;
}

static void ppae_v1_abort(PPAtomEventsV1Impl *impl) {
    if (!impl)
        return;
    if (impl->active && impl->arena)
        arena_reset(impl->arena, impl->mark);
    ppae_v1_heap_release(impl);
    impl->plan = NULL;
    impl->arena = NULL;
    impl->depth_limit = 0u;
    impl->active = false;
    impl->document_started = false;
    impl->document_closed = false;
}

static bool ppae_v1_fail(PPAtomEventsV1Impl *impl,
                         char *error,
                         size_t error_cap,
                         const char *message) {
    ppae_v1_set_error(error, error_cap, "%s", message);
    ppae_v1_abort(impl);
    return false;
}

static bool ppae_v1_atom_vec_push(PPAtomEventsV1AtomVec *vec,
                                  Atom *atom) {
    Atom **next;
    uint32_t cap;

    if (vec->len >= PPATOM_EVENTS_V1_MAX_CONTAINER_ITEMS)
        return false;
    if (vec->len == vec->cap) {
        cap = vec->cap ? vec->cap * 2u : 8u;
        if (cap < vec->cap || cap > PPATOM_EVENTS_V1_MAX_CONTAINER_ITEMS)
            cap = PPATOM_EVENTS_V1_MAX_CONTAINER_ITEMS;
        next = realloc(vec->data, sizeof(*next) * (size_t)cap);
        if (!next)
            return false;
        vec->data = next;
        vec->cap = cap;
    }
    vec->data[vec->len++] = atom;
    return true;
}

static bool ppae_v1_byte_reserve(PPAtomEventsV1ByteVec *vec,
                                 uint32_t additional) {
    uint8_t *next;
    uint32_t required;
    uint32_t cap;

    if (additional > PPATOM_EVENTS_V1_MAX_TEXT_BYTES - vec->len)
        return false;
    required = vec->len + additional + 1u;
    if (required <= vec->cap)
        return true;
    cap = vec->cap ? vec->cap : 32u;
    while (cap < required) {
        uint32_t next_cap =
            cap > (PPATOM_EVENTS_V1_MAX_TEXT_BYTES + 1u) / 2u
            ? PPATOM_EVENTS_V1_MAX_TEXT_BYTES + 1u
            : cap * 2u;
        if (next_cap <= cap)
            return false;
        cap = next_cap;
    }
    next = realloc(vec->data, cap);
    if (!next)
        return false;
    vec->data = next;
    vec->cap = cap;
    return true;
}

static bool ppae_v1_byte_push(PPAtomEventsV1ByteVec *vec, uint8_t byte) {
    if (!ppae_v1_byte_reserve(vec, 1u))
        return false;
    vec->data[vec->len++] = byte;
    if (vec->len < vec->cap)
        vec->data[vec->len] = 0u;
    return true;
}

static bool ppae_v1_utf8_push(PPAtomEventsV1ByteVec *vec,
                              uint32_t scalar) {
    if (scalar <= 0x7fu)
        return ppae_v1_byte_push(vec, (uint8_t)scalar);
    if (scalar <= 0x7ffu)
        return ppae_v1_byte_push(vec, (uint8_t)(0xc0u | (scalar >> 6u))) &&
            ppae_v1_byte_push(vec, (uint8_t)(0x80u | (scalar & 0x3fu)));
    if (scalar <= 0xffffu)
        return ppae_v1_byte_push(vec, (uint8_t)(0xe0u | (scalar >> 12u))) &&
            ppae_v1_byte_push(
                vec, (uint8_t)(0x80u | ((scalar >> 6u) & 0x3fu))) &&
            ppae_v1_byte_push(vec, (uint8_t)(0x80u | (scalar & 0x3fu)));
    return ppae_v1_byte_push(vec, (uint8_t)(0xf0u | (scalar >> 18u))) &&
        ppae_v1_byte_push(
            vec, (uint8_t)(0x80u | ((scalar >> 12u) & 0x3fu))) &&
        ppae_v1_byte_push(
            vec, (uint8_t)(0x80u | ((scalar >> 6u) & 0x3fu))) &&
        ppae_v1_byte_push(vec, (uint8_t)(0x80u | (scalar & 0x3fu)));
}

static PPAtomEventsV1Frame *ppae_v1_top(PPAtomEventsV1Impl *impl) {
    return impl->frame_len ? &impl->frames[impl->frame_len - 1u] : NULL;
}

static bool ppae_v1_push_frame(PPAtomEventsV1Impl *impl,
                               PPAtomEventsV1FrameKind kind,
                               uint32_t operand) {
    PPAtomEventsV1Frame *next;
    uint32_t cap;

    if (impl->frame_len >= impl->depth_limit)
        return false;
    if (impl->frame_len == impl->frame_cap) {
        cap = impl->frame_cap ? impl->frame_cap * 2u : 8u;
        if (cap < impl->frame_cap || cap > impl->depth_limit)
            cap = impl->depth_limit;
        next = realloc(impl->frames, sizeof(*next) * (size_t)cap);
        if (!next)
            return false;
        impl->frames = next;
        impl->frame_cap = cap;
    }
    memset(&impl->frames[impl->frame_len], 0,
           sizeof(impl->frames[impl->frame_len]));
    impl->frames[impl->frame_len].kind = kind;
    impl->frames[impl->frame_len].operand = operand;
    impl->frame_len++;
    return true;
}

static bool ppae_v1_parent_accepts_atom(PPAtomEventsV1Impl *impl) {
    PPAtomEventsV1Frame *parent = ppae_v1_top(impl);
    return parent &&
        (parent->kind == PPATOM_EVENTS_V1_FRAME_DOCUMENT ||
         parent->kind == PPATOM_EVENTS_V1_FRAME_EXPRESSION);
}

static bool ppae_v1_append_atom(PPAtomEventsV1Impl *impl, Atom *atom) {
    PPAtomEventsV1Frame *parent = ppae_v1_top(impl);
    return parent && atom &&
        (parent->kind == PPATOM_EVENTS_V1_FRAME_DOCUMENT ||
         parent->kind == PPATOM_EVENTS_V1_FRAME_EXPRESSION) &&
        ppae_v1_atom_vec_push(&parent->payload.atoms, atom);
}

static bool ppae_v1_variable(PPAtomEventsV1Impl *impl,
                             const uint8_t *bytes,
                             uint32_t len,
                             Atom **out) {
    PPAtomEventsV1VarEnv *environment = &impl->variables;
    SymbolId spelling = len == 0u
        ? SYMBOL_ID_NONE
        : symbol_intern_bytes(g_symbols, bytes, len);
    uint32_t index;

    for (index = 0u; index < environment->len; index++) {
        if (environment->data[index].spelling == spelling) {
            *out = environment->data[index].variable;
            return true;
        }
    }
    if (environment->len >= PPATOM_EVENTS_V1_MAX_PLAN_ITEMS)
        return false;
    if (environment->len == environment->cap) {
        uint32_t cap = environment->cap ? environment->cap * 2u : 8u;
        PPAtomEventsV1VarBinding *next;
        if (cap < environment->cap || cap > PPATOM_EVENTS_V1_MAX_PLAN_ITEMS)
            cap = PPATOM_EVENTS_V1_MAX_PLAN_ITEMS;
        next = realloc(environment->data, sizeof(*next) * (size_t)cap);
        if (!next)
            return false;
        environment->data = next;
        environment->cap = cap;
    }
    *out = atom_var_with_spelling(impl->arena, spelling, fresh_var_id());
    if (!*out)
        return false;
    environment->data[environment->len++] =
        (PPAtomEventsV1VarBinding){spelling, *out};
    return true;
}

static bool ppae_v1_leaf_atom(PPAtomEventsV1Impl *impl,
                              PPAtomEventsV1Frame *frame,
                              Atom **out,
                              char *error,
                              size_t error_cap) {
    const PPAtomProjectionV1RuleView *rule = &impl->rules[frame->operand];
    PPAtomEventsV1ByteVec *bytes = &frame->payload.bytes;

    if (rule->text_mode == PPATOM_PROJECTION_V1_TEXT_DECODED_QUOTED &&
        !ppae_v1_byte_push(bytes, (uint8_t)'"')) {
        ppae_v1_set_error(error, error_cap,
                          "projected text exceeds its byte limit");
        return false;
    }
    if (bytes->len == 0u &&
        rule->kind == PPATOM_PROJECTION_V1_RULE_TEXT &&
        rule->output_kind == PPATOM_PROJECTION_V1_OUTPUT_SYMBOL) {
        ppae_v1_set_error(error, error_cap,
                          "host symbol spelling must not be empty");
        return false;
    }
    if (rule->kind == PPATOM_PROJECTION_V1_RULE_VARIABLE) {
        if (!ppae_v1_variable(impl, bytes->data, bytes->len, out)) {
            ppae_v1_set_error(error, error_cap,
                              "failed to materialize host variable");
            return false;
        }
        return true;
    }
    if (rule->output_kind == PPATOM_PROJECTION_V1_OUTPUT_SYMBOL) {
        SymbolId spelling = symbol_intern_bytes(
            g_symbols, bytes->data, bytes->len);
        if (spelling == SYMBOL_ID_NONE) {
            ppae_v1_set_error(error, error_cap,
                              "failed to intern host symbol spelling");
            return false;
        }
        *out = atom_symbol_id(impl->arena, spelling);
        return *out != NULL;
    }
    if (bytes->len > 0u && memchr(bytes->data, 0, bytes->len)) {
        ppae_v1_set_error(error, error_cap,
                          "grounded string spelling contains an embedded NUL");
        return false;
    }
    *out = atom_string(
        impl->arena, bytes->len ? (const char *)bytes->data : "");
    return *out != NULL;
}

static int32_t ppae_v1_hex_digit(uint32_t scalar) {
    if (scalar >= (uint32_t)'0' && scalar <= (uint32_t)'9')
        return (int32_t)(scalar - (uint32_t)'0');
    if (scalar >= (uint32_t)'a' && scalar <= (uint32_t)'f')
        return (int32_t)(scalar - (uint32_t)'a' + 10u);
    if (scalar >= (uint32_t)'A' && scalar <= (uint32_t)'F')
        return (int32_t)(scalar - (uint32_t)'A' + 10u);
    return -1;
}

static bool ppae_v1_wrapper_scalar(PPAtomEventsV1Impl *impl,
                                   PPAtomEventsV1Frame *frame,
                                   uint32_t *out,
                                   char *error,
                                   size_t error_cap) {
    const PPAtomProjectionV1WrapperView *wrapper =
        &impl->wrappers[frame->operand];
    const PPAtomEventsV1ScalarVec *source = &frame->payload.scalars;
    uint32_t index;

    if (wrapper->kind == PPATOM_PROJECTION_V1_WRAPPER_MAP) {
        uint32_t result;
        if (source->len != 1u) {
            ppae_v1_set_error(error, error_cap,
                              "mapped escape must contain one scalar");
            return false;
        }
        result = source->data[0];
        for (index = 0u; index < wrapper->mapping_len; index++) {
            if (wrapper->mappings[index].source == result) {
                result = wrapper->mappings[index].target;
                break;
            }
        }
        if (index == wrapper->mapping_len &&
            wrapper->map_default == PPATOM_PROJECTION_V1_MAP_REJECT) {
            ppae_v1_set_error(error, error_cap, "unmapped escape scalar");
            return false;
        }
        *out = result;
        return true;
    }
    if (source->len < wrapper->min_digits ||
        source->len > wrapper->max_digits) {
        ppae_v1_set_error(error, error_cap,
                          "hex escape has the wrong digit count");
        return false;
    }
    {
        uint64_t value = 0u;
        for (index = 0u; index < source->len; index++) {
            int32_t digit = ppae_v1_hex_digit(source->data[index]);
            if (digit < 0) {
                ppae_v1_set_error(error, error_cap,
                                  "hex escape contains a non-hex scalar");
                return false;
            }
            value = value * 16u + (uint32_t)digit;
            if (value > wrapper->max_value) {
                ppae_v1_set_error(error, error_cap,
                                  "hex escape exceeds its value bound");
                return false;
            }
        }
        *out = (uint32_t)value;
    }
    if (!ppae_v1_unicode_scalar(*out)) {
        ppae_v1_set_error(error, error_cap,
                          "hex escape is not a Unicode scalar");
        return false;
    }
    return true;
}

void ppatom_projection_events_v1_builder_init(
    PPAtomProjectionEventsV1Builder *builder) {
    if (builder)
        builder->implementation = NULL;
}

void ppatom_projection_events_v1_builder_free(
    PPAtomProjectionEventsV1Builder *builder) {
    PPAtomEventsV1Impl *impl;

    if (!builder)
        return;
    impl = builder->implementation;
    ppae_v1_abort(impl);
    free(impl);
    builder->implementation = NULL;
}

bool ppatom_projection_events_v1_builder_bind(
    PPAtomProjectionEventsV1Builder *builder,
    const PPAtomProjectionV1Plan *plan,
    Arena *output_arena,
    uint32_t depth_limit,
    char *error_buf,
    size_t error_buf_size) {
    PPAtomEventsV1Impl *impl;
    uint32_t index;

    if (error_buf && error_buf_size)
        error_buf[0] = '\0';
    if (!builder || !plan || !output_arena || depth_limit == 0u) {
        ppae_v1_set_error(error_buf, error_buf_size,
                          "invalid projection-event bind arguments");
        return false;
    }
    if (!builder->implementation) {
        builder->implementation = calloc(1u, sizeof(PPAtomEventsV1Impl));
        if (!builder->implementation) {
            ppae_v1_set_error(error_buf, error_buf_size,
                              "out of memory allocating event builder");
            return false;
        }
    }
    impl = builder->implementation;
    ppae_v1_abort(impl);
    if (!ppatom_projection_v1_plan_rule_count(
            plan, &impl->rule_len, error_buf, error_buf_size) ||
        !ppatom_projection_v1_plan_wrapper_count(
            plan, &impl->wrapper_len, error_buf, error_buf_size) ||
        impl->rule_len == 0u ||
        impl->rule_len > PPATOM_EVENTS_V1_MAX_PLAN_ITEMS ||
        impl->wrapper_len > PPATOM_EVENTS_V1_MAX_PLAN_ITEMS) {
        if (!error_buf || !error_buf[0]) {
            ppae_v1_set_error(error_buf, error_buf_size,
                              "projection-event plan inventory is invalid");
        }
        ppae_v1_heap_release(impl);
        return false;
    }
    impl->rules = calloc(impl->rule_len, sizeof(*impl->rules));
    impl->wrappers = calloc(
        impl->wrapper_len ? impl->wrapper_len : 1u,
        sizeof(*impl->wrappers));
    if (!impl->rules || !impl->wrappers) {
        ppae_v1_set_error(error_buf, error_buf_size,
                          "out of memory binding projection-event plan");
        ppae_v1_heap_release(impl);
        return false;
    }
    for (index = 0u; index < impl->rule_len; index++) {
        if (!ppatom_projection_v1_plan_rule(
                plan, index, &impl->rules[index],
                error_buf, error_buf_size)) {
            ppae_v1_heap_release(impl);
            return false;
        }
    }
    for (index = 0u; index < impl->wrapper_len; index++) {
        if (!ppatom_projection_v1_plan_wrapper(
                plan, index, &impl->wrappers[index],
                error_buf, error_buf_size)) {
            ppae_v1_heap_release(impl);
            return false;
        }
    }
    impl->plan = plan;
    impl->arena = output_arena;
    impl->mark = arena_mark(output_arena);
    impl->depth_limit = depth_limit;
    impl->active = true;
    return true;
}

bool ppatom_projection_events_v1_builder_emit(
    PPAtomProjectionEventsV1Builder *builder,
    PPAtomProjectionEventsV1Event event,
    char *error_buf,
    size_t error_buf_size) {
    PPAtomEventsV1Impl *impl = builder ? builder->implementation : NULL;
    PPAtomEventsV1Frame *frame;

    if (error_buf && error_buf_size)
        error_buf[0] = '\0';
    if (!impl || !impl->active) {
        ppae_v1_set_error(error_buf, error_buf_size,
                          "projection-event builder is not active");
        return false;
    }
    switch (event.kind) {
    case PPATOM_PROJECTION_EVENTS_V1_DOCUMENT_BEGIN:
        if (event.operand != 0u || impl->document_started ||
            impl->frame_len != 0u) {
            return ppae_v1_fail(
                impl, error_buf, error_buf_size,
                "document begin is out of sequence");
        }
        if (!ppae_v1_push_frame(
                impl, PPATOM_EVENTS_V1_FRAME_DOCUMENT, 0u)) {
            return ppae_v1_fail(
                impl, error_buf, error_buf_size,
                "document exceeds the event depth or memory limit");
        }
        impl->document_started = true;
        return true;

    case PPATOM_PROJECTION_EVENTS_V1_DOCUMENT_END:
        frame = ppae_v1_top(impl);
        if (event.operand != 0u || !frame ||
            frame->kind != PPATOM_EVENTS_V1_FRAME_DOCUMENT ||
            impl->frame_len != 1u) {
            return ppae_v1_fail(
                impl, error_buf, error_buf_size,
                "document end does not close the active document");
        }
        impl->document = frame->payload.atoms;
        memset(&frame->payload.atoms, 0, sizeof(frame->payload.atoms));
        impl->frame_len--;
        impl->document_closed = true;
        return true;

    case PPATOM_PROJECTION_EVENTS_V1_EXPRESSION_BEGIN:
        if (event.operand != 0u || !ppae_v1_parent_accepts_atom(impl)) {
            return ppae_v1_fail(
                impl, error_buf, error_buf_size,
                "expression begin requires a document or expression parent");
        }
        if (ppae_v1_top(impl)->kind == PPATOM_EVENTS_V1_FRAME_DOCUMENT)
            impl->variables.len = 0u;
        if (!ppae_v1_push_frame(
                impl, PPATOM_EVENTS_V1_FRAME_EXPRESSION, 0u)) {
            return ppae_v1_fail(
                impl, error_buf, error_buf_size,
                "expression exceeds the event depth or memory limit");
        }
        return true;

    case PPATOM_PROJECTION_EVENTS_V1_EXPRESSION_END:
        frame = ppae_v1_top(impl);
        if (event.operand != 0u || !frame ||
            frame->kind != PPATOM_EVENTS_V1_FRAME_EXPRESSION) {
            return ppae_v1_fail(
                impl, error_buf, error_buf_size,
                "expression end does not match an expression begin");
        }
        {
            Atom *expression = atom_expr(
                impl->arena, frame->payload.atoms.data,
                frame->payload.atoms.len);
            free(frame->payload.atoms.data);
            memset(&frame->payload.atoms, 0, sizeof(frame->payload.atoms));
            impl->frame_len--;
            if (!ppae_v1_append_atom(impl, expression)) {
                return ppae_v1_fail(
                    impl, error_buf, error_buf_size,
                    "failed to append projected expression");
            }
        }
        return true;

    case PPATOM_PROJECTION_EVENTS_V1_LEAF_BEGIN:
        if (event.operand >= impl->rule_len ||
            !ppae_v1_parent_accepts_atom(impl)) {
            return ppae_v1_fail(
                impl, error_buf, error_buf_size,
                "leaf begin has an invalid rule or parent");
        }
        if (ppae_v1_top(impl)->kind == PPATOM_EVENTS_V1_FRAME_DOCUMENT)
            impl->variables.len = 0u;
        if (!ppae_v1_push_frame(
                impl, PPATOM_EVENTS_V1_FRAME_LEAF, event.operand)) {
            return ppae_v1_fail(
                impl, error_buf, error_buf_size,
                "leaf exceeds the event depth or memory limit");
        }
        if (impl->rules[event.operand].text_mode ==
                PPATOM_PROJECTION_V1_TEXT_DECODED_QUOTED &&
            !ppae_v1_byte_push(
                &ppae_v1_top(impl)->payload.bytes, (uint8_t)'"')) {
            return ppae_v1_fail(
                impl, error_buf, error_buf_size,
                "projected text exceeds its byte limit");
        }
        return true;

    case PPATOM_PROJECTION_EVENTS_V1_LEAF_END:
        frame = ppae_v1_top(impl);
        if (!frame || frame->kind != PPATOM_EVENTS_V1_FRAME_LEAF ||
            event.operand != frame->operand) {
            return ppae_v1_fail(
                impl, error_buf, error_buf_size,
                "leaf end does not match its leaf begin");
        }
        {
            Atom *leaf = NULL;
            if (!ppae_v1_leaf_atom(
                    impl, frame, &leaf, error_buf, error_buf_size)) {
                ppae_v1_abort(impl);
                return false;
            }
            free(frame->payload.bytes.data);
            memset(&frame->payload.bytes, 0, sizeof(frame->payload.bytes));
            impl->frame_len--;
            if (!ppae_v1_append_atom(impl, leaf)) {
                return ppae_v1_fail(
                    impl, error_buf, error_buf_size,
                    "failed to append projected leaf");
            }
        }
        return true;

    case PPATOM_PROJECTION_EVENTS_V1_WRAPPER_BEGIN:
        frame = ppae_v1_top(impl);
        if (event.operand >= impl->wrapper_len || !frame ||
            frame->kind != PPATOM_EVENTS_V1_FRAME_LEAF ||
            impl->rules[frame->operand].text_mode ==
                PPATOM_PROJECTION_V1_TEXT_RAW) {
            return ppae_v1_fail(
                impl, error_buf, error_buf_size,
                "wrapper begin requires a decoded leaf and valid wrapper");
        }
        if (!ppae_v1_push_frame(
                impl, PPATOM_EVENTS_V1_FRAME_WRAPPER, event.operand)) {
            return ppae_v1_fail(
                impl, error_buf, error_buf_size,
                "wrapper exceeds the event depth or memory limit");
        }
        return true;

    case PPATOM_PROJECTION_EVENTS_V1_WRAPPER_END:
        frame = ppae_v1_top(impl);
        if (!frame || frame->kind != PPATOM_EVENTS_V1_FRAME_WRAPPER ||
            event.operand != frame->operand) {
            return ppae_v1_fail(
                impl, error_buf, error_buf_size,
                "wrapper end does not match its wrapper begin");
        }
        {
            uint32_t scalar;
            if (!ppae_v1_wrapper_scalar(
                    impl, frame, &scalar, error_buf, error_buf_size)) {
                ppae_v1_abort(impl);
                return false;
            }
            impl->frame_len--;
            frame = ppae_v1_top(impl);
            if (!frame || frame->kind != PPATOM_EVENTS_V1_FRAME_LEAF ||
                !ppae_v1_utf8_push(&frame->payload.bytes, scalar)) {
                return ppae_v1_fail(
                    impl, error_buf, error_buf_size,
                    "failed to append decoded wrapper scalar");
            }
        }
        return true;

    case PPATOM_PROJECTION_EVENTS_V1_SCALAR:
        frame = ppae_v1_top(impl);
        if (!ppae_v1_unicode_scalar(event.operand) || !frame) {
            return ppae_v1_fail(
                impl, error_buf, error_buf_size,
                "scalar event is outside a text frame or is not Unicode");
        }
        if (frame->kind == PPATOM_EVENTS_V1_FRAME_LEAF) {
            if (!ppae_v1_utf8_push(&frame->payload.bytes, event.operand)) {
                return ppae_v1_fail(
                    impl, error_buf, error_buf_size,
                    "projected text exceeds its byte limit");
            }
            return true;
        }
        if (frame->kind == PPATOM_EVENTS_V1_FRAME_WRAPPER) {
            if (frame->payload.scalars.len >=
                    PPATOM_EVENTS_V1_MAX_WRAPPER_SCALARS) {
                return ppae_v1_fail(
                    impl, error_buf, error_buf_size,
                    "wrapper payload exceeds its scalar limit");
            }
            frame->payload.scalars.data[frame->payload.scalars.len++] =
                event.operand;
            return true;
        }
        return ppae_v1_fail(
            impl, error_buf, error_buf_size,
            "scalar event requires a leaf or wrapper frame");
    }
    return ppae_v1_fail(
        impl, error_buf, error_buf_size,
        "projection event kind is invalid");
}

bool ppatom_projection_events_v1_builder_finish(
    PPAtomProjectionEventsV1Builder *builder,
    Atom ***out_atoms,
    uint32_t *out_len,
    char *error_buf,
    size_t error_buf_size) {
    PPAtomEventsV1Impl *impl = builder ? builder->implementation : NULL;
    Atom **atoms = NULL;
    uint32_t len;

    if (error_buf && error_buf_size)
        error_buf[0] = '\0';
    if (out_atoms)
        *out_atoms = NULL;
    if (out_len)
        *out_len = 0u;
    if (!impl || !impl->active) {
        ppae_v1_set_error(error_buf, error_buf_size,
                          "projection-event builder is not active");
        return false;
    }
    if (!out_atoms || !out_len || !impl->document_started ||
        !impl->document_closed || impl->frame_len != 0u) {
        return ppae_v1_fail(
            impl, error_buf, error_buf_size,
            "projection-event document is incomplete");
    }
    len = impl->document.len;
    if (len > 0u) {
        atoms = arena_alloc(impl->arena, sizeof(*atoms) * (size_t)len);
        if (!atoms) {
            return ppae_v1_fail(
                impl, error_buf, error_buf_size,
                "out of memory materializing projected document");
        }
        memcpy(atoms, impl->document.data, sizeof(*atoms) * (size_t)len);
    }
    impl->active = false;
    ppae_v1_heap_release(impl);
    impl->plan = NULL;
    impl->arena = NULL;
    impl->depth_limit = 0u;
    impl->document_started = false;
    impl->document_closed = false;
    *out_atoms = atoms;
    *out_len = len;
    return true;
}

enum {
    PPATOM_EVENTS_V1_MAX_SEMANTIC_NODES =
        2u * PPATOM_EVENTS_V1_MAX_TEXT_BYTES + 1024u
};

typedef struct {
    const Atom **data;
    uint32_t len;
    uint32_t cap;
} PPAtomEventsV1ConstAtomVec;

typedef struct {
    const PPAtomProjectionV1Plan *plan;
    PPAtomProjectionEventsV1Builder *builder;
    PPAtomProjectionV1CarrierView carriers;
    uint32_t rule_len;
    uint32_t wrapper_len;
    uint32_t depth_limit;
    uint32_t node_len;
    char *error;
    size_t error_cap;
} PPAtomEventsV1Bridge;

static bool ppae_v1_const_atom_push(PPAtomEventsV1ConstAtomVec *vec,
                                    const Atom *atom) {
    const Atom **next;
    uint32_t cap;

    if (vec->len >= PPATOM_EVENTS_V1_MAX_SEMANTIC_NODES)
        return false;
    if (vec->len == vec->cap) {
        cap = vec->cap ? vec->cap * 2u : 16u;
        if (cap < vec->cap || cap > PPATOM_EVENTS_V1_MAX_SEMANTIC_NODES)
            cap = PPATOM_EVENTS_V1_MAX_SEMANTIC_NODES;
        next = realloc(vec->data, sizeof(*next) * (size_t)cap);
        if (!next)
            return false;
        vec->data = next;
        vec->cap = cap;
    }
    vec->data[vec->len++] = atom;
    return true;
}

static bool ppae_v1_expr_head(const Atom *atom,
                              const char *head,
                              CettaExprLen argument_len) {
    return atom && atom->kind == ATOM_EXPR &&
        atom->expr.len == argument_len + 1u &&
        atom_is_symbol(atom->expr.elems[0], head);
}

static const char *ppae_v1_symbol_name(const Atom *atom) {
    return atom && atom->kind == ATOM_SYMBOL
        ? atom_name_cstr((Atom *)atom) : NULL;
}

static bool ppae_v1_bridge_emit(PPAtomEventsV1Bridge *bridge,
                                PPAtomProjectionEventsV1Kind kind,
                                uint32_t operand) {
    return ppatom_projection_events_v1_builder_emit(
        bridge->builder,
        (PPAtomProjectionEventsV1Event){kind, operand},
        bridge->error, bridge->error_cap);
}

static bool ppae_v1_bridge_rule(PPAtomEventsV1Bridge *bridge,
                                const char *label,
                                uint32_t *index,
                                PPAtomProjectionV1RuleView *out) {
    for (*index = 0u; *index < bridge->rule_len; (*index)++) {
        if (!ppatom_projection_v1_plan_rule(
                bridge->plan, *index, out,
                bridge->error, bridge->error_cap)) {
            return false;
        }
        if (strcmp(out->source_label, label) == 0)
            return true;
    }
    return false;
}

static bool ppae_v1_bridge_wrapper(PPAtomEventsV1Bridge *bridge,
                                   const char *label,
                                   uint32_t *index) {
    PPAtomProjectionV1WrapperView wrapper;

    for (*index = 0u; *index < bridge->wrapper_len; (*index)++) {
        if (!ppatom_projection_v1_plan_wrapper(
                bridge->plan, *index, &wrapper,
                bridge->error, bridge->error_cap)) {
            return false;
        }
        if (strcmp(wrapper.label, label) == 0)
            return true;
    }
    return false;
}

static bool ppae_v1_bridge_text(PPAtomEventsV1Bridge *bridge,
                                const Atom *term,
                                bool allow_wrappers,
                                uint32_t depth) {
    PPAtomEventsV1ConstAtomVec stack = {0};
    bool ok = false;

    if (depth > bridge->depth_limit) {
        ppae_v1_set_error(bridge->error, bridge->error_cap,
                          "semantic-event bridge exceeded its depth limit");
        return false;
    }
    if (!ppae_v1_const_atom_push(&stack, term)) {
        ppae_v1_set_error(bridge->error, bridge->error_cap,
                          "semantic-event text stack exceeds its limit");
        return false;
    }
    while (stack.len > 0u) {
        const Atom *current = stack.data[--stack.len];
        uint32_t scalar;

        if (bridge->node_len++ >= PPATOM_EVENTS_V1_MAX_SEMANTIC_NODES) {
            ppae_v1_set_error(
                bridge->error, bridge->error_cap,
                "semantic-event bridge exceeds its node limit");
            goto done;
        }
        if (atom_is_symbol((Atom *)current, bridge->carriers.nil_label))
            continue;
        if (ppae_v1_expr_head(
                current, bridge->carriers.codepoint_label, 1u)) {
            const Atom *value = current->expr.elems[1];
            if (!value || value->kind != ATOM_GROUNDED ||
                value->ground.gkind != GV_INT || value->ground.ival < 0 ||
                (uint64_t)value->ground.ival > UINT32_MAX) {
                ppae_v1_set_error(
                    bridge->error, bridge->error_cap,
                    "semantic-event codepoint is not uint32");
                goto done;
            }
            scalar = (uint32_t)value->ground.ival;
            if (!ppae_v1_bridge_emit(
                    bridge, PPATOM_PROJECTION_EVENTS_V1_SCALAR, scalar)) {
                goto done;
            }
            continue;
        }
        if (current && current->kind == ATOM_EXPR &&
            current->expr.len == 3u &&
            (atom_is_symbol(current->expr.elems[0],
                            bridge->carriers.pair_label) ||
             atom_is_symbol(current->expr.elems[0],
                            bridge->carriers.cons_label))) {
            if (!ppae_v1_const_atom_push(
                    &stack, current->expr.elems[2]) ||
                !ppae_v1_const_atom_push(
                    &stack, current->expr.elems[1])) {
                ppae_v1_set_error(
                    bridge->error, bridge->error_cap,
                    "semantic-event text stack exceeds its limit");
                goto done;
            }
            continue;
        }
        if (allow_wrappers && current && current->kind == ATOM_EXPR &&
            current->expr.len == 3u &&
            atom_is_symbol(
                current->expr.elems[0], bridge->carriers.node_label)) {
            const char *label = ppae_v1_symbol_name(
                current->expr.elems[1]);
            uint32_t wrapper_index;
            if (!label || !ppae_v1_bridge_wrapper(
                    bridge, label, &wrapper_index)) {
                if (!bridge->error || bridge->error[0] == '\0') {
                    ppae_v1_set_error(
                        bridge->error, bridge->error_cap,
                        "semantic-event text wrapper is unknown");
                }
                goto done;
            }
            if (!ppae_v1_bridge_emit(
                    bridge, PPATOM_PROJECTION_EVENTS_V1_WRAPPER_BEGIN,
                    wrapper_index) ||
                !ppae_v1_bridge_text(
                    bridge, current->expr.elems[2], false, depth + 1u) ||
                !ppae_v1_bridge_emit(
                    bridge, PPATOM_PROJECTION_EVENTS_V1_WRAPPER_END,
                    wrapper_index)) {
                goto done;
            }
            continue;
        }
        ppae_v1_set_error(bridge->error, bridge->error_cap,
                          "malformed semantic-event text spine");
        goto done;
    }
    ok = true;

done:
    free(stack.data);
    return ok;
}

static bool ppae_v1_bridge_node(PPAtomEventsV1Bridge *bridge,
                                const Atom *term,
                                uint32_t depth);

static bool ppae_v1_bridge_node_list(PPAtomEventsV1Bridge *bridge,
                                     const Atom *list,
                                     uint32_t depth) {
    const Atom *current = list;

    while (!atom_is_symbol((Atom *)current, bridge->carriers.nil_label)) {
        if (bridge->node_len++ >= PPATOM_EVENTS_V1_MAX_SEMANTIC_NODES) {
            ppae_v1_set_error(
                bridge->error, bridge->error_cap,
                "semantic-event bridge exceeds its node limit");
            return false;
        }
        if (!current || current->kind != ATOM_EXPR ||
            current->expr.len != 3u ||
            !(atom_is_symbol(current->expr.elems[0],
                             bridge->carriers.pair_label) ||
              atom_is_symbol(current->expr.elems[0],
                             bridge->carriers.cons_label))) {
            ppae_v1_set_error(bridge->error, bridge->error_cap,
                              "malformed semantic-event node list");
            return false;
        }
        if (!ppae_v1_bridge_node(
                bridge, current->expr.elems[1], depth + 1u)) {
            return false;
        }
        current = current->expr.elems[2];
    }
    return true;
}

static bool ppae_v1_bridge_node(PPAtomEventsV1Bridge *bridge,
                                const Atom *term,
                                uint32_t depth) {
    const char *label;
    const Atom *payload;
    PPAtomProjectionV1RuleView rule;
    uint32_t rule_index;

    if (depth > bridge->depth_limit) {
        ppae_v1_set_error(bridge->error, bridge->error_cap,
                          "semantic-event bridge exceeded its depth limit");
        return false;
    }
    if (!term || term->kind != ATOM_EXPR || term->expr.len != 3u ||
        !atom_is_symbol(term->expr.elems[0], bridge->carriers.node_label) ||
        !(label = ppae_v1_symbol_name(term->expr.elems[1]))) {
        ppae_v1_set_error(bridge->error, bridge->error_cap,
                          "expected a neutral semantic-event node");
        return false;
    }
    payload = term->expr.elems[2];
    if (strcmp(label, bridge->carriers.expression_label) == 0) {
        return ppae_v1_bridge_emit(
                   bridge, PPATOM_PROJECTION_EVENTS_V1_EXPRESSION_BEGIN, 0u) &&
            ppae_v1_bridge_node_list(bridge, payload, depth + 1u) &&
            ppae_v1_bridge_emit(
                bridge, PPATOM_PROJECTION_EVENTS_V1_EXPRESSION_END, 0u);
    }
    if (!ppae_v1_bridge_rule(
            bridge, label, &rule_index, &rule)) {
        if (!bridge->error || bridge->error[0] == '\0') {
            ppae_v1_set_error(
                bridge->error, bridge->error_cap,
                "neutral semantic node has no event projection rule");
        }
        return false;
    }
    return ppae_v1_bridge_emit(
               bridge, PPATOM_PROJECTION_EVENTS_V1_LEAF_BEGIN, rule_index) &&
        ppae_v1_bridge_text(
            bridge, payload,
            rule.text_mode != PPATOM_PROJECTION_V1_TEXT_RAW,
            depth + 1u) &&
        ppae_v1_bridge_emit(
            bridge, PPATOM_PROJECTION_EVENTS_V1_LEAF_END, rule_index);
}

bool ppatom_projection_events_v1_project_document(
    const PPAtomProjectionV1Plan *plan,
    const Atom *semantic_result,
    Arena *output_arena,
    uint32_t depth_limit,
    Atom ***out_atoms,
    uint32_t *out_len,
    char *error_buf,
    size_t error_buf_size) {
    PPAtomProjectionEventsV1Builder builder;
    PPAtomEventsV1Bridge bridge;
    const Atom *document;
    bool ok = false;

    if (error_buf && error_buf_size)
        error_buf[0] = '\0';
    if (out_atoms)
        *out_atoms = NULL;
    if (out_len)
        *out_len = 0u;
    ppatom_projection_events_v1_builder_init(&builder);
    memset(&bridge, 0, sizeof(bridge));
    if (!plan || !semantic_result || !output_arena || depth_limit == 0u ||
        !out_atoms || !out_len ||
        !ppae_v1_expr_head(semantic_result, "result", 2u)) {
        ppae_v1_set_error(error_buf, error_buf_size,
                          "invalid semantic-event projection arguments");
        goto done;
    }
    bridge = (PPAtomEventsV1Bridge){
        .plan = plan,
        .builder = &builder,
        .depth_limit = depth_limit,
        .error = error_buf,
        .error_cap = error_buf_size,
    };
    if (!ppatom_projection_v1_plan_carriers(
            plan, &bridge.carriers, error_buf, error_buf_size) ||
        !ppatom_projection_v1_plan_rule_count(
            plan, &bridge.rule_len, error_buf, error_buf_size) ||
        !ppatom_projection_v1_plan_wrapper_count(
            plan, &bridge.wrapper_len, error_buf, error_buf_size)) {
        goto done;
    }
    if (!atom_is_symbol(
            semantic_result->expr.elems[2], bridge.carriers.nil_label)) {
        ppae_v1_set_error(error_buf, error_buf_size,
                          "semantic-event result has trailing values");
        goto done;
    }
    document = semantic_result->expr.elems[1];
    if (!document || document->kind != ATOM_EXPR ||
        document->expr.len != 3u ||
        !atom_is_symbol(document->expr.elems[0],
                        bridge.carriers.node_label) ||
        !atom_is_symbol(document->expr.elems[1],
                        bridge.carriers.document_label)) {
        ppae_v1_set_error(error_buf, error_buf_size,
                          "semantic-event result lacks a document node");
        goto done;
    }
    if (!ppatom_projection_events_v1_builder_bind(
            &builder, plan, output_arena, depth_limit,
            error_buf, error_buf_size) ||
        !ppae_v1_bridge_emit(
            &bridge, PPATOM_PROJECTION_EVENTS_V1_DOCUMENT_BEGIN, 0u) ||
        !ppae_v1_bridge_node_list(
            &bridge, document->expr.elems[2], 1u) ||
        !ppae_v1_bridge_emit(
            &bridge, PPATOM_PROJECTION_EVENTS_V1_DOCUMENT_END, 0u) ||
        !ppatom_projection_events_v1_builder_finish(
            &builder, out_atoms, out_len,
            error_buf, error_buf_size)) {
        goto done;
    }
    ok = true;

done:
    ppatom_projection_events_v1_builder_free(&builder);
    if (!ok && error_buf && error_buf_size && error_buf[0] == '\0') {
        ppae_v1_set_error(error_buf, error_buf_size,
                          "failed to bridge semantic projection events");
    }
    return ok;
}

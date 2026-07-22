#include "parser_pack_native_api_v1.h"

#include "finite_horn_ground_term_v1.h"
#include "parser_pack_abi_stream_v1.h"
#include "parser_pack_gll_v1.h"
#include "parser_pack_glr_v1.h"

#include "symbol.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pthread_mutex_t ppnative_api_v1_mutex = PTHREAD_MUTEX_INITIALIZER;
static SymbolTable ppnative_api_v1_symbols;
static bool ppnative_api_v1_symbols_initialized = false;

static void ppnative_api_v1_set_error(char *buf,
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

void ppnative_api_v1_response_init(PPNativeApiV1Response *response) {
    if (!response)
        return;
    memset(response, 0, sizeof(*response));
}

void ppnative_api_v1_response_free(PPNativeApiV1Response *response) {
    if (!response)
        return;
    free(response->bytes);
    memset(response, 0, sizeof(*response));
}

static Atom *ppnative_api_v1_expr(Arena *arena,
                                  const char *head,
                                  Atom **arguments,
                                  uint32_t argument_len) {
    Atom **elements;
    Atom *result;
    uint32_t index;

    elements = malloc(sizeof(*elements) * (argument_len + 1u));
    if (!elements)
        return NULL;
    elements[0] = atom_symbol(arena, head);
    for (index = 0u; index < argument_len; index++)
        elements[index + 1u] = arguments[index];
    result = atom_expr(arena, elements, argument_len + 1u);
    free(elements);
    return result;
}

static Atom *ppnative_api_v1_field(Arena *arena,
                                   const char *name,
                                   Atom *value) {
    return ppnative_api_v1_expr(arena, name, &value, 1u);
}

static Atom *ppnative_api_v1_list(Arena *arena,
                                  Atom *const *items,
                                  uint32_t len) {
    Atom *list = atom_symbol(arena, "nil");

    while (len > 0u) {
        Atom *arguments[2];
        len--;
        arguments[0] = items[len];
        arguments[1] = list;
        list = ppnative_api_v1_expr(arena, "cons", arguments, 2u);
        if (!list)
            return NULL;
    }
    return list;
}

static const char *ppnative_api_v1_backend_name(
    PPNativeApiV1Backend backend) {
    return backend == PPNATIVE_API_V1_GLL ? "gll" : "glr";
}

static const char *ppnative_api_v1_outcome_name(PPNativeV1Outcome outcome) {
    switch (outcome) {
    case PPNATIVE_V1_COMPLETED:
        return "completed";
    case PPNATIVE_V1_UNSUPPORTED_OPEN_PACK:
        return "unsupported-open-pack";
    case PPNATIVE_V1_RECOGNIZER_LIMIT:
        return "recognizer-limit";
    case PPNATIVE_V1_REPLAY_DEPTH:
        return "replay-depth";
    case PPNATIVE_V1_RESULT_LIMIT:
        return "result-limit";
    }
    return "unknown";
}

static Atom *ppnative_api_v1_u32_list(Arena *arena,
                                      const uint32_t *values,
                                      uint32_t len) {
    Atom **items = NULL;
    Atom *result;
    uint32_t index;

    if (len > 0u) {
        items = malloc(sizeof(*items) * len);
        if (!items)
            return NULL;
    }
    for (index = 0u; index < len; index++)
        items[index] = atom_int(arena, values[index]);
    result = ppnative_api_v1_list(arena, items, len);
    free(items);
    return result;
}

static Atom *ppnative_api_v1_expected_list(
    Arena *arena,
    const PPABIV1Pack *pack,
    const CettaLpNativeUtf8Forest *forest) {
    Atom **items = NULL;
    Atom *result = NULL;
    uint32_t index;

    if (forest->expected_terminal_len > 0u) {
        items = malloc(sizeof(*items) * forest->expected_terminal_len);
        if (!items)
            return NULL;
    }
    for (index = 0u; index < forest->expected_terminal_len; index++) {
        uint32_t terminal_id = forest->expected_terminal_ids[index];
        if (terminal_id >= pack->terminal_len)
            goto done;
        items[index] = pack->terminals[terminal_id].identity;
    }
    result = ppnative_api_v1_list(
        arena, items, forest->expected_terminal_len);

done:
    free(items);
    return result;
}

static Atom *ppnative_api_v1_response_atom(
    PPNativeApiV1Backend backend,
    const PPABIV1Pack *pack,
    PPNativeV1Result *result,
    char *error_buf,
    size_t error_buf_size) {
    Atom *fields[24];
    uint32_t field_len = 0u;
    Arena *arena = &result->arena;

#define ADD_FIELD(name, value)                                                \
    do {                                                                      \
        Atom *field_value_ = (value);                                         \
        Atom *field_ = field_value_                                           \
            ? ppnative_api_v1_field(arena, (name), field_value_)              \
            : NULL;                                                           \
        if (!field_ || field_len >= (uint32_t)(sizeof(fields) / sizeof(*fields))) \
            goto fail;                                                        \
        fields[field_len++] = field_;                                         \
    } while (0)

    ADD_FIELD("backend", atom_symbol(
        arena, ppnative_api_v1_backend_name(backend)));
    ADD_FIELD("outcome", atom_symbol(
        arena, ppnative_api_v1_outcome_name(result->outcome)));
    ADD_FIELD("detail", atom_string(arena, result->detail));
    ADD_FIELD("source-digest", atom_string(arena, pack->source_digest));
    ADD_FIELD("compiler-digest", atom_string(arena, pack->compiler_digest));
    ADD_FIELD("environment-digest", atom_string(
        arena, pack->environment_digest));
    ADD_FIELD("pack-digest", atom_string(arena, pack->pack_digest));

    if (result->outcome == PPNATIVE_V1_COMPLETED) {
        Atom *semantic_results = ppnative_api_v1_list(
            arena, result->semantic_results, result->semantic_result_len);
        Atom *expected = ppnative_api_v1_expected_list(
            arena, pack, &result->forest);
        Atom *byte_offsets = ppnative_api_v1_u32_list(
            arena, result->forest.byte_offsets,
            result->forest.scalar_len + 1u);

        ADD_FIELD("decision", atom_symbol(
            arena, result->accepted ? "accepted" : "rejected"));
        ADD_FIELD("forest-digest", atom_string(
            arena, result->forest_digest));
        ADD_FIELD("forest", result->canonical_forest);
        ADD_FIELD("results", semantic_results);
        ADD_FIELD("expected", expected);
        ADD_FIELD("byte-offsets", byte_offsets);
        ADD_FIELD("input-bytes", atom_int(
            arena, result->forest.input_byte_len));
        ADD_FIELD("input-scalars", atom_int(
            arena, result->forest.scalar_len));
        ADD_FIELD("decoded-bytes", atom_int(
            arena, result->forest.decoded_byte_len));
        ADD_FIELD("source-passes", atom_int(
            arena, result->forest.source_pass_count));
        ADD_FIELD("farthest-scalar", atom_int(
            arena, result->forest.farthest_scalar));
        ADD_FIELD("farthest-byte", atom_int(
            arena, result->forest.farthest_byte));
        ADD_FIELD("work-items", atom_int(
            arena, result->forest.work_item_len));
        ADD_FIELD("graph-nodes", atom_int(
            arena, result->forest.graph_node_len));
    }

#undef ADD_FIELD

    return ppnative_api_v1_expr(
        arena, "pp-native-response-v1", fields, field_len);

fail:
#undef ADD_FIELD
    ppnative_api_v1_set_error(error_buf, error_buf_size,
                              "failed to construct native parser response");
    return NULL;
}

static bool ppnative_api_v1_parse_locked(
    PPNativeApiV1Backend backend,
    const char *abi_path,
    const uint8_t *start_text,
    size_t start_text_len,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    uint32_t work_limit,
    uint32_t replay_depth,
    uint32_t result_limit,
    PPNativeApiV1Response *out,
    char *error_buf,
    size_t error_buf_size) {
    PPABIV1Wire wire;
    PPABIV1Pack pack;
    PPNativeV1Result result;
    Arena start_arena;
    Atom *start = NULL;
    Atom *response_atom;
    uint8_t *response_bytes = NULL;
    size_t response_len = 0u;
    bool ok = false;

    ppabi_v1_wire_init(&wire);
    ppabi_v1_pack_init(&pack);
    ppnative_v1_result_init(&result);
    arena_init(&start_arena);
    if (!ppabi_v1_wire_read(
            &wire, abi_path, error_buf, error_buf_size) ||
        !ppabi_v1_wire_load_pack(
            &wire, &pack, error_buf, error_buf_size) ||
        !fh_ground_term_v1_parse(
            &start_arena, start_text, start_text_len, &start,
            error_buf, error_buf_size)) {
        goto done;
    }
    if (backend == PPNATIVE_API_V1_GLL) {
        if (!ppgll_v1_parse(
                &pack, start, input_bytes, input_byte_len, work_limit,
                replay_depth, result_limit, &result,
                error_buf, error_buf_size)) {
            goto done;
        }
    } else if (backend == PPNATIVE_API_V1_GLR) {
        if (!ppglr_v1_parse(
                &pack, start, input_bytes, input_byte_len, work_limit,
                replay_depth, result_limit, &result,
                error_buf, error_buf_size)) {
            goto done;
        }
    } else {
        ppnative_api_v1_set_error(error_buf, error_buf_size,
                                  "unknown native parser backend");
        goto done;
    }
    response_atom = ppnative_api_v1_response_atom(
        backend, &pack, &result, error_buf, error_buf_size);
    if (!response_atom ||
        !fh_ground_term_v1_render(
            response_atom, &response_bytes, &response_len,
            error_buf, error_buf_size)) {
        goto done;
    }
    ppnative_api_v1_response_free(out);
    out->bytes = response_bytes;
    out->len = response_len;
    response_bytes = NULL;
    ok = true;

done:
    free(response_bytes);
    arena_free(&start_arena);
    ppnative_v1_result_free(&result);
    ppabi_v1_pack_free(&pack);
    ppabi_v1_wire_free(&wire);
    return ok;
}

bool ppnative_api_v1_with_runtime(
    PPNativeApiV1RuntimeCall call,
    void *context,
    char *error_buf,
    size_t error_buf_size) {
    SymbolTable *previous_symbols;
    HashConsTable *previous_hashcons;
    VarInternTable *previous_var_intern;
    bool ok = false;
    int unlock_status;

    if (!call) {
        ppnative_api_v1_set_error(error_buf, error_buf_size,
                                  "bad native parser runtime callback");
        return false;
    }
    if (pthread_mutex_lock(&ppnative_api_v1_mutex) != 0) {
        ppnative_api_v1_set_error(error_buf, error_buf_size,
                                  "failed to lock native parser API");
        return false;
    }
    previous_symbols = g_symbols;
    previous_hashcons = g_hashcons;
    previous_var_intern = g_var_intern;
    if (!g_symbols) {
        if (!ppnative_api_v1_symbols_initialized) {
            symbol_table_init(&ppnative_api_v1_symbols);
            symbol_table_init_builtins(
                &ppnative_api_v1_symbols, &g_builtin_syms);
            ppnative_api_v1_symbols_initialized = true;
        }
        g_symbols = &ppnative_api_v1_symbols;
        g_hashcons = NULL;
        g_var_intern = NULL;
    }
    ok = call(context);
    g_symbols = previous_symbols;
    g_hashcons = previous_hashcons;
    g_var_intern = previous_var_intern;
    unlock_status = pthread_mutex_unlock(&ppnative_api_v1_mutex);
    if (unlock_status != 0) {
        ppnative_api_v1_set_error(error_buf, error_buf_size,
                                  "failed to unlock native parser API");
        ok = false;
    }
    return ok;
}

typedef struct {
    PPNativeApiV1Backend backend;
    const char *abi_path;
    const uint8_t *start_text;
    size_t start_text_len;
    const uint8_t *input_bytes;
    size_t input_byte_len;
    uint32_t work_limit;
    uint32_t replay_depth;
    uint32_t result_limit;
    PPNativeApiV1Response *out;
    char *error_buf;
    size_t error_buf_size;
} PPNativeApiV1ParseCall;

static bool ppnative_api_v1_parse_call(void *raw_context) {
    PPNativeApiV1ParseCall *context = raw_context;

    return ppnative_api_v1_parse_locked(
        context->backend, context->abi_path,
        context->start_text, context->start_text_len,
        context->input_bytes, context->input_byte_len,
        context->work_limit, context->replay_depth, context->result_limit,
        context->out, context->error_buf, context->error_buf_size);
}

bool ppnative_api_v1_parse(
    PPNativeApiV1Backend backend,
    const char *abi_path,
    const uint8_t *start_text,
    size_t start_text_len,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    uint32_t work_limit,
    uint32_t replay_depth,
    uint32_t result_limit,
    PPNativeApiV1Response *out,
    char *error_buf,
    size_t error_buf_size) {
    PPNativeApiV1ParseCall context;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!abi_path || !start_text || !out || work_limit == 0u ||
        replay_depth == 0u || result_limit == 0u ||
        input_byte_len > UINT32_MAX ||
        (input_byte_len > 0u && !input_bytes)) {
        ppnative_api_v1_set_error(error_buf, error_buf_size,
                                  "bad native parser API arguments");
        return false;
    }
    context = (PPNativeApiV1ParseCall){
        .backend = backend,
        .abi_path = abi_path,
        .start_text = start_text,
        .start_text_len = start_text_len,
        .input_bytes = input_bytes,
        .input_byte_len = input_byte_len,
        .work_limit = work_limit,
        .replay_depth = replay_depth,
        .result_limit = result_limit,
        .out = out,
        .error_buf = error_buf,
        .error_buf_size = error_buf_size,
    };
    return ppnative_api_v1_with_runtime(
        ppnative_api_v1_parse_call, &context, error_buf, error_buf_size);
}

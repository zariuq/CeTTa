#include "he_compiled_reader.h"

#include "generated/he_reader_direct_v1.generated.h"
#include "gslt_direct_reader_v1.h"
#include "parser.h"

#include "symbol.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct HECompiledReaderV1 {
    bool ready;
    SymbolTable *owner_symbols;
    uint64_t owner_symbols_instance_id;
};

static void he_compiled_reader_v1_set_error(char *buf, size_t size,
                                            const char *format, ...) {
    va_list arguments;
    if (!buf || size == 0u)
        return;
    va_start(arguments, format);
    (void)vsnprintf(buf, size, format, arguments);
    va_end(arguments);
}

static bool he_compiled_reader_v1_begin_form(void *context) {
    return parser_host_projection_v1_begin_form(context);
}

static AtomId he_compiled_reader_v1_word_bytes(
    void *context, const uint8_t *bytes, size_t byte_len) {
    return parser_host_projection_v1_word_bytes(
        context, bytes, byte_len);
}

static AtomId he_compiled_reader_v1_variable_bytes(
    void *context, const uint8_t *bytes, size_t byte_len) {
    return parser_host_projection_v1_variable_bytes(
        context, bytes, byte_len);
}

static AtomId he_compiled_reader_v1_string_bytes(
    void *context, const uint8_t *bytes, size_t byte_len) {
    return parser_host_projection_v1_string_bytes(
        context, bytes, byte_len);
}

static AtomId he_compiled_reader_v1_expression(
    void *context, const AtomId *children, size_t child_len) {
    return parser_host_projection_v1_expression(
        context, children, child_len);
}

HECompiledReaderV1 *he_compiled_reader_v1_new(void) {
    return calloc(1u, sizeof(HECompiledReaderV1));
}

void he_compiled_reader_v1_free(HECompiledReaderV1 *reader) {
    free(reader);
}

bool he_compiled_reader_v1_prepare(HECompiledReaderV1 *reader,
                                   char *error_buf,
                                   size_t error_buf_size) {
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!reader || !g_symbols) {
        he_compiled_reader_v1_set_error(
            error_buf, error_buf_size,
            "compiled HE reader requires an initialized symbol table");
        return false;
    }
    if (reader->ready) {
        he_compiled_reader_v1_set_error(
            error_buf, error_buf_size,
            "compiled HE reader is already prepared");
        return false;
    }
    if (!gslt_direct_reader_v1_plan_validate(
            &he_reader_direct_v1_plan, error_buf, error_buf_size) ||
        strcmp(he_reader_direct_v1_plan.composition_digest,
               he_reader_direct_v1_program_digest()) != 0) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            he_compiled_reader_v1_set_error(
                error_buf, error_buf_size,
                "generated HE reader digest validation failed");
        }
        return false;
    }
    if (strcmp(he_reader_direct_v1_plan.profile, "cetta-he-v1") != 0) {
        he_compiled_reader_v1_set_error(
            error_buf, error_buf_size,
            "compiled HE host projection has the wrong profile");
        return false;
    }
    reader->owner_symbols = g_symbols;
    reader->owner_symbols_instance_id = symbol_table_instance_id(g_symbols);
    reader->ready = true;
    return true;
}

int he_compiled_reader_v1_parse_bytes_ids(
    HECompiledReaderV1 *reader, const uint8_t *input, size_t input_len,
    TermUniverse *universe, AtomId **out_ids,
    HECompiledReaderV1Receipt *receipt, char *error_buf,
    size_t error_buf_size) {
    GSLTDirectReaderV1Receipt direct_receipt;
    GSLTDirectAtomProjectionV1 projection;
    ParserHostProjectionV1 *projection_context = NULL;
    int result;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (out_ids)
        *out_ids = NULL;
    if (receipt)
        memset(receipt, 0, sizeof(*receipt));
    if (!reader || !reader->ready || reader->owner_symbols != g_symbols ||
        reader->owner_symbols_instance_id !=
            symbol_table_instance_id(g_symbols) ||
        !universe || !out_ids || input_len > UINT32_MAX ||
        (input_len > 0u && !input)) {
        he_compiled_reader_v1_set_error(error_buf, error_buf_size,
                                        "invalid compiled HE parse");
        return -1;
    }

    projection_context = parser_host_projection_v1_new(universe);
    if (!projection_context) {
        he_compiled_reader_v1_set_error(
            error_buf, error_buf_size,
            "failed to initialize the HE Atom projection");
        return -1;
    }
    projection = (GSLTDirectAtomProjectionV1){
        .context = projection_context,
        .begin_form = he_compiled_reader_v1_begin_form,
        .word_bytes = he_compiled_reader_v1_word_bytes,
        .variable_bytes = he_compiled_reader_v1_variable_bytes,
        .string_bytes = he_compiled_reader_v1_string_bytes,
        .expression = he_compiled_reader_v1_expression,
    };
    memset(&direct_receipt, 0, sizeof(direct_receipt));
    result = he_reader_direct_v1_parse_bytes_ids(
        input, input_len, &projection, out_ids, &direct_receipt,
        error_buf, error_buf_size);
    parser_host_projection_v1_free(projection_context);
    if (result < 0)
        return -1;
    if (receipt) {
        (void)snprintf(receipt->program_digest,
                       sizeof(receipt->program_digest), "%s",
                       he_reader_direct_v1_program_digest());
        (void)snprintf(receipt->fragment, sizeof(receipt->fragment), "%s",
                       he_reader_direct_v1_plan.fragment);
        (void)snprintf(receipt->profile, sizeof(receipt->profile), "%s",
                       he_reader_direct_v1_plan.profile);
        (void)snprintf(receipt->syntax_digest,
                       sizeof(receipt->syntax_digest), "%s",
                       he_reader_direct_v1_plan.syntax_digest);
        (void)snprintf(receipt->scalar_class_digest,
                       sizeof(receipt->scalar_class_digest), "%s",
                       he_reader_direct_v1_plan.class_digest);
        (void)snprintf(receipt->projection_digest,
                       sizeof(receipt->projection_digest), "%s",
                       he_reader_direct_v1_plan.projection_digest);
        (void)snprintf(receipt->compiler_digest,
                       sizeof(receipt->compiler_digest), "%s",
                       he_reader_direct_v1_plan.compiler_digest);
        receipt->source_pass_count = direct_receipt.source_pass_count;
        receipt->input_byte_len = direct_receipt.input_byte_len;
        receipt->token_len = direct_receipt.token_len;
        receipt->shift_len = direct_receipt.shift_len;
        receipt->reduce_len = direct_receipt.reduce_len;
    }
    return result;
}

int he_compiled_reader_v1_parse_text_ids(
    HECompiledReaderV1 *reader, const char *text, TermUniverse *universe,
    AtomId **out_ids, HECompiledReaderV1Receipt *receipt,
    char *error_buf, size_t error_buf_size) {
    if (!text) {
        if (out_ids)
            *out_ids = NULL;
        he_compiled_reader_v1_set_error(error_buf, error_buf_size,
                                        "compiled HE text is absent");
        return -1;
    }
    return he_compiled_reader_v1_parse_bytes_ids(
        reader, (const uint8_t *)text, strlen(text), universe, out_ids,
        receipt, error_buf, error_buf_size);
}

static bool he_compiled_reader_v1_read_file(const char *filename,
                                            uint8_t **bytes_out,
                                            size_t *len_out) {
    FILE *file = NULL;
    uint8_t *bytes = NULL;
    size_t len = 0u;
    size_t cap = 0u;
    bool ok = false;

    if (!filename || !bytes_out || !len_out)
        return false;
    *bytes_out = NULL;
    *len_out = 0u;
    file = fopen(filename, "rb");
    if (!file)
        goto done;
    for (;;) {
        size_t amount;
        if (len == cap) {
            size_t next_cap = cap ? cap * 2u : 4096u;
            uint8_t *next;
            if (next_cap < cap)
                goto done;
            next = realloc(bytes, next_cap);
            if (!next)
                goto done;
            bytes = next;
            cap = next_cap;
        }
        amount = fread(bytes + len, 1u, cap - len, file);
        len += amount;
        if (amount > 0u)
            continue;
        if (ferror(file))
            goto done;
        break;
    }
    *bytes_out = bytes;
    *len_out = len;
    bytes = NULL;
    ok = true;

done:
    if (file)
        (void)fclose(file);
    free(bytes);
    return ok;
}

int he_compiled_reader_v1_parse_file_ids(
    HECompiledReaderV1 *reader, const char *filename,
    TermUniverse *universe, AtomId **out_ids,
    HECompiledReaderV1Receipt *receipt, char *error_buf,
    size_t error_buf_size) {
    uint8_t *bytes = NULL;
    size_t len = 0u;
    int result;

    if (!out_ids)
        return -1;
    *out_ids = NULL;
    if (!he_compiled_reader_v1_read_file(filename, &bytes, &len)) {
        he_compiled_reader_v1_set_error(error_buf, error_buf_size,
                                        "cannot read HE source file");
        return -1;
    }
    result = he_compiled_reader_v1_parse_bytes_ids(
        reader, bytes, len, universe, out_ids, receipt,
        error_buf, error_buf_size);
    free(bytes);
    return result;
}

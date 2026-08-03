#include "petta_compiled_reader.h"

#include "generated/petta_reader_direct_v1.generated.h"
#include "gslt_direct_reader_v1.h"
#include "symbol.h"

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    SymbolId spelling;
    VarId variable;
} PeTTaVariableEntryV1;

typedef struct {
    TermUniverse *universe;
    PeTTaVariableEntryV1 *variables;
    uint32_t variable_len;
    uint32_t variable_cap;
    bool form_active;
} PeTTaProjectionContextV1;

struct PeTTaCompiledReaderV1 {
    bool ready;
    SymbolTable *owner_symbols;
    uint64_t owner_symbols_instance_id;
};

static void petta_compiled_reader_v1_set_error(
    char *buf, size_t size, const char *format, ...) {
    va_list arguments;
    if (!buf || size == 0u)
        return;
    va_start(arguments, format);
    (void)vsnprintf(buf, size, format, arguments);
    va_end(arguments);
}

static bool petta_projection_begin_form(void *raw) {
    PeTTaProjectionContextV1 *context = raw;
    if (!context || !context->universe || !g_symbols)
        return false;
    context->variable_len = 0u;
    context->form_active = true;
    return true;
}

static bool petta_ascii_number_shape(
    const uint8_t *bytes, size_t len, bool *floating) {
    size_t index = 0u;
    bool has_fraction = false;
    bool has_exponent = false;
    if (!bytes || len == 0u || !floating)
        return false;
    if (bytes[index] == (uint8_t)'+' || bytes[index] == (uint8_t)'-') {
        index++;
        if (index == len)
            return false;
    }
    if (bytes[index] < (uint8_t)'0' || bytes[index] > (uint8_t)'9')
        return false;
    while (index < len && bytes[index] >= (uint8_t)'0' &&
           bytes[index] <= (uint8_t)'9')
        index++;
    if (index < len && bytes[index] == (uint8_t)'.') {
        has_fraction = true;
        index++;
        if (index == len || bytes[index] < (uint8_t)'0' ||
            bytes[index] > (uint8_t)'9')
            return false;
        while (index < len && bytes[index] >= (uint8_t)'0' &&
               bytes[index] <= (uint8_t)'9')
            index++;
    }
    if (index < len &&
        (bytes[index] == (uint8_t)'e' || bytes[index] == (uint8_t)'E')) {
        has_exponent = true;
        index++;
        if (index < len &&
            (bytes[index] == (uint8_t)'+' || bytes[index] == (uint8_t)'-'))
            index++;
        if (index == len || bytes[index] < (uint8_t)'0' ||
            bytes[index] > (uint8_t)'9')
            return false;
        while (index < len && bytes[index] >= (uint8_t)'0' &&
               bytes[index] <= (uint8_t)'9')
            index++;
    }
    *floating = has_fraction || has_exponent;
    return index == len;
}

static char *petta_projection_cstr(
    const uint8_t *bytes, size_t len) {
    char *text;
    if ((len > 0u && (!bytes || memchr(bytes, '\0', len))))
        return NULL;
    text = malloc(len + 1u);
    if (!text)
        return NULL;
    if (len > 0u)
        memcpy(text, bytes, len);
    text[len] = '\0';
    return text;
}

static AtomId petta_projection_token_bytes(
    void *raw, const uint8_t *bytes, size_t len) {
    PeTTaProjectionContextV1 *context = raw;
    bool floating = false;
    if (!context || !context->form_active || !context->universe ||
        len == 0u || (len > 0u && !bytes))
        return CETTA_ATOM_ID_NONE;
    if (len == 4u && memcmp(bytes, "True", 4u) == 0) {
        return tu_intern_symbol(
            context->universe,
            symbol_intern_cstr(g_symbols, "true"));
    }
    if (len == 5u && memcmp(bytes, "False", 5u) == 0) {
        return tu_intern_symbol(
            context->universe,
            symbol_intern_cstr(g_symbols, "false"));
    }
    if (!memchr(bytes, '\0', len) &&
        petta_ascii_number_shape(bytes, len, &floating)) {
        char *text = petta_projection_cstr(bytes, len);
        AtomId number = CETTA_ATOM_ID_NONE;
        if (!text)
            return CETTA_ATOM_ID_NONE;
        if (floating) {
            char *end = NULL;
            double value;
            errno = 0;
            value = strtod(text, &end);
            if (end == text + len && errno == 0)
                number = tu_intern_float(context->universe, value);
        } else {
            number = tu_intern_bigint(context->universe, text);
        }
        free(text);
        if (number != CETTA_ATOM_ID_NONE)
            return number;
    }
    if (len > UINT32_MAX)
        return CETTA_ATOM_ID_NONE;
    return tu_intern_symbol(
        context->universe,
        symbol_intern_bytes(g_symbols, bytes, (uint32_t)len));
}

static AtomId petta_projection_variable_bytes(
    void *raw, const uint8_t *bytes, size_t len, bool anonymous) {
    PeTTaProjectionContextV1 *context = raw;
    SymbolId spelling;
    VarId variable;
    if (!context || !context->form_active || !context->universe ||
        len == 0u || len > UINT32_MAX || !bytes)
        return CETTA_ATOM_ID_NONE;
    spelling = symbol_intern_bytes(g_symbols, bytes, (uint32_t)len);
    if (anonymous) {
        variable = fresh_var_id();
    } else {
        for (uint32_t index = 0u; index < context->variable_len; index++) {
            if (context->variables[index].spelling == spelling) {
                return tu_intern_var(
                    context->universe, spelling,
                    context->variables[index].variable);
            }
        }
        variable = fresh_var_id();
        if (context->variable_len == context->variable_cap) {
            uint32_t next = context->variable_cap
                                ? context->variable_cap * 2u
                                : 16u;
            if (next < context->variable_cap)
                return CETTA_ATOM_ID_NONE;
            context->variables = cetta_realloc(
                context->variables,
                (size_t)next * sizeof(*context->variables));
            context->variable_cap = next;
        }
        context->variables[context->variable_len++] =
            (PeTTaVariableEntryV1){spelling, variable};
    }
    return tu_intern_var(context->universe, spelling, variable);
}

static AtomId petta_projection_string_bytes(
    void *raw, const uint8_t *bytes, size_t len) {
    PeTTaProjectionContextV1 *context = raw;
    char *text;
    AtomId result;
    if (!context || !context->form_active || !context->universe)
        return CETTA_ATOM_ID_NONE;
    text = petta_projection_cstr(bytes, len);
    if (!text)
        return CETTA_ATOM_ID_NONE;
    result = tu_intern_string(context->universe, text);
    free(text);
    return result;
}

static AtomId petta_projection_dollar_symbol(void *raw) {
    static const uint8_t dollar[] = {'$'};
    return petta_projection_token_bytes(raw, dollar, sizeof(dollar));
}

static AtomId petta_projection_expression(
    void *raw, const AtomId *children, size_t len) {
    PeTTaProjectionContextV1 *context = raw;
    if (!context || !context->form_active || !context->universe ||
        len > UINT16_MAX || (len > 0u && !children))
        return CETTA_ATOM_ID_NONE;
    return tu_expr_from_ids(
        context->universe, children, (CettaExprLen)len);
}

static bool petta_projection_finish_form(
    void *raw, bool runnable, AtomId value, AtomId output[2],
    uint32_t *output_len) {
    PeTTaProjectionContextV1 *context = raw;
    if (!context || !context->form_active || !context->universe ||
        value == CETTA_ATOM_ID_NONE || !output || !output_len)
        return false;
    if (runnable) {
        output[0] = tu_intern_symbol(
            context->universe,
            symbol_intern_cstr(g_symbols, "!"));
        output[1] = value;
        *output_len = 2u;
    } else {
        output[0] = value;
        output[1] = CETTA_ATOM_ID_NONE;
        *output_len = 1u;
    }
    context->form_active = false;
    return output[0] != CETTA_ATOM_ID_NONE;
}

PeTTaCompiledReaderV1 *petta_compiled_reader_v1_new(void) {
    return calloc(1u, sizeof(PeTTaCompiledReaderV1));
}

void petta_compiled_reader_v1_free(PeTTaCompiledReaderV1 *reader) {
    free(reader);
}

bool petta_compiled_reader_v1_prepare(
    PeTTaCompiledReaderV1 *reader, char *error_buf,
    size_t error_buf_size) {
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!reader || !g_symbols) {
        petta_compiled_reader_v1_set_error(
            error_buf, error_buf_size,
            "compiled PeTTa reader requires an initialized symbol table");
        return false;
    }
    if (reader->ready) {
        petta_compiled_reader_v1_set_error(
            error_buf, error_buf_size,
            "compiled PeTTa reader is already prepared");
        return false;
    }
    if (!gslt_direct_petta_reader_v1_plan_validate(
            &petta_reader_direct_v1_plan, error_buf, error_buf_size) ||
        strcmp(petta_reader_direct_v1_plan.composition_digest,
               petta_reader_direct_v1_program_digest()) != 0) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0')
            petta_compiled_reader_v1_set_error(
                error_buf, error_buf_size,
                "generated PeTTa reader digest validation failed");
        return false;
    }
    if (strcmp(petta_reader_direct_v1_plan.profile,
               "cetta-petta-v1") != 0) {
        petta_compiled_reader_v1_set_error(
            error_buf, error_buf_size,
            "compiled PeTTa host projection has the wrong profile");
        return false;
    }
    reader->owner_symbols = g_symbols;
    reader->owner_symbols_instance_id = symbol_table_instance_id(g_symbols);
    reader->ready = true;
    return true;
}

int petta_compiled_reader_v1_parse_bytes_ids(
    PeTTaCompiledReaderV1 *reader, const uint8_t *input,
    size_t input_len, TermUniverse *universe, AtomId **out_ids,
    PeTTaCompiledReaderV1Receipt *receipt, char *error_buf,
    size_t error_buf_size) {
    PeTTaProjectionContextV1 context = {0};
    GSLTDirectPeTTaProjectionV1 projection;
    GSLTDirectPeTTaReaderV1Receipt direct_receipt;
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
        petta_compiled_reader_v1_set_error(
            error_buf, error_buf_size, "invalid compiled PeTTa parse");
        return -1;
    }
    context.universe = universe;
    projection = (GSLTDirectPeTTaProjectionV1){
        .context = &context,
        .begin_form = petta_projection_begin_form,
        .token_bytes = petta_projection_token_bytes,
        .variable_bytes = petta_projection_variable_bytes,
        .string_bytes = petta_projection_string_bytes,
        .dollar_symbol = petta_projection_dollar_symbol,
        .expression = petta_projection_expression,
        .finish_form = petta_projection_finish_form,
    };
    memset(&direct_receipt, 0, sizeof(direct_receipt));
    result = petta_reader_direct_v1_parse_bytes_ids(
        input, input_len, &projection, out_ids, &direct_receipt,
        error_buf, error_buf_size);
    free(context.variables);
    if (result < 0)
        return -1;
    if (receipt) {
#define PETTA_RECEIPT_TEXT(field, value) \
        (void)snprintf(receipt->field, sizeof(receipt->field), "%s", value)
        PETTA_RECEIPT_TEXT(program_digest,
                           petta_reader_direct_v1_program_digest());
        PETTA_RECEIPT_TEXT(fragment, petta_reader_direct_v1_plan.fragment);
        PETTA_RECEIPT_TEXT(profile, petta_reader_direct_v1_plan.profile);
        PETTA_RECEIPT_TEXT(splitter_syntax_digest,
                           petta_reader_direct_v1_plan.splitter_syntax_digest);
        PETTA_RECEIPT_TEXT(splitter_class_digest,
                           petta_reader_direct_v1_plan.splitter_class_digest);
        PETTA_RECEIPT_TEXT(form_syntax_digest,
                           petta_reader_direct_v1_plan.form_syntax_digest);
        PETTA_RECEIPT_TEXT(form_class_digest,
                           petta_reader_direct_v1_plan.form_class_digest);
        PETTA_RECEIPT_TEXT(projection_digest,
                           petta_reader_direct_v1_plan.projection_digest);
        PETTA_RECEIPT_TEXT(compiler_digest,
                           petta_reader_direct_v1_plan.compiler_digest);
#undef PETTA_RECEIPT_TEXT
        receipt->source_pass_count = direct_receipt.source_pass_count;
        receipt->form_replay_count = direct_receipt.form_replay_count;
        receipt->input_byte_len = direct_receipt.input_byte_len;
        receipt->form_len = direct_receipt.form_len;
        receipt->token_len = direct_receipt.token_len;
        receipt->shift_len = direct_receipt.shift_len;
        receipt->reduce_len = direct_receipt.reduce_len;
    }
    return result;
}

int petta_compiled_reader_v1_parse_text_ids(
    PeTTaCompiledReaderV1 *reader, const char *text,
    TermUniverse *universe, AtomId **out_ids,
    PeTTaCompiledReaderV1Receipt *receipt, char *error_buf,
    size_t error_buf_size) {
    if (!text) {
        if (out_ids)
            *out_ids = NULL;
        petta_compiled_reader_v1_set_error(
            error_buf, error_buf_size, "compiled PeTTa text is absent");
        return -1;
    }
    return petta_compiled_reader_v1_parse_bytes_ids(
        reader, (const uint8_t *)text, strlen(text), universe,
        out_ids, receipt, error_buf, error_buf_size);
}

static bool petta_compiled_reader_v1_read_file(
    const char *filename, uint8_t **bytes_out, size_t *len_out) {
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

int petta_compiled_reader_v1_parse_file_ids(
    PeTTaCompiledReaderV1 *reader, const char *filename,
    TermUniverse *universe, AtomId **out_ids,
    PeTTaCompiledReaderV1Receipt *receipt, char *error_buf,
    size_t error_buf_size) {
    uint8_t *bytes = NULL;
    size_t len = 0u;
    int result;
    if (!out_ids)
        return -1;
    *out_ids = NULL;
    if (!petta_compiled_reader_v1_read_file(filename, &bytes, &len)) {
        petta_compiled_reader_v1_set_error(
            error_buf, error_buf_size, "cannot read PeTTa source file");
        return -1;
    }
    result = petta_compiled_reader_v1_parse_bytes_ids(
        reader, bytes, len, universe, out_ids, receipt,
        error_buf, error_buf_size);
    free(bytes);
    return result;
}

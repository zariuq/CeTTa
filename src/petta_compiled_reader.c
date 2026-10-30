#define _GNU_SOURCE
#include "petta_compiled_reader.h"

#include "generated/petta_extended_reader_direct_v1.generated.h"
#include "generated/petta_reader_direct_v1.generated.h"
#include "gslt_direct_reader_v1.h"
#include "petta_numeric.h"
#include "symbol.h"
#include "var_index.h"

#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    SymbolId spelling;
    VarId variable;
} PeTTaVariableEntryV1;

/* A token's id, kept while one parse runs: direct-mapped by its spelling,
 * a spelling longer than PETTA_TOKEN_BYTES is not kept.  Within a parse
 * the universe collects nothing, so a spelling names one id. */
enum { PETTA_TOKEN_SLOTS = 4096u, PETTA_TOKEN_BYTES = 23u };

typedef struct {
    AtomId id;
    uint8_t len;
    uint8_t bytes[PETTA_TOKEN_BYTES];
} PeTTaTokenSlotV1;

typedef struct {
    TermUniverse *universe;
    PeTTaVariableEntryV1 *variables;
    uint32_t variable_len;
    uint32_t variable_cap;
    CettaVarIndex variable_index;
    /* The tokens read so far, PETTA_TOKEN_SLOTS of them once one is read. */
    PeTTaTokenSlotV1 *tokens;
    bool form_active;
} PeTTaProjectionContextV1;

struct PeTTaCompiledReaderV1 {
    bool ready;
    /* The PeTTa reader, or the extended profiles' reader with lists. */
    const GSLTDirectPeTTaReaderV1Plan *plan;
    const char *(*program_digest)(void);
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

/* A token's id: a truth value, a number, or a symbol. */
static AtomId petta_projection_token_id(TermUniverse *universe,
                                        const uint8_t *bytes, size_t len) {
    if (len == 4u && memcmp(bytes, "True", 4u) == 0) {
        return tu_intern_symbol(
            universe,
            symbol_intern_cstr(g_symbols, "true"));
    }
    if (len == 5u && memcmp(bytes, "False", 5u) == 0) {
        return tu_intern_symbol(
            universe,
            symbol_intern_cstr(g_symbols, "false"));
    }
    AtomId number = cetta_petta_number_parse_id(
        universe, bytes, len,
        CETTA_PETTA_NUMBER_SOURCE_TOKEN);
    if (number != CETTA_ATOM_ID_NONE)
        return number;
    if (len > UINT32_MAX)
        return CETTA_ATOM_ID_NONE;
    return tu_intern_symbol(
        universe,
        symbol_intern_bytes(g_symbols, bytes, (uint32_t)len));
}

/* The slot a spelling of LEN bytes is kept in, NULL when it is too long to
 * keep or the slots cannot be had. */
static PeTTaTokenSlotV1 *petta_projection_token_slot(
    PeTTaProjectionContextV1 *context, const uint8_t *bytes, size_t len) {
    if (len > PETTA_TOKEN_BYTES)
        return NULL;
    if (!context->tokens) {
        context->tokens = calloc(PETTA_TOKEN_SLOTS, sizeof(*context->tokens));
        if (!context->tokens)
            return NULL;
    }
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    for (size_t at = 0u; at < len; at++)
        hash = (hash ^ bytes[at]) * UINT64_C(0x100000001b3);
    return &context->tokens[(hash ^ (hash >> 32)) & (PETTA_TOKEN_SLOTS - 1u)];
}

static AtomId petta_projection_token_bytes(
    void *raw, const uint8_t *bytes, size_t len) {
    PeTTaProjectionContextV1 *context = raw;
    if (!context || !context->form_active || !context->universe ||
        len == 0u || (len > 0u && !bytes))
        return CETTA_ATOM_ID_NONE;
    PeTTaTokenSlotV1 *slot = petta_projection_token_slot(context, bytes, len);
    if (slot && slot->len == len && memcmp(slot->bytes, bytes, len) == 0)
        return slot->id;
    AtomId id = petta_projection_token_id(context->universe, bytes, len);
    if (slot && id != CETTA_ATOM_ID_NONE) {
        slot->id = id;
        slot->len = (uint8_t)len;
        memcpy(slot->bytes, bytes, len);
    }
    return id;
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
        uint32_t found = UINT32_MAX;
        if (context->variable_len <= CETTA_VAR_INDEX_SCAN ||
            !context->variable_index.slots) {
            for (uint32_t index = 0u; index < context->variable_len; index++) {
                if (context->variables[index].spelling == spelling) {
                    found = index;
                    break;
                }
            }
        } else {
            uint32_t proposed = cetta_var_index_propose(
                &context->variable_index, var_id_from_base(spelling));
            if (proposed < context->variable_len &&
                context->variables[proposed].spelling == spelling)
                found = proposed;
        }
        if (found != UINT32_MAX)
            return tu_intern_var(context->universe, spelling,
                                 context->variables[found].variable);
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
        if (context->variable_len > CETTA_VAR_INDEX_SCAN) {
            if (context->variable_len == CETTA_VAR_INDEX_SCAN + 1u ||
                !cetta_var_index_has_room(&context->variable_index)) {
                if (!cetta_var_index_reset(&context->variable_index,
                                            context->variable_len))
                    return CETTA_ATOM_ID_NONE;
                for (uint32_t index = 0u; index < context->variable_len; index++)
                    cetta_var_index_record(&context->variable_index,
                        var_id_from_base(context->variables[index].spelling), index);
            } else {
                cetta_var_index_record(&context->variable_index,
                    var_id_from_base(spelling), context->variable_len - 1u);
            }
        }
    }
    return tu_intern_var(context->universe, spelling, variable);
}

/* A string keeps its bytes exactly, embedded NUL included. */
static AtomId petta_projection_string_bytes(
    void *raw, const uint8_t *bytes, size_t len) {
    PeTTaProjectionContextV1 *context = raw;
    if (!context || !context->form_active || !context->universe ||
        (len > 0u && !bytes))
        return CETTA_ATOM_ID_NONE;
    return tu_intern_string_n(context->universe,
                              len > 0u ? (const char *)bytes : "", len);
}

static AtomId petta_projection_dollar_symbol(void *raw) {
    static const uint8_t dollar[] = {'$'};
    return petta_projection_token_bytes(raw, dollar, sizeof(dollar));
}

static AtomId petta_projection_expression(
    void *raw, const AtomId *children, size_t len) {
    PeTTaProjectionContextV1 *context = raw;
    if (!context || !context->form_active || !context->universe ||
        (len > 0u && !children))
        return CETTA_ATOM_ID_NONE;
    /* The term universe checks its storage bound. The header's 16-bit
     * inline length is only a summary, not the expression arity limit. */
    return tu_expr_from_ids(
        context->universe, children, (CettaExprLen)len);
}

static AtomId petta_projection_list(
    void *raw, const AtomId *elems, size_t len, AtomId rest) {
    PeTTaProjectionContextV1 *context = raw;
    if (!context || !context->form_active || !context->universe ||
        (len > 0u && !elems))
        return CETTA_ATOM_ID_NONE;
    return tu_list_from_ids(context->universe, elems, (CettaExprLen)len, rest);
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
    PeTTaCompiledReaderV1 *reader = calloc(1u, sizeof(PeTTaCompiledReaderV1));
    if (reader) {
        reader->plan = &petta_reader_direct_v1_plan;
        reader->program_digest = petta_reader_direct_v1_program_digest;
    }
    return reader;
}

PeTTaCompiledReaderV1 *petta_compiled_reader_v1_new_with_lists(void) {
    PeTTaCompiledReaderV1 *reader = calloc(1u, sizeof(PeTTaCompiledReaderV1));
    if (reader) {
        reader->plan = &petta_extended_reader_direct_v1_plan;
        reader->program_digest = petta_extended_reader_direct_v1_program_digest;
    }
    return reader;
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
            reader->plan, error_buf, error_buf_size) ||
        strcmp(reader->plan->composition_digest,
               reader->program_digest()) != 0) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0')
            petta_compiled_reader_v1_set_error(
                error_buf, error_buf_size,
                "generated PeTTa reader digest validation failed");
        return false;
    }
    if (strcmp(reader->plan->profile,
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

/* The forms of INPUT from START, at most MAX_FORMS, with *END_OUT where
 * the next would start: the parse both the whole-input entry and a
 * document's slices take. */
static int petta_compiled_reader_v1_parse_from(
    PeTTaCompiledReaderV1 *reader, const uint8_t *input, size_t input_len,
    size_t start, uint32_t max_forms, TermUniverse *universe,
    AtomId **out_ids, GSLTDirectPeTTaReaderV1Receipt *direct_receipt,
    size_t *end_out, char *error_buf, size_t error_buf_size) {
    PeTTaProjectionContextV1 context = {0};
    GSLTDirectPeTTaProjectionV1 projection;
    int result;
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (out_ids)
        *out_ids = NULL;
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
        .list = petta_projection_list,
    };
    memset(direct_receipt, 0, sizeof(*direct_receipt));
    result = gslt_direct_petta_reader_v1_parse_bytes_ids_from(
        reader->plan, input, input_len, start, max_forms, &projection,
        out_ids, direct_receipt, end_out, error_buf, error_buf_size);
    cetta_var_index_free(&context.variable_index);
    free(context.variables);
    free(context.tokens);
    return result;
}

int petta_compiled_reader_v1_parse_bytes_ids(
    PeTTaCompiledReaderV1 *reader, const uint8_t *input,
    size_t input_len, TermUniverse *universe, AtomId **out_ids,
    PeTTaCompiledReaderV1Receipt *receipt, char *error_buf,
    size_t error_buf_size) {
    GSLTDirectPeTTaReaderV1Receipt direct_receipt;
    if (receipt)
        memset(receipt, 0, sizeof(*receipt));
    int result = petta_compiled_reader_v1_parse_from(
        reader, input, input_len, 0u, UINT32_MAX, universe, out_ids,
        &direct_receipt, NULL, error_buf, error_buf_size);
    if (result < 0)
        return -1;
    if (receipt) {
#define PETTA_RECEIPT_TEXT(field, value) \
        (void)snprintf(receipt->field, sizeof(receipt->field), "%s", value)
        PETTA_RECEIPT_TEXT(program_digest, reader->program_digest());
        PETTA_RECEIPT_TEXT(fragment, reader->plan->fragment);
        PETTA_RECEIPT_TEXT(profile, reader->plan->profile);
        PETTA_RECEIPT_TEXT(splitter_syntax_digest,
                           reader->plan->splitter_syntax_digest);
        PETTA_RECEIPT_TEXT(splitter_class_digest,
                           reader->plan->splitter_class_digest);
        PETTA_RECEIPT_TEXT(form_syntax_digest,
                           reader->plan->form_syntax_digest);
        PETTA_RECEIPT_TEXT(form_class_digest,
                           reader->plan->form_class_digest);
        PETTA_RECEIPT_TEXT(projection_digest,
                           reader->plan->projection_digest);
        PETTA_RECEIPT_TEXT(compiler_digest,
                           reader->plan->compiler_digest);
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

int petta_compiled_reader_v1_parse_term_bytes_id(
    PeTTaCompiledReaderV1 *reader, const uint8_t *input,
    size_t input_len, TermUniverse *universe, AtomId *out_id,
    char *error_buf, size_t error_buf_size) {
    PeTTaProjectionContextV1 context = {0};
    GSLTDirectPeTTaProjectionV1 projection;
    int result;
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (out_id)
        *out_id = CETTA_ATOM_ID_NONE;
    if (!reader || !reader->ready || reader->owner_symbols != g_symbols ||
        reader->owner_symbols_instance_id !=
            symbol_table_instance_id(g_symbols) ||
        !universe || !out_id || input_len > UINT32_MAX ||
        (input_len > 0u && !input)) {
        petta_compiled_reader_v1_set_error(
            error_buf, error_buf_size, "invalid compiled PeTTa term read");
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
        .list = petta_projection_list,
    };
    result = gslt_direct_petta_reader_v1_parse_term_bytes_id(
        reader->plan, input, input_len, &projection, out_id,
        error_buf, error_buf_size);
    cetta_var_index_free(&context.variable_index);
    free(context.variables);
    free(context.tokens);
    return result;
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

bool petta_compiled_document_v1_open(PeTTaCompiledDocumentV1 *document,
                                     const char *filename) {
    struct stat status;
    if (!document)
        return false;
    *document = (PeTTaCompiledDocumentV1){.fd = -1};
    if (!filename)
        return false;
    document->fd = open(filename, O_RDONLY);
    if (document->fd < 0 || fstat(document->fd, &status) != 0 ||
        status.st_size < 0) {
        petta_compiled_document_v1_close(document);
        return false;
    }
    document->len = (size_t)status.st_size;
    if (document->len == 0u)
        return true;
    void *map = mmap(NULL, document->len, PROT_READ, MAP_PRIVATE,
                     document->fd, 0);
    if (map == MAP_FAILED) {
        petta_compiled_document_v1_close(document);
        return false;
    }
    document->bytes = map;
    return true;
}

/* A document's first pass parses every form as the second pass does, so
 * it finds the same faults, but builds only a form whose first token
 * spells a head the predeclaration reads: `=`, `=%` or `:`
 * (petta_program_form_id_may_affect_metadata).  Such a form is built by the
 * parse's own projection; every other form builds nothing and stands as
 * the empty expression.  Whether a form is built is decided at its first
 * part, before any of it is built. */
typedef enum {
    PETTA_SCAN_UNDECIDED = 0,
    PETTA_SCAN_BUILD,
    PETTA_SCAN_SKIP,
} PeTTaScanModeV1;

typedef struct {
    PeTTaProjectionContextV1 built;
    PeTTaScanModeV1 mode;
    AtomId skipped;
    AtomId bang;
} PeTTaScanContextV1;

static bool petta_scan_spells(const uint8_t *bytes, size_t len,
                              SymbolId symbol) {
    const char *spelling = symbol_bytes(g_symbols, symbol);
    return spelling && strlen(spelling) == len &&
        memcmp(spelling, bytes, len) == 0;
}

/* Whether the form is built: decided at its first part, a token spelling
 * a head the predeclaration reads, or anything else (BYTES NULL). */
static bool petta_scan_builds(PeTTaScanContextV1 *scan, const uint8_t *bytes,
                              size_t len) {
    if (scan->mode == PETTA_SCAN_UNDECIDED)
        scan->mode = bytes &&
                (petta_scan_spells(bytes, len, g_builtin_syms.equals) ||
                 petta_scan_spells(bytes, len, g_builtin_syms.equals_percent) ||
                 petta_scan_spells(bytes, len, g_builtin_syms.colon))
            ? PETTA_SCAN_BUILD : PETTA_SCAN_SKIP;
    return scan->mode == PETTA_SCAN_BUILD;
}

static bool petta_scan_begin_form(void *raw) {
    PeTTaScanContextV1 *scan = raw;
    scan->mode = PETTA_SCAN_UNDECIDED;
    return petta_projection_begin_form(&scan->built);
}

static AtomId petta_scan_token_bytes(void *raw, const uint8_t *bytes,
                                     size_t len) {
    PeTTaScanContextV1 *scan = raw;
    return petta_scan_builds(scan, bytes, len)
        ? petta_projection_token_bytes(&scan->built, bytes, len)
        : scan->skipped;
}

static AtomId petta_scan_variable_bytes(void *raw, const uint8_t *bytes,
                                        size_t len, bool anonymous) {
    PeTTaScanContextV1 *scan = raw;
    return petta_scan_builds(scan, NULL, 0u)
        ? petta_projection_variable_bytes(&scan->built, bytes, len, anonymous)
        : scan->skipped;
}

static AtomId petta_scan_string_bytes(void *raw, const uint8_t *bytes,
                                      size_t len) {
    PeTTaScanContextV1 *scan = raw;
    return petta_scan_builds(scan, NULL, 0u)
        ? petta_projection_string_bytes(&scan->built, bytes, len)
        : scan->skipped;
}

static AtomId petta_scan_dollar_symbol(void *raw) {
    PeTTaScanContextV1 *scan = raw;
    return petta_scan_builds(scan, NULL, 0u)
        ? petta_projection_dollar_symbol(&scan->built)
        : scan->skipped;
}

static AtomId petta_scan_expression(void *raw, const AtomId *children,
                                    size_t len) {
    PeTTaScanContextV1 *scan = raw;
    return petta_scan_builds(scan, NULL, 0u)
        ? petta_projection_expression(&scan->built, children, len)
        : scan->skipped;
}

static AtomId petta_scan_list(void *raw, const AtomId *elems, size_t len,
                              AtomId rest) {
    PeTTaScanContextV1 *scan = raw;
    return petta_scan_builds(scan, NULL, 0u)
        ? petta_projection_list(&scan->built, elems, len, rest)
        : scan->skipped;
}

static bool petta_scan_finish_form(void *raw, bool runnable, AtomId value,
                                   AtomId output[2], uint32_t *output_len) {
    PeTTaScanContextV1 *scan = raw;
    if (scan->mode == PETTA_SCAN_BUILD)
        return petta_projection_finish_form(&scan->built, runnable, value,
                                            output, output_len);
    if (!scan->built.form_active || value == CETTA_ATOM_ID_NONE ||
        !output || !output_len)
        return false;
    output[0] = runnable ? scan->bang : scan->skipped;
    output[1] = runnable ? scan->skipped : CETTA_ATOM_ID_NONE;
    *output_len = runnable ? 2u : 1u;
    scan->built.form_active = false;
    return true;
}

int petta_compiled_document_v1_check_next(PeTTaCompiledReaderV1 *reader,
                                          PeTTaCompiledDocumentV1 *document,
                                          uint32_t max_forms,
                                          TermUniverse *universe,
                                          AtomId **out_ids,
                                          char *error_buf,
                                          size_t error_buf_size) {
    GSLTDirectPeTTaReaderV1Receipt direct_receipt;
    PeTTaScanContextV1 scan = {0};
    size_t end = 0u;
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (out_ids)
        *out_ids = NULL;
    if (!document || max_forms == 0u)
        return -1;
    if (document->pos >= document->len)
        return 0;
    if (!reader || !reader->ready || reader->owner_symbols != g_symbols ||
        reader->owner_symbols_instance_id !=
            symbol_table_instance_id(g_symbols) ||
        !universe || !out_ids || document->len > UINT32_MAX) {
        petta_compiled_reader_v1_set_error(
            error_buf, error_buf_size, "invalid compiled PeTTa parse");
        return -1;
    }
    scan.built.universe = universe;
    scan.skipped = tu_expr_from_ids(universe, NULL, 0u);
    scan.bang = tu_intern_symbol(universe, symbol_intern_cstr(g_symbols, "!"));
    if (scan.skipped == CETTA_ATOM_ID_NONE || scan.bang == CETTA_ATOM_ID_NONE) {
        petta_compiled_reader_v1_set_error(
            error_buf, error_buf_size, "invalid compiled PeTTa parse");
        return -1;
    }
    GSLTDirectPeTTaProjectionV1 projection = {
        .context = &scan,
        .begin_form = petta_scan_begin_form,
        .token_bytes = petta_scan_token_bytes,
        .variable_bytes = petta_scan_variable_bytes,
        .string_bytes = petta_scan_string_bytes,
        .dollar_symbol = petta_scan_dollar_symbol,
        .expression = petta_scan_expression,
        .finish_form = petta_scan_finish_form,
        .list = petta_scan_list,
    };
    memset(&direct_receipt, 0, sizeof(direct_receipt));
    int result = gslt_direct_petta_reader_v1_parse_bytes_ids_from(
        reader->plan, document->bytes, document->len, document->pos,
        max_forms, &projection, out_ids, &direct_receipt, &end, error_buf,
        error_buf_size);
    cetta_var_index_free(&scan.built.variable_index);
    free(scan.built.variables);
    free(scan.built.tokens);
    if (result >= 0)
        document->pos = end;
    return result;
}

int petta_compiled_document_v1_next(PeTTaCompiledReaderV1 *reader,
                                    PeTTaCompiledDocumentV1 *document,
                                    uint32_t max_forms,
                                    TermUniverse *universe, AtomId **out_ids,
                                    char *error_buf, size_t error_buf_size) {
    GSLTDirectPeTTaReaderV1Receipt direct_receipt;
    size_t end = 0u;
    if (out_ids)
        *out_ids = NULL;
    if (!document || max_forms == 0u)
        return -1;
    if (document->pos >= document->len)
        return 0;
    int result = petta_compiled_reader_v1_parse_from(
        reader, document->bytes, document->len, document->pos, max_forms,
        universe, out_ids, &direct_receipt, &end, error_buf, error_buf_size);
    if (result >= 0)
        document->pos = end;
    return result;
}

void petta_compiled_document_v1_release_read(
    PeTTaCompiledDocumentV1 *document) {
    if (!document || !document->bytes)
        return;
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    size_t through = document->pos / page * page;
    if (through <= document->released)
        return;
    (void)madvise((uint8_t *)document->bytes + document->released,
                  through - document->released, MADV_DONTNEED);
    document->released = through;
}

void petta_compiled_document_v1_rewind(PeTTaCompiledDocumentV1 *document) {
    if (!document)
        return;
    document->pos = 0u;
    document->released = 0u;
}

void petta_compiled_document_v1_close(PeTTaCompiledDocumentV1 *document) {
    if (!document)
        return;
    if (document->bytes)
        (void)munmap((void *)document->bytes, document->len);
    if (document->fd >= 0)
        (void)close(document->fd);
    *document = (PeTTaCompiledDocumentV1){.fd = -1};
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

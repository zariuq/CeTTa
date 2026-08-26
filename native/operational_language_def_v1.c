#include "operational_language_def_v1.h"

#include "lib_parse_native_grammar.h"
#include "native_sha256.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    OP_LANG_NT_DOCUMENT = 100u,
    OP_LANG_NT_TERM,
    OP_LANG_NT_LIST,
    OP_LANG_NT_TERMS,
    OP_LANG_NT_TRIVIA,

    OP_LANG_TM_LPAREN = 200u,
    OP_LANG_TM_RPAREN,
    OP_LANG_TM_SPACE,
    OP_LANG_TM_SYMBOL,
    OP_LANG_TM_STRING,
    OP_LANG_TM_NATURAL,
    OP_LANG_TM_EOF,
};

typedef enum {
    OP_LANG_PROD_DOCUMENT = 0,
    OP_LANG_PROD_TERM_SYMBOL,
    OP_LANG_PROD_TERM_STRING,
    OP_LANG_PROD_TERM_NATURAL,
    OP_LANG_PROD_TERM_LIST,
    OP_LANG_PROD_LIST,
    OP_LANG_PROD_TERMS_CONS,
    OP_LANG_PROD_TERMS_NIL,
    OP_LANG_PROD_TRIVIA_CONS,
    OP_LANG_PROD_TRIVIA_NIL,
    OP_LANG_PROD_COUNT
} OpLangProduction;

typedef struct {
    CettaLpNativeUtf8Lattice lattice;
    CettaLpNativeUtf8LatticeEdge *edges;
    uint32_t *start_offsets;
    uint32_t terminal_ids[7];
} OpLangTokenLattice;

typedef struct {
    CettaOpLangV1SExpr **data;
    uint32_t len;
    uint32_t cap;
} OpLangNodeVector;

typedef struct {
    uint32_t *data;
    uint32_t len;
    uint32_t cap;
} OpLangIndexVector;

typedef enum {
    OP_LANG_VALUE_UNIT = 0,
    OP_LANG_VALUE_NODE,
    OP_LANG_VALUE_NODES
} OpLangValueKind;

typedef struct {
    OpLangValueKind kind;
    CettaOpLangV1SExpr *node;
    OpLangNodeVector nodes;
} OpLangValue;

static void op_lang_set_error(char *buf,
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

static void op_lang_set_status(CettaOpLangV1Status *status,
                               CettaOpLangV1Status value) {
    if (status)
        *status = value;
}

static CettaOpLangV1SExpr *op_lang_node_alloc(CettaOpLangV1SExprKind kind,
                                              uint32_t byte_left,
                                              uint32_t byte_right) {
    CettaOpLangV1SExpr *node = calloc(1u, sizeof(*node));

    if (!node)
        return NULL;
    node->kind = kind;
    node->byte_left = byte_left;
    node->byte_right = byte_right;
    return node;
}

static void op_lang_sexpr_free(CettaOpLangV1SExpr *expression) {
    uint32_t index;

    if (!expression)
        return;
    switch (expression->kind) {
    case CETTA_OP_LANG_V1_SEXPR_SYMBOL:
        free(expression->as.symbol);
        break;
    case CETTA_OP_LANG_V1_SEXPR_STRING:
        free(expression->as.string.bytes);
        break;
    case CETTA_OP_LANG_V1_SEXPR_NATURAL:
        free(expression->as.natural);
        break;
    case CETTA_OP_LANG_V1_SEXPR_APPLICATION:
        free(expression->as.application.head);
        for (index = 0u;
             index < expression->as.application.argument_len;
             index++) {
            op_lang_sexpr_free(expression->as.application.arguments[index]);
        }
        free(expression->as.application.arguments);
        break;
    }
    free(expression);
}

static void op_lang_node_vector_free(OpLangNodeVector *vector) {
    uint32_t index;

    if (!vector)
        return;
    for (index = 0u; index < vector->len; index++)
        op_lang_sexpr_free(vector->data[index]);
    free(vector->data);
    memset(vector, 0, sizeof(*vector));
}

static bool op_lang_node_vector_push(OpLangNodeVector *vector,
                                     CettaOpLangV1SExpr *node) {
    CettaOpLangV1SExpr **next;
    uint32_t next_cap;

    if (!vector || !node)
        return false;
    if (vector->len == vector->cap) {
        next_cap = vector->cap ? vector->cap * 2u : 8u;
        if (next_cap < vector->cap) {
            return false;
        }
        next = realloc(vector->data, sizeof(*vector->data) * next_cap);
        if (!next)
            return false;
        vector->data = next;
        vector->cap = next_cap;
    }
    vector->data[vector->len++] = node;
    return true;
}

static bool op_lang_index_vector_push(OpLangIndexVector *vector,
                                      uint32_t value) {
    uint32_t *next;
    uint32_t next_cap;

    if (!vector)
        return false;
    if (vector->len == vector->cap) {
        next_cap = vector->cap ? vector->cap * 2u : 8u;
        if (next_cap < vector->cap) {
            return false;
        }
        next = realloc(vector->data, sizeof(*vector->data) * next_cap);
        if (!next)
            return false;
        vector->data = next;
        vector->cap = next_cap;
    }
    vector->data[vector->len++] = value;
    return true;
}

static void op_lang_value_free(OpLangValue *value) {
    if (!value)
        return;
    if (value->kind == OP_LANG_VALUE_NODE)
        op_lang_sexpr_free(value->node);
    else if (value->kind == OP_LANG_VALUE_NODES)
        op_lang_node_vector_free(&value->nodes);
    memset(value, 0, sizeof(*value));
}

void cetta_op_lang_v1_document_init(CettaOpLangV1Document *document) {
    if (document)
        memset(document, 0, sizeof(*document));
}

void cetta_op_lang_v1_document_free(CettaOpLangV1Document *document) {
    if (!document)
        return;
    op_lang_sexpr_free(document->root);
    memset(document, 0, sizeof(*document));
}

void cetta_op_lang_v1_init(CettaOperationalLanguageDefV1 *language) {
    if (language)
        memset(language, 0, sizeof(*language));
}

void cetta_op_lang_v1_free(CettaOperationalLanguageDefV1 *language) {
    if (!language)
        return;
    op_lang_sexpr_free(language->root);
    memset(language, 0, sizeof(*language));
}

bool cetta_op_lang_v1_symbol_is(const CettaOpLangV1SExpr *expression,
                                const char *text) {
    return expression && text &&
        expression->kind == CETTA_OP_LANG_V1_SEXPR_SYMBOL &&
        expression->as.symbol && strcmp(expression->as.symbol, text) == 0;
}

bool cetta_op_lang_v1_string_is(const CettaOpLangV1SExpr *expression,
                                const uint8_t *bytes,
                                size_t len) {
    return expression && (bytes || len == 0u) &&
        expression->kind == CETTA_OP_LANG_V1_SEXPR_STRING &&
        (size_t)expression->as.string.len == len &&
        (len == 0u ||
         (expression->as.string.bytes &&
          memcmp(expression->as.string.bytes, bytes, len) == 0));
}

bool cetta_op_lang_v1_application_is(
    const CettaOpLangV1SExpr *expression,
    const char *head,
    uint32_t argument_len) {
    return expression && head &&
        expression->kind == CETTA_OP_LANG_V1_SEXPR_APPLICATION &&
        expression->as.application.head &&
        strcmp(expression->as.application.head, head) == 0 &&
        expression->as.application.argument_len == argument_len;
}

bool cetta_op_lang_v1_sexpr_equal(const CettaOpLangV1SExpr *left,
                                  const CettaOpLangV1SExpr *right) {
    uint32_t index;

    if (left == right)
        return true;
    if (!left || !right || left->kind != right->kind ||
        left->byte_left != right->byte_left ||
        left->byte_right != right->byte_right) {
        return false;
    }
    switch (left->kind) {
    case CETTA_OP_LANG_V1_SEXPR_SYMBOL:
        return left->as.symbol && right->as.symbol &&
            strcmp(left->as.symbol, right->as.symbol) == 0;
    case CETTA_OP_LANG_V1_SEXPR_STRING:
        return left->as.string.len == right->as.string.len &&
            (left->as.string.len == 0u ||
             (left->as.string.bytes && right->as.string.bytes &&
              memcmp(left->as.string.bytes, right->as.string.bytes,
                     left->as.string.len) == 0));
    case CETTA_OP_LANG_V1_SEXPR_NATURAL:
        return left->as.natural && right->as.natural &&
            strcmp(left->as.natural, right->as.natural) == 0;
    case CETTA_OP_LANG_V1_SEXPR_APPLICATION:
        if (!left->as.application.head || !right->as.application.head ||
            strcmp(left->as.application.head,
                   right->as.application.head) != 0 ||
            left->as.application.head_byte_left !=
                right->as.application.head_byte_left ||
            left->as.application.head_byte_right !=
                right->as.application.head_byte_right ||
            left->as.application.argument_len !=
                right->as.application.argument_len) {
            return false;
        }
        for (index = 0u;
             index < left->as.application.argument_len;
             index++) {
            if (!cetta_op_lang_v1_sexpr_equal(
                    left->as.application.arguments[index],
                    right->as.application.arguments[index])) {
                return false;
            }
        }
        return true;
    }
    return false;
}

uint32_t cetta_op_lang_v1_field_len(const CettaOpLangV1SExpr *field) {
    const CettaOpLangV1SExpr *cursor = field;
    uint32_t len = 0u;

    while (cursor &&
           cetta_op_lang_v1_application_is(cursor, "LCons", 2u)) {
        if (len == UINT32_MAX)
            return 0u;
        len++;
        cursor = cursor->as.application.arguments[1];
    }
    return cetta_op_lang_v1_symbol_is(cursor, "LNil") ? len : 0u;
}

const CettaOpLangV1SExpr *cetta_op_lang_v1_field_entry(
    const CettaOpLangV1SExpr *field,
    uint32_t index) {
    const CettaOpLangV1SExpr *cursor = field;
    uint32_t position = 0u;

    while (cursor &&
           cetta_op_lang_v1_application_is(cursor, "LCons", 2u)) {
        if (position == index)
            return cursor->as.application.arguments[0];
        position++;
        cursor = cursor->as.application.arguments[1];
    }
    return NULL;
}

static bool op_lang_proper_list(const CettaOpLangV1SExpr *field) {
    const CettaOpLangV1SExpr *cursor = field;

    while (cursor &&
           cetta_op_lang_v1_application_is(cursor, "LCons", 2u)) {
        cursor = cursor->as.application.arguments[1];
    }
    return cetta_op_lang_v1_symbol_is(cursor, "LNil");
}

const char *cetta_op_lang_v1_status_name(CettaOpLangV1Status status) {
    switch (status) {
    case CETTA_OP_LANG_V1_OK:
        return "ok";
    case CETTA_OP_LANG_V1_BAD_ARGUMENT:
        return "bad-argument";
    case CETTA_OP_LANG_V1_INVALID_UTF8:
        return "invalid-utf8";
    case CETTA_OP_LANG_V1_GLL_RESOURCE_LIMIT:
        return "gll-resource-limit";
    case CETTA_OP_LANG_V1_GLR_RESOURCE_LIMIT:
        return "glr-resource-limit";
    case CETTA_OP_LANG_V1_SYNTAX_REJECTED:
        return "syntax-rejected";
    case CETTA_OP_LANG_V1_AMBIGUOUS:
        return "ambiguous";
    case CETTA_OP_LANG_V1_BACKEND_DISAGREEMENT:
        return "backend-disagreement";
    case CETTA_OP_LANG_V1_MALFORMED_LANGUAGE_DEF:
        return "malformed-language-def";
    case CETTA_OP_LANG_V1_IO_FAILURE:
        return "io-failure";
    case CETTA_OP_LANG_V1_ALLOCATION_FAILURE:
        return "allocation-failure";
    case CETTA_OP_LANG_V1_INTERNAL_FAILURE:
        return "internal-failure";
    }
    return "unknown";
}

static bool op_lang_grammar_set_production(
    CettaLpNativeGrammar *grammar,
    uint32_t index,
    uint32_t lhs,
    const CettaLpNativeSymbol *rhs,
    uint32_t rhs_len) {
    CettaLpNativeProduction *production;

    if (!grammar || index >= OP_LANG_PROD_COUNT)
        return false;
    production = &grammar->productions[index];
    production->label = 1000u + index;
    production->lhs = lhs;
    production->rhs_len = rhs_len;
    if (rhs_len > 0u) {
        production->rhs = malloc(sizeof(*production->rhs) * rhs_len);
        if (!production->rhs)
            return false;
        memcpy(production->rhs, rhs, sizeof(*production->rhs) * rhs_len);
    }
    grammar->entries[index].kind = CETTA_LP_NATIVE_ENTRY_PRODUCTION;
    grammar->entries[index].index = index;
    return true;
}

#define OP_LANG_TM(id) \
    (CettaLpNativeSymbol){CETTA_LP_NATIVE_SYMBOL_TM, (id), 0u}
#define OP_LANG_NT(id) \
    (CettaLpNativeSymbol){CETTA_LP_NATIVE_SYMBOL_HL, (id), 0u}

static bool op_lang_grammar_build(CettaLpNativeGrammar *grammar) {
    static const CettaLpNativeSymbol document[] = {
        OP_LANG_NT(OP_LANG_NT_TRIVIA),
        OP_LANG_NT(OP_LANG_NT_TERM),
        OP_LANG_NT(OP_LANG_NT_TRIVIA),
        OP_LANG_TM(OP_LANG_TM_EOF),
    };
    static const CettaLpNativeSymbol term_symbol[] = {
        OP_LANG_TM(OP_LANG_TM_SYMBOL),
    };
    static const CettaLpNativeSymbol term_string[] = {
        OP_LANG_TM(OP_LANG_TM_STRING),
    };
    static const CettaLpNativeSymbol term_natural[] = {
        OP_LANG_TM(OP_LANG_TM_NATURAL),
    };
    static const CettaLpNativeSymbol term_list[] = {
        OP_LANG_NT(OP_LANG_NT_LIST),
    };
    static const CettaLpNativeSymbol list[] = {
        OP_LANG_TM(OP_LANG_TM_LPAREN),
        OP_LANG_TM(OP_LANG_TM_SYMBOL),
        OP_LANG_NT(OP_LANG_NT_TRIVIA),
        OP_LANG_NT(OP_LANG_NT_TERMS),
        OP_LANG_TM(OP_LANG_TM_RPAREN),
    };
    static const CettaLpNativeSymbol terms_cons[] = {
        OP_LANG_NT(OP_LANG_NT_TERM),
        OP_LANG_NT(OP_LANG_NT_TRIVIA),
        OP_LANG_NT(OP_LANG_NT_TERMS),
    };
    static const CettaLpNativeSymbol trivia_cons[] = {
        OP_LANG_TM(OP_LANG_TM_SPACE),
        OP_LANG_NT(OP_LANG_NT_TRIVIA),
    };
    bool ok;

    if (!grammar)
        return false;
    cetta_lp_native_grammar_init(grammar);
    grammar->production_len = OP_LANG_PROD_COUNT;
    grammar->entry_len = OP_LANG_PROD_COUNT;
    grammar->productions = calloc(
        grammar->production_len, sizeof(*grammar->productions));
    grammar->entries = calloc(grammar->entry_len, sizeof(*grammar->entries));
    if (!grammar->productions || !grammar->entries) {
        cetta_lp_native_grammar_free(grammar);
        return false;
    }
    ok =
        op_lang_grammar_set_production(
            grammar, OP_LANG_PROD_DOCUMENT, OP_LANG_NT_DOCUMENT,
            document, 4u) &&
        op_lang_grammar_set_production(
            grammar, OP_LANG_PROD_TERM_SYMBOL, OP_LANG_NT_TERM,
            term_symbol, 1u) &&
        op_lang_grammar_set_production(
            grammar, OP_LANG_PROD_TERM_STRING, OP_LANG_NT_TERM,
            term_string, 1u) &&
        op_lang_grammar_set_production(
            grammar, OP_LANG_PROD_TERM_NATURAL, OP_LANG_NT_TERM,
            term_natural, 1u) &&
        op_lang_grammar_set_production(
            grammar, OP_LANG_PROD_TERM_LIST, OP_LANG_NT_TERM,
            term_list, 1u) &&
        op_lang_grammar_set_production(
            grammar, OP_LANG_PROD_LIST, OP_LANG_NT_LIST,
            list, 5u) &&
        op_lang_grammar_set_production(
            grammar, OP_LANG_PROD_TERMS_CONS, OP_LANG_NT_TERMS,
            terms_cons, 3u) &&
        op_lang_grammar_set_production(
            grammar, OP_LANG_PROD_TERMS_NIL, OP_LANG_NT_TERMS,
            NULL, 0u) &&
        op_lang_grammar_set_production(
            grammar, OP_LANG_PROD_TRIVIA_CONS, OP_LANG_NT_TRIVIA,
            trivia_cons, 2u) &&
        op_lang_grammar_set_production(
            grammar, OP_LANG_PROD_TRIVIA_NIL, OP_LANG_NT_TRIVIA,
            NULL, 0u);
    if (!ok)
        cetta_lp_native_grammar_free(grammar);
    return ok;
}

#undef OP_LANG_TM
#undef OP_LANG_NT

static void op_lang_token_lattice_free(OpLangTokenLattice *tokens) {
    if (!tokens)
        return;
    free(tokens->start_offsets);
    free(tokens->edges);
    memset(tokens, 0, sizeof(*tokens));
}

static bool op_lang_scalar_is_space(uint32_t scalar) {
    return scalar == 32u || (scalar >= 9u && scalar <= 13u);
}

static bool op_lang_scalar_is_symbol(uint32_t scalar) {
    if (scalar == (uint32_t)'(' || scalar == (uint32_t)')' ||
        scalar == (uint32_t)'"' || op_lang_scalar_is_space(scalar)) {
        return false;
    }
    return (scalar >= 33u && scalar <= 126u && scalar != 127u) ||
        scalar >= 128u;
}

static bool op_lang_scalar_is_lower_hex(uint32_t scalar) {
    return (scalar >= (uint32_t)'0' && scalar <= (uint32_t)'9') ||
        (scalar >= (uint32_t)'a' && scalar <= (uint32_t)'f');
}

static bool op_lang_string_token_right(
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t left,
    uint32_t *right_out) {
    uint32_t cursor;

    if (!view || !right_out || left >= view->scalar_len ||
        cetta_lp_native_utf8_scalar_view_scalar_at(view, left) !=
            (uint32_t)'"') {
        return false;
    }
    cursor = left + 1u;
    while (cursor < view->scalar_len) {
        uint32_t scalar =
            cetta_lp_native_utf8_scalar_view_scalar_at(view, cursor);
        if (scalar == (uint32_t)'"') {
            *right_out = cursor + 1u;
            return true;
        }
        if (scalar == (uint32_t)'\\') {
            uint32_t escaped;
            if (++cursor >= view->scalar_len)
                return false;
            escaped = cetta_lp_native_utf8_scalar_view_scalar_at(view, cursor);
            if (escaped == (uint32_t)'"' || escaped == (uint32_t)'\\' ||
                escaped == (uint32_t)'n' || escaped == (uint32_t)'t') {
                cursor++;
                continue;
            }
            if (escaped == (uint32_t)'x' &&
                cursor + 2u < view->scalar_len &&
                op_lang_scalar_is_lower_hex(
                    cetta_lp_native_utf8_scalar_view_scalar_at(
                        view, cursor + 1u)) &&
                op_lang_scalar_is_lower_hex(
                    cetta_lp_native_utf8_scalar_view_scalar_at(
                        view, cursor + 2u))) {
                uint32_t high =
                    cetta_lp_native_utf8_scalar_view_scalar_at(
                        view, cursor + 1u);
                uint32_t low =
                    cetta_lp_native_utf8_scalar_view_scalar_at(
                        view, cursor + 2u);
                uint32_t value =
                    (high <= (uint32_t)'9' ? high - (uint32_t)'0'
                                           : high - (uint32_t)'a' + 10u) *
                        16u +
                    (low <= (uint32_t)'9' ? low - (uint32_t)'0'
                                          : low - (uint32_t)'a' + 10u);
                if ((value <= 31u && value != 9u && value != 10u) ||
                    value == 127u) {
                    cursor += 3u;
                    continue;
                }
            }
            return false;
        }
        if (scalar < 32u || scalar == 127u)
            return false;
        cursor++;
    }
    return false;
}

static bool op_lang_token_lattice_build(
    OpLangTokenLattice *tokens,
    const CettaLpNativeUtf8ScalarView *view,
    char *error_buf,
    size_t error_buf_size) {
    static const uint32_t terminal_ids[] = {
        OP_LANG_TM_LPAREN,
        OP_LANG_TM_RPAREN,
        OP_LANG_TM_SPACE,
        OP_LANG_TM_SYMBOL,
        OP_LANG_TM_STRING,
        OP_LANG_TM_NATURAL,
        OP_LANG_TM_EOF,
    };
    OpLangTokenLattice result;
    uint32_t position = 0u;
    uint32_t edge_len = 0u;
    uint32_t index;

    if (!tokens || !view ||
        !cetta_lp_native_utf8_scalar_view_validate(
            view, error_buf, error_buf_size)) {
        return false;
    }
    memset(&result, 0, sizeof(result));
    if (view->scalar_len > UINT32_MAX - 1u ||
        (size_t)view->scalar_len + 1u >
            SIZE_MAX / sizeof(*result.edges) ||
        (size_t)view->scalar_len + 2u >
            SIZE_MAX / sizeof(*result.start_offsets)) {
        op_lang_set_error(error_buf, error_buf_size,
                          "operational LanguageDef source is too large");
        return false;
    }
    result.edges = calloc(
        (size_t)view->scalar_len + 1u, sizeof(*result.edges));
    result.start_offsets = calloc(
        (size_t)view->scalar_len + 2u, sizeof(*result.start_offsets));
    if (!result.edges || !result.start_offsets) {
        op_lang_set_error(error_buf, error_buf_size,
                          "failed to allocate source token lattice");
        op_lang_token_lattice_free(&result);
        return false;
    }
    memcpy(result.terminal_ids, terminal_ids, sizeof(terminal_ids));

    /*
     * These lexical classes are disjoint.  Maximal whitespace and word runs
     * therefore form one complete deterministic lattice; syntax and all
     * remaining ambiguity are still owned by the independent GLL/GLR parses.
     */
    while (position < view->scalar_len) {
        CettaLpNativeUtf8LatticeEdge *edge = &result.edges[edge_len];
        uint32_t scalar =
            cetta_lp_native_utf8_scalar_view_scalar_at(view, position);
        uint32_t right = position + 1u;
        uint32_t terminal_id;

        if (scalar == (uint32_t)'(') {
            terminal_id = OP_LANG_TM_LPAREN;
        } else if (scalar == (uint32_t)')') {
            terminal_id = OP_LANG_TM_RPAREN;
        } else if (scalar == (uint32_t)'"') {
            terminal_id = OP_LANG_TM_STRING;
            if (!op_lang_string_token_right(view, position, &right)) {
                position++;
                continue;
            }
        } else if (op_lang_scalar_is_space(scalar)) {
            terminal_id = OP_LANG_TM_SPACE;
            while (right < view->scalar_len &&
                   op_lang_scalar_is_space(
                       cetta_lp_native_utf8_scalar_view_scalar_at(
                           view, right))) {
                right++;
            }
        } else if (op_lang_scalar_is_symbol(scalar)) {
            terminal_id = OP_LANG_TM_SYMBOL;
            while (right < view->scalar_len &&
                   op_lang_scalar_is_symbol(
                       cetta_lp_native_utf8_scalar_view_scalar_at(
                           view, right))) {
                right++;
            }
            {
                uint32_t cursor;
                bool all_digits = true;
                for (cursor = position; cursor < right; cursor++) {
                    uint32_t word_scalar =
                        cetta_lp_native_utf8_scalar_view_scalar_at(
                            view, cursor);
                    if (word_scalar < (uint32_t)'0' ||
                        word_scalar > (uint32_t)'9') {
                        all_digits = false;
                        break;
                    }
                }
                if (all_digits && right - position > 1u &&
                    cetta_lp_native_utf8_scalar_view_scalar_at(
                        view, position) == (uint32_t)'0') {
                    position = right;
                    continue;
                }
                if (all_digits)
                    terminal_id = OP_LANG_TM_NATURAL;
            }
        } else {
            /* Leave a lexical gap.  Both generalized parsers must reject it. */
            position++;
            continue;
        }
        *edge = (CettaLpNativeUtf8LatticeEdge){
            .terminal_id = terminal_id,
            .scalar_left = position,
            .scalar_right = right,
            .byte_left = cetta_lp_native_utf8_scalar_view_byte_offset(
                view, position),
            .byte_right = cetta_lp_native_utf8_scalar_view_byte_offset(
                view, right),
            .value_kind = CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_WITNESS,
            .value = edge_len + 1u,
        };
        edge_len++;
        position = right;
    }
    result.edges[edge_len++] = (CettaLpNativeUtf8LatticeEdge){
        .terminal_id = OP_LANG_TM_EOF,
        .scalar_left = view->scalar_len,
        .scalar_right = view->scalar_len,
        .byte_left = view->input_byte_len,
        .byte_right = view->input_byte_len,
        .value_kind = CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_EOF,
        .value = 0u,
    };

    index = 0u;
    for (position = 0u; position <= view->scalar_len; position++) {
        while (index < edge_len &&
               result.edges[index].scalar_left < position) {
            index++;
        }
        result.start_offsets[position] = index;
    }
    result.start_offsets[view->scalar_len + 1u] = edge_len;
    result.lattice = (CettaLpNativeUtf8Lattice){
        .terminal_ids = result.terminal_ids,
        .terminal_len =
            (uint32_t)(sizeof(terminal_ids) / sizeof(*terminal_ids)),
        .edges = result.edges,
        .edge_len = edge_len,
        .start_offsets = result.start_offsets,
        .start_offset_len = view->scalar_len + 2u,
        .codepoints = view->codepoints,
        .byte_offsets = view->byte_offsets,
        .scalar_len = view->scalar_len,
        .input_byte_len = view->input_byte_len,
        /* The lattice is derived; source decoding is reported separately. */
        .decoded_byte_len = 0u,
        .source_pass_count = 0u,
    };
    if (!cetta_lp_native_utf8_lattice_validate(
            &result.lattice, error_buf, error_buf_size)) {
        op_lang_token_lattice_free(&result);
        return false;
    }
    op_lang_token_lattice_free(tokens);
    *tokens = result;
    tokens->lattice.terminal_ids = tokens->terminal_ids;
    return true;
}

static bool op_lang_unique_node(
    const CettaLpNativeUtf8Forest *forest,
    uint32_t node_index,
    uint8_t *state,
    bool *ambiguous,
    char *error_buf,
    size_t error_buf_size) {
    const CettaLpNativeUtf8ForestNode *node;
    const CettaLpNativeUtf8ForestChoice *choice;

    if (!forest || !state || node_index >= forest->node_len) {
        op_lang_set_error(error_buf, error_buf_size,
                          "forest contains an invalid node reference");
        return false;
    }
    if (state[node_index] == 2u)
        return true;
    if (state[node_index] == 1u) {
        op_lang_set_error(error_buf, error_buf_size,
                          "forest contains a cyclic derivation");
        return false;
    }
    state[node_index] = 1u;
    node = &forest->nodes[node_index];
    if (node->kind == CETTA_LP_NATIVE_UTF8_FOREST_TERM ||
        node->kind == CETTA_LP_NATIVE_UTF8_FOREST_EPSILON) {
        if (node->choice_len != 0u) {
            op_lang_set_error(error_buf, error_buf_size,
                              "leaf forest node carries packed choices");
            return false;
        }
        state[node_index] = 2u;
        return true;
    }
    if (node->choice_len != 1u) {
        if (node->choice_len > 1u && ambiguous)
            *ambiguous = true;
        if (node->choice_len == 0u) {
            op_lang_set_error(
                error_buf, error_buf_size,
                "derived forest node %u of kind %u has no packed choice",
                node_index, (unsigned)node->kind);
        }
        return node->choice_len > 1u;
    }
    if (node->choice_begin >= forest->choice_len) {
        op_lang_set_error(error_buf, error_buf_size,
                          "forest choice range is out of bounds");
        return false;
    }
    choice = &forest->choices[node->choice_begin];
    if (choice->parent_node != node_index ||
        choice->production_index >= OP_LANG_PROD_COUNT) {
        op_lang_set_error(error_buf, error_buf_size,
                          "forest choice identity is malformed");
        return false;
    }
    if (choice->prefix_node != CETTA_LP_NATIVE_UTF8_FOREST_NONE &&
        !op_lang_unique_node(forest, choice->prefix_node, state, ambiguous,
                             error_buf, error_buf_size)) {
        return false;
    }
    if (choice->child_node != CETTA_LP_NATIVE_UTF8_FOREST_NONE &&
        !op_lang_unique_node(forest, choice->child_node, state, ambiguous,
                             error_buf, error_buf_size)) {
        return false;
    }
    state[node_index] = 2u;
    return true;
}

static bool op_lang_collect_components(
    const CettaLpNativeUtf8Forest *forest,
    uint32_t node_index,
    OpLangIndexVector *components,
    uint32_t depth,
    char *error_buf,
    size_t error_buf_size) {
    const CettaLpNativeUtf8ForestNode *node;
    const CettaLpNativeUtf8ForestChoice *choice;

    if (node_index == CETTA_LP_NATIVE_UTF8_FOREST_NONE)
        return true;
    if (!forest || !components || node_index >= forest->node_len ||
        depth > 65536u) {
        op_lang_set_error(error_buf, error_buf_size,
                          "invalid or excessively deep forest component");
        return false;
    }
    node = &forest->nodes[node_index];
    if (node->kind != CETTA_LP_NATIVE_UTF8_FOREST_INTERMEDIATE)
        return op_lang_index_vector_push(components, node_index);
    if (node->choice_len != 1u || node->choice_begin >= forest->choice_len) {
        op_lang_set_error(error_buf, error_buf_size,
                          "intermediate forest node is not uniquely packed");
        return false;
    }
    choice = &forest->choices[node->choice_begin];
    return op_lang_collect_components(
               forest, choice->prefix_node, components, depth + 1u,
               error_buf, error_buf_size) &&
        op_lang_collect_components(
               forest, choice->child_node, components, depth + 1u,
               error_buf, error_buf_size);
}

static bool op_lang_symbol_components(
    const CettaLpNativeUtf8Forest *forest,
    uint32_t node_index,
    uint32_t *production_index,
    OpLangIndexVector *components,
    char *error_buf,
    size_t error_buf_size) {
    const CettaLpNativeUtf8ForestNode *node;
    const CettaLpNativeUtf8ForestChoice *choice;

    if (!forest || !production_index || !components ||
        node_index >= forest->node_len) {
        op_lang_set_error(error_buf, error_buf_size,
                          "bad forest projection arguments");
        return false;
    }
    node = &forest->nodes[node_index];
    if (node->kind != CETTA_LP_NATIVE_UTF8_FOREST_SYMBOL ||
        node->choice_len != 1u || node->choice_begin >= forest->choice_len) {
        op_lang_set_error(error_buf, error_buf_size,
                          "expected one uniquely derived symbol node");
        return false;
    }
    choice = &forest->choices[node->choice_begin];
    *production_index = choice->production_index;
    return op_lang_collect_components(
               forest, choice->prefix_node, components, 0u,
               error_buf, error_buf_size) &&
        op_lang_collect_components(
               forest, choice->child_node, components, 0u,
               error_buf, error_buf_size);
}

static uint8_t op_lang_hex_value(uint8_t byte) {
    return byte <= (uint8_t)'9' ? (uint8_t)(byte - (uint8_t)'0')
                               : (uint8_t)(byte - (uint8_t)'a' + 10u);
}

static CettaOpLangV1SExpr *op_lang_project_word(
    const CettaLpNativeUtf8ForestNode *terminal,
    uint32_t expected_symbol_id,
    CettaOpLangV1SExprKind kind,
    const uint8_t *bytes,
    size_t byte_len) {
    CettaOpLangV1SExpr *node;
    char **slot;
    size_t len;

    if (!terminal || !bytes ||
        terminal->kind != CETTA_LP_NATIVE_UTF8_FOREST_TERM ||
        terminal->symbol_id != expected_symbol_id ||
        terminal->byte_right <= terminal->byte_left ||
        (size_t)terminal->byte_right > byte_len) {
        return NULL;
    }
    if (kind != CETTA_OP_LANG_V1_SEXPR_SYMBOL &&
        kind != CETTA_OP_LANG_V1_SEXPR_NATURAL) {
        return NULL;
    }
    len = (size_t)(terminal->byte_right - terminal->byte_left);
    node = op_lang_node_alloc(kind, terminal->byte_left, terminal->byte_right);
    if (!node)
        return NULL;
    slot = kind == CETTA_OP_LANG_V1_SEXPR_SYMBOL
        ? &node->as.symbol : &node->as.natural;
    *slot = malloc(len + 1u);
    if (!*slot) {
        op_lang_sexpr_free(node);
        return NULL;
    }
    memcpy(*slot, bytes + terminal->byte_left, len);
    (*slot)[len] = '\0';
    return node;
}

static CettaOpLangV1SExpr *op_lang_project_string(
    const CettaLpNativeUtf8ForestNode *terminal,
    const uint8_t *bytes,
    size_t byte_len) {
    CettaOpLangV1SExpr *node;
    uint8_t *decoded;
    size_t cursor;
    size_t end;
    uint32_t decoded_len = 0u;

    if (!terminal || !bytes ||
        terminal->kind != CETTA_LP_NATIVE_UTF8_FOREST_TERM ||
        terminal->symbol_id != OP_LANG_TM_STRING ||
        terminal->byte_right < terminal->byte_left + 2u ||
        (size_t)terminal->byte_right > byte_len ||
        bytes[terminal->byte_left] != (uint8_t)'"' ||
        bytes[terminal->byte_right - 1u] != (uint8_t)'"') {
        return NULL;
    }
    node = op_lang_node_alloc(
        CETTA_OP_LANG_V1_SEXPR_STRING,
        terminal->byte_left, terminal->byte_right);
    if (!node)
        return NULL;
    decoded = malloc((size_t)(terminal->byte_right - terminal->byte_left));
    if (!decoded) {
        op_lang_sexpr_free(node);
        return NULL;
    }
    cursor = (size_t)terminal->byte_left + 1u;
    end = (size_t)terminal->byte_right - 1u;
    while (cursor < end) {
        uint8_t byte = bytes[cursor++];
        if (byte != (uint8_t)'\\') {
            decoded[decoded_len++] = byte;
            continue;
        }
        if (cursor >= end) {
            free(decoded);
            op_lang_sexpr_free(node);
            return NULL;
        }
        byte = bytes[cursor++];
        if (byte == (uint8_t)'"' || byte == (uint8_t)'\\') {
            decoded[decoded_len++] = byte;
        } else if (byte == (uint8_t)'n') {
            decoded[decoded_len++] = (uint8_t)'\n';
        } else if (byte == (uint8_t)'t') {
            decoded[decoded_len++] = (uint8_t)'\t';
        } else if (byte == (uint8_t)'x' && cursor + 1u < end) {
            decoded[decoded_len++] = (uint8_t)(
                op_lang_hex_value(bytes[cursor]) * 16u +
                op_lang_hex_value(bytes[cursor + 1u]));
            cursor += 2u;
        } else {
            free(decoded);
            op_lang_sexpr_free(node);
            return NULL;
        }
    }
    node->as.string.bytes = decoded;
    node->as.string.len = decoded_len;
    return node;
}

static bool op_lang_eval_symbol(
    const CettaLpNativeUtf8Forest *forest,
    uint32_t node_index,
    const uint8_t *bytes,
    size_t byte_len,
    uint32_t depth,
    OpLangValue *out,
    char *error_buf,
    size_t error_buf_size) {
    const CettaLpNativeUtf8ForestNode *node;
    OpLangIndexVector components = {0};
    uint32_t production_index = 0u;
    bool ok = false;

    if (!forest || !out || node_index >= forest->node_len || depth > 8192u) {
        op_lang_set_error(error_buf, error_buf_size,
                          "invalid or excessively deep source derivation");
        return false;
    }
    memset(out, 0, sizeof(*out));
    node = &forest->nodes[node_index];
    if (!op_lang_symbol_components(
            forest, node_index, &production_index, &components,
            error_buf, error_buf_size)) {
        goto done;
    }
    switch ((OpLangProduction)production_index) {
    case OP_LANG_PROD_DOCUMENT: {
        OpLangValue term = {0};
        if (components.len != 4u ||
            !op_lang_eval_symbol(
                forest, components.data[1], bytes, byte_len, depth + 1u,
                &term, error_buf, error_buf_size) ||
            term.kind != OP_LANG_VALUE_NODE) {
            op_lang_value_free(&term);
            goto malformed;
        }
        out->kind = OP_LANG_VALUE_NODE;
        out->node = term.node;
        term.node = NULL;
        ok = true;
        break;
    }
    case OP_LANG_PROD_TERM_SYMBOL:
    case OP_LANG_PROD_TERM_STRING:
    case OP_LANG_PROD_TERM_NATURAL: {
        const CettaLpNativeUtf8ForestNode *terminal;
        CettaOpLangV1SExpr *term;
        if (components.len != 1u ||
            components.data[0] >= forest->node_len) {
            goto malformed;
        }
        terminal = &forest->nodes[components.data[0]];
        if (production_index == OP_LANG_PROD_TERM_SYMBOL) {
            term = op_lang_project_word(
                terminal, OP_LANG_TM_SYMBOL,
                CETTA_OP_LANG_V1_SEXPR_SYMBOL, bytes, byte_len);
        } else if (production_index == OP_LANG_PROD_TERM_NATURAL) {
            term = op_lang_project_word(
                terminal, OP_LANG_TM_NATURAL,
                CETTA_OP_LANG_V1_SEXPR_NATURAL, bytes, byte_len);
        } else {
            term = op_lang_project_string(terminal, bytes, byte_len);
        }
        if (!term)
            goto allocation;
        out->kind = OP_LANG_VALUE_NODE;
        out->node = term;
        ok = true;
        break;
    }
    case OP_LANG_PROD_TERM_LIST: {
        OpLangValue child = {0};
        if (components.len != 1u ||
            !op_lang_eval_symbol(
                forest, components.data[0], bytes, byte_len, depth + 1u,
                &child, error_buf, error_buf_size) ||
            child.kind != OP_LANG_VALUE_NODE) {
            op_lang_value_free(&child);
            goto malformed;
        }
        out->kind = OP_LANG_VALUE_NODE;
        out->node = child.node;
        child.node = NULL;
        ok = true;
        break;
    }
    case OP_LANG_PROD_LIST: {
        OpLangValue items = {0};
        CettaOpLangV1SExpr *application;
        CettaOpLangV1SExpr *head;
        if (components.len != 5u || components.data[1] >= forest->node_len ||
            !op_lang_eval_symbol(
                forest, components.data[3], bytes, byte_len, depth + 1u,
                &items, error_buf, error_buf_size) ||
            items.kind != OP_LANG_VALUE_NODES) {
            op_lang_value_free(&items);
            goto malformed;
        }
        head = op_lang_project_word(
            &forest->nodes[components.data[1]], OP_LANG_TM_SYMBOL,
            CETTA_OP_LANG_V1_SEXPR_SYMBOL, bytes, byte_len);
        if (!head) {
            op_lang_value_free(&items);
            goto allocation;
        }
        application = op_lang_node_alloc(
            CETTA_OP_LANG_V1_SEXPR_APPLICATION,
            node->byte_left, node->byte_right);
        if (!application) {
            op_lang_sexpr_free(head);
            op_lang_value_free(&items);
            goto allocation;
        }
        application->as.application.head = head->as.symbol;
        application->as.application.head_byte_left = head->byte_left;
        application->as.application.head_byte_right = head->byte_right;
        application->as.application.arguments = items.nodes.data;
        application->as.application.argument_len = items.nodes.len;
        head->as.symbol = NULL;
        op_lang_sexpr_free(head);
        memset(&items.nodes, 0, sizeof(items.nodes));
        out->kind = OP_LANG_VALUE_NODE;
        out->node = application;
        ok = true;
        break;
    }
    case OP_LANG_PROD_TERMS_CONS: {
        OpLangValue head = {0};
        OpLangValue tail = {0};
        OpLangNodeVector result = {0};
        uint32_t index;
        if (components.len != 3u ||
            !op_lang_eval_symbol(
                forest, components.data[0], bytes, byte_len, depth + 1u,
                &head, error_buf, error_buf_size) ||
            head.kind != OP_LANG_VALUE_NODE ||
            !op_lang_eval_symbol(
                forest, components.data[2], bytes, byte_len, depth + 1u,
                &tail, error_buf, error_buf_size) ||
            tail.kind != OP_LANG_VALUE_NODES ||
            !op_lang_node_vector_push(&result, head.node)) {
            op_lang_value_free(&head);
            op_lang_value_free(&tail);
            op_lang_node_vector_free(&result);
            goto malformed;
        }
        head.node = NULL;
        for (index = 0u; index < tail.nodes.len; index++) {
            if (!op_lang_node_vector_push(&result, tail.nodes.data[index])) {
                uint32_t remaining;
                for (remaining = index; remaining < tail.nodes.len;
                     remaining++) {
                    op_lang_sexpr_free(tail.nodes.data[remaining]);
                }
                tail.nodes.len = 0u;
                op_lang_node_vector_free(&tail.nodes);
                op_lang_node_vector_free(&result);
                goto allocation;
            }
            tail.nodes.data[index] = NULL;
        }
        op_lang_node_vector_free(&tail.nodes);
        out->kind = OP_LANG_VALUE_NODES;
        out->nodes = result;
        ok = true;
        break;
    }
    case OP_LANG_PROD_TERMS_NIL:
        if (components.len > 1u ||
            (components.len == 1u &&
             (components.data[0] >= forest->node_len ||
              forest->nodes[components.data[0]].kind !=
                  CETTA_LP_NATIVE_UTF8_FOREST_EPSILON))) {
            goto malformed;
        }
        out->kind = OP_LANG_VALUE_NODES;
        ok = true;
        break;
    case OP_LANG_PROD_TRIVIA_CONS:
    case OP_LANG_PROD_TRIVIA_NIL:
        out->kind = OP_LANG_VALUE_UNIT;
        ok = true;
        break;
    case OP_LANG_PROD_COUNT:
        goto malformed;
    }
    goto done;

allocation:
    op_lang_set_error(error_buf, error_buf_size,
                      "failed to allocate source projection");
    goto done;

malformed:
    if (!error_buf || error_buf[0] == '\0') {
        op_lang_set_error(
            error_buf, error_buf_size,
            "forest action %u received %u components",
            production_index, components.len);
    }

done:
    free(components.data);
    if (!ok)
        op_lang_value_free(out);
    return ok;
}

static uint64_t op_lang_fnv_u8(uint64_t value, uint8_t byte) {
    return (value ^ (uint64_t)byte) * UINT64_C(1099511628211);
}

static uint64_t op_lang_fnv_u32(uint64_t value, uint32_t number) {
    uint32_t shift;

    for (shift = 0u; shift < 32u; shift += 8u)
        value = op_lang_fnv_u8(value, (uint8_t)(number >> shift));
    return value;
}

static uint64_t op_lang_sexpr_fingerprint(const CettaOpLangV1SExpr *node,
                                          uint64_t value) {
    uint32_t index;

    if (!node)
        return op_lang_fnv_u8(value, 0xffu);
    value = op_lang_fnv_u8(value, (uint8_t)node->kind);
    value = op_lang_fnv_u32(value, node->byte_left);
    value = op_lang_fnv_u32(value, node->byte_right);
    if (node->kind == CETTA_OP_LANG_V1_SEXPR_SYMBOL ||
        node->kind == CETTA_OP_LANG_V1_SEXPR_NATURAL) {
        const unsigned char *cursor = (const unsigned char *)(
            node->kind == CETTA_OP_LANG_V1_SEXPR_SYMBOL
                ? node->as.symbol : node->as.natural);
        while (cursor && *cursor)
            value = op_lang_fnv_u8(value, *cursor++);
        return op_lang_fnv_u8(value, 0u);
    }
    if (node->kind == CETTA_OP_LANG_V1_SEXPR_STRING) {
        value = op_lang_fnv_u32(value, node->as.string.len);
        for (index = 0u; index < node->as.string.len; index++)
            value = op_lang_fnv_u8(value, node->as.string.bytes[index]);
        return value;
    }
    {
        const unsigned char *cursor =
            (const unsigned char *)node->as.application.head;
        while (cursor && *cursor)
            value = op_lang_fnv_u8(value, *cursor++);
        value = op_lang_fnv_u8(value, 0u);
    }
    value = op_lang_fnv_u32(value, node->as.application.head_byte_left);
    value = op_lang_fnv_u32(value, node->as.application.head_byte_right);
    value = op_lang_fnv_u32(value, node->as.application.argument_len);
    for (index = 0u; index < node->as.application.argument_len; index++) {
        value = op_lang_sexpr_fingerprint(
            node->as.application.arguments[index], value);
    }
    return value;
}

static void op_lang_receipt_copy(CettaOpLangV1ParserReceipt *receipt,
                                 const CettaLpNativeUtf8Forest *forest,
                                 const CettaOpLangV1SExpr *root,
                                 uint32_t source_decode_pass_count,
                                 uint32_t lexical_projection_pass_count) {
    memset(receipt, 0, sizeof(*receipt));
    receipt->node_len = forest->node_len;
    receipt->choice_len = forest->choice_len;
    receipt->work_item_len = forest->work_item_len;
    receipt->graph_node_len = forest->graph_node_len;
    receipt->stack_node_len = forest->stack_node_len;
    receipt->source_pass_count = forest->source_pass_count;
    receipt->decoded_byte_len = forest->decoded_byte_len;
    receipt->source_decode_pass_count = source_decode_pass_count;
    receipt->lexical_projection_pass_count = lexical_projection_pass_count;
    receipt->derivation_fingerprint = op_lang_sexpr_fingerprint(
        root, UINT64_C(14695981039346656037));
}

static bool op_lang_forest_project(
    const CettaLpNativeUtf8Forest *forest,
    const uint8_t *bytes,
    size_t byte_len,
    CettaOpLangV1SExpr **root,
    CettaOpLangV1ParserReceipt *receipt,
    uint32_t source_decode_pass_count,
    uint32_t lexical_projection_pass_count,
    CettaOpLangV1Status *status,
    char *error_buf,
    size_t error_buf_size) {
    uint8_t *state = NULL;
    bool ambiguous = false;
    OpLangValue value = {0};
    uint32_t root_index;
    bool ok = false;

    if (!forest || !root || !receipt)
        return false;
    if (forest->outcome != CETTA_LP_NATIVE_UTF8_FOREST_COMPLETED) {
        op_lang_set_error(error_buf, error_buf_size,
                          "parser exhausted its work budget");
        return false;
    }
    if (forest->root_len != 1u) {
        op_lang_set_status(status, CETTA_OP_LANG_V1_SYNTAX_REJECTED);
        op_lang_set_error(error_buf, error_buf_size,
                          "canonical source has no unique full-input root");
        return false;
    }
    root_index = forest->roots[0];
    if (root_index >= forest->node_len ||
        forest->nodes[root_index].kind !=
            CETTA_LP_NATIVE_UTF8_FOREST_SYMBOL ||
        forest->nodes[root_index].symbol_id != OP_LANG_NT_DOCUMENT ||
        forest->nodes[root_index].scalar_left != 0u ||
        forest->nodes[root_index].scalar_right != forest->scalar_len) {
        op_lang_set_status(status, CETTA_OP_LANG_V1_SYNTAX_REJECTED);
        op_lang_set_error(error_buf, error_buf_size,
                          "canonical source root has the wrong extent");
        return false;
    }
    state = calloc(forest->node_len ? forest->node_len : 1u, 1u);
    if (!state) {
        op_lang_set_status(status, CETTA_OP_LANG_V1_ALLOCATION_FAILURE);
        op_lang_set_error(error_buf, error_buf_size,
                          "failed to allocate forest validation state");
        return false;
    }
    if (!op_lang_unique_node(
            forest, root_index, state, &ambiguous,
            error_buf, error_buf_size)) {
        op_lang_set_status(status, CETTA_OP_LANG_V1_INTERNAL_FAILURE);
        goto done;
    }
    if (ambiguous) {
        op_lang_set_status(status, CETTA_OP_LANG_V1_AMBIGUOUS);
        op_lang_set_error(error_buf, error_buf_size,
                          "canonical source has multiple derivations");
        goto done;
    }
    if (!op_lang_eval_symbol(
            forest, root_index, bytes, byte_len, 0u, &value,
            error_buf, error_buf_size) ||
        value.kind != OP_LANG_VALUE_NODE) {
        op_lang_set_status(status, CETTA_OP_LANG_V1_INTERNAL_FAILURE);
        goto done;
    }
    *root = value.node;
    value.node = NULL;
    op_lang_receipt_copy(
        receipt, forest, *root,
        source_decode_pass_count, lexical_projection_pass_count);
    ok = true;

done:
    free(state);
    op_lang_value_free(&value);
    return ok;
}

static bool op_lang_decode_envelope(CettaOperationalLanguageDefV1 *language,
                                    CettaOpLangV1Status *status,
                                    char *error_buf,
                                    size_t error_buf_size) {
    CettaOpLangV1SExpr *root;
    CettaOpLangV1SExpr *name;

    if (!language || !(root = language->root) ||
        !cetta_op_lang_v1_application_is(
            root, "GSLTLanguageDefWireV1", 5u)) {
        goto malformed;
    }
    name = root->as.application.arguments[0];
    if (!name || name->kind != CETTA_OP_LANG_V1_SEXPR_STRING ||
        !op_lang_proper_list(root->as.application.arguments[1]) ||
        !op_lang_proper_list(root->as.application.arguments[2]) ||
        !op_lang_proper_list(root->as.application.arguments[3]) ||
        !op_lang_proper_list(root->as.application.arguments[4])) {
        goto malformed;
    }
    language->name_bytes = name->as.string.bytes;
    language->name_len = name->as.string.len;
    language->types_field = root->as.application.arguments[1];
    language->terms_field = root->as.application.arguments[2];
    language->equations_field = root->as.application.arguments[3];
    language->rewrites_field = root->as.application.arguments[4];
    return true;

malformed:
    op_lang_set_status(status, CETTA_OP_LANG_V1_MALFORMED_LANGUAGE_DEF);
    op_lang_set_error(
        error_buf, error_buf_size,
        "expected (GSLTLanguageDefWireV1 \"N\" types terms equations "
        "rewrites) with four canonical LNil/LCons lists");
    return false;
}

bool cetta_op_lang_v1_parse_document_bytes(
    CettaOpLangV1Document *out,
    const uint8_t *bytes,
    size_t byte_len,
    uint32_t gll_work_limit,
    uint32_t glr_work_limit,
    CettaOpLangV1Status *status,
    char *error_buf,
    size_t error_buf_size) {
    CettaOpLangV1Document candidate;
    CettaLpNativeGrammar grammar;
    CettaLpNativeGllPrepared gll;
    CettaLpNativeGlrPrepared glr;
    CettaLpNativeUtf8ScalarBuffer scalar_buffer;
    OpLangTokenLattice token_lattice;
    CettaLpNativeUtf8Forest gll_forest;
    CettaLpNativeUtf8Forest glr_forest;
    CettaOpLangV1SExpr *gll_root = NULL;
    CettaOpLangV1SExpr *glr_root = NULL;
    char parser_error[512] = {0};
    bool ok = false;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    op_lang_set_status(status, CETTA_OP_LANG_V1_OK);
    if (!out || (!bytes && byte_len > 0u) ||
        gll_work_limit == 0u || glr_work_limit == 0u) {
        op_lang_set_status(status, CETTA_OP_LANG_V1_BAD_ARGUMENT);
        op_lang_set_error(error_buf, error_buf_size,
                          "bad CettaTerm document parse arguments");
        return false;
    }
    cetta_op_lang_v1_document_init(&candidate);
    cetta_lp_native_grammar_init(&grammar);
    cetta_lp_native_gll_prepared_init(&gll);
    cetta_lp_native_glr_prepared_init(&glr);
    cetta_lp_native_utf8_scalar_buffer_init(&scalar_buffer);
    memset(&token_lattice, 0, sizeof(token_lattice));
    cetta_lp_native_utf8_forest_init(&gll_forest);
    cetta_lp_native_utf8_forest_init(&glr_forest);

    if (!op_lang_grammar_build(&grammar) ||
        !cetta_lp_native_gll_prepare(
            &gll, &grammar, OP_LANG_NT_DOCUMENT,
            parser_error, sizeof(parser_error)) ||
        !cetta_lp_native_glr_prepare(
            &glr, &grammar, OP_LANG_NT_DOCUMENT,
            parser_error, sizeof(parser_error))) {
        op_lang_set_status(status, CETTA_OP_LANG_V1_INTERNAL_FAILURE);
        op_lang_set_error(error_buf, error_buf_size, "%s",
                          parser_error[0] ? parser_error
                                          : "failed to prepare native parsers");
        goto done;
    }
    if (!cetta_lp_native_utf8_scalar_buffer_decode(
            &scalar_buffer, bytes, byte_len,
            parser_error, sizeof(parser_error))) {
        op_lang_set_status(status, CETTA_OP_LANG_V1_INVALID_UTF8);
        op_lang_set_error(error_buf, error_buf_size, "%s",
                          parser_error[0] ? parser_error
                                          : "invalid UTF-8 source");
        goto done;
    }
    if (!op_lang_token_lattice_build(
            &token_lattice, &scalar_buffer.view,
            parser_error, sizeof(parser_error))) {
        op_lang_set_status(status, CETTA_OP_LANG_V1_INTERNAL_FAILURE);
        op_lang_set_error(error_buf, error_buf_size, "%s",
                          parser_error[0] ? parser_error
                                          : "failed to project source tokens");
        goto done;
    }
    if (!cetta_lp_native_gll_prepared_parse_utf8_lattice_forest_from_complete(
            &gll, &token_lattice.lattice, 0u,
            gll_work_limit, &gll_forest,
            parser_error, sizeof(parser_error))) {
        op_lang_set_status(status, CETTA_OP_LANG_V1_INTERNAL_FAILURE);
        op_lang_set_error(error_buf, error_buf_size, "%s",
                          parser_error[0] ? parser_error
                                          : "native GLL failure");
        goto done;
    }
    if (gll_forest.outcome == CETTA_LP_NATIVE_UTF8_FOREST_RESOURCE_LIMIT) {
        op_lang_set_status(status, CETTA_OP_LANG_V1_GLL_RESOURCE_LIMIT);
        op_lang_set_error(error_buf, error_buf_size,
                          "native GLL work limit reached");
        goto done;
    }
    parser_error[0] = '\0';
    if (!cetta_lp_native_glr_prepared_parse_utf8_lattice_forest_from(
            &glr, &token_lattice.lattice, 0u,
            glr_work_limit, &glr_forest,
            parser_error, sizeof(parser_error))) {
        op_lang_set_status(status, CETTA_OP_LANG_V1_INTERNAL_FAILURE);
        op_lang_set_error(error_buf, error_buf_size, "%s",
                          parser_error[0] ? parser_error
                                          : "native GLR failure");
        goto done;
    }
    if (glr_forest.outcome == CETTA_LP_NATIVE_UTF8_FOREST_RESOURCE_LIMIT) {
        op_lang_set_status(status, CETTA_OP_LANG_V1_GLR_RESOURCE_LIMIT);
        op_lang_set_error(error_buf, error_buf_size,
                          "native GLR work limit reached");
        goto done;
    }
    if (!op_lang_forest_project(
            &gll_forest, bytes, byte_len, &gll_root, &candidate.gll,
            scalar_buffer.view.source_pass_count, 1u,
            status, error_buf, error_buf_size)) {
        goto done;
    }
    if (!op_lang_forest_project(
            &glr_forest, bytes, byte_len, &glr_root, &candidate.glr,
            scalar_buffer.view.source_pass_count, 1u,
            status, error_buf, error_buf_size)) {
        goto done;
    }
    if (!cetta_op_lang_v1_sexpr_equal(gll_root, glr_root) ||
        candidate.gll.derivation_fingerprint !=
            candidate.glr.derivation_fingerprint) {
        op_lang_set_status(status, CETTA_OP_LANG_V1_BACKEND_DISAGREEMENT);
        op_lang_set_error(error_buf, error_buf_size,
                          "native GLL and GLR projections disagree");
        goto done;
    }
    candidate.root = gll_root;
    gll_root = NULL;
    cetta_native_sha256_hex(bytes, byte_len, candidate.source_sha256);
    cetta_op_lang_v1_document_free(out);
    *out = candidate;
    memset(&candidate, 0, sizeof(candidate));
    op_lang_set_status(status, CETTA_OP_LANG_V1_OK);
    ok = true;

done:
    op_lang_sexpr_free(gll_root);
    op_lang_sexpr_free(glr_root);
    cetta_op_lang_v1_document_free(&candidate);
    cetta_lp_native_utf8_forest_free(&glr_forest);
    cetta_lp_native_utf8_forest_free(&gll_forest);
    op_lang_token_lattice_free(&token_lattice);
    cetta_lp_native_utf8_scalar_buffer_free(&scalar_buffer);
    cetta_lp_native_glr_prepared_free(&glr);
    cetta_lp_native_gll_prepared_free(&gll);
    cetta_lp_native_grammar_free(&grammar);
    return ok;
}

static bool op_lang_decode_document(
    CettaOperationalLanguageDefV1 *out,
    CettaOpLangV1Document *document,
    CettaOpLangV1Status *status,
    char *error_buf,
    size_t error_buf_size) {
    CettaOperationalLanguageDefV1 candidate;

    if (!out || !document || !document->root) {
        op_lang_set_status(status, CETTA_OP_LANG_V1_BAD_ARGUMENT);
        op_lang_set_error(error_buf, error_buf_size,
                          "bad LanguageDef document decode arguments");
        return false;
    }
    cetta_op_lang_v1_init(&candidate);
    candidate.root = document->root;
    document->root = NULL;
    memcpy(candidate.source_sha256, document->source_sha256,
           sizeof(candidate.source_sha256));
    candidate.gll = document->gll;
    candidate.glr = document->glr;
    if (!op_lang_decode_envelope(
            &candidate, status, error_buf, error_buf_size)) {
        cetta_op_lang_v1_free(&candidate);
        return false;
    }
    cetta_op_lang_v1_free(out);
    *out = candidate;
    memset(&candidate, 0, sizeof(candidate));
    op_lang_set_status(status, CETTA_OP_LANG_V1_OK);
    return true;
}

bool cetta_op_lang_v1_parse_bytes(
    CettaOperationalLanguageDefV1 *out,
    const uint8_t *bytes,
    size_t byte_len,
    uint32_t gll_work_limit,
    uint32_t glr_work_limit,
    CettaOpLangV1Status *status,
    char *error_buf,
    size_t error_buf_size) {
    CettaOpLangV1Document document;
    bool ok;

    if (!out) {
        if (error_buf && error_buf_size > 0u)
            error_buf[0] = '\0';
        op_lang_set_status(status, CETTA_OP_LANG_V1_BAD_ARGUMENT);
        op_lang_set_error(error_buf, error_buf_size,
                          "bad operational LanguageDef parse arguments");
        return false;
    }
    cetta_op_lang_v1_document_init(&document);
    ok = cetta_op_lang_v1_parse_document_bytes(
        &document, bytes, byte_len, gll_work_limit, glr_work_limit,
        status, error_buf, error_buf_size);
    if (ok) {
        ok = op_lang_decode_document(
            out, &document, status, error_buf, error_buf_size);
    }
    cetta_op_lang_v1_document_free(&document);
    return ok;
}

bool cetta_op_lang_v1_parse_document_file(
    CettaOpLangV1Document *out,
    const char *path,
    uint32_t gll_work_limit,
    uint32_t glr_work_limit,
    CettaOpLangV1Status *status,
    char *error_buf,
    size_t error_buf_size) {
    FILE *input;
    long length;
    uint8_t *bytes = NULL;
    bool ok = false;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!out || !path) {
        op_lang_set_status(status, CETTA_OP_LANG_V1_BAD_ARGUMENT);
        op_lang_set_error(error_buf, error_buf_size,
                          "bad CettaTerm document file arguments");
        return false;
    }
    input = fopen(path, "rb");
    if (!input) {
        op_lang_set_status(status, CETTA_OP_LANG_V1_IO_FAILURE);
        op_lang_set_error(error_buf, error_buf_size,
                          "cannot open CettaTerm document source");
        return false;
    }
    if (fseek(input, 0L, SEEK_END) != 0 ||
        (length = ftell(input)) < 0L ||
        (uintmax_t)length > SIZE_MAX ||
        fseek(input, 0L, SEEK_SET) != 0) {
        op_lang_set_status(status, CETTA_OP_LANG_V1_IO_FAILURE);
        op_lang_set_error(error_buf, error_buf_size,
                          "cannot size CettaTerm document source");
        goto done;
    }
    bytes = malloc(length > 0L ? (size_t)length : 1u);
    if (!bytes) {
        op_lang_set_status(status, CETTA_OP_LANG_V1_ALLOCATION_FAILURE);
        op_lang_set_error(error_buf, error_buf_size,
                          "cannot allocate CettaTerm document source");
        goto done;
    }
    if (length > 0L &&
        fread(bytes, 1u, (size_t)length, input) != (size_t)length) {
        op_lang_set_status(status, CETTA_OP_LANG_V1_IO_FAILURE);
        op_lang_set_error(error_buf, error_buf_size,
                          "cannot read CettaTerm document source");
        goto done;
    }
    ok = cetta_op_lang_v1_parse_document_bytes(
        out, bytes, (size_t)length, gll_work_limit, glr_work_limit,
        status, error_buf, error_buf_size);

done:
    free(bytes);
    (void)fclose(input);
    return ok;
}

bool cetta_op_lang_v1_parse_file(
    CettaOperationalLanguageDefV1 *out,
    const char *path,
    uint32_t gll_work_limit,
    uint32_t glr_work_limit,
    CettaOpLangV1Status *status,
    char *error_buf,
    size_t error_buf_size) {
    CettaOpLangV1Document document;
    bool ok;

    if (!out) {
        if (error_buf && error_buf_size > 0u)
            error_buf[0] = '\0';
        op_lang_set_status(status, CETTA_OP_LANG_V1_BAD_ARGUMENT);
        op_lang_set_error(error_buf, error_buf_size,
                          "bad operational LanguageDef file arguments");
        return false;
    }
    cetta_op_lang_v1_document_init(&document);
    ok = cetta_op_lang_v1_parse_document_file(
        &document, path, gll_work_limit, glr_work_limit,
        status, error_buf, error_buf_size);
    if (ok) {
        ok = op_lang_decode_document(
            out, &document, status, error_buf, error_buf_size);
    }
    cetta_op_lang_v1_document_free(&document);
    return ok;
}

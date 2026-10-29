#define _GNU_SOURCE
#include "parser.h"
#include "utf8.h"
#include "name_key.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef M_E
#define M_E 2.71828182845904523536
#endif

#ifndef CETTA_PARSE_DEPTH_LIMIT
#define CETTA_PARSE_DEPTH_LIMIT 4096
#endif

size_t parser_supported_expression_depth(void) {
    return CETTA_PARSE_DEPTH_LIMIT > 0 ? (size_t)CETTA_PARSE_DEPTH_LIMIT : 0u;
}

#ifndef CETTA_UNIVERSAL_NAME_MUTATION
#define CETTA_UNIVERSAL_NAME_MUTATION 0
#endif

#ifndef CETTA_SYNTAX_NOTATION_MUTATION
#define CETTA_SYNTAX_NOTATION_MUTATION 0
#endif

#ifndef CETTA_BARE_DOLLAR_DEFAULT_MODE
#define CETTA_BARE_DOLLAR_DEFAULT_MODE PARSER_BARE_DOLLAR_FRESH_VARIABLE
#endif

static __thread bool g_rational_literals_enabled = true;
static __thread bool g_runtime_data_symbols = false;
static __thread bool g_universal_name_syntax_enabled = false;
static __thread bool g_list_syntax_enabled = false;
static __thread bool g_namespace_sugar_enabled = true;
static __thread ParserBareDollarMode g_bare_dollar_mode =
    CETTA_BARE_DOLLAR_DEFAULT_MODE;
static ParserDocumentIdsBackend g_document_ids_backend;
static __thread bool g_document_ids_backend_active = false;

static SymbolId parser_intern_token(const char *text) {
    return g_runtime_data_symbols
        ? symbol_intern_data_cstr(g_symbols, text)
        : symbol_intern_cstr(g_symbols, text);
}

static Atom *parser_symbol_token(Arena *arena, const char *text) {
    return g_runtime_data_symbols ? atom_symbol_data(arena, text)
        : atom_symbol_id(arena, parser_intern_token(text));
}

static const ParserSyntaxFormSpec g_syntax_forms[] = {
    {PARSER_SYNTAX_QUOTE,
     CETTA_SYNTAX_NOTATION_MUTATION == 1 ? "^" : "@", "quote"},
    {PARSER_SYNTAX_UNQUOTE,
     CETTA_SYNTAX_NOTATION_MUTATION == 2 ? "^" : "*", "unquote"},
    {PARSER_SYNTAX_VAR, "$",
     CETTA_SYNTAX_NOTATION_MUTATION == 3 ? "syn:bad-var" : "syn:var"},
    {PARSER_SYNTAX_REF, "&",
     CETTA_SYNTAX_NOTATION_MUTATION == 4 ? "syn:bad-ref" : "syn:ref"},
    {PARSER_SYNTAX_EXEC, "!",
     CETTA_SYNTAX_NOTATION_MUTATION == 5 ? "syn:bad-exec" : "syn:exec"},
};

const ParserSyntaxFormSpec *parser_syntax_form_specs(size_t *count_out) {
    if (count_out)
        *count_out = sizeof(g_syntax_forms) / sizeof(g_syntax_forms[0]);
    return g_syntax_forms;
}

static const ParserSyntaxFormSpec *parser_syntax_form(
    ParserSyntaxFormKind kind) {
    for (size_t i = 0; i < sizeof(g_syntax_forms) / sizeof(g_syntax_forms[0]);
         i++) {
        if (g_syntax_forms[i].kind == kind)
            return &g_syntax_forms[i];
    }
    return NULL;
}

static char parser_syntax_compact_char(ParserSyntaxFormKind kind) {
    const ParserSyntaxFormSpec *spec = parser_syntax_form(kind);
    return spec && spec->compact ? spec->compact[0] : '\0';
}

static SymbolId parser_syntax_expanded_id(ParserSyntaxFormKind kind) {
    const ParserSyntaxFormSpec *spec = parser_syntax_form(kind);
    return spec && spec->expanded
        ? symbol_intern_cstr(g_symbols, spec->expanded)
        : SYMBOL_ID_NONE;
}

static bool parser_symbol_is_syntax_form(SymbolId symbol,
                                         ParserSyntaxFormKind kind) {
    const ParserSyntaxFormSpec *spec = parser_syntax_form(kind);
    return spec && spec->expanded &&
           symbol_eq_cstr(g_symbols, symbol, spec->expanded);
}

bool parser_set_rational_literals_enabled(bool enabled) {
    bool old = g_rational_literals_enabled;
    g_rational_literals_enabled = enabled;
    return old;
}

bool parser_rational_literals_enabled(void) {
    return g_rational_literals_enabled;
}

bool parser_set_namespace_sugar_enabled(bool enabled) {
    bool old = g_namespace_sugar_enabled;
    g_namespace_sugar_enabled = enabled;
    return old;
}

bool parser_namespace_sugar_enabled(void) {
    return g_namespace_sugar_enabled;
}

bool parser_set_universal_name_syntax_enabled(bool enabled) {
    bool old = g_universal_name_syntax_enabled;
    g_universal_name_syntax_enabled = enabled;
    return old;
}

bool parser_universal_name_syntax_enabled(void) {
    return g_universal_name_syntax_enabled;
}

bool parser_set_list_syntax_enabled(bool enabled) {
    bool old = g_list_syntax_enabled;
    g_list_syntax_enabled = enabled;
    return old;
}

bool parser_list_syntax_enabled(void) {
    return g_list_syntax_enabled;
}

ParserBareDollarMode parser_set_bare_dollar_mode(
    ParserBareDollarMode mode) {
    ParserBareDollarMode old = g_bare_dollar_mode;
    g_bare_dollar_mode = mode;
    return old;
}

ParserBareDollarMode parser_bare_dollar_mode(void) {
    return g_bare_dollar_mode;
}

bool parser_set_document_ids_backend(
    const ParserDocumentIdsBackend *backend) {
    if (!backend || !backend->context || !backend->parse_text_ids ||
        !backend->parse_file_ids || g_document_ids_backend.context) {
        return false;
    }
    g_document_ids_backend = *backend;
    return true;
}

void parser_clear_document_ids_backend(void *context) {
    if (context && g_document_ids_backend.context == context)
        memset(&g_document_ids_backend, 0, sizeof(g_document_ids_backend));
}

/* The installed reader is not reentered: while it runs, or while a caller
 * asks for the built-in reader, the built-in reader parses. */
bool parser_set_builtin_document_reader(bool builtin) {
    bool old = g_document_ids_backend_active;
    g_document_ids_backend_active = builtin;
    return old;
}

Atom *parser_read_document_term_n(Arena *a, const char *text, size_t text_len,
                                  bool *handled) {
    *handled = false;
    if (!a || !text || !g_document_ids_backend.parse_term_bytes_id ||
        g_document_ids_backend_active)
        return NULL;
    *handled = true;
    Arena persistent;
    arena_init(&persistent);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);
    AtomId id = CETTA_ATOM_ID_NONE;
    g_document_ids_backend_active = true;
    int status = g_document_ids_backend.parse_term_bytes_id(
        g_document_ids_backend.context, (const uint8_t *)text, text_len,
        &universe, &id, NULL, 0u);
    g_document_ids_backend_active = false;
    Atom *term = status == 0 && id != CETTA_ATOM_ID_NONE
        ? term_universe_copy_atom(&universe, a, id) : NULL;
    term_universe_free(&universe);
    arena_free(&persistent);
    return term;
}

/* ── Helpers ────────────────────────────────────────────────────────────── */

/* The scanners read text_len bytes of text, which may hold NUL and is
 * followed by a NUL byte; PARSER_TEXT_NUL_TERMINATED reads up to the first
 * NUL instead. */
#define PARSER_TEXT_NUL_TERMINATED SIZE_MAX

static inline bool parser_more(const char *text, size_t text_len, size_t pos) {
    return text_len == PARSER_TEXT_NUL_TERMINATED ? text[pos] != '\0'
                                                  : pos < text_len;
}

static void skip_whitespace_and_comments(const char *text, size_t text_len,
                                         size_t *pos) {
    for (;;) {
        while (parser_more(text, text_len, *pos) &&
               isspace((unsigned char)text[*pos]))
            (*pos)++;
        if (parser_more(text, text_len, *pos) && text[*pos] == ';') {
            while (parser_more(text, text_len, *pos) && text[*pos] != '\n')
                (*pos)++;
        } else {
            break;
        }
    }
}

static bool is_token_char(char c) {
    return c && !isspace((unsigned char)c) && c != '(' && c != ')' && c != ';' && c != '"';
}

/* Inside a list a token also ends at a bracket, and in Prime's universal
 * name syntax at a brace, which opens or closes a braces node. */
static bool is_list_token_char(char c) {
    return is_token_char(c) && c != '[' && c != ']' &&
           !(g_universal_name_syntax_enabled && (c == '{' || c == '}'));
}

/* The bar alone, which marks a list's rest; a token may start with it. */
static bool parser_at_list_bar(const char *text, size_t text_len, size_t pos) {
    return text[pos] == '|' &&
        (!parser_more(text, text_len, pos + 1u) || !is_list_token_char(text[pos + 1u]));
}

static bool is_token_char_in(char c, bool in_list) {
    return in_list ? is_list_token_char(c) : is_token_char(c);
}

static bool namespace_segment_start_char(char c) {
    return isalpha((unsigned char)c) || c == '_';
}

static bool namespace_segment_char(char c) {
    return isalnum((unsigned char)c) || c == '-' || c == '_' || c == '!' || c == '?';
}

static bool namespace_token_has_file_extension(const char *tok) {
    const char *dot = strrchr(tok, '.');
    if (!dot || dot == tok || dot[1] == '\0') return false;
    const char *ext = dot + 1;
    return strcmp(ext, "metta") == 0 ||
           strcmp(ext, "mm2") == 0 ||
           strcmp(ext, "act") == 0;
}

static bool namespace_token_looks_qualified(const char *tok, char separator) {
    if (!tok || !*tok || tok[0] == separator || !strchr(tok, separator)) {
        return false;
    }

    bool at_segment_start = true;
    for (const char *p = tok; *p; p++) {
        if (*p == separator) {
            if (at_segment_start || p[1] == '\0' || p[1] == separator) {
                return false;
            }
            at_segment_start = true;
            continue;
        }
        if (at_segment_start) {
            if (!namespace_segment_start_char(*p)) {
                return false;
            }
            at_segment_start = false;
            continue;
        }
        if (!namespace_segment_char(*p)) {
            return false;
        }
    }
    return !at_segment_start;
}

static const char *parser_namespace_token(Arena *a, const char *tok) {
    if (!tok || !*tok || !strchr(tok, '.'))
        return tok;
    if (strchr(tok, '/') || tok[0] == '.' || namespace_token_has_file_extension(tok))
        return tok;
    if (!namespace_token_looks_qualified(tok, '.'))
        return tok;

    size_t len = strlen(tok);
    char *canonical = arena_alloc(a, len + 1);
    for (size_t i = 0; i < len; i++)
        canonical[i] = (tok[i] == '.') ? ':' : tok[i];
    canonical[len] = '\0';
    return canonical;
}

const char *parser_canonicalize_namespace_token(Arena *a, const char *tok) {
    return g_namespace_sugar_enabled ? parser_namespace_token(a, tok) : tok;
}

static char decode_string_escape(char c) {
    switch (c) {
        case 'n': return '\n';
        case 'r': return '\r';
        case 't': return '\t';
        case '"': return '"';
        case '\\': return '\\';
        default: return c;
    }
}

static int parser_hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* The bytes a string literal's body denotes: \xhh is one byte, NUL
 * included, and \u{h...} is its scalar in UTF-8.  Every other escape keeps
 * its reading above.  The result is never longer than the body. */
static size_t parser_decode_string_body(const char *text, size_t start,
                                        size_t end, char *buf) {
    size_t out = 0;
    for (size_t i = start; i < end; i++) {
        if (text[i] != '\\' || i + 1 >= end) {
            buf[out++] = text[i];
            continue;
        }
        char kind = text[i + 1];
        if (kind == 'x' && i + 3 < end) {
            int high = parser_hex_digit(text[i + 2]);
            int low = parser_hex_digit(text[i + 3]);
            if (high >= 0 && low >= 0) {
                buf[out++] = (char)(uint8_t)((high << 4) | low);
                i += 3;
                continue;
            }
        }
        if (kind == 'u' && i + 2 < end && text[i + 2] == '{') {
            size_t j = i + 3;
            uint32_t scalar = 0;
            size_t digits = 0;
            while (j < end && digits < 6) {
                int digit = parser_hex_digit(text[j]);
                if (digit < 0)
                    break;
                scalar = (scalar << 4) | (uint32_t)digit;
                digits++;
                j++;
            }
            if (digits > 0 && j < end && text[j] == '}' &&
                cetta_utf8_scalar_valid(scalar)) {
                uint8_t encoded[4];
                size_t width = cetta_utf8_encode(scalar, encoded);
                memcpy(buf + out, encoded, width);
                out += width;
                i = j;
                continue;
            }
        }
        buf[out++] = decode_string_escape(kind);
        i++;
    }
    buf[out] = '\0';
    return out;
}

bool parser_text_well_formed_n(const char *text, size_t text_len) {
    int depth = 0;
    for (size_t i = 0; parser_more(text, text_len, i); i++) {
        if (text[i] == ';') {
            while (parser_more(text, text_len, i) && text[i] != '\n') i++;
            if (!parser_more(text, text_len, i)) break;
            continue;
        }
        if (text[i] == '"') {
            i++;
            while (parser_more(text, text_len, i) && text[i] != '"') {
                if (text[i] == '\\' && parser_more(text, text_len, i + 1)) i++;
                i++;
            }
            if (!parser_more(text, text_len, i)) return false;
            continue;
        }
        if (text[i] == '(') {
            depth++;
            if (depth > CETTA_PARSE_DEPTH_LIMIT)
                return false;
            continue;
        }
        if (text[i] == ')') {
            depth--;
            if (depth < 0) return false;
        }
    }
    return depth == 0;
}

bool parser_text_well_formed(const char *text) {
    return parser_text_well_formed_n(text, PARSER_TEXT_NUL_TERMINATED);
}

bool parser_rest_is_delimiters_n(const char *text, size_t text_len,
                                 size_t *pos) {
    skip_whitespace_and_comments(text, text_len, pos);
    return !parser_more(text, text_len, *pos);
}

bool parser_rest_is_delimiters(const char *text, size_t *pos) {
    return parser_rest_is_delimiters_n(text, PARSER_TEXT_NUL_TERMINATED, pos);
}

typedef struct {
    SymbolId *spellings;
    VarId *ids;
    uint32_t len;
    uint32_t cap;
    NameKeyTable *name_keys;
    NameId *names;
    VarId *name_ids;
    uint32_t name_len;
    uint32_t name_cap;
} ParserVarScope;

struct ParserHostProjectionV1 {
    TermUniverse *universe;
    Arena scratch;
    ArenaMark scratch_root;
    ParserVarScope scope;
    bool form_active;
    bool namespace_sugar;
};

static void parser_var_scope_init(ParserVarScope *scope) {
    scope->spellings = NULL;
    scope->ids = NULL;
    scope->len = 0;
    scope->cap = 0;
    scope->name_keys = NULL;
    scope->names = NULL;
    scope->name_ids = NULL;
    scope->name_len = 0;
    scope->name_cap = 0;
}

static void parser_var_scope_free(ParserVarScope *scope) {
    free(scope->spellings);
    free(scope->ids);
    name_key_table_delete(scope->name_keys);
    free(scope->names);
    free(scope->name_ids);
    scope->spellings = NULL;
    scope->ids = NULL;
    scope->len = 0;
    scope->cap = 0;
    scope->name_keys = NULL;
    scope->names = NULL;
    scope->name_ids = NULL;
    scope->name_len = 0;
    scope->name_cap = 0;
}

static VarId parser_var_scope_id(ParserVarScope *scope, SymbolId spelling) {
    for (uint32_t i = 0; i < scope->len; i++) {
        if (scope->spellings[i] == spelling)
            return scope->ids[i];
    }
    if (scope->len >= scope->cap) {
        scope->cap = scope->cap ? scope->cap * 2 : 8;
        scope->spellings = cetta_realloc(scope->spellings, sizeof(SymbolId) * scope->cap);
        scope->ids = cetta_realloc(scope->ids, sizeof(VarId) * scope->cap);
    }
    VarId id = fresh_var_id();
    scope->spellings[scope->len] = spelling;
    scope->ids[scope->len] = id;
    scope->len++;
    return id;
}

static bool parser_bare_dollar_is_variable(void) {
    return g_universal_name_syntax_enabled &&
           g_bare_dollar_mode != PARSER_BARE_DOLLAR_SYMBOL;
}

static VarId parser_bare_dollar_id(ParserVarScope *scope,
                                   SymbolId spelling) {
    return g_bare_dollar_mode == PARSER_BARE_DOLLAR_SHARED_VARIABLE
               ? parser_var_scope_id(scope, spelling)
               : fresh_var_id();
}

static NameId parser_var_scope_name_key_id(ParserVarScope *scope,
                                           Atom *name_key) {
    if (!scope || !name_key) return NAME_ID_NONE;
    if (!scope->name_keys)
        scope->name_keys = name_key_table_new(32u);
    if (!scope->name_keys) return NAME_ID_NONE;
    return name_key_intern(scope->name_keys, name_key);
}

static VarId parser_var_scope_name_id(ParserVarScope *scope, Atom *name_key) {
    NameId name = parser_var_scope_name_key_id(scope, name_key);
    if (name == NAME_ID_NONE) return VAR_ID_NONE;
    if (CETTA_UNIVERSAL_NAME_MUTATION == 1 && scope->name_len > 0u)
        return scope->name_ids[0];
    for (uint32_t i = 0; i < scope->name_len; i++) {
#if CETTA_SYNTAX_NOTATION_MUTATION != 6
        if (scope->names[i] == name)
            return scope->name_ids[i];
#endif
    }
    if (scope->name_len >= scope->name_cap) {
        scope->name_cap = scope->name_cap ? scope->name_cap * 2u : 8u;
        scope->names = cetta_realloc(
            scope->names, sizeof(*scope->names) * scope->name_cap);
        scope->name_ids = cetta_realloc(
            scope->name_ids, sizeof(*scope->name_ids) * scope->name_cap);
    }
    VarId id = fresh_var_id();
    scope->names[scope->name_len] = name;
    scope->name_ids[scope->name_len] = id;
    scope->name_len++;
    return id;
}

static bool parser_quote_payload(Atom *form, Atom **payload_out) {
    if (payload_out) *payload_out = NULL;
    if (!form || form->kind != ATOM_EXPR || form->expr.len != 2u ||
        !atom_is_symbol_id(form->expr.elems[0], g_builtin_syms.quote)) {
        return false;
    }
    if (payload_out) *payload_out = form->expr.elems[1];
    return true;
}

/* The long forms preserve the two existing identity tiers:
   (syn:var x) / (syn:ref x) use legacy symbol spellings, while
   (syn:var (quote e)) / (syn:ref (quote e)) use closed structural names.
   (syn:exec e) remains data for the loader, but its arity is checked here.
   syn:syntactic-equal lowers to noreduce-eq so both names have exactly one
   evaluation-control implementation. */
static Atom *parser_lower_syn_expression(Arena *a, Atom *expr,
                                         ParserVarScope *scope) {
    if (!g_universal_name_syntax_enabled || !expr ||
        expr->kind != ATOM_EXPR || expr->expr.len == 0u ||
        expr->expr.elems[0]->kind != ATOM_SYMBOL) {
        return expr;
    }

    SymbolId head = atom_symbol_current_id(expr->expr.elems[0]);
    bool is_var = parser_symbol_is_syntax_form(head, PARSER_SYNTAX_VAR);
    bool is_ref = parser_symbol_is_syntax_form(head, PARSER_SYNTAX_REF);
    bool is_exec = parser_symbol_is_syntax_form(head, PARSER_SYNTAX_EXEC);
    bool is_syntactic_equal =
        symbol_eq_cstr(g_symbols, head, "syn:syntactic-equal");
    if (!is_var && !is_ref && !is_exec && !is_syntactic_equal)
        return expr;
    if (is_exec)
        return expr->expr.len == 2u ? expr : NULL;
    if (is_syntactic_equal) {
        if (expr->expr.len != 3u)
            return NULL;
        return atom_expr3(a, atom_symbol(a, "noreduce-eq"),
                          expr->expr.elems[1], expr->expr.elems[2]);
    }
    if (expr->expr.len != 2u)
        return NULL;

    Atom *arg = expr->expr.elems[1];
    if (is_var) {
        if (arg->kind == ATOM_SYMBOL) {
            VarId id = parser_var_scope_id(scope, atom_symbol_current_id(arg));
            return atom_var_with_spelling(a, atom_symbol_current_id(arg), id);
        }
        Atom *key = NULL;
        if (parser_quote_payload(arg, &key)) {
            VarId id = parser_var_scope_name_id(scope, key);
            if (!var_id_is_none(id))
                return atom_var_with_name_key(a, key, id);
        }
        return NULL;
    }

    if (is_ref) {
        if (arg->kind == ATOM_SYMBOL) {
            const char *name = atom_name_cstr(arg);
            size_t len = strlen(name);
            char *legacy = arena_alloc(a, len + 2u);
            legacy[0] = '&';
            memcpy(legacy + 1u, name, len + 1u);
            return parser_symbol_token(a, legacy);
        }
        Atom *key = NULL;
        if (parser_quote_payload(arg, &key) &&
            parser_var_scope_name_key_id(scope, key) != NAME_ID_NONE) {
            return atom_expr2(
                a, atom_symbol(a, "resolve-name"), arg);
        }
        return NULL;
    }
    return NULL;
}

static bool parser_quote_payload_id(const TermUniverse *universe, AtomId form,
                                    AtomId *payload_out) {
    if (payload_out) *payload_out = CETTA_ATOM_ID_NONE;
    if (tu_kind(universe, form) != ATOM_EXPR ||
        tu_arity(universe, form) != 2u) {
        return false;
    }
    AtomId head = tu_child(universe, form, 0u);
    if (tu_kind(universe, head) != ATOM_SYMBOL ||
        tu_sym(universe, head) != g_builtin_syms.quote) {
        return false;
    }
    if (payload_out) *payload_out = tu_child(universe, form, 1u);
    return true;
}

typedef enum {
    PARSER_SYN_LOWER_NOT_FORM,
    PARSER_SYN_LOWER_OK,
    PARSER_SYN_LOWER_INVALID,
} ParserSynLowerStatus;

static ParserSynLowerStatus parser_lower_syn_expression_id(
    TermUniverse *universe, Arena *scratch, const AtomId *children,
    CettaExprLen arity, ParserVarScope *scope, AtomId *lowered_out) {
    if (lowered_out) *lowered_out = CETTA_ATOM_ID_NONE;
    if (!g_universal_name_syntax_enabled || arity == 0u ||
        tu_kind(universe, children[0]) != ATOM_SYMBOL) {
        return PARSER_SYN_LOWER_NOT_FORM;
    }

    SymbolId head = tu_sym(universe, children[0]);
    bool is_var = parser_symbol_is_syntax_form(head, PARSER_SYNTAX_VAR);
    bool is_ref = parser_symbol_is_syntax_form(head, PARSER_SYNTAX_REF);
    bool is_exec = parser_symbol_is_syntax_form(head, PARSER_SYNTAX_EXEC);
    bool is_syntactic_equal =
        symbol_eq_cstr(g_symbols, head, "syn:syntactic-equal");
    if (!is_var && !is_ref && !is_exec && !is_syntactic_equal)
        return PARSER_SYN_LOWER_NOT_FORM;
    if (is_exec) {
        if (arity != 2u)
            return PARSER_SYN_LOWER_INVALID;
        if (lowered_out)
            *lowered_out = tu_expr_from_ids(universe, children, arity);
        return PARSER_SYN_LOWER_OK;
    }
    if (is_syntactic_equal) {
        if (arity != 3u)
            return PARSER_SYN_LOWER_INVALID;
        AtomId syntax_children[3] = {
            tu_intern_symbol(
                universe, symbol_intern_cstr(g_symbols, "noreduce-eq")),
            children[1], children[2],
        };
        if (lowered_out)
            *lowered_out = tu_expr_from_ids(universe, syntax_children, 3u);
        return PARSER_SYN_LOWER_OK;
    }
    if (arity != 2u)
        return PARSER_SYN_LOWER_INVALID;

    AtomId arg = children[1];
    if (is_var) {
        if (tu_kind(universe, arg) == ATOM_SYMBOL) {
            SymbolId spelling = tu_sym(universe, arg);
            VarId id = parser_var_scope_id(scope, spelling);
            if (lowered_out)
                *lowered_out = tu_intern_var(universe, spelling, id);
            return PARSER_SYN_LOWER_OK;
        }
        AtomId key_id = CETTA_ATOM_ID_NONE;
        if (parser_quote_payload_id(universe, arg, &key_id)) {
            Atom *key = term_universe_copy_atom(universe, scratch, key_id);
            VarId id = key ? parser_var_scope_name_id(scope, key) : VAR_ID_NONE;
            if (!var_id_is_none(id)) {
                if (lowered_out)
                    *lowered_out = tu_intern_named_var(universe, key_id, id);
                return PARSER_SYN_LOWER_OK;
            }
        }
        return PARSER_SYN_LOWER_INVALID;
    }

    if (is_ref) {
        if (tu_kind(universe, arg) == ATOM_SYMBOL) {
            const char *name = symbol_bytes(g_symbols, tu_sym(universe, arg));
            size_t len = strlen(name);
            char *legacy = arena_alloc(scratch, len + 2u);
            legacy[0] = '&';
            memcpy(legacy + 1u, name, len + 1u);
            if (lowered_out) {
                *lowered_out = tu_intern_symbol(
                    universe, symbol_intern_cstr(g_symbols, legacy));
            }
            return PARSER_SYN_LOWER_OK;
        }
        AtomId key_id = CETTA_ATOM_ID_NONE;
        if (parser_quote_payload_id(universe, arg, &key_id)) {
            Atom *key = term_universe_copy_atom(universe, scratch, key_id);
            if (key && parser_var_scope_name_key_id(scope, key) != NAME_ID_NONE) {
                AtomId head_id = tu_intern_symbol(
                    universe, symbol_intern_cstr(g_symbols, "resolve-name"));
                AtomId ref_children[2] = {head_id, arg};
                if (lowered_out)
                    *lowered_out = tu_expr_from_ids(universe, ref_children, 2u);
                return PARSER_SYN_LOWER_OK;
            }
        }
        return PARSER_SYN_LOWER_INVALID;
    }
    return PARSER_SYN_LOWER_INVALID;
}

typedef enum {
    PARSER_NAMED_LITERAL_NONE,
    PARSER_NAMED_LITERAL_TRUE,
    PARSER_NAMED_LITERAL_FALSE,
    PARSER_NAMED_LITERAL_PI,
    PARSER_NAMED_LITERAL_EXP,
} ParserNamedLiteral;

/* Both readers classify the same bounded token before selecting their atom
 * or universe representation. The lexer already supplies its exact length. */
static ParserNamedLiteral parser_named_literal(const char *token, size_t length) {
    switch (length) {
    case 2u:
        if (memcmp(token, "PI", 2u) == 0)
            return PARSER_NAMED_LITERAL_PI;
        break;
    case 3u:
        if (memcmp(token, "EXP", 3u) == 0)
            return PARSER_NAMED_LITERAL_EXP;
        break;
    case 4u:
        if (memcmp(token, "True", 4u) == 0)
            return PARSER_NAMED_LITERAL_TRUE;
        break;
    case 5u:
        if (memcmp(token, "False", 5u) == 0)
            return PARSER_NAMED_LITERAL_FALSE;
        break;
    }
    return PARSER_NAMED_LITERAL_NONE;
}

typedef struct {
    bool rational_literals;
    bool namespace_sugar;
} ParserWordPolicy;

/* A NULL universe selects arena construction; otherwise the word is born
 * directly in that universe. Neither target renders or reparses a value. */
typedef union {
    Atom *atom;
    AtomId id;
} ParserWordValue;

__attribute__((always_inline))
static inline ParserWordValue parser_word_bool(Arena *a, TermUniverse *universe,
                                       bool value) {
    return universe ? (ParserWordValue){.id = tu_intern_bool(universe, value)}
                    : (ParserWordValue){.atom = atom_bool(a, value)};
}

__attribute__((always_inline))
static inline ParserWordValue parser_word_int(Arena *a, TermUniverse *universe,
                                      int64_t value) {
    return universe ? (ParserWordValue){.id = tu_intern_int(universe, value)}
                    : (ParserWordValue){.atom = atom_int(a, value)};
}

__attribute__((always_inline))
static inline ParserWordValue parser_word_float(Arena *a, TermUniverse *universe,
                                        double value) {
    return universe ? (ParserWordValue){.id = tu_intern_float(universe, value)}
                    : (ParserWordValue){.atom = atom_float(a, value)};
}

__attribute__((always_inline))
static inline ParserWordValue parser_word_exact(Arena *a, TermUniverse *universe,
                                        const char *tok, bool rational) {
    if (universe) {
        return (ParserWordValue){.id = rational
            ? tu_intern_rational(universe, tok)
            : tu_intern_bigint(universe, tok)};
    }
    return (ParserWordValue){.atom = rational ? atom_rational(a, tok)
                                            : atom_bigint(a, tok)};
}

/* The shared word-grounding decision path. Delimiters, strings, variable
 * identity and token bindings belong to the surrounding reader. Inline the
 * target dispatch so parser-only bootstrap tools retain arena construction
 * without acquiring term-universe constructor dependencies. */
__attribute__((always_inline))
static inline ParserWordValue parser_ground_word(Arena *a, TermUniverse *universe,
                                          const char *tok, size_t length,
                                          ParserWordPolicy policy) {
    if (!a || !tok || !*tok) {
        return universe ? (ParserWordValue){.id = CETTA_ATOM_ID_NONE}
                        : (ParserWordValue){.atom = NULL};
    }
    switch (parser_named_literal(tok, length)) {
    case PARSER_NAMED_LITERAL_TRUE: return parser_word_bool(a, universe, true);
    case PARSER_NAMED_LITERAL_FALSE: return parser_word_bool(a, universe, false);
    case PARSER_NAMED_LITERAL_PI: return parser_word_float(a, universe, M_PI);
    case PARSER_NAMED_LITERAL_EXP: return parser_word_float(a, universe, M_E);
    case PARSER_NAMED_LITERAL_NONE: break;
    }

    char *endp;
    errno = 0;
    long long val = strtoll(tok, &endp, 10);
    if (*endp == '\0' && errno == 0)
        return parser_word_int(a, universe, (int64_t)val);

    bool has_dot = strchr(tok, '.') != NULL;
    if (!has_dot) {
        if (policy.rational_literals && strchr(tok, '/')) {
            ParserWordValue rational = parser_word_exact(a, universe, tok, true);
            if (universe ? rational.id != CETTA_ATOM_ID_NONE
                         : rational.atom != NULL)
                return rational;
        }
        char *canonical = cetta_bigint_canonicalize_owned(tok);
        if (canonical) {
            free(canonical);
            return parser_word_exact(a, universe, tok, false);
        }
    } else {
        char *fendp;
        /* Preserve the dot requirement and accept the nearest float even
         * when strtod reports ERANGE (zero, subnormal or infinity). */
        double fval = strtod(tok, &fendp);
        if (*fendp == '\0')
            return parser_word_float(a, universe, fval);
    }

    const char *canonical = policy.namespace_sugar
        ? parser_namespace_token(a, tok) : tok;
    if (universe) {
        SymbolId spelling = symbol_intern_cstr(g_symbols, canonical);
        return (ParserWordValue){.id = tu_intern_symbol(universe, spelling)};
    }
    return (ParserWordValue){.atom = parser_symbol_token(a, canonical)};
}

static AtomId parser_project_word_id(TermUniverse *universe, Arena *scratch,
                                     const char *tok, size_t length,
                                     bool namespace_sugar) {
    if (!universe)
        return CETTA_ATOM_ID_NONE;
    ParserWordPolicy policy = {g_rational_literals_enabled, namespace_sugar};
    return parser_ground_word(scratch, universe, tok, length, policy).id;
}

ParserHostProjectionV1 *parser_host_projection_v1_new(
    TermUniverse *universe) {
    ParserHostProjectionV1 *projection;

    if (!universe || !g_symbols)
        return NULL;
    projection = calloc(1u, sizeof(*projection));
    if (!projection)
        return NULL;
    projection->universe = universe;
    projection->namespace_sugar = g_namespace_sugar_enabled;
    arena_init(&projection->scratch);
    arena_set_hashcons(&projection->scratch, NULL);
    /* The root mark holds the first block, so starting a form keeps it
     * instead of claiming a block for every form. */
    arena_reserve(&projection->scratch, 1u);
    projection->scratch_root = arena_mark(&projection->scratch);
    parser_var_scope_init(&projection->scope);
    return projection;
}

void parser_host_projection_v1_set_namespace_sugar(
    ParserHostProjectionV1 *projection, bool enabled) {
    if (projection)
        projection->namespace_sugar = enabled;
}

void parser_host_projection_v1_free(ParserHostProjectionV1 *projection) {
    if (!projection)
        return;
    parser_var_scope_free(&projection->scope);
    arena_free(&projection->scratch);
    free(projection);
}

bool parser_host_projection_v1_begin_form(
    ParserHostProjectionV1 *projection) {
    if (!projection || !projection->universe || !g_symbols)
        return false;
    parser_var_scope_free(&projection->scope);
    parser_var_scope_init(&projection->scope);
    arena_reset(&projection->scratch, projection->scratch_root);
    projection->form_active = true;
    return true;
}

static char *parser_host_projection_v1_cstr(
    ParserHostProjectionV1 *projection, const uint8_t *bytes, size_t len,
    bool require_nonempty) {
    char *text;

    if (!projection || !projection->form_active ||
        (require_nonempty && len == 0u) || (len > 0u && !bytes) ||
        (len > 0u && memchr(bytes, '\0', len))) {
        return NULL;
    }
    text = arena_alloc(&projection->scratch, len + 1u);
    if (len > 0u)
        memcpy(text, bytes, len);
    text[len] = '\0';
    return text;
}

AtomId parser_host_projection_v1_word_bytes(
    ParserHostProjectionV1 *projection, const uint8_t *bytes, size_t len) {
    char *text = parser_host_projection_v1_cstr(
        projection, bytes, len, true);
    return text ? parser_project_word_id(
                      projection->universe, &projection->scratch, text, len,
                      projection->namespace_sugar)
                : CETTA_ATOM_ID_NONE;
}

AtomId parser_host_projection_v1_variable_bytes(
    ParserHostProjectionV1 *projection, const uint8_t *bytes, size_t len) {
    char *text = parser_host_projection_v1_cstr(
        projection, bytes, len, true);
    const char *canonical;
    SymbolId spelling;

    if (!text)
        return CETTA_ATOM_ID_NONE;
    canonical = projection->namespace_sugar
        ? parser_namespace_token(&projection->scratch, text) : text;
    spelling = symbol_intern_cstr(g_symbols, canonical);
    return tu_intern_var(
        projection->universe, spelling,
        parser_var_scope_id(&projection->scope, spelling));
}

AtomId parser_host_projection_v1_anonymous_variable(
    ParserHostProjectionV1 *projection) {
    SymbolId spelling;
    if (!projection || !projection->form_active || !projection->universe ||
        !g_symbols) {
        return CETTA_ATOM_ID_NONE;
    }
    spelling = symbol_intern_cstr(g_symbols, "");
    return tu_intern_var(projection->universe, spelling, fresh_var_id());
}

/* A string keeps its bytes exactly, embedded NUL included; words and
 * variables are symbols and cannot hold NUL. */
AtomId parser_host_projection_v1_string_bytes(
    ParserHostProjectionV1 *projection, const uint8_t *bytes, size_t len) {
    if (!projection || !projection->form_active || (len > 0u && !bytes))
        return CETTA_ATOM_ID_NONE;
    return tu_intern_string_n(projection->universe,
                              len > 0u ? (const char *)bytes : "", len);
}

AtomId parser_host_projection_v1_expression(
    ParserHostProjectionV1 *projection, const AtomId *children,
    CettaExprLen arity) {
    AtomId result = CETTA_ATOM_ID_NONE;
    ParserSynLowerStatus lowered;

    if (!projection || !projection->form_active ||
        (arity > 0u && !children)) {
        return CETTA_ATOM_ID_NONE;
    }
    lowered = parser_lower_syn_expression_id(
        projection->universe, &projection->scratch, children, arity,
        &projection->scope, &result);
    if (lowered == PARSER_SYN_LOWER_NOT_FORM)
        return tu_expr_from_ids(projection->universe, children, arity);
    if (lowered == PARSER_SYN_LOWER_INVALID)
        return CETTA_ATOM_ID_NONE;
    return result;
}

AtomId parser_host_projection_v1_list(
    ParserHostProjectionV1 *projection, const AtomId *elems,
    CettaExprLen elem_len, AtomId rest) {
    if (!projection || !projection->form_active)
        return CETTA_ATOM_ID_NONE;
    return tu_list_from_ids(projection->universe, elems, elem_len, rest);
}

AtomId parser_host_projection_v1_braces(
    ParserHostProjectionV1 *projection, const AtomId *children,
    CettaExprLen child_len) {
    if (!projection || !projection->form_active)
        return CETTA_ATOM_ID_NONE;
    return tu_braces_from_ids(projection->universe, children, child_len);
}

static AtomId parser_project_atom_leaf(ParserHostProjectionV1 *projection, Atom *atom) {
    switch (atom->kind) {
    case ATOM_SYMBOL: {
        const char *bytes = atom_symbol_bytes(atom);
        size_t len = atom_symbol_length(atom);
        return parser_host_projection_v1_word_bytes(
            projection, (const uint8_t *)bytes, len);
    }
    case ATOM_VAR: {
        const char *bytes = atom_symbol_bytes(atom);
        size_t len = atom_symbol_length(atom);
        return len == 0u
                   ? parser_host_projection_v1_anonymous_variable(projection)
                   : parser_host_projection_v1_variable_bytes(
                         projection, (const uint8_t *)bytes, len);
    }
    case ATOM_GROUNDED:
        return term_universe_store_atom_id(
            projection->universe, &projection->scratch, atom);
    case ATOM_EXPR: break;
    }
    return CETTA_ATOM_ID_NONE;
}

typedef struct {
    Atom *source;
    AtomId *children;
    CettaExprIndex next;
    uint32_t depth;
} ParserProjectionFrame;

static AtomId parser_project_atom_id_scoped(
    ParserHostProjectionV1 *projection, Atom *atom, uint32_t depth) {
    ParserProjectionFrame inline_frames[32];
    ParserProjectionFrame *frames = inline_frames;
    size_t len = 0u, cap = sizeof inline_frames / sizeof inline_frames[0];
    AtomId result = CETTA_ATOM_ID_NONE;
    if (!projection)
        return result;
    for (;;) {
        if (!atom || !depth)
            goto fail;
        if (atom->kind != ATOM_EXPR) {
            result = parser_project_atom_leaf(projection, atom);
        } else if (!atom->expr.len) {
            result = parser_host_projection_v1_expression(projection, NULL, 0u);
        } else {
            if (len == cap) {
                if (cap > SIZE_MAX / 2u || cap * 2u > SIZE_MAX / sizeof(*frames))
                    goto fail;
                size_t next_cap = cap * 2u;
                ParserProjectionFrame *next = frames == inline_frames
                    ? malloc(sizeof(*next) * next_cap)
                    : realloc(frames, sizeof(*next) * next_cap);
                if (!next)
                    goto fail;
                if (frames == inline_frames)
                    memcpy(next, inline_frames, sizeof(*next) * len);
                frames = next;
                cap = next_cap;
            }
            if (!cetta_expr_len_mul_fits_size(atom->expr.len, sizeof(AtomId)))
                goto fail;
            AtomId *children = malloc(sizeof(*children) * (size_t)atom->expr.len);
            if (!children)
                goto fail;
            frames[len++] = (ParserProjectionFrame){atom, children, 0u, depth};
            atom = atom->expr.elems[0];
            depth--;
            continue;
        }
        if (result == CETTA_ATOM_ID_NONE)
            goto fail;
        while (len) {
            ParserProjectionFrame *frame = &frames[len - 1u];
            frame->children[frame->next++] = result;
            if (frame->next < frame->source->expr.len) {
                atom = frame->source->expr.elems[frame->next];
                depth = frame->depth - 1u;
                break;
            }
            result = parser_host_projection_v1_expression(
                projection, frame->children, frame->source->expr.len);
            free(frame->children);
            len--;
            if (result == CETTA_ATOM_ID_NONE)
                goto fail;
        }
        if (!len)
            break;
    }
    if (frames != inline_frames)
        free(frames);
    return result;

fail:
    while (len)
        free(frames[--len].children);
    if (frames != inline_frames)
        free(frames);
    return CETTA_ATOM_ID_NONE;
}

bool parser_project_document_ids(Atom *const *atoms, uint32_t atom_len,
                                 TermUniverse *universe, AtomId **out_ids) {
    ParserHostProjectionV1 *projection = NULL;
    AtomId *ids = NULL;

    if (!out_ids)
        return false;
    *out_ids = NULL;
    if (!universe || (atom_len > 0u && !atoms))
        return false;
    if (atom_len > 0u) {
        ids = cetta_malloc(sizeof(*ids) * (size_t)atom_len);
        if (!ids)
            return false;
    }
    projection = parser_host_projection_v1_new(universe);
    if (!projection) {
        free(ids);
        return false;
    }
    for (uint32_t index = 0u; index < atom_len; index++) {
        if (!parser_host_projection_v1_begin_form(projection)) {
            free(ids);
            parser_host_projection_v1_free(projection);
            return false;
        }
        ids[index] = parser_project_atom_id_scoped(
            projection, atoms[index], CETTA_PARSE_DEPTH_LIMIT);
        if (ids[index] == CETTA_ATOM_ID_NONE) {
            free(ids);
            parser_host_projection_v1_free(projection);
            return false;
        }
    }
    parser_host_projection_v1_free(projection);
    *out_ids = ids;
    return true;
}

/* ── Parse a single token or expression ─────────────────────────────────── */

/* Prime's reader: a bracket or brace touching the end of a term is a
 * meta-argument, T[...] or T{...}, read as the wrapper (meta T [...]) or
 * (meta T {...}); they apply left to right, and a prefix binds tighter, so
 * @(f $x)[$x := @a] wraps the code @(f $x).  The reader gives no head a
 * meaning (the core reads [k := v] on code as substitution, and {...} on a
 * scope-forming construct as its crossing set).  Inside a touching bracket
 * `:=` is a word; with layout before it, `[` opens a list, in which `:=` is
 * refused, so the one-space slip is caught.  Brackets and braces end
 * tokens. */
static bool parser_at_binding_mark(const char *text, size_t text_len,
                                   size_t pos) {
    return parser_more(text, text_len, pos) && text[pos] == ':' &&
           parser_more(text, text_len, pos + 1u) && text[pos + 1u] == '=' &&
           !(parser_more(text, text_len, pos + 2u) &&
             is_list_token_char(text[pos + 2u]));
}

static inline __attribute__((always_inline)) Atom *parser_read_word_atom(
    Arena *a, char *tok, size_t len, ParserVarScope *scope) {
    /* Prime's bare `$` is a fresh anonymous variable. Named variables keep
       their ordinary per-form co-reference. */
    if (tok[0] == '$') {
        if (len > 1) {
            const char *spelling_text =
                parser_canonicalize_namespace_token(a, tok + 1);
            SymbolId spelling = parser_intern_token(spelling_text);
            VarId id = parser_var_scope_id(scope, spelling);
            return atom_var_with_spelling(a, spelling, id);
        }
        if (parser_bare_dollar_is_variable()) {
            SymbolId spelling = symbol_intern_cstr(g_symbols, "");
            VarId id = parser_bare_dollar_id(scope, spelling);
            return atom_var_with_spelling(a, spelling, id);
        }
    }

    ParserWordPolicy policy = {g_rational_literals_enabled,
                               g_namespace_sugar_enabled};
    return parser_ground_word(a, NULL, tok, len, policy).atom;
}

typedef union {
    Atom *atom;
    AtomId id;
} ParserReadValue;

typedef enum {
    PARSER_READ_EXPRESSION,
    PARSER_READ_LIST,
    PARSER_READ_LIST_REST,
    PARSER_READ_BRACES,
    PARSER_READ_TOUCHING_LIST,
    PARSER_READ_META,
    PARSER_READ_REF,
    PARSER_READ_VARIABLE,
    PARSER_READ_QUOTE,
    PARSER_READ_UNQUOTE,
} ParserReadKind;

typedef struct ParserReadBackend ParserReadBackend;

typedef struct {
    ParserReadKind kind;
    const ParserReadBackend *backend;
    bool in_list;
    bool term_only;
    int depth;
    ParserReadValue base;
    void *children;
    uint32_t len;
    uint32_t cap;
} ParserReadFrame;

typedef struct {
    TermUniverse *universe;
    Arena *arena;
    ParserVarScope *scope;
} ParserReadContext;

struct ParserReadBackend {
    bool ids;
    ParserReadValue (*word)(ParserReadContext *, char *, size_t);
    ParserReadValue (*string)(ParserReadContext *, char *, size_t);
    ParserReadValue (*container)(ParserReadContext *, const ParserReadFrame *, ParserReadValue);
    ParserReadValue (*prefix)(ParserReadContext *, ParserReadKind, Atom *);
    ParserReadValue (*meta)(ParserReadContext *, ParserReadValue, ParserReadValue);
};

typedef struct {
    ParserReadFrame inline_frames[32];
    ParserReadFrame *frames;
    size_t len;
    size_t cap;
} ParserReadStack;

static inline __attribute__((always_inline)) bool parser_read_push(
    ParserReadStack *stack, ParserReadFrame frame) {
    if (stack->len == stack->cap) {
        if (stack->cap > SIZE_MAX / 2u ||
            stack->cap * 2u > SIZE_MAX / sizeof(*stack->frames))
            return false;
        size_t cap = stack->cap * 2u;
        ParserReadFrame *next = stack->frames == stack->inline_frames
            ? malloc(cap * sizeof(*next))
            : realloc(stack->frames, cap * sizeof(*next));
        if (!next)
            return false;
        if (stack->frames == stack->inline_frames)
            memcpy(next, stack->inline_frames, stack->len * sizeof(*next));
        stack->frames = next;
        stack->cap = cap;
    }
    stack->frames[stack->len++] = frame;
    return true;
}

static inline __attribute__((always_inline)) bool parser_read_append(
    ParserReadFrame *frame, ParserReadValue value) {
    size_t width = frame->backend->ids ? sizeof(AtomId) : sizeof(Atom *);
    if (frame->len == frame->cap) {
        if (frame->cap > UINT32_MAX / 2u)
            return false;
        uint32_t cap = frame->cap ? frame->cap * 2u
            : frame->kind == PARSER_READ_EXPRESSION ? 16u : 8u;
        if (!cetta_expr_len_mul_fits_size(cap, width))
            return false;
        void *next = realloc(frame->children, cap * width);
        if (!next)
            return false;
        frame->children = next;
        frame->cap = cap;
    }
    if (frame->backend->ids)
        ((AtomId *)frame->children)[frame->len++] = value.id;
    else
        ((Atom **)frame->children)[frame->len++] = value.atom;
    return true;
}

static ParserReadValue parser_read_id_word(ParserReadContext *ctx, char *word, size_t length) {
    AtomId result;
    if (word[0] == '$' && (length > 1u || parser_bare_dollar_is_variable())) {
        const char *name = length > 1u
            ? parser_canonicalize_namespace_token(ctx->arena, word + 1u) : "";
        SymbolId spelling = symbol_intern_cstr(g_symbols, name);
        VarId id = length > 1u ? parser_var_scope_id(ctx->scope, spelling)
            : parser_bare_dollar_id(ctx->scope, spelling);
        result = tu_intern_var(ctx->universe, spelling, id);
    } else {
        result = parser_project_word_id(ctx->universe, ctx->arena, word, length,
                                        g_namespace_sugar_enabled);
    }
    return (ParserReadValue){.id = result};
}

static ParserReadValue parser_read_id_string(ParserReadContext *ctx, char *bytes, size_t length) {
    return (ParserReadValue){.id = tu_intern_string_n(ctx->universe, bytes, length)};
}

static inline __attribute__((always_inline)) ParserReadValue parser_read_atom_container(ParserReadContext *ctx,
    const ParserReadFrame *frame, ParserReadValue rest) {
    Atom *result;
    if (frame->kind == PARSER_READ_BRACES) {
        result = atom_prime_braces(ctx->arena, frame->children, frame->len);
    } else if (frame->kind != PARSER_READ_EXPRESSION) {
        result = rest.atom
            ? atom_list_with_rest(ctx->arena, frame->children, frame->len, rest.atom)
            : atom_list(ctx->arena, frame->children, frame->len);
    } else {
        result = parser_lower_syn_expression(ctx->arena,
            atom_expr(ctx->arena, frame->children, frame->len), ctx->scope);
    }
    return (ParserReadValue){.atom = result};
}

static ParserReadValue parser_read_id_container(ParserReadContext *ctx,
    const ParserReadFrame *frame, ParserReadValue rest) {
    AtomId result = CETTA_ATOM_ID_NONE;
    if (frame->kind == PARSER_READ_BRACES) {
        result = tu_braces_from_ids(ctx->universe, frame->children, frame->len);
    } else if (frame->kind != PARSER_READ_EXPRESSION) {
        result = tu_list_from_ids(ctx->universe, frame->children, frame->len, rest.id);
    } else {
        ParserSynLowerStatus lowered = parser_lower_syn_expression_id(
            ctx->universe, ctx->arena, frame->children, frame->len, ctx->scope, &result);
        if (lowered == PARSER_SYN_LOWER_NOT_FORM)
            result = tu_expr_from_ids(ctx->universe, frame->children, frame->len);
        else if (lowered == PARSER_SYN_LOWER_INVALID)
            result = CETTA_ATOM_ID_NONE;
    }
    return (ParserReadValue){.id = result};
}

static ParserReadValue parser_read_id_meta(ParserReadContext *ctx,
    ParserReadValue base, ParserReadValue argument) {
    AtomId parts[3] = {
        tu_intern_symbol(ctx->universe, symbol_intern_cstr(g_symbols, "meta")),
        base.id, argument.id,
    };
    return (ParserReadValue){.id = tu_expr_from_ids(ctx->universe, parts, 3u)};
}

static Atom *parser_read_prefix_atom(ParserReadContext *ctx, ParserReadKind kind, Atom *payload) {
    if (kind == PARSER_READ_VARIABLE) {
        VarId id = parser_var_scope_name_id(ctx->scope, payload);
        return var_id_is_none(id) ? NULL : atom_var_with_name_key(ctx->arena, payload, id);
    }
    if (kind == PARSER_READ_REF) {
        if (parser_var_scope_name_key_id(ctx->scope, payload) == NAME_ID_NONE)
            return NULL;
        payload = atom_expr2(ctx->arena, atom_symbol_id(ctx->arena, g_builtin_syms.quote), payload);
        return atom_expr2(ctx->arena, atom_symbol(ctx->arena, "resolve-name"), payload);
    }
    return atom_expr2(ctx->arena,
        kind == PARSER_READ_QUOTE ? atom_symbol_id(ctx->arena, g_builtin_syms.quote)
            : atom_symbol(ctx->arena, "unquote"), payload);
}

static ParserReadValue parser_read_id_prefix(ParserReadContext *ctx, ParserReadKind kind, Atom *payload) {
    AtomId result = CETTA_ATOM_ID_NONE;
    if (kind == PARSER_READ_VARIABLE) {
        VarId id = parser_var_scope_name_id(ctx->scope, payload);
        if (!var_id_is_none(id)) {
            AtomId key = term_universe_store_atom_id(ctx->universe, ctx->arena, payload);
            if (key != CETTA_ATOM_ID_NONE)
                result = tu_intern_named_var(ctx->universe, key, id);
        }
    } else {
        Atom *form = parser_read_prefix_atom(ctx, kind, payload);
        if (form)
            result = term_universe_store_atom_id(ctx->universe, ctx->arena, form);
    }
    return (ParserReadValue){.id = result};
}

/* Keep ID conversion callbacks at the requesting call site: an atom-only
 * generator can discard them without linking a TermUniverse. Atom construction
 * takes a direct path in the same grammar. */
static const ParserReadBackend g_parser_atom_backend = {.ids = false};

/* One grammar driver serves borrowed atoms and persistent IDs. Prefix forms
 * retain the original atom-valued name interpretation before publication to
 * the requesting backend. The declared depth budget belongs to the syntax,
 * independently of native call depth, and every unfinished buffer is owned
 * by exactly one frame. */
static ParserReadValue parser_read_scoped(
    TermUniverse *universe, Arena *arena, const char *text, size_t text_len,
    size_t *pos, ParserVarScope *scope, int depth, const ParserReadBackend *backend) {
    if (!backend)
        backend = &g_parser_atom_backend;
    const ParserReadBackend *root_backend = backend;
    ParserReadContext context = {universe, arena, scope};
    bool in_list = false;
    /* Prefix operands consume one term; touching suffixes belong to the
     * completed prefix. Container children consume their own suffixes. */
    bool term_only = false;
    /* The term just delivered is the query marker `!` written alone.  It
     * marks the term after it and is no head, so a bracket or brace
     * touching it opens that term, as a parenthesis after it does: `![a]`
     * and `!{a}` read as `! [a]` and `! {a}`.  A longer word that starts
     * with `!` is an ordinary word. */
    bool exec_marker = false;
    ParserReadStack stack;
    stack.frames = stack.inline_frames;
    stack.len = 0u;
    stack.cap = 32u;
    ParserReadValue result;
    if (!arena || !text || !pos || (backend->ids && !universe))
        goto fail;

read_node:
    if (depth <= 0)
        goto fail;
    skip_whitespace_and_comments(text, text_len, pos);

read_trimmed_node:
    if (depth <= 0 || !parser_more(text, text_len, *pos))
        goto fail;
    {
        ParserReadFrame frame = {
            .backend = backend, .in_list = in_list,
            .term_only = term_only, .depth = depth,
        };
        bool next = parser_more(text, text_len, *pos + 1u);
        char token = text[*pos];
        char following = next ? text[*pos + 1u] : '\0';
        bool prefix = g_universal_name_syntax_enabled;
        if (prefix && next &&
            token == parser_syntax_compact_char(PARSER_SYNTAX_REF) &&
            following == parser_syntax_compact_char(PARSER_SYNTAX_QUOTE)) {
            frame.kind = PARSER_READ_REF;
            *pos += 2u;
        } else if (prefix && next &&
                   token == parser_syntax_compact_char(PARSER_SYNTAX_VAR) &&
                   following == parser_syntax_compact_char(PARSER_SYNTAX_QUOTE)) {
            frame.kind = PARSER_READ_VARIABLE;
            *pos += 2u;
        } else if (prefix && token == parser_syntax_compact_char(PARSER_SYNTAX_QUOTE)) {
            frame.kind = PARSER_READ_QUOTE;
            (*pos)++;
        } else if (prefix && token == parser_syntax_compact_char(PARSER_SYNTAX_UNQUOTE) &&
                   next && !isspace((unsigned char)following) &&
                   following != ')' && following != ';' && following != '}' &&
                   (!in_list || following != ']')) {
            frame.kind = PARSER_READ_UNQUOTE;
            (*pos)++;
        } else {
            prefix = false;
        }
        if (prefix) {
            if (!parser_read_push(&stack, frame))
                goto fail;
            backend = &g_parser_atom_backend;
            term_only = true;
            depth--;
            goto read_node;
        }
        if (token == '(' || (g_list_syntax_enabled && token == '[') ||
            (g_universal_name_syntax_enabled && token == '{')) {
            frame.kind = token == '(' ? PARSER_READ_EXPRESSION
                : token == '{' ? PARSER_READ_BRACES : PARSER_READ_LIST;
            if (!parser_read_push(&stack, frame))
                goto fail;
            (*pos)++;
            goto read_container;
        }
        if (token == '"') {
            (*pos)++;
            size_t start = *pos;
            while (parser_more(text, text_len, *pos) && text[*pos] != '"') {
                if (text[*pos] == '\\' && parser_more(text, text_len, *pos + 1u))
                    (*pos)++;
                (*pos)++;
            }
            if (!parser_more(text, text_len, *pos))
                goto fail;
            char *buffer = arena_alloc(arena, *pos - start + 1u);
            size_t length = parser_decode_string_body(text, start, *pos, buffer);
            (*pos)++;
            result = backend->ids ? backend->string(&context, buffer, length)
                : (ParserReadValue){.atom = atom_string_n(arena, buffer, length)};
            goto term_delivered;
        }
        size_t start = *pos;
        while (parser_more(text, text_len, *pos) &&
               is_token_char_in(text[*pos], in_list || g_universal_name_syntax_enabled))
            (*pos)++;
        size_t length = *pos - start;
        if (!length)
            goto fail;
        char *word = arena_alloc(arena, length + 1u);
        memcpy(word, text + start, length);
        word[length] = '\0';
        result = backend->ids ? backend->word(&context, word, length)
            : (ParserReadValue){.atom = parser_read_word_atom(arena, word, length, scope)};
        exec_marker = length == 1u &&
            word[0] == parser_syntax_compact_char(PARSER_SYNTAX_EXEC);
        goto term_delivered;
    }

read_container:
    {
        ParserReadFrame *frame = &stack.frames[stack.len - 1u];
        skip_whitespace_and_comments(text, text_len, pos);
        char close = frame->kind == PARSER_READ_EXPRESSION ? ')'
            : frame->kind == PARSER_READ_BRACES ? '}' : ']';
        if (parser_more(text, text_len, *pos) && text[*pos] == close) {
            (*pos)++;
            backend = frame->backend;
            in_list = frame->in_list;
            term_only = frame->term_only;
            depth = frame->depth;
            ParserReadValue rest;
            if (backend->ids) rest.id = CETTA_ATOM_ID_NONE;
            else rest.atom = NULL;
            result = backend->ids ? backend->container(&context, frame, rest)
                : parser_read_atom_container(&context, frame, rest);
            free(frame->children);
            stack.len--;
            goto term_delivered;
        }
        if (frame->kind == PARSER_READ_LIST && g_universal_name_syntax_enabled &&
            parser_at_binding_mark(text, text_len, *pos))
            goto fail;
        if ((frame->kind == PARSER_READ_LIST || frame->kind == PARSER_READ_TOUCHING_LIST) &&
            parser_more(text, text_len, *pos) && parser_at_list_bar(text, text_len, *pos)) {
            if (frame->kind == PARSER_READ_TOUCHING_LIST || !frame->len)
                goto fail;
            frame->kind = PARSER_READ_LIST_REST;
            (*pos)++;
            skip_whitespace_and_comments(text, text_len, pos);
            if (g_universal_name_syntax_enabled &&
                parser_at_binding_mark(text, text_len, *pos))
                goto fail;
            backend = frame->backend;
            in_list = true;
            term_only = false;
            depth = frame->depth - 1;
            goto read_trimmed_node;
        }
        backend = frame->backend;
        in_list = frame->kind == PARSER_READ_LIST ||
                  frame->kind == PARSER_READ_TOUCHING_LIST;
        term_only = false;
        depth = frame->depth - 1;
        goto read_trimmed_node;
    }

term_delivered:
    if (backend->ids ? result.id == CETTA_ATOM_ID_NONE : result.atom == NULL)
        goto fail;
    /* Layout ends the suffix chain. The saved term is never read again and
     * each suffix is constructed in the same requesting backend.  The query
     * marker takes no suffix. */
    if (exec_marker) {
        exec_marker = false;
    } else if (!term_only && g_universal_name_syntax_enabled &&
        parser_more(text, text_len, *pos) && (text[*pos] == '[' || text[*pos] == '{')) {
        if (depth <= 1)
            goto fail;
        ParserReadFrame meta = {
            .kind = PARSER_READ_META, .backend = backend, .in_list = in_list,
            .term_only = term_only, .depth = depth, .base = result,
        };
        ParserReadFrame argument = {
            .kind = text[*pos] == '{' ? PARSER_READ_BRACES : PARSER_READ_TOUCHING_LIST,
            .backend = backend, .in_list = in_list, .term_only = true, .depth = depth,
        };
        if (!parser_read_push(&stack, meta) || !parser_read_push(&stack, argument))
            goto fail;
        (*pos)++;
        goto read_container;
    }
    if (!stack.len) {
        if (stack.frames != stack.inline_frames)
            free(stack.frames);
        return result;
    }
    {
        ParserReadFrame *frame = &stack.frames[stack.len - 1u];
        if (frame->kind == PARSER_READ_EXPRESSION || frame->kind == PARSER_READ_LIST ||
            frame->kind == PARSER_READ_BRACES || frame->kind == PARSER_READ_TOUCHING_LIST) {
            if (!parser_read_append(frame, result))
                goto fail;
            goto read_container;
        }
        if (frame->kind == PARSER_READ_LIST_REST) {
            skip_whitespace_and_comments(text, text_len, pos);
            if (!parser_more(text, text_len, *pos) || text[*pos] != ']')
                goto fail;
            (*pos)++;
            backend = frame->backend;
            result = backend->ids ? backend->container(&context, frame, result)
                : parser_read_atom_container(&context, frame, result);
        } else if (frame->kind == PARSER_READ_META) {
            backend = frame->backend;
            result = backend->ids ? backend->meta(&context, frame->base, result)
                : (ParserReadValue){.atom = atom_expr3(arena, atom_symbol(arena, "meta"),
                                                       frame->base.atom, result.atom)};
        } else {
            Atom *payload = result.atom;
            backend = frame->backend;
            result = backend->ids ? backend->prefix(&context, frame->kind, payload)
                : (ParserReadValue){.atom = parser_read_prefix_atom(&context, frame->kind, payload)};
        }
        in_list = frame->in_list;
        term_only = frame->term_only;
        depth = frame->depth;
        free(frame->children);
        stack.len--;
        goto term_delivered;
    }

fail:
    while (stack.len)
        free(stack.frames[--stack.len].children);
    if (stack.frames != stack.inline_frames)
        free(stack.frames);
    if (root_backend->ids) result.id = CETTA_ATOM_ID_NONE;
    else result.atom = NULL;
    return result;
}

Atom *parse_sexpr_n(Arena *arena, const char *text, size_t text_len, size_t *pos) {
    ParserVarScope scope;
    parser_var_scope_init(&scope);
    Atom *result = parser_read_scoped(NULL, arena, text, text_len, pos, &scope,
                                     CETTA_PARSE_DEPTH_LIMIT, NULL).atom;
    parser_var_scope_free(&scope);
    return result;
}

Atom *parse_data_sexpr_n(Arena *a, const char *text, size_t text_len,
                         size_t *pos) {
    bool previous = g_runtime_data_symbols;
    g_runtime_data_symbols = true;
    Atom *result = parse_sexpr_n(a, text, text_len, pos);
    g_runtime_data_symbols = previous;
    return result;
}

Atom *parse_sexpr(Arena *a, const char *text, size_t *pos) {
    return parse_sexpr_n(a, text, PARSER_TEXT_NUL_TERMINATED, pos);
}

static AtomId parse_sexpr_to_id_scoped(TermUniverse *universe, Arena *scratch,
                                       const char *text, size_t text_len,
                                       size_t *pos, ParserVarScope *scope,
                                       int depth) {
    const ParserReadBackend backend = {
        true, parser_read_id_word, parser_read_id_string,
        parser_read_id_container, parser_read_id_prefix, parser_read_id_meta,
    };
    return parser_read_scoped(universe, scratch, text, text_len, pos, scope,
                              depth, &backend).id;
}

AtomId parse_sexpr_to_id(TermUniverse *universe, const char *text, size_t *pos) {
    ParserVarScope scope;
    Arena scratch;
    parser_var_scope_init(&scope);
    arena_init(&scratch);
    arena_set_hashcons(&scratch, NULL);
    AtomId result = parse_sexpr_to_id_scoped(universe, &scratch, text,
        PARSER_TEXT_NUL_TERMINATED, pos, &scope, CETTA_PARSE_DEPTH_LIMIT);
    arena_free(&scratch);
    parser_var_scope_free(&scope);
    return result;
}

bool parser_syn_exec_payload(Atom *form, Atom **payload_out) {
    if (payload_out) *payload_out = NULL;
    if (!form || form->kind != ATOM_EXPR || form->expr.len != 2u ||
        form->expr.elems[0]->kind != ATOM_SYMBOL ||
        !parser_symbol_is_syntax_form(
            atom_symbol_current_id(form->expr.elems[0]), PARSER_SYNTAX_EXEC)) {
        return false;
    }
    if (payload_out) *payload_out = form->expr.elems[1];
    return true;
}

bool parser_syn_exec_payload_id(const TermUniverse *universe, AtomId form_id,
                                AtomId *payload_out) {
    if (payload_out) *payload_out = CETTA_ATOM_ID_NONE;
    if (!universe || tu_kind(universe, form_id) != ATOM_EXPR ||
        tu_arity(universe, form_id) != 2u) {
        return false;
    }
    AtomId head = tu_child(universe, form_id, 0u);
    if (tu_kind(universe, head) != ATOM_SYMBOL ||
        !parser_symbol_is_syntax_form(
            tu_sym(universe, head), PARSER_SYNTAX_EXEC)) {
        return false;
    }
    if (payload_out) *payload_out = tu_child(universe, form_id, 1u);
    return true;
}

Atom *parser_read_source_form_n(Arena *a, const char *text, size_t text_len) {
    if (!a || !text || !parser_text_well_formed_n(text, text_len))
        return NULL;

    size_t pos = 0;
    skip_whitespace_and_comments(text, text_len, &pos);
    /* The marker alone is the query marker; in Prime's universal name
     * syntax a bracket or brace ends it, as it ends every word. */
    bool is_exec = parser_more(text, text_len, pos) &&
                   text[pos] == parser_syntax_compact_char(PARSER_SYNTAX_EXEC) &&
                   parser_more(text, text_len, pos + 1u) &&
                   !is_token_char_in(text[pos + 1u],
                                     g_universal_name_syntax_enabled);
    if (is_exec)
        pos++;

    Atom *form = parse_sexpr_n(a, text, text_len, &pos);
    if (!form || !parser_rest_is_delimiters_n(text, text_len, &pos))
        return NULL;
    return is_exec
        ? atom_expr2(
              a,
              atom_symbol_id(
                  a, parser_syntax_expanded_id(PARSER_SYNTAX_EXEC)),
              form)
        : form;
}

Atom *parser_read_source_form(Arena *a, const char *text) {
    return parser_read_source_form_n(a, text, PARSER_TEXT_NUL_TERMINATED);
}

typedef struct {
    VarId id;
    bool named;
    SymbolId spelling;
    Atom *key;
    Atom *fresh_key;
    bool collides;
} ParserRenderVar;

typedef struct {
    Arena *arena;
    ParserRenderVar *vars;
    size_t len;
    size_t cap;
    uint64_t next_fresh;
} ParserRenderContext;

static ParserRenderVar *parser_render_var_find(ParserRenderContext *ctx,
                                               VarId id) {
    for (size_t i = 0; i < ctx->len; i++)
        if (var_id_eq(ctx->vars[i].id, id))
            return &ctx->vars[i];
    return NULL;
}

typedef struct {
    Atom *source;
    CettaExprIndex next;
    int depth;
} ParserSyntaxWalkFrame;

static bool parser_render_record_var(ParserRenderContext *ctx, Atom *atom) {
    if (parser_render_var_find(ctx, atom->var_id))
        return true;
    if (ctx->len == ctx->cap) {
        size_t next = ctx->cap ? ctx->cap * 2u : 16u;
        if (next < ctx->cap || next > SIZE_MAX / sizeof(*ctx->vars))
            return false;
        ParserRenderVar *vars = realloc(ctx->vars, next * sizeof(*vars));
        if (!vars)
            return false;
        ctx->vars = vars;
        ctx->cap = next;
    }
    ctx->vars[ctx->len++] = (ParserRenderVar){
        .id = atom->var_id,
        .named = atom->name_key != NULL,
        .spelling = atom_symbol_current_id(atom),
        .key = atom->name_key,
    };
    return true;
}

static bool parser_render_collect_vars(ParserRenderContext *ctx, Atom *atom,
                                       int depth) {
    ParserSyntaxWalkFrame inline_frames[32];
    ParserSyntaxWalkFrame *frames = inline_frames;
    size_t len = 0u, cap = sizeof inline_frames / sizeof inline_frames[0];
    bool ok = false;
    for (;;) {
        atom = atom_prime_held_payload(atom);
        if (!atom || depth <= 0)
            goto done;
        if (atom->kind == ATOM_VAR) {
            if (!parser_render_record_var(ctx, atom))
                goto done;
        } else if (atom->kind == ATOM_EXPR && atom_authored_len(atom)) {
            if (len == cap) {
                if (cap > SIZE_MAX / 2u || cap * 2u > SIZE_MAX / sizeof(*frames))
                    goto done;
                size_t next_cap = cap * 2u;
                ParserSyntaxWalkFrame *next = frames == inline_frames
                    ? malloc(sizeof(*next) * next_cap)
                    : realloc(frames, sizeof(*next) * next_cap);
                if (!next)
                    goto done;
                if (frames == inline_frames)
                    memcpy(next, inline_frames, sizeof(*next) * len);
                frames = next;
                cap = next_cap;
            }
            frames[len++] = (ParserSyntaxWalkFrame){atom, 0u, depth};
            atom = atom->expr.elems[0];
            depth--;
            continue;
        }
        while (len) {
            ParserSyntaxWalkFrame *frame = &frames[len - 1u];
            if (++frame->next < atom_authored_len(frame->source)) {
                atom = frame->source->expr.elems[frame->next];
                depth = frame->depth - 1;
                break;
            }
            len--;
        }
        if (!len) {
            ok = true;
            break;
        }
    }

done:
    if (frames != inline_frames)
        free(frames);
    return ok;
}

static bool parser_render_var_same_presentation(const ParserRenderVar *a,
                                                const ParserRenderVar *b) {
    if (a->named != b->named)
        return false;
    return a->named ? atom_eq(a->key, b->key) : a->spelling == b->spelling;
}

static bool parser_render_key_used(const ParserRenderContext *ctx, Atom *key) {
    for (size_t i = 0; i < ctx->len; i++) {
        if (ctx->vars[i].named && ctx->vars[i].key &&
            atom_eq(ctx->vars[i].key, key)) {
            return true;
        }
        if (ctx->vars[i].fresh_key && atom_eq(ctx->vars[i].fresh_key, key))
            return true;
    }
    return false;
}

static bool parser_render_prepare_vars(ParserRenderContext *ctx) {
    for (size_t i = 0; i < ctx->len; i++) {
        for (size_t j = i + 1u; j < ctx->len; j++) {
            if (parser_render_var_same_presentation(&ctx->vars[i],
                                                    &ctx->vars[j])) {
                ctx->vars[i].collides = true;
                ctx->vars[j].collides = true;
            }
        }
    }
    for (size_t i = 0; i < ctx->len; i++) {
        if (!ctx->vars[i].collides)
            continue;
        Atom *candidate = NULL;
        do {
            Atom *serial = atom_int(ctx->arena, (int64_t)ctx->next_fresh++);
            candidate = atom_expr2(
                ctx->arena, atom_symbol(ctx->arena, "syn:printed-variable"),
                serial);
        } while (parser_render_key_used(ctx, candidate));
        ctx->vars[i].fresh_key = candidate;
    }
    return true;
}

static bool parser_render_grounded(FILE *out, Atom *atom) {
    switch (atom->ground.gkind) {
    case GV_INT:
    case GV_BOOL:
    case GV_STRING:
    case GV_BIGINT:
    case GV_RATIONAL:
        atom_print(atom, out);
        return true;
    case GV_FLOAT:
        if (!isfinite(atom->ground.fval))
            return false;
        atom_print(atom, out);
        return true;
    case GV_SPACE:
    case GV_STATE:
    case GV_CAPTURE:
    case GV_BINDINGS:
    case GV_TENSOR:
    case GV_FOREIGN:
    case GV_STRUCTURAL:
    case GV_SEAL:
    case GV_TERM_GRAPH:
    case GV_PRIME_NEED_CAPABILITY:
    case GV_PRIME_CONTEXT:
    case GV_INTERNAL_TAG:
        return false;
    }
    return false;
}

typedef struct {
    Atom *source;
    Atom *single;
    CettaExprIndex start;
    CettaExprIndex next;
    CettaExprIndex end;
    ParserSyntaxPrintMode mode;
    int depth;
    const char *close;
    bool list;
    bool open;
    bool touching;
} ParserRenderFrame;

static bool parser_render_form(FILE *out, ParserRenderContext *ctx, Atom *atom,
                               ParserSyntaxPrintMode mode, int depth) {
    ParserRenderFrame inline_frames[32];
    ParserRenderFrame *frames = inline_frames;
    size_t len = 0u, cap = sizeof inline_frames / sizeof inline_frames[0];
    bool ok = false;
    if (!out || !ctx)
        return false;

render_node:
    atom = atom_prime_held_payload(atom);
    if (!atom || depth <= 0)
        goto done;
    {
        ParserRenderFrame frame = {.mode = mode, .depth = depth, .close = ""};
        bool compact = mode == PARSER_SYNTAX_PRINT_COMPACT &&
                       g_universal_name_syntax_enabled;
        if (atom->kind == ATOM_GROUNDED) {
            if (!parser_render_grounded(out, atom))
                goto done;
            goto unwind;
        }
        if (atom->kind == ATOM_SYMBOL) {
            const char *name = atom_name_cstr(atom);
            if (mode == PARSER_SYNTAX_PRINT_EXPANDED && name[0] == '&' && name[1])
                fprintf(out, "(%s %s)",
                    parser_syntax_form(PARSER_SYNTAX_REF)->expanded, name + 1u);
            else
                fputs(name, out);
            goto unwind;
        }
        if (atom->kind == ATOM_VAR) {
            ParserRenderVar *entry = parser_render_var_find(ctx, atom->var_id);
            if (!entry)
                goto done;
            Atom *key = entry->fresh_key ? entry->fresh_key : entry->key;
            const ParserSyntaxFormSpec *var = parser_syntax_form(PARSER_SYNTAX_VAR);
            const ParserSyntaxFormSpec *quote = parser_syntax_form(PARSER_SYNTAX_QUOTE);
            if (compact) {
                fputs(var->compact, out);
                if (!key) {
                    fputs(symbol_bytes(g_symbols, entry->spelling), out);
                    goto unwind;
                }
                fputs(quote->compact, out);
            } else {
                fprintf(out, "(%s ", var->expanded);
                if (!key) {
                    fprintf(out, "%s)", symbol_bytes(g_symbols, entry->spelling));
                    goto unwind;
                }
                fprintf(out, "(%s ", quote->expanded);
                frame.mode = PARSER_SYNTAX_PRINT_EXPANDED;
                frame.close = "))";
            }
            frame.single = key;
        } else if (atom_is_prime_meta(atom)) {
            frame.source = atom;
            frame.start = frame.next = 1u;
            frame.end = 3u;
            frame.touching = true;
        } else if (atom_is_prime_braces(atom) || atom_is_prime_own_list(atom)) {
            bool braces = atom_is_prime_braces(atom);
            fputc(braces ? '{' : '(', out);
            frame.source = atom;
            frame.start = frame.next = braces ? 1u : 2u;
            frame.end = atom->expr.len;
            frame.close = braces ? "}" : ")";
        } else if (atom_is_list_form(atom)) {
            fputc('[', out);
            frame.source = atom;
            frame.start = frame.next = 1u;
            frame.end = atom->expr.len;
            frame.close = "]";
            frame.list = true;
            frame.open = atom_is_list_rest(atom);
        } else {
            Atom *head = atom->expr.len ? atom->expr.elems[0] : NULL;
            SymbolId head_id = head && head->kind == ATOM_SYMBOL
                ? atom_symbol_current_id(head) : SYMBOL_ID_NONE;
            Atom *payload = atom->expr.len == 2u ? atom->expr.elems[1] : NULL;
            Atom *key = NULL;
            if (payload && head && atom_is_symbol(head, "resolve-name") &&
                parser_quote_payload(payload, &key)) {
                const ParserSyntaxFormSpec *ref = parser_syntax_form(PARSER_SYNTAX_REF);
                const ParserSyntaxFormSpec *quote = parser_syntax_form(PARSER_SYNTAX_QUOTE);
                if (compact) {
                    fputs(ref->compact, out);
                    fputs(quote->compact, out);
                } else {
                    fprintf(out, "(%s (%s ", ref->expanded, quote->expanded);
                    frame.mode = PARSER_SYNTAX_PRINT_EXPANDED;
                    frame.close = "))";
                }
                frame.single = key;
            } else if (payload && (head_id == g_builtin_syms.quote ||
                                   atom_is_symbol(head, "unquote"))) {
                const ParserSyntaxFormSpec *spec = parser_syntax_form(
                    head_id == g_builtin_syms.quote ? PARSER_SYNTAX_QUOTE : PARSER_SYNTAX_UNQUOTE);
                if (compact) fputs(spec->compact, out);
                else {
                    fprintf(out, "(%s ", spec->expanded);
                    frame.mode = PARSER_SYNTAX_PRINT_EXPANDED;
                    frame.close = ")";
                }
                frame.single = payload;
            } else if (payload && parser_symbol_is_syntax_form(head_id, PARSER_SYNTAX_EXEC)) {
                const ParserSyntaxFormSpec *spec = parser_syntax_form(PARSER_SYNTAX_EXEC);
                if (mode == PARSER_SYNTAX_PRINT_COMPACT) fputs(spec->compact, out);
                else {
                    fprintf(out, "(%s ", spec->expanded);
                    frame.mode = PARSER_SYNTAX_PRINT_EXPANDED;
                    frame.close = ")";
                }
                frame.single = payload;
            } else {
                fputc('(', out);
                frame.source = atom;
                frame.end = atom_authored_len(atom);
                frame.close = ")";
            }
        }
        if (frame.single && !*frame.close) {
            atom = frame.single;
            mode = frame.mode;
            depth--;
            goto render_node;
        }
        if (len == cap) {
            if (cap > SIZE_MAX / 2u || cap * 2u > SIZE_MAX / sizeof(*frames))
                goto done;
            size_t next_cap = cap * 2u;
            ParserRenderFrame *next = frames == inline_frames
                ? malloc(sizeof(*next) * next_cap)
                : realloc(frames, sizeof(*next) * next_cap);
            if (!next)
                goto done;
            if (frames == inline_frames)
                memcpy(next, inline_frames, sizeof(*next) * len);
            frames = next;
            cap = next_cap;
        }
        frames[len++] = frame;
    }

unwind:
    while (len) {
        ParserRenderFrame *frame = &frames[len - 1u];
        if (frame->single) {
            atom = frame->single;
            frame->single = NULL;
            mode = frame->mode;
            depth = frame->depth - 1;
            goto render_node;
        }
        if (frame->next < frame->end) {
            if (frame->next > frame->start && !frame->touching)
                fputs(frame->list && frame->open && frame->next + 1u == frame->end
                    ? " | " : " ", out);
            atom = frame->source->expr.elems[frame->next++];
            mode = frame->mode;
            depth = frame->depth - 1;
            goto render_node;
        }
        fputs(frame->close, out);
        len--;
    }
    ok = true;

done:
    if (frames != inline_frames)
        free(frames);
    return ok;
}

static char *parser_render_syntax_text(Arena *a, Atom *atom,
                                       ParserSyntaxPrintMode mode,
                                       bool c_text, size_t *len_out) {
    if (len_out)
        *len_out = 0u;
    if (!a || !atom)
        return NULL;
    ParserRenderContext ctx = {.arena = a};
    if (!parser_render_collect_vars(&ctx, atom, CETTA_PARSE_DEPTH_LIMIT) ||
        !parser_render_prepare_vars(&ctx)) {
        free(ctx.vars);
        return NULL;
    }

    char *buffer = NULL;
    size_t length = 0u;
    FILE *stream = open_memstream(&buffer, &length);
    if (!stream) {
        free(ctx.vars);
        return NULL;
    }
    bool previous = atom_print_set_c_text(c_text);
    bool ok = parser_render_form(stream, &ctx, atom, mode,
                                 CETTA_PARSE_DEPTH_LIMIT);
    if (fclose(stream) != 0)
        ok = false;
    atom_print_set_c_text(previous);
    free(ctx.vars);
    if (!ok) {
        free(buffer);
        return NULL;
    }
    char *result = arena_alloc(a, length + 1u);
    if (length)
        memcpy(result, buffer, length);
    result[length] = '\0';
    free(buffer);
    if (len_out)
        *len_out = length;
    return result;
}

char *parser_render_syntax(Arena *a, Atom *atom, ParserSyntaxPrintMode mode) {
    return parser_render_syntax_text(a, atom, mode, true, NULL);
}

char *parser_render_syntax_bytes(Arena *a, Atom *atom,
                                 ParserSyntaxPrintMode mode, size_t *len_out) {
    return parser_render_syntax_text(a, atom, mode, false, len_out);
}

/* ── Parse entire file ──────────────────────────────────────────────────── */

static int parse_metta_buffer(const char *text, size_t text_len, Arena *a,
                              Atom ***out_atoms) {
    if (!parser_text_well_formed_n(text, text_len)) {
        *out_atoms = NULL;
        return -1;
    }

    Atom **atoms = NULL;
    int count = 0;
    int cap = 0;
    size_t pos = 0;
    for (;;) {
        size_t probe = pos;
        if (parser_rest_is_delimiters_n(text, text_len, &probe)) {
            pos = probe;
            break;
        }
        Atom *at = parse_sexpr_n(a, text, text_len, &pos);
        if (!at) {
            free(atoms);
            *out_atoms = NULL;
            return -1;
        }
        if (count >= cap) {
            cap = cap ? cap * 2 : 64;
            atoms = cetta_realloc(atoms, sizeof(Atom *) * (size_t)cap);
        }
        atoms[count++] = at;
    }

    *out_atoms = atoms;
    return count;
}

static int parse_metta_buffer_ids(const char *text, size_t text_len,
                                  TermUniverse *universe, AtomId **out_ids) {
    Arena scratch;
    if (!out_ids)
        return -1;
    *out_ids = NULL;
    if (!text || !universe || !parser_text_well_formed_n(text, text_len))
        return -1;

    arena_init(&scratch);
    arena_set_hashcons(&scratch, NULL);

    AtomId *ids = NULL;
    int count = 0;
    int cap = 0;
    size_t pos = 0;
    for (;;) {
        size_t probe = pos;
        if (parser_rest_is_delimiters_n(text, text_len, &probe)) {
            pos = probe;
            break;
        }
        ParserVarScope scope;
        ArenaMark mark = arena_mark(&scratch);
        AtomId id;
        parser_var_scope_init(&scope);
        id = parse_sexpr_to_id_scoped(universe, &scratch, text, text_len, &pos,
                                      &scope, CETTA_PARSE_DEPTH_LIMIT);
        parser_var_scope_free(&scope);
        arena_reset(&scratch, mark);
        if (id == CETTA_ATOM_ID_NONE) {
            free(ids);
            arena_free(&scratch);
            *out_ids = NULL;
            return -1;
        }
        if (count >= cap) {
            cap = cap ? cap * 2 : 64;
            ids = cetta_realloc(ids, sizeof(AtomId) * (size_t)cap);
        }
        ids[count++] = id;
    }

    arena_free(&scratch);
    *out_ids = ids;
    return count;
}

static bool read_all_text(FILE *f, char **text_out, size_t *nread_out) {
    if (!f || !text_out || !nread_out) return false;

    if (fseek(f, 0, SEEK_END) == 0) {
        long fsize = ftell(f);
        if (fsize >= 0 && fseek(f, 0, SEEK_SET) == 0) {
            char *text = cetta_malloc((size_t)fsize + 1);
            size_t nread = fread(text, 1, (size_t)fsize, f);
            text[nread] = '\0';
            *text_out = text;
            *nread_out = nread;
            return true;
        }
        clearerr(f);
    } else {
        clearerr(f);
    }

    size_t cap = 4096;
    size_t nread = 0;
    char *text = cetta_malloc(cap + 1);
    for (;;) {
        size_t remaining = cap - nread;
        size_t nr = fread(text + nread, 1, remaining, f);
        nread += nr;
        if (nr < remaining) {
            if (ferror(f)) {
                free(text);
                return false;
            }
            if (feof(f)) {
                break;
            }
        }
        if (nread == cap) {
            cap *= 2;
            text = cetta_realloc(text, cap + 1);
        }
    }
    text[nread] = '\0';
    *text_out = text;
    *nread_out = nread;
    return true;
}

int parse_metta_text(const char *text, Arena *a, Atom ***out_atoms) {
    if (!text) {
        *out_atoms = NULL;
        return -1;
    }
    return parse_metta_buffer(text, PARSER_TEXT_NUL_TERMINATED, a, out_atoms);
}

int parse_metta_file(const char *filename, Arena *a, Atom ***out_atoms) {
    FILE *f = fopen(filename, "r");
    if (!f) return -1;

    char *text = NULL;
    size_t nread = 0;
    if (!read_all_text(f, &text, &nread)) {
        fclose(f);
        return -1;
    }
    fclose(f);

    int count = parse_metta_buffer(text, nread, a, out_atoms);
    free(text);
    return count;
}

int parse_metta_text_ids_diagnostic(const char *text,
                                    TermUniverse *universe,
                                    AtomId **out_ids, char *error_buf,
                                    size_t error_buf_size) {
    int result;
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!text) {
        if (out_ids)
            *out_ids = NULL;
        return -1;
    }
    if (g_document_ids_backend.parse_text_ids &&
        !g_document_ids_backend_active) {
        g_document_ids_backend_active = true;
        result = g_document_ids_backend.parse_text_ids(
            g_document_ids_backend.context, text, universe, out_ids,
            error_buf, error_buf_size);
        g_document_ids_backend_active = false;
        return result;
    }
    return parse_metta_buffer_ids(text, PARSER_TEXT_NUL_TERMINATED, universe,
                                  out_ids);
}

int parse_metta_text_ids(const char *text, TermUniverse *universe,
                         AtomId **out_ids) {
    return parse_metta_text_ids_diagnostic(
        text, universe, out_ids, NULL, 0u);
}

int parse_metta_file_ids_diagnostic(const char *filename,
                                    TermUniverse *universe,
                                    AtomId **out_ids, char *error_buf,
                                    size_t error_buf_size) {
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (g_document_ids_backend.parse_file_ids &&
        !g_document_ids_backend_active) {
        int result;
        g_document_ids_backend_active = true;
        result = g_document_ids_backend.parse_file_ids(
            g_document_ids_backend.context, filename, universe, out_ids,
            error_buf, error_buf_size);
        g_document_ids_backend_active = false;
        return result;
    }
    FILE *f = fopen(filename, "r");
    if (!f)
        return -1;

    char *text = NULL;
    size_t nread = 0;
    if (!read_all_text(f, &text, &nread)) {
        fclose(f);
        return -1;
    }
    fclose(f);

    int count = parse_metta_buffer_ids(text, nread, universe, out_ids);
    free(text);
    return count;
}

int parse_metta_file_ids(const char *filename, TermUniverse *universe,
                         AtomId **out_ids) {
    return parse_metta_file_ids_diagnostic(
        filename, universe, out_ids, NULL, 0u);
}

int parse_metta_file_prefix_ids(
    const char *filename, TermUniverse *universe, AtomId **out_ids,
    int *complete_count, char *error_buf, size_t error_buf_size) {
    if (out_ids)
        *out_ids = NULL;
    if (!out_ids || !complete_count)
        return -1;
    *complete_count = 0;
    if (g_document_ids_backend.parse_file_prefix_ids &&
        !g_document_ids_backend_active) {
        g_document_ids_backend_active = true;
        int result = g_document_ids_backend.parse_file_prefix_ids(
            g_document_ids_backend.context, filename, universe, out_ids,
            complete_count, error_buf, error_buf_size);
        g_document_ids_backend_active = false;
        return result;
    }
    int result = parse_metta_file_ids_diagnostic(
        filename, universe, out_ids, error_buf, error_buf_size);
    if (result >= 0)
        *complete_count = result;
    return result;
}

int parse_metta_bytes_prefix_ids(
    const uint8_t *bytes, size_t length, TermUniverse *universe, AtomId **out_ids,
    int *complete_count, char *error_buf, size_t error_buf_size) {
    if (out_ids)
        *out_ids = NULL;
    if (!out_ids || !complete_count || !bytes)
        return -1;
    *complete_count = 0;
    if (error_buf && error_buf_size)
        error_buf[0] = '\0';
    if (g_document_ids_backend.parse_bytes_prefix_ids &&
        !g_document_ids_backend_active) {
        g_document_ids_backend_active = true;
        int result = g_document_ids_backend.parse_bytes_prefix_ids(
            g_document_ids_backend.context, bytes, length, universe, out_ids,
            complete_count, error_buf, error_buf_size);
        g_document_ids_backend_active = false;
        return result;
    }
    int result = parse_metta_buffer_ids((const char *)bytes, length, universe, out_ids);
    if (result >= 0)
        *complete_count = result;
    return result;
}

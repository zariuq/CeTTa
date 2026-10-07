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
static __thread bool g_universal_name_syntax_enabled = false;
static __thread bool g_list_syntax_enabled = false;
static __thread ParserBareDollarMode g_bare_dollar_mode =
    CETTA_BARE_DOLLAR_DEFAULT_MODE;
static ParserDocumentIdsBackend g_document_ids_backend;
static __thread bool g_document_ids_backend_active = false;

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
static bool parser_at_list_bar(const char *text, size_t pos) {
    return text[pos] == '|' && !is_list_token_char(text[pos + 1u]);
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

const char *parser_canonicalize_namespace_token(Arena *a, const char *tok) {
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
    if (!scope || !name_key) return VAR_ID_NONE;
    if (!scope->name_keys)
        scope->name_keys = name_key_table_new(32u);
    if (!scope->name_keys) return VAR_ID_NONE;
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

    SymbolId head = expr->expr.elems[0]->sym_id;
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
            VarId id = parser_var_scope_id(scope, arg->sym_id);
            return atom_var_with_spelling(a, arg->sym_id, id);
        }
        Atom *key = NULL;
        if (parser_quote_payload(arg, &key)) {
            VarId id = parser_var_scope_name_id(scope, key);
            if (id != VAR_ID_NONE)
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
            return atom_symbol(a, legacy);
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
            if (id != VAR_ID_NONE) {
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

static AtomId parser_project_word_id(TermUniverse *universe, Arena *scratch,
                                     const char *tok) {
    char *endp;
    long long val;

    if (!universe || !scratch || !tok || !*tok)
        return CETTA_ATOM_ID_NONE;
    if (strcmp(tok, "True") == 0)
        return tu_intern_bool(universe, true);
    if (strcmp(tok, "False") == 0)
        return tu_intern_bool(universe, false);
    if (strcmp(tok, "PI") == 0)
        return tu_intern_float(universe, M_PI);
    if (strcmp(tok, "EXP") == 0)
        return tu_intern_float(universe, M_E);

    errno = 0;
    val = strtoll(tok, &endp, 10);
    if (*endp == '\0' && errno == 0)
        return tu_intern_int(universe, (int64_t)val);
    if (!strchr(tok, '.')) {
        if (g_rational_literals_enabled && strchr(tok, '/')) {
            AtomId rational_id = tu_intern_rational(universe, tok);
            if (rational_id != CETTA_ATOM_ID_NONE)
                return rational_id;
        }
        char *canonical = cetta_bigint_canonicalize_owned(tok);
        if (canonical) {
            free(canonical);
            return tu_intern_bigint(universe, tok);
        }
    }
    if (strchr(tok, '.')) {
        char *fendp;
        /* A value past the float range reads as the nearest float, zero, a
         * subnormal or an infinity, as Hyperon reads it; strtod reports
         * those with ERANGE. */
        double fval = strtod(tok, &fendp);
        if (*fendp == '\0')
            return tu_intern_float(universe, fval);
    }
    return tu_intern_symbol(
        universe, symbol_intern_cstr(
                      g_symbols,
                      parser_canonicalize_namespace_token(scratch, tok)));
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
    arena_init(&projection->scratch);
    arena_set_hashcons(&projection->scratch, NULL);
    projection->scratch_root = arena_mark(&projection->scratch);
    parser_var_scope_init(&projection->scope);
    return projection;
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
                      projection->universe, &projection->scratch, text)
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
    canonical = parser_canonicalize_namespace_token(
        &projection->scratch, text);
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

static AtomId parser_project_atom_id_scoped(
    ParserHostProjectionV1 *projection, Atom *atom, uint32_t depth) {
    if (!projection || !atom || depth == 0u)
        return CETTA_ATOM_ID_NONE;
    switch (atom->kind) {
    case ATOM_SYMBOL: {
        const char *bytes = symbol_bytes(g_symbols, atom->sym_id);
        size_t len = symbol_len(g_symbols, atom->sym_id);
        return parser_host_projection_v1_word_bytes(
            projection, (const uint8_t *)bytes, len);
    }
    case ATOM_VAR: {
        const char *bytes = symbol_bytes(g_symbols, atom->sym_id);
        size_t len = symbol_len(g_symbols, atom->sym_id);
        return len == 0u
                   ? parser_host_projection_v1_anonymous_variable(projection)
                   : parser_host_projection_v1_variable_bytes(
                         projection, (const uint8_t *)bytes, len);
    }
    case ATOM_GROUNDED:
        return term_universe_store_atom_id(
            projection->universe, &projection->scratch, atom);
    case ATOM_EXPR: {
        AtomId *children = NULL;
        AtomId result = CETTA_ATOM_ID_NONE;
        CettaExprLen arity = atom->expr.len;
        if (arity > 0u) {
            children = cetta_malloc(sizeof(*children) * (size_t)arity);
            if (!children)
                return CETTA_ATOM_ID_NONE;
        }
        for (CettaExprIndex index = 0u; index < arity; index++) {
            children[index] = parser_project_atom_id_scoped(
                projection, atom->expr.elems[index], depth - 1u);
            if (children[index] == CETTA_ATOM_ID_NONE)
                goto done;
        }
        result = parser_host_projection_v1_expression(
            projection, children, arity);
done:
        free(children);
        return result;
    }
    }
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

static Atom *parse_sexpr_scoped_in(Arena *a, const char *text,
                                   size_t text_len, size_t *pos,
                                   ParserVarScope *scope, int depth,
                                   bool in_list);

/* One term without its meta-arguments: a prefix's operand. */
static Atom *parse_term_scoped_in(Arena *a, const char *text,
                                  size_t text_len, size_t *pos,
                                  ParserVarScope *scope, int depth,
                                  bool in_list);

static Atom *parse_sexpr_scoped(Arena *a, const char *text, size_t text_len,
                                size_t *pos, ParserVarScope *scope, int depth) {
    return parse_sexpr_scoped_in(a, text, text_len, pos, scope, depth, false);
}

/* [x y] and [x y | rest]: elements separated by layout, then optionally the
 * bar alone and one rest element, in brackets. */
static Atom *parse_list_scoped(Arena *a, const char *text, size_t text_len,
                               size_t *pos, ParserVarScope *scope, int depth) {
    Atom **elems = NULL;
    uint32_t n = 0, cap = 0;
    Atom *rest = NULL;
    Atom *list = NULL;
    (*pos)++;
    for (;;) {
        skip_whitespace_and_comments(text, text_len, pos);
        if (parser_more(text, text_len, *pos) && text[*pos] == ']') {
            (*pos)++;
            list = atom_list(a, elems, n);
            goto done;
        }
        /* `:=` is reserved inside brackets. */
        if (g_universal_name_syntax_enabled &&
            parser_at_binding_mark(text, text_len, *pos))
            goto done;
        if (parser_more(text, text_len, *pos) && parser_at_list_bar(text, *pos)) {
            if (n == 0u)
                goto done;
            (*pos)++;
            skip_whitespace_and_comments(text, text_len, pos);
            if (g_universal_name_syntax_enabled &&
                parser_at_binding_mark(text, text_len, *pos))
                goto done;
            rest = parse_sexpr_scoped_in(a, text, text_len, pos, scope,
                                         depth - 1, true);
            skip_whitespace_and_comments(text, text_len, pos);
            if (!rest || !parser_more(text, text_len, *pos) || text[*pos] != ']')
                goto done;
            (*pos)++;
            list = atom_list_with_rest(a, elems, n, rest);
            goto done;
        }
        Atom *element = parse_sexpr_scoped_in(a, text, text_len, pos, scope,
                                              depth - 1, true);
        if (!element)
            goto done;
        if (n >= cap) {
            cap = cap ? cap * 2 : 8;
            elems = cetta_realloc(elems, sizeof(Atom *) * cap);
        }
        elems[n++] = element;
    }
done:
    free(elems);
    return list;
}

/* A braces node {x y ...} (Prime's universal name syntax): its elements
 * are read as an expression's. */
static Atom *parse_braces_scoped(Arena *a, const char *text, size_t text_len,
                                 size_t *pos, ParserVarScope *scope,
                                 int depth) {
    Atom **children = NULL;
    uint32_t n = 0, cap = 0;
    Atom *braces = NULL;
    (*pos)++;
    for (;;) {
        skip_whitespace_and_comments(text, text_len, pos);
        if (!parser_more(text, text_len, *pos))
            goto done;
        if (text[*pos] == '}') {
            (*pos)++;
            braces = atom_prime_braces(a, children, n);
            goto done;
        }
        Atom *child = parse_sexpr_scoped(a, text, text_len, pos, scope,
                                         depth - 1);
        if (!child)
            goto done;
        if (n >= cap) {
            cap = cap ? cap * 2 : 8;
            children = cetta_realloc(children, sizeof(Atom *) * cap);
        }
        children[n++] = child;
    }
done:
    free(children);
    return braces;
}

/* A touching bracket's elements, T[...]: list elements, among which the
 * binding mark is a word, with no rest. */
static Atom *parse_touching_list_scoped(Arena *a, const char *text,
                                        size_t text_len, size_t *pos,
                                        ParserVarScope *scope, int depth) {
    Atom **elems = NULL;
    uint32_t n = 0, cap = 0;
    Atom *list = NULL;
    (*pos)++;
    for (;;) {
        skip_whitespace_and_comments(text, text_len, pos);
        if (!parser_more(text, text_len, *pos))
            goto done;
        if (text[*pos] == ']') {
            (*pos)++;
            list = atom_list(a, elems, n);
            goto done;
        }
        if (parser_at_list_bar(text, *pos))
            goto done;
        Atom *element = parse_sexpr_scoped_in(a, text, text_len, pos, scope,
                                              depth - 1, true);
        if (!element)
            goto done;
        if (n >= cap) {
            cap = cap ? cap * 2 : 8;
            elems = cetta_realloc(elems, sizeof(Atom *) * cap);
        }
        elems[n++] = element;
    }
done:
    free(elems);
    return list;
}

/* The meta-arguments touching the end of `term`: T[...] and T{...} read as
 * (meta T [...]) and (meta T {...}), left to right.  The reader gives no
 * head a meaning. */
static Atom *parse_meta_arguments_scoped(Arena *a, const char *text,
                                         size_t text_len, size_t *pos,
                                         ParserVarScope *scope, int depth,
                                         Atom *term) {
    while (term && g_universal_name_syntax_enabled &&
           parser_more(text, text_len, *pos) &&
           (text[*pos] == '[' || text[*pos] == '{')) {
        if (depth <= 1)
            return NULL;
        Atom *argument = text[*pos] == '{'
            ? parse_braces_scoped(a, text, text_len, pos, scope, depth)
            : parse_touching_list_scoped(a, text, text_len, pos, scope,
                                         depth);
        if (!argument)
            return NULL;
        term = atom_expr3(a, atom_symbol(a, "meta"), term, argument);
    }
    return term;
}

static Atom *parse_sexpr_scoped_in(Arena *a, const char *text,
                                   size_t text_len, size_t *pos,
                                   ParserVarScope *scope, int depth,
                                   bool in_list) {
    Atom *term = parse_term_scoped_in(a, text, text_len, pos, scope, depth,
                                      in_list);
    return parse_meta_arguments_scoped(a, text, text_len, pos, scope, depth,
                                       term);
}

static Atom *parse_term_scoped_in(Arena *a, const char *text,
                                  size_t text_len, size_t *pos,
                                  ParserVarScope *scope, int depth,
                                  bool in_list) {
    if (depth <= 0)
        return NULL;
    skip_whitespace_and_comments(text, text_len, pos);
    if (!parser_more(text, text_len, *pos)) return NULL;

    if (g_universal_name_syntax_enabled &&
        text[*pos] == parser_syntax_compact_char(PARSER_SYNTAX_REF) &&
        text[*pos + 1u] == parser_syntax_compact_char(PARSER_SYNTAX_QUOTE)) {
        *pos += 2u;
        Atom *key = parse_term_scoped_in(a, text, text_len, pos, scope,
                                         depth - 1, in_list);
        if (!key || parser_var_scope_name_key_id(scope, key) == NAME_ID_NONE)
            return NULL;
        Atom *quoted = atom_expr2(
            a, atom_symbol_id(a, g_builtin_syms.quote), key);
        return atom_expr2(
            a, atom_symbol(a, "resolve-name"), quoted);
    }

    if (g_universal_name_syntax_enabled &&
        text[*pos] == parser_syntax_compact_char(PARSER_SYNTAX_VAR) &&
        text[*pos + 1u] == parser_syntax_compact_char(PARSER_SYNTAX_QUOTE)) {
        *pos += 2u;
        Atom *key = parse_term_scoped_in(a, text, text_len, pos, scope,
                                         depth - 1, in_list);
        if (!key) return NULL;
        VarId id = parser_var_scope_name_id(scope, key);
        return id == VAR_ID_NONE ? NULL : atom_var_with_name_key(a, key, id);
    }

    if (g_universal_name_syntax_enabled &&
        text[*pos] == parser_syntax_compact_char(PARSER_SYNTAX_QUOTE)) {
        (*pos)++;
        Atom *payload = parse_term_scoped_in(a, text, text_len, pos, scope,
                                             depth - 1, in_list);
        return payload
                   ? atom_expr2(a, atom_symbol_id(a, g_builtin_syms.quote),
                                payload)
                   : NULL;
    }

    if (g_universal_name_syntax_enabled &&
        text[*pos] == parser_syntax_compact_char(PARSER_SYNTAX_UNQUOTE) &&
        parser_more(text, text_len, *pos + 1u) &&
        !isspace((unsigned char)text[*pos + 1u]) &&
        text[*pos + 1u] != ')' && text[*pos + 1u] != ';' &&
        text[*pos + 1u] != '}' &&
        (!in_list || text[*pos + 1u] != ']')) {
        (*pos)++;
        Atom *payload = parse_term_scoped_in(a, text, text_len, pos, scope,
                                             depth - 1, in_list);
        return payload
                   ? atom_expr2(a, atom_symbol(a, "unquote"), payload)
                   : NULL;
    }

    /* String literal */
    if (text[*pos] == '"') {
        (*pos)++;
        size_t start = *pos;
        while (parser_more(text, text_len, *pos) && text[*pos] != '"') {
            if (text[*pos] == '\\' && parser_more(text, text_len, *pos + 1u))
                (*pos)++;
            (*pos)++;
        }
        if (!parser_more(text, text_len, *pos)) return NULL;
        size_t len = *pos - start;
        char *buf = arena_alloc(a, len + 1);
        size_t out = parser_decode_string_body(text, start, *pos, buf);
        (*pos)++;
        return atom_string_n(a, buf, out);
    }

    if (g_universal_name_syntax_enabled && text[*pos] == '{')
        return parse_braces_scoped(a, text, text_len, pos, scope, depth);

    if (g_list_syntax_enabled && text[*pos] == '[')
        return parse_list_scoped(a, text, text_len, pos, scope, depth);

    /* Expression */
    if (text[*pos] == '(') {
        (*pos)++;
        /* Collect children (dynamically sized) */
        Atom **children = NULL;
        uint32_t n = 0, ccap = 0;
        for (;;) {
            skip_whitespace_and_comments(text, text_len, pos);
            if (!parser_more(text, text_len, *pos) || text[*pos] == ')') break;
            Atom *child = parse_sexpr_scoped(a, text, text_len, pos, scope,
                                             depth - 1);
            if (!child) {
                free(children);
                return NULL;
            }
            if (n >= ccap) {
                ccap = ccap ? ccap * 2 : 16;
                children = cetta_realloc(children, sizeof(Atom *) * ccap);
            }
            children[n++] = child;
        }
        if (!parser_more(text, text_len, *pos) || text[*pos] != ')') {
            free(children);
            return NULL;
        }
        (*pos)++;
        Atom *expr = atom_expr(a, children, n);
        free(children);
        return parser_lower_syn_expression(a, expr, scope);
    }

    /* Token: symbol, variable, or number */
    size_t start = *pos;
    /* In Prime's reader brackets end every token. */
    while (parser_more(text, text_len, *pos) &&
           is_token_char_in(text[*pos],
                            in_list || g_universal_name_syntax_enabled))
        (*pos)++;
    size_t len = *pos - start;
    if (len == 0) return NULL;

    char *tok = arena_alloc(a, len + 1);
    memcpy(tok, text + start, len);
    tok[len] = '\0';

    /* Prime's bare `$` is a fresh anonymous variable. Named variables keep
       their ordinary per-form co-reference. */
    if (tok[0] == '$') {
        if (len > 1) {
            const char *spelling_text =
                parser_canonicalize_namespace_token(a, tok + 1);
            SymbolId spelling = symbol_intern_cstr(g_symbols, spelling_text);
            VarId id = parser_var_scope_id(scope, spelling);
            return atom_var_with_spelling(a, spelling, id);
        }
        if (parser_bare_dollar_is_variable()) {
            SymbolId spelling = symbol_intern_cstr(g_symbols, "");
            VarId id = parser_bare_dollar_id(scope, spelling);
            return atom_var_with_spelling(a, spelling, id);
        }
    }

    /* Boolean */
    if (strcmp(tok, "True") == 0)  return atom_bool(a, true);
    if (strcmp(tok, "False") == 0) return atom_bool(a, false);

    /* HE math.rs registers PI and EXP as grounded numeric tokens. */
    if (strcmp(tok, "PI") == 0)  return atom_float(a, M_PI);
    if (strcmp(tok, "EXP") == 0) return atom_float(a, M_E);

    /* Integer: try to parse */
    char *endp;
    errno = 0;
    long long val = strtoll(tok, &endp, 10);
    if (*endp == '\0' && errno == 0) {
        return atom_int(a, (int64_t)val);
    }
    if (!strchr(tok, '.')) {
        if (g_rational_literals_enabled && strchr(tok, '/')) {
            Atom *rational = atom_rational(a, tok);
            if (rational)
                return rational;
        }
        char *canonical = cetta_bigint_canonicalize_owned(tok);
        if (canonical) {
            free(canonical);
            return atom_bigint(a, tok);
        }
    }

    /* Float: try to parse (must contain '.') */
    if (strchr(tok, '.')) {
        char *fendp;
        /* Past the float range: the nearest float, as above. */
        double fval = strtod(tok, &fendp);
        if (*fendp == '\0') {
            return atom_float(a, fval);
        }
    }

    /* Symbol */
    return atom_symbol_id(a, symbol_intern_cstr(g_symbols,
                                                parser_canonicalize_namespace_token(a, tok)));
}

Atom *parse_sexpr_n(Arena *a, const char *text, size_t text_len, size_t *pos) {
    ParserVarScope scope;
    parser_var_scope_init(&scope);
    Atom *result = parse_sexpr_scoped(a, text, text_len, pos, &scope,
                                      CETTA_PARSE_DEPTH_LIMIT);
    parser_var_scope_free(&scope);
    return result;
}

Atom *parse_sexpr(Arena *a, const char *text, size_t *pos) {
    return parse_sexpr_n(a, text, PARSER_TEXT_NUL_TERMINATED, pos);
}

static AtomId parse_sexpr_to_id_scoped_in(TermUniverse *universe,
                                          Arena *scratch, const char *text,
                                          size_t text_len, size_t *pos,
                                          ParserVarScope *scope,
                                          int depth, bool in_list);

/* One term without its meta-arguments (parse_term_scoped_in). */
static AtomId parse_term_to_id_scoped_in(TermUniverse *universe,
                                         Arena *scratch, const char *text,
                                         size_t text_len, size_t *pos,
                                         ParserVarScope *scope,
                                         int depth, bool in_list);

static AtomId parse_sexpr_to_id_scoped(TermUniverse *universe, Arena *scratch,
                                       const char *text, size_t text_len,
                                       size_t *pos, ParserVarScope *scope,
                                       int depth) {
    return parse_sexpr_to_id_scoped_in(universe, scratch, text, text_len, pos,
                                       scope, depth, false);
}

static AtomId parse_list_to_id_scoped(TermUniverse *universe, Arena *scratch,
                                      const char *text, size_t text_len,
                                      size_t *pos, ParserVarScope *scope,
                                      int depth) {
    AtomId *elems = NULL;
    uint32_t n = 0, cap = 0;
    AtomId rest = CETTA_ATOM_ID_NONE;
    AtomId list = CETTA_ATOM_ID_NONE;
    (*pos)++;
    for (;;) {
        skip_whitespace_and_comments(text, text_len, pos);
        if (parser_more(text, text_len, *pos) && text[*pos] == ']') {
            (*pos)++;
            list = tu_list_from_ids(universe, elems, n, CETTA_ATOM_ID_NONE);
            goto done;
        }
        /* `:=` is reserved inside brackets. */
        if (g_universal_name_syntax_enabled &&
            parser_at_binding_mark(text, text_len, *pos))
            goto done;
        if (parser_more(text, text_len, *pos) && parser_at_list_bar(text, *pos)) {
            if (n == 0u)
                goto done;
            (*pos)++;
            skip_whitespace_and_comments(text, text_len, pos);
            if (g_universal_name_syntax_enabled &&
                parser_at_binding_mark(text, text_len, *pos))
                goto done;
            rest = parse_sexpr_to_id_scoped_in(
                universe, scratch, text, text_len, pos, scope, depth - 1, true);
            skip_whitespace_and_comments(text, text_len, pos);
            if (rest == CETTA_ATOM_ID_NONE || !parser_more(text, text_len, *pos) ||
                text[*pos] != ']')
                goto done;
            (*pos)++;
            list = tu_list_from_ids(universe, elems, n, rest);
            goto done;
        }
        AtomId element = parse_sexpr_to_id_scoped_in(
            universe, scratch, text, text_len, pos, scope, depth - 1, true);
        if (element == CETTA_ATOM_ID_NONE)
            goto done;
        if (n >= cap) {
            cap = cap ? cap * 2 : 8;
            elems = cetta_realloc(elems, sizeof(AtomId) * cap);
        }
        elems[n++] = element;
    }
done:
    free(elems);
    return list;
}

/* A braces node (parse_braces_scoped). */
static AtomId parse_braces_to_id_scoped(TermUniverse *universe,
                                        Arena *scratch, const char *text,
                                        size_t text_len, size_t *pos,
                                        ParserVarScope *scope, int depth) {
    AtomId *children = NULL;
    uint32_t n = 0, cap = 0;
    AtomId braces = CETTA_ATOM_ID_NONE;
    (*pos)++;
    for (;;) {
        skip_whitespace_and_comments(text, text_len, pos);
        if (!parser_more(text, text_len, *pos))
            goto done;
        if (text[*pos] == '}') {
            (*pos)++;
            braces = tu_braces_from_ids(universe, children, n);
            goto done;
        }
        AtomId child = parse_sexpr_to_id_scoped(universe, scratch, text,
                                                text_len, pos, scope,
                                                depth - 1);
        if (child == CETTA_ATOM_ID_NONE)
            goto done;
        if (n >= cap) {
            cap = cap ? cap * 2 : 8;
            children = cetta_realloc(children, sizeof(AtomId) * cap);
        }
        children[n++] = child;
    }
done:
    free(children);
    return braces;
}

/* A touching bracket's elements (parse_touching_list_scoped). */
static AtomId parse_touching_list_to_id_scoped(TermUniverse *universe,
                                               Arena *scratch,
                                               const char *text,
                                               size_t text_len, size_t *pos,
                                               ParserVarScope *scope,
                                               int depth) {
    AtomId *elems = NULL;
    uint32_t n = 0, cap = 0;
    AtomId list = CETTA_ATOM_ID_NONE;
    (*pos)++;
    for (;;) {
        skip_whitespace_and_comments(text, text_len, pos);
        if (!parser_more(text, text_len, *pos))
            goto done;
        if (text[*pos] == ']') {
            (*pos)++;
            list = tu_list_from_ids(universe, elems, n, CETTA_ATOM_ID_NONE);
            goto done;
        }
        if (parser_at_list_bar(text, *pos))
            goto done;
        AtomId element = parse_sexpr_to_id_scoped_in(
            universe, scratch, text, text_len, pos, scope, depth - 1, true);
        if (element == CETTA_ATOM_ID_NONE)
            goto done;
        if (n >= cap) {
            cap = cap ? cap * 2 : 8;
            elems = cetta_realloc(elems, sizeof(AtomId) * cap);
        }
        elems[n++] = element;
    }
done:
    free(elems);
    return list;
}

/* The meta-arguments touching the end of `term`
 * (parse_meta_arguments_scoped). */
static AtomId parse_meta_arguments_to_id_scoped(TermUniverse *universe,
                                                Arena *scratch,
                                                const char *text,
                                                size_t text_len, size_t *pos,
                                                ParserVarScope *scope,
                                                int depth, AtomId term) {
    while (term != CETTA_ATOM_ID_NONE && g_universal_name_syntax_enabled &&
           parser_more(text, text_len, *pos) &&
           (text[*pos] == '[' || text[*pos] == '{')) {
        if (depth <= 1)
            return CETTA_ATOM_ID_NONE;
        AtomId argument = text[*pos] == '{'
            ? parse_braces_to_id_scoped(universe, scratch, text, text_len,
                                        pos, scope, depth)
            : parse_touching_list_to_id_scoped(universe, scratch, text,
                                               text_len, pos, scope, depth);
        if (argument == CETTA_ATOM_ID_NONE)
            return CETTA_ATOM_ID_NONE;
        AtomId parts[3] = {
            tu_intern_symbol(universe, symbol_intern_cstr(g_symbols, "meta")),
            term, argument,
        };
        term = tu_expr_from_ids(universe, parts, 3u);
    }
    return term;
}

static AtomId parse_sexpr_to_id_scoped_in(TermUniverse *universe,
                                          Arena *scratch, const char *text,
                                          size_t text_len, size_t *pos,
                                          ParserVarScope *scope,
                                          int depth, bool in_list) {
    AtomId term = parse_term_to_id_scoped_in(
        universe, scratch, text, text_len, pos, scope, depth, in_list);
    return parse_meta_arguments_to_id_scoped(
        universe, scratch, text, text_len, pos, scope, depth, term);
}

static AtomId parse_term_to_id_scoped_in(TermUniverse *universe,
                                         Arena *scratch, const char *text,
                                         size_t text_len, size_t *pos,
                                         ParserVarScope *scope,
                                         int depth, bool in_list) {
    if (depth <= 0)
        return CETTA_ATOM_ID_NONE;
    skip_whitespace_and_comments(text, text_len, pos);
    if (!parser_more(text, text_len, *pos) || !universe)
        return CETTA_ATOM_ID_NONE;

    if (g_universal_name_syntax_enabled &&
        text[*pos] == parser_syntax_compact_char(PARSER_SYNTAX_REF) &&
        text[*pos + 1u] == parser_syntax_compact_char(PARSER_SYNTAX_QUOTE)) {
        *pos += 2u;
        Atom *key = parse_term_scoped_in(
            scratch, text, text_len, pos, scope, depth - 1, in_list);
        if (!key || parser_var_scope_name_key_id(scope, key) == NAME_ID_NONE)
            return CETTA_ATOM_ID_NONE;
        Atom *quoted = atom_expr2(
            scratch, atom_symbol_id(scratch, g_builtin_syms.quote), key);
        Atom *ref = atom_expr2(
            scratch, atom_symbol(scratch, "resolve-name"),
            quoted);
        return term_universe_store_atom_id(universe, scratch, ref);
    }

    if (g_universal_name_syntax_enabled &&
        text[*pos] == parser_syntax_compact_char(PARSER_SYNTAX_VAR) &&
        text[*pos + 1u] == parser_syntax_compact_char(PARSER_SYNTAX_QUOTE)) {
        *pos += 2u;
        Atom *key = parse_term_scoped_in(
            scratch, text, text_len, pos, scope, depth - 1, in_list);
        if (!key) return CETTA_ATOM_ID_NONE;
        VarId var_id = parser_var_scope_name_id(scope, key);
        if (var_id == VAR_ID_NONE) return CETTA_ATOM_ID_NONE;
        AtomId key_id = term_universe_store_atom_id(universe, scratch, key);
        return key_id == CETTA_ATOM_ID_NONE
                   ? CETTA_ATOM_ID_NONE
                   : tu_intern_named_var(universe, key_id, var_id);
    }

    if (g_universal_name_syntax_enabled &&
        (text[*pos] == parser_syntax_compact_char(PARSER_SYNTAX_QUOTE) ||
         (text[*pos] == parser_syntax_compact_char(PARSER_SYNTAX_UNQUOTE) &&
          parser_more(text, text_len, *pos + 1u) &&
          !isspace((unsigned char)text[*pos + 1u]) &&
          text[*pos + 1u] != ')' && text[*pos + 1u] != ';'))) {
        bool quote = text[*pos] ==
                     parser_syntax_compact_char(PARSER_SYNTAX_QUOTE);
        (*pos)++;
        Atom *payload = parse_term_scoped_in(
            scratch, text, text_len, pos, scope, depth - 1, in_list);
        if (!payload) return CETTA_ATOM_ID_NONE;
        Atom *form = atom_expr2(
            scratch,
            quote ? atom_symbol_id(scratch, g_builtin_syms.quote)
                  : atom_symbol(scratch, "unquote"),
            payload);
        return term_universe_store_atom_id(universe, scratch, form);
    }

    if (text[*pos] == '"') {
        (*pos)++;
        size_t start = *pos;
        while (parser_more(text, text_len, *pos) && text[*pos] != '"') {
            if (text[*pos] == '\\' && parser_more(text, text_len, *pos + 1u))
                (*pos)++;
            (*pos)++;
        }
        if (!parser_more(text, text_len, *pos))
            return CETTA_ATOM_ID_NONE;
        size_t len = *pos - start;
        char *buf = arena_alloc(scratch, len + 1);
        size_t out = parser_decode_string_body(text, start, *pos, buf);
        (*pos)++;
        return tu_intern_string_n(universe, buf, out);
    }

    if (g_universal_name_syntax_enabled && text[*pos] == '{')
        return parse_braces_to_id_scoped(universe, scratch, text, text_len,
                                         pos, scope, depth);

    if (g_list_syntax_enabled && text[*pos] == '[')
        return parse_list_to_id_scoped(universe, scratch, text, text_len, pos,
                                       scope, depth);

    if (text[*pos] == '(') {
        AtomId *children = NULL;
        uint32_t n = 0;
        uint32_t ccap = 0;
        AtomId expr_id = CETTA_ATOM_ID_NONE;
        (*pos)++;
        for (;;) {
            skip_whitespace_and_comments(text, text_len, pos);
            if (!parser_more(text, text_len, *pos) || text[*pos] == ')')
                break;
            AtomId child_id =
                parse_sexpr_to_id_scoped(universe, scratch, text, text_len,
                                         pos, scope, depth - 1);
            if (child_id == CETTA_ATOM_ID_NONE) {
                free(children);
                return CETTA_ATOM_ID_NONE;
            }
            if (n >= ccap) {
                ccap = ccap ? ccap * 2 : 16;
                children = cetta_realloc(children, sizeof(AtomId) * ccap);
            }
            children[n++] = child_id;
        }
        if (!parser_more(text, text_len, *pos) || text[*pos] != ')') {
            free(children);
            return CETTA_ATOM_ID_NONE;
        }
        (*pos)++;
        ParserSynLowerStatus lowered = parser_lower_syn_expression_id(
            universe, scratch, children, n, scope, &expr_id);
        if (lowered == PARSER_SYN_LOWER_NOT_FORM) {
            expr_id = tu_expr_from_ids(universe, children, n);
        } else if (lowered == PARSER_SYN_LOWER_INVALID) {
            expr_id = CETTA_ATOM_ID_NONE;
        }
        free(children);
        return expr_id;
    }

    size_t start = *pos;
    /* In Prime's reader brackets end every token. */
    while (parser_more(text, text_len, *pos) &&
           is_token_char_in(text[*pos],
                            in_list || g_universal_name_syntax_enabled))
        (*pos)++;
    size_t len = *pos - start;
    if (len == 0)
        return CETTA_ATOM_ID_NONE;

    char *tok = arena_alloc(scratch, len + 1);
    memcpy(tok, text + start, len);
    tok[len] = '\0';

    if (tok[0] == '$') {
        if (len > 1) {
            const char *spelling_text =
                parser_canonicalize_namespace_token(scratch, tok + 1);
            SymbolId spelling = symbol_intern_cstr(g_symbols, spelling_text);
            VarId id = parser_var_scope_id(scope, spelling);
            return tu_intern_var(universe, spelling, id);
        }
        if (parser_bare_dollar_is_variable()) {
            SymbolId spelling = symbol_intern_cstr(g_symbols, "");
            VarId id = parser_bare_dollar_id(scope, spelling);
            return tu_intern_var(universe, spelling, id);
        }
    }

    return parser_project_word_id(universe, scratch, tok);
}

AtomId parse_sexpr_to_id(TermUniverse *universe, const char *text, size_t *pos) {
    ParserVarScope scope;
    Arena scratch;
    AtomId result = CETTA_ATOM_ID_NONE;
    parser_var_scope_init(&scope);
    arena_init(&scratch);
    arena_set_hashcons(&scratch, NULL);
    result = parse_sexpr_to_id_scoped(universe, &scratch, text,
                                      PARSER_TEXT_NUL_TERMINATED, pos, &scope,
                                      CETTA_PARSE_DEPTH_LIMIT);
    arena_free(&scratch);
    parser_var_scope_free(&scope);
    return result;
}

bool parser_syn_exec_payload(Atom *form, Atom **payload_out) {
    if (payload_out) *payload_out = NULL;
    if (!form || form->kind != ATOM_EXPR || form->expr.len != 2u ||
        form->expr.elems[0]->kind != ATOM_SYMBOL ||
        !parser_symbol_is_syntax_form(
            form->expr.elems[0]->sym_id, PARSER_SYNTAX_EXEC)) {
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
    bool is_exec = parser_more(text, text_len, pos) &&
                   text[pos] == parser_syntax_compact_char(PARSER_SYNTAX_EXEC) &&
                   parser_more(text, text_len, pos + 1u) &&
                   !is_token_char(text[pos + 1u]);
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
        if (ctx->vars[i].id == id)
            return &ctx->vars[i];
    return NULL;
}

static bool parser_render_collect_vars(ParserRenderContext *ctx, Atom *atom,
                                       int depth) {
    if (!atom || depth <= 0)
        return false;
    if (atom->kind == ATOM_VAR) {
        if (parser_render_var_find(ctx, atom->var_id))
            return true;
        if (ctx->len == ctx->cap) {
            size_t next = ctx->cap ? ctx->cap * 2u : 16u;
            if (next < ctx->cap || next > SIZE_MAX / sizeof(*ctx->vars))
                return false;
            ctx->vars = cetta_realloc(ctx->vars, next * sizeof(*ctx->vars));
            ctx->cap = next;
        }
        ctx->vars[ctx->len++] = (ParserRenderVar){
            .id = atom->var_id,
            .named = atom->name_key != NULL,
            .spelling = atom->sym_id,
            .key = atom->name_key,
            .fresh_key = NULL,
            .collides = false,
        };
        return true;
    }
    if (atom->kind != ATOM_EXPR)
        return true;
    for (CettaExprIndex i = 0; i < atom->expr.len; i++)
        if (!parser_render_collect_vars(ctx, atom->expr.elems[i], depth - 1))
            return false;
    return true;
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

static bool parser_render_form(FILE *out, ParserRenderContext *ctx, Atom *atom,
                               ParserSyntaxPrintMode mode, int depth);

static bool parser_render_var(FILE *out, ParserRenderContext *ctx, Atom *atom,
                              ParserSyntaxPrintMode mode, int depth) {
    ParserRenderVar *entry = parser_render_var_find(ctx, atom->var_id);
    if (!entry)
        return false;
    Atom *key = entry->fresh_key ? entry->fresh_key : entry->key;
    bool named = key != NULL;
    bool compact = mode == PARSER_SYNTAX_PRINT_COMPACT &&
                   g_universal_name_syntax_enabled;
    const ParserSyntaxFormSpec *var_spec =
        parser_syntax_form(PARSER_SYNTAX_VAR);
    const ParserSyntaxFormSpec *quote_spec =
        parser_syntax_form(PARSER_SYNTAX_QUOTE);

    if (compact) {
        fputs(var_spec->compact, out);
        if (named) {
            fputs(quote_spec->compact, out);
            return parser_render_form(out, ctx, key, mode, depth - 1);
        }
        fputs(symbol_bytes(g_symbols, entry->spelling), out);
        return true;
    }

    fprintf(out, "(%s ", var_spec->expanded);
    if (named) {
        fprintf(out, "(%s ", quote_spec->expanded);
        if (!parser_render_form(out, ctx, key, PARSER_SYNTAX_PRINT_EXPANDED,
                                depth - 1)) {
            return false;
        }
        fputc(')', out);
    } else {
        fputs(symbol_bytes(g_symbols, entry->spelling), out);
    }
    fputc(')', out);
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
    case GV_FOREIGN:
    case GV_TERM_GRAPH:
    case GV_PRIME_NEED_CAPABILITY:
    case GV_PRIME_CONTEXT:
    case GV_INTERNAL_TAG:
        return false;
    }
    return false;
}

static bool parser_render_form(FILE *out, ParserRenderContext *ctx, Atom *atom,
                               ParserSyntaxPrintMode mode, int depth) {
    if (!out || !ctx || !atom || depth <= 0)
        return false;
    if (atom->kind == ATOM_VAR)
        return parser_render_var(out, ctx, atom, mode, depth);
    if (atom->kind == ATOM_GROUNDED)
        return parser_render_grounded(out, atom);
    if (atom->kind == ATOM_SYMBOL) {
        const char *name = atom_name_cstr(atom);
        if (mode == PARSER_SYNTAX_PRINT_EXPANDED && name[0] == '&' && name[1]) {
            fprintf(out, "(%s %s)",
                    parser_syntax_form(PARSER_SYNTAX_REF)->expanded, name + 1u);
        } else {
            fputs(name, out);
        }
        return true;
    }

    if (atom_is_list_form(atom)) {
        CettaExprLen len = atom->expr.len - 1u;
        bool open = atom_is_list_rest(atom);
        fputc('[', out);
        for (CettaExprIndex i = 0; i < len; i++) {
            if (i > 0u)
                fputs(open && i + 1u == len ? " | " : " ", out);
            if (!parser_render_form(out, ctx, atom->expr.elems[i + 1u], mode,
                                    depth - 1)) {
                return false;
            }
        }
        fputc(']', out);
        return true;
    }

    /* T[...] and T{...}, touching, as the atom printer writes them. */
    if (atom_is_prime_meta(atom))
        return parser_render_form(out, ctx, atom->expr.elems[1], mode,
                                  depth - 1) &&
               parser_render_form(out, ctx, atom->expr.elems[2], mode,
                                  depth - 1);

    /* {x y}, and an own list as its names ($y ...), as the atom printer
     * writes them. */
    if (atom_is_prime_braces(atom) || atom_is_prime_own_list(atom)) {
        bool braces = atom_is_prime_braces(atom);
        CettaExprIndex first = braces ? 1u : 2u;
        fputc(braces ? '{' : '(', out);
        for (CettaExprIndex i = first; i < atom->expr.len; i++) {
            if (i > first)
                fputc(' ', out);
            if (!parser_render_form(out, ctx, atom->expr.elems[i], mode,
                                    depth - 1))
                return false;
        }
        fputc(braces ? '}' : ')', out);
        return true;
    }

    Atom *head = atom->expr.len > 0u ? atom->expr.elems[0] : NULL;
    SymbolId head_id = head && head->kind == ATOM_SYMBOL
        ? head->sym_id : SYMBOL_ID_NONE;
    Atom *payload = atom->expr.len == 2u ? atom->expr.elems[1] : NULL;
    bool compact = mode == PARSER_SYNTAX_PRINT_COMPACT &&
                   g_universal_name_syntax_enabled;
    if (payload && head && atom_is_symbol(head, "resolve-name")) {
        Atom *key = NULL;
        if (parser_quote_payload(payload, &key)) {
            if (compact) {
                fputs(parser_syntax_form(PARSER_SYNTAX_REF)->compact, out);
                fputs(parser_syntax_form(PARSER_SYNTAX_QUOTE)->compact, out);
                return parser_render_form(out, ctx, key, mode, depth - 1);
            }
            fprintf(out, "(%s (%s ",
                    parser_syntax_form(PARSER_SYNTAX_REF)->expanded,
                    parser_syntax_form(PARSER_SYNTAX_QUOTE)->expanded);
            if (!parser_render_form(out, ctx, key,
                                    PARSER_SYNTAX_PRINT_EXPANDED, depth - 1)) {
                return false;
            }
            fputs("))", out);
            return true;
        }
    }
    if (payload && (head_id == g_builtin_syms.quote ||
                    atom_is_symbol(head, "unquote"))) {
        ParserSyntaxFormKind kind = head_id == g_builtin_syms.quote
            ? PARSER_SYNTAX_QUOTE : PARSER_SYNTAX_UNQUOTE;
        const ParserSyntaxFormSpec *spec = parser_syntax_form(kind);
        if (compact) {
            fputs(spec->compact, out);
            return parser_render_form(out, ctx, payload, mode, depth - 1);
        }
        fprintf(out, "(%s ", spec->expanded);
        if (!parser_render_form(out, ctx, payload,
                                PARSER_SYNTAX_PRINT_EXPANDED, depth - 1)) {
            return false;
        }
        fputc(')', out);
        return true;
    }
    if (payload &&
        parser_symbol_is_syntax_form(head_id, PARSER_SYNTAX_EXEC)) {
        const ParserSyntaxFormSpec *spec =
            parser_syntax_form(PARSER_SYNTAX_EXEC);
        if (mode == PARSER_SYNTAX_PRINT_COMPACT) {
            fputs(spec->compact, out);
            return parser_render_form(out, ctx, payload, mode, depth - 1);
        }
        fprintf(out, "(%s ", spec->expanded);
        if (!parser_render_form(out, ctx, payload,
                                PARSER_SYNTAX_PRINT_EXPANDED, depth - 1)) {
            return false;
        }
        fputc(')', out);
        return true;
    }

    /* An elaborated template's own list with no names carries only its
     * record and prints nothing. */
    CettaExprLen shown = atom->expr.len;
    if (atom_prime_template_own_hidden(atom))
        shown--;
    fputc('(', out);
    for (CettaExprIndex i = 0; i < shown; i++) {
        if (i > 0u) fputc(' ', out);
        if (!parser_render_form(out, ctx, atom->expr.elems[i], mode,
                                depth - 1)) {
            return false;
        }
    }
    fputc(')', out);
    return true;
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

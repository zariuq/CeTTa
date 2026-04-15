#include "parser.h"
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

/* ── Helpers ────────────────────────────────────────────────────────────── */

static void skip_whitespace_and_comments(const char *text, size_t *pos) {
    for (;;) {
        while (text[*pos] && isspace((unsigned char)text[*pos]))
            (*pos)++;
        if (text[*pos] == ';') {
            while (text[*pos] && text[*pos] != '\n')
                (*pos)++;
        } else {
            break;
        }
    }
}

static bool is_token_char(char c) {
    return c && !isspace((unsigned char)c) && c != '(' && c != ')' && c != ';' && c != '"';
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

static bool parser_text_well_formed(const char *text) {
    int depth = 0;
    for (size_t i = 0; text[i]; i++) {
        if (text[i] == ';') {
            while (text[i] && text[i] != '\n') i++;
            if (!text[i]) break;
            continue;
        }
        if (text[i] == '"') {
            i++;
            while (text[i] && text[i] != '"') {
                if (text[i] == '\\' && text[i + 1]) i++;
                i++;
            }
            if (!text[i]) return false;
            continue;
        }
        if (text[i] == '(') {
            depth++;
            continue;
        }
        if (text[i] == ')') {
            depth--;
            if (depth < 0) return false;
        }
    }
    return depth == 0;
}

typedef struct {
    SymbolId *spellings;
    VarId *ids;
    uint32_t len;
    uint32_t cap;
} ParserVarScope;

static void parser_var_scope_init(ParserVarScope *scope) {
    scope->spellings = NULL;
    scope->ids = NULL;
    scope->len = 0;
    scope->cap = 0;
}

static void parser_var_scope_free(ParserVarScope *scope) {
    free(scope->spellings);
    free(scope->ids);
    scope->spellings = NULL;
    scope->ids = NULL;
    scope->len = 0;
    scope->cap = 0;
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

/* ── Parse a single token or expression ─────────────────────────────────── */

static Atom *parse_sexpr_scoped(Arena *a, const char *text, size_t *pos,
                                ParserVarScope *scope) {
    skip_whitespace_and_comments(text, pos);
    if (!text[*pos]) return NULL;

    /* String literal */
    if (text[*pos] == '"') {
        (*pos)++;
        size_t start = *pos;
        while (text[*pos] && text[*pos] != '"') {
            if (text[*pos] == '\\' && text[*pos + 1]) (*pos)++;
            (*pos)++;
        }
        size_t len = *pos - start;
        char *buf = arena_alloc(a, len + 1);
        size_t out = 0;
        for (size_t i = start; i < *pos; i++) {
            if (text[i] == '\\' && i + 1 < *pos) {
                buf[out++] = decode_string_escape(text[++i]);
            } else {
                buf[out++] = text[i];
            }
        }
        buf[out] = '\0';
        if (text[*pos] == '"') (*pos)++;
        return atom_string(a, buf);
    }

    /* Expression */
    if (text[*pos] == '(') {
        (*pos)++;
        /* Collect children (dynamically sized) */
        Atom **children = NULL;
        uint32_t n = 0, ccap = 0;
        for (;;) {
            skip_whitespace_and_comments(text, pos);
            if (!text[*pos] || text[*pos] == ')') break;
            Atom *child = parse_sexpr_scoped(a, text, pos, scope);
            if (!child) break;
            if (n >= ccap) {
                ccap = ccap ? ccap * 2 : 16;
                children = cetta_realloc(children, sizeof(Atom *) * ccap);
            }
            children[n++] = child;
        }
        if (text[*pos] == ')') (*pos)++;
        Atom *expr = atom_expr(a, children, n);
        free(children);
        return expr;
    }

    /* Token: symbol, variable, or number */
    size_t start = *pos;
    while (is_token_char(text[*pos]))
        (*pos)++;
    size_t len = *pos - start;
    if (len == 0) return NULL;

    char *tok = arena_alloc(a, len + 1);
    memcpy(tok, text + start, len);
    tok[len] = '\0';

    /* Variable: starts with $ */
    if (tok[0] == '$' && len > 1) {
        const char *spelling_text = parser_canonicalize_namespace_token(a, tok + 1);
        SymbolId spelling = symbol_intern_cstr(g_symbols, spelling_text);
        VarId id = parser_var_scope_id(scope, spelling);
        return atom_var_with_spelling(a, spelling, id);
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

    /* Float: try to parse (must contain '.') */
    if (strchr(tok, '.')) {
        char *fendp;
        errno = 0;
        double fval = strtod(tok, &fendp);
        if (*fendp == '\0' && errno == 0) {
            return atom_float(a, fval);
        }
    }

    /* Symbol */
    return atom_symbol_id(a, symbol_intern_cstr(g_symbols,
                                                parser_canonicalize_namespace_token(a, tok)));
}

Atom *parse_sexpr(Arena *a, const char *text, size_t *pos) {
    ParserVarScope scope;
    parser_var_scope_init(&scope);
    Atom *result = parse_sexpr_scoped(a, text, pos, &scope);
    parser_var_scope_free(&scope);
    return result;
}

static AtomId parse_sexpr_to_id_scoped(TermUniverse *universe, Arena *scratch,
                                       const char *text, size_t *pos,
                                       ParserVarScope *scope) {
    skip_whitespace_and_comments(text, pos);
    if (!text[*pos] || !universe)
        return CETTA_ATOM_ID_NONE;

    if (text[*pos] == '"') {
        (*pos)++;
        size_t start = *pos;
        while (text[*pos] && text[*pos] != '"') {
            if (text[*pos] == '\\' && text[*pos + 1])
                (*pos)++;
            (*pos)++;
        }
        size_t len = *pos - start;
        char *buf = arena_alloc(scratch, len + 1);
        size_t out = 0;
        for (size_t i = start; i < *pos; i++) {
            if (text[i] == '\\' && i + 1 < *pos) {
                buf[out++] = decode_string_escape(text[++i]);
            } else {
                buf[out++] = text[i];
            }
        }
        buf[out] = '\0';
        if (text[*pos] == '"')
            (*pos)++;
        return tu_intern_string(universe, buf);
    }

    if (text[*pos] == '(') {
        AtomId *children = NULL;
        uint32_t n = 0;
        uint32_t ccap = 0;
        AtomId expr_id = CETTA_ATOM_ID_NONE;
        (*pos)++;
        for (;;) {
            skip_whitespace_and_comments(text, pos);
            if (!text[*pos] || text[*pos] == ')')
                break;
            AtomId child_id =
                parse_sexpr_to_id_scoped(universe, scratch, text, pos, scope);
            if (child_id == CETTA_ATOM_ID_NONE)
                break;
            if (n >= ccap) {
                ccap = ccap ? ccap * 2 : 16;
                children = cetta_realloc(children, sizeof(AtomId) * ccap);
            }
            children[n++] = child_id;
        }
        if (text[*pos] == ')')
            (*pos)++;
        expr_id = tu_expr_from_ids(universe, children, n);
        free(children);
        return expr_id;
    }

    size_t start = *pos;
    while (is_token_char(text[*pos]))
        (*pos)++;
    size_t len = *pos - start;
    if (len == 0)
        return CETTA_ATOM_ID_NONE;

    char *tok = arena_alloc(scratch, len + 1);
    memcpy(tok, text + start, len);
    tok[len] = '\0';

    if (tok[0] == '$' && len > 1) {
        const char *spelling_text =
            parser_canonicalize_namespace_token(scratch, tok + 1);
        SymbolId spelling = symbol_intern_cstr(g_symbols, spelling_text);
        VarId id = parser_var_scope_id(scope, spelling);
        return tu_intern_var(universe, spelling, id);
    }

    if (strcmp(tok, "True") == 0)
        return tu_intern_bool(universe, true);
    if (strcmp(tok, "False") == 0)
        return tu_intern_bool(universe, false);
    if (strcmp(tok, "PI") == 0)
        return tu_intern_float(universe, M_PI);
    if (strcmp(tok, "EXP") == 0)
        return tu_intern_float(universe, M_E);

    char *endp;
    errno = 0;
    long long val = strtoll(tok, &endp, 10);
    if (*endp == '\0' && errno == 0)
        return tu_intern_int(universe, (int64_t)val);

    if (strchr(tok, '.')) {
        char *fendp;
        errno = 0;
        double fval = strtod(tok, &fendp);
        if (*fendp == '\0' && errno == 0)
            return tu_intern_float(universe, fval);
    }

    return tu_intern_symbol(
        universe, symbol_intern_cstr(
                      g_symbols, parser_canonicalize_namespace_token(scratch, tok)));
}

AtomId parse_sexpr_to_id(TermUniverse *universe, const char *text, size_t *pos) {
    ParserVarScope scope;
    Arena scratch;
    AtomId result = CETTA_ATOM_ID_NONE;
    parser_var_scope_init(&scope);
    arena_init(&scratch);
    arena_set_hashcons(&scratch, NULL);
    result = parse_sexpr_to_id_scoped(universe, &scratch, text, pos, &scope);
    arena_free(&scratch);
    parser_var_scope_free(&scope);
    return result;
}

/* ── Parse entire file ──────────────────────────────────────────────────── */

static int parse_metta_buffer(const char *text, Arena *a, Atom ***out_atoms) {
    if (!parser_text_well_formed(text)) {
        *out_atoms = NULL;
        return -1;
    }

    Atom **atoms = NULL;
    int count = 0;
    int cap = 0;
    size_t pos = 0;
    for (;;) {
        Atom *at = parse_sexpr(a, text, &pos);
        if (!at) break;
        if (count >= cap) {
            cap = cap ? cap * 2 : 64;
            atoms = cetta_realloc(atoms, sizeof(Atom *) * (size_t)cap);
        }
        atoms[count++] = at;
    }

    *out_atoms = atoms;
    return count;
}

static int parse_metta_buffer_ids(const char *text, TermUniverse *universe,
                                  AtomId **out_ids) {
    Arena scratch;
    if (!out_ids)
        return -1;
    *out_ids = NULL;
    if (!text || !universe || !parser_text_well_formed(text))
        return -1;

    arena_init(&scratch);
    arena_set_hashcons(&scratch, NULL);

    AtomId *ids = NULL;
    int count = 0;
    int cap = 0;
    size_t pos = 0;
    for (;;) {
        ParserVarScope scope;
        ArenaMark mark = arena_mark(&scratch);
        AtomId id;
        parser_var_scope_init(&scope);
        id = parse_sexpr_to_id_scoped(universe, &scratch, text, &pos, &scope);
        parser_var_scope_free(&scope);
        arena_reset(&scratch, mark);
        if (id == CETTA_ATOM_ID_NONE)
            break;
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
    return parse_metta_buffer(text, a, out_atoms);
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

    int count = parse_metta_buffer(text, a, out_atoms);
    (void)nread;
    free(text);
    return count;
}

int parse_metta_text_ids(const char *text, TermUniverse *universe,
                         AtomId **out_ids) {
    if (!text) {
        if (out_ids)
            *out_ids = NULL;
        return -1;
    }
    return parse_metta_buffer_ids(text, universe, out_ids);
}

int parse_metta_file_ids(const char *filename, TermUniverse *universe,
                         AtomId **out_ids) {
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

    int count = parse_metta_buffer_ids(text, universe, out_ids);
    (void)nread;
    free(text);
    return count;
}

#include "parser.h"
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
    const char **names;
    VarId *ids;
    uint32_t len;
    uint32_t cap;
} ParserVarScope;

static void parser_var_scope_init(ParserVarScope *scope) {
    scope->names = NULL;
    scope->ids = NULL;
    scope->len = 0;
    scope->cap = 0;
}

static void parser_var_scope_free(ParserVarScope *scope) {
    free(scope->names);
    free(scope->ids);
    scope->names = NULL;
    scope->ids = NULL;
    scope->len = 0;
    scope->cap = 0;
}

static VarId parser_var_scope_id(ParserVarScope *scope, const char *name) {
    for (uint32_t i = 0; i < scope->len; i++) {
        if (strcmp(scope->names[i], name) == 0)
            return scope->ids[i];
    }
    if (scope->len >= scope->cap) {
        scope->cap = scope->cap ? scope->cap * 2 : 8;
        scope->names = cetta_realloc(scope->names, sizeof(const char *) * scope->cap);
        scope->ids = cetta_realloc(scope->ids, sizeof(VarId) * scope->cap);
    }
    VarId id = fresh_var_id();
    scope->names[scope->len] = name;
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
        VarId id = parser_var_scope_id(scope, tok + 1);
        return atom_var_with_id(a, tok + 1, id);
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
    return atom_symbol(a, tok);
}

Atom *parse_sexpr(Arena *a, const char *text, size_t *pos) {
    ParserVarScope scope;
    parser_var_scope_init(&scope);
    Atom *result = parse_sexpr_scoped(a, text, pos, &scope);
    parser_var_scope_free(&scope);
    return result;
}

/* ── Parse entire file ──────────────────────────────────────────────────── */

int parse_metta_file(const char *filename, Arena *a, Atom ***out_atoms) {
    FILE *f = fopen(filename, "r");
    if (!f) return -1;

    /* Read entire file */
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *text = cetta_malloc((size_t)fsize + 1);
    size_t nread = fread(text, 1, (size_t)fsize, f);
    text[nread] = '\0';
    fclose(f);

    if (!parser_text_well_formed(text)) {
        free(text);
        *out_atoms = NULL;
        return -1;
    }

    /* Parse top-level atoms */
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

    free(text);
    *out_atoms = atoms;
    return count;
}

#include "parser.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

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

/* ── Parse a single token or expression ─────────────────────────────────── */

Atom *parse_sexpr(Arena *a, const char *text, size_t *pos) {
    skip_whitespace_and_comments(text, pos);
    if (!text[*pos]) return NULL;

    /* String literal */
    if (text[*pos] == '"') {
        (*pos)++;
        size_t start = *pos;
        while (text[*pos] && text[*pos] != '"') {
            if (text[*pos] == '\\') (*pos)++;
            (*pos)++;
        }
        size_t len = *pos - start;
        char *buf = arena_alloc(a, len + 1);
        memcpy(buf, text + start, len);
        buf[len] = '\0';
        if (text[*pos] == '"') (*pos)++;
        return atom_string(a, buf);
    }

    /* Expression */
    if (text[*pos] == '(') {
        (*pos)++;
        /* Collect children */
        Atom *children[256];
        uint32_t n = 0;
        for (;;) {
            skip_whitespace_and_comments(text, pos);
            if (!text[*pos] || text[*pos] == ')') break;
            if (n >= 256) break;
            Atom *child = parse_sexpr(a, text, pos);
            if (!child) break;
            children[n++] = child;
        }
        if (text[*pos] == ')') (*pos)++;
        return atom_expr(a, children, n);
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
        return atom_var(a, tok + 1);
    }

    /* Boolean */
    if (strcmp(tok, "True") == 0)  return atom_bool(a, true);
    if (strcmp(tok, "False") == 0) return atom_bool(a, false);

    /* Integer: try to parse */
    char *endp;
    errno = 0;
    long val = strtol(tok, &endp, 10);
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

/* ── Parse entire file ──────────────────────────────────────────────────── */

int parse_metta_file(const char *filename, Arena *a, Atom ***out_atoms) {
    FILE *f = fopen(filename, "r");
    if (!f) return -1;

    /* Read entire file */
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *text = malloc((size_t)fsize + 1);
    if (!text) { fclose(f); return -1; }
    size_t nread = fread(text, 1, (size_t)fsize, f);
    text[nread] = '\0';
    fclose(f);

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
            atoms = realloc(atoms, sizeof(Atom *) * (size_t)cap);
        }
        atoms[count++] = at;
    }

    free(text);
    *out_atoms = atoms;
    return count;
}

/* tcalc_surface.c - a tcalc program written in f1r3lang or CHR notation,
 * translated into the canonical form (tcalc_surface.h).
 *
 * Both readers build a small tree of the program and write it out as
 * S-expressions, one top-level form per line.  Every row is written (put ROW),
 * so that no row is taken for a declaration.
 *
 *   1. Names of the notations
 *   2. Tokens
 *   3. Terms, shared by both notations
 *   4. Writing the canonical form
 *   5. f1r3lang: rows are data on channels, rules are receives
 *   6. CHR: rows are constraints, rules are CHR rules
 *   7. Printing rows in a notation */
#define _POSIX_C_SOURCE 200809L

#include "tcalc_surface.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* ======================================================================
 * 1. Names of the notations
 * ====================================================================== */

const char *tc_surface_name(TcSurface surface) {
    switch (surface) {
    case TC_SURFACE_RHO: return "rho";
    case TC_SURFACE_CHR: return "chr";
    default: return "tcalc";
    }
}

bool tc_surface_from_name(const char *name, TcSurface *surface) {
    static const TcSurface all[] = {TC_SURFACE_TCALC, TC_SURFACE_RHO, TC_SURFACE_CHR};
    for (size_t i = 0; name && i < sizeof all / sizeof all[0]; i++)
        if (strcmp(name, tc_surface_name(all[i])) == 0) { *surface = all[i]; return true; }
    return false;
}

static bool has_suffix(const char *path, const char *suffix) {
    size_t n = strlen(path), k = strlen(suffix);
    return n >= k && strcmp(path + n - k, suffix) == 0;
}

TcSurface tc_surface_of_path(const char *path) {
    if (path && has_suffix(path, ".rho")) return TC_SURFACE_RHO;
    if (path && has_suffix(path, ".chr")) return TC_SURFACE_CHR;
    return TC_SURFACE_TCALC;
}

/* ======================================================================
 * 2. Tokens
 *
 * A word is a name; in CHR a word that starts with a capital or _ is a
 * variable, in f1r3lang a variable is $ and a name.  Comments: // and /∗ ∗/ in
 * f1r3lang, % and /∗ ∗/ in CHR.
 * ====================================================================== */

typedef enum { TOK_END, TOK_WORD, TOK_VAR, TOK_NUMBER, TOK_STRING, TOK_QUOTED, TOK_PUNCT } TokenKind;

typedef struct {
    TokenKind kind;
    const char *text;   /* in the source; a variable without its $, a quoted atom without quotes */
    size_t length;
    size_t line, column;
} Token;

typedef struct { void **item; size_t len, cap; } List;

typedef struct {
    TcSurface surface;
    const char *text;
    size_t length, at, line, column;
    Token token;          /* the current token */
    TcSyntaxError *error;
    bool failed;
    void **owned; size_t owned_len, owned_cap;   /* everything allocated, freed together */
    unsigned anonymous;   /* CHR: each _ is a variable of its own */
} Reader;

static bool fail(Reader *r, size_t line, size_t column, const char *format, ...) {
    if (!r->failed) {
        r->failed = true;
        r->error->line = line;
        r->error->column = column;
        va_list ap;
        va_start(ap, format);
        vsnprintf(r->error->message, sizeof r->error->message, format, ap);
        va_end(ap);
    }
    return false;
}

static bool no_memory(Reader *r) {
    if (!r->failed) {
        r->failed = true;
        r->error->line = r->error->column = 0;
        r->error->message[0] = 0;
    }
    return false;
}

/* Zeroed memory that lives until the reader is done. */
static void *grab(Reader *r, size_t size) {
    if (r->failed) return NULL;
    if (r->owned_len == r->owned_cap) {
        size_t cap = r->owned_cap ? r->owned_cap * 2 : 64;
        void **grown = realloc(r->owned, cap * sizeof *grown);
        if (!grown) { no_memory(r); return NULL; }
        r->owned = grown;
        r->owned_cap = cap;
    }
    void *p = calloc(1, size ? size : 1);
    if (!p) { no_memory(r); return NULL; }
    r->owned[r->owned_len++] = p;
    return p;
}

static bool push(Reader *r, List *list, void *item) {
    if (r->failed) return false;
    if (list->len == list->cap) {
        size_t cap = list->cap ? list->cap * 2 : 4;
        void **grown = grab(r, cap * sizeof *grown);   /* the old block is freed at the end */
        if (!grown) return false;
        if (list->len) memcpy(grown, list->item, list->len * sizeof *grown);
        list->item = grown;
        list->cap = cap;
    }
    list->item[list->len++] = item;
    return true;
}

static void advance(Reader *r, size_t n) {
    for (size_t i = 0; i < n && r->at < r->length; i++, r->at++) {
        if (r->text[r->at] == '\n') { r->line++; r->column = 1; }
        else r->column++;
    }
}

static void skip_space(Reader *r) {
    for (;;) {
        while (r->at < r->length && isspace((unsigned char)r->text[r->at])) advance(r, 1);
        if (r->at >= r->length) return;
        const char *s = r->text + r->at;
        size_t left = r->length - r->at;
        bool line_comment = r->surface == TC_SURFACE_CHR ? s[0] == '%'
                                                          : left >= 2 && s[0] == '/' && s[1] == '/';
        if (line_comment) {
            while (r->at < r->length && r->text[r->at] != '\n') advance(r, 1);
        } else if (left >= 2 && s[0] == '/' && s[1] == '*') {
            size_t line = r->line, column = r->column;
            advance(r, 2);
            while (r->at + 1 < r->length && !(r->text[r->at] == '*' && r->text[r->at + 1] == '/'))
                advance(r, 1);
            if (r->at + 1 >= r->length) {
                fail(r, line, column, "this comment is not closed");
                r->at = r->length;
                return;
            }
            advance(r, 2);
        } else {
            return;
        }
    }
}

/* Longest first where one spelling begins another. */
static const char *const rho_punctuation[] = {
    "<<-", "<-", "<=", "==", "!=", "!!", "!?", "?!", "!", "(", ")", "{", "}", "[", "]",
    ",", "|", "&", ";", "@", "=", "*", NULL};
static const char *const chr_punctuation[] = {
    "<=>", "==>", "\\==", "==", "=", "\\+", "\\", ":-", "@", "|", ",", ".", "(", ")", "[", "]", NULL};

static bool word_char(char c) { return isalnum((unsigned char)c) || c == '_' || c == '\''; }

static void next(Reader *r) {
    skip_space(r);
    Token t = {TOK_END, r->text + r->at, 0, r->line, r->column};
    if (r->failed || r->at >= r->length) { r->token = t; return; }
    const char *s = r->text + r->at;
    size_t left = r->length - r->at, n = 0;
    char c = s[0];
    if (c == '"' || (c == '\'' && r->surface == TC_SURFACE_CHR)) {
        for (n = 1; n < left && s[n] != c; n++)
            if (s[n] == '\\' && n + 1 < left) n++;
        if (n >= left) { fail(r, t.line, t.column, "this quoted text is not closed"); r->token = t; return; }
        n++;
        if (c == '"') { t.kind = TOK_STRING; t.length = n; }
        else { t.kind = TOK_QUOTED; t.text = s + 1; t.length = n - 2; }
    } else if (isdigit((unsigned char)c) || (c == '-' && left > 1 && isdigit((unsigned char)s[1]))) {
        for (n = 1; n < left && isdigit((unsigned char)s[n]); n++) {}
        if (n + 1 < left && s[n] == '.' && isdigit((unsigned char)s[n + 1]))   /* a version: 0.1 */
            for (n += 1; n < left && isdigit((unsigned char)s[n]); n++) {}
        t.kind = TOK_NUMBER;
        t.length = n;
    } else if (c == '$' && r->surface == TC_SURFACE_RHO) {
        for (n = 1; n < left && word_char(s[n]); n++) {}
        if (n == 1) { fail(r, t.line, t.column, "a variable is $ and a name"); r->token = t; return; }
        t.kind = TOK_VAR;
        t.text = s + 1;
        t.length = n - 1;
    } else if (isalpha((unsigned char)c) || c == '_') {
        for (n = 1; n < left && (word_char(s[n]) && !(r->surface == TC_SURFACE_CHR && s[n] == '\'')); n++) {}
        bool variable = r->surface == TC_SURFACE_CHR && (isupper((unsigned char)c) || c == '_');
        t.kind = variable ? TOK_VAR : TOK_WORD;
        t.length = n;
    } else {
        const char *const *p = r->surface == TC_SURFACE_CHR ? chr_punctuation : rho_punctuation;
        while (*p && !(strlen(*p) <= left && memcmp(s, *p, strlen(*p)) == 0)) p++;
        if (!*p) {
            fail(r, t.line, t.column, "'%c' is not part of the %s notation", c, tc_surface_name(r->surface));
            r->token = t;
            return;
        }
        n = strlen(*p);
        t.kind = TOK_PUNCT;
        t.length = n;
    }
    advance(r, n);
    r->token = t;
}

static bool is_punct(const Reader *r, const char *p) {
    return r->token.kind == TOK_PUNCT && r->token.length == strlen(p) &&
           memcmp(r->token.text, p, r->token.length) == 0;
}

static bool is_word(const Reader *r, const char *w) {
    return r->token.kind == TOK_WORD && r->token.length == strlen(w) &&
           memcmp(r->token.text, w, r->token.length) == 0;
}

/* The first character after the current token, past white space. */
static char peek_char(const Reader *r) {
    size_t at = r->at;
    while (at < r->length && isspace((unsigned char)r->text[at])) at++;
    return at < r->length ? r->text[at] : 0;
}

static bool expected(Reader *r, const char *what) {
    if (r->token.kind == TOK_END)
        return fail(r, r->token.line, r->token.column, "expected %s, found the end", what);
    int shown = r->token.length > 24 ? 24 : (int)r->token.length;
    return fail(r, r->token.line, r->token.column, "expected %s, found '%.*s'", what, shown, r->token.text);
}

static bool expect(Reader *r, const char *p, const char *what) {
    if (!is_punct(r, p)) return expected(r, what);
    next(r);
    return true;
}

/* ======================================================================
 * 3. Terms, shared by both notations
 *
 *   term  ::= NAME | NAME ( term, ... ) | variable | number | "string"
 *           | @term                      (f1r3lang: a quoted term is the term)
 * ====================================================================== */

typedef struct Term {
    char kind;            /* 's' symbol, 'v' variable, 'n' number, 'q' string, 'a' application */
    const char *text;     /* the name, digits or quoted string; an application's head */
    size_t length;
    List arg;             /* an application's arguments */
    size_t line, column;
} Term;

static Term *make_term(Reader *r, char kind, const Token *t) {
    Term *x = grab(r, sizeof *x);
    if (!x) return NULL;
    x->kind = kind;
    x->text = t->text;
    x->length = t->length;
    x->line = t->line;
    x->column = t->column;
    return x;
}

/* A quoted CHR atom names a symbol of tcalc's notation, so it may not hold a
 * character that ends one there. */
static bool symbol_text(const char *s, size_t n) {
    if (n == 0) return false;
    for (size_t i = 0; i < n; i++)
        if (isspace((unsigned char)s[i]) || strchr("()\";$'", s[i])) return false;
    return true;
}

static Term *term(Reader *r) {
    Token t = r->token;
    switch (t.kind) {
    case TOK_VAR:
        next(r);
        if (r->surface == TC_SURFACE_CHR && t.length == 1 && t.text[0] == '_') {
            char *name = grab(r, 16);   /* _ is a variable no other place mentions */
            if (!name) return NULL;
            t.text = name;
            t.length = (size_t)snprintf(name, 16, "_%u", ++r->anonymous);
        }
        return make_term(r, 'v', &t);
    case TOK_NUMBER: next(r); return make_term(r, 'n', &t);
    case TOK_STRING: next(r); return make_term(r, 'q', &t);
    case TOK_QUOTED:
        if (!symbol_text(t.text, t.length))
            return fail(r, t.line, t.column, "a quoted atom here names a tcalc symbol, which has no space, "
                        "quote or parenthesis"), NULL;
        next(r);
        return make_term(r, 's', &t);
    case TOK_WORD: {
        next(r);
        if (!is_punct(r, "(")) return make_term(r, 's', &t);
        next(r);
        Term *x = make_term(r, 'a', &t);
        if (!x) return NULL;
        if (!is_punct(r, ")"))
            for (;;) {
                Term *a = term(r);
                if (!a || !push(r, &x->arg, a)) return NULL;
                if (!is_punct(r, ",")) break;
                next(r);
            }
        return expect(r, ")", "')' after the arguments") ? x : NULL;
    }
    case TOK_PUNCT:
        if (r->surface == TC_SURFACE_RHO && is_punct(r, "@")) { next(r); return term(r); }
        /* fall through */
    default:
        return expected(r, "a term: a name, name(...), a variable, a number or a string"), NULL;
    }
}

static bool same_variable(const Term *a, const Term *b) {
    return a->length == b->length && memcmp(a->text, b->text, a->length) == 0;
}

static bool mentions(const List *variables, const Term *v) {
    for (size_t i = 0; i < variables->len; i++)
        if (same_variable(variables->item[i], v)) return true;
    return false;
}

/* Adds the variables of t to `variables`, each once, in order of appearance. */
static void variables_of(Reader *r, List *variables, const Term *t) {
    if (t->kind == 'v' && !mentions(variables, t)) push(r, variables, (void *)t);
    for (size_t i = 0; i < t->arg.len; i++) variables_of(r, variables, t->arg.item[i]);
}

/* ======================================================================
 * 4. Writing the canonical form
 * ====================================================================== */

typedef struct {
    char *text; size_t length, cap;
    size_t *source_line; size_t lines, line_cap;
    bool failed;
} Out;

static void put_bytes(Out *o, const char *s, size_t n) {
    if (o->failed) return;
    if (o->length + n + 1 > o->cap) {
        size_t cap = o->cap ? o->cap : 4096;
        while (o->length + n + 1 > cap) cap *= 2;
        char *grown = realloc(o->text, cap);
        if (!grown) { o->failed = true; return; }
        o->text = grown;
        o->cap = cap;
    }
    memcpy(o->text + o->length, s, n);
    o->length += n;
    o->text[o->length] = 0;
}

static void put_text(Out *o, const char *s) { put_bytes(o, s, strlen(s)); }

/* Ends a top-level form, translated from the given source line. */
static void end_form(Out *o, size_t source_line) {
    put_bytes(o, "\n", 1);
    if (o->failed) return;
    if (o->lines == o->line_cap) {
        size_t cap = o->line_cap ? o->line_cap * 2 : 64;
        size_t *grown = realloc(o->source_line, cap * sizeof *grown);
        if (!grown) { o->failed = true; return; }
        o->source_line = grown;
        o->line_cap = cap;
    }
    o->source_line[o->lines++] = source_line;
}

static void put_term(Out *o, const Term *t) {
    if (t->kind == 'v') put_bytes(o, "$", 1);
    if (t->kind == 'a') put_bytes(o, "(", 1);
    put_bytes(o, t->text, t->length);
    for (size_t i = 0; i < t->arg.len; i++) {
        put_bytes(o, " ", 1);
        put_term(o, t->arg.item[i]);
    }
    if (t->kind == 'a') put_bytes(o, ")", 1);
}

/* A row that is a name alone, c, is the row (c): a constraint or a datum with
 * no arguments. */
static void put_row(Out *o, const Term *t) {
    if (t->kind == 's') {
        put_bytes(o, "(", 1);
        put_bytes(o, t->text, t->length);
        put_bytes(o, ")", 1);
    } else {
        put_term(o, t);
    }
}

/* ======================================================================
 * 5. f1r3lang
 *
 *   process ::= process | process  |  { process }  |  Nil  |  0
 *             | channel!(term, ...)                         a row
 *             | for (receipt; ...) { process }              a rule
 *             | contract channel(pattern, ...) = { process }
 *   receipt ::= bind & ... [where guard and ...]
 *   bind    ::= pattern, ... <- channel     take, the receive fires once
 *             | pattern, ... <= channel     take, the receive persists
 *             | pattern, ... <<- channel    read, the receive fires once
 *   guard   ::= term == term                a check
 *             | term = term                 a tell
 *             | absent(channel!(pattern, ...))   count(N, ...)   census(...)
 *   channel ::= NAME | $x | @term
 *
 * The datum c!(a, b) is the row (c a b), and a bind a, b <- c selects it.
 * A receive is a rule.  A receive that fires once is also present in the
 * store, as in f1r3lang: it is a row, taken by its firing.  A receive inside
 * another's body is made by that body, as that row, carrying the values the
 * body gives the variables it shares with the inner receive; an inner receive
 * that persists reads its row.  Receipts separated by ; nest, as in
 * f1r3lang.  f1r3lang gives every bind of a receive the mode of its first; a
 * receive here may mix the modes, each bind taking or reading its own row,
 * and persists when one of its binds is <=.
 * ====================================================================== */

typedef struct { Term *channel; List arg; size_t line, column; } Shape;   /* channel!(args) */

typedef struct { Shape shape; char mode; } Bind;   /* '-' <-, '=' <=, '<' <<- */

typedef struct {
    char kind;          /* '=' check, 't' tell, 'a' absent, 'n' count, 'c' census */
    Term *left, *right;
    Shape shape;
    Token count;
} Guard;

typedef struct Receive Receive;
typedef struct { List send; List receive; } Par;   /* Shape *, Receive * */

struct Receive {
    List bind, guard;   /* Bind *, Guard * */
    Par body;
    bool persists;
    size_t line, column;
};

static Term *channel(Reader *r) {
    Token t = r->token;
    if (t.kind == TOK_WORD) { next(r); return make_term(r, 's', &t); }
    if (t.kind == TOK_VAR) { next(r); return make_term(r, 'v', &t); }
    if (is_punct(r, "@")) { next(r); return term(r); }
    return expected(r, "a channel: a name, $x or @term"), NULL;
}

/* ( term, ... ) after a channel and its ! */
static bool shape_arguments(Reader *r, Shape *s) {
    if (!expect(r, "(", "'(' and the message")) return false;
    if (!is_punct(r, ")"))
        for (;;) {
            Term *a = term(r);
            if (!a || !push(r, &s->arg, a)) return false;
            if (!is_punct(r, ",")) break;
            next(r);
        }
    return expect(r, ")", "')' after the message");
}

static bool shape(Reader *r, Shape *s) {
    s->line = r->token.line;
    s->column = r->token.column;
    if (!(s->channel = channel(r))) return false;
    if (!expect(r, "!", "'!' and the message")) return false;
    return shape_arguments(r, s);
}

static bool process(Reader *r, Par *into);

static bool guard(Reader *r, Receive *x) {
    for (;;) {
        Guard *g = grab(r, sizeof *g);
        if (!g) return false;
        char kind = is_word(r, "absent") ? 'a' : is_word(r, "count") ? 'n' : is_word(r, "census") ? 'c' : 0;
        if (kind && peek_char(r) == '(') {
            g->kind = kind;
            next(r);
            next(r);
            if (kind == 'n') {
                if (r->token.kind != TOK_NUMBER) return expected(r, "the number of rows a count asks for");
                g->count = r->token;
                next(r);
                if (!expect(r, ",", "',' after the number")) return false;
            }
            if (!shape(r, &g->shape) || !expect(r, ")", "')'")) return false;
        } else {
            if (!(g->left = term(r))) return false;
            if (is_punct(r, "==")) g->kind = '=';
            else if (is_punct(r, "=")) g->kind = 't';
            else if (is_punct(r, "!="))
                return fail(r, r->token.line, r->token.column,
                            "tcalc@0.1 knows equations; a disequation (!=) has no meaning in its checks");
            else return expected(r, "a guard: A == B, A = B (told), absent(c!(...)), count(N, c!(...)) "
                                    "or census(c!(...))");
            next(r);
            if (!(g->right = term(r))) return false;
        }
        if (!push(r, &x->guard, g)) return false;
        if (!is_word(r, "and")) return true;
        next(r);
    }
}

static bool receipt(Reader *r, Receive *x) {
    x->line = r->token.line;
    x->column = r->token.column;
    for (;;) {
        Bind *b = grab(r, sizeof *b);
        if (!b) return false;
        bool arrow = is_punct(r, "<-") || is_punct(r, "<=") || is_punct(r, "<<-");
        if (!arrow)
            for (;;) {
                Term *p = term(r);
                if (!p || !push(r, &b->shape.arg, p)) return false;
                if (!is_punct(r, ",")) break;
                next(r);
            }
        b->mode = is_punct(r, "<-") ? '-' : is_punct(r, "<=") ? '=' : is_punct(r, "<<-") ? '<' : 0;
        if (!b->mode) return expected(r, "<-, <= or <<- in a bind");
        next(r);
        b->shape.line = r->token.line;
        b->shape.column = r->token.column;
        if (!(b->shape.channel = channel(r))) return false;
        if (b->mode == '=') x->persists = true;
        if (!push(r, &x->bind, b)) return false;
        if (!is_punct(r, "&")) break;
        next(r);
    }
    if (is_word(r, "where")) {
        next(r);
        return guard(r, x);
    }
    return true;
}

static bool receive_for(Reader *r, Par *into) {
    next(r);   /* for */
    if (!expect(r, "(", "'(' after for")) return false;
    List chain = {0};   /* the receipts, each nested in the one before */
    for (;;) {
        Receive *x = grab(r, sizeof *x);
        if (!x || !receipt(r, x) || !push(r, &chain, x)) return false;
        if (!is_punct(r, ";")) break;
        next(r);
    }
    if (!expect(r, ")", "')' after the receipts") || !expect(r, "{", "'{' and the body")) return false;
    Receive *last = chain.item[chain.len - 1];
    if (!process(r, &last->body) || !expect(r, "}", "'}' after the body")) return false;
    for (size_t k = chain.len - 1; k > 0; k--) {
        Receive *outer = chain.item[k - 1];
        if (!push(r, &outer->body.receive, chain.item[k])) return false;
    }
    return push(r, &into->receive, chain.item[0]);
}

static bool contract(Reader *r, Par *into) {
    Receive *x = grab(r, sizeof *x);
    Bind *b = grab(r, sizeof *b);
    if (!x || !b) return false;
    x->line = r->token.line;
    x->column = r->token.column;
    x->persists = true;
    next(r);   /* contract */
    b->mode = '=';
    b->shape.line = r->token.line;
    b->shape.column = r->token.column;
    if (!(b->shape.channel = channel(r)) || !expect(r, "(", "'(' and the parameters")) return false;
    if (!is_punct(r, ")"))
        for (;;) {
            Term *p = term(r);
            if (!p || !push(r, &b->shape.arg, p)) return false;
            if (!is_punct(r, ",")) break;
            next(r);
        }
    if (!expect(r, ")", "')' after the parameters") || !expect(r, "=", "'=' and the body") ||
        !expect(r, "{", "'{' and the body"))
        return false;
    if (!process(r, &x->body) || !expect(r, "}", "'}' after the body")) return false;
    return push(r, &x->bind, b) && push(r, &into->receive, x);
}

/* One process of a parallel composition, added to `into`. */
static bool process_one(Reader *r, Par *into) {
    Token t = r->token;
    if (is_punct(r, "{")) {
        next(r);
        return process(r, into) && expect(r, "}", "'}'");
    }
    if (is_word(r, "Nil") || (t.kind == TOK_NUMBER && t.length == 1 && t.text[0] == '0')) {
        next(r);
        return true;
    }
    if (is_word(r, "for")) return receive_for(r, into);
    if (is_word(r, "contract")) return contract(r, into);
    if (is_word(r, "new"))
        return fail(r, t.line, t.column, "new makes a name no other term equals; tcalc@0.1 has unknowns, "
                    "which a match may decide, and no such names");
    static const char *const elsewhere[] = {"select", "match", "if", "let", "bundle", "bundle+",
                                            "bundle-", "bundle0", NULL};
    for (const char *const *w = elsewhere; *w; w++)
        if (is_word(r, *w))
            return fail(r, t.line, t.column, "%s has no meaning in tcalc@0.1, whose programs are rows "
                        "and rules", *w);
    if (is_punct(r, "*"))
        return fail(r, t.line, t.column, "*x would run a received term; in tcalc@0.1 a received term is data");
    Shape *s = grab(r, sizeof *s);
    if (!s) return false;
    s->line = t.line;
    s->column = t.column;
    if (!(s->channel = channel(r))) return false;
    if (is_punct(r, "!!"))
        return fail(r, r->token.line, r->token.column,
                    "a datum that no receive consumes (!!) has no row in tcalc@0.1");
    if (is_punct(r, "!?") || is_punct(r, "?!"))
        return fail(r, r->token.line, r->token.column, "a synchronous send has no meaning in tcalc@0.1");
    if (!expect(r, "!", "'!' and the message of a send")) return false;
    return shape_arguments(r, s) && push(r, &into->send, s);
}

static bool process(Reader *r, Par *into) {
    for (;;) {
        if (!process_one(r, into)) return false;
        if (!is_punct(r, "|")) return true;
        next(r);
    }
}

static void variables_of_shape(Reader *r, List *v, const Shape *s) {
    variables_of(r, v, s->channel);
    for (size_t i = 0; i < s->arg.len; i++) variables_of(r, v, s->arg.item[i]);
}

/* The variables a receive mentions itself: in its binds, its guard and the
 * rows its body makes, not inside the receives its body makes. */
static void variables_of_level(Reader *r, List *v, const Receive *x) {
    for (size_t i = 0; i < x->bind.len; i++) variables_of_shape(r, v, &((Bind *)x->bind.item[i])->shape);
    for (size_t i = 0; i < x->guard.len; i++) {
        const Guard *g = x->guard.item[i];
        if (g->left) { variables_of(r, v, g->left); variables_of(r, v, g->right); }
        else variables_of_shape(r, v, &g->shape);
    }
    for (size_t i = 0; i < x->body.send.len; i++) variables_of_shape(r, v, x->body.send.item[i]);
}

static void variables_of_receive(Reader *r, List *v, const Receive *x) {
    variables_of_level(r, v, x);
    for (size_t i = 0; i < x->body.receive.len; i++) variables_of_receive(r, v, x->body.receive.item[i]);
}

/* The variables of x that the place it is made in can see: what its row carries. */
static void carried(Reader *r, List *out, const Receive *x, const List *visible) {
    List all = {0};
    variables_of_receive(r, &all, x);
    for (size_t i = 0; i < all.len; i++)
        if (mentions(visible, all.item[i])) push(r, out, all.item[i]);
}

static void put_shape(Out *o, const Shape *s) {
    put_bytes(o, "(", 1);
    put_term(o, s->channel);
    for (size_t i = 0; i < s->arg.len; i++) {
        put_bytes(o, " ", 1);
        put_term(o, s->arg.item[i]);
    }
    put_bytes(o, ")", 1);
}

static void receive_name(const Receive *x, char *name, size_t size) {
    snprintf(name, size, "rho_%zu_%zu", x->line, x->column);
}

/* The row that stands for a receive in the store. */
static void put_receive_row(Reader *r, Out *o, const Receive *x, const List *visible) {
    char name[48];
    receive_name(x, name, sizeof name);
    List carry = {0};
    carried(r, &carry, x, visible);
    put_bytes(o, "(", 1);
    put_text(o, name);
    for (size_t i = 0; i < carry.len; i++) {
        put_bytes(o, " ", 1);
        put_term(o, carry.item[i]);
    }
    put_bytes(o, ")", 1);
}

static void put_receive_rule(Reader *r, Out *o, const Receive *x, const List *visible, bool top) {
    char name[48];
    receive_name(x, name, sizeof name);
    List inner = {0};   /* what the receives this body makes can see */
    for (size_t i = 0; i < visible->len; i++) push(r, &inner, visible->item[i]);
    variables_of_level(r, &inner, x);
    put_text(o, "(rule ");
    put_text(o, name);
    if (!(top && x->persists)) {   /* the receive's own row */
        put_text(o, x->persists ? " (read " : " (take ");
        put_receive_row(r, o, x, visible);
        put_bytes(o, ")", 1);
    }
    for (int pass = 0; pass < 2; pass++) {   /* the takes, then the reads */
        bool any = false;
        for (size_t i = 0; i < x->bind.len; i++) {
            const Bind *b = x->bind.item[i];
            if ((b->mode == '<') != (pass == 1)) continue;
            put_text(o, any ? " " : pass ? " (read " : " (take ");
            put_shape(o, &b->shape);
            any = true;
        }
        if (any) put_bytes(o, ")", 1);
    }
    for (size_t i = 0; i < x->guard.len; i++) {
        const Guard *g = x->guard.item[i];
        if (g->kind == '=' || g->kind == 't') {
            put_text(o, g->kind == '=' ? " (check (= " : " (tell (= ");
            put_term(o, g->left);
            put_bytes(o, " ", 1);
            put_term(o, g->right);
            put_text(o, "))");
        } else {
            put_text(o, g->kind == 'a' ? " (absent " : g->kind == 'n' ? " (count " : " (census ");
            if (g->kind == 'n') { put_bytes(o, g->count.text, g->count.length); put_bytes(o, " ", 1); }
            put_shape(o, &g->shape);
            put_bytes(o, ")", 1);
        }
    }
    if (x->body.send.len || x->body.receive.len) {
        put_text(o, " (make");
        for (size_t i = 0; i < x->body.send.len; i++) {
            put_bytes(o, " ", 1);
            put_shape(o, x->body.send.item[i]);
        }
        for (size_t i = 0; i < x->body.receive.len; i++) {
            put_bytes(o, " ", 1);
            put_receive_row(r, o, x->body.receive.item[i], &inner);
        }
        put_bytes(o, ")", 1);
    }
    put_bytes(o, ")", 1);
    end_form(o, x->line);
    for (size_t i = 0; i < x->body.receive.len; i++)
        put_receive_rule(r, o, x->body.receive.item[i], &inner, false);
}

static void rho_program(Reader *r, Out *o) {
    Par top = {0};
    next(r);
    if (r->token.kind != TOK_END && process(r, &top) && r->token.kind != TOK_END)
        expected(r, "'|' and another process, or the end");
    if (r->failed) return;
    List none = {0};
    for (size_t i = 0; i < top.send.len; i++) {
        const Shape *s = top.send.item[i];
        put_text(o, "(put ");
        put_shape(o, s);
        put_bytes(o, ")", 1);
        end_form(o, s->line);
    }
    for (size_t i = 0; i < top.receive.len; i++) {
        const Receive *x = top.receive.item[i];
        put_receive_rule(r, o, x, &none, true);
        if (!x->persists) {   /* a receive that fires once is in the store from the start */
            put_text(o, "(put ");
            put_receive_row(r, o, x, &none);
            put_bytes(o, ")", 1);
            end_form(o, x->line);
        }
    }
}

/* ======================================================================
 * 6. CHR
 *
 *   clause ::= :- NAME(term, ...).                  a declaration: :- step(one).
 *            | constraint.                          a row
 *            | [NAME @] kept \ removed <=> [guard |] body.     simpagation
 *            | [NAME @] removed <=> [guard |] body.            simplification
 *            | [NAME @] kept ==> [guard |] body.               propagation
 *   guard  ::= goal, ...    X == Y   \+ constraint   count(N, c)   census(c)   true
 *   body   ::= goal, ...    X = Y (told)   constraint (made)   true
 *
 * Kept heads are read and removed heads are taken.  A constraint is a name or
 * name(term, ...), and a name alone c is the row (c).  A body may also make
 * the row a variable stands for.
 * ====================================================================== */

typedef struct { char kind; Term *left, *right; size_t line, column; } Goal;
/* kind: 'e' A == B, 't' A = B, 'a' \+ C, 'g' a term: a constraint, true, count(...), census(...) */

static Term *constraint(Reader *r) {
    Token t = r->token;
    Term *c = term(r);
    if (c && c->kind != 's' && c->kind != 'a')
        return fail(r, t.line, t.column, "a constraint is a name or name(...)"), NULL;
    return c;
}

static bool goals(Reader *r, List *out) {
    for (;;) {
        Goal *g = grab(r, sizeof *g);
        if (!g) return false;
        g->line = r->token.line;
        g->column = r->token.column;
        if (is_punct(r, "\\+")) {
            next(r);
            g->kind = 'a';
            if (!(g->left = constraint(r))) return false;
        } else {
            if (!(g->left = term(r))) return false;
            g->kind = is_punct(r, "==") ? 'e' : is_punct(r, "=") ? 't' : 'g';
            if (is_punct(r, "\\=="))
                return fail(r, r->token.line, r->token.column,
                            "tcalc@0.1 knows equations; a disequation (\\==) has no meaning in its checks");
            if (g->kind != 'g') {
                next(r);
                if (!(g->right = term(r))) return false;
            }
        }
        if (!push(r, out, g)) return false;
        if (!is_punct(r, ",")) return true;
        next(r);
    }
}

static bool is_name(const Term *t, const char *name, size_t args) {
    return (t->kind == 's' || t->kind == 'a') && t->arg.len == args &&
           t->length == strlen(name) && memcmp(t->text, name, t->length) == 0;
}

static bool put_guard(Reader *r, Out *o, const Goal *g) {
    if (g->kind == 'e') {
        put_text(o, " (check (= ");
        put_term(o, g->left);
        put_bytes(o, " ", 1);
        put_term(o, g->right);
        put_text(o, "))");
    } else if (g->kind == 'a') {
        put_text(o, " (absent ");
        put_row(o, g->left);
        put_bytes(o, ")", 1);
    } else if (is_name(g->left, "true", 0)) {
    } else if (is_name(g->left, "count", 2) && ((Term *)g->left->arg.item[0])->kind == 'n') {
        put_text(o, " (count ");
        put_term(o, g->left->arg.item[0]);
        put_bytes(o, " ", 1);
        put_row(o, g->left->arg.item[1]);
        put_bytes(o, ")", 1);
    } else if (is_name(g->left, "census", 1)) {
        put_text(o, " (census ");
        put_row(o, g->left->arg.item[0]);
        put_bytes(o, ")", 1);
    } else {
        return fail(r, g->line, g->column, "a guard goal is X == Y, \\+ c, count(N, c), census(c) or true");
    }
    return true;
}

static bool chr_rule(Reader *r, Out *o, Token name, List *first, size_t line, size_t column) {
    List kept = {0}, removed = {0};
    if (is_punct(r, "\\")) {
        kept = *first;
        next(r);
        for (;;) {
            Term *c = constraint(r);
            if (!c || !push(r, &removed, c)) return false;
            if (!is_punct(r, ",")) break;
            next(r);
        }
        if (!is_punct(r, "<=>")) return expected(r, "'<=>' after the removed heads");
    } else if (is_punct(r, "<=>")) {
        removed = *first;
    } else if (is_punct(r, "==>")) {
        kept = *first;
    } else {
        return expected(r, "'.', '\\', '<=>' or '==>' after the heads");
    }
    next(r);
    List guard = {0}, body = {0};
    if (!goals(r, &body)) return false;
    if (is_punct(r, "|")) {
        guard = body;
        body = (List){0};
        next(r);
        if (!goals(r, &body)) return false;
    }
    if (!expect(r, ".", "'.' at the end of the rule")) return false;
    char generated[48];
    put_text(o, "(rule ");
    if (name.length) put_bytes(o, name.text, name.length);
    else { snprintf(generated, sizeof generated, "chr_%zu_%zu", line, column); put_text(o, generated); }
    const List *heads[2] = {&removed, &kept};
    for (int k = 0; k < 2; k++)
        if (heads[k]->len) {
            put_text(o, k ? " (read" : " (take");
            for (size_t i = 0; i < heads[k]->len; i++) {
                put_bytes(o, " ", 1);
                put_row(o, heads[k]->item[i]);
            }
            put_bytes(o, ")", 1);
        }
    for (size_t i = 0; i < guard.len; i++)
        if (!put_guard(r, o, guard.item[i])) return false;
    bool made = false;
    for (size_t i = 0; i < body.len; i++) {
        const Goal *g = body.item[i];
        if (g->kind == 't') {
            put_text(o, " (tell (= ");
            put_term(o, g->left);
            put_bytes(o, " ", 1);
            put_term(o, g->right);
            put_text(o, "))");
        } else if (g->kind != 'g') {
            return fail(r, g->line, g->column, "a body goal is X = Y (told), a constraint (made) or true");
        }
    }
    for (size_t i = 0; i < body.len; i++) {
        const Goal *g = body.item[i];
        if (g->kind != 'g' || is_name(g->left, "true", 0)) continue;
        if (g->left->kind != 's' && g->left->kind != 'a' && g->left->kind != 'v')
            return fail(r, g->line, g->column, "a made constraint is a name, name(...) or a variable");
        put_text(o, made ? " " : " (make ");
        put_row(o, g->left);
        made = true;
    }
    if (made) put_bytes(o, ")", 1);
    put_bytes(o, ")", 1);
    end_form(o, line);
    return true;
}

static void chr_program(Reader *r, Out *o) {
    next(r);
    while (r->token.kind != TOK_END && !r->failed) {
        size_t line = r->token.line, column = r->token.column;
        if (is_punct(r, ":-")) {   /* a declaration */
            next(r);
            Term *d = term(r);
            if (!d || !expect(r, ".", "'.' at the end of the declaration")) return;
            if (d->kind != 'a') { fail(r, line, column, "a declaration is :- name(value, ...)."); return; }
            put_term(o, d);
            end_form(o, line);
            continue;
        }
        Token name = {0};
        if (r->token.kind == TOK_WORD && peek_char(r) == '@') {
            name = r->token;
            next(r);
            next(r);
        }
        List first = {0};
        for (;;) {
            Term *c = constraint(r);
            if (!c || !push(r, &first, c)) return;
            if (!is_punct(r, ",")) break;
            next(r);
        }
        if (is_punct(r, ".") && !name.length && first.len == 1) {   /* a row */
            next(r);
            put_text(o, "(put ");
            put_row(o, first.item[0]);
            put_bytes(o, ")", 1);
            end_form(o, line);
            continue;
        }
        if (!chr_rule(r, o, name, &first, line, column)) return;
    }
}

/* ======================================================================
 * The translation
 * ====================================================================== */

bool tc_surface_translate(TcSurface surface, const char *text, size_t length,
                          TcCanonical *out, TcSyntaxError *error) {
    memset(out, 0, sizeof *out);
    memset(error, 0, sizeof *error);
    Reader r = {.surface = surface, .text = text, .length = length, .line = 1, .column = 1, .error = error};
    Out o = {0};
    if (surface == TC_SURFACE_RHO) rho_program(&r, &o);
    else if (surface == TC_SURFACE_CHR) chr_program(&r, &o);
    else fail(&r, 1, 1, "a program in tcalc's own notation needs no translation");
    for (size_t i = 0; i < r.owned_len; i++) free(r.owned[i]);
    free(r.owned);
    if (o.failed) no_memory(&r);
    if (r.failed) {
        free(o.text);
        free(o.source_line);
        return false;
    }
    out->text = o.text ? o.text : calloc(1, 1);
    out->length = o.length;
    out->source_line = o.source_line;
    out->lines = o.lines;
    if (!out->text) { error->line = 0; return false; }
    return true;
}

void tc_canonical_free(TcCanonical *c) {
    if (!c) return;
    free(c->text);
    free(c->source_line);
    memset(c, 0, sizeof *c);
}

/* ======================================================================
 * 7. Printing rows in a notation
 * ====================================================================== */

static bool named(const TcTerms *t, TcTerm term) {
    return tc_kind(t, term) == TC_EXPRESSION && tc_length(t, term) > 0 &&
           tc_kind(t, tc_item(t, term, 0)) == TC_SYMBOL;
}

/* name(a, b) for an expression headed by a name; anything else as tcalc writes it. */
static void print_term(const TcTerms *t, TcTerm term, FILE *out) {
    if (!named(t, term)) { tc_print(t, term, out); return; }
    tc_print(t, tc_item(t, term, 0), out);
    fputc('(', out);
    for (uint32_t i = 1; i < tc_length(t, term); i++) {
        if (i > 1) fputs(", ", out);
        print_term(t, tc_item(t, term, i), out);
    }
    fputc(')', out);
}

void tc_surface_print_row(TcSurface surface, const TcTerms *t, TcTerm row, FILE *out) {
    bool expression = tc_kind(t, row) == TC_EXPRESSION && tc_length(t, row) > 0;
    if (surface == TC_SURFACE_RHO && expression) {   /* channel!(message) */
        TcTerm head = tc_item(t, row, 0);
        if (tc_kind(t, head) != TC_SYMBOL) fputc('@', out);
        print_term(t, head, out);
        fputs("!(", out);
        for (uint32_t i = 1; i < tc_length(t, row); i++) {
            if (i > 1) fputs(", ", out);
            print_term(t, tc_item(t, row, i), out);
        }
        fputc(')', out);
    } else if (surface == TC_SURFACE_CHR && named(t, row) && tc_length(t, row) == 1) {
        tc_print(t, tc_item(t, row, 0), out);   /* the row (c) is the constraint c */
    } else if (surface == TC_SURFACE_CHR) {
        print_term(t, row, out);
    } else {
        tc_print(t, row, out);
    }
}

/* ======================================================================
 * 8. Writing a program in a notation
 *
 * The reverse of sections 5 and 6: a program in the canonical form written in
 * f1r3lang or CHR notation.  What a notation has no way to write is refused
 * with its reason, never written as something else:
 *   rho  a rule that only reads (a receive whose binds all read fires once in
 *        f1r3lang, and none persists); a rule with no pattern; a pattern or a
 *        made row with no channel; a symbol that is no f1r3lang name; a region
 *        (receives have no names).  Declarations become the profile comment of
 *        the first line, since the notation has none.
 *   chr  a rule with no head; a row or pattern that is no constraint.
 * In CHR a variable $x is written X, and a rule name's '-' is written '_';
 * both only when no two names meet, else the program is refused.
 * ====================================================================== */

typedef struct Sexp {
    bool list;
    const char *text;   /* an atom's text */
    size_t length;
    List item;          /* a list's items */
    size_t line;
} Sexp;

static void sexp_skip(Reader *r) {
    for (;;) {
        while (r->at < r->length && isspace((unsigned char)r->text[r->at])) advance(r, 1);
        if (r->at >= r->length || r->text[r->at] != ';') return;
        while (r->at < r->length && r->text[r->at] != '\n') advance(r, 1);
    }
}

static Sexp *sexp_read(Reader *r) {
    sexp_skip(r);
    Sexp *x = grab(r, sizeof *x);
    if (!x) return NULL;
    x->line = r->line;
    const char *s = r->text + r->at;
    size_t left = r->length - r->at, n = 0;
    if (*s == '(') {
        x->list = true;
        advance(r, 1);
        for (;;) {
            sexp_skip(r);
            if (r->at >= r->length) return fail(r, x->line, 1, "a list is not closed"), NULL;
            if (r->text[r->at] == ')') { advance(r, 1); return x; }
            Sexp *e = sexp_read(r);
            if (!e || !push(r, &x->item, e)) return NULL;
        }
    }
    if (*s == ')') return fail(r, r->line, r->column, "')' closes no list"), NULL;
    if (*s == '"') {
        for (n = 1; n < left && s[n] != '"'; n++)
            if (s[n] == '\\' && n + 1 < left) n++;
        if (n >= left) return fail(r, r->line, r->column, "a string is not closed"), NULL;
        n++;
    } else {
        while (n < left && !isspace((unsigned char)s[n]) && !strchr("()\";", s[n])) n++;
    }
    x->text = s;
    x->length = n;
    advance(r, n);
    return x;
}

static bool sexp_is(const Sexp *x, const char *word) {
    return x && !x->list && x->length == strlen(word) && memcmp(x->text, word, x->length) == 0;
}

static bool sexp_variable(const Sexp *x) { return !x->list && x->text[0] == '$'; }

static bool sexp_literal(const Sexp *x) {   /* a number or a string */
    if (x->list) return false;
    char c = x->text[0];
    return c == '"' || isdigit((unsigned char)c) ||
           (c == '-' && x->length > 1 && isdigit((unsigned char)x->text[1]));
}

static bool sexp_symbol(const Sexp *x) { return !x->list && !sexp_variable(x) && !sexp_literal(x); }

static const Sexp *sexp_item(const Sexp *x, size_t i) {
    return x->list && i < x->item.len ? x->item.item[i] : NULL;
}

typedef struct {
    TcSurface surface;
    Reader *r;
    FILE *out;
    bool plain_variables;   /* CHR: $x written X (else V_x) */
} Writer;

static bool rho_word(const char *s, size_t n) {
    if (n == 0 || !(isalpha((unsigned char)s[0]) || s[0] == '_')) return false;
    for (size_t i = 1; i < n; i++)
        if (!(isalnum((unsigned char)s[i]) || s[i] == '_' || s[i] == '\'')) return false;
    return true;
}

static bool chr_word(const char *s, size_t n) {
    if (n == 0 || !islower((unsigned char)s[0])) return false;
    for (size_t i = 1; i < n; i++)
        if (!(isalnum((unsigned char)s[i]) || s[i] == '_')) return false;
    return true;
}

/* A CHR variable for $name: Name, or V_name when the program's names would
 * meet that way. */
static void chr_variable(const Writer *w, const Sexp *x, FILE *out) {
    const char *name = x->text + 1;
    int n = (int)(x->length - 1);
    if (n == 1 && name[0] == '_')   /* _ alone is CHR's anonymous variable */
        fputs("V__", out);
    else if (w->plain_variables && n > 0 && islower((unsigned char)name[0]))
        fprintf(out, "%c%.*s", toupper((unsigned char)name[0]), n - 1, name + 1);
    else if (w->plain_variables && n > 0 && (isupper((unsigned char)name[0]) || name[0] == '_'))
        fprintf(out, "%.*s", n, name);
    else
        fprintf(out, "V_%.*s", n, name);
}

static bool write_symbol(Writer *w, const Sexp *x) {
    if (w->surface == TC_SURFACE_RHO) {
        if (!rho_word(x->text, x->length))
            return fail(w->r, x->line, 1, "the symbol %.*s is no f1r3lang name", (int)x->length, x->text);
        fwrite(x->text, 1, x->length, w->out);
    } else if (chr_word(x->text, x->length)) {
        fwrite(x->text, 1, x->length, w->out);
    } else {
        if (!symbol_text(x->text, x->length))
            return fail(w->r, x->line, 1, "the symbol %.*s has no CHR spelling", (int)x->length, x->text);
        fprintf(w->out, "'%.*s'", (int)x->length, x->text);
    }
    return true;
}

static bool write_term(Writer *w, const Sexp *x) {
    if (sexp_variable(x)) {
        if (w->surface == TC_SURFACE_CHR) chr_variable(w, x, w->out);
        else fwrite(x->text, 1, x->length, w->out);
        return true;
    }
    if (sexp_literal(x)) { fwrite(x->text, 1, x->length, w->out); return true; }
    if (!x->list) return write_symbol(w, x);
    const Sexp *head = sexp_item(x, 0);
    if (!head || !sexp_symbol(head))
        return fail(w->r, x->line, 1, "a term whose head is not a name has no %s spelling",
                    tc_surface_name(w->surface));
    if (!write_symbol(w, head)) return false;
    fputc('(', w->out);
    for (size_t i = 1; i < x->item.len; i++) {
        if (i > 1) fputs(", ", w->out);
        if (!write_term(w, x->item.item[i])) return false;
    }
    fputc(')', w->out);
    return true;
}

/* The arguments of a row or pattern, from its second item on. */
static bool write_arguments(Writer *w, const Sexp *row) {
    for (size_t i = 1; i < row->item.len; i++) {
        if (i > 1) fputs(", ", w->out);
        if (!write_term(w, row->item.item[i])) return false;
    }
    return true;
}

/* rho: the channel of a row, a name or @term. */
static bool write_channel(Writer *w, const Sexp *row) {
    const Sexp *head = sexp_item(row, 0);
    if (!head)
        return fail(w->r, row->line, 1, "a row that is not an expression with a head has no channel");
    if (sexp_symbol(head)) return write_symbol(w, head);
    fputc('@', w->out);
    return write_term(w, head);
}

/* A row, a pattern, or (made) a made row; only a made row may be a variable,
 * the row the variable stands for, and only in CHR. */
static bool write_row_as(Writer *w, const Sexp *row, bool made) {
    if (w->surface == TC_SURFACE_CHR && made && sexp_variable(row)) {
        chr_variable(w, row, w->out);
        return true;
    }
    if (!row->list || !row->item.len)
        return fail(w->r, row->line, 1, "a row that is not an expression with a head has no %s spelling",
                    tc_surface_name(w->surface));
    if (w->surface == TC_SURFACE_RHO) {
        if (!write_channel(w, row)) return false;
        fputs("!(", w->out);
        if (!write_arguments(w, row)) return false;
        fputc(')', w->out);
        return true;
    }
    const Sexp *head = sexp_item(row, 0);
    if (!sexp_symbol(head))
        return fail(w->r, row->line, 1, "a row whose head is not a name is no CHR constraint");
    if (!write_symbol(w, head)) return false;
    if (row->item.len > 1) {
        fputc('(', w->out);
        if (!write_arguments(w, row)) return false;
        fputc(')', w->out);
    }
    return true;
}

static bool write_row(Writer *w, const Sexp *row) { return write_row_as(w, row, false); }

static bool write_equation(Writer *w, const Sexp *eq, const char *sign) {
    if (!eq->list || eq->item.len != 3 || !sexp_is(sexp_item(eq, 0), "="))
        return fail(w->r, eq->line, 1, "an equation is (= A B)");
    if (!write_term(w, sexp_item(eq, 1))) return false;
    fputs(sign, w->out);
    return write_term(w, sexp_item(eq, 2));
}

/* A check that is no equation: absent, count, census. */
static bool write_row_check(Writer *w, const Sexp *clause) {
    const Sexp *kind = sexp_item(clause, 0);
    bool count = sexp_is(kind, "count");
    const Sexp *row = sexp_item(clause, count ? 2 : 1);
    if (!row || clause->item.len != (count ? 3u : 2u))
        return fail(w->r, clause->line, 1, "a malformed check");
    if (w->surface == TC_SURFACE_CHR && sexp_is(kind, "absent")) {
        fputs("\\+ ", w->out);
        return write_row(w, row);
    }
    fwrite(kind->text, 1, kind->length, w->out);
    fputc('(', w->out);
    if (count) {
        if (!write_term(w, sexp_item(clause, 1))) return false;
        fputs(", ", w->out);
    }
    if (!write_row(w, row)) return false;
    fputc(')', w->out);
    return true;
}

/* rho: the binds of the patterns of one clause, each `args <= c` or `args <<- c`. */
static bool write_binds(Writer *w, const Sexp *clause, const char *arrow, bool *first) {
    for (size_t i = 1; i < clause->item.len; i++) {
        const Sexp *p = clause->item.item[i];
        if (!p->list || !p->item.len)
            return fail(w->r, p->line, 1, "a pattern that is not an expression with a head has no channel");
        if (!*first) fputs(" & ", w->out);
        *first = false;
        if (!write_arguments(w, p)) return false;
        fprintf(w->out, "%s%s ", p->item.len > 1 ? " " : "", arrow);
        if (!write_channel(w, p)) return false;
    }
    return true;
}

/* A rule name as CHR writes it ('-' as '_') into buf; false when it is no CHR name. */
static bool chr_rule_name(const Sexp *name, char *buf, size_t size) {
    if (!name || name->list || name->length >= size) return false;
    for (size_t i = 0; i < name->length; i++) buf[i] = name->text[i] == '-' ? '_' : name->text[i];
    buf[name->length] = 0;
    return chr_word(buf, name->length);
}

static bool write_rule(Writer *w, const Sexp *rule) {
    const Sexp *name = sexp_item(rule, 1);
    size_t takes = 0, reads = 0, makes = 0, guards = 0, tells = 0;
    if (!name || name->list)
        return fail(w->r, rule->line, 1, "a rule is (rule NAME CLAUSE ...)");
    for (size_t i = 2; i < rule->item.len; i++) {
        const Sexp *c = rule->item.item[i];
        const Sexp *kind = sexp_item(c, 0);
        size_t n = c->list && c->item.len ? c->item.len - 1 : 0;
        if (sexp_is(kind, "take")) takes += n;
        else if (sexp_is(kind, "read")) reads += n;
        else if (sexp_is(kind, "make")) makes += n;
        else if (sexp_is(kind, "tell")) tells += n;
        else if (sexp_is(kind, "check") || sexp_is(kind, "absent") || sexp_is(kind, "count") ||
                 sexp_is(kind, "census")) guards += sexp_is(kind, "check") ? n : 1;
        else return fail(w->r, c->line, 1, "a rule clause is take, read, tell, check, absent, count, census or make");
    }
    if (!takes && !reads)
        return fail(w->r, rule->line, 1, "a rule with no pattern has no %s spelling",
                    w->surface == TC_SURFACE_RHO ? "receive" : "CHR head");
    FILE *o = w->out;
    if (w->surface == TC_SURFACE_RHO) {
        if (!takes)
            return fail(w->r, rule->line, 1, "a rule that only reads has no f1r3lang spelling: a receive "
                        "whose binds all read (<<-) fires once, and none persists");
        bool first = true;
        fputs("for (", o);
        for (int pass = 0; pass < 2; pass++)
            for (size_t i = 2; i < rule->item.len; i++) {
                const Sexp *c = rule->item.item[i];
                if (sexp_is(sexp_item(c, 0), pass ? "read" : "take") &&
                    !write_binds(w, c, pass ? "<<-" : "<=", &first))
                    return false;
            }
        bool any = false;
        for (size_t i = 2; i < rule->item.len; i++) {
            const Sexp *c = rule->item.item[i];
            const Sexp *kind = sexp_item(c, 0);
            if (sexp_is(kind, "take") || sexp_is(kind, "read") || sexp_is(kind, "make")) continue;
            if (sexp_is(kind, "tell") || sexp_is(kind, "check")) {
                for (size_t j = 1; j < c->item.len; j++) {
                    fputs(any ? " and " : " where ", o);
                    any = true;
                    if (!write_equation(w, c->item.item[j], sexp_is(kind, "tell") ? " = " : " == "))
                        return false;
                }
            } else {
                fputs(any ? " and " : " where ", o);
                any = true;
                if (!write_row_check(w, c)) return false;
            }
        }
        fputs(") { ", o);
        bool made = false;
        for (size_t i = 2; i < rule->item.len; i++) {
            const Sexp *c = rule->item.item[i];
            if (!sexp_is(sexp_item(c, 0), "make")) continue;
            for (size_t j = 1; j < c->item.len; j++) {
                if (made) fputs(" | ", o);
                made = true;
                if (!write_row_as(w, c->item.item[j], true)) return false;
            }
        }
        fputs(made ? " }" : "Nil }", o);
        return true;
    }
    /* CHR: NAME @ kept \ removed <=> guard | body. */
    char rule_name[128];
    if (!chr_rule_name(name, rule_name, sizeof rule_name))
        return fail(w->r, rule->line, 1, "the rule name %.*s is no CHR name", (int)name->length, name->text);
    fprintf(o, "%s @ ", rule_name);
    for (int pass = 0; pass < 2; pass++) {   /* kept (read), then removed (take) */
        bool first = true;
        for (size_t i = 2; i < rule->item.len; i++) {
            const Sexp *c = rule->item.item[i];
            if (!sexp_is(sexp_item(c, 0), pass ? "take" : "read")) continue;
            for (size_t j = 1; j < c->item.len; j++) {
                if (!first) fputs(", ", o);
                first = false;
                if (!write_row(w, c->item.item[j])) return false;
            }
        }
        if (pass == 0 && reads && takes) fputs(" \\ ", o);
    }
    fputs(takes ? " <=> " : " ==> ", o);
    bool first = true;
    for (size_t i = 2; i < rule->item.len; i++) {
        const Sexp *c = rule->item.item[i];
        const Sexp *kind = sexp_item(c, 0);
        if (sexp_is(kind, "check")) {
            for (size_t j = 1; j < c->item.len; j++) {
                if (!first) fputs(", ", o);
                first = false;
                if (!write_equation(w, c->item.item[j], " == ")) return false;
            }
        } else if (sexp_is(kind, "absent") || sexp_is(kind, "count") || sexp_is(kind, "census")) {
            if (!first) fputs(", ", o);
            first = false;
            if (!write_row_check(w, c)) return false;
        }
    }
    if (guards) fputs(" | ", o);
    first = true;
    for (size_t i = 2; i < rule->item.len; i++) {
        const Sexp *c = rule->item.item[i];
        const Sexp *kind = sexp_item(c, 0);
        if (!sexp_is(kind, "tell") && !sexp_is(kind, "make")) continue;
        for (size_t j = 1; j < c->item.len; j++) {
            if (!first) fputs(", ", o);
            first = false;
            if (sexp_is(kind, "tell") ? !write_equation(w, c->item.item[j], " = ")
                                      : !write_row_as(w, c->item.item[j], true))
                return false;
        }
    }
    fputs(first ? "true.\n" : ".\n", o);
    (void)tells; (void)makes;
    return true;
}

static const char *const declaration_words[] = {
    "tcalc", "step", "ask", "store", "matches", "backend", "observe", "schedule", "budget", "region",
    NULL};

static bool is_declaration(const Sexp *form) {
    for (const char *const *d = declaration_words; *d; d++)
        if (sexp_is(sexp_item(form, 0), *d)) return true;
    return false;
}

/* The variables of a form, each spelling once. */
static void collect_variables(Reader *r, List *seen, const Sexp *x) {
    if (sexp_variable(x)) {
        for (size_t i = 0; i < seen->len; i++) {
            const Sexp *v = seen->item[i];
            if (v->length == x->length && memcmp(v->text, x->text, x->length) == 0) return;
        }
        push(r, seen, (void *)x);
        return;
    }
    for (size_t i = 0; x->list && i < x->item.len; i++) collect_variables(r, seen, x->item.item[i]);
}

/* Whether every variable's CHR spelling Name is its own. */
static bool plain_spellings_distinct(const List *vars) {
    for (size_t i = 0; i < vars->len; i++)
        for (size_t j = i + 1; j < vars->len; j++) {
            const Sexp *a = vars->item[i], *b = vars->item[j];
            if (a->length != b->length || a->length < 2) continue;
            if (toupper((unsigned char)a->text[1]) == toupper((unsigned char)b->text[1]) &&
                memcmp(a->text + 2, b->text + 2, a->length - 2) == 0)
                return false;
        }
    return true;
}

bool tc_surface_write(TcSurface surface, const char *text, size_t length, FILE *out,
                      TcSyntaxError *error) {
    memset(error, 0, sizeof *error);
    Reader r = {.surface = TC_SURFACE_TCALC, .text = text, .length = length, .line = 1, .column = 1,
                .error = error};
    List forms = {0}, vars = {0};
    if (surface == TC_SURFACE_TCALC) fail(&r, 1, 1, "the canonical form is written as it is");
    for (sexp_skip(&r); !r.failed && r.at < r.length; sexp_skip(&r)) {
        Sexp *form = sexp_read(&r);
        if (form) push(&r, &forms, form);
    }
    for (size_t i = 0; !r.failed && i < forms.len; i++) collect_variables(&r, &vars, forms.item[i]);
    for (size_t i = 0; !r.failed && surface == TC_SURFACE_CHR && i < forms.len; i++)
        for (size_t j = i + 1; j < forms.len; j++) {   /* names that '-' as '_' would make meet */
            const Sexp *a = forms.item[i], *b = forms.item[j];
            char na[128], nb[128];
            if (!sexp_is(sexp_item(a, 0), "rule") || !sexp_is(sexp_item(b, 0), "rule") ||
                !chr_rule_name(sexp_item(a, 1), na, sizeof na) ||
                !chr_rule_name(sexp_item(b, 1), nb, sizeof nb) || strcmp(na, nb) != 0)
                continue;
            fail(&r, b->line, 1, "two rule names are one CHR name, %s", na);
            break;
        }
    char *buf = NULL;
    size_t buf_length = 0;
    FILE *o = r.failed ? NULL : open_memstream(&buf, &buf_length);
    if (!r.failed && !o) no_memory(&r);
    Writer w = {.surface = surface, .r = &r, .out = o, .plain_variables = plain_spellings_distinct(&vars)};
    if (!r.failed && surface == TC_SURFACE_RHO) {   /* the declarations, as the profile comment */
        bool any = false;
        for (size_t i = 0; !r.failed && i < forms.len; i++) {
            const Sexp *f = forms.item[i];
            if (!is_declaration(f) || sexp_is(sexp_item(f, 0), "tcalc")) continue;
            if (sexp_is(sexp_item(f, 0), "region")) {
                fail(&r, f->line, 1, "a region names rules, and f1r3lang receives have no names");
                break;
            }
            const Sexp *v = sexp_item(f, 1);
            if (!v || v->list || f->item.len != 2) { fail(&r, f->line, 1, "a malformed declaration"); break; }
            fprintf(o, "%s%.*s=%.*s", any ? ";" : "// profile: ", (int)sexp_item(f, 0)->length,
                    sexp_item(f, 0)->text, (int)v->length, v->text);
            any = true;
        }
        if (any) fputc('\n', o);
    }
    bool first = true;
    for (size_t i = 0; !r.failed && i < forms.len; i++) {
        const Sexp *f = forms.item[i];
        bool ok = true;
        if (is_declaration(f)) {
            if (surface == TC_SURFACE_RHO) continue;
            bool region = sexp_is(sexp_item(f, 0), "region");
            fputs(":- ", o);
            ok = write_symbol(&w, sexp_item(f, 0));
            fputc('(', o);
            for (size_t j = 1; ok && j < f->item.len; j++) {
                char rule_name[128];
                if (j > 1) fputs(", ", o);
                if (!region) ok = write_term(&w, f->item.item[j]);
                else if (chr_rule_name(f->item.item[j], rule_name, sizeof rule_name)) fputs(rule_name, o);
                else ok = fail(&r, f->line, 1, "a region names a rule with no CHR name");
            }
            fputs(").\n", o);
        } else if (sexp_is(sexp_item(f, 0), "rule")) {
            if (surface == TC_SURFACE_RHO && !first) fputs(" |\n", o);
            first = false;
            ok = write_rule(&w, f);
        } else {
            const Sexp *row = sexp_is(sexp_item(f, 0), "put") && f->item.len == 2 ? sexp_item(f, 1) : f;
            if (surface == TC_SURFACE_RHO && !first) fputs(" |\n", o);
            first = false;
            ok = write_row(&w, row);
            if (ok && surface == TC_SURFACE_CHR) fputs(".\n", o);
        }
        if (!ok) break;
    }
    if (o && !r.failed && surface == TC_SURFACE_RHO) fputs(first ? "Nil\n" : "\n", o);
    if (o) fclose(o);
    if (!r.failed) fwrite(buf, 1, buf_length, out);
    free(buf);
    for (size_t i = 0; i < r.owned_len; i++) free(r.owned[i]);
    free(r.owned);
    return !r.failed;
}

#define _POSIX_C_SOURCE 200809L
#include "pp.h"

#include <stdlib.h>
#include <string.h>

/* ---- Phases 1 and 2 -------------------------------------------------------
   Lexing reads logical characters: trigraphs are replaced when the dialect
   reads them, \r\n and \r are new-lines, and line splices disappear. The
   positions stay raw byte offsets, so every token keeps its exact bytes. */

typedef struct { const unsigned char *data; size_t len; bool trigraphs; } Raw;

static unsigned phase1(const Raw *r, size_t pos, size_t *len) {
    unsigned char c = r->data[pos];
    if (c == '\r') { *len = pos+1 < r->len && r->data[pos+1] == '\n' ? 2 : 1; return '\n'; }
    if (c == '?' && r->trigraphs && pos+2 < r->len && r->data[pos+1] == '?' && r->data[pos+2]) {
        static const char from[] = "=(/)'<!>-", to[] = "#[\\]^{|}~";
        const char *hit = strchr(from,r->data[pos+2]);
        if (hit) { *len = 3; return (unsigned char)to[hit-from]; }
    }
    *len = 1; return c;
}

static bool horizontal(int c) { return c == ' ' || c == '\t' || c == '\f' || c == '\v'; }

/* The logical character at or after pos, skipping line splices. GCC and
   Clang also splice when only horizontal white space separates the
   backslash from the new-line. Returns -1 at the end of the file. */
static int logical(const Raw *r, size_t pos, size_t *start, size_t *next) {
    for (;;) {
        if (pos >= r->len) { *start = *next = r->len; return -1; }
        size_t n; unsigned c = phase1(r,pos,&n);
        if (c == '\\') {
            size_t q = pos+n;
            while (q < r->len && horizontal(r->data[q])) ++q;
            if (q < r->len && (r->data[q] == '\n' || r->data[q] == '\r')) {
                size_t m; (void)phase1(r,q,&m); pos = q+m; continue;
            }
        }
        *start = pos; *next = pos+n; return (int)c;
    }
}
static int get(const Raw *r, size_t *pos) {
    size_t start, next; int c = logical(r,*pos,&start,&next);
    if (c >= 0) *pos = next;
    return c;
}
static int look(const Raw *r, size_t pos) { size_t start, next; return logical(r,pos,&start,&next); }

/* ---- Phase 3 -------------------------------------------------------------- */

static bool letter(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static bool digit(int c) { return c >= '0' && c <= '9'; }
static bool hex(int c) { return digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
/* Identifier characters as GCC reads them: '$' and extended characters too. */
static bool nondigit(int c) { return letter(c) || c == '_' || c == '$' || c >= 0x80; }

static bool ucn(const Raw *r, size_t *pos) {
    size_t p = *pos;
    if (get(r,&p) != '\\') return false;
    int u = get(r,&p);
    unsigned digits = u == 'u' ? 4 : u == 'U' ? 8 : 0;
    if (!digits) return false;
    for (unsigned i = 0; i < digits; ++i) if (!hex(get(r,&p))) return false;
    *pos = p; return true;
}

static void identifier_rest(const Raw *r, size_t *pos) {
    for (;;) {
        size_t p = *pos; int c = get(r,&p);
        if (c >= 0 && (nondigit(c) || digit(c))) { *pos = p; continue; }
        if (c == '\\' && ucn(r,pos)) continue;
        return;
    }
}

/* ISO 6.4.8: a pp-number continues with digits, identifier characters,
   periods, and e, E, p or P followed by a sign. */
static void number_rest(const Raw *r, size_t *pos) {
    for (;;) {
        size_t p = *pos; int c = get(r,&p);
        if (c == 'e' || c == 'E' || c == 'p' || c == 'P') {
            size_t q = p; int sign = get(r,&q);
            *pos = sign == '+' || sign == '-' ? q : p; continue;
        }
        if (c >= 0 && (nondigit(c) || digit(c) || c == '.')) { *pos = p; continue; }
        if (c == '\\' && ucn(r,pos)) continue;
        return;
    }
}

/* After an opening quote: true at the closing quote. When the line ends
   first, pos stays before the new-line, and the token runs to there. */
static bool quoted(const Raw *r, size_t *pos, int quote) {
    for (;;) {
        size_t p = *pos; int c = get(r,&p);
        if (c < 0 || c == '\n') return false;
        *pos = p;
        if (c == quote) return true;
        if (c == '\\') { size_t q = *pos; int d = get(r,&q); if (d >= 0 && d != '\n') *pos = q; }
    }
}

/* ISO 6.4.7: a header-name, read only where 6.10.2 reads one. */
static bool header_name(const Raw *r, size_t *pos) {
    size_t p = *pos; int open = get(r,&p), close = open == '<' ? '>' : '"';
    for (bool any = false;; any = true) {
        int c = get(r,&p);
        if (c < 0 || c == '\n') return false;
        if (c == close) { if (!any) return false; *pos = p; return true; }
    }
}

/* A GNU C raw string literal from its opening quote. Its contents are the
   bytes as written; phases 1 and 2 do not apply inside it. Returns 0 when
   the delimiter is not a raw string's, -1 when it is never closed. */
static int raw_string(const Raw *r, size_t quote, size_t *end) {
    size_t p = quote+1, delimiter = p;
    while (p < r->len && r->data[p] != '(') {
        unsigned char c = r->data[p];
        if (p-delimiter >= 16 || strchr(" )\\\t\v\f\n\r\"",c) || !c) return 0;
        ++p;
    }
    if (p >= r->len) return 0;
    size_t length = p-delimiter;
    for (++p; p < r->len; ++p)
        if (r->data[p] == ')' && p+length+1 < r->len &&
            !memcmp(r->data+p+1,r->data+delimiter,length) && r->data[p+1+length] == '"') {
            *end = p+length+2; return 1;
        }
    return -1;
}

static const char *const punctuators[] = {
    "%:%:", "...", "<<=", ">>=", "->", "++", "--", "<<", ">>", "<=", ">=", "==", "!=",
    "&&", "||", "*=", "/=", "%=", "+=", "-=", "&=", "^=", "|=", "##", "<:", ":>",
    "<%", "%>", "%:", NULL
};

static bool punctuator(const Raw *r, size_t *pos) {
    int c[4]; size_t after[4], p = *pos;
    for (int i = 0; i < 4; ++i) { c[i] = get(r,&p); after[i] = p; }
    for (size_t k = 0; punctuators[k]; ++k) {
        size_t n = strlen(punctuators[k]); bool match = true;
        for (size_t i = 0; i < n && match; ++i) match = c[i] == (unsigned char)punctuators[k][i];
        if (match) { *pos = after[n-1]; return true; }
    }
    if (c[0] > 0 && c[0] < 128 && strchr("[](){}.&*+-~!/%<>^|?:;=,#",c[0])) { *pos = after[0]; return true; }
    return false;
}

/* ---- Grammar terminals ---------------------------------------------------- */

enum {
    D_IF, D_IFDEF, D_IFNDEF, D_ELIF, D_ELIFDEF, D_ELIFNDEF, D_ELSE, D_ENDIF,
    D_INCLUDE, D_INCLUDE_NEXT, D_IMPORT, D_EMBED, D_DEFINE, D_UNDEF, D_LINE,
    D_ERROR, D_PRAGMA, D_WARNING, D_IDENT, D_SCCS, D_ASSERT, D_UNASSERT, D_COUNT
};
static const char *const directive_names[D_COUNT] = {
    "if", "ifdef", "ifndef", "elif", "elifdef", "elifndef", "else", "endif",
    "include", "include_next", "import", "embed", "define", "undef", "line",
    "error", "pragma", "warning", "ident", "sccs", "assert", "unassert"
};

typedef struct {
    int directive[D_COUNT];
    int hash, comma, ellipsis, rparen;
    int elif_line, else_line, endif_line, lenient, line_marker;
    int kind[CE_PP_NEWLINE+1];
    int lparen;
} Symbols;

static int find_symbol(const CeGrammar *g, const char *name, unsigned kind) {
    for (size_t i = 0; i < g->nsymbols; ++i)
        if (g->symbols[i].kind == kind && ce_is(g->symbols[i].name,name)) return (int)i;
    return -1;
}

static bool symbols(const CeGrammar *g, Symbols *s, CeError *error) {
    for (int d = 0; d < D_COUNT; ++d) s->directive[d] = find_symbol(g,directive_names[d],CE_LITERAL);
    s->hash = find_symbol(g,"#",CE_LITERAL); s->comma = find_symbol(g,",",CE_LITERAL);
    s->ellipsis = find_symbol(g,"...",CE_LITERAL); s->rparen = find_symbol(g,")",CE_LITERAL);
    s->elif_line = find_symbol(g,"<elif-line>",CE_LITERAL); s->else_line = find_symbol(g,"<else-line>",CE_LITERAL);
    s->endif_line = find_symbol(g,"<endif-line>",CE_LITERAL); s->lenient = find_symbol(g,"<lenient>",CE_LITERAL);
    s->line_marker = find_symbol(g,"<line-marker>",CE_LITERAL);
    static const char *const kinds[] = {
        [CE_PP_HEADER_NAME] = "CPPHeaderName", [CE_PP_IDENTIFIER] = "CPPIdentifier",
        [CE_PP_NUMBER] = "CPPNumber", [CE_PP_CHARACTER] = "CPPCharacter", [CE_PP_STRING] = "CPPString",
        [CE_PP_PUNCTUATOR] = "CPPPunctuator", [CE_PP_OTHER] = "CPPOther",
        [CE_PP_UNTERMINATED] = "CPPUnterminated", [CE_PP_RAW_STRING] = "CPPRawString",
        [CE_PP_NEWLINE] = "CPPNewLine"
    };
    s->kind[CE_PP_MARKER] = -1;
    for (unsigned k = CE_PP_HEADER_NAME; k <= CE_PP_NEWLINE; ++k) s->kind[k] = find_symbol(g,kinds[k],CE_TOKEN);
    s->lparen = find_symbol(g,"CPPLParen",CE_TOKEN);
    bool ok = s->hash >= 0 && s->comma >= 0 && s->ellipsis >= 0 && s->rparen >= 0 && s->elif_line >= 0 &&
        s->else_line >= 0 && s->endif_line >= 0 && s->lenient >= 0 && s->line_marker >= 0 && s->lparen >= 0;
    for (int d = 0; d < D_COUNT; ++d) ok &= s->directive[d] >= 0;
    for (unsigned k = CE_PP_HEADER_NAME; k <= CE_PP_NEWLINE; ++k) ok &= s->kind[k] >= 0;
    return ok || ce_error(error,0,"the preprocessing grammar lacks a terminal the scanner forms");
}

bool ce_source_grammar(CeGrammar *g, CeText cf, CeError *error) {
    if (!ce_grammar_read(g,cf,error) || !ce_grammar_finish(g,error)) return false;
    /* Angle-bracket literals are markers: zero-width terminals the scanner
       inserts, which no source spelling forms. */
    for (size_t i = 0; i < g->nsymbols; ++i) {
        CeText name = g->symbols[i].name;
        if (g->symbols[i].kind == CE_LITERAL && name.len > 2 && name.data[0] == '<' && name.data[name.len-1] == '>')
            g->symbols[i].virtual_token = true;
    }
    Symbols s; return symbols(g,&s,error);
}

/* ---- Lines ---------------------------------------------------------------- */

typedef struct { CeSourceToken *data; size_t len, capacity; } Buffer;

static void push(Buffer *b, CeSourceToken t) {
    if (b->len == b->capacity) {
        b->capacity = b->capacity ? b->capacity*2 : 64;
        b->data = ce_resize(b->data,b->capacity,sizeof(*b->data));
    }
    b->data[b->len++] = t;
}

typedef struct {
    Raw raw;
    CeArena *arena;
    const Symbols *sym;
    Buffer line, out;
    bool raw_strings;
    size_t directives;
    CeError *error;
} Scanner;

static CeText value_of(Scanner *s, size_t begin, size_t end, unsigned kind) {
    CeText text = {(unsigned char *)s->raw.data+begin,end-begin};
    if (kind == CE_PP_RAW_STRING) return text;
    bool plain = true;
    for (size_t i = begin; i < end && plain; ++i) {
        unsigned char c = s->raw.data[i];
        plain = c != '\\' && c != '\r' && !(c == '?' && s->raw.trigraphs);
    }
    if (plain) return text;
    unsigned char *buffer = ce_alloc(s->arena,end-begin+1); size_t n = 0;
    for (size_t p = begin; p < end;) {
        int c = get(&s->raw,&p);
        if (c < 0) break;
        buffer[n++] = (unsigned char)c;
    }
    return (CeText){buffer,n};
}

static bool is_value(const CeSourceToken *t, unsigned kind, const char *text) {
    return t->kind == kind && ce_is(t->value,text);
}
static bool is_punct(const CeSourceToken *t, const char *text) { return is_value(t,CE_PP_PUNCTUATOR,text); }

static int directive_of(const CeSourceToken *t) {
    if (t->kind != CE_PP_IDENTIFIER) return -1;
    for (int d = 0; d < D_COUNT; ++d) if (ce_is(t->value,directive_names[d])) return d;
    return -1;
}

/* Whether a known directive has its required form (6.10 and GCC's). A
   define's shape also gives where its parameter list ends. */
static bool define_shape(const CeSourceToken *a, size_t n, bool *function, size_t *close) {
    *function = false;
    if (!n || a[0].kind != CE_PP_IDENTIFIER) return false;
    if (n < 2 || !is_punct(&a[1],"(") || a[1].space_before) return true;
    *function = true;
    size_t i = 2;
    if (i < n && is_punct(&a[i],")")) { *close = i; return true; }
    if (i < n && is_punct(&a[i],"...")) { ++i; *close = i; return i < n && is_punct(&a[i],")"); }
    for (;;) {
        if (i >= n || a[i].kind != CE_PP_IDENTIFIER) return false;
        if (++i >= n) return false;
        if (is_punct(&a[i],")")) { *close = i; return true; }
        if (is_punct(&a[i],"...")) { ++i; *close = i; return i < n && is_punct(&a[i],")"); }
        if (!is_punct(&a[i],",")) return false;
        if (++i < n && is_punct(&a[i],"...")) { ++i; *close = i; return i < n && is_punct(&a[i],")"); }
    }
}

static bool strict_form(int d, const CeSourceToken *a, size_t n) {
    switch (d) {
    case D_IF: case D_ELIF: case D_INCLUDE: case D_INCLUDE_NEXT: case D_IMPORT: case D_EMBED:
    case D_LINE: case D_IDENT: case D_SCCS: case D_ASSERT: case D_UNASSERT:
        return n >= 1;
    case D_IFDEF: case D_IFNDEF: case D_ELIFDEF: case D_ELIFNDEF: case D_UNDEF:
        return n == 1 && a[0].kind == CE_PP_IDENTIFIER;
    case D_ELSE: case D_ENDIF:
        return n == 0;
    case D_DEFINE: { bool function; size_t close; return define_shape(a,n,&function,&close); }
    default:
        return true;
    }
}

static void emit(Scanner *s, CeSourceToken t, int symbol) { t.symbol = symbol; push(&s->out,t); }

static void emit_marker(Scanner *s, size_t at, int symbol) {
    CeSourceToken t = {.symbol = symbol, .kind = CE_PP_MARKER, .trivia = at, .begin = at, .end = at,
                       .value = {(unsigned char *)s->raw.data+at,0}};
    push(&s->out,t);
}

static void emit_tokens(Scanner *s, const CeSourceToken *t, size_t n) {
    for (size_t i = 0; i < n; ++i) emit(s,t[i],s->sym->kind[t[i].kind]);
}

/* A logical line is complete: classify a directive and give each token its
   terminal. The line's last token is its new-line. */
static void finish_line(Scanner *s) {
    const Symbols *y = s->sym;
    CeSourceToken *t = s->line.data; size_t n = s->line.len, operands = n > 2 ? n-3 : 0;
    bool directive = n > 1 && (is_punct(&t[0],"#") || is_punct(&t[0],"%:"));
    if (!directive) { emit_tokens(s,t,n); s->line.len = 0; return; }
    ++s->directives;
    int d = n > 2 ? directive_of(&t[1]) : -1;
    if (d < 0) {
        t[0].directive = n == 2 ? CE_DIRECTIVE_NULL : t[1].kind == CE_PP_NUMBER ? CE_DIRECTIVE_LINE_MARKER : CE_DIRECTIVE_NON;
        emit(s,t[0],y->hash);
        if (n > 2 && t[1].kind == CE_PP_NUMBER) emit_marker(s,t[1].trivia,y->line_marker);
        emit_tokens(s,t+1,n-1);
        s->line.len = 0; return;
    }
    const CeSourceToken *a = t+2;
    bool strict = strict_form(d,a,operands);
    t[0].directive = (unsigned char)(CE_DIRECTIVE_IF+d); t[0].lenient = !strict;
    if (d == D_ELIF || d == D_ELIFDEF || d == D_ELIFNDEF) emit_marker(s,t[0].trivia,y->elif_line);
    if (d == D_ELSE) emit_marker(s,t[0].trivia,y->else_line);
    if (d == D_ENDIF) emit_marker(s,t[0].trivia,y->endif_line);
    emit(s,t[0],y->hash);
    if (!strict) emit_marker(s,t[1].trivia,y->lenient);
    emit(s,t[1],y->directive[d]);
    size_t from = 0;
    if (strict && d == D_DEFINE) {
        bool function; size_t close = 0;
        (void)define_shape(a,operands,&function,&close);
        emit(s,a[0],y->kind[CE_PP_IDENTIFIER]); from = 1;
        if (function) {
            emit(s,a[1],y->lparen);
            for (size_t i = 2; i < close; ++i)
                emit(s,a[i],a[i].kind == CE_PP_IDENTIFIER ? y->kind[CE_PP_IDENTIFIER] :
                     is_punct(&a[i],",") ? y->comma : y->ellipsis);
            emit(s,a[close],y->rparen); from = close+1;
        }
    }
    emit_tokens(s,a+from,operands-from);
    emit_tokens(s,t+n-1,1);
    s->line.len = 0;
}

/* Whether the next token of this line is read as a header-name: the
   operand of #include and its relatives, and the operand of
   __has_include in a conditional. */
static bool header_context(const Scanner *s) {
    const CeSourceToken *t = s->line.data; size_t n = s->line.len;
    if (n < 2 || !(is_punct(&t[0],"#") || is_punct(&t[0],"%:"))) return false;
    int d = directive_of(&t[1]);
    if (n == 2) return d == D_INCLUDE || d == D_INCLUDE_NEXT || d == D_IMPORT || d == D_EMBED;
    if (d != D_IF && d != D_ELIF) return false;
    return n >= 4 && is_punct(&t[n-1],"(") &&
        (is_value(&t[n-2],CE_PP_IDENTIFIER,"__has_include") || is_value(&t[n-2],CE_PP_IDENTIFIER,"__has_include_next") ||
         is_value(&t[n-2],CE_PP_IDENTIFIER,"__has_embed"));
}

static bool prefix(CeText v, const char *const *list) {
    for (size_t i = 0; list[i]; ++i) if (ce_is(v,list[i])) return true;
    return false;
}

/* One pp-token from start, whose first logical character is c. */
static int lex(Scanner *s, size_t start, int c, bool header, size_t *end, unsigned *kind) {
    const Raw *r = &s->raw; size_t p = start;
    if (header && (c == '<' || c == '"') && header_name(r,&p)) { *end = p; *kind = CE_PP_HEADER_NAME; return 1; }
    p = start;
    if (nondigit(c) || (c == '\\' && ucn(r,&p))) {
        if (c != '\\') (void)get(r,&p);
        identifier_rest(r,&p);
        CeText v = value_of(s,start,p,CE_PP_IDENTIFIER);
        static const char *const quotes[] = {"L", "u", "U", "u8", NULL}, *const chars[] = {"L", "u", "U", NULL};
        static const char *const raws[] = {"R", "LR", "uR", "UR", "u8R", NULL};
        int q = look(r,p);
        if ((q == '"' && prefix(v,quotes)) || (q == '\'' && prefix(v,chars))) {
            (void)get(r,&p);
            bool closed = quoted(r,&p,q);
            *end = p; *kind = !closed ? CE_PP_UNTERMINATED : q == '"' ? CE_PP_STRING : CE_PP_CHARACTER; return 1;
        }
        if (s->raw_strings && p < r->len && r->data[p] == '"' && prefix(v,raws)) {
            int raw = raw_string(r,p,end);
            if (raw < 0) {
                unsigned line = 1; for (size_t b = 0; b < start; ++b) line += r->data[b] == '\n';
                ce_error(s->error,start,"line %u: unterminated raw string literal",line); return -1;
            }
            if (raw > 0) { *kind = CE_PP_RAW_STRING; return 1; }
        }
        *end = p; *kind = CE_PP_IDENTIFIER; return 1;
    }
    p = start;
    bool number = digit(c);
    if (c == '.') { size_t q = start; (void)get(r,&q); number = digit(look(r,q)); }
    if (number) {
        (void)get(r,&p); number_rest(r,&p);
        *end = p; *kind = CE_PP_NUMBER; return 1;
    }
    if (c == '\'' || c == '"') {
        (void)get(r,&p);
        bool closed = quoted(r,&p,c);
        *end = p; *kind = !closed ? CE_PP_UNTERMINATED : c == '"' ? CE_PP_STRING : CE_PP_CHARACTER; return 1;
    }
    if (punctuator(r,&p)) { *end = p; *kind = CE_PP_PUNCTUATOR; return 1; }
    p = start; (void)get(r,&p);
    *end = p; *kind = CE_PP_OTHER; return 1;
}

/* Whether text spells exactly one preprocessing token (as ## must form):
   the token's kind, or 0 when it does not. */
unsigned ce_source_token_kind(CeArena *arena, CeText text, bool c11) {
    if (!text.len) return 0;
    CeError error = {{0},0};
    Scanner s = {.raw = {text.data,text.len,c11}, .arena = arena, .raw_strings = !c11, .error = &error};
    size_t start, next; int c = logical(&s.raw,0,&start,&next);
    if (c < 0 || start || horizontal(c) || c == '\n' || (c == '/' && (look(&s.raw,next) == '*' || look(&s.raw,next) == '/'))) return 0;
    size_t end; unsigned kind;
    if (lex(&s,0,c,false,&end,&kind) < 0 || end != text.len) return 0;
    return kind;
}

static void positions(CeSourceFile *f) {
    const unsigned char *d = f->bytes.data; size_t n = f->bytes.len;
    size_t *starts = ce_resize(NULL,n+2,sizeof(*starts)), lines = 0;
    starts[lines++] = 0;
    for (size_t i = 0; i < n; ++i)
        if (d[i] == '\n' || (d[i] == '\r' && !(i+1 < n && d[i+1] == '\n'))) starts[lines++] = i+1;
    size_t line = 0;
    for (size_t i = 0; i < f->ntokens; ++i) {
        size_t at = f->tokens[i].begin;
        while (line+1 < lines && starts[line+1] <= at) ++line;
        f->tokens[i].line = (unsigned)line+1; f->tokens[i].column = (unsigned)(at-starts[line])+1;
    }
    f->lines = lines;
    free(starts);
}

static void where(const CeSourceFile *f, size_t offset, unsigned *line, unsigned *column) {
    unsigned l = 1; size_t start = 0;
    for (size_t i = 0; i < offset && i < f->bytes.len; ++i)
        if (f->bytes.data[i] == '\n' || (f->bytes.data[i] == '\r' && !(i+1 < f->bytes.len && f->bytes.data[i+1] == '\n'))) { ++l; start = i+1; }
    *line = l; *column = (unsigned)(offset-start)+1;
}

/* Conditional nesting, reported as GCC and Clang report it. The grammar
   states the same structure; this names the mistake for the reader. */
static bool conditionals(const CeSourceFile *f, const Symbols *y, CeError *error) {
    typedef struct { unsigned line; bool otherwise; unsigned else_line; } Open;
    Open *open = NULL; size_t depth = 0, capacity = 0; bool ok = true;
    for (size_t i = 0; i+1 < f->ntokens && ok; ++i) {
        const CeSourceToken *t = &f->tokens[i];
        if (t->symbol != y->hash) continue;
        size_t k = i+1;
        while (k < f->ntokens && f->tokens[k].kind == CE_PP_MARKER) ++k;
        if (k >= f->ntokens) break;
        int s = f->tokens[k].symbol, d = -1;
        for (int j = 0; j < D_COUNT; ++j) if (y->directive[j] == s) d = j;
        if (d == D_IF || d == D_IFDEF || d == D_IFNDEF) {
            if (depth == capacity) { capacity = capacity ? capacity*2 : 16; open = ce_resize(open,capacity,sizeof(*open)); }
            open[depth++] = (Open){t->line,false,0};
        } else if (d == D_ELIF || d == D_ELIFDEF || d == D_ELIFNDEF || d == D_ELSE || d == D_ENDIF) {
            const char *name = directive_names[d];
            if (!depth) { ok = ce_error(error,t->begin,"line %u: #%s without #if",t->line,name) && false; break; }
            Open *top = &open[depth-1];
            if (d == D_ENDIF) { --depth; continue; }
            if (top->otherwise) {
                ok = ce_error(error,t->begin,"line %u: #%s after #else (the #else at line %u)",t->line,name,top->else_line) && false;
                break;
            }
            if (d == D_ELSE) { top->otherwise = true; top->else_line = t->line; }
        }
    }
    if (ok && depth) ok = ce_error(error,0,"unterminated #if from line %u",open[depth-1].line) && false;
    free(open);
    return ok;
}

bool ce_source_scan(const CeGrammar *g, CeArena *arena, CeText bytes, bool c11,
                    CeSourceFile *out, CeError *error) {
    memset(out,0,sizeof(*out)); out->forest.root = -1;
    Symbols sym;
    if (!symbols(g,&sym,error)) return false;
    Scanner s = {.raw = {bytes.data,bytes.len,c11}, .arena = arena, .sym = &sym, .raw_strings = !c11, .error = error};
    out->bytes = bytes; out->trigraphs = c11; out->raw_strings = !c11;
    size_t pos = 0, trivia = 0; bool space = false;
    if (bytes.len >= 3 && !memcmp(bytes.data,"\xef\xbb\xbf",3)) pos = 3;
    for (;;) {
        size_t start, next; int c = logical(&s.raw,pos,&start,&next);
        if (c < 0) break;
        if (horizontal(c) || c == 0) { pos = next; space = true; continue; }
        if (c == '/') {
            size_t p = next; int d = get(&s.raw,&p);
            if (d == '*') {
                for (int previous = 0;;) {
                    int e = get(&s.raw,&p);
                    if (e < 0) {
                        free(s.line.data); free(s.out.data);
                        unsigned line = 1; for (size_t b = 0; b < start; ++b) line += bytes.data[b] == '\n';
                        return ce_error(error,start,"line %u: unterminated comment",line);
                    }
                    if (previous == '*' && e == '/') break;
                    previous = e;
                }
                pos = p; space = true; continue;
            }
            if (d == '/') {
                for (;;) { size_t q = p; int e = get(&s.raw,&q); if (e < 0 || e == '\n') break; p = q; }
                pos = p; space = true; continue;
            }
        }
        CeSourceToken t = {.trivia = trivia, .begin = start, .space_before = space};
        if (c == '\n') {
            t.kind = CE_PP_NEWLINE; t.end = next;
            t.value = value_of(&s,start,next,CE_PP_NEWLINE);
            push(&s.line,t); finish_line(&s);
            trivia = pos = next; space = false; continue;
        }
        size_t end; unsigned kind;
        if (lex(&s,start,c,header_context(&s),&end,&kind) < 0) { free(s.line.data); free(s.out.data); return false; }
        t.kind = kind; t.end = end; t.value = value_of(&s,start,end,kind);
        push(&s.line,t);
        trivia = pos = end; space = false;
    }
    if (s.line.len || trivia < bytes.len) {
        CeSourceToken t = {.kind = CE_PP_NEWLINE, .trivia = trivia, .begin = bytes.len, .end = bytes.len,
                           .value = {(unsigned char *)bytes.data+bytes.len,0}, .space_before = space};
        push(&s.line,t); finish_line(&s);
    }
    free(s.line.data);
    out->tokens = s.out.data; out->ntokens = s.out.len; out->directives = s.directives;
    positions(out);
    out->parse.data = ce_resize(NULL,out->ntokens ? out->ntokens : 1,sizeof(*out->parse.data));
    out->parse.len = out->ntokens;
    for (size_t i = 0; i < out->ntokens; ++i) {
        const CeSourceToken *t = &out->tokens[i];
        out->parse.data[i] = (CeToken){t->symbol,{(unsigned char *)bytes.data+t->begin,t->end-t->begin},t->begin};
    }
    return conditionals(out,&sym,error);
}

bool ce_source_read(const CeGrammar *g, const CeLR *lr, CeArena *arena, CeText bytes, bool c11,
                    CeSourceFile *out, CeError *error) {
    if (!ce_source_scan(g,arena,bytes,c11,out,error)) return false;
    if (!ce_lr_parse(g,lr,&out->parse,&out->forest,error)) {
        unsigned line, column; where(out,error->offset,&line,&column);
        char message[sizeof(error->message)];
        snprintf(message,sizeof(message),"line %u, column %u: %.400s",line,column,error->message);
        memcpy(error->message,message,sizeof(message));
        return false;
    }
    return ce_tree_from_forest(g,&out->forest,&out->parse,arena,&out->tree,error);
}

void ce_source_free(CeSourceFile *f) {
    free(f->tokens); ce_tokens_free(&f->parse); ce_forest_free(&f->forest);
    memset(f,0,sizeof(*f)); f->forest.root = -1;
}

/* ---- The exact document --------------------------------------------------- */

static void forest_children(const CeForest *f, int id, int *out, size_t *len) {
    if (id < 0) return;
    CePacked p = f->nodes[id].alternatives[0];
    forest_children(f,p.left,out,len);
    if (p.right >= 0) out[(*len)++] = p.right;
}

typedef struct { const CeGrammar *g; const CeSourceFile *file; FILE *out; } Encoder;

static void encode_terminal(Encoder *e, size_t index) {
    const CeSourceToken *t = &e->file->tokens[index];
    if (t->kind == CE_PP_MARKER) return;
    fputc(' ',e->out);
    ce_quote(e->out,(CeText){(unsigned char *)e->file->bytes.data+t->trivia,t->end-t->trivia});
}

static void encode_node(Encoder *e, int id);

/* Lists are flattened in source order, as tree extraction flattens them. */
static void encode_items(Encoder *e, int id, int list) {
    const CeForest *f = &e->file->forest;
    typedef struct { int node; bool expand; } Item;
    size_t capacity = 64, n = 1;
    Item *stack = ce_resize(NULL,capacity,sizeof(*stack));
    stack[0] = (Item){id,true};
    while (n) {
        Item item = stack[--n];
        if (!item.expand) { encode_node(e,item.node); continue; }
        CePacked alt = f->nodes[item.node].alternatives[0];
        const CeProduction *p = &e->g->productions[alt.production];
        int *ids = ce_resize(NULL,p->len ? p->len : 1,sizeof(*ids)); size_t k = 0;
        forest_children(f,alt.left,ids,&k);
        for (size_t i = k; i; --i) {
            if (n == capacity) { capacity *= 2; stack = ce_resize(stack,capacity,sizeof(*stack)); }
            stack[n++] = (Item){ids[i-1],f->nodes[ids[i-1]].symbol == list};
        }
        free(ids);
    }
    free(stack);
}

static void encode_node(Encoder *e, int id) {
    const CeForest *f = &e->file->forest;
    CeForestNode node = f->nodes[id];
    if (e->g->symbols[node.symbol].kind != CE_NONTERMINAL) { encode_terminal(e,node.begin); return; }
    CePacked alt = node.alternatives[0];
    const CeProduction *p = &e->g->productions[alt.production];
    if (e->g->symbols[p->lhs].list_element >= 0) {
        fputs(" (c:List",e->out); encode_items(e,id,p->lhs); fputc(')',e->out); return;
    }
    fputs(" (c:",e->out); fwrite(p->label.data,1,p->label.len,e->out);
    int *ids = ce_resize(NULL,p->len ? p->len : 1,sizeof(*ids)); size_t k = 0;
    forest_children(f,alt.left,ids,&k);
    for (size_t i = 0; i < k; ++i) encode_node(e,ids[i]);
    free(ids);
    fputc(')',e->out);
}

bool ce_source_encode(const CeGrammar *g, const CeSourceFile *file, FILE *out) {
    Encoder e = {g,file,out};
    fputs("(cesta source/1",out);
    if (file->forest.root >= 0) encode_node(&e,file->forest.root);
    fputs(")\n",out);
    return !ferror(out);
}

/* The document's strings, in order, are the file's bytes; its labels and
   parentheses spell nothing. */
bool ce_source_bytes(CeArena *arena, CeText document, CeText *out, CeError *error) {
    static const char head[] = "(cesta source/1";
    size_t pos = 0;
    while (pos < document.len && (document.data[pos] == ' ' || (document.data[pos] >= 9 && document.data[pos] <= 13))) ++pos;
    size_t head_end = pos+sizeof(head)-1;
    if (document.len-pos < sizeof(head)-1 || memcmp(document.data+pos,head,sizeof(head)-1) ||
        (head_end < document.len && document.data[head_end] != ')' && document.data[head_end] != ' ' &&
         !(document.data[head_end] >= 9 && document.data[head_end] <= 13)))
        return ce_error(error,pos,"expected a (cesta source/1 ...) document");
    unsigned char *buffer = NULL; size_t len = 0, capacity = 0; int depth = 0; bool closed = false;
    for (; pos < document.len; ++pos) {
        unsigned c = document.data[pos];
        if (closed && c != ';' && c != ' ' && !(c >= 9 && c <= 13)) {
            free(buffer); return ce_error(error,pos,"unexpected data after source document");
        }
        if (c == '(') ++depth;
        else if (c == ')') { --depth; if (!depth) closed = true; }
        else if (c == ';') { while (pos < document.len && document.data[pos] != '\n') ++pos; }
        else if (c == '"') {
            CeText value;
            if (!ce_quoted_value(arena,document,&pos,&value,error)) { free(buffer); return false; }
            while (len+value.len > capacity) { capacity = capacity ? capacity*2 : 65536; buffer = ce_resize(buffer,capacity,1); }
            if (value.len) memcpy(buffer+len,value.data,value.len);
            len += value.len; --pos;
        }
        if (depth < 0) { free(buffer); return ce_error(error,pos,"unbalanced document parenthesis"); }
    }
    if (depth) { free(buffer); return ce_error(error,document.len,"unbalanced document parenthesis"); }
    *out = ce_text(arena,buffer ? buffer : (unsigned char *)"",len);
    free(buffer);
    return true;
}

/* ---- Commands ------------------------------------------------------------- */

static bool token_admitted(const CeGrammar *g, const CeSourceToken *t) {
    const CeSymbol *symbol = &g->symbols[t->symbol];
    if (t->kind == CE_PP_MARKER) return symbol->virtual_token && !t->value.len;
    if (symbol->kind == CE_LITERAL)
        return ce_equal(symbol->name,t->value) || (ce_is(symbol->name,"#") && ce_is(t->value,"%:"));
    if (t->kind == CE_PP_NEWLINE && !t->value.len) return t->begin == t->end;
    for (size_t j = 0; j < g->nlex; ++j) {
        if (g->lex[j].symbol != t->symbol) continue;
        size_t length = 0;
        if (ce_regex_match(g,g->lex[j].start,g->lex[j].end,t->value,0,&length) && length == t->value.len) return true;
    }
    return false;
}

static char *document_text(const CeGrammar *g, const CeSourceFile *file, size_t *len) {
    char *text = NULL;
    FILE *memory = open_memstream(&text,len);
    if (!memory) return NULL;
    bool ok = ce_source_encode(g,file,memory);
    fclose(memory);
    if (!ok) { free(text); return NULL; }
    return text;
}

/* Read, print, re-read and compare one file: the exact document must spell
   the file's bytes, and those bytes must give the same document again. */
static bool check_file(const CeGrammar *g, const CeLR *lr, const char *path, bool c11, CeError *error) {
    CeArena arena = {0}, trees = {0}; CeText bytes; CeSourceFile file = {0}, again = {0}; bool ok = false;
    char *first = NULL, *second = NULL; size_t first_len = 0, second_len = 0;
    if (!ce_read_file(&arena,path,&bytes,error) || !ce_source_read(g,lr,&trees,bytes,c11,&file,error)) goto done;
    for (size_t i = 0; i < file.ntokens; ++i)
        if (!token_admitted(g,&file.tokens[i])) {
            ce_error(error,file.tokens[i].begin,"line %u, column %u: a %.*s token's spelling is outside its lexical rule",
                     file.tokens[i].line,file.tokens[i].column,(int)g->symbols[file.tokens[i].symbol].name.len,
                     g->symbols[file.tokens[i].symbol].name.data);
            goto done;
        }
    for (size_t i = 0; i < file.ntokens; ++i) {
        size_t expected = i ? file.tokens[i-1].end : 0;
        if (file.tokens[i].trivia != expected || file.tokens[i].begin < file.tokens[i].trivia || file.tokens[i].end < file.tokens[i].begin) {
            ce_error(error,file.tokens[i].begin,"token %zu does not continue its predecessor's bytes",i); goto done;
        }
    }
    if ((file.ntokens ? file.tokens[file.ntokens-1].end : 0) != bytes.len) { ce_error(error,0,"trailing bytes belong to no token"); goto done; }
    if (!(first = document_text(g,&file,&first_len))) { ce_error(error,0,"document printing failed"); goto done; }
    size_t lines = file.lines, tokens = file.ntokens, directives = file.directives;
    ce_source_free(&file); ce_arena_free(&trees);
    CeText spelled_bytes;
    if (!ce_source_bytes(&arena,(CeText){(unsigned char *)first,first_len},&spelled_bytes,error)) goto done;
    if (!ce_equal(spelled_bytes,bytes)) { ce_error(error,0,"the exact document does not spell the file"); goto done; }
    if (!ce_source_read(g,lr,&trees,spelled_bytes,c11,&again,error)) goto done;
    if (!(second = document_text(g,&again,&second_len))) { ce_error(error,0,"document printing failed"); goto done; }
    if (first_len != second_len || memcmp(first,second,first_len)) { ce_error(error,0,"re-reading the spelled bytes changed the document"); goto done; }
    printf("%s: %zu lines, %zu tokens, %zu directives; exact\n",path,lines,tokens,directives);
    ok = true;
done:
    free(first); free(second); ce_source_free(&file); ce_source_free(&again);
    ce_arena_free(&trees); ce_arena_free(&arena);
    return ok;
}

int ce_pp_source_command(int argc, char **argv, CeText cf) {
    CeGrammar g = {0}; CeLR lr = {0}; CeError error = {{0},0}; CeArena arena = {0};
    int result = 1; bool c11 = false, check = false, tokens = false; const char *path = "";
    if (!strcmp(argv[1],"source-bytes")) {
        CeText document, bytes;
        if (argc != 3) { fputs("usage: cesta source-bytes DOCUMENT\n",stderr); return 2; }
        path = argv[2];
        if (!ce_read_file(&arena,path,&document,&error) || !ce_source_bytes(&arena,document,&bytes,&error)) goto done;
        fwrite(bytes.data,1,bytes.len,stdout);
        result = ferror(stdout) ? 1 : 0; goto done;
    }
    int i = 2;
    for (; i < argc && argv[i][0] == '-'; ++i) {
        if (!strcmp(argv[i],"--std=c11")) c11 = true;
        else if (!strcmp(argv[i],"--std=gnu11")) c11 = false;
        else if (!strcmp(argv[i],"--check")) check = true;
        else if (!strcmp(argv[i],"--tokens")) tokens = true;
        else { fprintf(stderr,"cesta source: unknown option %s\n",argv[i]); return 2; }
    }
    if (i == argc || (!check && argc-i != 1)) { fputs("usage: cesta source [--std=c11|gnu11] [--check|--tokens] FILE...\n",stderr); return 2; }
    if (!ce_source_grammar(&g,cf,&error) || !ce_lr_prepare(&g,g.entries[0],&lr,&error)) goto done;
    if (lr.conflicts) { ce_lr_conflicts(&g,&lr,stderr); ce_error(&error,0,"the preprocessing grammar has LR conflicts"); goto done; }
    if (check) {
        result = 0;
        for (; i < argc; ++i) {
            path = argv[i]; error.message[0] = 0;
            if (!check_file(&g,&lr,path,c11,&error)) { fprintf(stderr,"%s: %s\n",path,error.message); result = 1; }
        }
        error.message[0] = 0; goto done;
    }
    path = argv[i];
    CeText bytes; CeSourceFile file;
    if (tokens) {
        /* One preprocessing token per line, as phase 3 spells it; directive
           lines keep their # and name. */
        if (!ce_read_file(&arena,path,&bytes,&error) || !ce_source_scan(&g,&arena,bytes,c11,&file,&error)) goto done;
        for (size_t k = 0; k < file.ntokens; ++k) {
            const CeSourceToken *x = &file.tokens[k];
            if (x->kind == CE_PP_MARKER || x->kind == CE_PP_NEWLINE) continue;
            ce_quote(stdout,x->value); fputc('\n',stdout);
        }
        ce_source_free(&file);
        result = ferror(stdout) ? 1 : 0; goto done;
    }
    if (!ce_read_file(&arena,path,&bytes,&error) || !ce_source_read(&g,&lr,&arena,bytes,c11,&file,&error)) goto done;
    result = ce_source_encode(&g,&file,stdout) ? 0 : 1;
    ce_source_free(&file);
done:
    if (result && error.message[0]) fprintf(stderr,"%s: %s\n",path,error.message);
    ce_lr_free(&lr); ce_grammar_free(&g); ce_arena_free(&arena);
    return result;
}

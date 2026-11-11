/* Build-time compiler for GSLT presentations of MeTTa.
 *
 *   gslt_presentation_compile_v1 NAME INSPECTION FORMS ENTRY OUT.h
 *
 * INSPECTION is the module's syntax as CeTTa assembles it
 * (langdef:inspect-syntax-gslt, written by lib/presentation/export_presentation.metta):
 * its grammar rules after list and coercion expansion, and its token layer
 * (regular expressions over code points, trivia, comments).  FORMS is the
 * presentation's forms table: the atom each production denotes.
 *
 * The output is a C header of tables for src/gslt_presentation_v1.c:
 *   - an LALR(1) parse table; any shift/reduce or reduce/reduce conflict is
 *     reported and the compiler fails, so a generated table certifies that
 *     the grammar is unambiguous;
 *   - a DFA lexer over code-point classes with BNFC's lexical policy
 *     (longest match, earlier rule on a tie, reserved words over tokens);
 *   - template code for reading, and the form order, categories and
 *     parenthesization for printing.
 */

#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GSLT_PRESENTATION_TABLES_ONLY
#include "../src/gslt_presentation_v1.h"

static void die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fputs("gslt_presentation_compile_v1: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(1);
}

static void *xcalloc(size_t n, size_t s) {
    void *p = calloc(n ? n : 1u, s ? s : 1u);
    if (!p) die("out of memory");
    return p;
}

static void *xrealloc(void *p, size_t s) {
    p = realloc(p, s ? s : 1u);
    if (!p) die("out of memory");
    return p;
}

static char *xstrdup(const char *s) {
    size_t n = strlen(s);
    char *d = xcalloc(n + 1u, 1u);
    memcpy(d, s, n);
    return d;
}

/* ------------------------------------------------------------ data reader */

typedef struct Node {
    char kind; /* 'l' list, 's' word, 'q' string */
    char *text;
    struct Node **kids;
    int n, cap;
} Node;

typedef struct {
    const char *s;
    size_t i, n;
} Src;

static void skip_space(Src *in) {
    while (in->i < in->n) {
        char c = in->s[in->i];
        if (c == ';') {
            while (in->i < in->n && in->s[in->i] != '\n') in->i++;
        } else if (isspace((unsigned char)c)) {
            in->i++;
        } else {
            break;
        }
    }
}

static Node *read_node(Src *in) {
    skip_space(in);
    if (in->i >= in->n) return NULL;
    char c = in->s[in->i];
    Node *node = xcalloc(1u, sizeof *node);
    if (c == '(') {
        in->i++;
        node->kind = 'l';
        for (;;) {
            skip_space(in);
            if (in->i >= in->n) die("unbalanced parenthesis in data");
            if (in->s[in->i] == ')') { in->i++; break; }
            Node *kid = read_node(in);
            if (!kid) die("unexpected end of data");
            if (node->n == node->cap) {
                node->cap = node->cap ? node->cap * 2 : 4;
                node->kids = xrealloc(node->kids, (size_t)node->cap * sizeof *node->kids);
            }
            node->kids[node->n++] = kid;
        }
        return node;
    }
    if (c == ')') die("unexpected ')' in data");
    if (c == '"') {
        size_t cap = 16, len = 0;
        char *buf = xcalloc(cap, 1u);
        in->i++;
        while (in->i < in->n && in->s[in->i] != '"') {
            char ch = in->s[in->i++];
            if (ch == '\\' && in->i < in->n) {
                char e = in->s[in->i++];
                switch (e) {
                case 'n': ch = '\n'; break;
                case 't': ch = '\t'; break;
                case 'r': ch = '\r'; break;
                case '0': ch = '\0'; break;
                default: ch = e; break;
                }
            }
            if (len + 2 >= cap) { cap *= 2; buf = xrealloc(buf, cap); }
            buf[len++] = ch;
        }
        if (in->i >= in->n) die("unterminated string in data");
        in->i++;
        buf[len] = '\0';
        node->kind = 'q';
        node->text = buf;
        return node;
    }
    size_t start = in->i;
    while (in->i < in->n) {
        char ch = in->s[in->i];
        if (isspace((unsigned char)ch) || ch == '(' || ch == ')' || ch == '"') break;
        in->i++;
    }
    node->kind = 's';
    node->text = xcalloc(in->i - start + 1u, 1u);
    memcpy(node->text, in->s + start, in->i - start);
    return node;
}

static char *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) die("cannot open %s", path);
    size_t cap = 1 << 16, n = 0;
    char *buf = xcalloc(cap, 1u);
    for (;;) {
        if (n + 4096 > cap) { cap *= 2; buf = xrealloc(buf, cap); }
        size_t got = fread(buf + n, 1u, cap - n - 1u, f);
        n += got;
        if (got == 0) break;
    }
    fclose(f);
    buf[n] = '\0';
    if (len) *len = n;
    return buf;
}

static bool is_head(const Node *node, const char *head) {
    return node && node->kind == 'l' && node->n > 0 && node->kids[0]->kind == 's' &&
           strcmp(node->kids[0]->text, head) == 0;
}

static const Node *find_head(const Node *node, const char *head) {
    if (!node || node->kind != 'l') return NULL;
    if (is_head(node, head)) return node;
    for (int i = 0; i < node->n; i++) {
        const Node *found = find_head(node->kids[i], head);
        if (found) return found;
    }
    return NULL;
}

static void find_all(const Node *node, const char *head, const Node ***out, int *n, int *cap) {
    if (!node || node->kind != 'l') return;
    if (is_head(node, head)) {
        if (*n == *cap) {
            *cap = *cap ? *cap * 2 : 8;
            *out = xrealloc(*out, (size_t)*cap * sizeof **out);
        }
        (*out)[(*n)++] = node;
        return;
    }
    for (int i = 0; i < node->n; i++) find_all(node->kids[i], head, out, n, cap);
}

/* (LCons a (LCons b LNil)) -> [a, b] */
static int lcons_items(const Node *list, const Node ***out) {
    int n = 0, cap = 0;
    *out = NULL;
    while (is_head(list, "LCons") && list->n == 3) {
        if (n == cap) {
            cap = cap ? cap * 2 : 8;
            *out = xrealloc(*out, (size_t)cap * sizeof **out);
        }
        (*out)[n++] = list->kids[1];
        list = list->kids[2];
    }
    if (!(list && list->kind == 's' && strcmp(list->text, "LNil") == 0))
        die("malformed LCons list");
    return n;
}

static const char *str_of(const Node *node, const char *what) {
    if (!node || node->kind != 'q') die("expected a string for %s", what);
    return node->text;
}

/* bnf-v1 text as UTF-8 */
static char *text_of(const Node *node) {
    size_t cap = 16, len = 0;
    char *buf = xcalloc(cap, 1u);
    while (is_head(node, "bnf-v1:text-cons") && node->n == 3) {
        unsigned long cp = strtoul(node->kids[1]->text, NULL, 10);
        if (len + 5 >= cap) { cap *= 2; buf = xrealloc(buf, cap); }
        if (cp < 0x80) buf[len++] = (char)cp;
        else if (cp < 0x800) { buf[len++] = (char)(0xC0 | (cp >> 6)); buf[len++] = (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { buf[len++] = (char)(0xE0 | (cp >> 12)); buf[len++] = (char)(0x80 | ((cp >> 6) & 0x3F)); buf[len++] = (char)(0x80 | (cp & 0x3F)); }
        else { buf[len++] = (char)(0xF0 | (cp >> 18)); buf[len++] = (char)(0x80 | ((cp >> 12) & 0x3F)); buf[len++] = (char)(0x80 | ((cp >> 6) & 0x3F)); buf[len++] = (char)(0x80 | (cp & 0x3F)); }
        node = node->kids[2];
    }
    buf[len] = '\0';
    return buf;
}

/* ------------------------------------------------------------ grammar */

#define MAXT 256
#define LAW 4 /* lookahead words: MAXT / 64 */

typedef struct { uint64_t w[LAW]; } TSet;

static bool tset_add(TSet *a, int t) {
    uint64_t bit = 1ull << (t & 63);
    if (a->w[t >> 6] & bit) return false;
    a->w[t >> 6] |= bit;
    return true;
}
static bool tset_has(const TSet *a, int t) { return (a->w[t >> 6] >> (t & 63)) & 1u; }
static bool tset_union(TSet *a, const TSet *b) {
    bool grew = false;
    for (int i = 0; i < LAW; i++) {
        uint64_t old = a->w[i];
        a->w[i] |= b->w[i];
        if (a->w[i] != old) grew = true;
    }
    return grew;
}

typedef struct {
    char *text;
    uint8_t kind;
    int token_decl; /* for token terminals: index into token declarations */
} Terminal;

typedef struct {
    char *name;
    int family, level;
    bool is_list;
    int list_empty, list_one, list_cons;
} Nonterminal;

typedef struct {
    int lhs;
    int len;
    int *rhs; /* >=0 terminal, -(n+1) nonterminal */
    char *label;
    uint8_t kind, leaf, guard, look;
    int template_off;
    int role_off; /* into role_code, or -1: every child inherits */
} Production;

static Terminal terms[MAXT];
static int n_terms;
static Nonterminal *nts;
static int n_nts, cap_nts;
static Production *prods;
static int n_prods, cap_prods;

static int term_find(const char *text, uint8_t kind) {
    for (int i = 0; i < n_terms; i++)
        if (terms[i].kind == kind && strcmp(terms[i].text, text) == 0) return i;
    return -1;
}

static int term_add(const char *text, uint8_t kind, int decl) {
    int found = term_find(text, kind);
    if (found >= 0) return found;
    if (n_terms >= MAXT) die("too many terminals");
    terms[n_terms].text = xstrdup(text);
    terms[n_terms].kind = kind;
    terms[n_terms].token_decl = decl;
    return n_terms++;
}

static int nt_find(const char *name) {
    for (int i = 0; i < n_nts; i++)
        if (strcmp(nts[i].name, name) == 0) return i;
    return -1;
}

static int nt_add(const char *name) {
    int found = nt_find(name);
    if (found >= 0) return found;
    if (n_nts == cap_nts) {
        cap_nts = cap_nts ? cap_nts * 2 : 32;
        nts = xrealloc(nts, (size_t)cap_nts * sizeof *nts);
    }
    memset(&nts[n_nts], 0, sizeof nts[n_nts]);
    nts[n_nts].name = xstrdup(name);
    nts[n_nts].list_empty = nts[n_nts].list_one = nts[n_nts].list_cons = -1;
    return n_nts++;
}

static bool identifier_like(const char *s) {
    /* BNFC: a reserved word is a keyword when it looks like an identifier */
    if (!isalpha((unsigned char)s[0])) return false;
    for (const char *p = s + 1; *p; p++)
        if (!(isalnum((unsigned char)*p) || *p == '_' || *p == '\'')) return false;
    return true;
}

/* ------------------------------------------------------------ token layer */

typedef struct {
    char *name;
    const Node *regex;
    int kind; /* GSLT_PRES_RULE_* */
    int terminal;
} TokenDecl;

static TokenDecl decls[64];
static int n_decls;
static char *line_comments[8];
static int n_line_comments;
static char *layout_words[32], *layout_stops[32];
static int n_layout_words, n_layout_stops;
static bool layout_toplevel, has_layout;
static char *block_open[8], *block_close[8];
static int n_block_comments;

/* ------------------------------------------------------------ forms */

typedef struct {
    char *label;
    const Node *tpl;
} Form;

static Form forms[512];
static int n_forms;
static struct { char *label; int kind; } token_forms[64];
static int n_token_forms;
static struct { char *label; int guard; } guards[64];
static int n_guards;
static char *statement_cat, *statement_end, *leaf_cat;
static char *channel_cats[GSLT_PRES_MAX_CHANNELS];
static int n_channel_cats;
static char *scope_openers[16];
static int n_scope_openers;
static struct { char *text; int kind; } spacings[64];
static int n_spacings;
static char *breaks[32];
static int n_breaks;
static char *version;
/* roles of a production's children, and the roles it prints in */
static struct { char *label; const Node *roles; } role_decls[128];
static int n_role_decls;
static struct { char *label; unsigned look; } look_decls[64];
static int n_look_decls;
/* builtin positions, derived from the builtins' declared types
   (: HEAD (-> T1 ... Tn R)): the role of argument i is Ti's */
static struct { char *head; int n; uint8_t roles[32]; } sig_decls[256];
static int n_sig_decls;
static uint8_t role_code[4096];
static int n_role_code;

static int role_of(const char *s) {
    static const char *names[] = {"run", "data", "list", "head", "inherit", "args", "source", "pattern"};
    for (int i = 0; i < 8; i++)
        if (strcmp(s, names[i]) == 0) return i;
    die("unknown role %s (run, data, list, head, inherit, args, source, pattern)", s);
    return 0;
}

/* the roles of a list node, appended to role_code; its offset */
static int roles_emit(const Node *list, int want, const char *what) {
    if (!list || list->kind != 'l') die("%s: roles are a list", what);
    if (want >= 0 && list->n != want)
        die("%s: %d roles for %d children", what, list->n, want);
    int off = n_role_code;
    for (int i = 0; i < list->n; i++) {
        if (list->kids[i]->kind != 's') die("%s: a role is a word", what);
        if (n_role_code == (int)sizeof role_code) die("too many roles");
        role_code[n_role_code++] = (uint8_t)role_of(list->kids[i]->text);
    }
    return off;
}

/* The builtins' declarations (: HEAD (-> T1 ... Tn R)); each argument's role
   is its type's, by the rule the printer applies to a program's own
   declarations (gslt_presentation_v1_role_of_type).  A special form whose
   positions no type of that rule states is given by its roles:
   (pres:special-form HEAD (r1 ... rn)). */
static void load_declarations(const char *path) {
    size_t len;
    char *text = read_file(path, &len);
    Src in = {text, 0, len};
    for (;;) {
        Node *node = read_node(&in);
        if (!node) break;
        if (is_head(node, "pres:special-form")) {
            if (node->n != 3 || node->kids[1]->kind != 's' || node->kids[2]->kind != 'l' || node->kids[2]->n > 32)
                die("%s: a malformed pres:special-form", path);
            if (n_sig_decls == 256) die("too many builtin declarations");
            sig_decls[n_sig_decls].head = node->kids[1]->text;
            sig_decls[n_sig_decls].n = node->kids[2]->n;
            for (int i = 0; i < node->kids[2]->n; i++) {
                if (node->kids[2]->kids[i]->kind != 's') die("%s: a role is a word", path);
                sig_decls[n_sig_decls].roles[i] = (uint8_t)role_of(node->kids[2]->kids[i]->text);
            }
            n_sig_decls++;
            continue;
        }
        if (!is_head(node, ":"))
            die("%s: only declarations (: HEAD (-> T1 ... Tn R)) and pres:special-form belong here", path);
        const Node *ty = node->n == 3 ? node->kids[2] : NULL;
        if (node->kids[1]->kind != 's' || !ty || !is_head(ty, "->") || ty->n < 2)
            die("%s: a malformed declaration", path);
        int n = ty->n - 2;
        if (n > 32) die("%s: %s declares more than 32 arguments", path, node->kids[1]->text);
        if (n_sig_decls == 256) die("too many builtin declarations");
        sig_decls[n_sig_decls].head = node->kids[1]->text;
        sig_decls[n_sig_decls].n = n;
        for (int i = 0; i < n; i++) {
            const Node *t = ty->kids[1 + i];
            const char *h = t->kind == 'l' ? (t->n > 0 && t->kids[0]->kind == 's' ? t->kids[0]->text : NULL) : t->text;
            sig_decls[n_sig_decls].roles[i] =
                gslt_presentation_v1_role_of_type(h, h ? strlen(h) : 0, t->kind == 'l' ? (size_t)t->n : 0);
        }
        n_sig_decls++;
    }
}

static int channel_index(const char *cat) {
    for (int i = 0; i < n_channel_cats; i++)
        if (strcmp(channel_cats[i], cat) == 0) return i;
    return -1;
}

static int32_t *tcode;
static int n_tcode, cap_tcode;
static char **tconst;
static uint8_t *tconst_is_string;
static int n_tconst, cap_tconst;

static void emit_t(int32_t v) {
    if (n_tcode == cap_tcode) {
        cap_tcode = cap_tcode ? cap_tcode * 2 : 256;
        tcode = xrealloc(tcode, (size_t)cap_tcode * sizeof *tcode);
    }
    tcode[n_tcode++] = v;
}

static int const_add(const char *text, bool is_string) {
    for (int i = 0; i < n_tconst; i++)
        if (tconst_is_string[i] == is_string && strcmp(tconst[i], text) == 0) return i;
    if (n_tconst == cap_tconst) {
        cap_tconst = cap_tconst ? cap_tconst * 2 : 32;
        tconst = xrealloc(tconst, (size_t)cap_tconst * sizeof *tconst);
        tconst_is_string = xrealloc(tconst_is_string, (size_t)cap_tconst);
    }
    tconst[n_tconst] = xstrdup(text);
    tconst_is_string[n_tconst] = is_string;
    return n_tconst++;
}

static void compile_template(const Node *t, int nargs, const char *label) {
    if (t->kind == 's') {
        emit_t(GSLT_PRES_T_WORD);
        emit_t(const_add(t->text, false));
        return;
    }
    if (t->kind == 'q') {
        emit_t(GSLT_PRES_T_STRING);
        emit_t(const_add(t->text, true));
        return;
    }
    if (is_head(t, "cs:arg") && t->n == 2) {
        int i = atoi(t->kids[1]->text);
        if (i < 0 || i >= nargs) die("form %s: (cs:arg %d) but the production has %d children", label, i, nargs);
        emit_t(GSLT_PRES_T_ARG);
        emit_t(i);
        return;
    }
    if (is_head(t, "cs:cons") && t->n == 3) {
        emit_t(GSLT_PRES_T_CONS);
        compile_template(t->kids[1], nargs, label);
        compile_template(t->kids[2], nargs, label);
        return;
    }
    if (is_head(t, "cs:run") && t->n == 2) {
        emit_t(GSLT_PRES_T_RUN);
        compile_template(t->kids[1], nargs, label);
        return;
    }
    if (is_head(t, "cs:fixed") && t->n == 4 && t->kids[1]->kind == 's' && t->kids[2]->kind == 's') {
        int i = atoi(t->kids[1]->text);
        if (i < 0 || i >= nargs) die("form %s: (cs:fixed %d ...) but the production has %d children", label, i, nargs);
        emit_t(GSLT_PRES_T_FIXED);
        emit_t(i);
        emit_t(const_add(t->kids[2]->text, false));
        compile_template(t->kids[3], nargs, label);
        return;
    }
    if (is_head(t, "cs:channel") && t->n == 2 && t->kids[1]->kind == 's') {
        int k = channel_index(t->kids[1]->text);
        if (k < 0) die("form %s: (cs:channel %s) names no declared channel category", label, t->kids[1]->text);
        emit_t(GSLT_PRES_T_CHANNEL);
        emit_t(k);
        return;
    }
    if (is_head(t, "cs:x")) {
        emit_t(GSLT_PRES_T_EXPR);
        emit_t(t->n - 1);
        for (int i = 1; i < t->n; i++) compile_template(t->kids[i], nargs, label);
        return;
    }
    die("form %s: a template is a word, a string, (cs:arg i), (cs:cons x l), (cs:run e), "
        "(cs:channel C), (cs:fixed i word t) or (cs:x ...)", label);
}

/* whether the template at *pc names a channel; advances past it */
static bool template_uses_channel(int *pc) {
    int32_t op = tcode[(*pc)++];
    bool uses = false;
    switch (op) {
    case GSLT_PRES_T_WORD:
    case GSLT_PRES_T_STRING:
    case GSLT_PRES_T_ARG:
        (*pc)++;
        break;
    case GSLT_PRES_T_CHANNEL:
        (*pc)++;
        uses = true;
        break;
    case GSLT_PRES_T_RUN:
        uses = template_uses_channel(pc);
        break;
    case GSLT_PRES_T_FIXED:
        (*pc) += 2;
        uses = template_uses_channel(pc);
        break;
    case GSLT_PRES_T_CONS:
        uses = template_uses_channel(pc);
        uses = template_uses_channel(pc) || uses;
        break;
    case GSLT_PRES_T_EXPR: {
        int32_t n = tcode[(*pc)++];
        for (int32_t k = 0; k < n; k++) uses = template_uses_channel(pc) || uses;
        break;
    }
    default:
        die("malformed template code");
    }
    return uses;
}

static int leaf_kind_of(const char *s) {
    static const char *names[] = {"", "var", "name", "quoted", "number", "negative", "string", "raw", "word"};
    for (int i = 1; i < 9; i++)
        if (strcmp(s, names[i]) == 0) return i;
    die("unknown token form %s", s);
    return 0;
}

static void load_forms(const char *path, const char *name) {
    size_t len;
    char *text = read_file(path, &len);
    Src in = {text, 0, len};
    for (;;) {
        Node *node = read_node(&in);
        if (!node) break;
        if (node->kind != 'l' || node->n < 2 || node->kids[1]->kind != 's' ||
            strcmp(node->kids[1]->text, name) != 0)
            continue;
        if (is_head(node, "pres:forms") && node->n == 3) {
            const Node *list = node->kids[2];
            for (int i = 0; i < list->n; i++) {
                const Node *f = list->kids[i];
                if (f->kind != 'l' || f->n != 2 || f->kids[0]->kind != 's') die("malformed form entry");
                forms[n_forms].label = f->kids[0]->text;
                forms[n_forms].tpl = f->kids[1];
                n_forms++;
            }
        } else if (is_head(node, "pres:token-forms") && node->n == 3) {
            const Node *list = node->kids[2];
            for (int i = 0; i < list->n; i++) {
                const Node *f = list->kids[i];
                token_forms[n_token_forms].label = f->kids[0]->text;
                token_forms[n_token_forms].kind = leaf_kind_of(f->kids[1]->text);
                n_token_forms++;
            }
        } else if (is_head(node, "pres:statement") && node->n == 4) {
            statement_cat = node->kids[2]->text;
            statement_end = node->kids[3]->text;
        } else if (is_head(node, "pres:leaf-category") && node->n == 3) {
            leaf_cat = node->kids[2]->text;
        } else if (is_head(node, "pres:channel") && node->n == 3) {
            if (n_channel_cats == GSLT_PRES_MAX_CHANNELS) die("too many channel categories");
            channel_cats[n_channel_cats++] = node->kids[2]->text;
        } else if (is_head(node, "pres:break-after") && node->n == 3) {
            if (n_breaks == 32) die("too many break declarations");
            breaks[n_breaks++] = node->kids[2]->text;
        } else if (is_head(node, "pres:spacing") && node->n == 4) {
            if (n_spacings == 64) die("too many spacing declarations");
            spacings[n_spacings].text = node->kids[2]->text;
            if (strcmp(node->kids[3]->text, "tight") == 0) spacings[n_spacings].kind = GSLT_PRES_SPACING_TIGHT;
            else if (strcmp(node->kids[3]->text, "spaced") == 0) spacings[n_spacings].kind = GSLT_PRES_SPACING_SPACED;
            else if (strcmp(node->kids[3]->text, "prefix") == 0) spacings[n_spacings].kind = GSLT_PRES_SPACING_PREFIX;
            else die("unknown spacing %s", node->kids[3]->text);
            n_spacings++;
        } else if (is_head(node, "pres:scope-open") && node->n == 3) {
            if (n_scope_openers == 16) die("too many scope openers");
            scope_openers[n_scope_openers++] = node->kids[2]->text;
        } else if (is_head(node, "pres:view-guard") && node->n == 4) {
            guards[n_guards].label = node->kids[2]->text;
            if (strcmp(node->kids[3]->text, "callable-head") == 0)
                guards[n_guards].guard = GSLT_PRES_GUARD_CALLABLE_HEAD;
            else if (strcmp(node->kids[3]->text, "symbol-head") == 0)
                guards[n_guards].guard = GSLT_PRES_GUARD_SYMBOL_HEAD;
            else
                die("unknown view guard %s", node->kids[3]->text);
            n_guards++;
        } else if (is_head(node, "pres:version") && node->n == 3) {
            version = (char *)str_of(node->kids[2], "version");
        } else if (is_head(node, "pres:roles") && node->n == 4) {
            if (n_role_decls == 128) die("too many role declarations");
            role_decls[n_role_decls].label = node->kids[2]->text;
            role_decls[n_role_decls].roles = node->kids[3];
            n_role_decls++;
        } else if (is_head(node, "pres:look") && node->n >= 4) {
            if (n_look_decls == 64) die("too many look declarations");
            look_decls[n_look_decls].label = node->kids[2]->text;
            look_decls[n_look_decls].look = 0;
            for (int i = 3; i < node->n; i++)
                look_decls[n_look_decls].look |= GSLT_PRES_LOOK_BIT(role_of(node->kids[i]->text));
            n_look_decls++;
        }
    }
    if (!n_forms) die("no (pres:forms %s ...) in %s", name, path);
    if (!statement_cat || !leaf_cat) die("forms need pres:statement and pres:leaf-category");
    if (!version) die("forms need (pres:version %s \"MAJOR.MINOR\")", name);
}

/* ------------------------------------------------------------ grammar load */

static int prod_add(int lhs, int len, const int *rhs, const char *label) {
    if (n_prods == cap_prods) {
        cap_prods = cap_prods ? cap_prods * 2 : 64;
        prods = xrealloc(prods, (size_t)cap_prods * sizeof *prods);
    }
    Production *p = &prods[n_prods];
    memset(p, 0, sizeof *p);
    p->lhs = lhs;
    p->len = len;
    p->rhs = xcalloc((size_t)(len ? len : 1), sizeof(int));
    if (len) memcpy(p->rhs, rhs, (size_t)len * sizeof(int));
    p->label = xstrdup(label);
    p->template_off = -1;
    p->role_off = -1;
    return n_prods++;
}

static void split_cat(const char *cat, char *family, int *level) {
    size_t n = strlen(cat), k = n;
    while (k > 0 && isdigit((unsigned char)cat[k - 1])) k--;
    memcpy(family, cat, k);
    family[k] = '\0';
    *level = k == n ? 0 : atoi(cat + k);
}

static char *families[128];
static int n_families;

static int family_id(const char *f) {
    for (int i = 0; i < n_families; i++)
        if (strcmp(families[i], f) == 0) return i;
    families[n_families] = xstrdup(f);
    return n_families++;
}

static void load_inspection(const char *path, const char *entry) {
    size_t len;
    char *text = read_file(path, &len);
    Src in = {text, 0, len};
    Node *root = read_node(&in);
    if (!root) die("empty inspection");
    const Node *wire = find_head(root, "GSLTLanguageDefWireV1");
    if (!wire || wire->n < 4) die("no GSLTLanguageDefWireV1 in the inspection");
    const Node *layer = find_head(root, "ParserTokenLayerV1");
    if (!layer) die("no ParserTokenLayerV1 in the inspection");

    /* token categories */
    const Node **types;
    int n_types = lcons_items(wire->kids[2], &types);
    char *token_cats[64];
    int n_token_cats = 0;
    for (int i = 0; i < n_types; i++) {
        if (!is_head(types[i], "TypeDecl")) continue;
        if (strcmp(types[i]->kids[2]->text, "CarrierTokenRaw") == 0)
            token_cats[n_token_cats++] = types[i]->kids[1]->text;
    }

    /* terminal 0: end of input */
    term_add("end of input", GSLT_PRES_TERM_EOF, -1);

    /* token declarations, in the layer's order */
    const Node **tok;
    int nt = 0, ct = 0;
    tok = NULL;
    find_all(layer, "RegularTokenDeclV1", &tok, &nt, &ct);
    for (int i = 0; i < nt; i++) {
        decls[n_decls].name = xstrdup(str_of(tok[i]->kids[1], "token name"));
        decls[n_decls].regex = tok[i]->kids[3];
        decls[n_decls].kind = GSLT_PRES_RULE_TOKEN;
        decls[n_decls].terminal = term_add(decls[n_decls].name, GSLT_PRES_TERM_TOKEN, n_decls);
        n_decls++;
    }
    const Node **triv = NULL;
    int ntv = 0, ctv = 0;
    find_all(layer, "RegularTriviaDeclV1", &triv, &ntv, &ctv);
    for (int i = 0; i < ntv; i++) {
        decls[n_decls].name = xstrdup("trivia");
        decls[n_decls].regex = triv[i]->kids[2];
        decls[n_decls].kind = GSLT_PRES_RULE_TRIVIA;
        decls[n_decls].terminal = -1;
        n_decls++;
    }
    const Node **lc = NULL, **bc = NULL;
    int nl = 0, cl = 0, nb = 0, cb = 0;
    find_all(layer, "ParserLineCommentV1", &lc, &nl, &cl);
    find_all(layer, "ParserBlockCommentV1", &bc, &nb, &cb);
    for (int i = 0; i < nl; i++) line_comments[n_line_comments++] = text_of(lc[i]->kids[1]);
    /* BNFC layout declarations, lowered by CeTTa's LBNF machinery */
    const Node **lw = NULL, **ls = NULL, **lt = NULL;
    int nlw = 0, clw = 0, nls = 0, cls = 0, nlt = 0, clt = 0;
    find_all(layer, "ParserLayoutV1", &lw, &nlw, &clw);
    find_all(layer, "ParserLayoutStopV1", &ls, &nls, &cls);
    find_all(layer, "ParserLayoutToplevelV1", &lt, &nlt, &clt);
    for (int i = 0; i < nlw; i++) {
        const Node **words;
        int nw = lcons_items(lw[i]->kids[1], &words);
        for (int k = 0; k < nw && n_layout_words < 32; k++) layout_words[n_layout_words++] = xstrdup(str_of(words[k], "layout word"));
        has_layout = true;
    }
    for (int i = 0; i < nls; i++) {
        const Node **words;
        int nw = lcons_items(ls[i]->kids[1], &words);
        for (int k = 0; k < nw && n_layout_stops < 32; k++) layout_stops[n_layout_stops++] = xstrdup(str_of(words[k], "layout stop word"));
        has_layout = true;
    }
    if (nlt) { layout_toplevel = true; has_layout = true; }
    for (int i = 0; i < nb; i++) {
        block_open[n_block_comments] = text_of(bc[i]->kids[1]);
        block_close[n_block_comments] = text_of(bc[i]->kids[2]);
        n_block_comments++;
    }

    /* rules */
    const Node **rules;
    int n_rules = lcons_items(wire->kids[3], &rules);
    /* nonterminal 0 is the start; the entry is nonterminal 1 */
    nt_add("%start");
    nt_add(entry);
    for (int r = 0; r < n_rules; r++) {
        const Node *g = rules[r];
        if (!is_head(g, "GrammarRule") || g->n != 6) die("malformed GrammarRule");
        nt_add(str_of(g->kids[2], "rule category"));
    }
    for (int r = 0; r < n_rules; r++) {
        const Node *g = rules[r];
        const char *label = str_of(g->kids[1], "rule label");
        int lhs = nt_find(str_of(g->kids[2], "rule category"));
        const Node **params, **items;
        int np = lcons_items(g->kids[3], &params);
        int ni = lcons_items(g->kids[4], &items);
        int rhs[64];
        if (ni > 64) die("rule %s too long", label);
        for (int k = 0; k < ni; k++) {
            if (is_head(items[k], "SyntaxTerminal")) {
                const char *lit = str_of(items[k]->kids[1], "terminal");
                rhs[k] = term_add(lit, identifier_like(lit) ? GSLT_PRES_TERM_KEYWORD : GSLT_PRES_TERM_SYMBOL, -1);
            } else if (is_head(items[k], "SyntaxNonTerminal")) {
                const char *arg = str_of(items[k]->kids[1], "argument");
                const char *cat = NULL;
                for (int q = 0; q < np; q++)
                    if (is_head(params[q], "TermSimple") && strcmp(str_of(params[q]->kids[1], "param"), arg) == 0 &&
                        is_head(params[q]->kids[2], "TBase"))
                        cat = str_of(params[q]->kids[2]->kids[1], "param category");
                if (!cat) die("rule %s: unbound argument %s", label, arg);
                bool is_token = false;
                for (int q = 0; q < n_token_cats; q++)
                    if (strcmp(token_cats[q], cat) == 0) is_token = true;
                if (is_token) {
                    int t = term_find(cat, GSLT_PRES_TERM_TOKEN);
                    if (t < 0) die("rule %s uses token %s with no declaration", label, cat);
                    rhs[k] = t;
                } else {
                    int n = nt_find(cat);
                    if (n < 0) die("rule %s uses category %s with no rule", label, cat);
                    rhs[k] = -(n + 1);
                }
            } else {
                die("rule %s: unsupported item", label);
            }
        }
        prod_add(lhs, ni, rhs, label);
    }
    int start_rhs[1] = {-(1 + 1)};
    int start = prod_add(0, 1, start_rhs, "%start");
    prods[start].kind = GSLT_PRES_PROD_START;
    if (nt_find(entry) != 1) die("entry category %s missing", entry);
}

static void classify_productions(void) {
    for (int p = 0; p < n_prods; p++) {
        Production *pr = &prods[p];
        if (pr->kind == GSLT_PRES_PROD_START) continue;
        int nargs = 0;
        for (int k = 0; k < pr->len; k++)
            if (pr->rhs[k] < 0 || terms[pr->rhs[k]].kind == GSLT_PRES_TERM_TOKEN) nargs++;
        if (strcmp(pr->label, "_") == 0) {
            pr->kind = GSLT_PRES_PROD_COERCION;
            int nn = 0;
            for (int k = 0; k < pr->len; k++) if (pr->rhs[k] < 0) nn++;
            if (nn != 1) die("coercion on %s has %d categories", nts[pr->lhs].name, nn);
            continue;
        }
        if (strcmp(pr->label, "[]") == 0) { pr->kind = GSLT_PRES_PROD_LIST_EMPTY; nts[pr->lhs].list_empty = p; continue; }
        if (strcmp(pr->label, "(:[])") == 0) { pr->kind = GSLT_PRES_PROD_LIST_ONE; nts[pr->lhs].list_one = p; continue; }
        if (strcmp(pr->label, "(:)") == 0) { pr->kind = GSLT_PRES_PROD_LIST_CONS; nts[pr->lhs].list_cons = p; continue; }
        for (int i = 0; i < n_token_forms; i++) {
            if (strcmp(token_forms[i].label, pr->label) == 0) {
                if (nargs != 1) die("token form %s needs exactly one token child", pr->label);
                pr->kind = GSLT_PRES_PROD_TOKEN;
                pr->leaf = (uint8_t)token_forms[i].kind;
            }
        }
        if (pr->kind == GSLT_PRES_PROD_TOKEN) continue;
        pr->kind = GSLT_PRES_PROD_PLAIN;
        for (int i = 0; i < n_guards; i++)
            if (strcmp(guards[i].label, pr->label) == 0) pr->guard = (uint8_t)guards[i].guard;
        for (int i = 0; i < n_role_decls; i++)
            if (strcmp(role_decls[i].label, pr->label) == 0)
                pr->role_off = roles_emit(role_decls[i].roles, nargs, pr->label);
        for (int i = 0; i < n_look_decls; i++)
            if (strcmp(look_decls[i].label, pr->label) == 0) pr->look = (uint8_t)look_decls[i].look;
        for (int i = 0; i < n_forms; i++) {
            if (strcmp(forms[i].label, pr->label) == 0) {
                pr->template_off = n_tcode;
                compile_template(forms[i].tpl, nargs, pr->label);
            }
        }
        if (pr->template_off < 0) {
            /* A production without a form denotes the expression headed by
               its label: (Label child ...). */
            if (!isupper((unsigned char)pr->label[0]))
                die("production %s has no form and a lowercase label", pr->label);
            pr->template_off = n_tcode;
            emit_t(GSLT_PRES_T_EXPR);
            emit_t(nargs + 1);
            emit_t(GSLT_PRES_T_WORD);
            emit_t(const_add(pr->label, false));
            for (int i = 0; i < nargs; i++) { emit_t(GSLT_PRES_T_ARG); emit_t(i); }
        }
    }
    for (int i = 0; i < n_forms; i++) {
        bool found = false;
        for (int p = 0; p < n_prods; p++)
            if (strcmp(prods[p].label, forms[i].label) == 0) found = true;
        if (!found) die("form %s names no production", forms[i].label);
    }
    for (int i = 0; i < n_role_decls + n_look_decls; i++) {
        const char *label = i < n_role_decls ? role_decls[i].label : look_decls[i - n_role_decls].label;
        bool found = false;
        for (int p = 0; p < n_prods; p++)
            if (strcmp(prods[p].label, label) == 0 && prods[p].kind == GSLT_PRES_PROD_PLAIN) found = true;
        if (!found) die("roles or look for %s, which is no production with a form", label);
    }
    for (int n = 0; n < n_nts; n++) {
        char fam[256];
        int lvl;
        nts[n].is_list = nts[n].name[0] == '[';
        if (nts[n].is_list || n == 0) { nts[n].family = -1; nts[n].level = 0; continue; }
        split_cat(nts[n].name, fam, &lvl);
        nts[n].family = family_id(fam);
        nts[n].level = lvl;
    }
}

/* ------------------------------------------------------------ LALR(1) */

static bool *nullable;
static TSet *first;
static int *item_base; /* item id = item_base[p] + dot */
static int n_items;

static void compute_first(void) {
    nullable = xcalloc((size_t)n_nts, sizeof *nullable);
    first = xcalloc((size_t)n_nts, sizeof *first);
    bool changed = true;
    while (changed) {
        changed = false;
        for (int p = 0; p < n_prods; p++) {
            Production *pr = &prods[p];
            bool all = true;
            for (int k = 0; k < pr->len && all; k++) {
                int s = pr->rhs[k];
                if (s >= 0) {
                    if (tset_add(&first[pr->lhs], s)) changed = true;
                    all = false;
                } else {
                    if (tset_union(&first[pr->lhs], &first[-s - 1])) changed = true;
                    if (!nullable[-s - 1]) all = false;
                }
            }
            if (all && !nullable[pr->lhs]) { nullable[pr->lhs] = true; changed = true; }
        }
    }
    item_base = xcalloc((size_t)n_prods, sizeof *item_base);
    for (int p = 0; p < n_prods; p++) { item_base[p] = n_items; n_items += prods[p].len + 1; }
}

/* FIRST of rhs[k..] of production p, and whether it is nullable */
static bool first_of_rest(int p, int k, TSet *out) {
    Production *pr = &prods[p];
    for (; k < pr->len; k++) {
        int s = pr->rhs[k];
        if (s >= 0) { tset_add(out, s); return false; }
        tset_union(out, &first[-s - 1]);
        if (!nullable[-s - 1]) return false;
    }
    return true;
}

typedef struct {
    int nk;
    int *kitem;   /* kernel item ids, sorted */
    TSet *kla;    /* their lookaheads */
    int *go;      /* per symbol: terminals then nonterminals; -1 none */
} State;

static State *states;
static int n_states, cap_states;
static int *item_prod, *item_dot;

static int find_state(const int *items, int nk) {
    for (int s = 0; s < n_states; s++) {
        if (states[s].nk != nk) continue;
        if (memcmp(states[s].kitem, items, (size_t)nk * sizeof(int)) == 0) return s;
    }
    return -1;
}

static int new_state(const int *items, const TSet *la, int nk) {
    if (n_states == cap_states) {
        cap_states = cap_states ? cap_states * 2 : 64;
        states = xrealloc(states, (size_t)cap_states * sizeof *states);
    }
    State *st = &states[n_states];
    st->nk = nk;
    st->kitem = xcalloc((size_t)nk, sizeof(int));
    st->kla = xcalloc((size_t)nk, sizeof(TSet));
    memcpy(st->kitem, items, (size_t)nk * sizeof(int));
    memcpy(st->kla, la, (size_t)nk * sizeof(TSet));
    st->go = xcalloc((size_t)(n_terms + n_nts), sizeof(int));
    for (int i = 0; i < n_terms + n_nts; i++) st->go[i] = -1;
    return n_states++;
}

/* closure of a state's kernel: items and lookaheads */
static int *cl_item;
static TSet *cl_la;
static int *cl_index; /* item id -> position in closure, or -1 */
static int cl_n;

static void closure(int s) {
    State *st = &states[s];
    for (int i = 0; i < cl_n; i++) cl_index[cl_item[i]] = -1;
    cl_n = 0;
    for (int k = 0; k < st->nk; k++) {
        cl_item[cl_n] = st->kitem[k];
        cl_la[cl_n] = st->kla[k];
        cl_index[st->kitem[k]] = cl_n;
        cl_n++;
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (int i = 0; i < cl_n; i++) {
            int it = cl_item[i], p = item_prod[it], d = item_dot[it];
            if (d >= prods[p].len || prods[p].rhs[d] >= 0) continue;
            int B = -prods[p].rhs[d] - 1;
            TSet la = {{0}};
            if (first_of_rest(p, d + 1, &la)) tset_union(&la, &cl_la[i]);
            for (int q = 0; q < n_prods; q++) {
                if (prods[q].lhs != B) continue;
                int j = item_base[q];
                if (cl_index[j] < 0) {
                    cl_index[j] = cl_n;
                    cl_item[cl_n] = j;
                    memset(&cl_la[cl_n], 0, sizeof cl_la[cl_n]);
                    cl_n++;
                    changed = true;
                }
                if (tset_union(&cl_la[cl_index[j]], &la)) changed = true;
            }
        }
    }
}

static int cmp_int(const void *a, const void *b) {
    int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}

static void build_lalr(void) {
    item_prod = xcalloc((size_t)n_items, sizeof(int));
    item_dot = xcalloc((size_t)n_items, sizeof(int));
    for (int p = 0; p < n_prods; p++)
        for (int d = 0; d <= prods[p].len; d++) { item_prod[item_base[p] + d] = p; item_dot[item_base[p] + d] = d; }
    cl_item = xcalloc((size_t)n_items, sizeof(int));
    cl_la = xcalloc((size_t)n_items, sizeof(TSet));
    cl_index = xcalloc((size_t)n_items, sizeof(int));
    for (int i = 0; i < n_items; i++) cl_index[i] = -1;

    int start_prod = -1;
    for (int p = 0; p < n_prods; p++) if (prods[p].kind == GSLT_PRES_PROD_START) start_prod = p;
    int k0 = item_base[start_prod];
    TSet la0 = {{0}};
    tset_add(&la0, 0);
    new_state(&k0, &la0, 1);

    int nsym = n_terms + n_nts;
    int *queue = NULL, qcap = 0, qhead = 0, qtail = 0;
    bool *queued = NULL;
    int queued_cap = 0;
#define ENQUEUE(s) do { \
        if ((s) >= queued_cap) { int nc = (s) * 2 + 64; queued = xrealloc(queued, (size_t)nc); memset(queued + queued_cap, 0, (size_t)(nc - queued_cap)); queued_cap = nc; } \
        if (!queued[s]) { queued[s] = true; \
            if (qtail == qcap) { qcap = qcap ? qcap * 2 : 64; queue = xrealloc(queue, (size_t)qcap * sizeof(int)); } \
            queue[qtail++] = (s); } } while (0)
    ENQUEUE(0);
    int *kbuf = xcalloc((size_t)n_items, sizeof(int));
    TSet *labuf = xcalloc((size_t)n_items, sizeof(TSet));
    while (qhead < qtail) {
        int s = queue[qhead++];
        queued[s] = false;
        closure(s);
        for (int X = 0; X < nsym; X++) {
            int sym = X < n_terms ? X : -(X - n_terms + 1);
            int nk = 0;
            for (int i = 0; i < cl_n; i++) {
                int it = cl_item[i], p = item_prod[it], d = item_dot[it];
                if (d < prods[p].len && prods[p].rhs[d] == sym) {
                    kbuf[nk] = it + 1;
                    nk++;
                }
            }
            if (!nk) continue;
            qsort(kbuf, (size_t)nk, sizeof(int), cmp_int);
            for (int j = 0; j < nk; j++) {
                memset(&labuf[j], 0, sizeof labuf[j]);
                for (int i = 0; i < cl_n; i++)
                    if (cl_item[i] + 1 == kbuf[j]) tset_union(&labuf[j], &cl_la[i]);
            }
            int t = find_state(kbuf, nk);
            if (t < 0) {
                t = new_state(kbuf, labuf, nk);
                ENQUEUE(t);
            } else {
                bool grew = false;
                for (int j = 0; j < nk; j++)
                    if (tset_union(&states[t].kla[j], &labuf[j])) grew = true;
                if (grew) ENQUEUE(t);
            }
            states[s].go[X] = t;
        }
    }
#undef ENQUEUE
    free(queue);
    free(queued);
    free(kbuf);
    free(labuf);
}

static int16_t *action_table, *goto_table;
static int n_conflicts;

static void conflict(int s, int t, const char *what, int a, int b) {
    n_conflicts++;
    fprintf(stderr, "conflict in state %d on %s '%s': %s", s,
            terms[t].kind == GSLT_PRES_TERM_TOKEN ? "token" : "terminal", terms[t].text, what);
    if (a >= 0) fprintf(stderr, " %s(%s)", prods[a].label, nts[prods[a].lhs].name);
    if (b >= 0) fprintf(stderr, " / %s(%s)", prods[b].label, nts[prods[b].lhs].name);
    fputc('\n', stderr);
    /* the items of the state, for the reader of the report */
    State *st = &states[s];
    for (int k = 0; k < st->nk; k++) {
        int it = st->kitem[k], p = item_prod[it], d = item_dot[it];
        fprintf(stderr, "    %s ->", nts[prods[p].lhs].name);
        for (int j = 0; j < prods[p].len; j++) {
            if (j == d) fputs(" .", stderr);
            int sym = prods[p].rhs[j];
            fprintf(stderr, " %s", sym >= 0 ? terms[sym].text : nts[-sym - 1].name);
        }
        if (d == prods[p].len) fputs(" .", stderr);
        fprintf(stderr, "   [%s]\n", prods[p].label);
    }
}

static void build_tables(void) {
    action_table = xcalloc((size_t)n_states * (size_t)n_terms, sizeof(int16_t));
    goto_table = xcalloc((size_t)n_states * (size_t)n_nts, sizeof(int16_t));
    if (n_states >= INT16_MAX - 1 || n_prods >= INT16_MAX - 1) die("table too large");
    for (int s = 0; s < n_states; s++) {
        int16_t *row = &action_table[(size_t)s * (size_t)n_terms];
        for (int n = 0; n < n_nts; n++) goto_table[(size_t)s * (size_t)n_nts + (size_t)n] = (int16_t)states[s].go[n_terms + n];
        for (int t = 0; t < n_terms; t++) if (states[s].go[t] >= 0) row[t] = (int16_t)(states[s].go[t] + 1);
        closure(s);
        for (int i = 0; i < cl_n; i++) {
            int it = cl_item[i], p = item_prod[it], d = item_dot[it];
            if (d != prods[p].len) continue;
            for (int t = 0; t < n_terms; t++) {
                if (!tset_has(&cl_la[i], t)) continue;
                int16_t v = prods[p].kind == GSLT_PRES_PROD_START ? GSLT_PRES_ACCEPT : (int16_t)(-p - 1);
                if (row[t] == 0) { row[t] = v; continue; }
                if (row[t] > 0 && row[t] != GSLT_PRES_ACCEPT) conflict(s, t, "shift/reduce", p, -1);
                else if (row[t] != v) conflict(s, t, "reduce/reduce", p, row[t] == GSLT_PRES_ACCEPT ? -1 : -row[t] - 1);
            }
        }
    }
}

/* ------------------------------------------------------------ lexer DFA */

#define MAXCP 0x110000u

typedef struct { uint32_t lo, hi; } Ival;
typedef struct { Ival *iv; int n; } CSet;

static CSet cset_make(void) { CSet c = {NULL, 0}; return c; }
static void cset_push(CSet *c, uint32_t lo, uint32_t hi) {
    c->iv = xrealloc(c->iv, (size_t)(c->n + 1) * sizeof(Ival));
    c->iv[c->n].lo = lo;
    c->iv[c->n].hi = hi;
    c->n++;
}
static int ival_cmp(const void *a, const void *b) {
    const Ival *x = a, *y = b;
    return (x->lo > y->lo) - (x->lo < y->lo);
}
static void cset_norm(CSet *c) {
    if (!c->n) return;
    qsort(c->iv, (size_t)c->n, sizeof(Ival), ival_cmp);
    int k = 0;
    for (int i = 1; i < c->n; i++) {
        if (c->iv[i].lo <= c->iv[k].hi + 1u) {
            if (c->iv[i].hi > c->iv[k].hi) c->iv[k].hi = c->iv[i].hi;
        } else {
            c->iv[++k] = c->iv[i];
        }
    }
    c->n = k + 1;
}
static CSet cset_minus(CSet a, CSet b) {
    CSet out = cset_make();
    for (int i = 0; i < a.n; i++) {
        uint32_t lo = a.iv[i].lo, hi = a.iv[i].hi;
        for (int j = 0; j < b.n && lo <= hi; j++) {
            if (b.iv[j].hi < lo || b.iv[j].lo > hi) continue;
            if (b.iv[j].lo > lo) cset_push(&out, lo, b.iv[j].lo - 1u);
            if (b.iv[j].hi >= hi) { lo = 1; hi = 0; break; }
            lo = b.iv[j].hi + 1u;
        }
        if (lo <= hi) cset_push(&out, lo, hi);
    }
    cset_norm(&out);
    return out;
}

static uint32_t cp_of(const Node *n) {
    if (!is_head(n, "cp") || n->n != 2) die("expected (cp N)");
    return (uint32_t)strtoul(n->kids[1]->text, NULL, 10);
}

/* a regex that denotes a set of code points, or false */
static bool regex_class(const Node *r, CSet *out) {
    if (r->kind == 's') {
        if (strcmp(r->text, "rx-any") == 0) { *out = cset_make(); cset_push(out, 0, MAXCP - 1u); return true; }
        if (strcmp(r->text, "rx-empty") == 0) { *out = cset_make(); return true; }
        return false;
    }
    if (is_head(r, "rx-char")) { *out = cset_make(); uint32_t c = cp_of(r->kids[1]); cset_push(out, c, c); return true; }
    if (is_head(r, "rx-range")) { *out = cset_make(); cset_push(out, cp_of(r->kids[1]), cp_of(r->kids[2])); return true; }
    if (is_head(r, "rx-alt")) {
        CSet a, b;
        if (!regex_class(r->kids[1], &a) || !regex_class(r->kids[2], &b)) return false;
        for (int i = 0; i < b.n; i++) cset_push(&a, b.iv[i].lo, b.iv[i].hi);
        cset_norm(&a);
        *out = a;
        return true;
    }
    if (is_head(r, "rx-minus")) {
        CSet a, b;
        if (!regex_class(r->kids[1], &a) || !regex_class(r->kids[2], &b))
            die("rx-minus is supported on sets of characters only");
        *out = cset_minus(a, b);
        return true;
    }
    return false;
}

/* elementary classes: the partition of code points by all boundaries */
static uint32_t *bounds;
static int n_bounds, cap_bounds;

static void add_bound(uint32_t b) {
    if (n_bounds == cap_bounds) {
        cap_bounds = cap_bounds ? cap_bounds * 2 : 64;
        bounds = xrealloc(bounds, (size_t)cap_bounds * sizeof *bounds);
    }
    bounds[n_bounds++] = b;
}

static void collect_bounds(const Node *r) {
    CSet c;
    if (regex_class(r, &c)) {
        for (int i = 0; i < c.n; i++) { add_bound(c.iv[i].lo); add_bound(c.iv[i].hi + 1u); }
        return;
    }
    if (r->kind == 'l')
        for (int i = 1; i < r->n; i++) collect_bounds(r->kids[i]);
}

static int cmp_u32(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

/* class k is [bounds[k], bounds[k+1]) */
static int n_classes;

static void finish_bounds(void) {
    add_bound(0);
    add_bound(MAXCP);
    qsort(bounds, (size_t)n_bounds, sizeof *bounds, cmp_u32);
    int k = 0;
    for (int i = 1; i < n_bounds; i++) if (bounds[i] != bounds[k]) bounds[++k] = bounds[i];
    n_bounds = k + 1;
    n_classes = n_bounds - 1;
    if (n_classes > 1024) die("too many character classes");
}

static int class_of_cp(uint32_t cp) {
    int lo = 0, hi = n_classes - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (bounds[mid] <= cp) lo = mid; else hi = mid - 1;
    }
    return lo;
}

/* NFA */
#define CW 16 /* class set words: 1024 classes */
typedef struct { uint64_t w[CW]; } ClassSet;

typedef struct {
    int eps[2], n_eps;
    int to;        /* transition target on `on`, or -1 */
    ClassSet on;
    int accept;    /* rule id or -1 */
} NState;

static NState *ns;
static int n_ns, cap_ns;

static int nfa_new(void) {
    if (n_ns == cap_ns) {
        cap_ns = cap_ns ? cap_ns * 2 : 256;
        ns = xrealloc(ns, (size_t)cap_ns * sizeof *ns);
    }
    memset(&ns[n_ns], 0, sizeof ns[n_ns]);
    ns[n_ns].to = -1;
    ns[n_ns].accept = -1;
    return n_ns++;
}

static void nfa_eps(int a, int b) {
    if (ns[a].n_eps < 2) { ns[a].eps[ns[a].n_eps++] = b; return; }
    /* chain through a fresh state */
    int m = nfa_new();
    ns[m].eps[0] = ns[a].eps[1];
    ns[m].eps[1] = b;
    ns[m].n_eps = 2;
    ns[a].eps[1] = m;
}

static ClassSet classes_of(const CSet *c) {
    ClassSet out;
    memset(&out, 0, sizeof out);
    for (int k = 0; k < n_classes; k++) {
        uint32_t lo = bounds[k];
        for (int i = 0; i < c->n; i++)
            if (c->iv[i].lo <= lo && lo <= c->iv[i].hi) { out.w[k >> 6] |= 1ull << (k & 63); break; }
    }
    return out;
}

typedef struct { int in, out; } Frag;

static Frag nfa_regex(const Node *r) {
    Frag f;
    CSet c;
    if (regex_class(r, &c)) {
        f.in = nfa_new();
        f.out = nfa_new();
        ns[f.in].to = f.out;
        ns[f.in].on = classes_of(&c);
        return f;
    }
    if (r->kind == 's' && strcmp(r->text, "rx-eps") == 0) {
        f.in = nfa_new();
        f.out = nfa_new();
        nfa_eps(f.in, f.out);
        return f;
    }
    if (is_head(r, "rx-seq")) {
        Frag a = nfa_regex(r->kids[1]);
        Frag b = nfa_regex(r->kids[2]);
        nfa_eps(a.out, b.in);
        f.in = a.in;
        f.out = b.out;
        return f;
    }
    if (is_head(r, "rx-alt")) {
        Frag a = nfa_regex(r->kids[1]);
        Frag b = nfa_regex(r->kids[2]);
        f.in = nfa_new();
        f.out = nfa_new();
        nfa_eps(f.in, a.in);
        nfa_eps(f.in, b.in);
        nfa_eps(a.out, f.out);
        nfa_eps(b.out, f.out);
        return f;
    }
    if (is_head(r, "rx-star") || is_head(r, "rx-plus") || is_head(r, "rx-opt")) {
        Frag a = nfa_regex(r->kids[1]);
        f.in = nfa_new();
        f.out = nfa_new();
        bool star = is_head(r, "rx-star"), plus = is_head(r, "rx-plus");
        nfa_eps(f.in, a.in);
        nfa_eps(a.out, f.out);
        if (star || plus) nfa_eps(a.out, a.in);
        if (!plus) nfa_eps(f.in, f.out);
        return f;
    }
    die("unsupported regular expression form %s", r->kind == 'l' && r->n ? r->kids[0]->text : r->text);
    return f;
}

static Frag nfa_literal(const char *s) {
    Frag f;
    f.in = nfa_new();
    int cur = f.in;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        uint32_t cp;
        if (*p < 0x80) cp = *p++;
        else if ((*p & 0xE0) == 0xC0) { cp = (uint32_t)(*p & 0x1F) << 6 | (p[1] & 0x3F); p += 2; }
        else if ((*p & 0xF0) == 0xE0) { cp = (uint32_t)(*p & 0x0F) << 12 | (uint32_t)(p[1] & 0x3F) << 6 | (p[2] & 0x3F); p += 3; }
        else { cp = (uint32_t)(*p & 0x07) << 18 | (uint32_t)(p[1] & 0x3F) << 12 | (uint32_t)(p[2] & 0x3F) << 6 | (p[3] & 0x3F); p += 4; }
        int nx = nfa_new();
        ns[cur].to = nx;
        memset(&ns[cur].on, 0, sizeof ns[cur].on);
        int k = class_of_cp(cp);
        if (bounds[k] != cp || bounds[k + 1] != cp + 1u) die("literal code point %u is not its own class", cp);
        ns[cur].on.w[k >> 6] |= 1ull << (k & 63);
        cur = nx;
    }
    f.out = cur;
    return f;
}

/* rules: symbols first (BNFC lists reserved symbols before tokens), then the
   token declarations in order, then trivia */
static int rule_kind[MAXT + 64], rule_terminal[MAXT + 64], n_rule;

typedef struct { uint64_t *w; } NSet;
static int nsw; /* words per NFA set */

static void eps_close(uint64_t *set) {
    int *stack = xcalloc((size_t)n_ns, sizeof(int));
    int sp = 0;
    for (int i = 0; i < n_ns; i++) if ((set[i >> 6] >> (i & 63)) & 1u) stack[sp++] = i;
    while (sp) {
        int s = stack[--sp];
        for (int e = 0; e < ns[s].n_eps; e++) {
            int t = ns[s].eps[e];
            if (!((set[t >> 6] >> (t & 63)) & 1u)) { set[t >> 6] |= 1ull << (t & 63); stack[sp++] = t; }
        }
    }
    free(stack);
}

static uint64_t **dsets;
static int n_dfa, cap_dfa;
static int16_t *dfa_next_table;
static int16_t *dfa_accept_table;

static int dfa_find_or_add(uint64_t *set, bool *added) {
    for (int i = 0; i < n_dfa; i++)
        if (memcmp(dsets[i], set, (size_t)nsw * sizeof(uint64_t)) == 0) { *added = false; return i; }
    if (n_dfa == cap_dfa) {
        cap_dfa = cap_dfa ? cap_dfa * 2 : 64;
        dsets = xrealloc(dsets, (size_t)cap_dfa * sizeof *dsets);
    }
    dsets[n_dfa] = xcalloc((size_t)nsw, sizeof(uint64_t));
    memcpy(dsets[n_dfa], set, (size_t)nsw * sizeof(uint64_t));
    *added = true;
    return n_dfa++;
}

static void build_lexer(void) {
    for (int i = 0; i < n_decls; i++) collect_bounds(decls[i].regex);
    for (int t = 0; t < n_terms; t++) {
        if (terms[t].kind != GSLT_PRES_TERM_SYMBOL) continue;
        const unsigned char *p = (const unsigned char *)terms[t].text;
        while (*p) {
            uint32_t cp;
            if (*p < 0x80) cp = *p++;
            else if ((*p & 0xE0) == 0xC0) { cp = (uint32_t)(*p & 0x1F) << 6 | (p[1] & 0x3F); p += 2; }
            else if ((*p & 0xF0) == 0xE0) { cp = (uint32_t)(*p & 0x0F) << 12 | (uint32_t)(p[1] & 0x3F) << 6 | (p[2] & 0x3F); p += 3; }
            else { cp = (uint32_t)(*p & 0x07) << 18 | (uint32_t)(p[1] & 0x3F) << 12 | (uint32_t)(p[2] & 0x3F) << 6 | (p[3] & 0x3F); p += 4; }
            add_bound(cp);
            add_bound(cp + 1u);
        }
    }
    finish_bounds();
    int start = nfa_new();
    for (int t = 0; t < n_terms; t++) {
        if (terms[t].kind != GSLT_PRES_TERM_SYMBOL) continue;
        Frag f = nfa_literal(terms[t].text);
        nfa_eps(start, f.in);
        rule_kind[n_rule] = GSLT_PRES_RULE_SYMBOL;
        rule_terminal[n_rule] = t;
        ns[f.out].accept = n_rule++;
    }
    for (int i = 0; i < n_decls; i++) {
        Frag f = nfa_regex(decls[i].regex);
        nfa_eps(start, f.in);
        rule_kind[n_rule] = decls[i].kind;
        rule_terminal[n_rule] = decls[i].terminal;
        ns[f.out].accept = n_rule++;
    }
    nsw = (n_ns + 63) / 64;
    uint64_t *set = xcalloc((size_t)nsw, sizeof(uint64_t));
    set[start >> 6] |= 1ull << (start & 63);
    eps_close(set);
    bool added;
    dfa_find_or_add(set, &added);
    size_t tcap = 0;
    for (int d = 0; d < n_dfa; d++) {
        if ((size_t)n_dfa * (size_t)n_classes > tcap) {
            size_t ncap = (size_t)(n_dfa * 2 + 16) * (size_t)n_classes;
            dfa_next_table = xrealloc(dfa_next_table, ncap * sizeof(int16_t));
            tcap = ncap;
        }
        for (int k = 0; k < n_classes; k++) {
            memset(set, 0, (size_t)nsw * sizeof(uint64_t));
            bool any = false;
            for (int s = 0; s < n_ns; s++) {
                if (!((dsets[d][s >> 6] >> (s & 63)) & 1u) || ns[s].to < 0) continue;
                if ((ns[s].on.w[k >> 6] >> (k & 63)) & 1u) {
                    set[ns[s].to >> 6] |= 1ull << (ns[s].to & 63);
                    any = true;
                }
            }
            int target = -1;
            if (any) {
                eps_close(set);
                target = dfa_find_or_add(set, &added);
                if (n_dfa >= INT16_MAX - 1) die("lexer DFA too large");
            }
            if ((size_t)n_dfa * (size_t)n_classes > tcap) {
                size_t ncap = (size_t)(n_dfa * 2 + 16) * (size_t)n_classes;
                dfa_next_table = xrealloc(dfa_next_table, ncap * sizeof(int16_t));
                tcap = ncap;
            }
            dfa_next_table[(size_t)d * (size_t)n_classes + (size_t)k] = (int16_t)target;
        }
    }
    dfa_accept_table = xcalloc((size_t)n_dfa, sizeof(int16_t));
    for (int d = 0; d < n_dfa; d++) {
        int best = -1;
        for (int s = 0; s < n_ns; s++)
            if (((dsets[d][s >> 6] >> (s & 63)) & 1u) && ns[s].accept >= 0 && (best < 0 || ns[s].accept < best))
                best = ns[s].accept;
        dfa_accept_table[d] = (int16_t)best;
    }
    free(set);
}

/* ------------------------------------------------------------ output */

static void put_cstr(FILE *out, const char *s) {
    fputc('"', out);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p == '"' || *p == '\\') fprintf(out, "\\%c", *p);
        else if (*p == '\n') fputs("\\n", out);
        else if (*p == '\t') fputs("\\t", out);
        else if (*p < 0x20 || *p >= 0x7f) fprintf(out, "\\%03o", *p);
        else fputc(*p, out);
    }
    fputc('"', out);
}

static uint64_t fnv1a(uint64_t h, const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) { h ^= (unsigned char)s[i]; h *= 0x100000001b3ull; }
    return h;
}

#define ARR(type, name, count, expr) do { \
        fprintf(out, "static const " type " %s_%s[] = {", prefix, name); \
        for (long i_ = 0; i_ < (long)(count); i_++) { if (i_ % 16 == 0) fputs("\n  ", out); fprintf(out, "%ld,", (long)(expr)); } \
        fputs("\n  0};\n", out); } while (0)

int main(int argc, char **argv) {
    if (argc != 6 && argc != 7) {
        fprintf(stderr, "usage: %s NAME INSPECTION FORMS ENTRY OUT.h [BUILTIN-DECLARATIONS]\n", argv[0]);
        return 2;
    }
    const char *name = argv[1];
    load_forms(argv[3], name);
    if (argc == 7) load_declarations(argv[6]);
    load_inspection(argv[2], argv[4]);
    classify_productions();
    compute_first();
    build_lalr();
    build_tables();
    if (n_conflicts) die("%d LALR(1) conflicts: the grammar is not certified unambiguous", n_conflicts);
    build_lexer();

    size_t ilen, flen;
    char *itext = read_file(argv[2], &ilen), *ftext = read_file(argv[3], &flen);
    uint64_t digest = fnv1a(fnv1a(0xcbf29ce484222325ull, itext, ilen), ftext, flen);
    if (argc == 7) {
        size_t rlen;
        char *rtext = read_file(argv[6], &rlen);
        digest = fnv1a(digest, rtext, rlen);
    }
    /* builtin positions, after the productions' roles in role_code */
    int sig_off[256], sig_len[256];
    for (int k = 0; k < n_sig_decls; k++) {
        sig_off[k] = n_role_code;
        sig_len[k] = sig_decls[k].n;
        for (int i = 0; i < sig_decls[k].n; i++) {
            if (n_role_code == (int)sizeof role_code) die("too many roles");
            role_code[n_role_code++] = sig_decls[k].roles[i];
        }
    }

    /* the tables' C name is the output file's: gslt_presentation_NAME_v1,
       or a frozen version's gslt_presentation_NAME_MAJOR_MINOR_v1 */
    char prefix[128];
    {
        const char *base = strrchr(argv[5], '/');
        base = base ? base + 1 : argv[5];
        size_t bl = strcspn(base, ".");
        if (bl == 0 || bl >= sizeof prefix) die("cannot name the tables after %s", argv[5]);
        memcpy(prefix, base, bl);
        prefix[bl] = '\0';
    }
    for (char *p = prefix; *p; p++) if (!isalnum((unsigned char)*p) && *p != '_') *p = '_';

    char tmp[4096];
    snprintf(tmp, sizeof tmp, "%s.tmp", argv[5]);
    FILE *out = fopen(tmp, "w");
    if (!out) die("cannot write %s", tmp);
    fprintf(out, "/* Generated by tools/gslt_presentation_compile_v1.c from the %s presentation\n"
                 " * module and forms table. Do not edit. */\n\n", name);
    char cert[512];
    snprintf(cert, sizeof cert,
             "LALR(1): %d states, %d productions, %d terminals, 0 conflicts (unambiguous); "
             "lexer DFA: %d states over %d code-point classes, longest match, reserved words",
             n_states, n_prods, n_terms, n_dfa, n_classes);

    fprintf(out, "static const char *const %s_terminal_text[] = {", prefix);
    for (int t = 0; t < n_terms; t++) { fputs("\n  ", out); put_cstr(out, terms[t].text); fputc(',', out); }
    fputs("\n  0};\n", out);
    ARR("uint8_t", "terminal_kind", n_terms, terms[i_].kind);
    fprintf(out, "static const char *const %s_nonterminal_name[] = {", prefix);
    for (int n = 0; n < n_nts; n++) { fputs("\n  ", out); put_cstr(out, nts[n].name); fputc(',', out); }
    fputs("\n  0};\n", out);
    ARR("int16_t", "action", (long)n_states * n_terms, action_table[i_]);
    ARR("int16_t", "goto", (long)n_states * n_nts, goto_table[i_]);
    ARR("uint16_t", "prod_lhs", n_prods, prods[i_].lhs);
    ARR("uint8_t", "prod_len", n_prods, prods[i_].len);
    {
        uint32_t off = 0;
        fprintf(out, "static const uint32_t %s_prod_rhs_off[] = {", prefix);
        for (int p = 0; p < n_prods; p++) { if (p % 16 == 0) fputs("\n  ", out); fprintf(out, "%u,", off); off += (uint32_t)prods[p].len; }
        fputs("\n  0};\n", out);
        fprintf(out, "static const int16_t %s_prod_rhs[] = {", prefix);
        int c = 0;
        for (int p = 0; p < n_prods; p++)
            for (int k = 0; k < prods[p].len; k++) { if (c++ % 16 == 0) fputs("\n  ", out); fprintf(out, "%d,", prods[p].rhs[k]); }
        fputs("\n  0};\n", out);
    }
    ARR("uint8_t", "prod_kind", n_prods, prods[i_].kind);
    ARR("uint8_t", "prod_leaf", n_prods, prods[i_].leaf);
    ARR("int32_t", "prod_template", n_prods, prods[i_].template_off);
    ARR("uint8_t", "prod_guard", n_prods, prods[i_].guard);
    fprintf(out, "static const char *const %s_prod_label[] = {", prefix);
    for (int p = 0; p < n_prods; p++) { fputs("\n  ", out); put_cstr(out, prods[p].label); fputc(',', out); }
    fputs("\n  0};\n", out);
    ARR("int32_t", "prod_role_off", n_prods, prods[i_].role_off);
    ARR("uint8_t", "role_code", n_role_code, role_code[i_]);
    ARR("uint8_t", "prod_look", n_prods, prods[i_].look);
    fprintf(out, "static const char *const %s_sig_head[] = {", prefix);
    for (int k = 0; k < n_sig_decls; k++) { fputs("\n  ", out); put_cstr(out, sig_decls[k].head); fputc(',', out); }
    fputs("\n  0};\n", out);
    ARR("uint32_t", "sig_off", n_sig_decls, sig_off[i_]);
    ARR("uint8_t", "sig_len", n_sig_decls, sig_len[i_]);
    ARR("int32_t", "tcode", n_tcode, tcode[i_]);
    fprintf(out, "static const char *const %s_tconst[] = {", prefix);
    for (int i = 0; i < n_tconst; i++) { fputs("\n  ", out); put_cstr(out, tconst[i]); fputc(',', out); }
    fputs("\n  0};\n", out);
    ARR("uint32_t", "tconst_len", n_tconst, tconst_is_string[i_] ? (strlen(tconst[i_]) | 0x80000000u) : strlen(tconst[i_]));

    /* printing order: the forms table order */
    int *order = xcalloc((size_t)n_forms, sizeof(int));
    int n_order = 0;
    for (int i = 0; i < n_forms; i++)
        for (int p = 0; p < n_prods; p++)
            if (prods[p].kind == GSLT_PRES_PROD_PLAIN && strcmp(prods[p].label, forms[i].label) == 0) {
                bool seen = false;
                for (int j = 0; j < n_order; j++) if (order[j] == p) seen = true;
                if (!seen) order[n_order++] = p;
            }
    /* productions without a form (constructor notation) print before the base forms */
    int *full = xcalloc((size_t)n_prods + (size_t)n_forms, sizeof(int));
    int n_full = 0;
    for (int p = 0; p < n_prods; p++) {
        if (prods[p].kind != GSLT_PRES_PROD_PLAIN) continue;
        bool has_form = false;
        for (int i = 0; i < n_forms; i++) if (strcmp(forms[i].label, prods[p].label) == 0) has_form = true;
        if (!has_form) full[n_full++] = p;
    }
    for (int i = 0; i < n_order; i++) full[n_full++] = order[i];
    ARR("uint16_t", "forms", n_full, full[i_]);
    ARR("int16_t", "nt_family", n_nts, nts[i_].family);
    ARR("int16_t", "nt_level", n_nts, nts[i_].level);
    ARR("uint8_t", "nt_is_list", n_nts, nts[i_].is_list);
    ARR("int16_t", "nt_list_empty", n_nts, nts[i_].list_empty);
    ARR("int16_t", "nt_list_one", n_nts, nts[i_].list_one);
    ARR("int16_t", "nt_list_cons", n_nts, nts[i_].list_cons);
    /* the parenthesizing coercion of each family: "_" with terminals */
    int *paren = xcalloc((size_t)n_families + 1u, sizeof(int));
    for (int f = 0; f < n_families; f++) {
        paren[f] = -1;
        for (int p = 0; p < n_prods; p++) {
            if (prods[p].kind != GSLT_PRES_PROD_COERCION || nts[prods[p].lhs].family != f) continue;
            bool has_terminal = false;
            for (int k = 0; k < prods[p].len; k++) if (prods[p].rhs[k] >= 0) has_terminal = true;
            if (has_terminal) paren[f] = p;
        }
    }
    ARR("int16_t", "family_paren", n_families, paren[i_]);
    int leaf_family = -1;
    for (int f = 0; f < n_families; f++) if (strcmp(families[f], leaf_cat) == 0) leaf_family = f;
    if (leaf_family < 0) die("leaf category %s is no family of the grammar", leaf_cat);
    int statement_nt = nt_find(statement_cat);
    if (statement_nt < 0) die("statement category %s missing", statement_cat);
    /* A statement is a variable scope, so its category may occur only in
       the program's list of statements. */
    for (int p = 0; p < n_prods; p++) {
        bool list_of_statements = nts[prods[p].lhs].is_list;
        for (int k = 0; k < prods[p].len; k++)
            if (prods[p].rhs[k] == -(statement_nt + 1) && !list_of_statements)
                die("the statement category %s occurs inside %s (%s); a statement must be top-level",
                    statement_cat, prods[p].label, nts[prods[p].lhs].name);
    }
    /* channel scopes */
    int *nt_channel = xcalloc((size_t)n_nts, sizeof(int));
    for (int n = 0; n < n_nts; n++) nt_channel[n] = channel_index(nts[n].name);
    for (int k = 0; k < n_channel_cats; k++)
        if (nt_find(channel_cats[k]) < 0) die("channel category %s is no category of the grammar", channel_cats[k]);
    int *terminal_scope = xcalloc((size_t)n_terms, sizeof(int));
    for (int i = 0; i < n_scope_openers; i++) {
        int t = term_find(scope_openers[i], identifier_like(scope_openers[i]) ? GSLT_PRES_TERM_KEYWORD : GSLT_PRES_TERM_SYMBOL);
        if (t < 0) die("scope opener \"%s\" is no terminal of the grammar", scope_openers[i]);
        terminal_scope[t] = 1;
    }
    int *prod_scope = xcalloc((size_t)n_prods, sizeof(int));
    for (int p = 0; p < n_prods; p++) {
        if (prods[p].len > 0 && prods[p].rhs[0] >= 0 && terminal_scope[prods[p].rhs[0]]) prod_scope[p] = 1;
        for (int k = 1; k < prods[p].len; k++)
            if (prods[p].rhs[k] >= 0 && terminal_scope[prods[p].rhs[k]])
                die("production %s has the scope opener %s after its first item", prods[p].label, terms[prods[p].rhs[k]].text);
        if (prods[p].template_off >= 0 && !prod_scope[p]) {
            /* a channel names its scope: only a closing production may use it */
            int pc = prods[p].template_off;
            if (template_uses_channel(&pc))
                die("form %s names a channel but does not begin with a scope opener", prods[p].label);
        }
    }
    int *terminal_spacing = xcalloc((size_t)n_terms, sizeof(int));
    for (int i = 0; i < n_spacings; i++) {
        int t = term_find(spacings[i].text, identifier_like(spacings[i].text) ? GSLT_PRES_TERM_KEYWORD : GSLT_PRES_TERM_SYMBOL);
        if (t < 0) die("spacing for \"%s\", which is no terminal of the grammar", spacings[i].text);
        terminal_spacing[t] = spacings[i].kind;
    }
    ARR("uint8_t", "terminal_spacing", n_terms, terminal_spacing[i_]);
    /* break points of long brace groups: the declared ones, else "," */
    int *terminal_break = xcalloc((size_t)n_terms, sizeof(int));
    if (n_breaks == 0) {
        int t = term_find(",", GSLT_PRES_TERM_SYMBOL);
        if (t >= 0) terminal_break[t] = 1;
    }
    for (int i = 0; i < n_breaks; i++) {
        int t = term_find(breaks[i], identifier_like(breaks[i]) ? GSLT_PRES_TERM_KEYWORD : GSLT_PRES_TERM_SYMBOL);
        if (t < 0) die("break point \"%s\" is no terminal of the grammar", breaks[i]);
        terminal_break[t] = 1;
    }
    ARR("uint8_t", "terminal_break", n_terms, terminal_break[i_]);
    /* layout: BNFC's block terminals are "{", ";" and "}" */
    int *terminal_layout = xcalloc((size_t)n_terms, sizeof(int));
    int lay_open = -1, lay_sep = -1, lay_close = -1;
    if (has_layout) {
        lay_open = term_find("{", GSLT_PRES_TERM_SYMBOL);
        lay_sep = term_find(";", GSLT_PRES_TERM_SYMBOL);
        lay_close = term_find("}", GSLT_PRES_TERM_SYMBOL);
        if (lay_open < 0 || lay_close < 0)
            die("a grammar with layout declarations needs the terminals \"{\" and \"}\"");
        for (int i = 0; i < n_layout_words; i++) {
            int t = term_find(layout_words[i], identifier_like(layout_words[i]) ? GSLT_PRES_TERM_KEYWORD : GSLT_PRES_TERM_SYMBOL);
            if (t < 0) die("layout word \"%s\" is no terminal of the grammar", layout_words[i]);
            terminal_layout[t] = 1;
        }
        for (int i = 0; i < n_layout_stops; i++) {
            int t = term_find(layout_stops[i], identifier_like(layout_stops[i]) ? GSLT_PRES_TERM_KEYWORD : GSLT_PRES_TERM_SYMBOL);
            if (t < 0) die("layout stop word \"%s\" is no terminal of the grammar", layout_stops[i]);
            terminal_layout[t] = 2;
        }
        if (layout_toplevel && lay_sep < 0) die("layout toplevel needs the terminal \";\"");
    }
    ARR("uint8_t", "terminal_layout", n_terms, terminal_layout[i_]);
    ARR("int8_t", "nt_channel", n_nts, nt_channel[i_]);
    ARR("uint8_t", "terminal_scope", n_terms, terminal_scope[i_]);
    ARR("uint8_t", "prod_scope", n_prods, prod_scope[i_]);

    int leaf_prod[9];
    for (int k = 0; k < 9; k++) leaf_prod[k] = -1;
    for (int p = 0; p < n_prods; p++)
        if (prods[p].kind == GSLT_PRES_PROD_TOKEN && leaf_prod[prods[p].leaf] < 0) leaf_prod[prods[p].leaf] = p;

    /* lexer */
    {
        fprintf(out, "static const uint16_t %s_class_of_low[] = {", prefix);
        for (uint32_t cp = 0; cp < 256; cp++) { if (cp % 16 == 0) fputs("\n  ", out); fprintf(out, "%d,", class_of_cp(cp)); }
        fputs("\n  0};\n", out);
        int first_high = class_of_cp(256);
        int n_high = n_classes - first_high;
        ARR("uint32_t", "high_lo", n_high, i_ == 0 ? 256u : bounds[first_high + i_]);
        ARR("uint32_t", "high_hi", n_high, bounds[first_high + i_ + 1] - 1u);
        ARR("uint16_t", "high_class", n_high, first_high + i_);
        ARR("int16_t", "dfa_next", (long)n_dfa * n_classes, dfa_next_table[i_]);
        ARR("int16_t", "dfa_accept", n_dfa, dfa_accept_table[i_]);
        ARR("uint8_t", "rule_kind", n_rule, rule_kind[i_]);
        ARR("int16_t", "rule_terminal", n_rule, rule_terminal[i_]);
        int n_res = 0;
        fprintf(out, "static const char *const %s_reserved_text[] = {", prefix);
        for (int t = 0; t < n_terms; t++)
            if (terms[t].kind == GSLT_PRES_TERM_KEYWORD || terms[t].kind == GSLT_PRES_TERM_SYMBOL) {
                fputs("\n  ", out); put_cstr(out, terms[t].text); fputc(',', out); n_res++;
            }
        fputs("\n  0};\n", out);
        fprintf(out, "static const int16_t %s_reserved_terminal[] = {", prefix);
        for (int t = 0; t < n_terms; t++)
            if (terms[t].kind == GSLT_PRES_TERM_KEYWORD || terms[t].kind == GSLT_PRES_TERM_SYMBOL) fprintf(out, "%d,", t);
        fputs("0};\n", out);
        fprintf(out, "static const char *const %s_line_comment[] = {", prefix);
        for (int i = 0; i < n_line_comments; i++) { put_cstr(out, line_comments[i]); fputc(',', out); }
        fputs("0};\n", out);
        fprintf(out, "static const char *const %s_block_open[] = {", prefix);
        for (int i = 0; i < n_block_comments; i++) { put_cstr(out, block_open[i]); fputc(',', out); }
        fputs("0};\n", out);
        fprintf(out, "static const char *const %s_block_close[] = {", prefix);
        for (int i = 0; i < n_block_comments; i++) { put_cstr(out, block_close[i]); fputc(',', out); }
        fputs("0};\n", out);

        fprintf(out, "\nstatic const GSLTPresentationTablesV1 %s = {\n", prefix);
        fputs("  .name = ", out); put_cstr(out, name); fputs(",\n", out);
        fputs("  .version = ", out); put_cstr(out, version); fputs(",\n", out);
        fputs("  .certificate = ", out); put_cstr(out, cert); fputs(",\n", out);
        fprintf(out, "  .source_digest = \"fnv1a64:%016llx\",\n", (unsigned long long)digest);
        fputs("  .generator = \"gslt_presentation_compile_v1\",\n", out);
        fprintf(out, "  .n_terminals = %d, .n_nonterminals = %d, .n_states = %d, .n_productions = %d, .eof_terminal = 0,\n",
                n_terms, n_nts, n_states, n_prods);
        fprintf(out, "  .terminal_text = %s_terminal_text, .terminal_kind = %s_terminal_kind, .nonterminal_name = %s_nonterminal_name,\n", prefix, prefix, prefix);
        fprintf(out, "  .action = %s_action, .goto_ = %s_goto,\n", prefix, prefix);
        fprintf(out, "  .prod_lhs = %s_prod_lhs, .prod_len = %s_prod_len, .prod_rhs_off = %s_prod_rhs_off, .prod_rhs = %s_prod_rhs,\n", prefix, prefix, prefix, prefix);
        fprintf(out, "  .prod_kind = %s_prod_kind, .prod_leaf = %s_prod_leaf, .prod_template = %s_prod_template, .prod_guard = %s_prod_guard, .prod_label = %s_prod_label,\n", prefix, prefix, prefix, prefix, prefix);
        fprintf(out, "  .prod_role_off = %s_prod_role_off, .role_code = %s_role_code, .prod_look = %s_prod_look,\n", prefix, prefix, prefix);
        fprintf(out, "  .n_sigs = %d, .sig_head = %s_sig_head, .sig_off = %s_sig_off, .sig_len = %s_sig_len,\n", n_sig_decls, prefix, prefix, prefix);
        fprintf(out, "  .tcode = %s_tcode, .n_tcode = %d, .tconst = %s_tconst, .tconst_len = %s_tconst_len, .n_tconst = %d,\n", prefix, n_tcode, prefix, prefix, n_tconst);
        fprintf(out, "  .forms = %s_forms, .n_forms = %d,\n", prefix, n_full);
        fprintf(out, "  .nt_family = %s_nt_family, .nt_level = %s_nt_level, .nt_is_list = %s_nt_is_list,\n", prefix, prefix, prefix);
        fprintf(out, "  .nt_list_empty = %s_nt_list_empty, .nt_list_one = %s_nt_list_one, .nt_list_cons = %s_nt_list_cons,\n", prefix, prefix, prefix);
        fprintf(out, "  .family_paren = %s_family_paren, .n_families = %d, .leaf_family = %d, .statement_nt = %d, .entry_nt = 1,\n", prefix, n_families, leaf_family, statement_nt);
        fputs("  .statement_end = ", out); put_cstr(out, statement_end); fputs(",\n", out);
        fputs("  .leaf_prod = {", out);
        for (int k = 0; k < 9; k++) fprintf(out, "%d,", leaf_prod[k]);
        fputs("},\n", out);
        fprintf(out, "  .n_channels = %d, .nt_channel = %s_nt_channel, .terminal_scope = %s_terminal_scope, .prod_scope = %s_prod_scope,\n",
                n_channel_cats, prefix, prefix, prefix);
        fprintf(out, "  .terminal_spacing = %s_terminal_spacing,\n", prefix);
        fprintf(out, "  .terminal_break = %s_terminal_break,\n", prefix);
        fprintf(out, "  .has_layout = %s, .layout_toplevel = %s, .terminal_layout = %s_terminal_layout,\n",
                has_layout ? "true" : "false", layout_toplevel ? "true" : "false", prefix);
        fprintf(out, "  .layout_open = %d, .layout_sep = %d, .layout_close = %d,\n", lay_open, lay_sep, lay_close);
        fprintf(out, "  .n_classes = %d, .class_of_low = %s_class_of_low, .n_high = %d, .high_lo = %s_high_lo, .high_hi = %s_high_hi, .high_class = %s_high_class,\n",
                n_classes, prefix, n_high, prefix, prefix, prefix);
        fprintf(out, "  .n_dfa = %d, .dfa_next = %s_dfa_next, .dfa_accept = %s_dfa_accept,\n", n_dfa, prefix, prefix);
        fprintf(out, "  .n_rules = %d, .rule_kind = %s_rule_kind, .rule_terminal = %s_rule_terminal,\n", n_rule, prefix, prefix);
        fprintf(out, "  .n_reserved = %d, .reserved_text = %s_reserved_text, .reserved_terminal = %s_reserved_terminal,\n", n_res, prefix, prefix);
        fprintf(out, "  .n_line_comments = %d, .line_comment = %s_line_comment,\n", n_line_comments, prefix);
        fprintf(out, "  .n_block_comments = %d, .block_open = %s_block_open, .block_close = %s_block_close,\n", n_block_comments, prefix, prefix);
        fputs("};\n", out);
    }
    if (fclose(out) != 0) die("cannot finish %s", tmp);
    if (rename(tmp, argv[5]) != 0) die("cannot move %s into place", tmp);
    fprintf(stderr, "%s: %s\n", name, cert);
    return 0;
}

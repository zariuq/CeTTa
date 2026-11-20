/* gslt_c_answers_v1: GSLT relations compiled to C (langdef-compile petta-direct --target c, through
 * rulec), run on recorded calls and compared with the answers recorded for them.
 *
 *   cc -O2 -I. -DGSLT_C_PROGRAM='"PROGRAM.c"' tests/support/gslt_c_answers_v1.c -o gslt-c-answers
 *   gslt-c-answers RECORDED...
 *
 * A RECORDED file holds rows of one of two forms:
 *   (demand CALL), (petta-answers CALL (answer OUT ...) ...), (out-of-domain CALL REASON)
 *       as tests/langdef/bnf/plain_bnf_tcalc_recorded_calls_v1.tcalc: what PeTTa answered each call,
 *       and the calls outside the relations' declared domain, which no answer is specified for;
 *   (gslt-capture-v1 MODE (IN ...) ((gslt:result:MODE OUT ...) ...))
 *       a PeTTa run's capture of the calls it answered; a call whose input holds a non-integer number
 *       is outside the domain (the ground integer relations presuppose the builtin-integer carrier).
 * CALL is (MODE IN ...), MODE a selected mode R:BITS.  Each call is run by the compiled judgment MODE
 * (rc_solve, every answer); its answers must be the recorded ones, as sets of atoms.  Prints a line
 * per call, the time the compiled program took, and exits 1 on any difference.
 *
 *   gslt-c-answers --calls FILE
 *
 * runs the calls of FILE, one per line, MODE IN ..., and prints each call's answers as the PeTTa
 * target's entry collapses them: ((gslt:result:MODE OUT ...) ...), each answer once, or (). */
#define _GNU_SOURCE
#include "native/rule_runtime/rc_rt.c"
#include "native/rule_components/rc_atoms.c"
#include GSLT_C_PROGRAM

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---- S-expressions ------------------------------------------------------------ */

enum { N_LIST, N_SYM, N_INT, N_STR, N_FLOAT };
typedef struct Node { int kind; char *text; size_t len; long long ival; struct Node **kid; size_t n; } Node;

static void *xalloc(size_t n) { void *p = calloc(1, n ? n : 1); if (!p) { fputs("out of memory\n", stderr); exit(2); } return p; }

static const char *skip(const char *p) {
    for (;;) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (*p != ';') return p;
        while (*p && *p != '\n') p++;
    }
}

static Node *parse(const char **pp) {
    const char *p = skip(*pp);
    if (!*p) { *pp = p; return NULL; }
    Node *x = xalloc(sizeof *x);
    if (*p == '(') {
        p++;
        size_t cap = 0;
        x->kind = N_LIST;
        for (;;) {
            p = skip(p);
            if (*p == ')') { p++; break; }
            if (!*p) { fputs("unterminated list\n", stderr); exit(2); }
            *pp = p;
            Node *k = parse(pp);
            p = *pp;
            if (x->n == cap) { cap = cap ? 2 * cap : 4; x->kid = realloc(x->kid, cap * sizeof *x->kid); if (!x->kid) exit(2); }
            x->kid[x->n++] = k;
        }
    } else if (*p == '"') {
        p++;
        size_t cap = 16;
        x->kind = N_STR;
        x->text = xalloc(cap);
        while (*p != '"') {
            if (!*p) { fputs("unterminated string\n", stderr); exit(2); }
            char c = *p++;
            if (c == '\\') { c = *p++; if (c == 'n') c = '\n'; else if (c == 't') c = '\t'; else if (c == 'r') c = '\r'; }
            if (x->len + 1 >= cap) { cap *= 2; x->text = realloc(x->text, cap); if (!x->text) exit(2); }
            x->text[x->len++] = c;
        }
        p++;
    } else {
        const char *q = p;
        while (*q && *q != ' ' && *q != '\t' && *q != '\n' && *q != '\r' && *q != '(' && *q != ')' && *q != '"') q++;
        x->len = (size_t)(q - p);
        x->text = strndup(p, x->len);
        char *end;
        long long v = strtoll(x->text, &end, 10);
        bool digits = (x->text[0] >= '0' && x->text[0] <= '9') || (x->text[0] == '-' && x->text[1] >= '0' && x->text[1] <= '9');
        if (digits && !*end) { x->kind = N_INT; x->ival = v; }
        else if (digits && (strchr(x->text, '.') || strchr(x->text, 'e') || strchr(x->text, 'E'))) { strtod(x->text, &end); x->kind = *end ? N_SYM : N_FLOAT; }
        else x->kind = N_SYM;
        p = q;
    }
    *pp = p;
    return x;
}

static void node_free(Node *x) {
    if (!x) return;
    for (size_t i = 0; i < x->n; i++) node_free(x->kid[i]);
    free(x->kid);
    free(x->text);
    free(x);
}

static bool is_sym(const Node *x, const char *s) { return x && x->kind == N_SYM && !strcmp(x->text, s); }
static bool has_float(const Node *x) {
    if (x->kind == N_FLOAT) return true;
    for (size_t i = 0; i < x->n; i++) if (has_float(x->kid[i])) return true;
    return false;
}

/* ---- The program's constructors by name ----------------------------------------- */

static int *ctor_slot; static size_t ctor_slots;
static uint64_t hash_name(const char *s) { uint64_t h = 1469598103934665603ull; while (*s) { h ^= (uint8_t)*s++; h *= 1099511628211ull; } return h; }
static void index_ctors(void) {
    ctor_slots = 1;
    while (ctor_slots < 4u * (size_t)rc_nctors) ctor_slots *= 2;
    ctor_slot = xalloc(ctor_slots * sizeof *ctor_slot);
    for (int c = 0; c < rc_nctors; c++)
        for (size_t i = hash_name(rc_ctor_name[c]) & (ctor_slots - 1);; i = (i + 1) & (ctor_slots - 1))
            if (!ctor_slot[i]) { ctor_slot[i] = c + 1; break; }
}
static int ctor_named(const char *name) {
    for (size_t i = hash_name(name) & (ctor_slots - 1); ctor_slot[i]; i = (i + 1) & (ctor_slots - 1))
        if (!strcmp(rc_ctor_name[ctor_slot[i] - 1], name)) return ctor_slot[i] - 1;
    return -1;
}
static int c_int, c_string, c_symbol, c_expression, s_term;

/* an atom as a value of the program's atoms sort */
static T *to_term(const Node *x) {
    switch (x->kind) {
    case N_INT: return rc_atoms_int((uint16_t)c_int, x->ival);
    case N_STR: return rc_atoms_bytes((uint16_t)c_string, (const uint8_t *)x->text, x->len);
    case N_SYM: {
        int c = ctor_named(x->text);
        if (c >= 0 && !rc_ctor_judgment[c] && rc_ctor_sort[c] == s_term && rc_ctor_arity[c] == 0) return rc_construct(c, NULL);
        return rc_atoms_bytes((uint16_t)c_symbol, (const uint8_t *)x->text, x->len);
    }
    case N_LIST: {
        T **kids = xalloc((x->n + 1) * sizeof *kids);
        T *r = NULL;
        if (x->n && x->kid[0]->kind == N_SYM) {
            char *name = xalloc(x->kid[0]->len + 32);
            sprintf(name, "(%s %zu)", x->kid[0]->text, x->n - 1);
            int c = ctor_named(name);
            free(name);
            if (c >= 0 && !rc_ctor_judgment[c] && rc_ctor_sort[c] == s_term) {
                for (size_t i = 1; i < x->n; i++) kids[i - 1] = to_term(x->kid[i]);
                r = rc_construct(c, kids);
            }
        }
        if (!r) {
            for (size_t i = 0; i < x->n; i++) kids[i] = to_term(x->kid[i]);
            r = rc_mkv((uint16_t)c_expression, (uint32_t)x->n, kids);
        }
        free(kids);
        return r;
    }
    default:
        fprintf(stderr, "a float is no atom of the program\n");
        exit(2);
    }
}

static void print_term(FILE *o, T *x, int depth) {
    int64_t v;
    if (rc_atoms_int_of(x, (uint16_t)c_int, &v)) { fprintf(o, "%lld", (long long)v); return; }
    if (x->flags & RC_FLAG_BYTES) {
        if (x->tag == c_string) fprintf(o, "\"%.*s\"", (int)x->n, (const char *)rc_bytes(x));
        else fprintf(o, "%.*s", (int)x->n, (const char *)rc_bytes(x));
        return;
    }
    const char *name = rc_ctor_name[x->tag];
    if (x->tag == c_expression) {
        fputc('(', o);
        for (uint32_t i = 0; i < x->n; i++) { if (i) fputc(' ', o); print_term(o, x->k[i], depth + 1); }
        fputc(')', o);
        return;
    }
    if (name[0] != '(') { fputs(name, o); return; }
    const char *sp = strrchr(name, ' ');
    fprintf(o, "(%.*s", (int)(sp - name - 1), name + 1);
    for (uint32_t i = 0; i < x->n; i++) { fputc(' ', o); print_term(o, x->k[i], depth + 1); }
    fputc(')', o);
}

static void print_node(FILE *o, const Node *x, int depth) {
    if (x->kind == N_INT) { fprintf(o, "%lld", x->ival); return; }
    if (x->kind == N_STR) { fprintf(o, "\"%.*s\"", (int)x->len, x->text); return; }
    if (x->kind != N_LIST) { fputs(x->text, o); return; }
    if (depth > 12) { fputs("...", o); return; }
    fputc('(', o);
    for (size_t i = 0; i < x->n; i++) { if (i) fputc(' ', o); print_node(o, x->kid[i], depth + 1); }
    fputc(')', o);
}

/* ---- Answers -------------------------------------------------------------------- */

typedef struct { T **out; size_t n; } Answer;   /* the outputs of one answer */
typedef struct { RcK k; T **items; size_t n, cap; } Collect;
static void collect(const RcK *k, T *a) {
    Collect *c = (Collect *)(void *)k;
    for (size_t i = 0; i < c->n; i++) if (rc_atoms_equal(c->items[i], a)) return;
    if (c->n == c->cap) { c->cap = c->cap ? 2 * c->cap : 8; c->items = realloc(c->items, c->cap * sizeof *c->items); if (!c->items) exit(2); }
    c->items[c->n++] = a;
}

/* the outputs of an answer of judgment J: its one output, a tuple's children, or none */
static size_t outputs_of(int j, T *a, T **out, size_t cap) {
    const char *sort = rc_sort_name[rc_ctor_sort[j]];
    if (!strcmp(sort, "Term")) { out[0] = a; return 1; }
    if (!strcmp(sort, "Unit")) return 0;
    for (uint32_t i = 0; i < a->n && i < cap; i++) out[i] = a->k[i];
    return a->n;
}

static long agree, differ, outside;
static double solve_seconds;

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec + 1e-9 * (double)t.tv_nsec; }

/* runs CALL = (MODE IN ...) and compares its answers with EXPECTED, each a list of outputs (the
 * nodes' children from index `from` on) */
static void check_call(const Node *call, Node *const *expected, size_t nexp, size_t from) {
    rc_runtime_reset();
    const char *mode = call->kid[0]->text;
    int j = ctor_named(mode);
    if (j < 0 || !rc_ctor_judgment[j]) { printf("DIFFER %s: no compiled judgment of this name\n", mode); differ++; return; }
    size_t nin = call->n - 1;
    if ((int)nin != rc_ctor_arity[j]) { printf("DIFFER %s: %zu inputs, the judgment takes %d\n", mode, nin, rc_ctor_arity[j]); differ++; return; }
    T **in = xalloc((nin + 1) * sizeof *in);
    for (size_t i = 0; i < nin; i++) in[i] = to_term(call->kid[i + 1]);
    Collect c = {{collect}, NULL, 0, 0};
    double t0 = now();
    rc_solve(j, in, &c.k);
    solve_seconds += now() - t0;
    /* each expected answer must be one of the program's, and as many */
    size_t found = 0;
    bool *met = xalloc(c.n + 1);
    T *outs[64];
    for (size_t e = 0; e < nexp; e++) {
        const Node *ans = expected[e];
        bool hit = false;
        for (size_t a = 0; a < c.n && !hit; a++) {
            size_t no = outputs_of(j, c.items[a], outs, 64);
            if (no != ans->n - from) continue;
            bool same = true;
            for (size_t i = 0; i < no && same; i++) same = rc_atoms_equal(outs[i], to_term(ans->kid[from + i]));
            if (same) { hit = true; if (!met[a]) { met[a] = true; found++; } }
        }
        if (!hit) {
            printf("   only petta:");
            for (size_t i = from; i < ans->n; i++) { putchar(' '); print_node(stdout, ans->kid[i], 0); }
            putchar('\n');
        }
    }
    size_t nexp_set = nexp;   /* recorded answers are distinct */
    bool same = found == c.n && found == nexp_set;
    printf("%s %s: recorded %zu answers, C %zu\n", same ? "agree" : "DIFFER", mode, nexp_set, c.n);
    if (!same)
        for (size_t a = 0; a < c.n; a++)
            if (!met[a]) {
                printf("   only C: ");
                size_t no = outputs_of(j, c.items[a], outs, 64);
                for (size_t i = 0; i < no; i++) { if (i) putchar(' '); print_term(stdout, outs[i], 0); }
                putchar('\n');
            }
    if (same) agree++; else differ++;
    free(met);
    free(in);
    free(c.items);
}

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = xalloc((size_t)n + 1);
    if (fread(b, 1, (size_t)n, f) != (size_t)n) exit(2);
    fclose(f);
    return b;
}

static bool same_node(const Node *a, const Node *b) {
    if (a->kind != b->kind || a->n != b->n || a->len != b->len) return false;
    if (a->kind == N_INT) return a->ival == b->ival;
    if (a->kind != N_LIST) return !memcmp(a->text, b->text, a->len);
    for (size_t i = 0; i < a->n; i++) if (!same_node(a->kid[i], b->kid[i])) return false;
    return true;
}

static void run_file(const char *path) {
    char *text = slurp(path);
    const char *p = text;
    Node **rows = NULL;
    size_t nrows = 0, cap = 0;
    for (Node *x; (x = parse(&p));) {
        if (nrows == cap) { cap = cap ? 2 * cap : 64; rows = realloc(rows, cap * sizeof *rows); if (!rows) exit(2); }
        rows[nrows++] = x;
    }
    for (size_t r = 0; r < nrows; r++) {
        Node *x = rows[r];
        if (x->kind != N_LIST || !x->n) continue;
        if (is_sym(x->kid[0], "out-of-domain") && x->n == 3) {
            printf("out of domain: %s: %s\n", x->kid[1]->kid[0]->text, x->kid[2]->kind == N_STR ? x->kid[2]->text : "");
            outside++;
            continue;
        }
        if (is_sym(x->kid[0], "petta-answers") && x->n >= 2) {
            bool excluded = false;
            for (size_t q = 0; q < nrows && !excluded; q++)
                if (is_sym(rows[q]->kid[0], "out-of-domain") && rows[q]->n == 3 && same_node(rows[q]->kid[1], x->kid[1])) excluded = true;
            if (!excluded) check_call(x->kid[1], x->kid + 2, x->n - 2, 1);
            continue;
        }
        if (is_sym(x->kid[0], "gslt-capture-v1") && x->n >= 4) {
            const Node *mode = x->kid[1], *ins = x->kid[2], *answers = x->kid[3];
            if (has_float(ins)) { printf("out of domain: %s: the input holds a non-integer number\n", mode->text); outside++; continue; }
            Node call = {N_LIST, NULL, 0, 0, xalloc((ins->n + 1) * sizeof(Node *)), ins->n + 1};
            call.kid[0] = (Node *)mode;
            for (size_t i = 0; i < ins->n; i++) call.kid[i + 1] = ins->kid[i];
            check_call(&call, answers->kid, answers->n, 1);
            free(call.kid);
        }
    }
    for (size_t r = 0; r < nrows; r++) node_free(rows[r]);
    free(rows);
    free(text);
}

/* --calls: each line MODE IN ...; its answers printed */
static int run_calls(const char *path) {
    char *text = slurp(path);
    for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
        const char *p = skip(line);
        if (!*p) continue;
        const char *q = p;
        while (*q && *q != ' ') q++;
        char *mode = strndup(p, (size_t)(q - p));
        int j = ctor_named(mode);
        if (j < 0 || !rc_ctor_judgment[j]) { fprintf(stderr, "%s: no compiled judgment of this name\n", mode); return 2; }
        rc_runtime_reset();
        T *in[64];
        int nin = 0;
        for (Node *x; (x = parse(&q));) {
            if (nin == 64) { fprintf(stderr, "too many inputs\n"); return 2; }
            in[nin++] = to_term(x);
            node_free(x);
        }
        if (nin != rc_ctor_arity[j]) { fprintf(stderr, "%s takes %d inputs, the call has %d\n", mode, rc_ctor_arity[j], nin); return 2; }
        Collect c = {{collect}, NULL, 0, 0};
        rc_solve(j, in, &c.k);
        putchar('(');
        for (size_t a = 0; a < c.n; a++) {
            T *outs[64];
            size_t no = outputs_of(j, c.items[a], outs, 64);
            printf("%s(gslt:result:%s", a ? " " : "", mode);
            for (size_t i = 0; i < no; i++) { putchar(' '); print_term(stdout, outs[i], 0); }
            putchar(')');
        }
        puts(")");
        free(c.items);
        free(mode);
    }
    free(text);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: gslt-c-answers RECORDED... | --calls FILE\n"); return 2; }
    rc_runtime_init(0);
    rc_components_init();
    index_ctors();
    c_int = ctor_named("other integer");
    c_string = ctor_named("other string");
    c_symbol = ctor_named("other symbol");
    c_expression = ctor_named("other expression");
    s_term = c_int >= 0 ? rc_ctor_sort[c_int] : -1;
    if (c_int < 0 || c_string < 0 || c_symbol < 0 || c_expression < 0) { fprintf(stderr, "not a GSLT program compiled to C\n"); return 2; }
    if (!strcmp(argv[1], "--calls")) return argc == 3 ? run_calls(argv[2]) : 2;
    for (int i = 1; i < argc; i++) run_file(argv[i]);
    printf("%s: %ld calls agree, %ld differ, %ld out of domain; the compiled program took %.3f s\n",
           differ || !agree ? "FAIL" : "PASS", agree, differ, outside, solve_seconds);
    return differ || !agree ? 1 : 0;
}

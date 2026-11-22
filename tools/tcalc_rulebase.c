/* tcalc_rulebase.c - a rule base exported from the C engine's tables (one atom per declaration:
 * (rule ID NAME (holes ...) (take ...) (keep ...) (tell ...) (neq ...) (absent ...) (count ...)
 *  (op ...) (guard ...) (fresh ...) (inputs ...) (birth B) (make ...)), (fact ID SPACE TERM), and
 * declarations) written as tcalc@0.1 rules and rows.
 *
 *   tcalc_rulebase RULES-FILE > PROGRAM.tcalc
 *
 * A row of space S is (at sS TERM), so spaces stay apart and a pattern meets only rows of its space.
 * A rule rID reads its keeps, takes its takes, tells its equations, asks its absent and count queries
 * and makes its makes, each in its space; its holes are its variables ($hN as written).  The holes a
 * caller binds (inputs) are listed as "; inputs rID $hI ...", for a driver that fires the rule at a
 * named selection with those holes bound.  Hole sorts are not read: tcalc@0.1 matches untyped, so a
 * rule means the same only while every row is well sorted.
 *
 * A rule using a form tcalc@0.1 does not have is not written; it is listed as "; refused rID REASON"
 * and counted on stderr: disequations (neq), sealed operations (op) and guards (guard), a minted fresh
 * name (fresh $hN), a bound hole with an avoid set, a birth other than 0.  Declarations (sort-def,
 * con, fresh-name, protect, seal, unset) are the engine's and are listed as comments.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Sx { char *atom; struct Sx **item; size_t n, cap; } Sx;

static void *grow(void *p, size_t n) {
    p = realloc(p, n);
    if (!p && n) { perror("tcalc_rulebase"); exit(3); }
    return p;
}

static const char *at;
static size_t line = 1;
static void skip(void) {
    for (;;) {
        while (*at && isspace((unsigned char)*at)) { if (*at == '\n') line++; at++; }
        if (*at == ';') { while (*at && *at != '\n') at++; continue; }
        return;
    }
}
static Sx *read_sx(void) {
    skip();
    if (!*at) return NULL;
    Sx *x = calloc(1, sizeof *x);
    if (!x) { perror("tcalc_rulebase"); exit(3); }
    if (*at == '(') {
        at++;
        for (;;) {
            skip();
            if (!*at) { fprintf(stderr, "tcalc_rulebase: line %zu: unclosed list\n", line); exit(2); }
            if (*at == ')') { at++; return x; }
            if (x->n == x->cap) { x->cap = x->cap ? 2 * x->cap : 8; x->item = grow(x->item, x->cap * sizeof *x->item); }
            x->item[x->n++] = read_sx();
        }
    }
    if (*at == ')') { fprintf(stderr, "tcalc_rulebase: line %zu: unexpected )\n", line); exit(2); }
    const char *s = at;
    if (*at == '"') {
        at++;
        while (*at && *at != '"') { if (*at == '\\' && at[1]) at++; at++; }
        if (*at) at++;
    } else {
        while (*at && !isspace((unsigned char)*at) && *at != '(' && *at != ')') at++;
    }
    x->atom = strndup(s, (size_t)(at - s));
    return x;
}

static int is(const Sx *x, const char *s) { return x && x->atom && !strcmp(x->atom, s); }
static int head(const Sx *x, const char *s) { return x && !x->atom && x->n && is(x->item[0], s); }

static void print(FILE *o, const Sx *x) {
    if (x->atom) { fputs(x->atom, o); return; }
    fputc('(', o);
    for (size_t i = 0; i < x->n; i++) { if (i) fputc(' ', o); print(o, x->item[i]); }
    fputc(')', o);
}
/* a pattern (SPACE TERM) as the row (at sSPACE TERM) */
static void placed(FILE *o, const Sx *p) {
    if (p->atom || p->n != 2 || !p->item[0]->atom) {
        fprintf(stderr, "tcalc_rulebase: a pattern is not (SPACE TERM)\n");
        exit(2);
    }
    fprintf(o, "(at s%s ", p->item[0]->atom);
    print(o, p->item[1]);
    fputc(')', o);
}

static const Sx *field(const Sx *rule, const char *name) {
    for (size_t i = 3; i < rule->n; i++) if (head(rule->item[i], name)) return rule->item[i];
    fprintf(stderr, "tcalc_rulebase: rule %s has no %s field\n", rule->item[1]->atom ? rule->item[1]->atom : "?", name);
    exit(2);
}

enum { NEQ, OP, GUARD, FRESH, BOUND, BIRTH, NREASONS };
static const char *const reason[NREASONS] = {
    "disequations (neq)", "sealed operations (op)", "guards (guard)", "a minted fresh name (fresh)",
    "a bound hole with an avoid set", "a birth other than 0"};

int main(int argc, char **argv) {
    if (argc != 2) { fputs("usage: tcalc_rulebase RULES-FILE > PROGRAM.tcalc\n", stderr); return 2; }
    FILE *f = fopen(argv[1], "r");
    if (!f) { perror(argv[1]); return 2; }
    size_t len = 0, cap = 1 << 16;
    char *text = grow(NULL, cap);
    for (size_t r; (r = fread(text + len, 1, cap - len - 1, f)) > 0;) {
        len += r;
        if (len + 1 == cap) { cap *= 2; text = grow(text, cap); }
    }
    text[len] = 0;
    fclose(f);
    at = text;
    size_t written = 0, refused = 0, count[NREASONS] = {0}, facts = 0;
    printf("; a rule base read for tcalc@0.1: rows (at sSPACE TERM), holes as variables\n");
    for (Sx *x; (x = read_sx());) {
        if (head(x, "rule") && x->n >= 4) {
            const char *id = x->item[1]->atom ? x->item[1]->atom : "?";
            int why = -1;
            if (field(x, "neq")->n > 1) why = NEQ;
            else if (field(x, "op")->n > 1) why = OP;
            else if (field(x, "guard")->n > 1) why = GUARD;
            else if (field(x, "fresh")->n > 1) why = FRESH;
            else if (field(x, "birth")->n != 2 || !is(field(x, "birth")->item[1], "0")) why = BIRTH;
            else {
                const Sx *holes = field(x, "holes");
                for (size_t i = 1; i < holes->n && why < 0; i++) {
                    const Sx *h = holes->item[i];
                    if (!h->atom && h->n >= 3 && head(h->item[2], "bound")) why = BOUND;
                }
            }
            if (why >= 0) {
                printf("; refused r%s %s: %s\n", id, x->item[2]->atom ? x->item[2]->atom : "?", reason[why]);
                refused++; count[why]++;
                continue;
            }
            printf("(rule r%s", id);
            const Sx *take = field(x, "take"), *keep = field(x, "keep"), *tell = field(x, "tell"),
                     *absent = field(x, "absent"), *cnt = field(x, "count"), *make = field(x, "make");
            if (take->n > 1) { printf(" (take"); for (size_t i = 1; i < take->n; i++) { putchar(' '); placed(stdout, take->item[i]); } putchar(')'); }
            if (keep->n > 1) { printf(" (read"); for (size_t i = 1; i < keep->n; i++) { putchar(' '); placed(stdout, keep->item[i]); } putchar(')'); }
            if (tell->n > 1) {
                printf(" (tell");
                for (size_t i = 1; i < tell->n; i++) {
                    const Sx *e = tell->item[i];
                    if (e->atom || e->n != 2) { fprintf(stderr, "tcalc_rulebase: rule %s: a tell is not (A B)\n", id); return 2; }
                    printf(" (= "); print(stdout, e->item[0]); putchar(' '); print(stdout, e->item[1]); putchar(')');
                }
                putchar(')');
            }
            for (size_t i = 1; i < absent->n; i++) { printf(" (absent "); placed(stdout, absent->item[i]); putchar(')'); }
            for (size_t i = 1; i < cnt->n; i++) {
                const Sx *q = cnt->item[i];
                if (q->atom || q->n != 3 || !q->item[2]->atom) { fprintf(stderr, "tcalc_rulebase: rule %s: a count is not (SPACE PAT N)\n", id); return 2; }
                printf(" (count %s (at s%s ", q->item[2]->atom, q->item[0]->atom);
                print(stdout, q->item[1]);
                printf("))");
            }
            if (make->n > 1) { printf(" (make"); for (size_t i = 1; i < make->n; i++) { putchar(' '); placed(stdout, make->item[i]); } putchar(')'); }
            printf(")\n");
            const Sx *inputs = field(x, "inputs");
            if (inputs->n > 1 && !inputs->item[1]->atom && inputs->item[1]->n) {
                printf("; inputs r%s", id);
                for (size_t i = 0; i < inputs->item[1]->n; i++) { putchar(' '); print(stdout, inputs->item[1]->item[i]); }
                putchar('\n');
            }
            written++;
        } else if (head(x, "fact") && x->n == 4 && x->item[2]->atom) {
            printf("(at s%s ", x->item[2]->atom);
            print(stdout, x->item[3]);
            printf(")\n");
            facts++;
        } else {
            fputs("; ", stdout);
            print(stdout, x);
            putchar('\n');
        }
    }
    fprintf(stderr, "%s: rules %zu written, %zu refused", argv[1], written, refused);
    for (int r = 0; r < NREASONS; r++) if (count[r]) fprintf(stderr, "; %s %zu", reason[r], count[r]);
    fprintf(stderr, "; facts %zu\n", facts);
    return 0;
}

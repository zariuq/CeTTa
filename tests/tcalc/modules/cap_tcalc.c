/* cap_tcalc.c - replay calls captured from a compiled rule module (RCM_CAPTURE, see
 * native/rule_module/rcm_cetta.c) through the module's reading by demand in tcalc, and compare each
 * call's value with the captured result.
 *   cap_tcalc CETTA PROGRAM.tcalc WORKDIR NAME CAPTURE...
 * Capture lines:  n ID CTOR KID-ID ...   a node of module data
 *                 c JUDGMENT ARG-ID ... = R   a call and its result (a node id, none, or error)
 *                 r RELATION ARITY ID ...     the relation's rows from now on
 * Per call: PROGRAM, the relations' rows and (demand CALL) run under `CETTA --lang tcalc`; every row
 * (value CALL V) of the outcome is collected.  A captured error has no counterpart in the reading and
 * is counted apart.  Verdicts: agree, differ, several values, unfinished.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *grow(void *p, size_t n) {
    p = realloc(p, n);
    if (!p && n) { perror("cap_tcalc"); exit(3); }
    return p;
}

typedef struct { char *s; size_t len, cap; } Text;
static void put(Text *b, const char *s, size_t n) {
    if (b->len + n + 1 > b->cap) { b->cap = 2 * (b->len + n + 1); b->s = grow(b->s, b->cap); }
    memcpy(b->s + b->len, s, n);
    b->len += n;
    b->s[b->len] = 0;
}
static void puts_text(Text *b, const char *s) { put(b, s, strlen(s)); }

typedef struct { char *ctor; int n; int *kid; char *text; } Node;
static Node *node;
static int nnode, capnode;
typedef struct { char *name; int arity, nrows; int *ids; } Rel;
static Rel *rel;
static int nrel, caprel;

static const char *text(int id) {
    if (id <= 0 || id >= nnode || !node[id].ctor) { fprintf(stderr, "cap_tcalc: unknown node %d\n", id); exit(3); }
    Node *x = &node[id];
    if (x->text) return x->text;
    Text b = {0};
    puts_text(&b, "("); puts_text(&b, x->ctor);
    for (int i = 0; i < x->n; i++) { puts_text(&b, " "); puts_text(&b, text(x->kid[i])); }
    puts_text(&b, ")");
    x->text = b.s;
    return x->text;
}

static char *slurp(FILE *f) {
    Text b = {0}; char chunk[1 << 14]; size_t r;
    puts_text(&b, "");
    while ((r = fread(chunk, 1, sizeof chunk, f)) > 0) put(&b, chunk, r);
    return b.s;
}

static int values(const char *out, const char *key, char **first) {
    int count = 0;
    for (const char *s = strstr(out, key); s; s = strstr(s + 1, key)) {
        const char *v = s + strlen(key), *e = v;
        int depth = 0;
        do { if (*e == '(') depth++; else if (*e == ')') depth--; e++; } while (depth > 0 && *e);
        if (count == 0) *first = strndup(v, (size_t)(e - v));
        count++;
    }
    return count;
}

static char *next_token(char **save) { return strtok_r(NULL, " ", save); }

int main(int argc, char **argv) {
    if (argc < 6) { fputs("usage: cap_tcalc CETTA PROGRAM.tcalc WORKDIR NAME CAPTURE...\n", stderr); return 2; }
    const char *cetta = argv[1], *work = argv[3], *name = argv[4];
    FILE *pf = fopen(argv[2], "r");
    if (!pf) { perror(argv[2]); return 2; }
    char *prog = slurp(pf);
    fclose(pf);
    int calls = 0, agree = 0, differ = 0, several = 0, unfinished = 0, errors = 0;
    for (int fi = 5; fi < argc; fi++) {
        FILE *f = fopen(argv[fi], "r");
        if (!f) { perror(argv[fi]); return 2; }
        for (int i = 1; i < nnode; i++) { free(node[i].ctor); free(node[i].kid); free(node[i].text); }
        nnode = 1;   /* node ids start at 1; slot 0 is never used */
        for (int i = 0; i < nrel; i++) { free(rel[i].name); free(rel[i].ids); }
        nrel = 0;
        char *line = NULL; size_t lcap = 0;
        while (getline(&line, &lcap, f) > 0) {
            line[strcspn(line, "\n")] = 0;
            char *save = NULL, *tok = strtok_r(line, " ", &save);
            if (!tok) continue;
            if (!strcmp(tok, "n")) {
                int id = atoi(next_token(&save));
                if (id <= 0) { fputs("cap_tcalc: a node id below 1\n", stderr); return 3; }
                if (id >= capnode) { capnode = 2 * id + 16; node = grow(node, (size_t)capnode * sizeof *node); }
                while (nnode <= id) node[nnode++] = (Node){0};
                node[id].ctor = strdup(next_token(&save));
                for (char *k; (k = next_token(&save));) {
                    node[id].kid = grow(node[id].kid, (size_t)(node[id].n + 1) * sizeof(int));
                    node[id].kid[node[id].n++] = atoi(k);
                }
            } else if (!strcmp(tok, "r")) {
                char *rname = next_token(&save);
                int arity = atoi(next_token(&save)), r;
                for (r = 0; r < nrel && strcmp(rel[r].name, rname); r++) {}
                if (r == nrel) {
                    if (nrel == caprel) { caprel = 2 * caprel + 4; rel = grow(rel, (size_t)caprel * sizeof *rel); }
                    rel[nrel++] = (Rel){strdup(rname), arity, 0, NULL};
                }
                int count = 0;
                for (char *k; (k = next_token(&save));) {
                    rel[r].ids = grow(rel[r].ids, (size_t)(count + 1) * sizeof(int));
                    rel[r].ids[count++] = atoi(k);
                }
                if (arity <= 0 || count % arity) { fputs("cap_tcalc: a relation line without whole rows\n", stderr); return 3; }
                rel[r].nrows = count / arity;
            } else if (!strcmp(tok, "c")) {
                calls++;
                Text call = {0};
                puts_text(&call, "("); puts_text(&call, next_token(&save));
                char *k, *result = NULL;
                while ((k = next_token(&save))) {
                    if (!strcmp(k, "=")) { result = next_token(&save); break; }
                    puts_text(&call, " "); puts_text(&call, text(atoi(k)));
                }
                puts_text(&call, ")");
                if (!result || !strcmp(result, "error")) { errors++; free(call.s); continue; }
                const char *expect = strcmp(result, "none") ? text(atoi(result)) : NULL;
                Text path = {0}, cmd = {0}, key = {0};
                puts_text(&path, work); puts_text(&path, "/call.tcalc");
                FILE *o = fopen(path.s, "w");
                if (!o) { perror(path.s); return 2; }
                fprintf(o, "%s(budget 1000000000)\n", prog);
                for (int r = 0; r < nrel; r++)
                    for (int w = 0; w < rel[r].nrows; w++) {
                        fprintf(o, "(%s", rel[r].name);
                        for (int a = 0; a < rel[r].arity; a++) fprintf(o, " %s", text(rel[r].ids[w * rel[r].arity + a]));
                        fputs(")\n", o);
                    }
                fprintf(o, "(demand %s)\n", call.s);
                fclose(o);
                puts_text(&cmd, cetta); puts_text(&cmd, " --lang tcalc --profile observe=outcomes ");
                puts_text(&cmd, path.s); puts_text(&cmd, " 2>&1");
                FILE *q = popen(cmd.s, "r");
                if (!q) { perror("popen"); return 2; }
                char *out = slurp(q);
                pclose(q);
                puts_text(&key, "(value "); puts_text(&key, call.s); puts_text(&key, " ");
                char *got = NULL;
                int nv = values(out, key.s, &got);
                if (!strstr(out, "(finished true)")) { unfinished++; printf("UNFINISHED %.200s\n", call.s); }
                else if (nv > 1) { several++; printf("SEVERAL (%d) %.200s\n", nv, call.s); }
                else if ((nv == 0 && !expect) || (nv == 1 && expect && !strcmp(got, expect))) agree++;
                else {
                    differ++;
                    printf("DIFFER %.300s\n  compiled: %.300s\n  tcalc: %.300s\n", call.s, expect ? expect : "(no result)",
                           nv ? got : "(no value)");
                }
                free(out); free(got); free(key.s); free(call.s); free(path.s); free(cmd.s);
            }
        }
        free(line);
        fclose(f);
    }
    printf("%s: calls %d: agree %d, differ %d, several values %d, unfinished %d; captured errors %d\n",
           name, calls, agree, differ, several, unfinished, errors);
    return differ || several || unfinished;
}

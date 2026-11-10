/* Test driver for the SAT personality.
 *   sat_main_v1 FILE.cnf               s SATISFIABLE and a v line, or s UNSATISFIABLE
 *   sat_main_v1 FILE.cnf PROOF.lrat    the same; a refutation's LRAT proof to PROOF
 *   sat_main_v1 --check FILE.cnf ...   each verdict; then every refutation is
 *                                      re-checked: accepted for its formula,
 *                                      refused when truncated (unless the formula
 *                                      has an empty clause), refused for every
 *                                      satisfiable formula of the list
 * The answer is checked before it is printed: a model against every clause, a
 * refutation by the certificate checker.  Exit 10 satisfiable, 20 unsatisfiable,
 * 1 otherwise (--check: 0 when every check holds). */
#include "asp_cdnl_v1.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int32_t *lits;
    uint32_t len, cap;
} Lits;

static int push(Lits *l, int32_t x) {
    if (l->len == l->cap) {
        uint32_t cap = l->cap ? 2u * l->cap : 1024u;
        int32_t *v = realloc(l->lits, sizeof(*v) * cap);
        if (!v)
            return 0;
        l->lits = v;
        l->cap = cap;
    }
    l->lits[l->len++] = x;
    return 1;
}

typedef struct {
    uint32_t nvars, nclauses;
    AspClause *clauses;
    uint32_t *starts;
    Lits all;
} Cnf;

static void cnf_free(Cnf *f) {
    free(f->clauses);
    free(f->starts);
    free(f->all.lits);
}

/* DIMACS: comment lines, a "p cnf V C" line, clauses ended by 0 */
static int read_cnf(const char *path, Cnf *f) {
    memset(f, 0, sizeof(*f));
    FILE *in = fopen(path, "r");
    if (!in)
        return 0;
    uint32_t capc = 0u, start = 0u;
    int c;
    long v;
    while ((c = fgetc(in)) != EOF) {
        if (c == 'c' || c == 'p') {
            char line[4096];
            if (c == 'p' && fscanf(in, " cnf %u", &f->nvars) != 1)
                break;
            if (!fgets(line, sizeof line, in))
                break;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            continue;
        ungetc(c, in);
        if (fscanf(in, "%ld", &v) != 1)
            break;
        if (v == 0) {
            if (f->nclauses == capc) {
                capc = capc ? 2u * capc : 1024u;
                uint32_t *grown = realloc(f->starts, sizeof(*grown) * capc);
                if (!grown)
                    break;
                f->starts = grown;
            }
            f->starts[f->nclauses++] = start;
            start = f->all.len;
        } else if (!push(&f->all, (int32_t)v)) {
            break;
        }
    }
    fclose(in);
    f->clauses = malloc(sizeof(*f->clauses) * (f->nclauses ? f->nclauses : 1u));
    if (!f->clauses)
        return 0;
    for (uint32_t i = 0u; i < f->nclauses; i++) {
        uint32_t end = i + 1u < f->nclauses ? f->starts[i + 1u] : f->all.len;
        f->clauses[i] = (AspClause){f->all.lits + f->starts[i], end - f->starts[i]};
    }
    return 1;
}

static int check_all(int n, char **paths) {
    Cnf *f = calloc((size_t)n, sizeof(*f));
    AspAnswer *a = calloc((size_t)n, sizeof(*a));
    if (!f || !a)
        return 1;
    int failures = 0;
    uint32_t sat = 0u, unsat = 0u, accepted = 0u, refused = 0u;
    for (int i = 0; i < n; i++) {
        const char *name = strrchr(paths[i], '/') ? strrchr(paths[i], '/') + 1 : paths[i];
        if (!read_cnf(paths[i], &f[i])) {
            printf("%s: unreadable\n", name);
            failures++;
            continue;
        }
        a[i] = asp_sat_solve(f[i].nvars, f[i].clauses, f[i].nclauses);
        printf("%s %s\n", name, a[i].kind == ASP_ANSWER_SAT ? "SATISFIABLE"
               : a[i].kind == ASP_ANSWER_UNSAT ? "UNSATISFIABLE" : "UNKNOWN");
        if (a[i].kind == ASP_ANSWER_SAT)
            sat++;
        else if (a[i].kind == ASP_ANSWER_UNSAT)
            unsat++;
        else
            failures++;
    }
    for (int i = 0; i < n; i++) {
        if (a[i].kind != ASP_ANSWER_UNSAT)
            continue;
        if (asp_sat_certificate_check(f[i].nvars, f[i].clauses, f[i].nclauses,
                                      a[i].steps, a[i].steps_len))
            accepted++;
        else
            printf("FAIL: %s: its certificate is refused\n", paths[i]), failures++;
        /* an empty input clause refutes its formula with no step at all */
        bool empty_input = false;
        for (uint32_t k = 0u; k < f[i].nclauses && !empty_input; k++)
            empty_input = f[i].clauses[k].len == 0u;
        if (empty_input)
            ;
        else if (asp_sat_certificate_check(f[i].nvars, f[i].clauses, f[i].nclauses,
                                           a[i].steps, a[i].steps_len - 1u))
            printf("FAIL: %s: a truncated certificate is accepted\n", paths[i]), failures++;
        else
            refused++;
        for (int j = 0; j < n; j++) {
            if (a[j].kind != ASP_ANSWER_SAT)
                continue;
            if (asp_sat_certificate_check(f[j].nvars, f[j].clauses, f[j].nclauses,
                                          a[i].steps, a[i].steps_len))
                printf("FAIL: %s's certificate is accepted for %s, which is satisfiable\n",
                       paths[i], paths[j]), failures++;
            else
                refused++;
        }
    }
    printf("%u satisfiable, %u unsatisfiable; certificates accepted %u, mutants refused %u\n",
           sat, unsat, accepted, refused);
    for (int i = 0; i < n; i++) {
        asp_answer_free(&a[i]);
        cnf_free(&f[i]);
    }
    free(a);
    free(f);
    return failures ? 1 : 0;
}

int main(int argc, char **argv) {
    if (argc > 2 && strcmp(argv[1], "--check") == 0)
        return check_all(argc - 2, argv + 2);
    if (argc < 2)
        return 1;
    Cnf f;
    if (!read_cnf(argv[1], &f))
        return 1;
    uint32_t nvars = f.nvars, nclauses = f.nclauses;
    AspClause *clauses = f.clauses;
    AspAnswer a = asp_sat_solve(nvars, clauses, nclauses);
    int rc = 1;
    if (a.kind == ASP_ANSWER_SAT) {
        uint8_t *val = calloc(nvars + 1u, 1u);
        for (uint32_t k = 0u; val && k < a.model_len; k++)
            val[a.model[k]] = 1u;
        puts("s SATISFIABLE");
        fputs("v", stdout);
        for (uint32_t x = 1u; val && x <= nvars; x++)
            printf(" %d", val[x] ? (int)x : -(int)x);
        puts(" 0");
        free(val);
        rc = 10;
    } else if (a.kind == ASP_ANSWER_UNSAT) {
        puts("s UNSATISFIABLE");
        if (argc > 2) {
            FILE *out = fopen(argv[2], "w");
            if (!out)
                return 1;
            asp_lrat_print(out, a.steps, a.steps_len);
            fclose(out);
        }
        rc = 20;
    } else {
        printf("s UNKNOWN (answer kind %d)\n", (int)a.kind);
    }
    asp_answer_free(&a);
    cnf_free(&f);
    return rc;
}

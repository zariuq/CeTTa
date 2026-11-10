/* Hall steps in certificates.  For each program file (one ground program,
 * rules "H : p1 p2 : n1 n2"), the fast search with Hall's condition must
 * refute it with a certificate that holds a Hall step and that the checker
 * accepts.  Then, for every Hall step, each mutant that is invalid by
 * construction must make the certificate refused:
 *   - dropping one group, when the step has exactly one group more than
 *     cliques (the counts become equal);
 *   - dropping one clique atom that is not in the step's clause (the group
 *     atom it covered becomes uncovered).
 * Usage: asp_hall_certificates_v1 FILE ... */
#include "asp_cdnl_v1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int32_t *v;
    uint32_t len, cap;
} Ints;

static bool ints_push(Ints *x, int32_t v) {
    if (x->len == x->cap) {
        uint32_t cap = x->cap ? 2u * x->cap : 16u;
        int32_t *p = realloc(x->v, sizeof(*p) * cap);
        if (!p)
            return false;
        x->v = p;
        x->cap = cap;
    }
    x->v[x->len++] = v;
    return true;
}

static bool parse_ints(char *s, Ints *out) {
    for (char *t = strtok(s, " \t\n"); t; t = strtok(NULL, " \t\n"))
        if (!ints_push(out, (int32_t)atoi(t)))
            return false;
    return true;
}

/* Rules of one program file; the literal arrays are owned by the caller. */
static AspRule *read_program(const char *path, uint32_t *nrules) {
    FILE *f = fopen(path, "r");
    if (!f)
        return NULL;
    AspRule *rules = NULL;
    uint32_t n = 0u, cap = 0u;
    char line[1 << 16];
    while (fgets(line, sizeof line, f)) {
        char *c1 = strchr(line, ':');
        char *c2 = c1 ? strchr(c1 + 1, ':') : NULL;
        if (!c2)
            continue;
        *c1 = '\0';
        *c2 = '\0';
        Ints head = {0}, pos = {0}, neg = {0};
        if (!parse_ints(line, &head) || head.len != 1u ||
            !parse_ints(c1 + 1, &pos) || !parse_ints(c2 + 1, &neg))
            return NULL;
        if (n == cap) {
            cap = cap ? 2u * cap : 64u;
            AspRule *p = realloc(rules, sizeof(*p) * cap);
            if (!p)
                return NULL;
            rules = p;
        }
        rules[n++] = (AspRule){head.v[0], pos.v, pos.len, neg.v, neg.len};
        free(head.v);
    }
    fclose(f);
    *nrules = n;
    return rules;
}

static bool lits_has(const int32_t *xs, uint32_t n, int32_t x) {
    for (uint32_t i = 0u; i < n; i++)
        if (xs[i] == x)
            return true;
    return false;
}

static int check_file(const char *path) {
    uint32_t nr = 0u;
    AspRule *rules = read_program(path, &nr);
    if (!rules) {
        printf("FAIL %s: unreadable\n", path);
        return 1;
    }
    AspAnswer a = asp_cdnl_solve_search(rules, nr, ASP_SEARCH_FAST_HALL, NULL, NULL);
    int failures = 0;
    uint32_t halls = 0u, refused = 0u;
    if (a.kind != ASP_ANSWER_UNSAT) {
        printf("FAIL %s: not refuted\n", path);
        failures++;
    } else if (!asp_certificate_check(rules, nr, a.steps, a.steps_len)) {
        printf("FAIL %s: certificate refused\n", path);
        failures++;
    }
    for (uint32_t s = 0u; a.kind == ASP_ANSWER_UNSAT && s < a.steps_len; s++) {
        AspStep *st = &a.steps[s];
        if (st->kind != ASP_STEP_HALL)
            continue;
        halls++;
        uint32_t ncliques = 0u;
        for (uint32_t k = 0u; k < st->cliques_len; k++)
            ncliques += st->cliques[k] == UINT32_MAX;
        AspStep save = *st;
        uint32_t *groups = malloc(sizeof(*groups) * (st->groups_len + 1u));
        uint32_t *cliques = malloc(sizeof(*cliques) * (st->cliques_len + 1u));
        if (!groups || !cliques) {
            free(groups);
            free(cliques);
            failures++;
            break;
        }
        if (st->groups_len == ncliques + 1u)
            for (uint32_t k = 0u; k < save.groups_len; k++) {
                memcpy(groups, save.groups, sizeof(*groups) * save.groups_len);
                memmove(groups + k, groups + k + 1u,
                        sizeof(*groups) * (save.groups_len - k - 1u));
                st->groups = groups;
                st->groups_len = save.groups_len - 1u;
                if (asp_certificate_check(rules, nr, a.steps, a.steps_len)) {
                    printf("FAIL %s: step %u accepted without group %u\n", path, st->id, k);
                    failures++;
                } else {
                    refused++;
                }
                *st = save;
            }
        for (uint32_t k = 0u; k < save.cliques_len; k++) {
            uint32_t atom = save.cliques[k];
            if (atom == UINT32_MAX ||
                lits_has(save.clause, save.clause_len, (int32_t)atom + 1))
                continue;
            memcpy(cliques, save.cliques, sizeof(*cliques) * save.cliques_len);
            memmove(cliques + k, cliques + k + 1u,
                    sizeof(*cliques) * (save.cliques_len - k - 1u));
            st->cliques = cliques;
            st->cliques_len = save.cliques_len - 1u;
            if (asp_certificate_check(rules, nr, a.steps, a.steps_len)) {
                printf("FAIL %s: step %u accepted without clique atom %u\n", path, st->id, atom);
                failures++;
            } else {
                refused++;
            }
            *st = save;
        }
        free(groups);
        free(cliques);
    }
    if (a.kind == ASP_ANSWER_UNSAT && halls == 0u) {
        printf("FAIL %s: no Hall step\n", path);
        failures++;
    }
    printf("%s: refuted, %u Hall steps, certificate accepted, %u invalid mutants refused\n",
           path, halls, refused);
    asp_answer_free(&a);
    for (uint32_t r = 0u; r < nr; r++) {
        free((void *)rules[r].pos);
        free((void *)rules[r].neg);
    }
    free(rules);
    return failures;
}

int main(int argc, char **argv) {
    int failures = 0;
    for (int i = 1; i < argc; i++)
        failures += check_file(argv[i]);
    return failures ? 1 : 0;
}

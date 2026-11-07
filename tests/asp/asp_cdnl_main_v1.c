/* Test driver for the answer-set solver.  Programs are separated by "---";
 * a rule is "H : p1 p2 : n1 n2" over integer atoms.
 *   asp_cdnl_main_v1 FILE          print each answer, then "==="
 *   asp_cdnl_main_v1 --check FILE  re-check every certificate: accepted for its
 *                                  program, refused when truncated, and refused
 *                                  for every program that has a stable model
 *   asp_cdnl_main_v1 --enumerate FILE  every stable model, then the certificate
 *                                  that no further one exists, then "###"
 *   asp_cdnl_main_v1 --differential FILE  the fast search against the
 *                                  reference one: the same verdicts and sets of
 *                                  models, each fast answer a model the
 *                                  reference finds or a refutation the
 *                                  checker accepts
 * The reference search runs by default; --fast first runs the fast one. */
#include "asp_cdnl_v1.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MAX_PROGRAMS = 256 };

typedef struct {
    AspRule *rules;
    uint32_t nrules;
    uint32_t caprules;
} Program;

/* the integers of a space-separated field, in a fresh array */
static int32_t *parse_ints(char *s, uint32_t *n) {
    uint32_t cap = 8u;
    int32_t *out = malloc(sizeof(*out) * cap);
    *n = 0u;
    for (char *tok = strtok(s, " \t\n"); out && tok; tok = strtok(NULL, " \t\n")) {
        if (*n == cap) {
            cap *= 2u;
            int32_t *grown = realloc(out, sizeof(*out) * cap);
            if (!grown) { free(out); return NULL; }
            out = grown;
        }
        out[(*n)++] = atoi(tok);
    }
    return out;
}

static uint32_t read_programs(FILE *in, Program *programs) {
    char line[1 << 16];
    uint32_t count = 0u;
    Program *p = &programs[0];
    bool started = false;
    while (fgets(line, sizeof line, in)) {
        if (strncmp(line, "---", 3) == 0) {
            count++;
            p = &programs[count];
            started = false;
            continue;
        }
        char *c1 = strchr(line, ':');
        char *c2 = c1 ? strchr(c1 + 1, ':') : NULL;
        if (!c2)
            continue;
        *c1 = 0;
        *c2 = 0;
        if (p->nrules == p->caprules) {
            p->caprules = p->caprules ? 2u * p->caprules : 64u;
            p->rules = realloc(p->rules, sizeof(*p->rules) * p->caprules);
            if (!p->rules)
                exit(2);
        }
        AspRule *r = &p->rules[p->nrules++];
        r->head = atoi(line);
        r->pos = parse_ints(c1 + 1, &r->pos_len);
        r->neg = parse_ints(c2 + 1, &r->neg_len);
        if (!r->pos || !r->neg)
            exit(2);
        started = true;
    }
    return count + (started || count ? 1u : 0u);
}

/* whether a model with these atoms is among the enumeration's, as sets */
static bool has_model(const AspEnumeration *e, const int32_t *atoms, uint32_t len) {
    for (uint32_t k = 0u; k < e->models_len; k++) {
        const AspModel *m = &e->models[k];
        if (m->len != len)
            continue;
        bool all = true;
        for (uint32_t i = 0u; i < len && all; i++) {
            bool found = false;
            for (uint32_t j = 0u; j < m->len && !found; j++)
                found = m->atoms[j] == atoms[i];
            all = found;
        }
        if (all)
            return true;
    }
    return false;
}

static int differential(Program *programs, uint32_t n) {
    int failures = 0;
    uint32_t sat = 0u, unsat = 0u, models = 0u, refused = 0u;
    for (uint32_t i = 0u; i < n; i++) {
        const AspRule *rules = programs[i].rules;
        uint32_t nr = programs[i].nrules;
        AspEnumeration ref = asp_cdnl_enumerate_search(rules, nr, ASP_SEARCH_REFERENCE);
        AspEnumeration fast = asp_cdnl_enumerate_search(rules, nr, ASP_SEARCH_FAST);
        AspAnswer a = asp_cdnl_solve_search(rules, nr, ASP_SEARCH_FAST, NULL, NULL);
        bool same = ref.kind == fast.kind && ref.models_len == fast.models_len;
        for (uint32_t k = 0u; same && k < fast.models_len; k++)
            same = has_model(&ref, fast.models[k].atoms, fast.models[k].len);
        if (!same)
            printf("FAIL: program %u: the fast enumeration differs\n", i), failures++;
        models += ref.models_len;
        /* The model check: it accepts every stable model, and refuses one less
         * an atom or with another atom, since stable models never contain one
         * another. */
        for (uint32_t k = 0u; k < ref.models_len; k++) {
            const AspModel *m = &ref.models[k];
            if (!asp_model_check(rules, nr, m->atoms, m->len))
                printf("FAIL: program %u: the model check refuses a stable model\n", i), failures++;
            if (m->len > 0u && asp_model_check(rules, nr, m->atoms, m->len - 1u))
                printf("FAIL: program %u: the model check accepts a model less an atom\n", i), failures++;
            else if (m->len > 0u)
                refused++;
            int32_t *more = malloc(sizeof(*more) * (m->len + 1u));
            bool extra = false;
            for (uint32_t r = 0u; more && r < nr && !extra; r++) {
                bool in = false;
                for (uint32_t j = 0u; j < m->len && !in; j++)
                    in = m->atoms[j] == rules[r].head;
                if (!in) {
                    memcpy(more, m->atoms, sizeof(*more) * m->len);
                    more[m->len] = rules[r].head;
                    extra = true;
                }
            }
            if (extra && asp_model_check(rules, nr, more, m->len + 1u))
                printf("FAIL: program %u: the model check accepts a model with another atom\n", i), failures++;
            else if (extra)
                refused++;
            free(more);
        }
        if (a.kind == ASP_ANSWER_SAT) {
            sat++;
            if (!has_model(&ref, a.model, a.model_len))
                printf("FAIL: program %u: the fast answer is no stable model\n", i), failures++;
        } else if (a.kind == ASP_ANSWER_UNSAT) {
            unsat++;
            if (ref.models_len != 0u)
                printf("FAIL: program %u: the fast search finds no model\n", i), failures++;
            else if (!asp_certificate_check(rules, nr, a.steps, a.steps_len))
                printf("FAIL: program %u: the fast certificate is refused\n", i), failures++;
        } else {
            printf("FAIL: program %u: the fast search is stuck\n", i), failures++;
        }
        asp_answer_free(&a);
        asp_enumeration_free(&ref);
        asp_enumeration_free(&fast);
    }
    printf("fast search: %u programs, %u with a model, %u without; %u models, all as the reference's; certificates accepted; the model check accepts every model and refuses %u mutants\n",
           n, sat, unsat, models, refused);
    return failures ? 1 : 0;
}

int main(int argc, char **argv) {
    AspSearch search = ASP_SEARCH_REFERENCE;
    int arg = 1;
    if (arg < argc && strcmp(argv[arg], "--fast") == 0) {
        search = ASP_SEARCH_FAST;
        arg++;
    }
    bool check = arg + 1 < argc && strcmp(argv[arg], "--check") == 0;
    bool enumerate = arg + 1 < argc && strcmp(argv[arg], "--enumerate") == 0;
    bool compare = arg + 1 < argc && strcmp(argv[arg], "--differential") == 0;
    if (arg >= argc)
        return 2;
    FILE *in = fopen(argv[check || enumerate || compare ? arg + 1 : arg], "r");
    if (!in)
        return 2;
    static Program programs[MAX_PROGRAMS];
    uint32_t n = read_programs(in, programs);
    fclose(in);
    if (compare)
        return differential(programs, n);
    if (enumerate) {
        int bad = 0;
        for (uint32_t i = 0u; i < n; i++) {
            AspEnumeration e = asp_cdnl_enumerate_search(programs[i].rules, programs[i].nrules, search);
            for (uint32_t k = 0u; k < e.models_len; k++) {
                fputs("model", stdout);
                for (uint32_t j = 0u; j < e.models[k].len; j++)
                    printf(" %d", e.models[k].atoms[j]);
                fputc('\n', stdout);
            }
            if (e.kind == ASP_ANSWER_UNSAT)
                asp_answer_print(stdout, &e.final);
            else
                puts("stuck"), bad = 1;
            puts("###");
            asp_enumeration_free(&e);
        }
        return bad;
    }
    static AspAnswer answers[MAX_PROGRAMS];
    for (uint32_t i = 0u; i < n; i++) {
        answers[i] = asp_cdnl_solve_search(programs[i].rules, programs[i].nrules, search, NULL, NULL);
        if (!check) {
            asp_answer_print(stdout, &answers[i]);
            puts("===");
        }
    }
    int failures = 0;
    if (check) {
        uint32_t accepted = 0u, refused = 0u;
        for (uint32_t i = 0u; i < n; i++) {
            const AspAnswer *a = &answers[i];
            if (a->kind != ASP_ANSWER_UNSAT)
                continue;
            if (asp_certificate_check(programs[i].rules, programs[i].nrules, a->steps, a->steps_len))
                accepted++;
            else
                printf("FAIL: program %u: its certificate is refused\n", i), failures++;
            if (asp_certificate_check(programs[i].rules, programs[i].nrules, a->steps, a->steps_len - 1u))
                printf("FAIL: program %u: a truncated certificate is accepted\n", i), failures++;
            else
                refused++;
            for (uint32_t j = 0u; j < n; j++) {
                if (answers[j].kind != ASP_ANSWER_SAT)
                    continue;
                if (asp_certificate_check(programs[j].rules, programs[j].nrules, a->steps, a->steps_len))
                    printf("FAIL: program %u's certificate is accepted for program %u, which has a stable model\n", i, j), failures++;
                else
                    refused++;
            }
        }
        printf("certificates accepted %u, mutants refused %u\n", accepted, refused);
    }
    for (uint32_t i = 0u; i < n; i++)
        asp_answer_free(&answers[i]);
    return failures ? 1 : 0;
}

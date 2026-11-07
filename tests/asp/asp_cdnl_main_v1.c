/* Test driver for the answer-set solver.  Programs are separated by "---";
 * a rule is "H : p1 p2 : n1 n2" over integer atoms.
 *   asp_cdnl_main_v1 FILE          print each answer, then "==="
 *   asp_cdnl_main_v1 --check FILE  re-check every certificate: accepted for its
 *                                  program, refused when truncated, and refused
 *                                  for every program that has a stable model
 *   asp_cdnl_main_v1 --enumerate FILE  every stable model, then the certificate
 *                                  that no further one exists, then "###" */
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

int main(int argc, char **argv) {
    bool check = argc > 2 && strcmp(argv[1], "--check") == 0;
    bool enumerate = argc > 2 && strcmp(argv[1], "--enumerate") == 0;
    FILE *in = fopen(argv[check || enumerate ? 2 : 1], "r");
    if (!in)
        return 2;
    static Program programs[MAX_PROGRAMS];
    uint32_t n = read_programs(in, programs);
    fclose(in);
    if (enumerate) {
        int bad = 0;
        for (uint32_t i = 0u; i < n; i++) {
            AspEnumeration e = asp_cdnl_enumerate(programs[i].rules, programs[i].nrules);
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
        answers[i] = asp_cdnl_solve(programs[i].rules, programs[i].nrules);
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

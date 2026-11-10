/* Test driver for the answer-set solver.  Programs are separated by "---";
 * a rule is "H : p1 p2 : n1 n2" over integer atoms.
 *   asp_cdnl_main_v1 FILE          print each answer, then "==="
 *   asp_cdnl_main_v1 --check FILE  re-check every certificate: accepted for its
 *                                  program, refused when truncated (unless the
 *                                  program's inputs hold the empty clause, a
 *                                  rule p :- not p, when the empty certificate
 *                                  refutes it), and refused for every program
 *                                  that has a stable model
 *   asp_cdnl_main_v1 --enumerate FILE  every stable model, then the certificate
 *                                  that no further one exists, then "###"
 *   asp_cdnl_main_v1 --differential FILE  the fast search against the
 *                                  reference one: the same verdicts and sets of
 *                                  models, each fast answer a model the
 *                                  reference finds or a refutation the
 *                                  checker accepts
 *   asp_cdnl_main_v1 --multishot FILE  every stable model by one session, each
 *                                  model blocked by an added constraint (its
 *                                  atoms true, the program's others false)
 *                                  before the search goes on: the models are
 *                                  the enumeration's, and the final refutation
 *                                  is accepted over the added constraints and
 *                                  refused without the last one, with an input
 *                                  clause altered, and by the checker without
 *                                  added constraints
 * The reference search runs by default; --fast first runs the fast one, and
 * --hall the fast one with Hall's condition over the program's constraints. */
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

static int differential(Program *programs, uint32_t n, AspSearch fastkind) {
    int failures = 0;
    uint32_t sat = 0u, unsat = 0u, models = 0u, refused = 0u;
    for (uint32_t i = 0u; i < n; i++) {
        const AspRule *rules = programs[i].rules;
        uint32_t nr = programs[i].nrules;
        AspEnumeration ref = asp_cdnl_enumerate_search(rules, nr, ASP_SEARCH_REFERENCE);
        AspEnumeration fast = asp_cdnl_enumerate_search(rules, nr, fastkind);
        AspAnswer a = asp_cdnl_solve_search(rules, nr, fastkind, NULL, NULL);
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

static bool in_list(const int32_t *xs, uint32_t n, int32_t x) {
    for (uint32_t i = 0u; i < n; i++)
        if (xs[i] == x)
            return true;
    return false;
}

static int multishot(Program *programs, uint32_t n, AspSearch search) {
    int failures = 0;
    uint32_t models = 0u, refutations = 0u, refused = 0u;
    for (uint32_t i = 0u; i < n; i++) {
        const AspRule *rules = programs[i].rules;
        uint32_t nr = programs[i].nrules;
        /* the program's atoms */
        uint32_t cap = 16u, natoms = 0u;
        int32_t *atoms = malloc(sizeof(*atoms) * cap);
        for (uint32_t r = 0u; atoms && r < nr; r++) {
            uint32_t len = 1u + rules[r].pos_len + rules[r].neg_len;
            for (uint32_t k = 0u; k < len; k++) {
                int32_t a = k == 0u ? rules[r].head
                          : k <= rules[r].pos_len ? rules[r].pos[k - 1u]
                          : rules[r].neg[k - 1u - rules[r].pos_len];
                if (a < 0 || in_list(atoms, natoms, a))
                    continue;
                if (natoms == cap) {
                    cap *= 2u;
                    int32_t *grown = realloc(atoms, sizeof(*atoms) * cap);
                    if (!grown) { free(atoms); atoms = NULL; break; }
                    atoms = grown;
                }
                atoms[natoms++] = a;
            }
        }
        AspEnumeration ref = asp_cdnl_enumerate_search(rules, nr, ASP_SEARCH_REFERENCE);
        AspSession *session = atoms ? asp_session_new(rules, nr, search, NULL, NULL) : NULL;
        AspRule *extra = NULL;
        uint32_t nextra = 0u;
        bool ok = session != NULL;
        while (ok) {
            while (asp_session_step(session) == ASP_SESSION_RUNNING) {
            }
            int32_t *model = NULL;
            uint32_t len = 0u;
            if (!asp_session_model(session, &model, &len))
                break;
            models++;
            if (!has_model(&ref, model, len))
                printf("FAIL: program %u: a session model the enumeration does not have\n", i), failures++;
            int32_t *neg = malloc(sizeof(*neg) * (natoms ? natoms : 1u));
            uint32_t nn = 0u;
            for (uint32_t k = 0u; neg && k < natoms; k++)
                if (!in_list(model, len, atoms[k]))
                    neg[nn++] = atoms[k];
            AspRule *grown = neg ? realloc(extra, sizeof(*extra) * (nextra + 1u)) : NULL;
            ok = grown && asp_session_constrain(session, model, len, neg, nn);
            if (grown) {
                extra = grown;
                extra[nextra++] = (AspRule){.head = -1, .pos = model, .pos_len = len,
                                            .neg = neg, .neg_len = nn};
            } else {
                free(model);
                free(neg);
            }
        }
        AspAnswer a = ok ? asp_session_answer(session) : (AspAnswer){.kind = ASP_ANSWER_ERROR};
        if (!ok || a.kind != ASP_ANSWER_UNSAT || nextra != ref.models_len) {
            printf("FAIL: program %u: %u session models, %u in the enumeration, kind %d\n",
                   i, nextra, ref.models_len, (int)a.kind);
            failures++;
        } else {
            refutations++;
            if (!asp_certificate_check_constrained(rules, nr, NULL, 0u, extra, nextra,
                                                   a.steps, a.steps_len))
                printf("FAIL: program %u: its refutation is refused over its constraints\n", i), failures++;
            if (nextra) {
                /* without the last constraint */
                if (asp_certificate_check_constrained(rules, nr, NULL, 0u, extra, nextra - 1u,
                                                      a.steps, a.steps_len))
                    printf("FAIL: program %u: accepted without its last constraint\n", i), failures++;
                else
                    refused++;
                if (asp_certificate_check(rules, nr, a.steps, a.steps_len))
                    printf("FAIL: program %u: accepted without its constraints\n", i), failures++;
                else
                    refused++;
                /* an input clause with its first literal complemented: the
                 * clause of no blocking constraint (no stable model is a
                 * subset of another) */
                AspStep *steps = malloc(sizeof(*steps) * a.steps_len);
                int32_t *clause = NULL;
                uint32_t k = 0u;
                while (k < a.steps_len && !(a.steps[k].kind == ASP_STEP_INPUT && a.steps[k].clause_len))
                    k++;
                if (steps && k < a.steps_len &&
                    (clause = malloc(sizeof(*clause) * a.steps[k].clause_len))) {
                    memcpy(steps, a.steps, sizeof(*steps) * a.steps_len);
                    memcpy(clause, a.steps[k].clause, sizeof(*clause) * a.steps[k].clause_len);
                    clause[0] = -clause[0];
                    steps[k].clause = clause;
                    if (asp_certificate_check_constrained(rules, nr, NULL, 0u, extra, nextra,
                                                          steps, a.steps_len))
                        printf("FAIL: program %u: accepted with an altered input clause\n", i), failures++;
                    else
                        refused++;
                }
                free(clause);
                free(steps);
            }
        }
        asp_answer_free(&a);
        asp_session_free(session);
        for (uint32_t k = 0u; k < nextra; k++) {
            free((int32_t *)extra[k].pos);
            free((int32_t *)extra[k].neg);
        }
        free(extra);
        free(atoms);
        asp_enumeration_free(&ref);
    }
    printf("multi-shot: %u programs, %u models, all the enumeration's; %u refutations accepted over their constraints, mutants refused %u\n",
           n, models, refutations, refused);
    return failures ? 1 : 0;
}

int main(int argc, char **argv) {
    AspSearch search = ASP_SEARCH_REFERENCE;
    int arg = 1;
    if (arg < argc && strcmp(argv[arg], "--fast") == 0) {
        search = ASP_SEARCH_FAST;
        arg++;
    } else if (arg < argc && strcmp(argv[arg], "--hall") == 0) {
        search = ASP_SEARCH_FAST_HALL;
        arg++;
    }
    bool check = arg + 1 < argc && strcmp(argv[arg], "--check") == 0;
    bool enumerate = arg + 1 < argc && strcmp(argv[arg], "--enumerate") == 0;
    bool compare = arg + 1 < argc && strcmp(argv[arg], "--differential") == 0;
    bool shots = arg + 1 < argc && strcmp(argv[arg], "--multishot") == 0;
    if (arg >= argc)
        return 2;
    FILE *in = fopen(argv[check || enumerate || compare || shots ? arg + 1 : arg], "r");
    if (!in)
        return 2;
    static Program programs[MAX_PROGRAMS];
    uint32_t n = read_programs(in, programs);
    fclose(in);
    if (shots)
        return multishot(programs, n, search);
    if (compare)
        return differential(programs, n, search == ASP_SEARCH_FAST_HALL ? ASP_SEARCH_FAST_HALL : ASP_SEARCH_FAST);
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
        uint32_t accepted = 0u, refused = 0u, by_inputs = 0u;
        for (uint32_t i = 0u; i < n; i++) {
            const AspAnswer *a = &answers[i];
            if (a->kind != ASP_ANSWER_UNSAT)
                continue;
            if (asp_certificate_check(programs[i].rules, programs[i].nrules, a->steps, a->steps_len))
                accepted++;
            else
                printf("FAIL: program %u: its certificate is refused\n", i), failures++;
            /* a program whose inputs hold the empty clause is refuted by them */
            bool inputs_refute = asp_certificate_check(programs[i].rules, programs[i].nrules,
                                                       a->steps, 0u);
            by_inputs += inputs_refute;
            if (inputs_refute && a->steps_len > 1u)
                printf("FAIL: program %u: its inputs refute it, yet its certificate has %u steps\n",
                       i, a->steps_len), failures++;
            else if (!inputs_refute &&
                     asp_certificate_check(programs[i].rules, programs[i].nrules, a->steps, a->steps_len - 1u))
                printf("FAIL: program %u: a truncated certificate is accepted\n", i), failures++;
            else if (!inputs_refute)
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
        printf("certificates accepted %u (%u refuted by their inputs), mutants refused %u\n",
               accepted, by_inputs, refused);
    }
    for (uint32_t i = 0u; i < n; i++)
        asp_answer_free(&answers[i]);
    return failures ? 1 : 0;
}

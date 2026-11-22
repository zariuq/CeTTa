/* gslt_tcalc_answers_v1: a tcalc program that tcalc-direct wrote, run with
 * recorded calls, against the answers recorded for them.
 *
 *   gslt_tcalc_answers_v1 PROGRAM.tcalc RECORDED.tcalc
 *
 * RECORDED holds rows read after the program's own: (demand CALL) asks a call,
 * (petta-answers CALL (answer OUT ...) ...) is what another dialect answered
 * it, and (out-of-domain CALL REASON) is a call outside the relations'
 * declared domain, asked of neither.  The program runs as tcalc-direct reads
 * it (step every over a set store), and each call's answers, the outcome's
 * rows (value CALL OUT ...), must be the recorded answers, as a set: terms
 * are compared by identity in the one term table both were read into.  Prints
 * a line per call and exits 1 on any difference. */
#include "tcalc_core.h"
#include "tcalc_syntax.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    if (fseek(f, 0, SEEK_END) != 0) exit(2);
    long size = ftell(f);
    if (size < 0 || fseek(f, 0, SEEK_SET) != 0) exit(2);
    char *text = malloc((size_t)size + 1);
    if (!text || fread(text, 1, (size_t)size, f) != (size_t)size) exit(2);
    fclose(f);
    text[size] = 0;
    *n = (size_t)size;
    return text;
}

static bool head_is(const TcTerms *t, TcTerm row, const char *name) {
    if (tc_kind(t, row) != TC_EXPRESSION || tc_length(t, row) == 0) return false;
    TcTerm h = tc_item(t, row, 0);
    size_t n = 0;
    const char *b = tc_kind(t, h) == TC_SYMBOL ? tc_bytes(t, h, &n) : NULL;
    return b && n == strlen(name) && memcmp(b, name, n) == 0;
}

static void print_head(const TcTerms *t, TcTerm call) {
    TcTerm h = tc_kind(t, call) == TC_EXPRESSION && tc_length(t, call) ? tc_item(t, call, 0) : call;
    tc_print(t, h, stdout);
}

int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "usage: gslt_tcalc_answers_v1 PROGRAM RECORDED\n"); return 2; }
    size_t pn = 0, rn = 0;
    char *program = slurp(argv[1], &pn), *recorded = slurp(argv[2], &rn);
    char *text = malloc(pn + rn + 2);
    if (!text) return 2;
    memcpy(text, program, pn);
    text[pn] = '\n';
    memcpy(text + pn + 1, recorded, rn);
    text[pn + 1 + rn] = 0;
    TcTerms *terms = tc_terms_new();
    TcDeclarations d;
    TcSyntaxError e;
    TcProgram *p = terms ? tc_read_program(terms, text, pn + 1 + rn, &d, &e) : NULL;
    if (!p) { fprintf(stderr, "FAIL: refused: %zu:%zu: %s\n", e.line, e.column, e.message); return 1; }
    TcInterpretation how = {.step = TC_STEP_EVERY, .ask = TC_ASK_AFTER_TELLS, .store = TC_STORE_SET,
                            .matches = TC_MATCHES_SYMMETRIC, .budget = UINT64_MAX,
                            .backend = TC_BACKEND_C, .observe = TC_OBSERVE_OUTCOMES,
                            .shortcuts = TC_SHORTCUTS_TAKEN};
    TcOutcomes *o = tc_explore(p, &how);
    if (!o || !tc_outcomes_finished(o) || !tc_outcomes_complete_memory(o) || tc_outcomes_count(o) != 1) {
        fprintf(stderr, "FAIL: the run reached no single outcome\n");
        return 1;
    }
    size_t n = 0;
    const TcTerm *rows = tc_outcome_rows(o, 0, &n);
    TcTerm answer = tc_symbol(terms, "answer", 6);
    size_t agree = 0, differ = 0, outside = 0;
    for (size_t i = 0; i < n; i++) {
        TcTerm r = rows[i];
        if (head_is(terms, r, "out-of-domain") && tc_length(terms, r) == 3) {
            printf("out of domain: ");
            print_head(terms, tc_item(terms, r, 1));
            printf(": ");
            tc_print(terms, tc_item(terms, r, 2), stdout);
            printf("\n");
            outside++;
            continue;
        }
        if (!head_is(terms, r, "petta-answers") || tc_length(terms, r) < 2) continue;
        TcTerm call = tc_item(terms, r, 1);
        size_t expected = tc_length(terms, r) - 2, met = 0, found = 0;
        for (size_t j = 0; j < n; j++) {   /* this call's values, each as (answer OUT ...) */
            TcTerm v = rows[j];
            if (!head_is(terms, v, "value") || tc_length(terms, v) < 2 || tc_item(terms, v, 1) != call) continue;
            uint32_t len = tc_length(terms, v) - 1;
            TcTerm *items = malloc(len * sizeof *items);
            if (!items) return 2;
            items[0] = answer;
            for (uint32_t k = 1; k < len; k++) items[k] = tc_item(terms, v, k + 1);
            TcTerm a = tc_expression(terms, items, len);
            free(items);
            met++;
            for (uint32_t k = 2; k < tc_length(terms, r); k++)
                if (tc_item(terms, r, k) == a) { found++; break; }
        }
        bool same = met == expected && found == expected;
        printf("%s ", same ? "agree" : "DIFFER");
        print_head(terms, call);
        printf(": recorded %zu answers, tcalc %zu\n", expected, met);
        if (same) agree++; else differ++;
    }
    printf("%s: %zu calls agree, %zu differ, %zu out of domain\n", differ || !agree ? "FAIL" : "PASS",
           agree, differ, outside);
    tc_outcomes_free(o);
    tc_program_free(p);
    tc_terms_free(terms);
    free(d.region);
    free(text);
    free(program);
    free(recorded);
    return differ || !agree ? 1 : 0;
}

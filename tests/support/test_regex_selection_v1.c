#include "native/regex_compiler_v1.h"
#include "native/prepared_unary_index_v1.h"
#include "parser.h"
#include "symbol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static unsigned passed, failed;
static const CettaReRangeV1 word[] = {{48,57}, {65,90}, {95,95}, {97,122}};
static void check(bool ok, const char *name) {
    if (ok) ++passed; else { ++failed; fprintf(stderr, "FAIL: %s\n", name); }
}
static Atom *term(Arena *a, const char *source) {
    Atom **values = NULL; int n = parse_metta_text(source, a, &values);
    Atom *result = n == 1 ? values[0] : NULL;
    free(values); return result;
}
static Atom *call1(Arena *arena, const char *head, Atom *argument) {
    Atom *items[] = {atom_symbol(arena, head), argument};
    return atom_expr(arena, items, 2u);
}
static Atom *call2(Arena *arena, const char *head, Atom *left, Atom *right) {
    Atom *items[] = {atom_symbol(arena, head), left, right};
    return atom_expr(arena, items, 3u);
}
static void run_address_index(void) {
    Arena arena; arena_init(&arena);
    CettaPreparedUnaryIndexV1 index = {0}; uint32_t row = 0;
    Atom *root = atom_symbol(&arena, "address-root");
    Atom *one = call1(&arena, "address-next", root);
    Atom *two = call1(&arena, "address-next", one);
    Atom *same = call1(&arena, "address-next", call1(&arena, "address-next",
        atom_symbol(&arena, "address-root")));
    check(cpui_bind(&index, root, 7u) && cpui_bind(&index, two, 11u) &&
        cpui_row(&index, same, &row) && row == 11u,
        "separately stored equal addresses share structural identity");
    check(cpui_row(&index, root, &row) && row == 7u,
        "double wrapper does not collapse to its root");
    check(!cpui_bind(&index, same, 99u) && !cpui_row(&index, one, &row),
        "duplicate rows and undefined addresses are not silently accepted");
    Atom *deep = root;
    for (unsigned i = 0; i < 10000u; ++i) deep = call1(&arena, "address-next", deep);
    check(cpui_bind(&index, deep, 13u) && cpui_row(&index, deep, &row) && row == 13u,
        "deep shared addresses use an explicit traversal stack");
    Atom *cycle = call1(&arena, "address-cycle", root);
    cycle->expr.elems[1] = cycle;
    check(!cpui_row(&index, cycle, &row), "cyclic addresses are not finite program labels");
    cycle->expr.elems[1] = root;
    cpui_free(&index); arena_free(&arena);
}
static void scale_compilation(CettaReCompilerV1 *compiler, size_t length, bool keywords) {
    Arena arena; arena_init(&arena);
    Atom *pattern = atom_symbol(&arena, keywords ? "re:zero" : "re:epsilon");
    Atom *literal = call1(&arena, "re:literal", atom_int(&arena, 97));
    for (size_t i = 0; i < length; ++i) {
        if (!keywords) pattern = call2(&arena, "re:seq", literal, pattern);
        else {
            char word_text[64]; snprintf(word_text, sizeof(word_text), "keyword%05zu", i);
            Atom *branch = atom_symbol(&arena, "re:epsilon");
            for (size_t j = strlen(word_text); j; --j)
                branch = call2(&arena, "re:seq", call1(&arena, "re:literal",
                    atom_int(&arena, (unsigned char)word_text[j - 1u])), branch);
            pattern = call2(&arena, "re:alt", pattern, branch);
        }
    }
    CettaReProgramV1 *program = NULL; CettaDeterministicEquationStatusV1 status;
    char error[512] = {0}; clock_t start = clock();
    bool ok = cetta_re_v1_compile(compiler, &arena, pattern, word, 4u, 0u, 0u,
        &program, &status, error, sizeof(error));
    double seconds = (double)(clock() - start) / CLOCKS_PER_SEC;
    if (!ok) fprintf(stderr, "scale compile: %s\n", error);
    check(ok, "unbounded compilation of a constructed literal sequence");
    if (ok) {
        CettaReResultV1 result = cetta_re_v1_find(program, NULL, 0, 0, CETTA_RE_V1_FULL, 0);
        check(result.outcome == (length || keywords ? CETTA_RE_V1_NO_MATCH : CETTA_RE_V1_FOUND),
            "large compiled sequence retains its recognition meaning");
        cetta_re_v1_result_free(&result);
    }
    printf("{\"kind\":\"%s\",\"count\":%zu,\"compile_cpu_seconds\":%.6f,\"compiled\":%s}\n",
        keywords ? "left-associated-keywords" : "literals", length, seconds, ok ? "true" : "false");
    cetta_re_v1_release(program); arena_free(&arena);
}
static CettaReProgramV1 *compile(CettaReCompilerV1 *c, Arena *a, const char *source) {
    CettaReProgramV1 *p = NULL; CettaDeterministicEquationStatusV1 status;
    char error[512] = {0};
    if (!cetta_re_v1_compile(c, a, term(a, source), word, 4u, 4096u, 10000000u,
            &p, &status, error, sizeof(error))) fprintf(stderr, "compile: %s\n", error);
    return p;
}
typedef struct {
    const char *name, *pattern, *input;
    CettaReModeV1 mode;
    size_t start, end;
    int group; size_t group_start, group_end;
} Case;
static const Case cases[] = {
    {"ordered alternative", "(re:alt (re:literal 97) (re:seq (re:literal 97) (re:literal 98)))", "ab", CETTA_RE_V1_SEARCH, 0,1, 0,0,0},
    {"full constraints before selection", "(re:alt (re:literal 97) (re:seq (re:literal 97) (re:literal 98)))", "ab", CETTA_RE_V1_FULL, 0,2, 0,0,0},
    {"greedy", "(re:plus (re:literal 97) Greedy)", "aaa", CETTA_RE_V1_SEARCH, 0,3, 0,0,0},
    {"lazy", "(re:plus (re:literal 97) Lazy)", "aaa", CETTA_RE_V1_SEARCH, 0,1, 0,0,0},
    {"full lazy", "(re:plus (re:literal 97) Lazy)", "aaa", CETTA_RE_V1_FULL, 0,3, 0,0,0},
    {"nonparticipating capture", "(re:seq (re:optional (re:capture 1 (re:literal 97)) Greedy) (re:literal 98))", "b", CETTA_RE_V1_SEARCH, 0,1, -1,0,0},
    {"participating empty capture", "(re:seq (re:capture 1 (re:star (re:literal 97) Greedy)) (re:literal 98))", "b", CETTA_RE_V1_SEARCH, 0,1, 1,0,0},
    {"last repeated capture", "(re:plus (re:capture 1 (re:literal 97)) Greedy)", "aa", CETTA_RE_V1_SEARCH, 0,2, 1,1,2},
    {"nullable alternative", "(re:star (re:capture 1 (re:alt (re:literal 97) re:epsilon)) Greedy)", "aa", CETTA_RE_V1_SEARCH, 0,2, 1,1,2},
    {"nullable star capture", "(re:star (re:capture 1 (re:optional (re:literal 97) Greedy)) Greedy)", "b", CETTA_RE_V1_SEARCH, 0,0, 1,0,0},
    {"nested nullable star", "(re:star (re:capture 1 (re:star (re:literal 97) Greedy)) Greedy)", "b", CETTA_RE_V1_SEARCH, 0,0, 1,0,0},
    {"capture persists across iterations", "(re:plus (re:alt (re:capture 1 (re:literal 97)) (re:literal 98)) Greedy)", "ab", CETTA_RE_V1_SEARCH, 0,2, 1,0,1},
    {"failed branch does not leak", "(re:alt (re:seq (re:capture 1 (re:literal 97)) (re:literal 99)) (re:seq (re:literal 97) (re:literal 98)))", "ab", CETTA_RE_V1_SEARCH, 0,2, -1,0,0},
    {"Unicode byte spans", "(re:capture 1 (re:literal 955))", "a\xce\xbb" "b", CETTA_RE_V1_SEARCH, 1,3, 1,1,3},
    {"empty", "re:epsilon", "a\xce\xbb", CETTA_RE_V1_SEARCH, 0,0, 0,0,0},
    {"typed nullary constructor adapter", "(re:epsilon)", "a", CETTA_RE_V1_SEARCH, 0,0, 0,0,0},
    {"grounded assertion payload", "(re:seq (re:assert \"text-start\") (re:literal \"97\"))", "a", CETTA_RE_V1_SEARCH, 0,1, 0,0,0},
    {"line assertion", "(re:seq (re:assert line-start) (re:literal 98))", "a\nb", CETTA_RE_V1_SEARCH, 2,3, 0,0,0},
    {"class", "(re:plus (re:class (LCons (re:range 97 122) LNil)) Greedy)", "12abc", CETTA_RE_V1_SEARCH, 2,5, 0,0,0},
    {"boundary", "(re:seq (re:assert word-boundary) (re:literal 97))", " a", CETTA_RE_V1_SEARCH, 1,2, 0,0,0},
};
static void run_cases(CettaReCompilerV1 *c) {
    for (size_t i = 0; i < sizeof(cases)/sizeof(*cases); ++i) {
        const Case *v = &cases[i]; Arena a; arena_init(&a);
        CettaReProgramV1 *p = compile(c, &a, v->pattern), *restored = NULL;
        check(p != NULL, v->name); arena_free(&a);
        if (!p) continue;
        uint8_t *image = NULL; size_t length = 0; char error[256] = {0};
        check(cetta_re_v1_store(p, &image, &length, error, sizeof(error)) &&
              cetta_re_v1_restore(image, length, &restored, error, sizeof(error)), "capture artifact restore");
        for (unsigned copy = 0; copy < 2; ++copy) {
            const CettaReProgramV1 *owner = copy ? restored : p;
            CettaReResultV1 r = cetta_re_v1_find(owner, (const uint8_t *)v->input, strlen(v->input), 0, v->mode, 0);
            bool match = r.outcome == CETTA_RE_V1_FOUND && r.groups[0].start == v->start && r.groups[0].end == v->end;
            if (!match) fprintf(stderr, "%s: %s %zu..%zu\n", v->name, cetta_re_v1_outcome_name(r.outcome),
                r.group_len ? r.groups[0].start : 0, r.group_len ? r.groups[0].end : 0);
            check(match, v->name);
            if (v->group) check(r.group_len > 1 && r.groups[1].participated == (v->group > 0) &&
                (v->group < 0 || (r.groups[1].start == v->group_start && r.groups[1].end == v->group_end)), v->name);
            cetta_re_v1_result_free(&r);
        }
        CettaReProgramV1 *same = restored;
        if (image) image[0] ^= 1u;
        check(!cetta_re_v1_restore(image, length, &restored, error, sizeof(error)) && restored == same,
            "bad artifact preserves owned pattern");
        free(image); cetta_re_v1_release(p); cetta_re_v1_release(restored);
    }
}
static void print_groups(const CettaReResultV1 *r) {
    putchar('[');
    for (uint32_t i = 0; i < r->group_len; ++i) {
        if (i) putchar(',');
        if (!r->groups[i].participated) printf("null");
        else printf("[%zu,%zu]", r->groups[i].start, r->groups[i].end);
    }
    putchar(']');
}
static void run_layout_and_iteration(CettaReCompilerV1 *c) {
    Arena a; arena_init(&a); char error[512] = {0};
    const char *names[] = {NULL, "letter"};
    CettaReProgramV1 *p = NULL, *restored = NULL;
    CettaDeterministicEquationStatusV1 status;
    check(cetta_re_v1_compile_named(c, &a, term(&a, "re:epsilon"), names, 2u, word, 4u,
        4096u, 10000000u, &p, &status, error, sizeof(error)), "elided group retains declared layout");
    uint8_t *image = NULL; size_t len = 0;
    check(p && cetta_re_v1_store(p, &image, &len, error, sizeof(error)) &&
        cetta_re_v1_restore(image, len, &restored, error, sizeof(error)) &&
        !strcmp(cetta_re_v1_group_name(restored, 1u), "letter"), "named layout survives restoration");
    if (restored) {
        CettaReResultV1 r = cetta_re_v1_find(restored, NULL, 0, 0, CETTA_RE_V1_SEARCH, 0);
        check(r.outcome == CETTA_RE_V1_FOUND && r.group_len == 2u && !r.groups[1].participated,
            "elided capture is unmatched, not absent from layout");
        cetta_re_v1_result_free(&r);
    }
    CettaReProgramV1 *same = restored;
    check(!cetta_re_v1_restore(image, len ? len - 1u : 0u, &restored, error, sizeof(error)) && restored == same,
        "truncated names preserve previous owner");
    free(image); cetta_re_v1_release(p); cetta_re_v1_release(restored);
    p = compile(c, &a, "re:epsilon");
    const uint8_t input[] = {'a', 0xce, 0xbb};
    CettaReCursorV1 cursor = {0};
    check(cetta_re_v1_cursor(&cursor, input, sizeof(input)), "cursor validates original UTF8");
    const size_t offsets[] = {0, 1, 3};
    for (unsigned i = 0; i < 3u; ++i) {
        CettaReResultV1 r = cetta_re_v1_next(p, &cursor, 0);
        check(r.outcome == CETTA_RE_V1_FOUND && r.groups[0].start == offsets[i] &&
            r.groups[0].end == offsets[i], "empty iteration follows scalar boundaries");
        cetta_re_v1_result_free(&r);
    }
    for (unsigned i = 0; i < 2u; ++i) {
        CettaReResultV1 r = cetta_re_v1_next(p, &cursor, 0);
        check(r.outcome == CETTA_RE_V1_NO_MATCH, "iterator completion is stable");
        cetta_re_v1_result_free(&r);
    }
    cetta_re_v1_release(p);
    p = compile(c, &a, "(re:star (re:literal 97) Greedy)");
    (void)cetta_re_v1_cursor(&cursor, (const uint8_t *)"aa", 2u);
    CettaReResultV1 r = cetta_re_v1_next(p, &cursor, 0);
    check(r.outcome == CETTA_RE_V1_FOUND && r.groups[0].end == 2u, "iteration nonempty match");
    cetta_re_v1_result_free(&r); r = cetta_re_v1_next(p, &cursor, 0);
    check(r.outcome == CETTA_RE_V1_NO_MATCH, "adjacent empty is suppressed after nonempty");
    cetta_re_v1_result_free(&r); cetta_re_v1_release(p);
    const CettaReInstructionV1 accept = {.opcode = CETTA_RE_V1_MATCH};
    const char *unique[] = {NULL, "letter", "\xce\xbb"};
    CettaReProgramSpecV1 spec = {.instructions = &accept, .instruction_len = 1u,
        .group_len = 3u, .group_names = unique};
    p = NULL;
    check(cetta_re_v1_program(&spec, &p, error, sizeof(error)) &&
        !strcmp(cetta_re_v1_group_name(p, 2u), "\xce\xbb"), "distinct UTF8 capture names retain layout");
    const char *duplicate[] = {NULL, "letter", "letter"};
    spec.group_names = duplicate; same = p;
    check(!cetta_re_v1_program(&spec, &p, error, sizeof(error)) && p == same,
        "duplicate-name refusal leaves the previous owner unchanged");
    const char *bad_name[] = {NULL, "letter", "\xff"};
    spec.group_names = bad_name;
    check(!cetta_re_v1_program(&spec, &p, error, sizeof(error)) && p == same,
        "malformed capture names are not accepted as UTF8");
    cetta_re_v1_release(p);
    arena_free(&a);
}
static void run_compiler_restoration(CettaReCompilerV1 *c) {
    uint8_t *image = NULL; size_t len = 0; char error[512] = {0};
    check(cetta_re_v1_compiler_store(c, &image, &len, error, sizeof(error)), "store prepared compiler");
    for (unsigned audited = 0; audited < 2u; ++audited) {
        CettaReCompilerV1 *restored = NULL;
        check(cetta_re_v1_compiler_restore(image, len, audited != 0, &restored, error, sizeof(error)),
            "restore prepared compiler without source admission");
        if (!restored) continue;
        const char *patterns[] = {
            "(re:capture 1 (re:plus (re:literal 955) Greedy))",
            "(re:seq (re:capture 1 (re:literal 955)) (re:backref 1 Exact))",
            "(re:seq (re:capture 1 (re:literal 75)) (re:backref 1 UnicodeFold))",
            "(re:lookahead (re:not-lookbehind Forward (re:literal 97)))",
            "(re:lookbehind Reverse (re:repeat-at-least (re:capture 1 (re:literal 97)) (re:count-succ re:count-zero) Greedy))"};
        for (size_t i = 0; i < sizeof(patterns) / sizeof(*patterns); ++i) {
            Arena a; arena_init(&a);
            CettaReProgramV1 *first = compile(c, &a, patterns[i]);
            CettaReProgramV1 *second = compile(restored, &a, patterns[i]);
            uint8_t *one = NULL, *two = NULL; size_t one_len = 0, two_len = 0;
            check(cetta_re_v1_store(first, &one, &one_len, error, sizeof(error)) &&
                cetta_re_v1_store(second, &two, &two_len, error, sizeof(error)) &&
                one_len == two_len && !memcmp(one, two, one_len), "restored compiler emits identical capture programs");
            free(one); free(two); cetta_re_v1_release(first); cetta_re_v1_release(second);
            arena_free(&a);
        }
        cetta_re_v1_compiler_free(restored);
    }
    free(image);
}
int main(int argc, char **argv) {
    SymbolTable symbols; symbol_table_init(&symbols); g_symbols = &symbols;
    CettaReCompilerV1 *c = NULL; char error[512] = {0};
    check(cetta_re_v1_compiler_load(&c, error, sizeof(error)), error);
    if (!c) return 1;
    if (argc == 3 && (!strcmp(argv[1], "--scale") || !strcmp(argv[1], "--scale-keywords"))) {
        char *end = NULL; unsigned long long length = strtoull(argv[2], &end, 10);
        if (!argv[2][0] || !end || *end || length > SIZE_MAX) {
            fprintf(stderr, "expected a representable literal count\n"); ++failed;
        } else scale_compilation(c, (size_t)length, !strcmp(argv[1], "--scale-keywords"));
    } else if (argc == 5 && !strcmp(argv[1], "--probe")) {
        Arena a; arena_init(&a); CettaReProgramV1 *p = compile(c, &a, argv[3]);
        if (!p) { ++failed; arena_free(&a); goto done; }
        if (!strcmp(argv[2], "all")) {
            CettaReCursorV1 cursor = {0};
            (void)cetta_re_v1_cursor(&cursor, (const uint8_t *)argv[4], strlen(argv[4]));
            printf("{\"matches\":["); unsigned n = 0;
            for (;;) {
                CettaReResultV1 r = cetta_re_v1_next(p, &cursor, 0);
                if (r.outcome == CETTA_RE_V1_NO_MATCH) { cetta_re_v1_result_free(&r); break; }
                if (r.outcome != CETTA_RE_V1_FOUND) { cetta_re_v1_result_free(&r); ++failed; break; }
                if (n++) putchar(',');
                print_groups(&r); cetta_re_v1_result_free(&r);
            }
            puts("]}");
            cetta_re_v1_release(p); arena_free(&a);
            goto done;
        }
        CettaReModeV1 mode = !strcmp(argv[2], "full") ? CETTA_RE_V1_FULL : CETTA_RE_V1_SEARCH;
        CettaReResultV1 r = cetta_re_v1_find(p, (const uint8_t *)argv[4], strlen(argv[4]), 0, mode, 0);
        printf("{\"outcome\":\"%s\",\"groups\":", cetta_re_v1_outcome_name(r.outcome));
        print_groups(&r);
        puts("}"); cetta_re_v1_result_free(&r); cetta_re_v1_release(p); arena_free(&a);
    } else if (argc == 3 && (!strcmp(argv[1], "--freeze") || !strcmp(argv[1], "--freeze-extended"))) {
        Arena a; arena_init(&a);
        CettaReProgramV1 *p = compile(c, &a, !strcmp(argv[1], "--freeze-extended") ?
            "(re:seq (re:capture 1 (re:literal 955)) (re:seq (re:backref 1 Exact) (re:lookahead (re:literal 33))))" :
            "(re:capture 1 (re:plus (re:literal 955) Greedy))");
        uint8_t *image = NULL; size_t len = 0;
        bool ok = cetta_re_v1_store(p, &image, &len, error, sizeof(error));
        FILE *file = ok ? fopen(argv[2], "wb") : NULL;
        if (!file) ok = false;
        else { ok = fwrite(image, 1u, len, file) == len; if (fclose(file)) ok = false; }
        check(ok, "freeze capture program for standalone consumer");
        free(image); cetta_re_v1_release(p); arena_free(&a);
    } else {
        run_address_index();
        run_compiler_restoration(c);
        CettaDeterministicEquationPreparationV1 before = cetta_deterministic_equation_plan_v1_preparation();
        run_cases(c);
        run_layout_and_iteration(c);
        Arena a; arena_init(&a); CettaReProgramV1 *p = compile(c, &a, "(re:literal 97)");
        CettaReProgramV1 *previous = p;
        CettaDeterministicEquationStatusV1 status;
        check(!cetta_re_v1_compile(c, &a, term(&a, "(re:plus (re:literal 97) Greedy)"), word, 4u,
                0u, 1u, &p, &status, error, sizeof(error)) && p == previous &&
                status == CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT,
            "explicit compile budget remains incomplete and preserves the previous owner");
        CettaReProgramV1 *unbounded = NULL;
        check(cetta_re_v1_compile(c, &a, term(&a, "(re:plus (re:literal 97) Greedy)"), word, 4u,
                0u, 0u, &unbounded, &status, error, sizeof(error)),
            "zero allowances request compilation without caller-imposed limits");
        cetta_re_v1_release(unbounded);
        check(!cetta_re_v1_compile(c, &a, term(&a, "(re:assert \"not-a-declared-assertion\")"), word, 4u,
                4096u, 10000000u, &p, &status, error, sizeof(error)) && p == previous &&
                status == CETTA_DETERMINISTIC_EQUATION_V1_NO_RULE,
            "unknown assertion retains the previous owner and an interpretation obligation");
        check(!cetta_re_v1_compile(c, &a, term(&a, "(re:literal \"097\")"), word, 4u,
                4096u, 10000000u, &p, &status, error, sizeof(error)) && p == previous &&
                status == CETTA_DETERMINISTIC_EQUATION_V1_NO_RULE,
            "noncanonical numeral is not silently reinterpreted");
        uint8_t invalid[] = {0xff};
        CettaReResultV1 r = cetta_re_v1_find(p, invalid, 1, 0, CETTA_RE_V1_SEARCH, 0);
        check(r.outcome == CETTA_RE_V1_INVALID_UTF8, "invalid UTF8 is not absence"); cetta_re_v1_result_free(&r);
        r = cetta_re_v1_find(p, (const uint8_t *)"b", 1, 0, CETTA_RE_V1_SEARCH, 0);
        check(r.outcome == CETTA_RE_V1_NO_MATCH, "completed negative"); cetta_re_v1_result_free(&r);
        r = cetta_re_v1_find(p, (const uint8_t *)"aaaa", 4, 0, CETTA_RE_V1_SEARCH, 1);
        check(r.outcome == CETTA_RE_V1_INCOMPLETE, "budget exhausted is not absence"); cetta_re_v1_result_free(&r);
        CettaReProgramV1 *retained = cetta_re_v1_retain(p); cetta_re_v1_release(p);
        r = cetta_re_v1_find(retained, (const uint8_t *)"a", 1, 0, CETTA_RE_V1_SEARCH, 0);
        check(r.outcome == CETTA_RE_V1_FOUND, "independent retained owner"); cetta_re_v1_result_free(&r);
        cetta_re_v1_release(retained); arena_free(&a);
        CettaDeterministicEquationPreparationV1 after = cetta_deterministic_equation_plan_v1_preparation();
        check(after.source_loads == before.source_loads && after.source_admissions == before.source_admissions &&
              after.audits == before.audits, "pattern compilation never reloads authored compiler source");
        printf("(RegexSelectionV1Summary %u %u)\n", passed, failed);
    }
done:
    cetta_re_v1_compiler_free(c); symbol_table_free(&symbols); g_symbols = NULL;
    return failed ? 1 : 0;
}

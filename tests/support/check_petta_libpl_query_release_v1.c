/* check_petta_libpl_query_release_v1 — permanent FFI lifecycle gate for the
 * PeTTa/Prolog boundary's query-release protocol (2026-09-30, Kimi).
 *
 * Nine scenarios over the SWI this build links (in-house 10.1.9):
 *   1. normal cut after one answer, quiet cleanup
 *   2. normal close after one answer, quiet cleanup
 *   3. teardown fault at CUT with an unread solution left
 *   4. teardown fault at CLOSE with an unread solution left
 *   5. body fault consumed via PL_exception(query), then clean cut
 *   6. body fault with teardown also throwing: body dominates, teardown lost
 *   7. nested queries: out-of-order release refused (PL_S_NOT_INNER),
 *      valid order afterwards (the refusal never cancels the outer query)
 *   8. a clean query after handled exceptions
 *   9. control: close with no solutions requested registers no teardown
 *
 * Release helper petta_libpl_release_query encodes exactly these facts; this
 * gate proves them against the linked SWI itself, which no MeTTa program can
 * reach with certainty (a MeTTa call always exhausts its generator inside
 * the goal first).
 */
#include <SWI-Prolog.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

static void require(bool condition, const char *what) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", what);
        exit(1);
    }
}

static qid_t open_pred(const char *name, int arity, term_t args) {
    return PL_open_query(NULL,
        PL_Q_NODEBUG | PL_Q_CATCH_EXCEPTION | PL_Q_EXT_STATUS,
        PL_predicate(name, arity, "user"), args);
}

static const char *show(term_t t, char *buffer, size_t size) {
    char *s = NULL;
    if (t && PL_get_chars(t, &s, CVT_WRITE) && s)
        snprintf(buffer, size, "%s", s);
    else
        snprintf(buffer, size, "(none)");
    return buffer;
}

static void load(const char *file) {
    fid_t frame = PL_open_foreign_frame();
    term_t path = PL_new_term_ref();
    require(PL_put_atom_chars(path, file), "fixture path");
    require(PL_call_predicate(NULL, PL_Q_NODEBUG | PL_Q_CATCH_EXCEPTION,
        PL_predicate("consult", 1, "system"), path), "consult fixture");
    PL_discard_foreign_frame(frame);
}

static int quiet_case(int use_cut) {
    fid_t f0 = PL_open_foreign_frame();
    term_t args = PL_new_term_refs(2);
    require(PL_put_atom_chars(args + 0, "quiet") && PL_put_variable(args + 1),
            "quiet args");
    qid_t q = open_pred("qrel_gen", 2, args);
    require(q, "quiet open");
    require(PL_next_solution(q) == PL_S_TRUE, "quiet first answer");
    int rc = use_cut ? PL_cut_query(q) : PL_close_query(q);
    require(rc == true && PL_exception(0) == 0, "quiet release clean");
    PL_discard_foreign_frame(f0);
    return rc;
}

static void cleanup_fault_case(int use_cut) {
    fid_t f0 = PL_open_foreign_frame();
    term_t args = PL_new_term_refs(2);
    require(PL_put_atom_chars(args + 0, "throw") && PL_put_variable(args + 1),
            "throw args");
    qid_t q = open_pred("qrel_gen", 2, args);
    require(q, "throw open");
    require(PL_next_solution(q) == PL_S_TRUE, "throw first answer");
    int rc = use_cut ? PL_cut_query(q) : PL_close_query(q);
    char a[512];
    require(rc == false, "teardown fault reported by release");
    term_t pending = PL_exception(0);
    require(pending != 0, "teardown fault pending after release");
    printf("   cleanup fault at %s: %s\n", use_cut ? "cut" : "close",
           show(pending, a, sizeof a));
    PL_clear_exception();
    require(PL_exception(0) == 0, "handled teardown fault cleared");
    PL_discard_foreign_frame(f0);
}

int main(int argc, char **argv) {
    (void)setvbuf(stdout, NULL, _IONBF, 0);
    require(argc == 2, "fixture path required");
    char *initial[] = {"check-prefix", "-q", "--nosignals", NULL};
    require(PL_initialise(3, initial), "PL_initialise");
    load(argv[1]);

    /* 1, 2. quiet cut and quiet close. */
    require(quiet_case(1) == true, "1 quiet cut");
    require(quiet_case(0) == true, "2 quiet close");

    /* 3, 4. teardown fault at cut and close with an unread solution. */
    cleanup_fault_case(1);
    cleanup_fault_case(0);

    /* 5. body fault consumed through PL_exception(query), then clean cut. */
    {
        fid_t f0 = PL_open_foreign_frame();
        term_t args = PL_new_term_refs(1);
        require(PL_put_variable(args + 0), "raise args");
        qid_t q = open_pred("qrel_raise_body", 1, args);
        require(q, "raise open");
        require(PL_next_solution(q) == PL_S_EXCEPTION, "5 body exception propagates");
        term_t body = PL_exception(q);
        char a[512];
        require(body != 0, "5 body readable");
        printf("   body value: %s\n", show(body, a, sizeof a));
        require(PL_exception(0) == 0, "5 pending cleared by read");
        require(PL_cut_query(q) == true && PL_exception(0) == 0,
                "5 cut after consumed body fault");
        PL_discard_foreign_frame(f0);
    }

    /* 6. body fault with teardown also throwing: body dominates. */
    {
        fid_t f0 = PL_open_foreign_frame();
        term_t args = PL_new_term_refs(1);
        require(PL_put_variable(args + 0), "both args");
        qid_t q = open_pred("qrel_raise_both", 1, args);
        require(q, "both open");
        require(PL_next_solution(q) == PL_S_EXCEPTION, "6 body exception propagates");
        term_t body = PL_exception(q);
        char a[512];
        require(body != 0, "6 body readable");
        printf("   body dominates teardown: %s\n", show(body, a, sizeof a));
        require(PL_cut_query(q) == true && PL_exception(0) == 0,
                "6 teardown fault swallowed by SWI during body exception");
        PL_discard_foreign_frame(f0);
    }

    /* 7. out-of-order release refused; valid order afterwards. */
    {
        fid_t fn1 = PL_open_foreign_frame();
        term_t pa = PL_new_term_refs(2);
        require(PL_put_atom_chars(pa + 0, "quiet") && PL_put_variable(pa + 1),
                "nested a");
        qid_t qa = open_pred("qrel_gen", 2, pa);
        fid_t fn2 = PL_open_foreign_frame();
        term_t pb = PL_new_term_refs(2);
        require(PL_put_atom_chars(pb + 0, "quiet") && PL_put_variable(pb + 1),
                "nested b");
        qid_t qb = open_pred("qrel_gen", 2, pb);
        require(fn1 && fn2 && qa && qb, "7 nested opens");
        require(PL_cut_query(qa) == PL_S_NOT_INNER, "7 outer-first refused");
        require(PL_cut_query(qb) == true, "7 inner releases");
        PL_discard_foreign_frame(fn2);
        require(PL_cut_query(qa) == true,
                "7 outer still releasable afterwards");
        PL_discard_foreign_frame(fn1);
    }

    /* 8. a clean query after handled exceptions. */
    require(quiet_case(1) == true, "8 clean query works");

    /* 9. control: close with no solutions registers no teardown. */
    {
        fid_t f0 = PL_open_foreign_frame();
        term_t args = PL_new_term_refs(2);
        require(PL_put_atom_chars(args + 0, "throw") && PL_put_variable(args + 1),
                "control args");
        qid_t q = open_pred("qrel_gen", 2, args);
        require(q, "9 control open");
        require(PL_close_query(q) == true && PL_exception(0) == 0,
                "9 close with no solutions is clean");
        PL_discard_foreign_frame(f0);
    }

    printf("PASS: petta/libpl query-release lifecycle (9 scenarios)\n");
    return 0;
}

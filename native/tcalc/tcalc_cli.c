/* tcalc_cli.c - runs a tcalc program and prints its interpretations. */
#define _POSIX_C_SOURCE 200809L   /* open_memstream */

#include "tcalc_cli.h"
#include "tcalc_core.h"
#include "tcalc_surface.h"
#include "tcalc_syntax.h"
#include "tcalc_x86.h"
#include "../kernel/kernel.h"
#include "../../src/runner.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

/* The version of the calculus this core implements: RuleSyntax/Core.lean at one
 * frame, with the step disciplines of RuleSyntax/JointFirings (source hashes in
 * docs/tcalc.md). */
#define TCALC_VERSION "0.1"
#define TCALC_DEFAULT_BUDGET UINT64_C(100000)

typedef struct {
    bool has_step, all_steps, has_ask, has_store, has_matches, has_backend, has_observe,
        has_schedule, has_budget, has_region;
    TcStep step;
    TcAsk ask;
    TcStore store;
    TcMatches matches;
    TcBackend backend;
    TcObserve observe;
    RunSchedule schedule;
    uint64_t budget;
    const char *region; size_t region_length;   /* NAME+NAME..., resolved later */
} Profile;

static bool parse_profile(const char *text, Profile *p, FILE *err) {
    memset(p, 0, sizeof *p);
    if (!text) return true;
    for (const char *at = text; *at;) {
        const char *end = at;
        while (*end && *end != ';' && *end != ',') end++;
        const char *eq = memchr(at, '=', (size_t)(end - at));
        if (end > at) {
            if (!eq) { fprintf(err, "error: a profile setting is AXIS=VALUE: %.*s\n", (int)(end - at), at); return false; }
            size_t klen = (size_t)(eq - at), vlen = (size_t)(end - eq - 1);
            const char *v = eq + 1;
            bool ok = true, twice = false;
            if (klen == 4 && memcmp(at, "step", 4) == 0) {
                twice = p->has_step;
                if (vlen == 3 && memcmp(v, "all", 3) == 0) p->all_steps = true;
                else ok = tc_step_from_name(v, vlen, &p->step);
                p->has_step = ok;
            } else if (klen == 3 && memcmp(at, "ask", 3) == 0) {
                twice = p->has_ask;
                ok = p->has_ask = tc_ask_from_name(v, vlen, &p->ask);
            } else if (klen == 5 && memcmp(at, "store", 5) == 0) {
                twice = p->has_store;
                ok = p->has_store = tc_store_from_name(v, vlen, &p->store);
            } else if (klen == 7 && memcmp(at, "matches", 7) == 0) {
                twice = p->has_matches;
                ok = p->has_matches = tc_matches_from_name(v, vlen, &p->matches);
            } else if (klen == 7 && memcmp(at, "backend", 7) == 0) {
                twice = p->has_backend;
                ok = p->has_backend = tc_backend_from_name(v, vlen, &p->backend);
            } else if (klen == 6 && memcmp(at, "budget", 6) == 0) {
                twice = p->has_budget;
                char *stop;
                unsigned long long n = strtoull(v, &stop, 10);
                ok = stop == end && n > 0 && v[0] >= '0' && v[0] <= '9';
                p->budget = n;
                p->has_budget = ok;
            } else if (klen == 6 && memcmp(at, "region", 6) == 0) {
                twice = p->has_region;
                p->region = v;
                p->region_length = vlen;
                p->has_region = ok = vlen > 0;
            } else if (klen == 7 && memcmp(at, "observe", 7) == 0) {
                twice = p->has_observe;
                ok = p->has_observe = tc_observe_from_name(v, vlen, &p->observe);
            } else if (klen == 8 && memcmp(at, "schedule", 8) == 0) {
                twice = p->has_schedule;
                ok = p->has_schedule = run_schedule_from_name(v, vlen, &p->schedule);
            } else {
                fprintf(err, "error: no profile axis is called %.*s\n", (int)klen, at);
                return false;
            }
            if (twice) {
                fprintf(err, "error: the profile sets %.*s twice\n", (int)klen, at);
                return false;
            }
            if (!ok) { fprintf(err, "error: the profile value %.*s is not one this axis takes\n", (int)(end - at), at); return false; }
        }
        at = *end ? end + 1 : end;
    }
    return true;
}

/* The rule indices a region of the profile names. */
static bool resolve_region(const TcProgram *program, const Profile *p, uint32_t **out,
                           uint32_t *count, FILE *err) {
    *out = NULL;
    *count = 0;
    for (const char *at = p->region, *stop = p->region + p->region_length; at < stop;) {
        const char *end = at;
        while (end < stop && *end != '+') end++;
        uint32_t found = UINT32_MAX;
        for (uint32_t k = 0; k < tc_program_rules(program); k++) {
            const char *name = tc_program_rule_name(program, k);
            if (strlen(name) == (size_t)(end - at) && memcmp(name, at, (size_t)(end - at)) == 0) found = k;
        }
        if (found == UINT32_MAX) {
            fprintf(err, "error: the profile's region names no rule %.*s\n", (int)(end - at), at);
            free(*out);
            return false;
        }
        uint32_t *g = realloc(*out, (*count + 1) * sizeof *g);
        if (!g) { free(*out); return false; }
        *out = g;
        (*out)[(*count)++] = found;
        at = end < stop ? end + 1 : end;
    }
    return true;
}

/* A row as text, in the notation the program was written in. */
static char *term_text(const TcTerms *terms, TcTerm term, TcSurface surface) {
    char *buf = NULL;
    size_t len = 0;
    FILE *f = open_memstream(&buf, &len);
    if (!f) return NULL;
    tc_surface_print_row(surface, terms, term, f);
    fclose(f);
    return buf;
}

static int text_order(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* A state's rows as text, each after a space, in the order of their text: a
 * state is a bag, and this order is for reading only. */
static char *rows_text(const TcTerms *terms, const TcTerm *rows, size_t n, TcSurface surface) {
    char **row = calloc(n ? n : 1, sizeof *row);
    bool ok = row != NULL;
    size_t length = 1;
    for (size_t i = 0; ok && i < n; i++) {
        ok = (row[i] = term_text(terms, rows[i], surface)) != NULL;
        if (ok) length += strlen(row[i]) + 1;
    }
    char *text = ok ? malloc(length) : NULL;
    if (text) {
        qsort(row, n, sizeof *row, text_order);
        char *at = text;
        for (size_t i = 0; i < n; i++) {
            *at++ = ' ';
            size_t l = strlen(row[i]);
            memcpy(at, row[i], l);
            at += l;
        }
        *at = 0;
    }
    for (size_t i = 0; row && i < n; i++) free(row[i]);
    free(row);
    return text;
}

/* The interpretation line: every choice the run made, including the ones
 * nobody named.  The region is a choice of every and unique, the schedule of
 * a run, and are printed where they are made. */
static void print_interpretation(const TcProgram *program, const TcInterpretation *how, FILE *out) {
    fprintf(out, "(interpretation (version tcalc@%s) (step %s) (ask %s) (store %s)",
            TCALC_VERSION, tc_step_name(how->step), tc_ask_name(how->ask), tc_store_name(how->store));
    if (how->step == TC_STEP_EVERY || how->step == TC_STEP_UNIQUE) {
        fputs(" (region", out);
        if (!how->regions) fputs(" all", out);
        for (uint32_t i = 0; i < how->regions; i++)
            fprintf(out, " %s", tc_program_rule_name(program, how->region[i]));
        fputc(')', out);
    }
    fprintf(out, " (matches %s) (solver mgu-first-order) (observe %s)", tc_matches_name(how->matches),
            tc_observe_name(how->observe));
    if (how->observe == TC_OBSERVE_RUN) fprintf(out, " (schedule %s)", run_schedule_name(how->schedule));
    fprintf(out, " (budget %" PRIu64 ") (backend %s))\n", how->budget, tc_backend_name(how->backend));
}

/* Runs one interpretation and prints it.  Returns false when memory ran out. */
static bool run_one(TcProgram *program, const TcInterpretation *how, TcSurface surface,
                    FILE *out) {
    print_interpretation(program, how, out);
    TcOutcomes *o = tc_explore(program, how);
    if (!o) return false;
    const TcTerms *terms = tc_program_terms(program);
    size_t n = tc_outcomes_count(o);
    char **text = calloc(n ? n : 1, sizeof *text);
    bool ok = text != NULL;
    for (size_t i = 0; ok && i < n; i++) {
        size_t rows;
        const TcTerm *row = tc_outcome_rows(o, i, &rows);
        ok = (text[i] = rows_text(terms, row, rows, surface)) != NULL;
    }
    if (ok) {
        if (how->observe == TC_OBSERVE_RUN)   /* the run's states, in their order */
            for (size_t i = 0; i < n; i++) fprintf(out, "(state%s)\n", text[i]);
        else {
            qsort(text, n, sizeof *text, text_order);   /* an order of printing only */
            for (size_t i = 0; i < n; i++) fprintf(out, "(outcome%s)\n", text[i]);
        }
        for (size_t i = 0; i < tc_outcomes_ambiguous(o); i++) {
            size_t rows, matches;
            const TcTerm *row = tc_ambiguous_rows(o, i, &rows, &matches);
            char *t = rows_text(terms, row, rows, surface);
            if (!t) { ok = false; break; }
            fprintf(out, "(ambiguous (matches %zu)%s)\n", matches, t);
            free(t);
        }
        fprintf(out, "(explored (states %" PRIu64 ") (work %" PRIu64 ") (finished %s) (cycle %s)%s%s%s)\n",
                tc_outcomes_states(o), tc_outcomes_work(o),
                tc_outcomes_finished(o) ? "true" : "false",
                tc_outcomes_cyclic(o) ? "true" : "false",
                tc_outcomes_complete_memory(o) ? "" : " (memory exhausted)",
                tc_outcomes_decider_failed(o) ? " (decider gave no verdict)" : "",
                tc_outcomes_shortcuts_disagreed(o) ? " (shortcuts disagreed)" : "");
    }
    for (size_t i = 0; text && i < n; i++) free(text[i]);
    free(text);
    bool complete = tc_outcomes_complete_memory(o) && !tc_outcomes_decider_failed(o);
    tc_outcomes_free(o);
    return ok && complete;
}

/* Reads a program in its notation: tcalc's own is read directly; another is
 * first translated, and a fault the reader finds in the translation is
 * reported at the source line it came from, with the form it became. */
static TcProgram *read_in(TcSurface surface, TcTerms *terms, const char *text, size_t length,
                          TcDeclarations *d, FILE *err, int *status) {
    TcSyntaxError e;
    if (surface == TC_SURFACE_TCALC) {
        TcProgram *program = tc_read_program(terms, text, length, d, &e);
        if (!program && e.line) fprintf(err, "error: %zu:%zu: %s\n", e.line, e.column, e.message);
        *status = program ? 0 : e.line ? 2 : 1;
        return program;
    }
    TcCanonical c;
    if (!tc_surface_translate(surface, text, length, &c, &e)) {
        if (e.line) fprintf(err, "error: %zu:%zu: %s\n", e.line, e.column, e.message);
        *status = e.line ? 2 : 1;
        return NULL;
    }
    TcProgram *program = tc_read_program(terms, c.text, c.length, d, &e);
    if (!program && e.line) {
        const char *form = c.text;   /* line e.line of the translation */
        for (size_t k = 1; k < e.line && (form = strchr(form, '\n')); k++) form++;
        size_t source = e.line <= c.lines ? c.source_line[e.line - 1] : 0;
        int shown = form ? (int)strcspn(form, "\n") : 0;
        fprintf(err, "error: %zu: %s, in the %s form this becomes: %.*s\n", source, e.message,
                tc_surface_name(TC_SURFACE_TCALC), shown, form ? form : "");
    }
    *status = program ? 0 : e.line ? 2 : 1;
    tc_canonical_free(&c);
    return program;
}

/* A program read, with the interpretation its declarations and a profile
 * give: the profile's setting of an axis first, then the program's, then
 * Core.lean's choice. */
typedef struct {
    TcSurface surface;
    TcTerms *terms;
    TcProgram *program;
    TcDeclarations d;
    uint32_t *profile_region;
    TcInterpretation how;
    bool every_step;   /* no step named: every step, each in turn */
} Run;

static void run_close(Run *run) {
    free(run->profile_region);
    free(run->d.region);
    tc_program_free(run->program);
    tc_terms_free(run->terms);
}

/* CETTA_TCALC_SHORTCUTS: taken (unset: the default), none (every successor
 * computed in full: the reference) or checked (both, compared).  The output is
 * the same, but for a disagreement, which a checked run reports; the gates run
 * every fixture checked.  False for any other value. */
static bool shortcuts_requested(TcShortcuts *shortcuts, FILE *err) {
    const char *setting = getenv("CETTA_TCALC_SHORTCUTS");
    *shortcuts = TC_SHORTCUTS_TAKEN;
    if (!setting || !*setting || strcmp(setting, "taken") == 0) return true;
    if (strcmp(setting, "none") == 0) { *shortcuts = TC_SHORTCUTS_NONE; return true; }
    if (strcmp(setting, "checked") == 0) { *shortcuts = TC_SHORTCUTS_CHECKED; return true; }
    fprintf(err, "error: CETTA_TCALC_SHORTCUTS is taken, none or checked, not %s\n", setting);
    return false;
}

/* Reads the program and settles its interpretation.  0, or the exit status of
 * a refusal (2) or of memory running out (1) with the reason written to err,
 * the run then closed already. */
static int run_open(Run *run, const char *text, size_t length, const char *syntax,
                    const char *profile_text, const char *version, FILE *err) {
    memset(run, 0, sizeof *run);
    if (syntax && !tc_surface_from_name(syntax, &run->surface)) {
        fprintf(err, "error: tcalc reads the syntaxes tcalc, rho and chr, not %s\n", syntax);
        return 2;
    }
    Profile profile;
    TcShortcuts shortcuts;
    if (!parse_profile(profile_text, &profile, err) || !shortcuts_requested(&shortcuts, err))
        return 2;
    if (version && *version && strcmp(version, TCALC_VERSION) != 0) {
        fprintf(err, "error: this core implements tcalc@%s, not tcalc@%s\n", TCALC_VERSION, version);
        return 2;
    }
    if (!(run->terms = tc_terms_new())) return 1;
    int status;
    run->program = read_in(run->surface, run->terms, text, length, &run->d, err, &status);
    if (!run->program) {
        if (status == 1) fprintf(err, "error: out of memory\n");
        tc_terms_free(run->terms);
        return status;
    }
    const TcDeclarations *d = &run->d;
    if (d->has_version && strcmp(d->version, TCALC_VERSION) != 0) {
        fprintf(err, "error: the program is written for tcalc@%s; this core implements tcalc@%s\n",
                d->version, TCALC_VERSION);
        status = 2;
    }
    uint32_t *region = d->region, regions = d->regions;
    if (!status && profile.has_region) {
        uint32_t count;
        if (!resolve_region(run->program, &profile, &run->profile_region, &count, err)) status = 2;
        else { region = run->profile_region; regions = count; }
    }
    run->how = (TcInterpretation){
        .step = profile.has_step ? profile.step : d->step,
        /* Core.lean's choices, when neither the profile nor the program names one */
        .ask = profile.has_ask ? profile.ask : d->has_ask ? d->ask : TC_ASK_AFTER_TELLS,
        .store = profile.has_store ? profile.store : d->has_store ? d->store : TC_STORE_MULTISET,
        /* matches up to symmetry, so that every and unique do not read how a
         * rule is presented (with keys, every over a rule that takes two
         * identical rows has no step at two such rows) */
        .matches = profile.has_matches ? profile.matches
                 : d->has_matches ? d->matches : TC_MATCHES_SYMMETRIC,
        .backend = profile.has_backend ? profile.backend
                 : d->has_backend ? d->backend : TC_BACKEND_C,
        .observe = profile.has_observe ? profile.observe
                 : d->has_observe ? d->observe : TC_OBSERVE_OUTCOMES,
        .schedule = profile.has_schedule ? profile.schedule
                  : d->has_schedule ? d->schedule : RUN_SCHEDULE_CANONICAL,
        .budget = profile.has_budget ? profile.budget : d->has_budget ? d->budget : TCALC_DEFAULT_BUDGET,
        .region = region, .regions = regions,
        .shortcuts = shortcuts,
    };
    run->every_step = profile.all_steps || (!profile.has_step && !d->has_step);
    if (!status && run->how.backend == TC_BACKEND_X86_FLAT) {   /* a flat decider decides single firings */
        char why[200];
        if (cetta_kernel_requested() != CETTA_KERNEL_C) {
            fprintf(err, "error: backend x86-flat and a non-C --kernel select different execution interfaces\n");
            status = 2;
        } else if (run->every_step || run->how.step != TC_STEP_ONE) {
            fprintf(err, "error: backend x86-flat decides step one; name it (step=one)\n");
            status = 2;
        } else if (!tc_program_flat(run->program, why, sizeof why)) {
            fprintf(err, "error: backend x86-flat: %s\n", why);
            status = 2;
        } else if (!tcalc_x86_available()) {
            fprintf(err, "error: backend x86-flat: this binary does not carry the flat_fire image\n");
            status = 2;
        }
        run->how.flat = tcalc_x86_decide;
    }
    if (status) run_close(run);
    return status;
}

/* Does `action` under the step named, or under every step in turn.  1 when an
 * action ran out of memory. */
static int each_step(Run *run, bool (*action)(Run *, void *, FILE *), void *env, FILE *out) {
    int first = run->every_step ? 0 : (int)run->how.step;
    int last = run->every_step ? TC_STEP_COUNT - 1 : (int)run->how.step;
    for (int s = first; s <= last; s++) {
        run->how.step = (TcStep)s;
        if (!action(run, env, out)) return 1;
    }
    return 0;
}

static bool outcomes_action(Run *run, void *env, FILE *out) {
    (void)env;
    return run_one(run->program, &run->how, run->surface, out);
}

int tcalc_run_text(const char *text, size_t length, const char *syntax, const char *profile_text,
                   const char *version, FILE *out, FILE *err) {
    Run run;
    int status = run_open(&run, text, length, syntax, profile_text, version, err);
    if (status) return status;
    status = each_step(&run, outcomes_action, NULL, out);
    run_close(&run);
    return status;
}

/* ---- One step, and a query, from MeTTa (native/tcalc/tcalc_cetta_module.c) -- */

/* Rows read as a state: a text of rows alone, into the run's terms. */
static TcProgram *read_state(Run *run, const char *text, size_t length, FILE *err, int *status) {
    TcDeclarations d;
    TcSyntaxError e;
    TcProgram *state = tc_read_program(run->terms, text, length, &d, &e);
    if (!state) {
        if (e.line) fprintf(err, "error: the state, %zu:%zu: %s\n", e.line, e.column, e.message);
        else fprintf(err, "error: out of memory\n");
        *status = e.line ? 2 : 1;
        return NULL;
    }
    bool declared = d.has_version || d.has_step || d.has_ask || d.has_store || d.has_matches ||
                    d.has_backend || d.has_observe || d.has_schedule || d.has_budget || d.has_region;
    free(d.region);
    if (declared || tc_program_rules(state) > 0) {
        fprintf(err, "error: a state is rows alone, with no rule or declaration\n");
        tc_program_free(state);
        *status = 2;
        return NULL;
    }
    *status = 0;
    return state;
}

static bool step_action(Run *run, void *env, FILE *out) {
    const TcProgram *state = env;
    size_t n;
    const TcTerm *rows = tc_program_rows(state, &n);
    print_interpretation(run->program, &run->how, out);
    TcOutcomes *o = tc_successors(run->program, &run->how, rows, n);
    if (!o) return false;
    bool ok = true;
    size_t count = tc_outcomes_count(o);
    for (size_t i = 0; ok && i < count; i++) {
        size_t m;
        const TcTerm *next = tc_outcome_rows(o, i, &m);
        char *t = rows_text(run->terms, next, m, run->surface);
        if (!(ok = t != NULL)) break;
        fprintf(out, "(successor%s)\n", t);
        free(t);
    }
    for (size_t i = 0; ok && i < tc_outcomes_ambiguous(o); i++) {
        size_t m, matches;
        const TcTerm *at = tc_ambiguous_rows(o, i, &m, &matches);
        char *t = rows_text(run->terms, at, m, run->surface);
        if (!(ok = t != NULL)) break;
        fprintf(out, "(ambiguous (matches %zu)%s)\n", matches, t);
        free(t);
    }
    if (ok) fprintf(out, "(stepped (successors %zu) (finished %s)%s)\n", count,
                    tc_outcomes_finished(o) ? "true" : "false",
                    tc_outcomes_shortcuts_disagreed(o) ? " (shortcuts disagreed)" : "");
    ok = ok && tc_outcomes_complete_memory(o);
    tc_outcomes_free(o);
    return ok;
}

int tcalc_step_text(const char *program, size_t length, const char *state, size_t state_length,
                    const char *profile, FILE *out, FILE *err) {
    Run run;
    int status = run_open(&run, program, length, "tcalc", profile, NULL, err);
    if (status) return status;
    TcProgram *rows = read_state(&run, state, state_length, err, &status);
    if (rows) {
        status = each_step(&run, step_action, rows, out);
        tc_program_free(rows);
    }
    run_close(&run);
    return status;
}

/* query: in each outcome, the rows a read of the pattern can select.  A row
 * is selected when the rule (read PATTERN) has a step from the state of that
 * row alone: the pattern and the row unify. */
static bool query_action(Run *run, void *env, FILE *out) {
    TcProgram *reader = env;
    print_interpretation(run->program, &run->how, out);
    TcOutcomes *o = tc_explore(run->program, &run->how);
    if (!o) return false;
    TcInterpretation one = {.step = TC_STEP_ONE, .ask = run->how.ask, .store = TC_STORE_MULTISET,
                            .matches = TC_MATCHES_SYMMETRIC, .budget = 1, .backend = TC_BACKEND_C,
                            .observe = TC_OBSERVE_OUTCOMES, .shortcuts = run->how.shortcuts};
    size_t n = tc_outcomes_count(o);
    char **line = calloc(n ? n : 1, sizeof *line);
    bool ok = line != NULL;
    for (size_t i = 0; ok && i < n; i++) {   /* each outcome: its rows, then those selected */
        size_t m;
        const TcTerm *rows = tc_outcome_rows(o, i, &m);
        TcTerm *chosen = malloc((m ? m : 1) * sizeof *chosen);
        size_t k = 0;
        ok = chosen != NULL;
        for (size_t j = 0; ok && j < m; j++) {
            TcOutcomes *s = tc_successors(reader, &one, &rows[j], 1);
            ok = s != NULL && tc_outcomes_complete_memory(s);
            if (ok && tc_outcomes_count(s) > 0) chosen[k++] = rows[j];
            tc_outcomes_free(s);
        }
        char *all = ok ? rows_text(run->terms, rows, m, run->surface) : NULL;
        char *picked = all ? rows_text(run->terms, chosen, k, run->surface) : NULL;
        if (picked) {
            size_t size = strlen(all) + strlen(picked) + 32;
            if ((line[i] = malloc(size)))
                snprintf(line[i], size, "(answers (outcome%s)%s)", all, picked);
        }
        ok = line[i] != NULL;
        free(all); free(picked); free(chosen);
    }
    if (ok) {
        qsort(line, n, sizeof *line, text_order);   /* the order outcomes print in */
        for (size_t i = 0; i < n; i++) fprintf(out, "%s\n", line[i]);
        fprintf(out, "(explored (states %" PRIu64 ") (work %" PRIu64 ") (finished %s) (cycle %s))\n",
                tc_outcomes_states(o), tc_outcomes_work(o),
                tc_outcomes_finished(o) ? "true" : "false", tc_outcomes_cyclic(o) ? "true" : "false");
    }
    for (size_t i = 0; line && i < n; i++) free(line[i]);
    free(line);
    ok = ok && tc_outcomes_complete_memory(o);
    tc_outcomes_free(o);
    return ok;
}

int tcalc_query_text(const char *program, size_t length, const char *pattern, size_t pattern_length,
                     const char *profile, FILE *out, FILE *err) {
    Run run;
    int status = run_open(&run, program, length, "tcalc", profile, NULL, err);
    if (status) return status;
    /* the reading rule, in the run's terms: its unknowns are the pattern's */
    const char *head = "(rule tcalc-query (read ", *tail = "))";
    size_t size = strlen(head) + pattern_length + strlen(tail) + 1;
    char *text = malloc(size);
    TcProgram *reader = NULL;
    if (!text) status = 1;
    else {
        snprintf(text, size, "%s%.*s%s", head, (int)pattern_length, pattern, tail);
        TcDeclarations d;
        TcSyntaxError e;
        reader = tc_read_program(run.terms, text, strlen(text), &d, &e);
        if (reader) free(d.region);
        else if (e.line) { fprintf(err, "error: the pattern: %s\n", e.message); status = 2; }
        else status = 1;
        free(text);
    }
    if (reader) {
        status = each_step(&run, query_action, reader, out);
        tc_program_free(reader);
    }
    run_close(&run);
    return status;
}
/* The whole of a file; NULL, with *status set, when it cannot be read. */
static char *read_file(const char *path, size_t *length, int *status) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "error: cannot open %s\n", path); *status = 2; return NULL; }
    char *text = NULL;
    size_t cap = 0;
    *length = 0;
    for (;;) {
        if (*length == cap) {
            cap = cap ? cap * 2 : 65536;
            char *g = realloc(text, cap);
            if (!g) { free(text); fclose(f); *status = 1; return NULL; }
            text = g;
        }
        size_t got = fread(text + *length, 1, cap - *length, f);
        *length += got;
        if (got == 0) break;
    }
    bool bad = ferror(f) != 0;
    fclose(f);
    if (bad) { free(text); fprintf(stderr, "error: cannot read %s\n", path); *status = 2; return NULL; }
    return text;
}

int cetta_tcalc_run_file(const char *path, const char *profile, const char *version,
                         const char *syntax) {
    size_t length;
    int status;
    char *text = read_file(path, &length, &status);
    if (!text) return status;
    if (!syntax) syntax = tc_surface_name(tc_surface_of_path(path));
    status = tcalc_run_text(text, length, syntax, profile, version, stdout, stderr);
    free(text);
    return status;
}

/* ---- Printing join plans ---------------------------------------------------
 * The words of tc_program_plans (tcalc_core.h), read as any backend reads
 * them: the indexes, then each rule's plans, an op a line. */
static void plan_print_path(uint32_t path, FILE *out) {
    fprintf(out, "%u", path & 1023u);
    if (path >> 10 & 1023u) fprintf(out, ".%u", (path >> 10 & 1023u) - 1);
    if (path >> 20) fprintf(out, ".%u", (path >> 20) - 1);
}

static void plan_print_relation(const TcTerms *t, uint32_t head, uint32_t length, FILE *out) {
    fputs("(relation ", out);
    if (head) tc_print(t, head, out); else fputs("()", out);
    fprintf(out, " %u)", length);
}

static void plan_print_probe(const TcTerms *t, const uint32_t *w, uint32_t probe, FILE *out) {
    uint32_t head = w[probe + 1], index = w[probe + 3], level = w[probe + 4];
    if (head == TC_PLAN_OPEN) { fputs(" (open)", out); return; }
    fputc(' ', out);
    plan_print_relation(t, head, w[probe + 2], out);
    if (index == TC_PLAN_NONE) { fputs(" (rows)", out); return; }
    fprintf(out, " (index %u %u) (keys", index, level);
    for (uint32_t l = 0; l < level; l++) { fputc(' ', out); tc_print(t, w[probe + 5 + l], out); }
    fputc(')', out);
}

static void plans_print(TcProgram *program, const uint32_t *w, FILE *out) {
    static const char *const names[] = {"probe", "unify", "check", "absent", "tell", "take", "make"};
    const TcTerms *t = tc_program_terms(program);
    uint32_t at = w[1], rules = w[0];
    for (uint32_t c = 0; c < w[at]; c++) {
        uint32_t index = w[at + 1 + c];
        fprintf(out, "(index %u ", c);
        plan_print_relation(t, w[index], w[index + 1], out);
        fputs(" (paths", out);
        for (uint32_t l = 0; l < w[index + 2]; l++) { fputc(' ', out); plan_print_path(w[index + 5 + l], out); }
        fputc(')', out);
        uint32_t domain = w[index + 3];
        if (domain) {
            fputs(" (domain", out);
            for (uint32_t i = 0; i < w[domain]; i++) { fputc(' ', out); tc_print(t, w[domain + 1 + i], out); }
            fputc(')', out);
        }
        fputs(")\n", out);
    }
    for (uint32_t r = 0; r < rules; r++) {
        fprintf(out, "(plans %s", tc_program_rule_name(program, r));
        uint32_t record = w[2 + r];
        if (!record) { fputs(" (as an earlier rule))\n", out); continue; }
        for (uint32_t e = 0; e < w[record]; e++) {
            if (e + 1 < w[record]) fprintf(out, "\n  (enter %u", e); else fputs("\n  (every", out);
            uint32_t plan = w[record + 1 + e];
            for (uint32_t k = 0; k < w[plan]; k++) {
                const uint32_t *op = w + plan + 1 + 4 * k;
                fprintf(out, "\n    (%s %u", names[op[0]], op[1]);
                if (op[0] == TC_PLAN_PROBE || op[0] == TC_PLAN_ABSENT) plan_print_probe(t, w, op[2], out);
                fputc(')', out);
            }
            fputc(')', out);
        }
        fputs(")\n", out);
    }
}

int cetta_tcalc_plans_file(const char *path, const char *syntax) {
    size_t length;
    int status;
    char *text = read_file(path, &length, &status);
    if (!text) return status;
    if (!syntax) syntax = tc_surface_name(tc_surface_of_path(path));
    Run run;
    status = run_open(&run, text, length, syntax, NULL, NULL, stderr);
    free(text);
    if (status) return status;
    size_t words = 0;
    const uint32_t *plans = tc_program_plans(run.program, &words);
    status = words ? 0 : 1;
    if (words) plans_print(run.program, plans, stdout);
    else fprintf(stderr, "error: out of memory\n");
    run_close(&run);
    return status;
}

int cetta_tcalc_translate_file(const char *path, const char *from, const char *to) {
    TcSurface source, target;
    if (!from) from = tc_surface_name(tc_surface_of_path(path));
    if (!tc_surface_from_name(from, &source) || !to || !tc_surface_from_name(to, &target)) {
        fprintf(stderr, "error: tcalc reads the syntaxes tcalc, rho and chr\n");
        return 2;
    }
    size_t length;
    int status;
    char *text = read_file(path, &length, &status);
    if (!text) return status;
    TcCanonical c = {0};
    TcSyntaxError e;
    if (source != TC_SURFACE_TCALC) {   /* first into the canonical form */
        bool ok = tc_surface_translate(source, text, length, &c, &e);
        free(text);
        if (!ok) {
            if (e.line) fprintf(stderr, "error: %zu:%zu: %s\n", e.line, e.column, e.message);
            else fprintf(stderr, "error: out of memory\n");
            return e.line ? 2 : 1;
        }
        text = c.text;
        length = c.length;
        c.text = NULL;
    }
    status = 0;
    if (target == TC_SURFACE_TCALC) {
        fwrite(text, 1, length, stdout);
    } else if (!tc_surface_write(target, text, length, stdout, &e)) {
        size_t line = e.line && source != TC_SURFACE_TCALC && e.line <= c.lines
                      ? c.source_line[e.line - 1] : e.line;
        if (e.line) fprintf(stderr, "error: %zu: %s\n", line, e.message);
        else fprintf(stderr, "error: out of memory\n");
        status = e.line ? 2 : 1;
    }
    free(text);
    tc_canonical_free(&c);
    return status;
}

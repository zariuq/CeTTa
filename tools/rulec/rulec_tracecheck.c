/* rulec_tracecheck.c - checks that a trace written by compiled procedures is a derivation of the
 * module's rules, read from the module's core rows (independent of rulec and of the generated code).
 *
 *   rulec_tracecheck WIRE ENTRY SAFEPOINT [--native J]... [--law J]... [--many] TRACE...
 *
 * A trace is a sequence of 16-bit events (little endian): a rule id; 0xE000+j a constant judgment j
 * (its value was computed once); 0xF000+k native operation k (k-th --native); 0xFFFC a query of
 * a relation found no row; 0xFFFD no rule applies; 0xFFFE a premise target did not match; 0xFFFF
 * the safepoint (the start of a record);
 * 0xF800+k the k-th --law (a reflexivity law answered J for two identical arguments); 0xE800+j a
 * memoised judgment j answered from its kept results (its derivation was recorded when the result
 * was computed).
 * A derivation of judgment J is
 *     rule r with head J, then a derivation of each premise's judgment in r's written order (a
 *     premise that queries a relation has none)
 *   | native operation k replacing J | constant J
 * and a failure (no rule, a target that does not match, a constant that failed) ends the whole
 * trace, since a deterministic judgment whose premise fails has no other rule.  The safepoint event
 * precedes each derivation of SAFEPOINT.  The trace of a run is one derivation of ENTRY; with --many a
 * file holds the traces of several runs of ENTRY one after another (each complete or failed).
 * The check is by judgment: it does not recompute terms.
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void die(const char *f, ...) { va_list a; va_start(a, f); fputs("rulec_tracecheck: ", stderr); vfprintf(stderr, f, a); fputc('\n', stderr); va_end(a); exit(2); }

/* the wire: constructor names in order; for each rule its head and its premises' judgments */
#define MAXC 4096
static char *cname[MAXC]; static int nc;
static int rhead[MAXC], rnp[MAXC], *rcall[MAXC]; static char *rlabel[MAXC]; static int nr;
/* the rules as read: premise sources (NULL for a relation query) and right sides, as text */
static char **rpremise[MAXC], *rrhs[MAXC]; static int rnprem[MAXC];
static bool judgment[MAXC];
static int ctor(const char *s) { for (int i = 0; i < nc; i++) if (!strcmp(cname[i], s)) return i; return -1; }

/* tiny scanner for the rows: we need GrammarRuleV2 names, and for ScopedRewriteRuleV1 the head of
 * the left side and the head of each premise source */
static const char *skip_ws(const char *p) { while (isspace((unsigned char)*p)) p++; return p; }
static const char *skip_form(const char *p) {
    p = skip_ws(p);
    if (*p == '"') { p++; while (*p != '"') { if (*p == '\\') p++; p++; } return p + 1; }
    if (*p == '(') { int d = 0; do { if (*p == '"') { p++; while (*p != '"') { if (*p == '\\') p++; p++; } } else if (*p == '(') d++; else if (*p == ')') d--; p++; } while (d); return p; }
    while (*p && !isspace((unsigned char)*p) && *p != ')') p++;
    return p;
}
static char *read_str(const char **pp) {
    const char *p = skip_ws(*pp);
    if (*p != '"') die("expected a string");
    p++;
    const char *q = p;
    while (*q != '"') { if (*q == '\\') q++; q++; }
    *pp = q + 1;
    return strndup(p, (size_t)(q - p));
}
/* (PApp "name" ...) -> name */
static char *papp_head(const char *p) {
    p = skip_ws(p);
    if (strncmp(p, "(PApp ", 6)) return NULL;
    p += 6;
    return read_str(&p);
}

/* the calls of a pattern in post-order (arguments left to right, then the application itself when it
 * is a call; for a premise source the root is a call by construction) */
static void nested_calls(const char *p, bool root_may_be_data, int *calls, int *n) {
    p = skip_ws(p);
    if (strncmp(p, "(PApp ", 6)) return;                  /* a variable */
    char *h = papp_head(p);
    p = skip_ws(p + 6);
    p = skip_form(p);                                      /* the name */
    p = skip_ws(p);
    while (!strncmp(p, "(LCons ", 7)) {
        p = skip_ws(p + 7);
        nested_calls(p, true, calls, n);
        p = skip_ws(skip_form(p));
    }
    int c = ctor(h);
    free(h);
    if (c < 0) die("unknown constructor in a rule");
    if (!root_may_be_data || judgment[c]) {
        if (*n == 512) die("more than 512 calls in a rule");
        calls[(*n)++] = c;
    }
}

int main(int argc, char **argv) {
    if (argc < 5) die("usage: rulec_tracecheck WIRE ENTRY SAFEPOINT [--native J]... TRACE...");
    FILE *f = fopen(argv[1], "r");
    if (!f) die("cannot open %s", argv[1]);
    char *line = NULL; size_t cap = 0;
    while (getline(&line, &cap, f) > 0) {
        const char *p = line;
        if (!strncmp(p, "(GrammarRuleV2 ", 15)) {
            /* a constructor repeated identically (joined theories share it) is one constructor */
            p += 15;
            char *nm = read_str(&p);
            if (ctor(nm) >= 0) { free(nm); continue; }
            cname[nc++] = nm;
        }
        else if (!strncmp(p, "(ScopedRewriteRuleV1 ", 21)) {
            p += 21;
            if (nr == MAXC) die("too many rules");
            rlabel[nr] = read_str(&p);
            p = skip_form(p);                              /* variables */
            p = skip_ws(p);
            /* premises: (LCons PREMISE REST) ... LNil; kept as text until the judgments are known */
            int n = 0, pcap = 0;
            const char *q = p;
            rpremise[nr] = NULL;
            while (!strncmp(skip_ws(q), "(LCons ", 7)) {
                q = skip_ws(q) + 7;
                q = skip_ws(q);
                if (n == pcap) {
                    pcap = pcap ? 2 * pcap : 8;
                    rpremise[nr] = realloc(rpremise[nr], (size_t)pcap * sizeof *rpremise[nr]);
                    if (!rpremise[nr]) die("out of memory");
                }
                if (!strncmp(q, "(RelationQuery ", 15)) { rpremise[nr][n++] = NULL; q = skip_form(q); continue; }
                if (strncmp(q, "(Congruence ", 12)) die("unexpected premise form");
                const char *src = skip_ws(q + 12);
                rpremise[nr][n++] = strndup(src, (size_t)(skip_form(src) - src));
                q = skip_form(q);                          /* the Congruence */
            }
            rnprem[nr] = n;
            p = skip_form(p);                              /* the premise list */
            char *lh = papp_head(p);
            if (!lh) die("left side is not an application");
            rhead[nr] = ctor(lh);
            free(lh);
            p = skip_form(p);                              /* the left side */
            const char *rhs = skip_ws(p);
            rrhs[nr] = strndup(rhs, (size_t)(skip_form(rhs) - rhs));
            nr++;
        }
    }
    fclose(f);
    /* each rule's derivation: its premises' derivations in order, where a call nested in a premise's
     * arguments or in the right side stands for its value, made before (call by value, innermost first,
     * left to right; the right side's calls after every premise) */
    for (int r = 0; r < nr; r++) judgment[rhead[r]] = true;
    for (int r = 0; r < nr; r++) {
        int calls[512], n = 0;
        for (int j = 0; j < rnprem[r]; j++) {
            if (!rpremise[r][j]) continue;                 /* a query of a relation (host rows) has no derivation */
            nested_calls(rpremise[r][j], false, calls, &n);
        }
        nested_calls(rrhs[r], true, calls, &n);
        rnp[r] = n;
        rcall[r] = malloc((size_t)(n ? n : 1) * sizeof(int));
        memcpy(rcall[r], calls, (size_t)n * sizeof(int));
    }
    int entry = ctor(argv[2]), safepoint = ctor(argv[3]);
    if (entry < 0 || safepoint < 0) die("unknown entry or safepoint");
    int native[64], nnative = 0, law[64], nlaw = 0, first_trace = 4;
    bool many = false;
    for (int i = 4; i < argc; i++) {
        if (!strcmp(argv[i], "--native") && i + 1 < argc) { native[nnative] = ctor(argv[++i]); if (native[nnative++] < 0) die("unknown judgment %s", argv[i]); first_trace = i + 1; }
        else if (!strcmp(argv[i], "--law") && i + 1 < argc) { law[nlaw] = ctor(argv[++i]); if (law[nlaw++] < 0) die("unknown judgment %s", argv[i]); first_trace = i + 1; }
        else if (!strcmp(argv[i], "--many")) { many = true; first_trace = i + 1; }
        else break;
    }
    long bad = 0, files = 0, total_events = 0, total_rules = 0, total_records = 0, failures = 0, total_runs = 0;
    for (int fi = first_trace; fi < argc; fi++) {
        FILE *t = fopen(argv[fi], "rb");
        if (!t) die("cannot open %s", argv[fi]);
        files++;
        /* stack of (rule, next premise) frames */
        size_t scap = 1 << 16, sp = 0;
        int *sr = malloc(scap * sizeof(int)), *sk = malloc(scap * sizeof(int));
        int expect = entry;           /* judgment whose derivation comes next; -1 when complete */
        bool failed = false, done = false;
        long runs = 0;
        long events = 0, err = 0;
        uint16_t buf[1 << 15];
        size_t got;
        while ((got = fread(buf, sizeof(uint16_t), sizeof buf / sizeof buf[0], t)) > 0) {
            for (size_t i = 0; i < got && !err; i++) {
                uint16_t e = buf[i];
                events++;
                if ((done || failed) && many) { runs++; done = failed = false; sp = 0; expect = entry; }
                if (done || failed) { err = 1; fprintf(stderr, "%s: event %ld after the end of the derivation\n", argv[fi], events); break; }
                if (e == 0xFFFFu) { if (expect != safepoint) { err = 1; fprintf(stderr, "%s: event %ld: safepoint where %s is expected\n", argv[fi], events, cname[expect]); } total_records++; continue; }
                if (e == 0xFFFCu || e == 0xFFFDu || e == 0xFFFEu) { failed = true; failures++; continue; }
                bool complete = false;
                if (e >= 0xF800u) {
                    unsigned k = e - 0xF800u;
                    if (k >= (unsigned)nlaw || law[k] != expect) { err = 1; fprintf(stderr, "%s: event %ld: law %u where %s is expected\n", argv[fi], events, k, cname[expect]); break; }
                    complete = true;
                } else if (e >= 0xF000u) {
                    unsigned k = e - 0xF000u;
                    if (k >= (unsigned)nnative || native[k] != expect) { err = 1; fprintf(stderr, "%s: event %ld: native operation %u where %s is expected\n", argv[fi], events, k, cname[expect]); break; }
                    complete = true;
                } else if (e >= 0xE800u) {
                    unsigned j = e - 0xE800u;
                    if ((int)j != expect) { err = 1; fprintf(stderr, "%s: event %ld: kept result of %u where %s is expected\n", argv[fi], events, j, cname[expect]); break; }
                    complete = true;
                } else if (e >= 0xE000u) {
                    unsigned j = e - 0xE000u;
                    if ((int)j != expect) { err = 1; fprintf(stderr, "%s: event %ld: constant %u where %s is expected\n", argv[fi], events, j, cname[expect]); break; }
                    complete = true;
                } else {
                    if (e >= nr || rhead[e] != expect) { err = 1; fprintf(stderr, "%s: event %ld: rule %u (%s) where %s is expected\n", argv[fi], events, e, e < nr ? rlabel[e] : "?", cname[expect]); break; }
                    total_rules++;
                    if (rnp[e] == 0) complete = true;
                    else {
                        if (sp == scap) { scap *= 2; sr = realloc(sr, scap * sizeof(int)); sk = realloc(sk, scap * sizeof(int)); }
                        sr[sp] = e; sk[sp] = 0; sp++;
                        expect = rcall[e][0];
                    }
                }
                while (complete) {
                    if (sp == 0) { done = true; break; }
                    int r = sr[sp - 1];
                    if (++sk[sp - 1] < rnp[r]) { expect = rcall[r][sk[sp - 1]]; break; }
                    sp--;
                }
            }
            if (err) break;
        }
        fclose(t);
        if (!err && !done && !failed) { err = 1; fprintf(stderr, "%s: the trace ends inside the derivation (%zu open rules)\n", argv[fi], sp); }
        if (err) bad++;
        total_events += events;
        total_runs += runs + (events ? 1 : 0);
        free(sr); free(sk);
    }
    printf("rulec_tracecheck: %ld traces (%ld runs), %ld events, %ld rule applications, %ld safepoints, %ld failures: %ld traces not a derivation\n",
           files, total_runs, total_events, total_rules, total_records, failures, bad);
    return bad ? 1 : 0;
}

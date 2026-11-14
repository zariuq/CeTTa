/* rulec_tracecheck.c - checks that a trace written by compiled procedures is a derivation of the
 * module's rules, read from the module's core rows (independent of rulec and of the generated code).
 *
 *   rulec_tracecheck WIRE ENTRY SAFEPOINT [--native J]... [--law J]... TRACE...
 *
 * A trace is a sequence of 16-bit events (little endian): a rule id; 0xE000+j a constant judgment j
 * (its value was computed once); 0xF000+k native operation k (k-th --native); 0xFFFD no rule
 * applies; 0xFFFE a premise target did not match; 0xFFFF the safepoint (the start of a record);
 * 0xF800+k the k-th --law (a reflexivity law answered J for two identical arguments).
 * A derivation of judgment J is
 *     rule r with head J, then a derivation of each premise's judgment in r's written order
 *   | native operation k replacing J | constant J
 * and a failure (no rule, a target that does not match, a constant that failed) ends the whole
 * trace, since a deterministic judgment whose premise fails has no other rule.  The safepoint event
 * precedes each derivation of SAFEPOINT.  The trace of a run is one derivation of ENTRY.
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

int main(int argc, char **argv) {
    if (argc < 5) die("usage: rulec_tracecheck WIRE ENTRY SAFEPOINT [--native J]... TRACE...");
    FILE *f = fopen(argv[1], "r");
    if (!f) die("cannot open %s", argv[1]);
    char *line = NULL; size_t cap = 0;
    while (getline(&line, &cap, f) > 0) {
        const char *p = line;
        if (!strncmp(p, "(GrammarRuleV2 ", 15)) { p += 15; cname[nc++] = read_str(&p); }
        else if (!strncmp(p, "(ScopedRewriteRuleV1 ", 21)) {
            p += 21;
            rlabel[nr] = read_str(&p);
            p = skip_form(p);                              /* variables */
            p = skip_ws(p);
            /* premises: (LCons (Congruence SRC TGT) REST) ... LNil */
            int calls[64], n = 0;
            const char *q = p;
            while (!strncmp(skip_ws(q), "(LCons ", 7)) {
                q = skip_ws(q) + 7;
                q = skip_ws(q);
                if (strncmp(q, "(Congruence ", 12)) die("unexpected premise form");
                char *h = papp_head(q + 12);
                if (!h) die("premise source is not an application");
                calls[n++] = ctor(h);
                free(h);
                q = skip_form(q);                          /* the Congruence */
            }
            p = skip_form(p);                              /* the premise list */
            char *lh = papp_head(p);
            if (!lh) die("left side is not an application");
            rhead[nr] = ctor(lh);
            free(lh);
            rnp[nr] = n;
            rcall[nr] = malloc((size_t)(n ? n : 1) * sizeof(int));
            memcpy(rcall[nr], calls, (size_t)n * sizeof(int));
            nr++;
        }
    }
    fclose(f);
    int entry = ctor(argv[2]), safepoint = ctor(argv[3]);
    if (entry < 0 || safepoint < 0) die("unknown entry or safepoint");
    int native[64], nnative = 0, law[64], nlaw = 0, first_trace = 4;
    for (int i = 4; i < argc; i++) {
        if (!strcmp(argv[i], "--native") && i + 1 < argc) { native[nnative] = ctor(argv[++i]); if (native[nnative++] < 0) die("unknown judgment %s", argv[i]); first_trace = i + 1; }
        else if (!strcmp(argv[i], "--law") && i + 1 < argc) { law[nlaw] = ctor(argv[++i]); if (law[nlaw++] < 0) die("unknown judgment %s", argv[i]); first_trace = i + 1; }
        else break;
    }
    long bad = 0, files = 0, total_events = 0, total_rules = 0, total_records = 0, failures = 0;
    for (int fi = first_trace; fi < argc; fi++) {
        FILE *t = fopen(argv[fi], "rb");
        if (!t) die("cannot open %s", argv[fi]);
        files++;
        /* stack of (rule, next premise) frames */
        size_t scap = 1 << 16, sp = 0;
        int *sr = malloc(scap * sizeof(int)), *sk = malloc(scap * sizeof(int));
        int expect = entry;           /* judgment whose derivation comes next; -1 when complete */
        bool failed = false, done = false;
        long events = 0, err = 0;
        uint16_t buf[1 << 15];
        size_t got;
        while ((got = fread(buf, sizeof(uint16_t), sizeof buf / sizeof buf[0], t)) > 0) {
            for (size_t i = 0; i < got && !err; i++) {
                uint16_t e = buf[i];
                events++;
                if (done || failed) { err = 1; fprintf(stderr, "%s: event %ld after the end of the derivation\n", argv[fi], events); break; }
                if (e == 0xFFFFu) { if (expect != safepoint) { err = 1; fprintf(stderr, "%s: event %ld: safepoint where %s is expected\n", argv[fi], events, cname[expect]); } total_records++; continue; }
                if (e == 0xFFFDu || e == 0xFFFEu) { failed = true; failures++; continue; }
                bool complete = false;
                if (e >= 0xF800u) {
                    unsigned k = e - 0xF800u;
                    if (k >= (unsigned)nlaw || law[k] != expect) { err = 1; fprintf(stderr, "%s: event %ld: law %u where %s is expected\n", argv[fi], events, k, cname[expect]); break; }
                    complete = true;
                } else if (e >= 0xF000u) {
                    unsigned k = e - 0xF000u;
                    if (k >= (unsigned)nnative || native[k] != expect) { err = 1; fprintf(stderr, "%s: event %ld: native operation %u where %s is expected\n", argv[fi], events, k, cname[expect]); break; }
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
        free(sr); free(sk);
    }
    printf("rulec_tracecheck: %ld traces, %ld events, %ld rule applications, %ld safepoints, %ld failures: %ld traces not a derivation\n",
           files, total_events, total_rules, total_records, failures, bad);
    return bad ? 1 : 0;
}

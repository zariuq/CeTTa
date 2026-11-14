/* holm_cetta_module.c - the HOL Light kernel module, compiled by rulec, as a CeTTa native module.
 *
 * The module (a rule module: fusion.ml and the HOLR replay as rules) is the specification; the code
 * in generated/ is compiled from its rows by rulec and validated against them (decision DAGs by
 * rulec_validate, the corpus against CeTTa's native kernel).  CeTTa's own decoder reads the stream.
 *
 * Operations (lib/holmodule.metta):
 *   __cetta_lib_holm_check PATH POLICY      the module's verdict for the stream, as module data:
 *       (hl:accepted RECORDS THEOREMS) | (hl:refused STATUS REASON RECORDS) | (hl:malformed REASON
 *       RECORDS) | (hl:probe-refused FAILED); numerals as binary numerals, names as byte lists, types
 *       and terms in the module's constructors (inside the run they are interned nodes)
 *   __cetta_lib_holm_measure PATH POLICY    (holm:measure RECORDS DECODE-NS MODULE-NS YOUNG-BYTES
 *       PROMOTED-BYTES OLD-PEAK-BYTES) for the same run
 * POLICY: open | standard | sealed (the host's axiom policy).  A call that cannot run (an unreadable
 * stream, a bad argument, no verdict, a refinement violation) gives (holm:error "message").  Calls are
 * serialised; each runs on a thread with a large stack (the rules recurse on the depth of terms).  The
 * native kernel store is kept from call to call (a cache of interned values, collected after each
 * call), and freed after a stopped run.
 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "holm_cetta_module.h"

#include "holr_bridge.h"
#include "generated/hol_module_ids.h"

#include <pthread.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static pthread_mutex_t holm_gate = PTHREAD_MUTEX_INITIALIZER;
static bool holm_ready;

static uint64_t holm_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/* a refusal to run is a value, as hol:replay's (hol:error ...): (holm:error "message") */
static Atom *holm_error(Arena *a, Atom *head, const char *message) {
    (void)head;
    Atom *e[2] = {atom_symbol(a, "holm:error"), atom_string(a, message)};
    return atom_expr(a, e, 2);
}

static bool holm_text(Atom *x, const char **out) {
    if (x && x->kind == ATOM_GROUNDED && x->ground.gkind == GV_STRING) { *out = x->ground.sval; return true; }
    if (x && x->kind == ATOM_SYMBOL) { *out = atom_name_cstr(x); return true; }
    return false;
}

/* module data as CeTTa atoms, in the shape CeTTa prints them: (ctor kid ...), a nullary
 * constructor as (ctor); read through rc_view, which uses each sort's representation (interned
 * names, types and terms, immediate numerals, heap terms), so numerals come out as binary numerals
 * and names as byte lists, as the module writes them */
static Atom *holm_atom(Arena *a, T *t, int sort) {
    T *kids[8];
    int c = rc_view(sort, t, kids);
    if (c < 0) return atom_symbol(a, "holm:unreadable");
    uint32_t n = rc_ctor_arity[c];
    Atom *e[9];
    e[0] = atom_symbol(a, rc_ctor_name[c]);
    for (uint32_t i = 0; i < n && i < 8; i++) e[i + 1] = holm_atom(a, kids[i], rc_ctor_asort[rc_ctor_asort_off[c] + i]);
    return atom_expr(a, e, (CettaExprLen)(n + 1));
}

typedef struct {
    const uint8_t *bytes; size_t len; HolAxiomPolicy policy;
    T *verdict; uint64_t decode_ns, module_ns, young_bytes; size_t records;
    char violation[256];   /* a refinement violation stopped the run */
} HolmRun;

/* A refinement violation (the module built a value a native representation cannot hold) leaves the
 * run through holm_jump and becomes (holm:error ...), instead of stopping CeTTa. */
static jmp_buf holm_jump;
static HolmRun *holm_current;
static _Noreturn void holm_violation(const char *what) {
    snprintf(holm_current->violation, sizeof holm_current->violation, "refinement violation: %s", what);
    longjmp(holm_jump, 1);
}

static void *holm_thread(void *p) {
    HolmRun *r = p;
    holm_current = r;
    rc_fail_hook = holm_violation;
    if (setjmp(holm_jump)) {
        rc_fail_hook = NULL;
        r->verdict = NULL;
        holr_store_free();   /* the store is not reused after a stopped run */
        return NULL;
    }
    rc_runtime_reset();
    memset(&rc_stats, 0, sizeof rc_stats);
    uint64_t t0 = holm_now_ns();
    T *decoded = holr_bridge_decode(r->bytes, r->len);
    uint64_t t1 = holm_now_ns();
    r->verdict = holr_bridge_check(holr_bridge_policy(r->policy), decoded);
    uint64_t t2 = holm_now_ns();
    r->records = holr_bridge_records();
    r->decode_ns = t1 - t0;
    r->module_ns = t2 - t1;
    r->young_bytes = rc_alloc_total();
    rc_fail_hook = NULL;
    return NULL;
}

static uint8_t *holm_slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    size_t cap = 1 << 16, n = 0;
    uint8_t *b = malloc(cap);
    while (b) {
        if (n == cap) { uint8_t *g = realloc(b, cap * 2); if (!g) { free(b); b = NULL; break; } b = g; cap *= 2; }
        size_t got = fread(b + n, 1, cap - n, f);
        n += got;
        if (!got) break;
    }
    fclose(f);
    *len = n;
    return b;
}

Atom *cetta_holm_dispatch(struct CettaLibraryContext *ctx, Space *space, Arena *a, Atom *head,
                          Atom **args, uint32_t nargs) {
    (void)ctx; (void)space;
    static const char prefix[] = "__cetta_lib_holm_";
    if (!head || head->kind != ATOM_SYMBOL || strncmp(atom_name_cstr(head), prefix, sizeof prefix - 1)) return NULL;
    const char *op = atom_name_cstr(head) + sizeof prefix - 1;
    bool measure = !strcmp(op, "measure");
    if (!measure && strcmp(op, "check")) return NULL;
    const char *path, *pol;
    if (nargs != 2 || !holm_text(args[0], &path) || !holm_text(args[1], &pol))
        return holm_error(a, head, "expected a stream path and an axiom policy (open, standard or sealed)");
    HolAxiomPolicy policy;
    if (!strcmp(pol, "open")) policy = HOL_AXIOMS_OPEN;
    else if (!strcmp(pol, "standard")) policy = HOL_AXIOMS_STANDARD;
    else if (!strcmp(pol, "sealed")) policy = HOL_AXIOMS_SEALED;
    else return holm_error(a, head, "axiom policy is open, standard or sealed");
    size_t len = 0;
    uint8_t *bytes = holm_slurp(path, &len);
    if (!bytes) return holm_error(a, head, "cannot read the stream");
    pthread_mutex_lock(&holm_gate);
    if (!holm_ready) { rc_runtime_init(0); holr_bridge_init(); holm_ready = true; }
    HolmRun run = {bytes, len, policy, NULL, 0, 0, 0, 0, {0}};
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, (size_t)1 << 30);
    pthread_t th;
    Atom *result;
    if (pthread_create(&th, &at, holm_thread, &run)) {
        result = holm_error(a, head, "cannot start the module thread");
    } else {
        pthread_join(th, NULL);
        if (run.violation[0]) {
            result = holm_error(a, head, run.violation);
        } else if (measure) {
            Atom *e[7] = {atom_symbol(a, "holm:measure"), atom_int(a, (int64_t)run.records), atom_int(a, (int64_t)run.decode_ns),
                          atom_int(a, (int64_t)run.module_ns), atom_int(a, (int64_t)run.young_bytes),
                          atom_int(a, (int64_t)rc_stats.promoted_bytes), atom_int(a, (int64_t)rc_stats.old_live_peak)};
            result = atom_expr(a, e, 7);
        } else if (!run.verdict) {
            result = holm_error(a, head, "the module gives no verdict (a judgment failed)");
        } else {
            result = holm_atom(a, run.verdict, S_Verdict);
        }
    }
    pthread_attr_destroy(&at);
    holr_store_end();   /* the run's native store, after its values were read */
    rc_runtime_reset();
    pthread_mutex_unlock(&holm_gate);
    free(bytes);
    return result;
}

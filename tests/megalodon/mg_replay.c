/*
 * Replay a megalodon -sexprinfo export through the native kernel, without
 * OCaml, and optionally save the result as a CeTTa-native library and load
 * it back into a fresh theory.
 *
 *   mg_replay PROFILE EXPORT [--save LIB] [--reload] [--work LIMIT] [--reference]
 *
 * Axioms in the export are accepted (the replay is the importing
 * authority's explicit choice); the report counts them separately.
 * Prints one summary line per phase with wall time and peak RSS.
 */
#include "mg_core.h"
#include "mg_library.h"
#include "mg_profiles.h"
#include "mg_worker.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static long peak_kb(void) {
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_maxrss;
}

typedef struct {
    MgDeepCall call;
    MgTheory *theory;
    const char *path;
    bool library;
    uint64_t limit;
    MgImportReport report;
    MgStatus status;
} Run;

static void run_import(MgDeepCall *call) {
    Run *r = (Run *)call;
    MgImportPolicy policy = {.all_axioms = true};
    MgBudget budget;
    mg_budget_init(&budget, r->limit);
    pthread_mutex_lock(&r->theory->lock);
    r->status = r->library
        ? mg_library_load_file(r->theory, r->path, &policy, &budget, &r->report)
        : mg_import_sexprinfo_file(r->theory, r->path, &policy, &budget, &r->report);
    pthread_mutex_unlock(&r->theory->lock);
}

static void print_report(const char *phase, const MgImportReport *r, MgStatus st,
                         double seconds, const MgTheory *t) {
    printf("%s status=%s prims=%llu params=%llu defs=%llu axioms=%llu thms=%llu "
           "reproved=%llu repeated=%llu normalized=%llu realized=%llu identities=%llu unproved=%llu dependent=%llu unsupported=%llu rev=%llu nodes=%llu "
           "store_mb=%.1f time=%.3fs peak_rss_mb=%.1f\n",
           phase, mg_status_name(st),
           (unsigned long long)r->primitives, (unsigned long long)r->parameters,
           (unsigned long long)r->definitions, (unsigned long long)r->axioms,
           (unsigned long long)r->theorems, (unsigned long long)r->reproved,
           (unsigned long long)r->repeated,
           (unsigned long long)r->normalized, (unsigned long long)r->realized,
           (unsigned long long)r->identities,
           (unsigned long long)r->unproved, (unsigned long long)r->dependent,
           (unsigned long long)r->unsupported,
           (unsigned long long)r->revision, (unsigned long long)t->store.nodes,
           (double)t->store.bytes / 1048576.0, seconds, (double)peak_kb() / 1024.0);
    if (r->stopped)
        printf("  stopped at line %llu: %s\n", (unsigned long long)r->line, r->message);
}

static MgStatus replay(MgTheory *t, const char *path, bool library, uint64_t limit,
                       MgImportReport *out, double *seconds) {
    Run r;
    memset(&r, 0, sizeof(r));
    r.call.run = run_import;
    r.theory = t;
    r.path = path;
    r.library = library;
    r.limit = limit;
    double t0 = now();
    if (!mg_deep_call(&r.call, NULL, NULL)) {
        fprintf(stderr, "no worker\n");
        exit(2);
    }
    *seconds = now() - t0;
    *out = r.report;
    return r.status;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s PROFILE EXPORT [--save LIB] [--reload] [--work LIMIT]\n",
                argv[0]);
        return 2;
    }
    MgProfileTag tag;
    if (!mg_profile_from_name(argv[1], &tag)) {
        fprintf(stderr, "unknown profile %s\n", argv[1]);
        return 2;
    }
    const char *save = NULL;
    bool reload = false, reference = false;
    uint64_t limit = 0;
    for (int i = 3; i < argc; ++i) {
        if (strcmp(argv[i], "--save") == 0 && i + 1 < argc)
            save = argv[++i];
        else if (strcmp(argv[i], "--reload") == 0)
            reload = true;
        else if (strcmp(argv[i], "--reference") == 0)
            reference = true;
        else if (strcmp(argv[i], "--work") == 0 && i + 1 < argc)
            limit = strtoull(argv[++i], NULL, 10);
    }
    double t0 = now();
    MgStatus st;
    MgTheory *t = mg_theory_new(tag, "replay", &st);
    if (!t) {
        fprintf(stderr, "theory: %s\n", mg_status_name(st));
        return 2;
    }
    t->reference_conversion = reference;
    printf("startup time=%.6fs peak_rss_mb=%.1f conversion=%s\n", now() - t0,
           (double)peak_kb() / 1024.0, reference ? "reference" : "lazy");
    MgImportReport report;
    double seconds;
    st = replay(t, argv[2], false, limit, &report, &seconds);
    print_report("import", &report, st, seconds, t);
    int code = st == MG_OK ? 0 : 1;
    if (st == MG_OK && save) {
        const char *why = NULL;
        double s0 = now();
        pthread_mutex_lock(&t->lock);
        MgStatus sv = mg_library_save_file(t, save, &why);
        pthread_mutex_unlock(&t->lock);
        printf("save status=%s time=%.3fs%s%s\n", mg_status_name(sv), now() - s0,
               why ? " reason=" : "", why ? why : "");
        if (sv != MG_OK)
            code = 1;
        if (sv == MG_OK && reload) {
            MgTheory *fresh = mg_theory_new(tag, "reloaded", &st);
            fresh->reference_conversion = reference;
            MgImportReport lr;
            st = replay(fresh, save, true, 0, &lr, &seconds);
            print_report("reload", &lr, st, seconds, fresh);
            if (st != MG_OK || lr.theorems != report.theorems)
                code = 1;
            mg_theory_release(fresh);
        }
    }
    mg_theory_release(t);
    mg_deep_call_shutdown();
    return code;
}

/*
 * mg-check [-hf|-mizar|-hoas|-setmm] [-I SIGNATURE]... [--theorem NAME]...
 *          [--allow-axiom NAME]... [--allow-document-axioms] [--theorem-work UNITS]
 *          [--prove-aby [--aby-steps N] [--aby-depth N]] [--verbose] FILE
 * mg-check --help
 *
 * Checks a Megalodon document with the native kernel and says what is
 * verified.  A theorem is verified when the kernel checked its proof and
 * every axiom of the document that the proof rests on, directly or through
 * the theorems it cites, is approved.  The signatures (-I) are the theory:
 * their axioms need no approval.  A document axiom is approved by
 * --allow-axiom (its name or identity) or --allow-document-axioms.
 *
 * With --prove-aby, each aby step is first proved by search from its
 * goal's context and the facts it names (native/megalodon/mg_aby.c); a
 * proof found is the step's proof, checked by the kernel with the rest,
 * and the theorem can be verified.  The search takes at most --aby-steps
 * steps (default 200,000) and --aby-depth levels of iterative deepening
 * (default 8) for each step.
 *
 * Each theorem is checked within a work budget (--theorem-work, in the
 * kernel's work units; 0 for none).  A theorem whose checking uses it up
 * is undecided: not verified, and the document goes on.
 *
 * The report lists every theorem that is not verified (every theorem, with
 * the work its checking took, with --verbose) and every axiom the document
 * itself states, then a summary and the verdict.  Exit status: 0 when every theorem of the document, or
 * each --theorem, is verified; 1 when one is not; 2 when the document does
 * not go through (a syntax error, a failure, a kernel refusal, an
 * unreadable file); 64 for a usage error.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mg_core.h"
#include "mg_elab.h"
#include "mg_library.h"
#include "mg_run.h"

#define USAGE \
    "usage: mg-check [-hf|-mizar|-hoas|-setmm] [-I SIGNATURE]... [--theorem NAME]...\n" \
    "                [--allow-axiom NAME]... [--allow-document-axioms] [--theorem-work UNITS]\n" \
    "                [--prove-aby [--aby-steps N] [--aby-depth N]] [--verbose] FILE\n"

#define HELP \
    "mg-check: check a Megalodon document with a native kernel and say what is verified.\n" \
    "\n" \
    USAGE \
    "\n" \
    "The document is read, evaluated and proved as Megalodon does it (its items,\n" \
    "notations, tactics and megaauto), and the kernel admits every declaration and\n" \
    "checks every proof.  A theorem is verified when the kernel checked its proof\n" \
    "and every axiom of the document that the proof rests on, directly or through\n" \
    "the theorems it cites, is approved.  The signatures are the theory: their\n" \
    "axioms need no approval.\n" \
    "\n" \
    "Theory (as Megalodon's flags; Egal without one): -hf, -mizar, -hoas, -setmm.\n" \
    "\n" \
    "  -I SIGNATURE             read a signature first, as megalodon -I does\n" \
    "  --theorem NAME           the theorems the exit status follows (repeatable)\n" \
    "  --allow-axiom NAME       approve a document axiom, by name or identity\n" \
    "                           (repeatable)\n" \
    "  --allow-document-axioms  approve every axiom the document states\n" \
    "  --theorem-work UNITS     each theorem's budget, in kernel work units\n" \
    "                           (default 1000000000; 0 for none); a theorem whose\n" \
    "                           checking uses it up is undecided, and the document\n" \
    "                           goes on\n" \
    "  --prove-aby              prove each aby step by search, from its goal's\n" \
    "                           context and the facts it names; a proof found is\n" \
    "                           checked by the kernel with the rest\n" \
    "  --aby-steps N            search steps for each aby step (default 200000)\n" \
    "  --aby-depth N            levels of iterative deepening for each aby step\n" \
    "                           (default 8, at most 64)\n" \
    "  --verbose                list every theorem, with its identity and the work\n" \
    "                           its checking took\n" \
    "  --help                   print this help\n" \
    "\n" \
    "The report lists every theorem that is not verified, with its verdict:\n" \
    "checked, assuming document axioms; not checked, as it cites theorems without\n" \
    "proof; admitted by the document; left to automation (aby); unfinished at the\n" \
    "end of the document; or not decided, as it used up its work budget.  Then\n" \
    "every axiom the document itself states, a summary and the verdict.\n" \
    "\n" \
    "Exit status: 0 when every theorem of the document, or each --theorem, is\n" \
    "verified; 1 when one is not; 2 when the document does not go through (a\n" \
    "syntax error, a failure, a kernel refusal, an unreadable file); 64 for a\n" \
    "usage error.\n"

/* The default budget of one theorem, in work units: far above what any
 * theorem of the example corpus takes (tests/megalodon/corpus_verdicts.py
 * --work reports the largest). */
#ifndef MG_CHECK_THEOREM_WORK_DEFAULT
#define MG_CHECK_THEOREM_WORK_DEFAULT 1000000000
#endif
#define MG_CHECK_THEOREM_WORK ((uint64_t)(MG_CHECK_THEOREM_WORK_DEFAULT))

typedef struct {
    const char **v;
    int len;
} Names;

static bool names_has(const Names *n, const char *name, const char *hash) {
    for (int i = 0; i < n->len; ++i)
        if (strcmp(n->v[i], name) == 0 || (hash && strcmp(n->v[i], hash) == 0))
            return true;
    return false;
}

static const char *theory_name(MgProfileTag t) {
    switch (t) {
    case MG_PROFILE_HF: return "hf";
    case MG_PROFILE_MIZAR: return "mizar";
    case MG_PROFILE_HOAS: return "hoas";
    case MG_PROFILE_SETMM: return "setmm";
    default: return "egal";
    }
}

static const char *id_text(MgTheory *t, uint32_t id) {
    const MgName *n = mg_name_get(&t->store, id);
    return n ? n->text : "";
}

static bool checked(const MgLedgerEntry *x) {
    return x->verdict == MG_VERDICT_CHECKED || x->verdict == MG_VERDICT_REPROVED;
}

typedef struct {
    const MgLedger *ledger;
    MgTheory *theory;
    const Names *allowed;
    bool allow_all;
    uint64_t work;             /* each theorem's budget */
} Ctx;

static bool axiom_approved(const Ctx *c, uint32_t pos) {
    if (c->allow_all)
        return true;
    MgLedgerEntry a;
    mg_ledger_get(c->ledger, pos, &a);
    return a.origin == MG_ORIGIN_SIGNATURE || names_has(c->allowed, a.name, id_text(c->theory, a.id));
}

static bool verified(const Ctx *c, const MgLedgerEntry *x) {
    if (!checked(x))
        return false;
    for (uint32_t k = 0; k < x->axiom_count; ++k)
        if (!axiom_approved(c, x->axioms[k]))
            return false;
    return true;
}

static void print_names(const Ctx *c, const uint32_t *pos, uint32_t n, bool only_unapproved) {
    bool first = true;
    for (uint32_t k = 0; k < n; ++k) {
        if (only_unapproved && axiom_approved(c, pos[k]))
            continue;
        MgLedgerEntry a;
        mg_ledger_get(c->ledger, pos[k], &a);
        printf("%s%s", first ? "" : ", ", a.name);
        first = false;
    }
}

static void print_theorem(const Ctx *c, const MgLedgerEntry *x, bool verbose) {
    printf("theorem %s line %llu: ", x->name, (unsigned long long)x->line);
    switch (x->verdict) {
    case MG_VERDICT_CHECKED:
    case MG_VERDICT_REPROVED:
        if (verified(c, x)) {
            printf("verified");
        } else {
            printf("checked, assuming document axioms ");
            print_names(c, x->axioms, x->axiom_count, true);
        }
        break;
    case MG_VERDICT_DEPENDENT:
        printf("not checked: it cites theorems without proof: ");
        print_names(c, x->unproved, x->unproved_count, false);
        break;
    case MG_VERDICT_ADMITTED: printf("admitted by the document"); break;
    case MG_VERDICT_ABY: printf("left to automation (aby)"); break;
    case MG_VERDICT_PENDING: printf("unfinished at the end of the document"); break;
    case MG_VERDICT_EXHAUSTED:
        printf("not decided: checking it used up its work budget of %llu units", (unsigned long long)c->work);
        break;
    }
    if (verbose)
        printf(" (id %s, %llu work units)", id_text(c->theory, x->id), (unsigned long long)x->work);
    printf("\n");
}

int main(int argc, char **argv) {
    MgElabOptions options = {.theory = MG_PROFILE_EGAL, .kernel = true};
    MgRunFile files[256];
    int nfiles = 0, nsig = 0;
    const char *wanted_v[256], *allowed_v[256];
    Names wanted = {wanted_v, 0}, allowed = {allowed_v, 0};
    bool allow_all = false, verbose = false;
    options.theorem_work = MG_CHECK_THEOREM_WORK;
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (strcmp(a, "-hf") == 0) options.theory = MG_PROFILE_HF;
        else if (strcmp(a, "-mizar") == 0) options.theory = MG_PROFILE_MIZAR;
        else if (strcmp(a, "-hoas") == 0) options.theory = MG_PROFILE_HOAS;
        else if (strcmp(a, "-setmm") == 0) options.theory = MG_PROFILE_SETMM;
        else if (strcmp(a, "-I") == 0 && i + 1 < argc && nfiles < 255) {
            files[nfiles++] = (MgRunFile){argv[++i], true};
            ++nsig;
        } else if (strcmp(a, "--theorem") == 0 && i + 1 < argc && wanted.len < 256)
            wanted.v[wanted.len++] = argv[++i];
        else if (strcmp(a, "--allow-axiom") == 0 && i + 1 < argc && allowed.len < 256)
            allowed.v[allowed.len++] = argv[++i];
        else if (strcmp(a, "--allow-document-axioms") == 0) allow_all = true;
        else if (strcmp(a, "--prove-aby") == 0) options.prove_aby = true;
        else if (strcmp(a, "--aby-steps") == 0 && i + 1 < argc) {
            char *end;
            const char *v = argv[++i];
            unsigned long long w = strtoull(v, &end, 10);
            if (!*v || *end || v[0] == '-') {
                fputs(USAGE, stderr);
                return 64;
            }
            options.aby_max_steps = (uint64_t)w;
        } else if (strcmp(a, "--aby-depth") == 0 && i + 1 < argc) {
            char *end;
            const char *v = argv[++i];
            unsigned long w = strtoul(v, &end, 10);
            if (!*v || *end || v[0] == '-' || w > 64u) {
                fputs(USAGE, stderr);
                return 64;
            }
            options.aby_max_depth = (uint32_t)w;
        }
        else if (strcmp(a, "--theorem-work") == 0 && i + 1 < argc) {
            char *end;
            const char *v = argv[++i];
            unsigned long long w = strtoull(v, &end, 10);
            if (!*v || *end || v[0] == '-') {
                fputs(USAGE, stderr);
                return 64;
            }
            options.theorem_work = (uint64_t)w;
        }
        else if (strcmp(a, "--verbose") == 0) verbose = true;
        else if (strcmp(a, "--help") == 0) {
            fputs(HELP, stdout);
            return 0;
        }
        else if (a[0] != '-' && nfiles < 255) files[nfiles++] = (MgRunFile){a, false};
        else {
            fputs(USAGE, stderr);
            return 64;
        }
    }
    if (!nfiles || files[nfiles - 1].signature) {
        fputs(USAGE, stderr);
        return 64;
    }
    MgLedger *ledger = mg_ledger_new();
    options.ledger = ledger;
    MgElab *e = ledger ? mg_elab_new(&options) : NULL;
    if (!e) {
        fprintf(stderr, "mg-check: cannot start the checker\n");
        mg_ledger_free(ledger);
        return 2;
    }
    char message[4096];
    MgRunStatus run = mg_run_files(e, files, nfiles, message, sizeof message);
    char summary[512];
    MgStatus fin = mg_elab_finish(e, summary, sizeof summary);
    Ctx c = {ledger, mg_elab_theory(e), &allowed, allow_all, options.theorem_work};

    printf("mg-check: %s (theory %s, %d signature%s)\n", files[nfiles - 1].path,
           theory_name(options.theory), nsig, nsig == 1 ? "" : "s");
    uint32_t n = mg_ledger_len(ledger);
    uint64_t thms = 0, nchecked = 0, nverified = 0, admitted = 0, aby = 0, dependent = 0, pending = 0, exhausted = 0, daxioms = 0, dapproved = 0, most_work = 0;
    bool all_ok = true;
    for (uint32_t i = 0; i < n; ++i) {
        MgLedgerEntry x;
        mg_ledger_get(ledger, i, &x);
        if (!x.theorem) {
            if (x.origin != MG_ORIGIN_DOCUMENT)
                continue;
            bool ok = axiom_approved(&c, i);
            ++daxioms;
            dapproved += ok;
            printf("axiom %s line %llu: stated by the document%s\n", x.name, (unsigned long long)x.line,
                   ok ? ", approved" : "");
            continue;
        }
        ++thms;
        bool v = verified(&c, &x);
        nchecked += checked(&x);
        nverified += v;
        admitted += x.verdict == MG_VERDICT_ADMITTED;
        aby += x.verdict == MG_VERDICT_ABY;
        dependent += x.verdict == MG_VERDICT_DEPENDENT;
        pending += x.verdict == MG_VERDICT_PENDING;
        exhausted += x.verdict == MG_VERDICT_EXHAUSTED;
        if (x.work > most_work)
            most_work = x.work;
        bool selected = wanted.len == 0 || names_has(&wanted, x.name, NULL);
        if (selected && !v)
            all_ok = false;
        if (verbose || (selected && (!v || wanted.len)))
            print_theorem(&c, &x, verbose);
    }
    for (int k = 0; k < wanted.len; ++k) {
        bool found = false;
        for (uint32_t i = 0; i < n && !found; ++i) {
            MgLedgerEntry x;
            mg_ledger_get(ledger, i, &x);
            found = x.theorem && strcmp(x.name, wanted.v[k]) == 0;
        }
        if (!found) {
            printf("theorem %s: not found\n", wanted.v[k]);
            all_ok = false;
        }
    }
    printf("summary: %llu theorems: %llu checked (%llu verified), %llu admitted, %llu aby, "
           "%llu dependent, %llu unfinished, %llu out of budget; "
           "%llu document axioms (%llu approved)\n",
           (unsigned long long)thms, (unsigned long long)nchecked, (unsigned long long)nverified,
           (unsigned long long)admitted, (unsigned long long)aby,
           (unsigned long long)dependent, (unsigned long long)pending, (unsigned long long)exhausted,
           (unsigned long long)daxioms, (unsigned long long)dapproved);
    if (options.prove_aby) {
        uint64_t tried, proved;
        mg_elab_aby_counts(e, &tried, &proved);
        printf("aby: %llu of %llu steps proved by search\n", (unsigned long long)proved,
               (unsigned long long)tried);
    }
    if (verbose)
        printf("%s\nwork: the costliest theorem took %llu units (budget %llu)\n", summary,
               (unsigned long long)most_work, (unsigned long long)options.theorem_work);
    int status;
    if (run != MG_RUN_OK) {
        const char *nl = strchr(message, '\n');
        printf("verdict: failed: %.*s\n", nl ? (int)(nl - message) : (int)strlen(message), message);
        status = 2;
    } else if (fin != MG_OK || !mg_ledger_complete(ledger)) {
        printf("verdict: failed: %s\n", fin != MG_OK ? summary : "the verdict ledger is incomplete");
        status = 2;
    } else {
        printf("verdict: %s\n", all_ok ? "verified" : "not verified");
        status = all_ok ? 0 : 1;
    }
    mg_elab_free(e);
    mg_ledger_free(ledger);
    return status;
}

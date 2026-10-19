#define _GNU_SOURCE
#include "atom_wire.h"
#include "cetta_stdlib.h"
#include "eval.h"
#include "library.h"
#include "symbol.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t monotonic_ns(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) return 0;
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}

/* A private, parent-owned worker. The operator selects the module and
 * entry point; CSP1 carries values, never generated source text. This is an
 * evaluator with the parent's authority, not the no-eval space service. */
int main(int argc, char **argv) {
    CettaLanguageId language = CETTA_LANGUAGE_PETTA;
    const CettaProfile *worker_profile = cetta_profile_petta_extended();
    const char *threads_repr = NULL;
    unsigned long threads = 0;
    int first = 1;
    while (first < argc && argv[first][0] == '-') {
        const char *option = argv[first++];
        if (!strcmp(option, "--")) break;
        if (first >= argc) goto usage;
        if (!strcmp(option, "--lang")) {
            const char *name = argv[first++];
            if (!strcmp(name, "prime")) {
                language = CETTA_LANGUAGE_PRIME;
                worker_profile = cetta_profile_prime_default();
            } else if (!strcmp(name, "petta")) {
                language = CETTA_LANGUAGE_PETTA;
                worker_profile = cetta_profile_petta_extended();
            } else goto usage;
        } else if (!strcmp(option, "--num-threads")) {
            char *end = NULL;
            threads_repr = argv[first++];
            errno = 0;
            threads = strtoul(threads_repr, &end, 10);
            if (errno || end == threads_repr || *end ||
                threads_repr[0] == '-' || threads > 1024) goto usage;
        } else goto usage;
    }
    if (argc - first != 2) goto usage;
    const char *module = argv[first], *entry = argv[first + 1];
    signal(SIGPIPE, SIG_IGN);
    SymbolTable symbols;
    symbol_table_init(&symbols); g_symbols = &symbols;
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    VarInternTable variables; var_intern_init(&variables); g_var_intern = &variables;
    Arena permanent, scratch;
    arena_init(&permanent); arena_init(&scratch);
    arena_set_runtime_kind(&permanent, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    arena_set_runtime_kind(&scratch, CETTA_ARENA_RUNTIME_KIND_EVAL);
    CettaLibraryContext *libraries = calloc(1, sizeof(*libraries));
    if (!libraries) return 1;
    cetta_library_context_init_for_language_profile(libraries, language, worker_profile);
    term_universe_set_persistent_arena(&libraries->term_universe, &permanent);
    cetta_library_context_set_exec_path(libraries, argv[0]);
    cetta_library_context_set_script_path(libraries, module);
    eval_set_library_context(libraries);
    Space top; space_init_with_universe(&top, &libraries->term_universe);
    Registry registry; registry_init(&registry);
    registry_bind_id(&registry, g_builtin_syms.self, atom_space(&permanent, &top));
    cetta_library_set_top_module_space(libraries, &top);
    cetta_library_set_top_module_registry(libraries, &registry);
    Atom *error = NULL;
    int rc = 1;
    if (language != CETTA_LANGUAGE_PETTA) {
        stdlib_load(&top, &permanent);
        if (language == CETTA_LANGUAGE_PRIME)
            eval_prime_set_he_signature_space(&top);
    }
    if (threads_repr && !cetta_eval_session_record_generic_setting(
            &libraries->session, "num-threads", CETTA_EVAL_OPTION_VALUE_INT,
            threads_repr, (int64_t)threads)) goto done;
    if (!cetta_library_import_module(libraries, module, &top, false,
            &scratch, &permanent, &registry, -1, NULL, &error)) {
        fprintf(stderr, "worker: module import failed\n");
        if (error) { atom_print(error, stderr); fputc('\n', stderr); }
        goto done;
    }
    for (;;) {
        arena_free(&scratch); arena_init(&scratch);
        arena_set_runtime_kind(&scratch, CETTA_ARENA_RUNTIME_KIND_EVAL);
        unsigned char header[8];
        size_t got = fread(header, 1, 8, stdin);
        if (!got && feof(stdin)) { rc = 0; break; }
        if (got != 8 || memcmp(header, "CSP1", 4)) break;
        size_t length = 0;
        for (size_t i = 4; i < 8; ++i) length = (length << 8) | header[i];
        if (length < 4 || length > CETTA_ATOM_WIRE_BYTES) break;
        unsigned char *bytes = malloc(length);
        if (!bytes) break;
        Atom *request = NULL;
        bool valid = fread(bytes, 1, length, stdin) == length;
        uint64_t decode_start = monotonic_ns();
        valid = valid && cetta_atom_wire_decode(&scratch, bytes, length, &request) == ATOM_WIRE_OK;
        uint64_t decode_ns = monotonic_ns() - decode_start;
        free(bytes);
        if (!valid || request->kind != ATOM_EXPR || request->expr.len != 3 ||
            (!atom_is_symbol(request->expr.elems[0], "work") &&
             !atom_is_symbol(request->expr.elems[0], "work-profile")) ||
            request->expr.elems[1]->kind != ATOM_GROUNDED ||
            request->expr.elems[1]->ground.gkind != GV_STRING) break;
        bool profile = atom_is_symbol(request->expr.elems[0], "work-profile");
        /* PeTTa's quote passes data; Prime's owned return publishes
         * a completed value. Prime's quote instead constructs sealed code. */
        Atom *input = language == CETTA_LANGUAGE_PETTA
            ? atom_expr2(&scratch, atom_symbol(&scratch, "quote"), request->expr.elems[2])
            : atom_expr2(&scratch, atom_symbol(&scratch, "function"),
                atom_expr2(&scratch, atom_symbol(&scratch, "return"), request->expr.elems[2]));
        Atom *call = atom_expr2(&scratch, atom_symbol(&scratch, entry), input);
        EvalOutcome outcome; eval_outcome_init(&outcome);
        uint64_t eval_start = profile ? monotonic_ns() : 0;
        eval_top_with_registry_outcome(&top, &scratch, &permanent, &registry,
            call, &outcome, NULL, NULL);
        uint64_t eval_ns = profile ? monotonic_ns() - eval_start : 0;
        bool complete = outcome.completion == CETTA_EVAL_COMPLETE &&
            !outcome.petta_raised_error && outcome.results.len == 1 &&
            !atom_is_error(outcome.results.items[0]);
        Atom *parts[5] = {atom_symbol(&scratch,"reply"), request->expr.elems[1],
            atom_symbol(&scratch,complete ? "ok" : "error"), complete ? outcome.results.items[0] :
            atom_symbol(&scratch,"incomplete-or-nonsingle-result")};
        if (profile) {
            SymbolTableStats stats;
            symbol_table_stats_snapshot(&symbols, &stats);
            Atom *fields[] = {atom_symbol(&scratch, "WorkerMetrics"),
                atom_int(&scratch, (int64_t)decode_ns), atom_int(&scratch, (int64_t)eval_ns),
                atom_int(&scratch, (int64_t)arena_accounted_live_bytes(&permanent)),
                atom_int(&scratch, (int64_t)arena_accounted_live_bytes(&scratch)),
                atom_int(&scratch, (int64_t)stats.data_count),
                atom_int(&scratch, (int64_t)(stats.owned_bytes + stats.data_allocated_bytes + stats.metadata_bytes))};
            parts[4] = atom_expr(&scratch, fields, sizeof(fields)/sizeof(fields[0]));
        }
        Atom *reply = atom_expr(&scratch, parts, profile ? 5 : 4);
        bytes = NULL;
        valid = cetta_atom_wire_encode(reply, &bytes, &length) == ATOM_WIRE_OK;
        if (valid) {
            for (size_t i = 0; i < 4; ++i) header[i+4] = (unsigned char)(length >> (24-8*i));
            valid = fwrite(header,1,8,stdout) == 8 && fwrite(bytes,1,length,stdout) == length &&
                fflush(stdout) == 0;
        }
        free(bytes); eval_outcome_free(&outcome);
        eval_release_temporary_spaces(); eval_reset_form_gc_survivor();
        if (!valid) break;
    }
done:
    /* The worker has no separate registry-space sweep. Drain all tracked
     * new-space owners here, including the ones still bound in the registry. */
    eval_cleanup_owned_new_spaces(NULL, &top);
    registry_free(&registry);
    space_free(&top);
    cetta_library_context_free(libraries); free(libraries);
    cetta_lib_prolog_global_shutdown();
    cetta_foreign_global_shutdown();
    eval_match_decision_cache_free_for_current_thread();
    eval_profiled_type_cache_free_for_current_thread();
    space_execution_analysis_cache_free_for_current_thread();
    eval_set_library_context(NULL);
    if (language == CETTA_LANGUAGE_PRIME)
        eval_prime_set_he_signature_heads(NULL, 0u);
    arena_free(&scratch); arena_free(&permanent);
    var_intern_free(&variables); g_var_intern = NULL;
    symbol_table_free(&symbols); g_symbols = NULL;
    return rc;
usage:
    fprintf(stderr, "usage: cetta-value-worker [--lang petta|prime] "
        "[--num-threads N] MODULE ENTRY\n");
    return 2;
}

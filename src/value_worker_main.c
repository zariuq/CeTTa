#define _GNU_SOURCE
#include "atom_wire.h"
#include "eval.h"
#include "library.h"
#include "symbol.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A private, parent-owned PeTTa worker. The operator selects the module and
 * entry point; CSP1 carries values, never generated source text. This is an
 * evaluator with the parent's authority, not the no-eval space service. */
int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: cetta-value-worker MODULE ENTRY\n"); return 2;
    }
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
    cetta_library_context_init_for_language_profile(libraries,
        CETTA_LANGUAGE_PETTA, cetta_profile_petta_extended());
    term_universe_set_persistent_arena(&libraries->term_universe, &permanent);
    cetta_library_context_set_exec_path(libraries, argv[0]);
    cetta_library_context_set_script_path(libraries, argv[1]);
    eval_set_library_context(libraries);
    Space top; space_init_with_universe(&top, &libraries->term_universe);
    Registry registry; registry_init(&registry);
    registry_bind_id(&registry, g_builtin_syms.self, atom_space(&permanent, &top));
    cetta_library_set_top_module_space(libraries, &top);
    cetta_library_set_top_module_registry(libraries, &registry);
    Atom *error = NULL;
    int rc = 1;
    if (!cetta_library_import_module(libraries, argv[1], &top, false,
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
        bool valid = fread(bytes, 1, length, stdin) == length &&
            cetta_atom_wire_decode(&scratch, bytes, length, &request) == ATOM_WIRE_OK;
        free(bytes);
        if (!valid || request->kind != ATOM_EXPR || request->expr.len != 3 ||
            !atom_is_symbol(request->expr.elems[0], "work") ||
            request->expr.elems[1]->kind != ATOM_GROUNDED ||
            request->expr.elems[1]->ground.gkind != GV_STRING) break;
        Atom *input = atom_expr2(&scratch, atom_symbol(&scratch, "quote"), request->expr.elems[2]);
        Atom *call = atom_expr2(&scratch, atom_symbol(&scratch, argv[2]), input);
        EvalOutcome outcome; eval_outcome_init(&outcome);
        eval_top_with_registry_outcome(&top, &scratch, &permanent, &registry,
            call, &outcome, NULL, NULL);
        bool complete = outcome.completion == CETTA_EVAL_COMPLETE &&
            !outcome.petta_raised_error && outcome.results.len == 1 &&
            !atom_is_error(outcome.results.items[0]);
        Atom *parts[] = {atom_symbol(&scratch,"reply"), request->expr.elems[1],
            atom_symbol(&scratch,complete ? "ok" : "error"), complete ? outcome.results.items[0] :
            atom_symbol(&scratch,"incomplete-or-nonsingle-result")};
        Atom *reply = atom_expr(&scratch, parts, 4);
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
    arena_free(&scratch); arena_free(&permanent);
    var_intern_free(&variables); g_var_intern = NULL;
    symbol_table_free(&symbols); g_symbols = NULL;
    return rc;
}

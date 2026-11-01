#include "cesta.h"
#include "pp.h"
#include "native.h"
#include "macro.h"
#include "export.h"
#include "layout.h"
#include "operation.h"
#include "table.h"
#include "protocol.h"
#include "project.h"
#include "checked.h"

#include <string.h>

#ifdef CE_BUILTIN_LANGUAGES
CeText ce_builtin_CPPGNUGrammar(void);
#endif

static int diagnostic(const char *path, CeText source, const CeError *error) {
    size_t line = 1, column = 1;
    for (size_t i = 0; i < error->offset && i < source.len; ++i) {
        if (source.data[i] == '\n') { ++line; column = 1; }
        else ++column;
    }
    fprintf(stderr, "%s:%zu:%zu: %s\n", path, line, column, error->message);
    return 1;
}

int main(int argc, char **argv) {
    if (argc > 1 && (!strcmp(argv[1],"pack") || !strcmp(argv[1],"unpack"))) return ce_wire_command(argc,argv);
#ifdef CE_BUILTIN_LANGUAGES
    if (argc > 1 && !strcmp(argv[1],"cc")) return ce_cc(argc-2,argv+2);
    /* cesta build GRAMMAR.cf … is the grammar workbench; otherwise a project build. */
    if (argc > 1 && !strcmp(argv[1],"build") &&
        !(argc > 2 && strlen(argv[2]) > 3 && !strcmp(argv[2]+strlen(argv[2])-3,".cf")))
        return ce_build_command(argc,argv);
    if (argc > 1 && !strcmp(argv[1],"expand")) return ce_expand_command(argc,argv);
    if (argc > 1 && !strcmp(argv[1],"check-macros")) return ce_macro_command(argc,argv);
    if (argc > 1 && !strcmp(argv[1],"export-header")) return ce_export_command(argc,argv);
    if (argc > 1 && !strcmp(argv[1],"export-c")) return ce_export_c_command(argc,argv);
    if (argc > 1 && !strcmp(argv[1],"layout")) return ce_layout_command(argc,argv);
    if (argc > 1 && !strcmp(argv[1],"tables")) return ce_table_command(argc,argv);
    if (argc > 1 && !strcmp(argv[1],"protocol")) return ce_protocol_command(argc,argv);
    if (argc > 1 && !strcmp(argv[1],"checked")) return ce_checked_command(argc,argv);
    if (argc > 1 && !strcmp(argv[1],"operations")) return ce_operation_command(argc,argv);
    if (argc > 1 && (!strcmp(argv[1],"view") || !strcmp(argv[1],"convert") || !strcmp(argv[1],"export-view-rules")))
        return ce_view_command(argc,argv);
    if (argc > 1 && (!strcmp(argv[1],"bind") || !strcmp(argv[1],"types") || !strcmp(argv[1],"probe") ||
                     !strcmp(argv[1],"explicit") || !strcmp(argv[1],"census") || !strcmp(argv[1],"abi-tables")))
        return ce_sema_command(argc,argv);
    if (argc > 1 && (!strcmp(argv[1],"source") || !strcmp(argv[1],"source-bytes")))
        return ce_pp_source_command(argc,argv,ce_builtin_CPPGNUGrammar());
    if (argc > 1 && !strcmp(argv[1],"pp")) return ce_pp_command(argc,argv,ce_builtin_CPPGNUGrammar());
    if (argc > 1 && !strcmp(argv[1],"read"))
        for (int i = 2; i < argc; ++i)
            if (!strcmp(argv[i],"--cc")) return ce_source_command(argc,argv,CE_COMPOSE_ORIGIN_COHERENT);
#endif
    int compiler_arg = argc;
    if (argc > 1 && (!strcmp(argv[1],"build") || !strcmp(argv[1],"check-program") || !strcmp(argv[1],"run"))) {
        for (int i = 4; i < argc; ++i)
            if (!strcmp(argv[i],"-o") || !strcmp(argv[i],"--cc") || !strcmp(argv[i],"--")) { compiler_arg = i; break; }
    }
    int compiler_argc = argc-compiler_arg;
    char **compiler_argv = argv+compiler_arg;
    argc = compiler_arg;
    unsigned composition = CE_COMPOSE_ORIGIN_COHERENT;
    if (argc >= 3 && !strcmp(argv[argc-2],"--composition")) {
        if (!strcmp(argv[argc-1],"origin-view")) composition = CE_COMPOSE_ORIGIN_VIEW;
        else if (strcmp(argv[argc-1],"origin-coherent")) { fputs("cesta: unknown module composition profile\n",stderr); return 2; }
        argc -= 2;
    }
#ifdef CE_BUILTIN_LANGUAGES
    if ((argc == 3 && (!strcmp(argv[1],"read") || !strcmp(argv[1],"write") || !strcmp(argv[1],"check-module"))) ||
        (argc == 4 && !strcmp(argv[1],"module-grammar")) ||
        (argc == 4 && (!strcmp(argv[1],"read") || !strcmp(argv[1],"write")) && !strncmp(argv[2],"--std=",6)))
        return ce_source_command(argc,argv,composition);
#endif
    if (argc < 3 || (strcmp(argv[1], "check-grammar") && strcmp(argv[1], "parse") &&
                    strcmp(argv[1], "read") && strcmp(argv[1], "write") && strcmp(argv[1], "check-module") && strcmp(argv[1], "module-grammar") && strcmp(argv[1], "lr-table") && strcmp(argv[1], "parse-lr") && strcmp(argv[1],"build") && strcmp(argv[1],"check-program") && strcmp(argv[1],"run") && strcmp(argv[1],"export-metadata"))) {
        fputs("usage: cesta check-grammar GRAMMAR.cf\n"
              "       cesta cc [--cc COMPILER] [--read-warnings] FLAGS SOURCES [OBJECTS/LIBRARIES]\n"
              "       cesta expand [--cc COMPILER] [--presentation native-c/1] [-o OUTPUT.c] INPUT.cesta [-- NATIVE-FLAGS]\n"
              "       cesta check-macros MODULE THEORY\n"
              "       cesta checked check|plan|header FILE.cesta [-o OUTPUT]\n"
              "       cesta export-header [--plan PLAN] EXPORT-SPEC -o OUTPUT.h\n"
              "       cesta export-c [--cc COMPILER] [-o OUTPUT.c] INPUT.cesta [-- NATIVE-FLAGS]\n"
              "       cesta layout check|functions MODULE THEORY\n"
              "       cesta layout header MODULE THEORY -o OUTPUT.h\n"
              "       cesta tables check|rows|header BUNDLE [TABLE [COLUMN ...]] [-o HEADER]\n"
              "       cesta operations check|rows|metta|tables|protocols MODULE THEORY\n"
              "       cesta operations header MODULE THEORY -o OUTPUT.h\n"
              "       cesta view --as PRESENTATION [--from PRESENTATION] [--std=c11|gnu11] INPUT\n"
              "       cesta convert --to PRESENTATION [--from PRESENTATION] [--std=c11|gnu11] INPUT\n"
              "       cesta export-view-rules --as PRESENTATION [--std=c11|gnu11]\n"
              "       cesta export-metadata GRAMMAR.cf [PROFILE]\n"
              "       cesta source [--std=c11|gnu11] [--check|--tokens] SOURCE...\n"
              "       cesta pp [--cc COMPILER] [-P] [-o OUTPUT] [--provenance MAP] COMPILER-FLAGS SOURCE\n"
              "       cesta source-bytes SOURCE-DOCUMENT\n"
              "       cesta read [--std=c11|gnu11] PREPROCESSED-C\n"
              "       cesta read --cc COMPILER COMPILER-FLAGS SOURCE\n"
              "       cesta write [--std=c11|gnu11] SEXP\n"
              "       cesta bind|types|probe|explicit|census [--std=c11|gnu11] [--dialect=gcc|clang] [--abi=ABI] PREPROCESSED-C\n"
              "       cesta bind|types|census --json-lines --cc COMPILER COMPILER-FLAGS SOURCE\n"
              "       cesta abi-tables [--abi=ABI]\n"
              "       cesta check-module MODULE\n"
              "       cesta module-grammar MODULE THEORY\n"
              "       cesta parse GRAMMAR.cf SOURCE [ENTRY [PROFILE]]\n"
              "       cesta read GRAMMAR.cf SOURCE [ENTRY [PROFILE]]\n"
              "       cesta write GRAMMAR.cf SEXP [ENTRY [PROFILE]]\n"
              "       cesta check-module MODULE-SYNTAX.cf MODULE [PROFILE]\n"
              "       cesta module-grammar MODULE-SYNTAX.cf MODULE THEORY [PROFILE]\n"
              "       cesta build [-C DIR] [-f cesta.project] [--cc COMPILER] [-j N] [-v] [--export-c DIR] [TARGET...]\n"
              "       cesta build|check-program|run GRAMMAR.cf SEXP [ENTRY PROFILE] -o OUTPUT [--cc COMPILER] [-- FLAGS]\n"
              "module commands accept --composition origin-coherent|origin-view\n", stderr);
        return 2;
    }
    CeArena arena = {0};
    CeGrammar grammar = {0};
    CeTokens tokens = {0};
    CeForest forest = {.root = -1};
    CeCatalog catalog = {0};
    CeLR lr = {0};
    CeDFA dfa = {0};
    CeError error = {{0},0};
    CeText source = {NULL,0}, input = {NULL,0};
    const char *path = argv[2];
    int result = 0;
    if (!ce_read_file(&arena, path, &source, &error) ||
        !ce_grammar_read(&grammar, source, &error) || !ce_grammar_finish(&grammar, &error)) {
        result = diagnostic(path, source, &error); goto done;
    }
    if (!strcmp(argv[1], "check-grammar")) {
        printf("grammar: %zu productions, %zu symbols, %zu lexical rules\n", grammar.nproductions, grammar.nsymbols, grammar.nlex);
        goto done;
    }
    bool modules = !strcmp(argv[1],"check-module") || !strcmp(argv[1],"module-grammar");
    int profile_arg = !strcmp(argv[1],"check-module") ? 4 : !strcmp(argv[1],"export-metadata") ? 3 : 5;
    if (argc > profile_arg) {
        CeText profile = {NULL,0};
        if (!ce_read_file(&arena, argv[profile_arg], &profile, &error) || !ce_profile_load(&grammar, argv[profile_arg], &error)) {
            result = diagnostic(argv[profile_arg], profile, &error); goto done;
        }
    }
    if (!strcmp(argv[1],"export-metadata")) {
        if (!ce_metadata_export(&grammar,stdout,&error)) result = diagnostic(path,source,&error);
        goto done;
    }
    if (argc < 4) { fputs("cesta: parse needs a source file\n", stderr); result = 2; goto done; }
    path = argv[3];
    if (modules) {
        if (!ce_read_file(&arena,path,&input,&error)) { result = diagnostic(path,input,&error); goto done; }
        catalog.syntax = &grammar;
        catalog.composition = composition;
        CeModule *module;
        if (!ce_module_load(&catalog,path,&module,&error) || !ce_module_check(&catalog,module,&error)) {
            result = diagnostic(path,input,&error); goto done;
        }
        if (!strcmp(argv[1],"check-module")) { puts("module: syntax and activated-theory admission passed"); goto done; }
        if (argc < 5) { fputs("cesta: module-grammar needs a theory name\n",stderr); result = 2; goto done; }
        CePresentation presentation;
        if (!ce_module_theory(&catalog,module,argv[4],&presentation,&error)) { result = diagnostic(path,input,&error); goto done; }
        for (size_t i = 0; i < presentation.len; ++i) {
            CeTree *tree = presentation.definitions[i].tree;
            if (!ce_tree_print(&grammar,tree,tree->symbol,stdout,&error)) { result = diagnostic(path,input,&error); goto done; }
            puts(";");
        }
        goto done;
    }
    if (!grammar.nproductions) { fputs("cesta: lexer-only grammar has no syntax entry\n",stderr); result = 2; goto done; }
    int entry = grammar.nentries ? grammar.entries[0] : grammar.productions[0].lhs;
    if (argc > 4) {
        entry = -1;
        for (size_t i = 0; i < grammar.nsymbols; ++i)
            if (grammar.symbols[i].kind == CE_NONTERMINAL && ce_is(grammar.symbols[i].name, argv[4])) entry = (int)i;
        if (entry < 0) { fputs("cesta: unknown entry category\n", stderr); result = 2; goto done; }
    }
    bool fast = !strcmp(argv[1],"parse-lr") || (!strcmp(argv[1],"read") && grammar.npolicies);
    if (!strcmp(argv[1],"lr-table") || fast) {
        if (!ce_lr_prepare(&grammar,entry,&lr,&error)) { result = diagnostic(argv[2],source,&error); goto done; }
        if (!strcmp(argv[1],"lr-table")) {
            printf("LALR: %zu states, %zu items, %zu conflict cells, %zu unresolved\n",lr.states,lr.items,lr.ambiguities,lr.conflicts);
            ce_lr_conflicts(&grammar,&lr,stdout); goto done;
        }
    }
    if (!ce_read_file(&arena, path, &input, &error)) { result = diagnostic(path,input,&error); goto done; }
    CeTree *tree;
    bool compiling = !strcmp(argv[1],"build") || !strcmp(argv[1],"check-program") || !strcmp(argv[1],"run");
    if (compiling) {
        if (!ce_tree_decode(&grammar,input,entry,&arena,&tree,&error)) result = diagnostic(path,input,&error);
        else {
            result = ce_compile_tree(&grammar,tree,entry,compiler_argc,compiler_argv,!strcmp(argv[1],"check-program"),!strcmp(argv[1],"run"),&error);
            if (result && error.message[0]) diagnostic(path,input,&error);
        }
        goto done;
    }
    if (!strcmp(argv[1],"write")) {
        if (!ce_tree_decode(&grammar,input,entry,&arena,&tree,&error) || !ce_tree_print(&grammar,tree,entry,stdout,&error))
            result = diagnostic(path,input,&error);
        else fputc('\n',stdout);
        goto done;
    }
    if (!ce_dfa_prepare(&grammar,&dfa,&error) || !ce_dfa_lex(&grammar,&dfa,input,&tokens,&error) ||
        (fast ? !ce_lr_parse(&grammar,&lr,&tokens,&forest,&error) : !ce_parse(&grammar, &tokens, entry, &forest, &error))) {
        result = diagnostic(path, input, &error); goto done;
    }
    if (!strcmp(argv[1],"read")) {
        if (!ce_tree_from_forest(&grammar,&forest,&tokens,&arena,&tree,&error)) result = diagnostic(path,input,&error);
        else { ce_tree_encode(&grammar,tree,stdout); fputc('\n',stdout); }
        goto done;
    }
    printf("parse: %zu tokens, %zu packed nodes, %zu descriptors; parses=%u%s\n", tokens.len, forest.len, forest.descriptors,
           ce_forest_count(&forest, 2), ce_forest_count(&forest, 2) == 2 ? "+" : "");
done:
    ce_catalog_free(&catalog);
    ce_lr_free(&lr);
    ce_dfa_free(&dfa);
    ce_forest_free(&forest); ce_tokens_free(&tokens); ce_grammar_free(&grammar); ce_arena_free(&arena);
    return result;
}

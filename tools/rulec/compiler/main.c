/* main.c - see rulec.h */
#include "rulec.h"

int main(int argc, char **argv) {
    const char *wire = NULL, *census = NULL, *census_tsv = NULL, *dag = NULL, *emit = NULL, *emit_h = NULL;
    const char *native_sorts[16]; int nns = 0;
    const char *native_op_specs[64]; int nos = 0;
    const char *law_specs[64]; int nls = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--census") && i + 1 < argc) census = argv[++i];
        else if (!strcmp(argv[i], "--census-tsv") && i + 1 < argc) census_tsv = argv[++i];
        else if (!strcmp(argv[i], "--dag") && i + 1 < argc) dag = argv[++i];
        else if (!strcmp(argv[i], "--emit") && i + 1 < argc) emit = argv[++i];
        else if (!strcmp(argv[i], "--emit-h") && i + 1 < argc) emit_h = argv[++i];
        else if (!strcmp(argv[i], "--safepoint") && i + 1 < argc) safepoint_name = argv[++i];
        else if (!strcmp(argv[i], "--no-musttail")) no_musttail = true;
        else if (!strcmp(argv[i], "--census-only")) census_only = true;
        else if (!strcmp(argv[i], "--cache-constants")) cache_constants = true;
        else if (!strcmp(argv[i], "--include-rt") && i + 1 < argc) include_rt = argv[++i];
        else if ((!strcmp(argv[i], "--blocks") || !strcmp(argv[i], "--bindings")) && i + 1 < argc) blocks_file = argv[++i];
        else if (!strcmp(argv[i], "--component-prefix") && i + 1 < argc) component_prefix = argv[++i];
        else if (!strcmp(argv[i], "--source-digest") && i + 1 < argc) source_digest = argv[++i];
        else if (!strcmp(argv[i], "--native-sort") && i + 1 < argc) native_sorts[nns++] = argv[++i];
        else if (!strcmp(argv[i], "--native-op") && i + 1 < argc) native_op_specs[nos++] = argv[++i];
        else if (!strcmp(argv[i], "--refl-law") && i + 1 < argc) law_specs[nls++] = argv[++i];
        else if (!strcmp(argv[i], "--layers") && i + 1 < argc) {
            char *s = xstrdup(argv[++i]);
            for (char *t = strtok(s, ","); t && nlayers < MAXLAYERS; t = strtok(NULL, ",")) {
                char *eq = strchr(t, '=');
                if (!eq) die("bad --layers");
                *eq = 0;
                layer_name[nlayers] = t; layer_first[nlayers++] = eq + 1;
            }
        } else if (argv[i][0] != '-' && !wire) wire = argv[i];
        else die("usage: rulec WIRE [--census F] [--census-tsv F] [--dag F] [--emit F.c] [--emit-h F.h] [--layers L=label,...] [--safepoint J] [--native-sort S=kind(...)] [--native-op J=fn[:why]]");
    }
    if (!wire) die("no wire file");
    load_wire(wire);
    flatten_calls();
    check_rules();
    if (nerrors) { fprintf(stderr, "rulec: %d check failures; nothing emitted\n", nerrors); return 1; }
    for (int i = 0; i < nns; i++) parse_native_sort(native_sorts[i]);
    if (blocks_file) read_blocks(blocks_file);
    check_reps();
    apply_blocks();
    for (int i = 0; i < nos; i++) {
        char *s = xstrdup(native_op_specs[i]), *eq = strchr(s, '='), *colon;
        if (!eq) die("bad --native-op");
        *eq = 0;
        colon = strchr(eq + 1, ':');
        if (colon) *colon = 0;
        int c = map_get(&ctor_map, s);
        if (c < 0 || !ctors[c].defined) die("--native-op: %s is not a judgment", s);
        native_ops[nnative_ops++] = (NativeOp){c, eq + 1, colon ? colon + 1 : NULL, xstrdup(s),
            colon && !strncmp(colon + 1, "proved", 6) ? "proved" : colon && !strncmp(colon + 1, "trusted", 7) ? "trusted" : "checked", NULL};
    }
    assign_layers();
    build_dags();
    analyse_determinism();
    analyse_calls();
    check_safepoint();
    apply_laws();
    /* laws need the coverage analysis */
    for (int i = 0; i < nls; i++) {
        /* J=RESULT[@justification]: J has two arguments of one sort; RESULT is a nullary constructor of J's sort */
        char *s = xstrdup(law_specs[i]), *eq = strchr(s, '='), *colon;
        if (!eq) die("bad --refl-law");
        *eq = 0;
        colon = strchr(eq + 1, '@');
        if (colon) *colon = 0;
        int c = map_get(&ctor_map, s), r = map_get(&ctor_map, eq + 1);
        if (c < 0 || !ctors[c].defined || ctors[c].arity != 2 || ctors[c].asort[0] != ctors[c].asort[1])
            die("--refl-law: %s is not a judgment of two arguments of one sort", s);
        if (r < 0 || ctors[r].defined || ctors[r].arity != 0 || ctors[r].sort != ctors[c].sort)
            die("--refl-law: %s is not a nullary constructor of %s's sort", eq + 1, s);
        if (native_op_of(c) >= 0) die("--refl-law: %s already has a native operation", s);
        if (!ctors[c].complete) die("--refl-law: %s does not cover every input, so it cannot be reflexive on every input", s);
        laws[nlaws++] = (Law){c, r, colon ? colon + 1 : NULL, xstrdup(s), colon && !strncmp(colon + 1, "proved", 6) ? "proved" : "checked"};
    }
    if (nerrors) { fprintf(stderr, "rulec: %d check failures; nothing emitted\n", nerrors); return 1; }
    int nj = 0;
    for (int c = 0; c < nctors; c++) if (ctors[c].defined) nj++;
    fprintf(stderr, "rulec: %s/%s: %d sorts, %d constructors, %d judgments, %d rules, %d DAG nodes, %d occurrences\n",
            module_name, theory_name, nsorts, nctors, nj, nrules, nnodes, noccs);
    number_trmc();
    if (census) { write_census(census, census_tsv); write_census_blocks(census); }
    if (dag) { write_dag(dag); write_block_table(dag, "a"); }
    if (emit && census_only) die("--census-only: nothing is emitted");
    if (emit) emit_c(emit, emit_h, wire);
    return 0;
}


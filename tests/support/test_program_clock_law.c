/* The program clock's law (space_global_program_epoch).  Every operation that
 * can change a program moves the clock: equations and declarations added and
 * removed, imports, module bits and the engine's registries, through each
 * route that performs them.  An operation that changes only data leaves the
 * clock where it was, so no tier decides an occurrence again for a program
 * that did not change: the machine's planned data, the entered plans, the
 * compiled tier's versions and the prepared machine's guard all key on it.
 *
 * usage: test_program_clock_law [--lib-prolog 0|1] */
#include "../../src/embedding.c"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned g_moves;
static unsigned g_stays;
static const char *g_engine_name = "";

/* Every answer of the document, printed, with errors flagged. */
static char *run_text(cetta_engine_t *engine, const char *source,
                      bool *errored) {
    cetta_result_t *result = NULL;
    cetta_status_t status = cetta_embed_run(engine, source, strlen(source),
                                            NULL, &result);
    *errored = status != CETTA_OK;
    char *text = NULL;
    size_t length = 0u;
    FILE *out = open_memstream(&text, &length);
    assert(out);
    size_t count = result ? cetta_embed_result_count(result) : 0u;
    for (size_t index = 0u; index < count; index++) {
        cetta_atom_t *atom = NULL;
        assert(cetta_embed_result_get(result, index, &atom) == CETTA_OK);
        if (atom_is_error(atom->value))
            *errored = true;
        atom_print(atom->value, out);
        fputc(' ', out);
        cetta_embed_atom_release(atom);
    }
    fclose(out);
    cetta_embed_result_release(result);
    return text;
}

static void setup(cetta_engine_t *engine, const char *source) {
    bool errored = false;
    char *text = run_text(engine, source, &errored);
    if (errored) {
        fprintf(stderr, "FAIL (%s): setup failed: %s\n  answers: %s\n",
                g_engine_name, source, text);
        exit(1);
    }
    free(text);
}

/* A program change: the clock moves. */
static void moves(cetta_engine_t *engine, const char *what,
                  const char *source) {
    uint64_t before = space_global_program_epoch();
    bool errored = false;
    char *text = run_text(engine, source, &errored);
    uint64_t after = space_global_program_epoch();
    if (errored || after == before) {
        fprintf(stderr, "FAIL (%s): %s %s\n  %s\n  answers: %s\n",
                g_engine_name, what,
                errored ? "failed" : "left the program clock where it was",
                source, text);
        exit(1);
    }
    free(text);
    g_moves++;
}

/* A data change: run once (a first run may load what the operation needs),
 * then the clock stays where it is across the next run. */
static void stays(cetta_engine_t *engine, const char *what,
                  const char *source) {
    bool errored = false;
    char *text = run_text(engine, source, &errored);
    free(text);
    uint64_t before = space_global_program_epoch();
    text = run_text(engine, source, &errored);
    uint64_t after = space_global_program_epoch();
    if (errored || after != before) {
        fprintf(stderr, "FAIL (%s): %s %s\n  %s\n  answers: %s\n",
                g_engine_name, what,
                errored ? "failed" : "moved the program clock",
                source, text);
        exit(1);
    }
    free(text);
    g_stays++;
}

static cetta_engine_t *engine_for(const char *language, const char *profile) {
    cetta_engine_t *engine = NULL;
    cetta_engine_config_t config = {sizeof(config), CETTA_EMBED_ABI_VERSION,
        language, profile, NULL};
    assert(cetta_embed_engine_new(&config, &engine) == CETTA_OK);
    return engine;
}

/* The foreign runtime is one per process: its definitions are made once. */
static void petta_routes(const char *profile, bool foreign) {
    g_engine_name = profile ? profile : "petta";
    cetta_engine_t *engine = engine_for("petta", profile);
    setup(engine, "!(+ 1 1)");
    setup(engine,
          "(= (lc-into $sp $h) (add-atom $sp (= ($h $x) v)))\n"
          "(= (lc-from $sp $h) (remove-atom $sp (= ($h $x) v)))\n"
          "(= (lc-declare $sp $h) (add-atom $sp (: $h (-> Number Number))))\n"
          "(= (lc-put $sp $n) (add-atom $sp (lc-fact $n)))\n"
          "(= (lc-take $sp $n) (remove-atom $sp (lc-fact $n)))\n"
          "(= (lc-inbody) (add-atom &self (= (lc-body $x) v)))\n"
          "(= (lc-outbody) (remove-atom &self (= (lc-body $x) v)))\n"
          "(= (lc-dbody $n) (add-atom &self (lc-fact $n)))\n"
          "(= (lc-memo $x) $x)\n"
          "(= (lc-rule $x) $x)\n"
          "!(bind! &lcd (new-space))");

    /* Equations, through each route that adds or removes them. */
    moves(engine, "an equation written in a document",
          "(= (lc-doc $x) (Doc $x))");
    moves(engine, "an equation added by add-atom",
          "!(add-atom &self (= (lc-add $x) v))");
    moves(engine, "an equation added through a variable target",
          "!(lc-into &self lc-var)");
    moves(engine, "an equation added inside an equation body",
          "!(lc-inbody)");
    moves(engine, "an equation added under a collection",
          "!(collapse (add-atom &self (= (lc-coll $x) v)))");
    moves(engine, "an equation added to another space",
          "!(add-atom &lcd (= (lc-other $x) v))");
    moves(engine, "an equation removed by remove-atom",
          "!(remove-atom &self (= (lc-add $x) v))");
    moves(engine, "an equation removed through a variable target",
          "!(lc-from &self lc-var)");
    moves(engine, "an equation removed inside an equation body",
          "!(lc-outbody)");
    moves(engine, "an equation removed from another space",
          "!(remove-atom &lcd (= (lc-other $x) v))");

    /* Declarations. */
    moves(engine, "a declaration written in a document",
          "(: lc-decl (-> Number Number))");
    moves(engine, "a declaration added by add-atom",
          "!(add-atom &self (: lc-decl2 (-> Number Number)))");
    moves(engine, "a declaration added through a variable target",
          "!(lc-declare &self lc-decl3)");
    moves(engine, "a declaration removed by remove-atom",
          "!(remove-atom &self (: lc-decl2 (-> Number Number)))");

    /* Imports and module bits. */
    moves(engine, "a library imported by import!",
          "!(import! &self (library list))");
    moves(engine, "a module file imported by import!",
          "!(import! &self \"tests/petta/fixtures/import_forward.metta\")");
    {
        cetta_atom_t *spec = NULL;
        const char *text = "(library str)";
        assert(cetta_embed_parse(engine, text, strlen(text), &spec) ==
               CETTA_OK);
        uint64_t before = space_global_program_epoch();
        cetta_result_t *result = NULL;
        assert(cetta_embed_import(engine, NULL, spec, NULL, &result) ==
               CETTA_OK);
        cetta_embed_result_release(result);
        cetta_embed_atom_release(spec);
        if (space_global_program_epoch() == before) {
            fprintf(stderr, "FAIL (%s): a library imported through the "
                    "embedding interface left the program clock\n",
                    g_engine_name);
            exit(1);
        }
        g_moves++;
    }

    /* The engine's registries: translator rules, tabling, memoization. */
    moves(engine, "a translator rule added",
          "!(add-translator-rule! lc-rule)");
    moves(engine, "a translator rule removed",
          "!(remove-translator-rule! lc-rule)");
    setup(engine, "!(import! &self (library lib_tabling))");
    moves(engine, "a relation tabled", "!(tabled (lc-tab $x))");
    setup(engine, "!(import! &self (library lib_memo))");
    moves(engine, "a function memoized", "!(memoize lc-memo)");

    /* Foreign definitions. */
    if (foreign) {
        moves(engine, "a foreign function imported",
              "!(import_prolog_function succ)");
        moves(engine, "foreign predicates consulted",
              "!(callPredicate (Predicate (consult "
              "\"tests/petta/fixtures/late_foreign_continuation.pl\")))");
        moves(engine, "a foreign predicate asserted",
              "!(assertzPredicate (Predicate (lc_fact 1)))");
    }

    /* Data only: the clock stays. */
    stays(engine, "a data row added by add-atom",
          "!(add-atom &self (lc-fact 1))");
    stays(engine, "a data row removed by remove-atom",
          "!(remove-atom &self (lc-fact 1))");
    stays(engine, "a data row added through a variable target",
          "!(lc-put &self 2)");
    stays(engine, "a data row removed through a variable target",
          "!(lc-take &self 2)");
    stays(engine, "a data row added inside an equation body",
          "!(lc-dbody 3)");
    stays(engine, "a data row added to another space",
          "!(add-atom &lcd (lc-fact 4))");
    stays(engine, "data rows removed by a match",
          "!(match &self (lc-fact $x) (remove-atom &self (lc-fact $x)))");
    stays(engine, "a space made", "!(new-space)");
    stays(engine, "a state cell made and changed",
          "!(let $s (new-state 1) (change-state! $s 2))");
    stays(engine, "a token bound", "!(bind! lc-word 5)");
    stays(engine, "a line printed", "!(println! lc-hello)");
    stays(engine, "an equation called", "!(lc-doc 5)");
    stays(engine, "arithmetic", "!(* 6 7)");
    cetta_embed_engine_release(engine);
}

static void he_routes(void) {
    g_engine_name = "he";
    cetta_engine_t *engine = engine_for("he", NULL);
    setup(engine, "!(+ 1 1)");
    setup(engine, "!(bind! &lcd (new-space))");
    moves(engine, "an equation written in a document",
          "(= (lc-doc $x) (Doc $x))");
    moves(engine, "an equation added by add-atom",
          "!(add-atom &self (= (lc-add $x) v))");
    moves(engine, "an equation added to another space",
          "!(add-atom &lcd (= (lc-other $x) v))");
    moves(engine, "an equation removed by remove-atom",
          "!(remove-atom &self (= (lc-add $x) v))");
    moves(engine, "a declaration written in a document",
          "(: lc-decl (-> Number Number))");
    moves(engine, "a declaration added by add-atom",
          "!(add-atom &self (: lc-decl2 (-> Number Number)))");
    moves(engine, "a declaration removed by remove-atom",
          "!(remove-atom &self (: lc-decl2 (-> Number Number)))");
    stays(engine, "a data row added by add-atom",
          "!(add-atom &self (lc-fact 1))");
    stays(engine, "a data row removed by remove-atom",
          "!(remove-atom &self (lc-fact 1))");
    stays(engine, "a data row added to another space",
          "!(add-atom &lcd (lc-fact 2))");
    stays(engine, "an equation called", "!(lc-doc 5)");
    cetta_embed_engine_release(engine);
}

/* The space operations every route above reaches. */
static Atom *parse_one(Arena *arena, const char *text) {
    Atom **forms = NULL;
    int count = parse_metta_text(text, arena, &forms);
    assert(count == 1 && forms);
    Atom *atom = forms[0];
    free(forms);
    return atom;
}

static void space_routes(void) {
    g_engine_name = "space";
    cetta_engine_t *engine = engine_for("petta", NULL);
    EmbeddingScope scope;
    assert(engine_enter(engine, &scope) == CETTA_OK);
    Arena *arena = &engine->persistent;
    Atom *equation = parse_one(arena, "(= (lc-space $x) v)");
    Atom *declaration = parse_one(arena, "(: lc-space (-> Atom Atom))");
    Atom *fact = parse_one(arena, "(lc-space-fact 1)");
    Space *program = calloc(1u, sizeof(*program));
    Space *other = calloc(1u, sizeof(*other));
    Space *data = calloc(1u, sizeof(*data));
    assert(program && other && data);
    space_init(program);
    space_init(other);
    space_init(data);
    struct { const char *what; int op; bool program; } cases[] = {
        {"space_add of an equation", 0, true},
        {"space_remove of an equation", 1, true},
        {"space_add of a declaration", 2, true},
        {"space_remove of a declaration", 3, true},
        {"space_add of a data row", 4, false},
        {"space_remove of a data row", 5, false},
        {"space_replace_contents with equations", 6, true},
        {"space_free of a space that held equations", 7, true},
        {"space_free of a space that held only data", 8, false},
    };
    for (size_t index = 0u; index < sizeof(cases) / sizeof(cases[0]); index++) {
        uint64_t before = space_global_program_epoch();
        switch (cases[index].op) {
        case 0: space_add(program, equation); break;
        case 1: assert(space_remove(program, equation)); break;
        case 2: space_add(program, declaration); break;
        case 3: assert(space_remove(program, declaration)); break;
        case 4: space_add(data, fact); break;
        case 5: assert(space_remove(data, fact)); break;
        case 6:
            space_add(other, equation);
            before = space_global_program_epoch();
            space_replace_contents(program, other);
            break;
        case 7:
            before = space_global_program_epoch();
            space_free(program);
            break;
        case 8:
            space_add(data, fact);
            before = space_global_program_epoch();
            space_free(data);
            break;
        }
        bool moved = space_global_program_epoch() != before;
        if (moved != cases[index].program) {
            fprintf(stderr, "FAIL (space): %s %s the program clock\n",
                    cases[index].what, moved ? "moved" : "left");
            exit(1);
        }
        if (moved)
            g_moves++;
        else
            g_stays++;
    }
    space_free(other);
    free(program);
    free(other);
    free(data);
    engine_leave(engine, &scope);
    cetta_embed_engine_release(engine);
}

int main(int argc, char **argv) {
    bool lib_prolog = true;
    for (int index = 1; index < argc; index++) {
        if (strcmp(argv[index], "--lib-prolog") == 0 && index + 1 < argc)
            lib_prolog = strcmp(argv[++index], "0") != 0;
    }
    petta_routes(NULL, lib_prolog);
    petta_routes("extended", false);
    he_routes();
    space_routes();
    printf("PASS: %u program changes move the program clock and %u data "
           "changes leave it, on every route\n", g_moves, g_stays);
    return 0;
}

#include "atom.h"
#include "eval.h"
#include "grounded.h"
#include "library.h"
#include "native_handle.h"
#include "parser.h"
#include "session.h"
#include "space.h"
#include "symbol.h"
#include "term_universe.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool grounded_profile_contract(void) {
    CettaLibraryContext he_prime = {0};
    CettaLibraryContext prime = {0};
    CettaLibraryContext petta = {0};
    cetta_eval_session_init(
        &he_prime.session, CETTA_LANGUAGE_HE, cetta_profile_he_prime());
    cetta_eval_session_init(
        &prime.session, CETTA_LANGUAGE_PRIME, cetta_profile_prime_default());
    cetta_eval_session_init(
        &petta.session, CETTA_LANGUAGE_PETTA, cetta_profile_petta_extended());

    SymbolId typing = symbol_intern_cstr(g_symbols, "normalize-type");
    SymbolId repra = symbol_intern_cstr(g_symbols, "repra");
    SymbolId ordinary = symbol_intern_cstr(g_symbols, "ordinary-constructor");

    eval_set_library_context(NULL);
    bool fallback_ok =
        is_grounded_op(g_builtin_syms.op_plus) &&
        !is_grounded_op(typing) &&
        !is_grounded_op(g_builtin_syms.prime_package) &&
        !is_grounded_op(repra) &&
        !is_grounded_op(ordinary);

    eval_set_library_context(&he_prime);
    bool he_prime_ok =
        is_grounded_op(typing) && is_grounded_op(typing) &&
        !is_grounded_op(g_builtin_syms.prime_package) &&
        !is_grounded_op(repra) && !is_grounded_op(ordinary);

    eval_set_library_context(&prime);
    bool prime_ok =
        is_grounded_op(typing) &&
        is_grounded_op(g_builtin_syms.prime_package) &&
        is_grounded_op(g_builtin_syms.prime_package) &&
        !is_grounded_op(repra) && !is_grounded_op(ordinary);

    eval_set_library_context(&petta);
    bool petta_ok =
        !is_grounded_op(typing) &&
        !is_grounded_op(g_builtin_syms.prime_package) &&
        is_grounded_op(repra) && is_grounded_op(repra) &&
        !is_grounded_op(ordinary);

    eval_set_library_context(&prime);
    bool restored_prime_ok =
        is_grounded_op(typing) &&
        is_grounded_op(g_builtin_syms.prime_package) &&
        !is_grounded_op(repra);

    eval_set_library_context(NULL);
    bool restored_fallback_ok =
        !is_grounded_op(typing) &&
        !is_grounded_op(g_builtin_syms.prime_package) &&
        !is_grounded_op(repra) && !is_grounded_op(ordinary);
    return fallback_ok && he_prime_ok && prime_ok && petta_ok &&
           restored_prime_ok && restored_fallback_ok;
}

static uint32_t declared_type_count(Space *space, Arena *arena, Atom *subject) {
    Atom **types = NULL;
    uint32_t count = space_get_declared_types(space, arena, subject, &types);
    free(types);
    return count;
}

/* HE's grounded operation types follow the active session, not the process:
 * new-space's extended signature appears outside he-compat only, and no
 * grounded type exists outside HE. */
static bool he_grounded_type_session_contract(Space *space, Arena *arena) {
    /* A library context is megabytes: the four live on the heap. */
    CettaLibraryContext *he_extended = calloc(1u, sizeof(*he_extended));
    CettaLibraryContext *he_compat = calloc(1u, sizeof(*he_compat));
    CettaLibraryContext *petta = calloc(1u, sizeof(*petta));
    CettaLibraryContext *prime = calloc(1u, sizeof(*prime));
    if (!he_extended || !he_compat || !petta || !prime) {
        free(he_extended);
        free(he_compat);
        free(petta);
        free(prime);
        return false;
    }
    cetta_eval_session_init(
        &he_extended->session, CETTA_LANGUAGE_HE, cetta_profile_he_extended());
    cetta_eval_session_init(
        &he_compat->session, CETTA_LANGUAGE_HE, cetta_profile_he_compat());
    cetta_eval_session_init(
        &petta->session, CETTA_LANGUAGE_PETTA, cetta_profile_petta_extended());
    cetta_eval_session_init(
        &prime->session, CETTA_LANGUAGE_PRIME, cetta_profile_prime_default());
    Atom *new_space = atom_symbol_id(arena, g_builtin_syms.new_space);
    Atom *plus = atom_symbol_id(arena, g_builtin_syms.op_plus);

    eval_set_library_context(he_extended);
    bool extended_ok = declared_type_count(space, arena, new_space) == 2u &&
        declared_type_count(space, arena, plus) == 1u;
    eval_set_library_context(he_compat);
    bool compat_ok = declared_type_count(space, arena, new_space) == 1u &&
        declared_type_count(space, arena, plus) == 1u;
    eval_set_library_context(petta);
    bool petta_ok = declared_type_count(space, arena, new_space) == 0u &&
        declared_type_count(space, arena, plus) == 0u;
    eval_set_library_context(prime);
    bool prime_ok = declared_type_count(space, arena, plus) == 0u;
    eval_set_library_context(he_extended);
    bool extended_again_ok =
        declared_type_count(space, arena, new_space) == 2u;
    eval_set_library_context(he_compat);
    bool compat_again_ok =
        declared_type_count(space, arena, new_space) == 1u;
    eval_set_library_context(NULL);
    free(he_extended);
    free(he_compat);
    free(petta);
    free(prime);
    return extended_ok && compat_ok && petta_ok && prime_ok &&
           extended_again_ok && compat_again_ok;
}

static Atom *module_atom(Arena *arena, const char *text) {
    size_t pos = 0u;
    return parse_sexpr(arena, text, &pos);
}

/* Render a list of atoms as one space-separated line. */
static void module_render(Arena *arena, Atom *const *atoms, CettaCount count,
                          char *out, size_t out_size) {
    size_t used = 0u;
    out[0] = '\0';
    for (CettaCount i = 0u; i < count && used < out_size; i++) {
        char *text = atom_to_string(arena, atoms[i]);
        used += (size_t)snprintf(out + used, out_size - used, "%s%s",
                                 i ? " " : "", text ? text : "?");
    }
}

static bool module_equation_answers(Space *space, Arena *arena,
                                    const char *query, const char *expected) {
    QueryResults results;
    query_results_init(&results);
    query_equations(space, module_atom(arena, query), arena, &results);
    Atom *answers[16];
    CettaCount count = results.len < 16u ? results.len : 16u;
    for (CettaCount i = 0u; i < count; i++)
        answers[i] = results.items[i].result;
    char rendered[512];
    module_render(arena, answers, count, rendered, sizeof(rendered));
    query_results_free(&results);
    if (strcmp(rendered, expected) != 0)
        fprintf(stderr, "equations of %s: [%s], expected [%s]\n", query,
                rendered, expected);
    return strcmp(rendered, expected) == 0;
}

static bool module_match_answers(Space *space, Arena *arena,
                                 const char *pattern, const char *expected) {
    Atom *query = module_atom(arena, pattern);
    SubstMatchSet matches;
    space_subst_query(space, arena, query, &matches);
    Atom *answers[16];
    CettaCount count = 0u;
    for (CettaIndex i = 0u; i < matches.len && count < 16u; i++) {
        Bindings final_b;
        if (!space_subst_match_with_seed(space, query, &matches.items[i],
                                         NULL, arena, &final_b))
            continue;
        answers[count++] = bindings_apply(&final_b, arena, query);
        bindings_free(&final_b);
    }
    smset_free(&matches);
    char rendered[512];
    module_render(arena, answers, count, rendered, sizeof(rendered));
    if (strcmp(rendered, expected) != 0)
        fprintf(stderr, "match of %s: [%s], expected [%s]\n", pattern,
                rendered, expected);
    return strcmp(rendered, expected) == 0;
}

static bool module_types_are(Space *space, Arena *arena, const char *subject,
                             const char *expected) {
    Atom **types = NULL;
    uint32_t count = space_get_declared_types(
        space, arena, module_atom(arena, subject), &types);
    char rendered[512];
    module_render(arena, types, count, rendered, sizeof(rendered));
    free(types);
    if (strcmp(rendered, expected) != 0)
        fprintf(stderr, "types of %s: [%s], expected [%s]\n", subject,
                rendered, expected);
    return strcmp(rendered, expected) == 0;
}

#define MODULE_CHECK(condition)                                           \
    do {                                                                  \
        if (!(condition)) {                                               \
            fprintf(stderr, "module dependency contract, line %d: %s\n", \
                    __LINE__, #condition);                                \
            return false;                                                 \
        }                                                                 \
    } while (0)

/* HE's ModuleSpace: a space reads its own atoms, then those it imports as a
 * prefix, then each dependency's own atoms in import order.  Listing and
 * mutation see the space's own atoms; a dependency's change advances its
 * importers' revisions; links end with either space and travel with a
 * committed transaction. */
static bool space_module_dependency_contract(void) {
    Arena persistent;
    Arena scratch;
    TermUniverse universe;
    arena_init(&persistent);
    arena_set_runtime_kind(&persistent, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    arena_init(&scratch);
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);

    Space top, a_mod, d_mod;
    space_init_with_universe(&top, &universe);
    space_init_with_universe(&a_mod, &universe);
    space_init_with_universe(&d_mod, &universe);
    /* Each space carries the library as an imported prefix. */
    Space *spaces[3] = {&top, &a_mod, &d_mod};
    for (int i = 0; i < 3; i++) {
        space_add(spaces[i], module_atom(&scratch, "(= (dep-f) lib)"));
        space_add(spaces[i], module_atom(&scratch, "(: dep-g (-> Lib Lib))"));
        MODULE_CHECK(space_mark_imported_prefix(spaces[i]));
    }
    space_add(&top, module_atom(&scratch, "(= (dep-f) top)"));
    space_add(&top, module_atom(&scratch, "(dep-fact top)"));
    space_add(&a_mod, module_atom(&scratch, "(= (dep-f) a)"));
    space_add(&a_mod, module_atom(&scratch, "(dep-fact a)"));
    space_add(&a_mod, module_atom(&scratch, "(: dep-g (-> A A))"));
    space_add(&d_mod, module_atom(&scratch, "(= (dep-f) d)"));
    space_add(&d_mod, module_atom(&scratch, "(dep-fact d)"));

    /* Linking: once per dependency, never to itself; a link advances the
     * importer's revisions and ends its prefix epoch. */
    SpaceReadToken before_link = space_read_token(&top);
    uint64_t global_before_link = space_global_mutation_epoch();
    uint64_t equations_before = space_equation_revision(&top);
    MODULE_CHECK(!space_add_dependency(&top, &top));
    MODULE_CHECK(space_add_dependency(&top, &a_mod));
    MODULE_CHECK(space_add_dependency(&top, &d_mod));
    MODULE_CHECK(space_add_dependency(&top, &a_mod));
    MODULE_CHECK(space_dependency_count(&top) == 2u);
    MODULE_CHECK(space_dependency_at(&top, 0u) == &a_mod);
    MODULE_CHECK(space_dependency_at(&top, 1u) == &d_mod);
    MODULE_CHECK(!space_read_token_is_current(before_link));
    MODULE_CHECK(!space_read_token_prefix_intact(before_link));
    MODULE_CHECK(space_equation_revision(&top) != equations_before);
    MODULE_CHECK(space_global_mutation_epoch() != global_before_link);
    MODULE_CHECK(space_has_dependencies(&top));
    MODULE_CHECK(!space_has_dependencies(&a_mod));

    /* Composed reads: own, imported prefix, then each dependency's own
     * atoms; a dependency's prefix is not read again. */
    MODULE_CHECK(module_equation_answers(&top, &scratch, "(dep-f)",
                                         "top lib a d"));
    MODULE_CHECK(module_match_answers(&top, &scratch, "(dep-fact $x)",
                                      "(dep-fact top) (dep-fact a) "
                                      "(dep-fact d)"));
    /* One parse, so both conjuncts share $x. */
    BindingSet conjunction;
    Atom *conjunct = module_atom(&scratch, "(, (dep-fact $x) (dep-fact $x))");
    Atom *patterns[2] = {conjunct->expr.elems[1], conjunct->expr.elems[2]};
    space_query_conjunction(&top, &scratch, patterns, 2u, NULL, &conjunction);
    MODULE_CHECK(conjunction.len == 3u);
    binding_set_free(&conjunction);
    MODULE_CHECK(module_types_are(&top, &scratch, "dep-g",
                                  "(-> Lib Lib) (-> A A)"));
    SymbolId f_head = symbol_intern_cstr(g_symbols, "dep-f");
    SpaceEquationCursor cursor;
    SpaceEquationOccurrenceId occurrence;
    SpaceEquationOccurrence resolved;
    const char *cursor_order[4] = {"top", "lib", "a", "d"};
    MODULE_CHECK(space_equation_cursor_init(&top, f_head, &cursor));
    for (int i = 0; i < 4; i++) {
        MODULE_CHECK(space_equation_cursor_next(&cursor, &occurrence) ==
                     SPACE_EQUATION_CURSOR_ITEM);
        MODULE_CHECK(space_equation_occurrence_resolve(occurrence, &resolved));
        MODULE_CHECK(atom_eq(resolved.rhs,
                             module_atom(&scratch, cursor_order[i])));
    }
    MODULE_CHECK(space_equation_cursor_next(&cursor, &occurrence) ==
                 SPACE_EQUATION_CURSOR_END);
    /* A module holding only the library, as one of directives does, adds
     * nothing to any read. */
    Space library_only;
    space_init_with_universe(&library_only, &universe);
    space_add(&library_only, module_atom(&scratch, "(= (dep-f) lib)"));
    space_add(&library_only,
              module_atom(&scratch, "(: dep-g (-> Lib Lib))"));
    MODULE_CHECK(space_mark_imported_prefix(&library_only));
    MODULE_CHECK(space_add_dependency(&top, &library_only));
    MODULE_CHECK(space_equation_cursor_init(&top, f_head, &cursor));
    for (int i = 0; i < 4; i++) {
        MODULE_CHECK(space_equation_cursor_next(&cursor, &occurrence) ==
                     SPACE_EQUATION_CURSOR_ITEM);
        MODULE_CHECK(space_equation_occurrence_resolve(occurrence, &resolved));
        MODULE_CHECK(atom_eq(resolved.rhs,
                             module_atom(&scratch, cursor_order[i])));
    }
    MODULE_CHECK(space_equation_cursor_next(&cursor, &occurrence) ==
                 SPACE_EQUATION_CURSOR_END);
    MODULE_CHECK(module_equation_answers(&top, &scratch, "(dep-f)",
                                         "top lib a d"));
    MODULE_CHECK(module_match_answers(&top, &scratch, "(: dep-g $t)",
                                      "(: dep-g (-> Lib Lib)) "
                                      "(: dep-g (-> A A))"));
    MODULE_CHECK(module_types_are(&top, &scratch, "dep-g",
                                  "(-> Lib Lib) (-> A A)"));
    space_free(&library_only);
    MODULE_CHECK(space_dependency_count(&top) == 2u);
    MODULE_CHECK(space_equations_may_match_known_head(&top, f_head));
    MODULE_CHECK(space_single_linear_equation(&top, f_head) == NULL);
    SymbolId g_head = symbol_intern_cstr(g_symbols, "dep-g");
    MODULE_CHECK(space_head_declares_type(&top, g_head));
    MODULE_CHECK(space_head_has_arrow_signature(&top, g_head, 1u));
    MODULE_CHECK(space_view_length64(&top) == 4u + 3u + 2u);
    MODULE_CHECK(atom_eq(space_view_get_at64(&top, 4u),
                         module_atom(&scratch, "(= (dep-f) a)")));
    MODULE_CHECK(atom_eq(space_view_get_at64(&top, 7u),
                         module_atom(&scratch, "(= (dep-f) d)")));
    MODULE_CHECK(space_view_get_at64(&top, 9u) == NULL);

    /* A module-only head is defined through the importer. */
    space_add(&d_mod, module_atom(&scratch, "(= (only-d) yes)"));
    SymbolId only_d = symbol_intern_cstr(g_symbols, "only-d");
    MODULE_CHECK(space_equations_may_match_known_head(&top, only_d));
    MODULE_CHECK(module_equation_answers(&top, &scratch, "(only-d)", "yes"));

    /* Listing, counting and removal see the space's own atoms. */
    MODULE_CHECK(space_length64(&top) == 4u);
    MODULE_CHECK(!space_remove(&top, module_atom(&scratch, "(dep-fact a)")));
    MODULE_CHECK(space_length64(&a_mod) == 5u);

    /* Accelerators over one space's storage decline. */
    SpaceOccurrenceCursor occurrences;
    MODULE_CHECK(!space_occurrence_cursor_init(
        &top, module_atom(&scratch, "(dep-fact $x)"), &occurrences));
    space_occurrence_cursor_release(&occurrences);
    MODULE_CHECK(!space_match_backend_supports_seeded_candidates(&top));
    bool applicable = true;
    (void)space_match_exists_ground_exact(
        &top, module_atom(&scratch, "(dep-fact a)"), &applicable);
    MODULE_CHECK(!applicable);
    uint64_t counted = 0u;
    MODULE_CHECK(!space_match_count_conjunction64(
        &top, &scratch, patterns, 2u, NULL, &counted));
    MODULE_CHECK(space_type_annotation_may_match_subject(
        &top, module_atom(&scratch, "dep-g")));

    /* A dependency's append advances the importer's revisions but keeps
     * its prefix epoch; a removal from it ends that epoch too. */
    SpaceReadToken before_append = space_read_token(&top);
    equations_before = space_equation_revision(&top);
    space_add(&d_mod, module_atom(&scratch, "(= (dep-f) d2)"));
    MODULE_CHECK(!space_read_token_is_current(before_append));
    MODULE_CHECK(space_read_token_prefix_intact(before_append));
    MODULE_CHECK(space_equation_revision(&top) != equations_before);
    MODULE_CHECK(module_equation_answers(&top, &scratch, "(dep-f)",
                                         "top lib a d d2"));
    SpaceReadToken before_removal = space_read_token(&top);
    MODULE_CHECK(space_remove(&d_mod, module_atom(&scratch, "(= (dep-f) d2)")));
    MODULE_CHECK(!space_read_token_prefix_intact(before_removal));
    MODULE_CHECK(module_equation_answers(&top, &scratch, "(dep-f)",
                                         "top lib a d"));

    /* A copy reads through the same dependencies. */
    Space *copy = space_heap_clone_shallow(&top);
    MODULE_CHECK(copy && space_dependency_count(copy) == 2u);
    MODULE_CHECK(space_imported_length(copy) == 2u);
    MODULE_CHECK(module_equation_answers(copy, &scratch, "(dep-f)",
                                         "top lib a d"));
    /* A transaction keeps the imports made inside it when committed. */
    Space extra;
    space_init_with_universe(&extra, &universe);
    space_add(&extra, module_atom(&scratch, "(= (dep-f) extra)"));
    MODULE_CHECK(space_add_dependency(copy, &extra));
    space_replace_contents(&top, copy);
    MODULE_CHECK(space_dependency_count(&top) == 3u);
    MODULE_CHECK(space_dependency_count(copy) == 0u);
    MODULE_CHECK(module_equation_answers(&top, &scratch, "(dep-f)",
                                         "top lib a d extra"));
    space_free(copy);
    free(copy);
    /* A discarded copy drops its links with it. */
    copy = space_heap_clone_shallow(&top);
    MODULE_CHECK(copy && space_dependency_count(copy) == 3u);
    space_free(copy);
    free(copy);
    MODULE_CHECK(space_dependency_count(&top) == 3u);
    space_add(&extra, module_atom(&scratch, "(= (dep-f) extra2)"));
    MODULE_CHECK(module_equation_answers(&top, &scratch, "(dep-f)",
                                         "top lib a d extra extra2"));
    /* A rebuild from a fresh copy, as a backend change or a stable removal
     * does, keeps the space's links. */
    Space fresh;
    space_init_with_universe(&fresh, &universe);
    for (CettaIndex i = 0u; i < space_length64(&top); i++)
        space_add_atom_id(&fresh, space_get_atom_id_at64(&top, i));
    space_replace_contents(&top, &fresh);
    space_free(&fresh);
    MODULE_CHECK(space_dependency_count(&top) == 3u);
    MODULE_CHECK(module_equation_answers(&top, &scratch, "(dep-f)",
                                         "top lib a d extra extra2"));

    /* An overlay reads its root's dependencies. */
    Space overlay;
    space_init_overlay(&overlay, &top);
    space_add(&overlay, module_atom(&scratch, "(= (dep-f) scratch)"));
    MODULE_CHECK(space_has_dependencies(&overlay));
    MODULE_CHECK(module_equation_answers(
        &overlay, &scratch, "(dep-f)",
        "top scratch lib a d extra extra2"));
    MODULE_CHECK(module_match_answers(
        &overlay, &scratch, "(dep-fact $x)",
        "(dep-fact top) (dep-fact a) (dep-fact d)"));
    Space nested_overlay;
    space_init_overlay(&nested_overlay, &overlay);
    MODULE_CHECK(module_match_answers(
        &nested_overlay, &scratch, "(dep-fact $x)",
        "(dep-fact top) (dep-fact a) (dep-fact d)"));
    space_add(&d_mod, module_atom(&scratch, "(= (dep-effect) 42)"));
    SpaceProgramToken overlay_program = space_program_token(&nested_overlay);
    SpaceEquationToken overlay_equations = space_equation_token(&nested_overlay);
    SpaceReadToken overlay_read = space_read_token(&nested_overlay);
    SymbolId effect_head = symbol_intern_cstr(g_symbols, "dep-effect");
    MODULE_CHECK(space_query_effect_for_head(&nested_overlay, effect_head, NULL) ==
                 CETTA_GSLT_QUERY_EFFECT_PURE);
    space_add(&d_mod, module_atom(&scratch,
                       "(= (dep-effect) (match &self (dep-fact $x) $x))"));
    MODULE_CHECK(!space_program_token_is_current(overlay_program));
    MODULE_CHECK(!space_equation_token_is_current(overlay_equations));
    MODULE_CHECK(!space_read_token_is_current(overlay_read));
    MODULE_CHECK(!space_read_token_matches_live_space(overlay_read,
                                                      &nested_overlay));
    MODULE_CHECK(space_read_token_prefix_intact(overlay_read));
    MODULE_CHECK(space_query_effect_for_head(&nested_overlay, effect_head, NULL) !=
                 CETTA_GSLT_QUERY_EFFECT_PURE);
    MODULE_CHECK(space_remove(&d_mod, module_atom(&scratch,
                       "(= (dep-effect) (match &self (dep-fact $x) $x))")));
    MODULE_CHECK(space_remove(&d_mod,
                              module_atom(&scratch, "(= (dep-effect) 42)")));
    MODULE_CHECK(!space_read_token_prefix_intact(overlay_read));
    space_free(&nested_overlay);
    space_free(&overlay);

    /* A join may use a different module for each conjunct. Duplicate
     * occurrences contribute independently, rather than being coalesced. */
    Space join_top, join_left, join_right;
    space_init_with_universe(&join_top, &universe);
    space_init_with_universe(&join_left, &universe);
    space_init_with_universe(&join_right, &universe);
    space_add(&join_left, module_atom(&scratch, "(dep-left shared)"));
    space_add(&join_left, module_atom(&scratch, "(dep-left shared)"));
    space_add(&join_right, module_atom(&scratch, "(dep-right shared)"));
    space_add(&join_right, module_atom(&scratch, "(dep-right shared)"));
    MODULE_CHECK(space_add_dependency(&join_top, &join_left));
    MODULE_CHECK(space_add_dependency(&join_top, &join_right));
    conjunct = module_atom(&scratch, "(, (dep-left $x) (dep-right $x))");
    patterns[0] = conjunct->expr.elems[1];
    patterns[1] = conjunct->expr.elems[2];
    space_query_conjunction(&join_top, &scratch, patterns, 2u, NULL,
                             &conjunction);
    MODULE_CHECK(conjunction.len == 4u);
    binding_set_free(&conjunction);
    MODULE_CHECK(space_remove(&join_right,
                              module_atom(&scratch, "(dep-right shared)")));
    MODULE_CHECK(space_remove(&join_right,
                              module_atom(&scratch, "(dep-right shared)")));
    space_add(&join_right, module_atom(&scratch, "(dep-right other)"));
    space_query_conjunction(&join_top, &scratch, patterns, 2u, NULL,
                             &conjunction);
    MODULE_CHECK(conjunction.len == 0u);
    binding_set_free(&conjunction);
    space_free(&join_top);
    space_free(&join_left);
    space_free(&join_right);

    /* Freeing a dependency first ends its link and advances the importer. */
    SpaceReadToken before_free = space_read_token(&top);
    uint64_t global_before_free = space_global_mutation_epoch();
    space_free(&extra);
    MODULE_CHECK(space_dependency_count(&top) == 2u);
    MODULE_CHECK(!space_read_token_prefix_intact(before_free));
    MODULE_CHECK(space_global_mutation_epoch() != global_before_free);
    MODULE_CHECK(module_equation_answers(&top, &scratch, "(dep-f)",
                                         "top lib a d"));
    /* Freeing the importer first leaves the modules without importers. */
    space_free(&top);
    space_add(&a_mod, module_atom(&scratch, "(dep-fact a2)"));
    space_free(&a_mod);
    space_free(&d_mod);

    term_universe_free(&universe);
    arena_free(&scratch);
    arena_free(&persistent);
    return true;
}

/* Reimport is a no-op even if the loaded module subsequently acquires a
 * new dependency. HE records the first import's flattened dependency list. */
static bool he_repeated_import_contract(void) {
    Arena persistent;
    Arena scratch;
    TermUniverse universe;
    Registry registry;
    Space top;
    Space later;
    CettaLibraryContext *ctx = calloc(1u, sizeof(*ctx));
    if (!ctx)
        return false;
    arena_init(&persistent);
    arena_set_runtime_kind(&persistent, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    arena_init(&scratch);
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);
    registry_init(&registry);
    space_init_with_universe(&top, &universe);
    space_init_with_universe(&later, &universe);
    registry_bind_id(&registry, g_builtin_syms.self,
                     atom_space(&persistent, &top));
    cetta_library_context_init_for_language_profile(
        ctx, CETTA_LANGUAGE_HE, cetta_profile_he_compat());
    cetta_library_context_set_script_path(
        ctx, "tests/he/module_import_v1/main.metta");
    cetta_library_set_top_module_space(ctx, &top);
    Atom *error = NULL;
    Space *module = NULL;
    bool ok = cetta_library_import_module(
        ctx, "mi_dmod", &top, false, &scratch, &persistent, &registry,
        1000, NULL, &error);
    /* Rollback bookkeeping is allocated before module code can expose its
     * space. Retiring failed handles must not allocate after that point. */
    ok = ok && ctx->retired_module_cap >=
        ctx->retired_module_len + ctx->loaded_module_len;
    if (ok && space_dependency_count(&top) == 1u) {
        module = space_dependency_at(&top, 0u);
        space_add(&later, module_atom(&scratch, "(reimport-fact later)"));
        ok = space_add_dependency(module, &later);
    } else {
        ok = false;
    }
    if (ok) {
        SpaceReadToken before = space_read_token(&top);
        uint64_t global_before = space_global_mutation_epoch();
        ok = cetta_library_import_module(
            ctx, "mi_dmod", &top, false, &scratch, &persistent, &registry,
            1000, NULL, &error) &&
            space_dependency_count(&top) == 1u &&
            space_dependency_at(&top, 0u) == module &&
            space_read_token_is_current(before) &&
            space_global_mutation_epoch() == global_before &&
            module_match_answers(&top, &scratch, "(reimport-fact $x)", "");
    }
    for (uint32_t attempt = 0u; ok && attempt < 12u; attempt++) {
        uint32_t retired = ctx->retired_module_len;
        uint32_t loaded = ctx->loaded_module_len;
        error = NULL;
        ok = !cetta_library_import_module(
            ctx, "mi_badparse", &top, false, &scratch, &persistent,
            &registry, 1000, NULL, &error) && error &&
            ctx->loaded_module_len == loaded &&
            ctx->retired_module_len == retired + 1u &&
            ctx->retired_module_cap >=
                ctx->retired_module_len + ctx->loaded_module_len &&
            space_dependency_count(&top) == 1u;
        if (ok) {
            Space *failed = ctx->retired_modules[retired].space;
            ok = failed && ctx->retired_modules[retired].module_name &&
                space_length64(failed) == 0u &&
                space_dependency_count(failed) == 0u;
        }
    }
    cetta_library_context_free(ctx);
    free(ctx);
    registry_free(&registry);
    space_free(&top);
    space_free(&later);
    term_universe_free(&universe);
    arena_free(&scratch);
    arena_free(&persistent);
    return ok;
}

/* Keep an open outcome while entering each public evaluator boundary. A
 * nested invocation must not reset its caller's variant bank, and disabling
 * further factoring must not invalidate an already factored outcome. */
static bool execution_owner_nesting_contract(void) {
    CettaLibraryContext *context = calloc(1u, sizeof(*context));
    if (!context)
        return false;
    cetta_eval_session_init(&context->session, CETTA_LANGUAGE_HE,
                            cetta_profile_he_extended());
    CettaEvalOptionEntry *sharing = &context->session.options.entries[0];
    strcpy(sharing->key, "outcome-variant-sharing");
    sharing->kind = CETTA_EVAL_OPTION_VALUE_INT;
    sharing->int_value = 1;
    context->session.options.entry_len = 1u;
    eval_set_library_context(context);

    Arena caller, source, receiver;
    arena_init_detached(&caller);
    arena_init_detached(&source);
    arena_init_detached(&receiver);
    Space space;
    space_init(&space);
    Registry registry;
    registry_init(&registry);
    Bindings empty;
    bindings_init(&empty);
    OutcomeSet suspended;
    outcome_set_init(&suspended);
    Atom *expected = module_atom(&caller, "(Held $x (Payload $x))");
    outcome_set_add(&suspended, expected, &empty);
    bool ok = suspended.len == 1u &&
        variant_instance_present(&suspended.items[0].variant);

    for (unsigned entry = 0u; ok && entry < 3u; entry++) {
        ArenaMark source_start = arena_mark(&source);
        Atom *input = module_atom(&source, "(Nested $y)");
        ResultSet results;
        result_set_init(&results);
        Atom *prefix = atom_symbol(&caller, "ExistingAnswer");
        result_set_add(&results, prefix);
        if (entry == 0u)
            eval_top(&space, &receiver, input, &results);
        else if (entry == 1u)
            eval_top_one_step(&space, &receiver, input, &results);
        else
            eval_top_with_registry(&space, &receiver, &caller,
                                    &registry, input, &results);
        ok = results.len == 2u && results.items[0] == prefix &&
            atom_graph_is_closed_for_arena(&receiver, results.items[1]);
        arena_reset(&source, source_start);
        if (ok) {
            char *text = atom_to_string(&receiver, results.items[1]);
            ok = text && strcmp(text, "(Nested $y)") == 0;
        }
        result_set_free(&results);
        if (ok) {
            Outcome *held = &suspended.items[0];
            Atom *restored = variant_instance_materialize(
                &receiver, held->atom, &held->variant);
            ok = restored && atom_eq(restored, expected);
        }
    }

    sharing->int_value = 0;
    OutcomeSet unfactored;
    outcome_set_init(&unfactored);
    outcome_set_add(&unfactored, expected, &empty);
    ok = ok && unfactored.len == 1u &&
        !variant_instance_present(&unfactored.items[0].variant);
    if (ok) {
        Outcome *held = &suspended.items[0];
        Atom *restored = variant_instance_materialize(
            &receiver, held->atom, &held->variant);
        ok = restored && atom_eq(restored, expected);
    }
    outcome_set_free(&unfactored);
    outcome_set_free(&suspended);
    bindings_free(&empty);
    eval_release_temporary_spaces();
    registry_free(&registry);
    space_free(&space);
    arena_free(&source);
    arena_free(&receiver);
    arena_free(&caller);
    eval_set_library_context(NULL);
    free(context);
    return ok;
}

static unsigned byte_payload_released;
static const char *byte_payload_kind = "test.immutable-byte-payload";

static void byte_payload_release(void *payload) {
    byte_payload_released++;
    free(payload);
}

/* A capture is an owned input plus a checked span. Its syntax is just test
 * data: neither matching nor publication needs a regex or tensor backend. */
static const unsigned char *byte_view_read(CettaLibraryContext *context,
                                          Atom *view, size_t size) {
    uint64_t id;
    if (!view || view->kind != ATOM_EXPR || view->expr.len != 4u ||
        !cetta_native_handle_arg(view->expr.elems[1], byte_payload_kind, &id))
        return NULL;
    Atom *offset = view->expr.elems[2], *length = view->expr.elems[3];
    if (offset->kind != ATOM_GROUNDED || offset->ground.gkind != GV_INT ||
        length->kind != ATOM_GROUNDED || length->ground.gkind != GV_INT ||
        offset->ground.ival < 0 || length->ground.ival < 0 ||
        (uint64_t)offset->ground.ival > size ||
        (uint64_t)length->ground.ival > size - (uint64_t)offset->ground.ival)
        return NULL;
    const unsigned char *bytes = cetta_native_handle_get(context, byte_payload_kind, id);
    return bytes ? bytes + offset->ground.ival : NULL;
}

static bool match_owned_byte_view_contract(void) {
    const size_t size = 65536u;
    const unsigned char expected[] = {'a', 0, 'b', 'c', 'd'};
    for (unsigned language = 0u; language < 2u; language++) {
        CettaLibraryContext *context = calloc(1u, sizeof(*context));
        if (!context)
            return false;
        cetta_eval_session_init(&context->session,
            language ? CETTA_LANGUAGE_PETTA : CETTA_LANGUAGE_HE,
            language ? cetta_profile_petta_extended() : cetta_profile_he_extended());
        context->native_handle_next_id = 1u;
        eval_set_library_context(context);
        Arena source, receiver, sibling;
        arena_init_detached(&source);
        arena_init_detached(&receiver);
        arena_init_detached(&sibling);
        ArenaMark source_start = arena_mark(&source);
        ArenaMark receiver_start = arena_mark(&receiver);
        ArenaMark sibling_start = arena_mark(&sibling);
        bool ok = true;
        /* A fixed live frontier must not retain abandoned payloads across
         * successive queries. Different offsets alias one large buffer. */
        for (unsigned iteration = 0u; ok && iteration < 32u; iteration++) {
            Space space;
            TermUniverse stored;
            term_universe_init(&stored);
            term_universe_set_persistent_arena(&stored, &source);
            space_init_with_universe(&space, &stored);
            unsigned char *bytes = malloc(size);
            if (!bytes)
                abort();
            memset(bytes, 'x', size);
            memcpy(bytes + 17u, expected, sizeof(expected));
            uint64_t id;
            if (!cetta_native_handle_alloc(context, byte_payload_kind, bytes,
                                           byte_payload_release, &id))
                abort();
            Atom *handle = cetta_native_handle_owned_atom(
                context, &source, byte_payload_kind, id);
            Atom *view = atom_expr(&source, (Atom *[]){
                atom_symbol(&source, "ByteSpan"), handle,
                atom_int(&source, 17), atom_int(&source, sizeof(expected))}, 4u);
            Atom *shifted = atom_expr(&source, (Atom *[]){
                view->expr.elems[0], handle, atom_int(&source, 18),
                view->expr.elems[3]}, 4u);
            Atom *tag = atom_symbol(&source, "capture-view");
            Atom *key = atom_symbol(&source, "input");
            Atom *fact = atom_expr(&source, (Atom *[]){tag, key, view, view}, 4u);
            space_add(&space, fact);
            space_add(&space, fact);
            space_add(&space, atom_expr(&source,
                (Atom *[]){tag, key, view, shifted}, 4u));
            space_add(&space, module_atom(&source, "(request input)"));
            Atom *query = module_atom(&source,
                "(match &self (request $key) "
                "(match &self (capture-view $key $v $v) (Pair $v $v)))");
            ResultSet results;
            result_set_init(&results);
            unsigned before_release = byte_payload_released;
            eval_top(&space, &receiver, query, &results);
            ok = results.len == 2u;
            if (!ok)
                fprintf(stderr, "byte-view answers: %" PRIu64 "\n", results.len);
            Atom *saved = ok ? atom_deep_copy(&sibling, results.items[0]) : NULL;
            space_free(&space);
            term_universe_free(&stored);
            arena_reset(&source, source_start);
            ok = ok && byte_payload_released == before_release;
            for (CettaCount i = 0u; ok && i < results.len; i++) {
                Atom *pair = results.items[i];
                ok = pair->kind == ATOM_EXPR && pair->expr.len == 3u;
                for (unsigned side = 1u; ok && side <= 2u; side++) {
                    const unsigned char *span = byte_view_read(
                        context, pair->expr.elems[side], size);
                    ok = span == bytes + 17u &&
                        memcmp(span, expected, sizeof(expected)) == 0;
                    if (!ok) {
                        fprintf(stderr, "byte-view result %" PRIu64 " side %u: ", i, side);
                        atom_print(pair->expr.elems[side], stderr);
                        fputc('\n', stderr);
                    }
                }
            }
            result_set_free(&results);
            arena_reset(&receiver, receiver_start);
            ok = ok && byte_payload_released == before_release;
            if (ok) {
                const unsigned char *span = byte_view_read(
                    context, saved->expr.elems[1], size);
                ok = span == bytes + 17u &&
                    memcmp(span, expected, sizeof(expected)) == 0;
            }
            arena_reset(&sibling, sibling_start);
            if (ok && byte_payload_released == before_release)
                fprintf(stderr, "byte-view retained after all three arenas reset\n");
            ok = ok && byte_payload_released == before_release + 1u &&
                !cetta_native_handle_get(context, byte_payload_kind, id);
            if (!ok)
                fprintf(stderr, "byte-view failure: language %u iteration %u releases %u -> %u\n",
                        language, iteration, before_release, byte_payload_released);
        }
        eval_release_temporary_spaces();
        arena_free(&source);
        arena_free(&receiver);
        arena_free(&sibling);
        cetta_native_handle_cleanup_all(context);
        cetta_library_context_free(context);
        eval_set_library_context(NULL);
        free(context);
        if (!ok)
            return false;
    }
    return true;
}

typedef struct {
    unsigned holds;
    bool armed;
    bool entered;
    bool ok;
    Space *space;
} ReentrantHold;

static void reentrant_hold_retain(void *owner) {
    ReentrantHold *hold = owner;
    hold->holds++;
    if (!hold->armed || hold->entered)
        return;
    hold->entered = true;
    Arena scratch;
    arena_init_detached(&scratch);
    Bindings empty;
    bindings_init(&empty);
    OutcomeSet suspended;
    outcome_set_init(&suspended);
    Atom *original = module_atom(&scratch, "(CallbackHeld $x $x)");
    outcome_set_add(&suspended, original, &empty);
    hold->ok = suspended.len == 1u &&
        variant_instance_present(&suspended.items[0].variant);
    ResultSet child;
    result_set_init(&child);
    eval_top(hold->space, &scratch, atom_int(&scratch, 7), &child);
    /* Import and Python callbacks perform this cleanup after nested entry. */
    eval_release_temporary_spaces();
    hold->ok = hold->ok && child.len == 1u &&
        atom_eq(child.items[0], atom_int(&scratch, 7));
    if (hold->ok) {
        Outcome *pending = &suspended.items[0];
        Atom *restored = variant_instance_materialize(
            &scratch, pending->atom, &pending->variant);
        hold->ok = restored && atom_eq(restored, original);
    }
    result_set_free(&child);
    outcome_set_free(&suspended);
    bindings_free(&empty);
    arena_free(&scratch);
}

static void reentrant_hold_release(void *owner) {
    ReentrantHold *hold = owner;
    if (hold->holds == 0u)
        abort();
    hold->holds--;
}

static bool execution_owner_callback_contract(void) {
    CettaLibraryContext *context = calloc(1u, sizeof(*context));
    if (!context)
        return false;
    cetta_eval_session_init(&context->session, CETTA_LANGUAGE_HE,
                            cetta_profile_he_extended());
    CettaEvalOptionEntry *sharing = &context->session.options.entries[0];
    strcpy(sharing->key, "outcome-variant-sharing");
    sharing->kind = CETTA_EVAL_OPTION_VALUE_INT;
    sharing->int_value = 1;
    context->session.options.entry_len = 1u;
    eval_set_library_context(context);
    Arena source, receiver;
    arena_init_detached(&source);
    arena_init_detached(&receiver);
    Space space;
    space_init(&space);
    ReentrantHold hold = {.space = &space};
    Atom *value = atom_native_handle_identifier(
        &source, 42, &hold, reentrant_hold_retain, reentrant_hold_release);
    hold.armed = true;
    ResultSet results;
    result_set_init(&results);
    eval_top(&space, &receiver, value, &results);
    bool ok = hold.entered && hold.ok && results.len == 1u &&
        atom_eq(results.items[0], value);
    result_set_free(&results);
    arena_free(&source);
    ok = ok && hold.holds > 0u;
    arena_free(&receiver);
    ok = ok && hold.holds == 0u;
    eval_release_temporary_spaces();
    space_free(&space);
    eval_set_library_context(NULL);
    free(context);
    return ok;
}

int main(void) {
    int rc = 1;
    Arena arena;
    Arena eval_arena;
    TermUniverse universe;
    Space space;
    SymbolTable symbols;
    VarInternTable var_intern;
    ResultSet rs;
    Atom *expr = NULL;
    size_t pos = 0;

    arena_init(&arena);
    arena_set_runtime_kind(&arena, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    arena_init(&eval_arena);
    arena_set_runtime_kind(&eval_arena, CETTA_ARENA_RUNTIME_KIND_EVAL);
    term_universe_init(&universe);
    space_init_with_universe(&space, &universe);
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&var_intern);
    g_symbols = &symbols;
    g_var_intern = &var_intern;
    eval_set_library_context(NULL);

    if (!grounded_profile_contract()) {
        fprintf(stderr, "grounded operator profile contract failure\n");
        goto cleanup;
    }
    if (!he_grounded_type_session_contract(&space, &arena)) {
        fprintf(stderr, "HE grounded type session contract failure\n");
        goto cleanup;
    }
    if (!space_module_dependency_contract()) {
        fprintf(stderr, "space module dependency contract failure\n");
        goto cleanup;
    }
    if (!he_repeated_import_contract()) {
        fprintf(stderr, "HE repeated import contract failure\n");
        goto cleanup;
    }
    if (!execution_owner_nesting_contract()) {
        fprintf(stderr, "nested execution ownership contract failure\n");
        goto cleanup;
    }
    if (!match_owned_byte_view_contract()) {
        fprintf(stderr, "owned byte-view publication contract failure\n");
        goto cleanup;
    }
    if (!execution_owner_callback_contract()) {
        fprintf(stderr, "reentrant execution cleanup contract failure\n");
        goto cleanup;
    }

    expr = parse_sexpr(&arena, "(once (superpose (1 2)))", &pos);
    if (!expr) {
        fprintf(stderr, "parse failure\n");
        goto cleanup;
    }

    result_set_init(&rs);
    eval_top(&space, &eval_arena, expr, &rs);
    if (rs.len != 2) {
        fprintf(stderr, "unexpected result count: %" PRIu64 "\n", rs.len);
        result_set_free(&rs);
        goto cleanup;
    }

    char *rendered = atom_to_string(&arena, rs.items[0]);
    char *rendered2 = atom_to_string(&arena, rs.items[1]);
    if (!rendered || !rendered2) {
        fprintf(stderr, "render failure\n");
        result_set_free(&rs);
        goto cleanup;
    }
    if (!((strcmp(rendered, "(once 1)") == 0 &&
           strcmp(rendered2, "(once 2)") == 0) ||
          (strcmp(rendered, "(once 2)") == 0 &&
           strcmp(rendered2, "(once 1)") == 0))) {
        fprintf(stderr, "unexpected fallback results: [%s, %s]\n",
                rendered, rendered2);
        result_set_free(&rs);
        goto cleanup;
    }

    printf("[%s, %s]", rendered, rendered2);
    result_set_free(&rs);
    rc = 0;

cleanup:
    g_var_intern = NULL;
    g_symbols = NULL;
    var_intern_free(&var_intern);
    symbol_table_free(&symbols);
    space_free(&space);
    term_universe_free(&universe);
    arena_free(&eval_arena);
    arena_free(&arena);
    return rc;
}

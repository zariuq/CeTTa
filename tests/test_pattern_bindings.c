#include "match.h"
#include "term_graph.h"
#include "term_canon.h"
#include "binding/slot_store_internal.h"
#include "variant_shape.h"
#if defined(CETTA_PATTERN_ACTUAL_PRIME_HOOKS)
#include "prime_semantics.h"
#include "eval.h"
#include "library.h"
#include "parser.h"
#include "symbol.h"
#else
#include "tests/test_runtime_stats_stubs.h"
#endif

#include <assert.h>
#include <stdio.h>

#if defined(CETTA_PATTERN_ACTUAL_PRIME_HOOKS)
static bool interpretation_fail_calloc, interpretation_fail_malloc;
static size_t interpretation_fail_malloc_after;
void *__real_calloc(size_t count, size_t bytes);
void *__real_malloc(size_t bytes);
void *__wrap_calloc(size_t count, size_t bytes) {
    if (interpretation_fail_calloc) { interpretation_fail_calloc = false; return NULL; }
    return __real_calloc(count, bytes);
}
void *__wrap_malloc(size_t bytes) {
    if (interpretation_fail_malloc) { interpretation_fail_malloc = false; return NULL; }
    if (interpretation_fail_malloc_after && !--interpretation_fail_malloc_after)
        return NULL;
    return __real_malloc(bytes);
}

static int interpretation_read(CettaLibraryContext *context, const char *text) {
    Arena arena; arena_init_detached(&arena);
    TermUniverse universe; term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &arena);
    AtomId *ids = NULL; char error[512];
    CettaLibraryContext *previous = eval_swap_library_context(context);
    int count = prime_semantics_read_source_text_ids(NULL, text, &universe,
        &ids, error, sizeof(error));
    eval_swap_library_context(previous);
    free(ids); term_universe_free(&universe); arena_free(&arena);
    return count;
}

static bool interpretation_inner(CettaLibraryContext *context, const char *head) {
    Arena arena; arena_init_detached(&arena);
    char text[256];
    snprintf(text, sizeof(text), "(let $x outer (%s seed $x $x))", head);
    size_t position = 0u;
    CettaLibraryContext *previous = eval_swap_library_context(context);
    Atom *form = parse_sexpr(&arena, text, &position);
    Atom *query = prime_semantics_elaborate_form(&arena, form);
    assert(query && query->kind == ATOM_EXPR && query->expr.len >= 4u);
    Atom *outer = query->expr.elems[1], *body = query->expr.elems[3];
    assert(outer->kind == ATOM_VAR && body->kind == ATOM_EXPR && body->expr.len == 4u &&
        body->expr.elems[2]->kind == ATOM_VAR && body->expr.elems[3]->kind == ATOM_VAR &&
        var_id_eq(body->expr.elems[2]->var_id, body->expr.elems[3]->var_id));
    bool inner = !var_id_eq(outer->var_id, body->expr.elems[2]->var_id);
    eval_swap_library_context(previous); arena_free(&arena);
    return inner;
}

static Atom *interpretation_width_declaration(Arena *arena, const char *head,
                                             size_t values, bool repeat) {
    size_t count = values + 3u + (repeat ? 1u : 0u);
    Atom **items = arena_alloc(arena, count * sizeof(*items));
    assert(items);
    items[0] = atom_symbol(arena, head);
    for (size_t i = 0u; i < values; i++) items[i + 1u] = atom_symbol(arena, "value");
    items[values + 1u] = atom_symbol(arena, "pattern");
    items[values + 2u] = atom_symbol(arena, "body");
    if (repeat) items[count - 1u] = atom_symbol(arena, "...");
    return atom_expr2(arena, atom_symbol(arena, "scope:binder"),
        atom_expr(arena, items, count));
}

static void interpretation_width_check(CettaLibraryContext *context,
        const char *head, size_t values, size_t repeats, bool binds) {
    Arena arena; arena_init_detached(&arena);
    Atom *x = atom_var(&arena, "width-x");
    size_t count = 1u + repeats * (values + 2u);
    Atom **items = arena_alloc(&arena, count * sizeof(*items));
    assert(items);
    items[0] = atom_symbol(&arena, head);
    for (size_t r = 0u; r < repeats; r++) {
        size_t start = 1u + r * (values + 2u);
        for (size_t i = 0u; i < values; i++) items[start + i] = x;
        items[start + values] = items[start + values + 1u] = x;
    }
    Atom *body = atom_expr(&arena, items, count);
    Atom *form = atom_expr(&arena, (Atom *[]){atom_symbol(&arena, "let"),
        x, atom_symbol(&arena, "outer"), body}, 4u);
    CettaLibraryContext *previous = eval_swap_library_context(context);
    Atom *out = prime_semantics_elaborate_form(&arena, form);
    if (!out || out->kind != ATOM_EXPR || out->expr.len < 4u)
        fprintf(stderr, "width elaboration: %s values=%zu repeats=%zu binds=%d: %s\n",
            head, values, repeats, binds, prime_semantics_elaboration_error());
    assert(out && out->kind == ATOM_EXPR && out->expr.len >= 4u);
    Atom *outer = out->expr.elems[1], *inner = out->expr.elems[3];
    assert(outer->kind == ATOM_VAR && inner->kind == ATOM_EXPR && inner->expr.len == count);
    for (size_t r = 0u; r < repeats; r++) {
        size_t start = 1u + r * (values + 2u);
        for (size_t i = 0u; i < values; i++)
            assert(inner->expr.elems[start + i]->kind == ATOM_VAR &&
                var_id_eq(outer->var_id, inner->expr.elems[start + i]->var_id));
        Atom *pattern = inner->expr.elems[start + values];
        Atom *use = inner->expr.elems[start + values + 1u];
        assert(pattern->kind == ATOM_VAR && use->kind == ATOM_VAR &&
            var_id_eq(pattern->var_id, use->var_id));
        assert(var_id_eq(outer->var_id, pattern->var_id) == !binds);
    }
    eval_swap_library_context(previous);
    arena_free(&arena);
}

static void interpretation_width_controls(void) {
    VarInternTable variables; var_intern_init(&variables);
    VarInternTable *previous_variables = g_var_intern; g_var_intern = &variables;
    CettaLibraryContext *previous = eval_current_library_context();
    CettaLibraryContext *source = calloc(1u, sizeof(*source));
    CettaLibraryContext *copy = calloc(1u, sizeof(*copy));
    assert(source && copy);
    cetta_library_context_init_for_language_profile(source, CETTA_LANGUAGE_PRIME,
        cetta_profile_prime_default());
    eval_swap_library_context(source);
    Arena arena; arena_init_detached(&arena);
    TermUniverse universe; term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &arena);
    static const size_t widths[] = {0u, 5u, 6u, 12u, 65u, 257u};
    for (size_t i = 0u; i < sizeof(widths) / sizeof(*widths); i++) {
        char head[64]; snprintf(head, sizeof(head), "width-pick-%zu", widths[i]);
        AtomId id = term_universe_store_atom_id(&universe, &arena,
            interpretation_width_declaration(&arena, head, widths[i], false));
        assert(id != CETTA_ATOM_ID_NONE &&
            prime_semantics_elaborate_read_forms(&universe, &id, 1u));
        interpretation_width_check(source, head, widths[i], 1u, true);
        interpretation_width_check(source, head, widths[i] + 1u, 1u, false);
    }
    AtomId repeating = term_universe_store_atom_id(&universe, &arena,
        interpretation_width_declaration(&arena, "width-repeat", 257u, true));
    assert(repeating != CETTA_ATOM_ID_NONE &&
        prime_semantics_elaborate_read_forms(&universe, &repeating, 1u));
    interpretation_width_check(source, "width-repeat", 257u, 2u, true);

    Atom *word_declaration = interpretation_width_declaration(&arena,
        "width-word", 257u, false);
    Atom **word_roles = arena_alloc(&arena, 260u * sizeof(*word_roles));
    assert(word_roles);
    memcpy(word_roles, word_declaration->expr.elems[1]->expr.elems,
        260u * sizeof(*word_roles));
    word_roles[257] = atom_symbol(&arena, "marker");
    word_declaration = atom_expr2(&arena, atom_symbol(&arena, "scope:binder"),
        atom_expr(&arena, word_roles, 260u));
    AtomId word_id = term_universe_store_atom_id(&universe, &arena, word_declaration);
    assert(prime_semantics_elaborate_read_forms(&universe, &word_id, 1u));
    interpretation_width_check(source, "width-word", 257u, 1u, false);

    Atom **malformed_roles = arena_alloc(&arena, 260u * sizeof(*malformed_roles));
    assert(malformed_roles);
    memcpy(malformed_roles, word_roles, 260u * sizeof(*malformed_roles));
    malformed_roles[0] = atom_symbol(&arena, "width-malformed");
    malformed_roles[257] = atom_int(&arena, 7);
    AtomId malformed_id = term_universe_store_atom_id(&universe, &arena,
        atom_expr2(&arena, atom_symbol(&arena, "scope:binder"),
            atom_expr(&arena, malformed_roles, 260u)));
    CettaPrimeInterpretation *malformed_before = prime_interpretation_capture(source->prime_interpretation);
    interpretation_fail_malloc = true;
    assert(prime_semantics_elaborate_read_forms(&universe, &malformed_id, 1u) &&
        interpretation_fail_malloc);
    interpretation_fail_malloc = false;
    CettaPrimeInterpretation *malformed_after = prime_interpretation_capture(source->prime_interpretation);
    assert(malformed_after == malformed_before);
    prime_interpretation_release(malformed_before);
    prime_interpretation_release(malformed_after);
    interpretation_width_check(source, "width-malformed", 257u, 1u, false);

    Atom *source_declaration = interpretation_width_declaration(&arena,
        "width-entry", 257u, false);
    Atom **source_roles = arena_alloc(&arena, 260u * sizeof(*source_roles));
    assert(source_roles);
    memcpy(source_roles, source_declaration->expr.elems[1]->expr.elems,
        260u * sizeof(*source_roles));
    source_roles[259] = atom_symbol(&arena, "value");
    Atom *source_shape = atom_expr(&arena, source_roles, 260u);
    AtomId source_id = term_universe_store_atom_id(&universe, &arena,
        atom_expr3(&arena, atom_symbol(&arena, "scope:source"),
            atom_symbol(&arena, "width-join"), source_shape));
    assert(prime_semantics_elaborate_read_forms(&universe, &source_id, 1u) &&
        interpretation_read(source, "(scope:binder (width-join source body))") == 1);
    Atom *source_x = atom_var(&arena, "source-width-x");
    Atom *source_outer = atom_var(&arena, "source-width-outer");
    Atom **source_items = arena_alloc(&arena, 260u * sizeof(*source_items));
    assert(source_items);
    source_items[0] = atom_symbol(&arena, "width-entry");
    for (size_t i = 1u; i < 260u; i++) source_items[i] = source_outer;
    source_items[258] = source_x;
    Atom *source_form = atom_expr(&arena, (Atom *[]){atom_symbol(&arena, "let"),
        source_outer, atom_symbol(&arena, "outer"),
        atom_expr3(&arena, atom_symbol(&arena, "width-join"),
            atom_expr(&arena, source_items, 260u), source_x)}, 4u);
    Atom *source_out = prime_semantics_elaborate_form(&arena, source_form);
    assert(source_out && source_out->kind == ATOM_EXPR && source_out->expr.len >= 4u);
    Atom *outer_x = source_out->expr.elems[1];
    Atom *join = source_out->expr.elems[3], *entry = join->expr.elems[1];
    for (size_t i = 1u; i <= 257u; i++)
        assert(var_id_eq(outer_x->var_id, entry->expr.elems[i]->var_id));
    assert(!var_id_eq(source_x->var_id, entry->expr.elems[258]->var_id) &&
        var_id_eq(outer_x->var_id, entry->expr.elems[259]->var_id) &&
        var_id_eq(join->expr.elems[2]->var_id, entry->expr.elems[258]->var_id));

    AtomId refused[] = {
        term_universe_store_atom_id(&universe, &arena,
            interpretation_width_declaration(&arena, "width-refuse-first", 258u, false)),
        term_universe_store_atom_id(&universe, &arena,
            interpretation_width_declaration(&arena, "width-refuse-second", 259u, false)),
    };
    CettaPrimeInterpretation *before = prime_interpretation_capture(source->prime_interpretation);
    assert(before);
    for (size_t at = 1u; at <= 3u; at++) {
        interpretation_fail_malloc_after = at;
        assert(!prime_semantics_elaborate_read_forms(&universe, refused, 2u) &&
            !interpretation_fail_malloc_after);
        CettaPrimeInterpretation *after = prime_interpretation_capture(source->prime_interpretation);
        assert(after == before);
        prime_interpretation_release(after);
        interpretation_width_check(source, "width-refuse-first", 258u, 1u, false);
        interpretation_width_check(source, "width-refuse-second", 259u, 1u, false);
    }
    assert(prime_semantics_elaborate_read_forms(&universe, refused, 2u));
    interpretation_width_check(source, "width-refuse-first", 258u, 1u, true);
    interpretation_width_check(source, "width-refuse-second", 259u, 1u, true);
    prime_interpretation_release(before);

    Atom *pending_head = atom_var(&arena, "width-pending-head");
    Atom *pending_x = atom_var(&arena, "width-pending-x");
    Atom **pending_items = arena_alloc(&arena, 260u * sizeof(*pending_items));
    assert(pending_items);
    pending_items[0] = pending_head;
    for (size_t i = 1u; i < 260u; i++) pending_items[i] = pending_x;
    Atom *pending_form = atom_expr(&arena, pending_items, 260u);
    Bindings pending_bindings; bindings_init(&pending_bindings);
    assert(bindings_add_var(&pending_bindings, pending_head,
        atom_symbol(&arena, "width-pick-257")));
    BindingsApplySession *pending = bindings_apply_session_new(&pending_bindings,
        &arena, binding_value_from_atom(pending_form), true);
    assert(pending);
    Atom *pending_out = NULL; uint64_t used = 0u;
    assert(bindings_apply_session_advance(pending, 1u, &pending_out, &used) ==
        BINDINGS_APPLY_PENDING && used == 1u);

    assert(cetta_library_context_init_prime_snapshot_from(copy, source));
    CettaPrimeInterpretation *captured = prime_interpretation_capture(copy->prime_interpretation);
    CettaStorageCensus census = {0};
    prime_interpretation_selection_storage(source->prime_interpretation, &census);
    size_t once = census.bytes;
    prime_interpretation_storage(captured, &census);
    assert(once > 257u && census.bytes == once && census.repeated && !census.incomplete);
    cetta_storage_census_free(&census);
    eval_swap_library_context(copy);
    cetta_library_context_free(source); free(source);
    interpretation_width_check(copy, "width-pick-257", 257u, 1u, true);
    interpretation_width_check(copy, "width-repeat", 257u, 2u, true);
    eval_swap_library_context(previous);
    cetta_library_context_free(copy); free(copy);
    prime_interpretation_release(captured);
    BindingsApplyStatus status;
    do {
        status = bindings_apply_session_advance(pending, 1u, &pending_out, &used);
        assert(used <= 1u);
    } while (status == BINDINGS_APPLY_PENDING);
    assert(status == BINDINGS_APPLY_COMPLETE && pending_out &&
        pending_out->kind == ATOM_EXPR && pending_out->expr.len == 260u &&
        !var_id_eq(pending_x->var_id, pending_out->expr.elems[258]->var_id) &&
        var_id_eq(pending_out->expr.elems[258]->var_id, pending_out->expr.elems[259]->var_id));
    for (size_t i = 1u; i <= 257u; i++)
        assert(var_id_eq(pending_x->var_id, pending_out->expr.elems[i]->var_id));
    bindings_apply_session_free(pending);
    bindings_free(&pending_bindings);
    term_universe_free(&universe); arena_free(&arena);
    g_var_intern = previous_variables; var_intern_free(&variables);
}

static bool interpretation_capture_refused(void *context, void **owned) {
    (void)context;
    *owned = NULL;
    return false;
}

static void interpretation_refusal_controls(CettaLibraryContext *context) {
    CettaLibraryContext *previous = eval_swap_library_context(context);
    CettaPrimeInterpretation *before = prime_interpretation_capture(context->prime_interpretation);
    assert(before);
    CettaPrimeScopeProfile profile = prime_scope_runtime_profile();
    interpretation_fail_calloc = true;
    assert(prime_scope_runtime_profile_set(profile) && interpretation_fail_calloc);
    interpretation_fail_calloc = false;
    interpretation_fail_malloc = true;
    assert(prime_quote_seal_set(true) && interpretation_fail_malloc);
    interpretation_fail_malloc = false;
    assert(interpretation_read(context, "(scope:binder (snapshot-pick value pattern body))") == 1);
    CettaPrimeScopeProfile changed = profile;
    changed.lifetime = CETTA_PRIME_LIFETIME_PER_CLOSURE;
    interpretation_fail_calloc = true;
    assert(!prime_scope_runtime_profile_set(changed) && !interpretation_fail_calloc);
    interpretation_fail_malloc = true;
    assert(!prime_quote_seal_set(false) && !interpretation_fail_malloc);
    interpretation_fail_calloc = true;
    assert(!prime_interpretation_selection_copy(context->prime_interpretation) &&
        !interpretation_fail_calloc);
    CettaPrimeInterpretation *after = prime_interpretation_capture(context->prime_interpretation);
    assert(after == before);
    prime_interpretation_release(after);
    Arena arena; arena_init_detached(&arena);
    Bindings bindings; bindings_init(&bindings);
    Atom *x = atom_var(&arena, "capture-refusal-x");
    Atom *source = atom_expr2(&arena, atom_symbol(&arena, "Box"), x);
    assert(bindings_add_var(&bindings, x, atom_int(&arena, 7)));
    const BindingsStructureHooks *prior = bindings_structure_hooks();
    BindingsStructureHooks rejecting = *prime_semantics_binding_hooks();
    rejecting.capture = interpretation_capture_refused;
    bindings_structure_hooks_set(&rejecting);
    assert(!bindings_apply_session_new(&bindings, &arena, binding_value_from_atom(source), true));
    Atom *ground = atom_expr2(&arena, atom_symbol(&arena, "Box"), atom_int(&arena, 7));
    BindingsApplySession *identity = bindings_apply_session_new(&bindings, &arena,
        binding_value_from_atom(ground), true);
    assert(identity);
    Atom *identity_result = NULL; uint64_t identity_used = 0u;
    assert(bindings_apply_session_advance(identity, 8u, &identity_result, &identity_used) ==
        BINDINGS_APPLY_COMPLETE && identity_result == ground);
    bindings_apply_session_free(identity);
    bindings_structure_hooks_set(prior);
    BindingsStructureHooks invalid = *prime_semantics_binding_hooks(), closed = {0};
    invalid.context = before; invalid.capture = NULL; invalid.retain = NULL;
    assert(!bindings_structure_hooks_capture(&closed, &invalid) && !closed.context);
    Atom *healthy = bindings_apply(&bindings, &arena, source);
    assert(healthy && atom_eq(healthy->expr.elems[1], atom_int(&arena, 7)) &&
        source->expr.elems[1] == x);
    BindingsStructureHooks comparison = {0};
    assert(bindings_structure_hooks_capture(&comparison, prime_semantics_binding_hooks()));
    Atom *integer = atom_expr2(&arena, atom_symbol_id(&arena, g_builtin_syms.quote), atom_int(&arena, 1));
    Atom *floating = atom_expr2(&arena, atom_symbol_id(&arena, g_builtin_syms.quote), atom_float(&arena, 1.0));
    Bindings empty; bindings_init(&empty);
    bool matched = true;
    assert(comparison.code_match(comparison.context, &arena, integer, floating, &empty, &matched) &&
        !matched && bindings_logically_empty(&empty));
    CettaLibraryContext *he = calloc(1u, sizeof(*he)); assert(he);
    cetta_library_context_init_for_language_profile(he, CETTA_LANGUAGE_HE, cetta_profile_he_compat());
    eval_swap_library_context(he);
    matched = true;
    assert(!comparison.code_match(comparison.context, &arena, integer, floating, &empty, &matched) &&
        !matched && bindings_logically_empty(&empty));
    eval_swap_library_context(context);
    assert(comparison.code_match(comparison.context, &arena, integer, integer, &empty, &matched) && matched);
    bindings_structure_hooks_release(&comparison);
    bindings_free(&empty); cetta_library_context_free(he); free(he);
    bindings_free(&bindings); arena_free(&arena);
    prime_interpretation_release(before);
    eval_swap_library_context(previous);
}

static void interpretation_symbol_owner_controls(void) {
    SymbolTable *runtime = g_symbols;
    g_symbols = NULL;
    CettaPrimeInterpretationSelection *early = prime_interpretation_selection_new(CETTA_LANGUAGE_PRIME, false, true);
    assert(early && !prime_interpretation_capture(early));
    g_symbols = runtime;
    CettaPrimeInterpretation *bound = prime_interpretation_capture(early);
    assert(bound && prime_interpretation_is_current(bound));
    SymbolTable other; symbol_table_init(&other); g_symbols = &other;
    assert(!prime_interpretation_capture(early) && !prime_interpretation_is_current(bound));
    CettaPrimeInterpretationSelection *retired = prime_interpretation_selection_new(CETTA_LANGUAGE_PRIME, false, true);
    CettaPrimeInterpretation *retained = prime_interpretation_capture(retired);
    assert(retained);
    g_symbols = runtime;
    symbol_table_free(&other);
    assert(!prime_interpretation_capture(retired) && !prime_interpretation_is_current(retained));
    CettaStorageCensus census = {0};
    prime_interpretation_selection_storage(retired, &census);
    size_t bytes = census.bytes;
    prime_interpretation_storage(retained, &census);
    assert(bytes && census.bytes == bytes && census.repeated && !census.incomplete);
    cetta_storage_census_free(&census);
    prime_interpretation_release(retained);
    prime_interpretation_selection_free(retired);
    prime_interpretation_release(bound);
    prime_interpretation_selection_free(early);
}

static void interpretation_profile_session(CettaLibraryContext *context) {
    CettaLibraryContext *previous = eval_swap_library_context(context);
    CettaPrimeScopeProfile original = prime_scope_runtime_profile(), wide = original;
    wide.ownership = CETTA_PRIME_OWNERSHIP_QUERY_WIDE;
    assert(prime_scope_runtime_profile_set(wide));
    Arena arena; arena_init_detached(&arena);
    Bindings bindings; bindings_init(&bindings);
    Atom *head = atom_var(&arena, "profile-head"), *x = atom_var(&arena, "profile-x");
    Atom *source = atom_expr(&arena,
        (Atom *[]){head, atom_symbol(&arena, "seed"), x, x}, 4u);
    assert(bindings_add_var(&bindings, head, atom_symbol(&arena, "snapshot-pick")));
    BindingsApplySession *session = bindings_apply_session_new(&bindings, &arena,
        binding_value_from_atom(source), true);
    assert(session);
    Atom *out = NULL; uint64_t used = 0u;
    assert(bindings_apply_session_advance(session, 1u, &out, &used) == BINDINGS_APPLY_PENDING);
    assert(prime_scope_runtime_profile_set(original));
    Atom *later = bindings_apply(&bindings, &arena, source);
    assert(later && later->expr.elems[2]->kind == ATOM_VAR &&
        !var_id_eq(later->expr.elems[2]->var_id, x->var_id));
    CettaLibraryContext *he = calloc(1u, sizeof(*he)); assert(he);
    cetta_library_context_init_for_language_profile(he, CETTA_LANGUAGE_HE, cetta_profile_he_compat());
    eval_swap_library_context(he);
    BindingsApplyStatus status;
    do { status = bindings_apply_session_advance(session, 1u, &out, &used); }
    while (status == BINDINGS_APPLY_PENDING);
    assert(status == BINDINGS_APPLY_COMPLETE && out &&
        var_id_eq(out->expr.elems[2]->var_id, x->var_id) &&
        var_id_eq(out->expr.elems[3]->var_id, x->var_id));
    eval_swap_library_context(context); cetta_library_context_free(he); free(he);
    bindings_apply_session_free(session); bindings_free(&bindings); arena_free(&arena);
    eval_swap_library_context(previous);
}

static void interpretation_snapshot_controls(void) {
    VarInternTable variables; var_intern_init(&variables);
    VarInternTable *previous_variables = g_var_intern; g_var_intern = &variables;
    CettaLibraryContext *previous = eval_current_library_context();
    CettaLibraryContext *source = calloc(1u, sizeof(*source));
    CettaLibraryContext *copy = calloc(1u, sizeof(*copy));
    CettaLibraryContext *other = calloc(1u, sizeof(*other));
    assert(source && copy && other);
    cetta_library_context_init_for_language_profile(source, CETTA_LANGUAGE_PRIME,
        cetta_profile_prime_default());
    cetta_library_context_init_for_language_profile(other, CETTA_LANGUAGE_PRIME,
        cetta_profile_prime_default());
    assert(interpretation_read(source,
        "(scope:binder (snapshot-pick value pattern body))\n"
        "(scope:binder (ordered-pick pattern value body))\n"
        "(scope:binder (ordered-pick value pattern body))") == 3);
    assert(interpretation_inner(source, "snapshot-pick"));
    assert(!interpretation_inner(source, "ordered-pick"));
    interpretation_refusal_controls(source);
    interpretation_profile_session(source);
    assert(interpretation_read(other,
        "(scope:binder (ordered-pick value pattern body))\n"
        "(scope:binder (ordered-pick pattern value body))") == 2);
    assert(interpretation_inner(other, "ordered-pick"));
    assert(cetta_library_context_init_prime_snapshot_from(copy, source));
    CettaPrimeInterpretation *captured = prime_interpretation_capture(copy->prime_interpretation);
    assert(captured);
    assert(interpretation_read(source,
        "(scope:binder (later-pick value pattern body))") == 1);
    assert(interpretation_inner(source, "later-pick"));
    assert(!interpretation_inner(copy, "later-pick"));
    eval_swap_library_context(source);
    CettaPrimeScopeProfile wide = prime_scope_runtime_profile();
    wide.ownership = CETTA_PRIME_OWNERSHIP_QUERY_WIDE;
    assert(prime_scope_runtime_profile_set(wide));
    assert(prime_quote_seal_set(false));
    eval_swap_library_context(copy);
    assert(prime_scope_runtime_profile().ownership == CETTA_PRIME_OWNERSHIP_LEXICAL_FRESH);
    assert(prime_quote_seal_active());
    prime_scope_document_reading(true);
    assert(interpretation_read(copy,
        "(scope:profile query-wide per-call reference)\n"
        "(scope:binder (failed-profile-pick value pattern body))\n"
        "(= (bad $x) (chain 1 $v $v){7})") < 0);
    prime_scope_document_reading(false);
    assert(prime_scope_runtime_profile().ownership == CETTA_PRIME_OWNERSHIP_LEXICAL_FRESH);
    assert(!interpretation_inner(copy, "failed-profile-pick"));
    CettaPrimeInterpretation *unchanged = prime_interpretation_capture(copy->prime_interpretation);
    assert(unchanged && prime_interpretation_equal(captured, unchanged));
    prime_interpretation_release(unchanged);
    cetta_library_context_free(source); free(source);
    assert(interpretation_inner(copy, "snapshot-pick"));
    assert(!interpretation_inner(copy, "later-pick"));
    CettaStorageCensus census = {0};
    prime_interpretation_storage(captured, &census);
    size_t before = census.bytes;
    prime_interpretation_selection_storage(copy->prime_interpretation, &census);
    assert(!census.incomplete && before && census.bytes > before && census.repeated);
    cetta_storage_census_free(&census);
    eval_swap_library_context(other);
    cetta_library_context_free(copy); free(copy);
    assert(prime_interpretation_is_current(captured));
    prime_interpretation_release(captured);
    eval_swap_library_context(previous);
    cetta_library_context_free(other); free(other);
    g_var_intern = previous_variables; var_intern_free(&variables);
}

static void interpretation_context_controls(void) {
    VarInternTable variables; var_intern_init(&variables);
    VarInternTable *previous_variables = g_var_intern; g_var_intern = &variables;
    CettaLibraryContext *previous = eval_current_library_context();
    const BindingsStructureHooks *previous_hooks = bindings_structure_hooks();
    CettaPrimeScopeProfile previous_profile = prime_scope_runtime_profile();
    bool previous_sealing = prime_quote_seal_active();
    CettaLibraryContext *a = calloc(1u, sizeof(*a)), *b = calloc(1u, sizeof(*b));
    assert(a && b);
    cetta_library_context_init_for_language_profile(a, CETTA_LANGUAGE_PRIME, cetta_profile_prime_default());
    cetta_library_context_init_for_language_profile(b, CETTA_LANGUAGE_PRIME, cetta_profile_prime_default());
    assert(interpretation_read(b, "(scope:profile lexical-fresh per-call reference)") == 1);
    bool before = interpretation_inner(b, "other-context-pick");
    eval_set_library_context(b);
    Arena arena; arena_init_detached(&arena);
    Atom *h = atom_var(&arena, "pending-head"), *x = atom_var(&arena, "pending-x");
    Atom *source = atom_expr(&arena, (Atom *[]){h, atom_symbol(&arena, "seed"), x, x}, 4u);
    Bindings bindings; bindings_init(&bindings);
    assert(bindings_add_var(&bindings, h, atom_symbol(&arena, "other-context-pick")));
    BindingsApplySession *session = bindings_apply_session_new(&bindings, &arena,
        binding_value_from_atom(source), true);
    assert(session);
    Atom *expected = bindings_apply(&bindings, &arena, source);
    assert(expected && expected->kind == ATOM_EXPR && expected->expr.len == 4u &&
        expected->expr.elems[2]->kind == ATOM_VAR &&
        var_id_eq(expected->expr.elems[2]->var_id, x->var_id));
    Atom *result = NULL; uint64_t used = 0u;
    assert(bindings_apply_session_advance(session, 1u, &result, &used) == BINDINGS_APPLY_PENDING && used == 1u);
    assert(interpretation_read(a,
        "(scope:profile lexical-fresh per-call reference)\n"
        "(scope:binder (other-context-pick value pattern body))") == 2);
    bool after = interpretation_inner(b, "other-context-pick");
    bool own_after = interpretation_inner(a, "other-context-pick");
    bool failed_before = interpretation_inner(b, "failed-context-pick");
    bool failed_self_before = interpretation_inner(a, "failed-context-pick");
    int failed = interpretation_read(a,
        "(scope:profile lexical-fresh per-call reference)\n"
        "(scope:binder (failed-context-pick value pattern body))\n"
        "(= (bad $x) (chain 1 $v $v){7})");
    bool failed_after = interpretation_inner(b, "failed-context-pick");
    bool failed_self_after = interpretation_inner(a, "failed-context-pick");
    eval_set_library_context(b); cetta_library_context_free(a); free(a);
    bool retired_after = interpretation_inner(b, "other-context-pick");
    CettaLibraryContext *c = calloc(1u, sizeof(*c)); assert(c);
    cetta_library_context_init_for_language_profile(c, CETTA_LANGUAGE_PRIME, cetta_profile_prime_default());
    assert(interpretation_read(c,
        "(scope:profile lexical-fresh per-call reference)\n"
        "(scope:binder (other-context-pick value pattern body))") == 2);
    bool receiver_binding = interpretation_inner(c, "other-context-pick");
    eval_set_library_context(c); cetta_library_context_free(b); free(b);
    BindingsApplyStatus status;
    do {
        status = bindings_apply_session_advance(session, 1u, &result, &used);
        assert(used <= 1u);
    } while (status == BINDINGS_APPLY_PENDING);
    bool retained = status == BINDINGS_APPLY_COMPLETE && result && result->kind == ATOM_EXPR &&
        result->expr.len == 4u && result->expr.elems[2]->kind == ATOM_VAR &&
        result->expr.elems[3]->kind == ATOM_VAR &&
        var_id_eq(result->expr.elems[2]->var_id, x->var_id) &&
        var_id_eq(result->expr.elems[3]->var_id, x->var_id) && atom_eq(result, expected);
    bindings_apply_session_free(session); bindings_free(&bindings); arena_free(&arena);
    eval_set_library_context(previous); cetta_library_context_free(c); free(c);
    prime_scope_runtime_profile_set(previous_profile); prime_quote_seal_set(previous_sealing);
    bindings_structure_hooks_set(previous_hooks);
    g_var_intern = previous_variables; var_intern_free(&variables);
    if (before || after || retired_after || failed_before || failed_after || failed >= 0 ||
            failed_self_before || failed_self_after || !own_after || !receiver_binding || !retained)
        fprintf(stderr, "interpretation isolation: before=%d after=%d retired=%d failed_before=%d failed_after=%d failed_self=%d,%d failure=%d own=%d receiver=%d retained=%d\n",
            before, after, retired_after, failed_before, failed_after,
            failed_self_before, failed_self_after, failed, own_after, receiver_binding, retained);
    assert(!before && !after && !retired_after && !failed_before && !failed_after && failed < 0 &&
        !failed_self_before && !failed_self_after && own_after && receiver_binding && retained);
}
#endif

static void saved_match(void) {
    Arena source, saved, destination;
    arena_init_detached(&source);
    arena_init_detached(&saved);
    arena_init_detached(&destination);
    Atom *x = atom_var(&source, "x"), *u = atom_var(&source, "u");
    VarId x_id = x->var_id, u_id = u->var_id;
    Atom *subterm = atom_expr2(&source, atom_symbol(&source, "f"), u);
    Atom *subject = atom_expr2(&source, subterm, subterm);
    CettaTermMatch match;
    assert(term_graph_match_pattern(&source, x, subject, atom_eq, &match) == CETTA_TERM_MATCH_OK);
    assert(match.count == 1u && match.values[0] == subject);
    Bindings env;
    bindings_init(&env);
    assert(bindings_add_var(&env, match.variables[0], match.values[0]));
    Atom *witness = bindings_capture_value(&saved, &env);
    assert(witness);
    bindings_free(&env);
    arena_free(&source);

    Atom *query = atom_var_with_id(&destination, "x", x_id);
    Atom *result = bindings_apply_saved(&destination, witness, query);
    assert(result && result->kind == ATOM_EXPR && result->expr.len == 2u);
    assert(result->expr.elems[0] == result->expr.elems[1]);
    arena_free(&saved);
    assert(var_id_eq(result->expr.elems[0]->expr.elems[1]->var_id, u_id));
    assert(atom_is_symbol(result->expr.elems[0]->expr.elems[0], "f"));
    arena_free(&destination);
}

static void saved_permutation(void) {
    Arena source, saved, copied, destination;
    arena_init_detached(&source);
    arena_init_detached(&saved);
    arena_init_detached(&copied);
    arena_init_detached(&destination);
    Atom *x = atom_var(&source, "same"), *y = atom_var(&source, "same");
    Atom *z = atom_var(&source, "z");
    VarId xi = x->var_id, yi = y->var_id, zi = z->var_id;
    Atom *keys[] = {x, y}, *values[] = {y, x};
    Atom *witness = bindings_capture_renaming(&saved, keys, values, 2u);
    assert(witness);
    Atom *copy = atom_deep_copy(&copied, witness);
    assert(copy && atom_eq(copy, witness));

    /* Mixing saved logical substitutions and renamings cannot reinterpret
     * the memory layout or recursively follow a permutation cycle. */
    Bindings env;
    bindings_init(&env);
    assert(!bindings_restore_captured_value(copy, &env));
    assert(bindings_add_var(&env, x, atom_int(&source, 1)));
    Atom *logical = bindings_capture_value(&saved, &env);
    assert(!atom_eq(logical, witness) && !atom_eq(witness, logical));
    bindings_free(&env);
    arena_free(&source);
    arena_free(&saved);

    Atom *a = atom_var_with_id(&destination, "x", xi);
    Atom *b = atom_var_with_id(&destination, "y", yi);
    Atom *c = atom_var_with_id(&destination, "z", zi);
    Atom *shared = atom_expr2(&destination, a, b);
    Atom *term = atom_expr3(&destination, shared, shared, c);
    Atom *result = bindings_apply_saved(&destination, copy, term);
    assert(result && result->expr.elems[0] == result->expr.elems[1]);
    assert(var_id_eq(result->expr.elems[0]->expr.elems[0]->var_id, yi));
    assert(var_id_eq(result->expr.elems[0]->expr.elems[1]->var_id, xi));
    assert(var_id_eq(result->expr.elems[2]->var_id, zi));
    CettaBindingsValue *value = copy->ground.ptr;
    Atom *projection = value->observe(&destination, value);
    arena_free(&copied);
    assert(var_id_eq(result->expr.elems[0]->expr.elems[0]->var_id, yi));
    assert(projection && projection->expr.elems[1]->expr.len == 2u);
    assert(var_id_eq(projection->expr.elems[1]->expr.elems[0]->expr.elems[1]->var_id, yi));
    arena_free(&destination);
}

static void saved_open_template(void) {
    Arena source, saved, destination;
    arena_init_detached(&source);
    arena_init_detached(&saved);
    arena_init_detached(&destination);
    Atom *x = atom_var(&source, "x"), *y = atom_var(&source, "y");
    VarId xi = x->var_id, yi = y->var_id;
    Atom *shared = atom_expr2(&source, x, y);
    /* The bound image is itself a shared open DAG. Applying the saved
     * substitution must preserve both image and template sharing. */
    for (unsigned depth = 0; depth < 32u; ++depth)
        shared = atom_expr2(&source, shared, shared);
    Atom *image = atom_var(&source, "image");
    VarId ii = image->var_id;
    Bindings env;
    bindings_init(&env);
    assert(bindings_add_var(&env, image, shared));
    assert(bindings_add_var(&env, x, atom_symbol(&source, "done")));
    Atom *witness = bindings_capture_value(&saved, &env);
    assert(witness);
    bindings_free(&env);
    arena_free(&source);

    Atom *variable = atom_var_with_id(&destination, "image", ii);
    Atom *template = atom_expr2(&destination, variable,
        atom_var_with_id(&destination, "x", xi));
    for (unsigned depth = 0; depth < 32u; ++depth)
        template = atom_expr2(&destination, template, template);
    Atom *result = bindings_apply_saved(&destination, witness, template);
    assert(result);
    arena_free(&saved);
    for (unsigned depth = 0; depth < 32u; ++depth) {
        assert(result->kind == ATOM_EXPR && result->expr.len == 2u);
        assert(result->expr.elems[0] == result->expr.elems[1]);
        result = result->expr.elems[0];
    }
    assert(atom_is_symbol(result->expr.elems[1], "done"));
    result = result->expr.elems[0];
    for (unsigned depth = 0; depth < 32u; ++depth) {
        assert(result->expr.elems[0] == result->expr.elems[1]);
        result = result->expr.elems[0];
    }
    assert(atom_is_symbol(result->expr.elems[0], "done"));
    assert(var_id_eq(result->expr.elems[1]->var_id, yi));
    arena_free(&destination);
}

static Atom *retained_application(const Bindings *bindings, Arena *arena,
        BindingValue source, bool graph, Atom *expected) {
    const uint64_t quanta[] = {1u, 7u, UINT64_MAX};
    uint64_t reference_steps = 0u;
    Atom *completed = NULL;
    for (size_t trial = 0u; trial < sizeof(quanta)/sizeof(*quanta); trial++) {
        BindingsApplySession *session = bindings_apply_session_new(bindings, arena, source, graph);
        assert(session && !bindings_apply_session_steps(session));
        BindingsApplyStatus status = BINDINGS_APPLY_PENDING;
        Atom *result = NULL;
        while (status == BINDINGS_APPLY_PENDING) {
            uint64_t before = bindings_apply_session_steps(session), used = 99u;
            assert(bindings_apply_session_advance(session, 0u, &result, &used) ==
                BINDINGS_APPLY_PENDING && !result && !used &&
                bindings_apply_session_steps(session) == before);
            status = bindings_apply_session_advance(session, quanta[trial], &result, &used);
            assert(used && used <= quanta[trial] &&
                bindings_apply_session_steps(session) == before + used);
            if (status != BINDINGS_APPLY_COMPLETE) assert(!result);
        }
        assert((expected && status == BINDINGS_APPLY_COMPLETE && atom_eq(result, expected)) ||
            (!expected && status == BINDINGS_APPLY_REFUSED && !result));
        uint64_t steps = bindings_apply_session_steps(session), used = 99u;
        if (!trial) reference_steps = steps;
        else assert(steps == reference_steps);
        assert(bindings_apply_session_advance(session, 7u, &completed, &used) == status &&
            !used && completed == result && bindings_apply_session_steps(session) == steps);
        bindings_apply_session_free(session);
    }
    for (uint64_t cut = 1u; cut < reference_steps && cut <= 17u; cut += 4u) {
        BindingsApplySession *session = bindings_apply_session_new(bindings, arena, source, graph);
        Atom *unfinished = NULL;uint64_t used = 0u;
        assert(session && bindings_apply_session_advance(session, cut, &unfinished, &used) ==
            BINDINGS_APPLY_PENDING && !unfinished && used == cut);
        bindings_apply_session_free(session);
        if (expected) assert(completed && atom_eq(completed, expected));
    }
    return completed;
}

static Atom *retained_graph_image(Bindings *bindings, Arena *arena, Atom *source) {
    Atom *expected = bindings_apply_graph(bindings, arena, source);
    return retained_application(bindings, arena, binding_value_from_atom(source), true, expected);
}

static void retained_application_controls(void) {
    Arena source, destination;arena_init_detached(&source);arena_init_detached(&destination);
    Bindings bindings;bindings_init(&bindings);
    Atom *x = atom_var(&source, "retained-x"), *y = atom_var(&source, "retained-y");
    Atom *seven = atom_int(&source, 7);
    assert(bindings_add_var_acyclic(&bindings, x, seven));
    Atom *children[2048];
    for (unsigned i = 0u; i < 2048u; i++) children[i] = atom_int(&source, i);
    children[2047] = x;
    Atom *wide = atom_expr(&source, children, 2048u);
    Atom *expected = bindings_apply(&bindings, &destination, wide);
    Atom *out = retained_application(&bindings, &destination, binding_value_from_atom(wide),
        false, expected);
    assert(out->expr.len == 2048u && out->expr.elems[2047] == seven);
    /* The source prefix remains intact through every partial-copy cut. */
    for (unsigned i = 0u; i < 2047u; i++) assert(wide->expr.elems[i] == children[i]);
    Atom *leaf = atom_expr2(&source, atom_symbol(&source, "shared"), x);
    Atom *dag = leaf;
    for (unsigned i = 0u; i < 18u; i++) dag = atom_expr2(&source, dag, dag);
    out = retained_graph_image(&bindings, &destination, dag);
    for (unsigned i = 0u; i < 18u; i++) {
        assert(out->expr.elems[0] == out->expr.elems[1]);out = out->expr.elems[0];
    }
    assert(out->expr.elems[1] == seven);
    CETTA_FRAME_IDENTITY_SCOPE(frames);
    CettaFrameIdentity frame = cetta_frame_identity_scope_fresh(&frames);
    VarId ids[] = {x->var_id, y->var_id};
    assert(bindings_register_contextual_frame(&bindings, ids, 2u, frame));
    assert(bindings_add_id(&bindings, var_epoch_id(x->var_id, frame), SYMBOL_ID_NONE, seven));
    Atom *pair = atom_expr2(&source, x, y);
    BindingValue contextual = binding_value_from_context(pair, frame);
    expected = bindings_apply_value(&bindings, &destination, contextual);
    out = retained_application(&bindings, &destination, contextual, false, expected);
    assert(out->expr.elems[0] == seven &&
        var_id_eq(out->expr.elems[1]->var_id, var_epoch_id(y->var_id, frame)));
    /* A list-rest child needs its existing splicing finisher, not the raw
     * expression constructor's refusal or a silently truncated list. */
    Atom *list = atom_list_with_rest(&source, &x, 1u, y);
    expected = bindings_apply(&bindings, &destination, list);
    out = retained_application(&bindings, &destination, binding_value_from_atom(list), false, expected);
    assert(out && atom_eq(out, atom_list_with_rest(&destination, &seven, 1u, y)));
    ArenaMark mark = arena_mark(&destination);
    BindingsApplySession *session = bindings_apply_session_new(&bindings, &destination,
        binding_value_from_atom(wide), false);
    assert(session);assert(atom_int(&destination, 31337));arena_reset(&destination, mark);
    uint64_t used = 99u;out = NULL;
    assert(bindings_apply_session_advance(session, 1u, &out, &used) == BINDINGS_APPLY_REFUSED &&
        !out && !used);
    bindings_apply_session_free(session);
    bindings_free(&bindings);arena_free(&destination);arena_free(&source);
    puts("PASS: retained binding application preserves exact cuts, lexical contexts, shared graphs, wide prefixes, list splicing and reset refusal");
}

static void shared_lexical_readings(void) {
    Arena arena;
    arena_init_detached(&arena);
    CETTA_FRAME_IDENTITY_SCOPE(frames);
    CettaFrameIdentity first = cetta_frame_identity_scope_fresh(&frames);
    CettaFrameIdentity second = cetta_frame_identity_scope_fresh(&frames);
    Atom *slot = atom_var(&arena, "slot"), *x = atom_var(&arena, "x");
    Atom *y = atom_var(&arena, "y");
    Atom *source = atom_expr2(&arena, atom_symbol(&arena, "node"), slot);
    Bindings env;
    bindings_init(&env);
    assert(match_binding_values(binding_value_from_atom(x),
        binding_value_from_context(source, first), &env, &arena));
    assert(match_binding_values(binding_value_from_atom(y),
        binding_value_from_context(source, second), &env, &arena));
    Atom *a = atom_symbol(&arena, "a"), *b = atom_symbol(&arena, "b");
    assert(bindings_add_id(&env, var_epoch_id(slot->var_id, first), atom_symbol_current_id(slot), a));
    assert(bindings_add_id(&env, var_epoch_id(slot->var_id, second), atom_symbol_current_id(slot), b));
    Atom *pair = atom_expr2(&arena, x, y);
    Atom *ordinary = bindings_apply(&env, &arena, pair);
    Atom *shared = retained_graph_image(&env, &arena, pair);
    assert(ordinary && shared && atom_eq(ordinary, shared));
    assert(shared->expr.elems[0]->expr.elems[1] == a);
    assert(shared->expr.elems[1]->expr.elems[1] == b);
    bindings_free(&env);
    arena_free(&arena);
}

/* A controlled quotation reading exercises the hook contract even in the
 * standalone matcher build. The full-runtime build uses the actual Prime
 * reading for this case. */
static Atom *quotation_graph_code_reading(const void *context, Atom *image) {
    (void)context;
    if (image && image->kind == ATOM_EXPR && image->expr.len == 3u &&
        atom_is_symbol(image->expr.elems[0], "quote") &&
        image->expr.elems[2]->kind == ATOM_EXPR &&
        image->expr.elems[2]->expr.len == 1u &&
        image->expr.elems[2]->expr.elems[0]->kind == ATOM_SYMBOL)
        return image->expr.elems[1];
    return image;
}

static Atom *quotation_graph_formed_node(const void *context, Arena *arena, Atom *node,
                                         bool in_code) {
    (void)context;
    return atom_expr3(arena,
        atom_symbol(arena, in_code ? "formed-code" : "formed-value"),
        node, atom_bool(arena, in_code));
}

static Atom *quotation_graph_rejected_form(const void *context, Arena *arena, Atom *node,
                                           bool in_code) {
    (void)context;
    (void)arena;
    return in_code ? NULL : node;
}

static void shared_quotation_readings(void) {
    Arena source, ordinary_arena, graph_arena;
    arena_init_detached(&source);
    arena_init_detached(&ordinary_arena);
    arena_init_detached(&graph_arena);
    const BindingsStructureHooks *previous = bindings_structure_hooks();
#if defined(CETTA_PATTERN_ACTUAL_PRIME_HOOKS)
    bindings_structure_hooks_set(prime_semantics_binding_hooks());
#else
    const BindingsStructureHooks hooks = {
        .code_reading = quotation_graph_code_reading};
    bindings_structure_hooks_set(&hooks);
#endif
    Atom *quote = atom_symbol_id(&source, g_builtin_syms.quote);
    Atom *pair = atom_symbol(&source, "pair");
    Atom *row_head = atom_symbol(&source, "row");
    Atom *payload = atom_symbol(&source, "Payload");
    Atom *key[] = {atom_symbol(&source, "Key")};
    Atom *code = atom_expr3(&source, quote, payload,
                            atom_expr(&source, key, 1u));
#if defined(CETTA_PATTERN_ACTUAL_PRIME_HOOKS)
    assert(prime_semantics_contextual_code(code));
#endif
    Atom *hole = atom_var(&source, "hole");
    Atom *shared = atom_expr2(&source, row_head, hole);
    Atom *quoted = atom_expr2(&source, quote, shared);
    Bindings env;
    bindings_init(&env);
    assert(bindings_add_var_acyclic(&env, hole, code));
    for (unsigned reverse = 0u; reverse < 2u; ++reverse) {
        Atom *term = reverse ? atom_expr3(&source, pair, quoted, shared)
                             : atom_expr3(&source, pair, shared, quoted);
        Atom *ordinary = bindings_apply(&env, &ordinary_arena, term);
        Atom *graph = retained_graph_image(&env, &graph_arena, term);
        assert(ordinary && graph && atom_eq(ordinary, graph));
        Atom *outside = graph->expr.elems[reverse ? 2u : 1u];
        Atom *inside = graph->expr.elems[reverse ? 1u : 2u]->expr.elems[1];
        assert(outside->expr.elems[1] == code);
        assert(inside->expr.elems[1] == payload);
        assert(outside != inside);
    }
    Atom *same_depth = retained_graph_image(&env, &graph_arena,
        atom_expr3(&source, pair, shared, shared));
    assert(same_depth && same_depth->expr.elems[1] == same_depth->expr.elems[2]);
    Atom *inside_pair = atom_expr3(&source, pair, shared, shared);
    Atom *quoted_pair = atom_expr2(&source, quote, inside_pair);
    Atom *nested = atom_expr3(&source, pair, quoted_pair,
        atom_expr2(&source, quote, quoted_pair));
    Atom *ordinary = bindings_apply(&env, &ordinary_arena, nested);
    Atom *graph = retained_graph_image(&env, &graph_arena, nested);
    assert(ordinary && graph && atom_eq(ordinary, graph));
    Atom *first = graph->expr.elems[1]->expr.elems[1];
    Atom *second = graph->expr.elems[2]->expr.elems[1]->expr.elems[1];
    assert(first->expr.elems[1] == first->expr.elems[2]);
    assert(second->expr.elems[1] == second->expr.elems[2]);
    assert(first->expr.elems[1] != second->expr.elems[1]);

    /* Grow and rehash both observation tables while retaining the same
     * syntax at different quotation depths and twice at each depth. */
    Atom *rows[64];
    for (unsigned i = 0u; i < 64u; ++i)
        rows[i] = atom_expr3(&source, row_head, atom_int(&source, i), hole);
    Atom *block = atom_expr(&source, rows, 64u);
    Atom *both = atom_expr3(&source, pair, block, block);
    Atom *grown = atom_expr3(&source, pair, both,
        atom_expr2(&source, quote, both));
    ordinary = bindings_apply(&env, &ordinary_arena, grown);
    graph = retained_graph_image(&env, &graph_arena, grown);
    assert(ordinary && graph && atom_eq(ordinary, graph));
    Atom *outside = graph->expr.elems[1];
    Atom *inside = graph->expr.elems[2]->expr.elems[1];
    assert(outside->expr.elems[1] == outside->expr.elems[2]);
    assert(inside->expr.elems[1] == inside->expr.elems[2]);
    for (unsigned i = 0u; i < 64u; ++i) {
        assert(outside->expr.elems[1]->expr.elems[i]->expr.elems[2] == code);
        assert(inside->expr.elems[1]->expr.elems[i]->expr.elems[2] == payload);
    }

    /* A raw cycle is rejected even when every traversal of its back edge
     * changes quotation depth. The explicit rational carrier is separate. */
    Atom *cycle = atom_expr2(&source, quote, hole);
    cycle->expr.elems[1] = cycle;
    assert(!retained_graph_image(&env, &graph_arena, cycle));
    Atom *plain_cycle = atom_expr2(&source, row_head, hole);
    plain_cycle->expr.elems[1] = plain_cycle;
    assert(!retained_graph_image(&env, &graph_arena, plain_cycle));
    bindings_structure_hooks_set(previous);
    bindings_free(&env);
    arena_free(&graph_arena);
    arena_free(&ordinary_arena);
    arena_free(&source);
}

static void shared_formed_quotation_readings(void) {
    Arena source, ordinary_arena, graph_arena;
    arena_init_detached(&source);
    arena_init_detached(&ordinary_arena);
    arena_init_detached(&graph_arena);
    const BindingsStructureHooks *previous = bindings_structure_hooks();
    const BindingsStructureHooks hooks = {
        .formed_node = quotation_graph_formed_node,
        .code_reading = quotation_graph_code_reading};
    bindings_structure_hooks_set(&hooks);
    Atom *head = atom_var(&source, "head");
    Atom *call = atom_expr2(&source, head, atom_int(&source, 9));
    Atom *quoted = atom_expr2(&source,
        atom_symbol_id(&source, g_builtin_syms.quote), call);
    Atom *pair = atom_symbol(&source, "pair");
    Bindings env;
    bindings_init(&env);
    assert(bindings_add_var_acyclic(&env, head, atom_symbol(&source, "new-form")));
    for (unsigned reverse = 0u; reverse < 2u; ++reverse) {
        Atom *term = reverse ? atom_expr3(&source, pair, quoted, call)
                             : atom_expr3(&source, pair, call, quoted);
        Atom *ordinary = bindings_apply(&env, &ordinary_arena, term);
        Atom *graph = retained_graph_image(&env, &graph_arena, term);
        assert(ordinary && graph && atom_eq(ordinary, graph));
        assert(atom_is_symbol(graph->expr.elems[reverse ? 2u : 1u]->expr.elems[0],
                              "formed-value"));
        assert(atom_is_symbol(graph->expr.elems[reverse ? 1u : 2u]->expr.elems[1]
                                  ->expr.elems[0], "formed-code"));
    }
    const BindingsStructureHooks rejecting = {
        .formed_node = quotation_graph_rejected_form,
        .code_reading = quotation_graph_code_reading};
    bindings_structure_hooks_set(&rejecting);
    for (unsigned reverse = 0u; reverse < 2u; ++reverse) {
        Atom *term = reverse ? atom_expr3(&source, pair, quoted, call)
                             : atom_expr3(&source, pair, call, quoted);
        assert(!retained_graph_image(&env, &graph_arena, term));
    }
    bindings_structure_hooks_set(previous);
    bindings_free(&env);
    arena_free(&graph_arena);
    arena_free(&ordinary_arena);
    arena_free(&source);
}

static void saved_deep_template(void) {
    Arena arena;
    arena_init_detached(&arena);
    Atom *x = atom_var(&arena, "x");
    Atom *tag = atom_symbol(&arena, "next");
    Bindings env;
    bindings_init(&env);
    assert(bindings_add_var(&env, x, atom_int(&arena, 7)));
    Atom *saved = bindings_capture_value(&arena, &env);
    bindings_free(&env);
    Atom *term = x;
    for (unsigned i = 0u; i < 100000u; ++i)
        term = atom_expr2(&arena, tag, term);
    Atom *result = bindings_apply_saved(&arena, saved, term);
    for (unsigned i = 0u; i < 100000u; ++i) {
        assert(result && result->kind == ATOM_EXPR && result->expr.len == 2u);
        result = result->expr.elems[1];
    }
    assert(result && atom_eq(result, atom_int(&arena, 7)));
    arena_free(&arena);
}

static void malformed_renaming(void) {
    Arena arena;
    arena_init_detached(&arena);
    Atom *x = atom_var(&arena, "x"), *y = atom_var(&arena, "y");
    Atom *keys[] = {x, y}, *values[] = {x, x};
    assert(!bindings_capture_renaming(&arena, keys, values, 2u));
    values[1] = atom_int(&arena, 1);
    assert(!bindings_capture_renaming(&arena, keys, values, 2u));
    keys[1] = x; values[1] = y;
    assert(!bindings_capture_renaming(&arena, keys, values, 2u));
    assert(!bindings_capture_renaming(&arena, NULL, NULL, 1u));
    assert(!bindings_capture_renaming(&arena, keys, values, SIZE_MAX));
    Atom *identity = bindings_capture_renaming(&arena, NULL, NULL, 0u);
    assert(identity && atom_eq(bindings_apply_saved(&arena, identity, x), x));
    assert(!bindings_apply_saved(&arena, x, y));
    arena_free(&arena);
}

static void saved_carrier(void) {
    Arena source, saved, destination;
    arena_init_detached(&source);
    arena_init_detached(&saved);
    arena_init_detached(&destination);
    Atom *x = atom_var(&source, "x"), *u = atom_var(&source, "u");
    VarId xi = x->var_id, ui = u->var_id;
    CettaTermGraph *graph = term_graph_new();
    assert(graph);
    uint32_t parameter = term_graph_add_param(graph, 0u);
    uint32_t root = term_graph_add_compound(graph, atom_symbol_current_id(atom_symbol(&source, "cycle")), 2u);
    assert(term_graph_set_child(graph, root, 0u, parameter));
    assert(term_graph_set_child(graph, root, 1u, root));
    assert(term_graph_seal(graph));
    Atom *parameters[] = {u};
    Atom *subject = term_graph_node_value(&source, graph, root, parameters);
    assert(subject);
    term_graph_release(graph);

    Bindings env;
    bindings_init(&env);
    assert(bindings_add_var(&env, x, subject));
    Atom *witness = bindings_capture_value(&saved, &env);
    Atom *keys[] = {u}, *values[] = {x};
    Atom *permutation = bindings_capture_renaming(&saved, keys, values, 1u);
    assert(witness && permutation);
    bindings_free(&env);
    arena_free(&source);

    Atom *query = atom_var_with_id(&destination, "x", xi);
    Atom *result = bindings_apply_saved(&destination, witness, query);
    Atom *renamed = bindings_apply_saved(&destination, permutation, result);
    assert(result && renamed);
    arena_free(&saved);
    Atom *result_parameter = term_graph_value_param(result, 0u);
    Atom *renamed_parameter = term_graph_value_param(renamed, 0u);
    assert(result_parameter && result_parameter->kind == ATOM_VAR && var_id_eq(result_parameter->var_id, ui));
    assert(renamed_parameter && renamed_parameter->kind == ATOM_VAR && var_id_eq(renamed_parameter->var_id, xi));
    const CettaTermGraphRef *reference;
    Atom *const *arguments;
    uint32_t argument_count;
    assert(term_graph_value_node(result, &reference, &arguments, &argument_count));
    assert(argument_count == 1u && arguments[0] == result_parameter);
    assert(term_graph_open_value(&destination, result));
    assert(term_graph_open_value(&destination, renamed));
    arena_free(&destination);
}

static void opaque_context_scope(void) {
    Arena source, saved, destination;
    arena_init_detached(&source);
    arena_init_detached(&saved);
    arena_init_detached(&destination);
    Atom *key = atom_symbol(&source, "local");
    Atom *inside = atom_var(&source, "same");
    Atom *other = atom_var(&source, "same");
    Atom *capture = atom_var(&source, "capture");
    VarId inside_id = inside->var_id, capture_id = capture->var_id;
    Atom *context = atom_prime_context_bind(&source, NULL, key, inside);
    Atom *different = atom_prime_context_bind(&source, NULL, key, other);
    CettaTermMatch found;
    assert(term_graph_match_pattern(&source, context, different, atom_eq, &found)
        == CETTA_TERM_MATCH_MISMATCH);
    assert(term_graph_match_pattern(&source, context, context, atom_eq, &found)
        == CETTA_TERM_MATCH_OK && found.count == 0u);
    assert(term_graph_match_pattern(&source, capture, context, atom_eq, &found)
        == CETTA_TERM_MATCH_OK && found.count == 1u);
    Atom *witness = bindings_capture_images(&saved, found.variables, found.values,
        found.count, false);
    Atom *keys[] = {inside};
    Atom *images[] = {atom_symbol(&source, "outside")};
    Atom *outer = bindings_capture_images(&saved, keys, images, 1u, false);
    assert(witness && outer);
    arena_free(&source);
    Atom *result = bindings_apply_saved(&destination, witness,
        atom_var_with_id(&destination, "capture", capture_id));
    result = bindings_apply_saved(&destination, outer, result);
    assert(result && atom_prime_context_value(result));
    arena_free(&saved);
    Atom *value = atom_prime_context_lookup(atom_prime_context_value(result),
        atom_symbol(&destination, "local"));
    assert(value && value->kind == ATOM_VAR && var_id_eq(value->var_id, inside_id));
    arena_free(&destination);
}

static void frame_inventory_shared_readings(void) {
    Arena source, destination;
    arena_init_detached(&source);
    arena_init_detached(&destination);
    CETTA_FRAME_IDENTITY_SCOPE(frames);
    CettaFrameIdentity first = cetta_frame_identity_scope_fresh(&frames);
    CettaFrameIdentity second = cetta_frame_identity_scope_fresh(&frames);
    Atom *a = atom_var(&source, "inventory-a");
    Atom *b = atom_var(&source, "inventory-b");
    Atom *shared = atom_expr2(&source, a, b);
    for (unsigned depth = 0u; depth < 32u; ++depth)
        shared = atom_expr2(&source, shared, shared);
    Atom *left = atom_var(&source, "inventory-left");
    Atom *right = atom_var(&source, "inventory-right");
    Bindings env;
    bindings_init(&env);
    assert(match_binding_values(binding_value_from_atom(left),
        binding_value_from_context(shared, first), &env, &source));
    assert(match_binding_values(binding_value_from_atom(right),
        binding_value_from_context(shared, second), &env, &source));
    const BindingsFrameIndexEntry *f = bindings_frame_index_find_frame_const(
        env.frame_index, first);
    const BindingsFrameIndexEntry *g = bindings_frame_index_find_frame_const(
        env.frame_index, second);
    assert(f && g && f != g && f->slot_len == 2u && g->slot_len == 2u);
    assert(!f->schema_complete && !g->schema_complete);
    assert(!f->bound_value_count && !g->bound_value_count);
    /* Registration sorts sparse source coordinates. Both contexts retain
     * their own authoritative schema and values. */
    assert(var_id_eq(bindings_frame_slot_source_id(f, 0u), var_source_id(a->var_id)));
    assert(var_id_eq(bindings_frame_slot_source_id(f, 1u), var_source_id(b->var_id)));
    assert(var_id_eq(bindings_frame_slot_source_id(g, 0u), var_source_id(a->var_id)));
    assert(var_id_eq(bindings_frame_slot_source_id(g, 1u), var_source_id(b->var_id)));
    assert(bindings_add_id(&env, var_epoch_id(a->var_id, first), atom_symbol_current_id(a),
                           atom_int(&source, 1)));
    assert(bindings_add_id(&env, var_epoch_id(b->var_id, first), atom_symbol_current_id(b),
                           atom_int(&source, 2)));
    assert(bindings_add_id(&env, var_epoch_id(a->var_id, second), atom_symbol_current_id(a),
                           atom_int(&source, 3)));
    assert(bindings_add_id(&env, var_epoch_id(b->var_id, second), atom_symbol_current_id(b),
                           atom_int(&source, 4)));
    assert(env.len == 2u && env.frame_index->bound_value_count == 4u);
    Atom *result = retained_graph_image(&env, &destination,
        atom_expr2(&source, left, right));
    assert(result && result->expr.elems[0] != result->expr.elems[1]);
    for (unsigned side = 0u; side < 2u; ++side) {
        Atom *node = result->expr.elems[side];
        for (unsigned depth = 0u; depth < 32u; ++depth) {
            assert(node && node->kind == ATOM_EXPR && node->expr.len == 2u);
            assert(node->expr.elems[0] == node->expr.elems[1]);
            node = node->expr.elems[0];
        }
        assert(node && node->kind == ATOM_EXPR && node->expr.len == 2u);
        assert(atom_eq(node->expr.elems[0], atom_int(&source, 1 + 2 * side)));
        assert(atom_eq(node->expr.elems[1], atom_int(&source, 2 + 2 * side)));
    }
    bindings_free(&env);
    arena_free(&destination);
    arena_free(&source);
}

static void frame_inventory_identity_filtering(void) {
    Arena arena;
    arena_init_detached(&arena);
    CETTA_FRAME_IDENTITY_SCOPE(frames);
    CettaFrameIdentity first = cetta_frame_identity_scope_fresh(&frames);
    CettaFrameIdentity second = cetta_frame_identity_scope_fresh(&frames);
    Atom *a = atom_var(&arena, "mixed-a");
    Atom *b = atom_var(&arena, "mixed-b");
    Atom *framed_a = atom_var_like(&arena, a, var_epoch_id(a->var_id, first));
    Atom *framed_b = atom_var_like(&arena, b, var_epoch_id(b->var_id, second));
    Atom *private = atom_var_with_id(&arena, "private",
                                     variant_shape_slot_id(7u));
    Atom *plain = atom_var(&arena, "unframed");
    Atom *items[] = {framed_a, private, plain, framed_b};
    Atom *row = atom_expr(&arena, items, 4u);
    Bindings env;
    bindings_init(&env);
    assert(bindings_add_var(&env, atom_var(&arena, "materialized"),
                             atom_expr2(&arena, row, row)));
    assert(env.frame_index && env.frame_index->len == 2u);
    const BindingsFrameIndexEntry *f = bindings_frame_index_find_frame_const(
        env.frame_index, first);
    const BindingsFrameIndexEntry *g = bindings_frame_index_find_frame_const(
        env.frame_index, second);
    assert(f && g && f->slot_len == 1u && g->slot_len == 1u);
    assert(var_id_eq(bindings_frame_slot_source_id(f, 0u), var_source_id(a->var_id)));
    assert(var_id_eq(bindings_frame_slot_source_id(g, 0u), var_source_id(b->var_id)));
    assert(!bindings_frame_index_find_frame_const(
        env.frame_index, var_epoch_suffix(private->var_id)));
    bindings_free(&env);

    bindings_init(&env);
    Atom *context_items[] = {a, private, b};
    row = atom_expr(&arena, context_items, 3u);
    assert(match_binding_values(
        binding_value_from_atom(atom_var(&arena, "contextual")),
        binding_value_from_context(atom_expr2(&arena, row, row), first),
        &env, &arena));
    f = bindings_frame_index_find_frame_const(env.frame_index, first);
    assert(f && env.frame_index->len == 1u && f->slot_len == 2u);
    assert(var_id_eq(bindings_frame_slot_source_id(f, 0u), var_source_id(a->var_id)));
    assert(var_id_eq(bindings_frame_slot_source_id(f, 1u), var_source_id(b->var_id)));
    bindings_free(&env);
    arena_free(&arena);
}

static void frame_inventory_raw_cycle_refusal(void) {
    Arena arena;
    arena_init_detached(&arena);
    CETTA_FRAME_IDENTITY_SCOPE(frames);
    CettaFrameIdentity epoch = cetta_frame_identity_scope_fresh(&frames);
    Atom *a = atom_var(&arena, "cycle-a");
    Atom *b = atom_var(&arena, "cycle-b");
    a = atom_var_like(&arena, a, var_epoch_id(a->var_id, epoch));
    b = atom_var_like(&arena, b, var_epoch_id(b->var_id, epoch));
    Atom *capture = atom_var(&arena, "cycle-capture");
    Atom *placeholder = atom_symbol(&arena, "placeholder");
    Atom *cycle = atom_expr3(&arena, a, b, placeholder);
    cycle->expr.elems[2] = cycle;
    Bindings env, before;
    bindings_init(&env);
    assert(bindings_add_var(&env, atom_var(&arena, "sentinel"), placeholder));
    assert(bindings_clone(&before, &env));
    assert(!bindings_add_var(&env, capture, cycle));
    assert(bindings_eq(&env, &before));
    assert(!env.frame_index && env.len == before.len);
    Atom *nested = atom_expr2(&arena, cycle, placeholder);
    cycle->expr.elems[2] = nested;
    assert(!bindings_add_var(&env, capture, nested));
    assert(bindings_eq(&env, &before));
    assert(!env.frame_index && env.len == before.len);
    cycle->expr.elems[2] = placeholder;
    assert(bindings_add_var(&env, capture, atom_expr2(&arena, cycle, cycle)));
    const BindingsFrameIndexEntry *f = bindings_frame_index_find_frame_const(
        env.frame_index, epoch);
    assert(f && f->slot_len == 2u && env.len == before.len + 1u);
    assert(var_id_eq(bindings_frame_slot_source_id(f, 0u), var_source_id(a->var_id)));
    assert(var_id_eq(bindings_frame_slot_source_id(f, 1u), var_source_id(b->var_id)));
    bindings_free(&before);
    bindings_free(&env);
    arena_free(&arena);
}

static void frame_inventory_growth_and_cleanup(void) {
    Arena arena;
    arena_init_detached(&arena);
    CETTA_FRAME_IDENTITY_SCOPE(frames);
    CettaFrameIdentity epoch = cetta_frame_identity_scope_fresh(&frames);
    Atom *common = atom_var(&arena, "growth-common");
    common = atom_var_like(&arena, common, var_epoch_id(common->var_id, epoch));
    Atom *rows[64], *variables[64];
    for (unsigned i = 0u; i < 64u; ++i) {
        Atom *variable = atom_var(&arena, "growth-slot");
        variables[i] = atom_var_like(&arena, variable,
                                      var_epoch_id(variable->var_id, epoch));
        rows[i] = atom_expr2(&arena, common, variables[i]);
    }
    Atom *block = atom_expr(&arena, rows, 64u);
    Bindings env;
    bindings_init(&env);
    Atom *capture = atom_var(&arena, "growth-capture");
    assert(bindings_add_var(&env, capture, atom_expr2(&arena, block, block)));
    const BindingsFrameIndexEntry *frame = bindings_frame_index_find_frame_const(
        env.frame_index, epoch);
    assert(frame && frame->slot_len == 65u && frame->schema->len == 65u);
    assert(var_id_eq(bindings_frame_slot_source_id(frame, 0u), var_source_id(common->var_id)));
    for (unsigned i = 0u; i < 64u; ++i)
        assert(var_id_eq(bindings_frame_slot_source_id(frame, i + 1u), var_source_id(variables[i]->var_id)));
    bindings_free(&env);

    /* The rightmost rows grow the work stack, expression memo, variable
     * list and identifier set before the leftmost edge returns to the root. */
    bindings_init(&env);
    Atom *first = block->expr.elems[0];
    block->expr.elems[0] = block;
    assert(!bindings_add_var(&env, capture, block));
    assert(!env.len && !env.frame_index);
    block->expr.elems[0] = first;
    assert(bindings_add_var(&env, capture, block));
    frame = bindings_frame_index_find_frame_const(env.frame_index, epoch);
    assert(frame && frame->slot_len == 65u && env.len == 1u);
    bindings_free(&env);
    arena_free(&arena);
}

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    saved_match();
    saved_permutation();
    saved_open_template();
    retained_application_controls();
    shared_lexical_readings();
    shared_quotation_readings();
    shared_formed_quotation_readings();
    frame_inventory_shared_readings();
    frame_inventory_identity_filtering();
    frame_inventory_raw_cycle_refusal();
    frame_inventory_growth_and_cleanup();
    saved_deep_template();
    malformed_renaming();
    saved_carrier();
    opaque_context_scope();
#if defined(CETTA_PATTERN_ACTUAL_PRIME_HOOKS)
    interpretation_context_controls();
    interpretation_snapshot_controls();
    interpretation_symbol_owner_controls();
    interpretation_width_controls();
#endif
    symbol_table_free(&symbols);
    g_symbols = NULL;
#if defined(CETTA_PATTERN_ACTUAL_PRIME_HOOKS)
    puts("PASS: graph substitution preserves actual Prime quotation readings and aliasing");
#else
    puts("PASS: graph substitution preserves quotation hook readings and aliasing");
#endif
    puts("PASS: shared frame inventories preserve context, filtering and refusal cleanup");
    puts("PASS: saved matching and simultaneous renaming survive source and witness reclamation");
    return 0;
}

#include "space_source_overlay.h"
#include "native_disk_space.h"
#include "atom_wire.h"
#include "parser.h"
#include "petta_search_machine.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static CettaDiskBytes bytes(const char *s) { return (CettaDiskBytes){(const unsigned char *)s, strlen(s)}; }
static Atom *parse(Arena *a, const char *text) {
    Atom **forms = NULL; assert(parse_metta_text(text, a, &forms) == 1);
    Atom *result = forms[0]; free(forms); return result;
}
static PettaMachineHostMode classify(void *ctx, Space *space, Atom *call) {
    (void)ctx; (void)space;
    return atom_head_symbol_id(call) == g_builtin_syms.pat_query ? PETTA_MACHINE_HOST_PATTERN_QUERY : PETTA_MACHINE_HOST_NONE;
}
static Space *resolve(void *ctx, Space *space, Arena *a, Atom *reference) {
    (void)ctx; (void)a; return atom_is_symbol(reference, "&self") ? space : NULL;
}
static size_t query(Space *space, Arena *a, const char *text, const char *expected) {
    PettaMachineHost host = {.classify = classify, .resolve_space = resolve}; PettaMachine machine;
    assert(petta_machine_init(&machine, space, a, parse(a, text), NULL, &host));
    size_t count = 0; PettaMachineStep step; Atom *answer = NULL;
    do {
        Bindings bindings; step = petta_machine_next(&machine, &answer, &bindings); bindings_free(&bindings);
        if (step == PETTA_MACHINE_STEP_ANSWER) {
            ++count;
            if (expected && !atom_eq(answer, parse(a, expected))) {
                fprintf(stderr, "query %s returned %s; expected data %s\n", text, atom_to_string(a, answer), expected);
                abort();
            }
        }
    } while (step == PETTA_MACHINE_STEP_ANSWER || step == PETTA_MACHINE_STEP_SUSPENDED);
    if (step != PETTA_MACHINE_STEP_EXHAUSTED) {
        fprintf(stderr, "query %s on %s stopped with step %d after %zu answers\n",
            text, space->match_backend.ops->name, (int)step, count); abort();
    }
    petta_machine_destroy(&machine); return count;
}
static size_t drain(SpaceOccurrenceCursor *cursor) {
    size_t count = 0; CettaIndex index; SpaceOccurrenceCursorStep step;
    while ((step = space_occurrence_cursor_next(cursor, &index, NULL)) == SPACE_OCCURRENCE_CURSOR_ITEM ||
            step == SPACE_OCCURRENCE_CURSOR_PROGRESS) {
        if (step == SPACE_OCCURRENCE_CURSOR_ITEM) { assert(cursor->source_ops->identity(cursor->source_state, index)); ++count; }
    }
    assert(step == SPACE_OCCURRENCE_CURSOR_END); return count;
}
static CettaMatchCompletion incomplete(Atom *a, Atom *b) { (void)a; (void)b; return CETTA_MATCH_INCOMPLETE; }
static CettaMatchCompletion selects_rel(Atom *a, Atom *b) {
    (void)b;
    return a && a->kind == ATOM_EXPR && a->expr.len && atom_is_symbol(a->expr.elems[0], "rel")
        ? CETTA_MATCH_MATCHED : CETTA_MATCH_REFUTED;
}
static CettaMatchCompletion selects_edge_a(Atom *a, Atom *b) {
    (void)b;
    return a && a->kind == ATOM_EXPR && a->expr.len == 3 &&
        atom_is_symbol(a->expr.elems[0], "edge") && atom_is_symbol(a->expr.elems[1], "a")
        ? CETTA_MATCH_MATCHED : CETTA_MATCH_REFUTED;
}
static CettaMatchCompletion incomplete_after_a(Atom *a, Atom *b) {
    if (selects_edge_a(a, b) == CETTA_MATCH_MATCHED) return CETTA_MATCH_MATCHED;
    return a && a->kind == ATOM_EXPR && a->expr.len == 3 && atom_is_symbol(a->expr.elems[0], "edge")
        ? CETTA_MATCH_INCOMPLETE : CETTA_MATCH_REFUTED;
}
static unsigned paused_polls;
static bool paused_clone(const void *source, bool rewind, void **out) {
    (void)source; (void)rewind; *out = malloc(1); return *out != NULL;
}
static bool paused_open(Space *space, Atom *pattern, void **out, SpaceOccurrenceCursorStep *failure) {
    (void)space; (void)pattern; (void)failure; return paused_clone(NULL, true, out);
}
static SpaceOccurrenceCursorStep paused_next(void *source, CettaIndex *index) {
    (void)source; (void)index; assert(++paused_polls == 1); return SPACE_OCCURRENCE_CURSOR_PROGRESS;
}
static Atom *paused_atom(const void *source, CettaIndex index) { (void)source; (void)index; return NULL; }
static uint64_t paused_length(const Space *space) { (void)space; return 1; }
static const SpaceSourceCursorOps paused_rows = {
    .clone = paused_clone, .next = paused_next, .atom = paused_atom, .identity = paused_atom, .free = free,
};
static const SpaceMatchBackendOps paused_ops = {
    .name = "paused-fixture", .occurrence_ops = &paused_rows, .occurrence_open = paused_open, .logical_len = paused_length,
};

int main(int argc, char **argv) {
    assert(argc == 2);
    SymbolTable symbols; symbol_table_init(&symbols); symbol_table_init_builtins(&symbols, &g_builtin_syms); g_symbols = &symbols;
    Arena data, answers, transient; arena_init_detached(&data); arena_init_detached(&answers); arena_init_detached(&transient);
    CettaDiskStore *store = NULL; CettaDiskStoreView *empty = NULL, *view = NULL;
    assert(cetta_disk_store_open(argv[1], true, NULL, &store) == CETTA_STORE_OK);
    assert(cetta_disk_store_snapshot(store, &empty) == CETTA_STORE_OK);
    const char *facts[] = {"(edge a b)", "(edge a b)", "(edge b c)", "(rel $x $x)", "(= (run) (print! inert))"};
    const char *keys[] = {"facts/a", "facts/b", "facts/c", "facts/d", "facts/e"};
    CettaDiskOp operations[10]; unsigned char *encoded[5], *postings[5]; size_t sizes[5];
    for (size_t i = 0; i < 5; ++i) {
        Atom *fact = parse(&data, facts[i]);
        assert(cetta_atom_wire_encode(fact, &encoded[i], &sizes[i]) == ATOM_WIRE_OK);
        operations[i] = (CettaDiskOp){false, bytes(keys[i]), {encoded[i], sizes[i]}};
        unsigned char *head = NULL; size_t size = 0;
        assert(cetta_atom_wire_encode(fact->expr.elems[0], &head, &size) == ATOM_WIRE_OK);
        const char *prefix = "index/head/exact/"; size_t length = strlen(prefix) + size + 1 + strlen(keys[i]);
        postings[i] = malloc(length); assert(postings[i]);
        memcpy(postings[i], prefix, strlen(prefix)); memcpy(postings[i] + strlen(prefix), head, size);
        postings[i][strlen(prefix) + size] = '/';
        memcpy(postings[i] + strlen(prefix) + size + 1, keys[i], strlen(keys[i])); free(head);
        operations[i + 5] = (CettaDiskOp){false, {postings[i], length}, bytes("")};
    }
    assert(cetta_disk_store_commit(store, empty, operations, 10, &view) == CETTA_STORE_OK);
    for (size_t i = 0; i < 5; ++i) { free(encoded[i]); free(postings[i]); }
    Space *base = malloc(sizeof(*base)); assert(base); space_init(base);
    CettaDiskSpaceIndex declared = {3, 0, bytes("index/head")};
    assert(cetta_disk_space_capture_indexed(base, view, bytes("facts/"), 5, &declared, 1) == CETTA_STORE_OK);
    assert(!space_admit_atom(base, &data, parse(&data, "(edge forbidden mutation)")));
    space_match_backend_clear_error();
    assert(!space_add_atom_ids_batch(base, NULL, 0));
    assert(space_match_backend_last_error_code() == SPACE_MATCH_BACKEND_ERROR_READ_ONLY);
    space_match_backend_clear_error();
    Space overlay, reference; space_init(&overlay); space_init(&reference);
    assert(space_source_overlay_init_owned(&overlay, base, 4096));
    CettaDiskSpaceStatistics statistics;
    assert(cetta_disk_space_statistics(base, &statistics) && !statistics.candidates);
    assert(space_length64(&overlay) == 5 && !overlay.native.len);
    for (size_t i = 0; i < 5; ++i) assert(space_admit_atom(&reference, &data, parse(&data, facts[i])));
    const char *edges = "(match &self (edge a $to) $to)";
    assert(query(&overlay, &answers, edges, "b") == 2);
    assert(cetta_disk_space_statistics(base, &statistics) && statistics.indexed_selections == 1 && statistics.candidates == 3);
    assert(query(&overlay, &answers, edges, "b") == query(&reference, &answers, edges, "b"));
    const char *join = "(match &self (, (edge a $x) (edge $x c)) $x)";
    assert(query(&overlay, &answers, join, "b") == 2);
    assert(query(&overlay, &answers, "(match &self (= (run) $rhs) $rhs)", "(print! inert)") == 1);
    assert(query(&overlay, &answers, "(match &self (rel a b) false)", NULL) == 0);
    assert(query(&overlay, &answers, "(match &self (rel a a) true)", "true") == 1);

    SpaceOccurrenceCursor before; assert(space_occurrence_cursor_init_scan(&overlay, &before));
    assert(space_remove_checked(&overlay, parse(&data, "(edge a b)")) == CETTA_MATCH_MATCHED);
    assert(space_remove_checked(&reference, parse(&data, "(edge a b)")) == CETTA_MATCH_MATCHED);
    assert(space_admit_atom(&overlay, &data, parse(&data, "(edge a d)")));
    assert(space_admit_atom(&reference, &data, parse(&data, "(edge a d)")));
    assert(space_length64(base) == 5 && space_length64(&overlay) == 5 && !overlay.native.len);
    assert(drain(&before) == 5); space_occurrence_cursor_release(&before);
    assert(query(&overlay, &answers, edges, NULL) == query(&reference, &answers, edges, NULL));
    assert(query(&overlay, &answers, join, "b") == 1);

    Atom *added = NULL, *removed = NULL;
    assert(space_source_overlay_changes(&overlay, &answers, &added, &removed));
    assert(added->expr.len == 1 && removed->expr.len == 1);
    assert(atom_eq(added->expr.elems[0], parse(&answers, "(edge a d)")));
    assert(removed->expr.elems[0]->kind == ATOM_EXPR);
    /* The exact duplicate that was removed has the original durable identity. */
    char *printed = atom_to_string(&answers, removed->expr.elems[0]); assert(printed && strstr(printed, "facts/a"));

    /* Atomic admission refuses a malformed second value without a shadow row. */
    uint64_t revision = overlay.revision; CettaCount length = space_length64(&overlay);
    Atom *bad[] = {parse(&data, "(must not publish)"), atom_space(&data, &reference)};
    assert(!space_admit_atoms_batch_from_source_arena(&overlay, &data, &data, bad, 2));
    assert(overlay.revision == revision && space_length64(&overlay) == length && !overlay.native.len);
    char large[5001]; memset(large, 'x', sizeof(large));
    assert(!space_admit_atom(&overlay, &data, atom_string_n(&data, large, sizeof(large))));
    assert(overlay.revision == revision && space_length64(&overlay) == length);
    assert(space_remove_compared(&overlay, parse(&data, "(edge a b)"), incomplete, incomplete) == CETTA_MATCH_INCOMPLETE);
    assert(overlay.revision == revision && space_length64(&overlay) == length);
    assert(!space_truncate64(&overlay, 0) && !space_pop(&overlay, NULL));
    assert(!space_remove_occurrence_mask_stable(&overlay, NULL, 0, NULL));

    /* A borrowed input may disappear immediately after admission. */
    assert(space_admit_atom_from_source_arena(&overlay, &data, &transient, parse(&transient, "(temporary retained)")));
    arena_free(&transient); arena_init_detached(&transient);
    assert(query(&overlay, &answers, "(match &self (temporary $x) $x)", "retained") == 1);

    /* Alpha-equivalent open rows remain ambiguous until there is only one. */
    Atom *open[] = {parse(&data, "(other $a $a)"), parse(&data, "(other $b $b)")};
    assert(space_admit_atoms_batch_from_source_arena(&overlay, &data, &data, open, 2));
    assert(space_remove_checked(&overlay, parse(&data, "(other $c $c)")) == CETTA_MATCH_REFUTED);
    assert(space_remove_checked(&overlay, open[0]) == CETTA_MATCH_MATCHED);
    assert(space_remove_checked(&overlay, parse(&data, "(other $c $c)")) == CETTA_MATCH_MATCHED);
    assert(query(&overlay, &answers, "(match &self (other a a) true)", NULL) == 0);

    /* Clone retains the current loan; select retains the old delta snapshot. */
    SpaceOccurrenceCursor captured, clone, selected;
    assert(space_occurrence_cursor_init_scan(&overlay, &captured)); CettaIndex index;
    SpaceOccurrenceCursorStep step;
    do { step = space_occurrence_cursor_next(&captured, &index, NULL); } while (step == SPACE_OCCURRENCE_CURSOR_PROGRESS);
    assert(step == SPACE_OCCURRENCE_CURSOR_ITEM);
    Atom *identity = atom_deep_copy(&answers, captured.source_ops->identity(captured.source_state, index));
    assert(space_occurrence_cursor_clone(&captured, &clone));
    assert(atom_eq(clone.source_ops->identity(clone.source_state, index), identity));
    assert(space_remove_checked(&overlay, parse(&data, "(edge a d)")) == CETTA_MATCH_MATCHED);
    assert(space_admit_atom(&overlay, &data, parse(&data, "(edge a newer)")));
    assert(space_occurrence_cursor_select(&captured, parse(&data, "(edge a $x)"), &selected));
    assert(drain(&selected) == 4); /* Indexed base candidates plus the old local delta. */
    space_occurrence_cursor_release(&selected);
    assert(drain(&captured) == length); assert(drain(&clone) == length);
    space_occurrence_cursor_release(&captured); space_occurrence_cursor_release(&clone);
    assert(space_source_overlay_changes(&overlay, &answers, &added, &removed));
    assert(added->expr.len == 2 && removed->expr.len == 1);
    assert(query(&overlay, &answers, "(match &self (edge a newer) true)", "true") == 1);

    CettaStorageCensus census = {0};
    space_storage_census(&overlay, &census); assert(!census.incomplete && !census.overflow); cetta_storage_census_free(&census);
    SpaceOccurrenceCursor survivor; assert(space_occurrence_cursor_init_scan(&overlay, &survivor));
    space_free(&overlay); assert(drain(&survivor) == 6); space_occurrence_cursor_release(&survivor);
    added=removed=atom_symbol(&answers,"not-cleared");
    assert(!space_source_overlay_changes(&overlay,&answers,&added,&removed));
    assert(!added && !removed);
    space_free(&reference);

    /* A generated update stream agrees with the native Space oracle for every
     * projected value, including multiplicity, rather than formatted output. */
    Space generated, oracle; space_init(&generated); space_init(&oracle);
    Space *indexed = malloc(sizeof(*indexed)); assert(indexed); space_init(indexed);
    assert(cetta_disk_space_capture_indexed(indexed, view, bytes("facts/"), 5, &declared, 1) == CETTA_STORE_OK);
    assert(space_source_overlay_init_owned(&generated, indexed, 4096));
    for (size_t i = 0; i < 5; ++i) assert(space_admit_atom(&oracle, &data, parse(&data, facts[i])));
    unsigned random = 719;
    for (unsigned operation = 0; operation < 64; ++operation) {
        random = random * 1664525u + 1013904223u; unsigned value = (random >> 16) % 8;
        char text[80]; snprintf(text, sizeof(text), "(edge a n%u)", value); Atom *fact = parse(&data, text);
        if (random & 1) {
            assert(space_admit_atom(&generated, &data, fact)); assert(space_admit_atom(&oracle, &data, fact));
        } else assert(space_remove_checked(&generated, fact) == space_remove_checked(&oracle, fact));
        assert(space_length64(&generated) == space_length64(&oracle));
        for (unsigned projected = 0; projected < 8; ++projected) {
            snprintf(text, sizeof(text), "(match &self (edge a n%u) n%u)", projected, projected);
            char expected[16]; snprintf(expected, sizeof(expected), "n%u", projected);
            assert(query(&generated, &answers, text, expected) == query(&oracle, &answers, text, expected));
        }
    }
    /* Interpreted equality must not be narrowed by the body's unrelated head. */
    assert(space_remove_compared(&generated, parse(&data, "(unrelated body)"), selects_rel, selects_rel) == CETTA_MATCH_MATCHED);
    assert(query(&generated, &answers, "(match &self (rel a a) true)", NULL) == 0);
    space_free(&generated); space_free(&oracle);

    Space filtered; space_init(&filtered); Space *filter_base = malloc(sizeof(*filter_base)); assert(filter_base); space_init(filter_base);
    assert(cetta_disk_space_capture(filter_base, view, bytes("facts/"), 5) == CETTA_STORE_OK);
    assert(space_source_overlay_init_owned(&filtered, filter_base, 4096));
    assert(space_admit_atom(&filtered, &data, parse(&data, "(edge a local)")));
    CettaCount affected = 99; uint64_t filter_revision = filtered.revision;
    assert(space_remove_all_compared(&filtered, parse(&data, "filter"), incomplete_after_a, &affected) == CETTA_MATCH_INCOMPLETE);
    assert(!affected && filtered.revision == filter_revision && space_length64(&filtered) == 6);
    assert(space_source_overlay_changes(&filtered, &answers, &added, &removed));
    assert(added->expr.len == 1 && !removed->expr.len);
    assert(space_remove_up_to_compared(&filtered, parse(&data, "filter"), incomplete_after_a, 0, &affected) == CETTA_MATCH_REFUTED);
    assert(!affected && filtered.revision == filter_revision);
    assert(space_remove_up_to_compared(&filtered, parse(&data, "filter"), incomplete_after_a, 1, &affected) == CETTA_MATCH_MATCHED);
    assert(affected == 1 && space_length64(&filtered) == 5);
    filter_revision = filtered.revision;
    assert(space_remove_up_to_compared(&filtered, parse(&data, "filter"), incomplete_after_a, 2, &affected) == CETTA_MATCH_INCOMPLETE);
    assert(!affected && filtered.revision == filter_revision && space_length64(&filtered) == 5);
    assert(space_remove_all_compared(&filtered, parse(&data, "filter"), selects_edge_a, &affected) == CETTA_MATCH_MATCHED);
    assert(affected == 2 && space_length64(&filtered) == 3);
    assert(query(&filtered, &answers, "(match &self (edge a $x) $x)", NULL) == 0);
    assert(space_source_overlay_changes(&filtered, &answers, &added, &removed));
    assert(!added->expr.len && removed->expr.len == 2); space_free(&filtered);

    /* Existing local-overlay callers use the same adapter and retain no
     * borrowed source owner, index descriptor, or byte-policy default. */
    Space ordinary_base, ordinary_overlay; space_init(&ordinary_base);
    assert(cetta_disk_space_capture_indexed_bounded(&ordinary_base, view, bytes("facts/"), 5,
        &declared, 1, 4096) == CETTA_STORE_OK);
    assert(!space_init_overlay_local(&ordinary_base, &ordinary_base));
    assert(space_length64(&ordinary_base) == 5);
    assert(space_init_overlay_local(&ordinary_overlay, &ordinary_base));
    space_free(&ordinary_base);
    assert(query(&ordinary_overlay, &answers, edges, "b") == 2);
    assert(space_admit_atom(&ordinary_overlay, &data, parse(&data, "(edge a local)")));
    assert(space_remove_checked(&ordinary_overlay, parse(&data, "(edge a b)")) == CETTA_MATCH_MATCHED);
    assert(query(&ordinary_overlay, &answers, edges, NULL) == 2);
    assert(!space_admit_atom(&ordinary_overlay, &data, atom_string_n(&data, large, sizeof(large))));
    space_free(&ordinary_overlay);

    unsigned char *point_bytes[2]; size_t point_sizes[2]; CettaDiskOp point_ops[2];
    for (size_t i = 0; i < 2; ++i) {
        assert(cetta_atom_wire_encode(parse(&data, facts[i ? 2 : 0]), &point_bytes[i], &point_sizes[i]) == ATOM_WIRE_OK);
        point_ops[i] = (CettaDiskOp){false, bytes(i ? "point/aa" : "point/a"), {point_bytes[i], point_sizes[i]}};
    }
    CettaDiskStoreView *point_view = NULL;
    assert(cetta_disk_store_commit(store, view, point_ops, 2, &point_view) == CETTA_STORE_OK);
    for (size_t i = 0; i < 2; ++i) free(point_bytes[i]);
    Space point_base, point_overlay; space_init(&point_base);
    assert(cetta_disk_space_capture_point_bounded(&point_base, point_view, bytes("point/a"), 4096) == CETTA_STORE_OK);
    assert(space_init_overlay_local(&point_overlay, &point_base)); space_free(&point_base);
    assert(query(&point_overlay, &answers, "(match &self (edge a b) hit)", "hit") == 1);
    assert(query(&point_overlay, &answers, "(match &self (edge b c) wrong)", NULL) == 0);
    space_free(&point_overlay); cetta_disk_store_view_close(point_view);

    Space pending; space_init(&pending); Space *paused = malloc(sizeof(*paused)); assert(paused); space_init(paused);
    paused->match_backend.kind = SPACE_ENGINE_SOURCE; paused->match_backend.ops = &paused_ops;
    assert(space_source_overlay_init_owned(&pending, paused, 4096));
    assert(space_remove_checked(&pending, parse(&data, "(edge a b)")) == CETTA_MATCH_INCOMPLETE);
    assert(paused_polls == 1 && space_length64(&pending) == 1);
    paused_polls = 0;
    assert(space_remove_all_compared(&pending, parse(&data, "filter"), selects_edge_a, &affected) == CETTA_MATCH_INCOMPLETE);
    assert(paused_polls == 1 && !affected && space_length64(&pending) == 1);
    assert(space_source_overlay_changes(&pending, &answers, &added, &removed));
    assert(!added->expr.len && !removed->expr.len); space_free(&pending);
    cetta_disk_store_view_close(view); cetta_disk_store_view_close(empty); cetta_disk_store_close(store);
    arena_free(&transient); arena_free(&answers); arena_free(&data); symbol_table_free(&symbols);
    puts("source overlay: ordinary queries, identities, atomic edits, snapshot cursors and rollback passed");
    return 0;
}

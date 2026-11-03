#include "atom.h"
#include "proof_row.h"
#include "space.h"
#include "symbol.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { CALLBACK_BIRTHS = 8u, PROOF_ROWS = 5u };

typedef struct {
    TermUniverse universe;
    Arena persistent, scratch;
} Fixture;

typedef struct {
    TermUniverse *universe;
    Space *space, *retired_space;
    AtomId born[CALLBACK_BIRTHS];
    size_t born_count, before_len, before_capacity;
    unsigned references, finalizations;
    bool keep_births;
} CallbackOwner;

typedef struct {
    CettaProofImage *image;
    CettaProofRowLayout layout;
    AtomId states[3], seed, one, m, three;
} ProofData;

static const char *format_name(TermUniverseStoreFormat format) {
    return format == TERM_UNIVERSE_STORE_FORMAT_COMPACT32_V1
        ? "compact" : "wide";
}

static void fixture_init(Fixture *fixture, TermUniverseStoreFormat format) {
    arena_init_detached(&fixture->persistent);
    arena_init_detached(&fixture->scratch);
    assert(term_universe_init_with_store_format(&fixture->universe, format));
    term_universe_set_persistent_arena(&fixture->universe,
                                        &fixture->persistent);
    term_universe_set_collection_mode(&fixture->universe,
                                       TERM_UNIVERSE_COLLECTION_RECLAIM);
    assert(term_universe_collect(&fixture->universe, NULL));
    assert(fixture->universe.counting);
}

static void fixture_free(Fixture *fixture) {
    TermUniverse *universe = &fixture->universe;
    TermUniverseCollectionReport report;
    assert(term_universe_collect(universe, &report));
    assert(report.reclaimed == 0u && universe->live_entries == 0u);
    assert(universe->root_provider_len == 0u && universe->fallback_bytes == 0u);
    assert(!universe->collection_inhibit && !universe->counted_sweep_active);
    term_universe_free(universe);
    arena_free(&fixture->scratch);
    arena_free(&fixture->persistent);
}

static void born_roots(const TermUniverse *universe,
                        TermUniverseRootVisit visit, void *visit_context,
                        void *context) {
    const CallbackOwner *owner = context;
    (void)universe;
    for (size_t i = 0u; i < owner->born_count; i++)
        visit(visit_context, owner->born[i]);
}

static void owner_retain(void *context) {
    CallbackOwner *owner = context;
    owner->references++;
}

/* This is an actual native-handle finalizer, reached only when the TU
 * releases its opaque image after the source Arena has retired. */
static void owner_release(void *context) {
    CallbackOwner *owner = context;
    assert(owner->references != 0u);
    if (--owner->references != 0u)
        return;
    owner->finalizations++;
    TermUniverse *universe = owner->universe;
    assert(universe->collection_inhibit == 1u);
    owner->before_len = universe->len;
    owner->before_capacity = universe->cap;
    assert(owner->before_len == owner->before_capacity);
    for (size_t i = 0u; i < CALLBACK_BIRTHS; i++) {
        AtomId id = tu_intern_int(universe, INT64_C(2000000) + (int64_t)i);
        assert(id != CETTA_ATOM_ID_NONE);
        owner->born[owner->born_count++] = id;
        if (owner->keep_births)
            term_universe_hold_counted(universe, id);
    }
    assert(universe->len == owner->before_len + CALLBACK_BIRTHS);
    assert(universe->cap > owner->before_capacity);
    if (owner->keep_births)
        assert(term_universe_add_counted_root_provider(
            universe, born_roots, owner));
    if (owner->space) {
        owner->retired_space = owner->space;
        owner->space = NULL;
        space_free(owner->retired_space);
        /* Keep only the C struct allocation until any occurrence cursor
         * is released. Its Space storage and provider are already retired. */
        assert(owner->retired_space->instance_id == 0u);
    }
    assert(universe->collection_inhibit == 1u);
}

static void install_opaque_owner(Fixture *fixture, CallbackOwner *owner) {
    Arena source;
    arena_init_detached(&source);
    Atom *handle = atom_native_handle_identifier(
        &source, 73, owner, owner_retain, owner_release);
    assert(handle && owner->references == 1u);
    assert(term_universe_store_atom_id(&fixture->universe, &source, handle)
           != CETTA_ATOM_ID_NONE);
    assert(owner->references == 2u);
    arena_free(&source);
    assert(owner->references == 1u && owner->finalizations == 0u);
    assert(fixture->universe.fallback_bytes != 0u);

    /* Ordinary allocations fill the real vector. There is no synthetic
     * capacity override: the finalizer must force its actual reallocation. */
    assert(fixture->universe.free_slot_len == 0u);
    for (int64_t i = 0; fixture->universe.len < fixture->universe.cap; i++)
        assert(tu_intern_int(&fixture->universe, INT64_C(1000000) + i)
               != CETTA_ATOM_ID_NONE);
    assert(fixture->universe.len == fixture->universe.cap);
}

static void verify_births(Fixture *fixture, CallbackOwner *owner) {
    TermUniverse *universe = &fixture->universe;
    assert(owner->finalizations == 1u && owner->references == 0u);
    assert(owner->born_count == CALLBACK_BIRTHS);
    assert(!universe->collection_inhibit && !universe->counted_sweep_active);
    assert(universe->fallback_bytes == 0u);
    for (size_t i = 0u; i < CALLBACK_BIRTHS; i++) {
        Atom *value = atom_int(&fixture->scratch,
                              INT64_C(2000000) + (int64_t)i);
        AtomId found = term_universe_lookup_atom_id(universe, value);
        assert(found == (owner->keep_births ? owner->born[i]
                                          : CETTA_ATOM_ID_NONE));
    }
}

static void drop_births(CallbackOwner *owner) {
    if (!owner->keep_births)
        return;
    term_universe_remove_root_provider(owner->universe, born_roots, owner);
    for (size_t i = 0u; i < owner->born_count; i++)
        term_universe_release_counted(owner->universe, owner->born[i]);
    owner->born_count = 0u;
}

static void callback_births(TermUniverseStoreFormat format, bool keep) {
    Fixture fixture;
    fixture_init(&fixture, format);
    CallbackOwner owner = {.universe = &fixture.universe, .keep_births = keep};
    install_opaque_owner(&fixture, &owner);
    TermUniverseCollectionReport report;
    assert(term_universe_collect(&fixture.universe, &report));
    verify_births(&fixture, &owner);
    assert(fixture.universe.live_entries == (keep ? CALLBACK_BIRTHS : 0u));
    drop_births(&owner);
    assert(term_universe_collect(&fixture.universe, &report));
    assert(report.reclaimed == (keep ? CALLBACK_BIRTHS : 0u));
    fixture_free(&fixture);
    printf("PASS: %s callback births %s across actual entry-vector growth\n",
           format_name(format), keep ? "survive an independent holder"
                                     : "retire in the same sweep");
}

static AtomId expression(TermUniverse *universe, AtomId *children,
                         CettaExprLen count) {
    AtomId result = tu_expr_from_ids(universe, children, count);
    assert(result != CETTA_ATOM_ID_NONE);
    return result;
}

static AtomId state_key(TermUniverse *universe, AtomId head, AtomId c,
                        AtomId pair, int64_t remaining, int64_t hyp,
                        int64_t left, int64_t right) {
    AtomId body_children[] = {pair, tu_intern_int(universe, left),
                              tu_intern_int(universe, right)};
    AtomId body = expression(universe, body_children, 3u);
    AtomId c_children[] = {c, body};
    AtomId erased = expression(universe, c_children, 2u);
    AtomId children[] = {head, tu_intern_int(universe, remaining),
                         tu_intern_int(universe, hyp), erased};
    return expression(universe, children, 4u);
}

static ProofData make_proof_data(TermUniverse *universe) {
    SymbolId head_symbol = symbol_intern_cstr(g_symbols, "callback-proofs");
    SymbolId c_symbol = symbol_intern_cstr(g_symbols, "C");
    AtomId head = tu_intern_symbol(universe, head_symbol);
    AtomId c = tu_intern_symbol(universe, c_symbol);
    AtomId pair = tu_intern_symbol(universe,
                                   symbol_intern_cstr(g_symbols, "pair"));
    ProofData data = {
        .seed = tu_intern_symbol(universe, symbol_intern_cstr(g_symbols, "I")),
        .one = tu_intern_int(universe, 1),
        .m = tu_intern_symbol(universe, symbol_intern_cstr(g_symbols, "M")),
        .three = tu_intern_int(universe, 3),
        .layout = {.proof = {.length = 2u, .child = {3u, 2u}},
                   .arity = {4u, 3u}, .head = {head_symbol, c_symbol},
                   .universe_instance = universe->instance_id,
                   .storage_epoch = universe->storage_epoch},
    };
    data.states[0] = state_key(universe, head, c, pair, 2, 2, 11, 11);
    data.states[1] = state_key(universe, head, c, pair, 1, 1, 11, 12);
    data.states[2] = state_key(universe, head, c, pair, 0, 0, 12, 11);
    CettaProofBuilder *builder = NULL;
    assert(cetta_proof_builder_new(universe, data.states[0], data.seed,
        (CettaProofLimits){8u, 8u, 4u, 100u, 100u}, &builder) == CETTA_PROOF_OK);
    uint32_t child, last;
    CettaProofOffer offer;
    assert(cetta_proof_builder_state(builder, data.states[1], &child) == CETTA_PROOF_OK);
    assert(cetta_proof_builder_offer(builder, 0u, data.one, child, &offer) == CETTA_PROOF_OK);
    assert(cetta_proof_builder_offer(builder, 0u, data.m, child, &offer) == CETTA_PROOF_OK);
    assert(cetta_proof_builder_advance(builder) == CETTA_PROOF_OK);
    assert(cetta_proof_builder_state(builder, data.states[2], &last) == CETTA_PROOF_OK);
    assert(cetta_proof_builder_offer(builder, child, data.three, last, &offer) == CETTA_PROOF_OK);
    assert(cetta_proof_builder_seal(builder, &data.image) == CETTA_PROOF_OK);
    cetta_proof_builder_free(builder);
    assert(cetta_proof_image_stats(data.image).support == PROOF_ROWS);
    return data;
}

/* The independently expected five words are I, 1I, MI, 31I and 3MI. */
static void verify_words(CettaProofCursor *cursor, const ProofData *data) {
    unsigned seen[PROOF_ROWS] = {0};
    unsigned rows = 0u;
    CettaProofBudget budget = {128u, 0u};
    for (;;) {
        CettaProofWord word;
        CettaProofNext next = cetta_proof_cursor_peek(cursor, &budget, &word);
        if (next == CETTA_PROOF_END)
            break;
        assert(next == CETTA_PROOF_ROW && rows++ < PROOF_ROWS);
        assert(word.seed == data->seed);
        unsigned which = PROOF_ROWS;
        if (word.state_key == data->states[0] && word.label_count == 0u)
            which = 0u;
        else if (word.state_key == data->states[1] && word.label_count == 1u)
            which = word.outer_labels[0] == data->one ? 1u
                : word.outer_labels[0] == data->m ? 2u : PROOF_ROWS;
        else if (word.state_key == data->states[2] && word.label_count == 2u &&
                 word.outer_labels[0] == data->three)
            which = word.outer_labels[1] == data->one ? 3u
                : word.outer_labels[1] == data->m ? 4u : PROOF_ROWS;
        assert(which < PROOF_ROWS && seen[which]++ == 0u);
        assert(cetta_proof_cursor_commit(cursor, word.token));
    }
    assert(rows == PROOF_ROWS);
    for (unsigned i = 0u; i < PROOF_ROWS; i++)
        assert(seen[i] == 1u);
}

enum RetainedClient { NO_CLIENT, IMAGE_CLIENT, CURSOR_CLIENT, CAPTURED_PREFIX };

static void callback_space(TermUniverseStoreFormat format,
                            enum RetainedClient client) {
    Fixture fixture;
    fixture_init(&fixture, format);
    TermUniverse *universe = &fixture.universe;
    ProofData data = make_proof_data(universe);
    Space *space = malloc(sizeof(*space));
    assert(space);
    space_init_with_universe(space, universe);
    space->kind = SPACE_KIND_SET;
    Atom *prefix_parts[] = {atom_symbol(&fixture.scratch, "retained-prefix"),
                            atom_int(&fixture.scratch, 9)};
    Atom *prefix = atom_expr(&fixture.scratch, prefix_parts, 2u);
    assert(prefix);
    if (client == CAPTURED_PREFIX)
        space_add(space, prefix);
    assert(space_attach_proof_image(space, data.layout.head[0],
        &data.layout, data.image) == SPACE_PROOF_IMAGE_READY);
    assert(space_has_proof_image(space, data.layout.head[0]));

    CettaProofImage *image_client = NULL;
    CettaProofCursor *cursor_client = NULL;
    SpaceOccurrenceCursor captured;
    space_occurrence_cursor_init_empty(&captured);
    if (client == IMAGE_CLIENT) {
        assert(cetta_proof_image_retain(data.image));
        image_client = data.image;
    } else if (client == CURSOR_CLIENT) {
        assert(cetta_proof_cursor_new(data.image, CETTA_PROOF_ALL_STATES,
                                      &cursor_client) == CETTA_PROOF_OK);
    } else if (client == CAPTURED_PREFIX) {
        assert(space_occurrence_cursor_init(space, prefix, &captured));
        assert(space_has_proof_image(space, data.layout.head[0]));
    }
    /* The constructor's builder and image handle are both gone before
     * the native handle becomes the only owner of the actual Space. */
    cetta_proof_image_release(data.image);
    data.image = NULL;
    CallbackOwner owner = {.universe = universe, .space = space};
    install_opaque_owner(&fixture, &owner);
    assert(term_universe_collect(universe, NULL));
    verify_births(&fixture, &owner);
    assert(!owner.space && owner.retired_space == space);

    if (image_client || cursor_client) {
        assert(tu_hdr(universe, data.states[0]));
        if (image_client)
            assert(cetta_proof_cursor_new(image_client, CETTA_PROOF_ALL_STATES,
                                          &cursor_client) == CETTA_PROOF_OK);
        verify_words(cursor_client, &data);
        cetta_proof_cursor_free(cursor_client);
        cetta_proof_image_release(image_client);
    } else if (client == CAPTURED_PREFIX) {
        /* No extra counted hold masks the transfer from the Space to its
         * newly detached, independently rooted occurrence capture. */
        AtomId prefix_id = term_universe_lookup_atom_id(universe, prefix);
        assert(prefix_id != CETTA_ATOM_ID_NONE);
        assert(term_universe_collect(universe, NULL));
        CettaIndex index = UINT64_MAX;
        Atom *row = NULL;
        assert(space_occurrence_cursor_next(&captured, &index, &row)
               == SPACE_OCCURRENCE_CURSOR_ITEM);
        assert(index == 0u && row && atom_eq(row, prefix));
        assert(space_occurrence_cursor_next(&captured, &index, &row)
               == SPACE_OCCURRENCE_CURSOR_END);
        space_occurrence_cursor_release(&captured);
    } else {
        assert(universe->live_entries == 0u && universe->root_provider_len == 0u);
    }
    free(owner.retired_space);
    assert(term_universe_collect(universe, NULL));
    fixture_free(&fixture);
    printf("PASS: %s callback retires actual proof Space with client %u\n",
           format_name(format), (unsigned)client);
}

int main(int argc, char **argv) {
    assert(setvbuf(stdout, NULL, _IOLBF, 0) == 0);
    assert(argc == 1 || (argc == 2 &&
        (!strcmp(argv[1], "--compact") || !strcmp(argv[1], "--wide"))));
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    const TermUniverseStoreFormat formats[] = {
        TERM_UNIVERSE_STORE_FORMAT_COMPACT32_V1,
        TERM_UNIVERSE_STORE_FORMAT_WIDE64_V1,
    };
    for (size_t i = 0u; i < sizeof(formats) / sizeof(formats[0]); i++) {
        if (argc == 2 && ((!strcmp(argv[1], "--compact") && i != 0u) ||
                         (!strcmp(argv[1], "--wide") && i != 1u)))
            continue;
        callback_births(formats[i], false);
        callback_births(formats[i], true);
        for (enum RetainedClient client = NO_CLIENT; client <= CAPTURED_PREFIX;
             client++)
            callback_space(formats[i], client);
    }
    g_symbols = NULL;
    symbol_table_free(&symbols);
    puts("PASS: callback collection preserves births, image clients and captured rows");
    return 0;
}

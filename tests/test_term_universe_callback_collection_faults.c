#define main maintained_callback_collection_main
#include "test_term_universe_callback_collection.c"
#undef main

/* Linker wrappers refuse only the temporary root-list allocation while a
 * real finalizer registers a detached captured-row provider. */
static TermUniverse *subject;
static bool inside_registration, refuse_once, force_move;
static size_t refused_size;
static unsigned registrations, registered, matching_allocations, refusals, moves;

bool __real_term_universe_add_root_provider(
    TermUniverse *, TermUniverseRootProvider, void *);
bool __wrap_term_universe_add_root_provider(
        TermUniverse *u, TermUniverseRootProvider fn, void *context) {
    bool previous = inside_registration;
    bool targeted = u == subject && u->counted_sweep_active;
    if (targeted) {
        inside_registration = true;
        registrations++;
    }
    bool result = __real_term_universe_add_root_provider(u, fn, context);
    if (targeted && result)
        registered++;
    inside_registration = previous;
    return result;
}

void *__real_realloc(void *, size_t);
void *__wrap_realloc(void *old, size_t bytes) {
    if (inside_registration &&
        ((bytes == 256u * sizeof(size_t) && !old) ||
         (bytes == 512u * sizeof(size_t) && old))) {
        matching_allocations++;
        if (refuse_once && bytes == refused_size) {
            refuse_once = false;
            refusals++;
            return NULL;
        }
        if (force_move && bytes == 512u * sizeof(size_t)) {
            void *replacement = malloc(bytes);
            assert(replacement);
            memcpy(replacement, old, 256u * sizeof(size_t));
            free(old);
            moves++;
            return replacement;
        }
    }
    return __real_realloc(old, bytes);
}

enum { MAX_TRANSFERS = 2u, MAX_PREFIX = 160u };
typedef struct {
    TermUniverse *universe;
    Space *spaces[MAX_TRANSFERS];
    Atom *prefix[MAX_TRANSFERS][MAX_PREFIX];
    AtomId prefix_ids[MAX_TRANSFERS][MAX_PREFIX];
    SpaceOccurrenceCursor captures[MAX_TRANSFERS];
    AtomId born[CALLBACK_BIRTHS];
    unsigned references, finalized, count, prefix_count;
} TransferOwner;

static void transfer_retain(void *context) {
    ((TransferOwner *)context)->references++;
}

static void transfer_release(void *context) {
    TransferOwner *owner = context;
    assert(owner->references);
    if (--owner->references)
        return;
    owner->finalized++;
    assert(owner->universe->collection_inhibit == 1u);
    /* These ordinary unrooted births must remain queued on an incomplete
     * sweep and disappear after the successful retry. */
    for (unsigned i = 0u; i < CALLBACK_BIRTHS; i++) {
        owner->born[i] = tu_intern_int(owner->universe,
                                       INT64_C(3000000) + i);
        assert(owner->born[i] != CETTA_ATOM_ID_NONE);
    }
    for (unsigned i = 0u; i < owner->count; i++) {
        space_free(owner->spaces[i]);
        assert(owner->spaces[i]->instance_id == 0u);
    }
    assert(owner->universe->collection_inhibit == 1u);
}

static void assert_prefixes(Fixture *fixture, const TransferOwner *owner) {
    for (unsigned i = 0u; i < owner->count; i++)
        for (unsigned j = 0u; j < owner->prefix_count; j++)
            assert(term_universe_lookup_atom_id(&fixture->universe,
                owner->prefix[i][j]) == owner->prefix_ids[i][j]);
}

static void transfer_control(TermUniverseStoreFormat format, unsigned kind) {
    Fixture fixture;
    fixture_init(&fixture, format);
    TermUniverse *u = &fixture.universe;
    ProofData proof = make_proof_data(u);
    TransferOwner owner = {.universe = u, .count = kind == 0u ? 1u : 2u,
                           .prefix_count = kind == 0u ? 1u : MAX_PREFIX};
    for (unsigned i = 0u; i < owner.count; i++) {
        owner.spaces[i] = malloc(sizeof(*owner.spaces[i]));
        assert(owner.spaces[i]);
        space_init_with_universe(owner.spaces[i], u);
        owner.spaces[i]->kind = SPACE_KIND_SET;
        for (unsigned j = 0u; j < owner.prefix_count; j++) {
            char head[80];
            snprintf(head, sizeof(head), "transfer-prefix-%u-%u", i, j);
            Atom *parts[] = {atom_symbol(&fixture.scratch, head),
                              atom_int(&fixture.scratch, (int64_t)j)};
            owner.prefix[i][j] = atom_expr(&fixture.scratch, parts, 2u);
            assert(owner.prefix[i][j]);
            space_add(owner.spaces[i], owner.prefix[i][j]);
            owner.prefix_ids[i][j] = term_universe_lookup_atom_id(
                u, owner.prefix[i][j]);
            assert(owner.prefix_ids[i][j] != CETTA_ATOM_ID_NONE);
        }
        assert(space_attach_proof_image(owner.spaces[i], proof.layout.head[0],
            &proof.layout, proof.image) == SPACE_PROOF_IMAGE_READY);
        space_occurrence_cursor_init_empty(&owner.captures[i]);
        assert(space_occurrence_cursor_init(owner.spaces[i],
            owner.prefix[i][0], &owner.captures[i]));
        assert(space_has_proof_image(owner.spaces[i], proof.layout.head[0]));
    }
    cetta_proof_image_release(proof.image);
    proof.image = NULL;
    Arena source;
    arena_init_detached(&source);
    Atom *handle = atom_native_handle_identifier(&source, 74, &owner,
                                                 transfer_retain, transfer_release);
    assert(handle && owner.references == 1u);
    assert(term_universe_store_atom_id(u, &source, handle) != CETTA_ATOM_ID_NONE);
    assert(owner.references == 2u);
    arena_free(&source);
    assert(owner.references == 1u && owner.finalized == 0u);

    subject = u;
    inside_registration = false;
    refuse_once = kind != 2u;
    force_move = kind == 2u;
    refused_size = (kind == 0u ? 256u : 512u) * sizeof(size_t);
    registrations = registered = matching_allocations = refusals = moves = 0u;
    uint64_t epoch = u->collection_epoch;
    size_t old_highwater = u->collection_len_after;
    bool complete = term_universe_collect(u, NULL);
    assert(owner.finalized == 1u && owner.references == 0u);
    assert(registrations == owner.count && registered == owner.count);
    assert(u->root_provider_len == owner.count);
    assert(!u->collection_inhibit && !u->counted_sweep_active);
    assert(u->collection_epoch > epoch);
    assert_prefixes(&fixture, &owner);
    if (kind != 2u) {
        assert(!complete && refusals == 1u && !refuse_once);
        assert(u->last_error == TERM_UNIVERSE_ERROR_ALLOCATION_FAILED);
        assert(u->collection_len_after == old_highwater && u->zero_len > 0u);
        for (unsigned i = 0u; i < CALLBACK_BIRTHS; i++)
            assert(tu_hdr(u, owner.born[i]));
        term_universe_clear_error(u);
        assert(term_universe_collect(u, NULL));
    } else {
        assert(complete && !refusals && moves == 1u);
    }
    assert_prefixes(&fixture, &owner);
    for (unsigned i = 0u; i < CALLBACK_BIRTHS; i++)
        assert(term_universe_lookup_atom_id(u,
            atom_int(&fixture.scratch, INT64_C(3000000) + i)) == CETTA_ATOM_ID_NONE);
    subject = NULL;
    for (unsigned i = 0u; i < owner.count; i++) {
        CettaIndex index = UINT64_MAX;
        Atom *row = NULL;
        assert(space_occurrence_cursor_next(&owner.captures[i], &index, &row)
               == SPACE_OCCURRENCE_CURSOR_ITEM);
        assert(index == 0u && row && atom_eq(row, owner.prefix[i][0]));
        assert(space_occurrence_cursor_next(&owner.captures[i], &index, &row)
               == SPACE_OCCURRENCE_CURSOR_END);
        space_occurrence_cursor_release(&owner.captures[i]);
        free(owner.spaces[i]);
    }
    assert(term_universe_collect(u, NULL));
    fixture_free(&fixture);
    printf("PASS_TRANSFER format=%s kind=%u roots=%u registrations=%u "
           "matched_allocations=%u refusals=%u forced_moves=%u\n",
           format_name(format), kind, owner.count * owner.prefix_count,
           registered, matching_allocations, refusals, moves);
}

int main(void) {
    assert(setvbuf(stdout, NULL, _IOLBF, 0) == 0);
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    for (unsigned wide = 0u; wide < 2u; wide++)
        for (unsigned kind = 0u; kind < 3u; kind++)
            transfer_control(wide ? TERM_UNIVERSE_STORE_FORMAT_WIDE64_V1
                                  : TERM_UNIVERSE_STORE_FORMAT_COMPACT32_V1,
                             kind);
    g_symbols = NULL;
    symbol_table_free(&symbols);
    return 0;
}

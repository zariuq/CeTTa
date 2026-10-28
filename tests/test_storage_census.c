#include "atom.h"
#include "match.h"
#include "prime_need.h"
#include "symbol.h"
#include "space.h"
#include "parser.h"
#include "answer_storage.h"
#include "delay_service.h"
#include "term_universe.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(condition) do { assert(condition); checks++; } while (0)

static size_t reported_allocation(const CettaStorageCensus *census, const void *owner) {
    for (size_t i = 0u; i < census->capacity; i++)
        if (census->slots[i].identity == owner &&
            census->slots[i].domain == CETTA_STORAGE_BLOCK)
            return census->slots[i].retained;
    return 0u;
}

static size_t universe_metadata(const TermUniverse *universe) {
    CettaStorageCensus census = {0};
    term_universe_storage_census(universe, &census);
    cetta_storage_finish(&census);
    CHECK(!census.incomplete && !census.overflow && !census.invalidated);
    if (universe->decoded_pages) {
        CHECK(reported_allocation(&census, universe->decoded_pages) ==
              universe->decoded_page_cap * sizeof(*universe->decoded_pages));
        for (size_t i = 0u; i < universe->decoded_page_cap; i++)
            if (universe->decoded_pages[i])
                CHECK(reported_allocation(&census, universe->decoded_pages[i]) >= sizeof(Atom *));
    }
    size_t bytes = census.retained;
    cetta_storage_census_free(&census);
    return bytes;
}

static void no_roots(const TermUniverse *universe, TermUniverseRootVisit visit,
                     void *visit_context, void *provider_context) {
    (void)universe; (void)visit; (void)visit_context; (void)provider_context;
}

static void decoded_universe_storage(void) {
    Arena arena;
    arena_init_detached(&arena);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &arena);
    AtomId id = tu_intern_int(&universe, 7);
    CHECK(id != CETTA_ATOM_ID_NONE && !universe.decoded_page_cap);
    size_t cold = universe_metadata(&universe);
    Atom *value = term_universe_get_atom(&universe, id);
    CHECK(value && value->kind == ATOM_GROUNDED && value->ground.gkind == GV_INT);
    CHECK(value->ground.ival == 7 && universe.decoded_page_cap);
    size_t decoded = universe_metadata(&universe);
    /* Both the sparse directory and its allocated atom page are retained;
     * decoding into the separate arena must not hide either allocation. */
    CHECK(decoded > cold + universe.decoded_page_cap * sizeof(*universe.decoded_pages));
    CHECK(term_universe_get_atom(&universe, id) == value);
    CHECK(universe_metadata(&universe) == decoded);
    CHECK(term_universe_add_root_provider(&universe, no_roots, NULL));
    size_t with_roots = universe_metadata(&universe);
    CHECK(with_roots == decoded + universe.root_provider_cap * sizeof(*universe.root_providers));
    term_universe_remove_root_provider(&universe, no_roots, NULL);
    CHECK(universe_metadata(&universe) == with_roots);
    term_universe_free(&universe);
    arena_free(&arena);
}

static void identities(void) {
    unsigned char objects[1024] = {0};
    CettaStorageCensus c = {0};
    CHECK(cetta_storage_object(&c, objects, 10u, CETTA_STORAGE_BLOCK));
    CHECK(!cetta_storage_object(&c, objects, 10u, CETTA_STORAGE_BLOCK));
    CHECK(c.bytes == 10u && c.retained == 10u && c.repeated == 1u);
    CHECK(cetta_storage_object_dual(&c, objects, 2u, 100u, CETTA_STORAGE_ARENA));
    CHECK(cetta_storage_object(&c, objects + 1u, 10u, CETTA_STORAGE_BLOCK));
    CHECK(c.bytes == 22u && c.retained == 120u);
    CHECK(!cetta_storage_object(&c, objects, 11u, CETTA_STORAGE_BLOCK));
    CHECK(c.invalidated && !strcmp(c.incomplete, "OwnerChangedDuringInspection"));
    cetta_storage_census_free(&c);
    CHECK(cetta_storage_object(&c, objects, SIZE_MAX, CETTA_STORAGE_BLOCK));
    CHECK(cetta_storage_object(&c, objects + 1u, 1u, CETTA_STORAGE_BLOCK));
    CHECK(c.overflow && !c.invalidated && c.bytes == SIZE_MAX && c.retained == SIZE_MAX);
    cetta_storage_census_free(&c);
    CHECK(!cetta_storage_array(&c, objects, SIZE_MAX, 8u, 0u));
    CHECK(c.overflow && c.incomplete && !c.bytes);
    cetta_storage_census_free(&c);
    for (size_t i = 0u; i < sizeof(objects); i++)
        assert(cetta_storage_object(&c, objects+i, i+1u, CETTA_STORAGE_BLOCK));
    for (size_t i = 0u; i < sizeof(objects); i++)
        assert(!cetta_storage_object(&c, objects+i, i+1u, CETTA_STORAGE_BLOCK));
    CHECK(c.bytes == 1024u*1025u/2u && c.repeated == 1024u && !c.incomplete);
    cetta_storage_census_free(&c);
}

typedef struct Link { const struct Link *next; } Link;
static void visit_link(const void *raw, CettaStorageCensus *c) {
    const Link *link = raw;
    if (cetta_storage_object(c, link, sizeof(*link), CETTA_STORAGE_BLOCK))
        cetta_storage_schedule(c, link->next, visit_link);
}
static void cycles_and_depth(void) {
    const size_t count = 100000u;
    Link *links = calloc(count, sizeof(*links));
    assert(links);
    for (size_t i = 0u; i < count - 1u; i++) links[i].next = &links[i+1u];
    links[count-1u].next = links;
    CettaStorageCensus c = {0};
    cetta_storage_schedule(&c, links, visit_link);
    cetta_storage_finish(&c);
    CHECK(!c.incomplete && c.length == count && c.repeated == 1u);
    CHECK(c.bytes == count * sizeof(*links) && !c.job_length);
    cetta_storage_census_free(&c);
    free(links);
}

typedef struct { size_t refs; char bytes[91]; } SharedOwner;
static void retain(void *raw) { ((SharedOwner *)raw)->refs++; }
static void release(void *raw) { ((SharedOwner *)raw)->refs--; }
static void owner_storage(const void *raw, CettaStorageCensus *c) {
    cetta_storage_object(c, raw, sizeof(SharedOwner), CETTA_STORAGE_BLOCK);
}
static size_t blocks(const Arena *arena) {
    size_t bytes = 0;
    const ArenaBlock *heads[] = {arena->head, arena->spare};
    for (size_t i = 0u; i < 2u; i++)
        for (const ArenaBlock *b = heads[i]; b; b = b->next)
            bytes += sizeof(ArenaBlock) - ARENA_BLOCK_SIZE + b->capacity;
    return bytes;
}
static void arenas_and_owners(void) {
    Arena a, b;
    arena_init(&a); arena_init(&b);
    arena_set_block_capacity(&a, 256u);
    (void)arena_alloc(&a, 200u);
    ArenaMark mark = arena_mark(&a);
    (void)arena_alloc(&a, 700u);
    arena_reset(&a, mark);
    CHECK(a.spare != NULL && a.head != NULL);
    CettaStorageCensus c = {0};
    arena_storage_census(&a, &c); cetta_storage_finish(&c);
    CHECK(!c.incomplete && c.bytes == arena_accounted_live_bytes(&a));
    CHECK(c.retained == blocks(&a) + a.external_bytes + a.persistent_bytes);
    CHECK(c.retained > c.bytes);
    cetta_storage_census_free(&c);
    SharedOwner shared = {0};
    CHECK(arena_retain_owner_with_storage(&a, &shared, retain, release, owner_storage));
    CHECK(arena_retain_owner_with_storage(&b, &shared, retain, release, owner_storage));
    CHECK(shared.refs == 2u);
    ArenaMark before_link = arena_mark(&b);
    arena_set_older_generation(&b, &a);
    ArenaFrameIdentitySet *metadata = b.frame_identities;
    size_t persistent = b.persistent_bytes;
    CHECK(metadata && persistent);
    arena_set_older_generation(&b, &a);
    CHECK(b.frame_identities == metadata && b.persistent_bytes == persistent);
    (void)arena_alloc(&b, 64u);
    arena_reset(&b, before_link);
    CHECK(b.older_identity == a.identity && b.frame_identities == metadata);
    CHECK(b.persistent_bytes == persistent);
    arena_storage_census(&b, &c); cetta_storage_finish(&c);
    CHECK(!c.incomplete && c.repeated >= 1u);
    CHECK(c.bytes == arena_accounted_live_bytes(&a) + arena_accounted_live_bytes(&b) + sizeof(shared));
    CHECK(c.retained == blocks(&a) + blocks(&b) + a.external_bytes + b.external_bytes + a.persistent_bytes + b.persistent_bytes + sizeof(shared));
    size_t before = c.bytes;
    arena_storage_census(&a, &c); cetta_storage_finish(&c);
    CHECK(c.bytes == before && !c.incomplete);
    cetta_storage_census_free(&c);
    arena_set_older_generation(&b, NULL);
    CHECK(!b.older_identity && b.frame_identities == metadata);
    arena_storage_census(&b, &c); cetta_storage_finish(&c);
    CHECK(!c.incomplete && c.bytes == arena_accounted_live_bytes(&b) + sizeof(shared));
    CHECK(c.retained == blocks(&b) + b.external_bytes + b.persistent_bytes + sizeof(shared));
    cetta_storage_census_free(&c);
    arena_free(&b); arena_free(&a);
    CHECK(!shared.refs);
    arena_init(&a);
    CHECK(arena_retain_owner(&a, &shared, retain, release));
    arena_storage_census(&a, &c); cetta_storage_finish(&c);
    CHECK(c.incomplete && !strcmp(c.incomplete, "OpaqueRetainedOwner") && !c.invalidated);
    cetta_storage_census_free(&c); arena_free(&a);
}

static void bindings_and_need(void) {
    Arena arena;
    arena_init(&arena);
    BindingsBuilder builder;
    CHECK(bindings_builder_init(&builder, NULL));
    for (unsigned i = 0u; i < 100u; i++) {
        char name[32]; snprintf(name, sizeof(name), "storage%u", i);
        Atom *variable = atom_var(&arena, name);
        assert(bindings_builder_add_var_fresh(&builder, variable, atom_int(&arena, i)));
    }
    Bindings first, second;
    bindings_builder_take(&builder, &first);
    bindings_builder_free(&builder);
    CHECK(bindings_clone(&second, &first));
    CettaStorageCensus c = {0};
    bindings_storage_census(&first, &c); cetta_storage_finish(&c);
    CHECK(!c.incomplete && c.bytes > 0u);
    size_t initial = c.bytes;
    bindings_storage_census(&second, &c); cetta_storage_finish(&c);
    CHECK(!c.incomplete && c.bytes == initial && c.repeated > 0u);
    cetta_storage_census_free(&c);
    PrimeNeedSnapshot root, allocated;
    prime_need_snapshot_init(&root);
    CHECK(prime_need_snapshot_begin(&root));
    uint64_t cell = 0u;
    CHECK(prime_need_snapshot_allocate(&arena, &root, atom_symbol(&arena, "delayed"), &allocated, &cell));
    *bindings_need_mut(&first) = allocated;
    *bindings_need_mut(&second) = allocated;
    bindings_storage_census(&first, &c); bindings_storage_census(&second, &c);
    cetta_storage_finish(&c);
    /* Two occurrence records share one immutable Need owner and COW buffers. */
    CHECK(!c.incomplete && c.bytes == initial + 2u*sizeof(PrimeOccurrence) + arena_accounted_live_bytes(&arena));
    CHECK(c.retained >= blocks(&arena) && c.repeated > 0u);
    cetta_storage_census_free(&c);
    bindings_free(&second); bindings_free(&first); arena_free(&arena);
}

static void numeric_storage(void) {
    Arena arena;
    arena_init(&arena);
#if CETTA_BUILD_WITH_GMP
    mpz_t z;
    mpz_init2(z, 8192u);
    mpz_set_ui(z, 123u);
    mpz_setbit(z, 100u);
    char expected[64];
    mpz_get_str(expected, 10, z);
    Atom *big = atom_bigint_take_mpz(&arena, z);
    mpz_clear(z);
    CettaStorageCensus c = {0};
    arena_storage_census(&arena, &c); cetta_storage_finish(&c);
    CHECK(!c.incomplete && c.retained >= blocks(&arena) + 1024u);
    size_t before = c.retained;
    cetta_storage_census_free(&c);
    CHECK(!strcmp(atom_bigint_cstr(big), expected));
    arena_storage_census(&arena, &c); cetta_storage_finish(&c);
    CHECK(!c.incomplete && c.retained == before + strlen(expected) + 1u);
    cetta_storage_census_free(&c);
    Atom *rational = atom_rational(&arena, "123456789123456789/987654321");
    CHECK(rational != NULL);
    arena_storage_census(&arena, &c); cetta_storage_finish(&c);
    CHECK(!c.incomplete && c.retained > before + strlen(expected) + 1u);
    cetta_storage_census_free(&c);
#else
    Atom *big = atom_bigint(&arena, "123456789123456789123456789");
    CHECK(big != NULL);
    CettaStorageCensus c = {0};
    arena_storage_census(&arena, &c); cetta_storage_finish(&c);
    CHECK(!c.incomplete && c.retained >= blocks(&arena) + strlen(atom_bigint_cstr(big)) + 1u);
    cetta_storage_census_free(&c);
#endif
    arena_free(&arena);
}

static void private_world_storage(void) {
    HashConsTable hash;
    hashcons_init_compact(&hash);
    Arena arena;
    arena_init_detached(&arena);
    CettaStorageCensus c = {0};
    hashcons_storage_census(&hash, &c); cetta_storage_finish(&c);
    CHECK(!c.incomplete && c.bytes == hash.size * sizeof(Atom *));
    size_t empty = c.retained;
    cetta_storage_census_free(&c);
    Atom value; atom_scalar_leaf_int(&value, 9);
    CHECK(hashcons_get(&hash, &value) != &value);
    hashcons_storage_census(&hash, &c); cetta_storage_finish(&c);
    CHECK(!c.incomplete && c.bytes >= empty + sizeof(Atom));
    size_t before = c.retained;
    hashcons_storage_census(&hash, &c); cetta_storage_finish(&c);
    CHECK(c.retained == before && c.repeated > 0u);
    cetta_storage_census_free(&c);

    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &arena);
    Space space; space_init_with_universe(&space, &universe);
    for (unsigned i = 0u; i < 400u; i++) {
        char source[100]; snprintf(source, sizeof(source), "(= (member %u $x) (Pair %u $x))", i, i);
        Atom **parsed = NULL;
        CHECK(parse_metta_text(source, &arena, &parsed) == 1);
        space_add(&space, parsed[0]); free(parsed);
    }
    Atom **parsed = NULL;
    CHECK(parse_metta_text("(member 10 A)", &arena, &parsed) == 1);
    space_match_native_ensure_trie(&space);
    free(parsed);
    for (size_t i = 0u; i < universe.len; i++) {
        const CettaTermVariableSupport *support;
        CHECK(term_universe_variable_support(&universe, i, &support));
    }
    term_universe_storage_census(&universe, &c); space_storage_census(&space, &c);
    cetta_storage_finish(&c);
    CHECK(!c.incomplete && c.retained >= universe.blob_cap + space.cap * sizeof(uint32_t));
    before = c.retained;
    term_universe_storage_census(&universe, &c); space_storage_census(&space, &c); cetta_storage_finish(&c);
    CHECK(c.retained == before && c.repeated >= 2u);
    cetta_storage_census_free(&c);
    SpaceEngine old = space.match_backend.kind;
    space.match_backend.kind = SPACE_ENGINE_MORK;
    space_storage_census(&space, &c); cetta_storage_finish(&c);
    CHECK(c.incomplete && !strcmp(c.incomplete, "ForeignSpaceBackendOwner"));
    cetta_storage_census_free(&c); space.match_backend.kind = old;
    space_free(&space); term_universe_free(&universe); arena_free(&arena); hashcons_free(&hash);
}

static void registry_state_storage(void) {
    Arena input;
    arena_init(&input);
    Registry registry;
    registry_init(&registry);
    SymbolId first = symbol_intern_cstr(g_symbols, "storage-state-first");
    SymbolId second = symbol_intern_cstr(g_symbols, "storage-state-second");
    const size_t length = 4096u;
    Atom **items = malloc(length * sizeof(*items));
    assert(items);
    for (size_t i = 0u; i < length; i++) items[i] = atom_int(&input, i);
    Atom *large = atom_expr(&input, items, (CettaExprLen)length);
    CHECK(registry_state_set(&registry, first, large));
    free(items);
    CettaStorageCensus c = {0};
    registry_storage_census(&registry, &c); cetta_storage_finish(&c);
    CHECK(!c.incomplete && !c.invalidated && !c.overflow);
    size_t large_bytes = c.retained;
    size_t initial_state_arenas = 0u;
    for (size_t i = 0u; i < c.capacity; i++)
        if (c.slots[i].identity && c.slots[i].domain == CETTA_STORAGE_ARENA)
            initial_state_arenas++;
    registry_storage_census(&registry, &c); cetta_storage_finish(&c);
    CHECK(c.retained == large_bytes && c.repeated > 0u);
    cetta_storage_census_free(&c);
    arena_free(&input);
    Atom *kept = registry_state_lookup(&registry, first);
    CHECK(kept && kept->expr.len == length &&
        kept->expr.elems[length - 1u]->ground.ival == (int64_t)length - 1);

    arena_init(&input);
    size_t settled_bytes = 0u;
    for (unsigned i = 0u; i < 64u; i++) {
        CHECK(registry_state_set(&registry, first, atom_int(&input, i)));
        CHECK(registry_state_lookup(&registry, first)->ground.ival == i);
        registry_storage_census(&registry, &c); cetta_storage_finish(&c);
        CHECK(!c.incomplete && !c.invalidated && !c.overflow);
        size_t state_arenas = 0u;
        for (size_t j = 0u; j < c.capacity; j++)
            if (c.slots[j].identity && c.slots[j].domain == CETTA_STORAGE_ARENA)
                state_arenas++;
        CHECK(state_arenas == initial_state_arenas + 1u);
        CHECK(c.retained < large_bytes);
        if (i == 2u) settled_bytes = c.retained;
        if (i > 2u) CHECK(c.retained == settled_bytes);
        cetta_storage_census_free(&c);
    }
    CHECK(registry_state_set(&registry, second,
        registry_state_lookup(&registry, first)));
    CHECK(registry_state_count(&registry) == 2u);
    CHECK(registry_state_set(&registry, first, atom_int(&input, 100)));
    CHECK(registry_state_lookup(&registry, second)->ground.ival == 63);
    registry_storage_census(&registry, &c); cetta_storage_finish(&c);
    CHECK(!c.incomplete && c.retained > settled_bytes);
    size_t before_refusal = c.retained;
    cetta_storage_census_free(&c);
    CHECK(!registry_state_set_symbol(&registry, atom_int(&input, 0),
        atom_int(&input, 200)));
    registry_storage_census(&registry, &c); cetta_storage_finish(&c);
    CHECK(!c.incomplete && c.retained == before_refusal);
    CHECK(registry_state_lookup(&registry, first)->ground.ival == 100 &&
        registry_state_lookup(&registry, second)->ground.ival == 63);
    cetta_storage_census_free(&c);
    registry_free(&registry);
    registry_storage_census(&registry, &c); cetta_storage_finish(&c);
    CHECK(!c.incomplete && c.retained == 0u);
    cetta_storage_census_free(&c);
    arena_free(&input);
}

static bool has_block(const CettaStorageCensus *census, const void *identity) {
    for(size_t i=0u;i<census->capacity;i++)
        if(census->slots[i].identity==identity && census->slots[i].domain==CETTA_STORAGE_BLOCK)
            return true;
    return false;
}

static void answer_collection_storage(void) {
    CettaAnswerStorageAccount *account=NULL;
    CettaAnswerStorage *first=cetta_answer_storage_new(&account);
    CettaAnswerStorage *second=cetta_answer_storage_new(&account);
    CHECK(first && second && account);
    Atom *value=atom_expr2(&first->arena,atom_symbol(&first->arena,"RetainedAnswer"),atom_int(&first->arena,7));
    (void)atom_int(&second->arena,11);
    Arena receiver;arena_init_detached(&receiver);
    CHECK(cetta_answer_storage_retain_into(first,&receiver));
    cetta_answer_storage_release(first);
    CettaStorageCensus c={0};
    arena_storage_census(&receiver,&c);cetta_storage_finish(&c);
    CHECK(!c.incomplete && !c.invalidated && has_block(&c,first) && has_block(&c,account));
    CHECK(!has_block(&c,second) && first->shares==1u && account->shares==3u);
    cetta_answer_storage_account_census(account,&c);cetta_storage_finish(&c);
    CHECK(!c.incomplete && has_block(&c,second));
    CettaAnswerStorageUsage usage;CHECK(cetta_answer_storage_account_usage(account,&usage) && usage.collections==2u);
    size_t retained=c.retained,objects=c.length;
    cetta_answer_storage_account_census(account,&c);arena_storage_census(&receiver,&c);cetta_storage_finish(&c);
    CHECK(!c.incomplete && c.retained==retained && c.length==objects && c.repeated>0u);
    CHECK(atom_is_symbol(value->expr.elems[0],"RetainedAnswer") && value->expr.elems[1]->ground.ival==7);
    cetta_storage_census_free(&c);
    arena_free(&receiver);
    CHECK(cetta_answer_storage_account_usage(account,&usage) && usage.collections==1u);
    cetta_answer_storage_account_census(account,&c);cetta_storage_finish(&c);
    CHECK(!c.incomplete && has_block(&c,second) && c.retained<retained);
    cetta_storage_census_free(&c);
    cetta_answer_storage_release(second);cetta_answer_storage_account_release(account);account=NULL;
    cetta_answer_storage_account_census(account,&c);cetta_storage_finish(&c);
    CHECK(!c.incomplete && !c.bytes && !c.retained);
    cetta_storage_census_free(&c);
}

static void delayed_goal_storage(void) {
    CettaDelayService *service=cetta_delay_service_new();CHECK(service);
    Arena input;arena_init_detached(&input);
    VarId variable=fresh_var_id();
    Atom *goal=atom_symbol(&input,"DelayedGoal");
    CHECK(cetta_delay_suspend(service,1u,CETTA_DELAY_CLIENT_PROLOG,goal,&variable,1u,false));
    arena_free(&input);
    CHECK(cetta_delay_watches(service,variable) && !cetta_delay_has_woken(service));
    CettaStorageCensus c={0};cetta_delay_service_storage_census(service,&c);cetta_storage_finish(&c);
    CHECK(!c.incomplete && !c.invalidated && !c.overflow && has_block(&c,service) && c.retained>0u);
    size_t retained=c.retained,objects=c.length;
    cetta_delay_service_storage_census(service,&c);cetta_storage_finish(&c);
    CHECK(!c.incomplete && c.retained==retained && c.length==objects);
    CHECK(cetta_delay_watches(service,variable) && !cetta_delay_has_woken(service));
    cetta_storage_census_free(&c);
    CHECK(cetta_delay_bound(service,2u,variable) && cetta_delay_has_woken(service));
    cetta_delay_service_storage_census(service,&c);cetta_storage_finish(&c);
    CHECK(!c.incomplete && cetta_delay_has_woken(service));
    CettaDelayGoal taken;CHECK(cetta_delay_take_woken(service,3u,&taken));
    CHECK(atom_is_symbol(taken.goal,"DelayedGoal") && !cetta_delay_has_woken(service));
    cetta_storage_census_free(&c);cetta_delay_service_free(service);
}

int main(void) {
    CETTA_FRAME_IDENTITY_SCOPE(frame_identity_scope);
    SymbolTable symbols;
    VarInternTable variables;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&variables);
    g_symbols = &symbols; g_var_intern = &variables; g_hashcons = NULL;
    identities(); cycles_and_depth(); arenas_and_owners(); bindings_and_need(); numeric_storage(); private_world_storage();
    registry_state_storage(); decoded_universe_storage();
    answer_collection_storage();delayed_goal_storage();
    var_intern_free(&variables); symbol_table_free(&symbols);
    g_symbols = NULL; g_var_intern = NULL;
    printf("PASS: owned storage census: %u identity, capacity, sharing, cycle and refusal checks\n", checks);
    return 0;
}

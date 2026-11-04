#define _GNU_SOURCE
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static bool watching, persistent, injected, only_realloc;
static size_t allocation, failure = SIZE_MAX, tracked, held;
static void *owned[8192];
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
void __real_free(void *);
static bool refuse(bool reallocation) {
    if (!watching || (only_realloc && !reallocation)) return false;
    size_t at = allocation++;
    if (persistent ? at >= failure : at == failure) {
        injected = true; errno = ENOMEM; return true;
    }
    return false;
}
static void *retain(void *pointer) {
    if (pointer && watching) {
        assert(tracked < sizeof(owned) / sizeof(*owned));
        owned[tracked++] = pointer; held++;
    }
    return pointer;
}
void *__wrap_malloc(size_t size) { return refuse(false) ? NULL : retain(__real_malloc(size)); }
void *__wrap_calloc(size_t count, size_t size) {
    return refuse(false) ? NULL : retain(__real_calloc(count, size));
}
void *__wrap_realloc(void *pointer, size_t size) {
    if (refuse(true)) return NULL;
    size_t slot = tracked;
    for (size_t i = 0; pointer && i < tracked; ++i) if (owned[i] == pointer) { slot = i; break; }
    void *next = __real_realloc(pointer, size);
    if (slot < tracked) {
        if (next || !size) { owned[slot] = next; if (!next) { assert(held); held--; } }
        return next;
    }
    return retain(next);
}
void __wrap_free(void *pointer) {
    for (size_t i = 0; pointer && i < tracked; ++i) if (owned[i] == pointer) {
        owned[i] = NULL; assert(held); held--;
    }
    __real_free(pointer);
}
#include "../../src/parser.c"
#include "../../src/he_type_policy.c"
#include "runtime_names.h"

typedef struct {
    CettaNameDomain names;
    CettaNameContext previous;
    CettaHeTypeTables *tables, *prior;
} Fixture;
static void fixture_init(Fixture *fixture) {
    cetta_name_domain_init(&fixture->names);
    fixture->previous = cetta_name_context_current();
    CettaNameContext names = cetta_name_domain_context(&fixture->names);
    cetta_name_context_install(&names);
    fixture->tables = he_type_tables_new(); assert(fixture->tables);
    fixture->prior = he_type_tables_exchange(fixture->tables);
}
static void fixture_free(Fixture *fixture) {
    he_type_tables_exchange(fixture->prior);
    he_type_tables_free(fixture->tables);
    cetta_name_context_install(&fixture->previous);
    cetta_name_domain_free(&fixture->names);
}
static void begin(size_t at, bool suffix) {
    assert(!held);
    allocation = tracked = 0u; failure = at; persistent = suffix; injected = only_realloc = false; watching = true;
}
static void verify_catalog(void) {
    assert(he_library_declaration_count() == HE_LIBRARY_DECLARATION_COUNT);
    Atom *arrow = he_library_declaration_at(0u)->expr.elems[2];
    assert(arrow->kind == ATOM_EXPR && arrow->expr.len == 4u);
    assert(var_id_eq(arrow->expr.elems[1]->var_id, arrow->expr.elems[2]->var_id));
    for (size_t i = 0; i < sizeof(HE_GROUNDED_TYPES) / sizeof(HE_GROUNDED_TYPES[0]); ++i) {
        SymbolId symbol = symbol_lookup_cstr(g_symbols, HE_GROUNDED_TYPES[i].name);
        assert(he_grounded_symbol_types(symbol, false, NULL) > 0u);
    }
    Atom *const *types = NULL;
    assert(he_grounded_symbol_types(symbol_lookup_cstr(g_symbols, "=="), false, &types) == 1u);
    assert(var_id_eq(types[0]->expr.elems[1]->var_id, types[0]->expr.elems[2]->var_id));
    assert(!var_id_eq(arrow->expr.elems[1]->var_id, types[0]->expr.elems[1]->var_id));
}
static void scope_retry(void) {
    Fixture fixture; fixture_init(&fixture);
    SymbolId x = symbol_intern_cstr(g_symbols, "scope-x");
    SymbolId y = symbol_intern_cstr(g_symbols, "scope-y");
    ParserVarScope scope; parser_var_scope_init(&scope);
    begin(1u, false);
    only_realloc = true;
    assert(var_id_is_none(parser_var_scope_id(&scope, x)) && scope.cap == 0u && scope.len == 0u);
    watching = false;
    VarId first = parser_var_scope_id(&scope, x);
    assert(!var_id_is_none(first) && var_id_eq(first, parser_var_scope_id(&scope, x)));
    assert(!var_id_eq(first, parser_var_scope_id(&scope, y)));
    parser_var_scope_free(&scope); assert(!held);
    Arena arena; assert(arena_try_init_detached(&arena));
    Atom *key = atom_symbol_id(&arena, x), *other = atom_symbol_id(&arena, y);
    parser_var_scope_init(&scope);
    assert(parser_var_scope_name_key_id(&scope, key) != NAME_ID_NONE);
    begin(1u, false);
    only_realloc = true;
    assert(var_id_is_none(parser_var_scope_name_id(&scope, key)) && scope.name_cap == 0u && scope.name_len == 0u);
    watching = false;
    first = parser_var_scope_name_id(&scope, key);
    assert(!var_id_is_none(first) && var_id_eq(first, parser_var_scope_name_id(&scope, key)));
    assert(!var_id_eq(first, parser_var_scope_name_id(&scope, other)));
    parser_var_scope_free(&scope); assert(!held);
    arena_free(&arena); fixture_free(&fixture);
}
static void catalog_bounds(void) {
    Fixture fixture; fixture_init(&fixture);
    assert(arena_try_init_detached(&fixture.tables->arena)); fixture.tables->initialized = true;
    const char *texts[HE_LIBRARY_DECLARATION_COUNT + sizeof(HE_GROUNDED_TYPES) / sizeof(HE_GROUNDED_TYPES[0]) + sizeof(HE_GROUNDED_EXTENSION_TYPES) / sizeof(HE_GROUNDED_EXTENSION_TYPES[0])];
    size_t count = 0u;
    for (size_t i = 0; i < HE_LIBRARY_DECLARATION_COUNT; ++i) texts[count++] = HE_LIBRARY_DECLARATIONS[i];
    for (size_t i = 0; i < sizeof(HE_GROUNDED_TYPES) / sizeof(HE_GROUNDED_TYPES[0]); ++i) texts[count++] = HE_GROUNDED_TYPES[i].type;
    for (size_t i = 0; i < sizeof(HE_GROUNDED_EXTENSION_TYPES) / sizeof(HE_GROUNDED_EXTENSION_TYPES[0]); ++i) texts[count++] = HE_GROUNDED_EXTENSION_TYPES[i].type;
    for (size_t i = 0; i < count; ++i) {
        size_t bound = strlen(texts[i]) * (sizeof(Atom) + sizeof(Atom *) + 16u);
        assert(arena_try_reserve(&fixture.tables->arena, bound));
        ArenaBlock *head = fixture.tables->arena.head;
        size_t live = fixture.tables->arena.live_bytes;
        assert(he_parse_catalog_atom(texts[i]));
        assert(fixture.tables->arena.head == head && fixture.tables->arena.live_bytes - live <= bound);
    }
    fixture_free(&fixture);
}
int main(void) {
    /* The process/thread literal cache has a different lifetime from a catalog.
     * Cold engine construction is exercised by the separate startup consumer. */
    Fixture warm; fixture_init(&warm);
    assert(he_library_tables_init()); fixture_free(&warm);
    Fixture baseline; fixture_init(&baseline);
    begin(SIZE_MAX, false);
    assert(he_library_tables_init());
    size_t sites = allocation; assert(sites > 0u && !injected);
    verify_catalog(); assert(allocation == sites);
    watching = false; fixture_free(&baseline); assert(!held);
    unsigned refusals = 0;
    for (unsigned suffix = 0; suffix < 2u; ++suffix) for (size_t at = 0; at < sites; ++at) {
        Fixture fixture; fixture_init(&fixture); begin(at, suffix != 0);
        assert(!he_library_tables_init() && injected && errno == ENOMEM);
        assert(!fixture.tables->initialized && !fixture.tables->loaded && !fixture.tables->arena.head);
        watching = false;
        assert(he_library_tables_init()); verify_catalog();
        fixture_free(&fixture); assert(!held && "catalog failure retained owned storage");
        refusals++;
    }
    tracked = 0u;
    scope_retry(); catalog_bounds();
    printf("HE catalog: %u allocation refusals, partial cleanup, retry, scoped identities and arena bounds pass\n", refusals);
    return 0;
}

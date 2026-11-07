#include "atom.h"
#include "library.h"
#include "native_handle.h"
#include "shared_transition.h"
#include "space.h"
#include "symbol.h"
#include "native/source_occurrence_view_v1.h"
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Observe entry to the real transition guard, without replacing its lock.
 * The main thread changes the Space while the contracting thread waits for
 * that guard. Validation must happen after the wait, not before it. */
static pthread_mutex_t reached_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t reached_condition = PTHREAD_COND_INITIALIZER;
static bool reached;
static _Thread_local bool observe_guard;
void __real_cetta_shared_transition_guard_enter(CettaSharedTransitionGuard *guard);
void __wrap_cetta_shared_transition_guard_enter(CettaSharedTransitionGuard *guard) {
    if (observe_guard) {
        observe_guard = false;
        assert(pthread_mutex_lock(&reached_mutex) == 0);
        reached = true;
        assert(pthread_cond_signal(&reached_condition) == 0);
        assert(pthread_mutex_unlock(&reached_mutex) == 0);
    }
    __real_cetta_shared_transition_guard_enter(guard);
}

typedef struct {
    Arena arena;
    Arena persistent;
    TermUniverse universe;
    Space space;
    CettaLibraryContext *context;
} Fixture;

static bool form(Atom *value, const char *name) {
    return value && value->kind == ATOM_EXPR && value->expr.len &&
        atom_is_symbol(value->expr.elems[0], name);
}
static Atom *row(Arena *arena, const char *name, int64_t number) {
    Atom *parts[] = {atom_symbol(arena, name), atom_int(arena, number)};
    return atom_expr(arena, parts, 2u);
}
static void init(Fixture *test, bool stack) {
    arena_init_detached(&test->arena);
    arena_init_detached(&test->persistent);
    term_universe_init(&test->universe);
    term_universe_set_persistent_arena(&test->universe, &test->persistent);
    space_init_with_universe(&test->space, &test->universe);
    if (stack) test->space.kind = SPACE_KIND_STACK;
    test->context = calloc(1, sizeof(*test->context));
    assert(test->context);
    test->context->native_handle_next_id = 1u;
    space_add(&test->space, row(&test->arena, "p", 1));
    space_add(&test->space, row(&test->arena, "unrelated", 7));
    space_add(&test->space, row(&test->arena, "p", 1));
    assert(space_length64(&test->space) == 3u);
}
static void finish(Fixture *test) {
    cetta_native_handle_cleanup_all(test->context);
    arena_free(&test->arena);
    free(test->context);
    space_free(&test->space);
    term_universe_free(&test->universe);
    arena_free(&test->persistent);
}
static Atom *invoke(Fixture *test, const char *name, Atom **arguments, uint32_t count) {
    Atom *result = cetta_source_occurrence_view_dispatch_v1(test->context, &test->arena,
        atom_symbol(&test->arena, name), arguments, count);
    assert(result);
    return result;
}
static Atom *capture(Fixture *test) {
    Atom *arguments[] = {atom_space(&test->arena, &test->space)};
    Atom *captured = invoke(test, "__cetta_lib_source_occurrence_capture_v1", arguments, 1u);
    assert(form(captured, "SourceOccurrenceViewV1") && captured->expr.len == 2u);
    return captured->expr.elems[1];
}
static void select_rows(Fixture *test, Atom *view) {
    Atom *names[] = {atom_string(&test->arena, "p")};
    Atom *arguments[] = {view, atom_expr(&test->arena, names, 1u)};
    Atom *selected = invoke(test, "__cetta_lib_source_occurrence_select_v1", arguments, 2u);
    assert(form(selected, "SourceOccurrencesV1"));
    Atom *json = selected->expr.elems[1];
    assert(json->kind == ATOM_GROUNDED && json->ground.gkind == GV_STRING);
    if (!strstr(json->ground.sval, "\"integer\":\"1\""))
        fprintf(stderr, "selected records: %s; rows=%llu\n", json->ground.sval,
            (unsigned long long)space_length64(&test->space));
    assert(strstr(json->ground.sval, "\"integer\":\"1\""));
    assert(!strstr(json->ground.sval, "\"integer\":\"9\""));
}
static Atom *contract(Fixture *test, Atom *view, const int64_t *indices, uint32_t count) {
    Atom **ordinals = count ? malloc(count * sizeof(*ordinals)) : NULL;
    assert(!count || ordinals);
    for (uint32_t i = 0u; i < count; i++) ordinals[i] = atom_int(&test->arena, indices[i]);
    Atom *arguments[] = {view, atom_space(&test->arena, &test->space),
        atom_expr(&test->arena, ordinals, count)};
    free(ordinals);
    return invoke(test, "__cetta_lib_source_occurrence_contract_v1", arguments, 3u);
}
static void assert_refused(Atom *result, const char *reason) {
    if (!form(result, "SourceOccurrenceRefusedV1"))
        fprintf(stderr, "expected refusal %s, got %s\n", reason,
            result && result->kind == ATOM_EXPR && result->expr.len
              ? atom_name_cstr(result->expr.elems[0]) : "non-expression");
    assert(form(result, "SourceOccurrenceRefusedV1"));
    assert(atom_is_symbol(result->expr.elems[1], reason));
}
static void basic(bool stack) {
    Fixture test; init(&test, stack);
    Atom *view = capture(&test); select_rows(&test, view);
    int64_t duplicate[] = {0, 0}, outside[] = {2}, negative[] = {-1}, second[] = {1};
    Space other; space_init_with_universe(&other, &test.universe);
    Atom *ordinal[] = {atom_int(&test.arena, 0)};
    Atom *arguments[] = {view, atom_space(&test.arena, &other), atom_expr(&test.arena, ordinal, 1u)};
    Atom *distinct = invoke(&test, "__cetta_lib_source_occurrence_contract_v1", arguments, 3u);
    assert(atom_is_symbol(distinct, "SourceOccurrenceDistinctV1"));
    assert(space_length64(&test.space) == 3u && space_length64(&other) == 0u);
    space_free(&other);
    assert(form(contract(&test, view, duplicate, 2u), "Error"));
    assert(form(contract(&test, view, outside, 1u), "Error"));
    assert(form(contract(&test, view, negative, 1u), "Error"));
    assert(space_length64(&test.space) == 3u);
    assert(form(contract(&test, view, second, 1u), "SourceOccurrencesContractedV1"));
    assert(space_length64(&test.space) == 2u);
    assert(atom_eq(space_get_at64(&test.space, 0u), row(&test.arena, "p", 1)));
    assert(atom_eq(space_get_at64(&test.space, 1u), row(&test.arena, "unrelated", 7)));
    assert_refused(contract(&test, view, second, 1u), "AlreadyContracted");
    assert(space_length64(&test.space) == 2u);
    finish(&test);
}
static void snapshot(bool stack) {
    Fixture test; init(&test, stack);
    Atom *view = capture(&test);
    space_add(&test.space, row(&test.arena, "p", 9));
    select_rows(&test, view);
    int64_t first[] = {0};
    assert_refused(contract(&test, view, first, 1u), "StaleCapture");
    assert(space_length64(&test.space) == 4u);
    assert(form(contract(&test, view, NULL, 0u), "SourceOccurrencesContractedV1"));
    assert(space_length64(&test.space) == 4u);
    finish(&test);
}

typedef struct {
    Fixture *test;
    SymbolTable *symbols;
    BuiltinSyms builtins;
    Atom *head;
    Atom *arguments[3];
    Arena result_arena;
    Atom *result;
} Race;
static void *contract_worker(void *raw) {
    Race *race = raw;
    g_symbols = race->symbols; g_builtin_syms = race->builtins;
    g_hashcons = NULL; g_var_intern = NULL;
    cetta_shared_transition_scope_enter();
    observe_guard = true;
    race->result = cetta_source_occurrence_view_dispatch_v1(race->test->context,
        &race->result_arena, race->head, race->arguments, 3u);
    cetta_shared_transition_scope_leave();
    return NULL;
}
static void changed_while_waiting(SymbolTable *symbols, bool stack) {
    Fixture test; init(&test, stack);
    Atom *view = capture(&test); select_rows(&test, view);
    Atom *ordinal[] = {atom_int(&test.arena, 0)};
    Race race = {.test = &test, .symbols = symbols, .builtins = g_builtin_syms,
        .head = atom_symbol(&test.arena, "__cetta_lib_source_occurrence_contract_v1"),
        .arguments = {view, atom_space(&test.arena, &test.space), atom_expr(&test.arena, ordinal, 1u)}};
    arena_init_detached(&race.result_arena);
    reached = false;
    cetta_shared_transition_scope_enter();
    CettaSharedTransitionGuard held = {0};
    cetta_shared_transition_guard_enter(&held);
    pthread_t worker;
    assert(pthread_mutex_lock(&reached_mutex) == 0);
    assert(pthread_create(&worker, NULL, contract_worker, &race) == 0);
    while (!reached) assert(pthread_cond_wait(&reached_condition, &reached_mutex) == 0);
    assert(pthread_mutex_unlock(&reached_mutex) == 0);
    uint8_t mask[] = {1u, 0u, 0u};
    CettaCount removed = 0u;
    assert(space_remove_occurrence_mask_stable(&test.space, mask, 3u, &removed) && removed == 1u);
    space_add(&test.space, row(&test.arena, "p", 9));
    cetta_shared_transition_guard_leave(&held);
    cetta_shared_transition_scope_leave();
    assert(pthread_join(worker, NULL) == 0);
    assert_refused(race.result, "StaleCapture");
    assert(space_length64(&test.space) == 3u);
    assert(atom_eq(space_get_at64(&test.space, 0u), row(&test.arena, "unrelated", 7)));
    assert(atom_eq(space_get_at64(&test.space, 1u), row(&test.arena, "p", 1)));
    assert(atom_eq(space_get_at64(&test.space, 2u), row(&test.arena, "p", 9)));
    arena_free(&race.result_arena); finish(&test);
}
static void counted_head(bool stack) {
    Fixture test; init(&test, stack);
    Atom *counted = atom_string_n(&test.arena, "a\0b", 3u);
    Atom *parts[] = {counted, atom_int(&test.arena, 9)};
    space_add(&test.space, atom_expr(&test.arena, parts, 2u));
    Atom *symbol_parts[] = {atom_symbol_id(&test.arena,
        symbol_intern_bytes(g_symbols, (const uint8_t *)"a\0b", 3u)), atom_int(&test.arena, 10)};
    space_add(&test.space, atom_expr(&test.arena, symbol_parts, 2u));
    Atom *prefix[] = {atom_string(&test.arena, "a"), atom_int(&test.arena, 8)};
    space_add(&test.space, atom_expr(&test.arena, prefix, 2u));
    Atom *view = capture(&test);
    Atom *names[] = {counted};
    Atom *arguments[] = {view, atom_expr(&test.arena, names, 1u)};
    Atom *selected = invoke(&test, "__cetta_lib_source_occurrence_select_v1", arguments, 2u);
    assert(form(selected, "SourceOccurrencesV1"));
    const char *json = selected->expr.elems[1]->ground.sval;
    assert(strstr(json, "a\\u0000b") && strstr(json, "\"integer\":\"9\""));
    assert(strstr(json, "\"symbol\":\"a\\u0000b\""));
    assert(strstr(json, "\"integer\":\"10\""));
    assert(!strstr(json, "\"integer\":\"8\""));
    int64_t first[] = {0};
    assert(form(contract(&test, view, first, 1u), "SourceOccurrencesContractedV1"));
    assert(space_length64(&test.space) == 5u);
    assert(atom_eq(space_get_at64(&test.space, 3u), atom_expr(&test.arena, symbol_parts, 2u)));
    assert(atom_eq(space_get_at64(&test.space, 4u), atom_expr(&test.arena, prefix, 2u)));
    finish(&test);
}

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols); symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols; g_hashcons = NULL; g_var_intern = NULL;
    counted_head(false); counted_head(true);
    basic(false); snapshot(false); changed_while_waiting(&symbols, false);
    basic(true); snapshot(true); changed_while_waiting(&symbols, true);
    symbol_table_free(&symbols);
    puts("source occurrence view: exact contraction, refusal, snapshot and guarded revision checks pass");
    return 0;
}

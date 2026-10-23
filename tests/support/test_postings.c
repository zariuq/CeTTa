#include "atom.h"
#include "library.h"
#include "library_postings.h"
#include "native_handle.h"
#include "space.h"
#include "space_match_backend.h"
#include "shared_transition.h"
#include <assert.h>
#include <stdlib.h>

static bool interrupted;
static int semantic_fuel = -1;
static Space *redirect_from, *redirect_to;
static size_t source_reads;
Space *eval_resolve_space_read(Space *source) {
    source_reads++;
    return source == redirect_from ? redirect_to : source;
}
static unsigned checks;
/* A small read-provider spy checks the observer's protected interval and
 * source-view admission without constructing a second runtime test matrix. */
static unsigned read_depth, admissions, direct_visits, native_visits;
static bool exact_source = true;
static Atom *observed_row;
/* The guard's inline halves (shared_transition.h) take the out-of-line ones
 * only inside a concurrent-evaluation scope; the spy keeps one open. */
_Thread_local unsigned cetta_shared_transition_scope_depth = 1u;
void cetta_shared_transition_guard_acquire(CettaSharedTransitionGuard *guard) {
    read_depth++;
    guard->held = true;
}
void cetta_shared_transition_guard_release(CettaSharedTransitionGuard *guard) {
    assert(guard->held && read_depth);
    read_depth--;
    guard->held = false;
}
bool space_contains_only_exact_atoms(Space *space) {
    assert(space && read_depth);
    admissions++;
    return exact_source;
}
CettaCount space_length64(const Space *space) {
    assert(space && read_depth);
    return 1u;
}
Atom *space_get_at64(const Space *space, CettaIndex index) {
    assert(space && read_depth && index == 0u);
    native_visits++;
    return observed_row;
}
SpaceMatchPullVisitResult space_match_backend_try_visit_atoms_direct(
    Space *space, Arena *scratch, CettaMorkAtomVisitor visitor, void *context) {
    assert(space && scratch && read_depth);
    direct_visits++;
    if (space->match_backend.kind != SPACE_ENGINE_PATHMAP)
        return SPACE_MATCH_PULL_VISIT_DECLINED;
    return visitor(observed_row, context)
        ? SPACE_MATCH_PULL_VISIT_COMPLETE : SPACE_MATCH_PULL_VISIT_TERMINATED;
}
static long fail_after = -1;
bool eval_native_interrupted(void) { return interrupted; }
int eval_current_effective_fuel_limit(void) { return semantic_fuel; }
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
static bool fail_allocation(void) {
    if (fail_after < 0) return false;
    if (fail_after-- > 0) return false;
    fail_after = -1;
    return true;
}
void *__wrap_malloc(size_t n) { return fail_allocation() ? NULL : __real_malloc(n); }
void *__wrap_calloc(size_t n, size_t size) { return fail_allocation() ? NULL : __real_calloc(n, size); }
#define check(x) do { ++checks; assert(x); } while (0)

static Atom *invoke(CettaLibraryContext *context, Arena *arena, SymbolId head,
    Atom **args, uint32_t nargs) {
    CettaCallOutcome result;
    check(cetta_postings_call(context, arena, atom_symbol_id(arena, head), args, nargs, &result));
    check(result.kind == CETTA_CALL_VALUE);
    return result.term;
}

static void protected_projection(CettaLibraryContext *context, Arena *arena) {
    Space space = {0};
    Atom *empty = atom_expr(arena, NULL, 0u);
    Atom *head = atom_symbol(arena, "Block");
    observed_row = atom_expr(arena, (Atom *[]){head,
        atom_expr(arena, (Atom *[]){atom_string(arena, "k")}, 1u),
        atom_int(arena, 2), atom_int(arena, 3)}, 4u);
    Atom *layout = atom_expr(arena, (Atom *[]){
        atom_symbol(arena, "Postings:BlockLayout"), head, atom_int(arena, 4),
        atom_int(arena, 1), atom_int(arena, 2), atom_int(arena, 3), empty, empty}, 8u);
    Atom *request = atom_expr3(arena, atom_symbol(arena, "Postings:SpaceBlocks"),
                               atom_space(arena, &space), layout);
    for (unsigned route = 0u; route < 2u; route++) {
        space.match_backend.kind = route ? SPACE_ENGINE_PATHMAP : SPACE_ENGINE_NATIVE;
        for (unsigned view = 0u; view < 4u; view++) {
            space.overlay_base = view == 1u ? &space : NULL;
            space.dep_count = view == 2u ? 1u : 0u;
            exact_source = view != 3u;
            admissions = direct_visits = native_visits = 0u;
            Atom *answer = invoke(context, arena, g_builtin_syms.lib_postings_prepare,
                                   (Atom *[]){request}, 1u);
            check(read_depth == 0u);
            if (view) {
                check(atom_is_symbol(answer, "Postings:Fallback"));
                check(direct_visits == 0u && native_visits == 0u);
                check(admissions == (view == 3u ? 1u : 0u));
            } else {
                uint64_t id;
                check(cetta_native_handle_arg(answer, "postings-table-v1", &id));
                check(admissions == 1u && direct_visits == 1u);
                check(native_visits == (route ? 0u : 1u));
                (void)invoke(context, arena, g_builtin_syms.lib_postings_close,
                             (Atom *[]){answer}, 1u);
            }
            check(cetta_native_handle_count(context) == 0u);
        }
    }
    exact_source = true;
}

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    CettaLibraryContext *context = calloc(1, sizeof(*context));
    assert(context);
    context->native_handle_next_id = 1;
    Arena source, handle, survivor, results;
    arena_init_detached(&source); arena_init_detached(&handle);
    arena_init_detached(&survivor); arena_init_detached(&results);
    protected_projection(context, &source);
    /* Only the revision getter observes these initialized read records. The
     * language-level gate below uses actual new-space owners and mutation. */
    Space original_source = {.revision = 7u}, shadow_source = {.revision = 8u};
    Atom *source_atom = atom_space(&source, &original_source);
    Atom *guard = atom_expr2(&source, source_atom, atom_int(&source, 7));
    Atom *guards = atom_expr(&source, &guard, 1u);
    Atom *verdict = invoke(context, &results, g_builtin_syms.lib_postings_sources_current,
        (Atom *[]){guards}, 1u);
    check(atom_eq(verdict, atom_bool(&results, true)));
    original_source.revision = 8u;
    verdict = invoke(context, &results, g_builtin_syms.lib_postings_sources_current,
        (Atom *[]){guards}, 1u);
    check(atom_eq(verdict, atom_bool(&results, false)));
    original_source.revision = 7u;
    redirect_from = &original_source; redirect_to = &shadow_source;
    verdict = invoke(context, &results, g_builtin_syms.lib_postings_sources_current,
        (Atom *[]){guards}, 1u);
    check(atom_eq(verdict, atom_bool(&results, false)));
    redirect_from = redirect_to = NULL;
    Atom *malformed[] = {guard, atom_int(&source, 3)};
    Atom *invalid = atom_expr(&source, malformed, 2u);
    source_reads = 0;
    verdict = invoke(context, &results, g_builtin_syms.lib_postings_sources_current,
        (Atom *[]){invalid}, 1u);
    check(atom_is_symbol(verdict, "Postings:Fallback"));
    check(source_reads == 0u);
    semantic_fuel = 10;
    verdict = invoke(context, &results, g_builtin_syms.lib_postings_sources_current,
        (Atom *[]){guards}, 1u);
    check(atom_is_symbol(verdict, "Postings:Fallback"));
    semantic_fuel = -1;
    original_source.revision = UINT64_MAX;
    verdict = invoke(context, &results, g_builtin_syms.lib_postings_sources_current,
        (Atom *[]){guards}, 1u);
    check(atom_is_symbol(verdict, "Postings:Fallback"));
    original_source.revision = 7u;
    verdict = invoke(context, &results, g_builtin_syms.lib_postings_sources_current,
        (Atom *[]){atom_expr(&source, NULL, 0u)}, 1u);
    check(atom_eq(verdict, atom_bool(&results, true)));
    Atom *positions = atom_expr(&source, (Atom *[]){atom_int(&source, -3),
        atom_int(&source, 1), atom_int(&source, 1), atom_int(&source, 5)}, 4);
    Atom *groups = atom_expr(&source, (Atom *[]){atom_expr2(&source,
        atom_string(&source, "key"), positions)}, 1);
    (void)atom_symbol_id(&handle, g_builtin_syms.lib_postings_prepare);
    for (long n = 0; n < 4; n++) {
        fail_after = n;
        Atom *declined = invoke(context, &handle, g_builtin_syms.lib_postings_prepare,
            (Atom *[]){groups}, 1);
        fail_after = -1;
        check(atom_is_symbol(declined, "Postings:Fallback"));
        check(cetta_native_handle_count(context) == 0);
    }
    cetta_native_handle_test_fail_owner_after(0);
    Atom *declined = invoke(context, &handle, g_builtin_syms.lib_postings_prepare,
        (Atom *[]){groups}, 1);
    cetta_native_handle_test_fail_owner_after(-1);
    check(atom_is_symbol(declined, "Postings:Fallback"));
    check(cetta_native_handle_count(context) == 0);
    Atom *index = invoke(context, &handle, g_builtin_syms.lib_postings_prepare,
        (Atom *[]){groups}, 1);
    check(cetta_native_handle_count(context) == 1);

    Atom *keys_ab = atom_expr(&source, (Atom *[]){
        atom_string(&source, "a"), atom_string(&source, "b")}, 2u);
    Atom *keys_a = atom_expr(&source, (Atom *[]){atom_string(&source, "a")}, 1u);
    Atom *keys_bb = atom_expr(&source, (Atom *[]){
        atom_string(&source, "b"), atom_string(&source, "b")}, 2u);
    Atom *blocks = atom_expr(&source, (Atom *[]){
        atom_expr3(&source, keys_ab, atom_int(&source, 0), atom_int(&source, 4)),
        atom_expr3(&source, keys_a, atom_int(&source, 0), atom_int(&source, 1)),
        atom_expr3(&source, keys_a, atom_int(&source, 2), atom_int(&source, 3)),
        atom_expr3(&source, keys_bb, atom_int(&source, 1), atom_int(&source, 2)),
    }, 4u);
    Atom *tagged_blocks = atom_expr2(
        &source, atom_symbol(&source, "Postings:Blocks"), blocks);
    Atom *table = invoke(context, &handle, g_builtin_syms.lib_postings_prepare,
        (Atom *[]){tagged_blocks}, 1u);
    check(cetta_native_handle_count(context) == 2u);
    uint64_t table_id = 0u;
    check(cetta_native_handle_arg(table, "postings-table-v1", &table_id));
    Atom *request_a = atom_expr3(&source, atom_string(&source, "a"),
        atom_int(&source, 4), atom_int(&source, 3));
    Atom *table_counted = invoke(context, &results, g_builtin_syms.lib_postings_counts,
        (Atom *[]){table, atom_symbol(&results, "table-before"), request_a}, 3u);
    Atom *expected_a = atom_expr(&results, (Atom *[]){
        atom_expr2(&results, atom_int(&results, 0), atom_int(&results, 1)),
        atom_expr2(&results, atom_int(&results, 2), atom_int(&results, 1)),
    }, 2u);
    check(atom_eq(table_counted, expected_a));
    Atom *request_b = atom_expr3(&source, atom_string(&source, "b"),
        atom_int(&source, 4), atom_int(&source, 3));
    table_counted = invoke(context, &results, g_builtin_syms.lib_postings_counts,
        (Atom *[]){table, atom_symbol(&results, "table-before"), request_b}, 3u);
    Atom *expected_b = atom_expr(&results, (Atom *[]){
        atom_expr2(&results, atom_int(&results, 1), atom_int(&results, 2)),
    }, 1u);
    check(atom_eq(table_counted, expected_b));
    Atom *request_unknown = atom_expr3(&source, atom_string(&source, "unknown"),
        atom_int(&source, 4), atom_int(&source, 3));
    table_counted = invoke(context, &results, g_builtin_syms.lib_postings_counts,
        (Atom *[]){table, atom_symbol(&results, "table-before"), request_unknown}, 3u);
    check(table_counted->kind == ATOM_EXPR && table_counted->expr.len == 0u);
    Atom *table_inspection = invoke(
        context, &results, g_builtin_syms.lib_postings_inspect,
        (Atom *[]){table}, 1u);
    check(table_inspection->kind == ATOM_EXPR && table_inspection->expr.len == 2u);
    check(atom_eq(table_inspection->expr.elems[0]->expr.elems[0],
                  atom_string(&results, "a")));
    Atom *table_stats = invoke(context, &results, g_builtin_syms.lib_postings_counts,
        (Atom *[]){table, atom_symbol(&results, "stats"), atom_int(&results, 0)}, 3u);
    check(table_stats->kind == ATOM_EXPR && table_stats->expr.len == 3u &&
          atom_is_symbol(table_stats->expr.elems[0], "PostingsTableStats"));
    Atom *malformed_blocks = atom_expr2(&source,
        atom_symbol(&source, "Postings:Blocks"),
        atom_expr(&source, (Atom *[]){atom_expr2(&source,
            atom_string(&source, "bad"), atom_int(&source, 0))}, 1u));
    Atom *malformed_result = invoke(
        context, &handle, g_builtin_syms.lib_postings_prepare,
        (Atom *[]){malformed_blocks}, 1u);
    check(atom_is_symbol(malformed_result, "Postings:Fallback"));
    check(cetta_native_handle_count(context) == 2u);
    (void)invoke(context, &results, g_builtin_syms.lib_postings_close,
        (Atom *[]){table}, 1u);
    check(cetta_native_handle_count(context) == 1u);

    Atom *table_reference = invoke(
        context, &handle, g_builtin_syms.lib_postings_own,
        (Atom *[]){tagged_blocks}, 1u);
    check(cetta_native_handle_count(context) == 2u);
    table_counted = invoke(context, &results, g_builtin_syms.lib_postings_counts,
        (Atom *[]){table_reference, atom_symbol(&results, "table-before"), request_a}, 3u);
    check(atom_is_symbol(table_counted, "Postings:Fallback"));
    table_inspection = invoke(context, &results, g_builtin_syms.lib_postings_inspect,
        (Atom *[]){table_reference}, 1u);
    check(atom_eq(table_inspection, tagged_blocks));
    (void)invoke(context, &results, g_builtin_syms.lib_postings_close,
        (Atom *[]){table_reference}, 1u);
    check(cetta_native_handle_count(context) == 1u);

    Atom *copy = atom_deep_copy(&survivor, index);
    arena_free(&source); arena_free(&handle);
    Atom *counted = invoke(context, &results, g_builtin_syms.lib_postings_counts,
        (Atom *[]){copy, atom_symbol(&results, "before"), atom_int(&results, 2)}, 3);
    Atom *expected = atom_expr(&results, (Atom *[]){atom_expr2(&results,
        atom_string(&results, "key"), atom_int(&results, 3))}, 1);
    check(atom_eq(counted, expected));
    Atom *inspection = invoke(context, &results, g_builtin_syms.lib_postings_inspect,
        (Atom *[]){copy}, 1);
    check(inspection->expr.len == 1);
    check(inspection->expr.elems[0]->expr.elems[1]->expr.len == 4);
    uint64_t id = 0;
    check(cetta_native_handle_arg(copy, "postings-index-v1", &id));
    Atom *forged = cetta_native_handle_atom(&results, "postings-index-v1", id);
    Atom *bad = invoke(context, &results, g_builtin_syms.lib_postings_inspect,
        (Atom *[]){forged}, 1);
    check(atom_is_symbol(bad->expr.elems[0], "Postings:Fault"));
    interrupted = true;
    CettaCallOutcome stop;
    check(cetta_postings_call(context, &results,
        atom_symbol_id(&results, g_builtin_syms.lib_postings_counts),
        (Atom *[]){copy, atom_symbol(&results, "before"), atom_int(&results, 2)}, 3, &stop));
    check(stop.kind == CETTA_CALL_INTERRUPTED);
    interrupted = false;
    (void)invoke(context, &results, g_builtin_syms.lib_postings_close, (Atom *[]){copy}, 1);
    check(cetta_native_handle_count(context) == 0);
    bad = invoke(context, &results, g_builtin_syms.lib_postings_inspect, (Atom *[]){copy}, 1);
    check(atom_is_symbol(bad->expr.elems[0], "Postings:Fault"));
    check(atom_eq(counted, expected));
    check(atom_eq(inspection->expr.elems[0]->expr.elems[0], atom_string(&results, "key")));
    arena_free(&survivor); arena_free(&results);
    cetta_native_handle_cleanup_all(context);
    free(context); symbol_table_free(&symbols); g_symbols = NULL;
    printf("PostingsOwnership %u 0\n", checks);
    return 0;
}

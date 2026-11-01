#include "prime_need.h"

#include <stdio.h>

static unsigned checks, failures;
#define CHECK(condition, message) do { checks++; if (!(condition)) { \
    fprintf(stderr, "FAIL: %s\n", message); failures++; } } while (0)

typedef struct {
    const Atom *wanted;
    unsigned visits;
} Visit;

static bool count_atom(const Atom *atom, void *context) {
    Visit *visit = context;
    if (atom == visit->wanted)
        visit->visits++;
    return true;
}

static bool excludes_atom(const Arena *forbidden, const Atom *atom) {
    PrimeNeedArenaAudit *audit = prime_need_arena_audit_new(forbidden);
    bool result = audit && prime_need_arena_audit_atom(audit, atom);
    prime_need_arena_audit_free(audit);
    return result;
}

static bool excludes_span(const Arena *forbidden, const void *pointer, size_t bytes) {
    PrimeNeedArenaAudit *audit = prime_need_arena_audit_new(forbidden);
    bool result = audit && prime_need_arena_audit_span(audit, pointer, bytes);
    prime_need_arena_audit_free(audit);
    return result;
}

static void check_view_backing(void) {
    static Atom global[] = {
        {.kind = ATOM_GROUNDED, .flags = ATOM_FLAG_ARENA_CLOSED,
         .structural_facts = ATOM_STRUCTURAL_FACTS_VALID,
         .ground = {.gkind = GV_INT, .ival = 1}},
        {.kind = ATOM_GROUNDED, .flags = ATOM_FLAG_ARENA_CLOSED,
         .structural_facts = ATOM_STRUCTURAL_FACTS_VALID,
         .ground = {.gkind = GV_INT, .ival = 2}},
        {.kind = ATOM_GROUNDED, .flags = ATOM_FLAG_ARENA_CLOSED,
         .structural_facts = ATOM_STRUCTURAL_FACTS_VALID,
         .ground = {.gkind = GV_INT, .ival = 3}},
    };
    Arena old, young;
    arena_init_detached(&old); arena_init_detached(&young);
    arena_set_older_generation(&young, &old);
    Atom *items[] = {&global[0], &global[1], &global[2]};
    Atom *source = atom_expr(&old, items, 3u);
    Atom *suffix = atom_expr_suffix(&young, source, 1u);
    Atom *again = suffix ? atom_expr_suffix(&young, suffix, 1u) : NULL;
    CHECK(suffix && again && suffix->arena_id == young.identity &&
          (suffix->flags & ATOM_FLAG_ARENA_CLOSED) &&
          arena_owns_ptr(&old, suffix->expr.elems) &&
          arena_owns_ptr(&old, again->expr.elems),
          "real linked-generation suffix headers borrow their older element vectors");
    CHECK(suffix && !excludes_atom(&old, suffix),
          "a young suffix cannot hide forbidden backing behind global closed children");
    CHECK(again && !excludes_atom(&old, again),
          "a suffix of that young view still retains the actual older vector");
    Atom *nested = suffix ? atom_expr2(&young, &global[0], suffix) : NULL;
    Atom *named = suffix ? atom_var_with_name_key(&young, suffix, var_id_from_base(71u)) : NULL;
    CHECK(nested && !excludes_atom(&old, nested),
          "a constructor-closed enclosing expression does not hide its child's borrowed vector");
    CHECK(named && !excludes_atom(&old, named),
          "a variable name key can retain a borrowed expression vector");
    AtomExprBorrowedView borrowed;
    bool ready = atom_expr_borrowed_view_init(&borrowed, source->expr.elems + 1u, 2u);
    CHECK(ready && borrowed.root.arena_id == 0u && !excludes_atom(&old, &borrowed.root),
          "a legitimate arena-zero borrowed root retains its supplied vector storage");
    Atom *rehome = atom_expr_view_rehome(&young, source, source->expr.elems);
    CHECK(rehome && !excludes_atom(&old, rehome),
          "re-homing a header does not establish the supplied backing's lifetime");
    Atom **owned = arena_alloc(&young, 3u * sizeof(*owned));
    for (unsigned i = 0u; i < 3u; i++) owned[i] = items[i];
    Atom *held = atom_expr_view_rehome(&young, source, owned);
    Atom *empty = atom_expr_suffix(&young, source, 3u);
    CHECK(held && empty && excludes_atom(&old, held) && excludes_atom(&old, empty),
          "an actually held supplied vector and empty owned suffix remain admissible");
    CHECK(excludes_span(&old, owned, 3u * sizeof(*owned)) &&
          !excludes_span(&old, source->expr.elems, 3u * sizeof(*source->expr.elems)),
          "raw metadata spans use their actual backing instead of Atom identities");
    CHECK(!excludes_span(&old, NULL, sizeof(Atom)) && !excludes_span(&old, owned, 0u),
          "absent optional spans must be skipped explicitly by callers");
    PrimeNeedArenaAudit *span_refusal = prime_need_arena_audit_new(&old);
    CHECK(span_refusal && !prime_need_arena_audit_span(span_refusal, source->expr.elems, 1u) &&
          !prime_need_arena_audit_span(span_refusal, owned, sizeof(*owned)),
          "a forbidden raw metadata span remains a sticky refusal");
    prime_need_arena_audit_free(span_refusal);
    CHECK(!excludes_span(&old, (const void *)(UINTPTR_MAX - 1u), 4u),
          "wrapping byte-span bounds cannot certify exclusion");
    Atom *capability = atom_prime_need_capability(&young, 1u, 2u, 3u);
    Atom borrowed_capability = *atom_prime_need_capability(&old, 1u, 2u, 3u);
    borrowed_capability.arena_id = 0u;
    CHECK(capability && excludes_atom(&old, capability) &&
          !excludes_atom(&old, &borrowed_capability),
          "capability records have backing ownership independent of a borrowed header");
    Atom borrowed_string = *atom_string(&old, "borrowed bytes");
    borrowed_string.arena_id = 0u;
    CHECK(!excludes_atom(&old, &borrowed_string),
          "a borrowed string header does not hide the actual forbidden byte span");
    PrimeNeedArenaAudit *growing = prime_need_arena_audit_new(&old);
    CHECK(growing && prime_need_arena_audit_atom(growing, &global[0]),
          "prepare an arena-span inventory before further block allocation");
    Atom **new_block = arena_alloc(&old, ARENA_BLOCK_SIZE + sizeof(*new_block));
    new_block[0] = &global[0];
    AtomExprBorrowedView added;
    ready = atom_expr_borrowed_view_init(&added, new_block, 1u);
    CHECK(ready && !prime_need_arena_audit_atom(growing, &added.root),
          "new forbidden blocks invalidate the cached raw-span inventory");
    prime_need_arena_audit_free(growing);
    Arena incarnation; arena_init_detached(&incarnation);
    PrimeNeedArenaAudit *old_incarnation = prime_need_arena_audit_new(&incarnation);
    CHECK(old_incarnation && prime_need_arena_audit_atom(old_incarnation, &global[0]),
          "bind an audit to one actual arena incarnation");
    arena_free(&incarnation); arena_init_detached(&incarnation);
    CHECK(!prime_need_arena_audit_atom(old_incarnation, &global[0]),
          "arena-address reuse cannot renew an old audit's ownership evidence");
    prime_need_arena_audit_free(old_incarnation); arena_free(&incarnation);
    arena_free(&young); arena_free(&old);
}

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    check_view_backing();
    Arena stable, forbidden;
    arena_init_detached(&stable);
    arena_init_detached(&forbidden);
    Atom *origin = atom_expr2(&stable, atom_symbol(&stable, "held"), atom_int(&stable, 7));
    PrimeNeedSnapshot root = {0}, entrance = {0}, foreign_metadata = {0};
    uint64_t thunk = 0u, foreign_thunk = 0u;
    bool ready = prime_need_snapshot_begin(&root) &&
        prime_need_snapshot_allocate(&stable, &root, origin, &entrance, &thunk) &&
        prime_need_snapshot_allocate(&forbidden, &root, origin,
            &foreign_metadata, &foreign_thunk);
    CHECK(ready, "prepare held payloads with separately owned Need metadata");
    PrimeNeedReadCapture *capture = ready ? prime_need_read_capture_new(&root, &entrance) : NULL;
    PrimeNeedReadCapture *metadata = ready
        ? prime_need_read_capture_new(&foreign_metadata, &foreign_metadata) : NULL;
    PrimeNeedArenaAudit *audit = prime_need_arena_audit_new(&forbidden);
    CHECK(capture && metadata && audit, "prepare independent read-capture audit owners");
    if (!capture || !metadata || !audit)
        goto cleanup;

    CHECK(prime_need_arena_audit_atom(audit, origin) &&
          prime_need_arena_audit_read_capture(audit, capture),
          "held snapshots and their full payload carriers exclude the forbidden arena");
    Visit visit = {.wanted = origin};
    CHECK(prime_need_read_capture_atoms_satisfy(metadata, count_atom, &visit) &&
          visit.visits == 2u && !prime_need_arena_audit_read_capture(audit, metadata),
          "safe Atom payloads do not hide forbidden common and entrance metadata");
    PrimeNeedSnapshot mixed = {0};
    uint64_t mixed_thunk = 0u;
    PrimeNeedArenaAudit *mixed_audit = prime_need_arena_audit_new(&forbidden);
    CHECK(mixed_audit && prime_need_snapshot_allocate(
              &stable, &foreign_metadata, origin, &mixed, &mixed_thunk),
          "prepare a stable top frame above a forbidden metadata prefix");
    CHECK(!prime_need_arena_audit_snapshot(mixed_audit, &mixed) &&
          !prime_need_arena_audit_snapshot(mixed_audit, &mixed),
          "a refused mixed metadata prefix stays refused on a repeated scan");
    prime_need_arena_audit_free(mixed_audit);
    prime_need_arena_audit_free(audit);
    audit = prime_need_arena_audit_new(&forbidden);
    CHECK(audit != NULL, "dependency inspection uses a fresh audit after metadata refusal");
    if (!audit)
        goto cleanup;
    CHECK(prime_need_read_capture_has_cuts(capture, &root, &entrance) &&
          prime_need_read_capture_complete(capture),
          "inspection leaves captured cuts and evidence completeness unchanged");

    Atom *name = atom_expr2(&forbidden, atom_symbol(&forbidden, "private-name"),
                           atom_string(&forbidden, "key"));
    Atom *named = atom_var_with_name_key(&stable, name, var_id_from_base(17u));
    PrimeNeedArenaAudit *atom_audit = prime_need_arena_audit_new(&forbidden);
    CHECK(named && !arena_owns_atom(&forbidden, named) &&
          atom_audit && !prime_need_arena_audit_atom(atom_audit, named),
          "an external variable header cannot hide its forbidden structural name key");
    CHECK(!prime_need_arena_audit_atom(atom_audit, origin),
          "a refused Atom payload cannot be forgotten by later audit roots");
    prime_need_arena_audit_free(atom_audit);
    uint64_t absent = 0u;
    PrimeNeedReadScope scope;
    prime_need_read_scope_enter(&scope, capture);
    bool found = prime_need_snapshot_find_imported_cell(
        &entrance, UINT64_C(987654321), named, &absent);
    prime_need_read_scope_leave(&scope);
    CHECK(!found && prime_need_read_capture_complete(capture),
          "a negative imported-cell lookup still records complete support");
    visit = (Visit){.wanted = named};
    CHECK(prime_need_read_capture_atoms_satisfy(capture, count_atom, &visit) && visit.visits == 1u,
          "read-capture traversal includes the actual negative-lookup origin");
    CHECK(!prime_need_arena_audit_read_capture(audit, capture),
          "a hidden dependency-origin name key prevents collection");
    PrimeNeedReadCapture *frozen = prime_need_read_capture_copy(capture);
    PrimeNeedArenaAudit *frozen_audit = prime_need_arena_audit_new(&forbidden);
    CHECK(frozen && frozen_audit && !prime_need_arena_audit_read_capture(frozen_audit, frozen),
          "freezing support does not erase its dependency payload obligation");
    prime_need_arena_audit_free(frozen_audit);
    prime_need_read_capture_free(frozen);

    PrimeNeedSnapshot other = {0}, other_world = {0};
    uint64_t other_id = 0u;
    PrimeNeedCellView observed;
    CHECK(prime_need_snapshot_begin(&other) &&
          prime_need_snapshot_allocate(&stable, &other, origin, &other_world, &other_id),
          "prepare a foreign-session read without changing source results");
    prime_need_read_scope_enter(&scope, capture);
    found = prime_need_snapshot_lookup(&other_world, other_id, &observed);
    prime_need_read_scope_leave(&scope);
    prime_need_arena_audit_free(audit);
    audit = prime_need_arena_audit_new(&forbidden);
    visit = (Visit){.wanted = named};
    CHECK(audit && found && !prime_need_read_capture_complete(capture) &&
          prime_need_read_capture_atoms_satisfy(capture, count_atom, &visit) &&
          visit.visits == 1u && !prime_need_arena_audit_read_capture(audit, capture),
          "incomplete evidence keeps its earned negative-origin payload live");

    Atom opaque = *atom_int(&stable, 1);
    opaque.kind = ATOM_GROUNDED;
    opaque.ground.gkind = GV_CAPTURE;
    opaque.flags = 0u;
    opaque.structural_facts = 0u;
    prime_need_arena_audit_free(audit);
    audit = prime_need_arena_audit_new(&forbidden);
    CHECK(audit && !prime_need_arena_audit_atom(audit, &opaque) &&
          !prime_need_arena_audit_atom(NULL, origin) &&
          !prime_need_arena_audit_read_capture(NULL, capture) &&
          !prime_need_read_capture_atoms_satisfy(capture, NULL, NULL),
          "unknown opaque payloads and missing audit inputs refuse conservatively");

cleanup:
    prime_need_arena_audit_free(audit);
    prime_need_read_capture_free(metadata);
    prime_need_read_capture_free(capture);
    arena_free(&forbidden);
    arena_free(&stable);
    g_symbols = NULL;
    symbol_table_free(&symbols);
    printf("PrimeNeedReadAudit checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}

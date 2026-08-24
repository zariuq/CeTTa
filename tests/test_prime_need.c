#include <stdio.h>

#include "atom.h"
#include "prime_need.h"
#include "symbol.h"

static unsigned checks = 0u;
static unsigned failures = 0u;

#define CHECK(condition, label)                                                \
    do {                                                                       \
        checks++;                                                              \
        if (!(condition)) {                                                    \
            fprintf(stderr, "FAIL: %s\n", (label));                          \
            failures++;                                                       \
        }                                                                      \
    } while (0)

int main(void) {
    SymbolTable symbols;
    Arena arena;
    Arena branch_arena;
    Arena snapshot_branch_arena;
    Arena promoted_arena;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    arena_init(&arena);
    arena_init(&branch_arena);
    arena_init(&snapshot_branch_arena);
    arena_init(&promoted_arena);

    PrimeNeedSnapshot root;
    prime_need_snapshot_init(&root);
    CHECK(!prime_need_snapshot_present(&root), "empty snapshot is absent");
    CHECK(prime_need_snapshot_begin(&root), "snapshot session begins");
    CHECK(prime_need_snapshot_present(&root), "begun snapshot is present");

    PrimeNeedSnapshot thunk;
    uint64_t thunk_id = 0u;
    Atom *term = atom_symbol(&arena, "delayed");
    CHECK(prime_need_snapshot_allocate(&arena, &root, term, &thunk,
                                       &thunk_id),
          "allocate extends the root");
    CHECK(!prime_need_snapshot_excludes_arena(&thunk, &arena) &&
              prime_need_snapshot_excludes_arena(
                  &thunk, &promoted_arena),
          "snapshot ownership audit distinguishes its owner arena");
    CHECK(prime_need_snapshot_is_ancestor(&root, &thunk),
          "root precedes allocation");
    PrimeNeedCellView cell;
    CHECK(prime_need_snapshot_lookup(&thunk, thunk_id, &cell) &&
              cell.cache_state == PRIME_NEED_CACHE_EMPTY &&
              cell.origin == term && cell.cached == NULL &&
              cell.authority_id != 0u && cell.evaluator_id == 0u,
          "allocation stores an immutable origin and empty cache");
    Atom *branch_term = atom_symbol(
        &snapshot_branch_arena, "branch-delayed");
    PrimeNeedSnapshot branch_thunk;
    uint64_t branch_thunk_id = 0u;
    CHECK(prime_need_snapshot_allocate(
              &snapshot_branch_arena, &thunk, branch_term,
              &branch_thunk, &branch_thunk_id) &&
              branch_thunk.owner == &snapshot_branch_arena &&
              branch_thunk.closure_owner == NULL &&
              !prime_need_snapshot_excludes_arena(
                  &branch_thunk, &arena) &&
              !prime_need_snapshot_excludes_arena(
                  &branch_thunk, &snapshot_branch_arena),
          "snapshot branch suffix retains its longer-lived prefix");
    CHECK(prime_need_snapshot_promote_suffix(
              &arena, &snapshot_branch_arena, &branch_thunk) &&
              branch_thunk.owner == &arena &&
              branch_thunk.closure_owner == &arena &&
              prime_need_snapshot_excludes_arena(
                  &branch_thunk, &snapshot_branch_arena),
          "snapshot suffix promotion closes directly into its stable parent");
    arena_free(&snapshot_branch_arena);
    PrimeNeedCellView branch_cell;
    CHECK(prime_need_snapshot_lookup(
              &branch_thunk, branch_thunk_id, &branch_cell) &&
              branch_cell.origin && atom_is_symbol(
                  branch_cell.origin, "branch-delayed") &&
              prime_need_snapshot_is_ancestor(&thunk, &branch_thunk),
          "promoted snapshot survives reclamation of its branch arena");
    PrimeNeedSnapshot promoted_thunk = thunk;
    CHECK(prime_need_snapshot_promote(
              &promoted_arena, &promoted_thunk) &&
              prime_need_snapshot_excludes_arena(
                  &promoted_thunk, &arena) &&
              !prime_need_snapshot_excludes_arena(
                  &promoted_thunk, &promoted_arena),
          "snapshot promotion removes every source-arena frame");
    uint64_t storage_key = cell.storage_key;
    PrimeNeedSnapshot rehydrated;
    uint64_t rehydrated_id = 0u;
    CHECK(storage_key != 0u &&
              prime_need_snapshot_allocate_persisted(
                  &arena, &thunk, term, storage_key,
                  &rehydrated, &rehydrated_id) &&
              rehydrated_id == thunk_id && rehydrated.top == thunk.top,
          "rehydration reuses one cell for one stored graph key");
    Atom *different_origin = atom_symbol(&arena, "different-origin");
    PrimeNeedSnapshot instantiated;
    uint64_t instantiated_id = 0u;
    PrimeNeedCellView instantiated_cell;
    CHECK(prime_need_snapshot_allocate_persisted(
              &arena, &thunk, different_origin, storage_key,
              &instantiated, &instantiated_id) &&
              instantiated_id != thunk_id &&
              prime_need_snapshot_lookup(
                  &instantiated, instantiated_id, &instantiated_cell) &&
              instantiated_cell.storage_key != storage_key &&
              instantiated_cell.origin == different_origin,
          "substitution under one stored key allocates a fresh graph node");
    PrimeNeedSnapshot instantiated_again;
    uint64_t instantiated_again_id = 0u;
    CHECK(prime_need_snapshot_allocate_persisted(
              &arena, &instantiated, different_origin, storage_key,
              &instantiated_again, &instantiated_again_id) &&
              instantiated_again_id == instantiated_id &&
              instantiated_again.top == instantiated.top,
          "one imported key and instantiated origin reuse one graph node");
    PrimeNeedSnapshot original_again;
    uint64_t original_again_id = 0u;
    CHECK(prime_need_snapshot_allocate_persisted(
              &arena, &instantiated, term, storage_key,
              &original_again, &original_again_id) &&
              original_again_id == thunk_id &&
              original_again.top == instantiated.top,
          "the original stored key and origin still recover the original node");
    PrimeNeedSnapshot second_thunk;
    uint64_t second_thunk_id = 0u;
    PrimeNeedCellView second_cell;
    CHECK(prime_need_snapshot_allocate(
              &arena, &thunk, term, &second_thunk, &second_thunk_id) &&
              prime_need_snapshot_lookup(
                  &second_thunk, second_thunk_id, &second_cell) &&
              second_cell.storage_key != storage_key,
          "two ordinary delays remain distinct graph nodes");
    uint64_t source_occurrence = prime_need_fresh_source_occurrence();
    uint64_t next_source_occurrence = prime_need_fresh_source_occurrence();
    CHECK(source_occurrence != 0u &&
              next_source_occurrence != 0u &&
              source_occurrence != next_source_occurrence,
          "dynamic source occurrences have distinct nonzero identities");
    PrimeNeedSnapshot source_argument;
    uint64_t source_argument_id = 0u;
    PrimeNeedCellView source_argument_cell;
    CHECK(prime_need_snapshot_allocate_source_argument(
              &arena, &thunk, term, source_occurrence, 2u,
              &source_argument, &source_argument_id) &&
              prime_need_snapshot_lookup(
                  &source_argument, source_argument_id,
                  &source_argument_cell) &&
              source_argument_cell.source_occurrence_id ==
                  source_occurrence &&
              source_argument_cell.source_argument_index == 2u,
          "a source argument cell retains application and position identity");

#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
    VarId capture_ids[] = {202u, 101u, 202u};
    PrimeNeedSnapshot captured;
    uint64_t captured_id = 0u;
    PrimeNeedCellView captured_cell;
    CHECK(prime_need_snapshot_allocate_closure(
              &arena, &source_argument, term,
              capture_ids, 3u, &captured, &captured_id) &&
              prime_need_snapshot_lookup(
                  &captured, captured_id, &captured_cell) &&
              captured_cell.capture_known &&
              captured_cell.capture_var_count == 2u &&
              captured_cell.capture_var_ids[0] == 101u &&
              captured_cell.capture_var_ids[1] == 202u,
          "closure allocation stores a canonical exact capture set");
    PrimeNeedSnapshot captured_evaluating;
    CHECK(prime_need_snapshot_start_evaluation(
              &arena, &captured, captured_id, 13u,
              &captured_evaluating) &&
              prime_need_snapshot_lookup(
                  &captured_evaluating, captured_id,
                  &captured_cell) &&
              captured_cell.capture_known &&
              captured_cell.capture_var_count == 2u &&
              captured_cell.capture_var_ids[0] == 101u &&
              captured_cell.capture_var_ids[1] == 202u,
          "cache transitions preserve exact closure captures");
    PrimeNeedSnapshot empty_capture;
    uint64_t empty_capture_id = 0u;
    PrimeNeedCellView empty_capture_cell;
    CHECK(prime_need_snapshot_allocate_closure(
              &arena, &captured, term, NULL, 0u,
              &empty_capture, &empty_capture_id) &&
              prime_need_snapshot_lookup(
                  &empty_capture, empty_capture_id,
                  &empty_capture_cell) &&
              empty_capture_cell.capture_known &&
              empty_capture_cell.capture_var_count == 0u &&
              empty_capture_cell.capture_var_ids == NULL,
          "an exact empty capture is distinct from unavailable metadata");
    PrimeNeedCellView legacy_capture_cell;
    CHECK(prime_need_snapshot_lookup(
              &thunk, thunk_id, &legacy_capture_cell) &&
              !legacy_capture_cell.capture_known &&
              legacy_capture_cell.capture_var_count == 0u,
          "legacy allocation remains an explicit fail-closed fallback");
    PrimeNeedSnapshot captured_persisted;
    uint64_t captured_persisted_id = 0u;
    CHECK(prime_need_snapshot_allocate_persisted_closure(
              &arena, &source_argument, term, UINT64_C(8128),
              capture_ids, 3u, &captured_persisted,
              &captured_persisted_id),
          "persisted closure allocation admits an exact capture set");
    VarId same_capture_ids[] = {101u, 202u};
    PrimeNeedSnapshot captured_reused;
    uint64_t captured_reused_id = 0u;
    CHECK(prime_need_snapshot_allocate_persisted_closure(
              &arena, &captured_persisted, term, UINT64_C(8128),
              same_capture_ids, 2u, &captured_reused,
              &captured_reused_id) &&
              captured_reused_id == captured_persisted_id &&
              captured_reused.top == captured_persisted.top,
          "persisted closure identity accepts the same capture set");
    VarId conflicting_capture_id = 303u;
    CHECK(!prime_need_snapshot_allocate_persisted_closure(
              &arena, &captured_persisted, term, UINT64_C(8128),
              &conflicting_capture_id, 1u, &captured_reused,
              &captured_reused_id),
          "persisted closure identity rejects conflicting capture metadata");
#endif

    Atom *ref = prime_need_ref(&arena, &thunk, thunk_id);
    uint64_t parsed_id = 0u;
    uint64_t parsed_authority = 0u;
    CHECK(ref && prime_need_ref_parse(ref, NULL, &parsed_id,
                                     &parsed_authority) &&
              prime_need_ref_belongs_to(ref, &thunk, &parsed_id) &&
              parsed_id == thunk_id && parsed_authority == cell.authority_id,
          "opaque capability round-trips within its session");
    Atom *ordinary_graph = atom_expr2(
        &arena, atom_int(&arena, 1), atom_int(&arena, 2));
    CHECK(ordinary_graph && !atom_has_registry_refs(ordinary_graph),
          "ordinary immutable data proves it contains no private reference");
    Atom *capability_graph = atom_expr2(
        &arena, atom_symbol(&arena, "box"), ref);
    CHECK(capability_graph && atom_has_registry_refs(ref) &&
              atom_has_registry_refs(capability_graph),
          "private-reference reachability propagates through its container");
    PrimeNeedSnapshot other_root;
    prime_need_snapshot_init(&other_root);
    CHECK(prime_need_snapshot_begin(&other_root) &&
              !prime_need_ref_belongs_to(ref, &other_root, NULL),
          "capability cannot cross a session boundary");
    Atom *source_forgery = atom_expr3(
        &arena, atom_symbol(&arena, "__prime_need_ref_v1"),
        atom_int(&arena, (int64_t)thunk.session_id),
        atom_int(&arena, (int64_t)thunk_id));
    CHECK(!prime_need_ref_parse(source_forgery, NULL, NULL, NULL) &&
              !prime_need_ref_belongs_to(source_forgery, &thunk, NULL),
          "source-shaped sequential IDs cannot forge a capability");
    Atom *wrong_authority = atom_prime_need_capability(
        &arena, thunk.session_id, thunk_id, cell.authority_id + 1u);
    CHECK(wrong_authority &&
              !prime_need_ref_belongs_to(wrong_authority, &thunk, NULL),
          "authority mismatch fails closed");

    uint32_t parsed_rights = 0u;
    CHECK(prime_need_ref_parse_rights(
              ref, NULL, NULL, NULL, &parsed_rights) &&
              parsed_rights == CETTA_PRIME_NEED_RIGHT_ALL,
          "new suspensions begin with force and inspect rights");
    Atom *inspect_ref = prime_need_ref_restrict(
        &arena, ref, &thunk, CETTA_PRIME_NEED_RIGHT_INSPECT);
    Atom *force_ref = prime_need_ref_restrict(
        &arena, ref, &thunk, CETTA_PRIME_NEED_RIGHT_FORCE);
    CHECK(inspect_ref &&
              prime_need_ref_belongs_to(inspect_ref, &thunk, NULL) &&
              prime_need_ref_belongs_to_with_rights(
                  inspect_ref, &thunk,
                  CETTA_PRIME_NEED_RIGHT_INSPECT, NULL) &&
              !prime_need_ref_belongs_to_with_rights(
                  inspect_ref, &thunk,
                  CETTA_PRIME_NEED_RIGHT_FORCE, NULL),
          "inspect-only attenuation preserves identity but removes force");
    CHECK(force_ref &&
              prime_need_ref_belongs_to(force_ref, &thunk, NULL) &&
              prime_need_ref_belongs_to_with_rights(
                  force_ref, &thunk,
                  CETTA_PRIME_NEED_RIGHT_FORCE, NULL) &&
              !prime_need_ref_belongs_to_with_rights(
                  force_ref, &thunk,
                  CETTA_PRIME_NEED_RIGHT_INSPECT, NULL),
          "force-only attenuation preserves identity but removes inspection");
    CHECK(!prime_need_ref_restrict(
              &arena, inspect_ref, &thunk,
              CETTA_PRIME_NEED_RIGHT_ALL),
          "attenuated rights cannot be restored");
    Atom *inspect_copy = atom_deep_copy(&arena, inspect_ref);
    CHECK(inspect_copy && atom_eq(inspect_copy, inspect_ref) &&
              !atom_eq(inspect_copy, ref) &&
              !prime_need_ref_belongs_to_with_rights(
                  inspect_copy, &thunk,
                  CETTA_PRIME_NEED_RIGHT_FORCE, NULL),
          "copying preserves an attenuated rights set");

    PrimeNeedSnapshot evaluating;
    const uint64_t evaluator_id = 17u;
    CHECK(prime_need_snapshot_start_evaluation(
              &arena, &thunk, thunk_id, evaluator_id, &evaluating),
          "force enters the evaluating state");
    CHECK(prime_need_snapshot_lookup(&evaluating, thunk_id, &cell) &&
              cell.cache_state == PRIME_NEED_CACHE_EVALUATING &&
              cell.origin == term && cell.cached == NULL &&
              cell.evaluator_id == evaluator_id,
          "evaluation preserves origin and records its owner");
    PrimeNeedSnapshot invalid;
    CHECK(!prime_need_snapshot_start_evaluation(
              &arena, &evaluating, thunk_id, evaluator_id, &invalid),
          "an evaluating cell cannot be entered twice");
    CHECK(!prime_need_snapshot_resolve_value(
              &arena, &evaluating, thunk_id, evaluator_id + 1u,
              atom_symbol(&arena, "wrong-owner"), &invalid),
          "a different evaluator cannot resolve the demand");

    PrimeNeedSnapshot faulted;
    Atom *fault_value = atom_symbol(&arena, "fault");
    CHECK(prime_need_snapshot_resolve_stable_fault(
              &arena, &evaluating, thunk_id, evaluator_id,
              fault_value, &faulted),
          "a certified stable fault is memoized");
    PrimeNeedCellView repeated_fault;
    CHECK(prime_need_snapshot_lookup(&faulted, thunk_id, &cell) &&
              prime_need_snapshot_lookup(&faulted, thunk_id,
                                         &repeated_fault) &&
              cell.cache_state == PRIME_NEED_CACHE_STABLE_FAULT &&
              repeated_fault.cache_state ==
                  PRIME_NEED_CACHE_STABLE_FAULT &&
              cell.cached == repeated_fault.cached,
          "repeated fault lookup returns the cached receipt");
    CHECK(cell.origin == term && cell.cached == fault_value,
          "stable-fault caching does not overwrite the origin");

    PrimeNeedSnapshot left_evaluating;
    PrimeNeedSnapshot right_evaluating;
    CHECK(prime_need_snapshot_start_evaluation(
              &arena, &thunk, thunk_id, 21u, &left_evaluating) &&
              prime_need_snapshot_start_evaluation(
                  &arena, &thunk, thunk_id, 22u, &right_evaluating),
          "sibling evaluations may refine the same empty ancestor");
    PrimeNeedSnapshot left;
    PrimeNeedSnapshot right;
    Atom *left_value = atom_symbol(&arena, "left");
    Atom *right_value = atom_symbol(&arena, "right");
    CHECK(prime_need_snapshot_resolve_value(
              &arena, &left_evaluating, thunk_id, 21u, left_value, &left),
          "left branch memoizes its value");
    CHECK(prime_need_snapshot_resolve_value(
              &arena, &right_evaluating, thunk_id, 22u,
              right_value, &right),
          "right branch memoizes its value");
    CHECK(!prime_need_snapshot_is_ancestor(&left, &right) &&
              !prime_need_snapshot_is_ancestor(&right, &left),
          "sibling branch refinements are incomparable");
    PrimeNeedSnapshot rejected_merge = left;
    CHECK(!prime_need_snapshot_merge(&rejected_merge, &right),
          "sibling heaps cannot merge");
    CHECK(prime_need_snapshot_lookup(&rejected_merge, thunk_id, &cell) &&
              cell.cached == left_value && cell.origin == term,
          "failed merge leaves destination unchanged");

    PrimeNeedSnapshot ancestor_merge = thunk;
    CHECK(prime_need_snapshot_merge(&ancestor_merge, &left) &&
              prime_need_snapshot_lookup(&ancestor_merge, thunk_id, &cell) &&
              cell.cached == left_value,
          "ancestor merge selects the descendant");
    CHECK(prime_need_snapshot_merge(&ancestor_merge, &thunk) &&
              prime_need_snapshot_lookup(&ancestor_merge, thunk_id, &cell) &&
              cell.cached == left_value,
          "merging an ancestor cannot rewind a branch");

    PrimeNeedSnapshot retrying;
    PrimeNeedSnapshot retried;
    CHECK(prime_need_snapshot_start_evaluation(
              &arena, &thunk, thunk_id, 31u, &retrying) &&
              prime_need_snapshot_retry_evaluation(
                  &arena, &retrying, thunk_id, 31u, &retried),
          "uncertified faults return the cell to a retryable state");
    CHECK(prime_need_snapshot_lookup(&retried, thunk_id, &cell) &&
              cell.cache_state == PRIME_NEED_CACHE_EMPTY &&
              cell.origin == term && cell.cached == NULL,
          "retry preserves the immutable origin");

    PrimeNeedSnapshot promoted = left;
    CHECK(prime_need_snapshot_promote(&promoted_arena, &promoted),
          "snapshot promotes across arena ownership");
    CHECK(prime_need_snapshot_is_ancestor(&thunk, &promoted) &&
              prime_need_snapshot_lookup(&promoted, thunk_id, &cell) &&
              atom_is_symbol(cell.origin, "delayed") &&
              atom_is_symbol(cell.cached, "left") &&
              prime_need_ref_belongs_to(ref, &promoted, NULL),
          "promotion preserves lineage, origin, cache, and authority");
    CHECK(!prime_need_snapshot_start_evaluation(
              &arena, &left, thunk_id + 1u, 41u, &invalid),
          "unknown thunk transition fails closed");
    CHECK(!prime_need_snapshot_resolve_stable_fault(
              &arena, &thunk, thunk_id, 41u, fault_value, &invalid),
          "resolution cannot skip the evaluating state");

#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
    PrimeNeedSnapshot captured_promoted = captured_evaluating;
    CHECK(prime_need_snapshot_promote(
              &promoted_arena, &captured_promoted) &&
              prime_need_snapshot_lookup(
                  &captured_promoted, captured_id,
                  &captured_cell) &&
              captured_cell.capture_known &&
              captured_cell.capture_var_count == 2u &&
              captured_cell.capture_var_ids[0] == 101u &&
              captured_cell.capture_var_ids[1] == 202u,
          "promotion preserves exact closure captures");
#endif

#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    PrimeNeedReceipt receipt_root;
    prime_need_receipt_init(&receipt_root);
    CHECK(!prime_need_receipt_present(&receipt_root),
          "empty dependency receipt is absent");
    CHECK(prime_need_receipt_begin(&arena, &receipt_root) &&
              prime_need_receipt_present(&receipt_root) &&
              prime_need_receipt_event_count(&receipt_root) == 0u,
          "an evaluation episode has an explicit empty receipt root");
    CHECK(!prime_need_receipt_excludes_arena(
              &receipt_root, &arena) &&
              prime_need_receipt_excludes_arena(
                  &receipt_root, &promoted_arena),
          "receipt ownership audit distinguishes its owner arena");
    PrimeNeedReceipt empty_promoted = receipt_root;
    CHECK(prime_need_receipt_promote(&promoted_arena, &empty_promoted) &&
              prime_need_receipt_equal(&empty_promoted, &receipt_root),
          "the empty receipt root promotes without inventing an event");
    CHECK(prime_need_receipt_observe_cell(
              &arena, &receipt_root, thunk.session_id, thunk_id,
              left_value, &receipt_root) &&
              prime_need_receipt_present(&receipt_root),
          "a cell observation begins a causal receipt");
    PrimeNeedReceiptEvent receipt_event;
    CHECK(prime_need_receipt_event_count(&receipt_root) == 1u &&
              prime_need_receipt_event_at(
                  &receipt_root, 0u, &receipt_event) &&
              receipt_event.kind == PRIME_NEED_RECEIPT_OBSERVE_CELL &&
              receipt_event.need_session_id == thunk.session_id &&
              receipt_event.thunk_id == thunk_id &&
              atom_eq(receipt_event.after, left_value),
          "cell receipt exposes semantic identity and observed outcome");

    PrimeNeedReceipt inspected_origin;
    CHECK(prime_need_receipt_inspect_origin(
              &arena, &receipt_root, thunk.session_id, thunk_id,
              term, &inspected_origin),
          "origin inspection appends a distinct receipt event");
    CHECK(prime_need_receipt_event_count(&inspected_origin) == 2u &&
              prime_need_receipt_event_at(
                  &inspected_origin, 1u, &receipt_event) &&
              receipt_event.kind == PRIME_NEED_RECEIPT_INSPECT_ORIGIN &&
              atom_eq(receipt_event.after, term),
          "inspection receipts identify the cell and disclosed origin");

    Atom *equation = atom_expr3(
        &arena, atom_symbol(&arena, "="), term, left_value);
    PrimeNeedReceipt used_equation;
    CHECK(prime_need_receipt_use_equation(
              &arena, &receipt_root, source_occurrence, 17u,
              equation, left_value, &used_equation) &&
              prime_need_receipt_event_at(
                  &used_equation, 1u, &receipt_event) &&
              receipt_event.kind == PRIME_NEED_RECEIPT_USE_EQUATION &&
              receipt_event.source_occurrence_id == source_occurrence &&
              receipt_event.rule_occurrence_id == 17u &&
              atom_eq(receipt_event.before, equation) &&
              atom_eq(receipt_event.after, left_value),
          "equation use retains dynamic call and admitted rule occurrence");
    PrimeNeedReceipt used_equation_ids;
    CHECK(prime_need_receipt_use_equation_ids(
              &arena, &receipt_root, source_occurrence, 18u,
              &used_equation_ids) &&
              prime_need_receipt_event_at(
                  &used_equation_ids, 1u, &receipt_event) &&
              receipt_event.kind == PRIME_NEED_RECEIPT_USE_EQUATION &&
              receipt_event.source_occurrence_id == source_occurrence &&
              receipt_event.rule_occurrence_id == 18u &&
              receipt_event.before == NULL &&
              receipt_event.after == NULL &&
              !prime_need_receipt_equal(
                  &used_equation_ids, &used_equation),
          "compact equation use keeps causal identity without display payload");
    PrimeNeedReceipt inherited_owner_equation;
    CHECK(prime_need_receipt_use_equation_ids(
              &branch_arena, &receipt_root, source_occurrence, 19u,
              &inherited_owner_equation) &&
              inherited_owner_equation.owner == &arena,
          "ordinary receipt extension inherits the causal base lifetime");
    PrimeNeedReceipt branch_local_equation;
    CHECK(prime_need_receipt_use_equation_ids_branch_local(
              &branch_arena, &receipt_root, source_occurrence, 20u,
              &branch_local_equation) &&
              branch_local_equation.owner == &branch_arena &&
              prime_need_receipt_event_count(&branch_local_equation) == 2u &&
              !prime_need_receipt_excludes_arena(
                  &branch_local_equation, &arena) &&
              !prime_need_receipt_excludes_arena(
                  &branch_local_equation, &branch_arena),
          "branch-local extension retains its older base across arenas");
    PrimeNeedReceipt promoted_branch_local = branch_local_equation;
    CHECK(prime_need_receipt_promote(
              &promoted_arena, &promoted_branch_local) &&
              prime_need_receipt_event_count(&promoted_branch_local) == 2u &&
              prime_need_receipt_excludes_arena(
                  &promoted_branch_local, &arena) &&
              prime_need_receipt_excludes_arena(
                  &promoted_branch_local, &branch_arena),
          "promotion closes a branch-local receipt into its destination");
    PrimeNeedReceipt suffix_promoted_branch_local =
        branch_local_equation;
    CHECK(prime_need_receipt_promote_suffix(
              &arena, &branch_arena,
              &suffix_promoted_branch_local) &&
              suffix_promoted_branch_local.owner == &arena &&
              suffix_promoted_branch_local.top !=
                  branch_local_equation.top &&
              prime_need_receipt_event_count(
                  &suffix_promoted_branch_local) == 2u &&
              prime_need_receipt_excludes_arena(
                  &suffix_promoted_branch_local, &branch_arena) &&
              !prime_need_receipt_excludes_arena(
                  &suffix_promoted_branch_local, &arena),
          "receipt suffix promotion retains its stable parent without copying it");
    PrimeNeedReceipt resampled;
    CHECK(prime_need_receipt_resample(
              &arena, &receipt_root, term, &resampled) &&
              prime_need_receipt_event_at(
                  &resampled, 1u, &receipt_event) &&
              receipt_event.kind == PRIME_NEED_RECEIPT_RESAMPLE &&
              atom_eq(receipt_event.before, term),
          "resampling records one fresh evaluation occurrence");

    PrimeNeedReceipt observation_left;
    PrimeNeedReceipt observation_right;
    CHECK(prime_need_receipt_observe_cell(
              &arena, &receipt_root, thunk.session_id, second_thunk_id,
              left_value, &observation_left),
          "left sibling records one cell outcome");
    CHECK(prime_need_receipt_observe_cell(
              &arena, &receipt_root, thunk.session_id, second_thunk_id,
              right_value, &observation_right),
          "right sibling records a different cell outcome");
    CHECK(!prime_need_receipt_compatible(
              &observation_left, &observation_right),
          "different outcomes for one Need cell conflict");
    PrimeNeedReceipt observation_rejected = observation_left;
    CHECK(!prime_need_receipt_merge(
              &observation_rejected, &observation_right) &&
              prime_need_receipt_equal(
                  &observation_rejected, &observation_left),
          "conflicting receipt merge fails without changing its input");

    PrimeNeedReceipt same_observation_left;
    PrimeNeedReceipt same_observation_right;
    CHECK(prime_need_receipt_observe_cell(
              &arena, &receipt_root, thunk.session_id, second_thunk_id,
              left_value, &same_observation_left) &&
          prime_need_receipt_observe_cell(
              &arena, &receipt_root, thunk.session_id, second_thunk_id,
              left_value, &same_observation_right),
          "siblings may independently observe the same cell outcome");
    PrimeNeedReceipt same_observation_left_deep;
    PrimeNeedReceipt same_observation_left_deeper;
    CHECK(prime_need_receipt_observe_cell(
              &arena, &same_observation_left, thunk.session_id,
              second_thunk_id + 10u, left_value,
              &same_observation_left_deep) &&
              prime_need_receipt_observe_cell(
                  &arena, &same_observation_left_deep,
                  thunk.session_id, second_thunk_id + 11u,
                  left_value, &same_observation_left_deeper) &&
              prime_need_receipt_compatible(
                  &same_observation_left,
                  &same_observation_right) &&
              !prime_need_receipt_is_ancestor(
                  &same_observation_left, &same_observation_right) &&
              !prime_need_receipt_is_ancestor(
                  &same_observation_right, &same_observation_left) &&
              prime_need_receipt_is_ancestor(
                  &same_observation_left,
                  &same_observation_left_deeper) &&
              !prime_need_receipt_is_ancestor(
                  &same_observation_right,
                  &same_observation_left_deeper),
          "exact reachability accepts a deep ancestor and rejects "
          "a shallower disconnected sibling");
    PrimeNeedReceipt same_observation_join = same_observation_left;
    CHECK(prime_need_receipt_merge(
              &same_observation_join, &same_observation_right) &&
              prime_need_receipt_is_ancestor(
                  &same_observation_left, &same_observation_join) &&
              prime_need_receipt_is_ancestor(
                  &same_observation_right, &same_observation_join) &&
              prime_need_receipt_is_ancestor(
                  &same_observation_join, &same_observation_join),
          "equal cell observations have a native least joined receipt");
    CHECK(prime_need_receipt_event_count(&same_observation_join) == 3u,
          "native receipts retain equal observation occurrences before the functional quotient");
    PrimeNeedReceiptEvent joined_event_zero;
    PrimeNeedReceiptEvent joined_event_one;
    PrimeNeedReceiptEvent joined_event_two;
    CHECK(prime_need_receipt_event_at(
              &same_observation_join, 0u, &joined_event_zero) &&
              prime_need_receipt_event_at(
                  &same_observation_join, 1u, &joined_event_one) &&
              prime_need_receipt_event_at(
                  &same_observation_join, 2u, &joined_event_two) &&
              joined_event_zero.event_id != 0u &&
              joined_event_one.event_id != 0u &&
              joined_event_two.event_id != 0u &&
              joined_event_zero.event_id != joined_event_one.event_id &&
              joined_event_zero.event_id != joined_event_two.event_id &&
              joined_event_one.event_id != joined_event_two.event_id,
          "equal receipt payloads retain distinct nonzero occurrence IDs");

    PrimeNeedReceipt independent_left;
    PrimeNeedReceipt independent_right;
    CHECK(prime_need_receipt_observe_cell(
              &arena, &receipt_root, thunk.session_id, second_thunk_id,
              left_value, &independent_left),
          "first independent observation extends the common support");
    CHECK(prime_need_receipt_observe_cell(
              &arena, &receipt_root, thunk.session_id,
              second_thunk_id + 1u, right_value, &independent_right),
          "second independent observation extends the common support");
    CHECK(prime_need_receipt_compatible(
              &independent_left, &independent_right),
          "observations of different cells are compatible");
    PrimeNeedReceipt independent_join = independent_left;
    CHECK(prime_need_receipt_merge(
              &independent_join, &independent_right),
          "compatible receipts have a native join");
    CHECK(prime_need_receipt_is_ancestor(
              &independent_left, &independent_join) &&
              prime_need_receipt_is_ancestor(
                  &independent_right, &independent_join),
          "the receipt join contains both causal branches");
    CHECK(prime_need_receipt_event_count(&independent_join) == 3u,
          "the join is the occurrence-preserving union of common support");

    StateCell state = {
        .value = atom_int(&arena, 0),
        .content_type = atom_symbol(&arena, "Number"),
    };
    PrimeNeedReceipt state_read_left;
    PrimeNeedReceipt state_read_right;
    CHECK(prime_need_receipt_read_state(
              &arena, &receipt_root, &state, state.value,
              &state_read_left),
          "a state read is an explicit receipt event");
    CHECK(prime_need_receipt_read_state(
              &arena, &receipt_root, &state, state.value,
              &state_read_right),
          "a sibling can read the same parent state");
    CHECK(prime_need_receipt_compatible(
              &state_read_left, &state_read_right),
          "equal sibling reads are compatible");
    PrimeNeedReceipt read_join = state_read_left;
    CHECK(prime_need_receipt_merge(&read_join, &state_read_right),
          "equal sibling reads have a least joined receipt");

    PrimeNeedReceipt state_write_left;
    PrimeNeedReceipt state_write_right;
    Atom *one = atom_int(&arena, 1);
    Atom *two = atom_int(&arena, 2);
    CHECK(prime_need_receipt_write_state(
              &arena, &receipt_root, &state, state.value, one,
              &state_write_left),
          "a state write records before and after values");
    CHECK(prime_need_receipt_write_state(
              &arena, &receipt_root, &state, state.value, two,
              &state_write_right),
          "a sibling write is a distinct effect occurrence");
    CHECK(!prime_need_receipt_compatible(
              &state_write_left, &state_write_right),
          "sibling writes do not merge without an admitted effect law");

    PrimeNeedReceipt sequential_write;
    CHECK(prime_need_receipt_write_state(
              &arena, &state_read_left, &state, state.value, one,
              &sequential_write),
          "a causally later write extends its branch read");
    Atom *effective_state = NULL;
    CHECK(prime_need_receipt_state_value(
              &sequential_write, &state, &effective_state) &&
              atom_eq(effective_state, one),
          "branch lookup observes the latest causal state value");
    PrimeNeedReceipt promoted_receipt = independent_join;
    CHECK(prime_need_receipt_promote(
              &promoted_arena, &promoted_receipt) &&
              prime_need_receipt_equal(
                  &promoted_receipt, &independent_join) &&
              prime_need_receipt_is_ancestor(
                  &independent_join, &promoted_receipt) &&
              prime_need_receipt_is_ancestor(
                  &promoted_receipt, &independent_join) &&
              prime_need_receipt_excludes_arena(
                  &promoted_receipt, &arena) &&
              !prime_need_receipt_excludes_arena(
                  &promoted_receipt, &promoted_arena),
          "receipt promotion preserves arena-independent causal identity");
    const PrimeNeedReceiptFrame *same_owner_top = promoted_receipt.top;
    CHECK(prime_need_receipt_promote(
              &promoted_arena, &promoted_receipt) &&
              promoted_receipt.top == same_owner_top,
          "same-owner promotion preserves the persistent receipt DAG");
#endif

    Atom *context_x = atom_symbol(&arena, "x");
    Atom *context_y = atom_symbol(&arena, "y");
    Atom *context_one = atom_int(&arena, 1);
    Atom *context_two = atom_int(&arena, 2);
    Atom *context_x_one = atom_prime_context_bind(
        &arena, NULL, context_x, context_one);
    const CettaPrimeContext *context_x_one_value =
        atom_prime_context_value(context_x_one);
    CHECK(context_x_one_value &&
              atom_prime_context_depth(context_x_one_value) == 1u,
          "context binding allocates one immutable chain frame");
    CHECK(atom_eq(atom_prime_context_lookup(
                      context_x_one_value, atom_symbol(&arena, "x")),
                  context_one) &&
              !atom_prime_context_lookup(
                  context_x_one_value, atom_symbol(&arena, "missing")),
          "context lookup is structural and absence does not invent a value");
    Atom *context_xy = atom_prime_context_bind(
        &arena, context_x_one_value, context_y, context_two);
    const CettaPrimeContext *context_xy_value =
        atom_prime_context_value(context_xy);
    CHECK(context_xy_value &&
              atom_prime_context_depth(context_xy_value) == 2u &&
              atom_eq(atom_prime_context_lookup(context_xy_value, context_x),
                      context_one) &&
              atom_eq(atom_prime_context_lookup(context_xy_value, context_y),
                      context_two),
          "context extension shares its parent and preserves older bindings");
    Atom *context_shadow = atom_prime_context_bind(
        &arena, context_xy_value, context_x, context_two);
    const CettaPrimeContext *context_shadow_value =
        atom_prime_context_value(context_shadow);
    CHECK(context_shadow_value &&
              atom_eq(atom_prime_context_lookup(
                          context_shadow_value, context_x),
                      context_two) &&
              atom_eq(atom_prime_context_lookup(
                          context_x_one_value, context_x),
                      context_one),
          "newest context binding shadows without mutating its parent");
    Atom *context_copy = atom_deep_copy(&promoted_arena, context_shadow);
    CHECK(context_copy && atom_eq(context_copy, context_shadow),
          "context promotion preserves the semantic chain");
    CHECK(!atom_eq(context_shadow, context_xy),
          "different context extensions remain distinguishable values");

    printf("(PrimeNeedAlgebraSummary %u %u %u)\n",
           checks, checks - failures, failures);
    arena_free(&promoted_arena);
    arena_free(&branch_arena);
    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return failures == 0u ? 0 : 1;
}

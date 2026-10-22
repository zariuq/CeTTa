#include "atom.h"
#include "match.h"
#include "binding/slot_store_internal.h"
#include "tests/test_runtime_stats_stubs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failures;
#define CHECK(c, m) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL: %s\n", m); } } while (0)

static CettaFrameIdentity fresh(void) {
    CettaFrameIdentity result;
    if (!cetta_frame_identity_acquire(&result)) abort();
    return result;
}

static void prepare(Bindings *bindings, Arena *arena, CettaFrameIdentity epoch) {
    VarId ids[] = {var_id_from_base(1u), var_id_from_base(2u)};
    Atom *variables[] = {atom_var_with_name_key(arena,
                            atom_expr2(arena, atom_symbol(arena, "relocation-name"),
                                       atom_string(arena, "first")), var_id_from_base(1u)),
                         atom_var_with_id(arena, "relocation-second", var_id_from_base(2u))};
    BindingsFrameSchema *schema = bindings_frame_schema_new_presented(ids, variables, 2u);
    bindings_init(bindings);
    CHECK(schema && bindings_register_complete_frame_schema(bindings, schema, epoch),
          "register a presented complete frame");
    bindings_frame_schema_release(schema);
    CHECK(bindings_add_id(bindings, var_epoch_id(var_id_from_base(1u), epoch), atom_symbol_current_id(variables[0]),
                         atom_string(arena, "first value")),
          "publish a foreign slot value");
}

static BindingsFrameIndexEntry *frame(Bindings *bindings, CettaFrameIdentity epoch) {
    return bindings_frame_index_find_frame(bindings->frame_index, epoch);
}

static void shared_images(void) {
    Arena source, destination;
    arena_init(&source); arena_init(&destination);
    CettaFrameIdentity epoch = fresh();
    Bindings original, first, second;
    prepare(&original, &source, epoch);
    bindings_init(&first); bindings_init(&second);
    CHECK(bindings_copy(&first, &original) && bindings_copy(&second, &original),
          "two images retain the original backing");
    CHECK(bindings_frame_index_detach(&second), "detach only a view directory");
    frame(&second, epoch)->activation_write_boundary = 37u;
    frame(&second, epoch)->has_activation_write_boundary = true;
    BindingsFrameIndexEntry before_first = *frame(&first, epoch);
    BindingsFrameIndexEntry before_second = *frame(&second, epoch);
    Atom *old_value = before_first.values[0].skeleton;
    AtomDeepCopySession *session = atom_deep_copy_session_new(&destination);
    CHECK(bindings_promote_logical_atoms_with_session(&first, session) &&
          bindings_promote_logical_atoms_with_session(&second, session),
          "promote separately named binding images in one episode");
    BindingsFrameIndexEntry *left = frame(&first, epoch), *right = frame(&second, epoch);
    CHECK(left->storage_references == right->storage_references && left->schema == right->schema,
          "aliased source backing has one shared relocation image");
    CHECK(left->storage_references != before_first.storage_references &&
          left->values[0].skeleton != old_value,
          "foreign source payload is not overwritten or borrowed");
    CHECK(left->schema != before_first.schema &&
          left->schema->name_keys[0] != before_first.schema->name_keys[0] &&
          atom_eq(left->schema->name_keys[0], before_first.schema->name_keys[0]),
          "structured schema names move once with their payload");
    CHECK(frame(&original, epoch)->values[0].skeleton == old_value,
          "an unpromoted sibling still observes the original value");
    CHECK(cetta_frame_identity_eq(left->epoch, before_first.epoch) && left->owns_identity == before_first.owns_identity &&
          left->bound_value_count == before_first.bound_value_count &&
          left->private_value_count == before_first.private_value_count &&
          left->slot_len == before_first.slot_len && left->schema_complete == before_first.schema_complete &&
          left->has_activation_write_boundary == before_first.has_activation_write_boundary &&
          left->activation_write_boundary == before_first.activation_write_boundary &&
          right->activation_write_boundary == before_second.activation_write_boundary &&
          right->has_activation_write_boundary == before_second.has_activation_write_boundary,
          "per-view identities and write boundaries survive shared relocation");
    CHECK(left->write_version[0] == before_first.write_version[0] &&
          first.frame_index->write_clock == original.frame_index->write_clock &&
          binding_value_kind(left->values[0]) == binding_value_kind(before_first.values[0]) &&
          cetta_frame_identity_eq(binding_value_epoch(left->values[0]), binding_value_epoch(before_first.values[0])),
          "relocation preserves versions and value context");
    CHECK(bindings_add_id(&first, var_epoch_id(var_id_from_base(2u), epoch), 0u,
                         atom_string(&destination, "branch write")),
          "a relocated image remains writable through copy-on-write");
    CHECK(frame(&first, epoch)->storage_references != frame(&second, epoch)->storage_references &&
          !bindings_lookup_value_id(&second, var_epoch_id(var_id_from_base(2u), epoch)).skeleton &&
          !bindings_lookup_value_id(&original, var_epoch_id(var_id_from_base(2u), epoch)).skeleton,
          "a branch write does not leak into either sibling");
    atom_deep_copy_session_free(session);
    CHECK(atomic_load(frame(&second, epoch)->storage_references) == 1u,
          "session teardown releases its temporary image hold");
    bindings_free(&original); arena_free(&source);
    CHECK(atom_eq(bindings_lookup_value_id(&second, var_epoch_id(var_id_from_base(1u), epoch)).skeleton,
                  atom_string(&destination, "first value")),
          "promoted values outlive the original arena");
    bindings_free(&first); bindings_free(&second);
    arena_free(&destination); cetta_frame_identity_release(epoch);
    CHECK(!cetta_frame_identity_retain(epoch), "all relocation identity holds are released");
}

static void image_outlives_first_view(void) {
    Arena source, destination;
    arena_init(&source); arena_init(&destination);
    CettaFrameIdentity epoch = fresh();
    Bindings first, second;
    prepare(&first, &source, epoch); bindings_init(&second);
    CHECK(bindings_copy(&second, &first), "retain a delayed second image");
    AtomDeepCopySession *session = atom_deep_copy_session_new(&destination);
    CHECK(bindings_promote_logical_atoms_with_session(&first, session), "promote the first view");
    _Atomic uint32_t *image = frame(&first, epoch)->storage_references;
    bindings_free(&first);
    CHECK(bindings_promote_logical_atoms_with_session(&second, session) &&
          frame(&second, epoch)->storage_references == image,
          "a later alias can reuse the image after the first view is freed");
    atom_deep_copy_session_free(session);
    bindings_free(&second); arena_free(&source); arena_free(&destination);
    cetta_frame_identity_release(epoch);
}

static void exclusive_image_stays_in_place(void) {
    Arena source, destination;
    arena_init(&source); arena_init(&destination);
    CettaFrameIdentity epoch = fresh();
    Bindings original;
    prepare(&original, &source, epoch);
    _Atomic uint32_t *payload = frame(&original, epoch)->storage_references;
    uint64_t detaches = test_runtime_stats_counter(CETTA_RUNTIME_COUNTER_BINDINGS_FRAME_STORAGE_DETACH);
    AtomDeepCopySession *session = atom_deep_copy_session_new(&destination);
    CHECK(bindings_promote_logical_atoms_with_session(&original, session),
          "promote an exclusively owned frame");
    CHECK(frame(&original, epoch)->storage_references == payload &&
          test_runtime_stats_counter(CETTA_RUNTIME_COUNTER_BINDINGS_FRAME_STORAGE_DETACH) == detaches,
          "exclusive payload promotion does not manufacture a copy-on-write detach");
    atom_deep_copy_session_free(session);
    arena_free(&source);
    CHECK(atom_eq(frame(&original, epoch)->values[0].skeleton,
                  atom_string(&destination, "first value")), "in-place promotion still owns its copied value");
    bindings_free(&original); arena_free(&destination); cetta_frame_identity_release(epoch);
}

static void distinct_occurrences_and_episodes(void) {
    Arena source, destination, later;
    arena_init(&source); arena_init(&destination); arena_init(&later);
    CettaFrameIdentity epoch = fresh();
    Bindings first, independent, delayed;
    prepare(&first, &source, epoch); prepare(&independent, &source, epoch);
    bindings_init(&delayed); CHECK(bindings_copy(&delayed, &first), "retain another episode's source");
    AtomDeepCopySession *session = atom_deep_copy_session_new(&destination);
    CHECK(bindings_promote_logical_atoms_with_session(&first, session) &&
          bindings_promote_logical_atoms_with_session(&independent, session),
          "promote equal-valued independently allocated frames");
    CHECK(frame(&first, epoch)->storage_references != frame(&independent, epoch)->storage_references,
          "equal values do not merge distinct backing occurrences");
    atom_deep_copy_session_free(session);
    session = atom_deep_copy_session_new(&later);
    CHECK(bindings_promote_logical_atoms_with_session(&delayed, session) &&
          frame(&delayed, epoch)->storage_references != frame(&first, epoch)->storage_references &&
          frame(&delayed, epoch)->values[0].skeleton != frame(&first, epoch)->values[0].skeleton,
          "a separate episode and destination cannot reuse stale relocation images");
    atom_deep_copy_session_free(session);
    bindings_free(&first); bindings_free(&independent); bindings_free(&delayed);
    arena_free(&source); arena_free(&destination); arena_free(&later);
    cetta_frame_identity_release(epoch);
}

static Atom *refuse_leaf(Arena *arena, Atom *atom, void *context) {
    bool refuse_names = *(bool *)context;
    if (atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_STRING &&
        (refuse_names || (atom->ground.slen == sizeof("first value") - 1u &&
                         memcmp(atom->ground.sval, "first value", sizeof("first value") - 1u) == 0)))
        return NULL;
    return atom_deep_copy_single(arena, atom);
}

static void failed_episode(bool refuse_names) {
    Arena source, destination;
    arena_init(&source); arena_init(&destination);
    CettaFrameIdentity epoch = fresh();
    Bindings first, alias;
    prepare(&first, &source, epoch); bindings_init(&alias);
    CHECK(bindings_copy(&alias, &first), "retain source for a failed mapped episode");
    AtomDeepCopySession *session = atom_deep_copy_session_new_mapped(&destination, refuse_leaf, &refuse_names);
    CHECK(!bindings_promote_logical_atoms_with_session(&first, session) &&
          atom_deep_copy_session_failed(session), "a refused schema or value leaf fails the episode");
    CHECK(!bindings_promote_logical_atoms_with_session(&alias, session),
          "a failed session cannot publish a later alias");
    atom_deep_copy_session_free(session);
    bindings_free(&first); bindings_free(&alias); arena_free(&source); arena_free(&destination);
    cetta_frame_identity_release(epoch);
    CHECK(!cetta_frame_identity_retain(epoch), "failure releases inventory identity holds");
}

static void cached_image_does_not_clear_failure(void) {
    Arena source, destination;
    arena_init(&source); arena_init(&destination);
    CettaFrameIdentity epoch = fresh();
    Bindings first, alias;
    prepare(&first, &source, epoch); bindings_init(&alias);
    CHECK(bindings_copy(&alias, &first), "retain a source with a completed cache entry");
    AtomDeepCopySession *session = atom_deep_copy_session_new(&destination);
    CHECK(bindings_promote_logical_atoms_with_session(&first, session), "establish a completed image");
    CHECK(!atom_deep_copy_session_copy(session, NULL), "a later graph failure poisons the episode");
    CHECK(!bindings_promote_logical_atoms_with_session(&alias, session),
          "a completed inventory entry cannot hide subsequent session failure");
    atom_deep_copy_session_free(session);
    bindings_free(&first); bindings_free(&alias); arena_free(&source); arena_free(&destination);
    cetta_frame_identity_release(epoch);
}

static void schema_only_relocation(void) {
    Arena source, destination;
    arena_init(&source); arena_init(&destination);
    CettaFrameIdentity epoch = fresh();
    Bindings first, alias;
    prepare(&first, &source, epoch);
    frame(&first, epoch)->values[0].skeleton = atom_string(&destination, "destination value");
    bindings_init(&alias); CHECK(bindings_copy(&alias, &first), "retain a schema-only foreign image");
    _Atomic uint32_t *payload = frame(&first, epoch)->storage_references;
    AtomDeepCopySession *session = atom_deep_copy_session_new(&destination);
    CHECK(bindings_promote_logical_atoms_with_session(&first, session) &&
          bindings_promote_logical_atoms_with_session(&alias, session), "promote only foreign name keys");
    CHECK(frame(&first, epoch)->storage_references == payload &&
          frame(&alias, epoch)->storage_references == payload &&
          frame(&first, epoch)->schema == frame(&alias, epoch)->schema,
          "schema-only relocation preserves already safe slot sharing");
    atom_deep_copy_session_free(session);
    arena_free(&source);
    CHECK(atom_eq(frame(&alias, epoch)->schema->name_keys[0],
                  atom_expr2(&destination, atom_symbol(&destination, "relocation-name"),
                             atom_string(&destination, "first"))), "copied structured schema outlives its arena");
    bindings_free(&first); bindings_free(&alias); arena_free(&destination);
    cetta_frame_identity_release(epoch);
}

static void inventory_growth(void) {
    enum { COUNT = 96 };
    Arena source, destination;
    arena_init(&source); arena_init(&destination);
    Bindings originals[COUNT], images[COUNT];
    CettaFrameIdentity epochs[COUNT];
    AtomDeepCopySession *session = atom_deep_copy_session_new(&destination);
    for (unsigned i = 0; i < COUNT; i++) {
        epochs[i] = fresh(); prepare(&originals[i], &source, epochs[i]);
        bindings_init(&images[i]);
        CHECK(bindings_copy(&images[i], &originals[i]) &&
              bindings_promote_logical_atoms_with_session(&images[i], session),
              "grow the inventory with independently retained sources");
    }
    for (unsigned i = 0; i < COUNT; i++) {
        CHECK(bindings_promote_logical_atoms_with_session(&originals[i], session) &&
              frame(&originals[i], epochs[i])->storage_references == frame(&images[i], epochs[i])->storage_references,
              "rehashing retains every source-to-image correspondence");
    }
    atom_deep_copy_session_free(session);
    for (unsigned i = 0; i < COUNT; i++) {
        bindings_free(&originals[i]); bindings_free(&images[i]); cetta_frame_identity_release(epochs[i]);
    }
    arena_free(&source); arena_free(&destination);
}

static void identical_snapshot_merge(void) {
    Arena arena; arena_init(&arena);
    CettaFrameIdentity epoch = fresh(), context_epoch = fresh();
    Bindings original, alias, oracle, different;
    prepare(&original, &arena, epoch);
    bindings_init(&alias); bindings_init(&oracle); bindings_init(&different);
    BindingValue contextual = binding_value_from_context(
        atom_expr2(&arena, atom_symbol(&arena, "Contexted"),
                   atom_var_with_id(&arena, "scope-other", var_id_from_base(17u))), context_epoch);
    CHECK(bindings_rewrite_value_id(&original, var_epoch_id(var_id_from_base(1u), epoch), contextual),
          "publish a contextual value before copying its complete snapshot");
    CHECK(bindings_copy(&alias, &original) && bindings_copy(&oracle, &original),
          "retain an identical live snapshot and a general-merge oracle");
    CHECK(alias.frame_index == original.frame_index && alias.owners == original.owners,
          "the fast case is physical snapshot and owner identity");
    BindingsFrameIndex *directory = original.frame_index;
    BindingsFrameIndexEntry before = *frame(&original, epoch);
    uint64_t clock = original.frame_index->write_clock;
    CHECK(bindings_try_merge(&oracle, &alias), "general merge accepts the identical snapshot");
    CHECK(bindings_try_merge_live(&original, &alias), "live merge accepts the identical snapshot");
    CHECK(original.frame_index == directory &&
          original.frame_index->write_clock == clock &&
          frame(&original, epoch)->storage_references == before.storage_references &&
          frame(&original, epoch)->write_version[0] == before.write_version[0],
          "identical merge does not manufacture storage or a write");
    CHECK(frame(&original, epoch)->values[0].skeleton == contextual.skeleton &&
          binding_value_kind(frame(&original, epoch)->values[0]) == binding_value_kind(contextual) &&
          cetta_frame_identity_eq(binding_value_epoch(frame(&original, epoch)->values[0]), binding_value_epoch(contextual)) &&
          atom_eq(bindings_to_atom(&arena, &original), bindings_to_atom(&arena, &oracle)),
          "the untouched value context agrees with general merge observation");
    CHECK(bindings_copy(&different, &alias) &&
          bindings_add_id(&different, var_id_from_base(33u), 0u, atom_int(&arena, 54)),
          "add a generic row without changing the shared framed directory");
    CHECK(different.frame_index == alias.frame_index && different.len == 1u,
          "matching framed backing does not establish whole-image identity");
    CHECK(bindings_try_merge_live(&original, &different) &&
          bindings_lookup_value_id(&original, var_id_from_base(33u)).skeleton &&
          bindings_lookup_value_id(&original, var_id_from_base(33u)).skeleton->ground.ival == 54,
          "the general path retains additional unframed bindings");
    bindings_free(&different); bindings_init(&different);
    CHECK(bindings_copy(&different, &alias) &&
          bindings_add_id(&different, var_epoch_id(var_id_from_base(2u), epoch), 0u, atom_string(&arena, "new value")),
          "prepare a different snapshot with an additional authoritative binding");
    CHECK(different.frame_index != original.frame_index &&
          bindings_try_merge_live(&original, &different) &&
          atom_eq(frame(&original, epoch)->values[1].skeleton, atom_string(&arena, "new value")),
          "a different directory is not skipped as an identical image");
    bindings_free(&different); bindings_init(&different);
    CHECK(bindings_copy(&different, &alias) &&
          bindings_add_id(&different, var_epoch_id(var_id_from_base(2u), epoch), 0u, atom_string(&arena, "conflict")),
          "prepare a genuinely conflicting snapshot");
    CHECK(!bindings_try_merge_live(&original, &different) &&
          atom_eq(frame(&original, epoch)->values[1].skeleton, atom_string(&arena, "new value")),
          "the general conflict path refuses without overwriting the destination");
    bindings_free(&original); bindings_free(&alias); bindings_free(&oracle); bindings_free(&different);
    arena_free(&arena); cetta_frame_identity_release(epoch); cetta_frame_identity_release(context_epoch);
}

int main(void) {
    SymbolTable symbols; symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms); g_symbols = &symbols; g_hashcons = NULL;
    VarInternTable vars; var_intern_init(&vars); g_var_intern = &vars;
    shared_images(); image_outlives_first_view(); exclusive_image_stays_in_place();
    distinct_occurrences_and_episodes();
    failed_episode(false); failed_episode(true);
    cached_image_does_not_clear_failure(); schema_only_relocation(); inventory_growth();
    identical_snapshot_merge();
    bindings_thread_cache_free(); g_var_intern = NULL; var_intern_free(&vars);
    g_symbols = NULL; symbol_table_free(&symbols);
    printf("BindingFrameRelocation checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}

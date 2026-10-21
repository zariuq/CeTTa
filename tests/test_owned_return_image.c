#include "owned_return_image.h"
#include "shared_return_producer.h"
#include "parser.h"
#include "prepared_pure_machine.h"
#include "term_canon.h"
#include "library.h"
#include "native_handle.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned refuse_calloc;
void *__real_calloc(size_t count, size_t size);
void *__wrap_calloc(size_t count, size_t size) {
    if (refuse_calloc && !--refuse_calloc)
        return NULL;
    return __real_calloc(count, size);
}

static Atom *parse(Arena *arena, const char *text) {
    size_t position = 0u;
    Atom *term = parse_sexpr(arena, text, &position);
    assert(term);
    return term;
}

static void assert_int(Atom *atom, int64_t expected) {
    assert(atom && atom->kind == ATOM_GROUNDED &&
           atom->ground.gkind == GV_INT && atom->ground.ival == expected);
}

static void test_logical_roots(void) {
    Arena source, observation;
    arena_init_detached(&source);
    arena_init_detached(&observation);
    Atom *packet = cetta_instantiate_frame_syntax(&source,
        parse(&source, "(packet $x $y $unused (closure $y $x $x) (check $y))"));
    assert(packet);
    Atom *x = packet->expr.elems[1], *y = packet->expr.elems[2];
    Atom *unused = packet->expr.elems[3];
    VarId xid = x->var_id, yid = y->var_id, unused_id = unused->var_id;
    BindingsBuilder builder;
    assert(bindings_builder_init(&builder, NULL));
    assert(bindings_builder_add_var_fresh(&builder, x, y));
    assert(bindings_builder_add_var_fresh(&builder, y, atom_int(&source, 7)));
    assert(bindings_builder_add_var_fresh(&builder, unused, atom_int(&source, 29)));
    Bindings bindings;
    bindings_builder_take(&builder, &bindings);
    bindings_builder_free(&builder);
    Atom *left = atom_expr3(&source, atom_symbol(&source, "left"), x,
                           atom_var_with_id(&source, "constraint-only-u", fresh_var_id()));
    Atom *right = atom_expr3(&source, atom_symbol(&source, "right"), y,
                            atom_var_with_id(&source, "constraint-only-v", fresh_var_id()));
    Atom *third = atom_expr3(&source, atom_symbol(&source, "third"), y,
                            atom_var_with_id(&source, "constraint-only-w", fresh_var_id()));
    assert(bindings_add_constraint(&bindings, left, right));
    assert(bindings_add_constraint(&bindings, left, right));
    assert(bindings_add_constraint(&bindings, left, third));
    size_t count = 0u;
    assert(bindings_current_binding_count(&bindings, &count) && count == 3u);
    CettaOwnedReturn returned = {
        .outcome = cetta_call_suspended(packet->expr.elems[4], packet->expr.elems[5]),
        .environment = &bindings,
    };
    CettaOwnedReturnImage *image = cetta_owned_return_image_capture(&returned, NULL);
    assert(image && cetta_owned_return_image_syntax_bytes(image) > 0u);
    const CettaOwnedReturn *view = cetta_owned_return_image_view(image);
    assert(view->outcome.kind == CETTA_CALL_SUSPENDED && view->environment);
    assert(bindings_eq((Bindings *)view->environment, &bindings));
    Atom *cx = view->outcome.term->expr.elems[2];
    Atom *cy = view->outcome.term->expr.elems[1];
    assert(var_id_eq(cx->var_id, xid) && var_id_eq(cy->var_id, yid));
    assert(view->outcome.term->expr.elems[3] == cx);
    assert(view->outcome.delayed->expr.elems[1] == cy);
    assert(bindings_lookup_value_id((Bindings *)view->environment, xid).skeleton == cy);
    assert(view->environment->eq_len == 2u &&
           view->environment->constraints[0].lhs.skeleton->expr.elems[1] == cx &&
           view->environment->constraints[1].rhs.skeleton->expr.elems[1] == cy &&
           view->environment->constraints[0].lhs.skeleton ==
               view->environment->constraints[1].lhs.skeleton);
    assert(cetta_owned_return_image_retain(image));
    cetta_owned_return_image_release(image);
    bindings_free(&bindings);
    arena_free(&source);
    assert(bindings_current_binding_count(view->environment, &count) && count == 3u);
    assert_int(bindings_apply((Bindings *)view->environment, &observation, cx), 7);
    assert_int(bindings_apply((Bindings *)view->environment, &observation, cy), 7);
    assert_int(bindings_lookup_value_id((Bindings *)view->environment,
                                       unused_id).skeleton, 29);
    arena_free(&observation);
    cetta_owned_return_image_release(image);
    puts("PASS: contextual identities, aliases, live constraint-only roots, delayed roots and full bindings survive source destruction");
}

static void test_provenance_and_scalars(void) {
    Arena source, observation;
    arena_init_detached(&source);
    arena_init_detached(&observation);
    Atom *data = parse(&source, "(Error (println! must-stay-data) 3.5 True \"text\")");
    CettaCallOutcome outcomes[] = {
        cetta_call_value(data), cetta_call_completed_value(data),
        cetta_call_completed_frontier(atom_expr2(&source, data, data)),
        cetta_call_raised(data), cetta_call_failure(), cetta_call_interrupted(),
        cetta_call_suspended(data, data),
    };
    CettaOwnedReturnImage *images[sizeof(outcomes) / sizeof(outcomes[0])];
    for (size_t i = 0u; i < sizeof(images) / sizeof(images[0]); ++i) {
        CettaOwnedReturn returned = {.outcome = outcomes[i], .final = i % 2u != 0u};
        images[i] = cetta_owned_return_image_capture(&returned, NULL);
        assert(images[i]);
        const CettaOwnedReturn *view = cetta_owned_return_image_view(images[i]);
        assert(view->outcome.kind == outcomes[i].kind &&
               view->outcome.result_form == outcomes[i].result_form &&
               view->final == returned.final &&
               !view->environment);
        assert(!outcomes[i].term || atom_eq(view->outcome.term, outcomes[i].term));
        if (i == 6u)
            assert(view->outcome.term == view->outcome.delayed);
    }
    arena_free(&source);
    for (size_t i = 0u; i < sizeof(images) / sizeof(images[0]); ++i) {
        const CettaOwnedReturn *view = cetta_owned_return_image_view(images[i]);
        if (view->outcome.term) {
            Atom *copy = atom_deep_copy(&observation, view->outcome.term);
            assert(copy && atom_eq(copy, view->outcome.term));
        }
        cetta_owned_return_image_release(images[i]);
    }
    arena_free(&observation);
    assert(!cetta_owned_return_image_capture(NULL, NULL));
    puts("PASS: return kinds and completed-answer provenance remain explicit, including code-shaped data");
}

static void test_unregistered_symbol_image(void) {
    Arena source;
    arena_init_detached(&source);
    Atom *first = atom_symbol_data(&source, "return-image-unregistered-a");
    Atom *other = atom_symbol_data(&source, "return-image-unregistered-b");
#if CETTA_MANAGED_SYMBOLS
    assert(atom_symbol_current_id(first) == SYMBOL_ID_NONE);
    assert(atom_symbol_current_id(other) == SYMBOL_ID_NONE);
#endif
    CettaOwnedReturn returned = {
        .outcome = cetta_call_completed_value(atom_expr3(&source, first, other, first)),
    };
    CettaOwnedReturnImage *image = cetta_owned_return_image_capture(&returned, NULL);
    assert(image);
    const CettaOwnedReturn *view = cetta_owned_return_image_view(image);
    assert(atom_eq(view->outcome.term, returned.outcome.term));
    assert(view->outcome.term->expr.elems[0] == view->outcome.term->expr.elems[2]);
    arena_free(&source);
    Atom *copy = view->outcome.term;
    assert(atom_symbol_equals_cstr(copy->expr.elems[0], "return-image-unregistered-a"));
    assert(atom_symbol_equals_cstr(copy->expr.elems[1], "return-image-unregistered-b"));
    assert(!atom_symbols_equal(copy->expr.elems[0], copy->expr.elems[1]));
#if CETTA_MANAGED_SYMBOLS
    assert(atom_symbol_current_id(copy->expr.elems[0]) == SYMBOL_ID_NONE);
    assert(atom_symbol_current_id(copy->expr.elems[1]) == SYMBOL_ID_NONE);
#endif
    cetta_owned_return_image_release(image);
    puts("PASS: distinct unregistered spellings and repeated symbol aliases survive owned-image source destruction");
}

static void test_explicit_contextual_carrier(void) {
    Arena source, observation;
    arena_init_detached(&source);
    arena_init_detached(&observation);
    CettaFrameIdentity epoch;
    assert(cetta_frame_identity_acquire(&epoch));
    Atom *x = atom_var_with_id(&source, "explicit-x", fresh_var_id());
    Atom *y = atom_var_with_id(&source, "explicit-y", var_id_from_base(1u));
    Atom *skeleton = atom_expr2(&source, atom_symbol(&source, "Box"), y);
    Bindings bindings;
    bindings_init(&bindings);
    VarId ids[] = {var_id_from_base(1u)};
    assert(bindings_register_complete_contextual_frame(&bindings, ids, 1u, epoch));
    assert(match_binding_values(binding_value_from_atom(x),
        binding_value_from_context(skeleton, epoch), &bindings, &source));
    BindingValue value = bindings_lookup_value_id(&bindings, x->var_id);
    assert(binding_value_kind(value) == BINDING_VALUE_CONTEXTUAL && cetta_frame_identity_eq(binding_value_epoch(value), epoch) &&
           value.skeleton == skeleton);
    Atom *qualified_y = atom_var_with_id(&source, "explicit-y", var_epoch_id(y->var_id, epoch));
    assert(bindings_add_var(&bindings, qualified_y, atom_int(&source, 71)));
    CettaOwnedReturn returned = {
        .outcome = cetta_call_completed_value(x), .environment = &bindings,
    };
    CettaOwnedReturnImage *image = cetta_owned_return_image_capture(&returned, NULL);
    assert(image);
    VarId xid = x->var_id;
    bindings_free(&bindings);
    arena_free(&source);
    cetta_frame_identity_release(epoch);
    const CettaOwnedReturn *view = cetta_owned_return_image_view(image);
    value = bindings_lookup_value_id((Bindings *)view->environment, xid);
    assert(binding_value_kind(value) == BINDING_VALUE_CONTEXTUAL && cetta_frame_identity_eq(binding_value_epoch(value), epoch));
    Atom *applied = bindings_apply((Bindings *)view->environment, &observation, view->outcome.term);
    assert(applied && applied->kind == ATOM_EXPR && applied->expr.len == 2u);
    assert_int(applied->expr.elems[1], 71);
    arena_free(&observation);
    cetta_owned_return_image_release(image);
    puts("PASS: unresolved contextual skeletons retain their separate epoch and frame namespace after source destruction");
}

typedef struct {
    unsigned references, retained, released, destroyed;
    Arena syntax;
    TermUniverse universe;
    Space space;
    StateCell state;
    CaptureClosure capture;
} Resources;

static void retain_resources(void *context) {
    Resources *resources = context;
    assert(resources->references);
    ++resources->references;
    ++resources->retained;
}

static void release_resources(void *context) {
    Resources *resources = context;
    assert(resources->references);
    ++resources->released;
    if (--resources->references)
        return;
    ++resources->destroyed;
    space_free(&resources->space);
    term_universe_free(&resources->universe);
    arena_free(&resources->syntax);
}

static void init_resources(Resources *resources) {
    *resources = (Resources){.references = 1u};
    arena_init_detached(&resources->syntax);
    term_universe_init(&resources->universe);
    term_universe_set_persistent_arena(&resources->universe, &resources->syntax);
    space_init_with_universe(&resources->space, &resources->universe);
    assert(space_admit_atom(&resources->space, &resources->syntax,
                           parse(&resources->syntax, "(retained fact)")));
    resources->state = (StateCell){
        .value = atom_int(&resources->syntax, 41),
        .content_type = atom_symbol(&resources->syntax, "Number"),
    };
    resources->capture = (CaptureClosure){.space_ptr = &resources->space};
}

typedef struct {
    Resources *resources;
    const CettaBindingsValue *saved;
    bool shareable;
} ResourcePolicy;

static bool describe_owner(void *context, const Atom *leaf,
                           CettaReturnLeafOwner *out) {
    ResourcePolicy *policy = context;
    if (!policy->shareable)
        return false;
    Resources *resources = policy->resources;
    if (resources &&
        ((leaf->ground.gkind == GV_SPACE && leaf->ground.ptr == &resources->space) ||
         (leaf->ground.gkind == GV_STATE && leaf->ground.ptr == &resources->state) ||
         (leaf->ground.gkind == GV_CAPTURE && leaf->ground.ptr == &resources->capture))) {
        *out = (CettaReturnLeafOwner){
            .owner = resources, .retain = retain_resources, .release = release_resources,
        };
        return true;
    }
    /* This fixture explicitly qualifies the one saved ordinary substitution
     * it constructed. Other saved substitutions may hide services or rights. */
    if (policy->saved && leaf->ground.gkind == GV_BINDINGS && leaf->ground.ptr == policy->saved) {
        *out = (CettaReturnLeafOwner){
            .owner = (void *)policy->saved,
            .retain = policy->saved->retain, .release = policy->saved->release,
        };
        return true;
    }
    return false;
}

typedef struct {
    CettaLibraryContext context;
    size_t references, retained, released, destroyed, finalized;
    int payload;
} NativeResource;

static void retain_native_resource(void *raw) {
    NativeResource *resource = raw;
    assert(resource->references);
    ++resource->references;
    ++resource->retained;
}

static void release_native_resource(void *raw) {
    NativeResource *resource = raw;
    assert(resource->references);
    ++resource->released;
    if (--resource->references)
        return;
    cetta_native_handle_cleanup_all(&resource->context);
    ++resource->destroyed;
}

static void finalize_native_resource(void *raw) {
    NativeResource *resource = raw;
    ++resource->finalized;
}

typedef struct {
    NativeResource *resource;
    const Atom *identifier;
    bool shareable;
    size_t calls;
} NativeResourcePolicy;

static bool describe_native_resource(void *raw, const Atom *leaf,
                                    CettaReturnLeafOwner *out) {
    NativeResourcePolicy *policy = raw;
    ++policy->calls;
    if (!policy->shareable || leaf != policy->identifier)
        return false;
    *out = (CettaReturnLeafOwner){
        .owner = policy->resource,
        .retain = retain_native_resource,
        .release = release_native_resource,
    };
    return true;
}

static void test_native_identifier_resources(void) {
    NativeResource *resource = calloc(1u, sizeof(*resource));
    assert(resource);
    resource->references = 1u;
    resource->payload = 47;
    resource->context.native_handle_next_id = 1u;
    uint64_t id = 0u;
    assert(cetta_native_handle_alloc(&resource->context, "ReturnImage", resource,
                                    finalize_native_resource, &id));
    Arena source;
    arena_init_detached(&source);
    Atom *handle = cetta_native_handle_owned_atom(
        &resource->context, &source, "ReturnImage", id);
    assert(handle && cetta_native_handle_get_owned(
        &resource->context, "ReturnImage", handle) == resource);
    Atom *identifier = handle->expr.elems[2];
    assert(identifier->ground.gkind == GV_INT &&
        (identifier->structural_facts & ATOM_STRUCTURAL_HAS_NATIVE_HANDLE_ID));
    Atom *ordinary = atom_int(&source, identifier->ground.ival);
    CettaOwnedReturn plain = {.outcome = cetta_call_value(ordinary)};
    CettaOwnedReturnImage *plain_image = cetta_owned_return_image_capture(&plain, NULL);
    assert(plain_image && !resource->retained);
    cetta_owned_return_image_release(plain_image);

    Atom *hidden = atom_var_with_id(&source, "hidden-native-resource", fresh_var_id());
    Bindings bindings;
    bindings_init(&bindings);
    assert(bindings_add_var(&bindings, hidden, identifier));
    Atom *named = atom_var_with_id(&source, "native-resource-name", fresh_var_id());
    named->name_key = identifier;
    CettaOwnedReturn returns[] = {
        {.outcome = cetta_call_value(identifier)},
        {.outcome = cetta_call_value(handle)},
        {.outcome = cetta_call_suspended(ordinary, identifier)},
        {.outcome = cetta_call_value(ordinary), .environment = &bindings},
        {.outcome = cetta_call_value(named)},
    };
    NativeResourcePolicy descriptor = {.resource = resource, .identifier = identifier};
    CettaOwnedReturnImagePolicy policy = {
        .describe_leaf_owner = describe_native_resource, .context = &descriptor,
    };
    for (size_t i = 0u; i < sizeof(returns) / sizeof(returns[0]); ++i) {
        assert(!cetta_owned_return_image_capture(&returns[i], NULL));
        assert(!cetta_owned_return_image_capture(&returns[i], &policy));
        assert(resource->references == 1u && !resource->retained && !resource->finalized);
    }
    assert(descriptor.calls == 5u);
    descriptor.shareable = true;

    /* Failure after retaining the first root releases both temporary holds;
     * the source's live holder keeps its original registry slot. */
    StateCell state = {.value = ordinary};
    CettaOwnedReturn partial = {
        .outcome = cetta_call_suspended(handle, atom_state(&source, &state)),
    };
    assert(!cetta_owned_return_image_capture(&partial, &policy));
    assert(resource->references == 1u && resource->retained == 1u &&
           resource->released == 1u && !resource->finalized &&
           cetta_native_handle_get_owned(&resource->context, "ReturnImage", handle) == resource);

    CettaOwnedReturn shared = {
        .outcome = cetta_call_suspended(handle, identifier), .environment = &bindings,
    };
    CettaOwnedReturnImage *first = cetta_owned_return_image_capture(&shared, &policy);
    CettaOwnedReturnImage *second = cetta_owned_return_image_capture(&shared, &policy);
    assert(first && second && resource->references == 3u && resource->retained == 3u);
    const CettaOwnedReturn *a = cetta_owned_return_image_view(first);
    const CettaOwnedReturn *b = cetta_owned_return_image_view(second);
    assert(a->outcome.term->expr.elems[2] == a->outcome.delayed &&
           b->outcome.term->expr.elems[2] == b->outcome.delayed);
    assert(bindings_lookup_value_id((Bindings *)a->environment,
                                   hidden->var_id).skeleton == a->outcome.delayed);
    Atom *forged = cetta_native_handle_atom(&source, "ReturnImage", id);
    assert(!cetta_native_handle_get_owned(&resource->context, "ReturnImage", forged));
    bindings_free(&bindings);
    arena_free(&source);
    release_native_resource(resource);
    assert(resource->references == 2u && !resource->destroyed && !resource->finalized);
    assert(cetta_native_handle_get_owned(&resource->context, "ReturnImage",
                                        a->outcome.term) == resource);
    assert(cetta_native_handle_get_owned(&resource->context, "ReturnImage",
                                        b->outcome.term) == resource);
    CettaNativeHandleUse exclusive = {0}, conflict = {0};
    assert(cetta_native_handle_acquire_owned(&resource->context, "ReturnImage",
        a->outcome.term, CETTA_NATIVE_HANDLE_USE_EXCLUSIVE, &exclusive));
    assert(!cetta_native_handle_acquire_owned(&resource->context, "ReturnImage",
        b->outcome.term, CETTA_NATIVE_HANDLE_USE_EXCLUSIVE, &conflict));
    assert(!conflict.owner && cetta_native_handle_use_is_current(&exclusive));
    cetta_owned_return_image_release(second);
    assert(resource->references == 1u && !resource->destroyed &&
           cetta_native_handle_use_resource(&exclusive) == resource);
    assert(cetta_native_handle_close(&resource->context, "ReturnImage", id));
    assert(!cetta_native_handle_use_is_current(&exclusive) &&
           !cetta_native_handle_get_owned(&resource->context, "ReturnImage", a->outcome.term) &&
           cetta_native_handle_use_resource(&exclusive) == resource && !resource->finalized);
    assert(cetta_native_handle_use_release(&exclusive) && resource->finalized == 1u);
    cetta_owned_return_image_release(first);
    assert(!resource->references && resource->destroyed == 1u &&
           resource->released == 4u && resource->finalized == 1u);
    free(resource);
    puts("PASS: owning integer identifiers require explicit resource qualification through every return root; lifetime sharing preserves revocation and exclusive use");
}

static void test_name_keys_and_resources(void) {
    Resources *resources = calloc(1u, sizeof(*resources));
    assert(resources);
    init_resources(resources);
    Arena source;
    arena_init_detached(&source);
    Atom *space = atom_space(&source, &resources->space);
    Atom *state = atom_state(&source, &resources->state);
    Atom *capture = atom_capture(&source, &resources->capture);
    Atom *named = atom_var_with_name_key(&source, space, fresh_var_id());
    CettaOwnedReturn hidden = {.outcome = cetta_call_completed_value(named)};
    assert(!cetta_owned_return_image_capture(&hidden, NULL));
    ResourcePolicy resource_policy = {.resources = resources};
    CettaOwnedReturnImagePolicy policy = {
        .describe_leaf_owner = describe_owner, .context = &resource_policy,
    };
    assert(!cetta_owned_return_image_capture(&hidden, &policy));
    resource_policy.shareable = true;
    CettaOwnedReturn returned = {
        .outcome = cetta_call_suspended(atom_expr3(&source, named, state, capture), space),
    };
    CettaOwnedReturnImage *image = cetta_owned_return_image_capture(&returned, &policy);
    assert(image && resources->retained == 1u && resources->references == 2u);
    const CettaOwnedReturn *view = cetta_owned_return_image_view(image);
    Atom *copied_name = view->outcome.term->expr.elems[0];
    assert(copied_name != named && copied_name->name_key == view->outcome.delayed);
    assert(var_id_eq(copied_name->var_id, named->var_id));
    /* A later unqualified leaf must release every earlier tentative hold. */
    Atom *unqualified = atom_state(&source, &(StateCell){0});
    CettaOwnedReturn failure = {
        .outcome = cetta_call_completed_value(atom_expr2(&source, space, unqualified)),
    };
    CettaSharedReturnRow row = {.returned = &hidden};
    assert(!cetta_shared_return_capture_image(&policy, &failure, &row));
    assert(row.returned == &hidden && !row.owner && !row.release);
    assert(resources->references == 2u && resources->retained == 2u && resources->released == 1u);
    arena_free(&source);
    release_resources(resources);
    assert(!resources->destroyed && resources->references == 1u);
    Space *held_space = copied_name->name_key->ground.ptr;
    assert(atom_eq(space_get_at(held_space, 0u),
                   parse(&resources->syntax, "(retained fact)")));
    StateCell *held_state = view->outcome.term->expr.elems[1]->ground.ptr;
    assert_int(held_state->value, 41);
    CaptureClosure *held_capture = view->outcome.term->expr.elems[2]->ground.ptr;
    assert(held_capture->space_ptr == held_space);
    cetta_owned_return_image_release(image);
    assert(resources->destroyed == 1u && resources->references == 0u &&
           resources->released == 3u);
    free(resources);
    puts("PASS: hidden name-key resources require permission; qualified space, state and capture lifetimes survive their source owner");
}

static void test_frame_schema_resource_root(void) {
    Resources *resources = calloc(1u, sizeof(*resources));
    assert(resources);
    init_resources(resources);
    Arena source;
    arena_init_detached(&source);
    CettaFrameIdentity epoch;
    assert(cetta_frame_identity_acquire(&epoch));
    Atom *variable = atom_var_with_name_key(&source,
        atom_space(&source, &resources->space), var_id_from_base(1u));
    VarId ids[] = {var_id_from_base(1u)};
    Atom *variables[] = {variable};
    BindingsFrameSchema *schema = bindings_frame_schema_new_presented(ids, variables, 1u);
    assert(schema);
    Bindings bindings;
    bindings_init(&bindings);
    assert(bindings_register_complete_frame_schema(&bindings, schema, epoch));
    Atom *slot = atom_var_with_name_key(&source, variable->name_key, var_epoch_id(var_id_from_base(1u), epoch));
    assert(bindings_add_var(&bindings, slot, atom_int(&source, 11)) && bindings.len == 0u);
    CettaOwnedReturn returned = {
        .outcome = cetta_call_completed_value(atom_int(&source, 5)), .environment = &bindings,
    };
    assert(!cetta_owned_return_image_capture(&returned, NULL));
    ResourcePolicy resource_policy = {.resources = resources, .shareable = true};
    CettaOwnedReturnImagePolicy policy = {
        .describe_leaf_owner = describe_owner, .context = &resource_policy,
    };
    CettaOwnedReturnImage *image = cetta_owned_return_image_capture(&returned, &policy);
    assert(image && resources->retained == 1u);
    bindings_frame_schema_release(schema);
    bindings_free(&bindings);
    arena_free(&source);
    cetta_frame_identity_release(epoch);
    release_resources(resources);
    const CettaOwnedReturn *view = cetta_owned_return_image_view(image);
    BindingsIterator iterator = {.bindings = view->environment};
    Binding retained;
    assert(bindings_iterator_next(&iterator, &retained));
    assert(var_id_eq(retained.var_id, var_epoch_id(var_id_from_base(1u), epoch)) && retained.name_key &&
           retained.name_key->ground.gkind == GV_SPACE &&
           retained.name_key->ground.ptr == &resources->space);
    assert_int(retained.value.skeleton, 11);
    assert(!bindings_iterator_next(&iterator, &retained) && !resources->destroyed);
    cetta_owned_return_image_release(image);
    assert(resources->destroyed == 1u);
    free(resources);
    puts("PASS: a resource reachable only through a contextual frame schema is qualified and independently retained");
}

static void test_deep_name_keys_and_refusal(void) {
    Arena source;
    arena_init_detached(&source);
    Atom *term = atom_int(&source, 73);
    const size_t depth = 100000u;
    for (size_t i = 0u; i < depth; ++i)
        term = atom_var_with_name_key(&source, term, fresh_var_id());
    CettaOwnedReturn returned = {.outcome = cetta_call_completed_value(term)};
    refuse_calloc = 1u;
    assert(!cetta_owned_return_image_capture(&returned, NULL) &&
           !refuse_calloc && returned.outcome.term == term);
    CettaOwnedReturnImage *image = cetta_owned_return_image_capture(&returned, NULL);
    assert(image);
    arena_free(&source);
    term = cetta_owned_return_image_view(image)->outcome.term;
    for (size_t i = 0u; i < depth; ++i) {
        assert(term && term->kind == ATOM_VAR && term->name_key);
        term = term->name_key;
    }
    assert_int(term, 73);
    cetta_owned_return_image_release(image);
    arena_init_detached(&source);
    Bindings prime;
    bindings_init(&prime);
    assert(bindings_need_mut(&prime) && prime.prime_ext);
    StateCell cell = {.value = atom_int(&source, 0)};
    PrimeNeedBranchState base, state;
    assert(prime_need_branch_state_begin(&source, &base));
    assert(prime_need_branch_state_write(&source, &base, &cell,
        cell.value, atom_int(&source, 1), &state));
    *bindings_branch_state_mut(&prime) = state;
    returned = (CettaOwnedReturn){.outcome = cetta_call_failure(), .environment = &prime};
    assert(!cetta_owned_return_image_capture(&returned, NULL));
    assert(bindings_branch_state_view(&prime)->top == state.top && cell.value->ground.ival == 0);
    bindings_free(&prime);
    arena_free(&source);
    puts("PASS: 100000 structured-name links use explicit work; allocation and unqualified mutable-state refusal preserve the loan");
}

static void test_distinct_prime_worlds(void) {
    Arena source;
    arena_init_detached(&source);
    PrimeNeedSnapshot root, allocated, evaluating, pending;
    prime_need_snapshot_init(&root);
    assert(prime_need_snapshot_begin(&root));
    Atom *origin = parse(&source, "(source-computation)");
    uint64_t id = 0u, pending_id = 0u;
    assert(prime_need_snapshot_allocate(&source, &root, origin, &allocated, &id));
    assert(prime_need_snapshot_allocate(&source, &allocated,
        parse(&source, "(println! must-remain-pending)"), &pending, &pending_id));
    uint64_t evaluator = prime_need_fresh_evaluator_id();
    assert(prime_need_snapshot_start_evaluation(&source, &pending, id,
        evaluator, &evaluating));
    Atom *variable = atom_var_with_id(&source, "retained-world", fresh_var_id());
    Bindings worlds[2];
    CettaOwnedReturnImage *images[2];
    uint64_t factors_ids[2], handlers[2];
    for (unsigned index = 0u; index < 2u; ++index) {
        bindings_init(&worlds[index]);
        Atom *number = atom_int(&source, index ? 11 : 7);
        Atom *cached = atom_expr3(&source, atom_symbol(&source, "Pair"), number, number);
        CettaWeightLedger factors, claimed, selected;
        factors_ids[index] = prime_need_fresh_source_occurrence();
        handlers[index] = prime_need_fresh_source_occurrence();
        assert(cetta_weight_ledger_append(&source, bindings_weight_view(&worlds[index]),
            factors_ids[index], 0u, cached, &factors));
        assert(cetta_weight_ledger_handle(&source, &factors,
            bindings_weight_view(&worlds[index]), handlers[index], &claimed, &selected));
        *bindings_weight_mut(&worlds[index]) = claimed;
        cached = factors.last->value;
        assert(prime_need_snapshot_resolve_ready_value(&source, &evaluating,
            id, evaluator, cached, bindings_need_mut(&worlds[index])));
        assert(bindings_add_var(&worlds[index], variable, cached));
        CettaOwnedReturn returned = {
            .outcome = cetta_call_completed_value(atom_int(&source, 999)),
            .environment = &worlds[index], .final = index == 1u,
        };
        images[index] = cetta_owned_return_image_capture(&returned, NULL);
        assert(images[index]);
    }
    VarId variable_id = variable->var_id;
    bindings_free(&worlds[0]);
    bindings_free(&worlds[1]);
    arena_free(&source);
    for (unsigned index = 0u; index < 2u; ++index) {
        const CettaOwnedReturn *view = cetta_owned_return_image_view(images[index]);
        assert_int(view->outcome.term, 999);
        assert(view->final == (index == 1u));
        assert(bindings_need_view(view->environment)->owner != &source &&
            bindings_weight_view(view->environment)->owner != &source);
        PrimeNeedCellView cached, unevaluated;
        assert(prime_need_snapshot_lookup(bindings_need_view(view->environment), id, &cached));
        assert(cached.cache_state == PRIME_NEED_CACHE_VALUE &&
               cached.value_role == PRIME_NEED_VALUE_READY);
        assert(cached.cached->expr.elems[1] == cached.cached->expr.elems[2]);
        assert_int(cached.cached->expr.elems[1], index ? 11 : 7);
        assert(bindings_lookup_value_id((Bindings *)view->environment,
            variable_id).skeleton == cached.cached);
        assert(bindings_weight_view(view->environment)->last->value == cached.cached);
        assert(bindings_weight_view(view->environment)->claim_count == 1u &&
            cetta_weight_ledger_handler(bindings_weight_view(view->environment),
                factors_ids[index]) == handlers[index]);
        assert(prime_need_snapshot_lookup(bindings_need_view(view->environment),
            pending_id, &unevaluated));
        assert(unevaluated.cache_state == PRIME_NEED_CACHE_EMPTY &&
               atom_symbol_equals_cstr(unevaluated.origin->expr.elems[0], "println!"));
    }
    PrimeNeedSnapshot mixed = *bindings_need_view(
        cetta_owned_return_image_view(images[0])->environment);
    assert(!prime_need_snapshot_merge(&mixed, bindings_need_view(
        cetta_owned_return_image_view(images[1])->environment)));
    cetta_owned_return_image_release(images[0]);
    PrimeNeedCellView survivor;
    assert(prime_need_snapshot_lookup(bindings_need_view(
        cetta_owned_return_image_view(images[1])->environment), id, &survivor));
    assert_int(survivor.cached->expr.elems[1], 11);
    cetta_owned_return_image_release(images[1]);
    puts("PASS: equal returned data retains distinct immutable Prime worlds, aliases, pending cells and factor roots after source destruction");
}

#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
static void test_recording_requires_its_own_transport(void) {
    Arena source;
    arena_init_detached(&source);
    Bindings recorded;
    bindings_init(&recorded);
    PrimeNeedRecording *group = prime_need_recording_new(
        (PrimeNeedRecordingLimits){2u, 8192u, 2u, 1u});
    assert(group && prime_need_recording_reserve(group, NULL, 1u, 8192u,
        bindings_receipt_mut(&recorded)));
    Atom *value = atom_int(&source, 9);
    CettaWeightLedger factor;
    assert(cetta_weight_ledger_append(&source, bindings_weight_view(&recorded),
        41u, 71u, value, &factor));
    *bindings_weight_mut(&recorded) = factor;
    value = factor.last->value;
    assert(prime_need_receipt_observe_cell(&source, bindings_receipt_view(&recorded),
        71u, 1u, value, bindings_receipt_mut(&recorded)));
    PrimeNeedSnapshot root = {0}, allocated = {0};
    uint64_t thunk = 0u;
    assert(prime_need_snapshot_begin(&root) &&
        prime_need_snapshot_allocate(&source, &root, value, &allocated, &thunk));
    *bindings_need_mut(&recorded) = allocated;
    PrimeNeedRecordingStatus before = prime_need_recording_status(group);
    PrimeNeedReceipt receipt = *bindings_receipt_view(&recorded);
    CettaOwnedReturn returned = {
        .outcome = cetta_call_completed_value(value),
        .environment = &recorded,
    };
    assert(!cetta_owned_return_image_capture(&returned, NULL));
    PrimeNeedRecordingStatus after = prime_need_recording_status(group);
    assert(bindings_receipt_view(&recorded)->recording == receipt.recording &&
        before.owner_links == after.owner_links && before.omitted == after.omitted &&
        before.nodes == after.nodes && before.reserved_nodes == after.reserved_nodes &&
        before.payload_charge == after.payload_charge &&
        before.reserved_payload_bytes == after.reserved_payload_bytes &&
        before.metadata_bound == after.metadata_bound &&
        before.retained_bound == after.retained_bound &&
        before.incomplete == after.incomplete && before.overflow == after.overflow);
    CettaOwnedReturnImagePolicy retain = {.bounded_history=CETTA_RETURN_HISTORY_RETAIN};
    CettaOwnedReturnImage *kept = cetta_owned_return_image_capture(&returned, &retain);
    assert(kept && prime_need_recording_status(group).owner_links == 1u);
    const PrimeNeedReceipt *reader = bindings_receipt_view(
        cetta_owned_return_image_view(kept)->environment);
    assert(reader->top == receipt.top && prime_need_recording_readable(reader));
    assert(!cetta_owned_return_image_capture(&returned, &retain));
    assert(bindings_receipt_view(&recorded)->top == receipt.top &&
        prime_need_recording_status(group).owner_links == 1u &&
        prime_need_recording_status(group).omitted > before.omitted);
    CettaOwnedReturnImagePolicy forget = {.bounded_history=CETTA_RETURN_HISTORY_FORGET};
    CettaOwnedReturnImage *erased = cetta_owned_return_image_capture(&returned, &forget);
    assert(erased && !bindings_receipt_view(
        cetta_owned_return_image_view(erased)->environment)->recording);
    assert(prime_need_recording_readable(bindings_receipt_view(&recorded)));
    prime_need_recording_forget(group);
    assert(!prime_need_recording_readable(bindings_receipt_view(&recorded)) &&
        prime_need_recording_readable(reader));
    bindings_free(&recorded);
    arena_free(&source);
    prime_need_recording_release(group);
    CettaOwnedReturnImage *images[] = {kept, erased};
    for (size_t i=0u; i<2u; i++) {
        const CettaOwnedReturn *view = cetta_owned_return_image_view(images[i]);
        assert_int(view->outcome.term, 9);
        PrimeNeedCellView pending;
        assert(prime_need_snapshot_lookup(bindings_need_view(view->environment), thunk, &pending));
        assert(pending.origin == view->outcome.term && pending.cache_state == PRIME_NEED_CACHE_EMPTY);
        assert(bindings_weight_view(view->environment)->length == 1u &&
            bindings_weight_view(view->environment)->last->value == view->outcome.term);
    }
    PrimeNeedReceiptGraph graph = {0};
    assert(prime_need_receipt_graph_collect(reader, &graph) == PRIME_NEED_RECEIPT_GRAPH_INCOMPLETE);
    assert(graph.length == 1u && graph.nodes[0].event.after->ground.ival == 9);
    prime_need_receipt_graph_free(&graph);
    cetta_owned_return_image_release(kept);
    cetta_owned_return_image_release(erased);
    puts("PASS: explicit history retention and forgetting preserve operational worlds; quota refusal keeps the loan and retained readers survive source retirement");
}
#endif

typedef struct {
    CettaOwnedReturnImagePolicy policy;
    ResourcePolicy resources;
    unsigned next_calls, effects, released;
} ProducerAccount;

typedef struct {
    ProducerAccount *account;
    Arena syntax;
    Bindings bindings[2];
    CettaOwnedReturn returns[2];
    unsigned emitted;
} Computation;

static CettaOwnedExecutionStep next_return(void *context, CettaOwnedReturn *returned,
                                          CettaEvalCompletion *completion) {
    Computation *computation = context;
    ++computation->account->next_calls;
    if (computation->emitted == 3u) {
        *completion = CETTA_EVAL_COMPLETE;
        return CETTA_OWNED_EXECUTION_COMPLETE;
    }
    /* One effect immediately precedes each return. Capture refusal must keep
     * this pending return rather than executing that effect again. */
    ++computation->account->effects;
    *returned = computation->returns[computation->emitted == 2u ? 1u : 0u];
    ++computation->emitted;
    return CETTA_OWNED_EXECUTION_RETURN;
}

static void release_computation(void *context) {
    Computation *computation = context;
    ++computation->account->released;
    bindings_free(&computation->bindings[0]);
    bindings_free(&computation->bindings[1]);
    arena_free(&computation->syntax);
    free(computation);
}

static bool always_compatible(void *context) {
    (void)context;
    return true;
}

typedef struct {
    CettaOwnedReturnImage *answers[4];
    uint64_t positions[4];
    size_t count;
    bool refuse;
    void (*on_accept)(void *context);
    void *on_accept_context;
} Recipient;

static bool retain_answer(void *context, uint64_t position,
                          const CettaOwnedReturn *returned) {
    Recipient *recipient = context;
    if (recipient->refuse)
        return false;
    assert(recipient->count < 4u);
    CettaOwnedReturnImage *image = cetta_owned_return_image_from_view(returned);
    assert(cetta_owned_return_image_retain(image));
    recipient->answers[recipient->count] = image;
    recipient->positions[recipient->count++] = position;
    if (recipient->on_accept)
        recipient->on_accept(recipient->on_accept_context);
    return true;
}

static void release_answers(Recipient *recipient) {
    for (size_t i = 0u; i < recipient->count; ++i)
        cetta_owned_return_image_release(recipient->answers[i]);
    recipient->count = 0u;
}

static void test_native_resource_shared_return(void) {
    NativeResource *resource = calloc(1u, sizeof(*resource));
    Computation *computation = calloc(1u, sizeof(*computation));
    assert(resource && computation);
    resource->references = 1u;
    resource->context.native_handle_next_id = 1u;
    uint64_t id = 0u;
    assert(cetta_native_handle_alloc(&resource->context, "SharedReturn", resource,
                                    finalize_native_resource, &id));
    ProducerAccount account = {0};
    computation->account = &account;
    arena_init_detached(&computation->syntax);
    Atom *handle = cetta_native_handle_owned_atom(
        &resource->context, &computation->syntax, "SharedReturn", id);
    assert(handle);
    NativeResourcePolicy descriptor = {
        .resource = resource, .identifier = handle->expr.elems[2],
    };
    account.policy = (CettaOwnedReturnImagePolicy){
        .describe_leaf_owner = describe_native_resource, .context = &descriptor,
    };
    for (size_t i = 0u; i < 2u; ++i) {
        bindings_init(&computation->bindings[i]);
        computation->returns[i] = (CettaOwnedReturn){
            .outcome = cetta_call_completed_value(handle),
            .environment = &computation->bindings[i],
        };
    }
    CettaOwnedExecution execution = {0};
    cetta_owned_execution_init(&execution, computation, next_return, release_computation);
    execution.relocatable = true;
    CettaSharedReturnProducer *producer = cetta_shared_return_producer_new(
        &execution, 2u, 1u, cetta_shared_return_capture_image, always_compatible, &account);
    assert(producer && !execution.computation);
    assert(cetta_shared_return_producer_advance(producer, 1u) == CETTA_SHARED_RETURN_CAPTURE_REFUSED);
    assert(account.effects == 1u && account.next_calls == 1u &&
           cetta_shared_return_producer_execution(producer)->has_pending &&
           !resource->retained);
    descriptor.shareable = true;
    assert(cetta_shared_return_producer_advance(producer, 1u) == CETTA_SHARED_RETURN_PAUSED);
    assert(account.effects == 1u && account.next_calls == 1u && resource->retained == 1u);
    Recipient a = {0}, b = {.refuse = true};
    assert(cetta_shared_return_producer_take(producer, 0u, retain_answer, &a).kind ==
           CETTA_SHARED_DELIVERY_ANSWER);
    assert(cetta_shared_return_producer_take(producer, 1u, retain_answer, &b).kind ==
           CETTA_SHARED_DELIVERY_PUBLICATION_REFUSED);
    assert(cetta_shared_return_producer_cancel(producer, 1u));
    assert(cetta_shared_return_producer_trim(producer) == 1u);
    assert(a.count == 1u && !b.count && resource->references == 2u);
    cetta_shared_return_producer_free(producer);
    assert(account.released == 1u && account.effects == 1u && !resource->finalized);
    release_native_resource(resource);
    const CettaOwnedReturn *returned = cetta_owned_return_image_view(a.answers[0]);
    assert(returned->outcome.result_form == CETTA_CALL_RESULT_COMPLETED_VALUE &&
           cetta_native_handle_get_owned(&resource->context, "SharedReturn",
                                        returned->outcome.term) == resource &&
           resource->references == 1u && !resource->destroyed);
    release_answers(&a);
    assert(!resource->references && resource->destroyed == 1u && resource->finalized == 1u);
    free(resource);
    puts("PASS: native resource qualification refusal keeps the paid source loan; retry, independent cancellation and producer retirement preserve the retained value");
}

static void test_saved_environment_and_consumers(void) {
    ProducerAccount account = {0};
    account.policy = (CettaOwnedReturnImagePolicy){
        .describe_leaf_owner = describe_owner, .context = &account.resources,
    };
    Computation *computation = calloc(1u, sizeof(*computation));
    assert(computation);
    computation->account = &account;
    arena_init_detached(&computation->syntax);
    Atom *body = cetta_instantiate_frame_syntax(&computation->syntax,
        parse(&computation->syntax, "(closure $x $y $x)"));
    Atom *x = body->expr.elems[1], *y = body->expr.elems[2];
    for (size_t i = 0u; i < 2u; ++i) {
        bindings_init(&computation->bindings[i]);
        assert(bindings_add_var(&computation->bindings[i], x, y));
        assert(bindings_add_var(&computation->bindings[i], y,
                               atom_int(&computation->syntax, i ? 9 : 7)));
    }
    Atom *saved = bindings_capture_value(&computation->syntax, &computation->bindings[0]);
    assert(saved);
    account.resources.saved = saved->ground.ptr;
    for (size_t i = 0u; i < 2u; ++i)
        computation->returns[i] = (CettaOwnedReturn){
            .outcome = cetta_call_suspended(atom_expr2(&computation->syntax, body, saved),
                                           atom_expr2(&computation->syntax, atom_symbol(&computation->syntax, "check"), y)),
            .environment = &computation->bindings[i],
        };
    CettaOwnedExecution execution = {0};
    cetta_owned_execution_init(&execution, computation, next_return, release_computation);
    execution.relocatable = true;
    CettaSharedReturnProducer *producer = cetta_shared_return_producer_new(
        &execution, 2u, 2u, cetta_shared_return_capture_image, always_compatible, &account);
    assert(producer && !execution.computation);
    assert(cetta_shared_return_producer_advance(producer, 1u) == CETTA_SHARED_RETURN_CAPTURE_REFUSED);
    assert(account.effects == 1u && account.next_calls == 1u &&
           cetta_shared_return_producer_execution(producer)->has_pending);
    account.resources.shareable = true;
    assert(cetta_shared_return_producer_advance(producer, 2u) == CETTA_SHARED_RETURN_PAUSED);
    assert(account.effects == 2u && account.next_calls == 2u);
    Recipient a = {0}, b = {.refuse = true};
    assert(cetta_shared_return_producer_take(producer, 0u, retain_answer, &a).kind == CETTA_SHARED_DELIVERY_ANSWER);
    assert(cetta_shared_return_producer_take(producer, 1u, retain_answer, &b).kind == CETTA_SHARED_DELIVERY_PUBLICATION_REFUSED);
    assert(cetta_shared_return_producer_cancel(producer, 0u));
    b.refuse = false;
    assert(cetta_shared_return_producer_take(producer, 1u, retain_answer, &b).kind == CETTA_SHARED_DELIVERY_ANSWER);
    assert(cetta_shared_return_producer_trim(producer) == 1u);
    assert(cetta_shared_return_producer_advance(producer, 1u) == CETTA_SHARED_RETURN_PAUSED);
    assert(account.effects == 3u);
    for (size_t i = 1u; i < 3u; ++i) {
        assert(cetta_shared_return_producer_take(producer, 1u, retain_answer, &b).kind == CETTA_SHARED_DELIVERY_ANSWER);
        assert(b.positions[i] == i);
        assert(cetta_shared_return_producer_trim(producer) == 1u);
    }
    assert(cetta_shared_return_producer_advance(producer, 1u) == CETTA_SHARED_RETURN_COMPLETE);
    cetta_shared_return_producer_free(producer);
    assert(account.released == 1u && account.effects == 3u && b.count == 3u);
    Arena observation;
    arena_init_detached(&observation);
    for (size_t i = 0u; i < b.count; ++i) {
        const CettaOwnedReturn *view = cetta_owned_return_image_view(b.answers[i]);
        Atom *closure = view->outcome.term->expr.elems[0];
        Atom *cx = closure->expr.elems[1], *cy = closure->expr.elems[2];
        assert(cx == closure->expr.elems[3] && cy == view->outcome.delayed->expr.elems[1]);
        assert_int(bindings_apply((Bindings *)view->environment, &observation, cx), i == 2u ? 9 : 7);
        assert_int(bindings_apply((Bindings *)view->environment, &observation, cy), i == 2u ? 9 : 7);
        assert_int(bindings_apply_saved(&observation, view->outcome.term->expr.elems[1], cy), 7);
    }
    assert(a.answers[0] == b.answers[0]);
    release_answers(&a);
    /* A's cancellation and final release cannot invalidate B's saved owner. */
    const CettaOwnedReturn *remaining = cetta_owned_return_image_view(b.answers[0]);
    assert_int(bindings_apply_saved(&observation, remaining->outcome.term->expr.elems[1],
                                   remaining->outcome.delayed->expr.elems[1]), 7);
    arena_free(&observation);
    release_answers(&b);
    puts("PASS: real saved substitution, correlated alternatives, duplicate occurrences and effect-before-refusal survive independent cancellation and producer destruction");
}

typedef struct {
    CettaOwnedReturnImagePolicy policy;
    Space *space;
    SpaceProgramToken token;
} ProgramContext;

static bool program_compatible(void *context) {
    ProgramContext *program = context;
    return space_program_token_matches_live_space(program->token, program->space);
}

typedef struct {
    Space *space;
    Arena *source;
} ProgramChange;

static void change_program(void *context) {
    ProgramChange *change = context;
    assert(space_admit_atom(change->space, change->source,
                           parse(change->source, "(= (pick Nil) changed)")));
}

static void test_prepared_search_returns(bool change_during_publication) {
    Arena source, observation;
    arena_init_detached(&source);
    arena_init_detached(&observation);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &source);
    Space space;
    space_init_with_universe(&space, &universe);
    assert(space_admit_atom(&space, &source,
        parse(&source, "(= (pick (Cons $x $xs)) $x)")));
    assert(space_admit_atom(&space, &source,
        parse(&source, "(= (pick (Cons $x (Cons $y $ys))) (pick (Cons $y $ys)))")));
    CettaPreparedPureProgram *program = cetta_prepared_pure_program_compile_closed_answers(
        &space, parse(&source, "(pick (Cons (pair 7 7) (Cons (pair 7 7) (Cons (pair 9 9) Nil))))"),
        CETTA_GSLT_PURE_CALL_EAGER, atom_bool, atom_expr,
        cetta_prepared_pure_constructs_expression_always,
        NULL, NULL, NULL, NULL, NULL, true, true,
        (CettaMatchDecisionSemanticIdentity){
            .compiler_identity = cetta_match_decision_compiler_identity(),
        });
    assert(program);
    CettaPreparedPureAnswerCursorOptions options = {
        .limits = {.max_transitions = 1024u, .max_scratch_bytes = 1024u * 1024u,
                   .construct_allocation_bound = atom_expr_allocation_bound},
        .unmatched_call = CETTA_PREPARED_PURE_UNMATCHED_FAILS,
        .allow_continuations = true,
    };
    CettaPreparedPureAnswerCursor *cursor = cetta_prepared_pure_answer_cursor_open(program, &options);
    assert(cursor);
    ProgramContext context = {
        .space = &space, .token = cetta_prepared_pure_answer_cursor_program_token(cursor),
    };
    ProgramChange change = {.space = &space, .source = &source};
    CettaOwnedExecution execution = {0};
    assert(cetta_prepared_pure_answer_cursor_own_execution(&execution, cursor));
    CettaSharedReturnProducer *producer = cetta_shared_return_producer_new(
        &execution, 2u, 2u, cetta_shared_return_capture_image, program_compatible, &context);
    assert(producer);
    Recipient a = {0}, b = {0};
    assert(cetta_shared_return_producer_advance(producer, 2u) == CETTA_SHARED_RETURN_PAUSED);
    assert(cetta_shared_return_producer_advance(producer, 1u) == CETTA_SHARED_RETURN_BACKPRESSURE);
    assert(cetta_shared_return_producer_take(producer, 0u, retain_answer, &a).kind == CETTA_SHARED_DELIVERY_ANSWER);
    assert(cetta_shared_return_producer_cancel(producer, 0u));
    assert(cetta_shared_return_producer_take(producer, 1u, retain_answer, &b).kind == CETTA_SHARED_DELIVERY_ANSWER);
    assert(cetta_shared_return_producer_trim(producer) == 1u);
    assert(space_admit_atom(&space, &source, parse(&source, "(unrelated fact)")));
    assert(program_compatible(&context));
    assert(cetta_shared_return_producer_advance(producer, 1u) == CETTA_SHARED_RETURN_PAUSED);
    for (size_t i = 1u; i < 3u; ++i) {
        if (change_during_publication && i == 2u) {
            b.on_accept = change_program;
            b.on_accept_context = &change;
        }
        assert(cetta_shared_return_producer_take(producer, 1u, retain_answer, &b).kind == CETTA_SHARED_DELIVERY_ANSWER);
        assert(b.positions[i] == i && cetta_shared_return_producer_trim(producer) == 1u);
    }
    if (change_during_publication) {
        assert(!program_compatible(&context));
        uint64_t position = UINT64_MAX;
        assert(cetta_occurrence_log_position(cetta_shared_return_producer_log(producer),
                                             1u, &position) && position == 3u);
        assert(cetta_shared_return_producer_advance(producer, 1u) == CETTA_SHARED_RETURN_CONTEXT_REFUSED);
    } else {
        assert(cetta_shared_return_producer_advance(producer, 1u) == CETTA_SHARED_RETURN_COMPLETE);
        change_program(&change);
    }
    assert(cetta_shared_return_producer_peek(producer, 1u).kind == CETTA_SHARED_DELIVERY_CONTEXT_REFUSED);
    cetta_shared_return_producer_free(producer);
    cetta_prepared_pure_program_free(program);
    space_free(&space);
    term_universe_free(&universe);
    arena_free(&source);
    for (size_t i = 0u; i < b.count; ++i) {
        const CettaOwnedReturn *view = cetta_owned_return_image_view(b.answers[i]);
        assert(view->outcome.kind == CETTA_CALL_VALUE &&
               view->outcome.result_form == CETTA_CALL_RESULT_COMPLETED_VALUE &&
               !view->environment);
        assert(atom_eq(view->outcome.term,
                       parse(&observation, i == 2u ? "(pair 9 9)" : "(pair 7 7)")));
    }
    release_answers(&a);
    assert(atom_eq(cetta_owned_return_image_view(b.answers[0])->outcome.term,
                   parse(&observation, "(pair 7 7)")));
    release_answers(&b);
    arena_free(&observation);
    puts(change_during_publication
        ? "PASS: publication accepts at its validated entrance; a source change during acceptance refuses the next delivery and resumption"
        : "PASS: actual prepared search uses the common image adapter, bounded delivery and dependency checks; retained answers outlive the cursor and program");
}

int main(void) {
    SymbolTable symbols;
    VarInternTable variables;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    var_intern_init(&variables);
    g_var_intern = &variables;
    test_logical_roots();
    test_provenance_and_scalars();
    test_unregistered_symbol_image();
    test_explicit_contextual_carrier();
    test_native_identifier_resources();
    test_name_keys_and_resources();
    test_frame_schema_resource_root();
    test_deep_name_keys_and_refusal();
    test_distinct_prime_worlds();
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    test_recording_requires_its_own_transport();
#endif
    test_saved_environment_and_consumers();
    test_native_resource_shared_return();
    test_prepared_search_returns(false);
    test_prepared_search_returns(true);
    symbol_table_free(&symbols);
    var_intern_free(&variables);
    g_symbols = NULL;
    g_var_intern = NULL;
    return 0;
}

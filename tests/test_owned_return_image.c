#include "owned_return_image.h"
#include "shared_return_producer.h"
#include "parser.h"
#include "prepared_pure_machine.h"
#include "term_canon.h"

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
        CettaOwnedReturn returned = {.outcome = outcomes[i]};
        images[i] = cetta_owned_return_image_capture(&returned, NULL);
        assert(images[i]);
        const CettaOwnedReturn *view = cetta_owned_return_image_view(images[i]);
        assert(view->outcome.kind == outcomes[i].kind &&
               view->outcome.result_form == outcomes[i].result_form &&
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
    Bindings prime;
    bindings_init(&prime);
    assert(bindings_need_mut(&prime) && prime.prime_ext);
    returned = (CettaOwnedReturn){.outcome = cetta_call_failure(), .environment = &prime};
    assert(!cetta_owned_return_image_capture(&returned, NULL));
    bindings_free(&prime);
    puts("PASS: 100000 structured-name links use explicit work; allocation and unqualified Prime-state refusal preserve the loan");
}

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
    CettaOwnedExecution execution;
    cetta_owned_execution_init(&execution, computation, next_return, release_computation);
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
        }, NULL);
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
    CettaOwnedExecution execution;
    cetta_prepared_pure_answer_cursor_own_execution(&execution, cursor);
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
    test_explicit_contextual_carrier();
    test_name_keys_and_resources();
    test_frame_schema_resource_root();
    test_deep_name_keys_and_refusal();
    test_saved_environment_and_consumers();
    test_prepared_search_returns(false);
    test_prepared_search_returns(true);
    symbol_table_free(&symbols);
    var_intern_free(&variables);
    g_symbols = NULL;
    g_var_intern = NULL;
    return 0;
}

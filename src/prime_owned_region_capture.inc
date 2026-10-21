/* Capture one ordinary native world and a finite structural DAG. The
 * immutable profile declines computed heads; the private-data profile
 * checks their resolved operations at invocation. Both refuse opaque leaves.
 * The caller keeps every source live and quiescent throughout this function;
 * concurrent source mutation and process-global shutdown are not admitted. */
typedef struct {
    Atom *source, *image;
    AtomId stored;
    size_t edge;
    unsigned char state;
    bool value, executable;
} PrimeNativeRegionNode;

typedef struct {
    PrimeNativeOwnedRegion *region;
    PrimeNativeRegionNode *nodes;
    size_t *stack;
    EvalNodeMemo index;
    size_t length, capacity, depth;
    bool value, executable;
    CettaContinuationStatus status;
} PrimeNativeRegionCapture;

/* The preparation metric counts inspected graph headers, edges, equality
 * probes and service queries. It does not claim a CPU instruction bound. */
static bool prime_native_region_tick(PrimeNativeRegionCapture *capture) {
    PrimeNativeOwnedRegion *region = capture->region;
    if (region->inspection_spent == region->inspection_limit) {
        capture->status = CETTA_CONTINUATION_CAPACITY;
        return false;
    }
    region->inspection_spent++;
    cetta_native_cost_charge(CETTA_COST_ANALYSIS_STEP, 1u);
    return true;
}

static bool prime_native_region_probe(void *context) {
    return prime_native_region_tick(context);
}

static size_t prime_native_region_find(PrimeNativeRegionCapture *capture, const Atom *source) {
    uintptr_t index=0u;
    return eval_node_memo_find_checked(&capture->index,source,0u,&index,
        prime_native_region_probe,capture) ? (size_t)index : SIZE_MAX;
}

/* These are the scalar payload kinds carried by the snapshot graph.
 * Head admission below uses the same boundary as structural capture. */
static bool prime_native_region_scalar_kind(GroundedKind kind) {
    return kind == GV_INT || kind == GV_FLOAT || kind == GV_BOOL ||
        kind == GV_STRING || kind == GV_BIGINT || kind == GV_RATIONAL;
}

static CettaInternalTag prime_native_region_syntax_tag(const Atom *node) {
    if (atom_is_prime_own_list(node)) return CETTA_INTERNAL_TAG_PRIME_OWN;
    if (atom_is_prime_braces(node)) return CETTA_INTERNAL_TAG_PRIME_BRACES;
    if (atom_is_list_form(node))
        return atom_is_list_rest(node) ? CETTA_INTERNAL_TAG_LIST_REST : CETTA_INTERNAL_TAG_LIST;
    return 0;
}

static bool prime_native_region_push(PrimeNativeRegionCapture *capture, Atom *source,
    CettaInternalTag syntax_tag) {
    if (!source || !prime_native_region_tick(capture)) return false;
    /* Only a recognized immutable syntax container supplies its tag leaf.
     * Check that role before DAG reuse, so sharing cannot turn a private
     * marker into a standalone executable or value. */
    if (source->kind==ATOM_GROUNDED && source->ground.gkind==GV_INTERNAL_TAG &&
        (!syntax_tag || !atom_is_internal_tag(source,syntax_tag)))
        return false;
    size_t known = prime_native_region_find(capture, source);
    if (capture->status == CETTA_CONTINUATION_CAPACITY) return false;
    if (known != SIZE_MAX) {
        PrimeNativeRegionNode *node=&capture->nodes[known];
        if (node->state != 2u) return false;
        if ((capture->value && !node->value) ||
            (capture->executable && !node->executable)) {
            node->value|=capture->value; node->executable|=capture->executable;
            node->state=1u; node->edge=0u;
            capture->stack[capture->depth++]=known;
        }
        return true;
    }
    if (capture->length == capture->capacity) {
        size_t next = capture->capacity ? capture->capacity * 2u : 16u;
        size_t unit = sizeof(*capture->nodes) + sizeof(*capture->stack);
        if (next < capture->capacity || next > SIZE_MAX / unit ||
            next * unit > capture->region->byte_limit ||
            capture->capacity * unit > capture->region->byte_limit - next * unit) {
            capture->status = CETTA_CONTINUATION_CAPACITY; return false;
        }
        PrimeNativeRegionNode *nodes = malloc(next * sizeof(*nodes));
        size_t *stack = malloc(next * sizeof(*stack));
        if (!nodes || !stack) {
            free(nodes); free(stack);
            capture->status = CETTA_CONTINUATION_CAPACITY; return false;
        }
        if (capture->length) memcpy(nodes, capture->nodes, capture->length * sizeof(*nodes));
        if (capture->depth) memcpy(stack, capture->stack, capture->depth * sizeof(*stack));
        free(capture->nodes); free(capture->stack);
        capture->nodes = nodes; capture->stack = stack; capture->capacity = next;
    }
    if (source->structural_facts & ATOM_STRUCTURAL_HAS_NATIVE_HANDLE_ID) return false;
    if (source->kind == ATOM_GROUNDED) {
        GroundedKind kind = source->ground.gkind;
        if (!prime_native_region_scalar_kind(kind) &&
            !(syntax_tag && atom_is_internal_tag(source,syntax_tag)))
            return false;
        if (kind == GV_STRING && (!source->ground.sval ||
                source->ground.slen >= capture->region->byte_limit)) {
            capture->status = CETTA_CONTINUATION_CAPACITY; return false;
        }
    } else if (source->kind != ATOM_SYMBOL && source->kind != ATOM_VAR && source->kind != ATOM_EXPR)
        return false;
    size_t index = capture->length;
    if (!eval_node_memo_store_checked(&capture->index,source,0u,(uintptr_t)index,
            prime_native_region_probe,capture,capture->region->byte_limit)) {
        capture->status=CETTA_CONTINUATION_CAPACITY;return false;
    }
    capture->length++;
    capture->nodes[index] = (PrimeNativeRegionNode){.source=source,.state=1u,
        .value=capture->value,.executable=capture->executable};
    capture->stack[capture->depth++] = index;
    return true;
}

static bool prime_native_region_walk(PrimeNativeRegionCapture *capture, Atom *root) {
    if (!prime_native_region_push(capture, root, false)) return false;
    while (capture->depth) {
        PrimeNativeRegionNode *node = &capture->nodes[capture->stack[capture->depth-1u]];
        Atom *source = node->source;
        size_t edges = source->kind == ATOM_EXPR ? source->expr.len :
            (source->kind == ATOM_VAR && source->name_key ? 1u : 0u);
        if (node->edge == edges) {
            node->state = 2u; capture->depth--; continue;
        }
        if (!prime_native_region_tick(capture) || (source->kind == ATOM_EXPR && !source->expr.elems))
            return false;
        CettaInternalTag syntax_tag=source->kind==ATOM_EXPR && node->edge==0u
            ? prime_native_region_syntax_tag(source) : 0;
        Atom *child = source->kind == ATOM_EXPR ? source->expr.elems[node->edge] : source->name_key;
        node->edge++;
        if (capture->region->captured_edges == SIZE_MAX) return false;
        capture->region->captured_edges++;
        if (!prime_native_region_push(capture, child, syntax_tag)) return false;
    }
    return true;
}

static bool prime_native_region_world_registered(PrimeNativeRegionCapture *capture,
    const Space *space) {
    PrimeNativeOwnedRegion *region=capture->region;
    SpaceCheckpointBudget budget={.work_limit=region->inspection_limit-region->inspection_spent};
    bool okay=space_universe_registration_current(space,&budget);
    assert(budget.work<=budget.work_limit);
    region->inspection_spent+=budget.work;
    /* The Space reader itself emits these exact analysis charges. */
    if (!okay && budget.refusal && !strcmp(budget.refusal,"WorldInspectionLimit"))
        capture->status=CETTA_CONTINUATION_CAPACITY;
    return okay;
}

/* Operations admitted by the immutable first-order profile. */
static bool prime_native_region_primitive(SymbolId head) {
    return head == g_builtin_syms.quote || head == g_builtin_syms.superpose ||
        head == g_builtin_syms.if_text || head == g_builtin_syms.if_equal ||
        head == g_builtin_syms.let || head == g_builtin_syms.chain ||
        head == g_builtin_syms.let_star || head == g_builtin_syms.case_text ||
        head == g_builtin_syms.cons_atom || head == g_builtin_syms.decons_atom ||
        head == g_builtin_syms.op_plus || head == g_builtin_syms.op_minus ||
        head == g_builtin_syms.op_mul || head == g_builtin_syms.op_div ||
        head == g_builtin_syms.op_mod || head == g_builtin_syms.op_lt ||
        head == g_builtin_syms.op_gt || head == g_builtin_syms.op_le ||
        head == g_builtin_syms.op_ge || head == g_builtin_syms.op_eq ||
        head == g_builtin_syms.op_and || head == g_builtin_syms.op_or ||
        head == g_builtin_syms.op_not || head == g_builtin_syms.empty_form;
}

/* This is shallow constructor recognition, not an effect judgement for
 * the expression. Capture still inspects every descendant and world row;
 * ordinary evaluation still owns field demand and equation queries. */
static bool prime_native_owned_region_constructor_head(Space *space, Arena *arena, Atom *call) {
    if (!call || call->kind != ATOM_EXPR || !call->expr.len || !call->expr.elems)
        return false;
    Atom *head = call->expr.elems[0];
    return head &&
        ((head->kind==ATOM_GROUNDED && prime_native_region_scalar_kind(head->ground.gkind) &&
          atom_has_constructor_head(space,arena,call)) ||
         (head->kind==ATOM_EXPR && atom_has_constructor_head(space,arena,head)));
}

static bool prime_native_owned_region_call_admitted(Space *space, Arena *arena,
    Atom *call, bool private_data) {
    if (!call || call->kind != ATOM_EXPR || !call->expr.len) return true;
    Atom *head_atom = call->expr.elems[0];
    if (private_data && prime_native_head_requires_resolution(
            head_atom, &g_prime_need_active))
        return true;
    if (!head_atom || head_atom->kind != ATOM_SYMBOL)
        return prime_native_owned_region_constructor_head(space, arena, call);
    SymbolId head = atom_symbol_current_id(head_atom);
    if (prime_native_region_primitive(head)) return true;
    if (private_data && (head == g_builtin_syms.match ||
        head == g_builtin_syms.get_atoms || head == g_builtin_syms.add_atom ||
        head == g_builtin_syms.remove_atom || head == g_builtin_syms.collapse ||
        head == g_builtin_syms.function || head == g_builtin_syms.return_text ||
        head == g_builtin_syms.eval || head == g_builtin_syms.unify ||
        head == g_builtin_syms.context_space))
        return true;
    /* These semantic handlers use the same inspected equation/argument
     * closure. Costs, interaction services and effectful primitives retain
     * their separate admission contracts. Every descendant is still checked. */
    const PrimeNeedSymbolIds *weights=prime_need_symbols();
    if (head==weights->weight_where || head==weights->weight_read ||
        head==weights->weight_exact_rational) return true;
    /* These are declaration/data constructors, not mutable services. */
    if (head == g_builtin_syms.equals || head == g_builtin_syms.colon ||
        head == g_builtin_syms.arrow) return true;
    if (symbol_id_is_builtin(head) || registry_lookup_atom(head_atom) ||
        eval_native_weight_head(head) || cetta_prime_host_intrinsic_head(head)) return false;
    if (private_data && eval_equations_may_match_known_head(space,head)) return true;
    return g_library_context && g_library_context->petta_program &&
        petta_program_classify_resolved_call(g_library_context->petta_program, space, call)
            != PETTA_RESOLVED_CALL_UNSAFE;
}

/* Compare one already captured expression in two held, quiescent
 * contexts. This does not inspect the rest of the graph or grant execution
 * authority. The caller pays its inspection tick and retains the complete
 * root, descendant, catalog, relocation and currentness obligations. */
static bool prime_native_region_call_agrees(PrimeNativeOwnedRegion *region,
    CettaLibraryContext *origin, Space *source, Registry *registry,
    Atom *node, Atom *image, const Atom *expression) {
    if (!region || !origin || !source || !registry || !node || !image ||
        node->kind != ATOM_EXPR || image->kind != ATOM_EXPR ||
        !node->expr.len || !image->expr.len || !node->expr.elems || !image->expr.elems ||
        !node->expr.elems[0] || !image->expr.elems[0]) return false;
    Atom *head_atom = node->expr.elems[0];
    bool symbol_head = head_atom->kind == ATOM_SYMBOL;
    SymbolId head = symbol_head ? atom_symbol_current_id(head_atom) : SYMBOL_ID_NONE;
    if (symbol_head) {
        PeTTaNamedArity foreign = petta_libpl_named_arity(
            origin->lib_prolog, head, expr_nargs(node));
        if (foreign.known) return false;
    }
    Registry *saved_registry = g_registry;
    g_registry = registry;
    bool left_scalar = prime_native_owned_region_constructor_head(source, &region->snapshot, node);
    bool indirect = region->private_data &&
        (head_atom->kind == ATOM_VAR || head_atom->kind == ATOM_EXPR);
    bool left = (symbol_head || left_scalar || indirect) &&
        (region->private_data || node == expression || left_scalar ||
         prime_native_owned_region_call_admitted(source, &region->snapshot, node,
             region->private_data));
    PettaResolvedCallClass left_class = petta_program_classify_resolved_call(
        origin->petta_program, source, node);
    bool left_grounded = symbol_head && is_grounded_op(head);
    g_registry = saved_registry;
    PrimeNativeRegionScope scope = {0};
    bool okay = left && prime_native_region_scope_select(region, &scope);
    if (okay) {
        Atom *image_head = image->expr.elems[0];
        bool right_symbol = image_head->kind == ATOM_SYMBOL;
        bool right_scalar = prime_native_owned_region_constructor_head(
            &region->world, &region->snapshot, image);
        bool right_indirect = region->private_data &&
            (image_head->kind == ATOM_VAR || image_head->kind == ATOM_EXPR);
        bool right = (right_symbol || right_scalar || right_indirect) &&
            (region->private_data || node == expression || right_scalar ||
             prime_native_owned_region_call_admitted(&region->world, &region->snapshot, image,
                 region->private_data));
        PettaResolvedCallClass right_class = petta_program_classify_resolved_call(
            region->library->petta_program, &region->world, image);
        bool foreign_known = false;
        if (right_symbol) {
            PeTTaNamedArity foreign = petta_libpl_named_arity(
                region->library->lib_prolog, atom_symbol_current_id(image_head), expr_nargs(image));
            foreign_known = foreign.known;
        }
        bool same_category = symbol_head == right_symbol && left_scalar == right_scalar &&
            indirect == right_indirect &&
            (symbol_head ? atom_symbols_equal(head_atom, image_head) :
             indirect ? head_atom->kind == image_head->kind :
             left_scalar && head_atom->kind==image_head->kind &&
                 (head_atom->kind!=ATOM_GROUNDED ||
                  head_atom->ground.gkind == image_head->ground.gkind));
        okay = right && same_category && !foreign_known && left_class == right_class &&
            left_grounded == (right_symbol && is_grounded_op(atom_symbol_current_id(image_head)));
    }
    prime_native_region_scope_leave(&scope);
    return okay;
}

static bool prime_native_region_baseline(const CettaLibraryContext *source) {
    return cetta_library_context_prime_snapshot_source_admitted(source);
}

static bool prime_native_region_measure(PrimeNativeRegionCapture *capture,
    const AtomDeepCopySession *copy) {
    if (!prime_native_region_tick(capture)) return false;
    CettaStorageCensus census={0};
    prime_native_owned_region_storage(capture->region, &census);
    atom_deep_copy_session_storage_census(copy, &census);
    cetta_storage_array(&census, capture->nodes, capture->capacity, sizeof(*capture->nodes), 0u);
    cetta_storage_array(&census, capture->stack, capture->capacity, sizeof(*capture->stack), 0u);
    cetta_storage_array(&census, capture->index.slots, capture->index.cap,
        sizeof(*capture->index.slots), 0u);
    cetta_storage_finish(&census);
    cetta_native_cost_charge(CETTA_COST_STORAGE_PROBE,census.probes);
    bool okay = !census.incomplete && !census.overflow &&
        census.retained <= capture->region->byte_limit;
    capture->region->prepared_bytes = census.retained;
    if (!okay) capture->status = CETTA_CONTINUATION_CAPACITY;
    cetta_storage_census_free(&census);
    return okay;
}

typedef struct {
    PrimeNativeRegionCapture *capture;
    const AtomDeepCopySession *copy;
    size_t baseline, temporary;
    bool refused;
} PrimeNativeRegionPlanCompare;

static bool prime_native_region_plan_scalar(void *context, const char *site,
    uint64_t left, uint64_t right) {
    (void)site; (void)left; (void)right;
    PrimeNativeRegionPlanCompare *compare = context;
    bool okay = prime_native_region_tick(compare->capture);
    if (!okay) compare->refused = true;
    return okay;
}

static bool prime_native_region_plan_atom(void *context, const char *site,
    const Atom *left, const Atom *right) {
    (void)site;
    PrimeNativeRegionPlanCompare *compare = context;
    bool okay = prime_native_region_tick(compare->capture) &&
        left && right && atom_deep_copy_session_forwarded(compare->copy, left) == right;
    if (!okay) compare->refused = true;
    return okay;
}

static bool prime_native_region_plan_service(void *context, const char *site,
    size_t work, size_t scratch) {
    (void)site;
    PrimeNativeRegionPlanCompare *compare = context;
    PrimeNativeRegionCapture *capture = compare->capture;
    /* A scratch request is one paid service admission; subsequent actual
     * stack-copy and field visits have their own charges. */
    if (scratch && !prime_native_region_tick(capture)) {
        compare->refused = true;
        return false;
    }
    for (size_t i = 0u; i < work; i++) {
        if (!prime_native_region_tick(capture)) {
            compare->refused = true;
            return false;
        }
    }
    size_t limit = capture->region->byte_limit;
    if (compare->baseline > limit || compare->temporary > limit - compare->baseline ||
        scratch > limit - compare->baseline - compare->temporary) {
        capture->status = CETTA_CONTINUATION_CAPACITY;
        compare->refused = true;
        return false;
    }
    compare->temporary += scratch;
    return true;
}

static void prime_native_region_plan_release(void *context, size_t scratch) {
    PrimeNativeRegionPlanCompare *compare = context;
    assert(scratch <= compare->temporary);
    compare->temporary -= scratch;
}

static bool prime_native_region_prepare_programs(PrimeNativeRegionCapture *capture,
    CettaLibraryContext *origin, Space *source, Registry *registry,
    const AtomDeepCopySession *copy) {
    PrimeNativeOwnedRegion *region = capture->region;
    Registry *saved_registry = g_registry;
    g_registry = registry;
    /* These are paid metadata service queries, not a bound on the internal
     * compiler's work or peak allocation. Known authored plans remain owned
     * by the source program; only unseen equations are prepared there. */
    bool okay = prime_native_region_tick(capture) &&
        petta_program_synchronize_space(origin->petta_program, source);
    g_registry = saved_registry;
    PrimeNativeRegionScope scope = {0};
    if (okay) okay = prime_native_region_scope_select(region, &scope);
    if (okay) okay = prime_native_region_tick(capture) &&
        petta_program_synchronize_space(region->library->petta_program, &region->world);
    prime_native_region_scope_leave(&scope);
    if (!okay || !prime_native_region_measure(capture, copy))
        return false;
    PrimeNativeRegionPlanCompare compare = {.capture=capture, .copy=copy,
        .baseline=region->prepared_bytes};
    CettaCheckpointCompare read = {.context=&compare,
        .scalar=prime_native_region_plan_scalar, .atom=prime_native_region_plan_atom,
        .service=prime_native_region_plan_service, .service_release=prime_native_region_plan_release};
    int equal = petta_program_snapshot_semantics_compare(origin->petta_program, source,
        region->library->petta_program, &region->world, &read);
    assert(!compare.temporary);
    if (equal < 0 && !compare.refused) capture->status = CETTA_CONTINUATION_CAPACITY;
    return equal == 1;
}

static PrimeNativeOwnedRegion *prime_native_owned_region_capture(
    Space *source, Registry *registry, Atom *expression, Atom *program,
    size_t inspection_limit, size_t byte_limit, bool private_data,
    CettaContinuationStatus *status) {
    *status = CETTA_CONTINUATION_UNSUPPORTED;
    if (!source || !registry || !expression || !program || !prime_native_owned_top_context_admitted() ||
        !prime_native_region_baseline(g_library_context) || g_prime_owned_region ||
        g_prime_need_active.top || g_prime_need_logical_env ||
        g_prime_need_answer_observer || g_prime_need_answer_observer_context ||
        registry->states || registry->len > registry->cap ||
        (registry->len && !registry->entries) || source->kind != SPACE_KIND_ATOM ||
        source->overlay_base || source->dep_count || source->imported_len ||
        (source->match_backend.kind != SPACE_ENGINE_NATIVE &&
         source->match_backend.kind != SPACE_ENGINE_NATIVE_CANDIDATE_EXACT)) return NULL;
    if (!inspection_limit || byte_limit < sizeof(PrimeNativeOwnedRegion) + sizeof(CettaLibraryContext)) {
        *status = CETTA_CONTINUATION_CAPACITY; return NULL;
    }
    CettaLibraryContext *origin = g_library_context;
    PrimeNativeOwnedRegion *region = calloc(1u, sizeof(*region));
    if (!region) { *status=CETTA_CONTINUATION_CAPACITY; return NULL; }
    region->inspection_limit=inspection_limit; region->byte_limit=byte_limit;
    region->private_data=private_data;
    region->symbols=symbol_table_instance_id(g_symbols);
    cetta_native_cost_init(&region->preparation);
    CettaNativeCostScope cost;
    cetta_native_cost_scope_enter(&cost, &region->preparation, 0u);
    PrimeNativeRegionCapture capture = {.region=region,.value=true,.executable=program==expression,
        .status=CETTA_CONTINUATION_UNSUPPORTED};
    AtomDeepCopySession *copy=NULL;
    SpaceReadToken source_read={0};
    bool okay=prime_native_region_tick(&capture);
    if (okay) {
        source_read=space_read_token(source);
        okay=space_read_token_matches_live_space(source_read,source);
    }
    okay=okay && prime_native_region_world_registered(&capture,source) &&
        prime_native_region_walk(&capture,expression);
    /* The admitted top request interprets its policy as data. Only its
     * program executes. A node shared with that program, a world row or a
     * registry value is still checked in its executable role. */
    capture.executable=true;
    if (okay && program!=expression) {
        okay=prime_native_region_walk(&capture,program);
        size_t root=prime_native_region_find(&capture,expression);
        okay=okay && root!=SIZE_MAX;
        if (okay) capture.nodes[root].executable=true;
    }
    CettaCount rows=space_length64(source);
    for (CettaIndex i=0u; okay && i<rows; i++) {
        okay=prime_native_region_tick(&capture) &&
            prime_native_region_walk(&capture,space_get_at64(source,i));
        if (okay) region->captured_occurrences++;
    }
    for (uint32_t i=0u; okay && i<registry->len; i++) {
        Atom *key=(Atom *)registry_entry_name_key(registry,i);
        bool symbol=registry->entries[i].key!=SYMBOL_ID_NONE;
        Atom *value=registry->entries[i].value;
        bool root_space=value && value->kind==ATOM_GROUNDED &&
            value->ground.gkind==GV_SPACE && value->ground.ptr==source;
        capture.value=false;
        okay=prime_native_region_tick(&capture) &&
            (symbol ? registry->entries[i].name_id==NAME_ID_NONE :
                key && prime_native_region_walk(&capture,key));
        capture.value=true;
        okay=okay && (root_space || prime_native_region_walk(&capture,value));
    }
    if (okay) {
        region->library=calloc(1u,sizeof(*region->library));
        okay=region->library!=NULL;
        if (!okay) capture.status=CETTA_CONTINUATION_CAPACITY;
    }
    if (okay) {
        okay=cetta_library_context_init_prime_snapshot_from(region->library,origin);
        region->library_ready=true;
        if (!okay) capture.status=CETTA_CONTINUATION_CAPACITY;
        arena_init_detached(&region->snapshot); arena_set_scalar_cache(&region->snapshot,false);
        arena_init_detached(&region->persistent); arena_set_scalar_cache(&region->persistent,false);
        region->arenas_ready=true;
        term_universe_set_persistent_arena(&region->library->term_universe,&region->snapshot);
        space_init_with_universe(&region->world,&region->library->term_universe);
        region->world_ready=true;
        HashConsTable *previous_hashcons=g_hashcons;
        g_hashcons=NULL;
        registry_init(&region->registry);
        g_hashcons=previous_hashcons;
        region->registry_ready=true;
        region->services=prime_native_source_services_new();
        okay=okay && region->services;
        if (!okay) capture.status=CETTA_CONTINUATION_CAPACITY;
        if (okay) okay=prime_native_region_world_registered(&capture,&region->world);
    }
    if (okay) {
        copy=atom_deep_copy_session_new(&region->snapshot);
        okay=copy && atom_deep_copy_session_set_snapshot(copy) &&
            prime_native_region_measure(&capture,copy);
    }
    for (size_t i=0u; okay && i<capture.length; i++) {
        okay=prime_native_region_tick(&capture);
        if (!okay) break;
        capture.nodes[i].image=atom_deep_copy_session_copy(copy,capture.nodes[i].source);
        okay=capture.nodes[i].image && prime_native_region_tick(&capture);
        if (okay) {
            capture.nodes[i].stored=term_universe_store_snapshot_image(
                &region->library->term_universe,copy,capture.nodes[i].source);
            okay=capture.nodes[i].stored!=CETTA_ATOM_ID_NONE &&
                term_universe_get_atom(&region->library->term_universe,
                    capture.nodes[i].stored)==capture.nodes[i].image;
        }
        okay=okay && prime_native_region_measure(&capture,copy);
    }
    if (okay) region->expression=atom_deep_copy_session_forwarded(copy,expression);
    for (CettaIndex i=0u; okay && i<rows; i++) {
        Atom *root=space_get_at64(source,i);
        size_t node=prime_native_region_find(&capture,root);
        okay=prime_native_region_tick(&capture) && node!=SIZE_MAX;
        if (okay) {
            space_add_atom_id(&region->world,capture.nodes[node].stored);
            okay=space_length64(&region->world)==i+1u &&
                space_get_at64(&region->world,i)==capture.nodes[node].image &&
                prime_native_region_measure(&capture,copy);
        }
    }
    for (uint32_t i=0u; okay && i<registry->len; i++) {
        const Atom *source_key=registry_entry_name_key(registry,i);
        bool symbol=registry->entries[i].key!=SYMBOL_ID_NONE;
        Atom *key=symbol ? NULL : atom_deep_copy_session_snapshot_image(copy,
            &region->snapshot,source_key);
        Atom *value=registry->entries[i].value;
        bool root_space=value->kind==ATOM_GROUNDED && value->ground.gkind==GV_SPACE;
        Atom *image=root_space ? NULL : atom_deep_copy_session_forwarded(copy,value);
        for (uint32_t j=0u; root_space && okay && !image && j<i; j++) {
            okay=prime_native_region_tick(&capture);
            if (registry->entries[j].value==value) image=region->registry.entries[j].value;
        }
        if (root_space && okay && !image) image=atom_space(&region->snapshot,&region->world);
        okay=okay && prime_native_region_tick(&capture) && image;
        if (okay && symbol) {
            registry_bind_id(&region->registry,registry->entries[i].key,image);
            okay=region->registry.len==i+1u && region->registry.entries[i].value==image &&
                region->registry.entries[i].key==registry->entries[i].key;
        } else if (okay) {
            okay=key && registry_bind_snapshot_name(&region->registry,copy,
                &region->snapshot,source_key,image) && region->registry.len==i+1u &&
                registry_entry_name_key(&region->registry,i)==key &&
                region->registry.entries[i].value==image;
        }
    }
    if (okay) okay=prime_native_region_prepare_programs(&capture,origin,source,registry,copy);
    /* Compare every executable call-shaped node, including RHS and quoted
     * payloads. Admitted top-policy data retains its separate role. Private
     * code may retain unavailable operations without invoking them; every
     * actual resolved call still crosses its invocation-time service gate.
     * The omitted foreign capability is compared even for inert names. */
    for (size_t i=0u; okay && i<capture.length; i++) {
        Atom *node=capture.nodes[i].source, *image=capture.nodes[i].image;
        if (node->kind==ATOM_SYMBOL) {
            /* A symbolic function can be passed as data and later become a
             * computed head. Omitting its foreign callable interpretation
             * must not turn it into a constructor in the captured context. */
            if (!prime_native_region_tick(&capture) ||
                petta_libpl_named_arity(origin->lib_prolog,atom_symbol_current_id(node),0u).known) {
                okay=false;break;
            }
        }
        if (node->kind==ATOM_SYMBOL && capture.nodes[i].value) {
            Atom *binding=registry_lookup_symbol(registry,node);
            if (binding && binding->kind==ATOM_GROUNDED && binding->ground.gkind==GV_SPACE) {
                okay=region->private_data && binding->ground.ptr==source;
                if (!okay) break;
            }
        }
        if (!capture.nodes[i].executable || node->kind!=ATOM_EXPR || !node->expr.len) continue;
        if (!prime_native_region_tick(&capture)) {
            okay=false; break;
        }
        CettaInternalTag syntax_tag=prime_native_region_syntax_tag(node);
        if (syntax_tag) {
            okay=prime_native_region_syntax_tag(image)==syntax_tag;
            continue;
        }
        okay=prime_native_region_call_agrees(region,origin,source,registry,
            node,image,expression);
    }
    region->captured_nodes=capture.length;
    okay=okay && region->expression && region->symbols==symbol_table_instance_id(g_symbols) &&
        prime_native_region_tick(&capture) &&
        space_read_token_matches_live_space(source_read,source) &&
        prime_native_region_measure(&capture,copy);
    atom_deep_copy_session_free(copy);
    eval_node_memo_free(&capture.index);
    free(capture.nodes); free(capture.stack);
    cetta_native_cost_scope_leave(&cost);
    if (!okay) {
        *status=capture.status;
        prime_native_owned_region_free(region);
        return NULL;
    }
    region->ready=true;
    *status=CETTA_CONTINUATION_READY;
    return region;
}

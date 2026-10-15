/* Optional projection of the existing owned agenda and Need receipt DAG.
 * Allocation order is not selection order. Graphs are views of individual
 * branch roots; their overlapping ancestors are not additive partitions. */
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
static PrimeNeedReceipt prime_native_parallel_history(const PrimeNativeWork *work);

static bool prime_native_history_configure(PrimeNativeControl *control,
    const PrimeNativeHistorySpec *spec) {
    PrimeNativeHistory *history = &control->history;
    /* An allowance-only resume inherits the retained recording. Changing
     * its owner or limits requires a new observation, not a hidden restart. */
    if (!spec->enabled) return true;
    if (history->spec.enabled)
        return history->spec.selections == spec->selections &&
            history->spec.nodes == spec->nodes && history->spec.bytes == spec->bytes &&
            history->spec.owners == spec->owners;
    history->spec = *spec;
    history->start = control->steps;
    if (spec->selections > SIZE_MAX / sizeof(*history->entries)) return true;
    history->entries = spec->selections
        ? calloc(spec->selections, sizeof(*history->entries)) : NULL;
    if (spec->selections && !history->entries) return true;
    history->group = prime_need_recording_new((PrimeNeedRecordingLimits){
        .nodes = spec->selections * spec->nodes,
        .payload_bytes = spec->selections * spec->bytes,
        .slices = spec->selections + 1u,
        .owner_links = spec->owners,
    });
    return true;
}

static PrimeNeedReceipt prime_native_history_reserve(PrimeNativeControl *control) {
    PrimeNativeHistory *history = &control->history;
    PrimeNeedReceipt receipt = {0};
    if (history->group && history->reservations < history->spec.selections) {
        history->reservations++;
        (void)prime_need_recording_reserve_growing(history->group, NULL,
            history->spec.nodes, history->spec.bytes, &receipt);
    }
    return receipt;
}

static void prime_native_history_begin(PrimeNativeControl *control,
    const PrimeNativeWork *work, uint64_t occurrence,
    PrimeNeedReceipt *slice, PrimeNeedRecordingScope *scope) {
    PrimeNativeHistory *history = &control->history;
    if (!history->spec.enabled) return;
    *slice = prime_native_history_reserve(control);
    if (history->entries && history->count < history->spec.selections) {
        history->active = &history->entries[history->count++];
        *history->active = (PrimeNativeHistorySelection){
            .occurrence = occurrence,
            .protocol = work->licence ? "Analysis" : work->resource_step ? "Resource" :
                        work->dialect ? "Dialect" : "Need",
            .operation = *slice,
            .prepared = prime_native_parallel_history(work),
            .paid = work->prepared_publication,
            .preparation = work->parallel != NULL || work->prepared_publication,
        };
    } else if (history->omitted != UINT64_MAX) history->omitted++;
    else history->overflow = true;
    /* Even a refused reservation installs a scope: optional recording must
     * not escape its bound by using legacy diagnostic allocation. */
    prime_need_recording_enter(scope, slice);
}

static Atom *prime_native_history_field(Arena *arena, const char *name, Atom *value) {
    return atom_expr2(arena, atom_symbol(arena, name), value);
}

static Atom *prime_native_history_quote(Arena *arena, Atom *value) {
    return value ? atom_expr2(arena, atom_symbol(arena, "quote"),
                             atom_deep_copy(arena, value))
                 : atom_symbol(arena, "None");
}

typedef struct {
    Arena *arena;
    Atom **items;
    size_t count;
    bool complete;
} PrimeNativeHistoryRoots;

static bool prime_native_history_count_root(const PrimeNeedReceipt *root, void *raw) {
    (void)root;
    (*(size_t *)raw)++;
    return true;
}

static bool prime_native_history_root_atom(const PrimeNeedReceipt *root, void *raw) {
    PrimeNativeHistoryRoots *roots = raw;
    Arena *arena = roots->arena;
    PrimeNeedReceiptGraph graph = {0};
    PrimeNeedReceiptGraphStatus status = prime_need_receipt_graph_collect(root, &graph);
    if (status != PRIME_NEED_RECEIPT_GRAPH_COMPLETE &&
        status != PRIME_NEED_RECEIPT_GRAPH_INCOMPLETE) {
        roots->complete = false;
        roots->items[roots->count++] = atom_symbol(arena, "GraphUnavailable");
        prime_need_receipt_graph_free(&graph);
        return true;
    }
    if (status != PRIME_NEED_RECEIPT_GRAPH_COMPLETE) roots->complete = false;
    Atom **nodes = graph.length ? cetta_malloc(graph.length * sizeof(*nodes)) : NULL;
    for (size_t i = 0u; i < graph.length; i++) {
        const PrimeNeedReceiptGraphNode *node = &graph.nodes[i];
        const PrimeNeedReceiptEvent *event = &node->event;
        Atom *payload = atom_symbol(arena, "Join");
        if (node->has_event) {
            bool retained_producer = false;
            if (event->producer_update_id) {
                for (size_t j = 0u; j < graph.length; j++) {
                    const PrimeNeedReceiptEvent *producer = &graph.nodes[j].event;
                    if (graph.nodes[j].has_event && producer->producer_completed &&
                        producer->producer_update_id == event->producer_update_id &&
                        producer->need_session_id == event->need_session_id &&
                        producer->thunk_id == event->thunk_id) {
                        retained_producer = true;
                        break;
                    }
                }
            }
            Atom *fields[] = {atom_symbol(arena, "NeedEvent"),
                atom_int(arena, event->kind),
                prime_native_occurrence_atom(arena, event->need_session_id),
                prime_native_occurrence_atom(arena, event->thunk_id),
                prime_native_occurrence_atom(arena, event->source_occurrence_id),
                prime_native_occurrence_atom(arena, event->source_argument_index),
                prime_native_occurrence_atom(arena, event->rule_occurrence_id),
                prime_native_occurrence_atom(arena, event->producer_update_id),
                atom_symbol(arena, event->producer_completed ? "True" : "False"),
                prime_native_history_field(arena, "producer", atom_symbol(arena,
                    !event->producer_update_id ? "None" : retained_producer ? "Retained" : "External")),
                prime_native_history_quote(arena, event->before),
                prime_native_history_quote(arena, event->after)};
            payload = atom_expr(arena, fields, 12u);
        }
        Atom *fields[] = {atom_symbol(arena, "NeedNode"),
            prime_native_occurrence_atom(arena, node->identity_id),
            prime_native_occurrence_atom(arena, node->left_id),
            prime_native_occurrence_atom(arena, node->right_id), payload};
        nodes[i] = atom_expr(arena, fields, 5u);
    }
    Atom *fields[] = {atom_symbol(arena, "NeedGraph"),
        prime_native_occurrence_atom(arena, graph.session_id),
        prime_native_occurrence_atom(arena, graph.root_id),
        atom_symbol(arena, graph.incomplete ? "Incomplete" : "Complete"),
        atom_expr(arena, nodes, (CettaExprLen)graph.length)};
    roots->items[roots->count++] = atom_expr(arena, fields, 5u);
    free(nodes);
    prime_need_receipt_graph_free(&graph);
    return true;
}

static Atom *prime_native_history_forest(Arena *arena,
    const PrimeNeedReceipt *slice, bool *complete) {
    if (!slice->recording) {
        *complete = false;
        return atom_symbol(arena, "Uncaptured");
    }
    size_t count = 0u;
    if (!prime_need_recording_roots(slice, prime_native_history_count_root, &count)) {
        *complete = false;
        return atom_symbol(arena, "GraphUnavailable");
    }
    PrimeNativeHistoryRoots roots = {.arena = arena, .complete = true,
        .items = count ? cetta_malloc(count * sizeof(*roots.items)) : NULL};
    (void)prime_need_recording_roots(slice, prime_native_history_root_atom, &roots);
    *complete &= roots.complete;
    Atom *result = atom_expr(arena, roots.items, (CettaExprLen)roots.count);
    free(roots.items);
    return result;
}

static Atom *prime_native_history_atom(PrimeNativeControl *control, Arena *arena) {
    PrimeNativeHistory *history = &control->history;
    PrimeNeedRecordingStatus status = prime_need_recording_status(history->group);
    bool complete = history->group && !status.incomplete &&
        !history->omitted && !history->overflow;
    Atom **entries = history->count ? cetta_malloc(history->count * sizeof(*entries)) : NULL;
    for (size_t i = 0u; i < history->count; i++) {
        const PrimeNativeHistorySelection *entry = &history->entries[i];
        Atom *prepared = atom_symbol(arena, "None");
        if (entry->preparation)
            prepared = prime_native_history_forest(arena, &entry->prepared, &complete);
        Atom *operation = prime_native_history_forest(arena, &entry->operation, &complete);
        if (strcmp(entry->protocol, "Need") != 0) complete = false;
        Atom *fields[] = {atom_symbol(arena, "Selection"),
            prime_native_occurrence_atom(arena, i + 1u),
            prime_native_occurrence_atom(arena, entry->occurrence),
            atom_symbol(arena, entry->protocol),
            prime_native_occurrence_atom(arena, entry->spent),
            atom_symbol(arena, entry->paid ? "Prepared" : "Direct"),
            prime_native_history_field(arena, "preparation", prepared),
            prime_native_history_field(arena, "operation", operation)};
        entries[i] = atom_expr(arena, fields, 8u);
    }
    size_t entry_bytes = history->entries
        ? history->spec.selections * sizeof(*history->entries) : 0u;
    Atom *bounds[] = {atom_symbol(arena, "RetainedCharges"),
        prime_native_history_field(arena, "payload", prime_native_occurrence_atom(arena, status.payload_charge)),
        prime_native_history_field(arena, "group-metadata-bound", prime_native_occurrence_atom(arena, status.metadata_bound)),
        prime_native_history_field(arena, "selection-metadata", prime_native_occurrence_atom(arena, entry_bytes)),
        prime_native_history_field(arena, "owner-links", prime_native_occurrence_atom(arena, status.owner_links))};
    Atom *limits[] = {atom_symbol(arena, "limits"),
        prime_native_history_field(arena, "selections", prime_native_occurrence_atom(arena, history->spec.selections)),
        prime_native_history_field(arena, "nodes", prime_native_occurrence_atom(arena, history->spec.nodes)),
        prime_native_history_field(arena, "bytes", prime_native_occurrence_atom(arena, history->spec.bytes)),
        prime_native_history_field(arena, "owners", prime_native_occurrence_atom(arena, history->spec.owners))};
    Atom *fields[] = {atom_symbol(arena, "History"),
        prime_native_history_field(arena, "status", atom_symbol(arena, complete ? "Complete" : "Incomplete")),
        prime_native_history_field(arena, "start-work", prime_native_occurrence_atom(arena, history->start)),
        prime_native_history_field(arena, "earlier", atom_symbol(arena, "Unrecorded")),
        prime_native_history_field(arena, "source-correspondence", atom_symbol(arena, "Unchecked")),
        prime_native_history_field(arena, "omitted-selections", prime_native_occurrence_atom(arena, history->omitted)),
        prime_native_history_field(arena, "omitted-nodes", prime_native_occurrence_atom(arena, status.omitted)),
        prime_native_history_field(arena, "overflow", atom_symbol(arena,
            history->overflow || status.overflow ? "True" : "False")),
        atom_expr(arena, bounds, 5u),
        atom_expr(arena, limits, 5u),
        prime_native_history_field(arena, "nodes", prime_native_occurrence_atom(arena, status.nodes)),
        prime_native_history_field(arena, "reservations", prime_native_occurrence_atom(arena, history->reservations)),
        prime_native_history_field(arena, "selections", atom_expr(arena, entries, (CettaExprLen)history->count))};
    Atom *result = prime_native_history_field(arena, "history", atom_expr(arena, fields, 13u));
    free(entries);
    return result;
}
#endif

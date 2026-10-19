/* Optional projection of the existing owned agenda and Need receipt DAG.
 * Allocation order is not selection order. Graphs are views of individual
 * branch roots; their overlapping ancestors are not additive partitions. */
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
static PrimeNeedReceipt prime_native_parallel_history(const PrimeNativeWork *work);

/* The unrecorded loop never enters this scope. Keeping its cleanup inline
 * lets that specialization omit the recording service call entirely. */
static inline __attribute__((always_inline)) void prime_native_history_scope_leave(
    PrimeNeedRecordingScope *scope) {
    if (scope->entered) prime_need_recording_leave(scope);
}

/* Stage observer storage without changing the current diary or computation. */
static bool prime_native_history_prepare(const PrimeNativeHistorySpec *spec,
    uint64_t start, PrimeNativeHistory *out) {
    *out = (PrimeNativeHistory){.spec=*spec, .start=start};
    if (spec->selections == SIZE_MAX ||
        spec->selections > SIZE_MAX / sizeof(*out->entries) ||
        (spec->selections && (spec->nodes > SIZE_MAX / spec->selections ||
                             spec->bytes > SIZE_MAX / spec->selections))) return false;
    out->entries = spec->selections
        ? calloc(spec->selections, sizeof(*out->entries)) : NULL;
    if (spec->selections && !out->entries) return false;
    out->group = prime_need_recording_new((PrimeNeedRecordingLimits){
        .nodes=spec->selections * spec->nodes,
        .payload_bytes=spec->selections * spec->bytes,
        .slices=spec->selections + 1u, .owner_links=spec->owners});
    return out->group != NULL;
}

/* Recorder allocation and replacement are absent from the plain entry's
 * stack frame. They run only for an explicitly enabled observation. */
static __attribute__((noinline, cold)) bool prime_native_history_start(
    PrimeNativeControl *control, const PrimeNativeHistorySpec *spec) {
    PrimeNativeHistory *history = &control->history;
    PrimeNativeHistory prepared;
    bool ready = prime_native_history_prepare(spec, control->steps, &prepared);
    if (history->stopped || history->forgotten) {
        /* Completed values and prepared slices may still carry handles to
         * the old group. Keep its metadata through replacement; only actual
         * readers retain the old payload after the recorder is released. */
        if (!ready) {
            prime_need_recording_release(prepared.group);
            free(prepared.entries);
            return false;
        }
        if (!history->forgotten) prime_need_recording_demote(history->group);
        if (history->group) prime_need_recording_adopt_previous(prepared.group, history->group);
        free(history->entries);
    }
    /* Initial allocation refusal remains an explicitly incomplete observer;
     * it does not obstruct ordinary execution. Replacing a stopped diary,
     * in contrast, commits only after the new owner is ready. */
    *history = prepared;
    control->policy.history = *spec;
    return true;
}

static bool prime_native_history_configure(PrimeNativeControl *control,
    const PrimeNativeHistorySpec *spec) {
    PrimeNativeHistory *history = &control->history;
    /* An allowance-only resume inherits recording. Explicit False stops
     * future recording without moving executable state or releasing roots
     * still borrowed by retained children. Re-enabling starts a new suffix. */
    if (spec->stop) {
        if (spec->enabled || history->active) return false;
        if (history->spec.enabled) {
            prime_need_recording_seal(history->group);
            history->spec.enabled = false;
            history->stopped = true;
            history->stop = control->steps;
        }
        control->policy.history = *spec;
        return true;
    }
    if (!spec->enabled) return true;
    if (history->spec.enabled)
        return history->spec.selections == spec->selections &&
            history->spec.nodes == spec->nodes && history->spec.bytes == spec->bytes &&
            history->spec.owners == spec->owners;
    return prime_native_history_start(control, spec);
}

/* The observer's owner supplies lifetime and authority. This shared update
 * requires quiescence and preserves every executable frame and paid account. */
static bool prime_native_history_quiescent(const PrimeNativeControl *control) {
    return control && !control->leased && !control->active_work &&
        !control->driver.running_task && !control->history.active &&
        g_prime_eval_stack_driver!=&control->driver &&
        g_prime_eval_top_authority!=&control->driver;
}
static bool prime_native_history_forget_control(PrimeNativeControl *control) {
    if (!prime_native_history_quiescent(control)) return false;
    PrimeNativeHistory *history=&control->history;
    PrimeNeedRecording *carrier=history->group;
    if (!history->forgotten) prime_need_recording_demote(carrier);
    free(history->entries);
    *history=(PrimeNativeHistory){.group=carrier,.forgotten=true};
    control->policy.history=(PrimeNativeHistorySpec){0};
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
    bool short_retained[32];
    bool *retained = graph.length <= 32u ? short_retained
        : malloc(graph.length * sizeof(*retained));
    if ((graph.length && !retained) ||
        !prime_need_receipt_graph_producers(&graph, retained)) {
        roots->complete = false;
        roots->items[roots->count++] = atom_symbol(arena, "GraphUnavailable");
        if (retained != short_retained) free(retained);
        prime_need_receipt_graph_free(&graph);
        return true;
    }
    Atom **nodes = graph.length ? cetta_malloc(graph.length * sizeof(*nodes)) : NULL;
    for (size_t i = 0u; i < graph.length; i++) {
        const PrimeNeedReceiptGraphNode *node = &graph.nodes[i];
        const PrimeNeedReceiptEvent *event = &node->event;
        Atom *payload = atom_symbol(arena, "Join");
        if (node->has_event) {
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
                    !event->producer_update_id ? "None" : retained[i] ? "Retained" : "External")),
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
    if (retained != short_retained) free(retained);
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
        prime_native_history_field(arena, "recording", atom_symbol(arena,
            history->stopped ? "False" : "True")),
        prime_native_history_field(arena, "end-work", prime_native_occurrence_atom(arena,
            history->stopped ? history->stop : control->steps)),
        prime_native_history_field(arena, "omitted-selections", prime_native_occurrence_atom(arena, history->omitted)),
        prime_native_history_field(arena, "omitted-nodes", prime_native_occurrence_atom(arena, status.omitted)),
        prime_native_history_field(arena, "overflow", atom_symbol(arena,
            history->overflow || status.overflow ? "True" : "False")),
        atom_expr(arena, bounds, 5u),
        atom_expr(arena, limits, 5u),
        prime_native_history_field(arena, "nodes", prime_native_occurrence_atom(arena, status.nodes)),
        prime_native_history_field(arena, "reservations", prime_native_occurrence_atom(arena, history->reservations)),
        prime_native_history_field(arena, "selections", atom_expr(arena, entries, (CettaExprLen)history->count))};
    _Static_assert(sizeof(fields) / sizeof(*fields) == PRIME_NATIVE_HISTORY_ARITY,
        "history producer and replay schema must agree");
    Atom *result = prime_native_history_field(arena, "history", atom_expr(arena, fields,
        sizeof(fields) / sizeof(*fields)));
    free(entries);
    return result;
}
#endif

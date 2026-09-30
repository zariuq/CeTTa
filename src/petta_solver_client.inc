/* Retained CLP(FD) client of the branch delay service. The reference's eight
 * arithmetic relations are admitted by their live clause, not by workload
 * shape. Other foreign calls materialize this client into ordinary residual
 * goals. There is no replay of user hooks or foreign effects here.
 *
 * Linear calls keep SWI's graph and checkpoints. At an actual engine/branch
 * transfer, current and rewindable states become independent recorded graphs.
 * Native terms live in the client's arena; no machine nursery pointer or
 * operation-local SWI term reference survives a call. */

typedef struct PettaSolverClient PettaSolverClient;
typedef struct {
    CettaForeignSnapshot *graph;
    size_t references;
} PettaSolverImage;
typedef struct {
    VarId id;
    Atom *prototype;
    uint64_t key;
    bool queued;
} PettaSolverVariable;
typedef struct {
    uint32_t mark;
    size_t variables, first;
    ArenaMark storage;
    PettaSolverImage *before;
} PettaSolverCheckpoint;

struct PettaSolverClient {
    PettaSolverClient *next;
    CettaLibPrologRuntime *runtime;
    CettaForeignRegion *region;
    PL_engine_t engine;
    PettaSolverImage *image;
    PettaSolverVariable *vars;
    size_t len, cap, first;
    CettaVarIndex index;
    uint64_t next_key;
    PettaSolverCheckpoint *history;
    size_t history_len, history_cap;
    Arena arena, views;
    bool pending, broken, retired;
    bool rescan;
    VarId *changed;
    size_t changed_len, changed_cap;
    uint64_t admitted_revision;
    unsigned admitted, checked;
    uint64_t posts, synchronizations, detached_states;
    uint64_t published, native_reads;
};

/* Protected by the existing libpl engine lease. This is resource metadata:
 * each semantic graph is owned by one branch client, never by this registry. */
static PettaSolverClient *petta_solver_clients;

/* The monitor stores no global variable state. Its trailed queue and attribute
 * belong to the region's graph and travel with its snapshots. Union publishes
 * the two old representatives, then retains the least owned key; later binding
 * publishes that representative, whose native aliases already follow it.
 * Implementation attributes are omitted from guest residual-goal observations. */
static bool petta_solver_monitor(CettaLibPrologRuntime *runtime) {
    if (runtime->solver_monitor_ready)
        return true;
    if (!runtime->solver_module_name) {
        size_t size = runtime->module_name_len + 32u;
        runtime->solver_module_name = cetta_malloc(size);
        if (!runtime->solver_module_name)
            return false;
        snprintf(runtime->solver_module_name, size, "%s_solver", runtime->module_name);
        atom_t name = PL_new_atom(runtime->solver_module_name);
        runtime->solver_module = name ? PL_new_module(name) : NULL;
        if (name)
            PL_unregister_atom(name);
    }
    if (!runtime->solver_module)
        return false;
    const char *name = runtime->solver_module_name;
    char source[4096];
    int length = snprintf(source, sizeof(source),
        "(assertz((observe(V,B,K):-put_attr(V,'%s',mon(B,K)))),"
        "assertz((push(B,K,V):-arg(1,B,Q),setarg(1,B,[event(K,V)|Q]))),"
        "assertz((attr_unify_hook(mon(B,K),V):-push(B,K,V),"
            "(var(V)->(get_attr(V,'%s',mon(C,L))->push(C,L,V),"
                "M is min(K,L),put_attr(V,'%s',mon(B,M));"
                "put_attr(V,'%s',mon(B,K)));true))),"
        "assertz((attribute_goals(_,G,G))),"
        "assertz((drain(B,E):-arg(1,B,E),setarg(1,B,[]))))",
        name, name, name, name);
    fid_t frame = PL_open_foreign_frame();
    term_t goal = frame ? PL_new_term_ref() : 0;
    bool ok = length > 0 && (size_t)length < sizeof(source) && goal &&
              PL_chars_to_term(source, goal) && PL_call(goal, runtime->solver_module);
    if (frame)
        PL_discard_foreign_frame(frame);
    runtime->solver_monitor_ready = ok;
    return ok;
}

static functor_t petta_solver_functor(const char *name, int arity) {
    atom_t atom = PL_new_atom(name);
    functor_t result = atom ? PL_new_functor(atom, arity) : 0;
    if (atom)
        PL_unregister_atom(atom);
    return result;
}

static term_t petta_solver_box(PettaSolverClient *client) {
    term_t box = cetta_foreign_region_variable(client->region, 0u);
    if (box && PL_is_variable(box)) {
        term_t empty = PL_new_term_ref(), value = PL_new_term_ref();
        if (!empty || !value || !PL_put_nil(empty) ||
            !PL_cons_functor(value, petta_solver_functor("box", 1), empty) ||
            !PL_unify(box, value))
            return 0;
    }
    return box;
}

static bool petta_solver_grow(void **array, size_t *capacity, size_t count,
                              size_t width) {
    if (count <= *capacity)
        return true;
    size_t next = *capacity ? *capacity : 16u;
    while (next < count) {
        if (next > SIZE_MAX / 2u)
            return false;
        next *= 2u;
    }
    if (next > SIZE_MAX / width)
        return false;
    void *grown = cetta_realloc(*array, next * width);
    if (!grown)
        return false;
    *array = grown;
    *capacity = next;
    return true;
}

static PettaSolverImage *petta_solver_image(CettaForeignRegion *region) {
    PettaSolverImage *image = cetta_malloc(sizeof(*image));
    if (!image)
        return NULL;
    image->graph = cetta_foreign_region_snapshot(region);
    image->references = 1u;
    if (!image->graph) {
        free(image);
        return NULL;
    }
    return image;
}

static void petta_solver_image_drop(PettaSolverImage *image) {
    if (image && --image->references == 0u) {
        cetta_foreign_snapshot_free(image->graph);
        free(image);
    }
}

static void petta_solver_clear_views(PettaSolverClient *client) {
    arena_reset(&client->views, (ArenaMark){0});
}

/* This copies only at a branch/engine handover. Older checkpoints which were
 * already detached reuse their immutable images. Equal marks share the state
 * before the earliest mutation at that mark. */
static bool petta_solver_detach(PettaSolverClient *client) {
    if (!client->region)
        return true;
    if (cetta_foreign_region_adopt(client->region) != CETTA_FOREIGN_OK)
        return false;
    PettaSolverImage *current = petta_solver_image(client->region);
    if (!current)
        return false;
    client->detached_states++;
    size_t end = client->history_len;
    while (end) {
        size_t first = end - 1u;
        uint32_t mark = client->history[first].mark;
        while (first && client->history[first - 1u].mark == mark)
            first--;
        PettaSolverImage *before = client->history[first].before;
        if (!before) {
            if (cetta_foreign_region_rollback(client->region, mark) != CETTA_FOREIGN_OK ||
                !(before = petta_solver_image(client->region))) {
                /* Current still owns the exact pre-detach graph. Keep it as
                 * the recovery image; a failed handover cannot expose the
                 * partially rewound graph as the current branch. */
                petta_solver_image_drop(client->image);
                client->image = current;
                client->broken = true;
                return false;
            }
            client->history[first].before = before;
            client->detached_states++;
        }
        for (size_t at = first + 1u; at < end; at++) {
            if (!client->history[at].before) {
                before->references++;
                client->history[at].before = before;
            }
        }
        end = first;
    }
    if (cetta_foreign_region_free(client->region) != CETTA_FOREIGN_OK) {
        petta_solver_image_drop(current);
        return false;
    }
    client->region = NULL;
    client->engine = NULL;
    petta_solver_image_drop(client->image);
    client->image = current;
    return true;
}

static bool petta_solver_activate(PettaSolverClient *client) {
    if (client->broken || client->retired || PL_current_query())
        return false;
    if (!petta_solver_monitor(client->runtime))
        return false;
    PL_engine_t engine = PL_current_engine();
    if (client->region && client->engine != engine)
        return false;
    for (PettaSolverClient *other = petta_solver_clients; other; other = other->next) {
        if (other != client && other->region && other->engine == engine &&
            !petta_solver_detach(other))
            return false;
    }
    if (client->region)
        return cetta_foreign_region_adopt(client->region) == CETTA_FOREIGN_OK;
    client->region = client->image
        ? cetta_foreign_region_restore(client->image->graph)
        : cetta_foreign_region_open();
    client->engine = client->region ? engine : NULL;
    return client->region != NULL;
}

static void petta_solver_dispose(PettaSolverClient *client) {
    if (getenv("CETTA_TRACE_SOLVER_CLIENT"))
        fprintf(stderr, "[petta-solver] posts=%llu syncs=%llu detached-states=%llu published=%llu native-reads=%llu\n",
            (unsigned long long)client->posts,
            (unsigned long long)client->synchronizations,
            (unsigned long long)client->detached_states,
            (unsigned long long)client->published,
            (unsigned long long)client->native_reads);
    PettaSolverClient **link = &petta_solver_clients;
    while (*link && *link != client)
        link = &(*link)->next;
    if (*link)
        *link = client->next;
    petta_solver_image_drop(client->image);
    for (size_t at = 0; at < client->history_len; at++)
        petta_solver_image_drop(client->history[at].before);
    arena_free(&client->views);
    arena_free(&client->arena);
    cetta_var_index_free(&client->index);
    free(client->vars);
    free(client->history);
    free(client->changed);
    free(client);
}

static void petta_solver_collect_retired(void) {
    for (PettaSolverClient *client = petta_solver_clients, *next; client; client = next) {
        next = client->next;
        if (!client->retired)
            continue;
        if (client->region) {
            if (client->engine != PL_current_engine() ||
                cetta_foreign_region_adopt(client->region) != CETTA_FOREIGN_OK ||
                cetta_foreign_region_free(client->region) != CETTA_FOREIGN_OK)
                continue;
            client->region = NULL;
        }
        petta_solver_dispose(client);
    }
}

static void petta_solver_free(void *state) {
    PettaSolverClient *client = state;
    bool claimed = false;
    if (!petta_libpl_enter(&claimed)) {
        client->retired = true;
        return;
    }
    client->retired = true;
    petta_solver_collect_retired();
    petta_libpl_leave(claimed);
}

static PettaSolverClient *petta_solver_new(CettaLibPrologRuntime *runtime) {
    PettaSolverClient *client = cetta_malloc(sizeof(*client));
    if (!client)
        return NULL;
    *client = (PettaSolverClient){.runtime = runtime, .next_key = 1u};
    arena_init_detached(&client->arena);
    arena_init_detached(&client->views);
    arena_set_block_capacity(&client->arena, 4096u);
    arena_set_block_capacity(&client->views, 4096u);
    client->next = petta_solver_clients;
    petta_solver_clients = client;
    return client;
}

static bool petta_solver_prepare_detach(void *state) {
    PettaSolverClient *client = state;
    bool claimed = false;
    if (!petta_libpl_enter(&claimed))
        return false;
    bool ok = !client->broken && petta_solver_detach(client);
    petta_libpl_leave(claimed);
    return ok;
}

static void *petta_solver_clone(const void *state) {
    PettaSolverClient *parent = (PettaSolverClient *)state;
    bool claimed = false;
    if (!petta_libpl_enter(&claimed))
        return NULL;
    PettaSolverClient *child = NULL;
    if (!parent->broken && parent->first == parent->len) {
        child = petta_solver_new(parent->runtime);
    } else if (!parent->broken && (!parent->region || petta_solver_activate(parent))) {
        PettaSolverImage *image = parent->region ? petta_solver_image(parent->region)
                                                : parent->image;
        if (parent->region && image)
            parent->detached_states++;
        if (!parent->region && image)
            image->references++;
        if ((!parent->region || image) && (child = petta_solver_new(parent->runtime))) {
            child->image = image;
            child->next_key = parent->next_key;
            child->pending = parent->pending;
            child->rescan = parent->pending;
            size_t count = parent->len - parent->first;
            if (!petta_solver_grow((void **)&child->vars, &child->cap, count,
                                   sizeof(*child->vars)))
                child->broken = true;
            for (size_t at = parent->first; !child->broken && at < parent->len; at++) {
                PettaSolverVariable variable = parent->vars[at];
                variable.queued = false;
                variable.prototype = atom_deep_copy(&child->arena, variable.prototype);
                if (!variable.prototype) {
                    child->broken = true;
                    break;
                }
                child->vars[child->len++] = variable;
            }
            if (!cetta_var_index_note_records(&child->index, child->vars,
                                               sizeof(*child->vars), child->len))
                child->broken = true;
            if (child->broken) {
                petta_solver_dispose(child);
                child = NULL;
            }
        } else {
            petta_solver_image_drop(image);
        }
    }
    petta_libpl_leave(claimed);
    return child;
}

static bool petta_solver_checkpoint(PettaSolverClient *client, uint32_t mark) {
    if (!petta_solver_activate(client) ||
        !petta_solver_grow((void **)&client->history, &client->history_cap,
                           client->history_len + 1u, sizeof(*client->history)) ||
        cetta_foreign_region_checkpoint(client->region, mark) != CETTA_FOREIGN_OK)
        return false;
    client->history[client->history_len++] = (PettaSolverCheckpoint){
        .mark = mark, .variables = client->len, .first = client->first,
        .storage = arena_mark(&client->arena),
    };
    petta_solver_image_drop(client->image);
    client->image = NULL;
    petta_solver_clear_views(client);
    return true;
}

static void petta_solver_rollback_locked(PettaSolverClient *client, uint32_t mark) {
    size_t first = client->history_len;
    while (first && client->history[first - 1u].mark >= mark)
        first--;
    if (first < client->history_len) {
        PettaSolverCheckpoint before = client->history[first];
        bool ok = true;
        if (before.before) {
            before.before->references++;
            if (client->region) {
                ok = cetta_foreign_region_adopt(client->region) == CETTA_FOREIGN_OK &&
                     cetta_foreign_region_free(client->region) == CETTA_FOREIGN_OK;
                if (ok) {
                    client->region = NULL;
                    client->engine = NULL;
                }
            }
            if (ok) {
                petta_solver_image_drop(client->image);
                client->image = before.before;
            } else {
                petta_solver_image_drop(before.before);
            }
        } else {
            ok = client->region && petta_solver_activate(client) &&
                 cetta_foreign_region_rollback(client->region, mark) == CETTA_FOREIGN_OK;
        }
        if (!ok) {
            client->broken = true;
            return;
        }
        for (size_t at = first; at < client->history_len; at++)
            petta_solver_image_drop(client->history[at].before);
        client->history_len = first;
        client->len = before.variables;
        client->first = before.first;
        arena_reset(&client->arena, before.storage);
        cetta_var_index_free(&client->index);
        if (!cetta_var_index_note_records(&client->index, client->vars ? client->vars + client->first : NULL,
                                           sizeof(*client->vars), client->len - client->first))
            client->broken = true;
    }
    /* A native binding older than the restored propagation checkpoint may
     * remain. Reconcile it before the next guest step rather than dropping a
     * consumed wake notification. */
    client->pending = client->len > client->first;
    client->rescan = client->pending;
    client->changed_len = 0u;
    for (size_t at = client->first; at < client->len; at++)
        client->vars[at].queued = false;
    petta_solver_clear_views(client);
}

static void petta_solver_rollback(void *state, uint32_t mark) {
    PettaSolverClient *client = state;
    bool claimed = false;
    if (!petta_libpl_enter(&claimed)) {
        client->broken = true;
        return;
    }
    petta_solver_rollback_locked(client, mark);
    petta_libpl_leave(claimed);
}

static void petta_solver_commit(void *state) {
    PettaSolverClient *client = state;
    bool claimed = false;
    if (!petta_libpl_enter(&claimed)) {
        client->broken = true;
        return;
    }
    if (client->region && (!petta_solver_activate(client) ||
        cetta_foreign_region_commit(client->region) != CETTA_FOREIGN_OK))
        client->broken = true;
    for (size_t at = 0; at < client->history_len; at++)
        petta_solver_image_drop(client->history[at].before);
    client->history_len = 0u;
    if (!client->broken && client->first == client->len) {
        if (client->region && cetta_foreign_region_free(client->region) != CETTA_FOREIGN_OK)
            client->broken = true;
        if (!client->broken) {
            client->region = NULL;
            client->engine = NULL;
            petta_solver_image_drop(client->image);
            client->image = NULL;
            client->len = client->first = 0u;
            client->pending = false;
            client->rescan = false;
            client->changed_len = 0u;
            cetta_var_index_free(&client->index);
            arena_reset(&client->arena, (ArenaMark){0});
        }
    }
    petta_solver_clear_views(client);
    petta_libpl_leave(claimed);
}

static void petta_solver_rebase(void *state, const uint32_t *kept, uint32_t count) {
    PettaSolverClient *client = state;
    if (!count) {
        petta_solver_commit(client);
        return;
    }
    bool claimed = false;
    if (!petta_libpl_enter(&claimed)) {
        client->broken = true;
        return;
    }
    if (client->region && (!petta_solver_activate(client) ||
        cetta_foreign_region_rebase(client->region, kept, count) != CETTA_FOREIGN_OK))
        client->broken = true;
    size_t first = 0u, position = 0u;
    while (first < client->history_len && client->history[first].mark < kept[0])
        petta_solver_image_drop(client->history[first++].before);
    for (size_t at = first; at < client->history_len; at++) {
        while (position + 1u < count && kept[position + 1u] <= client->history[at].mark)
            position++;
        client->history[at - first] = client->history[at];
        client->history[at - first].mark = (uint32_t)position;
    }
    client->history_len -= first;
    petta_libpl_leave(claimed);
}

/* The search publishes its live rollback horizons only between guest steps.
 * Equal rounded marks need one pre-state: the earliest operation in that
 * segment. Physical prefix frames remain anchors until safe to close. */
static bool petta_solver_frontier(void *state, const uint32_t *kept, uint32_t count) {
    PettaSolverClient *client = state;
    if (client->broken || (count && !kept))
        return false;
    if (!client->history_len)
        return true;
    if (!count) {
        petta_solver_commit(client);
        return !client->broken;
    }
    for (uint32_t at = 1u; at < count; at++)
        if (kept[at] <= kept[at - 1u])
            return false;
    bool claimed = false;
    if (!petta_libpl_enter(&claimed))
        return false;
    bool ok = !client->region || (petta_solver_activate(client) &&
        cetta_foreign_region_frontier(client->region, kept, count) == CETTA_FOREIGN_OK);
    if (ok) {
        size_t written = 0u, position = 0u;
        for (size_t at = 0u; at < client->history_len; at++) {
            PettaSolverCheckpoint checkpoint = client->history[at];
            if (checkpoint.mark < kept[0]) {
                petta_solver_image_drop(checkpoint.before);
                continue;
            }
            while (position + 1u < count && kept[position + 1u] <= checkpoint.mark)
                position++;
            checkpoint.mark = kept[position];
            if (written && client->history[written - 1u].mark == checkpoint.mark) {
                petta_solver_image_drop(checkpoint.before);
            } else {
                client->history[written++] = checkpoint;
            }
        }
        client->history_len = written;
    }
    petta_libpl_leave(claimed);
    return ok;
}

static size_t petta_solver_find(const PettaSolverClient *client, VarId id) {
    size_t at = cetta_var_index_find_records(&client->index,
        client->vars ? client->vars + client->first : NULL,
        sizeof(*client->vars), client->len - client->first, id);
    return at == SIZE_MAX ? SIZE_MAX : at + client->first;
}

static term_t petta_solver_slot(void *context, Atom *variable) {
    PettaSolverClient *client = context;
    size_t at = petta_solver_find(client, variable->var_id);
    bool added = at == SIZE_MAX;
    if (at == SIZE_MAX) {
        if (!client->next_key || client->next_key == UINT64_MAX ||
            !petta_solver_grow((void **)&client->vars, &client->cap, client->len + 1u,
                               sizeof(*client->vars)))
            return 0;
        Atom *owned = atom_deep_copy(&client->arena, variable);
        if (!owned)
            return 0;
        at = client->len++;
        client->vars[at] = (PettaSolverVariable){
            .id = variable->var_id, .prototype = owned, .key = client->next_key++};
        if (!cetta_var_index_note_records(&client->index, client->vars + client->first,
                                           sizeof(*client->vars), client->len - client->first))
            return 0;
    }
    term_t slot = cetta_foreign_region_variable(client->region, client->vars[at].key);
    if (added && slot && PL_is_variable(slot)) {
        term_t arguments = PL_new_term_refs(3);
        term_t box = petta_solver_box(client);
        if (!arguments || !box || !PL_put_term(arguments, slot) ||
            !PL_put_term(arguments + 1, box) ||
            !PL_put_uint64(arguments + 2, client->vars[at].key) ||
            !PL_call_predicate(client->runtime->solver_module, PL_Q_NODEBUG,
                PL_predicate("observe", 3, client->runtime->solver_module_name), arguments))
            return 0;
    }
    return slot;
}

static PettaSolverVariable *petta_solver_key(PettaSolverClient *client, uint64_t key) {
    size_t low = client->first, high = client->len;
    while (low < high) {
        size_t middle = low + (high - low) / 2u;
        if (client->vars[middle].key < key)
            low = middle + 1u;
        else
            high = middle;
    }
    return low < client->len && client->vars[low].key == key ? &client->vars[low] : NULL;
}

static Atom *petta_solver_native_variable(void *context, Arena *arena, term_t variable) {
    PettaSolverClient *client = context;
    term_t attrs = PL_new_term_ref(), part = PL_new_term_ref(), value = PL_new_term_ref();
    if (!attrs || !part || !value || !PL_get_attr(variable, attrs))
        return NULL;
    while (!PL_get_nil(attrs)) {
        char *module = NULL;
        if (!PL_get_arg(1, attrs, part) || !PL_get_atom_chars(part, &module))
            return NULL;
        if (strcmp(module, client->runtime->solver_module_name) == 0) {
            uint64_t key;
            if (!PL_get_arg(2, attrs, value) || !PL_get_arg(2, value, part) ||
                !PL_get_uint64(part, &key))
                return NULL;
            PettaSolverVariable *owned = petta_solver_key(client, key);
            return owned ? atom_var_like(arena, owned->prototype, owned->id) : NULL;
        }
        if (!PL_get_arg(3, attrs, part) || !PL_put_term(attrs, part))
            return NULL;
    }
    return NULL;
}

/* Drain after propagation, inside the selected query. setarg/3 is trailed;
 * neither a failed call nor branch rollback consumes another branch's events. */
static term_t petta_solver_query(PettaSolverClient *client, term_t goal) {
    term_t box = petta_solver_box(client), events = PL_new_term_ref();
    term_t arguments = PL_new_term_refs(2), drain = PL_new_term_ref();
    term_t qualified = PL_new_term_ref(), parts = PL_new_term_refs(2);
    if (!box || !events || !arguments || !drain || !qualified || !parts ||
        !PL_put_term(arguments, box) || !PL_put_term(arguments + 1, events) ||
        !PL_cons_functor_v(drain, petta_solver_functor("drain", 2), arguments) ||
        !PL_put_atom_chars(parts, client->runtime->solver_module_name) ||
        !PL_put_term(parts + 1, drain) ||
        !PL_cons_functor_v(qualified, petta_solver_functor(":", 2), parts) ||
        !PL_put_term(parts, goal) || !PL_put_term(parts + 1, qualified) ||
        !PL_cons_functor_v(goal, petta_solver_functor(",", 2), parts))
        return 0;
    return events;
}

static bool petta_solver_changes(PettaSolverClient *client, Arena *arena,
                                 term_t events, PettaLibplVarMap *variables,
                                 PettaLibplBackVarMap *back, Bindings *bindings) {
    term_t list = PL_new_term_ref(), head = PL_new_term_ref(), key_term = PL_new_term_ref();
    term_t value_term = PL_new_term_ref();
    if (!list || !head || !key_term || !value_term || !PL_put_term(list, events))
        return false;
    Atom **prototypes = NULL, **values = NULL;
    size_t count = 0u, prototype_cap = 0u, value_cap = 0u;
    bool ok = true;
    while (!PL_get_nil(list)) {
        uint64_t key;
        if (!PL_get_list(list, head, list) || !PL_get_arg(1, head, key_term) ||
            !PL_get_uint64(key_term, &key) || !PL_get_arg(2, head, value_term))
            { ok = false; break; }
        PettaSolverVariable *variable = petta_solver_key(client, key);
        if (!variable)
            continue; /* A released epoch is no longer observable by this owner. */
        Atom *value = petta_libpl_from_term(arena, value_term, variables, back, 0u);
        if (!value || (value->kind != ATOM_VAR || value->var_id != variable->id)) {
            Atom *prototype = value ? atom_var_like(arena, variable->prototype, variable->id) : NULL;
            if (!prototype || count == UINT32_MAX ||
                !petta_solver_grow((void **)&prototypes, &prototype_cap, count + 1u,
                                   sizeof(*prototypes)) ||
                !petta_solver_grow((void **)&values, &value_cap, count + 1u, sizeof(*values))) {
                ok = false;
                break;
            }
            prototypes[count] = prototype;
            values[count++] = value;
        }
    }
    ok = ok && bindings_add_vars(bindings, prototypes, values, (uint32_t)count);
    if (ok)
        client->published += count;
    free(prototypes);
    free(values);
    return ok;
}

static bool petta_solver_emit(PettaSolverClient *client, Arena *arena, term_t result,
                              term_t events, PettaLibplVarMap *variables,
                              OutcomeSet *outcomes) {
    PettaLibplBackVarMap back = {0};
    Bindings bindings = {0};
    bool ok = petta_libpl_solution_bindings(arena, variables, &back, &bindings);
    if (ok)
        ok = petta_solver_changes(client, arena, events, variables, &back, &bindings);
    Atom *value = ok ? petta_libpl_from_term(arena, result, variables, &back, 0u) : NULL;
    CettaCount before = outcomes->len;
    if (value)
        outcome_set_add(outcomes, value, &bindings);
    bindings_free(&bindings);
    petta_libpl_back_map_free(&back);
    return value && outcomes->len == before + 1u;
}

static bool petta_solver_map(PettaSolverClient *client, PettaLibplVarMap *variables) {
    for (size_t at = client->first; at < client->len; at++) {
        if (!petta_libpl_var_add(variables, client->vars[at].prototype))
            return false;
    }
    return true;
}

static CettaDelaySyncStatus petta_solver_finish(PettaSolverClient *client,
                                               bool commit, Arena *arena,
                                               Atom **raised) {
    record_t exception = 0;
    CettaForeignStatus status = cetta_foreign_region_finish(client->region, commit, &exception);
    if (exception) {
        fid_t frame = PL_open_foreign_frame();
        term_t term = frame ? PL_new_term_ref() : 0;
        if (term && PL_recorded(exception, term))
            *raised = petta_libpl_exception_value(arena, term);
        if (frame)
            PL_discard_foreign_frame(frame);
        PL_erase(exception);
    }
    return status == CETTA_FOREIGN_FAULT && *raised ? CETTA_DELAY_SYNC_RAISED
         : status == CETTA_FOREIGN_OK ? CETTA_DELAY_SYNC_READY : CETTA_DELAY_SYNC_CAPACITY;
}

static bool petta_solver_watches(const void *state, VarId var) {
    return petta_solver_find(state, var) != SIZE_MAX;
}

static bool petta_solver_bound(void *state, uint32_t mark, VarId var) {
    (void)mark;
    PettaSolverClient *client = state;
    size_t at = petta_solver_find(client, var);
    if (at == SIZE_MAX)
        return true;
    if (!client->vars[at].queued) {
        if (!petta_solver_grow((void **)&client->changed, &client->changed_cap,
                               client->changed_len + 1u, sizeof(*client->changed)))
            return false;
        client->changed[client->changed_len++] = var;
        client->vars[at].queued = true;
    }
    client->pending = true;
    return true;
}

static bool petta_solver_withdraw(void *state, uint32_t mark, VarId var) {
    (void)mark;
    /* Every incoming residual answer first materializes the retained epoch.
     * An individual retained-variable withdrawal would omit its neighbours. */
    return !petta_solver_watches(state, var);
}

static bool petta_solver_reset(void *state, uint32_t mark) {
    PettaSolverClient *client = state;
    if (client->first == client->len)
        return !client->broken;
    bool claimed = false;
    if (!petta_libpl_enter(&claimed))
        return false;
    bool ok = petta_solver_checkpoint(client, mark);
    if (ok) {
        client->first = client->len;
        cetta_var_index_free(&client->index);
        client->pending = false;
        client->rescan = false;
        client->changed_len = 0u;
    }
    petta_libpl_leave(claimed);
    return ok;
}

static bool petta_solver_pending(const void *state) {
    const PettaSolverClient *client = state;
    return client->pending || client->broken;
}

static CettaDelaySyncStatus petta_solver_sync(void *state, uint32_t mark,
                                             const Bindings *bindings, Arena *arena,
                                             Bindings *delta, Atom **raised) {
    PettaSolverClient *client = state;
    bool claimed = false;
    if (!petta_libpl_enter(&claimed))
        return CETTA_DELAY_SYNC_CAPACITY;
    CettaLibPrologRuntime *previous = g_petta_libpl_active_runtime;
    g_petta_libpl_active_runtime = client->runtime;
    CettaDelaySyncStatus status = CETTA_DELAY_SYNC_CAPACITY;
    bool started = petta_solver_checkpoint(client, mark) &&
                   cetta_foreign_region_begin(client->region) == CETTA_FOREIGN_OK;
    PettaLibplVarMap variables = {.slot = petta_solver_slot, .slot_context = client,
                                 .native_variable = petta_solver_native_variable};
    PettaLibplBackVarMap back = {0};
    term_t goal = started ? PL_new_term_ref() : 0;
    bool ok = goal && PL_put_atom_chars(goal, "true");
    atom_t equal_name = PL_new_atom("="), comma_name = PL_new_atom(",");
    functor_t equal = PL_new_functor(equal_name, 2), comma = PL_new_functor(comma_name, 2);
    PL_unregister_atom(equal_name);
    PL_unregister_atom(comma_name);
    size_t visits = client->rescan ? client->len - client->first : client->changed_len;
    for (size_t visit = 0; ok && visit < visits; visit++) {
        size_t at = client->rescan ? client->first + visit
                                  : petta_solver_find(client, client->changed[visit]);
        if (at == SIZE_MAX)
            continue;
        client->native_reads++;
        Atom *prototype = client->vars[at].prototype;
        Atom *value = bindings_apply_if_vars(bindings, arena, prototype);
        term_t pair = PL_new_term_refs(2), equation = PL_new_term_ref(), parts = PL_new_term_refs(2);
        ok = value && pair && equation && parts &&
             petta_libpl_to_term(prototype, pair, &variables, 0u) &&
             petta_libpl_to_term(value, pair + 1, &variables, 0u);
        if (ok && PL_compare(pair, pair + 1) != 0) {
            ok = PL_cons_functor(equation, equal, pair, pair + 1) &&
                 PL_put_term(parts, goal) && PL_put_term(parts + 1, equation) &&
                 PL_cons_functor_v(goal, comma, parts);
        }
    }
    term_t events = ok ? petta_solver_query(client, goal) : 0;
    ok = ok && events &&
         cetta_foreign_region_start(client->region, client->runtime->module,
             PL_predicate("call", 1, client->runtime->module_name), goal) == CETTA_FOREIGN_OK;
    int result = ok ? cetta_foreign_region_next(client->region) : -1;
    if (ok)
        client->synchronizations++;
    bool solved = result == PL_S_TRUE || result == PL_S_LAST;
    bool read = solved && petta_libpl_solution_bindings(arena, &variables, &back, delta) &&
                petta_solver_changes(client, arena, events, &variables, &back, delta);
    if (started) {
        status = petta_solver_finish(client, read, arena, raised);
        if (status == CETTA_DELAY_SYNC_READY && !read)
            status = result == PL_S_FALSE ? CETTA_DELAY_SYNC_FAILED : CETTA_DELAY_SYNC_CAPACITY;
    }
    petta_libpl_var_map_free(&variables);
    petta_libpl_back_map_free(&back);
    for (size_t visit = 0; visit < client->changed_len; visit++) {
        size_t at = petta_solver_find(client, client->changed[visit]);
        if (at != SIZE_MAX)
            client->vars[at].queued = false;
    }
    client->changed_len = 0u;
    client->rescan = false;
    client->pending = false;
    if (status != CETTA_DELAY_SYNC_READY && client->history_len)
        petta_solver_rollback_locked(client, mark);
    if (status == CETTA_DELAY_SYNC_READY)
        client->pending = false;
    g_petta_libpl_active_runtime = previous;
    petta_libpl_leave(claimed);
    return status;
}

static bool petta_solver_roots(const void *state,
                               bool (*visit)(void *, Atom *), void *context) {
    const PettaSolverClient *client = state;
    for (size_t at = 0; at < client->len; at++) {
        if (!visit(context, client->vars[at].prototype))
            return false;
    }
    return true;
}

static bool petta_solver_goals(const void *state,
                               bool (*visit)(void *, const CettaDelayGoal *), void *context) {
    PettaSolverClient *client = (PettaSolverClient *)state;
    if (client->first == client->len)
        return !client->broken;
    bool claimed = false;
    if (!petta_libpl_enter(&claimed))
        return false;
    petta_solver_clear_views(client);
    CettaLibPrologRuntime *previous = g_petta_libpl_active_runtime;
    g_petta_libpl_active_runtime = client->runtime;
    bool started = petta_solver_activate(client) &&
                   cetta_foreign_region_begin(client->region) == CETTA_FOREIGN_OK;
    PettaLibplVarMap variables = {.slot = petta_solver_slot, .slot_context = client,
                                 .native_variable = petta_solver_native_variable};
    PettaLibplBackVarMap back = {0};
    Atom *payload = NULL;
    bool ok = started && petta_solver_map(client, &variables) &&
              petta_libpl_known_variables(&variables, &back) &&
              petta_libpl_delay_answer(&client->views, &variables, &back, &payload);
    if (started && cetta_foreign_region_finish(client->region, false, NULL) != CETTA_FOREIGN_OK)
        ok = false;
    petta_libpl_var_map_free(&variables);
    petta_libpl_back_map_free(&back);
    Atom *components = payload ? payload->expr.elems[1] : NULL;
    for (CettaExprIndex at = 0; ok && components && at < components->expr.len; at++) {
        Atom *component = components->expr.elems[at];
        if (!component || component->kind != ATOM_EXPR || component->expr.len < 1u) {
            ok = false;
            break;
        }
        CettaDelayGoal *goal = arena_alloc(&client->views, sizeof(*goal));
        uint32_t count = (uint32_t)component->expr.len - 1u;
        VarId *vars = count ? arena_alloc(&client->views, (size_t)count * sizeof(*vars)) : NULL;
        if (!goal || (count && !vars)) {
            ok = false;
            break;
        }
        for (uint32_t v = 0; v < count; v++)
            vars[v] = component->expr.elems[v + 1u]->var_id;
        *goal = (CettaDelayGoal){CETTA_DELAY_CLIENT_PROLOG, component->expr.elems[0], vars, count, true};
        ok = visit(context, goal);
    }
    g_petta_libpl_active_runtime = previous;
    petta_libpl_leave(claimed);
    return ok;
}

static const CettaDelayOwnerOps petta_solver_ops = {
    .client = CETTA_DELAY_CLIENT_PROLOG, .clone = petta_solver_clone,
    .free = petta_solver_free, .detach = petta_solver_prepare_detach,
    .frontier = petta_solver_frontier,
    .rollback = petta_solver_rollback,
    .commit = petta_solver_commit, .rebase = petta_solver_rebase,
    .watches = petta_solver_watches, .bound = petta_solver_bound,
    .withdraw = petta_solver_withdraw, .reset = petta_solver_reset,
    .pending = petta_solver_pending, .sync = petta_solver_sync,
    .roots = petta_solver_roots, .goals = petta_solver_goals,
};

static int petta_solver_relation(const Atom *expression) {
    static const char *names[] = {"#+", "#-", "#*", "#div", "#//", "#mod", "#min", "#max"};
    if (!expression || expression->kind != ATOM_EXPR || expression->expr.len != 3u ||
        expression->expr.elems[0]->kind != ATOM_SYMBOL)
        return -1;
    for (int at = 0; at < 8; at++) {
        if (petta_libpl_atom_is_symbol(expression->expr.elems[0], names[at]))
            return at;
    }
    return -1;
}

/* The definition must still be the single reference arithmetic clause. A
 * user import, added clause or changed body leaves the ordinary Prolog route
 * authoritative. Cache only within the bridge's mutation revision. */
static bool petta_solver_admitted(PettaSolverClient *client, int relation) {
    static const char *names[] = {"#+", "#-", "#*", "#div", "#//", "#mod", "#min", "#max"};
    static const char *rhs[] = {"A+B", "A-B", "A*B", "A div B", "A//B", "A mod B", "min(A,B)", "max(A,B)"};
    uint64_t revision = atomic_load_explicit(&client->runtime->capability_revision, memory_order_acquire);
    if (revision != client->admitted_revision) {
        client->admitted_revision = revision;
        client->admitted = client->checked = 0u;
    }
    unsigned bit = 1u << relation;
    if (!(client->checked & bit)) {
        char source[512];
        int length = snprintf(source, sizeof(source),
            "(predicate_property('%s'(_,_,_), number_of_clauses(1)), "
            "clause('%s'(A,B,R), Body), Body =@= '#='(R,%s))", names[relation], names[relation], rhs[relation]);
        fid_t frame = PL_open_foreign_frame();
        term_t goal = frame ? PL_new_term_ref() : 0;
        bool ok = length > 0 && (size_t)length < sizeof(source) && goal &&
                  PL_chars_to_term(source, goal) && PL_call(goal, client->runtime->module);
        if (frame)
            PL_discard_foreign_frame(frame);
        client->checked |= bit;
        if (ok)
            client->admitted |= bit;
    }
    return (client->admitted & bit) != 0u;
}

/* Called before the boundary's temporary foreign frame is opened. */
static bool petta_solver_try_call(CettaLibPrologRuntime *runtime, Arena *arena,
                                 Atom *expression, Atom *expected, OutcomeSet *outcomes,
                                 Atom **raised, const CettaDelayView *view, bool *handled) {
    *handled = false;
    bool predicate = expression && expression->kind == ATOM_EXPR &&
        expression->expr.len == 2u && expression->expr.elems[0]->kind == ATOM_SYMBOL &&
        petta_semantics_form(expression->expr.elems[0]->sym_id) == PETTA_FORM_TRANSLATE_PREDICATE;
    Atom *source = predicate ? expression->expr.elems[1] : expression;
    Atom *result_value = expected;
    if (predicate) {
        if (!source || source->kind != ATOM_EXPR || source->expr.len != 4u)
            return true;
        result_value = source->expr.elems[3];
        source = atom_expr(arena, source->expr.elems, 3u);
        if (!source)
            return false;
    }
    int relation = petta_solver_relation(source);
    if (relation < 0 || !view || !view->acquire || !view->mark || PL_current_query())
        return true;
    PettaLibplImport *entry = petta_libpl_find_import(runtime, source->expr.elems[0]->sym_id);
    if (!predicate && (!entry || !entry->reference_stdlib || entry->auto_resolved))
        return true;
    CettaDelayService *service = view->acquire(view->context);
    if (!service)
        return false;
    /* Serialized goals can belong to arbitrary attribute clients. A call
     * connected to any of them keeps the original all-purpose bridge. */
    if (cetta_delay_serialized(service))
        return true;
    PettaSolverClient *client = cetta_delay_owner(service, CETTA_DELAY_CLIENT_PROLOG);
    if (!client) {
        client = petta_solver_new(runtime);
        if (!client || !cetta_delay_owner_install(service, &petta_solver_ops, client)) {
            if (client)
                petta_solver_dispose(client);
            return false;
        }
    }
    if (client->runtime != runtime || !petta_solver_admitted(client, relation))
        return true;
    *handled = true;
    uint32_t mark = view->mark(view->context);
    if (mark == UINT32_MAX || !petta_solver_checkpoint(client, mark))
        return false;
    bool started = cetta_foreign_region_begin(client->region) == CETTA_FOREIGN_OK;
    PettaLibplVarMap variables = {.slot = petta_solver_slot, .slot_context = client,
                                 .native_variable = petta_solver_native_variable};
    term_t arguments = started ? PL_new_term_refs(3) : 0;
    term_t goal = started ? PL_new_term_ref() : 0;
    term_t result_term = predicate && started ? PL_new_term_ref() : arguments + 2;
    bool ok = arguments && goal && result_term &&
              petta_libpl_to_term(source->expr.elems[1], arguments, &variables, 0u) &&
              petta_libpl_to_term(source->expr.elems[2], arguments + 1, &variables, 0u) &&
              petta_libpl_to_term(result_value, arguments + 2, &variables, 0u) &&
              (!predicate || PL_put_atom_chars(result_term, "true"));
    static const char *names[] = {"#+", "#-", "#*", "#div", "#//", "#mod", "#min", "#max"};
    atom_t name = PL_new_atom(names[relation]);
    functor_t functor = name ? PL_new_functor(name, 3) : 0;
    ok = ok && functor && PL_cons_functor_v(goal, functor, arguments);
    if (name)
        PL_unregister_atom(name);
    term_t events = ok ? petta_solver_query(client, goal) : 0;
    ok = ok && events && cetta_foreign_region_start(client->region, runtime->module,
        PL_predicate("call", 1, runtime->module_name), goal) == CETTA_FOREIGN_OK;
    int result = ok ? cetta_foreign_region_next(client->region) : -1;
    bool solved = result == PL_S_TRUE || result == PL_S_LAST;
    bool emitted = solved && petta_solver_emit(client, arena, result_term, events,
                                              &variables, outcomes);
    if (solved)
        client->posts++;
    CettaDelaySyncStatus status = started ? petta_solver_finish(client, emitted, arena, raised)
                                         : CETTA_DELAY_SYNC_CAPACITY;
    petta_libpl_var_map_free(&variables);
    if (!emitted || status != CETTA_DELAY_SYNC_READY)
        petta_solver_rollback_locked(client, mark);
    return status == CETTA_DELAY_SYNC_RAISED ||
           (status == CETTA_DELAY_SYNC_READY && (emitted || result == PL_S_FALSE));
}

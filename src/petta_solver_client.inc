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
    uint64_t admitted_revision;
    unsigned admitted, checked;
    uint64_t posts, synchronizations, detached_states;
};

/* Protected by the existing libpl engine lease. This is resource metadata:
 * each semantic graph is owned by one branch client, never by this registry. */
static PettaSolverClient *petta_solver_clients;

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
        fprintf(stderr, "[petta-solver] posts=%llu syncs=%llu detached-states=%llu\n",
            (unsigned long long)client->posts,
            (unsigned long long)client->synchronizations,
            (unsigned long long)client->detached_states);
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
            size_t count = parent->len - parent->first;
            if (!petta_solver_grow((void **)&child->vars, &child->cap, count,
                                   sizeof(*child->vars)))
                child->broken = true;
            for (size_t at = parent->first; !child->broken && at < parent->len; at++) {
                PettaSolverVariable variable = parent->vars[at];
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
        if (!cetta_var_index_note_records(&client->index, client->vars + client->first,
                                           sizeof(*client->vars), client->len - client->first))
            client->broken = true;
    }
    /* A native binding older than the restored propagation checkpoint may
     * remain. Reconcile it before the next guest step rather than dropping a
     * consumed wake notification. */
    client->pending = client->len > client->first;
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

static size_t petta_solver_find(const PettaSolverClient *client, VarId id) {
    size_t at = cetta_var_index_find_records(&client->index,
        client->vars + client->first, sizeof(*client->vars), client->len - client->first, id);
    return at == SIZE_MAX ? SIZE_MAX : at + client->first;
}

static term_t petta_solver_slot(void *context, Atom *variable) {
    PettaSolverClient *client = context;
    size_t at = petta_solver_find(client, variable->var_id);
    if (at == SIZE_MAX) {
        if (!client->next_key || client->next_key == UINT64_MAX ||
            !petta_solver_grow((void **)&client->vars, &client->cap, client->len + 1u,
                               sizeof(*client->vars)))
            return 0;
        Atom *owned = atom_deep_copy(&client->arena, variable);
        if (!owned)
            return 0;
        at = client->len++;
        client->vars[at] = (PettaSolverVariable){variable->var_id, owned, client->next_key++};
        if (!cetta_var_index_note_records(&client->index, client->vars + client->first,
                                           sizeof(*client->vars), client->len - client->first))
            return 0;
    }
    return cetta_foreign_region_variable(client->region, client->vars[at].key);
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
    (void)mark; (void)var;
    ((PettaSolverClient *)state)->pending = true;
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
    PettaLibplVarMap variables = {.slot = petta_solver_slot, .slot_context = client};
    PettaLibplBackVarMap back = {0};
    term_t goal = started ? PL_new_term_ref() : 0;
    bool ok = goal && PL_put_atom_chars(goal, "true");
    atom_t equal_name = PL_new_atom("="), comma_name = PL_new_atom(",");
    functor_t equal = PL_new_functor(equal_name, 2), comma = PL_new_functor(comma_name, 2);
    PL_unregister_atom(equal_name);
    PL_unregister_atom(comma_name);
    for (size_t at = client->first; ok && at < client->len; at++) {
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
    ok = ok && petta_solver_map(client, &variables) &&
         cetta_foreign_region_start(client->region, client->runtime->module,
             PL_predicate("call", 1, client->runtime->module_name), goal) == CETTA_FOREIGN_OK;
    int result = ok ? cetta_foreign_region_next(client->region) : -1;
    if (ok)
        client->synchronizations++;
    bool solved = result == PL_S_TRUE || result == PL_S_LAST;
    bool read = solved && petta_libpl_solution_bindings(arena, &variables, &back, delta);
    if (started) {
        status = petta_solver_finish(client, read, arena, raised);
        if (status == CETTA_DELAY_SYNC_READY && !read)
            status = result == PL_S_FALSE ? CETTA_DELAY_SYNC_FAILED : CETTA_DELAY_SYNC_CAPACITY;
    }
    petta_libpl_var_map_free(&variables);
    petta_libpl_back_map_free(&back);
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
    PettaLibplVarMap variables = {.slot = petta_solver_slot, .slot_context = client};
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
    .free = petta_solver_free, .rollback = petta_solver_rollback,
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
    int relation = petta_solver_relation(expression);
    if (relation < 0 || !view || !view->acquire || !view->mark || PL_current_query())
        return true;
    PettaLibplImport *entry = petta_libpl_find_import(runtime, expression->expr.elems[0]->sym_id);
    if (!entry || !entry->reference_stdlib || entry->auto_resolved)
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
    PettaLibplVarMap variables = {.slot = petta_solver_slot, .slot_context = client};
    term_t arguments = started ? PL_new_term_refs(3) : 0;
    term_t goal = started ? PL_new_term_ref() : 0;
    bool ok = arguments && goal &&
              petta_libpl_to_term(expression->expr.elems[1], arguments, &variables, 0u) &&
              petta_libpl_to_term(expression->expr.elems[2], arguments + 1, &variables, 0u) &&
              petta_libpl_to_term(expected, arguments + 2, &variables, 0u) &&
              petta_solver_map(client, &variables);
    atom_t name = petta_libpl_new_utf8_atom(entry->name, entry->name_len);
    functor_t functor = name ? PL_new_functor(name, 3) : 0;
    ok = ok && functor && PL_cons_functor_v(goal, functor, arguments);
    if (name)
        PL_unregister_atom(name);
    ok = ok && cetta_foreign_region_start(client->region, runtime->module,
        PL_predicate("call", 1, runtime->module_name), goal) == CETTA_FOREIGN_OK;
    int result = ok ? cetta_foreign_region_next(client->region) : -1;
    bool solved = result == PL_S_TRUE || result == PL_S_LAST;
    bool emitted = solved && petta_libpl_emit_solution(arena, arguments + 2, NULL, &variables, outcomes);
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

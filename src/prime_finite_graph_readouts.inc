/* Qualified finite presentations. Material equality is Aczel decoration;
 * raw-unfolding equality is rooted path-tree isomorphism. The latter is
 * not Scott material equality on an arbitrary graph. Returned values retain
 * their presentation; comparison always names the observation explicitly. */

typedef enum {
    SJ_GRAPH_OK,
    SJ_GRAPH_INVALID,
    SJ_GRAPH_OPEN,
    SJ_GRAPH_BUDGET,
    SJ_GRAPH_STORAGE
} SjGraphFailure;

typedef struct {
    CettaPrimeRegularKernelBudget budget;
    SjGraphFailure failure;
} SjGraphWork;

typedef struct {
    Atom *source;
    Atom *rows;
    size_t root, count;
    size_t *offsets;
    size_t *children;
} SjFiniteGraph;

typedef struct {
    SjFiniteGraph graph;
    Atom *source, *outcome, *scope, *origins;
} SjObservedGraph;

static bool sj_graph_stop(SjGraphWork *work, SjGraphFailure failure) {
    if (work->failure == SJ_GRAPH_OK) work->failure = failure;
    return false;
}

static bool sj_graph_invalid_input(SjGraphWork *work, Atom *input) {
    return sj_graph_stop(work, input && input->kind == ATOM_VAR
        ? SJ_GRAPH_OPEN : SJ_GRAPH_INVALID);
}

static bool sj_graph_spend(SjGraphWork *work) {
    if (work->failure != SJ_GRAPH_OK) return false;
    for (CettaPrimeRegularKernelBudget *budget = &work->budget; budget;
         budget = budget->within) {
        if (budget->limited && budget->remaining == 0u)
            return sj_graph_stop(work, SJ_GRAPH_BUDGET);
    }
    for (CettaPrimeRegularKernelBudget *budget = &work->budget; budget;
         budget = budget->within) {
        if (budget->limited) budget->remaining--;
        if (budget->spent != UINT64_MAX) budget->spent++;
    }
    return true;
}

static void *sj_graph_array(SjGraphWork *work, size_t count, size_t width) {
    if (width == 0u || count > SIZE_MAX / width) {
        sj_graph_stop(work, SJ_GRAPH_STORAGE);
        return NULL;
    }
    void *result = calloc(count ? count : 1u, width);
    if (!result) sj_graph_stop(work, SJ_GRAPH_STORAGE);
    return result;
}

static bool sj_graph_nat(Atom *atom) {
    if (!atom || atom->kind != ATOM_GROUNDED) return false;
    if (atom->ground.gkind == GV_INT) return atom->ground.ival >= 0;
    if (atom->ground.gkind != GV_BIGINT) return false;
    const char *text = atom_bigint_cstr(atom);
    return text && *text && *text != '-';
}

static bool sj_graph_index(Atom *atom, size_t count, size_t *out) {
    if (!sj_graph_nat(atom) || atom->ground.gkind != GV_INT ||
        (uint64_t)atom->ground.ival >= count) return false;
    *out = (size_t)atom->ground.ival;
    return true;
}

static void sj_graph_clear(SjFiniteGraph *graph) {
    free(graph->offsets);
    free(graph->children);
    memset(graph, 0, sizeof(*graph));
}

/* Deduplication here is of an adjacency target, not a proof occurrence.
 * Authored duplicate positions remain in graph->source. */
static bool sj_graph_parse(Atom *source, SjFiniteGraph *graph, SjGraphWork *work) {
    memset(graph, 0, sizeof(*graph));
    if (!sj_graph_spend(work)) return false;
    if (!sj_is_expr(source, "graph", 3u) ||
        !source->expr.elems[2] || source->expr.elems[2]->kind != ATOM_EXPR)
        return sj_graph_invalid_input(work, sj_is_expr(source, "graph", 3u)
            ? source->expr.elems[2] : source);
    Atom *rows = source->expr.elems[2];
    size_t count = (size_t)rows->expr.len, root, entries = 0u;
    if (!count || !sj_graph_index(source->expr.elems[1], count, &root))
        return sj_graph_invalid_input(work, source->expr.elems[1]);
    for (size_t parent = 0u; parent < count; parent++) {
        if (!sj_graph_spend(work)) return false;
        Atom *row = rows->expr.elems[parent];
        if (!row || row->kind != ATOM_EXPR)
            return sj_graph_invalid_input(work, row);
        if ((size_t)row->expr.len > SIZE_MAX - entries)
            return sj_graph_stop(work, SJ_GRAPH_STORAGE);
        entries += (size_t)row->expr.len;
    }
    if (count == SIZE_MAX) return sj_graph_stop(work, SJ_GRAPH_STORAGE);
    graph->offsets = sj_graph_array(work, count + 1u, sizeof(size_t));
    graph->children = sj_graph_array(work, entries, sizeof(size_t));
    unsigned char *seen = sj_graph_array(work, count, 1u);
    if (!graph->offsets || !graph->children || !seen) {
        free(seen);
        sj_graph_clear(graph);
        return false;
    }
    size_t used = 0u;
    bool ok = true;
    for (size_t parent = 0u; ok && parent < count; parent++) {
        graph->offsets[parent] = used;
        Atom *row = rows->expr.elems[parent];
        for (size_t position = 0u; position < (size_t)row->expr.len; position++) {
            size_t child;
            if (!sj_graph_spend(work)) { ok = false; break; }
            if (!sj_graph_index(row->expr.elems[position], count, &child)) {
                ok = sj_graph_invalid_input(work, row->expr.elems[position]);
                break;
            }
            if (!seen[child]) {
                seen[child] = 1u;
                graph->children[used++] = child;
            }
        }
        for (size_t position = graph->offsets[parent]; position < used; position++)
            seen[graph->children[position]] = 0u;
    }
    free(seen);
    if (!ok) { sj_graph_clear(graph); return false; }
    graph->offsets[count] = used;
    graph->source = source;
    graph->rows = rows;
    graph->root = root;
    graph->count = count;
    return true;
}

static bool sj_graph_observed(Atom *source, SjObservedGraph *observed, SjGraphWork *work) {
    memset(observed, 0, sizeof(*observed));
    if (!sj_is_expr(source, "observed-graph", 5u))
        return sj_graph_invalid_input(work, source);
    Atom *outcome = source->expr.elems[2], *scope = source->expr.elems[3];
    Atom *origins = source->expr.elems[4];
    if (!sj_is_expr(outcome, "returned", 2u) && !sj_is_expr(outcome, "faulted", 2u))
        return sj_graph_invalid_input(work, outcome);
    if (!sj_graph_nat(outcome->expr.elems[1]))
        return sj_graph_invalid_input(work, outcome->expr.elems[1]);
    if (!sj_graph_nat(scope)) return sj_graph_invalid_input(work, scope);
    if (!origins || origins->kind != ATOM_EXPR) return sj_graph_invalid_input(work, origins);
    for (size_t i = 0u; i < (size_t)origins->expr.len; i++) {
        if (!sj_graph_spend(work)) return false;
        if (!sj_graph_nat(origins->expr.elems[i]))
            return sj_graph_invalid_input(work, origins->expr.elems[i]);
    }
    if (!sj_graph_parse(source->expr.elems[1], &observed->graph, work)) return false;
    observed->source = source;
    observed->outcome = outcome;
    observed->scope = scope;
    observed->origins = origins;
    return true;
}

static bool sj_graph_aczel_pair(const SjFiniteGraph *left, const SjFiniteGraph *right,
                               const unsigned char *table, size_t a, size_t b,
                               SjGraphWork *work) {
    for (size_t i = left->offsets[a]; i < left->offsets[a+1u]; i++) {
        bool found = false;
        for (size_t j = right->offsets[b]; j < right->offsets[b+1u]; j++) {
            if (!sj_graph_spend(work)) return false;
            if (table[left->children[i] * right->count + right->children[j]]) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    for (size_t j = right->offsets[b]; j < right->offsets[b+1u]; j++) {
        bool found = false;
        for (size_t i = left->offsets[a]; i < left->offsets[a+1u]; i++) {
            if (!sj_graph_spend(work)) return false;
            if (table[left->children[i] * right->count + right->children[j]]) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

/* An alternating-path perfect matching of distinct adjacent targets.
 * Unlike existential child matching this preserves their multiplicities
 * in the raw unfolding. No recursive native stack is needed. */
static bool sj_graph_unfolding_pair(const SjFiniteGraph *left, const SjFiniteGraph *right,
                                   const unsigned char *table, size_t a, size_t b,
                                   SjGraphWork *work) {
    size_t begin_a = left->offsets[a], begin_b = right->offsets[b];
    size_t count = left->offsets[a+1u] - begin_a;
    if (count != right->offsets[b+1u] - begin_b) return false;
    if (!count) return true;
    if (count > SIZE_MAX / 5u) return sj_graph_stop(work, SJ_GRAPH_STORAGE);
    size_t *storage = sj_graph_array(work, 5u * count, sizeof(size_t));
    if (!storage) return false;
    size_t *match_a = storage, *match_b = storage + count;
    size_t *predecessor = storage + 2u*count, *queue = storage + 3u*count;
    size_t *seen_a = storage + 4u*count;
    for (size_t i = 0u; i < count; i++) match_a[i] = match_b[i] = SIZE_MAX;
    bool ok = true;
    for (size_t start = 0u; ok && start < count; start++) {
        for (size_t i = 0u; i < count; i++) {
            predecessor[i] = SIZE_MAX;
            seen_a[i] = 0u;
        }
        size_t head = 0u, tail = 1u, finish = SIZE_MAX;
        queue[0] = start;
        seen_a[start] = 1u;
        while (head < tail && finish == SIZE_MAX && ok) {
            size_t i = queue[head++];
            for (size_t j = 0u; j < count; j++) {
                if (!sj_graph_spend(work)) { ok = false; break; }
                if (predecessor[j] != SIZE_MAX ||
                    !table[left->children[begin_a+i] * right->count + right->children[begin_b+j]])
                    continue;
                predecessor[j] = i;
                if (match_b[j] == SIZE_MAX) { finish = j; break; }
                size_t next = match_b[j];
                if (!seen_a[next]) { seen_a[next] = 1u; queue[tail++] = next; }
            }
        }
        if (finish == SIZE_MAX) { ok = false; break; }
        while (finish != SIZE_MAX) {
            size_t i = predecessor[finish], previous = match_a[i];
            match_a[i] = finish;
            match_b[finish] = i;
            finish = previous;
        }
    }
    free(storage);
    return ok;
}

static unsigned char *sj_graph_relation(const SjFiniteGraph *left, const SjFiniteGraph *right,
                                       bool unfolding, SjGraphWork *work) {
    if (left->count > SIZE_MAX / right->count) {
        sj_graph_stop(work, SJ_GRAPH_STORAGE);
        return NULL;
    }
    size_t pairs = left->count * right->count;
    unsigned char *table = sj_graph_array(work, pairs, 1u);
    unsigned char *next = sj_graph_array(work, pairs, 1u);
    if (!table || !next) { free(table); free(next); return NULL; }
    memset(table, 1, pairs);
    size_t rounds = 0u;
    for (;;) {
        bool changes = false;
        for (size_t a = 0u; a < left->count; a++) {
            for (size_t b = 0u; b < right->count; b++) {
                size_t index = a * right->count + b;
                if (!sj_graph_spend(work)) { free(table); free(next); return NULL; }
                bool matches = table[index] && (unfolding
                    ? sj_graph_unfolding_pair(left, right, table, a, b, work)
                    : sj_graph_aczel_pair(left, right, table, a, b, work));
                if (work->failure != SJ_GRAPH_OK) { free(table); free(next); return NULL; }
                next[index] = (unsigned char)matches;
                changes |= table[index] != next[index];
            }
        }
        unsigned char *swap = table; table = next; next = swap;
        if (!changes) break;
        /* Each changing round deletes at least one of the finite pairs. */
        if (rounds == pairs) {
            free(table); free(next);
            sj_graph_stop(work, SJ_GRAPH_STORAGE);
            return NULL;
        }
        rounds++;
    }
    free(next);
    return table;
}

/* Compare roots inside one tagged disjoint union. Each genuine merge is
 * followed by support deduplication and a fresh unfolding comparison; raw
 * unfolding counts from the old graph are never reused after a quotient. */
static unsigned char *sj_graph_scott_relation(const SjFiniteGraph *left,
                                              const SjFiniteGraph *right,
                                              SjGraphWork *work) {
    bool single = left == right;
    if ((!single && right->count > SIZE_MAX - left->count) ||
        left->count > SIZE_MAX / right->count) {
        sj_graph_stop(work, SJ_GRAPH_STORAGE);
        return NULL;
    }
    size_t original_count = left->count + (single ? 0u : right->count);
    size_t edge_count = left->offsets[left->count];
    if ((!single && right->offsets[right->count] > SIZE_MAX - edge_count) || original_count == SIZE_MAX) {
        sj_graph_stop(work, SJ_GRAPH_STORAGE);
        return NULL;
    }
    if (!single) edge_count += right->offsets[right->count];
    SjFiniteGraph current = {.count = original_count};
    current.offsets = sj_graph_array(work, original_count + 1u, sizeof(size_t));
    current.children = sj_graph_array(work, edge_count, sizeof(size_t));
    size_t *projected = sj_graph_array(work, original_count, sizeof(size_t));
    unsigned char *result = NULL;
    if (!current.offsets || !current.children || !projected) goto finish;
    size_t used = 0u;
    for (size_t source = 0u; source < original_count; source++) {
        if (!sj_graph_spend(work)) goto finish;
        projected[source] = source;
        bool first = source < left->count;
        const SjFiniteGraph *graph = first ? left : right;
        size_t offset = first ? 0u : left->count, local = source - offset;
        current.offsets[source] = used;
        for (size_t i = graph->offsets[local]; i < graph->offsets[local+1u]; i++) {
            if (!sj_graph_spend(work)) goto finish;
            current.children[used++] = graph->children[i] + offset;
        }
    }
    current.offsets[original_count] = used;
    bool stopped = false;
    for (size_t round = 0u; round < original_count && work->failure == SJ_GRAPH_OK; round++) {
        unsigned char *table = sj_graph_relation(&current, &current, true, work);
        size_t *classes = sj_graph_array(work, current.count, sizeof(size_t));
        size_t *representatives = sj_graph_array(work, current.count, sizeof(size_t));
        if (!table || !classes || !representatives) {
            free(table); free(classes); free(representatives);
            goto finish;
        }
        size_t class_count = 0u;
        for (size_t source = 0u; source < current.count; source++) {
            size_t representative = 0u;
            for (; representative < source; representative++) {
                if (!sj_graph_spend(work)) break;
                if (table[source * current.count + representative]) break;
            }
            if (work->failure != SJ_GRAPH_OK) break;
            if (representative < source) classes[source] = classes[representative];
            else { classes[source] = class_count; representatives[class_count++] = source; }
        }
        free(table);
        if (work->failure != SJ_GRAPH_OK) { free(classes); free(representatives); goto finish; }
        if (class_count == current.count) {
            stopped = true;
            free(classes); free(representatives);
            break;
        }
        SjFiniteGraph next = {.count = class_count};
        next.offsets = sj_graph_array(work, class_count + 1u, sizeof(size_t));
        next.children = sj_graph_array(work, current.offsets[current.count], sizeof(size_t));
        unsigned char *seen = sj_graph_array(work, class_count, 1u);
        if (!next.offsets || !next.children || !seen) {
            free(classes); free(representatives); free(seen); sj_graph_clear(&next);
            goto finish;
        }
        used = 0u;
        for (size_t source = 0u; source < class_count; source++) {
            memset(seen, 0, class_count);
            next.offsets[source] = used;
            size_t original = representatives[source];
            for (size_t i = current.offsets[original]; i < current.offsets[original+1u]; i++) {
                if (!sj_graph_spend(work)) break;
                size_t target = classes[current.children[i]];
                if (!seen[target]) { seen[target] = 1u; next.children[used++] = target; }
            }
            if (work->failure != SJ_GRAPH_OK) break;
        }
        next.offsets[class_count] = used;
        for (size_t i = 0u; work->failure == SJ_GRAPH_OK && i < original_count; i++) {
            if (!sj_graph_spend(work)) break;
            projected[i] = classes[projected[i]];
        }
        free(classes); free(representatives); free(seen);
        sj_graph_clear(&current);
        current = next;
    }
    if (work->failure != SJ_GRAPH_OK) goto finish;
    if (!stopped) { sj_graph_stop(work, SJ_GRAPH_STORAGE); goto finish; }
    result = sj_graph_array(work, left->count * right->count, 1u);
    if (result) {
        size_t offset = single ? 0u : left->count;
        for (size_t i = 0u; work->failure == SJ_GRAPH_OK && i < left->count; i++)
            for (size_t j = 0u; j < right->count; j++) {
                if (!sj_graph_spend(work)) break;
                result[i * right->count + j] = projected[i] == projected[offset + j];
            }
    }
finish:
    free(projected);
    sj_graph_clear(&current);
    if (work->failure != SJ_GRAPH_OK) { free(result); return NULL; }
    return result;
}

static bool sj_graph_eligible(const SjFiniteGraph *graph, SjGraphWork *work) {
    unsigned char *admitted = sj_graph_array(work, graph->count, 1u);
    unsigned char *next = sj_graph_array(work, graph->count, 1u);
    if (!admitted || !next) { free(admitted); free(next); return false; }
    for (size_t round = 0u; round < graph->count; round++) {
        for (size_t parent = 0u; parent < graph->count; parent++) {
            if (!sj_graph_spend(work)) { free(admitted); free(next); return false; }
            bool children = true;
            for (size_t i = graph->offsets[parent]; i < graph->offsets[parent+1u]; i++) {
                if (!sj_graph_spend(work)) { free(admitted); free(next); return false; }
                children &= admitted[graph->children[i]] != 0u;
            }
            next[parent] = (unsigned char)(admitted[parent] || children);
        }
        unsigned char *swap = admitted; admitted = next; next = swap;
    }
    bool result = admitted[graph->root] != 0u;
    free(admitted); free(next);
    return result;
}

static Atom *sj_graph_value(Arena *arena, Atom *qualifier, const SjFiniteGraph *graph,
                           size_t root) {
    Atom *presentation = sj_expr3(arena, "graph", atom_int(arena, (int64_t)root), graph->rows);
    return sj_expr3(arena, "FiniteGraphValue", qualifier, presentation);
}

static Atom *sj_graph_failure(Arena *arena, Atom *judgment, SjGraphFailure failure) {
    if (failure == SJ_GRAPH_OPEN)
        return sj_undetermined(arena, judgment, sj_expr1(arena, "set:open-finite-presentation"));
    if (failure == SJ_GRAPH_INVALID)
        return sj_refuted(arena, judgment, sj_expr1(arena, "set:invalid-finite-presentation"));
    return sj_incomplete(arena, judgment, sj_expr1(arena,
        failure == SJ_GRAPH_BUDGET ? "set:finite-readout-budget" : "set:finite-readout-storage"));
}

/* `(set:interpret finite-graph qualifier operation [steps])` is a
 * computation about the named observation. It publishes no assumptions,
 * does not change profile selection, and cannot identify receipts through
 * an observer which omits them. */
static Atom *sj_finite_graph_judgment(Arena *arena, Atom *judgment,
                                    bool limited, uint64_t steps) {
    CettaExprLen len = judgment->expr.len;
    if (len < 2u || !atom_is_symbol(judgment->expr.elems[1], "finite-graph")) return NULL;
    if (len != 4u && len != 5u)
        return sj_refuted(arena, judgment, sj_expr1(arena, "set:finite-readout-arity"));
    Atom *qualifier = judgment->expr.elems[2], *operation = judgment->expr.elems[3];
    bool aczel = atom_is_symbol(qualifier, "aczel");
    bool unfolding = atom_is_symbol(qualifier, "raw-unfolding");
    bool scott = atom_is_symbol(qualifier, "scott-canonical");
    bool foundation = atom_is_symbol(qualifier, "foundation");
    bool presentation = atom_is_symbol(qualifier, "presentation");
    bool observed = atom_is_symbol(qualifier, "observed");
    if (!aczel && !unfolding && !scott && !foundation && !presentation && !observed) return NULL;
    if (len == 5u) {
        Atom *limit = judgment->expr.elems[4];
        if (limit && limit->kind == ATOM_VAR)
            return sj_undetermined(arena, judgment, sj_expr1(arena, "set:open-finite-readout-budget"));
        if (sj_graph_nat(limit) && limit->ground.gkind == GV_BIGINT)
            return sj_incomplete(arena, judgment, sj_expr1(arena, "set:finite-readout-budget-storage"));
        if (!limit || limit->kind != ATOM_GROUNDED || limit->ground.gkind != GV_INT ||
            limit->ground.ival < 0)
            return sj_refuted(arena, judgment, sj_expr1(arena, "set:finite-readout-budget-arity"));
        uint64_t requested = (uint64_t)limit->ground.ival;
        if (!limited || requested < steps) steps = requested;
        limited = true;
    }
    const char *name = sj_head_name(operation);
    bool equal = name && strcmp(name, "equal") == 0;
    bool member = name && strcmp(name, "member") == 0;
    bool value = name && strcmp(name, "value") == 0;
    bool members = name && strcmp(name, "members") == 0;
    bool eligible = name && strcmp(name, "eligible") == 0;
    bool validate = name && strcmp(name, "validate") == 0;
    bool outcome = name && strcmp(name, "outcome") == 0;
    bool scope = name && strcmp(name, "scope") == 0;
    bool origins = name && strcmp(name, "origins") == 0;
    bool origin_count = name && strcmp(name, "origin-count") == 0;
    bool origin_at = name && strcmp(name, "origin-at") == 0;
    bool occurrence_count = name && strcmp(name, "occurrence-count") == 0;
    if (!equal && !member && !value && !members && !eligible && !validate &&
        !outcome && !scope && !origins && !origin_count && !origin_at && !occurrence_count) return NULL;
    CettaExprLen expected = equal || member || origin_at ? 3u : 2u;
    if (!operation || operation->expr.len != expected)
        return sj_refuted(arena, judgment, sj_expr1(arena, "set:finite-operation-arity"));
    if ((member || members) && !aczel && !foundation && !scott) return NULL;
    if (eligible && !foundation) return NULL;
    if ((outcome || scope || origins || origin_count || origin_at) && !observed) return NULL;
    if (occurrence_count && !presentation) return NULL;
    SjGraphWork work = {0};
    cetta_prime_regular_kernel_budget_init(&work.budget, limited, steps);
    SjFiniteGraph first = {0}, second = {0};
    SjObservedGraph observed_first = {0}, observed_second = {0};
    Atom *left = operation->expr.elems[1], *right = expected == 3u ? operation->expr.elems[2] : NULL;
    bool parsed = observed ? sj_graph_observed(left, &observed_first, &work)
                           : sj_graph_parse(left, &first, &work);
    if (observed) first = observed_first.graph;
    if (parsed && (equal || member)) {
        parsed = observed ? sj_graph_observed(right, &observed_second, &work)
                          : sj_graph_parse(right, &second, &work);
        if (observed) second = observed_second.graph;
    }
    Atom *result = NULL;
    if (!parsed) goto finish;
    if (validate) result = atom_bool(arena, true);
    else if (eligible) result = atom_bool(arena, sj_graph_eligible(&first, &work));
    else {
        if (foundation && (!sj_graph_eligible(&first, &work) ||
                           (expected == 3u && !sj_graph_eligible(&second, &work)))) {
            if (work.failure == SJ_GRAPH_OK)
                result = sj_undetermined(arena, judgment, sj_expr1(arena, "set:outside-well-founded-domain"));
            goto finish;
        }
        if (value) result = observed ? sj_expr2(arena, "ObservedGraphValue", left)
                                   : sj_graph_value(arena, qualifier, &first, first.root);
        else if (outcome) result = observed_first.outcome;
        else if (scope) result = observed_first.scope;
        else if (origins) result = observed_first.origins;
        else if (origin_count) result = atom_int(arena, (int64_t)observed_first.origins->expr.len);
        else if (origin_at) {
            size_t position;
            if (right && right->kind == ATOM_VAR)
                result = sj_undetermined(arena, judgment, sj_expr1(arena, "set:open-origin-index"));
            else if (!sj_graph_index(right, (size_t)observed_first.origins->expr.len, &position))
                result = sj_refuted(arena, judgment, sj_expr1(arena, "set:origin-index-outside-fibre"));
            else {
                Atom *items[4] = {sj_sym(arena, "OriginReceipt"), left, right,
                    observed_first.origins->expr.elems[position]};
                result = atom_expr(arena, items, 4u);
            }
        }
        else if (occurrence_count) result = atom_int(arena,
            (int64_t)first.rows->expr.elems[first.root]->expr.len);
        else if (equal && presentation) result = atom_bool(arena, atom_eq(left, right));
        else {
            const SjFiniteGraph *other = members ? &first : &second;
            unsigned char *table = scott ? sj_graph_scott_relation(&first, other, &work)
                : sj_graph_relation(&first, other, unfolding, &work);
            if (!table) goto finish;
            if (equal) {
                bool same = table[first.root * other->count + other->root] != 0u;
                if (observed) same = same && atom_eq(observed_first.outcome, observed_second.outcome) &&
                    atom_eq(observed_first.scope, observed_second.scope) &&
                    atom_eq(observed_first.origins, observed_second.origins);
                result = atom_bool(arena, same);
            } else if (member) {
                bool found = false;
                for (size_t i = second.offsets[second.root]; i < second.offsets[second.root+1u]; i++)
                    found |= table[first.root * second.count + second.children[i]] != 0u;
                result = atom_bool(arena, found);
            } else if (members) {
                size_t count = first.offsets[first.root+1u] - first.offsets[first.root], used = 0u;
                size_t *representatives = sj_graph_array(&work, count, sizeof(size_t));
                Atom **values = sj_graph_array(&work, count, sizeof(Atom *));
                if (representatives && values) {
                    for (size_t i = first.offsets[first.root]; i < first.offsets[first.root+1u]; i++) {
                        size_t child = first.children[i];
                        bool previous = false;
                        for (size_t j = 0u; j < used; j++) {
                            if (!sj_graph_spend(&work)) break;
                            previous |= table[child * first.count + representatives[j]] != 0u;
                        }
                        if (work.failure != SJ_GRAPH_OK) break;
                        if (!previous) {
                            representatives[used] = child;
                            values[used++] = sj_graph_value(arena, qualifier, &first, child);
                        }
                    }
                    if (work.failure == SJ_GRAPH_OK) result = atom_expr(arena, values, (CettaExprLen)used);
                }
                free(representatives); free(values);
            }
            free(table);
        }
    }
finish:
    sj_graph_clear(&first);
    sj_graph_clear(&second);
    if (work.failure != SJ_GRAPH_OK) return sj_graph_failure(arena, judgment, work.failure);
    if (!result) return NULL;
    if (sj_is_expr(result, "PrimeVerdict", 4u)) return result;
    return sj_established(arena, judgment, sj_expr2(arena, "PrimeScopedValue", result));
}

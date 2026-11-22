/* tcalc_delta.c - step every over a set store, run in one store, by delta.
 * Included by tcalc_core.c at its section 7, in the Cesta unit
 * tcalc_core.cesta with the rest.
 *
 * A program propagates when its rules only read and make (section 7).  Run
 * under step every over a set store from a state of fixed terms, each step
 * fires every match at the state, and the state it reaches holds the state's
 * rows and the rows the matches make: every state of the run holds the one
 * before, and every row is a fixed term.  Such a run is kept as one store,
 * growing, not as a chain of states.  Its versions form a line, so the store
 * is a log (tcalc_store.c, one store): a version is a length of the log, and
 * a state's maps, which serve versions that branch, are not needed.  Its rules
 * run by their plans (tcalc_plan.c), as over a state's view (section 5):
 *
 *   - the store's rows in the order they arrive, each once (a set);
 *   - a run's list and the rows of each index of the plans, made when a probe
 *     first asks and kept as rows arrive, never made again: under each key
 *     node of an index (tcalc_store.c), the positions of the rows below it,
 *     the latest first, so that the rows of a window of positions are read
 *     without reading older ones; an index split by a key (its domain) is
 *     reached only by the rows holding that key, posted under the key's
 *     split as they arrive;
 *   - the delta: the rows the step before added, the last of the store;
 *   - the joins: each rule pattern, waiting on the most particular place it
 *     fixes, its first argument when that is a fixed term, else that
 *     argument's head when that is a fixed symbol (a call's head: a value row
 *     wakes only the rules waiting on its call), else its run (a pattern of no
 *     fixed head waits on every row), so that a step meets only the patterns
 *     some delta row can meet, as a datum meets the continuations waiting on
 *     its channel.
 *
 * A step from the store S, which the step before grew from S0 by the delta
 * D = S - S0, meets only the matches that read a row of D, each once, by the
 * first pattern that reads one: for each pattern d, the plan entering by d
 * meets a row of D for d first, so the other patterns are asked with its
 * bindings, rows of S0 for the patterns before d, rows of S for those after.
 * A match at S whose rows all are rows of S0 was a match at S0, fired by the
 * step before, and makes only rows S holds (RuleSyntax/SetStoreFamilies:
 * Propagates, and completed_outcomes_agree).  The first step meets every
 * match, by each rule's plan of no entry.  The step adds the rows its matches
 * make that the store lacks; none: the store is the outcome.  With shortcuts
 * none every step meets every match; with them checked, both, and the rows
 * they add compared.
 *
 * The states of the run are the stores after each step, as in the chain: the
 * run counts the first store and each store a step grows to, and spends one
 * unit of work a store expanded, so states and work are those of the chain of
 * states the other path explores (a state with no step is the last).  Under
 * observe run, each store expanded is listed, as a run lists its states. */

/* ---- The rows as a set ------------------------------------------------------ */

static bool delta_has(const DeltaStore *s, TcTerm t) {
    if (!s->member_cap) return false;
    size_t mask = s->member_cap - 1;
    for (size_t h = place_hash(t, 0) & mask; s->member[h]; h = (h + 1) & mask)
        if (s->member[h] == t) return true;
    return false;
}

/* Room for one more member: the set rebuilt from the rows when half full. */
static bool delta_member_room(DeltaStore *s) {
    if ((s->rows + 1) * 2 <= s->member_cap) return true;
    size_t cap = s->member_cap ? s->member_cap * 2 : 1024;
    uint32_t *m = calloc(cap, sizeof *m);
    if (!m) return false;
    for (size_t i = 0; i < s->rows; i++) {
        size_t h = place_hash(delta_row(s, i), 0) & (cap - 1);
        while (m[h]) h = (h + 1) & (cap - 1);
        m[h] = delta_row(s, i);
    }
    free(s->member);
    s->member = m;
    s->member_cap = cap;
    return true;
}

/* ---- Lists and indexes ------------------------------------------------------------
 * A run's list: its rows' positions, the latest first, made when a probe first
 * reads the run.  An index's rows: under each of its key nodes, the positions
 * of the rows below it, the latest first. */

static inline uint32_t dnode(const DeltaStore *s, uint32_t node) {
    return node < s->node_words ? s->node[node] : 0;
}
static inline uint32_t entry_position(const DeltaStore *s, uint32_t entry) { return s->entry[2 * (size_t)entry]; }
static inline uint32_t entry_before(const DeltaStore *s, uint32_t entry) { return s->entry[2 * (size_t)entry + 1]; }

/* Room for the list of key node `node`, cleared.  False when memory ran out. */
static bool delta_node_room(DeltaStore *s, uint32_t node) {
    while (s->node_words < (size_t)node + 1)
        if (!tc_push_u32(realloc, &s->node, &s->node_words, &s->node_cap, 0)) return false;
    return true;
}

/* An entry of position `position` before entry `before` + 1: its index + 1,
 * 0 when memory ran out. */
static uint32_t delta_entry(DeltaStore *s, uint32_t position, uint32_t before) {
    uint32_t entry = (uint32_t)(s->entries / 2);
    if (!tc_push_u32(realloc, &s->entry, &s->entries, &s->entry_cap, position) ||
        !tc_push_u32(realloc, &s->entry, &s->entries, &s->entry_cap, before))
        return 0;
    return entry + 1;
}

/* The row at `position`, one the index keeps, enters index `index` (of that
 * shape): listed the latest under each of its key nodes.  False when memory
 * ran out. */
static bool delta_index_enter(Explorer *x, uint32_t index, const IndexShape *shape, TcTerm row, uint32_t position) {
    uint32_t node[INDEX_PATHS] = {0};
    if (!row_nodes(&x->places, x->terms, index_root(&x->places, index), shape, row, node)) return false;
    DeltaStore *s = &x->delta_store;
    for (uint32_t j = 0; j < shape_below(shape, 0); j++) {
        if (!delta_node_room(s, node[j])) return false;
        uint32_t entry = delta_entry(s, position, s->node[node[j]]);
        if (!entry) return false;
        s->node[node[j]] = entry;
    }
    return true;
}

/* Whether the row is of the run (head, length): a row of a symbol head. */
static bool delta_in_run(const TcTerms *t, TcTerm row, uint32_t head, uint32_t length) {
    TcNode rn = term_node(t, row);
    return rn.head && rn.head == head && rn.kind == TC_EXPRESSION && rn.length == length;
}

/* The postings of split `split` (+ 1): the positions of the rows holding its
 * key at its cut's path, the latest first; its first entry + 1. */
static inline uint32_t delta_postings(const DeltaStore *s, uint32_t split) {
    return split - 1 < s->posts ? s->post[split - 1] : 0;
}

/* Posts the row at `position` under split `split` (+ 1).  False when memory
 * ran out. */
static bool delta_post(DeltaStore *s, uint32_t split, uint32_t position) {
    while (s->posts < split)
        if (!tc_push_u32(realloc, &s->post, &s->posts, &s->post_cap, 0)) return false;
    uint32_t entry = delta_entry(s, position, s->post[split - 1]);
    if (!entry) return false;
    s->post[split - 1] = entry;
    return true;
}

/* A new cut of a run at a path (+ 1, `cut`): the splits of every index of the
 * plans on the run (head, length) at that path, each by its key, then each
 * row of the run holding one of those keys there posted under it, in the
 * order of the store.  False when memory ran out. */
static bool delta_cut(Explorer *x, uint32_t cut, uint32_t head, uint32_t length) {
    const ProgramView *v = &x->program;
    uint32_t path = cut_path(&x->places, cut - 1);
    for (uint32_t c = 0; c < plan_indexes(v); c++) {
        TcTerm key = 0;
        if (index_head(v, c) == head && index_length(v, c) == length && index_split(v, c, &key) == path &&
            !place_split(&x->places, cut, key, true))
            return false;
    }
    for (size_t i = 0; i < x->delta_store.rows; i++) {
        TcTerm row = delta_row(&x->delta_store, i), key = 0;
        if (!delta_in_run(x->terms, row, head, length) || !path_key(x->terms, row, path, &key)) continue;
        uint32_t split = place_split(&x->places, cut, key, false);
        if (split && !delta_post(&x->delta_store, split, (uint32_t)i)) return false;
    }
    return true;
}

/* The rows of index `index` kept, entered when it is first asked for, in the
 * order of the store: those of its split's postings, or (an index of every
 * row) those of its run.  False when memory ran out. */
static bool delta_fill(Explorer *x, uint32_t index) {
    while (x->delta_store.filleds <= index)
        if (!tc_push_u32(realloc, &x->delta_store.filled, &x->delta_store.filleds, &x->delta_store.filled_cap, 0))
            return false;
    if (x->delta_store.filled[index]) return true;
    x->delta_store.filled[index] = 1;
    uint32_t run = index_run(&x->places, index);
    uint32_t head = run_head(&x->places, run), length = run_length(&x->places, run);
    IndexShape shape = index_shape(&x->program, index);
    TcTerm key = 0;
    uint32_t path = index_split(&x->program, index, &key);
    if (!path) {
        for (size_t i = 0; i < x->delta_store.rows; i++) {
            TcTerm row = delta_row(&x->delta_store, i);
            if (delta_in_run(x->terms, row, head, length) && !delta_index_enter(x, index, &shape, row, (uint32_t)i))
                return false;
        }
        return true;
    }
    bool made = false;
    uint32_t cut = place_cut(&x->places, run, path, false, &made);
    uint32_t split = cut ? place_split(&x->places, cut, key, false) : 0;
    uint32_t first = split ? delta_postings(&x->delta_store, split) : 0;
    size_t n = 0;
    for (uint32_t e = first; e; e = entry_before(&x->delta_store, e - 1)) n++;
    uint32_t *position = malloc((n ? n : 1) * sizeof *position);
    if (!position) return false;
    size_t k = n;   /* the postings come the latest first: entered in reverse */
    for (uint32_t e = first; e && k; e = entry_before(&x->delta_store, e - 1))
        position[--k] = entry_position(&x->delta_store, e - 1);
    bool ok = true;
    for (size_t i = 0; ok && i < n; i++)
        ok = delta_index_enter(x, index, &shape, delta_row(&x->delta_store, position[i]), position[i]);
    free(position);
    return ok;
}

/* The run's list kept, made from the store when it is first asked for: its
 * first entry + 1.  False when memory ran out. */
static bool delta_run_list(Explorer *x, uint32_t run, uint32_t *first) {
    DeltaStore *s = &x->delta_store;
    while (s->lists < 2 * ((size_t)run + 1))
        if (!tc_push_u32(realloc, &s->list, &s->lists, &s->list_cap, 0)) return false;
    if (!s->list[2 * (size_t)run]) {
        s->list[2 * (size_t)run] = 1;
        uint32_t head = run_head(&x->places, run), length = run_length(&x->places, run);
        for (size_t i = 0; i < s->rows; i++) {
            if (!delta_in_run(x->terms, delta_row(s, i), head, length)) continue;
            uint32_t entry = delta_entry(s, (uint32_t)i, s->list[2 * (size_t)run + 1]);
            if (!entry) return false;
            s->list[2 * (size_t)run + 1] = entry;
        }
    }
    *first = s->list[2 * (size_t)run + 1];
    return true;
}

/* The row at `position` enters every index kept of its run that keeps it:
 * those of every row, and, at each cut of the run, those of the split of
 * the key it holds there, after it is posted under that split. */
static bool delta_add_indexes(Explorer *x, uint32_t run, TcTerm row, uint32_t position) {
    for (uint32_t i = run_indexes(&x->places, run); i; i = index_next(&x->places, i - 1)) {
        if (i - 1 >= x->delta_store.filleds || !x->delta_store.filled[i - 1]) continue;
        IndexShape shape = index_shape(&x->program, i - 1);
        if (!delta_index_enter(x, i - 1, &shape, row, position)) return false;
    }
    for (uint32_t c = run_cuts(&x->places, run); c; c = cut_next(&x->places, c - 1)) {
        TcTerm key = 0;
        if (!path_key(x->terms, row, cut_path(&x->places, c - 1), &key)) continue;
        uint32_t split = place_split(&x->places, c, key, false);
        if (!split) continue;   /* no index keeps rows of that key */
        if (!delta_post(&x->delta_store, split, position)) return false;
        for (uint32_t i = split_indexes(&x->places, split - 1); i; i = index_next(&x->places, i - 1)) {
            if (i - 1 >= x->delta_store.filleds || !x->delta_store.filled[i - 1]) continue;
            IndexShape shape = index_shape(&x->program, i - 1);
            if (!delta_index_enter(x, i - 1, &shape, row, position)) return false;
        }
    }
    return true;
}

/* A row arrives: it joins the set, its run's list when it is kept, and every
 * index kept of its run. */
static bool delta_add(Explorer *x, TcTerm row) {
    DeltaStore *s = &x->delta_store;
    if (!delta_member_room(s)) return false;
    uint32_t position = (uint32_t)s->rows;
    if (!tc_push_u32(realloc, &s->row, &s->rows, &s->row_cap, row)) return false;
    size_t mask = s->member_cap - 1, h = place_hash(row, 0) & mask;
    while (s->member[h]) h = (h + 1) & mask;
    s->member[h] = row;
    TcNode rn = term_node(x->terms, row);
    if (!rn.head || rn.kind != TC_EXPRESSION) return true;
    uint32_t run = row_run(&x->places, x->terms, row, true);
    if (run == UINT32_MAX) return false;
    if (2 * (size_t)run < s->lists && s->list[2 * (size_t)run]) {
        uint32_t entry = delta_entry(s, position, s->list[2 * (size_t)run + 1]);
        if (!entry) return false;
        s->list[2 * (size_t)run + 1] = entry;
    }
    return delta_add_indexes(x, run, row, position);
}

/* ---- The rows a probe meets ----------------------------------------------------
 * A probe (section 5) over the one store: its relation's list, or the list of
 * its index's key node of its keys, at positions lo .. hi-1, the latest
 * first; every position when its relation has no symbol head. */
static bool delta_probe(Explorer *x, uint32_t probe, uint32_t base, uint32_t lo, uint32_t hi, DeltaCursor *c) {
    const ProgramView *v = &x->program;
    const TcTerms *t = x->terms;
    *c = (DeltaCursor){.lo = lo, .hi = hi};
    uint32_t head = probe_head(v, probe), length = probe_length(v, probe), keys = 0;
    TcTerm key[INDEX_PATHS] = {0};
    if (head == PLAN_OPEN) {   /* the relation the term walks to */
        TcNode pn = term_node(t, walk(t, &x->solver, probe_term(v, probe), base));
        TcTerm first = pn.kind == TC_EXPRESSION && pn.length ? walk(t, &x->solver, term_item(t, pn.start), base) : 0;
        head = first && term_node(t, first).kind == TC_SYMBOL ? first : 0;
        length = pn.length;
    } else
        for (uint32_t l = 0; l < probe_level(v, probe); l++, keys++)
            if (!term_node(t, key[l] = walk(t, &x->solver, probe_key(v, probe, l), base)).ground) break;
    if (!head) {   /* a relation of no symbol head: every position */
        c->scan = true;
        c->at = hi;
        return true;
    }
    uint32_t run = place_find(&x->places, head, length);
    if (run == UINT32_MAX) return true;   /* no row of the relation */
    if (!keys) return delta_run_list(x, run, &c->entry);
    uint32_t index = probe_index(v, probe), cut = 0;   /* its keys are in its domain */
    IndexShape shape = index_shape(v, index);
    TcTerm split_key = 0;
    uint32_t split = index_split(v, index, &split_key);
    if (!place_index(&x->places, index, head, length, split, split_key, &cut) ||
        (cut && !delta_cut(x, cut, head, length)) || !delta_fill(x, index))
        return false;
    keys = shape_kept(&shape, keys);
    if (!keys) return delta_run_list(x, run, &c->entry);
    c->entry = dnode(&x->delta_store, keys_node(&x->places, index_root(&x->places, index), &shape, key, keys));
    return true;
}

/* The next position of the cursor, the latest first; UINT32_MAX when none is
 * left.  A list's entries come the latest first, so the first below lo ends
 * them. */
static uint32_t delta_next(const Explorer *x, DeltaCursor *c) {
    const DeltaStore *s = &x->delta_store;
    if (c->scan) return c->at > c->lo ? --c->at : UINT32_MAX;
    while (c->entry) {
        uint32_t position = entry_position(s, c->entry - 1);
        c->entry = entry_before(s, c->entry - 1);
        if (position >= c->hi) continue;
        if (position < c->lo) break;
        return position;
    }
    c->entry = 0;
    return UINT32_MAX;
}

/* ---- Joins ------------------------------------------------------------------
 * The join table is keyed by (head, length, path, key), five words a slot:
 * those four and the last join there + 1.  Path 0, key 0: the run itself. */

/* The slot of a place (head 0: empty), made when it has none and `make`;
 * SIZE_MAX when it has none or memory ran out.  Rebuilt when half full. */
static size_t delta_place(DeltaStore *s, uint32_t head, uint32_t length, uint32_t path, uint32_t key,
                          bool make) {
    size_t n = s->place_cap / 5;
    if (n) {
        size_t mask = n - 1;
        for (size_t h = place_hash((uint64_t)head << 32 | length, (uint64_t)path << 32 | key) & mask;
             s->place[5 * h]; h = (h + 1) & mask)
            if (s->place[5 * h] == head && s->place[5 * h + 1] == length && s->place[5 * h + 2] == path &&
                s->place[5 * h + 3] == key)
                return 5 * h;
    }
    if (!make) return SIZE_MAX;
    if ((s->places + 1) * 2 > n) {
        size_t cap = n ? n * 2 : 256;
        uint32_t *t = calloc(5 * cap, sizeof *t);
        if (!t) return SIZE_MAX;
        for (size_t i = 0; i < n; i++) {
            if (!s->place[5 * i]) continue;
            size_t h = place_hash((uint64_t)s->place[5 * i] << 32 | s->place[5 * i + 1],
                                  (uint64_t)s->place[5 * i + 2] << 32 | s->place[5 * i + 3]) & (cap - 1);
            while (t[5 * h]) h = (h + 1) & (cap - 1);
            for (size_t w = 0; w < 5; w++) t[5 * h + w] = s->place[5 * i + w];
        }
        free(s->place);
        s->place = t;
        s->place_cap = 5 * cap;
        n = cap;
    }
    size_t mask = n - 1, h = place_hash((uint64_t)head << 32 | length, (uint64_t)path << 32 | key) & mask;
    while (s->place[5 * h]) h = (h + 1) & mask;
    uint32_t words[5] = {head, length, path, key, 0};
    for (size_t w = 0; w < 5; w++) s->place[5 * h + w] = words[w];
    s->places++;
    return 5 * h;
}

/* ---- One step ----------------------------------------------------------------- */

static bool delta_decide(Explorer *x, uint32_t rule) {
    x->decided++;
    x->fire = rule_at(&x->program, rule);
    x->fire_rule = rule;
    return tc_fire(x) != tc_fire_engine_failure && !x->solver.failed;
}

/* Sorts n term ids (merge sort); false when memory ran out. */
static bool delta_sort(uint32_t *a, size_t n) {
    uint32_t *tmp = malloc((n ? n : 1) * sizeof *tmp);
    if (!tmp) return false;
    for (size_t width = 1; width < n; width *= 2) {
        for (size_t lo = 0; lo < n; lo += 2 * width) {
            size_t mid = lo + width < n ? lo + width : n;
            size_t hi = lo + 2 * width < n ? lo + 2 * width : n;
            size_t i = lo, j = mid, k = lo;
            while (i < mid && j < hi) tmp[k++] = a[i] <= a[j] ? a[i++] : a[j++];
            while (i < mid) tmp[k++] = a[i++];
            while (j < hi) tmp[k++] = a[j++];
        }
        memcpy(a, tmp, n * sizeof *a);
    }
    free(tmp);
    return true;
}

/* The rows a step adds: the matches of the step (from the delta at delta_lo;
 * every match when delta_lo is 0) decided, and the rows they make that the
 * store lacks, each once, ordered by term id, left in x->next.  False when
 * memory ran out. */
static bool delta_step(Explorer *x, uint32_t delta_lo) {
    uint32_t size = (uint32_t)x->delta_store.rows;
    x->next_len = 0;
    if (delta_lo == 0) {   /* every match: each rule's plan of no entry over every row */
        for (uint32_t rule = 0; rule < x->program.rules; rule++) {
            TcStoredRule r = rule_at(&x->program, rule);
            if (r.same != rule || !in_region(x, rule)) continue;   /* that rule again; out of the region */
            if (!ensure_pick(x, (size_t)r.reads + 1) || !frame(x, 0, &r) || !plan_run(x, rule, UINT32_MAX, 0, size))
                return false;
        }
    } else {   /* the patterns waiting on the delta rows' places, each once, in rule order */
        DeltaStore *s = &x->delta_store;
        uint32_t stamp = ++s->step;
        s->todos = 0;
        for (size_t i = delta_lo; i < size; i++) {
            TcNode rn = term_node(x->terms, delta_row(s, i));
            if (!rn.head || rn.kind != TC_EXPRESSION) continue;
            /* the joins on the run, on its fixed first argument, and on that argument's head */
            uint32_t paths[3] = {0, 1, 1u | 1u << 10}, keys[3] = {0, 0, 0}, probes = 1;
            if (rn.length > 1) {
                keys[1] = term_item(x->terms, rn.start + 1);
                TcNode an = term_node(x->terms, keys[1]);
                probes = 2;
                if (an.kind == TC_EXPRESSION && an.head) { keys[2] = an.head; probes = 3; }
            }
            for (uint32_t k = 0; k < probes; k++) {
                size_t at = delta_place(s, rn.head, rn.length, paths[k], keys[k], false);
                for (uint32_t e = at == SIZE_MAX ? 0 : s->place[at + 4]; e; e = s->join[3 * (size_t)(e - 1) + 2])
                    if (s->stamp[e - 1] != stamp) {
                        s->stamp[e - 1] = stamp;
                        if (!tc_push_u32(realloc, &s->todo, &s->todos, &s->todo_cap, e - 1)) return false;
                    }
            }
        }
        for (size_t w = 0; w < s->wilds; w += 2)   /* patterns of no fixed head meet any row */
            if (!tc_push_u32(realloc, &s->todo, &s->todos, &s->todo_cap, (uint32_t)(w / 2) | UINT32_C(1) << 31))
                return false;
        if (!delta_sort(s->todo, s->todos)) return false;
        for (size_t k = 0; k < x->delta_store.todos; k++) {
            uint32_t e = x->delta_store.todo[k], rule, d;
            if (e >> 31) {
                rule = x->delta_store.wild[2 * (size_t)(e & 0x7fffffffu)];
                d = x->delta_store.wild[2 * (size_t)(e & 0x7fffffffu) + 1];
            } else {
                rule = x->delta_store.join[3 * (size_t)e];
                d = x->delta_store.join[3 * (size_t)e + 1];
            }
            TcStoredRule r = rule_at(&x->program, rule);
            if (!ensure_pick(x, (size_t)r.reads + 1) || !frame(x, 0, &r) || !plan_run(x, rule, d, delta_lo, size))
                return false;
        }
    }
    size_t kept = 0;
    for (size_t i = 0; i < x->next_len; i++)
        if (!delta_has(&x->delta_store, x->next[i])) x->next[kept++] = x->next[i];
    if (!delta_sort(x->next, kept)) return false;
    x->next_len = 0;
    for (size_t i = 0; i < kept; i++)
        if (!x->next_len || x->next[x->next_len - 1] != x->next[i]) x->next[x->next_len++] = x->next[i];
    return true;
}

/* The joins: each pattern of each rule the run fires, waiting on the most
 * particular place it fixes (above), or, when its head is no fixed symbol,
 * on every row. */
static bool delta_joins(Explorer *x) {
    DeltaStore *s = &x->delta_store;
    for (uint32_t rule = 0; rule < x->program.rules; rule++) {
        TcStoredRule r = rule_at(&x->program, rule);
        if (r.same != rule || !in_region(x, rule)) continue;
        for (uint32_t k = 0; k < r.reads; k++) {
            TcNode pn = term_node(x->terms, pattern_at(&x->program, &r, k));
            if (!pn.head || pn.kind != TC_EXPRESSION) {
                if (!tc_push_u32(realloc, &s->wild, &s->wilds, &s->wild_cap, rule) ||
                    !tc_push_u32(realloc, &s->wild, &s->wilds, &s->wild_cap, k))
                    return false;
                continue;
            }
            /* the most particular place the pattern fixes: its first argument, that
             * argument's head symbol, or the whole run */
            uint32_t path = 0, key = 0;
            if (pn.length > 1) {
                TcTerm arg = term_item(x->terms, pn.start + 1);
                TcNode an = term_node(x->terms, arg);
                if (an.ground) { path = 1; key = arg; }
                else if (an.kind == TC_EXPRESSION && an.head) { path = 1u | 1u << 10; key = an.head; }
            }
            size_t at = delta_place(s, pn.head, pn.length, path, key, true);
            if (at == SIZE_MAX) return false;
            uint32_t entry = (uint32_t)(s->joins / 3);
            if (!tc_push_u32(realloc, &s->join, &s->joins, &s->join_cap, rule) ||
                !tc_push_u32(realloc, &s->join, &s->joins, &s->join_cap, k) ||
                !tc_push_u32(realloc, &s->join, &s->joins, &s->join_cap, s->place[at + 4]) ||
                !tc_push_u32(realloc, &s->stamp, &s->stamps, &s->stamp_cap, 0))
                return false;
            s->place[at + 4] = entry + 1u;
        }
    }
    return true;
}

/* Lists the store as a state, its rows (all fixed) in canonical order: an
 * outcome, or under observe run the run's next state. */
static bool delta_list(Explorer *x, TcOutcomes *o) {
    const DeltaStore *s = &x->delta_store;
    uint32_t start = (uint32_t)o->rows;
    for (size_t i = 0; i < s->rows; i++)
        if (!tc_push_u32(realloc, &o->row, &o->rows, &o->row_cap, delta_row(s, i))) return false;
    uint32_t n = (uint32_t)(o->rows - start);
    if (n) {
        uint32_t *pos = malloc(n * sizeof *pos);
        TcTerm *sorted = malloc(n * sizeof *sorted);
        const TcNode *nodes = x->terms->node;
        if (!pos || !sorted || !nodes || !o->row) { free(pos); free(sorted); return false; }
        for (uint32_t i = 0; i < n; i++) pos[i] = i;
        bool ok;
        {
            RowOrder order = {nodes, x->terms->nodes, o->row + start, o->row + start, n, false};
            ok = sort_positions(&order, pos, n);
        }
        for (uint32_t i = 0; ok && i < n; i++) sorted[i] = o->row[start + pos[i]];
        if (ok) memcpy(o->row + start, sorted, n * sizeof *sorted);
        free(pos); free(sorted);
        if (!ok) return false;
    }
    o->outcomes++;
    return tc_push_u32(realloc, &o->start, &o->starts, &o->start_cap, start) &&
           tc_push_u32(realloc, &o->size, &o->sizes, &o->size_cap, n);
}

/* The run of a program that propagates, from a state of fixed terms, under
 * step every over a set store (above).  *finished: it reached a store with no
 * step.  False when memory ran out. */
static bool delta_loop(Explorer *x, const TcTerm *initial, size_t initials, TcOutcomes *o,
                       bool *finished) {
    bool run = x->how.observe == TC_OBSERVE_RUN;
    uint64_t states = 1;
    *finished = false;
    if (!reserve_unknowns(&x->solver, 0)) return false;
    if (initials) {
        if (!initial) return false;
        for (size_t i = 0; i < initials; i++)
            if (!delta_has(&x->delta_store, initial[i]) && !delta_add(x, initial[i])) return false;
    }
    if (!delta_joins(x)) return false;
    uint32_t delta_lo = 0;
    for (;;) {
        if (!spend(x)) break;
        if (run && !delta_list(x, o)) return false;
        TcShortcuts shortcuts = x->how.shortcuts;
        if (!delta_step(x, shortcuts == TC_SHORTCUTS_NONE ? 0 : delta_lo)) return false;
        if (shortcuts == TC_SHORTCUTS_CHECKED && delta_lo != 0) {   /* the step again, every match */
            size_t n = x->next_len;
            TcTerm *by_delta = malloc((n ? n : 1) * sizeof *by_delta);
            if (!by_delta) return false;
            if (n) memcpy(by_delta, x->next, n * sizeof *by_delta);
            bool ok = delta_step(x, 0);
            if (ok && (x->next_len != n || (n && memcmp(x->next, by_delta, n * sizeof *by_delta) != 0)))
                x->disagreed = true;
            free(by_delta);
            if (!ok) return false;
        }
        if (x->next_len == 0) {   /* no step: the store is the outcome */
            if (!run && !delta_list(x, o)) return false;
            *finished = true;
            break;
        }
        delta_lo = (uint32_t)x->delta_store.rows;
        for (size_t i = 0; i < x->next_len; i++)
            if (!delta_add(x, x->next[i])) return false;
        states++;
    }
    o->states = run ? o->outcomes : states;
    return !x->solver.failed && !x->terms->failed;
}

static bool delta_run(Explorer *x, const TcTerm *initial, size_t initials, TcOutcomes *o,
                      bool *finished) {
    x->delta = true;
    x->ground = true;           /* its rows are fixed terms */
    x->view.state = VIEW_ONE;   /* the one store is the store matched; no hook of a match reads it besides */
    return delta_loop(x, initial, initials, o, finished);
}

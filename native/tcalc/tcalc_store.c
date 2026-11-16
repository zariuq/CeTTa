/* tcalc_store.c - the data the calculus works on, included by tcalc_core.c at
 * its sections 3 and 4: the canonical forms of states and the table of states,
 * the program's pools and its view of a rule, and the record an exploration
 * leaves (its outcomes and the steps among the states it reached).  The
 * definitions are in tcalc_core.c's section headings; this file is their
 * representation, in the Cesta unit tcalc_core.cesta with the rest. */

/* ---- Canonical forms and the table of states (section 3) ------------------ */

typedef struct {
    const TcNode *node;
    size_t nodes;
    const TcTerm *shape;   /* per position: the row with unknowns blanked */
    const TcTerm *row;
    size_t count;
    bool shaped;
} RowOrder;

static inline int head_length_order(const TcNode *node, TcTerm a, TcTerm b) {
    const TcNode *x = &node[a], *y = &node[b];
    uint32_t xl = x->kind == TC_EXPRESSION ? x->length : 0;
    uint32_t yl = y->kind == TC_EXPRESSION ? y->length : 0;
    if (x->head != y->head) return x->head < y->head ? -1 : 1;
    if (xl != yl) return xl < yl ? -1 : 1;
    return 0;
}

static int row_compare(const RowOrder *o, uint32_t a, uint32_t b) {
    int c = head_length_order(o->node, o->row[a], o->row[b]);
    if (c) return c;
    if (o->shaped && o->shape[a] != o->shape[b]) return o->shape[a] < o->shape[b] ? -1 : 1;
    if (o->row[a] != o->row[b]) return o->row[a] < o->row[b] ? -1 : 1;
    return 0;
}

/* The order of the rows of a state with no unknowns: row_compare without shapes. */
static int ground_order(const TcNode *node, TcTerm a, TcTerm b) {
    int c = head_length_order(node, a, b);
    return c ? c : a == b ? 0 : a < b ? -1 : 1;
}

/* Sorts positions 0..n-1 by row_compare (merge sort, stable). */
static bool sort_positions(const RowOrder *o, uint32_t *pos, uint32_t n) {
    uint32_t *tmp = malloc((n ? n : 1) * sizeof *tmp);
    if (!tmp) return false;
    for (uint32_t width = 1; width < n; width *= 2) {
        for (uint32_t lo = 0; lo < n; lo += 2 * width) {
            uint32_t mid = lo + width < n ? lo + width : n;
            uint32_t hi = lo + 2 * width < n ? lo + 2 * width : n;
            uint32_t i = lo, j = mid, k = lo;
            while (i < mid && j < hi)
                tmp[k++] = row_compare(o, pos[i], pos[j]) <= 0 ? pos[i++] : pos[j++];
            while (i < mid) tmp[k++] = pos[i++];
            while (j < hi) tmp[k++] = pos[j++];
        }
        memcpy(pos, tmp, n * sizeof *pos);
    }
    free(tmp);
    return true;
}

/* The row with every unknown replaced by `blank`. */
static TcTerm blank_from(TcTerms *t, TcTerm row, TcTerm blank) {
    const TcNode *n = node_of(t, row);
    if (n->ground) return row;
    if (n->kind == TC_UNKNOWN) return blank;
    if (n->kind != TC_EXPRESSION) return row;
    TcTerm done;
    if (walk_seen(t, row, &done)) return done;
    uint32_t count = n->length, start = n->start;
    TcTerm *items = malloc((count ? count : 1) * sizeof *items);
    if (!items) return 0;
    for (uint32_t i = 0; i < count; i++)
        items[i] = blank_from(t, t->item[start + i], blank);
    TcTerm out = tc_expression(t, items, count);
    free(items);
    if (out) walk_record(t, row, out);
    return out;
}

static TcTerm blank_unknowns(TcTerms *t, TcTerm row, TcTerm blank) {
    walk_begin(t);
    return blank_from(t, row, blank);
}

/* Renumbers the variables of one kind (unknowns, or locals) in `term` by `map`
 * (old index -> new index + 1, 0 when not met yet), giving new indices in order
 * of first appearance.  Returns 0 when memory ran out.  A subterm met again has
 * all its variables numbered already, so its record is its renumbering. */
static TcTerm renumber_from(TcTerms *t, TcTerm term, TcKind kind, uint32_t *map, uint32_t *next) {
    const TcNode n = *node_of(t, term);
    if (n.ground) return term;
    if (n.kind == kind) {
        uint32_t old = (uint32_t)n.value;
        if (!map[old]) map[old] = ++*next;
        return kind == TC_UNKNOWN ? tc_unknown(t, map[old] - 1) : tc_local(t, map[old] - 1);
    }
    if (n.kind != TC_EXPRESSION) return term;
    TcTerm done;
    if (walk_seen(t, term, &done)) return done;
    uint32_t count = n.length, start = n.start;
    TcTerm *items = malloc((count ? count : 1) * sizeof *items);
    if (!items) return 0;
    for (uint32_t i = 0; i < count; i++)
        if (!(items[i] = renumber_from(t, t->item[start + i], kind, map, next))) { free(items); return 0; }
    TcTerm out = tc_expression(t, items, count);
    free(items);
    if (out) walk_record(t, term, out);
    return out;
}

static TcTerm renumber(TcTerms *t, TcTerm term, TcKind kind, uint32_t *map, uint32_t *next) {
    walk_begin(t);
    return renumber_from(t, term, kind, map, next);
}

/* One more than the largest unknown index in row (0 when it has none). */
static void max_unknown_from(TcTerms *t, TcTerm row, uint32_t *most) {
    const TcNode *n = node_of(t, row);
    if (n->ground) return;
    if (n->kind == TC_UNKNOWN) {
        if ((uint32_t)n->value + 1 > *most) *most = (uint32_t)n->value + 1;
        return;
    }
    TcTerm done;
    if (n->kind != TC_EXPRESSION || walk_seen(t, row, &done)) return;
    uint32_t count = n->length, start = n->start;
    walk_record(t, row, 0);
    for (uint32_t i = 0; i < count; i++) max_unknown_from(t, t->item[start + i], most);
}

static uint32_t max_unknown(TcTerms *t, TcTerm row) {
    uint32_t most = 0;
    walk_begin(t);
    max_unknown_from(t, row, &most);
    return most;
}

/* Puts rows[0..*n) (closed terms) in canonical form, in place; with a set store
 * equal rows collapse.  *ground: whether the rows mention no unknown.  Returns
 * false when memory ran out. */
static bool canonical_form(TcTerms *t, TcTerm *rows, uint32_t *n, TcStore store, bool *ground) {
    uint32_t count = *n, unknowns = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t m = max_unknown(t, rows[i]);
        if (m > unknowns) unknowns = m;
    }
    *ground = unknowns == 0;
    uint32_t *pos = malloc((count ? count : 1) * sizeof *pos);
    TcTerm *sorted = malloc((count ? count : 1) * sizeof *sorted);
    if (!pos || !sorted) { free(pos); free(sorted); return false; }
    for (uint32_t i = 0; i < count; i++) pos[i] = i;
    if (!unknowns) {
        const TcNode *nodes = t->node;
        if (!nodes) { free(pos); free(sorted); return false; }
        {
            RowOrder order = {nodes, t->nodes, rows, rows, count, false};
            if (!sort_positions(&order, pos, count)) { free(pos); free(sorted); return false; }
        }
        uint32_t out = 0;
        for (uint32_t i = 0; i < count; i++) {
            TcTerm r = rows[pos[i]];
            if (store == TC_STORE_SET && out > 0 && sorted[out - 1] == r) continue;
            sorted[out++] = r;
        }
        if (out) memcpy(rows, sorted, out * sizeof *rows);
        *n = out;
        free(pos); free(sorted);
        return true;
    }
    {
        TcTerm *shape = malloc(count * sizeof *shape);
        uint32_t *map = calloc(unknowns, sizeof *map);
        TcTerm blank = tc_symbol(t, "?", 1);
        if (!shape || !map || !blank) {
            free(pos); free(shape); free(sorted); free(map);
            return false;
        }
        bool ok = true;
        for (uint32_t i = 0; ok && i < count; i++) ok = (shape[i] = blank_unknowns(t, rows[i], blank)) != 0;
        if (ok) {
            const TcNode *nodes = t->node;
            if (!nodes) ok = false;
            else {
                RowOrder order = {nodes, t->nodes, shape, rows, count, true};
                ok = sort_positions(&order, pos, count);
            }
        }
        uint32_t next = 0;
        for (uint32_t i = 0; ok && i < count; i++)
            ok = (sorted[i] = renumber(t, rows[pos[i]], TC_UNKNOWN, map, &next)) != 0;
        if (ok) {   /* the renamed rows in that order, and their shapes with them */
            for (uint32_t i = 0; i < count; i++) rows[i] = sorted[i];
            for (uint32_t i = 0; i < count; i++) sorted[i] = shape[pos[i]];
            for (uint32_t i = 0; i < count; i++) shape[i] = sorted[i];
            for (uint32_t i = 0; i < count; i++) pos[i] = i;
            const TcNode *nodes = t->node;
            if (!nodes) ok = false;
            else {
                RowOrder order = {nodes, t->nodes, shape, rows, count, true};
                ok = sort_positions(&order, pos, count);
            }
        }
        free(map);
        if (!ok) { free(pos); free(shape); free(sorted); return false; }
        uint32_t out = 0;
        for (uint32_t i = 0; i < count; i++) {
            TcTerm r = rows[pos[i]];
            if (store == TC_STORE_SET && out > 0 && sorted[out - 1] == r) continue;
            sorted[out++] = r;
        }
        if (out) memcpy(rows, sorted, out * sizeof *rows);
        *n = out;
        free(pos); free(shape); free(sorted);
    }
    return true;
}

/* The states met so far, each once. */
typedef struct {
    TcState *state; size_t states, state_cap;
    uint32_t *row;  size_t rows, row_cap;      /* the rows of every state */
    uint32_t *slot; size_t slots;              /* hash index: state + 1   */
    bool *ground; size_t grounds, ground_cap;  /* per state: no unknowns  */
} StateTable;

static uint64_t rows_hash(const TcTerm *rows, uint32_t n) {
    uint64_t h = UINT64_C(1469598103934665603) ^ n;
    for (uint32_t i = 0; i < n; i++) { h ^= rows[i]; h *= UINT64_C(1099511628211); }
    return h;
}

static bool states_grow(StateTable *st) {
    size_t slots = st->slots ? st->slots * 2 : 1024;
    uint32_t *slot = calloc(slots, sizeof *slot);
    if (!slot) return false;
    for (size_t k = 0; k < st->states; k++) {
        size_t i = st->state[k].hash & (slots - 1);
        while (slot[i]) i = (i + 1) & (slots - 1);
        slot[i] = (uint32_t)k + 1;
    }
    free(st->slot);
    st->slot = slot;
    st->slots = slots;
    return true;
}

/* The index of the state with these canonical rows (`ground`: they mention no
 * unknown), added if it is new.  Returns UINT32_MAX when memory ran out. */
static uint32_t state_index(StateTable *st, const TcTerm *rows, uint32_t n, bool ground) {
    uint64_t h = rows_hash(rows, n);
    if ((st->states + 1) * 2 > st->slots && !states_grow(st)) return UINT32_MAX;
    size_t i = h & (st->slots - 1);
    for (; st->slot[i]; i = (i + 1) & (st->slots - 1)) {
        const TcState *s = &st->state[st->slot[i] - 1];
        if (s->hash == h && s->rows == n &&
            (n == 0 || memcmp(st->row + s->start, rows, n * sizeof *rows) == 0))
            return st->slot[i] - 1;
    }
    TcState s = {.start = (uint32_t)st->rows, .rows = n, .hash = h};
    for (uint32_t k = 0; k < n; k++)
        if (!tc_push_u32(realloc, &st->row, &st->rows, &st->row_cap, rows[k])) return UINT32_MAX;
    if (!tc_push_flag(realloc, &st->ground, &st->grounds, &st->ground_cap, ground) ||
        !tc_push_state(realloc, &st->state, &st->states, &st->state_cap, s))
        return UINT32_MAX;
    st->slot[i] = (uint32_t)st->states;
    return (uint32_t)st->states - 1;
}

static void states_free(StateTable *st) {
    free(st->state); free(st->row); free(st->slot); free(st->ground);
}

/* ---- Rules and programs (section 4) --------------------------------------- */

struct TcProgram {
    TcTerms *terms;
    TcStoredRule *rule; size_t rules, rule_cap;
    uint32_t *term;     size_t terms_len, term_cap;   /* every rule's terms            */
    uint64_t *check;    size_t checks_len, check_cap; /* 4 words a check: kind, left, right, count */
    char *name;         size_t name_len, name_cap;    /* rule names, NUL-terminated    */
    uint32_t *row;      size_t rows, row_cap;         /* the initial state            */
};

TcProgram *tc_program_new(TcTerms *terms) {
    TcProgram *p = calloc(1, sizeof *p);
    if (!p) return NULL;
    *p = (TcProgram){.terms = terms};
    return p;
}

/* Releases the program and its pools; its term table is the caller's. */
static void program_release(TcProgram *p) {
    free(p->rule); free(p->term); free(p->check); free(p->name); free(p->row); free(p);
}

void tc_program_free(TcProgram *p) { if (p) program_release(p); }

TcTerms *tc_program_terms(TcProgram *p) { return p->terms; }
uint32_t tc_program_rules(const TcProgram *p) { return (uint32_t)p->rules; }
const TcTerm *tc_program_rows(const TcProgram *p, size_t *n) {
    *n = p->rows;
    return p->rows ? p->row : no_items;
}
const char *tc_program_rule_name(const TcProgram *p, uint32_t r) {
    return p->name + p->rule[r].name;
}

/* Whether every local of t is below `locals` and t mentions no unknown. */
static bool rule_term_ok(const TcTerms *t, TcTerm term, uint32_t locals) {
    if (!term) return false;
    TcNode n = term_node(t, term);
    if (n.ground) return true;
    if (n.kind == TC_LOCAL) return n.value < locals;
    if (n.kind == TC_UNKNOWN) return false;
    for (uint32_t i = 0; i < n.length; i++)
        if (!rule_term_ok(t, term_item(t, n.start + i), locals)) return false;
    return true;
}

static bool push_term(TcProgram *p, TcTerm term) {
    return tc_push_u32(realloc, &p->term, &p->terms_len, &p->term_cap, term);
}

/* Whether two stored rules are equal up to renaming: their locals are numbered
 * by first appearance, so this is equality of their terms and checks. */
static bool same_rule(const TcProgram *p, const TcStoredRule *a, const TcStoredRule *b) {
    if (a->takes != b->takes || a->reads != b->reads || a->tells != b->tells ||
        a->check_count != b->check_count || a->makes != b->makes || a->locals != b->locals)
        return false;
    size_t terms = (size_t)a->takes + a->reads + 2 * (size_t)a->tells + a->makes;
    for (size_t i = 0; i < terms; i++)
        if (p->term[a->terms + i] != p->term[b->terms + i]) return false;
    for (size_t i = 0; i < 4 * (size_t)a->check_count; i++)
        if (p->check[4 * (size_t)a->checks + i] != p->check[4 * (size_t)b->checks + i]) return false;
    return true;
}

bool tc_program_add_rule(TcProgram *p, const TcRule *r) {
    TcTerms *t = p->terms;
    if ((r->takes && !r->take) || (r->reads && !r->read) || (r->tells && !r->tell) ||
        (r->checks && !r->check) || (r->makes && !r->make)) return false;
    const TcTerm *take = r->take ? r->take : no_items;
    const TcTerm *read = r->read ? r->read : no_items;
    const TcTerm *tell = r->tell ? r->tell : no_items;
    const TcCheck *check = r->check ? r->check : no_checks;
    const TcTerm *make = r->make ? r->make : no_items;
    for (uint32_t i = 0; i < r->takes; i++) if (!rule_term_ok(t, take[i], r->locals)) return false;
    for (uint32_t i = 0; i < r->reads; i++) if (!rule_term_ok(t, read[i], r->locals)) return false;
    for (uint32_t i = 0; i < 2 * r->tells; i++) if (!rule_term_ok(t, tell[i], r->locals)) return false;
    for (uint32_t i = 0; i < r->makes; i++) if (!rule_term_ok(t, make[i], r->locals)) return false;
    for (uint32_t i = 0; i < r->checks; i++) {
        const TcCheck *c = &check[i];
        if (!rule_term_ok(t, c->left, r->locals)) return false;
        if (c->kind == TC_CHECK_EQUAL && !rule_term_ok(t, c->right, r->locals)) return false;
    }
    TcStoredRule s = {
        .name = (uint32_t)p->name_len, .terms = (uint32_t)p->terms_len,
        .checks = (uint32_t)(p->checks_len / 4), .takes = r->takes, .reads = r->reads,
        .tells = r->tells, .check_count = r->checks, .makes = r->makes,
    };
    /* the locals renumbered by first appearance, in the order take, read, tell,
     * check, make */
    uint32_t *map = calloc(r->locals ? r->locals : 1, sizeof *map), next = 0;
    if (!map) return false;
    bool ok = true;
    const char *name = r->name ? r->name : "";
    for (size_t i = 0; ok && i <= strlen(name); i++)
        ok = tc_push_char(realloc, &p->name, &p->name_len, &p->name_cap, name[i]);
    for (uint32_t i = 0; ok && i < r->takes; i++) {
        TcTerm u = renumber(t, take[i], TC_LOCAL, map, &next);
        ok = u && push_term(p, u);
    }
    for (uint32_t i = 0; ok && i < r->reads; i++) {
        TcTerm u = renumber(t, read[i], TC_LOCAL, map, &next);
        ok = u && push_term(p, u);
    }
    for (uint32_t i = 0; ok && i < 2 * r->tells; i++) {
        TcTerm u = renumber(t, tell[i], TC_LOCAL, map, &next);
        ok = u && push_term(p, u);
    }
    for (uint32_t i = 0; ok && i < r->checks; i++) {
        const TcCheck *c = &check[i];
        TcTerm left = renumber(t, c->left, TC_LOCAL, map, &next);
        TcTerm right = c->kind == TC_CHECK_EQUAL ? renumber(t, c->right, TC_LOCAL, map, &next) : 0;
        uint64_t words[4] = {(uint64_t)c->kind, left, right, c->count};
        ok = left && (right || c->kind != TC_CHECK_EQUAL);
        for (int k = 0; ok && k < 4; k++)
            ok = tc_push_u64(realloc, &p->check, &p->checks_len, &p->check_cap, words[k]);
    }
    for (uint32_t i = 0; ok && i < r->makes; i++) {
        TcTerm u = renumber(t, make[i], TC_LOCAL, map, &next);
        ok = u && push_term(p, u);
    }
    free(map);
    s.locals = next;
    s.same = (uint32_t)p->rules;
    for (size_t k = 0; ok && k < p->rules; k++)
        if (same_rule(p, &p->rule[k], &s)) { s.same = p->rule[k].same; break; }
    if (ok && tc_push_rule(realloc, &p->rule, &p->rules, &p->rule_cap, s)) return true;
    p->name_len = s.name;   /* nothing of the refused rule stays */
    p->terms_len = s.terms;
    p->checks_len = 4 * (size_t)s.checks;
    return false;
}

/* Whether t mentions no local (a row may mention unknowns of the state). */
static bool row_term_ok(const TcTerms *t, TcTerm term) {
    if (!term) return false;
    TcNode n = term_node(t, term);
    if (n.ground || n.kind == TC_UNKNOWN) return true;
    if (n.kind == TC_LOCAL) return false;
    for (uint32_t i = 0; i < n.length; i++)
        if (!row_term_ok(t, term_item(t, n.start + i))) return false;
    return true;
}

bool tc_program_add_row(TcProgram *p, TcTerm row) {
    if (!row_term_ok(p->terms, row)) return false;
    return tc_push_u32(realloc, &p->row, &p->rows, &p->row_cap, row);
}

/* A program as one exploration reads it: its pools, borrowed.  A rule's terms
 * are consecutive: its take patterns, its read patterns, its tells (two terms
 * an equation), its made rows. */
typedef struct {
    const TcStoredRule *rule; size_t rules;
    const TcTerm *term; size_t terms;
    const uint64_t *check; size_t checks;
    const char *name;
} ProgramView;

static ProgramView program_view(const TcProgram *p) {
    return (ProgramView){p->rule, p->rules, p->term, p->terms_len, p->check, p->checks_len, p->name};
}

static inline TcStoredRule rule_at(const ProgramView *v, uint32_t rule) { return v->rule[rule]; }
/* Pattern k: the takes, then the reads. */
static inline TcTerm pattern_at(const ProgramView *v, const TcStoredRule *r, uint32_t k) {
    return v->term[r->terms + k];
}
static inline TcTerm tell_at(const ProgramView *v, const TcStoredRule *r, uint32_t i) {
    return v->term[r->terms + r->takes + r->reads + i];
}
static inline TcTerm make_at(const ProgramView *v, const TcStoredRule *r, uint32_t k) {
    return v->term[r->terms + r->takes + r->reads + 2 * r->tells + k];
}
static inline TcCheck check_at(const ProgramView *v, const TcStoredRule *r, uint32_t i) {
    const uint64_t *w = v->check + 4 * ((size_t)r->checks + i);
    return (TcCheck){(TcCheckKind)w[0], (TcTerm)w[1], (TcTerm)w[2], w[3]};
}

/* ---- What an exploration leaves (section 7) ------------------------------- */

struct TcOutcomes {
    TcTerm *row; size_t rows, row_cap;          /* the rows of every listed state */
    uint32_t *start; size_t starts, start_cap;  /* outcome i: rows start[i] .. */
    uint32_t *size;  size_t sizes, size_cap;
    uint32_t *amb_start; size_t ambs, amb_cap;  /* ambiguous states            */
    uint32_t *amb_size;  size_t amb_sizes, amb_size_cap;
    uint64_t *amb_matches; size_t amb_match_len, amb_match_cap;
    size_t outcomes;
    bool finished, memory_ok, cyclic, decider_failed, disagreed;
    uint64_t states, work;
};

static bool list_state(TcOutcomes *o, const StateTable *st, uint32_t s, bool ambiguous,
                       uint64_t matches) {
    const TcState *rec = &st->state[s];
    uint32_t start = (uint32_t)o->rows;
    for (uint32_t i = 0; i < rec->rows; i++)
        if (!tc_push_u32(realloc, &o->row, &o->rows, &o->row_cap, st->row[rec->start + i]))
            return false;
    if (ambiguous)
        return tc_push_u32(realloc, &o->amb_start, &o->ambs, &o->amb_cap, start) &&
               tc_push_u32(realloc, &o->amb_size, &o->amb_sizes, &o->amb_size_cap, rec->rows) &&
               tc_push_u64(realloc, &o->amb_matches, &o->amb_match_len, &o->amb_match_cap, matches);
    o->outcomes++;
    return tc_push_u32(realloc, &o->start, &o->starts, &o->start_cap, start) &&
           tc_push_u32(realloc, &o->size, &o->sizes, &o->size_cap, rec->rows);
}

/* A structural order on terms: by kind, then by value, bytes or items, never
 * by term id, so that it does not depend on the order terms were made in. */
static int term_order(const TcTerms *t, TcTerm a, TcTerm b) {
    if (a == b) return 0;
    const TcNode *x = node_of(t, a), *y = node_of(t, b);
    if (x->kind != y->kind) return x->kind < y->kind ? -1 : 1;
    if (x->kind == TC_SYMBOL || x->kind == TC_STRING) {
        uint32_t n = x->length < y->length ? x->length : y->length;
        int c = n ? memcmp(t->byte + x->start, t->byte + y->start, n) : 0;
        if (c) return c < 0 ? -1 : 1;
    } else if (x->kind == TC_EXPRESSION) {
        uint32_t n = x->length < y->length ? x->length : y->length;
        for (uint32_t i = 0; i < n; i++) {
            int c = term_order(t, t->item[x->start + i], t->item[y->start + i]);
            if (c) return c;
        }
    } else if (x->value != y->value) {   /* integer, local, unknown */
        return x->value < y->value ? -1 : 1;
    }
    return x->length < y->length ? -1 : x->length > y->length;
}

/* Sorts terms by term_order (merge sort, stable); false when memory ran out. */
static bool sort_terms(const TcTerms *t, TcTerm *items, uint32_t n) {
    TcTerm *tmp = malloc((n ? n : 1) * sizeof *tmp);
    if (!tmp) return false;
    for (uint32_t width = 1; width < n; width *= 2) {
        for (uint32_t lo = 0; lo < n; lo += 2 * width) {
            uint32_t mid = lo + width < n ? lo + width : n;
            uint32_t hi = lo + 2 * width < n ? lo + 2 * width : n;
            uint32_t i = lo, j = mid, k = lo;
            while (i < mid && j < hi)
                tmp[k++] = term_order(t, items[i], items[j]) <= 0 ? items[i++] : items[j++];
            while (i < mid) tmp[k++] = items[i++];
            while (j < hi) tmp[k++] = items[j++];
        }
        memcpy(items, tmp, n * sizeof *items);
    }
    free(tmp);
    return true;
}

/* The structural order of two states: their rows each sorted by term_order,
 * then compared in turn, the shorter first when one is the other's prefix.
 * Returns -2 when memory ran out. */
static int state_order(const StateTable *st, const TcTerms *t, uint32_t a, uint32_t b) {
    const TcState *x = &st->state[a], *y = &st->state[b];
    TcTerm *ra = malloc((x->rows ? x->rows : 1) * sizeof *ra);
    TcTerm *rb = malloc((y->rows ? y->rows : 1) * sizeof *rb);
    int verdict = -2;
    if (ra && rb) {
        if (x->rows) memcpy(ra, st->row + x->start, x->rows * sizeof *ra);
        if (y->rows) memcpy(rb, st->row + y->start, y->rows * sizeof *rb);
    }
    if (ra && rb && sort_terms(t, ra, x->rows) && sort_terms(t, rb, y->rows)) {
        verdict = 0;
        for (uint32_t i = 0; verdict == 0 && i < x->rows && i < y->rows; i++)
            verdict = term_order(t, ra[i], rb[i]);
        if (verdict == 0 && x->rows != y->rows) verdict = x->rows < y->rows ? -1 : 1;
    }
    free(ra); free(rb);
    return verdict;
}

static void outcomes_release(TcOutcomes *o) {
    free(o->row); free(o->start); free(o->size);
    free(o->amb_start); free(o->amb_size); free(o->amb_matches);
    free(o);
}

void tc_outcomes_free(TcOutcomes *o) { if (o) outcomes_release(o); }

size_t tc_outcomes_count(const TcOutcomes *o) { return o->outcomes; }
/* (Rows of states that are all empty are never stored: the list may be null,
 * and an empty state's rows are then the empty list.) */
const TcTerm *tc_outcome_rows(const TcOutcomes *o, size_t i, size_t *rows) {
    *rows = o->size[i];
    return o->row ? o->row + o->start[i] : no_items;
}
size_t tc_outcomes_ambiguous(const TcOutcomes *o) { return o->ambs; }
const TcTerm *tc_ambiguous_rows(const TcOutcomes *o, size_t i, size_t *rows, size_t *matches) {
    *rows = o->amb_size[i];
    *matches = (size_t)o->amb_matches[i];
    return o->row ? o->row + o->amb_start[i] : no_items;
}
bool tc_outcomes_finished(const TcOutcomes *o) { return o->finished; }
uint64_t tc_outcomes_states(const TcOutcomes *o) { return o->states; }
uint64_t tc_outcomes_work(const TcOutcomes *o) { return o->work; }
bool tc_outcomes_complete_memory(const TcOutcomes *o) { return o->memory_ok; }
bool tc_outcomes_cyclic(const TcOutcomes *o) { return o->cyclic; }
bool tc_outcomes_decider_failed(const TcOutcomes *o) { return o->decider_failed; }
bool tc_outcomes_shortcuts_disagreed(const TcOutcomes *o) { return o->disagreed; }

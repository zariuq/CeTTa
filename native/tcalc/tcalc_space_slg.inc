/* tcalc_space_slg.inc - incremental semi-naive demand-driven SLG executor over
 * compiled tcalc space clauses with inline arithmetic/comparison/list/control
 * ops, argument-key indexed data queries, selectivity-ordered multi-way joins,
 * higher-order calls & list combinators, iterative 3-color demand-cycle
 * detection, and exact O(V+E) bottom-up DAG multiplicity semiring.
 *
 * Included by native/tcalc/tcalc_cetta_module.c. */

static inline bool tcalc_term_is_real_expr(const TcTerms *terms,
                                           const TcalcSpaceCacheEntry *entry,
                                           TcTerm t) {
    if (tc_kind(terms, t) != TC_EXPRESSION) return false;
    uint32_t n = tc_length(terms, t);
    if (n == 2u) {
        TcTerm hd = tc_item(terms, t, 0u);
        if (hd == entry->bool_sym || hd == entry->float_sym)
            return false;
    }
    return true;
}

static inline bool tcalc_term_as_bool(const TcTerms *terms,
                                      const TcalcSpaceCacheEntry *entry,
                                      CettaLanguageId language_id,
                                      TcTerm t, bool *out) {
    if (tc_kind(terms, t) == TC_EXPRESSION && tc_length(terms, t) == 2u &&
        tc_item(terms, t, 0u) == entry->bool_sym &&
        tc_kind(terms, tc_item(terms, t, 1u)) == TC_INTEGER) {
        *out = tc_value(terms, tc_item(terms, t, 1u)) != 0;
        return true;
    }
    if (language_id == CETTA_LANGUAGE_PETTA) {
        if (t == entry->true_sym_tc || t == entry->true_lower_tc) {
            *out = true;
            return true;
        }
        if (t == entry->false_sym_tc || t == entry->false_lower_tc) {
            *out = false;
            return true;
        }
    }
    return false;
}

static bool tcalc_terms_equal(const TcTerms *terms, TcTerm float_sym,
                              TcTerm a, TcTerm b, uint32_t depth,
                              bool *fallback) {
    if (a == b) return true;
    if (depth > TCALC_SPACE_MAX_DEPTH) {
        *fallback = true;
        return false;
    }
    TcKind ka = tc_kind(terms, a);
    TcKind kb = tc_kind(terms, b);
    double fa = 0.0, fb = 0.0;
    bool a_is_f = tcalc_term_as_float(terms, float_sym, a, &fa);
    bool b_is_f = tcalc_term_as_float(terms, float_sym, b, &fb);
    if (a_is_f && b_is_f) return fa == fb;
    if (a_is_f && kb == TC_INTEGER) {
        int64_t iv = tc_value(terms, b);
        if (iv > (INT64_C(1) << 53) || iv < -(INT64_C(1) << 53)) {
            *fallback = true;
            return false;
        }
        return fa == (double)iv;
    }
    if (b_is_f && ka == TC_INTEGER) {
        int64_t iv = tc_value(terms, a);
        if (iv > (INT64_C(1) << 53) || iv < -(INT64_C(1) << 53)) {
            *fallback = true;
            return false;
        }
        return (double)iv == fb;
    }
    if (a_is_f || b_is_f) return false;
    if (ka != kb || ka != TC_EXPRESSION) return false;
    uint32_t n = tc_length(terms, a);
    if (n != tc_length(terms, b)) return false;
    for (uint32_t i = 0u; i < n; i++) {
        if (!tcalc_terms_equal(terms, float_sym, tc_item(terms, a, i),
                               tc_item(terms, b, i), depth + 1u, fallback))
            return false;
        if (*fallback) return false;
    }
    return true;
}

static bool tcalc_slg_match(const TcTerms *terms, TcTerm float_sym,
                            TcTerm pat, TcTerm val, TcTerm *locals,
                            uint32_t depth, bool *fallback) {
    if (depth > TCALC_SPACE_MAX_DEPTH) {
        *fallback = true;
        return false;
    }
    TcKind pk = tc_kind(terms, pat);
    if (pk == TC_LOCAL) {
        uint32_t idx = tc_index(terms, pat);
        if (locals[idx] == 0u) {
            locals[idx] = val;
            return true;
        }
        return tcalc_terms_equal(terms, float_sym, locals[idx], val, 0u, fallback);
    }
    if (pat == val) return true;
    if (pk != TC_EXPRESSION || tc_kind(terms, val) != TC_EXPRESSION)
        return tcalc_terms_equal(terms, float_sym, pat, val, 0u, fallback);
    if (tc_length(terms, pat) == 2u && tc_item(terms, pat, 0u) == float_sym)
        return tcalc_terms_equal(terms, float_sym, pat, val, 0u, fallback);
    uint32_t n = tc_length(terms, pat);
    if (n != tc_length(terms, val)) return false;
    for (uint32_t i = 0u; i < n; i++) {
        if (!tcalc_slg_match(terms, float_sym, tc_item(terms, pat, i),
                             tc_item(terms, val, i), locals, depth + 1u, fallback))
            return false;
        if (*fallback) return false;
    }
    return true;
}

static TcTerm tcalc_slg_inst(TcTerms *terms, TcTerm pat, const TcTerm *locals,
                             uint32_t depth, bool *fallback) {
    if (!pat || depth > TCALC_SPACE_MAX_DEPTH) {
        *fallback = true;
        return 0u;
    }
    TcKind k = tc_kind(terms, pat);
    if (k == TC_LOCAL) {
        TcTerm v = locals[tc_index(terms, pat)];
        if (!v) *fallback = true;
        return v;
    }
    if (k != TC_EXPRESSION) return pat;
    uint32_t n = tc_length(terms, pat);
    if (n == 0u) return pat;
    TcTerm stack_items[16];
    TcTerm *items = n <= 16u ? stack_items : malloc((size_t)n * sizeof(*items));
    if (!items) { *fallback = true; return 0u; }
    bool changed = false;
    for (uint32_t i = 0u; i < n; i++) {
        TcTerm orig = tc_item(terms, pat, i);
        TcTerm sub = tcalc_slg_inst(terms, orig, locals, depth + 1u, fallback);
        if (*fallback || !sub) {
            if (items != stack_items) free(items);
            return 0u;
        }
        items[i] = sub;
        if (sub != orig) changed = true;
    }
    TcTerm res = changed ? tc_expression(terms, items, n) : pat;
    if (items != stack_items) free(items);
    if (!res) *fallback = true;
    return res;
}

/* Instantiate PAT only if every local inside PAT is currently bound; returns 0
 * (without setting fallback) when any local inside PAT is still unbound. */
static TcTerm tcalc_slg_try_inst_ground(TcTerms *terms, TcTerm pat,
                                        const TcTerm *locals, uint32_t depth) {
    if (!pat || depth > 32u) return 0u;
    TcKind k = tc_kind(terms, pat);
    if (k == TC_LOCAL)
        return locals[tc_index(terms, pat)];
    if (k != TC_EXPRESSION) return pat;
    uint32_t n = tc_length(terms, pat);
    if (n == 0u) return pat;
    TcTerm stack_items[16];
    TcTerm *items = n <= 16u ? stack_items : malloc((size_t)n * sizeof(*items));
    if (!items) return 0u;
    bool changed = false;
    for (uint32_t i = 0u; i < n; i++) {
        TcTerm orig = tc_item(terms, pat, i);
        TcTerm sub = tcalc_slg_try_inst_ground(terms, orig, locals, depth + 1u);
        if (!sub) {
            if (items != stack_items) free(items);
            return 0u;
        }
        items[i] = sub;
        if (sub != orig) changed = true;
    }
    TcTerm res = changed ? tc_expression(terms, items, n) : pat;
    if (items != stack_items) free(items);
    return res;
}

static TcTerm tcalc_slg_eval_prim(TcalcSpaceCacheEntry *entry,
                                  CettaLanguageId language_id,
                                  TcalcPrimOp op, TcTerm a, TcTerm b,
                                  bool *fallback) {
    TcTerms *terms = entry->terms;
    TcTerm fsym = entry->float_sym;
    TcTerm bsym = entry->bool_sym;

    if (op == TCPRIM_EQ || op == TCPRIM_NEQ) {
        bool eq = tcalc_terms_equal(terms, fsym, a, b, 0u, fallback);
        if (*fallback) return 0u;
        bool ans = (op == TCPRIM_EQ) ? eq : !eq;
        return tcalc_encode_bool(terms, bsym, ans);
    }
    if (op == TCPRIM_NOT) {
        bool ba = false;
        if (!tcalc_term_as_bool(terms, entry, language_id, a, &ba)) {
            *fallback = true;
            return 0u;
        }
        return tcalc_encode_bool(terms, bsym, !ba);
    }
    if (op == TCPRIM_AND || op == TCPRIM_OR) {
        bool ba = false, bb = false;
        if (!tcalc_term_as_bool(terms, entry, language_id, a, &ba) ||
            !tcalc_term_as_bool(terms, entry, language_id, b, &bb)) {
            *fallback = true;
            return 0u;
        }
        return tcalc_encode_bool(terms, bsym, op == TCPRIM_AND ? (ba && bb) : (ba || bb));
    }

    /* List / expression primitives */
    if (op == TCPRIM_CAR) {
        if (!tcalc_term_is_real_expr(terms, entry, a) || tc_length(terms, a) == 0u) {
            *fallback = true;
            return 0u;
        }
        return tc_item(terms, a, 0u);
    }
    if (op == TCPRIM_CDR) {
        if (!tcalc_term_is_real_expr(terms, entry, a) || tc_length(terms, a) == 0u) {
            *fallback = true;
            return 0u;
        }
        uint32_t n = tc_length(terms, a);
        if (n == 1u) return tc_expression(terms, NULL, 0u);
        uint32_t m = n - 1u;
        TcTerm stack_items[16];
        TcTerm *items = m <= 16u ? stack_items : malloc((size_t)m * sizeof(*items));
        if (!items) { *fallback = true; return 0u; }
        for (uint32_t i = 0u; i < m; i++)
            items[i] = tc_item(terms, a, i + 1u);
        TcTerm res = tc_expression(terms, items, m);
        if (items != stack_items) free(items);
        if (!res) *fallback = true;
        return res;
    }
    if (op == TCPRIM_CONS) {
        if (!tcalc_term_is_real_expr(terms, entry, b)) {
            *fallback = true;
            return 0u;
        }
        uint32_t n = tc_length(terms, b);
        uint32_t m = n + 1u;
        TcTerm stack_items[16];
        TcTerm *items = m <= 16u ? stack_items : malloc((size_t)m * sizeof(*items));
        if (!items) { *fallback = true; return 0u; }
        items[0] = a;
        for (uint32_t i = 0u; i < n; i++)
            items[i + 1u] = tc_item(terms, b, i);
        TcTerm res = tc_expression(terms, items, m);
        if (items != stack_items) free(items);
        if (!res) *fallback = true;
        return res;
    }
    if (op == TCPRIM_DECONS) {
        if (!tcalc_term_is_real_expr(terms, entry, a) || tc_length(terms, a) == 0u) {
            *fallback = true;
            return 0u;
        }
        TcTerm hd = tc_item(terms, a, 0u);
        TcTerm tl = tcalc_slg_eval_prim(entry, language_id, TCPRIM_CDR, a, 0u, fallback);
        if (*fallback || !tl) return 0u;
        TcTerm pair[2] = {hd, tl};
        TcTerm res = tc_expression(terms, pair, 2u);
        if (!res) *fallback = true;
        return res;
    }
    if (op == TCPRIM_SIZE) {
        if (!tcalc_term_is_real_expr(terms, entry, a)) {
            *fallback = true;
            return 0u;
        }
        return tc_integer(terms, (int64_t)tc_length(terms, a));
    }
    if (op == TCPRIM_INDEX) {
        if (!tcalc_term_is_real_expr(terms, entry, a) ||
            tc_kind(terms, b) != TC_INTEGER) {
            *fallback = true;
            return 0u;
        }
        int64_t idx = tc_value(terms, b);
        uint32_t n = tc_length(terms, a);
        if (idx < 0 || (uint64_t)idx >= (uint64_t)n) {
            *fallback = true;
            return 0u;
        }
        return tc_item(terms, a, (uint32_t)idx);
    }

    /* Numeric arithmetic and ordering operations */
    bool a_int = tc_kind(terms, a) == TC_INTEGER;
    bool b_int = tc_kind(terms, b) == TC_INTEGER;
    double fa = 0.0, fb = 0.0;
    bool a_flt = !a_int && tcalc_term_as_float(terms, fsym, a, &fa);
    bool b_flt = !b_int && tcalc_term_as_float(terms, fsym, b, &fb);
    if ((!a_int && !a_flt) || (!b_int && !b_flt)) {
        *fallback = true;
        return 0u;
    }

    if (a_int && b_int) {
        int64_t ia = tc_value(terms, a);
        int64_t ib = tc_value(terms, b);
        int64_t r = 0;
        switch (op) {
        case TCPRIM_ADD:
            if (__builtin_add_overflow(ia, ib, &r)) { *fallback = true; return 0u; }
            return tc_integer(terms, r);
        case TCPRIM_SUB:
            if (__builtin_sub_overflow(ia, ib, &r)) { *fallback = true; return 0u; }
            return tc_integer(terms, r);
        case TCPRIM_MUL:
            if (__builtin_mul_overflow(ia, ib, &r)) { *fallback = true; return 0u; }
            return tc_integer(terms, r);
        case TCPRIM_DIV:
            if (ib == 0 || (ia == INT64_MIN && ib == -1)) { *fallback = true; return 0u; }
            if (ia % ib == 0) return tc_integer(terms, ia / ib);
            return tcalc_encode_float(terms, fsym, (double)ia / (double)ib);
        case TCPRIM_MOD:
            if (ib <= 0 || ia < 0) { *fallback = true; return 0u; }
            return tc_integer(terms, ia % ib);
        case TCPRIM_LT: return tcalc_encode_bool(terms, bsym, ia < ib);
        case TCPRIM_LE: return tcalc_encode_bool(terms, bsym, ia <= ib);
        case TCPRIM_GT: return tcalc_encode_bool(terms, bsym, ia > ib);
        case TCPRIM_GE: return tcalc_encode_bool(terms, bsym, ia >= ib);
        default: *fallback = true; return 0u;
        }
    }

    /* Mixed int/float or float/float */
    if (a_int) {
        int64_t ia = tc_value(terms, a);
        if (ia > (INT64_C(1) << 53) || ia < -(INT64_C(1) << 53)) {
            *fallback = true;
            return 0u;
        }
        fa = (double)ia;
    }
    if (b_int) {
        int64_t ib = tc_value(terms, b);
        if (ib > (INT64_C(1) << 53) || ib < -(INT64_C(1) << 53)) {
            *fallback = true;
            return 0u;
        }
        fb = (double)ib;
    }
    double fr = 0.0;
    switch (op) {
    case TCPRIM_ADD: fr = fa + fb; break;
    case TCPRIM_SUB: fr = fa - fb; break;
    case TCPRIM_MUL: fr = fa * fb; break;
    case TCPRIM_DIV:
        if (fb == 0.0) { *fallback = true; return 0u; }
        fr = fa / fb;
        break;
    case TCPRIM_LT: return tcalc_encode_bool(terms, bsym, fa < fb);
    case TCPRIM_LE: return tcalc_encode_bool(terms, bsym, fa <= fb);
    case TCPRIM_GT: return tcalc_encode_bool(terms, bsym, fa > fb);
    case TCPRIM_GE: return tcalc_encode_bool(terms, bsym, fa >= fb);
    default: *fallback = true; return 0u;
    }
    TcTerm enc = tcalc_encode_float(terms, fsym, fr);
    if (!enc) *fallback = true;
    return enc;
}

typedef struct {
    uint32_t sub_call_id;
    uint32_t wid;
    TcTerm sub_val;
} TcalcSlgSubPair;

typedef struct {
    uint32_t caller_id;
    uint32_t sub_id;
    uint32_t clause_idx;
    uint32_t branch_ord;
    uint32_t next_pc;
    TcTerm target_pat;
    uint32_t local_off;
    uint32_t sub_off;
    uint32_t sub_len;
    uint32_t list_task_id; /* UINT32_MAX for ordinary TCEXEC_CALL waiters */
    uint32_t next_waiter;  /* intrusive linked list per sub_id */
} TcalcSlgWaiter;

typedef struct {
    uint32_t caller_id;
    uint32_t clause_idx;
    uint32_t branch_ord;
    uint32_t next_pc;
    uint32_t target_local;
    uint32_t ho_idx;
    TcTerm list_tc;
    uint32_t list_len;
    uint32_t cur_idx;
    TcTerm acc_tc;
    uint32_t out_off;
    uint32_t out_len;
    uint32_t local_off;
    uint32_t sub_off;
    uint32_t sub_len;
    bool branched;
} TcalcSlgListTask;

typedef struct {
    uint32_t waiter_idx;
    TcTerm val;
} TcalcSlgResume;

typedef struct {
    TcTerm val;
    uint32_t next_val; /* intrusive linked list per call_id */
} TcalcSlgValNode;

typedef struct {
    uint32_t call_id;
    uint32_t clause_idx;
    uint32_t branch_ord;
    uint32_t sub_off;
    uint32_t sub_len;
    TcTerm val;
} TcalcSlgDeriv;

typedef struct {
    uint32_t caller_id;
    uint32_t callee_id;
} TcalcSlgEdge;

typedef struct {
    TcalcSpaceCacheEntry *entry;
    CettaLanguageId language_id;
    bool presence_only;
    bool fallback;
    uint64_t work;
    uint32_t branch_seq;
    TcTerm max_term_seen;

    /* Call table (id = 0 .. call_len - 1) */
    TcTerm *call_terms;
    uint8_t *call_matched;
    uint32_t *val_head;
    uint32_t *waiter_head;
    uint32_t call_len, call_cap;

    /* Dynamic open-addressed map: call TcTerm -> call_id + 1 */
    TcTerm *cm_keys;
    uint32_t *cm_vals;
    size_t cm_cap;

    /* Dynamic open-addressed set: ((uint64_t)(call_id + 1) << 32) | val */
    uint64_t *cv_keys;
    size_t cv_len, cv_cap;

    /* Queues and flat slabs */
    uint32_t *demand_q;
    size_t demand_head, demand_len, demand_cap;

    TcalcSlgResume *resume_q;
    size_t resume_head, resume_len, resume_cap;

    TcalcSlgValNode *vals;
    uint32_t val_len, val_cap;

    TcalcSlgWaiter *waiters;
    uint32_t waiter_len, waiter_cap;

    TcalcSlgListTask *list_tasks;
    uint32_t list_task_len, list_task_cap;

    TcTerm *list_out_slab;
    uint32_t list_out_len, list_out_cap;

    TcTerm *local_slab;
    uint32_t local_len, local_cap;

    TcalcSlgSubPair *sub_slab;
    uint32_t sub_len, sub_cap;

    TcalcSlgDeriv *derivs;
    uint32_t deriv_len, deriv_cap;

    TcalcSlgEdge *edges;
    uint32_t edge_len, edge_cap;
} TcalcSlgState;

static void tcalc_slg_free(TcalcSlgState *st) {
    free(st->call_terms);
    free(st->call_matched);
    free(st->val_head);
    free(st->waiter_head);
    free(st->cm_keys);
    free(st->cm_vals);
    free(st->cv_keys);
    free(st->demand_q);
    free(st->resume_q);
    free(st->vals);
    free(st->waiters);
    free(st->list_tasks);
    free(st->list_out_slab);
    free(st->local_slab);
    free(st->sub_slab);
    free(st->derivs);
    free(st->edges);
}

static bool tcalc_slg_grow_cm(TcalcSlgState *st) {
    size_t ncap = st->cm_cap ? st->cm_cap * 2u : 64u;
    TcTerm *nk = calloc(ncap, sizeof(*nk));
    uint32_t *nv = calloc(ncap, sizeof(*nv));
    if (!nk || !nv) { free(nk); free(nv); return false; }
    size_t mask = ncap - 1u;
    for (size_t i = 0u; i < st->cm_cap; i++) {
        if (!st->cm_keys[i]) continue;
        size_t slot = (size_t)tcalc_mix64((uint64_t)st->cm_keys[i]) & mask;
        while (nk[slot]) slot = (slot + 1u) & mask;
        nk[slot] = st->cm_keys[i];
        nv[slot] = st->cm_vals[i];
    }
    free(st->cm_keys);
    free(st->cm_vals);
    st->cm_keys = nk;
    st->cm_vals = nv;
    st->cm_cap = ncap;
    return true;
}

static uint32_t tcalc_slg_demand_call(TcalcSlgState *st, TcTerm call_tc) {
    if (call_tc > st->max_term_seen) st->max_term_seen = call_tc;
    if ((size_t)(st->call_len + 1u) * 2u >= st->cm_cap) {
        if (!tcalc_slg_grow_cm(st)) { st->fallback = true; return UINT32_MAX; }
    }
    size_t mask = st->cm_cap - 1u;
    size_t slot = (size_t)tcalc_mix64((uint64_t)call_tc) & mask;
    while (st->cm_keys[slot]) {
        if (st->cm_keys[slot] == call_tc) return st->cm_vals[slot] - 1u;
        slot = (slot + 1u) & mask;
    }
    if (st->call_len >= TCALC_SPACE_MAX_SLG_CALLS) {
        st->fallback = true;
        return UINT32_MAX;
    }
    if (st->call_len == st->call_cap) {
        uint32_t ncap = st->call_cap ? st->call_cap * 2u : 32u;
        TcTerm *ct = realloc(st->call_terms, (size_t)ncap * sizeof(*ct));
        uint8_t *cm = realloc(st->call_matched, (size_t)ncap * sizeof(*cm));
        uint32_t *vh = realloc(st->val_head, (size_t)ncap * sizeof(*vh));
        uint32_t *wh = realloc(st->waiter_head, (size_t)ncap * sizeof(*wh));
        if (!ct || !cm || !vh || !wh) {
            if (ct) st->call_terms = ct;
            if (cm) st->call_matched = cm;
            if (vh) st->val_head = vh;
            if (wh) st->waiter_head = wh;
            st->fallback = true;
            return UINT32_MAX;
        }
        st->call_terms = ct;
        st->call_matched = cm;
        st->val_head = vh;
        st->waiter_head = wh;
        st->call_cap = ncap;
    }
    uint32_t id = st->call_len++;
    st->call_terms[id] = call_tc;
    st->call_matched[id] = 0u;
    st->val_head[id] = UINT32_MAX;
    st->waiter_head[id] = UINT32_MAX;
    st->cm_keys[slot] = call_tc;
    st->cm_vals[slot] = id + 1u;

    if (st->demand_len == st->demand_cap) {
        size_t ncap = st->demand_cap ? st->demand_cap * 2u : 32u;
        uint32_t *dq = realloc(st->demand_q, ncap * sizeof(*dq));
        if (!dq) { st->fallback = true; return UINT32_MAX; }
        st->demand_q = dq;
        st->demand_cap = ncap;
    }
    st->demand_q[st->demand_len++] = id;
    return id;
}

static bool tcalc_slg_seen_cv_insert(TcalcSlgState *st, uint32_t call_id, TcTerm val) {
    if ((st->cv_len + 1u) * 2u >= st->cv_cap) {
        size_t ncap = st->cv_cap ? st->cv_cap * 2u : 64u;
        uint64_t *nk = calloc(ncap, sizeof(*nk));
        if (!nk) { st->fallback = true; return false; }
        size_t mask = ncap - 1u;
        for (size_t i = 0u; i < st->cv_cap; i++) {
            if (!st->cv_keys[i]) continue;
            size_t slot = (size_t)tcalc_mix64(st->cv_keys[i]) & mask;
            while (nk[slot]) slot = (slot + 1u) & mask;
            nk[slot] = st->cv_keys[i];
        }
        free(st->cv_keys);
        st->cv_keys = nk;
        st->cv_cap = ncap;
    }
    uint64_t k = ((uint64_t)(call_id + 1u) << 32) | (uint64_t)val;
    size_t mask = st->cv_cap - 1u;
    size_t slot = (size_t)tcalc_mix64(k) & mask;
    while (st->cv_keys[slot]) {
        if (st->cv_keys[slot] == k) return false; /* already seen */
        slot = (slot + 1u) & mask;
    }
    st->cv_keys[slot] = k;
    st->cv_len++;
    return true;
}

static bool tcalc_slg_push_resume(TcalcSlgState *st, uint32_t waiter_idx, TcTerm val) {
    if (st->resume_len == st->resume_cap) {
        size_t ncap = st->resume_cap ? st->resume_cap * 2u : 64u;
        TcalcSlgResume *rq = realloc(st->resume_q, ncap * sizeof(*rq));
        if (!rq) { st->fallback = true; return false; }
        st->resume_q = rq;
        st->resume_cap = ncap;
    }
    st->resume_q[st->resume_len++] = (TcalcSlgResume){
        .waiter_idx = waiter_idx, .val = val
    };
    return true;
}

static uint32_t tcalc_slg_alloc_locals(TcalcSlgState *st, const TcTerm *src,
                                       uint32_t count) {
    if (count == 0u) return 0u;
    if (st->local_len + count > st->local_cap) {
        uint32_t ncap = st->local_cap ? st->local_cap * 2u : 256u;
        while (ncap < st->local_len + count) ncap *= 2u;
        TcTerm *grown = realloc(st->local_slab, (size_t)ncap * sizeof(*grown));
        if (!grown) { st->fallback = true; return UINT32_MAX; }
        st->local_slab = grown;
        st->local_cap = ncap;
    }
    uint32_t off = st->local_len;
    memcpy(st->local_slab + off, src, (size_t)count * sizeof(*src));
    st->local_len += count;
    return off;
}

static uint32_t tcalc_slg_alloc_list_out(TcalcSlgState *st, uint32_t count) {
    if (count == 0u) return 0u;
    if (st->list_out_len + count > st->list_out_cap) {
        uint32_t ncap = st->list_out_cap ? st->list_out_cap * 2u : 128u;
        while (ncap < st->list_out_len + count) ncap *= 2u;
        TcTerm *grown = realloc(st->list_out_slab, (size_t)ncap * sizeof(*grown));
        if (!grown) { st->fallback = true; return UINT32_MAX; }
        st->list_out_slab = grown;
        st->list_out_cap = ncap;
    }
    uint32_t off = st->list_out_len;
    st->list_out_len += count;
    return off;
}

static uint32_t tcalc_slg_push_sub_pair(TcalcSlgState *st, uint32_t prev_off,
                                        uint32_t prev_len, uint32_t sub_id,
                                        uint32_t wid, TcTerm sub_val) {
    uint32_t need = prev_len + 1u;
    if (st->sub_len + need > st->sub_cap) {
        uint32_t ncap = st->sub_cap ? st->sub_cap * 2u : 128u;
        while (ncap < st->sub_len + need) ncap *= 2u;
        TcalcSlgSubPair *grown = realloc(st->sub_slab, (size_t)ncap * sizeof(*grown));
        if (!grown) { st->fallback = true; return UINT32_MAX; }
        st->sub_slab = grown;
        st->sub_cap = ncap;
    }
    uint32_t off = st->sub_len;
    if (prev_len > 0u)
        memcpy(st->sub_slab + off, st->sub_slab + prev_off,
               (size_t)prev_len * sizeof(*st->sub_slab));
    st->sub_slab[off + prev_len] = (TcalcSlgSubPair){
        .sub_call_id = sub_id, .wid = wid, .sub_val = sub_val
    };
    st->sub_len += need;
    return off;
}

static void tcalc_slg_record_edge(TcalcSlgState *st, uint32_t caller_id,
                                  uint32_t callee_id) {
    if (st->presence_only) return;
    if (st->edge_len == st->edge_cap) {
        uint32_t ncap = st->edge_cap ? st->edge_cap * 2u : 64u;
        TcalcSlgEdge *grown = realloc(st->edges, (size_t)ncap * sizeof(*grown));
        if (!grown) { st->fallback = true; return; }
        st->edges = grown;
        st->edge_cap = ncap;
    }
    st->edges[st->edge_len++] = (TcalcSlgEdge){
        .caller_id = caller_id, .callee_id = callee_id
    };
}

static bool tcalc_slg_add_waiter(TcalcSlgState *st, uint32_t caller_id,
                                 uint32_t sub_id, uint32_t clause_idx,
                                 uint32_t branch_ord, uint32_t next_pc,
                                 TcTerm target_pat, uint32_t local_off,
                                 uint32_t sub_off, uint32_t sub_len,
                                 uint32_t list_task_id) {
    if (st->waiter_len == st->waiter_cap) {
        uint32_t ncap = st->waiter_cap ? st->waiter_cap * 2u : 64u;
        TcalcSlgWaiter *grown = realloc(st->waiters, (size_t)ncap * sizeof(*grown));
        if (!grown) { st->fallback = true; return false; }
        st->waiters = grown;
        st->waiter_cap = ncap;
    }
    uint32_t wid = st->waiter_len++;
    st->waiters[wid] = (TcalcSlgWaiter){
        .caller_id = caller_id,
        .sub_id = sub_id,
        .clause_idx = clause_idx,
        .branch_ord = branch_ord,
        .next_pc = next_pc,
        .target_pat = target_pat,
        .local_off = local_off,
        .sub_off = sub_off,
        .sub_len = sub_len,
        .list_task_id = list_task_id,
        .next_waiter = st->waiter_head[sub_id],
    };
    st->waiter_head[sub_id] = wid;
    for (uint32_t vn = st->val_head[sub_id]; vn != UINT32_MAX;
         vn = st->vals[vn].next_val) {
        if (!tcalc_slg_push_resume(st, wid, st->vals[vn].val)) return false;
    }
    return true;
}

static void tcalc_slg_emit_value(TcalcSlgState *st, uint32_t caller_id,
                                 uint32_t clause_idx, uint32_t branch_ord,
                                 TcTerm val, uint32_t sub_off, uint32_t sub_len) {
    if (val > st->max_term_seen) st->max_term_seen = val;
    if (!st->presence_only) {
        if (st->deriv_len >= TCALC_SPACE_MAX_SLG_DERIVS) {
            st->fallback = true;
            return;
        }
        if (st->deriv_len == st->deriv_cap) {
            uint32_t ncap = st->deriv_cap ? st->deriv_cap * 2u : 64u;
            TcalcSlgDeriv *grown = realloc(st->derivs, (size_t)ncap * sizeof(*grown));
            if (!grown) { st->fallback = true; return; }
            st->derivs = grown;
            st->deriv_cap = ncap;
        }
        st->derivs[st->deriv_len++] = (TcalcSlgDeriv){
            .call_id = caller_id,
            .clause_idx = clause_idx,
            .branch_ord = branch_ord,
            .sub_off = sub_off,
            .sub_len = sub_len,
            .val = val,
        };
    }
    if (tcalc_slg_seen_cv_insert(st, caller_id, val)) {
        if (st->fallback) return;
        if (st->val_len == st->val_cap) {
            uint32_t ncap = st->val_cap ? st->val_cap * 2u : 64u;
            TcalcSlgValNode *grown = realloc(st->vals, (size_t)ncap * sizeof(*grown));
            if (!grown) { st->fallback = true; return; }
            st->vals = grown;
            st->val_cap = ncap;
        }
        uint32_t vid = st->val_len++;
        st->vals[vid] = (TcalcSlgValNode){
            .val = val, .next_val = st->val_head[caller_id]
        };
        st->val_head[caller_id] = vid;
        for (uint32_t w = st->waiter_head[caller_id]; w != UINT32_MAX;
             w = st->waiters[w].next_waiter) {
            if (!tcalc_slg_push_resume(st, w, val)) return;
        }
    }
}

/* Demand the next element call for a map-atom / filter-atom / foldl-atom task. */
static void tcalc_slg_list_step(TcalcSlgState *st, uint32_t task_id) {
    TcalcSlgListTask task = st->list_tasks[task_id];
    const TcalcHoListSpec *spec = &st->entry->ho_lists[task.ho_idx];
    TcTerms *terms = st->entry->terms;
    uint32_t nargs = spec->cap_len + (spec->kind == TCHO_FOLDL ? 2u : 1u);
    TcTerm items[TCALC_SPACE_MAX_HO_CAPS + 3u];
    items[0] = spec->helper_head_tc;
    const TcTerm *saved_locals = st->local_slab + task.local_off;
    for (uint32_t i = 0u; i < spec->cap_len; i++)
        items[1u + i] = saved_locals[spec->cap_locals[i]];
    TcTerm elem = tc_item(terms, task.list_tc, task.cur_idx);
    if (spec->kind == TCHO_FOLDL) {
        items[1u + spec->cap_len] = task.acc_tc;
        items[2u + spec->cap_len] = elem;
    } else {
        items[1u + spec->cap_len] = elem;
    }
    TcTerm sub_tc = tc_expression(terms, items, 1u + nargs);
    if (!sub_tc) { st->fallback = true; return; }
    uint32_t sub_id = tcalc_slg_demand_call(st, sub_tc);
    if (st->fallback || sub_id == UINT32_MAX) return;
    tcalc_slg_record_edge(st, task.caller_id, sub_id);
    if (st->fallback) return;
    (void)tcalc_slg_add_waiter(st, task.caller_id, sub_id, task.clause_idx,
                               task.branch_ord, task.next_pc, 0u,
                               task.local_off, task.sub_off, task.sub_len,
                               task_id);
}

static inline bool tcalc_key_idx_find(const TcalcKeyIndex *idx, uint64_t key,
                                      uint32_t *head_out, uint32_t *len_out) {
    if (!idx || idx->cap == 0u) {
        *head_out = UINT32_MAX;
        *len_out = 0u;
        return false;
    }
    size_t mask = idx->cap - 1u;
    size_t slot = (size_t)tcalc_mix64(key) & mask;
    while (idx->keys[slot]) {
        if (idx->keys[slot] == key) {
            *head_out = idx->head_node[slot];
            *len_out = idx->bucket_len[slot];
            return true;
        }
        slot = (slot + 1u) & mask;
    }
    *head_out = UINT32_MAX;
    *len_out = 0u;
    return false;
}

/* Check whether key VAL can safely use an exact hash bucket on a column that
 * may hold floats (exact int/float cross-equality 1 == 1.0 needs full match). */
static inline bool tcalc_arg_key_index_safe(const TcTerms *terms,
                                            TcTerm float_sym,
                                            TcTerm val, bool col_has_float) {
    if (!val) return false;
    if (tc_kind(terms, val) == TC_INTEGER)
        return !col_has_float;
    double fv = 0.0;
    if (tcalc_term_as_float(terms, float_sym, val, &fv))
        return false;
    return true;
}

static void tcalc_slg_run_frame(TcalcSlgState *st, uint32_t caller_id,
                                uint32_t clause_idx, uint32_t branch_ord,
                                uint32_t pc, TcTerm *locals, uint32_t sub_off,
                                uint32_t sub_len, uint32_t query_depth);

/* Execute one indexed data pattern match against REL, invoking either the next
 * join leg (if JOIN != NULL and LEG_POS + 1 < JOIN->nlegs) or continuing the
 * clause frame at NEXT_PC. Uses O(1) 2-arg composite and min-cardinality 1-arg
 * hash indexes whenever argument keys are bound. */
static void tcalc_slg_exec_indexed_query(
        TcalcSlgState *st, uint32_t caller_id, uint32_t clause_idx,
        uint32_t branch_ord, uint32_t next_pc, TcTerm *locals, uint32_t sub_off,
        uint32_t sub_len, uint32_t query_depth, const TcalcJoinSpec *join,
        uint32_t leg_pos, uint32_t rel_idx, TcTerm pat_tc) {
    TcalcSpaceCacheEntry *entry = st->entry;
    const TcalcExecClause *cl = &entry->clauses[clause_idx];
    TcTerms *terms = entry->terms;
    if (rel_idx >= entry->data_dep_len) return;
    const TcalcDataRelIndex *rel = &entry->data_rels[rel_idx];
    if (rel->atom_len == 0u) return;

    uint32_t arity = tc_length(terms, pat_tc);
    if (arity < rel->min_arity || arity > rel->max_arity) return;

    uint32_t nargs = arity > 0u ? (arity - 1u) : 0u;
    uint32_t check_cols = nargs < TCALC_DATA_IDX_COLS ? nargs : TCALC_DATA_IDX_COLS;
    TcTerm bound_val[TCALC_DATA_IDX_COLS] = {0};
    bool safe_col[TCALC_DATA_IDX_COLS] = {false};
    for (uint32_t p = 0u; p < check_cols; p++) {
        TcTerm arg_pat = tc_item(terms, pat_tc, p + 1u);
        TcTerm v = tcalc_slg_try_inst_ground(terms, arg_pat, locals, 0u);
        if (v && tcalc_arg_key_index_safe(terms, entry->float_sym, v,
                                          rel->col_has_float[p])) {
            bound_val[p] = v;
            safe_col[p] = true;
        }
    }

    const TcalcKeyIndex *use_idx = NULL;
    uint32_t chain_head = UINT32_MAX;
    uint32_t best_len = UINT32_MAX;

    /* 1. Try 2-argument composite index (arg1, arg2) when both are bound. */
    if (nargs >= 2u && safe_col[0] && safe_col[1] && !rel->pair12_has_float &&
        rel->pair12_idx.cap > 0u) {
        uint64_t pk = tcalc_pair_key(arity, bound_val[0], bound_val[1]);
        uint32_t h = UINT32_MAX, blen = 0u;
        if (!tcalc_key_idx_find(&rel->pair12_idx, pk, &h, &blen))
            return; /* definitive O(1) empty miss! */
        use_idx = &rel->pair12_idx;
        chain_head = h;
        best_len = blen;
    }

    /* 2. Check single-argument indexes and pick the smallest posting chain. */
    for (uint32_t p = 0u; p < check_cols; p++) {
        if (!safe_col[p] || rel->arg_idx[p].cap == 0u) continue;
        uint64_t ak = tcalc_col_key(arity, bound_val[p]);
        uint32_t h = UINT32_MAX, blen = 0u;
        if (!tcalc_key_idx_find(&rel->arg_idx[p], ak, &h, &blen))
            return; /* definitive O(1) empty miss! */
        if (blen < best_len) {
            best_len = blen;
            chain_head = h;
            use_idx = &rel->arg_idx[p];
        }
    }

    TcTerm trial_small[32];
    TcTerm *trial = cl->nlocals <= 32u
        ? trial_small : malloc((size_t)cl->nlocals * sizeof(*trial));
    if (!trial) { st->fallback = true; return; }

    bool more_legs = join && (leg_pos + 1u < join->nlegs);
    uint32_t next_leg_idx = more_legs ? join->order[leg_pos + 1u] : 0u;

    if (use_idx) {
        for (uint32_t occ = chain_head; occ != UINT32_MAX && !st->fallback;
             occ = use_idx->next_occ[occ]) {
            TcTerm atom_tc = rel->atoms[occ];
            if (tc_length(terms, atom_tc) != arity) continue;
            memcpy(trial, locals, (size_t)cl->nlocals * sizeof(*trial));
            if (tcalc_slg_match(terms, entry->float_sym, pat_tc, atom_tc,
                                trial, 0u, &st->fallback) && !st->fallback) {
                uint32_t bo = (sub_len == 0u) ? ++st->branch_seq : branch_ord;
                if (more_legs) {
                    const TcalcJoinLeg *nl = &join->legs[next_leg_idx];
                    tcalc_slg_exec_indexed_query(
                        st, caller_id, clause_idx, bo, next_pc, trial,
                        sub_off, sub_len, query_depth + 1u,
                        join, leg_pos + 1u, nl->rel_idx, nl->pat_tc);
                } else {
                    tcalc_slg_run_frame(st, caller_id, clause_idx, bo, next_pc,
                                        trial, sub_off, sub_len, query_depth + 1u);
                }
            }
        }
    } else {
        for (uint32_t occ = 0u; occ < rel->atom_len && !st->fallback; occ++) {
            TcTerm atom_tc = rel->atoms[occ];
            if (tc_length(terms, atom_tc) != arity) continue;
            memcpy(trial, locals, (size_t)cl->nlocals * sizeof(*trial));
            if (tcalc_slg_match(terms, entry->float_sym, pat_tc, atom_tc,
                                trial, 0u, &st->fallback) && !st->fallback) {
                uint32_t bo = (sub_len == 0u) ? ++st->branch_seq : branch_ord;
                if (more_legs) {
                    const TcalcJoinLeg *nl = &join->legs[next_leg_idx];
                    tcalc_slg_exec_indexed_query(
                        st, caller_id, clause_idx, bo, next_pc, trial,
                        sub_off, sub_len, query_depth + 1u,
                        join, leg_pos + 1u, nl->rel_idx, nl->pat_tc);
                } else {
                    tcalc_slg_run_frame(st, caller_id, clause_idx, bo, next_pc,
                                        trial, sub_off, sub_len, query_depth + 1u);
                }
            }
        }
    }

    if (trial != trial_small) free(trial);
}

/* Advance a clause frame from PC until it returns, fails, empties, suspends on
 * a CALL/HO_LIST waiter, or forks on a QUERY/JOIN/FORK/SUPERPOSE. */
static void tcalc_slg_run_frame(TcalcSlgState *st, uint32_t caller_id,
                                uint32_t clause_idx, uint32_t branch_ord,
                                uint32_t pc, TcTerm *locals, uint32_t sub_off,
                                uint32_t sub_len, uint32_t query_depth) {
    TcalcSpaceCacheEntry *entry = st->entry;
    const TcalcExecClause *cl = &entry->clauses[clause_idx];
    TcTerms *terms = entry->terms;

    while (pc < cl->op_len && !st->fallback) {
        if (++st->work > TCALC_SPACE_BUDGET) {
            st->fallback = true;
            return;
        }
        const TcalcExecOp *op = &cl->ops[pc];
        switch (op->kind) {
        case TCEXEC_EMPTY:
            return;
        case TCEXEC_JUMP:
            pc = op->jump_pc;
            break;
        case TCEXEC_MOV: {
            TcTerm v = tcalc_slg_inst(terms, op->primary, locals, 0u, &st->fallback);
            if (st->fallback) return;
            locals[tc_index(terms, op->target)] = v;
            pc++;
            break;
        }
        case TCEXEC_MATCH: {
            TcTerm v = tcalc_slg_inst(terms, op->primary, locals, 0u, &st->fallback);
            if (st->fallback) return;
            TcTerm trial_small[32];
            TcTerm *trial = cl->nlocals <= 32u
                ? trial_small : malloc((size_t)cl->nlocals * sizeof(*trial));
            if (!trial) { st->fallback = true; return; }
            memcpy(trial, locals, (size_t)cl->nlocals * sizeof(*trial));
            bool ok = tcalc_slg_match(terms, entry->float_sym, op->target, v,
                                      trial, 0u, &st->fallback);
            if (!ok || st->fallback) {
                if (trial != trial_small) free(trial);
                return;
            }
            memcpy(locals, trial, (size_t)cl->nlocals * sizeof(*trial));
            if (trial != trial_small) free(trial);
            pc++;
            break;
        }
        case TCEXEC_TRY_MATCH: {
            TcTerm v = tcalc_slg_inst(terms, op->primary, locals, 0u, &st->fallback);
            if (st->fallback) return;
            TcTerm trial_small[32];
            TcTerm *trial = cl->nlocals <= 32u
                ? trial_small : malloc((size_t)cl->nlocals * sizeof(*trial));
            if (!trial) { st->fallback = true; return; }
            memcpy(trial, locals, (size_t)cl->nlocals * sizeof(*trial));
            bool ok = tcalc_slg_match(terms, entry->float_sym, op->target, v,
                                      trial, 0u, &st->fallback);
            if (st->fallback) {
                if (trial != trial_small) free(trial);
                return;
            }
            if (ok) {
                memcpy(locals, trial, (size_t)cl->nlocals * sizeof(*trial));
                pc++;
            } else {
                pc = op->jump_pc;
            }
            if (trial != trial_small) free(trial);
            break;
        }
        case TCEXEC_PRIM: {
            TcTerm a = tcalc_slg_inst(terms, op->primary, locals, 0u, &st->fallback);
            TcTerm b = (!st->fallback && op->secondary)
                ? tcalc_slg_inst(terms, op->secondary, locals, 0u, &st->fallback)
                : 0u;
            if (st->fallback) return;
            TcTerm res = tcalc_slg_eval_prim(entry, st->language_id, op->prim,
                                             a, b, &st->fallback);
            if (st->fallback || !res) { st->fallback = true; return; }
            if (res > st->max_term_seen) st->max_term_seen = res;
            locals[tc_index(terms, op->target)] = res;
            pc++;
            break;
        }
        case TCEXEC_IF: {
            TcTerm cv = tcalc_slg_inst(terms, op->primary, locals, 0u, &st->fallback);
            if (st->fallback) return;
            bool truth = false;
            if (tcalc_term_as_bool(terms, entry, st->language_id, cv, &truth)) {
                pc = truth ? (pc + 1u) : op->jump_pc;
            } else if (st->language_id == CETTA_LANGUAGE_PETTA) {
                pc = op->jump_pc;
            } else {
                /* HE keeps `(if non-bool then else)` unreduced: fall back. */
                st->fallback = true;
                return;
            }
            break;
        }
        case TCEXEC_FORK: {
            if (query_depth > 48u) { st->fallback = true; return; }
            TcTerm trial_small[32];
            TcTerm *trial = cl->nlocals <= 32u
                ? trial_small : malloc((size_t)cl->nlocals * sizeof(*trial));
            if (!trial) { st->fallback = true; return; }
            uint32_t bo1 = (sub_len == 0u) ? ++st->branch_seq : branch_ord;
            memcpy(trial, locals, (size_t)cl->nlocals * sizeof(*trial));
            tcalc_slg_run_frame(st, caller_id, clause_idx, bo1, pc + 1u,
                                trial, sub_off, sub_len, query_depth + 1u);
            if (!st->fallback) {
                uint32_t bo2 = (sub_len == 0u) ? ++st->branch_seq : branch_ord;
                memcpy(trial, locals, (size_t)cl->nlocals * sizeof(*trial));
                tcalc_slg_run_frame(st, caller_id, clause_idx, bo2, op->jump_pc,
                                    trial, sub_off, sub_len, query_depth + 1u);
            }
            if (trial != trial_small) free(trial);
            return;
        }
        case TCEXEC_SUPERPOSE: {
            if (query_depth > 48u) { st->fallback = true; return; }
            TcTerm seq = tcalc_slg_inst(terms, op->primary, locals, 0u, &st->fallback);
            if (st->fallback) return;
            if (!tcalc_term_is_real_expr(terms, entry, seq)) {
                st->fallback = true;
                return;
            }
            uint32_t n = tc_length(terms, seq);
            if (n == 0u) return;
            TcTerm trial_small[32];
            TcTerm *trial = cl->nlocals <= 32u
                ? trial_small : malloc((size_t)cl->nlocals * sizeof(*trial));
            if (!trial) { st->fallback = true; return; }
            uint32_t dst_idx = tc_index(terms, op->target);
            for (uint32_t i = 0u; i < n && !st->fallback; i++) {
                uint32_t bo = (sub_len == 0u) ? ++st->branch_seq : branch_ord;
                memcpy(trial, locals, (size_t)cl->nlocals * sizeof(*trial));
                trial[dst_idx] = tc_item(terms, seq, i);
                tcalc_slg_run_frame(st, caller_id, clause_idx, bo, pc + 1u,
                                    trial, sub_off, sub_len, query_depth + 1u);
            }
            if (trial != trial_small) free(trial);
            return;
        }
        case TCEXEC_QUERY: {
            if (query_depth > 48u) { st->fallback = true; return; }
            tcalc_slg_exec_indexed_query(
                st, caller_id, clause_idx, branch_ord, pc + 1u, locals,
                sub_off, sub_len, query_depth, NULL, 0u,
                op->jump_pc, op->primary);
            return;
        }
        case TCEXEC_JOIN: {
            if (query_depth > 48u) { st->fallback = true; return; }
            if (op->jump_pc >= entry->join_len) { st->fallback = true; return; }
            const TcalcJoinSpec *join = &entry->joins[op->jump_pc];
            if (join->nlegs == 0u) { pc++; break; }
            const TcalcJoinLeg *first = &join->legs[join->order[0]];
            tcalc_slg_exec_indexed_query(
                st, caller_id, clause_idx, branch_ord, pc + 1u, locals,
                sub_off, sub_len, query_depth, join, 0u,
                first->rel_idx, first->pat_tc);
            return;
        }
        case TCEXEC_HO_CALL: {
            TcTerm sub_tc = tcalc_slg_inst(terms, op->primary, locals, 0u, &st->fallback);
            if (st->fallback) return;
            if (!tcalc_term_is_real_expr(terms, entry, sub_tc) ||
                tc_length(terms, sub_tc) == 0u) {
                st->fallback = true;
                return;
            }
            TcTerm fn_tc = tc_item(terms, sub_tc, 0u);
            if (tc_kind(terms, fn_tc) != TC_SYMBOL) {
                st->fallback = true;
                return;
            }
            /* 1. Compiled defined head -> dispatch as tabled SLG call */
            bool is_compiled = false;
            for (uint32_t hi = 0u; hi < entry->head_count; hi++) {
                if (entry->head_syms[hi] == fn_tc) { is_compiled = true; break; }
            }
            if (is_compiled) {
                uint32_t sub_id = tcalc_slg_demand_call(st, sub_tc);
                if (st->fallback || sub_id == UINT32_MAX) return;
                tcalc_slg_record_edge(st, caller_id, sub_id);
                if (st->fallback) return;
                uint32_t loff = tcalc_slg_alloc_locals(st, locals, cl->nlocals);
                if (st->fallback) return;
                (void)tcalc_slg_add_waiter(st, caller_id, sub_id, clause_idx,
                                           branch_ord, pc + 1u, op->target, loff,
                                           sub_off, sub_len, UINT32_MAX);
                return;
            }
            /* 2. Known compiled primitive operation (e.g. +, *, ==, car-atom) */
            size_t fn_len = 0u;
            const char *fn_bytes = tc_bytes(terms, fn_tc, &fn_len);
            if (!fn_bytes) { st->fallback = true; return; }
            SymbolId fn_sym = symbol_intern_bytes(
                g_symbols, (const uint8_t *)fn_bytes, (uint32_t)fn_len);
            uint32_t nargs = tc_length(terms, sub_tc) - 1u;
            TcalcPrimOp pop = tcalc_prim_op_of(fn_sym, nargs, st->language_id);
            if (pop != TCPRIM_NONE &&
                !space_equations_may_match_known_head(entry->space, fn_sym)) {
                TcTerm a0 = nargs >= 1u ? tc_item(terms, sub_tc, 1u) : 0u;
                TcTerm a1 = nargs >= 2u ? tc_item(terms, sub_tc, 2u) : 0u;
                TcTerm res = tcalc_slg_eval_prim(entry, st->language_id, pop,
                                                 a0, a1, &st->fallback);
                if (st->fallback || !res) { st->fallback = true; return; }
                if (res > st->max_term_seen) st->max_term_seen = res;
                locals[tc_index(terms, op->target)] = res;
                pc++;
                break;
            }
            /* 3. Inert constructor symbol with no definitions & not reserved */
            if (!tcalc_space_head_is_reserved_op(fn_sym, st->language_id) &&
                !space_equations_may_match_known_head(entry->space, fn_sym)) {
                locals[tc_index(terms, op->target)] = sub_tc;
                pc++;
                break;
            }
            st->fallback = true;
            return;
        }
        case TCEXEC_HO_LIST: {
            if (op->jump_pc >= entry->ho_list_len) { st->fallback = true; return; }
            const TcalcHoListSpec *spec = &entry->ho_lists[op->jump_pc];
            TcTerm list_tc = tcalc_slg_inst(terms, op->primary, locals, 0u, &st->fallback);
            TcTerm init_tc = (!st->fallback && spec->kind == TCHO_FOLDL)
                ? tcalc_slg_inst(terms, op->secondary, locals, 0u, &st->fallback)
                : 0u;
            if (st->fallback) return;
            if (!tcalc_term_is_real_expr(terms, entry, list_tc)) {
                st->fallback = true;
                return;
            }
            uint32_t list_len = tc_length(terms, list_tc);
            uint32_t target_local = tc_index(terms, op->target);
            if (list_len == 0u) {
                locals[target_local] = (spec->kind == TCHO_FOLDL) ? init_tc : list_tc;
                pc++;
                break;
            }
            uint32_t loff = tcalc_slg_alloc_locals(st, locals, cl->nlocals);
            if (st->fallback) return;
            uint32_t ooff = (spec->kind == TCHO_FOLDL)
                ? 0u : tcalc_slg_alloc_list_out(st, list_len);
            if (st->fallback) return;
            if (st->list_task_len == st->list_task_cap) {
                uint32_t ncap = st->list_task_cap ? st->list_task_cap * 2u : 32u;
                TcalcSlgListTask *grown = realloc(st->list_tasks,
                                                  (size_t)ncap * sizeof(*grown));
                if (!grown) { st->fallback = true; return; }
                st->list_tasks = grown;
                st->list_task_cap = ncap;
            }
            uint32_t tid = st->list_task_len++;
            st->list_tasks[tid] = (TcalcSlgListTask){
                .caller_id = caller_id,
                .clause_idx = clause_idx,
                .branch_ord = branch_ord,
                .next_pc = pc + 1u,
                .target_local = target_local,
                .ho_idx = op->jump_pc,
                .list_tc = list_tc,
                .list_len = list_len,
                .cur_idx = 0u,
                .acc_tc = init_tc,
                .out_off = ooff,
                .out_len = 0u,
                .local_off = loff,
                .sub_off = sub_off,
                .sub_len = sub_len,
                .branched = false,
            };
            tcalc_slg_list_step(st, tid);
            return;
        }
        case TCEXEC_CALL: {
            TcTerm sub_tc = tcalc_slg_inst(terms, op->primary, locals, 0u, &st->fallback);
            if (st->fallback) return;
            uint32_t sub_id = tcalc_slg_demand_call(st, sub_tc);
            if (st->fallback || sub_id == UINT32_MAX) return;
            tcalc_slg_record_edge(st, caller_id, sub_id);
            if (st->fallback) return;
            uint32_t loff = tcalc_slg_alloc_locals(st, locals, cl->nlocals);
            if (st->fallback) return;
            (void)tcalc_slg_add_waiter(st, caller_id, sub_id, clause_idx,
                                       branch_ord, pc + 1u, op->target, loff,
                                       sub_off, sub_len, UINT32_MAX);
            return;
        }
        case TCEXEC_RETURN: {
            TcTerm val = tcalc_slg_inst(terms, op->primary, locals, 0u, &st->fallback);
            if (st->fallback) return;
            tcalc_slg_emit_value(st, caller_id, clause_idx, branch_ord, val,
                                 sub_off, sub_len);
            return;
        }
        }
    }
}

static inline bool tcalc_he_deriv_less(const TcalcSlgState *st,
                                       const TcalcMultMap *rank_map,
                                       uint32_t di_a, uint32_t di_b) {
    const TcalcSlgDeriv *da = &st->derivs[di_a];
    const TcalcSlgDeriv *db = &st->derivs[di_b];
    if (da->clause_idx != db->clause_idx)
        return da->clause_idx < db->clause_idx;
    if (da->branch_ord != db->branch_ord)
        return da->branch_ord < db->branch_ord;
    uint32_t min_sub = da->sub_len < db->sub_len ? da->sub_len : db->sub_len;
    for (uint32_t si = 0u; si < min_sub; si++) {
        TcalcSlgSubPair sa = st->sub_slab[da->sub_off + si];
        TcalcSlgSubPair sb = st->sub_slab[db->sub_off + si];
        if (sa.wid != sb.wid)
            return sa.wid < sb.wid;
        if (sa.sub_val != sb.sub_val) {
            uint64_t ra = tcalc_mult_map_get(rank_map, sa.sub_call_id + 1u, sa.sub_val);
            uint64_t rb = tcalc_mult_map_get(rank_map, sb.sub_call_id + 1u, sb.sub_val);
            if (ra != rb) return ra < rb;
        }
    }
    return di_a < di_b;
}

static void tcalc_he_heapsort_derivs(const TcalcSlgState *st,
                                     const TcalcMultMap *rank_map,
                                     uint32_t *arr, uint32_t n) {
    if (n <= 1u) return;
    for (uint32_t start = n / 2u; start > 0u; start--) {
        uint32_t root = start - 1u;
        for (;;) {
            uint32_t child = root * 2u + 1u;
            if (child >= n) break;
            if (child + 1u < n &&
                tcalc_he_deriv_less(st, rank_map, arr[child], arr[child + 1u]))
                child++;
            if (tcalc_he_deriv_less(st, rank_map, arr[root], arr[child])) {
                uint32_t tmp = arr[root];
                arr[root] = arr[child];
                arr[child] = tmp;
                root = child;
            } else {
                break;
            }
        }
    }
    for (uint32_t end = n; end > 1u; end--) {
        uint32_t tmp = arr[end - 1u];
        arr[end - 1u] = arr[0];
        arr[0] = tmp;
        uint32_t limit = end - 1u;
        uint32_t root = 0u;
        for (;;) {
            uint32_t child = root * 2u + 1u;
            if (child >= limit) break;
            if (child + 1u < limit &&
                tcalc_he_deriv_less(st, rank_map, arr[child], arr[child + 1u]))
                child++;
            if (tcalc_he_deriv_less(st, rank_map, arr[root], arr[child])) {
                uint32_t sw = arr[root];
                arr[root] = arr[child];
                arr[child] = sw;
                root = child;
            } else {
                break;
            }
        }
    }
}

typedef struct {
    const TcalcSlgState *st;
    const TcalcMultMap *rank_map;
    const TcalcMultMap *mult_map;
    const uint32_t *deriv_list;
    const uint32_t *seq_off;
    const uint32_t *seq_len;
    const TcTerm *seq_buf;
    const uint8_t *non_adj_dup;
    TcTerm *out_buf;
    uint32_t out_pos;
    uint32_t out_cap;
    bool overflow;
} TcalcHeSeqEmitter;

static void tcalc_he_emit_slice(TcalcHeSeqEmitter *em, uint32_t lo, uint32_t hi,
                                uint32_t depth) {
    if (em->overflow || lo >= hi) return;
    const TcalcSlgState *st = em->st;
    uint32_t i = lo;
    while (i < hi && !em->overflow) {
        const TcalcSlgDeriv *d0 = &st->derivs[em->deriv_list[i]];
        if (d0->sub_len == depth) {
            if (em->out_pos >= em->out_cap) { em->overflow = true; return; }
            em->out_buf[em->out_pos++] = d0->val;
            i++;
            continue;
        }
        /* All derivations in [i, j) share (clause_idx, branch_ord, wid@depth). */
        TcalcSlgSubPair sp0 = st->sub_slab[d0->sub_off + depth];
        uint32_t c = sp0.sub_call_id;
        uint32_t j = i + 1u;
        while (j < hi) {
            const TcalcSlgDeriv *dj = &st->derivs[em->deriv_list[j]];
            if (dj->clause_idx != d0->clause_idx ||
                dj->branch_ord != d0->branch_ord ||
                dj->sub_len <= depth)
                break;
            TcalcSlgSubPair spj = st->sub_slab[dj->sub_off + depth];
            if (spj.wid != sp0.wid || spj.sub_call_id != c)
                break;
            j++;
        }
        if (!em->non_adj_dup[c]) {
            /* Callee's duplicates are contiguous: iterate sorted distinct-value runs. */
            uint32_t k = i;
            while (k < j && !em->overflow) {
                const TcalcSlgDeriv *dk = &st->derivs[em->deriv_list[k]];
                TcTerm val = st->sub_slab[dk->sub_off + depth].sub_val;
                uint32_t k2 = k + 1u;
                while (k2 < j) {
                    const TcalcSlgDeriv *dk2 = &st->derivs[em->deriv_list[k2]];
                    if (st->sub_slab[dk2->sub_off + depth].sub_val != val)
                        break;
                    k2++;
                }
                uint64_t m = tcalc_mult_map_get(em->mult_map, c + 1u, val);
                for (uint64_t rep = 0u; rep < m && !em->overflow; rep++)
                    tcalc_he_emit_slice(em, k, k2, depth + 1u);
                k = k2;
            }
        } else {
            /* Callee has non-adjacent duplicates: walk callee's exact ordered
             * sequence seq[c] and binary-search its matching value run in [i, j)
             * ordered by rank_map(c + 1, sub_val). */
            uint32_t coff = em->seq_off[c];
            uint32_t clen = em->seq_len[c];
            for (uint32_t si = 0u; si < clen && !em->overflow; si++) {
                TcTerm want_val = em->seq_buf[coff + si];
                uint64_t want_rank = tcalc_mult_map_get(em->rank_map, c + 1u, want_val);
                if (want_rank == 0u) continue;
                uint32_t bl = i, br = j;
                while (bl < br) {
                    uint32_t mid = bl + (br - bl) / 2u;
                    const TcalcSlgDeriv *dm = &st->derivs[em->deriv_list[mid]];
                    TcTerm mv = st->sub_slab[dm->sub_off + depth].sub_val;
                    uint64_t mr = tcalc_mult_map_get(em->rank_map, c + 1u, mv);
                    if (mr < want_rank) bl = mid + 1u;
                    else br = mid;
                }
                if (bl >= j) continue;
                const TcalcSlgDeriv *db = &st->derivs[em->deriv_list[bl]];
                if (st->sub_slab[db->sub_off + depth].sub_val != want_val) continue;
                uint32_t bend = bl + 1u;
                while (bend < j) {
                    const TcalcSlgDeriv *de = &st->derivs[em->deriv_list[bend]];
                    if (st->sub_slab[de->sub_off + depth].sub_val != want_val) break;
                    bend++;
                }
                tcalc_he_emit_slice(em, bl, bend, depth + 1u);
            }
        }
        i = j;
    }
}

static bool tcalc_space_exec_slg(TcalcSpaceCacheEntry *entry, TcTerm call_tc,
                                 Atom *call_atom, CettaLanguageId language_id,
                                 bool presence_only, Arena *a,
                                 CettaCallOutcome *out) {
    TcalcSlgState st;
    memset(&st, 0, sizeof(st));
    st.entry = entry;
    st.language_id = language_id;
    st.presence_only = presence_only;

    uint32_t root_id = tcalc_slg_demand_call(&st, call_tc);
    if (st.fallback || root_id == UINT32_MAX) {
        tcalc_slg_free(&st);
        return false;
    }

    TcTerms *terms = entry->terms;
    while (!st.fallback &&
           (st.demand_head < st.demand_len || st.resume_head < st.resume_len)) {
        while (!st.fallback && st.demand_head < st.demand_len) {
            uint32_t cid = st.demand_q[st.demand_head++];
            TcTerm c_tc = st.call_terms[cid];
            if (tc_kind(terms, c_tc) != TC_EXPRESSION || tc_length(terms, c_tc) == 0u)
                continue;
            TcTerm hd = tc_item(terms, c_tc, 0u);
            uint32_t hidx = UINT32_MAX;
            for (uint32_t i = 0u; i < entry->head_count; i++) {
                if (entry->head_syms[i] == hd) { hidx = i; break; }
            }
            if (hidx == UINT32_MAX) continue;
            uint32_t lo = entry->head_off[hidx];
            uint32_t hi = entry->head_off[hidx + 1u];
            uint32_t call_arity = tc_length(terms, c_tc);
            bool any_same_arity = false;
            for (uint32_t k = lo; k < hi && !st.fallback; k++) {
                uint32_t cidx = entry->head_clause_idx[k];
                const TcalcExecClause *cl = &entry->clauses[cidx];
                if (tc_length(terms, cl->lhs_tc) == call_arity)
                    any_same_arity = true;
                TcTerm locals_small[32];
                TcTerm *locals = cl->nlocals <= 32u
                    ? locals_small : malloc((size_t)cl->nlocals * sizeof(*locals));
                if (!locals) { st.fallback = true; break; }
                memset(locals, 0, (size_t)cl->nlocals * sizeof(*locals));
                if (tcalc_slg_match(terms, entry->float_sym, cl->lhs_tc, c_tc,
                                    locals, 0u, &st.fallback) && !st.fallback) {
                    st.call_matched[cid] = 1u;
                    tcalc_slg_run_frame(&st, cid, cidx, 0u, 0u, locals, 0u, 0u, 0u);
                }
                if (locals != locals_small) free(locals);
            }
            if (language_id == CETTA_LANGUAGE_PETTA && !any_same_arity) {
                /* Under-application produces a partial value and over-application
                 * raises an arity error in PeTTa: defer to the evaluator. */
                st.fallback = true;
                break;
            }
        }
        while (!st.fallback && st.resume_head < st.resume_len) {
            TcalcSlgResume task = st.resume_q[st.resume_head++];
            TcalcSlgWaiter w = st.waiters[task.waiter_idx];
            const TcalcExecClause *cl = &entry->clauses[w.clause_idx];

            if (w.list_task_id != UINT32_MAX) {
                TcalcSlgListTask lt = st.list_tasks[w.list_task_id];
                const TcalcHoListSpec *spec = &entry->ho_lists[lt.ho_idx];
                bool reuse_buf = !st.list_tasks[w.list_task_id].branched;
                st.list_tasks[w.list_task_id].branched = true;

                uint32_t ooff = lt.out_off;
                uint32_t olen = lt.out_len;
                TcTerm next_acc = lt.acc_tc;

                if (spec->kind == TCHO_MAP || spec->kind == TCHO_FILTER) {
                    if (!reuse_buf) {
                        ooff = tcalc_slg_alloc_list_out(&st, lt.list_len);
                        if (st.fallback) break;
                        if (olen > 0u)
                            memcpy(st.list_out_slab + ooff,
                                   st.list_out_slab + lt.out_off,
                                   (size_t)olen * sizeof(TcTerm));
                    }
                    if (spec->kind == TCHO_MAP) {
                        st.list_out_slab[ooff + olen] = task.val;
                        olen++;
                    } else {
                        bool keep = false;
                        if (!tcalc_term_as_bool(terms, entry, language_id,
                                                task.val, &keep)) {
                            if (language_id == CETTA_LANGUAGE_PETTA) {
                                keep = false;
                            } else {
                                st.fallback = true;
                                break;
                            }
                        }
                        if (keep) {
                            st.list_out_slab[ooff + olen] =
                                tc_item(terms, lt.list_tc, lt.cur_idx);
                            olen++;
                        }
                    }
                } else {
                    next_acc = task.val;
                }

                uint32_t soff = lt.sub_off;
                uint32_t slen = lt.sub_len;
                if (!presence_only) {
                    soff = tcalc_slg_push_sub_pair(&st, lt.sub_off, lt.sub_len,
                                                   w.sub_id, task.waiter_idx,
                                                   task.val);
                    if (st.fallback) break;
                    slen = lt.sub_len + 1u;
                }
                uint32_t next_idx = lt.cur_idx + 1u;
                if (next_idx < lt.list_len) {
                    if (st.list_task_len == st.list_task_cap) {
                        uint32_t ncap = st.list_task_cap ? st.list_task_cap * 2u : 32u;
                        TcalcSlgListTask *grown = realloc(
                            st.list_tasks, (size_t)ncap * sizeof(*grown));
                        if (!grown) { st.fallback = true; break; }
                        st.list_tasks = grown;
                        st.list_task_cap = ncap;
                    }
                    uint32_t ntid = st.list_task_len++;
                    st.list_tasks[ntid] = (TcalcSlgListTask){
                        .caller_id = lt.caller_id,
                        .clause_idx = lt.clause_idx,
                        .branch_ord = lt.branch_ord,
                        .next_pc = lt.next_pc,
                        .target_local = lt.target_local,
                        .ho_idx = lt.ho_idx,
                        .list_tc = lt.list_tc,
                        .list_len = lt.list_len,
                        .cur_idx = next_idx,
                        .acc_tc = next_acc,
                        .out_off = ooff,
                        .out_len = olen,
                        .local_off = lt.local_off,
                        .sub_off = soff,
                        .sub_len = slen,
                        .branched = false,
                    };
                    tcalc_slg_list_step(&st, ntid);
                } else {
                    TcTerm final_tc = (spec->kind == TCHO_FOLDL)
                        ? next_acc
                        : tc_expression(terms,
                                        olen ? (st.list_out_slab + ooff) : NULL,
                                        olen);
                    if (!final_tc) { st.fallback = true; break; }
                    if (final_tc > st.max_term_seen) st.max_term_seen = final_tc;
                    TcTerm locals_small[32];
                    TcTerm *locals = cl->nlocals <= 32u
                        ? locals_small : malloc((size_t)cl->nlocals * sizeof(*locals));
                    if (!locals) { st.fallback = true; break; }
                    memcpy(locals, st.local_slab + lt.local_off,
                           (size_t)cl->nlocals * sizeof(*locals));
                    locals[lt.target_local] = final_tc;
                    tcalc_slg_run_frame(&st, lt.caller_id, lt.clause_idx,
                                        lt.branch_ord, lt.next_pc, locals,
                                        soff, slen, 0u);
                    if (locals != locals_small) free(locals);
                }
                continue;
            }

            TcTerm locals_small[32];
            TcTerm *locals = cl->nlocals <= 32u
                ? locals_small : malloc((size_t)cl->nlocals * sizeof(*locals));
            if (!locals) { st.fallback = true; break; }
            memcpy(locals, st.local_slab + w.local_off,
                   (size_t)cl->nlocals * sizeof(*locals));
            if (!tcalc_slg_match(terms, entry->float_sym, w.target_pat,
                                 task.val, locals, 0u, &st.fallback) || st.fallback) {
                if (locals != locals_small) free(locals);
                continue;
            }
            uint32_t soff = w.sub_off;
            uint32_t slen = w.sub_len;
            if (!presence_only) {
                soff = tcalc_slg_push_sub_pair(&st, w.sub_off, w.sub_len,
                                               w.sub_id, task.waiter_idx,
                                               task.val);
                if (st.fallback) {
                    if (locals != locals_small) free(locals);
                    break;
                }
                slen = w.sub_len + 1u;
            }
            tcalc_slg_run_frame(&st, w.caller_id, w.clause_idx, w.branch_ord,
                                w.next_pc, locals, soff, slen, 0u);
            if (locals != locals_small) free(locals);
        }
    }

    if (st.max_term_seen > 1048576u)
        entry->terms_need_reset = true;

    bool ok = false;
    if (st.fallback) goto cleanup;

    /* Dialect unmatched-clause law */
    if (!st.call_matched[root_id]) {
        *out = language_id == CETTA_LANGUAGE_HE
            ? cetta_call_completed_value(call_atom)
            : cetta_call_failure();
        ok = true;
        goto cleanup;
    }
    if (language_id == CETTA_LANGUAGE_HE) {
        for (uint32_t cid = 0u; cid < st.call_len; cid++) {
            if (cid != root_id && !st.call_matched[cid])
                goto cleanup;
        }
    }

    if (presence_only) {
        size_t nvals = 0u;
        for (uint32_t vn = st.val_head[root_id]; vn != UINT32_MAX;
             vn = st.vals[vn].next_val)
            nvals++;
        if (nvals > TCALC_SPACE_MAX_ANSWERS) goto cleanup;
        TcTerm *vals = nvals ? malloc(nvals * sizeof(*vals)) : NULL;
        uint64_t *counts = nvals ? malloc(nvals * sizeof(*counts)) : NULL;
        if (nvals && (!vals || !counts)) { free(vals); free(counts); goto cleanup; }
        size_t vi = 0u;
        for (uint32_t vn = st.val_head[root_id]; vn != UINT32_MAX;
             vn = st.vals[vn].next_val) {
            vals[vi] = st.vals[vn].val;
            counts[vi] = 1u;
            vi++;
        }
        ok = tcalc_emit_answers_outcome(entry, language_id, a, vals, counts,
                                        vi, (uint64_t)vi, out);
        free(vals);
        free(counts);
        goto cleanup;
    }

    /* Multiset mode: iterative 3-color DFS on dense call IDs + O(V+E) DAG semiring */
    {
        uint32_t ncalls = st.call_len;
        uint32_t *adj_off = calloc((size_t)ncalls + 1u, sizeof(*adj_off));
        uint32_t *adj_cur = calloc(ncalls, sizeof(*adj_cur));
        uint32_t *adj_to = st.edge_len ? malloc((size_t)st.edge_len * sizeof(*adj_to)) : NULL;
        uint8_t *color = calloc(ncalls, sizeof(*color));
        uint32_t *dfs_node = malloc((size_t)ncalls * sizeof(*dfs_node));
        uint32_t *dfs_edge = malloc((size_t)ncalls * sizeof(*dfs_edge));
        uint32_t *topo = malloc((size_t)ncalls * sizeof(*topo));
        uint32_t *deriv_off = calloc((size_t)ncalls + 1u, sizeof(*deriv_off));
        uint32_t *deriv_cur = calloc(ncalls, sizeof(*deriv_cur));
        uint32_t *deriv_list = st.deriv_len ? malloc((size_t)st.deriv_len * sizeof(*deriv_list)) : NULL;
        TcalcMultMap mult_map = {0};
        TcTerm *root_vals = NULL;
        uint64_t *root_counts = NULL;

        if (!adj_off || !adj_cur || (st.edge_len && !adj_to) || !color ||
            !dfs_node || !dfs_edge || !topo || !deriv_off || !deriv_cur ||
            (st.deriv_len && !deriv_list) ||
            !tcalc_mult_map_init(&mult_map, (size_t)st.deriv_len + 1u)) {
            goto ms_done;
        }
        for (uint32_t i = 0u; i < st.edge_len; i++)
            adj_off[st.edges[i].caller_id + 1u]++;
        for (uint32_t u = 0u; u < ncalls; u++) {
            adj_off[u + 1u] += adj_off[u];
            adj_cur[u] = adj_off[u];
        }
        for (uint32_t i = 0u; i < st.edge_len; i++)
            adj_to[adj_cur[st.edges[i].caller_id]++] = st.edges[i].callee_id;

        size_t sp = 0u, topo_len = 0u;
        color[root_id] = 1u;
        dfs_node[sp] = root_id;
        dfs_edge[sp] = adj_off[root_id];
        sp++;
        while (sp > 0u) {
            uint32_t u = dfs_node[sp - 1u];
            uint32_t ec = dfs_edge[sp - 1u];
            if (ec < adj_off[u + 1u]) {
                dfs_edge[sp - 1u] = ec + 1u;
                uint32_t v = adj_to[ec];
                if (color[v] == 1u) goto ms_done; /* cycle -> interpreter */
                if (color[v] == 0u) {
                    color[v] = 1u;
                    dfs_node[sp] = v;
                    dfs_edge[sp] = adj_off[v];
                    sp++;
                }
            } else {
                color[u] = 2u;
                topo[topo_len++] = u;
                sp--;
            }
        }

        for (uint32_t i = 0u; i < st.deriv_len; i++) {
            uint32_t u = st.derivs[i].call_id;
            if (color[u] == 2u) deriv_off[u + 1u]++;
        }
        for (uint32_t u = 0u; u < ncalls; u++) {
            deriv_off[u + 1u] += deriv_off[u];
            deriv_cur[u] = deriv_off[u];
        }
        for (uint32_t i = 0u; i < st.deriv_len; i++) {
            uint32_t u = st.derivs[i].call_id;
            if (color[u] == 2u) deriv_list[deriv_cur[u]++] = i;
        }

        size_t root_cap = deriv_off[root_id + 1u] - deriv_off[root_id];
        root_vals = root_cap ? malloc(root_cap * sizeof(*root_vals)) : NULL;
        root_counts = root_cap ? malloc(root_cap * sizeof(*root_counts)) : NULL;
        if (root_cap && (!root_vals || !root_counts)) goto ms_done;
        size_t root_distinct = 0u;

        for (size_t ti = 0u; ti < topo_len; ti++) {
            uint32_t u = topo[ti];
            for (uint32_t dk = deriv_off[u]; dk < deriv_off[u + 1u]; dk++) {
                const TcalcSlgDeriv *dr = &st.derivs[deriv_list[dk]];
                uint64_t prod = 1u;
                for (uint32_t si = 0u; si < dr->sub_len; si++) {
                    TcalcSlgSubPair spair = st.sub_slab[dr->sub_off + si];
                    uint64_t m = tcalc_mult_map_get(&mult_map,
                                                    spair.sub_call_id + 1u,
                                                    spair.sub_val);
                    if (m == 0u) { prod = 0u; break; }
                    if (prod > UINT64_MAX / m) goto ms_done;
                    prod *= m;
                }
                if (prod == 0u) continue;
                bool is_new = false;
                if (!tcalc_mult_map_add(&mult_map, u + 1u, dr->val, prod, &is_new))
                    goto ms_done;
                if (u == root_id && is_new)
                    root_vals[root_distinct++] = dr->val;
            }
        }

        uint64_t total_answers = 0u;
        for (size_t i = 0u; i < root_distinct; i++) {
            uint64_t c = tcalc_mult_map_get(&mult_map, root_id + 1u, root_vals[i]);
            root_counts[i] = c;
            if (c > TCALC_SPACE_MAX_ANSWERS - total_answers) goto ms_done;
            total_answers += c;
        }
        if (language_id == CETTA_LANGUAGE_HE && total_answers > 1u) {
            /* HE preserves depth-first clause/branch/sub-answer order.
             * Reconstruct exact DFS order bottom-up along `topo` from the
             * recorded derivation DAG, including exact replay across callees
             * with non-adjacent duplicates (e.g. [A, B, A]). */
            enum { TCALC_HE_SEQ_SLAB_CAP = 524288u };
            TcalcMultMap rank_map = {0};
            uint8_t *non_adj_dup = calloc(ncalls, sizeof(*non_adj_dup));
            uint32_t *seq_off = calloc(ncalls, sizeof(*seq_off));
            uint32_t *seq_len = calloc(ncalls, sizeof(*seq_len));
            TcTerm *seq_buf = NULL;
            uint32_t seq_cap = 0u, seq_used = 0u;
            Atom *frontier = NULL;
            bool order_ok = non_adj_dup && seq_off && seq_len &&
                tcalc_mult_map_init(&rank_map, (size_t)st.deriv_len + 1u);
            for (size_t ti = 0u; order_ok && ti < topo_len; ti++) {
                uint32_t u = topo[ti];
                uint32_t lo = deriv_off[u];
                uint32_t hi = deriv_off[u + 1u];
                tcalc_he_heapsort_derivs(&st, &rank_map, deriv_list + lo, hi - lo);
                uint64_t next_rank = 0u;
                TcTerm prev_val = 0u;
                bool any_callee_non_adj = false;
                uint64_t u_total = 0u;
                for (uint32_t dk = lo; dk < hi; dk++) {
                    const TcalcSlgDeriv *dr = &st.derivs[deriv_list[dk]];
                    uint64_t prod = 1u;
                    for (uint32_t si = 0u; si < dr->sub_len; si++) {
                        TcalcSlgSubPair spair = st.sub_slab[dr->sub_off + si];
                        if (non_adj_dup[spair.sub_call_id])
                            any_callee_non_adj = true;
                        uint64_t m = tcalc_mult_map_get(&mult_map,
                                                        spair.sub_call_id + 1u,
                                                        spair.sub_val);
                        if (m == 0u) { prod = 0u; break; }
                        prod *= m;
                    }
                    if (prod == 0u) continue;
                    if (prod > (uint64_t)TCALC_HE_SEQ_SLAB_CAP - u_total) {
                        u_total = (uint64_t)TCALC_HE_SEQ_SLAB_CAP + 1u;
                    } else {
                        u_total += prod;
                    }
                    uint64_t existing = tcalc_mult_map_get(&rank_map, u + 1u, dr->val);
                    if (existing == 0u) {
                        if (!tcalc_mult_map_add(&rank_map, u + 1u, dr->val,
                                                ++next_rank, NULL)) {
                            order_ok = false;
                            break;
                        }
                    } else if (dr->val != prev_val) {
                        non_adj_dup[u] = 1u;
                    }
                    prev_val = dr->val;
                }
                if (!order_ok) break;
                if (any_callee_non_adj || non_adj_dup[u] || u == root_id) {
                    if (u_total > (uint64_t)TCALC_HE_SEQ_SLAB_CAP - (uint64_t)seq_used) {
                        order_ok = false;
                        break;
                    }
                    uint32_t need = seq_used + (uint32_t)u_total;
                    if (need > seq_cap) {
                        uint32_t ncap = seq_cap ? seq_cap * 2u : 1024u;
                        while (ncap < need) ncap *= 2u;
                        if (ncap > TCALC_HE_SEQ_SLAB_CAP) ncap = TCALC_HE_SEQ_SLAB_CAP;
                        TcTerm *grown = realloc(seq_buf, (size_t)ncap * sizeof(*grown));
                        if (!grown) { order_ok = false; break; }
                        seq_buf = grown;
                        seq_cap = ncap;
                    }
                    seq_off[u] = seq_used;
                    TcalcHeSeqEmitter em = {
                        .st = &st,
                        .rank_map = &rank_map,
                        .mult_map = &mult_map,
                        .deriv_list = deriv_list,
                        .seq_off = seq_off,
                        .seq_len = seq_len,
                        .seq_buf = seq_buf,
                        .non_adj_dup = non_adj_dup,
                        .out_buf = seq_buf + seq_used,
                        .out_pos = 0u,
                        .out_cap = (uint32_t)u_total,
                        .overflow = false,
                    };
                    tcalc_he_emit_slice(&em, lo, hi, 0u);
                    if (em.overflow || em.out_pos != (uint32_t)u_total) {
                        order_ok = false;
                        break;
                    }
                    seq_len[u] = em.out_pos;
                    seq_used += em.out_pos;
                    if (any_callee_non_adj) {
                        /* Re-check non-adjacent duplicates on actual replayed sequence */
                        for (uint32_t si = 1u; si < em.out_pos && !non_adj_dup[u]; si++) {
                            TcTerm cur_v = seq_buf[seq_off[u] + si];
                            if (cur_v != seq_buf[seq_off[u] + si - 1u]) {
                                uint64_t cr = tcalc_mult_map_get(&rank_map, u + 1u, cur_v);
                                uint64_t pr = tcalc_mult_map_get(&rank_map, u + 1u,
                                                                 seq_buf[seq_off[u] + si - 1u]);
                                if (cr <= pr) non_adj_dup[u] = 1u;
                            }
                        }
                    }
                }
            }
            if (order_ok && seq_len[root_id] == (uint32_t)total_answers) {
                frontier = atom_expr_builder_begin(a, (CettaExprLen)total_answers);
                if (!frontier) order_ok = false;
            } else {
                order_ok = false;
            }
            if (order_ok) {
                uint32_t roff = seq_off[root_id];
                for (CettaExprLen pos = 0u; pos < (CettaExprLen)total_answers; pos++) {
                    Atom *v = tcalc_tc_to_atom(entry->terms, entry->bool_sym,
                                               entry->float_sym, language_id,
                                               seq_buf[roff + pos], a, 0u);
                    if (!v) { order_ok = false; break; }
                    frontier->expr.elems[pos] = v;
                }
                if (order_ok) {
                    frontier = atom_expr_builder_finish(a, frontier);
                    if (frontier) {
                        *out = cetta_call_completed_frontier(frontier);
                        ok = true;
                    }
                }
            }
            free(seq_buf);
            free(seq_len);
            free(seq_off);
            tcalc_mult_map_free(&rank_map);
            free(non_adj_dup);
        } else {
            ok = tcalc_emit_answers_outcome(entry, language_id, a, root_vals,
                                            root_counts, root_distinct,
                                            total_answers, out);
        }
    ms_done:
        free(root_counts);
        free(root_vals);
        tcalc_mult_map_free(&mult_map);
        free(deriv_list);
        free(deriv_cur);
        free(deriv_off);
        free(topo);
        free(dfs_edge);
        free(dfs_node);
        free(color);
        free(adj_to);
        free(adj_cur);
        free(adj_off);
    }

cleanup:
    tcalc_slg_free(&st);
    return ok;
}

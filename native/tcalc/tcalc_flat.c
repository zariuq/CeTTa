/* tcalc_flat.c - backend x86-flat: the adapter between the core's matching and
 * the flat_fire decider.  It is part of the Cesta unit tcalc_core.cesta,
 * included after tcalc_core.c and checked with it, and in a file of its own so
 * that the calculus file reads without it: it is the C side of the seam for a
 * machine-code core, not the calculus. */

/* ---- Flat firings decided by another procedure (backend x86-flat) --------
 * A flat rule's firing is a selection of rows and word equalities: a pattern
 * item that is a ground term must equal one of the rule's constants, and a
 * local must equal the word where it first occurs.  C chooses the selection
 * (rows of each pattern's length, takes distinct, reads not taken); the decider
 * decides its equalities and copies out the made rows' words, in the packed
 * form tcalc_core.h describes.  Its output is checked to be terms. */

static uint32_t flat_select(uint32_t row, uint32_t column) { return row | column << 8; }

/* Whether a term is an expression of one to four items. */
static bool flat_shape(const TcTerms *t, TcTerm term) {
    TcNode n = term_node(t, term);
    return n.kind == TC_EXPRESSION && n.length >= 1 && n.length <= FLAT_ROW;
}

/* The constant slot of a ground term in the plan, added if new; FLAT_CONSTANTS
 * when the plan is full. */
static uint32_t flat_constant(FlatPlan *plan, TcTerm term) {
    for (uint32_t k = 0; k < plan->constants; k++) if (plan->constant[k] == term) return k;
    if (plan->constants == FLAT_CONSTANTS) return FLAT_CONSTANTS;
    plan->constant[plan->constants] = term;
    return plan->constants++;
}

/* The packed plan of a rule; false, with the reason in why, when it is not
 * flat.  Locals are placed where they first occur in the patterns. */
static bool flat_plan(const TcTerms *t, const ProgramView *view,
                      const TcStoredRule *r, FlatPlan *plan,
                      char *why, size_t why_size) {
    const char *name = view->name + r->name;
    const TcNode *nodes = t->node;
    if (!nodes) {
        snprintf(why, why_size, "rule %s has no term table", name);
        return false;
    }
    uint32_t patterns = r->takes + r->reads, place[FLAT_PATTERNS * FLAT_ROW];
    memset(plan, 0, sizeof *plan);
    if (r->tells || r->check_count) {
        snprintf(why, why_size, "rule %s tells or checks", name);
        return false;
    }
    if (patterns > FLAT_PATTERNS || r->locals > FLAT_PATTERNS * FLAT_ROW) {
        snprintf(why, why_size, "rule %s has more patterns or locals than a flat firing holds", name);
        return false;
    }
    for (uint32_t v = 0; v < r->locals; v++) place[v] = UINT32_MAX;
    for (uint32_t i = 0; i < patterns; i++) {
        TcTerm pattern = pattern_at(view, r, i);   /* takes, then reads */
        if (!flat_shape(t, pattern)) {
            snprintf(why, why_size, "a pattern of rule %s is not an expression of one to four items", name);
            return false;
        }
        TcNode n = term_node(t, pattern);
        for (uint32_t c = 0; c < n.length; c++) {
            TcTerm item = term_item(t, n.start + c);
            TcNode in = term_node(t, item);
            uint32_t other;
            if (in.ground) {
                uint32_t k = flat_constant(plan, item);
                if (k == FLAT_CONSTANTS) {
                    snprintf(why, why_size, "rule %s has more constants than a flat firing holds", name);
                    return false;
                }
                other = flat_select(patterns, k);
            } else if (in.kind == TC_LOCAL) {
                uint32_t local = (uint32_t)in.value;
                if (place[local] == UINT32_MAX) { place[local] = flat_select(i, c); continue; }
                other = place[local];
            } else {
                snprintf(why, why_size, "a pattern of rule %s has a local inside a nested term", name);
                return false;
            }
            if (plan->guards == FLAT_GUARDS) {
                snprintf(why, why_size, "rule %s has more guards than a flat firing holds", name);
                return false;
            }
            plan->guard[plan->guards++] = flat_select(i, c) | other << 16;
        }
    }
    for (uint32_t m = 0; m < r->makes; m++) {
        TcTerm made = make_at(view, r, m);
        if (!flat_shape(t, made)) {
            snprintf(why, why_size, "a made row of rule %s is not an expression of one to four items", name);
            return false;
        }
        TcNode n = term_node(t, made);
        if (plan->makes + n.length > FLAT_MAKES) {
            snprintf(why, why_size, "rule %s makes more words than a flat firing holds", name);
            return false;
        }
        for (uint32_t c = 0; c < n.length; c++) {
            TcTerm item = term_item(t, n.start + c);
            TcNode in = term_node(t, item);
            uint32_t from;
            if (in.ground) {
                uint32_t k = flat_constant(plan, item);
                if (k == FLAT_CONSTANTS) {
                    snprintf(why, why_size, "rule %s has more constants than a flat firing holds", name);
                    return false;
                }
                from = flat_select(patterns, k);
            } else if (in.kind == TC_LOCAL && place[in.value] != UINT32_MAX) {
                from = place[in.value];
            } else {
                snprintf(why, why_size, "a made row of rule %s has a local no pattern places", name);
                return false;
            }
            plan->make[plan->makes++] = from;
        }
        plan->made_length[plan->made_rows++] = n.length;
    }
    return true;
}

bool tc_program_flat(const TcProgram *p, char *why, size_t why_size) {
    FlatPlan plan = {0};
    ProgramView view = program_view(p);
    for (size_t i = 0; i < p->rows; i++)
        if (!term_node(p->terms, p->row[i]).ground || !flat_shape(p->terms, p->row[i])) {
            snprintf(why, why_size, "a row of the initial state is not a ground expression of one to four items");
            return false;
        }
    for (size_t k = 0; k < p->rules; k++)
        if (p->rule[k].same == k &&
            !flat_plan(p->terms, &view, &p->rule[k], &plan, why, why_size)) return false;
    return true;
}

/* Decides one full selection (x->pick) through the decider and records the
 * match it admits.  False when memory ran out or the decider gave no verdict. */
static bool flat_decide(Explorer *x, uint32_t rule, const FlatPlan *plan) {
    if (!x->how.flat || !x->flat_words || !x->pick) { x->flat_failed = true; return false; }
    TcStoredRule r = x->program.rule[rule];
    uint32_t total = r.takes + r.reads, premise[FLAT_PATTERNS] = {0}, out[FLAT_MAKES] = {0};
    for (uint32_t i = 0; i < total; i++) premise[i] = FLAT_ROW * x->pick[i];
    uint64_t verdict = x->how.flat(x->flat_words, (size_t)FLAT_ROW * x->rows, premise, total,
                                   plan->constant, plan->constants, plan->guard, plan->guards,
                                   plan->make, plan->makes, out);
    if (verdict == 0) return true;   /* a guard failed: no match */
    for (uint32_t j = 0; verdict == 1 && j < plan->makes; j++)
        if (out[j] == 0 || out[j] >= x->terms->nodes) verdict = 2;   /* not a term */
    if (verdict != 1) { x->flat_failed = true; return false; }
    x->next_len = 0;
    for (uint32_t m = 0, at = 0; m < plan->made_rows; at += plan->made_length[m], m++) {
        TcTerm made = tc_expression(x->terms, &out[at], plan->made_length[m]);
        if (!made || !next_push(x, made)) return false;
    }
    bool *gone = calloc(x->rows ? x->rows : 1, sizeof *gone);
    if (!gone) return false;
    for (uint32_t i = 0; i < r.takes; i++) gone[x->pick[i]] = true;
    uint32_t succ = successor_state(x, gone);
    free(gone);
    return succ != UINT32_MAX && record_member(x, rule, total, true, succ, UINT32_MAX);
}

/* Chooses rows (x->pick) for the patterns of a flat rule from position k on
 * and lets the decider decide each full selection.  The backend runs step
 * one, so at each position one copy of each row term is tried, as in
 * match_from. */
static bool flat_match_from(Explorer *x, uint32_t rule, const FlatPlan *plan, uint32_t k) {
    TcStoredRule r = rule_at(&x->program, rule);
    if (k == r.takes + r.reads) return flat_decide(x, rule, plan);
    bool take = k < r.takes;
    TcTerm pattern = pattern_at(&x->program, &r, k);   /* takes, then reads */
    uint32_t length = term_node(x->terms, pattern).length;
    Runs runs = candidates(x, pattern, 0);
    if (x->how.matches == TC_MATCHES_SYMMETRIC) {
        uint32_t twin = identical_before(x, &r, k);
        uint32_t bound = twin == UINT32_MAX ? 0 : pick_at(x, twin) + (take ? 1 : 0);
        for (int p = 0; p < 2; p++) if (runs.lo[p] < bound) runs.lo[p] = bound;
    }
    for (int p = 0; p < 2; p++) {
        TcTerm tried = 0;   /* the row term last tried at this position */
        for (uint32_t i = runs.lo[p]; i < runs.hi[p]; i++) {
            TcTerm row = row_at(x, i);
            if (term_node(x->terms, row).length != length) continue;
            bool clash = false;   /* takes distinct; a read is never a taken row */
            for (uint32_t j = 0; j < (take ? k : r.takes) && !clash; j++) clash = pick_at(x, j) == i;
            if (clash || row == tried) continue;
            tried = row;
            pick_put(x, k, i);
            if (!flat_match_from(x, rule, plan, k + 1)) return false;
        }
    }
    return true;
}

/* Every match of a flat program at the state, each decided by the decider. */
static bool flat_collect(Explorer *x) {
    size_t words = (size_t)FLAT_ROW * (x->rows ? x->rows : 1);
    if (!reserve(&x->flat_words, &x->flat_words_cap, words)) return false;
    uint32_t *packed = x->flat_words;
    if (!packed) return false;
    for (uint32_t i = 0; i < x->rows; i++) {   /* each row's items, then 0 */
        const TcNode *n = node_of(x->terms, x->row[i]);
        for (uint32_t c = 0; c < FLAT_ROW; c++)
            packed[FLAT_ROW * i + c] = c < n->length ? x->terms->item[n->start + c] : 0;
    }
    x->ground = true;   /* a flat program's rows are ground */
    if (!ensure_pick(x, FLAT_PATTERNS)) return false;
    for (uint32_t rule = 0; rule < x->program.rules; rule++) {
        if (x->program.rule[rule].same != rule) continue;
        if (!flat_match_from(x, rule, &x->plans[rule], 0)) return false;
    }
    return true;
}

/* The plans of a flat program's rules, packed when an exploration starts.
 * False when memory ran out, or (x->flat_failed) when the decider cannot take
 * the program or the interpretation: it decides one match at a time. */
static bool flat_start(Explorer *x) {
    char why[160] = {0};
    if (x->how.step != TC_STEP_ONE || !x->how.flat) { x->flat_failed = true; return false; }
    if (!x->plans) return false;
    for (size_t k = 0; k < x->program.rules; k++)
        if (x->program.rule[k].same == k &&
            !flat_plan(x->terms, &x->program, &x->program.rule[k],
                       &x->plans[k], why, sizeof why)) {
            x->flat_failed = true;
            return false;
        }
    return true;
}

/* A program owns its prepared rule prefix. Appending a rule extends this
 * cache; appended terms retain their ids. No binding-dependent value is kept.
 * Failure to prepare an optimization leaves the ordinary solver available. */
static void resident_prepare(TcProgram *p) {
    if (!cetta_kernel_batch || p->prepared_count==p->rules || p->rules>SIZE_MAX/sizeof(FlatPlan)) return;
    FlatPlan *grown=realloc(p->prepared,p->rules*sizeof *grown);
    if (!grown) return;
    p->prepared=grown;
    ProgramView view={p->rule,p->rules,p->term,p->terms_len,p->check,p->checks_len,p->name,NULL,0};
    for (size_t k=p->prepared_count;k<p->rules;++k) {
        FlatPlan plan={0};char why[160]={0};
        if (p->rule[k].same!=k || !flat_plan(p->terms,&view,&p->rule[k],&plan,why,sizeof why))
            memset(&plan,0,sizeof plan),plan.guards=UINT32_MAX;
        if (plan.guards!=UINT32_MAX) {
            bool indexed=true;uint32_t patterns=p->rule[k].takes+p->rule[k].reads;
            for (uint32_t i=0;indexed && i<patterns;++i) {
                TcNode n=term_node(p->terms,pattern_at(&view,&p->rule[k],i));
                TcNode head=term_node(p->terms,term_item(p->terms,n.start));
                if (head.kind==TC_LOCAL) for (uint32_t g=0;g<plan.guards;++g)
                    if ((plan.guard[g]&65535u)==i) { indexed=false;break; }
            }
            /* A head bound by an earlier pattern needs its dynamic index.
             * Retain the general matcher until that index is in the batch. */
            if (!indexed) plan.guards=UINT32_MAX;
        }
        p->prepared[k]=plan;
        p->prepared_count=k+1;
    }
}

/* Publish one private, completely checked selection. The same shortcuts and
 * state builder as ordinary matching decide the observable successor. */
static bool resident_record(Explorer *x,uint32_t rule,const FlatPlan *plan,const uint32_t *record) {
    TcStoredRule r=rule_at(&x->program,rule);uint32_t total=r.takes+r.reads;
    for (uint32_t i=0;i<total;++i) pick_put(x,i,record[i]);
    TcMember same={0};
    if (known_selection(x,rule,total,&same))
        return record_member(x,rule,total,same.alone,same.successor,same.firing);
    x->fire=r;x->fire_rule=rule;
    uint32_t derived=derived_successor(x,total),succ=derived;
    if (derived==UINT32_MAX || x->how.shortcuts==TC_SHORTCUTS_CHECKED) {
        x->next_len=0;
        for (uint32_t m=0,at=0;m<plan->made_rows;at+=plan->made_length[m],++m) {
            TcTerm made=tc_expression(x->terms,record+total+at,plan->made_length[m]);
            if (!made || !next_push(x,made)) return false;
        }
        bool *gone=calloc(x->rows ? x->rows : 1,sizeof *gone);
        if (!gone) return false;
        for (uint32_t i=0;i<r.takes;++i) gone[pick_at(x,i)]=true;
        succ=successor_state(x,gone);free(gone);
        if (derived!=UINT32_MAX && derived!=succ) x->disagreed=true;
    }
    if (succ==UINT32_MAX) return false;
    uint32_t firing=remember_firing(x,total,succ);
    if (!record_member(x,rule,total,true,succ,firing)) return false;
    remember_selection(x,(uint32_t)x->members-1,total);return true;
}

/* One resident rule circuit over the original term buffers. The native call
 * never allocates, calls C or publishes. Source pointers are borrowed only
 * inside each call: host interning may relocate pools between result blocks. */
static bool resident_collect(Explorer *x,uint32_t rule,const FlatPlan *plan) {
    TcStoredRule r=rule_at(&x->program,rule);uint32_t total=r.takes+r.reads;
    const FlatPlan physical=*plan;
    uint32_t stages[TC_BATCH_PATTERNS*TC_BATCH_STAGE_WORDS]={0},guard=0;
    for (uint32_t k=0;k<total;++k) {
        uint32_t offset=TC_BATCH_STAGE_WORDS*k;
        TcTerm pattern=pattern_at(&x->program,&r,k);Runs runs=candidates(x,pattern,0);
        stages[offset+TC_BATCH_LENGTH]=term_node(x->terms,pattern).length;
        stages[offset+TC_BATCH_LO0]=runs.lo[0];stages[offset+TC_BATCH_HI0]=runs.hi[0];
        stages[offset+TC_BATCH_LO1]=runs.lo[1];stages[offset+TC_BATCH_HI1]=runs.hi[1];
        stages[offset+TC_BATCH_TWIN]=identical_before(x,&r,k);stages[offset+TC_BATCH_GUARD_BEGIN]=guard;
        while (guard<plan->guards && (plan->guard[guard]&255u)==k) ++guard;
        stages[offset+TC_BATCH_GUARD_END]=guard;
    }
    size_t stride=(size_t)total+plan->makes;if (!stride) stride=1;
    if (!reserve(&x->batch_cursor,&x->batch_cursor_cap,TC_BATCH_CURSOR_WORDS) ||
        !reserve(&x->batch_output,&x->batch_output_cap,64*stride)) return false;
    memset(x->batch_cursor,0,TC_BATCH_CURSOR_WORDS*sizeof *x->batch_cursor);
    CettaKernelMode mode=cetta_kernel_requested();
    for (;;) {
        uint64_t status,produced;
        {
            TcBatchInput input={x->terms->node,x->terms->nodes,x->terms->item,x->terms->items,
                x->row,x->rows,&physical,stages,total,r.takes,TC_BATCH_ONE|
                (x->how.matches==TC_MATCHES_SYMMETRIC ? TC_BATCH_SYMMETRIC : 0),4096,UINT64_MAX};
            TcBatchWork work={x->batch_cursor,x->batch_cursor_cap,x->batch_output,x->batch_output_cap,
                0,x->batch_used,0,0};
            status=cetta_kernel_batch(mode,&input,&work);produced=work.produced;x->batch_used=work.used;
        }
        if (status!=TC_BATCH_END && status!=TC_BATCH_YIELD) { x->solver.failed=true;return false; }
        for (uint64_t i=0;i<produced;++i)
            if (!resident_record(x,rule,plan,x->batch_output+i*stride)) return false;
        if (status==TC_BATCH_END) return true;
    }
    return false;
}

/* tcalc_plan.c - join plans: how each rule of a program is run, decided once
 * for the program and kept with it as data.  Included by tcalc_core.c at its
 * section 4, in the Cesta unit tcalc_core.cesta with the rest.
 *
 * A match is a rule and a row for each of its patterns; its information is
 * the equations of each pattern with its row (Core.lean: Firing.information),
 * a conjunction.  The patterns may be met in any order: the information has
 * one most general solution whatever the order it is solved in, and admission
 * is decided there (FirstOrderLicence.admits_iff_checked).  So which pattern
 * is met next, which index finds its rows and where a check is decided are
 * decided here, once, when the program is first explored; no match is planned
 * when it is met.
 *
 * A plan is a list of ops over the rule's patterns (named by position: the
 * takes, then the reads), its checks, its tells and its made rows:
 *
 *   probe P R    the rows pattern P can meet, by the probe R: the rows of
 *                its relation whose terms at the first L paths of an index
 *                are the pattern's terms there (L = 0: every row of the
 *                relation)
 *   unify P      pattern P equal to the row met
 *   check C      an equation or a guard, at the first step where every local
 *                of it that some pattern binds is bound
 *   absent C R   an absence, a count or a census, its rows read by the probe
 *                R, placed as a check
 *   tell E       the rule's equation E
 *   take P       the row pattern P met leaves
 *   make M       made row M arrives
 *
 * Each probe is followed by the unify of its pattern, then the checks its
 * bindings complete; the tells, takes and makes end the plan, in the order of
 * the firing's protocol (Fire.module).  A check placed at a step is decided
 * there when the values it reads are fixed from that step on: the rule tells
 * nothing and the store holds fixed terms, so the patterns met bind the
 * locals they mention to fixed terms and nothing later binds them again
 * (SetStoreSyntax.matches_determine_scoped_terms: the matched patterns
 * determine every term scoped to their locals).  Otherwise the protocol asks
 * it after the matching, as Core.lean orders it.
 *
 * Plans per rule: one per entry pattern, the pattern the rows of a delta
 * enter by (the run by delta's semi-naive split, tcalc_delta.c), and one with
 * no entry, for a step that meets every match.  After the entry, the patterns
 * are taken in an order fixed by what the patterns met so far bind:
 *   - a pattern whose locals are all bound (a fully keyed lookup) first,
 *   - then one keyed by a bound local, then one keyed by its constants only,
 *     then one read by its whole relation, then one of no fixed relation;
 *   - within these, the one after which more checks are decided, then the
 *     one with more keys, then the one written first.
 * A pattern is never taken before an identical pattern of its part written
 * before it, so that the symmetric reading of matches (rows rising along a
 * run of identical patterns) is met in the order it is defined in.
 *
 * Keys and indexes.  A pattern keys its relation at its places: down to the
 * items of its arguments' items, each constant (a fixed term with no items,
 * or any fixed term at the deepest level) and each local, a place named by
 * its path (path_key: an argument, an item of it, an item of that).  A
 * probe's search is the set of places whose terms are fixed when it runs:
 * the constants and the bound locals.  A row of fixed terms meets the pattern
 * only if it holds the pattern's fixed term at every such place: two fixed
 * terms unify only when they are one term.  So two searches holding
 * different constants at their first constant path never read the same row,
 * and searches are split by that path and constant: an index serves the
 * searches of one split and keeps only the rows holding its constant there
 * (its domain), as the calls of a demand reading are each a relation of their
 * own.  The searches a program makes on one relation and split are sets of
 * paths; searches each contained in the next are served by one index
 * ordering its paths that way, a lookup by the first k paths reading the rows
 * under its keys there.  The indexes kept are a minimum chain cover of the
 * searches, found by bipartite matching (Subotic et al., VLDB 2018, as in
 * Souffle): by Dilworth's theorem the fewest chains are the searches less a
 * largest matching of each search to a strictly larger one.
 *
 * The plans are plain words, in terms of the program's terms and its rules'
 * positions, independent of how a run keeps its rows (their layout and ops
 * are tcalc_core.h's, tc_program_plans): the interpreter of tcalc_core.c runs
 * them over a state's maps and over the one store of a run by delta, and a
 * compiled backend can read the same words. */

enum {
    PLAN_KEYS = TC_PLAN_KEYS,   /* the most paths an index orders */
    PLAN_PLACES = 64            /* the most places of a pattern considered */
};
_Static_assert(TC_PLAN_KEYS == INDEX_PATHS, "an index's paths are those the stores key");

enum { PLAN_PROBE = TC_PLAN_PROBE, PLAN_UNIFY = TC_PLAN_UNIFY, PLAN_CHECK = TC_PLAN_CHECK,
       PLAN_ABSENT = TC_PLAN_ABSENT, PLAN_TELL = TC_PLAN_TELL, PLAN_TAKE = TC_PLAN_TAKE,
       PLAN_MAKE = TC_PLAN_MAKE };

#define PLAN_OPEN TC_PLAN_OPEN
#define PLAN_NONE TC_PLAN_NONE

/* ---- Reading plans ----------------------------------------------------------- */

static inline uint32_t plan_at(const ProgramView *v, size_t at) { return v->plan[at]; }
static inline uint32_t plan_indexes(const ProgramView *v) { return plan_at(v, plan_at(v, 1)); }
/* Index c: its record's offset, its relation, its paths and its path l. */
static inline uint32_t plan_index(const ProgramView *v, uint32_t c) {
    return plan_at(v, (size_t)plan_at(v, 1) + 1 + c);
}
static inline uint32_t index_head(const ProgramView *v, uint32_t c) { return plan_at(v, plan_index(v, c)); }
static inline uint32_t index_length(const ProgramView *v, uint32_t c) {
    return plan_at(v, (size_t)plan_index(v, c) + 1);
}
static inline uint32_t index_paths(const ProgramView *v, uint32_t c) {
    return plan_at(v, (size_t)plan_index(v, c) + 2);
}
static inline uint32_t index_path_at(const ProgramView *v, uint32_t c, uint32_t l) {
    return plan_at(v, (size_t)plan_index(v, c) + 5 + l);
}
/* Index c's levels: bit l - 1 for each level l some probe reads it at. */
static inline uint32_t index_levels(const ProgramView *v, uint32_t c) {
    return plan_at(v, (size_t)plan_index(v, c) + 4);
}
/* Index c's domain: the offset of its keys at its first path, 0 when it
 * keeps every row. */
static inline uint32_t index_domain(const ProgramView *v, uint32_t c) {
    return plan_at(v, (size_t)plan_index(v, c) + 3);
}

/* The shape of index c as a store keeps it (tcalc_store.c): its words in the
 * plans. */
static IndexShape index_shape(const ProgramView *v, uint32_t c) {
    uint32_t record = plan_index(v, c), domain = index_domain(v, c);   /* none: no keys, read nowhere */
    return (IndexShape){v->plan + record + 5, plan_at(v, (size_t)record + 2), plan_at(v, (size_t)record + 4),
                        v->plan + domain + (domain ? 1 : 0), domain ? plan_at(v, domain) : 0};
}

/* The split of index c: the path its domain's key is at (0: it keeps every
 * row), and that key. */
static uint32_t index_split(const ProgramView *v, uint32_t c, TcTerm *key) {
    uint32_t domain = index_domain(v, c);
    *key = domain ? plan_at(v, (size_t)domain + 1) : 0;
    return domain ? index_path_at(v, c, 0) : 0;
}

/* The plan of rule `rule` (as stored first) entering by pattern e, or with
 * no entry (e: its patterns). */
static inline uint32_t plan_of(const ProgramView *v, uint32_t rule, uint32_t e) {
    return plan_at(v, (size_t)plan_at(v, 2 + (size_t)rule) + 1 + e);
}
/* The probe of check i of rule `rule` (as stored first); 0 when it reads no
 * row. */
static inline uint32_t plan_check_probe(const ProgramView *v, uint32_t rule, uint32_t i) {
    uint32_t at = plan_at(v, 2 + (size_t)rule);
    return plan_at(v, (size_t)at + 1 + plan_at(v, at) + i);
}
static inline uint32_t plan_ops(const ProgramView *v, uint32_t plan) { return plan_at(v, plan); }
/* Word w of op k of a plan: the op, then its operands. */
static inline uint32_t plan_op(const ProgramView *v, uint32_t plan, uint32_t k, uint32_t w) {
    return plan_at(v, (size_t)plan + 1 + 4 * (size_t)k + w);
}
/* A probe: the term probed, its relation, index and level, and its key l. */
static inline TcTerm probe_term(const ProgramView *v, uint32_t probe) { return plan_at(v, probe); }
static inline uint32_t probe_head(const ProgramView *v, uint32_t probe) { return plan_at(v, (size_t)probe + 1); }
static inline uint32_t probe_length(const ProgramView *v, uint32_t probe) {
    return plan_at(v, (size_t)probe + 2);
}
static inline uint32_t probe_index(const ProgramView *v, uint32_t probe) { return plan_at(v, (size_t)probe + 3); }
static inline uint32_t probe_level(const ProgramView *v, uint32_t probe) { return plan_at(v, (size_t)probe + 4); }
static inline TcTerm probe_key(const ProgramView *v, uint32_t probe, uint32_t l) {
    return plan_at(v, (size_t)probe + 5 + l);
}

/* ---- Locals -------------------------------------------------------------------- */

/* Whether every local of `term` is marked in `read` (or, with `mark`, marks
 * them); false for a local beyond the rule's. */
static bool locals_in(const TcTerms *t, TcTerm term, bool *read, uint32_t locals, bool mark) {
    TcNode n = term_node(t, term);
    if (n.ground) return true;
    if (n.kind == TC_LOCAL) {
        if ((uint64_t)n.value >= locals) return false;
        if (mark) read[n.value] = true;
        return read[n.value];
    }
    if (n.kind != TC_EXPRESSION) return false;
    for (uint32_t i = 0; i < n.length; i++)
        if (!locals_in(t, term_item(t, n.start + i), read, locals, mark)) return false;
    return true;
}

/* Whether every local of `term` that some pattern binds (`bound_by`) is
 * bound. */
static bool check_ready(const TcTerms *t, TcTerm term, const bool *bound, const bool *bound_by,
                        uint32_t locals) {
    TcNode n = term_node(t, term);
    if (n.ground) return true;
    if (n.kind == TC_LOCAL)
        return (uint64_t)n.value >= locals || !bound_by[n.value] || bound[n.value];
    if (n.kind != TC_EXPRESSION) return true;
    for (uint32_t i = 0; i < n.length; i++)
        if (!check_ready(t, term_item(t, n.start + i), bound, bound_by, locals)) return false;
    return true;
}

/* ---- Places and relations ------------------------------------------------------- */

/* The relation a term is met in: its head symbol and length, or PLAN_OPEN
 * when no symbol is fixed at its head (a local, or an expression headed by
 * another term); a term with no items is of the relation (0, 0). */
static void term_relation(const TcTerms *t, TcTerm term, uint32_t *head, uint32_t *length) {
    TcNode n = term_node(t, term);
    *head = 0;
    *length = 0;
    if (n.kind == TC_LOCAL) { *head = PLAN_OPEN; return; }
    if (n.kind != TC_EXPRESSION || n.length == 0) return;
    if (term_node(t, term_item(t, n.start)).kind != TC_SYMBOL) { *head = PLAN_OPEN; return; }
    *head = term_item(t, n.start);
    *length = n.length;
}

/* Appends the places of `term`, at `path` and `depth` (1: an argument), to
 * paths[] and their terms to at[], up to PLAN_PLACES. */
static void places_below(const TcTerms *t, TcTerm term, uint32_t path, uint32_t depth, uint32_t *paths,
                         TcTerm *at, uint32_t *n) {
    if (*n == PLAN_PLACES) return;
    TcNode tn = term_node(t, term);
    if (tn.kind == TC_LOCAL || (tn.ground && (tn.kind != TC_EXPRESSION || tn.length == 0 || depth == 3))) {
        paths[*n] = path;
        at[*n] = term;
        (*n)++;
        return;
    }
    if (tn.kind != TC_EXPRESSION || depth == 3) return;
    for (uint32_t i = 0; i < tn.length && i < 1022u; i++)
        places_below(t, term_item(t, tn.start + i), path | (i + 1u) << (10 * depth), depth + 1, paths, at, n);
}

/* The search `term` makes given the bound locals: the paths of its places
 * whose terms are fixed, sorted, at most PLAN_KEYS (the first in path
 * order), in out[]; *constants, a bit for each that is a constant there;
 * *split, the first of those (0: none) and *split_key its constant; *joined,
 * whether a bound local is among them. */
static uint32_t term_search(const TcTerms *t, TcTerm term, const bool *bound, uint32_t locals, uint32_t *out,
                            uint32_t *constants, uint32_t *split, TcTerm *split_key, bool *joined) {
    uint32_t paths[PLAN_PLACES] = {0}, places = 0, n = 0;
    TcTerm at[PLAN_PLACES] = {0};
    TcNode tn = term_node(t, term);
    *joined = false;
    *constants = 0;
    *split = 0;
    *split_key = 0;
    if (tn.kind != TC_EXPRESSION) return 0;
    for (uint32_t a = 1; a < tn.length && a < 1024u; a++)
        places_below(t, term_item(t, tn.start + a), a, 1, paths, at, &places);
    uint32_t kept[PLAN_PLACES] = {0};
    TcTerm held[PLAN_PLACES] = {0};
    for (uint32_t i = 0; i < places; i++) {
        TcNode pn = term_node(t, at[i]);
        if (pn.kind == TC_LOCAL) {
            if ((uint64_t)pn.value >= locals || !bound[pn.value]) continue;
            *joined = true;
        }
        uint32_t k = n++;   /* by path */
        while (k > 0 && kept[k - 1] > paths[i]) { kept[k] = kept[k - 1]; held[k] = held[k - 1]; k--; }
        kept[k] = paths[i];
        held[k] = at[i];
    }
    if (n > PLAN_KEYS) n = PLAN_KEYS;
    for (uint32_t i = 0; i < n; i++) {
        out[i] = kept[i];
        if (term_node(t, held[i]).kind == TC_LOCAL) continue;
        *constants |= 1u << i;
        if (!*split) { *split = kept[i]; *split_key = held[i]; }
    }
    return n;
}

/* ---- Choosing plans ---------------------------------------------------------------
 * The plans are written into the pool of words above as they are chosen; a
 * probe is written with its search, and given its index, level and keys once
 * the indexes are chosen. */
typedef struct {
    uint32_t *word; size_t words, word_cap;          /* the pool being written                 */
    uint32_t *probe; size_t probes, probe_cap;       /* 2 words a probe written: its offset, its
                                                        search + 1 (0: none)                   */
    uint32_t *search; size_t searches, search_cap;   /* SEARCH words a search (below)          */
    uint32_t *path; size_t paths, path_cap;          /* the searches' paths, each sorted       */
    uint32_t *op; size_t ops, op_cap;                /* the plan being chosen: 4 words an op   */
    uint32_t *pending; size_t pendings, pending_cap; /* its probes: 4 words, term, head,
                                                        length, search + 1                     */
    uint32_t *chain; size_t chains, chain_cap;       /* the indexes chosen: 2 words, the first
                                                        of their searches in `member`, their
                                                        searches                               */
    uint32_t *member; size_t members, member_cap;    /* each index's searches, smallest first  */
} Planner;

static void planner_free(Planner *pl) {
    free(pl->word); free(pl->probe); free(pl->search); free(pl->path); free(pl->op); free(pl->pending);
    free(pl->chain); free(pl->member);
}

/* A search: its relation (head, length), its first path in `path`, its
 * paths, a bit for each path every pattern making it holds a constant at, and
 * its split: its first constant path and the constant there (0: none).  Two
 * searches of different splits never read each other's rows, and are served
 * by different indexes. */
enum { SEARCH = 7, S_HEAD = 0, S_LENGTH, S_FIRST, S_PATHS, S_CONSTANTS, S_SPLIT, S_SPLIT_KEY };

static inline uint32_t search_word(const Planner *pl, size_t at) { return pl->search[at]; }
static inline uint32_t search_of(const Planner *pl, uint32_t s, uint32_t w) {
    return search_word(pl, SEARCH * (size_t)s + w);
}

/* Appends a word to the pool; false when memory ran out. */
static bool planner_word(Planner *pl, uint32_t word) {
    return tc_push_u32(realloc, &pl->word, &pl->words, &pl->word_cap, word);
}

/* Sets word `at` of the pool, written before. */
static void planner_put(Planner *pl, size_t at, uint32_t word) {
    if (pl->word && at < pl->words) pl->word[at] = word;
}
static inline uint32_t path_word(const Planner *pl, size_t at) { return pl->path[at]; }
static inline uint32_t member_word(const Planner *pl, size_t at) { return pl->member[at]; }

/* The search (relation, paths, split) + 1, the one made before when it was
 * (its constant paths then those constant in both); 0 for no paths;
 * UINT32_MAX when memory ran out. */
static uint32_t planner_search(Planner *pl, uint32_t head, uint32_t length, const uint32_t *paths, uint32_t n,
                               uint32_t constants, uint32_t split, TcTerm split_key) {
    if (!n) return 0;
    for (uint32_t s = 0; s < pl->searches / SEARCH; s++) {
        if (search_of(pl, s, S_HEAD) != head || search_of(pl, s, S_LENGTH) != length ||
            search_of(pl, s, S_PATHS) != n || search_of(pl, s, S_SPLIT) != split ||
            search_of(pl, s, S_SPLIT_KEY) != split_key)
            continue;
        uint32_t first = search_of(pl, s, S_FIRST), i = 0;
        while (i < n && path_word(pl, (size_t)first + i) == paths[i]) i++;
        if (i < n) continue;
        pl->search[SEARCH * (size_t)s + S_CONSTANTS] &= constants;
        return s + 1;
    }
    uint32_t words[SEARCH] = {head, length, (uint32_t)pl->paths, n, constants, split, split_key};
    for (uint32_t i = 0; i < n; i++)
        if (!tc_push_u32(realloc, &pl->path, &pl->paths, &pl->path_cap, paths[i])) return UINT32_MAX;
    for (int w = 0; w < SEARCH; w++)
        if (!tc_push_u32(realloc, &pl->search, &pl->searches, &pl->search_cap, words[w])) return UINT32_MAX;
    return (uint32_t)(pl->searches / SEARCH);
}

/* A probe of `term` given the bound locals, among the plan's pending ones:
 * its position.  UINT32_MAX when memory ran out. */
static uint32_t planner_probe(Planner *pl, const TcTerms *t, TcTerm term, const bool *bound, uint32_t locals) {
    uint32_t head = 0, length = 0, paths[PLAN_KEYS] = {0}, search = 0, constants = 0, split = 0;
    TcTerm split_key = 0;
    bool joined = false;
    term_relation(t, term, &head, &length);
    if (head != PLAN_OPEN) {
        uint32_t n = term_search(t, term, bound, locals, paths, &constants, &split, &split_key, &joined);
        search = planner_search(pl, head, length, paths, n, constants, split, split_key);
        if (search == UINT32_MAX) return UINT32_MAX;
    }
    uint32_t at = (uint32_t)(pl->pendings / 4), words[4] = {term, head, length, search};
    for (int w = 0; w < 4; w++)
        if (!tc_push_u32(realloc, &pl->pending, &pl->pendings, &pl->pending_cap, words[w])) return UINT32_MAX;
    return at;
}

/* Writes probe p of the pending ones to the pool: its offset, 0 when memory
 * ran out. */
static uint32_t planner_write_probe(Planner *pl, uint32_t p) {
    uint32_t at = (uint32_t)pl->words;
    uint32_t words[5] = {pl->pending[4 * (size_t)p], pl->pending[4 * (size_t)p + 1],
                         pl->pending[4 * (size_t)p + 2], PLAN_NONE, 0};
    for (int w = 0; w < 5; w++)
        if (!planner_word(pl, words[w])) return 0;
    for (int w = 0; w < PLAN_KEYS; w++)
        if (!planner_word(pl, 0)) return 0;
    if (pl->pending[4 * (size_t)p + 3] &&
        (!tc_push_u32(realloc, &pl->probe, &pl->probes, &pl->probe_cap, at) ||
         !tc_push_u32(realloc, &pl->probe, &pl->probes, &pl->probe_cap, pl->pending[4 * (size_t)p + 3])))
        return 0;
    return at;
}

static bool planner_op(Planner *pl, uint32_t op, uint32_t a, uint32_t b) {
    uint32_t words[4] = {op, a, b, 0};
    for (int w = 0; w < 4; w++)
        if (!tc_push_u32(realloc, &pl->op, &pl->ops, &pl->op_cap, words[w])) return false;
    return true;
}

/* The scratch of one rule's planning: the bound locals, the locals some
 * pattern binds, a trial binding, the patterns met, the checks placed. */
typedef struct {
    bool *bound; bool *bound_by; bool *trial; bool *met; bool *placed;
    uint32_t locals, patterns, checks;
} RulePlanning;

/* Places every check not yet placed whose locals that patterns bind are
 * bound: the equations and guards, then the checks that read rows (their
 * probes written with the rule, `probes[i]`).  Once every pattern is met,
 * every check is placed. */
static bool planner_checks(Planner *pl, const TcTerms *t, const ProgramView *v, const TcStoredRule *r,
                           RulePlanning *rp, const uint32_t *probes) {
    for (int reads = 0; reads < 2; reads++)
        for (uint32_t i = 0; i < r->check_count; i++) {
            TcCheck c = check_at(v, r, i);
            if (rp->placed[i] || check_binary(c.kind) == (reads == 1)) continue;
            if (!check_ready(t, c.left, rp->bound, rp->bound_by, rp->locals) ||
                (check_binary(c.kind) && !check_ready(t, c.right, rp->bound, rp->bound_by, rp->locals)))
                continue;
            rp->placed[i] = true;
            if (!planner_op(pl, reads ? PLAN_ABSENT : PLAN_CHECK, i, reads ? probes[i] : 0)) return false;
        }
    return true;
}

/* How pattern k ranks as the next pattern met (lower first): its class,
 * then the checks it completes, its keys, its position. */
static uint64_t pattern_rank(const TcTerms *t, const ProgramView *v, const TcStoredRule *r, RulePlanning *rp,
                             uint32_t k) {
    TcTerm pattern = pattern_at(v, r, k);
    uint32_t head = 0, length = 0, paths[PLAN_KEYS] = {0}, keys = 0, rank_class = 3, completes = 0;
    bool joined = false;
    term_relation(t, pattern, &head, &length);
    if (head == PLAN_OPEN) rank_class = 4;
    else {
        uint32_t constants = 0, split = 0;
        TcTerm split_key = 0;
        keys = term_search(t, pattern, rp->bound, rp->locals, paths, &constants, &split, &split_key, &joined);
        if (locals_in(t, pattern, rp->bound, rp->locals, false)) rank_class = 0;
        else if (joined) rank_class = 1;
        else if (keys) rank_class = 2;
    }
    for (uint32_t i = 0; i < rp->locals; i++) rp->trial[i] = rp->bound[i];
    (void)locals_in(t, pattern, rp->trial, rp->locals, true);
    for (uint32_t i = 0; i < r->check_count; i++) {
        TcCheck c = check_at(v, r, i);
        if (!rp->placed[i] && check_ready(t, c.left, rp->trial, rp->bound_by, rp->locals) &&
            (!check_binary(c.kind) || check_ready(t, c.right, rp->trial, rp->bound_by, rp->locals)))
            completes++;
    }
    if (completes > 255) completes = 255;
    return (uint64_t)rank_class << 48 | (uint64_t)(255 - completes) << 40 | (uint64_t)(PLAN_KEYS - keys) << 32 | k;
}

/* Whether pattern k may be met next: no identical pattern of its part written
 * before it is still to be met. */
static bool pattern_eligible(const ProgramView *v, const TcStoredRule *r, const RulePlanning *rp, uint32_t k) {
    uint32_t first = k < r->takes ? 0 : r->takes;
    for (uint32_t j = first; j < k; j++)
        if (!rp->met[j] && pattern_at(v, r, j) == pattern_at(v, r, k)) return false;
    return true;
}

/* Chooses rule r's plan entering by pattern `entry` (none: its patterns) and
 * writes it to the pool: its offset, 0 when memory ran out. */
static uint32_t planner_plan(Planner *pl, const TcTerms *t, const ProgramView *v, const TcStoredRule *r,
                             RulePlanning *rp, const uint32_t *probes, uint32_t entry) {
    uint32_t total = r->takes + r->reads;
    for (uint32_t i = 0; i < rp->locals; i++) rp->bound[i] = false;
    for (uint32_t k = 0; k < total; k++) rp->met[k] = false;
    for (uint32_t i = 0; i < r->check_count; i++) rp->placed[i] = false;
    pl->ops = 0;
    pl->pendings = 0;
    if (!planner_checks(pl, t, v, r, rp, probes)) return 0;   /* checks no pattern binds a local of */
    for (uint32_t step = 0; step < total; step++) {
        uint32_t k = entry;
        if (step > 0 || entry >= total) {
            uint64_t best = UINT64_MAX;
            for (uint32_t j = 0; j < total; j++) {
                if (rp->met[j] || !pattern_eligible(v, r, rp, j)) continue;
                uint64_t rank = pattern_rank(t, v, r, rp, j);
                if (rank < best) { best = rank; k = j; }
            }
        }
        uint32_t probe = planner_probe(pl, t, pattern_at(v, r, k), rp->bound, rp->locals);
        if (probe == UINT32_MAX || !planner_op(pl, PLAN_PROBE, k, probe) || !planner_op(pl, PLAN_UNIFY, k, 0))
            return 0;
        rp->met[k] = true;
        (void)locals_in(t, pattern_at(v, r, k), rp->bound, rp->locals, true);
        if (!planner_checks(pl, t, v, r, rp, probes)) return 0;
    }
    for (uint32_t i = 0; i < r->tells; i++) if (!planner_op(pl, PLAN_TELL, i, 0)) return 0;
    for (uint32_t k = 0; k < r->takes; k++) if (!planner_op(pl, PLAN_TAKE, k, 0)) return 0;
    for (uint32_t m = 0; m < r->makes; m++) if (!planner_op(pl, PLAN_MAKE, m, 0)) return 0;
    for (size_t o = 0; o < pl->ops / 4; o++)   /* the plan's probes, then the plan */
        if (pl->op[4 * o] == PLAN_PROBE) {
            uint32_t at = planner_write_probe(pl, pl->op[4 * o + 2]);
            if (!at) return 0;
            pl->op[4 * o + 2] = at;
        }
    uint32_t at = (uint32_t)pl->words;
    if (!planner_word(pl, (uint32_t)(pl->ops / 4))) return 0;
    for (size_t w = 0; w < pl->ops; w++)
        if (!planner_word(pl, pl->op[w])) return 0;
    return at;
}

/* Writes rule r's plans: its record's offset, 0 when memory ran out. */
static uint32_t planner_rule(Planner *pl, const TcTerms *t, const ProgramView *v, const TcStoredRule *r) {
    uint32_t total = r->takes + r->reads;
    RulePlanning rp = {.locals = r->locals, .patterns = total, .checks = r->check_count};
    size_t locals = r->locals ? r->locals : 1;
    rp.bound = calloc(locals, sizeof *rp.bound);
    rp.bound_by = calloc(locals, sizeof *rp.bound_by);
    rp.trial = calloc(locals, sizeof *rp.trial);
    rp.met = calloc(total ? total : 1, sizeof *rp.met);
    rp.placed = calloc(r->check_count ? r->check_count : 1, sizeof *rp.placed);
    uint32_t *probes = calloc(r->check_count ? r->check_count : 1, sizeof *probes);
    uint32_t at = 0;
    if (!rp.bound || !rp.bound_by || !rp.trial || !rp.met || !rp.placed || !probes) {
        free(rp.bound); free(rp.bound_by); free(rp.trial); free(rp.met); free(rp.placed); free(probes);
        return 0;
    }
    bool ok = true;
    for (uint32_t k = 0; k < total; k++)
        (void)locals_in(t, pattern_at(v, r, k), rp.bound_by, rp.locals, true);
    for (uint32_t i = 0; ok && i < r->check_count; i++) {   /* the probes of the checks that read rows */
        TcCheck c = check_at(v, r, i);
        if (check_binary(c.kind)) continue;
        pl->pendings = 0;
        uint32_t p = planner_probe(pl, t, c.left, rp.bound_by, rp.locals);
        ok = p != UINT32_MAX && (probes[i] = planner_write_probe(pl, p)) != 0;
    }
    if (ok) {   /* the rule's record, its plan offsets filled in as they are written */
        at = (uint32_t)pl->words;
        ok = planner_word(pl, total + 1);
        for (uint32_t e = 0; ok && e <= total; e++) ok = planner_word(pl, 0);
        for (uint32_t i = 0; ok && i < r->check_count; i++)
            ok = planner_word(pl, probes[i]);
    }
    for (uint32_t e = 0; ok && e <= total; e++) {
        uint32_t plan = planner_plan(pl, t, v, r, &rp, probes, e);
        ok = plan != 0;
        planner_put(pl, (size_t)at + 1 + e, plan);
    }
    free(rp.bound); free(rp.bound_by); free(rp.trial); free(rp.met); free(rp.placed); free(probes);
    return ok ? at : 0;
}

/* Whether search a's paths are a strict subset of search b's. */
static bool search_within(const Planner *pl, uint32_t a, uint32_t b) {
    uint32_t na = search_of(pl, a, S_PATHS), nb = search_of(pl, b, S_PATHS);
    if (na >= nb) return false;
    size_t pa = search_of(pl, a, S_FIRST), pb = search_of(pl, b, S_FIRST);
    uint32_t j = 0;
    for (uint32_t i = 0; i < na; i++) {
        while (j < nb && path_word(pl, pb + j) < path_word(pl, pa + i)) j++;
        if (j == nb || path_word(pl, pb + j) != path_word(pl, pa + i)) return false;
    }
    return true;
}

/* The cover of one relation's searches (group[0..n)): a search tries to be
 * followed by a larger one (Kuhn's augmenting paths); before[j], the search
 * j follows, NO_SEARCH none. */
#define NO_SEARCH UINT32_MAX
static bool cover_augment(const Planner *pl, const uint32_t *group, uint32_t n, uint32_t i, uint32_t *before,
                          bool *seen) {
    for (uint32_t j = 0; j < n; j++) {
        if (seen[j] || !search_within(pl, group[i], group[j])) continue;
        seen[j] = true;
        if (before[j] == NO_SEARCH || cover_augment(pl, group, n, before[j], before, seen)) {
            before[j] = i;
            return true;
        }
    }
    return false;
}

/* Whether path `path` is among search s's paths. */
static bool search_has(const Planner *pl, uint32_t s, uint32_t path) {
    size_t at = search_of(pl, s, S_FIRST);
    for (uint32_t j = 0; j < search_of(pl, s, S_PATHS); j++)
        if (path_word(pl, at + j) == path) return true;
    return false;
}

/* Writes the record of the index whose searches are pl->member[first ..
 * first + n), of one split: their relation, its paths, its domain's offset,
 * its levels (a bit for each search's size), then the paths in order: the
 * split first, then each search's paths not in the one before it, those
 * every pattern holds a constant at first; then its domain, the split's
 * constant (the index keeps only the rows holding it there: its searches
 * read no other).  False when memory ran out. */
static bool planner_write_index(Planner *pl, uint32_t first, uint32_t n) {
    uint32_t last = member_word(pl, (size_t)first + n - 1), levels = 0, split = search_of(pl, last, S_SPLIT);
    for (uint32_t k = 0; k < n; k++) levels |= 1u << (search_of(pl, member_word(pl, (size_t)first + k), S_PATHS) - 1);
    uint32_t record = (uint32_t)pl->words;
    uint32_t words[5] = {search_of(pl, last, S_HEAD), search_of(pl, last, S_LENGTH), search_of(pl, last, S_PATHS),
                         0, levels};
    bool ok = true;
    for (int w = 0; ok && w < 5; w++) ok = planner_word(pl, words[w]);
    if (ok && split) ok = planner_word(pl, split);
    for (uint32_t k = 0; ok && k < n; k++) {
        uint32_t s = member_word(pl, (size_t)first + k), prev = k ? member_word(pl, (size_t)first + k - 1) : NO_SEARCH;
        size_t at = search_of(pl, s, S_FIRST);
        uint32_t constants = search_of(pl, s, S_CONSTANTS);
        for (int constant = 1; constant >= 0; constant--)
            for (uint32_t i = 0; ok && i < search_of(pl, s, S_PATHS); i++) {
                uint32_t path = path_word(pl, at + i);
                if ((int)(constants >> i & 1u) != constant || path == split ||
                    (prev != NO_SEARCH && search_has(pl, prev, path)))
                    continue;
                ok = planner_word(pl, path);
            }
    }
    if (ok && split) {
        planner_put(pl, (size_t)record + 3, (uint32_t)pl->words);
        ok = planner_word(pl, 1) && planner_word(pl, search_of(pl, last, S_SPLIT_KEY));
    }
    return ok;
}

/* Chooses the indexes, a minimum chain cover of each relation's searches,
 * and writes them after the plans; each probe gets its index, level and
 * keys.  False when memory ran out. */
static bool planner_indexes(Planner *pl, const TcTerms *t) {
    size_t searches = pl->searches / SEARCH, cap = searches ? searches : 1;
    uint32_t *assign = calloc(2 * cap, sizeof *assign);   /* per search: its index, its level */
    uint32_t *group = malloc(cap * sizeof *group), *before = malloc(cap * sizeof *before);
    uint32_t *after = malloc(cap * sizeof *after);
    bool *seen = malloc(cap * sizeof *seen), *done = calloc(cap, sizeof *done);
    if (!assign || !group || !before || !after || !seen || !done) {
        free(assign); free(group); free(before); free(after); free(seen); free(done);
        return false;
    }
    bool ok = true;
    for (size_t s = 0; ok && s < searches; s++) {   /* each relation's searches together */
        if (done[s]) continue;
        uint32_t n = 0;
        for (size_t o = s; o < searches; o++) {
            bool same = !done[o];   /* one relation, one split */
            for (uint32_t w = S_HEAD; same && w <= S_SPLIT_KEY; w++)
                same = w == S_FIRST || w == S_PATHS || w == S_CONSTANTS ||
                       search_of(pl, (uint32_t)o, w) == search_of(pl, (uint32_t)s, w);
            if (!same) continue;
            done[o] = true;
            group[n++] = (uint32_t)o;
        }
        for (uint32_t j = 0; j < n; j++) { before[j] = NO_SEARCH; after[j] = NO_SEARCH; }
        for (uint32_t i = 0; i < n; i++) {
            for (uint32_t j = 0; j < n; j++) seen[j] = false;
            (void)cover_augment(pl, group, n, i, before, seen);
        }
        for (uint32_t j = 0; j < n; j++) if (before[j] != NO_SEARCH) after[before[j]] = j;
        for (uint32_t i = 0; ok && i < n; i++) {
            if (before[i] != NO_SEARCH) continue;   /* a chain starts where no smaller search precedes */
            uint32_t index = (uint32_t)(pl->chains / 2), first = (uint32_t)pl->members, count = 0;
            for (uint32_t k = i; ok && k != NO_SEARCH; k = after[k], count++) {
                assign[2 * (size_t)group[k]] = index;
                assign[2 * (size_t)group[k] + 1] = search_of(pl, group[k], S_PATHS);
                ok = tc_push_u32(realloc, &pl->member, &pl->members, &pl->member_cap, group[k]);
            }
            ok = ok && tc_push_u32(realloc, &pl->chain, &pl->chains, &pl->chain_cap, first) &&
                 tc_push_u32(realloc, &pl->chain, &pl->chains, &pl->chain_cap, count);
        }
    }
    uint32_t indexes = (uint32_t)(pl->chains / 2), at = (uint32_t)pl->words;
    ok = ok && planner_word(pl, indexes);
    for (uint32_t c = 0; ok && c < indexes; c++) ok = planner_word(pl, 0);
    for (uint32_t c = 0; ok && c < indexes; c++) {
        ok = pl->word && pl->chain;
        if (!ok) break;
        pl->word[(size_t)at + 1 + c] = (uint32_t)pl->words;
        ok = planner_write_index(pl, pl->chain[2 * (size_t)c], pl->chain[2 * (size_t)c + 1]);
    }
    ok = ok && pl->word;
    if (ok && pl->word) pl->word[1] = at;
    /* each probe: its index, level and keys */
    for (size_t p = 0; ok && pl->word && pl->probe && p < pl->probes / 2; p++) {
        uint32_t probe = pl->probe[2 * p], s = pl->probe[2 * p + 1] - 1;
        uint32_t index = assign[2 * (size_t)s], level = assign[2 * (size_t)s + 1];
        uint32_t record = pl->word[(size_t)at + 1 + index];
        TcTerm term = pl->word[probe];
        pl->word[(size_t)probe + 3] = index;
        pl->word[(size_t)probe + 4] = level;
        for (uint32_t l = 0; l < level; l++) {
            TcTerm key = 0;
            (void)path_key(t, term, pl->word[(size_t)record + 5 + l], &key);
            pl->word[(size_t)probe + 5 + l] = key;
        }
    }
    free(assign); free(group); free(before); free(after); free(seen); free(done);
    return ok;
}

/* Writes the plans of the program's rules into pl's pool: its header, each
 * rule's plans, the indexes.  False when memory ran out. */
static bool planner_program(Planner *pl, const TcProgram *p) {
    /* the rules as the planner reads them: no plans yet */
    ProgramView v = {p->rule, p->rules, p->term, p->terms_len, p->check, p->checks_len, p->name, NULL, 0, NULL, 0};
    bool ok = planner_word(pl, (uint32_t)p->rules) && planner_word(pl, 0);
    for (size_t r = 0; ok && r < p->rules; r++) ok = planner_word(pl, 0);
    for (uint32_t r = 0; ok && r < p->rules; r++) {
        TcStoredRule rule = rule_at(&v, r);
        if (rule.same != r) continue;   /* that rule again: planned there */
        uint32_t at = planner_rule(pl, p->terms, &v, &rule);
        ok = at != 0;
        planner_put(pl, 2 + (size_t)r, at);
    }
    return ok && planner_indexes(pl, p->terms);
}

/* The program's plans, chosen when it is first explored and again when rules
 * were added since.  False when memory ran out (the program then has none). */
static bool plans_prepare(TcProgram *p) {
    if (p->plan_len && p->plan && p->plan[0] == p->rules) return true;
    Planner pl = {0};
    if (!planner_program(&pl, p) || !pl.word) { planner_free(&pl); return false; }
    uint32_t *plan = malloc(pl.words * sizeof *plan);
    if (!plan) { planner_free(&pl); return false; }
    memcpy(plan, pl.word, pl.words * sizeof *plan);
    free(p->plan);
    p->plan = plan;
    p->plan_len = p->plan_cap = pl.words;
    planner_free(&pl);
    return true;
}

const uint32_t *tc_program_plans(TcProgram *program, size_t *words) {
    bool planned = plans_prepare(program);
    *words = planned ? program->plan_len : 0;
    return planned && program->plan_len ? program->plan : no_items;
}

/* Completed values: which ground applications of typed constructors are
 * their own values.
 *
 * Evaluating V = (c v1 .. vn), where c's one type is a ground arrow and no
 * equation applies at V, checks the arguments against the parameters,
 * evaluates each argument, finds no equation, and answers [V] with the
 * bindings it was asked under.  When every argument is a symbol, a scalar,
 * a value this index already holds, or held unevaluated by an Atom
 * parameter, nothing in that evaluation can run an effect or answer twice,
 * and under the same program it answers the same again: V is a completed
 * value.  A value shares subterms, so without this a DAG of such values is
 * evaluated once per path to each node.
 *
 * A certificate is issued where a typed call's single delivery publishes its
 * own source (no equation, every argument its own value) and consulted where
 * an occurrence is about to be interpreted under an expected type the
 * evaluation does not depend on: none, %Undefined%, or the head's range.
 *
 * What the answer depends on, and how each is kept:
 *   - the program (equations, declarations, subtypes, imports): the space's
 *     program token; a change clears the index;
 *   - the occurrence's address, which a reset or a collection frees for
 *     reuse: each certificate records its arena's identity and reset epoch
 *     (the hash-cons reclamation epoch for atoms no arena owns), and only
 *     arenas whose epochs this index can read are certified: the run's, the
 *     collector's survivors, and the term universe of the run's space;
 *   - how evaluation runs: the index is off under fuel, step budgets,
 *     dependent telescopes, the bare-minimal interpreter and languages other
 *     than HE.  A foreign callable is a grounded atom, never a symbol, so a
 *     constructor's head cannot become one.
 * It lives for one outermost canonical run.  CETTA_HE_COMPLETED_DELIVERY=0
 * turns it off. */

typedef struct {
    const Atom *atom;
    const Atom *range;
    uint64_t epoch;
    uint32_t arena;
} HeValueCertificate;

typedef enum {
    HE_VALUE_HEAD_UNKNOWN = 0,
    /* One ground arrow, every parameter %Undefined%, Atom or a ground
     * non-meta type. */
    HE_VALUE_HEAD_TYPED,
    HE_VALUE_HEAD_OTHER,
} HeValueHeadState;

typedef struct {
    SymbolId head;
    CettaExprLen arity;
    uint8_t state;
    Atom *type;
} HeValueHeadClass;

typedef struct {
    HeValueCertificate *slots;
    size_t cap;
    size_t len;
    HeValueHeadClass *heads;
    size_t head_cap;
    size_t head_len;
    Arena types;
    bool types_ready;
    const Space *space;
    SpaceProgramToken program;
    const Arena *arena;
    bool open;
} HeValueIndex;

/* A run that completes more values than this starts its certificates over:
 * the newest are the likeliest to be met again. */
#define HE_VALUE_INDEX_MAX_CERTIFICATES ((size_t)1u << 18)

static __thread HeValueIndex g_he_value_index;

static bool he_value_index_enabled(void) {
    static _Thread_local int enabled = -1;
    if (enabled < 0) {
        const char *setting = getenv("CETTA_HE_COMPLETED_DELIVERY");
        enabled = !setting || strcmp(setting, "0") != 0;
    }
    return enabled;
}

static void he_value_index_forget(void) {
    HeValueIndex *index = &g_he_value_index;
    free(index->slots);
    free(index->heads);
    if (index->types_ready)
        arena_free(&index->types);
    index->slots = NULL;
    index->heads = NULL;
    index->cap = index->len = 0u;
    index->head_cap = index->head_len = 0u;
    index->types_ready = false;
}

static void he_value_index_open(const Arena *arena) {
    he_value_index_forget();
    g_he_value_index.space = NULL;
    g_he_value_index.arena = arena;
    g_he_value_index.open = true;
}

static void he_value_index_close(void) {
    he_value_index_forget();
    g_he_value_index.space = NULL;
    g_he_value_index.arena = NULL;
    g_he_value_index.open = false;
}

/* Whether certificates may be read or issued for an evaluation in `s` with
 * `fuel`, binding the index to `s`'s current program first. */
static bool he_value_index_usable(Space *s, int fuel) {
    HeValueIndex *index = &g_he_value_index;
    if (!index->open || fuel >= 0 || !s || !he_value_index_enabled() ||
        eval_current_language_id() != CETTA_LANGUAGE_HE ||
        (g_eval_completion_tracker &&
         g_eval_completion_tracker->budget_limited) ||
        cetta_evaluator_options_is_bare_minimal(active_eval_options()) ||
        eval_dependent_telescope_enabled())
        return false;
    if (index->space != s) {
        /* One program per run: what is known of another stays. */
        if (index->len || index->head_len)
            return false;
        index->space = s;
        index->program = space_program_token(s);
    } else if (!space_program_token_is_current(index->program)) {
        he_value_index_forget();
        index->program = space_program_token(s);
    }
    return true;
}

/* The identity and current epoch of the arena that owns `atom`, where the
 * index can read them. */
static bool he_value_arena_epoch(const Atom *atom, uint32_t *arena,
                                 uint64_t *epoch) {
    const Arena *run = g_he_value_index.arena;
    *arena = atom->arena_id;
    if (atom->arena_id == 0u) {
        *epoch = hashcons_reclamation_epoch();
        return *epoch != UINT64_MAX;
    }
    if (run && atom->arena_id == run->identity) {
        *epoch = run->reset_epoch;
        return true;
    }
    if (atom->arena_id == g_eval_gc.survivor.identity) {
        *epoch = g_eval_gc.survivor.reset_epoch;
        return true;
    }
    /* What a match decodes from the space lives in its term universe. */
    const Space *space = g_he_value_index.space;
    const Arena *stored = space && space->native.universe
        ? space->native.universe->persistent_arena : NULL;
    if (stored && atom->arena_id == stored->identity) {
        *epoch = stored->reset_epoch;
        return true;
    }
    return false;
}

static size_t he_value_slot(const Atom *atom, size_t cap) {
    uintptr_t key = (uintptr_t)atom;
    key ^= key >> 33u;
    key *= (uintptr_t)UINT64_C(0xff51afd7ed558ccd);
    key ^= key >> 29u;
    return (size_t)key & (cap - 1u);
}

/* The slot holding `atom`'s certificate, current or stale, or NULL. */
static HeValueCertificate *he_value_slot_of(const Atom *atom) {
    HeValueIndex *index = &g_he_value_index;
    if (!index->cap)
        return NULL;
    size_t slot = he_value_slot(atom, index->cap);
    while (index->slots[slot].atom) {
        if (index->slots[slot].atom == atom)
            return &index->slots[slot];
        slot = (slot + 1u) & (index->cap - 1u);
    }
    return NULL;
}

/* `atom`'s certificate, if it holds for the atom now at that address. */
static const HeValueCertificate *he_value_find(const Atom *atom) {
    HeValueCertificate *held = he_value_slot_of(atom);
    if (!held)
        return NULL;
    uint32_t arena = 0u;
    uint64_t epoch = 0u;
    if (!he_value_arena_epoch(atom, &arena, &epoch) ||
        arena != held->arena || epoch != held->epoch)
        return NULL;
    return held;
}

static bool he_value_grow(void) {
    HeValueIndex *index = &g_he_value_index;
    size_t cap = index->cap ? index->cap * 2u : 256u;
    HeValueCertificate *slots = calloc(cap, sizeof(*slots));
    if (!slots)
        return false;
    for (size_t i = 0u; i < index->cap; i++) {
        if (!index->slots[i].atom)
            continue;
        size_t slot = he_value_slot(index->slots[i].atom, cap);
        while (slots[slot].atom)
            slot = (slot + 1u) & (cap - 1u);
        slots[slot] = index->slots[i];
    }
    free(index->slots);
    index->slots = slots;
    index->cap = cap;
    return true;
}

static void he_value_insert(const HeValueCertificate *certificate) {
    HeValueIndex *index = &g_he_value_index;
    HeValueCertificate *stale = he_value_slot_of(certificate->atom);
    if (stale) {
        *stale = *certificate;
        return;
    }
    if (index->len >= HE_VALUE_INDEX_MAX_CERTIFICATES) {
        memset(index->slots, 0, index->cap * sizeof(*index->slots));
        index->len = 0u;
    }
    if ((index->len + 1u) * 2u > index->cap && !he_value_grow())
        return;
    size_t slot = he_value_slot(certificate->atom, index->cap);
    while (index->slots[slot].atom)
        slot = (slot + 1u) & (index->cap - 1u);
    index->slots[slot] = *certificate;
    index->len++;
}

static bool he_value_parameter_admitted(Atom *parameter) {
    return atom_is_symbol_id(parameter, g_builtin_syms.undefined_type) ||
        atom_is_symbol_id(parameter, g_builtin_syms.atom) ||
        (!atom_has_vars(parameter) && !atom_is_meta_type(parameter));
}

static size_t he_value_head_slot(SymbolId head, CettaExprLen arity,
                                 size_t cap) {
    uint64_t key = ((uint64_t)head << 32u) ^ (uint64_t)arity;
    key ^= key >> 33u;
    key *= UINT64_C(0xff51afd7ed558ccd);
    key ^= key >> 29u;
    return (size_t)key & (cap - 1u);
}

/* Room for one more head class; false where the table cannot grow. */
static bool he_value_heads_reserve(void) {
    HeValueIndex *index = &g_he_value_index;
    if ((index->head_len + 1u) * 2u <= index->head_cap)
        return true;
    size_t cap = index->head_cap ? index->head_cap * 2u : 32u;
    HeValueHeadClass *heads = calloc(cap, sizeof(*heads));
    if (!heads)
        return false;
    for (size_t i = 0u; i < index->head_cap; i++) {
        if (index->heads[i].state == HE_VALUE_HEAD_UNKNOWN)
            continue;
        size_t slot = he_value_head_slot(index->heads[i].head,
                                         index->heads[i].arity, cap);
        while (heads[slot].state != HE_VALUE_HEAD_UNKNOWN)
            slot = (slot + 1u) & (cap - 1u);
        heads[slot] = index->heads[i];
    }
    free(index->heads);
    index->heads = heads;
    index->head_cap = cap;
    return true;
}

/* What the program says of `head` at `arity`, computed once per program. */
static const HeValueHeadClass *he_value_head_class(Space *s, Arena *a,
                                                   Atom *head,
                                                   CettaExprLen arity) {
    HeValueIndex *index = &g_he_value_index;
    if (!head || head->kind != ATOM_SYMBOL)
        return NULL;
    if (index->head_cap) {
        size_t slot = he_value_head_slot(head->sym_id, arity, index->head_cap);
        while (index->heads[slot].state != HE_VALUE_HEAD_UNKNOWN) {
            if (index->heads[slot].head == head->sym_id &&
                index->heads[slot].arity == arity)
                return &index->heads[slot];
            slot = (slot + 1u) & (index->head_cap - 1u);
        }
    }
    if (!he_value_heads_reserve())
        return NULL;
    HeValueHeadClass class = {
        .head = head->sym_id, .arity = arity, .state = HE_VALUE_HEAD_OTHER,
    };
    if (!symbol_id_is_builtin(head->sym_id) && !is_grounded_op(head->sym_id) &&
        !atom_has_registry_refs(head)) {
        Atom **types = NULL;
        uint32_t count = eval_get_atom_types_profiled(s, a, head, &types);
        Atom *type = count == 1u ? types[0] : NULL;
        bool typed = type && is_function_type(type) && !atom_has_vars(type) &&
            type->expr.len == arity + 2u;
        for (CettaExprIndex i = 1u; typed && i <= arity; i++)
            typed = he_value_parameter_admitted(type->expr.elems[i]);
        if (typed) {
            if (!index->types_ready) {
                arena_init(&index->types);
                arena_set_hashcons(&index->types, NULL);
                arena_set_runtime_kind(&index->types,
                                       CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
                index->types_ready = true;
            }
            class.type = atom_deep_copy(&index->types, type);
            class.state = HE_VALUE_HEAD_TYPED;
        }
        free(types);
    }
    size_t slot = he_value_head_slot(head->sym_id, arity, index->head_cap);
    while (index->heads[slot].state != HE_VALUE_HEAD_UNKNOWN)
        slot = (slot + 1u) & (index->head_cap - 1u);
    index->heads[slot] = class;
    index->head_len++;
    return &index->heads[slot];
}

/* A symbol or a scalar: evaluated under any parameter it was admitted to,
 * it is itself, and it runs nothing. */
static bool he_value_argument_immediate(const Atom *atom) {
    if (atom_has_registry_refs(atom))
        return false;
    if (atom->kind == ATOM_SYMBOL)
        return true;
    if (atom->kind != ATOM_GROUNDED)
        return false;
    switch (atom->ground.gkind) {
    case GV_INT:
    case GV_FLOAT:
    case GV_BOOL:
    case GV_STRING:
    case GV_BIGINT:
    case GV_RATIONAL:
        return true;
    default:
        return false;
    }
}

/* Issue: `value`, a typed call's own source, was just published by that
 * call's single delivery with no equation applying. */
static void he_value_certify(Space *s, Arena *a, int fuel, Atom *value) {
    if (!value || value->kind != ATOM_EXPR || !value->expr.len ||
        atom_has_vars(value) || atom_has_registry_refs(value) ||
        (value->flags & ATOM_FLAG_HAS_THREAD_LOCAL_RESOURCE) ||
        !he_value_index_usable(s, fuel) || he_value_find(value))
        return;
    HeValueCertificate certificate = {.atom = value};
    if (!he_value_arena_epoch(value, &certificate.arena, &certificate.epoch))
        return;
    CettaExprLen arity = value->expr.len - 1u;
    const HeValueHeadClass *head =
        he_value_head_class(s, a, value->expr.elems[0], arity);
    if (!head || head->state != HE_VALUE_HEAD_TYPED)
        return;
    for (CettaExprIndex i = 1u; i <= arity; i++) {
        Atom *parameter = head->type->expr.elems[i];
        Atom *argument = value->expr.elems[i];
        if (atom_is_symbol_id(parameter, g_builtin_syms.atom) ||
            he_value_argument_immediate(argument))
            continue;
        const HeValueCertificate *held = he_value_find(argument);
        if (!held ||
            (!atom_is_symbol_id(parameter, g_builtin_syms.undefined_type) &&
             !atom_eq((Atom *)held->range, parameter)))
            return;
    }
    certificate.range = head->type->expr.elems[arity + 1u];
    he_value_insert(&certificate);
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_HE_COMPLETED_VALUE_CERTIFIED);
}

/* Consult: whether interpreting `atom` in `s` under `type` (NULL when the
 * caller asks for none) answers exactly `atom`, its bindings unchanged. */
static bool he_value_certified(Space *s, int fuel, Atom *atom, Atom *type) {
    if (!g_he_value_index.len || !atom || atom->kind != ATOM_EXPR ||
        atom_has_vars(atom) || !he_value_index_usable(s, fuel))
        return false;
    const HeValueCertificate *held = he_value_find(atom);
    if (!held ||
        (type && !atom_is_symbol_id(type, g_builtin_syms.undefined_type) &&
         !atom_eq(type, (Atom *)held->range)))
        return false;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_HE_COMPLETED_VALUE_DELIVERED);
    return true;
}

/* Whether `value` is `own` marked as an evaluated value: the same
 * expression over the same child atoms (atom_he_evaluated_copy). */
static bool he_value_marked_copy_of(const Atom *value, const Atom *own) {
    if (!atom_is_he_evaluated(value) || own->kind != ATOM_EXPR ||
        value->expr.len != own->expr.len)
        return false;
    for (CettaExprIndex i = 0u; i < value->expr.len; i++)
        if (value->expr.elems[i] != own->expr.elems[i])
            return false;
    return true;
}

/* Whether `built`, an expression rebuilt from evaluated parts, is `source`
 * part for part: the same atoms, or symbols and scalars equal to its own.
 * With `marked`, a part may also be its own atom marked as an evaluated
 * value: the same value, though not the same occurrence. */
static bool he_value_parts_match(const Atom *built, const Atom *source,
                                 bool marked) {
    if (built == source)
        return true;
    if (!built || !source || built->kind != ATOM_EXPR ||
        source->kind != ATOM_EXPR || built->expr.len != source->expr.len)
        return false;
    for (CettaExprIndex i = 0u; i < built->expr.len; i++) {
        Atom *part = built->expr.elems[i];
        Atom *own = source->expr.elems[i];
        if (part == own || (marked && he_value_marked_copy_of(part, own)))
            continue;
        if (!part || !own || part->kind != own->kind ||
            !he_value_argument_immediate(part) ||
            !he_value_argument_immediate(own) || !atom_eq(part, own))
            return false;
    }
    return true;
}

/* The source itself can stand for the rebuilt call: its parts are the
 * call's parts, so publishing it keeps the sharing it was built with. */
static bool he_value_same_parts(const Atom *built, const Atom *source) {
    return he_value_parts_match(built, source, false);
}

/* The rebuilt call is the source as a value: each part is its own atom or
 * that atom marked as evaluated.  The source is then its own value. */
static bool he_value_same_value(const Atom *built, const Atom *source) {
    return he_value_parts_match(built, source, true);
}

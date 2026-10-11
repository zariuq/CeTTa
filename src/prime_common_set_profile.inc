/* The material schema compiler of the common profile. The source grammar
 * and logical compilation are specified in CommonCoreSyntax and
 * CommonCoreLogicalCompilation; native correspondence is qualified separately. */

typedef enum {
    SJ_COMMON_OK = 0,
    SJ_COMMON_INVALID,
    SJ_COMMON_BUDGET,
    SJ_COMMON_DEPTH,
    SJ_COMMON_STORAGE
} SjCommonFailure;

typedef struct {
    CettaPrimeRegularKernelBudget *budget;
    SjCommonFailure failure;
} SjCommonExpansion;

static bool sj_common_stop(SjCommonExpansion *work, SjCommonFailure failure) {
    if (work->failure == SJ_COMMON_OK) work->failure = failure;
    return false;
}

/* Construction spends from the request and every enclosing budget, just as
 * the shared level arithmetic does. Exhaustion is incomplete computation. */
static bool sj_common_spend(SjCommonExpansion *work, uint64_t amount) {
    if (work->failure != SJ_COMMON_OK) return false;
    bool enough = true;
    for (CettaPrimeRegularKernelBudget *budget = work->budget; budget;
         budget = budget->within) {
        if (!budget->limited) continue;
        uint64_t taken = amount;
        if (taken > budget->remaining) {
            taken = budget->remaining;
            enough = false;
        }
        budget->remaining -= taken;
        budget->spent = budget->spent > UINT64_MAX - taken
            ? UINT64_MAX : budget->spent + taken;
    }
    return enough || sj_common_stop(work, SJ_COMMON_BUDGET);
}

static bool sj_common_profile(SjTheoryProfile profile) {
    return profile == SJ_PROFILE_PRIME_COMMON ||
           profile == SJ_PROFILE_FOUNDATION_COMMON ||
           profile == SJ_PROFILE_CLASSICAL_COMMON ||
           profile == SJ_PROFILE_CLASSICAL_FOUNDATION_COMMON;
}

static bool sj_common_schema_name(Atom *name) {
    return name && name->kind == ATOM_EXPR && name->expr.len > 0u &&
           (atom_is_symbol(name->expr.elems[0], "boundedSeparation") ||
            atom_is_symbol(name->expr.elems[0], "strongCollection") ||
            atom_is_symbol(name->expr.elems[0], "subsetCollection"));
}

static bool sj_common_natural(Atom *atom, size_t *value) {
    if (!atom || atom->kind != ATOM_GROUNDED || atom->ground.gkind != GV_INT ||
        atom->ground.ival < 0 || (uint64_t)atom->ground.ival > SIZE_MAX)
        return false;
    *value = (size_t)atom->ground.ival;
    return true;
}

typedef struct {
    Atom *source;
    size_t count;
    size_t remaining_depth;
} SjCommonPending;

static bool sj_common_push(SjCommonPending **pending, size_t *length,
                            size_t *capacity, Atom *source, size_t count,
                            size_t remaining_depth, size_t used_depth,
                            SjCommonExpansion *work) {
    if (remaining_depth < used_depth)
        return sj_common_stop(work, SJ_COMMON_DEPTH);
    if (*length == *capacity) {
        size_t next = *capacity ? *capacity * 2u : 16u;
        if (next < *capacity || next > SIZE_MAX / sizeof(**pending))
            return sj_common_stop(work, SJ_COMMON_STORAGE);
        SjCommonPending *grown = realloc(*pending, next * sizeof(*grown));
        if (!grown) return sj_common_stop(work, SJ_COMMON_STORAGE);
        *pending = grown;
        *capacity = next;
    }
    (*pending)[(*length)++] = (SjCommonPending){source, count, remaining_depth - used_depth};
    return true;
}

/* Check the entire authored grammar and the expanded native depth without
 * recursive traversal or constructing the parameter prefix. The three schema
 * wrappers add at most 21, 28 and 43 expression levels respectively. A free
 * parameter adds `all` and `lam`; the retained known record adds one level.
 * These are bounds for the current serialization, not restrictions on sets
 * or on the mathematical schema. The bound is the reader's own allowance. */
static bool sj_common_preflight(Atom *source, size_t count, size_t parameters,
                                bool bounded, bool strong, SjCommonExpansion *work) {
    if (!sj_common_spend(work, (uint64_t)parameters)) return false;
    size_t limit = parser_supported_expression_depth();
    size_t wrapper = bounded ? 21u : strong ? 28u : 43u;
    if (limit <= wrapper || parameters > (limit - wrapper - 1u) / 2u)
        return sj_common_stop(work, SJ_COMMON_DEPTH);
    size_t available = limit - wrapper - 1u - 2u * parameters;
    SjCommonPending *pending = NULL;
    size_t length = 0u, capacity = 0u;
    bool ok = sj_common_push(&pending, &length, &capacity,
                              source, count, available, 0u, work);
    while (ok && length > 0u) {
        SjCommonPending item = pending[--length];
        Atom *term = item.source;
        if (!sj_common_spend(work, 1u)) { ok = false; break; }
        if (sj_is_expr(term, "set:bottom", 1u)) {
            if (item.remaining_depth < 2u) ok = sj_common_stop(work, SJ_COMMON_DEPTH);
            continue;
        }
        if (sj_is_expr(term, "set:eq", 3u) || sj_is_expr(term, "set:mem", 3u)) {
            size_t first, second;
            if (!sj_common_natural(term->expr.elems[1], &first) || first >= item.count ||
                !sj_common_natural(term->expr.elems[2], &second) || second >= item.count)
                ok = sj_common_stop(work, SJ_COMMON_INVALID);
            else if (item.remaining_depth < 1u) ok = sj_common_stop(work, SJ_COMMON_DEPTH);
            continue;
        }
        bool implication = sj_is_expr(term, "set:imp", 3u);
        if (implication || sj_is_expr(term, "set:and", 3u) || sj_is_expr(term, "set:or", 3u)) {
            /* Church conjunction and disjunction have different depths on
             * their first and second inputs; implication adds one on both. */
            ok = sj_common_push(&pending, &length, &capacity,
                term->expr.elems[2], item.count, item.remaining_depth,
                implication ? 1u : 5u, work) &&
                 sj_common_push(&pending, &length, &capacity,
                term->expr.elems[1], item.count, item.remaining_depth,
                implication ? 1u : 4u, work);
            continue;
        }
        if (bounded && (sj_is_expr(term, "set:all-in", 3u) ||
                         sj_is_expr(term, "set:exists-in", 3u))) {
            size_t parent;
            if (!sj_common_natural(term->expr.elems[1], &parent) || parent >= item.count)
                ok = sj_common_stop(work, SJ_COMMON_INVALID);
            else if (item.count >= INT64_MAX) ok = sj_common_stop(work, SJ_COMMON_STORAGE);
            else ok = sj_common_push(&pending, &length, &capacity,
                term->expr.elems[2], item.count + 1u, item.remaining_depth,
                atom_is_symbol(term->expr.elems[0], "set:all-in") ? 3u : 11u, work);
            continue;
        }
        if (!bounded && (sj_is_expr(term, "set:all", 2u) ||
                          sj_is_expr(term, "set:exists", 2u))) {
            if (item.count >= INT64_MAX) ok = sj_common_stop(work, SJ_COMMON_STORAGE);
            else ok = sj_common_push(&pending, &length, &capacity,
                term->expr.elems[1], item.count + 1u, item.remaining_depth,
                atom_is_symbol(term->expr.elems[0], "set:all") ? 2u : 6u, work);
            continue;
        }
        ok = sj_common_stop(work, SJ_COMMON_INVALID);
    }
    free(pending);
    return ok;
}

static Atom *sj_common_index(Arena *a, size_t index) {
    return index <= INT64_MAX ? atom_int(a, (int64_t)index) : NULL;
}

static Atom *sj_common_atom(Arena *a, const char *head, size_t first, size_t second) {
    Atom *x = sj_common_index(a, first);
    Atom *y = sj_common_index(a, second);
    return x && y ? sj_expr3(a, head, x, y) : NULL;
}

static Atom *sj_common_binary(Arena *a, const char *head, Atom *first, Atom *second) {
    return first && second ? sj_expr3(a, head, first, second) : NULL;
}

static Atom *sj_common_quantifier(Arena *a, const char *head, Atom *body) {
    return body ? sj_expr2(a, head, body) : NULL;
}

/* Decode the declared first-order grammar, or its stricter bounded
 * subgrammar. Every variable index is checked at its actual binder depth.
 * In particular there is no arbitrary-predicate Separation branch. */
static Atom *sj_common_decode(Arena *a, Atom *source, size_t count, bool bounded,
                               SjCommonExpansion *work) {
    if (!sj_common_spend(work, 1u)) return NULL;
    if (sj_is_expr(source, "set:bottom", 1u)) return sj_expr1(a, "set:bottom");
    if (sj_is_expr(source, "set:eq", 3u) || sj_is_expr(source, "set:mem", 3u)) {
        size_t first, second;
        if (!sj_common_natural(source->expr.elems[1], &first) || first >= count ||
            !sj_common_natural(source->expr.elems[2], &second) || second >= count)
            return NULL;
        return sj_common_atom(a, atom_is_symbol(source->expr.elems[0], "set:eq")
                                ? "set:eq" : "set:mem", first, second);
    }
    const char *binary = sj_is_expr(source, "set:and", 3u) ? "set:and" :
                         sj_is_expr(source, "set:or", 3u) ? "set:or" :
                         sj_is_expr(source, "set:imp", 3u) ? "set:imp" : NULL;
    if (binary)
        return sj_common_binary(a, binary,
            sj_common_decode(a, source->expr.elems[1], count, bounded, work),
            sj_common_decode(a, source->expr.elems[2], count, bounded, work));
    if (count >= INT64_MAX) return NULL;
    if (bounded && (sj_is_expr(source, "set:all-in", 3u) ||
                    sj_is_expr(source, "set:exists-in", 3u))) {
        size_t parent;
        if (!sj_common_natural(source->expr.elems[1], &parent) || parent >= count)
            return NULL;
        bool all = atom_is_symbol(source->expr.elems[0], "set:all-in");
        Atom *guard = sj_common_atom(a, "set:mem", 0u, parent + 1u);
        Atom *body = sj_common_decode(a, source->expr.elems[2], count + 1u, true, work);
        return sj_common_quantifier(a, all ? "set:all" : "set:exists",
            sj_common_binary(a, all ? "set:imp" : "set:and", guard, body));
    }
    if (!bounded && (sj_is_expr(source, "set:all", 2u) ||
                     sj_is_expr(source, "set:exists", 2u)))
        return sj_common_quantifier(a,
            atom_is_symbol(source->expr.elems[0], "set:all") ? "set:all" : "set:exists",
            sj_common_decode(a, source->expr.elems[1], count + 1u, false, work));
    return NULL;
}

/* Rename the distinguished schema slots and insert the outer set slots.
 * Bound variables stay fixed, and the renaming lifts under every binder. */
static Atom *sj_common_substitute(Arena *a, Atom *body, size_t depth,
                                  const size_t *slots, size_t retained, size_t shift,
                                  SjCommonExpansion *work) {
    if (!sj_common_spend(work, 1u)) return NULL;
    if (sj_is_expr(body, "set:bottom", 1u)) return body;
    if (sj_is_expr(body, "set:eq", 3u) || sj_is_expr(body, "set:mem", 3u)) {
        size_t mapped[2];
        for (size_t i = 0u; i < 2u; i++) {
            size_t index;
            if (!sj_common_natural(body->expr.elems[i + 1u], &index)) return NULL;
            if (index < depth) mapped[i] = index;
            else {
                size_t free = index - depth;
                if (free >= retained && free > SIZE_MAX - shift) return NULL;
                size_t changed = free < retained ? slots[free] : free + shift;
                if (changed > SIZE_MAX - depth) return NULL;
                mapped[i] = depth + changed;
            }
        }
        return sj_common_atom(a, atom_is_symbol(body->expr.elems[0], "set:eq")
                                ? "set:eq" : "set:mem", mapped[0], mapped[1]);
    }
    if (sj_is_expr(body, "set:all", 2u) || sj_is_expr(body, "set:exists", 2u)) {
        if (depth == SIZE_MAX) return NULL;
        return sj_common_quantifier(a,
            atom_is_symbol(body->expr.elems[0], "set:all") ? "set:all" : "set:exists",
            sj_common_substitute(a, body->expr.elems[1], depth + 1u, slots, retained, shift, work));
    }
    const char *head = sj_is_expr(body, "set:and", 3u) ? "set:and" :
                       sj_is_expr(body, "set:or", 3u) ? "set:or" :
                       sj_is_expr(body, "set:imp", 3u) ? "set:imp" : NULL;
    return head ? sj_common_binary(a, head,
        sj_common_substitute(a, body->expr.elems[1], depth, slots, retained, shift, work),
        sj_common_substitute(a, body->expr.elems[2], depth, slots, retained, shift, work)) : NULL;
}

static Atom *sj_common_equivalent(Arena *a, Atom *first, Atom *second) {
    return sj_common_binary(a, "set:and",
        sj_common_binary(a, "set:imp", first, second),
        sj_common_binary(a, "set:imp", second, first));
}

static Atom *sj_common_separation(Arena *a, Atom *body, SjCommonExpansion *work) {
    if (!sj_common_spend(work, 1u)) return NULL;
    const size_t slots[] = {0u};
    Atom *selected = sj_common_binary(a, "set:and",
        sj_common_atom(a, "set:mem", 0u, 2u),
        sj_common_substitute(a, body, 0u, slots, 1u, 2u, work));
    return sj_common_quantifier(a, "set:all", sj_common_quantifier(a, "set:exists",
        sj_common_quantifier(a, "set:all", sj_common_equivalent(a,
            sj_common_atom(a, "set:mem", 0u, 1u), selected))));
}

static Atom *sj_common_strong_collection(Arena *a, Atom *body, SjCommonExpansion *work) {
    if (!sj_common_spend(work, 1u)) return NULL;
    const size_t same[] = {0u, 1u};
    const size_t swap[] = {1u, 0u};
    Atom *premise = sj_common_quantifier(a, "set:all", sj_common_binary(a, "set:imp",
        sj_common_atom(a, "set:mem", 0u, 1u), sj_common_quantifier(a, "set:exists",
            sj_common_substitute(a, body, 0u, same, 2u, 1u, work))));
    Atom *forward = sj_common_quantifier(a, "set:all", sj_common_binary(a, "set:imp",
        sj_common_atom(a, "set:mem", 0u, 2u), sj_common_quantifier(a, "set:exists",
            sj_common_binary(a, "set:and", sj_common_atom(a, "set:mem", 0u, 2u),
                sj_common_substitute(a, body, 0u, same, 2u, 2u, work)))));
    Atom *backward = sj_common_quantifier(a, "set:all", sj_common_binary(a, "set:imp",
        sj_common_atom(a, "set:mem", 0u, 1u), sj_common_quantifier(a, "set:exists",
            sj_common_binary(a, "set:and", sj_common_atom(a, "set:mem", 0u, 3u),
                sj_common_substitute(a, body, 0u, swap, 2u, 2u, work)))));
    return sj_common_quantifier(a, "set:all", sj_common_binary(a, "set:imp", premise,
        sj_common_quantifier(a, "set:exists", sj_common_binary(a, "set:and", forward, backward))));
}

static Atom *sj_common_subset_collection(Arena *a, Atom *body, SjCommonExpansion *work) {
    if (!sj_common_spend(work, 1u)) return NULL;
    const size_t premiseSlots[] = {0u, 1u, 2u};
    const size_t forwardSlots[] = {0u, 1u, 3u};
    const size_t backwardSlots[] = {1u, 0u, 3u};
    Atom *premise = sj_common_quantifier(a, "set:all", sj_common_binary(a, "set:imp",
        sj_common_atom(a, "set:mem", 0u, 4u), sj_common_quantifier(a, "set:exists",
            sj_common_binary(a, "set:and", sj_common_atom(a, "set:mem", 0u, 4u),
                sj_common_substitute(a, body, 0u, premiseSlots, 3u, 3u, work)))));
    Atom *forward = sj_common_quantifier(a, "set:all", sj_common_binary(a, "set:imp",
        sj_common_atom(a, "set:mem", 0u, 5u), sj_common_quantifier(a, "set:exists",
            sj_common_binary(a, "set:and", sj_common_atom(a, "set:mem", 0u, 2u),
                sj_common_substitute(a, body, 0u, forwardSlots, 3u, 4u, work)))));
    Atom *backward = sj_common_quantifier(a, "set:all", sj_common_binary(a, "set:imp",
        sj_common_atom(a, "set:mem", 0u, 1u), sj_common_quantifier(a, "set:exists",
            sj_common_binary(a, "set:and", sj_common_atom(a, "set:mem", 0u, 6u),
                sj_common_substitute(a, body, 0u, backwardSlots, 3u, 4u, work)))));
    Atom *conclusion = sj_common_quantifier(a, "set:exists", sj_common_binary(a, "set:and",
        sj_common_atom(a, "set:mem", 0u, 2u), sj_common_binary(a, "set:and", forward, backward)));
    return sj_common_quantifier(a, "set:all", sj_common_quantifier(a, "set:all",
        sj_common_quantifier(a, "set:exists", sj_common_quantifier(a, "set:all",
            sj_common_binary(a, "set:imp", premise, conclusion)))));
}

static Atom *sj_common_binder(Arena *a, const char *prefix, size_t sets, size_t props) {
    if (sets > SIZE_MAX - props) return NULL;
    char name[64];
    snprintf(name, sizeof(name), "_core_%s%zu", prefix, sets + props);
    return sj_sym(a, name);
}

static Atom *sj_common_all(Arena *a, const char *type, Atom *binder, Atom *body) {
    return binder && body ? sj_expr3(a, "all", sj_sym(a, type), sj_expr3(a, "lam", binder, body)) : NULL;
}

/* Compile the validated first-order sentence. The set environment is
 * extended only by set quantifiers. Church result binders never enter it. */
static Atom *sj_common_compile(Arena *a, Atom *body, Atom **environment,
                               size_t sets, size_t props, SjCommonExpansion *work) {
    if (!sj_common_spend(work, 1u)) return NULL;
    if (sj_is_expr(body, "set:bottom", 1u)) {
        Atom *result = sj_common_binder(a, "p", sets, props);
        return sj_common_all(a, "prop", result, result);
    }
    if (sj_is_expr(body, "set:eq", 3u) || sj_is_expr(body, "set:mem", 3u)) {
        size_t first, second;
        if (!sj_common_natural(body->expr.elems[1], &first) || first >= sets ||
            !sj_common_natural(body->expr.elems[2], &second) || second >= sets)
            return NULL;
        if (atom_is_symbol(body->expr.elems[0], "set:mem"))
            return sj_expr3(a, "In", environment[first], environment[second]);
        Atom *items[] = {sj_sym(a, "eq"), sj_sym(a, "set"),
                         environment[first], environment[second]};
        return atom_expr(a, items, 4u);
    }
    if (sj_is_expr(body, "set:imp", 3u))
        return sj_common_binary(a, "imp",
            sj_common_compile(a, body->expr.elems[1], environment, sets, props, work),
            sj_common_compile(a, body->expr.elems[2], environment, sets, props, work));
    if (sj_is_expr(body, "set:and", 3u) || sj_is_expr(body, "set:or", 3u)) {
        if (props == SIZE_MAX) return NULL;
        Atom *result = sj_common_binder(a, "p", sets, props);
        Atom *first = sj_common_compile(a, body->expr.elems[1], environment, sets, props + 1u, work);
        Atom *second = sj_common_compile(a, body->expr.elems[2], environment, sets, props + 1u, work);
        Atom *use = atom_is_symbol(body->expr.elems[0], "set:and")
            ? sj_common_binary(a, "imp", sj_common_binary(a, "imp", first,
                sj_common_binary(a, "imp", second, result)), result)
            : sj_common_binary(a, "imp", sj_common_binary(a, "imp", first, result),
                sj_common_binary(a, "imp", sj_common_binary(a, "imp", second, result), result));
        return sj_common_all(a, "prop", result, use);
    }
    if (sj_is_expr(body, "set:all", 2u) || sj_is_expr(body, "set:exists", 2u)) {
        if (sets >= SIZE_MAX / sizeof(Atom *) - 1u) return NULL;
        bool exists = atom_is_symbol(body->expr.elems[0], "set:exists");
        if (exists && props == SIZE_MAX) return NULL;
        size_t innerProps = props + (exists ? 1u : 0u);
        Atom *value = sj_common_binder(a, "s", sets, innerProps);
        if (!sj_common_spend(work, (uint64_t)sets + 1u)) return NULL;
        Atom **extended = arena_alloc(a, sizeof(Atom *) * (sets + 1u));
        if (!extended || !value) return NULL;
        extended[0] = value;
        for (size_t i = 0u; i < sets; i++) extended[i + 1u] = environment[i];
        Atom *compiled = sj_common_compile(a, body->expr.elems[1], extended, sets + 1u, innerProps, work);
        if (!exists) return sj_common_all(a, "set", value, compiled);
        Atom *result = sj_common_binder(a, "p", sets, props);
        Atom *use = sj_common_all(a, "set", value, sj_common_binary(a, "imp", compiled, result));
        return sj_common_all(a, "prop", result, sj_common_binary(a, "imp", use, result));
    }
    return NULL;
}

/* Schema instances have signature authority only after their complete
 * authored syntax has passed the declared decoder. The instance itself,
 * including its scope count and body, is retained as the dependency name. */
static Atom *sj_common_known(Arena *a, Atom *name,
                              CettaPrimeRegularKernelBudget *budget,
                              SjCommonFailure *failure_kind) {
    SjCommonExpansion work = {budget, SJ_COMMON_OK};
    Atom *record = NULL;
    if (!sj_common_profile(g_sj_signature_profile) || !sj_common_schema_name(name) ||
        name->expr.len != 3u) {
        work.failure = SJ_COMMON_INVALID;
        goto done;
    }
    if (!sj_common_spend(&work, 1u)) goto done;
    size_t parameters;
    if (!sj_common_natural(name->expr.elems[1], &parameters)) {
        /* A positive scope count larger than the host counter is still a
         * mathematical natural. This representation cannot expand it. */
        const char *large = atom_bigint_cstr(name->expr.elems[1]);
        work.failure = large && large[0] != '-' ? SJ_COMMON_DEPTH : SJ_COMMON_INVALID;
        goto done;
    }
    bool separation = atom_is_symbol(name->expr.elems[0], "boundedSeparation");
    bool strong = atom_is_symbol(name->expr.elems[0], "strongCollection");
    size_t extra = separation ? 1u : strong ? 2u : 3u;
    if (parameters > (size_t)INT64_MAX - extra) {
        work.failure = SJ_COMMON_DEPTH;
        goto done;
    }
    if (!sj_common_preflight(name->expr.elems[2], parameters + extra, parameters,
                             separation, strong, &work)) goto done;
    Atom *body = sj_common_decode(a, name->expr.elems[2], parameters + extra, separation, &work);
    if (!body) goto failed;
    Atom *sentence = separation ? sj_common_separation(a, body, &work) : strong
        ? sj_common_strong_collection(a, body, &work) : sj_common_subset_collection(a, body, &work);
    if (!sentence) goto failed;
    /* The closure allocation was charged in preflight, before this loop. */
    for (size_t i = 0u; i < parameters; i++) {
        sentence = sj_common_quantifier(a, "set:all", sentence);
        if (!sentence) goto failed;
    }
    Atom *proposition = sj_common_compile(a, sentence, NULL, 0u, 0u, &work);
    if (!proposition) goto failed;
    record = sj_known_record(a, name, proposition, "signature", NULL, NULL, 0u);
    if (record) goto done;
failed:
    if (work.failure == SJ_COMMON_OK) work.failure = SJ_COMMON_STORAGE;
done:
    if (failure_kind) *failure_kind = work.failure;
    return record;
}

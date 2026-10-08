/* Lossless source migration consumes this plan; binding decisions stay here,
 * beside the elaborator. Variable identities and template ownership are the
 * result of its actual rule-M pass, not a second spelling-based resolver. */
typedef struct {
    VarId id;
    const Atom *cover;
} PrimeMigrationSlot;

typedef struct {
    PrimeElab *elab;
    Atom *root;
    PrimeMigrationSlot *slots;
    size_t slots_len, slots_cap;
    CettaExprIndex path[PRIME_ELAB_NESTING_LIMIT + 1u];
    size_t depth;
} PrimeMigration;

static size_t prime_migration_occ(const Atom *term, VarId id) {
    if (!term || !atom_has_vars(term)) return 0u;
    if (term->kind == ATOM_VAR) return term->var_id == id;
    if (term->kind != ATOM_EXPR || atom_is_prime_own_list(term)) return 0u;
    size_t count = 0u;
    for (CettaExprIndex i = 0u; i < atom_authored_len(term); i++)
        count += prime_migration_occ(term->expr.elems[i], id);
    return count;
}

static bool prime_migration_simple(const Atom *term, CettaExprIndex *pattern,
                                    CettaExprIndex *source) {
    if (!term || term->kind != ATOM_EXPR || atom_authored_len(term) != 4u ||
        !term->expr.elems[0] || term->expr.elems[0]->kind != ATOM_SYMBOL)
        return false;
    SymbolId head = term->expr.elems[0]->sym_id;
    if (head == g_builtin_syms.let) {
        *pattern = 1u; *source = 2u;
        return true;
    }
    if (head == g_builtin_syms.match || head == g_builtin_syms.chain ||
        head == g_builtin_syms.filter_atom) {
        *pattern = 2u; *source = 1u;
        return true;
    }
    return false;
}

/* The unique pattern at this activation level whose pattern and continuation
 * contain every occurrence of a slot. Sources are outside the new scope.
 * Captures count as occurrences, but searching never enters a child frame. */
static const Atom *prime_migration_cover(PrimeMigration *m, Atom *term,
                                         VarId id) {
    if (!term || term->kind != ATOM_EXPR || !prime_migration_occ(term, id))
        return NULL;
    PrimeLambdaTelescope lambda;
    PrimeIterTemplate iter;
    if (prime_lambda_telescope(m->elab->arena, term, &lambda) ||
        prime_iter_template(term, &iter) || prime_code_like(term))
        return NULL;
    Atom *inner, *braces;
    if (prime_meta_braces(term, &inner, &braces)) {
        /* Existing explicit sharing already determines this boundary. */
        if (prime_braces_has(braces, id)) return NULL;
        return prime_migration_cover(m, inner, id);
    }
    if (prime_new_form(term)) {
        if (prime_migration_occ(term->expr.elems[1], id)) return NULL;
        return prime_migration_cover(m, term->expr.elems[2], id);
    }
    /* A function returns the refinements of its authored names. Even a
     * name used only in its body is observable through collapse-bind. */
    if (atom_is_symbol_id(term->expr.elems[0], g_builtin_syms.function))
        return NULL;
    CettaExprIndex pat, source;
    if (prime_migration_simple(term, &pat, &source) &&
        prime_migration_occ(term->expr.elems[pat], id))
        return prime_migration_occ(term->expr.elems[source], id) == 0u
            ? term : NULL;
    /* A sequence's covering pattern may scope over the remaining pairs. */
    if (prime_elab_is_let_star(term) &&
        term->expr.elems[1]->kind == ATOM_EXPR) {
        Atom *pairs = term->expr.elems[1];
        for (CettaExprIndex i = 0u; i < pairs->expr.len; i++) {
            Atom *pair = pairs->expr.elems[i];
            if (!pair || pair->kind != ATOM_EXPR || pair->expr.len != 2u)
                return NULL;
            if (prime_migration_occ(pair->expr.elems[1], id)) return NULL;
            if (prime_migration_occ(pair->expr.elems[0], id)) {
                /* One crossing set covers the whole let*. If a later pair
                 * refines this name, all its pairs must share that slot. */
                for (CettaExprIndex j = i + 1u; j < pairs->expr.len; j++) {
                    Atom *later = pairs->expr.elems[j];
                    if (!later || later->kind != ATOM_EXPR || later->expr.len != 2u ||
                        prime_migration_occ(later->expr.elems[0], id)) return NULL;
                }
                return pair;
            }
        }
        return prime_migration_cover(m, term->expr.elems[2], id);
    }
    SymbolId head = term->expr.len && term->expr.elems[0]->kind == ATOM_SYMBOL
        ? term->expr.elems[0]->sym_id : SYMBOL_ID_NONE;
    if ((head == g_builtin_syms.case_text || head == g_builtin_syms.switch_text ||
         head == g_builtin_syms.switch_minimal) &&
        (term->expr.len == 3u || term->expr.len == 4u) &&
        term->expr.elems[2]->kind == ATOM_EXPR) {
        Atom *pairs = term->expr.elems[2];
        size_t total = prime_migration_occ(term, id);
        for (CettaExprIndex i = 0u; i < pairs->expr.len; i++) {
            Atom *pair = pairs->expr.elems[i];
            if (pair && pair->kind == ATOM_EXPR && pair->expr.len == 2u &&
                prime_migration_occ(pair->expr.elems[0], id) &&
                prime_migration_occ(pair, id) == total) return pair;
        }
        return NULL;
    }
    /* An arbitrary compound can retain a child as syntax and run it later,
     * possibly more than once. A sole textual occurrence below such a node
     * does not make its pattern the owner of the enclosing activation's slot.
     * Only the binding forms handled above justify moving that ownership. */
    if (prime_migration_simple(term, &pat, &source) ||
        ((head == g_builtin_syms.equals || head == g_builtin_syms.bind_bang) &&
         atom_authored_len(term) == 3u) ||
        (head == g_builtin_syms.collapse && atom_authored_len(term) == 2u)) {
        Atom *only = NULL;
        for (CettaExprIndex i = 0u; i < atom_authored_len(term); i++) {
            Atom *child = term->expr.elems[i];
            if (!prime_migration_occ(child, id)) continue;
            if (only) return NULL;
            only = child;
        }
        return only ? prime_migration_cover(m, only, id) : NULL;
    }
    return NULL;
}

static const Atom *prime_migration_slot(PrimeMigration *m, VarId id) {
    for (size_t i = m->slots_len; i > 0u; i--)
        if (m->slots[i - 1u].id == id) return m->slots[i - 1u].cover;
    return prime_migration_cover(m, m->root, id);
}

static void prime_migration_string(const char *text) {
    putchar('"');
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if (*p == '"' || *p == '\\') printf("\\%c", *p);
        else if (*p < 32u) printf("\\u%04x", *p);
        else putchar(*p);
    }
    putchar('"');
}

static void prime_migration_event(PrimeMigration *m, const char *kind,
                                   Atom *term, Atom *const *names,
                                   size_t count) {
    printf("{\"kind\":\"%s\",\"form\":%zu,\"path\":[", kind,
           g_prime_migration_form);
    for (size_t i = 0u; i < m->depth; i++)
        printf("%s%u", i ? "," : "", (unsigned)m->path[i]);
    printf("],\"arity\":%u,\"head\":", (unsigned)(term->kind == ATOM_EXPR ? atom_authored_len(term) : 0u));
    const char *head = term->kind == ATOM_EXPR && term->expr.len &&
                       term->expr.elems[0]->kind == ATOM_SYMBOL
        ? symbol_bytes(g_symbols, term->expr.elems[0]->sym_id) : "";
    prime_migration_string(head ? head : "");
    printf(",\"names\":[");
    for (size_t i = 0u; i < count; i++) {
        if (i) putchar(',');
        char *name = atom_to_parseable_string(m->elab->arena, names[i]);
        if (!name) m->elab->failed = true;
        prime_migration_string(name ? name : "");
    }
    puts("]}");
}

static void prime_migration_walk(PrimeMigration *m, Atom *term, bool pattern,
                                  bool explicit_boundary);

static void prime_migration_child(PrimeMigration *m, Atom *term,
                                   CettaExprIndex index, bool pattern) {
    if (m->depth >= PRIME_ELAB_NESTING_LIMIT) {
        m->elab->failed = true;
        return;
    }
    m->path[m->depth++] = index;
    prime_migration_walk(m, term->expr.elems[index], pattern, false);
    m->depth--;
}

static void prime_migration_pattern(PrimeMigration *m, Atom *owner,
                                     Atom *pattern, Atom ***shared,
                                     size_t *count, size_t *cap) {
    PrimeElab *e = m->elab;
    Atom **names = NULL;
    size_t len = 0u, names_cap = 0u;
    /* A newly written crossing set must carry inherited shared names too:
     * its explicit boundary owns every pattern name omitted from the set. */
    size_t force = e->lex_shared_len;
    e->lex_shared_len = 0u;
    prime_lex_pattern_names(e, pattern, &names, &len, &names_cap);
    e->lex_shared_len = force;
    for (size_t i = 0u; i < len && !e->failed; i++) {
        if (!prime_lex_is_shared(e, names[i]->var_id) &&
            prime_migration_slot(m, names[i]->var_id) == owner) continue;
        bool seen = false;
        for (size_t j = 0u; j < *count; j++)
            if ((*shared)[j]->var_id == names[i]->var_id) seen = true;
        if (!seen && prime_elab_reserve(e, (void **)shared, cap, *count + 1u,
                                        sizeof(**shared)))
            (*shared)[(*count)++] = names[i];
    }
    free(names);
}

static void prime_migration_walk(PrimeMigration *m, Atom *term, bool pattern,
                                  bool explicit_boundary) {
    PrimeElab *e = m->elab;
    if (e->failed || !term || term->kind != ATOM_EXPR || !atom_has_vars(term))
        return;
    if (pattern) return; /* Pattern syntax is data, including quoted holes. */
    Atom *inner, *braces;
    if (prime_meta_braces(term, &inner, &braces)) {
        size_t force = e->lex_shared_len;
        for (CettaExprIndex i = 1u; i < braces->expr.len; i++)
            prime_lex_force_id(e, braces->expr.elems[i]->var_id, true);
        m->path[m->depth++] = 1u;
        prime_migration_walk(m, inner, false, true);
        m->depth--;
        e->lex_shared_len = force;
        return;
    }
    PrimeLambdaTelescope lambda;
    PrimeIterTemplate iter;
    Atom *own = NULL;
    CettaExprIndex body = 0u;
    bool template = false;
    if (prime_lambda_telescope(e->arena, term, &lambda)) {
        own = lambda.own; body = 2u; template = true;
    } else if (prime_iter_template(term, &iter)) {
        own = iter.own ? term->expr.elems[iter.own] : NULL;
        body = iter.body; template = true;
        for (CettaExprIndex i = 1u; i < iter.first_param; i++)
            prime_migration_child(m, term, i, false);
    } else if (prime_code_like(term)) {
        /* Code inspection is observable. Do not rewrite literal code as if
         * it were an executing body; retain it for explicit review. */
        if (atom_prime_template_own_hidden(term))
            own = term->expr.elems[term->expr.len - 1u];
        if (own && own->expr.len > PRIME_OWN_FIRST)
            prime_migration_event(m, "code-ownership", term,
                own->expr.elems + PRIME_OWN_FIRST,
                own->expr.len - PRIME_OWN_FIRST);
        return;
    }
    if (template) {
        size_t mark = m->slots_len, force = e->lex_shared_len;
        Atom **news = NULL;
        size_t n = 0u, cap = 0u;
        for (CettaExprIndex i = PRIME_OWN_FIRST;
             own && i < own->expr.len && !e->failed; i++) {
            Atom *name = own->expr.elems[i];
            const Atom *cover = prime_migration_cover(m, term->expr.elems[body],
                                                      name->var_id);
            if (prime_elab_reserve(e, (void **)&m->slots, &m->slots_cap,
                                  m->slots_len + 1u, sizeof(*m->slots)))
                m->slots[m->slots_len++] = (PrimeMigrationSlot){name->var_id, cover};
            prime_lex_force_id(e, name->var_id, false);
            if (!cover && !explicit_boundary &&
                !prime_elab_is_binder_id(e, name->var_id) &&
                prime_elab_reserve(e, (void **)&news, &cap, n + 1u,
                                  sizeof(*news)))
                news[n++] = name;
        }
        m->path[m->depth++] = body;
        if (n) prime_migration_event(m, "new", term->expr.elems[body], news, n);
        prime_migration_walk(m, term->expr.elems[body], false, false);
        m->depth--;
        free(news);
        m->slots_len = mark;
        e->lex_shared_len = force;
        return;
    }
    Atom **shared = NULL;
    size_t n = 0u, cap = 0u;
    CettaExprIndex pat = 0u, source = 0u;
    bool simple = prime_migration_simple(term, &pat, &source);
    bool sequence = prime_elab_is_let_star(term);
    SymbolId head = term->expr.len && term->expr.elems[0]->kind == ATOM_SYMBOL
        ? term->expr.elems[0]->sym_id : SYMBOL_ID_NONE;
    bool branches = (head == g_builtin_syms.case_text ||
                     head == g_builtin_syms.switch_text ||
                     head == g_builtin_syms.switch_minimal) &&
                    (term->expr.len == 3u || term->expr.len == 4u);
    if (!explicit_boundary && simple)
        prime_migration_pattern(m, term, term->expr.elems[pat], &shared, &n, &cap);
    if (!explicit_boundary && (sequence || branches)) {
        Atom *pairs = term->expr.elems[sequence ? 1u : 2u];
        for (CettaExprIndex i = 0u; pairs->kind == ATOM_EXPR && i < pairs->expr.len; i++) {
            Atom *pair = pairs->expr.elems[i];
            if (pair && pair->kind == ATOM_EXPR && pair->expr.len == 2u)
                prime_migration_pattern(m, pair, pair->expr.elems[0], &shared, &n, &cap);
        }
    }
    if (n) prime_migration_event(m, "cross", term, shared, n);
    size_t force = e->lex_shared_len;
    if (simple) {
        prime_migration_child(m, term, source, false);
        for (size_t i = 0u; i < n; i++) prime_lex_force_id(e, shared[i]->var_id, true);
        prime_migration_child(m, term, 3u, false);
    } else {
        for (size_t i = 0u; i < n; i++) prime_lex_force_id(e, shared[i]->var_id, true);
        for (CettaExprIndex i = 0u; i < atom_authored_len(term); i++) {
            PrimeChildRole role = prime_child_role(term, i);
            if (role == PRIME_CHILD_PATTERN_PAIRS) {
                Atom *pairs = term->expr.elems[i];
                for (CettaExprIndex j = 0u; pairs->kind == ATOM_EXPR && j < pairs->expr.len; j++) {
                    Atom *pair = pairs->expr.elems[j];
                    if (pair && pair->kind == ATOM_EXPR && pair->expr.len == 2u) {
                        m->path[m->depth++] = i;
                        m->path[m->depth++] = j;
                        prime_migration_child(m, pair, 1u, false);
                        m->depth -= 2u;
                    }
                }
            } else prime_migration_child(m, term, i, role == PRIME_CHILD_PATTERN);
        }
    }
    e->lex_shared_len = force;
    free(shared);
}

static bool prime_scope_migration_emit(PrimeElab *e, Atom *form) {
    PrimeMigration m = {.elab = e, .root = form};
    prime_migration_walk(&m, form, false, false);
    free(m.slots);
    return !e->failed;
}

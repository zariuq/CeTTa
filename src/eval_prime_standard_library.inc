/* Standard definitions are retained as data independently of their editable
 * occurrences in program spaces. Native execution is an optimization of an
 * unchanged default; modified heads use the ordinary equation dispatcher. */

typedef struct {
    SymbolId name;
    SymbolId original;
    bool native;
    Atom **equations;
    size_t equation_count;
    Atom **types;
    size_t type_count;
} PrimeStandardHead;

typedef struct {
    Arena arena;
    const Space *program;
    uint64_t program_instance;
    PrimeStandardHead *heads;
    size_t count;
    uint64_t identity;
    uint64_t symbols;
} PrimeStandardLibrary;

static _Atomic uint64_t g_prime_standard_identity = 1u;

static PrimeStandardLibrary *prime_standard_library(void) {
    if (eval_current_language_id() != CETTA_LANGUAGE_PRIME ||
        !g_library_context) return NULL;
    PrimeStandardLibrary *library = g_library_context->prime_standard_library;
    return library && library->symbols == symbol_table_instance_id(g_symbols)
        ? library : NULL;
}

static int prime_standard_head_order(const void *left, const void *right) {
    SymbolId x = ((const PrimeStandardHead *)left)->name;
    SymbolId y = ((const PrimeStandardHead *)right)->name;
    return (x > y) - (x < y);
}

static PrimeStandardHead *prime_standard_head(SymbolId name) {
    PrimeStandardLibrary *library = prime_standard_library();
    if (!library) return NULL;
    PrimeStandardHead key = {.name = name};
    return bsearch(&key, library->heads, library->count,
                   sizeof(*library->heads), prime_standard_head_order);
}

static SymbolId prime_standard_native_head(SymbolId name) {
    if (!prime_standard_library()) return name;
    const char *spelling = symbol_bytes(g_symbols, name);
    if (!spelling || strncmp(spelling, "std:", 4u) != 0) return name;
    PrimeStandardHead *head = prime_standard_head(
        symbol_lookup_cstr(g_symbols, spelling + 4u));
    return head && head->native && head->original == name ? head->name : name;
}

static SymbolId prime_standard_origin_head(SymbolId name) {
    if (!prime_standard_library()) return name;
    const char *spelling = symbol_bytes(g_symbols, name);
    if (!spelling || strncmp(spelling, "std:", 4u) != 0) return name;
    PrimeStandardHead *head = prime_standard_head(
        symbol_lookup_cstr(g_symbols, spelling + 4u));
    return head && head->original == name ? head->name : name;
}

static bool prime_standard_public_native(SymbolId name) {
    PrimeStandardHead *head = prime_standard_head(name);
    return head && head->native;
}

static bool prime_standard_grounded(SymbolId name) {
    return is_grounded_op(prime_standard_native_head(name));
}

static bool prime_standard_callable(SymbolId name) {
    if (!prime_standard_library()) return false;
    return prime_standard_native_head(name) != name ||
        symbol_eq_cstr(g_symbols, name, "std:definitions");
}

static Atom *prime_standard_call_view(Arena *arena, Atom *call) {
    SymbolId old = atom_head_symbol_id(call);
    SymbolId original = prime_standard_native_head(old);
    return original == old ? call
        : atom_expr_with_head(arena, call, atom_symbol_id(arena, original));
}

static void prime_standard_library_free(void *opaque) {
    PrimeStandardLibrary *library = opaque;
    for (size_t i = 0u; i < library->count; i++) {
        free(library->heads[i].equations);
        free(library->heads[i].types);
    }
    free(library->heads);
    arena_free(&library->arena);
    free(library);
}

static PrimeStandardHead *prime_standard_add_head(
    PrimeStandardLibrary *library, Atom *name) {
    if (!name || name->kind != ATOM_SYMBOL) return NULL;
    SymbolId id = atom_symbol_current_id(name);
    for (size_t i = 0u; i < library->count; i++)
        if (library->heads[i].name == id) return &library->heads[i];
    library->heads = cetta_realloc(library->heads,
        sizeof(*library->heads) * (library->count + 1u));
    const char *spelling = atom_name_cstr(name);
    size_t size = strlen(spelling) + 5u;
    char *qualified = arena_alloc(&library->arena, size);
    snprintf(qualified, size, "std:%s", spelling);
    PrimeStandardHead *head = &library->heads[library->count++];
    *head = (PrimeStandardHead){
        .name = id,
        .original = symbol_intern_cstr(g_symbols, qualified),
        .native = prime_builtin_name_reason(name) != NULL,
    };
    return head;
}

static void prime_standard_append(Atom ***items, size_t *count, Atom *atom) {
    *items = cetta_realloc(*items, sizeof(**items) * (*count + 1u));
    (*items)[(*count)++] = atom;
}

static void prime_standard_delegate(
    PrimeStandardLibrary *library, Space *space, Arena *arena,
    PrimeStandardHead *head, CettaExprLen arity) {
    for (size_t i = 0u; i < head->equation_count; i++) {
        Atom *lhs = head->equations[i]->expr.elems[1];
        if (lhs->kind == ATOM_EXPR && lhs->expr.len == arity + 1u)
            return;
    }
    Atom **arguments = arena_alloc(&library->arena,
        sizeof(*arguments) * ((size_t)arity + 1u));
    arguments[0] = atom_symbol_id(&library->arena, head->name);
    for (CettaExprIndex i = 0u; i < arity; i++) {
        char name[32];
        snprintf(name, sizeof name, "argument%" PRIu64, (uint64_t)i);
        arguments[i + 1u] = atom_var_with_id(&library->arena, name, fresh_var_id());
    }
    Atom *lhs = atom_expr(&library->arena, arguments, arity + 1u);
    arguments[0] = atom_symbol_id(&library->arena, head->original);
    Atom *rhs = atom_expr(&library->arena, arguments, arity + 1u);
    Atom *equation = atom_expr3(&library->arena,
        atom_symbol_id(&library->arena, g_builtin_syms.equals), lhs, rhs);
    prime_standard_append(&head->equations, &head->equation_count, equation);
    space_add(space, atom_deep_copy(arena, equation));
}

bool eval_prime_install_standard_library(Space *space, Arena *arena) {
    if (!space || !arena || !g_library_context ||
        eval_current_language_id() != CETTA_LANGUAGE_PRIME) return false;
    if (g_library_context->prime_standard_library) return true;
    /* These ordinary lazy functions consume computations, including the
     * selected continuation. Their demand is public declaration data,
     * retained by the qualified delegates and replaceable by programs. */
    static const struct { const char *name; CettaExprLen arity; } demands[] = {
        {"if-error", 3u}, {"return-on-error", 2u},
    };
    for (size_t d = 0u; d < sizeof(demands) / sizeof(*demands); d++) {
        CettaCount count = space_length64(space);
        for (CettaIndex i = 0u; i < count; i++) {
            Atom *row = space_get_at64(space, i);
            if (!row || row->kind != ATOM_EXPR || row->expr.len != 3u ||
                !atom_is_symbol_id(row->expr.elems[0], g_builtin_syms.colon) ||
                !atom_is_symbol(row->expr.elems[1], demands[d].name)) continue;
            Atom *type = row->expr.elems[2];
            if (!is_function_type(type) ||
                get_function_arg_count(type) != demands[d].arity) continue;
            Atom **parts = arena_alloc(arena, sizeof(*parts) * type->expr.len);
            if (!parts) return false;
            memcpy(parts, type->expr.elems, sizeof(*parts) * type->expr.len);
            for (CettaExprIndex k = 1u; k <= demands[d].arity; k++)
                parts[k] = atom_symbol_id(arena, g_builtin_syms.prime_proc);
            Atom *signature = atom_expr(arena, parts, type->expr.len);
            Atom *declaration = signature ? atom_expr3(arena,
                row->expr.elems[0], row->expr.elems[1], signature) : NULL;
            if (!declaration || !space_remove(space, row)) return false;
            space_add(space, declaration);
            break;
        }
    }
    /* quote's shared Atom domain already holds syntax in Prime. The
     * qualified delegate retains that ordinary declaration unchanged. */
    PrimeStandardLibrary *library = cetta_malloc(sizeof(*library));
    memset(library, 0, sizeof(*library));
    arena_init(&library->arena);
    arena_set_hashcons(&library->arena, NULL);
    arena_set_runtime_kind(&library->arena, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    library->program = space;
    library->program_instance = space_instance_id(space);
    library->identity = atomic_fetch_add(&g_prime_standard_identity, 1u);
    library->symbols = symbol_table_instance_id(g_symbols);
    CettaCount count = space_length64(space);
    for (CettaIndex i = 0u; i < count; i++) {
        Atom *row = space_get_at64(space, i);
        if (!row || row->kind != ATOM_EXPR || row->expr.len != 3u) continue;
        bool equation = atom_is_symbol_id(row->expr.elems[0], g_builtin_syms.equals);
        bool type = atom_is_symbol_id(row->expr.elems[0], g_builtin_syms.colon);
        if (!equation && !type) continue;
        Atom *name = row->expr.elems[1];
        if (equation && name->kind == ATOM_EXPR && name->expr.len)
            name = name->expr.elems[0];
        PrimeStandardHead *head = prime_standard_add_head(library, name);
        if (!head) continue;
        Atom *copy = atom_deep_copy(&library->arena, row);
        if (equation)
            prime_standard_append(&head->equations, &head->equation_count, copy);
        else prime_standard_append(&head->types, &head->type_count, copy);
    }
    /* These control forms have no ordinary function annotation, or expose an
     * additional arity beyond that annotation. Their delegates keep syntax
     * unevaluated until the selected original requests it. */
    static const struct { const char *name; CettaExprLen arity; } forms[] = {
        {"get-type", 2u}, {"let", 3u}, {"let*", 2u}, {"chain", 3u},
        {"match", 2u}, {"match", 3u}, {"eval", 1u}, {"eval", 2u},
        {"evalc", 2u}, {"metta", 3u}, {"collapse", 1u}, {"superpose", 1u},
        {"case", 2u}, {"switch", 2u}, {"switch-minimal", 2u}, {"unify", 4u},
        {"function", 1u}, {"return", 1u}, {"bind!", 2u}, {"context-space", 0u},
        {"new-state", 1u}, {"get-state", 1u}, {"change-state!", 2u},
        {"add-atom", 2u}, {"remove-atom", 2u}, {"get-atoms", 1u},
        {"capture", 2u}, {"run", 1u}, {"import!", 2u}, {"pragma!", 2u},
    };
    for (size_t i = 0u; i < sizeof forms / sizeof forms[0]; i++) {
        PrimeStandardHead *head = prime_standard_add_head(library,
            atom_symbol(&library->arena, forms[i].name));
        prime_standard_delegate(library, space, arena, head, forms[i].arity);
    }
    for (size_t i = 0u; i < library->count; i++) {
        PrimeStandardHead *head = &library->heads[i];
        if (head->native)
            for (size_t j = 0u; j < head->type_count; j++) {
                Atom *type = head->types[j]->expr.elems[2];
                if (is_function_type(type))
                    prime_standard_delegate(library, space, arena, head,
                        get_function_arg_count(type));
            }
        for (size_t j = 0u; j < head->type_count; j++)
            space_add(space, atom_expr3(arena,
                atom_symbol_id(arena, g_builtin_syms.colon),
                atom_symbol_id(arena, head->original),
                atom_deep_copy(arena, head->types[j]->expr.elems[2])));
        if (!head->native)
            for (size_t j = 0u; j < head->equation_count; j++) {
                Atom *row = head->equations[j];
                Atom *lhs = atom_deep_copy(arena, row->expr.elems[1]);
                lhs = lhs->kind == ATOM_SYMBOL
                    ? atom_symbol_id(arena, head->original)
                    : atom_expr_with_head(arena, lhs, atom_symbol_id(arena, head->original));
                space_add(space, atom_expr3(arena,
                    atom_symbol_id(arena, g_builtin_syms.equals), lhs,
                    atom_deep_copy(arena, row->expr.elems[2])));
            }
    }
    qsort(library->heads, library->count, sizeof(*library->heads), prime_standard_head_order);
    g_library_context->prime_standard_library = library;
    g_library_context->prime_standard_library_free = prime_standard_library_free;
    space_add(space, atom_expr3(arena,
        atom_symbol_id(arena, g_builtin_syms.colon),
        atom_symbol(arena, "std:definitions"),
        atom_expr3(arena, atom_symbol_id(arena, g_builtin_syms.arrow),
            atom_symbol_id(arena, g_builtin_syms.atom),
            atom_symbol_id(arena, g_builtin_syms.atom))));
    return true;
}

static bool prime_standard_head_unchanged(Space *space, SymbolId name) {
    PrimeStandardHead *head = prime_standard_head(name);
    if (!head || !head->native) return true;
    PrimeStandardLibrary *library = prime_standard_library();
    /* A separately created theory has no installed standard occurrences.
     * Its native service remains available until it supplies an equation.
     * Loaded programs and copies retain their qualified declaration view. */
    const Space *base = space;
    while (base && base->overlay_base) base = base->overlay_base;
    bool installed = base == library->program &&
        space_instance_id(base) == library->program_instance;
    if (!installed) {
        PrimeStandardHead *classifier = prime_standard_head(g_builtin_syms.get_type);
        installed = classifier && space_head_declares_type(space, classifier->original);
    }
    if (!installed && !eval_space_has_equations_raw(space, name)) return true;
    static _Thread_local struct {
        uint64_t library;
        SymbolId head;
        SpaceProgramToken token;
        bool unchanged;
    } cache[64];
    size_t slot = name % (sizeof cache / sizeof cache[0]);
    SpaceProgramToken token = space_program_token(space);
    /* Revalidation is real work in a native cost observation. Its memo is
     * shared by independent runs, so it cannot suppress recorded operations. */
    if (!cetta_native_cost_active() &&
        cache[slot].library == library->identity && cache[slot].head == name &&
        space_program_token_eq(cache[slot].token, token)) return cache[slot].unchanged;
    bool same = true;
    SpaceEquationCursor cursor;
    size_t found = 0u;
    if (!space_equation_cursor_init(space, name, &cursor)) same = false;
    while (same) {
        SpaceEquationOccurrenceId id;
        SpaceEquationCursorStep step = space_equation_cursor_next(&cursor, &id);
        if (step == SPACE_EQUATION_CURSOR_END) break;
        SpaceEquationOccurrence row;
        if (step != SPACE_EQUATION_CURSOR_ITEM ||
            !space_equation_occurrence_resolve(id, &row) ||
            found >= head->equation_count ||
            !atom_alpha_eq(row.equation, head->equations[found++])) same = false;
    }
    same = same && found == head->equation_count;
    Arena scratch;
    arena_init(&scratch);
    Atom **types = NULL;
    uint32_t type_count = space_get_declared_types(space,
        &scratch, atom_symbol_id(&scratch, name), &types);
    same = same && type_count == head->type_count;
    for (uint32_t i = 0u; same && i < type_count; i++)
        same = atom_alpha_eq(types[i], head->types[i]->expr.elems[2]);
    free(types);
    arena_free(&scratch);
    cache[slot].library = library->identity;
    cache[slot].head = name;
    cache[slot].token = token;
    cache[slot].unchanged = same;
    return same;
}

static bool prime_standard_library_unchanged(Space *space) {
    PrimeStandardLibrary *library = prime_standard_library();
    if (!library) return true;
    static _Thread_local struct {
        uint64_t library;
        SpaceProgramToken token;
        bool unchanged;
    } cache;
    SpaceProgramToken token = space_program_token(space);
    if (!cetta_native_cost_active() && cache.library == library->identity &&
        space_program_token_eq(cache.token, token)) return cache.unchanged;
    bool unchanged = true;
    for (size_t i = 0u; i < library->count; i++)
        if (library->heads[i].native &&
            !prime_standard_head_unchanged(space, library->heads[i].name)) {
            unchanged = false;
            break;
        }
    cache.library = library->identity;
    cache.token = token;
    cache.unchanged = unchanged;
    return unchanged;
}

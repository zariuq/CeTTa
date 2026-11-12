/* Standard definitions are retained as data independently of their editable
 * occurrences in program spaces. Native execution is an optimization of an
 * unchanged default; modified heads use the ordinary equation dispatcher.
 * CeTTa's own library may define a native head too: its equations are the
 * relational meaning the native operation accelerates. Those library rows
 * leave the head unchanged; only a program's own definition changes it. */

typedef struct {
    SymbolId name;
    SymbolId original;
    bool native;
    Atom **equations;
    size_t equation_count;
    Atom **types;
    size_t type_count;
    Atom **library_equations;
    size_t library_equation_count;
    Atom **library_types;
    size_t library_type_count;
} PrimeStandardHead;

typedef struct {
    Arena arena;
    const Space *program;
    uint64_t program_instance;
    PrimeStandardHead *heads;
    size_t count;
    uint64_t identity;
    uint64_t symbols;
    /* Advances whenever a library row is recorded: memoized verdicts of an
     * unchanged head depend on the rows as well as on the program. */
    uint64_t library_rows;
    CettaSymbolIndex names,originals;
    bool indexed;
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
    if(library->indexed) {
        size_t index=cetta_symbol_index_find(&library->names,name);
        return index==SIZE_MAX ? NULL : &library->heads[index];
    }
    PrimeStandardHead key = {.name = name};
    return bsearch(&key, library->heads, library->count,
                   sizeof(*library->heads), prime_standard_head_order);
}

/* The immutable library owns both spellings for its whole lifetime. This
 * indexes only their exact identities; live program edits are checked by
 * the ordinary declaration/currentness path after lookup. */
static PrimeStandardHead *prime_standard_alias(PrimeStandardLibrary *library,SymbolId name) {
    if(library->indexed) {
        size_t index=cetta_symbol_index_find(&library->originals,name);
        return index==SIZE_MAX ? NULL : &library->heads[index];
    }
    const char *spelling = symbol_bytes(g_symbols, name);
    if (!spelling || strncmp(spelling, "std:", 4u) != 0) return NULL;
    PrimeStandardHead *head = prime_standard_head(
        symbol_lookup_cstr(g_symbols, spelling + 4u));
    return head && head->original==name ? head : NULL;
}

static bool prime_standard_prepare_index(PrimeStandardLibrary *library) {
    library->indexed=false;
    for(size_t i=0u;i<library->count;i++) {
        const PrimeStandardHead *head=&library->heads[i];
        const char *alias=symbol_bytes(g_symbols,head->original);
        /* Binary symbol names are valid. The legacy alias route uses C
         * strings, so admit the index only when that route returns the same
         * public identity, including for embedded-NUL spellings. */
        if(head->name==SYMBOL_ID_NONE || head->original==SYMBOL_ID_NONE ||
            !alias || strncmp(alias,"std:",4u)!=0 ||
            symbol_lookup_cstr(g_symbols,alias+4u)!=head->name ||
            cetta_symbol_index_find(&library->names,head->name)!=SIZE_MAX ||
            cetta_symbol_index_find(&library->originals,head->original)!=SIZE_MAX ||
            !cetta_symbol_index_put(&library->names,head->name,i) ||
            !cetta_symbol_index_put(&library->originals,head->original,i)) {
            cetta_symbol_index_free(&library->names);
            cetta_symbol_index_free(&library->originals);
            return false;
        }
    }
    library->indexed=true;
    return true;
}

static SymbolId prime_standard_native_head(SymbolId name) {
    PrimeStandardLibrary *library=prime_standard_library();
    PrimeStandardHead *head=library ? prime_standard_alias(library,name) : NULL;
    return head && head->native ? head->name : name;
}

static SymbolId prime_standard_origin_head(SymbolId name) {
    PrimeStandardLibrary *library=prime_standard_library();
    PrimeStandardHead *head=library ? prime_standard_alias(library,name) : NULL;
    return head ? head->name : name;
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
        free(library->heads[i].library_equations);
        free(library->heads[i].library_types);
    }
    free(library->heads);
    cetta_symbol_index_free(&library->names);
    cetta_symbol_index_free(&library->originals);
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
    (void)prime_standard_prepare_index(library);
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

/* Whether `equation` is a forwarder prime_standard_delegate made for `head`:
 * (= (name $argument0 ...) (original $argument0 ...)). */
static bool prime_standard_is_forwarder(const PrimeStandardHead *head,
                                        Atom *equation) {
    if (!equation || equation->kind != ATOM_EXPR || equation->expr.len != 3u)
        return false;
    Atom *lhs = equation->expr.elems[1];
    Atom *rhs = equation->expr.elems[2];
    if (!lhs || !rhs || lhs->kind != ATOM_EXPR || rhs->kind != ATOM_EXPR ||
        lhs->expr.len == 0u || lhs->expr.len != rhs->expr.len ||
        !atom_is_symbol_id(lhs->expr.elems[0], head->name) ||
        !atom_is_symbol_id(rhs->expr.elems[0], head->original))
        return false;
    for (CettaExprIndex i = 1u; i < lhs->expr.len; i++)
        if (lhs->expr.elems[i]->kind != ATOM_VAR ||
            !atom_eq(lhs->expr.elems[i], rhs->expr.elems[i]))
            return false;
    return true;
}

/* Whether `row` is one of `rows`, up to renaming. */
static bool prime_standard_row_listed(Atom *const *rows, size_t count, Atom *row) {
    for (size_t i = 0u; i < count; i++)
        if (atom_alpha_eq(rows[i], row)) return true;
    return false;
}

/* Whether `space` is the program the standard library was installed in, or
 * an overlay of it. */
static bool prime_standard_space_is_program(const PrimeStandardLibrary *library,
                                            const Space *space) {
    const Space *base = space;
    while (base && base->overlay_base) base = base->overlay_base;
    return base && base == library->program &&
        space_instance_id(base) == library->program_instance;
}

/* The native heads' rows a space shows before an import: an import of
 * CeTTa's library records as library rows only the rows it adds, never a
 * definition already made.  Any space: a library a module imports is
 * imported into that module's space before the module reaches the program. */
typedef struct {
    size_t head;
    bool type;
    Atom *row;
} PrimeStandardMarkedRow;

typedef struct {
    Arena arena;
    PrimeStandardMarkedRow *rows;
    size_t count;
    uint64_t library;
    bool active;
} PrimeStandardImportMark;

static void prime_standard_mark_add(PrimeStandardImportMark *mark, size_t head,
                                    bool type, Atom *row) {
    if (!row) return;
    mark->rows = cetta_realloc(mark->rows, sizeof(*mark->rows) * (mark->count + 1u));
    mark->rows[mark->count++] = (PrimeStandardMarkedRow){
        .head = head, .type = type, .row = row};
}

static bool prime_standard_mark_has(const PrimeStandardImportMark *mark,
                                    size_t head, bool type, Atom *row) {
    for (size_t i = 0u; i < mark->count; i++)
        if (mark->rows[i].head == head && mark->rows[i].type == type &&
            atom_alpha_eq(mark->rows[i].row, row)) return true;
    return false;
}

static bool eval_prime_standard_import_begin(Space *space,
                                             PrimeStandardImportMark *mark) {
    memset(mark, 0, sizeof(*mark));
    PrimeStandardLibrary *library = prime_standard_library();
    if (!library || !space) return false;
    arena_init(&mark->arena);
    arena_set_hashcons(&mark->arena, NULL);
    mark->library = library->identity;
    mark->active = true;
    for (size_t i = 0u; i < library->count; i++) {
        PrimeStandardHead *head = &library->heads[i];
        if (!head->native) continue;
        SpaceEquationCursor cursor;
        if (space_equation_cursor_init(space, head->name, &cursor)) for (;;) {
            SpaceEquationOccurrenceId id;
            if (space_equation_cursor_next(&cursor, &id) !=
                SPACE_EQUATION_CURSOR_ITEM) break;
            SpaceEquationOccurrence row;
            if (space_equation_occurrence_resolve(id, &row) && row.equation)
                prime_standard_mark_add(mark, i, false,
                    atom_deep_copy(&mark->arena, row.equation));
        }
        Atom **types = NULL;
        uint32_t type_count = space_get_declared_types(space, &mark->arena,
            atom_symbol_id(&mark->arena, head->name), &types);
        for (uint32_t t = 0u; t < type_count; t++)
            prime_standard_mark_add(mark, i, true,
                atom_deep_copy(&mark->arena, types[t]));
        free(types);
    }
    return true;
}

static void eval_prime_standard_import_end(PrimeStandardImportMark *mark) {
    if (!mark->active) return;
    free(mark->rows);
    arena_free(&mark->arena);
    mark->active = false;
}

/* Records, as library rows, the equations and declarations of native heads
 * that an import of CeTTa's library added to the program. */
static void prime_standard_note_library_rows(PrimeStandardLibrary *library,
                                             Space *space,
                                             const PrimeStandardImportMark *mark) {
    for (size_t i = 0u; i < library->count; i++) {
        PrimeStandardHead *head = &library->heads[i];
        if (!head->native) continue;
        SpaceEquationCursor cursor;
        if (space_equation_cursor_init(space, head->name, &cursor)) for (;;) {
            SpaceEquationOccurrenceId id;
            if (space_equation_cursor_next(&cursor, &id) !=
                SPACE_EQUATION_CURSOR_ITEM) break;
            SpaceEquationOccurrence row;
            if (!space_equation_occurrence_resolve(id, &row) || !row.equation ||
                prime_standard_mark_has(mark, i, false, row.equation) ||
                prime_standard_row_listed(head->equations,
                    head->equation_count, row.equation) ||
                prime_standard_row_listed(head->library_equations,
                    head->library_equation_count, row.equation)) continue;
            prime_standard_append(&head->library_equations,
                &head->library_equation_count,
                atom_deep_copy(&library->arena, row.equation));
            library->library_rows++;
        }
        Arena scratch;
        arena_init(&scratch);
        Atom **types = NULL;
        uint32_t type_count = space_get_declared_types(space, &scratch,
            atom_symbol_id(&scratch, head->name), &types);
        for (uint32_t t = 0u; t < type_count; t++) {
            bool standard = false;
            for (size_t j = 0u; !standard && j < head->type_count; j++)
                standard = atom_alpha_eq(types[t], head->types[j]->expr.elems[2]);
            if (standard || prime_standard_mark_has(mark, i, true, types[t]) ||
                prime_standard_row_listed(head->library_types,
                    head->library_type_count, types[t])) continue;
            prime_standard_append(&head->library_types, &head->library_type_count,
                atom_deep_copy(&library->arena, types[t]));
            library->library_rows++;
        }
        free(types);
        arena_free(&scratch);
    }
}

/* A forwarder stands for a native head that has no equation of its arity
 * (prime_standard_delegate).  The standard library is installed before any
 * import, so a library imported later can define a head that its native
 * operation only accelerates: _minimal-retain-top-k-by-number in list, the
 * hol: and set: operations.  Kept beside that definition, the forwarder
 * answers as well, once more when the native operation succeeds and without
 * end when it declines, since its original then reaches this forwarder
 * again.  An import that makes an equation of a forwarder's arity visible in
 * the program retires that forwarder's occurrence there.  The standard
 * record keeps it, so std: still names the native original.
 *
 * When the import read CeTTa's own library, the rows it added for native
 * heads are recorded as library rows first (`mark` holds the rows shown
 * before it).  Such a head stays unchanged, so its native operation runs
 * first and its library equations answer when the operation declines. */
static void eval_prime_standard_note_import(
    Space *space, const PrimeStandardImportMark *mark, bool from_library) {
    PrimeStandardLibrary *library = prime_standard_library();
    if (!library || !space) return;
    if (from_library && mark && mark->active && mark->library == library->identity)
        prime_standard_note_library_rows(library, space, mark);
    if (!prime_standard_space_is_program(library, space)) return;
    for (size_t i = 0u; i < library->count; i++) {
        PrimeStandardHead *head = &library->heads[i];
        if (!head->native) continue;
        for (size_t j = 0u; j < head->equation_count; j++) {
            Atom *forwarder = head->equations[j];
            if (!prime_standard_is_forwarder(head, forwarder)) continue;
            CettaExprLen width = forwarder->expr.elems[1]->expr.len;
            SpaceEquationCursor cursor;
            if (!space_equation_cursor_init(space, head->name, &cursor)) continue;
            Atom *occurrence = NULL;
            bool defined = false;
            for (;;) {
                SpaceEquationOccurrenceId id;
                if (space_equation_cursor_next(&cursor, &id) !=
                    SPACE_EQUATION_CURSOR_ITEM) break;
                SpaceEquationOccurrence row;
                if (!space_equation_occurrence_resolve(id, &row) || !row.equation)
                    continue;
                if (atom_alpha_eq(row.equation, forwarder)) {
                    if (!occurrence) occurrence = row.equation;
                } else if (row.lhs && row.lhs->kind == ATOM_EXPR &&
                           row.lhs->expr.len == width) {
                    defined = true;
                }
            }
            if (defined && occurrence)
                (void)space_remove(space, occurrence);
        }
    }
}

/* The next standard row a program must show, from `found`: a forwarder that
 * a library row of its arity retired is not shown any more, unless `row` is
 * that forwarder itself. */
static size_t prime_standard_skip_retired(const PrimeStandardHead *head,
                                          size_t found, Atom *row) {
    while (found < head->equation_count) {
        Atom *standard = head->equations[found];
        if (!prime_standard_is_forwarder(head, standard) ||
            (row && atom_alpha_eq(row, standard))) break;
        CettaExprLen width = standard->expr.elems[1]->expr.len;
        bool retired = false;
        for (size_t i = 0u; !retired && i < head->library_equation_count; i++) {
            Atom *lhs = head->library_equations[i]->expr.elems[1];
            retired = lhs->kind == ATOM_EXPR && lhs->expr.len == width;
        }
        if (!retired) break;
        found++;
    }
    return found;
}

/* A verdict of prime_standard_head_verdict for the library, library rows and
 * program it read; the head is only compared. */
typedef struct {
    uint64_t library;
    uint64_t rows;
    SymbolId head;
    SpaceProgramToken token;
    bool unchanged;
    bool library_shown;
} PrimeStandardHeadVerdictMemo;

/* Whether `space` shows native head `name` as the standard library left it,
 * up to library rows; `library_shown` reports whether it shows any.  An
 * unchanged head runs natively.  Shown alone, its standard rows only name
 * the native operation; library rows are the relational meaning that
 * answers when the operation declines. */
static bool prime_standard_head_verdict(Space *space, SymbolId name,
                                        bool *library_shown) {
    *library_shown = false;
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
    static _Thread_local PrimeStandardHeadVerdictMemo cache[64];
    size_t slot = name % (sizeof cache / sizeof cache[0]);
    SpaceProgramToken token = space_program_token(space);
    /* Revalidation is real work in a native cost observation. Its memo is
     * shared by independent runs, so it cannot suppress recorded operations. */
    if (!cetta_native_cost_active() &&
        cache[slot].library == library->identity &&
        cache[slot].rows == library->library_rows && cache[slot].head == name &&
        space_program_token_eq(cache[slot].token, token)) {
        *library_shown = cache[slot].library_shown;
        return cache[slot].unchanged;
    }
    bool same = true;
    bool shown = false;
    SpaceEquationCursor cursor;
    size_t found = 0u;
    if (!space_equation_cursor_init(space, name, &cursor)) same = false;
    while (same) {
        SpaceEquationOccurrenceId id;
        SpaceEquationCursorStep step = space_equation_cursor_next(&cursor, &id);
        if (step == SPACE_EQUATION_CURSOR_END) break;
        SpaceEquationOccurrence row;
        if (step != SPACE_EQUATION_CURSOR_ITEM ||
            !space_equation_occurrence_resolve(id, &row)) {
            same = false;
            break;
        }
        /* Standard rows keep their order; library rows may stand among them. */
        found = prime_standard_skip_retired(head, found, row.equation);
        if (found < head->equation_count &&
            atom_alpha_eq(row.equation, head->equations[found])) {
            found++;
            continue;
        }
        same = prime_standard_row_listed(head->library_equations,
            head->library_equation_count, row.equation);
        shown = shown || same;
    }
    found = prime_standard_skip_retired(head, found, NULL);
    same = same && found == head->equation_count;
    Arena scratch;
    arena_init(&scratch);
    Atom **types = NULL;
    uint32_t type_count = space_get_declared_types(space,
        &scratch, atom_symbol_id(&scratch, name), &types);
    size_t found_type = 0u;
    for (uint32_t i = 0u; same && i < type_count; i++) {
        if (found_type < head->type_count &&
            atom_alpha_eq(types[i], head->types[found_type]->expr.elems[2])) {
            found_type++;
            continue;
        }
        same = prime_standard_row_listed(head->library_types,
            head->library_type_count, types[i]);
    }
    same = same && found_type == head->type_count;
    free(types);
    arena_free(&scratch);
    cache[slot].library = library->identity;
    cache[slot].rows = library->library_rows;
    cache[slot].head = name;
    cache[slot].token = token;
    cache[slot].unchanged = same;
    cache[slot].library_shown = same && shown;
    *library_shown = same && shown;
    return same;
}

static bool prime_standard_head_unchanged(Space *space, SymbolId name) {
    bool library_shown;
    return prime_standard_head_verdict(space, name, &library_shown);
}

/* Whether `space` runs native head `name` natively alone: unchanged and
 * showing no library row, so its equations are not consulted. */
static bool prime_standard_head_native_only(Space *space, SymbolId name) {
    bool library_shown;
    return prime_standard_head_verdict(space, name, &library_shown) &&
        !library_shown;
}

static bool prime_standard_library_unchanged(Space *space) {
    PrimeStandardLibrary *library = prime_standard_library();
    if (!library) return true;
    static _Thread_local struct {
        uint64_t library;
        uint64_t rows;
        SpaceProgramToken token;
        bool unchanged;
    } cache;
    SpaceProgramToken token = space_program_token(space);
    if (!cetta_native_cost_active() && cache.library == library->identity &&
        cache.rows == library->library_rows &&
        space_program_token_eq(cache.token, token)) return cache.unchanged;
    bool unchanged = true;
    for (size_t i = 0u; i < library->count; i++)
        if (library->heads[i].native &&
            !prime_standard_head_unchanged(space, library->heads[i].name)) {
            unchanged = false;
            break;
        }
    cache.library = library->identity;
    cache.rows = library->library_rows;
    cache.token = token;
    cache.unchanged = unchanged;
    return unchanged;
}

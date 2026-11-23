#define _POSIX_C_SOURCE 200809L
#include "tmetta_internal.h"
#include <stdlib.h>

bool tm_named(const Atom *atom, const char *name) {
    return atom && atom->kind == ATOM_SYMBOL &&
        symbol_eq_cstr(g_symbols, atom->sym_id, name);
}

TmDemand tm_type_demand(const Atom *type) {
    if (!type || tm_named(type, "Undefined") || tm_named(type, "%Undefined%"))
        return TM_DEMAND_DEFAULT;
    if (tm_named(type, "Atom") || tm_named(type, "Code") ||
        (type->kind == ATOM_EXPR && type->expr.len == 2u &&
            tm_named(type->expr.elems[0], "Code")))
        return TM_DEMAND_HELD;
    return TM_DEMAND_VALUE;
}

const TmDeclaration *tm_declaration(const TmProgram *program, const Atom *head) {
    uint32_t index;
    return head && head->kind == ATOM_SYMBOL &&
        cetta_u64_index_find(&program->declaration_index, head->sym_id, &index) ?
        &program->declarations[index] : NULL;
}

SymbolId tm_equation_head(const Atom *term) {
    const Atom *head = term->kind == ATOM_EXPR && term->expr.len ? term->expr.elems[0] : term;
    /* ABT presentation and contextual substitution can change the root. */
    return head->kind == ATOM_SYMBOL && !tm_named(head, "ABTBind") && !tm_named(head, "ctx") ?
        head->sym_id : SYMBOL_ID_NONE;
}

static int compare_equation_keys(const void *left, const void *right) {
    const TmEquationKey *a = left, *b = right;
    if (a->head != b->head) return a->head < b->head ? -1 : 1;
    return (a->ordinal > b->ordinal) - (a->ordinal < b->ordinal);
}

static size_t equation_bound(const TmProgram *program, SymbolId head, bool upper) {
    size_t lo = 0, hi = program->len;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        SymbolId key = program->by_head[mid].head;
        if (key < head || (upper && key == head)) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

void tm_equation_range(const TmProgram *program, SymbolId head, size_t *begin, size_t *end) {
    *begin = equation_bound(program, head, false);
    *end = equation_bound(program, head, true);
}

typedef struct { CettaVarMap names; bool binder; } TmInventory;

static Atom *inventory_variable(Arena *arena, Atom *variable, void *context) {
    (void)arena;
    TmInventory *inventory = context;
    CettaVarMap *map = &inventory->names;
    if (!inventory->binder && !cetta_frame_identity_is_none(variable->var_id.epoch)) return variable;
    if (!cetta_var_map_lookup(map, variable->var_id) &&
        !cetta_var_map_add(map, variable->var_id, variable)) return NULL;
    return variable;
}

static int compare_variables(const void *left, const void *right) {
    return var_id_cmp(((const CettaVarMapEntry *)left)->source_id,
                      ((const CettaVarMapEntry *)right)->source_id);
}

static BindingsFrameSchema *source_schema(Atom *source, TmCode *binder, Arena *owner) {
    Arena scratch;
    arena_init(&scratch);
    TmInventory inventory = {.binder = binder != NULL};
    CettaVarMap *map = &inventory.names;
    cetta_var_map_init(map);
    BindingsFrameSchema *schema = NULL;
    if (!cetta_atom_rewrite_vars(&scratch, source, inventory_variable, &inventory, false)) goto done;
    if (map->len > 1) qsort(map->items, map->len, sizeof *map->items, compare_variables);
    VarId *ids = map->len ? malloc(map->len * sizeof *ids) : NULL;
    Atom **variables = map->len ? malloc(map->len * sizeof *variables) : NULL;
    if (map->len && (!ids || !variables)) { free(ids); free(variables); goto done; }
    if (binder && map->len) {
        binder->bound_names = malloc(map->len * sizeof *binder->bound_names);
        binder->quote_slots = arena_alloc(owner, map->len * sizeof *binder->quote_slots);
        if (!binder->bound_names || !binder->quote_slots) { free(ids); free(variables); goto done; }
    }
    for (uint32_t i = 0; i < map->len; ++i) {
        VarId name = map->items[i].source_id;
        ids[i] = binder ? var_id_from_base((uint64_t)i + 1) : name;
        variables[i] = map->items[i].mapped_var;
        if (binder) {
            variables[i] = atom_var_like(&scratch, variables[i], ids[i]);
            binder->bound_names[i] = name;
            VarId local = cetta_frame_identity_is_none(name.epoch) ? name : fresh_var_id();
            binder->quote_slots[i] = atom_var_with_literal(owner, "local", local);
            if (!variables[i] || !binder->quote_slots[i]) { free(ids); free(variables); goto done; }
        }
    }
    schema = bindings_frame_schema_new_presented(ids, variables, map->len);
    free(ids);
    free(variables);
done:
    cetta_var_map_free(map);
    arena_free(&scratch);
    return schema;
}

BindingsFrameSchema *tm_schema(Atom *source) { return source_schema(source, NULL, NULL); }

static TmCode *compile(TmProgram *program, Atom *source) {
    if (!source) return NULL;
    for (TmCode *old = program->allocated; old; old = old->allocated_next)
        if (old->source == source) return old->compiled ? old : NULL;
    TmCode *code = calloc(1, sizeof *code);
    if (!code) return NULL;
    code->allocated_next = program->allocated;
    program->allocated = code;
    code->source = source;
    code->op = TM_DATA;
    if (source->kind == ATOM_SYMBOL) { code->op = TM_CALL; code->compiled = true; return code; }
    if (source->kind != ATOM_EXPR || !source->expr.len) { code->compiled = true; return code; }
    Atom **items = source->expr.elems;
    uint64_t n = source->expr.len - 1;
    if (n >= UINT32_MAX || n > SIZE_MAX / sizeof *code->children) return NULL;
    code->arity = (uint32_t)n;
    code->head = compile(program, items[0]);
    if (!code->head) return NULL;
    code->lexical = code->head->lexical;
    if (n) {
        code->children = calloc((size_t)n, sizeof *code->children);
        if (!code->children) return NULL;
        for (uint32_t i = 0; i < code->arity; ++i) {
            code->children[i] = compile(program, items[i + 1]);
            if (!code->children[i]) return NULL;
            code->lexical |= code->children[i]->lexical;
        }
    }
    if (atom_is_list_form(source) || tm_named(items[0], "ctx") ||
        tm_named(items[0], "Contextual")) {
        code->compiled = true;
        return code;
    }
    code->op = TM_CALL;
    if (n == 1 && (tm_named(items[0], "hold") || tm_named(items[0], "quote"))) code->op = TM_HOLD;
    if (n == 1 && (tm_named(items[0], "run") || tm_named(items[0], "eval"))) code->op = TM_RUN;
    if (n == 1 && (tm_named(items[0], "collapse") || tm_named(items[0], "reify"))) code->op = TM_COLLECT;
    if (n == 1 && (tm_named(items[0], "reflect") || tm_named(items[0], "superpose"))) {
        code->op = TM_REFLECT;
        code->superpose = tm_named(items[0], "superpose");
    }
    if ((n == 1 || n == 2) && tm_named(items[0], "serialize")) {
        code->op = TM_SERIALIZE;
        code->order = n == 2 && tm_named(items[1], "canonical") ? TM_ORDER_CANONICAL :
            n == 2 && tm_named(items[1], "any") ? TM_ORDER_ANY : TM_ORDER_INVALID;
    }
    if (n == 1 && (tm_named(items[0], "count") || tm_named(items[0], "size"))) code->op = TM_COUNT;
    if (n == 1 && (tm_named(items[0], "is-empty") || tm_named(items[0], "empty?"))) code->op = TM_EMPTY;
    if (n == 3 && tm_named(items[0], "fold-space")) code->op = TM_FOLD;
    if (n == 1 && tm_named(items[0], "delay")) code->op = TM_DELAY;
    if (n == 1 && tm_named(items[0], "force")) code->op = TM_FORCE;
    if (n == 2 && tm_named(items[0], "amb")) code->op = TM_CHOICE;
    if (!n && (tm_named(items[0], "empty") || tm_named(items[0], "fail"))) code->op = TM_FAIL;
    if (n == 3 && tm_named(items[0], "if")) code->op = TM_IF;
    if (n == 2 && tm_named(items[0], "check")) code->op = TM_CHECK;
    if (n == 2 && (tm_named(items[0], "unify") || tm_named(items[0], "=="))) code->op = TM_UNIFY;
    if (n == 3 && (tm_named(items[0], "let") || tm_named(items[0], "chain") || tm_named(items[0], "delay"))) {
        code->op = tm_named(items[0], "delay") ? TM_DELAY : TM_LET;
        code->binding = tm_named(items[0], "chain") ? 2 : 1;
    }
    if (n == 2 && tm_named(items[0], "new")) {
        code->op = TM_NEW;
        code->binding = 1;
    }
    if (n == 3 && tm_named(items[0], "match")) {
        code->op = TM_MATCH;
    }
    if (tm_named(items[0], "transform") || tm_named(items[0], "add-atom") ||
        tm_named(items[0], "remove-atom") || tm_named(items[0], "new-space") ||
        tm_named(items[0], "import!") || tm_named(items[0], "bind!")) code->op = TM_EFFECT;
    if (code->binding) {
        code->lexical = true;
        code->schema = source_schema(items[code->binding], code, &program->arena);
        if (!code->schema) return NULL;
    }
    code->compiled = true;
    return code;
}

static void discard_code(TmProgram *program, TmCode *checkpoint) {
    while (program->allocated != checkpoint) {
        TmCode *code = program->allocated;
        program->allocated = code->allocated_next;
        bindings_frame_schema_release(code->schema);
        free(code->bound_names);
        free(code->children);
        free(code);
    }
}

TmCode *tm_compile(TmProgram *program, Atom *source) {
    TmCode *checkpoint = program->allocated;
    TmCode *code = compile(program, source);
    if (!code) discard_code(program, checkpoint);
    return code;
}

void tm_program_dispose(TmProgram *program) {
    discard_code(program, NULL);
    for (size_t i = 0; i < program->len; ++i)
        bindings_frame_schema_release(program->equations[i].schema);
    free(program->equations);
    free(program->by_head);
    for (uint32_t i = 0; i < program->declaration_count; ++i)
        bindings_frame_schema_release(program->declarations[i].schema);
    free(program->declarations);
    cetta_u64_index_free(&program->declaration_index);
    abt_signature_free(&program->binders);
    arena_free(&program->arena);
}

static void free_program(TmProgram *program) {
    tm_program_dispose(program);
    free(program);
}

void tm_program_retain(TmProgram *program) {
    atomic_fetch_add_explicit(&program->refs, 1, memory_order_relaxed);
}

void tm_program_release(TmProgram *program) {
    if (program && atomic_fetch_sub_explicit(&program->refs, 1, memory_order_acq_rel) == 1)
        free_program(program);
}

void cetta_tmetta_runtime_free(struct CettaTmettaRuntime *runtime) {
    if (!runtime) return;
    tm_program_release(runtime->cached_program);
    bindings_free(&runtime->captures);
    for (uint32_t k = 0; k < runtime->slot_schema_cap; ++k)
        bindings_frame_schema_release(runtime->slot_schemas[k]);
    free(runtime->slot_schemas);
    arena_free(&runtime->knowledge);
    cetta_frame_identity_scope_clear(&runtime->capture_scope);
    pthread_mutex_destroy(&runtime->mutex);
    free(runtime);
}

struct CettaTmettaRuntime *tm_runtime(CettaLibraryContext *context) {
    if (!context->nik_runtime_mutex_ready || pthread_mutex_lock(&context->nik_runtime_mutex)) return NULL;
    if (!context->tmetta_runtime) {
        struct CettaTmettaRuntime *runtime = calloc(1, sizeof *runtime);
        pthread_mutexattr_t attributes;
        bool ready = runtime && pthread_mutexattr_init(&attributes) == 0;
        if (ready) {
            ready = pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_RECURSIVE) == 0 &&
                pthread_mutex_init(&runtime->mutex, &attributes) == 0;
            pthread_mutexattr_destroy(&attributes);
        }
        if (!ready) { free(runtime); pthread_mutex_unlock(&context->nik_runtime_mutex); return NULL; }
        bindings_init(&runtime->captures);
        if (!arena_try_init_detached(&runtime->knowledge) ||
            !cetta_frame_identity_scope_try(&runtime->capture_scope, &runtime->capture_epoch)) {
            cetta_tmetta_runtime_free(runtime);
            pthread_mutex_unlock(&context->nik_runtime_mutex);
            return NULL;
        }
        context->tmetta_runtime = runtime;
    }
    struct CettaTmettaRuntime *runtime = context->tmetta_runtime;
    pthread_mutex_unlock(&context->nik_runtime_mutex);
    return runtime;
}

static bool admit_declaration(TmProgram *program, Atom *head, Atom *type) {
    if (head->kind != ATOM_SYMBOL || type->kind != ATOM_EXPR || type->expr.len < 2 ||
        !tm_named(type->expr.elems[0], "->") || tm_declaration(program, head))
        return true;
    uint32_t index = program->declaration_count;
    uint64_t count = type->expr.len - 2;
    if (index == UINT32_MAX || count >= UINT32_MAX || count > SIZE_MAX / sizeof(TmDemand) ||
        (size_t)index >= SIZE_MAX / sizeof(TmDeclaration))
        return false;
    TmDeclaration *declarations = realloc(program->declarations,
        ((size_t)index + 1) * sizeof *declarations);
    if (!declarations) return false;
    program->declarations = declarations;
    program->declaration_count++;
    TmDeclaration *decl = &declarations[index];
    *decl = (TmDeclaration){.type = type, .count = (uint32_t)count};
    if (atom_has_vars(type)) {
        decl->schema = tm_schema(type);
        if (!decl->schema) return false;
    }
    if (count) {
        decl->parameters = arena_alloc(&program->arena, count * sizeof *decl->parameters);
        if (!decl->parameters) return false;
        for (uint32_t i = 0; i < decl->count; ++i)
            decl->parameters[i] = tm_type_demand(type->expr.elems[i + 1]);
    }
    return cetta_u64_index_insert(&program->declaration_index, head->sym_id, index) ==
        CETTA_GSLT_U32_INDEX_INSERTED_V1;
}

static bool scalar_pattern(Atom *left) {
    if (left->kind != ATOM_EXPR || !left->expr.len ||
        tm_equation_head(left) == SYMBOL_ID_NONE)
        return false;
    for (CettaExprIndex i = 1; i < left->expr.len; ++i) {
        Atom *arg = left->expr.elems[i];
        if (arg->kind == ATOM_VAR) {
            if (!cetta_frame_identity_is_none(arg->var_id.epoch)) return false;
        } else if (!tm_scalar_leaf(arg)) {
            return false;
        }
    }
    return true;
}

TmProgram *tm_program(CettaLibraryContext *context, Space *space) {
    struct CettaTmettaRuntime *runtime = context->tmetta_runtime;
    TmProgram *cached = runtime->cached_program;
    SpaceTheoryToken token = space_theory_token(space);
    if (cached && space_theory_token_eq(cached->token, token)) return cached;
    TmProgram *program = calloc(1, sizeof *program);
    if (!program) return NULL;
    atomic_init(&program->refs, 1);
    if (!arena_try_init_detached(&program->arena)) { free(program); return NULL; }
    abt_signature_init(&program->binders);
    uint32_t body_depth[] = {1};
    if (!abt_signature_add_defaults(&program->binders, &program->arena) ||
        !abt_signature_add_fixed(&program->binders,
            symbol_intern_cstr(g_symbols, "lam"), 1, body_depth)) goto fail;
    program->token = token;
    CettaCount rows = space_view_length64(space);
    for (CettaIndex i = 0; i < rows; ++i) {
        Atom *row = space_view_get_at64(space, i);
        if (!row || row->kind != ATOM_EXPR || row->expr.len != 3 || !tm_named(row->expr.elems[0], ":")) continue;
        Atom *owned = atom_deep_copy(&program->arena, row);
        if (!owned) goto fail;
        Atom *type = owned->expr.elems[2];
        if (!admit_declaration(program, owned->expr.elems[1], type)) goto fail;
        if (type->kind == ATOM_EXPR && type->expr.len &&
            (tm_named(type->expr.elems[0], "AbtSignature") || tm_named(type->expr.elems[0], "AbtSignatures"))) {
            bool set = tm_named(type->expr.elems[0], "AbtSignatures");
            if (!(set ? abt_signature_add_set(&program->binders, type) :
                    abt_signature_add_decl(&program->binders, type))) {
                program->error = atom_error(&program->arena, owned,
                    atom_symbol(&program->arena, "InvalidBindingSignature"));
                if (!program->error) goto fail;
            }
        }
    }
    SpaceEquationCursor cursor;
    if (!space_rule_cursor_init(space, SYMBOL_ID_NONE, CETTA_RULE_SYNTAX_ORDINARY, &cursor)) goto fail;
    SpaceEquationOccurrenceId identity;
    SpaceEquationCursorStep next;
    while ((next = space_equation_cursor_next(&cursor, &identity)) == SPACE_EQUATION_CURSOR_ITEM) {
        SpaceEquationOccurrence occurrence;
        if (!space_equation_occurrence_resolve(identity, &occurrence)) goto fail;
        if (program->len == program->cap) {
            if (program->cap > SIZE_MAX / (2 * sizeof *program->equations)) goto fail;
            size_t cap = program->cap ? program->cap * 2 : 16;
            TmEquation *equations = realloc(program->equations, cap * sizeof *equations);
            if (!equations) goto fail;
            program->equations = equations;
            program->cap = cap;
        }
        Atom *source = atom_deep_copy(&program->arena, occurrence.equation);
        if (!source) goto fail;
        TmCode *body = tm_compile(program, source->expr.elems[2]);
        if (!body) goto fail;
        BindingsFrameSchema *schema = tm_schema(source);
        if (!schema) goto fail;
        Atom *left = source->expr.elems[1];
        program->equations[program->len++] = (TmEquation){left, body, schema, scalar_pattern(left)};
    }
    if (next != SPACE_EQUATION_CURSOR_END) goto fail;
    if (program->len) {
        if (program->len > SIZE_MAX / sizeof *program->by_head) goto fail;
        program->by_head = malloc(program->len * sizeof *program->by_head);
        if (!program->by_head) goto fail;
        for (size_t i = 0; i < program->len; ++i)
            program->by_head[i] = (TmEquationKey){tm_equation_head(program->equations[i].left), i};
        qsort(program->by_head, program->len, sizeof *program->by_head, compare_equation_keys);
    }
    runtime->cached_program = program;
    tm_program_release(cached);
    return program;
fail:
    free_program(program);
    return NULL;
}

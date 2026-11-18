#include "tmetta_internal.h"
#include "grounded.h"
#include "transform_core.h"
#include "transform_tcalc.h"
#include "call_outcome.h"
#include "delay_service.h"
#include "shared_transition.h"
#include <stdlib.h>

typedef struct TmContinuation TmContinuation;
typedef struct TmCollector TmCollector;
typedef struct TmBranch TmBranch;
typedef struct TmTransaction TmTransaction;

typedef struct TmRead {
    Atom *owner;
    SpaceReadToken version;
    struct TmRead *next;
} TmRead;

typedef enum { K_BIND, K_ARGUMENT, K_REFLECT, K_SERIALIZE, K_COUNT, K_EMPTY,
    K_IF, K_MATCH, K_CHECK, K_LINEARIZE, K_FOLD, K_MEMO, K_FORCE, K_DISPATCH, K_CLOSE } TmContinuationKind;

struct TmContinuation {
    TmContinuationKind kind;
    TmCode *code;
    TmScope *scope;
    uint32_t position;
    union {
        Atom **arguments;
        Atom *call;
    };
    Atom *expected;
    const TmDeclaration *declaration;
    CettaIndex *order;
    uint64_t evaluator;
    TmCollector *collector;
    TmContinuation *next;
};

struct TmBranch {
    TmCode *code;
    Atom *value;
    Bindings bindings;
    TmScope *scope;
    TmContinuation *continuation;
    TmCollector *collector;
    bool waiting;
    Atom *residual;
    CettaDelayService *delay;
    TmBranch *next;
    TmBranch *allocated_next;
};

struct TmCollector {
    AnswerBank answers;
    size_t live;
    TmBranch *caller;
    TmCollector *parent;
    TmScope *scope_boundary;
    SpaceProgramToken program;
    TmRead *reads;
    TmCollector *next;
};

typedef struct TmProgramPin {
    TmProgram *program;
    struct TmProgramPin *next;
} TmProgramPin;

typedef struct TmMachine {
    CettaLibraryContext *context;
    Space *space;
    Registry *registry;
    TmProgram *program;
    TmProgramPin *program_pins;
    TmProgram query;
    Arena arena;
    TmCollector root;
    BindingsBuilder scalar_trail;
    TmScope empty_scope;
    TmScope *input_scope;
    SpaceReadToken checkpoint;
    TmScope *scopes;
    TmCollector *collectors;
    TmBranch *pending;
    TmBranch *suspended;
    TmBranch *active;
    TmBranch *branches;
    CettaFrameIdentityScope frames;
    CettaEvalEpisode episode;
    bool eager;
    bool read_only;
    bool values_only;
    TmTransaction *transaction;
    Atom *error;
} TmMachine;

static const char tm_frontier_kind[] = "tmetta-frontier";

static CettaCallOutcome evaluate(CettaLibraryContext *context, Space *space,
    Registry *registry, Arena *output, Atom *expression, int fuel,
    const Bindings *seed, TmMachine *parent, bool read_only,
    AnswerBank *answers, CettaEvalEpisode *episode);
static Space *transaction_space(TmTransaction *transaction, Space *source);
static Atom *contextual_normalize(TmMachine *machine, Bindings *bindings, Atom *source, int *status);

static void *work(TmMachine *machine, size_t bytes) {
    void *value = arena_alloc(&machine->arena, bytes);
    if (value) memset(value, 0, bytes);
    return value;
}

static bool refresh_program(TmMachine *machine) {
    SpaceProgramToken token = space_program_token(machine->space);
    if (machine->program && space_program_token_eq(machine->program->token, token)) return true;
    TmProgram *program = tm_program(machine->context, machine->space);
    if (!program) return false;
    for (TmProgramPin *pin = machine->program_pins; pin; pin = pin->next) {
        if (pin->program == program) {
            machine->program = program;
            machine->error = program->error;
            return true;
        }
    }
    TmProgramPin *pin = work(machine, sizeof *pin);
    if (!pin) return false;
    tm_program_retain(program);
    pin->program = program;
    pin->next = machine->program_pins;
    machine->program_pins = pin;
    machine->program = program;
    machine->error = program->error;
    return true;
}

static void raise_error(TmMachine *machine, Atom *source, const char *reason) {
    if (!machine->error) machine->error = atom_error(&machine->arena, source,
        atom_symbol(&machine->arena, reason));
}

static TmScope *scope_view(TmMachine *machine, TmScope *parent,
    BindingsFrameSchema *schema, const VarId *keys, Atom **slots) {
    TmScope *scope = work(machine, sizeof *scope);
    if (!scope) return NULL;
    bindings_frame_schema_retain(schema);
    scope->schema = schema;
    scope->keys = keys;
    scope->slots = slots;
    scope->parent = parent;
    scope->allocated_next = machine->scopes;
    machine->scopes = scope;
    return scope;
}

static TmScope *fresh_scope(TmMachine *machine, TmScope *parent,
    BindingsFrameSchema *schema, const VarId *keys) {
    uint32_t count = bindings_frame_schema_len(schema);
    if (!count) return parent ? parent : &machine->empty_scope;
    Atom **slots = work(machine, (size_t)count * sizeof *slots);
    TmScope *scope = slots ? scope_view(machine, parent, schema, keys, slots) : NULL;
    CettaFrameIdentity epoch;
    if (!scope || !cetta_frame_identity_scope_try(&machine->frames, &epoch)) return NULL;
    scope->identity = epoch;
    scope->collector = machine->active ? machine->active->collector : &machine->root;
    const VarId *ids = bindings_frame_schema_source_ids(schema);
    for (uint32_t i = 0; i < count; ++i) {
        slots[i] = atom_var_with_literal(&machine->arena, "slot", var_epoch_id(ids[i], epoch));
        if (!slots[i]) return NULL;
    }
    return scope;
}

static TmScope *open_scope_named(TmMachine *machine, TmScope *parent,
    BindingsFrameSchema *schema, Bindings *bindings, const VarId *keys) {
    TmScope *scope = fresh_scope(machine, parent, schema, keys);
    if (scope && bindings_frame_schema_len(schema) &&
        !bindings_register_complete_frame_schema(bindings, schema, scope->identity))
        return NULL;
    return scope;
}

static TmScope *open_scope(TmMachine *machine, TmScope *parent,
    BindingsFrameSchema *schema, Bindings *bindings) {
    return open_scope_named(machine, parent, schema, bindings, NULL);
}

static Atom *scope_lookup(const TmScope *scope, VarId name) {
    uint32_t slot;
    if (!scope->keys)
        return bindings_frame_schema_find_slot(scope->schema, name, &slot) ?
            scope->slots[slot] : NULL;
    uint32_t lo = 0, hi = bindings_frame_schema_len(scope->schema);
    while (lo < hi) {
        slot = lo + (hi - lo) / 2;
        int cmp = var_id_cmp(scope->keys[slot], name);
        if (!cmp) return scope->slots[slot];
        if (cmp < 0) lo = slot + 1;
        else hi = slot;
    }
    return NULL;
}

static Atom *scope_variable(Arena *arena, Atom *variable, void *context) {
    (void)arena;
    for (TmScope *scope = context; scope; scope = scope->parent) {
        Atom *mapped = scope_lookup(scope, variable->var_id);
        if (mapped) return mapped;
    }
    return variable;
}

static Atom *syntax(TmMachine *machine, TmScope *scope, Atom *source) {
    return cetta_atom_rewrite_vars(&machine->arena, source, scope_variable, scope, true);
}

static Atom *value(TmMachine *machine, TmBranch *branch, Atom *source) {
    Atom *opened;
    if (source->kind == ATOM_VAR)
        opened = scope_variable(&machine->arena, source, branch->scope);
    else if (tm_scalar_leaf(source))
        opened = source;
    else
        opened = syntax(machine, branch->scope, source);
    if (!opened) return NULL;
    Atom *bound = registry_lookup_ref(machine->registry, opened);
    if (bound) {
        opened = atom_deep_copy_single(&machine->arena, bound);
        if (!opened) return NULL;
    }
    int status = 1;
    Atom *result = contextual_normalize(machine, &branch->bindings, opened, &status);
    if (!result && status == 0) raise_error(machine, source, "ContextualArityRefused");
    return result;
}

typedef struct { TmCode *plan; Atom *result; TmScope *scope; } TmQuoteEntry;
typedef struct { TmQuoteEntry *entries; size_t len, cap; } TmQuoteMemo;

static Atom *quote_at(TmMachine *machine, TmBranch *branch, TmCode *plan,
    TmScope *scope, TmQuoteMemo *memo) {
    Atom *source = plan->source;
    if (!atom_has_vars(source)) return source;
    for (size_t i = 0; i < memo->len; ++i)
        if (memo->entries[i].plan == plan && memo->entries[i].scope == scope)
            return memo->entries[i].result;
    if (memo->len == memo->cap) {
        size_t cap = memo->cap ? memo->cap * 2 : 16;
        TmQuoteEntry *entries = realloc(memo->entries, cap * sizeof *entries);
        if (!entries) return NULL;
        memo->entries = entries;
        memo->cap = cap;
    }
    size_t position = memo->len++;
    memo->entries[position] = (TmQuoteEntry){.plan = plan, .scope = scope};
    if (!plan->lexical) {
        Atom *opened = syntax(machine, scope, source);
        Atom *result = opened ? bindings_apply_graph(&branch->bindings, &machine->arena, opened) : NULL;
        memo->entries[position].result = result;
        return result;
    }
    TmScope *body_scope = scope;
    if (plan->binding && bindings_frame_schema_len(plan->schema)) {
        body_scope = scope_view(machine, scope, plan->schema, plan->bound_names, plan->quote_slots);
        if (!body_scope) return NULL;
    }
    Atom **elements = work(machine, (plan->arity + 1) * sizeof *elements);
    if (!elements) return NULL;
    elements[0] = quote_at(machine, branch, plan->head, scope, memo);
    if (!elements[0]) return NULL;
    bool changed = elements[0] != plan->head->source;
    for (uint32_t i = 0; i < plan->arity; ++i) {
        TmCode *field = plan->children[i];
        elements[i + 1] = i + 1 == plan->binding ? syntax(machine, body_scope, field->source) :
            quote_at(machine, branch, field, i + 1 == plan->arity ? body_scope : scope, memo);
        if (!elements[i + 1]) return NULL;
        changed |= elements[i + 1] != field->source;
    }
    Atom *result = changed ? atom_expr(&machine->arena, elements, plan->arity + 1) : source;
    memo->entries[position].result = result;
    return result;
}

static Atom *quote_text(TmMachine *machine, TmBranch *branch, TmCode *plan, TmScope *scope) {
    TmQuoteMemo memo = {0};
    Atom *result = quote_at(machine, branch, plan, scope, &memo);
    free(memo.entries);
    return result;
}

static TmBranch *clone_branch(TmMachine *machine, TmBranch *source) {
    TmBranch *branch = calloc(1, sizeof *branch);
    if (!branch) return NULL;
    *branch = *source;
    bindings_init(&branch->bindings);
    if (!bindings_clone(&branch->bindings, &source->bindings)) { free(branch); return NULL; }
    branch->delay = NULL;
    branch->next = NULL;
    branch->allocated_next = machine->branches;
    machine->branches = branch;
    ++branch->collector->live;
    return branch;
}

static void drop_branch(TmMachine *machine, TmBranch *branch) {
    TmBranch **link = &machine->branches;
    while (*link && *link != branch) link = &(*link)->allocated_next;
    if (!*link) abort();
    *link = branch->allocated_next;
    if (machine->active == branch) machine->active = NULL;
    if (branch->collector) --branch->collector->live;
    cetta_delay_service_free(branch->delay);
    bindings_free(&branch->bindings);
    free(branch);
}

static void schedule(TmMachine *machine, TmBranch *branch) {
    branch->next = machine->pending;
    machine->pending = branch;
}

static TmContinuation *continuation(TmMachine *machine, TmBranch *branch,
    TmContinuationKind kind, TmCode *code) {
    TmContinuation *frame = work(machine, sizeof *frame);
    if (!frame) return NULL;
    frame->kind = kind;
    frame->code = code;
    frame->scope = branch->scope;
    frame->next = branch->continuation;
    return frame;
}

static bool start(TmMachine *machine, TmBranch *branch,
    TmContinuationKind kind, TmCode *code, TmCode *producer) {
    TmContinuation *frame = continuation(machine, branch, kind, code);
    if (!frame) return false;
    branch->continuation = frame;
    branch->code = producer;
    branch->value = NULL;
    return true;
}

static bool finish_branch(TmMachine *machine, TmBranch *branch, bool answer) {
    TmCollector *collector = branch->collector;
    if (answer) {
        int status = 1;
        branch->value = contextual_normalize(machine, &branch->bindings, branch->value, &status);
        if (!branch->value) {
            if (status == 0) raise_error(machine, branch->code ? branch->code->source : NULL, "ContextualArityRefused");
            return false;
        }
    }
    TmContinuation *close = collector->live == 1 && collector->caller ?
        continuation(machine, collector->caller, K_CLOSE, NULL) : NULL;
    if (collector->live == 1 && collector->caller && !close) return false;
    if (close) close->collector = collector;
    if (answer) {
        const Bindings *image = &branch->bindings;
        /* EvalOutcome observes a completed scalar directly. Collectors
         * and internal evaluations export the full answer image. */
        if (collector == &machine->root && machine->values_only &&
            tm_scalar_leaf(branch->value)) image = NULL;
        if (!answer_bank_add(&collector->answers, branch->value, image, NULL, NULL)) return false;
    }
    drop_branch(machine, branch);
    if (!collector->live && collector->caller) {
        TmBranch *caller = collector->caller;
        collector->caller = NULL;
        caller->continuation = close;
        caller->value = NULL;
        caller->code = NULL;
        schedule(machine, caller);
    }
    return true;
}

static void suspend(TmMachine *machine, TmBranch *branch) {
    if (machine->active == branch) machine->active = NULL;
    branch->next = machine->suspended;
    machine->suspended = branch;
}

static Atom *wait_variable(Arena *arena, Atom *variable, void *context) {
    (void)arena;
    CettaVarMap *support = context;
    if (!cetta_var_map_lookup(support, variable->var_id) &&
        !cetta_var_map_add(support, variable->var_id, variable)) return NULL;
    return variable;
}

static bool suspend_read(TmMachine *machine, TmBranch *branch, Atom *term) {
    CettaVarMap support;
    cetta_var_map_init(&support);
    Atom *observed = cetta_atom_rewrite_vars(&machine->arena, term, wait_variable, &support, true);
    VarId *variables = support.len ? work(machine, support.len * sizeof *variables) : NULL;
    bool ok = observed && (!support.len || variables);
    if (ok && support.len) {
        for (uint32_t i = 0; i < support.len; ++i) variables[i] = support.items[i].source_id;
        CettaDelayService *delay = cetta_delay_service_new();
        if (delay && cetta_delay_suspend(delay, 0, CETTA_DELAY_CLIENT_TMETTA,
                observed, variables, support.len, false)) {
            cetta_delay_commit(delay);
            branch->delay = delay;
        } else {
            cetta_delay_service_free(delay);
            ok = false;
        }
    }
    cetta_var_map_free(&support);
    if (ok) suspend(machine, branch);
    return ok;
}

static bool scope_local_to(const TmScope *scope, const TmCollector *collector) {
    for (const TmCollector *owner = scope->collector; owner; owner = owner->parent)
        if (owner == collector) return true;
    return false;
}

static bool collection_current(const TmMachine *machine, const TmCollector *collector) {
    if (!space_program_token_matches_live_space(collector->program, machine->space))
        return false;
    for (const TmRead *read = collector->reads; read; read = read->next)
        if (!space_read_token_matches_live_space(read->version, read->owner->ground.ptr))
            return false;
    return true;
}

static bool observe_space(TmMachine *machine, TmCollector *collector, Space *space) {
    for (; collector->parent; collector = collector->parent) {
        TmRead *read = collector->reads;
        while (read && read->version.space != space) read = read->next;
        if (read) {
            if (space_read_token_matches_live_space(read->version, space)) continue;
            machine->episode.completion = CETTA_EVAL_INCOMPLETE_INVALIDATED;
            return false;
        }
        read = work(machine, sizeof *read);
        if (!read) return false;
        read->owner = atom_space(&machine->arena, space);
        if (!read->owner) return false;
        read->version = space_read_token(space);
        read->next = collector->reads;
        collector->reads = read;
    }
    return true;
}

static Atom *publish_collection(TmMachine *machine, TmCollector *collector) {
    if (!collection_current(machine, collector)) {
        machine->episode.completion = CETTA_EVAL_INCOMPLETE_INVALIDATED;
        return NULL;
    }
    size_t count = 0;
    for (TmScope *scope = machine->scopes; scope != collector->scope_boundary;
         scope = scope->allocated_next)
        if (scope_local_to(scope, collector)) ++count;
    CettaFrameIdentity *locals = count ? work(machine, count * sizeof *locals) : NULL;
    if (count && !locals) return NULL;
    size_t next = 0;
    for (TmScope *scope = machine->scopes; scope != collector->scope_boundary;
         scope = scope->allocated_next)
        if (scope_local_to(scope, collector)) locals[next++] = scope->identity;
    Atom *value = tm_answers_publish(machine->context, &machine->arena,
        &collector->answers, NULL, false, locals, count);
    if (value) answer_bank_free(&collector->answers);
    return value;
}

static bool contextual_application(Atom *term) {
    return term && term->kind == ATOM_EXPR && term->expr.len >= 2 && tm_named(term->expr.elems[0], "ctx");
}

static bool object_index(Atom *term, uint64_t *index) {
    if (!term || term->kind != ATOM_EXPR || term->expr.len != 2 || !tm_named(term->expr.elems[0], "idx")) return false;
    Atom *number = term->expr.elems[1];
    if (number->kind != ATOM_GROUNDED || number->ground.gkind != GV_INT || number->ground.ival < 0) return false;
    *index = (uint64_t)number->ground.ival;
    return true;
}

typedef struct { Atom *source, *result; } TmReduction;
typedef struct { TmReduction *items; size_t len, cap; } TmReductions;

static Atom *reduce_contextual(TmMachine *machine, Bindings *bindings, Atom *source,
    TmReductions *memo, int *status) {
    if (!source || source->kind != ATOM_EXPR) return source;
    for (size_t i = 0; i < memo->len; ++i)
        if (memo->items[i].source == source) return memo->items[i].result;
    if (memo->len == memo->cap) {
        size_t cap = memo->cap ? memo->cap * 2 : 16;
        TmReduction *items = realloc(memo->items, cap * sizeof *items);
        if (!items) { *status = -1; return NULL; }
        memo->items = items; memo->cap = cap;
    }
    size_t position = memo->len++;
    memo->items[position] = (TmReduction){.source = source};
    Atom **children = work(machine, source->expr.len * sizeof *children);
    if (source->expr.len && !children) { *status = -1; return NULL; }
    bool changed = false;
    for (CettaExprIndex i = 0; i < source->expr.len; ++i) {
        children[i] = reduce_contextual(machine, bindings, source->expr.elems[i], memo, status);
        if (!children[i]) return NULL;
        changed = changed || children[i] != source->expr.elems[i];
    }
    Atom *result = changed ? atom_expr(&machine->arena, children, source->expr.len) : source;
    if (contextual_application(result)) {
        Atom *function = result->expr.elems[1];
        if (function->kind != ATOM_VAR) {
            CettaExprLen arity = result->expr.len - 2;
            if (function->kind != ATOM_EXPR || function->expr.len != 3 ||
                !tm_named(function->expr.elems[0], "Contextual") ||
                function->expr.elems[1]->kind != ATOM_GROUNDED ||
                function->expr.elems[1]->ground.gkind != GV_INT ||
                function->expr.elems[1]->ground.ival < 0 ||
                (uint64_t)function->expr.elems[1]->ground.ival != arity) {
                *status = 0; return NULL;
            }
            Atom *body = function->expr.elems[2];
            for (CettaExprIndex i = 0; i < arity; ++i) {
                if (!body || body->kind != ATOM_EXPR || body->expr.len != 2 || !tm_named(body->expr.elems[0], "lam")) {
                    *status = 0; return NULL;
                }
                body = abt_subst(&machine->program->binders, &machine->arena, 0,
                    result->expr.elems[i + 2], body->expr.elems[1]);
            }
            body = body ? bindings_apply_graph(bindings, &machine->arena, body) : NULL;
            if (!body) { *status = -1; return NULL; }
            result = reduce_contextual(machine, bindings, body, memo, status);
        }
    }
    memo->items[position].result = result;
    return result;
}

static Atom *contextual_normalize(TmMachine *machine, Bindings *bindings, Atom *source, int *status) {
    if (source && tm_scalar_leaf(source)) return source;
    TmReductions memo = {0};
    Atom *opened = source ? bindings_apply_graph(bindings, &machine->arena, source) : NULL;
    Atom *result = opened ? reduce_contextual(machine, bindings, opened, &memo, status) : NULL;
    free(memo.items);
    if (!result && *status == 1) *status = -1;
    return result;
}

static Atom *contextual_projection(TmMachine *machine, uint32_t arity, Atom *helper,
    const uint32_t *positions, uint32_t count) {
    Atom **parts = work(machine, (count + 2) * sizeof *parts);
    if (!parts) return NULL;
    parts[0] = atom_symbol(&machine->arena, "ctx"); parts[1] = helper;
    for (uint32_t i = 0; i < count; ++i)
        parts[i + 2] = atom_expr2(&machine->arena, atom_symbol(&machine->arena, "idx"),
            atom_int(&machine->arena, arity - 1 - positions[i]));
    Atom *body = atom_expr(&machine->arena, parts, count + 2);
    for (uint32_t i = 0; body && i < arity; ++i)
        body = atom_expr2(&machine->arena, atom_symbol(&machine->arena, "lam"), body);
    return body ? atom_expr3(&machine->arena, atom_symbol(&machine->arena, "Contextual"),
        atom_int(&machine->arena, arity), body) : NULL;
}

static Atom *contextual_helper(TmMachine *machine, Bindings *bindings) {
    Atom *source = atom_var_with_id(&machine->arena, "context", fresh_var_id());
    BindingsFrameSchema *schema = source ? tm_schema(source) : NULL;
    TmScope *scope = schema ? open_scope(machine, NULL, schema, bindings) : NULL;
    bindings_frame_schema_release(schema);
    return scope ? scope_lookup(scope, source->var_id) : NULL;
}

/* After opening the ambient object context, its parameters are fresh rigid
 * markers. Flexible occurrences may forget forbidden parameters; rigid
 * occurrences of them cannot be abstracted. The actual binder operations
 * remain the common ABT close/substitution operations. */
static Atom *contextual_prune(TmMachine *machine, Bindings *bindings, Atom *term,
    Atom *solving, Atom **parameters, uint64_t depth, const bool *allowed,
    uint64_t local_depth, int *status) {
    if (term->kind == ATOM_VAR) {
        if (var_id_eq(term->var_id, solving->var_id)) { *status = 0; return NULL; }
        for (uint64_t i = 0; i < depth; ++i)
            if (var_id_eq(term->var_id, parameters[i]->var_id) && !allowed[i]) { *status = 0; return NULL; }
        return term;
    }
    if (term->kind != ATOM_EXPR) return term;
    if (contextual_application(term)) {
        Atom *identity = term->expr.elems[1];
        if (identity->kind == ATOM_VAR) {
            if (var_id_eq(identity->var_id, solving->var_id)) { *status = 0; return NULL; }
            uint32_t arity = (uint32_t)(term->expr.len - 2), count = 0;
            uint32_t *kept = work(machine, arity * sizeof *kept);
            if (arity && !kept) { *status = -1; return NULL; }
            for (uint32_t i = 0; i < arity; ++i) {
                Atom *argument = term->expr.elems[i + 2];
                bool parameter = false, keep = false;
                uint64_t index;
                if (object_index(argument, &index) && index < local_depth) parameter = keep = true;
                for (uint64_t j = 0; !parameter && j < depth; ++j)
                    if (argument->kind == ATOM_VAR && var_id_eq(argument->var_id, parameters[j]->var_id)) {
                        parameter = true; keep = allowed[j];
                    }
                if (!parameter) { *status = 2; return NULL; }
                for (uint32_t j = 0; j < i; ++j)
                    if (atom_eq(argument, term->expr.elems[j + 2])) { *status = 2; return NULL; }
                if (keep) kept[count++] = i;
            }
            if (count < arity) {
                Atom *helper = contextual_helper(machine, bindings);
                Atom *body = helper ? contextual_projection(machine, arity, helper, kept, count) : NULL;
                if (!body) { *status = -1; return NULL; }
                if (!bindings_add_var_acyclic(bindings, identity, body)) { *status = 0; return NULL; }
                return contextual_normalize(machine, bindings, term, status);
            }
        }
    }
    Atom **parts = work(machine, term->expr.len * sizeof *parts);
    if (term->expr.len && !parts) { *status = -1; return NULL; }
    const AbtSignatureEntry *entry = term->expr.len && term->expr.elems[0]->kind == ATOM_SYMBOL ?
        abt_signature_lookup(&machine->program->binders, term->expr.elems[0]->sym_id,
            (uint32_t)(term->expr.len - 1)) : NULL;
    for (CettaExprIndex i = 0; i < term->expr.len; ++i) {
        uint64_t next_depth = local_depth + (entry && i ? entry->depths[i - 1] : 0);
        parts[i] = contextual_prune(machine, bindings, term->expr.elems[i], solving,
            parameters, depth, allowed, next_depth, status);
        if (!parts[i]) return NULL;
    }
    return atom_expr(&machine->arena, parts, term->expr.len);
}

static int contextual_solve(TmMachine *machine, Bindings *bindings,
    Atom *application, Atom *target, uint64_t depth) {
    uint32_t arity = (uint32_t)(application->expr.len - 2);
    uint64_t *indices = work(machine, arity * sizeof *indices);
    if (arity && !indices) return -1;
    for (uint32_t i = 0; i < arity; ++i) {
        if (!object_index(application->expr.elems[i + 2], &indices[i])) return 2;
        if (indices[i] >= depth) return 0;
        for (uint32_t j = 0; j < i; ++j) if (indices[i] == indices[j]) return 2;
    }
    Atom *identity = application->expr.elems[1];
    if (identity->kind != ATOM_VAR) return 0;
    if (contextual_application(target) && target->expr.elems[1]->kind == ATOM_VAR) {
        uint32_t other_arity = (uint32_t)(target->expr.len - 2);
        uint64_t *other = work(machine, other_arity * sizeof *other);
        uint32_t *left = work(machine, arity * sizeof *left);
        uint32_t *right = work(machine, arity * sizeof *right);
        if ((other_arity && !other) || (arity && (!left || !right))) return -1;
        for (uint32_t i = 0; i < other_arity; ++i) {
            if (!object_index(target->expr.elems[i + 2], &other[i])) return 2;
            if (other[i] >= depth) return 0;
            for (uint32_t j = 0; j < i; ++j) if (other[i] == other[j]) return 2;
        }
        bool same = var_id_eq(identity->var_id, target->expr.elems[1]->var_id);
        if (same && arity != other_arity) return 0;
        uint32_t common = 0;
        for (uint32_t i = 0; i < arity; ++i)
            for (uint32_t j = 0; j < other_arity; ++j)
                if (indices[i] == other[j] && (!same || i == j)) {
                    left[common] = i; right[common++] = j;
                }
        Atom *helper = contextual_helper(machine, bindings);
        Atom *first = helper ? contextual_projection(machine, arity, helper, left, common) : NULL;
        Atom *second = helper && !same ? contextual_projection(machine, other_arity, helper, right, common) : NULL;
        if (!first || (!same && !second)) return -1;
        if (!bindings_add_var_acyclic(bindings, identity, first)) return 0;
        return same || bindings_add_var_acyclic(bindings, target->expr.elems[1], second) ? 1 : 0;
    }
    Atom **parameters = work(machine, depth * sizeof *parameters);
    bool *allowed = work(machine, depth * sizeof *allowed);
    if (depth && (!parameters || !allowed)) return -1;
    for (uint64_t i = 0; i < depth; ++i) {
        parameters[i] = atom_var_with_id(&machine->arena, "object-parameter", fresh_var_id());
        if (!parameters[i]) return -1;
    }
    for (uint32_t i = 0; i < arity; ++i) allowed[depth - 1 - indices[i]] = true;
    Atom *body = abt_subst_vars(&machine->program->binders, &machine->arena, parameters, depth, target);
    if (!body) return -1;
    int status = 1;
    body = contextual_prune(machine, bindings, body, identity, parameters, depth, allowed, 0, &status);
    if (!body) return status;
    for (uint32_t i = arity; body && i; --i) {
        body = abt_close(&machine->program->binders, &machine->arena, parameters[depth - 1 - indices[i - 1]], body);
        if (body) body = atom_expr2(&machine->arena, atom_symbol(&machine->arena, "lam"), body);
    }
    if (!body) return -1;
    if (!abt_scope_check(&machine->program->binders, 0, body)) return 0;
    Atom *closure = atom_expr3(&machine->arena, atom_symbol(&machine->arena, "Contextual"),
        atom_int(&machine->arena, arity), body);
    return closure && bindings_add_var_acyclic(bindings, identity, closure) ? 1 : -1;
}

typedef struct { Atom *left, *right; uint64_t depth; } TmPair;

typedef struct {
    TmPair local[16];
    TmPair *items;
    size_t len, cap;
} TmPairs;

static bool push_pair(TmPairs *stack, TmPair pair) {
    if (!stack->items) {
        stack->items = stack->local;
        stack->cap = sizeof stack->local / sizeof *stack->local;
    }
    if (stack->len == stack->cap) {
        if (stack->cap > SIZE_MAX / (2 * sizeof *stack->items)) return false;
        size_t cap = stack->cap * 2;
        bool local = stack->items == stack->local;
        TmPair *items = local ? malloc(cap * sizeof *items) :
            realloc(stack->items, cap * sizeof *items);
        if (!items) return false;
        if (local) memcpy(items, stack->local, stack->len * sizeof *items);
        stack->items = items;
        stack->cap = cap;
    }
    stack->items[stack->len++] = pair;
    return true;
}

/* Canonical ABT scope is checked before a logical variable may acquire a
 * body. Logical variables are nullary unless explicitly applied by ctx;
 * an object-bound index cannot escape through such a nullary variable. */
static int match_data_internal(TmMachine *machine, Bindings *bindings,
    Atom *left, Atom *right, BindingsBuilder *trail) {
    const AbtSignature *signature = &machine->program->binders;
    Atom *parsed_left = abt_parse(signature, &machine->arena, left);
    if (tm_capacity_failed()) return -1;
    Atom *parsed_right = abt_parse(signature, &machine->arena, right);
    if (tm_capacity_failed()) return -1;
    if (parsed_left) left = parsed_left;
    else if (!abt_scope_check(signature, 0, left)) return tm_capacity_failed() ? -1 : 0;
    if (parsed_right) right = parsed_right;
    else if (!abt_scope_check(signature, 0, right)) return tm_capacity_failed() ? -1 : 0;
    TmPairs first = {0}, second = {0};
    TmPairs *ready = &first, *waiting = &second;
    if (!push_pair(ready, (TmPair){left, right, 0})) return -1;
    int result = 1;
    bool progressed = false;
    for (;;) {
        if (!ready->len) {
            if (!waiting->len || !progressed) break;
            TmPairs *previous = ready;
            ready = waiting;
            waiting = previous;
            progressed = false;
        }
        TmPair pair = ready->items[--ready->len];
        int normalized = 1;
        Atom *a = contextual_normalize(machine, bindings, pair.left, &normalized);
        Atom *b = a ? contextual_normalize(machine, bindings, pair.right, &normalized) : NULL;
        if (!a || !b) { result = normalized; break; }
        if (atom_eq(a, b)) continue;
        if (contextual_application(a) || contextual_application(b)) {
            /* The in-place caller admits a first-order scalar pattern.
             * If a future caller presents a contextual application, its
             * writes still belong to the separately staged matcher. */
            if (trail) { result = 2; break; }
            int solved = contextual_application(a) ? contextual_solve(machine, bindings, a, b, pair.depth) :
                contextual_solve(machine, bindings, b, a, pair.depth);
            if (tm_capacity_failed()) { result = -1; break; }
            if (solved <= 0) { result = solved; break; }
            progressed |= solved == 1;
            if (!push_pair(solved == 2 ? waiting : ready, pair)) { result = -1; break; }
            continue;
        }
        if (a->kind == ATOM_VAR || b->kind == ATOM_VAR) {
            Atom *body = a->kind == ATOM_VAR ? b : a;
            if (!abt_scope_check(signature, 0, body) ||
                !(trail ? match_atoms_builder(a, b, trail, &machine->arena) :
                    match_atoms(a, b, bindings, &machine->arena))) {
                result = tm_capacity_failed() ? -1 : 0;
                break;
            }
            progressed = true;
            continue;
        }
        if (a->kind != ATOM_EXPR || b->kind != ATOM_EXPR || a->expr.len != b->expr.len) {
            result = 0; break;
        }
        const AbtSignatureEntry *entry = a->expr.len && a->expr.elems[0]->kind == ATOM_SYMBOL ?
            abt_signature_lookup(signature, a->expr.elems[0]->sym_id, (uint32_t)(a->expr.len - 1)) : NULL;
        for (CettaExprIndex i = 0; i < a->expr.len; ++i) {
            uint64_t depth = pair.depth + (entry && i ? entry->depths[i - 1] : 0);
            if (!push_pair(ready, (TmPair){a->expr.elems[i], b->expr.elems[i], depth})) {
                result = -1;
                break;
            }
        }
        if (result < 0) break;
    }
    if (first.items != first.local) free(first.items);
    if (second.items != second.local) free(second.items);
    if (result == 1 && waiting->len) {
        /* Store the complete scoped equation with known applications
         * reduced. The shared store sees only the actual residual. */
        int status = 1;
        Atom *a = contextual_normalize(machine, bindings, left, &status);
        Atom *b = a ? contextual_normalize(machine, bindings, right, &status) : NULL;
        if (!b) return status;
        if (!atom_eq(a, b)) return bindings_add_constraint(bindings, a, b) ? 2 : -1;
    }
    return result;
}

static int match_data(TmMachine *machine, Bindings *bindings, Atom *left, Atom *right) {
    return match_data_internal(machine, bindings, left, right, NULL);
}

static SymbolId primitive(Atom *head) {
    if (!head || head->kind != ATOM_SYMBOL) return SYMBOL_ID_NONE;
    SymbolId id = head->sym_id;
    if (id == g_builtin_syms.car_atom || id == g_builtin_syms.cdr_atom || is_grounded_op(id))
        return id;
    if (tm_named(head, "list:from")) return g_builtin_syms.lib_list_from_expression;
    if (tm_named(head, "tuple:from")) return g_builtin_syms.lib_list_to_expression;
    return SYMBOL_ID_NONE;
}

static bool check_value(TmMachine *machine, TmBranch *branch, Atom *expected, Atom *answer) {
    if (expected && atom_has_vars(expected)) {
        expected = bindings_apply_graph(&branch->bindings, &machine->arena, expected);
        if (!expected) return false;
    }
    if (tm_type_demand(expected) != TM_DEMAND_VALUE) return true;
    if (answer->kind == ATOM_VAR) return suspend_read(machine, branch, answer);
    if ((tm_named(expected, "Expression") && answer->kind == ATOM_EXPR) ||
        (tm_named(expected, "Symbol") && answer->kind == ATOM_SYMBOL) ||
        (tm_named(expected, "Grounded") && answer->kind == ATOM_GROUNDED)) return true;
    Atom *actual = get_grounded_type(&machine->arena, answer);
    if (expected->kind == ATOM_VAR && actual)
        return match_atoms(expected, actual, &branch->bindings, &machine->arena);
    if (actual && atom_eq(actual, expected)) return true;
    Atom **types = NULL;
    uint32_t count = space_get_declared_types(machine->space, &machine->arena, answer, &types);
    bool accepted = false;
    for (uint32_t i = 0; i < count; ++i) if (atom_eq(types[i], expected)) { accepted = true; break; }
    free(types);
    if (!accepted) machine->error = atom_error(&machine->arena, answer,
        atom_expr3(&machine->arena, atom_symbol(&machine->arena, "TypeMismatch"), expected,
            actual ? actual : atom_symbol(&machine->arena, "UnknownType")));
    return true;
}

static bool argument_is_value(TmMachine *machine, TmCode *code, Atom *head,
    const TmDeclaration *declaration, uint32_t position) {
    TmCode *argument = code->children[position];
    if (argument->op == TM_HOLD) return false;
    if (argument->op == TM_RUN) return true;
    if (code->op == TM_UNIFY) return false;
    if (code->op == TM_EFFECT) return tm_named(head, "bind!") && position == 1;
    if (declaration && position < declaration->count) {
        TmDemand demand = declaration->parameters[position];
        if (demand != TM_DEMAND_DEFAULT) return demand == TM_DEMAND_VALUE;
    }
    return primitive(head) != SYMBOL_ID_NONE || machine->eager;
}

static Space *knowledge_space(TmMachine *machine, Atom *ref) {
    Space *space = ref && ref->kind == ATOM_GROUNDED && ref->ground.gkind == GV_SPACE
        ? ref->ground.ptr : ref ? resolve_space(machine->registry, ref) : NULL;
    return machine->transaction && space ? transaction_space(machine->transaction, space) : space;
}

typedef struct {
    Space *source, *prepared;
    SpaceReadToken version;
    bool changed;
} TmTransactionSpace;

struct TmTransaction {
    TmMachine *machine;
    TmBranch *branch;
    TmTransactionSpace *spaces;
    size_t len, cap;
    bool failed, pending;
};

static int transaction_open_row(void *context, Arena *arena, BindingsBuilder *builder,
    Atom *stored, Atom **opened) {
    (void)arena;
    TmTransaction *transaction = context;
    Bindings next;
    bindings_init(&next);
    BindingsFrameSchema *schema = tm_schema(stored);
    bool ok = schema && bindings_clone(&next, bindings_builder_bindings(builder));
    TmScope *scope = ok ? open_scope(transaction->machine, NULL, schema, &next) : NULL;
    *opened = scope ? syntax(transaction->machine, scope, stored) : NULL;
    ok = *opened && bindings_builder_try_merge(builder, &next);
    bindings_frame_schema_release(schema);
    bindings_free(&next);
    if (!ok) {
        transaction->failed = true;
        transaction->machine->episode.completion = CETTA_EVAL_INCOMPLETE_CAPACITY;
    }
    return ok ? 1 : -1;
}

static int transaction_unify(void *context, Arena *arena, BindingsBuilder *builder,
    Atom *left, Atom *right) {
    (void)arena;
    TmTransaction *transaction = context;
    TmMachine *machine = transaction->machine;
    Bindings next;
    bindings_init(&next);
    Atom *owned_left = atom_deep_copy(&machine->arena, left);
    Atom *owned_right = atom_deep_copy(&machine->arena, right);
    int decision = owned_left && owned_right && bindings_clone(&next, bindings_builder_bindings(builder)) ?
        match_data(machine, &next, owned_left, owned_right) : -1;
    if (decision == 1 && !bindings_builder_try_merge(builder, &next)) decision = -1;
    if (decision == 2) {
        transaction->pending = true;
        transaction->branch->residual = bindings_capture_value(&machine->arena, &next);
        if (!transaction->branch->residual) decision = -1;
        machine->episode.completion = CETTA_EVAL_INCOMPLETE_SUSPENDED;
    }
    if (decision < 0) {
        transaction->failed = true;
        machine->episode.completion = CETTA_EVAL_INCOMPLETE_CAPACITY;
    }
    bindings_free(&next);
    return decision;
}

static Atom *transaction_capture(void *context, Arena *arena, Atom *publication,
    const Bindings *bindings) {
    (void)context;
    Atom *knowledge = bindings_capture_value(arena, bindings);
    return knowledge ? atom_expr3(arena, atom_symbol(arena, "tmetta:publication"), publication, knowledge) : NULL;
}

static Space *transaction_space(TmTransaction *transaction, Space *source) {
    for (size_t i = 0; i < transaction->len; ++i)
        if (transaction->spaces[i].source == source || transaction->spaces[i].prepared == source)
            return transaction->spaces[i].prepared;
    if (transaction->len == transaction->cap) {
        size_t cap = transaction->cap ? transaction->cap * 2 : 4;
        TmTransactionSpace *spaces = realloc(transaction->spaces, cap * sizeof *spaces);
        if (!spaces) { transaction->failed = true; return NULL; }
        transaction->spaces = spaces;
        transaction->cap = cap;
    }
    SpaceReadToken version = space_read_token(source);
    Space *prepared = space_heap_clone_shallow(source);
    if (!prepared) { transaction->failed = true; return NULL; }
    transaction->spaces[transaction->len++] = (TmTransactionSpace){source, prepared, version, false};
    return prepared;
}

static Atom *transaction_error(TmTransaction *transaction, Atom *source, const char *reason) {
    transaction->failed = true;
    return atom_error(&transaction->machine->arena, source,
        atom_symbol(&transaction->machine->arena, reason));
}

static bool transaction_resolve(void *context, Atom *ref, Space **result) {
    TmTransaction *transaction = context;
    *result = knowledge_space(transaction->machine, ref);
    if (!*result && ref && ref->kind == ATOM_VAR) transaction->pending = true;
    return *result != NULL;
}

static Atom *transaction_take(void *context, Atom *ref, Atom *atom, size_t copies) {
    TmTransaction *transaction = context;
    Space *space = knowledge_space(transaction->machine, ref);
    if (!space) return transaction_error(transaction, ref, "ExpectedKnowledgeSpace");
    CettaIndex *positions = copies ? malloc(copies * sizeof *positions) : NULL;
    if (copies && !positions) return transaction_error(transaction, atom, "CapacityExhausted");
    size_t found = 0;
    CettaCount count = space_length64(space);
    for (CettaIndex i = 0; i < count && found < copies; ++i)
        if (atom_eq(space_get_at64(space, i), atom)) positions[found++] = i;
    bool removed = found == copies && space_remove_logical_positions(space, positions, found);
    free(positions);
    if (!removed) return transaction_error(transaction, atom, "TransformTakeRefused");
    for (size_t i = 0; i < transaction->len; ++i)
        if (transaction->spaces[i].prepared == space) transaction->spaces[i].changed = true;
    return NULL;
}

static Atom *transaction_remove(void *context, Atom *ref, Atom *atom) {
    return transaction_take(context, ref, atom, 1);
}

static Atom *transaction_add(void *context, Atom *ref, Atom *atom) {
    TmTransaction *transaction = context;
    Space *space = knowledge_space(transaction->machine, ref);
    if (!space || !space_admit_scoped_atom(space, &transaction->machine->arena, atom))
        return transaction_error(transaction, atom, "TransformMakeRefused");
    for (size_t i = 0; i < transaction->len; ++i)
        if (transaction->spaces[i].prepared == space) transaction->spaces[i].changed = true;
    return NULL;
}

static bool transaction_spend(void *context, uint64_t units) {
    TmTransaction *transaction = context;
    if (eval_native_interrupted()) {
        transaction->machine->episode.completion = tm_capacity_failed() ?
            CETTA_EVAL_INCOMPLETE_CAPACITY : CETTA_EVAL_INCOMPLETE_CANCELLED;
        return false;
    }
    return !transaction->failed && cetta_eval_episode_step(&transaction->machine->episode, units);
}

static bool transaction_evaluate_bound(void *context, Arena *arena, Atom *expression,
    const Bindings *bindings, Atom *variables, Atom ***results, Atom ***givens,
    size_t *len, Atom **error) {
    TmTransaction *transaction = context;
    TmMachine *machine = transaction->machine;
    AnswerBank answers;
    answer_bank_init(&answers);
    CettaEvalEpisode episode;
    CettaCallOutcome outcome = evaluate(machine->context, machine->space, machine->registry,
        arena, expression, -1, bindings, machine, true, &answers, &episode);
    *results = NULL; *len = 0; *error = NULL;
    if (givens) *givens = NULL;
    bool ok = outcome.kind != CETTA_CALL_RAISED && outcome.kind != CETTA_CALL_INTERRUPTED;
    if (outcome.kind == CETTA_CALL_RAISED) *error = outcome.term;
    if (outcome.kind == CETTA_CALL_SUSPENDED) transaction->pending = true;
    if (answers.len) {
        *results = malloc(answers.len * sizeof **results);
        if (givens && variables) *givens = malloc(answers.len * sizeof **givens);
        if (!*results || (givens && variables && !*givens)) ok = false;
        for (CettaIndex i = 0; ok && i < answers.len; ++i) {
            const AnswerRecord *record = answer_bank_get(&answers, i);
            Atom *result = bindings_apply_graph((Bindings *)&record->bindings, arena, record->result);
            (*results)[i] = result ? atom_deep_copy(arena, result) : NULL;
            ok = (*results)[i] != NULL;
            if (givens && variables) {
                Atom *given = bindings_apply_graph((Bindings *)&record->bindings, arena, variables);
                (*givens)[i] = given ? atom_deep_copy(arena, given) : NULL;
                ok = ok && (*givens)[i] != NULL;
            }
        }
    }
    if (ok) *len = answers.len;
    else if (!*error && episode.completion == CETTA_EVAL_COMPLETE) transaction->failed = true;
    answer_bank_free(&answers);
    return ok;
}

static bool transaction_evaluate(void *context, Arena *arena, Atom *expression,
    Atom ***results, size_t *len, Atom **error) {
    TmTransaction *transaction = context;
    return transaction_evaluate_bound(context, arena, expression,
        &transaction->branch->bindings, NULL, results, NULL, len, error);
}

static CettaTransformOutcome transaction_decide(void *context, Arena *arena,
    const CettaTransformFiring *firing, Atom **error) {
    if (firing->principal || firing->expected_count != 1 || !firing->port_count ||
        firing->check_count || firing->equation_count != firing->port_count ||
        firing->port_count > SIZE_MAX / sizeof(CettaTransformPort))
        return CETTA_TRANSFORM_DECLINED;
    Atom *packet = firing->expected[0];
    if (packet->kind != ATOM_EXPR || packet->expr.len != 3 ||
        !tm_named(packet->expr.elems[0], "tmetta:publication"))
        return CETTA_TRANSFORM_DECLINED;
    Atom *payload = packet->expr.elems[1];
    if (atom_has_vars(payload) || !atom_eq(payload, firing->publication))
        return CETTA_TRANSFORM_DECLINED;
    size_t count = firing->port_count;
    for (size_t i = 0; i < count; ++i) {
        const CettaTransformPort *port = &firing->ports[i];
        if (!port->space || atom_has_vars(port->receipt) ||
            !atom_eq(port->pattern, port->receipt))
            return CETTA_TRANSFORM_DECLINED;
    }
    CettaTransformPort *ports = arena_alloc(arena, count * sizeof *ports);
    if (!ports) return CETTA_TRANSFORM_DECLINED;
    memcpy(ports, firing->ports, count * sizeof *ports);
    for (size_t i = 0; i < count; ++i) ports[i].exact = true;
    /* The carrier checks the payload and exact occurrences. The common
     * commit publishes the original packet, including the captured store. */
    CettaTransformFiring projected = *firing;
    projected.ports = ports;
    projected.expected = &payload;
    return cetta_transform_tcalc_decide(context, arena, &projected, error);
}

static bool transform(TmMachine *machine, TmBranch *branch, Atom *call) {
    if (call->expr.len != 4 && call->expr.len != 5) {
        raise_error(machine, call, "TransformArity"); return true;
    }
    TmTransaction transaction = {.machine = machine, .branch = branch};
    const CettaTransformLogic logic = {.open_row = transaction_open_row,
        .unify = transaction_unify, .capture = transaction_capture, .exact_occurrences = true};
    CettaTransformHost host = {.context = &transaction, .dialect = CETTA_TRANSFORM_DIALECT_HE,
        .logic = &logic,
        .environment = &branch->bindings, .resolve_space = transaction_resolve,
        .remove_atom = transaction_remove, .add_atom = transaction_add,
        .take_atom = transaction_take, .evaluate = transaction_evaluate,
        .evaluate_bound = transaction_evaluate_bound, .spend = transaction_spend,
        .spend_poll_only = !machine->episode.budget_limited,
        .decide_firing = transaction_decide};
    Atom **args = call->expr.elems;
    Atom *conjuncts = NULL, *unused = NULL, *error = NULL;
    bool ok = cetta_transform_prepare(&machine->arena, args[2], args[3],
        host.dialect, false, true, &conjuncts, &unused, &error);
    AnswerBank made;
    answer_bank_init(&made);
    Bindings next;
    bindings_init(&next);
    TmTransaction *previous = machine->transaction;
    machine->transaction = &transaction;
    Atom *publication = atom_expr3(&machine->arena,
        atom_symbol(&machine->arena, "tmetta:firing"), call, args[3]);
    ok = ok && publication && conjuncts;
    Atom *answer = NULL;
    CettaTransformOutcome outcome = ok ? cetta_transform_step(&machine->arena,
        args[1], conjuncts, publication, NULL, &host, &answer) : CETTA_TRANSFORM_ERROR;
    ok = ok && !transaction.failed;
    if (outcome == CETTA_TRANSFORM_ERROR) {
        if (answer) machine->error = answer;
        else if (error) machine->error = error;
        else if (ok) raise_error(machine, call, "MalformedTransform");
    } else if (outcome == CETTA_TRANSFORM_FIRED && ok) {
        ok = bindings_clone(&next, &branch->bindings);
        Atom *rows = answer->expr.elems[1];
        Atom *destination = call->expr.len == 5 ? args[4] : args[1];
        bool comma = args[3]->kind == ATOM_EXPR && args[3]->expr.len && tm_named(args[3]->expr.elems[0], ",");
        for (CettaExprIndex i = 0; ok && i < rows->expr.len; ++i) {
            Atom *packet = rows->expr.elems[i];
            Bindings known;
            bindings_init(&known);
            ok = packet->kind == ATOM_EXPR && packet->expr.len == 3 &&
                tm_named(packet->expr.elems[0], "tmetta:publication") &&
                bindings_restore_captured_value(packet->expr.elems[2], &known) &&
                bindings_try_merge_live(&next, &known);
            bindings_free(&known);
            if (!ok) { raise_error(machine, call, "IncompatibleFiringKnowledge"); break; }
            Atom *row = packet->expr.elems[1];
            int matched = match_data(machine, &next, call, row->expr.elems[1]);
            if (matched != 1) {
                if (matched == 0) raise_error(machine, call, "TransformBindingMismatch");
                transaction.pending = matched > 1;
                ok = matched > 1;
                break;
            }
            Atom *makes = row->expr.elems[2];
            CettaExprLen count = comma ? makes->expr.len - 1 : 1;
            for (CettaExprIndex j = 0; ok && j < count; ++j) {
                Atom *item = comma ? makes->expr.elems[j + 1] : makes;
                int normalized = 1;
                item = contextual_normalize(machine, &next, item, &normalized);
                if (!item || !abt_scope_check(&machine->program->binders, 0, item)) {
                    raise_error(machine, makes, "IllScopedPublication"); ok = false; break;
                }
                if (transaction_add(&transaction, destination, item) || transaction.failed) { ok = false; break; }
                ok = answer_bank_add(&made, item, &next, NULL, NULL);
            }
        }
    } else if (outcome == CETTA_TRANSFORM_DECLINED && ok && !transaction.pending) {
        raise_error(machine, call, "MalformedTransform");
    }
    Atom *result = ok && !machine->error && !transaction.pending &&
        machine->episode.completion == CETTA_EVAL_COMPLETE ?
        tm_answers_publish(machine->context, &machine->arena, &made, NULL, false, NULL, 0) : NULL;
    if (outcome == CETTA_TRANSFORM_FIRED && result) {
        Space **destinations = work(machine, transaction.len * sizeof *destinations);
        Space **prepared = work(machine, transaction.len * sizeof *prepared);
        SpaceReadToken *versions = work(machine, transaction.len * sizeof *versions);
        if (transaction.len && (!destinations || !prepared || !versions)) ok = false;
        size_t count = 0;
        for (size_t i = 0; ok && i < transaction.len; ++i) {
            TmTransactionSpace *entry = &transaction.spaces[i];
            if (!space_read_token_matches_live_space(entry->version, entry->source)) {
                machine->episode.completion = CETTA_EVAL_INCOMPLETE_INVALIDATED;
                ok = false; break;
            }
            if (entry->changed) {
                destinations[count] = entry->source; prepared[count] = entry->prepared;
                versions[count++] = entry->version;
            }
        }
        if (ok && !space_publish_native_replacements(destinations, prepared, versions, count)) {
            raise_error(machine, call, "TransformPublicationRefused"); ok = false;
        }
        if (ok) { bindings_free(&branch->bindings); branch->bindings = next; bindings_init(&next); }
    }
    machine->transaction = previous;
    for (size_t i = 0; i < transaction.len; ++i) {
        space_free(transaction.spaces[i].prepared); free(transaction.spaces[i].prepared);
    }
    free(transaction.spaces);
    bindings_free(&next);
    answer_bank_free(&made);
    if (machine->error) return true;
    if (transaction.pending) return suspend_read(machine, branch, call);
    if (machine->episode.completion != CETTA_EVAL_COMPLETE) return true;
    if (!ok || !result) return false;
    branch->code = NULL;
    branch->value = result;
    return true;
}

static bool effect(TmMachine *machine, TmBranch *branch, Atom *call) {
    Atom **args = call->expr.elems;
    uint64_t n = call->expr.len;
    if (machine->read_only || branch->collector->caller) {
        raise_error(machine, call, "ReadOnlyCollection"); return true;
    }
    Atom *answer = NULL;
    if (tm_named(args[0], "new-space") && n == 1) {
        answer = eval_new_managed_space(machine->space, &machine->arena);
    } else if (tm_named(args[0], "bind!") && n == 3) {
        if (args[1]->kind != ATOM_SYMBOL) { raise_error(machine, call, "ExpectedRegistryName"); return true; }
        Arena *persistent = cetta_library_context_universe(machine->context)->persistent_arena;
        Atom *owned = atom_deep_copy(persistent, args[2]);
        if (!owned) return false;
        registry_bind_id(machine->registry, args[1]->sym_id, owned);
        answer = atom_unit(&machine->arena);
    } else if ((tm_named(args[0], "add-atom") || tm_named(args[0], "remove-atom")) && n == 3) {
        Space *space = knowledge_space(machine, args[1]);
        if (!space) { raise_error(machine, call, "ExpectedKnowledgeSpace"); return true; }
        if (tm_named(args[0], "add-atom")) {
            if (!space_admit_scoped_atom(space, &machine->arena, args[2])) return false;
        } else (void)space_remove(space, args[2]);
        answer = atom_unit(&machine->arena);
    } else if (tm_named(args[0], "import!") && n == 3) {
        Space *space = knowledge_space(machine, args[1]);
        if (!space) { raise_error(machine, call, "ExpectedKnowledgeSpace"); return true; }
        Atom *spec = args[2];
        const char *path = spec->kind == ATOM_GROUNDED && spec->ground.gkind == GV_STRING ? spec->ground.sval :
            spec->kind == ATOM_SYMBOL ? symbol_bytes(g_symbols, spec->sym_id) : NULL;
        if (!path) { raise_error(machine, call, "ExpectedModuleName"); return true; }
        Atom *error = NULL;
        if (!cetta_library_import_module(machine->context, path, space, false, &machine->arena,
            cetta_library_context_universe(machine->context)->persistent_arena, machine->registry, -1, NULL, &error)) {
            if (error) machine->error = error;
            else raise_error(machine, call, "ImportFailed");
            return true;
        }
        answer = atom_unit(&machine->arena);
    } else if (tm_named(args[0], "transform")) return transform(machine, branch, call);
    else { raise_error(machine, call, "MalformedEffect"); return true; }
    if (!answer) return false;
    branch->code = NULL;
    branch->value = answer;
    return true;
}

static bool scalar_call(const Atom *call) {
    if (call->kind != ATOM_EXPR || !call->expr.len ||
        call->expr.elems[0]->kind != ATOM_SYMBOL || tm_named(call->expr.elems[0], "ctx"))
        return false;
    for (CettaExprIndex i = 1; i < call->expr.len; ++i)
        if (!tm_scalar_leaf(call->expr.elems[i])) return false;
    return true;
}

/* A single scalar match uses the same epoch and matcher as a fork, with
 * a trail in place of copying the live frame directory. */
static int match_scalar_in_place(TmMachine *machine, TmBranch *branch, TmScope *scope,
    BindingsFrameSchema *schema, Atom *pattern, Atom *value) {
    /* The empty writer keeps its scratch arrays between trials. The live
     * binding image returns to the branch before any continuation runs. */
    BindingsBuilder *trail = &machine->scalar_trail;
    bindings_move(&trail->current, &branch->bindings);
    uint32_t mark = bindings_builder_save(trail);
    bool registered = !bindings_frame_schema_len(schema) ||
        bindings_builder_register_complete_frame_schema(trail, schema, scope->identity);
    int result = registered ? match_data_internal(machine, &trail->current, pattern, value, trail) : -1;
    if (tm_capacity_failed()) result = -1;
    if (result != 1) bindings_builder_rollback(trail, mark);
    bindings_builder_commit(trail);
    bindings_move(&branch->bindings, &trail->current);
    return result;
}

static int scalar_equation(TmMachine *machine, TmBranch *branch,
    const TmEquation *equation, Atom *call) {
    if (call->expr.len != equation->left->expr.len)
        return 0;
    TmScope *scope = fresh_scope(machine, NULL, equation->schema, NULL);
    Atom *opened = scope ? syntax(machine, scope, equation->left) : NULL;
    if (!opened) return -1;
    int result = match_scalar_in_place(machine, branch, scope, equation->schema, opened, call);
    if (result == 1) {
        branch->scope = scope;
        branch->code = equation->body;
        branch->value = NULL;
    }
    return result;
}

static bool dispatch_call(TmMachine *machine, TmBranch *branch, Atom *call, Atom *return_type) {
    bool scalar = scalar_call(call);
    if (!scalar) {
        int normalized = 1;
        call = contextual_normalize(machine, &branch->bindings, call, &normalized);
        if (!call) {
            if (normalized == 0) { raise_error(machine, branch->code->source, "ContextualArityRefused"); return true; }
            return false;
        }
    }
    Atom *head = call;
    if (call->kind == ATOM_EXPR && call->expr.len) head = call->expr.elems[0];
    if (branch->code->op == TM_EFFECT) return effect(machine, branch, call);
    if (branch->code->op == TM_UNIFY) {
        int matched = match_data(machine, &branch->bindings, call->expr.elems[1], call->expr.elems[2]);
        if (matched < 0) return false;
        if (!matched) return finish_branch(machine, branch, false);
        branch->code = NULL;
        branch->value = atom_bool(&machine->arena, true);
        if (matched > 1) { branch->waiting = true; suspend(machine, branch); }
        return true;
    }
    SymbolId operation = primitive(head);
    if (operation != SYMBOL_ID_NONE && atom_has_vars(call)) {
        if (call->kind != ATOM_EXPR ||
            !tm_is_sequence_operation(operation, (uint32_t)(call->expr.len - 1)))
            return suspend_read(machine, branch, call);
        /* The projection needs the sequence shape and any index. Its
         * elements can be open syntax, including captured variables. */
        Atom *waiting[2];
        CettaExprLen count = 0;
        for (CettaExprIndex i = 1; i < call->expr.len; ++i)
            if (call->expr.elems[i]->kind == ATOM_VAR) waiting[count++] = call->expr.elems[i];
        if (count)
            return suspend_read(machine, branch, count == 1 ? waiting[0] :
                atom_expr(&machine->arena, waiting, count));
    }
    if (return_type) {
        TmContinuation *check = continuation(machine, branch, K_CHECK, branch->code);
        if (!check) return false;
        check->expected = return_type;
        branch->continuation = check;
    }
    if (operation != SYMBOL_ID_NONE && call->kind == ATOM_EXPR) {
        Atom **arguments = call->expr.elems + 1;
        uint32_t arity = (uint32_t)(call->expr.len - 1);
        CettaCallOutcome result;
        Atom *op = operation == head->sym_id ? head : atom_symbol_id(&machine->arena, operation);
        if (!op) return false;
        if (tm_answers_project(machine->context, &machine->arena, operation, arguments,
                arity, branch->code->source, &branch->bindings, &result) ||
            grounded_call(&machine->arena, op, arguments, arity, &result)) {
            switch (result.kind) {
            case CETTA_CALL_VALUE:
                branch->code = NULL;
                branch->value = result.term;
                return true;
            case CETTA_CALL_FAILURE: return finish_branch(machine, branch, false);
            case CETTA_CALL_RAISED: machine->error = result.term; return true;
            case CETTA_CALL_INTERRUPTED: return false;
            case CETTA_CALL_SUSPENDED:
                return suspend_read(machine, branch, result.delayed ? result.delayed : call);
            }
        }
    }
    bool matched = false, pending = false;
    const TmProgram *program = machine->program;
    SymbolId key = tm_equation_head(call);
    size_t named = 0, named_end = program->len, wildcard = 0, wildcard_end = 0;
    if (key != SYMBOL_ID_NONE) {
        tm_equation_range(program, key, &named, &named_end);
        tm_equation_range(program, SYMBOL_ID_NONE, &wildcard, &wildcard_end);
        if (scalar && named_end - named == 1 && wildcard == wildcard_end) {
            const TmEquation *equation = &program->equations[program->by_head[named].ordinal];
            int selected = equation->scalar_pattern ? scalar_equation(machine, branch, equation, call) : 2;
            if (selected < 0) return false;
            if (selected == 1) return true;
            if (selected == 0) {
                branch->code = NULL;
                branch->value = call;
                return true;
            }
        }
    }
    while (named < named_end || wildcard < wildcard_end) {
        size_t i;
        if (key == SYMBOL_ID_NONE) i = named++;
        else if (wildcard == wildcard_end || (named < named_end &&
                 program->by_head[named].ordinal < program->by_head[wildcard].ordinal))
            i = program->by_head[named++].ordinal;
        else i = program->by_head[wildcard++].ordinal;
        const TmEquation *equation = &program->equations[i];
        TmBranch *candidate = clone_branch(machine, branch);
        if (!candidate) return false;
        TmScope *scope = open_scope(machine, NULL, equation->schema, &candidate->bindings);
        if (!scope) return false;
        Atom *opened = syntax(machine, scope, equation->left);
        if (!opened) return false;
        int matched_data = match_data(machine, &candidate->bindings, opened, call);
        if (matched_data < 0) return false;
        if (!matched_data) {
            if (!finish_branch(machine, candidate, false)) return false;
            continue;
        }
        candidate->scope = scope;
        candidate->code = equation->body;
        candidate->value = NULL;
        if (matched_data == 2) {
            pending = true;
            candidate->waiting = true;
            suspend(machine, candidate);
        } else {
            matched = true;
            schedule(machine, candidate);
        }
    }
    if (matched || pending) return finish_branch(machine, branch, false);
    branch->code = NULL;
    branch->value = call;
    return true;
}

static bool call_arguments(TmMachine *machine, TmBranch *branch, TmCode *code,
    uint32_t first, Atom **arguments, Atom *head, const TmDeclaration *declaration, Atom *type) {
    for (uint32_t i = first; i < code->arity; ++i) {
        if (argument_is_value(machine, code, head, declaration, i)) {
            TmContinuation *frame = continuation(machine, branch, K_ARGUMENT, code);
            if (!frame) return false;
            frame->position = i;
            frame->arguments = arguments;
            frame->expected = type;
            frame->declaration = declaration;
            branch->continuation = frame;
            branch->code = code->children[i];
            branch->value = NULL;
            return true;
        }
        TmCode *operand = code->children[i];
        if (operand->op == TM_HOLD) operand = operand->children[0];
        arguments[i + 1] = quote_text(machine, branch, operand, branch->scope);
        if (!arguments[i + 1]) return false;
    }
    arguments[0] = head;
    Atom *call = code->head ?
        atom_expr(&machine->arena, arguments, code->arity + 1) : head;
    if (!call) return false;
    TmContinuation *dispatch = continuation(machine, branch, K_DISPATCH, code);
    if (!dispatch) return false;
    dispatch->call = call;
    dispatch->expected = type ? type->expr.elems[declaration->count + 1] : NULL;
    branch->continuation = dispatch;
    branch->code = NULL;
    branch->value = call;
    return true;
}

static bool reflect(TmMachine *machine, TmBranch *branch, Atom *bag) {
    TmAnswers *answers = tm_answers_get(machine->context, bag);
    if (answers) {
        for (CettaIndex i = 0; i < answers->bank.len; ++i) {
            CettaIndex index = answers->ordered ? answers->bank.len - 1 - i : i;
            const AnswerRecord *record = answer_bank_get(&answers->bank, index);
            TmBranch *alternative = clone_branch(machine, branch);
            if (!alternative) return false;
            int opened = tm_answer_open(&machine->arena, record, &alternative->bindings,
                &alternative->value);
            if (opened < 0) return false;
            if (!opened) {
                if (!finish_branch(machine, alternative, false)) return false;
                continue;
            }
            alternative->code = NULL;
            schedule(machine, alternative);
        }
    } else {
        Atom *const *elements;
        CettaExprLen length;
        if (!atom_sequence_view(bag, &elements, &length)) {
            raise_error(machine, bag, "ExpectedAnswerSpaceOrSequence");
            return true;
        }
        bool ordered = atom_is_list(bag);
        for (CettaExprIndex i = 0; i < length; ++i) {
            TmBranch *alternative = clone_branch(machine, branch);
            if (!alternative) return false;
            alternative->code = NULL;
            alternative->value = elements[ordered ? length - 1 - i : i];
            schedule(machine, alternative);
        }
    }
    return finish_branch(machine, branch, false);
}

static bool scalar_binding(const TmCode *code, const Atom *value) {
    return value && code->children[code->binding - 1]->source->kind == ATOM_VAR &&
        tm_scalar_leaf(value);
}

static bool bind_value(TmMachine *machine, TmBranch *branch, TmCode *code,
    TmScope *parent, TmCode *body) {
    bool scalar = scalar_binding(code, branch->value);
    TmScope *scope = scalar ? fresh_scope(machine, parent, code->schema, code->bound_names) :
        open_scope_named(machine, parent, code->schema, &branch->bindings, code->bound_names);
    if (!scope) return false;
    Atom *pattern = syntax(machine, scope, code->children[code->binding - 1]->source);
    if (!pattern) return false;
    int matched = scalar ? match_scalar_in_place(machine, branch, scope, code->schema,
        pattern, branch->value) : match_data(machine, &branch->bindings, pattern, branch->value);
    if (matched < 0) return false;
    if (matched == 0) return finish_branch(machine, branch, false);
    branch->scope = scope;
    branch->code = body;
    branch->value = NULL;
    if (matched > 1) { branch->waiting = true; suspend(machine, branch); }
    return true;
}

static bool enter_runtime_code(TmMachine *machine, TmBranch *branch, Atom *owned) {
    TmCode *code = tm_compile(&machine->query, owned);
    if (!code) return false;
    BindingsFrameSchema *schema = tm_schema(owned);
    TmScope *scope = schema ? open_scope(machine, NULL, schema, &branch->bindings) : NULL;
    bindings_frame_schema_release(schema);
    if (!scope) return false;
    branch->code = code;
    branch->scope = scope;
    branch->value = NULL;
    return true;
}

static bool force_need(TmMachine *machine, TmBranch *branch, Atom *reference) {
    const PrimeNeedSnapshot *base = bindings_need_view(&branch->bindings);
    uint64_t id;
    if (reference->kind == ATOM_VAR) return suspend_read(machine, branch, reference);
    if (!prime_need_ref_belongs_to(reference, base, &id)) {
        raise_error(machine, reference, "ExpectedNeedInCurrentBranch"); return true;
    }
    PrimeNeedCellView cell;
    if (!prime_need_snapshot_lookup(base, id, &cell)) return false;
    if (cell.cache_state == PRIME_NEED_CACHE_VALUE) {
        branch->code = NULL;
        branch->value = atom_deep_copy_single(&machine->arena, cell.cached);
        return branch->value != NULL;
    }
    if (cell.cache_state == PRIME_NEED_CACHE_STABLE_FAULT) {
        machine->error = cell.cached; return true;
    }
    if (cell.cache_state == PRIME_NEED_CACHE_EVALUATING) { suspend(machine, branch); return true; }
    Atom *origin = atom_deep_copy_single(&machine->query.arena, cell.origin);
    if (!origin) return false;
    TmContinuation *memo = continuation(machine, branch, K_MEMO, branch->code);
    CettaFrameIdentity evaluator;
    PrimeNeedSnapshot *next = bindings_need_mut(&branch->bindings);
    if (!memo || !next || !cetta_frame_identity_scope_try(&machine->frames, &evaluator)) return false;
    if (!prime_need_snapshot_start_evaluation(&machine->arena, base, id, evaluator.word, next)) return false;
    memo->expected = reference;
    memo->evaluator = evaluator.word;
    branch->continuation = memo;
    return enter_runtime_code(machine, branch, origin);
}

static bool exact_number(const Atom *term) {
    return term && term->kind == ATOM_GROUNDED &&
        (term->ground.gkind == GV_INT || term->ground.gkind == GV_BIGINT ||
         term->ground.gkind == GV_RATIONAL);
}

static bool boolean(const Atom *term) {
    return term && term->kind == ATOM_GROUNDED && term->ground.gkind == GV_BOOL;
}

static bool apply_continuation(TmMachine *machine, TmBranch *branch) {
    TmContinuation *frame = branch->continuation;
    branch->continuation = frame->next;
    branch->scope = frame->scope;
    switch (frame->kind) {
    case K_CLOSE:
        branch->value = publish_collection(machine, frame->collector);
        return branch->value != NULL;
    case K_DISPATCH: {
        if (!refresh_program(machine)) return false;
        if (machine->error) return true;
        Atom *call = frame->code->head ? frame->call :
            value(machine, branch, frame->code->source);
        if (!call) return false;
        branch->code = frame->code;
        bool ok = dispatch_call(machine, branch, call, frame->expected);
        if ((machine->active == branch && machine->episode.completion != CETTA_EVAL_COMPLETE) ||
            machine->suspended == branch) {
            branch->code = NULL;
            branch->continuation = frame;
        }
        return ok;
    }
    case K_BIND: {
        return bind_value(machine, branch, frame->code, frame->scope, frame->code->children[2]);
    }
    case K_ARGUMENT: {
        Atom **arguments = work(machine, sizeof *arguments * (frame->code->arity + 1));
        if (!arguments) return false;
        memcpy(arguments, frame->arguments, sizeof *arguments * (frame->code->arity + 1));
        arguments[frame->position + 1] = branch->value;
        Atom *type = frame->expected;
        const TmDeclaration *declaration = frame->declaration;
        if (declaration && frame->position < declaration->count &&
            frame->code->children[frame->position]->op != TM_RUN) {
            if (!check_value(machine, branch, type->expr.elems[frame->position + 1], branch->value)) return false;
            if (machine->active != branch) { branch->continuation = frame; return true; }
            if (machine->error) return true;
        }
        branch->code = frame->code;
        Atom *head = value(machine, branch, frame->code->head->source);
        return head && call_arguments(machine, branch, frame->code,
            frame->position + 1, arguments, head, declaration, type);
    }
    case K_REFLECT:
        if (branch->value->kind == ATOM_VAR) {
            branch->continuation = frame;
            return suspend_read(machine, branch, branch->value);
        }
        return reflect(machine, branch, branch->value);
    case K_FORCE: {
        bool ok = force_need(machine, branch, branch->value);
        if (machine->active != branch) branch->continuation = frame;
        return ok;
    }
    case K_COUNT: case K_EMPTY: {
        if (branch->value->kind == ATOM_VAR) {
            branch->continuation = frame;
            return suspend_read(machine, branch, branch->value);
        }
        TmAnswers *answers = tm_answers_get(machine->context, branch->value);
        if (!answers) { raise_error(machine, branch->value, "ExpectedAnswerSpace"); return true; }
        branch->value = frame->kind == K_COUNT ? atom_int(&machine->arena, (int64_t)answers->bank.len) :
            atom_bool(&machine->arena, answers->bank.len == 0);
        break;
    }
    case K_IF: {
        if (branch->value->kind == ATOM_VAR) {
            branch->continuation = frame;
            return suspend_read(machine, branch, branch->value);
        }
        bool truth = tm_named(branch->value, "True") ||
            (branch->value->kind == ATOM_GROUNDED && branch->value->ground.gkind == GV_BOOL && branch->value->ground.bval);
        branch->code = frame->code->children[truth ? 1 : 2];
        branch->value = NULL;
        break;
    }
    case K_MATCH: {
        Space *space = knowledge_space(machine, branch->value);
        if (!space) {
            if (branch->value->kind == ATOM_VAR) {
                branch->continuation = frame;
                return suspend_read(machine, branch, branch->value);
            }
            raise_error(machine, branch->value, "ExpectedKnowledgeSpace");
            return true;
        }
        if (!observe_space(machine, branch->collector, space)) return false;
        Atom *pattern = syntax(machine, frame->scope, frame->code->children[1]->source);
        if (!pattern) return false;
        SpaceReadToken version = space_read_token(space);
        CettaCount count = space_view_length64(space);
        for (CettaIndex i = 0; i < count; ++i) {
            Atom *row = space_view_get_at64(space, i);
            if (!space_read_token_matches_live_space(version, space)) {
                machine->episode.completion = CETTA_EVAL_INCOMPLETE_INVALIDATED; return true;
            }
            TmBranch *alternative = clone_branch(machine, branch);
            if (!alternative) return false;
            Atom *owned = row ? atom_deep_copy(&machine->arena, row) : NULL;
            BindingsFrameSchema *schema = owned ? tm_schema(owned) : NULL;
            TmScope *scope = schema ? open_scope(machine, NULL, schema, &alternative->bindings) : NULL;
            bindings_frame_schema_release(schema);
            Atom *opened = scope ? syntax(machine, scope, owned) : NULL;
            if (!opened) return false;
            int matched_data = match_data(machine, &alternative->bindings, pattern, opened);
            if (matched_data < 0) return false;
            if (!matched_data) {
                if (!finish_branch(machine, alternative, false)) return false;
                continue;
            }
            alternative->code = NULL;
            alternative->value = quote_text(machine, alternative, frame->code->children[2], frame->scope);
            if (!alternative->value) return false;
            if (matched_data > 1) { alternative->waiting = true; suspend(machine, alternative); }
            else schedule(machine, alternative);
        }
        return finish_branch(machine, branch, false);
    }
    case K_SERIALIZE: {
        if (branch->value->kind == ATOM_VAR) {
            branch->continuation = frame;
            return suspend_read(machine, branch, branch->value);
        }
        TmAnswers *answers = tm_answers_get(machine->context, branch->value);
        if (!answers) { raise_error(machine, branch->value, "ExpectedAnswerSpace"); return true; }
        CettaCount count = answers->bank.len;
        if (count > SIZE_MAX / sizeof(CettaIndex)) return false;
        if (frame->code->order == TM_ORDER_ANY) {
            if (count > UINT32_MAX) return false;
            TmContinuation *linear = continuation(machine, branch, K_LINEARIZE, frame->code);
            if (!linear) return false;
            linear->position = 0;
            linear->arguments = work(machine, sizeof *linear->arguments);
            linear->order = work(machine, count * sizeof *linear->order);
            if (!linear->arguments || (count && !linear->order)) return false;
            linear->arguments[0] = branch->value;
            branch->continuation = linear;
            return true;
        }
        CettaIndex *order = work(machine, count * sizeof *order);
        if (count && !order) return false;
        int ordered = tm_answers_canonical_order(machine->context, &machine->arena, answers, order);
        if (ordered == 0) {
            raise_error(machine, frame->code->source, "CanonicalAnswerOrderUndefined");
            return true;
        }
        if (ordered < 0) return false;
        branch->value = tm_answers_publish(machine->context, &machine->arena,
            &answers->bank, order, true, answers->local_frames, answers->local_frame_count);
        return branch->value != NULL;
    }
    case K_LINEARIZE: {
        TmAnswers *answers = tm_answers_get(machine->context, frame->arguments[0]);
        if (!answers) return false;
        if (frame->position < answers->bank.len) {
            for (CettaIndex i = 0; i < answers->bank.len; ++i) {
                bool used = false;
                for (uint32_t j = 0; j < frame->position; ++j) if (frame->order[j] == i) used = true;
                if (used) continue;
                TmBranch *alternative = clone_branch(machine, branch);
                if (!alternative) return false;
                TmContinuation *next = continuation(machine, alternative, K_LINEARIZE, frame->code);
                if (!next) return false;
                next->position = frame->position + 1;
                next->arguments = frame->arguments;
                next->order = work(machine, answers->bank.len * sizeof *next->order);
                if (!next->order) return false;
                memcpy(next->order, frame->order, frame->position * sizeof *next->order);
                next->order[frame->position] = i;
                alternative->continuation = next;
                alternative->code = NULL;
                schedule(machine, alternative);
            }
            return finish_branch(machine, branch, false);
        }
        branch->value = tm_answers_publish(machine->context, &machine->arena,
            &answers->bank, frame->order, true, answers->local_frames, answers->local_frame_count);
        return branch->value != NULL;
    }
    case K_FOLD: {
        if (branch->value->kind == ATOM_VAR) {
            branch->continuation = frame;
            return suspend_read(machine, branch, branch->value);
        }
        TmAnswers *answers = tm_answers_get(machine->context, branch->value);
        if (!answers) { raise_error(machine, branch->value, "ExpectedAnswerSpace"); return true; }
        Atom *operation = frame->arguments ? frame->arguments[0] :
            value(machine, branch, frame->code->children[0]->source);
        Atom *accumulator = frame->expected ? frame->expected :
            value(machine, branch, frame->code->children[1]->source);
        if (!operation || !accumulator) return false;
        bool logical = tm_named(operation, "and") || tm_named(operation, "or");
        bool minimum = tm_named(operation, "min"), maximum = tm_named(operation, "max");
        if (!logical && !minimum && !maximum && !tm_named(operation, "+") && !tm_named(operation, "*")) {
            raise_error(machine, operation, "ExpectedCommutativeFold");
            return true;
        }
        if (atom_has_vars(accumulator)) {
            branch->continuation = frame;
            return suspend_read(machine, branch, accumulator);
        }
        if (logical ? !boolean(accumulator) : !exact_number(accumulator)) {
            raise_error(machine, accumulator, "FoldValueRefused");
            return true;
        }
        if (frame->position == answers->bank.len) {
            branch->value = accumulator;
            return true;
        }
        if (frame->position == UINT32_MAX) return false;
        const AnswerRecord *record = answer_bank_get(&answers->bank, frame->position);
        Atom *item = bindings_apply_graph((Bindings *)&record->bindings, &machine->arena, record->result);
        if (!item) return false;
        if (atom_has_vars(item)) {
            branch->continuation = frame;
            suspend(machine, branch);
            return true;
        }
        if (logical ? !boolean(item) : !exact_number(item)) {
            raise_error(machine, item, "FoldValueRefused");
            return true;
        }
        Atom *next_value = NULL;
        if (minimum || maximum) {
            if (!petta_numeric_min_max(&machine->arena, minimum, accumulator, item, &next_value)) return false;
        } else {
            Atom *arguments[] = {accumulator, item};
            next_value = grounded_dispatch(&machine->arena, operation, arguments, 2);
        }
        if (!next_value) return false;
        TmContinuation *next = continuation(machine, branch, K_FOLD, frame->code);
        if (!next) return false;
        next->arguments = frame->arguments ? frame->arguments : work(machine, sizeof *next->arguments);
        if (!next->arguments) return false;
        if (!frame->arguments) next->arguments[0] = operation;
        next->expected = next_value;
        next->position = frame->position + 1;
        branch->continuation = next;
        return true;
    }
    case K_CHECK: {
        bool ok = check_value(machine, branch, frame->expected, branch->value);
        if (machine->active != branch) branch->continuation = frame;
        return ok;
    }
    case K_MEMO: {
        uint64_t id;
        const PrimeNeedSnapshot *base = bindings_need_view(&branch->bindings);
        PrimeNeedSnapshot *next = bindings_need_mut(&branch->bindings);
        if (!next || !prime_need_ref_belongs_to(frame->expected, base, &id)) return false;
        return prime_need_snapshot_resolve_ready_value(&machine->arena, base, id,
            frame->evaluator, branch->value, next);
    }
    }
    return true;
}

static bool execute(TmMachine *machine, TmBranch *branch) {
    TmCode *code = branch->code;
    if (!code) {
        if (branch->continuation) return apply_continuation(machine, branch);
        return finish_branch(machine, branch, true);
    }
    switch (code->op) {
    case TM_DATA: case TM_HOLD:
        if (code->op == TM_HOLD)
            branch->value = quote_text(machine, branch, code->children[0], branch->scope);
        else if (atom_is_list_form(code->source))
            branch->value = quote_text(machine, branch, code, branch->scope);
        else branch->value = value(machine, branch, code->source);
        branch->code = NULL;
        return branch->value != NULL;
    case TM_CALL: case TM_EFFECT: case TM_UNIFY: {
        if (!refresh_program(machine)) return false;
        if (machine->error) return true;
        Atom **arguments = work(machine, (code->arity + 1) * sizeof *arguments);
        Atom *head = value(machine, branch, code->head ? code->head->source : code->source);
        if (!head) return false;
        const TmDeclaration *declaration = code->op == TM_UNIFY ? NULL :
            tm_declaration(machine->program, head);
        Atom *type = declaration ? declaration->type : NULL;
        if (declaration && declaration->schema) {
            TmScope *scope = open_scope(machine, NULL, declaration->schema, &branch->bindings);
            if (!scope) return false;
            type = syntax(machine, scope, type);
            if (!type) return false;
        }
        return arguments && call_arguments(machine, branch, code, 0, arguments, head, declaration, type);
    }
    case TM_LET: return start(machine, branch, K_BIND, code, code->children[2 - code->binding]);
    case TM_DELAY: {
        Atom *origin = quote_text(machine, branch, code->children[code->binding != 0], branch->scope);
        PrimeNeedSnapshot *heap = bindings_need_mut(&branch->bindings);
        if (!origin || !heap) return false;
        if (!prime_need_snapshot_present(heap) && !prime_need_snapshot_begin(heap)) return false;
        uint64_t id;
        if (!prime_need_snapshot_allocate(&machine->arena, heap, origin, heap, &id)) return false;
        branch->value = prime_need_ref(&machine->arena, heap, id);
        branch->code = NULL;
        if (!branch->value) return false;
        if (code->binding) return bind_value(machine, branch, code, branch->scope, code->children[2]);
        return true;
    }
    case TM_FORCE: return start(machine, branch, K_FORCE, code, code->children[0]);
    case TM_NEW: {
        if (machine->read_only || branch->collector->caller) {
            raise_error(machine, code->source, "ReadOnlyCollection"); return true;
        }
        branch->value = eval_new_managed_space(machine->space, &machine->arena);
        return branch->value && bind_value(machine, branch, code, branch->scope, code->children[1]);
    }
    case TM_RUN: {
        TmCode *child = code->children[0];
        if (child->op == TM_HOLD) child = child->children[0];
        Atom *text = child->source;
        if (text->kind != ATOM_VAR) {
            branch->code = child;
        } else {
            text = value(machine, branch, text);
            if (!text) return false;
            if (text->kind == ATOM_VAR) return suspend_read(machine, branch, text);
            Atom *owned = atom_deep_copy_single(&machine->query.arena, text);
            return enter_runtime_code(machine, branch, owned);
        }
        return branch->code != NULL;
    }
    case TM_CHOICE: {
        TmBranch *other = clone_branch(machine, branch);
        if (!other) return false;
        branch->code = code->children[0];
        other->code = code->children[1];
        schedule(machine, other);
        break;
    }
    case TM_FAIL: return finish_branch(machine, branch, false);
    case TM_COLLECT: {
        TmCollector *collector = work(machine, sizeof *collector);
        if (!collector) return false;
        answer_bank_init(&collector->answers);
        collector->parent = branch->collector;
        collector->scope_boundary = machine->scopes;
        collector->program = space_program_token(machine->space);
        collector->next = machine->collectors;
        machine->collectors = collector;
        TmBranch *child = clone_branch(machine, branch);
        if (!child) return false;
        /* Undo the clone's count when moving it to the child collector.
         * The parked caller keeps its place in the parent collector. */
        --branch->collector->live;
        collector->live = 1;
        collector->caller = branch;
        child->collector = collector;
        child->continuation = NULL;
        child->code = code->children[0];
        branch->code = NULL;
        machine->active = NULL;
        schedule(machine, child);
        return true;
    }
    case TM_REFLECT:
        if (code->superpose &&
            code->children[0]->op == TM_CALL && code->children[0]->source->kind == ATOM_EXPR) {
            Atom *sequence = quote_text(machine, branch, code->children[0], branch->scope);
            return sequence && reflect(machine, branch, sequence);
        }
        return start(machine, branch, K_REFLECT, code, code->children[0]);
    case TM_COUNT: return start(machine, branch, K_COUNT, code, code->children[0]);
    case TM_EMPTY: return start(machine, branch, K_EMPTY, code, code->children[0]);
    case TM_SERIALIZE:
        if (code->order == TM_ORDER_INVALID) {
            raise_error(machine, code->source,
                code->arity == 1 ? "ExplicitOrderRequired" : "UnknownSerializeMode");
            return true;
        }
        return start(machine, branch, K_SERIALIZE, code, code->children[code->arity - 1]);
    case TM_FOLD: return start(machine, branch, K_FOLD, code, code->children[2]);
    case TM_MATCH: return start(machine, branch, K_MATCH, code, code->children[0]);
    case TM_IF: return start(machine, branch, K_IF, code, code->children[0]);
    case TM_CHECK: {
        TmContinuation *frame = continuation(machine, branch, K_CHECK, code);
        if (!frame) return false;
        frame->expected = code->children[0]->source;
        branch->continuation = frame;
        branch->code = code->children[1];
        break;
    }
    }
    return true;
}

static bool machine_start(TmMachine *machine, CettaLibraryContext *context,
    Space *space, Registry *registry, Atom *expression, const Bindings *seed,
    TmMachine *parent, bool read_only) {
    machine->context = context;
    machine->space = space;
    machine->registry = registry;
    machine->transaction = parent ? parent->transaction : NULL;
    machine->read_only = read_only;
    machine->values_only = parent == NULL;
    machine->eager = context->session.profile &&
        context->session.profile->id == CETTA_PROFILE_TMETTA_EAGER;
    answer_bank_init(&machine->root.answers);
    if (!bindings_builder_init(&machine->scalar_trail, NULL) ||
        !arena_try_init_detached(&machine->arena) ||
        !arena_try_init_detached(&machine->query.arena) ||
        !refresh_program(machine)) return false;
    if (machine->error) return true;
    TmBranch *branch = calloc(1, sizeof *branch);
    if (!branch) return false;
    bindings_init(&branch->bindings);
    if (seed && !bindings_clone(&branch->bindings, seed)) {
        bindings_free(&branch->bindings);
        free(branch);
        return false;
    }
    Atom *source = atom_deep_copy(&machine->query.arena, expression);
    branch->code = tm_compile(&machine->query, source);
    BindingsFrameSchema *schema = source ? tm_schema(source) : NULL;
    branch->scope = schema ? open_scope(machine, NULL, schema, &branch->bindings) : NULL;
    bindings_frame_schema_release(schema);
    if (!branch->code || !branch->scope) {
        bindings_free(&branch->bindings);
        free(branch);
        return false;
    }
    branch->collector = &machine->root;
    machine->input_scope = branch->scope;
    machine->root.live = 1;
    machine->branches = branch;
    schedule(machine, branch);
    return true;
}

/* These instructions either leave the current image alone or restore their
 * own writes on failure. Control and queue rollback always apply. */
static bool preserves_bindings_on_failure(const TmBranch *branch) {
    if (branch->code) {
        switch (branch->code->op) {
        case TM_DATA: case TM_HOLD: case TM_LET: case TM_CHOICE: case TM_FAIL:
        case TM_COLLECT: case TM_REFLECT: case TM_SERIALIZE: case TM_COUNT:
        case TM_EMPTY: case TM_MATCH: case TM_IF: case TM_CHECK: case TM_FOLD:
        case TM_FORCE:
            return true;
        default:
            return false;
        }
    }
    if (!branch->continuation) return true;
    switch (branch->continuation->kind) {
    case K_BIND:
        return scalar_binding(branch->continuation->code, branch->value);
    case K_DISPATCH: {
        const TmContinuation *frame = branch->continuation;
        if (frame->code->op != TM_CALL || !frame->code->head) return false;
        Atom *head = frame->call->expr.elems[0];
        return head->kind == ATOM_SYMBOL && !tm_named(head, "ctx") &&
            !tm_is_sequence_operation(primitive(head), frame->code->arity);
    }
    case K_CLOSE: case K_REFLECT: case K_SERIALIZE: case K_COUNT: case K_EMPTY:
    case K_IF: case K_MATCH: case K_LINEARIZE: case K_FOLD:
        return true;
    default:
        return false;
    }
}

static void machine_run(TmMachine *machine) {
    while (machine->pending && !machine->error &&
           machine->episode.completion == CETTA_EVAL_COMPLETE) {
        if (eval_native_interrupted()) {
            machine->episode.completion = tm_capacity_failed() ?
                CETTA_EVAL_INCOMPLETE_CAPACITY : CETTA_EVAL_INCOMPLETE_CANCELLED;
            break;
        }
        if (!cetta_eval_episode_step(&machine->episode, 1)) break;
        TmBranch *branch = machine->pending;
        machine->pending = branch->next;
        branch->next = NULL;
        machine->active = branch;
        TmBranch saved = *branch;
        Bindings saved_bindings;
        bindings_init(&saved_bindings);
        TmBranch *allocated = machine->branches;
        TmBranch *pending = machine->pending;
        TmBranch *suspended = machine->suspended;
        bool checkpoint = !preserves_bindings_on_failure(branch);
        if (checkpoint && !bindings_clone(&saved_bindings, &branch->bindings)) {
            bindings_free(&saved_bindings);
            schedule(machine, branch);
            machine->active = NULL;
            machine->episode.completion = CETTA_EVAL_INCOMPLETE_CAPACITY;
            break;
        }
        if (!execute(machine, branch)) {
            machine->pending = pending;
            machine->suspended = suspended;
            while (machine->branches != allocated) drop_branch(machine, machine->branches);
            if (machine->active == branch) {
                if (checkpoint) bindings_replace(&branch->bindings, &saved_bindings);
                branch->code = saved.code;
                branch->value = saved.value;
                branch->scope = saved.scope;
                branch->continuation = saved.continuation;
                branch->waiting = saved.waiting;
                branch->residual = saved.residual;
                schedule(machine, branch);
                machine->active = NULL;
            }
            bindings_free(&saved_bindings);
            if (!machine->error) cetta_eval_completion_record(&machine->episode.completion,
                CETTA_EVAL_INCOMPLETE_CAPACITY);
            break;
        }
        bindings_free(&saved_bindings);
        if (machine->active) {
            schedule(machine, machine->active);
            machine->active = NULL;
        }
    }
}

static void machine_dispose(TmMachine *machine) {
    while (machine->branches) drop_branch(machine, machine->branches);
    bindings_builder_free(&machine->scalar_trail);
    for (TmCollector *collector = machine->collectors; collector; collector = collector->next)
        answer_bank_free(&collector->answers);
    for (TmScope *scope = machine->scopes; scope; scope = scope->allocated_next)
        bindings_frame_schema_release(scope->schema);
    answer_bank_free(&machine->root.answers);
    cetta_frame_identity_scope_clear(&machine->frames);
    tm_program_dispose(&machine->query);
    for (TmProgramPin *pin = machine->program_pins; pin; pin = pin->next)
        tm_program_release(pin->program);
    arena_free(&machine->arena);
}

static CettaCallOutcome machine_outcome(TmMachine *machine, Arena *output) {
    if (machine->episode.completion != CETTA_EVAL_COMPLETE)
        return cetta_call_interrupted();
    if (machine->error) {
        Atom *error = atom_deep_copy(output, machine->error);
        if (error) return cetta_call_raised(error);
        machine->episode.completion = CETTA_EVAL_INCOMPLETE_CAPACITY;
        return cetta_call_interrupted();
    }
    if (machine->suspended) {
        TmBranch *branch = machine->suspended;
        TmCode *code = branch->code;
        if (!code && branch->continuation) code = branch->continuation->code;
        Atom *goal = code ? code->source : branch->value;
        return (CettaCallOutcome){.kind = CETTA_CALL_SUSPENDED, .term = goal,
            .delayed = branch->residual};
    }
    const AnswerRecord *first = answer_bank_get(&machine->root.answers, 0);
    return first ? cetta_call_completed_value(first->result) : cetta_call_failure();
}

static CettaCallOutcome evaluate(CettaLibraryContext *context, Space *space,
    Registry *registry, Arena *output, Atom *expression, int fuel,
    const Bindings *seed, TmMachine *parent, bool read_only,
    AnswerBank *answers, CettaEvalEpisode *episode) {
    TmMachine machine = {0};
    machine.episode = cetta_eval_episode_begin(&parent->episode, fuel);
    if (machine_start(&machine, context, space, registry, expression, seed, parent, read_only))
        machine_run(&machine);
    else machine.episode.completion = CETTA_EVAL_INCOMPLETE_CAPACITY;
    CettaCallOutcome outcome = machine_outcome(&machine, output);
    if (outcome.kind == CETTA_CALL_SUSPENDED) {
        outcome.term = expression;
        outcome.delayed = NULL;
    }
    if (outcome.kind == CETTA_CALL_VALUE) {
        CettaIndex offset = answers->len;
        if (answer_bank_append(answers, &machine.root.answers))
            outcome = cetta_call_completed_value(answer_bank_get(answers, offset)->result);
        else {
            machine.episode.completion = CETTA_EVAL_INCOMPLETE_CAPACITY;
            outcome = cetta_call_interrupted();
        }
    }
    (void)cetta_eval_episode_end(&machine.episode);
    *episode = machine.episode;
    machine_dispose(&machine);
    return outcome;
}

static void machine_export(TmMachine *machine, Arena *output, EvalOutcome *result) {
    CettaCallOutcome outcome = machine_outcome(machine, output);
    size_t initial = result->results.len;
    ArenaMark mark = arena_mark(output);
    if (outcome.kind == CETTA_CALL_RAISED) {
        result_set_add(&result->results, outcome.term);
        if (result->results.len == initial)
            machine->episode.completion = CETTA_EVAL_INCOMPLETE_CAPACITY;
    } else if (outcome.kind == CETTA_CALL_VALUE) {
        AnswerBank *answers = &machine->root.answers;
        for (CettaIndex i = 0; i < answers->len; ++i) {
            const AnswerRecord *record = answer_bank_get(answers, i);
            Atom *observed = bindings_apply_graph((Bindings *)&record->bindings, output, record->result);
            Atom *answer = observed ? atom_deep_copy(output, observed) : NULL;
            if (answer) result_set_add(&result->results, answer);
            if (result->results.len != initial + i + 1) {
                result->results.len = initial;
                arena_reset(output, mark);
                machine->episode.completion = CETTA_EVAL_INCOMPLETE_CAPACITY;
                break;
            }
        }
    }
    result->completion = outcome.kind == CETTA_CALL_SUSPENDED ?
        CETTA_EVAL_INCOMPLETE_SUSPENDED : machine->episode.completion;
    result->budget_limited = machine->episode.budget_limited;
    result->budget_initial = machine->episode.budget_initial;
    result->budget_remaining = machine->episode.budget_remaining;
    result->steps_spent = machine->episode.steps_spent;
}

static void frontier_free(void *resource) {
    TmMachine *machine = resource;
    machine_dispose(machine);
    free(machine);
}

void cetta_tmetta_eval(CettaLibraryContext *context, Space *space,
    Registry *registry, Arena *output, Atom *expression, int fuel, EvalOutcome *result) {
    CETTA_SCOPED_SHARED_TRANSITION(transition);
    struct CettaTmettaRuntime *runtime = tm_runtime(context);
    if (!runtime || pthread_mutex_lock(&runtime->mutex)) {
        result->completion = CETTA_EVAL_INCOMPLETE_CAPACITY;
        return;
    }
    result->frontier = NULL;
    TmMachine *machine = calloc(1, sizeof *machine);
    if (!machine || !machine_start(machine, context, space, registry, expression, NULL, NULL, false)) {
        result->completion = CETTA_EVAL_INCOMPLETE_CAPACITY;
        if (machine) frontier_free(machine);
        pthread_mutex_unlock(&runtime->mutex);
        return;
    }
    machine->episode = cetta_eval_episode_begin(NULL, fuel);
    machine_run(machine);
    machine_export(machine, output, result);
    uint64_t id;
    if (result->completion != CETTA_EVAL_COMPLETE && !machine->error &&
        cetta_native_handle_alloc(context, tm_frontier_kind, machine, frontier_free, &id)) {
        machine->checkpoint = space_read_token(space);
        result->frontier = cetta_native_handle_owned_atom(context, output, tm_frontier_kind, id);
        if (!result->frontier) (void)cetta_native_handle_close(context, tm_frontier_kind, id);
    } else frontier_free(machine);
    pthread_mutex_unlock(&runtime->mutex);
}

typedef struct TmWake {
    TmBranch *branch;
    Bindings bindings;
    Atom *value;
    struct TmWake *next;
} TmWake;

/* Stage every wake before changing the frontier. A failed allocation rolls
 * back the shared delay tables and leaves the original branch images intact. */
static bool wake_input(TmMachine *machine, Atom *variable, Atom *given, size_t *count) {
    if (!variable || variable->kind != ATOM_VAR || !given || atom_has_vars(given) ||
        !machine->input_scope || !abt_scope_check(&machine->program->binders, 0, given))
        return true;
    Atom *input = scope_lookup(machine->input_scope, variable->var_id);
    if (!input) {
        if (cetta_frame_identity_is_none(variable->var_id.epoch)) return true;
        input = variable;
    }
    TmWake *ready = NULL, **next = &ready;
    Atom *owned = NULL;
    for (TmBranch *branch = machine->suspended; branch; branch = branch->next) {
        if (!branch->delay || branch->bindings.eq_len) continue;
        Atom *target = bindings_apply_graph(&branch->bindings, &machine->arena, input);
        if (!target) goto failed;
        if (target->kind != ATOM_VAR || !cetta_delay_watches(branch->delay, target->var_id))
            continue;
        if (!owned) owned = atom_deep_copy_single(&machine->arena, given);
        if (!owned) goto failed;
        TmWake *wake = calloc(1, sizeof *wake);
        if (!wake) goto failed;
        wake->branch = branch;
        bindings_init(&wake->bindings);
        *next = wake;
        next = &wake->next;
        if (!bindings_clone(&wake->bindings, &branch->bindings) ||
            !bindings_add_var_acyclic(&wake->bindings, target, owned))
            goto failed;
        if (branch->value) {
            wake->value = bindings_apply_graph(&wake->bindings, &machine->arena, branch->value);
            if (!wake->value) goto failed;
        }
        if (!cetta_delay_bound(branch->delay, 1, target->var_id) ||
            !cetta_delay_has_woken(branch->delay)) goto failed;
    }

    TmBranch **link = &machine->suspended;
    while (ready) {
        TmWake *wake = ready;
        ready = wake->next;
        TmBranch *branch = wake->branch;
        while (*link != branch) link = &(*link)->next;
        *link = branch->next;
        bindings_replace(&branch->bindings, &wake->bindings);
        branch->value = wake->value;
        cetta_delay_service_free(branch->delay);
        branch->delay = NULL;
        branch->residual = NULL;
        schedule(machine, branch);
        ++*count;
        free(wake);
    }
    return true;

failed:
    while (ready) {
        TmWake *wake = ready;
        ready = wake->next;
        cetta_delay_rollback(wake->branch->delay, 1);
        bindings_free(&wake->bindings);
        free(wake);
    }
    return false;
}

size_t cetta_tmetta_resume_with_value(CettaLibraryContext *context, Space *space,
    Registry *registry, Arena *output, Atom *frontier, Atom *variable,
    Atom *value, int fuel, EvalOutcome *result) {
    CETTA_SCOPED_SHARED_TRANSITION(transition);
    struct CettaTmettaRuntime *runtime = context->tmetta_runtime;
    size_t woken = 0;
    result->frontier = NULL;
    result->completion = CETTA_EVAL_INCOMPLETE_INVALIDATED;
    if (!runtime || pthread_mutex_lock(&runtime->mutex)) return 0;
    TmMachine *machine = cetta_native_handle_get_owned(context, frontier, tm_frontier_kind);
    if (machine && registry == machine->registry &&
        space_read_token_matches_live_space(machine->checkpoint, space)) {
        for (TmCollector *collector = machine->collectors; collector; collector = collector->next) {
            if (collector->caller && !collection_current(machine, collector)) {
                pthread_mutex_unlock(&runtime->mutex);
                return 0;
            }
        }
        uint64_t id;
        if (cetta_native_handle_arg(frontier, tm_frontier_kind, &id)) {
            Atom *kept = atom_deep_copy(output, frontier);
            if (!kept) result->completion = CETTA_EVAL_INCOMPLETE_CAPACITY;
            else {
                machine->episode = cetta_eval_episode_begin(NULL, fuel);
                if (wake_input(machine, variable, value, &woken))
                    machine_run(machine);
                else machine->episode.completion = CETTA_EVAL_INCOMPLETE_CAPACITY;
                machine_export(machine, output, result);
                if (result->completion == CETTA_EVAL_COMPLETE) {
                    (void)cetta_native_handle_close(context, tm_frontier_kind, id);
                } else {
                    machine->checkpoint = space_read_token(space);
                    result->frontier = kept;
                }
            }
        }
    }
    pthread_mutex_unlock(&runtime->mutex);
    return woken;
}

void cetta_tmetta_resume(CettaLibraryContext *context, Space *space,
    Registry *registry, Arena *output, Atom *frontier, int fuel, EvalOutcome *result) {
    (void)cetta_tmetta_resume_with_value(context, space, registry, output, frontier,
        NULL, NULL, fuel, result);
}

bool cetta_tmetta_frontier_info(CettaLibraryContext *context, Atom *frontier,
    CettaTmettaFrontier *info) {
    struct CettaTmettaRuntime *runtime = context->tmetta_runtime;
    if (!info || !runtime || pthread_mutex_lock(&runtime->mutex)) return false;
    TmMachine *machine = cetta_native_handle_get_owned(context, frontier, tm_frontier_kind);
    if (machine) {
        *info = (CettaTmettaFrontier){.completed = machine->root.answers.len};
        for (TmBranch *branch = machine->pending; branch; branch = branch->next) ++info->runnable;
        for (TmBranch *branch = machine->suspended; branch; branch = branch->next) ++info->suspended;
        for (TmCollector *collector = machine->collectors; collector; collector = collector->next) {
            if (!collector->caller) continue;
            ++info->collecting;
            for (TmRead *read = collector->reads; read; read = read->next)
                ++info->observed_spaces;
        }
    }
    pthread_mutex_unlock(&runtime->mutex);
    return machine != NULL;
}

#include "tmetta_internal.h"
#include "petta_semantics.h"
#include <stdlib.h>

static void answers_free(void *pointer) {
    TmAnswers *answers = pointer;
    if (answers->backing) {
        answers_free(answers->backing);
    } else {
        if (atomic_fetch_sub_explicit(&answers->references, 1u, memory_order_acq_rel) != 1u)
            return;
        answer_bank_free(&answers->bank);
        cetta_answer_storage_release(answers->storage);
        cetta_answer_storage_account_release(answers->account);
    }
    free(answers);
}

static Atom *publish_answers(CettaLibraryContext *context, Arena *output, TmAnswers *answers) {
    uint64_t id;
    const char *kind = answers->ordered ? "tmetta-sequence" : "tmetta-answers";
    if (!cetta_native_handle_alloc(context, kind, answers, answers_free, &id)) {
        answers_free(answers);
        return NULL;
    }
    Atom *handle = cetta_native_handle_owned_atom(context, output, kind, id);
    if (!handle) cetta_native_handle_close(context, kind, id);
    return handle;
}

TmAnswers *tm_answers_get(CettaLibraryContext *context, Atom *value) {
    TmAnswers *answers = cetta_native_handle_get_owned(context, value, "tmetta-answers");
    return answers ? answers : cetta_native_handle_get_owned(context, value, "tmetta-sequence");
}

Atom *tm_answers_publish(CettaLibraryContext *context, Arena *output,
    const AnswerBank *bank, const CettaIndex *order, bool ordered,
    const CettaFrameIdentity *local_frames, size_t local_frame_count) {
    TmAnswers *answers = calloc(1, sizeof *answers);
    if (!answers) return NULL;
    atomic_init(&answers->references, 1u);
    answers->storage = cetta_answer_storage_new(&answers->account);
    answers->ordered = ordered;
    if (!answers->storage) { cetta_answer_storage_account_release(answers->account); free(answers); return NULL; }
    Arena *arena = &answers->storage->arena;
    answer_bank_init_in(&answers->bank, arena);
    if (local_frame_count) {
        answers->local_frames = arena_alloc(arena, local_frame_count * sizeof *local_frames);
        if (!answers->local_frames) { answers_free(answers); return NULL; }
        memcpy(answers->local_frames, local_frames, local_frame_count * sizeof *local_frames);
        answers->local_frame_count = local_frame_count;
    }
    if (!answer_bank_append_order(&answers->bank, bank, order)) {
        answers_free(answers);
        return NULL;
    }
    return publish_answers(context, output, answers);
}

int tm_answer_open(Arena *arena, const AnswerRecord *record, Bindings *caller,
    Atom **value) {
    Atom *term = variant_instance_present(&record->variant) ?
        variant_instance_materialize(arena, record->result, &record->variant) : record->result;
    if (!term) return -1;
    if (!bindings_try_merge_live(caller, &record->bindings))
        return tm_capacity_failed() ? -1 : 0;
    *value = term;
    return 1;
}

static Atom *answer_tail(CettaLibraryContext *context, Arena *arena, TmAnswers *source) {
    TmAnswers *tail = calloc(1, sizeof *tail);
    if (!tail) return NULL;
    TmAnswers *backing = source->backing ? source->backing : source;
    atomic_fetch_add_explicit(&backing->references, 1u, memory_order_relaxed);
    tail->backing = backing;
    tail->storage = backing->storage;
    tail->local_frames = backing->local_frames;
    tail->local_frame_count = backing->local_frame_count;
    tail->ordered = true;
    /* This bank header is a read-only slice. The backing owner frees the rows. */
    tail->bank.items = source->bank.items + 1;
    tail->bank.len = source->bank.len - 1;
    return publish_answers(context, arena, tail);
}

bool tm_is_sequence_operation(SymbolId head, uint32_t arity) {
    if (arity == 2u) return head == g_builtin_syms.index_atom;
    return arity == 1u && (head == g_builtin_syms.size_atom ||
        head == g_builtin_syms.car_atom || head == g_builtin_syms.cdr_atom ||
        head == g_builtin_syms.lib_list_from_expression ||
        head == g_builtin_syms.lib_list_to_expression);
}

static bool private_variable(const TmAnswers *answers, VarId id) {
    for (size_t i = 0; i < answers->local_frame_count; ++i)
        if (cetta_frame_identity_eq(answers->local_frames[i], id.epoch)) return true;
    return false;
}

/* A closed answer can erase solved, closed assignments made entirely in its
 * private frames. Captured assignments and residual equations remain data. */
static int plain_row(Arena *arena, const TmAnswers *answers, const AnswerRecord *row) {
    if (atom_has_vars(row->result) || row->bindings.eq_len ||
        bindings_prime_present(&row->bindings) || variant_instance_present(&row->variant))
        return 0;
    BindingsIterator iterator = {.bindings = &row->bindings};
    Binding binding;
    while (bindings_iterator_next(&iterator, &binding)) {
        if (!private_variable(answers, binding.var_id)) return 0;
        Atom *image = bindings_apply_value(&row->bindings, arena, binding.value);
        if (!image) return -1;
        if (atom_has_vars(image)) return 0;
    }
    return 1;
}

bool tm_answers_project(CettaLibraryContext *context, Arena *arena, SymbolId head,
    Atom *const *arguments, uint32_t arity, Atom *source, Bindings *caller,
    CettaCallOutcome *outcome) {
    bool list = head == g_builtin_syms.lib_list_from_expression;
    bool tuple = head == g_builtin_syms.lib_list_to_expression;
    bool index = head == g_builtin_syms.index_atom;
    bool size = head == g_builtin_syms.size_atom;
    bool cdr = head == g_builtin_syms.cdr_atom;
    if (!tm_is_sequence_operation(head, arity)) return false;
    TmAnswers *answers = tm_answers_get(context, arguments[0]);
    if (!answers) return false;

    *outcome = cetta_call_interrupted();
    const char *reason = NULL;
    Atom *value = NULL;
    CettaCount count = answers->bank.len;
    if (size) {
        if (count <= INT64_MAX) value = atom_int(arena, (int64_t)count);
    } else if (!answers->ordered) {
        reason = "ExpectedAnswerSequence";
    } else if (list || tuple) {
        for (CettaIndex i = 0; i < count; ++i) {
            const AnswerRecord *row = answer_bank_get(&answers->bank, i);
            int plain = plain_row(arena, answers, row);
            if (plain < 0) return true;
            if (!plain) {
                reason = "ConstrainedAnswerProjection";
                break;
            }
        }
        if (!reason && count <= SIZE_MAX / sizeof(Atom *)) {
            Atom **rows = count ? arena_alloc(arena, (size_t)count * sizeof *rows) : NULL;
            if (count && !rows) return true;
            for (CettaIndex i = 0; i < count; ++i)
                rows[i] = answer_bank_get(&answers->bank, i)->result;
            value = list ? atom_list(arena, rows, count) : atom_expr(arena, rows, count);
        }
    } else {
        int64_t position = 0;
        if (index) {
            Atom *offset = arguments[1];
            if (offset->kind != ATOM_GROUNDED || offset->ground.gkind != GV_INT)
                reason = "ExpectedIntegerIndex";
            else position = offset->ground.ival;
        }
        if (!reason && (position < 0 || (uint64_t)position >= count))
            reason = "AnswerIndexOutOfBounds";
        if (!reason && cdr) value = answer_tail(context, arena, answers);
        if (!reason && !cdr) {
            int opened = tm_answer_open(arena, answer_bank_get(&answers->bank,
                (CettaIndex)position), caller, &value);
            if (opened == 0) *outcome = cetta_call_failure();
        }
    }
    if (reason) {
        Atom *symbol = atom_symbol(arena, reason);
        Atom *error = symbol ? atom_error(arena, source, symbol) : NULL;
        if (error) *outcome = cetta_call_raised(error);
    } else if (value) *outcome = cetta_call_completed_value(value);
    return true;
}

typedef struct TmOrderKey {
    CettaLibraryContext *context;
    const TmAnswers *answers;
    const struct TmOrderKey *outer;
    CettaVarMap slots;
    bool *unorderable;
    uint32_t depth;
} TmOrderKey;

static Atom *order_record_key(Arena *arena, TmOrderKey *key, const AnswerRecord *record);

static Atom *order_nested_key(Arena *arena, TmOrderKey *outer, const TmAnswers *answers) {
    CettaCount count = answers->bank.len;
    if (count > SIZE_MAX / sizeof(Atom *)) return NULL;
    Atom **rows = count ? arena_alloc(arena, count * sizeof *rows) : NULL;
    if (count && !rows) return NULL;
    for (CettaIndex i = 0; i < count; ++i) {
        TmOrderKey key = {.context = outer->context, .answers = answers,
            .outer = outer, .unorderable = outer->unorderable, .depth = outer->depth + 1};
        rows[i] = order_record_key(arena, &key, answer_bank_get(&answers->bank, i));
        if (!rows[i]) return NULL;
    }
    Atom *contents = atom_expr(arena, rows, count);
    if (contents && !answers->ordered)
        contents = cetta_term_msort_structural(arena, contents);
    if (!contents) return NULL;
    /* Native-handle syntax is reserved in keys. An actual handle is read
     * here by its contents and never compared by its allocation identity. */
    return atom_expr3(arena, atom_symbol_id(arena, g_builtin_syms.native_handle),
        atom_symbol(arena, answers->ordered ? "tmetta-sequence" : "tmetta-answers"), contents);
}

static Atom *private_slot(Arena *arena, Atom *source, uint32_t ordinal, void *context) {
    (void)source;
    const TmOrderKey *key = context;
    VarId name = variant_shape_slot_id(ordinal);
    name.base |= (uint64_t)key->depth << 32;
    return atom_var_with_literal(arena, "private", name);
}

static Atom *inventory_order_slot(Arena *arena, Atom *variable, void *context) {
    TmOrderKey *key = context;
    if (private_variable(key->answers, variable->var_id) &&
        !cetta_var_map_get_or_add(&key->slots, arena, variable, private_slot, key))
        return NULL;
    return variable;
}

static bool order_key_node(Arena *arena, Atom *source, Atom **view, bool *complete,
    void *context) {
    TmOrderKey *key = context;
    if (source->kind == ATOM_VAR) {
        for (const TmOrderKey *scope = key; scope; scope = scope->outer) {
            if (!private_variable(scope->answers, source->var_id)) continue;
            *view = cetta_var_map_lookup(&scope->slots, source->var_id);
            *complete = true;
            /* A capture with no occurrence in its enclosing row needs a
             * joint scope for canonical ordering of the nested bags. */
            if (!*view) *key->unorderable = true;
            return *view != NULL;
        }
    }
    if (source->kind == ATOM_EXPR && source->expr.len &&
        atom_is_symbol_id(source->expr.elems[0], g_builtin_syms.native_handle)) {
        TmAnswers *nested = tm_answers_get(key->context, source);
        if (!nested) { *key->unorderable = true; return false; }
        *view = order_nested_key(arena, key, nested);
        *complete = true;
        return *view != NULL;
    }
    if (source->kind == ATOM_GROUNDED) {
        switch (source->ground.gkind) {
        case GV_INT: case GV_FLOAT: case GV_BIGINT: case GV_RATIONAL:
        case GV_BOOL: case GV_STRING: case GV_INTERNAL_TAG:
            break;
        default:
            *key->unorderable = true;
            return false;
        }
    }
    return true;
}

static Atom *order_record_key(Arena *arena, TmOrderKey *key, const AnswerRecord *record) {
    if (prime_need_snapshot_present(bindings_need_view(&record->bindings)) ||
        variant_instance_present(&record->variant)) {
        *key->unorderable = true;
        return NULL;
    }
    Atom *observed = bindings_apply_graph((Bindings *)&record->bindings, arena, record->result);
    Atom *equations = bindings_to_atom(arena, &record->bindings);
    Atom *pair = observed && equations ? atom_expr2(arena, observed, equations) : NULL;
    if (!pair) return NULL;
    cetta_var_map_init(&key->slots);
    /* Assign the visible private coordinates before visiting any nested
     * bag, whose rows may be stored in any order. */
    bool inventoried = !atom_has_vars(pair) ||
        cetta_atom_rewrite_vars(arena, pair, inventory_order_slot, key, true);
    Atom *result = inventoried ? atom_deep_copy_observed(arena, pair, order_key_node, key) : NULL;
    cetta_var_map_free(&key->slots);
    return result;
}

int tm_answers_canonical_order(CettaLibraryContext *context, Arena *arena,
    const TmAnswers *answers, CettaIndex *order) {
    CettaCount count = answers->bank.len;
    if (!count) return 1;
    if (count > INT64_MAX || count > SIZE_MAX / sizeof(Atom *)) return -1;
    Atom **keys = arena_alloc(arena, count * sizeof *keys);
    if (!keys) return -1;
    for (CettaIndex i = 0; i < count; ++i) {
        bool unorderable = false;
        TmOrderKey policy = {.context = context, .answers = answers, .unorderable = &unorderable};
        Atom *key = order_record_key(arena, &policy, answer_bank_get(&answers->bank, i));
        if (!key) return unorderable ? 0 : -1;
        Atom *ordinal = atom_int(arena, (int64_t)i);
        keys[i] = ordinal ? atom_expr2(arena, key, ordinal) : NULL;
        if (!keys[i]) return -1;
    }
    Atom *list = atom_expr(arena, keys, count);
    Atom *sorted = list ? cetta_term_msort_structural(arena, list) : NULL;
    if (!sorted) return -1;
    for (CettaIndex i = 0; i < count; ++i)
        order[i] = (CettaIndex)sorted->expr.elems[i]->expr.elems[1]->ground.ival;
    return 1;
}

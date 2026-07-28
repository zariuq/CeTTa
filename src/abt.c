#include "abt.h"
#include "atom_blob.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CETTA_NO_STDLIB
#include "abt_default_signatures_blob.h"
#endif

#ifndef CETTA_ABT_MUTATION
#define CETTA_ABT_MUTATION 0
#endif

/* Mutation ids used only by the destructive test target.
   1: subst without lifting under binders
   2: subst without decrementing higher indices
   3: shift without increasing cutoff under binders
   4: shift cutoff off by one
   5: Pi codomain does not bind
   6: Lam body does not bind
   7: App argument spuriously binds
   8: ArgsCons head spuriously binds
   9: Case body spuriously binds
  10: structurally duplicate fields are admitted
  11: BindPi codomain does not bind
  12: incomplete BindFields defaults missing fields to depth zero
  13: Bind1 accepts an undeclared field
  14: a negative field depth defaults to zero
  15: duplicate head/arity declarations are admitted
  16: a failed signature-set admission does not roll back
  17: open/close accept a loose canonical replacement/name
  18: transformed DAG memoization ignores binder depth
  19: named readback reverses de Bruijn index/level conversion
  20: named parsing ignores declared field depth
  21: named readback does not avoid free-name capture
  22: named parsing admits duplicate binders */

void abt_signature_init(AbtSignature *signature) {
    signature->entries = NULL;
    signature->len = 0;
    signature->cap = 0;
}

void abt_signature_free(AbtSignature *signature) {
    if (!signature) return;
    for (uint32_t i = 0; i < signature->len; i++)
        free(signature->entries[i].depths);
    free(signature->entries);
    abt_signature_init(signature);
}

static void abt_signature_truncate(AbtSignature *signature, uint32_t len) {
    assert(signature && len <= signature->len);
    while (signature->len > len) {
        AbtSignatureEntry *entry = &signature->entries[--signature->len];
        free(entry->depths);
        entry->depths = NULL;
    }
}

const AbtSignatureEntry *abt_signature_lookup(const AbtSignature *signature,
                                              SymbolId head,
                                              uint32_t arity) {
    if (!signature) return NULL;
    for (uint32_t i = 0; i < signature->len; i++) {
        const AbtSignatureEntry *entry = &signature->entries[i];
        if (entry->head == head && entry->arity == arity) return entry;
    }
    return NULL;
}

static bool abt_signature_add_raw(AbtSignature *signature, SymbolId head,
                                  uint32_t arity, const uint32_t *depths) {
    if (!signature || head == SYMBOL_ID_NONE || (arity > 0u && !depths) ||
        (CETTA_ABT_MUTATION != 15 &&
         abt_signature_lookup(signature, head, arity)))
        return false;
    if (signature->len == signature->cap) {
        uint32_t next_cap = signature->cap ? signature->cap * 2u : 16u;
        if (next_cap < signature->cap ||
            (size_t)next_cap > SIZE_MAX / sizeof(*signature->entries))
            return false;
        signature->entries = cetta_realloc(
            signature->entries,
            sizeof(*signature->entries) * (size_t)next_cap);
        signature->cap = next_cap;
    }
    AbtSignatureEntry *entry = &signature->entries[signature->len];
    entry->head = head;
    entry->arity = arity;
    entry->depths = arity
        ? cetta_malloc(sizeof(*entry->depths) * (size_t)arity) : NULL;
    if (arity)
        memcpy(entry->depths, depths, sizeof(*entry->depths) * (size_t)arity);
    signature->len++;
    return true;
}

#ifndef CETTA_NO_STDLIB
static bool abt_default_equation(Atom *atom, Atom **set_out) {
    if (!atom || !set_out || atom->kind != ATOM_EXPR ||
        atom->expr.len != 3u ||
        !atom_is_symbol(atom->expr.elems[0], "="))
        return false;
    Atom *lhs = atom->expr.elems[1];
    Atom *set = atom->expr.elems[2];
    if (!lhs || lhs->kind != ATOM_EXPR || lhs->expr.len != 1u ||
        !atom_is_symbol(lhs->expr.elems[0], "abt-default-signatures") ||
        !set || set->kind != ATOM_EXPR || set->expr.len == 0u ||
        !atom_is_symbol(set->expr.elems[0], "AbtSignatures"))
        return false;
    *set_out = set;
    return true;
}
#endif

Atom *abt_default_signature_set(Arena *arena) {
#ifdef CETTA_NO_STDLIB
    (void)arena;
    return NULL;
#else
    AtomBlobStatus status = ATOM_BLOB_OK;
    Atom *equation = atom_blob_decode_single_ground(
        arena, ABT_DEFAULT_SIGNATURES_BLOB,
        ABT_DEFAULT_SIGNATURES_BLOB_LEN, &status);
    Atom *set = NULL;
    return status == ATOM_BLOB_OK && abt_default_equation(equation, &set)
        ? set : NULL;
#endif
}

static bool abt_mutate_default_depth(AbtSignature *signature,
                                     const char *head_name,
                                     uint32_t arity, uint32_t field,
                                     uint32_t depth) {
    if (!signature || field >= arity) return false;
    SymbolId head = symbol_intern_cstr(g_symbols, head_name);
    for (uint32_t i = 0; i < signature->len; i++) {
        AbtSignatureEntry *entry = &signature->entries[i];
        if (entry->head == head && entry->arity == arity) {
            entry->depths[field] = depth;
            return true;
        }
    }
    return false;
}

bool abt_signature_add_defaults(AbtSignature *signature, Arena *arena) {
    uint32_t original_len = signature ? signature->len : 0u;
    Atom *set = arena ? abt_default_signature_set(arena) : NULL;
    bool ok = signature && set && abt_signature_add_set(signature, set);
    if (ok && CETTA_ABT_MUTATION == 5)
        ok = abt_mutate_default_depth(signature, "Pi", 2u, 1u, 0u);
    if (ok && CETTA_ABT_MUTATION == 6)
        ok = abt_mutate_default_depth(signature, "Lam", 2u, 1u, 0u);
    if (ok && CETTA_ABT_MUTATION == 7)
        ok = abt_mutate_default_depth(signature, "App", 2u, 1u, 1u);
    if (ok && CETTA_ABT_MUTATION == 8)
        ok = abt_mutate_default_depth(signature, "ArgsCons", 2u, 0u, 1u);
    if (ok && CETTA_ABT_MUTATION == 9)
        ok = abt_mutate_default_depth(signature, "Case", 2u, 1u, 1u);
    if (!ok && signature) abt_signature_truncate(signature, original_len);
    return ok;
}

static bool abt_expr_head(Atom *atom, const char *name, CettaExprLen arity) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len == arity + 1u &&
           atom_is_symbol(atom->expr.elems[0], name);
}

static int32_t abt_field_index(Atom *const *fields, uint32_t arity,
                               Atom *field) {
    if (!field || !abt_alpha_eq(field, field)) return -1;
    for (uint32_t i = 0; i < arity; i++)
        if (abt_alpha_eq(fields[i], field)) return (int32_t)i;
    return -1;
}

static bool abt_parse_depth(Atom *atom, uint32_t *depth) {
    if (CETTA_ABT_MUTATION == 14 && atom &&
        atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_INT &&
        atom->ground.ival < 0) {
        *depth = 0u;
        return true;
    }
    if (!atom || atom->kind != ATOM_GROUNDED ||
        atom->ground.gkind != GV_INT || atom->ground.ival < 0 ||
        (uint64_t)atom->ground.ival > UINT32_MAX)
        return false;
    *depth = (uint32_t)atom->ground.ival;
    return true;
}

bool abt_signature_add_decl(AbtSignature *signature, Atom *declaration) {
    if (!signature || !abt_expr_head(declaration, "AbtSignature", 3u))
        return false;
    Atom *head = declaration->expr.elems[1];
    Atom *field_expr = declaration->expr.elems[2];
    Atom *decl = declaration->expr.elems[3];
    if (head->kind != ATOM_SYMBOL || field_expr->kind != ATOM_EXPR ||
        field_expr->expr.len == 0 ||
        !atom_is_symbol(field_expr->expr.elems[0], "Fields") ||
        !cetta_expr_len_fits_u32(field_expr->expr.len - 1u))
        return false;
    uint32_t arity = (uint32_t)(field_expr->expr.len - 1u);
    Atom **fields = arity
        ? cetta_malloc(sizeof(*fields) * (size_t)arity) : NULL;
    uint32_t *depths = arity
        ? cetta_malloc(sizeof(*depths) * (size_t)arity) : NULL;
    bool *seen = arity
        ? cetta_malloc(sizeof(*seen) * (size_t)arity) : NULL;
    bool ok = true;
    for (uint32_t i = 0; i < arity; i++) {
        Atom *field = field_expr->expr.elems[i + 1u];
        fields[i] = field;
        depths[i] = 0u;
        seen[i] = false;
        if (!abt_alpha_eq(fields[i], fields[i])) ok = false;
        for (uint32_t j = 0; j < i; j++)
            if (CETTA_ABT_MUTATION != 10 &&
                abt_alpha_eq(fields[i], fields[j]))
                ok = false;
    }
    if (!ok) goto done;

    if (atom_is_symbol(decl, "Bind0")) {
        /* Every field is explicitly non-binding. */
    } else if (abt_expr_head(decl, "Bind1", 1u)) {
        int32_t field = abt_field_index(fields, arity, decl->expr.elems[1]);
        if (field < 0 && CETTA_ABT_MUTATION != 13) {
            ok = false;
            goto done;
        }
        if (field >= 0) depths[(uint32_t)field] = 1u;
    } else if (abt_expr_head(decl, "BindPi", 2u)) {
        int32_t domain = abt_field_index(fields, arity, decl->expr.elems[1]);
        int32_t codomain = abt_field_index(fields, arity, decl->expr.elems[2]);
        if (domain < 0 || codomain < 0 || domain == codomain) {
            ok = false;
            goto done;
        }
        depths[(uint32_t)domain] = 0u;
        depths[(uint32_t)codomain] =
            CETTA_ABT_MUTATION == 11 ? 0u : 1u;
    } else if (abt_expr_head(decl, "BindFields", 1u)) {
        Atom *list = decl->expr.elems[1];
        uint32_t count = 0;
        while (!atom_is_symbol(list, "BNil")) {
            if (count >= arity || !abt_expr_head(list, "BCons", 2u) ||
                !abt_expr_head(list->expr.elems[1], "BField", 2u)) {
                ok = false;
                goto done;
            }
            Atom *binding_field = list->expr.elems[1];
            int32_t field = abt_field_index(
                fields, arity, binding_field->expr.elems[1]);
            uint32_t depth = 0;
            if (field < 0 || seen[(uint32_t)field] ||
                !abt_parse_depth(binding_field->expr.elems[2], &depth)) {
                ok = false;
                goto done;
            }
            seen[(uint32_t)field] = true;
            depths[(uint32_t)field] = depth;
            count++;
            list = list->expr.elems[2];
        }
        if (count != arity && CETTA_ABT_MUTATION != 12) {
            ok = false;
            goto done;
        }
    } else {
        ok = false;
        goto done;
    }

    ok = abt_signature_add_raw(signature, head->sym_id, arity, depths);

done:
    free(seen);
    free(depths);
    free(fields);
    return ok;
}

bool abt_signature_add_set(AbtSignature *signature, Atom *set) {
    if (!signature || !set || set->kind != ATOM_EXPR || set->expr.len == 0 ||
        !atom_is_symbol(set->expr.elems[0], "AbtSignatures"))
        return false;
    uint32_t original_len = signature->len;
    for (CettaExprIndex i = 1; i < set->expr.len; i++) {
        if (abt_signature_add_decl(signature, set->expr.elems[i])) continue;
        if (CETTA_ABT_MUTATION != 16)
            abt_signature_truncate(signature, original_len);
        return false;
    }
    return true;
}

typedef struct {
    Atom *term;
    uint8_t state; /* 0 empty, 1 active, 2 tombstone */
} AbtActiveSlot;

typedef struct {
    AbtActiveSlot inline_slots[32];
    AbtActiveSlot *slots;
    size_t cap;
    size_t active;
    size_t tombstones;
} AbtActiveSet;

static size_t abt_pointer_hash(Atom *term) {
    uintptr_t x = (uintptr_t)term >> 4;
    x ^= x >> 17;
    x *= (uintptr_t)0xed5ad4bbu;
    x ^= x >> 11;
    return (size_t)x;
}

static void abt_active_init(AbtActiveSet *set) {
    set->slots = set->inline_slots;
    set->cap = sizeof(set->inline_slots) / sizeof(set->inline_slots[0]);
    set->active = 0;
    set->tombstones = 0;
    memset(set->slots, 0, sizeof(set->inline_slots));
}

static void abt_active_free(AbtActiveSet *set) {
    if (set->slots != set->inline_slots) free(set->slots);
}

static bool abt_active_rehash(AbtActiveSet *set, size_t new_cap) {
    if (new_cap < 32u || new_cap > SIZE_MAX / sizeof(*set->slots)) return false;
    AbtActiveSlot *old_slots = set->slots;
    size_t old_cap = set->cap;
    size_t old_active = set->active;
    AbtActiveSlot *slots = cetta_malloc(sizeof(*slots) * new_cap);
    memset(slots, 0, sizeof(*slots) * new_cap);
    set->slots = slots;
    set->cap = new_cap;
    set->active = 0;
    set->tombstones = 0;
    for (size_t i = 0; i < old_cap; i++) {
        if (old_slots[i].state != 1u) continue;
        size_t pos = abt_pointer_hash(old_slots[i].term) & (new_cap - 1u);
        while (slots[pos].state != 0u) pos = (pos + 1u) & (new_cap - 1u);
        slots[pos] = old_slots[i];
        set->active++;
    }
    assert(set->active == old_active);
    if (old_slots != set->inline_slots) free(old_slots);
    return true;
}

static bool abt_active_enter(AbtActiveSet *set, Atom *term) {
    size_t occupied = set->active + set->tombstones;
    if ((occupied + 1u) * 4u >= set->cap * 3u) {
        size_t new_cap = set->cap;
        if ((set->active + 1u) * 4u >= set->cap * 3u) {
            if (set->cap > SIZE_MAX / 2u) return false;
            new_cap *= 2u;
        }
        if (!abt_active_rehash(set, new_cap)) return false;
    }
    size_t mask = set->cap - 1u;
    size_t pos = abt_pointer_hash(term) & mask;
    AbtActiveSlot *first_tombstone = NULL;
    for (;;) {
        AbtActiveSlot *slot = &set->slots[pos];
        if (slot->state == 0u) {
            slot = first_tombstone ? first_tombstone : slot;
            if (slot->state == 2u) set->tombstones--;
            slot->term = term;
            slot->state = 1u;
            set->active++;
            return true;
        }
        if (slot->term == term) {
            if (slot->state == 1u) return false;
            set->tombstones--;
            slot->state = 1u;
            set->active++;
            return true;
        }
        if (slot->state == 2u && !first_tombstone) first_tombstone = slot;
        pos = (pos + 1u) & mask;
    }
}

static void abt_active_leave(AbtActiveSet *set, Atom *term) {
    size_t mask = set->cap - 1u;
    size_t pos = abt_pointer_hash(term) & mask;
    for (;;) {
        AbtActiveSlot *slot = &set->slots[pos];
        assert(slot->state != 0u);
        if (slot->state == 1u && slot->term == term) {
            slot->state = 2u;
            set->active--;
            set->tombstones++;
            return;
        }
        pos = (pos + 1u) & mask;
    }
}

typedef struct {
    Atom *term;
    uint64_t depth;
    Atom *result;
} AbtTransformMemoSlot;

typedef struct {
    AbtTransformMemoSlot inline_slots[64];
    AbtTransformMemoSlot *slots;
    size_t cap;
    size_t used;
} AbtTransformMemo;

static size_t abt_transform_memo_hash(Atom *term, uint64_t depth) {
    if (CETTA_ABT_MUTATION == 18) depth = 0u;
    uint64_t mixed_depth = depth;
    mixed_depth ^= mixed_depth >> 30;
    mixed_depth *= UINT64_C(0xbf58476d1ce4e5b9);
    mixed_depth ^= mixed_depth >> 27;
    mixed_depth *= UINT64_C(0x94d049bb133111eb);
    mixed_depth ^= mixed_depth >> 31;
    return abt_pointer_hash(term) ^ (size_t)mixed_depth;
}

static void abt_transform_memo_init(AbtTransformMemo *memo) {
    memo->slots = memo->inline_slots;
    memo->cap = sizeof(memo->inline_slots) / sizeof(memo->inline_slots[0]);
    memo->used = 0u;
    memset(memo->slots, 0, sizeof(memo->inline_slots));
}

static void abt_transform_memo_free(AbtTransformMemo *memo) {
    if (memo->slots != memo->inline_slots) free(memo->slots);
}

static Atom *abt_transform_memo_lookup(const AbtTransformMemo *memo,
                                       Atom *term, uint64_t depth) {
    if (CETTA_ABT_MUTATION == 18) depth = 0u;
    size_t mask = memo->cap - 1u;
    size_t pos = abt_transform_memo_hash(term, depth) & mask;
    for (;;) {
        const AbtTransformMemoSlot *slot = &memo->slots[pos];
        if (!slot->term) return NULL;
        if (slot->term == term && slot->depth == depth) return slot->result;
        pos = (pos + 1u) & mask;
    }
}

static bool abt_transform_memo_grow(AbtTransformMemo *memo) {
    if (memo->cap > SIZE_MAX / 2u ||
        memo->cap * 2u > SIZE_MAX / sizeof(*memo->slots))
        return false;
    size_t old_cap = memo->cap;
    size_t next_cap = old_cap * 2u;
    AbtTransformMemoSlot *old_slots = memo->slots;
    AbtTransformMemoSlot *next = cetta_malloc(sizeof(*next) * next_cap);
    memset(next, 0, sizeof(*next) * next_cap);
    memo->slots = next;
    memo->cap = next_cap;
    memo->used = 0u;
    for (size_t i = 0; i < old_cap; i++) {
        AbtTransformMemoSlot *old = &old_slots[i];
        if (!old->term) continue;
        size_t mask = next_cap - 1u;
        size_t pos = abt_transform_memo_hash(old->term, old->depth) & mask;
        while (next[pos].term) pos = (pos + 1u) & mask;
        next[pos] = *old;
        memo->used++;
    }
    if (old_slots != memo->inline_slots) free(old_slots);
    return true;
}

static bool abt_transform_memo_store(AbtTransformMemo *memo, Atom *term,
                                     uint64_t depth, Atom *result) {
    if (!term || !result) return false;
    if (CETTA_ABT_MUTATION == 18) depth = 0u;
    if ((memo->used + 1u) * 4u >= memo->cap * 3u &&
        !abt_transform_memo_grow(memo))
        return false;
    size_t mask = memo->cap - 1u;
    size_t pos = abt_transform_memo_hash(term, depth) & mask;
    for (;;) {
        AbtTransformMemoSlot *slot = &memo->slots[pos];
        if (!slot->term) {
            slot->term = term;
            slot->depth = depth;
            slot->result = result;
            memo->used++;
            return true;
        }
        if (slot->term == term && slot->depth == depth) {
            slot->result = result;
            return true;
        }
        pos = (pos + 1u) & mask;
    }
}

typedef enum {
    ABT_TRANSFORM_SHIFT,
    ABT_TRANSFORM_SUBST,
    ABT_TRANSFORM_CLOSE,
    ABT_TRANSFORM_OPEN,
    ABT_TRANSFORM_BIND,
} AbtTransformKind;

typedef struct {
    AbtTransformKind kind;
    int64_t delta;
    uint64_t index;
    Atom *replacement;
} AbtTransform;

typedef enum { ABT_TASK_VISIT, ABT_TASK_BUILD } AbtTaskKind;

typedef struct {
    AbtTaskKind kind;
    Atom *term;
    uint64_t depth;
    Atom **destination;
    Atom **elems;
} AbtTask;

typedef struct {
    AbtTask inline_tasks[64];
    AbtTask *tasks;
    size_t len;
    size_t cap;
} AbtTaskStack;

static void abt_tasks_init(AbtTaskStack *stack) {
    stack->tasks = stack->inline_tasks;
    stack->len = 0;
    stack->cap = sizeof(stack->inline_tasks) / sizeof(stack->inline_tasks[0]);
}

static void abt_tasks_free(AbtTaskStack *stack) {
    if (stack->tasks != stack->inline_tasks) free(stack->tasks);
}

static bool abt_task_push(AbtTaskStack *stack, AbtTask task) {
    if (stack->len == stack->cap) {
        if (stack->cap > SIZE_MAX / 2u ||
            stack->cap * 2u > SIZE_MAX / sizeof(*stack->tasks))
            return false;
        size_t next_cap = stack->cap * 2u;
        if (stack->tasks == stack->inline_tasks) {
            AbtTask *next = cetta_malloc(sizeof(*next) * next_cap);
            memcpy(next, stack->inline_tasks,
                   sizeof(*next) * stack->len);
            stack->tasks = next;
        } else {
            stack->tasks = cetta_realloc(
                stack->tasks, sizeof(*stack->tasks) * next_cap);
        }
        stack->cap = next_cap;
    }
    stack->tasks[stack->len++] = task;
    return true;
}

static bool abt_is_quote_form(Atom *term) {
    return term && term->kind == ATOM_EXPR && term->expr.len == 2u &&
           atom_is_symbol(term->expr.elems[0], "quote");
}

/* 0 is not an index, 1 is a canonical index, and -1 is malformed. */
static int abt_idx_form(Atom *term, uint64_t *index) {
    if (!term || term->kind != ATOM_EXPR || term->expr.len == 0u ||
        !atom_is_symbol(term->expr.elems[0], "idx"))
        return 0;
    if (term->expr.len != 2u) return -1;
    Atom *payload = term->expr.elems[1];
    if (payload->kind == ATOM_GROUNDED &&
        payload->ground.gkind == GV_INT) {
        if (payload->ground.ival < 0) return -1;
        *index = (uint64_t)payload->ground.ival;
        return 1;
    }
    return -1;
}

static bool abt_closed_stable_graph(Atom *root) {
    if (!root || atom_has_vars(root) ||
        (root->flags & ATOM_FLAG_HASH_STABLE) == 0u)
        return false;
    AbtTaskStack stack;
    AbtActiveSet active;
    abt_tasks_init(&stack);
    abt_active_init(&active);
    if (!abt_task_push(&stack, (AbtTask){
            ABT_TASK_VISIT, root, 0u, NULL, NULL}))
        goto fail;
    while (stack.len > 0u) {
        AbtTask task = stack.tasks[--stack.len];
        if (task.kind == ABT_TASK_BUILD) {
            abt_active_leave(&active, task.term);
            continue;
        }
        Atom *current = task.term;
        if (!current || current->kind == ATOM_VAR ||
            (current->flags & ATOM_FLAG_HASH_STABLE) == 0u)
            goto fail;
        if (current->kind != ATOM_EXPR) continue;
        if (!abt_active_enter(&active, current) ||
            !abt_task_push(&stack, (AbtTask){
                ABT_TASK_BUILD, current, 0u, NULL, NULL}))
            goto fail;
        for (CettaExprIndex i = current->expr.len; i > 0u; i--)
            if (!abt_task_push(&stack, (AbtTask){
                    ABT_TASK_VISIT, current->expr.elems[i - 1u],
                    0u, NULL, NULL}))
                goto fail;
    }
    abt_active_free(&active);
    abt_tasks_free(&stack);
    return true;

fail:
    abt_active_free(&active);
    abt_tasks_free(&stack);
    return false;
}

/* Keys accepted by close/open/bind are closed structural atoms.  Canonical
   loose indices remain forbidden; quote makes a structural name explicit. */
static bool abt_key_admitted(const AbtSignature *signature, Atom *name) {
    uint64_t ignored = 0u;
    int form = abt_idx_form(name, &ignored);
    if (form != 0) return false;
    if (abt_is_quote_form(name))
        return abt_closed_stable_graph(name);
    return abt_scope_check(signature, 0u, name);
}

static bool abt_surface_binder_name(Atom *name) {
    if (name && name->kind == ATOM_SYMBOL) return true;
    if (abt_is_quote_form(name))
        return abt_closed_stable_graph(name);
    return false;
}

static int abt_idx_index(Atom *term, uint64_t *index) {
    return abt_idx_form(term, index);
}

static Atom *abt_make_idx(Arena *arena, uint64_t index) {
    if (index > INT64_MAX) return NULL;
    return atom_expr2(
        arena, atom_symbol(arena, "idx"), atom_int(arena, (int64_t)index));
}

static bool abt_depth_add(uint64_t depth, uint32_t increment,
                          uint64_t *result) {
    if (depth > UINT64_MAX - (uint64_t)increment) return false;
    *result = depth + (uint64_t)increment;
    return true;
}

static Atom *abt_transform_var(const AbtSignature *signature, Arena *arena,
                               const AbtTransform *transform,
                               Atom *original, uint64_t depth,
                               uint64_t variable) {
    uint64_t target = 0;
    switch (transform->kind) {
    case ABT_TRANSFORM_SHIFT: {
        if (transform->index > UINT64_MAX - depth) return NULL;
        target = transform->index + depth;
        bool should_shift = CETTA_ABT_MUTATION == 4
            ? variable > target : variable >= target;
        if (!should_shift || transform->delta == 0) return original;
        if (transform->delta >= 0) {
            uint64_t amount = (uint64_t)transform->delta;
            if (variable > (uint64_t)INT64_MAX - amount) return NULL;
            return abt_make_idx(arena, variable + amount);
        }
        uint64_t amount = (uint64_t)(-(transform->delta + 1)) + 1u;
        if (variable < amount) return NULL;
        return abt_make_idx(arena, variable - amount);
    }
    case ABT_TRANSFORM_SUBST:
        if (transform->index > UINT64_MAX - depth) return NULL;
        target = transform->index + depth;
        if (variable == target) {
            if (CETTA_ABT_MUTATION == 1 || depth == 0u)
                return transform->replacement;
            if (depth > INT64_MAX) return NULL;
            return abt_shift(signature, arena, (int64_t)depth, 0u,
                             transform->replacement);
        }
        if (variable > target && CETTA_ABT_MUTATION != 2)
            return abt_make_idx(arena, variable - 1u);
        return original;
    case ABT_TRANSFORM_OPEN:
        if (variable == depth) return transform->replacement;
        return original;
    case ABT_TRANSFORM_CLOSE:
        return original;
    case ABT_TRANSFORM_BIND: {
        if (variable < depth) return original;
        if (variable == UINT64_MAX) return NULL;
        return abt_make_idx(arena, variable + 1u);
    }
    }
    return NULL;
}

static Atom *abt_transform_term(const AbtSignature *signature, Arena *arena,
                                const AbtTransform *transform, Atom *term) {
    if (!signature || !arena || !transform || !term) return NULL;
    if (transform->kind == ABT_TRANSFORM_SUBST &&
        (!transform->replacement ||
         !abt_alpha_eq(transform->replacement, transform->replacement)))
        return NULL;
    if ((transform->kind == ABT_TRANSFORM_CLOSE ||
         transform->kind == ABT_TRANSFORM_OPEN ||
         transform->kind == ABT_TRANSFORM_BIND) &&
        !(CETTA_ABT_MUTATION == 17 &&
          transform->kind != ABT_TRANSFORM_BIND) &&
        !abt_key_admitted(signature, transform->replacement))
        return NULL;

    Atom *result = NULL;
    AbtTaskStack stack;
    AbtActiveSet active;
    AbtTransformMemo memo;
    abt_tasks_init(&stack);
    abt_active_init(&active);
    abt_transform_memo_init(&memo);
    if (!abt_task_push(&stack, (AbtTask){
            ABT_TASK_VISIT, term, 0u, &result, NULL}))
        goto fail;

    while (stack.len > 0) {
        AbtTask task = stack.tasks[--stack.len];
        if (task.kind == ABT_TASK_BUILD) {
            bool unchanged = true;
            for (CettaExprIndex i = 0; i < task.term->expr.len; i++)
                if (task.elems[i] != task.term->expr.elems[i]) {
                    unchanged = false;
                    break;
                }
            Atom *built = unchanged
                ? task.term
                : atom_expr(arena, task.elems, task.term->expr.len);
            *task.destination = built;
            if (!abt_transform_memo_store(
                    &memo, task.term, task.depth, built))
                goto fail;
            abt_active_leave(&active, task.term);
            continue;
        }

        Atom *current = task.term;
        Atom *memoized = abt_transform_memo_lookup(
            &memo, current, task.depth);
        if (memoized) {
            *task.destination = memoized;
            continue;
        }
        if ((transform->kind == ABT_TRANSFORM_CLOSE ||
             transform->kind == ABT_TRANSFORM_BIND) &&
            !(CETTA_ABT_MUTATION == 23 &&
              transform->kind == ABT_TRANSFORM_BIND) &&
            atom_eq(current, transform->replacement)) {
            *task.destination = abt_make_idx(arena, task.depth);
            if (!*task.destination || !abt_transform_memo_store(
                    &memo, current, task.depth, *task.destination))
                goto fail;
            continue;
        }

        /* Quoted syntax is inert under object-language transformations.  It
           can be closed as one whole name by the equality case above, but an
           unrelated outer binder/substitution never traverses its payload.
           Ordinary quoted code may contain MeTTa matcher variables; only a
           quote used as a persistent binder key must satisfy closedness. */
        if (abt_is_quote_form(current)) {
            *task.destination = current;
            if (!abt_transform_memo_store(
                    &memo, current, task.depth, current))
                goto fail;
            continue;
        }

        uint64_t variable = 0;
        int var_status = abt_idx_form(current, &variable);
        if (var_status < 0) goto fail;
        if (var_status > 0) {
            *task.destination = abt_transform_var(
                signature, arena, transform, current, task.depth, variable);
            if (!*task.destination || !abt_transform_memo_store(
                    &memo, current, task.depth, *task.destination))
                goto fail;
            continue;
        }
        if (current->kind != ATOM_EXPR) {
            *task.destination = current;
            if (!abt_transform_memo_store(
                    &memo, current, task.depth, current))
                goto fail;
            continue;
        }
        if (!abt_active_enter(&active, current) ||
            !cetta_expr_len_mul_fits_size(current->expr.len, sizeof(Atom *)))
            goto fail;
        Atom **elems = current->expr.len
            ? arena_alloc(arena, sizeof(*elems) * (size_t)current->expr.len)
            : NULL;
        if (!abt_task_push(&stack, (AbtTask){
                ABT_TASK_BUILD, current, task.depth, task.destination, elems}))
            goto fail;

        bool symbolic_head = current->expr.len > 0 &&
            current->expr.elems[0]->kind == ATOM_SYMBOL;
        const AbtSignatureEntry *entry = NULL;
        uint32_t arity = 0;
        if (symbolic_head) {
            if (!cetta_expr_len_fits_u32(current->expr.len - 1u)) goto fail;
            arity = (uint32_t)(current->expr.len - 1u);
            entry = abt_signature_lookup(
                signature, current->expr.elems[0]->sym_id, arity);
            elems[0] = current->expr.elems[0];
        }
        CettaExprIndex first = symbolic_head ? 1u : 0u;
        for (CettaExprIndex i = current->expr.len; i > first; i--) {
            CettaExprIndex child = i - 1u;
            uint32_t field_depth = entry
                ? entry->depths[(uint32_t)(child - 1u)] : 0u;
            if (CETTA_ABT_MUTATION == 3 &&
                transform->kind == ABT_TRANSFORM_SHIFT)
                field_depth = 0u;
            uint64_t child_depth = 0;
            if (!abt_depth_add(task.depth, field_depth, &child_depth) ||
                !abt_task_push(&stack, (AbtTask){
                    ABT_TASK_VISIT, current->expr.elems[child], child_depth,
                    &elems[child], NULL}))
                goto fail;
        }
        if (!symbolic_head && current->expr.len > 0) {
            if (!abt_task_push(&stack, (AbtTask){
                    ABT_TASK_VISIT, current->expr.elems[0], task.depth,
                    &elems[0], NULL}))
                goto fail;
        }
    }

    abt_transform_memo_free(&memo);
    abt_active_free(&active);
    abt_tasks_free(&stack);
    return result;

fail:
    abt_transform_memo_free(&memo);
    abt_active_free(&active);
    abt_tasks_free(&stack);
    return NULL;
}

Atom *abt_shift(const AbtSignature *signature, Arena *arena,
                int64_t delta, uint64_t cutoff, Atom *term) {
    AbtTransform transform = {
        .kind = ABT_TRANSFORM_SHIFT,
        .delta = delta,
        .index = cutoff,
        .replacement = NULL,
    };
    return abt_transform_term(signature, arena, &transform, term);
}

Atom *abt_subst(const AbtSignature *signature, Arena *arena,
                uint64_t index, Atom *substitution, Atom *term) {
    if (!substitution) return NULL;
    AbtTransform transform = {
        .kind = ABT_TRANSFORM_SUBST,
        .delta = 0,
        .index = index,
        .replacement = substitution,
    };
    return abt_transform_term(signature, arena, &transform, term);
}

Atom *abt_close(const AbtSignature *signature, Arena *arena,
                Atom *name, Atom *term) {
    if (CETTA_ABT_MUTATION != 17 && !abt_key_admitted(signature, name))
        return NULL;
    AbtTransform transform = {
        .kind = ABT_TRANSFORM_CLOSE,
        .delta = 0,
        .index = 0,
        .replacement = name,
    };
    return abt_transform_term(signature, arena, &transform, term);
}

Atom *abt_open(const AbtSignature *signature, Arena *arena,
               Atom *fresh_name, Atom *term) {
    if (CETTA_ABT_MUTATION != 17 &&
        !abt_key_admitted(signature, fresh_name))
        return NULL;
    AbtTransform transform = {
        .kind = ABT_TRANSFORM_OPEN,
        .delta = 0,
        .index = 0,
        .replacement = fresh_name,
    };
    return abt_transform_term(signature, arena, &transform, term);
}

Atom *abt_bind(const AbtSignature *signature, Arena *arena,
               Atom *name, Atom *term) {
    if (!abt_key_admitted(signature, name)) return NULL;
    AbtTransform transform = {
        .kind = ABT_TRANSFORM_BIND,
        .delta = 0,
        .index = 0,
        .replacement = name,
    };
    return abt_transform_term(signature, arena, &transform, term);
}

static uint32_t abt_entry_max_depth(const AbtSignatureEntry *entry) {
    uint32_t maximum = 0u;
    if (!entry) return maximum;
    for (uint32_t i = 0; i < entry->arity; i++)
        if (entry->depths[i] > maximum) maximum = entry->depths[i];
    return maximum;
}

/* 0 is not a generated name, 1 is a canonical a<decimal> name, and -1 is
   syntactically generated but outside the representable counter range. */
static int abt_generated_name_index(SymbolId symbol, uint64_t *index) {
    if (!g_symbols || symbol == SYMBOL_ID_NONE || !index) return 0;
    const char *bytes = symbol_bytes(g_symbols, symbol);
    uint32_t len = symbol_len(g_symbols, symbol);
    if (!bytes || len < 2u || bytes[0] != 'a') return 0;
    if (len > 2u && bytes[1] == '0') return 0;
    uint64_t value = 0u;
    for (uint32_t i = 1u; i < len; i++) {
        unsigned char ch = (unsigned char)bytes[i];
        if (ch < (unsigned char)'0' || ch > (unsigned char)'9') return 0;
        uint64_t digit = (uint64_t)(ch - (unsigned char)'0');
        if (value > (UINT64_MAX - digit) / 10u) return -1;
        value = value * 10u + digit;
    }
    *index = value;
    return 1;
}

static bool abt_print_fresh_offset(Atom *term, uint64_t *offset) {
    AbtTaskStack stack;
    AbtTransformMemo seen;
    bool found = false;
    uint64_t maximum = 0u;
    abt_tasks_init(&stack);
    abt_transform_memo_init(&seen);
    if (!abt_task_push(&stack, (AbtTask){
            ABT_TASK_VISIT, term, 0u, NULL, NULL}))
        goto fail;
    while (stack.len > 0u) {
        AbtTask task = stack.tasks[--stack.len];
        Atom *current = task.term;
        if (abt_transform_memo_lookup(&seen, current, 0u)) continue;
        if (!abt_transform_memo_store(&seen, current, 0u, current)) goto fail;
        if (current->kind == ATOM_SYMBOL) {
            uint64_t value = 0u;
            int status = abt_generated_name_index(current->sym_id, &value);
            if (status < 0) goto fail;
            if (status > 0 && (!found || value > maximum)) {
                maximum = value;
                found = true;
            }
            continue;
        }
        if (current->kind != ATOM_EXPR) continue;
        for (CettaExprIndex i = current->expr.len; i > 0u; i--)
            if (!abt_task_push(&stack, (AbtTask){
                    ABT_TASK_VISIT, current->expr.elems[i - 1u], 0u,
                    NULL, NULL}))
                goto fail;
    }
    if (CETTA_ABT_MUTATION == 21) {
        *offset = 0u;
    } else if (!found) {
        *offset = 0u;
    } else {
        if (maximum == UINT64_MAX) goto fail;
        *offset = maximum + 1u;
    }
    abt_transform_memo_free(&seen);
    abt_tasks_free(&stack);
    return true;

fail:
    abt_transform_memo_free(&seen);
    abt_tasks_free(&stack);
    return false;
}

static Atom *abt_print_name(Arena *arena, uint64_t index) {
    char name[32];
    int written = snprintf(name, sizeof(name), "a%llu",
                           (unsigned long long)index);
    if (written < 0 || (size_t)written >= sizeof(name)) return NULL;
    return atom_symbol(arena, name);
}

typedef enum {
    ABT_PRINT_VISIT,
    ABT_PRINT_BUILD,
} AbtPrintTaskKind;

typedef struct {
    AbtPrintTaskKind kind;
    Atom *term;
    uint64_t depth;
    Atom **destination;
    Atom **elems;
    CettaExprLen result_len;
    bool wrapped;
} AbtPrintTask;

typedef struct {
    AbtPrintTask inline_tasks[64];
    AbtPrintTask *tasks;
    size_t len;
    size_t cap;
} AbtPrintStack;

static void abt_print_stack_init(AbtPrintStack *stack) {
    stack->tasks = stack->inline_tasks;
    stack->len = 0u;
    stack->cap = sizeof(stack->inline_tasks) / sizeof(stack->inline_tasks[0]);
}

static void abt_print_stack_free(AbtPrintStack *stack) {
    if (stack->tasks != stack->inline_tasks) free(stack->tasks);
}

static bool abt_print_stack_push(AbtPrintStack *stack, AbtPrintTask task) {
    if (stack->len == stack->cap) {
        if (stack->cap > SIZE_MAX / 2u ||
            stack->cap * 2u > SIZE_MAX / sizeof(*stack->tasks))
            return false;
        size_t next_cap = stack->cap * 2u;
        if (stack->tasks == stack->inline_tasks) {
            AbtPrintTask *next = cetta_malloc(sizeof(*next) * next_cap);
            memcpy(next, stack->inline_tasks, sizeof(*next) * stack->len);
            stack->tasks = next;
        } else {
            stack->tasks = cetta_realloc(
                stack->tasks, sizeof(*stack->tasks) * next_cap);
        }
        stack->cap = next_cap;
    }
    stack->tasks[stack->len++] = task;
    return true;
}

Atom *abt_print(const AbtSignature *signature, Arena *arena, Atom *term) {
    if (!signature || !arena || !term ||
        !abt_scope_check(signature, 0u, term))
        return NULL;
    uint64_t name_offset = 0u;
    if (!abt_print_fresh_offset(term, &name_offset)) return NULL;

    Atom *result = NULL;
    AbtPrintStack stack;
    AbtActiveSet active;
    AbtTransformMemo memo;
    abt_print_stack_init(&stack);
    abt_active_init(&active);
    abt_transform_memo_init(&memo);
    if (!abt_print_stack_push(&stack, (AbtPrintTask){
            ABT_PRINT_VISIT, term, 0u, &result, NULL, 0u, false}))
        goto fail;

    while (stack.len > 0u) {
        AbtPrintTask task = stack.tasks[--stack.len];
        if (task.kind == ABT_PRINT_BUILD) {
            Atom *built = NULL;
            if (!task.wrapped) {
                bool unchanged = true;
                for (CettaExprIndex i = 0u; i < task.term->expr.len; i++)
                    if (task.elems[i] != task.term->expr.elems[i]) {
                        unchanged = false;
                        break;
                    }
                built = unchanged
                    ? task.term
                    : atom_expr(arena, task.elems, task.result_len);
            } else {
                built = atom_expr(arena, task.elems, task.result_len);
            }
            *task.destination = built;
            if (!built || !abt_transform_memo_store(
                    &memo, task.term, task.depth, built))
                goto fail;
            abt_active_leave(&active, task.term);
            continue;
        }

        Atom *current = task.term;
        Atom *memoized = abt_transform_memo_lookup(
            &memo, current, task.depth);
        if (memoized) {
            *task.destination = memoized;
            continue;
        }
        if (abt_is_quote_form(current)) {
            if (!abt_transform_memo_store(
                    &memo, current, task.depth, current))
                goto fail;
            *task.destination = current;
            continue;
        }
        uint64_t variable = 0u;
        int var_status = abt_idx_index(current, &variable);
        if (var_status < 0 || (var_status > 0 && variable >= task.depth))
            goto fail;
        if (var_status > 0) {
            uint64_t level = CETTA_ABT_MUTATION == 19
                ? variable : task.depth - variable - 1u;
            if (name_offset > UINT64_MAX - level) goto fail;
            Atom *name = abt_print_name(arena, name_offset + level);
            if (!name || !abt_transform_memo_store(
                    &memo, current, task.depth, name))
                goto fail;
            *task.destination = name;
            continue;
        }
        if (current->kind != ATOM_EXPR) {
            *task.destination = current;
            if (!abt_transform_memo_store(
                    &memo, current, task.depth, current))
                goto fail;
            continue;
        }
        if (current->expr.len > 0u &&
            atom_is_symbol(current->expr.elems[0], "ABTBind"))
            goto fail;
        if (!abt_active_enter(&active, current)) goto fail;

        bool symbolic_head = current->expr.len > 0u &&
            current->expr.elems[0]->kind == ATOM_SYMBOL;
        const AbtSignatureEntry *entry = NULL;
        uint32_t arity = 0u;
        if (symbolic_head) {
            if (!cetta_expr_len_fits_u32(current->expr.len - 1u)) goto fail;
            arity = (uint32_t)(current->expr.len - 1u);
            entry = abt_signature_lookup(
                signature, current->expr.elems[0]->sym_id, arity);
        }
        uint32_t binder_count = abt_entry_max_depth(entry);
        bool wrapped = binder_count > 0u;
        CettaExprLen result_len = current->expr.len;
        if (wrapped) {
            if (result_len > UINT64_MAX - 2u) goto fail;
            result_len += 2u;
        }
        if (!cetta_expr_len_mul_fits_size(result_len, sizeof(Atom *)))
            goto fail;
        Atom **elems = result_len
            ? arena_alloc(arena, sizeof(*elems) * (size_t)result_len)
            : NULL;
        if (wrapped) {
            if ((uint64_t)binder_count + 1u > UINT64_MAX ||
                !cetta_expr_len_mul_fits_size(
                    (CettaExprLen)binder_count + 1u, sizeof(Atom *)))
                goto fail;
            Atom **names = arena_alloc(
                arena, sizeof(*names) * ((size_t)binder_count + 1u));
            names[0] = atom_symbol(arena, "Binders");
            for (uint32_t i = 0u; i < binder_count; i++) {
                if (task.depth > UINT64_MAX - (uint64_t)i ||
                    name_offset > UINT64_MAX - (task.depth + (uint64_t)i))
                    goto fail;
                names[i + 1u] = abt_print_name(
                    arena, name_offset + task.depth + (uint64_t)i);
                if (!names[i + 1u]) goto fail;
            }
            elems[0] = atom_symbol(arena, "ABTBind");
            elems[1] = current->expr.elems[0];
            elems[2] = atom_expr(
                arena, names, (CettaExprLen)binder_count + 1u);
        } else if (symbolic_head) {
            elems[0] = current->expr.elems[0];
        }
        if (!abt_print_stack_push(&stack, (AbtPrintTask){
                ABT_PRINT_BUILD, current, task.depth, task.destination,
                elems, result_len, wrapped}))
            goto fail;

        CettaExprIndex first = symbolic_head ? 1u : 0u;
        for (CettaExprIndex i = current->expr.len; i > first; i--) {
            CettaExprIndex child = i - 1u;
            uint32_t field_depth = entry
                ? entry->depths[(uint32_t)(child - 1u)] : 0u;
            uint64_t child_depth = 0u;
            if (!abt_depth_add(task.depth, field_depth, &child_depth)) goto fail;
            CettaExprIndex destination = wrapped ? child + 2u : child;
            if (!abt_print_stack_push(&stack, (AbtPrintTask){
                    ABT_PRINT_VISIT, current->expr.elems[child], child_depth,
                    &elems[destination], NULL, 0u, false}))
                goto fail;
        }
        if (!symbolic_head && current->expr.len > 0u &&
            !abt_print_stack_push(&stack, (AbtPrintTask){
                ABT_PRINT_VISIT, current->expr.elems[0], task.depth,
                &elems[0], NULL, 0u, false}))
            goto fail;
    }

    abt_transform_memo_free(&memo);
    abt_active_free(&active);
    abt_print_stack_free(&stack);
    return result;

fail:
    abt_transform_memo_free(&memo);
    abt_active_free(&active);
    abt_print_stack_free(&stack);
    return NULL;
}

typedef struct AbtNameBinding AbtNameBinding;
struct AbtNameBinding {
    Atom *name;
    uint64_t level;
    AbtNameBinding *previous_same;
};

typedef struct {
    Atom *name;
    AbtNameBinding *top;
} AbtNameSlot;

typedef struct {
    AbtNameSlot inline_slots[64];
    AbtNameSlot *slots;
    size_t cap;
    size_t used;
    uint64_t depth;
} AbtNameEnv;

static size_t abt_name_hash(Atom *name) {
    return (size_t)atom_hash(name);
}

static void abt_name_env_init(AbtNameEnv *env) {
    env->slots = env->inline_slots;
    env->cap = sizeof(env->inline_slots) / sizeof(env->inline_slots[0]);
    env->used = 0u;
    env->depth = 0u;
    memset(env->slots, 0, sizeof(env->inline_slots));
}

static void abt_name_env_free(AbtNameEnv *env) {
    if (env->slots != env->inline_slots) free(env->slots);
}

static bool abt_name_env_grow(AbtNameEnv *env) {
    if (env->cap > SIZE_MAX / 2u ||
        env->cap * 2u > SIZE_MAX / sizeof(*env->slots))
        return false;
    size_t old_cap = env->cap;
    size_t next_cap = old_cap * 2u;
    AbtNameSlot *old_slots = env->slots;
    AbtNameSlot *next = cetta_malloc(sizeof(*next) * next_cap);
    memset(next, 0, sizeof(*next) * next_cap);
    env->slots = next;
    env->cap = next_cap;
    for (size_t i = 0u; i < old_cap; i++) {
        if (!old_slots[i].name) continue;
        size_t pos = abt_name_hash(old_slots[i].name) & (next_cap - 1u);
        while (next[pos].name)
            pos = (pos + 1u) & (next_cap - 1u);
        next[pos] = old_slots[i];
    }
    if (old_slots != env->inline_slots) free(old_slots);
    return true;
}

static AbtNameSlot *abt_name_env_slot(AbtNameEnv *env, Atom *name,
                                      bool create) {
    if (!name) return NULL;
    if (create && (env->used + 1u) * 4u >= env->cap * 3u &&
        !abt_name_env_grow(env))
        return NULL;
    size_t mask = env->cap - 1u;
    size_t pos = abt_name_hash(name) & mask;
    for (;;) {
        AbtNameSlot *slot = &env->slots[pos];
        if (slot->name && atom_eq(slot->name, name)) return slot;
        if (!slot->name) {
            if (!create) return NULL;
            slot->name = name;
            slot->top = NULL;
            env->used++;
            return slot;
        }
        pos = (pos + 1u) & mask;
    }
}

static bool abt_name_env_enter(AbtNameEnv *env, Arena *arena,
                               Atom *names, uint32_t count) {
    if (env->depth > UINT64_MAX - (uint64_t)count) return false;
    for (uint32_t i = 0u; i < count; i++) {
        Atom *name = names->expr.elems[i + 1u];
        AbtNameSlot *slot = abt_name_env_slot(env, name, true);
        if (!slot) return false;
        AbtNameBinding *binding = arena_alloc(arena, sizeof(*binding));
        binding->name = name;
        binding->level = env->depth;
        binding->previous_same = slot->top;
        slot->top = binding;
        env->depth++;
    }
    return true;
}

static bool abt_name_env_leave(AbtNameEnv *env, Atom *names,
                               uint32_t count) {
    if (env->depth < (uint64_t)count) return false;
    for (uint32_t i = count; i > 0u; i--) {
        Atom *name = names->expr.elems[i];
        AbtNameSlot *slot = abt_name_env_slot(env, name, false);
        env->depth--;
        if (!slot || !slot->top || !atom_eq(slot->top->name, name) ||
            slot->top->level != env->depth)
            return false;
        slot->top = slot->top->previous_same;
    }
    return true;
}

static AbtNameBinding *abt_name_env_lookup(AbtNameEnv *env, Atom *name) {
    AbtNameSlot *slot = abt_name_env_slot(env, name, false);
    return slot ? slot->top : NULL;
}

typedef enum {
    ABT_PARSE_VISIT,
    ABT_PARSE_BUILD_PLAIN,
    ABT_PARSE_BUILD_BIND,
    ABT_PARSE_ENTER,
    ABT_PARSE_LEAVE,
} AbtParseTaskKind;

typedef struct {
    AbtParseTaskKind kind;
    Atom *term;
    Atom **destination;
    Atom **elems;
    Atom *names;
    uint32_t count;
    CettaExprLen result_len;
} AbtParseTask;

typedef struct {
    AbtParseTask inline_tasks[64];
    AbtParseTask *tasks;
    size_t len;
    size_t cap;
} AbtParseStack;

static void abt_parse_stack_init(AbtParseStack *stack) {
    stack->tasks = stack->inline_tasks;
    stack->len = 0u;
    stack->cap = sizeof(stack->inline_tasks) / sizeof(stack->inline_tasks[0]);
}

static void abt_parse_stack_free(AbtParseStack *stack) {
    if (stack->tasks != stack->inline_tasks) free(stack->tasks);
}

static bool abt_parse_stack_push(AbtParseStack *stack, AbtParseTask task) {
    if (stack->len == stack->cap) {
        if (stack->cap > SIZE_MAX / 2u ||
            stack->cap * 2u > SIZE_MAX / sizeof(*stack->tasks))
            return false;
        size_t next_cap = stack->cap * 2u;
        if (stack->tasks == stack->inline_tasks) {
            AbtParseTask *next = cetta_malloc(sizeof(*next) * next_cap);
            memcpy(next, stack->inline_tasks, sizeof(*next) * stack->len);
            stack->tasks = next;
        } else {
            stack->tasks = cetta_realloc(
                stack->tasks, sizeof(*stack->tasks) * next_cap);
        }
        stack->cap = next_cap;
    }
    stack->tasks[stack->len++] = task;
    return true;
}

static bool abt_parse_binders(Atom *names, uint32_t count) {
    if (!names || names->kind != ATOM_EXPR ||
        names->expr.len != (CettaExprLen)count + 1u ||
        !atom_is_symbol(names->expr.elems[0], "Binders"))
        return false;
    for (uint32_t i = 0u; i < count; i++) {
        Atom *name = names->expr.elems[i + 1u];
        if (!abt_surface_binder_name(name)) return false;
        if (CETTA_ABT_MUTATION == 22) continue;
        for (uint32_t j = 0u; j < i; j++)
            if (atom_eq(names->expr.elems[j + 1u], name))
                return false;
    }
    return true;
}

Atom *abt_parse(const AbtSignature *signature, Arena *arena, Atom *surface) {
    if (!signature || !arena || !surface) return NULL;
    Atom *result = NULL;
    AbtParseStack stack;
    AbtActiveSet active;
    AbtNameEnv env;
    abt_parse_stack_init(&stack);
    abt_active_init(&active);
    abt_name_env_init(&env);
    if (!abt_parse_stack_push(&stack, (AbtParseTask){
            ABT_PARSE_VISIT, surface, &result, NULL, NULL, 0u, 0u}))
        goto fail;

    while (stack.len > 0u) {
        AbtParseTask task = stack.tasks[--stack.len];
        if (task.kind == ABT_PARSE_ENTER) {
            if (!abt_name_env_enter(&env, arena, task.names, task.count))
                goto fail;
            continue;
        }
        if (task.kind == ABT_PARSE_LEAVE) {
            if (!abt_name_env_leave(&env, task.names, task.count)) goto fail;
            continue;
        }
        if (task.kind == ABT_PARSE_BUILD_PLAIN ||
            task.kind == ABT_PARSE_BUILD_BIND) {
            Atom *built = NULL;
            if (task.kind == ABT_PARSE_BUILD_PLAIN) {
                bool unchanged = true;
                for (CettaExprIndex i = 0u; i < task.term->expr.len; i++)
                    if (task.elems[i] != task.term->expr.elems[i]) {
                        unchanged = false;
                        break;
                    }
                built = unchanged
                    ? task.term
                    : atom_expr(arena, task.elems, task.result_len);
            } else {
                built = atom_expr(arena, task.elems, task.result_len);
            }
            if (!built) goto fail;
            *task.destination = built;
            abt_active_leave(&active, task.term);
            continue;
        }

        Atom *current = task.term;
        if (abt_surface_binder_name(current)) {
            AbtNameBinding *binding = abt_name_env_lookup(&env, current);
            if (!binding) {
                if (current->kind == ATOM_SYMBOL ||
                    abt_is_quote_form(current)) {
                    *task.destination = current;
                    continue;
                }
            } else {
                if (binding->level >= env.depth) goto fail;
                *task.destination = abt_make_idx(
                    arena, env.depth - binding->level - 1u);
                if (!*task.destination) goto fail;
                continue;
            }
        }
        if (current->kind != ATOM_EXPR) {
            *task.destination = current;
            continue;
        }
        if (abt_is_quote_form(current)) {
            *task.destination = current;
            continue;
        }
        uint64_t forbidden_index = 0u;
        if (abt_idx_index(current, &forbidden_index) != 0) goto fail;

        bool is_bind = current->expr.len > 0u &&
            atom_is_symbol(current->expr.elems[0], "ABTBind");
        if (is_bind) {
            if (current->expr.len < 3u ||
                current->expr.elems[1]->kind != ATOM_SYMBOL ||
                !cetta_expr_len_fits_u32(current->expr.len - 3u))
                goto fail;
            uint32_t arity = (uint32_t)(current->expr.len - 3u);
            const AbtSignatureEntry *entry = abt_signature_lookup(
                signature, current->expr.elems[1]->sym_id, arity);
            uint32_t binder_count = abt_entry_max_depth(entry);
            Atom *names = current->expr.elems[2];
            if (!entry || binder_count == 0u ||
                !abt_parse_binders(names, binder_count) ||
                !abt_active_enter(&active, current) ||
                !cetta_expr_len_mul_fits_size(
                    (CettaExprLen)arity + 1u, sizeof(Atom *)))
                goto fail;
            Atom **elems = arena_alloc(
                arena, sizeof(*elems) * ((size_t)arity + 1u));
            elems[0] = current->expr.elems[1];
            if (!abt_parse_stack_push(&stack, (AbtParseTask){
                    ABT_PARSE_BUILD_BIND, current, task.destination, elems,
                    NULL, 0u, (CettaExprLen)arity + 1u}))
                goto fail;
            for (uint32_t i = arity; i > 0u; i--) {
                uint32_t child = i - 1u;
                uint32_t field_depth = CETTA_ABT_MUTATION == 20
                    ? 0u : entry->depths[child];
                if (!abt_parse_stack_push(&stack, (AbtParseTask){
                        ABT_PARSE_LEAVE, NULL, NULL, NULL, names,
                        field_depth, 0u}) ||
                    !abt_parse_stack_push(&stack, (AbtParseTask){
                        ABT_PARSE_VISIT, current->expr.elems[child + 3u],
                        &elems[child + 1u], NULL, NULL, 0u, 0u}) ||
                    !abt_parse_stack_push(&stack, (AbtParseTask){
                        ABT_PARSE_ENTER, NULL, NULL, NULL, names,
                        field_depth, 0u}))
                    goto fail;
            }
            continue;
        }

        bool symbolic_head = current->expr.len > 0u &&
            current->expr.elems[0]->kind == ATOM_SYMBOL;
        if (symbolic_head) {
            if (!cetta_expr_len_fits_u32(current->expr.len - 1u)) goto fail;
            const AbtSignatureEntry *entry = abt_signature_lookup(
                signature, current->expr.elems[0]->sym_id,
                (uint32_t)(current->expr.len - 1u));
            if (abt_entry_max_depth(entry) > 0u) goto fail;
        }
        if (!abt_active_enter(&active, current) ||
            !cetta_expr_len_mul_fits_size(current->expr.len, sizeof(Atom *)))
            goto fail;
        Atom **elems = current->expr.len
            ? arena_alloc(arena, sizeof(*elems) * (size_t)current->expr.len)
            : NULL;
        if (symbolic_head) elems[0] = current->expr.elems[0];
        if (!abt_parse_stack_push(&stack, (AbtParseTask){
                ABT_PARSE_BUILD_PLAIN, current, task.destination, elems,
                NULL, 0u, current->expr.len}))
            goto fail;
        CettaExprIndex first = symbolic_head ? 1u : 0u;
        for (CettaExprIndex i = current->expr.len; i > first; i--)
            if (!abt_parse_stack_push(&stack, (AbtParseTask){
                    ABT_PARSE_VISIT, current->expr.elems[i - 1u],
                    &elems[i - 1u], NULL, NULL, 0u, 0u}))
                goto fail;
        if (!symbolic_head && current->expr.len > 0u &&
            !abt_parse_stack_push(&stack, (AbtParseTask){
                ABT_PARSE_VISIT, current->expr.elems[0], &elems[0],
                NULL, NULL, 0u, 0u}))
            goto fail;
    }

    if (env.depth != 0u || !result ||
        !abt_scope_check(signature, 0u, result))
        goto fail;
    abt_name_env_free(&env);
    abt_active_free(&active);
    abt_parse_stack_free(&stack);
    return result;

fail:
    abt_name_env_free(&env);
    abt_active_free(&active);
    abt_parse_stack_free(&stack);
    return NULL;
}

bool abt_scope_check(const AbtSignature *signature, uint64_t initial_depth,
                     Atom *term) {
    if (!signature || !term) return false;
    AbtTaskStack stack;
    AbtActiveSet active;
    abt_tasks_init(&stack);
    abt_active_init(&active);
    if (!abt_task_push(&stack, (AbtTask){
            ABT_TASK_VISIT, term, initial_depth, NULL, NULL}))
        goto fail;
    while (stack.len > 0) {
        AbtTask task = stack.tasks[--stack.len];
        if (task.kind == ABT_TASK_BUILD) {
            abt_active_leave(&active, task.term);
            continue;
        }
        if (abt_is_quote_form(task.term)) {
            continue;
        }
        uint64_t variable = 0;
        int var_status = abt_idx_index(task.term, &variable);
        if (var_status < 0 || (var_status > 0 && variable >= task.depth))
            goto fail;
        if (var_status > 0 || task.term->kind != ATOM_EXPR) continue;
        if (!abt_active_enter(&active, task.term) ||
            !abt_task_push(&stack, (AbtTask){
                ABT_TASK_BUILD, task.term, task.depth, NULL, NULL}))
            goto fail;
        bool symbolic_head = task.term->expr.len > 0 &&
            task.term->expr.elems[0]->kind == ATOM_SYMBOL;
        const AbtSignatureEntry *entry = NULL;
        if (symbolic_head) {
            if (!cetta_expr_len_fits_u32(task.term->expr.len - 1u)) goto fail;
            entry = abt_signature_lookup(
                signature, task.term->expr.elems[0]->sym_id,
                (uint32_t)(task.term->expr.len - 1u));
        }
        CettaExprIndex first = symbolic_head ? 1u : 0u;
        for (CettaExprIndex i = task.term->expr.len; i > first; i--) {
            CettaExprIndex child = i - 1u;
            uint32_t field_depth = entry
                ? entry->depths[(uint32_t)(child - 1u)] : 0u;
            uint64_t child_depth = 0;
            if (!abt_depth_add(task.depth, field_depth, &child_depth) ||
                !abt_task_push(&stack, (AbtTask){
                    ABT_TASK_VISIT, task.term->expr.elems[child], child_depth,
                    NULL, NULL}))
                goto fail;
        }
        if (!symbolic_head && task.term->expr.len > 0 &&
            !abt_task_push(&stack, (AbtTask){
                ABT_TASK_VISIT, task.term->expr.elems[0], task.depth,
                NULL, NULL}))
            goto fail;
    }
    abt_active_free(&active);
    abt_tasks_free(&stack);
    return true;

fail:
    abt_active_free(&active);
    abt_tasks_free(&stack);
    return false;
}

typedef enum {
    ABT_PAIR_VISIT,
    ABT_PAIR_LEAVE,
} AbtPairTaskKind;

typedef struct {
    AbtPairTaskKind kind;
    Atom *left;
    Atom *right;
} AbtPairTask;

bool abt_alpha_eq(Atom *left, Atom *right) {
    if (!left || !right) return left == right;
    AbtPairTask inline_tasks[64];
    AbtPairTask *tasks = inline_tasks;
    size_t len = 1u;
    size_t cap = sizeof(inline_tasks) / sizeof(inline_tasks[0]);
    AbtActiveSet left_active;
    AbtActiveSet right_active;
    abt_active_init(&left_active);
    abt_active_init(&right_active);
    tasks[0] = (AbtPairTask){ABT_PAIR_VISIT, left, right};
    while (len > 0) {
        AbtPairTask task = tasks[--len];
        if (task.kind == ABT_PAIR_LEAVE) {
            abt_active_leave(&left_active, task.left);
            abt_active_leave(&right_active, task.right);
            continue;
        }

        if (task.left->kind != task.right->kind) goto unequal;
        if (task.left->kind != ATOM_EXPR) {
            if (!atom_eq(task.left, task.right)) goto unequal;
            continue;
        }
        if (abt_is_quote_form(task.left) || abt_is_quote_form(task.right)) {
            if (!abt_is_quote_form(task.left) ||
                !abt_is_quote_form(task.right) ||
                !atom_eq(task.left, task.right))
                goto unequal;
            continue;
        }
        uint64_t left_index = 0;
        uint64_t right_index = 0;
        if (abt_idx_index(task.left, &left_index) < 0 ||
            abt_idx_index(task.right, &right_index) < 0)
            goto unequal;
        if (task.left->expr.len != task.right->expr.len) goto unequal;

        /* Canonical ABTs are acyclic.  Reject recurrence on either active
           structural path so equality itself cannot become a cycle-driven
           memory blow-up, even when the two cyclic pointers are identical. */
        if (!abt_active_enter(&left_active, task.left) ||
            !abt_active_enter(&right_active, task.right))
            goto unequal;

        size_t child_count = (size_t)task.left->expr.len;
        if (child_count > SIZE_MAX - 1u ||
            len > SIZE_MAX - (child_count + 1u))
            goto unequal;
        size_t required = child_count + 1u;
        while (len + required > cap) {
            if (cap > SIZE_MAX / 2u ||
                cap * 2u > SIZE_MAX / sizeof(*tasks))
                goto unequal;
            size_t next_cap = cap * 2u;
            if (tasks == inline_tasks) {
                AbtPairTask *next = cetta_malloc(sizeof(*next) * next_cap);
                memcpy(next, inline_tasks, sizeof(*next) * len);
                tasks = next;
            } else {
                tasks = cetta_realloc(
                    tasks, sizeof(*tasks) * next_cap);
            }
            cap = next_cap;
        }
        tasks[len++] = (AbtPairTask){
            ABT_PAIR_LEAVE, task.left, task.right};
        for (CettaExprIndex i = task.left->expr.len; i > 0u; i--) {
            CettaExprIndex child = i - 1u;
            tasks[len++] = (AbtPairTask){
                ABT_PAIR_VISIT,
                task.left->expr.elems[child],
                task.right->expr.elems[child]};
        }
    }
    abt_active_free(&left_active);
    abt_active_free(&right_active);
    if (tasks != inline_tasks) free(tasks);
    return true;

unequal:
    abt_active_free(&left_active);
    abt_active_free(&right_active);
    if (tasks != inline_tasks) free(tasks);
    return false;
}

static Atom *abt_call_expr(Arena *arena, Atom *head, Atom **args,
                           uint32_t nargs) {
    Atom **elems = arena_alloc(
        arena, sizeof(*elems) * ((size_t)nargs + 1u));
    elems[0] = head;
    for (uint32_t i = 0; i < nargs; i++) elems[i + 1u] = args[i];
    return atom_expr(arena, elems, (CettaExprLen)nargs + 1u);
}

static Atom *abt_grounded_error(Arena *arena, Atom *head, Atom **args,
                                uint32_t nargs, const char *reason) {
    return atom_error(arena, abt_call_expr(arena, head, args, nargs),
                      atom_symbol(arena, reason));
}

static bool abt_grounded_int(Atom *atom, int64_t *value) {
    if (!atom || atom->kind != ATOM_GROUNDED ||
        atom->ground.gkind != GV_INT)
        return false;
    *value = atom->ground.ival;
    return true;
}

static bool abt_signature_from_atom(AbtSignature *signature, Arena *arena,
                                    Atom *set) {
    abt_signature_init(signature);
    if (!abt_signature_add_defaults(signature, arena) ||
        !abt_signature_add_set(signature, set)) {
        abt_signature_free(signature);
        return false;
    }
    return true;
}

typedef enum {
    ABT_OP_NONE,
    ABT_OP_DEFAULT_SIGNATURES,
    ABT_OP_SIGNATURE_ADMITTED,
    ABT_OP_SHIFT,
    ABT_OP_SUBST,
    ABT_OP_CLOSE,
    ABT_OP_OPEN,
    ABT_OP_BIND,
    ABT_OP_PRINT,
    ABT_OP_PARSE,
    ABT_OP_SCOPE_CHECK,
    ABT_OP_ALPHA_EQ,
} AbtOpKind;

/* Hot paths compare interned SymbolIds, not bytes.  These fixed internal
 * operators are part of the builtin symbol set, so classification is both
 * side-effect free and independent of SymbolTable pointer lifetime. */
static AbtOpKind abt_op_kind(SymbolId id) {
    if (id == g_builtin_syms.abt_default_signatures)
        return ABT_OP_DEFAULT_SIGNATURES;
    if (id == g_builtin_syms.abt_signature_admitted)
        return ABT_OP_SIGNATURE_ADMITTED;
    if (id == g_builtin_syms.abt_shift)
        return ABT_OP_SHIFT;
    if (id == g_builtin_syms.abt_subst)
        return ABT_OP_SUBST;
    if (id == g_builtin_syms.abt_close)
        return ABT_OP_CLOSE;
    if (id == g_builtin_syms.abt_open)
        return ABT_OP_OPEN;
    if (id == g_builtin_syms.abt_bind)
        return ABT_OP_BIND;
    if (id == g_builtin_syms.abt_print)
        return ABT_OP_PRINT;
    if (id == g_builtin_syms.abt_parse)
        return ABT_OP_PARSE;
    if (id == g_builtin_syms.abt_scope_check)
        return ABT_OP_SCOPE_CHECK;
    if (id == g_builtin_syms.abt_alpha_eq)
        return ABT_OP_ALPHA_EQ;
    return ABT_OP_NONE;
}

bool abt_is_op(SymbolId id) {
    return abt_op_kind(id) != ABT_OP_NONE;
}

bool abt_op_data_arg(SymbolId id, uint32_t arg_index) {
    AbtOpKind kind = abt_op_kind(id);
    if (kind == ABT_OP_DEFAULT_SIGNATURES)
        return arg_index == 0u;
    if (kind == ABT_OP_SIGNATURE_ADMITTED)
        return arg_index == 0u;
    if (kind == ABT_OP_SHIFT)
        return arg_index == 0u || arg_index == 3u;
    if (kind == ABT_OP_SUBST)
        return arg_index == 0u || arg_index == 2u || arg_index == 3u;
    if (kind == ABT_OP_CLOSE || kind == ABT_OP_OPEN ||
        kind == ABT_OP_BIND)
        return arg_index <= 2u;
    if (kind == ABT_OP_PRINT || kind == ABT_OP_PARSE)
        return arg_index <= 1u;
    if (kind == ABT_OP_SCOPE_CHECK)
        return arg_index == 0u || arg_index == 2u;
    if (kind == ABT_OP_ALPHA_EQ)
        return arg_index <= 1u;
    return false;
}

Atom *abt_grounded_dispatch(Arena *arena, Atom *head,
                            Atom **args, uint32_t nargs) {
    if (!head || head->kind != ATOM_SYMBOL) return NULL;
    SymbolId id = head->sym_id;
    AbtOpKind kind = abt_op_kind(id);
    if (kind == ABT_OP_DEFAULT_SIGNATURES) {
        if (nargs != 1u)
            return abt_grounded_error(
                arena, head, args, nargs, "ABTIncorrectArity");
        if (!atom_is_symbol(args[0], "AbtDefaultsV1"))
            return abt_grounded_error(
                arena, head, args, nargs,
                "ABTInvalidDefaultSignatureCapability");
        Atom *set = abt_default_signature_set(arena);
        return set ? set : abt_grounded_error(
            arena, head, args, nargs,
            "ABTDefaultSignaturesUnavailable");
    }
    if (kind == ABT_OP_ALPHA_EQ) {
        if (nargs != 2u)
            return abt_grounded_error(
                arena, head, args, nargs, "ABTIncorrectArity");
        return abt_alpha_eq(args[0], args[1])
            ? atom_true(arena) : atom_false(arena);
    }
    if (!abt_is_op(id)) return NULL;
    if (nargs < 1u)
        return abt_grounded_error(
            arena, head, args, nargs, "ABTIncorrectArity");

    AbtSignature signature;
    bool signature_ok = abt_signature_from_atom(
        &signature, arena, args[0]);
    if (kind == ABT_OP_SIGNATURE_ADMITTED) {
        if (nargs != 1u) {
            if (signature_ok) abt_signature_free(&signature);
            return abt_grounded_error(
                arena, head, args, nargs, "ABTIncorrectArity");
        }
        if (signature_ok) abt_signature_free(&signature);
        return signature_ok ? atom_true(arena) : atom_false(arena);
    }
    if (!signature_ok)
        return abt_grounded_error(
            arena, head, args, nargs, "ABTInvalidSignature");

    Atom *result = NULL;
    if (kind == ABT_OP_SHIFT) {
        int64_t delta = 0, cutoff = 0;
        if (nargs != 4u || !abt_grounded_int(args[1], &delta) ||
            !abt_grounded_int(args[2], &cutoff) || cutoff < 0) {
            abt_signature_free(&signature);
            return abt_grounded_error(
                arena, head, args, nargs, "ABTInvalidArguments");
        }
        result = abt_shift(
            &signature, arena, delta, (uint64_t)cutoff, args[3]);
    } else if (kind == ABT_OP_SUBST) {
        int64_t index = 0;
        if (nargs != 4u || !abt_grounded_int(args[1], &index) || index < 0) {
            abt_signature_free(&signature);
            return abt_grounded_error(
                arena, head, args, nargs, "ABTInvalidArguments");
        }
        result = abt_subst(
            &signature, arena, (uint64_t)index, args[2], args[3]);
    } else if (kind == ABT_OP_CLOSE) {
        if (nargs != 3u) {
            abt_signature_free(&signature);
            return abt_grounded_error(
                arena, head, args, nargs, "ABTIncorrectArity");
        }
        result = abt_close(&signature, arena, args[1], args[2]);
    } else if (kind == ABT_OP_OPEN) {
        if (nargs != 3u) {
            abt_signature_free(&signature);
            return abt_grounded_error(
                arena, head, args, nargs, "ABTIncorrectArity");
        }
        result = abt_open(&signature, arena, args[1], args[2]);
    } else if (kind == ABT_OP_BIND) {
        if (nargs != 3u) {
            abt_signature_free(&signature);
            return abt_grounded_error(
                arena, head, args, nargs, "ABTIncorrectArity");
        }
        result = abt_bind(&signature, arena, args[1], args[2]);
    } else if (kind == ABT_OP_PRINT) {
        if (nargs != 2u) {
            abt_signature_free(&signature);
            return abt_grounded_error(
                arena, head, args, nargs, "ABTIncorrectArity");
        }
        result = abt_print(&signature, arena, args[1]);
    } else if (kind == ABT_OP_PARSE) {
        if (nargs != 2u) {
            abt_signature_free(&signature);
            return abt_grounded_error(
                arena, head, args, nargs, "ABTIncorrectArity");
        }
        result = abt_parse(&signature, arena, args[1]);
    } else if (kind == ABT_OP_SCOPE_CHECK) {
        int64_t depth = 0;
        if (nargs != 3u || !abt_grounded_int(args[1], &depth) || depth < 0) {
            abt_signature_free(&signature);
            return abt_grounded_error(
                arena, head, args, nargs, "ABTInvalidArguments");
        }
        bool valid = abt_scope_check(
            &signature, (uint64_t)depth, args[2]);
        abt_signature_free(&signature);
        return valid ? atom_true(arena) : atom_false(arena);
    }
    abt_signature_free(&signature);
    return result ? result : abt_grounded_error(
        arena, head, args, nargs, "ABTInvalidTerm");
}

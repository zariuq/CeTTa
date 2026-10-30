#include "library_work.h"
#include "eval.h"
#include "shared_transition.h"
#include "space.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static bool source_field_equal(Atom *field, const char *text) {
    size_t length = strlen(text);
    return field && field->kind == ATOM_GROUNDED && field->ground.gkind == GV_STRING &&
           atom_string_len(field) == length && !memcmp(field->ground.sval, text, length);
}

/* The same protected view supplies capture and validation. Epochs retain
 * complete decimal uint64 fields; checking does not allocate another token. */
static Atom *source_view(Arena *arena, Atom *reference, Atom *expected) {
    /* Observe a returned record through its data carrier without executing
     * its contents. The record grants no lifetime or write authority. */
    while (atom_prime_held_is(expected))
        expected = atom_prime_held_payload(expected);
    if (!reference || reference->kind != ATOM_GROUNDED ||
        reference->ground.gkind != GV_SPACE)
        return atom_expr2(arena, atom_symbol(arena, "Work:Fault"), atom_symbol(arena, "InvalidSource"));
    __attribute__((cleanup(cetta_shared_transition_guard_leave)))
    CettaSharedTransitionGuard transition = {0};
    cetta_shared_transition_guard_enter(&transition);
    Space *source = eval_observe_space_read
        ? eval_observe_space_read(reference->ground.ptr)
        : eval_resolve_space_read ? eval_resolve_space_read(reference->ground.ptr) : NULL;
    if (!source)
        return atom_expr2(arena, atom_symbol(arena, "Work:Fault"), atom_symbol(arena, "ClosedSource"));
    SpaceReadToken read = space_read_token(source);
    if (!read.instance_id)
        return atom_expr2(arena, atom_symbol(arena, "Work:Fault"), atom_symbol(arena, "ClosedSource"));
    if (read.base_dependency_epoch == UINT64_MAX)
        return atom_expr2(arena, atom_symbol(arena, "Work:Fault"), atom_symbol(arena, "UnsupportedSourceView"));
    char identity[32], revision[32], dependencies[32];
    snprintf(identity, sizeof(identity), "%" PRIu64, read.instance_id);
    snprintf(revision, sizeof(revision), "%" PRIu64, read.revision);
    snprintf(dependencies, sizeof(dependencies), "%" PRIu64, read.base_dependency_epoch);
    if (expected) {
        if (expected->kind != ATOM_EXPR || expected->expr.len != 4u ||
            !atom_is_symbol(expected->expr.elems[0], "Work:SourceEpoch"))
            return atom_expr2(arena, atom_symbol(arena, "Work:Fault"), atom_symbol(arena, "InvalidSourceEpoch"));
        bool same = source_field_equal(expected->expr.elems[1], identity) &&
                    source_field_equal(expected->expr.elems[2], revision) &&
                    source_field_equal(expected->expr.elems[3], dependencies);
        return atom_symbol(arena, same ? "Work:Current" : "Work:Changed");
    }
    Atom *fields[] = {atom_symbol(arena, "Work:SourceEpoch"),
        atom_string(arena, identity), atom_string(arena, revision), atom_string(arena, dependencies)};
    return atom_expr(arena, fields, 4u);
}

bool cetta_work_module_call(struct CettaLibraryContext *context, struct Space *space,
        Arena *arena, Atom *head, Atom **args, uint32_t count, CettaCallOutcome *out) {
    (void)context; (void)space;
    if (!head || head->kind != ATOM_SYMBOL) return false;
    const char *name = atom_name_cstr(head);
    if (!strcmp(name, "work:source-epoch") && count == 1u) {
        *out = cetta_call_completed_value(source_view(arena, args[0], NULL));
        return true;
    }
    if (!strcmp(name, "work:source-status") && count == 2u) {
        *out = cetta_call_completed_value(source_view(arena, args[0], args[1]));
        return true;
    }
    if (strcmp(name, "work:epoch") || count != 1u) return false;
    Atom *epoch = eval_current_work_epoch(arena, args[0]);
    if (!epoch) epoch = atom_expr2(arena, atom_symbol(arena, "Work:Fault"), atom_symbol(arena, "Released"));
    *out = cetta_call_completed_value(epoch);
    return true;
}

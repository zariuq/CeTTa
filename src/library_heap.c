#include "library_heap.h"
#include "petta_priority_heap.h"
#include "eval.h"
#include "lang.h"
#include "petta_semantics.h"
#include <string.h>

bool cetta_heap_module_call(struct CettaLibraryContext *context, struct Space *space,
        Arena *arena, Atom *head, Atom **args, uint32_t count, CettaCallOutcome *out) {
    (void)context; (void)space;
    if (!head || head->kind != ATOM_SYMBOL) return false;
    const char *name = atom_name_cstr(head), *legacy = NULL;
    bool take = false;
    if (!strcmp(name, "heap:valid?") && count == 1) {
        bool valid = false;
        PettaPriorityHeapStatus status = petta_priority_heap_validate(args[0], eval_native_interrupted, &valid);
        *out = eval_native_interrupted() ? cetta_call_interrupted()
            : cetta_call_completed_value(status == PETTA_PRIORITY_HEAP_ANSWER
                ? (eval_current_language_id() == CETTA_LANGUAGE_PETTA
                    ? petta_semantics_boolean_value(arena, valid) : atom_bool(arena, valid))
                : atom_expr2(arena, atom_symbol(arena, "Heap:Fault"), atom_symbol(arena, "Capacity")));
        return true;
    }
    if (!strcmp(name, "heap:empty") && count == 0) legacy = "empty_heap";
    if (!strcmp(name, "heap:insert") && count == 3) legacy = "add_to_heap";
    if (!strcmp(name, "heap:take") && count == 1) { legacy = "get_from_heap"; take = true; }
    if (!strcmp(name, "heap:size") && count == 1) legacy = "heap_size";
    if (!legacy) return false;
    Atom *items[4] = {atom_symbol(arena, legacy), NULL, NULL, NULL};
    uint32_t arity = take ? 3 : count;
    for (uint32_t i = 0; i < arity; i++) items[i + 1] = i < count ? args[i] : atom_unit(arena);
    Atom *call = atom_expr(arena, items, arity + 1);
    PettaPriorityHeapAnswer answer;
    PettaPriorityHeapStatus status = petta_priority_heap_call(arena, call, &answer);
    if (status == PETTA_PRIORITY_HEAP_DECLINED) return false;
    Atom *value;
    if (status == PETTA_PRIORITY_HEAP_NO_ANSWER) value = atom_symbol(arena, "Heap:Empty");
    else if (status == PETTA_PRIORITY_HEAP_CAPACITY)
        value = atom_expr2(arena, atom_symbol(arena, "Heap:Fault"), atom_symbol(arena, "Capacity"));
    else if (take) {
        Atom *fields[] = {atom_symbol(arena, "Heap:Item"), answer.key, answer.value, answer.result};
        value = atom_expr(arena, fields, 4);
    } else value = answer.result;
    *out = eval_native_interrupted() ? cetta_call_interrupted() : cetta_call_completed_value(value);
    return true;
}

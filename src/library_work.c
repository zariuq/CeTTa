#include "library_work.h"
#include "eval.h"
#include <string.h>

bool cetta_work_module_call(struct CettaLibraryContext *context, struct Space *space,
        Arena *arena, Atom *head, Atom **args, uint32_t count, CettaCallOutcome *out) {
    (void)context; (void)space;
    if (!head || head->kind != ATOM_SYMBOL || count != 1 ||
        strcmp(atom_name_cstr(head), "work:epoch")) return false;
    Atom *epoch = eval_current_work_epoch(arena, args[0]);
    if (!epoch) epoch = atom_expr2(arena, atom_symbol(arena, "Work:Fault"), atom_symbol(arena, "Released"));
    *out = cetta_call_completed_value(epoch);
    return true;
}

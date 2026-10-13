/* Overmarks the inspected producer; does not relocate continuation locals. */
#include "shadow_probe.h"
void gc_probe_visit_prepared(const CettaPreparedPureAnswerCursor *cursor) {
    if (!cursor) return;
    const CettaPreparedPureProgram *p = cursor->program;
    gc_probe_visit_id(p->entry_head, "prepared");
    for (size_t i = 0; i < cursor->argument_len; i++)
        gc_probe_visit_atom(cursor->arguments[i], "prepared");
    for (size_t i = 0; i < cursor->frame_len; i++) {
        const PreparedPureContinuation *k = cursor->frames[i].continuation;
        for (; k; k = k->parent) {
            if (k->equation >= p->equation_len) { gc_probe_opaque("invalid-continuation"); break; }
            for (uint32_t j = 0; j < p->equations[k->equation].local_count; j++)
                gc_probe_visit_atom(k->locals[j], "prepared");
        }
    }
    for (size_t i = 0; i < p->entry_argument_count; i++)
        gc_probe_visit_atom(p->entry_arguments[i], "prepared");
    for (size_t i = 0; i < p->value_len; i++) gc_probe_visit_atom(p->values[i], "prepared");
    for (size_t i = 0; i < p->slot_len; i++) gc_probe_visit_atom(p->slots[i], "prepared");
    for (size_t i = 0; i < p->frame_len; i++) gc_probe_visit_atom(p->frame_atoms[i], "prepared");
    for (size_t i = 0; i < p->code_literal_len; i++) gc_probe_visit_atom(p->code_literals[i], "prepared");
    /* Completed memo entries are overmarked for this diagnostic. Exact
     * ephemeron reclamation and unfinished producers need a stronger owner API. */
    for (size_t i = 0; i < p->memo_len; i++) {
        gc_probe_visit_atom(p->memo_keys[i], "prepared");
        gc_probe_visit_atom(p->memo_values[i], "prepared");
    }
}

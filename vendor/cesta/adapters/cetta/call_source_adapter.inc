typedef struct {
    PrimeNeedArenaAudit *eval_audit;
    PrimeNeedArenaAudit *survivor_audit;
} CestaCallSourceAudit;

static bool cesta_call_source_reference(CestaCallSourceAudit *audit, const Atom *atom) {
    return prime_eval_stack_atom_avoids_both(audit->eval_audit, audit->survivor_audit, atom);
}
static bool cesta_call_source_common(CestaCallSourceAudit *audit, const Bindings *bindings) {
    return prime_eval_stack_audit_held_bindings(audit->eval_audit, audit->survivor_audit, bindings);
}
static bool cesta_call_source_slots(CestaCallSourceAudit *audit, const PrimeNeedCallSourceSlot *slot) {
    while (slot) {
        if (!prime_eval_stack_audit_call_inputs(audit->eval_audit, audit->survivor_audit, slot->inputs)) return false;
        if (!prime_eval_stack_audit_production(audit->eval_audit, audit->survivor_audit, slot->source)) return false;
        slot = slot->next;
    }
    return true;
}
static bool cesta_call_source_forks(CestaCallSourceAudit *audit, const PrimeNeedCallForkPoint *point) {
    while (point) {
        if (!prime_eval_stack_audit_production(audit->eval_audit, audit->survivor_audit, point->origin)) return false;
        if (!prime_eval_stack_audit_production(audit->eval_audit, audit->survivor_audit, point->snapshot)) return false;
        point = point->next;
    }
    return true;
}
static inline bool cesta_call_source_audit(CestaCallSourceAudit *, const PrimeNeedCallSourceCell *);
#ifndef CESTA_RECORD_DECLARE_ONLY
#include "generated/cesta_call_sources.h"
#endif

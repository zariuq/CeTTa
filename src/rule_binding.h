#ifndef CETTA_RULE_BINDING_H
#define CETTA_RULE_BINDING_H

#include "match.h"
#include "term_graph.h"

typedef enum {
    CETTA_RULE_BIND_UNIFY = 0,
    CETTA_RULE_BIND_HEAD,
} CettaRuleBindingPolicy;

enum {
    CETTA_RULE_SYNTAX_ORDINARY = 1u,
    CETTA_RULE_SYNTAX_DIRECTIONAL = 2u,
};

/* Borrowed source view. Admission chooses the permitted declaration forms;
 * matching consumes the resulting policy, never an ambient matching mode.
 * Retaining a descriptor also requires retaining its source syntax. */
typedef struct {
    Atom *source;
    Atom *head;
    Atom *body;
    CettaRuleBindingPolicy binding;
} CettaRuleDescriptor;

/* Recognition is pure. It does not add an atom, evaluate a constructor or
 * grant execution authority to syntax outside the caller's admitted forms. */
static inline bool cetta_rule_view(Atom *source, unsigned admitted_forms,
                                   CettaRuleDescriptor *out) {
    if (!source || !out || source->kind != ATOM_EXPR ||
        source->expr.len != 3u)
        return false;
    CettaRuleBindingPolicy binding;
    if ((admitted_forms & CETTA_RULE_SYNTAX_ORDINARY) &&
        atom_is_symbol_id(source->expr.elems[0], g_builtin_syms.equals))
        binding = CETTA_RULE_BIND_UNIFY;
    else if ((admitted_forms & CETTA_RULE_SYNTAX_DIRECTIONAL) &&
             atom_is_symbol_id(source->expr.elems[0], g_builtin_syms.equals_percent))
        binding = CETTA_RULE_BIND_HEAD;
    else
        return false;
    *out = (CettaRuleDescriptor){source, source->expr.elems[1],
                                source->expr.elems[2], binding};
    return true;
}

/* Prepare one candidate before evaluating any guard or body. `epoch` is a
 * fresh activation identity supplied by the existing frame owner. On success
 * replace `environment` atomically and publish substituted, unevaluated body
 * syntax in `body`. On all other statuses both outputs retain their previous
 * values. Syntax borrows follow the ordinary binding/arena contract; a caller
 * retaining the result across collection must use the existing owner services.
 *
 * HEAD protects the resolved caller during head matching only. Its body may
 * subsequently perform ordinary unification or effects. Grounded leaves use
 * the caller's equality service; completeness assumes the declared finite
 * first-order leaf contract. */
CettaTermMatchStatus cetta_rule_prepare(const CettaRuleDescriptor *rule,
    Atom *query, uint32_t epoch, Arena *arena, Bindings *environment,
    bool (*leaf_equal)(Atom *, Atom *), Atom **body);

#endif

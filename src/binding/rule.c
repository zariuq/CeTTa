#include "rule_binding.h"
#include "term_canon.h"

typedef struct {
    uint32_t epoch;
    Bindings *bindings;
    CettaVarMap images;
} RuleBodyInstance;

static Atom *rule_fresh_variable(Arena *arena, Atom *variable, void *raw) {
    uint32_t epoch = *(uint32_t *)raw;
    return atom_var_like(arena, variable, var_epoch_id(variable->var_id, epoch));
}

static Atom *rule_body_variable(Arena *arena, Atom *variable, void *raw) {
    RuleBodyInstance *instance = raw;
    VarId id = var_epoch_id(variable->var_id, instance->epoch);
    Atom *image = cetta_var_map_lookup(&instance->images, id);
    if (image) return image;
    /* Directional images are already resolved caller subterms. Their free
     * identities are protected and disjoint from this fresh rule frame, so
     * simultaneous substitution is sufficient. Ordinary unification follows
     * the existing substitution service for its potentially chained image. */
    image = instance->bindings
        ? bindings_apply_epoch(instance->bindings, arena, variable, instance->epoch)
        : atom_var_like(arena, variable, id);
    return image && cetta_var_map_add(&instance->images, id, image) ? image : NULL;
}

CettaTermMatchStatus cetta_rule_prepare(const CettaRuleDescriptor *rule,
    Atom *query, uint32_t epoch, Arena *arena, Bindings *environment,
    bool (*leaf_equal)(Atom *, Atom *), Atom **body) {
    if (!rule || !rule->head || !rule->body || !query || !epoch || !arena ||
        !environment || !leaf_equal || !body ||
        (unsigned)rule->binding > CETTA_RULE_BIND_HEAD)
        return CETTA_TERM_MATCH_INVALID;
    Bindings prepared;
    bindings_init(&prepared);
    if (!bindings_clone(&prepared, environment))
        return CETTA_TERM_MATCH_NO_MEMORY;
    RuleBodyInstance instance = {.epoch = epoch};
    cetta_var_map_init(&instance.images);
    CettaTermMatchStatus status;
    if (rule->binding == CETTA_RULE_BIND_UNIFY) {
        instance.bindings = &prepared;
        status = match_atoms_epoch(query, rule->head, &prepared, arena, epoch)
            ? CETTA_TERM_MATCH_OK : CETTA_TERM_MATCH_MISMATCH;
    } else {
        Atom *caller = bindings_apply_graph(&prepared, arena, query);
        Atom *head = cetta_atom_rewrite_vars(arena, rule->head,
                                             rule_fresh_variable, &epoch, false);
        CettaTermMatch matched;
        status = caller && head
            ? term_graph_match_pattern(arena, head, caller, leaf_equal, &matched)
            : CETTA_TERM_MATCH_NO_MEMORY;
        if (status == CETTA_TERM_MATCH_OK &&
            !bindings_add_vars(&prepared, matched.variables, matched.values, matched.count))
            status = CETTA_TERM_MATCH_NO_MEMORY;
        for (uint32_t i = 0u; status == CETTA_TERM_MATCH_OK && i < matched.count; ++i)
            if (!cetta_var_map_add(&instance.images,
                    matched.variables[i]->var_id, matched.values[i]))
                status = CETTA_TERM_MATCH_NO_MEMORY;
    }
    if (status == CETTA_TERM_MATCH_OK) {
        Atom *result = cetta_atom_rewrite_vars(arena, rule->body,
                                               rule_body_variable, &instance, false);
        cetta_var_map_free(&instance.images);
        if (result) {
            bindings_free(environment);
            *environment = prepared;
            *body = result;
            return CETTA_TERM_MATCH_OK;
        }
        status = CETTA_TERM_MATCH_NO_MEMORY;
    } else
        cetta_var_map_free(&instance.images);
    bindings_free(&prepared);
    return status;
}

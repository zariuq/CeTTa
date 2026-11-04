#include "proof_producer.h"
#include "shared_transition.h"
#include <stdlib.h>
#include <string.h>

struct CettaProofProducerPlan {
    Arena syntax;
    CettaProofProducerDescription description;
    Atom *input_state, *output_state, *label;
    Atom *patterns[CETTA_PROOF_SOURCE_MAX], *effect, *answer;
    void *source_holds[CETTA_PROOF_SOURCE_MAX], *target_hold, *program_hold;
};

static CettaProofProducerStatus declined(CettaProofProducerReason *why,
                                          CettaProofProducerReason reason) {
    if (why) *why = reason;
    return CETTA_PROOF_PRODUCER_DECLINED;
}
static bool ordinary_var(const Atom *a) {
    return a && a->kind == ATOM_VAR && !a->name_key && !var_id_is_none(a->var_id);
}
/* These are inspection limits, not an execution quota or completeness law. */
static bool syntax_fragment(const Atom *a, unsigned depth, size_t *left) {
    if (!a || !*left || depth > 64u) return false;
    --*left;
    if (a->kind == ATOM_SYMBOL) return true;
    if (a->kind == ATOM_VAR) return ordinary_var(a);
    if (a->kind == ATOM_GROUNDED) return a->ground.gkind == GV_INT;
    if (a->kind != ATOM_EXPR) return false;
    for (CettaExprIndex i = 0u; i < a->expr.len; i++)
        if (!syntax_fragment(a->expr.elems[i], depth + 1u, left)) return false;
    return true;
}
static size_t occurrences(const Atom *a, VarId variable) {
    if (a->kind == ATOM_VAR) return var_id_eq(a->var_id, variable) ? 1u : 0u;
    size_t count = 0u;
    if (a->kind == ATOM_EXPR)
        for (CettaExprIndex i = 0u; i < a->expr.len; i++)
            count += occurrences(a->expr.elems[i], variable);
    return count;
}
static bool path_valid(const CettaProofFieldPath *path) {
    if (!path->length || path->length > CETTA_PROOF_PATH_MAX) return false;
    for (size_t i = 0u; i < path->length; i++)
        if (!path->child[i]) return false;
    return true;
}
static const Atom *field(const Atom *a, const CettaProofFieldPath *path) {
    for (size_t i = 0u; i < path->length; i++) {
        if (!a || a->kind != ATOM_EXPR || path->child[i] >= a->expr.len) return NULL;
        a = a->expr.elems[path->child[i]];
    }
    return a;
}
static bool same_layout(const Atom *a, const Atom *b,
                         const CettaProofFieldPath *path,
                         CettaExprLen *arities) {
    for (size_t i = 0u; i < path->length; i++) {
        if (!a || !b || a->kind != ATOM_EXPR || b->kind != ATOM_EXPR ||
            a->expr.len != b->expr.len || path->child[i] >= a->expr.len ||
            !a->expr.len || a->expr.elems[0]->kind != ATOM_SYMBOL ||
            b->expr.elems[0]->kind != ATOM_SYMBOL ||
            atom_symbol_current_id(a->expr.elems[0]) != atom_symbol_current_id(b->expr.elems[0])) return false;
        arities[i] = a->expr.len;
        a = a->expr.elems[path->child[i]];
        b = b->expr.elems[path->child[i]];
    }
    return true;
}
static bool native_space(const Space *s) {
    return s && s->native.universe && !s->overlay_base && !space_has_dependencies(s) &&
        (s->match_backend.kind == SPACE_ENGINE_NATIVE ||
         s->match_backend.kind == SPACE_ENGINE_NATIVE_CANDIDATE_EXACT);
}
static Space *source_at(const CettaNativeQuery *q, size_t i) {
    return q->sources ? q->sources[i] : q->source;
}
static CettaProofSourceBinding capture(Space *s, void **hold) {
    *hold = space_lifetime_pin ? space_lifetime_pin(s) : NULL;
    return (CettaProofSourceBinding){
        .read = space_read_token(s), .count = space_length64(s),
        .universe_instance = s->native.universe->instance_id,
        .universe_epoch = s->native.universe->storage_epoch,
        .has_lifetime_hold = *hold != NULL,
        .hold_keeps_storage = *hold && space_lifetime_pin_keeps_storage &&
            space_lifetime_pin_keeps_storage(*hold),
    };
}
static bool current_binding(const CettaProofSourceBinding *binding, Space *live) {
    return live && binding->read.space == live &&
        space_read_token_matches_live_space(binding->read, live) &&
        live->native.universe &&
        live->native.universe->instance_id == binding->universe_instance &&
        live->native.universe->storage_epoch == binding->universe_epoch &&
        space_length64(live) == binding->count;
}

void cetta_proof_producer_free(CettaProofProducerPlan *plan) {
    if (!plan) return;
    /* Syntax/resource finalizers run while all optional owner holds stand. */
    arena_free(&plan->syntax);
    if (space_lifetime_unpin) {
        for (size_t i = 0u; i < plan->description.source_count; i++)
            space_lifetime_unpin(plan->source_holds[i]);
        space_lifetime_unpin(plan->target_hold);
        space_lifetime_unpin(plan->program_hold);
    }
    free(plan);
}

/* Omit exactly one child under the fixed owned layout. All other children
 * remain together, so their cross-subtree variable aliases are retained. */
static Atom *erase_proof(Arena *arena, Atom *a,
                         const CettaProofFieldPath *path, size_t at) {
    CettaExprIndex chosen = path->child[at];
    bool last = at + 1u == path->length;
    CettaExprLen length = a->expr.len - (last ? 1u : 0u);
    Atom *out = atom_expr_builder_begin(arena, length);
    if (!out) return NULL;
    CettaExprIndex next = 0u;
    for (CettaExprIndex i = 0u; i < a->expr.len; i++) {
        if (i == chosen && last) continue;
        Atom *child = i == chosen
            ? erase_proof(arena, a->expr.elems[i], path, at + 1u)
            : a->expr.elems[i];
        if (!child) return NULL;
        out->expr.elems[next++] = child;
    }
    return atom_expr_builder_finish(arena, out);
}

CettaProofProducerStatus cetta_proof_producer_prepare(
    const CettaNativeQuery *q, Space *program,
    uint64_t source_root, uint32_t source_position,
    const CettaProofProducerLayout *layout,
    CettaProofProducerPlan **out, CettaProofProducerReason *why) {
    CETTA_SCOPED_SHARED_TRANSITION(guard);
    if (why) *why = CETTA_PROOF_REASON_NONE;
    if (!out || !q || !q->patterns || !q->pattern_count ||
        q->pattern_count > CETTA_PROOF_SOURCE_MAX ||
        q->consumer != CETTA_NATIVE_QUERY_EFFECTS || !q->effects ||
        q->effect_count != 1u || !q->apply || q->rows || q->guard_space ||
        q->guard_pattern || q->else_atom || q->else_empty || q->filter_count ||
        q->inert_head_count || q->distinct_key || q->afresh || !q->template_atom)
        return declined(why, CETTA_PROOF_REASON_QUERY);
    if (!layout || layout->source_leg >= q->pattern_count ||
        !path_valid(&layout->rank) || !path_valid(&layout->proof))
        return declined(why, CETTA_PROOF_REASON_LAYOUT);
    const CettaNativeQueryEffect *effect = &q->effects[0];
    Space *source = source_at(q, layout->source_leg);
    if (!native_space(program) || !native_space(q->source) || !native_space(source) ||
        !native_space(effect->target) || effect->target != source ||
        !space_is_set(source) || effect->removal || effect->insert ||
        effect->refused_head_count ||
        program->native.universe != source->native.universe ||
        q->source->native.universe != source->native.universe)
        return declined(why, CETTA_PROOF_REASON_SPACE);
    for (size_t i = 0u; i < q->pattern_count; i++) {
        Space *s = source_at(q, i);
        if (!native_space(s) || s->native.universe != source->native.universe ||
            (i != layout->source_leg && s == source))
            return declined(why, CETTA_PROOF_REASON_SPACE);
    }
    size_t left = 4096u;
    for (size_t i = 0u; i < q->pattern_count; i++)
        if (!syntax_fragment(q->patterns[i], 0u, &left))
            return declined(why, CETTA_PROOF_REASON_FRAGMENT);
    if (!syntax_fragment(effect->data, 0u, &left) ||
        !syntax_fragment(q->template_atom, 0u, &left))
        return declined(why, CETTA_PROOF_REASON_FRAGMENT);
    const Atom *input = q->patterns[layout->source_leg], *output = effect->data;
    CettaExprLen arities[CETTA_PROOF_PATH_MAX] = {0};
    if (!same_layout(input, output, &layout->proof, arities))
        return declined(why, CETTA_PROOF_REASON_LAYOUT);
    const Atom *rank = field(input, &layout->rank), *next = field(output, &layout->rank);
    if (!rank || !next || rank->kind != ATOM_GROUNDED || next->kind != ATOM_GROUNDED ||
        rank->ground.gkind != GV_INT || next->ground.gkind != GV_INT ||
        rank->ground.ival <= 0 || next->ground.ival != rank->ground.ival - 1)
        return declined(why, CETTA_PROOF_REASON_RANK);
    const Atom *proof = field(input, &layout->proof), *made = field(output, &layout->proof);
    if (!ordinary_var(proof) || !made || made->kind != ATOM_EXPR || made->expr.len != 2u ||
        !ordinary_var(made->expr.elems[1]) ||
        !var_id_eq(proof->var_id, made->expr.elems[1]->var_id) ||
        occurrences(input, proof->var_id) != 1u ||
        occurrences(output, proof->var_id) != 1u ||
        occurrences(q->template_atom, proof->var_id) != 0u)
        return declined(why, CETTA_PROOF_REASON_PROOF);
    for (size_t i = 0u; i < q->pattern_count; i++)
        if (i != layout->source_leg && occurrences(q->patterns[i], proof->var_id))
            return declined(why, CETTA_PROOF_REASON_PROOF);
    const Atom *label = made->expr.elems[0];
    bool literal = cetta_proof_producer_exact_label(label);
    if (!literal) {
        bool side_bound = false;
        if (!ordinary_var(label) || occurrences(input, label->var_id))
            return declined(why, CETTA_PROOF_REASON_LABEL);
        for (size_t i = 0u; i < q->pattern_count; i++)
            if (i != layout->source_leg && occurrences(q->patterns[i], label->var_id))
                side_bound = true;
        if (!side_bound) return declined(why, CETTA_PROOF_REASON_LABEL);
    }
    /* The first family only counts or returns closed data independent of
     * every source variable. More general answer instantiation is pending. */
    if (atom_has_vars(q->template_atom))
        return declined(why, CETTA_PROOF_REASON_QUERY);
    CettaProofProducerPlan *plan = calloc(1u, sizeof(*plan));
    if (!plan) {
        if (why) *why = CETTA_PROOF_REASON_CAPACITY;
        return CETTA_PROOF_PRODUCER_CAPACITY;
    }
    arena_init_detached(&plan->syntax);
    CettaProofProducerDescription *d = &plan->description;
    d->layout = *layout;
    memcpy(d->proof_ancestor_arity, arities, sizeof(arities));
    d->source_root = source_root; d->source_position = source_position;
    d->source_count = q->pattern_count; d->proof_variable = proof->var_id;
    d->source_rank = rank->ground.ival; d->target_rank = next->ground.ival;
    d->count_only = q->count_only; d->literal_label = literal;
    d->remaining = CETTA_PROOF_NEEDS_SOURCE_AUTHORITY | CETTA_PROOF_NEEDS_SINGLE_CLOSED_SEED |
        CETTA_PROOF_NEEDS_FUNCTIONAL_BATCH | CETTA_PROOF_NEEDS_WORD_SET_UNAMBIGUITY |
        CETTA_PROOF_NEEDS_LABEL_DATA_AUTHORITY | CETTA_PROOF_NEEDS_PUBLICATION_PREFLIGHT |
        CETTA_PROOF_NEEDS_WORK_ADMISSION | CETTA_PROOF_NEEDS_OWNER_ADMISSION |
        (literal ? 0u : CETTA_PROOF_NEEDS_GROUND_LABEL_BINDING);
    d->program = space_program_token(program);
    plan->program_hold = space_lifetime_pin ? space_lifetime_pin(program) : NULL;
    d->program_has_lifetime_hold = plan->program_hold != NULL;
    for (size_t i = 0u; i < q->pattern_count; i++)
        d->sources[i] = capture(source_at(q, i), &plan->source_holds[i]);
    d->target = capture(effect->target, &plan->target_hold);
    AtomDeepCopySession *copy = atom_deep_copy_session_new(&plan->syntax);
    Atom *owned_input = NULL, *owned_output = NULL;
    if (copy) {
        for (size_t i = 0u; i < q->pattern_count; i++) {
            Atom *a = atom_deep_copy_session_copy(copy, q->patterns[i]);
            if (!a || !atom_deep_copy_session_settled(copy, a)) {
                owned_input = NULL;
                break;
            }
            plan->patterns[i] = a;
            if (i == layout->source_leg) owned_input = a;
        }
        owned_output = atom_deep_copy_session_copy(copy, effect->data);
        plan->effect = owned_output;
        plan->answer = atom_deep_copy_session_copy(copy, q->template_atom);
        if (!plan->answer || !owned_output || atom_deep_copy_session_failed(copy) ||
            !atom_deep_copy_session_settled(copy, owned_output) ||
            !atom_deep_copy_session_settled(copy, plan->answer)) owned_input = NULL;
        atom_deep_copy_session_free(copy);
    }
    if (owned_input && owned_output) {
        plan->input_state = erase_proof(&plan->syntax, owned_input, &layout->proof, 0u);
        plan->output_state = erase_proof(&plan->syntax, owned_output, &layout->proof, 0u);
        plan->label = (Atom *)field(owned_output, &layout->proof)->expr.elems[0];
    }
    if (!plan->input_state || !plan->output_state) {
        cetta_proof_producer_free(plan);
        if (why) *why = CETTA_PROOF_REASON_CAPACITY;
        return CETTA_PROOF_PRODUCER_CAPACITY;
    }
    Space *sources[CETTA_PROOF_SOURCE_MAX];
    for (size_t i = 0u; i < q->pattern_count; i++) sources[i] = source_at(q, i);
    if (!cetta_proof_producer_is_current(plan, program, sources, q->pattern_count, effect->target)) {
        cetta_proof_producer_free(plan);
        return declined(why, CETTA_PROOF_REASON_STALE);
    }
    *out = plan;
    return CETTA_PROOF_PRODUCER_SHAPE_ONLY;
}

bool cetta_proof_producer_describe(const CettaProofProducerPlan *plan,
                                  CettaProofProducerDescription *out) {
    if (!plan || !out) return false;
    *out = plan->description;
    return true;
}
const Atom *cetta_proof_producer_state_template(const CettaProofProducerPlan *plan, bool output) {
    return plan ? (output ? plan->output_state : plan->input_state) : NULL;
}
const Atom *cetta_proof_producer_label(const CettaProofProducerPlan *plan) {
    return plan ? plan->label : NULL;
}
bool cetta_proof_producer_is_current(const CettaProofProducerPlan *plan,
    Space *program, Space *const *sources, size_t count, Space *target) {
    CETTA_SCOPED_SHARED_TRANSITION(guard);
    if (!plan || !program || !sources || count != plan->description.source_count ||
        !space_program_token_matches_live_space(plan->description.program, program) ||
        !current_binding(&plan->description.target, target)) return false;
    for (size_t i = 0u; i < count; i++)
        if (!current_binding(&plan->description.sources[i], sources[i])) return false;
    return true;
}

const Atom *cetta_proof_producer_pattern(const CettaProofProducerPlan *plan, size_t leg) {
    return plan && leg < plan->description.source_count ? plan->patterns[leg] : NULL;
}
const Atom *cetta_proof_producer_effect(const CettaProofProducerPlan *plan) {
    return plan ? plan->effect : NULL;
}
const Atom *cetta_proof_producer_answer(const CettaProofProducerPlan *plan) {
    return plan ? plan->answer : NULL;
}

bool cetta_proof_producer_exact_label(const Atom *value) {
    return value && (value->kind == ATOM_SYMBOL ||
        (value->kind == ATOM_GROUNDED && value->ground.gkind == GV_INT));
}

#include "regex_trie_v1.h"
#include "regex_selection_internal_v1.h"

#include <stdlib.h>
#include <string.h>

typedef struct { uint32_t pc, node; size_t bytes; bool consumed; } LiteralFrame;
static size_t scalar_width(uint32_t cp) {
    return cp < 0x80u ? 1u : cp < 0x800u ? 2u : cp < 0x10000u ? 3u : 4u;
}
CettaScalarTrie *cetta_re_v1_literal_trie_build(const CettaReProgramV1 *p) {
    if (!p || p->extended || p->group_len != 1u) return NULL;
    size_t n = p->instruction_len;
    uint32_t *incoming = calloc(n, sizeof(*incoming));
    uint8_t *seen = calloc(n, 1u);
    LiteralFrame *stack = malloc(n * sizeof(*stack));
    CettaScalarTrie *trie = NULL;
    if (!incoming || !seen || !stack) goto done;
    size_t len = 1u; stack[0] = (LiteralFrame){.pc = p->entry}; seen[p->entry] = 1u;
    while (len) {
        uint32_t pc = stack[--len].pc; CettaReInstructionV1 op = p->instructions[pc];
        if (op.opcode != CETTA_RE_V1_MATCH && op.opcode != CETTA_RE_V1_FAIL &&
            op.opcode != CETTA_RE_V1_CHAR && op.opcode != CETTA_RE_V1_JUMP &&
            op.opcode != CETTA_RE_V1_SPLIT) goto done;
        if (op.opcode <= CETTA_RE_V1_MATCH) continue;
        uint32_t targets[2] = {op.next, op.second}; unsigned count = op.opcode == CETTA_RE_V1_SPLIT ? 2u : 1u;
        for (unsigned i = 0; i < count; ++i) {
            uint32_t target = targets[i]; uint32_t kind = p->instructions[target].opcode;
            if (target == p->entry || (++incoming[target] > 1u &&
                kind != CETTA_RE_V1_MATCH && kind != CETTA_RE_V1_FAIL)) goto done;
            if (!seen[target]) { seen[target] = 1u; stack[len++] = (LiteralFrame){.pc = target}; }
        }
    }
    trie = cetta_scalar_trie_new(); if (!trie) goto done;
    len = 1u; uint64_t ordinal = 0u;
    stack[0] = (LiteralFrame){.pc = p->entry};
    while (len) {
        LiteralFrame f = stack[--len]; CettaReInstructionV1 op = p->instructions[f.pc];
        if (op.opcode == CETTA_RE_V1_FAIL) continue;
        if (op.opcode == CETTA_RE_V1_MATCH) {
            if (!cetta_scalar_trie_terminal(trie, f.node, ordinal++, f.bytes)) goto reject;
        } else if (op.opcode == CETTA_RE_V1_SPLIT) {
            if (f.consumed) goto reject;
            LiteralFrame right = f; right.pc = op.second; stack[len++] = right;
            f.pc = op.next; stack[len++] = f;
        } else {
            if (op.opcode == CETTA_RE_V1_CHAR) {
                if (!cetta_scalar_trie_extend(trie, f.node, op.argument, &f.node) ||
                    f.bytes > SIZE_MAX - scalar_width(op.argument)) goto reject;
                f.bytes += scalar_width(op.argument); f.consumed = true;
            }
            f.pc = op.next; stack[len++] = f;
        }
    }
    if (!cetta_scalar_trie_prepare(trie)) goto reject;
    goto done;
reject:
    cetta_scalar_trie_free(trie); trie = NULL;
done:
    free(incoming); free(seen); free(stack); return trie;
}
bool cetta_re_v1_literal_accelerated(const CettaReProgramV1 *p) { return p && p->literal_trie; }
CettaReResultV1 cetta_re_v1_literal_find(const CettaReProgramV1 *p, const uint8_t *bytes,
    size_t len, size_t start, CettaReModeV1 mode, const CettaReExecutionPolicyV1 *policy) {
    CettaReResultV1 result = {.outcome = CETTA_RE_V1_NO_MATCH};
    const CettaScalarTrie *trie = p->literal_trie;
    uint32_t node = 0u, cp; size_t after, at = start;
    bool found = false; size_t best_start = 0, best_end = 0; uint64_t best_ordinal = UINT64_MAX;
    for (;;) {
        if (policy && policy->cancelled && policy->cancelled(policy->context)) {
            result.outcome = CETTA_RE_V1_CANCELLED; return result;
        }
        if (policy && policy->work_budget && result.state_visits >= policy->work_budget) {
            result.outcome = CETTA_RE_V1_INCOMPLETE; return result;
        }
        uint64_t rank; size_t length; ++result.state_visits;
        if (cetta_scalar_trie_best(trie, node, mode == CETTA_RE_V1_SEARCH, &rank, &length) &&
            length <= at - start && (mode != CETTA_RE_V1_FULL || at == len)) {
            size_t candidate_start = at - length;
            if (!found || candidate_start < best_start ||
                (candidate_start == best_start && rank < best_ordinal)) {
                found = true; best_start = candidate_start; best_end = at; best_ordinal = rank;
            }
        }
        if (at == len || (found && ((!best_ordinal && best_start == start) ||
            at - best_start >= cetta_scalar_trie_max_length(trie)))) break;
        if (!cetta_scalar_trie_decode(bytes, len, at, &cp, &after)) {
            result.outcome = CETTA_RE_V1_INVALID_UTF8; return result;
        }
        node = mode == CETTA_RE_V1_SEARCH ? cetta_scalar_trie_advance(trie, node, cp) :
                                          cetta_scalar_trie_child(trie, node, cp);
        at = after;
        if (node == CETTA_TRIE_NONE) break;
    }
    if (found) {
        result.groups = calloc(1u, sizeof(*result.groups));
        if (!result.groups) { result.outcome = CETTA_RE_V1_ALLOCATION_FAILURE; return result; }
        result.groups[0] = (CettaReCaptureV1){true, best_start, best_end};
        result.group_len = 1u; result.outcome = CETTA_RE_V1_FOUND;
    }
    return result;
}

typedef struct { size_t position; uint32_t previous; bool has_previous, matched; } NameState;
struct CettaReNameAutomatonV1 {
    CettaReProgramV1 *program; CettaReModeV1 mode; CettaReExecutionPolicyV1 policy;
    CettaTrieAutomaton view; void *initial;
    uint8_t *seen, *active; uint32_t *stack;
    uint64_t visits; CettaReOutcomeV1 outcome;
};
static bool name_tick(CettaReNameAutomatonV1 *a) {
    if (a->policy.cancelled && a->policy.cancelled(a->policy.context)) {
        a->outcome = CETTA_RE_V1_CANCELLED; return false;
    }
    if (a->policy.work_budget && a->visits >= a->policy.work_budget) {
        a->outcome = CETTA_RE_V1_INCOMPLETE; return false;
    }
    ++a->visits; return true;
}
static bool name_contains(const CettaReRangeV1 *ranges, uint32_t len, uint32_t cp) {
    for (uint32_t i = 0; i < len; ++i) if (ranges[i].low <= cp && cp <= ranges[i].high) return true;
    return false;
}
static bool name_asserts(CettaReNameAutomatonV1 *a, uint32_t which, const NameState *s,
                        bool has_next, uint32_t next) {
    switch (which) {
    case CETTA_RE_V1_TEXT_START: return !s->has_previous;
    case CETTA_RE_V1_TEXT_END: return !has_next;
    case CETTA_RE_V1_LINE_START: return !s->has_previous || s->previous == '\n';
    case CETTA_RE_V1_LINE_END: return !has_next || next == '\n';
    default: {
        static const CettaReRangeV1 ascii[] = {{48,57},{65,90},{95,95},{97,122}};
        bool ascii_mode = which == CETTA_RE_V1_ASCII_WORD_BOUNDARY || which == CETTA_RE_V1_NOT_ASCII_WORD_BOUNDARY;
        const CettaReRangeV1 *ranges = ascii_mode ? ascii : a->program->word_ranges;
        uint32_t len = ascii_mode ? 4u : a->program->word_range_len;
        bool left = s->has_previous && name_contains(ranges, len, s->previous);
        bool right = has_next && name_contains(ranges, len, next);
        return which == CETTA_RE_V1_WORD_BOUNDARY || which == CETTA_RE_V1_ASCII_WORD_BOUNDARY ? left != right : left == right;
    }
    }
}
static void name_push(CettaReNameAutomatonV1 *a, uint32_t pc, size_t *len) {
    if (!a->seen[pc]) { a->seen[pc] = 1u; a->stack[(*len)++] = pc; }
}
static bool name_closure(CettaReNameAutomatonV1 *a, const NameState *s,
                         bool has_next, uint32_t next, bool *matched) {
    const CettaReProgramV1 *p = a->program; size_t n = p->instruction_len, len = 0;
    const uint8_t *raw = (const uint8_t *)(s + 1);
    memset(a->seen, 0, n); memset(a->active, 0, n); *matched = false;
    for (uint32_t pc = 0; pc < n; ++pc) if (raw[pc]) name_push(a, pc, &len);
    if (a->mode == CETTA_RE_V1_SEARCH) name_push(a, p->entry, &len);
    while (len) {
        if (!name_tick(a)) return false;
        uint32_t pc = a->stack[--len]; CettaReInstructionV1 op = p->instructions[pc];
        switch (op.opcode) {
        case CETTA_RE_V1_MATCH: *matched = true; break;
        case CETTA_RE_V1_FAIL: break;
        case CETTA_RE_V1_JUMP: case CETTA_RE_V1_SAVE: name_push(a, op.next, &len); break;
        case CETTA_RE_V1_SPLIT: name_push(a, op.next, &len); name_push(a, op.second, &len); break;
        case CETTA_RE_V1_ASSERT:
            if (name_asserts(a, op.argument, s, has_next, next)) name_push(a, op.next, &len);
            break;
        default: a->active[pc] = 1u; break;
        }
    }
    return true;
}
static int name_step(void *context, const void *source, uint32_t cp, void *destination) {
    CettaReNameAutomatonV1 *a = context; const NameState *s = source; NameState *d = destination;
    bool matched; if (!name_closure(a, s, true, cp, &matched)) return -1;
    *d = (NameState){s->position + 1u, cp, true,
        a->mode != CETTA_RE_V1_FULL && (s->matched || matched)};
    uint8_t *raw = (uint8_t *)(d + 1); memset(raw, 0, a->program->instruction_len);
    bool alive = d->matched || a->mode == CETTA_RE_V1_SEARCH;
    for (uint32_t pc = 0; pc < a->program->instruction_len; ++pc) if (a->active[pc]) {
        CettaReInstructionV1 op = a->program->instructions[pc];
        if (op.opcode == CETTA_RE_V1_ANY || (op.opcode == CETTA_RE_V1_CHAR && op.argument == cp) ||
            (op.opcode == CETTA_RE_V1_CLASS && name_contains(a->program->ranges + op.argument, op.second, cp))) {
            raw[op.next] = 1u; alive = true;
        }
    }
    return alive ? 1 : 0;
}
static int name_accepts(void *context, const void *state) {
    CettaReNameAutomatonV1 *a = context; const NameState *s = state;
    bool matched; if (!name_closure(a, s, false, 0u, &matched)) return -1;
    return matched || (a->mode != CETTA_RE_V1_FULL && s->matched);
}
bool cetta_re_v1_name_automaton(const CettaReProgramV1 *p, CettaReModeV1 mode,
    const CettaReExecutionPolicyV1 *policy, CettaReNameAutomatonV1 **out) {
    if (!p || !out || p->extended || mode > CETTA_RE_V1_FULL) return false;
    /* Raw/restored graphs need not satisfy the authored compiler's capture
     * flow theorem. A terminal can then reject reversed capture endpoints.
     * Erasing SAVE would conceal that fault, so keep selected filtering until
     * a checked native capture-flow certificate is available. */
    for (uint32_t pc = 0; pc < p->instruction_len; ++pc)
        if (p->instructions[pc].opcode == CETTA_RE_V1_SAVE) return false;
    size_t state_size = p->instruction_len;
    if (state_size + sizeof(NameState) < state_size) return false;
    CettaReNameAutomatonV1 *a = calloc(1u, sizeof(*a)); if (!a) return false;
    a->program = cetta_re_v1_retain((CettaReProgramV1 *)p);
    a->mode = mode; a->outcome = CETTA_RE_V1_FOUND;
    if (policy) a->policy = *policy;
    a->view = (CettaTrieAutomaton){sizeof(NameState) + state_size, a, name_step, name_accepts};
    a->initial = calloc(1u, a->view.state_size); a->seen = calloc(p->instruction_len, 1u);
    a->active = calloc(p->instruction_len, 1u); a->stack = malloc((size_t)p->instruction_len * sizeof(*a->stack));
    if (!a->program || !a->initial || !a->seen || !a->active || !a->stack) {
        cetta_re_v1_name_automaton_free(a); return false;
    }
    ((uint8_t *)((NameState *)a->initial + 1))[p->entry] = 1u;
    CettaReNameAutomatonV1 *old = *out; *out = a; cetta_re_v1_name_automaton_free(old); return true;
}
const CettaTrieAutomaton *cetta_re_v1_name_automaton_view(CettaReNameAutomatonV1 *a) { return a ? &a->view : NULL; }
const void *cetta_re_v1_name_automaton_initial(const CettaReNameAutomatonV1 *a) { return a ? a->initial : NULL; }
CettaReOutcomeV1 cetta_re_v1_name_automaton_outcome(const CettaReNameAutomatonV1 *a) {
    return a ? a->outcome : CETTA_RE_V1_ALLOCATION_FAILURE;
}
void cetta_re_v1_name_automaton_free(CettaReNameAutomatonV1 *a) {
    if (!a) return;
    cetta_re_v1_release(a->program); free(a->initial); free(a->seen); free(a->active); free(a->stack); free(a);
}

#include "gslt_ground_dense_term_v1.h"

#include "gslt_epoch_slots_v1.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    CETTA_GSLT_GROUND_DENSE_RIGID_NODE_V1,
    CETTA_GSLT_GROUND_DENSE_SLOT_NODE_V1,
    CETTA_GSLT_GROUND_DENSE_EXPRESSION_NODE_V1,
} CettaGsltGroundDenseNodeKindV1;

typedef struct {
    CettaGsltGroundDenseNodeKindV1 kind;
    Atom *source;
    uint32_t slot;
    uint32_t edge_begin;
    uint32_t edge_len;
    bool has_slots;
} CettaGsltGroundDenseNodeV1;

typedef struct {
    CettaGsltGroundDenseNodeV1 *nodes;
    uint32_t node_len;
    uint32_t node_cap;
    uint32_t *edges;
    uint32_t edge_len;
    uint32_t edge_cap;
    uint32_t root;
    uint32_t maximum_edge_len;
    uint32_t variable_width;
    uint32_t *first_binding_offsets;
    uint32_t first_binding_count;
    VarId first_variable;
    bool variable_linear;
} CettaGsltGroundDenseTermProgramImplV1;

typedef struct {
    Atom *source;
    uint32_t edge_begin;
    uint32_t next_child;
    bool entered;
} CettaGsltGroundDenseCompileFrameV1;

typedef struct {
    uint32_t node;
    Atom *target;
    bool source_view;
} CettaGsltGroundDenseMatchPairV1;

typedef struct {
    CettaGsltEpochSlotsV1 slots;
    CettaGsltGroundDenseMatchPairV1 *pairs;
    uint32_t pair_cap;
    Atom **results;
    uint32_t result_cap;
    Atom **elements;
    uint32_t element_cap;
    uint32_t matched_width;
    bool has_match;
} CettaGsltGroundDenseWorkspaceImplV1;

static void cetta_gslt_ground_dense_set_error_v1(
    char *error_buf, size_t error_buf_size, const char *format, ...) {
    va_list args;

    if (!error_buf || error_buf_size == 0u)
        return;
    va_start(args, format);
    vsnprintf(error_buf, error_buf_size, format, args);
    va_end(args);
}

static bool cetta_gslt_ground_dense_reserve_v1(
    void **values, uint32_t *capacity, uint32_t required,
    size_t value_size) {
    uint32_t next;
    void *grown;

    if (!values || !capacity || value_size == 0u)
        return false;
    if (required <= *capacity)
        return true;
    next = *capacity != 0u ? *capacity : 16u;
    while (next < required) {
        if (next > UINT32_MAX / 2u) {
            next = required;
            break;
        }
        next *= 2u;
    }
    if ((size_t)next > SIZE_MAX / value_size)
        return false;
    grown = realloc(*values, (size_t)next * value_size);
    if (!grown)
        return false;
    *values = grown;
    *capacity = next;
    return true;
}

static void cetta_gslt_ground_dense_term_program_impl_free_v1(
    CettaGsltGroundDenseTermProgramImplV1 *impl) {
    if (!impl)
        return;
    free(impl->nodes);
    free(impl->edges);
    free(impl->first_binding_offsets);
    free(impl);
}

void cetta_gslt_ground_dense_term_program_init_v1(
    CettaGsltGroundDenseTermProgramV1 *program) {
    if (program)
        program->impl = NULL;
}

void cetta_gslt_ground_dense_term_program_free_v1(
    CettaGsltGroundDenseTermProgramV1 *program) {
    if (!program)
        return;
    cetta_gslt_ground_dense_term_program_impl_free_v1(program->impl);
    program->impl = NULL;
}

static bool cetta_gslt_ground_dense_push_node_v1(
    CettaGsltGroundDenseTermProgramImplV1 *impl,
    CettaGsltGroundDenseNodeV1 node,
    uint32_t *node_out) {
    if (!impl || !node_out || impl->node_len == UINT32_MAX ||
        !cetta_gslt_ground_dense_reserve_v1(
            (void **)&impl->nodes, &impl->node_cap,
            impl->node_len + 1u, sizeof(*impl->nodes)))
        return false;
    *node_out = impl->node_len;
    impl->nodes[impl->node_len++] = node;
    return true;
}

static bool cetta_gslt_ground_dense_reserve_edges_v1(
    CettaGsltGroundDenseTermProgramImplV1 *impl,
    uint32_t count,
    uint32_t *begin_out) {
    uint32_t required;

    if (!impl || !begin_out || count > UINT32_MAX - impl->edge_len)
        return false;
    required = impl->edge_len + count;
    if (!cetta_gslt_ground_dense_reserve_v1(
            (void **)&impl->edges, &impl->edge_cap,
            required, sizeof(*impl->edges)))
        return false;
    *begin_out = impl->edge_len;
    impl->edge_len = required;
    return true;
}

static bool cetta_gslt_ground_dense_finish_compile_node_v1(
    CettaGsltGroundDenseCompileFrameV1 *stack,
    uint32_t stack_len,
    CettaGsltGroundDenseTermProgramImplV1 *impl,
    CettaGsltGroundDenseNodeV1 node,
    uint32_t *root_out) {
    uint32_t node_index;

    if (!stack || stack_len == 0u || !impl || !root_out ||
        !cetta_gslt_ground_dense_push_node_v1(
            impl, node, &node_index))
        return false;
    if (stack_len == 1u) {
        *root_out = node_index;
    } else {
        CettaGsltGroundDenseCompileFrameV1 *parent =
            &stack[stack_len - 2u];
        uint32_t child = parent->next_child - 1u;
        if (!parent->entered || parent->next_child == 0u ||
            child >= parent->source->expr.len ||
            parent->edge_begin > impl->edge_len ||
            child >= impl->edge_len - parent->edge_begin)
            return false;
        impl->edges[parent->edge_begin + child] = node_index;
    }
    return true;
}

static bool cetta_gslt_ground_dense_compile_impl_v1(
    CettaGsltGroundDenseTermProgramImplV1 *impl,
    Atom *source,
    char *error_buf,
    size_t error_buf_size) {
    CettaGsltGroundDenseCompileFrameV1 *stack = NULL;
    uint32_t stack_len = 0u;
    uint32_t stack_cap = 0u;
    uint8_t *seen = NULL;
    bool ok = false;

    if (impl->variable_width != 0u) {
        seen = calloc(impl->variable_width, sizeof(*seen));
        impl->first_binding_offsets = malloc(
            (size_t)impl->variable_width *
            sizeof(*impl->first_binding_offsets));
        if (!seen || !impl->first_binding_offsets) {
            cetta_gslt_ground_dense_set_error_v1(
                error_buf, error_buf_size,
                "cannot allocate dense variable admission state");
            goto done;
        }
        for (uint32_t slot = 0u; slot < impl->variable_width; slot++)
            impl->first_binding_offsets[slot] = UINT32_MAX;
    }
    if (!cetta_gslt_ground_dense_reserve_v1(
            (void **)&stack, &stack_cap, 1u, sizeof(*stack))) {
        cetta_gslt_ground_dense_set_error_v1(
            error_buf, error_buf_size,
            "cannot allocate dense term compiler stack");
        goto done;
    }
    stack[stack_len++] = (CettaGsltGroundDenseCompileFrameV1){
        .source = source,
    };
    impl->variable_linear = true;
    while (stack_len != 0u) {
        CettaGsltGroundDenseCompileFrameV1 *frame =
            &stack[stack_len - 1u];
        Atom *atom = frame->source;

        if (!atom) {
            cetta_gslt_ground_dense_set_error_v1(
                error_buf, error_buf_size,
                "dense term contains a null atom");
            goto done;
        }
        if (!frame->entered) {
            if (atom->kind == ATOM_VAR) {
                uint64_t offset;
                uint32_t slot;
                CettaGsltGroundDenseNodeV1 node;

                if (atom->var_id < impl->first_variable) {
                    cetta_gslt_ground_dense_set_error_v1(
                        error_buf, error_buf_size,
                        "dense term variable precedes its admitted interval");
                    goto done;
                }
                offset = atom->var_id - impl->first_variable;
                if (offset >= impl->variable_width) {
                    cetta_gslt_ground_dense_set_error_v1(
                        error_buf, error_buf_size,
                        "dense term variable exceeds its admitted interval");
                    goto done;
                }
                slot = (uint32_t)offset;
                if (seen[slot] != 0u) {
                    impl->variable_linear = false;
                } else {
                    impl->first_binding_offsets[slot] =
                        impl->first_binding_count++;
                }
                seen[slot] = 1u;
                node = (CettaGsltGroundDenseNodeV1){
                    .kind = CETTA_GSLT_GROUND_DENSE_SLOT_NODE_V1,
                    .source = atom,
                    .slot = slot,
                    .has_slots = true,
                };
                if (!cetta_gslt_ground_dense_finish_compile_node_v1(
                        stack, stack_len, impl, node, &impl->root)) {
                    cetta_gslt_ground_dense_set_error_v1(
                        error_buf, error_buf_size,
                        "cannot append a dense slot instruction");
                    goto done;
                }
                stack_len--;
                continue;
            }
            if (atom->kind != ATOM_EXPR) {
                CettaGsltGroundDenseNodeV1 node;

                if (atom->kind != ATOM_SYMBOL &&
                    atom->kind != ATOM_GROUNDED) {
                    cetta_gslt_ground_dense_set_error_v1(
                        error_buf, error_buf_size,
                        "dense term contains an unsupported atom kind");
                    goto done;
                }
                node = (CettaGsltGroundDenseNodeV1){
                    .kind = CETTA_GSLT_GROUND_DENSE_RIGID_NODE_V1,
                    .source = atom,
                };
                if (!cetta_gslt_ground_dense_finish_compile_node_v1(
                        stack, stack_len, impl, node, &impl->root)) {
                    cetta_gslt_ground_dense_set_error_v1(
                        error_buf, error_buf_size,
                        "cannot append a dense rigid instruction");
                    goto done;
                }
                stack_len--;
                continue;
            }
            if (!cetta_expr_len_fits_u32(atom->expr.len) ||
                (atom->expr.len != 0u && !atom->expr.elems) ||
                !cetta_gslt_ground_dense_reserve_edges_v1(
                    impl, (uint32_t)atom->expr.len,
                    &frame->edge_begin)) {
                cetta_gslt_ground_dense_set_error_v1(
                    error_buf, error_buf_size,
                    "dense expression exceeds the admitted program width");
                goto done;
            }
            frame->entered = true;
        }
        if (frame->next_child < atom->expr.len) {
            Atom *child = atom->expr.elems[frame->next_child++];
            if (stack_len == UINT32_MAX ||
                !cetta_gslt_ground_dense_reserve_v1(
                    (void **)&stack, &stack_cap, stack_len + 1u,
                    sizeof(*stack))) {
                cetta_gslt_ground_dense_set_error_v1(
                    error_buf, error_buf_size,
                    "dense term compiler stack is exhausted");
                goto done;
            }
            stack[stack_len++] =
                (CettaGsltGroundDenseCompileFrameV1){.source = child};
            continue;
        }
        {
            uint32_t edge_len = (uint32_t)atom->expr.len;
            bool has_slots = false;
            CettaGsltGroundDenseNodeV1 node;

            for (uint32_t edge = 0u; edge < edge_len; edge++) {
                uint32_t child = impl->edges[frame->edge_begin + edge];
                if (child >= impl->node_len) {
                    cetta_gslt_ground_dense_set_error_v1(
                        error_buf, error_buf_size,
                        "dense expression contains an invalid child");
                    goto done;
                }
                has_slots = has_slots || impl->nodes[child].has_slots;
            }
            node = (CettaGsltGroundDenseNodeV1){
                .kind = CETTA_GSLT_GROUND_DENSE_EXPRESSION_NODE_V1,
                .source = atom,
                .edge_begin = frame->edge_begin,
                .edge_len = edge_len,
                .has_slots = has_slots,
            };
            if (edge_len > impl->maximum_edge_len)
                impl->maximum_edge_len = edge_len;
            if (!cetta_gslt_ground_dense_finish_compile_node_v1(
                    stack, stack_len, impl, node, &impl->root)) {
                cetta_gslt_ground_dense_set_error_v1(
                    error_buf, error_buf_size,
                    "cannot append a dense expression instruction");
                goto done;
            }
            stack_len--;
        }
    }
    ok = impl->node_len != 0u && impl->root < impl->node_len;
    if (!ok)
        cetta_gslt_ground_dense_set_error_v1(
            error_buf, error_buf_size,
            "dense term compiler produced no root");

done:
    free(seen);
    free(stack);
    return ok;
}

bool cetta_gslt_ground_dense_term_compile_v1(
    CettaGsltGroundDenseTermProgramV1 *program,
    Atom *source,
    VarId first_variable,
    uint32_t variable_width,
    char *error_buf,
    size_t error_buf_size) {
    CettaGsltGroundDenseTermProgramImplV1 *replacement;

    if (error_buf && error_buf_size != 0u)
        error_buf[0] = '\0';
    if (!program || !source || first_variable == VAR_ID_NONE ||
        (variable_width != 0u &&
         (uint64_t)(variable_width - 1u) >
             UINT64_MAX - first_variable)) {
        cetta_gslt_ground_dense_set_error_v1(
            error_buf, error_buf_size,
            "invalid dense term compilation request");
        return false;
    }
    replacement = calloc(1u, sizeof(*replacement));
    if (!replacement) {
        cetta_gslt_ground_dense_set_error_v1(
            error_buf, error_buf_size,
            "cannot allocate dense term program");
        return false;
    }
    replacement->first_variable = first_variable;
    replacement->variable_width = variable_width;
    if (!cetta_gslt_ground_dense_compile_impl_v1(
            replacement, source, error_buf, error_buf_size)) {
        cetta_gslt_ground_dense_term_program_impl_free_v1(replacement);
        return false;
    }
    cetta_gslt_ground_dense_term_program_impl_free_v1(program->impl);
    program->impl = replacement;
    return true;
}

uint32_t cetta_gslt_ground_dense_term_width_v1(
    const CettaGsltGroundDenseTermProgramV1 *program) {
    const CettaGsltGroundDenseTermProgramImplV1 *impl =
        program ? program->impl : NULL;
    return impl ? impl->variable_width : 0u;
}

uint32_t cetta_gslt_ground_dense_term_node_count_v1(
    const CettaGsltGroundDenseTermProgramV1 *program) {
    const CettaGsltGroundDenseTermProgramImplV1 *impl =
        program ? program->impl : NULL;
    return impl ? impl->node_len : 0u;
}

bool cetta_gslt_ground_dense_term_is_linear_v1(
    const CettaGsltGroundDenseTermProgramV1 *program) {
    const CettaGsltGroundDenseTermProgramImplV1 *impl =
        program ? program->impl : NULL;
    return impl && impl->variable_linear;
}

bool cetta_gslt_ground_dense_term_first_binding_offset_v1(
        const CettaGsltGroundDenseTermProgramV1 *program,
        VarId source_variable, uint32_t *offset_out) {
    const CettaGsltGroundDenseTermProgramImplV1 *impl =
        program ? program->impl : NULL;
    uint64_t slot;

    if (!impl || !offset_out ||
        source_variable < impl->first_variable)
        return false;
    slot = source_variable - impl->first_variable;
    if (slot >= impl->variable_width ||
        !impl->first_binding_offsets ||
        impl->first_binding_offsets[slot] == UINT32_MAX)
        return false;
    *offset_out = impl->first_binding_offsets[slot];
    return true;
}

void cetta_gslt_ground_dense_workspace_init_v1(
    CettaGsltGroundDenseWorkspaceV1 *workspace) {
    if (workspace)
        workspace->impl = NULL;
}

void cetta_gslt_ground_dense_workspace_free_v1(
    CettaGsltGroundDenseWorkspaceV1 *workspace) {
    CettaGsltGroundDenseWorkspaceImplV1 *impl;

    if (!workspace)
        return;
    impl = workspace->impl;
    if (impl) {
        cetta_gslt_epoch_slots_free_v1(&impl->slots);
        free(impl->pairs);
        free(impl->results);
        free(impl->elements);
        free(impl);
    }
    workspace->impl = NULL;
}

void cetta_gslt_ground_dense_workspace_discard_match_v1(
    CettaGsltGroundDenseWorkspaceV1 *workspace) {
    CettaGsltGroundDenseWorkspaceImplV1 *impl =
        workspace ? workspace->impl : NULL;
    if (impl)
        impl->has_match = false;
}

static CettaGsltGroundDenseWorkspaceImplV1 *
cetta_gslt_ground_dense_workspace_get_v1(
    CettaGsltGroundDenseWorkspaceV1 *workspace) {
    CettaGsltGroundDenseWorkspaceImplV1 *impl;

    if (!workspace)
        return NULL;
    impl = workspace->impl;
    if (impl)
        return impl;
    impl = calloc(1u, sizeof(*impl));
    if (!impl)
        return NULL;
    workspace->impl = impl;
    return impl;
}

static CettaGsltGroundDenseStatusV1
cetta_gslt_ground_dense_term_match_impl_v1(
    CettaGsltGroundDenseWorkspaceV1 *workspace,
    const CettaGsltGroundDenseTermProgramV1 *program,
    Atom *target,
    bool source_view,
    CettaGsltGroundDenseViewResolveV1 resolve,
    void *resolve_context,
    CettaGsltGroundDenseStatsV1 *stats) {
    const CettaGsltGroundDenseTermProgramImplV1 *program_impl =
        program ? program->impl : NULL;
    CettaGsltGroundDenseWorkspaceImplV1 *workspace_impl;
    uint32_t pair_len = 0u;
    uint32_t old_slot_capacity;
    uint32_t old_pair_capacity;

    if (!program_impl || !target ||
        (!source_view && atom_has_vars(target)))
        return CETTA_GSLT_GROUND_DENSE_INVALID_V1;
    workspace_impl = cetta_gslt_ground_dense_workspace_get_v1(workspace);
    if (!workspace_impl)
        return CETTA_GSLT_GROUND_DENSE_RESOURCE_V1;
    workspace_impl->has_match = false;
    old_slot_capacity = workspace_impl->slots.capacity;
    old_pair_capacity = workspace_impl->pair_cap;
    if (!cetta_gslt_epoch_slots_prepare_v1(
            &workspace_impl->slots, program_impl->variable_width,
            sizeof(Atom *)) ||
        !cetta_gslt_ground_dense_reserve_v1(
            (void **)&workspace_impl->pairs,
            &workspace_impl->pair_cap, 1u,
            sizeof(*workspace_impl->pairs)))
        return CETTA_GSLT_GROUND_DENSE_RESOURCE_V1;
    if (stats) {
        stats->workspace_growths +=
            (workspace_impl->slots.capacity > old_slot_capacity ? 1u : 0u) +
            (workspace_impl->pair_cap > old_pair_capacity ? 1u : 0u);
    }
    old_pair_capacity = workspace_impl->pair_cap;
    workspace_impl->pairs[pair_len++] =
        (CettaGsltGroundDenseMatchPairV1){
            .node = program_impl->root,
            .target = target,
            .source_view = source_view,
        };
    while (pair_len != 0u) {
        CettaGsltGroundDenseMatchPairV1 pair =
            workspace_impl->pairs[--pair_len];
        const CettaGsltGroundDenseNodeV1 *node;
        bool pair_is_view = pair.source_view;

        if (pair.node >= program_impl->node_len || !pair.target)
            return CETTA_GSLT_GROUND_DENSE_INVALID_V1;
        node = &program_impl->nodes[pair.node];
        if (stats)
            stats->match_nodes++;
        if (pair_is_view && stats)
            stats->view_nodes++;
        if (pair_is_view && pair.target->kind == ATOM_VAR) {
            Atom *resolved = NULL;
            CettaGsltGroundDenseStatusV1 resolved_status =
                resolve
                    ? resolve(resolve_context, pair.target, &resolved)
                    : CETTA_GSLT_GROUND_DENSE_DEFER_V1;

            if (resolved_status != CETTA_GSLT_GROUND_DENSE_OK_V1) {
                if (stats &&
                    resolved_status == CETTA_GSLT_GROUND_DENSE_DEFER_V1)
                    stats->view_deferrals++;
                return resolved_status ==
                           CETTA_GSLT_GROUND_DENSE_MISMATCH_V1
                    ? CETTA_GSLT_GROUND_DENSE_INVALID_V1
                    : resolved_status;
            }
            if (!resolved)
                return CETTA_GSLT_GROUND_DENSE_INVALID_V1;
            if (atom_has_vars(resolved)) {
                if (stats)
                    stats->view_deferrals++;
                return CETTA_GSLT_GROUND_DENSE_DEFER_V1;
            }
            if (stats)
                stats->view_variable_resolutions++;
            pair.target = resolved;
            pair_is_view = false;
        } else if (pair_is_view && pair.target->kind != ATOM_EXPR) {
            pair_is_view = false;
        }
        if (node->kind == CETTA_GSLT_GROUND_DENSE_SLOT_NODE_V1) {
            Atom **bound;

            if (node->slot >= program_impl->variable_width)
                return CETTA_GSLT_GROUND_DENSE_INVALID_V1;
            if (pair_is_view && atom_has_vars(pair.target)) {
                if (stats)
                    stats->view_deferrals++;
                return CETTA_GSLT_GROUND_DENSE_DEFER_V1;
            }
            bound = cetta_gslt_epoch_slots_get_v1(
                &workspace_impl->slots, node->slot);
            if (bound) {
                if (program_impl->variable_linear)
                    return CETTA_GSLT_GROUND_DENSE_INVALID_V1;
                if (stats)
                    stats->slot_compares++;
                if (!atom_eq(*bound, pair.target))
                    return CETTA_GSLT_GROUND_DENSE_MISMATCH_V1;
            } else {
                bound = cetta_gslt_epoch_slots_set_v1(
                    &workspace_impl->slots, node->slot);
                if (!bound)
                    return CETTA_GSLT_GROUND_DENSE_INVALID_V1;
                *bound = pair.target;
                if (stats)
                    stats->slot_writes++;
            }
            continue;
        }
        if (node->kind == CETTA_GSLT_GROUND_DENSE_RIGID_NODE_V1 ||
            (node->kind ==
                 CETTA_GSLT_GROUND_DENSE_EXPRESSION_NODE_V1 &&
             !node->has_slots && !pair_is_view)) {
            if (stats)
                stats->rigid_subtrees_compared++;
            if (!atom_eq(node->source, pair.target))
                return CETTA_GSLT_GROUND_DENSE_MISMATCH_V1;
            continue;
        }
        if (node->kind != CETTA_GSLT_GROUND_DENSE_EXPRESSION_NODE_V1 ||
            pair.target->kind != ATOM_EXPR ||
            pair.target->expr.len != node->edge_len ||
            node->edge_begin > program_impl->edge_len ||
            node->edge_len > program_impl->edge_len - node->edge_begin)
            return node->kind ==
                       CETTA_GSLT_GROUND_DENSE_EXPRESSION_NODE_V1
                       ? CETTA_GSLT_GROUND_DENSE_MISMATCH_V1
                       : CETTA_GSLT_GROUND_DENSE_INVALID_V1;
        if (node->edge_len > UINT32_MAX - pair_len ||
            !cetta_gslt_ground_dense_reserve_v1(
                (void **)&workspace_impl->pairs,
                &workspace_impl->pair_cap,
                pair_len + node->edge_len,
                sizeof(*workspace_impl->pairs)))
            return CETTA_GSLT_GROUND_DENSE_RESOURCE_V1;
        if (stats && workspace_impl->pair_cap > old_pair_capacity) {
            stats->workspace_growths++;
            old_pair_capacity = workspace_impl->pair_cap;
        }
        for (uint32_t edge = node->edge_len; edge > 0u; edge--) {
            uint32_t offset = edge - 1u;
            uint32_t child =
                program_impl->edges[node->edge_begin + offset];
            if (child >= program_impl->node_len)
                return CETTA_GSLT_GROUND_DENSE_INVALID_V1;
            workspace_impl->pairs[pair_len++] =
                (CettaGsltGroundDenseMatchPairV1){
                    .node = child,
                    .target = pair.target->expr.elems[offset],
                    .source_view = pair_is_view,
                };
        }
    }
    workspace_impl->matched_width = program_impl->variable_width;
    workspace_impl->has_match = true;
    return CETTA_GSLT_GROUND_DENSE_OK_V1;
}

CettaGsltGroundDenseStatusV1 cetta_gslt_ground_dense_term_match_v1(
    CettaGsltGroundDenseWorkspaceV1 *workspace,
    const CettaGsltGroundDenseTermProgramV1 *program,
    Atom *target,
    CettaGsltGroundDenseStatsV1 *stats) {
    return cetta_gslt_ground_dense_term_match_impl_v1(
        workspace, program, target, false, NULL, NULL, stats);
}

CettaGsltGroundDenseStatusV1 cetta_gslt_ground_dense_term_match_view_v1(
    CettaGsltGroundDenseWorkspaceV1 *workspace,
    const CettaGsltGroundDenseTermProgramV1 *program,
    Atom *source,
    CettaGsltGroundDenseViewResolveV1 resolve,
    void *resolve_context,
    CettaGsltGroundDenseStatsV1 *stats) {
    return cetta_gslt_ground_dense_term_match_impl_v1(
        workspace, program, source, true, resolve, resolve_context, stats);
}

CettaGsltGroundDenseStatusV1 cetta_gslt_ground_dense_term_instantiate_v1(
    CettaGsltGroundDenseWorkspaceV1 *workspace,
    const CettaGsltGroundDenseTermProgramV1 *program,
    Arena *arena,
    Atom **result_out,
    CettaGsltGroundDenseStatsV1 *stats) {
    const CettaGsltGroundDenseTermProgramImplV1 *program_impl =
        program ? program->impl : NULL;
    CettaGsltGroundDenseWorkspaceImplV1 *workspace_impl =
        workspace ? workspace->impl : NULL;
    uint32_t old_result_capacity;
    uint32_t old_element_capacity;

    if (result_out)
        *result_out = NULL;
    if (!program_impl || !workspace_impl || !arena || !result_out ||
        !workspace_impl->has_match ||
        workspace_impl->matched_width != program_impl->variable_width)
        return CETTA_GSLT_GROUND_DENSE_INVALID_V1;
    old_result_capacity = workspace_impl->result_cap;
    old_element_capacity = workspace_impl->element_cap;
    if (!cetta_gslt_ground_dense_reserve_v1(
            (void **)&workspace_impl->results,
            &workspace_impl->result_cap, program_impl->node_len,
            sizeof(*workspace_impl->results)) ||
        !cetta_gslt_ground_dense_reserve_v1(
            (void **)&workspace_impl->elements,
            &workspace_impl->element_cap, program_impl->maximum_edge_len,
            sizeof(*workspace_impl->elements)))
        return CETTA_GSLT_GROUND_DENSE_RESOURCE_V1;
    if (stats) {
        stats->workspace_growths +=
            (workspace_impl->result_cap > old_result_capacity ? 1u : 0u) +
            (workspace_impl->element_cap > old_element_capacity ? 1u : 0u);
    }
    for (uint32_t index = 0u; index < program_impl->node_len; index++) {
        const CettaGsltGroundDenseNodeV1 *node =
            &program_impl->nodes[index];

        if (node->kind == CETTA_GSLT_GROUND_DENSE_SLOT_NODE_V1) {
            Atom *const *bound = cetta_gslt_epoch_slots_get_const_v1(
                &workspace_impl->slots, node->slot);
            if (!bound || !*bound)
                return CETTA_GSLT_GROUND_DENSE_INVALID_V1;
            workspace_impl->results[index] = *bound;
            continue;
        }
        if (node->kind == CETTA_GSLT_GROUND_DENSE_RIGID_NODE_V1 ||
            (node->kind ==
                 CETTA_GSLT_GROUND_DENSE_EXPRESSION_NODE_V1 &&
             !node->has_slots)) {
            workspace_impl->results[index] = node->source;
            if (stats)
                stats->rigid_subtrees_reused++;
            continue;
        }
        if (node->kind != CETTA_GSLT_GROUND_DENSE_EXPRESSION_NODE_V1 ||
            node->edge_begin > program_impl->edge_len ||
            node->edge_len > program_impl->edge_len - node->edge_begin)
            return CETTA_GSLT_GROUND_DENSE_INVALID_V1;
        for (uint32_t edge = 0u; edge < node->edge_len; edge++) {
            uint32_t child =
                program_impl->edges[node->edge_begin + edge];
            if (child >= index || !workspace_impl->results[child])
                return CETTA_GSLT_GROUND_DENSE_INVALID_V1;
            workspace_impl->elements[edge] =
                workspace_impl->results[child];
        }
        workspace_impl->results[index] = atom_expr(
            arena, workspace_impl->elements, node->edge_len);
        if (!workspace_impl->results[index])
            return CETTA_GSLT_GROUND_DENSE_RESOURCE_V1;
        if (stats)
            stats->expression_materializations++;
    }
    if (program_impl->root >= program_impl->node_len ||
        !workspace_impl->results[program_impl->root])
        return CETTA_GSLT_GROUND_DENSE_INVALID_V1;
    *result_out = workspace_impl->results[program_impl->root];
    return CETTA_GSLT_GROUND_DENSE_OK_V1;
}

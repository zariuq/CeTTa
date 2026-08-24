#include "gdl_stratification.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
    GDL_STRATIFICATION_DEFAULT_MAX_RELATIONS_V1 = 65536u,
    GDL_STRATIFICATION_DEFAULT_MAX_EDGES_V1 = 1048576u,
    GDL_STRATIFICATION_DEFAULT_MAX_LOGICAL_DEPTH_V1 = 4096u,
};

typedef struct {
    const char *name;
    size_t arity;
    size_t stratum;
    bool defined;
} GdlStratificationRelationV1;

typedef struct {
    size_t source_form_ordinal;
    size_t source_start_line;
    size_t source_end_line;
    size_t *path;
    size_t path_length;
    size_t head_relation;
    size_t body_relation;
    bool negative;
} GdlStratificationEdgeV1;

struct CettaGdlStratificationV1 {
    Arena arena;
    GdlStratificationRelationV1 *relations;
    size_t relation_count;
    size_t relation_capacity;
    GdlStratificationEdgeV1 *edges;
    size_t edge_count;
    size_t edge_capacity;
    size_t maximum_stratum;
    size_t *negative_cycle_edges;
    size_t negative_cycle_length;
};

typedef struct {
    CettaGdlStratificationV1 *analysis;
    CettaGdlStratificationLimitsV1 limits;
    CettaGdlStratificationKindV1 status;
} GdlStratificationBuilderV1;

static bool gdl_stratification_raw_head_v1(
    const GdlSourceRawExprV1 *expression, const char *head) {
    return expression && !expression->token && expression->count > 0u &&
        expression->items[0] && expression->items[0]->token &&
        strcmp(expression->items[0]->token, head) == 0;
}

static bool gdl_stratification_variable_v1(const char *token) {
    return token && token[0] == '?';
}

static bool gdl_stratification_logical_head_v1(const char *head) {
    return head &&
        (strcmp(head, "and") == 0 || strcmp(head, "or") == 0 ||
         strcmp(head, "not") == 0 || strcmp(head, "distinct") == 0);
}

static bool gdl_stratification_reserve_v1(
    void **items, size_t *capacity, size_t needed, size_t item_size) {
    size_t next;
    if (!items || !capacity || item_size == 0u)
        return false;
    if (*capacity >= needed)
        return true;
    next = *capacity ? *capacity : 16u;
    while (next < needed) {
        if (next > SIZE_MAX / 2u)
            return false;
        next *= 2u;
    }
    if (next > SIZE_MAX / item_size)
        return false;
    *items = cetta_realloc(*items, next * item_size);
    *capacity = next;
    return true;
}

static bool gdl_stratification_signature_v1(
    const GdlSourceRawExprV1 *expression,
    const char **name_out,
    size_t *arity_out) {
    const char *name;
    size_t arity;
    if (!expression || !name_out || !arity_out)
        return false;
    if (expression->token) {
        name = expression->token;
        arity = 0u;
    } else {
        if (expression->count == 0u || !expression->items[0] ||
            !expression->items[0]->token)
            return false;
        name = expression->items[0]->token;
        arity = expression->count - 1u;
    }
    if (!*name || gdl_stratification_variable_v1(name) ||
        gdl_stratification_logical_head_v1(name))
        return false;
    *name_out = name;
    *arity_out = arity;
    return true;
}

static bool gdl_stratification_relation_v1(
    GdlStratificationBuilderV1 *builder,
    const GdlSourceRawExprV1 *expression,
    bool defined,
    size_t *index_out) {
    const char *name;
    size_t arity;
    if (!builder || builder->status !=
            CETTA_GDL_STRATIFICATION_ESTABLISHED_V1 ||
        !gdl_stratification_signature_v1(expression, &name, &arity)) {
        if (builder)
            builder->status =
                CETTA_GDL_STRATIFICATION_OUTSIDE_FRAGMENT_V1;
        return false;
    }
    CettaGdlStratificationV1 *analysis = builder->analysis;
    for (size_t index = 0u; index < analysis->relation_count; index++) {
        GdlStratificationRelationV1 *relation =
            &analysis->relations[index];
        if (relation->arity == arity &&
            strcmp(relation->name, name) == 0) {
            relation->defined = relation->defined || defined;
            *index_out = index;
            return true;
        }
    }
    if (analysis->relation_count == builder->limits.max_relations ||
        !gdl_stratification_reserve_v1(
            (void **)&analysis->relations,
            &analysis->relation_capacity,
            analysis->relation_count + 1u,
            sizeof(*analysis->relations))) {
        builder->status = CETTA_GDL_STRATIFICATION_INCOMPLETE_V1;
        return false;
    }
    char *owned_name = arena_strdup(&analysis->arena, name);
    if (!owned_name) {
        builder->status = CETTA_GDL_STRATIFICATION_ENGINE_FAULT_V1;
        return false;
    }
    size_t index = analysis->relation_count++;
    analysis->relations[index] = (GdlStratificationRelationV1){
        .name = owned_name,
        .arity = arity,
        .defined = defined,
    };
    *index_out = index;
    return true;
}

static bool gdl_stratification_add_edge_v1(
    GdlStratificationBuilderV1 *builder,
    const GdlSourceRawFormV1 *source,
    size_t source_ordinal,
    const size_t *path,
    size_t path_length,
    size_t head_relation,
    const GdlSourceRawExprV1 *body,
    bool negative) {
    CettaGdlStratificationV1 *analysis = builder->analysis;
    size_t body_relation;
    if (!gdl_stratification_relation_v1(
            builder, body, false, &body_relation))
        return false;
    if (analysis->edge_count == builder->limits.max_edges ||
        !gdl_stratification_reserve_v1(
            (void **)&analysis->edges,
            &analysis->edge_capacity,
            analysis->edge_count + 1u,
            sizeof(*analysis->edges))) {
        builder->status = CETTA_GDL_STRATIFICATION_INCOMPLETE_V1;
        return false;
    }
    size_t *owned_path = NULL;
    if (path_length) {
        if (path_length > SIZE_MAX / sizeof(*owned_path)) {
            builder->status = CETTA_GDL_STRATIFICATION_INCOMPLETE_V1;
            return false;
        }
        owned_path = arena_alloc(
            &analysis->arena, path_length * sizeof(*owned_path));
        if (!owned_path) {
            builder->status = CETTA_GDL_STRATIFICATION_ENGINE_FAULT_V1;
            return false;
        }
        memcpy(owned_path, path, path_length * sizeof(*owned_path));
    }
    analysis->edges[analysis->edge_count++] =
        (GdlStratificationEdgeV1){
            .source_form_ordinal = source_ordinal,
            .source_start_line = source->start_line,
            .source_end_line = source->end_line,
            .path = owned_path,
            .path_length = path_length,
            .head_relation = head_relation,
            .body_relation = body_relation,
            .negative = negative,
        };
    return true;
}

static bool gdl_stratification_dependencies_v1(
    GdlStratificationBuilderV1 *builder,
    const GdlSourceRawFormV1 *source,
    size_t source_ordinal,
    const GdlSourceRawExprV1 *expression,
    size_t head_relation,
    size_t *path,
    size_t path_length,
    bool negative) {
    if (!builder || builder->status !=
            CETTA_GDL_STRATIFICATION_ESTABLISHED_V1 ||
        !source || !expression)
        return false;
    if (path_length > builder->limits.max_logical_depth) {
        builder->status = CETTA_GDL_STRATIFICATION_INCOMPLETE_V1;
        return false;
    }
    if (gdl_stratification_raw_head_v1(expression, "distinct")) {
        if (expression->count != 3u)
            builder->status =
                CETTA_GDL_STRATIFICATION_OUTSIDE_FRAGMENT_V1;
        return builder->status == CETTA_GDL_STRATIFICATION_ESTABLISHED_V1;
    }
    if (gdl_stratification_raw_head_v1(expression, "not")) {
        const GdlSourceRawExprV1 *operand = expression->count == 2u
            ? expression->items[1] : NULL;
        if (!operand || (!operand->token && operand->count > 0u &&
                operand->items[0] && operand->items[0]->token &&
                gdl_stratification_logical_head_v1(
                    operand->items[0]->token))) {
            builder->status =
                CETTA_GDL_STRATIFICATION_OUTSIDE_FRAGMENT_V1;
            return false;
        }
        if (path_length == builder->limits.max_logical_depth) {
            builder->status = CETTA_GDL_STRATIFICATION_INCOMPLETE_V1;
            return false;
        }
        path[path_length] = 1u;
        return gdl_stratification_add_edge_v1(
            builder, source, source_ordinal,
            path, path_length + 1u,
            head_relation, operand, !negative);
    }
    bool conjunction = gdl_stratification_raw_head_v1(expression, "and");
    bool disjunction = gdl_stratification_raw_head_v1(expression, "or");
    if (conjunction || disjunction) {
        if ((conjunction && expression->count < 2u) ||
            (disjunction && expression->count < 3u)) {
            builder->status =
                CETTA_GDL_STRATIFICATION_OUTSIDE_FRAGMENT_V1;
            return false;
        }
        if (path_length == builder->limits.max_logical_depth) {
            builder->status = CETTA_GDL_STRATIFICATION_INCOMPLETE_V1;
            return false;
        }
        for (size_t index = 1u; index < expression->count; index++) {
            path[path_length] = index;
            if (!gdl_stratification_dependencies_v1(
                    builder, source, source_ordinal,
                    expression->items[index], head_relation,
                    path, path_length + 1u, negative))
                return false;
        }
        return true;
    }
    return gdl_stratification_add_edge_v1(
        builder, source, source_ordinal,
        path, path_length, head_relation, expression, negative);
}

static CettaGdlStratificationKindV1 gdl_stratification_solve_v1(
    CettaGdlStratificationV1 *analysis) {
    size_t relation_count = analysis->relation_count;
    size_t *predecessor = cetta_malloc(
        (relation_count ? relation_count : 1u) * sizeof(*predecessor));
    for (size_t index = 0u; index < relation_count; index++)
        predecessor[index] = SIZE_MAX;
    size_t changed = SIZE_MAX;
    for (size_t pass = 0u; pass < relation_count; pass++) {
        changed = SIZE_MAX;
        for (size_t edge_index = 0u;
             edge_index < analysis->edge_count; edge_index++) {
            GdlStratificationEdgeV1 *edge = &analysis->edges[edge_index];
            size_t required =
                analysis->relations[edge->body_relation].stratum +
                (edge->negative ? 1u : 0u);
            if (analysis->relations[edge->head_relation].stratum < required) {
                analysis->relations[edge->head_relation].stratum = required;
                predecessor[edge->head_relation] = edge_index;
                changed = edge->head_relation;
            }
        }
        if (changed == SIZE_MAX)
            break;
    }
    if (changed != SIZE_MAX) {
        size_t cursor = changed;
        for (size_t index = 0u; index < relation_count; index++) {
            if (predecessor[cursor] == SIZE_MAX) {
                free(predecessor);
                return CETTA_GDL_STRATIFICATION_ENGINE_FAULT_V1;
            }
            cursor = analysis->edges[predecessor[cursor]].body_relation;
        }
        size_t cycle_start = cursor;
        size_t *cycle = arena_alloc(
            &analysis->arena,
            (relation_count ? relation_count : 1u) * sizeof(*cycle));
        size_t cycle_length = 0u;
        bool has_negative = false;
        do {
            if (cycle_length == relation_count ||
                predecessor[cursor] == SIZE_MAX) {
                free(predecessor);
                return CETTA_GDL_STRATIFICATION_ENGINE_FAULT_V1;
            }
            size_t edge_index = predecessor[cursor];
            cycle[cycle_length++] = edge_index;
            has_negative = has_negative || analysis->edges[edge_index].negative;
            cursor = analysis->edges[edge_index].body_relation;
        } while (cursor != cycle_start);
        free(predecessor);
        if (!has_negative)
            return CETTA_GDL_STRATIFICATION_ENGINE_FAULT_V1;
        analysis->negative_cycle_edges = cycle;
        analysis->negative_cycle_length = cycle_length;
        return CETTA_GDL_STRATIFICATION_REFUTED_NEGATIVE_CYCLE_V1;
    }
    free(predecessor);
    for (size_t index = 0u; index < relation_count; index++)
        if (analysis->relations[index].stratum > analysis->maximum_stratum)
            analysis->maximum_stratum = analysis->relations[index].stratum;
    return CETTA_GDL_STRATIFICATION_ESTABLISHED_V1;
}

CettaGdlStratificationResultV1 cetta_gdl_stratification_construct_v1(
    const GdlSourceRawFormsV1 *forms,
    CettaGdlStratificationLimitsV1 limits) {
    if (!forms)
        return (CettaGdlStratificationResultV1){
            .kind = CETTA_GDL_STRATIFICATION_ENGINE_FAULT_V1,
        };
    if (forms->foreign_lines != 0u)
        return (CettaGdlStratificationResultV1){
            .kind = CETTA_GDL_STRATIFICATION_OUTSIDE_FRAGMENT_V1,
        };
    if (limits.max_relations == 0u)
        limits.max_relations =
            GDL_STRATIFICATION_DEFAULT_MAX_RELATIONS_V1;
    if (limits.max_edges == 0u)
        limits.max_edges = GDL_STRATIFICATION_DEFAULT_MAX_EDGES_V1;
    if (limits.max_logical_depth == 0u)
        limits.max_logical_depth =
            GDL_STRATIFICATION_DEFAULT_MAX_LOGICAL_DEPTH_V1;
    if (limits.max_logical_depth > SIZE_MAX / sizeof(size_t))
        return (CettaGdlStratificationResultV1){
            .kind = CETTA_GDL_STRATIFICATION_INCOMPLETE_V1,
        };

    CettaGdlStratificationV1 *analysis = cetta_malloc(sizeof(*analysis));
    memset(analysis, 0, sizeof(*analysis));
    arena_init(&analysis->arena);
    arena_set_runtime_kind(
        &analysis->arena, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    GdlStratificationBuilderV1 builder = {
        .analysis = analysis,
        .limits = limits,
        .status = CETTA_GDL_STRATIFICATION_ESTABLISHED_V1,
    };
    size_t *path = cetta_malloc(
        limits.max_logical_depth * sizeof(*path));
    for (size_t ordinal = 0u;
         ordinal < forms->count && builder.status ==
             CETTA_GDL_STRATIFICATION_ESTABLISHED_V1; ordinal++) {
        const GdlSourceRawFormV1 *source = &forms->items[ordinal];
        if (!source->selected)
            continue;
        const GdlSourceRawExprV1 *form = source->form;
        if (!form) {
            builder.status = CETTA_GDL_STRATIFICATION_OUTSIDE_FRAGMENT_V1;
            break;
        }
        if (gdl_stratification_raw_head_v1(form, "distinct")) {
            if (form->count != 3u)
                builder.status =
                    CETTA_GDL_STRATIFICATION_OUTSIDE_FRAGMENT_V1;
            continue;
        }
        bool rule = gdl_stratification_raw_head_v1(form, "<=");
        if (rule && form->count < 2u) {
            builder.status = CETTA_GDL_STRATIFICATION_OUTSIDE_FRAGMENT_V1;
            break;
        }
        const GdlSourceRawExprV1 *conclusion = rule
            ? form->items[1] : form;
        size_t head_relation;
        if (!gdl_stratification_relation_v1(
                &builder, conclusion, true, &head_relation))
            break;
        if (!rule)
            continue;
        for (size_t premise = 2u; premise < form->count; premise++) {
            path[0] = premise;
            if (!gdl_stratification_dependencies_v1(
                    &builder, source, ordinal + 1u,
                    form->items[premise], head_relation,
                    path, 1u, false))
                break;
        }
    }
    free(path);
    if (builder.status == CETTA_GDL_STRATIFICATION_ESTABLISHED_V1 &&
        analysis->relation_count == 0u)
        builder.status = CETTA_GDL_STRATIFICATION_OUTSIDE_FRAGMENT_V1;
    if (builder.status == CETTA_GDL_STRATIFICATION_ESTABLISHED_V1)
        builder.status = gdl_stratification_solve_v1(analysis);
    if (builder.status != CETTA_GDL_STRATIFICATION_ESTABLISHED_V1 &&
        builder.status !=
            CETTA_GDL_STRATIFICATION_REFUTED_NEGATIVE_CYCLE_V1) {
        cetta_gdl_stratification_destroy_v1(analysis);
        analysis = NULL;
    }
    return (CettaGdlStratificationResultV1){
        .kind = builder.status,
        .analysis = analysis,
    };
}

void cetta_gdl_stratification_destroy_v1(
    CettaGdlStratificationV1 *analysis) {
    if (!analysis)
        return;
    free(analysis->relations);
    free(analysis->edges);
    arena_free(&analysis->arena);
    free(analysis);
}

size_t cetta_gdl_stratification_relation_count_v1(
    const CettaGdlStratificationV1 *analysis) {
    return analysis ? analysis->relation_count : 0u;
}

size_t cetta_gdl_stratification_edge_count_v1(
    const CettaGdlStratificationV1 *analysis) {
    return analysis ? analysis->edge_count : 0u;
}

size_t cetta_gdl_stratification_maximum_stratum_v1(
    const CettaGdlStratificationV1 *analysis) {
    return analysis ? analysis->maximum_stratum : 0u;
}

bool cetta_gdl_stratification_relation_view_v1(
    const CettaGdlStratificationV1 *analysis,
    size_t index,
    CettaGdlStratifiedRelationViewV1 *view_out) {
    if (!analysis || !view_out || index >= analysis->relation_count)
        return false;
    const GdlStratificationRelationV1 *relation =
        &analysis->relations[index];
    *view_out = (CettaGdlStratifiedRelationViewV1){
        .name = relation->name,
        .arity = relation->arity,
        .stratum = relation->stratum,
        .defined = relation->defined,
    };
    return true;
}

bool cetta_gdl_stratification_edge_view_v1(
    const CettaGdlStratificationV1 *analysis,
    size_t index,
    CettaGdlDependencyEdgeViewV1 *view_out) {
    if (!analysis || !view_out || index >= analysis->edge_count)
        return false;
    const GdlStratificationEdgeV1 *edge = &analysis->edges[index];
    *view_out = (CettaGdlDependencyEdgeViewV1){
        .source_form_ordinal = edge->source_form_ordinal,
        .source_start_line = edge->source_start_line,
        .source_end_line = edge->source_end_line,
        .path = edge->path,
        .path_length = edge->path_length,
        .head_relation = edge->head_relation,
        .body_relation = edge->body_relation,
        .negative = edge->negative,
    };
    return true;
}

size_t cetta_gdl_stratification_negative_cycle_length_v1(
    const CettaGdlStratificationV1 *analysis) {
    return analysis ? analysis->negative_cycle_length : 0u;
}

bool cetta_gdl_stratification_negative_cycle_edge_v1(
    const CettaGdlStratificationV1 *analysis,
    size_t cycle_index,
    CettaGdlDependencyEdgeViewV1 *view_out) {
    if (!analysis || cycle_index >= analysis->negative_cycle_length)
        return false;
    return cetta_gdl_stratification_edge_view_v1(
        analysis, analysis->negative_cycle_edges[cycle_index], view_out);
}

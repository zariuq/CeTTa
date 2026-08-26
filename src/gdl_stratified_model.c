#include "gdl_stratified_model.h"

#include "native_sha256.h"
#include "symbol.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    GDL_STRATIFIED_MODEL_DEFAULT_MAX_VARIABLES_V1 = 64u,
    GDL_STRATIFIED_MODEL_DEFAULT_MAX_BRANCHES_V1 = 262144u,
    GDL_STRATIFIED_MODEL_DEFAULT_MAX_ASSIGNMENTS_V1 = 16777216u,
    GDL_STRATIFIED_MODEL_DEFAULT_MAX_GROUND_INSTANCES_V1 = 16777216u,
    GDL_STRATIFIED_MODEL_DEFAULT_MAX_SUPPORTS_V1 = 4194304u,
    GDL_STRATIFIED_MODEL_DEFAULT_MAX_PROOF_EDGES_V1 = 16777216u,
    GDL_STRATIFIED_MODEL_DEFAULT_MAX_ROUNDS_V1 = 4096u,
    GDL_STRATIFIED_MODEL_DEFAULT_MAX_DEPTH_V1 = 4096u,
    GDL_STRATIFIED_EPISODE_DEFAULT_MAX_FACTS_V1 = 65536u,
};

static const CettaNikDirectAuthorityV1
    g_gdl_stratified_model_authority_v1 = {
        .alias = "gdl-stratified-model-native-v1",
        .system_id = "gdl.stratified-model.native.v1",
        .authority_identity = UINT64_C(0x67646c2e73747261),
        .realization_identity = UINT64_C(0x67646c2e73746d31),
        .authority_revision = 2u,
        .realization_abi = 2u,
    };

/* This family contains exactly the realization admitted for one immutable
 * source/profile/revision fibre.  No dominance edge is invented against the
 * separately admitted positive-Horn or finite-view calculi. */
static const CettaNikImplementationCapabilityIdV1
    g_gdl_stratified_model_capabilities_v1[] = {
        UINT64_C(0x67646c7300000001), /* exact typed source image */
        UINT64_C(0x67646c7300000002), /* finite typed Herbrand carrier */
        UINT64_C(0x67646c7300000003), /* stratified least support */
        UINT64_C(0x67646c7300000004), /* occurrence-bearing proof graph */
        UINT64_C(0x67646c7300000005), /* constructive lower-stratum absence */
        UINT64_C(0x67646c7300000006), /* native ground proof construction */
        UINT64_C(0x67646c7300000007), /* typed finite episode ingress */
        UINT64_C(0x67646c7300000008), /* revision-current native flow */
    };

static CettaNikImplementationSelectionV1 gdl_stratified_no_selection_v1(void) {
    return (CettaNikImplementationSelectionV1){
        .status = CETTA_NIK_IMPLEMENTATION_SELECTION_STATUS_OK_V1,
        .kind = CETTA_NIK_IMPLEMENTATION_SELECTION_NONE_V1,
        .greatest_index = SIZE_MAX,
    };
}

static CettaNikImplementationSelectionV1 gdl_stratified_select_v1(
    size_t *frontier_index_out) {
    const CettaNikLicensedImplementationV1 implementation = {
        .calculus_identity =
            g_gdl_stratified_model_authority_v1.authority_identity,
        .implementation_identity =
            g_gdl_stratified_model_authority_v1.realization_identity,
        .capabilities = g_gdl_stratified_model_capabilities_v1,
        .capability_count =
            sizeof(g_gdl_stratified_model_capabilities_v1) /
            sizeof(g_gdl_stratified_model_capabilities_v1[0]),
    };
    const CettaNikLicensedImplementationFamilyV1 family = {
        .implementations = &implementation,
        .implementation_count = 1u,
    };
    const CettaNikImplementationCapabilityRequestV1 request = {
        .required_capabilities = g_gdl_stratified_model_capabilities_v1,
        .required_capability_count =
            sizeof(g_gdl_stratified_model_capabilities_v1) /
            sizeof(g_gdl_stratified_model_capabilities_v1[0]),
    };
    return cetta_nik_licensed_implementation_select_v1(
        &family, &request, frontier_index_out, 1u);
}

static bool gdl_stratified_selection_is_native_greatest_v1(
    const CettaNikImplementationSelectionV1 *selection,
    size_t frontier_index) {
    return selection &&
        selection->status == CETTA_NIK_IMPLEMENTATION_SELECTION_STATUS_OK_V1 &&
        selection->kind == CETTA_NIK_IMPLEMENTATION_SELECTION_UNIQUE_GREATEST_V1 &&
        selection->eligible_count == 1u &&
        selection->frontier_count == 1u &&
        selection->greatest_index == 0u && frontier_index == 0u;
}

static void gdl_stratified_sha_length_v1(
    CettaNativeSha256 *sha, size_t length) {
    uint8_t bytes[8];
    uint64_t value = (uint64_t)length;
    for (size_t index = 0u; index < sizeof(bytes); index++)
        bytes[sizeof(bytes) - 1u - index] =
            (uint8_t)(value >> (index * 8u));
    cetta_native_sha256_update(sha, bytes, sizeof(bytes));
}

typedef enum {
    GDL_STRATIFIED_BUILD_OK_V1 = 0,
    GDL_STRATIFIED_BUILD_OUTSIDE_V1,
    GDL_STRATIFIED_BUILD_INCOMPLETE_V1,
    GDL_STRATIFIED_BUILD_FAULT_V1,
} GdlStratifiedBuildV1;

typedef struct {
    char *name;
    char *type_name;
} GdlStratifiedVariableV1;

typedef struct {
    GdlStratifiedVariableV1 *items;
    size_t count;
    size_t capacity;
} GdlStratifiedVariablesV1;

typedef enum {
    GDL_STRATIFIED_TEMPLATE_POSITIVE_V1 = 1,
    GDL_STRATIFIED_TEMPLATE_NEGATIVE_V1,
    GDL_STRATIFIED_TEMPLATE_DISTINCT_V1,
} GdlStratifiedTemplatePremiseKindV1;

typedef struct {
    GdlStratifiedTemplatePremiseKindV1 kind;
    const GdlSourceRawExprV1 *expression;
    const GdlSourceRawExprV1 *left;
    const GdlSourceRawExprV1 *right;
    const char *path;
    const char *operand_path;
    size_t relation_index;
    size_t relation_stratum;
    CettaGdlTypeOfNativeSourceJudgmentV1 source;
    CettaGdlTypeOfNativeSourceJudgmentV1 operand_source;
} GdlStratifiedTemplatePremiseV1;

typedef struct {
    const char *path;
    size_t alternative;
    CettaGdlTypeOfNativeSourceJudgmentV1 source;
} GdlStratifiedTemplateChoiceV1;

typedef struct {
    GdlStratifiedTemplatePremiseV1 *premises;
    size_t premise_count;
    size_t premise_capacity;
    GdlStratifiedTemplateChoiceV1 *choices;
    size_t choice_count;
    size_t choice_capacity;
} GdlStratifiedTemplateBranchV1;

typedef struct {
    GdlStratifiedTemplateBranchV1 *items;
    size_t count;
    size_t capacity;
} GdlStratifiedTemplateBranchesV1;

typedef struct {
    size_t source_form_ordinal;
    size_t source_start_line;
    size_t source_end_line;
    const GdlSourceRawExprV1 *head;
    size_t head_relation;
    size_t head_stratum;
    CettaGdlTypeOfNativeSourceJudgmentV1 source_head;
    GdlStratifiedVariableV1 *variables;
    size_t variable_count;
    GdlStratifiedTemplateBranchV1 branch;
    size_t same_stratum_positive_count;
} GdlStratifiedTemplateV1;

typedef struct {
    const char *path;
    Atom *literal;
    size_t relation_index;
    CettaGdlTypeOfNativeSourceJudgmentV1 source;
} GdlStratifiedGroundPositiveV1;

typedef struct {
    const char *not_path;
    Atom *literal;
    size_t relation_index;
    CettaGdlTypeOfNativeSourceJudgmentV1 source_not;
    CettaGdlTypeOfNativeSourceJudgmentV1 source_operand;
} GdlStratifiedGroundNegativeV1;

typedef struct {
    const char *path;
    Atom *left;
    Atom *right;
    CettaGdlTypeOfNativeSourceJudgmentV1 source;
} GdlStratifiedGroundDistinctV1;

typedef struct {
    CettaGdlStratifiedProofOriginKindV1 origin;
    Atom *episode_occurrence;
    size_t source_form_ordinal;
    size_t source_start_line;
    size_t source_end_line;
    Atom *head;
    size_t head_relation;
    size_t head_stratum;
    CettaGdlTypeOfNativeSourceJudgmentV1 source_head;
    CettaGdlStratifiedSubstitutionViewV1 *substitution;
    size_t substitution_count;
    CettaGdlStratifiedBranchChoiceViewV1 *branch_choices;
    size_t branch_choice_count;
    GdlStratifiedGroundPositiveV1 *positive;
    size_t positive_count;
    GdlStratifiedGroundNegativeV1 *negative;
    size_t negative_count;
    GdlStratifiedGroundDistinctV1 *distinct;
    size_t distinct_count;
    bool active;
} GdlStratifiedGroundRuleV1;

typedef struct {
    Atom *literal;
    size_t relation_index;
    size_t stratum;
    size_t *proof_edge_indices;
    size_t proof_edge_count;
    size_t proof_edge_capacity;
} GdlStratifiedSupportV1;

typedef struct {
    size_t *support_indices;
    size_t count;
    size_t capacity;
} GdlStratifiedRelationSupportsV1;

typedef struct {
    size_t template_index;
    size_t *assignment;
    size_t assignment_count;
} GdlStratifiedGroundingV1;

typedef struct {
    CettaGdlStratifiedProofEdgeViewV1 view;
} GdlStratifiedProofEdgeV1;

typedef struct {
    size_t *rows;
    size_t count;
    size_t capacity;
    size_t width;
} GdlStratifiedAssignmentsV1;

struct CettaGdlStratifiedModelV1 {
    Arena arena;
    bool owns_basis;
    Arena basis_arena;
    GdlSourceRawFormsV1 owned_forms;
    CettaGdlTypeOfNativeV1 *owned_typing;
    CettaGdlFiniteHerbrandV1 *owned_herbrand;
    CettaGdlStratificationV1 *owned_stratification;
    const CettaGdlTypeOfNativeV1 *typing;
    const CettaGdlFiniteHerbrandV1 *herbrand;
    const CettaGdlStratificationV1 *stratification;
    CettaGdlStratifiedModelLimitsV1 limits;
    GdlStratifiedTemplateV1 *templates;
    size_t template_count;
    size_t template_capacity;
    GdlStratifiedGroundingV1 *groundings;
    size_t grounding_count;
    size_t grounding_capacity;
    size_t *grounding_slots;
    size_t grounding_slot_capacity;
    size_t grounding_slot_used;
    GdlStratifiedSupportV1 *supports;
    size_t support_count;
    size_t support_capacity;
    size_t *support_slots;
    size_t support_slot_capacity;
    size_t support_slot_used;
    GdlStratifiedRelationSupportsV1 *relation_supports;
    size_t relation_count;
    GdlStratifiedProofEdgeV1 *edges;
    size_t edge_count;
    size_t edge_capacity;
    size_t maximum_stratum;
    size_t *completed_relation_support_counts;
    CettaGdlStratifiedModelStatsV1 stats;
    CettaNikDirectAuthorityTokenV1 token;
    CettaNikImplementationSelectionV1 selection;
    uint64_t selected_realization_identity;
    char source_sha256[65];
    char profile_sha256[65];
    char calculus_input_sha256[65];
    char revision[81];
    char *target_name;
    size_t target_arity;
    size_t target_source_forms;
    size_t target_selected_forms;
    size_t target_reachable_relations;
    size_t target_external_relations;
    bool token_valid;
};

struct CettaGdlStratifiedEpisodeV1 {
    Arena arena;
    CettaGdlStratifiedModelV1 *source_model;
    CettaNikDirectAuthorityTokenV1 source_token;
    CettaNikDirectAuthorityTokenV1 token;
    char digest[65];
    char *revision;
    Atom *identity;
    CettaGdlStratifiedModelV1 *model;
    CettaGdlStratifiedEpisodeStatsV1 stats;
    bool token_valid;
};

static bool gdl_stratified_episode_digest_v1(
    const CettaGdlStratifiedModelV1 *source_model,
    Arena *scratch,
    Atom *episode_identity,
    Atom *const *facts,
    size_t fact_count,
    char digest_out[65]) {
    static const char domain[] =
        "cetta.gdl-stratified-model.typed-episode.v1";
    if (!source_model || !scratch || !episode_identity || !digest_out ||
        (fact_count != 0u && !facts))
        return false;
    CettaNativeSha256 sha;
    cetta_native_sha256_init(&sha);
    cetta_native_sha256_update(
        &sha, (const uint8_t *)domain, strlen(domain) + 1u);
    size_t source_length = strlen(source_model->calculus_input_sha256);
    gdl_stratified_sha_length_v1(&sha, source_length);
    cetta_native_sha256_update(
        &sha, (const uint8_t *)source_model->calculus_input_sha256,
        source_length);
    char *identity_text = atom_to_parseable_string(
        scratch, episode_identity);
    if (!identity_text || !*identity_text)
        return false;
    size_t identity_length = strlen(identity_text);
    gdl_stratified_sha_length_v1(&sha, identity_length);
    cetta_native_sha256_update(
        &sha, (const uint8_t *)identity_text, identity_length);
    gdl_stratified_sha_length_v1(&sha, fact_count);
    for (size_t index = 0u; index < fact_count; index++) {
        char *fact_text = facts[index]
            ? atom_to_parseable_string(scratch, facts[index])
            : NULL;
        if (!fact_text || !*fact_text)
            return false;
        size_t fact_length = strlen(fact_text);
        gdl_stratified_sha_length_v1(&sha, fact_length);
        cetta_native_sha256_update(
            &sha, (const uint8_t *)fact_text, fact_length);
    }
    cetta_native_sha256_finish_hex(&sha, digest_out);
    return true;
}

typedef struct {
    CettaGdlStratifiedModelV1 *model;
    GdlStratifiedBuildV1 status;
} GdlStratifiedBuilderV1;

static bool gdl_stratified_reserve_v1(
    void **items, size_t *capacity, size_t needed, size_t item_size) {
    if (!items || !capacity || item_size == 0u)
        return false;
    if (*capacity >= needed)
        return true;
    size_t next = *capacity ? *capacity : 16u;
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

static bool gdl_stratified_raw_head_v1(
    const GdlSourceRawExprV1 *raw, const char *head) {
    return raw && !raw->token && raw->count > 0u && raw->items[0] &&
        raw->items[0]->token && strcmp(raw->items[0]->token, head) == 0;
}

static bool gdl_stratified_logical_head_v1(const char *head) {
    return head &&
        (strcmp(head, "and") == 0 || strcmp(head, "or") == 0 ||
         strcmp(head, "not") == 0 || strcmp(head, "distinct") == 0);
}

static char *gdl_stratified_path_child_v1(
    Arena *arena, const char *path, size_t child) {
    if (!arena || !path || !*path || child == 0u)
        return NULL;
    int length = strcmp(path, "root") == 0
        ? snprintf(NULL, 0, "%zu", child)
        : snprintf(NULL, 0, "%s.%zu", path, child);
    if (length < 0)
        return NULL;
    char *result = arena_alloc(arena, (size_t)length + 1u);
    int written = strcmp(path, "root") == 0
        ? snprintf(result, (size_t)length + 1u, "%zu", child)
        : snprintf(result, (size_t)length + 1u, "%s.%zu", path, child);
    return written == length ? result : NULL;
}

static Atom *gdl_stratified_token_v1(Arena *arena, const char *token) {
    if (!arena || !token || !*token)
        return NULL;
    char *end = NULL;
    errno = 0;
    intmax_t value = strtoimax(token, &end, 10);
    if (errno == 0 && end && *end == '\0' && value >= INT64_MIN &&
        value <= INT64_MAX)
        return atom_int(arena, (int64_t)value);
    return atom_symbol(arena, token);
}

static bool gdl_stratified_relation_signature_v1(
    const GdlSourceRawExprV1 *raw,
    const char **name_out,
    size_t *arity_out) {
    if (!raw || !name_out || !arity_out)
        return false;
    if (raw->token) {
        if (!*raw->token || raw->token[0] == '?')
            return false;
        *name_out = raw->token;
        *arity_out = 0u;
        return true;
    }
    if (raw->count == 0u || !raw->items[0] ||
        !raw->items[0]->token || !*raw->items[0]->token ||
        raw->items[0]->token[0] == '?' ||
        gdl_stratified_logical_head_v1(raw->items[0]->token))
        return false;
    *name_out = raw->items[0]->token;
    *arity_out = raw->count - 1u;
    return true;
}

static bool gdl_stratified_relation_index_v1(
    const CettaGdlStratificationV1 *stratification,
    const GdlSourceRawExprV1 *raw,
    size_t *index_out,
    size_t *stratum_out) {
    const char *name = NULL;
    size_t arity = 0u;
    if (!stratification ||
        !gdl_stratified_relation_signature_v1(raw, &name, &arity))
        return false;
    size_t count =
        cetta_gdl_stratification_relation_count_v1(stratification);
    for (size_t index = 0u; index < count; index++) {
        CettaGdlStratifiedRelationViewV1 view = {0};
        if (!cetta_gdl_stratification_relation_view_v1(
                stratification, index, &view))
            return false;
        if (view.arity == arity && strcmp(view.name, name) == 0) {
            if (index_out)
                *index_out = index;
            if (stratum_out)
                *stratum_out = view.stratum;
            return true;
        }
    }
    return false;
}

static bool gdl_stratified_source_judgment_v1(
    GdlStratifiedBuilderV1 *builder,
    size_t form_ordinal,
    const char *path,
    bool require_literal,
    CettaGdlTypeOfNativeSourceJudgmentV1 *view_out) {
    if (!builder || builder->status != GDL_STRATIFIED_BUILD_OK_V1 ||
        !view_out ||
        !cetta_gdl_type_of_native_source_judgment_v1(
            builder->model->typing, form_ordinal, path, view_out) ||
        !view_out->type_name || view_out->type_proof_count == 0u ||
        (require_literal &&
         (strcmp(view_out->type_name, "bool") != 0 ||
          view_out->literal_proof_count == 0u))) {
        if (builder)
            builder->status = GDL_STRATIFIED_BUILD_OUTSIDE_V1;
        return false;
    }
    return true;
}

static size_t gdl_stratified_variable_index_v1(
    const GdlStratifiedVariablesV1 *variables, const char *name) {
    if (!variables || !name)
        return SIZE_MAX;
    for (size_t index = 0u; index < variables->count; index++)
        if (strcmp(variables->items[index].name, name) == 0)
            return index;
    return SIZE_MAX;
}

static bool gdl_stratified_collect_variables_v1(
    GdlStratifiedBuilderV1 *builder,
    size_t form_ordinal,
    const GdlSourceRawExprV1 *raw,
    const char *path,
    size_t depth,
    GdlStratifiedVariablesV1 *variables) {
    if (!builder || builder->status != GDL_STRATIFIED_BUILD_OK_V1 ||
        !raw || !path || !variables)
        return false;
    if (depth > builder->model->limits.max_logical_depth) {
        builder->status = GDL_STRATIFIED_BUILD_INCOMPLETE_V1;
        return false;
    }
    if (raw->token) {
        if (raw->token[0] != '?')
            return true;
        CettaGdlTypeOfNativeSourceJudgmentV1 source = {0};
        if (!gdl_stratified_source_judgment_v1(
                builder, form_ordinal, path, false, &source))
            return false;
        size_t found =
            gdl_stratified_variable_index_v1(variables, raw->token);
        if (found != SIZE_MAX) {
            if (strcmp(variables->items[found].type_name,
                       source.type_name) != 0)
                builder->status = GDL_STRATIFIED_BUILD_OUTSIDE_V1;
            return builder->status == GDL_STRATIFIED_BUILD_OK_V1;
        }
        if (variables->count ==
                builder->model->limits.max_variables_per_form ||
            !gdl_stratified_reserve_v1(
                (void **)&variables->items, &variables->capacity,
                variables->count + 1u, sizeof(*variables->items))) {
            builder->status = GDL_STRATIFIED_BUILD_INCOMPLETE_V1;
            return false;
        }
        GdlStratifiedVariableV1 *variable =
            &variables->items[variables->count++];
        memset(variable, 0, sizeof(*variable));
        variable->name = arena_strdup(
            &builder->model->arena, raw->token);
        variable->type_name = arena_strdup(
            &builder->model->arena, source.type_name);
        if (!variable->name || !variable->type_name) {
            builder->status = GDL_STRATIFIED_BUILD_FAULT_V1;
            return false;
        }
        return true;
    }
    if (raw->count == 0u || !raw->items[0] ||
        !raw->items[0]->token || raw->items[0]->token[0] == '?') {
        builder->status = GDL_STRATIFIED_BUILD_OUTSIDE_V1;
        return false;
    }
    for (size_t child = 1u; child < raw->count; child++) {
        char *child_path = gdl_stratified_path_child_v1(
            &builder->model->arena, path, child);
        if (!child_path || !gdl_stratified_collect_variables_v1(
                builder, form_ordinal, raw->items[child], child_path,
                depth + 1u, variables))
            return false;
    }
    return true;
}

static void gdl_stratified_variables_free_v1(
    GdlStratifiedVariablesV1 *variables) {
    if (!variables)
        return;
    free(variables->items);
    memset(variables, 0, sizeof(*variables));
}

static bool gdl_stratified_raw_contains_variable_v1(
    const GdlSourceRawExprV1 *raw, const char *name, size_t depth,
    size_t max_depth) {
    if (!raw || !name || depth > max_depth)
        return false;
    if (raw->token)
        return strcmp(raw->token, name) == 0;
    for (size_t index = 1u; index < raw->count; index++)
        if (gdl_stratified_raw_contains_variable_v1(
                raw->items[index], name, depth + 1u, max_depth))
            return true;
    return false;
}

static bool gdl_stratified_template_push_v1(
    GdlStratifiedBuilderV1 *builder,
    const GdlSourceRawFormV1 *source_form,
    size_t form_index,
    const GdlSourceRawExprV1 *head,
    size_t head_relation,
    size_t head_stratum,
    CettaGdlTypeOfNativeSourceJudgmentV1 source_head,
    const GdlStratifiedVariablesV1 *variables,
    GdlStratifiedTemplateBranchV1 *branch) {
    if (!builder || builder->status != GDL_STRATIFIED_BUILD_OK_V1 ||
        !source_form || !head || !variables || !branch)
        return false;
    for (size_t variable_index = 0u;
         variable_index < variables->count; variable_index++) {
        bool bound = false;
        for (size_t premise_index = 0u;
             premise_index < branch->premise_count; premise_index++) {
            const GdlStratifiedTemplatePremiseV1 *premise =
                &branch->premises[premise_index];
            if (premise->kind == GDL_STRATIFIED_TEMPLATE_POSITIVE_V1 &&
                gdl_stratified_raw_contains_variable_v1(
                    premise->expression,
                    variables->items[variable_index].name, 0u,
                    builder->model->limits.max_logical_depth)) {
                bound = true;
                break;
            }
        }
        if (!bound) {
            builder->status = GDL_STRATIFIED_BUILD_OUTSIDE_V1;
            return false;
        }
    }
    if (!gdl_stratified_reserve_v1(
            (void **)&builder->model->templates,
            &builder->model->template_capacity,
            builder->model->template_count + 1u,
            sizeof(*builder->model->templates))) {
        builder->status = GDL_STRATIFIED_BUILD_INCOMPLETE_V1;
        return false;
    }
    GdlStratifiedTemplateV1 *target =
        &builder->model->templates[builder->model->template_count++];
    memset(target, 0, sizeof(*target));
    target->source_form_ordinal = form_index + 1u;
    target->source_start_line = source_form->start_line;
    target->source_end_line = source_form->end_line;
    target->head = head;
    target->head_relation = head_relation;
    target->head_stratum = head_stratum;
    target->source_head = source_head;
    target->variable_count = variables->count;
    if (variables->count != 0u) {
        target->variables = arena_alloc(
            &builder->model->arena,
            variables->count * sizeof(*target->variables));
        memcpy(target->variables, variables->items,
               variables->count * sizeof(*target->variables));
    }
    target->branch = *branch;
    memset(branch, 0, sizeof(*branch));
    for (size_t index = 0u;
         index < target->branch.premise_count; index++) {
        const GdlStratifiedTemplatePremiseV1 *premise =
            &target->branch.premises[index];
        if (premise->kind == GDL_STRATIFIED_TEMPLATE_POSITIVE_V1) {
            if (premise->relation_stratum > head_stratum) {
                builder->status = GDL_STRATIFIED_BUILD_FAULT_V1;
                return false;
            }
            target->same_stratum_positive_count +=
                premise->relation_stratum == head_stratum;
        } else if (premise->kind ==
                       GDL_STRATIFIED_TEMPLATE_NEGATIVE_V1 &&
                   premise->relation_stratum >= head_stratum) {
            builder->status = GDL_STRATIFIED_BUILD_FAULT_V1;
            return false;
        }
    }
    return true;
}

static void gdl_stratified_template_branch_free_v1(
    GdlStratifiedTemplateBranchV1 *branch) {
    if (!branch)
        return;
    free(branch->premises);
    free(branch->choices);
    memset(branch, 0, sizeof(*branch));
}

static void gdl_stratified_template_branches_free_v1(
    GdlStratifiedTemplateBranchesV1 *branches) {
    if (!branches)
        return;
    for (size_t index = 0u; index < branches->count; index++)
        gdl_stratified_template_branch_free_v1(&branches->items[index]);
    free(branches->items);
    memset(branches, 0, sizeof(*branches));
}

static bool gdl_stratified_template_branch_clone_v1(
    const GdlStratifiedTemplateBranchV1 *source,
    GdlStratifiedTemplateBranchV1 *target) {
    if (!source || !target)
        return false;
    memset(target, 0, sizeof(*target));
    if (source->premise_count != 0u) {
        target->premises = cetta_malloc(
            source->premise_count * sizeof(*target->premises));
        memcpy(target->premises, source->premises,
               source->premise_count * sizeof(*target->premises));
        target->premise_count = source->premise_count;
        target->premise_capacity = source->premise_count;
    }
    if (source->choice_count != 0u) {
        target->choices = cetta_malloc(
            source->choice_count * sizeof(*target->choices));
        memcpy(target->choices, source->choices,
               source->choice_count * sizeof(*target->choices));
        target->choice_count = source->choice_count;
        target->choice_capacity = source->choice_count;
    }
    return true;
}

static bool gdl_stratified_template_branch_add_premise_v1(
    GdlStratifiedTemplateBranchV1 *branch,
    GdlStratifiedTemplatePremiseV1 premise) {
    if (!branch || !gdl_stratified_reserve_v1(
            (void **)&branch->premises, &branch->premise_capacity,
            branch->premise_count + 1u, sizeof(*branch->premises)))
        return false;
    branch->premises[branch->premise_count++] = premise;
    return true;
}

static bool gdl_stratified_template_branch_add_choice_v1(
    GdlStratifiedTemplateBranchV1 *branch,
    GdlStratifiedTemplateChoiceV1 choice) {
    if (!branch || !gdl_stratified_reserve_v1(
            (void **)&branch->choices, &branch->choice_capacity,
            branch->choice_count + 1u, sizeof(*branch->choices)))
        return false;
    branch->choices[branch->choice_count++] = choice;
    return true;
}

static bool gdl_stratified_template_branches_push_move_v1(
    GdlStratifiedTemplateBranchesV1 *branches,
    GdlStratifiedTemplateBranchV1 *branch) {
    if (!branches || !branch || !gdl_stratified_reserve_v1(
            (void **)&branches->items, &branches->capacity,
            branches->count + 1u, sizeof(*branches->items)))
        return false;
    branches->items[branches->count++] = *branch;
    memset(branch, 0, sizeof(*branch));
    return true;
}

static bool gdl_stratified_expand_expression_v1(
    GdlStratifiedBuilderV1 *builder,
    size_t form_ordinal,
    const GdlSourceRawExprV1 *raw,
    const char *path,
    size_t depth,
    GdlStratifiedTemplateBranchesV1 *branches) {
    if (!builder || builder->status != GDL_STRATIFIED_BUILD_OK_V1 ||
        !raw || !path || !branches)
        return false;
    if (depth > builder->model->limits.max_logical_depth) {
        builder->status = GDL_STRATIFIED_BUILD_INCOMPLETE_V1;
        return false;
    }
    if (gdl_stratified_raw_head_v1(raw, "and")) {
        if (raw->count < 2u) {
            builder->status = GDL_STRATIFIED_BUILD_OUTSIDE_V1;
            return false;
        }
        for (size_t child = 1u; child < raw->count; child++) {
            char *child_path = gdl_stratified_path_child_v1(
                &builder->model->arena, path, child);
            if (!child_path || !gdl_stratified_expand_expression_v1(
                    builder, form_ordinal, raw->items[child], child_path,
                    depth + 1u, branches))
                return false;
        }
        return true;
    }
    if (gdl_stratified_raw_head_v1(raw, "or")) {
        if (raw->count < 3u) {
            builder->status = GDL_STRATIFIED_BUILD_OUTSIDE_V1;
            return false;
        }
        CettaGdlTypeOfNativeSourceJudgmentV1 source = {0};
        if (!gdl_stratified_source_judgment_v1(
                builder, form_ordinal, path, true, &source))
            return false;
        GdlStratifiedTemplateBranchesV1 output = {0};
        for (size_t branch_index = 0u;
             branch_index < branches->count; branch_index++) {
            for (size_t child = 1u; child < raw->count; child++) {
                if (builder->model->stats.branch_expansions ==
                    builder->model->limits.max_branch_expansions) {
                    builder->status = GDL_STRATIFIED_BUILD_INCOMPLETE_V1;
                    gdl_stratified_template_branches_free_v1(&output);
                    return false;
                }
                builder->model->stats.branch_expansions++;
                GdlStratifiedTemplateBranchV1 clone = {0};
                if (!gdl_stratified_template_branch_clone_v1(
                        &branches->items[branch_index], &clone) ||
                    !gdl_stratified_template_branch_add_choice_v1(
                        &clone,
                        (GdlStratifiedTemplateChoiceV1){
                            .path = path,
                            .alternative = child,
                            .source = source,
                        })) {
                    gdl_stratified_template_branch_free_v1(&clone);
                    gdl_stratified_template_branches_free_v1(&output);
                    builder->status = GDL_STRATIFIED_BUILD_INCOMPLETE_V1;
                    return false;
                }
                GdlStratifiedTemplateBranchesV1 selected = {0};
                if (!gdl_stratified_template_branches_push_move_v1(
                        &selected, &clone)) {
                    gdl_stratified_template_branch_free_v1(&clone);
                    gdl_stratified_template_branches_free_v1(&selected);
                    gdl_stratified_template_branches_free_v1(&output);
                    builder->status = GDL_STRATIFIED_BUILD_INCOMPLETE_V1;
                    return false;
                }
                char *child_path = gdl_stratified_path_child_v1(
                    &builder->model->arena, path, child);
                if (!child_path || !gdl_stratified_expand_expression_v1(
                        builder, form_ordinal, raw->items[child], child_path,
                        depth + 1u, &selected)) {
                    gdl_stratified_template_branches_free_v1(&selected);
                    gdl_stratified_template_branches_free_v1(&output);
                    return false;
                }
                for (size_t selected_index = 0u;
                     selected_index < selected.count; selected_index++)
                    if (!gdl_stratified_template_branches_push_move_v1(
                            &output, &selected.items[selected_index])) {
                        gdl_stratified_template_branches_free_v1(&selected);
                        gdl_stratified_template_branches_free_v1(&output);
                        builder->status =
                            GDL_STRATIFIED_BUILD_INCOMPLETE_V1;
                        return false;
                    }
                free(selected.items);
            }
        }
        gdl_stratified_template_branches_free_v1(branches);
        *branches = output;
        return true;
    }

    GdlStratifiedTemplatePremiseV1 premise = {0};
    premise.path = path;
    if (gdl_stratified_raw_head_v1(raw, "not")) {
        const GdlSourceRawExprV1 *operand = raw->count == 2u
            ? raw->items[1] : NULL;
        if (!operand || (!operand->token && operand->count > 0u &&
                operand->items[0] && operand->items[0]->token &&
                gdl_stratified_logical_head_v1(
                    operand->items[0]->token))) {
            builder->status = GDL_STRATIFIED_BUILD_OUTSIDE_V1;
            return false;
        }
        premise.kind = GDL_STRATIFIED_TEMPLATE_NEGATIVE_V1;
        premise.expression = operand;
        premise.operand_path = gdl_stratified_path_child_v1(
            &builder->model->arena, path, 1u);
        if (!premise.operand_path ||
            !gdl_stratified_source_judgment_v1(
                builder, form_ordinal, path, true, &premise.source) ||
            !gdl_stratified_source_judgment_v1(
                builder, form_ordinal, premise.operand_path, true,
                &premise.operand_source))
            return false;
        if (!gdl_stratified_relation_index_v1(
                builder->model->stratification, operand,
                &premise.relation_index, &premise.relation_stratum)) {
            builder->status = GDL_STRATIFIED_BUILD_OUTSIDE_V1;
            return false;
        }
    } else if (gdl_stratified_raw_head_v1(raw, "distinct")) {
        if (raw->count != 3u ||
            !gdl_stratified_source_judgment_v1(
                builder, form_ordinal, path, true, &premise.source)) {
            builder->status = GDL_STRATIFIED_BUILD_OUTSIDE_V1;
            return false;
        }
        premise.kind = GDL_STRATIFIED_TEMPLATE_DISTINCT_V1;
        premise.left = raw->items[1];
        premise.right = raw->items[2];
    } else {
        premise.kind = GDL_STRATIFIED_TEMPLATE_POSITIVE_V1;
        premise.expression = raw;
        if (!gdl_stratified_source_judgment_v1(
                builder, form_ordinal, path, true, &premise.source))
            return false;
        if (!gdl_stratified_relation_index_v1(
                builder->model->stratification, raw,
                &premise.relation_index, &premise.relation_stratum)) {
            builder->status = GDL_STRATIFIED_BUILD_OUTSIDE_V1;
            return false;
        }
    }
    for (size_t index = 0u; index < branches->count; index++)
        if (!gdl_stratified_template_branch_add_premise_v1(
                &branches->items[index], premise)) {
            builder->status = GDL_STRATIFIED_BUILD_INCOMPLETE_V1;
            return false;
        }
    return true;
}

static Atom *gdl_stratified_ground_expression_v1(
    GdlStratifiedBuilderV1 *builder,
    Arena *target,
    const GdlSourceRawExprV1 *raw,
    const GdlStratifiedVariablesV1 *variables,
    const size_t *assignment,
    size_t depth) {
    if (!builder || builder->status != GDL_STRATIFIED_BUILD_OK_V1 ||
        !target || !raw || !variables ||
        (variables->count != 0u && !assignment))
        return NULL;
    if (depth > builder->model->limits.max_logical_depth) {
        builder->status = GDL_STRATIFIED_BUILD_INCOMPLETE_V1;
        return NULL;
    }
    if (raw->token) {
        if (raw->token[0] != '?')
            return gdl_stratified_token_v1(
                target, raw->token);
        size_t variable =
            gdl_stratified_variable_index_v1(variables, raw->token);
        CettaGdlFiniteHerbrandTermViewV1 term = {0};
        if (variable == SIZE_MAX ||
            !cetta_gdl_finite_herbrand_term_view_v1(
                builder->model->herbrand, assignment[variable], &term) ||
            !term.term) {
            builder->status = GDL_STRATIFIED_BUILD_FAULT_V1;
            return NULL;
        }
        return term.term;
    }
    if (raw->count == 0u || !raw->items[0] ||
        !raw->items[0]->token || raw->items[0]->token[0] == '?') {
        builder->status = GDL_STRATIFIED_BUILD_OUTSIDE_V1;
        return NULL;
    }
    Atom **items = arena_alloc(
        target, raw->count * sizeof(*items));
    items[0] = atom_symbol(
        target, raw->items[0]->token);
    for (size_t child = 1u; child < raw->count; child++) {
        items[child] = gdl_stratified_ground_expression_v1(
            builder, target, raw->items[child], variables, assignment,
            depth + 1u);
        if (!items[child])
            return NULL;
    }
    return atom_expr(
        target, items, (CettaExprLen)raw->count);
}

static void gdl_stratified_assignments_free_v1(
    GdlStratifiedAssignmentsV1 *assignments) {
    if (!assignments)
        return;
    free(assignments->rows);
    memset(assignments, 0, sizeof(*assignments));
}

static bool gdl_stratified_assignments_initial_v1(
    GdlStratifiedBuilderV1 *builder,
    size_t width,
    const size_t *initial,
    GdlStratifiedAssignmentsV1 *assignments) {
    if (!builder || builder->status != GDL_STRATIFIED_BUILD_OK_V1 ||
        !assignments)
        return false;
    if (width != 0u && width > SIZE_MAX / sizeof(size_t)) {
        builder->status = GDL_STRATIFIED_BUILD_INCOMPLETE_V1;
        return false;
    }
    memset(assignments, 0, sizeof(*assignments));
    assignments->width = width;
    assignments->count = 1u;
    assignments->capacity = 1u;
    if (width != 0u) {
        assignments->rows = cetta_malloc(width * sizeof(*assignments->rows));
        if (initial)
            memcpy(assignments->rows, initial,
                   width * sizeof(*assignments->rows));
        else
            for (size_t index = 0u; index < width; index++)
                assignments->rows[index] = SIZE_MAX;
    }
    return true;
}

static bool gdl_stratified_assignments_push_v1(
    GdlStratifiedBuilderV1 *builder,
    GdlStratifiedAssignmentsV1 *assignments,
    const size_t *row) {
    if (!builder || builder->status != GDL_STRATIFIED_BUILD_OK_V1 ||
        !assignments || (assignments->width != 0u && !row))
        return false;
    if (builder->model->stats.assignments >=
        builder->model->limits.max_assignments) {
        builder->status = GDL_STRATIFIED_BUILD_INCOMPLETE_V1;
        return false;
    }
    if (assignments->width != 0u) {
        if (assignments->width > SIZE_MAX / sizeof(size_t) ||
            !gdl_stratified_reserve_v1(
                (void **)&assignments->rows, &assignments->capacity,
                assignments->count + 1u,
                assignments->width * sizeof(*assignments->rows))) {
            builder->status = GDL_STRATIFIED_BUILD_INCOMPLETE_V1;
            return false;
        }
        memcpy(
            assignments->rows + assignments->count * assignments->width,
            row, assignments->width * sizeof(*row));
    } else if (assignments->count != 0u) {
        builder->status = GDL_STRATIFIED_BUILD_FAULT_V1;
        return false;
    }
    assignments->count++;
    builder->model->stats.assignments++;
    return true;
}

static bool gdl_stratified_token_matches_v1(
    const char *token, Atom *atom) {
    if (!token || !*token || !atom)
        return false;
    char *end = NULL;
    errno = 0;
    intmax_t value = strtoimax(token, &end, 10);
    if (errno == 0 && end && *end == '\0' && value >= INT64_MIN &&
        value <= INT64_MAX)
        return atom->kind == ATOM_GROUNDED &&
            atom->ground.gkind == GV_INT &&
            atom->ground.ival == (int64_t)value;
    return atom_is_symbol(atom, token);
}

static bool gdl_stratified_match_expression_v1(
    GdlStratifiedBuilderV1 *builder,
    const GdlSourceRawExprV1 *raw,
    Atom *value,
    const GdlStratifiedTemplateV1 *template,
    GdlStratifiedAssignmentsV1 *assignments,
    size_t depth) {
    if (!builder || builder->status != GDL_STRATIFIED_BUILD_OK_V1 ||
        !raw || !value || !template || !assignments ||
        assignments->width != template->variable_count)
        return false;
    if (depth > builder->model->limits.max_logical_depth) {
        builder->status = GDL_STRATIFIED_BUILD_INCOMPLETE_V1;
        return false;
    }
    if (raw->token) {
        if (raw->token[0] != '?') {
            if (!gdl_stratified_token_matches_v1(raw->token, value))
                assignments->count = 0u;
            return true;
        }
        GdlStratifiedVariablesV1 variables = {
            .items = template->variables,
            .count = template->variable_count,
            .capacity = template->variable_count,
        };
        size_t variable = gdl_stratified_variable_index_v1(
            &variables, raw->token);
        if (variable == SIZE_MAX) {
            builder->status = GDL_STRATIFIED_BUILD_FAULT_V1;
            return false;
        }
        GdlStratifiedAssignmentsV1 output = {
            .width = assignments->width,
        };
        for (size_t row_index = 0u;
             row_index < assignments->count; row_index++) {
            size_t *row = assignments->rows +
                row_index * assignments->width;
            if (row[variable] != SIZE_MAX) {
                CettaGdlFiniteHerbrandTermViewV1 term = {0};
                if (!cetta_gdl_finite_herbrand_term_view_v1(
                        builder->model->herbrand,
                        row[variable], &term)) {
                    gdl_stratified_assignments_free_v1(&output);
                    builder->status = GDL_STRATIFIED_BUILD_FAULT_V1;
                    return false;
                }
                if (atom_eq(term.term, value) &&
                    !gdl_stratified_assignments_push_v1(
                        builder, &output, row)) {
                    gdl_stratified_assignments_free_v1(&output);
                    return false;
                }
                continue;
            }
            size_t match_count =
                cetta_gdl_finite_herbrand_matching_terms_v1(
                    builder->model->herbrand, value,
                    template->variables[variable].type_name, NULL, 0u);
            if (match_count > SIZE_MAX / sizeof(size_t)) {
                gdl_stratified_assignments_free_v1(&output);
                builder->status = GDL_STRATIFIED_BUILD_INCOMPLETE_V1;
                return false;
            }
            size_t *matches = match_count
                ? cetta_malloc(match_count * sizeof(*matches)) : NULL;
            if (match_count != 0u &&
                cetta_gdl_finite_herbrand_matching_terms_v1(
                    builder->model->herbrand, value,
                    template->variables[variable].type_name,
                    matches, match_count) != match_count) {
                free(matches);
                gdl_stratified_assignments_free_v1(&output);
                builder->status = GDL_STRATIFIED_BUILD_FAULT_V1;
                return false;
            }
            for (size_t match = 0u; match < match_count; match++) {
                size_t previous = row[variable];
                row[variable] = matches[match];
                bool pushed = gdl_stratified_assignments_push_v1(
                    builder, &output, row);
                row[variable] = previous;
                if (!pushed) {
                    free(matches);
                    gdl_stratified_assignments_free_v1(&output);
                    return false;
                }
            }
            free(matches);
        }
        free(assignments->rows);
        *assignments = output;
        return true;
    }
    if (value->kind != ATOM_EXPR || raw->count == 0u ||
        value->expr.len != (CettaExprLen)raw->count ||
        !raw->items[0] || !raw->items[0]->token ||
        !gdl_stratified_token_matches_v1(
            raw->items[0]->token, value->expr.elems[0])) {
        assignments->count = 0u;
        return true;
    }
    for (size_t child = 1u;
         child < raw->count && assignments->count != 0u; child++)
        if (!gdl_stratified_match_expression_v1(
                builder, raw->items[child], value->expr.elems[child],
                template, assignments, depth + 1u))
            return false;
    return true;
}

static bool gdl_stratified_ground_rule_v1(
    GdlStratifiedBuilderV1 *builder,
    const GdlStratifiedTemplateV1 *template,
    const size_t *assignment,
    GdlStratifiedGroundRuleV1 *rule,
    bool *allowed_out) {
    if (allowed_out)
        *allowed_out = false;
    if (!builder || builder->status != GDL_STRATIFIED_BUILD_OK_V1 ||
        !template || !rule || !allowed_out ||
        (template->variable_count != 0u && !assignment))
        return false;
    GdlStratifiedVariablesV1 variables = {
        .items = template->variables,
        .count = template->variable_count,
        .capacity = template->variable_count,
    };
    const GdlStratifiedTemplateBranchV1 *branch = &template->branch;

    size_t distinct_count = 0u;
    Arena distinct_scratch;
    bool distinct_scratch_initialized = false;
    for (size_t premise_index = 0u;
         premise_index < branch->premise_count; premise_index++)
        distinct_count += branch->premises[premise_index].kind ==
            GDL_STRATIFIED_TEMPLATE_DISTINCT_V1;
    if (distinct_count != 0u) {
        arena_init(&distinct_scratch);
        distinct_scratch_initialized = true;
    }
    for (size_t premise_index = 0u;
         premise_index < branch->premise_count; premise_index++) {
        const GdlStratifiedTemplatePremiseV1 *premise =
            &branch->premises[premise_index];
        if (premise->kind != GDL_STRATIFIED_TEMPLATE_DISTINCT_V1)
            continue;
        builder->model->stats.distinct_checks++;
        Atom *left = gdl_stratified_ground_expression_v1(
            builder, &distinct_scratch, premise->left,
            &variables, assignment, 0u);
        Atom *right = gdl_stratified_ground_expression_v1(
            builder, &distinct_scratch, premise->right,
            &variables, assignment, 0u);
        if (!left || !right) {
            arena_free(&distinct_scratch);
            return false;
        }
        if (atom_eq(left, right)) {
            arena_free(&distinct_scratch);
            return true;
        }
    }
    if (distinct_scratch_initialized)
        arena_free(&distinct_scratch);

    GdlStratifiedGroundDistinctV1 *distinct = distinct_count
        ? arena_alloc(
            &builder->model->arena, distinct_count * sizeof(*distinct))
        : NULL;
    size_t distinct_index = 0u;
    for (size_t premise_index = 0u;
         premise_index < branch->premise_count; premise_index++) {
        const GdlStratifiedTemplatePremiseV1 *premise =
            &branch->premises[premise_index];
        if (premise->kind != GDL_STRATIFIED_TEMPLATE_DISTINCT_V1)
            continue;
        Atom *left = gdl_stratified_ground_expression_v1(
            builder, &builder->model->arena, premise->left,
            &variables, assignment, 0u);
        Atom *right = gdl_stratified_ground_expression_v1(
            builder, &builder->model->arena, premise->right,
            &variables, assignment, 0u);
        if (!left || !right)
            return false;
        distinct[distinct_index++] = (GdlStratifiedGroundDistinctV1){
            .path = premise->path,
            .left = left,
            .right = right,
            .source = premise->source,
        };
    }

    memset(rule, 0, sizeof(*rule));
    rule->origin = CETTA_GDL_STRATIFIED_PROOF_AUTHORED_SOURCE_V1;
    rule->source_form_ordinal = template->source_form_ordinal;
    rule->source_start_line = template->source_start_line;
    rule->source_end_line = template->source_end_line;
    rule->head = gdl_stratified_ground_expression_v1(
        builder, &builder->model->arena,
        template->head, &variables, assignment, 0u);
    rule->head_relation = template->head_relation;
    rule->head_stratum = template->head_stratum;
    rule->source_head = template->source_head;
    rule->distinct = distinct;
    rule->distinct_count = distinct_count;
    if (!rule->head || atom_has_vars(rule->head)) {
        builder->status = GDL_STRATIFIED_BUILD_FAULT_V1;
        return false;
    }

    rule->substitution_count = variables.count;
    if (rule->substitution_count != 0u) {
        rule->substitution = arena_alloc(
            &builder->model->arena,
            rule->substitution_count * sizeof(*rule->substitution));
        for (size_t index = 0u; index < variables.count; index++) {
            CettaGdlFiniteHerbrandTermViewV1 term = {0};
            if (!cetta_gdl_finite_herbrand_term_view_v1(
                    builder->model->herbrand, assignment[index], &term)) {
                builder->status = GDL_STRATIFIED_BUILD_FAULT_V1;
                return false;
            }
            rule->substitution[index] =
                (CettaGdlStratifiedSubstitutionViewV1){
                    .name = variables.items[index].name,
                    .term_index = assignment[index],
                    .term = term.term,
                    .exact_type = term.exact_type,
                };
        }
    }

    rule->branch_choice_count = branch->choice_count;
    if (rule->branch_choice_count != 0u) {
        rule->branch_choices = arena_alloc(
            &builder->model->arena,
            rule->branch_choice_count * sizeof(*rule->branch_choices));
        for (size_t index = 0u; index < branch->choice_count; index++) {
            const GdlStratifiedTemplateChoiceV1 *choice =
                &branch->choices[index];
            rule->branch_choices[index] =
                (CettaGdlStratifiedBranchChoiceViewV1){
                    .or_path = choice->path,
                    .alternative = choice->alternative,
                    .source_literal_proofs =
                        choice->source.literal_proofs,
                    .source_literal_proof_count =
                        choice->source.literal_proof_count,
                };
        }
    }

    size_t positive_count = 0u;
    size_t negative_count = 0u;
    for (size_t index = 0u; index < branch->premise_count; index++) {
        positive_count += branch->premises[index].kind ==
            GDL_STRATIFIED_TEMPLATE_POSITIVE_V1;
        negative_count += branch->premises[index].kind ==
            GDL_STRATIFIED_TEMPLATE_NEGATIVE_V1;
    }
    rule->positive_count = positive_count;
    rule->negative_count = negative_count;
    if (positive_count != 0u)
        rule->positive = arena_alloc(
            &builder->model->arena,
            positive_count * sizeof(*rule->positive));
    if (negative_count != 0u)
        rule->negative = arena_alloc(
            &builder->model->arena,
            negative_count * sizeof(*rule->negative));
    positive_count = 0u;
    negative_count = 0u;
    for (size_t index = 0u; index < branch->premise_count; index++) {
        const GdlStratifiedTemplatePremiseV1 *premise =
            &branch->premises[index];
        if (premise->kind == GDL_STRATIFIED_TEMPLATE_POSITIVE_V1) {
            GdlStratifiedGroundPositiveV1 *ground =
                &rule->positive[positive_count++];
            ground->path = premise->path;
            ground->literal = gdl_stratified_ground_expression_v1(
                builder, &builder->model->arena, premise->expression,
                &variables, assignment, 0u);
            ground->source = premise->source;
            if (!ground->literal || atom_has_vars(ground->literal) ||
                premise->relation_stratum > template->head_stratum) {
                builder->status = GDL_STRATIFIED_BUILD_FAULT_V1;
                return false;
            }
            ground->relation_index = premise->relation_index;
        } else if (premise->kind ==
                   GDL_STRATIFIED_TEMPLATE_NEGATIVE_V1) {
            GdlStratifiedGroundNegativeV1 *ground =
                &rule->negative[negative_count++];
            ground->not_path = premise->path;
            ground->literal = gdl_stratified_ground_expression_v1(
                builder, &builder->model->arena, premise->expression,
                &variables, assignment, 0u);
            ground->source_not = premise->source;
            ground->source_operand = premise->operand_source;
            if (!ground->literal || atom_has_vars(ground->literal) ||
                premise->relation_stratum >= template->head_stratum) {
                builder->status = GDL_STRATIFIED_BUILD_FAULT_V1;
                return false;
            }
            ground->relation_index = premise->relation_index;
        }
    }
    *allowed_out = true;
    return true;
}

static bool gdl_stratified_compile_form_v1(
    GdlStratifiedBuilderV1 *builder,
    const GdlSourceRawFormV1 *source_form,
    size_t form_index) {
    if (!builder || builder->status != GDL_STRATIFIED_BUILD_OK_V1 ||
        !source_form || !source_form->form)
        return false;
    const GdlSourceRawExprV1 *form = source_form->form;
    if (gdl_stratified_raw_head_v1(form, "distinct")) {
        CettaGdlTypeOfNativeSourceJudgmentV1 source = {0};
        GdlStratifiedVariablesV1 variables = {0};
        bool okay = form->count == 3u &&
            gdl_stratified_source_judgment_v1(
                builder, form_index, "root", true, &source) &&
            gdl_stratified_collect_variables_v1(
                builder, form_index, form, "root", 0u, &variables) &&
            variables.count == 0u;
        Atom *left = okay
            ? gdl_stratified_ground_expression_v1(
                builder, &builder->model->arena, form->items[1],
                &variables, NULL, 0u)
            : NULL;
        Atom *right = okay
            ? gdl_stratified_ground_expression_v1(
                builder, &builder->model->arena, form->items[2],
                &variables, NULL, 0u)
            : NULL;
        free(variables.items);
        if (!okay || !left || !right || atom_eq(left, right)) {
            if (builder->status == GDL_STRATIFIED_BUILD_OK_V1)
                builder->status = GDL_STRATIFIED_BUILD_OUTSIDE_V1;
            return false;
        }
        builder->model->stats.source_facts++;
        return true;
    }
    bool is_rule = gdl_stratified_raw_head_v1(form, "<=");
    if (is_rule && form->count < 2u) {
        builder->status = GDL_STRATIFIED_BUILD_OUTSIDE_V1;
        return false;
    }
    const GdlSourceRawExprV1 *head = is_rule ? form->items[1] : form;
    const char *head_path = is_rule ? "1" : "root";
    size_t head_relation = SIZE_MAX;
    size_t head_stratum = 0u;
    CettaGdlTypeOfNativeSourceJudgmentV1 source_head = {0};
    if (!gdl_stratified_relation_index_v1(
            builder->model->stratification, head,
            &head_relation, &head_stratum) ||
        !gdl_stratified_source_judgment_v1(
            builder, form_index, head_path, true, &source_head)) {
        builder->status = GDL_STRATIFIED_BUILD_OUTSIDE_V1;
        return false;
    }

    GdlStratifiedVariablesV1 variables = {0};
    GdlStratifiedTemplateBranchesV1 branches = {0};
    GdlStratifiedTemplateBranchV1 empty = {0};
    bool okay = gdl_stratified_collect_variables_v1(
        builder, form_index, head, head_path, 0u, &variables);
    if (okay && is_rule)
        for (size_t premise = 2u;
             premise < form->count && okay; premise++) {
            char *path = gdl_stratified_path_child_v1(
                &builder->model->arena, "root", premise);
            okay = path && gdl_stratified_collect_variables_v1(
                builder, form_index, form->items[premise], path,
                0u, &variables);
        }
    if (okay)
        okay = gdl_stratified_template_branches_push_move_v1(
            &branches, &empty);
    if (!okay && builder->status == GDL_STRATIFIED_BUILD_OK_V1)
        builder->status = GDL_STRATIFIED_BUILD_INCOMPLETE_V1;
    if (okay && is_rule)
        for (size_t premise = 2u;
             premise < form->count && okay; premise++) {
            char *path = gdl_stratified_path_child_v1(
                &builder->model->arena, "root", premise);
            okay = path && gdl_stratified_expand_expression_v1(
                builder, form_index, form->items[premise], path,
                0u, &branches);
        }
    if (!okay)
        goto done;

    if (is_rule)
        builder->model->stats.source_rules++;
    else
        builder->model->stats.source_facts++;
    for (size_t branch_index = 0u;
         branch_index < branches.count && okay; branch_index++)
        okay = gdl_stratified_template_push_v1(
            builder, source_form, form_index, head,
            head_relation, head_stratum, source_head,
            &variables, &branches.items[branch_index]);

done:
    gdl_stratified_template_branches_free_v1(&branches);
    gdl_stratified_variables_free_v1(&variables);
    return builder->status == GDL_STRATIFIED_BUILD_OK_V1;
}

static uint64_t gdl_stratified_support_key_v1(
    Atom *literal, size_t relation_index) {
    uint64_t value = (uint64_t)atom_hash(literal);
    value ^= UINT64_C(0x9e3779b97f4a7c15) +
        (uint64_t)relation_index + (value << 6u) + (value >> 2u);
    value ^= value >> 30u;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27u;
    value *= UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31u);
}

static bool gdl_stratified_support_slots_rebuild_v1(
    CettaGdlStratifiedModelV1 *model, size_t capacity) {
    if (!model || capacity < 16u ||
        (capacity & (capacity - 1u)) != 0u ||
        capacity > SIZE_MAX / sizeof(size_t))
        return false;
    size_t *slots = cetta_malloc(capacity * sizeof(*slots));
    memset(slots, 0, capacity * sizeof(*slots));
    for (size_t index = 0u; index < model->support_count; index++) {
        GdlStratifiedSupportV1 *support = &model->supports[index];
        size_t slot = (size_t)gdl_stratified_support_key_v1(
            support->literal, support->relation_index) & (capacity - 1u);
        while (slots[slot] != 0u)
            slot = (slot + 1u) & (capacity - 1u);
        slots[slot] = index + 1u;
    }
    free(model->support_slots);
    model->support_slots = slots;
    model->support_slot_capacity = capacity;
    model->support_slot_used = model->support_count;
    return true;
}

static bool gdl_stratified_support_slots_prepare_v1(
    CettaGdlStratifiedModelV1 *model) {
    if (!model)
        return false;
    if (model->support_slot_capacity == 0u)
        return gdl_stratified_support_slots_rebuild_v1(model, 16u);
    if (model->support_slot_used < model->support_slot_capacity / 2u)
        return true;
    if (model->support_slot_capacity > SIZE_MAX / 2u)
        return false;
    return gdl_stratified_support_slots_rebuild_v1(
        model, model->support_slot_capacity * 2u);
}

static uint64_t gdl_stratified_grounding_key_v1(
    size_t template_index, const size_t *assignment,
    size_t assignment_count) {
    uint64_t value = UINT64_C(1469598103934665603);
    value ^= (uint64_t)template_index;
    value *= UINT64_C(1099511628211);
    for (size_t index = 0u; index < assignment_count; index++) {
        value ^= (uint64_t)assignment[index] +
            UINT64_C(0x9e3779b97f4a7c15);
        value *= UINT64_C(1099511628211);
    }
    value ^= (uint64_t)assignment_count;
    value *= UINT64_C(1099511628211);
    return value;
}

static bool gdl_stratified_same_grounding_v1(
    const GdlStratifiedGroundingV1 *grounding,
    size_t template_index,
    const size_t *assignment,
    size_t assignment_count) {
    if (!grounding || grounding->template_index != template_index ||
        grounding->assignment_count != assignment_count)
        return false;
    for (size_t index = 0u; index < assignment_count; index++)
        if (grounding->assignment[index] != assignment[index])
            return false;
    return true;
}

static bool gdl_stratified_grounding_slots_rebuild_v1(
    CettaGdlStratifiedModelV1 *model, size_t capacity) {
    if (!model || capacity < 16u ||
        (capacity & (capacity - 1u)) != 0u ||
        capacity > SIZE_MAX / sizeof(size_t))
        return false;
    size_t *slots = cetta_malloc(capacity * sizeof(*slots));
    memset(slots, 0, capacity * sizeof(*slots));
    for (size_t index = 0u; index < model->grounding_count; index++) {
        const GdlStratifiedGroundingV1 *grounding =
            &model->groundings[index];
        size_t slot = (size_t)gdl_stratified_grounding_key_v1(
            grounding->template_index, grounding->assignment,
            grounding->assignment_count) & (capacity - 1u);
        while (slots[slot] != 0u)
            slot = (slot + 1u) & (capacity - 1u);
        slots[slot] = index + 1u;
    }
    free(model->grounding_slots);
    model->grounding_slots = slots;
    model->grounding_slot_capacity = capacity;
    model->grounding_slot_used = model->grounding_count;
    return true;
}

static bool gdl_stratified_grounding_slots_prepare_v1(
    CettaGdlStratifiedModelV1 *model) {
    if (!model)
        return false;
    if (model->grounding_slot_capacity == 0u)
        return gdl_stratified_grounding_slots_rebuild_v1(model, 16u);
    if (model->grounding_slot_used <
        model->grounding_slot_capacity / 2u)
        return true;
    if (model->grounding_slot_capacity > SIZE_MAX / 2u)
        return false;
    return gdl_stratified_grounding_slots_rebuild_v1(
        model, model->grounding_slot_capacity * 2u);
}

static size_t gdl_stratified_grounding_find_v1(
    const CettaGdlStratifiedModelV1 *model,
    size_t template_index,
    const size_t *assignment,
    size_t assignment_count) {
    if (!model || (assignment_count != 0u && !assignment) ||
        model->grounding_slot_capacity == 0u)
        return SIZE_MAX;
    size_t slot = (size_t)gdl_stratified_grounding_key_v1(
        template_index, assignment, assignment_count) &
        (model->grounding_slot_capacity - 1u);
    for (;;) {
        size_t encoded = model->grounding_slots[slot];
        if (encoded == 0u)
            return SIZE_MAX;
        size_t index = encoded - 1u;
        if (gdl_stratified_same_grounding_v1(
                &model->groundings[index], template_index,
                assignment, assignment_count))
            return index;
        slot = (slot + 1u) &
            (model->grounding_slot_capacity - 1u);
    }
}

static bool gdl_stratified_grounding_add_v1(
    GdlStratifiedBuilderV1 *builder,
    size_t template_index,
    const size_t *assignment,
    size_t assignment_count,
    bool *new_out) {
    if (new_out)
        *new_out = false;
    if (!builder || builder->status != GDL_STRATIFIED_BUILD_OK_V1 ||
        !new_out || (assignment_count != 0u && !assignment))
        return false;
    CettaGdlStratifiedModelV1 *model = builder->model;
    if (gdl_stratified_grounding_find_v1(
            model, template_index, assignment,
            assignment_count) != SIZE_MAX)
        return true;
    if (model->grounding_count >= model->limits.max_ground_instances ||
        !gdl_stratified_grounding_slots_prepare_v1(model) ||
        !gdl_stratified_reserve_v1(
            (void **)&model->groundings, &model->grounding_capacity,
            model->grounding_count + 1u,
            sizeof(*model->groundings))) {
        builder->status = GDL_STRATIFIED_BUILD_INCOMPLETE_V1;
        return false;
    }
    size_t *copy = NULL;
    if (assignment_count != 0u) {
        if (assignment_count > SIZE_MAX / sizeof(*copy)) {
            builder->status = GDL_STRATIFIED_BUILD_INCOMPLETE_V1;
            return false;
        }
        copy = arena_alloc(
            &model->arena, assignment_count * sizeof(*copy));
        memcpy(copy, assignment, assignment_count * sizeof(*copy));
    }
    size_t index = model->grounding_count++;
    model->groundings[index] = (GdlStratifiedGroundingV1){
        .template_index = template_index,
        .assignment = copy,
        .assignment_count = assignment_count,
    };
    size_t slot = (size_t)gdl_stratified_grounding_key_v1(
        template_index, copy, assignment_count) &
        (model->grounding_slot_capacity - 1u);
    while (model->grounding_slots[slot] != 0u)
        slot = (slot + 1u) &
            (model->grounding_slot_capacity - 1u);
    model->grounding_slots[slot] = index + 1u;
    model->grounding_slot_used++;
    model->stats.ground_instances = model->grounding_count;
    *new_out = true;
    return true;
}

static size_t gdl_stratified_support_find_v1(
    const CettaGdlStratifiedModelV1 *model,
    Atom *literal,
    size_t relation_index) {
    if (!model || !literal ||
        model->support_slot_capacity == 0u)
        return SIZE_MAX;
    size_t slot = (size_t)gdl_stratified_support_key_v1(
        literal, relation_index) & (model->support_slot_capacity - 1u);
    for (;;) {
        size_t encoded = model->support_slots[slot];
        if (encoded == 0u)
            return SIZE_MAX;
        size_t index = encoded - 1u;
        const GdlStratifiedSupportV1 *support = &model->supports[index];
        if (support->relation_index == relation_index &&
            atom_eq(support->literal, literal))
            return index;
        slot = (slot + 1u) & (model->support_slot_capacity - 1u);
    }
}

static bool gdl_stratified_support_add_v1(
    CettaGdlStratifiedModelV1 *model,
    Atom *literal,
    size_t relation_index,
    size_t stratum,
    size_t *index_out,
    bool *new_out) {
    if (new_out)
        *new_out = false;
    if (!model || !literal || !index_out || !new_out ||
        relation_index >= model->relation_count)
        return false;
    size_t found = gdl_stratified_support_find_v1(
        model, literal, relation_index);
    if (found != SIZE_MAX) {
        *index_out = found;
        return true;
    }
    GdlStratifiedRelationSupportsV1 *relation_supports =
        &model->relation_supports[relation_index];
    if (model->support_count >= model->limits.max_supports ||
        !gdl_stratified_reserve_v1(
            (void **)&relation_supports->support_indices,
            &relation_supports->capacity,
            relation_supports->count + 1u,
            sizeof(*relation_supports->support_indices)) ||
        !gdl_stratified_support_slots_prepare_v1(model) ||
        !gdl_stratified_reserve_v1(
            (void **)&model->supports, &model->support_capacity,
            model->support_count + 1u, sizeof(*model->supports)))
        return false;
    size_t index = model->support_count++;
    GdlStratifiedSupportV1 *support = &model->supports[index];
    memset(support, 0, sizeof(*support));
    support->literal = literal;
    support->relation_index = relation_index;
    support->stratum = stratum;
    size_t slot = (size_t)gdl_stratified_support_key_v1(
        literal, relation_index) & (model->support_slot_capacity - 1u);
    while (model->support_slots[slot] != 0u)
        slot = (slot + 1u) & (model->support_slot_capacity - 1u);
    model->support_slots[slot] = index + 1u;
    model->support_slot_used++;
    relation_supports->support_indices[relation_supports->count++] = index;
    model->stats.support_nodes = model->support_count;
    *index_out = index;
    *new_out = true;
    return true;
}

static size_t gdl_stratified_relation_support_count_v1(
    const CettaGdlStratifiedModelV1 *model, size_t relation_index) {
    size_t count = 0u;
    if (!model)
        return 0u;
    for (size_t index = 0u; index < model->support_count; index++)
        count += model->supports[index].relation_index == relation_index;
    return count;
}

static GdlStratifiedBuildV1 gdl_stratified_activate_rule_v1(
    CettaGdlStratifiedModelV1 *model,
    GdlStratifiedGroundRuleV1 *rule,
    bool *new_support_out) {
    if (new_support_out)
        *new_support_out = false;
    if (!model || !rule || !new_support_out || rule->active ||
        rule->source_head.literal_proof_count == 0u)
        return GDL_STRATIFIED_BUILD_FAULT_V1;
    size_t *positive_supports = rule->positive_count
        ? cetta_malloc(rule->positive_count * sizeof(*positive_supports))
        : NULL;
    for (size_t index = 0u; index < rule->positive_count; index++) {
        size_t support = gdl_stratified_support_find_v1(
            model, rule->positive[index].literal,
            rule->positive[index].relation_index);
        if (support == SIZE_MAX) {
            free(positive_supports);
            return GDL_STRATIFIED_BUILD_OK_V1;
        }
        positive_supports[index] = support;
    }
    for (size_t index = 0u; index < rule->negative_count; index++) {
        CettaGdlStratifiedRelationViewV1 relation = {0};
        if (!cetta_gdl_stratification_relation_view_v1(
                model->stratification,
                rule->negative[index].relation_index, &relation) ||
            relation.stratum >= rule->head_stratum ||
            model->stats.completed_strata <= relation.stratum) {
            free(positive_supports);
            return GDL_STRATIFIED_BUILD_FAULT_V1;
        }
        if (gdl_stratified_support_find_v1(
                model, rule->negative[index].literal,
                rule->negative[index].relation_index) != SIZE_MAX) {
            free(positive_supports);
            return GDL_STRATIFIED_BUILD_OK_V1;
        }
    }

    size_t edge_count = rule->source_head.literal_proof_count;
    if (model->edge_count > model->limits.max_proof_edges ||
        edge_count > model->limits.max_proof_edges - model->edge_count) {
        free(positive_supports);
        return GDL_STRATIFIED_BUILD_INCOMPLETE_V1;
    }
    size_t existing = gdl_stratified_support_find_v1(
        model, rule->head, rule->head_relation);
    if (existing == SIZE_MAX &&
        model->support_count >= model->limits.max_supports) {
        free(positive_supports);
        return GDL_STRATIFIED_BUILD_INCOMPLETE_V1;
    }
    if (!gdl_stratified_reserve_v1(
            (void **)&model->edges, &model->edge_capacity,
            model->edge_count + edge_count, sizeof(*model->edges))) {
        free(positive_supports);
        return GDL_STRATIFIED_BUILD_INCOMPLETE_V1;
    }
    size_t support_index = existing;
    bool new_support = false;
    size_t *new_edge_indices = NULL;
    if (existing != SIZE_MAX) {
        GdlStratifiedSupportV1 *support = &model->supports[existing];
        if (!gdl_stratified_reserve_v1(
                (void **)&support->proof_edge_indices,
                &support->proof_edge_capacity,
                support->proof_edge_count + edge_count,
                sizeof(*support->proof_edge_indices))) {
            free(positive_supports);
            return GDL_STRATIFIED_BUILD_INCOMPLETE_V1;
        }
    } else {
        new_edge_indices = cetta_malloc(
            edge_count * sizeof(*new_edge_indices));
        if (!gdl_stratified_support_add_v1(
                model, rule->head, rule->head_relation,
                rule->head_stratum, &support_index, &new_support)) {
            free(new_edge_indices);
            free(positive_supports);
            return GDL_STRATIFIED_BUILD_INCOMPLETE_V1;
        }
        model->supports[support_index].proof_edge_indices =
            new_edge_indices;
        model->supports[support_index].proof_edge_capacity = edge_count;
    }

    for (size_t proof_index = 0u; proof_index < edge_count; proof_index++) {
        GdlStratifiedProofEdgeV1 *edge =
            &model->edges[model->edge_count];
        memset(edge, 0, sizeof(*edge));
        edge->view.origin = rule->origin;
        edge->view.episode_occurrence = rule->episode_occurrence;
        edge->view.source_form_ordinal = rule->source_form_ordinal;
        edge->view.source_start_line = rule->source_start_line;
        edge->view.source_end_line = rule->source_end_line;
        edge->view.head_support_index = support_index;
        edge->view.source_head_proof =
            rule->source_head.literal_proofs[proof_index];
        edge->view.substitution = rule->substitution;
        edge->view.substitution_count = rule->substitution_count;
        edge->view.branch_choices = rule->branch_choices;
        edge->view.branch_choice_count = rule->branch_choice_count;
        edge->view.distinct_evidence_count = rule->distinct_count;
        if (rule->distinct_count != 0u) {
            CettaGdlStratifiedDistinctViewV1 *distinct = arena_alloc(
                &model->arena,
                rule->distinct_count * sizeof(*distinct));
            for (size_t index = 0u; index < rule->distinct_count; index++) {
                distinct[index] = (CettaGdlStratifiedDistinctViewV1){
                    .path = rule->distinct[index].path,
                    .left = rule->distinct[index].left,
                    .right = rule->distinct[index].right,
                    .source_literal_proofs =
                        rule->distinct[index].source.literal_proofs,
                    .source_literal_proof_count =
                        rule->distinct[index].source.literal_proof_count,
                };
            }
            edge->view.distinct_evidence = distinct;
        }
        edge->view.positive_premise_count = rule->positive_count;
        if (rule->positive_count != 0u) {
            CettaGdlStratifiedPositivePremiseViewV1 *positive =
                arena_alloc(
                    &model->arena,
                    rule->positive_count * sizeof(*positive));
            for (size_t index = 0u; index < rule->positive_count; index++) {
                positive[index] =
                    (CettaGdlStratifiedPositivePremiseViewV1){
                        .path = rule->positive[index].path,
                        .support_index = positive_supports[index],
                        .literal = rule->positive[index].literal,
                        .source_literal_proofs =
                            rule->positive[index].source.literal_proofs,
                        .source_literal_proof_count =
                            rule->positive[index]
                                .source.literal_proof_count,
                    };
            }
            edge->view.positive_premises = positive;
        }
        edge->view.absence_receipt_count = rule->negative_count;
        if (rule->negative_count != 0u) {
            CettaGdlStratifiedAbsenceViewV1 *absence = arena_alloc(
                &model->arena,
                rule->negative_count * sizeof(*absence));
            for (size_t index = 0u; index < rule->negative_count; index++) {
                CettaGdlStratifiedRelationViewV1 relation = {0};
                if (!cetta_gdl_stratification_relation_view_v1(
                        model->stratification,
                        rule->negative[index].relation_index, &relation)) {
                    free(positive_supports);
                    return GDL_STRATIFIED_BUILD_FAULT_V1;
                }
                absence[index] = (CettaGdlStratifiedAbsenceViewV1){
                    .not_path = rule->negative[index].not_path,
                    .literal = rule->negative[index].literal,
                    .relation_index =
                        rule->negative[index].relation_index,
                    .completed_stratum = relation.stratum,
                    .completed_relation_support_count =
                        model->completed_relation_support_counts[
                            rule->negative[index].relation_index],
                    .source_not_proofs =
                        rule->negative[index].source_not.literal_proofs,
                    .source_not_proof_count =
                        rule->negative[index]
                            .source_not.literal_proof_count,
                    .source_operand_proofs =
                        rule->negative[index]
                            .source_operand.literal_proofs,
                    .source_operand_proof_count =
                        rule->negative[index]
                            .source_operand.literal_proof_count,
                };
            }
            edge->view.absence_receipts = absence;
        }
        model->supports[support_index].proof_edge_indices[
            model->supports[support_index].proof_edge_count++] =
                model->edge_count;
        model->edge_count++;
        model->stats.positive_premise_references += rule->positive_count;
        model->stats.absence_receipts += rule->negative_count;
    }
    model->stats.proof_edges = model->edge_count;
    rule->active = true;
    *new_support_out = new_support;
    free(positive_supports);
    return GDL_STRATIFIED_BUILD_OK_V1;
}

static const GdlStratifiedTemplatePremiseV1 *
gdl_stratified_positive_premise_v1(
    const GdlStratifiedTemplateV1 *template, size_t ordinal) {
    if (!template)
        return NULL;
    size_t seen = 0u;
    for (size_t index = 0u;
         index < template->branch.premise_count; index++) {
        const GdlStratifiedTemplatePremiseV1 *premise =
            &template->branch.premises[index];
        if (premise->kind != GDL_STRATIFIED_TEMPLATE_POSITIVE_V1)
            continue;
        if (seen++ == ordinal)
            return premise;
    }
    return NULL;
}

static size_t gdl_stratified_positive_premise_count_v1(
    const GdlStratifiedTemplateV1 *template) {
    size_t count = 0u;
    if (!template)
        return 0u;
    for (size_t index = 0u;
         index < template->branch.premise_count; index++)
        count += template->branch.premises[index].kind ==
            GDL_STRATIFIED_TEMPLATE_POSITIVE_V1;
    return count;
}

static bool gdl_stratified_assignments_append_v1(
    GdlStratifiedBuilderV1 *builder,
    GdlStratifiedAssignmentsV1 *target,
    const GdlStratifiedAssignmentsV1 *source) {
    if (!builder || builder->status != GDL_STRATIFIED_BUILD_OK_V1 ||
        !target || !source || target->width != source->width)
        return false;
    if (source->count == 0u)
        return true;
    if (target->width == 0u) {
        if (target->count != 0u || source->count != 1u) {
            builder->status = GDL_STRATIFIED_BUILD_FAULT_V1;
            return false;
        }
        target->count = 1u;
        target->capacity = 1u;
        return true;
    }
    if (source->count > SIZE_MAX - target->count ||
        !gdl_stratified_reserve_v1(
            (void **)&target->rows, &target->capacity,
            target->count + source->count,
            target->width * sizeof(*target->rows))) {
        builder->status = GDL_STRATIFIED_BUILD_INCOMPLETE_V1;
        return false;
    }
    memcpy(target->rows + target->count * target->width,
           source->rows,
           source->count * source->width * sizeof(*source->rows));
    target->count += source->count;
    return true;
}

static GdlStratifiedBuildV1 gdl_stratified_join_template_v1(
    GdlStratifiedBuilderV1 *builder,
    size_t template_index,
    size_t fixed_positive_ordinal,
    size_t fixed_support_index,
    bool *new_support_out) {
    if (new_support_out)
        *new_support_out = false;
    if (!builder || builder->status != GDL_STRATIFIED_BUILD_OK_V1 ||
        !new_support_out ||
        template_index >= builder->model->template_count)
        return GDL_STRATIFIED_BUILD_FAULT_V1;
    CettaGdlStratifiedModelV1 *model = builder->model;
    const GdlStratifiedTemplateV1 *template =
        &model->templates[template_index];
    size_t positive_count =
        gdl_stratified_positive_premise_count_v1(template);
    if ((fixed_positive_ordinal == SIZE_MAX) !=
            (fixed_support_index == SIZE_MAX) ||
        (fixed_positive_ordinal != SIZE_MAX &&
         (fixed_positive_ordinal >= positive_count ||
          fixed_support_index >= model->support_count)))
        return GDL_STRATIFIED_BUILD_FAULT_V1;

    GdlStratifiedAssignmentsV1 assignments = {0};
    if (!gdl_stratified_assignments_initial_v1(
            builder, template->variable_count, NULL, &assignments))
        return builder->status;
    for (size_t premise_ordinal = 0u;
         premise_ordinal < positive_count && assignments.count != 0u;
         premise_ordinal++) {
        const GdlStratifiedTemplatePremiseV1 *premise =
            gdl_stratified_positive_premise_v1(
                template, premise_ordinal);
        if (!premise || premise->relation_index >= model->relation_count) {
            gdl_stratified_assignments_free_v1(&assignments);
            return GDL_STRATIFIED_BUILD_FAULT_V1;
        }
        const GdlStratifiedRelationSupportsV1 *bucket =
            &model->relation_supports[premise->relation_index];
        size_t support_begin = 0u;
        size_t support_end = bucket->count;
        if (premise_ordinal == fixed_positive_ordinal) {
            support_begin = 0u;
            support_end = 1u;
        }
        GdlStratifiedAssignmentsV1 output = {
            .width = template->variable_count,
        };
        for (size_t row_index = 0u;
             row_index < assignments.count; row_index++) {
            const size_t *row = template->variable_count
                ? assignments.rows +
                    row_index * template->variable_count
                : NULL;
            for (size_t support_position = support_begin;
                 support_position < support_end; support_position++) {
                size_t support_index =
                    premise_ordinal == fixed_positive_ordinal
                    ? fixed_support_index
                    : bucket->support_indices[support_position];
                const GdlStratifiedSupportV1 *support =
                    &model->supports[support_index];
                if (support->relation_index != premise->relation_index)
                    continue;
                GdlStratifiedAssignmentsV1 candidate = {0};
                if (!gdl_stratified_assignments_initial_v1(
                        builder, template->variable_count,
                        row, &candidate) ||
                    !gdl_stratified_match_expression_v1(
                        builder, premise->expression, support->literal,
                        template, &candidate, 0u) ||
                    !gdl_stratified_assignments_append_v1(
                        builder, &output, &candidate)) {
                    gdl_stratified_assignments_free_v1(&candidate);
                    gdl_stratified_assignments_free_v1(&output);
                    gdl_stratified_assignments_free_v1(&assignments);
                    return builder->status;
                }
                gdl_stratified_assignments_free_v1(&candidate);
            }
        }
        gdl_stratified_assignments_free_v1(&assignments);
        assignments = output;
    }

    for (size_t row_index = 0u;
         row_index < assignments.count; row_index++) {
        const size_t *assignment = template->variable_count
            ? assignments.rows + row_index * template->variable_count
            : NULL;
        for (size_t variable = 0u;
             variable < template->variable_count; variable++)
            if (assignment[variable] == SIZE_MAX) {
                gdl_stratified_assignments_free_v1(&assignments);
                builder->status = GDL_STRATIFIED_BUILD_FAULT_V1;
                return builder->status;
            }
        bool new_grounding = false;
        if (!gdl_stratified_grounding_add_v1(
                builder, template_index, assignment,
                template->variable_count, &new_grounding)) {
            gdl_stratified_assignments_free_v1(&assignments);
            return builder->status;
        }
        if (!new_grounding)
            continue;
        GdlStratifiedGroundRuleV1 rule = {0};
        bool allowed = false;
        if (!gdl_stratified_ground_rule_v1(
                builder, template, assignment, &rule, &allowed)) {
            gdl_stratified_assignments_free_v1(&assignments);
            return builder->status;
        }
        if (!allowed)
            continue;
        bool new_support = false;
        GdlStratifiedBuildV1 activated =
            gdl_stratified_activate_rule_v1(
                model, &rule, &new_support);
        if (activated != GDL_STRATIFIED_BUILD_OK_V1) {
            gdl_stratified_assignments_free_v1(&assignments);
            builder->status = activated;
            return activated;
        }
        *new_support_out = *new_support_out || new_support;
    }
    gdl_stratified_assignments_free_v1(&assignments);
    return GDL_STRATIFIED_BUILD_OK_V1;
}

static GdlStratifiedBuildV1 gdl_stratified_evaluate_v1(
    CettaGdlStratifiedModelV1 *model) {
    if (!model)
        return GDL_STRATIFIED_BUILD_FAULT_V1;
    GdlStratifiedBuilderV1 builder = {
        .model = model,
        .status = GDL_STRATIFIED_BUILD_OK_V1,
    };
    for (size_t stratum = 0u;
         stratum <= model->maximum_stratum; stratum++) {
        if (model->stats.rounds >= model->limits.max_rounds)
            return GDL_STRATIFIED_BUILD_INCOMPLETE_V1;
        model->stats.rounds++;
        /* Episode facts may already inhabit this stratum before authored
         * zero-premise rules run.  They are part of the initial frontier,
         * while lower/higher-stratum supports are filtered below. */
        size_t frontier_begin = 0u;
        for (size_t template_index = 0u;
             template_index < model->template_count; template_index++) {
            const GdlStratifiedTemplateV1 *template =
                &model->templates[template_index];
            if (template->head_stratum != stratum ||
                template->same_stratum_positive_count != 0u)
                continue;
            bool ignored = false;
            GdlStratifiedBuildV1 joined =
                gdl_stratified_join_template_v1(
                    &builder, template_index, SIZE_MAX, SIZE_MAX,
                    &ignored);
            if (joined != GDL_STRATIFIED_BUILD_OK_V1)
                return joined;
        }
        size_t frontier_end = model->support_count;
        bool has_recursive_templates = false;
        for (size_t template_index = 0u;
             template_index < model->template_count; template_index++)
            has_recursive_templates = has_recursive_templates ||
                (model->templates[template_index].head_stratum == stratum &&
                 model->templates[template_index]
                         .same_stratum_positive_count != 0u);
        while (has_recursive_templates && frontier_begin < frontier_end) {
            if (model->stats.rounds >= model->limits.max_rounds)
                return GDL_STRATIFIED_BUILD_INCOMPLETE_V1;
            model->stats.rounds++;
            for (size_t support_index = frontier_begin;
                 support_index < frontier_end; support_index++) {
                const size_t support_stratum =
                    model->supports[support_index].stratum;
                const size_t support_relation_index =
                    model->supports[support_index].relation_index;
                if (support_stratum != stratum)
                    continue;
                for (size_t template_index = 0u;
                     template_index < model->template_count;
                     template_index++) {
                    const GdlStratifiedTemplateV1 *template =
                        &model->templates[template_index];
                    if (template->head_stratum != stratum ||
                        template->same_stratum_positive_count == 0u)
                        continue;
                    size_t positive_count =
                        gdl_stratified_positive_premise_count_v1(
                            template);
                    for (size_t premise_ordinal = 0u;
                         premise_ordinal < positive_count;
                         premise_ordinal++) {
                        const GdlStratifiedTemplatePremiseV1 *premise =
                            gdl_stratified_positive_premise_v1(
                                template, premise_ordinal);
                        if (!premise ||
                            premise->relation_stratum != stratum ||
                            premise->relation_index !=
                                support_relation_index)
                            continue;
                        bool ignored = false;
                        GdlStratifiedBuildV1 joined =
                            gdl_stratified_join_template_v1(
                                &builder, template_index,
                                premise_ordinal, support_index,
                                &ignored);
                        if (joined != GDL_STRATIFIED_BUILD_OK_V1)
                            return joined;
                    }
                }
            }
            frontier_begin = frontier_end;
            frontier_end = model->support_count;
        }
        for (size_t relation_index = 0u;
             relation_index < model->relation_count; relation_index++) {
            CettaGdlStratifiedRelationViewV1 relation = {0};
            if (!cetta_gdl_stratification_relation_view_v1(
                    model->stratification, relation_index, &relation))
                return GDL_STRATIFIED_BUILD_FAULT_V1;
            if (relation.stratum == stratum)
                model->completed_relation_support_counts[relation_index] =
                    gdl_stratified_relation_support_count_v1(
                        model, relation_index);
        }
        model->stats.completed_strata = stratum + 1u;
    }
    return GDL_STRATIFIED_BUILD_OK_V1;
}

static CettaGdlStratifiedModelKindV1 gdl_stratified_result_kind_v1(
    GdlStratifiedBuildV1 status) {
    switch (status) {
    case GDL_STRATIFIED_BUILD_OK_V1:
        return CETTA_GDL_STRATIFIED_MODEL_ESTABLISHED_V1;
    case GDL_STRATIFIED_BUILD_INCOMPLETE_V1:
        return CETTA_GDL_STRATIFIED_MODEL_INCOMPLETE_V1;
    case GDL_STRATIFIED_BUILD_FAULT_V1:
        return CETTA_GDL_STRATIFIED_MODEL_ENGINE_FAULT_V1;
    case GDL_STRATIFIED_BUILD_OUTSIDE_V1:
    default:
        return CETTA_GDL_STRATIFIED_MODEL_OUTSIDE_FRAGMENT_V1;
    }
}

static GdlStratifiedBuildV1
gdl_stratified_model_compile_components_v1(
    const GdlSourceRawFormsV1 *forms,
    const CettaGdlTypeOfNativeV1 *typing,
    const CettaGdlFiniteHerbrandV1 *herbrand,
    const CettaGdlStratificationV1 *stratification,
    CettaGdlStratifiedModelLimitsV1 limits,
    CettaGdlStratifiedModelV1 **model_out) {
    if (model_out)
        *model_out = NULL;
    if (!forms || !typing || !herbrand || !stratification ||
        !model_out || forms->foreign_lines != 0u || forms->count == 0u)
        return GDL_STRATIFIED_BUILD_OUTSIDE_V1;
    CettaGdlStratifiedModelV1 *model = cetta_malloc(sizeof(*model));
    memset(model, 0, sizeof(*model));
    arena_init(&model->arena);
    arena_set_runtime_kind(
        &model->arena, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    model->typing = typing;
    model->herbrand = herbrand;
    model->stratification = stratification;
    model->limits = limits;
    if (model->limits.max_variables_per_form == 0u)
        model->limits.max_variables_per_form =
            GDL_STRATIFIED_MODEL_DEFAULT_MAX_VARIABLES_V1;
    if (model->limits.max_branch_expansions == 0u)
        model->limits.max_branch_expansions =
            GDL_STRATIFIED_MODEL_DEFAULT_MAX_BRANCHES_V1;
    if (model->limits.max_assignments == 0u)
        model->limits.max_assignments =
            GDL_STRATIFIED_MODEL_DEFAULT_MAX_ASSIGNMENTS_V1;
    if (model->limits.max_ground_instances == 0u)
        model->limits.max_ground_instances =
            GDL_STRATIFIED_MODEL_DEFAULT_MAX_GROUND_INSTANCES_V1;
    if (model->limits.max_supports == 0u)
        model->limits.max_supports =
            GDL_STRATIFIED_MODEL_DEFAULT_MAX_SUPPORTS_V1;
    if (model->limits.max_proof_edges == 0u)
        model->limits.max_proof_edges =
            GDL_STRATIFIED_MODEL_DEFAULT_MAX_PROOF_EDGES_V1;
    if (model->limits.max_rounds == 0u)
        model->limits.max_rounds =
            GDL_STRATIFIED_MODEL_DEFAULT_MAX_ROUNDS_V1;
    if (model->limits.max_logical_depth == 0u)
        model->limits.max_logical_depth =
            GDL_STRATIFIED_MODEL_DEFAULT_MAX_DEPTH_V1;
    model->maximum_stratum =
        cetta_gdl_stratification_maximum_stratum_v1(stratification);
    size_t relation_count =
        cetta_gdl_stratification_relation_count_v1(stratification);
    if (relation_count > SIZE_MAX /
            sizeof(*model->completed_relation_support_counts) ||
        relation_count > SIZE_MAX / sizeof(*model->relation_supports)) {
        cetta_gdl_stratified_model_destroy_v1(model);
        return GDL_STRATIFIED_BUILD_INCOMPLETE_V1;
    }
    model->relation_count = relation_count;
    model->relation_supports = cetta_malloc(
        (relation_count ? relation_count : 1u) *
        sizeof(*model->relation_supports));
    memset(model->relation_supports, 0,
           (relation_count ? relation_count : 1u) *
               sizeof(*model->relation_supports));
    model->completed_relation_support_counts = cetta_malloc(
        (relation_count ? relation_count : 1u) *
        sizeof(*model->completed_relation_support_counts));
    memset(model->completed_relation_support_counts, 0,
           (relation_count ? relation_count : 1u) *
               sizeof(*model->completed_relation_support_counts));
    model->stats.source_forms =
        gdl_source_selected_form_count_v1(forms);

    GdlStratifiedBuilderV1 builder = {
        .model = model,
        .status = GDL_STRATIFIED_BUILD_OK_V1,
    };
    for (size_t index = 0u;
         index < forms->count &&
         builder.status == GDL_STRATIFIED_BUILD_OK_V1; index++)
        if (forms->items[index].selected)
            (void)gdl_stratified_compile_form_v1(
                &builder, &forms->items[index], index);
    if (builder.status == GDL_STRATIFIED_BUILD_OUTSIDE_V1 ||
        builder.status == GDL_STRATIFIED_BUILD_FAULT_V1) {
        cetta_gdl_stratified_model_destroy_v1(model);
        return builder.status;
    }
    *model_out = model;
    return builder.status;
}

static CettaGdlStratifiedModelResultV1
gdl_stratified_model_construct_components_v1(
    const GdlSourceRawFormsV1 *forms,
    const CettaGdlTypeOfNativeV1 *typing,
    const CettaGdlFiniteHerbrandV1 *herbrand,
    const CettaGdlStratificationV1 *stratification,
    CettaGdlStratifiedModelLimitsV1 limits) {
    CettaGdlStratifiedModelV1 *model = NULL;
    GdlStratifiedBuildV1 status =
        gdl_stratified_model_compile_components_v1(
            forms, typing, herbrand, stratification,
            limits, &model);
    if (status == GDL_STRATIFIED_BUILD_OK_V1)
        status = gdl_stratified_evaluate_v1(model);
    CettaGdlStratifiedModelKindV1 kind =
        gdl_stratified_result_kind_v1(status);
    if (kind == CETTA_GDL_STRATIFIED_MODEL_OUTSIDE_FRAGMENT_V1 ||
        kind == CETTA_GDL_STRATIFIED_MODEL_ENGINE_FAULT_V1) {
        cetta_gdl_stratified_model_destroy_v1(model);
        model = NULL;
    }
    return (CettaGdlStratifiedModelResultV1){
        .kind = kind,
        .model = model,
    };
}

static CettaGdlStratifiedModelKindV1
gdl_stratified_model_parse_kind_v1(GdlSourceParseV1 kind) {
    switch (kind) {
    case GDL_SOURCE_PARSE_OK_V1:
        return CETTA_GDL_STRATIFIED_MODEL_ESTABLISHED_V1;
    case GDL_SOURCE_PARSE_INCOMPLETE_V1:
        return CETTA_GDL_STRATIFIED_MODEL_INCOMPLETE_V1;
    case GDL_SOURCE_PARSE_ENGINE_FAULT_V1:
        return CETTA_GDL_STRATIFIED_MODEL_ENGINE_FAULT_V1;
    case GDL_SOURCE_PARSE_OUTSIDE_FRAGMENT_V1:
    default:
        return CETTA_GDL_STRATIFIED_MODEL_OUTSIDE_FRAGMENT_V1;
    }
}

static bool gdl_stratified_model_attach_identity_v1(
    CettaGdlStratifiedModelV1 *model,
    const GdlSourcePackageV1 *package,
    const char *calculus_input_sha256,
    const GdlSourceTargetSliceV1 *target_slice,
    bool completed) {
    if (!model || !package || !calculus_input_sha256)
        return false;
    memcpy(model->source_sha256, package->source_sha256,
           sizeof(model->source_sha256));
    memcpy(model->profile_sha256, package->profile_sha256,
           sizeof(model->profile_sha256));
    memcpy(model->calculus_input_sha256,
           calculus_input_sha256,
           sizeof(model->calculus_input_sha256));
    memcpy(model->revision, package->revision,
           sizeof(model->revision));
    if (target_slice) {
        model->target_name = arena_strdup(
            &model->arena, target_slice->target_name);
        if (!model->target_name)
            return false;
        model->target_arity = target_slice->target_arity;
        model->target_source_forms = target_slice->source_forms;
        model->target_selected_forms = target_slice->selected_forms;
        model->target_reachable_relations =
            target_slice->reachable_relations;
        model->target_external_relations =
            target_slice->external_relations;
    }
    model->selection = gdl_stratified_no_selection_v1();
    model->selected_realization_identity = 0u;
    model->token_valid = false;
    if (!completed)
        return true;
    size_t frontier_index = SIZE_MAX;
    model->selection = gdl_stratified_select_v1(&frontier_index);
    if (!gdl_stratified_selection_is_native_greatest_v1(
            &model->selection, frontier_index) ||
        !cetta_nik_direct_authority_v1_token_from_sha256(
            &g_gdl_stratified_model_authority_v1,
            model->calculus_input_sha256, 1u, &model->token))
        return false;
    model->selected_realization_identity =
        g_gdl_stratified_model_authority_v1.realization_identity;
    model->token_valid = true;
    return true;
}

static CettaGdlStratifiedModelResultV1
gdl_stratified_model_admit_authored_impl_v1(
    Atom *source_program,
    const char *target_name,
    size_t target_arity,
    CettaGdlStratifiedModelAdmissionLimitsV1 limits) {
    if (!source_program)
        return (CettaGdlStratifiedModelResultV1){
            .kind = CETTA_GDL_STRATIFIED_MODEL_ENGINE_FAULT_V1,
        };

    GdlSourcePackageV1 package = {0};
    GdlSourceParseV1 package_kind = gdl_source_package_view_v1(
        source_program, NULL, NULL, NULL, &package);
    if (package_kind != GDL_SOURCE_PARSE_OK_V1)
        return (CettaGdlStratifiedModelResultV1){
            .kind = gdl_stratified_model_parse_kind_v1(package_kind),
        };

    Arena basis_arena;
    GdlSourceRawFormsV1 forms = {0};
    GdlSourceProfileV1 profile = {0};
    GdlSourceTargetSliceV1 target_slice = {0};
    char calculus_input_sha256[65];
    arena_init(&basis_arena);
    size_t parse_depth = limits.evaluation.max_logical_depth
        ? limits.evaluation.max_logical_depth
        : GDL_STRATIFIED_MODEL_DEFAULT_MAX_DEPTH_V1;
    GdlSourceParseV1 forms_kind = gdl_source_parse_forms_v1(
        &basis_arena, package.source_text, parse_depth, &forms);
    if (forms_kind != GDL_SOURCE_PARSE_OK_V1) {
        gdl_source_raw_forms_free_v1(&forms);
        arena_free(&basis_arena);
        return (CettaGdlStratifiedModelResultV1){
            .kind = gdl_stratified_model_parse_kind_v1(forms_kind),
        };
    }
    memcpy(
        calculus_input_sha256, package.calculus_input_sha256,
        sizeof(calculus_input_sha256));
    if (target_name) {
        GdlSourceParseV1 target_kind =
            gdl_source_select_target_dependency_v1(
                &forms, target_name, target_arity,
                limits.stratification.max_relations,
                parse_depth, &target_slice);
        if (target_kind != GDL_SOURCE_PARSE_OK_V1) {
            gdl_source_raw_forms_free_v1(&forms);
            arena_free(&basis_arena);
            return (CettaGdlStratifiedModelResultV1){
                .kind = gdl_stratified_model_parse_kind_v1(target_kind),
            };
        }
        if (!gdl_source_target_calculus_input_v1(
                package.calculus_input_sha256, &forms,
                target_name, target_arity,
                calculus_input_sha256)) {
            gdl_source_raw_forms_free_v1(&forms);
            arena_free(&basis_arena);
            return (CettaGdlStratifiedModelResultV1){
                .kind = CETTA_GDL_STRATIFIED_MODEL_ENGINE_FAULT_V1,
            };
        }
    }
    GdlSourceParseV1 profile_kind = gdl_source_parse_profile_v1(
        &basis_arena, package.profile_text, &profile);
    if (profile_kind != GDL_SOURCE_PARSE_OK_V1) {
        gdl_source_profile_free_v1(&profile);
        gdl_source_raw_forms_free_v1(&forms);
        arena_free(&basis_arena);
        return (CettaGdlStratifiedModelResultV1){
            .kind = gdl_stratified_model_parse_kind_v1(profile_kind),
        };
    }

    CettaGdlStratificationResultV1 stratification_result =
        cetta_gdl_stratification_construct_v1(
            &forms, limits.stratification);
    if (stratification_result.kind !=
        CETTA_GDL_STRATIFICATION_ESTABLISHED_V1) {
        gdl_source_profile_free_v1(&profile);
        gdl_source_raw_forms_free_v1(&forms);
        arena_free(&basis_arena);
        if (stratification_result.kind ==
            CETTA_GDL_STRATIFICATION_REFUTED_NEGATIVE_CYCLE_V1)
            return (CettaGdlStratifiedModelResultV1){
                .kind =
                    CETTA_GDL_STRATIFIED_MODEL_REFUTED_NEGATIVE_CYCLE_V1,
                .negative_cycle_obstruction =
                    stratification_result.analysis,
            };
        CettaGdlStratifiedModelKindV1 kind =
            stratification_result.kind ==
                    CETTA_GDL_STRATIFICATION_INCOMPLETE_V1
                ? CETTA_GDL_STRATIFIED_MODEL_INCOMPLETE_V1
                : stratification_result.kind ==
                          CETTA_GDL_STRATIFICATION_ENGINE_FAULT_V1
                    ? CETTA_GDL_STRATIFIED_MODEL_ENGINE_FAULT_V1
                    : CETTA_GDL_STRATIFIED_MODEL_OUTSIDE_FRAGMENT_V1;
        cetta_gdl_stratification_destroy_v1(
            stratification_result.analysis);
        return (CettaGdlStratifiedModelResultV1){.kind = kind};
    }

    CettaGdlFiniteHerbrandResultV1 herbrand_result =
        cetta_gdl_finite_herbrand_construct_v1(
            &profile, limits.herbrand);
    gdl_source_profile_free_v1(&profile);
    if (herbrand_result.kind !=
        CETTA_GDL_FINITE_HERBRAND_ESTABLISHED_V1) {
        CettaGdlStratifiedModelKindV1 kind =
            herbrand_result.kind ==
                    CETTA_GDL_FINITE_HERBRAND_INCOMPLETE_V1
                ? CETTA_GDL_STRATIFIED_MODEL_INCOMPLETE_V1
                : herbrand_result.kind ==
                          CETTA_GDL_FINITE_HERBRAND_ENGINE_FAULT_V1
                    ? CETTA_GDL_STRATIFIED_MODEL_ENGINE_FAULT_V1
                    : CETTA_GDL_STRATIFIED_MODEL_OUTSIDE_FRAGMENT_V1;
        cetta_gdl_finite_herbrand_destroy_v1(herbrand_result.domain);
        cetta_gdl_stratification_destroy_v1(
            stratification_result.analysis);
        gdl_source_raw_forms_free_v1(&forms);
        arena_free(&basis_arena);
        return (CettaGdlStratifiedModelResultV1){.kind = kind};
    }

    CettaGdlTypeOfNativeAdmissionV1 typing_result =
        target_name
            ? cetta_gdl_type_of_native_admit_authored_target_v1(
                source_program, target_name, target_arity,
                limits.typing)
            : cetta_gdl_type_of_native_admit_authored_source_v1(
                source_program, limits.typing);
    if (typing_result.kind != CETTA_GDL_TYPE_OF_NATIVE_ADMITTED_V1) {
        CettaGdlStratifiedModelKindV1 kind =
            typing_result.kind == CETTA_GDL_TYPE_OF_NATIVE_INCOMPLETE_V1
                ? CETTA_GDL_STRATIFIED_MODEL_INCOMPLETE_V1
                : typing_result.kind ==
                          CETTA_GDL_TYPE_OF_NATIVE_ENGINE_FAULT_V1
                    ? CETTA_GDL_STRATIFIED_MODEL_ENGINE_FAULT_V1
                    : CETTA_GDL_STRATIFIED_MODEL_OUTSIDE_FRAGMENT_V1;
        cetta_gdl_type_of_native_destroy_v1(typing_result.native);
        cetta_gdl_finite_herbrand_destroy_v1(herbrand_result.domain);
        cetta_gdl_stratification_destroy_v1(
            stratification_result.analysis);
        gdl_source_raw_forms_free_v1(&forms);
        arena_free(&basis_arena);
        return (CettaGdlStratifiedModelResultV1){.kind = kind};
    }

    CettaGdlRuleVariableSelectionV1 variable_selection = {0};
    if (!cetta_gdl_type_of_native_rule_variable_selection_v1(
            typing_result.native, &variable_selection) ||
        variable_selection.kind !=
            CETTA_GDL_RULE_VARIABLE_UNIQUE_GREATEST_V1) {
        CettaGdlStratifiedModelKindV1 kind =
            variable_selection.kind ==
                    CETTA_GDL_RULE_VARIABLE_SELECTION_INCOMPLETE_V1
                ? CETTA_GDL_STRATIFIED_MODEL_INCOMPLETE_V1
                : variable_selection.kind ==
                          CETTA_GDL_RULE_VARIABLE_NO_COMMON_GREATEST_V1
                    ? CETTA_GDL_STRATIFIED_MODEL_OUTSIDE_FRAGMENT_V1
                    : CETTA_GDL_STRATIFIED_MODEL_ENGINE_FAULT_V1;
        cetta_gdl_type_of_native_destroy_v1(typing_result.native);
        cetta_gdl_finite_herbrand_destroy_v1(herbrand_result.domain);
        cetta_gdl_stratification_destroy_v1(
            stratification_result.analysis);
        gdl_source_raw_forms_free_v1(&forms);
        arena_free(&basis_arena);
        return (CettaGdlStratifiedModelResultV1){.kind = kind};
    }

    CettaGdlStratifiedModelResultV1 result =
        gdl_stratified_model_construct_components_v1(
            &forms, typing_result.native, herbrand_result.domain,
            stratification_result.analysis, limits.evaluation);
    if (result.model) {
        result.model->owns_basis = true;
        result.model->basis_arena = basis_arena;
        memset(&basis_arena, 0, sizeof(basis_arena));
        result.model->owned_forms = forms;
        memset(&forms, 0, sizeof(forms));
        result.model->owned_typing = typing_result.native;
        result.model->owned_herbrand = herbrand_result.domain;
        result.model->owned_stratification =
            stratification_result.analysis;
        if (!gdl_stratified_model_attach_identity_v1(
                result.model, &package, calculus_input_sha256,
                target_name ? &target_slice : NULL,
                result.kind == CETTA_GDL_STRATIFIED_MODEL_ESTABLISHED_V1)) {
            cetta_gdl_stratified_model_destroy_v1(result.model);
            return (CettaGdlStratifiedModelResultV1){
                .kind = CETTA_GDL_STRATIFIED_MODEL_ENGINE_FAULT_V1,
            };
        }
        return result;
    }

    cetta_gdl_type_of_native_destroy_v1(typing_result.native);
    cetta_gdl_finite_herbrand_destroy_v1(herbrand_result.domain);
    cetta_gdl_stratification_destroy_v1(stratification_result.analysis);
    gdl_source_raw_forms_free_v1(&forms);
    arena_free(&basis_arena);
    return result;
}

CettaGdlStratifiedModelResultV1
cetta_gdl_stratified_model_admit_authored_source_v1(
    Atom *source_program,
    CettaGdlStratifiedModelAdmissionLimitsV1 limits) {
    return gdl_stratified_model_admit_authored_impl_v1(
        source_program, NULL, 0u, limits);
}

CettaGdlStratifiedModelResultV1
cetta_gdl_stratified_model_admit_authored_target_v1(
    Atom *source_program,
    const char *target_name,
    size_t target_arity,
    CettaGdlStratifiedModelAdmissionLimitsV1 limits) {
    if (!target_name || target_name[0] == '\0')
        return (CettaGdlStratifiedModelResultV1){
            .kind = CETTA_GDL_STRATIFIED_MODEL_ENGINE_FAULT_V1,
        };
    return gdl_stratified_model_admit_authored_impl_v1(
        source_program, target_name, target_arity, limits);
}

const CettaNikDirectAuthorityV1 *
cetta_gdl_stratified_model_authority_v1(void) {
    return &g_gdl_stratified_model_authority_v1;
}

bool cetta_gdl_stratified_model_token_v1(
    const CettaGdlStratifiedModelV1 *model,
    CettaNikDirectAuthorityTokenV1 *token_out) {
    if (!model || !token_out || !model->token_valid)
        return false;
    *token_out = model->token;
    return true;
}

bool cetta_gdl_stratified_model_token_is_current_v1(
    const CettaGdlStratifiedModelV1 *model,
    const CettaNikDirectAuthorityTokenV1 *token) {
    return model && token && model->token_valid &&
        cetta_nik_direct_authority_token_v1_equal(
            &model->token, token);
}

bool cetta_gdl_stratified_model_identity_v1(
    const CettaGdlStratifiedModelV1 *model,
    const char **source_sha256_out,
    const char **profile_sha256_out,
    const char **revision_out) {
    if (!model || !source_sha256_out || !profile_sha256_out ||
        !revision_out || model->source_sha256[0] == '\0' ||
        model->profile_sha256[0] == '\0' || model->revision[0] == '\0')
        return false;
    *source_sha256_out = model->source_sha256;
    *profile_sha256_out = model->profile_sha256;
    *revision_out = model->revision;
    return true;
}

bool cetta_gdl_stratified_model_target_slice_v1(
    const CettaGdlStratifiedModelV1 *model,
    const char **target_name_out,
    size_t *target_arity_out,
    size_t *source_forms_out,
    size_t *selected_forms_out,
    size_t *reachable_relations_out,
    size_t *external_relations_out) {
    if (!model || !model->target_name || !target_name_out ||
        !target_arity_out || !source_forms_out || !selected_forms_out ||
        !reachable_relations_out || !external_relations_out)
        return false;
    *target_name_out = model->target_name;
    *target_arity_out = model->target_arity;
    *source_forms_out = model->target_source_forms;
    *selected_forms_out = model->target_selected_forms;
    *reachable_relations_out = model->target_reachable_relations;
    *external_relations_out = model->target_external_relations;
    return true;
}

bool cetta_gdl_stratified_model_selection_v1(
    const CettaGdlStratifiedModelV1 *model,
    CettaNikImplementationSelectionV1 *selection_out,
    uint64_t *realization_identity_out) {
    if (!model || !selection_out || !realization_identity_out ||
        !model->token_valid)
        return false;
    *selection_out = model->selection;
    *realization_identity_out = model->selected_realization_identity;
    return true;
}

static bool gdl_stratified_atom_relation_index_v1(
    const CettaGdlStratificationV1 *stratification,
    Atom *literal,
    size_t *relation_index_out,
    size_t *stratum_out) {
    if (!stratification || !literal || !relation_index_out ||
        !stratum_out)
        return false;
    const char *name = NULL;
    size_t arity = 0u;
    if (literal->kind == ATOM_SYMBOL) {
        name = atom_name_cstr(literal);
    } else if (literal->kind == ATOM_EXPR && literal->expr.len > 0u &&
               literal->expr.elems[0] &&
               literal->expr.elems[0]->kind == ATOM_SYMBOL) {
        name = atom_name_cstr(literal->expr.elems[0]);
        arity = (size_t)literal->expr.len - 1u;
    }
    if (!name)
        return false;
    size_t relation_count =
        cetta_gdl_stratification_relation_count_v1(stratification);
    for (size_t index = 0u; index < relation_count; index++) {
        CettaGdlStratifiedRelationViewV1 relation = {0};
        if (cetta_gdl_stratification_relation_view_v1(
                stratification, index, &relation) &&
            relation.name && strcmp(relation.name, name) == 0 &&
            relation.arity == arity) {
            *relation_index_out = index;
            *stratum_out = relation.stratum;
            return true;
        }
    }
    return false;
}

static Atom *gdl_stratified_episode_occurrence_v1(
    Arena *arena, Atom *identity, size_t fact_ordinal) {
    if (!arena || !identity || fact_ordinal == 0u ||
        fact_ordinal > (size_t)INT64_MAX)
        return NULL;
    Atom **items = arena_alloc(arena, 3u * sizeof(*items));
    items[0] = atom_symbol(arena, "gdl:episode-occurrence");
    items[1] = atom_deep_copy(arena, identity);
    items[2] = atom_int(arena, (int64_t)fact_ordinal);
    return items[0] && items[1] && items[2]
        ? atom_expr(arena, items, 3u) : NULL;
}

static CettaGdlStratifiedEpisodeKindV1
gdl_stratified_episode_ground_kind_v1(
    const CettaGdlTypeOfNativeGroundV1 *ground) {
    if (!ground)
        return CETTA_GDL_STRATIFIED_EPISODE_ENGINE_FAULT_V1;
    if (ground->kind == CETTA_GDL_TYPE_OF_NATIVE_GROUND_STALE_V1)
        return CETTA_GDL_STRATIFIED_EPISODE_STALE_V1;
    if (ground->kind == CETTA_GDL_TYPE_OF_NATIVE_GROUND_ENGINE_FAULT_V1)
        return CETTA_GDL_STRATIFIED_EPISODE_ENGINE_FAULT_V1;
    if (ground->kind != CETTA_GDL_TYPE_OF_NATIVE_GROUND_OUTCOME_V1)
        return CETTA_GDL_STRATIFIED_EPISODE_ENGINE_FAULT_V1;
    if (ground->value.outcome == CETTA_NIK_OUTCOME_ESTABLISHED)
        return CETTA_GDL_STRATIFIED_EPISODE_ESTABLISHED_V1;
    if (ground->value.outcome == CETTA_NIK_OUTCOME_OUTSIDE_FRAGMENT)
        return CETTA_GDL_STRATIFIED_EPISODE_OUTSIDE_FRAGMENT_V1;
    if (ground->value.outcome == CETTA_NIK_OUTCOME_INCOMPLETE)
        return CETTA_GDL_STRATIFIED_EPISODE_INCOMPLETE_V1;
    /* This constructor currently returns no Refuted arm because it has no
     * checked-obstruction payload.  Treat an impossible such result as a
     * contract fault rather than weakening it to abstention. */
    return CETTA_GDL_STRATIFIED_EPISODE_ENGINE_FAULT_V1;
}

static CettaGdlStratifiedEpisodeKindV1
gdl_stratified_episode_build_kind_v1(GdlStratifiedBuildV1 status) {
    switch (status) {
    case GDL_STRATIFIED_BUILD_OK_V1:
        return CETTA_GDL_STRATIFIED_EPISODE_ESTABLISHED_V1;
    case GDL_STRATIFIED_BUILD_OUTSIDE_V1:
        return CETTA_GDL_STRATIFIED_EPISODE_OUTSIDE_FRAGMENT_V1;
    case GDL_STRATIFIED_BUILD_INCOMPLETE_V1:
        return CETTA_GDL_STRATIFIED_EPISODE_INCOMPLETE_V1;
    case GDL_STRATIFIED_BUILD_FAULT_V1:
    default:
        return CETTA_GDL_STRATIFIED_EPISODE_ENGINE_FAULT_V1;
    }
}

CettaGdlStratifiedEpisodeResultV1
cetta_gdl_stratified_model_admit_episode_v1(
    CettaGdlStratifiedModelV1 *source_model,
    const CettaNikDirectAuthorityTokenV1 *source_token,
    Atom *episode_identity,
    Atom *const *facts,
    size_t fact_count,
    CettaGdlStratifiedEpisodeLimitsV1 limits) {
    if (!source_model || !source_token || !episode_identity ||
        (fact_count != 0u && !facts))
        return (CettaGdlStratifiedEpisodeResultV1){
            .kind = CETTA_GDL_STRATIFIED_EPISODE_ENGINE_FAULT_V1,
        };
    if (!cetta_gdl_stratified_model_token_is_current_v1(
            source_model, source_token))
        return (CettaGdlStratifiedEpisodeResultV1){
            .kind = CETTA_GDL_STRATIFIED_EPISODE_STALE_V1,
        };
    if (!source_model->owns_basis || atom_has_vars(episode_identity))
        return (CettaGdlStratifiedEpisodeResultV1){
            .kind = CETTA_GDL_STRATIFIED_EPISODE_OUTSIDE_FRAGMENT_V1,
        };
    if (limits.max_facts == 0u)
        limits.max_facts = GDL_STRATIFIED_EPISODE_DEFAULT_MAX_FACTS_V1;
    if (fact_count > limits.max_facts || fact_count > (size_t)INT64_MAX)
        return (CettaGdlStratifiedEpisodeResultV1){
            .kind = CETTA_GDL_STRATIFIED_EPISODE_INCOMPLETE_V1,
        };
    for (size_t index = 0u; index < fact_count; index++) {
        if (!facts[index])
            return (CettaGdlStratifiedEpisodeResultV1){
                .kind = CETTA_GDL_STRATIFIED_EPISODE_ENGINE_FAULT_V1,
            };
        if (atom_has_vars(facts[index]))
            return (CettaGdlStratifiedEpisodeResultV1){
                .kind =
                    CETTA_GDL_STRATIFIED_EPISODE_OUTSIDE_FRAGMENT_V1,
            };
    }

    CettaGdlStratifiedEpisodeV1 *episode =
        cetta_malloc(sizeof(*episode));
    memset(episode, 0, sizeof(*episode));
    arena_init(&episode->arena);
    arena_set_runtime_kind(
        &episode->arena, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    episode->source_model = source_model;
    episode->source_token = *source_token;
    episode->identity = atom_deep_copy(
        &episode->arena, episode_identity);
    Arena scratch;
    arena_init(&scratch);
    arena_set_runtime_kind(&scratch, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    bool hashed = episode->identity && gdl_stratified_episode_digest_v1(
        source_model, &scratch, episode_identity, facts,
        fact_count, episode->digest);
    arena_free(&scratch);
    char revision_text[96];
    int revision_length = hashed
        ? snprintf(revision_text, sizeof(revision_text),
                   "gdl-stratified-episode-%s", episode->digest)
        : -1;
    episode->revision = revision_length > 0 &&
            (size_t)revision_length < sizeof(revision_text)
        ? arena_strdup(&episode->arena, revision_text)
        : NULL;
    if (!episode->identity || !episode->revision) {
        cetta_gdl_stratified_episode_destroy_v1(episode);
        return (CettaGdlStratifiedEpisodeResultV1){
            .kind = CETTA_GDL_STRATIFIED_EPISODE_ENGINE_FAULT_V1,
        };
    }

    GdlStratifiedBuildV1 built =
        gdl_stratified_model_compile_components_v1(
            &source_model->owned_forms, source_model->typing,
            source_model->herbrand, source_model->stratification,
            limits.evaluation, &episode->model);
    CettaGdlStratifiedEpisodeKindV1 kind =
        gdl_stratified_episode_build_kind_v1(built);
    if (built == GDL_STRATIFIED_BUILD_OK_V1) {
        CettaNikDirectAuthorityTokenV1 typing_token = {0};
        if (!cetta_gdl_type_of_native_token_v1(
                source_model->typing, &typing_token) ||
            !cetta_gdl_type_of_native_token_is_current_v1(
                source_model->typing, &typing_token)) {
            kind = CETTA_GDL_STRATIFIED_EPISODE_ENGINE_FAULT_V1;
        } else {
            for (size_t fact_index = 0u;
                 fact_index < fact_count &&
                 kind == CETTA_GDL_STRATIFIED_EPISODE_ESTABLISHED_V1;
                 fact_index++) {
                Atom *occurrence = gdl_stratified_episode_occurrence_v1(
                    &episode->model->arena, episode->identity,
                    fact_index + 1u);
                CettaGdlTypeOfNativeGroundV1 ground =
                    occurrence
                    ? cetta_gdl_type_of_native_construct_ground_literal_v1(
                        source_model->typing, &typing_token,
                        &episode->model->arena, occurrence,
                        facts[fact_index],
                        limits.max_typing_proofs_per_fact)
                    : (CettaGdlTypeOfNativeGroundV1){
                        .kind =
                            CETTA_GDL_TYPE_OF_NATIVE_GROUND_ENGINE_FAULT_V1,
                    };
                kind = gdl_stratified_episode_ground_kind_v1(&ground);
                if (kind != CETTA_GDL_STRATIFIED_EPISODE_ESTABLISHED_V1)
                    break;
                Atom *head = atom_deep_copy(
                    &episode->model->arena, facts[fact_index]);
                size_t relation_index = SIZE_MAX;
                size_t stratum = 0u;
                if (!head || !gdl_stratified_atom_relation_index_v1(
                        source_model->stratification, head,
                        &relation_index, &stratum)) {
                    kind =
                        CETTA_GDL_STRATIFIED_EPISODE_OUTSIDE_FRAGMENT_V1;
                    break;
                }
                GdlStratifiedGroundRuleV1 fact = {0};
                fact.origin =
                    CETTA_GDL_STRATIFIED_PROOF_TYPED_EPISODE_FACT_V1;
                fact.episode_occurrence = occurrence;
                fact.head = head;
                fact.head_relation = relation_index;
                fact.head_stratum = stratum;
                fact.source_head =
                    (CettaGdlTypeOfNativeSourceJudgmentV1){
                        .occurrence = occurrence,
                        .term = head,
                        .type = ground.type,
                        .literal_proofs = ground.proofs,
                        .literal_proof_count = ground.proof_count,
                    };
                bool new_support = false;
                built = gdl_stratified_activate_rule_v1(
                    episode->model, &fact, &new_support);
                kind = gdl_stratified_episode_build_kind_v1(built);
                if (kind != CETTA_GDL_STRATIFIED_EPISODE_ESTABLISHED_V1)
                    break;
                episode->stats.authored_facts++;
                episode->stats.typing_proof_occurrences +=
                    ground.proof_count;
                episode->stats.seeded_support_nodes += new_support ? 1u : 0u;
                episode->stats.seeded_proof_edges += ground.proof_count;
            }
        }
    }
    if (kind == CETTA_GDL_STRATIFIED_EPISODE_ESTABLISHED_V1) {
        built = gdl_stratified_evaluate_v1(episode->model);
        kind = gdl_stratified_episode_build_kind_v1(built);
    }
    if (kind == CETTA_GDL_STRATIFIED_EPISODE_ESTABLISHED_V1) {
        episode->token_valid =
            cetta_nik_direct_authority_v1_token_from_sha256(
                &g_gdl_stratified_model_authority_v1,
                episode->digest, 2u, &episode->token);
        if (!episode->token_valid)
            kind = CETTA_GDL_STRATIFIED_EPISODE_ENGINE_FAULT_V1;
    }
    if (kind == CETTA_GDL_STRATIFIED_EPISODE_OUTSIDE_FRAGMENT_V1 ||
        kind == CETTA_GDL_STRATIFIED_EPISODE_STALE_V1 ||
        kind == CETTA_GDL_STRATIFIED_EPISODE_ENGINE_FAULT_V1) {
        cetta_gdl_stratified_episode_destroy_v1(episode);
        episode = NULL;
    }
    return (CettaGdlStratifiedEpisodeResultV1){
        .kind = kind,
        .episode = episode,
    };
}

void cetta_gdl_stratified_episode_destroy_v1(
    CettaGdlStratifiedEpisodeV1 *episode) {
    if (!episode)
        return;
    cetta_gdl_stratified_model_destroy_v1(episode->model);
    arena_free(&episode->arena);
    free(episode);
}

bool cetta_gdl_stratified_episode_token_v1(
    const CettaGdlStratifiedEpisodeV1 *episode,
    CettaNikDirectAuthorityTokenV1 *token_out) {
    if (!episode || !token_out || !episode->token_valid)
        return false;
    *token_out = episode->token;
    return true;
}

bool cetta_gdl_stratified_episode_token_is_current_v1(
    const CettaGdlStratifiedEpisodeV1 *episode,
    const CettaNikDirectAuthorityTokenV1 *token) {
    return episode && episode->source_model && token &&
        episode->token_valid &&
        cetta_gdl_stratified_model_token_is_current_v1(
            episode->source_model, &episode->source_token) &&
        cetta_nik_direct_authority_token_v1_equal(
            &episode->token, token);
}

bool cetta_gdl_stratified_episode_identity_v1(
    const CettaGdlStratifiedEpisodeV1 *episode,
    const char **digest_out,
    const char **revision_out) {
    if (!episode || !digest_out || !revision_out ||
        episode->digest[0] == '\0' || !episode->revision)
        return false;
    *digest_out = episode->digest;
    *revision_out = episode->revision;
    return true;
}

const CettaGdlStratifiedModelV1 *
cetta_gdl_stratified_episode_model_v1(
    const CettaGdlStratifiedEpisodeV1 *episode) {
    return episode ? episode->model : NULL;
}

bool cetta_gdl_stratified_episode_stats_v1(
    const CettaGdlStratifiedEpisodeV1 *episode,
    CettaGdlStratifiedEpisodeStatsV1 *stats_out) {
    if (!episode || !stats_out)
        return false;
    *stats_out = episode->stats;
    return true;
}

void cetta_gdl_stratified_model_destroy_v1(
    CettaGdlStratifiedModelV1 *model) {
    if (!model)
        return;
    for (size_t index = 0u; index < model->support_count; index++)
        free(model->supports[index].proof_edge_indices);
    for (size_t index = 0u; index < model->relation_count; index++)
        free(model->relation_supports[index].support_indices);
    for (size_t index = 0u; index < model->template_count; index++)
        gdl_stratified_template_branch_free_v1(
            &model->templates[index].branch);
    free(model->completed_relation_support_counts);
    free(model->relation_supports);
    free(model->grounding_slots);
    free(model->groundings);
    free(model->edges);
    free(model->support_slots);
    free(model->supports);
    free(model->templates);
    arena_free(&model->arena);
    if (model->owns_basis) {
        cetta_gdl_stratification_destroy_v1(
            model->owned_stratification);
        cetta_gdl_finite_herbrand_destroy_v1(model->owned_herbrand);
        cetta_gdl_type_of_native_destroy_v1(model->owned_typing);
        gdl_source_raw_forms_free_v1(&model->owned_forms);
        arena_free(&model->basis_arena);
    }
    free(model);
}

size_t cetta_gdl_stratified_model_support_count_v1(
    const CettaGdlStratifiedModelV1 *model) {
    return model ? model->support_count : 0u;
}

size_t cetta_gdl_stratified_model_proof_edge_count_v1(
    const CettaGdlStratifiedModelV1 *model) {
    return model ? model->edge_count : 0u;
}

bool cetta_gdl_stratified_model_support_view_v1(
    const CettaGdlStratifiedModelV1 *model,
    size_t index,
    CettaGdlStratifiedSupportViewV1 *view_out) {
    if (!model || !view_out || index >= model->support_count)
        return false;
    const GdlStratifiedSupportV1 *support = &model->supports[index];
    *view_out = (CettaGdlStratifiedSupportViewV1){
        .literal = support->literal,
        .relation_index = support->relation_index,
        .stratum = support->stratum,
        .proof_edge_indices = support->proof_edge_indices,
        .proof_edge_count = support->proof_edge_count,
    };
    return true;
}

bool cetta_gdl_stratified_model_proof_edge_view_v1(
    const CettaGdlStratifiedModelV1 *model,
    size_t index,
    CettaGdlStratifiedProofEdgeViewV1 *view_out) {
    if (!model || !view_out || index >= model->edge_count)
        return false;
    *view_out = model->edges[index].view;
    return true;
}

bool cetta_gdl_stratified_model_find_support_v1(
    const CettaGdlStratifiedModelV1 *model,
    Atom *literal,
    size_t *index_out) {
    if (!model || !literal || !index_out)
        return false;
    for (size_t index = 0u; index < model->support_count; index++)
        if (atom_eq(model->supports[index].literal, literal)) {
            *index_out = index;
            return true;
        }
    return false;
}

bool cetta_gdl_stratified_model_stats_v1(
    const CettaGdlStratifiedModelV1 *model,
    CettaGdlStratifiedModelStatsV1 *stats_out) {
    if (!model || !stats_out)
        return false;
    *stats_out = model->stats;
    return true;
}

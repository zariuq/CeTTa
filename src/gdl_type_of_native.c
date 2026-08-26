#include "gdl_type_of_native.h"

#include "gdl_source_presentation.h"
#include "gslt_u32_index_v1.h"
#include "native_sha256.h"
#include "symbol.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    GDL_TYPE_OF_NATIVE_DEFAULT_SOURCE_NODES = 1000000u,
    GDL_TYPE_OF_NATIVE_DEFAULT_PROOF_NODES = 4000000u,
    GDL_TYPE_OF_NATIVE_DEFAULT_DERIVATION_DEPTH = 4096u,
};

typedef struct {
    Atom **items;
    size_t count;
    size_t capacity;
} GdlNativeProofBagV1;

typedef struct {
    SymbolId occurrence_key;
    size_t form_ordinal;
    const char *path;
    Atom *occurrence;
    Atom *term;
    Atom *children;
    Atom *form;
    Atom *source_node_proof;
    Atom *source_children_proof;
    Atom *source_form_proof;
    Atom *type;
    const char *type_name;
    GdlNativeProofBagV1 type_proofs;
    GdlNativeProofBagV1 literal_proofs;
    uint8_t derivation_state;
} GdlNativeSourceNodeV1;

typedef struct {
    Atom *name;
    Atom *argument_types;
    Atom *result_type;
    Atom *proof;
} GdlNativeSignatureV1;

typedef struct {
    Atom *form;
    Atom *name;
    Atom *type;
    Atom *proof;
} GdlNativeVariableBindingV1;

typedef struct {
    Atom *subtype;
    Atom *supertype;
    Atom *proof;
} GdlNativeSubtypeEdgeV1;

struct CettaGdlTypeOfNativeV1 {
    Arena arena;
    AtomDeepCopySession *copy;
    char source_sha256[65];
    char profile_sha256[65];
    char calculus_input_sha256[65];
    char *revision;
    char *target_name;
    size_t target_arity;
    size_t target_source_forms;
    size_t target_selected_forms;
    size_t target_reachable_relations;
    size_t target_external_relations;
    CettaNikDirectAuthorityTokenV1 token;
    CettaGdlTypeOfNativeLimitsV1 limits;
    CettaGdlTypeOfNativeStatsV1 stats;
    Atom *bool_type;
    GdlNativeSourceNodeV1 *nodes;
    size_t node_count;
    size_t node_capacity;
    CettaGsltU32IndexV1 node_index;
    GdlNativeSignatureV1 *signatures;
    size_t signature_count;
    size_t signature_capacity;
    GdlNativeVariableBindingV1 *variables;
    size_t variable_count;
    size_t variable_capacity;
    GdlNativeSubtypeEdgeV1 *subtypes;
    size_t subtype_count;
    size_t subtype_capacity;
    CettaGsltU32IndexV1 rule_ids;
    CettaGdlRuleVariableSelectionV1 rule_variable_selection;
    bool resource_exhausted;
};

static const CettaNikDirectAuthorityV1 g_gdl_type_of_native_authority_v1 = {
    .alias = "gdl-type-of-native-v1",
    .system_id = "gdl.type-of.native.v1",
    .authority_identity = UINT64_C(0x67646c2e74797065),
    .realization_identity = UINT64_C(0x67646c2e6e747631),
    .authority_revision = 2u,
    .realization_abi = 2u,
};

/* Language-owned capabilities for one exact GDL typing request.  Their
 * numeric identities are stable only within this family; NIK compares them
 * opaquely and never assigns a global mode rank. */
static const CettaNikImplementationCapabilityIdV1
    g_gdl_type_of_native_capabilities_v1[] = {
        UINT64_C(0x67646c2e65786163), /* exact ordered proof fibre */
        UINT64_C(0x67646c2e6e617469), /* native proof construction */
        UINT64_C(0x67646c2e6e6f7270), /* no certificate replay */
    };

typedef enum {
    GDL_NATIVE_BUILD_OK_V1 = 0,
    GDL_NATIVE_BUILD_OUTSIDE_V1,
    GDL_NATIVE_BUILD_RESOURCE_V1,
} GdlNativeBuildV1;

static GdlNativeBuildV1 gdl_native_parse_status_v1(
    GdlSourceParseV1 status) {
    switch (status) {
    case GDL_SOURCE_PARSE_OK_V1:
        return GDL_NATIVE_BUILD_OK_V1;
    case GDL_SOURCE_PARSE_INCOMPLETE_V1:
        return GDL_NATIVE_BUILD_RESOURCE_V1;
    case GDL_SOURCE_PARSE_OUTSIDE_FRAGMENT_V1:
    default:
        return GDL_NATIVE_BUILD_OUTSIDE_V1;
    }
}

typedef struct {
    const char *id;
    size_t formals;
    size_t premises;
    const char *const *premise_heads;
    const char *conclusion_head;
} GdlNativeCoreRuleV1;

static const char *const g_accepts_step_premises[] = {
    "gdl:subtype", "gdl:accepts",
};
static const char *const g_arguments_type_cons_premises[] = {
    "type:of", "gdl:accepts", "gdl:arguments-type",
};
static const char *const g_arguments_typed_cons_premises[] = {
    "type:of", "gdl:arguments-typed",
};
static const char *const g_type_application_premises[] = {
    "gdl:source-node", "gdl:source-children", "gdl:signature",
    "gdl:arguments-type",
};
static const char *const g_type_variable_premises[] = {
    "gdl:source-node", "gdl:source-form", "gdl:variable-type",
};
static const char *const g_type_not_premises[] = {
    "gdl:source-node", "gdl:source-children", "type:of", "gdl:accepts",
};
static const char *const g_type_or_premises[] = {
    "gdl:source-node", "gdl:source-children", "gdl:arguments-type",
    "gdl:all-type",
};
static const char *const g_all_type_cons_premises[] = {
    "gdl:all-type",
};
static const char *const g_type_distinct_premises[] = {
    "gdl:source-node", "gdl:source-children", "gdl:arguments-typed",
};
static const char *const g_literal_premises[] = {
    "type:of", "gdl:accepts",
};

static const GdlNativeCoreRuleV1 g_core_rules[] = {
    {"gdl:accepts-refl", 1u, 0u, NULL, "gdl:accepts"},
    {"gdl:accepts-step", 3u, 2u, g_accepts_step_premises,
     "gdl:accepts"},
    {"gdl:arguments-type-nil", 0u, 0u, NULL,
     "gdl:arguments-type"},
    {"gdl:arguments-type-cons", 7u, 3u,
     g_arguments_type_cons_premises, "gdl:arguments-type"},
    {"gdl:arguments-typed-nil", 0u, 0u, NULL,
     "gdl:arguments-typed"},
    {"gdl:arguments-typed-cons", 5u, 2u,
     g_arguments_typed_cons_premises, "gdl:arguments-typed"},
    {"gdl:type-application", 6u, 4u, g_type_application_premises,
     "type:of"},
    {"gdl:type-variable", 4u, 3u, g_type_variable_premises, "type:of"},
    {"gdl:type-not", 4u, 4u, g_type_not_premises, "type:of"},
    {"gdl:type-or", 6u, 4u, g_type_or_premises, "type:of"},
    {"gdl:all-type-nil", 1u, 0u, NULL, "gdl:all-type"},
    {"gdl:all-type-cons", 2u, 1u, g_all_type_cons_premises,
     "gdl:all-type"},
    {"gdl:type-distinct", 5u, 3u, g_type_distinct_premises, "type:of"},
    {"gdl:literal", 3u, 2u, g_literal_premises, "gdl:literal"},
};

static bool gdl_native_expr_named(
    const Atom *atom, const char *name, CettaExprLen length) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len == length &&
           atom_is_symbol(atom->expr.elems[0], name);
}

static bool gdl_native_expr_head(
    const Atom *atom, const char *name, CettaExprLen minimum_length) {
    return atom && atom->kind == ATOM_EXPR &&
           atom->expr.len >= minimum_length &&
           atom_is_symbol(atom->expr.elems[0], name);
}

static bool gdl_native_string(const Atom *atom, const char **text_out) {
    if (!atom || !text_out || atom->kind != ATOM_GROUNDED ||
        atom->ground.gkind != GV_STRING || !atom->ground.sval)
        return false;
    *text_out = atom->ground.sval;
    return true;
}

static bool gdl_native_nonnegative_size(
    const Atom *atom, size_t *value_out) {
    if (!atom || !value_out || atom->kind != ATOM_GROUNDED ||
        atom->ground.gkind != GV_INT || atom->ground.ival < 0 ||
        (uint64_t)atom->ground.ival > SIZE_MAX)
        return false;
    *value_out = (size_t)atom->ground.ival;
    return true;
}

static bool gdl_native_hex_digest(const char *text) {
    size_t index;
    if (!text || strlen(text) != 64u)
        return false;
    for (index = 0u; index < 64u; index++)
        if (!((text[index] >= '0' && text[index] <= '9') ||
              (text[index] >= 'a' && text[index] <= 'f')))
            return false;
    return true;
}

static bool gdl_native_reserve(
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

static bool gdl_native_wire_list_count(Atom *list, size_t *count_out) {
    size_t count = 0u;
    Atom *cursor = list;
    if (!count_out)
        return false;
    while (!atom_is_symbol(cursor, "LNil")) {
        if (!gdl_native_expr_named(cursor, "LCons", 3u) ||
            count == SIZE_MAX)
            return false;
        count++;
        cursor = cursor->expr.elems[2];
    }
    *count_out = count;
    return true;
}

static bool gdl_native_wire_list_item(
    Atom *list, size_t index, Atom **item_out) {
    Atom *cursor = list;
    size_t position = 0u;
    if (!item_out)
        return false;
    while (!atom_is_symbol(cursor, "LNil")) {
        if (!gdl_native_expr_named(cursor, "LCons", 3u))
            return false;
        if (position == index) {
            *item_out = cursor->expr.elems[1];
            return true;
        }
        position++;
        cursor = cursor->expr.elems[2];
    }
    return false;
}

static bool gdl_native_papp(
    Atom *pattern, const char **head_out, Atom **arguments_out) {
    const char *head;
    if (!gdl_native_expr_named(pattern, "PApp", 3u) ||
        !gdl_native_string(pattern->expr.elems[1], &head) || !*head ||
        !gdl_native_wire_list_count(pattern->expr.elems[2], &(size_t){0}))
        return false;
    if (head_out)
        *head_out = head;
    if (arguments_out)
        *arguments_out = pattern->expr.elems[2];
    return true;
}

static bool gdl_native_papp_arity(
    Atom *pattern, const char *expected_head, size_t expected_arity,
    Atom **arguments_out) {
    const char *head;
    Atom *arguments;
    size_t arity;
    if (!gdl_native_papp(pattern, &head, &arguments) ||
        strcmp(head, expected_head) != 0 ||
        !gdl_native_wire_list_count(arguments, &arity) ||
        arity != expected_arity)
        return false;
    if (arguments_out)
        *arguments_out = arguments;
    return true;
}

static bool gdl_native_zero_papp_key(
    Atom *pattern, const char *prefix, SymbolId *key_out,
    const char **head_out) {
    const char *head;
    Atom *arguments;
    size_t arity;
    size_t prefix_len = strlen(prefix);
    if (!key_out || !gdl_native_papp(pattern, &head, &arguments) ||
        strncmp(head, prefix, prefix_len) != 0 || head[prefix_len] == '\0' ||
        !gdl_native_wire_list_count(arguments, &arity) || arity != 0u)
        return false;
    *key_out = symbol_intern_cstr(g_symbols, head);
    if (*key_out == SYMBOL_ID_NONE)
        return false;
    if (head_out)
        *head_out = head;
    return true;
}

static Atom *gdl_native_copy(
    CettaGdlTypeOfNativeV1 *native, Atom *source) {
    return native && native->copy && source
        ? atom_deep_copy_session_copy(native->copy, source)
        : NULL;
}

static Atom *gdl_native_wire_list(
    Arena *arena, const char *nil, const char *cons,
    Atom *const *items, size_t count) {
    Atom *result = atom_symbol(arena, nil);
    size_t index = count;
    while (result && index > 0u) {
        Atom *parts[3];
        index--;
        parts[0] = atom_symbol(arena, cons);
        parts[1] = items[index];
        parts[2] = result;
        result = atom_expr(arena, parts, 3u);
    }
    return result;
}

static Atom *gdl_native_proof(
    CettaGdlTypeOfNativeV1 *native, const char *rule_id,
    Atom *const *arguments, size_t argument_count,
    Atom *const *premises, size_t premise_count) {
    Atom *argument_list;
    Atom *premise_list;
    Atom *inst_parts[3];
    Atom *inst;
    Atom *proof_parts[3];
    if (!native || !rule_id || !*rule_id ||
        native->stats.constructed_proof_nodes >= native->limits.max_proof_nodes) {
        if (native)
            native->resource_exhausted = true;
        return NULL;
    }
    argument_list = gdl_native_wire_list(
        &native->arena, "LNil", "LCons", arguments, argument_count);
    premise_list = gdl_native_wire_list(
        &native->arena, "PrNil", "PrCons", premises, premise_count);
    if (!argument_list || !premise_list)
        return NULL;
    inst_parts[0] = atom_symbol(&native->arena, "GRuleInst");
    inst_parts[1] = atom_string(&native->arena, rule_id);
    inst_parts[2] = argument_list;
    inst = atom_expr(&native->arena, inst_parts, 3u);
    proof_parts[0] = atom_symbol(&native->arena, "GProof");
    proof_parts[1] = inst;
    proof_parts[2] = premise_list;
    if (!inst || !proof_parts[0] || !proof_parts[1] || !proof_parts[2])
        return NULL;
    native->stats.constructed_proof_nodes++;
    return atom_expr(&native->arena, proof_parts, 3u);
}

static bool gdl_native_proof_bag_append(
    GdlNativeProofBagV1 *bag, Atom *proof) {
    if (!bag || !proof || !gdl_native_reserve(
            (void **)&bag->items, &bag->capacity, bag->count + 1u,
            sizeof(*bag->items)))
        return false;
    bag->items[bag->count++] = proof;
    return true;
}

static void gdl_native_proof_bag_free(GdlNativeProofBagV1 *bag) {
    if (!bag)
        return;
    free(bag->items);
    memset(bag, 0, sizeof(*bag));
}

static bool gdl_native_rule_id_insert(
    CettaGdlTypeOfNativeV1 *native, const char *rule_id) {
    SymbolId key;
    if (!native || !rule_id || !*rule_id)
        return false;
    key = symbol_intern_cstr(g_symbols, rule_id);
    return key != SYMBOL_ID_NONE &&
        cetta_gslt_u32_index_insert_unique_v1(
            &native->rule_ids, key, (uint32_t)native->rule_ids.len) ==
        CETTA_GSLT_U32_INDEX_INSERTED_V1;
}

static bool gdl_native_rule_shape(
    Atom *rule, const GdlNativeCoreRuleV1 *expected) {
    const char *rule_id;
    Atom *formals;
    Atom *premises;
    Atom *conclusion;
    const char *conclusion_head;
    size_t formal_count;
    size_t premise_count;
    size_t index;
    if (!rule || !expected || !gdl_native_expr_named(rule, "GRuleV1", 6u) ||
        !gdl_native_string(rule->expr.elems[1], &rule_id) ||
        strcmp(rule_id, expected->id) != 0 ||
        !atom_is_symbol(rule->expr.elems[5], "LNil"))
        return false;
    formals = rule->expr.elems[2];
    premises = rule->expr.elems[3];
    conclusion = rule->expr.elems[4];
    if (!gdl_native_wire_list_count(formals, &formal_count) ||
        formal_count != expected->formals ||
        !gdl_native_wire_list_count(premises, &premise_count) ||
        premise_count != expected->premises ||
        !gdl_native_papp(conclusion, &conclusion_head, NULL) ||
        strcmp(conclusion_head, expected->conclusion_head) != 0)
        return false;
    for (index = 0u; index < premise_count; index++) {
        Atom *premise;
        const char *premise_head;
        if (!gdl_native_wire_list_item(premises, index, &premise) ||
            !gdl_native_papp(premise, &premise_head, NULL) ||
            strcmp(premise_head, expected->premise_heads[index]) != 0)
            return false;
    }
    return true;
}

static bool gdl_native_rule_fact(
    Atom *rule, const char **rule_id_out, Atom **conclusion_out) {
    const char *rule_id;
    size_t formals;
    size_t premises;
    if (!rule_id_out || !conclusion_out ||
        !gdl_native_expr_named(rule, "GRuleV1", 6u) ||
        !gdl_native_string(rule->expr.elems[1], &rule_id) || !*rule_id ||
        !gdl_native_wire_list_count(rule->expr.elems[2], &formals) ||
        formals != 0u ||
        !gdl_native_wire_list_count(rule->expr.elems[3], &premises) ||
        premises != 0u || !atom_is_symbol(rule->expr.elems[5], "LNil"))
        return false;
    *rule_id_out = rule_id;
    *conclusion_out = rule->expr.elems[4];
    return true;
}

static bool gdl_native_rule_suffix_matches(
    const char *rule_id, const char *prefix, const char *suffix) {
    size_t prefix_len;
    if (!rule_id || !prefix || !suffix)
        return false;
    prefix_len = strlen(prefix);
    return strncmp(rule_id, prefix, prefix_len) == 0 &&
           strcmp(rule_id + prefix_len, suffix) == 0;
}

static GdlNativeSourceNodeV1 *gdl_native_node_get_or_add(
    CettaGdlTypeOfNativeV1 *native, Atom *occurrence,
    const char **occurrence_head_out) {
    SymbolId key;
    const char *head;
    uint32_t index;
    GdlNativeSourceNodeV1 *node;
    if (!native || !occurrence ||
        !gdl_native_zero_papp_key(
            occurrence, "gdl:occurrence:", &key, &head))
        return NULL;
    if (cetta_gslt_u32_index_find_v1(&native->node_index, key, &index)) {
        if ((size_t)index >= native->node_count)
            return NULL;
        if (occurrence_head_out)
            *occurrence_head_out = head;
        return &native->nodes[index];
    }
    if (native->node_count >= native->limits.max_source_nodes ||
        native->node_count >= UINT32_MAX) {
        native->resource_exhausted = true;
        return NULL;
    }
    if (!gdl_native_reserve(
            (void **)&native->nodes, &native->node_capacity,
            native->node_count + 1u, sizeof(*native->nodes))) {
        native->resource_exhausted = true;
        return NULL;
    }
    index = (uint32_t)native->node_count;
    node = &native->nodes[native->node_count++];
    memset(node, 0, sizeof(*node));
    node->occurrence_key = key;
    node->occurrence = gdl_native_copy(native, occurrence);
    if (!node->occurrence ||
        cetta_gslt_u32_index_insert_unique_v1(
            &native->node_index, key, index) !=
            CETTA_GSLT_U32_INDEX_INSERTED_V1) {
        native->node_count--;
        return NULL;
    }
    if (occurrence_head_out)
        *occurrence_head_out = head;
    return node;
}

static GdlNativeBuildV1 gdl_native_add_source_fact(
    CettaGdlTypeOfNativeV1 *native, const char *rule_id,
    Atom *conclusion, const char *judgment_head) {
    Atom *arguments;
    Atom *occurrence;
    Atom *value;
    const char *occurrence_head;
    GdlNativeSourceNodeV1 *node;
    Atom **value_slot;
    Atom **proof_slot;
    const char *prefix;
    if (!gdl_native_papp_arity(
            conclusion, judgment_head, 2u, &arguments) ||
        !gdl_native_wire_list_item(arguments, 0u, &occurrence) ||
        !gdl_native_wire_list_item(arguments, 1u, &value))
        return GDL_NATIVE_BUILD_OUTSIDE_V1;
    node = gdl_native_node_get_or_add(
        native, occurrence, &occurrence_head);
    if (!node)
        return native->resource_exhausted
            ? GDL_NATIVE_BUILD_RESOURCE_V1
            : GDL_NATIVE_BUILD_OUTSIDE_V1;
    if (strcmp(judgment_head, "gdl:source-node") == 0) {
        prefix = "gdl:source-node:";
        value_slot = &node->term;
        proof_slot = &node->source_node_proof;
    } else if (strcmp(judgment_head, "gdl:source-children") == 0) {
        prefix = "gdl:source-children:";
        value_slot = &node->children;
        proof_slot = &node->source_children_proof;
    } else if (strcmp(judgment_head, "gdl:source-form") == 0) {
        prefix = "gdl:source-form:";
        value_slot = &node->form;
        proof_slot = &node->source_form_proof;
    } else {
        return GDL_NATIVE_BUILD_OUTSIDE_V1;
    }
    if (*value_slot || *proof_slot ||
        !gdl_native_rule_suffix_matches(rule_id, prefix, occurrence_head))
        return GDL_NATIVE_BUILD_OUTSIDE_V1;
    *value_slot = gdl_native_copy(native, value);
    *proof_slot = gdl_native_proof(native, rule_id, NULL, 0u, NULL, 0u);
    if (!*value_slot || !*proof_slot)
        return native->resource_exhausted
            ? GDL_NATIVE_BUILD_RESOURCE_V1
            : GDL_NATIVE_BUILD_OUTSIDE_V1;
    return GDL_NATIVE_BUILD_OK_V1;
}

static GdlNativeBuildV1 gdl_native_add_signature_fact(
    CettaGdlTypeOfNativeV1 *native, const char *rule_id,
    Atom *conclusion) {
    static const char *const prefixes[] = {
        "gdl:signature-authored:",
        "gdl:signature-structural:",
        "gdl:signature-extension:",
    };
    Atom *arguments;
    GdlNativeSignatureV1 *signature;
    size_t index;
    bool prefix_ok = false;
    if (!gdl_native_papp_arity(
            conclusion, "gdl:signature", 3u, &arguments))
        return GDL_NATIVE_BUILD_OUTSIDE_V1;
    for (index = 0u; index < sizeof(prefixes) / sizeof(prefixes[0]); index++)
        if (strncmp(rule_id, prefixes[index], strlen(prefixes[index])) == 0 &&
            rule_id[strlen(prefixes[index])] != '\0') {
            prefix_ok = true;
            break;
        }
    if (!prefix_ok)
        return GDL_NATIVE_BUILD_OUTSIDE_V1;
    if (!gdl_native_reserve(
            (void **)&native->signatures, &native->signature_capacity,
            native->signature_count + 1u, sizeof(*native->signatures))) {
        native->resource_exhausted = true;
        return GDL_NATIVE_BUILD_RESOURCE_V1;
    }
    signature = &native->signatures[native->signature_count++];
    memset(signature, 0, sizeof(*signature));
    if (!gdl_native_wire_list_item(arguments, 0u, &signature->name) ||
        !gdl_native_wire_list_item(arguments, 1u,
                                   &signature->argument_types) ||
        !gdl_native_wire_list_item(arguments, 2u,
                                   &signature->result_type))
        return GDL_NATIVE_BUILD_OUTSIDE_V1;
    signature->name = gdl_native_copy(native, signature->name);
    signature->argument_types =
        gdl_native_copy(native, signature->argument_types);
    signature->result_type = gdl_native_copy(native, signature->result_type);
    signature->proof = gdl_native_proof(
        native, rule_id, NULL, 0u, NULL, 0u);
    if (!signature->name || !signature->argument_types ||
        !signature->result_type || !signature->proof)
        return native->resource_exhausted
            ? GDL_NATIVE_BUILD_RESOURCE_V1
            : GDL_NATIVE_BUILD_OUTSIDE_V1;
    return GDL_NATIVE_BUILD_OK_V1;
}

static GdlNativeBuildV1 gdl_native_add_variable_fact(
    CettaGdlTypeOfNativeV1 *native, const char *rule_id,
    Atom *conclusion) {
    const char *prefix = "gdl:variable-type:";
    Atom *arguments;
    GdlNativeVariableBindingV1 *binding;
    size_t index;
    if (strncmp(rule_id, prefix, strlen(prefix)) != 0 ||
        rule_id[strlen(prefix)] == '\0' ||
        !gdl_native_papp_arity(
            conclusion, "gdl:variable-type", 3u, &arguments))
        return GDL_NATIVE_BUILD_OUTSIDE_V1;
    if (!gdl_native_reserve(
            (void **)&native->variables, &native->variable_capacity,
            native->variable_count + 1u, sizeof(*native->variables)))
        return GDL_NATIVE_BUILD_RESOURCE_V1;
    binding = &native->variables[native->variable_count];
    memset(binding, 0, sizeof(*binding));
    if (!gdl_native_wire_list_item(arguments, 0u, &binding->form) ||
        !gdl_native_wire_list_item(arguments, 1u, &binding->name) ||
        !gdl_native_wire_list_item(arguments, 2u, &binding->type))
        return GDL_NATIVE_BUILD_OUTSIDE_V1;
    for (index = 0u; index < native->variable_count; index++)
        if (atom_eq(native->variables[index].form, binding->form) &&
            atom_eq(native->variables[index].name, binding->name))
            return GDL_NATIVE_BUILD_OUTSIDE_V1;
    binding->form = gdl_native_copy(native, binding->form);
    binding->name = gdl_native_copy(native, binding->name);
    binding->type = gdl_native_copy(native, binding->type);
    binding->proof = gdl_native_proof(
        native, rule_id, NULL, 0u, NULL, 0u);
    if (!binding->form || !binding->name || !binding->type ||
        !binding->proof)
        return native->resource_exhausted
            ? GDL_NATIVE_BUILD_RESOURCE_V1
            : GDL_NATIVE_BUILD_OUTSIDE_V1;
    native->variable_count++;
    return GDL_NATIVE_BUILD_OK_V1;
}

static GdlNativeBuildV1 gdl_native_add_subtype_fact(
    CettaGdlTypeOfNativeV1 *native, const char *rule_id,
    Atom *conclusion) {
    const char *prefix = "gdl:subtype-authored:";
    Atom *arguments;
    GdlNativeSubtypeEdgeV1 *edge;
    if (strncmp(rule_id, prefix, strlen(prefix)) != 0 ||
        rule_id[strlen(prefix)] == '\0' ||
        !gdl_native_papp_arity(conclusion, "gdl:subtype", 2u, &arguments))
        return GDL_NATIVE_BUILD_OUTSIDE_V1;
    if (!gdl_native_reserve(
            (void **)&native->subtypes, &native->subtype_capacity,
            native->subtype_count + 1u, sizeof(*native->subtypes))) {
        native->resource_exhausted = true;
        return GDL_NATIVE_BUILD_RESOURCE_V1;
    }
    edge = &native->subtypes[native->subtype_count++];
    memset(edge, 0, sizeof(*edge));
    if (!gdl_native_wire_list_item(arguments, 0u, &edge->subtype) ||
        !gdl_native_wire_list_item(arguments, 1u, &edge->supertype))
        return GDL_NATIVE_BUILD_OUTSIDE_V1;
    edge->subtype = gdl_native_copy(native, edge->subtype);
    edge->supertype = gdl_native_copy(native, edge->supertype);
    edge->proof = gdl_native_proof(
        native, rule_id, NULL, 0u, NULL, 0u);
    if (!edge->subtype || !edge->supertype || !edge->proof)
        return native->resource_exhausted
            ? GDL_NATIVE_BUILD_RESOURCE_V1
            : GDL_NATIVE_BUILD_OUTSIDE_V1;
    return GDL_NATIVE_BUILD_OK_V1;
}

static GdlNativeBuildV1 gdl_native_add_fact(
    CettaGdlTypeOfNativeV1 *native, Atom *rule) {
    const char *rule_id;
    Atom *conclusion;
    const char *head;
    if (!gdl_native_rule_fact(rule, &rule_id, &conclusion) ||
        !gdl_native_rule_id_insert(native, rule_id) ||
        !gdl_native_papp(conclusion, &head, NULL))
        return GDL_NATIVE_BUILD_OUTSIDE_V1;
    if (strcmp(head, "gdl:source-node") == 0 ||
        strcmp(head, "gdl:source-children") == 0 ||
        strcmp(head, "gdl:source-form") == 0)
        return gdl_native_add_source_fact(
            native, rule_id, conclusion, head);
    if (strcmp(head, "gdl:signature") == 0)
        return gdl_native_add_signature_fact(native, rule_id, conclusion);
    if (strcmp(head, "gdl:variable-type") == 0)
        return gdl_native_add_variable_fact(native, rule_id, conclusion);
    if (strcmp(head, "gdl:subtype") == 0)
        return gdl_native_add_subtype_fact(native, rule_id, conclusion);
    return GDL_NATIVE_BUILD_OUTSIDE_V1;
}

static bool gdl_native_object_nil(Atom *list) {
    return gdl_native_papp_arity(list, "gdl:nil", 0u, NULL);
}

static bool gdl_native_object_cons(
    Atom *list, Atom **head_out, Atom **tail_out) {
    Atom *arguments;
    return head_out && tail_out &&
        gdl_native_papp_arity(list, "gdl:cons", 2u, &arguments) &&
        gdl_native_wire_list_item(arguments, 0u, head_out) &&
        gdl_native_wire_list_item(arguments, 1u, tail_out);
}

static bool gdl_native_object_list_count(Atom *list, size_t *count_out) {
    size_t count = 0u;
    Atom *cursor = list;
    if (!count_out)
        return false;
    while (!gdl_native_object_nil(cursor)) {
        Atom *head;
        Atom *tail;
        if (!gdl_native_object_cons(cursor, &head, &tail) || count == SIZE_MAX)
            return false;
        (void)head;
        count++;
        cursor = tail;
    }
    *count_out = count;
    return true;
}

static bool gdl_native_zero_papp_named(Atom *pattern, const char *name) {
    const char *head;
    Atom *arguments;
    size_t count;
    return gdl_native_papp(pattern, &head, &arguments) &&
           strcmp(head, name) == 0 &&
           gdl_native_wire_list_count(arguments, &count) && count == 0u;
}

static Atom *gdl_native_make_papp(
    CettaGdlTypeOfNativeV1 *native, const char *head,
    Atom *const *arguments, size_t argument_count) {
    Atom *wire;
    Atom *parts[3];
    if (!native || !head || !*head)
        return NULL;
    wire = gdl_native_wire_list(
        &native->arena, "LNil", "LCons", arguments, argument_count);
    parts[0] = atom_symbol(&native->arena, "PApp");
    parts[1] = atom_string(&native->arena, head);
    parts[2] = wire;
    return parts[0] && parts[1] && parts[2]
        ? atom_expr(&native->arena, parts, 3u)
        : NULL;
}

static Atom *gdl_native_object_list(
    CettaGdlTypeOfNativeV1 *native, Atom *const *items, size_t count) {
    Atom *result;
    size_t index = count;
    if (!native)
        return NULL;
    result = gdl_native_make_papp(native, "gdl:nil", NULL, 0u);
    while (result && index > 0u) {
        Atom *arguments[2];
        index--;
        arguments[0] = items[index];
        arguments[1] = result;
        result = gdl_native_make_papp(
            native, "gdl:cons", arguments, 2u);
    }
    return result;
}

static GdlNativeSourceNodeV1 *gdl_native_node_find(
    CettaGdlTypeOfNativeV1 *native, Atom *occurrence) {
    SymbolId key;
    uint32_t index;
    if (!native ||
        !gdl_native_zero_papp_key(
            occurrence, "gdl:occurrence:", &key, NULL) ||
        !cetta_gslt_u32_index_find_v1(&native->node_index, key, &index) ||
        (size_t)index >= native->node_count)
        return NULL;
    return &native->nodes[index];
}

static bool gdl_native_subtype_path_cycle(
    const CettaGdlTypeOfNativeV1 *native, Atom *type,
    Atom **path, size_t path_count) {
    size_t index;
    if (!native || !type || !path)
        return true;
    for (index = 0u; index < path_count; index++)
        if (atom_eq(path[index], type))
            return true;
    if (path_count >= native->limits.max_derivation_depth)
        return true;
    path[path_count++] = type;
    for (index = 0u; index < native->subtype_count; index++)
        if (atom_eq(native->subtypes[index].subtype, type) &&
            gdl_native_subtype_path_cycle(
                native, native->subtypes[index].supertype,
                path, path_count))
            return true;
    return false;
}

static bool gdl_native_subtypes_acyclic(
    const CettaGdlTypeOfNativeV1 *native) {
    Atom **path;
    size_t path_capacity;
    size_t index;
    bool valid = true;
    if (!native)
        return false;
    path_capacity = native->subtype_count + 1u;
    if (path_capacity > native->limits.max_derivation_depth)
        path_capacity = native->limits.max_derivation_depth;
    if (path_capacity == 0u)
        path_capacity = 1u;
    path = cetta_malloc(path_capacity * sizeof(*path));
    for (index = 0u; index < native->subtype_count; index++)
        if (gdl_native_subtype_path_cycle(
                native, native->subtypes[index].subtype, path, 0u)) {
            valid = false;
            break;
        }
    free(path);
    return valid;
}

static GdlNativeBuildV1 gdl_native_accepts_rec(
    CettaGdlTypeOfNativeV1 *native, Atom *actual, Atom *expected,
    Atom **path, size_t path_count, GdlNativeProofBagV1 *proofs_out) {
    size_t index;
    if (!native || !actual || !expected || !path || !proofs_out)
        return GDL_NATIVE_BUILD_OUTSIDE_V1;
    if (path_count >= native->limits.max_derivation_depth) {
        native->resource_exhausted = true;
        return GDL_NATIVE_BUILD_RESOURCE_V1;
    }
    for (index = 0u; index < path_count; index++)
        if (atom_eq(path[index], actual))
            return GDL_NATIVE_BUILD_OUTSIDE_V1;
    path[path_count++] = actual;
    if (atom_eq(actual, expected)) {
        Atom *arguments[] = {actual};
        Atom *proof = gdl_native_proof(
            native, "gdl:accepts-refl", arguments, 1u, NULL, 0u);
        if (!proof || !gdl_native_proof_bag_append(proofs_out, proof))
            return native->resource_exhausted
                ? GDL_NATIVE_BUILD_RESOURCE_V1
                : GDL_NATIVE_BUILD_OUTSIDE_V1;
    }
    for (index = 0u; index < native->subtype_count; index++) {
        const GdlNativeSubtypeEdgeV1 *edge = &native->subtypes[index];
        GdlNativeProofBagV1 tails = {0};
        GdlNativeBuildV1 built;
        size_t tail_index;
        if (!atom_eq(edge->subtype, actual))
            continue;
        built = gdl_native_accepts_rec(
            native, edge->supertype, expected,
            path, path_count, &tails);
        if (built == GDL_NATIVE_BUILD_RESOURCE_V1) {
            gdl_native_proof_bag_free(&tails);
            return built;
        }
        for (tail_index = 0u; tail_index < tails.count; tail_index++) {
            Atom *arguments[] = {actual, edge->supertype, expected};
            Atom *premises[] = {edge->proof, tails.items[tail_index]};
            Atom *proof = gdl_native_proof(
                native, "gdl:accepts-step", arguments, 3u,
                premises, 2u);
            if (!proof || !gdl_native_proof_bag_append(proofs_out, proof)) {
                gdl_native_proof_bag_free(&tails);
                return native->resource_exhausted
                    ? GDL_NATIVE_BUILD_RESOURCE_V1
                    : GDL_NATIVE_BUILD_OUTSIDE_V1;
            }
        }
        gdl_native_proof_bag_free(&tails);
    }
    return GDL_NATIVE_BUILD_OK_V1;
}

static GdlNativeBuildV1 gdl_native_accepts(
    CettaGdlTypeOfNativeV1 *native, Atom *actual, Atom *expected,
    GdlNativeProofBagV1 *proofs_out) {
    Atom **path;
    size_t path_capacity;
    GdlNativeBuildV1 result;
    if (!native || !proofs_out)
        return GDL_NATIVE_BUILD_OUTSIDE_V1;
    path_capacity = native->subtype_count + 2u;
    if (path_capacity > native->limits.max_derivation_depth)
        path_capacity = native->limits.max_derivation_depth;
    if (path_capacity == 0u)
        return GDL_NATIVE_BUILD_RESOURCE_V1;
    path = cetta_malloc(path_capacity * sizeof(*path));
    result = gdl_native_accepts_rec(
        native, actual, expected, path, 0u, proofs_out);
    free(path);
    return result;
}

static GdlNativeBuildV1 gdl_native_node_add_type_proof(
    CettaGdlTypeOfNativeV1 *native, GdlNativeSourceNodeV1 *node,
    Atom *type, Atom *proof) {
    if (!native || !node || !type || !proof)
        return GDL_NATIVE_BUILD_OUTSIDE_V1;
    if (node->type && !atom_eq(node->type, type))
        return GDL_NATIVE_BUILD_OUTSIDE_V1;
    if (!node->type)
        node->type = type;
    if (!gdl_native_proof_bag_append(&node->type_proofs, proof))
        return GDL_NATIVE_BUILD_RESOURCE_V1;
    native->stats.type_proof_occurrences++;
    return GDL_NATIVE_BUILD_OK_V1;
}

static GdlNativeBuildV1 gdl_native_derive_node(
    CettaGdlTypeOfNativeV1 *native, GdlNativeSourceNodeV1 *node,
    size_t depth);

static GdlNativeBuildV1 gdl_native_arguments_type(
    CettaGdlTypeOfNativeV1 *native,
    Atom *children, Atom *terms, Atom *expected_types,
    size_t depth, GdlNativeProofBagV1 *proofs_out) {
    Atom *child;
    Atom *remaining_children;
    Atom *child_term;
    Atom *remaining_terms;
    Atom *expected_type;
    Atom *remaining_types;
    GdlNativeSourceNodeV1 *child_node;
    GdlNativeProofBagV1 accepts = {0};
    GdlNativeProofBagV1 tails = {0};
    GdlNativeBuildV1 built;
    size_t child_proof_index;
    size_t accepts_index;
    size_t tail_index;
    if (!native || !proofs_out ||
        depth >= native->limits.max_derivation_depth) {
        if (native)
            native->resource_exhausted = true;
        return GDL_NATIVE_BUILD_RESOURCE_V1;
    }
    if (gdl_native_object_nil(children) &&
        gdl_native_object_nil(terms) &&
        gdl_native_object_nil(expected_types)) {
        Atom *proof = gdl_native_proof(
            native, "gdl:arguments-type-nil", NULL, 0u, NULL, 0u);
        return proof && gdl_native_proof_bag_append(proofs_out, proof)
            ? GDL_NATIVE_BUILD_OK_V1
            : GDL_NATIVE_BUILD_RESOURCE_V1;
    }
    if (!gdl_native_object_cons(
            children, &child, &remaining_children) ||
        !gdl_native_object_cons(terms, &child_term, &remaining_terms) ||
        !gdl_native_object_cons(
            expected_types, &expected_type, &remaining_types) ||
        !(child_node = gdl_native_node_find(native, child)) ||
        !atom_eq(child_node->term, child_term))
        return GDL_NATIVE_BUILD_OUTSIDE_V1;
    built = gdl_native_derive_node(native, child_node, depth + 1u);
    if (built != GDL_NATIVE_BUILD_OK_V1)
        return built;
    built = gdl_native_accepts(
        native, child_node->type, expected_type, &accepts);
    if (built != GDL_NATIVE_BUILD_OK_V1) {
        gdl_native_proof_bag_free(&accepts);
        return built;
    }
    built = gdl_native_arguments_type(
        native, remaining_children, remaining_terms, remaining_types,
        depth + 1u, &tails);
    if (built != GDL_NATIVE_BUILD_OK_V1) {
        gdl_native_proof_bag_free(&accepts);
        gdl_native_proof_bag_free(&tails);
        return built;
    }
    for (child_proof_index = 0u;
         child_proof_index < child_node->type_proofs.count;
         child_proof_index++)
        for (accepts_index = 0u; accepts_index < accepts.count;
             accepts_index++)
            for (tail_index = 0u; tail_index < tails.count; tail_index++) {
                Atom *arguments[] = {
                    child, remaining_children, child_term, remaining_terms,
                    child_node->type, expected_type, remaining_types,
                };
                Atom *premises[] = {
                    child_node->type_proofs.items[child_proof_index],
                    accepts.items[accepts_index], tails.items[tail_index],
                };
                Atom *proof = gdl_native_proof(
                    native, "gdl:arguments-type-cons", arguments, 7u,
                    premises, 3u);
                if (!proof ||
                    !gdl_native_proof_bag_append(proofs_out, proof)) {
                    gdl_native_proof_bag_free(&accepts);
                    gdl_native_proof_bag_free(&tails);
                    return GDL_NATIVE_BUILD_RESOURCE_V1;
                }
            }
    gdl_native_proof_bag_free(&accepts);
    gdl_native_proof_bag_free(&tails);
    return GDL_NATIVE_BUILD_OK_V1;
}

/* ------------------------------------------------------------------------- */
/* Authored GDL source/profile path.                                          */

static char *gdl_source_format(Arena *arena, const char *format, ...) {
    va_list arguments;
    va_list copy;
    int length;
    char *result;
    if (!arena || !format)
        return NULL;
    va_start(arguments, format);
    va_copy(copy, arguments);
    length = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (length < 0) {
        va_end(arguments);
        return NULL;
    }
    result = arena_alloc(arena, (size_t)length + 1u);
    vsnprintf(result, (size_t)length + 1u, format, arguments);
    va_end(arguments);
    return result;
}

static const char *gdl_source_hex_name(
    Arena *arena, const char *prefix, const char *text) {
    static const char digits[] = "0123456789abcdef";
    size_t prefix_length;
    size_t text_length;
    char *result;
    size_t index;
    if (!arena || !prefix || !text)
        return NULL;
    prefix_length = strlen(prefix);
    text_length = strlen(text);
    if (text_length > (SIZE_MAX - prefix_length - 1u) / 2u)
        return NULL;
    result = arena_alloc(arena, prefix_length + text_length * 2u + 1u);
    memcpy(result, prefix, prefix_length);
    for (index = 0u; index < text_length; index++) {
        unsigned char byte = (unsigned char)text[index];
        result[prefix_length + index * 2u] = digits[byte >> 4u];
        result[prefix_length + index * 2u + 1u] = digits[byte & 15u];
    }
    result[prefix_length + text_length * 2u] = '\0';
    return result;
}

 typedef enum {
    GDL_SOURCE_APPLICATION_AUTHORED_V1 = 0,
    GDL_SOURCE_APPLICATION_STRUCTURAL_V1,
    GDL_SOURCE_APPLICATION_DERIVED_V1,
    GDL_SOURCE_APPLICATION_AMBIGUOUS_V1,
} GdlSourceApplicationKindV1;

typedef enum {
    GDL_SOURCE_TERM_APPLICATION_V1 = 0,
    GDL_SOURCE_TERM_VARIABLE_V1,
    GDL_SOURCE_TERM_NOT_V1,
    GDL_SOURCE_TERM_OR_V1,
    GDL_SOURCE_TERM_DISTINCT_V1,
} GdlSourceTermKindV1;

typedef struct {
    GdlSourceRawExprV1 *raw;
    size_t native_index;
    size_t form_ordinal;
    const char *path;
    const char *name;
    size_t *children;
    size_t child_count;
    size_t type_variable;
    GdlSourceTermKindV1 kind;
    GdlSourceApplicationKindV1 application_kind;
    bool literal_position;
} GdlSourceAnalysisNodeV1;

typedef enum {
    GDL_SOURCE_TYPE_OCCURRENCE_V1 = 0,
    GDL_SOURCE_TYPE_RULE_VARIABLE_V1,
    GDL_SOURCE_TYPE_DERIVED_SIGNATURE_V1,
} GdlSourceTypeVariableKindV1;

typedef struct {
    size_t parent;
    GdlSourceTypeVariableKindV1 kind;
    size_t source_order;
    size_t form_ordinal;
    const char *name;
    size_t arity;
    size_t position;
    const char *anchor;
} GdlSourceTypeVariableV1;

typedef struct {
    size_t form_ordinal;
    const char *name;
    size_t variable;
} GdlSourceRuleVariableV1;

typedef struct {
    const char *name;
    size_t arity;
    size_t position;
    size_t variable;
} GdlSourceDerivedSlotV1;

typedef struct {
    bool known;
    union {
        const char *type_name;
        size_t variable;
    } value;
} GdlSourceTypeReferenceV1;

typedef struct {
    GdlSourceTypeReferenceV1 actual;
    GdlSourceTypeReferenceV1 expected;
} GdlSourceAcceptanceV1;

typedef struct {
    GdlSourceAnalysisNodeV1 *nodes;
    size_t node_count;
    size_t node_capacity;
    GdlSourceTypeVariableV1 *type_variables;
    size_t type_variable_count;
    size_t type_variable_capacity;
    GdlSourceRuleVariableV1 *rule_variables;
    size_t rule_variable_count;
    size_t rule_variable_capacity;
    GdlSourceDerivedSlotV1 *derived_slots;
    size_t derived_slot_count;
    size_t derived_slot_capacity;
    GdlSourceAcceptanceV1 *acceptances;
    size_t acceptance_count;
    size_t acceptance_capacity;
    const char **type_names;
    size_t type_name_count;
    size_t type_name_capacity;
    bool conflict;
} GdlSourceAnalysisV1;

typedef struct {
    const char *name;
    const char *const *argument_types;
    size_t argument_count;
    const char *result_type;
} GdlSourceStructuralSignatureV1;

static const char *const gdl_source_role_arguments[] = {"agent"};
static const char *const gdl_source_base_arguments[] = {"prop"};
static const char *const gdl_source_input_arguments[] = {"agent", "action"};
static const char *const gdl_source_init_arguments[] = {"prop"};
static const char *const gdl_source_true_arguments[] = {"prop"};
static const char *const gdl_source_does_arguments[] = {"agent", "action"};
static const char *const gdl_source_next_arguments[] = {"prop"};
static const char *const gdl_source_legal_arguments[] = {"agent", "action"};

static const GdlSourceStructuralSignatureV1 gdl_source_structural_signatures[] = {
    {"role", gdl_source_role_arguments, 1u, "bool"},
    {"base", gdl_source_base_arguments, 1u, "bool"},
    {"input", gdl_source_input_arguments, 2u, "bool"},
    {"init", gdl_source_init_arguments, 1u, "bool"},
    {"true", gdl_source_true_arguments, 1u, "bool"},
    {"does", gdl_source_does_arguments, 2u, "bool"},
    {"next", gdl_source_next_arguments, 1u, "bool"},
    {"legal", gdl_source_legal_arguments, 2u, "bool"},
    {"terminal", NULL, 0u, "bool"},
};

static const GdlSourceStructuralSignatureV1 *gdl_source_structural_signature(
    const char *name, size_t arity) {
    size_t index;
    for (index = 0u;
         index < sizeof(gdl_source_structural_signatures) /
                     sizeof(gdl_source_structural_signatures[0]);
         index++) {
        const GdlSourceStructuralSignatureV1 *signature =
            &gdl_source_structural_signatures[index];
        if (signature->argument_count == arity &&
            strcmp(signature->name, name) == 0)
            return signature;
    }
    return NULL;
}

static bool gdl_source_type_name_add(
    GdlSourceAnalysisV1 *analysis, const char *name) {
    size_t index;
    if (!analysis || !name || !*name)
        return false;
    for (index = 0u; index < analysis->type_name_count; index++)
        if (strcmp(analysis->type_names[index], name) == 0)
            return true;
    if (!gdl_native_reserve(
            (void **)&analysis->type_names, &analysis->type_name_capacity,
            analysis->type_name_count + 1u,
            sizeof(*analysis->type_names)))
        return false;
    analysis->type_names[analysis->type_name_count++] = name;
    return true;
}

static size_t gdl_source_type_variable_add(
    GdlSourceAnalysisV1 *analysis, GdlSourceTypeVariableKindV1 kind,
    size_t source_order, size_t form_ordinal, const char *name,
    size_t arity, size_t position) {
    size_t index;
    GdlSourceTypeVariableV1 *variable;
    if (!analysis || !gdl_native_reserve(
            (void **)&analysis->type_variables,
            &analysis->type_variable_capacity,
            analysis->type_variable_count + 1u,
            sizeof(*analysis->type_variables)))
        return SIZE_MAX;
    index = analysis->type_variable_count++;
    variable = &analysis->type_variables[index];
    memset(variable, 0, sizeof(*variable));
    variable->parent = index;
    variable->kind = kind;
    variable->source_order = source_order;
    variable->form_ordinal = form_ordinal;
    variable->name = name;
    variable->arity = arity;
    variable->position = position;
    return index;
}

static Atom *gdl_source_name_pattern(
    CettaGdlTypeOfNativeV1 *native, const char *name) {
    const char *head = gdl_source_hex_name(
        &native->arena, "gdl:name:", name);
    return head ? gdl_native_make_papp(native, head, NULL, 0u) : NULL;
}

static Atom *gdl_source_type_pattern(
    CettaGdlTypeOfNativeV1 *native, const char *name) {
    const char *head = gdl_source_hex_name(
        &native->arena, "gdl:type:", name);
    return head ? gdl_native_make_papp(native, head, NULL, 0u) : NULL;
}

static Atom *gdl_source_term_pattern(
    CettaGdlTypeOfNativeV1 *native, GdlSourceRawExprV1 *raw,
    size_t depth) {
    Atom *name;
    Atom *terms;
    Atom *arguments[2];
    Atom **term_items = NULL;
    size_t term_count = 0u;
    size_t index;
    if (!native || !raw || depth >= native->limits.max_derivation_depth) {
        if (native)
            native->resource_exhausted = true;
        return NULL;
    }
    if (raw->token) {
        name = gdl_source_name_pattern(native, raw->token);
        if (!name)
            return NULL;
        if (raw->token[0] == '?')
            return gdl_native_make_papp(native, "gdl:variable", &name, 1u);
        terms = gdl_native_object_list(native, NULL, 0u);
    } else {
        if (raw->count == 0u || !raw->items[0]->token)
            return NULL;
        name = gdl_source_name_pattern(native, raw->items[0]->token);
        term_count = raw->count - 1u;
        if (term_count > 0u) {
            term_items = cetta_malloc(term_count * sizeof(*term_items));
            for (index = 0u; index < term_count; index++) {
                term_items[index] = gdl_source_term_pattern(
                    native, raw->items[index + 1u], depth + 1u);
                if (!term_items[index]) {
                    free(term_items);
                    return NULL;
                }
            }
        }
        terms = gdl_native_object_list(native, term_items, term_count);
        free(term_items);
    }
    if (!name || !terms)
        return NULL;
    arguments[0] = name;
    arguments[1] = terms;
    return gdl_native_make_papp(native, "gdl:application", arguments, 2u);
}

static Atom *gdl_source_occurrence_pattern(
    CettaGdlTypeOfNativeV1 *native, size_t form_ordinal,
    const char *path) {
    const char *head = gdl_source_format(
        &native->arena, "gdl:occurrence:%zu:%s", form_ordinal, path);
    return head ? gdl_native_make_papp(native, head, NULL, 0u) : NULL;
}

static GdlNativeBuildV1 gdl_source_add_owned_node(
    CettaGdlTypeOfNativeV1 *native, size_t form_ordinal, const char *path,
    GdlSourceRawExprV1 *raw, Atom *const *child_occurrences,
    size_t child_count, size_t *native_index_out) {
    GdlNativeSourceNodeV1 *node;
    Atom *occurrence;
    Atom *form;
    Atom *children;
    const char *occurrence_head;
    SymbolId key;
    uint32_t index;
    const char *node_rule;
    const char *children_rule;
    const char *form_rule;
    if (!native || !path || !raw || !native_index_out ||
        native->node_count >= native->limits.max_source_nodes ||
        native->node_count >= UINT32_MAX)
        return GDL_NATIVE_BUILD_RESOURCE_V1;
    occurrence = gdl_source_occurrence_pattern(native, form_ordinal, path);
    form = gdl_native_make_papp(
        native, gdl_source_format(
            &native->arena, "gdl:form:%zu", form_ordinal), NULL, 0u);
    children = gdl_native_object_list(native, child_occurrences, child_count);
    if (!occurrence || !form || !children ||
        !gdl_native_zero_papp_key(
            occurrence, "gdl:occurrence:", &key, &occurrence_head) ||
        cetta_gslt_u32_index_find_v1(&native->node_index, key, &index) ||
        !gdl_native_reserve(
            (void **)&native->nodes, &native->node_capacity,
            native->node_count + 1u, sizeof(*native->nodes)))
        return GDL_NATIVE_BUILD_OUTSIDE_V1;
    index = (uint32_t)native->node_count;
    node = &native->nodes[native->node_count++];
    memset(node, 0, sizeof(*node));
    node->occurrence_key = key;
    node->form_ordinal = form_ordinal;
    node->path = gdl_source_format(&native->arena, "%s", path);
    node->occurrence = occurrence;
    node->term = gdl_source_term_pattern(native, raw, 0u);
    node->children = children;
    node->form = form;
    node_rule = gdl_source_format(
        &native->arena, "gdl:source-node:%s", occurrence_head);
    children_rule = gdl_source_format(
        &native->arena, "gdl:source-children:%s", occurrence_head);
    form_rule = gdl_source_format(
        &native->arena, "gdl:source-form:%s", occurrence_head);
    if (!node->path || !node->term || !node_rule || !children_rule ||
        !form_rule ||
        !gdl_native_rule_id_insert(native, node_rule) ||
        !gdl_native_rule_id_insert(native, children_rule) ||
        !gdl_native_rule_id_insert(native, form_rule) ||
        cetta_gslt_u32_index_insert_unique_v1(
            &native->node_index, key, index) !=
            CETTA_GSLT_U32_INDEX_INSERTED_V1)
        return native->resource_exhausted
            ? GDL_NATIVE_BUILD_RESOURCE_V1
            : GDL_NATIVE_BUILD_OUTSIDE_V1;
    node->source_node_proof = gdl_native_proof(
        native, node_rule, NULL, 0u, NULL, 0u);
    node->source_children_proof = gdl_native_proof(
        native, children_rule, NULL, 0u, NULL, 0u);
    node->source_form_proof = gdl_native_proof(
        native, form_rule, NULL, 0u, NULL, 0u);
    if (!node->source_node_proof || !node->source_children_proof ||
        !node->source_form_proof)
        return GDL_NATIVE_BUILD_RESOURCE_V1;
    *native_index_out = index;
    return GDL_NATIVE_BUILD_OK_V1;
}

static GdlNativeBuildV1 gdl_source_visit_node(
    CettaGdlTypeOfNativeV1 *native, Arena *scratch,
    GdlSourceAnalysisV1 *analysis, size_t form_ordinal,
    const char *path, GdlSourceRawExprV1 *raw, bool literal,
    size_t depth,
    size_t *node_index_out) {
    GdlSourceAnalysisNodeV1 *node;
    const char *name;
    size_t child_count;
    Atom **child_occurrences = NULL;
    size_t *children = NULL;
    size_t native_index;
    size_t analysis_index;
    size_t child_index;
    GdlNativeBuildV1 built;
    if (!native || !scratch || !analysis || !path || !raw ||
        !node_index_out || depth >= native->limits.max_derivation_depth)
        return GDL_NATIVE_BUILD_RESOURCE_V1;
    if (raw->token) {
        name = raw->token;
        child_count = 0u;
    } else {
        if (raw->count == 0u || !raw->items[0]->token)
            return GDL_NATIVE_BUILD_OUTSIDE_V1;
        name = raw->items[0]->token;
        child_count = raw->count - 1u;
    }
    if (child_count > 0u) {
        child_occurrences = cetta_malloc(
            child_count * sizeof(*child_occurrences));
        children = arena_alloc(scratch, child_count * sizeof(*children));
        for (child_index = 0u; child_index < child_count; child_index++) {
            const char *child_path = strcmp(path, "root") == 0
                ? gdl_source_format(scratch, "%zu", child_index + 1u)
                : gdl_source_format(
                    scratch, "%s.%zu", path, child_index + 1u);
            child_occurrences[child_index] = gdl_source_occurrence_pattern(
                native, form_ordinal, child_path);
            if (!child_occurrences[child_index]) {
                free(child_occurrences);
                return GDL_NATIVE_BUILD_RESOURCE_V1;
            }
        }
    }
    built = gdl_source_add_owned_node(
        native, form_ordinal, path, raw, child_occurrences, child_count,
        &native_index);
    free(child_occurrences);
    if (built != GDL_NATIVE_BUILD_OK_V1)
        return built;
    if (!gdl_native_reserve(
            (void **)&analysis->nodes, &analysis->node_capacity,
            analysis->node_count + 1u, sizeof(*analysis->nodes)))
        return GDL_NATIVE_BUILD_RESOURCE_V1;
    analysis_index = analysis->node_count++;
    node = &analysis->nodes[analysis_index];
    memset(node, 0, sizeof(*node));
    node->raw = raw;
    node->native_index = native_index;
    node->form_ordinal = form_ordinal;
    node->path = path;
    node->name = name;
    node->children = children;
    node->child_count = child_count;
    node->literal_position = literal;
    node->type_variable = gdl_source_type_variable_add(
        analysis, GDL_SOURCE_TYPE_OCCURRENCE_V1, analysis_index,
        form_ordinal, NULL, 0u, 0u);
    if (node->type_variable == SIZE_MAX)
        return GDL_NATIVE_BUILD_RESOURCE_V1;
    node->kind = raw->token && raw->token[0] == '?'
        ? GDL_SOURCE_TERM_VARIABLE_V1
        : !raw->token && strcmp(name, "not") == 0
        ? GDL_SOURCE_TERM_NOT_V1
        : !raw->token && strcmp(name, "or") == 0
        ? GDL_SOURCE_TERM_OR_V1
        : !raw->token && strcmp(name, "distinct") == 0
        ? GDL_SOURCE_TERM_DISTINCT_V1
        : GDL_SOURCE_TERM_APPLICATION_V1;
    for (child_index = 0u; child_index < child_count; child_index++) {
        const char *child_path = strcmp(path, "root") == 0
            ? gdl_source_format(scratch, "%zu", child_index + 1u)
            : gdl_source_format(
                scratch, "%s.%zu", path, child_index + 1u);
        built = gdl_source_visit_node(
            native, scratch, analysis, form_ordinal, child_path,
            raw->items[child_index + 1u],
            literal && (strcmp(name, "not") == 0 ||
                        strcmp(name, "or") == 0),
            depth + 1u,
            &children[child_index]);
        if (built != GDL_NATIVE_BUILD_OK_V1)
            return built;
    }
    *node_index_out = analysis_index;
    return GDL_NATIVE_BUILD_OK_V1;
}

static GdlNativeBuildV1 gdl_source_build_nodes(
    CettaGdlTypeOfNativeV1 *native, Arena *scratch,
    const GdlSourceRawFormsV1 *forms, GdlSourceAnalysisV1 *analysis) {
    size_t form_index;
    if (!native || !scratch || !forms || !analysis)
        return GDL_NATIVE_BUILD_OUTSIDE_V1;
    for (form_index = 0u; form_index < forms->count; form_index++) {
        if (!forms->items[form_index].selected)
            continue;
        GdlSourceRawExprV1 *form = forms->items[form_index].form;
        const char *head = form->items[0]->token;
        if (strcmp(head, "<=") == 0) {
            size_t literal_index;
            if (form->count < 2u)
                return GDL_NATIVE_BUILD_OUTSIDE_V1;
            for (literal_index = 1u; literal_index < form->count;
                 literal_index++) {
                size_t ignored;
                const char *path = gdl_source_format(
                    scratch, "%zu", literal_index);
                GdlNativeBuildV1 built = gdl_source_visit_node(
                    native, scratch, analysis, form_index, path,
                    form->items[literal_index], true, 0u, &ignored);
                if (built != GDL_NATIVE_BUILD_OK_V1)
                    return built;
            }
        } else {
            size_t ignored;
            GdlNativeBuildV1 built = gdl_source_visit_node(
                native, scratch, analysis, form_index, "root", form,
                true, 0u, &ignored);
            if (built != GDL_NATIVE_BUILD_OK_V1)
                return built;
        }
    }
    return analysis->node_count > 0u
        ? GDL_NATIVE_BUILD_OK_V1
        : GDL_NATIVE_BUILD_OUTSIDE_V1;
}

static int gdl_source_type_variable_key_compare(
    const GdlSourceTypeVariableV1 *left,
    const GdlSourceTypeVariableV1 *right) {
    int text_order;
    if (left->kind != right->kind)
        return left->kind < right->kind ? -1 : 1;
    if (left->kind == GDL_SOURCE_TYPE_OCCURRENCE_V1) {
        if (left->source_order != right->source_order)
            return left->source_order < right->source_order ? -1 : 1;
        return 0;
    }
    if (left->kind == GDL_SOURCE_TYPE_RULE_VARIABLE_V1) {
        if (left->form_ordinal != right->form_ordinal)
            return left->form_ordinal < right->form_ordinal ? -1 : 1;
        text_order = strcmp(left->name, right->name);
        return text_order < 0 ? -1 : text_order > 0 ? 1 : 0;
    }
    text_order = strcmp(left->name, right->name);
    if (text_order != 0)
        return text_order < 0 ? -1 : 1;
    if (left->arity != right->arity)
        return left->arity < right->arity ? -1 : 1;
    if (left->position != right->position)
        return left->position < right->position ? -1 : 1;
    return 0;
}

static size_t gdl_source_type_find(
    GdlSourceAnalysisV1 *analysis, size_t variable) {
    size_t root = variable;
    while (analysis->type_variables[root].parent != root)
        root = analysis->type_variables[root].parent;
    while (analysis->type_variables[variable].parent != variable) {
        size_t next = analysis->type_variables[variable].parent;
        analysis->type_variables[variable].parent = root;
        variable = next;
    }
    return root;
}

static bool gdl_source_type_union(
    GdlSourceAnalysisV1 *analysis, size_t left, size_t right) {
    size_t left_root = gdl_source_type_find(analysis, left);
    size_t right_root = gdl_source_type_find(analysis, right);
    GdlSourceTypeVariableV1 *left_variable;
    GdlSourceTypeVariableV1 *right_variable;
    if (left_root == right_root)
        return true;
    left_variable = &analysis->type_variables[left_root];
    right_variable = &analysis->type_variables[right_root];
    if (gdl_source_type_variable_key_compare(
            right_variable, left_variable) < 0) {
        size_t temporary = left_root;
        left_root = right_root;
        right_root = temporary;
        left_variable = &analysis->type_variables[left_root];
        right_variable = &analysis->type_variables[right_root];
    }
    if (left_variable->anchor && right_variable->anchor &&
        strcmp(left_variable->anchor, right_variable->anchor) != 0) {
        analysis->conflict = true;
        return false;
    }
    right_variable->parent = left_root;
    if (!left_variable->anchor)
        left_variable->anchor = right_variable->anchor;
    return true;
}

static bool gdl_source_type_anchor(
    GdlSourceAnalysisV1 *analysis, size_t variable, const char *type_name) {
    size_t root;
    if (!analysis || variable >= analysis->type_variable_count ||
        !gdl_source_type_name_add(analysis, type_name))
        return false;
    root = gdl_source_type_find(analysis, variable);
    if (analysis->type_variables[root].anchor &&
        strcmp(analysis->type_variables[root].anchor, type_name) != 0) {
        analysis->conflict = true;
        return false;
    }
    analysis->type_variables[root].anchor = type_name;
    return true;
}

static GdlSourceTypeReferenceV1 gdl_source_type_unknown(size_t variable) {
    GdlSourceTypeReferenceV1 result = {
        .known = false,
        .value.variable = variable,
    };
    return result;
}

static GdlSourceTypeReferenceV1 gdl_source_type_known(
    const char *type_name) {
    GdlSourceTypeReferenceV1 result = {
        .known = true,
        .value.type_name = type_name,
    };
    return result;
}

static bool gdl_source_acceptance_add(
    GdlSourceAnalysisV1 *analysis, GdlSourceTypeReferenceV1 actual,
    GdlSourceTypeReferenceV1 expected) {
    GdlSourceAcceptanceV1 *acceptance;
    if (!analysis ||
        (actual.known &&
         !gdl_source_type_name_add(analysis, actual.value.type_name)) ||
        (expected.known &&
         !gdl_source_type_name_add(analysis, expected.value.type_name)) ||
        !gdl_native_reserve(
            (void **)&analysis->acceptances,
            &analysis->acceptance_capacity,
            analysis->acceptance_count + 1u,
            sizeof(*analysis->acceptances)))
        return false;
    acceptance = &analysis->acceptances[analysis->acceptance_count++];
    acceptance->actual = actual;
    acceptance->expected = expected;
    return true;
}

static size_t gdl_source_rule_variable(
    GdlSourceAnalysisV1 *analysis, size_t form_ordinal,
    const char *name) {
    size_t index;
    size_t variable;
    GdlSourceRuleVariableV1 *binding;
    for (index = 0u; index < analysis->rule_variable_count; index++)
        if (analysis->rule_variables[index].form_ordinal == form_ordinal &&
            strcmp(analysis->rule_variables[index].name, name) == 0)
            return analysis->rule_variables[index].variable;
    variable = gdl_source_type_variable_add(
        analysis, GDL_SOURCE_TYPE_RULE_VARIABLE_V1, 0u,
        form_ordinal, name, 0u, 0u);
    if (variable == SIZE_MAX || !gdl_native_reserve(
            (void **)&analysis->rule_variables,
            &analysis->rule_variable_capacity,
            analysis->rule_variable_count + 1u,
            sizeof(*analysis->rule_variables)))
        return SIZE_MAX;
    binding = &analysis->rule_variables[analysis->rule_variable_count++];
    binding->form_ordinal = form_ordinal;
    binding->name = name;
    binding->variable = variable;
    return variable;
}

static size_t gdl_source_derived_slot(
    GdlSourceAnalysisV1 *analysis, const char *name,
    size_t arity, size_t position) {
    size_t index;
    size_t variable;
    GdlSourceDerivedSlotV1 *slot;
    for (index = 0u; index < analysis->derived_slot_count; index++)
        if (analysis->derived_slots[index].arity == arity &&
            analysis->derived_slots[index].position == position &&
            strcmp(analysis->derived_slots[index].name, name) == 0)
            return analysis->derived_slots[index].variable;
    variable = gdl_source_type_variable_add(
        analysis, GDL_SOURCE_TYPE_DERIVED_SIGNATURE_V1, 0u, 0u,
        name, arity, position);
    if (variable == SIZE_MAX || !gdl_native_reserve(
            (void **)&analysis->derived_slots,
            &analysis->derived_slot_capacity,
            analysis->derived_slot_count + 1u,
            sizeof(*analysis->derived_slots)))
        return SIZE_MAX;
    slot = &analysis->derived_slots[analysis->derived_slot_count++];
    slot->name = name;
    slot->arity = arity;
    slot->position = position;
    slot->variable = variable;
    return variable;
}

static bool gdl_source_signature_scheme_equal(
    const GdlSourceSignatureV1 *left,
    const GdlSourceSignatureV1 *right) {
    size_t index;
    if (left->argument_count != right->argument_count ||
        strcmp(left->result_type, right->result_type) != 0)
        return false;
    for (index = 0u; index < left->argument_count; index++)
        if (strcmp(left->argument_types[index],
                   right->argument_types[index]) != 0)
            return false;
    return true;
}

static bool gdl_source_structural_scheme_equal(
    const GdlSourceSignatureV1 *authored,
    const GdlSourceStructuralSignatureV1 *structural) {
    size_t index;
    if (!structural || authored->argument_count != structural->argument_count ||
        strcmp(authored->result_type, structural->result_type) != 0)
        return false;
    for (index = 0u; index < authored->argument_count; index++)
        if (strcmp(authored->argument_types[index],
                   structural->argument_types[index]) != 0)
            return false;
    return true;
}

static GdlNativeBuildV1 gdl_source_application_constraints(
    GdlSourceAnalysisV1 *analysis, const GdlSourceProfileV1 *profile,
    GdlSourceAnalysisNodeV1 *node) {
    const GdlSourceSignatureV1 *first = NULL;
    const GdlSourceStructuralSignatureV1 *structural;
    size_t matching_count = 0u;
    bool multiple_schemes = false;
    size_t index;
    if (!analysis || !profile || !node || node->name[0] == '?' ||
        strcmp(node->name, "<=") == 0)
        return GDL_NATIVE_BUILD_OUTSIDE_V1;
    structural = gdl_source_structural_signature(
        node->name, node->child_count);
    for (index = 0u; index < profile->signature_count; index++) {
        const GdlSourceSignatureV1 *candidate = &profile->signatures[index];
        if (candidate->argument_count != node->child_count ||
            strcmp(candidate->name, node->name) != 0)
            continue;
        if (!first)
            first = candidate;
        else if (!gdl_source_signature_scheme_equal(first, candidate))
            multiple_schemes = true;
        matching_count++;
    }
    if (matching_count > 0u && structural &&
        !gdl_source_structural_scheme_equal(first, structural))
        multiple_schemes = true;
    if (multiple_schemes) {
        node->application_kind = GDL_SOURCE_APPLICATION_AMBIGUOUS_V1;
        return GDL_NATIVE_BUILD_OUTSIDE_V1;
    }
    if (matching_count > 0u) {
        node->application_kind = GDL_SOURCE_APPLICATION_AUTHORED_V1;
        if (!gdl_source_type_anchor(
                analysis, node->type_variable, first->result_type))
            return GDL_NATIVE_BUILD_OUTSIDE_V1;
        for (index = 0u; index < node->child_count; index++)
            if (!gdl_source_acceptance_add(
                    analysis,
                    gdl_source_type_unknown(
                        analysis->nodes[node->children[index]].type_variable),
                    gdl_source_type_known(first->argument_types[index])))
                return GDL_NATIVE_BUILD_RESOURCE_V1;
        return GDL_NATIVE_BUILD_OK_V1;
    }
    if (structural) {
        node->application_kind = GDL_SOURCE_APPLICATION_STRUCTURAL_V1;
        if (!gdl_source_type_anchor(
                analysis, node->type_variable, structural->result_type))
            return GDL_NATIVE_BUILD_OUTSIDE_V1;
        for (index = 0u; index < node->child_count; index++)
            if (!gdl_source_acceptance_add(
                    analysis,
                    gdl_source_type_unknown(
                        analysis->nodes[node->children[index]].type_variable),
                    gdl_source_type_known(
                        structural->argument_types[index])))
                return GDL_NATIVE_BUILD_RESOURCE_V1;
        return GDL_NATIVE_BUILD_OK_V1;
    }
    node->application_kind = GDL_SOURCE_APPLICATION_DERIVED_V1;
    {
        size_t result_slot = gdl_source_derived_slot(
            analysis, node->name, node->child_count, node->child_count);
        if (result_slot == SIZE_MAX ||
            !gdl_source_type_union(
                analysis, node->type_variable, result_slot))
            return result_slot == SIZE_MAX
                ? GDL_NATIVE_BUILD_RESOURCE_V1
                : GDL_NATIVE_BUILD_OUTSIDE_V1;
        for (index = 0u; index < node->child_count; index++) {
            size_t argument_slot = gdl_source_derived_slot(
                analysis, node->name, node->child_count, index);
            if (argument_slot == SIZE_MAX || !gdl_source_acceptance_add(
                    analysis,
                    gdl_source_type_unknown(
                        analysis->nodes[node->children[index]].type_variable),
                    gdl_source_type_unknown(argument_slot)))
                return GDL_NATIVE_BUILD_RESOURCE_V1;
        }
    }
    return GDL_NATIVE_BUILD_OK_V1;
}

static GdlNativeBuildV1 gdl_source_extract_constraints(
    GdlSourceAnalysisV1 *analysis, const GdlSourceProfileV1 *profile) {
    size_t index;
    if (!analysis || !profile)
        return GDL_NATIVE_BUILD_OUTSIDE_V1;
    for (index = 0u;
         index < sizeof(gdl_source_structural_signatures) /
                     sizeof(gdl_source_structural_signatures[0]);
         index++) {
        const GdlSourceStructuralSignatureV1 *signature =
            &gdl_source_structural_signatures[index];
        size_t argument;
        if (!gdl_source_type_name_add(analysis, signature->result_type))
            return GDL_NATIVE_BUILD_RESOURCE_V1;
        for (argument = 0u; argument < signature->argument_count; argument++)
            if (!gdl_source_type_name_add(
                    analysis, signature->argument_types[argument]))
                return GDL_NATIVE_BUILD_RESOURCE_V1;
    }
    for (index = 0u; index < profile->signature_count; index++) {
        const GdlSourceSignatureV1 *signature = &profile->signatures[index];
        size_t argument;
        if (!gdl_source_type_name_add(analysis, signature->result_type))
            return GDL_NATIVE_BUILD_RESOURCE_V1;
        for (argument = 0u; argument < signature->argument_count; argument++)
            if (!gdl_source_type_name_add(
                    analysis, signature->argument_types[argument]))
                return GDL_NATIVE_BUILD_RESOURCE_V1;
    }
    for (index = 0u; index < profile->subtype_count; index++)
        if (!gdl_source_type_name_add(
                analysis, profile->subtypes[index].subtype) ||
            !gdl_source_type_name_add(
                analysis, profile->subtypes[index].supertype))
            return GDL_NATIVE_BUILD_RESOURCE_V1;
    for (index = 0u; index < analysis->node_count; index++) {
        GdlSourceAnalysisNodeV1 *node = &analysis->nodes[index];
        GdlNativeBuildV1 built = GDL_NATIVE_BUILD_OK_V1;
        if (node->kind == GDL_SOURCE_TERM_VARIABLE_V1) {
            size_t variable;
            if (node->child_count != 0u)
                return GDL_NATIVE_BUILD_OUTSIDE_V1;
            variable = gdl_source_rule_variable(
                analysis, node->form_ordinal, node->name);
            if (variable == SIZE_MAX)
                return GDL_NATIVE_BUILD_RESOURCE_V1;
            if (!gdl_source_type_union(
                    analysis, node->type_variable, variable))
                return GDL_NATIVE_BUILD_OUTSIDE_V1;
        } else if (node->kind == GDL_SOURCE_TERM_NOT_V1) {
            if (node->child_count != 1u ||
                !gdl_source_type_anchor(
                    analysis, node->type_variable, "bool"))
                return GDL_NATIVE_BUILD_OUTSIDE_V1;
        } else if (node->kind == GDL_SOURCE_TERM_OR_V1) {
            if (node->child_count == 0u ||
                !gdl_source_type_anchor(
                    analysis, node->type_variable, "bool"))
                return GDL_NATIVE_BUILD_OUTSIDE_V1;
        } else if (node->kind == GDL_SOURCE_TERM_DISTINCT_V1) {
            if (node->child_count != 2u ||
                !gdl_source_type_anchor(
                    analysis, node->type_variable, "bool"))
                return GDL_NATIVE_BUILD_OUTSIDE_V1;
        } else {
            built = gdl_source_application_constraints(
                analysis, profile, node);
            if (built != GDL_NATIVE_BUILD_OK_V1)
                return built;
        }
        if (node->literal_position &&
            node->kind != GDL_SOURCE_TERM_NOT_V1 &&
            node->kind != GDL_SOURCE_TERM_OR_V1 &&
            node->kind != GDL_SOURCE_TERM_DISTINCT_V1 &&
            !gdl_source_acceptance_add(
                analysis, gdl_source_type_unknown(node->type_variable),
                gdl_source_type_known("bool")))
            return GDL_NATIVE_BUILD_RESOURCE_V1;
    }
    return analysis->conflict
        ? GDL_NATIVE_BUILD_OUTSIDE_V1
        : GDL_NATIVE_BUILD_OK_V1;
}

typedef struct {
    size_t component_count;
    size_t *component_roots;
    size_t *component_by_variable;
    size_t type_count;
    uint8_t *accepts;
    uint8_t *domains;
    size_t *selected_types;
    CettaGdlRuleVariableSelectionV1 rule_variable_selection;
} GdlSourceTypeSolutionV1;

static int gdl_source_text_pointer_compare(
    const void *left, const void *right) {
    const char *const *left_text = left;
    const char *const *right_text = right;
    return strcmp(*left_text, *right_text);
}

static size_t gdl_source_type_name_index(
    const GdlSourceAnalysisV1 *analysis, const char *name) {
    size_t low = 0u;
    size_t high = analysis->type_name_count;
    while (low < high) {
        size_t middle = low + (high - low) / 2u;
        int order = strcmp(analysis->type_names[middle], name);
        if (order < 0)
            low = middle + 1u;
        else if (order > 0)
            high = middle;
        else
            return middle;
    }
    return SIZE_MAX;
}

static void gdl_source_solution_free(GdlSourceTypeSolutionV1 *solution) {
    if (!solution)
        return;
    free(solution->component_roots);
    free(solution->component_by_variable);
    free(solution->accepts);
    free(solution->domains);
    free(solution->selected_types);
    memset(solution, 0, sizeof(*solution));
}

static bool gdl_source_component_less(
    const GdlSourceAnalysisV1 *analysis, size_t left_root,
    size_t right_root) {
    return gdl_source_type_variable_key_compare(
        &analysis->type_variables[left_root],
        &analysis->type_variables[right_root]) < 0;
}

static bool gdl_source_solution_components(
    GdlSourceAnalysisV1 *analysis, GdlSourceTypeSolutionV1 *solution) {
    size_t variable;
    if (!analysis || !solution || analysis->type_variable_count == 0u)
        return false;
    solution->component_roots = cetta_malloc(
        analysis->type_variable_count * sizeof(*solution->component_roots));
    solution->component_by_variable = cetta_malloc(
        analysis->type_variable_count *
        sizeof(*solution->component_by_variable));
    for (variable = 0u; variable < analysis->type_variable_count; variable++) {
        size_t root = gdl_source_type_find(analysis, variable);
        size_t position;
        bool found = false;
        for (position = 0u; position < solution->component_count; position++)
            if (solution->component_roots[position] == root) {
                found = true;
                break;
            }
        if (!found) {
            position = solution->component_count++;
            solution->component_roots[position] = root;
            while (position > 0u && gdl_source_component_less(
                    analysis, solution->component_roots[position],
                    solution->component_roots[position - 1u])) {
                size_t temporary = solution->component_roots[position];
                solution->component_roots[position] =
                    solution->component_roots[position - 1u];
                solution->component_roots[position - 1u] = temporary;
                position--;
            }
        }
    }
    for (variable = 0u; variable < analysis->type_variable_count; variable++) {
        size_t root = gdl_source_type_find(analysis, variable);
        size_t component;
        for (component = 0u; component < solution->component_count;
             component++)
            if (solution->component_roots[component] == root)
                break;
        if (component == solution->component_count)
            return false;
        solution->component_by_variable[variable] = component;
    }
    return true;
}

static bool gdl_source_acceptance_endpoints(
    const GdlSourceAnalysisV1 *analysis,
    const GdlSourceTypeSolutionV1 *solution,
    const GdlSourceAcceptanceV1 *acceptance,
    size_t *actual_component, size_t *actual_type,
    size_t *expected_component, size_t *expected_type) {
    if (acceptance->actual.known) {
        *actual_component = SIZE_MAX;
        *actual_type = gdl_source_type_name_index(
            analysis, acceptance->actual.value.type_name);
    } else {
        *actual_component = solution->component_by_variable[
            acceptance->actual.value.variable];
        *actual_type = SIZE_MAX;
    }
    if (acceptance->expected.known) {
        *expected_component = SIZE_MAX;
        *expected_type = gdl_source_type_name_index(
            analysis, acceptance->expected.value.type_name);
    } else {
        *expected_component = solution->component_by_variable[
            acceptance->expected.value.variable];
        *expected_type = SIZE_MAX;
    }
    return (*actual_component != SIZE_MAX || *actual_type != SIZE_MAX) &&
           (*expected_component != SIZE_MAX || *expected_type != SIZE_MAX);
}

static bool gdl_source_domains_reduce(
    const GdlSourceAnalysisV1 *analysis,
    const GdlSourceTypeSolutionV1 *solution, uint8_t *domains) {
    bool changed = true;
    const size_t type_count = solution->type_count;
    while (changed) {
        size_t constraint;
        changed = false;
        for (constraint = 0u; constraint < analysis->acceptance_count;
             constraint++) {
            const GdlSourceAcceptanceV1 *acceptance =
                &analysis->acceptances[constraint];
            size_t actual_component;
            size_t actual_type;
            size_t expected_component;
            size_t expected_type;
            size_t candidate;
            if (!gdl_source_acceptance_endpoints(
                    analysis, solution, acceptance,
                    &actual_component, &actual_type,
                    &expected_component, &expected_type))
                return false;
            if (actual_component != SIZE_MAX &&
                actual_component == expected_component)
                continue;
            if (actual_component == SIZE_MAX && expected_component == SIZE_MAX) {
                if (!solution->accepts[
                        actual_type * type_count + expected_type])
                    return false;
                continue;
            }
            if (actual_component != SIZE_MAX) {
                uint8_t *actual_domain =
                    domains + actual_component * type_count;
                for (candidate = 0u; candidate < type_count; candidate++) {
                    bool supported = false;
                    size_t expected_candidate;
                    if (!actual_domain[candidate])
                        continue;
                    if (expected_component == SIZE_MAX) {
                        supported = solution->accepts[
                            candidate * type_count + expected_type] != 0u;
                    } else {
                        const uint8_t *expected_domain =
                            domains + expected_component * type_count;
                        for (expected_candidate = 0u;
                             expected_candidate < type_count;
                             expected_candidate++)
                            if (expected_domain[expected_candidate] &&
                                solution->accepts[
                                    candidate * type_count +
                                    expected_candidate]) {
                                supported = true;
                                break;
                            }
                    }
                    if (!supported) {
                        actual_domain[candidate] = 0u;
                        changed = true;
                    }
                }
            }
            if (expected_component != SIZE_MAX) {
                uint8_t *expected_domain =
                    domains + expected_component * type_count;
                for (candidate = 0u; candidate < type_count; candidate++) {
                    bool supported = false;
                    size_t actual_candidate;
                    if (!expected_domain[candidate])
                        continue;
                    if (actual_component == SIZE_MAX) {
                        supported = solution->accepts[
                            actual_type * type_count + candidate] != 0u;
                    } else {
                        const uint8_t *actual_domain =
                            domains + actual_component * type_count;
                        for (actual_candidate = 0u;
                             actual_candidate < type_count;
                             actual_candidate++)
                            if (actual_domain[actual_candidate] &&
                                solution->accepts[
                                    actual_candidate * type_count +
                                    candidate]) {
                                supported = true;
                                break;
                            }
                    }
                    if (!supported) {
                        expected_domain[candidate] = 0u;
                        changed = true;
                    }
                }
            }
        }
        {
            size_t component;
            for (component = 0u; component < solution->component_count;
                 component++) {
                size_t count = 0u;
                size_t type;
                for (type = 0u; type < type_count; type++)
                    count += domains[component * type_count + type] != 0u;
                if (count == 0u)
                    return false;
            }
        }
    }
    return true;
}

static size_t gdl_source_component_degree(
    const GdlSourceAnalysisV1 *analysis,
    const GdlSourceTypeSolutionV1 *solution, size_t selected) {
    size_t constraint;
    size_t degree = 0u;
    for (constraint = 0u; constraint < analysis->acceptance_count;
         constraint++) {
        const GdlSourceAcceptanceV1 *acceptance =
            &analysis->acceptances[constraint];
        size_t actual = acceptance->actual.known
            ? SIZE_MAX
            : solution->component_by_variable[
                acceptance->actual.value.variable];
        size_t expected = acceptance->expected.known
            ? SIZE_MAX
            : solution->component_by_variable[
                acceptance->expected.value.variable];
        if (actual == selected && expected != SIZE_MAX &&
            expected != selected)
            degree++;
        else if (expected == selected && actual != SIZE_MAX &&
                 actual != selected)
            degree++;
    }
    return degree;
}

static GdlNativeBuildV1 gdl_source_domains_solve(
    const GdlSourceAnalysisV1 *analysis,
    const GdlSourceTypeSolutionV1 *solution, uint8_t *domains,
    size_t depth, size_t max_depth) {
    size_t selected = SIZE_MAX;
    size_t selected_count = SIZE_MAX;
    size_t selected_degree = 0u;
    size_t component;
    if (depth >= max_depth)
        return GDL_NATIVE_BUILD_RESOURCE_V1;
    if (!gdl_source_domains_reduce(analysis, solution, domains))
        return GDL_NATIVE_BUILD_OUTSIDE_V1;
    for (component = 0u; component < solution->component_count; component++) {
        size_t count = 0u;
        size_t type;
        size_t degree;
        for (type = 0u; type < solution->type_count; type++)
            count += domains[component * solution->type_count + type] != 0u;
        if (count <= 1u)
            continue;
        degree = gdl_source_component_degree(analysis, solution, component);
        if (degree == 0u) {
            bool retained = false;
            for (type = 0u; type < solution->type_count; type++) {
                uint8_t *entry = &domains[
                    component * solution->type_count + type];
                if (*entry && !retained)
                    retained = true;
                else
                    *entry = 0u;
            }
            continue;
        }
        if (selected == SIZE_MAX || count < selected_count ||
            (count == selected_count && degree > selected_degree) ||
            (count == selected_count && degree == selected_degree &&
             component < selected)) {
            selected = component;
            selected_count = count;
            selected_degree = degree;
        }
    }
    if (selected == SIZE_MAX)
        return GDL_NATIVE_BUILD_OK_V1;
    {
        size_t type;
        size_t domain_size = solution->component_count * solution->type_count;
        for (type = 0u; type < solution->type_count; type++) {
            uint8_t *branch;
            GdlNativeBuildV1 built;
            size_t candidate;
            if (!domains[selected * solution->type_count + type])
                continue;
            branch = cetta_malloc(domain_size);
            memcpy(branch, domains, domain_size);
            for (candidate = 0u; candidate < solution->type_count; candidate++)
                branch[selected * solution->type_count + candidate] =
                    candidate == type;
            built = gdl_source_domains_solve(
                analysis, solution, branch, depth + 1u, max_depth);
            if (built == GDL_NATIVE_BUILD_OK_V1) {
                memcpy(domains, branch, domain_size);
                free(branch);
                return built;
            }
            free(branch);
            if (built == GDL_NATIVE_BUILD_RESOURCE_V1)
                return built;
        }
    }
    return GDL_NATIVE_BUILD_OUTSIDE_V1;
}

static bool gdl_source_component_has_rule_variable(
    const GdlSourceAnalysisV1 *analysis,
    const GdlSourceTypeSolutionV1 *solution, size_t component) {
    size_t index;
    if (!analysis || !solution || component >= solution->component_count)
        return false;
    for (index = 0u; index < analysis->rule_variable_count; index++) {
        size_t variable = analysis->rule_variables[index].variable;
        if (variable < analysis->type_variable_count &&
            solution->component_by_variable[variable] == component)
            return true;
    }
    return false;
}

static bool gdl_source_component_unique_greatest(
    const GdlSourceTypeSolutionV1 *solution, const uint8_t *domains,
    size_t component, size_t *greatest_out, size_t *candidate_count_out) {
    size_t greatest = SIZE_MAX;
    size_t candidate_count = 0u;
    size_t candidate;
    if (greatest_out)
        *greatest_out = SIZE_MAX;
    if (candidate_count_out)
        *candidate_count_out = 0u;
    if (!solution || !domains || component >= solution->component_count ||
        !greatest_out || !candidate_count_out)
        return false;
    for (candidate = 0u; candidate < solution->type_count; candidate++) {
        size_t other;
        bool accepts_all = true;
        if (!domains[component * solution->type_count + candidate])
            continue;
        candidate_count++;
        for (other = 0u; other < solution->type_count; other++)
            if (domains[component * solution->type_count + other] &&
                !solution->accepts[
                    other * solution->type_count + candidate]) {
                accepts_all = false;
                break;
            }
        if (!accepts_all)
            continue;
        if (greatest != SIZE_MAX)
            return false;
        greatest = candidate;
    }
    *greatest_out = greatest;
    *candidate_count_out = candidate_count;
    return greatest != SIZE_MAX;
}

static GdlNativeBuildV1 gdl_source_solve_types(
    GdlSourceAnalysisV1 *analysis, const GdlSourceProfileV1 *profile,
    size_t max_depth, GdlSourceTypeSolutionV1 *solution) {
    size_t index;
    size_t matrix_size;
    GdlNativeBuildV1 built;
    if (!analysis || !profile || !solution || analysis->conflict ||
        analysis->type_name_count == 0u)
        return GDL_NATIVE_BUILD_OUTSIDE_V1;
    memset(solution, 0, sizeof(*solution));
    qsort(analysis->type_names, analysis->type_name_count,
          sizeof(*analysis->type_names), gdl_source_text_pointer_compare);
    solution->type_count = analysis->type_name_count;
    if (!gdl_source_solution_components(analysis, solution))
        goto resource;
    if (solution->component_count > SIZE_MAX / solution->type_count)
        goto resource;
    matrix_size = solution->component_count * solution->type_count;
    solution->domains = cetta_malloc(matrix_size);
    memset(solution->domains, 1, matrix_size);
    if (solution->type_count > SIZE_MAX / solution->type_count)
        goto resource;
    solution->accepts = cetta_malloc(
        solution->type_count * solution->type_count);
    memset(solution->accepts, 0,
           solution->type_count * solution->type_count);
    for (index = 0u; index < solution->type_count; index++)
        solution->accepts[index * solution->type_count + index] = 1u;
    for (index = 0u; index < profile->subtype_count; index++) {
        size_t subtype = gdl_source_type_name_index(
            analysis, profile->subtypes[index].subtype);
        size_t supertype = gdl_source_type_name_index(
            analysis, profile->subtypes[index].supertype);
        if (subtype == SIZE_MAX || supertype == SIZE_MAX)
            goto outside;
        solution->accepts[subtype * solution->type_count + supertype] = 1u;
    }
    {
        size_t middle;
        for (middle = 0u; middle < solution->type_count; middle++) {
            size_t actual;
            for (actual = 0u; actual < solution->type_count; actual++) {
                size_t expected;
                if (!solution->accepts[
                        actual * solution->type_count + middle])
                    continue;
                for (expected = 0u; expected < solution->type_count;
                     expected++)
                    if (solution->accepts[
                            middle * solution->type_count + expected])
                        solution->accepts[
                            actual * solution->type_count + expected] = 1u;
            }
        }
    }
    for (index = 0u; index < solution->type_count; index++) {
        size_t other;
        for (other = index + 1u; other < solution->type_count; other++)
            if (solution->accepts[
                    index * solution->type_count + other] &&
                solution->accepts[
                    other * solution->type_count + index])
                goto outside;
    }
    for (index = 0u; index < solution->component_count; index++) {
        const char *anchor = analysis->type_variables[
            solution->component_roots[index]].anchor;
        if (anchor) {
            size_t anchor_index = gdl_source_type_name_index(analysis, anchor);
            size_t type;
            if (anchor_index == SIZE_MAX)
                goto outside;
            for (type = 0u; type < solution->type_count; type++)
                solution->domains[index * solution->type_count + type] =
                    type == anchor_index;
        }
    }
    if (!gdl_source_domains_reduce(analysis, solution, solution->domains))
        goto outside;
    {
        uint8_t *existence_domains = cetta_malloc(matrix_size);
        uint8_t *greatest_domains = NULL;
        bool has_rule_variables = false;
        memcpy(existence_domains, solution->domains, matrix_size);
        built = gdl_source_domains_solve(
            analysis, solution, existence_domains, 0u, max_depth);
        if (built != GDL_NATIVE_BUILD_OK_V1) {
            free(existence_domains);
            gdl_source_solution_free(solution);
            return built;
        }

        solution->rule_variable_selection.kind =
            CETTA_GDL_RULE_VARIABLE_UNIQUE_GREATEST_V1;
        greatest_domains = cetta_malloc(matrix_size);
        memcpy(greatest_domains, solution->domains, matrix_size);
        for (index = 0u; index < solution->component_count; index++) {
            size_t greatest = SIZE_MAX;
            size_t candidate_count = 0u;
            size_t candidate;
            if (!gdl_source_component_has_rule_variable(
                    analysis, solution, index))
                continue;
            has_rule_variables = true;
            solution->rule_variable_selection.component_count++;
            if (!gdl_source_component_unique_greatest(
                    solution, solution->domains, index, &greatest,
                    &candidate_count)) {
                solution->rule_variable_selection.kind =
                    CETTA_GDL_RULE_VARIABLE_NO_COMMON_GREATEST_V1;
            } else {
                solution->rule_variable_selection.greatest_component_count++;
                for (candidate = 0u; candidate < solution->type_count;
                     candidate++)
                    greatest_domains[
                        index * solution->type_count + candidate] =
                        candidate == greatest;
            }
            solution->rule_variable_selection.candidate_count +=
                candidate_count;
            solution->rule_variable_selection.ambiguous_component_count +=
                candidate_count > 1u;
        }

        if (has_rule_variables &&
            solution->rule_variable_selection.kind ==
                CETTA_GDL_RULE_VARIABLE_UNIQUE_GREATEST_V1) {
            built = gdl_source_domains_solve(
                analysis, solution, greatest_domains, 0u, max_depth);
            if (built == GDL_NATIVE_BUILD_OK_V1) {
                free(existence_domains);
                existence_domains = greatest_domains;
                greatest_domains = NULL;
            } else if (built == GDL_NATIVE_BUILD_RESOURCE_V1) {
                solution->rule_variable_selection.kind =
                    CETTA_GDL_RULE_VARIABLE_SELECTION_INCOMPLETE_V1;
            } else {
                solution->rule_variable_selection.kind =
                    CETTA_GDL_RULE_VARIABLE_NO_COMMON_GREATEST_V1;
            }
        }
        free(greatest_domains);
        free(solution->domains);
        solution->domains = existence_domains;
    }
    solution->selected_types = cetta_malloc(
        solution->component_count * sizeof(*solution->selected_types));
    for (index = 0u; index < solution->component_count; index++) {
        size_t type;
        solution->selected_types[index] = SIZE_MAX;
        for (type = 0u; type < solution->type_count; type++)
            if (solution->domains[index * solution->type_count + type]) {
                if (solution->selected_types[index] != SIZE_MAX)
                    goto outside;
                solution->selected_types[index] = type;
            }
        if (solution->selected_types[index] == SIZE_MAX)
            goto outside;
    }
    return GDL_NATIVE_BUILD_OK_V1;

resource:
    gdl_source_solution_free(solution);
    return GDL_NATIVE_BUILD_RESOURCE_V1;
outside:
    gdl_source_solution_free(solution);
    return GDL_NATIVE_BUILD_OUTSIDE_V1;
}

static const char *gdl_source_resolved_type(
    const GdlSourceAnalysisV1 *analysis,
    const GdlSourceTypeSolutionV1 *solution, size_t variable) {
    size_t component;
    size_t type;
    if (!analysis || !solution || variable >= analysis->type_variable_count)
        return NULL;
    component = solution->component_by_variable[variable];
    if (component >= solution->component_count)
        return NULL;
    type = solution->selected_types[component];
    return type < analysis->type_name_count
        ? analysis->type_names[type]
        : NULL;
}

static bool gdl_source_rule_id_present(
    const CettaGdlTypeOfNativeV1 *native, const char *rule_id) {
    SymbolId key;
    uint32_t ignored;
    if (!native || !rule_id)
        return false;
    key = symbol_intern_cstr(g_symbols, rule_id);
    return key != SYMBOL_ID_NONE && cetta_gslt_u32_index_find_v1(
        &native->rule_ids, key, &ignored);
}

static GdlNativeBuildV1 gdl_source_add_signature(
    CettaGdlTypeOfNativeV1 *native, const char *rule_id,
    const char *name, const char *const *argument_types,
    size_t argument_count, const char *result_type) {
    GdlNativeSignatureV1 *signature;
    Atom **type_items = NULL;
    size_t index;
    if (!native || !rule_id || !name || !result_type)
        return GDL_NATIVE_BUILD_OUTSIDE_V1;
    if (gdl_source_rule_id_present(native, rule_id))
        return GDL_NATIVE_BUILD_OK_V1;
    if (!gdl_native_reserve(
            (void **)&native->signatures, &native->signature_capacity,
            native->signature_count + 1u, sizeof(*native->signatures)) ||
        !gdl_native_rule_id_insert(native, rule_id))
        return GDL_NATIVE_BUILD_RESOURCE_V1;
    if (argument_count > 0u) {
        type_items = cetta_malloc(argument_count * sizeof(*type_items));
        for (index = 0u; index < argument_count; index++) {
            type_items[index] = gdl_source_type_pattern(
                native, argument_types[index]);
            if (!type_items[index]) {
                free(type_items);
                return GDL_NATIVE_BUILD_RESOURCE_V1;
            }
        }
    }
    signature = &native->signatures[native->signature_count++];
    memset(signature, 0, sizeof(*signature));
    signature->name = gdl_source_name_pattern(native, name);
    signature->argument_types = gdl_native_object_list(
        native, type_items, argument_count);
    signature->result_type = gdl_source_type_pattern(native, result_type);
    signature->proof = gdl_native_proof(
        native, rule_id, NULL, 0u, NULL, 0u);
    free(type_items);
    if (!signature->name || !signature->argument_types ||
        !signature->result_type || !signature->proof)
        return GDL_NATIVE_BUILD_RESOURCE_V1;
    return GDL_NATIVE_BUILD_OK_V1;
}

static GdlNativeBuildV1 gdl_source_materialize_signatures(
    CettaGdlTypeOfNativeV1 *native, Arena *scratch,
    const GdlSourceProfileV1 *profile,
    const GdlSourceAnalysisV1 *analysis,
    const GdlSourceTypeSolutionV1 *solution) {
    size_t node_index;
    /* A target fibre selects executable source occurrences, not a smaller
     * declaration context.  Closed external values cross the typed ingress
     * using the complete authored profile; unused declarations establish no
     * source judgment and cannot revive an omitted malformed rule. */
    if (native->target_name)
        for (size_t signature_index = 0u;
             signature_index < profile->signature_count;
             signature_index++) {
            const GdlSourceSignatureV1 *signature =
                &profile->signatures[signature_index];
            const char *rule_id = gdl_source_format(
                scratch, "gdl:signature-authored:%zu:%zu",
                signature->statement_ordinal,
                signature->name_ordinal);
            GdlNativeBuildV1 built = gdl_source_add_signature(
                native, rule_id, signature->name,
                signature->argument_types,
                signature->argument_count,
                signature->result_type);
            if (built != GDL_NATIVE_BUILD_OK_V1)
                return built;
        }
    for (node_index = 0u; node_index < analysis->node_count; node_index++) {
        const GdlSourceAnalysisNodeV1 *node = &analysis->nodes[node_index];
        GdlNativeBuildV1 built;
        if (node->kind != GDL_SOURCE_TERM_APPLICATION_V1)
            continue;
        if (node->application_kind == GDL_SOURCE_APPLICATION_AUTHORED_V1) {
            size_t signature_index;
            for (signature_index = 0u;
                 signature_index < profile->signature_count;
                 signature_index++) {
                const GdlSourceSignatureV1 *signature =
                    &profile->signatures[signature_index];
                const char *rule_id;
                if (signature->argument_count != node->child_count ||
                    strcmp(signature->name, node->name) != 0)
                    continue;
                rule_id = gdl_source_format(
                    scratch, "gdl:signature-authored:%zu:%zu",
                    signature->statement_ordinal,
                    signature->name_ordinal);
                built = gdl_source_add_signature(
                    native, rule_id, signature->name,
                    signature->argument_types, signature->argument_count,
                    signature->result_type);
                if (built != GDL_NATIVE_BUILD_OK_V1)
                    return built;
            }
        } else if (
            node->application_kind ==
            GDL_SOURCE_APPLICATION_STRUCTURAL_V1) {
            const GdlSourceStructuralSignatureV1 *signature =
                gdl_source_structural_signature(
                    node->name, node->child_count);
            const char *encoded = gdl_source_hex_name(
                scratch, "", node->name);
            const char *rule_id = gdl_source_format(
                scratch, "gdl:signature-structural:%s:%zu",
                encoded, node->child_count);
            if (!signature)
                return GDL_NATIVE_BUILD_OUTSIDE_V1;
            built = gdl_source_add_signature(
                native, rule_id, signature->name,
                signature->argument_types, signature->argument_count,
                signature->result_type);
            if (built != GDL_NATIVE_BUILD_OK_V1)
                return built;
        } else if (
            node->application_kind == GDL_SOURCE_APPLICATION_DERIVED_V1) {
            const char **argument_types = NULL;
            const char *result_type;
            const char *encoded;
            const char *rule_id;
            size_t index;
            if (node->child_count > 0u)
                argument_types = cetta_malloc(
                    node->child_count * sizeof(*argument_types));
            for (index = 0u; index < node->child_count; index++) {
                size_t slot = SIZE_MAX;
                size_t slot_index;
                for (slot_index = 0u;
                     slot_index < analysis->derived_slot_count;
                     slot_index++)
                    if (analysis->derived_slots[slot_index].arity ==
                            node->child_count &&
                        analysis->derived_slots[slot_index].position == index &&
                        strcmp(analysis->derived_slots[slot_index].name,
                               node->name) == 0) {
                        slot = analysis->derived_slots[slot_index].variable;
                        break;
                    }
                argument_types[index] = gdl_source_resolved_type(
                    analysis, solution, slot);
                if (!argument_types[index]) {
                    free(argument_types);
                    return GDL_NATIVE_BUILD_OUTSIDE_V1;
                }
            }
            {
                size_t slot = SIZE_MAX;
                size_t slot_index;
                for (slot_index = 0u;
                     slot_index < analysis->derived_slot_count;
                     slot_index++)
                    if (analysis->derived_slots[slot_index].arity ==
                            node->child_count &&
                        analysis->derived_slots[slot_index].position ==
                            node->child_count &&
                        strcmp(analysis->derived_slots[slot_index].name,
                               node->name) == 0) {
                        slot = analysis->derived_slots[slot_index].variable;
                        break;
                    }
                result_type = gdl_source_resolved_type(
                    analysis, solution, slot);
            }
            encoded = gdl_source_hex_name(scratch, "", node->name);
            rule_id = gdl_source_format(
                scratch, "gdl:signature-extension:%s:%zu",
                encoded, node->child_count);
            built = result_type
                ? gdl_source_add_signature(
                    native, rule_id, node->name, argument_types,
                    node->child_count, result_type)
                : GDL_NATIVE_BUILD_OUTSIDE_V1;
            free(argument_types);
            if (built != GDL_NATIVE_BUILD_OK_V1)
                return built;
        } else {
            return GDL_NATIVE_BUILD_OUTSIDE_V1;
        }
    }
    return native->signature_count > 0u
        ? GDL_NATIVE_BUILD_OK_V1
        : GDL_NATIVE_BUILD_OUTSIDE_V1;
}

static GdlNativeBuildV1 gdl_source_materialize_variables(
    CettaGdlTypeOfNativeV1 *native, Arena *scratch,
    const GdlSourceAnalysisV1 *analysis,
    const GdlSourceTypeSolutionV1 *solution) {
    size_t node_index;
    for (node_index = 0u; node_index < analysis->node_count; node_index++) {
        const GdlSourceAnalysisNodeV1 *node = &analysis->nodes[node_index];
        const GdlSourceRuleVariableV1 *source_binding = NULL;
        GdlNativeVariableBindingV1 *binding;
        const char *type_name;
        const char *encoded;
        const char *rule_id;
        size_t binding_index;
        if (node->kind != GDL_SOURCE_TERM_VARIABLE_V1)
            continue;
        for (binding_index = 0u;
             binding_index < analysis->rule_variable_count;
             binding_index++)
            if (analysis->rule_variables[binding_index].form_ordinal ==
                    node->form_ordinal &&
                strcmp(analysis->rule_variables[binding_index].name,
                       node->name) == 0) {
                source_binding = &analysis->rule_variables[binding_index];
                break;
            }
        if (!source_binding)
            return GDL_NATIVE_BUILD_OUTSIDE_V1;
        encoded = gdl_source_hex_name(scratch, "", node->name);
        rule_id = gdl_source_format(
            scratch, "gdl:variable-type:%zu:%s",
            node->form_ordinal, encoded);
        if (gdl_source_rule_id_present(native, rule_id))
            continue;
        type_name = gdl_source_resolved_type(
            analysis, solution, source_binding->variable);
        if (!type_name || !gdl_native_reserve(
                (void **)&native->variables, &native->variable_capacity,
                native->variable_count + 1u, sizeof(*native->variables)) ||
            !gdl_native_rule_id_insert(native, rule_id))
            return GDL_NATIVE_BUILD_RESOURCE_V1;
        binding = &native->variables[native->variable_count++];
        memset(binding, 0, sizeof(*binding));
        binding->form = gdl_native_make_papp(
            native, gdl_source_format(
                &native->arena, "gdl:form:%zu", node->form_ordinal),
            NULL, 0u);
        binding->name = gdl_source_name_pattern(native, node->name);
        binding->type = gdl_source_type_pattern(native, type_name);
        binding->proof = gdl_native_proof(
            native, rule_id, NULL, 0u, NULL, 0u);
        if (!binding->form || !binding->name || !binding->type ||
            !binding->proof)
            return GDL_NATIVE_BUILD_RESOURCE_V1;
    }
    return GDL_NATIVE_BUILD_OK_V1;
}

static GdlNativeBuildV1 gdl_source_materialize_subtypes(
    CettaGdlTypeOfNativeV1 *native, Arena *scratch,
    const GdlSourceProfileV1 *profile) {
    size_t index;
    for (index = 0u; index < profile->subtype_count; index++) {
        const GdlSourceSubtypeV1 *source = &profile->subtypes[index];
        const char *rule_id = gdl_source_format(
            scratch, "gdl:subtype-authored:%zu",
            source->statement_ordinal);
        GdlNativeSubtypeEdgeV1 *edge;
        if (!gdl_native_reserve(
                (void **)&native->subtypes, &native->subtype_capacity,
                native->subtype_count + 1u, sizeof(*native->subtypes)) ||
            !gdl_native_rule_id_insert(native, rule_id))
            return GDL_NATIVE_BUILD_RESOURCE_V1;
        edge = &native->subtypes[native->subtype_count++];
        memset(edge, 0, sizeof(*edge));
        edge->subtype = gdl_source_type_pattern(native, source->subtype);
        edge->supertype = gdl_source_type_pattern(native, source->supertype);
        edge->proof = gdl_native_proof(
            native, rule_id, NULL, 0u, NULL, 0u);
        if (!edge->subtype || !edge->supertype || !edge->proof)
            return GDL_NATIVE_BUILD_RESOURCE_V1;
    }
    return GDL_NATIVE_BUILD_OK_V1;
}

static GdlNativeBuildV1 gdl_source_materialize_native(
    CettaGdlTypeOfNativeV1 *native, Arena *scratch,
    const GdlSourceRawFormsV1 *forms,
    const GdlSourceProfileV1 *profile,
    GdlSourceAnalysisV1 *analysis,
    const GdlSourceTypeSolutionV1 *solution) {
    size_t index;
    GdlNativeBuildV1 built;
    native->rule_variable_selection = solution->rule_variable_selection;
    built = gdl_source_materialize_signatures(
        native, scratch, profile, analysis, solution);
    if (built != GDL_NATIVE_BUILD_OK_V1)
        return built;
    built = gdl_source_materialize_variables(
        native, scratch, analysis, solution);
    if (built != GDL_NATIVE_BUILD_OK_V1)
        return built;
    built = gdl_source_materialize_subtypes(native, scratch, profile);
    if (built != GDL_NATIVE_BUILD_OK_V1 ||
        !gdl_native_subtypes_acyclic(native))
        return built != GDL_NATIVE_BUILD_OK_V1
            ? built
            : GDL_NATIVE_BUILD_OUTSIDE_V1;
    native->stats.source_forms =
        gdl_source_selected_form_count_v1(forms);
    native->stats.foreign_source_lines = forms->foreign_lines;
    native->stats.profile_statements = profile->statement_count;
    native->stats.typing_components = solution->component_count;
    native->stats.typing_acceptance_constraints = analysis->acceptance_count;
    native->stats.source_nodes = native->node_count;
    native->stats.signatures = native->signature_count;
    native->stats.variable_bindings = native->variable_count;
    native->stats.subtype_edges = native->subtype_count;
    for (index = 0u; index < native->node_count; index++) {
        built = gdl_native_derive_node(native, &native->nodes[index], 0u);
        if (built != GDL_NATIVE_BUILD_OK_V1)
            return built;
    }
    for (index = 0u; index < analysis->node_count; index++) {
        const GdlSourceAnalysisNodeV1 *analysis_node =
            &analysis->nodes[index];
        const char *expected_name = gdl_source_resolved_type(
            analysis, solution, analysis_node->type_variable);
        Atom *expected = expected_name
            ? gdl_source_type_pattern(native, expected_name)
            : NULL;
        GdlNativeSourceNodeV1 *node =
            &native->nodes[analysis_node->native_index];
        if (!expected || !node->type || !atom_eq(expected, node->type))
            return GDL_NATIVE_BUILD_OUTSIDE_V1;
        node->type_name = gdl_source_format(
            &native->arena, "%s", expected_name);
        if (!node->type_name)
            return GDL_NATIVE_BUILD_RESOURCE_V1;
    }
    return GDL_NATIVE_BUILD_OK_V1;
}

static void gdl_source_analysis_free(GdlSourceAnalysisV1 *analysis) {
    if (!analysis)
        return;
    free(analysis->nodes);
    free(analysis->type_variables);
    free(analysis->rule_variables);
    free(analysis->derived_slots);
    free(analysis->acceptances);
    free(analysis->type_names);
    memset(analysis, 0, sizeof(*analysis));
}

 static char *gdl_native_strdup(const char *text) {
    size_t length;
    char *copy;
    if (!text)
        return NULL;
    length = strlen(text);
    if (length == SIZE_MAX)
        return NULL;
    copy = cetta_malloc(length + 1u);
    memcpy(copy, text, length + 1u);
    return copy;
}

static uint8_t gdl_native_hex_digit(char digit) {
    if (digit >= '0' && digit <= '9')
        return (uint8_t)(digit - '0');
    return (uint8_t)(digit - 'a' + 10);
}

static bool gdl_native_sha_atom(
    CettaNativeSha256 *sha, Arena *scratch, Atom *atom) {
    char *text;
    uint64_t length;
    uint8_t length_bytes[8];
    size_t index;
    if (!sha || !scratch || !atom)
        return false;
    text = atom_to_parseable_string(scratch, atom);
    if (!text)
        return false;
    length = (uint64_t)strlen(text);
    for (index = 0u; index < sizeof(length_bytes); index++)
        length_bytes[index] = (uint8_t)(length >> (index * 8u));
    cetta_native_sha256_update(sha, length_bytes, sizeof(length_bytes));
    cetta_native_sha256_update(
        sha, (const uint8_t *)text, (size_t)length);
    return true;
}

static bool gdl_native_digest_atoms(
    const char *domain, Atom *const *atoms, size_t atom_count,
    char digest[65]) {
    Arena scratch;
    CettaNativeSha256 sha;
    size_t index;
    bool valid = domain && atoms && digest;
    if (!valid)
        return false;
    arena_init(&scratch);
    cetta_native_sha256_init(&sha);
    cetta_native_sha256_update(
        &sha, (const uint8_t *)domain, strlen(domain) + 1u);
    for (index = 0u; valid && index < atom_count; index++)
        valid = gdl_native_sha_atom(&sha, &scratch, atoms[index]);
    if (valid)
        cetta_native_sha256_finish_hex(&sha, digest);
    arena_free(&scratch);
    return valid;
}

static bool gdl_native_core_rules_exact(Atom *rules) {
    static const char domain[] = "cetta.gdl-type-of-native-core.v1";
    static const char expected_digest[] =
        "2086801fdcc5328382a4838f9c2aa0147fa95f48636eb3591643e114e6db5fb4";
    CettaNativeSha256 sha;
    Arena scratch;
    char digest[65];
    size_t index;
    bool valid = gdl_native_expr_head(
        rules, "rules",
        (CettaExprLen)(sizeof(g_core_rules) / sizeof(g_core_rules[0])) + 1u);
    if (!valid)
        return false;
    arena_init(&scratch);
    cetta_native_sha256_init(&sha);
    cetta_native_sha256_update(
        &sha, (const uint8_t *)domain, sizeof(domain));
    for (index = 0u;
         valid && index < sizeof(g_core_rules) / sizeof(g_core_rules[0]);
         index++)
        valid = gdl_native_sha_atom(
            &sha, &scratch, rules->expr.elems[index + 1u]);
    if (valid) {
        cetta_native_sha256_finish_hex(&sha, digest);
        valid = strcmp(digest, expected_digest) == 0;
    }
    arena_free(&scratch);
    return valid;
}

static bool gdl_native_build_token(CettaGdlTypeOfNativeV1 *native) {
    static const char domain[] = "gdl-type-of-native-v1";
    CettaNativeSha256 sha;
    char digest[65];
    CettaNikDirectAuthorityTokenV1 suffix = {0};
    size_t word_index;
    if (!native || !native->revision)
        return false;
    cetta_native_sha256_init(&sha);
    cetta_native_sha256_update(
        &sha, (const uint8_t *)domain, sizeof(domain));
    cetta_native_sha256_update(
        &sha, (const uint8_t *)native->source_sha256,
        sizeof(native->source_sha256));
    cetta_native_sha256_update(
        &sha, (const uint8_t *)native->profile_sha256,
        sizeof(native->profile_sha256));
    cetta_native_sha256_update(
        &sha, (const uint8_t *)native->calculus_input_sha256,
        sizeof(native->calculus_input_sha256));
    cetta_native_sha256_update(
        &sha, (const uint8_t *)native->revision,
        strlen(native->revision) + 1u);
    cetta_native_sha256_finish_hex(&sha, digest);
    suffix.length = 4u;
    for (word_index = 0u; word_index < 4u; word_index++) {
        uint64_t word = 0u;
        size_t digit_index;
        for (digit_index = 0u; digit_index < 16u; digit_index++)
            word = (word << 4u) |
                gdl_native_hex_digit(digest[word_index * 16u + digit_index]);
        suffix.words[word_index] = word;
    }
    return cetta_nik_direct_authority_v1_token(
        &g_gdl_type_of_native_authority_v1, 1u, &suffix, &native->token);
}

static bool gdl_native_judgments_exact(Atom *judgments) {
    static const struct {
        const char *name;
        size_t arity;
    } expected[] = {
        {"type:of", 3u},
        {"gdl:source-node", 2u},
        {"gdl:source-children", 2u},
        {"gdl:source-form", 2u},
        {"gdl:signature", 3u},
        {"gdl:variable-type", 3u},
        {"gdl:subtype", 2u},
        {"gdl:accepts", 2u},
        {"gdl:arguments-type", 3u},
        {"gdl:arguments-typed", 2u},
        {"gdl:all-type", 2u},
        {"gdl:literal", 2u},
    };
    size_t count;
    size_t index;
    if (!gdl_native_wire_list_count(judgments, &count) ||
        count != sizeof(expected) / sizeof(expected[0]))
        return false;
    for (index = 0u; index < count; index++) {
        Atom *decl;
        const char *name;
        size_t arity;
        if (!gdl_native_wire_list_item(judgments, index, &decl) ||
            !gdl_native_expr_named(decl, "JDecl", 3u) ||
            !gdl_native_string(decl->expr.elems[1], &name) ||
            !gdl_native_nonnegative_size(decl->expr.elems[2], &arity) ||
            strcmp(name, expected[index].name) != 0 ||
            arity != expected[index].arity)
            return false;
    }
    return true;
}

static GdlNativeBuildV1 gdl_native_load_presentation(
    CettaGdlTypeOfNativeV1 *native, Atom *presentation, Atom *rules) {
    size_t version;
    size_t constructor_count;
    size_t rule_count;
    size_t index;
    if (!native ||
        !gdl_native_expr_named(presentation, "GPresentationV1", 6u) ||
        !gdl_native_nonnegative_size(presentation->expr.elems[1], &version) ||
        version != 1u ||
        !gdl_native_wire_list_count(
            presentation->expr.elems[2], &constructor_count) ||
        constructor_count == 0u ||
        !gdl_native_judgments_exact(presentation->expr.elems[3]) ||
        !atom_is_symbol(presentation->expr.elems[4], "LNil") ||
        !gdl_native_expr_head(rules, "rules", 2u) ||
        !gdl_native_core_rules_exact(rules) ||
        !atom_is_symbol(presentation->expr.elems[5], "GNoConversion"))
        return GDL_NATIVE_BUILD_OUTSIDE_V1;
    rule_count = (size_t)rules->expr.len - 1u;
    if (rule_count <= sizeof(g_core_rules) / sizeof(g_core_rules[0]))
        return GDL_NATIVE_BUILD_OUTSIDE_V1;
    for (index = 0u; index < rule_count; index++) {
        Atom *rule = rules->expr.elems[index + 1u];
        GdlNativeBuildV1 built;
        if (index < sizeof(g_core_rules) / sizeof(g_core_rules[0])) {
            if (!gdl_native_rule_shape(rule, &g_core_rules[index]) ||
                !gdl_native_rule_id_insert(native, g_core_rules[index].id))
                return GDL_NATIVE_BUILD_OUTSIDE_V1;
            continue;
        }
        built = gdl_native_add_fact(native, rule);
        if (built != GDL_NATIVE_BUILD_OK_V1)
            return built;
    }
    if (native->node_count == 0u || native->signature_count == 0u ||
        !cetta_gslt_u32_index_validate_v1(&native->node_index) ||
        !cetta_gslt_u32_index_validate_v1(&native->rule_ids) ||
        !gdl_native_subtypes_acyclic(native))
        return GDL_NATIVE_BUILD_OUTSIDE_V1;
    native->stats.source_nodes = native->node_count;
    native->stats.signatures = native->signature_count;
    native->stats.variable_bindings = native->variable_count;
    native->stats.subtype_edges = native->subtype_count;
    for (index = 0u; index < native->node_count; index++) {
        GdlNativeBuildV1 built = gdl_native_derive_node(
            native, &native->nodes[index], 0u);
        if (built != GDL_NATIVE_BUILD_OK_V1)
            return built;
    }
    return GDL_NATIVE_BUILD_OK_V1;
}

static CettaGdlTypeOfNativeAdmissionV1 gdl_native_admission_result(
    CettaGdlTypeOfNativeAdmissionKindV1 kind,
    CettaGdlTypeOfNativeV1 *native) {
    CettaGdlTypeOfNativeAdmissionV1 result = {
        .kind = kind,
        .native = native,
    };
    return result;
}

const CettaNikDirectAuthorityV1 *
cetta_gdl_type_of_native_authority_v1(void) {
    return &g_gdl_type_of_native_authority_v1;
}

CettaGdlTypeOfNativeAdmissionV1 cetta_gdl_type_of_native_admit_v1(
    Atom *program,
    const char *expected_source_sha256,
    const char *expected_profile_sha256,
    const char *expected_revision,
    CettaGdlTypeOfNativeLimitsV1 limits) {
    CettaGdlTypeOfNativeV1 *native;
    Atom *source_field;
    Atom *profile_field;
    Atom *revision_field;
    Atom *presentation_field;
    Atom *rules_field;
    const char *source_sha256;
    const char *profile_sha256;
    const char *revision;
    GdlNativeBuildV1 built;
    if (!program || !expected_source_sha256 || !expected_profile_sha256 ||
        !expected_revision || !g_symbols)
        return gdl_native_admission_result(
            CETTA_GDL_TYPE_OF_NATIVE_ENGINE_FAULT_V1, NULL);
    if (!gdl_native_hex_digest(expected_source_sha256) ||
        !gdl_native_hex_digest(expected_profile_sha256) ||
        !gdl_native_expr_named(
            program, "gdl-type-of-inference-v2", 8u))
        return gdl_native_admission_result(
            CETTA_GDL_TYPE_OF_NATIVE_OUTSIDE_FRAGMENT_V1, NULL);
    source_field = program->expr.elems[1];
    profile_field = program->expr.elems[2];
    revision_field = program->expr.elems[3];
    presentation_field = program->expr.elems[4];
    rules_field = program->expr.elems[5];
    if (!gdl_native_expr_named(source_field, "source-digest", 2u) ||
        !gdl_native_string(source_field->expr.elems[1], &source_sha256) ||
        !gdl_native_expr_named(profile_field, "profile-digest", 2u) ||
        !gdl_native_string(profile_field->expr.elems[1], &profile_sha256) ||
        !gdl_native_expr_named(revision_field, "revision", 2u) ||
        revision_field->expr.elems[1]->kind != ATOM_SYMBOL ||
        !(revision = symbol_bytes(
              g_symbols, revision_field->expr.elems[1]->sym_id)) ||
        !gdl_native_expr_named(presentation_field, "presentation", 2u) ||
        !gdl_native_expr_head(rules_field, "rules", 2u) ||
        !gdl_native_expr_named(program->expr.elems[6], "rule-package", 2u) ||
        !gdl_native_expr_head(program->expr.elems[7], "cases", 2u) ||
        strcmp(source_sha256, expected_source_sha256) != 0 ||
        strcmp(profile_sha256, expected_profile_sha256) != 0 ||
        strcmp(revision, expected_revision) != 0)
        return gdl_native_admission_result(
            CETTA_GDL_TYPE_OF_NATIVE_OUTSIDE_FRAGMENT_V1, NULL);
    native = cetta_malloc(sizeof(*native));
    memset(native, 0, sizeof(*native));
    arena_init(&native->arena);
    arena_set_runtime_kind(
        &native->arena, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    cetta_gslt_u32_index_init_v1(&native->node_index);
    cetta_gslt_u32_index_init_v1(&native->rule_ids);
    native->copy = atom_deep_copy_session_new(&native->arena);
    native->limits.max_source_nodes = limits.max_source_nodes
        ? limits.max_source_nodes
        : GDL_TYPE_OF_NATIVE_DEFAULT_SOURCE_NODES;
    native->limits.max_proof_nodes = limits.max_proof_nodes
        ? limits.max_proof_nodes
        : GDL_TYPE_OF_NATIVE_DEFAULT_PROOF_NODES;
    native->limits.max_derivation_depth = limits.max_derivation_depth
        ? limits.max_derivation_depth
        : GDL_TYPE_OF_NATIVE_DEFAULT_DERIVATION_DEPTH;
    memcpy(native->source_sha256, source_sha256, 65u);
    memcpy(native->profile_sha256, profile_sha256, 65u);
    native->revision = gdl_native_strdup(revision);
    native->bool_type = gdl_native_make_papp(
        native, "gdl:type:626f6f6c", NULL, 0u);
    if (!native->copy || !native->revision || !native->bool_type) {
        cetta_gdl_type_of_native_destroy_v1(native);
        return gdl_native_admission_result(
            CETTA_GDL_TYPE_OF_NATIVE_ENGINE_FAULT_V1, NULL);
    }
    {
        Atom *calculus_inputs[] = {
            presentation_field->expr.elems[1], rules_field,
        };
        if (!gdl_native_digest_atoms(
                "cetta.gdl-type-of-native-input.v1", calculus_inputs,
                sizeof(calculus_inputs) / sizeof(calculus_inputs[0]),
                native->calculus_input_sha256) ||
            !gdl_native_build_token(native)) {
            cetta_gdl_type_of_native_destroy_v1(native);
            return gdl_native_admission_result(
                CETTA_GDL_TYPE_OF_NATIVE_ENGINE_FAULT_V1, NULL);
        }
    }
    built = gdl_native_load_presentation(
        native, presentation_field->expr.elems[1], rules_field);
    atom_deep_copy_session_free(native->copy);
    native->copy = NULL;
    if (built != GDL_NATIVE_BUILD_OK_V1) {
        CettaGdlTypeOfNativeAdmissionKindV1 kind =
            built == GDL_NATIVE_BUILD_RESOURCE_V1 ||
                    native->resource_exhausted
                ? CETTA_GDL_TYPE_OF_NATIVE_INCOMPLETE_V1
                : CETTA_GDL_TYPE_OF_NATIVE_OUTSIDE_FRAGMENT_V1;
        cetta_gdl_type_of_native_destroy_v1(native);
        return gdl_native_admission_result(kind, NULL);
    }
    return gdl_native_admission_result(
        CETTA_GDL_TYPE_OF_NATIVE_ADMITTED_V1, native);
}

void cetta_gdl_type_of_native_destroy_v1(CettaGdlTypeOfNativeV1 *native) {
    size_t index;
    if (!native)
        return;
    if (native->copy)
        atom_deep_copy_session_free(native->copy);
    for (index = 0u; index < native->node_count; index++) {
        gdl_native_proof_bag_free(&native->nodes[index].type_proofs);
        gdl_native_proof_bag_free(&native->nodes[index].literal_proofs);
    }
    free(native->nodes);
    free(native->signatures);
    free(native->variables);
    free(native->subtypes);
    free(native->revision);
    free(native->target_name);
    cetta_gslt_u32_index_free_v1(&native->node_index);
    cetta_gslt_u32_index_free_v1(&native->rule_ids);
    arena_free(&native->arena);
    free(native);
}

bool cetta_gdl_type_of_native_token_v1(
    const CettaGdlTypeOfNativeV1 *native,
    CettaNikDirectAuthorityTokenV1 *token_out) {
    if (!native || !token_out)
        return false;
    *token_out = native->token;
    return true;
}

bool cetta_gdl_type_of_native_token_is_current_v1(
    const CettaGdlTypeOfNativeV1 *native,
    const CettaNikDirectAuthorityTokenV1 *token) {
    return native && token &&
        cetta_nik_direct_authority_token_v1_equal(&native->token, token);
}

bool cetta_gdl_type_of_native_identity_v1(
    const CettaGdlTypeOfNativeV1 *native,
    const char **source_sha256_out,
    const char **profile_sha256_out,
    const char **revision_out) {
    if (!native || !source_sha256_out || !profile_sha256_out ||
        !revision_out)
        return false;
    *source_sha256_out = native->source_sha256;
    *profile_sha256_out = native->profile_sha256;
    *revision_out = native->revision;
    return true;
}

bool cetta_gdl_type_of_native_target_slice_v1(
    const CettaGdlTypeOfNativeV1 *native,
    const char **target_name_out,
    size_t *target_arity_out,
    size_t *source_forms_out,
    size_t *selected_forms_out,
    size_t *reachable_relations_out,
    size_t *external_relations_out) {
    if (!native || !native->target_name || !target_name_out ||
        !target_arity_out || !source_forms_out || !selected_forms_out ||
        !reachable_relations_out || !external_relations_out)
        return false;
    *target_name_out = native->target_name;
    *target_arity_out = native->target_arity;
    *source_forms_out = native->target_source_forms;
    *selected_forms_out = native->target_selected_forms;
    *reachable_relations_out = native->target_reachable_relations;
    *external_relations_out = native->target_external_relations;
    return true;
}

bool cetta_gdl_type_of_native_rule_variable_selection_v1(
    const CettaGdlTypeOfNativeV1 *native,
    CettaGdlRuleVariableSelectionV1 *selection_out) {
    if (selection_out)
        memset(selection_out, 0, sizeof(*selection_out));
    if (!native || !selection_out ||
        native->rule_variable_selection.kind == 0)
        return false;
    *selection_out = native->rule_variable_selection;
    return true;
}

static CettaGdlTypeOfNativeQueryV1 gdl_native_query_outcome(
    CettaNikOutcomeV1 outcome, Atom *const *proofs, size_t proof_count,
    Atom *type) {
    CettaGdlTypeOfNativeQueryV1 result = {
        .kind = CETTA_GDL_TYPE_OF_NATIVE_QUERY_OUTCOME_V1,
        .selection = {
            .status = CETTA_NIK_IMPLEMENTATION_SELECTION_STATUS_OK_V1,
            .kind = CETTA_NIK_IMPLEMENTATION_SELECTION_NONE_V1,
            .greatest_index = SIZE_MAX,
        },
        .value.outcome = outcome,
        .proofs = proofs,
        .proof_count = proof_count,
        .type = type,
    };
    return result;
}

static CettaGdlTypeOfNativeQueryV1 gdl_native_construct_v1(
    const CettaGdlTypeOfNativeV1 *native,
    const CettaNikDirectAuthorityTokenV1 *token,
    Atom *judgment,
    size_t max_proofs) {
    const char *head;
    Atom *arguments;
    Atom *occurrence;
    Atom *term;
    GdlNativeSourceNodeV1 *node;
    GdlNativeProofBagV1 *bag;
    if (!native || !token || !judgment) {
        CettaGdlTypeOfNativeQueryV1 fault = {
            .kind = CETTA_GDL_TYPE_OF_NATIVE_QUERY_ENGINE_FAULT_V1,
            .selection = {
                .status = CETTA_NIK_IMPLEMENTATION_SELECTION_STATUS_OK_V1,
                .kind = CETTA_NIK_IMPLEMENTATION_SELECTION_NONE_V1,
                .greatest_index = SIZE_MAX,
            },
            .value.fault = CETTA_NIK_ENGINE_FAULT_UNAVAILABLE,
        };
        return fault;
    }
    if (!cetta_gdl_type_of_native_token_is_current_v1(native, token)) {
        CettaGdlTypeOfNativeQueryV1 stale = {
            .kind = CETTA_GDL_TYPE_OF_NATIVE_QUERY_STALE_V1,
            .selection = {
                .status = CETTA_NIK_IMPLEMENTATION_SELECTION_STATUS_OK_V1,
                .kind = CETTA_NIK_IMPLEMENTATION_SELECTION_NONE_V1,
                .greatest_index = SIZE_MAX,
            },
        };
        return stale;
    }
    if (!gdl_native_papp(judgment, &head, &arguments))
        return gdl_native_query_outcome(
            CETTA_NIK_OUTCOME_OUTSIDE_FRAGMENT, NULL, 0u, NULL);
    if (strcmp(head, "type:of") == 0) {
        Atom *expected_type;
        if (!gdl_native_papp_arity(judgment, "type:of", 3u, &arguments) ||
            !gdl_native_wire_list_item(arguments, 0u, &occurrence) ||
            !gdl_native_wire_list_item(arguments, 1u, &term) ||
            !gdl_native_wire_list_item(arguments, 2u, &expected_type) ||
            !(node = gdl_native_node_find(
                  (CettaGdlTypeOfNativeV1 *)native, occurrence)) ||
            !atom_eq(node->term, term) || !atom_eq(node->type, expected_type))
            return gdl_native_query_outcome(
                CETTA_NIK_OUTCOME_OUTSIDE_FRAGMENT, NULL, 0u, NULL);
        bag = &node->type_proofs;
    } else if (strcmp(head, "gdl:literal") == 0) {
        if (!gdl_native_papp_arity(
                judgment, "gdl:literal", 2u, &arguments) ||
            !gdl_native_wire_list_item(arguments, 0u, &occurrence) ||
            !gdl_native_wire_list_item(arguments, 1u, &term) ||
            !(node = gdl_native_node_find(
                  (CettaGdlTypeOfNativeV1 *)native, occurrence)) ||
            !atom_eq(node->term, term))
            return gdl_native_query_outcome(
                CETTA_NIK_OUTCOME_OUTSIDE_FRAGMENT, NULL, 0u, NULL);
        bag = &node->literal_proofs;
    } else {
        return gdl_native_query_outcome(
            CETTA_NIK_OUTCOME_OUTSIDE_FRAGMENT, NULL, 0u, NULL);
    }
    if (bag->count == 0u)
        return gdl_native_query_outcome(
            CETTA_NIK_OUTCOME_OUTSIDE_FRAGMENT, NULL, 0u, NULL);
    if (max_proofs != 0u && bag->count > max_proofs)
        return gdl_native_query_outcome(
            CETTA_NIK_OUTCOME_INCOMPLETE, NULL, 0u, NULL);
    return gdl_native_query_outcome(
        CETTA_NIK_OUTCOME_ESTABLISHED, bag->items, bag->count,
        strcmp(head, "type:of") == 0 ? node->type : NULL);
}

static CettaNikImplementationSelectionV1 gdl_native_select_v1(
    size_t *frontier_index_out) {
    const CettaNikLicensedImplementationV1 implementation = {
        .calculus_identity =
            g_gdl_type_of_native_authority_v1.authority_identity,
        .implementation_identity =
            g_gdl_type_of_native_authority_v1.realization_identity,
        .capabilities = g_gdl_type_of_native_capabilities_v1,
        .capability_count =
            sizeof(g_gdl_type_of_native_capabilities_v1) /
            sizeof(g_gdl_type_of_native_capabilities_v1[0]),
    };
    const CettaNikLicensedImplementationFamilyV1 family = {
        .implementations = &implementation,
        .implementation_count = 1u,
    };
    const CettaNikImplementationCapabilityRequestV1 request = {
        .required_capabilities = g_gdl_type_of_native_capabilities_v1,
        .required_capability_count = implementation.capability_count,
    };
    return cetta_nik_licensed_implementation_select_v1(
        &family, &request, frontier_index_out, 1u);
}

static bool gdl_native_selection_is_unique_greatest_v1(
    const CettaNikImplementationSelectionV1 *selection, size_t frontier_index) {
    return selection &&
        selection->status == CETTA_NIK_IMPLEMENTATION_SELECTION_STATUS_OK_V1 &&
        selection->kind == CETTA_NIK_IMPLEMENTATION_SELECTION_UNIQUE_GREATEST_V1 &&
        selection->eligible_count == 1u &&
        selection->frontier_count == 1u &&
        selection->greatest_index == 0u && frontier_index == 0u;
}

static CettaGdlTypeOfNativeQueryV1 gdl_native_selected_result_v1(
    CettaGdlTypeOfNativeQueryV1 result,
    CettaNikImplementationSelectionV1 selection) {
    result.selection = selection;
    if (result.kind == CETTA_GDL_TYPE_OF_NATIVE_QUERY_OUTCOME_V1)
        result.selected_realization_identity =
            g_gdl_type_of_native_authority_v1.realization_identity;
    return result;
}

CettaGdlTypeOfNativeQueryV1 cetta_gdl_type_of_native_serve_v1(
    const CettaGdlTypeOfNativeV1 *native,
    const CettaNikDirectAuthorityTokenV1 *token,
    Atom *judgment,
    size_t max_proofs) {
    if (!native || !token || !judgment)
        return gdl_native_construct_v1(
            native, token, judgment, max_proofs);
    if (!cetta_gdl_type_of_native_token_is_current_v1(native, token))
        return gdl_native_construct_v1(
            native, token, judgment, max_proofs);

    size_t frontier_index = SIZE_MAX;
    CettaNikImplementationSelectionV1 selection = gdl_native_select_v1(
        &frontier_index);
    if (!gdl_native_selection_is_unique_greatest_v1(
            &selection, frontier_index)) {
        CettaGdlTypeOfNativeQueryV1 fault = {
            .kind = CETTA_GDL_TYPE_OF_NATIVE_QUERY_ENGINE_FAULT_V1,
            .selection = selection,
            .value.fault = CETTA_NIK_ENGINE_FAULT_UNAVAILABLE,
        };
        return fault;
    }

    return gdl_native_selected_result_v1(
        gdl_native_construct_v1(native, token, judgment, max_proofs),
        selection);
}

CettaGdlTypeOfNativeQueryV1 cetta_gdl_type_of_native_synthesize_v1(
    const CettaGdlTypeOfNativeV1 *native,
    const CettaNikDirectAuthorityTokenV1 *token,
    Atom *occurrence, Atom *term, size_t max_proofs) {
    if (!native || !token || !occurrence || !term) {
        CettaGdlTypeOfNativeQueryV1 fault = {
            .kind = CETTA_GDL_TYPE_OF_NATIVE_QUERY_ENGINE_FAULT_V1,
            .selection = {
                .status = CETTA_NIK_IMPLEMENTATION_SELECTION_STATUS_OK_V1,
                .kind = CETTA_NIK_IMPLEMENTATION_SELECTION_NONE_V1,
                .greatest_index = SIZE_MAX,
            },
            .value.fault = CETTA_NIK_ENGINE_FAULT_UNAVAILABLE,
        };
        return fault;
    }
    if (!cetta_gdl_type_of_native_token_is_current_v1(native, token)) {
        CettaGdlTypeOfNativeQueryV1 stale = {
            .kind = CETTA_GDL_TYPE_OF_NATIVE_QUERY_STALE_V1,
            .selection = {
                .status = CETTA_NIK_IMPLEMENTATION_SELECTION_STATUS_OK_V1,
                .kind = CETTA_NIK_IMPLEMENTATION_SELECTION_NONE_V1,
                .greatest_index = SIZE_MAX,
            },
        };
        return stale;
    }
    size_t frontier_index = SIZE_MAX;
    CettaNikImplementationSelectionV1 selection = gdl_native_select_v1(
        &frontier_index);
    if (!gdl_native_selection_is_unique_greatest_v1(
            &selection, frontier_index)) {
        CettaGdlTypeOfNativeQueryV1 fault = {
            .kind = CETTA_GDL_TYPE_OF_NATIVE_QUERY_ENGINE_FAULT_V1,
            .selection = selection,
            .value.fault = CETTA_NIK_ENGINE_FAULT_UNAVAILABLE,
        };
        return fault;
    }
    GdlNativeSourceNodeV1 *node = gdl_native_node_find(
        (CettaGdlTypeOfNativeV1 *)native, occurrence);
    if (!node || !atom_eq(node->term, term) || !node->type ||
        node->type_proofs.count == 0u) {
        return gdl_native_selected_result_v1(
            gdl_native_query_outcome(
                CETTA_NIK_OUTCOME_OUTSIDE_FRAGMENT, NULL, 0u, NULL),
            selection);
    }
    if (max_proofs != 0u && node->type_proofs.count > max_proofs) {
        return gdl_native_selected_result_v1(
            gdl_native_query_outcome(
                CETTA_NIK_OUTCOME_INCOMPLETE, NULL, 0u, NULL),
            selection);
    }
    return gdl_native_selected_result_v1(
        gdl_native_query_outcome(
            CETTA_NIK_OUTCOME_ESTABLISHED,
            node->type_proofs.items, node->type_proofs.count, node->type),
        selection);
}

typedef struct {
    Atom *type;
    Atom *proof;
} GdlNativeGroundTypingV1;

typedef struct {
    GdlNativeGroundTypingV1 *items;
    size_t count;
    size_t capacity;
} GdlNativeGroundTypingBagV1;

typedef struct {
    Atom **items;
    size_t count;
    size_t capacity;
} GdlNativeGroundProofBagV1;

typedef struct {
    const CettaGdlTypeOfNativeV1 *native;
    Arena *arena;
    size_t proof_nodes;
    size_t proof_node_limit;
    bool incomplete;
} GdlNativeGroundContextV1;

static bool gdl_native_ground_typing_append_v1(
    GdlNativeGroundTypingBagV1 *bag, Atom *type, Atom *proof) {
    if (!bag || !type || !proof || !gdl_native_reserve(
            (void **)&bag->items, &bag->capacity, bag->count + 1u,
            sizeof(*bag->items)))
        return false;
    bag->items[bag->count++] = (GdlNativeGroundTypingV1){
        .type = type,
        .proof = proof,
    };
    return true;
}

static bool gdl_native_ground_proof_append_v1(
    GdlNativeGroundProofBagV1 *bag, Atom *proof) {
    if (!bag || !proof || !gdl_native_reserve(
            (void **)&bag->items, &bag->capacity, bag->count + 1u,
            sizeof(*bag->items)))
        return false;
    bag->items[bag->count++] = proof;
    return true;
}

static Atom *gdl_native_ground_expr_v1(
    Arena *arena, const char *head, Atom *const *arguments,
    size_t argument_count) {
    Atom **items;
    size_t index;
    if (!arena || !head || !*head ||
        argument_count > (size_t)UINT32_MAX - 1u)
        return NULL;
    items = arena_alloc(arena, (argument_count + 1u) * sizeof(*items));
    items[0] = atom_symbol(arena, head);
    for (index = 0u; index < argument_count; index++)
        items[index + 1u] = arguments[index];
    return atom_expr(arena, items, (CettaExprLen)(argument_count + 1u));
}

static Atom *gdl_native_ground_proof_v1(
    GdlNativeGroundContextV1 *context, const char *constructor,
    Atom *const *arguments, size_t argument_count) {
    if (!context || !constructor ||
        context->proof_nodes >= context->proof_node_limit) {
        if (context)
            context->incomplete = true;
        return NULL;
    }
    Atom *proof = gdl_native_ground_expr_v1(
        context->arena, constructor, arguments, argument_count);
    if (proof)
        context->proof_nodes++;
    return proof;
}

static Atom *gdl_native_ground_papp_v1(
    Arena *arena, const char *head, Atom *const *arguments,
    size_t argument_count) {
    Atom *parts[3];
    if (!arena || !head || !*head)
        return NULL;
    parts[0] = atom_symbol(arena, "PApp");
    parts[1] = atom_string(arena, head);
    parts[2] = gdl_native_wire_list(
        arena, "LNil", "LCons", arguments, argument_count);
    return parts[0] && parts[1] && parts[2]
        ? atom_expr(arena, parts, 3u)
        : NULL;
}

static bool gdl_native_ground_head_v1(
    GdlNativeGroundContextV1 *context, Atom *term,
    const char **name_out, Atom ***arguments_out, size_t *arity_out) {
    if (!context || !term || !name_out || !arguments_out || !arity_out)
        return false;
    if (term->kind == ATOM_SYMBOL) {
        *name_out = atom_name_cstr(term);
        *arguments_out = NULL;
        *arity_out = 0u;
        return **name_out != '\0';
    }
    if (term->kind == ATOM_GROUNDED && term->ground.gkind == GV_INT) {
        *name_out = gdl_source_format(
            context->arena, "%" PRId64, term->ground.ival);
        *arguments_out = NULL;
        *arity_out = 0u;
        return *name_out != NULL;
    }
    if (term->kind != ATOM_EXPR || term->expr.len == 0u ||
        term->expr.elems[0]->kind != ATOM_SYMBOL)
        return false;
    *name_out = atom_name_cstr(term->expr.elems[0]);
    *arguments_out = term->expr.elems + 1u;
    *arity_out = (size_t)term->expr.len - 1u;
    return **name_out != '\0';
}

static GdlNativeBuildV1 gdl_native_ground_accepts_rec_v1(
    GdlNativeGroundContextV1 *context, Atom *actual, Atom *expected,
    Atom **path, size_t path_count,
    GdlNativeGroundProofBagV1 *proofs_out) {
    size_t index;
    if (!context || !actual || !expected || !path || !proofs_out)
        return GDL_NATIVE_BUILD_OUTSIDE_V1;
    if (path_count >= context->native->limits.max_derivation_depth)
        return GDL_NATIVE_BUILD_RESOURCE_V1;
    for (index = 0u; index < path_count; index++)
        if (atom_eq(path[index], actual))
            return GDL_NATIVE_BUILD_OK_V1;
    path[path_count++] = actual;
    if (atom_eq(actual, expected)) {
        Atom *arguments[] = {
            atom_deep_copy(context->arena, actual),
        };
        Atom *proof = arguments[0]
            ? gdl_native_ground_proof_v1(
                context, "gdl:native-ground-accepts-refl-v1",
                arguments, 1u)
            : NULL;
        if (!proof || !gdl_native_ground_proof_append_v1(
                proofs_out, proof))
            return GDL_NATIVE_BUILD_RESOURCE_V1;
    }
    for (index = 0u; index < context->native->subtype_count; index++) {
        const GdlNativeSubtypeEdgeV1 *edge =
            &context->native->subtypes[index];
        GdlNativeGroundProofBagV1 tails = {0};
        GdlNativeBuildV1 built;
        size_t tail_index;
        if (!atom_eq(edge->subtype, actual))
            continue;
        built = gdl_native_ground_accepts_rec_v1(
            context, edge->supertype, expected,
            path, path_count, &tails);
        if (built != GDL_NATIVE_BUILD_OK_V1) {
            free(tails.items);
            return built;
        }
        for (tail_index = 0u; tail_index < tails.count; tail_index++) {
            Atom *arguments[] = {
                atom_deep_copy(context->arena, actual),
                atom_deep_copy(context->arena, edge->supertype),
                atom_deep_copy(context->arena, expected),
                atom_deep_copy(context->arena, edge->proof),
                tails.items[tail_index],
            };
            Atom *proof = arguments[0] && arguments[1] && arguments[2] &&
                    arguments[3]
                ? gdl_native_ground_proof_v1(
                    context, "gdl:native-ground-accepts-step-v1",
                    arguments, 5u)
                : NULL;
            if (!proof || !gdl_native_ground_proof_append_v1(
                    proofs_out, proof)) {
                free(tails.items);
                return GDL_NATIVE_BUILD_RESOURCE_V1;
            }
        }
        free(tails.items);
    }
    return context->incomplete
        ? GDL_NATIVE_BUILD_RESOURCE_V1
        : GDL_NATIVE_BUILD_OK_V1;
}

static GdlNativeBuildV1 gdl_native_ground_accepts_v1(
    GdlNativeGroundContextV1 *context, Atom *actual, Atom *expected,
    GdlNativeGroundProofBagV1 *proofs_out) {
    size_t path_capacity;
    Atom **path;
    GdlNativeBuildV1 built;
    if (!context || !proofs_out)
        return GDL_NATIVE_BUILD_OUTSIDE_V1;
    path_capacity = context->native->subtype_count + 2u;
    if (path_capacity > context->native->limits.max_derivation_depth)
        path_capacity = context->native->limits.max_derivation_depth;
    if (path_capacity == 0u)
        return GDL_NATIVE_BUILD_RESOURCE_V1;
    path = cetta_malloc(path_capacity * sizeof(*path));
    built = gdl_native_ground_accepts_rec_v1(
        context, actual, expected, path, 0u, proofs_out);
    free(path);
    return built;
}

static GdlNativeBuildV1 gdl_native_ground_derive_v1(
    GdlNativeGroundContextV1 *context, Atom *occurrence, Atom *term,
    size_t depth, GdlNativeGroundTypingBagV1 *typings_out);

static GdlNativeBuildV1 gdl_native_ground_arguments_v1(
    GdlNativeGroundContextV1 *context, Atom *parent_occurrence,
    Atom **arguments, size_t argument_count, Atom *expected_types,
    size_t index, size_t depth, GdlNativeGroundProofBagV1 *proofs_out) {
    Atom *expected;
    Atom *remaining_types;
    if (!context || !parent_occurrence || !expected_types || !proofs_out ||
        depth >= context->native->limits.max_derivation_depth)
        return GDL_NATIVE_BUILD_RESOURCE_V1;
    if (index == argument_count) {
        if (!gdl_native_object_nil(expected_types))
            return GDL_NATIVE_BUILD_OK_V1;
        Atom *proof = gdl_native_ground_proof_v1(
            context, "gdl:native-ground-arguments-nil-v1", NULL, 0u);
        return proof && gdl_native_ground_proof_append_v1(
                proofs_out, proof)
            ? GDL_NATIVE_BUILD_OK_V1
            : GDL_NATIVE_BUILD_RESOURCE_V1;
    }
    if (!gdl_native_object_cons(
            expected_types, &expected, &remaining_types))
        return GDL_NATIVE_BUILD_OK_V1;

    Atom *child_occurrence_arguments[] = {
        atom_deep_copy(context->arena, parent_occurrence),
        atom_int(context->arena, (int64_t)(index + 1u)),
    };
    Atom *child_occurrence = child_occurrence_arguments[0]
        ? gdl_native_ground_expr_v1(
            context->arena, "gdl:episode-child-occurrence-v1",
            child_occurrence_arguments, 2u)
        : NULL;
    if (!child_occurrence)
        return GDL_NATIVE_BUILD_RESOURCE_V1;

    GdlNativeGroundTypingBagV1 child_typings = {0};
    GdlNativeGroundProofBagV1 tails = {0};
    GdlNativeBuildV1 built = gdl_native_ground_derive_v1(
        context, child_occurrence, arguments[index],
        depth + 1u, &child_typings);
    if (built != GDL_NATIVE_BUILD_OK_V1) {
        free(child_typings.items);
        return built;
    }
    built = gdl_native_ground_arguments_v1(
        context, parent_occurrence, arguments, argument_count,
        remaining_types, index + 1u, depth + 1u, &tails);
    if (built != GDL_NATIVE_BUILD_OK_V1) {
        free(child_typings.items);
        free(tails.items);
        return built;
    }
    for (size_t typing_index = 0u;
         typing_index < child_typings.count; typing_index++) {
        GdlNativeGroundProofBagV1 accepts = {0};
        built = gdl_native_ground_accepts_v1(
            context, child_typings.items[typing_index].type,
            expected, &accepts);
        if (built != GDL_NATIVE_BUILD_OK_V1) {
            free(accepts.items);
            free(child_typings.items);
            free(tails.items);
            return built;
        }
        for (size_t accepts_index = 0u;
             accepts_index < accepts.count; accepts_index++)
            for (size_t tail_index = 0u;
                 tail_index < tails.count; tail_index++) {
                Atom *proof_arguments[] = {
                    child_occurrence,
                    atom_deep_copy(context->arena, arguments[index]),
                    child_typings.items[typing_index].type,
                    atom_deep_copy(context->arena, expected),
                    child_typings.items[typing_index].proof,
                    accepts.items[accepts_index],
                    tails.items[tail_index],
                };
                Atom *proof = proof_arguments[1] && proof_arguments[3]
                    ? gdl_native_ground_proof_v1(
                        context,
                        "gdl:native-ground-arguments-cons-v1",
                        proof_arguments, 7u)
                    : NULL;
                if (!proof || !gdl_native_ground_proof_append_v1(
                        proofs_out, proof)) {
                    free(accepts.items);
                    free(child_typings.items);
                    free(tails.items);
                    return GDL_NATIVE_BUILD_RESOURCE_V1;
                }
            }
        free(accepts.items);
    }
    free(child_typings.items);
    free(tails.items);
    return context->incomplete
        ? GDL_NATIVE_BUILD_RESOURCE_V1
        : GDL_NATIVE_BUILD_OK_V1;
}

static GdlNativeBuildV1 gdl_native_ground_derive_v1(
    GdlNativeGroundContextV1 *context, Atom *occurrence, Atom *term,
    size_t depth, GdlNativeGroundTypingBagV1 *typings_out) {
    const char *name;
    Atom **arguments;
    size_t arity;
    const char *encoded_name;
    Atom *name_pattern;
    if (!context || !occurrence || !term || !typings_out ||
        depth >= context->native->limits.max_derivation_depth)
        return GDL_NATIVE_BUILD_RESOURCE_V1;
    if (!gdl_native_ground_head_v1(
            context, term, &name, &arguments, &arity))
        return GDL_NATIVE_BUILD_OUTSIDE_V1;
    encoded_name = gdl_source_hex_name(
        context->arena, "gdl:name:", name);
    name_pattern = encoded_name
        ? gdl_native_ground_papp_v1(
            context->arena, encoded_name, NULL, 0u)
        : NULL;
    if (!name_pattern)
        return GDL_NATIVE_BUILD_RESOURCE_V1;

    for (size_t signature_index = 0u;
         signature_index < context->native->signature_count;
         signature_index++) {
        const GdlNativeSignatureV1 *signature =
            &context->native->signatures[signature_index];
        size_t expected_arity;
        if (!atom_eq(signature->name, name_pattern) ||
            !gdl_native_object_list_count(
                signature->argument_types, &expected_arity) ||
            expected_arity != arity)
            continue;
        GdlNativeGroundProofBagV1 argument_proofs = {0};
        GdlNativeBuildV1 built = gdl_native_ground_arguments_v1(
            context, occurrence, arguments, arity,
            signature->argument_types, 0u, depth + 1u,
            &argument_proofs);
        if (built != GDL_NATIVE_BUILD_OK_V1) {
            free(argument_proofs.items);
            return built;
        }
        for (size_t proof_index = 0u;
             proof_index < argument_proofs.count; proof_index++) {
            Atom *type = atom_deep_copy(
                context->arena, signature->result_type);
            Atom *proof_arguments[] = {
                atom_deep_copy(context->arena, occurrence),
                atom_deep_copy(context->arena, term),
                name_pattern,
                atom_deep_copy(
                    context->arena, signature->argument_types),
                type,
                atom_deep_copy(context->arena, signature->proof),
                argument_proofs.items[proof_index],
            };
            Atom *proof = type && proof_arguments[0] &&
                    proof_arguments[1] && proof_arguments[3] &&
                    proof_arguments[5]
                ? gdl_native_ground_proof_v1(
                    context, "gdl:native-ground-application-v1",
                    proof_arguments, 7u)
                : NULL;
            if (!proof || !gdl_native_ground_typing_append_v1(
                    typings_out, type, proof)) {
                free(argument_proofs.items);
                return GDL_NATIVE_BUILD_RESOURCE_V1;
            }
        }
        free(argument_proofs.items);
    }
    return context->incomplete
        ? GDL_NATIVE_BUILD_RESOURCE_V1
        : GDL_NATIVE_BUILD_OK_V1;
}

static CettaGdlTypeOfNativeGroundV1 gdl_native_ground_outcome_v1(
    CettaNikOutcomeV1 outcome, CettaNikImplementationSelectionV1 selection) {
    CettaGdlTypeOfNativeGroundV1 result = {
        .kind = CETTA_GDL_TYPE_OF_NATIVE_GROUND_OUTCOME_V1,
        .selection = selection,
        .selected_realization_identity =
            g_gdl_type_of_native_authority_v1.realization_identity,
        .value.outcome = outcome,
    };
    return result;
}

CettaGdlTypeOfNativeGroundV1
cetta_gdl_type_of_native_construct_ground_literal_v1(
    const CettaGdlTypeOfNativeV1 *native,
    const CettaNikDirectAuthorityTokenV1 *token,
    Arena *result_arena,
    Atom *occurrence,
    Atom *term,
    size_t max_proofs) {
    if (!native || !token || !result_arena || !occurrence || !term) {
        return (CettaGdlTypeOfNativeGroundV1){
            .kind = CETTA_GDL_TYPE_OF_NATIVE_GROUND_ENGINE_FAULT_V1,
            .selection = {
                .status = CETTA_NIK_IMPLEMENTATION_SELECTION_STATUS_OK_V1,
                .kind = CETTA_NIK_IMPLEMENTATION_SELECTION_NONE_V1,
                .greatest_index = SIZE_MAX,
            },
            .value.fault = CETTA_NIK_ENGINE_FAULT_UNAVAILABLE,
        };
    }
    if (!cetta_gdl_type_of_native_token_is_current_v1(native, token)) {
        return (CettaGdlTypeOfNativeGroundV1){
            .kind = CETTA_GDL_TYPE_OF_NATIVE_GROUND_STALE_V1,
            .selection = {
                .status = CETTA_NIK_IMPLEMENTATION_SELECTION_STATUS_OK_V1,
                .kind = CETTA_NIK_IMPLEMENTATION_SELECTION_NONE_V1,
                .greatest_index = SIZE_MAX,
            },
        };
    }
    size_t frontier_index = SIZE_MAX;
    CettaNikImplementationSelectionV1 selection = gdl_native_select_v1(
        &frontier_index);
    if (!gdl_native_selection_is_unique_greatest_v1(
            &selection, frontier_index)) {
        return (CettaGdlTypeOfNativeGroundV1){
            .kind = CETTA_GDL_TYPE_OF_NATIVE_GROUND_ENGINE_FAULT_V1,
            .selection = selection,
            .value.fault = CETTA_NIK_ENGINE_FAULT_UNAVAILABLE,
        };
    }
    if (atom_has_vars(occurrence) || atom_has_vars(term))
        return gdl_native_ground_outcome_v1(
            CETTA_NIK_OUTCOME_OUTSIDE_FRAGMENT, selection);

    GdlNativeGroundContextV1 context = {
        .native = native,
        .arena = result_arena,
        .proof_node_limit = native->limits.max_proof_nodes,
    };
    GdlNativeGroundTypingBagV1 typings = {0};
    GdlNativeGroundProofBagV1 literal_proofs = {0};
    GdlNativeBuildV1 built = gdl_native_ground_derive_v1(
        &context, occurrence, term, 0u, &typings);
    Atom *unique_type = NULL;
    if (built == GDL_NATIVE_BUILD_OK_V1) {
        for (size_t typing_index = 0u;
             typing_index < typings.count; typing_index++) {
            GdlNativeGroundProofBagV1 accepts = {0};
            built = gdl_native_ground_accepts_v1(
                &context, typings.items[typing_index].type,
                native->bool_type, &accepts);
            if (built != GDL_NATIVE_BUILD_OK_V1) {
                free(accepts.items);
                break;
            }
            for (size_t accepts_index = 0u;
                 accepts_index < accepts.count; accepts_index++) {
                if (unique_type && !atom_eq(
                        unique_type, typings.items[typing_index].type)) {
                    built = GDL_NATIVE_BUILD_OUTSIDE_V1;
                    break;
                }
                if (!unique_type)
                    unique_type = typings.items[typing_index].type;
                Atom *proof_arguments[] = {
                    atom_deep_copy(result_arena, occurrence),
                    atom_deep_copy(result_arena, term),
                    typings.items[typing_index].type,
                    typings.items[typing_index].proof,
                    accepts.items[accepts_index],
                };
                Atom *proof = proof_arguments[0] && proof_arguments[1]
                    ? gdl_native_ground_proof_v1(
                        &context, "gdl:native-ground-literal-v1",
                        proof_arguments, 5u)
                    : NULL;
                if (!proof || !gdl_native_ground_proof_append_v1(
                        &literal_proofs, proof)) {
                    built = GDL_NATIVE_BUILD_RESOURCE_V1;
                    break;
                }
                if (max_proofs != 0u &&
                    literal_proofs.count > max_proofs) {
                    context.incomplete = true;
                    built = GDL_NATIVE_BUILD_RESOURCE_V1;
                    break;
                }
            }
            free(accepts.items);
            if (built != GDL_NATIVE_BUILD_OK_V1)
                break;
        }
    }

    CettaGdlTypeOfNativeGroundV1 result;
    if (built == GDL_NATIVE_BUILD_RESOURCE_V1 || context.incomplete) {
        result = gdl_native_ground_outcome_v1(
            CETTA_NIK_OUTCOME_INCOMPLETE, selection);
    } else if (built != GDL_NATIVE_BUILD_OK_V1 ||
               literal_proofs.count == 0u || !unique_type) {
        result = gdl_native_ground_outcome_v1(
            CETTA_NIK_OUTCOME_OUTSIDE_FRAGMENT, selection);
    } else {
        result = gdl_native_ground_outcome_v1(
            CETTA_NIK_OUTCOME_ESTABLISHED, selection);
        result.proofs = arena_alloc(
            result_arena, literal_proofs.count * sizeof(*result.proofs));
        memcpy(result.proofs, literal_proofs.items,
               literal_proofs.count * sizeof(*result.proofs));
        result.proof_count = literal_proofs.count;
        result.type = unique_type;
    }
    free(literal_proofs.items);
    free(typings.items);
    return result;
}

bool cetta_gdl_type_of_native_stats_v1(
    const CettaGdlTypeOfNativeV1 *native,
    CettaGdlTypeOfNativeStatsV1 *stats_out) {
    if (!native || !stats_out)
        return false;
    *stats_out = native->stats;
    return true;
}

static GdlNativeBuildV1 gdl_native_arguments_typed(
    CettaGdlTypeOfNativeV1 *native, Atom *children, Atom *terms,
    size_t depth, GdlNativeProofBagV1 *proofs_out) {
    Atom *child;
    Atom *remaining_children;
    Atom *child_term;
    Atom *remaining_terms;
    GdlNativeSourceNodeV1 *child_node;
    GdlNativeProofBagV1 tails = {0};
    GdlNativeBuildV1 built;
    size_t child_proof_index;
    size_t tail_index;
    if (!native || !proofs_out ||
        depth >= native->limits.max_derivation_depth) {
        if (native)
            native->resource_exhausted = true;
        return GDL_NATIVE_BUILD_RESOURCE_V1;
    }
    if (gdl_native_object_nil(children) && gdl_native_object_nil(terms)) {
        Atom *proof = gdl_native_proof(
            native, "gdl:arguments-typed-nil", NULL, 0u, NULL, 0u);
        return proof && gdl_native_proof_bag_append(proofs_out, proof)
            ? GDL_NATIVE_BUILD_OK_V1
            : GDL_NATIVE_BUILD_RESOURCE_V1;
    }
    if (!gdl_native_object_cons(
            children, &child, &remaining_children) ||
        !gdl_native_object_cons(terms, &child_term, &remaining_terms) ||
        !(child_node = gdl_native_node_find(native, child)) ||
        !atom_eq(child_node->term, child_term))
        return GDL_NATIVE_BUILD_OUTSIDE_V1;
    built = gdl_native_derive_node(native, child_node, depth + 1u);
    if (built != GDL_NATIVE_BUILD_OK_V1)
        return built;
    built = gdl_native_arguments_typed(
        native, remaining_children, remaining_terms, depth + 1u, &tails);
    if (built != GDL_NATIVE_BUILD_OK_V1) {
        gdl_native_proof_bag_free(&tails);
        return built;
    }
    for (child_proof_index = 0u;
         child_proof_index < child_node->type_proofs.count;
         child_proof_index++)
        for (tail_index = 0u; tail_index < tails.count; tail_index++) {
            Atom *arguments[] = {
                child, remaining_children, child_term, remaining_terms,
                child_node->type,
            };
            Atom *premises[] = {
                child_node->type_proofs.items[child_proof_index],
                tails.items[tail_index],
            };
            Atom *proof = gdl_native_proof(
                native, "gdl:arguments-typed-cons", arguments, 5u,
                premises, 2u);
            if (!proof || !gdl_native_proof_bag_append(proofs_out, proof)) {
                gdl_native_proof_bag_free(&tails);
                return GDL_NATIVE_BUILD_RESOURCE_V1;
            }
        }
    gdl_native_proof_bag_free(&tails);
    return GDL_NATIVE_BUILD_OK_V1;
}

static GdlNativeBuildV1 gdl_native_all_type(
    CettaGdlTypeOfNativeV1 *native, Atom *types, Atom *expected,
    size_t depth, Atom **proof_out) {
    Atom *head;
    Atom *tail;
    Atom *tail_proof;
    if (!native || !types || !expected || !proof_out ||
        depth >= native->limits.max_derivation_depth) {
        if (native)
            native->resource_exhausted = true;
        return GDL_NATIVE_BUILD_RESOURCE_V1;
    }
    if (gdl_native_object_nil(types)) {
        Atom *arguments[] = {expected};
        *proof_out = gdl_native_proof(
            native, "gdl:all-type-nil", arguments, 1u, NULL, 0u);
        return *proof_out
            ? GDL_NATIVE_BUILD_OK_V1
            : GDL_NATIVE_BUILD_RESOURCE_V1;
    }
    if (!gdl_native_object_cons(types, &head, &tail) ||
        !atom_eq(head, expected))
        return GDL_NATIVE_BUILD_OUTSIDE_V1;
    if (gdl_native_all_type(
            native, tail, expected, depth + 1u, &tail_proof) !=
        GDL_NATIVE_BUILD_OK_V1)
        return native->resource_exhausted
            ? GDL_NATIVE_BUILD_RESOURCE_V1
            : GDL_NATIVE_BUILD_OUTSIDE_V1;
    {
        Atom *arguments[] = {tail, expected};
        Atom *premises[] = {tail_proof};
        *proof_out = gdl_native_proof(
            native, "gdl:all-type-cons", arguments, 2u,
            premises, 1u);
    }
    return *proof_out
        ? GDL_NATIVE_BUILD_OK_V1
        : GDL_NATIVE_BUILD_RESOURCE_V1;
}

static GdlNativeBuildV1 gdl_native_derive_application(
    CettaGdlTypeOfNativeV1 *native, GdlNativeSourceNodeV1 *node,
    Atom *name, Atom *terms, size_t depth) {
    size_t signature_index;
    size_t child_count;
    size_t term_count;
    if (!gdl_native_object_list_count(node->children, &child_count) ||
        !gdl_native_object_list_count(terms, &term_count) ||
        child_count != term_count)
        return GDL_NATIVE_BUILD_OUTSIDE_V1;
    for (signature_index = 0u;
         signature_index < native->signature_count; signature_index++) {
        GdlNativeSignatureV1 *signature =
            &native->signatures[signature_index];
        GdlNativeProofBagV1 argument_proofs = {0};
        size_t expected_count;
        GdlNativeBuildV1 built;
        size_t proof_index;
        if (!atom_eq(signature->name, name))
            continue;
        if (!gdl_native_object_list_count(
                signature->argument_types, &expected_count) ||
            expected_count != child_count)
            continue;
        built = gdl_native_arguments_type(
            native, node->children, terms, signature->argument_types,
            depth + 1u, &argument_proofs);
        if (built == GDL_NATIVE_BUILD_RESOURCE_V1) {
            gdl_native_proof_bag_free(&argument_proofs);
            return built;
        }
        if (built != GDL_NATIVE_BUILD_OK_V1) {
            gdl_native_proof_bag_free(&argument_proofs);
            continue;
        }
        for (proof_index = 0u; proof_index < argument_proofs.count;
             proof_index++) {
            Atom *arguments[] = {
                node->occurrence, name, terms, node->children,
                signature->argument_types, signature->result_type,
            };
            Atom *premises[] = {
                node->source_node_proof, node->source_children_proof,
                signature->proof, argument_proofs.items[proof_index],
            };
            Atom *proof = gdl_native_proof(
                native, "gdl:type-application", arguments, 6u,
                premises, 4u);
            built = gdl_native_node_add_type_proof(
                native, node, signature->result_type, proof);
            if (built != GDL_NATIVE_BUILD_OK_V1) {
                gdl_native_proof_bag_free(&argument_proofs);
                return built;
            }
        }
        gdl_native_proof_bag_free(&argument_proofs);
    }
    return GDL_NATIVE_BUILD_OK_V1;
}

static GdlNativeBuildV1 gdl_native_derive_variable(
    CettaGdlTypeOfNativeV1 *native, GdlNativeSourceNodeV1 *node,
    Atom *name) {
    size_t index;
    bool found = false;
    for (index = 0u; index < native->variable_count; index++) {
        GdlNativeVariableBindingV1 *binding = &native->variables[index];
        GdlNativeBuildV1 built;
        if (!atom_eq(binding->form, node->form) ||
            !atom_eq(binding->name, name))
            continue;
        {
            Atom *arguments[] = {
                node->occurrence, node->form, name, binding->type,
            };
            Atom *premises[] = {
                node->source_node_proof, node->source_form_proof,
                binding->proof,
            };
            Atom *proof = gdl_native_proof(
                native, "gdl:type-variable", arguments, 4u,
                premises, 3u);
            built = gdl_native_node_add_type_proof(
                native, node, binding->type, proof);
        }
        if (built != GDL_NATIVE_BUILD_OK_V1)
            return built;
        found = true;
    }
    return found ? GDL_NATIVE_BUILD_OK_V1 : GDL_NATIVE_BUILD_OUTSIDE_V1;
}

static GdlNativeBuildV1 gdl_native_derive_not(
    CettaGdlTypeOfNativeV1 *native, GdlNativeSourceNodeV1 *node,
    Atom *terms, Atom *bool_type, size_t depth) {
    Atom *child_occurrence;
    Atom *remaining_children;
    Atom *child_term;
    Atom *remaining_terms;
    GdlNativeSourceNodeV1 *child;
    GdlNativeProofBagV1 accepts = {0};
    GdlNativeBuildV1 built;
    size_t child_proof_index;
    size_t accepts_index;
    if (!gdl_native_object_cons(
            node->children, &child_occurrence, &remaining_children) ||
        !gdl_native_object_nil(remaining_children) ||
        !gdl_native_object_cons(terms, &child_term, &remaining_terms) ||
        !gdl_native_object_nil(remaining_terms) ||
        !(child = gdl_native_node_find(native, child_occurrence)) ||
        !atom_eq(child->term, child_term))
        return GDL_NATIVE_BUILD_OUTSIDE_V1;
    built = gdl_native_derive_node(native, child, depth + 1u);
    if (built != GDL_NATIVE_BUILD_OK_V1)
        return built;
    built = gdl_native_accepts(native, child->type, bool_type, &accepts);
    if (built != GDL_NATIVE_BUILD_OK_V1) {
        gdl_native_proof_bag_free(&accepts);
        return built;
    }
    for (child_proof_index = 0u;
         child_proof_index < child->type_proofs.count;
         child_proof_index++)
        for (accepts_index = 0u; accepts_index < accepts.count;
             accepts_index++) {
            Atom *arguments[] = {
                node->occurrence, child_occurrence, child_term, child->type,
            };
            Atom *premises[] = {
                node->source_node_proof, node->source_children_proof,
                child->type_proofs.items[child_proof_index],
                accepts.items[accepts_index],
            };
            Atom *proof = gdl_native_proof(
                native, "gdl:type-not", arguments, 4u, premises, 4u);
            built = gdl_native_node_add_type_proof(
                native, node, bool_type, proof);
            if (built != GDL_NATIVE_BUILD_OK_V1) {
                gdl_native_proof_bag_free(&accepts);
                return built;
            }
        }
    gdl_native_proof_bag_free(&accepts);
    return GDL_NATIVE_BUILD_OK_V1;
}

static GdlNativeBuildV1 gdl_native_derive_or(
    CettaGdlTypeOfNativeV1 *native, GdlNativeSourceNodeV1 *node,
    Atom *terms, Atom *bool_type, size_t depth) {
    size_t count;
    Atom **bool_items;
    Atom *bool_types;
    Atom *first_child;
    Atom *remaining_children;
    Atom *first_term;
    Atom *remaining_terms;
    GdlNativeProofBagV1 argument_proofs = {0};
    Atom *all_type_proof = NULL;
    GdlNativeBuildV1 built;
    size_t proof_index;
    if (!gdl_native_object_list_count(node->children, &count) || count == 0u ||
        !gdl_native_object_cons(
            node->children, &first_child, &remaining_children) ||
        !gdl_native_object_cons(terms, &first_term, &remaining_terms))
        return GDL_NATIVE_BUILD_OUTSIDE_V1;
    bool_items = cetta_malloc(count * sizeof(*bool_items));
    for (proof_index = 0u; proof_index < count; proof_index++)
        bool_items[proof_index] = bool_type;
    bool_types = gdl_native_object_list(native, bool_items, count);
    free(bool_items);
    if (!bool_types)
        return GDL_NATIVE_BUILD_RESOURCE_V1;
    built = gdl_native_arguments_type(
        native, node->children, terms, bool_types,
        depth + 1u, &argument_proofs);
    if (built != GDL_NATIVE_BUILD_OK_V1) {
        gdl_native_proof_bag_free(&argument_proofs);
        return built;
    }
    built = gdl_native_all_type(
        native, bool_types, bool_type, depth + 1u, &all_type_proof);
    if (built != GDL_NATIVE_BUILD_OK_V1) {
        gdl_native_proof_bag_free(&argument_proofs);
        return built;
    }
    for (proof_index = 0u; proof_index < argument_proofs.count;
         proof_index++) {
        Atom *arguments[] = {
            node->occurrence, first_child, remaining_children,
            first_term, remaining_terms, bool_types,
        };
        Atom *premises[] = {
            node->source_node_proof, node->source_children_proof,
            argument_proofs.items[proof_index], all_type_proof,
        };
        Atom *proof = gdl_native_proof(
            native, "gdl:type-or", arguments, 6u, premises, 4u);
        built = gdl_native_node_add_type_proof(
            native, node, bool_type, proof);
        if (built != GDL_NATIVE_BUILD_OK_V1) {
            gdl_native_proof_bag_free(&argument_proofs);
            return built;
        }
    }
    gdl_native_proof_bag_free(&argument_proofs);
    return GDL_NATIVE_BUILD_OK_V1;
}

static GdlNativeBuildV1 gdl_native_derive_distinct(
    CettaGdlTypeOfNativeV1 *native, GdlNativeSourceNodeV1 *node,
    Atom *terms, Atom *bool_type, size_t depth) {
    Atom *left;
    Atom *after_left;
    Atom *right;
    Atom *after_right;
    Atom *left_term;
    Atom *terms_after_left;
    Atom *right_term;
    Atom *terms_after_right;
    GdlNativeProofBagV1 typed = {0};
    GdlNativeBuildV1 built;
    size_t proof_index;
    if (!gdl_native_object_cons(node->children, &left, &after_left) ||
        !gdl_native_object_cons(after_left, &right, &after_right) ||
        !gdl_native_object_nil(after_right) ||
        !gdl_native_object_cons(terms, &left_term, &terms_after_left) ||
        !gdl_native_object_cons(
            terms_after_left, &right_term, &terms_after_right) ||
        !gdl_native_object_nil(terms_after_right))
        return GDL_NATIVE_BUILD_OUTSIDE_V1;
    built = gdl_native_arguments_typed(
        native, node->children, terms, depth + 1u, &typed);
    if (built != GDL_NATIVE_BUILD_OK_V1) {
        gdl_native_proof_bag_free(&typed);
        return built;
    }
    for (proof_index = 0u; proof_index < typed.count; proof_index++) {
        Atom *arguments[] = {
            node->occurrence, left, right, left_term, right_term,
        };
        Atom *premises[] = {
            node->source_node_proof, node->source_children_proof,
            typed.items[proof_index],
        };
        Atom *proof = gdl_native_proof(
            native, "gdl:type-distinct", arguments, 5u,
            premises, 3u);
        built = gdl_native_node_add_type_proof(
            native, node, bool_type, proof);
        if (built != GDL_NATIVE_BUILD_OK_V1) {
            gdl_native_proof_bag_free(&typed);
            return built;
        }
    }
    gdl_native_proof_bag_free(&typed);
    return GDL_NATIVE_BUILD_OK_V1;
}

static GdlNativeBuildV1 gdl_native_derive_literal(
    CettaGdlTypeOfNativeV1 *native, GdlNativeSourceNodeV1 *node) {
    GdlNativeProofBagV1 accepts = {0};
    GdlNativeBuildV1 built;
    size_t type_proof_index;
    size_t accepts_index;
    if (!native || !node || !node->type || !native->bool_type)
        return GDL_NATIVE_BUILD_OUTSIDE_V1;
    built = gdl_native_accepts(
        native, node->type, native->bool_type, &accepts);
    if (built != GDL_NATIVE_BUILD_OK_V1) {
        gdl_native_proof_bag_free(&accepts);
        return built;
    }
    for (type_proof_index = 0u;
         type_proof_index < node->type_proofs.count; type_proof_index++)
        for (accepts_index = 0u; accepts_index < accepts.count;
             accepts_index++) {
            Atom *arguments[] = {
                node->occurrence, node->term, node->type,
            };
            Atom *premises[] = {
                node->type_proofs.items[type_proof_index],
                accepts.items[accepts_index],
            };
            Atom *proof = gdl_native_proof(
                native, "gdl:literal", arguments, 3u, premises, 2u);
            if (!proof ||
                !gdl_native_proof_bag_append(&node->literal_proofs, proof)) {
                gdl_native_proof_bag_free(&accepts);
                return GDL_NATIVE_BUILD_RESOURCE_V1;
            }
            native->stats.literal_proof_occurrences++;
        }
    gdl_native_proof_bag_free(&accepts);
    return GDL_NATIVE_BUILD_OK_V1;
}

static GdlNativeBuildV1 gdl_native_derive_node(
    CettaGdlTypeOfNativeV1 *native, GdlNativeSourceNodeV1 *node,
    size_t depth) {
    const char *term_head;
    Atom *term_arguments;
    GdlNativeBuildV1 built;
    if (!native || !node || depth >= native->limits.max_derivation_depth) {
        if (native)
            native->resource_exhausted = true;
        return GDL_NATIVE_BUILD_RESOURCE_V1;
    }
    if (node->derivation_state == 2u)
        return GDL_NATIVE_BUILD_OK_V1;
    if (node->derivation_state == 1u)
        return GDL_NATIVE_BUILD_OUTSIDE_V1;
    if (!node->occurrence || !node->term || !node->children || !node->form ||
        !node->source_node_proof || !node->source_children_proof ||
        !node->source_form_proof ||
        !gdl_native_papp(node->term, &term_head, &term_arguments))
        return GDL_NATIVE_BUILD_OUTSIDE_V1;
    node->derivation_state = 1u;
    if (strcmp(term_head, "gdl:application") == 0) {
        Atom *name;
        Atom *terms;
        size_t arity;
        if (!gdl_native_wire_list_count(term_arguments, &arity) || arity != 2u ||
            !gdl_native_wire_list_item(term_arguments, 0u, &name) ||
            !gdl_native_wire_list_item(term_arguments, 1u, &terms)) {
            node->derivation_state = 0u;
            return GDL_NATIVE_BUILD_OUTSIDE_V1;
        }
        built = gdl_native_derive_application(
            native, node, name, terms, depth + 1u);
        if (built != GDL_NATIVE_BUILD_OK_V1) {
            node->derivation_state = 0u;
            return built;
        }
        if (gdl_native_zero_papp_named(name, "gdl:name:6e6f74"))
            built = gdl_native_derive_not(
                native, node, terms, native->bool_type, depth + 1u);
        else if (gdl_native_zero_papp_named(name, "gdl:name:6f72"))
            built = gdl_native_derive_or(
                native, node, terms, native->bool_type, depth + 1u);
        else if (gdl_native_zero_papp_named(
                     name, "gdl:name:64697374696e6374"))
            built = gdl_native_derive_distinct(
                native, node, terms, native->bool_type, depth + 1u);
        if (built != GDL_NATIVE_BUILD_OK_V1) {
            node->derivation_state = 0u;
            return built;
        }
    } else if (strcmp(term_head, "gdl:variable") == 0) {
        Atom *name;
        size_t arity;
        if (!gdl_native_wire_list_count(term_arguments, &arity) || arity != 1u ||
            !gdl_native_wire_list_item(term_arguments, 0u, &name) ||
            !gdl_native_object_nil(node->children)) {
            node->derivation_state = 0u;
            return GDL_NATIVE_BUILD_OUTSIDE_V1;
        }
        built = gdl_native_derive_variable(native, node, name);
        if (built != GDL_NATIVE_BUILD_OK_V1) {
            node->derivation_state = 0u;
            return built;
        }
    } else {
        node->derivation_state = 0u;
        return GDL_NATIVE_BUILD_OUTSIDE_V1;
    }
    if (!node->type || node->type_proofs.count == 0u) {
        node->derivation_state = 0u;
        return GDL_NATIVE_BUILD_OUTSIDE_V1;
    }
    node->derivation_state = 2u;
    built = gdl_native_derive_literal(native, node);
    if (built != GDL_NATIVE_BUILD_OK_V1) {
        node->derivation_state = 0u;
        return built;
    }
    return GDL_NATIVE_BUILD_OK_V1;
}

static CettaGdlTypeOfNativeAdmissionV1
gdl_type_of_native_admit_source_impl_v1(
    Atom *source_program,
    const char *expected_source_sha256,
    const char *expected_profile_sha256,
    const char *expected_revision,
    const char *target_name,
    size_t target_arity,
    CettaGdlTypeOfNativeLimitsV1 limits) {
    GdlSourcePackageV1 package = {0};
    CettaGdlTypeOfNativeV1 *native = NULL;
    Arena scratch;
    GdlSourceRawFormsV1 forms = {0};
    GdlSourceProfileV1 profile = {0};
    GdlSourceTargetSliceV1 target_slice = {0};
    GdlSourceAnalysisV1 analysis = {0};
    GdlSourceTypeSolutionV1 solution = {0};
    GdlNativeBuildV1 built = GDL_NATIVE_BUILD_OUTSIDE_V1;
    size_t index;
    bool scratch_initialized = false;
    GdlSourceParseV1 package_status = gdl_source_package_view_v1(
        source_program, expected_source_sha256, expected_profile_sha256,
        expected_revision, &package);
    if (package_status != GDL_SOURCE_PARSE_OK_V1) {
        CettaGdlTypeOfNativeAdmissionKindV1 kind =
            package_status == GDL_SOURCE_PARSE_ENGINE_FAULT_V1
                ? CETTA_GDL_TYPE_OF_NATIVE_ENGINE_FAULT_V1
                : package_status == GDL_SOURCE_PARSE_INCOMPLETE_V1
                    ? CETTA_GDL_TYPE_OF_NATIVE_INCOMPLETE_V1
                    : CETTA_GDL_TYPE_OF_NATIVE_OUTSIDE_FRAGMENT_V1;
        return gdl_native_admission_result(kind, NULL);
    }

    native = cetta_malloc(sizeof(*native));
    memset(native, 0, sizeof(*native));
    arena_init(&native->arena);
    arena_set_runtime_kind(
        &native->arena, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    cetta_gslt_u32_index_init_v1(&native->node_index);
    cetta_gslt_u32_index_init_v1(&native->rule_ids);
    native->limits.max_source_nodes = limits.max_source_nodes
        ? limits.max_source_nodes
        : GDL_TYPE_OF_NATIVE_DEFAULT_SOURCE_NODES;
    native->limits.max_proof_nodes = limits.max_proof_nodes
        ? limits.max_proof_nodes
        : GDL_TYPE_OF_NATIVE_DEFAULT_PROOF_NODES;
    native->limits.max_derivation_depth = limits.max_derivation_depth
        ? limits.max_derivation_depth
        : GDL_TYPE_OF_NATIVE_DEFAULT_DERIVATION_DEPTH;
    memcpy(native->source_sha256, package.source_sha256, 65u);
    memcpy(native->profile_sha256, package.profile_sha256, 65u);
    native->revision = gdl_native_strdup(package.revision);
    native->bool_type = gdl_source_type_pattern(native, "bool");
    if (!native->revision || !native->bool_type)
        goto fault;
    for (index = 0u;
         index < sizeof(g_core_rules) / sizeof(g_core_rules[0]); index++)
        if (!gdl_native_rule_id_insert(native, g_core_rules[index].id))
            goto fault;
    arena_init(&scratch);
    arena_set_runtime_kind(&scratch, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    scratch_initialized = true;
    built = gdl_native_parse_status_v1(gdl_source_parse_forms_v1(
        &scratch, package.source_text,
        native->limits.max_derivation_depth, &forms));
    if (built != GDL_NATIVE_BUILD_OK_V1)
        goto semantic_failure;
    if (target_name) {
        GdlSourceParseV1 target_status =
            gdl_source_select_target_dependency_v1(
                &forms, target_name, target_arity,
                native->limits.max_source_nodes,
                native->limits.max_derivation_depth,
                &target_slice);
        built = gdl_native_parse_status_v1(target_status);
        if (built != GDL_NATIVE_BUILD_OK_V1)
            goto semantic_failure;
        native->target_name = gdl_native_strdup(target_name);
        if (!native->target_name ||
            !gdl_source_target_calculus_input_v1(
                package.calculus_input_sha256, &forms,
                target_name, target_arity,
                native->calculus_input_sha256))
            goto fault;
        native->target_arity = target_arity;
        native->target_source_forms = target_slice.source_forms;
        native->target_selected_forms = target_slice.selected_forms;
        native->target_reachable_relations =
            target_slice.reachable_relations;
        native->target_external_relations =
            target_slice.external_relations;
    } else {
        memcpy(
            native->calculus_input_sha256,
            package.calculus_input_sha256, 65u);
    }
    built = gdl_native_parse_status_v1(
        gdl_source_parse_profile_v1(
            &scratch, package.profile_text, &profile));
    if (built != GDL_NATIVE_BUILD_OK_V1)
        goto semantic_failure;
    built = gdl_source_build_nodes(native, &scratch, &forms, &analysis);
    if (built != GDL_NATIVE_BUILD_OK_V1)
        goto semantic_failure;
    built = gdl_source_extract_constraints(&analysis, &profile);
    if (built != GDL_NATIVE_BUILD_OK_V1)
        goto semantic_failure;
    built = gdl_source_solve_types(
        &analysis, &profile, native->limits.max_derivation_depth,
        &solution);
    if (built != GDL_NATIVE_BUILD_OK_V1)
        goto semantic_failure;
    built = gdl_source_materialize_native(
        native, &scratch, &forms, &profile, &analysis, &solution);
    if (built != GDL_NATIVE_BUILD_OK_V1)
        goto semantic_failure;
    if (!cetta_gslt_u32_index_validate_v1(&native->node_index) ||
        !cetta_gslt_u32_index_validate_v1(&native->rule_ids) ||
        !gdl_native_build_token(native))
        goto fault;
    gdl_source_raw_forms_free_v1(&forms);
    gdl_source_profile_free_v1(&profile);
    gdl_source_analysis_free(&analysis);
    gdl_source_solution_free(&solution);
    arena_free(&scratch);
    return gdl_native_admission_result(
        CETTA_GDL_TYPE_OF_NATIVE_ADMITTED_V1, native);

semantic_failure:
    gdl_source_raw_forms_free_v1(&forms);
    gdl_source_profile_free_v1(&profile);
    gdl_source_analysis_free(&analysis);
    gdl_source_solution_free(&solution);
    if (scratch_initialized)
        arena_free(&scratch);
    {
        CettaGdlTypeOfNativeAdmissionKindV1 kind =
            built == GDL_NATIVE_BUILD_RESOURCE_V1 ||
                    native->resource_exhausted
                ? CETTA_GDL_TYPE_OF_NATIVE_INCOMPLETE_V1
                : CETTA_GDL_TYPE_OF_NATIVE_OUTSIDE_FRAGMENT_V1;
        cetta_gdl_type_of_native_destroy_v1(native);
        return gdl_native_admission_result(kind, NULL);
    }

fault:
    gdl_source_raw_forms_free_v1(&forms);
    gdl_source_profile_free_v1(&profile);
    gdl_source_analysis_free(&analysis);
    gdl_source_solution_free(&solution);
    if (scratch_initialized)
        arena_free(&scratch);
    cetta_gdl_type_of_native_destroy_v1(native);
    return gdl_native_admission_result(
        CETTA_GDL_TYPE_OF_NATIVE_ENGINE_FAULT_V1, NULL);
}

CettaGdlTypeOfNativeAdmissionV1
cetta_gdl_type_of_native_admit_source_v1(
    Atom *source_program,
    const char *expected_source_sha256,
    const char *expected_profile_sha256,
    const char *expected_revision,
    CettaGdlTypeOfNativeLimitsV1 limits) {
    return gdl_type_of_native_admit_source_impl_v1(
        source_program, expected_source_sha256, expected_profile_sha256,
        expected_revision, NULL, 0u, limits);
}

CettaGdlTypeOfNativeAdmissionV1
cetta_gdl_type_of_native_admit_authored_source_v1(
    Atom *source_program, CettaGdlTypeOfNativeLimitsV1 limits) {
    return gdl_type_of_native_admit_source_impl_v1(
        source_program, NULL, NULL, NULL, NULL, 0u, limits);
}

CettaGdlTypeOfNativeAdmissionV1
cetta_gdl_type_of_native_admit_authored_target_v1(
    Atom *source_program,
    const char *target_name,
    size_t target_arity,
    CettaGdlTypeOfNativeLimitsV1 limits) {
    if (!target_name || target_name[0] == '\0')
        return gdl_native_admission_result(
            CETTA_GDL_TYPE_OF_NATIVE_ENGINE_FAULT_V1, NULL);
    return gdl_type_of_native_admit_source_impl_v1(
        source_program, NULL, NULL, NULL,
        target_name, target_arity, limits);
}

bool cetta_gdl_type_of_native_source_type_name_v1(
    const CettaGdlTypeOfNativeV1 *native,
    size_t form_ordinal,
    const char *path,
    const char **type_name_out) {
    if (type_name_out)
        *type_name_out = NULL;
    if (!native || !path || !*path || !type_name_out)
        return false;
    for (size_t index = 0u; index < native->node_count; index++) {
        const GdlNativeSourceNodeV1 *node = &native->nodes[index];
        if (node->form_ordinal == form_ordinal && node->path &&
            strcmp(node->path, path) == 0 && node->type_name) {
            *type_name_out = node->type_name;
            return true;
        }
    }
    return false;
}

bool cetta_gdl_type_of_native_source_judgment_v1(
    const CettaGdlTypeOfNativeV1 *native,
    size_t form_ordinal,
    const char *path,
    CettaGdlTypeOfNativeSourceJudgmentV1 *view_out) {
    if (view_out)
        memset(view_out, 0, sizeof(*view_out));
    if (!native || !path || !*path || !view_out)
        return false;
    for (size_t index = 0u; index < native->node_count; index++) {
        const GdlNativeSourceNodeV1 *node = &native->nodes[index];
        if (node->form_ordinal != form_ordinal || !node->path ||
            strcmp(node->path, path) != 0)
            continue;
        if (!node->occurrence || !node->term || !node->type ||
            !node->type_name || node->type_proofs.count == 0u)
            return false;
        *view_out = (CettaGdlTypeOfNativeSourceJudgmentV1){
            .occurrence = node->occurrence,
            .term = node->term,
            .type = node->type,
            .type_name = node->type_name,
            .type_proofs = node->type_proofs.items,
            .type_proof_count = node->type_proofs.count,
            .literal_proofs = node->literal_proofs.items,
            .literal_proof_count = node->literal_proofs.count,
        };
        return true;
    }
    return false;
}

#include "oslf_native_type_plan_v1.h"

#include "atom.h"
#include "symbol.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static const char *term_kind_name(PPOSLFNativeTermKindV1 kind) {
    switch (kind) {
    case PPOSLF_NATIVE_TERM_SYMBOL_V1:
        return "symbol";
    case PPOSLF_NATIVE_TERM_INTEGER_I64_V1:
        return "integer-i64";
    case PPOSLF_NATIVE_TERM_INTEGER_BIG_V1:
        return "integer-big";
    case PPOSLF_NATIVE_TERM_STRING_V1:
        return "string";
    case PPOSLF_NATIVE_TERM_VARIABLE_V1:
        return "variable";
    case PPOSLF_NATIVE_TERM_APPLICATION_V1:
        return "application";
    }
    return "unknown";
}

static void print_json_string(const char *text) {
    const unsigned char *cursor = (const unsigned char *)(text ? text : "");

    putchar('"');
    while (*cursor) {
        switch (*cursor) {
        case '"':
            fputs("\\\"", stdout);
            break;
        case '\\':
            fputs("\\\\", stdout);
            break;
        case '\b':
            fputs("\\b", stdout);
            break;
        case '\f':
            fputs("\\f", stdout);
            break;
        case '\n':
            fputs("\\n", stdout);
            break;
        case '\r':
            fputs("\\r", stdout);
            break;
        case '\t':
            fputs("\\t", stdout);
            break;
        default:
            if (*cursor < 0x20u)
                printf("\\u%04x", (unsigned)*cursor);
            else
                putchar((int)*cursor);
            break;
        }
        cursor++;
    }
    putchar('"');
}

static void print_open_payload(const PPOSLFNativeOpenHeadV1 *head) {
    if (head->kind == PPOSLF_NATIVE_TERM_INTEGER_I64_V1) {
        printf("%" PRId64, head->integer);
        return;
    }
    print_json_string(head->text);
}

static void print_group_payload(const PPOSLFNativeHeadGroupV1 *group) {
    if (group->kind == PPOSLF_NATIVE_TERM_INTEGER_I64_V1) {
        printf("%" PRId64, group->integer);
        return;
    }
    print_json_string(group->text);
}

static bool relation_is_open(const PPOSLFNativeTypePlanV1 *plan,
                             const char *relation,
                             uint32_t arity) {
    for (uint32_t index = 0u; index < plan->open_head_len; index++) {
        const PPOSLFNativeOpenHeadV1 *open = &plan->open_heads[index];

        if (open->kind == PPOSLF_NATIVE_TERM_APPLICATION_V1 &&
            open->text && strcmp(open->text, relation) == 0 &&
            open->arity == arity)
            return true;
    }
    return false;
}

static bool print_flow_properties(
    unsigned program,
    const PPOSLFNativeTypePlanV1 *plan) {
    uint32_t range_restricted = 0u;
    uint32_t head_linear = 0u;
    uint32_t range_and_head_linear = 0u;
    uint32_t maximum_variables = 0u;

    for (uint32_t index = 0u; index < plan->step_schema_len; index++) {
        const PPOSLFNativeStepSchemaV1 *step = &plan->step_schemas[index];
        PPOSLFNativeStepFlowV1 flow;

        if (!pposlf_native_type_plan_v1_step_flow(
                plan, index, &flow))
            return false;
        if (flow.body_variables_in_head)
            range_restricted++;
        if (flow.head_linear)
            head_linear++;
        if (flow.body_variables_in_head && flow.head_linear)
            range_and_head_linear++;
        if (step->variable_count > maximum_variables)
            maximum_variables = step->variable_count;
    }
    printf("flow\t%u\t%u\t%u\t%u\t%u\t%u\n",
           program, plan->step_schema_len, range_restricted,
           head_linear, range_and_head_linear, maximum_variables);
    return true;
}

int main(int argc, char **argv) {
    SymbolTable symbols;
    bool ok = true;

    if (argc < 2) {
        fprintf(stderr, "usage: %s NATIVE-TYPE-PROGRAM...\n", argv[0]);
        return 2;
    }

    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;

    for (int argument = 1; argument < argc; argument++) {
        PPOSLFNativeTypePlanV1 plan;
        char error[512] = {0};
        unsigned program = (unsigned)(argument - 1);

        pposlf_native_type_plan_v1_init(&plan);
        if (!pposlf_native_type_plan_v1_load(
                &plan, argv[argument], error, sizeof(error))) {
            fprintf(stderr, "program %u: %s\n", program,
                    error[0] ? error : "native type program did not load");
            pposlf_native_type_plan_v1_free(&plan);
            ok = false;
            continue;
        }

        printf("program\t%u\t%s\t%u\t%u\t%u\t%u\n", program,
               plan.semantic_digest, plan.head_signature_len,
               plan.step_schema_len, plan.external_relation_len,
               plan.open_head_len);
        if (!print_flow_properties(program, &plan)) {
            fprintf(stderr,
                    "program %u: native type flow analysis failed\n",
                    program);
            pposlf_native_type_plan_v1_free(&plan);
            ok = false;
            continue;
        }
        for (uint32_t index = 0u; index < plan.head_group_len; index++) {
            const PPOSLFNativeHeadGroupV1 *group =
                &plan.head_groups[index];

            printf("group\t%u\t%u\t%s\t%u\t", program, index,
                   term_kind_name(group->kind), group->step_len);
            print_group_payload(group);
            putchar('\n');
        }
        for (uint32_t index = 0u;
             index < plan.external_relation_len; index++) {
            const PPOSLFNativeExternalRelationV1 *external =
                &plan.external_relations[index];

            printf("external\t%u\t%u\t%u\t%u\t%u\t", program, index,
                   external->arity, external->body_occurrences,
                   relation_is_open(
                       &plan, external->relation, external->arity) ? 1u : 0u);
            print_json_string(external->owner);
            putchar('\t');
            print_json_string(external->relation);
            putchar('\n');
        }
        for (uint32_t index = 0u; index < plan.open_head_len; index++) {
            const PPOSLFNativeOpenHeadV1 *head = &plan.open_heads[index];

            printf("open\t%u\t%u\t%s\t%u\t%u\t", program, index,
                   term_kind_name(head->kind), head->arity,
                   head->body_occurrences);
            print_open_payload(head);
            putchar('\n');
        }
        pposlf_native_type_plan_v1_free(&plan);
    }

    g_symbols = NULL;
    symbol_table_free(&symbols);
    return ok ? 0 : 1;
}

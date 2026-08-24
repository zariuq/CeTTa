#include "oslf_native_type_plan_v1.h"

#include "atom.h"
#include "symbol.h"

#include <stdio.h>
#include <string.h>

static uint32_t checks_run;
static uint32_t checks_failed;

static bool expect(bool condition, const char *message) {
    checks_run++;
    if (!condition) {
        checks_failed++;
        fprintf(stderr, "FAIL: %s\n", message);
    }
    return condition;
}

static bool validate_program_shape(const PPOSLFNativeTypePlanV1 *plan) {
    bool ok = true;

    ok = expect(plan &&
                    (plan->head_signature_len == 0u ||
                     plan->head_signatures) &&
                    (plan->term_len == 0u || plan->terms) &&
                    (plan->term_edge_len == 0u || plan->term_edges) &&
                    (plan->body_root_len == 0u || plan->body_roots) &&
                    (plan->step_schema_len == 0u || plan->step_schemas),
                "native program has an incomplete vector") &&
         expect(plan &&
                    plan->head_step_index_len == plan->step_schema_len &&
                    (plan->head_group_len == 0u || plan->head_groups) &&
                    (plan->head_step_index_len == 0u ||
                     plan->head_step_indices) &&
                    (plan->external_relation_len == 0u ||
                     plan->external_relations) &&
                    (plan->open_head_len == 0u || plan->open_heads),
                "native program has an incomplete head index") &&
         ok;
    if (!plan)
        return false;
    for (uint32_t index = 0u; index < plan->term_edge_len; index++) {
        ok = expect(plan->term_edges[index] < plan->term_len,
                    "native term edge is out of range") &&
             ok;
    }
    for (uint32_t index = 0u; index < plan->body_root_len; index++) {
        ok = expect(plan->body_roots[index] < plan->term_len,
                    "native body root is out of range") &&
             ok;
    }
    for (uint32_t index = 0u; index < plan->term_len; index++) {
        const PPOSLFNativeTermV1 *term = &plan->terms[index];
        bool scalar = term->kind != PPOSLF_NATIVE_TERM_APPLICATION_V1;

        ok = expect(term->edge_begin <= plan->term_edge_len &&
                        term->edge_len <=
                            plan->term_edge_len - term->edge_begin,
                    "native term edge span is out of range") &&
             expect(!scalar || term->edge_len == 0u,
                    "native scalar unexpectedly has children") &&
             ok;
        if (term->kind == PPOSLF_NATIVE_TERM_APPLICATION_V1) {
            uint32_t arity = 0u;
            ok = expect(term->text &&
                            pposlf_native_type_plan_v1_head_arity(
                                plan, term->text, &arity) &&
                            arity == term->edge_len,
                        "native application violates its signature") &&
                 ok;
        } else if (term->kind == PPOSLF_NATIVE_TERM_SYMBOL_V1 ||
                   term->kind == PPOSLF_NATIVE_TERM_INTEGER_BIG_V1 ||
                   term->kind == PPOSLF_NATIVE_TERM_STRING_V1) {
            ok = expect(term->text != NULL,
                        "native scalar lost its text payload") &&
                 ok;
        }
    }
    for (uint32_t index = 0u; index < plan->step_schema_len; index++) {
        const PPOSLFNativeStepSchemaV1 *step = &plan->step_schemas[index];
        uint32_t begin = 0u;
        uint32_t end = 0u;

        ok = expect(step->owner && step->rule &&
                        step->head < plan->term_len &&
                        step->body_begin <= plan->body_root_len &&
                        step->body_len <=
                            plan->body_root_len - step->body_begin,
                    "native step schema has an invalid span") &&
             expect(pposlf_native_type_plan_v1_step_range(
                        plan, step->owner, step->rule, &begin, &end) &&
                        begin == index && end == index + 1u,
                    "native step index disagrees with its program") &&
             ok;
    }
    for (uint32_t index = 0u; index < plan->head_group_len; index++) {
        const PPOSLFNativeHeadGroupV1 *group = &plan->head_groups[index];
        uint32_t found = 0u;

        ok = expect(group->step_begin <= plan->head_step_index_len &&
                        group->step_len <=
                            plan->head_step_index_len - group->step_begin,
                    "native head group has an invalid span") &&
             expect(pposlf_native_type_plan_v1_head_group(
                        plan, group->kind, group->text, group->integer,
                        &found) &&
                        found == index,
                    "native head lookup disagrees with its index") &&
             ok;
        for (uint32_t item = 0u; item < group->step_len; item++) {
            uint32_t step_index =
                plan->head_step_indices[group->step_begin + item];
            ok = expect(step_index < plan->step_schema_len,
                        "native head index names an absent step") &&
                 ok;
        }
    }
    for (uint32_t index = 0u;
         index < plan->external_relation_len; index++) {
        const PPOSLFNativeExternalRelationV1 *external =
            &plan->external_relations[index];
        uint32_t found = 0u;
        uint32_t arity = 0u;
        uint32_t body_occurrences = 0u;

        for (uint32_t body = 0u; body < plan->body_root_len; body++) {
            const PPOSLFNativeTermV1 *term =
                &plan->terms[plan->body_roots[body]];
            if (term->kind == PPOSLF_NATIVE_TERM_APPLICATION_V1 &&
                term->text && external->relation &&
                strcmp(term->text, external->relation) == 0 &&
                term->edge_len == external->arity)
                body_occurrences++;
        }
        ok = expect(external->owner && external->relation &&
                        external->body_occurrences > 0u &&
                        external->body_occurrences == body_occurrences,
                    "extensional interface is malformed or unreferenced") &&
             expect(pposlf_native_type_plan_v1_head_arity(
                        plan, external->relation, &arity) &&
                        arity == external->arity,
                    "extensional interface violates its signature") &&
             expect(pposlf_native_type_plan_v1_external_relation(
                        plan, external->relation, external->arity,
                        &found) &&
                        found == index,
                    "extensional interface lookup disagrees with its index") &&
             ok;
    }
    for (uint32_t index = 0u; index < plan->open_head_len; index++) {
        const PPOSLFNativeOpenHeadV1 *open = &plan->open_heads[index];
        uint32_t group = 0u;
        uint32_t external = 0u;

        ok = expect(open->body_occurrences > 0u &&
                        !pposlf_native_type_plan_v1_head_group(
                            plan, open->kind, open->text,
                            open->integer, &group),
                    "open relation is defined or unreferenced") &&
             expect(open->kind == PPOSLF_NATIVE_TERM_APPLICATION_V1 &&
                        pposlf_native_type_plan_v1_external_relation(
                            plan, open->text, open->arity, &external),
                    "open relation lacks its authored interface") &&
             ok;
    }
    return ok;
}

static bool load_expected(const char *path,
                          uint32_t expected_heads,
                          uint32_t expected_steps,
                          uint32_t expected_external,
                          uint32_t expected_open) {
    PPOSLFNativeTypePlanV1 plan;
    char error[512] = {0};
    bool ok = false;

    pposlf_native_type_plan_v1_init(&plan);
    if (!expect(pposlf_native_type_plan_v1_load(
                    &plan, path, error, sizeof(error)),
                error[0] ? error : "native program did not load"))
        goto done;
    ok = expect(plan.head_signature_len == expected_heads,
                "native program head count changed") &&
         expect(plan.step_schema_len == expected_steps,
                "native program step count changed") &&
         expect(plan.external_relation_len == expected_external,
                "native program interface count changed") &&
         expect(plan.open_head_len == expected_open,
                "native program open-relation count changed") &&
         expect(plan.term_len > 0u && plan.semantic_digest[0] != '\0',
                "native program did not retain executable terms") &&
         validate_program_shape(&plan);

done:
    pposlf_native_type_plan_v1_free(&plan);
    return ok;
}

static bool validate_structural_canary(const char *path) {
    bool kinds[PPOSLF_NATIVE_TERM_APPLICATION_V1 + 1u] = {false};
    PPOSLFNativeTypePlanV1 plan;
    char error[512] = {0};
    bool empty_body = false;
    bool two_premises = false;
    bool recursive = false;
    bool ok = false;

    pposlf_native_type_plan_v1_init(&plan);
    if (!expect(pposlf_native_type_plan_v1_load(
                    &plan, path, error, sizeof(error)),
                error[0] ? error : "structural native-program canary failed") ||
        !expect(plan.head_signature_len == 25u &&
                    plan.step_schema_len == 30u,
                "structural native-program inventory changed") ||
        !expect(plan.external_relation_len == 0u,
                "closed structural canary acquired an interface") ||
        !expect(plan.open_head_len == 0u,
                "closed structural canary acquired an open relation") ||
        !validate_program_shape(&plan))
        goto done;
    for (uint32_t index = 0u; index < plan.term_len; index++) {
        if ((uint32_t)plan.terms[index].kind <
            sizeof(kinds) / sizeof(kinds[0]))
            kinds[plan.terms[index].kind] = true;
    }
    for (uint32_t index = 0u; index < plan.step_schema_len; index++) {
        const PPOSLFNativeStepSchemaV1 *step = &plan.step_schemas[index];
        const PPOSLFNativeTermV1 *head = &plan.terms[step->head];

        empty_body = empty_body || step->body_len == 0u;
        two_premises = two_premises || step->body_len == 2u;
        if (head->kind != PPOSLF_NATIVE_TERM_APPLICATION_V1)
            continue;
        for (uint32_t body = 0u; body < step->body_len; body++) {
            const PPOSLFNativeTermV1 *premise =
                &plan.terms[plan.body_roots[step->body_begin + body]];
            if (premise->kind == PPOSLF_NATIVE_TERM_APPLICATION_V1 &&
                strcmp(head->text, premise->text) == 0)
                recursive = true;
        }
    }
    ok = expect(kinds[PPOSLF_NATIVE_TERM_SYMBOL_V1] &&
                    kinds[PPOSLF_NATIVE_TERM_INTEGER_I64_V1] &&
                    kinds[PPOSLF_NATIVE_TERM_INTEGER_BIG_V1] &&
                    kinds[PPOSLF_NATIVE_TERM_STRING_V1] &&
                    kinds[PPOSLF_NATIVE_TERM_VARIABLE_V1] &&
                    kinds[PPOSLF_NATIVE_TERM_APPLICATION_V1],
                "structural canary did not exercise the whole term ABI") &&
         expect(empty_body && two_premises && recursive,
                "structural canary lost a program control shape");

done:
    pposlf_native_type_plan_v1_free(&plan);
    return ok;
}

static bool expect_rejected(const char *path) {
    PPOSLFNativeTypePlanV1 plan;
    char error[512] = {0};
    bool rejected;

    pposlf_native_type_plan_v1_init(&plan);
    rejected = !pposlf_native_type_plan_v1_load(
        &plan, path, error, sizeof(error));
    pposlf_native_type_plan_v1_free(&plan);
    return expect(rejected && error[0] != '\0',
                  "malformed native program was accepted");
}

static bool expect_rejected_with(const char *path,
                                 const char *diagnostic) {
    PPOSLFNativeTypePlanV1 plan;
    char error[512] = {0};
    bool rejected;

    pposlf_native_type_plan_v1_init(&plan);
    rejected = !pposlf_native_type_plan_v1_load(
        &plan, path, error, sizeof(error));
    pposlf_native_type_plan_v1_free(&plan);
    return expect(rejected && strstr(error, diagnostic) != NULL,
                  "interface mutation did not fail for its structural reason");
}

static bool transactional_rejection(const char *good_path,
                                    const char *bad_path) {
    PPOSLFNativeTypePlanV1 plan;
    char digest[65];
    char error[512] = {0};
    uint32_t heads;
    uint32_t steps;
    bool ok = false;

    pposlf_native_type_plan_v1_init(&plan);
    if (!expect(pposlf_native_type_plan_v1_load(
                    &plan, good_path, error, sizeof(error)),
                "transactional canary could not load its baseline"))
        goto done;
    memcpy(digest, plan.semantic_digest, sizeof(digest));
    heads = plan.head_signature_len;
    steps = plan.step_schema_len;
    error[0] = '\0';
    ok = expect(!pposlf_native_type_plan_v1_load(
                    &plan, bad_path, error, sizeof(error)) &&
                    error[0] != '\0',
                "transactional canary accepted a malformed replacement") &&
         expect(plan.head_signature_len == heads &&
                    plan.step_schema_len == steps &&
                    memcmp(plan.semantic_digest, digest, sizeof(digest)) == 0,
                "failed replacement damaged the admitted program");

done:
    pposlf_native_type_plan_v1_free(&plan);
    return ok;
}

static bool deletion_changes_program(const char *full_path,
                                     const char *deleted_path) {
    PPOSLFNativeTypePlanV1 full;
    PPOSLFNativeTypePlanV1 deleted;
    char error[512] = {0};
    bool ok = false;

    pposlf_native_type_plan_v1_init(&full);
    pposlf_native_type_plan_v1_init(&deleted);
    if (!expect(pposlf_native_type_plan_v1_load(
                    &full, full_path, error, sizeof(error)),
                "deletion baseline did not load") ||
        !expect(pposlf_native_type_plan_v1_load(
                    &deleted, deleted_path, error, sizeof(error)),
                "well-formed deletion mutant did not load"))
        goto done;
    ok = expect(full.step_schema_len == deleted.step_schema_len + 1u,
                "step deletion did not change the executable program") &&
         expect(strcmp(full.semantic_digest, deleted.semantic_digest) != 0,
                "step deletion did not change the program digest") &&
         validate_program_shape(&deleted);

done:
    pposlf_native_type_plan_v1_free(&deleted);
    pposlf_native_type_plan_v1_free(&full);
    return ok;
}

int main(int argc, char **argv) {
    SymbolTable symbols;
    bool ok;

    if (argc != 15) {
        fprintf(stderr,
                "usage: %s LARGE-PROGRAM SECOND-PROGRAM CANARY "
                "BAD-ARITY BAD-CONSTRUCTOR BAD-BODY DELETED-STEP "
                "OPEN MISSING-INTERFACE WRONG-RELATION WRONG-ARITY "
                "DUPLICATE-INTERFACE WRONG-TRANSITION MISSING-CARRIER\n",
                argv[0]);
        return 2;
    }
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;

    ok = load_expected(argv[1], 254u, 647u, 25u, 14u) &&
         load_expected(argv[2], 177u, 989u, 8u, 7u) &&
         validate_structural_canary(argv[3]) &&
         expect_rejected(argv[4]) &&
         expect_rejected(argv[5]) &&
         expect_rejected(argv[6]) &&
         transactional_rejection(argv[3], argv[4]) &&
         deletion_changes_program(argv[3], argv[7]) &&
         load_expected(argv[8], 5u, 4u, 1u, 1u) &&
         expect_rejected_with(
             argv[9], "lacks an authored extensional interface") &&
         expect_rejected_with(
             argv[10], "authored extensional interface violates its relation signature") &&
         expect_rejected_with(
             argv[11], "authored extensional interface violates its relation signature") &&
         expect_rejected_with(
             argv[12], "finite-Horn answers are not strictly ordered") &&
         expect_rejected_with(
             argv[13], "unknown or malformed fact") &&
         expect_rejected_with(
             argv[14], "exactly one admitted finite-Horn carrier") &&
         transactional_rejection(argv[8], argv[9]);

    g_symbols = NULL;
    symbol_table_free(&symbols);
    printf("(OSLFNativeTypeProgramV1Summary %u %u %u)\n",
           checks_run, checks_run - checks_failed, checks_failed);
    return ok ? 0 : 1;
}

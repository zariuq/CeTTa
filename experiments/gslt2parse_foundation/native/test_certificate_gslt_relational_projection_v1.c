#include "certificate_gslt_relational_projection_v1.h"

#include "atom.h"
#include "symbol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t passed;
static uint32_t failed;

static void check(bool condition, const char *name) {
    if (condition) {
        passed++;
        printf("PASS: %s\n", name);
    } else {
        failed++;
        printf("FAIL: %s\n", name);
    }
}

static bool name_equal(PPCertificateGSLTNameV1 left, PPCertificateGSLTNameV1 right) {
    return left.len == right.len &&
           (left.len == 0u ||
            memcmp(left.bytes, right.bytes, left.len) == 0);
}

static bool make_state_plan(
    const PPCertificateGSLTRelationalProjectionV1 *projection,
    PPRelationalStateProgramV1Plan *state_plan) {
    uint32_t index;

    memset(state_plan, 0, sizeof(*state_plan));
    if (projection->table_len != 0u) {
        state_plan->tables =
            calloc(projection->table_len, sizeof(*state_plan->tables));
        if (!state_plan->tables)
            return false;
    }
    state_plan->table_len = projection->table_len;
    for (index = 0u; index < projection->table_len; index++) {
        const PPCertificateGSLTRelationalProjectionTableV1 *source =
            &projection->tables[index];
        char *name = malloc((size_t)source->table.len + 1u);

        if (!name)
            return false;
        memcpy(name, source->table.bytes, source->table.len);
        name[source->table.len] = '\0';
        state_plan->tables[index].name = name;
        state_plan->tables[index].arity = source->arity;
        state_plan->tables[index].key_arity = source->key_arity;
    }
    memset(state_plan->plan_digest, '1', 64u);
    state_plan->plan_digest[64] = '\0';
    return true;
}

static void free_state_plan(PPRelationalStateProgramV1Plan *state_plan) {
    uint32_t index;

    for (index = 0u; index < state_plan->table_len; index++)
        free(state_plan->tables[index].name);
    free(state_plan->tables);
    memset(state_plan, 0, sizeof(*state_plan));
}

static bool has_repeated_selector_role(
    const PPCertificateGSLTRelationalProjectionV1 *projection) {
    uint32_t left;
    uint32_t right;

    for (left = 0u; left < projection->selector_len; left++) {
        for (right = left + 1u; right < projection->selector_len; right++) {
            if (name_equal(projection->selectors[left].role,
                           projection->selectors[right].role))
                return true;
        }
    }
    return false;
}

int main(int argc, char **argv) {
    SymbolTable symbols;
    PPCertificateGSLTRelationalProjectionV1 first;
    PPCertificateGSLTRelationalProjectionV1 second;
    PPCertificateGSLTRelationalProjectionV1 invalid;
    PPRelationalStateProgramV1Plan first_state;
    PPRelationalStateProgramV1Plan second_state;
    PPCertificateGSLTArticleV1Result result;
    char error[512] = {0};
    uint32_t retained_table_id = UINT32_MAX;
    uint32_t index;

    if (argc != 5) {
        fprintf(stderr,
                "usage: %s FIRST SECOND MISSING_IDENTITY MIXED_OWNER\n",
                argv[0]);
        return 2;
    }
    ppcertificate_gslt_relational_projection_v1_init(&first);
    ppcertificate_gslt_relational_projection_v1_init(&second);
    ppcertificate_gslt_relational_projection_v1_init(&invalid);
    memset(&first_state, 0, sizeof(first_state));
    memset(&second_state, 0, sizeof(second_state));

    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;

    result = ppcertificate_gslt_relational_projection_v1_read(
        &first, argv[1], error, sizeof(error));
    check(result == PPCERTIFICATE_GSLT_ARTICLE_V1_OK,
          "first variable-length projection descriptor loads");
    check(result == PPCERTIFICATE_GSLT_ARTICLE_V1_OK &&
              first.table_len == 9u && first.selector_len == 6u,
          "first descriptor preserves its generated inventory");
    check(result == PPCERTIFICATE_GSLT_ARTICLE_V1_OK &&
              first.artifact_digest[0] != '\0' &&
              first.state_plan_digest[0] == '\0',
          "descriptor digest precedes state binding");
    check(result == PPCERTIFICATE_GSLT_ARTICLE_V1_OK &&
              has_repeated_selector_role(&first),
          "multi-valued selector roles retain relational multiplicity");
    check(result == PPCERTIFICATE_GSLT_ARTICLE_V1_OK &&
              make_state_plan(&first, &first_state),
          "first state-plan fixture derives from descriptor data");
    result = ppcertificate_gslt_relational_projection_v1_bind_state(
        &first, &first_state, error, sizeof(error));
    check(result == PPCERTIFICATE_GSLT_ARTICLE_V1_OK,
          "first descriptor binds to its generated state shape");
    check(result == PPCERTIFICATE_GSLT_ARTICLE_V1_OK &&
              first.state_plan_digest[0] != '\0',
          "state binding records its plan identity");
    for (index = 0u; index < first.table_len; index++) {
        if (first.tables[index].table_id != index)
            break;
    }
    check(index == first.table_len,
          "all first descriptor tables resolve without fixed slots");

    result = ppcertificate_gslt_relational_projection_v1_read(
        &second, argv[2], error, sizeof(error));
    check(result == PPCERTIFICATE_GSLT_ARTICLE_V1_OK,
          "structurally different projection descriptor loads");
    check(result == PPCERTIFICATE_GSLT_ARTICLE_V1_OK &&
              second.table_len == 7u && second.selector_len == 5u &&
              second.table_len != first.table_len,
          "second descriptor changes table and selector inventory lengths");
    check(result == PPCERTIFICATE_GSLT_ARTICLE_V1_OK &&
              make_state_plan(&second, &second_state),
          "second state-plan fixture derives from descriptor data");
    result = ppcertificate_gslt_relational_projection_v1_bind_state(
        &second, &second_state, error, sizeof(error));
    check(result == PPCERTIFICATE_GSLT_ARTICLE_V1_OK,
          "unchanged loader binds the structurally different descriptor");

    if (first.table_len != 0u) {
        retained_table_id = first.tables[0].table_id;
        first_state.tables[0].arity++;
    }
    result = ppcertificate_gslt_relational_projection_v1_bind_state(
        &first, &first_state, error, sizeof(error));
    check(result == PPCERTIFICATE_GSLT_ARTICLE_V1_UNSUPPORTED,
          "state-table shape falsification fails closed");
    check(first.table_len == 0u ||
              first.tables[0].table_id == retained_table_id,
          "failed state binding is transactional");
    if (first.table_len != 0u)
        first_state.tables[0].arity--;

    if (first_state.table_len != 0u)
        first_state.table_len--;
    result = ppcertificate_gslt_relational_projection_v1_bind_state(
        &first, &first_state, error, sizeof(error));
    check(result == PPCERTIFICATE_GSLT_ARTICLE_V1_UNSUPPORTED,
          "missing state table fails closed");
    if (first.table_len != 0u)
        first_state.table_len++;

    if (first_state.table_len > 1u) {
        char *saved_name = first_state.tables[1].name;
        first_state.tables[1].name = first_state.tables[0].name;
        result = ppcertificate_gslt_relational_projection_v1_bind_state(
            &first, &first_state, error, sizeof(error));
        check(result == PPCERTIFICATE_GSLT_ARTICLE_V1_UNSUPPORTED,
              "ambiguous state table identity fails closed");
        first_state.tables[1].name = saved_name;
    } else {
        check(false, "ambiguous state table identity fails closed");
    }

    first_state.plan_digest[0] = 'G';
    result = ppcertificate_gslt_relational_projection_v1_bind_state(
        &first, &first_state, error, sizeof(error));
    check(result == PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
          "invalid state-plan digest is rejected");
    first_state.plan_digest[0] = '1';

    result = ppcertificate_gslt_relational_projection_v1_read(
        &invalid, argv[3], error, sizeof(error));
    check(result == PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
          "descriptor without identity is rejected");
    result = ppcertificate_gslt_relational_projection_v1_read(
        &invalid, argv[4], error, sizeof(error));
    check(result == PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
          "descriptor with mixed owners is rejected");

    ppcertificate_gslt_relational_projection_v1_free(&invalid);
    ppcertificate_gslt_relational_projection_v1_free(&second);
    ppcertificate_gslt_relational_projection_v1_free(&first);
    free_state_plan(&second_state);
    free_state_plan(&first_state);
    g_symbols = NULL;
    symbol_table_free(&symbols);

    printf("---\n%u passed, %u failed\n", passed, failed);
    return failed == 0u ? 0 : 1;
}

#include "../../src/gslt_indexed_value_table_v1.h"

#include <stdint.h>
#include <stdio.h>

static uint32_t checks;
static uint32_t failures;

static void expect_true(int condition, const char *message) {
    checks++;
    if (!condition) {
        failures++;
        fprintf(stderr, "FAIL: %s\n", message);
    }
}

static int value_equal(CettaGsltIndexedValueV1 left,
                       CettaGsltIndexedValueV1 right) {
    return left.tag == right.tag && left.first == right.first &&
           left.second == right.second;
}

int main(void) {
    CettaGsltIndexedValueTableV1 table;
    CettaGsltIndexedValueV1 value;
    CettaGsltIndexedValueV1 proof_formula = {1u, 12u, 3u};
    CettaGsltIndexedValueV1 proof_rule = {2u, 41u, 0u};
    CettaGsltIndexedValueV1 parser_action = {7u, 2u, 9u};
    CettaGsltIndexedValueV1 *retained_items;
    uint32_t retained_capacity;

    cetta_gslt_indexed_value_table_init_v1(&table);
    expect_true(table.items == NULL && table.len == 0u && table.cap == 0u,
                "initial table is empty");
    expect_true(!cetta_gslt_indexed_value_table_get_v1(&table, 0u, &value),
                "empty lookup fails closed");
    expect_true(!cetta_gslt_indexed_value_table_get_v1(NULL, 0u, &value),
                "null table lookup fails closed");
    expect_true(!cetta_gslt_indexed_value_table_get_v1(&table, 0u, NULL),
                "null output lookup fails closed");

    expect_true(cetta_gslt_indexed_value_table_push_v1(&table, proof_formula),
                "proof-like formula value appends");
    expect_true(cetta_gslt_indexed_value_table_push_v1(&table, proof_rule),
                "proof-like rule value appends");
    expect_true(cetta_gslt_indexed_value_table_push_v1(&table, proof_formula),
                "duplicate occurrence appends without deduplication");
    expect_true(table.len == 3u, "proof-like header retains multiplicity");
    expect_true(cetta_gslt_indexed_value_table_get_v1(&table, 0u, &value) &&
                    value_equal(value, proof_formula),
                "first proof-like index is exact");
    expect_true(cetta_gslt_indexed_value_table_get_v1(&table, 1u, &value) &&
                    value_equal(value, proof_rule),
                "second proof-like index is exact");
    expect_true(cetta_gslt_indexed_value_table_get_v1(&table, 2u, &value) &&
                    value_equal(value, proof_formula),
                "duplicate proof-like index remains exact");
    expect_true(!cetta_gslt_indexed_value_table_get_v1(&table, 3u, &value),
                "out-of-range proof-like index is rejected");

    retained_items = table.items;
    retained_capacity = table.cap;
    cetta_gslt_indexed_value_table_reset_v1(&table);
    expect_true(table.len == 0u, "transaction reset clears logical length");
    expect_true(table.items == retained_items && table.cap == retained_capacity,
                "transaction reset retains allocated capacity");
    expect_true(cetta_gslt_indexed_value_table_push_v1(&table, parser_action),
                "parser-like action value appends after reset");
    expect_true(cetta_gslt_indexed_value_table_get_v1(&table, 0u, &value) &&
                    value_equal(value, parser_action),
                "parser-like action index is exact");
    expect_true(table.items == retained_items,
                "parser-like reuse avoids a replacement allocation");
    expect_true(!cetta_gslt_indexed_value_table_push_v1(NULL, parser_action),
                "null append fails closed");

    cetta_gslt_indexed_value_table_free_v1(&table);
    expect_true(table.items == NULL && table.len == 0u && table.cap == 0u,
                "free restores the empty state");
    cetta_gslt_indexed_value_table_free_v1(NULL);

    printf("GsltIndexedValueTableV1Summary checks=%u failures=%u\n",
           checks, failures);
    return failures == 0u ? 0 : 1;
}

#include "gslt_split_indexed_table_v1.h"

#include <stdio.h>

typedef struct {
    uint32_t opcode;
    uint32_t payload;
} PreparedValue;

typedef struct {
    uint32_t continuation;
    uint32_t evidence;
    uint32_t destination;
} SavedValue;

static unsigned checks;
static unsigned failures;

static void expect(bool condition, const char *message) {
    checks++;
    if (!condition) {
        failures++;
        fprintf(stderr, "FAIL: %s\n", message);
    }
}

int main(void) {
    const PreparedValue prepared[] = {{1u, 11u}, {2u, 22u}};
    const SavedValue saved[] = {{7u, 8u, 9u}, {10u, 11u, 12u}};
    CettaGsltSplitIndexedTableV1 table = {
        .prepared = prepared,
        .prepared_len = 2u,
        .prepared_stride = sizeof(prepared[0]),
        .saved = saved,
        .saved_len = 2u,
        .saved_stride = sizeof(saved[0]),
    };
    CettaGsltSplitIndexedValueV1 value;

    expect(cetta_gslt_split_indexed_table_validate_v1(&table),
           "well-formed split table is admitted");
    expect(cetta_gslt_split_indexed_table_get_v1(&table, 0u, &value) &&
               value.kind == CETTA_GSLT_SPLIT_INDEXED_VALUE_V1_PREPARED &&
               value.value == &prepared[0],
           "first prepared occurrence keeps index zero");
    expect(cetta_gslt_split_indexed_table_get_v1(&table, 1u, &value) &&
               value.kind == CETTA_GSLT_SPLIT_INDEXED_VALUE_V1_PREPARED &&
               value.value == &prepared[1],
           "last prepared occurrence keeps its exact index");
    expect(cetta_gslt_split_indexed_table_get_v1(&table, 2u, &value) &&
               value.kind == CETTA_GSLT_SPLIT_INDEXED_VALUE_V1_SAVED &&
               value.value == &saved[0],
           "saved suffix begins at the prepared prefix length");
    expect(cetta_gslt_split_indexed_table_get_v1(&table, 3u, &value) &&
               value.kind == CETTA_GSLT_SPLIT_INDEXED_VALUE_V1_SAVED &&
               value.value == &saved[1],
           "saved suffix preserves append order");
    expect(!cetta_gslt_split_indexed_table_get_v1(&table, 4u, &value) &&
               value.value == NULL,
           "out-of-range lookup fails closed");

    table.prepared = NULL;
    expect(!cetta_gslt_split_indexed_table_validate_v1(&table),
           "nonempty prepared region requires storage");
    table.prepared = prepared;
    table.saved_stride = 0u;
    expect(!cetta_gslt_split_indexed_table_get_v1(&table, 2u, &value),
           "nonempty saved region with zero stride fails closed");
    expect(!cetta_gslt_split_indexed_table_get_v1(NULL, 0u, &value),
           "absent split table fails closed");
    expect(!cetta_gslt_split_indexed_table_get_v1(&table, 0u, NULL),
           "absent output fails closed");

    table = (CettaGsltSplitIndexedTableV1){
        .saved = saved,
        .saved_len = 2u,
        .saved_stride = sizeof(saved[0]),
    };
    expect(cetta_gslt_split_indexed_table_get_v1(&table, 0u, &value) &&
               value.kind == CETTA_GSLT_SPLIT_INDEXED_VALUE_V1_SAVED &&
               value.value == &saved[0],
           "empty prepared prefix preserves suffix index zero");
    expect(!cetta_gslt_split_indexed_table_get_v1(
               &table, UINT64_MAX, &value),
           "maximum-width index fails without wrapping into the suffix");

    table = (CettaGsltSplitIndexedTableV1){
        .prepared = prepared,
        .prepared_len = 2u,
        .prepared_stride = sizeof(prepared[0]),
    };
    expect(!cetta_gslt_split_indexed_table_get_v1(&table, 2u, &value),
           "empty saved suffix rejects its first absent index");

    table.prepared_len = UINT32_MAX;
    table.prepared_stride = SIZE_MAX;
    expect(!cetta_gslt_split_indexed_table_validate_v1(&table),
           "overflowing region offset is rejected before lookup");

    printf("GsltSplitIndexedTableV1Summary checks=%u failures=%u\n",
           checks, failures);
    return failures == 0u ? 0 : 1;
}

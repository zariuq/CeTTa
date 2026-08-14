#include "relational_stack_proof_v1.h"

#include "relational_value_list_v1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    TEST_LABEL_KIND = 0,
    TEST_FORMULA,
    TEST_BINDER_VARIABLE,
    TEST_MANDATORY_VARIABLE,
    TEST_ASSERTION_HYPOTHESIS,
    TEST_ASSERTION_DISJOINT,
    TEST_ACTIVE_HYPOTHESIS,
    TEST_ACTIVE_DISJOINT,
    TEST_SYMBOL_KIND,
    TEST_TABLE_LEN
};

typedef struct {
    uint8_t *bytes;
    uint32_t len;
} TestValue;

typedef struct {
    uint32_t arity;
    uint32_t key_arity;
    uint32_t *rows;
    uint32_t row_len;
    uint32_t row_cap;
} TestTable;

typedef struct {
    TestValue *values;
    uint32_t value_len;
    uint32_t value_cap;
    TestTable tables[TEST_TABLE_LEN];
} TestStore;

typedef struct {
    PPRelationalValueV1Slice label;
    PPRelationalValueV1Slice claim[3];
    PPRelationalValueV1Slice normal_steps[4];
    PPRelationalValueV1Slice compressed_header[2];
    PPRelationalValueV1Slice compressed_code;
    PPRelationalValueV1Slice unknown_step;
    PPRelationalValueV1Slice unknown_code;
} TestInputs;

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

static bool grow(void **items, uint32_t *capacity,
                 uint32_t needed, size_t item_size) {
    uint32_t next = *capacity ? *capacity : 16u;
    void *grown;

    if (needed <= *capacity)
        return true;
    while (next < needed) {
        if (next > UINT32_MAX / 2u)
            return false;
        next *= 2u;
    }
    if (item_size > 0u && (size_t)next > SIZE_MAX / item_size)
        return false;
    grown = realloc(*items, (size_t)next * item_size);
    if (!grown)
        return false;
    *items = grown;
    *capacity = next;
    return true;
}

static void test_store_free(TestStore *store) {
    uint32_t index;

    if (!store)
        return;
    for (index = 0u; index < store->value_len; index++)
        free(store->values[index].bytes);
    for (index = 0u; index < TEST_TABLE_LEN; index++)
        free(store->tables[index].rows);
    free(store->values);
    memset(store, 0, sizeof(*store));
}

static bool test_value_intern(void *context, const uint8_t *bytes,
                              uint32_t len, uint32_t *value_out) {
    TestStore *store = context;
    uint32_t index;
    uint8_t *copy;

    if (!store || !bytes || len == 0u || !value_out)
        return false;
    for (index = 0u; index < store->value_len; index++) {
        if (store->values[index].len == len &&
            memcmp(store->values[index].bytes, bytes, len) == 0) {
            *value_out = index;
            return true;
        }
    }
    if (!grow((void **)&store->values, &store->value_cap,
              store->value_len + 1u, sizeof(*store->values)) ||
        !(copy = malloc(len)))
        return false;
    memcpy(copy, bytes, len);
    store->values[store->value_len] = (TestValue){copy, len};
    *value_out = store->value_len++;
    return true;
}

static bool test_value_bytes(void *context, uint32_t value,
                             const uint8_t **bytes_out,
                             uint32_t *len_out) {
    TestStore *store = context;
    if (!store || value >= store->value_len || !bytes_out || !len_out)
        return false;
    *bytes_out = store->values[value].bytes;
    *len_out = store->values[value].len;
    return true;
}

static bool test_table_shape(void *context, uint32_t table_id,
                             uint32_t *arity_out,
                             uint32_t *key_arity_out,
                             uint32_t *row_len_out) {
    TestStore *store = context;
    if (!store || table_id >= TEST_TABLE_LEN || !arity_out ||
        !key_arity_out || !row_len_out)
        return false;
    *arity_out = store->tables[table_id].arity;
    *key_arity_out = store->tables[table_id].key_arity;
    *row_len_out = store->tables[table_id].row_len;
    return true;
}

static bool test_table_row(void *context, uint32_t table_id,
                           uint32_t row_index, uint32_t *values_out,
                           uint32_t value_capacity) {
    TestStore *store = context;
    TestTable *table;
    if (!store || table_id >= TEST_TABLE_LEN || !values_out)
        return false;
    table = &store->tables[table_id];
    if (row_index >= table->row_len || value_capacity < table->arity)
        return false;
    memcpy(values_out, &table->rows[(size_t)row_index * table->arity],
           (size_t)table->arity * sizeof(*values_out));
    return true;
}

static bool test_table_find(void *context, uint32_t table_id,
                            const uint32_t *key, uint32_t key_len,
                            uint32_t *values_out,
                            uint32_t value_capacity) {
    TestStore *store = context;
    TestTable *table;
    uint32_t index;

    if (!store || table_id >= TEST_TABLE_LEN || !key || !values_out)
        return false;
    table = &store->tables[table_id];
    if (key_len != table->key_arity || value_capacity < table->arity)
        return false;
    for (index = 0u; index < table->row_len; index++) {
        uint32_t *row = &table->rows[(size_t)index * table->arity];
        if (memcmp(row, key, (size_t)key_len * sizeof(*key)) == 0) {
            memcpy(values_out, row,
                   (size_t)table->arity * sizeof(*values_out));
            return true;
        }
    }
    return false;
}

static bool test_table_prefix_next(
    void *context, uint32_t table_id,
    const uint32_t *prefix, uint32_t prefix_len,
    uint32_t column_mask,
    uint64_t *cursor_io, uint32_t *values_out,
    uint32_t value_capacity, bool *found_out) {
    TestStore *store = context;
    TestTable *table;
    uint32_t index;

    if (!store || table_id >= TEST_TABLE_LEN || !prefix ||
        prefix_len == 0u || !cursor_io || !values_out || !found_out)
        return false;
    table = &store->tables[table_id];
    if (prefix_len > table->key_arity || value_capacity < table->arity ||
        column_mask == 0u || table->arity > 32u ||
        (table->arity < 32u && (column_mask >> table->arity) != 0u))
        return false;
    if (*cursor_io == UINT64_MAX) {
        index = 0u;
    } else {
        if (*cursor_io > UINT32_MAX)
            return false;
        index = (uint32_t)*cursor_io;
    }
    *found_out = false;
    while (index < table->row_len) {
        uint32_t *row = &table->rows[(size_t)index * table->arity];
        index++;
        if (memcmp(row, prefix,
                   (size_t)prefix_len * sizeof(*prefix)) == 0) {
            uint32_t column;
            memset(values_out, 0,
                   (size_t)table->arity * sizeof(*values_out));
            for (column = 0u; column < table->arity; column++) {
                if ((column_mask & (UINT32_C(1) << column)) != 0u)
                    values_out[column] = row[column];
            }
            *cursor_io = index;
            *found_out = true;
            return true;
        }
    }
    *cursor_io = table->row_len;
    return true;
}

static bool test_table_immutable_prefix(
    void *context, uint32_t table_id, uint32_t *row_len_out) {
    TestStore *store = context;

    if (!store || !row_len_out || table_id >= TEST_TABLE_LEN)
        return false;
    *row_len_out = store->tables[table_id].row_len;
    return true;
}

static PPRelationalStoreV1 test_store_interface(TestStore *store) {
    return (PPRelationalStoreV1){
        .context = store,
        .identity = UINT64_C(1),
        .table_immutable_prefix = test_table_immutable_prefix,
        .table_shape = test_table_shape,
        .table_row = test_table_row,
        .table_find = test_table_find,
        .table_prefix_next = test_table_prefix_next,
        .value_intern = test_value_intern,
        .value_bytes = test_value_bytes,
    };
}

static bool test_add_row(TestStore *store, uint32_t table_id,
                         const uint32_t *values) {
    TestTable *table;
    uint32_t needed;

    if (!store || table_id >= TEST_TABLE_LEN || !values)
        return false;
    table = &store->tables[table_id];
    if (table->row_len == UINT32_MAX ||
        table->arity > UINT32_MAX / (table->row_len + 1u))
        return false;
    needed = (table->row_len + 1u) * table->arity;
    if (!grow((void **)&table->rows, &table->row_cap,
              needed, sizeof(*table->rows)))
        return false;
    memcpy(&table->rows[(size_t)table->row_len * table->arity], values,
           (size_t)table->arity * sizeof(*values));
    table->row_len++;
    return true;
}

static bool test_text(TestStore *store, const char *text,
                      uint32_t *value_out) {
    return test_value_intern(
        store, (const uint8_t *)text, (uint32_t)strlen(text), value_out);
}

static bool test_formula(TestStore *store, const uint32_t *items,
                         uint32_t item_len, uint32_t *formula_out) {
    PPRelationalValueV1Slice *slices;
    uint8_t *encoded = NULL;
    uint32_t encoded_len = 0u;
    uint32_t index;
    bool ok = false;

    if (!store || !items || item_len == 0u || !formula_out ||
        (size_t)item_len > SIZE_MAX / sizeof(*slices) ||
        !(slices = calloc(item_len, sizeof(*slices))))
        return false;
    for (index = 0u; index < item_len; index++) {
        if (!test_value_bytes(store, items[index], &slices[index].bytes,
                              &slices[index].len))
            goto done;
    }
    ok = pprelational_value_list_v1_encode_items(
             slices, item_len, &encoded, &encoded_len) &&
         test_value_intern(store, encoded, encoded_len, formula_out);
done:
    free(encoded);
    free(slices);
    return ok;
}

static PPRelationalValueV1Slice test_slice(const char *text) {
    return (PPRelationalValueV1Slice){
        .bytes = (const uint8_t *)text,
        .len = (uint32_t)strlen(text),
    };
}

static bool setup(TestStore *store,
                  PPRelationalStackProofV1Machine *machine,
                  PPRelationalStackProofV1CacheAdmission *admission,
                  TestInputs *inputs, uint32_t *active_disjoint_row_out) {
    static const uint32_t arities[TEST_TABLE_LEN] = {
        2u, 2u, 2u, 2u, 3u, 3u, 4u, 2u, 2u,
    };
    static const uint32_t key_arities[TEST_TABLE_LEN] = {
        1u, 1u, 1u, 2u, 2u, 3u, 1u, 2u, 1u,
    };
    uint32_t k_bind, k_match, k_rule_first, k_rule_second;
    uint32_t k_variable, k_literal, unknown;
    uint32_t type, constructor, x, y, a, b;
    uint32_t hx, hy, essential, rule, ha, hb, actual, claim;
    uint32_t p10, p20, p30;
    uint32_t f_hx, f_hy, f_essential, f_rule;
    uint32_t f_ha, f_hb, f_actual, f_claim;
    uint32_t items[3];
    uint32_t row[4];
    uint32_t index;

    memset(store, 0, sizeof(*store));
    memset(machine, 0, sizeof(*machine));
    memset(admission, 0, sizeof(*admission));
    memset(inputs, 0, sizeof(*inputs));
    for (index = 0u; index < TEST_TABLE_LEN; index++) {
        store->tables[index].arity = arities[index];
        store->tables[index].key_arity = key_arities[index];
    }
    if (!test_text(store, "GuestBinderKind", &k_bind) ||
        !test_text(store, "GuestMatchKind", &k_match) ||
        !test_text(store, "GuestRuleFirst", &k_rule_first) ||
        !test_text(store, "GuestRuleSecond", &k_rule_second) ||
        !test_text(store, "GuestVariableKind", &k_variable) ||
        !test_text(store, "GuestLiteralKind", &k_literal) ||
        !test_text(store, "GuestUnknown", &unknown) ||
        !test_text(store, "GuestType", &type) ||
        !test_text(store, "GuestConstructor", &constructor) ||
        !test_text(store, "GuestX", &x) ||
        !test_text(store, "GuestY", &y) ||
        !test_text(store, "GuestA", &a) ||
        !test_text(store, "GuestB", &b) ||
        !test_text(store, "GuestBindX", &hx) ||
        !test_text(store, "GuestBindY", &hy) ||
        !test_text(store, "GuestEssential", &essential) ||
        !test_text(store, "GuestRule", &rule) ||
        !test_text(store, "GuestBindA", &ha) ||
        !test_text(store, "GuestBindB", &hb) ||
        !test_text(store, "GuestActual", &actual) ||
        !test_text(store, "GuestClaim", &claim) ||
        !test_text(store, "10", &p10) ||
        !test_text(store, "20", &p20) ||
        !test_text(store, "30", &p30))
        return false;

    items[0] = type; items[1] = x;
    if (!test_formula(store, items, 2u, &f_hx)) return false;
    items[1] = y;
    if (!test_formula(store, items, 2u, &f_hy)) return false;
    items[0] = constructor; items[1] = x; items[2] = y;
    if (!test_formula(store, items, 3u, &f_essential)) return false;
    items[0] = type; items[1] = constructor; items[2] = x;
    if (!test_formula(store, items, 3u, &f_rule)) return false;
    items[0] = type; items[1] = a;
    if (!test_formula(store, items, 2u, &f_ha)) return false;
    items[1] = b;
    if (!test_formula(store, items, 2u, &f_hb)) return false;
    items[0] = constructor; items[1] = a; items[2] = b;
    if (!test_formula(store, items, 3u, &f_actual)) return false;
    items[0] = type; items[1] = constructor; items[2] = a;
    if (!test_formula(store, items, 3u, &f_claim)) return false;

#define ADD2(TABLE, A, B) \
    do { row[0] = (A); row[1] = (B); \
         if (!test_add_row(store, (TABLE), row)) return false; } while (0)
#define ADD3(TABLE, A, B, C) \
    do { row[0] = (A); row[1] = (B); row[2] = (C); \
         if (!test_add_row(store, (TABLE), row)) return false; } while (0)
#define ADD4(TABLE, A, B, C, D) \
    do { row[0] = (A); row[1] = (B); row[2] = (C); row[3] = (D); \
         if (!test_add_row(store, (TABLE), row)) return false; } while (0)

    ADD2(TEST_LABEL_KIND, hx, k_bind);
    ADD2(TEST_LABEL_KIND, hy, k_bind);
    ADD2(TEST_LABEL_KIND, essential, k_match);
    ADD2(TEST_LABEL_KIND, rule, k_rule_first);
    ADD2(TEST_LABEL_KIND, ha, k_bind);
    ADD2(TEST_LABEL_KIND, hb, k_bind);
    ADD2(TEST_LABEL_KIND, actual, k_match);
    ADD2(TEST_LABEL_KIND, claim, k_rule_first);
    ADD2(TEST_FORMULA, hx, f_hx);
    ADD2(TEST_FORMULA, hy, f_hy);
    ADD2(TEST_FORMULA, essential, f_essential);
    ADD2(TEST_FORMULA, rule, f_rule);
    ADD2(TEST_FORMULA, ha, f_ha);
    ADD2(TEST_FORMULA, hb, f_hb);
    ADD2(TEST_FORMULA, actual, f_actual);
    ADD2(TEST_FORMULA, claim, f_claim);
    ADD2(TEST_BINDER_VARIABLE, hx, x);
    ADD2(TEST_BINDER_VARIABLE, hy, y);
    ADD2(TEST_BINDER_VARIABLE, ha, a);
    ADD2(TEST_BINDER_VARIABLE, hb, b);
    ADD2(TEST_MANDATORY_VARIABLE, rule, x);
    ADD2(TEST_MANDATORY_VARIABLE, rule, y);
    ADD2(TEST_MANDATORY_VARIABLE, claim, a);
    ADD2(TEST_MANDATORY_VARIABLE, claim, b);
    ADD3(TEST_ASSERTION_HYPOTHESIS, rule, p10, hx);
    ADD3(TEST_ASSERTION_HYPOTHESIS, rule, p20, essential);
    ADD3(TEST_ASSERTION_HYPOTHESIS, rule, p30, hy);
    ADD3(TEST_ASSERTION_HYPOTHESIS, claim, p10, ha);
    ADD3(TEST_ASSERTION_HYPOTHESIS, claim, p20, hb);
    ADD3(TEST_ASSERTION_DISJOINT, rule, x, y);
    /* A repeated semantic obligation is intentional: the generic compiled
     * machine must reuse each slot's exact support summary rather than scan
     * both substitution images again. */
    ADD3(TEST_ASSERTION_DISJOINT, rule, x, y);
    ADD4(TEST_ACTIVE_HYPOTHESIS, ha, k_bind, a, f_ha);
    ADD4(TEST_ACTIVE_HYPOTHESIS, hb, k_bind, b, f_hb);
    ADD4(TEST_ACTIVE_HYPOTHESIS, actual, k_match, constructor, f_actual);
    *active_disjoint_row_out = store->tables[TEST_ACTIVE_DISJOINT].row_len;
    ADD2(TEST_ACTIVE_DISJOINT, a, b);
    ADD2(TEST_SYMBOL_KIND, type, k_literal);
    ADD2(TEST_SYMBOL_KIND, constructor, k_literal);
    ADD2(TEST_SYMBOL_KIND, x, k_variable);
    ADD2(TEST_SYMBOL_KIND, y, k_variable);
    ADD2(TEST_SYMBOL_KIND, a, k_variable);
    ADD2(TEST_SYMBOL_KIND, b, k_variable);

#undef ADD4
#undef ADD3
#undef ADD2

    *machine = (PPRelationalStackProofV1Machine){
        .label_kind_table = TEST_LABEL_KIND,
        .formula_table = TEST_FORMULA,
        .binder_variable_table = TEST_BINDER_VARIABLE,
        .mandatory_variable_table = TEST_MANDATORY_VARIABLE,
        .assertion_hypothesis_table = TEST_ASSERTION_HYPOTHESIS,
        .assertion_disjoint_table = TEST_ASSERTION_DISJOINT,
        .active_hypothesis_table = TEST_ACTIVE_HYPOTHESIS,
        .active_disjoint_table = TEST_ACTIVE_DISJOINT,
        .symbol_kind_table = TEST_SYMBOL_KIND,
        .binder_hypothesis_kind = k_bind,
        .matching_hypothesis_kind = k_match,
        .rule_kind_first = k_rule_first,
        .rule_kind_second = k_rule_second,
        .variable_symbol_kind = k_variable,
        .unknown_token = unknown,
        .terminal_low = (uint8_t)'A',
        .terminal_high = (uint8_t)'T',
        .continuation_low = (uint8_t)'U',
        .continuation_high = (uint8_t)'Y',
        .save_byte = (uint8_t)'Z',
        .unknown_byte = (uint8_t)'?',
        .terminal_radix = 20u,
        .terminal_digit_bias = 0u,
        .continuation_radix = 5u,
        .continuation_digit_bias = 1u,
        .unknown_policy = PPRELATIONAL_STACK_PROOF_V1_UNKNOWN_REJECT,
        .save_placement =
            CETTA_GSLT_INDEXED_SAVE_IMMEDIATELY_AFTER_USE_V1,
        .header_hypothesis_policy =
            CETTA_GSLT_HEADER_HYPOTHESIS_NONMANDATORY_ONLY_V1,
    };
    *admission = (PPRelationalStackProofV1CacheAdmission){
        .label_kind_table = TEST_LABEL_KIND,
        .formula_table = TEST_FORMULA,
        .binder_variable_table = TEST_BINDER_VARIABLE,
        .mandatory_variable_table = TEST_MANDATORY_VARIABLE,
        .assertion_hypothesis_table = TEST_ASSERTION_HYPOTHESIS,
        .assertion_disjoint_table = TEST_ASSERTION_DISJOINT,
        .symbol_kind_table = TEST_SYMBOL_KIND,
        .scratch_reuse_admitted = true,
        .finite_support_admitted = true,
        .indexed_values_admitted = true,
        .literal_hole_admitted = true,
        .two_phase_frame_admitted = true,
    };
    memset(admission->native_type_digest, '1', 64u);
    admission->native_type_digest[64] = '\0';
    memset(admission->storage_plan_digest, '2', 64u);
    admission->storage_plan_digest[64] = '\0';

    inputs->label = test_slice("GuestClaim");
    inputs->claim[0] = test_slice("GuestType");
    inputs->claim[1] = test_slice("GuestConstructor");
    inputs->claim[2] = test_slice("GuestA");
    inputs->normal_steps[0] = test_slice("GuestBindA");
    inputs->normal_steps[1] = test_slice("GuestActual");
    inputs->normal_steps[2] = test_slice("GuestBindB");
    inputs->normal_steps[3] = test_slice("GuestRule");
    inputs->compressed_header[0] = test_slice("GuestActual");
    inputs->compressed_header[1] = test_slice("GuestRule");
    inputs->compressed_code = test_slice("ACBD");
    inputs->unknown_step = test_slice("GuestUnknown");
    inputs->unknown_code = test_slice("?");
    return true;
}

int main(void) {
    TestStore test_store;
    PPRelationalStoreV1 store;
    PPRelationalStackProofV1Machine machine;
    PPRelationalStackProofV1CacheAdmission admission;
    PPRelationalStackProofV1Cache cache = {0};
    PPRelationalStackProofV1CacheStats stats;
    TestInputs inputs;
    PPRelationalStackProofV1NormalInput normal;
    PPRelationalStackProofV1CompressedInput compressed;
    PPRelationalStackProofV1Result uncached;
    PPRelationalStackProofV1Result cached;
    uint32_t active_disjoint_row = 0u;
    char error[512] = {0};

    if (!setup(&test_store, &machine, &admission, &inputs,
               &active_disjoint_row)) {
        fprintf(stderr, "cannot construct generic cache canary\n");
        return 2;
    }
    store = test_store_interface(&test_store);
    normal = (PPRelationalStackProofV1NormalInput){
        .label = inputs.label,
        .claim = inputs.claim,
        .claim_len = 3u,
        .steps = inputs.normal_steps,
        .step_len = 4u,
    };
    compressed = (PPRelationalStackProofV1CompressedInput){
        .label = inputs.label,
        .claim = inputs.claim,
        .claim_len = 3u,
        .header = inputs.compressed_header,
        .header_len = 2u,
        .code = &inputs.compressed_code,
        .code_len = 1u,
    };

    {
        const TestTable *table =
            &test_store.tables[TEST_ASSERTION_HYPOTHESIS];
        uint32_t prefix = table->row_len != 0u ? table->rows[0] : 0u;
        uint32_t projected[3] = {UINT32_MAX, UINT32_MAX, UINT32_MAX};
        uint64_t cursor = UINT64_MAX;
        bool found = false;

        expect(table->row_len != 0u &&
                   store.table_prefix_next(
                       store.context, TEST_ASSERTION_HYPOTHESIS,
                       &prefix, 1u, UINT32_C(1) << 2u,
                       &cursor, projected, 3u, &found) &&
                   found && projected[0] == 0u &&
                   projected[1] == 0u &&
                   projected[2] == table->rows[2],
               "prefix projection materialized a dead column");
        cursor = UINT64_MAX;
        expect(!store.table_prefix_next(
                   store.context, TEST_ASSERTION_HYPOTHESIS,
                   &prefix, 1u, 0u, &cursor,
                   projected, 3u, &found),
               "prefix projection accepted an empty column mask");
        cursor = UINT64_MAX;
        expect(!store.table_prefix_next(
                   store.context, TEST_ASSERTION_HYPOTHESIS,
                   &prefix, 1u, UINT32_C(1) << 3u,
                   &cursor, projected, 3u, &found),
               "prefix projection accepted a column past its schema");
    }

    expect(pprelational_stack_proof_v1_cache_init(
               &cache, &store, &machine, &admission,
               error, sizeof(error)),
           error[0] ? error : "admitted cache initialization failed");
    {
        PPRelationalStackProofV1CompressedInput repeated_implicit =
            compressed;
        PPRelationalValueV1Slice repeated_header =
            test_slice("GuestBindA");
        PPRelationalStackProofV1Cache strict_cache = {0};
        repeated_implicit.header = &repeated_header;
        repeated_implicit.header_len = 1u;
        expect(pprelational_stack_proof_v1_cache_init(
                   &strict_cache, &store, &machine, &admission,
                   error, sizeof(error)),
               error[0] ? error
                        : "strict-header cache initialization failed");
        uncached = pprelational_stack_proof_v1_compressed(
            &store, &machine, &repeated_implicit,
            error, sizeof(error));
        cached = pprelational_stack_proof_v1_compressed_cached(
            &store, &machine, &strict_cache, &repeated_implicit,
            error, sizeof(error));
        expect(uncached == PPRELATIONAL_STACK_PROOF_V1_REJECTED &&
                   cached == uncached &&
                   strstr(error, "implicitly prepared hypothesis") != NULL,
               "compressed header repeated an implicit hypothesis");
        pprelational_stack_proof_v1_cache_free(&strict_cache);
    }
    uncached = pprelational_stack_proof_v1_normal(
        &store, &machine, &normal, error, sizeof(error));
    cached = pprelational_stack_proof_v1_normal_cached(
        &store, &machine, &cache, &normal, error, sizeof(error));
    expect(uncached == PPRELATIONAL_STACK_PROOF_V1_OK &&
               cached == uncached,
           "cached and source normal machines disagree");
    cached = pprelational_stack_proof_v1_normal_cached(
        &store, &machine, &cache, &normal, error, sizeof(error));
    expect(cached == PPRELATIONAL_STACK_PROOF_V1_OK,
           "cached normal frame cannot be reused");
    expect(pprelational_stack_proof_v1_cache_stats(&cache, &stats) &&
               stats.entry_len == 1u && stats.miss_len == 1u &&
               stats.hit_len == 1u &&
               stats.lookup_index_capacity >= 16u &&
               stats.scratch_acquire_len == 2u &&
               stats.scratch_reuse_len == 1u &&
               stats.scratch_formula_capacity != 0u &&
               stats.scratch_stack_capacity != 0u &&
               stats.scratch_substitution_capacity != 0u &&
               stats.scratch_support_width >= 2u &&
               stats.scratch_support_slot_capacity >= 2u &&
               stats.scratch_support_compute_len == 4u &&
               stats.scratch_support_hit_len == 4u &&
               stats.template_head_len != 0u &&
               stats.template_part_len < stats.template_cell_len,
           "cache statistics do not expose one compile and one reuse");

    uncached = pprelational_stack_proof_v1_compressed(
        &store, &machine, &compressed, error, sizeof(error));
    cached = pprelational_stack_proof_v1_compressed_cached(
        &store, &machine, &cache, &compressed, error, sizeof(error));
    expect(uncached == PPRELATIONAL_STACK_PROOF_V1_OK &&
               cached == uncached,
           "cached and source compressed machines disagree");
    expect(pprelational_stack_proof_v1_cache_stats(&cache, &stats) &&
               stats.entry_len == 2u && stats.miss_len == 1u &&
               stats.hit_len == 2u &&
               stats.retained_source_len == 1u &&
               stats.source_reuse_len == 0u &&
               stats.scratch_acquire_len == 3u &&
               stats.scratch_reuse_len == 2u &&
               stats.scratch_heap_capacity != 0u,
           "compressed execution did not retain its admitted source frame");

    {
        PPRelationalValueV1Slice retained_steps[3] = {
            test_slice("GuestBindA"),
            test_slice("GuestBindB"),
            test_slice("GuestClaim"),
        };
        PPRelationalStackProofV1NormalInput retained_normal = normal;

        retained_normal.steps = retained_steps;
        retained_normal.step_len = 3u;
        uncached = pprelational_stack_proof_v1_normal(
            &store, &machine, &retained_normal, error, sizeof(error));
        cached = pprelational_stack_proof_v1_normal_cached(
            &store, &machine, &cache, &retained_normal,
            error, sizeof(error));
        expect(uncached == PPRELATIONAL_STACK_PROOF_V1_OK &&
                   cached == uncached,
               "retained source frame changed rule application");
        expect(pprelational_stack_proof_v1_cache_stats(&cache, &stats) &&
                   stats.entry_len == 2u && stats.miss_len == 2u &&
                   stats.hit_len == 2u &&
                   stats.retained_source_len == 1u &&
                   stats.source_reuse_len == 1u,
               "first rule application rebuilt its retained source frame");
        cached = pprelational_stack_proof_v1_normal_cached(
            &store, &machine, &cache, &retained_normal,
            error, sizeof(error));
        expect(cached == PPRELATIONAL_STACK_PROOF_V1_OK &&
                   pprelational_stack_proof_v1_cache_stats(
                       &cache, &stats) &&
                   stats.hit_len == 3u && stats.source_reuse_len == 1u,
               "compiled retained frame cannot be reused");
    }

    {
        PPRelationalValueV1Slice saved = inputs.claim[2];
        inputs.claim[2] = test_slice("GuestB");
        uncached = pprelational_stack_proof_v1_normal(
            &store, &machine, &normal, error, sizeof(error));
        cached = pprelational_stack_proof_v1_normal_cached(
            &store, &machine, &cache, &normal, error, sizeof(error));
        expect(uncached == PPRELATIONAL_STACK_PROOF_V1_REJECTED &&
                   cached == uncached,
               "falsified claim distinguishes cached and source machines");
        inputs.claim[2] = saved;
    }

    {
        TestTable *table = &test_store.tables[TEST_ACTIVE_DISJOINT];
        uint32_t *row = &table->rows[(size_t)active_disjoint_row * 2u];
        uint32_t saved = row[1];
        row[1] = row[0];
        uncached = pprelational_stack_proof_v1_normal(
            &store, &machine, &normal, error, sizeof(error));
        cached = pprelational_stack_proof_v1_normal_cached(
            &store, &machine, &cache, &normal, error, sizeof(error));
        expect(uncached == PPRELATIONAL_STACK_PROOF_V1_REJECTED &&
                   cached == uncached,
               "cache retained scoped disjoint authority");
        row[1] = saved;
    }

    {
        PPRelationalStackProofV1CacheAdmission bad = admission;
        PPRelationalStackProofV1Cache rejected_cache = {0};
        bad.formula_table = TEST_LABEL_KIND;
        expect(!pprelational_stack_proof_v1_cache_init(
                   &rejected_cache, &store, &machine, &bad,
                   error, sizeof(error)),
               "cache accepted mismatched generated admission");
        pprelational_stack_proof_v1_cache_free(&rejected_cache);
    }

    {
        PPRelationalStackProofV1CacheAdmission bad = admission;
        PPRelationalStackProofV1Cache rejected_cache = {0};
        bad.scratch_reuse_admitted = false;
        expect(!pprelational_stack_proof_v1_cache_init(
                   &rejected_cache, &store, &machine, &bad,
                   error, sizeof(error)),
               "cache accepted a missing generated scratch-region admission");
        pprelational_stack_proof_v1_cache_free(&rejected_cache);
    }

    {
        PPRelationalStackProofV1CacheAdmission bad = admission;
        PPRelationalStackProofV1Cache rejected_cache = {0};
        bad.two_phase_frame_admitted = false;
        expect(!pprelational_stack_proof_v1_cache_init(
                   &rejected_cache, &store, &machine, &bad,
                   error, sizeof(error)),
               "cache accepted a missing generated two-phase-frame admission");
        pprelational_stack_proof_v1_cache_free(&rejected_cache);
    }

    {
        PPRelationalStackProofV1CacheAdmission fallback_admission =
            admission;
        PPRelationalStackProofV1Cache fallback_cache = {0};
        PPRelationalStackProofV1CacheStats fallback_stats;
        fallback_admission.finite_support_admitted = false;
        expect(pprelational_stack_proof_v1_cache_init(
                   &fallback_cache, &store, &machine,
                   &fallback_admission, error, sizeof(error)) &&
                   pprelational_stack_proof_v1_normal_cached(
                       &store, &machine, &fallback_cache, &normal,
                       error, sizeof(error)) ==
                       pprelational_stack_proof_v1_normal(
                           &store, &machine, &normal,
                           error, sizeof(error)) &&
                   pprelational_stack_proof_v1_cache_stats(
                       &fallback_cache, &fallback_stats) &&
                   fallback_stats.scratch_support_width == 0u,
               "missing support admission did not select the exact source check");
        pprelational_stack_proof_v1_cache_free(&fallback_cache);
    }

    {
        PPRelationalStackProofV1CacheAdmission fallback_admission =
            admission;
        PPRelationalStackProofV1Cache fallback_cache = {0};
        PPRelationalStackProofV1CacheStats fallback_stats;
        fallback_admission.literal_hole_admitted = false;
        expect(pprelational_stack_proof_v1_cache_init(
                   &fallback_cache, &store, &machine,
                   &fallback_admission, error, sizeof(error)) &&
                   pprelational_stack_proof_v1_normal_cached(
                       &store, &machine, &fallback_cache, &normal,
                       error, sizeof(error)) ==
                       pprelational_stack_proof_v1_normal(
                           &store, &machine, &normal,
                           error, sizeof(error)) &&
                   pprelational_stack_proof_v1_cache_stats(
                       &fallback_cache, &fallback_stats) &&
                   fallback_stats.template_head_len == 0u &&
                   fallback_stats.template_part_len ==
                       fallback_stats.template_cell_len,
               "missing literal/hole admission coalesced a template");
        pprelational_stack_proof_v1_cache_free(&fallback_cache);
    }

    {
        PPRelationalStackProofV1CacheAdmission fallback_admission =
            admission;
        PPRelationalStackProofV1Cache fallback_cache = {0};
        fallback_admission.indexed_values_admitted = false;
        expect(pprelational_stack_proof_v1_cache_init(
                   &fallback_cache, &store, &machine,
                   &fallback_admission, error, sizeof(error)) &&
                   pprelational_stack_proof_v1_normal_cached(
                       &store, &machine, &fallback_cache, &normal,
                       error, sizeof(error)) ==
                       pprelational_stack_proof_v1_normal(
                           &store, &machine, &normal,
                           error, sizeof(error)) &&
                   pprelational_stack_proof_v1_compressed_cached(
                       &store, &machine, &fallback_cache, &compressed,
                       error, sizeof(error)) ==
                       PPRELATIONAL_STACK_PROOF_V1_INVALID &&
                   strstr(error, "prepared indexed-value table") != NULL,
               "missing indexed-value admission did not fail closed");
        pprelational_stack_proof_v1_cache_free(&fallback_cache);
    }

    {
        PPRelationalStackProofV1Machine mismatched_machine = machine;
        mismatched_machine.terminal_radix++;
        cached = pprelational_stack_proof_v1_normal_cached(
            &store, &mismatched_machine, &cache, &normal,
            error, sizeof(error));
        expect(cached == PPRELATIONAL_STACK_PROOF_V1_INVALID,
               "proof transaction accepted a differently bound machine");
    }

    {
        PPRelationalStackProofV1Machine incomplete_machine = machine;
        PPRelationalStackProofV1CacheAdmission incomplete_admission =
            admission;
        PPRelationalStackProofV1Cache incomplete_cache = {0};
        PPRelationalStackProofV1NormalInput incomplete_normal = normal;
        PPRelationalStackProofV1CompressedInput incomplete_compressed =
            compressed;
        PPRelationalValueV1Slice open_save_code =
            test_slice("ACBDUZ");
        PPRelationalValueV1Slice open_incomplete_code =
            test_slice("U?");
        PPRelationalValueV1Slice incomplete_tail_code =
            test_slice("?A");

        incomplete_machine.unknown_policy =
            PPRELATIONAL_STACK_PROOF_V1_UNKNOWN_PUSH_CLAIM;
        incomplete_normal.steps = &inputs.unknown_step;
        incomplete_normal.step_len = 1u;
        incomplete_compressed.header = NULL;
        incomplete_compressed.header_len = 0u;
        incomplete_compressed.code = &inputs.unknown_code;
        incomplete_compressed.code_len = 1u;
        expect(pprelational_stack_proof_v1_cache_init(
                   &incomplete_cache, &store, &incomplete_machine,
                   &incomplete_admission, error, sizeof(error)),
               error[0] ? error
                        : "incomplete-policy cache initialization failed");
        uncached = pprelational_stack_proof_v1_normal(
            &store, &incomplete_machine, &incomplete_normal,
            error, sizeof(error));
        cached = pprelational_stack_proof_v1_normal_cached(
            &store, &incomplete_machine, &incomplete_cache,
            &incomplete_normal, error, sizeof(error));
        expect(uncached == PPRELATIONAL_STACK_PROOF_V1_INCOMPLETE &&
                   cached == uncached,
               "normal unknown proof was promoted to verified");
        uncached = pprelational_stack_proof_v1_compressed(
            &store, &incomplete_machine, &incomplete_compressed,
            error, sizeof(error));
        cached = pprelational_stack_proof_v1_compressed_cached(
            &store, &incomplete_machine, &incomplete_cache,
            &incomplete_compressed, error, sizeof(error));
        expect(uncached == PPRELATIONAL_STACK_PROOF_V1_INCOMPLETE &&
                   cached == uncached,
               "compressed unknown proof was promoted to verified");
        incomplete_compressed.code = &open_incomplete_code;
        uncached = pprelational_stack_proof_v1_compressed(
            &store, &incomplete_machine, &incomplete_compressed,
            error, sizeof(error));
        cached = pprelational_stack_proof_v1_compressed_cached(
            &store, &incomplete_machine, &incomplete_cache,
            &incomplete_compressed, error, sizeof(error));
        expect(uncached == PPRELATIONAL_STACK_PROOF_V1_REJECTED &&
                   cached == uncached,
               "compressed unknown accepted inside an open index");
        incomplete_compressed.code = &incomplete_tail_code;
        uncached = pprelational_stack_proof_v1_compressed(
            &store, &incomplete_machine, &incomplete_compressed,
            error, sizeof(error));
        cached = pprelational_stack_proof_v1_compressed_cached(
            &store, &incomplete_machine, &incomplete_cache,
            &incomplete_compressed, error, sizeof(error));
        expect(uncached == PPRELATIONAL_STACK_PROOF_V1_REJECTED &&
                   cached == uncached,
               "compressed unknown hid an invalid remaining step");
        incomplete_compressed.header = inputs.compressed_header;
        incomplete_compressed.header_len = 2u;
        incomplete_compressed.code = &open_save_code;
        uncached = pprelational_stack_proof_v1_compressed(
            &store, &incomplete_machine, &incomplete_compressed,
            error, sizeof(error));
        cached = pprelational_stack_proof_v1_compressed_cached(
            &store, &incomplete_machine, &incomplete_cache,
            &incomplete_compressed, error, sizeof(error));
        expect(uncached == PPRELATIONAL_STACK_PROOF_V1_REJECTED &&
                   cached == uncached,
               "compressed save accepted an open index");
        pprelational_stack_proof_v1_cache_free(&incomplete_cache);
    }

    pprelational_stack_proof_v1_cache_free(&cache);
    test_store_free(&test_store);
    printf("(RelationalStackProofCacheV1Summary %u %u %u)\n",
           checks_run - checks_failed, checks_run, checks_failed);
    return checks_failed == 0u ? 0 : 1;
}

#include "gslt_rigid_coordinate_dispatch_v1.h"

#include <stdio.h>
#include <string.h>

static unsigned checks;
static unsigned failures;

#define CHECK(CONDITION)                                                       \
    do {                                                                       \
        checks++;                                                              \
        if (!(CONDITION)) {                                                    \
            failures++;                                                        \
            fprintf(stderr, "check failed at %s:%d: %s\n",                  \
                    __FILE__, __LINE__, #CONDITION);                           \
        }                                                                      \
    } while (0)

typedef struct {
    uint32_t arity;
    uint32_t occurrence_count;
    bool present[12];
    CettaGsltRigidKeyV1 keys[12];
} RigidFixtureV1;

static CettaGsltRigidKeyV1 symbol_key(uint32_t symbol) {
    CettaGsltRigidKeyV1 key = {
        .kind = CETTA_GSLT_RIGID_KEY_SYMBOL_V1,
        .symbol = symbol,
    };
    return key;
}

static CettaGsltRigidKeyV1 application_key(
    uint32_t symbol, uint32_t arity) {
    CettaGsltRigidKeyV1 key = {
        .kind = CETTA_GSLT_RIGID_KEY_APPLICATION_V1,
        .symbol = symbol,
        .arity = arity,
    };
    return key;
}

static bool fixture_key_at(
    void *context, uint32_t occurrence, uint32_t coordinate,
    CettaGsltRigidKeyV1 *key_out) {
    const RigidFixtureV1 *fixture = context;
    size_t offset;

    if (!fixture || !key_out || occurrence >= fixture->occurrence_count ||
        coordinate >= fixture->arity)
        return false;
    offset = (size_t)occurrence * fixture->arity + coordinate;
    if (offset >= sizeof(fixture->present) / sizeof(fixture->present[0]) ||
        !fixture->present[offset])
        return false;
    *key_out = fixture->keys[offset];
    return true;
}

static bool positions_equal(
    const uint32_t *actual, uint32_t actual_count,
    const uint32_t *expected, uint32_t expected_count) {
    return actual_count == expected_count &&
        (expected_count == 0u ||
         memcmp(actual, expected,
                (size_t)expected_count * sizeof(*expected)) == 0);
}

int main(void) {
    CettaGsltRigidCoordinateScratchV1 scratch;
    CettaGsltRigidCoordinateIndexV1 index;
    const uint32_t *positions = NULL;
    uint32_t position_count = 0u;
    size_t first_allocation_count;

    RigidFixtureV1 reflective = {
        .arity = 2u,
        .occurrence_count = 5u,
    };
    const uint32_t reflective_three[] = {2u, 3u, 4u};
    const uint32_t reflective_wildcard[] = {4u};
    for (uint32_t occurrence = 0u;
         occurrence < reflective.occurrence_count; occurrence++) {
        size_t base = (size_t)occurrence * reflective.arity;
        if (occurrence != 2u && occurrence != 4u) {
            reflective.present[base] = true;
            reflective.keys[base] = symbol_key(
                occurrence < 2u ? 10u : 20u);
        }
        if (occurrence != 4u) {
            static const uint32_t second[] = {1u, 2u, 3u, 3u};
            reflective.present[base + 1u] = true;
            reflective.keys[base + 1u] = symbol_key(second[occurrence]);
        }
    }

    cetta_gslt_rigid_coordinate_scratch_init_v1(&scratch);
    cetta_gslt_rigid_coordinate_index_init_v1(&index);
    CHECK(cetta_gslt_rigid_coordinate_index_build_v1(
        &index, &scratch, reflective.arity, reflective.occurrence_count,
        fixture_key_at, &reflective));
    CHECK(index.admitted && index.coordinate == 1u);
    CettaGsltRigidKeyV1 query = symbol_key(3u);
    CHECK(cetta_gslt_rigid_coordinate_index_positions_v1(
        &index, &query, &positions, &position_count));
    CHECK(positions_equal(
        positions, position_count,
        reflective_three,
        sizeof(reflective_three) / sizeof(reflective_three[0])));
    query = symbol_key(99u);
    CHECK(cetta_gslt_rigid_coordinate_index_positions_v1(
        &index, &query, &positions, &position_count));
    CHECK(positions_equal(
        positions, position_count,
        reflective_wildcard,
        sizeof(reflective_wildcard) / sizeof(reflective_wildcard[0])));
    first_allocation_count = scratch.allocation_count;
    CHECK(first_allocation_count == 1u);

    RigidFixtureV1 literal = {
        .arity = 1u,
        .occurrence_count = 3u,
    };
    const uint32_t literal_left[] = {0u, 1u};
    literal.present[0] = true;
    literal.keys[0] = application_key(100u, 2u);
    literal.present[2] = true;
    literal.keys[2] = application_key(200u, 1u);
    CHECK(cetta_gslt_rigid_coordinate_index_build_v1(
        &index, &scratch, literal.arity, literal.occurrence_count,
        fixture_key_at, &literal));
    CHECK(index.admitted && index.coordinate == 0u);
    query = application_key(100u, 2u);
    CHECK(cetta_gslt_rigid_coordinate_index_positions_v1(
        &index, &query, &positions, &position_count));
    CHECK(positions_equal(
        positions, position_count,
        literal_left,
        sizeof(literal_left) / sizeof(literal_left[0])));
    CHECK(scratch.allocation_count == first_allocation_count);

    RigidFixtureV1 open = {
        .arity = 3u,
        .occurrence_count = 4u,
    };
    CHECK(cetta_gslt_rigid_coordinate_index_build_v1(
        &index, &scratch, open.arity, open.occurrence_count,
        fixture_key_at, &open));
    CHECK(!index.admitted);
    query = symbol_key(1u);
    CHECK(!cetta_gslt_rigid_coordinate_index_positions_v1(
        &index, &query, &positions, &position_count));

    CHECK(!cetta_gslt_rigid_coordinate_index_build_v1(
        NULL, &scratch, 1u, 1u, fixture_key_at, &literal));
    CHECK(!cetta_gslt_rigid_coordinate_index_positions_v1(
        NULL, &query, &positions, &position_count));

    cetta_gslt_rigid_coordinate_index_free_v1(&index);
    cetta_gslt_rigid_coordinate_scratch_free_v1(&scratch);
    CHECK(scratch.slots == NULL && scratch.slot_capacity == 0u &&
          scratch.allocation_count == 0u);

    printf("(GsltRigidCoordinateDispatchV1Summary %u %u %u)\n",
           checks, checks - failures, failures);
    return failures == 0u ? 0 : 1;
}

#include "gslt_dense_bitset_v1.h"

#include <stdio.h>

static unsigned checks_run;
static unsigned checks_failed;

static void expect(bool condition, const char *message) {
    checks_run++;
    if (!condition) {
        checks_failed++;
        fprintf(stderr, "FAIL: %s\n", message);
    }
}

int main(void) {
    CettaGsltDenseBitsetV1 left = {0};
    CettaGsltDenseBitsetV1 right = {0};
    CettaGsltDenseBitsetV1 wider = {0};
    CettaGsltDenseBitsetV1 empty = {0};
    bool observed = false;

    expect(cetta_gslt_dense_bitset_word_count_v1(0u) == 0u &&
               cetta_gslt_dense_bitset_word_count_v1(64u) == 1u &&
               cetta_gslt_dense_bitset_word_count_v1(65u) == 2u,
           "word counts cover empty, exact, and partial words");
    expect(cetta_gslt_dense_bitset_init_v1(&left, 4u) &&
               cetta_gslt_dense_bitset_init_v1(&right, 4u),
           "two equal finite carriers initialize");
    expect(cetta_gslt_dense_bitset_set_v1(&left, 0u, &observed) &&
               observed,
           "first insertion changes the finite set");
    expect(cetta_gslt_dense_bitset_set_v1(&left, 0u, &observed) &&
               !observed,
           "repeated insertion preserves the finite set");
    expect(cetta_gslt_dense_bitset_set_v1(&left, 2u, &observed) &&
               observed,
           "a second declared member is inserted");
    expect(cetta_gslt_dense_bitset_test_v1(&left, 2u, &observed) &&
               observed,
           "membership reads the exact declared slot");
    expect(cetta_gslt_dense_bitset_test_v1(&left, 1u, &observed) &&
               !observed,
           "absent membership remains false");
    expect(cetta_gslt_dense_bitset_set_v1(&right, 1u, &observed) &&
               cetta_gslt_dense_bitset_set_v1(&right, 3u, &observed),
           "an independent finite support is constructed");
    expect(cetta_gslt_dense_bitset_intersection_empty_v1(
               &left, &right, &observed) && observed,
           "disjoint finite supports have empty intersection");
    expect(cetta_gslt_dense_bitset_union_changed_v1(
               &left, &right, &observed) && observed,
           "union reports newly added support");
    expect(cetta_gslt_dense_bitset_prefix_full_v1(
               &left, 4u, &observed) && observed,
           "union covers the complete four-slot inventory");
    expect(cetta_gslt_dense_bitset_union_changed_v1(
               &left, &right, &observed) && !observed,
           "idempotent union reports no change");
    expect(cetta_gslt_dense_bitset_intersection_empty_v1(
               &left, &right, &observed) && !observed,
           "shared support is detected after union");
    cetta_gslt_dense_bitset_clear_v1(&left);
    expect(cetta_gslt_dense_bitset_prefix_full_v1(
               &left, 0u, &observed) && observed,
           "the empty prefix is fully covered");
    expect(cetta_gslt_dense_bitset_test_v1(&left, 0u, &observed) &&
               !observed,
           "clear restores the extensional empty set");
    expect(!cetta_gslt_dense_bitset_set_v1(&left, 4u, &observed),
           "undeclared writes fail closed");

    expect(cetta_gslt_dense_bitset_init_v1(&wider, 130u),
           "a multiword finite carrier initializes");
    expect(cetta_gslt_dense_bitset_set_v1(&wider, 129u, &observed) &&
               cetta_gslt_dense_bitset_test_v1(
                   &wider, 129u, &observed) && observed,
           "the final partial-word slot round trips");
    expect(!cetta_gslt_dense_bitset_union_changed_v1(
               &left, &wider, &observed),
           "different finite inventories cannot be combined");
    expect(cetta_gslt_dense_bitset_init_v1(&empty, 0u) &&
               cetta_gslt_dense_bitset_prefix_full_v1(
                   &empty, 0u, &observed) && observed,
           "the zero-width carrier has exact empty semantics");

    cetta_gslt_dense_bitset_free_v1(&empty);
    cetta_gslt_dense_bitset_free_v1(&wider);
    cetta_gslt_dense_bitset_free_v1(&right);
    cetta_gslt_dense_bitset_free_v1(&left);
    printf("GsltDenseBitsetV1Summary checks=%u failures=%u\n",
           checks_run, checks_failed);
    return checks_failed == 0u ? 0 : 1;
}

#ifndef CETTA_GSLT_DENSE_BITSET_V1_H
#define CETTA_GSLT_DENSE_BITSET_V1_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint64_t *words;
    uint32_t bit_count;
    uint32_t word_len;
} CettaGsltDenseBitsetV1;

uint32_t cetta_gslt_dense_bitset_word_count_v1(uint32_t bit_count);

bool cetta_gslt_dense_bitset_init_v1(
    CettaGsltDenseBitsetV1 *bitset, uint32_t bit_count);

void cetta_gslt_dense_bitset_free_v1(CettaGsltDenseBitsetV1 *bitset);

void cetta_gslt_dense_bitset_clear_v1(CettaGsltDenseBitsetV1 *bitset);

bool cetta_gslt_dense_bitset_set_v1(
    CettaGsltDenseBitsetV1 *bitset, uint32_t bit, bool *changed_out);

bool cetta_gslt_dense_bitset_test_v1(
    const CettaGsltDenseBitsetV1 *bitset, uint32_t bit,
    bool *present_out);

bool cetta_gslt_dense_bitset_union_changed_v1(
    CettaGsltDenseBitsetV1 *target,
    const CettaGsltDenseBitsetV1 *source,
    bool *changed_out);

bool cetta_gslt_dense_bitset_intersection_empty_v1(
    const CettaGsltDenseBitsetV1 *left,
    const CettaGsltDenseBitsetV1 *right,
    bool *empty_out);

bool cetta_gslt_dense_bitset_prefix_full_v1(
    const CettaGsltDenseBitsetV1 *bitset, uint32_t prefix_len,
    bool *full_out);

#endif /* CETTA_GSLT_DENSE_BITSET_V1_H */

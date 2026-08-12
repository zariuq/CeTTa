#include "gslt_dense_bitset_v1.h"

#include <stdlib.h>
#include <string.h>

uint32_t cetta_gslt_dense_bitset_word_count_v1(uint32_t bit_count) {
    return bit_count / 64u + (uint32_t)(bit_count % 64u != 0u);
}

static bool dense_bitset_valid(const CettaGsltDenseBitsetV1 *bitset) {
    if (!bitset || bitset->word_len !=
                       cetta_gslt_dense_bitset_word_count_v1(
                           bitset->bit_count))
        return false;
    return bitset->word_len == 0u || bitset->words != NULL;
}

bool cetta_gslt_dense_bitset_init_v1(
    CettaGsltDenseBitsetV1 *bitset, uint32_t bit_count) {
    uint32_t word_len;
    uint64_t *words = NULL;

    if (!bitset || bitset->words || bitset->bit_count != 0u ||
        bitset->word_len != 0u)
        return false;
    word_len = cetta_gslt_dense_bitset_word_count_v1(bit_count);
    if (word_len != 0u) {
        words = calloc((size_t)word_len, sizeof(*words));
        if (!words)
            return false;
    }
    bitset->words = words;
    bitset->bit_count = bit_count;
    bitset->word_len = word_len;
    return true;
}

void cetta_gslt_dense_bitset_free_v1(CettaGsltDenseBitsetV1 *bitset) {
    if (!bitset)
        return;
    free(bitset->words);
    memset(bitset, 0, sizeof(*bitset));
}

void cetta_gslt_dense_bitset_clear_v1(CettaGsltDenseBitsetV1 *bitset) {
    if (!dense_bitset_valid(bitset) || bitset->word_len == 0u)
        return;
    memset(bitset->words, 0,
           (size_t)bitset->word_len * sizeof(*bitset->words));
}

bool cetta_gslt_dense_bitset_set_v1(
    CettaGsltDenseBitsetV1 *bitset, uint32_t bit, bool *changed_out) {
    uint32_t word;
    uint64_t mask;

    if (!dense_bitset_valid(bitset) || !changed_out ||
        bit >= bitset->bit_count)
        return false;
    word = bit / 64u;
    mask = UINT64_C(1) << (bit % 64u);
    *changed_out = (bitset->words[word] & mask) == 0u;
    bitset->words[word] |= mask;
    return true;
}

bool cetta_gslt_dense_bitset_test_v1(
    const CettaGsltDenseBitsetV1 *bitset, uint32_t bit,
    bool *present_out) {
    if (!dense_bitset_valid(bitset) || !present_out ||
        bit >= bitset->bit_count)
        return false;
    *present_out =
        (bitset->words[bit / 64u] &
         (UINT64_C(1) << (bit % 64u))) != 0u;
    return true;
}

bool cetta_gslt_dense_bitset_union_changed_v1(
    CettaGsltDenseBitsetV1 *target,
    const CettaGsltDenseBitsetV1 *source,
    bool *changed_out) {
    uint32_t index;
    bool changed = false;

    if (!dense_bitset_valid(target) || !dense_bitset_valid(source) ||
        !changed_out || target->bit_count != source->bit_count)
        return false;
    for (index = 0u; index < target->word_len; index++) {
        uint64_t combined = target->words[index] | source->words[index];
        if (combined != target->words[index]) {
            target->words[index] = combined;
            changed = true;
        }
    }
    *changed_out = changed;
    return true;
}

bool cetta_gslt_dense_bitset_intersection_empty_v1(
    const CettaGsltDenseBitsetV1 *left,
    const CettaGsltDenseBitsetV1 *right,
    bool *empty_out) {
    uint32_t index;

    if (!dense_bitset_valid(left) || !dense_bitset_valid(right) ||
        !empty_out || left->bit_count != right->bit_count)
        return false;
    for (index = 0u; index < left->word_len; index++) {
        if ((left->words[index] & right->words[index]) != 0u) {
            *empty_out = false;
            return true;
        }
    }
    *empty_out = true;
    return true;
}

bool cetta_gslt_dense_bitset_prefix_full_v1(
    const CettaGsltDenseBitsetV1 *bitset, uint32_t prefix_len,
    bool *full_out) {
    uint32_t complete_words;
    uint32_t index;
    uint32_t remainder;

    if (!dense_bitset_valid(bitset) || !full_out ||
        prefix_len > bitset->bit_count)
        return false;
    complete_words = prefix_len / 64u;
    for (index = 0u; index < complete_words; index++) {
        if (bitset->words[index] != UINT64_MAX) {
            *full_out = false;
            return true;
        }
    }
    remainder = prefix_len % 64u;
    if (remainder == 0u) {
        *full_out = true;
        return true;
    }
    *full_out =
        (bitset->words[complete_words] &
         ((UINT64_C(1) << remainder) - UINT64_C(1))) ==
        ((UINT64_C(1) << remainder) - UINT64_C(1));
    return true;
}

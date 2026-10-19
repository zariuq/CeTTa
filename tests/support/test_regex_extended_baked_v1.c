#include "native/regex_selection_v1.h"
#include <stdio.h>
#include <string.h>

extern const uint8_t cetta_regex_extended_baked_example_v1[];
extern const size_t cetta_regex_extended_baked_example_v1_len;

int main(void) {
    CettaReProgramV1 *first = NULL, *second = NULL;
    char error[256] = {0}; unsigned checks = 0;
    if (!cetta_re_v1_restore(cetta_regex_extended_baked_example_v1,
            cetta_regex_extended_baked_example_v1_len, &first, error, sizeof(error)) ||
        !cetta_re_v1_restore(cetta_regex_extended_baked_example_v1,
            cetta_regex_extended_baked_example_v1_len, &second, error, sizeof(error))) goto fail;
    cetta_re_v1_release(first); first = NULL;
    const char *inputs[] = {"aλλ!", "λλ!", "λa!", "λλ?"};
    for (unsigned i = 0; i < 4u; ++i) {
        CettaReResultV1 result = cetta_re_v1_find(second, (const uint8_t *)inputs[i], strlen(inputs[i]),
            0u, CETTA_RE_V1_SEARCH, 0u);
        size_t start = i ? 0u : 1u;
        bool ok = i >= 2u ? result.outcome == CETTA_RE_V1_NO_MATCH :
            result.outcome == CETTA_RE_V1_FOUND && result.group_len == 2u &&
            result.groups[0].start == start && result.groups[0].end == start + 4u &&
            result.groups[1].participated && result.groups[1].start == start &&
            result.groups[1].end == start + 2u;
        cetta_re_v1_result_free(&result);
        if (!ok) goto fail;
        ++checks;
    }
    if (cetta_re_v1_restore(cetta_regex_extended_baked_example_v1,
            cetta_regex_extended_baked_example_v1_len - 1u, &second, error, sizeof(error))) goto fail;
    ++checks;
    const uint8_t input[] = "λλ!";
    CettaReResultV1 stopped = cetta_re_v1_find(second, input, sizeof(input) - 1u,
        0u, CETTA_RE_V1_SEARCH, 1u);
    bool incomplete = stopped.outcome == CETTA_RE_V1_INCOMPLETE;
    cetta_re_v1_result_free(&stopped);
    if (!incomplete) goto fail;
    ++checks;
    cetta_re_v1_release(second);
    printf("(RegexExtendedBakedV1Summary %u 0)\n", checks); return 0;
fail:
    fprintf(stderr, "baked extended program: %s\n", error);
    cetta_re_v1_release(first); cetta_re_v1_release(second); return 1;
}

#include "native/regex_selection_v1.h"
#include <stdio.h>
#include <string.h>

extern const uint8_t cetta_regex_selection_baked_example_v1[];
extern const size_t cetta_regex_selection_baked_example_v1_len;

int main(void) {
    CettaReProgramV1 *first = NULL, *second = NULL;
    char error[256] = {0}; unsigned checks = 0;
    if (!cetta_re_v1_restore(cetta_regex_selection_baked_example_v1,
            cetta_regex_selection_baked_example_v1_len, &first, error, sizeof(error)) ||
        !cetta_re_v1_restore(cetta_regex_selection_baked_example_v1,
            cetta_regex_selection_baked_example_v1_len, &second, error, sizeof(error))) goto fail;
    cetta_re_v1_release(first); first = NULL;
    const char *inputs[] = {"a\xce\xbb" "b", "\xce\xbb\xce\xbb", "none"};
    for (unsigned i = 0; i < 3u; ++i) {
        CettaReResultV1 r = cetta_re_v1_find(second, (const uint8_t *)inputs[i], strlen(inputs[i]),
            0u, CETTA_RE_V1_SEARCH, 0u);
        bool ok = i == 2u ? r.outcome == CETTA_RE_V1_NO_MATCH :
            r.outcome == CETTA_RE_V1_FOUND && r.group_len == 2u &&
            r.groups[0].start == (i ? 0u : 1u) && r.groups[0].end == (i ? 4u : 3u) &&
            r.groups[1].participated && r.groups[1].start == r.groups[0].start &&
            r.groups[1].end == r.groups[0].end;
        cetta_re_v1_result_free(&r);
        if (!ok) goto fail;
        ++checks;
    }
    if (cetta_re_v1_restore(cetta_regex_selection_baked_example_v1,
            cetta_regex_selection_baked_example_v1_len - 1u, &first, error, sizeof(error))) goto fail;
    ++checks; cetta_re_v1_release(second);
    printf("(RegexSelectionBakedV1Summary %u 0)\n", checks); return 0;
fail:
    fprintf(stderr, "baked capture program: %s\n", error);
    cetta_re_v1_release(first); cetta_re_v1_release(second); return 1;
}

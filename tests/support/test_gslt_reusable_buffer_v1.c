#include "gslt_reusable_buffer_v1.h"

#include <stdio.h>

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

int main(void) {
    CettaGsltReusableBufferV1 buffer = {0};
    uint32_t values[] = {11u, 22u, 33u, 44u};
    uint32_t retained_capacity;

    CHECK(!cetta_gslt_reusable_buffer_init_v1(NULL, sizeof(uint32_t)));
    CHECK(!cetta_gslt_reusable_buffer_init_v1(&buffer, 0u));
    CHECK(cetta_gslt_reusable_buffer_init_v1(
        &buffer, sizeof(uint32_t)));
    CHECK(cetta_gslt_reusable_buffer_push_v1(
              &buffer, &values[0], 3u) ==
          CETTA_GSLT_REUSABLE_BUFFER_INVALID_V1);

    CHECK(cetta_gslt_reusable_buffer_acquire_v1(&buffer) ==
          CETTA_GSLT_REUSABLE_BUFFER_OK_V1);
    CHECK(cetta_gslt_reusable_buffer_acquire_v1(&buffer) ==
          CETTA_GSLT_REUSABLE_BUFFER_INVALID_V1);
    CHECK(cetta_gslt_reusable_buffer_push_v1(
              &buffer, &values[0], 3u) ==
          CETTA_GSLT_REUSABLE_BUFFER_OK_V1);
    CHECK(cetta_gslt_reusable_buffer_push_v1(
              &buffer, &values[1], 3u) ==
          CETTA_GSLT_REUSABLE_BUFFER_OK_V1);
    CHECK(cetta_gslt_reusable_buffer_push_v1(
              &buffer, &values[2], 3u) ==
          CETTA_GSLT_REUSABLE_BUFFER_OK_V1);
    CHECK(cetta_gslt_reusable_buffer_push_v1(
              &buffer, &values[3], 3u) ==
          CETTA_GSLT_REUSABLE_BUFFER_LIMIT_V1);
    CHECK(*(const uint32_t *)cetta_gslt_reusable_buffer_at_const_v1(
              &buffer, 1u) == 22u);
    CHECK(cetta_gslt_reusable_buffer_at_v1(&buffer, 3u) == NULL);
    CHECK(!cetta_gslt_reusable_buffer_truncate_v1(&buffer, 4u));
    CHECK(cetta_gslt_reusable_buffer_truncate_v1(&buffer, 2u));
    retained_capacity = buffer.capacity;
    CHECK(retained_capacity >= 3u);
    CHECK(cetta_gslt_reusable_buffer_release_v1(&buffer) ==
          CETTA_GSLT_REUSABLE_BUFFER_OK_V1);
    CHECK(buffer.len == 0u && buffer.capacity == retained_capacity);
    CHECK(cetta_gslt_reusable_buffer_at_const_v1(&buffer, 0u) == NULL);
    CHECK(cetta_gslt_reusable_buffer_release_v1(&buffer) ==
          CETTA_GSLT_REUSABLE_BUFFER_INVALID_V1);

    CHECK(cetta_gslt_reusable_buffer_acquire_v1(&buffer) ==
          CETTA_GSLT_REUSABLE_BUFFER_OK_V1);
    CHECK(buffer.len == 0u && buffer.capacity == retained_capacity);
    CHECK(buffer.acquire_len == 2u && buffer.reuse_len == 1u);
    CHECK(cetta_gslt_reusable_buffer_push_v1(
              &buffer, &values[3], 3u) ==
          CETTA_GSLT_REUSABLE_BUFFER_OK_V1);
    CHECK(*(uint32_t *)cetta_gslt_reusable_buffer_at_v1(
              &buffer, 0u) == 44u);
    CHECK(cetta_gslt_reusable_buffer_release_v1(&buffer) ==
          CETTA_GSLT_REUSABLE_BUFFER_OK_V1);

    cetta_gslt_reusable_buffer_free_v1(&buffer);
    CHECK(buffer.items == NULL && buffer.element_size == 0u);
    printf("(GsltReusableBufferV1Summary %u %u %u)\n",
           checks, checks - failures, failures);
    return failures == 0u ? 0 : 1;
}

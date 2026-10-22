/* Exercise the finite-domain bound primitives without publishing another API. */
#include "../../src/tensor_screening.c"
#include <assert.h>
#include <stdio.h>

int main(void) {
    uint64_t state = 786921;
    size_t checks = 0;
    double edges[] = {0., -0., DBL_TRUE_MIN, -DBL_TRUE_MIN,
        DBL_MIN, -DBL_MIN, DBL_MAX, -DBL_MAX, 1., -1.};
    for (size_t i = 0; i < (1u << 20); i++) {
        state ^= state << 13; state ^= state >> 7; state ^= state << 17;
        double value;
        memcpy(&value, &state, sizeof(value));
        if (i < sizeof(edges) / sizeof(*edges)) value = edges[i];
        if (!isfinite(value)) continue;
        double actual_up = up(value), actual_down = down(value);
        double reference_up = nextafter(value, INFINITY);
        double reference_down = nextafter(value, -INFINITY);
        assert(!memcmp(&actual_up, &reference_up, sizeof(double)));
        assert(!memcmp(&actual_down, &reference_down, sizeof(double)));
        checks += 2;
    }
    printf("AdjacentBinary64 %zu 0\n", checks);
    return 0;
}

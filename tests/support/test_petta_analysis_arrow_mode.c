#include "petta_analysis.h"

#include <assert.h>
#include <stdbool.h>

static void test_compatibility_table(void) {
    const PettaAnalysisArrowMode modes[] = {
        PETTA_ANALYSIS_ARROW_MODE_PLAIN,
        PETTA_ANALYSIS_ARROW_MODE_DETERMINISTIC,
        PETTA_ANALYSIS_ARROW_MODE_SEMIDETERMINISTIC,
        PETTA_ANALYSIS_ARROW_MODE_NONDETERMINISTIC,
        PETTA_ANALYSIS_ARROW_MODE_EFFECT,
    };
    const bool expected[5][5] = {
        {true, false, false, true, true},
        {true, true, true, true, true},
        {true, false, true, true, true},
        {true, false, false, true, true},
        {true, false, false, true, true},
    };

    for (unsigned actual = 0u; actual < 5u; actual++) {
        for (unsigned required = 0u; required < 5u; required++) {
            assert(petta_analysis_arrow_mode_fits(
                       modes[actual], modes[required]) ==
                   expected[actual][required]);
        }
        assert(!petta_analysis_arrow_mode_fits(
            PETTA_ANALYSIS_ARROW_MODE_INVALID, modes[actual]));
        assert(!petta_analysis_arrow_mode_fits(
            modes[actual], PETTA_ANALYSIS_ARROW_MODE_INVALID));
    }
}

int main(void) {
    test_compatibility_table();
    return 0;
}

#include "petta_analysis.h"

#include <assert.h>

static void test_verdict_tables(void) {
    const PettaAnalysisVerdict established = PETTA_ANALYSIS_ESTABLISHED;
    const PettaAnalysisVerdict refuted = PETTA_ANALYSIS_REFUTED;
    const PettaAnalysisVerdict undetermined = PETTA_ANALYSIS_UNDETERMINED;
    const PettaAnalysisVerdict incomplete = PETTA_ANALYSIS_INCOMPLETE;
    const PettaAnalysisVerdict verdicts[] = {
        established,
        refuted,
        undetermined,
        incomplete,
    };
    const PettaAnalysisVerdict expected_all[4][4] = {
        {established, refuted, undetermined, incomplete},
        {refuted, refuted, refuted, refuted},
        {undetermined, refuted, undetermined, incomplete},
        {incomplete, refuted, incomplete, incomplete},
    };
    const PettaAnalysisVerdict expected_any[4][4] = {
        {established, established, established, established},
        {established, refuted, undetermined, incomplete},
        {established, undetermined, undetermined, incomplete},
        {established, incomplete, incomplete, incomplete},
    };

    for (unsigned left = 0u; left < 4u; left++) {
        for (unsigned right = 0u; right < 4u; right++) {
            assert(petta_analysis_verdict_all(
                       verdicts[left], verdicts[right]) ==
                   expected_all[left][right]);
            assert(petta_analysis_verdict_any(
                       verdicts[left], verdicts[right]) ==
                   expected_any[left][right]);
            assert(petta_analysis_verdict_all(
                       verdicts[left], verdicts[right]) ==
                   petta_analysis_verdict_all(
                       verdicts[right], verdicts[left]));
            assert(petta_analysis_verdict_any(
                       verdicts[left], verdicts[right]) ==
                   petta_analysis_verdict_any(
                       verdicts[right], verdicts[left]));
        }
    }
}

static void test_verdict_laws(void) {
    const PettaAnalysisVerdict verdicts[] = {
        PETTA_ANALYSIS_ESTABLISHED,
        PETTA_ANALYSIS_REFUTED,
        PETTA_ANALYSIS_UNDETERMINED,
        PETTA_ANALYSIS_INCOMPLETE,
    };

    for (unsigned first = 0u; first < 4u; first++) {
        assert(petta_analysis_verdict_all(
                   PETTA_ANALYSIS_ESTABLISHED, verdicts[first]) ==
               verdicts[first]);
        assert(petta_analysis_verdict_any(
                   PETTA_ANALYSIS_REFUTED, verdicts[first]) ==
               verdicts[first]);
        assert(petta_analysis_verdict_all(
                   verdicts[first], verdicts[first]) == verdicts[first]);
        assert(petta_analysis_verdict_any(
                   verdicts[first], verdicts[first]) == verdicts[first]);
        for (unsigned second = 0u; second < 4u; second++) {
            for (unsigned third = 0u; third < 4u; third++) {
                assert(petta_analysis_verdict_all(
                           petta_analysis_verdict_all(
                               verdicts[first], verdicts[second]),
                           verdicts[third]) ==
                       petta_analysis_verdict_all(
                           verdicts[first],
                           petta_analysis_verdict_all(
                               verdicts[second], verdicts[third])));
                assert(petta_analysis_verdict_any(
                           petta_analysis_verdict_any(
                               verdicts[first], verdicts[second]),
                           verdicts[third]) ==
                       petta_analysis_verdict_any(
                           verdicts[first],
                           petta_analysis_verdict_any(
                               verdicts[second], verdicts[third])));
            }
        }
    }
}

int main(void) {
    test_verdict_tables();
    test_verdict_laws();
    return 0;
}

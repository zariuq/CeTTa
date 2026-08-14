#include "petta_analysis.h"

#include <assert.h>

static void test_join_table(void) {
    const PettaAnalysisCardinality deterministic =
        PETTA_ANALYSIS_CARDINALITY_DETERMINISTIC;
    const PettaAnalysisCardinality semideterministic =
        PETTA_ANALYSIS_CARDINALITY_SEMIDETERMINISTIC;
    const PettaAnalysisCardinality nondeterministic =
        PETTA_ANALYSIS_CARDINALITY_NONDETERMINISTIC;
    const PettaAnalysisCardinality undetermined =
        PETTA_ANALYSIS_CARDINALITY_UNDETERMINED;
    const PettaAnalysisCardinality expected[4][4] = {
        {deterministic, semideterministic, nondeterministic, undetermined},
        {semideterministic, semideterministic, nondeterministic, undetermined},
        {nondeterministic, nondeterministic, nondeterministic, nondeterministic},
        {undetermined, undetermined, nondeterministic, undetermined},
    };
    const PettaAnalysisCardinality grades[] = {
        deterministic,
        semideterministic,
        nondeterministic,
        undetermined,
    };

    for (unsigned left = 0u; left < 4u; left++) {
        for (unsigned right = 0u; right < 4u; right++) {
            assert(petta_analysis_cardinality_join(
                       grades[left], grades[right]) == expected[left][right]);
            assert(petta_analysis_cardinality_join(
                       grades[left], grades[right]) ==
                   petta_analysis_cardinality_join(
                       grades[right], grades[left]));
        }
    }
}

static void test_join_laws(void) {
    const PettaAnalysisCardinality grades[] = {
        PETTA_ANALYSIS_CARDINALITY_DETERMINISTIC,
        PETTA_ANALYSIS_CARDINALITY_SEMIDETERMINISTIC,
        PETTA_ANALYSIS_CARDINALITY_NONDETERMINISTIC,
        PETTA_ANALYSIS_CARDINALITY_UNDETERMINED,
    };

    for (unsigned first = 0u; first < 4u; first++) {
        assert(petta_analysis_cardinality_join(grades[first], grades[first]) ==
               grades[first]);
        assert(petta_analysis_cardinality_join(
                   PETTA_ANALYSIS_CARDINALITY_DETERMINISTIC,
                   grades[first]) == grades[first]);
        for (unsigned second = 0u; second < 4u; second++) {
            for (unsigned third = 0u; third < 4u; third++) {
                assert(petta_analysis_cardinality_join(
                           petta_analysis_cardinality_join(
                               grades[first], grades[second]),
                           grades[third]) ==
                       petta_analysis_cardinality_join(
                           grades[first],
                           petta_analysis_cardinality_join(
                               grades[second], grades[third])));
            }
        }
    }
}

int main(void) {
    test_join_table();
    test_join_laws();
    return 0;
}

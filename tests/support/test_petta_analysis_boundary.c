#include "petta_analysis.h"

#include <assert.h>
#include <stdbool.h>

static void test_requirement_join(void) {
    const PettaAnalysisBoundaryRequirement requirements[] = {
        PETTA_ANALYSIS_BOUNDARY_NONE,
        PETTA_ANALYSIS_BOUNDARY_NONVAR,
        PETTA_ANALYSIS_BOUNDARY_PROPER_LIST,
        PETTA_ANALYSIS_BOUNDARY_NONEMPTY_EXPRESSION,
    };
    const PettaAnalysisBoundaryRequirement expected[4][4] = {
        {requirements[0], requirements[1], requirements[2], requirements[3]},
        {requirements[1], requirements[1], requirements[2], requirements[3]},
        {requirements[2], requirements[2], requirements[2], requirements[3]},
        {requirements[3], requirements[3], requirements[3], requirements[3]},
    };

    for (unsigned left = 0u; left < 4u; left++) {
        for (unsigned right = 0u; right < 4u; right++) {
            assert(petta_analysis_boundary_requirement_join(
                       requirements[left], requirements[right]) ==
                   expected[left][right]);
            assert(petta_analysis_boundary_requirement_join(
                       requirements[left], requirements[right]) ==
                   petta_analysis_boundary_requirement_join(
                       requirements[right], requirements[left]));
        }
    }
    for (unsigned first = 0u; first < 4u; first++) {
        assert(petta_analysis_boundary_requirement_join(
                   PETTA_ANALYSIS_BOUNDARY_NONE, requirements[first]) ==
               requirements[first]);
        assert(petta_analysis_boundary_requirement_join(
                   requirements[first], requirements[first]) ==
               requirements[first]);
        for (unsigned second = 0u; second < 4u; second++) {
            for (unsigned third = 0u; third < 4u; third++) {
                assert(petta_analysis_boundary_requirement_join(
                           petta_analysis_boundary_requirement_join(
                               requirements[first], requirements[second]),
                           requirements[third]) ==
                       petta_analysis_boundary_requirement_join(
                           requirements[first],
                           petta_analysis_boundary_requirement_join(
                               requirements[second], requirements[third])));
            }
        }
    }
}

static void test_fact_masks(void) {
    const PettaAnalysisBoundaryRequirement requirements[] = {
        PETTA_ANALYSIS_BOUNDARY_NONE,
        PETTA_ANALYSIS_BOUNDARY_NONVAR,
        PETTA_ANALYSIS_BOUNDARY_PROPER_LIST,
        PETTA_ANALYSIS_BOUNDARY_NONEMPTY_EXPRESSION,
    };
    const bool expected_satisfaction[8][4] = {
        {true, false, false, false},
        {true, true, false, false},
        {true, true, true, false},
        {true, true, true, false},
        {true, false, true, true},
        {true, true, true, true},
        {true, true, true, true},
        {true, true, true, true},
    };

    for (unsigned facts = 0u; facts < 8u; facts++) {
        for (unsigned requirement = 0u; requirement < 4u; requirement++) {
            assert(petta_analysis_boundary_facts_satisfy(
                       (PettaAnalysisBoundaryFacts)facts,
                       requirements[requirement]) ==
                   expected_satisfaction[facts][requirement]);
        }
        assert(petta_analysis_boundary_facts_join(
                   (PettaAnalysisBoundaryFacts)facts,
                   PETTA_ANALYSIS_BOUNDARY_FACT_NONE) == facts);
        assert(petta_analysis_boundary_facts_join(
                   (PettaAnalysisBoundaryFacts)facts,
                   (PettaAnalysisBoundaryFacts)facts) == facts);
        for (unsigned other = 0u; other < 8u; other++) {
            assert(petta_analysis_boundary_facts_join(
                       (PettaAnalysisBoundaryFacts)facts,
                       (PettaAnalysisBoundaryFacts)other) ==
                   (PettaAnalysisBoundaryFacts)(facts | other));
            assert(petta_analysis_boundary_facts_join(
                       (PettaAnalysisBoundaryFacts)facts,
                       (PettaAnalysisBoundaryFacts)other) ==
                   petta_analysis_boundary_facts_join(
                       (PettaAnalysisBoundaryFacts)other,
                       (PettaAnalysisBoundaryFacts)facts));
            for (unsigned third = 0u; third < 8u; third++) {
                assert(petta_analysis_boundary_facts_join(
                           petta_analysis_boundary_facts_join(
                               (PettaAnalysisBoundaryFacts)facts,
                               (PettaAnalysisBoundaryFacts)other),
                           (PettaAnalysisBoundaryFacts)third) ==
                       petta_analysis_boundary_facts_join(
                           (PettaAnalysisBoundaryFacts)facts,
                           petta_analysis_boundary_facts_join(
                               (PettaAnalysisBoundaryFacts)other,
                               (PettaAnalysisBoundaryFacts)third)));
            }
        }
    }
}

int main(void) {
    test_requirement_join();
    test_fact_masks();
    return 0;
}

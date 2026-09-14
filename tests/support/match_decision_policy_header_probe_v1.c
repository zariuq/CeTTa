#include <stdint.h>
#include <stdio.h>

/* The generated header under test is supplied with the compiler's -include. */
int main(void) {
    printf("%u %u\n", (unsigned)cetta_md_policy_v1[3][3][1][1],
           (unsigned)CETTA_MATCH_DECISION_POLICY_EXACT_KEY_PLAN_V1);
    return 0;
}

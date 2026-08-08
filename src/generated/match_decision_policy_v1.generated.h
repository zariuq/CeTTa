/* Generated from MatchDecisionPolicyV1; do not edit. */
#ifndef CETTA_MATCH_DECISION_POLICY_V1_GENERATED_H
#define CETTA_MATCH_DECISION_POLICY_V1_GENERATED_H

#define CETTA_MATCH_DECISION_POLICY_GSLT_DIGEST "5529a59090fb1ee8570dd6962cbe8fc0b3d827886101e02d3eaba10e7233b7a7"
#define CETTA_MATCH_DECISION_POLICY_GSLT_IDENTITY "MatchDecisionPolicyV1-5529a59090fb1ee8570dd6962cbe8fc0b3d827886101e02d3eaba10e7233b7a7"
#define CETTA_MATCH_DECISION_POLICY_ID UINT64_C(0x5529a59090fb1ee8)

enum {
    CETTA_MD_POLICY_FALLBACK = 0,
    CETTA_MD_POLICY_KEEP = 1,
    CETTA_MD_POLICY_REFUTE = 2,
};

static const unsigned char cetta_md_policy_v1[4][4][2][2] = {
    /* unknown */ {
        /* wildcard */ {
            {0, 0}, /* arity different */
            {0, 0}, /* arity equal */
        },
        /* literal */ {
            {0, 0}, /* arity different */
            {0, 0}, /* arity equal */
        },
        /* expression-arity */ {
            {0, 0}, /* arity different */
            {0, 0}, /* arity equal */
        },
        /* expression-head */ {
            {0, 0}, /* arity different */
            {0, 0}, /* arity equal */
        },
    },
    /* absent */ {
        /* wildcard */ {
            {1, 1}, /* arity different */
            {1, 1}, /* arity equal */
        },
        /* literal */ {
            {2, 2}, /* arity different */
            {2, 2}, /* arity equal */
        },
        /* expression-arity */ {
            {2, 2}, /* arity different */
            {2, 2}, /* arity equal */
        },
        /* expression-head */ {
            {2, 2}, /* arity different */
            {2, 2}, /* arity equal */
        },
    },
    /* literal */ {
        /* wildcard */ {
            {1, 1}, /* arity different */
            {1, 1}, /* arity equal */
        },
        /* literal */ {
            {2, 1}, /* arity different */
            {2, 1}, /* arity equal */
        },
        /* expression-arity */ {
            {2, 2}, /* arity different */
            {2, 2}, /* arity equal */
        },
        /* expression-head */ {
            {2, 2}, /* arity different */
            {2, 2}, /* arity equal */
        },
    },
    /* expression */ {
        /* wildcard */ {
            {1, 1}, /* arity different */
            {1, 1}, /* arity equal */
        },
        /* literal */ {
            {2, 2}, /* arity different */
            {2, 2}, /* arity equal */
        },
        /* expression-arity */ {
            {2, 2}, /* arity different */
            {1, 1}, /* arity equal */
        },
        /* expression-head */ {
            {2, 2}, /* arity different */
            {2, 1}, /* arity equal */
        },
    },
};

#endif

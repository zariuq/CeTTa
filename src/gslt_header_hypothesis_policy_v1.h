#ifndef CETTA_GSLT_HEADER_HYPOTHESIS_POLICY_V1_H
#define CETTA_GSLT_HEADER_HYPOTHESIS_POLICY_V1_H

/* Generated proof plans select whether the explicit header suffix may name a
 * value already supplied by the implicit header prefix.  The runtime owns the
 * finite policy algebra and its loop; language presentations own the choice. */
typedef enum {
    CETTA_GSLT_HEADER_HYPOTHESIS_INVALID_V1 = 0,
    CETTA_GSLT_HEADER_HYPOTHESIS_NONMANDATORY_ONLY_V1,
    CETTA_GSLT_HEADER_HYPOTHESIS_ANY_ACTIVE_V1
} CettaGsltHeaderHypothesisPolicyV1;

#endif

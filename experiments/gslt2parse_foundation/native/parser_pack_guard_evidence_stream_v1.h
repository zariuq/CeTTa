#ifndef CETTA_GSLT2PARSE_PARSER_PACK_GUARD_EVIDENCE_STREAM_V1_H
#define CETTA_GSLT2PARSE_PARSER_PACK_GUARD_EVIDENCE_STREAM_V1_H

#include <stdbool.h>
#include <stddef.h>

#include "parser_pack_guard_plan_v1.h"

typedef struct {
    Arena arena;
    PPGuardPlanV1DerivationInput *derivations;
    size_t derivation_len;
    size_t derivation_cap;
    char source_digest[65];
    char pre_reflection_digest[65];
    char environment_digest[65];
    char answer_set_digest[65];
} PPGuardEvidenceWireV1;

void ppguard_evidence_wire_v1_init(PPGuardEvidenceWireV1 *wire);
void ppguard_evidence_wire_v1_free(PPGuardEvidenceWireV1 *wire);

bool ppguard_evidence_wire_v1_read(PPGuardEvidenceWireV1 *wire,
                                   const char *path,
                                   char *error_buf,
                                   size_t error_buf_size);

#endif

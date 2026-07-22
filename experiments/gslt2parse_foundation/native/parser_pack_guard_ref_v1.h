#ifndef CETTA_GSLT2PARSE_PARSER_PACK_GUARD_REF_V1_H
#define CETTA_GSLT2PARSE_PARSER_PACK_GUARD_REF_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "parser_pack_guard_plan_v1.h"
#include "regular_span_dfa_v1.h"

typedef struct {
    uint32_t parser_run_len;
    uint32_t match_len;
    uint64_t work_item_len;
    uint32_t source_pass_count;
    char relation_digest[65];
} PPGuardRefV1Receipt;

bool ppguard_ref_v1_witness_family_prepare(
    const PPABIV1Pack *pack,
    const PPGuardPlanV1 *plan,
    PPNativeV1PreparedStartFamily *family,
    char *error_buf,
    size_t error_buf_size);

/*
 * Realize regular positive-guard spans whose compiled bodies are references
 * to closed ParserPack states.  Richer guard bodies fail closed.  The source
 * is not decoded again: every witness parser consumes an exact normalized
 * slice of the supplied lattice.  Evidence retains the slice-local neutral
 * forest while the relation row carries its absolute scalar/byte embedding.
 */
bool ppguard_ref_v1_relation_build_gll(
    const PPABIV1Pack *pack,
    const PPGuardPlanV1 *plan,
    Atom *const *scan_tags,
    uint32_t scan_tag_len,
    const RSDFAV1ScanResult *scan,
    const CettaLpNativeUtf8Lattice *base_lattice,
    Atom *const *base_witness_values,
    uint32_t base_witness_len,
    uint32_t work_limit,
    uint32_t replay_depth,
    uint32_t result_limit,
    PPGuardRelationV1 *out,
    PPGuardRefV1Receipt *receipt,
    char *error_buf,
    size_t error_buf_size);

bool ppguard_ref_v1_relation_build_glr(
    const PPABIV1Pack *pack,
    const PPGuardPlanV1 *plan,
    Atom *const *scan_tags,
    uint32_t scan_tag_len,
    const RSDFAV1ScanResult *scan,
    const CettaLpNativeUtf8Lattice *base_lattice,
    Atom *const *base_witness_values,
    uint32_t base_witness_len,
    uint32_t work_limit,
    uint32_t replay_depth,
    uint32_t result_limit,
    PPGuardRelationV1 *out,
    PPGuardRefV1Receipt *receipt,
    char *error_buf,
    size_t error_buf_size);

bool ppguard_ref_v1_relation_build_gll_prepared(
    const PPABIV1Pack *pack,
    const PPGuardPlanV1 *plan,
    Atom *const *scan_tags,
    uint32_t scan_tag_len,
    const RSDFAV1ScanResult *scan,
    const CettaLpNativeUtf8Lattice *base_lattice,
    Atom *const *base_witness_values,
    uint32_t base_witness_len,
    const PPNativeV1PreparedStartFamily *family,
    uint32_t work_limit,
    uint32_t replay_depth,
    uint32_t result_limit,
    PPGuardRelationV1 *out,
    PPGuardRefV1Receipt *receipt,
    char *error_buf,
    size_t error_buf_size);

bool ppguard_ref_v1_relation_build_glr_prepared(
    const PPABIV1Pack *pack,
    const PPGuardPlanV1 *plan,
    Atom *const *scan_tags,
    uint32_t scan_tag_len,
    const RSDFAV1ScanResult *scan,
    const CettaLpNativeUtf8Lattice *base_lattice,
    Atom *const *base_witness_values,
    uint32_t base_witness_len,
    const PPNativeV1PreparedStartFamily *family,
    uint32_t work_limit,
    uint32_t replay_depth,
    uint32_t result_limit,
    PPGuardRelationV1 *out,
    PPGuardRefV1Receipt *receipt,
    char *error_buf,
    size_t error_buf_size);

#endif

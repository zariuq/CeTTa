#ifndef CETTA_GSLT2PARSE_PARSER_PACK_NATIVE_V1_H
#define CETTA_GSLT2PARSE_PARSER_PACK_NATIVE_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lib_parse_native_grammar.h"
#include "parser_pack_abi_v1.h"

typedef enum {
    PPNATIVE_V1_COMPLETED = 0,
    PPNATIVE_V1_UNSUPPORTED_OPEN_PACK = 1,
    PPNATIVE_V1_RECOGNIZER_LIMIT = 2,
    PPNATIVE_V1_REPLAY_DEPTH = 3,
    PPNATIVE_V1_RESULT_LIMIT = 4
} PPNativeV1Outcome;

typedef struct {
    CettaLpNativeUtf8Terminal *terminals;
    CettaLpNativeUnicodeRange **owned_ranges;
    uint32_t len;
} PPNativeV1TerminalPlan;

/*
 * A prepared ParserPack owns its terminal plan and both native parser tables.
 * The pack and start-state atoms are borrowed and must remain alive and
 * immutable while the prepared value is in use.  Every parse rechecks the
 * frozen provenance, pack digest, and canonical start-state identity.
 */
typedef struct {
    void *implementation;
} PPNativeV1Prepared;

typedef struct {
    const PPABIV1Pack *pack;
    const Atom *start_state;
    uint32_t start_state_id;
    const PPNativeV1TerminalPlan *terminal_plan;
    const CettaLpNativeGllPrepared *gll;
    const CettaLpNativeGlrPrepared *glr;
} PPNativeV1PreparedView;

/*
 * A prepared start-state family owns one GLL and one GLR recognizer for each
 * distinct start state over the same immutable native grammar.  The caller's
 * binding digest identifies the LanguageDef-derived plan that produced the
 * grammar.  Repreparing an identical grammar/binding/start set is a cache hit;
 * failed replacement leaves the previous family intact.
 */
typedef struct {
    void *implementation;
} PPNativeV1PreparedStartFamily;

typedef struct {
    uint32_t terminal_id;
    Atom *identity;
} PPNativeV1TerminalExtension;

typedef struct {
    uint32_t state_id;
    Atom *identity;
} PPNativeV1StateExtension;

typedef struct {
    uint32_t production_id;
    Atom *identity;
    uint32_t lhs_state_id;
    const PPABIV1Item *items;
    uint32_t item_len;
    Atom *action;
} PPNativeV1ProductionExtension;

typedef struct {
    const PPNativeV1StateExtension *states;
    uint32_t state_len;
    const PPNativeV1TerminalExtension *terminals;
    uint32_t terminal_len;
    const PPNativeV1ProductionExtension *productions;
    uint32_t production_len;
    Atom *const *witness_values;
    uint32_t witness_len;
} PPNativeV1ForestExtension;

typedef struct {
    Arena arena;
    PPNativeV1Outcome outcome;
    CettaLpNativeUtf8Forest forest;
    Atom *canonical_forest;
    bool canonical_forest_materialized;
    Atom **semantic_results;
    uint32_t semantic_result_len;
    bool accepted;
    char forest_digest[65];
    char detail[512];
} PPNativeV1Result;

void ppnative_v1_result_init(PPNativeV1Result *result);
void ppnative_v1_result_free(PPNativeV1Result *result);

void ppnative_v1_prepared_init(PPNativeV1Prepared *prepared);
void ppnative_v1_prepared_free(PPNativeV1Prepared *prepared);
bool ppnative_v1_prepare(PPNativeV1Prepared *prepared,
                         const PPABIV1Pack *pack,
                         const Atom *start_state,
                         char *error_buf,
                         size_t error_buf_size);
bool ppnative_v1_prepared_view(
    const PPNativeV1Prepared *prepared,
    PPNativeV1PreparedView *view,
    char *error_buf,
    size_t error_buf_size);
uint32_t ppnative_v1_prepared_table_build_count(
    const PPNativeV1Prepared *prepared);

void ppnative_v1_prepared_start_family_init(
    PPNativeV1PreparedStartFamily *family);
void ppnative_v1_prepared_start_family_free(
    PPNativeV1PreparedStartFamily *family);
bool ppnative_v1_prepared_start_family_prepare(
    PPNativeV1PreparedStartFamily *family,
    const CettaLpNativeGrammar *grammar,
    const char *binding_digest,
    const uint32_t *start_state_ids,
    uint32_t start_state_len,
    char *error_buf,
    size_t error_buf_size);
bool ppnative_v1_prepared_start_family_lookup(
    const PPNativeV1PreparedStartFamily *family,
    const char *binding_digest,
    uint32_t start_state_id,
    const CettaLpNativeGllPrepared **gll,
    const CettaLpNativeGlrPrepared **glr,
    char *error_buf,
    size_t error_buf_size);
uint32_t ppnative_v1_prepared_start_family_len(
    const PPNativeV1PreparedStartFamily *family);
uint32_t ppnative_v1_prepared_start_family_build_count(
    const PPNativeV1PreparedStartFamily *family);

void ppnative_v1_terminal_plan_free(PPNativeV1TerminalPlan *plan);
bool ppnative_v1_class_ranges_build(
    const PPABIV1Pack *pack,
    Atom *class_expression,
    CettaLpNativeUnicodeRange **out_ranges,
    uint32_t *out_range_len,
    char *error_buf,
    size_t error_buf_size);
bool ppnative_v1_terminal_plan_build(const PPABIV1Pack *pack,
                                     PPNativeV1TerminalPlan *plan,
                                     char *error_buf,
                                     size_t error_buf_size);
bool ppnative_v1_grammar_build(const PPABIV1Pack *pack,
                               CettaLpNativeGrammar *grammar);
bool ppnative_v1_grammar_extend(
    const PPABIV1Pack *pack,
    const PPNativeV1ForestExtension *extension,
    CettaLpNativeGrammar *grammar,
    char *error_buf,
    size_t error_buf_size);
int32_t ppnative_v1_state_find(const PPABIV1Pack *pack,
                               const Atom *state);
int32_t ppnative_v1_state_find_extended(
    const PPABIV1Pack *pack,
    const PPNativeV1ForestExtension *extension,
    const Atom *state);

bool ppnative_v1_start_is_closed_extended(
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const PPNativeV1ForestExtension *extension,
    char *error_buf,
    size_t error_buf_size);

/* Execute one validated ParserPack action tree without running a parser. */
bool ppnative_v1_apply_action_term(Arena *arena,
                                   Atom *action,
                                   Atom *const *values,
                                   uint32_t value_len,
                                   Atom **out,
                                   char *error_buf,
                                   size_t error_buf_size);

bool ppnative_v1_finish(PPNativeV1Result *result,
                        const PPABIV1Pack *pack,
                        const Atom *start_state,
                        uint32_t replay_depth,
                        uint32_t result_limit,
                        char *error_buf,
                        size_t error_buf_size);

bool ppnative_v1_finish_extended(
    PPNativeV1Result *result,
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const PPNativeV1ForestExtension *extension,
    uint32_t replay_depth,
    uint32_t result_limit,
    char *error_buf,
    size_t error_buf_size);

#endif

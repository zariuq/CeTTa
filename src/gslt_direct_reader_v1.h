#ifndef CETTA_GSLT_DIRECT_READER_V1_H
#define CETTA_GSLT_DIRECT_READER_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "term_universe.h"

#define GSLT_DIRECT_READER_V1_FRAGMENT \
    "deterministic-sexpr-direct-v1"
#define GSLT_DIRECT_PETTA_READER_V1_FRAGMENT \
    "petta-two-stage-direct-v1"
#define GSLT_DIRECT_PREFIX_READER_V1_FRAGMENT \
    "deterministic-prefix-sexpr-direct-v1"

typedef struct {
    const uint8_t *ascii;
    const uint32_t *points;
    uint32_t point_len;
    bool complement;
} GSLTDirectScalarClassV1;

typedef struct {
    bool allow_eof;
    const uint32_t *literals;
    uint32_t literal_len;
    const GSLTDirectScalarClassV1 *const *classes;
    uint32_t class_len;
} GSLTDirectBoundaryV1;

typedef struct {
    uint32_t source;
    uint32_t target;
} GSLTDirectCodepointMapV1;

typedef struct {
    const char *presentation_name;
    const char *fragment;
    const char *syntax_digest;
    const char *class_digest;
    const char *projection_digest;
    const char *compiler_digest;
    const char *composition_digest;
    const char *profile;

    const GSLTDirectScalarClassV1 *whitespace;
    uint32_t comment_marker;
    const GSLTDirectScalarClassV1 *comment_body;
    GSLTDirectBoundaryV1 comment_boundary;

    uint32_t expression_open;
    uint32_t expression_close;
    uint32_t string_open;
    uint32_t string_close;
    uint32_t variable_marker;

    const GSLTDirectScalarClassV1 *word_start;
    const GSLTDirectScalarClassV1 *word_tail;
    GSLTDirectBoundaryV1 word_boundary;
    const GSLTDirectScalarClassV1 *variable_tail;
    GSLTDirectBoundaryV1 variable_boundary;
    const GSLTDirectScalarClassV1 *string_plain;

    const uint32_t *simple_prefix;
    uint32_t simple_prefix_len;
    const GSLTDirectCodepointMapV1 *simple_map;
    uint32_t simple_map_len;

    const uint32_t *byte_prefix;
    uint32_t byte_prefix_len;
    const GSLTDirectScalarClassV1 *const *byte_digit_classes;
    uint32_t byte_digit_class_len;
    uint32_t byte_hex_min;
    uint32_t byte_hex_max;
    uint32_t byte_value_max;
    uint32_t byte_radix;

    const uint32_t *unicode_prefix;
    uint32_t unicode_prefix_len;
    uint32_t unicode_suffix;
    const int16_t *unicode_transitions;
    const uint8_t *unicode_accepting;
    uint32_t unicode_state_len;
    uint32_t unicode_hex_min;
    uint32_t unicode_hex_max;
    uint32_t unicode_value_max;
    uint32_t unicode_radix;

    uint32_t depth_limit;
} GSLTDirectReaderV1Plan;

typedef struct {
    uint32_t source_pass_count;
    uint32_t input_byte_len;
    uint32_t token_len;
    uint32_t shift_len;
    uint32_t reduce_len;
} GSLTDirectReaderV1Receipt;

typedef struct {
    void *context;
    bool (*begin_form)(void *context);
    AtomId (*word_bytes)(void *context, const uint8_t *bytes,
                         size_t byte_len);
    AtomId (*variable_bytes)(void *context, const uint8_t *bytes,
                             size_t byte_len);
    AtomId (*string_bytes)(void *context, const uint8_t *bytes,
                           size_t byte_len);
    AtomId (*expression)(void *context, const AtomId *children,
                         size_t child_len);
} GSLTDirectAtomProjectionV1;

/*
 * A deterministic prefix S-expression fragment.  Prefix and token rules are
 * generated from the authored syntax rather than embedded in the runtime.
 * This is deliberately separate from the HE plan: languages may share the
 * cursor/runtime implementation without inheriting one another's syntax or
 * projection policy.
 */
typedef enum {
    GSLT_DIRECT_TOKEN_PROJECTION_INVALID = 0,
    GSLT_DIRECT_TOKEN_PROJECTION_WORD,
    GSLT_DIRECT_TOKEN_PROJECTION_VARIABLE,
    GSLT_DIRECT_TOKEN_PROJECTION_ANONYMOUS_VARIABLE,
} GSLTDirectTokenProjectionV1;

typedef enum {
    GSLT_DIRECT_PREFIX_ROLE_INVALID = 0,
    GSLT_DIRECT_PREFIX_ROLE_QUOTE,
    GSLT_DIRECT_PREFIX_ROLE_UNQUOTE,
    GSLT_DIRECT_PREFIX_ROLE_NAMED_VARIABLE,
    GSLT_DIRECT_PREFIX_ROLE_RESOLVE_NAME,
} GSLTDirectPrefixRoleV1;

typedef struct {
    const uint32_t *literal;
    uint32_t literal_len;
    const GSLTDirectScalarClassV1 *first;
    const GSLTDirectScalarClassV1 *tail;
    GSLTDirectBoundaryV1 boundary;
    GSLTDirectTokenProjectionV1 projection;
    bool strip_literal;
} GSLTDirectTokenRuleV1;

typedef struct {
    const uint32_t *literal;
    uint32_t literal_len;
    const GSLTDirectScalarClassV1 *payload_start;
    GSLTDirectPrefixRoleV1 role;
    bool skip_before_payload;
} GSLTDirectPrefixRuleV1;

typedef struct {
    const char *presentation_name;
    const char *fragment;
    const char *syntax_digest;
    const char *class_digest;
    const char *projection_digest;
    const char *compiler_digest;
    const char *composition_digest;
    const char *profile;

    const GSLTDirectScalarClassV1 *whitespace;
    uint32_t comment_marker;
    const GSLTDirectScalarClassV1 *comment_body;
    GSLTDirectBoundaryV1 comment_boundary;

    uint32_t expression_open;
    uint32_t expression_close;
    uint32_t string_open;
    uint32_t string_close;
    const GSLTDirectScalarClassV1 *string_plain;
    const uint32_t *string_escape_prefix;
    uint32_t string_escape_prefix_len;
    const GSLTDirectScalarClassV1 *string_escape_source;
    const GSLTDirectCodepointMapV1 *string_escape_map;
    uint32_t string_escape_map_len;
    bool string_escape_identity_fallback;

    const GSLTDirectPrefixRuleV1 *prefix_rules;
    uint32_t prefix_rule_len;
    const GSLTDirectTokenRuleV1 *token_rules;
    uint32_t token_rule_len;
    uint32_t depth_limit;
} GSLTDirectPrefixReaderV1Plan;

typedef struct {
    uint32_t source_pass_count;
    uint32_t input_byte_len;
    uint32_t token_len;
    uint32_t prefix_len;
    uint32_t shift_len;
    uint32_t reduce_len;
} GSLTDirectPrefixReaderV1Receipt;

typedef struct {
    void *context;
    bool (*begin_form)(void *context);
    AtomId (*word_bytes)(void *context, const uint8_t *bytes,
                         size_t byte_len);
    AtomId (*variable_bytes)(void *context, const uint8_t *bytes,
                             size_t byte_len);
    AtomId (*anonymous_variable)(void *context);
    AtomId (*string_bytes)(void *context, const uint8_t *bytes,
                           size_t byte_len);
    AtomId (*expression)(void *context, const AtomId *children,
                         size_t child_len);
    AtomId (*prefix)(void *context, GSLTDirectPrefixRoleV1 role,
                     AtomId payload);
} GSLTDirectPrefixProjectionV1;

/*
 * PeTTa's source language is a composition of two independently authored
 * readers: an escape-oblivious document splitter followed by a per-form
 * S-expression reader.  This plan keeps the two class families distinct so
 * specialization cannot accidentally replace either LanguageDef with HE's
 * one-stage delimiter policy.
 */
typedef struct {
    const char *presentation_name;
    const char *fragment;
    const char *splitter_syntax_digest;
    const char *splitter_class_digest;
    const char *form_syntax_digest;
    const char *form_class_digest;
    const char *projection_digest;
    const char *compiler_digest;
    const char *composition_digest;
    const char *profile;

    const GSLTDirectScalarClassV1 *splitter_blank;
    const GSLTDirectScalarClassV1 *splitter_nonquote;
    const GSLTDirectScalarClassV1 *splitter_comment_body;
    const GSLTDirectScalarClassV1 *splitter_ordinary;

    const GSLTDirectScalarClassV1 *form_blank;
    const GSLTDirectScalarClassV1 *token_boundary;
    const GSLTDirectScalarClassV1 *token;
    const GSLTDirectScalarClassV1 *token_first;
    const GSLTDirectScalarClassV1 *string_plain;
    const GSLTDirectScalarClassV1 *quoted_token_plain;

    uint32_t comment_marker;
    uint32_t comment_line_end;
    uint32_t expression_open;
    uint32_t expression_close;
    uint32_t string_quote;
    uint32_t escape_marker;
    uint32_t variable_marker;
    uint32_t runnable_marker;

    const GSLTDirectCodepointMapV1 *string_escape_map;
    uint32_t string_escape_map_len;
    bool string_escape_identity_fallback;
    uint32_t depth_limit;
} GSLTDirectPeTTaReaderV1Plan;

typedef struct {
    uint32_t source_pass_count;
    uint32_t form_replay_count;
    uint32_t input_byte_len;
    uint32_t form_len;
    uint32_t token_len;
    uint32_t shift_len;
    uint32_t reduce_len;
} GSLTDirectPeTTaReaderV1Receipt;

typedef struct {
    void *context;
    bool (*begin_form)(void *context);
    AtomId (*token_bytes)(void *context, const uint8_t *bytes,
                          size_t byte_len);
    AtomId (*variable_bytes)(void *context, const uint8_t *bytes,
                             size_t byte_len, bool anonymous);
    AtomId (*string_bytes)(void *context, const uint8_t *bytes,
                           size_t byte_len);
    AtomId (*dollar_symbol)(void *context);
    AtomId (*expression)(void *context, const AtomId *children,
                         size_t child_len);
    bool (*finish_form)(void *context, bool runnable, AtomId value,
                        AtomId output[2], uint32_t *output_len);
} GSLTDirectPeTTaProjectionV1;

bool gslt_direct_reader_v1_plan_validate(
    const GSLTDirectReaderV1Plan *plan, char *error_buf,
    size_t error_buf_size);

int gslt_direct_reader_v1_parse_bytes_ids(
    const GSLTDirectReaderV1Plan *plan, const uint8_t *input,
    size_t input_len, const GSLTDirectAtomProjectionV1 *projection,
    AtomId **out_ids,
    GSLTDirectReaderV1Receipt *receipt, char *error_buf,
    size_t error_buf_size);

bool gslt_direct_prefix_reader_v1_plan_validate(
    const GSLTDirectPrefixReaderV1Plan *plan, char *error_buf,
    size_t error_buf_size);

int gslt_direct_prefix_reader_v1_parse_bytes_ids(
    const GSLTDirectPrefixReaderV1Plan *plan, const uint8_t *input,
    size_t input_len, const GSLTDirectPrefixProjectionV1 *projection,
    AtomId **out_ids, GSLTDirectPrefixReaderV1Receipt *receipt,
    char *error_buf, size_t error_buf_size);

bool gslt_direct_petta_reader_v1_plan_validate(
    const GSLTDirectPeTTaReaderV1Plan *plan, char *error_buf,
    size_t error_buf_size);

int gslt_direct_petta_reader_v1_parse_bytes_ids(
    const GSLTDirectPeTTaReaderV1Plan *plan, const uint8_t *input,
    size_t input_len, const GSLTDirectPeTTaProjectionV1 *projection,
    AtomId **out_ids, GSLTDirectPeTTaReaderV1Receipt *receipt,
    char *error_buf, size_t error_buf_size);

#endif /* CETTA_GSLT_DIRECT_READER_V1_H */

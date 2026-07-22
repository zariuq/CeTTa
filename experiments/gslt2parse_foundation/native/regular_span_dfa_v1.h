#ifndef CETTA_GSLT2PARSE_REGULAR_SPAN_DFA_V1_H
#define CETTA_GSLT2PARSE_REGULAR_SPAN_DFA_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lib_parse_native_grammar.h"

typedef enum {
    RSDFA_V1_NFA_EPSILON = 0,
    RSDFA_V1_NFA_RANGES = 1,
    RSDFA_V1_NFA_EOF = 2
} RSDFAV1NfaEdgeKind;

typedef struct {
    uint32_t from;
    uint32_t to;
    RSDFAV1NfaEdgeKind kind;
    const CettaLpNativeUnicodeRange *ranges;
    uint32_t range_len;
} RSDFAV1NfaEdge;

typedef struct {
    uint32_t state;
    uint32_t tag;
} RSDFAV1NfaAccept;

typedef struct {
    uint32_t state_len;
    const uint32_t *start_states;
    uint32_t start_len;
    const RSDFAV1NfaEdge *edges;
    uint32_t edge_len;
    const RSDFAV1NfaAccept *accepts;
    uint32_t accept_len;
    uint32_t tag_len;
} RSDFAV1Nfa;

typedef enum {
    RSDFA_V1_BUILD_COMPLETED = 0,
    RSDFA_V1_BUILD_STATE_LIMIT = 1,
    RSDFA_V1_BUILD_TRANSITION_LIMIT = 2,
    RSDFA_V1_BUILD_ANNOTATION_CONFLICT = 3
} RSDFAV1BuildOutcome;

typedef struct RSDFAV1PlanImpl RSDFAV1PlanImpl;

typedef struct {
    RSDFAV1PlanImpl *impl;
    uint32_t state_len;
    uint32_t transition_len;
    uint32_t tag_len;
} RSDFAV1Plan;

/*
 * Immutable, pointer-free-in-shape DFA object suitable for interpretation or
 * build-time emission as static C data.  The owning arrays are flattened so
 * generated and interpreted executors consume exactly the same automaton.
 */
typedef struct {
    uint32_t low;
    uint32_t high;
    uint32_t target;
} RSDFAV1ProgramTransition;

typedef struct {
    uint32_t transition_begin;
    uint32_t transition_len;
    uint32_t accept_begin;
    uint32_t accept_len;
    uint32_t eof_accept_begin;
    uint32_t eof_accept_len;
} RSDFAV1ProgramState;

typedef struct {
    RSDFAV1ProgramState *states;
    RSDFAV1ProgramTransition *transitions;
    uint32_t *accept_tags;
    uint32_t *eof_accept_tags;
    uint32_t state_len;
    uint32_t transition_len;
    uint32_t accept_tag_len;
    uint32_t eof_accept_tag_len;
    uint32_t start_state;
    uint32_t tag_len;
} RSDFAV1Program;

/*
 * Exact NFA-state subsets carried by a determinized plan.  Rows correspond
 * one-for-one with RSDFAV1Program states and words use little-endian bit
 * numbering within each uint64_t.  This witness is intentionally separate
 * from the pointer-free recognition program: consumers that do not need to
 * reconstruct an accepting NFA path pay no storage cost for it.
 */
typedef struct {
    uint64_t *words;
    uint32_t state_len;
    uint32_t word_len;
} RSDFAV1SubsetTable;

/*
 * Decidable language facts for one tag in a completed DFA plan.  Ranges are
 * the exact possible first Unicode scalars of nonempty matches, normalized
 * and pairwise disjoint.  accepts_eof_empty denotes an EOF-conditioned
 * zero-scalar match and is distinct from an ordinary empty match.
 *
 * prefix_free means no ordinary accepting match is a strict prefix of a
 * later match for the same tag.  one_scalar_or_eof is the deliberately narrow
 * positive-lookahead fragment: every match is exactly one scalar or EOF.
 */
typedef struct {
    CettaLpNativeUnicodeRange *first_ranges;
    uint32_t first_range_len;
    bool is_empty;
    bool accepts_empty;
    bool accepts_eof_empty;
    bool prefix_free;
    bool one_scalar_or_eof;
} RSDFAV1TagLanguage;

typedef enum {
    RSDFA_V1_INCLUSION_COMPLETED = 0,
    RSDFA_V1_INCLUSION_PAIR_LIMIT = 1
} RSDFAV1InclusionOutcome;

/*
 * Exact inclusion result for two tagged complete-input languages.  A scalar
 * sequence is accepted when its final state carries the selected ordinary or
 * EOF-closed tag.  On a completed negative decision, counterexample is the
 * shortest discovered Unicode-scalar witness in the left language but not
 * the right language.  The array is owned by the result.
 */
typedef struct {
    RSDFAV1InclusionOutcome outcome;
    bool included;
    uint32_t *counterexample;
    uint32_t counterexample_len;
    uint32_t explored_pair_len;
} RSDFAV1InclusionResult;

typedef enum {
    RSDFA_V1_FUNCTIONALITY_COMPLETED = 0,
    RSDFA_V1_FUNCTIONALITY_PAIR_LIMIT = 1,
    RSDFA_V1_FUNCTIONALITY_WORK_LIMIT = 2
} RSDFAV1FunctionalityOutcome;

/*
 * Exact functionality result for an annotated epsilon-NFA.  Every consuming
 * edge emits one annotation; zero-width edges emit none.  A completed
 * negative decision owns the shortest discovered input whose two accepting
 * runs emit different annotation sequences.  first_left_annotation and
 * first_right_annotation identify the first disagreement on that witness.
 */
typedef struct {
    RSDFAV1FunctionalityOutcome outcome;
    bool functional;
    uint32_t *counterexample;
    uint32_t counterexample_len;
    uint32_t first_left_annotation;
    uint32_t first_right_annotation;
    uint32_t explored_pair_len;
    uint64_t work_item_len;
} RSDFAV1FunctionalityResult;

typedef struct {
    uint32_t tag;
    uint32_t start_scalar;
    uint32_t end_scalar;
    uint32_t start_byte;
    uint32_t end_byte;
} RSDFAV1Token;

typedef enum {
    RSDFA_V1_SCAN_COMPLETED = 0,
    RSDFA_V1_SCAN_WORK_LIMIT = 1,
    RSDFA_V1_SCAN_TOKEN_LIMIT = 2
} RSDFAV1ScanOutcome;

typedef struct {
    RSDFAV1ScanOutcome outcome;
    RSDFAV1Token *tokens;
    uint32_t token_len;
    uint32_t *codepoints;
    uint32_t *byte_offsets;
    uint32_t tag_len;
    uint32_t input_byte_len;
    uint32_t input_scalar_len;
    uint32_t decoded_byte_len;
    uint32_t source_pass_count;
    uint64_t work_item_len;
} RSDFAV1ScanResult;

typedef enum {
    RSDFA_V1_CURSOR_SCAN_COMPLETED = 0,
    RSDFA_V1_CURSOR_SCAN_WORK_LIMIT = 1
} RSDFAV1CursorScanOutcome;

/*
 * Result of advancing one DFA run from one source cursor.  The caller owns a
 * token buffer of at least plan.tag_len entries.  On return its first
 * accept_len entries contain, in tag order, the farthest accepting span for
 * each tag recognized from start_scalar.  This is a schedule-neutral
 * recognition primitive: token priority, skipping, and cursor advancement
 * remain policy owned by the caller.
 */
typedef struct {
    RSDFAV1CursorScanOutcome outcome;
    uint32_t start_scalar;
    uint32_t stop_scalar;
    uint32_t start_byte;
    uint32_t stop_byte;
    uint32_t accept_len;
    bool reached_eof;
    uint64_t work_item_len;
} RSDFAV1CursorScanResult;

/*
 * Parser-facing ownership wrapper for a scan result's complete tag/span
 * relation.  A full-source lattice borrows codepoints and byte_offsets from
 * its scan result.  A normalized slice borrows only its codepoint span and
 * owns byte_offsets through owned_byte_offsets.  The relevant source must
 * therefore outlive this wrapper.  All remaining arrays are owned.  A
 * UINT32_MAX tag mapping excludes that tag explicitly.
 */
typedef struct {
    CettaLpNativeUtf8Lattice lattice;
    uint32_t *terminal_ids;
    CettaLpNativeUtf8LatticeEdge *edges;
    uint32_t *start_offsets;
    uint32_t *owned_byte_offsets;
} RSDFAV1ParserLattice;

void rsdfa_v1_plan_init(RSDFAV1Plan *plan);
void rsdfa_v1_plan_free(RSDFAV1Plan *plan);

void rsdfa_v1_program_init(RSDFAV1Program *program);
void rsdfa_v1_program_free(RSDFAV1Program *program);

void rsdfa_v1_subset_table_init(RSDFAV1SubsetTable *table);
void rsdfa_v1_subset_table_free(RSDFAV1SubsetTable *table);

bool rsdfa_v1_program_validate(
    const RSDFAV1Program *program,
    char *error_buf,
    size_t error_buf_size);

bool rsdfa_v1_plan_export_program(
    const RSDFAV1Plan *plan,
    RSDFAV1Program *out,
    char *error_buf,
    size_t error_buf_size);

/* Failed export is atomic: an existing table remains usable. */
bool rsdfa_v1_plan_export_subsets(
    const RSDFAV1Plan *plan,
    RSDFAV1SubsetTable *out,
    char *error_buf,
    size_t error_buf_size);

/*
 * Export the annotation carried by each transition in program-export order.
 * An ordinary DFA build annotates every transition with UINT32_MAX.  The
 * returned array is owned by the caller.
 */
bool rsdfa_v1_plan_export_transition_annotations(
    const RSDFAV1Plan *plan,
    uint32_t **out,
    uint32_t *out_len,
    char *error_buf,
    size_t error_buf_size);

void rsdfa_v1_inclusion_result_init(RSDFAV1InclusionResult *result);
void rsdfa_v1_inclusion_result_free(RSDFAV1InclusionResult *result);

/*
 * Decide L(left,left_tag) subseteq L(right,right_tag) by a breadth-first
 * product traversal.  pair_limit bounds distinct reachable product states;
 * reaching it returns a successful, explicit PAIR_LIMIT result rather than a
 * partial decision.  Invalid or corrupt programs fail structurally.
 */
bool rsdfa_v1_program_tag_inclusion(
    const RSDFAV1Program *left,
    uint32_t left_tag,
    const RSDFAV1Program *right,
    uint32_t right_tag,
    uint32_t pair_limit,
    RSDFAV1InclusionResult *out,
    char *error_buf,
    size_t error_buf_size);

void rsdfa_v1_functionality_result_init(
    RSDFAV1FunctionalityResult *result);
void rsdfa_v1_functionality_result_free(
    RSDFAV1FunctionalityResult *result);

/*
 * Decide whether every complete accepting run for tag emits the same
 * annotation sequence for a given input.  This is stronger than successful
 * action-local determinization: alternatives may disagree at one prefix and
 * still be disambiguated by future input.  EOF-annotated transducers are not
 * part of v1 and fail structurally.  Pair/work limits return explicit bounded
 * outcomes rather than partial conclusions.
 */
bool rsdfa_v1_annotated_nfa_functionality(
    const RSDFAV1Nfa *nfa,
    const uint32_t *edge_annotations,
    uint32_t no_annotation,
    uint32_t tag,
    uint32_t pair_limit,
    uint64_t work_limit,
    RSDFAV1FunctionalityResult *out,
    char *error_buf,
    size_t error_buf_size);

void rsdfa_v1_tag_language_init(RSDFAV1TagLanguage *language);
void rsdfa_v1_tag_language_free(RSDFAV1TagLanguage *language);

bool rsdfa_v1_plan_tag_language(
    const RSDFAV1Plan *plan,
    uint32_t tag,
    RSDFAV1TagLanguage *out,
    char *error_buf,
    size_t error_buf_size);

/*
 * Decide whether every accepting state for tag is unable to consume a scalar
 * in boundary_ranges and later accept tag again.  Combined DFAs may retain
 * unrelated tag transitions; those do not affect this tag-local property.
 * EOF is intrinsically stopping and therefore needs no range entry.
 */
bool rsdfa_v1_plan_tag_stops_before(
    const RSDFAV1Plan *plan,
    uint32_t tag,
    const CettaLpNativeUnicodeRange *boundary_ranges,
    uint32_t boundary_range_len,
    bool *out,
    char *error_buf,
    size_t error_buf_size);

bool rsdfa_v1_plan_build(const RSDFAV1Nfa *nfa,
                         uint32_t state_limit,
                         uint32_t transition_limit,
                         RSDFAV1Plan *out,
                         RSDFAV1BuildOutcome *outcome,
                         char *error_buf,
                         size_t error_buf_size);

/*
 * Determinize an annotated NFA.  edge_annotations is parallel to nfa.edges;
 * zero-width edges must carry no_annotation and every consuming edge must
 * carry another value.  If simultaneously active consuming edges disagree,
 * construction returns ANNOTATION_CONFLICT and no plan.  This is only a
 * failure of action-local determinization: future input may still make the
 * complete annotated relation functional.
 */
bool rsdfa_v1_plan_build_annotated(
    const RSDFAV1Nfa *nfa,
    const uint32_t *edge_annotations,
    uint32_t no_annotation,
    uint32_t state_limit,
    uint32_t transition_limit,
    RSDFAV1Plan *out,
    RSDFAV1BuildOutcome *outcome,
    char *error_buf,
    size_t error_buf_size);

void rsdfa_v1_scan_result_init(RSDFAV1ScanResult *result);
void rsdfa_v1_scan_result_free(RSDFAV1ScanResult *result);

bool rsdfa_v1_scan(const RSDFAV1Plan *plan,
                   const uint8_t *input,
                   size_t input_len,
                   uint64_t work_limit,
                   uint32_t token_limit,
                   RSDFAV1ScanResult *out,
                   char *error_buf,
                   size_t error_buf_size);

bool rsdfa_v1_scan_scalar_view(
    const RSDFAV1Plan *plan,
    const CettaLpNativeUtf8ScalarView *view,
    uint64_t work_limit,
    uint32_t token_limit,
    RSDFAV1ScanResult *out,
    char *error_buf,
    size_t error_buf_size);

bool rsdfa_v1_scan_cursor_longest(
    const RSDFAV1Plan *plan,
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t start_scalar,
    uint64_t work_limit,
    RSDFAV1Token *tokens,
    uint32_t token_capacity,
    RSDFAV1CursorScanResult *out,
    char *error_buf,
    size_t error_buf_size);

/*
 * The same operation for a scalar view successfully checked with
 * cetta_lp_native_utf8_scalar_view_validate since its last mutation.  This
 * avoids an O(document-size) validation walk at every token cursor.
 */
bool rsdfa_v1_scan_cursor_longest_prevalidated(
    const RSDFAV1Plan *plan,
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t start_scalar,
    uint64_t work_limit,
    RSDFAV1Token *tokens,
    uint32_t token_capacity,
    RSDFAV1CursorScanResult *out,
    char *error_buf,
    size_t error_buf_size);

bool rsdfa_v1_program_scan_cursor_longest_prevalidated(
    const RSDFAV1Program *program,
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t start_scalar,
    uint64_t work_limit,
    RSDFAV1Token *tokens,
    uint32_t token_capacity,
    RSDFAV1CursorScanResult *out,
    char *error_buf,
    size_t error_buf_size);

bool rsdfa_v1_program_scan_cursor_longest(
    const RSDFAV1Program *program,
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t start_scalar,
    uint64_t work_limit,
    RSDFAV1Token *tokens,
    uint32_t token_capacity,
    RSDFAV1CursorScanResult *out,
    char *error_buf,
    size_t error_buf_size);

void rsdfa_v1_parser_lattice_init(RSDFAV1ParserLattice *lattice);
void rsdfa_v1_parser_lattice_free(RSDFAV1ParserLattice *lattice);

bool rsdfa_v1_parser_lattice_build(
    const RSDFAV1ScanResult *source,
    const uint32_t *tag_terminal_ids,
    uint32_t tag_len,
    RSDFAV1ParserLattice *out,
    char *error_buf,
    size_t error_buf_size);

bool rsdfa_v1_composite_parser_lattice_build(
    const RSDFAV1ScanResult *source,
    const CettaLpNativeUtf8Terminal *scalar_terminals,
    uint32_t scalar_terminal_len,
    const uint32_t *tag_terminal_ids,
    uint32_t tag_len,
    RSDFAV1ParserLattice *out,
    char *error_buf,
    size_t error_buf_size);

/*
 * Normalize one exact scalar interval of an existing lattice to [0,n].
 * Only edges contained completely in the interval survive.  In particular,
 * EOF remains available only when it was present in the source relation;
 * slicing never invents an end-of-source match.  Witness IDs are preserved.
 */
bool rsdfa_v1_parser_lattice_slice(
    const CettaLpNativeUtf8Lattice *source,
    uint32_t scalar_left,
    uint32_t scalar_right,
    RSDFAV1ParserLattice *out,
    char *error_buf,
    size_t error_buf_size);

/*
 * The same normalization for an immutable source that the caller has
 * successfully checked with cetta_lp_native_utf8_lattice_validate since its
 * last mutation.  This avoids revalidating the complete source for every
 * member of a token loop; the normalized result is still validated before it
 * is returned.
 */
bool rsdfa_v1_parser_lattice_slice_prevalidated(
    const CettaLpNativeUtf8Lattice *source,
    uint32_t scalar_left,
    uint32_t scalar_right,
    RSDFAV1ParserLattice *out,
    char *error_buf,
    size_t error_buf_size);

#endif

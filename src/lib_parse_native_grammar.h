#ifndef CETTA_LIB_PARSE_NATIVE_GRAMMAR_H
#define CETTA_LIB_PARSE_NATIVE_GRAMMAR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "atom.h"

typedef enum {
    CETTA_LP_NATIVE_SYMBOL_TM = 0,
    CETTA_LP_NATIVE_SYMBOL_HL = 1,
    CETTA_LP_NATIVE_SYMBOL_HLB = 2
} CettaLpNativeSymbolKind;

typedef struct {
    CettaLpNativeSymbolKind kind;
    SymbolId name;
    SymbolId scope;
} CettaLpNativeSymbol;

typedef struct {
    SymbolId label;
    SymbolId lhs;
    CettaLpNativeSymbol *rhs;
    uint32_t rhs_len;
} CettaLpNativeProduction;

typedef struct {
    SymbolId atom;
    SymbolId nt;
} CettaLpNativeVarDecl;

typedef struct {
    SymbolId klass;
    SymbolId nt;
} CettaLpNativeLexDecl;

typedef enum {
    CETTA_LP_NATIVE_ENTRY_PRODUCTION = 0,
    CETTA_LP_NATIVE_ENTRY_VAR = 1,
    CETTA_LP_NATIVE_ENTRY_LEX = 2
} CettaLpNativeEntryKind;

typedef struct {
    CettaLpNativeEntryKind kind;
    uint32_t index;
} CettaLpNativeEntry;

typedef struct {
    CettaLpNativeProduction *productions;
    uint32_t production_len;
    CettaLpNativeVarDecl *vars;
    uint32_t var_len;
    CettaLpNativeLexDecl *lexes;
    uint32_t lex_len;
    CettaLpNativeEntry *entries;
    uint32_t entry_len;
    uint32_t binder_hole_count;
} CettaLpNativeGrammar;

typedef struct {
    uint32_t production_len;
    uint32_t var_len;
    uint32_t lex_len;
    uint32_t binder_hole_count;
} CettaLpNativeGrammarSummary;

typedef struct {
    uint32_t state_len;
    uint32_t shift_len;
    uint32_t goto_len;
    uint32_t reduce_len;
    uint32_t accept_len;
    uint32_t conflict_len;
} CettaLpNativeSlrSummary;

typedef struct {
    uint32_t low;
    uint32_t high;
} CettaLpNativeUnicodeRange;

typedef enum {
    CETTA_LP_NATIVE_UTF8_TERMINAL_ANY = 0,
    CETTA_LP_NATIVE_UTF8_TERMINAL_EOF = 1,
    CETTA_LP_NATIVE_UTF8_TERMINAL_SCALAR = 2,
    CETTA_LP_NATIVE_UTF8_TERMINAL_RANGES = 3
} CettaLpNativeUtf8TerminalKind;

typedef struct {
    uint32_t terminal_id;
    CettaLpNativeUtf8TerminalKind kind;
    uint32_t scalar;
    const CettaLpNativeUnicodeRange *ranges;
    uint32_t range_len;
} CettaLpNativeUtf8Terminal;

/*
 * A borrowed, already-decoded scalar sequence.  Byte offsets describe the
 * normalized UTF-8 encoding of the sequence and begin at zero.  A derived
 * view performs no source work, so decoded_byte_len and source_pass_count are
 * both zero.  A producer that performs the source decode sets them to the
 * complete byte length and one respectively.
 */
typedef struct {
    const uint32_t *codepoints;
    const uint32_t *byte_offsets;
    uint32_t scalar_len;
    uint32_t input_byte_len;
    uint32_t decoded_byte_len;
    uint32_t source_pass_count;
} CettaLpNativeUtf8ScalarView;

/*
 * An owned scalar view produced by exactly one complete UTF-8 source pass.
 * Decode replaces a previous value only after the new input is valid.
 */
typedef struct {
    uint32_t *codepoints;
    uint32_t *byte_offsets;
    CettaLpNativeUtf8ScalarView view;
} CettaLpNativeUtf8ScalarBuffer;

void cetta_lp_native_utf8_scalar_buffer_init(
    CettaLpNativeUtf8ScalarBuffer *buffer);

void cetta_lp_native_utf8_scalar_buffer_free(
    CettaLpNativeUtf8ScalarBuffer *buffer);

bool cetta_lp_native_utf8_scalar_buffer_decode(
    CettaLpNativeUtf8ScalarBuffer *buffer,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    char *error_buf,
    size_t error_buf_size);

bool cetta_lp_native_utf8_scalar_view_validate(
    const CettaLpNativeUtf8ScalarView *view,
    char *error_buf,
    size_t error_buf_size);

/*
 * A complete terminal-match relation over one already-decoded UTF-8 source.
 * Edges are grouped by scalar_left through start_offsets, then ordered by
 * terminal_id, scalar_right, value_kind, and value.  Edges never move
 * backwards.  Witness edges may be zero-width; EOF edges are zero-width only
 * at the final scalar position.  A witness value is an opaque identity owned
 * by the producer of the lattice; the native parser preserves it in the
 * neutral forest without interpreting it.
 */
typedef enum {
    CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_SCALAR = 0,
    CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_EOF = 1,
    CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_WITNESS = 2
} CettaLpNativeUtf8TerminalValueKind;

typedef struct {
    uint32_t terminal_id;
    uint32_t scalar_left;
    uint32_t scalar_right;
    uint32_t byte_left;
    uint32_t byte_right;
    CettaLpNativeUtf8TerminalValueKind value_kind;
    uint32_t value;
} CettaLpNativeUtf8LatticeEdge;

typedef struct {
    const uint32_t *terminal_ids;
    uint32_t terminal_len;
    const CettaLpNativeUtf8LatticeEdge *edges;
    uint32_t edge_len;
    const uint32_t *start_offsets;
    uint32_t start_offset_len;
    const uint32_t *codepoints;
    const uint32_t *byte_offsets;
    uint32_t scalar_len;
    uint32_t input_byte_len;
    uint32_t decoded_byte_len;
    uint32_t source_pass_count;
} CettaLpNativeUtf8Lattice;

/* Validate the complete public lattice contract without running a parser. */
bool cetta_lp_native_utf8_lattice_validate(
    const CettaLpNativeUtf8Lattice *lattice,
    char *error_buf,
    size_t error_buf_size);

typedef enum {
    CETTA_LP_NATIVE_UTF8_FOREST_TERM = 0,
    CETTA_LP_NATIVE_UTF8_FOREST_EPSILON = 1,
    CETTA_LP_NATIVE_UTF8_FOREST_SYMBOL = 2,
    CETTA_LP_NATIVE_UTF8_FOREST_INTERMEDIATE = 3
} CettaLpNativeUtf8ForestNodeKind;

typedef struct {
    CettaLpNativeUtf8ForestNodeKind kind;
    uint32_t symbol_id;
    uint32_t production_index;
    uint32_t dot;
    uint32_t scalar_left;
    uint32_t scalar_right;
    uint32_t byte_left;
    uint32_t byte_right;
    bool terminal_is_eof;
    uint32_t terminal_scalar;
    CettaLpNativeUtf8TerminalValueKind terminal_value_kind;
    uint32_t terminal_witness_id;
    uint32_t choice_begin;
    uint32_t choice_len;
} CettaLpNativeUtf8ForestNode;

#define CETTA_LP_NATIVE_UTF8_FOREST_NONE UINT32_MAX

typedef struct {
    uint32_t parent_node;
    uint32_t prefix_node;
    uint32_t child_node;
    uint32_t production_index;
    uint32_t scalar_pivot;
    uint32_t byte_pivot;
} CettaLpNativeUtf8ForestChoice;

typedef enum {
    CETTA_LP_NATIVE_UTF8_FOREST_COMPLETED = 0,
    CETTA_LP_NATIVE_UTF8_FOREST_RESOURCE_LIMIT = 1
} CettaLpNativeUtf8ForestOutcome;

/*
 * Backend-neutral result of feeding UTF-8 once through a native recognizer.
 * Dense IDs are disposable adapter indexes; callers retain semantic identity
 * tables.  byte_offsets has scalar_len + 1 entries and roots/nodes/choices are
 * the complete forest reachable from every start-state prefix root.
 */
typedef struct {
    CettaLpNativeUtf8ForestOutcome outcome;
    CettaLpNativeUtf8ForestNode *nodes;
    uint32_t node_len;
    CettaLpNativeUtf8ForestChoice *choices;
    uint32_t choice_len;
    uint32_t *roots;
    uint32_t root_len;
    uint32_t *expected_terminal_ids;
    uint32_t expected_terminal_len;
    uint32_t *codepoints;
    uint32_t *byte_offsets;
    uint32_t scalar_len;
    uint32_t input_byte_len;
    uint32_t farthest_scalar;
    uint32_t farthest_byte;
    uint32_t graph_node_len;
    uint32_t stack_node_len;
    uint32_t work_item_len;
    uint32_t decoded_byte_len;
    uint32_t source_pass_count;
} CettaLpNativeUtf8Forest;

/*
 * Prepared recognizers own theory-derived tables and may parse many inputs.
 * Their implementation pointers are opaque; initialize before prepare and
 * free after the last parse.  Copying a prepared value does not transfer
 * ownership.
 */
typedef struct {
    void *implementation;
} CettaLpNativeGllPrepared;

typedef struct {
    void *implementation;
} CettaLpNativeGlrPrepared;

/*
 * A prepared SLR owner contains its own production labels and parse tables.
 * The source grammar may be freed after a successful prepare.  Preparation is
 * atomic: on failure, an existing prepared owner remains usable.  Copies do
 * not transfer ownership, and callers must serialize prepare/free with parse.
 */
typedef struct {
    void *implementation;
} CettaLpNativeSlrPrepared;

/*
 * Immutable SLR object suitable for direct interpretation or build-time
 * emission.  Actions and gotos are dense: actions are indexed by
 * state*(terminal_len+1)+terminal, where terminal_len denotes parser EOF;
 * gotos are indexed by state*nonterminal_len+nonterminal.  UINT32_MAX is a
 * missing goto.  Terminal and nonterminal identities occupy separate symbol
 * namespaces, so the same SymbolId may occur in both arrays.  Synthetic
 * lex/var productions have authored=false.
 */
typedef enum {
    CETTA_LP_NATIVE_SLR_PROGRAM_ERROR = 0,
    CETTA_LP_NATIVE_SLR_PROGRAM_SHIFT = 1,
    CETTA_LP_NATIVE_SLR_PROGRAM_REDUCE = 2,
    CETTA_LP_NATIVE_SLR_PROGRAM_ACCEPT = 3
} CettaLpNativeSlrProgramActionKind;

typedef struct {
    CettaLpNativeSlrProgramActionKind kind;
    int32_t value;
} CettaLpNativeSlrProgramAction;

typedef struct {
    SymbolId label;
    SymbolId lhs;
    uint32_t rhs_begin;
    uint32_t rhs_len;
    bool authored;
} CettaLpNativeSlrProgramProduction;

typedef struct {
    SymbolId start_nonterminal;
    SymbolId *terminals;
    SymbolId *nonterminals;
    CettaLpNativeSlrProgramProduction *productions;
    CettaLpNativeSymbol *rhs;
    CettaLpNativeSlrProgramAction *actions;
    uint32_t *gotos;
    uint32_t terminal_len;
    uint32_t nonterminal_len;
    uint32_t production_len;
    uint32_t authored_production_len;
    uint32_t rhs_len;
    uint32_t action_len;
    uint32_t goto_len;
    CettaLpNativeSlrSummary summary;
} CettaLpNativeSlrProgram;

typedef enum {
    CETTA_LP_NATIVE_SLR_LATTICE_ACCEPTED = 0,
    CETTA_LP_NATIVE_SLR_LATTICE_NEEDS_GENERAL = 1,
    CETTA_LP_NATIVE_SLR_LATTICE_RESOURCE_LIMIT = 2
} CettaLpNativeSlrLatticeOutcome;

void cetta_lp_native_grammar_init(CettaLpNativeGrammar *grammar);
void cetta_lp_native_grammar_free(CettaLpNativeGrammar *grammar);

bool cetta_lp_native_grammar_load_forms(CettaLpNativeGrammar *out,
                                        Atom **forms,
                                        int form_count,
                                        const char *def_name,
                                        char *error_buf,
                                        size_t error_buf_size);

bool cetta_lp_native_grammar_load_file(CettaLpNativeGrammar *out,
                                       const char *filename,
                                       const char *def_name,
                                       char *error_buf,
                                       size_t error_buf_size);

bool cetta_lp_native_grammar_load_list(CettaLpNativeGrammar *out,
                                       Atom *grammar_list,
                                       char *error_buf,
                                       size_t error_buf_size);

void cetta_lp_native_grammar_summary(const CettaLpNativeGrammar *grammar,
                                     CettaLpNativeGrammarSummary *out);

void cetta_lp_native_utf8_forest_init(
    CettaLpNativeUtf8Forest *forest);

void cetta_lp_native_utf8_forest_free(
    CettaLpNativeUtf8Forest *forest);

void cetta_lp_native_gll_prepared_init(
    CettaLpNativeGllPrepared *prepared);

void cetta_lp_native_gll_prepared_free(
    CettaLpNativeGllPrepared *prepared);

bool cetta_lp_native_gll_prepare(
    CettaLpNativeGllPrepared *prepared,
    const CettaLpNativeGrammar *grammar,
    uint32_t start_nonterminal_id,
    char *error_buf,
    size_t error_buf_size);

void cetta_lp_native_glr_prepared_init(
    CettaLpNativeGlrPrepared *prepared);

void cetta_lp_native_glr_prepared_free(
    CettaLpNativeGlrPrepared *prepared);

bool cetta_lp_native_glr_prepare(
    CettaLpNativeGlrPrepared *prepared,
    const CettaLpNativeGrammar *grammar,
    uint32_t start_nonterminal_id,
    char *error_buf,
    size_t error_buf_size);

void cetta_lp_native_slr_prepared_init(
    CettaLpNativeSlrPrepared *prepared);

void cetta_lp_native_slr_prepared_free(
    CettaLpNativeSlrPrepared *prepared);

bool cetta_lp_native_slr_prepare(
    CettaLpNativeSlrPrepared *prepared,
    const CettaLpNativeGrammar *grammar,
    uint32_t start_nonterminal_id,
    char *error_buf,
    size_t error_buf_size);

/* Copy the construction summary retained by a prepared SLR table. */
bool cetta_lp_native_slr_prepared_summary(
    const CettaLpNativeSlrPrepared *prepared,
    CettaLpNativeSlrSummary *out,
    char *error_buf,
    size_t error_buf_size);

void cetta_lp_native_slr_program_init(
    CettaLpNativeSlrProgram *program);

void cetta_lp_native_slr_program_free(
    CettaLpNativeSlrProgram *program);

bool cetta_lp_native_slr_program_validate(
    const CettaLpNativeSlrProgram *program,
    char *error_buf,
    size_t error_buf_size);

bool cetta_lp_native_slr_prepared_export_program(
    const CettaLpNativeSlrPrepared *prepared,
    CettaLpNativeSlrProgram *out,
    char *error_buf,
    size_t error_buf_size);

bool cetta_lp_native_gll_parse_utf8_forest(
    const CettaLpNativeGrammar *grammar,
    uint32_t start_nonterminal_id,
    const CettaLpNativeUtf8Terminal *terminals,
    uint32_t terminal_len,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    uint32_t descriptor_limit,
    CettaLpNativeUtf8Forest *out,
    char *error_buf,
    size_t error_buf_size);

/*
 * Full-input GLL mode.  FIRST/FOLLOW prediction suppresses alternatives that
 * cannot participate in a complete start-state derivation.  The ordinary
 * entry point above remains the prefix-forest contract.
 */
bool cetta_lp_native_gll_parse_utf8_forest_complete(
    const CettaLpNativeGrammar *grammar,
    uint32_t start_nonterminal_id,
    const CettaLpNativeUtf8Terminal *terminals,
    uint32_t terminal_len,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    uint32_t descriptor_limit,
    CettaLpNativeUtf8Forest *out,
    char *error_buf,
    size_t error_buf_size);

bool cetta_lp_native_gll_prepared_parse_utf8_forest_complete(
    const CettaLpNativeGllPrepared *prepared,
    const CettaLpNativeUtf8Terminal *terminals,
    uint32_t terminal_len,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    uint32_t descriptor_limit,
    CettaLpNativeUtf8Forest *out,
    char *error_buf,
    size_t error_buf_size);

bool cetta_lp_native_gll_parse_utf8_scalar_view_forest(
    const CettaLpNativeGrammar *grammar,
    uint32_t start_nonterminal_id,
    const CettaLpNativeUtf8Terminal *terminals,
    uint32_t terminal_len,
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t descriptor_limit,
    CettaLpNativeUtf8Forest *out,
    char *error_buf,
    size_t error_buf_size);

bool cetta_lp_native_gll_parse_utf8_scalar_view_forest_complete(
    const CettaLpNativeGrammar *grammar,
    uint32_t start_nonterminal_id,
    const CettaLpNativeUtf8Terminal *terminals,
    uint32_t terminal_len,
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t descriptor_limit,
    CettaLpNativeUtf8Forest *out,
    char *error_buf,
    size_t error_buf_size);

bool cetta_lp_native_gll_prepared_parse_utf8_scalar_view_forest_complete(
    const CettaLpNativeGllPrepared *prepared,
    const CettaLpNativeUtf8Terminal *terminals,
    uint32_t terminal_len,
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t descriptor_limit,
    CettaLpNativeUtf8Forest *out,
    char *error_buf,
    size_t error_buf_size);

bool cetta_lp_native_glr_parse_utf8_forest(
    const CettaLpNativeGrammar *grammar,
    uint32_t start_nonterminal_id,
    const CettaLpNativeUtf8Terminal *terminals,
    uint32_t terminal_len,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    uint32_t work_limit,
    CettaLpNativeUtf8Forest *out,
    char *error_buf,
    size_t error_buf_size);

bool cetta_lp_native_glr_prepared_parse_utf8_forest(
    const CettaLpNativeGlrPrepared *prepared,
    const CettaLpNativeUtf8Terminal *terminals,
    uint32_t terminal_len,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    uint32_t work_limit,
    CettaLpNativeUtf8Forest *out,
    char *error_buf,
    size_t error_buf_size);

bool cetta_lp_native_glr_parse_utf8_scalar_view_forest(
    const CettaLpNativeGrammar *grammar,
    uint32_t start_nonterminal_id,
    const CettaLpNativeUtf8Terminal *terminals,
    uint32_t terminal_len,
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t work_limit,
    CettaLpNativeUtf8Forest *out,
    char *error_buf,
    size_t error_buf_size);

bool cetta_lp_native_glr_prepared_parse_utf8_scalar_view_forest(
    const CettaLpNativeGlrPrepared *prepared,
    const CettaLpNativeUtf8Terminal *terminals,
    uint32_t terminal_len,
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t work_limit,
    CettaLpNativeUtf8Forest *out,
    char *error_buf,
    size_t error_buf_size);

bool cetta_lp_native_gll_parse_utf8_lattice_forest(
    const CettaLpNativeGrammar *grammar,
    uint32_t start_nonterminal_id,
    const CettaLpNativeUtf8Lattice *lattice,
    uint32_t descriptor_limit,
    CettaLpNativeUtf8Forest *out,
    char *error_buf,
    size_t error_buf_size);

bool cetta_lp_native_gll_parse_utf8_lattice_forest_complete(
    const CettaLpNativeGrammar *grammar,
    uint32_t start_nonterminal_id,
    const CettaLpNativeUtf8Lattice *lattice,
    uint32_t descriptor_limit,
    CettaLpNativeUtf8Forest *out,
    char *error_buf,
    size_t error_buf_size);

bool cetta_lp_native_gll_parse_utf8_lattice_forest_from(
    const CettaLpNativeGrammar *grammar,
    uint32_t start_nonterminal_id,
    const CettaLpNativeUtf8Lattice *lattice,
    uint32_t start_scalar,
    uint32_t descriptor_limit,
    CettaLpNativeUtf8Forest *out,
    char *error_buf,
    size_t error_buf_size);

bool cetta_lp_native_gll_parse_utf8_lattice_forest_from_complete(
    const CettaLpNativeGrammar *grammar,
    uint32_t start_nonterminal_id,
    const CettaLpNativeUtf8Lattice *lattice,
    uint32_t start_scalar,
    uint32_t descriptor_limit,
    CettaLpNativeUtf8Forest *out,
    char *error_buf,
    size_t error_buf_size);

bool cetta_lp_native_gll_prepared_parse_utf8_lattice_forest_from_complete(
    const CettaLpNativeGllPrepared *prepared,
    const CettaLpNativeUtf8Lattice *lattice,
    uint32_t start_scalar,
    uint32_t descriptor_limit,
    CettaLpNativeUtf8Forest *out,
    char *error_buf,
    size_t error_buf_size);

bool cetta_lp_native_glr_parse_utf8_lattice_forest(
    const CettaLpNativeGrammar *grammar,
    uint32_t start_nonterminal_id,
    const CettaLpNativeUtf8Lattice *lattice,
    uint32_t work_limit,
    CettaLpNativeUtf8Forest *out,
    char *error_buf,
    size_t error_buf_size);

bool cetta_lp_native_glr_parse_utf8_lattice_forest_from(
    const CettaLpNativeGrammar *grammar,
    uint32_t start_nonterminal_id,
    const CettaLpNativeUtf8Lattice *lattice,
    uint32_t start_scalar,
    uint32_t work_limit,
    CettaLpNativeUtf8Forest *out,
    char *error_buf,
    size_t error_buf_size);

bool cetta_lp_native_glr_prepared_parse_utf8_lattice_forest_from(
    const CettaLpNativeGlrPrepared *prepared,
    const CettaLpNativeUtf8Lattice *lattice,
    uint32_t start_scalar,
    uint32_t work_limit,
    CettaLpNativeUtf8Forest *out,
    char *error_buf,
    size_t error_buf_size);

bool cetta_lp_native_slr_summary(const CettaLpNativeGrammar *grammar,
                                 SymbolId start_nt,
                                 CettaLpNativeSlrSummary *out,
                                 char *error_buf,
                                 size_t error_buf_size);

Atom *cetta_lp_native_slr_parse_shared(const CettaLpNativeGrammar *grammar,
                                       SymbolId start_nt,
                                       Atom *token_list,
                                       Arena *arena,
                                       char *error_buf,
                                       size_t error_buf_size);

Atom *cetta_lp_native_slr_prepared_parse_shared(
    const CettaLpNativeSlrPrepared *prepared,
    Atom *token_list,
    Arena *arena,
    char *error_buf,
    size_t error_buf_size);

/*
 * Sound deterministic fast path over a complete token lattice.  ACCEPTED is
 * returned only when one lookahead-equivalent LR action chain selects one
 * edge at every shift and consumes the complete source.  Lexical or parser
 * branching, and ordinary rejection, return NEEDS_GENERAL without choosing a
 * policy; callers then use GLL/GLR for complete semantics and diagnostics.
 */
bool cetta_lp_native_slr_prepared_parse_utf8_lattice_forest(
    const CettaLpNativeSlrPrepared *prepared,
    const CettaLpNativeUtf8Lattice *lattice,
    uint32_t start_scalar,
    uint32_t work_limit,
    CettaLpNativeSlrLatticeOutcome *outcome,
    CettaLpNativeUtf8Forest *out,
    char *error_buf,
    size_t error_buf_size);

Atom *cetta_lp_native_glr_parse_class(const CettaLpNativeGrammar *grammar,
                                      SymbolId start_nt,
                                      Atom *token_list,
                                      Arena *arena,
                                      char *error_buf,
                                      size_t error_buf_size);

Atom *cetta_lp_native_glr_parse_shared(const CettaLpNativeGrammar *grammar,
                                       SymbolId start_nt,
                                       Atom *token_list,
                                       Arena *arena,
                                       char *error_buf,
                                       size_t error_buf_size);

Atom *cetta_lp_native_glr_forest_summary(const CettaLpNativeGrammar *grammar,
                                         SymbolId start_nt,
                                         Atom *token_list,
                                         Arena *arena,
                                         char *error_buf,
                                         size_t error_buf_size);

Atom *cetta_lp_native_glr_forest_signature(const CettaLpNativeGrammar *grammar,
                                           SymbolId start_nt,
                                           Atom *token_list,
                                           Arena *arena,
                                           char *error_buf,
                                           size_t error_buf_size);

Atom *cetta_lp_native_glr_forest_signature_digest(const CettaLpNativeGrammar *grammar,
                                                  SymbolId start_nt,
                                                  Atom *token_list,
                                                  Arena *arena,
                                                  char *error_buf,
                                                  size_t error_buf_size);

Atom *cetta_lp_native_glr_forest_data(const CettaLpNativeGrammar *grammar,
                                      SymbolId start_nt,
                                      Atom *token_list,
                                      Arena *arena,
                                      char *error_buf,
                                      size_t error_buf_size);

Atom *cetta_lp_native_gll_parse_shared(const CettaLpNativeGrammar *grammar,
                                       SymbolId start_nt,
                                       Atom *token_list,
                                       Arena *arena,
                                       char *error_buf,
                                       size_t error_buf_size);

Atom *cetta_lp_native_gll_recognize(const CettaLpNativeGrammar *grammar,
                                    SymbolId start_nt,
                                    Atom *token_list,
                                    Arena *arena,
                                    char *error_buf,
                                    size_t error_buf_size);

Atom *cetta_lp_native_gll_recognize_token_file(
    const CettaLpNativeGrammar *grammar,
    SymbolId start_nt,
    const char *token_filename,
    Arena *arena,
    char *error_buf,
    size_t error_buf_size);

Atom *cetta_lp_native_gll_span_summary(const CettaLpNativeGrammar *grammar,
                                       SymbolId start_nt,
                                       Atom *token_list,
                                       Arena *arena,
                                       char *error_buf,
                                       size_t error_buf_size);

Atom *cetta_lp_native_gll_span_summary_token_file(
    const CettaLpNativeGrammar *grammar,
    SymbolId start_nt,
    const char *token_filename,
    Arena *arena,
    char *error_buf,
    size_t error_buf_size);

Atom *cetta_lp_native_gll_forest_summary(const CettaLpNativeGrammar *grammar,
                                         SymbolId start_nt,
                                         Atom *token_list,
                                         Arena *arena,
                                         char *error_buf,
                                         size_t error_buf_size);

Atom *cetta_lp_native_gll_forest_summary_token_file(
    const CettaLpNativeGrammar *grammar,
    SymbolId start_nt,
    const char *token_filename,
    Arena *arena,
    char *error_buf,
    size_t error_buf_size);

Atom *cetta_lp_native_gll_forest_signature(const CettaLpNativeGrammar *grammar,
                                           SymbolId start_nt,
                                           Atom *token_list,
                                           Arena *arena,
                                           char *error_buf,
                                           size_t error_buf_size);

Atom *cetta_lp_native_gll_forest_signature_digest(const CettaLpNativeGrammar *grammar,
                                                  SymbolId start_nt,
                                                  Atom *token_list,
                                                  Arena *arena,
                                                  char *error_buf,
                                                  size_t error_buf_size);

Atom *cetta_lp_native_gll_forest_signature_digest_token_file(
    const CettaLpNativeGrammar *grammar,
    SymbolId start_nt,
    const char *token_filename,
    Arena *arena,
    char *error_buf,
    size_t error_buf_size);

Atom *cetta_lp_native_gll_forest_data(const CettaLpNativeGrammar *grammar,
                                      SymbolId start_nt,
                                      Atom *token_list,
                                      Arena *arena,
                                      char *error_buf,
                                      size_t error_buf_size);

#endif /* CETTA_LIB_PARSE_NATIVE_GRAMMAR_H */

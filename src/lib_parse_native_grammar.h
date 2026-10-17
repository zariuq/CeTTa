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
 * normalized UTF-8 encoding of the sequence and begin at zero.  An ASCII
 * identity view instead borrows ascii_bytes: every byte is one scalar and
 * every scalar position is its byte offset.  A derived view performs no
 * source work, so decoded_byte_len and source_pass_count are both zero.  A
 * producer that performs the source decode or ASCII validation sets them to
 * the complete byte length and one respectively.
 */
typedef struct {
    const uint32_t *codepoints;
    const uint32_t *byte_offsets;
    uint32_t scalar_len;
    uint32_t input_byte_len;
    uint32_t decoded_byte_len;
    uint32_t source_pass_count;
    const uint8_t *ascii_bytes;
} CettaLpNativeUtf8ScalarView;

static inline bool cetta_lp_native_utf8_scalar_view_has_storage(
    const CettaLpNativeUtf8ScalarView *view) {
    return view &&
        (view->ascii_bytes ||
         (view->byte_offsets &&
          (view->scalar_len == 0u || view->codepoints)));
}

static inline uint32_t cetta_lp_native_utf8_scalar_view_scalar_at(
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t index) {
    return view->ascii_bytes
        ? (uint32_t)view->ascii_bytes[index]
        : view->codepoints[index];
}

static inline uint32_t cetta_lp_native_utf8_scalar_view_byte_offset(
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t index) {
    return view->ascii_bytes ? index : view->byte_offsets[index];
}

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

/*
 * Prepare one validated source view.  ASCII input is borrowed without
 * materializing scalar or offset arrays; other UTF-8 input is decoded into
 * owned arrays.  The borrowed view remains valid only while input_bytes does.
 */
bool cetta_lp_native_utf8_scalar_buffer_prepare(
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
    /* Optional layout-transparent lookahead.  When set, the lattice is a
     * chain of edges, `layout_terminal_id` marks layout edges, and every
     * layout occurrence in the grammar is nullable.  A completion or a
     * reduction is then decided by the next non-layout terminal, which the
     * follow sets already contain.  Recognizers drop only derivations that
     * cannot continue, so accepted forests are unchanged. */
    bool layout_lookahead;
    uint32_t layout_terminal_id;
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
 * Validate the complete public neutral-forest contract without rerunning a
 * recognizer.  This checks owned choice ranges, scalar/byte spans, binary
 * prefix/child factorization, terminal payloads, root reachability, and typed
 * resource-limit results.  Semantic symbol, terminal, and production tables
 * remain the caller's separate authority.
 */
bool cetta_lp_native_utf8_forest_validate(
    const CettaLpNativeUtf8Forest *forest,
    char *error_buf,
    size_t error_buf_size);

/*
 * Canonical qualification snapshot of the public neutral-forest arrays.
 * The wire is independent of C struct layout: magic "CNF1", followed by
 * little-endian uint32 fields and length-delimited node, choice, root,
 * expectation, scalar, and byte-offset tables.  Semantic identity tables are
 * deliberately separate because the dense IDs are adapter-local.
 *
 * Both operations validate the forest first.  `wire_write` is fail-atomic:
 * it writes nothing unless the supplied buffer is large enough for the whole
 * snapshot.  These functions are opt-in qualification facilities and are not
 * called by a parser or prepared hot path.
 */
bool cetta_lp_native_utf8_forest_wire_size(
    const CettaLpNativeUtf8Forest *forest,
    size_t *out_size,
    char *error_buf,
    size_t error_buf_size);

bool cetta_lp_native_utf8_forest_wire_write(
    const CettaLpNativeUtf8Forest *forest,
    uint8_t *out_bytes,
    size_t out_size,
    size_t *out_written,
    char *error_buf,
    size_t error_buf_size);

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
    /* An authored production a derivation avoids: where an input has several
     * derivations, only those reducing the fewest avoided productions count. */
    bool avoided;
} CettaLpNativeSlrProgramProduction;

typedef struct {
    uint32_t state;
    uint32_t token_idx;
    CettaLpNativeSlrProgramActionKind kind;
    int32_t value;
} CettaLpNativeGlrAction;

typedef struct {
    SymbolId start_nonterminal;
    SymbolId *terminals;
    SymbolId *nonterminals;
    CettaLpNativeSlrProgramProduction *productions;
    CettaLpNativeSymbol *rhs;
    CettaLpNativeSlrProgramAction *actions;
    uint32_t *gotos;
    CettaLpNativeGlrAction *glr_actions;
    uint32_t terminal_len;
    uint32_t nonterminal_len;
    uint32_t production_len;
    uint32_t authored_production_len;
    uint32_t rhs_len;
    uint32_t action_len;
    uint32_t goto_len;
    uint32_t glr_action_len;
    CettaLpNativeSlrSummary summary;
} CettaLpNativeSlrProgram;

/* Frozen GLL preparation over ParserPack-local dense identities. Production
 * order is occurrence identity; it is never reconstructed from labels.
 * The enclosing artifact owns the symbolic identity and projection maps.
 * FIRST/FOLLOW include the final synthetic end-of-input bit. */
typedef struct {
    CettaLpNativeProduction *productions;
    uint32_t production_len;
    uint32_t start_nonterminal;
    uint32_t *nonterminals;
    uint32_t nonterminal_len;
    uint32_t *terminals;
    uint32_t terminal_len;
    uint8_t *nullable;
    uint64_t *first;
    uint64_t *follow;
    uint32_t word_len;
} CettaLpNativeGllProgram;

void cetta_lp_native_gll_program_init(CettaLpNativeGllProgram *program);
void cetta_lp_native_gll_program_free(CettaLpNativeGllProgram *program);
bool cetta_lp_native_gll_prepared_export_program(
    const CettaLpNativeGllPrepared *prepared, CettaLpNativeGllProgram *out,
    char *error_buf, size_t error_buf_size);
/* Restore the stored decisions, without FIRST/FOLLOW or table construction.
 * Required ownership/array decoding checks do not replay grammar analysis. */
bool cetta_lp_native_gll_prepared_import_program(
    CettaLpNativeGllPrepared *out, const CettaLpNativeGllProgram *program,
    char *error_buf, size_t error_buf_size);
/* Explicit audit of stored references and least nullable/FIRST/FOLLOW sets.
 * This neither installs a new program nor constructs recognizer tables. */
bool cetta_lp_native_gll_prepared_check_prediction(
    const CettaLpNativeGllPrepared *, char *, size_t);

/* Predictive execution is a specialization of the same prepared grammar.
 * Table entries name production occurrences, not constructor labels. An
 * ineligible grammar has a retained conflict/cycle witness and uses the
 * general engine unless predictive execution was explicitly requested.
 * Token-chain input means one selected token per position, with positive
 * width except for the declared EOF token. It is not a general lattice. */
typedef enum {
    CETTA_LP_NATIVE_PREDICTIVE_ELIGIBLE = 0,
    CETTA_LP_NATIVE_PREDICTIVE_SELECT_CONFLICT,
    CETTA_LP_NATIVE_PREDICTIVE_ZERO_PROGRESS_CYCLE
} CettaLpNativePredictiveStatus;

typedef struct {
    CettaLpNativePredictiveStatus status;
    bool token_chain;
    uint32_t eof_terminal_id;
    uint32_t nonterminal_len;
    uint32_t terminal_len;
    uint32_t production_len;
    uint32_t start_index;
    uint32_t *decisions;
    uint32_t *rhs_offsets;
    uint32_t *rhs_indices;
    uint32_t rhs_len;
    uint32_t conflict_nonterminal;
    uint32_t conflict_productions[2];
    uint32_t conflict_terminals[2];
    uint32_t conflict_scalar;
    uint32_t *cycle_productions;
    uint32_t cycle_len;
} CettaLpNativePredictiveProgram;

void cetta_lp_native_predictive_program_free(CettaLpNativePredictiveProgram *program);
bool cetta_lp_native_predictive_build(
    const CettaLpNativeGllPrepared *prepared,
    const CettaLpNativeUtf8Terminal *terminals, uint32_t terminal_len,
    bool token_chain, uint32_t eof_terminal_id,
    CettaLpNativePredictiveProgram *out, char *error, size_t error_size);
Atom *cetta_lp_native_predictive_export_storage(
    Arena *arena, const CettaLpNativePredictiveProgram *program);
bool cetta_lp_native_predictive_import_storage(
    CettaLpNativePredictiveProgram *out, const Atom *value,
    const CettaLpNativeGllProgram *grammar, char *error, size_t error_size);
/* Explicit audit of stored occurrence decisions and refusal witnesses.
 * Ordinary restoration and execution do not call this operation. */
bool cetta_lp_native_predictive_check(
    const CettaLpNativeGllPrepared *, const CettaLpNativePredictiveProgram *,
    const CettaLpNativeUtf8Terminal *, uint32_t, char *, size_t);
/* Owned input bindings come from the enclosing immutable parser owner. The
 * interpreter creates only the derivation needed for value replay; complete
 * evidence remains an explicit operation on GLL/GLR. No parse-time analysis,
 * strategy fallback or grammar hashing occurs here. */
bool cetta_lp_native_predictive_parse_owned_utf8_forest(
    const CettaLpNativeGllPrepared *prepared,
    const CettaLpNativePredictiveProgram *program,
    const CettaLpNativeUtf8Terminal *terminals, uint32_t terminal_len,
    const uint8_t *bytes, size_t length, uint32_t work_limit,
    CettaLpNativeUtf8Forest *out, char *error, size_t error_size);
bool cetta_lp_native_predictive_parse_owned_lattice_forest(
    const CettaLpNativeGllPrepared *prepared,
    const CettaLpNativePredictiveProgram *program,
    const CettaLpNativeUtf8Lattice *lattice, uint32_t work_limit,
    CettaLpNativeUtf8Forest *out, char *error, size_t error_size);

/* GLR uses the existing action/goto carrier. Identities in this interface
 * are ParserPack-local indices, not process-local interned symbols. The
 * paired prediction program retains nullable/FIRST/FOLLOW decisions. */
bool cetta_lp_native_glr_prepared_export_program(
    const CettaLpNativeGlrPrepared *prepared, CettaLpNativeSlrProgram *out,
    char *error_buf, size_t error_buf_size);
bool cetta_lp_native_glr_prepared_import_program(
    CettaLpNativeGlrPrepared *out, const CettaLpNativeSlrProgram *program,
    const CettaLpNativeGllProgram *prediction,
    char *error_buf, size_t error_buf_size);

typedef enum {
    CETTA_LP_NATIVE_SLR_LATTICE_ACCEPTED = 0,
    CETTA_LP_NATIVE_SLR_LATTICE_NEEDS_GENERAL = 1,
    CETTA_LP_NATIVE_SLR_LATTICE_RESOURCE_LIMIT = 2
} CettaLpNativeSlrLatticeOutcome;

/* Execute only the input-local single-action corridor of an existing GLR
 * program. All terminal occurrences and all stored actions are considered;
 * ambiguity or a dead end requests the general parser, never branch choice.
 * NEEDS_GENERAL leaves the previous forest untouched. Owned bindings retain
 * the same immutable terminal/program lifetime as other owned parse calls. */
bool cetta_lp_native_glr_prepared_parse_owned_utf8_deterministic_forest(
    const CettaLpNativeGlrPrepared *prepared,
    const CettaLpNativeUtf8Terminal *terminals, uint32_t terminal_len,
    const uint8_t *bytes, size_t length, uint32_t work_limit,
    CettaLpNativeSlrLatticeOutcome *outcome, CettaLpNativeUtf8Forest *out,
    char *error, size_t error_size);

/* One step of a deterministic derivation in postfix order: a shifted
 * terminal occurrence (production UINT32_MAX), or a reduction of the
 * preceding rhs_len steps' results. Spans are half-open scalar offsets. */
typedef struct {
    uint32_t production;
    uint32_t terminal;
    uint32_t left;
    uint32_t right;
    uint32_t rhs_len;
    CettaLpNativeUtf8TerminalValueKind value_kind;
    uint32_t value;
} CettaLpNativeLrTraceStep;

typedef struct {
    CettaLpNativeLrTraceStep *steps;
    uint32_t len;
    uint32_t cap;
} CettaLpNativeLrTrace;

void cetta_lp_native_lr_trace_free(CettaLpNativeLrTrace *trace);

/* The same single-action corridor, recording its unique derivation as a
 * postfix trace instead of a packed forest. Repeats are recognized by the
 * LR state stack; NEEDS_GENERAL also covers a stack-hash collision. On
 * ACCEPTED, out carries the input (codepoints and byte offsets) and no
 * forest nodes. */
bool cetta_lp_native_glr_prepared_parse_owned_utf8_deterministic_trace(
    const CettaLpNativeGlrPrepared *prepared,
    const CettaLpNativeUtf8Terminal *terminals, uint32_t terminal_len,
    const uint8_t *bytes, size_t length, uint32_t work_limit,
    CettaLpNativeSlrLatticeOutcome *outcome, CettaLpNativeUtf8Forest *out,
    CettaLpNativeLrTrace *trace, char *error, size_t error_size);

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

/*
 * Same tables as slr_prepare, keeping yacc shift-prefer resolution when the
 * grammar is not SLR.  summary.conflict_len is the unresolved count; each
 * action cell still holds one action.  slr_prepare remains the strict gate.
 */
bool cetta_lp_native_slr_prepare_shift_prefer(
    CettaLpNativeSlrPrepared *prepared,
    const CettaLpNativeGrammar *grammar,
    uint32_t start_nonterminal_id,
    char *error_buf,
    size_t error_buf_size);

/* Keep every shift/reduce in a cell (GLR multimap). conflict_len is the
 * extra-action count. The dense unique table is still first-action per cell. */
bool cetta_lp_native_slr_prepare_glr(
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

Atom *cetta_lp_native_slr_program_parse_shared(
    const CettaLpNativeSlrProgram *program,
    Atom *token_list,
    Arena *arena,
    char *error_buf,
    size_t error_buf_size);

/* Interpret an exported SLR/GLR program with an explicit combined work and
 * retained-branch budget.  A zero limit selects the input-sized default.
 * Outcomes are NoParse, (Unique certificate), Ambiguous, or
 * (ResourceLimit consumed limit); resource exhaustion is never reported as a
 * syntax rejection. */
Atom *cetta_lp_native_slr_program_parse_shared_counted(
    const CettaLpNativeSlrProgram *program,
    Atom *token_list,
    uint64_t work_limit,
    uint64_t *work_used,
    Arena *arena,
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

/* Owned execution: the immutable preparation and terminal bindings must have
 * been constructed together, or decoded from a compatible trusted artifact.
 * Scalar views/lattices must come from the owning input decoder/scanner.
 * These entries do not audit borrowed declarations or input certificates.
 * The ordinary entries above remain validating boundaries for external data. */
bool cetta_lp_native_gll_prepared_parse_owned_utf8_forest_complete(
    const CettaLpNativeGllPrepared *, const CettaLpNativeUtf8Terminal *, uint32_t,
    const uint8_t *, size_t, uint32_t, CettaLpNativeUtf8Forest *, char *, size_t);
bool cetta_lp_native_gll_prepared_parse_owned_utf8_scalar_view_forest_complete(
    const CettaLpNativeGllPrepared *, const CettaLpNativeUtf8Terminal *, uint32_t,
    const CettaLpNativeUtf8ScalarView *, uint32_t, CettaLpNativeUtf8Forest *, char *, size_t);
bool cetta_lp_native_glr_prepared_parse_owned_utf8_forest(
    const CettaLpNativeGlrPrepared *, const CettaLpNativeUtf8Terminal *, uint32_t,
    const uint8_t *, size_t, uint32_t, CettaLpNativeUtf8Forest *, char *, size_t);
bool cetta_lp_native_gll_prepared_parse_owned_utf8_lattice_forest_from_complete(
    const CettaLpNativeGllPrepared *, const CettaLpNativeUtf8Lattice *, uint32_t,
    uint32_t, CettaLpNativeUtf8Forest *, char *, size_t);
bool cetta_lp_native_glr_prepared_parse_owned_utf8_lattice_forest_from(
    const CettaLpNativeGlrPrepared *, const CettaLpNativeUtf8Lattice *, uint32_t,
    uint32_t, CettaLpNativeUtf8Forest *, char *, size_t);

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

/* The packed GLL parser with an explicit bound on distinct descriptors.
 * Zero selects the unbounded legacy behavior.  Resource exhaustion is a
 * returned (ResourceLimit used limit) value, not a syntax rejection. */
Atom *cetta_lp_native_gll_parse_shared_counted(
    const CettaLpNativeGrammar *grammar,
    SymbolId start_nt,
    Atom *token_list,
    uint64_t descriptor_limit,
    uint64_t *descriptors_used,
    Arena *arena,
    char *error_buf,
    size_t error_buf_size);

/* The same parse, preferring derivations that reduce fewer of the productions
 * marked in avoided (indexed like grammar->productions).  Only derivations
 * with the fewest such reductions count, so the result is Ambiguous only when
 * two of those remain.  NULL avoids nothing. */
Atom *cetta_lp_native_gll_parse_avoiding_counted(
    const CettaLpNativeGrammar *grammar,
    SymbolId start_nt,
    Atom *token_list,
    uint64_t descriptor_limit,
    uint64_t *descriptors_used,
    const uint8_t *avoided,
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

#ifndef CETTA_TPTP_OFFICIAL_SNAPSHOT_V1_H
#define CETTA_TPTP_OFFICIAL_SNAPSHOT_V1_H

#include "experiments/gslt2parse_foundation/native/parser_pack_table_snapshot_v1.h"
#include "src/atom.h"
#include "native/grammar_canonical_term_v1.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CETTA_TPTP_OFFICIAL_SYNTAXBNF_DIGEST_V1 \
    "f47940c43c23ed5ed8633a3b74a2847648d5ab794669430c8f0c38f138e61df6"

#define CETTA_TPTP_OFFICIAL_SNAPSHOT_PATH_V1 \
    "runtime/tptp-official-reader/" CETTA_TPTP_OFFICIAL_SYNTAXBNF_DIGEST_V1 \
    "/tables.tpp1"
#define CETTA_TPTP_CORPUS_COMPATIBLE_SNAPSHOT_PATH_V1 \
    "runtime/tptp-official-reader/" CETTA_TPTP_OFFICIAL_SYNTAXBNF_DIGEST_V1 \
    "/corpus-compatible/tables.tpp1"

/* The ordinary table interpreter remains the fast path.  If it reports no
 * parse or exhausts its bounded GLR budget, the prepared reader retries the
 * same token slice with packed GLL.  A zero limit requests the finite default:
 * the greater of this floor and 96 descriptors per token plus 200,000.  An
 * explicit nonzero limit remains available to callers and qualification.
 * Only a constructed unique or ambiguous parse replaces the table result; a
 * negative or bounded fallback preserves that original outcome. */
#define CETTA_TPTP_GLL_DESCRIPTOR_LIMIT_V1 UINT64_C(4000000)
#define CETTA_TPTP_GLL_DESCRIPTORS_PER_TOKEN_V1 UINT64_C(96)
#define CETTA_TPTP_GLL_DESCRIPTOR_ALLOWANCE_V1 UINT64_C(200000)

/* Construct a reader snapshot from a prepared pack.  A corpus-compatible
 * pack is constructed against baseline_path, the strict snapshot of the same
 * SyntaxBNF: its grammar must contain the strict one, and the productions it
 * adds are marked avoided.  A strict pack has no baseline (NULL). */
bool cetta_tptp_snapshot_construct_from_pack_v1(
    const char *pack_path,
    const char *baseline_path,
    const char *out_path,
    char *error,
    size_t error_size);

bool cetta_tptp_snapshot_load_v1(
    PPTableSnapshotV1 *out,
    const char *path,
    const char *expected_digest,
    char *error,
    size_t error_size);

bool cetta_tptp_snapshot_load_bound_v1(
    PPTableSnapshotV1 *out,
    const char *path,
    const char *expected_syntax_digest,
    const char *expected_profile,
    const char *expected_artifact_digest,
    char *error,
    size_t error_size);

typedef struct {
    PPTableSnapshotV1 snapshot;
    CettaLpNativeGrammar fallback_grammar;
    /* Built once from the snapshot at load: the canonical term table every
     * read projects by and printing prints by, and the lexer's index of
     * ASCII transitions. */
    CettaGrammarCanonicalTableV1 *canonical;
    RSDFAV1AsciiTransitionIndex ascii;
} CettaTptpPreparedReaderV1;

/* Print a canonical term occupying a position of the named grammar symbol,
 * by the snapshot's canonical table (see cetta_tptp_snapshot_canonical_table_v1),
 * which a caller printing many terms builds once; NULL builds one for this
 * call.  The text is malloc'd and owned by the caller. */
bool cetta_tptp_snapshot_canonical_print_v1(const PPTableSnapshotV1 *snap,
                                            const CettaGrammarCanonicalTableV1 *table,
                                            const Atom *term,
                                            const char *sort, char **out, size_t *out_len,
                                            char *error, size_t error_size);

/* Print a term whose nodes may need the grammar's wrappers, such as the
 * parenthesized formula, to stand at their positions; see
 * cetta_grammar_canonical_print_wrapped_v1.  The table is as above. */
bool cetta_tptp_snapshot_canonical_print_wrapped_v1(
    const PPTableSnapshotV1 *snap, const CettaGrammarCanonicalTableV1 *table,
    const Atom *term, const char *sort,
    const SymbolId *wrappers, uint32_t wrapper_len, char **out, size_t *out_len,
    char *error, size_t error_size);

/* The canonical term table of the snapshot's grammar; the caller frees it. */
bool cetta_tptp_snapshot_canonical_table_v1(const PPTableSnapshotV1 *snap,
                                            CettaGrammarCanonicalTableV1 **out,
                                            char *error, size_t error_size);

/* The derived LanguageDef of the reader's canonical terms. */
bool cetta_tptp_snapshot_canonical_language_v1(const PPTableSnapshotV1 *snap,
                                               Arena *arena, Atom **out,
                                               char *error, size_t error_size);

void cetta_tptp_prepared_reader_init_v1(CettaTptpPreparedReaderV1 *reader);
void cetta_tptp_prepared_reader_free_v1(CettaTptpPreparedReaderV1 *reader);

bool cetta_tptp_prepared_reader_load_v1(
    CettaTptpPreparedReaderV1 *reader,
    const char *path,
    const char *expected_digest,
    char *error,
    size_t error_size);

bool cetta_tptp_prepared_reader_load_bound_v1(
    CettaTptpPreparedReaderV1 *reader,
    const char *path,
    const char *expected_syntax_digest,
    const char *expected_profile,
    const char *expected_artifact_digest,
    char *error,
    size_t error_size);

typedef struct {
    uint32_t start_scalar;
    uint32_t len;
    uint16_t tag;
    size_t start_byte;
    size_t end_byte;
} CettaTptpLexTokenV1;

static inline uint32_t cetta_tptp_lex_end(const CettaTptpLexTokenV1 *tok) {
    return tok->start_scalar + (uint32_t)tok->len;
}

/* The byte view is the exact UTF-8 slice accepted for this token by the
 * prepared lexer. It borrows `text`; no decoding or interpretation occurs. */
bool cetta_tptp_lex_token_bytes_v1(
    const CettaTptpLexTokenV1 *token,
    const char *text,
    size_t text_len,
    const char **bytes,
    size_t *byte_len);

/* Longest-match tokenize with skip tags omitted. ASCII TPTP/TSTP text. */
bool cetta_tptp_snapshot_lex_text_v1(
    const PPTableSnapshotV1 *snap,
    const char *text,
    size_t text_len,
    CettaTptpLexTokenV1 *out,
    uint32_t cap,
    uint32_t *out_len,
    char *error,
    size_t error_size);

typedef struct {
    double lex_s;
    double parse_s;
    double project_s;
    double combine_s;
    uint64_t gll_descriptor_count;
    uint32_t token_count;
    uint32_t input_count;
} CettaTptpReadCostV1;

typedef enum {
    CETTA_TPTP_READ_ERROR_V1 = 0,
    CETTA_TPTP_READ_OK_V1 = 1,
    CETTA_TPTP_READ_LEX_REJECT_V1 = 2,
    CETTA_TPTP_READ_NO_PARSE_V1 = 3,
    CETTA_TPTP_READ_AMBIGUOUS_V1 = 4,
    CETTA_TPTP_READ_RESOURCE_LIMIT_V1 = 5
} CettaTptpReadStatusV1;

typedef struct {
    CettaTptpReadStatusV1 status;
    uint32_t byte_offset;
    uint64_t work;
    uint64_t limit;
} CettaTptpReadOutcomeV1;

/* Project one parsed TPTP_input CST into an ordinary expression whose
 * elements are the source-level records contributed by that input.  The CST
 * and scratch arena remain valid only for the duration of the callback; the
 * returned expression must be owned by result_arena. */
typedef bool (*CettaTptpCstProjectionV1)(
    Atom *trees,
    const char *source,
    size_t source_len,
    bool source_ascii,
    Arena *scratch_arena,
    Arena *result_arena,
    Atom **out_records,
    void *context,
    char *error,
    size_t error_size);

/* The observation boundary used by the prepared reader, also available to
 * build-time derivation qualification. The caller supplies a ParserPack
 * NodeC tree and its token/source spans, not a second TPTP grammar. */
Atom *cetta_tptp_observe_derivation_v1(
    const PPTableSnapshotV1 *snap,
    Arena *arena, Atom *tree, const CettaTptpLexTokenV1 *tokens,
    uint32_t token_count, const char *source, size_t source_length);

bool cetta_tptp_snapshot_read_text_v1(
    const PPTableSnapshotV1 *snap,
    const char *text,
    size_t text_len,
    Arena *arena,
    Atom **out_records,
    char *error,
    size_t error_size);

bool cetta_tptp_snapshot_read_text_outcome_v1(
    const PPTableSnapshotV1 *snap,
    const char *text,
    size_t text_len,
    Arena *arena,
    Atom **out_records,
    CettaTptpReadOutcomeV1 *outcome,
    char *error,
    size_t error_size);

bool cetta_tptp_snapshot_read_file_outcome_v1(
    const PPTableSnapshotV1 *snap,
    const char *path,
    Arena *arena,
    Atom **out_records,
    CettaTptpReadOutcomeV1 *outcome,
    char *error,
    size_t error_size);

/* As cetta_tptp_prepared_reader_read_text_outcome_v1, and *out_starts
 * receives, in a malloc'ed array with one entry per input, the first byte of
 * each input's first token. */
bool cetta_tptp_prepared_reader_read_text_spans_v1(
    const CettaTptpPreparedReaderV1 *reader,
    const char *text,
    size_t text_len,
    Arena *arena,
    Atom **out_records,
    size_t **out_starts,
    CettaTptpReadOutcomeV1 *outcome,
    char *error,
    size_t error_size);

bool cetta_tptp_prepared_reader_read_text_outcome_v1(
    const CettaTptpPreparedReaderV1 *reader,
    const char *text,
    size_t text_len,
    Arena *arena,
    Atom **out_records,
    CettaTptpReadOutcomeV1 *outcome,
    char *error,
    size_t error_size);

bool cetta_tptp_prepared_reader_read_text_projected_outcome_v1(
    const CettaTptpPreparedReaderV1 *reader,
    const char *text,
    size_t text_len,
    CettaTptpCstProjectionV1 projection,
    void *projection_context,
    Arena *arena,
    Atom **out_records,
    CettaTptpReadOutcomeV1 *outcome,
    char *error,
    size_t error_size);

bool cetta_tptp_prepared_reader_read_file_outcome_v1(
    const CettaTptpPreparedReaderV1 *reader,
    const char *path,
    Arena *arena,
    Atom **out_records,
    CettaTptpReadOutcomeV1 *outcome,
    char *error,
    size_t error_size);

bool cetta_tptp_prepared_reader_read_file_projected_outcome_v1(
    const CettaTptpPreparedReaderV1 *reader,
    const char *path,
    CettaTptpCstProjectionV1 projection,
    void *projection_context,
    Arena *arena,
    Atom **out_records,
    CettaTptpReadOutcomeV1 *outcome,
    char *error,
    size_t error_size);

bool cetta_tptp_snapshot_read_text_cost_v1(
    const PPTableSnapshotV1 *snap,
    const char *text,
    size_t text_len,
    Arena *arena,
    Atom **out_records,
    CettaTptpReadCostV1 *cost,
    char *error,
    size_t error_size);

/* Visit each TPTP_input without retaining the file record. on_input must
 * not keep Atom pointers after it returns; the arena is reset per input. */
typedef bool (*CettaTptpInputVisitV1)(Atom *input, void *user);

bool cetta_tptp_snapshot_read_text_each_v1(
    const PPTableSnapshotV1 *snap,
    const char *text,
    size_t text_len,
    Arena *arena,
    CettaTptpInputVisitV1 on_input,
    void *user,
    CettaTptpReadCostV1 *cost,
    char *error,
    size_t error_size);

/* As above, with an explicit parser-work budget for each TPTP_input.
 * A zero budget selects the parser's default. */
bool cetta_tptp_snapshot_read_text_each_with_work_limit_v1(
    const PPTableSnapshotV1 *snap,
    const char *text,
    size_t text_len,
    uint64_t work_limit,
    Arena *arena,
    CettaTptpInputVisitV1 on_input,
    void *user,
    CettaTptpReadCostV1 *cost,
    char *error,
    size_t error_size);

bool cetta_tptp_prepared_reader_read_text_each_with_work_limit_v1(
    const CettaTptpPreparedReaderV1 *reader,
    const char *text,
    size_t text_len,
    uint64_t work_limit,
    uint64_t gll_descriptor_limit,
    Arena *arena,
    CettaTptpInputVisitV1 on_input,
    void *user,
    CettaTptpReadCostV1 *cost,
    char *error,
    size_t error_size);

bool cetta_tptp_read_text_frozen_v1(
    const char *text,
    Arena *arena,
    Atom **out_records,
    char *error,
    size_t error_size);

bool cetta_tptp_read_text_frozen_outcome_v1(
    const char *text,
    Arena *arena,
    Atom **out_records,
    CettaTptpReadOutcomeV1 *outcome,
    char *error,
    size_t error_size);

bool cetta_tptp_read_file_frozen_v1(
    const char *path,
    Arena *arena,
    Atom **out_records,
    char *error,
    size_t error_size);

bool cetta_tptp_read_file_frozen_outcome_v1(
    const char *path,
    Arena *arena,
    Atom **out_records,
    CettaTptpReadOutcomeV1 *outcome,
    char *error,
    size_t error_size);

/* Prepared-artifact file utilities: content digest and s-expression I/O. */
bool cetta_tptp_file_sha256_hex_v1(
    const char *path, char out[65], char *error, size_t error_size);

bool cetta_tptp_write_atom_v1(
    const char *path, Atom *atom, Arena *arena, char *error, size_t error_size);

bool cetta_tptp_read_atom_v1(
    const char *path, Arena *arena, Atom **out, char *error, size_t error_size);

#endif

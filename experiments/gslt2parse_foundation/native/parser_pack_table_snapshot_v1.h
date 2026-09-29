#ifndef CETTA_GSLT2PARSE_PARSER_PACK_TABLE_SNAPSHOT_V1_H
#define CETTA_GSLT2PARSE_PARSER_PACK_TABLE_SNAPSHOT_V1_H

#include "lib_parse_native_grammar.h"
#include "regular_span_dfa_v1.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Frozen ParserPack tables: DFA lexical program plus SLR parser program.
 * Magic "TPP1". Independent of C struct layout. Fail-atomic write.
 * Version 4 records each production's flags: 1 authored, 2 avoided. */

#define PP_TABLE_SNAPSHOT_V1_MAGIC "TPP1"
#define PP_TABLE_SNAPSHOT_V1_VERSION 4u
#define PP_TABLE_SNAPSHOT_V1_PROFILE_CAP 32u

typedef enum {
    PP_TABLE_SNAPSHOT_V1_KERNEL_SLR = 0,
    PP_TABLE_SNAPSHOT_V1_KERNEL_GLR = 1
} PPTableSnapshotV1Kernel;

/* The lowering origin of one helper nonterminal: what made it and the
 * authored rule containing it.  kind and repeat are those of
 * CettaGrammarCanonicalOriginV1 (repetition 1, option 2, group 3; '*', '+',
 * '?' or 0). */
typedef struct {
    SymbolId helper;
    SymbolId owner;
    uint32_t kind;
    uint32_t repeat;
} PPTableSnapshotOriginV1;

typedef struct {
    char syntax_digest[65];
    char artifact_digest[65];
    char profile[PP_TABLE_SNAPSHOT_V1_PROFILE_CAP];
    PPTableSnapshotV1Kernel kernel;
    uint32_t conflict_len;
    RSDFAV1Program dfa;
    CettaLpNativeSlrProgram slr;
    char **tag_names;
    uint32_t tag_name_len;
    uint32_t *skip_tags;
    uint32_t skip_tag_len;
    PPTableSnapshotOriginV1 *origins;
    uint32_t origin_len;
    /* Derived from the DFA when a snapshot is read, never serialized.  A tag
     * carries a lexeme when its language holds more than one string; a tag
     * whose language is one string has that string as its fixed text. */
    uint8_t *tag_carries_lexeme;
    char **tag_fixed_texts;
    SymbolId *tag_symbol_ids;
} PPTableSnapshotV1;

void pp_table_snapshot_v1_init(PPTableSnapshotV1 *snap);
void pp_table_snapshot_v1_free(PPTableSnapshotV1 *snap);

/* Compute tag_carries_lexeme and tag_fixed_texts from the DFA. */
bool pp_table_snapshot_v1_derive_tag_facts(
    PPTableSnapshotV1 *snap,
    char *error_buf,
    size_t error_buf_size);

bool pp_table_snapshot_v1_size(
    const PPTableSnapshotV1 *snap,
    size_t *out_size,
    char *error_buf,
    size_t error_buf_size);

bool pp_table_snapshot_v1_write(
    const PPTableSnapshotV1 *snap,
    uint8_t *out_bytes,
    size_t out_size,
    size_t *out_written,
    char *error_buf,
    size_t error_buf_size);

bool pp_table_snapshot_v1_read(
    PPTableSnapshotV1 *out,
    const uint8_t *bytes,
    size_t size,
    char *error_buf,
    size_t error_buf_size);

bool pp_table_snapshot_v1_write_path(
    const PPTableSnapshotV1 *snap,
    const char *path,
    char *error_buf,
    size_t error_buf_size);

bool pp_table_snapshot_v1_read_path(
    PPTableSnapshotV1 *out,
    const char *path,
    char *error_buf,
    size_t error_buf_size);

#endif

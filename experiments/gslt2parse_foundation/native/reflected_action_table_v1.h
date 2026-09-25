#ifndef CETTA_GSLT2PARSE_REFLECTED_ACTION_TABLE_V1_H
#define CETTA_GSLT2PARSE_REFLECTED_ACTION_TABLE_V1_H

#include "parser_action_bytecode_v1.h"
#include "parser_pack_table_snapshot_v1.h"

#include <stdbool.h>
#include <stddef.h>

#define PP_REFLECTED_ACTION_COMPILER_ID_V1 \
    "grammar-head-election-action-reflection-v1"
#define PP_REFLECTED_ACTION_COMPILER_DIGEST_V1 \
    "04538c1928f92ae76893ead311429c9ea38367d4bee6d2a451b97a86a1259224"

/* Check a decoded reflected-action artifact against the exact table snapshot
 * and compile its dense action rows to the shared postfix executor. */
bool pp_reflected_action_table_v1_build(
    const PPTableSnapshotV1 *snapshot,
    const Atom *artifact,
    const char *artifact_digest,
    PPActionBytecodeV1Program *out,
    char *error_buf,
    size_t error_buf_size);

#endif

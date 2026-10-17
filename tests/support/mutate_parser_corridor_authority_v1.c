#include "parser_pack_table_snapshot_v1.h"
#include "native_sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A development negative control: retain the complete GLL/semantic grammar
 * while deleting one competing LR reduction and recomputing the checksum. */
int main(int argc, char **argv) {
    PPTableSnapshotV1 snapshot = {0};
    char error[512] = {0};
    uint8_t *bytes = NULL;
    CettaLpNativeGlrPrepared owner = {0};
    CettaLpNativeUtf8Forest forest = {0};
    size_t size = 0u, written = 0u;
    int status = 1;
    if (argc != 3 || !pp_table_snapshot_v1_read_path(&snapshot, argv[1], error, sizeof(error))) goto done;
    uint32_t removed = UINT32_MAX;
    for (uint32_t i = 0u; i < snapshot.slr.glr_action_len; ++i) {
        const CettaLpNativeGlrAction *a = &snapshot.slr.glr_actions[i];
        if (a->kind != CETTA_LP_NATIVE_SLR_PROGRAM_REDUCE) continue;
        for (uint32_t j = i + 1u; j < snapshot.slr.glr_action_len; ++j) {
            const CettaLpNativeGlrAction *b = &snapshot.slr.glr_actions[j];
            if (b->state == a->state && b->token_idx == a->token_idx &&
                b->kind == CETTA_LP_NATIVE_SLR_PROGRAM_REDUCE && b->value != a->value) {
                removed = i; break;
            }
        }
        if (removed != UINT32_MAX) break;
    }
    if (removed == UINT32_MAX) goto done;
    memmove(snapshot.slr.glr_actions + removed, snapshot.slr.glr_actions + removed + 1u,
        (size_t)(snapshot.slr.glr_action_len - removed - 1u) * sizeof(*snapshot.slr.glr_actions));
    --snapshot.slr.glr_action_len;
    CettaLpNativeSlrLatticeOutcome outcome;
    if (!cetta_lp_native_glr_prepared_import_program(&owner, &snapshot.slr, &snapshot.gll, error, sizeof(error)) ||
        !cetta_lp_native_glr_prepared_parse_owned_utf8_deterministic_forest(&owner,
            snapshot.scalar_terminals.terminals, snapshot.scalar_terminals.len,
            (const uint8_t *)"z", 1u, UINT32_MAX, &outcome, &forest, error, sizeof(error)) ||
        outcome != CETTA_LP_NATIVE_SLR_LATTICE_ACCEPTED || forest.root_len != 1u) goto done;
    memset(snapshot.artifact_digest, '0', 64u); snapshot.artifact_digest[64] = '\0';
    if (!pp_table_snapshot_v1_size(&snapshot, &size, error, sizeof(error))) goto done;
    bytes = malloc(size);
    if (!bytes || !pp_table_snapshot_v1_write(&snapshot, bytes, size, &written, error, sizeof(error)) || written != size) goto done;
    CettaNativeSha256 sha;
    char digest[65];
    const char domain[] = "cetta-general-parser-artifact-v1";
    cetta_native_sha256_init(&sha);
    cetta_native_sha256_update(&sha, (const uint8_t *)domain, sizeof(domain) - 1u);
    cetta_native_sha256_update(&sha, bytes, size);
    cetta_native_sha256_finish_hex(&sha, digest);
    memcpy(bytes + 72u, digest, 64u);
    FILE *output = fopen(argv[2], "wb");
    if (!output) goto done;
    bool saved = fwrite(bytes, 1u, size, output) == size;
    if (fclose(output)) saved = false;
    if (!saved) goto done;
    printf("(ParserCorridorAuthorityMutationV1 %u %u)\n", removed, snapshot.slr.glr_action_len);
    status = 0;
done:
    if (status) fprintf(stderr, "%s\n", error[0] ? error : "expected competing LR reductions and writable output");
    free(bytes); pp_table_snapshot_v1_free(&snapshot);
    cetta_lp_native_utf8_forest_free(&forest); cetta_lp_native_glr_prepared_free(&owner);
    return status;
}

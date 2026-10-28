#ifndef CETTA_PROCESS_SCOPE_V1_H
#define CETTA_PROCESS_SCOPE_V1_H
#include <stdint.h>

/* Private local process protocol. fd 3 reads the owner's liveness pipe;
 * closure of its writer revokes the tree. fd 4 writes one final receipt.
 * With the release argument before the program, what the command leaves
 * running when it exits by itself is released instead of revoked. */
#define CETTA_PROCESS_SCOPE_MAGIC_V1 UINT32_C(0x43505331)
#define CETTA_PROCESS_SCOPE_RELEASE_ON_EXIT_V1 "--release-on-exit"
typedef struct {
    uint32_t magic;
    int32_t launch_error;
    int32_t wait_status;
    int32_t host_error;
} CettaProcessScopeReceiptV1;
#endif

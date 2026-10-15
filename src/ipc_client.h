#ifndef CETTA_IPC_CLIENT_H
#define CETTA_IPC_CLIENT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Native bytes only. The evaluator owns this nonblocking local transaction;
 * readiness never evaluates or acknowledges its reply. */
typedef struct CettaIpcClient CettaIpcClient;
typedef enum { CETTA_IPC_CWP, CETTA_IPC_CSP } CettaIpcFraming;
CettaIpcClient *cetta_ipc_client_new(const char *path, CettaIpcFraming framing,
    const void *request, size_t size, uint64_t timeout_ms, size_t max_reply);
void cetta_ipc_client_pump(CettaIpcClient *client);
int cetta_ipc_client_fd(const CettaIpcClient *client);
short cetta_ipc_client_events(const CettaIpcClient *client);
uint64_t cetta_ipc_client_deadline(const CettaIpcClient *client);
bool cetta_ipc_client_ready(const CettaIpcClient *client);
bool cetta_ipc_client_sent(const CettaIpcClient *client);
const char *cetta_ipc_client_error(const CettaIpcClient *client);
const unsigned char *cetta_ipc_client_reply(const CettaIpcClient *client, size_t *size);
void cetta_ipc_client_free(CettaIpcClient *client);
#endif

#ifndef CETTA_IPC_CLIENT_H
#define CETTA_IPC_CLIENT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/* Shared local service listener and CSP1 framing. Stale owned socket files
 * may be removed after a connection probe; callers serialize startup. */
int cetta_ipc_listen(const char *path, mode_t mode, gid_t group, int backlog);
bool cetta_ipc_csp_header(unsigned char header[8], size_t bytes);
bool cetta_ipc_csp_size(const unsigned char header[8], size_t maximum, size_t *bytes);

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

/* SPDX-License-Identifier: MIT */
#ifndef CETTA_SIWC_H
#define CETTA_SIWC_H
#include <stdbool.h>
#include <stdint.h>
typedef struct CettaSiwcOperation CettaSiwcOperation;
bool cetta_siwc_available(void);
/* kind: sign-in, ensure, sign-out, accounts. Empty/new registration starts a
 * fresh registration. Zero deadline means no deadline; port zero chooses one. */
CettaSiwcOperation *cetta_siwc_start(const char *kind, const char *registration,
    unsigned port, uint64_t deadline_ms, int wakeup_fd);
bool cetta_siwc_ready(CettaSiwcOperation *operation);
char *cetta_siwc_status(CettaSiwcOperation *operation);
char *cetta_siwc_result(CettaSiwcOperation *operation);
void cetta_siwc_free(CettaSiwcOperation *operation);
/* Native-only bearer resolution. Refresh is an owned ensure operation before
 * HTTP submission, so network renewal never blocks the evaluator here. */
char *cetta_siwc_bearer(const char *alias, const char *url);
#endif

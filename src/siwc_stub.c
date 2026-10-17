/* SPDX-License-Identifier: MIT */
#include "siwc.h"
#include <stddef.h>
bool cetta_siwc_available(void) { return false; }
CettaSiwcOperation *cetta_siwc_start(const char *kind, const char *id, unsigned port, uint64_t deadline, int wakeup) {
    (void)kind; (void)id; (void)port; (void)deadline; (void)wakeup; return NULL;
}
bool cetta_siwc_ready(CettaSiwcOperation *op) { (void)op; return false; }
char *cetta_siwc_status(CettaSiwcOperation *op) { (void)op; return NULL; }
char *cetta_siwc_result(CettaSiwcOperation *op) { (void)op; return NULL; }
void cetta_siwc_free(CettaSiwcOperation *op) { (void)op; }
char *cetta_siwc_bearer(const char *alias, const char *url) { (void)alias; (void)url; return NULL; }

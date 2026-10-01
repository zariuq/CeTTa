#ifndef CETTA_RUN_GUARD_H
#define CETTA_RUN_GUARD_H
#include <stdint.h>
/* Explicit contracted/reporting invocations require a final process receipt.
 * A raw host exit cannot bypass that check. Ordinary invocations are direct. */
int cetta_run_guard(int argc, char **argv, int (*entry)(int, char **));
void cetta_run_guard_declare(uint64_t queries);
#endif

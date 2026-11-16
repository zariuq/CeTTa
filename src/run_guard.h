#ifndef CETTA_RUN_GUARD_H
#define CETTA_RUN_GUARD_H
#include <stdbool.h>
#include <stdint.h>
/* Explicit contracted/reporting invocations require a final process receipt.
 * A raw host exit cannot bypass that check. Activate only after the CLI has
 * parsed its options, before executing the document. True continues in the
 * child; false returns the parent's settled status through parent_status.
 * A signalled child retains the shell status 128 + signal; its report stays
 * unfinalized. The guard never parses or modifies guest arguments. */
bool cetta_run_guard_launch(const char *report_path, int *parent_status);
int cetta_run_guard_finish(int status);
void cetta_run_guard_declare(uint64_t queries);
#endif

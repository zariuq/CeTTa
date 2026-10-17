#ifndef CETTA_HOL_CLI_H
#define CETTA_HOL_CLI_H

#include "hol_replay.h"
#include <stdio.h>

/* Returns the retained replay, including a refused/resource frontier. The
 * caller frees it. Host I/O/interruption failure is reported separately. */
HolReplay *cetta_hol_read_stream(HolKernel *kernel, FILE *file,
                                HolAxiomPolicy policy, HolStatus *status);
int cetta_hol_run_file(const char *path, HolAxiomPolicy policy, uint64_t fuel,
                       bool count_only, bool no_results, bool statistics);

#endif

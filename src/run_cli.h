#ifndef CETTA_RUN_CLI_H
#define CETTA_RUN_CLI_H
#include "run_report.h"
#include "error_presentation.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct RunCli RunCli;
RunCli *run_cli_begin(void);
void run_cli_free(RunCli *cli);
bool run_cli_declare_queries(RunCli *cli, uint64_t count);
bool run_cli_query_begin(RunCli *cli);
void run_cli_test_verdict(void *cli, bool passed);
void run_cli_answers(RunCli *cli, uint64_t count);
void run_cli_query_stop(RunCli *cli, RunReportStopKind stop);
void run_cli_query_fault(RunCli *cli, const CettaErrorPresentation *detail);
void run_cli_query_end(RunCli *cli, bool output_ok, bool cleanup_ok);
/* The OS status includes publication failure. The serialized snapshot states
 * explicitly that its own publication has not yet been acknowledged. */
int run_cli_finish(RunCli *cli, int dialect_exit, bool output_ok,
                   bool cleanup_ok, const char *report_path, bool strict);
#endif

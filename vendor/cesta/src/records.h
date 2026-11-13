#ifndef CESTA_RECORDS_H
#define CESTA_RECORDS_H

#include "sema.h"

typedef struct { CeText code, facts; } CeRecordVisitors;

/* Validate optional record/visitor contracts against the actual C types.
   Native visitors remain named assumptions. No prover is invoked. */
bool ce_record_visitors(CeArena *arena, CeSema *sema, const char *schema_path,
                        CeRecordVisitors *output, CeError *error);
bool ce_record_output(const char *path, CeText text, CeError *error);

#endif

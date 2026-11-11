#ifndef CETTA_MEGALODON_RUN_H
#define CETTA_MEGALODON_RUN_H

/*
 * Running Megalodon documents as the megalodon binary runs them: the
 * signatures (-I) first, in order, then the document, each read by
 * mg_source and evaluated by mg_elab item by item.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "mg_elab.h"

typedef struct {
    const char *path;
    bool signature;
} MgRunFile;

typedef enum {
    MG_RUN_OK = 0,
    MG_RUN_FAILURE = 1,     /* a syntax error or failure, as Megalodon reports it */
    MG_RUN_EXCEPTION = 2,   /* where Megalodon stops with an uncaught exception */
    MG_RUN_REFUSED = 3,     /* the kernel refused what Megalodon accepts */
    MG_RUN_UNREADABLE = 4,  /* a file could not be read */
} MgRunStatus;

/* Evaluate the files in order, stopping at the first that does not go
 * through.  `message` receives what stopped it: Megalodon's syntax error
 * or failure text (one or two lines), the uncaught exception, the kernel's
 * refusal, or the unreadable path. */
MgRunStatus mg_run_files(MgElab *e, const MgRunFile *files, int count, char *message, size_t cap);

#endif

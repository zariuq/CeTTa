#ifndef CETTA_VALUE_SERVICE_H
#define CETTA_VALUE_SERVICE_H
#include "library.h"
/* Trusted MeTTa configuration/routes, CSP1 data messages, runtime-owned
 * connection-local resources. This is a single-account local service host. */
int cetta_value_service_run(CettaLibraryContext *context,Space *program,
    Registry *registry,Arena *permanent,const char *configuration);
#endif

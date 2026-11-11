#ifndef CETTA_RUNTIME_SESSION_H
#define CETTA_RUNTIME_SESSION_H

#include "library.h"
#include "eval.h"

/* The execution domain shared by command-line and foreign hosts. The host's
 * name domain and persistent arena outlive this owner. Reader/output/handle
 * lifetimes belong to the host and must stop before session disposal. */
typedef struct {
    CettaLibraryContext libraries;
    Space space;
    Registry registry;
    CettaEvalResources *resources;
    bool libraries_ready, space_ready, registry_ready, work_closed;
} CettaRuntimeSession;

/* Start from a zero-initialized owner. Partial initialization remains owned
 * and can always be disposed. The name domain must be selected by the host. */
bool cetta_runtime_session_init(CettaRuntimeSession *, Arena *,
    CettaLanguageId, const CettaProfile *);
bool cetta_runtime_session_init_in_domain(CettaRuntimeSession *, Arena *,
    CettaLanguageId, const CettaProfile *, struct CettaTermDomain *);
bool cetta_runtime_session_open_root(CettaRuntimeSession *, Arena *);
void cetta_runtime_session_stop(CettaRuntimeSession *);
/* Idempotent. Selects this session's execution resources during teardown,
 * preserving another active session's resources and library context. */
void cetta_runtime_session_dispose(CettaRuntimeSession *);

#endif

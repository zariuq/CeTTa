#ifndef CETTA_REMOTE_DB_H
#define CETTA_REMOTE_DB_H
#include "space.h"
#include "value_rpc.h"
typedef struct CettaRemoteDb CettaRemoteDb;
typedef struct CettaRemoteCapture CettaRemoteCapture;
typedef struct Resource CettaRemoteResource;
CettaValueRpcStatus cetta_remote_db_open(CettaIpcSession *,size_t maximum_bytes,
    uint64_t timeout_ms,CettaRemoteDb **);
void cetta_remote_db_retain(CettaRemoteDb *);
void cetta_remote_db_release(CettaRemoteDb *);
bool cetta_remote_db_writable(const CettaRemoteDb *);
bool cetta_remote_db_same_domain(const CettaRemoteDb *,const CettaRemoteDb *);
Atom *cetta_remote_db_context(const CettaRemoteDb *,Arena *);
CettaValueRpcStatus cetta_remote_db_call(CettaRemoteDb *,const char *operation,
    Atom *arguments,Arena *,Atom **result);
CettaValueRpcStatus cetta_remote_db_start(CettaRemoteDb *,const char *operation,
    Atom *arguments,Arena *,CettaRemoteResource **,Atom **metadata);
CettaValueRpcStatus cetta_remote_resource_call(CettaRemoteResource *,const char *operation,
    Atom *arguments,Arena *,Atom **result);
CettaValueRpcStatus cetta_remote_resource_start(CettaRemoteResource *,const char *operation,
    Atom *arguments,Arena *,CettaRemoteResource **,Atom **metadata);
CettaRemoteDb *cetta_remote_resource_db(const CettaRemoteResource *);
void cetta_remote_resource_release(CettaRemoteResource *);
void cetta_remote_resource_storage(const void *,CettaStorageCensus *);
CettaValueRpcStatus cetta_remote_db_capture(CettaRemoteDb *,Atom *collection,
    CettaRemoteCapture **);
CettaValueRpcStatus cetta_remote_capture_related(CettaRemoteCapture *,Atom *collection,
    CettaRemoteCapture **);
void cetta_remote_capture_retain(CettaRemoteCapture *);
void cetta_remote_capture_release(CettaRemoteCapture *);
bool cetta_remote_capture_space(CettaRemoteCapture *,Space *initialized_target);
CettaRemoteCapture *cetta_remote_capture_from_space(const Space *);
CettaRemoteDb *cetta_remote_capture_db(const CettaRemoteCapture *);
CettaValueRpcStatus cetta_remote_capture_prepare_publish(CettaRemoteCapture *,Atom *proposal,CettaValueRpc **);
Atom *cetta_remote_capture_context(const CettaRemoteCapture *,Arena *);
CettaValueRpcStatus cetta_remote_capture_publish(CettaRemoteCapture *,Atom *proposal,
    Arena *,Atom **result);
void cetta_remote_db_storage(const void *,CettaStorageCensus *);
void cetta_remote_capture_storage(const void *,CettaStorageCensus *);
#endif

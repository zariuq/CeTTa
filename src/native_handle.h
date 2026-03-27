#ifndef CETTA_NATIVE_HANDLE_H
#define CETTA_NATIVE_HANDLE_H

#include "atom.h"
#include <stdbool.h>
#include <stdint.h>

typedef void (*CettaNativeHandleFreeFn)(void *resource);

typedef struct {
    uint64_t id;
    const char *kind;
    void *resource;
    CettaNativeHandleFreeFn free_resource;
} CettaNativeHandleSlot;

struct CettaLibraryContext;

Atom *cetta_native_handle_atom(Arena *a, const char *kind, uint64_t id);
bool cetta_native_handle_arg(Atom *arg, const char *expected_kind, uint64_t *id_out);

bool cetta_native_handle_alloc(struct CettaLibraryContext *ctx, const char *kind,
                               void *resource, CettaNativeHandleFreeFn free_resource,
                               uint64_t *id_out);
void *cetta_native_handle_get(struct CettaLibraryContext *ctx, const char *kind,
                              uint64_t id);
bool cetta_native_handle_close(struct CettaLibraryContext *ctx, const char *kind,
                               uint64_t id);
void cetta_native_handle_cleanup_all(struct CettaLibraryContext *ctx);

#endif /* CETTA_NATIVE_HANDLE_H */

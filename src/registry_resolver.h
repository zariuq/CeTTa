#ifndef CETTA_REGISTRY_RESOLVER_H
#define CETTA_REGISTRY_RESOLVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "space.h"

/* A capability is deliberately opaque.  Structural descriptors are public
   data; possession of this runtime-minted object is a separate authority. */
typedef struct RegistryCapability RegistryCapability;

typedef bool (*RegistryRecognitionFn)(
    void *context, const Registry *source, const Atom *name_key,
    const Atom *value);

typedef enum {
    REGISTRY_ROUTE_PUBLIC = 0,
    REGISTRY_ROUTE_CAPABILITY = 1,
} RegistryRouteAccess;

typedef struct {
    Registry *registry;
    uint64_t source_id;
    RegistryRouteAccess access;
    const RegistryCapability *required_capability;
    RegistryRecognitionFn recognize;
    void *recognition_context;
} RegistryResolverRoute;

#define REGISTRY_RESOLVER_INLINE_ROUTES 4u

typedef struct {
    RegistryResolverRoute *routes;
    size_t len;
    size_t cap;
    RegistryResolverRoute inline_routes[REGISTRY_RESOLVER_INLINE_ROUTES];
} RegistryResolver;

typedef struct {
    Atom *value;
    Registry *source;
    uint64_t source_id;
    size_t route_index;
    RegistryRouteAccess access;
} RegistryResolution;

#define REGISTRY_RESOLUTION_INLINE_ITEMS 4u

typedef struct {
    RegistryResolution *items;
    size_t len;
    size_t cap;
    RegistryResolution inline_items[REGISTRY_RESOLUTION_INLINE_ITEMS];
} RegistryResolutionSet;

typedef enum {
    REGISTRY_RESOLUTION_MALFORMED_NAME = 0,
    REGISTRY_RESOLUTION_NO_MATCH = 1,
    REGISTRY_RESOLUTION_UNIQUE = 2,
    REGISTRY_RESOLUTION_AMBIGUOUS = 3,
} RegistryResolutionStatus;

RegistryCapability *registry_capability_new(void);
void registry_capability_delete(RegistryCapability *capability);

void registry_resolver_init(RegistryResolver *resolver);
void registry_resolver_free(RegistryResolver *resolver);
bool registry_resolver_add_public(
    RegistryResolver *resolver, Registry *source, uint64_t source_id,
    RegistryRecognitionFn recognize, void *recognition_context);
bool registry_resolver_add_capability(
    RegistryResolver *resolver, Registry *source, uint64_t source_id,
    const RegistryCapability *required_capability,
    RegistryRecognitionFn recognize, void *recognition_context);
size_t registry_resolver_route_count(const RegistryResolver *resolver);

void registry_resolution_set_init(RegistryResolutionSet *results);
void registry_resolution_set_free(RegistryResolutionSet *results);
RegistryResolutionStatus registry_resolver_lookup_name(
    const RegistryResolver *resolver, Atom *name_key,
    const RegistryCapability *const *presented_capabilities,
    size_t presented_capability_count, RegistryResolutionSet *results);
const char *registry_resolution_status_name(RegistryResolutionStatus status);

/* An explicit recognition policy for public data with no extra filtering. */
bool registry_recognize_all(void *context, const Registry *source,
                            const Atom *name_key, const Atom *value);

#endif /* CETTA_REGISTRY_RESOLVER_H */

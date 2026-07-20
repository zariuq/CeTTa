#include "registry_resolver.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#ifndef CETTA_REGISTRY_RESOLVER_MUTATION
#define CETTA_REGISTRY_RESOLVER_MUTATION 0
#endif

struct RegistryCapability {
    uint64_t serial;
};

static _Atomic uint64_t registry_capability_next_serial = 1u;

RegistryCapability *registry_capability_new(void) {
    RegistryCapability *capability = cetta_malloc(sizeof(*capability));
    capability->serial = atomic_fetch_add_explicit(
        &registry_capability_next_serial, 1u, memory_order_relaxed);
    return capability;
}

void registry_capability_delete(RegistryCapability *capability) {
    free(capability);
}

void registry_resolver_init(RegistryResolver *resolver) {
    if (!resolver) return;
    resolver->routes = resolver->inline_routes;
    resolver->len = 0u;
    resolver->cap = REGISTRY_RESOLVER_INLINE_ROUTES;
}

void registry_resolver_free(RegistryResolver *resolver) {
    if (!resolver) return;
    if (resolver->routes && resolver->routes != resolver->inline_routes)
        free(resolver->routes);
    resolver->routes = resolver->inline_routes;
    resolver->len = 0u;
    resolver->cap = REGISTRY_RESOLVER_INLINE_ROUTES;
}

static bool registry_resolver_ensure_capacity(RegistryResolver *resolver,
                                              size_t needed) {
    if (resolver->cap >= needed) return true;
    size_t next_cap = resolver->cap ? resolver->cap :
        REGISTRY_RESOLVER_INLINE_ROUTES;
    while (next_cap < needed) {
        if (next_cap > SIZE_MAX / 2u) return false;
        next_cap *= 2u;
    }
    if (next_cap > SIZE_MAX / sizeof(*resolver->routes)) return false;
    if (resolver->routes == resolver->inline_routes) {
        RegistryResolverRoute *next =
            cetta_malloc(sizeof(*next) * next_cap);
        memcpy(next, resolver->routes, sizeof(*next) * resolver->len);
        resolver->routes = next;
    } else {
        resolver->routes = cetta_realloc(
            resolver->routes, sizeof(*resolver->routes) * next_cap);
    }
    resolver->cap = next_cap;
    return true;
}

static bool registry_resolver_add_route(
    RegistryResolver *resolver, Registry *source, uint64_t source_id,
    RegistryRouteAccess access,
    const RegistryCapability *required_capability,
    RegistryRecognitionFn recognize, void *recognition_context) {
    if (!resolver || !source || source_id == 0u || !recognize)
        return false;
    if ((access == REGISTRY_ROUTE_CAPABILITY) !=
        (required_capability != NULL))
        return false;
    for (size_t i = 0u; i < resolver->len; i++)
        if (resolver->routes[i].source_id == source_id)
            return false;
    if (!registry_resolver_ensure_capacity(resolver, resolver->len + 1u))
        return false;
    resolver->routes[resolver->len++] = (RegistryResolverRoute){
        .registry = source,
        .source_id = source_id,
        .access = access,
        .required_capability = required_capability,
        .recognize = recognize,
        .recognition_context = recognition_context,
    };
    return true;
}

bool registry_resolver_add_public(
    RegistryResolver *resolver, Registry *source, uint64_t source_id,
    RegistryRecognitionFn recognize, void *recognition_context) {
    return registry_resolver_add_route(
        resolver, source, source_id, REGISTRY_ROUTE_PUBLIC, NULL,
        recognize, recognition_context);
}

bool registry_resolver_add_capability(
    RegistryResolver *resolver, Registry *source, uint64_t source_id,
    const RegistryCapability *required_capability,
    RegistryRecognitionFn recognize, void *recognition_context) {
    return registry_resolver_add_route(
        resolver, source, source_id, REGISTRY_ROUTE_CAPABILITY,
        required_capability, recognize, recognition_context);
}

size_t registry_resolver_route_count(const RegistryResolver *resolver) {
    return resolver ? resolver->len : 0u;
}

void registry_resolution_set_init(RegistryResolutionSet *results) {
    if (!results) return;
    results->items = results->inline_items;
    results->len = 0u;
    results->cap = REGISTRY_RESOLUTION_INLINE_ITEMS;
}

void registry_resolution_set_free(RegistryResolutionSet *results) {
    if (!results) return;
    if (results->items && results->items != results->inline_items)
        free(results->items);
    results->items = results->inline_items;
    results->len = 0u;
    results->cap = REGISTRY_RESOLUTION_INLINE_ITEMS;
}

static bool registry_resolution_set_push(RegistryResolutionSet *results,
                                         RegistryResolution result) {
    if (results->len == results->cap) {
        size_t next_cap = results->cap ? results->cap * 2u :
            REGISTRY_RESOLUTION_INLINE_ITEMS;
        if (next_cap < results->cap ||
            next_cap > SIZE_MAX / sizeof(*results->items))
            return false;
        if (results->items == results->inline_items) {
            RegistryResolution *next =
                cetta_malloc(sizeof(*next) * next_cap);
            memcpy(next, results->items, sizeof(*next) * results->len);
            results->items = next;
        } else {
            results->items = cetta_realloc(
                results->items, sizeof(*results->items) * next_cap);
        }
        results->cap = next_cap;
    }
    results->items[results->len++] = result;
    return true;
}

static bool registry_capability_present(
    const RegistryCapability *required,
    const RegistryCapability *const *presented, size_t presented_count) {
    if (!required) return true;
    for (size_t i = 0u; i < presented_count; i++)
        if (presented && presented[i] == required)
            return true;
    return false;
}

RegistryResolutionStatus registry_resolver_lookup_name(
    const RegistryResolver *resolver, Atom *name_key,
    const RegistryCapability *const *presented_capabilities,
    size_t presented_capability_count, RegistryResolutionSet *results) {
    if (!results) return REGISTRY_RESOLUTION_MALFORMED_NAME;
    results->len = 0u;
    if (!resolver || !name_key_is_admissible(name_key))
        return REGISTRY_RESOLUTION_MALFORMED_NAME;

    for (size_t i = 0u; i < resolver->len; i++) {
        const RegistryResolverRoute *route = &resolver->routes[i];
        if (!registry_capability_present(route->required_capability,
                                         presented_capabilities,
                                         presented_capability_count))
            continue;
        Atom *value = registry_lookup_name(route->registry, name_key);
        if (!value) continue;
#if CETTA_REGISTRY_RESOLVER_MUTATION == 1
        bool recognized = true;
#else
        bool recognized = route->recognize(
            route->recognition_context, route->registry, name_key, value);
#endif
        if (!recognized) continue;
        if (!registry_resolution_set_push(
                results,
                (RegistryResolution){
                    .value = value,
                    .source = route->registry,
                    .source_id = route->source_id,
                    .route_index = i,
                    .access = route->access,
                }))
            return REGISTRY_RESOLUTION_MALFORMED_NAME;
    }

    if (results->len == 0u) return REGISTRY_RESOLUTION_NO_MATCH;
    if (results->len == 1u) return REGISTRY_RESOLUTION_UNIQUE;
    return REGISTRY_RESOLUTION_AMBIGUOUS;
}

const char *registry_resolution_status_name(RegistryResolutionStatus status) {
    switch (status) {
    case REGISTRY_RESOLUTION_MALFORMED_NAME:
        return "MalformedName";
    case REGISTRY_RESOLUTION_NO_MATCH:
        return "NoMatch";
    case REGISTRY_RESOLUTION_UNIQUE:
        return "Unique";
    case REGISTRY_RESOLUTION_AMBIGUOUS:
        return "Ambiguous";
    }
    return "UnknownRegistryResolutionStatus";
}

bool registry_recognize_all(void *context, const Registry *source,
                            const Atom *name_key, const Atom *value) {
    (void)context;
    return source != NULL && name_key != NULL && value != NULL;
}

/* Registry capabilities draw their serials from a namespace that never
 * wraps.  Built with CETTA_REGISTRY_CAPABILITY_TEST_HOOKS, which lets the
 * test place the next serial near the end. */
#include <stdio.h>

#include "registry_resolver.h"

#if !CETTA_REGISTRY_CAPABILITY_TEST_HOOKS
#error "build with -DCETTA_REGISTRY_CAPABILITY_TEST_HOOKS=1"
#endif

static unsigned checks = 0u;
static unsigned failures = 0u;

#define CHECK(condition, label)                                               \
    do {                                                                      \
        checks++;                                                             \
        if (!(condition)) {                                                   \
            fprintf(stderr, "FAIL: %s\n", (label));                         \
            failures++;                                                      \
        }                                                                     \
    } while (0)

static bool accept_all(void *context, const Registry *source,
                       const Atom *name_key, const Atom *value) {
    (void)context;
    return source && name_key && value;
}

int main(void) {
    CHECK(registry_capability_test_seed(UINT64_MAX - 1u),
          "capability serials placed at their last identity");
    RegistryCapability *last = registry_capability_new();
    CHECK(last != NULL && registry_capability_test_next() == UINT64_MAX,
          "the last capability serial UINT64_MAX-1 is issued");
    RegistryCapability *refused = registry_capability_new();
    CHECK(refused == NULL && registry_capability_test_next() == UINT64_MAX,
          "an exhausted capability namespace refuses with NULL");
    CHECK(registry_capability_new() == NULL &&
              registry_capability_test_next() == UINT64_MAX,
          "the capability namespace stays exhausted");

    /* A refused capability cannot guard a route. */
    Registry registry;
    RegistryResolver resolver;
    registry_init(&registry);
    registry_resolver_init(&resolver);
    CHECK(!registry_resolver_add_capability(&resolver, &registry, 1u, refused,
                                            accept_all, NULL) &&
              registry_resolver_route_count(&resolver) == 0u,
          "a refused capability adds no capability route");
    CHECK(registry_resolver_add_capability(&resolver, &registry, 1u, last,
                                           accept_all, NULL) &&
              registry_resolver_route_count(&resolver) == 1u,
          "the capability issued before exhaustion still guards a route");
    registry_resolver_free(&resolver);
    registry_free(&registry);
    registry_capability_delete(last);
    CHECK(!registry_capability_test_seed(0u),
          "the capability namespace cannot be seeded with zero");

    printf("(RegistryCapabilityIdentitySummary %u %u %u)\n", checks,
           checks - failures, failures);
    return failures == 0u ? 0 : 1;
}

#include <stdio.h>

#include "atom.h"
#include "registry_resolver.h"
#include "symbol.h"

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

typedef struct {
    Atom *denied;
    unsigned calls;
} RecognitionContext;

static bool recognize_except(void *context, const Registry *source,
                             const Atom *name_key, const Atom *value) {
    RecognitionContext *recognition = context;
    recognition->calls++;
    return source && name_key && value &&
        !atom_eq(recognition->denied, (Atom *)value);
}

static Atom *descriptor(Arena *arena, const char *agent) {
    return atom_expr2(arena, atom_symbol(arena, "agent"),
                      atom_string(arena, agent));
}

static bool result_has_source(const RegistryResolutionSet *results,
                              uint64_t source_id) {
    for (size_t i = 0u; i < results->len; i++)
        if (results->items[i].source_id == source_id)
            return true;
    return false;
}

int main(void) {
    SymbolTable symbols;
    Arena arena;
    Registry local_a, local_b, private_c, outside_d, denied_e;
    RegistryResolver resolver_a, resolver_b, federation, private_only,
        recognition_only;
    RegistryResolutionSet results;

    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    arena_init(&arena);
    registry_init(&local_a);
    registry_init(&local_b);
    registry_init(&private_c);
    registry_init(&outside_d);
    registry_init(&denied_e);
    registry_resolver_init(&resolver_a);
    registry_resolver_init(&resolver_b);
    registry_resolver_init(&federation);
    registry_resolver_init(&private_only);
    registry_resolver_init(&recognition_only);
    registry_resolution_set_init(&results);

    Atom *max = descriptor(&arena, "Max Botnick");
    Atom *max_copy = descriptor(&arena, "Max Botnick");
    Atom *unknown = descriptor(&arena, "Oruzi");
    Atom *open = atom_expr2(&arena, atom_symbol(&arena, "agent"),
                            atom_var(&arena, "who"));
    Atom *a_value = atom_symbol(&arena, "local-a");
    Atom *b_value = atom_symbol(&arena, "local-b");
    Atom *c_value = atom_symbol(&arena, "private-c");
    Atom *d_value = atom_symbol(&arena, "outside-d");
    Atom *e_value = atom_symbol(&arena, "denied-e");

    CHECK(registry_bind_name(&local_a, max, a_value),
          "local A binds the structural descriptor");
    CHECK(registry_bind_name(&local_b, max_copy, b_value),
          "local B independently binds the equal descriptor");
    CHECK(registry_bind_name(&private_c, max, c_value),
          "private registry binds the public spelling");
    CHECK(registry_bind_name(&outside_d, max, d_value),
          "unconfigured registry binds the public spelling");
    CHECK(registry_bind_name(&denied_e, max, e_value),
          "recognition-controlled registry binds the descriptor");

    CHECK(registry_resolver_add_public(
              &resolver_a, &local_a, 101u, registry_recognize_all, NULL),
          "resolver A explicitly configures local A");
    CHECK(registry_resolver_add_public(
              &resolver_b, &local_b, 202u, registry_recognize_all, NULL),
          "resolver B explicitly configures local B");
    CHECK(registry_resolver_lookup_name(
              &resolver_a, max_copy, NULL, 0u, &results) ==
              REGISTRY_RESOLUTION_UNIQUE &&
              results.len == 1u && results.items[0].value == a_value &&
              results.items[0].source_id == 101u,
          "equal descriptor resolves within resolver A only");
    CHECK(registry_resolver_lookup_name(
              &resolver_b, max, NULL, 0u, &results) ==
              REGISTRY_RESOLUTION_UNIQUE &&
              results.len == 1u && results.items[0].value == b_value &&
              results.items[0].source_id == 202u,
          "isolated resolver B does not magically connect to A");

    uint32_t a_names_before = name_key_count(local_a.name_keys);
    CHECK(registry_resolver_lookup_name(
              &resolver_a, unknown, NULL, 0u, &results) ==
              REGISTRY_RESOLUTION_NO_MATCH && results.len == 0u,
          "unknown descriptor reports NoMatch");
    CHECK(name_key_count(local_a.name_keys) == a_names_before,
          "lookup does not vivify or intern an unknown descriptor");
    CHECK(registry_resolver_lookup_name(
              &resolver_a, open, NULL, 0u, &results) ==
              REGISTRY_RESOLUTION_MALFORMED_NAME && results.len == 0u,
          "open descriptor reports MalformedName");
    CHECK(registry_resolver_route_count(&resolver_a) == 1u,
          "failed lookups do not mutate resolver configuration");

    CHECK(registry_resolver_add_public(
              &federation, &local_a, 101u, registry_recognize_all, NULL) &&
              registry_resolver_add_public(
                  &federation, &local_b, 202u,
                  registry_recognize_all, NULL),
          "federation is an explicit route set");
    CHECK(registry_resolver_lookup_name(
              &federation, max, NULL, 0u, &results) ==
              REGISTRY_RESOLUTION_AMBIGUOUS && results.len == 2u &&
              result_has_source(&results, 101u) &&
              result_has_source(&results, 202u),
          "federation preserves both complete matches and provenance");
    CHECK(!result_has_source(&results, 404u),
          "an unconfigured equal-name registry remains invisible");
    CHECK(!registry_resolver_add_public(
              &federation, &outside_d, 202u,
              registry_recognize_all, NULL),
          "duplicate provenance identity fails closed");

    RegistryCapability *private_capability = registry_capability_new();
    RegistryCapability *forged_capability = registry_capability_new();
    CHECK(private_capability && forged_capability &&
              private_capability != forged_capability,
          "scope-born capabilities have distinct runtime identity");
    CHECK(registry_resolver_add_capability(
              &private_only, &private_c, 303u, private_capability,
              registry_recognize_all, NULL),
          "private route requires an explicit capability");
    CHECK(registry_resolver_lookup_name(
              &private_only, max, NULL, 0u, &results) ==
              REGISTRY_RESOLUTION_NO_MATCH,
          "public knowledge of the descriptor grants no private access");
    const RegistryCapability *forged_set[] = {forged_capability};
    CHECK(registry_resolver_lookup_name(
              &private_only, max, forged_set, 1u, &results) ==
              REGISTRY_RESOLUTION_NO_MATCH,
          "a different capability cannot authorize the private route");
    const RegistryCapability *private_set[] = {private_capability};
    CHECK(registry_resolver_lookup_name(
              &private_only, max_copy, private_set, 1u, &results) ==
              REGISTRY_RESOLUTION_UNIQUE && results.len == 1u &&
              results.items[0].value == c_value &&
              results.items[0].access == REGISTRY_ROUTE_CAPABILITY,
          "possession authorizes the private structural-name lookup");

    CHECK(registry_resolver_add_capability(
              &federation, &private_c, 303u, private_capability,
              registry_recognize_all, NULL),
          "federation may include capability-protected routes");
    CHECK(registry_resolver_lookup_name(
              &federation, max, NULL, 0u, &results) ==
              REGISTRY_RESOLUTION_AMBIGUOUS && results.len == 2u,
          "inaccessible private routes do not affect public results");
    CHECK(registry_resolver_lookup_name(
              &federation, max, private_set, 1u, &results) ==
              REGISTRY_RESOLUTION_AMBIGUOUS && results.len == 3u &&
              result_has_source(&results, 303u),
          "presented capability adds exactly its configured source");

    RecognitionContext recognition = {.denied = e_value, .calls = 0u};
    CHECK(registry_resolver_add_public(
              &recognition_only, &denied_e, 505u,
              recognize_except, &recognition),
          "recognition criterion is route data");
    CHECK(registry_resolver_lookup_name(
              &recognition_only, max, NULL, 0u, &results) ==
              REGISTRY_RESOLUTION_NO_MATCH && recognition.calls == 1u,
          "recognition rejects a bound but inadmissible result");
    CHECK(registry_resolver_lookup_name(
              &recognition_only, open, NULL, 0u, &results) ==
              REGISTRY_RESOLUTION_MALFORMED_NAME &&
              recognition.calls == 1u,
          "malformed names never reach the recognition authority");
    CHECK(registry_resolution_status_name(REGISTRY_RESOLUTION_UNIQUE) &&
              registry_resolution_status_name(
                  REGISTRY_RESOLUTION_AMBIGUOUS),
          "resolution outcomes have explicit public names");

    printf("(RegistryResolverSummary %u %u %u)\n",
           checks, checks - failures, failures);

    registry_capability_delete(forged_capability);
    registry_capability_delete(private_capability);
    registry_resolution_set_free(&results);
    registry_resolver_free(&recognition_only);
    registry_resolver_free(&private_only);
    registry_resolver_free(&federation);
    registry_resolver_free(&resolver_b);
    registry_resolver_free(&resolver_a);
    registry_free(&denied_e);
    registry_free(&outside_d);
    registry_free(&private_c);
    registry_free(&local_b);
    registry_free(&local_a);
    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return failures == 0u && checks == 29u ? 0 : 1;
}

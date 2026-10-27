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

static void read_view_controls(Arena *arena) {
    Registry source;
    RegistryReadView view, nested;
    registry_init(&source);
    SymbolId coefficient = symbol_intern_cstr(g_symbols, "&coefficient");
    SymbolId absent = symbol_intern_cstr(g_symbols, "&absent-coefficient");
    SymbolId unrelated = symbol_intern_cstr(g_symbols, "&saved-residual");
    Atom *two = atom_int(arena, 2), *nine = atom_int(arena, 9);
    registry_bind_id(&source, coefficient, two);
    CHECK(registry_read_view_init(&view, &source, arena),
          "admission captures a complete registry value view");
    registry_bind_id(&source, coefficient, nine);
    RegistryReadView *previous = registry_read_view_enter(&view, &source);
    CHECK(atom_eq(registry_lookup_id(&source, coefficient), two) && view.stale,
          "first post-suspension consultation detects replacement and retains old authority");
    CHECK(atom_eq(view.stale_expected, two) && atom_eq(view.stale_actual, nine),
          "stale receipt contains the admitted and replacement values");
    registry_read_view_leave(previous);
    CHECK(!registry_read_view_validate(&view, &source),
          "stale view cannot silently rebase");
    registry_read_view_free(&view);

    CHECK(registry_read_view_init(&view, &source, arena),
          "new admission may capture the replacement value");
    registry_bind_id(&source, absent, two);
    previous = registry_read_view_enter(&view, &source);
    CHECK(registry_lookup_id(&source, absent) == NULL && view.stale &&
              view.stale_expected == NULL && atom_eq(view.stale_actual, two),
          "initially absent name stays absent and records absent-to-present");
    registry_read_view_leave(previous);
    registry_read_view_free(&view);

    CHECK(registry_read_view_init(&view, &source, arena),
          "capture before a present-to-absent change");
    registry_bind_id(&source, coefficient, NULL);
    previous = registry_read_view_enter(&view, &source);
    CHECK(atom_eq(registry_lookup_id(&source, coefficient), nine) && view.stale &&
              view.stale_actual == NULL,
          "lookup records present-to-absent and retains the admitted value");
    registry_read_view_leave(previous);
    registry_read_view_free(&view);

    registry_bind_id(&source, coefficient, two);
    CHECK(registry_read_view_init(&view, &source, arena),
          "capture before unchanged and unrelated changes");
    previous = registry_read_view_enter(&view, &source);
    CHECK(atom_eq(registry_lookup_id(&source, coefficient), two) && !view.stale,
          "unchanged consultation is admitted");
    registry_read_view_leave(previous);
    registry_bind_id(&source, unrelated, nine);
    registry_bind_id(&source, coefficient, atom_int(arena, 2));
    CHECK(registry_read_view_validate(&view, &source),
          "unrelated names and equal value rebinding preserve consulted authority");
    registry_bind_id(&source, coefficient, nine);
    CHECK(!registry_read_view_validate(&view, &source) &&
              atom_eq(view.stale_expected, two),
          "resume revalidates names already read before suspension");
    registry_read_view_free(&view);

    Atom *name = descriptor(arena, "coefficient");
    CHECK(registry_bind_name(&source, name, two) &&
              registry_read_view_init(&view, &source, arena),
          "structural names have the same admission boundary");
    CHECK(registry_bind_name(&source, name, nine), "replace structural-name binding");
    previous = registry_read_view_enter(&view, &source);
    CHECK(atom_eq(registry_lookup_name(&source, name), two) && view.stale,
          "structural lookup cannot bypass stale-scope detection");
    registry_read_view_leave(previous);
    registry_read_view_free(&view);

    registry_bind_id(&source, absent, NULL);
    CHECK(registry_read_view_init(&view, &source, arena), "capture enclosing scope");
    previous = registry_read_view_enter(&view, &source);
    registry_bind_id(&source, absent, nine);
    CHECK(registry_read_view_init(&nested, &source, arena), "capture nested scope");
    RegistryReadView *outer = registry_read_view_enter(&nested, &source);
    CHECK(registry_lookup_id(&source, absent) == NULL && view.stale && nested.stale,
          "nested admission inherits the parent's absence and both observe the change");
    registry_read_view_leave(outer);
    registry_read_view_leave(previous);
    registry_read_view_free(&nested);
    registry_read_view_free(&view);
    CHECK(atom_eq(registry_lookup_id(&source, absent), nine),
          "ordinary lookup outside a retained scope still sees the live binding");

    CHECK(registry_read_view_init(&view, &source, arena),
          "capture before dependency capacity refusal");
    uint32_t saved_length = view.consulted.len;
    view.consulted.len = UINT32_MAX;
    previous = registry_read_view_enter(&view, &source);
    CHECK(atom_eq(registry_lookup_id(&source, coefficient), nine) && view.incomplete &&
              !view.stale && !registry_read_view_validate(&view, &source),
          "refused symbol dependency is incomplete even when the value is unchanged");
    CHECK(!registry_read_view_init(&nested, &source, arena) && nested.incomplete &&
              !registry_read_view_validate(&nested, &source),
          "nested admission inherits a refused dependency view");
    registry_read_view_free(&nested);
    registry_read_view_leave(previous);
    view.consulted.len = saved_length;
    CHECK(!registry_read_view_validate(&view, &source),
          "a refused dependency stays incomplete after capacity becomes available");
    registry_read_view_free(&view);

    CHECK(registry_read_view_init(&view, &source, arena),
          "capture before structural dependency capacity refusal");
    saved_length = view.consulted.len;
    view.consulted.len = UINT32_MAX;
    previous = registry_read_view_enter(&view, &source);
    CHECK(atom_eq(registry_lookup_name(&source, name), nine) && view.incomplete &&
              !registry_read_view_validate(&view, &source),
          "refused structural dependency cannot authorize publication");
    registry_read_view_leave(previous);
    view.consulted.len = saved_length;
    registry_read_view_free(&view);

    NameKeyTable *keys = source.name_keys;
    source.name_keys = NULL;
    CHECK(!registry_read_view_init(&view, &source, arena) && view.incomplete &&
              !registry_read_view_validate(&view, &source),
          "partial initial capture is retained but cannot certify a complete view");
    source.name_keys = keys;
    registry_read_view_free(&view);

    saved_length = source.len;
    source.len = UINT32_MAX;
    SymbolId refused = symbol_intern_cstr(g_symbols, "&unrecorded-capacity-key");
    CHECK(!registry_bind_id_checked(&source, refused, two) &&
              source.len == UINT32_MAX && registry_lookup_id(&source, refused) == NULL,
          "checked symbol insertion refuses length overflow without modifying entries");
    source.len = saved_length;
    registry_free(&source);
}

static void passive_read_controls(Arena *arena) {
    Registry source;
    registry_init(&source);
    SymbolId key = symbol_intern_cstr(g_symbols, "&passive-input");
    SymbolId absent = symbol_intern_cstr(g_symbols, "&passive-absent");
    Atom *two = atom_int(arena, 2), *nine = atom_int(arena, 9);
    registry_bind_id(&source, key, two);
    RegistryReadView passive, frozen;
    CHECK(registry_read_view_init_passive(&passive, arena), "create a passive dependency observer");
    RegistryReadView *previous = registry_read_view_enter(&passive, &source);
    CHECK(registry_lookup_id(&source, key) == two, "passive lookup returns the exact live value");
    registry_bind_id(&source, key, nine);
    CHECK(registry_lookup_id(&source, key) == nine && passive.stale,
          "a later live value is returned even when reuse evidence becomes stale");
    registry_read_view_leave(previous);
    CHECK(!registry_read_view_validate(&passive, &source), "changed observation cannot authorize reuse");
    registry_read_view_free(&passive);

    registry_bind_id(&source, key, two);
    CHECK(registry_read_view_init(&frozen, &source, arena) &&
          registry_read_view_init_passive(&passive, arena), "frozen selection and passive observation coexist");
    previous = registry_read_view_enter(&frozen, &source);
    RegistryReadView *outer = registry_read_view_enter(&passive, &source);
    registry_bind_id(&source, key, nine);
    Atom *selected = registry_lookup_id(&source, key);
    CHECK(atom_capture_identical(selected, two) && selected != nine &&
          atom_capture_identical(registry_lookup_id(&passive.consulted, key), two),
          "passive capture records the selected admitted value rather than the unselected live value");
    registry_read_view_leave(outer);
    registry_read_view_leave(previous);
    registry_read_view_free(&passive); registry_read_view_free(&frozen);

    CHECK(registry_read_view_init_passive(&passive, arena), "create passive absence capture");
    previous = registry_read_view_enter(&passive, &source);
    CHECK(!registry_lookup_id(&source, absent), "passive capture includes a failed name lookup");
    registry_read_view_leave(previous);
    CHECK(registry_read_view_validate(&passive, &source), "unchanged absence remains current");
    registry_bind_id(&source, absent, two);
    CHECK(!registry_read_view_validate(&passive, &source), "a newly present name invalidates the absent read");
    registry_read_view_free(&passive);

    Atom *name = descriptor(arena, "passive-structured");
    registry_bind_name(&source, name, two);
    CHECK(registry_read_view_init_passive(&passive, arena), "create structural-name observer");
    previous = registry_read_view_enter(&passive, &source);
    CHECK(registry_lookup_name(&source, name) == two &&
          registry_lookup(&source, "&passive-input") == nine,
          "structured and text lookup routes preserve their exact selected values");
    registry_read_view_leave(previous);
    CHECK(registry_read_view_validate(&passive, &source), "all observed lookup routes validate together");
    registry_read_view_free(&passive);

    CHECK(registry_read_view_init_passive(&passive, arena), "create capacity-refusal observer");
    passive.consulted.len = UINT32_MAX;
    previous = registry_read_view_enter(&passive, &source);
    CHECK(registry_lookup_id(&source, key) == nine && passive.incomplete,
          "dependency insertion refusal never substitutes a missing lookup");
    CHECK(registry_read_view_init(&frozen, &source, arena),
          "a failed passive observer does not invalidate a nested admitted view");
    registry_read_view_free(&frozen);
    registry_read_view_leave(previous);
    passive.consulted.len = 0u;
    registry_read_view_free(&passive);

    StateCell a = {.value = two}, b = {.value = two};
    registry_bind_id(&source, key, atom_state(arena, &a));
    CHECK(registry_read_view_init(&frozen, &source, arena), "capture a mutable handle as an input dependency");
    previous = registry_read_view_enter(&frozen, &source);
    registry_bind_id(&source, key, atom_state(arena, &b));
    Atom *held = registry_lookup_id(&source, key);
    CHECK(held && held->ground.ptr == &a && frozen.stale &&
          !registry_read_view_validate(&frozen, &source),
          "rebinding to an equal-valued different State is a changed input");
    registry_read_view_leave(previous); registry_read_view_free(&frozen);
    registry_free(&source);
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
    read_view_controls(&arena);
    passive_read_controls(&arena);
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

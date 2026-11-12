#include "atom.h"
#include "match.h"
#include "tests/test_runtime_stats_stubs.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failures, descriptor_calls;
#define CHECK(condition, message) do { checks++; if (!(condition)) { \
    failures++; fprintf(stderr, "FAIL: %s\n", message); } } while (0)
#define REQUIRE(condition) do { if (!(condition)) abort(); } while (0)

typedef struct {
    CettaForeignHold hold;
    unsigned references;
    CettaBindingsValue *back;
} TestResource;

static void resource_retain(void *raw) {
    TestResource *resource = raw;
    REQUIRE(resource->references > 0u);
    resource->references++;
}

static void resource_release(void *raw) {
    TestResource *resource = raw;
    REQUIRE(resource->references > 1u);
    resource->references--;
}

static void resource_set_back(TestResource *resource, CettaBindingsValue *back) {
    CettaBindingsValue *old = resource->back;
    if (back) back->retain(back);
    resource->back = back;
    if (old) old->release(old);
}

/* A fixture policy, not collector admission: foreign values are understood
 * only when they are this explicitly registered, quiescent test resource. */
typedef struct {
    size_t calls, refuse_at;
    unsigned kinds[256];
    bool failed;
    const CettaBindingsValue *known[16], *seen[32];
    unsigned char state[32];
    size_t known_count, seen_count;
    const Atom *atoms[128];
    size_t atom_count;
    TestResource *resource;
    unsigned shared, cycles, name_keys;
} Inspection;

static bool enter(Inspection *inspection, unsigned char kind) {
    if (inspection->failed) return false;
    inspection->calls++;
    inspection->kinds[kind]++;
    if (inspection->calls == inspection->refuse_at) inspection->failed = true;
    return !inspection->failed;
}

static bool visit_span(const void *pointer, size_t size, void *raw) {
    return pointer && size && enter(raw, 'S');
}

static bool visit_provider(const CettaBindingsValue *, void *);
static bool visit_atom(const Atom *, void *);

static bool visit_hold(const void *owner, void (*release)(void *),
                       CettaStorageVisitor storage, void *raw) {
    (void)storage;
    Inspection *inspection = raw;
    if (!enter(inspection, 'H')) return false;
    for (size_t i = 0u; i < inspection->known_count; i++) {
        const CettaBindingsValue *value = inspection->known[i];
        if (owner == value && release == value->release)
            return visit_provider(value, inspection);
    }
    if (owner != inspection->resource || release != resource_release) return false;
    return enter(inspection, 'F') && (!inspection->resource->back ||
        visit_provider(inspection->resource->back, inspection));
}

static bool visit_arena(const Arena *arena, void *raw) {
    return enter(raw, 'A') &&
        arena_retained_owners_satisfy(arena, visit_hold, raw) &&
        arena_finalizers_satisfy(arena, visit_hold, raw);
}

static bool visit_bindings(const Bindings *bindings, void *raw) {
    return enter(raw, 'B') &&
        bindings_logical_atoms_satisfy(bindings, visit_atom, raw) &&
        bindings_retained_schema_atoms_satisfy(bindings, visit_atom, raw) &&
        bindings_owners_visit_values(bindings->owners, visit_span, visit_provider, raw);
}

static bool visit_atom(const Atom *atom, void *raw) {
    Inspection *inspection = raw;
    if (!enter(inspection, 'T') || !atom) return false;
    for (size_t i = 0u; i < inspection->atom_count; i++)
        if (inspection->atoms[i] == atom) return true;
    REQUIRE(inspection->atom_count < sizeof(inspection->atoms) / sizeof(*inspection->atoms));
    inspection->atoms[inspection->atom_count++] = atom;
    if (!visit_span(atom, sizeof(*atom), raw)) return false;
    if (atom->kind == ATOM_VAR && atom->name_key) {
        inspection->name_keys++;
        return visit_atom(atom->name_key, raw);
    }
    if (atom->kind == ATOM_EXPR) {
        if (atom->expr.len &&
            !visit_span(atom->expr.elems, atom->expr.len * sizeof(*atom->expr.elems), raw))
            return false;
        for (CettaExprIndex i = 0u; i < atom->expr.len; i++)
            if (!visit_atom(atom->expr.elems[i], raw)) return false;
    } else if (atom->kind == ATOM_GROUNDED) {
        if (atom->ground.gkind == GV_BINDINGS) return visit_provider(atom->ground.ptr, raw);
        if (atom->ground.gkind == GV_FOREIGN)
            return visit_hold(atom->ground.ptr, resource_release, NULL, raw);
    }
    return true;
}

static const BindingsOwnedSyntaxVisitor visitor = {
    .span = visit_span, .arena = visit_arena,
    .bindings = visit_bindings, .atom = visit_atom,
};

static bool visit_provider(const CettaBindingsValue *value, void *raw) {
    Inspection *inspection = raw;
    if (!enter(inspection, 'P')) return false;
    size_t position = 0u;
    while (position < inspection->seen_count && inspection->seen[position] != value) position++;
    if (position < inspection->seen_count) {
        if (inspection->state[position] == 1u) {
            inspection->cycles++; inspection->failed = true; return false;
        }
        inspection->shared++;
        return true;
    }
    REQUIRE(position < sizeof(inspection->seen) / sizeof(*inspection->seen));
    inspection->seen[inspection->seen_count++] = value;
    inspection->state[position] = 1u;
    bool passed = bindings_value_visit_owned_syntax(value, &visitor, raw);
    if (!passed) inspection->failed = true;
    else inspection->state[position] = 2u;
    return passed;
}

static CettaBindingsValue *provider(Atom *atom) {
    REQUIRE(atom && atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_BINDINGS);
    return atom->ground.ptr;
}

static Atom *saved_integer(Arena *source, Arena *saved, VarId *id) {
    Bindings bindings;
    bindings_init(&bindings);
    Atom *variable = atom_var_with_id(source, "owner-visit-x", var_id_from_base(7100u));
    if (id) *id = variable->var_id;
    REQUIRE(bindings_add_var(&bindings, variable, atom_int(source, 23)));
    Atom *value = bindings_capture_value(saved, &bindings);
    bindings_free(&bindings);
    REQUIRE(value);
    return value;
}

static void saved_value_survives_and_ledgers_alias(void) {
    Arena source, saved, observation;
    arena_init_detached(&source); arena_init_detached(&saved); arena_init_detached(&observation);
    VarId id;
    Atom *value = saved_integer(&source, &saved, &id);
    CettaBindingsValue *owner = provider(value);
    Bindings restored, alias;
    REQUIRE(bindings_restore_captured_value(value, &restored));
    bindings_init(&alias);
    REQUIRE(bindings_copy(&alias, &restored));
    arena_free(&source); arena_free(&saved);
    Inspection inspection = {.known = {owner}, .known_count = 1u};
    CHECK(restored.owners == alias.owners &&
          bindings_owners_visit_values(restored.owners, visit_span, visit_provider, &inspection) &&
          bindings_owners_visit_values(alias.owners, visit_span, visit_provider, &inspection),
          "restored aliases expose their complete retained owner ledger");
    CHECK(inspection.seen_count == 1u && inspection.shared == 1u &&
          inspection.kinds['B'] == 1u && inspection.kinds['A'] == 1u,
          "a shared owner is inspected once in one attempt");
    Atom *variable = atom_var_with_id(&observation, "owner-visit-x", id);
    Atom *answer = bindings_apply(&restored, &observation, variable);
    CHECK(answer && answer->kind == ATOM_GROUNDED && answer->ground.gkind == GV_INT &&
          answer->ground.ival == 23, "inspection preserves the restored substitution");
    CHECK(bindings_owners_visit_values(NULL, visit_span, visit_provider, &inspection),
          "an empty owner ledger succeeds");
    CHECK(!bindings_owners_visit_values(restored.owners, NULL, visit_provider, &inspection) &&
          !bindings_owners_visit_values(restored.owners, visit_span, NULL, &inspection),
          "ledger callbacks are required");
    size_t calls = inspection.calls;
    inspection.refuse_at = calls + 2u;
    CHECK(!bindings_owners_visit_values(restored.owners, visit_span, visit_provider, &inspection) &&
          inspection.calls == calls + 2u, "a refused ledger span stops provider enumeration");
    bindings_free(&restored); bindings_free(&alias); arena_free(&observation);
}

static void nested_saved_values_and_stop_controls(void) {
    Arena source, saved, observation;
    arena_init_detached(&source); arena_init_detached(&saved); arena_init_detached(&observation);
    Atom *inner = saved_integer(&source, &saved, NULL);
    Atom *variables[] = {
        atom_var_with_name_key(&source, atom_expr2(&source, atom_symbol(&source, "domain"),
            atom_string(&source, "left")), var_id_from_base(7200u)),
        atom_var_with_name_key(&source, atom_expr2(&source, atom_symbol(&source, "domain"),
            atom_string(&source, "right")), var_id_from_base(7201u)),
    };
    Atom *images[] = {variables[1], variables[0]};
    Atom *renaming = bindings_capture_renaming(&saved, variables, images, 2u);
    REQUIRE(renaming);
    Bindings outer;
    bindings_init(&outer);
    REQUIRE(bindings_add_var(&outer, atom_var(&source, "outer"),
                             atom_expr2(&source, inner, renaming)));
    Atom *value = bindings_capture_value(&saved, &outer);
    bindings_free(&outer);
    REQUIRE(value);
    Inspection baseline = {.known = {provider(inner), provider(renaming), provider(value)},
                           .known_count = 3u};
    CHECK(visit_provider(provider(value), &baseline), "a real nested saved-value DAG is inspectable");
    CHECK(baseline.seen_count == 3u && baseline.kinds['A'] == 3u &&
          baseline.kinds['B'] == 2u && baseline.name_keys == 4u && baseline.shared > 0u,
          "inspection covers both kinds, renaming domains/ranges and structured names");
    CHECK(baseline.kinds['S'] > 15u && baseline.kinds['H'] >= 4u,
          "mapping/support/record/vector spans and both hold inventories are exposed");
    for (size_t failure = 1u; failure <= baseline.calls; failure++) {
        Inspection stopped = {.known = {provider(inner), provider(renaming), provider(value)},
                              .known_count = 3u, .refuse_at = failure};
        CHECK(!visit_provider(provider(value), &stopped) && stopped.calls == failure,
              "every stopped callback refuses without later visitation");
        CHECK(!visit_provider(provider(value), &stopped) && stopped.calls == failure,
              "a failed attempt cannot reuse partial owners as complete");
    }
    Inspection retry = {.known = {provider(inner), provider(renaming), provider(value)},
                       .known_count = 3u};
    CHECK(visit_provider(provider(value), &retry) && retry.calls == baseline.calls,
          "a new attempt re-inspects the same unchanged owners");
    Bindings wrong;
    bindings_init(&wrong);
    CHECK(!bindings_restore_captured_value(renaming, &wrong),
          "inspection does not turn a renaming into a substitution");
    bindings_free(&wrong);
    Atom *renamed = bindings_apply_saved(&observation, renaming,
                                         atom_expr2(&source, variables[0], variables[1]));
    CHECK(renamed && renamed->kind == ATOM_EXPR && renamed->expr.len == 2u &&
          var_id_eq(renamed->expr.elems[0]->var_id, variables[1]->var_id) &&
          var_id_eq(renamed->expr.elems[1]->var_id, variables[0]->var_id),
          "inspection preserves simultaneous permutation");
    arena_free(&source); arena_free(&saved); arena_free(&observation);
}

static void unknown_hold_and_cycle_controls(void) {
    Arena source, saved;
    arena_init_detached(&source); arena_init_detached(&saved);
    TestResource resource = {.hold = {.retain = resource_retain, .release = resource_release},
                             .references = 1u};
    Bindings bindings;
    bindings_init(&bindings);
    REQUIRE(bindings_add_var(&bindings, atom_var(&source, "foreign"),
                             atom_foreign(&source, (CettaForeignValue *)&resource)));
    Atom *value = bindings_capture_value(&saved, &bindings);
    REQUIRE(value);
    bindings_free(&bindings);
    unsigned held = resource.references;
    Inspection unknown = {.known = {provider(value)}, .known_count = 1u};
    CHECK(!visit_provider(provider(value), &unknown) && resource.references == held,
          "a real direct foreign finalizer refuses with its original hold intact");
    Inspection understood = {.known = {provider(value)}, .known_count = 1u, .resource = &resource};
    CHECK(visit_provider(provider(value), &understood) && understood.kinds['F'] > 0u &&
          resource.references == held, "a test handler visits its actual held resource");
    resource_set_back(&resource, provider(value));
    Inspection cyclic = {.known = {provider(value)}, .known_count = 1u, .resource = &resource};
    CHECK(!visit_provider(provider(value), &cyclic) && cyclic.cycles == 1u &&
          resource.references == held && resource.back == provider(value),
          "a real host-created strong cycle refuses without releasing either edge");
    resource_set_back(&resource, NULL);
    Inspection after = {.known = {provider(value)}, .known_count = 1u, .resource = &resource};
    CHECK(visit_provider(provider(value), &after), "a fresh attempt accepts after the host breaks its cycle");
    arena_free(&source); arena_free(&saved);
    CHECK(resource.references == 1u, "normal retirement releases all original foreign holds");
}

static void external_retain(void *raw) { (void)raw; descriptor_calls++; }
static void external_release(void *raw) { (void)raw; descriptor_calls++; }
static Atom *external_observe(Arena *arena, const CettaBindingsValue *value) {
    (void)arena; (void)value; descriptor_calls++; return NULL;
}
static bool external_equal(const CettaBindingsValue *left, const CettaBindingsValue *right) {
    (void)left; (void)right; descriptor_calls++; return false;
}
static Atom *external_apply(Arena *arena, const CettaBindingsValue *value, Atom *term) {
    (void)arena; (void)value; (void)term; descriptor_calls++; return NULL;
}
static void external_storage(const void *raw, CettaStorageCensus *census) {
    (void)raw; (void)census; descriptor_calls++;
}

static void descriptor_and_callback_controls(void) {
    CettaBindingsValue external = {.retain = external_retain, .release = external_release,
        .observe = external_observe, .equal = external_equal, .apply = external_apply,
        .storage = external_storage};
    Inspection inspection = {0};
    CHECK(!bindings_value_visit_owned_syntax(&external, &visitor, &inspection) &&
          descriptor_calls == 0u && inspection.calls == 1u,
          "unknown providers refuse without executing descriptor methods");
    Arena source, saved;
    arena_init_detached(&source); arena_init_detached(&saved);
    Atom *value = saved_integer(&source, &saved, NULL);
    for (unsigned method = 0u; method < 6u; method++) {
        CettaBindingsValue altered = *provider(value);
        switch (method) {
        case 0u: altered.retain = external_retain; break;
        case 1u: altered.release = external_release; break;
        case 2u: altered.observe = external_observe; break;
        case 3u: altered.equal = external_equal; break;
        case 4u: altered.apply = external_apply; break;
        default: altered.storage = external_storage; break;
        }
        Inspection unknown = {0};
        CHECK(!bindings_value_visit_owned_syntax(&altered, &visitor, &unknown) &&
              unknown.calls == 1u && descriptor_calls == 0u,
              "a partial private method table never authenticates an owner");
    }
    for (unsigned callback = 0u; callback < 4u; callback++) {
        BindingsOwnedSyntaxVisitor missing = visitor;
        switch (callback) {
        case 0u: missing.span = NULL; break;
        case 1u: missing.arena = NULL; break;
        case 2u: missing.bindings = NULL; break;
        default: missing.atom = NULL; break;
        }
        Inspection unused = {0};
        CHECK(!bindings_value_visit_owned_syntax(provider(value), &missing, &unused) &&
              unused.calls == 0u, "all callbacks are mandatory before inspection starts");
    }
    CHECK(!bindings_value_visit_owned_syntax(NULL, &visitor, &inspection) &&
          !bindings_value_visit_owned_syntax(provider(value), NULL, &inspection),
          "null owner and visitor refuse");
    arena_free(&source); arena_free(&saved);
}

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols; g_hashcons = NULL;
    VarInternTable variables;
    var_intern_init(&variables); g_var_intern = &variables;
    saved_value_survives_and_ledgers_alias();
    nested_saved_values_and_stop_controls();
    unknown_hold_and_cycle_controls();
    descriptor_and_callback_controls();
    bindings_thread_cache_free();
    g_var_intern = NULL; var_intern_free(&variables);
    g_symbols = NULL; symbol_table_free(&symbols);
    printf("CapturedOwnerVisit checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}

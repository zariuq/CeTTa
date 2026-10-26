#include "parser.h"
#include "prime_regular_kernel.h"
#include "prime_scoped_judgments.h"
#include "prime_semantics.h"
#include "prime_typing_authority.h"
#include "space.h"
#include "stats.h"
#include "symbol.h"
#include "term_universe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned failures;
static unsigned checks;

static void retain_identifier(void *raw) { (*(unsigned *)raw)++; }
static void release_identifier(void *raw) { (*(unsigned *)raw)--; }

static Atom *parse_one(Arena *arena, const char *text) {
    Atom **forms = NULL;
    int count = parse_metta_text(text, arena, &forms);
    Atom *result = count == 1 && forms ? forms[0] : NULL;
    free(forms);
    return result;
}

static Atom *admission_record(Arena *arena, const char *name, Atom *payload) {
    return atom_expr(arena, (Atom *[]){atom_symbol(arena, "type:rule"),
        atom_symbol(arena, name), atom_int(arena, 0), atom_unit(arena), payload}, 5u);
}

static void expect_admission(Arena *arena, Space *space, Atom *record,
                              bool expected, const char *label) {
    checks++;
    if (prime_scoped_judgment_admitted(arena, space, record) != expected) {
        failures++;
        fprintf(stderr, "FAIL: %s\n", label);
    }
}

static size_t authority_storage_bytes(void) {
    CettaStorageCensus census = {0};
    prime_scoped_judgment_storage_census(&census);
    prime_semantics_theorem_storage_census(&census);
    cetta_storage_finish(&census);
    checks++;
    if (census.incomplete || census.invalidated || census.overflow) {
        failures++;
        fprintf(stderr, "FAIL: authority storage census is incomplete\n");
    }
    size_t bytes = census.bytes;
    cetta_storage_census_free(&census);
    return bytes;
}

static void expect_instance_authority(uint64_t instance, bool admitted,
                                       bool theorems, const char *label) {
    size_t visited = 0u;
    bool empty = false;
    checks++;
    if (!prime_scoped_judgment_instance_empty(instance, SIZE_MAX, &visited, &empty) ||
        empty == admitted) {
        failures++;
        fprintf(stderr, "FAIL: %s (admissions)\n", label);
    }
    checks++;
    if (!prime_semantics_theorem_instance_empty(instance, SIZE_MAX, &visited, &empty) ||
        empty == theorems) {
        failures++;
        fprintf(stderr, "FAIL: %s (theorems)\n", label);
    }
}

static void check_exact_admission(Arena *arena, TermUniverse *universe) {
    Space source, destination;
    space_init_with_universe(&source, universe);
    space_init_with_universe(&destination, universe);
    Atom *original = admission_record(arena, "exactAdmission",
                                       atom_symbol(arena, "True"));
    Atom *forged = admission_record(arena, "exactAdmission", atom_bool(arena, true));
    checks++;
    if (strcmp(atom_to_string(arena, original), atom_to_string(arena, forged)) != 0) {
        failures++;
        fprintf(stderr, "FAIL: admission collision control must have identical printed forms\n");
    }
    space_add(&source, original);
    prime_scoped_judgment_admit(arena, &source, original);
    expect_admission(arena, &source, original, true, "the published exact record is admitted");
    expect_admission(arena, &source, atom_deep_copy(arena, original), true,
                     "an independent exact copy retains admission");
    size_t request_bytes = arena->live_bytes;
    for (unsigned i = 0u; i < 100u; i++)
        expect_admission(arena, &source, original, true,
                         "repeated admission reads retain exact membership");
    checks++;
    if (arena->live_bytes != request_bytes) {
        failures++;
        fprintf(stderr, "FAIL: admission lookup allocates printed records in the request arena\n");
    }
    expect_admission(arena, &source, forged, false,
                     "identical printed forms do not confer admission");
    expect_admission(arena, &destination, original, false,
                     "a record copied into another authority space is not admitted there");

    /* The index uses the head and name, so all these records deliberately
     * collide; payload equality remains the membership decision. */
    for (unsigned i = 0u; i < 200u; i++) {
        Atom *record = admission_record(arena, "collidingAdmission", atom_int(arena, i));
        space_add(&source, record);
        prime_scoped_judgment_admit(arena, &source, record);
    }
    for (unsigned i = 0u; i < 200u; i++)
        expect_admission(arena, &source,
            admission_record(arena, "collidingAdmission", atom_int(arena, i)), true,
            "table growth preserves distinct colliding exact records");
    expect_admission(arena, &source,
        admission_record(arena, "collidingAdmission", atom_int(arena, 200)), false,
        "an unadmitted payload in a populated collision bucket remains refused");

    Arena request;
    arena_init_detached(&request);
    Atom *temporary = admission_record(&request, "requestAdmission", atom_int(&request, 17));
    space_add(&source, temporary);
    prime_scoped_judgment_admit(&request, &source, temporary);
    temporary->expr.elems[4] = atom_int(&request, 18);
    expect_admission(arena, &source,
        admission_record(arena, "requestAdmission", atom_int(arena, 17)), true,
        "admission owns the original key rather than a mutable request alias");
    expect_admission(arena, &source, temporary, false,
        "mutating the request record does not mutate its admitted key");
    arena_free(&request);
    expect_admission(arena, &source,
        admission_record(arena, "requestAdmission", atom_int(arena, 17)), true,
        "admitted keys outlive the request arena");

    Atom *handle = admission_record(arena, "opaqueAdmission", atom_space(arena, &source));
    prime_scoped_judgment_admit(arena, &source, handle);
    expect_admission(arena, &source, handle, false,
                     "runtime handles cannot become immutable admission keys");
    unsigned identifier_holds = 0u;
    arena_init_detached(&request);
    Atom *identifier = atom_native_handle_identifier(&request, 19,
        &identifier_holds, retain_identifier, release_identifier);
    Atom *owned_integer = admission_record(&request, "ownedIntegerAdmission", identifier);
    prime_scoped_judgment_admit(&request, &source, owned_integer);
    expect_admission(arena, &source, owned_integer, false,
                     "integer-shaped native holders cannot become admission keys");
    expect_admission(arena, &source,
        admission_record(arena, "ownedIntegerAdmission", atom_int(arena, 19)), false,
        "an ordinary integer cannot impersonate an owned native identifier");
    arena_free(&request);
    checks++;
    if (identifier_holds != 0u) {
        failures++;
        fprintf(stderr, "FAIL: rejected admission retained a native identifier owner\n");
    }
    Atom *cycle = admission_record(arena, "cyclicAdmission", atom_unit(arena));
    cycle->expr.elems[4] = cycle;
    prime_scoped_judgment_admit(arena, &source, cycle);
    expect_admission(arena, &source, cycle, false,
                     "cyclic data cannot become an admission key");
    space_replace_contents(&destination, &source);
    expect_admission(arena, &destination, original, true,
                     "admission follows the actual space-contents transfer");
    expect_admission(arena, &destination, forged, false,
                     "contents transfer preserves exact admission distinctions");
    expect_admission(arena, &destination,
        admission_record(arena, "requestAdmission", atom_int(arena, 17)), true,
        "contents transfer keeps owned request-independent keys");
    space_free(&destination);
    space_free(&source);
}

static void check_admission_revocation(Arena *arena, TermUniverse *universe) {
    Space withdrawn;
    space_init_with_universe(&withdrawn, universe);
    Atom *digest_verdict = prime_scoped_judgment_judge(arena, &withdrawn,
        parse_one(arena, "(set:signature-digest)"), false, 0u);
    Atom *signature = digest_verdict->expr.elems[3]->expr.elems[1];
    Atom *record = atom_expr(arena, (Atom *[]){atom_symbol(arena, "set:known"),
        atom_symbol(arena, "withdrawnAdmission"), atom_symbol(arena, "MissingAdmissionProposition"),
        atom_symbol(arena, "axiom"), atom_unit(arena), atom_unit(arena),
        atom_int(arena, 0), signature}, 8u);
    space_add(&withdrawn, record);
    prime_scoped_judgment_admit(arena, &withdrawn, record);
    expect_admission(arena, &withdrawn, record, true,
                     "revocation control starts with an admitted exact artifact");
    Space *working = space_heap_clone_shallow(&withdrawn);
    checks++;
    if (!working) {
        failures++;
        fprintf(stderr, "FAIL: could not clone the admitted source for revocation\n");
        space_free(&withdrawn);
        return;
    }
    expect_admission(arena, working, record, true,
                     "the actual working-space clone receives admission");
    CettaNikDirectAuthorityTokenV1 before_withdrawal;
    const uint32_t token_policy = UINT32_C(0x54455354);
    checks++;
    if (!cetta_prime_typing_direct_authority_token_v1(
            working, token_policy, &before_withdrawal) ||
        !cetta_prime_typing_direct_authority_token_v1_is_current(
            &before_withdrawal, working, token_policy)) {
        failures++;
        fprintf(stderr, "FAIL: could not pin a stable admission authority\n");
    }
    uint64_t before_revision = space_revision(working);
    uint64_t before_mutation = space_global_mutation_epoch();
    (void)prime_scoped_judgment_judge(arena, working,
        parse_one(arena, "(set:recheck withdrawnAdmission)"), false, 0u);
    expect_admission(arena, working, record, false,
                     "failed rechecking withdraws that exact artifact");
    checks++;
    if (space_revision(working) != before_revision ||
        space_global_mutation_epoch() != before_mutation) {
        failures++;
        fprintf(stderr, "FAIL: withdrawal control changed syntax as well as admission\n");
    }
    checks++;
    if (cetta_prime_typing_direct_authority_token_v1_is_current(
            &before_withdrawal, working, token_policy)) {
        failures++;
        fprintf(stderr, "FAIL: a withdrawn admission retained its authority token\n");
    }
    CettaNikDirectAuthorityTokenV1 after_withdrawal;
    checks++;
    if (!cetta_prime_typing_direct_authority_token_v1(
            working, token_policy, &after_withdrawal) ||
        !cetta_prime_typing_direct_authority_token_v1_is_current(
            &after_withdrawal, working, token_policy)) {
        failures++;
        fprintf(stderr, "FAIL: withdrawal prevented a fresh authority observation\n");
    }
    expect_admission(arena, &withdrawn, record, true,
                     "an uncommitted working-space withdrawal does not change its parent");
    space_replace_contents(&withdrawn, working);
    space_free(working);
    free(working);
    expect_admission(arena, &withdrawn, record, false,
                     "committing a withdrawn source key replaces the parent's older admission");
    size_t before_growth = authority_storage_bytes(), grown = 0u;
    bool empty = false;
    /* These distinct colliding keys grow this instance's own table, including
     * the withdrawn entry, rather than an unrelated instance's table. */
    for (size_t i = 0u; i < 200u; i++) {
        Atom *item = admission_record(arena, "admissionGrowth", atom_int(arena, (int64_t)i));
        space_add(&withdrawn, item);
        prime_scoped_judgment_admit(arena, &withdrawn, item);
    }
    size_t after_growth = authority_storage_bytes();
    checks++;
    if (after_growth <= before_growth) {
        failures++;
        fprintf(stderr, "FAIL: revocation control did not actually grow the admission table\n");
    }
    expect_admission(arena, &withdrawn, record, false,
                     "table growth cannot restore withdrawn admission");
    checks++;
    if (!prime_scoped_judgment_instance_empty(space_instance_id(&withdrawn), SIZE_MAX,
            &grown, &empty) || empty) {
        failures++;
        fprintf(stderr, "FAIL: growth discarded retained revoked-authority storage\n");
    }
    prime_scoped_judgment_admit(arena, &withdrawn, record);
    expect_admission(arena, &withdrawn, record, true,
                     "fresh publication can explicitly restore the exact withdrawn artifact");
    space_free(&withdrawn);
}

static void publish_lifetime_records(Arena *arena, Space *space, Atom *record) {
    space_add(space, record);
    prime_scoped_judgment_admit(arena, space, record);
    Atom *name = atom_symbol(arena, "lifetimeTheorem");
    Atom *type = atom_symbol(arena, "lifetimeProposition");
    Atom *context = parse_one(arena, "(PrimeTheoremContextV1)");
    /* Exercise the trusted publication storage API, not a new proof of the
     * deliberately uninterpreted proposition above. */
    prime_semantics_theorem_record(space, name, type, &context, 1u);
}

static void check_admission_lifetimes(Arena *arena, TermUniverse *universe) {
    size_t baseline = authority_storage_bytes();
    Space source;
    space_init_with_universe(&source, universe);
    uint64_t original_instance = space_instance_id(&source);
    Atom *key = admission_record(arena, "lifetimeAdmission", atom_int(arena, 23));
    publish_lifetime_records(arena, &source, key);
    Space *left = space_heap_clone_shallow(&source);
    Space *right = space_heap_clone_shallow(&source);
    checks++;
    if (!left || !right) {
        failures++;
        fprintf(stderr, "FAIL: lifetime control could not clone its source\n");
        if (left) { space_free(left); free(left); }
        if (right) { space_free(right); free(right); }
        space_free(&source);
        return;
    }
    size_t visited = 0u;
    bool empty = true;
    checks++;
    if (prime_scoped_judgment_instance_empty(original_instance, 0u, &visited, &empty) ||
        visited || empty) {
        failures++;
        fprintf(stderr, "FAIL: a zero-budget authority probe completed a populated bucket\n");
    }
    space_free(&source);
    expect_instance_authority(original_instance, false, false,
        "freeing the source retires both of its authority ledgers");
    expect_admission(arena, left, key, true, "left clone keeps the retired source's immutable key");
    expect_admission(arena, right, key, true, "right clone keeps an independent admission");
    expect_instance_authority(space_instance_id(right), true, true,
        "clones retain their own theorem context storage");
    uint64_t left_instance = space_instance_id(left);
    space_free(left);
    free(left);
    expect_instance_authority(left_instance, false, false,
        "cancelling one clone retires only that clone's ledgers");
    expect_admission(arena, right, key, true, "cancelling a peer preserves the remaining key");
    expect_instance_authority(space_instance_id(right), true, true,
        "cancelling a peer preserves the remaining theorem contexts");
    space_free(right);
    free(right);
    size_t released = authority_storage_bytes();
    checks++;
    if (released != baseline) {
        failures++;
        fprintf(stderr, "FAIL: last clone retained authority storage (%zu != %zu)\n", released, baseline);
    }

    /* A replacement preserves destination history even when the source has
     * no such key. The moved-from old instance ends at the actual move. */
    Space destination, replacement;
    space_init_with_universe(&destination, universe);
    space_init_with_universe(&replacement, universe);
    uint64_t destination_instance = space_instance_id(&destination);
    uint64_t replacement_instance = space_instance_id(&replacement);
    Atom *other = admission_record(arena, "otherLifetimeAdmission", atom_int(arena, 31));
    publish_lifetime_records(arena, &destination, key);
    publish_lifetime_records(arena, &replacement, other);
    space_replace_contents(&destination, &replacement);
    checks++;
    if (space_instance_id(&destination) != destination_instance ||
        space_instance_id(&replacement) == replacement_instance) {
        failures++;
        fprintf(stderr, "FAIL: storage replacement changed the destination or retained the old source identity\n");
    }
    expect_instance_authority(replacement_instance, false, false,
        "a moved-from old instance is retired immediately");
    expect_instance_authority(space_instance_id(&replacement), false, false,
        "a moved-from fresh instance starts without authority");
    expect_admission(arena, &destination, key, true,
        "unmentioned destination admission survives storage replacement");
    expect_admission(arena, &destination, other, true,
        "moved source key is retained before source retirement");
    expect_instance_authority(destination_instance, true, true,
        "destination retains the moved theorem contexts");
    space_free(&replacement);
    /* Import the accumulated keys back through a separate logical instance.
     * Entries retain syntax collections, never the other instance itself. */
    Space peer;
    space_init_with_universe(&peer, universe);
    publish_lifetime_records(arena, &peer, other);
    Space *copy = space_heap_clone_shallow(&destination);
    bool copied_both = false;
    checks++;
    if (copy) {
        space_replace_contents(&peer, copy);
        space_free(copy);
        free(copy);
        copy = space_heap_clone_shallow(&peer);
        if (copy) {
            copied_both = true;
            space_replace_contents(&destination, copy);
            space_free(copy);
            free(copy);
        }
    }
    if (!copied_both) {
        failures++;
        fprintf(stderr, "FAIL: mutual-import lifetime control could not clone its peer\n");
    }
    space_free(&destination);
    expect_admission(arena, &peer, key, true, "mutual imports retain a surviving peer's first key");
    expect_admission(arena, &peer, other, true, "mutual imports retain a surviving peer's second key");
    space_free(&peer);
    released = authority_storage_bytes();
    checks++;
    if (released != baseline) {
        failures++;
        fprintf(stderr, "FAIL: mutual imports retained an authority owner cycle\n");
    }

    /* Repeated completed agent scopes cannot accumulate retired identities,
     * immutable key arenas, theorem contexts or lookup capacity. */
    for (unsigned i = 0u; i < 128u; i++) {
        Space turn;
        space_init_with_universe(&turn, universe);
        uint64_t turn_instance = space_instance_id(&turn);
        publish_lifetime_records(arena, &turn, key);
        space_free(&turn);
        expect_instance_authority(turn_instance, false, false,
            "completed agent scopes leave no authority for retired identities");
        released = authority_storage_bytes();
        checks++;
        if (released != baseline) {
            failures++;
            fprintf(stderr, "FAIL: completed agent turn %u retained authority storage\n", i);
        }
    }
    Space cohort[96];
    for (size_t i = 0u; i < 96u; i++) {
        space_init_with_universe(&cohort[i], universe);
        publish_lifetime_records(arena, &cohort[i], key);
    }
    for (size_t i = 0u; i < 96u; i += 2u) space_free(&cohort[i]);
    for (size_t i = 1u; i < 96u; i += 2u)
        expect_admission(arena, &cohort[i], key, true,
            "retirement and directory rehash preserve surviving instances");
    for (size_t i = 1u; i < 96u; i += 2u) space_free(&cohort[i]);
    released = authority_storage_bytes();
    checks++;
    if (released != baseline) {
        failures++;
        fprintf(stderr, "FAIL: directory growth and retirement retained authority capacity\n");
    }
#if CETTA_BUILD_WITH_RUNTIME_STATS
    bool was_enabled = cetta_runtime_stats_is_enabled();
    cetta_runtime_stats_enable();
    CettaRuntimeStats before, moved, retired;
    cetta_runtime_stats_snapshot(&before);
    Space counted_destination, counted_source;
    space_init_with_universe(&counted_destination, universe);
    space_init_with_universe(&counted_source, universe);
    space_replace_contents(&counted_destination, &counted_source);
    cetta_runtime_stats_snapshot(&moved);
    checks++;
    if (moved.counters[CETTA_RUNTIME_COUNTER_SPACE_LIVE] !=
            before.counters[CETTA_RUNTIME_COUNTER_SPACE_LIVE] + 2u ||
        moved.counters[CETTA_RUNTIME_COUNTER_SPACE_STRUCT_BYTES] !=
            before.counters[CETTA_RUNTIME_COUNTER_SPACE_STRUCT_BYTES] + 2u * sizeof(Space)) {
        failures++;
        fprintf(stderr, "FAIL: replacing storage retired a still-live space from the resource gauge\n");
    }
    space_free(&counted_source);
    space_free(&counted_destination);
    cetta_runtime_stats_snapshot(&retired);
    checks++;
    if (retired.counters[CETTA_RUNTIME_COUNTER_SPACE_LIVE] !=
            before.counters[CETTA_RUNTIME_COUNTER_SPACE_LIVE] ||
        retired.counters[CETTA_RUNTIME_COUNTER_SPACE_STRUCT_BYTES] !=
            before.counters[CETTA_RUNTIME_COUNTER_SPACE_STRUCT_BYTES]) {
        failures++;
        fprintf(stderr, "FAIL: true space retirement did not balance the resource gauge\n");
    }
    if (!was_enabled) cetta_runtime_stats_disable();
#endif
}

static void expect_judgment_status(Arena *arena, Space *space, Atom *query,
                                   const char *label, bool limited,
                                   uint64_t steps, const char *status) {
    checks++;
    Atom *verdict = query ? prime_scoped_judgment_judge(
        arena, space, query, limited, steps) : NULL;
    bool matches = verdict && verdict->kind == ATOM_EXPR &&
        verdict->expr.len == 4u &&
        atom_is_symbol(verdict->expr.elems[0], "PrimeVerdict") &&
        atom_is_symbol(verdict->expr.elems[1], status);
    if (!matches) {
        fprintf(stderr, "FAIL: expected %s (limited=%d, steps=%llu): %s\n",
                status, limited, (unsigned long long)steps, label);
        if (verdict) atom_print(verdict, stderr);
        fputc('\n', stderr);
        failures++;
    }
}

static void expect_status(Arena *arena, Space *space, const char *text,
                          bool limited, uint64_t steps, const char *status) {
    expect_judgment_status(
        arena, space, parse_one(arena, text), text, limited, steps, status);
}

static void check_request_cache_lifetime(Arena *persistent, Space *space) {
    space_add(space, parse_one(persistent, "(: cacheFunction (-> Field set))"));
    Arena request;
    arena_init_detached(&request);
    ArenaMark start = arena_mark(&request);
    VarInternTable *saved_variables = g_var_intern;
    g_var_intern = NULL;
    const uint64_t revision = space_revision(space);
    expect_status(&request, space,
        "(set:proves (pf:fix x (pf:all-elim (pf:known universeIn) (cacheFunction x)))"
        " (all Field (lam x (In (cacheFunction x) (UnivOf (cacheFunction x))))))",
        false, 0u, "Established");
    arena_reset(&request, start);
    /* Reuse the source arena, so a borrowed cached Atom is visibly wrong
     * even when resetting the arena does not return its pages to malloc. */
    for (unsigned i = 0u; i < 4096u; i++) (void)atom_int(&request, (int64_t)i);
    expect_status(&request, space,
        "(set:define cacheApply (-> (-> Field Field) Field Field)"
        " (= (cacheApply $f $x) ($f $x)))",
        false, 0u, "Established");
    checks++;
    if (space_revision(space) != revision) {
        failures++;
        fprintf(stderr, "FAIL: cache lifetime checking changed the authored theory\n");
    }
    g_var_intern = saved_variables;
    arena_free(&request);
}

static void check_retained_formation(Arena *arena, Space *space) {
    Atom *type = parse_one(arena, "(-> (A : (u 0)) (x : A) A)");
    Atom *query = atom_expr(arena, (Atom *[]){atom_symbol(arena, "type:formed"), type}, 2u);
    Atom *canonical = NULL;
    CettaPrimeTypingResourceObservationV1 resources;
    const uint64_t allowance = 1u << 20u;
    Atom *verdict = prime_semantics_judge_typing_accounted(
        arena, space, query, true, allowance, &resources, &canonical);
    Atom *expected = parse_one(arena,
        "(Pi (Sort (LevelConst 0)) (Pi (idx 0) (idx 1)))");
    checks++;
    if (!verdict || !atom_is_symbol(verdict->expr.elems[1], "Established") ||
        !canonical || !atom_eq(canonical, expected) ||
        resources.initial != allowance || resources.spent == 0u ||
        resources.remaining != allowance - resources.spent) {
        failures++;
        fprintf(stderr, "FAIL: accounted formation lost its checked telescope or resource receipt\n");
    }
    const char *obstructions[] = {
        "(type:formed (-> MissingType MissingType))",
        "(type:formed (u -1))",
    };
    const char *statuses[] = {"Undetermined", "Refuted"};
    for (size_t index = 0u; index < 2u; index++) {
        canonical = expected;
        verdict = prime_semantics_judge_typing_accounted(
            arena, space, parse_one(arena, obstructions[index]), true, allowance,
            &resources, &canonical);
        checks++;
        if (!verdict || !atom_is_symbol(verdict->expr.elems[1], statuses[index]) || canonical) {
            failures++;
            fprintf(stderr, "FAIL: %s formation retained a fabricated checked type\n", statuses[index]);
        }
    }
    canonical = expected;
    verdict = prime_semantics_judge_typing_accounted(
        arena, space, query, true, 1u, &resources, &canonical);
    checks++;
    if (!verdict || !atom_is_symbol(verdict->expr.elems[1], "Incomplete") ||
        canonical || resources.spent != 1u || resources.remaining != 0u) {
        failures++;
        fprintf(stderr, "FAIL: exhausted formation exported an unchecked telescope\n");
    }
    canonical = expected;
    verdict = prime_semantics_judge_typing_accounted(
        arena, space, query, true, 0u, &resources, &canonical);
    checks++;
    if (verdict || canonical || resources.spent != 0u) {
        failures++;
        fprintf(stderr, "FAIL: zero allowance replayed or retained formation\n");
    }
    CettaPrimeTypingCheckingCandidateV1 candidate = {
        .term = parse_one(arena, "(lam A (lam x x))"),
        .expected_type = type,
    };
    CettaPrimeTypingCheckingObservationV1 observation;
    checks++;
    if (!cetta_prime_typing_observe_checking_v1(arena, space, &candidate, &observation) ||
        observation.authority.result.value.outcome != CETTA_NIK_OUTCOME_ESTABLISHED ||
        !observation.authority.canonical_term ||
        !atom_eq(observation.authority.canonical_term, parse_one(arena, "(Lam (Lam (idx 0)))"))) {
        failures++;
        fprintf(stderr, "FAIL: dependent identity checking did not retain the actual lambda telescope\n");
    }
    candidate.term = parse_one(arena, "(lam A (lam x A))");
    checks++;
    if (!cetta_prime_typing_observe_checking_v1(arena, space, &candidate, &observation) ||
        observation.authority.result.value.outcome != CETTA_NIK_OUTCOME_REFUTED ||
        observation.authority.canonical_term) {
        failures++;
        fprintf(stderr, "FAIL: dependent identity confused its type parameter with its value\n");
    }
}

static bool judgment_is_established(Arena *arena, Space *space, Atom *query,
                                    uint64_t steps) {
    Atom *verdict = prime_scoped_judgment_judge(
        arena, space, query, true, steps);
    return verdict && verdict->kind == ATOM_EXPR && verdict->expr.len == 4u &&
        atom_is_symbol(verdict->expr.elems[0], "PrimeVerdict") &&
        atom_is_symbol(verdict->expr.elems[1], "Established");
}

static uint64_t minimum_judgment_steps(Arena *arena, Space *space,
                                       Atom *query) {
    uint64_t upper = 1u;
    while (upper <= (1u << 20u) &&
           !judgment_is_established(arena, space, query, upper))
        upper *= 2u;
    if (upper > (1u << 20u)) return 0u;
    uint64_t lower = 0u;
    while (lower + 1u < upper) {
        uint64_t middle = lower + (upper - lower) / 2u;
        if (judgment_is_established(arena, space, query, middle))
            upper = middle;
        else
            lower = middle;
    }
    return upper;
}

static void check_case_compilation_allowance(Arena *arena, TermUniverse *universe) {
    Space theory;
    space_init_with_universe(&theory, universe);
    Atom *decl = prime_scoped_judgment_judge(arena, &theory, parse_one(arena,
        "(set:inductive BudgetNat (u 0) (: budgetZero BudgetNat)"
        " (: budgetSucc (-> BudgetNat BudgetNat)))"), false, 0u);
    Atom *publication = decl && decl->kind == ATOM_EXPR && decl->expr.len == 4u
        ? decl->expr.elems[3] : NULL;
    checks++;
    if (!publication || publication->kind != ATOM_EXPR ||
        publication->expr.len < 2u ||
        !atom_is_symbol(publication->expr.elems[0], "SetPublish")) {
        failures++;
        fprintf(stderr, "FAIL: case-budget control could not declare its datatype\n");
        space_free(&theory);
        return;
    }
    for (CettaExprIndex i = 1u; i < publication->expr.len; i++) {
        Atom *entry = publication->expr.elems[i];
        space_add(&theory, entry);
        prime_scoped_judgment_admit(arena, &theory, entry);
    }
    Atom *query = parse_one(arena,
        "(set:define budgetMin (-> BudgetNat BudgetNat BudgetNat)"
        " (= (budgetMin budgetZero $n) budgetZero)"
        " (= (budgetMin (budgetSucc $m) budgetZero) budgetZero)"
        " (= (budgetMin (budgetSucc $m) (budgetSucc $n))"
        "    (budgetSucc (budgetMin $m $n))))");
    uint64_t revision = space_revision(&theory);
    uint64_t needed = minimum_judgment_steps(arena, &theory, query);
    checks++;
    if (needed < 2u) {
        failures++;
        fprintf(stderr, "FAIL: case compilation has no finite shared allowance\n");
    } else {
        expect_judgment_status(arena, &theory, query,
            "case compilation zero allowance", true, 0u, "Incomplete");
        expect_judgment_status(arena, &theory, query,
            "case compilation below shared allowance", true, needed - 1u, "Incomplete");
        expect_judgment_status(arena, &theory, query,
            "case compilation exact shared allowance", true, needed, "Established");
        expect_judgment_status(arena, &theory, query,
            "case compilation increased allowance", true, needed + 7u, "Established");
    }
    checks++;
    if (space_revision(&theory) != revision) {
        failures++;
        fprintf(stderr, "FAIL: case compilation checking published into its source\n");
    }
    space_free(&theory);
}

static bool kernel_synthesis_is_established(
    Arena *arena, Atom *context, Atom *rules, Atom *term, uint64_t steps) {
    CettaPrimeRegularKernelBudget budget;
    cetta_prime_regular_kernel_budget_init(&budget, true, steps);
    cetta_prime_regular_kernel_rules_set(rules);
    CettaPrimeRegularKernelResult result =
        cetta_prime_regular_kernel_synth_intrinsic_v1(
            arena, context, term, &budget);
    cetta_prime_regular_kernel_rules_set(NULL);
    return result.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED;
}

static uint64_t minimum_kernel_synthesis_steps(
    Arena *arena, Atom *context, Atom *rules, Atom *term) {
    uint64_t upper = 1u;
    while (upper <= (1u << 20u) &&
           !kernel_synthesis_is_established(
               arena, context, rules, term, upper))
        upper *= 2u;
    if (upper > (1u << 20u)) return 0u;
    uint64_t lower = 0u;
    while (lower + 1u < upper) {
        uint64_t middle = lower + (upper - lower) / 2u;
        if (kernel_synthesis_is_established(
                arena, context, rules, term, middle))
            upper = middle;
        else
            lower = middle;
    }
    return upper;
}

/* The recursor's declared type is a schema over its motive's level, one
 * level variable; its lowest instance puts that level at 0. */
static Atom *lowest_level_instance(Arena *arena, Atom *type) {
    if (!type) return NULL;
    if (type->kind == ATOM_VAR) return atom_int(arena, 0);
    if (type->kind != ATOM_EXPR) return type;
    Atom **items = arena_alloc(arena, sizeof(Atom *) * (size_t)type->expr.len);
    for (CettaExprIndex i = 0u; i < type->expr.len; i++)
        items[i] = lowest_level_instance(arena, type->expr.elems[i]);
    return atom_expr(arena, items, type->expr.len);
}

/* Measure the actual formation checks selected by the generated declaration
 * inventory. The largest individual allowance must not admit their batch;
 * the batch needs their sum, with no refreshed allowance between checks.
 * The recursor is formed at its lowest instance, then declared at its
 * schema and checked against that instance, which forms the schema at every
 * level of its motive. */
static void check_inductive_shared_allowance(Arena *arena, Space *space) {
    Atom *query = parse_one(arena,
        "(set:inductive BudgetTree (u 0)"
        " (: BudgetLeaf BudgetTree) (: BudgetNode (-> Field BudgetTree)))");
    Atom *verdict = prime_scoped_judgment_judge(
        arena, space, query, false, 0u);
    Atom *published = verdict && verdict->kind == ATOM_EXPR &&
            verdict->expr.len == 4u
        ? verdict->expr.elems[3] : NULL;
    if (!published || published->kind != ATOM_EXPR ||
        !atom_is_symbol(published->expr.elems[0], "SetPublish")) {
        checks++;
        failures++;
        fprintf(stderr, "FAIL: no generated BudgetTree inventory\n");
        return;
    }
    Space view;
    space_init_overlay(&view, space);
    Atom *signature_verdict = prime_scoped_judgment_judge(
        arena, space, parse_one(arena, "(set:signature)"), false, 0u);
    Atom *signature = signature_verdict->expr.elems[3]->expr.elems[1];
    for (CettaExprIndex i = 0u; i < signature->expr.len; i++)
        space_add(&view, signature->expr.elems[i]);
    Atom *principle = NULL;
    uint64_t total = 0u, largest = 0u;
    unsigned stages = 0u;
    Atom *recursor_entry = NULL;
    Atom *recursor_lowest = NULL;
    bool recursor_declared = false;
    for (CettaExprIndex i = 1u; i <= published->expr.len; i++) {
        Atom *entry = i < published->expr.len ? published->expr.elems[i] : NULL;
        Atom *check = NULL;
        bool add_after = false;
        if (!entry && recursor_entry && !recursor_declared) {
            /* The recursor declared at its schema, checked at its lowest
             * instance; the entry is visited again for the principle. */
            space_add(&view, recursor_entry);
            recursor_declared = true;
            check = atom_expr(arena, (Atom *[]){
                atom_symbol(arena, "type:check"), recursor_entry->expr.elems[1],
                recursor_lowest}, 3u);
            i--;
        } else if (!entry) {
            if (principle)
                check = atom_expr(arena, (Atom *[]){
                    atom_symbol(arena, "type:check"), principle,
                    atom_symbol(arena, "prop")}, 3u);
        } else if (entry->kind == ATOM_EXPR && entry->expr.len == 8u &&
                   atom_is_symbol(entry->expr.elems[0], "set:known") &&
                   atom_is_symbol(entry->expr.elems[1], "BudgetTree-ind")) {
            principle = entry->expr.elems[2];
        } else if (entry->kind == ATOM_EXPR && entry->expr.len == 3u &&
                   atom_is_symbol(entry->expr.elems[0], ":")) {
            bool family = atom_is_symbol(entry->expr.elems[1], "BudgetTree");
            bool recursor = atom_is_symbol(entry->expr.elems[1], "BudgetTree-rec");
            if (recursor) {
                recursor_entry = entry;
                recursor_lowest = lowest_level_instance(arena, entry->expr.elems[2]);
            }
            check = family || recursor
                ? atom_expr(arena, (Atom *[]){atom_symbol(arena, "type:formed"),
                                            recursor ? recursor_lowest
                                                     : entry->expr.elems[2]}, 2u)
                : atom_expr(arena, (Atom *[]){atom_symbol(arena, "type:check"),
                                            entry->expr.elems[2],
                                            query->expr.elems[2]}, 3u);
            add_after = !recursor;
        }
        if (!check) continue;
        CettaPrimeTypingResourceObservationV1 resources;
        const uint64_t allowance = 1u << 20u;
        Atom *checked = prime_semantics_judge_typing_accounted(
            arena, &view, check, true, allowance, &resources, NULL);
        stages++;
        checks++;
        if (!checked || !atom_is_symbol(checked->expr.elems[1], "Established") ||
            !resources.limited || resources.initial != allowance ||
            resources.spent == 0u || resources.spent > allowance ||
            resources.remaining != allowance - resources.spent) {
            failures++;
            fprintf(stderr, "FAIL: nonconservative datatype formation account\n");
        }
        total += resources.spent;
        if (resources.spent > largest) largest = resources.spent;
        if (add_after) space_add(&view, entry);
    }
    space_free(&view);
    uint64_t required = minimum_judgment_steps(arena, space, query);
    checks++;
    if (stages != 6u || required != total || total <= largest) {
        failures++;
        fprintf(stderr,
                "FAIL: datatype batch account (stages=%u sum=%llu max=%llu required=%llu)\n",
                stages, (unsigned long long)total, (unsigned long long)largest,
                (unsigned long long)required);
    }
    expect_judgment_status(arena, space, query, "datatype zero allowance",
                           true, 0u, "Incomplete");
    expect_judgment_status(arena, space, query, "datatype one allowance",
                           true, 1u, "Incomplete");
    expect_judgment_status(arena, space, query, "datatype individual maximum",
                           true, largest, "Incomplete");
    expect_judgment_status(arena, space, query, "datatype just below total",
                           true, total - 1u, "Incomplete");
    expect_judgment_status(arena, space, query, "datatype total allowance",
                           true, total, "Established");
    expect_judgment_status(arena, space, query, "datatype larger allowance",
                           true, total + 1u, "Established");
}

static void expect_rule_inventory(Arena *arena, Space *space,
                                  const char *expected, const char *label) {
    checks++;
    Atom *actual = prime_semantics_kernel_rules(arena, space);
    Atom *wanted = parse_one(arena, expected);
    if (!actual || !wanted || !atom_eq(actual, wanted)) {
        failures++;
        fprintf(stderr, "FAIL: %s\n", label);
        if (actual) atom_print(actual, stderr);
        fputc('\n', stderr);
    }
}

static void check_overlay_rule_inventory(Arena *arena, TermUniverse *universe) {
    Space base, view, nested;
    space_init_with_universe(&base, universe);
    Atom *inherited = parse_one(arena,
        "(type:rule inherited 0 () (DeclConst carrier))");
    space_add(&base, inherited);
    prime_scoped_judgment_admit(arena, &base, inherited);
    /* A rule-shaped atom that admission never published. */
    space_add(&base, parse_one(arena,
        "(type:rule forged 0 () (DeclConst carrier))"));
    space_add(&base, parse_one(arena, "(: irrelevant (u 0))"));
    space_init_overlay(&view, &base);
    Atom *late = parse_one(arena,
        "(type:rule late 0 () (DeclConst invisible))");
    space_add(&base, late);
    prime_scoped_judgment_admit(arena, &base, late);
    space_add(&view, parse_one(arena,
        "(type:rule local 1 ((PVar 0)) (PVar 0))"));
    expect_rule_inventory(arena, &view,
        "(LCons (PrimeRule inherited 0 () (DeclConst carrier)) LNil)",
        "an overlay views admitted inherited rules; later base additions, forged rules and its own rules stay out");
    space_init_overlay(&nested, &view);
    space_add(&nested, parse_one(arena,
        "(type:rule deepest 0 () (DeclConst local))"));
    expect_rule_inventory(arena, &nested,
        "(LCons (PrimeRule inherited 0 () (DeclConst carrier)) LNil)",
        "nested overlays view the same admitted rules");
    space_remove(&nested, inherited);
    checks++;
    if (prime_semantics_kernel_rules(arena, &nested) != NULL) {
        failures++;
        fprintf(stderr, "FAIL: nested overlay removal hides an inherited rule\n");
    }
    expect_rule_inventory(arena, &base,
        "(LCons (PrimeRule late 0 () (DeclConst invisible))"
        " (LCons (PrimeRule inherited 0 () (DeclConst carrier)) LNil))",
        "overlay removal does not mutate the indexed base inventory");
    space_free(&nested);
    space_free(&view);
    space_free(&base);
}

int main(void) {
    Arena arena;
    TermUniverse universe;
    Space space;
    SymbolTable symbols;
    VarInternTable variables;
    arena_init(&arena);
    arena_set_runtime_kind(&arena, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &arena);
    space_init_with_universe(&space, &universe);
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&variables);
    g_symbols = &symbols;
    g_var_intern = &variables;

    check_exact_admission(&arena, &universe);

    /* Fresh spaces interpret the shared core, without guest constants. */
    expect_status(&arena, &space, "(set:known-proposition corePairing)",
                  false, 0u, "Established");
    check_admission_revocation(&arena, &universe);
    check_admission_lifetimes(&arena, &universe);
    expect_status(&arena, &space, "(set:of Empty)",
                  false, 0u, "Undetermined");

    /* An unsupported explicit profile suppresses the default interpretation. */
    Atom *unsupported = parse_one(&arena, "(set:profile none)");
    space_add(&space, unsupported);
    checks++;
    if (prime_scoped_judgment_judge(&arena, &space,
            parse_one(&arena, "(set:known-proposition corePairing)"), false, 0u) != NULL) {
        failures++;
        fprintf(stderr, "FAIL: an unsupported profile interpreted a set judgment\n");
    }
    space_remove(&space, unsupported);
    expect_status(&arena, &space, "(set:known-proposition corePairing)",
                  false, 0u, "Established");
    space_add(&space, parse_one(&arena, "(set:profile megalodon-hotg)"));

    /* Conflicting explicit profiles also confer no interpretation. */
    Atom *conflicting = parse_one(&arena, "(hol:profile base)");
    space_add(&space, conflicting);
    checks++;
    if (prime_scoped_judgment_judge(&arena, &space,
            parse_one(&arena, "(set:of Empty)"), false, 0u) != NULL) {
        failures++;
        fprintf(stderr, "FAIL: conflicting profiles interpreted a set judgment\n");
    }
    space_remove(&space, conflicting);
    check_overlay_rule_inventory(&arena, &universe);

    const char *valid =
        "(set:proves (pf:all-elim (pf:known universeIn) Empty)"
        " (In Empty (UnivOf Empty)))";
    const char *wrong_target =
        "(set:proves (pf:all-elim (pf:known universeIn) Empty)"
        " (In Empty Empty))";

    /* Exhaustion of the conversion authority is not a new decision route. */
    expect_status(&arena, &space, valid, true, 0u, "Incomplete");
    expect_status(&arena, &space, valid, true, 64u, "Incomplete");
    expect_status(&arena, &space, wrong_target, true, 64u, "Incomplete");
    expect_status(&arena, &space, valid, false, 0u, "Established");
    expect_status(&arena, &space, wrong_target, false, 0u, "Refuted");
    expect_status(&arena, &space,
        "(set:proves (pf:known missing) Falsum)", false, 0u, "Undetermined");
    /* A bad submitted proof of a true proposition is rejected, not accepted
     * merely because another proof of that proposition exists. */
    expect_status(&arena, &space,
        "(set:proves (pf:hyp 0) (In Empty (Power Empty)))",
        false, 0u, "Refuted");

    /* Dependent use rechecks the exact package before applying the consumer.
     * Exhaustion remains incomplete; with an unbounded budget the same
     * package and consumer establish a genuine dependent application. */
    Atom *proof_query = parse_one(&arena, "(set:native-proof universeIn)");
    Atom *proof_verdict = prime_scoped_judgment_judge(
        &arena, &space, proof_query, false, 0u);
    Atom *proof_evidence = proof_verdict && proof_verdict->kind == ATOM_EXPR &&
            proof_verdict->expr.len == 4u
        ? proof_verdict->expr.elems[3] : NULL;
    Atom *package = proof_evidence && proof_evidence->kind == ATOM_EXPR &&
            proof_evidence->expr.len == 2u &&
            atom_is_symbol(proof_evidence->expr.elems[0], "PrimeScopedValue")
        ? proof_evidence->expr.elems[1] : NULL;
    Atom *proof_type = package && package->kind == ATOM_EXPR &&
            package->expr.len == 10u &&
            atom_is_symbol(package->expr.elems[0], "SetNativeProofV1")
        ? package->expr.elems[5] : NULL;
    Atom *consumer = proof_type
        ? atom_expr(
            &arena,
            (Atom *[]){atom_symbol(&arena, "Lam"), proof_type,
                       parse_one(&arena, "(Refl (idx 0))")},
            3u)
        : NULL;
    Atom *use = package && consumer
        ? atom_expr(
            &arena,
            (Atom *[]){atom_symbol(&arena, "set:native-use"), package,
                       consumer},
            3u)
        : NULL;
    expect_judgment_status(&arena, &space, use, "dependent native use",
                           true, 0u, "Incomplete");
    expect_judgment_status(&arena, &space, use, "dependent native use",
                           false, 0u, "Established");

    /* One public judgment has one regular-kernel allowance.  In particular,
     * native-proof revalidation and consumer synthesis cannot each restart
     * the caller's budget. */
    Atom *context = package ? package->expr.elems[6] : NULL;
    Atom *rules = package ? package->expr.elems[7] : NULL;
    Atom *application = consumer && package
        ? atom_expr(
            &arena,
            (Atom *[]){atom_symbol(&arena, "App"), consumer,
                       package->expr.elems[4]},
            3u)
        : NULL;
    uint64_t proof_steps = proof_query
        ? minimum_judgment_steps(&arena, &space, proof_query) : 0u;
    uint64_t consumer_steps = context && rules && application
        ? minimum_kernel_synthesis_steps(
            &arena, context, rules, application) : 0u;
    uint64_t use_steps = use
        ? minimum_judgment_steps(&arena, &space, use) : 0u;
    checks++;
    if (proof_steps == 0u || consumer_steps == 0u || use_steps == 0u ||
        use_steps < proof_steps + consumer_steps) {
        fprintf(stderr,
                "FAIL: native-use refreshed its budget "
                "(proof=%llu consumer=%llu use=%llu)\n",
                (unsigned long long)proof_steps,
                (unsigned long long)consumer_steps,
                (unsigned long long)use_steps);
        failures++;
    }

    /* Constructor shape and positivity are not field-type formation. The
     * existing authority's abstention must not be relabeled as refutation. */
    expect_status(&arena, &space,
        "(set:inductive GhostTree (u 0) (: GhostNode (-> MissingType GhostTree)))",
        false, 0u, "Undetermined");
    /* A set is a type of the sort of all sets, above (u 0): a field whose
     * type is the set Empty is a checked universe violation at (u 0). */
    expect_status(&arena, &space,
        "(set:inductive ValueTree (u 0) (: ValueNode (-> Empty ValueTree)))",
        false, 0u, "Refuted");
    expect_status(&arena, &space,
        "(set:inductive BadLevel (u -1) (: BadLevelCtor BadLevel))",
        false, 0u, "Refuted");
    space_add(&space, parse_one(&arena, "(: Large (u 1))"));
    space_add(&space, parse_one(&arena, "(: Field (u 0))"));
    expect_status(&arena, &space,
        "(set:inductive SmallTree (u 0) (: SmallNode (-> Large SmallTree)))",
        false, 0u, "Refuted");
    expect_status(&arena, &space,
        "(set:inductive LargeTree (u 1) (: LargeNode (-> Large LargeTree)))",
        false, 0u, "Established");
    expect_status(&arena, &space,
        "(set:inductive P (u 0) (: MkP P))",
        false, 0u, "Established");
    expect_status(&arena, &space,
        "(set:inductive NameTree (u 0) (: v1 (-> Field NameTree)))",
        false, 0u, "Established");
    expect_status(&arena, &space,
        "(set:inductive ConflictTree (u 0) (: ConflictTree-rec ConflictTree))",
        false, 0u, "Refuted");
    expect_status(&arena, &space,
        "(set:inductive WideTree (u 0)"
        " (: WideNode (-> Field Field Field Field Field Field Field Field Field WideTree)))",
        false, 0u, "Established");

    check_inductive_shared_allowance(&arena, &space);
    check_retained_formation(&arena, &space);
    check_request_cache_lifetime(&arena, &space);

    /* The definition elaborator's type-shaped syntax does not earn formation.
     * Bounded formation and both native endpoint checks share one allowance;
     * every refusal leaves the temporary declaration out of the theory. */
    expect_status(&arena, &space,
        "(set:define missingId (-> MissingType MissingType) (= (missingId $x) $x))",
        false, 0u, "Undetermined");
    /* Empty is a type, a set: the identity on it is no function into Field,
     * a checked refutation. */
    expect_status(&arena, &space,
        "(set:define valueDomain (-> Empty Field) (= (valueDomain $x) $x))",
        false, 0u, "Refuted");
    expect_status(&arena, &space,
        "(set:define badDefinition (-> (u -1) Field) (= (badDefinition $x) $x))",
        false, 0u, "Refuted");
    expect_status(&arena, &space,
        "(set:define typeId (-> (u 0) (u 0)) (= (typeId $x) $x))",
        false, 0u, "Established");
    expect_status(&arena, &space,
        "(set:define dependentId (-> (A : (u 0)) (x : A) A) (= (dependentId $A $x) $x))",
        false, 0u, "Established");
    expect_status(&arena, &space,
        "(set:define wrongDependent (-> (A : (u 0)) (x : A) A) (= (wrongDependent $A $x) $A))",
        false, 0u, "Refuted");
    expect_status(&arena, &space,
        "(set:define danglingDependent (-> (A : (u 0)) (x : A) A) (= (danglingDependent $A $x) $missing))",
        false, 0u, "Undetermined");
    expect_status(&arena, &space,
        "(set:define reflexiveEvidence (-> (A : (u 0)) (x : A) (id A x x))"
        " (= (reflexiveEvidence $A $x) (refl $x)))",
        false, 0u, "Established");
    Atom *dependent_query = parse_one(&arena,
        "(set:define budgetDependent (-> (A : (u 0)) (x : A) A) (= (budgetDependent $A $x) $x))");
    uint64_t dependent_revision = space_revision(&space);
    uint64_t dependent_steps = minimum_judgment_steps(&arena, &space, dependent_query);
    checks++;
    if (dependent_steps < 2u) {
        failures++;
        fprintf(stderr, "FAIL: no finite dependent clause checking allowance\n");
    } else {
        expect_judgment_status(&arena, &space, dependent_query,
            "dependent definition zero allowance", true, 0u, "Incomplete");
        expect_judgment_status(&arena, &space, dependent_query,
            "dependent definition just below allowance", true, dependent_steps - 1u, "Incomplete");
        expect_judgment_status(&arena, &space, dependent_query,
            "dependent definition exact allowance", true, dependent_steps, "Established");
        expect_judgment_status(&arena, &space, dependent_query,
            "dependent definition increased allowance", true, dependent_steps + 7u, "Established");
    }
    checks++;
    if (space_revision(&space) != dependent_revision) {
        failures++;
        fprintf(stderr, "FAIL: dependent clause checking published before admission\n");
    }
    Atom *definition_query = parse_one(&arena,
        "(set:define budgetId (-> Field Field) (= (budgetId $x) $x))");
    uint64_t definition_revision = space_revision(&space);
    uint64_t definition_steps = minimum_judgment_steps(&arena, &space, definition_query);
    checks++;
    if (definition_steps < 2u) {
        failures++;
        fprintf(stderr, "FAIL: no finite definition checking allowance\n");
    } else {
        expect_judgment_status(&arena, &space, definition_query,
            "definition zero allowance", true, 0u, "Incomplete");
        expect_judgment_status(&arena, &space, definition_query,
            "definition just below allowance", true, definition_steps - 1u, "Incomplete");
        expect_judgment_status(&arena, &space, definition_query,
            "definition exact allowance", true, definition_steps, "Established");
        expect_judgment_status(&arena, &space, definition_query,
            "definition increased allowance", true, definition_steps + 7u, "Established");
    }
    checks++;
    if (space_revision(&space) != definition_revision) {
        failures++;
        fprintf(stderr, "FAIL: definition checking mutated the authored theory\n");
    }

    check_case_compilation_allowance(&arena, &universe);

    g_var_intern = NULL;
    g_symbols = NULL;
    var_intern_free(&variables);
    symbol_table_free(&symbols);
    space_free(&space);
    term_universe_free(&universe);
    arena_free(&arena);

    if (failures) return 1;
    printf("Scoped conversion and declaration outcomes: %u checks passed\n", checks);
    return 0;
}

#include "term_domain.h"
#include "proof_language_region.h"
#include "space.h"
#include "parser.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static TermUniverse *observed_universe;
static SymbolTable *observed_symbols;
static unsigned freed_universes, freed_symbols;
static bool refuse_domain;
void __real_term_universe_free(TermUniverse *);
void __wrap_term_universe_free(TermUniverse *u) {
    if (u == observed_universe) {
        assert(g_symbols == observed_symbols);
        freed_universes++;
    }
    __real_term_universe_free(u);
}
void __real_symbol_table_free(SymbolTable *);
void __wrap_symbol_table_free(SymbolTable *s) {
    if (s == observed_symbols) {
        assert(g_symbols == observed_symbols);
        freed_symbols++;
    }
    __real_symbol_table_free(s);
}
void *__real_cetta_fallible_calloc(size_t, size_t);
void *__wrap_cetta_fallible_calloc(size_t count, size_t width) {
    if (refuse_domain) { refuse_domain = false; return NULL; }
    return __real_cetta_fallible_calloc(count, width);
}
typedef struct { unsigned references, finalized; SymbolTable *symbols; } Witness;
static void witness_keep(void *p) { ((Witness *)p)->references++; }
static void witness_drop(void *p) {
    Witness *w = p;
    assert(w->references && g_symbols == w->symbols);
    if (!--w->references) w->finalized++;
}
static Atom *parse(Arena *a, const char *text) {
    Atom **forms = NULL;
    assert(parse_metta_text(text, a, &forms) == 1);
    Atom *result = forms[0];
    free(forms);
    return result;
}
static AtomId key(TermUniverse *u, Atom *value) {
    AtomId result = term_universe_store_alpha_key(u, value);
    assert(result != CETTA_ATOM_ID_NONE);
    return result;
}
typedef struct { CettaTermDomain *domain; CettaProofImage *image; } DomainCapture;
static DomainCapture capture_new(CettaTermDomain *domain, AtomId root, AtomId seed) {
    CettaProofBuilder *builder = NULL;
    assert(cetta_proof_builder_new(cetta_term_domain_universe(domain), root, seed,
        (CettaProofLimits){8u, 8u, 8u, 64u, 64u}, &builder) == CETTA_PROOF_OK);
    DomainCapture capture = {.domain = domain};
    assert(cetta_proof_builder_seal(builder, &capture.image) == CETTA_PROOF_OK);
    cetta_proof_builder_free(builder);
    assert(cetta_term_domain_retain(domain));
    return capture;
}
static void capture_drop(DomainCapture *capture) {
    cetta_proof_image_release(capture->image);
    capture->image = NULL;
    cetta_term_domain_release(capture->domain);
    capture->domain = NULL;
}
static void capture_one(const DomainCapture *capture, AtomId root, AtomId seed) {
    CettaProofCursor *cursor = NULL;
    assert(cetta_proof_cursor_new(capture->image, CETTA_PROOF_ALL_STATES, &cursor)
           == CETTA_PROOF_OK);
    CettaProofBudget budget = {64u, 0u};
    CettaProofWord word;
    assert(cetta_proof_cursor_peek(cursor, &budget, &word) == CETTA_PROOF_ROW);
    assert(word.state_key == root && word.seed == seed && !word.label_count);
    assert(cetta_proof_cursor_commit(cursor, word.token));
    assert(cetta_proof_cursor_peek(cursor, &budget, &word) == CETTA_PROOF_END);
    cetta_proof_cursor_free(cursor);
}

static void captured_lifetime(TermUniverseStoreFormat format) {
    CettaTermDomain *prior = cetta_term_domain_new(format,
        TERM_UNIVERSE_COLLECTION_RECLAIM, true);
    CettaTermDomain *domain = cetta_term_domain_new(format,
        TERM_UNIVERSE_COLLECTION_RECLAIM, false);
    assert(prior && domain);
    CettaTermDomainScope outer = {0}, scope = {0};
    assert(cetta_term_domain_enter(prior, &outer));
    SymbolTable *prior_symbols = g_symbols;
    VarInternTable *prior_variables = g_var_intern;
    HashConsTable *prior_hashcons = g_hashcons;
    BuiltinSyms prior_builtins = g_builtin_syms;
    assert(prior_hashcons && cetta_term_domain_enter(domain, &scope));
    assert(!cetta_term_domain_enter(prior, &scope));
    observed_universe = cetta_term_domain_universe(domain);
    observed_symbols = cetta_term_domain_symbols(domain);
    assert(observed_universe->instance_id != cetta_term_domain_universe(prior)->instance_id);
    assert(observed_symbols->instance_id != prior_symbols->instance_id);
    freed_universes = freed_symbols = 0u;
    Witness witness = {.symbols = observed_symbols};
    assert(arena_retain_resource_sized(cetta_term_domain_persistent(domain),
        &witness, witness_keep, witness_drop, sizeof(witness), sizeof(witness)));
    Arena syntax;
    arena_init_detached(&syntax);
    AtomId root = key(observed_universe, parse(&syntax, "(rows 2 (C (pair $x $x)))"));
    AtomId seed = key(observed_universe, parse(&syntax, "I"));
    DomainCapture old = capture_new(domain, root, seed);
    capture_one(&old, root, seed);
    Space later;
    space_init_with_universe(&later, observed_universe);
    for (unsigned i = 0u; i < 4096u; i++) {
        char source[64];
        snprintf(source, sizeof(source), "(later %u)", i);
        space_add(&later, parse(&syntax, source));
    }
    assert(space_length64(&later) == 4096u);
    Atom *last = parse(&syntax, "(later 4095)");
    cetta_term_domain_leave(&scope);
    assert(g_symbols == prior_symbols && g_var_intern == prior_variables &&
           g_hashcons == prior_hashcons &&
           !memcmp(&g_builtin_syms, &prior_builtins, sizeof(prior_builtins)));
    cetta_term_domain_release(domain);
    assert(!freed_universes && !freed_symbols && !witness.finalized);
    assert(cetta_term_domain_enter(domain, &scope));
    space_free(&later);
    assert(observed_universe->root_provider_len == 1u);
    TermUniverseCollectionReport report;
    assert(term_universe_collect(observed_universe, &report) && report.reclaimed >= 4096u);
    assert(tu_hdr(observed_universe, root));
    assert(term_universe_lookup_atom_id(observed_universe, last) == CETTA_ATOM_ID_NONE);
    assert(cetta_proof_image_stats(old.image).support == 1u);
    capture_one(&old, root, seed);
    arena_free(&syntax);
    cetta_term_domain_leave(&scope);
    capture_drop(&old);
    assert(freed_universes == 1u && freed_symbols == 1u && witness.finalized == 1u);
    assert(g_symbols == prior_symbols && g_var_intern == prior_variables &&
           g_hashcons == prior_hashcons &&
           !memcmp(&g_builtin_syms, &prior_builtins, sizeof(prior_builtins)));
    observed_universe = NULL; observed_symbols = NULL;
    cetta_term_domain_leave(&outer);
    cetta_term_domain_release(prior);
    assert(!g_symbols && !g_var_intern && !g_hashcons);
    printf("PASS: %s physical domain keeps a one-row capture while 4096 later Space rows retire; nested globals and real destruction match\n",
        format == TERM_UNIVERSE_STORE_FORMAT_COMPACT32_V1 ? "compact" : "wide");
}

typedef struct { AtomId ids[64]; } Reused;
static void reused_roots(const TermUniverse *u, TermUniverseRootVisit visit,
                         void *visit_context, void *context) {
    (void)u;
    Reused *holder = context;
    for (unsigned i = 0u; i < 64u; i++) visit(visit_context, holder->ids[i]);
}
static void storage_reset(TermUniverseStoreFormat format) {
    CettaTermDomain *domain = cetta_term_domain_new(format,
        TERM_UNIVERSE_COLLECTION_RECLAIM, false);
    CettaTermDomainScope scope = {0};
    assert(domain && cetta_term_domain_enter(domain, &scope));
    TermUniverse *u = cetta_term_domain_universe(domain);
    Arena syntax;
    arena_init_detached(&syntax);
    AtomId root = key(u, parse(&syntax, "(rows 2 (C (pair $x $x)))"));
    AtomId seed = key(u, parse(&syntax, "I"));
    DomainCapture old = capture_new(domain, root, seed);
    uint64_t epoch = u->storage_epoch, instance = u->instance_id;
    term_universe_set_persistent_arena(u, NULL);
    term_universe_set_persistent_arena(u, cetta_term_domain_persistent(domain));
    assert(u->instance_id == instance && u->storage_epoch > epoch);
    assert(!cetta_proof_image_matches_universe(old.image, u));
    Reused holder;
    bool reused = false;
    for (unsigned i = 0u; i < 64u; i++) {
        holder.ids[i] = tu_intern_int(u, INT64_C(7000000) + i);
        assert(holder.ids[i] != CETTA_ATOM_ID_NONE);
        reused |= holder.ids[i] == root || holder.ids[i] == seed;
    }
    assert(reused && term_universe_add_counted_root_provider(u, reused_roots, &holder));
    assert(term_universe_collect(u, NULL) && u->counting);
    capture_drop(&old);
    for (unsigned i = 0u; i < 64u; i++) assert(tu_hdr(u, holder.ids[i]));
    term_universe_remove_root_provider(u, reused_roots, &holder);
    for (unsigned i = 0u; i < 64u; i++) term_universe_release_counted(u, holder.ids[i]);
    assert(term_universe_collect(u, NULL) && !u->live_entries);
    arena_free(&syntax);
    cetta_term_domain_leave(&scope);
    cetta_term_domain_release(domain);
    assert(!g_symbols && !g_var_intern && !g_hashcons);
    printf("PASS: %s storage reset invalidates capture without dropping reused current coordinates\n",
        format == TERM_UNIVERSE_STORE_FORMAT_COMPACT32_V1 ? "compact" : "wide");
}

int main(void) {
    refuse_domain = true;
    assert(!cetta_term_domain_new(TERM_UNIVERSE_STORE_FORMAT_COMPACT32_V1,
        TERM_UNIVERSE_COLLECTION_RECLAIM, false));
    assert(!g_symbols && !g_var_intern && !g_hashcons);
    assert(!cetta_term_domain_new((TermUniverseStoreFormat)0,
        TERM_UNIVERSE_COLLECTION_RECLAIM, false));
    assert(!cetta_term_domain_new(TERM_UNIVERSE_STORE_FORMAT_COMPACT32_V1,
        (TermUniverseCollectionMode)99, false));
    captured_lifetime(TERM_UNIVERSE_STORE_FORMAT_COMPACT32_V1);
    captured_lifetime(TERM_UNIVERSE_STORE_FORMAT_WIDE64_V1);
    storage_reset(TERM_UNIVERSE_STORE_FORMAT_COMPACT32_V1);
    storage_reset(TERM_UNIVERSE_STORE_FORMAT_WIDE64_V1);
    puts("PASS: allocation/configuration refusal and active-scope protection preserve the current environment");
    return 0;
}

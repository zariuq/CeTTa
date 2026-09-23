#include "prime_scoped_judgments.h"
#include "generated/prime_nik_authorities_v1.generated.h"

#include "lang.h"
#include "native_sha256.h"
#include "nik_direct_authority.h"
#include "parser.h"
#include "prime_regular_kernel.h"
#include "prime_regular_pattern.h"
#include "prime_semantics.h"
#include "stats.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Space *prime_public_space_argument(Arena *arena, Atom *atom);

/* ------------------------------------------------------------------------ */
/* Verdict helpers.  The shape is the one `type:` already uses so that the   */
/* evaluator's public readout treats all scoped bubbles alike.               */
/* ------------------------------------------------------------------------ */

static Atom *sj_sym(Arena *a, const char *name) { return atom_symbol(a, name); }

static Atom *sj_expr1(Arena *a, const char *head) {
    Atom *items[1] = {sj_sym(a, head)};
    return atom_expr(a, items, 1u);
}

static Atom *sj_expr2(Arena *a, const char *head, Atom *x) {
    return atom_expr2(a, sj_sym(a, head), x);
}

static Atom *sj_expr3(Arena *a, const char *head, Atom *x, Atom *y) {
    return atom_expr3(a, sj_sym(a, head), x, y);
}

static Atom *sj_verdict(Arena *a, const char *status, Atom *judgment,
                        Atom *evidence) {
    Atom *items[4] = {sj_sym(a, "PrimeVerdict"), sj_sym(a, status),
                      judgment, evidence ? evidence : atom_unit(a)};
    return atom_expr(a, items, 4u);
}

static Atom *sj_established(Arena *a, Atom *j, Atom *e) {
    return sj_verdict(a, "Established", j, e);
}
static Atom *sj_refuted(Arena *a, Atom *j, Atom *e) {
    return sj_verdict(a, "Refuted", j, e);
}
static Atom *sj_undetermined(Arena *a, Atom *j, Atom *e) {
    return sj_verdict(a, "Undetermined", j, e);
}
static Atom *sj_incomplete(Arena *a, Atom *j, Atom *e) {
    return sj_verdict(a, "Incomplete", j, e);
}

static bool sj_is_expr(Atom *t, const char *head, CettaExprLen len) {
    return t && t->kind == ATOM_EXPR && t->expr.len == len &&
           atom_is_symbol(t->expr.elems[0], head);
}

static const char *sj_head_name(Atom *t) {
    if (!t || t->kind != ATOM_EXPR || t->expr.len == 0u ||
        !t->expr.elems[0] || t->expr.elems[0]->kind != ATOM_SYMBOL)
        return NULL;
    return atom_name_cstr(t->expr.elems[0]);
}

typedef enum {
    SJ_STATUS_ESTABLISHED,
    SJ_STATUS_REFUTED,
    SJ_STATUS_UNDETERMINED,
    SJ_STATUS_INCOMPLETE,
    SJ_STATUS_INVALID
} SjStatus;

static SjStatus sj_verdict_status(Atom *verdict, Atom **evidence_out) {
    if (evidence_out) *evidence_out = NULL;
    if (!sj_is_expr(verdict, "PrimeVerdict", 4u)) return SJ_STATUS_INVALID;
    Atom *evidence = verdict->expr.elems[3];
    if (evidence && evidence->kind == ATOM_EXPR && evidence->expr.len >= 2u &&
        atom_is_symbol(evidence->expr.elems[0], "PrimeEvidenceV1"))
        evidence = evidence->expr.elems[1];
    if (evidence_out) *evidence_out = evidence;
    Atom *status = verdict->expr.elems[1];
    if (atom_is_symbol(status, "Established")) return SJ_STATUS_ESTABLISHED;
    if (atom_is_symbol(status, "Refuted")) return SJ_STATUS_REFUTED;
    if (atom_is_symbol(status, "Undetermined")) return SJ_STATUS_UNDETERMINED;
    if (atom_is_symbol(status, "Incomplete")) return SJ_STATUS_INCOMPLETE;
    return SJ_STATUS_INVALID;
}

static const char *sj_status_name(SjStatus status) {
    switch (status) {
    case SJ_STATUS_ESTABLISHED: return "Established";
    case SJ_STATUS_REFUTED: return "Refuted";
    case SJ_STATUS_INCOMPLETE: return "Incomplete";
    default: return "Undetermined";
    }
}

/* ------------------------------------------------------------------------ */
/* Ownership by symbol identity.  Only these spellings are judgments; the    */
/* evaluator asks with the interned head id, never with a string.            */
/* ------------------------------------------------------------------------ */

bool prime_scoped_judgment_is_head_id(SymbolId head) {
    if (head == SYMBOL_ID_NONE) return false;
    return head == g_builtin_syms.set_colon_signature ||
           head == g_builtin_syms.set_colon_formed ||
           head == g_builtin_syms.set_colon_of ||
           head == g_builtin_syms.set_colon_check ||
           head == g_builtin_syms.set_colon_eq ||
           head == g_builtin_syms.set_colon_proves ||
           head == g_builtin_syms.set_colon_axiom ||
           head == g_builtin_syms.set_colon_define ||
           head == g_builtin_syms.set_colon_inductive ||
           head == g_builtin_syms.set_colon_theorem ||
           head == g_builtin_syms.set_colon_native_proof ||
           head == g_builtin_syms.set_colon_native_use ||
           head == g_builtin_syms.set_colon_native_normalize ||
           head == g_builtin_syms.set_colon_recheck ||
           head == g_builtin_syms.set_colon_known_proof ||
           head == g_builtin_syms.set_colon_known_proposition ||
           head == g_builtin_syms.set_colon_signature_digest ||
           head == g_builtin_syms.lang_colon_languages ||
           head == g_builtin_syms.lang_colon_parse ||
           head == g_builtin_syms.lang_colon_print ||
           head == g_builtin_syms.lang_colon_native_type ||
           head == g_builtin_syms.lang_colon_step ||
           head == g_builtin_syms.try_judgment ||
           head == g_builtin_syms.id_colon_region ||
           head == g_builtin_syms.id_colon_policy;
}

/* ------------------------------------------------------------------------ */
/* The admitted `set:` signature.  The quantifier and equality are declared   */
/* once, polymorphic over a universe level; every use is an instance at a     */
/* simple type, and the proof checker lowers instances to simple constants   */
/* for the kernel, a chart that retains the higher-order source.  Base types */
/* live in the tower universe `(u 0)`; the legacy `u0`/`u1` and HE `Type`    */
/* spellings are not accepted as declared base types by the declared route.  */
/* ------------------------------------------------------------------------ */

static const char SJ_SET_SIGNATURE_TEXT[] =
    "(: set (u 0))\n"
    "(: prop (u 0))\n"
    "(: Falsum prop)\n"
    "(: imp (-> prop (-> prop prop)))\n"
    "(: all (-> (A : (u $level)) (-> (-> A prop) prop)))\n"
    "(: eq (-> (A : (u $level)) (-> A (-> A prop))))\n"
    "(: In (-> set (-> set prop)))\n"
    "(: Empty set)\n"
    "(: Union (-> set set))\n"
    "(: Power (-> set set))\n"
    "(: Sep (-> set (-> (-> set prop) set)))\n"
    "(: Repl (-> set (-> (-> set set) set)))\n"
    "(: Eps_set (-> (-> set prop) set))\n"
    "(: UnivOf (-> set set))\n"
    "(set:seed EmptyE (all set (lam x (imp (In x Empty) Falsum))))\n"
    "(set:seed PowerI (all set (lam x (In x (Power x)))))\n"
    "(set:seed UnivOfI (all set (lam x (In x (UnivOf x)))))\n";

/* Content digest of the admitted signature: the theory a published fact was
 * checked against.  Reuse under a different signature is not automatic. */
static const char *sj_signature_digest(void) {
    static char digest[65];
    if (digest[0] == '\0')
        cetta_native_sha256_hex((const uint8_t *)SJ_SET_SIGNATURE_TEXT,
                                strlen(SJ_SET_SIGNATURE_TEXT), digest);
    return digest;
}

/* A known proposition is an eight-place record:
 *   (set:known name proposition origin proof depends revision signature)
 * origin ∈ {signature, axiom, theorem}; proof is () unless origin is theorem;
 * depends lists (name proposition) pairs cited by the proof at publication;
 * revision is the space revision at publication (informational: reuse is
 * decided by replaying the dependencies, not by revision equality);
 * signature is the digest of the admitted signature the fact was checked
 * against, and reuse under another signature abstains. */
enum { SJ_KNOWN_LEN = 8, SJ_KNOWN_NAME = 1, SJ_KNOWN_PROP = 2,
       SJ_KNOWN_ORIGIN = 3, SJ_KNOWN_PROOF = 4, SJ_KNOWN_DEPENDS = 5,
       SJ_KNOWN_REVISION = 6, SJ_KNOWN_SIGNATURE = 7 };

static Atom *sj_known_record(Arena *a, Atom *name, Atom *prop,
                             const char *origin, Atom *proof, Atom *depends,
                             uint64_t revision) {
    Atom *items[SJ_KNOWN_LEN] = {
        sj_sym(a, "set:known"), name, prop, sj_sym(a, origin),
        proof ? proof : atom_unit(a), depends ? depends : atom_unit(a),
        atom_int(a, revision > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)revision),
        atom_string(a, sj_signature_digest())};
    return atom_expr(a, items, SJ_KNOWN_LEN);
}

/* Admitted-object boundary.  Publication through `set:axiom`/`set:theorem`
 * admits a record: its content digest is registered for the publishing
 * space.  A record found in a space by lookup alone, whatever its tag or
 * digest, is untrusted syntax until it is admitted: reuse abstains, and
 * `set:recheck` may admit it by checking it in this space.  The registry is
 * a small open-addressed set of (space instance, digest) entries; it never
 * replays proofs. */
typedef struct {
    uint64_t instance;
    char digest[65];
} SjAdmitted;

static SjAdmitted *g_admitted;
static size_t g_admitted_count;
static size_t g_admitted_cap;

static void sj_record_digest(Arena *a, Atom *record, char out[65]) {
    char *shown = atom_to_string(a, record);
    cetta_native_sha256_hex((const uint8_t *)(shown ? shown : ""),
                            shown ? strlen(shown) : 0u, out);
}

static size_t sj_admitted_slot(uint64_t instance, const char *digest) {
    uint64_t h = instance * 1469598103934665603ull;
    for (const char *c = digest; *c; c++) h = (h ^ (uint64_t)(unsigned char)*c) * 1099511628211ull;
    return (size_t)(h % g_admitted_cap);
}

static bool sj_admitted_contains(uint64_t instance, const char *digest) {
    if (!g_admitted_cap) return false;
    size_t slot = sj_admitted_slot(instance, digest);
    for (size_t probe = 0u; probe < g_admitted_cap; probe++) {
        SjAdmitted *entry = &g_admitted[(slot + probe) % g_admitted_cap];
        if (entry->digest[0] == '\0') return false;
        if (entry->instance == instance && strcmp(entry->digest, digest) == 0)
            return true;
    }
    return false;
}

static void sj_admitted_insert(uint64_t instance, const char *digest);

static void sj_admitted_grow(void) {
    size_t old_cap = g_admitted_cap;
    SjAdmitted *old = g_admitted;
    g_admitted_cap = old_cap ? old_cap * 2u : 64u;
    g_admitted = cetta_malloc(sizeof(SjAdmitted) * g_admitted_cap);
    memset(g_admitted, 0, sizeof(SjAdmitted) * g_admitted_cap);
    g_admitted_count = 0u;
    for (size_t i = 0u; i < old_cap; i++)
        if (old[i].digest[0] != '\0')
            sj_admitted_insert(old[i].instance, old[i].digest);
    free(old);
}

static void sj_admitted_insert(uint64_t instance, const char *digest) {
    if (g_admitted_count * 2u >= g_admitted_cap) sj_admitted_grow();
    size_t slot = sj_admitted_slot(instance, digest);
    for (size_t probe = 0u; probe < g_admitted_cap; probe++) {
        SjAdmitted *entry = &g_admitted[(slot + probe) % g_admitted_cap];
        if (entry->digest[0] == '\0') {
            entry->instance = instance;
            snprintf(entry->digest, sizeof entry->digest, "%s", digest);
            g_admitted_count++;
            return;
        }
        if (entry->instance == instance && strcmp(entry->digest, digest) == 0)
            return;
    }
}

void prime_scoped_judgment_admit(Arena *a, Space *space, Atom *record);

static void sj_admit(Arena *a, Space *space, Atom *record) {
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_SCOPED_PUBLICATION);
    char digest[65];
    sj_record_digest(a, record, digest);
    sj_admitted_insert(space_instance_id(space), digest);
}

static bool sj_record_admitted(Arena *a, Space *space, Atom *record) {
    char digest[65];
    sj_record_digest(a, record, digest);
    return sj_admitted_contains(space_instance_id(space), digest);
}

static bool sj_record_signature_current(Atom *record) {
    Atom *digest = record->expr.elems[SJ_KNOWN_SIGNATURE];
    return digest && digest->kind == ATOM_GROUNDED &&
           digest->ground.gkind == GV_STRING && digest->ground.sval &&
           strcmp(digest->ground.sval, sj_signature_digest()) == 0;
}

/* ------------------------------------------------------------------------ */
/* The signature environment: parsed once, kept for the life of the process, */
/* and its overlay over the current space rebuilt only when that space's     */
/* identity or revision changes.  Ordinary judgments pay nothing for it.     */
/* ------------------------------------------------------------------------ */

typedef struct {
    Atom *name;
    Atom *type;          /* canonical (declared-route) spelling */
} SjConstantType;

/* A definition by equations, compiled for the rewriter: each rule has one
 * canonical pattern per argument, with pattern variables as indices, and a
 * canonical right-hand side over the same indices. */
typedef struct {
    Atom **pats;
    Atom *rhs;
    size_t nvars;
} SjRule;

typedef struct {
    Atom *name;
    size_t arity;
    SjRule *rules;
    size_t rule_count;
    Atom *prop;                  /* the authored (define type equations...) */
} SjDefinition;

typedef struct {
    bool parsed;
    Arena arena;                 /* process-lifetime owner of the parsed text */
    Atom **signature;            /* authored (: name type) declarations */
    size_t signature_count;
    Atom **seeds;                /* known records of the signature's axioms */
    size_t seed_count;
    Atom **base_types;           /* names declared at a universe */
    size_t base_type_count;
    Atom **poly_heads;           /* names whose type binds a universe variable */
    size_t poly_count;
    bool overlay_ready;
    uint64_t base_instance;
    uint64_t base_revision;
    Space overlay;
    SjConstantType *constants;   /* canonical types of signature constants */
    size_t constant_count;
    size_t constant_cap;
    SjDefinition *defs;          /* the space's definitions, reloaded per revision */
    size_t def_count;
    Atom *kernel_rules;          /* (LCons (PrimeRule ...) ...) from the space's type:rule atoms */
} SjSetEnv;

static SjSetEnv g_set_env;

static bool sj_type_binds_universe(Atom *type) {
    if (!type || type->kind != ATOM_EXPR) return false;
    if (type->expr.len == 3u && atom_is_symbol(type->expr.elems[1], ":") &&
        sj_is_expr(type->expr.elems[2], "u", 2u))
        return true;
    for (CettaExprIndex i = 0u; i < type->expr.len; i++)
        if (sj_type_binds_universe(type->expr.elems[i])) return true;
    return false;
}

static bool sj_env_parse(SjSetEnv *env) {
    if (env->parsed) return true;
    arena_init_detached(&env->arena);
    Arena *a = &env->arena;
    Atom **atoms = NULL;
    int count = parse_metta_text(SJ_SET_SIGNATURE_TEXT, a, &atoms);
    if (count < 0 || !atoms) return false;
    env->signature = arena_alloc(a, sizeof(Atom *) * (size_t)count);
    env->seeds = arena_alloc(a, sizeof(Atom *) * (size_t)count);
    env->base_types = arena_alloc(a, sizeof(Atom *) * (size_t)count);
    env->poly_heads = arena_alloc(a, sizeof(Atom *) * (size_t)count);
    for (int i = 0; i < count; i++) {
        Atom *atom = atoms[i];
        if (sj_is_expr(atom, ":", 3u)) {
            env->signature[env->signature_count++] = atom;
            if (sj_is_expr(atom->expr.elems[2], "u", 2u))
                env->base_types[env->base_type_count++] = atom->expr.elems[1];
            else if (sj_type_binds_universe(atom->expr.elems[2]))
                env->poly_heads[env->poly_count++] = atom->expr.elems[1];
        } else if (sj_is_expr(atom, "set:seed", 3u)) {
            env->seeds[env->seed_count++] = sj_known_record(
                a, atom->expr.elems[1], atom->expr.elems[2], "signature",
                NULL, NULL, 0u);
        }
    }
    free(atoms);
    env->parsed = true;
    return true;
}

/* The environment for `space`: the parsed signature plus an overlay of the
 * space carrying its declarations, rebuilt only on a new space or revision. */
/* Load the space's definition records into the environment: one indexed
 * lookup per rebuild, pointers into the space's own atoms. */
static void sj_env_load_definitions(SjSetEnv *env, Space *space) {
    for (size_t i = 0u; i < env->def_count; i++) {
        for (size_t r = 0u; r < env->defs[i].rule_count; r++) free(env->defs[i].rules[r].pats);
        free(env->defs[i].rules);
    }
    free(env->defs);
    env->defs = NULL;
    env->def_count = 0u;
    Arena *a = &env->arena;
    Atom *items[SJ_KNOWN_LEN] = {
        sj_sym(a, "set:known"), atom_var(a, "n"), atom_var(a, "p"),
        sj_sym(a, "definition"), atom_var(a, "f"), atom_var(a, "d"),
        atom_var(a, "r"), atom_var(a, "s")};
    Atom *pattern = atom_expr(a, items, SJ_KNOWN_LEN);
    CettaIndex *candidates = NULL;
    CettaIndex count = space_match_candidates64(space, pattern, &candidates);
    for (CettaIndex i = 0u; i < count; i++) {
        Atom *atom = space_match_candidate_at64(space, candidates[i]);
        if (!sj_is_expr(atom, "set:known", SJ_KNOWN_LEN) ||
            !atom_is_symbol(atom->expr.elems[SJ_KNOWN_ORIGIN], "definition"))
            continue;
        Atom *compiled = atom->expr.elems[SJ_KNOWN_PROOF];
        if (!compiled || compiled->kind != ATOM_EXPR || compiled->expr.len < 2u ||
            !atom_is_symbol(compiled->expr.elems[0], "rules") ||
            compiled->expr.elems[1]->kind != ATOM_GROUNDED)
            continue;
        SjDefinition def = {0};
        def.name = atom->expr.elems[SJ_KNOWN_NAME];
        def.arity = (size_t)compiled->expr.elems[1]->ground.ival;
        def.prop = atom->expr.elems[SJ_KNOWN_PROP];
        def.rule_count = compiled->expr.len - 2u;
        def.rules = calloc(def.rule_count ? def.rule_count : 1u, sizeof(SjRule));
        bool ok = true;
        for (size_t r = 0u; r < def.rule_count && ok; r++) {
            Atom *rule = compiled->expr.elems[2u + r];
            if (!rule || rule->kind != ATOM_EXPR || rule->expr.len != 3u ||
                rule->expr.elems[0]->kind != ATOM_EXPR ||
                rule->expr.elems[0]->expr.len != def.arity ||
                rule->expr.elems[2]->kind != ATOM_GROUNDED) {
                ok = false;
                break;
            }
            def.rules[r].pats = calloc(def.arity ? def.arity : 1u, sizeof(Atom *));
            for (size_t k = 0u; k < def.arity; k++)
                def.rules[r].pats[k] = rule->expr.elems[0]->expr.elems[k];
            def.rules[r].rhs = rule->expr.elems[1];
            def.rules[r].nvars = (size_t)rule->expr.elems[2]->ground.ival;
        }
        if (!ok) {
            for (size_t r = 0u; r < def.rule_count; r++) free(def.rules[r].pats);
            free(def.rules);
            continue;
        }
        SjDefinition *grown = realloc(env->defs, sizeof(SjDefinition) * (env->def_count + 1u));
        if (!grown) break;
        env->defs = grown;
        env->defs[env->def_count++] = def;
    }
    free(candidates);
}

/* The space's `(type:rule head arity (pats...) rhs)` atoms, in kernel
 * spelling, as the kernel's rule list.  Loaded with the definitions, once
 * per revision. */
static void sj_env_load_kernel_rules(SjSetEnv *env, Space *space) {
    env->kernel_rules = prime_semantics_kernel_rules(&env->arena, space);
}

static SjSetEnv *sj_env(Space *space) {
    SjSetEnv *env = &g_set_env;
    if (!sj_env_parse(env)) return NULL;
    uint64_t instance = space_instance_id(space);
    uint64_t revision = space_revision(space);
    if (env->overlay_ready && env->base_instance == instance &&
        env->base_revision == revision)
        return env;
    if (env->overlay_ready) space_free(&env->overlay);
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_SCOPED_ENVIRONMENT_REBUILD);
    space_init_overlay(&env->overlay, space);
    for (size_t i = 0u; i < env->signature_count; i++)
        space_add(&env->overlay, env->signature[i]);
    env->overlay_ready = true;
    env->base_instance = instance;
    env->base_revision = revision;
    env->constant_count = 0u;
    sj_env_load_definitions(env, space);
    sj_env_load_kernel_rules(env, space);
    return env;
}

static bool sj_env_is_base_type(SjSetEnv *env, Atom *name) {
    for (size_t i = 0u; i < env->base_type_count; i++)
        if (atom_eq(env->base_types[i], name)) return true;
    if (!env->overlay_ready) return false;
    /* A symbol the extended space declares at a universe is a base type of
     * the simple fragment too: one indexed declaration lookup. */
    Atom **declared = NULL;
    SpaceDeclaredTypeLookupCost cost = {0};
    uint32_t count = space_get_declared_types_costed(
        &env->overlay, &env->arena, name, &declared, &cost);
    bool base = false;
    for (uint32_t i = 0u; i < count && !base; i++)
        base = sj_is_expr(declared[i], "u", 2u);
    free(declared);
    return base;
}

static bool sj_env_is_poly_head(SjSetEnv *env, Atom *name) {
    for (size_t i = 0u; i < env->poly_count; i++)
        if (atom_eq(env->poly_heads[i], name)) return true;
    return false;
}

/* Known propositions: records published into the current space by
 * `set:axiom`/`set:theorem` (or inserted directly), then the signature seeds.
 * Candidates come from the space's match index; two records for one name
 * with different propositions make the name ambiguous and nothing is chosen. */
static Atom *sj_known_lookup(Arena *a, SjSetEnv *env, Space *space, Atom *name,
                             bool *ambiguous_out) {
    if (ambiguous_out) *ambiguous_out = false;
    if (!name || name->kind != ATOM_SYMBOL) return NULL;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_SCOPED_RECORD_LOOKUP);
    Atom *found = NULL;
    Atom *items[SJ_KNOWN_LEN] = {
        sj_sym(a, "set:known"), name, atom_var(a, "p"), atom_var(a, "o"),
        atom_var(a, "f"), atom_var(a, "d"), atom_var(a, "r"), atom_var(a, "s")};
    Atom *pattern = atom_expr(a, items, SJ_KNOWN_LEN);
    CettaIndex *candidates = NULL;
    CettaIndex count = space_match_candidates64(space, pattern, &candidates);
    for (CettaIndex i = 0u; i < count; i++) {
        Atom *atom = space_match_candidate_at64(space, candidates[i]);
        if (!sj_is_expr(atom, "set:known", SJ_KNOWN_LEN) ||
            !atom_eq(atom->expr.elems[SJ_KNOWN_NAME], name))
            continue;
        /* A record claiming the signature as its origin is untrusted syntax
         * unless it is the signature's own seed. */
        if (atom_is_symbol(atom->expr.elems[SJ_KNOWN_ORIGIN], "signature")) {
            bool seed = false;
            for (size_t k = 0u; k < env->seed_count && !seed; k++)
                seed = atom_eq(env->seeds[k], atom);
            if (!seed) continue;
        }
        if (found && !atom_eq(found->expr.elems[SJ_KNOWN_PROP],
                              atom->expr.elems[SJ_KNOWN_PROP])) {
            free(candidates);
            if (ambiguous_out) *ambiguous_out = true;
            return NULL;
        }
        if (!found) found = atom;
    }
    free(candidates);
    for (size_t i = 0u; i < env->seed_count; i++) {
        Atom *seed = env->seeds[i];
        if (!atom_eq(seed->expr.elems[SJ_KNOWN_NAME], name)) continue;
        if (found && !atom_eq(found->expr.elems[SJ_KNOWN_PROP],
                              seed->expr.elems[SJ_KNOWN_PROP])) {
            if (ambiguous_out) *ambiguous_out = true;
            return NULL;
        }
        if (!found) found = seed;
    }
    return found;
}

/* ------------------------------------------------------------------------ */
/* Descent: the constant-family (simple) fragment, decided on the authored   */
/* operands without a second elaboration.  Types are base types, sorts and    */
/* binder-free arrows; terms use no dependent former, and a polymorphic head */
/* occurs only applied to a closed simple type.                              */
/* ------------------------------------------------------------------------ */

static bool sj_is_sort(Atom *t) {
    return atom_is_symbol(t, "U0") || atom_is_symbol(t, "U1") ||
           atom_is_symbol(t, "u0") || atom_is_symbol(t, "u1") ||
           sj_is_expr(t, "Sort", 2u) || sj_is_expr(t, "u", 2u);
}

static bool sj_authored_simple_type(SjSetEnv *env, Atom *t) {
    if (!t) return false;
    if (sj_is_sort(t)) return true;
    if (t->kind == ATOM_SYMBOL) return sj_env_is_base_type(env, t);
    if (t->kind == ATOM_EXPR && t->expr.len >= 3u &&
        atom_is_symbol(t->expr.elems[0], "->")) {
        for (CettaExprIndex i = 1u; i < t->expr.len; i++) {
            Atom *component = t->expr.elems[i];
            if (sj_is_expr(component, ":", 3u)) return false;
            if (!sj_authored_simple_type(env, component)) return false;
        }
        return true;
    }
    return false;
}

static bool sj_authored_simple_term(SjSetEnv *env, Atom *t) {
    if (!t) return false;
    if (t->kind == ATOM_SYMBOL) return !sj_env_is_poly_head(env, t);
    if (t->kind != ATOM_EXPR || t->expr.len == 0u) return true;
    Atom *head = t->expr.elems[0];
    if (atom_is_symbol(head, "->")) return sj_authored_simple_type(env, t);
    if (sj_is_expr(t, "lam", 3u))
        return t->expr.elems[1] && t->expr.elems[1]->kind == ATOM_SYMBOL &&
               sj_authored_simple_term(env, t->expr.elems[2]);
    if (sj_is_expr(t, ":", 3u) || atom_is_symbol(head, "sigma") ||
        atom_is_symbol(head, "pair") || atom_is_symbol(head, "fst") ||
        atom_is_symbol(head, "snd") || atom_is_symbol(head, "id") ||
        atom_is_symbol(head, "refl") || atom_is_symbol(head, "id:eliminate"))
        return false;
    CettaExprIndex first = 1u;
    if (head->kind == ATOM_SYMBOL && sj_env_is_poly_head(env, head)) {
        if (t->expr.len < 2u || !sj_authored_simple_type(env, t->expr.elems[1]))
            return false;
        first = 2u;
    } else if (!sj_authored_simple_term(env, head)) {
        return false;
    }
    for (CettaExprIndex i = first; i < t->expr.len; i++)
        if (!sj_authored_simple_term(env, t->expr.elems[i])) return false;
    return true;
}

/* Canonical (declared-route) type spelling, for synthesized types. */
static bool sj_intrinsic_index(Atom *t, uint64_t *index_out) {
    if (!sj_is_expr(t, "idx", 2u)) return false;
    Atom *n = t->expr.elems[1];
    if (!n || n->kind != ATOM_GROUNDED || n->ground.gkind != GV_INT ||
        n->ground.ival < 0)
        return false;
    if (index_out) *index_out = (uint64_t)n->ground.ival;
    return true;
}

static bool sj_is_constant(Atom *t) {
    return t && t->kind == ATOM_SYMBOL && !sj_is_sort(t);
}

static bool sj_mentions_index(Atom *t, uint64_t target) {
    if (!t) return false;
    uint64_t index = 0u;
    if (sj_intrinsic_index(t, &index)) return index == target;
    if (t->kind != ATOM_EXPR || t->expr.len == 0u) return false;
    if (sj_is_expr(t, "Lam", 2u))
        return sj_mentions_index(t->expr.elems[1], target + 1u);
    if (sj_is_expr(t, "Pi", 3u) || sj_is_expr(t, "Sigma", 3u) ||
        sj_is_expr(t, "Lam", 3u))
        return sj_mentions_index(t->expr.elems[1], target) ||
               sj_mentions_index(t->expr.elems[2], target + 1u);
    for (CettaExprIndex i = 1u; i < t->expr.len; i++)
        if (sj_mentions_index(t->expr.elems[i], target)) return true;
    return false;
}

static bool sj_simple_canonical_type(Atom *t) {
    if (!t) return false;
    if (sj_is_sort(t) || sj_is_constant(t)) return true;
    if (sj_is_expr(t, "Pi", 3u))
        return sj_simple_canonical_type(t->expr.elems[1]) &&
               !sj_mentions_index(t->expr.elems[2], 0u) &&
               sj_simple_canonical_type(t->expr.elems[2]);
    return false;
}

/* ------------------------------------------------------------------------ */
/* Observations through the existing `type:` authority in the extended space. */
/* ------------------------------------------------------------------------ */

static bool sj_outcome_established(const CettaPrimeTypingAuthorityObservationV1 *o) {
    return o->result.kind == CETTA_NIK_RESULT_OUTCOME &&
           o->result.value.outcome == CETTA_NIK_OUTCOME_ESTABLISHED;
}

static bool sj_canonical_checked(Arena *a, Space *space, Atom *term,
                                 Atom *expected, Atom **canonical_out,
                                 CettaNikOutcomeV1 *outcome_out) {
    CettaPrimeTypingCheckingCandidateV1 candidate = {
        .term = term, .expected_type = expected,
        .steps_limited = false, .steps = 0u};
    CettaPrimeTypingCheckingObservationV1 observation;
    memset(&observation, 0, sizeof observation);
    if (!cetta_prime_typing_observe_checking_v1(a, space, &candidate, &observation))
        return false;
    if (outcome_out)
        *outcome_out = observation.authority.result.kind == CETTA_NIK_RESULT_OUTCOME
                           ? observation.authority.result.value.outcome
                           : CETTA_NIK_OUTCOME_INCOMPLETE;
    if (!sj_outcome_established(&observation.authority)) return false;
    if (canonical_out) *canonical_out = observation.authority.canonical_term;
    return observation.authority.canonical_term != NULL;
}

static bool sj_canonical_synthesized(Arena *a, Space *space, Atom *term,
                                     Atom **canonical_out, Atom **type_out) {
    CettaPrimeTypingSynthesisCandidateV1 candidate = {
        .term = term, .steps_limited = false, .steps = 0u};
    CettaPrimeTypingSynthesisObservationV1 observation;
    memset(&observation, 0, sizeof observation);
    if (!cetta_prime_typing_observe_synthesis_v1(a, space, &candidate, &observation))
        return false;
    if (!sj_outcome_established(&observation.authority)) return false;
    if (canonical_out) *canonical_out = observation.authority.canonical_term;
    Atom *payload = observation.authority.payload;
    if (type_out) {
        *type_out = NULL;
        if (payload && payload->kind == ATOM_EXPR && payload->expr.len == 2u)
            *type_out = payload->expr.elems[1];
    }
    return observation.authority.canonical_term != NULL;
}

/* ------------------------------------------------------------------------ */
/* `set:` term judgments: one run of the `type:` judgment in the extended     */
/* space, guarded by the syntactic descent check.  The verdict is returned   */
/* as the authority produced it; no evidence is added on a plain call.        */
/* ------------------------------------------------------------------------ */

typedef enum {
    SJ_SET_FORMED,
    SJ_SET_OF,
    SJ_SET_CHECK,
    SJ_SET_EQ
} SjSetTermKind;

static Atom *sj_set_term_judgment(Arena *a, Space *space, Atom *judgment,
                                  SjSetTermKind kind, bool limited,
                                  uint64_t steps) {
    static const char *const TYPE_HEADS[] = {
        "type:formed", "type:of", "type:check", "type:eq"};
    CettaExprLen expected_len = (kind == SJ_SET_FORMED || kind == SJ_SET_OF)
                                    ? 2u : 3u;
    if (!judgment || judgment->kind != ATOM_EXPR ||
        judgment->expr.len != expected_len)
        return sj_refuted(a, judgment, sj_expr1(a, "set:arity"));
    SjSetEnv *env = sj_env(space);
    if (!env)
        return sj_undetermined(a, judgment, sj_expr1(a, "set:signature-unavailable"));

    for (CettaExprIndex i = 1u; i < expected_len; i++) {
        Atom *operand = judgment->expr.elems[i];
        bool as_type = kind == SJ_SET_FORMED || (kind == SJ_SET_CHECK && i == 2u);
        bool simple = as_type ? sj_authored_simple_type(env, operand)
                              : sj_authored_simple_term(env, operand);
        if (!simple)
            return sj_undetermined(
                a, judgment, sj_expr2(a, "set:outside-simple-fragment", operand));
    }

    Atom *type_judgment = expected_len == 2u
        ? sj_expr2(a, TYPE_HEADS[kind], judgment->expr.elems[1])
        : sj_expr3(a, TYPE_HEADS[kind], judgment->expr.elems[1],
                   judgment->expr.elems[2]);
    Atom *verdict = prime_semantics_judge_typing_direct(
        a, &env->overlay, type_judgment, limited, steps);
    if (kind != SJ_SET_OF) return verdict;

    Atom *evidence = NULL;
    if (sj_verdict_status(verdict, &evidence) != SJ_STATUS_ESTABLISHED)
        return verdict;
    Atom *type = evidence && evidence->kind == ATOM_EXPR && evidence->expr.len == 2u
                     ? evidence->expr.elems[1] : NULL;
    if (!type || !sj_simple_canonical_type(type))
        return sj_undetermined(
            a, judgment, sj_expr2(a, "set:outside-simple-fragment", judgment->expr.elems[1]));
    Atom *value = cetta_prime_regular_term_quote_intrinsic_v1(a, type);
    return sj_established(a, judgment,
                          sj_expr2(a, "PrimeScopedValue", value ? value : type));
}

/* ------------------------------------------------------------------------ */
/* Term utilities over the declared canonical spelling: shift, substitution, */
/* and a structural beta view used to read the head of a goal.  Conversion   */
/* decisions belong exclusively to the tower kernel.  The structural view   */
/* exposes goal heads but cannot establish or refute conversion when the     */
/* kernel declines or exhausts its allowance.                               */
/* ------------------------------------------------------------------------ */

static Atom *sj_rebuild(Arena *a, Atom *t, Atom **children) {
    Atom **items = arena_alloc(a, sizeof(Atom *) * (size_t)t->expr.len);
    items[0] = t->expr.elems[0];
    for (CettaExprIndex i = 1u; i < t->expr.len; i++) items[i] = children[i];
    return atom_expr(a, items, t->expr.len);
}

static bool sj_binds_body(Atom *t, CettaExprIndex i) {
    if (sj_is_expr(t, "Lam", 2u)) return i == 1u;
    if (sj_is_expr(t, "Pi", 3u) || sj_is_expr(t, "Sigma", 3u) ||
        sj_is_expr(t, "Lam", 3u))
        return i == 2u;
    return false;
}

static bool sj_is_leaf(Atom *t) {
    return !t || t->kind != ATOM_EXPR || t->expr.len == 0u ||
           t->expr.len > 8u || sj_is_sort(t) ||
           (t->expr.len >= 2u && atom_is_symbol(t->expr.elems[0], "DeclConst"));
}

static Atom *sj_shift(Arena *a, Atom *t, uint64_t cutoff, int64_t amount) {
    if (!t) return NULL;
    uint64_t index = 0u;
    if (sj_intrinsic_index(t, &index)) {
        if (index < cutoff) return t;
        int64_t shifted = (int64_t)index + amount;
        if (shifted < 0) return NULL;
        return sj_expr2(a, "idx", atom_int(a, shifted));
    }
    if (sj_is_leaf(t)) return t;
    Atom *children[8] = {0};
    for (CettaExprIndex i = 1u; i < t->expr.len; i++) {
        bool under = sj_binds_body(t, i);
        children[i] = sj_shift(a, t->expr.elems[i], under ? cutoff + 1u : cutoff,
                               amount);
        if (!children[i]) return NULL;
    }
    return sj_rebuild(a, t, children);
}

/* Substitute `value` for index `depth` in `t`, lowering higher indices. */
static Atom *sj_subst(Arena *a, Atom *t, uint64_t depth, Atom *value) {
    if (!t) return NULL;
    uint64_t index = 0u;
    if (sj_intrinsic_index(t, &index)) {
        if (index == depth) return sj_shift(a, value, 0u, (int64_t)depth);
        if (index > depth)
            return sj_expr2(a, "idx", atom_int(a, (int64_t)index - 1));
        return t;
    }
    if (sj_is_leaf(t)) return t;
    Atom *children[8] = {0};
    for (CettaExprIndex i = 1u; i < t->expr.len; i++) {
        bool under = sj_binds_body(t, i);
        children[i] = sj_subst(a, t->expr.elems[i], under ? depth + 1u : depth,
                               value);
        if (!children[i]) return NULL;
    }
    return sj_rebuild(a, t, children);
}

static Atom *sj_lambda_body(Atom *f) {
    if (sj_is_expr(f, "Lam", 3u)) return f->expr.elems[2];
    if (sj_is_expr(f, "Lam", 2u)) return f->expr.elems[1];
    return NULL;
}

static Atom *sj_whnf(Arena *a, Atom *t, unsigned *fuel);
static size_t sj_spine(Atom *t, Atom **head_out, Atom **args, size_t cap);
static bool g_sj_no_delta = false;

static SjDefinition *sj_definition_of(Atom *head) {
    if (sj_is_expr(head, "DeclConst", 2u)) head = head->expr.elems[1];
    if (!head || head->kind != ATOM_SYMBOL || !g_set_env.overlay_ready) return NULL;
    for (size_t i = 0u; i < g_set_env.def_count; i++)
        if (atom_eq(g_set_env.defs[i].name, head)) return &g_set_env.defs[i];
    return NULL;
}

/* First-order matching of a compiled pattern against an argument; a
 * constructor pattern looks at the argument's weak head normal form. */
static bool sj_match_rule(Arena *a, Atom *pat, Atom *arg, Atom **sols,
                          size_t nvars, unsigned *fuel) {
    uint64_t index = 0u;
    if (sj_intrinsic_index(pat, &index)) {
        if (index >= nvars) return false;
        size_t j = nvars - 1u - (size_t)index;
        if (!sols[j]) {
            sols[j] = arg;
            return true;
        }
        return atom_eq(sols[j], arg);
    }
    Atom *w = sj_whnf(a, arg, fuel);
    if (!w) return false;
    if (sj_is_leaf(pat)) {
        Atom *pattern_name = sj_is_expr(pat, "DeclConst", 2u) ? pat->expr.elems[1] : pat;
        Atom *argument_name = sj_is_expr(w, "DeclConst", 2u) ? w->expr.elems[1] : w;
        return atom_eq(pattern_name, argument_name);
    }
    if (!sj_is_expr(pat, "App", 3u) || !sj_is_expr(w, "App", 3u)) return false;
    return sj_match_rule(a, pat->expr.elems[1], w->expr.elems[1], sols, nvars, fuel) &&
           sj_match_rule(a, pat->expr.elems[2], w->expr.elems[2], sols, nvars, fuel);
}

/* Instantiate a rule's right-hand side: pattern index k (innermost 0) at
 * depth d stands for solution nvars-1-(k-d). */
static Atom *sj_instantiate_rule(Arena *a, Atom *t, Atom **sols, size_t nvars,
                                 uint64_t depth) {
    if (!t) return NULL;
    uint64_t index = 0u;
    if (sj_intrinsic_index(t, &index)) {
        if (index < depth) return t;
        uint64_t k = index - depth;
        if (k < nvars) return sj_shift(a, sols[nvars - 1u - (size_t)k], 0u, (int64_t)depth);
        return sj_expr2(a, "idx", atom_int(a, (int64_t)(index - nvars)));
    }
    if (sj_is_leaf(t)) return t;
    Atom *children[8] = {0};
    for (CettaExprIndex i = 1u; i < t->expr.len; i++) {
        bool under = sj_binds_body(t, i);
        children[i] = sj_instantiate_rule(a, t->expr.elems[i], sols, nvars,
                                          under ? depth + 1u : depth);
        if (!children[i]) return NULL;
    }
    return sj_rebuild(a, t, children);
}

/* One delta step at the head: unfold a defined constant applied to at least
 * its arity by the first rule whose patterns match. */
static Atom *sj_delta(Arena *a, Atom *t, unsigned *fuel, bool *changed) {
    *changed = false;
    Atom *head = NULL;
    Atom *args[8] = {0};
    size_t argc = sj_spine(t, &head, args, 8u);
    if (argc > 8u) return t;
    SjDefinition *def = sj_definition_of(head);
    if (!def || argc < def->arity) return t;
    for (size_t r = 0u; r < def->rule_count; r++) {
        SjRule *rule = &def->rules[r];
        Atom *sols[16] = {0};
        if (rule->nvars > 16u) return t;
        bool ok = true;
        for (size_t k = 0u; k < def->arity && ok; k++)
            ok = sj_match_rule(a, rule->pats[k], args[k], sols, rule->nvars, fuel);
        if (!ok) continue;
        for (size_t j = 0u; j < rule->nvars; j++)
            if (!sols[j]) return t;
        Atom *result = sj_instantiate_rule(a, rule->rhs, sols, rule->nvars, 0u);
        if (!result) return NULL;
        for (size_t k = def->arity; k < argc; k++)
            result = sj_expr3(a, "App", result, args[k]);
        *changed = true;
        return result;
    }
    return t;
}

/* Weak head normal form by beta and, for admitted definitions, delta at the
 * head.  The signature itself carries no definitions. */
static Atom *sj_whnf(Arena *a, Atom *t, unsigned *fuel) {
    for (;;) {
        if (*fuel == 0u) return NULL;
        (*fuel)--;
        if (sj_is_expr(t, "App", 3u)) {
            Atom *f = sj_whnf(a, t->expr.elems[1], fuel);
            if (!f) return NULL;
            Atom *body = sj_lambda_body(f);
            if (body) {
                t = sj_subst(a, body, 0u, t->expr.elems[2]);
                if (!t) return NULL;
                continue;
            }
            if (f != t->expr.elems[1]) t = sj_expr3(a, "App", f, t->expr.elems[2]);
        }
        if (g_set_env.def_count == 0u || g_sj_no_delta) return t;
        bool changed = false;
        Atom *d = sj_delta(a, t, fuel, &changed);
        if (!d) return NULL;
        if (!changed) return t;
        t = d;
    }
}

/* Full beta normal form, fuel-bounded. */
static Atom *sj_normalize(Arena *a, Atom *t, unsigned *fuel) {
    Atom *head = sj_whnf(a, t, fuel);
    if (!head) return NULL;
    if (sj_is_leaf(head)) return head;
    Atom *children[8] = {0};
    for (CettaExprIndex i = 1u; i < head->expr.len; i++) {
        children[i] = sj_normalize(a, head->expr.elems[i], fuel);
        if (!children[i]) return NULL;
    }
    return sj_rebuild(a, head, children);
}

/* Spine view: (App (App c x) y) → c, [x, y]. */
static size_t sj_spine(Atom *t, Atom **head_out, Atom **args, size_t cap) {
    size_t count = 0u;
    Atom *cursor = t;
    while (sj_is_expr(cursor, "App", 3u)) {
        count++;
        cursor = cursor->expr.elems[1];
    }
    if (head_out) *head_out = cursor;
    if (count > cap) return count;
    cursor = t;
    for (size_t i = count; i > 0u; i--) {
        args[i - 1u] = cursor->expr.elems[2];
        cursor = cursor->expr.elems[1];
    }
    return count;
}

static bool sj_constant_named(Atom *t, const char *name) {
    if (atom_is_symbol(t, name)) return true;
    return t && t->kind == ATOM_EXPR && t->expr.len == 2u &&
           atom_is_symbol(t->expr.elems[0], "DeclConst") &&
           atom_is_symbol(t->expr.elems[1], name);
}

/* Kernel spelling: bare constants become `(DeclConst c)`, the tower universe
 * `(u n)` becomes `(Sort (LevelConst n))`; indices, `App`, `Lam`, `Pi` are
 * shared. */
static Atom *sj_to_kernel(Arena *a, Atom *t) {
    if (!t) return NULL;
    if (sj_is_constant(t)) return sj_expr2(a, "DeclConst", t);
    if (sj_is_expr(t, "u", 2u))
        return sj_expr2(a, "Sort", sj_expr2(a, "LevelConst", t->expr.elems[1]));
    if (sj_is_leaf(t)) return t;
    Atom *children[8] = {0};
    for (CettaExprIndex i = 1u; i < t->expr.len; i++) {
        children[i] = sj_to_kernel(a, t->expr.elems[i]);
        if (!children[i]) return NULL;
    }
    return sj_rebuild(a, t, children);
}

/* The declared-name readout and the intrinsic kernel wire share binders and
 * indices. Only a monomorphic global occurrence changes spelling here. */
static Atom *sj_from_kernel(Arena *a, Atom *term) {
    if (sj_is_expr(term, "DeclConst", 2u)) return term->expr.elems[1];
    if (sj_is_leaf(term)) return term;
    Atom *children[8] = {0};
    for (CettaExprIndex i = 1u; i < term->expr.len; i++) {
        children[i] = sj_from_kernel(a, term->expr.elems[i]);
        if (!children[i]) return NULL;
    }
    return sj_rebuild(a, term, children);
}

/* ------------------------------------------------------------------------ */
/* Bidirectional proof checking for the implicational/universal fragment.     */
/* Closed propositions and witnesses are elaborated once through the         */
/* extended `type:` space; open witnesses under proof binders use a small     */
/* simple-type synthesizer over the canonical spelling.  Dependencies are     */
/* gathered only when the caller is going to publish.                        */
/* ------------------------------------------------------------------------ */

typedef struct {
    Arena *arena;
    Space *user_space;
    SjSetEnv *env;
    Atom **hypotheses;      /* innermost last */
    Atom **hypothesis_names; /* authored names, or NULL, parallel */
    size_t hypothesis_count;
    size_t hypothesis_cap;
    Atom **binders;         /* canonical domains, outermost first */
    Atom **binder_names;    /* authored names, or NULL, parallel */
    size_t binder_count;
    size_t binder_cap;
    Atom *open_names[16];   /* binders of authored lambdas inside an open witness */
    Atom *open_types[16];
    size_t open_count;
    bool collect_dependencies;
    bool diagnostic;        /* build evidence atoms on success */
    Atom **depend_names;    /* known propositions cited by this proof */
    Atom **depend_props;
    size_t depend_count;
    size_t depend_cap;
    Atom *kernel_context;   /* signature context in kernel spelling */
    bool kernel_context_failed;
    Atom **instance_names;  /* lowered instances of polymorphic heads */
    Atom **instance_types;
    size_t instance_count;
    size_t instance_cap;
    Atom *reachable_rules;  /* ordered rules at dependency-reachable heads */
    CettaPrimeRegularKernelBudget budget;
    unsigned fuel;
    size_t kernel_decisions;
    size_t structural_decisions;
    Atom *failure;          /* first failure reason */
    bool refuted;           /* failure is a checked refutation */
    bool incomplete;        /* failure is budget exhaustion */
} SjProofState;

static void sj_fail(SjProofState *st, bool refuted, Atom *reason) {
    if (st->failure) return;
    st->failure = reason;
    st->refuted = refuted;
}

static void sj_fail_fuel(SjProofState *st, Atom *subject) {
    st->incomplete = true;
    sj_fail(st, false, sj_expr2(st->arena, "set:normalization-fuel", subject));
}

static Atom **sj_grow(Arena *a, Atom **items, size_t count, size_t *cap) {
    if (count < *cap) return items;
    size_t next = *cap ? *cap * 2u : 8u;
    Atom **grown = arena_alloc(a, sizeof(Atom *) * next);
    for (size_t i = 0u; i < count; i++) grown[i] = items[i];
    *cap = next;
    return grown;
}

static void sj_push_hypothesis(SjProofState *st, Atom *prop, Atom *name) {
    if (st->hypothesis_count == st->hypothesis_cap) {
        size_t cap = st->hypothesis_cap;
        st->hypotheses = sj_grow(st->arena, st->hypotheses, st->hypothesis_count, &cap);
        st->hypothesis_names = sj_grow(st->arena, st->hypothesis_names,
                                       st->hypothesis_count, &st->hypothesis_cap);
    }
    st->hypothesis_names[st->hypothesis_count] = name;
    st->hypotheses[st->hypothesis_count++] = prop;
}

static void sj_push_binder(SjProofState *st, Atom *domain, Atom *name) {
    if (st->binder_count == st->binder_cap) {
        size_t cap = st->binder_cap;
        st->binders = sj_grow(st->arena, st->binders, st->binder_count, &cap);
        st->binder_names = sj_grow(st->arena, st->binder_names, st->binder_count,
                                   &st->binder_cap);
    }
    st->binder_names[st->binder_count] = name;
    st->binders[st->binder_count++] = domain;
}

static void sj_note_dependency(SjProofState *st, Atom *name, Atom *prop) {
    if (!st->collect_dependencies) return;
    for (size_t i = 0u; i < st->depend_count; i++)
        if (atom_eq(st->depend_names[i], name)) return;
    if (st->depend_count == st->depend_cap) {
        size_t next = st->depend_cap ? st->depend_cap * 2u : 8u;
        Atom **names = arena_alloc(st->arena, sizeof(Atom *) * next);
        Atom **props = arena_alloc(st->arena, sizeof(Atom *) * next);
        for (size_t i = 0u; i < st->depend_count; i++) {
            names[i] = st->depend_names[i];
            props[i] = st->depend_props[i];
        }
        st->depend_names = names;
        st->depend_props = props;
        st->depend_cap = next;
    }
    st->depend_names[st->depend_count] = name;
    st->depend_props[st->depend_count] = prop;
    st->depend_count++;
}

/* Canonical type of a signature constant, from one synthesis observation,
 * cached in the environment for the life of its overlay. */
static void sj_constant_cache_add(SjSetEnv *env, Atom *name, Atom *type);

static Atom *sj_constant_type(SjProofState *st, Atom *name) {
    SjSetEnv *env = st->env;
    for (size_t i = 0u; i < env->constant_count; i++)
        if (atom_eq(env->constants[i].name, name)) return env->constants[i].type;
    Atom *canonical = NULL;
    Atom *type = NULL;
    Arena *a = &env->arena;
    if (!sj_canonical_synthesized(a, &env->overlay, name, &canonical, &type) || !type)
        return NULL;
    sj_constant_cache_add(env, name, type);
    return type;
}

static void sj_constant_cache_add(SjSetEnv *env, Atom *name, Atom *type) {
    Arena *a = &env->arena;
    if (env->constant_count == env->constant_cap) {
        size_t next = env->constant_cap ? env->constant_cap * 2u : 16u;
        SjConstantType *grown = arena_alloc(a, sizeof(*grown) * next);
        for (size_t i = 0u; i < env->constant_count; i++)
            grown[i] = env->constants[i];
        env->constants = grown;
        env->constant_cap = next;
    }
    /* A cache entry outlives the request arena that supplied its name and,
     * for a private declaration, its formed type. Own both in the overlay. */
    env->constants[env->constant_count++] = (SjConstantType){
        atom_deep_copy(a, name), atom_deep_copy(a, type)};
}

static void sj_note_instance(SjProofState *st, Atom *name, Atom *type) {
    for (size_t i = 0u; i < st->instance_count; i++)
        if (atom_eq(st->instance_names[i], name)) return;
    if (st->instance_count == st->instance_cap) {
        size_t next = st->instance_cap ? st->instance_cap * 2u : 8u;
        Atom **names = arena_alloc(st->arena, sizeof(Atom *) * next);
        Atom **types = arena_alloc(st->arena, sizeof(Atom *) * next);
        for (size_t i = 0u; i < st->instance_count; i++) {
            names[i] = st->instance_names[i];
            types[i] = st->instance_types[i];
        }
        st->instance_names = names;
        st->instance_types = types;
        st->instance_cap = next;
    }
    st->instance_names[st->instance_count] = name;
    st->instance_types[st->instance_count] = type;
    st->instance_count++;
}

/* The simple instance `all@A : (A → prop) → prop` or `eq@A : A → A → prop`
 * of a polymorphic head at a canonical domain, noted for the context. */
static Atom *sj_declare_mentioned(SjProofState *st, Atom *term, Atom *context);

static Atom *sj_instance_constant(SjProofState *st, const char *poly,
                                  Atom *domain, Atom **type_out) {
    Arena *a = st->arena;
    /* The domain's constants are declared before the instance that
     * mentions them. */
    sj_declare_mentioned(st, domain, NULL);
    char *shown = atom_to_string(a, domain);
    size_t length = strlen(poly) + 1u + (shown ? strlen(shown) : 0u) + 1u;
    char *name = arena_alloc(a, length);
    snprintf(name, length, "%s@%s", poly, shown ? shown : "");
    Atom *instance = sj_sym(a, name);
    Atom *prop = sj_sym(a, "prop");
    Atom *type = strcmp(poly, "all") == 0
        ? sj_expr3(a, "Pi", sj_expr3(a, "Pi", domain, prop), prop)
        : sj_expr3(a, "Pi", domain, sj_expr3(a, "Pi", domain, prop));
    sj_note_instance(st, instance, type);
    *type_out = type;
    return instance;
}

/* Kernel spelling with the instance chart: `(all A F)` becomes an application
 * of the simple constant `all@A` of type `(A → prop) → prop`, and `(eq A x y)`
 * of `eq@A` of type `A → A → prop`.  The higher-order term stays the source;
 * the chart exists so that the kernel can own the conversion. */
static Atom *sj_to_kernel_chart(SjProofState *st, Atom *t) {
    Arena *a = st->arena;
    if (!t) return NULL;
    Atom *head = NULL;
    Atom *args[4] = {0};
    size_t argc = sj_spine(t, &head, args, 4u);
    const char *poly = sj_constant_named(head, "all") ? "all"
                     : sj_constant_named(head, "eq") ? "eq" : NULL;
    if (poly && argc >= 1u && argc <= 4u) {
        Atom *type = NULL;
        Atom *term = sj_expr2(a, "DeclConst",
                              sj_instance_constant(st, poly, args[0], &type));
        for (size_t i = 1u; i < argc; i++) {
            Atom *arg = sj_to_kernel_chart(st, args[i]);
            if (!arg) return NULL;
            term = sj_expr3(a, "App", term, arg);
        }
        return term;
    }
    if (sj_is_constant(t)) return sj_expr2(a, "DeclConst", t);
    if (sj_is_expr(t, "u", 2u))
        return sj_expr2(a, "Sort", sj_expr2(a, "LevelConst", t->expr.elems[1]));
    if (sj_is_leaf(t)) return t;
    Atom *children[8] = {0};
    for (CettaExprIndex i = 1u; i < t->expr.len; i++) {
        children[i] = sj_to_kernel_chart(st, t->expr.elems[i]);
        if (!children[i]) return NULL;
    }
    return sj_rebuild(a, t, children);
}

/* Signature context in kernel spelling, built once per proof.  Polymorphic
 * heads are omitted; their instances are added per conversion. */
static Atom *sj_kernel_context(SjProofState *st) {
    Arena *a = st->arena;
    if (st->kernel_context || st->kernel_context_failed) return st->kernel_context;
    Atom *context = sj_sym(a, "PrimeCtxNil");
    for (size_t i = 0u; i < st->env->signature_count; i++) {
        Atom *decl = st->env->signature[i];
        Atom *type = sj_constant_type(st, decl->expr.elems[1]);
        if (sj_env_is_poly_head(st->env, decl->expr.elems[1])) continue;
        Atom *kernel_type = type ? sj_to_kernel(a, type) : NULL;
        if (!kernel_type) {
            st->kernel_context_failed = true;
            return NULL;
        }
        Atom *items[4] = {sj_sym(a, "PrimeCtxDecl"),
                          sj_expr2(a, "DeclConst", decl->expr.elems[1]),
                          kernel_type, context};
        context = atom_expr(a, items, 4u);
    }
    st->kernel_context = context;
    return context;
}

/* Constants of the extended space mentioned by a canonical term that are not
 * signature constants: each is declared once in the kernel context with its
 * canonical type, so that user theories convert under the kernel too. */
static bool sj_context_declares(SjProofState *st, Atom *name) {
    for (size_t i = 0u; i < st->env->signature_count; i++)
        if (atom_eq(st->env->signature[i]->expr.elems[1], name)) return true;
    for (size_t i = 0u; i < st->instance_count; i++)
        if (atom_eq(st->instance_names[i], name)) return true;
    return false;
}

static Atom *sj_declare_mentioned(SjProofState *st, Atom *term, Atom *context) {
    Arena *a = st->arena;
    if (!term) return context;
    if (sj_is_constant(term)) {
        if (sj_context_declares(st, term) || sj_env_is_poly_head(st->env, term))
            return context;
        Atom *type = sj_constant_type(st, term);
        Atom *kernel_type = type ? sj_to_kernel(a, type) : NULL;
        if (!kernel_type) return context;
        /* The constants its type mentions are declared first, so that the
         * context lists every declaration after the ones it depends on. */
        sj_declare_mentioned(st, type, context);
        /* Record it as an instance so it is declared once per conversion. */
        sj_note_instance(st, term, type);
        return context;
    }
    if (term->kind != ATOM_EXPR) return context;
    for (CettaExprIndex i = 0u; i < term->expr.len; i++)
        context = sj_declare_mentioned(st, term->expr.elems[i], context);
    return context;
}

/* The type of the head of an application, for annotation: a constant's
 * canonical type, a polymorphic head's instance type, or a bound
 * variable's domain. */
static Atom *sj_head_type(SjProofState *st, Atom *head, Atom *domain_arg,
                          Atom **locals, size_t local_count) {
    Arena *a = st->arena;
    uint64_t index = 0u;
    if (sj_intrinsic_index(head, &index)) {
        if (index < local_count)
            return sj_shift(a, locals[local_count - 1u - index], 0u,
                            (int64_t)index + 1);
        index -= local_count;
        if (index < st->binder_count)
            return sj_shift(a, st->binders[st->binder_count - 1u - index], 0u,
                            (int64_t)(index + local_count) + 1);
        return NULL;
    }
    const char *poly = sj_constant_named(head, "all") ? "all"
                     : sj_constant_named(head, "eq") ? "eq" : NULL;
    if (poly) {
        if (!domain_arg) return NULL;
        Atom *prop = sj_sym(a, "prop");
        return strcmp(poly, "all") == 0
            ? sj_expr3(a, "Pi", sj_expr3(a, "Pi", domain_arg, prop), prop)
            : sj_expr3(a, "Pi", domain_arg, sj_expr3(a, "Pi", domain_arg, prop));
    }
    if (sj_is_constant(head)) return sj_constant_type(st, head);
    return NULL;
}

/* Recover an annotation hint from the simple context and application
 * spine.  This does not establish typing: the conversion kernel checks
 * the annotated operands.  In particular, a substituted lambda at the
 * head of a beta redex gets its domain from the argument's type. */
static Atom *sj_annotation_type(SjProofState *st, Atom *term, Atom **locals,
                                 size_t local_count) {
    Atom *head = NULL;
    Atom *args[8] = {0};
    size_t argc = sj_spine(term, &head, args, 8u);
    if (argc > 8u) return NULL;
    bool poly = sj_constant_named(head, "all") || sj_constant_named(head, "eq");
    Atom *type = sj_head_type(st, head, poly && argc ? args[0] : NULL,
                              locals, local_count);
    if (!type && sj_is_expr(head, "Lam", 3u) && local_count < 32u) {
        locals[local_count] = head->expr.elems[1];
        Atom *body_type = sj_annotation_type(st, head->expr.elems[2],
                                             locals, local_count + 1u);
        if (body_type) type = sj_expr3(st->arena, "Pi", head->expr.elems[1], body_type);
    }
    for (size_t i = poly ? 1u : 0u; type && i < argc; i++)
        type = sj_is_expr(type, "Pi", 3u)
            ? sj_subst(st->arena, type->expr.elems[2], 0u, args[i]) : NULL;
    return type;
}

static Atom *sj_annotate(SjProofState *st, Atom *t, Atom **locals,
                         size_t local_count);

/* A checking domain supplies every lambda domain in a nested witness.
 * Annotation is syntax preparation; the native kernel still checks it. */
static Atom *sj_annotate_at(SjProofState *st, Atom *term, Atom *type,
                            Atom **locals, size_t local_count) {
    if (local_count < 32u && sj_is_expr(type, "Pi", 3u) &&
        (sj_is_expr(term, "Lam", 2u) || sj_is_expr(term, "Lam", 3u))) {
        Atom *domain = sj_is_expr(term, "Lam", 3u)
            ? term->expr.elems[1] : type->expr.elems[1];
        locals[local_count] = domain;
        Atom *body = sj_annotate_at(st, sj_lambda_body(term),
            type->expr.elems[2], locals, local_count + 1u);
        return body ? sj_expr3(st->arena, "Lam", domain, body) : NULL;
    }
    return sj_annotate(st, term, locals, local_count);
}

/* Annotate every lambda with its domain where the surrounding application
 * determines it, so that both operands of a conversion carry the same
 * spelling and the kernel can infer the redexes they form. */
static Atom *sj_annotate(SjProofState *st, Atom *t, Atom **locals,
                         size_t local_count) {
    Arena *a = st->arena;
    if (!t || sj_is_leaf(t)) return t;
    if (local_count >= 32u) return t;
    if (sj_is_expr(t, "Lam", 3u)) {
        locals[local_count] = t->expr.elems[1];
        Atom *body = sj_annotate(st, t->expr.elems[2], locals, local_count + 1u);
        return body ? sj_expr3(a, "Lam", t->expr.elems[1], body) : NULL;
    }
    if (sj_is_expr(t, "Lam", 2u)) {
        locals[local_count] = NULL;
        Atom *body = sj_annotate(st, t->expr.elems[1], locals, local_count + 1u);
        return body ? sj_expr2(a, "Lam", body) : NULL;
    }
    if (sj_is_expr(t, "App", 3u)) {
        Atom *head = NULL;
        Atom *args[8] = {0};
        size_t argc = sj_spine(t, &head, args, 8u);
        if (argc > 8u) return t;
        if (argc && sj_is_expr(head, "Lam", 2u)) {
            Atom *domain = sj_annotation_type(st, args[0], locals, local_count);
            if (domain) head = sj_expr3(a, "Lam", domain, head->expr.elems[1]);
        }
        bool poly = sj_constant_named(head, "all") || sj_constant_named(head, "eq");
        Atom *type = sj_head_type(st, head, poly && argc >= 1u ? args[0] : NULL,
                                  locals, local_count);
        Atom *term = poly ? sj_expr3(a, "App", head, args[0])
                          : sj_annotate(st, head, locals, local_count);
        if (!term) return NULL;
        for (size_t i = poly ? 1u : 0u; i < argc; i++) {
            Atom *param = sj_is_expr(type, "Pi", 3u) ? type->expr.elems[1] : NULL;
            Atom *arg = args[i];
            arg = sj_annotate_at(st, arg, param, locals, local_count);
            if (!arg) return NULL;
            term = sj_expr3(a, "App", term, arg);
            type = sj_is_expr(type, "Pi", 3u)
                 ? sj_subst(a, type->expr.elems[2], 0u, arg) : NULL;
        }
        return term;
    }
    Atom *children[8] = {0};
    for (CettaExprIndex i = 1u; i < t->expr.len; i++) {
        children[i] = sj_annotate(st, t->expr.elems[i], locals, local_count);
        if (!children[i]) return NULL;
    }
    return sj_rebuild(a, t, children);
}

/* Constants mentioned by a term already in kernel spelling. */
static void sj_declare_kernel_mentioned(SjProofState *st, Atom *t, Atom *context) {
    if (!t || t->kind != ATOM_EXPR) return;
    if (sj_is_expr(t, "DeclConst", 2u)) {
        sj_declare_mentioned(st, t->expr.elems[1], context);
        return;
    }
    for (CettaExprIndex i = 0u; i < t->expr.len; i++)
        sj_declare_kernel_mentioned(st, t->expr.elems[i], context);
}

/* Close the request's declarations under types, rule patterns and right-hand
 * sides. Definitions can introduce specialized quantifiers whose types must
 * come from the source chart, not from parsing a generated constant name.
 * Only selected heads contribute edges. Cycles stop when no new declaration
 * is discovered; the source rule order and all alternatives at a head remain
 * unchanged. This is syntactic support, not a minimal dynamic read trace. */
static bool sj_declare_rule_dependencies(SjProofState *st, Atom *context) {
    size_t previous;
    do {
        previous = st->instance_count;
        for (size_t d = 0u; d < st->env->def_count; d++) {
            SjDefinition *def = &st->env->defs[d];
            if (!sj_context_declares(st, def->name)) continue;
            for (size_t r = 0u; r < def->rule_count; r++) {
                Atom *rhs = sj_to_kernel_chart(st, def->rules[r].rhs);
                if (!rhs) return false;
                sj_declare_kernel_mentioned(st, rhs, context);
                for (size_t k = 0u; k < def->arity; k++) {
                    Atom *pat = sj_to_kernel_chart(st, def->rules[r].pats[k]);
                    if (!pat) return false;
                    sj_declare_kernel_mentioned(st, pat, context);
                }
            }
        }
        for (Atom *r = st->env->kernel_rules; sj_is_expr(r, "LCons", 3u);
             r = r->expr.elems[2]) {
            Atom *rule = r->expr.elems[1];
            if (!sj_is_expr(rule, "PrimeRule", 5u) ||
                !sj_context_declares(st, rule->expr.elems[1])) continue;
            sj_declare_kernel_mentioned(st, rule->expr.elems[3], context);
            sj_declare_kernel_mentioned(st, rule->expr.elems[4], context);
        }
    } while (st->instance_count != previous);

    Atom **rules = NULL;
    size_t count = 0u, capacity = 0u;
    for (Atom *r = st->env->kernel_rules; sj_is_expr(r, "LCons", 3u);
         r = r->expr.elems[2]) {
        Atom *rule = r->expr.elems[1];
        if (!sj_is_expr(rule, "PrimeRule", 5u) ||
            !sj_context_declares(st, rule->expr.elems[1])) continue;
        if (count == capacity)
            rules = sj_grow(st->arena, rules, count, &capacity);
        rules[count++] = rule;
    }
    Atom *selected = sj_sym(st->arena, "LNil");
    for (size_t i = count; i > 0u; i--)
        selected = sj_expr3(st->arena, "LCons", rules[i - 1u], selected);
    st->reachable_rules = selected;
    return true;
}

/* Kernel-owned conversion under the current proof binders.  Declining an
 * operand, exhausting the budget, and refuting conversion are distinct. */
static CettaPrimeRegularKernelConversionDecision sj_kernel_convertible(
    SjProofState *st, Atom *left, Atom *right) {
    Arena *a = st->arena;
    CettaPrimeRegularKernelConversionDecision outside = {
        .status = CETTA_PRIME_REGULAR_KERNEL_OUT_OF_CLASS,
        .reason = "conversion-chart-unavailable",
    };
    Atom *context = sj_kernel_context(st);
    if (!context) return outside;
    Atom *locals[32] = {0};
    left = sj_annotate(st, left, locals, 0u);
    right = sj_annotate(st, right, locals, 0u);
    if (!left || !right) return outside;
    sj_declare_mentioned(st, left, context);
    sj_declare_mentioned(st, right, context);
    for (size_t i = 0u; i < st->binder_count; i++)
        sj_declare_mentioned(st, st->binders[i], context);
    Atom *kl = sj_to_kernel_chart(st, left);
    Atom *kr = sj_to_kernel_chart(st, right);
    if (!kl || !kr) return outside;
    /* Charting the operands can add specialized heads; close after charting. */
    if (!sj_declare_rule_dependencies(st, context)) return outside;
    for (size_t i = 0u; i < st->instance_count; i++) {
        Atom *type = sj_to_kernel(a, st->instance_types[i]);
        if (!type) return outside;
        Atom *items[4] = {sj_sym(a, "PrimeCtxDecl"),
                          sj_expr2(a, "DeclConst", st->instance_names[i]),
                          type, context};
        context = atom_expr(a, items, 4u);
    }
    for (size_t i = 0u; i < st->binder_count; i++) {
        Atom *domain = sj_to_kernel_chart(st, st->binders[i]);
        if (!domain) return outside;
        context = sj_expr3(a, "PrimeCtxCons", domain, context);
    }
    cetta_prime_regular_kernel_rules_set(st->reachable_rules);
    CettaPrimeRegularKernelConversionDecision decision =
        cetta_prime_regular_kernel_decide_intrinsic_conversion_v1(
        a, context, kl, kr, &st->budget);
    cetta_prime_regular_kernel_rules_set(NULL);
    return decision;
}

static Atom *sj_unannotate(Arena *a, Atom *t);

static bool sj_convertible(SjProofState *st, Atom *left, Atom *right) {
    /* The kernel computes with the space's admitted rules, definitions and
     * recursors alike, so conversion with definitions is one decision. */
    CettaPrimeRegularKernelConversionDecision decision =
        sj_kernel_convertible(st, left, right);
    if (decision.status == CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED) {
        st->incomplete = true;
        sj_fail(st, false, sj_expr2(st->arena, "set:conversion-incomplete",
            sj_sym(st->arena, decision.reason ? decision.reason : "kernel-budget")));
        return false;
    }
    if (!decision.operands_admitted ||
        (decision.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED &&
         decision.status != CETTA_PRIME_REGULAR_KERNEL_REFUTED)) {
        sj_fail(st, false, sj_expr2(st->arena, "set:conversion-undetermined",
            sj_sym(st->arena, decision.reason ? decision.reason : "kernel-declined")));
        return false;
    }
    st->kernel_decisions++;
    if (decision.status == CETTA_PRIME_REGULAR_KERNEL_REFUTED) {
        sj_fail(st, true, sj_expr3(st->arena, "set:proposition-mismatch", left, right));
        return false;
    }
    return true;
}

/* Elaborate a closed authored term at an authored type through the extended
 * space. */
static Atom *sj_elaborate_closed(SjProofState *st, Atom *authored,
                                 Atom *expected) {
    Atom *canonical = NULL;
    CettaNikOutcomeV1 outcome = CETTA_NIK_OUTCOME_INCOMPLETE;
    if (sj_canonical_checked(st->arena, &st->env->overlay, authored, expected,
                             &canonical, &outcome))
        return canonical;
    if (outcome == CETTA_NIK_OUTCOME_REFUTED)
        sj_fail(st, true, sj_expr3(st->arena, "set:not-a-term-of", authored,
                                   expected));
    else
        sj_fail(st, false, sj_expr3(st->arena, "set:elaboration-undetermined",
                                    authored, expected));
    return NULL;
}

static bool sj_mentions_proof_var(SjProofState *st, Atom *t) {
    if (!t) return false;
    if (t->kind == ATOM_SYMBOL || t->kind == ATOM_VAR) {
        for (size_t i = 0u; i < st->binder_count; i++)
            if (st->binder_names[i] && atom_eq(st->binder_names[i], t)) return true;
        return false;
    }
    if (t->kind != ATOM_EXPR) return false;
    if (sj_is_expr(t, "pf:var", 2u)) return true;
    for (CettaExprIndex i = 0u; i < t->expr.len; i++)
        if (sj_mentions_proof_var(st, t->expr.elems[i])) return true;
    return false;
}

/* Open witness synthesis in the simple fragment: `(pf:var n)` is the n-th
 * enclosing proof binder (innermost 0); constants carry their signature
 * types; application is curried and non-dependent. */
static Atom *sj_open_check(SjProofState *st, Atom *w, Atom *domain);

/* Canonical spelling of an authored simple type inside an open witness. */
static Atom *sj_open_domain(SjProofState *st, Atom *t) {
    Arena *a = st->arena;
    if (!t) return NULL;
    if (t->kind == ATOM_SYMBOL) return t;
    if (t->kind == ATOM_EXPR && t->expr.len >= 3u && atom_is_symbol(t->expr.elems[0], "->")) {
        Atom *cod = sj_open_domain(st, t->expr.elems[t->expr.len - 1u]);
        if (!cod) return NULL;
        for (CettaExprIndex i = t->expr.len - 1u; i > 1u; i--) {
            Atom *dom = sj_open_domain(st, t->expr.elems[i - 1u]);
            if (!dom) return NULL;
            cod = sj_expr3(a, "Pi", dom, cod);
        }
        return cod;
    }
    return NULL;
}

static Atom *sj_open_synth(SjProofState *st, Atom *w, Atom **type_out) {
    Arena *a = st->arena;
    if (sj_is_expr(w, "pf:var", 2u)) {
        Atom *n = w->expr.elems[1];
        if (!n || n->kind != ATOM_GROUNDED || n->ground.gkind != GV_INT ||
            n->ground.ival < 0 || (uint64_t)n->ground.ival >= st->binder_count) {
            sj_fail(st, true, sj_expr2(a, "set:binder-index", w));
            return NULL;
        }
        *type_out = st->binders[st->binder_count - 1u - (size_t)n->ground.ival];
        return sj_expr2(a, "idx", atom_int(a, n->ground.ival +
                                               (int64_t)st->open_count));
    }
    if (w && (w->kind == ATOM_SYMBOL || w->kind == ATOM_VAR)) {
        for (size_t i = st->open_count; i > 0u; i--) {
            if (!atom_eq(st->open_names[i - 1u], w)) continue;
            *type_out = st->open_types[i - 1u];
            return sj_expr2(a, "idx", atom_int(a, (int64_t)(st->open_count - i)));
        }
        for (size_t i = st->binder_count; i > 0u; i--) {
            if (!st->binder_names[i - 1u] || !atom_eq(st->binder_names[i - 1u], w)) continue;
            *type_out = st->binders[i - 1u];
            return sj_expr2(a, "idx", atom_int(a, (int64_t)(st->binder_count - i + st->open_count)));
        }
    }
    if (sj_is_constant(w)) {
        Atom *type = sj_constant_type(st, w);
        if (!type) {
            sj_fail(st, false, sj_expr2(a, "set:unknown-constant", w));
            return NULL;
        }
        *type_out = type;
        return w;
    }
    if (w && w->kind == ATOM_EXPR && w->expr.len >= 2u) {
        Atom *type = NULL;
        Atom *term = NULL;
        CettaExprIndex first = 1u;
        const char *poly = atom_is_symbol(w->expr.elems[0], "all") ? "all"
                         : atom_is_symbol(w->expr.elems[0], "eq") ? "eq" : NULL;
        if (poly) {
            Atom *domain = w->expr.len >= 3u ? sj_open_domain(st, w->expr.elems[1]) : NULL;
            if (!domain) {
                sj_fail(st, false, sj_expr2(a, "set:open-witness-domain", w));
                return NULL;
            }
            /* Canonical spelling keeps the polymorphic head applied to its
             * domain; the instance is noted so the chart can lower it. */
            sj_instance_constant(st, poly, domain, &type);
            term = sj_expr3(a, "App", sj_sym(a, poly), domain);
            first = 2u;
        } else {
            Atom *head = w->expr.elems[0];
            if (sj_is_expr(head, "lam", 3u) &&
                head->expr.elems[1]->kind == ATOM_SYMBOL) {
                /* Retain a source beta-redex. The argument supplies the
                 * domain missing from the surface lambda; the ordinary
                 * native checker will check the resulting annotated term. */
                Atom *domain = NULL;
                Atom *argument = sj_open_synth(st, w->expr.elems[1], &domain);
                if (!argument) return NULL;
                if (st->open_count >= 16u) {
                    sj_fail(st, false, sj_expr2(a, "set:open-binder-depth", w));
                    return NULL;
                }
                st->open_names[st->open_count] = head->expr.elems[1];
                st->open_types[st->open_count] = domain;
                st->open_count++;
                Atom *body_type = NULL;
                Atom *body = sj_open_synth(st, head->expr.elems[2], &body_type);
                st->open_count--;
                if (!body) return NULL;
                if (!sj_simple_canonical_type(body_type)) {
                    sj_fail(st, false, sj_expr2(a, "set:open-witness-domain", w));
                    return NULL;
                }
                term = sj_expr3(a, "App", sj_expr3(a, "Lam", domain, body), argument);
                type = body_type;
                first = 2u;
            } else {
                term = sj_open_synth(st, head, &type);
            }
            if (!term) return NULL;
        }
        for (CettaExprIndex i = first; i < w->expr.len; i++) {
            if (!sj_is_expr(type, "Pi", 3u) || sj_mentions_index(type->expr.elems[2], 0u)) {
                sj_fail(st, true, sj_expr2(a, "set:witness-not-a-function", w));
                return NULL;
            }
            Atom *arg = sj_open_check(st, w->expr.elems[i], type->expr.elems[1]);
            if (!arg) return NULL;
            term = sj_expr3(a, "App", term, arg);
            type = sj_shift(a, type->expr.elems[2], 0u, -1);
            if (!type) {
                sj_fail(st, false, sj_expr2(a, "set:shift-failed", w));
                return NULL;
            }
        }
        *type_out = type;
        return term;
    }
    sj_fail(st, false, sj_expr2(a, "set:open-witness-form", w));
    return NULL;
}

/* Authored spelling of a canonical domain, for closed elaboration. */
static Atom *sj_authored_domain(SjProofState *st, Atom *domain) {
    if (domain && domain->kind == ATOM_SYMBOL) return domain;
    return cetta_prime_regular_term_quote_intrinsic_v1(st->arena, domain);
}

/* Open witness checking: `(lam y body)` against a non-dependent `(Pi A B)`
 * opens `y : A` for the body; anything else is synthesized and compared. */
static Atom *sj_open_check(SjProofState *st, Atom *w, Atom *domain) {
    Arena *a = st->arena;
    if (sj_is_expr(w, "lam", 3u) && w->expr.elems[1]->kind == ATOM_SYMBOL) {
        if (!sj_is_expr(domain, "Pi", 3u) ||
            sj_mentions_index(domain->expr.elems[2], 0u)) {
            sj_fail(st, true, sj_expr3(a, "set:witness-type", w, domain));
            return NULL;
        }
        if (st->open_count >= 16u) {
            sj_fail(st, false, sj_expr2(a, "set:open-binder-depth", w));
            return NULL;
        }
        Atom *codomain = sj_shift(a, domain->expr.elems[2], 0u, -1);
        if (!codomain) {
            sj_fail(st, false, sj_expr2(a, "set:shift-failed", w));
            return NULL;
        }
        st->open_names[st->open_count] = w->expr.elems[1];
        st->open_types[st->open_count] = domain->expr.elems[1];
        st->open_count++;
        Atom *body = sj_open_check(st, w->expr.elems[2], codomain);
        st->open_count--;
        if (!body) return NULL;
        return sj_expr3(a, "Lam", domain->expr.elems[1], body);
    }
    Atom *head = w;
    while (head && head->kind == ATOM_EXPR && head->expr.len >= 2u &&
           !sj_is_expr(head, "lam", 3u))
        head = head->expr.elems[0];
    if (w && w->kind == ATOM_EXPR && w->expr.len == 2u &&
        sj_is_expr(head, "lam", 3u)) {
        Atom *argument_type = NULL;
        Atom *argument = sj_open_synth(st, w->expr.elems[1], &argument_type);
        if (!argument) return NULL;
        if (!sj_simple_canonical_type(argument_type) ||
            !sj_simple_canonical_type(domain)) {
            sj_fail(st, false, sj_expr2(a, "set:open-witness-domain", w));
            return NULL;
        }
        Atom *function = sj_open_check(
            st, w->expr.elems[0], sj_expr3(a, "Pi", argument_type, domain));
        return function ? sj_expr3(a, "App", function, argument) : NULL;
    }
    Atom *type = NULL;
    Atom *term = sj_open_synth(st, w, &type);
    if (!term) return NULL;
    if (!atom_eq(type, domain)) {
        sj_fail(st, true, sj_expr3(a, "set:witness-type", w, domain));
        return NULL;
    }
    return term;
}

static Atom *sj_witness(SjProofState *st, Atom *w, Atom *domain) {
    if (!sj_mentions_proof_var(st, w)) {
        Atom *authored = sj_authored_domain(st, domain);
        if (!authored) {
            sj_fail(st, false, sj_expr2(st->arena, "set:domain-not-quotable", domain));
            return NULL;
        }
        return sj_elaborate_closed(st, w, authored);
    }
    return sj_open_check(st, w, domain);
}

/* `(all A F)`: the quantifier head applied to its domain and its family. */
static bool sj_quantifier(Atom *head, Atom **args, size_t argc,
                          Atom **domain_out, Atom **family_out) {
    if (argc != 2u || !sj_constant_named(head, "all")) return false;
    *domain_out = args[0];
    *family_out = args[1];
    return true;
}

static Atom *sj_synth(SjProofState *st, Atom *proof);

static bool sj_check(SjProofState *st, Atom *proof, Atom *goal);
static Atom *sj_hypothesis_named(SjProofState *st, Atom *name);
static Atom *sj_known_proposition(SjProofState *st, Atom *name);
/* `(pf:by name p1 ... pk)`: the known fact or hypothesis `name`, its
 * universal prefix instantiated by first-order matching of its conclusion
 * against the goal, its first k antecedents discharged by the given proofs.
 * Matching only finds the witnesses; acceptance is the ordinary conversion
 * of the instantiated conclusion against the goal. */
#define SJ_META_MAX 32u

static bool sj_mentions_index_below(Atom *t, uint64_t depth) {
    for (uint64_t k = 0u; k < depth; k++)
        if (sj_mentions_index(t, k)) return true;
    return false;
}

/* Fill the solved metavariables of a pattern, leaving the others. */
static Atom *sj_fill_solved(Arena *a, Atom *t, Atom **sol, uint64_t depth) {
    if (!t) return NULL;
    if (sj_is_expr(t, "pf:meta", 2u)) {
        Atom *value = sol[t->expr.elems[1]->ground.ival];
        return value ? sj_shift(a, value, 0u, (int64_t)depth) : t;
    }
    if (sj_is_leaf(t)) return t;
    Atom *children[8] = {0};
    for (CettaExprIndex i = 1u; i < t->expr.len; i++) {
        bool under = sj_binds_body(t, i);
        children[i] = sj_fill_solved(a, t->expr.elems[i], sol, under ? depth + 1u : depth);
        if (!children[i]) return NULL;
    }
    return sj_rebuild(a, t, children);
}

static bool sj_has_unsolved_meta(Atom *t, Atom **sol) {
    if (!t) return false;
    if (sj_is_expr(t, "pf:meta", 2u)) return sol[t->expr.elems[1]->ground.ival] == NULL;
    if (t->kind != ATOM_EXPR) return false;
    for (CettaExprIndex i = 0u; i < t->expr.len; i++)
        if (sj_has_unsolved_meta(t->expr.elems[i], sol)) return true;
    return false;
}

static bool sj_has_solved_meta(Atom *t, Atom **sol) {
    if (!t) return false;
    if (sj_is_expr(t, "pf:meta", 2u)) return sol[t->expr.elems[1]->ground.ival] != NULL;
    if (t->kind != ATOM_EXPR) return false;
    for (CettaExprIndex i = 0u; i < t->expr.len; i++)
        if (sj_has_solved_meta(t->expr.elems[i], sol)) return true;
    return false;
}

static bool sj_match_meta(SjProofState *st, Atom *pat, Atom *term, Atom **sol,
                          uint64_t depth) {
    Arena *a = st->arena;
    if (!pat || !term) return false;
    /* A pattern node that mentions a metavariable solved earlier in the
     * traversal is filled and normalized before it is matched, so that an
     * instantiated argument meets the redexes it forms, by beta or by an
     * admitted definition. */
    if (pat->kind == ATOM_EXPR && sj_has_solved_meta(pat, sol)) {
        Atom *filled = sj_fill_solved(a, pat, sol, depth);
        Atom *normal = filled ? sj_normalize(a, filled, &st->fuel) : NULL;
        if (!normal) return false;
        if (!atom_eq(normal, pat)) return sj_match_meta(st, normal, term, sol, depth);
    }
    if (sj_is_expr(pat, "pf:meta", 2u)) {
        int64_t i = pat->expr.elems[1]->ground.ival;
        if (sj_mentions_index_below(term, depth)) return false;
        Atom *lowered = depth ? sj_shift(a, term, 0u, -(int64_t)depth) : term;
        if (!lowered) return false;
        if (!sol[i]) {
            sol[i] = lowered;
            return true;
        }
        return atom_eq(sol[i], lowered);
    }
    if (pat->kind == ATOM_EXPR && term->kind == ATOM_EXPR &&
        pat->expr.len == term->expr.len) {
        for (CettaExprIndex i = 0u; i < pat->expr.len; i++) {
            bool under = i > 0u && sj_binds_body(pat, i);
            if (!sj_match_meta(st, pat->expr.elems[i], term->expr.elems[i], sol,
                               under ? depth + 1u : depth))
                return false;
        }
        return true;
    }
    return atom_eq(pat, term);
}

/* A lambda solution is annotated with the domain of the quantifier it
 * instantiates, so that the kernel can infer the redexes it forms. */
static Atom *sj_annotate_solution(Arena *a, Atom *value, Atom *domain) {
    if (sj_is_expr(value, "Lam", 2u) && sj_is_expr(domain, "Pi", 3u))
        return sj_expr3(a, "Lam", domain->expr.elems[1], value->expr.elems[1]);
    return value;
}

static Atom *sj_fill_meta(Arena *a, Atom *t, Atom **sol, Atom **domains,
                          uint64_t depth) {
    if (!t) return NULL;
    if (sj_is_expr(t, "pf:meta", 2u)) {
        int64_t i = t->expr.elems[1]->ground.ival;
        Atom *value = domains ? sj_annotate_solution(a, sol[i], domains[i]) : sol[i];
        return sj_shift(a, value, 0u, (int64_t)depth);
    }
    if (sj_is_leaf(t)) return t;
    Atom *children[8] = {0};
    for (CettaExprIndex i = 1u; i < t->expr.len; i++) {
        bool under = sj_binds_body(t, i);
        children[i] = sj_fill_meta(a, t->expr.elems[i], sol, domains, under ? depth + 1u : depth);
        if (!children[i]) return NULL;
    }
    return sj_rebuild(a, t, children);
}

/* Lambda domain annotations are a kernel convenience, not part of the
 * canonical spelling; matching compares terms without them. */
static Atom *sj_unannotate(Arena *a, Atom *t) {
    if (!t || sj_is_leaf(t)) return t;
    if (sj_is_expr(t, "Lam", 3u))
        return sj_expr2(a, "Lam", sj_unannotate(a, t->expr.elems[2]));
    Atom *children[8] = {0};
    for (CettaExprIndex i = 1u; i < t->expr.len; i++) {
        children[i] = sj_unannotate(a, t->expr.elems[i]);
        if (!children[i]) return NULL;
    }
    return sj_rebuild(a, t, children);
}

static bool sj_check_by(SjProofState *st, Atom *proof, Atom *goal) {
    Arena *a = st->arena;
    Atom *name = proof->expr.elems[1];
    size_t premise_count = proof->expr.len - 2u;
    Atom *prop0 = NULL;
    if (name->kind == ATOM_SYMBOL) {
        prop0 = sj_hypothesis_named(st, name);
        if (!prop0) prop0 = sj_known_proposition(st, name);
    } else {
        /* the major premise is a proof term: its proposition is synthesized */
        prop0 = sj_synth(st, name);
    }
    if (!prop0) return false;
    if (premise_count > 16u) {
        sj_fail(st, false, sj_expr2(a, "set:by-premise-count", name));
        return false;
    }
    /* Two passes: first the prefix is peeled without unfolding definitions,
     * so that a conclusion keeps the spelling the fact gives it and matches
     * the goal as written; if that finds no instance, the prefix is peeled
     * with definitions unfolded. */
    for (int pass = 0; pass < 2; pass++) {
        Atom *prop = prop0;
        size_t meta_count = 0u;
        Atom *meta_domains[SJ_META_MAX] = {0};
        Atom *antecedents[16] = {0};
        size_t antecedent_count = 0u;
        bool ok = true;
        for (;;) {
            g_sj_no_delta = pass == 0;
            Atom *w = sj_whnf(a, prop, &st->fuel);
            g_sj_no_delta = false;
            if (!w) {
                sj_fail_fuel(st, prop);
                return false;
            }
            Atom *head = NULL;
            Atom *args[4] = {0};
            size_t argc = sj_spine(w, &head, args, 4u);
            Atom *domain = NULL;
            Atom *family_arg = NULL;
            if (sj_quantifier(head, args, argc, &domain, &family_arg)) {
                if (meta_count >= SJ_META_MAX) {
                    sj_fail(st, false, sj_expr2(a, "set:by-prefix-depth", name));
                    return false;
                }
                Atom *meta = sj_expr2(a, "pf:meta", atom_int(a, (int64_t)meta_count));
                meta_domains[meta_count] = domain;
                meta_count++;
                g_sj_no_delta = pass == 0;
                Atom *family = sj_whnf(a, family_arg, &st->fuel);
                g_sj_no_delta = false;
                if (!family) {
                    sj_fail_fuel(st, family_arg);
                    return false;
                }
                Atom *body = sj_lambda_body(family);
                prop = body ? sj_subst(a, body, 0u, meta) : sj_expr3(a, "App", family, meta);
                if (!prop) {
                    sj_fail(st, false, sj_expr2(a, "set:shift-failed", name));
                    return false;
                }
                continue;
            }
            if (antecedent_count < premise_count && argc == 2u && sj_constant_named(head, "imp")) {
                antecedents[antecedent_count++] = args[0];
                prop = args[1];
                continue;
            }
            prop = w;
            break;
        }
        if (antecedent_count < premise_count) {
            if (pass == 0) continue;
            sj_fail(st, true, sj_expr2(a, "set:by-needs-implication", prop));
            return false;
        }
        Atom *raw_conclusion = sj_unannotate(a, prop);
        Atom *raw_target = sj_unannotate(a, goal);
        Atom *conclusion = sj_normalize(a, prop, &st->fuel);
        Atom *target = conclusion ? sj_normalize(a, goal, &st->fuel) : NULL;
        if (!conclusion || !target) {
            sj_fail_fuel(st, conclusion ? goal : prop);
            return false;
        }
        Atom *sol[SJ_META_MAX] = {0};
        conclusion = sj_unannotate(a, conclusion);
        target = sj_unannotate(a, target);
        bool matched = sj_match_meta(st, raw_conclusion, raw_target, sol, 0u);
        if (matched) {
            for (size_t i = 0u; i < meta_count; i++) matched = matched && sol[i];
            if (matched) conclusion = raw_conclusion;
        }
        if (!matched) {
            memset(sol, 0, sizeof sol);
            matched = sj_match_meta(st, conclusion, target, sol, 0u);
        }
        if (!matched) {
            if (pass == 0) continue;
            sj_fail(st, true, sj_expr3(a, "set:by-no-instance", name, goal));
            return false;
        }
        /* A witness that occurs only in an antecedent is found from the type
         * of the premise proof supplied for it, when that proof synthesizes. */
        for (size_t k = 0u; k < premise_count; k++) {
            if (!sj_has_unsolved_meta(antecedents[k], sol)) continue;
            Atom *premise_type = sj_synth(st, proof->expr.elems[2u + k]);
            if (!premise_type) {
                st->failure = NULL;
                st->refuted = false;
                st->incomplete = false;
                break;
            }
            Atom *pat = sj_normalize(a, antecedents[k], &st->fuel);
            Atom *tgt = pat ? sj_normalize(a, premise_type, &st->fuel) : NULL;
            if (!pat || !tgt) break;
            sj_match_meta(st, sj_unannotate(a, pat), sj_unannotate(a, tgt), sol, 0u);
        }
        for (size_t i = 0u; i < meta_count && ok; i++) ok = sol[i] != NULL;
        if (!ok) {
            if (pass == 0) continue;
            sj_fail(st, true, sj_expr2(a, "set:by-unsolved", name));
            return false;
        }
        for (size_t k = 0u; k < premise_count; k++) {
            Atom *antecedent = sj_fill_meta(a, antecedents[k], sol, meta_domains, 0u);
            if (!antecedent || !sj_check(st, proof->expr.elems[2u + k], antecedent))
                return false;
        }
        Atom *instantiated = sj_fill_meta(a, conclusion, sol, meta_domains, 0u);
        if (!instantiated) return false;
        return sj_convertible(st, instantiated, goal);
    }
    return false;
}

static bool sj_check(SjProofState *st, Atom *proof, Atom *goal) {
    if (st->failure) return false;
    Arena *a = st->arena;
    Atom *whnf = sj_whnf(a, goal, &st->fuel);
    if (!whnf) {
        sj_fail_fuel(st, goal);
        return false;
    }
    Atom *head = NULL;
    Atom *args[4] = {0};
    size_t argc = sj_spine(whnf, &head, args, 4u);

    Atom *intro_name = NULL;
    Atom *intro_body = NULL;
    bool imp_intro = false;
    bool all_intro = false;
    if (sj_is_expr(proof, "pf:imp-intro", 2u)) {
        imp_intro = true;
        intro_body = proof->expr.elems[1];
    } else if (sj_is_expr(proof, "pf:assume", 3u) &&
               proof->expr.elems[1]->kind == ATOM_SYMBOL) {
        imp_intro = true;
        intro_name = proof->expr.elems[1];
        intro_body = proof->expr.elems[2];
    } else if (sj_is_expr(proof, "pf:all-intro", 2u)) {
        all_intro = true;
        intro_body = proof->expr.elems[1];
    } else if (sj_is_expr(proof, "pf:fix", 3u) &&
               proof->expr.elems[1]->kind == ATOM_SYMBOL) {
        all_intro = true;
        intro_name = proof->expr.elems[1];
        intro_body = proof->expr.elems[2];
    }
    if (sj_is_expr(proof, "pf:have", 4u) && proof->expr.elems[1]->kind == ATOM_SYMBOL) {
        Atom *lemma = sj_synth(st, proof->expr.elems[2]);
        if (!lemma) return false;
        sj_push_hypothesis(st, lemma, proof->expr.elems[1]);
        bool ok = sj_check(st, proof->expr.elems[3], goal);
        st->hypothesis_count--;
        return ok;
    }
    if (sj_is_expr(proof, "pf:have", 5u) && proof->expr.elems[1]->kind == ATOM_SYMBOL) {
        /* a stated local lemma: its proposition may mention the proof's
         * bound variables, so it is then checked as an open term of prop */
        Atom *stated = proof->expr.elems[2];
        Atom *lemma = sj_mentions_proof_var(st, stated)
            ? sj_open_check(st, stated, sj_sym(a, "prop"))
            : sj_elaborate_closed(st, stated, sj_sym(a, "prop"));
        if (!lemma) return false;
        if (!sj_check(st, proof->expr.elems[3], lemma)) return false;
        sj_push_hypothesis(st, lemma, proof->expr.elems[1]);
        bool ok = sj_check(st, proof->expr.elems[4], goal);
        st->hypothesis_count--;
        return ok;
    }
    if (proof && proof->kind == ATOM_EXPR && proof->expr.len >= 2u &&
        atom_is_symbol(proof->expr.elems[0], "pf:by"))
        return sj_check_by(st, proof, goal);
    if (imp_intro) {
        if (argc != 2u || !sj_constant_named(head, "imp")) {
            sj_fail(st, true, sj_expr2(a, "set:imp-intro-needs-implication", whnf));
            return false;
        }
        sj_push_hypothesis(st, args[0], intro_name);
        bool ok = sj_check(st, intro_body, args[1]);
        st->hypothesis_count--;
        return ok;
    }
    if (all_intro) {
        Atom *domain = NULL;
        Atom *family_arg = NULL;
        if (!sj_quantifier(head, args, argc, &domain, &family_arg)) {
            sj_fail(st, true, sj_expr2(a, "set:all-intro-needs-universal", whnf));
            return false;
        }
        Atom *family = sj_whnf(a, family_arg, &st->fuel);
        if (!family) {
            sj_fail_fuel(st, family_arg);
            return false;
        }
        Atom *body = sj_lambda_body(family);
        if (!body) {
            /* eta-expand a non-lambda family: body = (App F (idx 0)) */
            Atom *shifted = sj_shift(a, family, 0u, 1);
            if (!shifted) {
                sj_fail(st, false, sj_expr2(a, "set:shift-failed", family));
                return false;
            }
            body = sj_expr3(a, "App", shifted, sj_expr2(a, "idx", atom_int(a, 0)));
        }
        size_t count = st->hypothesis_count;
        Atom **saved = arena_alloc(a, sizeof(Atom *) * (count ? count : 1u));
        for (size_t i = 0u; i < count; i++) {
            saved[i] = st->hypotheses[i];
            Atom *shifted = sj_shift(a, st->hypotheses[i], 0u, 1);
            if (!shifted) {
                sj_fail(st, false, sj_expr2(a, "set:shift-failed", st->hypotheses[i]));
                return false;
            }
            st->hypotheses[i] = shifted;
        }
        sj_push_binder(st, domain, intro_name);
        bool ok = sj_check(st, intro_body, body);
        st->binder_count--;
        for (size_t i = 0u; i < count; i++) st->hypotheses[i] = saved[i];
        return ok;
    }
    Atom *synthesized = sj_synth(st, proof);
    if (!synthesized) return false;
    return sj_convertible(st, synthesized, goal);
}

/* Verify that a published theorem's recorded dependencies still hold now:
 * every cited name must still be known with the recorded proposition under
 * the current signature, and so must their own dependencies. */
static bool sj_dependencies_current(SjProofState *st, Atom *record,
                                    unsigned depth) {
    Arena *a = st->arena;
    if (depth > 64u) {
        sj_fail(st, false, sj_expr2(a, "set:dependency-depth", record->expr.elems[SJ_KNOWN_NAME]));
        return false;
    }
    Atom *depends = record->expr.elems[SJ_KNOWN_DEPENDS];
    if (!depends || depends->kind != ATOM_EXPR) return true;
    for (CettaExprIndex i = 0u; i < depends->expr.len; i++) {
        Atom *pair = depends->expr.elems[i];
        if (!pair || pair->kind != ATOM_EXPR || pair->expr.len != 2u) continue;
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_SCOPED_DEPENDENCY_VISIT);
        Atom *name = pair->expr.elems[0];
        bool ambiguous = false;
        Atom *current = sj_known_lookup(a, st->env, st->user_space, name, &ambiguous);
        if (ambiguous) {
            sj_fail(st, false, sj_expr2(a, "set:ambiguous-name", name));
            return false;
        }
        if (!current) {
            sj_fail(st, false, sj_expr2(a, "set:dependency-missing", name));
            return false;
        }
        if (!atom_eq(current->expr.elems[SJ_KNOWN_PROP], pair->expr.elems[1])) {
            sj_fail(st, false, sj_expr2(a, "set:dependency-changed", name));
            return false;
        }
        if (!sj_record_signature_current(current)) {
            sj_fail(st, false, sj_expr2(a, "set:signature-changed", name));
            return false;
        }
        if (!atom_is_symbol(current->expr.elems[SJ_KNOWN_ORIGIN], "signature") &&
            !sj_record_admitted(a, st->user_space, current)) {
            sj_fail(st, false, sj_expr2(a, "set:not-admitted", name));
            return false;
        }
        if (atom_is_symbol(current->expr.elems[SJ_KNOWN_ORIGIN], "theorem") &&
            !sj_dependencies_current(st, current, depth + 1u))
            return false;
    }
    return true;
}

/* The canonical proposition of a known fact, with the record's authority
 * and dependencies verified, noted as a dependency of this proof. */
static Atom *sj_known_proposition(SjProofState *st, Atom *name) {
    Arena *a = st->arena;
    bool ambiguous = false;
    Atom *record = sj_known_lookup(a, st->env, st->user_space, name, &ambiguous);
    if (ambiguous) {
        sj_fail(st, false, sj_expr2(a, "set:ambiguous-name", name));
        return NULL;
    }
    if (!record) {
        sj_fail(st, false, sj_expr2(a, "set:unknown-name", name));
        return NULL;
    }
    if (!sj_record_signature_current(record)) {
        sj_fail(st, false, sj_expr2(a, "set:signature-changed", name));
        return NULL;
    }
    if (!atom_is_symbol(record->expr.elems[SJ_KNOWN_ORIGIN], "signature") &&
        !sj_record_admitted(a, st->user_space, record)) {
        sj_fail(st, false, sj_expr2(a, "set:not-admitted", name));
        return NULL;
    }
    if (atom_is_symbol(record->expr.elems[SJ_KNOWN_ORIGIN], "theorem") &&
        !sj_dependencies_current(st, record, 0u))
        return NULL;
    Atom *prop = record->expr.elems[SJ_KNOWN_PROP];
    Atom *elaborated = sj_elaborate_closed(st, prop, sj_sym(a, "prop"));
    if (!elaborated) return NULL;
    sj_note_dependency(st, name, prop);
    return elaborated;
}

static Atom *sj_hypothesis_named(SjProofState *st, Atom *name) {
    for (size_t i = st->hypothesis_count; i > 0u; i--)
        if (st->hypothesis_names[i - 1u] && atom_eq(st->hypothesis_names[i - 1u], name))
            return st->hypotheses[i - 1u];
    return NULL;
}

static Atom *sj_synth(SjProofState *st, Atom *proof) {
    if (st->failure) return NULL;
    Arena *a = st->arena;
    /* An annotation makes a checking proof usable in elimination position.
     * Neither the stated proposition nor its proof is trusted. */
    if (sj_is_expr(proof, "pf:typed", 3u)) {
        Atom *source = proof->expr.elems[1];
        Atom *proposition = sj_mentions_proof_var(st, source)
            ? sj_open_check(st, source, sj_sym(a, "prop"))
            : sj_elaborate_closed(st, source, sj_sym(a, "prop"));
        return proposition && sj_check(st, proof->expr.elems[2], proposition)
            ? proposition : NULL;
    }
    if (proof && proof->kind == ATOM_SYMBOL) {
        Atom *hyp = sj_hypothesis_named(st, proof);
        if (!hyp) {
            sj_fail(st, false, sj_expr2(a, "set:unknown-hypothesis", proof));
            return NULL;
        }
        return hyp;
    }
    if (sj_is_expr(proof, "pf:hyp", 2u)) {
        Atom *n = proof->expr.elems[1];
        if (!n || n->kind != ATOM_GROUNDED || n->ground.gkind != GV_INT ||
            n->ground.ival < 0 ||
            (uint64_t)n->ground.ival >= st->hypothesis_count) {
            sj_fail(st, true, sj_expr2(a, "set:hypothesis-index", proof));
            return NULL;
        }
        return st->hypotheses[st->hypothesis_count - 1u - (size_t)n->ground.ival];
    }
    if (sj_is_expr(proof, "pf:known", 2u))
        return sj_known_proposition(st, proof->expr.elems[1]);
    if (false) {
        Atom *name = NULL;
        bool ambiguous = false;
        Atom *record = sj_known_lookup(a, st->env, st->user_space, name, &ambiguous);
        if (ambiguous) {
            sj_fail(st, false, sj_expr2(a, "set:ambiguous-name", name));
            return NULL;
        }
        if (!record) {
            sj_fail(st, false, sj_expr2(a, "set:unknown-name", name));
            return NULL;
        }
        if (!sj_record_signature_current(record)) {
            sj_fail(st, false, sj_expr2(a, "set:signature-changed", name));
            return NULL;
        }
        if (!atom_is_symbol(record->expr.elems[SJ_KNOWN_ORIGIN], "signature") &&
            !sj_record_admitted(a, st->user_space, record)) {
            sj_fail(st, false, sj_expr2(a, "set:not-admitted", name));
            return NULL;
        }
        if (atom_is_symbol(record->expr.elems[SJ_KNOWN_ORIGIN], "theorem") &&
            !sj_dependencies_current(st, record, 0u))
            return NULL;
        Atom *prop = record->expr.elems[SJ_KNOWN_PROP];
        Atom *elaborated = sj_elaborate_closed(st, prop, sj_sym(a, "prop"));
        if (!elaborated) return NULL;
        sj_note_dependency(st, name, prop);
        return elaborated;
    }
    if (sj_is_expr(proof, "pf:imp-elim", 3u)) {
        Atom *major = sj_synth(st, proof->expr.elems[1]);
        if (!major) return NULL;
        Atom *whnf = sj_whnf(a, major, &st->fuel);
        if (!whnf) {
            sj_fail_fuel(st, major);
            return NULL;
        }
        Atom *head = NULL;
        Atom *args[4] = {0};
        size_t argc = sj_spine(whnf, &head, args, 4u);
        if (argc != 2u || !sj_constant_named(head, "imp")) {
            sj_fail(st, true, sj_expr2(a, "set:imp-elim-needs-implication", whnf));
            return NULL;
        }
        if (!sj_check(st, proof->expr.elems[2], args[0])) return NULL;
        return args[1];
    }
    if (sj_is_expr(proof, "pf:all-elim", 3u)) {
        Atom *major = sj_synth(st, proof->expr.elems[1]);
        if (!major) return NULL;
        Atom *whnf = sj_whnf(a, major, &st->fuel);
        if (!whnf) {
            sj_fail_fuel(st, major);
            return NULL;
        }
        Atom *head = NULL;
        Atom *args[4] = {0};
        size_t argc = sj_spine(whnf, &head, args, 4u);
        Atom *domain = NULL;
        Atom *family_arg = NULL;
        if (!sj_quantifier(head, args, argc, &domain, &family_arg)) {
            sj_fail(st, true, sj_expr2(a, "set:all-elim-needs-universal", whnf));
            return NULL;
        }
        Atom *witness = sj_witness(st, proof->expr.elems[2], domain);
        if (!witness) return NULL;
        Atom *locals[32] = {0};
        witness = sj_annotate_at(st, witness, domain, locals, 0u);
        if (!witness) return NULL;
        /* Annotate an unannotated family with the quantifier's domain so the
         * kernel can synthesize the redex and own the conversion. */
        Atom *family = sj_whnf(a, family_arg, &st->fuel);
        if (!family) {
            sj_fail_fuel(st, family_arg);
            return NULL;
        }
        if (sj_is_expr(family, "Lam", 2u))
            family = sj_expr3(a, "Lam", domain, family->expr.elems[1]);
        return sj_expr3(a, "App", family, witness);
    }
    sj_fail(st, false, sj_expr2(a, "set:unknown-proof-form", proof));
    return NULL;
}

static void sj_proof_state_init(SjProofState *st, Arena *a, Space *space,
                                SjSetEnv *env, bool limited, uint64_t steps,
                                bool collect_dependencies, bool diagnostic) {
    memset(st, 0, sizeof *st);
    st->arena = a;
    st->user_space = space;
    st->env = env;
    st->collect_dependencies = collect_dependencies;
    st->diagnostic = diagnostic;
    cetta_prime_regular_kernel_budget_init(&st->budget, limited, steps);
    st->fuel = limited && steps < 100000u ? (unsigned)steps : 100000u;
}

/* Check `proof : proposition` in the open environment.  On a plain call the
 * success carries no evidence; under `try` it names the checked proposition
 * and which conversion decided. */
static Atom *sj_inductive_record(SjProofState *st, Atom *type);

/* A published fact depends on every definition its statement or its proof
 * mentions: any change to one of them makes reuse abstain. */
static void sj_note_definition_uses(SjProofState *st, Atom *t) {
    if (!t || !st->collect_dependencies) return;
    if (t->kind == ATOM_SYMBOL) {
        SjDefinition *def = sj_definition_of(t);
        if (def) sj_note_dependency(st, def->name, def->prop);
        Atom *record = sj_inductive_record(st, t);
        if (record) sj_note_dependency(st, t, record->expr.elems[SJ_KNOWN_PROP]);
        return;
    }
    if (t->kind != ATOM_EXPR) return;
    for (CettaExprIndex i = 0u; i < t->expr.len; i++)
        sj_note_definition_uses(st, t->expr.elems[i]);
}

static Atom *sj_prove(SjProofState *st, Atom *judgment, Atom *proof,
                      Atom *proposition) {
    Arena *a = st->arena;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_SCOPED_PROOF_CHECK);
    Atom *goal = sj_elaborate_closed(st, proposition, sj_sym(a, "prop"));
    if (goal && sj_check(st, proof, goal)) {
        sj_note_definition_uses(st, proposition);
        sj_note_definition_uses(st, proof);
        if (!st->diagnostic) return sj_established(a, judgment, NULL);
        Atom *conversion = sj_expr3(
            a, "SetConversion",
            atom_int(a, (int64_t)st->kernel_decisions),
            atom_int(a, (int64_t)st->structural_decisions));
        return sj_established(a, judgment,
                              sj_expr3(a, "SetProofChecked", goal, conversion));
    }
    if (st->incomplete) return sj_incomplete(a, judgment, st->failure);
    if (st->refuted) return sj_refuted(a, judgment, st->failure);
    return sj_undetermined(
        a, judgment, st->failure ? st->failure : sj_expr1(a, "set:proof-undetermined"));
}

static Atom *sj_depends_atom(SjProofState *st) {
    Arena *a = st->arena;
    Atom **items = arena_alloc(a, sizeof(Atom *) * (st->depend_count ? st->depend_count : 1u));
    for (size_t i = 0u; i < st->depend_count; i++)
        items[i] = atom_expr2(a, st->depend_names[i], st->depend_props[i]);
    return atom_expr(a, items, (CettaExprLen)st->depend_count);
}

static Atom *sj_set_proves(Arena *a, Space *space, Atom *judgment,
                           Atom *proof, Atom *proposition, bool limited,
                           uint64_t steps, bool diagnostic) {
    SjSetEnv *env = sj_env(space);
    if (!env)
        return sj_undetermined(a, judgment, sj_expr1(a, "set:signature-unavailable"));
    SjProofState st;
    sj_proof_state_init(&st, a, space, env, limited, steps, false, diagnostic);
    return sj_prove(&st, judgment, proof, proposition);
}

static Atom *sj_set_theorem(Arena *a, Space *space, Atom *judgment,
                            Atom *name, Atom *proposition, Atom *proof,
                            bool limited, uint64_t steps) {
    SjSetEnv *env = sj_env(space);
    if (!env)
        return sj_undetermined(a, judgment, sj_expr1(a, "set:signature-unavailable"));
    SjProofState st;
    sj_proof_state_init(&st, a, space, env, limited, steps, true, false);
    Atom *verdict = sj_prove(&st, judgment, proof, proposition);
    Atom *evidence = NULL;
    if (sj_verdict_status(verdict, &evidence) != SJ_STATUS_ESTABLISHED)
        return verdict;
    Atom *record = sj_known_record(
        a, name, proposition, "theorem", proof, sj_depends_atom(&st),
        space_revision(space));
    return sj_established(a, judgment, sj_expr2(a, "SetPublish", record));
}

/* ------------------------------------------------------------------------ */
/* Native proof packages.                                                    */
/*                                                                            */
/* A successful set proof is not yet a Curry--Howard witness: `prop` is the  */
/* type of proposition codes.  The native package below adds one displayed   */
/* family `Holds : prop -> Type`, equips implication and universal codes with */
/* their two decoding equations, compiles the retained logical proof tree,   */
/* and asks the regular kernel to check the result.  Axioms remain explicit   */
/* declaration assumptions in the returned context.  The operation therefore */
/* neither turns a Boolean verdict into a proof nor installs new trusted      */
/* kernel code.                                                               */
/* ------------------------------------------------------------------------ */

typedef enum {
    SJ_NATIVE_OBJECT_BINDER,
    SJ_NATIVE_PROOF_BINDER
} SjNativeBinderKind;

typedef struct {
    SjNativeBinderKind kind;
    Atom *name;
} SjNativeBinder;

typedef struct {
    Atom *source_name;
    Atom *source_prop;
    Atom *constant_name;
    Atom *kernel_prop;
} SjNativeAssumption;

typedef struct {
    SjProofState *proof;
    SjNativeBinder binders[128];
    size_t binder_count;
    SjNativeAssumption assumptions[128];
    size_t assumption_count;
    unsigned theorem_depth;
    Atom *family_name;
} SjNativeCompiler;

enum {
    SJ_NATIVE_PACKAGE_LEN = 10,
    SJ_NATIVE_PACKAGE_NAME = 1,
    SJ_NATIVE_PACKAGE_PROP = 2,
    SJ_NATIVE_PACKAGE_PROOF = 3,
    SJ_NATIVE_PACKAGE_TERM = 4,
    SJ_NATIVE_PACKAGE_TYPE = 5,
    SJ_NATIVE_PACKAGE_CONTEXT = 6,
    SJ_NATIVE_PACKAGE_RULES = 7,
    SJ_NATIVE_PACKAGE_ASSUMPTIONS = 8,
    SJ_NATIVE_PACKAGE_SIGNATURE = 9
};

static Atom *sj_native_compile_check(SjNativeCompiler *compiler,
                                     Atom *proof, Atom *goal);
static Atom *sj_native_compile_synth(SjNativeCompiler *compiler,
                                     Atom *proof, Atom **proposition_out);

static bool sj_native_push(SjNativeCompiler *compiler,
                           SjNativeBinderKind kind, Atom *name) {
    if (compiler->binder_count >= 128u) {
        sj_fail(compiler->proof, false,
                sj_expr1(compiler->proof->arena, "set:native-binder-bound"));
        return false;
    }
    compiler->binders[compiler->binder_count++] =
        (SjNativeBinder){.kind = kind, .name = name};
    return true;
}

/* Index of the n-th innermost binder of one discipline in the mixed native
 * context.  Object and proof binders are distinct source contexts, but the
 * dependent target has one de Bruijn context. */
static Atom *sj_native_index(SjNativeCompiler *compiler,
                             SjNativeBinderKind kind, size_t occurrence) {
    Arena *a = compiler->proof->arena;
    size_t seen = 0u;
    for (size_t i = compiler->binder_count; i > 0u; i--) {
        if (compiler->binders[i - 1u].kind != kind) continue;
        if (seen++ == occurrence)
            return sj_expr2(a, "idx",
                            atom_int(a, (int64_t)(compiler->binder_count - i)));
    }
    sj_fail(compiler->proof, true,
            sj_expr1(a, kind == SJ_NATIVE_OBJECT_BINDER
                            ? "set:native-object-index"
                            : "set:native-proof-index"));
    return NULL;
}

static Atom *sj_native_named_index(SjNativeCompiler *compiler,
                                   SjNativeBinderKind kind, Atom *name) {
    Arena *a = compiler->proof->arena;
    for (size_t i = compiler->binder_count; i > 0u; i--) {
        SjNativeBinder *binder = &compiler->binders[i - 1u];
        if (binder->kind == kind && binder->name && atom_eq(binder->name, name))
            return sj_expr2(a, "idx",
                            atom_int(a, (int64_t)(compiler->binder_count - i)));
    }
    return NULL;
}

/* `sj_witness` produces indices in the source object-binder context.  Insert
 * the proof binders that are present in the dependent target context while
 * preserving indices introduced by lambdas inside the witness itself. */
static Atom *sj_native_reindex_objects(SjNativeCompiler *compiler, Atom *term,
                                       uint64_t local_depth) {
    Arena *a = compiler->proof->arena;
    uint64_t index = 0u;
    if (sj_intrinsic_index(term, &index)) {
        if (index < local_depth) return term;
        Atom *mixed = sj_native_index(
            compiler, SJ_NATIVE_OBJECT_BINDER,
            (size_t)(index - local_depth));
        if (!mixed) return NULL;
        uint64_t mapped = 0u;
        if (!sj_intrinsic_index(mixed, &mapped) ||
            mapped > (uint64_t)INT64_MAX - local_depth)
            return NULL;
        return sj_expr2(a, "idx", atom_int(a, (int64_t)(mapped + local_depth)));
    }
    if (!term || term->kind != ATOM_EXPR) return term;
    Atom **items = arena_alloc(a, sizeof(Atom *) * (size_t)term->expr.len);
    for (CettaExprIndex i = 0u; i < term->expr.len; i++) {
        bool under = (sj_is_expr(term, "Lam", 2u) && i == 1u) ||
                     ((sj_is_expr(term, "Lam", 3u) ||
                       sj_is_expr(term, "Pi", 3u) ||
                       sj_is_expr(term, "Sigma", 3u)) && i == 2u);
        items[i] = sj_native_reindex_objects(
            compiler, term->expr.elems[i], local_depth + (under ? 1u : 0u));
        if (!items[i]) return NULL;
    }
    return atom_expr(a, items, term->expr.len);
}

static Atom *sj_native_witness(SjNativeCompiler *compiler, Atom *authored,
                               Atom *domain, Atom **canonical_out) {
    SjProofState *st = compiler->proof;
    Atom *canonical = sj_witness(st, authored, domain);
    if (!canonical) return NULL;
    Atom *locals[32] = {0};
    canonical = sj_annotate_at(st, canonical, domain, locals, 0u);
    if (!canonical) return NULL;
    if (canonical_out) *canonical_out = canonical;
    Atom *kernel = sj_to_kernel_chart(st, canonical);
    return kernel ? sj_native_reindex_objects(compiler, kernel, 0u) : NULL;
}

static Atom *sj_native_proof_type(SjNativeCompiler *compiler,
                                  Atom *canonical_prop) {
    Arena *a = compiler->proof->arena;
    Atom *locals[32] = {0};
    Atom *annotated = sj_annotate(
        compiler->proof, canonical_prop, locals, 0u);
    Atom *kernel_prop = annotated
        ? sj_to_kernel_chart(compiler->proof, annotated) : NULL;
    return kernel_prop
        ? sj_expr3(a, "App", sj_expr2(a, "DeclConst", compiler->family_name),
                   kernel_prop)
        : NULL;
}

static Atom *sj_native_assumption(SjNativeCompiler *compiler, Atom *name,
                                  Atom *canonical_prop) {
    Arena *a = compiler->proof->arena;
    for (size_t i = 0u; i < compiler->assumption_count; i++)
        if (atom_eq(compiler->assumptions[i].source_name, name) &&
            atom_eq(compiler->assumptions[i].source_prop, canonical_prop))
            return sj_expr2(a, "DeclConst",
                            compiler->assumptions[i].constant_name);
    if (compiler->assumption_count >= 128u) {
        sj_fail(compiler->proof, false,
                sj_expr1(a, "set:native-assumption-bound"));
        return NULL;
    }
    char *shown_name = atom_to_string(a, name);
    char *shown_prop = atom_to_string(a, canonical_prop);
    size_t bytes = strlen(shown_name ? shown_name : "") +
                   strlen(shown_prop ? shown_prop : "") + 2u;
    char *identity = arena_alloc(a, bytes);
    snprintf(identity, bytes, "%s|%s", shown_name ? shown_name : "",
             shown_prop ? shown_prop : "");
    char digest[65];
    cetta_native_sha256_hex((const uint8_t *)identity, strlen(identity), digest);
    char internal[48];
    snprintf(internal, sizeof internal, "__cetta_proof_%.24s", digest);
    SjNativeAssumption *entry =
        &compiler->assumptions[compiler->assumption_count++];
    entry->source_name = name;
    entry->source_prop = canonical_prop;
    entry->constant_name = sj_sym(a, internal);
    entry->kernel_prop = NULL;
    return sj_expr2(a, "DeclConst", entry->constant_name);
}

static Atom *sj_native_compile_known(SjNativeCompiler *compiler, Atom *name,
                                     Atom **proposition_out) {
    SjProofState *st = compiler->proof;
    Arena *a = st->arena;
    Atom *proposition = sj_known_proposition(st, name);
    if (!proposition) return NULL;
    bool ambiguous = false;
    Atom *record = sj_known_lookup(a, st->env, st->user_space, name, &ambiguous);
    if (ambiguous || !record) return NULL;
    *proposition_out = proposition;
    if (!atom_is_symbol(record->expr.elems[SJ_KNOWN_ORIGIN], "theorem"))
        return sj_native_assumption(compiler, name, proposition);
    if (compiler->theorem_depth >= 64u) {
        sj_fail(st, false, sj_expr2(a, "set:native-theorem-depth", name));
        return NULL;
    }
    compiler->theorem_depth++;
    Atom *term = sj_native_compile_check(
        compiler, record->expr.elems[SJ_KNOWN_PROOF], proposition);
    compiler->theorem_depth--;
    return term;
}

static Atom *sj_native_compile_synth(SjNativeCompiler *compiler,
                                     Atom *proof, Atom **proposition_out) {
    SjProofState *st = compiler->proof;
    Arena *a = st->arena;
    if (sj_is_expr(proof, "pf:typed", 3u)) {
        Atom *source = proof->expr.elems[1];
        Atom *proposition = sj_mentions_proof_var(st, source)
            ? sj_open_check(st, source, sj_sym(a, "prop"))
            : sj_elaborate_closed(st, source, sj_sym(a, "prop"));
        if (!proposition) return NULL;
        Atom *term = sj_native_compile_check(
            compiler, proof->expr.elems[2], proposition);
        if (term) *proposition_out = proposition;
        return term;
    }
    if (proof && proof->kind == ATOM_SYMBOL) {
        Atom *prop = sj_hypothesis_named(st, proof);
        Atom *term = sj_native_named_index(
            compiler, SJ_NATIVE_PROOF_BINDER, proof);
        if (!prop || !term) {
            sj_fail(st, false, sj_expr2(a, "set:native-unknown-hypothesis", proof));
            return NULL;
        }
        *proposition_out = prop;
        return term;
    }
    if (sj_is_expr(proof, "pf:hyp", 2u)) {
        Atom *n = proof->expr.elems[1];
        if (!n || n->kind != ATOM_GROUNDED || n->ground.gkind != GV_INT ||
            n->ground.ival < 0 ||
            (uint64_t)n->ground.ival >= st->hypothesis_count) {
            sj_fail(st, true, sj_expr2(a, "set:native-hypothesis-index", proof));
            return NULL;
        }
        *proposition_out =
            st->hypotheses[st->hypothesis_count - 1u - (size_t)n->ground.ival];
        return sj_native_index(compiler, SJ_NATIVE_PROOF_BINDER,
                               (size_t)n->ground.ival);
    }
    if (sj_is_expr(proof, "pf:known", 2u))
        return sj_native_compile_known(compiler, proof->expr.elems[1],
                                       proposition_out);
    if (sj_is_expr(proof, "pf:imp-elim", 3u)) {
        Atom *major_prop = NULL;
        Atom *major = sj_native_compile_synth(
            compiler, proof->expr.elems[1], &major_prop);
        if (!major) return NULL;
        Atom *whnf = sj_whnf(a, major_prop, &st->fuel);
        Atom *head = NULL;
        Atom *args[4] = {0};
        size_t argc = whnf ? sj_spine(whnf, &head, args, 4u) : 0u;
        if (argc != 2u || !sj_constant_named(head, "imp")) {
            sj_fail(st, true,
                    sj_expr2(a, "set:native-imp-elim-needs-implication",
                             major_prop));
            return NULL;
        }
        Atom *minor = sj_native_compile_check(
            compiler, proof->expr.elems[2], args[0]);
        if (!minor) return NULL;
        *proposition_out = args[1];
        return sj_expr3(a, "App", major, minor);
    }
    if (sj_is_expr(proof, "pf:all-elim", 3u)) {
        Atom *major_prop = NULL;
        Atom *major = sj_native_compile_synth(
            compiler, proof->expr.elems[1], &major_prop);
        if (!major) return NULL;
        Atom *whnf = sj_whnf(a, major_prop, &st->fuel);
        Atom *head = NULL;
        Atom *args[4] = {0};
        size_t argc = whnf ? sj_spine(whnf, &head, args, 4u) : 0u;
        Atom *domain = NULL;
        Atom *family_arg = NULL;
        if (!sj_quantifier(head, args, argc, &domain, &family_arg)) {
            sj_fail(st, true,
                    sj_expr2(a, "set:native-all-elim-needs-universal",
                             major_prop));
            return NULL;
        }
        Atom *canonical_witness = NULL;
        Atom *witness = sj_native_witness(
            compiler, proof->expr.elems[2], domain, &canonical_witness);
        if (!witness) return NULL;
        Atom *family = sj_whnf(a, family_arg, &st->fuel);
        Atom *body = family ? sj_lambda_body(family) : NULL;
        *proposition_out = body
            ? sj_subst(a, body, 0u, canonical_witness)
            : sj_expr3(a, "App", family_arg, canonical_witness);
        if (!*proposition_out) return NULL;
        return sj_expr3(a, "App", major, witness);
    }
    sj_fail(st, false,
            sj_expr2(a, "set:native-unsupported-proof-form", proof));
    return NULL;
}

static Atom *sj_native_compile_check(SjNativeCompiler *compiler,
                                     Atom *proof, Atom *goal) {
    SjProofState *st = compiler->proof;
    Arena *a = st->arena;
    Atom *whnf = sj_whnf(a, goal, &st->fuel);
    if (!whnf) {
        sj_fail_fuel(st, goal);
        return NULL;
    }
    Atom *head = NULL;
    Atom *args[4] = {0};
    size_t argc = sj_spine(whnf, &head, args, 4u);
    Atom *intro_name = NULL;
    Atom *intro_body = NULL;
    bool implication = false;
    bool universal = false;
    if (sj_is_expr(proof, "pf:imp-intro", 2u)) {
        implication = true;
        intro_body = proof->expr.elems[1];
    } else if (sj_is_expr(proof, "pf:assume", 3u) &&
               proof->expr.elems[1]->kind == ATOM_SYMBOL) {
        implication = true;
        intro_name = proof->expr.elems[1];
        intro_body = proof->expr.elems[2];
    } else if (sj_is_expr(proof, "pf:all-intro", 2u)) {
        universal = true;
        intro_body = proof->expr.elems[1];
    } else if (sj_is_expr(proof, "pf:fix", 3u) &&
               proof->expr.elems[1]->kind == ATOM_SYMBOL) {
        universal = true;
        intro_name = proof->expr.elems[1];
        intro_body = proof->expr.elems[2];
    }
    if (implication) {
        if (argc != 2u || !sj_constant_named(head, "imp")) return NULL;
        Atom *domain = sj_native_proof_type(compiler, args[0]);
        domain = domain ? sj_native_reindex_objects(compiler, domain, 0u) : NULL;
        if (!domain) return NULL;
        sj_push_hypothesis(st, args[0], intro_name);
        if (!sj_native_push(compiler, SJ_NATIVE_PROOF_BINDER, intro_name)) {
            st->hypothesis_count--;
            return NULL;
        }
        Atom *body = sj_native_compile_check(compiler, intro_body, args[1]);
        compiler->binder_count--;
        st->hypothesis_count--;
        return body ? sj_expr3(a, "Lam", domain, body) : NULL;
    }
    if (universal) {
        Atom *domain = NULL;
        Atom *family_arg = NULL;
        if (!sj_quantifier(head, args, argc, &domain, &family_arg)) return NULL;
        Atom *family = sj_whnf(a, family_arg, &st->fuel);
        Atom *body_goal = family ? sj_lambda_body(family) : NULL;
        if (!body_goal) {
            Atom *shifted = family ? sj_shift(a, family, 0u, 1) : NULL;
            body_goal = shifted
                ? sj_expr3(a, "App", shifted,
                           sj_expr2(a, "idx", atom_int(a, 0)))
                : NULL;
        }
        if (!body_goal) return NULL;
        Atom *native_domain = sj_to_kernel_chart(st, domain);
        native_domain = native_domain
            ? sj_native_reindex_objects(compiler, native_domain, 0u) : NULL;
        if (!native_domain) return NULL;
        size_t hypothesis_count = st->hypothesis_count;
        Atom **saved = arena_alloc(a, sizeof(Atom *) *
                                      (hypothesis_count ? hypothesis_count : 1u));
        for (size_t i = 0u; i < hypothesis_count; i++) {
            saved[i] = st->hypotheses[i];
            st->hypotheses[i] = sj_shift(a, st->hypotheses[i], 0u, 1);
            if (!st->hypotheses[i]) return NULL;
        }
        sj_push_binder(st, domain, intro_name);
        if (!sj_native_push(compiler, SJ_NATIVE_OBJECT_BINDER, intro_name)) {
            st->binder_count--;
            for (size_t i = 0u; i < hypothesis_count; i++)
                st->hypotheses[i] = saved[i];
            return NULL;
        }
        Atom *body = sj_native_compile_check(compiler, intro_body, body_goal);
        compiler->binder_count--;
        st->binder_count--;
        for (size_t i = 0u; i < hypothesis_count; i++)
            st->hypotheses[i] = saved[i];
        return body ? sj_expr3(a, "Lam", native_domain, body) : NULL;
    }
    Atom *synthesized = NULL;
    Atom *term = sj_native_compile_synth(compiler, proof, &synthesized);
    if (!term) return NULL;
    if (!sj_convertible(st, synthesized, goal)) return NULL;
    return term;
}

static Atom *sj_native_rule_cons(Arena *a, Atom *head, Atom *patterns,
                                 Atom *rhs, Atom *tail) {
    Atom *rule_items[5] = {sj_sym(a, "PrimeRule"), head,
                           atom_int(a, 1), patterns, rhs};
    return sj_expr3(a, "LCons", atom_expr(a, rule_items, 5u),
                    tail ? tail : sj_sym(a, "LNil"));
}

static Atom *sj_native_rules(SjNativeCompiler *compiler) {
    SjProofState *st = compiler->proof;
    Arena *a = st->arena;
    Atom *family = sj_expr2(a, "DeclConst", compiler->family_name);
    Atom *p0 = sj_expr2(a, "PVar", atom_int(a, 0));
    Atom *p1 = sj_expr2(a, "PVar", atom_int(a, 1));
    Atom *imp = sj_expr2(a, "DeclConst", sj_sym(a, "imp"));
    Atom *imp_code = sj_expr3(a, "App", sj_expr3(a, "App", imp, p0), p1);
    Atom *imp_patterns = atom_expr(a, (Atom *[]){imp_code}, 1u);
    Atom *imp_rhs = sj_expr3(
        a, "Pi", sj_expr3(a, "App", family, p0),
        sj_expr3(a, "App", family, p1));
    Atom *rules = sj_native_rule_cons(a, compiler->family_name, imp_patterns,
                                      imp_rhs, st->reachable_rules);
    for (size_t i = 0u; i < st->instance_count; i++) {
        const char *name = atom_name_cstr(st->instance_names[i]);
        Atom *type = st->instance_types[i];
        if (!name || strncmp(name, "all@", 4u) != 0 ||
            !sj_is_expr(type, "Pi", 3u) ||
            !sj_is_expr(type->expr.elems[1], "Pi", 3u))
            continue;
        Atom *domain = sj_to_kernel(a, type->expr.elems[1]->expr.elems[1]);
        if (!domain) return NULL;
        Atom *predicate = sj_expr2(a, "PVar", atom_int(a, 0));
        Atom *code = sj_expr3(
            a, "App", sj_expr2(a, "DeclConst", st->instance_names[i]),
            predicate);
        Atom *patterns = atom_expr(a, (Atom *[]){code}, 1u);
        Atom *applied = sj_expr3(
            a, "App", predicate, sj_expr2(a, "idx", atom_int(a, 0)));
        Atom *rhs = sj_expr3(
            a, "Pi", domain, sj_expr3(a, "App", family, applied));
        rules = sj_native_rule_cons(a, compiler->family_name, patterns, rhs,
                                    rules);
    }
    return rules;
}

/* Locate the first declaration whose domain is not scoped over the context
 * preceding it.  This runs only after the kernel has declined the complete
 * context, so successful proof checking pays no second recognition cost. */
static Atom *sj_native_context_failure(Arena *a, Atom *context) {
    Atom *rows[512];
    size_t count = 0u;
    for (Atom *cursor = context;
         cursor && !atom_is_symbol(cursor, "PrimeCtxNil") && count < 512u;) {
        if (!sj_is_expr(cursor, "PrimeCtxDecl", 4u) &&
            !sj_is_expr(cursor, "PrimeCtxCons", 3u))
            return sj_expr2(a, "set:native-context-syntax", cursor);
        rows[count++] = cursor;
        cursor = cursor->expr.elems[cursor->expr.len - 1u];
    }
    for (size_t i = count; i > 0u; i--) {
        Atom *row = rows[i - 1u];
        bool declaration = sj_is_expr(row, "PrimeCtxDecl", 4u);
        Atom *key = declaration ? row->expr.elems[1] : sj_sym(a, "local");
        Atom *domain = declaration ? row->expr.elems[2] : row->expr.elems[1];
        Atom *tail = row->expr.elems[row->expr.len - 1u];
        Atom *scoped = sj_expr3(a, "PrimeScoped", tail, domain);
        CettaPrimeRegularKernelBudget budget;
        cetta_prime_regular_kernel_budget_init(&budget, false, 0u);
        CettaPrimeRegularKernelResult classified =
            cetta_prime_regular_kernel_classify_scoped_syntax(
                scoped, &budget);
        if (classified.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
            Atom *reason = sj_sym(
                a, classified.reason ? classified.reason
                                     : "native-context-declined");
            Atom *items[4] = {
                sj_sym(a, "set:native-context-entry"), key, domain, reason};
            return atom_expr(a, items, 4u);
        }
    }
    return sj_expr1(a, "set:native-context-unlocated");
}

static Atom *sj_set_native_proof_with_state(
    Arena *a, Atom *judgment, Atom *name, SjProofState *st) {
    SjSetEnv *env = st->env;
    Space *space = st->user_space;
    bool ambiguous = false;
    Atom *record = sj_known_lookup(a, env, space, name, &ambiguous);
    if (ambiguous)
        return sj_undetermined(a, judgment,
                               sj_expr2(a, "set:ambiguous-name", name));
    if (!record)
        return sj_undetermined(a, judgment,
                               sj_expr2(a, "set:unknown-name", name));
    Atom *proposition = sj_known_proposition(st, name);
    if (!proposition)
        return st->incomplete ? sj_incomplete(a, judgment, st->failure)
             : st->refuted ? sj_refuted(a, judgment, st->failure)
                           : sj_undetermined(a, judgment, st->failure);
    if (atom_is_symbol(record->expr.elems[SJ_KNOWN_ORIGIN], "theorem") &&
        !sj_check(st, record->expr.elems[SJ_KNOWN_PROOF], proposition))
        return st->incomplete ? sj_incomplete(a, judgment, st->failure)
             : st->refuted ? sj_refuted(a, judgment, st->failure)
                           : sj_undetermined(a, judgment, st->failure);

    char family_spelling[48];
    snprintf(family_spelling, sizeof family_spelling, "__cetta_holds_%.24s",
             sj_signature_digest());
    SjNativeCompiler compiler = {
        .proof = st,
        .family_name = sj_sym(a, family_spelling),
    };
    Atom *native_proposition = NULL;
    Atom *term = sj_native_compile_known(&compiler, name, &native_proposition);
    if (!term || !native_proposition)
        return st->incomplete ? sj_incomplete(a, judgment, st->failure)
             : st->refuted ? sj_refuted(a, judgment, st->failure)
                           : sj_undetermined(a, judgment,
                              st->failure ? st->failure
                                         : sj_expr1(a, "set:native-compilation-declined"));
    Atom *expected = sj_native_proof_type(&compiler, native_proposition);
    if (!expected)
        return sj_undetermined(a, judgment,
                               sj_expr1(a, "set:native-proof-type"));
    sj_declare_mentioned(st, native_proposition, NULL);
    sj_declare_kernel_mentioned(st, term, NULL);
    if (!sj_declare_rule_dependencies(st, NULL))
        return sj_undetermined(a, judgment,
                               sj_expr1(a, "set:native-rule-dependencies"));
    for (size_t i = 0u; i < compiler.assumption_count; i++) {
        Atom *locals[32] = {0};
        Atom *annotated = sj_annotate(
            st, compiler.assumptions[i].source_prop, locals, 0u);
        compiler.assumptions[i].kernel_prop = annotated
            ? sj_to_kernel_chart(st, annotated) : NULL;
        if (!compiler.assumptions[i].kernel_prop)
            return sj_undetermined(a, judgment,
                                   sj_expr1(a, "set:native-assumption-type"));
        /* An assumed proposition can mention a definition absent from the
         * compiled term and its result. Assumptions are roots too. */
        sj_declare_kernel_mentioned(st, compiler.assumptions[i].kernel_prop, NULL);
    }
    if (!sj_declare_rule_dependencies(st, NULL))
        return sj_undetermined(a, judgment,
                               sj_expr1(a, "set:native-rule-dependencies"));

    Atom *context = sj_kernel_context(st);
    if (!context)
        return sj_undetermined(a, judgment,
                               sj_expr1(a, "set:native-context"));
    for (size_t i = 0u; i < st->instance_count; i++) {
        Atom *kernel_type = sj_to_kernel(a, st->instance_types[i]);
        if (!kernel_type)
            return sj_undetermined(a, judgment,
                                   sj_expr1(a, "set:native-instance-type"));
        Atom *items[4] = {sj_sym(a, "PrimeCtxDecl"),
                          sj_expr2(a, "DeclConst", st->instance_names[i]),
                          kernel_type, context};
        context = atom_expr(a, items, 4u);
    }
    Atom *proof_family_type = sj_expr3(
        a, "Pi", sj_expr2(a, "DeclConst", sj_sym(a, "prop")),
        sj_expr2(a, "Sort", sj_expr2(a, "LevelConst", atom_int(a, 0))));
    context = atom_expr(
        a, (Atom *[]){sj_sym(a, "PrimeCtxDecl"),
                      sj_expr2(a, "DeclConst", compiler.family_name),
                      proof_family_type, context}, 4u);
    Atom **assumption_rows = arena_alloc(
        a, sizeof(Atom *) * (compiler.assumption_count
                                 ? compiler.assumption_count
                                 : 1u));
    for (size_t i = 0u; i < compiler.assumption_count; i++) {
        SjNativeAssumption *assumption = &compiler.assumptions[i];
        Atom *type = sj_expr3(
            a, "App", sj_expr2(a, "DeclConst", compiler.family_name),
            assumption->kernel_prop);
        context = atom_expr(
            a, (Atom *[]){sj_sym(a, "PrimeCtxDecl"),
                          sj_expr2(a, "DeclConst", assumption->constant_name),
                          type, context}, 4u);
        assumption_rows[i] = atom_expr(
            a, (Atom *[]){assumption->source_name, assumption->source_prop,
                          assumption->constant_name}, 3u);
    }
    Atom *rules = sj_native_rules(&compiler);
    if (!rules)
        return sj_undetermined(a, judgment,
                               sj_expr1(a, "set:native-decoder-rules"));
    cetta_prime_regular_kernel_rules_set(rules);
    CettaPrimeRegularKernelResult checked =
        cetta_prime_regular_kernel_check_intrinsic(
            a, context, term, expected, &st->budget);
    cetta_prime_regular_kernel_rules_set(NULL);
    if (checked.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
        Atom *reason = sj_sym(a, checked.reason ? checked.reason
                                               : "native-kernel-declined");
        if (checked.status == CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED)
            return sj_incomplete(a, judgment,
                                 sj_expr2(a, "set:native-kernel", reason));
        if (checked.status == CETTA_PRIME_REGULAR_KERNEL_REFUTED)
            return sj_refuted(a, judgment,
                              sj_expr2(a, "set:native-kernel", reason));
        if (checked.reason &&
            strncmp(checked.reason, "outside-regular-context", 23u) == 0)
            reason = sj_native_context_failure(a, context);
        return sj_undetermined(a, judgment,
                               sj_expr2(a, "set:native-kernel", reason));
    }
    Atom *assumptions = atom_expr(a, assumption_rows,
                                  (CettaExprLen)compiler.assumption_count);
    Atom *package_items[SJ_NATIVE_PACKAGE_LEN] = {
        sj_sym(a, "SetNativeProofV1"), name, record->expr.elems[SJ_KNOWN_PROP],
        record->expr.elems[SJ_KNOWN_PROOF], term, expected, context, rules,
        assumptions, atom_string(a, sj_signature_digest())};
    return sj_established(
        a, judgment,
        sj_expr2(a, "PrimeScopedValue",
                 atom_expr(a, package_items, SJ_NATIVE_PACKAGE_LEN)));
}

static Atom *sj_set_native_proof(Arena *a, Space *space, Atom *judgment,
                                 Atom *name, bool limited, uint64_t steps) {
    SjSetEnv *env = sj_env(space);
    if (!env)
        return sj_undetermined(a, judgment,
                               sj_expr1(a, "set:signature-unavailable"));
    SjProofState st;
    sj_proof_state_init(&st, a, space, env, limited, steps, false, true);
    return sj_set_native_proof_with_state(a, judgment, name, &st);
}

/* Apply a checked native proof to a dependent consumer. The proof package's
 * context is stable; the consumer may extend it with current formed declarations
 * and their reachable rules. The package is data supplied by the caller, so it is never
 * trusted by shape or digest alone: the retained source proof is compiled
 * and kernel-checked again in the current theory, and every package field
 * must agree before the consumer application is checked.  The accepted
 * result retains that application and its dependent type. It is a current-theory
 * observation, not a standalone admission certificate for the consumer. The explicit
 * normalization mode additionally computes the value and type using the
 * existing conversion engine and independently checks the result. */
static Atom *sj_set_native_use(Arena *a, Space *space, Atom *judgment,
                               Atom *package, Atom *consumer,
                               bool normalize, bool limited, uint64_t steps) {
    if (!sj_is_expr(package, "SetNativeProofV1", SJ_NATIVE_PACKAGE_LEN))
        return sj_refuted(a, judgment,
                          sj_expr1(a, "set:native-package-syntax"));
    Atom *name = package->expr.elems[SJ_NATIVE_PACKAGE_NAME];
    if (!name || name->kind != ATOM_SYMBOL)
        return sj_refuted(a, judgment,
                          sj_expr1(a, "set:native-package-name"));
    Atom *digest = package->expr.elems[SJ_NATIVE_PACKAGE_SIGNATURE];
    if (!digest || digest->kind != ATOM_GROUNDED ||
        digest->ground.gkind != GV_STRING || !digest->ground.sval ||
        strcmp(digest->ground.sval, sj_signature_digest()) != 0)
        return sj_undetermined(a, judgment,
                               sj_expr1(a, "set:native-package-signature"));

    SjSetEnv *env = sj_env(space);
    if (!env)
        return sj_undetermined(a, judgment,
                               sj_expr1(a, "set:signature-unavailable"));
    SjProofState st;
    sj_proof_state_init(&st, a, space, env, limited, steps, false, true);
    Atom *proof_query = sj_expr2(a, "set:native-proof", name);
    Atom *proof_verdict = sj_set_native_proof_with_state(
        a, proof_query, name, &st);
    Atom *proof_evidence = NULL;
    SjStatus proof_status = sj_verdict_status(proof_verdict, &proof_evidence);
    if (proof_status != SJ_STATUS_ESTABLISHED)
        return sj_verdict(a, sj_status_name(proof_status), judgment,
                          proof_evidence);
    if (!sj_is_expr(proof_evidence, "PrimeScopedValue", 2u))
        return sj_undetermined(a, judgment,
                               sj_expr1(a, "set:native-package-recheck"));
    Atom *current = proof_evidence->expr.elems[1];
    if (!atom_eq(package, current))
        return sj_undetermined(a, judgment,
                               sj_expr1(a, "set:native-package-changed"));

    Atom *term = current->expr.elems[SJ_NATIVE_PACKAGE_TERM];
    Atom *context = current->expr.elems[SJ_NATIVE_PACKAGE_CONTEXT];
    size_t proof_instances = st.instance_count;
    sj_declare_kernel_mentioned(&st, consumer, NULL);
    if (!sj_declare_rule_dependencies(&st, NULL))
        return sj_undetermined(a, judgment,
                               sj_expr1(a, "set:native-consumer-dependencies"));
    for (size_t i = proof_instances; i < st.instance_count; i++) {
        /* A live declaration must not shadow a package-private proof/family
         * constant. Public declarations already occur in instance_names. */
        for (Atom *prior = context; sj_is_expr(prior, "PrimeCtxDecl", 4u);
             prior = prior->expr.elems[3]) {
            Atom *key = prior->expr.elems[1];
            if (sj_is_expr(key, "DeclConst", 2u) &&
                atom_eq(key->expr.elems[1], st.instance_names[i]))
                return sj_undetermined(a, judgment,
                    sj_expr2(a, "set:native-consumer-shadow", st.instance_names[i]));
        }
        Atom *type = sj_to_kernel(a, st.instance_types[i]);
        if (!type)
            return sj_undetermined(a, judgment,
                                   sj_expr1(a, "set:native-consumer-declaration"));
        context = atom_expr(
            a, (Atom *[]){sj_sym(a, "PrimeCtxDecl"),
                          sj_expr2(a, "DeclConst", st.instance_names[i]),
                          type, context}, 4u);
    }
    /* Read the family from the freshly checked package, never from a caller's
     * invented name. Decoder rules must also cover new consumer type instances. */
    Atom *proof_type = current->expr.elems[SJ_NATIVE_PACKAGE_TYPE];
    if (!sj_is_expr(proof_type, "App", 3u) ||
        !sj_is_expr(proof_type->expr.elems[1], "DeclConst", 2u))
        return sj_undetermined(a, judgment,
                               sj_expr1(a, "set:native-consumer-family"));
    SjNativeCompiler consumer_compiler = {
        .proof = &st,
        .family_name = proof_type->expr.elems[1]->expr.elems[1],
    };
    Atom *rules = sj_native_rules(&consumer_compiler);
    if (!rules)
        return sj_undetermined(a, judgment,
                               sj_expr1(a, "set:native-consumer-rules"));
    Atom *application = sj_expr3(a, "App", consumer, term);
    cetta_prime_regular_kernel_rules_set(rules);
    CettaPrimeRegularKernelResult used;
    Atom *normal = NULL;
    Atom *source_type = NULL;
    if (normalize) {
        CettaPrimeRegularKernelNormalFormV1 computed =
            cetta_prime_regular_kernel_normalize_intrinsic_v1(
                a, context, application, &st.budget);
        used = (CettaPrimeRegularKernelResult){
            computed.status, computed.type, computed.reason};
        normal = computed.term;
        source_type = computed.source_type;
    } else {
        used = cetta_prime_regular_kernel_synth_intrinsic_v1(
            a, context, application, &st.budget);
    }
    cetta_prime_regular_kernel_rules_set(NULL);
    if (used.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
        Atom *reason = sj_sym(a, used.reason ? used.reason
                                             : "native-consumer-declined");
        if (used.status == CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED)
            return sj_incomplete(a, judgment,
                                 sj_expr2(a, "set:native-consumer", reason));
        if (used.status == CETTA_PRIME_REGULAR_KERNEL_REFUTED)
            return sj_refuted(a, judgment,
                              sj_expr2(a, "set:native-consumer", reason));
        return sj_undetermined(a, judgment,
                               sj_expr2(a, "set:native-consumer", reason));
    }
    if (normalize) {
        /* The source application and package retain intensional provenance.
         * Only the separate normal-form field observes it through conversion. */
        Atom *normal_items[7] = {
            sj_sym(a, "SetNativeNormalFormV1"), current, consumer, application,
            normal, source_type, used.type};
        return sj_established(a, judgment,
            sj_expr2(a, "PrimeScopedValue", atom_expr(a, normal_items, 7u)));
    }
    Atom *use_items[5] = {
        sj_sym(a, "SetNativeUseV1"), current, consumer, application,
        used.type};
    return sj_established(
        a, judgment,
        sj_expr2(a, "PrimeScopedValue", atom_expr(a, use_items, 5u)));
}


/* ------------------------------------------------------------------------ */
/* Definitions by equations.  `(set:define name type (= lhs rhs) ...)`        */
/* admits a constant with its defining equations when they are a total        */
/* structural recursion over the constructors named by the datatype's         */
/* induction axiom: one scrutinized argument, every constructor once,         */
/* recursive calls only on that constructor's recursive arguments, and each   */
/* right-hand side a term of the result type under the pattern variables.     */
/* Admitted equations unfold during conversion and are recorded as            */
/* dependencies of every fact whose statement or proof mentions the name.    */
/* ------------------------------------------------------------------------ */

typedef struct {
    Atom *name;
    size_t arity;
    bool recursive[8];
} SjConstructor;

/* The constructors of a datatype, read off its inductive declaration
 * record `(set:known T (inductive (u n) (c type) ...) inductive ...)`.  An
 * induction-shaped axiom is not enough: a type satisfying induction need
 * not be freely generated, and only free generation justifies definition
 * by structural recursion. */
static Atom *sj_inductive_record(SjProofState *st, Atom *type) {
    if (!type || type->kind != ATOM_SYMBOL) return NULL;
    bool ambiguous = false;
    Atom *record = sj_known_lookup(st->arena, st->env, st->user_space, type, &ambiguous);
    if (!record || ambiguous ||
        !atom_is_symbol(record->expr.elems[SJ_KNOWN_ORIGIN], "inductive"))
        return NULL;
    return record;
}

static bool sj_datatype_constructors(SjProofState *st, Atom *type,
                                     SjConstructor *ctors, size_t *count_out,
                                     Atom **principle_out) {
    Arena *a = st->arena;
    Atom *record = sj_inductive_record(st, type);
    if (!record) {
        sj_fail(st, false, sj_expr2(a, "set:define-not-inductive", type));
        return false;
    }
    Atom *decl = record->expr.elems[SJ_KNOWN_PROP];
    if (!decl || decl->kind != ATOM_EXPR || decl->expr.len < 2u ||
        !atom_is_symbol(decl->expr.elems[0], "inductive")) {
        sj_fail(st, false, sj_expr2(a, "set:define-not-inductive", type));
        return false;
    }
    size_t n = 0u;
    for (CettaExprIndex i = 2u; i < decl->expr.len; i++) {
        Atom *c = decl->expr.elems[i];
        if (!c || c->kind != ATOM_EXPR || c->expr.len != 3u || n >= 8u) {
            sj_fail(st, false, sj_expr2(a, "set:define-not-inductive", type));
            return false;
        }
        SjConstructor *ctor = &ctors[n];
        memset(ctor, 0, sizeof *ctor);
        ctor->name = c->expr.elems[1];
        Atom *ctype = c->expr.elems[2];
        if (ctype->kind == ATOM_EXPR && ctype->expr.len >= 3u &&
            atom_is_symbol(ctype->expr.elems[0], "->")) {
            ctor->arity = ctype->expr.len - 2u;
            if (ctor->arity > 8u) {
                sj_fail(st, false, sj_expr2(a, "set:define-constructor-arity", ctor->name));
                return false;
            }
            for (size_t k = 0u; k < ctor->arity; k++)
                ctor->recursive[k] = atom_eq(ctype->expr.elems[1u + k], type);
        }
        n++;
    }
    *count_out = n;
    *principle_out = record->expr.elems[SJ_KNOWN_NAME];
    return true;
}

/* The canonical result type after n arguments of a Pi chain. */
static Atom *sj_pi_result(Arena *a, Atom *type, size_t n, Atom **params) {
    for (size_t i = 0u; i < n; i++) {
        if (!sj_is_expr(type, "Pi", 3u) || sj_mentions_index(type->expr.elems[2], 0u))
            return NULL;
        if (params) params[i] = type->expr.elems[1];
        type = sj_shift(a, type->expr.elems[2], 0u, -1);
        if (!type) return NULL;
    }
    return type;
}

/* True when `t`, sitting under `bound` outer binders, mentions one of them. */
static bool sj_mentions_outer(Atom *t, uint64_t bound) {
    for (uint64_t index = 0u; index < bound; index++)
        if (sj_mentions_index(t, index)) return true;
    return false;
}

/* Domain of telescope argument `index`, still under the preceding binders.
 * The domain is returned even when it mentions those binders. Coverage
 * decides after reduction, not from the unreduced mention. */
static bool sj_pi_domain_at(Atom *type, size_t arity, size_t index,
                            Atom **domain_out) {
    if (!domain_out || index >= arity) return false;
    for (size_t i = 0u; i < index; i++) {
        if (!sj_is_expr(type, "Pi", 3u)) return false;
        type = type->expr.elems[2];
    }
    if (!sj_is_expr(type, "Pi", 3u)) return false;
    Atom *domain = type->expr.elems[1];
    if (!domain) return false;
    *domain_out = domain;
    return true;
}

/* Formation quotes `(DeclConst c)` to `c` and writes an application as
 * `(f a ...)`. Admitted definitions match the kernel spine `(App f a)`. */
static bool sj_spine_preserved(Atom *t) {
    return sj_is_expr(t, "Pi", 3u) || sj_is_expr(t, "Sigma", 3u) ||
           sj_is_expr(t, "Lam", 2u) || sj_is_expr(t, "Lam", 3u) ||
           sj_is_expr(t, "App", 3u) || sj_is_expr(t, "idx", 2u) ||
           sj_is_expr(t, "DeclConst", 2u);
}

static Atom *sj_to_delta_spine(Arena *a, Atom *t) {
    if (!t || t->kind != ATOM_EXPR || t->expr.len == 0u || t->expr.len > 8u)
        return t;
    if (sj_spine_preserved(t)) {
        Atom *children[8] = {0};
        for (CettaExprIndex i = 1u; i < t->expr.len; i++) {
            children[i] = sj_to_delta_spine(a, t->expr.elems[i]);
            if (!children[i]) return NULL;
        }
        return sj_rebuild(a, t, children);
    }
    Atom *head = sj_to_delta_spine(a, t->expr.elems[0]);
    if (!head) return NULL;
    for (CettaExprIndex i = 1u; i < t->expr.len; i++) {
        Atom *arg = sj_to_delta_spine(a, t->expr.elems[i]);
        if (!arg) return NULL;
        head = sj_expr3(a, "App", head, arg);
        if (!head) return NULL;
    }
    return head;
}

/* Reduce a scrutinee domain by admitted definitions. Constructor coverage
 * applies only when the reduct is a closed inductive symbol: it mentions no
 * preceding binder, and it is not an application. An indexed application
 * stays a fragment even when its head is an inductive. Fuel exhaustion is
 * reported by the caller; it is not coverage and it is not a refutation. */
static Atom *sj_closed_inductive_reduct(SjProofState *st, Atom *domain,
                                        size_t outer, bool *exhausted) {
    if (exhausted) *exhausted = false;
    Atom *spine = sj_to_delta_spine(st->arena, domain);
    if (!spine) spine = domain;
    unsigned fuel = st->fuel;
    Atom *reduced = sj_whnf(st->arena, spine, &fuel);
    st->fuel = fuel;
    if (!reduced) {
        if (exhausted) *exhausted = true;
        sj_fail_fuel(st, domain);
        return NULL;
    }
    if (sj_is_expr(reduced, "DeclConst", 2u))
        reduced = reduced->expr.elems[1];
    if (!reduced || reduced->kind != ATOM_SYMBOL ||
        sj_mentions_outer(reduced, outer))
        return NULL;
    return reduced;
}

static bool sj_is_var(Atom *t) { return t && t->kind == ATOM_VAR; }

/* Every occurrence of `name` in an authored right-hand side must be applied
 * to at least `arity` arguments whose scrutinized argument is one of the
 * recursive pattern variables. */
static bool sj_recursion_guarded(Atom *t, Atom *name, size_t arity, size_t k,
                                 Atom **rec_vars, size_t rec_count) {
    if (!t) return true;
    if (t->kind == ATOM_SYMBOL) return !atom_eq(t, name);
    if (t->kind != ATOM_EXPR) return true;
    if (t->expr.len >= 1u && atom_eq(t->expr.elems[0], name)) {
        if (k >= arity || t->expr.len < arity + 1u) return false;
        Atom *scrut = t->expr.elems[1u + k];
        bool ok = false;
        for (size_t i = 0u; i < rec_count && !ok; i++) ok = atom_eq(rec_vars[i], scrut);
        if (!ok) return false;
        for (CettaExprIndex i = 1u; i < t->expr.len; i++)
            if (!sj_recursion_guarded(t->expr.elems[i], name, arity, k, rec_vars, rec_count))
                return false;
        return true;
    }
    for (CettaExprIndex i = 0u; i < t->expr.len; i++)
        if (!sj_recursion_guarded(t->expr.elems[i], name, arity, k, rec_vars, rec_count))
            return false;
    return true;
}


/* ------------------------------------------------------------------------ */
/* Inductive types.  `(set:inductive T (u n) (c type) ...)` declares a fresh   */
/* type freely generated by fresh constructors whose arguments are either T   */
/* itself (a recursive position) or types that do not mention T.  It          */
/* publishes the declarations, the datatype record that definitions by        */
/* structural recursion are justified by, and the induction principle T-ind   */
/* derived from the constructors.                                            */
/* ------------------------------------------------------------------------ */

static bool sj_mentions_symbol(Atom *t, Atom *name) {
    if (!t) return false;
    if (t->kind == ATOM_SYMBOL) return atom_eq(t, name);
    if (t->kind != ATOM_EXPR) return false;
    for (CettaExprIndex i = 0u; i < t->expr.len; i++)
        if (sj_mentions_symbol(t->expr.elems[i], name)) return true;
    return false;
}

/* Generated binders must not shadow a user declaration or field-type name.
 * The preferred spelling is retained only when it is fresh for this input. */
static Atom *sj_inductive_binder(Arena *a, Atom *judgment,
                                 Atom *rec_name, Atom *principle_name,
                                 const char *preferred) {
    size_t capacity = strlen(preferred) + 32u;
    char *spelling = arena_alloc(a, capacity);
    Atom *candidate = sj_sym(a, preferred);
    for (size_t suffix = 1u;
         sj_mentions_symbol(judgment, candidate) ||
         atom_eq(candidate, rec_name) || atom_eq(candidate, principle_name);
         suffix++) {
        snprintf(spelling, capacity, "%s~%zu", preferred, suffix);
        candidate = sj_sym(a, spelling);
    }
    return candidate;
}

/* Formation is earned by the existing typing authority, not by recognizing
 * the spelling of a field type.  The temporary declaration view is never
 * published: an unknown or rejected field leaves the caller's theory intact.
 * Constructor types must inhabit the datatype's universe; the eliminator may
 * inhabit a higher universe. */
static Atom *sj_declaration_check_obstruction(
    Arena *a, Space *view, Atom *judgment, Atom *subject, Atom *query,
    CettaPrimeRegularKernelBudget *budget, Atom **canonical_term_out) {
    if (canonical_term_out) *canonical_term_out = NULL;
    if (budget->limited && budget->remaining == 0u)
        return sj_incomplete(a, judgment,
                             sj_expr2(a, "set:declaration-budget", subject));
    CettaPrimeTypingResourceObservationV1 resources;
    Atom *checked = prime_semantics_judge_typing_accounted(
        a, view, query, budget->limited, budget->remaining, &resources,
        canonical_term_out);
    if (checked && budget->limited) {
        budget->remaining = resources.remaining;
        budget->spent += resources.spent;
    }
    Atom *reason = NULL;
    SjStatus status = sj_verdict_status(checked, &reason);
    if (status == SJ_STATUS_ESTABLISHED) return NULL;
    return sj_verdict(a, sj_status_name(status), judgment,
                     sj_expr3(a, "set:declaration-check", subject,
                              reason ? reason : sj_expr1(a, "formation-unavailable")));
}

static Atom *sj_inductive_check_declarations(
    Arena *a, SjSetEnv *env, Atom *judgment, Atom *type, Atom *universe,
    Atom **names, Atom **types, size_t ctor_count, Atom *rec_name,
    Atom *rec_type, Atom *principle_name, Atom *principle,
    CettaPrimeRegularKernelBudget *budget) {
    Space view;
    space_init_overlay(&view, &env->overlay);
    Atom *obstruction = sj_declaration_check_obstruction(
        a, &view, judgment, type, sj_expr2(a, "type:formed", universe), budget, NULL);
    if (!obstruction)
        space_add(&view, sj_expr3(a, ":", type, universe));
    for (size_t i = 0u; i < ctor_count && !obstruction; i++) {
        obstruction = sj_declaration_check_obstruction(
            a, &view, judgment, names[i],
            sj_expr3(a, "type:check", types[i], universe), budget, NULL);
        if (!obstruction)
            space_add(&view, sj_expr3(a, ":", names[i], types[i]));
    }
    if (!obstruction)
        obstruction = sj_declaration_check_obstruction(
            a, &view, judgment, rec_name, sj_expr2(a, "type:formed", rec_type), budget, NULL);
    if (!obstruction)
        obstruction = sj_declaration_check_obstruction(
            a, &view, judgment, principle_name,
            sj_expr3(a, "type:check", principle, sj_sym(a, "prop")), budget, NULL);
    space_free(&view);
    return obstruction;
}

static Atom *sj_set_inductive(Arena *a, Space *space, Atom *judgment,
                              bool limited, uint64_t steps) {
    if (limited && steps == 0u)
        return sj_incomplete(a, judgment, sj_expr1(a, "set:inductive-budget"));
    CettaPrimeRegularKernelBudget budget;
    cetta_prime_regular_kernel_budget_init(&budget, limited, steps);
    CettaExprLen len = judgment->expr.len;
    if (len < 4u) return sj_refuted(a, judgment, sj_expr1(a, "set:inductive-arity"));
    Atom *type = judgment->expr.elems[1];
    Atom *universe = judgment->expr.elems[2];
    if (!type || type->kind != ATOM_SYMBOL)
        return sj_refuted(a, judgment, sj_expr1(a, "set:inductive-name"));
    if (!sj_is_expr(universe, "u", 2u))
        return sj_refuted(a, judgment, sj_expr2(a, "set:inductive-universe", universe));
    SjSetEnv *env = sj_env(space);
    if (!env)
        return sj_undetermined(a, judgment, sj_expr1(a, "set:signature-unavailable"));
    SjProofState st;
    sj_proof_state_init(&st, a, space, env, false, 0u, false, true);
    if (sj_constant_type(&st, type) || sj_definition_of(type))
        return sj_refuted(a, judgment, sj_expr2(a, "set:inductive-already-declared", type));
    size_t ctor_count = len - 3u;
    if (ctor_count > 8u) return sj_refuted(a, judgment, sj_expr1(a, "set:inductive-constructor-bound"));
    Atom *names[8] = {0};
    Atom *types[8] = {0};
    for (size_t i = 0u; i < ctor_count; i++) {
        Atom *c = judgment->expr.elems[3u + i];
        if (!c || c->kind != ATOM_EXPR || c->expr.len != 3u || c->expr.elems[1]->kind != ATOM_SYMBOL)
            return sj_refuted(a, judgment, sj_expr2(a, "set:inductive-constructor", c));
        Atom *name = c->expr.elems[1];
        Atom *ctype = c->expr.elems[2];
        for (size_t j = 0u; j < i; j++)
            if (atom_eq(names[j], name))
                return sj_refuted(a, judgment, sj_expr2(a, "set:inductive-duplicate", name));
        if (atom_eq(name, type) || sj_constant_type(&st, name) || sj_definition_of(name))
            return sj_refuted(a, judgment, sj_expr2(a, "set:inductive-already-declared", name));
        /* result type T; each argument is T or a type not mentioning T */
        if (atom_eq(ctype, type)) {
            names[i] = name;
            types[i] = ctype;
            continue;
        }
        if (ctype->kind != ATOM_EXPR || ctype->expr.len < 3u ||
            !atom_is_symbol(ctype->expr.elems[0], "->") ||
            !atom_eq(ctype->expr.elems[ctype->expr.len - 1u], type))
            return sj_refuted(a, judgment, sj_expr2(a, "set:inductive-result-type", c));
        for (CettaExprIndex k = 1u; k + 1u < ctype->expr.len; k++) {
            Atom *arg = ctype->expr.elems[k];
            if (atom_eq(arg, type)) continue;
            if (sj_mentions_symbol(arg, type))
                return sj_refuted(a, judgment, sj_expr2(a, "set:inductive-positivity", c));
            if (!sj_open_domain(&st, arg))
                return sj_refuted(a, judgment, sj_expr2(a, "set:inductive-argument-type", arg));
        }
        names[i] = name;
        types[i] = ctype;
    }
    const char *type_spelling = atom_to_string(a, type);
    size_t generated_capacity = strlen(type_spelling) + 5u;
    char *generated_spelling = arena_alloc(a, generated_capacity);
    snprintf(generated_spelling, generated_capacity, "%s-ind", type_spelling);
    Atom *principle_name = sj_sym(a, generated_spelling);
    snprintf(generated_spelling, generated_capacity, "%s-rec", type_spelling);
    Atom *rec_name = sj_sym(a, generated_spelling);
    if (sj_constant_type(&st, principle_name) || sj_definition_of(principle_name))
        return sj_refuted(a, judgment, sj_expr2(a, "set:inductive-already-declared", principle_name));
    if (sj_constant_type(&st, rec_name) || sj_definition_of(rec_name))
        return sj_refuted(a, judgment, sj_expr2(a, "set:inductive-already-declared", rec_name));
    for (size_t i = 0u; i < ctor_count; i++) {
        if (atom_eq(names[i], rec_name) || atom_eq(names[i], principle_name))
            return sj_refuted(a, judgment,
                              sj_expr2(a, "set:inductive-duplicate", names[i]));
    }
    /* The induction principle: all P : T -> prop. case_1 -> ... -> all x : T. P x */
    Atom *P = sj_inductive_binder(a, judgment, rec_name, principle_name, "P");
    Atom *x = sj_inductive_binder(a, judgment, rec_name, principle_name, "x");
    Atom *conclusion = sj_expr3(a, "all", type, sj_expr3(a, "lam", x, atom_expr2(a, P, x)));
    Atom *body = conclusion;
    for (size_t i = ctor_count; i > 0u; i--) {
        Atom *ctype = types[i - 1u];
        size_t arity = atom_eq(ctype, type) ? 0u : (size_t)(ctype->expr.len - 2u);
        Atom **vars = arena_alloc(a, sizeof(Atom *) * (arity ? arity : 1u));
        Atom *applied = names[i - 1u];
        Atom **items = arena_alloc(a, sizeof(Atom *) * (arity + 1u));
        items[0] = names[i - 1u];
        for (size_t k = 0u; k < arity; k++) {
            char buf[32];
            snprintf(buf, sizeof buf, "v%zu", k + 1u);
            vars[k] = sj_inductive_binder(a, judgment, rec_name, principle_name, buf);
            items[1u + k] = vars[k];
        }
        if (arity > 0u) applied = atom_expr(a, items, (CettaExprLen)(arity + 1u));
        Atom *kase = atom_expr2(a, P, applied);
        for (size_t k = arity; k > 0u; k--) {
            if (atom_eq(ctype->expr.elems[k], type))
                kase = sj_expr3(a, "imp", atom_expr2(a, P, vars[k - 1u]), kase);
        }
        for (size_t k = arity; k > 0u; k--)
            kase = sj_expr3(a, "all", ctype->expr.elems[k], sj_expr3(a, "lam", vars[k - 1u], kase));
        body = sj_expr3(a, "imp", kase, body);
    }
    Atom *principle = sj_expr3(a, "all", sj_expr3(a, "->", type, sj_sym(a, "prop")),
                               sj_expr3(a, "lam", P, body));
    /* The recursor: T-rec : (P : T -> (u 0)) -> case_1 -> ... -> case_k -> (t : T) -> P t,
     * with case_i = (v1 : A1) -> ... -> (vm : Am) -> (h : P v_j) ... -> P (c_i v1 .. vm),
     * and its computation rules, one per constructor, in the kernel's spelling.
     * They belong to the supported fresh, strictly-positive constructor
     * fragment; formation is checked separately before publication. */
    Atom *Pm = P;
    Atom *tvar = sj_inductive_binder(a, judgment, rec_name, principle_name, "t");
    Atom *rec_type = sj_expr3(a, "->", sj_expr3(a, ":", tvar, type),
                              atom_expr2(a, Pm, tvar));
    Atom **rule_atoms = arena_alloc(a, sizeof(Atom *) * (ctor_count ? ctor_count : 1u));
    for (size_t i = ctor_count; i > 0u; i--) {
        Atom *ctype = types[i - 1u];
        size_t arity = atom_eq(ctype, type) ? 0u : (size_t)(ctype->expr.len - 2u);
        /* the case type */
        Atom **vars = arena_alloc(a, sizeof(Atom *) * (arity ? arity : 1u));
        Atom **items = arena_alloc(a, sizeof(Atom *) * (arity + 1u));
        items[0] = names[i - 1u];
        for (size_t k = 0u; k < arity; k++) {
            char buf[32];
            snprintf(buf, sizeof buf, "v%zu", k + 1u);
            vars[k] = sj_inductive_binder(a, judgment, rec_name, principle_name, buf);
            items[1u + k] = vars[k];
        }
        Atom *applied = arity ? atom_expr(a, items, (CettaExprLen)(arity + 1u)) : names[i - 1u];
        Atom *kase = atom_expr2(a, Pm, applied);
        for (size_t k = arity; k > 0u; k--) {
            if (atom_eq(ctype->expr.elems[k], type)) {
                char hb[32];
                snprintf(hb, sizeof hb, "h%zu", k);
                Atom *hyp = sj_inductive_binder(a, judgment, rec_name, principle_name, hb);
                kase = sj_expr3(a, "->", sj_expr3(a, ":", hyp, atom_expr2(a, Pm, vars[k - 1u])), kase);
            }
        }
        for (size_t k = arity; k > 0u; k--)
            kase = sj_expr3(a, "->", sj_expr3(a, ":", vars[k - 1u], ctype->expr.elems[k]), kase);
        char mb[32];
        snprintf(mb, sizeof mb, "m%zu", i);
        Atom *method = sj_inductive_binder(a, judgment, rec_name, principle_name, mb);
        rec_type = sj_expr3(a, "->", sj_expr3(a, ":", method, kase), rec_type);
    }
    rec_type = sj_expr3(a, "->", sj_expr3(a, ":", Pm, sj_expr3(a, "->", type, sj_expr2(a, "u", atom_int(a, 0)))),
                        rec_type);
    /* the rules: (T-rec (PVar 0) (PVar 1..k) (c_i (PVar k+1) .. (PVar k+m)))
     *   -> ((PVar i) (PVar k+1) .. (PVar k+m) (T-rec (PVar 0) (PVar 1..k) (PVar k+j)) ...) */
    for (size_t i = 0u; i < ctor_count; i++) {
        Atom *ctype = types[i];
        size_t arity = atom_eq(ctype, type) ? 0u : (size_t)(ctype->expr.len - 2u);
        size_t rec_arity = ctor_count + 2u;
        Atom **pats = arena_alloc(a, sizeof(Atom *) * rec_arity);
        for (size_t j = 0u; j < ctor_count + 1u; j++)
            pats[j] = sj_expr2(a, "PVar", atom_int(a, (int64_t)j));
        Atom *scrut = sj_expr2(a, "DeclConst", names[i]);
        for (size_t k = 0u; k < arity; k++)
            scrut = sj_expr3(a, "App", scrut, sj_expr2(a, "PVar", atom_int(a, (int64_t)(ctor_count + 1u + k))));
        pats[ctor_count + 1u] = scrut;
        Atom *rhs = sj_expr2(a, "PVar", atom_int(a, (int64_t)(i + 1u)));
        for (size_t k = 0u; k < arity; k++)
            rhs = sj_expr3(a, "App", rhs, sj_expr2(a, "PVar", atom_int(a, (int64_t)(ctor_count + 1u + k))));
        for (size_t k = 0u; k < arity; k++) {
            if (!atom_eq(ctype->expr.elems[1u + k], type)) continue;
            Atom *call = sj_expr2(a, "DeclConst", rec_name);
            for (size_t j = 0u; j < ctor_count + 1u; j++)
                call = sj_expr3(a, "App", call, sj_expr2(a, "PVar", atom_int(a, (int64_t)j)));
            call = sj_expr3(a, "App", call, sj_expr2(a, "PVar", atom_int(a, (int64_t)(ctor_count + 1u + k))));
            rhs = sj_expr3(a, "App", rhs, call);
        }
        Atom *rule_items[5] = {sj_sym(a, "type:rule"), rec_name, atom_int(a, (int64_t)rec_arity),
                               atom_expr(a, pats, (CettaExprLen)rec_arity), rhs};
        rule_atoms[i] = atom_expr(a, rule_items, 5u);
    }
    Atom *formation_obstruction = sj_inductive_check_declarations(
        a, env, judgment, type, universe, names, types, ctor_count,
        rec_name, rec_type, principle_name, principle, &budget);
    if (formation_obstruction) return formation_obstruction;
    /* the datatype record, the principle record, and the declarations */
    Atom **decl_items = arena_alloc(a, sizeof(Atom *) * (ctor_count + 2u));
    decl_items[0] = sj_sym(a, "inductive");
    decl_items[1] = universe;
    for (size_t i = 0u; i < ctor_count; i++) decl_items[2u + i] = judgment->expr.elems[3u + i];
    Atom *decl = atom_expr(a, decl_items, (CettaExprLen)(ctor_count + 2u));
    uint64_t revision = space_revision(space);
    Atom **published = arena_alloc(a, sizeof(Atom *) * (2u * ctor_count + 7u));
    size_t n = 0u;
    published[n++] = sj_sym(a, "SetPublish");
    published[n++] = sj_known_record(a, type, decl, "inductive", NULL, NULL, revision);
    published[n++] = sj_known_record(a, principle_name, principle, "inductive", NULL, NULL, revision);
    published[n++] = sj_expr3(a, ":", type, universe);
    for (size_t i = 0u; i < ctor_count; i++)
        published[n++] = sj_expr3(a, ":", names[i], types[i]);
    published[n++] = sj_expr3(a, ":", rec_name, rec_type);
    for (size_t i = 0u; i < ctor_count; i++)
        published[n++] = rule_atoms[i];
    return sj_established(a, judgment, atom_expr(a, published, (CettaExprLen)n));
}

/* A compiled set: rule (patterns and rhs over pattern indices, innermost
 * 0) in the kernel's spelling: constants as DeclConst, pattern variables as
 * (PVar j) with j the variable's position, polymorphic heads through the
 * instance chart. */
static Atom *sj_rule_term_to_kernel(SjProofState *st, Atom *t, size_t nvars, uint64_t depth) {
    Arena *a = st->arena;
    if (!t) return NULL;
    uint64_t index = 0u;
    if (sj_intrinsic_index(t, &index)) {
        if (index >= depth && index - depth < nvars)
            return sj_expr2(a, "PVar", atom_int(a, (int64_t)(nvars - 1u - (index - depth))));
        if (index >= depth)
            return sj_expr2(a, "idx", atom_int(a, (int64_t)(index - nvars)));
        return t;
    }
    if (sj_is_constant(t)) return sj_expr2(a, "DeclConst", t);
    if (sj_is_expr(t, "u", 2u))
        return sj_expr2(a, "Sort", sj_expr2(a, "LevelConst", t->expr.elems[1]));
    if (sj_is_leaf(t)) return t;
    Atom *head = NULL;
    Atom *args[4] = {0};
    size_t argc = sj_spine(t, &head, args, 4u);
    const char *poly = sj_constant_named(head, "all") ? "all"
                     : sj_constant_named(head, "eq") ? "eq" : NULL;
    if (poly && argc >= 1u && argc <= 4u) {
        Atom *type = NULL;
        Atom *term = sj_expr2(a, "DeclConst", sj_instance_constant(st, poly, args[0], &type));
        for (size_t i = 1u; i < argc; i++)
            term = sj_expr3(a, "App", term, sj_rule_term_to_kernel(st, args[i], nvars, depth));
        return term;
    }
    Atom *children[8] = {0};
    for (CettaExprIndex i = 1u; i < t->expr.len; i++) {
        bool under = sj_binds_body(t, i);
        children[i] = sj_rule_term_to_kernel(st, t->expr.elems[i], nvars, under ? depth + 1u : depth);
        if (!children[i]) return NULL;
    }
    return sj_rebuild(a, t, children);
}

static Atom *sj_type_rule_atom(SjProofState *st, Atom *head, size_t arity,
                               Atom **pats, Atom *rhs, size_t nvars) {
    Arena *a = st->arena;
    Atom **kp = arena_alloc(a, sizeof(Atom *) * (arity ? arity : 1u));
    for (size_t k = 0u; k < arity; k++) {
        kp[k] = sj_rule_term_to_kernel(st, pats[k], nvars, 0u);
        if (!kp[k]) return NULL;
    }
    Atom *krhs = sj_rule_term_to_kernel(st, rhs, nvars, 0u);
    if (!krhs) return NULL;
    Atom *items[5] = {sj_sym(a, "type:rule"), head, atom_int(a, (int64_t)arity),
                      atom_expr(a, kp, (CettaExprLen)arity), krhs};
    return atom_expr(a, items, 5u);
}

/* Closing a clause over its dependent pattern telescope lets the
 * existing native typing authority check both endpoints. The definition's
 * type is available in this private context, but none of its new computation
 * rules are: a clause cannot validate itself by executing its own equation. */
static Atom *sj_definition_clause_obstruction(
    SjProofState *st, Atom *judgment,
    Atom **domains, size_t count, Atom *left, Atom *right, Atom *result_type,
    CettaPrimeRegularKernelBudget *budget) {
    Arena *a = st->arena;
    Atom *closed_type = result_type;
    for (size_t i = count; i > 0u; i--) {
        closed_type = sj_expr3(a, "Pi", domains[i - 1u], closed_type);
        left = sj_expr3(a, "Lam", domains[i - 1u], left);
        right = sj_expr3(a, "Lam", domains[i - 1u], right);
    }
    sj_declare_mentioned(st, closed_type, NULL);
    sj_declare_mentioned(st, left, NULL);
    sj_declare_mentioned(st, right, NULL);
    Atom *kernel_type = sj_to_kernel_chart(st, closed_type);
    Atom *kernel_left = sj_to_kernel_chart(st, left);
    Atom *kernel_right = sj_to_kernel_chart(st, right);
    if (!kernel_type || !kernel_left || !kernel_right)
        return sj_undetermined(a, judgment, sj_expr1(a, "set:define-native-context"));
    Space view;
    space_init_overlay(&view, &st->env->overlay);
    space_add(&view, sj_expr3(a, ":", judgment->expr.elems[1],
                            judgment->expr.elems[2]));
    for (size_t i = 0u; i < st->instance_count; i++) {
        Atom **declared = NULL;
        uint32_t found = space_get_declared_types(
            &view, a, st->instance_names[i], &declared);
        free(declared);
        /* Existing declarations keep their schemas. The private view adds
         * only monomorphic HOL instances and otherwise absent signatures. */
        if (found != 0u) continue;
        Atom *type = cetta_prime_regular_term_quote_intrinsic_v1(
            a, sj_from_kernel(a, st->instance_types[i]));
        if (!type) {
            space_free(&view);
            return sj_undetermined(a, judgment, sj_expr1(a, "set:define-instance-type"));
        }
        space_add(&view, sj_expr3(a, ":", st->instance_names[i], type));
    }
    Atom *endpoints[2] = {kernel_left, kernel_right};
    Atom *subjects[2] = {left, right};
    Atom *obstruction = NULL;
    for (size_t i = 0u; i < 2u && !obstruction; i++) {
        CettaPrimeRegularKernelResult checked =
            prime_semantics_check_declared_intrinsic_v1(
                a, &view, endpoints[i], kernel_type, budget);
        if (checked.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) continue;
        const char *status =
            checked.status == CETTA_PRIME_REGULAR_KERNEL_REFUTED ? "Refuted"
            : checked.status == CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED ? "Incomplete"
            : "Undetermined";
        obstruction = sj_verdict(a, status, judgment,
            sj_expr3(a, "set:declaration-check", subjects[i],
                sj_expr1(a, checked.reason ? checked.reason : "declaration-check-unavailable")));
    }
    space_free(&view);
    return obstruction;
}

/* Scope lowering is shared with ordinary authored terms. Local matcher
 * variables are aliases of the clause telescope, never lexical binders.
 * The resulting term acquires typing only in the endpoint checks below. */
static Atom *sj_clause_lower(SjProofState *st, Atom *source,
                              Atom **variables, size_t count) {
    Atom *locals[16] = {0};
    for (size_t i = 0u; i < count; i++) locals[i] = variables[count - 1u - i];
    for (;;) {
        size_t capacity = st->env->signature_count + st->env->constant_count;
        Atom **globals = arena_alloc(st->arena, sizeof(*globals) * (capacity ? capacity : 1u));
        size_t global_count = 0u;
        for (size_t i = 0u; i < capacity; i++) {
            Atom *name = i < st->env->signature_count
                ? st->env->signature[i]->expr.elems[1]
                : st->env->constants[i - st->env->signature_count].name;
            bool duplicate = false;
            for (size_t j = 0u; j < global_count; j++)
                if (atom_eq(globals[j], name)) duplicate = true;
            if (!duplicate) globals[global_count++] = name;
        }
        CettaPrimeRegularTermEnvironmentV1 environment = {
            .names = (const Atom *const *)globals, .count = global_count,
            .local_names = (const Atom *const *)locals, .local_count = count,
        };
        CettaPrimeRegularTermElaborationV1 lowered =
            cetta_prime_regular_term_to_pattern_in_environment_v1(
                st->arena, environment, source, &st->budget);
        if (lowered.status == CETTA_PRIME_REGULAR_TERM_OK) {
            CettaPrimeRegularPatternEnvironmentV1 pattern_environment = {
                .bound_count = count,
                .declaration_names = (const Atom *const *)globals,
                .declaration_count = global_count,
            };
            CettaPrimeRegularPatternElaborationV1 intrinsic =
                cetta_prime_regular_pattern_elaborate_v1(
                    st->arena, pattern_environment, lowered.pattern, &st->budget);
            if (intrinsic.status == CETTA_PRIME_REGULAR_PATTERN_OK)
                return sj_from_kernel(st->arena, intrinsic.term);
            st->incomplete = intrinsic.status == CETTA_PRIME_REGULAR_PATTERN_BUDGET_EXHAUSTED;
            sj_fail(st, false, sj_expr2(st->arena, "set:define-pattern-elaboration",
                                      sj_sym(st->arena, intrinsic.reason ? intrinsic.reason : "outside-fragment")));
            return NULL;
        }
        if (lowered.status == CETTA_PRIME_REGULAR_TERM_BUDGET_EXHAUSTED)
            st->incomplete = true;
        Atom *missing = lowered.unresolved_name;
        if (!missing || !sj_constant_type(st, missing)) {
            sj_fail(st, false, sj_expr3(st->arena, "set:define-clause-elaboration", source,
                sj_sym(st->arena, lowered.reason ? lowered.reason : "outside-fragment")));
            return NULL;
        }
    }
}

/* Extend the actual pattern context. Previous arguments and the remaining
 * function type are weakened together; the new domain stays in its prior
 * context. This also handles constructor fields that depend on earlier ones. */
static bool sj_clause_bind(SjProofState *st, Atom *variable, Atom *domain,
                            Atom **variables, Atom **domains, size_t *count,
                            Atom **function_type, Atom **patterns, size_t prior_count) {
    if (*count >= 16u) {
        sj_fail(st, true, sj_expr1(st->arena, "set:define-variable-bound"));
        return false;
    }
    for (size_t i = 0u; i < *count; i++)
        if (atom_eq(variables[i], variable)) {
            sj_fail(st, true, sj_expr2(st->arena, "set:define-nonlinear", variable));
            return false;
        }
    variables[*count] = variable;
    domains[*count] = domain;
    (*count)++;
    *function_type = sj_shift(st->arena, *function_type, 0u, 1);
    for (size_t i = 0u; i < prior_count; i++)
        patterns[i] = sj_shift(st->arena, patterns[i], 0u, 1);
    return *function_type != NULL;
}

static Atom *sj_set_define(Arena *a, Space *space, Atom *judgment,
                            bool limited, uint64_t steps) {
    if (limited && steps == 0u)
        return sj_incomplete(a, judgment, sj_expr1(a, "set:define-budget"));
    CettaExprLen len = judgment->expr.len;
    if (len < 4u) return sj_refuted(a, judgment, sj_expr1(a, "set:define-arity"));
    Atom *name = judgment->expr.elems[1];
    Atom *type = judgment->expr.elems[2];
    if (!name || name->kind != ATOM_SYMBOL)
        return sj_refuted(a, judgment, sj_expr1(a, "set:define-name"));
    SjSetEnv *env = sj_env(space);
    if (!env)
        return sj_undetermined(a, judgment, sj_expr1(a, "set:signature-unavailable"));
    SjProofState st;
    sj_proof_state_init(&st, a, space, env, limited, steps, false, true);
    if (sj_constant_type(&st, name) || sj_definition_of(name))
        return sj_refuted(a, judgment, sj_expr2(a, "set:define-already-declared", name));
    Atom *canonical_type = NULL;
    Atom *obstruction = sj_declaration_check_obstruction(
        a, &env->overlay, judgment, type, sj_expr2(a, "type:formed", type),
        &st.budget, &canonical_type);
    if (obstruction) return obstruction;
    if (!canonical_type)
        return sj_undetermined(a, judgment, sj_expr2(a, "set:define-type-fragment", type));
    size_t type_arity = 0u;
    for (Atom *c = canonical_type; sj_is_expr(c, "Pi", 3u); c = c->expr.elems[2]) type_arity++;
    Atom *first_equation = judgment->expr.elems[3];
    if (!sj_is_expr(first_equation, "=", 3u))
        return sj_refuted(a, judgment, sj_expr2(a, "set:define-equation", first_equation));
    Atom *first_lhs = first_equation->expr.elems[1];
    size_t arity;
    if (atom_eq(first_lhs, name)) arity = 0u;
    else if (first_lhs && first_lhs->kind == ATOM_EXPR && first_lhs->expr.len >= 2u &&
             atom_eq(first_lhs->expr.elems[0], name))
        arity = first_lhs->expr.len - 1u;
    else return sj_refuted(a, judgment, sj_expr2(a, "set:define-left-side", first_lhs));
    if (arity > type_arity)
        return sj_refuted(a, judgment, sj_expr2(a, "set:define-left-side", first_lhs));
    /* Equation arity is authored, not the number of all Pi binders. A
     * definition may name a function or return a function after a prefix
     * of its arguments. Endpoint checking retains the remaining Pi type. */
    Atom *params[8] = {0};
    if (arity > 8u) return sj_refuted(a, judgment, sj_expr1(a, "set:define-arity-bound"));
    bool simple_parameters = sj_pi_result(a, canonical_type, arity, params) != NULL;
    size_t eq_count = len - 3u;
    if (eq_count > 8u) return sj_refuted(a, judgment, sj_expr1(a, "set:define-equation-bound"));

    /* The scrutinized position: the one where some equation has a
     * constructor pattern; the same for every equation. */
    size_t scrut = arity;
    for (size_t e = 0u; e < eq_count; e++) {
        Atom *eq = judgment->expr.elems[3u + e];
        if (!sj_is_expr(eq, "=", 3u))
            return sj_refuted(a, judgment, sj_expr2(a, "set:define-equation", eq));
        Atom *lhs = eq->expr.elems[1];
        if (arity == 0u ? !atom_eq(lhs, name)
                        : (lhs->kind != ATOM_EXPR || lhs->expr.len != arity + 1u ||
                           !atom_eq(lhs->expr.elems[0], name)))
            return sj_refuted(a, judgment, sj_expr2(a, "set:define-left-side", lhs));
        for (size_t k = 0u; k < arity; k++) {
            Atom *pat = lhs->expr.elems[1u + k];
            if (sj_is_var(pat)) continue;
            if (scrut != arity && scrut != k)
                return sj_refuted(a, judgment, sj_expr2(a, "set:define-two-scrutinees", lhs));
            scrut = k;
        }
    }
    SjConstructor ctors[8];
    size_t ctor_count = 0u;
    Atom *principle = NULL;
    bool covered[8] = {0};
    if (scrut != arity) {
        Atom *raw_domain = NULL;
        size_t outer = 0u;
        bool have_domain = false;
        if (simple_parameters) {
            raw_domain = params[scrut];
            have_domain = raw_domain != NULL;
        } else {
            have_domain = sj_pi_domain_at(canonical_type, arity, scrut, &raw_domain);
            outer = scrut;
        }
        if (!have_domain)
            return sj_undetermined(a, judgment, sj_expr2(a, "set:define-constructor-coverage-fragment", type));
        bool exhausted = false;
        Atom *scrut_type = sj_closed_inductive_reduct(&st, raw_domain, outer, &exhausted);
        if (!scrut_type) {
            if (exhausted || st.incomplete)
                return sj_incomplete(a, judgment, st.failure ? st.failure
                                     : sj_expr2(a, "set:normalization-fuel", raw_domain));
            return sj_undetermined(a, judgment, sj_expr2(a, "set:define-constructor-coverage-fragment", type));
        }
        if (!sj_datatype_constructors(&st, scrut_type, ctors, &ctor_count, &principle))
            return sj_undetermined(a, judgment, st.failure);
        if (eq_count != ctor_count)
            return sj_refuted(a, judgment, sj_expr2(a, "set:define-coverage", name));
    } else if (eq_count != 1u) {
        return sj_refuted(a, judgment, sj_expr2(a, "set:define-coverage", name));
    }

    Atom **rules = arena_alloc(a, sizeof(Atom *) * (eq_count + 2u));
    rules[0] = sj_sym(a, "rules");
    rules[1] = atom_int(a, (int64_t)arity);
    /* While the rules are checked, the constant being defined has its
     * declared type; the entry is dropped afterwards, so a refused
     * definition leaves nothing behind. */
    size_t saved_constants = env->constant_count;
    sj_constant_cache_add(env, name, canonical_type);
    /* The new head's formed type belongs to this private context even when
     * it quantifies over a universe. Its computation rules are still absent. */
    sj_declare_mentioned(&st, canonical_type, NULL);
    sj_note_instance(&st, name, canonical_type);
    for (size_t e = 0u; e < eq_count; e++) {
        Atom *eq = judgment->expr.elems[3u + e];
        Atom *lhs = eq->expr.elems[1];
        Atom *rhs = eq->expr.elems[2];
        Atom *vars[16] = {0};
        Atom *vtypes[16] = {0};
        size_t nvars = 0u;
        Atom *rec_vars[8] = {0};
        size_t rec_count = 0u;
        SjConstructor *ctor = NULL;
        Atom *pats[8] = {0};
        Atom *remaining_type = canonical_type;
        for (size_t k = 0u; k < arity; k++) {
            Atom *pat = lhs->expr.elems[1u + k];
            if (!sj_is_expr(remaining_type, "Pi", 3u))
                return env->constant_count = saved_constants,
                    sj_undetermined(a, judgment, sj_expr2(a, "set:define-dependent-domain", pat));
            if (sj_is_var(pat)) {
                if (!sj_clause_bind(&st, pat, remaining_type->expr.elems[1],
                                    vars, vtypes, &nvars, &remaining_type, pats, k))
                    return env->constant_count = saved_constants, sj_refuted(a, judgment, st.failure);
                pats[k] = sj_expr2(a, "idx", atom_int(a, 0));
                remaining_type = sj_subst(a, remaining_type->expr.elems[2], 0u, pats[k]);
                continue;
            }
            Atom *chead = pat->kind == ATOM_SYMBOL ? pat
                        : (pat->kind == ATOM_EXPR && pat->expr.len >= 2u ? pat->expr.elems[0] : NULL);
            size_t cargs = pat->kind == ATOM_SYMBOL ? 0u : (pat->kind == ATOM_EXPR ? pat->expr.len - 1u : 0u);
            for (size_t c = 0u; c < ctor_count && !ctor; c++)
                if (atom_eq(ctors[c].name, chead) && ctors[c].arity == cargs) ctor = &ctors[c];
            if (!ctor)
                return env->constant_count = saved_constants, sj_refuted(a, judgment, sj_expr2(a, "set:define-not-a-constructor", pat));
            size_t ci = (size_t)(ctor - ctors);
            if (covered[ci])
                return env->constant_count = saved_constants, sj_refuted(a, judgment, sj_expr2(a, "set:define-overlap", pat));
            covered[ci] = true;
            Atom *ctype = sj_constant_type(&st, ctor->name);
            if (!ctype)
                return env->constant_count = saved_constants, sj_undetermined(a, judgment, sj_expr2(a, "set:define-constructor-type", chead));
            ctype = sj_shift(a, ctype, 0u, (int64_t)nvars);
            Atom *constructor_term = ctor->name;
            for (size_t i = 0u; i < cargs; i++) {
                Atom *v = pat->expr.elems[1u + i];
                if (!sj_is_var(v))
                    return env->constant_count = saved_constants, sj_refuted(a, judgment, sj_expr2(a, "set:define-nested-pattern", pat));
                if (!sj_is_expr(ctype, "Pi", 3u))
                    return env->constant_count = saved_constants, sj_undetermined(a, judgment, sj_expr2(a, "set:define-constructor-type", chead));
                if (!sj_clause_bind(&st, v, ctype->expr.elems[1], vars, vtypes,
                                    &nvars, &remaining_type, pats, k))
                    return env->constant_count = saved_constants, sj_refuted(a, judgment, st.failure);
                ctype = sj_shift(a, ctype, 0u, 1);
                constructor_term = sj_shift(a, constructor_term, 0u, 1);
                Atom *argument = sj_expr2(a, "idx", atom_int(a, 0));
                constructor_term = sj_expr3(a, "App", constructor_term, argument);
                ctype = sj_subst(a, ctype->expr.elems[2], 0u, argument);
                if (ctor->recursive[i]) rec_vars[rec_count++] = v;
            }
            pats[k] = constructor_term;
            remaining_type = sj_subst(a, remaining_type->expr.elems[2], 0u, pats[k]);
        }
        if (!sj_recursion_guarded(rhs, name, arity, scrut, rec_vars, rec_count))
            return env->constant_count = saved_constants, sj_refuted(a, judgment, sj_expr2(a, "set:define-recursion", eq));
        Atom *crhs = sj_clause_lower(&st, rhs, vars, nvars);
        if (crhs) {
            Atom *locals[32] = {0};
            for (size_t j = 0u; j < nvars; j++) locals[j] = vtypes[j];
            crhs = sj_annotate(&st, crhs, locals, nvars);
        }
        if (!crhs)
            return env->constant_count = saved_constants,
                   st.incomplete ? sj_incomplete(a, judgment, st.failure)
                              : st.refuted ? sj_refuted(a, judgment, st.failure)
                              : sj_undetermined(a, judgment, st.failure);
        Atom *cleft = name;
        for (size_t k = 0u; k < arity; k++)
            cleft = sj_expr3(a, "App", cleft, pats[k]);
        obstruction = sj_definition_clause_obstruction(
            &st, judgment, vtypes, nvars,
            cleft, crhs, remaining_type, &st.budget);
        if (obstruction)
            return env->constant_count = saved_constants, obstruction;
        Atom *pat_list = atom_expr(a, pats, (CettaExprLen)arity);
        rules[2u + e] = sj_expr3(a, "", pat_list, crhs);
        /* rebuild as a 3-element list (pats rhs nvars) */
        Atom *rule_items[3] = {pat_list, crhs, atom_int(a, (int64_t)nvars)};
        rules[2u + e] = atom_expr(a, rule_items, 3u);
    }
    env->constant_count = saved_constants;
    for (size_t c = 0u; c < ctor_count; c++)
        if (!covered[c])
            return sj_refuted(a, judgment, sj_expr2(a, "set:define-missing-case", ctors[c].name));
    Atom *compiled = atom_expr(a, rules, (CettaExprLen)(eq_count + 2u));
    Atom **prop_items = arena_alloc(a, sizeof(Atom *) * (eq_count + 2u));
    prop_items[0] = sj_sym(a, "define");
    prop_items[1] = type;
    for (size_t e = 0u; e < eq_count; e++) prop_items[2u + e] = judgment->expr.elems[3u + e];
    Atom *prop = atom_expr(a, prop_items, (CettaExprLen)(eq_count + 2u));
    Atom *record = sj_known_record(a, name, prop, "definition", compiled, NULL,
                                   space_revision(space));
    Atom *declaration = sj_expr3(a, ":", name, type);
    (void)principle;
    Atom **published = arena_alloc(a, sizeof(Atom *) * (eq_count + 3u));
    size_t np = 0u;
    published[np++] = sj_sym(a, "SetPublish");
    published[np++] = record;
    published[np++] = declaration;
    for (size_t e = 0u; e < eq_count; e++) {
        Atom *rule = rules[2u + e];               /* ((pats) rhs nvars) */
        Atom *pat_list = rule->expr.elems[0];
        Atom **pats = pat_list->expr.elems;
        size_t nvars = (size_t)rule->expr.elems[2]->ground.ival;
        Atom *kernel_rule = sj_type_rule_atom(&st, name, arity, pats, rule->expr.elems[1], nvars);
        if (!kernel_rule)
            return sj_undetermined(a, judgment,
                                   sj_expr2(a, "set:define-rule-lowering", rule));
        published[np++] = kernel_rule;
    }
    return sj_established(a, judgment, atom_expr(a, published, (CettaExprLen)np));
}

static Atom *sj_set_recheck(Arena *a, Space *space, Atom *judgment, Atom *name, bool diagnostic,
                            bool limited, uint64_t steps) {
    SjSetEnv *env = sj_env(space);
    if (!env)
        return sj_undetermined(a, judgment, sj_expr1(a, "set:signature-unavailable"));
    bool ambiguous = false;
    Atom *record = sj_known_lookup(a, env, space, name, &ambiguous);
    if (ambiguous)
        return sj_undetermined(a, judgment, sj_expr2(a, "set:ambiguous-name", name));
    if (!record)
        return sj_undetermined(a, judgment, sj_expr2(a, "set:unknown-name", name));
    SjProofState st;
    sj_proof_state_init(&st, a, space, env, limited, steps, false, false);
    if (!sj_record_signature_current(record))
        return sj_undetermined(a, judgment, sj_expr2(a, "set:signature-changed", name));
    if (!atom_is_symbol(record->expr.elems[SJ_KNOWN_ORIGIN], "theorem")) {
        /* An assumption record is admitted as an assumption once its
         * proposition is formed; its origin stays visible. */
        Atom *goal = sj_elaborate_closed(&st, record->expr.elems[SJ_KNOWN_PROP], sj_sym(a, "prop"));
        if (!goal)
            return st.refuted ? sj_refuted(a, judgment, st.failure)
                              : sj_undetermined(a, judgment, st.failure);
        if (!diagnostic && !atom_is_symbol(record->expr.elems[SJ_KNOWN_ORIGIN], "signature"))
            sj_admit(a, space, record);
        return sj_established(a, judgment,
                              sj_expr2(a, "SetAssumption", record->expr.elems[SJ_KNOWN_ORIGIN]));
    }
    Atom *verdict = sj_prove(&st, judgment, record->expr.elems[SJ_KNOWN_PROOF],
                             record->expr.elems[SJ_KNOWN_PROP]);
    Atom *evidence = NULL;
    if (!diagnostic && sj_verdict_status(verdict, &evidence) == SJ_STATUS_ESTABLISHED)
        sj_admit(a, space, record);
    return verdict;
}

static Atom *sj_set_known_view(Arena *a, Space *space, Atom *judgment,
                               Atom *name, int field) {
    SjSetEnv *env = sj_env(space);
    if (!env)
        return sj_undetermined(a, judgment, sj_expr1(a, "set:signature-unavailable"));
    bool ambiguous = false;
    Atom *record = sj_known_lookup(a, env, space, name, &ambiguous);
    if (ambiguous)
        return sj_undetermined(a, judgment, sj_expr2(a, "set:ambiguous-name", name));
    if (!record)
        return sj_undetermined(a, judgment, sj_expr2(a, "set:unknown-name", name));
    return sj_established(a, judgment,
                          sj_expr2(a, "PrimeScopedValue", record->expr.elems[field]));
}

/* ------------------------------------------------------------------------ */
/* `lang:` judgments.                                                         */
/* ------------------------------------------------------------------------ */

static Atom *sj_lang_languages(Arena *a, Atom *judgment) {
    Atom *items[64];
    size_t count = 0u;
    int misses = 0;
    for (int id = 0; id < 64 && count < 64u && misses < 4; id++) {
        const CettaLanguageSpec *spec = cetta_language_from_id((CettaLanguageId)id);
        if (!spec || !spec->canonical) {
            misses++;
            continue;
        }
        bool seen = false;
        for (size_t i = 0u; i < count && !seen; i++)
            seen = atom_is_symbol(items[i], spec->canonical);
        if (!seen) items[count++] = sj_sym(a, spec->canonical);
    }
    return sj_established(a, judgment,
                          sj_expr2(a, "PrimeScopedValue",
                                   atom_expr(a, items, (CettaExprLen)count)));
}

static Atom *sj_lang_parse(Arena *a, Atom *judgment, Atom *text) {
    if (!text || text->kind != ATOM_GROUNDED || text->ground.gkind != GV_STRING ||
        !text->ground.sval)
        return sj_refuted(a, judgment, sj_expr1(a, "lang:parse-expects-string"));
    Atom **atoms = NULL;
    int count = parse_metta_text(text->ground.sval, a, &atoms);
    if (count < 0) {
        free(atoms);
        return sj_refuted(a, judgment, sj_expr1(a, "lang:parse-error"));
    }
    Atom *forms = atom_expr(a, atoms, (CettaExprLen)count);
    free(atoms);
    return sj_established(a, judgment, sj_expr2(a, "PrimeScopedValue", forms));
}

static Atom *sj_lang_print(Arena *a, Atom *judgment, Atom *form) {
    char *text = atom_to_string(a, form);
    if (!text) return sj_undetermined(a, judgment, sj_expr1(a, "lang:print-failed"));
    return sj_established(a, judgment,
                          sj_expr2(a, "PrimeScopedValue", atom_string(a, text)));
}

static const CettaLanguageSpec *sj_language(Atom *language) {
    const char *name = language && language->kind == ATOM_SYMBOL
                           ? atom_name_cstr(language) : NULL;
    return name ? cetta_language_lookup(name) : NULL;
}

/* The diamond (may) native type over Prime's own evaluation: some produced
 * result satisfies the predicate, with the positive evidence the `type:may`
 * authority already produces.  The box that quantifies over predecessors is
 * not realized: it must not silently become "all future results". */
static Atom *sj_lang_native_type(Arena *a, Space *space, Atom *judgment,
                                 bool limited, uint64_t steps) {
    Atom *language = judgment->expr.elems[1];
    const CettaLanguageSpec *spec = sj_language(language);
    if (!spec)
        return sj_refuted(a, judgment, sj_expr2(a, "lang:unknown-language",
                                                language ? language : atom_unit(a)));
    if (spec->id != CETTA_LANGUAGE_PRIME)
        return sj_undetermined(a, judgment,
                               sj_expr2(a, "lang:no-admitted-native-checker", language));
    Atom *may = sj_expr3(a, "type:may",
                         sj_expr2(a, "PrimeEvaluate", judgment->expr.elems[2]),
                         judgment->expr.elems[3]);
    return prime_semantics_judge_typing_direct(a, space, may, limited, steps);
}

/* ------------------------------------------------------------------------ */
/* `lang:step`: one backward derivation step over an admitted presentation  */
/* whose rules are data.  The presentation of a catalog authority is parsed */
/* once; a goal is matched against every rule's conclusion pattern, and the  */
/* instantiated premises of each applicable rule are returned as data.  A    */
/* formal that the conclusion does not determine stays as `(FVar name)`: the */
/* step exposes the choice instead of inventing it.  Nothing is checked or   */
/* accepted here; `nik:check` remains the acceptance boundary.               */
/* ------------------------------------------------------------------------ */

typedef struct {
    const char *alias;
    Atom *presentation;
} SjPresentationCache;

static SjPresentationCache g_sj_presentations[16];
static size_t g_sj_presentation_count = 0u;
static Arena g_sj_presentation_arena;
static bool g_sj_presentation_arena_ready = false;

static Atom *sj_presentation_of(const char *alias) {
    for (size_t i = 0u; i < g_sj_presentation_count; i++)
        if (strcmp(g_sj_presentations[i].alias, alias) == 0)
            return g_sj_presentations[i].presentation;
    for (size_t i = 0u; i < cetta_prime_nik_authorities_v1_count; i++) {
        const CettaNikAuthorityV1 *authority = &cetta_prime_nik_authorities_v1[i];
        if (strcmp(authority->alias, alias) != 0) continue;
        if (!g_sj_presentation_arena_ready) {
            arena_init_detached(&g_sj_presentation_arena);
            g_sj_presentation_arena_ready = true;
        }
        Atom **atoms = NULL;
        int count = parse_metta_text(authority->presentation_metta,
                                     &g_sj_presentation_arena, &atoms);
        if (count < 1 || !atoms) return NULL;
        Atom *presentation = atoms[0];
        free(atoms);
        if (g_sj_presentation_count < 16u) {
            g_sj_presentations[g_sj_presentation_count].alias = authority->alias;
            g_sj_presentations[g_sj_presentation_count].presentation = presentation;
            g_sj_presentation_count++;
        }
        return presentation;
    }
    return NULL;
}

typedef struct {
    Atom *names[32];
    Atom *values[32];
    size_t count;
} SjPatternBindings;

static bool sj_pattern_match(Atom *pat, Atom *term, SjPatternBindings *b) {
    if (!pat || !term) return false;
    if (sj_is_expr(pat, "FVar", 2u)) {
        Atom *name = pat->expr.elems[1];
        for (size_t i = 0u; i < b->count; i++)
            if (atom_eq(b->names[i], name)) return atom_eq(b->values[i], term);
        if (b->count >= 32u) return false;
        b->names[b->count] = name;
        b->values[b->count] = term;
        b->count++;
        return true;
    }
    if (pat->kind == ATOM_EXPR && term->kind == ATOM_EXPR &&
        pat->expr.len == term->expr.len) {
        for (CettaExprIndex i = 0u; i < pat->expr.len; i++)
            if (!sj_pattern_match(pat->expr.elems[i], term->expr.elems[i], b))
                return false;
        return true;
    }
    return atom_eq(pat, term);
}

static Atom *sj_pattern_instantiate(Arena *a, Atom *pat, const SjPatternBindings *b) {
    if (!pat) return NULL;
    if (sj_is_expr(pat, "FVar", 2u)) {
        for (size_t i = 0u; i < b->count; i++)
            if (atom_eq(b->names[i], pat->expr.elems[1])) return b->values[i];
        return pat;
    }
    if (pat->kind != ATOM_EXPR) return pat;
    Atom **items = arena_alloc(a, sizeof(Atom *) * (size_t)pat->expr.len);
    for (CettaExprIndex i = 0u; i < pat->expr.len; i++)
        items[i] = sj_pattern_instantiate(a, pat->expr.elems[i], b);
    return atom_expr(a, items, pat->expr.len);
}

static Atom *sj_lang_step(Arena *a, Atom *judgment) {
    Atom *language = judgment->expr.elems[1];
    Atom *goal = judgment->expr.elems[2];
    if (!language || language->kind != ATOM_SYMBOL)
        return sj_refuted(a, judgment, sj_expr2(a, "lang:unknown-language",
                                                language ? language : atom_unit(a)));
    Atom *presentation = sj_presentation_of(atom_name_cstr(language));
    if (!presentation) {
        if (!sj_language(language))
            return sj_refuted(a, judgment, sj_expr2(a, "lang:unknown-language", language));
        return sj_undetermined(a, judgment,
                               sj_expr2(a, "lang:no-admitted-step-service", language));
    }
    /* (GInferenceLanguageV1 version constructors judgments rules conversion) */
    if (!sj_is_expr(presentation, "GInferenceLanguageV1", 6u))
        return sj_undetermined(a, judgment, sj_expr2(a, "lang:presentation-shape", language));
    Atom *rules = presentation->expr.elems[4];
    Atom *steps = sj_sym(a, "LNil");
    Atom **collected = arena_alloc(a, sizeof(Atom *) * 64u);
    size_t step_count = 0u;
    for (Atom *cursor = rules; sj_is_expr(cursor, "LCons", 3u); cursor = cursor->expr.elems[2]) {
        Atom *rule = cursor->expr.elems[1];
        /* (GRuleV1 name formals premises conclusion side) */
        if (!sj_is_expr(rule, "GRuleV1", 6u)) continue;
        SjPatternBindings b = {0};
        if (!sj_pattern_match(rule->expr.elems[4], goal, &b)) continue;
        Atom *premises = sj_sym(a, "LNil");
        Atom *items[64];
        size_t n = 0u;
        for (Atom *p = rule->expr.elems[3]; sj_is_expr(p, "LCons", 3u) && n < 64u; p = p->expr.elems[2])
            items[n++] = sj_pattern_instantiate(a, p->expr.elems[1], &b);
        for (size_t i = n; i > 0u; i--)
            premises = sj_expr3(a, "LCons", items[i - 1u], premises);
        /* the rule's formals, instantiated in declaration order: exactly the
         * arguments a certificate's rule instance carries */
        Atom *args = sj_sym(a, "LNil");
        Atom *formals[32];
        size_t fn = 0u;
        for (Atom *f = rule->expr.elems[2]; sj_is_expr(f, "LCons", 3u) && fn < 32u; f = f->expr.elems[2]) {
            Atom *formal = f->expr.elems[1];   /* (Formal "name" arity) */
            Atom *name = sj_is_expr(formal, "Formal", 3u) ? formal->expr.elems[1] : NULL;
            Atom *value = name ? sj_pattern_instantiate(a, sj_expr2(a, "FVar", name), &b) : formal;
            formals[fn++] = value;
        }
        for (size_t i = fn; i > 0u; i--)
            args = sj_expr3(a, "LCons", formals[i - 1u], args);
        if (step_count < 64u) {
            Atom *step_items[4] = {sj_sym(a, "GStepV1"), rule->expr.elems[1], args, premises};
            collected[step_count++] = atom_expr(a, step_items, 4u);
        }
    }
    for (size_t i = step_count; i > 0u; i--)
        steps = sj_expr3(a, "LCons", collected[i - 1u], steps);
    return sj_established(a, judgment,
                          sj_expr2(a, "PrimeScopedValue", sj_expr2(a, "GStepsV1", steps)));
}

/* ------------------------------------------------------------------------ */
/* `try`: one evaluation of a judgment with the outcome made explicit.       */
/* Nothing is stored and nothing is re-run.  A `try` of a publishing         */
/* judgment reports the record it would publish and publishes nothing.       */
/* ------------------------------------------------------------------------ */

static Atom *sj_judge(Arena *a, Space *space, Atom *judgment,
                      bool steps_limited, uint64_t steps, bool diagnostic);

static Atom *sj_try(Arena *a, Space *space, Atom *judgment, Atom *subject,
                    bool limited, uint64_t steps) {
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_SCOPED_TRY);
    SymbolId head = atom_head_symbol_id(subject);
    Atom *verdict = NULL;
    /* A type query naming its space routes to that space, as it does
     * outside `try`. */
    if (subject && subject->kind == ATOM_EXPR && subject->expr.len >= 2u &&
        head != SYMBOL_ID_NONE && !prime_scoped_judgment_is_head_id(head)) {
        Space *explicit = prime_public_space_argument(a, subject->expr.elems[1]);
        if (explicit) {
            CettaExprLen n = subject->expr.len;
            Atom **items = arena_alloc(a, sizeof(Atom *) * (n - 1u));
            items[0] = subject->expr.elems[0];
            for (CettaExprIndex i = 2u; i < n; i++) items[i - 1u] = subject->expr.elems[i];
            subject = atom_expr(a, items, (CettaExprLen)(n - 1u));
            space = explicit;
        }
    }
    if (head != SYMBOL_ID_NONE && head != g_builtin_syms.try_judgment &&
        prime_scoped_judgment_is_head_id(head)) {
        verdict = sj_judge(a, space, subject, limited, steps, true);
    } else if (head == g_builtin_syms.type_colon_may ||
               head == g_builtin_syms.type_colon_must) {
        Atom *query = subject;
        if (subject->expr.len == 3u &&
            !sj_is_expr(subject->expr.elems[1], "PrimeEvaluate", 2u))
            query = atom_expr3(a, subject->expr.elems[0],
                               sj_expr2(a, "PrimeEvaluate", subject->expr.elems[1]),
                               subject->expr.elems[2]);
        verdict = prime_semantics_judge_typing_direct(a, space, query, limited, steps);
    } else if (head == g_builtin_syms.type_colon_of ||
               head == g_builtin_syms.type_colon_check ||
               head == g_builtin_syms.type_colon_analyze ||
               head == g_builtin_syms.type_colon_eq ||
               head == g_builtin_syms.type_colon_formed ||
               head == g_builtin_syms.type_colon_refine) {
        verdict = prime_semantics_judge_typing_direct(a, space, subject, limited, steps);
    } else if (head == g_builtin_syms.nik_colon_check) {
        verdict = prime_semantics_check_nik_direct(a, space, subject, limited, steps);
    }
    if (!verdict) return NULL;
    Atom *evidence = NULL;
    SjStatus status = sj_verdict_status(verdict, &evidence);
    if (status == SJ_STATUS_INVALID) return NULL;
    Atom *value = atom_expr2(a, sj_sym(a, sj_status_name(status)),
                             evidence ? evidence : atom_unit(a));
    return sj_established(a, judgment, sj_expr2(a, "PrimeScopedValue", value));
}

/* ------------------------------------------------------------------------ */
/* Identity profile: the active policy as data, and region declaration where */
/* the policy admits regions.  A region here is an explicit ASSUMPTION,       */
/* published into the space as the declaration of a per-carrier constant      */
/* `id:assumed-unique@A : Π a b (p q : id A a b). id (id A a b) p q`, used    */
/* through elimination and never as a conversion rule.  It is not a proved   */
/* region: nothing derives it from decidable equality, and the name says so. */
/* The space carries it visibly, so its consequences and withdrawal can run. */
/* ------------------------------------------------------------------------ */

static Atom *sj_identity_policy(Arena *a, Atom *judgment) {
    const char *name = "identity-j";
    switch (cetta_prime_identity_policy()) {
    case CETTA_PRIME_IDENTITY_SCOPED: name = "identity-scoped"; break;
    case CETTA_PRIME_IDENTITY_UIP: name = "identity-uip"; break;
    case CETTA_PRIME_IDENTITY_UNIVALENCE: name = "identity-univalence"; break;
    default: break;
    }
    return sj_established(a, judgment, sj_expr2(a, "PrimeScopedValue", sj_sym(a, name)));
}

static Atom *sj_identity_region(Arena *a, Atom *judgment, Atom *carrier) {
    int policy = cetta_prime_identity_policy();
    if (policy != CETTA_PRIME_IDENTITY_SCOPED &&
        policy != CETTA_PRIME_IDENTITY_UNIVALENCE)
        return sj_undetermined(a, judgment,
                               sj_expr2(a, "id:region-outside-profile", carrier));
    char name[96];
    char *shown = NULL;
    if (carrier && carrier->kind == ATOM_SYMBOL) {
        snprintf(name, sizeof name, "id:assumed-unique@%s", atom_name_cstr(carrier));
        shown = atom_to_string(a, carrier);
    } else if (sj_is_expr(carrier, "u", 2u) && carrier->expr.elems[1] &&
               carrier->expr.elems[1]->kind == ATOM_GROUNDED &&
               carrier->expr.elems[1]->ground.gkind == GV_INT) {
        snprintf(name, sizeof name, "id:assumed-unique@u%lld",
                 (long long)carrier->expr.elems[1]->ground.ival);
        shown = atom_to_string(a, carrier);
    }
    if (!shown)
        return sj_refuted(a, judgment, sj_expr2(a, "id:region-not-a-carrier", carrier));
    char text[512];
    snprintf(text, sizeof text,
             "(: %s (-> (a : %s) (b : %s) (p : (id %s a b)) (q : (id %s a b))"
             " (id (id %s a b) p q)))",
             name, shown, shown, shown, shown, shown);
    Atom **atoms = NULL;
    int count = parse_metta_text(text, a, &atoms);
    Atom *declaration = count == 1 && atoms ? atoms[0] : NULL;
    free(atoms);
    if (!declaration)
        return sj_undetermined(a, judgment, sj_expr2(a, "id:region-unparsed", carrier));
    return sj_established(a, judgment, sj_expr2(a, "SetPublish", declaration));
}

/* ------------------------------------------------------------------------ */
/* Entry point.                                                               */
/* ------------------------------------------------------------------------ */

static Atom *sj_judge(Arena *a, Space *space, Atom *judgment,
                      bool steps_limited, uint64_t steps, bool diagnostic);

Atom *prime_scoped_judgment_judge(Arena *a, Space *space, Atom *judgment,
                                  bool steps_limited, uint64_t steps) {
    return sj_judge(a, space, judgment, steps_limited, steps, false);
}

static Atom *sj_judge(Arena *a, Space *space, Atom *judgment,
                      bool steps_limited, uint64_t steps, bool diagnostic) {
    if (!a || !space || !judgment || judgment->kind != ATOM_EXPR ||
        judgment->expr.len == 0u)
        return NULL;
    SymbolId head = atom_head_symbol_id(judgment);
    if (!prime_scoped_judgment_is_head_id(head)) return NULL;
    /* `try` is counted once, by its own counter; the judgment it evaluates
     * is counted here when it arrives. */
    if (head != g_builtin_syms.try_judgment)
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_SCOPED_JUDGMENT_EXECUTION);
    const char *name = sj_head_name(judgment);
    CettaExprLen len = judgment->expr.len;

    if (head == g_builtin_syms.id_colon_policy) {
        if (len != 1u) return sj_refuted(a, judgment, sj_expr1(a, "id:policy-arity"));
        return sj_identity_policy(a, judgment);
    }
    if (head == g_builtin_syms.id_colon_region) {
        if (len != 2u) return sj_refuted(a, judgment, sj_expr1(a, "id:region-arity"));
        return sj_identity_region(a, judgment, judgment->expr.elems[1]);
    }
    if (head == g_builtin_syms.try_judgment) {
        if (len != 2u) return sj_refuted(a, judgment, sj_expr1(a, "try-arity"));
        return sj_try(a, space, judgment, judgment->expr.elems[1],
                      steps_limited, steps);
    }
    /* A quoted operand is the syntax it quotes: a program hands a judgment
     * syntax it built or received without the evaluator reading it. */
    for (CettaExprIndex i = 1u; i < len; i++) {
        Atom *op = judgment->expr.elems[i];
        if (!sj_is_expr(op, "quote", 2u)) continue;
        Atom **items = arena_alloc(a, sizeof(Atom *) * len);
        for (CettaExprIndex k = 0u; k < len; k++)
            items[k] = k == i ? judgment->expr.elems[k]->expr.elems[1] : judgment->expr.elems[k];
        judgment = atom_expr(a, items, len);
    }
    /* An explicit theory space as the first operand: the judgment is read
     * in that space, so a program can keep its own space free of the
     * object theory's declarations. */
    if (len >= 2u) {
        Space *explicit = prime_public_space_argument(a, judgment->expr.elems[1]);
        if (explicit) {
            Atom **items = arena_alloc(a, sizeof(Atom *) * (len - 1u));
            items[0] = judgment->expr.elems[0];
            for (CettaExprIndex i = 2u; i < len; i++) items[i - 1u] = judgment->expr.elems[i];
            judgment = atom_expr(a, items, (CettaExprLen)(len - 1u));
            len = judgment->expr.len;
            space = explicit;
        }
    }
    if (head == g_builtin_syms.set_colon_signature) {
        SjSetEnv *env = sj_env(space);
        if (!env)
            return sj_undetermined(a, judgment, sj_expr1(a, "set:signature-unavailable"));
        Atom *value = atom_expr(a, env->signature, (CettaExprLen)env->signature_count);
        return sj_established(a, judgment, sj_expr2(a, "PrimeScopedValue", value));
    }
    if (head == g_builtin_syms.set_colon_signature_digest) {
        if (len != 1u)
            return sj_refuted(a, judgment, sj_expr1(a, "set:signature-digest-arity"));
        return sj_established(a, judgment,
                              sj_expr2(a, "PrimeScopedValue",
                                       atom_string(a, sj_signature_digest())));
    }
    if (head == g_builtin_syms.set_colon_formed)
        return sj_set_term_judgment(a, space, judgment, SJ_SET_FORMED,
                                    steps_limited, steps);
    if (head == g_builtin_syms.set_colon_of)
        return sj_set_term_judgment(a, space, judgment, SJ_SET_OF,
                                    steps_limited, steps);
    if (head == g_builtin_syms.set_colon_check)
        return sj_set_term_judgment(a, space, judgment, SJ_SET_CHECK,
                                    steps_limited, steps);
    if (head == g_builtin_syms.set_colon_eq)
        return sj_set_term_judgment(a, space, judgment, SJ_SET_EQ,
                                    steps_limited, steps);
    if (head == g_builtin_syms.set_colon_proves) {
        if (len != 3u) return sj_refuted(a, judgment, sj_expr1(a, "set:proves-arity"));
        return sj_set_proves(a, space, judgment, judgment->expr.elems[1],
                             judgment->expr.elems[2], steps_limited, steps,
                             diagnostic);
    }
    if (head == g_builtin_syms.set_colon_define)
        return sj_set_define(a, space, judgment, steps_limited, steps);
    if (head == g_builtin_syms.set_colon_inductive)
        return sj_set_inductive(a, space, judgment, steps_limited, steps);
    if (head == g_builtin_syms.set_colon_axiom) {
        if (len != 3u) return sj_refuted(a, judgment, sj_expr1(a, "set:axiom-arity"));
        Atom *axiom_name = judgment->expr.elems[1];
        if (!axiom_name || axiom_name->kind != ATOM_SYMBOL)
            return sj_refuted(a, judgment, sj_expr1(a, "set:axiom-name"));
        Atom *check = sj_set_term_judgment(
            a, space,
            sj_expr3(a, "set:check", judgment->expr.elems[2], sj_sym(a, "prop")),
            SJ_SET_CHECK, steps_limited, steps);
        Atom *evidence = NULL;
        SjStatus status = sj_verdict_status(check, &evidence);
        if (status != SJ_STATUS_ESTABLISHED)
            return sj_verdict(a, sj_status_name(status), judgment, evidence);
        Atom *record = sj_known_record(
            a, axiom_name, judgment->expr.elems[2], "axiom", NULL, NULL,
            space_revision(space));
        return sj_established(a, judgment, sj_expr2(a, "SetPublish", record));
    }
    if (head == g_builtin_syms.set_colon_theorem) {
        if (len != 4u) return sj_refuted(a, judgment, sj_expr1(a, "set:theorem-arity"));
        Atom *theorem_name = judgment->expr.elems[1];
        if (!theorem_name || theorem_name->kind != ATOM_SYMBOL)
            return sj_refuted(a, judgment, sj_expr1(a, "set:theorem-name"));
        return sj_set_theorem(a, space, judgment, theorem_name,
                              judgment->expr.elems[2], judgment->expr.elems[3],
                              steps_limited, steps);
    }
    if (head == g_builtin_syms.set_colon_native_proof) {
        if (len != 2u)
            return sj_refuted(a, judgment,
                              sj_expr1(a, "set:native-proof-arity"));
        Atom *theorem_name = judgment->expr.elems[1];
        if (!theorem_name || theorem_name->kind != ATOM_SYMBOL)
            return sj_refuted(a, judgment,
                              sj_expr1(a, "set:native-proof-name"));
        return sj_set_native_proof(a, space, judgment, theorem_name,
                                   steps_limited, steps);
    }
    if (head == g_builtin_syms.set_colon_native_use ||
        head == g_builtin_syms.set_colon_native_normalize) {
        if (len != 3u)
            return sj_refuted(a, judgment,
                sj_expr1(a, head == g_builtin_syms.set_colon_native_normalize
                    ? "set:native-normalize-arity" : "set:native-use-arity"));
        return sj_set_native_use(a, space, judgment,
                                 judgment->expr.elems[1],
                                 judgment->expr.elems[2],
                                 head == g_builtin_syms.set_colon_native_normalize,
                                 steps_limited, steps);
    }
    if (head == g_builtin_syms.set_colon_recheck) {
        if (len != 2u) return sj_refuted(a, judgment, sj_expr1(a, "set:recheck-arity"));
        return sj_set_recheck(a, space, judgment, judgment->expr.elems[1], diagnostic,
                              steps_limited, steps);
    }
    if (head == g_builtin_syms.set_colon_known_proof) {
        if (len != 2u) return sj_refuted(a, judgment, sj_expr1(a, "set:known-proof-arity"));
        return sj_set_known_view(a, space, judgment, judgment->expr.elems[1],
                                 SJ_KNOWN_PROOF);
    }
    if (head == g_builtin_syms.set_colon_known_proposition) {
        if (len != 2u)
            return sj_refuted(a, judgment, sj_expr1(a, "set:known-proposition-arity"));
        return sj_set_known_view(a, space, judgment, judgment->expr.elems[1],
                                 SJ_KNOWN_PROP);
    }
    if (head == g_builtin_syms.lang_colon_languages) {
        if (len != 1u) return sj_refuted(a, judgment, sj_expr1(a, "lang:languages-arity"));
        return sj_lang_languages(a, judgment);
    }
    if (head == g_builtin_syms.lang_colon_parse) {
        if (len != 2u) return sj_refuted(a, judgment, sj_expr1(a, "lang:parse-arity"));
        return sj_lang_parse(a, judgment, judgment->expr.elems[1]);
    }
    if (head == g_builtin_syms.lang_colon_print) {
        if (len != 2u) return sj_refuted(a, judgment, sj_expr1(a, "lang:print-arity"));
        return sj_lang_print(a, judgment, judgment->expr.elems[1]);
    }
    if (head == g_builtin_syms.lang_colon_native_type) {
        if (len != 4u)
            return sj_refuted(a, judgment, sj_expr1(a, "lang:native-type-arity"));
        return sj_lang_native_type(a, space, judgment, steps_limited, steps);
    }
    if (head == g_builtin_syms.lang_colon_step) {
        if (len != 3u) return sj_refuted(a, judgment, sj_expr1(a, "lang:step-arity"));
        return sj_lang_step(a, judgment);
    }
    return sj_undetermined(a, judgment,
                           sj_expr2(a, "unknown-judgment",
                                    name ? judgment->expr.elems[0] : atom_unit(a)));
}

/* Admission at the publication boundary: the evaluator calls this when a
 * published record has actually been added to its space. */
void prime_scoped_judgment_admit(Arena *a, Space *space, Atom *record) {
    if (!record || !sj_is_expr(record, "set:known", SJ_KNOWN_LEN)) return;
    sj_admit(a, space, record);
}

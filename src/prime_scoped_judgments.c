#include "prime_scoped_judgments.h"
#include "generated/prime_nik_authorities_v1.generated.h"
#include "prime_arith_oracle.h"

#include "lang.h"
#include "native_sha256.h"
#include "nik_direct_authority.h"
#include "parser.h"
#include "prime_regular_kernel.h"
#include "prime_regular_pattern.h"
#include "prime_semantics.h"
#include "eval.h"
#include "stats.h"

#include <pthread.h>
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

/* A name a program would define as a constant is refused when it is
 * reserved: an identity proof packages generate, or a name Prime interprets
 * itself (cetta_prime_reserved_name_reason) that the space already declares
 * through Prime's own library, with the reason; such a definition could not
 * be admitted beside that declaration.  Otherwise a theory may name its
 * constants `and`, `not` or `eval`: the definition takes effect, when run
 * and in judgments. */
static Atom *sj_reserved_definition(Arena *a, Space *space, Atom *judgment,
                                    Atom *name);

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
           head == g_builtin_syms.set_colon_family ||
           head == g_builtin_syms.set_colon_theorem ||
           head == g_builtin_syms.set_colon_native_proof ||
           head == g_builtin_syms.set_colon_native_use ||
           head == g_builtin_syms.set_colon_native_normalize ||
           head == g_builtin_syms.set_colon_native_link ||
           head == g_builtin_syms.set_colon_interpret ||
           head == g_builtin_syms.set_colon_recheck ||
           head == g_builtin_syms.set_colon_known_proof ||
           head == g_builtin_syms.set_colon_known_proposition ||
           head == g_builtin_syms.set_colon_signature_digest ||
           head == g_builtin_syms.set_colon_numerals ||
           head == g_builtin_syms.set_colon_oracle ||
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
/* The admitted `set:` signature.  `set` is the sort of all sets, above     */
/* every universe `(u l)` an author writes, and `class` is its sort.  The    */
/* quantifier and equality are declared once, over any type of `class`: a   */
/* set, the type of all sets, or a type of predicates or functions on them; */
/* no level variable is needed.  Every use is an instance at a simple type, */
/* and the proof checker lowers instances to simple constants for the       */
/* kernel, a specialization that keeps the higher-order source.  `prop`     */
/* lives in the tower universe `(u 0)`; the legacy `u0`/`u1` and HE `Type`  */
/* spellings are not accepted as declared base types by the declared route. */
/*                                                                            */
/* A `set:define` entry defines a constant of the signature.  It is checked   */
/* by the same judgment as a program's definitions, in order, against the    */
/* signature's own theory; the constant is then declared by it and computes  */
/* by its equations in every space.  A refused entry leaves the signature    */
/* unavailable rather than the constant opaque.  Falsity is defined, as the   */
/* proposition that implies every proposition.                               */
/*                                                                            */
/* The seeds are the eleven laws of the sets and their universes, as Lean's */
/* package of the tower inside the sets states them (`lawRows`): seven laws  */
/* of sets and four of `UnivOf`, each named by its Lean row.  Conjunction,   */
/* equivalence and the existential quantifier are written out by their      */
/* definitions from `imp` and `all`, as that package writes them, and       */
/* negation as implication of Falsum.  `Eps_set` has its type and no law.   */
/*                                                                            */
/* A `set:native-rule` entry names a rule constant of native proof packages  */
/* and the rule it is: the introduction or elimination of implication, of    */
/* the quantifier, or of the equation code.  A package declares the rule     */
/* constants at the carriers it compiles by them, with the types the native  */
/* compiler gives them over the package's proof family; they have no         */
/* equation, so the kernel computes nothing with them.                       */
/* ------------------------------------------------------------------------ */

static const char SJ_SET_SIGNATURE_TEXT[] =
    "(: prop (u 0))\n"
    "(: imp (-> prop (-> prop prop)))\n"
    "(: all (-> (A : class) (-> (-> A prop) prop)))\n"
    "(set:define Falsum prop (= Falsum (all prop (lam p p))))\n"
    "(: eq (-> (A : class) (-> A (-> A prop))))\n"
    "(: In (-> set (-> set prop)))\n"
    "(: Empty set)\n"
    "(: Union (-> set set))\n"
    "(: Power (-> set set))\n"
    "(: Sep (-> set (-> (-> set prop) set)))\n"
    "(: Repl (-> set (-> (-> set set) set)))\n"
    "(: Eps_set (-> (-> set prop) set))\n"
    "(: UnivOf (-> set set))\n"
    /* extensionality, Megalodon law_extensionality: sets with the same members are equal. */
    "(set:seed extensionality (all set (lam a (all set (lam b (imp (all set (lam x (all "
    "prop (lam r (imp (imp (imp (In x a) (In x b)) (imp (imp (In x b) (In x a)) r)) "
    "r))))) (eq set a b)))))))\n"
    /* emptyLaw, Megalodon law_empty: nothing is a member of Empty. */
    "(set:seed emptyLaw (all set (lam x (imp (In x Empty) Falsum))))\n"
    /* unionLaw, Megalodon law_union: the members of a union. */
    "(set:seed unionLaw (all set (lam a (all set (lam x (all prop (lam r (imp (imp (imp "
    "(In x (Union a)) (all prop (lam r (imp (all set (lam y (imp (all prop (lam r (imp "
    "(imp (In y a) (imp (In x y) r)) r))) r))) r)))) (imp (imp (all prop (lam r (imp (all"
    " set (lam y (imp (all prop (lam r (imp (imp (In y a) (imp (In x y) r)) r))) r))) "
    "r))) (In x (Union a))) r)) r))))))))\n"
    /* powerLaw, Megalodon law_power: the members of a power set. */
    "(set:seed powerLaw (all set (lam a (all set (lam x (all prop (lam r (imp (imp (imp "
    "(In x (Power a)) (all set (lam y (imp (In y x) (In y a))))) (imp (imp (all set (lam "
    "y (imp (In y x) (In y a)))) (In x (Power a))) r)) r))))))))\n"
    /* separationLaw, Megalodon law_separation: the members of a separated set. */
    "(set:seed separationLaw (all set (lam a (all (-> set prop) (lam P (all set (lam x "
    "(all prop (lam r (imp (imp (imp (In x (Sep a P)) (all prop (lam r (imp (imp (In x a)"
    " (imp (P x) r)) r)))) (imp (imp (all prop (lam r (imp (imp (In x a) (imp (P x) r)) "
    "r))) (In x (Sep a P))) r)) r))))))))))\n"
    /* replacementLaw, Megalodon law_replacement: the members of a replacement. */
    "(set:seed replacementLaw (all set (lam a (all (-> set set) (lam F (all set (lam y "
    "(all prop (lam r (imp (imp (imp (In y (Repl a F)) (all prop (lam r (imp (all set "
    "(lam x (imp (all prop (lam r (imp (imp (In x a) (imp (eq set (F x) y) r)) r))) r))) "
    "r)))) (imp (imp (all prop (lam r (imp (all set (lam x (imp (all prop (lam r (imp "
    "(imp (In x a) (imp (eq set (F x) y) r)) r))) r))) r))) (In y (Repl a F))) r)) "
    "r))))))))))\n"
    /* setInduction, Megalodon law_set_induction: induction on membership. */
    "(set:seed setInduction (all (-> set prop) (lam P (imp (all set (lam X (imp (all set "
    "(lam x (imp (In x X) (P x)))) (P X)))) (all set (lam X (P X)))))))\n"
    /* universeIn, Megalodon law_universe_in: a set is a member of its universe. */
    "(set:seed universeIn (all set (lam N (In N (UnivOf N)))))\n"
    /* universeTransitive, Megalodon law_universe_transitive: a universe is transitive. */
    "(set:seed universeTransitive (all set (lam N (all set (lam x (imp (In x (UnivOf N)) "
    "(all set (lam z (imp (In z x) (In z (UnivOf N)))))))))))\n"
    /* universeClosed, Megalodon law_universe_closed: a universe is closed. */
    "(set:seed universeClosed (all set (lam N (all prop (lam r (imp (imp (all set (lam x "
    "(imp (In x (UnivOf N)) (In (Union x) (UnivOf N))))) (imp (all prop (lam r (imp (imp "
    "(all set (lam x (imp (In x (UnivOf N)) (In (Power x) (UnivOf N))))) (imp (all set "
    "(lam a (imp (In a (UnivOf N)) (all (-> set set) (lam f (imp (all set (lam x (imp (In"
    " x a) (In (f x) (UnivOf N))))) (In (Repl a f) (UnivOf N)))))))) r)) r))) r)) r))))))\n"
    /* universeMinimal, Megalodon law_universe_minimal: a universe is the least such set. */
    "(set:seed universeMinimal (all set (lam N (all set (lam U (imp (In N U) (imp (all "
    "set (lam x (imp (In x U) (all set (lam z (imp (In z x) (In z U))))))) (imp (all prop"
    " (lam r (imp (imp (all set (lam x (imp (In x U) (In (Union x) U)))) (imp (all prop "
    "(lam r (imp (imp (all set (lam x (imp (In x U) (In (Power x) U)))) (imp (all set "
    "(lam a (imp (In a U) (all (-> set set) (lam f (imp (all set (lam x (imp (In x a) (In"
    " (f x) U)))) (In (Repl a f) U))))))) r)) r))) r)) r))) (all set (lam z (imp (In z "
    "(UnivOf N)) (In z U))))))))))))\n"
    "(set:native-rule impI imp-intro)\n"
    "(set:native-rule impE imp-elim)\n"
    "(set:native-rule allI all-intro)\n"
    "(set:native-rule allE all-elim)\n"
    "(set:native-rule eqI eq-intro)\n"
    "(set:native-rule eqE eq-elim)\n";

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
 * origin ∈ {signature, axiom, theorem, inductive, definition, family}; proof
 * is the proof of a theorem, of a family's constant the set model it was
 * admitted on, (set-model theorem), and of a definition the evidence it was
 * admitted on,
 *   (definition-evidence (rules arity rule ...) termination),
 * where termination is (structural position type (constructor (field ...)) ...),
 * listing for each equation the constructor fields its recursive calls
 * descend on, or (non-recursive), or (scrutinee-first form structural) for a
 * definition admitted through its scrutinee-first form; otherwise ();
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
    bool revoked;        /* a failed recheck withdrew this admission */
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
            return !entry->revoked;
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
        if (old[i].digest[0] != '\0' && !old[i].revoked)
            sj_admitted_insert(old[i].instance, old[i].digest);
    free(old);
}

/* Moves whenever admission publishes or withdraws anything, or the
 * signature's own records change: what was found admitted before may differ
 * after. */
static uint64_t g_admission_epoch = 1u;

uint64_t prime_scoped_judgment_admission_epoch(void) {
    return g_admission_epoch;
}

static void sj_admitted_insert(uint64_t instance, const char *digest) {
    g_admission_epoch++;
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
        if (entry->instance == instance && strcmp(entry->digest, digest) == 0) {
            entry->revoked = false;
            return;
        }
    }
}

/* Withdraw an admission: the record stays in its space as syntax, and is
 * reusable again only once admitted again. */
static void sj_admitted_revoke(uint64_t instance, const char *digest) {
    g_admission_epoch++;
    if (!g_admitted_cap) return;
    size_t slot = sj_admitted_slot(instance, digest);
    for (size_t probe = 0u; probe < g_admitted_cap; probe++) {
        SjAdmitted *entry = &g_admitted[(slot + probe) % g_admitted_cap];
        if (entry->digest[0] == '\0') return;
        if (entry->instance == instance && strcmp(entry->digest, digest) == 0) {
            entry->revoked = true;
            return;
        }
    }
}

void prime_scoped_judgment_admit(Arena *a, Space *space, Atom *record);

/* A space that takes over another's contents, as an import commits its
 * working copy, takes over what admission published there, and the records
 * of the theorems published there. */
static void sj_admission_follows_contents(uint64_t from, uint64_t to) {
    prime_semantics_theorem_ledger_follow(from, to);
    size_t count = 0u;
    for (size_t i = 0u; i < g_admitted_cap; i++)
        if (g_admitted[i].digest[0] != '\0' && !g_admitted[i].revoked &&
            g_admitted[i].instance == from)
            count++;
    if (count == 0u) return;
    char (*digests)[65] = cetta_malloc(sizeof(*digests) * count);
    size_t n = 0u;
    for (size_t i = 0u; i < g_admitted_cap; i++)
        if (g_admitted[i].digest[0] != '\0' && !g_admitted[i].revoked &&
            g_admitted[i].instance == from)
            memcpy(digests[n++], g_admitted[i].digest, sizeof digests[0]);
    for (size_t i = 0u; i < n; i++) sj_admitted_insert(to, digests[i]);
    free(digests);
}

void prime_scoped_judgment_follow_space_contents(void) {
    static bool follows_contents = false;
    if (!follows_contents) {
        space_set_contents_moved_hook(sj_admission_follows_contents);
        follows_contents = true;
    }
}

static void sj_admit(Arena *a, Space *space, Atom *record) {
    prime_scoped_judgment_follow_space_contents();
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

static void sj_withdraw(Arena *a, Space *space, Atom *record) {
    char digest[65];
    sj_record_digest(a, record, digest);
    sj_admitted_revoke(space_instance_id(space), digest);
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

/* The rules of the logic a native proof package can name by a constant. */
typedef enum {
    SJ_NATIVE_IMP_INTRO,
    SJ_NATIVE_IMP_ELIM,
    SJ_NATIVE_ALL_INTRO,
    SJ_NATIVE_ALL_ELIM,
    SJ_NATIVE_EQ_INTRO,
    SJ_NATIVE_EQ_ELIM,
    SJ_NATIVE_RULE_COUNT
} SjNativeRuleRole;

static const char *const SJ_NATIVE_RULE_ROLES[SJ_NATIVE_RULE_COUNT] = {
    "imp-intro", "imp-elim", "all-intro", "all-elim", "eq-intro", "eq-elim",
};

/* A functor whose stored atoms a space declares typed with
 * `(type:stored (F T1 ... Tn))`, read once per revision of the space: its
 * stored occurrences in the order of the space, their names, and their
 * arguments in kernel spelling.  A functor whose stored atoms do not all
 * have the declared types is kept with the reason, and gives nothing. */
typedef struct SjStoredFunctor {
    Atom *functor;
    Atom *declared;              /* (F T1 ... Tn) */
    size_t arity;
    size_t count;
    Atom **atoms;                /* the stored occurrences */
    Atom **names;                /* their names */
    Atom ***arguments;           /* [occurrence][position 1..arity], kernel */
    Atom *failure;               /* why the declaration does not hold */
} SjStoredFunctor;

typedef struct {
    bool parsed;
    Arena arena;                 /* process-lifetime owner of the parsed text */
    Atom **signature;            /* (: name type) declarations, authored or
                                    published by the signature's definitions */
    size_t signature_count;
    Atom **seeds;                /* known records of the signature's axioms */
    size_t seed_count;
    Atom **definitions;          /* authored (set:define ...) entries, in order */
    size_t *definition_positions; /* declarations authored before each */
    size_t definition_count;
    bool theory_checking;        /* its definitions are being checked */
    bool theory_ready;           /* its definitions have been checked */
    bool theory_failed;          /* one was refused: the signature is unavailable */
    Space theory;                /* the signature's own theory: the records and
                                    rules its definitions published */
    Atom **own_records;          /* those known records, owned by identity */
    size_t own_record_count;
    Atom **own_rules;            /* those type:rule atoms, owned by identity */
    size_t own_rule_count;
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
    Atom *native_rules[SJ_NATIVE_RULE_COUNT]; /* the rule constants, by role */
    struct SjStoredFunctor *stored; /* functors the space declares stored typed */
    size_t stored_count;
    Atom **stored_atoms;         /* the declarations and equations they give */
    size_t stored_atom_count;
} SjSetEnv;

static SjSetEnv g_set_env;

static bool sj_is_sort(Atom *t);

/* Whether a type binds a variable at a sort, `(A : (u l))` or `(A : class)`:
 * the type of a polymorphic head. */
static bool sj_type_binds_universe(Atom *type) {
    if (!type || type->kind != ATOM_EXPR) return false;
    if (type->expr.len == 3u && atom_is_symbol(type->expr.elems[1], ":") &&
        sj_is_sort(type->expr.elems[2]))
        return true;
    for (CettaExprIndex i = 0u; i < type->expr.len; i++)
        if (sj_type_binds_universe(type->expr.elems[i])) return true;
    return false;
}

/* A declaration joins the signature's classification of its names: a name
 * declared at a sort, `set` included, is a base type. */
static void sj_env_classify_declaration(SjSetEnv *env, Atom *declaration) {
    if (sj_is_sort(declaration->expr.elems[2]))
        env->base_types[env->base_type_count++] = declaration->expr.elems[1];
    else if (sj_type_binds_universe(declaration->expr.elems[2]))
        env->poly_heads[env->poly_count++] = declaration->expr.elems[1];
}

static bool sj_env_parse(SjSetEnv *env) {
    if (env->parsed) return true;
    arena_init_detached(&env->arena);
    Arena *a = &env->arena;
    Atom **atoms = NULL;
    int count = parse_metta_text(SJ_SET_SIGNATURE_TEXT, a, &atoms);
    if (count < 0 || !atoms) return false;
    /* A definition entry publishes up to two declarations (a definition and
     * its scrutinee-first form), so the declaration lists have room for two
     * per parsed entry. */
    env->signature = arena_alloc(a, sizeof(Atom *) * 2u * (size_t)count);
    env->seeds = arena_alloc(a, sizeof(Atom *) * (size_t)count);
    env->base_types = arena_alloc(a, sizeof(Atom *) * 2u * (size_t)count);
    env->poly_heads = arena_alloc(a, sizeof(Atom *) * 2u * (size_t)count);
    env->definitions = arena_alloc(a, sizeof(Atom *) * (size_t)count);
    env->definition_positions = arena_alloc(a, sizeof(size_t) * (size_t)count);
    for (int i = 0; i < count; i++) {
        Atom *atom = atoms[i];
        if (sj_is_expr(atom, ":", 3u)) {
            env->signature[env->signature_count++] = atom;
            sj_env_classify_declaration(env, atom);
        } else if (sj_is_expr(atom, "set:seed", 3u)) {
            env->seeds[env->seed_count++] = sj_known_record(
                a, atom->expr.elems[1], atom->expr.elems[2], "signature",
                NULL, NULL, 0u);
        } else if (atom && atom->kind == ATOM_EXPR && atom->expr.len >= 4u &&
                   atom_is_symbol(atom->expr.elems[0], "set:define")) {
            env->definition_positions[env->definition_count] = env->signature_count;
            env->definitions[env->definition_count++] = atom;
        } else if (sj_is_expr(atom, "set:native-rule", 3u) &&
                   atom->expr.elems[1]->kind == ATOM_SYMBOL) {
            for (size_t role = 0u; role < SJ_NATIVE_RULE_COUNT; role++)
                if (atom_is_symbol(atom->expr.elems[2], SJ_NATIVE_RULE_ROLES[role]))
                    env->native_rules[role] = atom->expr.elems[1];
        }
    }
    free(atoms);
    /* A definition publishes one known record and one declaration per
     * defined name, and one rule per equation (at most eight). */
    env->own_records = arena_alloc(a, sizeof(Atom *) * (2u * env->definition_count + 1u));
    env->own_rules = arena_alloc(a, sizeof(Atom *) * (16u * env->definition_count + 1u));
    env->parsed = true;
    return true;
}

static Atom *sj_set_define(Arena *a, Space *space, Atom *judgment,
                            bool limited, uint64_t steps);

/* Check the signature's definitions, in order, with `set:define` against the
 * signature's own theory space, and keep what each published: its known
 * record and rules in that theory, admitted there, and its declaration in
 * the signature at the definition's authored place.  Runs once per process.
 * A refused definition makes the signature unavailable. */
static bool sj_env_check_definitions(SjSetEnv *env) {
    if (env->theory_ready) return !env->theory_failed;
    if (env->theory_checking) return true;
    env->theory_checking = true;
    Arena *a = &env->arena;
    space_init(&env->theory);
    uint64_t theory = space_instance_id(&env->theory);
    size_t inserted = 0u;
    for (size_t d = 0u; d < env->definition_count && !env->theory_failed; d++) {
        Atom *verdict = sj_set_define(a, &env->theory, env->definitions[d], false, 0u);
        Atom *published = NULL;
        if (sj_verdict_status(verdict, &published) != SJ_STATUS_ESTABLISHED ||
            !published || published->kind != ATOM_EXPR ||
            published->expr.len < 2u ||
            !atom_is_symbol(published->expr.elems[0], "SetPublish")) {
            env->theory_failed = true;
            break;
        }
        size_t at = env->definition_positions[d] + inserted;
        for (CettaExprIndex i = 1u; i < published->expr.len; i++) {
            Atom *item = published->expr.elems[i];
            if (sj_is_expr(item, ":", 3u)) {
                for (size_t k = env->signature_count; k > at; k--)
                    env->signature[k] = env->signature[k - 1u];
                env->signature[at++] = item;
                env->signature_count++;
                inserted++;
                sj_env_classify_declaration(env, item);
                continue;
            }
            char digest[65];
            sj_record_digest(a, item, digest);
            space_add(&env->theory, item);
            sj_admitted_insert(theory, digest);
            if (sj_is_expr(item, "set:known", SJ_KNOWN_LEN))
                env->own_records[env->own_record_count++] = item;
            else if (sj_is_expr(item, "type:rule", 5u))
                env->own_rules[env->own_rule_count++] = item;
        }
    }
    env->theory_checking = false;
    env->theory_ready = true;
    g_admission_epoch++;
    /* The environment built while checking was the theory's own. */
    if (env->overlay_ready) {
        space_free(&env->overlay);
        env->overlay_ready = false;
    }
    return !env->theory_failed;
}

/* A known record or rule the signature's definitions published.  Identity
 * is the whole atom: a record merely tagged or named like one is not it. */
static bool sj_signature_owns(const SjSetEnv *env, Atom *atom) {
    if (!env->theory_ready || !atom) return false;
    for (size_t i = 0u; i < env->own_record_count; i++)
        if (atom_eq(env->own_records[i], atom)) return true;
    for (size_t i = 0u; i < env->own_rule_count; i++)
        if (atom_eq(env->own_rules[i], atom)) return true;
    return false;
}

/* The name of one of the signature's defined constants. */
static bool sj_signature_defines(const SjSetEnv *env, Atom *name) {
    if (!name || name->kind != ATOM_SYMBOL) return false;
    for (size_t i = 0u; i < env->own_record_count; i++)
        if (atom_is_symbol(env->own_records[i]->expr.elems[SJ_KNOWN_ORIGIN], "definition") &&
            atom_eq(env->own_records[i]->expr.elems[SJ_KNOWN_NAME], name))
            return true;
    return false;
}

/* The environment for `space`: the parsed signature plus an overlay of the
 * space carrying its declarations, rebuilt only on a new space or revision. */
/* Load the definition records admitted in `space` into the environment: one
 * indexed lookup, pointers into the space's own atoms. */
static void sj_env_add_definitions(SjSetEnv *env, Space *space) {
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
            !atom_is_symbol(atom->expr.elems[SJ_KNOWN_ORIGIN], "definition") ||
            !sj_record_admitted(a, space, atom))
            continue;
        Atom *evidence = atom->expr.elems[SJ_KNOWN_PROOF];
        if (!evidence || evidence->kind != ATOM_EXPR || evidence->expr.len < 3u ||
            !atom_is_symbol(evidence->expr.elems[0], "definition-evidence"))
            continue;
        Atom *compiled = evidence->expr.elems[1];
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

/* The signature's definitions, then the space's own. */
static void sj_env_load_definitions(SjSetEnv *env, Space *space) {
    for (size_t i = 0u; i < env->def_count; i++) {
        for (size_t r = 0u; r < env->defs[i].rule_count; r++) free(env->defs[i].rules[r].pats);
        free(env->defs[i].rules);
    }
    free(env->defs);
    env->defs = NULL;
    env->def_count = 0u;
    if (env->theory_ready && space != &env->theory)
        sj_env_add_definitions(env, &env->theory);
    sj_env_add_definitions(env, space);
}

/* The `(type:rule head arity (pats...) rhs)` atoms of the signature's
 * definitions and of the space, in kernel spelling, as the kernel's rule
 * list.  Loaded with the definitions, once per revision. */
static void sj_env_load_kernel_rules(SjSetEnv *env, Space *space) {
    Atom *rules = prime_semantics_kernel_rules(&env->arena, space);
    if (!env->theory_ready || space == &env->theory || env->own_rule_count == 0u) {
        env->kernel_rules = rules;
        return;
    }
    Atom *own = prime_semantics_kernel_rules(&env->arena, &env->theory);
    /* Every rule of the signature's own definitions, however many. */
    size_t own_count = 0u;
    for (Atom *r = own; sj_is_expr(r, "LCons", 3u); r = r->expr.elems[2])
        own_count++;
    Atom **items = arena_alloc(&env->arena, sizeof(Atom *) * (own_count ? own_count : 1u));
    if (!items) {
        env->kernel_rules = NULL;
        return;
    }
    size_t count = 0u;
    for (Atom *r = own; sj_is_expr(r, "LCons", 3u); r = r->expr.elems[2])
        items[count++] = r->expr.elems[1];
    Atom *list = rules ? rules : sj_sym(&env->arena, "LNil");
    for (size_t i = count; i > 0u; i--)
        list = sj_expr3(&env->arena, "LCons", items[i - 1u], list);
    env->kernel_rules = list;
}

static void sj_env_load_stored(SjSetEnv *env, Space *space);

static SjSetEnv *sj_env(Space *space) {
    SjSetEnv *env = &g_set_env;
    if (!sj_env_parse(env)) return NULL;
    if (!sj_env_check_definitions(env)) return NULL;
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
    /* The rules of the signature's definitions compute in the extended
     * space's own judgments too. */
    if (env->theory_ready && space != &env->theory)
        for (size_t i = 0u; i < env->own_rule_count; i++)
            space_add(&env->overlay, env->own_rules[i]);
    env->overlay_ready = true;
    env->base_instance = instance;
    env->base_revision = revision;
    env->constant_count = 0u;
    sj_env_load_definitions(env, space);
    sj_env_load_kernel_rules(env, space);
    sj_env_load_stored(env, space);
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
        base = sj_is_sort(declared[i]);
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
    if (!name || name->kind != ATOM_SYMBOL ||
        prime_scoped_judgment_reserved_name(name))
        return NULL;
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
    /* The signature's definitions are known in every space, as its seeds. */
    for (size_t i = 0u; env->theory_ready && i < env->own_record_count; i++) {
        Atom *own = env->own_records[i];
        if (!atom_eq(own->expr.elems[SJ_KNOWN_NAME], name)) continue;
        if (found && !atom_eq(found->expr.elems[SJ_KNOWN_PROP],
                              own->expr.elems[SJ_KNOWN_PROP])) {
            if (ambiguous_out) *ambiguous_out = true;
            return NULL;
        }
        if (!found) found = own;
    }
    return found;
}

/* ------------------------------------------------------------------------ */
/* Descent: the constant-family (simple) fragment, decided on the authored   */
/* operands without a second elaboration.  Types are base types, sorts and    */
/* binder-free arrows; terms use no dependent former, and a polymorphic head */
/* occurs only applied to a closed simple type.                              */
/* ------------------------------------------------------------------------ */

/* A sort: a universe `(u l)`, a sort above them by name (`set`, `class`,
 * `(class n)`), the kernel's `(Sort level)`, or a legacy spelling. */
static bool sj_is_sort(Atom *t) {
    return atom_is_symbol(t, "U0") || atom_is_symbol(t, "U1") ||
           atom_is_symbol(t, "u0") || atom_is_symbol(t, "u1") ||
           sj_is_expr(t, "Sort", 2u) || sj_is_expr(t, "u", 2u) ||
           cetta_prime_regular_kernel_sort_above_spelling_v1(NULL, t, NULL);
}

static bool sj_env_declares_type_former(SjSetEnv *env, Atom *head, size_t argc);

static bool sj_authored_simple_type(SjSetEnv *env, Atom *t) {
    if (!t) return false;
    if (sj_is_sort(t)) return true;
    if (t->kind == ATOM_SYMBOL) return sj_env_is_base_type(env, t);
    /* A declared type former applied to simple types, `(Stream num)`, is a
     * type of the fragment like a base type: every use is at closed
     * arguments. */
    if (t->kind == ATOM_EXPR && t->expr.len >= 2u &&
        sj_env_declares_type_former(env, t->expr.elems[0], (size_t)t->expr.len - 1u)) {
        for (CettaExprIndex i = 1u; i < t->expr.len; i++)
            if (!sj_authored_simple_type(env, t->expr.elems[i])) return false;
        return true;
    }
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

static size_t sj_type_arity(Atom *type);

/* Whether the extended space declares `head` at a type taking at least
 * `argc` arguments: an application of it is then of that name, even where
 * the name spells a word of the term syntax. */
static bool sj_env_declares_applied(SjSetEnv *env, Atom *head, size_t argc) {
    if (!env->overlay_ready || !head || head->kind != ATOM_SYMBOL) return false;
    Atom **declared = NULL;
    SpaceDeclaredTypeLookupCost cost = {0};
    uint32_t count = space_get_declared_types_costed(
        &env->overlay, &env->arena, head, &declared, &cost);
    bool fits = false;
    for (uint32_t i = 0u; i < count && !fits; i++)
        fits = argc <= sj_type_arity(declared[i]);
    free(declared);
    return fits;
}

/* Whether the extended space declares `head` a type former of `argc`
 * arguments: at a type `(-> A1 ... An S)` of plain domains with a sort `S`. */
static bool sj_env_declares_type_former(SjSetEnv *env, Atom *head, size_t argc) {
    if (!env->overlay_ready || !head || head->kind != ATOM_SYMBOL || argc == 0u)
        return false;
    Atom **declared = NULL;
    SpaceDeclaredTypeLookupCost cost = {0};
    uint32_t count = space_get_declared_types_costed(
        &env->overlay, &env->arena, head, &declared, &cost);
    bool former = false;
    for (uint32_t i = 0u; i < count && !former; i++) {
        Atom *type = declared[i];
        if (!type || type->kind != ATOM_EXPR || (size_t)type->expr.len != argc + 2u ||
            !atom_is_symbol(type->expr.elems[0], "->") ||
            !sj_is_sort(type->expr.elems[type->expr.len - 1u]))
            continue;
        former = true;
        for (CettaExprIndex k = 1u; former && k + 1u < type->expr.len; k++) {
            Atom *domain = type->expr.elems[k];
            former = !(domain && domain->kind == ATOM_EXPR && domain->expr.len == 3u &&
                       (atom_is_symbol(domain->expr.elems[0], ":") ||
                        atom_is_symbol(domain->expr.elems[1], ":")));
        }
    }
    free(declared);
    return former;
}

static bool sj_authored_simple_term(SjSetEnv *env, Atom *t) {
    if (!t) return false;
    if (t->kind == ATOM_SYMBOL) return !sj_env_is_poly_head(env, t);
    if (t->kind != ATOM_EXPR || t->expr.len == 0u) return true;
    Atom *head = t->expr.elems[0];
    bool declared_head =
        sj_env_declares_applied(env, head, (size_t)t->expr.len - 1u);
    if (!declared_head && atom_is_symbol(head, "->"))
        return sj_authored_simple_type(env, t);
    if (!declared_head && sj_is_expr(t, "lam", 3u)) {
        /* The binder is a name, or a name with a written domain, `(x : A)` or
         * `(: x A)`, which the kernel checks against the quantifier's
         * carrier (`ATyped.lamTyped_inv`). */
        Atom *binder = t->expr.elems[1];
        bool named = binder && binder->kind == ATOM_SYMBOL;
        bool group = binder && binder->kind == ATOM_EXPR && binder->expr.len == 3u;
        bool written = group &&
            ((atom_is_symbol(binder->expr.elems[1], ":") &&
              binder->expr.elems[0]->kind == ATOM_SYMBOL) ||
             (atom_is_symbol(binder->expr.elems[0], ":") &&
              binder->expr.elems[1]->kind == ATOM_SYMBOL)) &&
            sj_authored_simple_type(env, binder->expr.elems[2]);
        return (named || written) && sj_authored_simple_term(env, t->expr.elems[2]);
    }
    if (!declared_head &&
        (sj_is_expr(t, ":", 3u) || atom_is_symbol(head, "sigma") ||
         atom_is_symbol(head, "pair") || atom_is_symbol(head, "fst") ||
         atom_is_symbol(head, "snd") || atom_is_symbol(head, "id") ||
         atom_is_symbol(head, "refl") || atom_is_symbol(head, "id:eliminate")))
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

/* A constant in canonical spelling: a bare name, or `(DeclConst c)` for a
 * name that also spells a universe. */
static Atom *sj_constant_ref(Atom *t) {
    if (sj_is_constant(t)) return t;
    if (sj_is_expr(t, "DeclConst", 2u) && t->expr.elems[1] &&
        t->expr.elems[1]->kind == ATOM_SYMBOL)
        return t->expr.elems[1];
    return NULL;
}

/* A constant written into a canonical term: bare, or `(DeclConst c)` when
 * its name also spells a universe written bare. */
static Atom *sj_canonical_constant(Arena *a, Atom *name) {
    return sj_is_sort(name) ? sj_expr2(a, "DeclConst", name) : name;
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
    if (sj_is_sort(t) || sj_constant_ref(t)) return true;
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
    Atom *value = cetta_prime_regular_term_quote_named_v1(a, type);
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
           sj_is_sort(t) ||
           (t->expr.len >= 2u && atom_is_symbol(t->expr.elems[0], "DeclConst"));
}

/* The most binders (lambdas, dependent functions and pairs) nested on one
 * path of `t`. */
static size_t sj_binder_depth(Atom *t) {
    if (!t || t->kind != ATOM_EXPR || t->expr.len == 0u) return 0u;
    bool binder = atom_is_symbol(t->expr.elems[0], "Lam") ||
                  atom_is_symbol(t->expr.elems[0], "Pi") ||
                  atom_is_symbol(t->expr.elems[0], "Sigma") ||
                  atom_is_symbol(t->expr.elems[0], "lam");
    size_t deepest = 0u;
    for (CettaExprIndex i = 1u; i < t->expr.len; i++) {
        size_t child = sj_binder_depth(t->expr.elems[i]);
        if (child > deepest) deepest = child;
    }
    return deepest + (binder ? 1u : 0u);
}

/* Storage for the local binder types met while annotating `first` and
 * `second`, after `preset` entries already in scope.  Zeroed; NULL when
 * allocation fails. */
static Atom **sj_locals_for(Arena *a, size_t preset, Atom *first, Atom *second) {
    size_t depth = sj_binder_depth(first);
    size_t other = sj_binder_depth(second);
    if (other > depth) depth = other;
    size_t cap = preset + depth + 1u;
    Atom **locals = arena_alloc(a, sizeof(Atom *) * cap);
    if (locals) memset(locals, 0, sizeof(Atom *) * cap);
    return locals;
}

/* Storage for the rebuilt children of `t`, one slot per child, however many
 * it has.  NULL when allocation fails. */
static Atom **sj_children(Arena *a, Atom *t) {
    size_t len = t && t->kind == ATOM_EXPR ? (size_t)t->expr.len : 0u;
    Atom **children = arena_alloc(a, sizeof(Atom *) * (len ? len : 1u));
    if (children) memset(children, 0, sizeof(Atom *) * (len ? len : 1u));
    return children;
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
    Atom **children = sj_children(a, t);
    if (!children) return NULL;
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
    Atom **children = sj_children(a, t);
    if (!children) return NULL;
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
    if (!head || head->kind != ATOM_SYMBOL || !g_set_env.overlay_ready ||
        prime_scoped_judgment_reserved_name(head))
        return NULL;
    for (size_t i = 0u; i < g_set_env.def_count; i++)
        if (atom_eq(g_set_env.defs[i].name, head)) return &g_set_env.defs[i];
    return NULL;
}

/* First-order matching of a compiled pattern against an argument; a
 * constructor pattern looks at the argument's weak head normal form. */
static bool sj_binds_body(Atom *t, CettaExprIndex i);

/* Whether `t` has no variable bound outside it and no metavariable. */
static bool sj_term_closed(Atom *t, uint64_t depth) {
    if (!t || t->kind != ATOM_EXPR) return true;
    uint64_t index = 0u;
    if (sj_intrinsic_index(t, &index)) return index < depth;
    if (sj_is_expr(t, "pf:meta", 2u) || sj_is_expr(t, "pf:var", 2u)) return false;
    for (CettaExprIndex i = 1u; i < t->expr.len; i++)
        if (!sj_term_closed(t->expr.elems[i], sj_binds_body(t, i) ? depth + 1u : depth))
            return false;
    return true;
}

static bool sj_match_rule(Arena *a, Atom *pat, Atom *arg, Atom **sols,
                          size_t nvars, unsigned *fuel) {
    /* A guarded position of a rule admitted on a set solution matches only
     * a closed argument. */
    if (sj_is_expr(pat, "PGuard", 2u))
        return sj_term_closed(arg, 0u) &&
               sj_match_rule(a, pat->expr.elems[1], arg, sols, nvars, fuel);
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
    Atom **children = sj_children(a, t);
    if (!children) return NULL;
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
    size_t argc = sj_spine(t, &head, NULL, 0u);
    SjDefinition *def = sj_definition_of(head);
    if (!def || argc < def->arity) return t;
    /* The spine and each rule's solutions are held at their own sizes. */
    Atom **args = arena_alloc(a, sizeof(Atom *) * (argc ? argc : 1u));
    if (!args) return NULL;
    sj_spine(t, &head, args, argc);
    for (size_t r = 0u; r < def->rule_count; r++) {
        SjRule *rule = &def->rules[r];
        Atom **sols = arena_alloc(a, sizeof(Atom *) * (rule->nvars ? rule->nvars : 1u));
        if (!sols) return NULL;
        memset(sols, 0, sizeof(Atom *) * (rule->nvars ? rule->nvars : 1u));
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
 * head.  The signature's own definitions are among them, loaded first. */
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
    Atom **children = sj_children(a, head);
    if (!children) return NULL;
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

/* The tower universe `(u level)` in kernel spelling.  At a numeral n that is
 * a machine integer it is `(Sort (LevelConst n))`.  Any other closed level,
 * such as a longer numeral, `(u omega)` or `(u (+ 1 2))`, is read as the
 * authored term syntax reads it, where a natural number of any length is
 * again spelled `(Sort (LevelConst n))`.  A level that does not read is kept
 * as written, and the kernel declines it. */
static Atom *sj_universe_to_kernel(Arena *a, Atom *universe) {
    Atom *level = universe->expr.elems[1];
    if (level && !(level->kind == ATOM_GROUNDED &&
                   level->ground.gkind == GV_INT)) {
        CettaPrimeRegularKernelBudget budget;
        cetta_prime_regular_kernel_budget_init(&budget, false, 0u);
        CettaPrimeRegularTermElaborationV1 lowered =
            cetta_prime_regular_term_to_pattern_v1(a, universe, &budget);
        if (lowered.status == CETTA_PRIME_REGULAR_TERM_OK) {
            CettaPrimeRegularPatternEnvironmentV1 closed = {0};
            CettaPrimeRegularPatternElaborationV1 elaborated =
                cetta_prime_regular_pattern_elaborate_v1(
                    a, closed, lowered.pattern, &budget);
            if (elaborated.status == CETTA_PRIME_REGULAR_PATTERN_OK)
                return elaborated.term;
        }
    }
    return sj_expr2(a, "Sort", sj_expr2(a, "LevelConst", level));
}

/* A sort above the written universes, by its name, in kernel spelling:
 * `(Sort (LevelAbove n))`.  NULL for anything else. */
static Atom *sj_sort_above_to_kernel(Arena *a, Atom *t) {
    Atom *numeral = NULL;
    return cetta_prime_regular_kernel_sort_above_spelling_v1(a, t, &numeral)
        ? cetta_prime_regular_kernel_sort_above_term_v1(a, numeral) : NULL;
}

/* Kernel spelling: bare constants become `(DeclConst c)`, the tower universe
 * `(u n)` becomes `(Sort (LevelConst n))`, and `set` and `class` the sorts
 * `(Sort (LevelAbove 0))` and `(Sort (LevelAbove 1))`; indices, `App`,
 * `Lam`, `Pi` are shared. */
static Atom *sj_to_kernel(Arena *a, Atom *t) {
    if (!t) return NULL;
    if (sj_is_constant(t)) return sj_expr2(a, "DeclConst", t);
    if (sj_is_expr(t, "u", 2u)) return sj_universe_to_kernel(a, t);
    Atom *above = sj_sort_above_to_kernel(a, t);
    if (above) return above;
    if (sj_is_leaf(t)) return t;
    Atom **children = sj_children(a, t);
    if (!children) return NULL;
    for (CettaExprIndex i = 1u; i < t->expr.len; i++) {
        children[i] = sj_to_kernel(a, t->expr.elems[i]);
        if (!children[i]) return NULL;
    }
    return sj_rebuild(a, t, children);
}

/* ------------------------------------------------------------------------ */
/* Stored atoms declared typed.  `(type:stored (F T1 ... Tn))` in a space     */
/* says that every stored atom `(F x1 ... xn)` is an occurrence, with xi a    */
/* term of Ti.  The occurrences are the type `F@occurrence`; an occurrence    */
/* is named by its content, `F@x1@...@xn`, and the k-th stored copy of one   */
/* atom, for k from 2, by `F@x1@...@xn#k`, so that removing an atom renames   */
/* no other atom, only a later copy of the same one; and the i-th argument    */
/* of an occurrence is `F@argi`, with one equation for each stored atom.      */
/* These declarations and equations are part of the space for the `type:`    */
/* and `set:` judgments on it.                                               */
/* ------------------------------------------------------------------------ */

static Atom *sj_stored_name(Arena *a, Atom *functor, Atom *atom, size_t copy) {
    size_t bytes = strlen(atom_name_cstr(functor)) + 24u;
    for (CettaExprIndex i = 1u; i < atom->expr.len; i++) {
        char *shown = atom_to_string(a, atom->expr.elems[i]);
        bytes += strlen(shown ? shown : "") + 1u;
    }
    char *name = arena_alloc(a, bytes);
    size_t at = (size_t)snprintf(name, bytes, "%s", atom_name_cstr(functor));
    for (CettaExprIndex i = 1u; i < atom->expr.len; i++) {
        char *shown = atom_to_string(a, atom->expr.elems[i]);
        at += (size_t)snprintf(name + at, bytes - at, "@%s", shown ? shown : "");
    }
    if (copy > 1u)
        snprintf(name + at, bytes - at, "#%zu", copy);
    return sj_sym(a, name);
}

static Atom *sj_stored_map_name(Arena *a, Atom *functor, const char *suffix) {
    size_t bytes = strlen(atom_name_cstr(functor)) + strlen(suffix) + 2u;
    char *name = arena_alloc(a, bytes);
    snprintf(name, bytes, "%s@%s", atom_name_cstr(functor), suffix);
    return sj_sym(a, name);
}

static Atom *sj_stored_argument_name(Arena *a, Atom *functor, size_t position) {
    char suffix[32];
    snprintf(suffix, sizeof suffix, "arg%zu", position);
    return sj_stored_map_name(a, functor, suffix);
}

static void sj_stored_add(SjSetEnv *env, Atom *atom, size_t *cap) {
    if (env->stored_atom_count == *cap) {
        size_t next = *cap ? 2u * *cap : 16u;
        Atom **grown = arena_alloc(&env->arena, sizeof(Atom *) * next);
        for (size_t i = 0u; i < env->stored_atom_count; i++)
            grown[i] = env->stored_atoms[i];
        env->stored_atoms = grown;
        *cap = next;
    }
    env->stored_atoms[env->stored_atom_count++] = atom;
}

/* Read the typed functors of a space.  Each stored argument is checked at
 * its declared type through the extended space, as any term is; the
 * declarations and equations of a functor whose arguments all check are
 * added to the extended space, and its equations to the kernel's rules.  A
 * rule computes only where admission published it: these are published
 * with the space's own reading. */
static void sj_env_load_stored(SjSetEnv *env, Space *space) {
    Arena *a = &env->arena;
    env->stored = NULL;
    env->stored_count = 0u;
    env->stored_atoms = NULL;
    env->stored_atom_count = 0u;
    Atom *pattern = sj_expr2(a, "type:stored", atom_var(a, "declared"));
    CettaIndex *candidates = NULL;
    CettaIndex count = space_match_candidates64(space, pattern, &candidates);
    if (count == 0u) {
        free(candidates);
        return;
    }
    env->stored = arena_alloc(a, sizeof(SjStoredFunctor) * (size_t)count);
    size_t atom_cap = 0u;
    CettaCount length = space_length64(space);
    for (CettaIndex c = 0u; c < count; c++) {
        Atom *declaration = space_match_candidate_at64(space, candidates[c]);
        Atom *declared = sj_is_expr(declaration, "type:stored", 2u)
            ? declaration->expr.elems[1] : NULL;
        if (!declared || declared->kind != ATOM_EXPR ||
            declared->expr.len < 2u ||
            declared->expr.elems[0]->kind != ATOM_SYMBOL ||
            atom_is_symbol(declared->expr.elems[0], "="))
            continue;
        SjStoredFunctor *functor = &env->stored[env->stored_count++];
        *functor = (SjStoredFunctor){
            .functor = declared->expr.elems[0],
            .declared = declared,
            .arity = (size_t)declared->expr.len - 1u,
        };
        functor->atoms = arena_alloc(a, sizeof(Atom *) * (length ? length : 1u));
        functor->names = arena_alloc(a, sizeof(Atom *) * (length ? length : 1u));
        functor->arguments = arena_alloc(a, sizeof(Atom **) * (length ? length : 1u));
        for (CettaIndex index = 0u; index < length && !functor->failure; index++) {
            Atom *atom = space_get_at64(space, index);
            if (!atom || atom->kind != ATOM_EXPR ||
                atom->expr.len != declared->expr.len ||
                !atom_eq(atom->expr.elems[0], functor->functor))
                continue;
            size_t copy = 1u;
            for (size_t prior = 0u; prior < functor->count; prior++)
                if (atom_eq(functor->atoms[prior], atom)) copy++;
            Atom **row = arena_alloc(a, sizeof(Atom *) * (functor->arity + 1u));
            for (size_t position = 1u; position <= functor->arity; position++) {
                Atom *canonical = NULL;
                CettaNikOutcomeV1 outcome = CETTA_NIK_OUTCOME_INCOMPLETE;
                row[position] = sj_canonical_checked(
                        a, &env->overlay, atom->expr.elems[position],
                        declared->expr.elems[position], &canonical, &outcome)
                    ? sj_to_kernel(a, canonical) : NULL;
                if (!row[position]) {
                    functor->failure = sj_expr2(
                        a, outcome == CETTA_NIK_OUTCOME_REFUTED
                               ? "typed-query-ill-typed-occurrence"
                               : "typed-query-occurrence",
                        atom);
                    break;
                }
            }
            functor->atoms[functor->count] = atom;
            functor->names[functor->count] =
                sj_stored_name(a, functor->functor, atom, copy);
            functor->arguments[functor->count++] = row;
        }
        if (functor->failure) continue;
        Atom *occurrence = sj_stored_map_name(a, functor->functor, "occurrence");
        sj_stored_add(env, sj_expr3(a, ":", occurrence,
                                    sj_expr2(a, "u", atom_int(a, 0))),
                      &atom_cap);
        for (size_t i = 0u; i < functor->count; i++)
            sj_stored_add(env, sj_expr3(a, ":", functor->names[i], occurrence),
                          &atom_cap);
        for (size_t position = 1u; position <= functor->arity; position++) {
            Atom *map = sj_stored_argument_name(a, functor->functor, position);
            sj_stored_add(env, sj_expr3(a, ":", map,
                                        sj_expr3(a, "->", occurrence,
                                                 declared->expr.elems[position])),
                          &atom_cap);
            for (size_t i = 0u; i < functor->count; i++) {
                Atom *patterns = atom_expr(
                    a, (Atom *[]){sj_expr2(a, "DeclConst", functor->names[i])}, 1u);
                Atom *items[5] = {sj_sym(a, "type:rule"), map, atom_int(a, 1),
                                  patterns, functor->arguments[i][position]};
                Atom *rule = atom_expr(a, items, 5u);
                sj_stored_add(env, rule, &atom_cap);
                char digest[65];
                sj_record_digest(a, rule, digest);
                sj_admitted_insert(space_instance_id(space), digest);
                Atom *kernel_rule = atom_expr(
                    a, (Atom *[]){sj_sym(a, "PrimeRule"), map, atom_int(a, 1),
                                  patterns, functor->arguments[i][position]}, 5u);
                env->kernel_rules = sj_expr3(
                    a, "LCons", kernel_rule,
                    env->kernel_rules ? env->kernel_rules : sj_sym(a, "LNil"));
            }
        }
    }
    free(candidates);
    for (size_t i = 0u; i < env->stored_atom_count; i++)
        space_add(&env->overlay, env->stored_atoms[i]);
}

static SjStoredFunctor *sj_env_stored_functor(SjSetEnv *env, Atom *functor,
                                              size_t arity) {
    for (size_t i = 0u; i < env->stored_count; i++)
        if (atom_eq(env->stored[i].functor, functor) &&
            env->stored[i].arity == arity)
            return &env->stored[i];
    return NULL;
}

/* The space for an ordinary `type:` judgment: the space itself, and, where
 * it declares stored atoms typed, the space extended with the declarations
 * and equations they give.  The extended spaces the set language builds
 * already have them. */
static Space g_stored_space;
static bool g_stored_space_ready;
static uint64_t g_stored_space_instance;
static uint64_t g_stored_space_revision;

/* Whether a space is one the set language or this extension built, or a
 * view over one: it is never extended again, and the environment it was
 * built from is never rebuilt for it. */
static bool sj_space_is_derived(const Space *space) {
    for (const Space *s = space; s; s = s->overlay_base)
        if (s == &g_set_env.overlay || s == &g_set_env.theory ||
            s == &g_stored_space)
            return true;
    return false;
}

/* Whether a space declares stored atoms typed.  An indexed lookup may
 * propose candidates that are not such declarations. */
static bool sj_space_declares_stored(Arena *a, Space *space) {
    Atom *pattern = sj_expr2(a, "type:stored", atom_var(a, "declared"));
    CettaIndex *candidates = NULL;
    CettaIndex count = space_match_candidates64(space, pattern, &candidates);
    bool found = false;
    for (CettaIndex i = 0u; i < count && !found; i++)
        found = sj_is_expr(space_match_candidate_at64(space, candidates[i]),
                           "type:stored", 2u);
    free(candidates);
    return found;
}

Space *prime_scoped_stored_space(Arena *a, Space *space) {
    if (!a || !space || sj_space_is_derived(space) ||
        !sj_space_declares_stored(a, space))
        return space;
    uint64_t instance = space_instance_id(space);
    uint64_t revision = space_revision(space);
    if (g_stored_space_ready && g_stored_space_instance == instance &&
        g_stored_space_revision == revision)
        return &g_stored_space;
    SjSetEnv *env = sj_env(space);
    if (!env) return space;
    if (g_stored_space_ready) space_free(&g_stored_space);
    space_init_overlay(&g_stored_space, space);
    for (size_t i = 0u; i < env->stored_atom_count; i++)
        space_add(&g_stored_space, env->stored_atoms[i]);
    g_stored_space_ready = true;
    g_stored_space_instance = instance;
    g_stored_space_revision = revision;
    return &g_stored_space;
}

/* The declared-name readout and the intrinsic kernel wire share binders and
 * indices. Only a monomorphic global occurrence changes spelling here. */
static Atom *sj_from_kernel(Arena *a, Atom *term) {
    /* A constant is written bare, unless its name also spells one of the
     * universes written bare (`U0`, `U1`, `set`, `class`): it then keeps its
     * declaration form, so that a theory's constant `U0` is never read as a
     * universe.  A sort above the written universes is written by name. */
    if (sj_is_expr(term, "DeclConst", 2u))
        return sj_is_sort(term->expr.elems[1]) ? term : term->expr.elems[1];
    if (sj_is_expr(term, "Sort", 2u)) {
        Atom *named = cetta_prime_regular_kernel_quote_sort_above_v1(a, term);
        if (named) return named;
    }
    if (sj_is_leaf(term)) return term;
    Atom **children = sj_children(a, term);
    if (!children) return NULL;
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
    Atom **open_names;      /* binders of authored lambdas inside an open witness */
    Atom **open_types;
    size_t open_count;
    size_t open_cap;
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
    Atom **signature_uses;  /* the signature's defined constants mentioned */
    size_t signature_use_count;
    size_t signature_use_cap;
    size_t equation_variable_cap; /* storage of the equation being checked */
    Atom *reachable_rules;  /* ordered rules at dependency-reachable heads */
    CettaPrimeRegularKernelBudget budget;
    unsigned fuel;
    size_t kernel_decisions;
    size_t structural_decisions;
    Atom *failure;          /* first failure reason */
    bool refuted;           /* failure is a checked refutation */
    bool incomplete;        /* failure is budget exhaustion */
} SjProofState;

/* Room for one more item in one or two parallel arena arrays of `count`
 * items and capacity `*cap`; false when allocation fails. */
static bool sj_grow_pair(Arena *a, Atom ***first, Atom ***second,
                         size_t count, size_t *cap) {
    if (count < *cap) return true;
    size_t next = *cap ? *cap * 2u : 8u;
    Atom **grown = arena_alloc(a, sizeof(Atom *) * next);
    Atom **grown2 = second ? arena_alloc(a, sizeof(Atom *) * next) : NULL;
    if (!grown || (second && !grown2)) return false;
    for (size_t i = 0u; i < count; i++) {
        grown[i] = (*first)[i];
        if (second) grown2[i] = (*second)[i];
    }
    *first = grown;
    if (second) *second = grown2;
    *cap = next;
    return true;
}

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

static void sj_note_definition_uses(SjProofState *st, Atom *t);

/* Canonical type of a signature constant, from one synthesis observation,
 * cached in the environment for the life of its overlay. */
static void sj_constant_cache_add(SjSetEnv *env, Atom *name, Atom *type);

static Atom *sj_constant_type(SjProofState *st, Atom *name) {
    if (prime_scoped_judgment_reserved_name(name)) return NULL;
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

/* Kernel spelling with instances specialized: `(all A F)` becomes an application
 * of the simple constant `all@A` of type `(A → prop) → prop`, and `(eq A x y)`
 * of `eq@A` of type `A → A → prop`.  The higher-order term stays the source;
 * the specialization exists so that the kernel can own the conversion. */
static Atom *sj_to_kernel_specialized(SjProofState *st, Atom *t) {
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
            Atom *arg = sj_to_kernel_specialized(st, args[i]);
            if (!arg) return NULL;
            term = sj_expr3(a, "App", term, arg);
        }
        return term;
    }
    if (sj_is_constant(t)) return sj_expr2(a, "DeclConst", t);
    if (sj_is_expr(t, "u", 2u)) return sj_universe_to_kernel(a, t);
    Atom *above = sj_sort_above_to_kernel(a, t);
    if (above) return above;
    if (sj_is_leaf(t)) return t;
    Atom **children = sj_children(a, t);
    if (!children) return NULL;
    for (CettaExprIndex i = 1u; i < t->expr.len; i++) {
        children[i] = sj_to_kernel_specialized(st, t->expr.elems[i]);
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

/* A defined constant of the signature is always declared, like every
 * signature constant; its rules join a request as a program's definitions
 * do, once the request mentions it. */
static void sj_note_signature_use(SjProofState *st, Atom *name) {
    if (!sj_signature_defines(st->env, name)) return;
    for (size_t i = 0u; i < st->signature_use_count; i++)
        if (atom_eq(st->signature_uses[i], name)) return;
    if (!sj_grow_pair(st->arena, &st->signature_uses, NULL,
                      st->signature_use_count, &st->signature_use_cap)) {
        sj_fail(st, false, sj_expr2(st->arena, "set:signature-use-storage", name));
        return;
    }
    st->signature_uses[st->signature_use_count++] = name;
}

/* A head whose rules a request selects: a mentioned definition of the
 * signature, or a constant the request's context declares. */
static bool sj_rule_head_selected(SjProofState *st, Atom *head) {
    if (sj_signature_defines(st->env, head)) {
        for (size_t i = 0u; i < st->signature_use_count; i++)
            if (atom_eq(st->signature_uses[i], head)) return true;
        return false;
    }
    return sj_context_declares(st, head);
}

static Atom *sj_declare_mentioned(SjProofState *st, Atom *term, Atom *context) {
    Arena *a = st->arena;
    if (!term) return context;
    if (sj_constant_ref(term)) {
        term = sj_constant_ref(term);
        sj_note_signature_use(st, term);
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
    /* A universe mentions no constant: its level, which can have any
     * number of terms, is not walked. */
    if (term->kind != ATOM_EXPR || sj_is_expr(term, "Sort", 2u))
        return context;
    /* The head of a canonical expression is the form's own word (`App`,
     * `Pi`, `Sort`, ...), never a constant, whatever a program's constants
     * are called. */
    for (CettaExprIndex i = term->expr.elems[0]->kind == ATOM_SYMBOL ? 1u : 0u;
         i < term->expr.len; i++)
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
    if (sj_constant_ref(head)) return sj_constant_type(st, sj_constant_ref(head));
    return NULL;
}

/* Recover an annotation hint from the simple context and application
 * spine.  This does not establish typing: the conversion kernel checks
 * the annotated operands.  In particular, a substituted lambda at the
 * head of a beta redex gets its domain from the argument's type. */
static Atom *sj_annotation_type(SjProofState *st, Atom *term, Atom **locals,
                                 size_t local_count) {
    Atom *head = NULL;
    size_t argc = sj_spine(term, &head, NULL, 0u);
    Atom **args = arena_alloc(st->arena, sizeof(Atom *) * (argc ? argc : 1u));
    if (!args) return NULL;
    sj_spine(term, &head, args, argc);
    bool poly = sj_constant_named(head, "all") || sj_constant_named(head, "eq");
    Atom *type = sj_head_type(st, head, poly && argc ? args[0] : NULL,
                              locals, local_count);
    if (!type && sj_is_expr(head, "Lam", 3u)) {
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
    if (sj_is_expr(type, "Pi", 3u) &&
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
        size_t argc = sj_spine(t, &head, NULL, 0u);
        Atom **args = arena_alloc(a, sizeof(Atom *) * (argc ? argc : 1u));
        if (!args) return NULL;
        sj_spine(t, &head, args, argc);
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
    Atom **children = sj_children(a, t);
    if (!children) return NULL;
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

/* The polymorphic head of a stored rule's spine: `all` or `eq`, with or
 * without the universe argument the stored spelling gives it. */
static const char *sj_rule_poly_head(Atom *head) {
    if (!head || head->kind != ATOM_EXPR || head->expr.len < 2u ||
        head->expr.len > 3u || !atom_is_symbol(head->expr.elems[0], "DeclConst"))
        return NULL;
    if (atom_is_symbol(head->expr.elems[1], "all")) return "all";
    if (atom_is_symbol(head->expr.elems[1], "eq")) return "eq";
    return NULL;
}

/* The canonical (declared-route) spelling of a stored rule's subterm at
 * binder depth `depth`, for a rule over `nvars` pattern variables: the
 * inverse of the lowering `sj_rule_term_to_kernel`. */
static Atom *sj_rule_term_from_kernel(Arena *a, Atom *t, size_t nvars,
                                      uint64_t depth) {
    if (!t) return NULL;
    if (sj_is_expr(t, "PVar", 2u)) {
        Atom *n = t->expr.elems[1];
        if (!n || n->kind != ATOM_GROUNDED || n->ground.gkind != GV_INT ||
            n->ground.ival < 0 || (uint64_t)n->ground.ival >= nvars)
            return NULL;
        return sj_expr2(a, "idx", atom_int(a, (int64_t)(depth + nvars - 1u -
                                                       (uint64_t)n->ground.ival)));
    }
    uint64_t index = 0u;
    if (sj_intrinsic_index(t, &index))
        return index < depth ? t
                             : sj_expr2(a, "idx", atom_int(a, (int64_t)(index + nvars)));
    if (t->kind == ATOM_EXPR && t->expr.len >= 2u && t->expr.len <= 3u &&
        atom_is_symbol(t->expr.elems[0], "DeclConst"))
        return t->expr.elems[1];
    if (sj_is_expr(t, "Sort", 2u) && sj_is_expr(t->expr.elems[1], "LevelConst", 2u))
        return sj_expr2(a, "u", t->expr.elems[1]->expr.elems[1]);
    if (sj_is_expr(t, "Sort", 2u) && sj_is_expr(t->expr.elems[1], "LevelCantor", 4u)) {
        Atom *universe = cetta_prime_regular_kernel_quote_closed_universe_sort_v1(a, t);
        if (universe) return universe;
    }
    if (sj_is_expr(t, "Sort", 2u)) {
        Atom *named = cetta_prime_regular_kernel_quote_sort_above_v1(a, t);
        if (named) return named;
    }
    if (sj_is_leaf(t)) return t;
    Atom **children = sj_children(a, t);
    if (!children) return NULL;
    for (CettaExprIndex i = 1u; i < t->expr.len; i++) {
        children[i] = sj_rule_term_from_kernel(a, t->expr.elems[i], nvars,
                                               sj_binds_body(t, i) ? depth + 1u : depth);
        if (!children[i]) return NULL;
    }
    return sj_rebuild(a, t, children);
}

/* A stored rule's subterm as the proof route declares it: each polymorphic
 * head at a domain becomes that domain's simple instance, the same constant
 * an operand at that domain lowers to, noted for the request's context. */
static Atom *sj_rule_term_specialize(SjProofState *st, Atom *t, size_t nvars,
                                     uint64_t depth) {
    Arena *a = st->arena;
    if (!t || t->kind != ATOM_EXPR || sj_is_leaf(t)) return t;
    Atom *head = NULL;
    Atom *args[4] = {0};
    size_t argc = sj_spine(t, &head, args, 4u);
    const char *poly = sj_rule_poly_head(head);
    if (poly && argc >= 1u && argc <= 4u) {
        Atom *domain = sj_rule_term_from_kernel(a, args[0], nvars, depth);
        if (!domain) return NULL;
        Atom *type = NULL;
        Atom *term = sj_expr2(a, "DeclConst",
                              sj_instance_constant(st, poly, domain, &type));
        for (size_t i = 1u; i < argc; i++) {
            Atom *arg = sj_rule_term_specialize(st, args[i], nvars, depth);
            if (!arg) return NULL;
            term = sj_expr3(a, "App", term, arg);
        }
        return term;
    }
    Atom **children = sj_children(a, t);
    if (!children) return NULL;
    for (CettaExprIndex i = 1u; i < t->expr.len; i++) {
        children[i] = sj_rule_term_specialize(st, t->expr.elems[i], nvars,
                                              sj_binds_body(t, i) ? depth + 1u : depth);
        if (!children[i]) return NULL;
    }
    return sj_rebuild(a, t, children);
}

/* The pattern variables of a stored rule: its patterns bind each once. */
static size_t sj_rule_pattern_variables(Atom *t) {
    if (!t || t->kind != ATOM_EXPR) return 0u;
    if (sj_is_expr(t, "PVar", 2u)) {
        Atom *n = t->expr.elems[1];
        return n && n->kind == ATOM_GROUNDED && n->ground.gkind == GV_INT &&
               n->ground.ival >= 0 ? (size_t)n->ground.ival + 1u : 0u;
    }
    size_t most = 0u;
    for (CettaExprIndex i = 0u; i < t->expr.len; i++) {
        size_t inner = sj_rule_pattern_variables(t->expr.elems[i]);
        if (inner > most) most = inner;
    }
    return most;
}

/* A selected `(PrimeRule head arity (pats...) rhs)` as the proof route's
 * kernel reads it. */
static Atom *sj_rule_specialize(SjProofState *st, Atom *rule) {
    Arena *a = st->arena;
    Atom *pats = rule->expr.elems[3];
    size_t nvars = sj_rule_pattern_variables(pats);
    Atom *spats = pats;
    if (pats && pats->kind == ATOM_EXPR && pats->expr.len > 0u) {
        /* One pattern per argument, each specialized on its own. */
        Atom **items = arena_alloc(a, sizeof(Atom *) * (size_t)pats->expr.len);
        for (CettaExprIndex k = 0u; k < pats->expr.len; k++) {
            items[k] = sj_rule_term_specialize(st, pats->expr.elems[k], nvars, 0u);
            if (!items[k]) return NULL;
        }
        spats = atom_expr(a, items, pats->expr.len);
    }
    Atom *srhs = sj_rule_term_specialize(st, rule->expr.elems[4], nvars, 0u);
    if (!srhs) return NULL;
    Atom *items[5] = {rule->expr.elems[0], rule->expr.elems[1],
                      rule->expr.elems[2], spats, srhs};
    return atom_expr(a, items, 5u);
}

/* Close the request's declarations under types, rule patterns and right-hand
 * sides. Definitions can introduce specialized quantifiers whose types must
 * come from the specialized source, not from parsing a generated constant name.
 * Only selected heads contribute edges. Cycles stop when no new declaration
 * is discovered; the source rule order and all alternatives at a head remain
 * unchanged. This is syntactic support, not a minimal dynamic read trace.
 * Stored rules keep the source's polymorphic heads; each selected rule is
 * specialized here, where the proof route reads it. */
static bool sj_declare_rule_dependencies(SjProofState *st, Atom *context) {
    size_t previous, previous_uses;
    do {
        previous = st->instance_count;
        previous_uses = st->signature_use_count;
        for (size_t d = 0u; d < st->env->def_count; d++) {
            SjDefinition *def = &st->env->defs[d];
            if (!sj_rule_head_selected(st, def->name)) continue;
            for (size_t r = 0u; r < def->rule_count; r++) {
                Atom *rhs = sj_to_kernel_specialized(st, def->rules[r].rhs);
                if (!rhs) return false;
                sj_declare_kernel_mentioned(st, rhs, context);
                for (size_t k = 0u; k < def->arity; k++) {
                    Atom *pat = sj_to_kernel_specialized(st, def->rules[r].pats[k]);
                    if (!pat) return false;
                    sj_declare_kernel_mentioned(st, pat, context);
                }
            }
        }
        for (Atom *r = st->env->kernel_rules; sj_is_expr(r, "LCons", 3u);
             r = r->expr.elems[2]) {
            Atom *rule = r->expr.elems[1];
            if (!sj_is_expr(rule, "PrimeRule", 5u) ||
                !sj_rule_head_selected(st, rule->expr.elems[1])) continue;
            Atom *specialized = sj_rule_specialize(st, rule);
            if (!specialized) return false;
            sj_declare_kernel_mentioned(st, specialized->expr.elems[3], context);
            sj_declare_kernel_mentioned(st, specialized->expr.elems[4], context);
        }
    } while (st->instance_count != previous ||
             st->signature_use_count != previous_uses);

    Atom **rules = NULL;
    size_t count = 0u, capacity = 0u;
    for (Atom *r = st->env->kernel_rules; sj_is_expr(r, "LCons", 3u);
         r = r->expr.elems[2]) {
        Atom *rule = r->expr.elems[1];
        if (!sj_is_expr(rule, "PrimeRule", 5u) ||
            !sj_rule_head_selected(st, rule->expr.elems[1])) continue;
        Atom *specialized = sj_rule_specialize(st, rule);
        if (!specialized) return false;
        if (count == capacity)
            rules = sj_grow(st->arena, rules, count, &capacity);
        rules[count++] = specialized;
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
        .reason = "conversion-kernel-form-unavailable",
    };
    Atom *context = sj_kernel_context(st);
    if (!context) return outside;
    Atom **locals = sj_locals_for(a, 0u, left, right);
    if (!locals) return outside;
    left = sj_annotate(st, left, locals, 0u);
    right = sj_annotate(st, right, locals, 0u);
    if (!left || !right) return outside;
    sj_declare_mentioned(st, left, context);
    sj_declare_mentioned(st, right, context);
    for (size_t i = 0u; i < st->binder_count; i++)
        sj_declare_mentioned(st, st->binders[i], context);
    Atom *kl = sj_to_kernel_specialized(st, left);
    Atom *kr = sj_to_kernel_specialized(st, right);
    if (!kl || !kr) return outside;
    /* Specializing the operands can add heads; close after specializing. */
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
        Atom *domain = sj_to_kernel_specialized(st, st->binders[i]);
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
    /* A declared type former applied to types, `(Stream num)`. */
    if (t->kind == ATOM_EXPR && t->expr.len >= 2u &&
        sj_env_declares_type_former(st->env, t->expr.elems[0], (size_t)t->expr.len - 1u)) {
        Atom *applied = t->expr.elems[0];
        for (CettaExprIndex i = 1u; i < t->expr.len; i++) {
            Atom *arg = sj_open_domain(st, t->expr.elems[i]);
            if (!arg) return NULL;
            applied = sj_expr3(a, "App", applied, arg);
        }
        return applied;
    }
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
             * domain; the instance is noted so the specialization can lower it. */
            sj_instance_constant(st, poly, domain, &type);
            term = sj_expr3(a, "App", sj_sym(a, poly), domain);
            first = 2u;
        } else {
            Atom *head = w->expr.elems[0];
            if (sj_is_expr(head, "lam", 3u) &&
                head->expr.elems[1]->kind == ATOM_SYMBOL) {
                /* Retain a source beta-redex. The argument supplies the
                 * domain missing from the authored lambda; the ordinary
                 * native checker will check the resulting annotated term. */
                Atom *domain = NULL;
                Atom *argument = sj_open_synth(st, w->expr.elems[1], &domain);
                if (!argument) return NULL;
                if (!sj_grow_pair(a, &st->open_names, &st->open_types,
                                  st->open_count, &st->open_cap)) {
                    sj_fail(st, false, sj_expr2(a, "set:open-binder-storage", w));
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
            if (!sj_is_expr(type, "Pi", 3u)) {
                sj_fail(st, true, sj_expr2(a, "set:witness-not-a-function", w));
                return NULL;
            }
            Atom *arg = sj_open_check(st, w->expr.elems[i], type->expr.elems[1]);
            if (!arg) return NULL;
            term = sj_expr3(a, "App", term, arg);
            /* A dependent function's result is read at its argument: a
             * family's constant takes its set first, `(shead num s)`. */
            type = sj_mentions_index(type->expr.elems[2], 0u)
                ? sj_subst(a, type->expr.elems[2], 0u, arg)
                : sj_shift(a, type->expr.elems[2], 0u, -1);
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
        if (!sj_grow_pair(a, &st->open_names, &st->open_types,
                          st->open_count, &st->open_cap)) {
            sj_fail(st, false, sj_expr2(a, "set:open-binder-storage", w));
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
    /* Universes are cumulative: a type of `(u 0)` is a set. */
    if (!atom_eq(type, domain) &&
        !cetta_prime_regular_kernel_sort_within_v1(a, sj_to_kernel(a, type),
                                                   sj_to_kernel(a, domain))) {
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
    Atom **children = sj_children(a, t);
    if (!children) return NULL;
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
        if (!filled) return false;
        /* A node whose metavariables are all solved solves nothing more: it
         * is compared with the goal's node, as written and then with both
         * sides normalized, never one side normalized against the other
         * as written.  The match stays first-order: a metavariable is solved
         * only where it stands in the pattern. */
        if (!sj_has_unsolved_meta(filled, sol)) {
            if (atom_eq(filled, term)) return true;
            Atom *normal = sj_normalize(a, filled, &st->fuel);
            Atom *target = normal ? sj_normalize(a, term, &st->fuel) : NULL;
            return normal && target &&
                   atom_eq(sj_unannotate(a, normal), sj_unannotate(a, target));
        }
        Atom *normal = sj_normalize(a, filled, &st->fuel);
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
    Atom **children = sj_children(a, t);
    if (!children) return NULL;
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
    Atom **children = sj_children(a, t);
    if (!children) return NULL;
    for (CettaExprIndex i = 1u; i < t->expr.len; i++) {
        children[i] = sj_unannotate(a, t->expr.elems[i]);
        if (!children[i]) return NULL;
    }
    return sj_rebuild(a, t, children);
}

/* The name a spine head stands for: a constant bare or declared. */
static Atom *sj_head_constant(Atom *head) {
    if (sj_is_expr(head, "DeclConst", 2u)) head = head->expr.elems[1];
    return sj_is_constant(head) ? head : NULL;
}

/* A rigid spine head: a bound variable, or a constant with no computation —
 * no admitted definition, no kernel rule, and not the identity eliminator.
 * A spine with a rigid head stays neutral whatever its arguments become, and
 * two neutral spines are convertible only when their heads are the same and
 * their arguments are convertible pairwise. */
static bool sj_rigid_head(SjProofState *st, Atom *head) {
    if (sj_intrinsic_index(head, NULL)) return true;
    Atom *name = sj_head_constant(head);
    if (!name || sj_definition_of(name) ||
        atom_is_symbol(name, "id:eliminate"))
        return false;
    for (Atom *r = st->env->kernel_rules; sj_is_expr(r, "LCons", 3u);
         r = r->expr.elems[2]) {
        Atom *rule = r->expr.elems[1];
        if (!sj_is_expr(rule, "PrimeRule", 5u)) return false;
        Atom *rule_head = sj_head_constant(rule->expr.elems[1]);
        if (!rule_head || atom_eq(rule_head, name)) return false;
    }
    return true;
}

/* Two rigid heads that are certainly different: two bound variables, or two
 * constants of different names, or a variable and a constant. */
static bool sj_rigid_heads_differ(Atom *left, Atom *right) {
    uint64_t i = 0u, j = 0u;
    bool li = sj_intrinsic_index(left, &i);
    bool ri = sj_intrinsic_index(right, &j);
    if (li || ri) return li != ri || i != j;
    Atom *ln = sj_head_constant(left);
    Atom *rn = sj_head_constant(right);
    return ln && rn && !atom_eq(ln, rn);
}

/* A witness that no instance of the pattern `pat` is convertible to `term`,
 * both in normal form.  Below a path of neutral spines with rigid heads and
 * of binders, which conversion takes apart argument by argument, either two
 * neutral spines have different rigid heads, or one metavariable meets two
 * subterms with that witness between them.  `seen` holds, per
 * metavariable, the first subterm it met on such a path.  Anything else —
 * a metavariable applied, a definition or rule waiting on a metavariable, a
 * lambda against a neutral term (eta) — gives no witness. */
static bool sj_rigid_refutes(SjProofState *st, Atom *pat, Atom *term,
                             uint64_t depth, Atom **seen) {
    if (!pat || !term) return false;
    if (sj_is_expr(pat, "pf:meta", 2u)) {
        if (!seen || sj_mentions_index_below(term, depth)) return false;
        Atom *lowered = depth ? sj_shift(st->arena, term, 0u, -(int64_t)depth) : term;
        if (!lowered) return false;
        int64_t i = pat->expr.elems[1]->ground.ival;
        if (!seen[i]) {
            seen[i] = lowered;
            return false;
        }
        return sj_rigid_refutes(st, seen[i], lowered, 0u, NULL);
    }
    Atom *pat_head = NULL, *term_head = NULL;
    size_t pat_argc = sj_spine(pat, &pat_head, NULL, 0u);
    size_t term_argc = sj_spine(term, &term_head, NULL, 0u);
    if (sj_rigid_head(st, pat_head) && sj_rigid_head(st, term_head)) {
        if (sj_rigid_heads_differ(pat_head, term_head)) return true;
        if (pat_argc != term_argc) return false;
        for (size_t k = 0u; k < pat_argc; k++) {
            if (sj_rigid_refutes(st, pat->expr.elems[2], term->expr.elems[2],
                                 depth, seen))
                return true;
            pat = pat->expr.elems[1];
            term = term->expr.elems[1];
        }
        return false;
    }
    if ((sj_is_expr(pat, "Pi", 3u) && sj_is_expr(term, "Pi", 3u)) ||
        (sj_is_expr(pat, "Sigma", 3u) && sj_is_expr(term, "Sigma", 3u)))
        return sj_rigid_refutes(st, pat->expr.elems[1], term->expr.elems[1],
                                depth, seen) ||
               sj_rigid_refutes(st, pat->expr.elems[2], term->expr.elems[2],
                                depth + 1u, seen);
    if (sj_is_expr(pat, "Lam", 2u) && sj_is_expr(term, "Lam", 2u))
        return sj_rigid_refutes(st, pat->expr.elems[1], term->expr.elems[1],
                                depth + 1u, seen);
    return false;
}

/* Mark the metavariables of `pat` that occur on a path of neutral spines
 * with rigid heads and of binders from its root: an instance convertible to
 * a given term fixes each of them up to conversion. */
static void sj_rigid_metas(SjProofState *st, Atom *pat, bool *rigid) {
    if (!pat) return;
    if (sj_is_expr(pat, "pf:meta", 2u)) {
        rigid[pat->expr.elems[1]->ground.ival] = true;
        return;
    }
    Atom *head = NULL;
    size_t argc = sj_spine(pat, &head, NULL, 0u);
    if (sj_rigid_head(st, head)) {
        for (size_t k = 0u; k < argc; k++) {
            sj_rigid_metas(st, pat->expr.elems[2], rigid);
            pat = pat->expr.elems[1];
        }
        return;
    }
    if (sj_is_expr(pat, "Pi", 3u) || sj_is_expr(pat, "Sigma", 3u)) {
        sj_rigid_metas(st, pat->expr.elems[1], rigid);
        sj_rigid_metas(st, pat->expr.elems[2], rigid);
    } else if (sj_is_expr(pat, "Lam", 2u)) {
        sj_rigid_metas(st, pat->expr.elems[1], rigid);
    }
}

/* A proposition that no instantiation unfolds further: its head is rigid, or
 * it is a dependent function type or a universe. */
static bool sj_rigid_proposition(SjProofState *st, Atom *w) {
    Atom *head = NULL;
    sj_spine(w, &head, NULL, 0u);
    return sj_rigid_head(st, head) || sj_is_expr(w, "Pi", 3u) || sj_is_sort(w);
}

static bool sj_mentions_meta(Atom *t, int64_t i) {
    if (!t) return false;
    if (sj_is_expr(t, "pf:meta", 2u)) return t->expr.elems[1]->ground.ival == i;
    if (t->kind != ATOM_EXPR) return false;
    for (CettaExprIndex k = 0u; k < t->expr.len; k++)
        if (sj_mentions_meta(t->expr.elems[k], i)) return true;
    return false;
}

/* A refutation found under an instance the goal does not determine refutes
 * only that instance: the judgment is then undetermined. */
static void sj_by_undetermined_instance(SjProofState *st, Atom *name) {
    if (!st->refuted) return;
    st->refuted = false;
    st->failure = sj_expr3(st->arena, "set:by-instance-not-determined", name,
                           st->failure);
}

typedef enum {
    SJ_BY_PROVED,       /* the instance proves the goal */
    SJ_BY_FAILED,       /* a failure is recorded in the proof state */
    SJ_BY_NEXT          /* the pass without unfolding found nothing: go on */
} SjByOutcome;

/* One attempt of `pf:by`: the fact's prefix is peeled, by its quantifiers up
 * to and between the antecedents its premises discharge, then by at most
 * `limit` further quantifiers, and the conclusion left is matched against
 * the goal.  `*trailing_out` is how many quantifiers after the last
 * antecedent were peeled.  A refutation recorded here is about this depth
 * only: some instance at another depth may still give the goal. */
static SjByOutcome sj_check_by_at(SjProofState *st, Atom *proof, Atom *goal,
                                  Atom *name, Atom *prop0, size_t premise_count,
                                  int pass, size_t limit, size_t *trailing_out) {
    Arena *a = st->arena;
    *trailing_out = 0u;
    {
        Atom *prop = prop0;
        size_t trailing = 0u;
        size_t meta_count = 0u;
        size_t meta_cap = 0u;
        Atom **meta_domains = NULL;
        Atom **antecedents = arena_alloc(
            a, sizeof(Atom *) * (premise_count ? premise_count : 1u));
        if (!antecedents) {
            sj_fail(st, false, sj_expr2(a, "set:by-storage", name));
            return SJ_BY_FAILED;
        }
        size_t antecedent_count = 0u;
        bool ok = true;
        for (;;) {
            g_sj_no_delta = pass == 0;
            Atom *w = sj_whnf(a, prop, &st->fuel);
            g_sj_no_delta = false;
            if (!w) {
                sj_fail_fuel(st, prop);
                return SJ_BY_FAILED;
            }
            Atom *head = NULL;
            Atom *args[4] = {0};
            size_t argc = sj_spine(w, &head, args, 4u);
            Atom *domain = NULL;
            Atom *family_arg = NULL;
            if (sj_quantifier(head, args, argc, &domain, &family_arg)) {
                if (antecedent_count == premise_count) {
                    if (trailing == limit) {
                        prop = w;
                        break;
                    }
                    trailing++;
                }
                if (!sj_grow_pair(a, &meta_domains, NULL, meta_count, &meta_cap)) {
                    sj_fail(st, false, sj_expr2(a, "set:by-storage", name));
                    return SJ_BY_FAILED;
                }
                Atom *meta = sj_expr2(a, "pf:meta", atom_int(a, (int64_t)meta_count));
                meta_domains[meta_count] = domain;
                meta_count++;
                g_sj_no_delta = pass == 0;
                Atom *family = sj_whnf(a, family_arg, &st->fuel);
                g_sj_no_delta = false;
                if (!family) {
                    sj_fail_fuel(st, family_arg);
                    return SJ_BY_FAILED;
                }
                Atom *body = sj_lambda_body(family);
                prop = body ? sj_subst(a, body, 0u, meta) : sj_expr3(a, "App", family, meta);
                if (!prop) {
                    sj_fail(st, false, sj_expr2(a, "set:shift-failed", name));
                    return SJ_BY_FAILED;
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
        *trailing_out = trailing;
        if (antecedent_count < premise_count) {
            if (pass == 0) return SJ_BY_NEXT;
            /* Refuted only when no instantiation can make the rest an
             * implication: its head is rigid.  A metavariable, or a head
             * that still computes, may yet become one. */
            sj_fail(st, sj_rigid_proposition(st, prop),
                    sj_expr2(a, "set:by-needs-implication", prop));
            return SJ_BY_FAILED;
        }
        Atom *raw_conclusion = sj_unannotate(a, prop);
        Atom *raw_target = sj_unannotate(a, goal);
        Atom *conclusion = sj_normalize(a, prop, &st->fuel);
        Atom *target = conclusion ? sj_normalize(a, goal, &st->fuel) : NULL;
        if (!conclusion || !target) {
            sj_fail_fuel(st, conclusion ? goal : prop);
            return SJ_BY_FAILED;
        }
        size_t sol_len = meta_count > SJ_META_MAX ? meta_count : SJ_META_MAX;
        Atom **sol = arena_alloc(a, sizeof(Atom *) * sol_len);
        if (!sol) {
            sj_fail(st, false, sj_expr2(a, "set:by-storage", name));
            return SJ_BY_FAILED;
        }
        memset(sol, 0, sizeof(Atom *) * sol_len);
        conclusion = sj_unannotate(a, conclusion);
        target = sj_unannotate(a, target);
        bool matched = sj_match_meta(st, raw_conclusion, raw_target, sol, 0u);
        if (matched) {
            for (size_t i = 0u; i < meta_count; i++) matched = matched && sol[i];
            if (matched) conclusion = raw_conclusion;
        }
        if (!matched) {
            memset(sol, 0, sizeof(Atom *) * sol_len);
            matched = sj_match_meta(st, conclusion, target, sol, 0u);
        }
        /* With nothing to instantiate, the fact's proposition is compared
         * with the goal by the kernel's conversion below, which also knows
         * eta and the rules this matching does not. */
        if (!matched && meta_count == 0u && pass == 1) matched = true;
        if (!matched) {
            if (pass == 0) return SJ_BY_NEXT;
            /* First-order matching is incomplete: an instance may exist only
             * up to conversion, by a higher-order solution, or by unfolding
             * a definition at an instantiated argument.  Refuted needs a
             * witness that no instance exists. */
            Atom **seen = arena_alloc(a, sizeof(Atom *) * sol_len);
            if (!seen) {
                sj_fail(st, false, sj_expr2(a, "set:by-storage", name));
                return SJ_BY_FAILED;
            }
            memset(seen, 0, sizeof(Atom *) * sol_len);
            if (sj_rigid_refutes(st, conclusion, target, 0u, seen)) {
                sj_fail(st, true, sj_expr3(a, "set:by-no-instance", name, goal));
                return SJ_BY_FAILED;
            }
            /* No witness: the instance the goal's rigid positions fix, when
             * they fix every metavariable of the conclusion, is the only
             * candidate, and the kernel decides it below. */
            for (size_t i = 0u; i < meta_count; i++) {
                if (seen[i] || !sj_mentions_meta(conclusion, (int64_t)i)) continue;
                sj_fail(st, false, sj_expr3(a, "set:by-no-first-order-instance",
                                            name, goal));
                return SJ_BY_FAILED;
            }
            memcpy(sol, seen, sizeof(Atom *) * sol_len);
        }
        /* Whether the goal fixes the instance up to conversion: every
         * metavariable occurs on a rigid path of the matched conclusion, or
         * of an antecedent matched against the type of a named premise.
         * Only then does a failure below refute every instance. */
        bool *rigid = arena_alloc(a, sizeof(bool) * sol_len);
        if (!rigid) {
            sj_fail(st, false, sj_expr2(a, "set:by-storage", name));
            return SJ_BY_FAILED;
        }
        memset(rigid, 0, sizeof(bool) * sol_len);
        sj_rigid_metas(st, conclusion, rigid);
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
            pat = sj_unannotate(a, pat);
            /* A named premise is checked by conversion with its type, so a
             * rigid occurrence in its antecedent is fixed by that type. */
            if (sj_match_meta(st, pat, sj_unannotate(a, tgt), sol, 0u) &&
                proof->expr.elems[2u + k]->kind == ATOM_SYMBOL)
                sj_rigid_metas(st, pat, rigid);
        }
        for (size_t i = 0u; i < meta_count && ok; i++) ok = sol[i] != NULL;
        if (!ok) {
            if (pass == 0) return SJ_BY_NEXT;
            /* The goal and the premises leave a quantifier uninstantiated:
             * whether some instance works is not decided here. */
            sj_fail(st, false, sj_expr2(a, "set:by-unsolved", name));
            return SJ_BY_FAILED;
        }
        bool determined = meta_count > 0u || sj_rigid_proposition(st, conclusion);
        for (size_t i = 0u; i < meta_count && determined; i++)
            determined = rigid[i];
        for (size_t k = 0u; k < premise_count; k++) {
            Atom *antecedent = sj_fill_meta(a, antecedents[k], sol, meta_domains, 0u);
            if (!antecedent || !sj_check(st, proof->expr.elems[2u + k], antecedent)) {
                if (!determined) sj_by_undetermined_instance(st, name);
                return SJ_BY_FAILED;
            }
        }
        Atom *instantiated = sj_fill_meta(a, conclusion, sol, meta_domains, 0u);
        if (!instantiated) return SJ_BY_FAILED;
        if (sj_convertible(st, instantiated, goal)) return SJ_BY_PROVED;
        if (!determined) sj_by_undetermined_instance(st, name);
        return SJ_BY_FAILED;
    }
}

/* `(pf:by fact premise ...)`.  Two passes: first the prefix is peeled
 * without unfolding definitions, so that a conclusion keeps the spelling the
 * fact gives it and matches the goal as written; then with definitions
 * unfolded.  In each pass every depth of peeling the quantifiers after the
 * last antecedent is tried, from all of them to none: a fact whose
 * conclusion is itself universal can give a universal goal.  The fact gives
 * the goal if one attempt does.  A refutation is the verdict only when every
 * attempt of the pass with unfolding is refuted, each by its own witness at
 * its depth; otherwise the first failure of the verdict's kind is kept. */
static bool sj_check_by(SjProofState *st, Atom *proof, Atom *goal) {
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
    Atom *first_refutation = NULL;
    Atom *first_undecided = NULL;
    Atom *first_incomplete = NULL;
    bool unfolded_refuted_everywhere = true;
    for (int pass = 0; pass < 2; pass++) {
        size_t limit = SIZE_MAX;
        for (;;) {
            size_t trailing = 0u;
            SjByOutcome outcome = sj_check_by_at(st, proof, goal, name, prop0,
                                                 premise_count, pass, limit,
                                                 &trailing);
            if (outcome == SJ_BY_PROVED) return true;
            if (outcome == SJ_BY_FAILED) {
                if (st->incomplete) {
                    if (!first_incomplete) first_incomplete = st->failure;
                } else if (st->refuted) {
                    if (!first_refutation) first_refutation = st->failure;
                } else if (!first_undecided) {
                    first_undecided = st->failure;
                }
                if (pass == 1 && (st->incomplete || !st->refuted))
                    unfolded_refuted_everywhere = false;
                st->failure = NULL;
                st->refuted = false;
                st->incomplete = false;
            }
            if (limit == SIZE_MAX) limit = trailing;
            if (limit == 0u) break;
            limit--;
        }
    }
    if (first_incomplete) {
        st->failure = first_incomplete;
        st->incomplete = true;
        st->refuted = false;
    } else if (unfolded_refuted_everywhere && first_refutation) {
        st->failure = first_refutation;
        st->refuted = true;
    } else {
        st->failure = first_undecided ? first_undecided
            : sj_expr3(st->arena, "set:by-instance-not-determined", name,
                       first_refutation ? first_refutation : atom_unit(st->arena));
        st->refuted = false;
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

/* Verify that a published record's recorded dependencies still hold now:
 * every cited name must still be known with the recorded proposition under
 * the current signature, and so must their own dependencies.  Reuse also
 * requires each of them to be admitted here; publication, which has just
 * used them, requires only that they are unchanged.
 *
 * The dependencies form a graph that shares its nodes: a definition built on
 * a stack of others cites each of them, and so does every one of those.  A
 * name is checked once per walk: the walk remembers each name it has found
 * current, with that record's proposition and the height of the walk below
 * it (how many records deep its own dependencies go).  Meeting the name
 * again, it compares the recorded proposition with the remembered one, and
 * walks below it again only when the depth bound would now be exceeded, so
 * the verdict and its reason are those of the walk that rechecks every path.
 * Without this the work grows with the number of paths, which is
 * exponential in the depth of a shared stack of definitions. */
typedef struct {
    Atom **names;
    Atom **props;
    int *heights;   /* levels of the walk below the record; -1 when it is not walked */
    size_t count;
    size_t cap;
} SjDependencyMemo;

static bool sj_dependency_memo_add(Arena *a, SjDependencyMemo *memo, Atom *name, Atom *prop,
                                   int height) {
    if (memo->count == memo->cap) {
        size_t next = memo->cap ? memo->cap * 2u : 16u;
        Atom **names = arena_alloc(a, sizeof(Atom *) * next);
        Atom **props = arena_alloc(a, sizeof(Atom *) * next);
        int *heights = arena_alloc(a, sizeof(int) * next);
        if (!names || !props || !heights) return false;
        for (size_t i = 0u; i < memo->count; i++) {
            names[i] = memo->names[i];
            props[i] = memo->props[i];
            heights[i] = memo->heights[i];
        }
        memo->names = names;
        memo->props = props;
        memo->heights = heights;
        memo->cap = next;
    }
    memo->names[memo->count] = name;
    memo->props[memo->count] = prop;
    memo->heights[memo->count] = height;
    memo->count++;
    return true;
}

static bool sj_dependencies_walk(SjProofState *st, Atom *record, unsigned depth,
                                 bool require_admission, SjDependencyMemo *memo,
                                 int *height_out) {
    Arena *a = st->arena;
    *height_out = 0;
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
        size_t seen = memo->count;
        for (size_t m = 0u; m < memo->count; m++)
            if (atom_eq(memo->names[m], name)) {
                seen = m;
                break;
            }
        if (seen < memo->count) {
            /* Found current before: the name resolves to the same record
             * throughout the walk, so only the proposition this record cited
             * and the depth bound are left to check. */
            if (!atom_eq(memo->props[seen], pair->expr.elems[1])) {
                sj_fail(st, false, sj_expr2(a, "set:dependency-changed", name));
                return false;
            }
            int below = memo->heights[seen];
            if (below < 0) continue;
            if ((unsigned long)depth + 1u + (unsigned)below <= 64u) {
                if (below + 1 > *height_out) *height_out = below + 1;
                continue;
            }
        }
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
        if (require_admission &&
            !atom_is_symbol(current->expr.elems[SJ_KNOWN_ORIGIN], "signature") &&
            !sj_signature_owns(st->env, current) &&
            !sj_record_admitted(a, st->user_space, current)) {
            sj_fail(st, false, sj_expr2(a, "set:not-admitted", name));
            return false;
        }
        /* A cited theorem's statement holds while its own dependencies do;
         * a definition means what it meant while its own do. */
        int below = -1;
        if (atom_is_symbol(current->expr.elems[SJ_KNOWN_ORIGIN], "theorem") ||
            atom_is_symbol(current->expr.elems[SJ_KNOWN_ORIGIN], "definition")) {
            if (!sj_dependencies_walk(st, current, depth + 1u, require_admission, memo, &below))
                return false;
            if (below + 1 > *height_out) *height_out = below + 1;
        }
        if (seen == memo->count &&
            !sj_dependency_memo_add(a, memo, name, current->expr.elems[SJ_KNOWN_PROP], below)) {
            st->incomplete = true;
            sj_fail(st, false, sj_expr1(a, "set:dependency-storage"));
            return false;
        }
    }
    return true;
}

static bool sj_dependencies_current_as(SjProofState *st, Atom *record,
                                       unsigned depth, bool require_admission) {
    SjDependencyMemo memo = {0};
    int height = 0;
    return sj_dependencies_walk(st, record, depth, require_admission, &memo, &height);
}

static bool sj_dependencies_current(SjProofState *st, Atom *record,
                                    unsigned depth) {
    return sj_dependencies_current_as(st, record, depth, true);
}

/* The canonical proposition of a known fact, with the record's authority
 * and dependencies verified, noted as a dependency of this proof. */
static Atom *sj_family_rename(Arena *a, Atom *t, Atom **from, Atom **to, size_t count);

/* A known fact, or `(law T ...)`: the instance of a law stated as a schema,
 * `(schema (A ...) P)`, at the types T ...  The instance is elaborated as
 * any statement; the dependency is on the schema. */
static Atom *sj_known_proposition(SjProofState *st, Atom *name) {
    Arena *a = st->arena;
    Atom *instance = NULL;
    if (name && name->kind == ATOM_EXPR && name->expr.len >= 2u &&
        name->expr.elems[0]->kind == ATOM_SYMBOL) {
        instance = name;
        name = name->expr.elems[0];
    }
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
        !sj_signature_owns(st->env, record) &&
        !sj_record_admitted(a, st->user_space, record)) {
        sj_fail(st, false, sj_expr2(a, "set:not-admitted", name));
        return NULL;
    }
    if (atom_is_symbol(record->expr.elems[SJ_KNOWN_ORIGIN], "theorem") &&
        !sj_dependencies_current(st, record, 0u))
        return NULL;
    Atom *prop = record->expr.elems[SJ_KNOWN_PROP];
    Atom *statement = prop;
    bool schema = sj_is_expr(prop, "schema", 3u) &&
                  prop->expr.elems[1]->kind == ATOM_EXPR;
    if (schema || instance) {
        size_t params = schema ? (size_t)prop->expr.elems[1]->expr.len : 0u;
        if (!schema || !instance || (size_t)instance->expr.len - 1u != params) {
            sj_fail(st, false, sj_expr2(a, "set:law-instance", instance ? instance : name));
            return NULL;
        }
        statement = sj_family_rename(a, prop->expr.elems[2], prop->expr.elems[1]->expr.elems,
                                     instance->expr.elems + 1, params);
        if (!statement) {
            sj_fail(st, false, sj_expr2(a, "set:law-instance", instance));
            return NULL;
        }
    }
    Atom *elaborated = sj_elaborate_closed(st, statement, sj_sym(a, "prop"));
    if (!elaborated) return NULL;
    sj_note_dependency(st, name, prop);
    /* The cited statement is converted here with the definitions it
     * mentions: they are this proof's dependencies too. */
    sj_note_definition_uses(st, statement);
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
        Atom **locals = sj_locals_for(a, 0u, witness, NULL);
        if (!locals) return NULL;
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
static Atom *sj_family_record(SjProofState *st, Atom *name);

static bool sj_dependency_noted(SjProofState *st, Atom *name) {
    for (size_t i = 0u; i < st->depend_count; i++)
        if (atom_eq(st->depend_names[i], name)) return true;
    return false;
}

/* A published record depends on every definition and datatype its statement
 * or proof mentions, and on everything their own records mention in turn:
 * the whole closure it was checked against, each with the definition or
 * proposition it had then.  Reuse abstains when any of them has changed,
 * even behind an alias whose own equation is unchanged. */
static void sj_note_definition_uses(SjProofState *st, Atom *t) {
    if (!t || !st->collect_dependencies) return;
    if (t->kind == ATOM_SYMBOL) {
        if (sj_dependency_noted(st, t)) return;
        SjDefinition *def = sj_definition_of(t);
        if (def) {
            sj_note_dependency(st, def->name, def->prop);
            sj_note_definition_uses(st, def->prop);
        }
        Atom *record = sj_inductive_record(st, t);
        if (!record) record = sj_family_record(st, t);
        if (record) {
            sj_note_dependency(st, t, record->expr.elems[SJ_KNOWN_PROP]);
            sj_note_definition_uses(st, record->expr.elems[SJ_KNOWN_PROP]);
        }
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
    /* Published only over definitions whose own dependencies are current:
     * a fact over a stale definition would not be reusable as recorded. */
    SjProofState check;
    sj_proof_state_init(&check, a, space, env, limited, steps, false, false);
    if (!sj_dependencies_current_as(&check, record, 0u, false))
        return check.incomplete ? sj_incomplete(a, judgment, check.failure)
                                : sj_undetermined(a, judgment, check.failure);
    return sj_established(a, judgment, sj_expr2(a, "SetPublish", record));
}

/* ------------------------------------------------------------------------ */
/* Native proof packages.                                                    */
/*                                                                            */
/* A successful set proof is not yet a Curry--Howard witness: `prop` is the  */
/* type of proposition codes.  The native package below adds one displayed   */
/* family `Holds : prop -> Type`, compiles the retained logical proof tree,  */
/* and asks the regular kernel to check the result.  A rule of the logic is  */
/* compiled in one of two presentations.  By unfolding, the family decodes  */
/* an implication or a quantifier over a carrier of the least universe to a */
/* function type, and the rule is an abstraction or an application.  By     */
/* rule constants, the rule is a declared constant of the signature with no */
/* equation (`impI`, `impE`, `allI`, `allE`, `eqI`, `eqE`, at the carrier's */
/* instance for the quantifier and the equation): a quantifier over a       */
/* larger carrier, such as all sets, is compiled so, since its decoding     */
/* would identify a small type with a larger one, and every rule is when    */
/* the package is asked for in that presentation.  Axioms remain explicit   */
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
    /* The named `identity` interpretation: the family at a represented
     * equation `eq@A x y` computes to `Id A x y`. */
    bool identity;
    /* Every rule is compiled to its rule constant, and the family decodes
     * nothing: the presentation by rule constants throughout. */
    bool rule_constants;
    /* The carriers classified so far, and whether the quantifier and the
     * equation at each are compiled by rule constants. */
    Atom **carriers;
    bool *carrier_by_rules;
    size_t carrier_count;
    size_t carrier_cap;
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
    Atom **locals = sj_locals_for(st->arena, 0u, canonical, NULL);
    if (!locals) return NULL;
    canonical = sj_annotate_at(st, canonical, domain, locals, 0u);
    if (!canonical) return NULL;
    if (canonical_out) *canonical_out = canonical;
    Atom *kernel = sj_to_kernel_specialized(st, canonical);
    return kernel ? sj_native_reindex_objects(compiler, kernel, 0u) : NULL;
}

static Atom *sj_native_proof_type(SjNativeCompiler *compiler,
                                  Atom *canonical_prop) {
    Arena *a = compiler->proof->arena;
    Atom **locals = sj_locals_for(a, 0u, canonical_prop, NULL);
    if (!locals) return NULL;
    Atom *annotated = sj_annotate(
        compiler->proof, canonical_prop, locals, 0u);
    Atom *kernel_prop = annotated
        ? sj_to_kernel_specialized(compiler->proof, annotated) : NULL;
    return kernel_prop
        ? sj_expr3(a, "App", sj_expr2(a, "DeclConst", compiler->family_name),
                   kernel_prop)
        : NULL;
}

static bool sj_native_small_carrier(Arena *a, Atom *context, Atom *carrier);

/* The kernel code of a proposition in the target context: annotated as a
 * proof type is, its polymorphic heads specialized, and its object indices
 * placed past the proof binders. */
static Atom *sj_native_code(SjNativeCompiler *compiler, Atom *canonical) {
    SjProofState *st = compiler->proof;
    Atom **locals = sj_locals_for(st->arena, 0u, canonical, NULL);
    if (!locals) return NULL;
    Atom *annotated = sj_annotate(st, canonical, locals, 0u);
    Atom *kernel = annotated ? sj_to_kernel_specialized(st, annotated) : NULL;
    return kernel ? sj_native_reindex_objects(compiler, kernel, 0u) : NULL;
}

/* The declarations of the signature and of every instance noted so far, in
 * the order they were noted. */
static Atom *sj_native_instance_context(SjProofState *st) {
    Arena *a = st->arena;
    Atom *context = sj_kernel_context(st);
    if (!context) return NULL;
    for (size_t i = 0u; i < st->instance_count; i++) {
        Atom *kernel_type = sj_to_kernel(a, st->instance_types[i]);
        if (!kernel_type) return NULL;
        Atom *items[4] = {sj_sym(a, "PrimeCtxDecl"),
                          sj_expr2(a, "DeclConst", st->instance_names[i]),
                          kernel_type, context};
        context = atom_expr(a, items, 4u);
    }
    return context;
}

/* Whether the quantifier and the equation at a carrier (in canonical
 * spelling) are compiled by rule constants: always in that presentation,
 * and otherwise exactly when the carrier is not a type of the least
 * universe.  The carrier is classified in `context` when one is given, and
 * otherwise in the declarations noted so far; a classification is kept for
 * the whole package.  False when the carrier cannot be classified. */
static bool sj_native_carrier_by_rules(SjNativeCompiler *compiler,
                                       Atom *carrier, Atom *context,
                                       bool *by_rules) {
    if (compiler->rule_constants) {
        *by_rules = true;
        return true;
    }
    for (size_t i = 0u; i < compiler->carrier_count; i++) {
        if (!atom_eq(compiler->carriers[i], carrier)) continue;
        *by_rules = compiler->carrier_by_rules[i];
        return true;
    }
    SjProofState *st = compiler->proof;
    Arena *a = st->arena;
    if (!context) {
        sj_declare_mentioned(st, carrier, NULL);
        context = sj_native_instance_context(st);
    }
    Atom *kernel = sj_to_kernel(a, carrier);
    if (!context || !kernel) {
        sj_fail(st, false, sj_expr2(a, "set:native-carrier", carrier));
        return false;
    }
    if (compiler->carrier_count == compiler->carrier_cap) {
        size_t cap = compiler->carrier_cap ? 2u * compiler->carrier_cap : 8u;
        Atom **carriers = arena_alloc(a, sizeof(Atom *) * cap);
        bool *flags = arena_alloc(a, sizeof(bool) * cap);
        for (size_t i = 0u; i < compiler->carrier_count; i++) {
            carriers[i] = compiler->carriers[i];
            flags[i] = compiler->carrier_by_rules[i];
        }
        compiler->carriers = carriers;
        compiler->carrier_by_rules = flags;
        compiler->carrier_cap = cap;
    }
    *by_rules = !sj_native_small_carrier(a, context, kernel);
    compiler->carriers[compiler->carrier_count] = carrier;
    compiler->carrier_by_rules[compiler->carrier_count++] = *by_rules;
    return true;
}

/* The rule constant of a role.  Implication has one; the quantifier and the
 * equation have one at every instance: `allI@A` at the instance `all@A`,
 * `eqE@A` at `eq@A`.  NULL when the signature names none for the role. */
static Atom *sj_native_rule_name(SjNativeCompiler *compiler,
                                 SjNativeRuleRole role, Atom *instance) {
    SjProofState *st = compiler->proof;
    Arena *a = st->arena;
    Atom *rule = st->env->native_rules[role];
    const char *at = instance ? strchr(atom_name_cstr(instance), '@') : NULL;
    if (!rule || (instance && !at)) {
        sj_fail(st, false, sj_expr2(a, "set:native-rule-undeclared",
                                    sj_sym(a, SJ_NATIVE_RULE_ROLES[role])));
        return NULL;
    }
    if (!instance) return rule;
    size_t bytes = strlen(atom_name_cstr(rule)) + strlen(at) + 1u;
    char *name = arena_alloc(a, bytes);
    snprintf(name, bytes, "%s%s", atom_name_cstr(rule), at);
    return sj_sym(a, name);
}

/* The instance a kernel code applies, `(App (DeclConst all@A) P)` for the
 * quantifier and `(App (App (DeclConst eq@A) x) y)` for the equation, and
 * its first argument. */
static Atom *sj_native_code_instance(Atom *code, const char *poly,
                                     Atom **argument_out) {
    Atom *head = code;
    while (sj_is_expr(head, "App", 3u) &&
           sj_is_expr(head->expr.elems[1], "App", 3u))
        head = head->expr.elems[1];
    if (!sj_is_expr(head, "App", 3u) ||
        !sj_is_expr(head->expr.elems[1], "DeclConst", 2u))
        return NULL;
    Atom *instance = head->expr.elems[1]->expr.elems[1];
    const char *name = instance && instance->kind == ATOM_SYMBOL
        ? atom_name_cstr(instance) : NULL;
    size_t length = strlen(poly);
    if (!name || strncmp(name, poly, length) != 0 || name[length] != '@')
        return NULL;
    if (argument_out) *argument_out = head->expr.elems[2];
    return instance;
}

/* The type of a rule constant over the package's family `H`:
 *
 *   impI   : Π (p q : prop). (H p → H q) → H (imp p q)
 *   impE   : Π (p q : prop). H (imp p q) → H p → H q
 *   allI@A : Π (P : A → prop). (Π (x : A). H (P x)) → H (all@A P)
 *   allE@A : Π (P : A → prop). H (all@A P) → Π (x : A). H (P x)
 *   eqI@A  : Π (a b : A). Id A a b → H (eq@A a b)
 *   eqE@A  : Π (a b : A). H (eq@A a b) → Id A a b
 *
 * where `A` is the closed carrier of the instance. */
static Atom *sj_native_rule_type(Arena *a, SjNativeRuleRole role,
                                 Atom *family_name, Atom *instance,
                                 Atom *carrier) {
    Atom *H = sj_expr2(a, "DeclConst", family_name);
    Atom *prop = sj_expr2(a, "DeclConst", sj_sym(a, "prop"));
    Atom *imp = sj_expr2(a, "DeclConst", sj_sym(a, "imp"));
    Atom *code = instance ? sj_expr2(a, "DeclConst", instance) : NULL;
#define SJ_I(n) sj_expr2(a, "idx", atom_int(a, (n)))
#define SJ_H(t) sj_expr3(a, "App", H, (t))
#define SJ_APP2(f, x, y) sj_expr3(a, "App", sj_expr3(a, "App", (f), (x)), (y))
    Atom *type = NULL;
    switch (role) {
    case SJ_NATIVE_IMP_INTRO:
        type = sj_expr3(a, "Pi", prop, sj_expr3(a, "Pi", prop, sj_expr3(a, "Pi",
            sj_expr3(a, "Pi", SJ_H(SJ_I(1)), SJ_H(SJ_I(1))),
            SJ_H(SJ_APP2(imp, SJ_I(2), SJ_I(1))))));
        break;
    case SJ_NATIVE_IMP_ELIM:
        type = sj_expr3(a, "Pi", prop, sj_expr3(a, "Pi", prop, sj_expr3(a, "Pi",
            SJ_H(SJ_APP2(imp, SJ_I(1), SJ_I(0))),
            sj_expr3(a, "Pi", SJ_H(SJ_I(2)), SJ_H(SJ_I(2))))));
        break;
    case SJ_NATIVE_ALL_INTRO:
        type = sj_expr3(a, "Pi", sj_expr3(a, "Pi", carrier, prop), sj_expr3(a, "Pi",
            sj_expr3(a, "Pi", carrier, SJ_H(sj_expr3(a, "App", SJ_I(1), SJ_I(0)))),
            SJ_H(sj_expr3(a, "App", code, SJ_I(1)))));
        break;
    case SJ_NATIVE_ALL_ELIM:
        type = sj_expr3(a, "Pi", sj_expr3(a, "Pi", carrier, prop), sj_expr3(a, "Pi",
            SJ_H(sj_expr3(a, "App", code, SJ_I(0))),
            sj_expr3(a, "Pi", carrier, SJ_H(sj_expr3(a, "App", SJ_I(2), SJ_I(0))))));
        break;
    case SJ_NATIVE_EQ_INTRO:
        type = sj_expr3(a, "Pi", carrier, sj_expr3(a, "Pi", carrier, sj_expr3(a, "Pi",
            atom_expr(a, (Atom *[]){sj_sym(a, "Id"), carrier, SJ_I(1), SJ_I(0)}, 4u),
            SJ_H(SJ_APP2(code, SJ_I(2), SJ_I(1))))));
        break;
    case SJ_NATIVE_EQ_ELIM:
        type = sj_expr3(a, "Pi", carrier, sj_expr3(a, "Pi", carrier, sj_expr3(a, "Pi",
            SJ_H(SJ_APP2(code, SJ_I(1), SJ_I(0))),
            atom_expr(a, (Atom *[]){sj_sym(a, "Id"), carrier, SJ_I(2), SJ_I(1)}, 4u))));
        break;
    case SJ_NATIVE_RULE_COUNT:
        break;
    }
#undef SJ_I
#undef SJ_H
#undef SJ_APP2
    return type;
}

static bool sj_native_context_declares(Atom *context, Atom *name) {
    for (Atom *row = context; sj_is_expr(row, "PrimeCtxDecl", 4u);
         row = row->expr.elems[3])
        if (sj_is_expr(row->expr.elems[1], "DeclConst", 2u) &&
            atom_eq(row->expr.elems[1]->expr.elems[1], name))
            return true;
    return false;
}

static Atom *sj_native_declare_rule(SjNativeCompiler *compiler, Atom *context,
                                    SjNativeRuleRole role, Atom *instance,
                                    Atom *carrier) {
    Arena *a = compiler->proof->arena;
    Atom *name = sj_native_rule_name(compiler, role, instance);
    if (!name) return NULL;
    if (sj_native_context_declares(context, name)) return context;
    Atom *type = sj_native_rule_type(a, role, compiler->family_name, instance,
                                     carrier);
    if (!type) return NULL;
    Atom *items[4] = {sj_sym(a, "PrimeCtxDecl"), sj_expr2(a, "DeclConst", name),
                      type, context};
    return atom_expr(a, items, 4u);
}

/* The context, which declares the family, with the rule constants the
 * presentation needs: implication's when every rule is compiled to its
 * constant, and the quantifier's at every instance in view whose carrier is
 * compiled by rule constants (the equation's too, under the identity
 * reading).  A constant the context declares is not declared again.  NULL
 * when one cannot be declared. */
static Atom *sj_native_rule_declarations(SjNativeCompiler *compiler,
                                         Atom *context) {
    SjProofState *st = compiler->proof;
    Arena *a = st->arena;
    if (compiler->rule_constants) {
        context = sj_native_declare_rule(compiler, context,
                                         SJ_NATIVE_IMP_INTRO, NULL, NULL);
        if (context)
            context = sj_native_declare_rule(compiler, context,
                                             SJ_NATIVE_IMP_ELIM, NULL, NULL);
    }
    Atom *classifying = context;
    for (size_t i = 0u; context && i < st->instance_count; i++) {
        const char *name = atom_name_cstr(st->instance_names[i]);
        Atom *type = st->instance_types[i];
        bool all = name && strncmp(name, "all@", 4u) == 0 &&
                   sj_is_expr(type, "Pi", 3u) &&
                   sj_is_expr(type->expr.elems[1], "Pi", 3u);
        bool eq = compiler->identity && name && strncmp(name, "eq@", 3u) == 0 &&
                  sj_is_expr(type, "Pi", 3u);
        if (!all && !eq) continue;
        Atom *carrier = all ? type->expr.elems[1]->expr.elems[1]
                            : type->expr.elems[1];
        bool by_rules = false;
        if (!sj_native_carrier_by_rules(compiler, carrier, classifying,
                                        &by_rules))
            return NULL;
        if (!by_rules) continue;
        Atom *kernel_carrier = sj_to_kernel(a, carrier);
        if (!kernel_carrier) return NULL;
        context = sj_native_declare_rule(
            compiler, context, all ? SJ_NATIVE_ALL_INTRO : SJ_NATIVE_EQ_INTRO,
            st->instance_names[i], kernel_carrier);
        if (context)
            context = sj_native_declare_rule(
                compiler, context, all ? SJ_NATIVE_ALL_ELIM : SJ_NATIVE_EQ_ELIM,
                st->instance_names[i], kernel_carrier);
    }
    return context;
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
    snprintf(internal, sizeof internal,
             CETTA_PRIME_PROOF_IDENTITY_PREFIX "%.24s", digest);
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
        if (!compiler->rule_constants) return sj_expr3(a, "App", major, minor);
        /* `impE p q major minor` */
        Atom *rule = sj_native_rule_name(compiler, SJ_NATIVE_IMP_ELIM, NULL);
        Atom *p = sj_native_code(compiler, args[0]);
        Atom *q = sj_native_code(compiler, args[1]);
        if (!rule || !p || !q) return NULL;
        Atom *applied = sj_expr3(a, "App", sj_expr3(a, "App",
            sj_expr2(a, "DeclConst", rule), p), q);
        return sj_expr3(a, "App", sj_expr3(a, "App", applied, major), minor);
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
        bool by_rules = false;
        if (!sj_native_carrier_by_rules(compiler, domain, NULL, &by_rules))
            return NULL;
        if (!by_rules) return sj_expr3(a, "App", major, witness);
        /* `allE@A P major witness` */
        Atom *code = sj_native_code(compiler, whnf);
        Atom *predicate = NULL;
        Atom *instance = code
            ? sj_native_code_instance(code, "all", &predicate) : NULL;
        Atom *rule = instance
            ? sj_native_rule_name(compiler, SJ_NATIVE_ALL_ELIM, instance) : NULL;
        if (!rule) return NULL;
        Atom *applied = sj_expr3(a, "App", sj_expr3(a, "App",
            sj_expr2(a, "DeclConst", rule), predicate), major);
        return sj_expr3(a, "App", applied, witness);
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
        Atom *abstraction = body ? sj_expr3(a, "Lam", domain, body) : NULL;
        if (!abstraction || !compiler->rule_constants) return abstraction;
        /* `impI p q (λ h. body)` */
        Atom *rule = sj_native_rule_name(compiler, SJ_NATIVE_IMP_INTRO, NULL);
        Atom *p = sj_native_code(compiler, args[0]);
        Atom *q = sj_native_code(compiler, args[1]);
        if (!rule || !p || !q) return NULL;
        Atom *applied = sj_expr3(a, "App", sj_expr3(a, "App",
            sj_expr2(a, "DeclConst", rule), p), q);
        return sj_expr3(a, "App", applied, abstraction);
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
        Atom *native_domain = sj_to_kernel_specialized(st, domain);
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
        Atom *abstraction = body ? sj_expr3(a, "Lam", native_domain, body) : NULL;
        if (!abstraction) return NULL;
        bool by_rules = false;
        if (!sj_native_carrier_by_rules(compiler, domain, NULL, &by_rules))
            return NULL;
        if (!by_rules) return abstraction;
        /* `allI@A P (λ x. body)` */
        Atom *code = sj_native_code(compiler, whnf);
        Atom *predicate = NULL;
        Atom *instance = code
            ? sj_native_code_instance(code, "all", &predicate) : NULL;
        Atom *rule = instance
            ? sj_native_rule_name(compiler, SJ_NATIVE_ALL_INTRO, instance) : NULL;
        if (!rule) return NULL;
        return sj_expr3(a, "App", sj_expr3(a, "App",
            sj_expr2(a, "DeclConst", rule), predicate), abstraction);
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

/* The family `Holds : prop -> Sort 0` decodes a code into a type of
 * `Sort 0`.  `Holds (all@A P) ↦ Pi A (Holds (P #0))` and
 * `Holds (eq@A x y) ↦ Id A x y` keep that type only when the carrier A is
 * itself in `Sort 0`; for a larger carrier, such as a universe or the sort
 * of all sets, the right side is a larger type than the left, and the
 * kernel, which computes with these rules without checking them, would
 * identify the two.  No decoding rule is generated for such a carrier: its
 * quantifier and equation are compiled by rule constants instead. */
static bool sj_native_small_carrier(Arena *a, Atom *context, Atom *carrier) {
    if (!context || !carrier) return false;
    CettaPrimeRegularKernelBudget budget;
    cetta_prime_regular_kernel_budget_init(&budget, false, 0u);
    Atom *small = sj_expr2(a, "Sort", sj_expr2(a, "LevelConst", atom_int(a, 0)));
    CettaPrimeRegularKernelResult checked =
        cetta_prime_regular_kernel_check_intrinsic(a, context, carrier, small,
                                                   &budget);
    return checked.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED;
}

/* The decoding rules of the family, after the reachable definition rules:
 * implication's, unless every rule is compiled to its constant, and the
 * quantifier's (the equation's, under the identity reading) at every
 * instance in view whose carrier is not compiled by rule constants. */
static Atom *sj_native_rules(SjNativeCompiler *compiler, Atom *context) {
    SjProofState *st = compiler->proof;
    Arena *a = st->arena;
    Atom *family = sj_expr2(a, "DeclConst", compiler->family_name);
    Atom *p0 = sj_expr2(a, "PVar", atom_int(a, 0));
    Atom *p1 = sj_expr2(a, "PVar", atom_int(a, 1));
    Atom *rules = st->reachable_rules;
    if (!compiler->rule_constants) {
        Atom *imp = sj_expr2(a, "DeclConst", sj_sym(a, "imp"));
        Atom *imp_code = sj_expr3(a, "App", sj_expr3(a, "App", imp, p0), p1);
        Atom *imp_patterns = atom_expr(a, (Atom *[]){imp_code}, 1u);
        Atom *imp_rhs = sj_expr3(
            a, "Pi", sj_expr3(a, "App", family, p0),
            sj_expr3(a, "App", family, p1));
        rules = sj_native_rule_cons(a, compiler->family_name, imp_patterns,
                                    imp_rhs, rules);
    }
    for (size_t i = 0u; i < st->instance_count; i++) {
        const char *name = atom_name_cstr(st->instance_names[i]);
        Atom *type = st->instance_types[i];
        if (!name || strncmp(name, "all@", 4u) != 0 ||
            !sj_is_expr(type, "Pi", 3u) ||
            !sj_is_expr(type->expr.elems[1], "Pi", 3u))
            continue;
        Atom *carrier = type->expr.elems[1]->expr.elems[1];
        bool by_rules = false;
        if (!sj_native_carrier_by_rules(compiler, carrier, context, &by_rules))
            return NULL;
        if (by_rules) continue;
        Atom *domain = sj_to_kernel(a, carrier);
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
    if (!compiler->identity) return rules;
    /* `Holds (eq@A x y) ↦ Id A x y`, for every equation instance in view. */
    for (size_t i = 0u; i < st->instance_count; i++) {
        const char *name = atom_name_cstr(st->instance_names[i]);
        Atom *type = st->instance_types[i];
        if (!name || strncmp(name, "eq@", 3u) != 0 ||
            !sj_is_expr(type, "Pi", 3u) ||
            !sj_is_expr(type->expr.elems[2], "Pi", 3u))
            continue;
        bool by_rules = false;
        if (!sj_native_carrier_by_rules(compiler, type->expr.elems[1], context,
                                        &by_rules))
            return NULL;
        if (by_rules) continue;
        Atom *carrier = sj_to_kernel(a, type->expr.elems[1]);
        if (!carrier) return NULL;
        Atom *code = sj_expr3(
            a, "App",
            sj_expr3(a, "App", sj_expr2(a, "DeclConst", st->instance_names[i]),
                     p0),
            p1);
        Atom *patterns = atom_expr(a, (Atom *[]){code}, 1u);
        Atom *rhs = atom_expr(a, (Atom *[]){sj_sym(a, "Id"), carrier, p0, p1},
                              4u);
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
    Arena *a, Atom *judgment, Atom *name, bool rule_constants,
    SjProofState *st) {
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
    snprintf(family_spelling, sizeof family_spelling,
             CETTA_PRIME_HOLDS_IDENTITY_PREFIX "%.24s",
             sj_signature_digest());
    SjNativeCompiler compiler = {
        .proof = st,
        .family_name = sj_sym(a, family_spelling),
        .rule_constants = rule_constants,
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
        Atom **locals = sj_locals_for(
            a, 0u, compiler.assumptions[i].source_prop, NULL);
        Atom *annotated = locals ? sj_annotate(
            st, compiler.assumptions[i].source_prop, locals, 0u) : NULL;
        compiler.assumptions[i].kernel_prop = annotated
            ? sj_to_kernel_specialized(st, annotated) : NULL;
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

    if (!sj_kernel_context(st))
        return sj_undetermined(a, judgment,
                               sj_expr1(a, "set:native-context"));
    Atom *context = sj_native_instance_context(st);
    if (!context)
        return sj_undetermined(a, judgment,
                               sj_expr1(a, "set:native-instance-type"));
    Atom *proof_family_type = sj_expr3(
        a, "Pi", sj_expr2(a, "DeclConst", sj_sym(a, "prop")),
        sj_expr2(a, "Sort", sj_expr2(a, "LevelConst", atom_int(a, 0))));
    context = atom_expr(
        a, (Atom *[]){sj_sym(a, "PrimeCtxDecl"),
                      sj_expr2(a, "DeclConst", compiler.family_name),
                      proof_family_type, context}, 4u);
    context = sj_native_rule_declarations(&compiler, context);
    if (!context)
        return sj_undetermined(a, judgment,
                               st->failure ? st->failure
                                           : sj_expr1(a, "set:native-rule-constants"));
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
    Atom *rules = sj_native_rules(&compiler, context);
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
                                 Atom *name, bool rule_constants,
                                 bool limited, uint64_t steps) {
    SjSetEnv *env = sj_env(space);
    if (!env)
        return sj_undetermined(a, judgment,
                               sj_expr1(a, "set:signature-unavailable"));
    SjProofState st;
    sj_proof_state_init(&st, a, space, env, limited, steps, false, true);
    return sj_set_native_proof_with_state(a, judgment, name, rule_constants,
                                          &st);
}

/* Whether a package was compiled with every rule as its rule constant: its
 * context declares implication's introduction, which only that
 * presentation declares.  A package is recompiled in the presentation it
 * names, and must then agree with it field by field. */
static bool sj_native_package_rule_constants(SjSetEnv *env, Atom *package) {
    Atom *rule = env->native_rules[SJ_NATIVE_IMP_INTRO];
    return rule &&
           sj_native_context_declares(
               package->expr.elems[SJ_NATIVE_PACKAGE_CONTEXT], rule);
}

/* ------------------------------------------------------------------------ */
/* Linking under a named interpretation.                                      */
/*                                                                            */
/* A native package keeps the source's assumptions as declared constants.    */
/* `(set:native-link identity package)` rechecks the package, replaces every  */
/* assumption by a realization, and asks the regular kernel to check each     */
/* realization at its assumption's type and the linked term at the package's */
/* type, in the package context without the assumption declarations.  The    */
/* interpretation is named and opt-in: under `identity` the family at a       */
/* represented equation `eq@A x y` computes to `Id A x y`.  A package that is */
/* not linked keeps the represented family unchanged.                         */
/*                                                                            */
/* The realization is chosen by the assumption's proposition:                 */
/*   substitution  all@(A→prop) (λP. all@A (λx. all@A (λy.                    */
/*                   eq@A x y ⇒ P x ⇒ P y)))             identity elimination */
/*   the principle T-ind of an inductive type T          its recursor T-rec   */
/*   an equation   all@A1 (λx1. … eq@B l r)              λx1 …. refl l        */
/* An equation is realized by reflexivity exactly when the kernel accepts    */
/* it, that is, when its two sides are definitionally equal: reflexivity     */
/* itself, a defining equation, eta.  The kernel judges every realization.   */
/* An assumption without one stays declared, and the result names it.        */
/*                                                                            */
/* A theory that declares the reading, `(set:interpret &self identity)`, has */
/* its packages linked automatically wherever they are used.                  */
/* ------------------------------------------------------------------------ */

enum {
    SJ_NATIVE_LINKED_LEN = 8,
    SJ_NATIVE_LINKED_INTERPRETATION = 1,
    SJ_NATIVE_LINKED_PACKAGE = 2,
    SJ_NATIVE_LINKED_TERM = 3,
    SJ_NATIVE_LINKED_TYPE = 4,
    SJ_NATIVE_LINKED_CONTEXT = 5,
    SJ_NATIVE_LINKED_RULES = 6,
    SJ_NATIVE_LINKED_REALIZATIONS = 7
};

static Atom *sj_link_index(Arena *a, uint64_t index) {
    return sj_expr2(a, "idx", atom_int(a, (int64_t)index));
}

static bool sj_link_is_index(Atom *t, uint64_t expected) {
    uint64_t index = 0u;
    return sj_intrinsic_index(t, &index) && index == expected;
}

/* No index of `t` escapes the binders around it. */
static bool sj_link_closed(Atom *t, uint64_t depth) {
    uint64_t index = 0u;
    if (sj_intrinsic_index(t, &index)) return index < depth;
    if (sj_is_leaf(t)) return true;
    for (CettaExprIndex i = 1u; i < t->expr.len; i++)
        if (!sj_link_closed(t->expr.elems[i],
                            sj_binds_body(t, i) ? depth + 1u : depth))
            return false;
    return true;
}

/* A declared constant whose spelling starts with `prefix`, such as `eq@A`. */
static bool sj_link_instance(Atom *t, const char *prefix) {
    if (!sj_is_expr(t, "DeclConst", 2u) || !t->expr.elems[1] ||
        t->expr.elems[1]->kind != ATOM_SYMBOL)
        return false;
    const char *spelling = atom_name_cstr(t->expr.elems[1]);
    return spelling && strncmp(spelling, prefix, strlen(prefix)) == 0;
}

/* `all@A (λ x : A. body)`. */
static bool sj_link_universal(Atom *t, Atom **carrier, Atom **body) {
    if (!sj_is_expr(t, "App", 3u) || !sj_link_instance(t->expr.elems[1], "all@") ||
        !sj_is_expr(t->expr.elems[2], "Lam", 3u))
        return false;
    *carrier = t->expr.elems[2]->expr.elems[1];
    *body = t->expr.elems[2]->expr.elems[2];
    return true;
}

/* `imp premise conclusion`. */
static bool sj_link_implication(Atom *t, Atom **premise, Atom **conclusion) {
    if (!sj_is_expr(t, "App", 3u) || !sj_is_expr(t->expr.elems[1], "App", 3u))
        return false;
    Atom *head = t->expr.elems[1]->expr.elems[1];
    if (!sj_is_expr(head, "DeclConst", 2u) ||
        !atom_is_symbol(head->expr.elems[1], "imp"))
        return false;
    *premise = t->expr.elems[1]->expr.elems[2];
    *conclusion = t->expr.elems[2];
    return true;
}

/* `eq@A (idx left) (idx right)`. */
static bool sj_link_equation(Atom *t, uint64_t left, uint64_t right) {
    return sj_is_expr(t, "App", 3u) && sj_is_expr(t->expr.elems[1], "App", 3u) &&
           sj_link_instance(t->expr.elems[1]->expr.elems[1], "eq@") &&
           sj_link_is_index(t->expr.elems[1]->expr.elems[2], left) &&
           sj_link_is_index(t->expr.elems[2], right);
}

/* `(idx function) (idx argument)`. */
static bool sj_link_applied(Atom *t, uint64_t function, uint64_t argument) {
    return sj_is_expr(t, "App", 3u) &&
           sj_link_is_index(t->expr.elems[1], function) &&
           sj_link_is_index(t->expr.elems[2], argument);
}

/* all@A1 (λ x1. … all@An (λ xn. eq@B l r))  ↦  λ x1 … xn. refl l
 * A candidate only: it realizes the equation when the kernel accepts it. */
static Atom *sj_link_reflexivity(Arena *a, Atom *proposition) {
    size_t depth = 0u;
    Atom *carrier = NULL, *body = NULL;
    for (Atom *cursor = proposition;
         sj_link_universal(cursor, &carrier, &body); cursor = body)
        depth++;
    Atom **carriers = arena_alloc(a, sizeof(Atom *) * (depth ? depth : 1u));
    if (!carriers) return NULL;
    depth = 0u;
    while (sj_link_universal(proposition, &carrier, &body)) {
        carriers[depth++] = carrier;
        proposition = body;
    }
    if (!sj_is_expr(proposition, "App", 3u) ||
        !sj_is_expr(proposition->expr.elems[1], "App", 3u) ||
        !sj_link_instance(proposition->expr.elems[1]->expr.elems[1], "eq@"))
        return NULL;
    Atom *realization =
        sj_expr2(a, "Refl", proposition->expr.elems[1]->expr.elems[2]);
    for (size_t i = depth; i > 0u; i--)
        realization = sj_expr3(a, "Lam", carriers[i - 1u], realization);
    return realization;
}

/* all@(A→prop) (λ P. all@A (λ x. all@A (λ y. eq@A x y ⇒ P x ⇒ P y)))  ↦
 *   λ P x y (e : Holds (eq@A x y)) (h : Holds (P x)).
 *     id:eliminate A x (λ y' (p : Id A x y'). Holds (P y')) h y e */
static Atom *sj_link_substitution(Arena *a, Atom *family, Atom *proposition) {
    Atom *predicate = NULL, *outer = NULL, *carrier = NULL, *middle = NULL;
    Atom *endpoint_carrier = NULL, *matrix = NULL;
    if (!sj_link_universal(proposition, &predicate, &outer) ||
        !sj_is_expr(predicate, "Pi", 3u) ||
        !sj_link_universal(outer, &carrier, &middle) ||
        !sj_link_universal(middle, &endpoint_carrier, &matrix) ||
        !atom_eq(carrier, endpoint_carrier) ||
        !atom_eq(predicate->expr.elems[1], carrier) ||
        !sj_link_closed(carrier, 0u))
        return NULL;
    Atom *equation = NULL, *transport = NULL;
    Atom *at_point = NULL, *at_endpoint = NULL;
    if (!sj_link_implication(matrix, &equation, &transport) ||
        !sj_link_equation(equation, 1u, 0u) ||
        !sj_link_implication(transport, &at_point, &at_endpoint) ||
        !sj_link_applied(at_point, 2u, 1u) ||
        !sj_link_applied(at_endpoint, 2u, 0u))
        return NULL;
    Atom *holds = sj_expr2(a, "DeclConst", family);
    Atom *path_type = atom_expr(
        a, (Atom *[]){sj_sym(a, "Id"), carrier, sj_link_index(a, 4u),
                      sj_link_index(a, 0u)}, 4u);
    Atom *motive = sj_expr3(
        a, "Lam", carrier,
        sj_expr3(a, "Lam", path_type,
                 sj_expr3(a, "App", holds,
                          sj_expr3(a, "App", sj_link_index(a, 6u),
                                   sj_link_index(a, 1u)))));
    Atom *arguments[6] = {carrier, sj_link_index(a, 3u), motive,
                          sj_link_index(a, 0u), sj_link_index(a, 2u),
                          sj_link_index(a, 1u)};
    Atom *elimination = sj_expr2(a, "DeclConst", sj_sym(a, "id:eliminate"));
    for (size_t i = 0u; i < 6u; i++)
        elimination = sj_expr3(a, "App", elimination, arguments[i]);
    Atom *hypothesis = sj_expr3(
        a, "App", holds,
        sj_expr3(a, "App", sj_link_index(a, 3u), sj_link_index(a, 2u)));
    return sj_expr3(a, "Lam", predicate,
           sj_expr3(a, "Lam", carrier,
           sj_expr3(a, "Lam", carrier,
           sj_expr3(a, "Lam", sj_expr3(a, "App", holds, equation),
           sj_expr3(a, "Lam", hypothesis, elimination)))));
}

/* The principle T-ind of an inductive type T,
 *   all@(T→prop) (λ P. c1 ⇒ … ⇒ ck ⇒ all@T (λ x. P x))  ↦
 *   λ P (m1 : Holds c1) … (mk : Holds ck) (x : T).
 *     T-rec (λ m : T. Holds (P m)) m1 … mk x
 * The principle's name and the recursor are the ones `set:inductive`
 * generates for T. */
static Atom *sj_link_induction(SjProofState *st, Atom *family,
                               Atom *source_name, Atom *proposition) {
    Arena *a = st->arena;
    Atom *predicate = NULL, *body = NULL;
    if (!sj_link_universal(proposition, &predicate, &body) ||
        !sj_is_expr(predicate, "Pi", 3u) ||
        !sj_is_expr(predicate->expr.elems[1], "DeclConst", 2u))
        return NULL;
    Atom *carrier = predicate->expr.elems[1];
    const char *type_spelling = atom_name_cstr(carrier->expr.elems[1]);
    if (!type_spelling) return NULL;
    size_t capacity = strlen(type_spelling) + 5u;
    char *spelling = arena_alloc(a, capacity);
    snprintf(spelling, capacity, "%s-ind", type_spelling);
    if (!atom_is_symbol_named(source_name, spelling)) return NULL;
    snprintf(spelling, capacity, "%s-rec", type_spelling);
    Atom *recursor = sj_sym(a, spelling);
    if (!sj_constant_type(st, recursor)) return NULL;
    Atom *cases[64];
    size_t count = 0u;
    Atom *premise = NULL, *rest = NULL;
    while (sj_link_implication(body, &premise, &rest)) {
        if (count == 64u) return NULL;
        cases[count++] = premise;
        body = rest;
    }
    Atom *final_carrier = NULL, *final_body = NULL;
    if (!sj_link_universal(body, &final_carrier, &final_body) ||
        !atom_eq(final_carrier, carrier) ||
        !sj_link_applied(final_body, 1u, 0u))
        return NULL;
    Atom *holds = sj_expr2(a, "DeclConst", family);
    Atom *motive = sj_expr3(
        a, "Lam", carrier,
        sj_expr3(a, "App", holds,
                 sj_expr3(a, "App", sj_link_index(a, count + 2u),
                          sj_link_index(a, 0u))));
    Atom *call = sj_expr3(a, "App", sj_expr2(a, "DeclConst", recursor), motive);
    for (size_t i = 1u; i <= count; i++)
        call = sj_expr3(a, "App", call, sj_link_index(a, count + 1u - i));
    call = sj_expr3(a, "App", call, sj_link_index(a, 0u));
    Atom *realization = sj_expr3(a, "Lam", carrier, call);
    for (size_t i = count; i > 0u; i--) {
        /* The case sits under `P` in the principle and under `P m1 … m(i-1)`
         * in the realization. */
        Atom *domain = sj_shift(a, cases[i - 1u], 0u, (int64_t)(i - 1u));
        if (!domain) return NULL;
        realization = sj_expr3(a, "Lam", sj_expr3(a, "App", holds, domain),
                               realization);
    }
    return sj_expr3(a, "Lam", predicate, realization);
}

/* The term with each assumption constant replaced by its closed realization. */
static Atom *sj_link_replace(Arena *a, Atom *t, Atom **constants,
                             Atom **realizations, size_t count) {
    if (!t) return NULL;
    if (sj_is_expr(t, "DeclConst", 2u)) {
        for (size_t i = 0u; i < count; i++)
            if (atom_eq(t->expr.elems[1], constants[i])) return realizations[i];
        return t;
    }
    if (sj_is_leaf(t)) return t;
    Atom **children = sj_children(a, t);
    if (!children) return NULL;
    for (CettaExprIndex i = 1u; i < t->expr.len; i++) {
        children[i] = sj_link_replace(a, t->expr.elems[i], constants,
                                      realizations, count);
        if (!children[i]) return NULL;
    }
    return sj_rebuild(a, t, children);
}

/* The type a context declares for a constant. */
static Atom *sj_link_declared(Atom *context, Atom *constant) {
    for (Atom *entry = context; sj_is_expr(entry, "PrimeCtxDecl", 4u);
         entry = entry->expr.elems[3]) {
        Atom *key = entry->expr.elems[1];
        if (sj_is_expr(key, "DeclConst", 2u) && atom_eq(key->expr.elems[1], constant))
            return entry->expr.elems[2];
    }
    return NULL;
}

/* The context without the declarations of the given constants. */
static Atom *sj_link_context(Arena *a, Atom *context, Atom **constants,
                             size_t count) {
    if (!sj_is_expr(context, "PrimeCtxDecl", 4u)) return context;
    Atom *rest = sj_link_context(a, context->expr.elems[3], constants, count);
    Atom *key = context->expr.elems[1];
    for (size_t i = 0u; i < count; i++)
        if (sj_is_expr(key, "DeclConst", 2u) &&
            atom_eq(key->expr.elems[1], constants[i]))
            return rest;
    if (rest == context->expr.elems[3]) return context;
    return atom_expr(a, (Atom *[]){context->expr.elems[0], key,
                                   context->expr.elems[2], rest}, 4u);
}

/* A declined kernel check.  A realization the kernel does not accept leaves
 * the link unestablished: another realization could still succeed. */
static Atom *sj_link_declined(Arena *a, Atom *judgment,
                              CettaPrimeRegularKernelResult result,
                              Atom *evidence_head, Atom *subject) {
    Atom *reason = sj_sym(a, result.reason ? result.reason
                                           : "native-kernel-declined");
    Atom *evidence = subject
        ? atom_expr3(a, evidence_head, subject, reason)
        : atom_expr2(a, evidence_head, reason);
    if (result.status == CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED)
        return sj_incomplete(a, judgment, evidence);
    return sj_undetermined(a, judgment, evidence);
}

static Atom *sj_set_native_link_with_state(Arena *a, Atom *judgment,
                                           Atom *interpretation,
                                           Atom *package, SjProofState *st) {
    if (!atom_is_symbol(interpretation, "identity"))
        return sj_undetermined(a, judgment,
                               sj_expr2(a, "set:native-link-interpretation",
                                        interpretation));
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
    bool rule_constants = sj_native_package_rule_constants(st->env, package);
    Atom *proof_verdict = sj_set_native_proof_with_state(
        a, sj_expr2(a, "set:native-proof", name), name, rule_constants, st);
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
        return sj_refuted(a, judgment,
                          sj_expr1(a, "set:native-package-changed"));

    Atom *term = current->expr.elems[SJ_NATIVE_PACKAGE_TERM];
    Atom *type = current->expr.elems[SJ_NATIVE_PACKAGE_TYPE];
    Atom *context = current->expr.elems[SJ_NATIVE_PACKAGE_CONTEXT];
    Atom *assumptions = current->expr.elems[SJ_NATIVE_PACKAGE_ASSUMPTIONS];
    if (!sj_is_expr(type, "App", 3u) ||
        !sj_is_expr(type->expr.elems[1], "DeclConst", 2u) ||
        !assumptions || assumptions->kind != ATOM_EXPR)
        return sj_undetermined(a, judgment,
                               sj_expr1(a, "set:native-link-package"));
    Atom *family = type->expr.elems[1]->expr.elems[1];
    size_t count = assumptions->expr.len;
    size_t slots = count ? count : 1u;
    Atom **constants = arena_alloc(a, sizeof(Atom *) * slots);
    Atom **declared = arena_alloc(a, sizeof(Atom *) * slots);
    Atom **realizations = arena_alloc(a, sizeof(Atom *) * slots);
    Atom **rows = arena_alloc(a, sizeof(Atom *) * slots);
    bool *candidate = arena_alloc(a, sizeof(bool) * slots);
    for (size_t i = 0u; i < count; i++) {
        Atom *row = assumptions->expr.elems[i];
        if (!row || row->kind != ATOM_EXPR || row->expr.len != 3u)
            return sj_undetermined(a, judgment,
                                   sj_expr1(a, "set:native-link-package"));
        Atom *source_name = row->expr.elems[0];
        constants[i] = row->expr.elems[2];
        declared[i] = sj_link_declared(context, constants[i]);
        if (!sj_is_expr(declared[i], "App", 3u) ||
            !atom_eq(declared[i]->expr.elems[1], type->expr.elems[1]))
            return sj_undetermined(a, judgment,
                                   sj_expr2(a, "set:native-link-assumption",
                                            source_name));
        Atom *proposition = declared[i]->expr.elems[2];
        Atom *realization = sj_link_substitution(a, family, proposition);
        if (!realization)
            realization = sj_link_induction(st, family, source_name, proposition);
        candidate[i] = false;
        if (!realization) {
            realization = sj_link_reflexivity(a, proposition);
            candidate[i] = realization != NULL;
        }
        if (realization && !sj_link_closed(realization, 0u)) {
            realization = NULL;
            candidate[i] = false;
        }
        realizations[i] = realization;
    }
    size_t package_instances = st->instance_count;
    for (size_t i = 0u; i < count; i++)
        if (realizations[i]) sj_declare_kernel_mentioned(st, realizations[i], NULL);
    if (!sj_declare_rule_dependencies(st, NULL))
        return sj_undetermined(a, judgment,
                               sj_expr1(a, "set:native-link-dependencies"));
    for (size_t i = package_instances; i < st->instance_count; i++) {
        Atom *declaration = sj_to_kernel(a, st->instance_types[i]);
        if (!declaration)
            return sj_undetermined(a, judgment,
                                   sj_expr1(a, "set:native-link-declaration"));
        context = atom_expr(
            a, (Atom *[]){sj_sym(a, "PrimeCtxDecl"),
                          sj_expr2(a, "DeclConst", st->instance_names[i]),
                          declaration, context}, 4u);
    }
    SjNativeCompiler link_compiler = {
        .proof = st,
        .family_name = family,
        .identity = true,
        .rule_constants = rule_constants,
    };
    context = sj_native_rule_declarations(&link_compiler, context);
    if (!context)
        return sj_undetermined(a, judgment,
                               sj_expr1(a, "set:native-link-rule-constants"));
    Atom *rules = sj_native_rules(&link_compiler, context);
    if (!rules)
        return sj_undetermined(a, judgment,
                               sj_expr1(a, "set:native-decoder-rules"));
    /* An equation is realized by reflexivity when its sides are
     * definitionally equal; otherwise the assumption stays. */
    cetta_prime_regular_kernel_rules_set(rules);
    for (size_t i = 0u; i < count; i++) {
        if (!candidate[i]) continue;
        CettaPrimeRegularKernelResult tried =
            cetta_prime_regular_kernel_check_intrinsic(
                a, context, realizations[i], declared[i], &st->budget);
        if (tried.status == CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED) {
            cetta_prime_regular_kernel_rules_set(NULL);
            return sj_link_declined(a, judgment, tried,
                                    sj_sym(a, "set:native-link-realization"),
                                    sj_expr2(a, "DeclConst", constants[i]));
        }
        if (tried.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
            realizations[i] = NULL;
    }
    cetta_prime_regular_kernel_rules_set(NULL);
    /* An assumption without a realization stays declared, and is named in
     * the result: extensionality, choice, a theory's own axioms. */
    for (size_t i = 0u; i < count; i++) {
        Atom *source_name = assumptions->expr.elems[i]->expr.elems[0];
        rows[i] = realizations[i]
            ? atom_expr(a, (Atom *[]){sj_sym(a, "Realized"), source_name,
                                      constants[i], realizations[i]}, 4u)
            : atom_expr(a, (Atom *[]){sj_sym(a, "Assumed"), source_name,
                                      constants[i]}, 3u);
    }
    /* The realized assumptions, in order. */
    size_t realized = 0u;
    for (size_t i = 0u; i < count; i++) {
        if (!realizations[i]) continue;
        constants[realized] = constants[i];
        declared[realized] = declared[i];
        realizations[realized] = realizations[i];
        realized++;
    }

    /* The context without the realized assumptions. */
    Atom *linked_context = sj_link_context(a, context, constants, realized);
    Atom *linked = sj_link_replace(a, term, constants, realizations, realized);
    if (!linked)
        return sj_undetermined(a, judgment, sj_expr1(a, "set:native-link-term"));
    cetta_prime_regular_kernel_rules_set(rules);
    for (size_t i = 0u; i < realized; i++) {
        CettaPrimeRegularKernelResult realization =
            cetta_prime_regular_kernel_check_intrinsic(
                a, linked_context, realizations[i], declared[i], &st->budget);
        if (realization.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
            cetta_prime_regular_kernel_rules_set(NULL);
            return sj_link_declined(a, judgment, realization,
                                    sj_sym(a, "set:native-link-realization"),
                                    sj_expr2(a, "DeclConst", constants[i]));
        }
    }
    CettaPrimeRegularKernelResult checked =
        cetta_prime_regular_kernel_check_intrinsic(
            a, linked_context, linked, type, &st->budget);
    cetta_prime_regular_kernel_rules_set(NULL);
    if (checked.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
        return sj_link_declined(a, judgment, checked,
                                sj_sym(a, "set:native-link-kernel"), NULL);
    Atom *items[SJ_NATIVE_LINKED_LEN] = {
        sj_sym(a, "SetNativeLinkedV1"), interpretation, current, linked, type,
        linked_context, rules, atom_expr(a, rows, (CettaExprLen)count)};
    return sj_established(
        a, judgment,
        sj_expr2(a, "PrimeScopedValue",
                 atom_expr(a, items, SJ_NATIVE_LINKED_LEN)));
}

/* Whether the theory declares the named reading. */
static bool sj_interpretation_declared(Arena *a, Space *space,
                                       const char *interpretation) {
    Atom *pattern = sj_expr2(a, "set:interpretation", sj_sym(a, interpretation));
    CettaIndex *candidates = NULL;
    CettaIndex count = space_match_candidates64(space, pattern, &candidates);
    bool declared = false;
    for (CettaIndex i = 0u; i < count && !declared; i++) {
        Atom *atom = space_match_candidate_at64(space, candidates[i]);
        declared = sj_is_expr(atom, "set:interpretation", 2u) &&
                   atom_is_symbol(atom->expr.elems[1], interpretation);
    }
    free(candidates);
    return declared;
}

static Atom *sj_set_native_link(Arena *a, Space *space, Atom *judgment,
                                Atom *interpretation, Atom *package,
                                bool limited, uint64_t steps) {
    SjSetEnv *env = sj_env(space);
    if (!env)
        return sj_undetermined(a, judgment,
                               sj_expr1(a, "set:signature-unavailable"));
    SjProofState st;
    sj_proof_state_init(&st, a, space, env, limited, steps, false, true);
    return sj_set_native_link_with_state(a, judgment, interpretation, package,
                                         &st);
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
    /* A linked package is used through its link: the link is recomputed from
     * the package it names, and the consumer is checked under the link's
     * interpretation.  In a theory that declares the identity reading, a
     * package is linked under it automatically. */
    bool linked = sj_is_expr(package, "SetNativeLinkedV1", SJ_NATIVE_LINKED_LEN);
    bool automatic = !linked &&
        sj_is_expr(package, "SetNativeProofV1", SJ_NATIVE_PACKAGE_LEN) &&
        sj_interpretation_declared(a, space, "identity");
    Atom *native = linked ? package->expr.elems[SJ_NATIVE_LINKED_PACKAGE] : package;
    if (!sj_is_expr(native, "SetNativeProofV1", SJ_NATIVE_PACKAGE_LEN))
        return sj_refuted(a, judgment,
                          sj_expr1(a, "set:native-package-syntax"));
    Atom *name = native->expr.elems[SJ_NATIVE_PACKAGE_NAME];
    if (!name || name->kind != ATOM_SYMBOL)
        return sj_refuted(a, judgment,
                          sj_expr1(a, "set:native-package-name"));
    Atom *digest = native->expr.elems[SJ_NATIVE_PACKAGE_SIGNATURE];
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
    Atom *current = NULL;
    Atom *term = NULL;
    Atom *context = NULL;
    Atom *proof_type = NULL;
    if (linked || automatic) {
        Atom *interpretation = linked
            ? package->expr.elems[SJ_NATIVE_LINKED_INTERPRETATION]
            : sj_sym(a, "identity");
        Atom *link_verdict = sj_set_native_link_with_state(
            a, sj_expr3(a, "set:native-link", interpretation, native),
            interpretation, native, &st);
        Atom *link_evidence = NULL;
        SjStatus link_status = sj_verdict_status(link_verdict, &link_evidence);
        if (link_status != SJ_STATUS_ESTABLISHED)
            return sj_verdict(a, sj_status_name(link_status), judgment,
                              link_evidence);
        if (!sj_is_expr(link_evidence, "PrimeScopedValue", 2u))
            return sj_undetermined(a, judgment,
                                   sj_expr1(a, "set:native-link-recheck"));
        current = link_evidence->expr.elems[1];
        /* The link rechecked the package it names; a supplied link must also
         * agree with the one just computed. */
        if (linked && !atom_eq(package, current))
            return sj_refuted(a, judgment,
                              sj_expr1(a, "set:native-link-changed"));
        term = current->expr.elems[SJ_NATIVE_LINKED_TERM];
        context = current->expr.elems[SJ_NATIVE_LINKED_CONTEXT];
        proof_type = current->expr.elems[SJ_NATIVE_LINKED_TYPE];
    } else {
        Atom *proof_query = sj_expr2(a, "set:native-proof", name);
        Atom *proof_verdict = sj_set_native_proof_with_state(
            a, proof_query, name, sj_native_package_rule_constants(env, native),
            &st);
        Atom *proof_evidence = NULL;
        SjStatus proof_status = sj_verdict_status(proof_verdict, &proof_evidence);
        if (proof_status != SJ_STATUS_ESTABLISHED)
            return sj_verdict(a, sj_status_name(proof_status), judgment,
                              proof_evidence);
        if (!sj_is_expr(proof_evidence, "PrimeScopedValue", 2u))
            return sj_undetermined(a, judgment,
                                   sj_expr1(a, "set:native-package-recheck"));
        current = proof_evidence->expr.elems[1];
        /* A package that disagrees with the proof just rechecked is not an
         * open question. The use is rejected. */
        if (!atom_eq(package, current))
            return sj_refuted(a, judgment,
                              sj_expr1(a, "set:native-package-changed"));
        term = current->expr.elems[SJ_NATIVE_PACKAGE_TERM];
        context = current->expr.elems[SJ_NATIVE_PACKAGE_CONTEXT];
        proof_type = current->expr.elems[SJ_NATIVE_PACKAGE_TYPE];
    }

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
    if (!sj_is_expr(proof_type, "App", 3u) ||
        !sj_is_expr(proof_type->expr.elems[1], "DeclConst", 2u))
        return sj_undetermined(a, judgment,
                               sj_expr1(a, "set:native-consumer-family"));
    SjNativeCompiler consumer_compiler = {
        .proof = &st,
        .family_name = proof_type->expr.elems[1]->expr.elems[1],
        .identity = linked || automatic,
        .rule_constants = sj_native_package_rule_constants(env, native),
    };
    context = sj_native_rule_declarations(&consumer_compiler, context);
    if (!context)
        return sj_undetermined(a, judgment,
                               sj_expr1(a, "set:native-consumer-rule-constants"));
    Atom *rules = sj_native_rules(&consumer_compiler, context);
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
/* A query is a type.                                                         */
/*                                                                            */
/* For a functor the space declares stored typed (see the stored atoms       */
/* above), a match of `(F ... k ...)`, the key `k` at position p and          */
/* variables elsewhere, returning the variable at position j, has the        */
/* answers                                                                    */
/*                                                                            */
/*     Sigma (o : F@occurrence). Id Tp (F@argp o) k                           */
/*                                                                            */
/* an occurrence with the evidence that its key is `k`; the answer returns    */
/* `F@argj o`.  Two stored copies of one atom are two occurrences, and two    */
/* answers.                                                                   */
/*                                                                            */
/* A call to a definition by stored rules is typed as a query followed by    */
/* the answers of the right side each rule gives.  For `(type:stored (= (f K) */
/* T))`, the stored equations `(= (f x) r)` with a closed left side are the   */
/* rules, the type `f@rule`, named like occurrences; `f@lhs` gives a rule's   */
/* argument, `f@answers` the type of the answers of its right side, and      */
/* `f@returns` the value an answer of it returns.  The answers of `(f k)` are */
/*                                                                            */
/*     Sigma (q : Sigma (r : f@rule). Id K (f@lhs r) k). f@answers (fst q)    */
/*                                                                            */
/* a rule whose left side is the call with an answer of its right side: a    */
/* typed query of the space, or a closed value `v`, whose one answer is      */
/* `refl v` of `Id T v v`.  Two rules with one left side and equal right      */
/* sides give every answer twice.  The judgment checks every stored          */
/* occurrence or rule, and the value each answer returns.                    */
/* ------------------------------------------------------------------------ */

/* The declaration `(type:stored (F T1 ... Tn))` of a functor and arity, or
 * NULL. */
static Atom *sj_query_declaration(Arena *a, Space *space, Atom *functor,
                                  CettaExprLen arity) {
    Atom *pattern = sj_expr2(a, "type:stored", atom_var(a, "declared"));
    CettaIndex *candidates = NULL;
    CettaIndex count = space_match_candidates64(space, pattern, &candidates);
    Atom *found = NULL;
    for (CettaIndex i = 0u; i < count && !found; i++) {
        Atom *atom = space_match_candidate_at64(space, candidates[i]);
        if (!sj_is_expr(atom, "type:stored", 2u)) continue;
        Atom *declared = atom->expr.elems[1];
        if (declared && declared->kind == ATOM_EXPR &&
            declared->expr.len == arity + 1u &&
            atom_eq(declared->expr.elems[0], functor))
            found = declared;
    }
    free(candidates);
    return found;
}

/* A verdict of a query or call judgment from a kernel status that is not a
 * decision of the question asked. */
static Atom *sj_query_declined(Arena *a, Atom *judgment,
                               CettaPrimeRegularKernelResult result,
                               const char *what) {
    Atom *reason = sj_expr2(a, what,
                            sj_sym(a, result.reason ? result.reason
                                                    : "kernel-declined"));
    return result.status == CETTA_PRIME_REGULAR_KERNEL_BUDGET_EXHAUSTED
        ? sj_incomplete(a, judgment, reason)
        : sj_undetermined(a, judgment, reason);
}

/* A match read as a query of a typed functor. */
typedef struct {
    Atom *functor;
    SjStoredFunctor *stored;
    size_t key_position;
    size_t value_position;
    Atom *kernel_key;
    Atom **types;                /* [position 1..arity], kernel */
    Atom *occurrence_type;       /* (DeclConst F@occurrence) */
    Atom *answer_type;           /* kernel */
    Atom *value_map;             /* (DeclConst F@argj) */
} SjQueryPlan;

typedef enum {
    SJ_QUERY_PLANNED,
    SJ_QUERY_NOT_TYPED,          /* no typed functor: another route judges */
    SJ_QUERY_REFUSED,            /* *reason_out refutes the judgment */
    SJ_QUERY_DECLINED            /* *reason_out leaves it undetermined */
} SjQueryPlanStatus;

/* Read `(match space pattern returned)` against the space of the proof
 * state as a query of a typed functor, noting what its types and arguments
 * mention. */
static SjQueryPlanStatus sj_query_plan(SjProofState *st, Atom *term,
                                       SjQueryPlan *plan, Atom **reason_out) {
    Arena *a = st->arena;
    SjSetEnv *env = st->env;
    *reason_out = NULL;
    if (!sj_is_expr(term, "match", 4u)) return SJ_QUERY_NOT_TYPED;
    Space *space = prime_public_space_argument(a, term->expr.elems[1]);
    Atom *pattern = term->expr.elems[2];
    Atom *returned = term->expr.elems[3];
    if (!space || space != st->user_space || !pattern ||
        pattern->kind != ATOM_EXPR || pattern->expr.len < 2u ||
        pattern->expr.elems[0]->kind != ATOM_SYMBOL)
        return SJ_QUERY_NOT_TYPED;
    Atom *functor = pattern->expr.elems[0];
    CettaExprLen arity = pattern->expr.len - 1u;
    Atom *declared = sj_query_declaration(a, space, functor, arity);
    if (!declared) return SJ_QUERY_NOT_TYPED;
    *plan = (SjQueryPlan){.functor = functor};

    /* The shape of this slice: one key, a closed term at one position, and
     * distinct variables elsewhere, one of which the match returns. */
    for (CettaExprIndex i = 1u; i < pattern->expr.len; i++) {
        Atom *argument = pattern->expr.elems[i];
        if (argument->kind == ATOM_VAR) {
            for (CettaExprIndex k = 1u; k < i; k++)
                if (atom_eq(pattern->expr.elems[k], argument)) {
                    *reason_out = sj_expr2(a, "typed-query-repeated-variable",
                                           argument);
                    return SJ_QUERY_DECLINED;
                }
            if (atom_eq(argument, returned)) plan->value_position = (size_t)i;
            continue;
        }
        if (!atom_has_vars(argument) && plan->key_position == 0u) {
            plan->key_position = (size_t)i;
            continue;
        }
        *reason_out = sj_expr2(a, "typed-query-pattern", pattern);
        return SJ_QUERY_DECLINED;
    }
    if (plan->key_position == 0u || plan->value_position == 0u) {
        *reason_out = sj_expr2(a, "typed-query-pattern", pattern);
        return SJ_QUERY_DECLINED;
    }

    plan->stored = sj_env_stored_functor(env, functor, (size_t)arity);
    if (!plan->stored) {
        *reason_out = sj_expr2(a, "typed-query-functor", functor);
        return SJ_QUERY_DECLINED;
    }
    if (plan->stored->failure) {
        *reason_out = plan->stored->failure;
        return atom_is_symbol(plan->stored->failure->expr.elems[0],
                              "typed-query-ill-typed-occurrence")
            ? SJ_QUERY_REFUSED : SJ_QUERY_DECLINED;
    }
    plan->types = arena_alloc(a, sizeof(Atom *) * ((size_t)arity + 1u));
    for (size_t position = 1u; position <= arity; position++) {
        Atom *canonical = NULL;
        Atom *sort = NULL;
        if (!sj_canonical_synthesized(a, &env->overlay,
                                      declared->expr.elems[position],
                                      &canonical, &sort) ||
            !sj_is_sort(sort) ||
            !(plan->types[position] = sj_to_kernel(a, canonical))) {
            *reason_out = sj_expr2(a, "typed-query-position-type",
                                   declared->expr.elems[position]);
            return SJ_QUERY_DECLINED;
        }
        sj_declare_mentioned(st, canonical, NULL);
    }
    Atom *key = NULL;
    CettaNikOutcomeV1 outcome = CETTA_NIK_OUTCOME_INCOMPLETE;
    if (!sj_canonical_checked(a, &env->overlay,
                              pattern->expr.elems[plan->key_position],
                              declared->expr.elems[plan->key_position], &key,
                              &outcome) ||
        !(plan->kernel_key = sj_to_kernel(a, key))) {
        *reason_out = sj_expr2(a, "typed-query-key",
                               pattern->expr.elems[plan->key_position]);
        return outcome == CETTA_NIK_OUTCOME_REFUTED ? SJ_QUERY_REFUSED
                                                    : SJ_QUERY_DECLINED;
    }
    sj_declare_mentioned(st, key, NULL);
    for (size_t i = 0u; i < plan->stored->count; i++)
        for (size_t position = 1u; position <= arity; position++)
            sj_declare_kernel_mentioned(st, plan->stored->arguments[i][position],
                                        NULL);
    plan->occurrence_type = sj_expr2(
        a, "DeclConst", sj_stored_map_name(a, functor, "occurrence"));
    plan->value_map = sj_expr2(
        a, "DeclConst", sj_stored_argument_name(a, functor, plan->value_position));
    Atom *key_map = sj_expr2(
        a, "DeclConst", sj_stored_argument_name(a, functor, plan->key_position));
    plan->answer_type = sj_expr3(
        a, "Sigma", plan->occurrence_type,
        atom_expr(a, (Atom *[]){sj_sym(a, "Id"), plan->types[plan->key_position],
                                sj_expr3(a, "App", key_map,
                                         sj_expr2(a, "idx", atom_int(a, 0))),
                                plan->kernel_key}, 4u));
    return SJ_QUERY_PLANNED;
}

static Atom *sj_query_context_declare(Arena *a, Atom *context, Atom *key,
                                      Atom *type) {
    return atom_expr(a, (Atom *[]){sj_sym(a, "PrimeCtxDecl"), key, type,
                                   context}, 4u);
}

/* The context with a planned query's type of occurrences, its occurrences
 * and its argument maps, once for each functor. */
static Atom *sj_query_context(Arena *a, Atom *context, const SjQueryPlan *plan) {
    if (sj_native_context_declares(context,
                                   plan->occurrence_type->expr.elems[1]))
        return context;
    context = sj_query_context_declare(
        a, context, plan->occurrence_type,
        sj_expr2(a, "Sort", sj_expr2(a, "LevelConst", atom_int(a, 0))));
    for (size_t i = 0u; i < plan->stored->count; i++)
        context = sj_query_context_declare(
            a, context, sj_expr2(a, "DeclConst", plan->stored->names[i]),
            plan->occurrence_type);
    for (size_t position = 1u; position <= plan->stored->arity; position++)
        context = sj_query_context_declare(
            a, context,
            sj_expr2(a, "DeclConst",
                     sj_stored_argument_name(a, plan->functor, position)),
            sj_expr3(a, "Pi", plan->occurrence_type, plan->types[position]));
    return context;
}

/* The answers of a planned query: for every stored occurrence, the pair of
 * it with the reflexivity of the key, checked against the answer type;
 * `*answers_out` holds the answers that check, `*keys_out` for the others
 * the key their occurrence has.  The kernel's rules are set. */
static CettaPrimeRegularKernelResult sj_query_answers(
    SjProofState *st, Atom *context, const SjQueryPlan *plan,
    Atom ***answers_out, size_t *answer_count_out,
    Atom ***others_out, Atom ***keys_out, size_t *other_count_out) {
    Arena *a = st->arena;
    size_t count = plan->stored->count;
    Atom **answers = arena_alloc(a, sizeof(Atom *) * (count ? count : 1u));
    Atom **others = arena_alloc(a, sizeof(Atom *) * (count ? count : 1u));
    Atom **keys = arena_alloc(a, sizeof(Atom *) * (count ? count : 1u));
    size_t answer_count = 0u, other_count = 0u;
    Atom *key_map = sj_expr2(a, "DeclConst",
                             sj_stored_argument_name(a, plan->functor,
                                                     plan->key_position));
    for (size_t i = 0u; i < count; i++) {
        Atom *occurrence = sj_expr2(a, "DeclConst", plan->stored->names[i]);
        Atom *candidate = sj_expr3(a, "Pair", occurrence,
                                   sj_expr2(a, "Refl", plan->kernel_key));
        CettaPrimeRegularKernelResult checked =
            cetta_prime_regular_kernel_check_intrinsic(
                a, context, candidate, plan->answer_type, &st->budget);
        if (checked.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
            answers[answer_count++] = candidate;
            continue;
        }
        if (checked.status != CETTA_PRIME_REGULAR_KERNEL_REFUTED)
            return checked;
        CettaPrimeRegularKernelNormalFormV1 its_key =
            cetta_prime_regular_kernel_normalize_intrinsic_v1(
                a, context, sj_expr3(a, "App", key_map, occurrence),
                &st->budget);
        if (its_key.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
            return (CettaPrimeRegularKernelResult){its_key.status, NULL,
                                                   its_key.reason};
        others[other_count] = occurrence;
        keys[other_count++] = its_key.term;
    }
    *answers_out = answers;
    *answer_count_out = answer_count;
    *others_out = others;
    *keys_out = keys;
    *other_count_out = other_count;
    return (CettaPrimeRegularKernelResult){
        CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED, NULL, NULL};
}

/* The value an answer returns, computed by a function on the answers and
 * checked at the type of values. */
static CettaPrimeRegularKernelNormalFormV1 sj_query_value(
    SjProofState *st, Atom *context, Atom *returns, Atom *answer,
    Atom *value_type) {
    Arena *a = st->arena;
    CettaPrimeRegularKernelNormalFormV1 value =
        cetta_prime_regular_kernel_normalize_intrinsic_v1(
            a, context, sj_expr3(a, "App", returns, answer), &st->budget);
    if (value.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) return value;
    CettaPrimeRegularKernelResult typed =
        cetta_prime_regular_kernel_check_intrinsic(
            a, context, value.term, value_type, &st->budget);
    if (typed.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
        return (CettaPrimeRegularKernelNormalFormV1){
            .status = typed.status, .reason = typed.reason};
    return value;
}

static Atom *sj_query_list(Arena *a, const char *head, Atom **items,
                           size_t count) {
    Atom **all = arena_alloc(a, sizeof(Atom *) * (count + 1u));
    all[0] = sj_sym(a, head);
    for (size_t i = 0u; i < count; i++) all[i + 1u] = items[i];
    return atom_expr(a, all, (CettaExprLen)(count + 1u));
}

static Atom *sj_quoted(Arena *a, Atom *kernel) {
    return cetta_prime_regular_term_quote_intrinsic_v1(a, kernel);
}

static Atom *sj_typed_query_judge(SjProofState *st, Atom *judgment,
                                  Atom *term) {
    Arena *a = st->arena;
    SjQueryPlan plan;
    Atom *reason = NULL;
    switch (sj_query_plan(st, term, &plan, &reason)) {
    case SJ_QUERY_NOT_TYPED: return NULL;
    case SJ_QUERY_REFUSED: return sj_refuted(a, judgment, reason);
    case SJ_QUERY_DECLINED: return sj_undetermined(a, judgment, reason);
    case SJ_QUERY_PLANNED: break;
    }
    Atom *context = sj_native_instance_context(st);
    if (!context)
        return sj_undetermined(a, judgment, sj_expr1(a, "typed-query-context"));
    context = sj_query_context(a, context, &plan);
    cetta_prime_regular_kernel_rules_set(st->env->kernel_rules);
    CettaPrimeRegularKernelResult formed =
        cetta_prime_regular_kernel_synth_intrinsic_v1(
            a, context, plan.answer_type, &st->budget);
    if (formed.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED ||
        !cetta_prime_regular_kernel_term_is_universe_sort_v1(formed.type)) {
        cetta_prime_regular_kernel_rules_set(NULL);
        return formed.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED
            ? sj_undetermined(a, judgment, sj_expr1(a, "typed-query-answer-type"))
            : sj_query_declined(a, judgment, formed, "typed-query-answer-type");
    }
    Atom **answers = NULL, **others = NULL, **keys = NULL;
    size_t answer_count = 0u, other_count = 0u;
    CettaPrimeRegularKernelResult enumerated = sj_query_answers(
        st, context, &plan, &answers, &answer_count, &others, &keys,
        &other_count);
    if (enumerated.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
        cetta_prime_regular_kernel_rules_set(NULL);
        return sj_query_declined(a, judgment, enumerated, "typed-query-answer");
    }
    /* The value an answer returns: the returned position of its occurrence,
     * a function on the answers applied to this one. */
    Atom *returns = sj_expr3(
        a, "Lam", plan.answer_type,
        sj_expr3(a, "App", plan.value_map,
                 sj_expr2(a, "Fst", sj_expr2(a, "idx", atom_int(a, 0)))));
    Atom **rows = arena_alloc(a, sizeof(Atom *) * (answer_count ? answer_count : 1u));
    for (size_t i = 0u; i < answer_count; i++) {
        CettaPrimeRegularKernelNormalFormV1 value = sj_query_value(
            st, context, returns, answers[i], plan.types[plan.value_position]);
        if (value.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
            cetta_prime_regular_kernel_rules_set(NULL);
            return sj_query_declined(
                a, judgment,
                (CettaPrimeRegularKernelResult){value.status, NULL, value.reason},
                "typed-query-value");
        }
        rows[i] = atom_expr(a, (Atom *[]){sj_sym(a, "Answer"),
                                          sj_quoted(a, answers[i]),
                                          sj_quoted(a, value.term)}, 3u);
    }
    cetta_prime_regular_kernel_rules_set(NULL);
    Atom **not_rows = arena_alloc(a, sizeof(Atom *) * (other_count ? other_count : 1u));
    for (size_t i = 0u; i < other_count; i++)
        not_rows[i] = atom_expr(a, (Atom *[]){sj_sym(a, "NotAnswer"),
                                              sj_quoted(a, others[i]),
                                              sj_quoted(a, keys[i])}, 3u);
    Atom *evidence = atom_expr(
        a, (Atom *[]){sj_sym(a, "PrimeTypedQuery"),
                      cetta_prime_regular_term_quote_named_v1(a, plan.answer_type),
                      sj_query_list(a, "Answers", rows, answer_count),
                      sj_query_list(a, "NotAnswers", not_rows, other_count)},
        4u);
    return sj_established(a, judgment, evidence);
}

/* The name of a stored rule: the function, its argument, a digest of its
 * right side, and the copy index of an identical rule from the second. */
static Atom *sj_rule_name(Arena *a, Atom *function, Atom *argument,
                          Atom *right, size_t copy) {
    char digest[65];
    char *shown = atom_to_string(a, right);
    cetta_native_sha256_hex((const uint8_t *)(shown ? shown : ""),
                            shown ? strlen(shown) : 0u, digest);
    char *argument_shown = atom_to_string(a, argument);
    size_t bytes = strlen(atom_name_cstr(function)) +
                   strlen(argument_shown ? argument_shown : "") + 40u;
    char *name = arena_alloc(a, bytes);
    size_t at = (size_t)snprintf(name, bytes, "%s@%s@%.8s",
                                 atom_name_cstr(function),
                                 argument_shown ? argument_shown : "", digest);
    if (copy > 1u) snprintf(name + at, bytes - at, "#%zu", copy);
    return sj_sym(a, name);
}

static Atom *sj_typed_call_judge(SjProofState *st, Atom *judgment, Atom *term) {
    Arena *a = st->arena;
    SjSetEnv *env = st->env;
    Space *space = st->user_space;
    if (!term || term->kind != ATOM_EXPR || term->expr.len != 2u ||
        term->expr.elems[0]->kind != ATOM_SYMBOL)
        return NULL;
    Atom *function = term->expr.elems[0];
    /* `(type:stored (= (f K) T))` */
    Atom *pattern = sj_expr2(a, "type:stored", atom_var(a, "declared"));
    CettaIndex *candidates = NULL;
    CettaIndex count = space_match_candidates64(space, pattern, &candidates);
    Atom *declared = NULL;
    for (CettaIndex i = 0u; i < count && !declared; i++) {
        Atom *atom = space_match_candidate_at64(space, candidates[i]);
        Atom *d = sj_is_expr(atom, "type:stored", 2u) ? atom->expr.elems[1] : NULL;
        if (sj_is_expr(d, "=", 3u) && d->expr.elems[1]->kind == ATOM_EXPR &&
            d->expr.elems[1]->expr.len == 2u &&
            atom_eq(d->expr.elems[1]->expr.elems[0], function))
            declared = d;
    }
    free(candidates);
    if (!declared) return NULL;
    Atom *key_type_authored = declared->expr.elems[1]->expr.elems[1];
    Atom *value_type_authored = declared->expr.elems[2];

    /* The types of the argument and of the values, and the key. */
    Atom *canonical = NULL, *sort = NULL;
    Atom *key_type = NULL, *value_type = NULL;
    if (sj_canonical_synthesized(a, &env->overlay, key_type_authored,
                                 &canonical, &sort) && sj_is_sort(sort)) {
        sj_declare_mentioned(st, canonical, NULL);
        key_type = sj_to_kernel(a, canonical);
    }
    if (sj_canonical_synthesized(a, &env->overlay, value_type_authored,
                                 &canonical, &sort) && sj_is_sort(sort)) {
        sj_declare_mentioned(st, canonical, NULL);
        value_type = sj_to_kernel(a, canonical);
    }
    if (!key_type || !value_type)
        return sj_undetermined(a, judgment,
                               sj_expr2(a, "typed-call-types", declared));
    Atom *key = NULL;
    CettaNikOutcomeV1 outcome = CETTA_NIK_OUTCOME_INCOMPLETE;
    if (!sj_canonical_checked(a, &env->overlay, term->expr.elems[1],
                              key_type_authored, &key, &outcome))
        return outcome == CETTA_NIK_OUTCOME_REFUTED
            ? sj_refuted(a, judgment, sj_expr2(a, "typed-call-key",
                                               term->expr.elems[1]))
            : sj_undetermined(a, judgment, sj_expr2(a, "typed-call-key",
                                                    term->expr.elems[1]));
    sj_declare_mentioned(st, key, NULL);
    Atom *kernel_key = sj_to_kernel(a, key);

    /* The stored rules with a closed left side, in the order of the space:
     * each with its argument, the type of the answers of its right side and
     * the function giving the value of each. */
    CettaCount length = space_length64(space);
    size_t cap = length ? (size_t)length : 1u;
    Atom **rule_atoms = arena_alloc(a, sizeof(Atom *) * cap);
    Atom **rule_names = arena_alloc(a, sizeof(Atom *) * cap);
    Atom **arguments = arena_alloc(a, sizeof(Atom *) * cap);
    Atom **answer_types = arena_alloc(a, sizeof(Atom *) * cap);
    Atom **returns = arena_alloc(a, sizeof(Atom *) * cap);
    SjQueryPlan *plans = arena_alloc(a, sizeof(SjQueryPlan) * cap);
    bool *queried = arena_alloc(a, sizeof(bool) * cap);
    size_t rule_count = 0u;
    for (CettaIndex index = 0u; index < length; index++) {
        Atom *atom = space_get_at64(space, index);
        if (!sj_is_expr(atom, "=", 3u)) continue;
        Atom *left = atom->expr.elems[1];
        if (!left || left->kind != ATOM_EXPR || left->expr.len != 2u ||
            !atom_eq(left->expr.elems[0], function) ||
            atom_has_vars(left->expr.elems[1]))
            continue;
        Atom *right = atom->expr.elems[2];
        /* Two stored copies of one rule differ in the identity of their
         * variables only: a copy is a rule written the same way. */
        size_t copy = 1u;
        char *written = atom_to_string(a, atom);
        for (size_t prior = 0u; prior < rule_count; prior++) {
            char *other = atom_to_string(a, rule_atoms[prior]);
            if (written && other && strcmp(written, other) == 0) copy++;
        }
        Atom *argument = NULL;
        outcome = CETTA_NIK_OUTCOME_INCOMPLETE;
        if (!sj_canonical_checked(a, &env->overlay, left->expr.elems[1],
                                  key_type_authored, &argument, &outcome))
            return outcome == CETTA_NIK_OUTCOME_REFUTED
                ? sj_refuted(a, judgment, sj_expr2(a, "typed-call-ill-typed-rule", atom))
                : sj_undetermined(a, judgment, sj_expr2(a, "typed-call-rule", atom));
        sj_declare_mentioned(st, argument, NULL);
        size_t r = rule_count++;
        rule_atoms[r] = atom;
        rule_names[r] = sj_rule_name(a, function, left->expr.elems[1], right, copy);
        arguments[r] = sj_to_kernel(a, argument);
        Atom *reason = NULL;
        SjQueryPlanStatus planned = sj_query_plan(st, right, &plans[r], &reason);
        if (planned == SJ_QUERY_REFUSED) return sj_refuted(a, judgment, reason);
        if (planned == SJ_QUERY_DECLINED) return sj_undetermined(a, judgment, reason);
        queried[r] = planned == SJ_QUERY_PLANNED;
        if (queried[r]) {
            answer_types[r] = plans[r].answer_type;
            returns[r] = sj_expr3(
                a, "Lam", answer_types[r],
                sj_expr3(a, "App", plans[r].value_map,
                         sj_expr2(a, "Fst", sj_expr2(a, "idx", atom_int(a, 0)))));
            continue;
        }
        /* A closed value: its one answer is its reflexivity. */
        Atom *value = NULL;
        outcome = CETTA_NIK_OUTCOME_INCOMPLETE;
        if (atom_has_vars(right) ||
            !sj_canonical_checked(a, &env->overlay, right, value_type_authored,
                                  &value, &outcome))
            return outcome == CETTA_NIK_OUTCOME_REFUTED
                ? sj_refuted(a, judgment, sj_expr2(a, "typed-call-ill-typed-rule", atom))
                : sj_undetermined(a, judgment, sj_expr2(a, "typed-call-right-side", atom));
        sj_declare_mentioned(st, value, NULL);
        Atom *kernel_value = sj_to_kernel(a, value);
        answer_types[r] = atom_expr(a, (Atom *[]){sj_sym(a, "Id"), value_type,
                                                  kernel_value, kernel_value}, 4u);
        returns[r] = sj_expr3(a, "Lam", answer_types[r], kernel_value);
    }
    if (!kernel_key)
        return sj_undetermined(a, judgment, sj_expr1(a, "typed-call-key"));

    /* The context and the rules of the call. */
    Atom *context = sj_native_instance_context(st);
    if (!context)
        return sj_undetermined(a, judgment, sj_expr1(a, "typed-call-context"));
    for (size_t r = 0u; r < rule_count; r++)
        if (queried[r]) context = sj_query_context(a, context, &plans[r]);
    Atom *rule_type = sj_expr2(a, "DeclConst", sj_stored_map_name(a, function, "rule"));
    Atom *lhs = sj_expr2(a, "DeclConst", sj_stored_map_name(a, function, "lhs"));
    Atom *answers_of = sj_expr2(a, "DeclConst", sj_stored_map_name(a, function, "answers"));
    Atom *returns_of = sj_expr2(a, "DeclConst", sj_stored_map_name(a, function, "returns"));
    Atom *small = sj_expr2(a, "Sort", sj_expr2(a, "LevelConst", atom_int(a, 0)));
    context = sj_query_context_declare(a, context, rule_type, small);
    for (size_t r = 0u; r < rule_count; r++)
        context = sj_query_context_declare(
            a, context, sj_expr2(a, "DeclConst", rule_names[r]), rule_type);
    context = sj_query_context_declare(a, context, lhs,
                                       sj_expr3(a, "Pi", rule_type, key_type));
    context = sj_query_context_declare(a, context, answers_of,
                                       sj_expr3(a, "Pi", rule_type, small));
    context = sj_query_context_declare(
        a, context, returns_of,
        sj_expr3(a, "Pi", rule_type,
                 sj_expr3(a, "Pi",
                          sj_expr3(a, "App", answers_of,
                                   sj_expr2(a, "idx", atom_int(a, 0))),
                          value_type)));
    Atom *rules = env->kernel_rules;
    for (size_t r = 0u; r < rule_count; r++) {
        Atom *patterns = atom_expr(
            a, (Atom *[]){sj_expr2(a, "DeclConst", rule_names[r])}, 1u);
        rules = sj_native_rule_cons(a, lhs->expr.elems[1], patterns, arguments[r], rules);
        rules = sj_native_rule_cons(a, answers_of->expr.elems[1], patterns,
                                    answer_types[r], rules);
        rules = sj_native_rule_cons(a, returns_of->expr.elems[1], patterns,
                                    returns[r], rules);
    }
    Atom *selected = sj_expr3(
        a, "Sigma", rule_type,
        atom_expr(a, (Atom *[]){sj_sym(a, "Id"), key_type,
                                sj_expr3(a, "App", lhs,
                                         sj_expr2(a, "idx", atom_int(a, 0))),
                                kernel_key}, 4u));
    Atom *call_type = sj_expr3(
        a, "Sigma", selected,
        sj_expr3(a, "App", answers_of,
                 sj_expr2(a, "Fst", sj_expr2(a, "idx", atom_int(a, 0)))));

    cetta_prime_regular_kernel_rules_set(rules);
    CettaPrimeRegularKernelResult formed =
        cetta_prime_regular_kernel_synth_intrinsic_v1(a, context, call_type,
                                                      &st->budget);
    if (formed.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED ||
        !cetta_prime_regular_kernel_term_is_universe_sort_v1(formed.type)) {
        cetta_prime_regular_kernel_rules_set(NULL);
        return formed.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED
            ? sj_undetermined(a, judgment, sj_expr1(a, "typed-call-type"))
            : sj_query_declined(a, judgment, formed, "typed-call-type");
    }
    /* Every rule: selected by the call, or not; each selected rule's right
     * side answers, each paired with the rule's selection. */
    Atom **rows = arena_alloc(a, sizeof(Atom *) * 1u);
    size_t row_count = 0u, row_cap = 1u;
    Atom **not_rows = arena_alloc(a, sizeof(Atom *) * (rule_count ? rule_count : 1u));
    size_t not_count = 0u;
    Atom *answer_value = sj_expr3(
        a, "Lam", call_type,
        sj_expr3(a, "App",
                 sj_expr3(a, "App", returns_of,
                          sj_expr2(a, "Fst", sj_expr2(a, "Fst",
                                                      sj_expr2(a, "idx", atom_int(a, 0))))),
                 sj_expr2(a, "Snd", sj_expr2(a, "idx", atom_int(a, 0)))));
    for (size_t r = 0u; r < rule_count; r++) {
        Atom *rule = sj_expr2(a, "DeclConst", rule_names[r]);
        Atom *selection = sj_expr3(a, "Pair", rule, sj_expr2(a, "Refl", kernel_key));
        CettaPrimeRegularKernelResult chosen =
            cetta_prime_regular_kernel_check_intrinsic(a, context, selection,
                                                       selected, &st->budget);
        if (chosen.status == CETTA_PRIME_REGULAR_KERNEL_REFUTED) {
            not_rows[not_count++] = atom_expr(
                a, (Atom *[]){sj_sym(a, "NotAnswer"), sj_quoted(a, rule),
                              sj_quoted(a, arguments[r])}, 3u);
            continue;
        }
        if (chosen.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
            cetta_prime_regular_kernel_rules_set(NULL);
            return sj_query_declined(a, judgment, chosen, "typed-call-rule");
        }
        Atom **inner = NULL, **others = NULL, **keys = NULL;
        size_t inner_count = 0u, other_count = 0u;
        if (queried[r]) {
            CettaPrimeRegularKernelResult enumerated = sj_query_answers(
                st, context, &plans[r], &inner, &inner_count, &others, &keys,
                &other_count);
            if (enumerated.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
                cetta_prime_regular_kernel_rules_set(NULL);
                return sj_query_declined(a, judgment, enumerated,
                                         "typed-call-right-side");
            }
        } else {
            inner = arena_alloc(a, sizeof(Atom *));
            inner[0] = sj_expr2(a, "Refl", answer_types[r]->expr.elems[2]);
            inner_count = 1u;
        }
        for (size_t i = 0u; i < inner_count; i++) {
            Atom *answer = sj_expr3(a, "Pair", selection, inner[i]);
            CettaPrimeRegularKernelResult checked =
                cetta_prime_regular_kernel_check_intrinsic(a, context, answer,
                                                           call_type, &st->budget);
            CettaPrimeRegularKernelNormalFormV1 value =
                checked.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED
                    ? sj_query_value(st, context, answer_value, answer, value_type)
                    : (CettaPrimeRegularKernelNormalFormV1){
                          .status = checked.status, .reason = checked.reason};
            if (value.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED) {
                cetta_prime_regular_kernel_rules_set(NULL);
                return sj_query_declined(
                    a, judgment,
                    (CettaPrimeRegularKernelResult){value.status, NULL, value.reason},
                    "typed-call-answer");
            }
            if (row_count == row_cap) {
                Atom **grown = arena_alloc(a, sizeof(Atom *) * 2u * row_cap);
                for (size_t k = 0u; k < row_count; k++) grown[k] = rows[k];
                rows = grown;
                row_cap *= 2u;
            }
            rows[row_count++] = atom_expr(
                a, (Atom *[]){sj_sym(a, "Answer"), sj_quoted(a, answer),
                              sj_quoted(a, value.term)}, 3u);
        }
    }
    cetta_prime_regular_kernel_rules_set(NULL);
    Atom *evidence = atom_expr(
        a, (Atom *[]){sj_sym(a, "PrimeTypedQuery"),
                      cetta_prime_regular_term_quote_named_v1(a, call_type),
                      sj_query_list(a, "Answers", rows, row_count),
                      sj_query_list(a, "NotAnswers", not_rows, not_count)},
        4u);
    return sj_established(a, judgment, evidence);
}

Atom *prime_scoped_typed_query_judge(Arena *a, Space *space, Atom *judgment,
                                     Atom *term, bool limited, uint64_t steps) {
    if (!a || !term || term->kind != ATOM_EXPR || term->expr.len < 2u)
        return NULL;
    /* A match names the space it asks; a call is asked of the judgment's
     * space, where its rules are stored. */
    Space *asked = sj_is_expr(term, "match", 4u)
        ? prime_public_space_argument(a, term->expr.elems[1]) : space;
    /* A view the set language or this extension built is read as the
     * space it views. */
    while (asked && asked->overlay_base && sj_space_is_derived(asked))
        asked = (Space *)asked->overlay_base;
    if (!asked || sj_space_is_derived(asked) ||
        !sj_space_declares_stored(a, asked))
        return NULL;
    SjSetEnv *env = sj_env(asked);
    if (!env) return NULL;
    SjProofState st;
    sj_proof_state_init(&st, a, asked, env, limited, steps, false, true);
    return sj_is_expr(term, "match", 4u)
        ? sj_typed_query_judge(&st, judgment, term)
        : sj_typed_call_judge(&st, judgment, term);
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
    bool *recursive;        /* one flag per field */
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

/* The record of a constant of an admitted family. */
static Atom *sj_family_record(SjProofState *st, Atom *name) {
    if (!name || name->kind != ATOM_SYMBOL) return NULL;
    bool ambiguous = false;
    Atom *record = sj_known_lookup(st->arena, st->env, st->user_space, name, &ambiguous);
    if (!record || ambiguous ||
        !atom_is_symbol(record->expr.elems[SJ_KNOWN_ORIGIN], "family"))
        return NULL;
    return record;
}

static bool sj_datatype_constructors(SjProofState *st, Atom *type,
                                     SjConstructor **ctors_out, size_t *count_out,
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
    size_t declared = decl->expr.len - 2u;
    SjConstructor *ctors = arena_alloc(a, sizeof(SjConstructor) * (declared ? declared : 1u));
    if (!ctors) {
        sj_fail(st, false, sj_expr2(a, "set:define-constructor-storage", type));
        return false;
    }
    size_t n = 0u;
    for (CettaExprIndex i = 2u; i < decl->expr.len; i++) {
        Atom *c = decl->expr.elems[i];
        if (!c || c->kind != ATOM_EXPR || c->expr.len != 3u) {
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
            ctor->recursive = arena_alloc(a, sizeof(bool) * ctor->arity);
            if (!ctor->recursive) {
                sj_fail(st, false, sj_expr2(a, "set:define-constructor-storage", ctor->name));
                return false;
            }
            for (size_t k = 0u; k < ctor->arity; k++)
                ctor->recursive[k] = atom_eq(ctype->expr.elems[1u + k], type);
        }
        n++;
    }
    *ctors_out = ctors;
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
        Atom **children = sj_children(a, t);
        if (!children) return NULL;
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

static size_t sj_recursion_call_count(Atom *t, Atom *name) {
    if (!t || t->kind != ATOM_EXPR) return 0u;
    size_t count = t->expr.len >= 1u && atom_eq(t->expr.elems[0], name) ? 1u : 0u;
    for (CettaExprIndex i = 0u; i < t->expr.len; i++)
        count += sj_recursion_call_count(t->expr.elems[i], name);
    return count;
}

/* For a guarded right-hand side: the constructor field each recursive call
 * descends on, outer calls before the calls in their arguments, left to
 * right. `rec_fields[i]` is the field position of `rec_vars[i]`. */
static void sj_recursion_descents(Arena *a, Atom *t, Atom *name, size_t k,
                                  Atom **rec_vars, size_t *rec_fields, size_t rec_count,
                                  Atom **out, size_t *n) {
    if (!t || t->kind != ATOM_EXPR) return;
    if (t->expr.len >= 1u && atom_eq(t->expr.elems[0], name)) {
        Atom *scrut = t->expr.elems[1u + k];
        for (size_t i = 0u; i < rec_count; i++)
            if (atom_eq(rec_vars[i], scrut)) {
                out[(*n)++] = atom_int(a, (int64_t)rec_fields[i]);
                break;
            }
    }
    for (CettaExprIndex i = 0u; i < t->expr.len; i++)
        sj_recursion_descents(a, t->expr.elems[i], name, k, rec_vars, rec_fields,
                              rec_count, out, n);
}

/* The termination evidence of a definition by structural recursion on the
 * argument at `scrut` of the inductive type `type`: one entry per equation,
 * its constructor and the fields its recursive calls descend on. */
static Atom *sj_structural_evidence(Arena *a, size_t scrut, Atom *type,
                                    Atom **descents, size_t count) {
    Atom **items = arena_alloc(a, sizeof(Atom *) * (count + 3u));
    items[0] = sj_sym(a, "structural");
    items[1] = atom_int(a, (int64_t)scrut);
    items[2] = type;
    for (size_t e = 0u; e < count; e++) items[3u + e] = descents[e];
    return atom_expr(a, items, (CettaExprLen)(count + 3u));
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

/* `rec_type` is the recursor's declaration, a schema over its motive's
 * level; `rec_type_lowest` is its instance at level 0. */
static Atom *sj_inductive_check_declarations(
    Arena *a, SjSetEnv *env, Atom *judgment, Atom *type, Atom *universe,
    Atom **names, Atom **types, size_t ctor_count, Atom *rec_name,
    Atom *rec_type, Atom *rec_type_lowest, Atom *principle_name,
    Atom *principle, CettaPrimeRegularKernelBudget *budget) {
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
            a, &view, judgment, rec_name,
            sj_expr2(a, "type:formed", rec_type_lowest), budget, NULL);
    /* Resolving a declared name forms its type with the declaration's own
     * levels rigid.  Checking the declared recursor at its lowest instance
     * therefore forms the schema at every level of its motive, not merely
     * the instance that `type:formed` above accepted. */
    if (!obstruction) {
        space_add(&view, sj_expr3(a, ":", rec_name, rec_type));
        obstruction = sj_declaration_check_obstruction(
            a, &view, judgment, rec_name,
            sj_expr3(a, "type:check", rec_name, rec_type_lowest), budget, NULL);
    }
    if (!obstruction)
        obstruction = sj_declaration_check_obstruction(
            a, &view, judgment, principle_name,
            sj_expr3(a, "type:check", principle, sj_sym(a, "prop")), budget, NULL);
    space_free(&view);
    return obstruction;
}

static bool sj_typed_binder_parts(Atom *t, Atom **name, Atom **domain);

/* A function type whose result is written as a function type again,
 * `(-> A (-> B C))`, is the function type `(-> A B C)`.  The arguments of a
 * constructor are the arguments of all of them, so its declared type is read
 * in the second form however its arrows are grouped. */
static Atom *sj_arrow_with_all_arguments(Arena *a, Atom *type) {
    while (type && type->kind == ATOM_EXPR && type->expr.len >= 3u &&
           atom_is_symbol(type->expr.elems[0], "->")) {
        Atom *result = type->expr.elems[type->expr.len - 1u];
        if (!result || result->kind != ATOM_EXPR || result->expr.len < 3u ||
            !atom_is_symbol(result->expr.elems[0], "->"))
            break;
        CettaExprLen kept = type->expr.len - 1u;
        CettaExprLen total = kept + (result->expr.len - 1u);
        Atom **items = arena_alloc(a, sizeof(Atom *) * total);
        if (!items) break;
        for (CettaExprIndex i = 0u; i < kept; i++)
            items[i] = type->expr.elems[i];
        for (CettaExprIndex i = 1u; i < result->expr.len; i++)
            items[kept + i - 1u] = result->expr.elems[i];
        type = atom_expr(a, items, total);
    }
    return type;
}

/* True when `type` occurs in the domain of a function type inside `t`:
 * written `(-> A1 ... An R)`, in some Ai, at any depth. */
static bool sj_occurs_in_domain(Atom *t, Atom *type) {
    if (!t || t->kind != ATOM_EXPR || t->expr.len == 0u) return false;
    if (atom_is_symbol(t->expr.elems[0], "->") && t->expr.len >= 3u) {
        for (CettaExprIndex k = 1u; k + 1u < t->expr.len; k++) {
            Atom *domain = t->expr.elems[k];
            Atom *name = NULL, *bound = NULL;
            if (sj_typed_binder_parts(domain, &name, &bound)) domain = bound;
            if (sj_mentions_symbol(domain, type)) return true;
        }
        return sj_occurs_in_domain(t->expr.elems[t->expr.len - 1u], type);
    }
    for (CettaExprIndex i = 0u; i < t->expr.len; i++)
        if (sj_occurs_in_domain(t->expr.elems[i], type)) return true;
    return false;
}

/* Whether a name is already taken: a declaration gives it a type, in the
 * space, the signature or the language, or a definition defines it.  `U0`
 * and `U1`, which the kernel also spells its closed universes with, are not
 * declared by that spelling, and a theory may declare them. */
static bool sj_name_taken(SjProofState *st, Atom *name) {
    if (sj_definition_of(name)) return true;
    if (atom_is_symbol(name, "U0") || atom_is_symbol(name, "U1")) {
        Atom **declared = NULL;
        SpaceDeclaredTypeLookupCost cost = {0};
        uint32_t count = space_get_declared_types_costed(
            &st->env->overlay, &st->env->arena, name, &declared, &cost);
        free(declared);
        return count > 0u;
    }
    return sj_constant_type(st, name) != NULL;
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
    /* A name with parameters, `(T (: A U) ...)`, declares a parameterized
     * type; such declarations are not admitted yet. */
    if (type && type->kind == ATOM_EXPR && type->expr.len >= 2u &&
        type->expr.elems[0]->kind == ATOM_SYMBOL)
        return sj_undetermined(a, judgment, sj_expr2(a, "set:inductive-parameters", type));
    if (!type || type->kind != ATOM_SYMBOL)
        return sj_refuted(a, judgment, sj_expr1(a, "set:inductive-name"));
    Atom *reserved_type = sj_reserved_definition(a, space, judgment, type);
    if (reserved_type) return reserved_type;
    /* A datatype is declared at a universe `(u l)` or at a sort above them,
     * `set` among them: a datatype declared at `set` is a set. */
    if (!sj_is_expr(universe, "u", 2u) &&
        !cetta_prime_regular_kernel_sort_above_spelling_v1(NULL, universe, NULL)) {
        /* A family `(-> I ... (u n))` is legal; families are not admitted
         * yet.  Anything else is not a universe. */
        if (universe && universe->kind == ATOM_EXPR && universe->expr.len >= 3u &&
            atom_is_symbol(universe->expr.elems[0], "->") &&
            sj_is_sort(universe->expr.elems[universe->expr.len - 1u]))
            return sj_undetermined(a, judgment, sj_expr2(a, "set:inductive-family", universe));
        return sj_refuted(a, judgment, sj_expr2(a, "set:inductive-universe", universe));
    }
    SjSetEnv *env = sj_env(space);
    if (!env)
        return sj_undetermined(a, judgment, sj_expr1(a, "set:signature-unavailable"));
    SjProofState st;
    sj_proof_state_init(&st, a, space, env, false, 0u, false, true);
    if (sj_name_taken(&st, type))
        return sj_refuted(a, judgment, sj_expr2(a, "set:inductive-already-declared", type));
    size_t ctor_count = len - 3u;
    Atom **names = arena_alloc(a, sizeof(Atom *) * ctor_count);
    Atom **types = arena_alloc(a, sizeof(Atom *) * ctor_count);
    if (!names || !types)
        return sj_incomplete(a, judgment, sj_expr1(a, "set:inductive-storage"));
    memset(names, 0, sizeof(Atom *) * ctor_count);
    memset(types, 0, sizeof(Atom *) * ctor_count);
    for (size_t i = 0u; i < ctor_count; i++) {
        Atom *c = judgment->expr.elems[3u + i];
        if (!c || c->kind != ATOM_EXPR || c->expr.len != 3u || c->expr.elems[1]->kind != ATOM_SYMBOL)
            return sj_refuted(a, judgment, sj_expr2(a, "set:inductive-constructor", c));
        Atom *name = c->expr.elems[1];
        Atom *ctype = sj_arrow_with_all_arguments(a, c->expr.elems[2]);
        Atom *reserved_constructor = sj_reserved_definition(a, space, judgment, name);
        if (reserved_constructor) return reserved_constructor;
        for (size_t j = 0u; j < i; j++)
            if (atom_eq(names[j], name))
                return sj_refuted(a, judgment, sj_expr2(a, "set:inductive-duplicate", name));
        if (atom_eq(name, type) || sj_name_taken(&st, name))
            return sj_refuted(a, judgment, sj_expr2(a, "set:inductive-already-declared", name));
        /* result type T; each argument is T or a type not mentioning T */
        if (atom_eq(ctype, type)) {
            names[i] = name;
            types[i] = ctype;
            continue;
        }
        Atom *result = ctype->kind == ATOM_EXPR && ctype->expr.len >= 3u &&
                atom_is_symbol(ctype->expr.elems[0], "->")
            ? ctype->expr.elems[ctype->expr.len - 1u] : ctype;
        if (!atom_eq(result, type)) {
            /* A constructor of an indexed family returns the family at
             * indices; families are not admitted yet.  A result that is not
             * the declared type at all refutes the declaration. */
            if (sj_mentions_symbol(result, type))
                return sj_undetermined(a, judgment, sj_expr2(a, "set:inductive-indexed-result", c));
            return sj_refuted(a, judgment, sj_expr2(a, "set:inductive-result-type", c));
        }
        for (CettaExprIndex k = 1u; k + 1u < ctype->expr.len; k++) {
            Atom *arg = ctype->expr.elems[k];
            if (atom_eq(arg, type)) continue;
            if (sj_mentions_symbol(arg, type)) {
                /* The declared type in the domain of a field's function type
                 * is a non-positive occurrence: the refutation's witness.  A
                 * strictly positive occurrence, as the result of a field's
                 * function type or inside another type, is legal; such
                 * fields are not admitted yet. */
                if (sj_occurs_in_domain(arg, type))
                    return sj_refuted(a, judgment, sj_expr3(a, "set:inductive-positivity", c, arg));
                return sj_undetermined(a, judgment, sj_expr2(a, "set:inductive-positive-field", arg));
            }
            /* A dependent or otherwise unsupported field type is not a
             * refutation: formation of legal ones is not admitted yet. */
            if (!sj_open_domain(&st, arg))
                return sj_undetermined(a, judgment, sj_expr2(a, "set:inductive-argument-type", arg));
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
    if (sj_name_taken(&st, principle_name))
        return sj_refuted(a, judgment, sj_expr2(a, "set:inductive-already-declared", principle_name));
    if (sj_name_taken(&st, rec_name))
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
    /* The recursor:
     *   T-rec : (P : T -> (u $motive-level)) -> case_1 -> ... -> case_k -> (t : T) -> P t,
     * with case_i = (v1 : A1) -> ... -> (vm : Am) -> (h : P v_j) ... -> P (c_i v1 .. vm),
     * and its computation rules, one per constructor, in the kernel's spelling.
     * The motive's level is a level variable of the declaration, as the
     * language declares the identity eliminator's motive level: each
     * occurrence of T-rec chooses it, so a motive may return types.  The
     * rules do not mention it.  They belong to the supported fresh,
     * strictly-positive constructor fragment; formation is checked separately
     * before publication. */
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
    Atom *motive_level = atom_var_with_id(a, "motive-level", fresh_var_id());
    Atom *rec_type_lowest = sj_expr3(
        a, "->",
        sj_expr3(a, ":", Pm,
                 sj_expr3(a, "->", type, sj_expr2(a, "u", atom_int(a, 0)))),
        rec_type);
    rec_type = sj_expr3(
        a, "->",
        sj_expr3(a, ":", Pm,
                 sj_expr3(a, "->", type, sj_expr2(a, "u", motive_level))),
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
        rec_name, rec_type, rec_type_lowest, principle_name, principle,
        &budget);
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

/* ------------------------------------------------------------------------ */
/* Declared families.  `(set:family (: c1 T1) ... (: cn Tn) (= l1 r1) ...)` */
/* declares constants with equations between them, as Lean declares a       */
/* family over the tower inside the sets (`withFamily`); the draft builds   */
/* no such package, it compares the declared family with the ones Lean      */
/* declares.  A family is admitted on a set model: one that interprets its  */
/* constants by sets and satisfies its equations between sets.  The models  */
/* the draft knows are the ones Lean constructs, and a family is admitted   */
/* when it is one of those families up to the names of its constants,       */
/* binders and variables; its record names the model.  Lean also proves of  */
/* some families that no set model has them: a family that is one of those, */
/* up to names, is refuted with the theorem named.  Any other family is not */
/* refused, only not admitted yet: whether it has a set model is not        */
/* decided here.                                                            */
/*                                                                          */
/* A declaration at a proposition declares a proof of it, a constant typed  */
/* at `cHolds p` in Lean.  A family can extend the constants of one         */
/* admitted before: they are then read by that family's model, as the       */
/* theorem about the extension reads them, and a template names them after  */
/* `over`.                                                                  */
/*                                                                          */
/* The equations become rules.  A rule whose right side calls its own head  */
/* again without descending into a constructor its left side matches makes  */
/* a new call on every use; it is published at arity `(PObserved n)` and    */
/* unfolds only where another rule inspects the call, so a stream is never  */
/* unfolded by itself and every observation of it stops.                    */
/* ------------------------------------------------------------------------ */

/* The Lean theorems are named in full below
 * Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId, as C strings: read
 * as MeTTa, a dotted name would become a colon name.  The models hold
 * relative to a chain of closed universes with the numbers in the universe
 * of the sets, which cofinally many inaccessibles give; a record says so. */
typedef struct {
    const char *theorem;     /* the Lean theorem: its set model, or that it has none */
    const char *over;        /* the model of the constants it extends, or NULL */
    const char *relative;    /* what the theorem holds relative to */
    const char *given;       /* its further hypotheses the draft takes as given,
                                space-separated, or NULL */
    const char *text;        /* the family, as that theorem's package declares it */
    const char *law;         /* a law of the model, or NULL: its Lean theorem, */
    const char *law_suffix;  /* the suffix of its name after the type former's, */
    const char *law_text;    /* and its statement, a schema over `law~A` */
} SjKnownFamily;

/* Families with a set model. */
static const SjKnownFamily SJ_FAMILY_MODELS[] = {
    /* Streams over a set, read as the functions from the numbers into it:
     * Instances/TowerInterpretation/AmbientStreams.lean:732, over a chain of
     * closed universes (`chain`) with the numbers in the universe of the sets
     * (`omegaMem`) and its ground typed (`groundTyped`).
     * Its law is extensionality, `streams_ext` (the same file:221): two
     * streams over one set with the same element at every position are
     * equal, where the element at position k is the head after k tails
     * (`observeSet`).  The law writes the positions as the pairs reached from
     * (s, t) by applying the tail to both: the least relation that holds of
     * (s, t) and is closed under the two tails, which in the set model holds
     * exactly of the k-th tails.  Agreement at the first position alone is
     * not enough (`not_streams_ext_at_first`, the same file:292). */
    {"TowerInterpretation.AmbientSets.Streams.streamFamily_setModel", NULL,
     "CofinalInaccessibles", "chain omegaMem groundTyped",
     "(: Stream (-> set set))\n"
     "(: scons (-> (A : set) (a : A) (s : (Stream A)) (Stream A)))\n"
     "(: shead (-> (A : set) (s : (Stream A)) A))\n"
     "(: stail (-> (A : set) (s : (Stream A)) (Stream A)))\n"
     "(: iter (-> (A : set) (f : (-> A A)) (a : A) (Stream A)))\n"
     "(= (shead $A (scons $A $a $s)) $a)\n"
     "(= (stail $A (scons $A $a $s)) $s)\n"
     "(= (iter $A $f $a) (scons $A $a (iter $A $f ($f $a))))\n",
     "TowerInterpretation.AmbientSets.Streams.streams_ext", "-ext",
     "(schema (law~A)"
     " (all (Stream law~A) (lam law~s (all (Stream law~A) (lam law~t"
     "  (imp (all (Stream law~A) (lam law~u (all (Stream law~A) (lam law~v"
     "         (imp (all (-> (Stream law~A) (Stream law~A) prop) (lam law~Q"
     "                (imp (law~Q law~s law~t)"
     "                  (imp (all (Stream law~A) (lam law~x (all (Stream law~A) (lam law~y"
     "                          (imp (law~Q law~x law~y)"
     "                               (law~Q (stail law~A law~x) (stail law~A law~y)))))))"
     "                       (law~Q law~u law~v)))))"
     "              (eq law~A (shead law~A law~u) (shead law~A law~v)))))))"
     "       (eq (Stream law~A) law~s law~t)))))))\n"},
    /* Lists over a set, read as the finite lists of its members:
     * Instances/TowerInterpretation/AmbientLists.lean:443, with the same
     * hypotheses. */
    {"TowerInterpretation.AmbientSets.Lists.listFamily_setModel", NULL,
     "CofinalInaccessibles", "chain omegaMem groundTyped",
     "(: List (-> set set))\n"
     "(: nil (-> (A : set) (List A)))\n"
     "(: cons (-> (A : set) (a : A) (l : (List A)) (List A)))\n"
     "(: append (-> (A : set) (l : (List A)) (m : (List A)) (List A)))\n"
     "(= (append $A (nil $A) $m) $m)\n"
     "(= (append $A (cons $A $a $l) $m) (cons $A $a (append $A $l $m)))\n",
     NULL, NULL, NULL},
};

/* Families with no set model. */
static const SjKnownFamily SJ_FAMILY_NON_MODELS[] = {
    /* The stream equation at lists, over the list family read as lists: it
     * puts a list in front of itself
     * (Instances/TowerInterpretation/AmbientStreams.lean:1396), on the
     * stages of cofinally many inaccessibles, at the readings of `List` and
     * `cons` as the lists (`readsList`, `readsCons`), which the draft takes
     * from the admitted list family. */
    {"TowerInterpretation.AmbientSets.Streams.iterList_no_setModel",
     "TowerInterpretation.AmbientSets.Lists.listFamily_setModel",
     "CofinalInaccessibles", "readsList readsCons",
     "(over List cons)\n"
     "(: iterList (-> (A : set) (f : (-> A A)) (a : A) (List A)))\n"
     "(= (iterList $A $f $a) (cons $A $a (iterList $A $f ($f $a))))\n",
     NULL, NULL, NULL},
    /* A set with a proof that it is a member of itself, at a reading of the
     * constants of set theory (`Reads`)
     * (Instances/TowerInterpretation/AmbientHypersets.lean:1350). */
    {"TowerInterpretation.AmbientSets.selfMemberTheory_no_setModel", NULL,
     "Reads", NULL,
     "(: selfSet set)\n"
     "(: selfIn (In selfSet selfSet))\n",
     NULL, NULL, NULL},
};

/* The hypotheses a known family's theorem is stated under:
 * `(relative R)`, and `(given h ...)` for the further ones the draft takes
 * as given. */
static Atom *sj_family_hypotheses(Arena *a, const SjKnownFamily *known, Atom **given_out) {
    *given_out = NULL;
    if (known->given) {
        size_t words = 1u;
        for (const char *c = known->given; *c; c++) words += *c == ' ';
        Atom **items = arena_alloc(a, sizeof(Atom *) * (words + 1u));
        size_t n = 0u;
        items[n++] = sj_sym(a, "given");
        const char *cursor = known->given;
        while (*cursor && n <= words) {
            const char *space = strchr(cursor, ' ');
            size_t len = space ? (size_t)(space - cursor) : strlen(cursor);
            char *word = arena_alloc(a, len + 1u);
            memcpy(word, cursor, len);
            word[len] = '\0';
            items[n++] = sj_sym(a, word);
            cursor += len + (space ? 1u : 0u);
        }
        *given_out = atom_expr(a, items, (CettaExprLen)n);
    }
    return sj_expr2(a, "relative", sj_sym(a, known->relative));
}

enum {
    SJ_FAMILY_MODEL_COUNT = sizeof SJ_FAMILY_MODELS / sizeof SJ_FAMILY_MODELS[0],
    SJ_FAMILY_NON_MODEL_COUNT = sizeof SJ_FAMILY_NON_MODELS / sizeof SJ_FAMILY_NON_MODELS[0]
};

/* A known family's text, parsed once for the life of the process: the
 * names of the constants it extends, as their model's family names them,
 * then its declarations and equations. */
typedef struct {
    bool parsed;
    bool valid;
    Arena arena;
    const char *over;
    Atom **base;
    size_t base_count;
    Atom **decls;
    size_t decl_count;
    Atom **eqs;
    size_t eq_count;
} SjFamilyTemplate;

static SjFamilyTemplate g_family_models[SJ_FAMILY_MODEL_COUNT];
static SjFamilyTemplate g_family_non_models[SJ_FAMILY_NON_MODEL_COUNT];

static SjFamilyTemplate *sj_family_template(SjFamilyTemplate *slot, const SjKnownFamily *known) {
    if (!slot->parsed) {
        slot->parsed = true;
        arena_init_detached(&slot->arena);
        Atom **atoms = NULL;
        int count = parse_metta_text(known->text, &slot->arena, &atoms);
        size_t at = 0u;
        slot->over = known->over;
        if (count > 0 && atoms && atoms[0]->kind == ATOM_EXPR && atoms[0]->expr.len >= 2u &&
            atom_is_symbol(atoms[0]->expr.elems[0], "over")) {
            slot->base = atoms[0]->expr.elems + 1;
            slot->base_count = (size_t)atoms[0]->expr.len - 1u;
            at = 1u;
        }
        slot->decls = count > 0 && atoms ? atoms + at : NULL;
        while (slot->decls && at + slot->decl_count < (size_t)count &&
               sj_is_expr(slot->decls[slot->decl_count], ":", 3u))
            slot->decl_count++;
        slot->eqs = slot->decls ? slot->decls + slot->decl_count : NULL;
        slot->eq_count = count > 0 ? (size_t)count - at - slot->decl_count : 0u;
        slot->valid = slot->decl_count > 0u && (slot->base_count == 0u) == (slot->over == NULL);
        for (size_t e = 0u; slot->valid && e < slot->eq_count; e++)
            slot->valid = sj_is_expr(slot->eqs[e], "=", 3u);
    }
    return slot->valid ? slot : NULL;
}

/* `t` with each of `from[i]` replaced by `to[i]`. */
static Atom *sj_family_rename(Arena *a, Atom *t, Atom **from, Atom **to, size_t count) {
    if (!t) return NULL;
    if (t->kind == ATOM_SYMBOL) {
        for (size_t i = 0u; i < count; i++)
            if (atom_eq(t, from[i])) return to[i];
        return t;
    }
    if (t->kind != ATOM_EXPR) return t;
    Atom **items = arena_alloc(a, sizeof(Atom *) * (t->expr.len ? t->expr.len : 1u));
    if (!items) return NULL;
    for (CettaExprIndex i = 0u; i < t->expr.len; i++) {
        items[i] = sj_family_rename(a, t->expr.elems[i], from, to, count);
        if (!items[i]) return NULL;
    }
    return atom_expr(a, items, t->expr.len);
}

/* An authored equation and a template's, alike up to a one-to-one renaming
 * of their variables.  The template's constants are `from`: the first
 * `own` are the family's own, named `to` by position; the others are base
 * constants, named by the first authored symbol met in their place. */
typedef struct {
    Atom *left[32];
    Atom *right[32];
    size_t count;
} SjVariablePairs;

static bool sj_family_alike(Atom *x, Atom *y, Atom **from, Atom **to, size_t own,
                            size_t count, SjVariablePairs *pairs) {
    if (!x || !y) return false;
    if (x->kind == ATOM_VAR || y->kind == ATOM_VAR) {
        if (x->kind != ATOM_VAR || y->kind != ATOM_VAR) return false;
        for (size_t i = 0u; i < pairs->count; i++) {
            bool left = atom_eq(pairs->left[i], x), right = atom_eq(pairs->right[i], y);
            if (left || right) return left && right;
        }
        if (pairs->count == sizeof pairs->left / sizeof pairs->left[0]) return false;
        pairs->left[pairs->count] = x;
        pairs->right[pairs->count] = y;
        pairs->count++;
        return true;
    }
    if (y->kind == ATOM_SYMBOL) {
        for (size_t i = 0u; i < count; i++) {
            if (!atom_eq(y, from[i])) continue;
            if (!to[i] && i >= own && x->kind == ATOM_SYMBOL) to[i] = x;
            return to[i] && atom_eq(x, to[i]);
        }
        return atom_eq(x, y);
    }
    if (x->kind == ATOM_EXPR || y->kind == ATOM_EXPR) {
        if (x->kind != ATOM_EXPR || y->kind != ATOM_EXPR || x->expr.len != y->expr.len)
            return false;
        for (CettaExprIndex i = 0u; i < x->expr.len; i++)
            if (!sj_family_alike(x->expr.elems[i], y->expr.elems[i], from, to, own, count, pairs))
                return false;
        return true;
    }
    return atom_eq(x, y);
}

/* The base constants a template's types mention, named by the authored
 * symbols in their place; a mismatch elsewhere is left to the comparison of
 * the types. */
static void sj_family_name_base(Atom *x, Atom *y, Atom **from, Atom **to, size_t own,
                                size_t count) {
    if (!x || !y) return;
    if (y->kind == ATOM_SYMBOL) {
        for (size_t i = own; i < count; i++)
            if (atom_eq(y, from[i]) && !to[i] && x->kind == ATOM_SYMBOL) to[i] = x;
        return;
    }
    if (x->kind != ATOM_EXPR || y->kind != ATOM_EXPR || x->expr.len != y->expr.len) return;
    for (CettaExprIndex i = 0u; i < x->expr.len; i++)
        sj_family_name_base(x->expr.elems[i], y->expr.elems[i], from, to, own, count);
}

/* Whether `name` is the constant at the place of `base` in an admitted
 * family whose record names the model `over`. */
static bool sj_family_base_in_place(SjProofState *st, const char *over, Atom *base,
                                    Atom *name) {
    Atom *record = sj_family_record(st, name);
    Atom *evidence = record ? record->expr.elems[SJ_KNOWN_PROOF] : NULL;
    if (!evidence || evidence->kind != ATOM_EXPR || evidence->expr.len < 2u ||
        !atom_is_symbol(evidence->expr.elems[0], "set-model") ||
        !atom_is_symbol(evidence->expr.elems[1], over))
        return false;
    SjFamilyTemplate *model = NULL;
    for (size_t k = 0u; k < SJ_FAMILY_MODEL_COUNT && !model; k++)
        if (strcmp(over, SJ_FAMILY_MODELS[k].theorem) == 0)
            model = sj_family_template(&g_family_models[k], &SJ_FAMILY_MODELS[k]);
    Atom *prop = record->expr.elems[SJ_KNOWN_PROP];
    if (!model || !prop || prop->kind != ATOM_EXPR) return false;
    for (size_t i = 0u; i < model->decl_count; i++) {
        if (!atom_eq(model->decls[i]->expr.elems[1], base)) continue;
        Atom *decl = 1u + i < prop->expr.len ? prop->expr.elems[1u + i] : NULL;
        return sj_is_expr(decl, ":", 3u) && atom_eq(decl->expr.elems[1], name);
    }
    return false;
}

/* Whether the declared family is the template's, up to names.  `proofs[i]`
 * tells that declaration i declares a proof of a proposition; `canonical`
 * holds each type, or proposition, as the kernel formed it in `view`. */
static bool sj_family_matches(Arena *a, SjProofState *st, Space *view, Atom *judgment,
                              Atom **names, const bool *proofs, Atom **canonical,
                              size_t decl_count, Atom **equations, size_t eq_count,
                              SjFamilyTemplate *tmpl, CettaPrimeRegularKernelBudget *budget) {
    if (!tmpl || tmpl->decl_count != decl_count || tmpl->eq_count != eq_count) return false;
    size_t count = decl_count + tmpl->base_count;
    Atom **from = arena_alloc(a, sizeof(Atom *) * count);
    Atom **to = arena_alloc(a, sizeof(Atom *) * count);
    Atom **trial = arena_alloc(a, sizeof(Atom *) * count);
    bool *used = arena_alloc(a, sizeof(bool) * (eq_count ? eq_count : 1u));
    if (!from || !to || !trial || !used) return false;
    for (size_t i = 0u; i < decl_count; i++) {
        from[i] = tmpl->decls[i]->expr.elems[1];
        to[i] = names[i];
    }
    for (size_t b = 0u; b < tmpl->base_count; b++) {
        from[decl_count + b] = tmpl->base[b];
        to[decl_count + b] = NULL;
    }
    memset(used, 0, sizeof(bool) * (eq_count ? eq_count : 1u));
    /* The equations, in any order. */
    for (size_t e = 0u; e < eq_count; e++) {
        bool found = false;
        for (size_t t = 0u; !found && t < eq_count; t++) {
            if (used[t]) continue;
            memcpy(trial, to, sizeof(Atom *) * count);
            SjVariablePairs pairs = {.count = 0u};
            if (sj_family_alike(equations[e], tmpl->eqs[t], from, trial, decl_count, count,
                                &pairs)) {
                memcpy(to, trial, sizeof(Atom *) * count);
                used[t] = true;
                found = true;
            }
        }
        if (!found) return false;
    }
    /* The base constants: each named, and in its place in its family. */
    for (size_t i = 0u; i < decl_count; i++)
        sj_family_name_base(judgment->expr.elems[1u + i]->expr.elems[2],
                            tmpl->decls[i]->expr.elems[2], from, to, decl_count, count);
    for (size_t b = 0u; b < tmpl->base_count; b++)
        if (!to[decl_count + b] ||
            !sj_family_base_in_place(st, tmpl->over, tmpl->base[b], to[decl_count + b]))
            return false;
    /* The declarations, in order, by the types the kernel forms. */
    for (size_t i = 0u; i < decl_count; i++) {
        Atom *type = sj_family_rename(a, tmpl->decls[i]->expr.elems[2], from, to, count);
        Atom *query = proofs[i]
            ? sj_expr3(a, "type:check", type, sj_sym(a, "prop"))
            : sj_expr2(a, "type:formed", type);
        Atom *formed = NULL;
        Atom *obstruction = type ? sj_declaration_check_obstruction(
            a, view, judgment, names[i], query, budget, &formed) : NULL;
        if (!type || obstruction || !formed || !atom_eq(formed, canonical[i])) return false;
    }
    return true;
}

/* Kernel spelling of a family equation's side.  On the left, each variable
 * is a fresh pattern variable at each occurrence, so the rule is linear: a
 * typed instance agrees at the repeated positions anyway, as the language's
 * identity rule also assumes.  On the right, a variable is the pattern
 * variable of its first occurrence on the left. */
typedef struct {
    Atom *vars[32];
    size_t count;          /* first occurrences, by pattern variable */
    size_t slots;          /* pattern variables, every occurrence */
    size_t first[32];      /* the pattern variable of each first occurrence */
} SjFamilyVariables;

static bool sj_family_names_constant(Atom *t, Atom **names, size_t count) {
    for (size_t i = 0u; i < count; i++)
        if (atom_eq(t, names[i])) return true;
    return false;
}

static Atom *sj_family_pattern(Arena *a, Atom *t, Atom **names, size_t count,
                               SjFamilyVariables *vars) {
    if (t && t->kind == ATOM_VAR) {
        if (vars->slots == sizeof vars->vars / sizeof vars->vars[0]) return NULL;
        size_t slot = vars->slots++;
        bool seen = false;
        for (size_t i = 0u; i < vars->count && !seen; i++)
            seen = atom_eq(vars->vars[i], t);
        if (!seen) {
            vars->vars[vars->count] = t;
            vars->first[vars->count] = slot;
            vars->count++;
        }
        return sj_expr2(a, "PVar", atom_int(a, (int64_t)slot));
    }
    if (!t || t->kind != ATOM_EXPR || t->expr.len < 2u ||
        !sj_family_names_constant(t->expr.elems[0], names, count))
        return NULL;
    Atom *term = sj_expr2(a, "DeclConst", t->expr.elems[0]);
    for (CettaExprIndex i = 1u; i < t->expr.len; i++) {
        Atom *arg = sj_family_pattern(a, t->expr.elems[i], names, count, vars);
        if (!arg) return NULL;
        term = sj_expr3(a, "App", term, arg);
    }
    return term;
}

static Atom *sj_family_right(Arena *a, Atom *t, SjFamilyVariables *vars) {
    if (!t) return NULL;
    if (t->kind == ATOM_VAR) {
        for (size_t i = 0u; i < vars->count; i++)
            if (atom_eq(vars->vars[i], t))
                return sj_expr2(a, "PVar", atom_int(a, (int64_t)vars->first[i]));
        return NULL;
    }
    if (t->kind == ATOM_SYMBOL) return sj_expr2(a, "DeclConst", t);
    if (t->kind != ATOM_EXPR || t->expr.len < 2u) return NULL;
    Atom *term = sj_family_right(a, t->expr.elems[0], vars);
    for (CettaExprIndex i = 1u; term && i < t->expr.len; i++) {
        Atom *arg = sj_family_right(a, t->expr.elems[i], vars);
        term = arg ? sj_expr3(a, "App", term, arg) : NULL;
    }
    return term;
}

/* Whether the variable `v` occurs below the root of the pattern `p`. */
static bool sj_family_inside(Atom *p, Atom *v) {
    if (!p || p->kind != ATOM_EXPR) return false;
    for (CettaExprIndex i = 1u; i < p->expr.len; i++)
        if (atom_eq(p->expr.elems[i], v) || sj_family_inside(p->expr.elems[i], v))
            return true;
    return false;
}

/* Whether every use of the left side's head in `t` is a full call that
 * descends: at some position the left side matches a constructor and the
 * call passes a variable bound below it. */
static bool sj_family_calls_descend(Atom *t, Atom *lhs) {
    Atom *head = lhs->expr.elems[0];
    if (!t) return true;
    if (t->kind == ATOM_SYMBOL) return !atom_eq(t, head);
    if (t->kind != ATOM_EXPR || t->expr.len == 0u) return true;
    CettaExprIndex first = 0u;
    if (atom_eq(t->expr.elems[0], head)) {
        if (t->expr.len != lhs->expr.len) return false;
        bool descends = false;
        for (CettaExprIndex k = 1u; k < t->expr.len && !descends; k++)
            descends = lhs->expr.elems[k]->kind == ATOM_EXPR &&
                       t->expr.elems[k]->kind == ATOM_VAR &&
                       sj_family_inside(lhs->expr.elems[k], t->expr.elems[k]);
        if (!descends) return false;
        first = 1u;
    }
    for (CettaExprIndex i = first; i < t->expr.len; i++)
        if (!sj_family_calls_descend(t->expr.elems[i], lhs)) return false;
    return true;
}

/* The rule of a family equation `(= (c p1 ... pn) r)`, NULL outside the
 * shape the known families have. */
static Atom *sj_family_rule(Arena *a, Atom *eq, Atom **names, size_t count) {
    Atom *lhs = eq->expr.elems[1];
    if (!lhs || lhs->kind != ATOM_EXPR || lhs->expr.len < 2u ||
        !sj_family_names_constant(lhs->expr.elems[0], names, count))
        return NULL;
    Atom *head = lhs->expr.elems[0];
    size_t arity = (size_t)lhs->expr.len - 1u;
    SjFamilyVariables vars = {.count = 0u, .slots = 0u};
    Atom **pats = arena_alloc(a, sizeof(Atom *) * arity);
    if (!pats) return NULL;
    for (size_t k = 0u; k < arity; k++) {
        pats[k] = sj_family_pattern(a, lhs->expr.elems[1u + k], names, count, &vars);
        if (!pats[k]) return NULL;
    }
    Atom *rhs = sj_family_right(a, eq->expr.elems[2], &vars);
    if (!rhs) return NULL;
    Atom *arity_atom = atom_int(a, (int64_t)arity);
    if (!sj_family_calls_descend(eq->expr.elems[2], lhs))
        arity_atom = sj_expr2(a, "PObserved", arity_atom);
    Atom *items[5] = {sj_sym(a, "type:rule"), head, arity_atom,
                      atom_expr(a, pats, (CettaExprLen)arity), rhs};
    return atom_expr(a, items, 5u);
}

static Atom *sj_set_family(Arena *a, Space *space, Atom *judgment,
                           bool limited, uint64_t steps) {
    if (limited && steps == 0u)
        return sj_incomplete(a, judgment, sj_expr1(a, "set:family-budget"));
    CettaPrimeRegularKernelBudget budget;
    cetta_prime_regular_kernel_budget_init(&budget, limited, steps);
    size_t len = (size_t)judgment->expr.len;
    size_t decl_count = 0u;
    while (1u + decl_count < len && sj_is_expr(judgment->expr.elems[1u + decl_count], ":", 3u))
        decl_count++;
    size_t eq_count = len - 1u - decl_count;
    if (decl_count == 0u)
        return sj_refuted(a, judgment, sj_expr1(a, "set:family-arity"));
    Atom **names = arena_alloc(a, sizeof(Atom *) * decl_count);
    Atom **types = arena_alloc(a, sizeof(Atom *) * decl_count);
    Atom **canonical = arena_alloc(a, sizeof(Atom *) * decl_count);
    bool *proofs = arena_alloc(a, sizeof(bool) * decl_count);
    Atom **equations = arena_alloc(a, sizeof(Atom *) * (eq_count ? eq_count : 1u));
    if (!names || !types || !canonical || !proofs || !equations)
        return sj_incomplete(a, judgment, sj_expr1(a, "set:family-storage"));
    for (size_t e = 0u; e < eq_count; e++) {
        equations[e] = judgment->expr.elems[1u + decl_count + e];
        if (!sj_is_expr(equations[e], "=", 3u))
            return sj_refuted(a, judgment, sj_expr2(a, "set:family-equation", equations[e]));
    }
    SjSetEnv *env = sj_env(space);
    if (!env)
        return sj_undetermined(a, judgment, sj_expr1(a, "set:signature-unavailable"));
    SjProofState st;
    sj_proof_state_init(&st, a, space, env, false, 0u, false, true);
    for (size_t i = 0u; i < decl_count; i++) {
        Atom *decl = judgment->expr.elems[1u + i];
        names[i] = decl->expr.elems[1];
        types[i] = decl->expr.elems[2];
        proofs[i] = false;
        if (!names[i] || names[i]->kind != ATOM_SYMBOL)
            return sj_refuted(a, judgment, sj_expr2(a, "set:family-name", decl));
        Atom *reserved_member = sj_reserved_definition(a, space, judgment, names[i]);
        if (reserved_member) return reserved_member;
        for (size_t j = 0u; j < i; j++)
            if (atom_eq(names[j], names[i]))
                return sj_refuted(a, judgment, sj_expr2(a, "set:family-duplicate", names[i]));
        if (sj_name_taken(&st, names[i]))
            return sj_refuted(a, judgment, sj_expr2(a, "set:family-already-declared", names[i]));
    }
    /* Each type is formed where the ones before it are declared.  A
     * declaration at a proposition declares a proof of it; nothing after it
     * mentions it. */
    Space view;
    space_init_overlay(&view, &env->overlay);
    for (size_t i = 0u; i < decl_count; i++) {
        Atom *obstruction = sj_declaration_check_obstruction(
            a, &view, judgment, names[i], sj_expr2(a, "type:formed", types[i]),
            &budget, &canonical[i]);
        if (obstruction) {
            Atom *proposition = NULL;
            if (!sj_declaration_check_obstruction(
                    a, &view, judgment, names[i],
                    sj_expr3(a, "type:check", types[i], sj_sym(a, "prop")),
                    &budget, &proposition) && proposition) {
                proofs[i] = true;
                canonical[i] = proposition;
                continue;
            }
        }
        if (!obstruction && !canonical[i])
            obstruction = sj_undetermined(a, judgment,
                                          sj_expr2(a, "set:family-type-fragment", types[i]));
        if (obstruction) {
            space_free(&view);
            return obstruction;
        }
        space_add(&view, sj_expr3(a, ":", names[i], types[i]));
    }
    int known = -1;
    for (size_t k = 0u; k < SJ_FAMILY_MODEL_COUNT && known < 0; k++)
        if (sj_family_matches(a, &st, &view, judgment, names, proofs, canonical, decl_count,
                              equations, eq_count,
                              sj_family_template(&g_family_models[k], &SJ_FAMILY_MODELS[k]),
                              &budget))
            known = (int)k;
    for (size_t k = 0u; k < SJ_FAMILY_NON_MODEL_COUNT && known < 0; k++) {
        if (!sj_family_matches(a, &st, &view, judgment, names, proofs, canonical, decl_count,
                               equations, eq_count,
                               sj_family_template(&g_family_non_models[k],
                                                  &SJ_FAMILY_NON_MODELS[k]),
                               &budget))
            continue;
        space_free(&view);
        Atom *given = NULL;
        Atom *relative = sj_family_hypotheses(a, &SJ_FAMILY_NON_MODELS[k], &given);
        Atom *items[4] = {sj_sym(a, "set:family-no-set-model"),
                          sj_sym(a, SJ_FAMILY_NON_MODELS[k].theorem), relative, given};
        return sj_refuted(a, judgment, atom_expr(a, items, given ? 4u : 3u));
    }
    space_free(&view);
    if (known < 0)
        return sj_undetermined(a, judgment, sj_expr1(a, "set:family-no-known-model"));
    Atom **rules = arena_alloc(a, sizeof(Atom *) * (eq_count ? eq_count : 1u));
    for (size_t e = 0u; e < eq_count; e++) {
        rules[e] = sj_family_rule(a, equations[e], names, decl_count);
        if (!rules[e])
            return sj_undetermined(a, judgment,
                                   sj_expr2(a, "set:family-rule-lowering", equations[e]));
    }
    /* One record per constant, each the whole family and the model it was
     * admitted on, then the declarations and the rules. */
    Atom **prop_items = arena_alloc(a, sizeof(Atom *) * len);
    prop_items[0] = sj_sym(a, "family");
    for (size_t i = 1u; i < len; i++) prop_items[i] = judgment->expr.elems[i];
    Atom *prop = atom_expr(a, prop_items, (CettaExprLen)len);
    const SjKnownFamily *model = &SJ_FAMILY_MODELS[known];
    Atom *given = NULL;
    Atom *relative = sj_family_hypotheses(a, model, &given);
    Atom *evidence_items[4] = {sj_sym(a, "set-model"), sj_sym(a, model->theorem), relative,
                               given};
    Atom *evidence = atom_expr(a, evidence_items, given ? 4u : 3u);
    /* The model's law, with the family's names, as a schema over the set the
     * streams are of: `(pf:known (law A))` is its instance at a set A. */
    Atom *law_record = NULL;
    uint64_t revision = space_revision(space);
    if (model->law) {
        SjFamilyTemplate *tmpl = sj_family_template(&g_family_models[known], model);
        Atom **law_atoms = NULL;
        int law_count = parse_metta_text(model->law_text, a, &law_atoms);
        Atom *schema = law_count == 1 && law_atoms ? law_atoms[0] : NULL;
        free(law_atoms);
        Atom **from = arena_alloc(a, sizeof(Atom *) * decl_count);
        for (size_t i = 0u; tmpl && i < decl_count; i++)
            from[i] = tmpl->decls[i]->expr.elems[1];
        Atom *law_prop = tmpl && schema
            ? sj_family_rename(a, schema, from, names, decl_count) : NULL;
        const char *former = atom_name_cstr(names[0]);
        size_t name_len = strlen(former ? former : "") + strlen(model->law_suffix) + 1u;
        char *law_spelling = arena_alloc(a, name_len);
        snprintf(law_spelling, name_len, "%s%s", former ? former : "", model->law_suffix);
        Atom *law_name = sj_sym(a, law_spelling);
        if (!law_prop)
            return sj_undetermined(a, judgment, sj_expr2(a, "set:family-law", law_name));
        if (sj_name_taken(&st, law_name))
            return sj_refuted(a, judgment, sj_expr2(a, "set:family-already-declared", law_name));
        for (size_t i = 0u; i < decl_count; i++)
            if (atom_eq(names[i], law_name))
                return sj_refuted(a, judgment, sj_expr2(a, "set:family-duplicate", law_name));
        Atom *law_items[4] = {sj_sym(a, "law"), sj_sym(a, model->law), relative,
                              sj_expr2(a, "positions", sj_sym(a, "stail-reach"))};
        law_record = sj_known_record(a, law_name, law_prop, "family-law",
                                     atom_expr(a, law_items, 4u), NULL, revision);
    }
    Atom **published = arena_alloc(a, sizeof(Atom *) * (2u * decl_count + eq_count + 2u));
    size_t n = 0u;
    published[n++] = sj_sym(a, "SetPublish");
    for (size_t i = 0u; i < decl_count; i++)
        published[n++] = sj_known_record(a, names[i], prop, "family", evidence, NULL, revision);
    if (law_record) published[n++] = law_record;
    for (size_t i = 0u; i < decl_count; i++)
        published[n++] = sj_expr3(a, ":", names[i], types[i]);
    for (size_t e = 0u; e < eq_count; e++)
        published[n++] = rules[e];
    return sj_established(a, judgment, atom_expr(a, published, (CettaExprLen)n));
}

/* The level of a sort, in the kernel's spelling of levels: the level of
 * `(u l)` as the kernel reads it, `(LevelAbove n)` for the n-th sort above
 * every written universe (`set`, `class`, `(class n)`), the level of the
 * kernel's own `(Sort level)`, and the levels of the legacy spellings.  A
 * level is carried as a level, whatever it is: a long numeral, an ordinal
 * notation, a level above the written ones.  NULL for anything else and for
 * a level that does not read. */
static Atom *sj_sort_level(Arena *a, Atom *sort) {
    Atom *kernel = sj_is_expr(sort, "Sort", 2u) ? sort
                 : sj_is_expr(sort, "u", 2u) ? sj_universe_to_kernel(a, sort)
                 : sj_sort_above_to_kernel(a, sort);
    if (kernel && sj_is_expr(kernel, "Sort", 2u)) {
        Atom *level = kernel->expr.elems[1];
        /* A level the authored syntax does not read is kept as written by
         * `sj_universe_to_kernel`: it is no level here. */
        if (sj_is_expr(level, "LevelConst", 2u) &&
            !cetta_prime_regular_kernel_level_numeral_v1(level->expr.elems[1]))
            return NULL;
        return level;
    }
    if (atom_is_symbol(sort, "U0") || atom_is_symbol(sort, "u0"))
        return sj_expr2(a, "LevelConst", atom_int(a, 0));
    if (atom_is_symbol(sort, "U1") || atom_is_symbol(sort, "u1"))
        return sj_expr2(a, "LevelConst", atom_int(a, 1));
    return NULL;
}

/* The universe level of a type, in canonical spelling and as a kernel level:
 * the successor of the level of a sort, the level of the sort of a declared
 * type, of a bound type variable, or of a declared family applied to
 * arguments, and the maximum of the levels of an arrow's domain and
 * codomain.  `locals` types the indices, innermost last.  NULL where the
 * level is not known. */
static Atom *sj_type_level(SjProofState *st, Atom *type, Atom **locals,
                           size_t count) {
    Arena *a = st->arena;
    Atom *level = sj_sort_level(a, type);
    if (level) return sj_expr2(a, "LevelSucc", level);
    uint64_t index = 0u;
    if (sj_intrinsic_index(type, &index))
        return index < count && locals[count - 1u - index]
            ? sj_sort_level(a, locals[count - 1u - index]) : NULL;
    if (sj_constant_ref(type)) {
        Atom *declared = sj_constant_type(st, sj_constant_ref(type));
        return declared ? sj_sort_level(a, declared) : NULL;
    }
    if (sj_is_expr(type, "Pi", 3u)) {
        Atom *domain = sj_type_level(st, type->expr.elems[1], locals, count);
        if (!domain) return NULL;
        Atom **inner = arena_alloc(a, sizeof(Atom *) * (count + 1u));
        if (!inner) return NULL;
        for (size_t i = 0u; i < count; i++) inner[i] = locals[i];
        inner[count] = type->expr.elems[1];
        Atom *codomain = sj_type_level(st, type->expr.elems[2], inner, count + 1u);
        return codomain ? sj_expr3(a, "LevelMax", domain, codomain) : NULL;
    }
    Atom *head = NULL;
    size_t argc = sj_spine(type, &head, NULL, 0u);
    if (argc >= 1u && sj_constant_ref(head)) {
        Atom *family = sj_constant_type(st, sj_constant_ref(head));
        for (size_t i = 0u; family && i < argc; i++)
            family = sj_is_expr(family, "Pi", 3u) ? family->expr.elems[2] : NULL;
        return family ? sj_sort_level(a, family) : NULL;
    }
    return NULL;
}

/* Whether a polymorphic head of the signature is declared over a universe
 * at a level variable, `(A : (u $level))`: each kernel occurrence then
 * carries the level of its domain.  A head declared over `class` has no
 * level variable. */
static bool sj_env_poly_head_levelled(SjSetEnv *env, Atom *name) {
    for (size_t i = 0u; i < env->signature_count; i++) {
        Atom *declaration = env->signature[i];
        if (!atom_eq(declaration->expr.elems[1], name)) continue;
        Atom *type = declaration->expr.elems[2];
        return sj_is_expr(type, "->", 3u) &&
               sj_is_expr(type->expr.elems[1], ":", 3u) &&
               sj_is_expr(type->expr.elems[1]->expr.elems[2], "u", 2u);
    }
    return false;
}

/* A compiled set: rule (patterns and rhs over pattern indices, innermost
 * 0) in the kernel's spelling: constants as DeclConst, pattern variables as
 * (PVar j) with j the variable's position.  A polymorphic head stays the
 * source's head applied to its domain, with that domain's universe as the
 * explicit universe argument every kernel occurrence of a polymorphic
 * declaration carries.  This is the one stored form every consumer reads;
 * the proof route specializes it where it selects rules (`sj_rule_specialize`).
 * `locals` types the indices, innermost last. */
static Atom *sj_rule_term_to_kernel(SjProofState *st, Atom *t, size_t nvars,
                                    uint64_t depth, Atom **locals, size_t count) {
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
    if (sj_is_expr(t, "u", 2u)) return sj_universe_to_kernel(a, t);
    Atom *above = sj_sort_above_to_kernel(a, t);
    if (above) return above;
    if (sj_is_leaf(t)) return t;
    Atom *head = NULL;
    Atom *args[4] = {0};
    size_t argc = sj_spine(t, &head, args, 4u);
    const char *poly = sj_constant_named(head, "all") ? "all"
                     : sj_constant_named(head, "eq") ? "eq" : NULL;
    if (poly && argc >= 1u && argc <= 4u) {
        Atom *term = sj_expr2(a, "DeclConst", sj_sym(a, poly));
        if (sj_env_poly_head_levelled(st->env, sj_sym(a, poly))) {
            Atom *level = sj_type_level(st, args[0], locals, count);
            if (!level) return NULL;
            term = atom_expr3(a, sj_sym(a, "DeclConst"), sj_sym(a, poly), level);
        }
        for (size_t i = 0u; i < argc; i++) {
            Atom *arg = sj_rule_term_to_kernel(st, args[i], nvars, depth, locals, count);
            if (!arg) return NULL;
            term = sj_expr3(a, "App", term, arg);
        }
        return term;
    }
    Atom **children = sj_children(a, t);
    if (!children) return NULL;
    for (CettaExprIndex i = 1u; i < t->expr.len; i++) {
        bool under = sj_binds_body(t, i);
        size_t inner = count;
        if (under) {
            locals[count] = sj_is_expr(t, "Lam", 2u) ? NULL : t->expr.elems[1];
            inner = count + 1u;
        }
        children[i] = sj_rule_term_to_kernel(st, t->expr.elems[i], nvars,
                                             under ? depth + 1u : depth, locals, inner);
        if (!children[i]) return NULL;
    }
    return sj_rebuild(a, t, children);
}

static Atom *sj_type_rule_atom(SjProofState *st, Atom *head, size_t arity,
                               Atom **pats, Atom *rhs, size_t nvars,
                               Atom **var_types) {
    Arena *a = st->arena;
    /* Room for the pattern variables and every binder the rule nests. */
    size_t deepest = sj_binder_depth(rhs);
    for (size_t k = 0u; k < arity; k++) {
        size_t depth = sj_binder_depth(pats[k]);
        if (depth > deepest) deepest = depth;
    }
    Atom **locals = sj_locals_for(a, nvars + deepest, NULL, NULL);
    if (!locals) return NULL;
    size_t count = nvars;
    for (size_t j = 0u; j < count; j++) locals[j] = var_types ? var_types[j] : NULL;
    Atom **kp = arena_alloc(a, sizeof(Atom *) * (arity ? arity : 1u));
    for (size_t k = 0u; k < arity; k++) {
        kp[k] = sj_rule_term_to_kernel(st, pats[k], nvars, 0u, locals, count);
        if (!kp[k]) return NULL;
    }
    Atom *krhs = sj_rule_term_to_kernel(st, rhs, nvars, 0u, locals, count);
    if (!krhs) return NULL;
    Atom *items[5] = {sj_sym(a, "type:rule"), head, atom_int(a, (int64_t)arity),
                      atom_expr(a, kp, (CettaExprLen)arity), krhs};
    return atom_expr(a, items, 5u);
}

/* Closing an equation over its dependent pattern telescope lets the
 * existing native typing authority check both endpoints. The definition's
 * type is available in this private context, but none of its new computation
 * rules are: an equation cannot validate itself by executing itself. */
static Atom *sj_definition_equation_obstruction(
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
    Atom *kernel_type = sj_to_kernel_specialized(st, closed_type);
    Atom *kernel_left = sj_to_kernel_specialized(st, left);
    Atom *kernel_right = sj_to_kernel_specialized(st, right);
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
 * variables are aliases of the equation's telescope, never lexical binders.
 * The resulting term acquires typing only in the endpoint checks below. */
/* How many arguments a type takes: the binders of its arrows, authored
 * `(-> A ... B)` or canonical `(Pi A B)`, however nested. */
static size_t sj_type_arity(Atom *type) {
    size_t arity = 0u;
    for (;;) {
        if (type && type->kind == ATOM_EXPR && type->expr.len >= 3u &&
            atom_is_symbol(type->expr.elems[0], "->")) {
            arity += (size_t)type->expr.len - 2u;
            type = type->expr.elems[type->expr.len - 1u];
        } else if (sj_is_expr(type, "Pi", 3u)) {
            arity++;
            type = type->expr.elems[2];
        } else {
            return arity;
        }
    }
}

static Atom *sj_equation_lower(SjProofState *st, Atom *source,
                              Atom **variables, size_t count) {
    Atom *locals[16] = {0};
    for (size_t i = 0u; i < count; i++) locals[i] = variables[count - 1u - i];
    for (;;) {
        size_t capacity = st->env->signature_count + st->env->constant_count;
        Atom **globals = arena_alloc(st->arena, sizeof(*globals) * (capacity ? capacity : 1u));
        size_t *arities = arena_alloc(st->arena, sizeof(*arities) * (capacity ? capacity : 1u));
        size_t global_count = 0u;
        for (size_t i = 0u; i < capacity; i++) {
            bool signature = i < st->env->signature_count;
            Atom *name = signature
                ? st->env->signature[i]->expr.elems[1]
                : st->env->constants[i - st->env->signature_count].name;
            Atom *type = signature
                ? (st->env->signature[i]->expr.len == 3u
                       ? st->env->signature[i]->expr.elems[2] : NULL)
                : st->env->constants[i - st->env->signature_count].type;
            bool duplicate = false;
            for (size_t j = 0u; j < global_count; j++)
                if (atom_eq(globals[j], name)) duplicate = true;
            if (!duplicate) {
                arities[global_count] = sj_type_arity(type);
                globals[global_count++] = name;
            }
        }
        CettaPrimeRegularTermEnvironmentV1 environment = {
            .names = (const Atom *const *)globals, .count = global_count,
            .local_names = (const Atom *const *)locals, .local_count = count,
            .arities = arities,
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
            sj_fail(st, false, sj_expr3(st->arena, "set:define-equation-elaboration", source,
                sj_sym(st->arena, lowered.reason ? lowered.reason : "outside-fragment")));
            return NULL;
        }
    }
}

/* The verdict of a failed binding: refuted only for a refutation the
 * binding found (a pattern variable twice); a storage miss or a shift with
 * no reason leaves the definition undetermined. */
static Atom *sj_equation_bind_failure(Arena *a, SjProofState *st, Atom *judgment) {
    Atom *reason = st->failure ? st->failure : sj_expr1(a, "set:define-pattern-binding");
    if (st->incomplete) return sj_incomplete(a, judgment, reason);
    return st->refuted && st->failure ? sj_refuted(a, judgment, reason)
                                      : sj_undetermined(a, judgment, reason);
}

/* Extend the actual pattern context. Previous arguments and the remaining
 * function type are weakened together; the new domain stays in its prior
 * context. This also handles constructor fields that depend on earlier ones. */
static bool sj_equation_bind(SjProofState *st, Atom *variable, Atom *domain,
                            Atom **variables, Atom **domains, size_t *count,
                            Atom **function_type, Atom **patterns, size_t prior_count) {
    if (*count >= st->equation_variable_cap) {
        sj_fail(st, false, sj_expr1(st->arena, "set:define-variable-storage"));
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

/* Every call of `name` in `t` passes, before the scrutinee, exactly the
 * equation's own variables at those positions. */
static bool sj_recursion_prefix_fixed(Atom *t, Atom *name, size_t arity,
                                      size_t scrut, Atom *lhs) {
    if (!t || t->kind != ATOM_EXPR) return true;
    if (t->expr.len >= arity + 1u && atom_eq(t->expr.elems[0], name)) {
        for (size_t i = 0u; i < scrut; i++)
            if (!atom_eq(t->expr.elems[1u + i], lhs->expr.elems[1u + i]))
                return false;
    }
    for (CettaExprIndex i = 0u; i < t->expr.len; i++)
        if (!sj_recursion_prefix_fixed(t->expr.elems[i], name, arity, scrut, lhs))
            return false;
    return true;
}

/* Move the element at position `1 + scrut` of `items` to position 1, keeping
 * the order of the elements it passes. */
static void sj_scrutinee_first(Atom **items, size_t scrut) {
    Atom *scrutinee = items[1u + scrut];
    for (size_t i = scrut; i > 0u; i--) items[1u + i] = items[i];
    items[1] = scrutinee;
}

/* Every call of `name` with at least `arity` arguments becomes a call of
 * `permuted` with the scrutinee first and the other arguments in order. */
static Atom *sj_scrutinee_first_calls(Arena *a, Atom *t, Atom *name,
                                      Atom *permuted, size_t arity,
                                      size_t scrut) {
    if (!t || t->kind != ATOM_EXPR) return t;
    size_t len = (size_t)t->expr.len;
    Atom **items = arena_alloc(a, sizeof(Atom *) * len);
    for (size_t i = 0u; i < len; i++) {
        items[i] = sj_scrutinee_first_calls(a, t->expr.elems[i], name,
                                            permuted, arity, scrut);
        if (!items[i]) return NULL;
    }
    if (len >= arity + 1u && atom_eq(t->expr.elems[0], name)) {
        sj_scrutinee_first(items, scrut);
        items[0] = permuted;
    }
    return atom_expr(a, items, (CettaExprLen)len);
}

/* A domain written as a named, typed binder: `(x : A)` or `(: x A)`. */
static bool sj_is_typed_binder(Atom *t) {
    if (!t || t->kind != ATOM_EXPR) return false;
    for (CettaExprIndex i = 0u; i < t->expr.len; i++)
        if (atom_is_symbol(t->expr.elems[i], ":")) return true;
    return false;
}

/* The name and the type of a typed binder `(x : A)` or `(: x A)`. */
static bool sj_typed_binder_parts(Atom *t, Atom **name, Atom **domain) {
    if (!t || t->kind != ATOM_EXPR || t->expr.len != 3u) return false;
    if (atom_is_symbol(t->expr.elems[1], ":")) {
        *name = t->expr.elems[0];
        *domain = t->expr.elems[2];
    } else if (atom_is_symbol(t->expr.elems[0], ":")) {
        *name = t->expr.elems[1];
        *domain = t->expr.elems[2];
    } else {
        return false;
    }
    return *name && (*name)->kind == ATOM_SYMBOL;
}

static Atom *sj_set_define(Arena *a, Space *space, Atom *judgment,
                           bool limited, uint64_t steps);

/* First-order matching of an equation pattern against a term: a pattern
 * variable matches anything, the same variable the same term. */
static bool sj_pattern_instance(Atom *pat, Atom *term, Atom **names,
                                Atom **values, size_t *count, size_t cap) {
    if (sj_is_var(pat)) {
        for (size_t i = 0u; i < *count; i++)
            if (atom_eq(names[i], pat)) return atom_eq(values[i], term);
        if (*count >= cap) return false;
        names[*count] = pat;
        values[*count] = term;
        (*count)++;
        return true;
    }
    if (pat->kind == ATOM_EXPR && term->kind == ATOM_EXPR &&
        pat->expr.len == term->expr.len) {
        for (CettaExprIndex i = 0u; i < pat->expr.len; i++)
            if (!sj_pattern_instance(pat->expr.elems[i], term->expr.elems[i],
                                     names, values, count, cap))
                return false;
        return true;
    }
    return atom_eq(pat, term);
}

static size_t sj_atom_size(Atom *t) {
    if (!t || t->kind != ATOM_EXPR) return 1u;
    size_t size = 1u;
    for (CettaExprIndex i = 0u; i < t->expr.len; i++) size += sj_atom_size(t->expr.elems[i]);
    return size;
}

/* A call in `t` of the equation's own head whose first `arity` arguments are
 * an instance of the equation's left side `lhs`.  Any term the equation
 * rewrites then rewrites to a term holding another such call, forever: the
 * call witnesses a loop.  NULL when there is none. */
static Atom *sj_recursion_repeats(Arena *a, Atom *t, Atom *lhs, size_t arity) {
    if (!t || !lhs) return NULL;
    if (arity == 0u && atom_eq(t, lhs)) return t;
    if (t->kind != ATOM_EXPR) return NULL;
    if (lhs->kind == ATOM_EXPR && t->expr.len >= arity + 1u &&
        atom_eq(t->expr.elems[0], lhs->expr.elems[0])) {
        size_t cap = sj_atom_size(lhs);
        Atom **names = arena_alloc(a, sizeof(Atom *) * cap);
        Atom **values = arena_alloc(a, sizeof(Atom *) * cap);
        size_t count = 0u;
        bool instance = names && values;
        for (size_t k = 0u; instance && k < arity; k++)
            instance = sj_pattern_instance(lhs->expr.elems[1u + k], t->expr.elems[1u + k],
                                           names, values, &count, cap);
        if (instance) return t;
    }
    for (CettaExprIndex i = 0u; i < t->expr.len; i++) {
        Atom *found = sj_recursion_repeats(a, t->expr.elems[i], lhs, arity);
        if (found) return found;
    }
    return NULL;
}

/* The head of the pattern at the inspected position of an equation, or NULL
 * when that pattern is a variable. */
static Atom *sj_define_inspected_head(Atom *eq, size_t scrut) {
    Atom *pat = eq->expr.elems[1]->expr.elems[1u + scrut];
    if (sj_is_var(pat)) return NULL;
    if (pat->kind == ATOM_EXPR && pat->expr.len >= 1u) return pat->expr.elems[0];
    return pat;
}

/* True when some equation has a variable at the inspected position. */
static bool sj_define_has_wildcard(Atom *judgment, size_t scrut) {
    for (CettaExprIndex e = 3u; e < judgment->expr.len; e++)
        if (!sj_define_inspected_head(judgment->expr.elems[e], scrut)) return true;
    return false;
}

/* A constructor that no equation names at the inspected position, when
 * every equation names a constructor there; NULL otherwise. */
static Atom *sj_define_missing_constructor(Atom *judgment, size_t scrut,
                                           SjConstructor *ctors,
                                           size_t ctor_count) {
    if (sj_define_has_wildcard(judgment, scrut)) return NULL;
    for (size_t c = 0u; c < ctor_count; c++) {
        bool named = false;
        for (CettaExprIndex e = 3u; e < judgment->expr.len && !named; e++)
            named = atom_eq(sj_define_inspected_head(judgment->expr.elems[e], scrut),
                            ctors[c].name);
        if (!named) return ctors[c].name;
    }
    return NULL;
}


/* A recursive definition whose calls change arguments before the scrutinee
 * is admitted through its scrutinee-first form.  `name~scrutinee-first`
 * takes the scrutinee first and the other arguments in their order, with the
 * same equations; `name` is then defined by the one equation that passes its
 * arguments to that form.  The first is structural recursion whose calls may
 * change every other argument (`DeclaresRecursion` with the scrutinee at
 * position 0, RecursiveDefinitions.lean); the second is a definition by one
 * equation (`DeclaresDefinition`, Definitions.lean).  The authored equations
 * hold by conversion, and the record of `name` keeps them. */
/* Whether `call` occurs in `t` below a nonempty path of constructors of the
 * inductive type whose constructors are `ctors`, each entered at one of its
 * fields of that same type.  `call` is a subterm of `t`, compared by
 * identity. */
static bool sj_call_inside_own_value(Atom *t, Atom *call, SjConstructor *ctors,
                                     size_t ctor_count, bool below) {
    if (t == call) return below;
    if (!t || t->kind != ATOM_EXPR || t->expr.len < 2u) return false;
    for (size_t c = 0u; c < ctor_count; c++) {
        if (!atom_eq(t->expr.elems[0], ctors[c].name) ||
            ctors[c].arity != (size_t)t->expr.len - 1u)
            continue;
        for (size_t i = 0u; i < ctors[c].arity; i++) {
            if (!ctors[c].recursive[i]) continue;
            if (sj_call_inside_own_value(t->expr.elems[1u + i], call, ctors,
                                         ctor_count, true))
                return true;
        }
        return false;
    }
    return false;
}

/* Whether a closed value of `type` is known to exist: a sort, a function
 * type whose result has one, or a datatype with a constructor whose fields
 * all have one, the datatypes being checked not counted (a least value comes
 * first).  Anything else is not known.  Both refutations of a loop need an
 * instance of the equation's variables: over a type with no value, such as
 * a datatype whose only constructor takes one of its own, every equation
 * holds of every function. */
static bool sj_type_has_value(SjProofState *st, Atom *type, Atom **visiting, size_t depth,
                              size_t room) {
    if (!type) return false;
    if (sj_is_sort(type) || atom_is_symbol(type, "prop")) return true;
    if (sj_is_expr(type, "Pi", 3u)) {
        if (sj_mentions_index(type->expr.elems[2], 0u)) return false;
        Atom *codomain = sj_shift(st->arena, type->expr.elems[2], 0u, -1);
        return codomain && sj_type_has_value(st, codomain, visiting, depth, room);
    }
    Atom *name = sj_constant_ref(type);
    if (!name || !sj_inductive_record(st, name) || depth == room) return false;
    for (size_t i = 0u; i < depth; i++)
        if (atom_eq(visiting[i], name)) return false;
    visiting[depth] = name;
    SjConstructor *ctors = NULL;
    size_t ctor_count = 0u;
    Atom *principle = NULL;
    Atom *saved_failure = st->failure;
    bool saved_refuted = st->refuted, saved_incomplete = st->incomplete;
    bool found = sj_datatype_constructors(st, name, &ctors, &ctor_count, &principle);
    bool value = false;
    for (size_t c = 0u; found && !value && c < ctor_count; c++) {
        Atom *ctype = sj_constant_type(st, ctors[c].name);
        bool fields = ctype != NULL;
        for (size_t i = 0u; fields && i < ctors[c].arity; i++) {
            fields = sj_is_expr(ctype, "Pi", 3u) &&
                     !sj_mentions_index(ctype->expr.elems[2], 0u) &&
                     sj_type_has_value(st, ctype->expr.elems[1], visiting, depth + 1u, room);
            if (fields) ctype = sj_shift(st->arena, ctype->expr.elems[2], 0u, -1);
        }
        value = fields;
    }
    st->failure = saved_failure;
    st->refuted = saved_refuted;
    st->incomplete = saved_incomplete;
    return value;
}

/* Whether every variable of an equation has a type with a known value. */
static bool sj_variables_have_values(SjProofState *st, Atom **types, size_t count) {
    for (size_t j = 0u; j < count; j++) {
        Atom **visiting = arena_alloc(st->arena, sizeof(Atom *) * 64u);
        if (!visiting || !sj_type_has_value(st, types[j], visiting, 0u, 64u)) return false;
    }
    return true;
}

/* Whether the equation is the one of `fromList` (Trinity/Stream/
 * AdmittedBySetSolution.lean:588), up to names: one argument of a type of
 * numbers (a constant and a successor), `f n = c n (f (s n))` with `s` the
 * successor and `c` the constructor of a type of lists of those numbers (a
 * constant, and `c` of a number and a list). */
static bool sj_loop_is_from_list(SjProofState *st, Atom *lhs, Atom *rhs, Atom *list_type,
                                 Atom *number_type) {
    if (!lhs || lhs->kind != ATOM_EXPR || lhs->expr.len != 2u || !sj_is_var(lhs->expr.elems[1]))
        return false;
    Atom *n = lhs->expr.elems[1];
    if (!rhs || rhs->kind != ATOM_EXPR || rhs->expr.len != 3u || !atom_eq(rhs->expr.elems[1], n))
        return false;
    Atom *call = rhs->expr.elems[2];
    if (!call || call->kind != ATOM_EXPR || call->expr.len != 2u ||
        !atom_eq(call->expr.elems[0], lhs->expr.elems[0]))
        return false;
    Atom *step = call->expr.elems[1];
    if (!step || step->kind != ATOM_EXPR || step->expr.len != 2u || !atom_eq(step->expr.elems[1], n))
        return false;
    SjConstructor *lists = NULL, *numbers = NULL;
    size_t list_count = 0u, number_count = 0u;
    Atom *principle = NULL;
    Atom *saved_failure = st->failure;
    bool saved_refuted = st->refuted, saved_incomplete = st->incomplete;
    bool shaped =
        number_type && list_type &&
        sj_datatype_constructors(st, list_type, &lists, &list_count, &principle) &&
        sj_datatype_constructors(st, number_type, &numbers, &number_count, &principle) &&
        list_count == 2u && number_count == 2u;
    st->failure = saved_failure;
    st->refuted = saved_refuted;
    st->incomplete = saved_incomplete;
    if (!shaped) return false;
    bool cons = false, nil = false, suc = false, zero = false;
    for (size_t c = 0u; c < 2u; c++) {
        nil = nil || lists[c].arity == 0u;
        cons = cons || (lists[c].arity == 2u && atom_eq(lists[c].name, rhs->expr.elems[0]) &&
                        !lists[c].recursive[0] && lists[c].recursive[1]);
        zero = zero || numbers[c].arity == 0u;
        suc = suc || (numbers[c].arity == 1u && atom_eq(numbers[c].name, step->expr.elems[0]) &&
                      numbers[c].recursive[0]);
    }
    if (!(cons && nil && suc && zero)) return false;
    Atom *cons_type = sj_constant_type(st, rhs->expr.elems[0]);
    return sj_is_expr(cons_type, "Pi", 3u) && atom_eq(cons_type->expr.elems[1], number_type);
}

/* A loop of an equation, its call `loop` repeating the left side, that no
 * set satisfies: the result type is an inductive type and the call stands
 * inside a constructor of that type, at a field of the type.  Every value of
 * the left side would then contain the value of the call as a proper part,
 * whose own value contains the next one, and so on: a value of an inductive
 * type, whose parts are finitely deep, cannot.  The call may be at any
 * instance of the left side.  The type's name, or NULL.  The refutation is
 * TowerInterpretation.DatatypeBound.containsItself_no_setModel
 * (TowerInterpretation/SetDefinitionsByBound.lean:603), given that the
 * variables stay typed along the instances and the constructors on the path
 * fit (`stable`, `fitting`), which the typing of the equation gives and this
 * check, made before it, takes as given. */
static Atom *sj_loop_without_solution(SjProofState *st, Atom *rhs, Atom *loop,
                                      Atom *result_type) {
    if (!result_type || result_type->kind != ATOM_SYMBOL ||
        !sj_inductive_record(st, result_type))
        return NULL;
    SjConstructor *ctors = NULL;
    size_t ctor_count = 0u;
    Atom *principle = NULL;
    Atom *saved_failure = st->failure;
    bool saved_refuted = st->refuted, saved_incomplete = st->incomplete;
    bool found = sj_datatype_constructors(st, result_type, &ctors, &ctor_count,
                                          &principle);
    st->failure = saved_failure;
    st->refuted = saved_refuted;
    st->incomplete = saved_incomplete;
    if (!found) return NULL;
    return sj_call_inside_own_value(rhs, loop, ctors, ctor_count, false)
        ? result_type : NULL;
}

/* A loop that makes a proposition equal to its own negation: the right side
 * is `(imp CALL Q)` with CALL the left side itself and Q a closed
 * proposition convertible with Falsum, so the equation says that the left
 * side holds exactly when it does not.  No truth value satisfies that
 * (Curry): it proves Falsum.  A call at another instance of the left side
 * is not this shape: `alt n = (alt (n + 1) → ⊥)` holds of the function true
 * at the even numbers and false at the odd ones.  The refutation is
 * TowerInterpretation.equals_own_negation_no_setModel
 * (TowerInterpretation/SetDefinitions.lean:376, `OwnNegation.right_eq`),
 * read with propositions as truth codes and `imp` as implication. */
static bool sj_loop_is_own_negation(SjProofState *st, Atom *lhs, Atom *rhs, Atom *loop) {
    if (!rhs || rhs->kind != ATOM_EXPR || rhs->expr.len != 3u ||
        !atom_is_symbol(rhs->expr.elems[0], "imp") || rhs->expr.elems[1] != loop ||
        !atom_eq(loop, lhs))
        return false;
    Atom *saved_failure = st->failure;
    bool saved_refuted = st->refuted, saved_incomplete = st->incomplete;
    Atom *prop = sj_sym(st->arena, "prop");
    Atom *consequent = sj_elaborate_closed(st, rhs->expr.elems[2], prop);
    Atom *falsum = consequent
        ? sj_elaborate_closed(st, sj_expr3(st->arena, "all", prop,
                                           sj_expr3(st->arena, "lam", sj_sym(st->arena, "p"),
                                                    sj_sym(st->arena, "p"))), prop)
        : NULL;
    bool negation = falsum && sj_convertible(st, consequent, falsum);
    st->failure = saved_failure;
    st->refuted = saved_refuted;
    st->incomplete = saved_incomplete;
    return negation;
}

/* One equation of `name` at `canonical_type`, checked in the context of its
 * patterns: the patterns against the constructors of the scrutinee's type,
 * the recursion guard, and both sides typed at the remaining type. The
 * compiled rule is left in `*rule` and, when the equation has a constructor
 * pattern, its termination evidence `(constructor (field ...))` in
 * `*descent`; an obstruction is returned as the verdict. */
static Atom *sj_definition_equation_check(Arena *a, SjProofState *st, Atom *judgment,
                                          Atom *name, Atom *canonical_type, size_t arity,
                                          size_t scrut, Atom *eq, SjConstructor *ctors,
                                          size_t ctor_count, bool *covered, Atom **rule,
                                          Atom **descent, Atom **types_out) {
    Atom *lhs = eq->expr.elems[1];
    Atom *rhs = eq->expr.elems[2];
    /* Storage sized by the equation: at most one variable per argument or
     * per field of the one constructor pattern. */
    size_t var_cap = arity;
    for (size_t k = 0u; k < arity; k++) {
        Atom *pat = lhs->expr.elems[1u + k];
        if (pat->kind == ATOM_EXPR) var_cap += (size_t)pat->expr.len;
    }
    if (var_cap == 0u) var_cap = 1u;
    Atom **vars = arena_alloc(a, sizeof(Atom *) * var_cap);
    Atom **vtypes = arena_alloc(a, sizeof(Atom *) * var_cap);
    size_t nvars = 0u;
    Atom **rec_vars = arena_alloc(a, sizeof(Atom *) * var_cap);
    size_t *rec_fields = arena_alloc(a, sizeof(size_t) * var_cap);
    size_t rec_count = 0u;
    *descent = NULL;
    SjConstructor *ctor = NULL;
    Atom **pats = arena_alloc(a, sizeof(Atom *) * (arity ? arity : 1u));
    if (!vars || !vtypes || !rec_vars || !rec_fields || !pats)
        return sj_incomplete(a, judgment, sj_expr1(a, "set:define-storage"));
    st->equation_variable_cap = var_cap;
    Atom *remaining_type = canonical_type;
    for (size_t k = 0u; k < arity; k++) {
        Atom *pat = lhs->expr.elems[1u + k];
        if (!sj_is_expr(remaining_type, "Pi", 3u))
            return sj_undetermined(a, judgment, sj_expr2(a, "set:define-dependent-domain", pat));
        if (sj_is_var(pat)) {
            if (!sj_equation_bind(st, pat, remaining_type->expr.elems[1],
                                vars, vtypes, &nvars, &remaining_type, pats, k))
                return sj_equation_bind_failure(a, st, judgment);
            pats[k] = sj_expr2(a, "idx", atom_int(a, 0));
            remaining_type = sj_subst(a, remaining_type->expr.elems[2], 0u, pats[k]);
            continue;
        }
        Atom *chead = pat->kind == ATOM_SYMBOL ? pat
                    : (pat->kind == ATOM_EXPR && pat->expr.len >= 2u ? pat->expr.elems[0] : NULL);
        size_t cargs = pat->kind == ATOM_SYMBOL ? 0u : (pat->kind == ATOM_EXPR ? pat->expr.len - 1u : 0u);
        for (size_t c = 0u; c < ctor_count && !ctor; c++)
            if (atom_eq(ctors[c].name, chead) && ctors[c].arity == cargs) ctor = &ctors[c];
        /* A pattern that is not a constructor of the argument's type is
         * not a definition by cases, and no route of the draft reads it;
         * it does not say that no set solves the equations: `pred (add
         * zero n) = n` and `pred zero = zero` hold of the identity. */
        if (!ctor)
            return sj_undetermined(a, judgment, sj_expr2(a, "set:define-not-a-constructor", pat));
        size_t ci = (size_t)(ctor - ctors);
        /* Two equations for one constructor are legal, the first matching
         * one computing; case trees with overlap are not admitted yet. */
        if (covered[ci])
            return sj_undetermined(a, judgment, sj_expr2(a, "set:define-overlap", pat));
        covered[ci] = true;
        Atom *ctype = sj_constant_type(st, ctor->name);
        if (!ctype)
            return sj_undetermined(a, judgment, sj_expr2(a, "set:define-constructor-type", chead));
        ctype = sj_shift(a, ctype, 0u, (int64_t)nvars);
        Atom *constructor_term = sj_canonical_constant(a, ctor->name);
        for (size_t i = 0u; i < cargs; i++) {
            Atom *v = pat->expr.elems[1u + i];
            /* A nested pattern is legal; case trees are not admitted yet. */
            if (!sj_is_var(v))
                return sj_undetermined(a, judgment, sj_expr3(a, "set:define-nested-pattern", pat,
                                                             sj_expr2(a, "admit-by", sj_sym(a, "bound"))));
            if (!sj_is_expr(ctype, "Pi", 3u))
                return sj_undetermined(a, judgment, sj_expr2(a, "set:define-constructor-type", chead));
            if (!sj_equation_bind(st, v, ctype->expr.elems[1], vars, vtypes,
                                &nvars, &remaining_type, pats, k))
                return sj_equation_bind_failure(a, st, judgment);
            ctype = sj_shift(a, ctype, 0u, 1);
            constructor_term = sj_shift(a, constructor_term, 0u, 1);
            Atom *argument = sj_expr2(a, "idx", atom_int(a, 0));
            constructor_term = sj_expr3(a, "App", constructor_term, argument);
            ctype = sj_subst(a, ctype->expr.elems[2], 0u, argument);
            if (ctor->recursive[i]) {
                rec_fields[rec_count] = i;
                rec_vars[rec_count++] = v;
            }
        }
        pats[k] = constructor_term;
        remaining_type = sj_subst(a, remaining_type->expr.elems[2], 0u, pats[k]);
    }
    /* A recursion that is not structural is not admitted here, and is
     * refuted only when the equations have no solution in sets.  A call that
     * repeats the equation's own left side never stops when run; it has no
     * solution when its value would contain itself (sj_loop_without_solution)
     * or when a proposition would equal its own negation
     * (sj_loop_is_own_negation), and may have one otherwise, as a stream or a function of positions
     * does: a set solution or a coinductive reading may admit it.  Any other
     * call off the structure may stop by a measure: a bound on the calls, a
     * set solution or accessibility may admit it. */
    if (!sj_recursion_guarded(rhs, name, arity, scrut, rec_vars, rec_count)) {
        Atom *loop = sj_recursion_repeats(a, rhs, lhs, arity);
        /* Both refutations need an instance of the equation's variables, and
         * a call of exactly the left side's arity: the theorems speak of the
         * call as an instance of the left side, and a call with more
         * arguments is the left side's value applied further. */
        bool exact = loop && (arity == 0u
            ? loop->kind != ATOM_EXPR
            : loop->kind == ATOM_EXPR && (size_t)loop->expr.len == arity + 1u);
        bool instances = exact && sj_variables_have_values(st, vtypes, nvars);
        if (loop && instances) {
            Atom *contains = sj_loop_without_solution(st, rhs, loop, remaining_type);
            if (contains) {
                Atom *number_type = arity == 1u && sj_is_expr(canonical_type, "Pi", 3u)
                    ? sj_constant_ref(canonical_type->expr.elems[1]) : NULL;
                bool from_list = sj_loop_is_from_list(st, lhs, rhs, contains, number_type);
                Atom *reason_items[5] = {
                    sj_sym(a, "contains-itself"), contains,
                    sj_sym(a, "TowerInterpretation.DatatypeBound.containsItself_no_setModel"),
                    sj_expr3(a, "given", sj_sym(a, "stability"), sj_sym(a, "path-fits")),
                    sj_sym(a, "Trinity.Stream.fromList_no_setModel")};
                Atom *items[4] = {sj_sym(a, "set:define-no-set-solution"), eq, loop,
                                  atom_expr(a, reason_items, from_list ? 5u : 4u)};
                return sj_refuted(a, judgment, atom_expr(a, items, 4u));
            }
            if (sj_loop_is_own_negation(st, lhs, rhs, loop)) {
                Atom *reason_items[3] = {
                    sj_sym(a, "equals-own-negation"),
                    sj_sym(a, "TowerInterpretation.equals_own_negation_no_setModel"),
                    sj_expr3(a, "relative", sj_sym(a, "truthCode"), sj_sym(a, "impReads"))};
                Atom *items[4] = {sj_sym(a, "set:define-no-set-solution"), eq, loop,
                                  atom_expr(a, reason_items, 3u)};
                return sj_refuted(a, judgment, atom_expr(a, items, 4u));
            }
        }
        if (loop) {
            Atom *items[4] = {sj_sym(a, "set:define-not-terminating"), eq, loop,
                              sj_expr3(a, "admit-by", sj_sym(a, "set-solution"),
                                       sj_sym(a, "coinductive"))};
            return sj_undetermined(a, judgment, atom_expr(a, items, 4u));
        }
        Atom *routes[4] = {sj_sym(a, "admit-by"), sj_sym(a, "bound"),
                           sj_sym(a, "set-solution"), sj_sym(a, "accessibility")};
        return sj_undetermined(a, judgment,
                               sj_expr3(a, "set:define-not-structural", eq,
                                        atom_expr(a, routes, 4u)));
    }
    if (ctor) {
        size_t calls = sj_recursion_call_count(rhs, name);
        Atom **fields = arena_alloc(a, sizeof(Atom *) * (calls ? calls : 1u));
        size_t n = 0u;
        sj_recursion_descents(a, rhs, name, scrut, rec_vars, rec_fields, rec_count,
                              fields, &n);
        *descent = atom_expr2(a, ctor->name, atom_expr(a, fields, (CettaExprLen)n));
    }
    Atom *crhs = sj_equation_lower(st, rhs, vars, nvars);
    if (crhs) {
        Atom **locals = sj_locals_for(a, nvars, crhs, NULL);
        if (!locals) return sj_incomplete(a, judgment, sj_expr1(a, "set:define-storage"));
        for (size_t j = 0u; j < nvars; j++) locals[j] = vtypes[j];
        crhs = sj_annotate(st, crhs, locals, nvars);
    }
    if (!crhs)
        return st->incomplete ? sj_incomplete(a, judgment, st->failure)
                          : st->refuted ? sj_refuted(a, judgment, st->failure)
                          : sj_undetermined(a, judgment, st->failure);
    Atom *cleft = sj_canonical_constant(a, name);
    for (size_t k = 0u; k < arity; k++)
        cleft = sj_expr3(a, "App", cleft, pats[k]);
    Atom *obstruction = sj_definition_equation_obstruction(
        st, judgment, vtypes, nvars,
        cleft, crhs, remaining_type, &st->budget);
    if (obstruction)
        return obstruction;
    Atom *pat_list = atom_expr(a, pats, (CettaExprLen)arity);
    Atom *rule_items[3] = {pat_list, crhs, atom_int(a, (int64_t)nvars)};
    *rule = atom_expr(a, rule_items, 3u);
    /* The pattern variables' types, outermost first, for lowering. */
    if (types_out)
        for (size_t j = 0u; j < nvars; j++) types_out[j] = vtypes[j];
    return NULL;
}

/* What an admitted definition rests on in Lean.  The record's evidence ends
 * with `(lean name ...)`: the theorem that gives the package with the
 * definition a set model, and the lemmas that supply its hypotheses from
 * the checks made here, named in full below
 * Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId (C strings: read as
 * MeTTa, a dotted name would become a colon name).
 *   explicit_setModel             TowerInterpretation/SetExplicitDefinitions.lean:60
 *   recursion_setModel            TowerInterpretation/SetRecursion.lean:515
 *   definition_setModel_of_bound  TowerInterpretation/SetDefinitionsByBound.lean:260
 *   DatatypeBound.reading_structOrder_wf      the same file:358
 *   DatatypeBound.apart_of_linear_patterns    the same file:530
 *   DatatypeBound.boundCovers_of_one_level    the same file:701
 *   definition_setModel_of_evidence  TowerInterpretation/SetDefinitionsBySolution.lean:146
 *   equationHolds_iff_atPosition     the same file:172
 * Where the definition has a shape the theorem does not state, the record
 * says what the theorem covers: `(lean-covers one-argument)` for a
 * definition of several arguments (recursion and the bound state a function
 * of one argument), `(lean-covers one-level)` for patterns below one
 * constructor (recursion states one equation per constructor over
 * variables).  The verdict does not depend on it.  None of these theorems
 * says that the equations, run as rules, stop at open terms. */
#define SJ_LEAN_EXPLICIT "TowerInterpretation.explicit_setModel"
#define SJ_LEAN_RECURSION "TowerInterpretation.recursion_setModel"
#define SJ_LEAN_BOUND "TowerInterpretation.definition_setModel_of_bound"
#define SJ_LEAN_BOUND_ORDER "TowerInterpretation.DatatypeBound.reading_structOrder_wf"
#define SJ_LEAN_BOUND_APART "TowerInterpretation.DatatypeBound.apart_of_linear_patterns"
#define SJ_LEAN_BOUND_COVERS "TowerInterpretation.DatatypeBound.boundCovers_of_one_level"
#define SJ_LEAN_EVIDENCE "TowerInterpretation.definition_setModel_of_evidence"
#define SJ_LEAN_AT_POSITION "TowerInterpretation.equationHolds_iff_atPosition"

static Atom *sj_lean_basis(Arena *a, const char *const *names, size_t count,
                           bool one_argument, bool one_level) {
    Atom **items = arena_alloc(a, sizeof(Atom *) * (count + 3u));
    size_t n = 0u;
    items[n++] = sj_sym(a, "lean");
    for (size_t i = 0u; i < count; i++) items[n++] = sj_sym(a, names[i]);
    if (one_argument) items[n++] = sj_expr2(a, "lean-covers", sj_sym(a, "one-argument"));
    if (one_level) items[n++] = sj_expr2(a, "lean-covers", sj_sym(a, "one-level"));
    return atom_expr(a, items, (CettaExprLen)n);
}

static Atom *sj_definition_evidence(Arena *a, Atom *compiled, Atom *termination,
                                    Atom *basis) {
    Atom *items[4] = {sj_sym(a, "definition-evidence"), compiled, termination, basis};
    return atom_expr(a, items, 4u);
}

/* Whether every compiled rule's pattern at `position` is one constructor
 * applied to variables. */
static bool sj_rules_one_level(Atom *compiled, size_t position) {
    for (CettaExprIndex r = 2u; r < compiled->expr.len; r++) {
        Atom *pats = compiled->expr.elems[r]->expr.elems[0];
        if (!pats || pats->kind != ATOM_EXPR || position >= (size_t)pats->expr.len)
            return false;
        Atom *pat = pats->expr.elems[position];
        while (sj_is_expr(pat, "App", 3u)) {
            if (!sj_intrinsic_index(pat->expr.elems[2], NULL)) return false;
            pat = pat->expr.elems[1];
        }
        if (!sj_constant_ref(pat)) return false;
    }
    return true;
}

static Atom *sj_set_define_scrutinee_first(Arena *a, Space *space,
                                           Atom *judgment, SjProofState *st,
                                           Atom *canonical_type, size_t arity,
                                           size_t scrut, Atom *scrut_type,
                                           SjConstructor *ctors, size_t ctor_count,
                                           bool limited, uint64_t steps) {
    Atom *name = judgment->expr.elems[1];
    Atom *type = judgment->expr.elems[2];
    /* The scrutinee's entry moves to the front of the written arrow. Written
     * binders name their dependencies, so the later entries and the result
     * keep their meaning when the scrutinee's type mentions no earlier binder
     * and its name shadows none; the moved telescope is checked for formation
     * below. */
    if (!sj_is_expr(type, "->", type->kind == ATOM_EXPR ? type->expr.len : 0u) ||
        type->expr.len < arity + 2u)
        return sj_undetermined(a, judgment,
                               sj_expr2(a, "set:define-scrutinee-first-type", type));
    {
        Atom *scrut_item = type->expr.elems[1u + scrut];
        Atom *scrut_name = NULL;
        Atom *scrut_domain = scrut_item;
        if (sj_is_typed_binder(scrut_item) &&
            !sj_typed_binder_parts(scrut_item, &scrut_name, &scrut_domain))
            return sj_undetermined(a, judgment,
                                   sj_expr2(a, "set:define-scrutinee-first-type", type));
        for (size_t i = 0u; i < scrut; i++) {
            Atom *item = type->expr.elems[1u + i];
            if (!sj_is_typed_binder(item)) continue;
            Atom *prefix_name = NULL;
            Atom *prefix_domain = NULL;
            if (!sj_typed_binder_parts(item, &prefix_name, &prefix_domain) ||
                sj_mentions_symbol(scrut_domain, prefix_name) ||
                (scrut_name && atom_eq(scrut_name, prefix_name)))
                return sj_undetermined(a, judgment,
                                       sj_expr2(a, "set:define-scrutinee-first-type", type));
        }
    }
    /* The authored equations, checked in their own contexts before the
     * transformation: the record of `name` carries them, and the check of
     * the transformed equations does not stand in for theirs. `name` has
     * its declared type while they are checked. */
    size_t authored_count = (size_t)judgment->expr.len - 3u;
    Atom **authored_descents = arena_alloc(
        a, sizeof(Atom *) * (authored_count ? authored_count : 1u));
    bool *covered = arena_alloc(a, sizeof(bool) * (ctor_count ? ctor_count : 1u));
    if (!authored_descents || !covered)
        return sj_incomplete(a, judgment, sj_expr1(a, "set:define-storage"));
    memset(authored_descents, 0, sizeof(Atom *) * (authored_count ? authored_count : 1u));
    memset(covered, 0, sizeof(bool) * (ctor_count ? ctor_count : 1u));
    {
        size_t saved_constants = st->env->constant_count;
        sj_constant_cache_add(st->env, name, canonical_type);
        sj_declare_mentioned(st, canonical_type, NULL);
        sj_note_instance(st, name, canonical_type);
        for (CettaExprIndex e = 3u; e < judgment->expr.len; e++) {
            Atom *rule = NULL;
            Atom *verdict = sj_definition_equation_check(
                a, st, judgment, name, canonical_type, arity, scrut,
                judgment->expr.elems[e], ctors, ctor_count, covered, &rule,
                &authored_descents[e - 3u], NULL);
            if (verdict) return st->env->constant_count = saved_constants, verdict;
        }
        st->env->constant_count = saved_constants;
    }
    const char *base = atom_name_cstr(name);
    size_t capacity = strlen(base) + 48u;
    char *spelling = arena_alloc(a, capacity);
    snprintf(spelling, capacity, "%s~scrutinee-first", base);
    Atom *permuted = sj_sym(a, spelling);
    for (size_t suffix = 1u;
         sj_constant_type(st, permuted) || sj_definition_of(permuted); suffix++) {
        snprintf(spelling, capacity, "%s~scrutinee-first~%zu", base, suffix);
        permuted = sj_sym(a, spelling);
    }
    Atom **domains = arena_alloc(a, sizeof(Atom *) * (size_t)type->expr.len);
    for (CettaExprIndex i = 0u; i < type->expr.len; i++)
        domains[i] = type->expr.elems[i];
    sj_scrutinee_first(domains, scrut);
    Atom *permuted_type = atom_expr(a, domains, type->expr.len);
    CettaExprLen len = judgment->expr.len;
    Atom **items = arena_alloc(a, sizeof(Atom *) * (size_t)len);
    items[0] = judgment->expr.elems[0];
    items[1] = permuted;
    items[2] = permuted_type;
    for (CettaExprIndex e = 3u; e < len; e++) {
        Atom *eq = judgment->expr.elems[e];
        items[e] = sj_expr3(a, "=",
            sj_scrutinee_first_calls(a, eq->expr.elems[1], name, permuted, arity, scrut),
            sj_scrutinee_first_calls(a, eq->expr.elems[2], name, permuted, arity, scrut));
        if (!items[e]->expr.elems[1] || !items[e]->expr.elems[2])
            return sj_undetermined(a, judgment, sj_expr1(a, "set:define-scrutinee-first"));
    }
    Atom *first = sj_set_define(a, space, atom_expr(a, items, len), limited, steps);
    if (!first || first->kind != ATOM_EXPR || first->expr.len != 4u)
        return sj_undetermined(a, judgment, sj_expr1(a, "set:define-scrutinee-first"));
    if (!atom_is_symbol(first->expr.elems[1], "Established")) {
        Atom *verdict[4] = {first->expr.elems[0], first->expr.elems[1], judgment,
                            first->expr.elems[3]};
        return atom_expr(a, verdict, 4u);
    }
    /* `name`, defined by passing its arguments to the scrutinee-first form. */
    Atom **arguments = arena_alloc(a, sizeof(Atom *) * (arity + 1u));
    arguments[0] = name;
    for (size_t i = 0u; i < arity; i++)
        arguments[1u + i] = atom_var_with_id(a, "argument", fresh_var_id());
    Atom *left = atom_expr(a, arguments, (CettaExprLen)(arity + 1u));
    Atom *right = sj_scrutinee_first_calls(a, left, name, permuted, arity, scrut);
    Atom *definition[4] = {judgment->expr.elems[0], name, type,
                           sj_expr3(a, "=", left, right)};
    Atom *second_judgment = atom_expr(a, definition, 4u);
    Atom *canonical = NULL;
    Atom *obstruction = sj_declaration_check_obstruction(
        a, &st->env->overlay, second_judgment, permuted_type,
        sj_expr2(a, "type:formed", permuted_type), &st->budget, &canonical);
    if (obstruction || !canonical)
        return sj_undetermined(a, judgment,
                               sj_expr2(a, "set:define-scrutinee-first-type", permuted_type));
    size_t saved_constants = st->env->constant_count;
    sj_constant_cache_add(st->env, permuted, canonical);
    Atom *second = sj_set_define(a, space, second_judgment, limited, steps);
    st->env->constant_count = saved_constants;
    if (!second || second->kind != ATOM_EXPR || second->expr.len != 4u)
        return sj_undetermined(a, judgment, sj_expr1(a, "set:define-scrutinee-first"));
    if (!atom_is_symbol(second->expr.elems[1], "Established")) {
        Atom *verdict[4] = {second->expr.elems[0], second->expr.elems[1], judgment,
                            second->expr.elems[3]};
        return atom_expr(a, verdict, 4u);
    }
    /* Publish `name` first, with the authored equations in its record, then
     * the scrutinee-first form. */
    Atom *published_first = first->expr.elems[3];
    Atom *published_second = second->expr.elems[3];
    if (!sj_is_expr(published_first, "SetPublish",
                    published_first->kind == ATOM_EXPR ? published_first->expr.len : 0u) ||
        !sj_is_expr(published_second, "SetPublish",
                    published_second->kind == ATOM_EXPR ? published_second->expr.len : 0u))
        return sj_undetermined(a, judgment, sj_expr1(a, "set:define-scrutinee-first"));
    Atom *record = published_second->expr.elems[1];
    Atom **prop_items = arena_alloc(a, sizeof(Atom *) * ((size_t)len - 1u));
    prop_items[0] = sj_sym(a, "define");
    prop_items[1] = type;
    for (CettaExprIndex e = 3u; e < len; e++) prop_items[e - 1u] = judgment->expr.elems[e];
    Atom *record_items[SJ_KNOWN_LEN];
    for (size_t i = 0u; i < SJ_KNOWN_LEN; i++) record_items[i] = record->expr.elems[i];
    record_items[SJ_KNOWN_PROP] = atom_expr(a, prop_items, len - 1u);
    /* `name` computes through the scrutinee-first form: it depends on that
     * definition and on everything that definition depends on. */
    {
        Atom *permuted_record = published_first->expr.elems[1];
        Atom *own = record->expr.elems[SJ_KNOWN_DEPENDS];
        Atom *inner = sj_is_expr(permuted_record, "set:known", SJ_KNOWN_LEN)
            ? permuted_record->expr.elems[SJ_KNOWN_DEPENDS] : NULL;
        size_t own_count = own && own->kind == ATOM_EXPR ? own->expr.len : 0u;
        size_t inner_count = inner && inner->kind == ATOM_EXPR ? inner->expr.len : 0u;
        Atom **pins = arena_alloc(a, sizeof(Atom *) * (own_count + inner_count + 1u));
        size_t pin_count = 0u;
        for (size_t i = 0u; i < own_count; i++) pins[pin_count++] = own->expr.elems[i];
        if (sj_is_expr(permuted_record, "set:known", SJ_KNOWN_LEN))
            pins[pin_count++] = atom_expr2(a, permuted_record->expr.elems[SJ_KNOWN_NAME],
                                           permuted_record->expr.elems[SJ_KNOWN_PROP]);
        for (size_t i = 0u; i < inner_count; i++) {
            Atom *pin = inner->expr.elems[i];
            bool seen = false;
            for (size_t k = 0u; k < pin_count && !seen; k++)
                seen = pins[k] && pins[k]->kind == ATOM_EXPR && pins[k]->expr.len == 2u &&
                       pin && pin->kind == ATOM_EXPR && pin->expr.len == 2u &&
                       atom_eq(pins[k]->expr.elems[0], pin->expr.elems[0]);
            if (!seen) pins[pin_count++] = pin;
        }
        record_items[SJ_KNOWN_DEPENDS] = atom_expr(a, pins, (CettaExprLen)pin_count);
    }
    /* The rules of `name` are the one equation passing its arguments to the
     * scrutinee-first form; its termination evidence is that of the authored
     * equations, at the authored scrutinee, through that form. */
    Atom *wrapper = record->expr.elems[SJ_KNOWN_PROOF];
    if (!wrapper || wrapper->kind != ATOM_EXPR || wrapper->expr.len < 3u ||
        !atom_is_symbol(wrapper->expr.elems[0], "definition-evidence"))
        return sj_undetermined(a, judgment, sj_expr1(a, "set:define-scrutinee-first"));
    /* The passing equation is an explicit definition over the form, whose
     * own record carries the recursion it rests on. */
    const char *passing_basis[1] = {SJ_LEAN_EXPLICIT};
    record_items[SJ_KNOWN_PROOF] = sj_definition_evidence(
        a, wrapper->expr.elems[1],
        sj_expr3(a, "scrutinee-first", permuted,
                 sj_structural_evidence(a, scrut, scrut_type, authored_descents,
                                        (size_t)len - 3u)),
        sj_lean_basis(a, passing_basis, 1u, false, false));
    size_t count = (size_t)published_second->expr.len + (size_t)published_first->expr.len - 1u;
    Atom **published = arena_alloc(a, sizeof(Atom *) * count);
    size_t n = 0u;
    published[n++] = published_second->expr.elems[0];
    published[n++] = atom_expr(a, record_items, SJ_KNOWN_LEN);
    for (CettaExprIndex i = 2u; i < published_second->expr.len; i++)
        published[n++] = published_second->expr.elems[i];
    for (CettaExprIndex i = 1u; i < published_first->expr.len; i++)
        published[n++] = published_first->expr.elems[i];
    return sj_established(a, judgment, atom_expr(a, published, (CettaExprLen)n));
}

/* ------------------------------------------------------------------------ */
/* A definition admitted by a bound on its calls (route 1 of a recursion   */
/* that is not structural):                                                  */
/*                                                                            */
/*   (set:define name type (by bound T B proof ...) (= lhs rhs) ...)          */
/*                                                                            */
/* B maps the arguments to the inductive type T.  For every call of `name`   */
/* in a right side, at arguments y, under the left side x, the bound goes    */
/* down: B y is below B x in T, where "below" is the least transitive        */
/* relation that puts every recursive field of a constructor below the       */
/* constructor's value.  Each such statement, quantified over the pattern    */
/* variables, is an obligation, proved by the proofs given in order.  So     */
/* every chain of calls is at most B x long: the equations computed with     */
/* that many calls left (the bounded form) give a function, and it satisfies */
/* the equations, by induction on the bound.  The equations are admitted as  */
/* written, with nested constructor patterns allowed, and run as written;    */
/* the record keeps the bound and the obligations as its termination         */
/* evidence, beside `structural` and `scrutinee-first`.                     */
/* ------------------------------------------------------------------------ */

typedef struct {
    Atom **vars;        /* pattern variables, in order of binding */
    Atom **types;       /* their canonical types */
    Atom **authored;    /* their authored types */
    size_t count;
    size_t cap;
} SjBoundVars;

/* The authored type of constructor `ctor` of the inductive type `type`, as
 * its declaration record writes it. */
static Atom *sj_bound_authored_constructor_type(SjProofState *st, Atom *type,
                                                Atom *ctor) {
    Atom *record = sj_inductive_record(st, type);
    Atom *decl = record ? record->expr.elems[SJ_KNOWN_PROP] : NULL;
    if (!decl || decl->kind != ATOM_EXPR) return NULL;
    for (CettaExprIndex i = 2u; i < decl->expr.len; i++) {
        Atom *c = decl->expr.elems[i];
        if (c && c->kind == ATOM_EXPR && c->expr.len == 3u &&
            atom_eq(c->expr.elems[1], ctor))
            return c->expr.elems[2];
    }
    return NULL;
}

/* Bind the variables of a linear constructor pattern of canonical type
 * `type` and authored type `authored`, constructors nested to any depth.
 * The constructors' fields must not depend on each other. */
static bool sj_bound_bind(SjProofState *st, Atom *pat, Atom *type,
                          Atom *authored, SjBoundVars *v) {
    Arena *a = st->arena;
    if (sj_is_var(pat)) {
        for (size_t i = 0u; i < v->count; i++)
            if (atom_eq(v->vars[i], pat)) {
                sj_fail(st, true, sj_expr2(a, "set:define-nonlinear", pat));
                return false;
            }
        if (v->count >= v->cap) {
            sj_fail(st, false, sj_expr1(a, "set:define-variable-storage"));
            return false;
        }
        v->vars[v->count] = pat;
        v->types[v->count] = type;
        v->authored[v->count] = authored;
        v->count++;
        return true;
    }
    Atom *head = pat->kind == ATOM_SYMBOL ? pat
               : (pat->kind == ATOM_EXPR && pat->expr.len >= 2u ? pat->expr.elems[0] : NULL);
    size_t fields = pat->kind == ATOM_EXPR ? (size_t)pat->expr.len - 1u : 0u;
    if (!head || !type || type->kind != ATOM_SYMBOL || !sj_inductive_record(st, type)) {
        sj_fail(st, false, sj_expr2(a, "set:define-not-a-constructor", pat));
        return false;
    }
    SjConstructor *ctors = NULL;
    size_t ctor_count = 0u;
    Atom *principle = NULL;
    if (!sj_datatype_constructors(st, type, &ctors, &ctor_count, &principle)) return false;
    SjConstructor *ctor = NULL;
    for (size_t c = 0u; c < ctor_count && !ctor; c++)
        if (atom_eq(ctors[c].name, head) && ctors[c].arity == fields) ctor = &ctors[c];
    if (!ctor) {
        sj_fail(st, false, sj_expr2(a, "set:define-not-a-constructor", pat));
        return false;
    }
    Atom *ctype = sj_constant_type(st, head);
    Atom *atype = sj_bound_authored_constructor_type(st, type, head);
    if (!ctype || !atype) {
        sj_fail(st, false, sj_expr2(a, "set:define-constructor-type", head));
        return false;
    }
    for (size_t i = 0u; i < fields; i++) {
        if (!sj_is_expr(ctype, "Pi", 3u) || sj_mentions_index(ctype->expr.elems[2], 0u) ||
            atype->kind != ATOM_EXPR || (size_t)atype->expr.len < fields + 2u ||
            !atom_is_symbol(atype->expr.elems[0], "->")) {
            sj_fail(st, false, sj_expr2(a, "set:define-dependent-domain", pat));
            return false;
        }
        Atom *field_type = ctype->expr.elems[1];
        ctype = sj_shift(a, ctype->expr.elems[2], 0u, -1);
        if (!ctype ||
            !sj_bound_bind(st, pat->expr.elems[1u + i], field_type,
                           atype->expr.elems[1u + i], v))
            return false;
    }
    return true;
}

/* Two linear patterns that some argument matches both. */
static bool sj_bound_patterns_overlap(Atom *p, Atom *q) {
    if (sj_is_var(p) || sj_is_var(q)) return true;
    if (p->kind != ATOM_EXPR || q->kind != ATOM_EXPR) return atom_eq(p, q);
    if (p->expr.len != q->expr.len) return false;
    for (CettaExprIndex i = 0u; i < p->expr.len; i++)
        if (!sj_bound_patterns_overlap(p->expr.elems[i], q->expr.elems[i])) return false;
    return true;
}

/* Whether `t` is a form that binds names: an abstraction, or a MeTTa
 * binding form that a right side may hold. */
static bool sj_bound_binding_form(Atom *t) {
    if (!t || t->kind != ATOM_EXPR || t->expr.len < 1u) return false;
    Atom *h = t->expr.elems[0];
    return atom_is_symbol(h, "lam") || atom_is_symbol(h, "let") ||
           atom_is_symbol(h, "let*") || atom_is_symbol(h, "match") ||
           atom_is_symbol(h, "case") || atom_is_symbol(h, "chain") ||
           atom_is_symbol(h, "unify");
}

/* Whether `t` mentions a variable that is not one of the equation's pattern
 * variables. */
static bool sj_bound_foreign_variable(Atom *t, SjBoundVars *v) {
    if (!t) return false;
    if (sj_is_var(t)) {
        for (size_t i = 0u; i < v->count; i++)
            if (atom_eq(v->vars[i], t)) return false;
        return true;
    }
    if (t->kind != ATOM_EXPR) return false;
    for (CettaExprIndex i = 0u; i < t->expr.len; i++)
        if (sj_bound_foreign_variable(t->expr.elems[i], v)) return true;
    return false;
}

/* The calls of `name` in `t`.  The obligations speak of every call, so
 * `name` may occur only as the head of a call at exactly `arity` arguments
 * that do not mention it, whose arguments mention only the equation's
 * pattern variables, and not inside a form that binds names.  Any other
 * occurrence is returned in `*offence`, with the kind of occurrence. */
static bool sj_bound_calls(Atom *t, Atom *name, size_t arity, SjBoundVars *v,
                           bool under_binder, Atom ***calls, size_t *count,
                           size_t *cap, Arena *a, Atom **offence) {
    if (!t) return true;
    if (atom_eq(t, name)) {
        *offence = sj_expr2(a, "not-a-call", t);
        return false;
    }
    if (t->kind != ATOM_EXPR) return true;
    if (t->expr.len >= 1u && atom_eq(t->expr.elems[0], name)) {
        if (under_binder) {
            *offence = sj_expr2(a, "under-binder", t);
            return false;
        }
        if ((size_t)t->expr.len != arity + 1u) {
            *offence = sj_expr2(a, "not-a-call", t);
            return false;
        }
        for (CettaExprIndex i = 1u; i < t->expr.len; i++) {
            if (sj_mentions_symbol(t->expr.elems[i], name)) {
                *offence = sj_expr2(a, "nested-call", t);
                return false;
            }
            if (sj_bound_foreign_variable(t->expr.elems[i], v)) {
                *offence = sj_expr2(a, "under-binder", t);
                return false;
            }
        }
        if (*count == *cap) *calls = sj_grow(a, *calls, *count, cap);
        (*calls)[(*count)++] = t;
        return true;
    }
    bool binder = under_binder || sj_bound_binding_form(t);
    for (CettaExprIndex i = 0u; i < t->expr.len; i++)
        if (!sj_bound_calls(t->expr.elems[i], name, arity, v, binder, calls, count,
                            cap, a, offence))
            return false;
    return true;
}

/* `t` with each pattern variable replaced by its name in the obligation. */
static Atom *sj_bound_named(Arena *a, Atom *t, SjBoundVars *v, Atom **names) {
    if (!t) return NULL;
    if (sj_is_var(t)) {
        for (size_t i = 0u; i < v->count; i++)
            if (atom_eq(v->vars[i], t)) return names[i];
        return t;
    }
    if (t->kind != ATOM_EXPR) return t;
    Atom **children = arena_alloc(a, sizeof(Atom *) * (t->expr.len ? t->expr.len : 1u));
    if (!children) return NULL;
    for (CettaExprIndex i = 0u; i < t->expr.len; i++) {
        children[i] = sj_bound_named(a, t->expr.elems[i], v, names);
        if (!children[i]) return NULL;
    }
    return atom_expr(a, children, t->expr.len);
}

/* `m` below `n` in the inductive type `type`, written out:
 *   (all (-> T (-> T prop)) (lam bound~below
 *     (imp <each recursive field is below its value>
 *       (imp <what is below a field is below its value> (bound~below m n)))))
 * The relation is the least one closed under the two kinds of clause. */
static Atom *sj_bound_below(SjProofState *st, Atom *type, Atom *m, Atom *n) {
    Arena *a = st->arena;
    SjConstructor *ctors = NULL;
    size_t ctor_count = 0u;
    Atom *principle = NULL;
    if (!sj_datatype_constructors(st, type, &ctors, &ctor_count, &principle)) return NULL;
    Atom *rel = sj_sym(a, "bound~below");
    Atom **clauses = NULL;
    size_t clause_count = 0u, clause_cap = 0u;
    for (int kind = 0; kind < 2; kind++) {
        for (size_t c = 0u; c < ctor_count; c++) {
            SjConstructor *ctor = &ctors[c];
            Atom *atype = sj_bound_authored_constructor_type(st, type, ctor->name);
            if (ctor->arity > 0u &&
                (!atype || atype->kind != ATOM_EXPR ||
                 (size_t)atype->expr.len < ctor->arity + 2u))
                return NULL;
            Atom **fields = arena_alloc(a, sizeof(Atom *) * (ctor->arity + 1u));
            for (size_t i = 0u; i < ctor->arity; i++) {
                char buf[48];
                snprintf(buf, sizeof buf, "bound~field%zu", i);
                fields[i] = sj_sym(a, buf);
            }
            Atom *value = ctor->name;
            if (ctor->arity > 0u) {
                Atom **items = arena_alloc(a, sizeof(Atom *) * (ctor->arity + 1u));
                items[0] = ctor->name;
                for (size_t i = 0u; i < ctor->arity; i++) items[1u + i] = fields[i];
                value = atom_expr(a, items, (CettaExprLen)(ctor->arity + 1u));
            }
            for (size_t r = 0u; r < ctor->arity; r++) {
                if (!ctor->recursive[r]) continue;
                Atom *low = sj_sym(a, "bound~low");
                Atom *body = kind == 0
                    ? atom_expr3(a, rel, fields[r], value)
                    : sj_expr3(a, "imp", atom_expr3(a, rel, low, fields[r]),
                               atom_expr3(a, rel, low, value));
                for (size_t i = ctor->arity; i > 0u; i--)
                    body = sj_expr3(a, "all", atype->expr.elems[i],
                                    sj_expr3(a, "lam", fields[i - 1u], body));
                if (kind == 1)
                    body = sj_expr3(a, "all", type, sj_expr3(a, "lam", low, body));
                if (clause_count == clause_cap)
                    clauses = sj_grow(a, clauses, clause_count, &clause_cap);
                clauses[clause_count++] = body;
            }
        }
    }
    Atom *goal = atom_expr3(a, rel, m, n);
    for (size_t i = clause_count; i > 0u; i--)
        goal = sj_expr3(a, "imp", clauses[i - 1u], goal);
    Atom *arrow[4] = {sj_sym(a, "->"), type, type, sj_sym(a, "prop")};
    return sj_expr3(a, "all", atom_expr(a, arrow, 4u), sj_expr3(a, "lam", rel, goal));
}

/* `t` with the constant `name` replaced by `by`. */
static Atom *sj_solution_rename(Arena *a, Atom *t, Atom *name, Atom *by) {
    if (!t) return NULL;
    if (atom_eq(t, name)) return by;
    if (t->kind != ATOM_EXPR) return t;
    Atom **children = arena_alloc(a, sizeof(Atom *) * (t->expr.len ? t->expr.len : 1u));
    if (!children) return NULL;
    for (CettaExprIndex i = 0u; i < t->expr.len; i++) {
        children[i] = sj_solution_rename(a, t->expr.elems[i], name, by);
        if (!children[i]) return NULL;
    }
    return atom_expr(a, children, t->expr.len);
}

/* Admission on evidence that the equations have a solution in sets: the
 * clause's proof proves, by the existing judgment, the statement that some
 * function of the declared type satisfies every equation.  When the result
 * is itself a function, of positions, an equation is stated at every
 * position: in sets two functions with the same values are one.  The rules
 * are guarded: they unfold only at closed arguments where they inspect
 * them, and a result that is a function waits for a closed position.  So
 * they never unfold in an open term, and a comparison that meets them is not
 * refuted (regular_mentions_guarded). */
static Atom *sj_set_define_admit_solution(Arena *a, Space *space, Atom *judgment,
                                          SjProofState *st, SjSetEnv *env, Atom *name,
                                          Atom *type, size_t arity, Atom *result,
                                          size_t eq_count, SjBoundVars *bound,
                                          Atom **rules, bool limited, uint64_t steps) {
    Atom *clause = judgment->expr.elems[3];
    /* The result read after the arguments, and the position it waits for
     * when it is a function. */
    size_t rest = (size_t)type->expr.len - 1u - arity;
    Atom *authored_result = rest == 1u ? type->expr.elems[1u + arity] : NULL;
    if (rest > 1u) {
        Atom **items = arena_alloc(a, sizeof(Atom *) * (rest + 1u));
        items[0] = sj_sym(a, "->");
        for (size_t i = 0u; i < rest; i++) items[1u + i] = type->expr.elems[1u + arity + i];
        authored_result = atom_expr(a, items, (CettaExprLen)(rest + 1u));
    }
    Atom *observed = NULL;          /* authored type of the position */
    Atom *observed_result = NULL;   /* authored type at a position */
    Atom *observed_canonical = NULL;
    if (sj_is_expr(result, "Pi", 3u)) {
        if (sj_mentions_index(result->expr.elems[2], 0u) ||
            authored_result->kind != ATOM_EXPR || authored_result->expr.len < 3u ||
            !atom_is_symbol(authored_result->expr.elems[0], "->") ||
            sj_is_typed_binder(authored_result->expr.elems[1]))
            return sj_undetermined(a, judgment,
                                   sj_expr2(a, "set:define-set-solution-type-fragment", type));
        observed = authored_result->expr.elems[1];
        observed_canonical = result->expr.elems[1];
        if (authored_result->expr.len == 3u) {
            observed_result = authored_result->expr.elems[2];
        } else {
            Atom **items = arena_alloc(a, sizeof(Atom *) * (size_t)(authored_result->expr.len - 1u));
            items[0] = sj_sym(a, "->");
            for (CettaExprIndex i = 2u; i < authored_result->expr.len; i++)
                items[i - 1u] = authored_result->expr.elems[i];
            observed_result = atom_expr(a, items, authored_result->expr.len - 1u);
        }
    }
    /* The statement: some function g of the declared type satisfies them. */
    Atom *g = sj_sym(a, "solution~f");
    Atom *position = sj_sym(a, "solution~k");
    Atom *conjunction = NULL;
    for (size_t e = eq_count; e > 0u; e--) {
        Atom *eq = judgment->expr.elems[4u + e - 1u];
        Atom *lhs = eq->expr.elems[1];
        SjBoundVars *v = &bound[e - 1u];
        Atom **names = arena_alloc(a, sizeof(Atom *) * (v->count ? v->count : 1u));
        for (size_t i = 0u; i < v->count; i++) {
            const char *spelling = atom_name_cstr(v->vars[i]);
            Atom *symbol = sj_sym(a, spelling ? spelling : "solution~x");
            if (sj_constant_type(st, symbol)) {
                char buf[96];
                snprintf(buf, sizeof buf, "%s~v", spelling ? spelling : "solution~x");
                symbol = sj_sym(a, buf);
            }
            names[i] = symbol;
        }
        Atom **left_items = arena_alloc(a, sizeof(Atom *) * (arity + 2u));
        left_items[0] = g;
        for (size_t k = 0u; k < arity; k++)
            left_items[1u + k] = sj_bound_named(a, lhs->expr.elems[1u + k], v, names);
        size_t left_len = arity + 1u;
        Atom *right = sj_solution_rename(a, sj_bound_named(a, eq->expr.elems[2], v, names),
                                         name, g);
        if (observed) {
            left_items[left_len++] = position;
            right = atom_expr2(a, right, position);
        }
        Atom *statement = atom_expr(a, (Atom *[]){sj_sym(a, "eq"),
                                                  observed ? observed_result : authored_result,
                                                  atom_expr(a, left_items, (CettaExprLen)left_len),
                                                  right}, 4u);
        if (observed)
            statement = sj_expr3(a, "all", observed, sj_expr3(a, "lam", position, statement));
        for (size_t i = v->count; i > 0u; i--)
            statement = sj_expr3(a, "all", v->authored[i - 1u],
                                 sj_expr3(a, "lam", names[i - 1u], statement));
        if (!conjunction) {
            conjunction = statement;
        } else {
            Atom *c = sj_sym(a, "solution~c");
            conjunction = sj_expr3(a, "all", sj_sym(a, "prop"), sj_expr3(a, "lam", c,
                sj_expr3(a, "imp", sj_expr3(a, "imp", statement, sj_expr3(a, "imp", conjunction, c)),
                         c)));
        }
    }
    Atom *r = sj_sym(a, "solution~r");
    Atom *exists = sj_expr3(a, "all", sj_sym(a, "prop"), sj_expr3(a, "lam", r,
        sj_expr3(a, "imp",
                 sj_expr3(a, "all", type, sj_expr3(a, "lam", g, sj_expr3(a, "imp", conjunction, r))),
                 r)));
    if (clause->expr.len < 3u)
        return sj_undetermined(a, judgment, sj_expr2(a, "set:define-set-solution-statement", exists));
    st->collect_dependencies = true;
    Atom *goal = sj_elaborate_closed(st, exists, sj_sym(a, "prop"));
    if (!goal || !sj_check(st, clause->expr.elems[2], goal)) {
        Atom *items[4] = {sj_sym(a, "set:define-set-solution-evidence"), exists,
                          clause->expr.elems[2], st->failure ? st->failure : atom_unit(a)};
        return st->incomplete ? sj_incomplete(a, judgment, atom_expr(a, items, 4u))
                              : sj_undetermined(a, judgment, atom_expr(a, items, 4u));
    }
    /* The guarded rules, and their record.  A result that is a type of an
     * admitted family, a stream, is not computed at a closed argument
     * either: its value never ends.  Its rules also wait for an
     * observation, as the family's own iteration does: under each
     * observation of a closed call one unfolding happens. */
    bool family_result = false;
    if (!observed) {
        Atom *former = result;
        while (sj_is_expr(former, "App", 3u)) former = former->expr.elems[1];
        family_result = former != result && sj_constant_ref(former) &&
                        sj_family_record(st, sj_constant_ref(former)) != NULL;
    }
    size_t rule_arity = arity + (observed ? 1u : 0u);
    Atom **kernel_rules = arena_alloc(a, sizeof(Atom *) * eq_count);
    for (size_t e = 0u; e < eq_count; e++) {
        Atom *rule = rules[2u + e];                  /* ((pats) rhs nvars) */
        Atom *lhs = judgment->expr.elems[4u + e]->expr.elems[1];
        SjBoundVars *v = &bound[e];
        size_t nvars = v->count + (observed ? 1u : 0u);
        Atom **pats = arena_alloc(a, sizeof(Atom *) * rule_arity);
        Atom **types = arena_alloc(a, sizeof(Atom *) * (nvars ? nvars : 1u));
        Atom *rhs = rule->expr.elems[1];
        bool guarded = false;
        for (size_t k = 0u; k < arity; k++) {
            Atom *pat = rule->expr.elems[0]->expr.elems[k];
            if (observed) pat = sj_shift(a, pat, 0u, 1);
            if (!sj_is_var(lhs->expr.elems[1u + k])) {
                pat = sj_expr2(a, "PGuard", pat);
                guarded = true;
            }
            pats[k] = pat;
        }
        for (size_t j = 0u; j < v->count; j++) types[j] = v->types[j];
        if (observed) {
            pats[arity] = sj_expr2(a, "PGuard", sj_expr2(a, "idx", atom_int(a, 0)));
            types[v->count] = observed_canonical;
            rhs = sj_expr3(a, "App", sj_shift(a, rhs, 0u, 1), sj_expr2(a, "idx", atom_int(a, 0)));
            guarded = true;
        }
        if (!guarded)
            for (size_t k = 0u; k < arity; k++) pats[k] = sj_expr2(a, "PGuard", pats[k]);
        Atom *items[3] = {atom_expr(a, pats, (CettaExprLen)rule_arity), rhs,
                          atom_int(a, (int64_t)nvars)};
        rules[2u + e] = atom_expr(a, items, 3u);
        kernel_rules[e] = sj_type_rule_atom(st, name, rule_arity, pats, rhs, nvars, types);
        if (!kernel_rules[e])
            return sj_undetermined(a, judgment,
                                   sj_expr2(a, "set:define-rule-lowering", rules[2u + e]));
        if (family_result) {
            Atom *items[5] = {kernel_rules[e]->expr.elems[0], kernel_rules[e]->expr.elems[1],
                              sj_expr2(a, "PObserved", kernel_rules[e]->expr.elems[2]),
                              kernel_rules[e]->expr.elems[3], kernel_rules[e]->expr.elems[4]};
            kernel_rules[e] = atom_expr(a, items, 5u);
        }
    }
    rules[1] = atom_int(a, (int64_t)rule_arity);
    Atom *compiled = atom_expr(a, rules, (CettaExprLen)(eq_count + 2u));
    const char *solution_basis[2] = {SJ_LEAN_EVIDENCE, SJ_LEAN_AT_POSITION};
    Atom *evidence = sj_definition_evidence(
        a, compiled, sj_expr2(a, "set-solution", exists),
        sj_lean_basis(a, solution_basis, observed ? 2u : 1u, false, false));
    Atom **prop_items = arena_alloc(a, sizeof(Atom *) * (eq_count + 2u));
    prop_items[0] = sj_sym(a, "define");
    prop_items[1] = type;
    for (size_t e = 0u; e < eq_count; e++) prop_items[2u + e] = judgment->expr.elems[4u + e];
    Atom *prop = atom_expr(a, prop_items, (CettaExprLen)(eq_count + 2u));
    sj_note_definition_uses(st, prop);
    Atom *record = sj_known_record(a, name, prop, "definition", evidence,
                                   sj_depends_atom(st), space_revision(space));
    {
        SjProofState check;
        sj_proof_state_init(&check, a, space, env, limited, steps, false, false);
        if (!sj_dependencies_current_as(&check, record, 0u, false))
            return check.incomplete ? sj_incomplete(a, judgment, check.failure)
                                    : sj_undetermined(a, judgment, check.failure);
    }
    Atom **published = arena_alloc(a, sizeof(Atom *) * (eq_count + 3u));
    size_t np = 0u;
    published[np++] = sj_sym(a, "SetPublish");
    published[np++] = record;
    published[np++] = sj_expr3(a, ":", name, type);
    for (size_t e = 0u; e < eq_count; e++) published[np++] = kernel_rules[e];
    return sj_established(a, judgment, atom_expr(a, published, (CettaExprLen)np));
}

static Atom *sj_set_define_by_clause(Arena *a, Space *space, Atom *judgment,
                                     bool by_bound, bool limited, uint64_t steps) {
    CettaExprLen len = judgment->expr.len;
    Atom *name = judgment->expr.elems[1];
    Atom *type = judgment->expr.elems[2];
    Atom *clause = judgment->expr.elems[3];
    if (len < 5u || clause->expr.len < (by_bound ? 4u : 2u) ||
        (!by_bound && clause->expr.len > 3u))
        return sj_refuted(a, judgment, sj_expr1(a, by_bound ? "set:define-bound-arity"
                                                            : "set:define-set-solution-arity"));
    if (!name || name->kind != ATOM_SYMBOL)
        return sj_refuted(a, judgment, sj_expr1(a, "set:define-name"));
    Atom *reserved = sj_reserved_definition(a, space, judgment, name);
    if (reserved) return reserved;
    SjSetEnv *env = sj_env(space);
    if (!env)
        return sj_undetermined(a, judgment, sj_expr1(a, "set:signature-unavailable"));
    SjProofState st;
    sj_proof_state_init(&st, a, space, env, limited, steps, false, true);
    if (sj_name_taken(&st, name))
        return sj_refuted(a, judgment, sj_expr2(a, "set:define-already-declared", name));
    Atom *canonical_type = NULL;
    Atom *obstruction = sj_declaration_check_obstruction(
        a, &env->overlay, judgment, type, sj_expr2(a, "type:formed", type),
        &st.budget, &canonical_type);
    if (obstruction) return obstruction;
    if (!canonical_type)
        return sj_undetermined(a, judgment, sj_expr2(a, "set:define-type-fragment", type));
    size_t eq_count = (size_t)len - 4u;
    Atom *first_lhs = sj_is_expr(judgment->expr.elems[4], "=", 3u)
        ? judgment->expr.elems[4]->expr.elems[1] : NULL;
    if (!first_lhs || first_lhs->kind != ATOM_EXPR || first_lhs->expr.len < 2u ||
        !atom_eq(first_lhs->expr.elems[0], name))
        return sj_refuted(a, judgment, sj_expr2(a, "set:define-left-side",
                                                first_lhs ? first_lhs : judgment->expr.elems[4]));
    size_t arity = (size_t)first_lhs->expr.len - 1u;
    /* The arguments' types, authored and canonical, and the result type:
     * the bound route reads a type of plain arrows. */
    Atom **params = arena_alloc(a, sizeof(Atom *) * arity);
    if (!params) return sj_incomplete(a, judgment, sj_expr1(a, "set:define-storage"));
    Atom *result = sj_pi_result(a, canonical_type, arity, params);
    if (!result || type->kind != ATOM_EXPR || (size_t)type->expr.len < arity + 2u ||
        !atom_is_symbol(type->expr.elems[0], "->"))
        return sj_undetermined(a, judgment, sj_expr2(a, "set:define-bound-type-fragment", type));
    for (size_t k = 0u; k < arity; k++)
        if (sj_is_typed_binder(type->expr.elems[1u + k]))
            return sj_undetermined(a, judgment, sj_expr2(a, "set:define-bound-type-fragment", type));
    Atom *bound_type = by_bound ? clause->expr.elems[2] : NULL;
    Atom *measure = by_bound ? clause->expr.elems[3] : NULL;
    size_t proof_count = by_bound ? (size_t)clause->expr.len - 4u : 0u;
    if (by_bound &&
        (!bound_type || bound_type->kind != ATOM_SYMBOL || !sj_inductive_record(&st, bound_type)))
        return sj_undetermined(a, judgment, sj_expr2(a, "set:define-bound-not-inductive", bound_type));
    for (size_t e = 0u; e < eq_count; e++) {
        Atom *eq = judgment->expr.elems[4u + e];
        if (!sj_is_expr(eq, "=", 3u))
            return sj_refuted(a, judgment, sj_expr2(a, "set:define-equation", eq));
        Atom *lhs = eq->expr.elems[1];
        if (lhs->kind != ATOM_EXPR || (size_t)lhs->expr.len != arity + 1u ||
            !atom_eq(lhs->expr.elems[0], name))
            return sj_refuted(a, judgment, sj_expr2(a, "set:define-left-side", lhs));
    }
    /* Two equations whose left sides some argument matches both are not
     * admitted: the rules would not agree with each other. */
    for (size_t e = 0u; e < eq_count; e++)
        for (size_t f = e + 1u; f < eq_count; f++)
            if (sj_bound_patterns_overlap(judgment->expr.elems[4u + e]->expr.elems[1],
                                          judgment->expr.elems[4u + f]->expr.elems[1]))
                return sj_undetermined(a, judgment, sj_expr2(a, "set:define-overlap",
                    judgment->expr.elems[4u + f]->expr.elems[1]));

    /* The equations: patterns bound, both sides typed at the result type with
     * the constant at its declared type and no rule of its own. */
    Atom **rules = arena_alloc(a, sizeof(Atom *) * (eq_count + 2u));
    Atom **kernel_rules = arena_alloc(a, sizeof(Atom *) * (eq_count ? eq_count : 1u));
    SjBoundVars *bound = arena_alloc(a, sizeof(SjBoundVars) * eq_count);
    if (!rules || !kernel_rules || !bound)
        return sj_incomplete(a, judgment, sj_expr1(a, "set:define-storage"));
    rules[0] = sj_sym(a, "rules");
    rules[1] = atom_int(a, (int64_t)arity);
    size_t saved_constants = env->constant_count;
    sj_constant_cache_add(env, name, canonical_type);
    sj_declare_mentioned(&st, canonical_type, NULL);
    sj_note_instance(&st, name, canonical_type);
    for (size_t e = 0u; e < eq_count; e++) {
        Atom *eq = judgment->expr.elems[4u + e];
        Atom *lhs = eq->expr.elems[1];
        SjBoundVars *v = &bound[e];
        v->cap = sj_atom_size(lhs);
        v->count = 0u;
        v->vars = arena_alloc(a, sizeof(Atom *) * v->cap);
        v->types = arena_alloc(a, sizeof(Atom *) * v->cap);
        v->authored = arena_alloc(a, sizeof(Atom *) * v->cap);
        Atom **pats = arena_alloc(a, sizeof(Atom *) * arity);
        if (pats) memset(pats, 0, sizeof(Atom *) * arity);
        if (!v->vars || !v->types || !v->authored || !pats) {
            env->constant_count = saved_constants;
            return sj_incomplete(a, judgment, sj_expr1(a, "set:define-storage"));
        }
        for (size_t k = 0u; k < arity; k++) {
            if (!sj_bound_bind(&st, lhs->expr.elems[1u + k], params[k],
                               type->expr.elems[1u + k], v)) {
                env->constant_count = saved_constants;
                return st.refuted ? sj_refuted(a, judgment, st.failure)
                                  : sj_undetermined(a, judgment, st.failure);
            }
        }
        if (by_bound) {
            /* Every occurrence of the constant in the right side is a call
             * the obligations can see, before the side is read at all. */
            Atom **seen_calls = NULL;
            size_t seen_count = 0u, seen_cap = 0u;
            Atom *offence = NULL;
            if (!sj_bound_calls(eq->expr.elems[2], name, arity, v, false, &seen_calls,
                                &seen_count, &seen_cap, a, &offence)) {
                env->constant_count = saved_constants;
                return sj_undetermined(a, judgment, sj_expr3(a, "set:define-bound-occurrence",
                                                             offence, eq));
            }
        }
        for (size_t k = 0u; k < arity && v->count <= 16u; k++) {
            pats[k] = sj_equation_lower(&st, lhs->expr.elems[1u + k], v->vars, v->count);
            if (!pats[k]) break;
        }
        Atom *crhs = v->count <= 16u && pats[arity - 1u]
            ? sj_equation_lower(&st, eq->expr.elems[2], v->vars, v->count) : NULL;
        if (crhs) {
            Atom **locals = sj_locals_for(a, v->count, crhs, NULL);
            if (!locals) {
                env->constant_count = saved_constants;
                return sj_incomplete(a, judgment, sj_expr1(a, "set:define-storage"));
            }
            for (size_t j = 0u; j < v->count; j++) locals[j] = v->types[j];
            crhs = sj_annotate(&st, crhs, locals, v->count);
        }
        if (!crhs) {
            env->constant_count = saved_constants;
            if (!st.failure)
                sj_fail(&st, false, sj_expr2(a, "set:define-variable-storage", eq));
            return st.incomplete ? sj_incomplete(a, judgment, st.failure)
                : st.refuted ? sj_refuted(a, judgment, st.failure)
                : sj_undetermined(a, judgment, st.failure);
        }
        Atom *cleft = sj_canonical_constant(a, name);
        for (size_t k = 0u; k < arity; k++) cleft = sj_expr3(a, "App", cleft, pats[k]);
        Atom *mismatch = sj_definition_equation_obstruction(
            &st, judgment, v->types, v->count, cleft, crhs, result, &st.budget);
        if (mismatch) {
            env->constant_count = saved_constants;
            return mismatch;
        }
        Atom *rule_items[3] = {atom_expr(a, pats, (CettaExprLen)arity), crhs,
                               atom_int(a, (int64_t)v->count)};
        rules[2u + e] = atom_expr(a, rule_items, 3u);
        kernel_rules[e] = sj_type_rule_atom(&st, name, arity, pats, crhs, v->count, v->types);
        if (!kernel_rules[e]) {
            env->constant_count = saved_constants;
            return sj_undetermined(a, judgment,
                                   sj_expr2(a, "set:define-rule-lowering", rules[2u + e]));
        }
    }
    env->constant_count = saved_constants;
    if (!by_bound)
        return sj_set_define_admit_solution(a, space, judgment, &st, env, name, type,
                                            arity, result, eq_count, bound, rules,
                                            limited, steps);

    /* The measure, a function from the arguments to the bound's type. */
    Atom **arrow = arena_alloc(a, sizeof(Atom *) * (arity + 2u));
    arrow[0] = sj_sym(a, "->");
    for (size_t k = 0u; k < arity; k++) arrow[1u + k] = type->expr.elems[1u + k];
    arrow[arity + 1u] = bound_type;
    if (!sj_elaborate_closed(&st, measure, atom_expr(a, arrow, (CettaExprLen)(arity + 2u))))
        return sj_undetermined(a, judgment, sj_expr3(a, "set:define-bound-measure",
                                                     measure, st.failure));

    /* The obligations: at every call, the bound goes down. */
    Atom **obligations = NULL;
    size_t obligation_count = 0u, obligation_cap = 0u;
    for (size_t e = 0u; e < eq_count; e++) {
        Atom *eq = judgment->expr.elems[4u + e];
        Atom *lhs = eq->expr.elems[1];
        SjBoundVars *v = &bound[e];
        Atom **calls = NULL;
        size_t call_count = 0u, call_cap = 0u;
        Atom *offence = NULL;
        if (!sj_bound_calls(eq->expr.elems[2], name, arity, v, false, &calls,
                            &call_count, &call_cap, a, &offence))
            return sj_undetermined(a, judgment, sj_expr3(a, "set:define-bound-occurrence",
                                                         offence, eq));
        Atom **names = arena_alloc(a, sizeof(Atom *) * (v->count ? v->count : 1u));
        for (size_t i = 0u; i < v->count; i++) {
            const char *spelling = atom_name_cstr(v->vars[i]);
            Atom *symbol = sj_sym(a, spelling ? spelling : "bound~x");
            if (sj_constant_type(&st, symbol)) {
                char buf[96];
                snprintf(buf, sizeof buf, "%s~v", spelling ? spelling : "bound~x");
                symbol = sj_sym(a, buf);
            }
            names[i] = symbol;
        }
        Atom **xs = arena_alloc(a, sizeof(Atom *) * (arity + 1u));
        xs[0] = measure;
        for (size_t k = 0u; k < arity; k++)
            xs[1u + k] = sj_bound_named(a, lhs->expr.elems[1u + k], v, names);
        Atom *above = atom_expr(a, xs, (CettaExprLen)(arity + 1u));
        for (size_t c = 0u; c < call_count; c++) {
            Atom **ys = arena_alloc(a, sizeof(Atom *) * (arity + 1u));
            ys[0] = measure;
            for (size_t k = 0u; k < arity; k++)
                ys[1u + k] = sj_bound_named(a, calls[c]->expr.elems[1u + k], v, names);
            Atom *statement = sj_bound_below(&st, bound_type,
                                             atom_expr(a, ys, (CettaExprLen)(arity + 1u)),
                                             above);
            if (!statement)
                return sj_undetermined(a, judgment,
                                       sj_expr2(a, "set:define-bound-not-inductive", bound_type));
            for (size_t i = v->count; i > 0u; i--)
                statement = sj_expr3(a, "all", v->authored[i - 1u],
                                     sj_expr3(a, "lam", names[i - 1u], statement));
            if (obligation_count == obligation_cap)
                obligations = sj_grow(a, obligations, obligation_count, &obligation_cap);
            obligations[obligation_count++] = statement;
        }
    }
    Atom *obligation_list = atom_expr(a, obligations ? obligations : &name,
                                      (CettaExprLen)obligation_count);
    if (proof_count != obligation_count)
        return sj_undetermined(a, judgment, sj_expr3(a, "set:define-bound-obligations",
            atom_int(a, (int64_t)proof_count), obligation_list));
    st.collect_dependencies = true;
    for (size_t i = 0u; i < obligation_count; i++) {
        Atom *goal = sj_elaborate_closed(&st, obligations[i], sj_sym(a, "prop"));
        if (!goal || !sj_check(&st, clause->expr.elems[4u + i], goal)) {
            Atom *items[4] = {sj_sym(a, "set:define-bound-obligation"), obligations[i],
                              clause->expr.elems[4u + i],
                              st.failure ? st.failure : atom_unit(a)};
            return st.incomplete ? sj_incomplete(a, judgment, atom_expr(a, items, 4u))
                                 : sj_undetermined(a, judgment, atom_expr(a, items, 4u));
        }
    }

    /* Admitted: the equations as written, with the bound as termination. */
    Atom *compiled = atom_expr(a, rules, (CettaExprLen)(eq_count + 2u));
    Atom *bound_items[4] = {sj_sym(a, "bound"), bound_type, measure, obligation_list};
    /* The bound's order is the structural order of a declared datatype, and
     * the patterns are linear constructor patterns that do not overlap; one
     * level per constructor also covers the datatype, which uniqueness uses. */
    bool covers_one_level = arity == 1u && sj_rules_one_level(compiled, 0u);
    const char *bound_basis[4] = {SJ_LEAN_BOUND, SJ_LEAN_BOUND_ORDER, SJ_LEAN_BOUND_APART,
                                  SJ_LEAN_BOUND_COVERS};
    /* The theorem's `filler`: the declared function type has a value.  It
     * is checked where the result type is a datatype with a constructor of
     * no field, whose constant function is a value; otherwise the record
     * says it is not checked. */
    Atom *filler = sj_expr2(a, "unchecked", sj_sym(a, "filler"));
    Atom *result_type = type && type->kind == ATOM_EXPR && type->expr.len >= 3u
        ? type->expr.elems[type->expr.len - 1u] : NULL;
    Atom *result_record = result_type ? sj_inductive_record(&st, result_type) : NULL;
    Atom *result_decl = result_record ? result_record->expr.elems[SJ_KNOWN_PROP] : NULL;
    for (CettaExprIndex i = 2u; result_decl && result_decl->kind == ATOM_EXPR &&
                                i < result_decl->expr.len; i++) {
        Atom *ctor = result_decl->expr.elems[i];
        if (sj_is_expr(ctor, ":", 3u) && atom_eq(ctor->expr.elems[2], result_type)) {
            filler = sj_expr2(a, "filler", ctor->expr.elems[1]);
            break;
        }
    }
    Atom *basis = sj_lean_basis(a, bound_basis, covers_one_level ? 4u : 3u, arity > 1u, false);
    Atom **basis_items = arena_alloc(a, sizeof(Atom *) * ((size_t)basis->expr.len + 1u));
    for (CettaExprIndex i = 0u; i < basis->expr.len; i++) basis_items[i] = basis->expr.elems[i];
    basis_items[basis->expr.len] = filler;
    basis = atom_expr(a, basis_items, basis->expr.len + 1u);
    Atom *evidence = sj_definition_evidence(
        a, compiled, atom_expr(a, bound_items, 4u), basis);
    Atom **prop_items = arena_alloc(a, sizeof(Atom *) * (eq_count + 2u));
    prop_items[0] = sj_sym(a, "define");
    prop_items[1] = type;
    for (size_t e = 0u; e < eq_count; e++) prop_items[2u + e] = judgment->expr.elems[4u + e];
    Atom *prop = atom_expr(a, prop_items, (CettaExprLen)(eq_count + 2u));
    sj_note_definition_uses(&st, prop);
    sj_note_definition_uses(&st, measure);
    Atom *record = sj_known_record(a, name, prop, "definition", evidence,
                                   sj_depends_atom(&st), space_revision(space));
    {
        SjProofState check;
        sj_proof_state_init(&check, a, space, env, limited, steps, false, false);
        if (!sj_dependencies_current_as(&check, record, 0u, false))
            return check.incomplete ? sj_incomplete(a, judgment, check.failure)
                                    : sj_undetermined(a, judgment, check.failure);
    }
    Atom **published = arena_alloc(a, sizeof(Atom *) * (eq_count + 3u));
    size_t np = 0u;
    published[np++] = sj_sym(a, "SetPublish");
    published[np++] = record;
    published[np++] = sj_expr3(a, ":", name, type);
    for (size_t e = 0u; e < eq_count; e++) published[np++] = kernel_rules[e];
    return sj_established(a, judgment, atom_expr(a, published, (CettaExprLen)np));
}

static Atom *sj_set_define(Arena *a, Space *space, Atom *judgment,
                            bool limited, uint64_t steps) {
    if (limited && steps == 0u)
        return sj_incomplete(a, judgment, sj_expr1(a, "set:define-budget"));
    CettaExprLen len = judgment->expr.len;
    if (len < 4u) return sj_refuted(a, judgment, sj_expr1(a, "set:define-arity"));
    Atom *clause = judgment->expr.elems[3];
    if (clause && clause->kind == ATOM_EXPR && clause->expr.len >= 2u &&
        atom_is_symbol(clause->expr.elems[0], "by") &&
        (atom_is_symbol(clause->expr.elems[1], "bound") ||
         atom_is_symbol(clause->expr.elems[1], "set-solution")))
        return sj_set_define_by_clause(a, space, judgment,
                                       atom_is_symbol(clause->expr.elems[1], "bound"),
                                       limited, steps);
    Atom *name = judgment->expr.elems[1];
    Atom *type = judgment->expr.elems[2];
    if (!name || name->kind != ATOM_SYMBOL)
        return sj_refuted(a, judgment, sj_expr1(a, "set:define-name"));
    Atom *reserved = sj_reserved_definition(a, space, judgment, name);
    if (reserved) return reserved;
    SjSetEnv *env = sj_env(space);
    if (!env)
        return sj_undetermined(a, judgment, sj_expr1(a, "set:signature-unavailable"));
    SjProofState st;
    sj_proof_state_init(&st, a, space, env, limited, steps, false, true);
    if (sj_name_taken(&st, name))
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
    Atom **params = arena_alloc(a, sizeof(Atom *) * (arity ? arity : 1u));
    if (!params) return sj_incomplete(a, judgment, sj_expr1(a, "set:define-storage"));
    memset(params, 0, sizeof(Atom *) * (arity ? arity : 1u));
    bool simple_parameters = sj_pi_result(a, canonical_type, arity, params) != NULL;
    size_t eq_count = len - 3u;

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
            /* Inspecting two arguments is legal; case trees over several
             * arguments are not admitted yet. */
            if (scrut != arity && scrut != k)
                return sj_undetermined(a, judgment, sj_expr3(a, "set:define-two-scrutinees", lhs,
                                                             sj_expr2(a, "admit-by", sj_sym(a, "bound"))));
            scrut = k;
        }
    }
    SjConstructor *ctors = NULL;
    size_t ctor_count = 0u;
    Atom *principle = NULL;
    Atom *scrut_type = NULL;
    bool *covered = NULL;
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
        scrut_type = sj_closed_inductive_reduct(&st, raw_domain, outer, &exhausted);
        if (!scrut_type) {
            if (exhausted || st.incomplete)
                return sj_incomplete(a, judgment, st.failure ? st.failure
                                     : sj_expr2(a, "set:normalization-fuel", raw_domain));
            return sj_undetermined(a, judgment, sj_expr2(a, "set:define-constructor-coverage-fragment", type));
        }
        if (!sj_datatype_constructors(&st, scrut_type, &ctors, &ctor_count, &principle))
            return sj_undetermined(a, judgment, st.failure);
        covered = arena_alloc(a, sizeof(bool) * (ctor_count ? ctor_count : 1u));
        if (!covered) return sj_incomplete(a, judgment, sj_expr1(a, "set:define-storage"));
        memset(covered, 0, sizeof(bool) * (ctor_count ? ctor_count : 1u));
        if (eq_count != ctor_count) {
            /* A constructor no equation names, when every equation names a
             * constructor, is a missing case.  The equations then leave the
             * value there open and some set satisfies them, so the structural
             * route declines without refuting.  A variable at the inspected
             * position, or two equations for one constructor, is legal with
             * the first matching equation computing; such case trees are not
             * admitted yet. */
            Atom *missing = sj_define_missing_constructor(
                judgment, scrut, ctors, ctor_count);
            if (missing)
                return sj_undetermined(a, judgment, sj_expr2(a, "set:define-missing-case", missing));
            return sj_undetermined(a, judgment, sj_expr3(a, "set:define-coverage", name,
                                                         sj_expr2(a, "admit-by", sj_sym(a, "bound"))));
        }
        /* A variable at the inspected position beside constructor patterns
         * is legal, the first matching equation computing; such case trees
         * are not admitted yet. */
        if (sj_define_has_wildcard(judgment, scrut))
            return sj_undetermined(a, judgment, sj_expr2(a, "set:define-wildcard-case", name));
    } else if (eq_count != 1u) {
        /* Several equations with no inspected argument overlap entirely. */
        return sj_undetermined(a, judgment, sj_expr2(a, "set:define-coverage", name));
    }
    if (scrut != arity && scrut > 0u) {
        bool fixed = true;
        for (size_t e = 0u; e < eq_count && fixed; e++) {
            Atom *eq = judgment->expr.elems[3u + e];
            fixed = sj_recursion_prefix_fixed(eq->expr.elems[2], name, arity,
                                              scrut, eq->expr.elems[1]);
        }
        if (!fixed)
            return sj_set_define_scrutinee_first(a, space, judgment, &st,
                                                 canonical_type, arity, scrut,
                                                 scrut_type, ctors, ctor_count,
                                                 limited, steps);
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
    Atom **descents = arena_alloc(a, sizeof(Atom *) * (eq_count ? eq_count : 1u));
    Atom ***equation_types = arena_alloc(a, sizeof(Atom **) * (eq_count ? eq_count : 1u));
    if (!descents || !equation_types) {
        env->constant_count = saved_constants;
        return sj_incomplete(a, judgment, sj_expr1(a, "set:define-storage"));
    }
    for (size_t e = 0u; e < eq_count; e++) {
        descents[e] = NULL;
        Atom *lhs = judgment->expr.elems[3u + e]->expr.elems[1];
        size_t var_cap = arity;
        for (size_t k = 0u; k < arity; k++) {
            Atom *pat = lhs->expr.elems[1u + k];
            if (pat->kind == ATOM_EXPR) var_cap += (size_t)pat->expr.len;
        }
        equation_types[e] = arena_alloc(a, sizeof(Atom *) * (var_cap ? var_cap : 1u));
        if (!equation_types[e]) {
            env->constant_count = saved_constants;
            return sj_incomplete(a, judgment, sj_expr1(a, "set:define-storage"));
        }
        memset(equation_types[e], 0, sizeof(Atom *) * (var_cap ? var_cap : 1u));
    }
    for (size_t e = 0u; e < eq_count; e++) {
        Atom *rule = NULL;
        Atom *verdict = sj_definition_equation_check(
            a, &st, judgment, name, canonical_type, arity, scrut,
            judgment->expr.elems[3u + e], ctors, ctor_count, covered, &rule,
            &descents[e], equation_types[e]);
        if (verdict) return env->constant_count = saved_constants, verdict;
        rules[2u + e] = rule;
    }
    env->constant_count = saved_constants;
    for (size_t c = 0u; c < ctor_count; c++)
        if (!covered[c])
            return sj_undetermined(a, judgment, sj_expr2(a, "set:define-missing-case", ctors[c].name));
    Atom *compiled = atom_expr(a, rules, (CettaExprLen)(eq_count + 2u));
    /* The record keeps the compiled rules with the termination evidence they
     * were admitted on: structural recursion on the argument at `scrut`, or
     * one equation with no scrutinee. */
    Atom *termination = scrut == arity
        ? sj_expr1(a, "non-recursive")
        : sj_structural_evidence(a, scrut, scrut_type, descents, eq_count);
    const char *explicit_basis[1] = {SJ_LEAN_EXPLICIT};
    const char *recursion_basis[1] = {SJ_LEAN_RECURSION};
    Atom *basis = scrut == arity
        ? sj_lean_basis(a, explicit_basis, 1u, false, false)
        : sj_lean_basis(a, recursion_basis, 1u, arity > 1u,
                        !sj_rules_one_level(compiled, scrut));
    Atom *evidence = sj_definition_evidence(a, compiled, termination, basis);
    Atom **prop_items = arena_alloc(a, sizeof(Atom *) * (eq_count + 2u));
    prop_items[0] = sj_sym(a, "define");
    prop_items[1] = type;
    for (size_t e = 0u; e < eq_count; e++) prop_items[2u + e] = judgment->expr.elems[3u + e];
    Atom *prop = atom_expr(a, prop_items, (CettaExprLen)(eq_count + 2u));
    /* The definition depends on the closure of definitions and datatypes
     * its type and equations mention, recorded as a theorem's are. */
    st.collect_dependencies = true;
    sj_note_definition_uses(&st, prop);
    Atom *record = sj_known_record(a, name, prop, "definition", evidence,
                                   sj_depends_atom(&st), space_revision(space));
    /* Admitted over what those definitions mean now: one whose own
     * dependencies have changed is renewed first. */
    {
        SjProofState check;
        sj_proof_state_init(&check, a, space, env, limited, steps, false, false);
        if (!sj_dependencies_current_as(&check, record, 0u, false))
            return check.incomplete ? sj_incomplete(a, judgment, check.failure)
                                    : sj_undetermined(a, judgment, check.failure);
    }
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
        Atom *kernel_rule = sj_type_rule_atom(&st, name, arity, pats, rule->expr.elems[1],
                                              nvars, equation_types[e]);
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
    /* A plain recheck that fails withdraws the record's admission here: it
     * is reusable again only once published again or rechecked with success.
     * Under `try` a recheck admits and withdraws nothing. */
    bool withdrawable = !diagnostic &&
        !atom_is_symbol(record->expr.elems[SJ_KNOWN_ORIGIN], "signature") &&
        !sj_signature_owns(env, record);
    if (atom_is_symbol(record->expr.elems[SJ_KNOWN_ORIGIN], "family") ||
        atom_is_symbol(record->expr.elems[SJ_KNOWN_ORIGIN], "family-law")) {
        /* A family is admitted by `set:family` alone, on the set model its
         * record names.  Rechecking one confirms that it is admitted. */
        if (!sj_record_admitted(a, space, record))
            return sj_undetermined(a, judgment, sj_expr2(a, "set:not-admitted", name));
        return sj_established(a, judgment, sj_expr1(a, "SetFamilyCurrent"));
    }
    if (atom_is_symbol(record->expr.elems[SJ_KNOWN_ORIGIN], "definition")) {
        /* A definition is admitted by `set:define` alone.  Rechecking one
         * confirms that it is admitted and that what it was admitted over is
         * unchanged; it admits nothing and withdraws nothing. */
        if (!sj_signature_owns(env, record) && !sj_record_admitted(a, space, record))
            return sj_undetermined(a, judgment, sj_expr2(a, "set:not-admitted", name));
        if (!sj_dependencies_current(&st, record, 0u))
            return st.incomplete ? sj_incomplete(a, judgment, st.failure)
                                 : sj_undetermined(a, judgment, st.failure);
        return sj_established(a, judgment, sj_expr1(a, "SetDefinitionCurrent"));
    }
    if (!atom_is_symbol(record->expr.elems[SJ_KNOWN_ORIGIN], "theorem")) {
        /* An assumption record is admitted as an assumption once its
         * proposition is formed; its origin stays visible. */
        Atom *goal = sj_elaborate_closed(&st, record->expr.elems[SJ_KNOWN_PROP], sj_sym(a, "prop"));
        if (!goal) {
            if (withdrawable) sj_withdraw(a, space, record);
            return st.refuted ? sj_refuted(a, judgment, st.failure)
                              : sj_undetermined(a, judgment, st.failure);
        }
        if (!diagnostic && !atom_is_symbol(record->expr.elems[SJ_KNOWN_ORIGIN], "signature"))
            sj_admit(a, space, record);
        return sj_established(a, judgment,
                              sj_expr2(a, "SetAssumption", record->expr.elems[SJ_KNOWN_ORIGIN]));
    }
    /* A theorem is rechecked as recorded: the dependencies it was checked
     * against must be unchanged, and its proof must check now. */
    Atom *verdict = NULL;
    if (!sj_dependencies_current(&st, record, 0u))
        verdict = st.incomplete ? sj_incomplete(a, judgment, st.failure)
                                : sj_undetermined(a, judgment, st.failure);
    else
        verdict = sj_prove(&st, judgment, record->expr.elems[SJ_KNOWN_PROOF],
                           record->expr.elems[SJ_KNOWN_PROP]);
    Atom *evidence = NULL;
    SjStatus status = sj_verdict_status(verdict, &evidence);
    if (!diagnostic && status == SJ_STATUS_ESTABLISHED)
        sj_admit(a, space, record);
    else if (withdrawable && status != SJ_STATUS_ESTABLISHED)
        sj_withdraw(a, space, record);
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

/* The number of items of an `(LCons x rest)` list. */
static size_t sj_list_length(Atom *list) {
    size_t count = 0u;
    for (Atom *cursor = list; sj_is_expr(cursor, "LCons", 3u);
         cursor = cursor->expr.elems[2])
        count++;
    return count;
}

typedef struct {
    Arena *arena;
    Atom **names;
    Atom **values;
    size_t count;
    size_t cap;
} SjPatternBindings;

static bool sj_pattern_match(Atom *pat, Atom *term, SjPatternBindings *b) {
    if (!pat || !term) return false;
    if (sj_is_expr(pat, "FVar", 2u)) {
        Atom *name = pat->expr.elems[1];
        for (size_t i = 0u; i < b->count; i++)
            if (atom_eq(b->names[i], name)) return atom_eq(b->values[i], term);
        if (b->count == b->cap) {
            size_t cap = b->cap ? b->cap * 2u : 16u;
            Atom **names = arena_alloc(b->arena, sizeof(Atom *) * cap);
            Atom **values = arena_alloc(b->arena, sizeof(Atom *) * cap);
            if (!names || !values) return false;
            for (size_t i = 0u; i < b->count; i++) {
                names[i] = b->names[i];
                values[i] = b->values[i];
            }
            b->names = names;
            b->values = values;
            b->cap = cap;
        }
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
    size_t rule_count = sj_list_length(rules);
    Atom **collected = arena_alloc(a, sizeof(Atom *) * (rule_count ? rule_count : 1u));
    if (!collected)
        return sj_incomplete(a, judgment, sj_expr1(a, "lang:step-storage"));
    size_t step_count = 0u;
    for (Atom *cursor = rules; sj_is_expr(cursor, "LCons", 3u); cursor = cursor->expr.elems[2]) {
        Atom *rule = cursor->expr.elems[1];
        /* (GRuleV1 name formals premises conclusion side) */
        if (!sj_is_expr(rule, "GRuleV1", 6u)) continue;
        SjPatternBindings b = {.arena = a};
        if (!sj_pattern_match(rule->expr.elems[4], goal, &b)) continue;
        Atom *premises = sj_sym(a, "LNil");
        size_t premise_count = sj_list_length(rule->expr.elems[3]);
        Atom **items = arena_alloc(a, sizeof(Atom *) * (premise_count ? premise_count : 1u));
        if (!items)
            return sj_incomplete(a, judgment, sj_expr1(a, "lang:step-storage"));
        size_t n = 0u;
        for (Atom *p = rule->expr.elems[3]; sj_is_expr(p, "LCons", 3u); p = p->expr.elems[2])
            items[n++] = sj_pattern_instantiate(a, p->expr.elems[1], &b);
        for (size_t i = n; i > 0u; i--)
            premises = sj_expr3(a, "LCons", items[i - 1u], premises);
        /* the rule's formals, instantiated in declaration order: exactly the
         * arguments a certificate's rule instance carries */
        Atom *args = sj_sym(a, "LNil");
        size_t formal_count = sj_list_length(rule->expr.elems[2]);
        Atom **formals = arena_alloc(a, sizeof(Atom *) * (formal_count ? formal_count : 1u));
        if (!formals)
            return sj_incomplete(a, judgment, sj_expr1(a, "lang:step-storage"));
        size_t fn = 0u;
        for (Atom *f = rule->expr.elems[2]; sj_is_expr(f, "LCons", 3u); f = f->expr.elems[2]) {
            Atom *formal = f->expr.elems[1];   /* (Formal "name" arity) */
            Atom *name = sj_is_expr(formal, "Formal", 3u) ? formal->expr.elems[1] : NULL;
            Atom *value = name ? sj_pattern_instantiate(a, sj_expr2(a, "FVar", name), &b) : formal;
            formals[fn++] = value;
        }
        for (size_t i = fn; i > 0u; i--)
            args = sj_expr3(a, "LCons", formals[i - 1u], args);
        Atom *step_items[4] = {sj_sym(a, "GStepV1"), rule->expr.elems[1], args, premises};
        collected[step_count++] = atom_expr(a, step_items, 4u);
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

/* Evidence as `try` shows it: every sort above the written universes is
 * written by its name, as every judgment prints it, wherever the evidence
 * holds a kernel term.  A level is not walked: it can have any number of
 * terms, and only a whole sort has a name. */
static Atom *sj_name_sorts_above(Arena *a, Atom *t) {
    if (!t || t->kind != ATOM_EXPR || t->expr.len == 0u) return t;
    if (sj_is_expr(t, "Sort", 2u)) {
        Atom *named = cetta_prime_regular_kernel_quote_sort_above_v1(a, t);
        return named ? named : t;
    }
    const char *head = sj_head_name(t);
    if (head && strncmp(head, "Level", 5u) == 0) return t;
    Atom **items = NULL;
    for (CettaExprIndex i = 0u; i < t->expr.len; i++) {
        Atom *child = sj_name_sorts_above(a, t->expr.elems[i]);
        if (child == t->expr.elems[i] && !items) continue;
        if (!items) {
            items = arena_alloc(a, sizeof(Atom *) * (size_t)t->expr.len);
            for (CettaExprIndex k = 0u; k < i; k++) items[k] = t->expr.elems[k];
        }
        items[i] = child;
    }
    return items ? atom_expr(a, items, t->expr.len) : t;
}

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
                             evidence ? sj_name_sorts_above(a, evidence)
                                      : atom_unit(a));
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
    if (head == g_builtin_syms.set_colon_signature) {
        SjSetEnv *env = sj_env(space);
        if (!env)
            return sj_undetermined(a, judgment, sj_expr1(a, "set:signature-unavailable"));
        /* What the signature adds to every extended space, in the order it
         * adds it: its declarations, then its definitions' rules. */
        size_t count = env->signature_count + env->own_rule_count;
        Atom **items = arena_alloc(a, sizeof(Atom *) * (count ? count : 1u));
        for (size_t i = 0u; i < env->signature_count; i++) items[i] = env->signature[i];
        for (size_t i = 0u; i < env->own_rule_count; i++)
            items[env->signature_count + i] = env->own_rules[i];
        Atom *value = atom_expr(a, items, (CettaExprLen)count);
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
    if (head == g_builtin_syms.set_colon_numerals)
        return prime_arith_oracle_judge_numerals(a, space, judgment);
    if (head == g_builtin_syms.set_colon_oracle)
        return prime_arith_oracle_judge_declaration(a, space, judgment);
    if (head == g_builtin_syms.set_colon_inductive)
        return sj_set_inductive(a, space, judgment, steps_limited, steps);
    if (head == g_builtin_syms.set_colon_family)
        return sj_set_family(a, space, judgment, steps_limited, steps);
    if (head == g_builtin_syms.set_colon_axiom) {
        if (len != 3u) return sj_refuted(a, judgment, sj_expr1(a, "set:axiom-arity"));
        Atom *axiom_name = judgment->expr.elems[1];
        if (!axiom_name || axiom_name->kind != ATOM_SYMBOL)
            return sj_refuted(a, judgment, sj_expr1(a, "set:axiom-name"));
        if (prime_scoped_judgment_reserved_name(axiom_name))
            return sj_refuted(a, judgment,
                              sj_expr2(a, "set:reserved-name", axiom_name));
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
        if (prime_scoped_judgment_reserved_name(theorem_name))
            return sj_refuted(a, judgment,
                              sj_expr2(a, "set:reserved-name", theorem_name));
        return sj_set_theorem(a, space, judgment, theorem_name,
                              judgment->expr.elems[2], judgment->expr.elems[3],
                              steps_limited, steps);
    }
    if (head == g_builtin_syms.set_colon_native_proof) {
        /* `(set:native-proof name)`, then optionally `rule-constants`, the
         * presentation with every rule as its constant, and a number of
         * steps, as the last operand of a judgment may be. */
        CettaExprIndex next = 2u;
        bool rule_constants = len > next &&
            atom_is_symbol(judgment->expr.elems[next], "rule-constants");
        if (rule_constants) next++;
        if (len > next) {
            Atom *limit = judgment->expr.elems[next];
            if (len != next + 1u || !limit || limit->kind != ATOM_GROUNDED ||
                limit->ground.gkind != GV_INT || limit->ground.ival < 0)
                return sj_refuted(a, judgment,
                                  sj_expr1(a, "set:native-proof-arity"));
            uint64_t given = (uint64_t)limit->ground.ival;
            if (!steps_limited || given < steps) steps = given;
            steps_limited = true;
        }
        if (len < 2u)
            return sj_refuted(a, judgment,
                              sj_expr1(a, "set:native-proof-arity"));
        Atom *theorem_name = judgment->expr.elems[1];
        if (!theorem_name || theorem_name->kind != ATOM_SYMBOL)
            return sj_refuted(a, judgment,
                              sj_expr1(a, "set:native-proof-name"));
        return sj_set_native_proof(a, space, judgment, theorem_name,
                                   rule_constants, steps_limited, steps);
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
    if (head == g_builtin_syms.set_colon_interpret) {
        if (len != 2u)
            return sj_refuted(a, judgment, sj_expr1(a, "set:interpret-arity"));
        Atom *interpretation = judgment->expr.elems[1];
        if (!atom_is_symbol(interpretation, "identity"))
            return sj_undetermined(a, judgment,
                                   sj_expr2(a, "set:interpret-unknown",
                                            interpretation));
        return sj_established(
            a, judgment,
            sj_expr2(a, "SetPublish",
                     sj_expr2(a, "set:interpretation", interpretation)));
    }
    if (head == g_builtin_syms.set_colon_native_link) {
        if (len != 3u)
            return sj_refuted(a, judgment,
                              sj_expr1(a, "set:native-link-arity"));
        return sj_set_native_link(a, space, judgment, judgment->expr.elems[1],
                                  judgment->expr.elems[2], steps_limited, steps);
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
bool prime_scoped_judgment_reserved_name(Atom *name) {
    if (!name || name->kind != ATOM_SYMBOL) return false;
    const char *spelling = atom_name_cstr(name);
    return spelling &&
           (strncmp(spelling, CETTA_PRIME_PROOF_IDENTITY_PREFIX,
                    sizeof CETTA_PRIME_PROOF_IDENTITY_PREFIX - 1u) == 0 ||
            strncmp(spelling, CETTA_PRIME_HOLDS_IDENTITY_PREFIX,
                    sizeof CETTA_PRIME_HOLDS_IDENTITY_PREFIX - 1u) == 0);
}

static Atom *sj_reserved_definition(Arena *a, Space *space, Atom *judgment,
                                    Atom *name) {
    if (prime_scoped_judgment_reserved_name(name))
        return sj_refuted(a, judgment, sj_expr2(a, "set:reserved-name", name));
    const char *reason = cetta_prime_reserved_name_reason(name);
    if (!reason || !space) return NULL;
    Atom **declared = NULL;
    uint32_t count = space_get_declared_types(space, a, name, &declared);
    free(declared);
    if (count == 0u) return NULL;
    return sj_refuted(a, judgment,
                      sj_expr3(a, "set:reserved-name", name, sj_sym(a, reason)));
}

void prime_scoped_judgment_admit(Arena *a, Space *space, Atom *record) {
    /* The numeral readings and oracle declarations of prime_arith_oracle.h
     * are published by their own admission checks, as these are. */
    if (!record || (!sj_is_expr(record, "set:known", SJ_KNOWN_LEN) &&
                    !sj_is_expr(record, "type:rule", 5u) &&
                    !sj_is_expr(record, "set:numerals-of", 4u) &&
                    !sj_is_expr(record, "set:oracle-of", 6u)))
        return;
    sj_admit(a, space, record);
}

/* The digests of computation rules, remembered per thread.  Every covered
 * call reads the space's rules and asks of each whether admission published
 * it, and a digest prints the rule and hashes the text; computed once per
 * rule instead, the cost of a call no longer grows with the number of rules
 * times their size.  An entry is used only for the atom it was computed for
 * and only while that atom is still structurally equal to the copy kept with
 * the digest, so a reused address or a changed atom is digested afresh.  The
 * admitted set itself is not remembered: membership is looked up each time,
 * so admissions and revocations take effect at once. */
typedef struct {
    const Atom *rule;
    Atom *copy;
    char digest[65];
} SjRuleDigest;

typedef struct {
    SjRuleDigest *slots;
    Arena copies;
} SjRuleDigests;

enum { SJ_RULE_DIGEST_SLOTS = 4096u, SJ_RULE_DIGEST_PROBES = 8u };
static _Thread_local SjRuleDigests *t_rule_digests;
static pthread_key_t g_rule_digests_key;
static pthread_once_t g_rule_digests_key_once = PTHREAD_ONCE_INIT;
static bool g_rule_digests_key_ready = false;

static void sj_rule_digests_destroy(void *raw) {
    SjRuleDigests *digests = raw;
    if (!digests) return;
    free(digests->slots);
    arena_free(&digests->copies);
    free(digests);
}

static void sj_rule_digests_make_key(void) {
    g_rule_digests_key_ready =
        pthread_key_create(&g_rule_digests_key, sj_rule_digests_destroy) == 0;
}

static SjRuleDigests *sj_rule_digests_get(void) {
    if (t_rule_digests) return t_rule_digests;
    pthread_once(&g_rule_digests_key_once, sj_rule_digests_make_key);
    if (!g_rule_digests_key_ready) return NULL;
    SjRuleDigests *digests = calloc(1u, sizeof *digests);
    if (!digests) return NULL;
    digests->slots = calloc(SJ_RULE_DIGEST_SLOTS, sizeof *digests->slots);
    arena_init_detached(&digests->copies);
    if (!digests->slots || pthread_setspecific(g_rule_digests_key, digests) != 0) {
        sj_rule_digests_destroy(digests);
        return NULL;
    }
    t_rule_digests = digests;
    return digests;
}

static bool sj_is_rule_atom(Atom *atom) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len == 5u &&
           atom_is_symbol(atom->expr.elems[0], "type:rule");
}

static void sj_rule_digest(Arena *a, Atom *rule, char out[65]) {
    SjRuleDigests *digests = sj_rule_digests_get();
    if (!digests) {
        sj_record_digest(a, rule, out);
        return;
    }
    size_t home = (size_t)(((uintptr_t)rule >> 4) * 11400714819323198485ull) %
                  SJ_RULE_DIGEST_SLOTS;
    size_t empty = SJ_RULE_DIGEST_SLOTS;
    for (size_t probe = 0u; probe < SJ_RULE_DIGEST_PROBES; probe++) {
        SjRuleDigest *entry = &digests->slots[(home + probe) % SJ_RULE_DIGEST_SLOTS];
        if (!entry->rule) {
            if (empty == SJ_RULE_DIGEST_SLOTS) empty = (home + probe) % SJ_RULE_DIGEST_SLOTS;
            continue;
        }
        if (entry->rule == rule && atom_eq(entry->copy, rule)) {
            memcpy(out, entry->digest, sizeof entry->digest);
            return;
        }
    }
    sj_record_digest(a, rule, out);
    if (empty == SJ_RULE_DIGEST_SLOTS) return;
    Atom *copy = atom_deep_copy(&digests->copies, rule);
    if (!copy) return;
    SjRuleDigest *entry = &digests->slots[empty];
    entry->rule = rule;
    entry->copy = copy;
    memcpy(entry->digest, out, sizeof entry->digest);
}

bool prime_scoped_judgment_admitted(Arena *a, Space *space, Atom *record) {
    if (!a || !space || !record) return false;
    /* What the signature's definitions published is admitted wherever the
     * signature is: the extended space of every theory carries it. */
    if (sj_signature_owns(&g_set_env, record)) return true;
    const Space *root = space;
    while (root->overlay_base) root = root->overlay_base;
    char digest[65];
    if (sj_is_rule_atom(record) && !atom_has_vars(record))
        sj_rule_digest(a, record, digest);
    else
        sj_record_digest(a, record, digest);
    return sj_admitted_contains(space_instance_id(root), digest);
}

/* ------------------------------------------------------------------------ */
/* What a refutation by the conversion algorithm assumes.  The refutation    */
/* is `not_equal_of_unrelated`, whose hypothesis is that the algorithm is    */
/* complete for the package.  Lean proves that for the bare tower and for    */
/* the object package (see regular_unrelated_refuted).  A comparison that    */
/* meets declarations outside those assumes it for them, and the reason      */
/* names which kinds: the constants of the two sides, closed under the       */
/* declared types of the constants and under the statements of the           */
/* definitions and families they come from, are classified by the record or */
/* the declaration that introduced them.                                     */
/*   declared-constants  declared at a type, with no record and no datatype  */
/*                       of their own                                        */
/*   declared-datatypes  a type declared by `set:inductive`, its             */
/*                       constructors, recursor and induction principle      */
/*   families            a constant or law of a `set:family`                 */
/*   definitions         one equation, no recursion (`non-recursive`)        */
/*   recursion           structural recursion, directly or scrutinee-first   */
/*   bound               admitted by a bound                                 */
/*   set-solution        admitted on evidence of a solution                  */
/* The signature's constants, the set theory every space has, are not       */
/* counted.  NULL when nothing is outside the proven packages.              */
/* ------------------------------------------------------------------------ */

enum {
    SJ_EXT_CONSTANTS, SJ_EXT_DATATYPES, SJ_EXT_FAMILIES, SJ_EXT_DEFINITIONS,
    SJ_EXT_RECURSION, SJ_EXT_BOUND, SJ_EXT_SOLUTION, SJ_EXT_COUNT
};

static const char *const SJ_EXT_NAMES[SJ_EXT_COUNT] = {
    "declared-constants", "declared-datatypes", "families", "definitions",
    "recursion", "bound", "set-solution",
};

typedef struct {
    SjProofState *st;
    Atom **seen;
    size_t count;
    size_t cap;
    bool kinds[SJ_EXT_COUNT];
} SjExtensionWalk;

static void sj_ext_walk(SjExtensionWalk *w, Atom *t);

static bool sj_ext_first_visit(SjExtensionWalk *w, Atom *name) {
    for (size_t i = 0u; i < w->count; i++)
        if (atom_eq(w->seen[i], name)) return false;
    if (w->count == w->cap) {
        size_t next = w->cap ? 2u * w->cap : 16u;
        Atom **grown = arena_alloc(w->st->arena, sizeof(Atom *) * next);
        if (!grown) return false;
        for (size_t i = 0u; i < w->count; i++) grown[i] = w->seen[i];
        w->seen = grown;
        w->cap = next;
    }
    w->seen[w->count++] = name;
    return true;
}

static bool sj_ext_signature_constant(SjSetEnv *env, Atom *name) {
    for (size_t i = 0u; i < env->signature_count; i++)
        if (atom_eq(env->signature[i]->expr.elems[1], name)) return true;
    return false;
}

/* Whether `name` is a constructor, the recursor or the induction principle
 * of the datatype whose record is `record`. */
static bool sj_ext_part_of_datatype(Atom *record, Atom *name) {
    Atom *decl = record->expr.elems[SJ_KNOWN_PROP];
    if (sj_is_expr(decl, "inductive", decl && decl->kind == ATOM_EXPR ? decl->expr.len : 0u))
        for (CettaExprIndex i = 2u; i < decl->expr.len; i++)
            if (sj_is_expr(decl->expr.elems[i], ":", 3u) &&
                atom_eq(decl->expr.elems[i]->expr.elems[1], name))
                return true;
    const char *type = atom_name_cstr(record->expr.elems[SJ_KNOWN_NAME]);
    const char *spelling = atom_name_cstr(name);
    if (!type || !spelling) return false;
    size_t len = strlen(type);
    return strncmp(spelling, type, len) == 0 &&
           (strcmp(spelling + len, "-rec") == 0 || strcmp(spelling + len, "-ind") == 0);
}

/* The datatypes a term mentions, by their records. */
static bool sj_ext_names_part(SjExtensionWalk *w, Atom *t, Atom *name) {
    if (!t) return false;
    if (t->kind == ATOM_SYMBOL) {
        Atom *record = sj_inductive_record(w->st, t);
        return record && sj_ext_part_of_datatype(record, name);
    }
    if (t->kind != ATOM_EXPR) return false;
    for (CettaExprIndex i = 0u; i < t->expr.len; i++)
        if (sj_ext_names_part(w, t->expr.elems[i], name)) return true;
    return false;
}

static void sj_ext_symbol(SjExtensionWalk *w, Atom *name) {
    SjProofState *st = w->st;
    if (prime_scoped_judgment_reserved_name(name) || !sj_ext_first_visit(w, name)) return;
    if (sj_ext_signature_constant(st->env, name) || sj_signature_defines(st->env, name))
        return;
    bool ambiguous = false;
    Atom *record = sj_known_lookup(st->arena, st->env, st->user_space, name, &ambiguous);
    if (record && !ambiguous) {
        Atom *origin = record->expr.elems[SJ_KNOWN_ORIGIN];
        if (atom_is_symbol(origin, "signature")) return;
        if (atom_is_symbol(origin, "inductive")) {
            w->kinds[SJ_EXT_DATATYPES] = true;
            sj_ext_walk(w, record->expr.elems[SJ_KNOWN_PROP]);
            return;
        }
        if (atom_is_symbol(origin, "family") || atom_is_symbol(origin, "family-law")) {
            w->kinds[SJ_EXT_FAMILIES] = true;
            sj_ext_walk(w, record->expr.elems[SJ_KNOWN_PROP]);
            return;
        }
        if (atom_is_symbol(origin, "definition")) {
            Atom *evidence = record->expr.elems[SJ_KNOWN_PROOF];
            Atom *termination = evidence && evidence->kind == ATOM_EXPR &&
                                        evidence->expr.len >= 3u
                ? evidence->expr.elems[2] : NULL;
            const char *kind = termination && termination->kind == ATOM_EXPR &&
                                       termination->expr.len >= 1u
                ? atom_name_cstr(termination->expr.elems[0]) : NULL;
            if (kind && strcmp(kind, "non-recursive") == 0)
                w->kinds[SJ_EXT_DEFINITIONS] = true;
            else if (kind && strcmp(kind, "bound") == 0)
                w->kinds[SJ_EXT_BOUND] = true;
            else if (kind && strcmp(kind, "set-solution") == 0)
                w->kinds[SJ_EXT_SOLUTION] = true;
            else
                w->kinds[SJ_EXT_RECURSION] = true;
            sj_ext_walk(w, record->expr.elems[SJ_KNOWN_PROP]);
            return;
        }
        return;
    }
    /* No record: a constant the space declares at a type, unless it is part
     * of a datatype its type mentions.  Only names the declared route types
     * are constants of the comparison: an operator of the language with a
     * runtime type, such as `=` or `*`, is not. */
    if (!sj_constant_type(st, name)) return;
    Atom **declared = NULL;
    SpaceDeclaredTypeLookupCost cost = {0};
    uint32_t count = space_get_declared_types_costed(
        &st->env->overlay, st->arena, name, &declared, &cost);
    bool part = false;
    for (uint32_t i = 0u; i < count; i++) {
        part = part || sj_ext_names_part(w, declared[i], name);
        sj_ext_walk(w, declared[i]);
    }
    free(declared);
    if (count > 0u && !part) w->kinds[SJ_EXT_CONSTANTS] = true;
}

static void sj_ext_walk(SjExtensionWalk *w, Atom *t) {
    if (!t) return;
    if (t->kind == ATOM_SYMBOL) {
        sj_ext_symbol(w, t);
        return;
    }
    if (t->kind != ATOM_EXPR) return;
    for (CettaExprIndex i = 0u; i < t->expr.len; i++) sj_ext_walk(w, t->expr.elems[i]);
}

Atom *prime_scoped_judgment_completeness_assumed(Arena *a, Space *space, Atom *left,
                                                 Atom *right) {
    SjSetEnv *env = sj_env(space);
    if (!a || !env) return NULL;
    SjProofState st;
    sj_proof_state_init(&st, a, space, env, false, 0u, false, true);
    SjExtensionWalk w = {.st = &st, .seen = NULL, .count = 0u, .cap = 0u};
    sj_ext_walk(&w, left);
    sj_ext_walk(&w, right);
    Atom *items[SJ_EXT_COUNT + 1u];
    size_t n = 0u;
    items[n++] = sj_sym(a, "assumes-completeness");
    for (size_t k = 0u; k < SJ_EXT_COUNT; k++)
        if (w.kinds[k]) items[n++] = sj_sym(a, SJ_EXT_NAMES[k]);
    return n > 1u ? atom_expr(a, items, (CettaExprLen)n) : NULL;
}

/* ------------------------------------------------------------------------ */
/* A refutation that needs no completeness.  Lean separates the empty list  */
/* from every non-empty one at the reading, so the refutation of `nil`      */
/* against `cons a l` rests on that theorem and assumes nothing about the   */
/* conversion algorithm (ExecutableModel/ObjectChurchLists.lean, below      */
/* Mettapedia.Languages.MeTTa.PrimeCandidates):                             */
/*   DeclarationBased.CertifiedTransformProgram.ExecutableModel.CodeModel   */
/*   .nil_not_equal_cons (a l : CTm Tower.Head 0) :                         */
/*     ¬ CEqual (dataChurch listDecl) .nil cnil (ccons a l) clist           */
/* that is, for closed `a` and `l`, no equality of `nil` and `cons a l` at  */
/* the lists is derivable in the object package with the lists of numbers. */
/* It is cited only for exactly that comparison: one side `nil`, the other  */
/* `cons` of two closed terms built from the constructors of the numbers    */
/* and the lists, in a space whose numbers and lists are admitted word for  */
/* word as Lean declares them (the object package's `num`, `zero`, `suc`;   */
/* `listDecl`'s `list`, `nil`, `cons`), each constructor declared at its    */
/* constructor type only.  Every other refutation at declared datatypes     */
/* keeps its (assumes-completeness ...).                                    */
/* ------------------------------------------------------------------------ */

static const char *const SJ_NIL_NOT_EQUAL_CONS =
    "DeclarationBased.CertifiedTransformProgram.ExecutableModel.CodeModel.nil_not_equal_cons";

/* The datatypes and constructor types of the lists of numbers, as Lean
 * declares them. */
static const char *const SJ_LISTS_OF_NUMBERS[][2] = {
    {"num", "(inductive (u 0) (: zero num) (: suc (-> num num)))"},
    {"list", "(inductive (u 0) (: nil list) (: cons (-> num list list)))"},
};
static const char *const SJ_LISTS_CONSTRUCTORS[][2] = {
    {"zero", "num"}, {"suc", "(-> num num)"},
    {"nil", "list"}, {"cons", "(-> num list list)"},
};

static Atom *sj_read_one(Arena *a, const char *text) {
    Atom **atoms = NULL;
    int count = parse_metta_text(text, a, &atoms);
    Atom *atom = count == 1 && atoms ? atoms[0] : NULL;
    free(atoms);
    return atom;
}

/* A closed term built from zero, suc, nil and cons, each at its arity. */
static bool sj_lists_constructor_term(Atom *t) {
    Atom **stack = NULL;
    size_t len = 0u;
    size_t cap = 0u;
    bool ok = true;
    for (Atom *cur = t; ok && cur;
         cur = len ? stack[--len] : NULL) {
        if (atom_is_symbol(cur, "zero") || atom_is_symbol(cur, "nil")) continue;
        bool suc = sj_is_expr(cur, "suc", 2u);
        if (!suc && !sj_is_expr(cur, "cons", 3u)) {
            ok = false;
            break;
        }
        if (len + 2u > cap) {
            size_t next = cap ? cap * 2u : 32u;
            Atom **grown = realloc(stack, next * sizeof *grown);
            if (!grown) {
                ok = false;
                break;
            }
            stack = grown;
            cap = next;
        }
        for (CettaExprIndex i = 1u; i < cur->expr.len; i++) stack[len++] = cur->expr.elems[i];
    }
    free(stack);
    return ok;
}

const char *prime_scoped_judgment_refutation_without_completeness(
    Arena *a, Space *space, Atom *left, Atom *right) {
    Atom *nonempty = atom_is_symbol(left, "nil") ? right
                     : atom_is_symbol(right, "nil") ? left : NULL;
    if (!a || !sj_is_expr(nonempty, "cons", 3u) || !sj_lists_constructor_term(nonempty))
        return NULL;
    SjSetEnv *env = sj_env(space);
    if (!env) return NULL;
    SjProofState st;
    sj_proof_state_init(&st, a, space, env, false, 0u, false, true);
    for (size_t k = 0u; k < sizeof SJ_LISTS_OF_NUMBERS / sizeof SJ_LISTS_OF_NUMBERS[0]; k++) {
        Atom *record = sj_inductive_record(&st, sj_sym(a, SJ_LISTS_OF_NUMBERS[k][0]));
        Atom *expected = sj_read_one(a, SJ_LISTS_OF_NUMBERS[k][1]);
        if (!record || !expected || !prime_scoped_judgment_admitted(a, space, record) ||
            !atom_eq(record->expr.elems[SJ_KNOWN_PROP], expected))
            return NULL;
    }
    for (size_t k = 0u; k < sizeof SJ_LISTS_CONSTRUCTORS / sizeof SJ_LISTS_CONSTRUCTORS[0]; k++) {
        Atom **declared = NULL;
        SpaceDeclaredTypeLookupCost cost = {0};
        uint32_t count = space_get_declared_types_costed(
            &env->overlay, a, sj_sym(a, SJ_LISTS_CONSTRUCTORS[k][0]), &declared, &cost);
        Atom *expected = sj_read_one(a, SJ_LISTS_CONSTRUCTORS[k][1]);
        bool exact = count == 1u && expected && atom_eq(declared[0], expected);
        free(declared);
        if (!exact) return NULL;
    }
    return SJ_NIL_NOT_EQUAL_CONS;
}

#define _GNU_SOURCE
#include "prime_arith_oracle.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#if CETTA_BUILD_WITH_GMP
#include <gmp.h>
#endif

#include "native_sha256.h"
#include "parser.h"
#include "prime_scoped_judgments.h"
#include "prime_semantics.h"

/* A test build plants a backend that adds wrongly at large values; the
 * audit, the residue check and the differential test must catch it. */
#ifndef CETTA_PRIME_ORACLE_PLANTED_FAULT
#define CETTA_PRIME_ORACLE_PLANTED_FAULT 0
#endif

/* ------------------------------------------------------------------------ */
/* Readings, operations and backends                                         */
/* ------------------------------------------------------------------------ */

typedef enum {
    READ_NONE = 0,
    READ_POSITIVE,
    READ_NATURAL,
    READ_INTEGER,
    READ_FRACTION,
    READ_ORDERING,
    READ_QUOTREM,
} ReadingKind;

static const struct {
    const char *name;
    ReadingKind kind;
    size_t ctors;
} k_reading_kinds[] = {
    {"positive", READ_POSITIVE, 3u},
    {"natural", READ_NATURAL, 2u},
    {"integer", READ_INTEGER, 3u},
    {"fraction", READ_FRACTION, 1u},
    {"ordering", READ_ORDERING, 3u},
    {"quotient-remainder", READ_QUOTREM, 1u},
};

typedef struct {
    Atom *type;
    ReadingKind kind;
    Atom *ctor[3];
    /* The readings of the fields' types: the positive type of a natural or
     * integer type, the integer and positive types of a fraction type, the
     * natural type of a quotient-remainder type. */
    int part[2];
} Reading;

typedef enum {
    NATIVE_ADD,
    NATIVE_SUB,
    NATIVE_SUB_TRUNCATED,
    NATIVE_MUL,
    NATIVE_COMPARE,
    NATIVE_DIVMOD,
    NATIVE_GCD,
    NATIVE_NORMALIZE,
} NativeOp;

static const struct {
    const char *name;
    NativeOp op;
} k_natives[] = {
    {"add", NATIVE_ADD},
    {"sub", NATIVE_SUB},
    {"sub-truncated", NATIVE_SUB_TRUNCATED},
    {"mul", NATIVE_MUL},
    {"compare", NATIVE_COMPARE},
    {"divmod", NATIVE_DIVMOD},
    {"gcd", NATIVE_GCD},
    {"normalize", NATIVE_NORMALIZE},
};

typedef enum { BACKEND_GMP, BACKEND_UINT64 } Backend;

static const char *backend_name(Backend backend) {
    return backend == BACKEND_GMP ? "gmp-mpz" : "uint64";
}

static const char *native_name(NativeOp op) {
    for (size_t i = 0u; i < sizeof k_natives / sizeof k_natives[0]; i++)
        if (k_natives[i].op == op) return k_natives[i].name;
    return "?";
}

/* An oracle rests on two trusts, and its records name them apart:
 *   (trusted backend native)  that the backend computes its native operation
 *                             correctly, as GMP or the machine word does;
 *   (spec-agreement ...)      that the native operation agrees with the
 *                             operation's declared equations:
 *       (proved theorem ...)  proved in Lean, below
 *                             Mettapedia.Languages.MeTTa.PrimeCandidates, for
 *                             the two operations of `Trinity.Arith.Binary`
 *                             whose meaning the Lean instance gives;
 *       (tested (admission-calls n))  otherwise: only tested, by the n small
 *                             calls checked at admission.
 * The standing tests, which check both trusts (the differential test, the
 * audit and the residue check), are the target named by (tests ...). */
static const char *const k_lean_padd =
    "Trinity.Arith.Oracle.realization_meets Trinity.Arith.Oracle.conservative_padd";
static const char *const k_lean_pmul =
    "Trinity.Arith.Oracle.realization_meets Trinity.Arith.Oracle.conservative_pmul";
static const char *const k_tests = "test-prime-arith-oracle";

/* The sample values of each argument checked at admission; every pair is
 * one call. */
enum { ORACLE_SAMPLE_VALUES = 3, ORACLE_ADMISSION_CALLS = ORACLE_SAMPLE_VALUES * ORACLE_SAMPLE_VALUES };

typedef struct {
    Atom *op;
    NativeOp native;
    Backend backend;
    size_t argc;
    int arg[2];
    int result;
    const char *lean;
} Oracle;

/* ------------------------------------------------------------------------ */
/* Small helpers                                                             */
/* ------------------------------------------------------------------------ */

static Atom *sym(Arena *a, const char *name) { return atom_symbol(a, name); }

static bool is_expr(Atom *t, size_t len) {
    return t && t->kind == ATOM_EXPR && t->expr.len == (CettaExprLen)len;
}

static bool is_expr_head(Atom *t, const char *head, size_t len) {
    return is_expr(t, len) && atom_is_symbol_named(t->expr.elems[0], head);
}

static bool is_sym(Atom *t) { return t && t->kind == ATOM_SYMBOL; }

static Atom *verdict(Arena *a, const char *status, Atom *judgment, Atom *evidence) {
    Atom *items[4] = {sym(a, "PrimeVerdict"), sym(a, status), judgment,
                      evidence ? evidence : atom_unit(a)};
    return atom_expr(a, items, 4u);
}

static Atom *refuted(Arena *a, Atom *judgment, const char *reason, Atom *detail) {
    return verdict(a, "Refuted", judgment,
                   detail ? atom_expr2(a, sym(a, reason), detail)
                          : atom_expr(a, (Atom *[]){sym(a, reason)}, 1u));
}

static Atom *undetermined(Arena *a, Atom *judgment, const char *reason, Atom *detail) {
    return verdict(a, "Undetermined", judgment,
                   detail ? atom_expr2(a, sym(a, reason), detail)
                          : atom_expr(a, (Atom *[]){sym(a, reason)}, 1u));
}

/* The admitted record of `pattern` in `space`, the first one the index
 * yields, or NULL.  A record not published by admission is not one. */
static Atom *admitted_record(Arena *a, Space *space, Atom *pattern,
                             bool (*fits)(Atom *, void *), void *context) {
    CettaIndex *candidates = NULL;
    CettaIndex count = space_match_candidates64(space, pattern, &candidates);
    Atom *found = NULL;
    for (CettaIndex i = 0u; i < count && !found; i++) {
        Atom *atom = space_match_candidate_at64(space, candidates[i]);
        if (atom && fits(atom, context) &&
            prime_scoped_judgment_admitted(a, space, atom))
            found = atom;
    }
    free(candidates);
    return found;
}

typedef struct {
    Atom *name;
    const char *origin;
} KnownQuery;

static bool fits_known(Atom *atom, void *raw) {
    KnownQuery *query = raw;
    return is_expr_head(atom, "set:known", 8u) &&
           atom_eq(atom->expr.elems[1], query->name) &&
           atom_is_symbol_named(atom->expr.elems[3], query->origin);
}

/* The admitted `set:known` record of `name` with origin `origin`. */
static Atom *known_record(Arena *a, Space *space, Atom *name, const char *origin) {
    Atom *items[8] = {sym(a, "set:known"), name, atom_var(a, "p"), sym(a, origin),
                      atom_var(a, "f"), atom_var(a, "d"), atom_var(a, "r"),
                      atom_var(a, "s")};
    KnownQuery query = {name, origin};
    return admitted_record(a, space, atom_expr(a, items, 8u), fits_known, &query);
}

static bool fits_head_name(Atom *atom, void *raw) {
    Atom **want = raw;
    return atom && atom->kind == ATOM_EXPR && atom->expr.len >= 2u &&
           atom_eq(atom->expr.elems[0], want[0]) && atom_eq(atom->expr.elems[1], want[1]);
}

/* The admitted reading record of `type`: (set:numerals-of type reading parts). */
static Atom *reading_record(Arena *a, Space *space, Atom *type) {
    Atom *head = sym(a, "set:numerals-of");
    Atom *items[4] = {head, type, atom_var(a, "r"), atom_var(a, "p")};
    Atom *want[2] = {head, type};
    return admitted_record(a, space, atom_expr(a, items, 4u), fits_head_name, want);
}

/* The admitted oracle record of `op`:
 * (set:oracle-of op spec backend signature (spec-agreement ...)). */
static Atom *oracle_record(Arena *a, Space *space, Atom *op) {
    Atom *head = sym(a, "set:oracle-of");
    Atom *items[6] = {head, op, atom_var(a, "e"), atom_var(a, "b"), atom_var(a, "s"),
                      atom_var(a, "l")};
    Atom *want[2] = {head, op};
    return admitted_record(a, space, atom_expr(a, items, 6u), fits_head_name, want);
}

/* ------------------------------------------------------------------------ */
/* Readings                                                                  */
/* ------------------------------------------------------------------------ */

static ReadingKind reading_kind_named(Atom *name, size_t *ctors) {
    for (size_t i = 0u; i < sizeof k_reading_kinds / sizeof k_reading_kinds[0]; i++)
        if (atom_is_symbol_named(name, k_reading_kinds[i].name)) {
            if (ctors) *ctors = k_reading_kinds[i].ctors;
            return k_reading_kinds[i].kind;
        }
    return READ_NONE;
}

/* (-> A ... R) as its argument and result types. */
static bool arrow_parts(Atom *type, Atom **parts, size_t max, size_t *count) {
    if (!type || type->kind != ATOM_EXPR || type->expr.len < 3u ||
        !atom_is_symbol_named(type->expr.elems[0], "->") ||
        type->expr.len - 1u > max)
        return false;
    for (CettaExprIndex i = 1u; i < type->expr.len; i++) parts[i - 1u] = type->expr.elems[i];
    *count = type->expr.len - 1u;
    return true;
}

typedef struct {
    Reading *items;
    size_t count;
    size_t cap;
} Readings;

static int readings_find(const Readings *readings, Atom *type) {
    for (size_t i = 0u; i < readings->count; i++)
        if (atom_eq(readings->items[i].type, type)) return (int)i;
    return -1;
}

/* The constructors `(: c type)` of the admitted inductive declaration of
 * `type`, or 0 when there is none. */
static size_t inductive_ctors(Arena *a, Space *space, Atom *type, Atom ***out) {
    Atom *record = known_record(a, space, type, "inductive");
    if (!record) return 0u;
    Atom *decl = record->expr.elems[2];
    if (!decl || decl->kind != ATOM_EXPR || decl->expr.len < 2u ||
        !atom_is_symbol_named(decl->expr.elems[0], "inductive"))
        return 0u;
    *out = decl->expr.elems + 2;
    return decl->expr.len - 2u;
}

/* Check that `ctors` of `type` have the shape of the reading `kind`, and
 * find the readings of the fields' types.  Returns NULL when they do, or
 * the reason they do not. */
static const char *reading_shape(const Readings *known, Atom *type, ReadingKind kind,
                                 Atom **names, size_t name_count, Atom **ctors,
                                 size_t ctor_count, int part[2]) {
    part[0] = part[1] = -1;
    if (ctor_count != name_count) return "set:numerals-constructor-count";
    for (size_t i = 0u; i < ctor_count; i++) {
        Atom *c = ctors[i];
        if (!is_expr_head(c, ":", 3u) || !atom_eq(c->expr.elems[1], names[i]))
            return "set:numerals-constructor-names";
    }
    Atom *fields[4];
    size_t count = 0u;
    switch (kind) {
    case READ_POSITIVE:
        if (!atom_eq(ctors[0]->expr.elems[2], type)) return "set:numerals-constructor-type";
        for (size_t i = 1u; i < 3u; i++)
            if (!arrow_parts(ctors[i]->expr.elems[2], fields, 4u, &count) || count != 2u ||
                !atom_eq(fields[0], type) || !atom_eq(fields[1], type))
                return "set:numerals-constructor-type";
        return NULL;
    case READ_ORDERING:
        for (size_t i = 0u; i < 3u; i++)
            if (!atom_eq(ctors[i]->expr.elems[2], type)) return "set:numerals-constructor-type";
        return NULL;
    case READ_NATURAL:
    case READ_INTEGER: {
        if (!atom_eq(ctors[0]->expr.elems[2], type)) return "set:numerals-constructor-type";
        Atom *positive = NULL;
        for (size_t i = 1u; i < ctor_count; i++) {
            if (!arrow_parts(ctors[i]->expr.elems[2], fields, 4u, &count) || count != 2u ||
                !atom_eq(fields[1], type))
                return "set:numerals-constructor-type";
            if (positive && !atom_eq(positive, fields[0])) return "set:numerals-constructor-type";
            positive = fields[0];
        }
        int p = readings_find(known, positive);
        if (p < 0 || known->items[p].kind != READ_POSITIVE) return "set:numerals-part-unread";
        part[0] = p;
        return NULL;
    }
    case READ_FRACTION: {
        if (!arrow_parts(ctors[0]->expr.elems[2], fields, 4u, &count) || count != 3u ||
            !atom_eq(fields[2], type))
            return "set:numerals-constructor-type";
        int z = readings_find(known, fields[0]);
        int d = readings_find(known, fields[1]);
        if (z < 0 || known->items[z].kind != READ_INTEGER || d < 0 ||
            known->items[d].kind != READ_POSITIVE ||
            known->items[z].part[0] != d)
            return "set:numerals-part-unread";
        part[0] = z;
        part[1] = d;
        return NULL;
    }
    case READ_QUOTREM: {
        if (!arrow_parts(ctors[0]->expr.elems[2], fields, 4u, &count) || count != 3u ||
            !atom_eq(fields[2], type) || !atom_eq(fields[0], fields[1]))
            return "set:numerals-constructor-type";
        int n = readings_find(known, fields[0]);
        if (n < 0 || known->items[n].kind != READ_NATURAL) return "set:numerals-part-unread";
        part[0] = n;
        return NULL;
    }
    default:
        return "set:numerals-kind";
    }
}

static bool readings_push(Readings *readings, Reading reading) {
    if (readings->count == readings->cap) {
        size_t next = readings->cap ? readings->cap * 2u : 8u;
        Reading *grown = realloc(readings->items, sizeof(Reading) * next);
        if (!grown) return false;
        readings->items = grown;
        readings->cap = next;
    }
    readings->items[readings->count++] = reading;
    return true;
}

/* Every admitted reading of `space` that still fits its type's admitted
 * declaration.  A reading names the readings of its parts by type, and
 * those are admitted first, so one pass in dependency order finds them. */
static void readings_collect(Arena *a, Space *space, Readings *out) {
    Atom *items[4] = {sym(a, "set:numerals-of"), atom_var(a, "t"), atom_var(a, "r"),
                      atom_var(a, "p")};
    Atom *pattern = atom_expr(a, items, 4u);
    CettaIndex *candidates = NULL;
    CettaIndex count = space_match_candidates64(space, pattern, &candidates);
    bool progress = true;
    bool *taken = calloc(count ? count : 1u, sizeof(bool));
    if (!taken) {
        free(candidates);
        return;
    }
    while (progress) {
        progress = false;
        for (CettaIndex i = 0u; i < count; i++) {
            if (taken[i]) continue;
            Atom *record = space_match_candidate_at64(space, candidates[i]);
            if (!is_expr_head(record, "set:numerals-of", 4u) ||
                !is_sym(record->expr.elems[1]) ||
                readings_find(out, record->expr.elems[1]) >= 0)
                continue;
            Atom *spec = record->expr.elems[2];
            size_t want = 0u;
            ReadingKind kind = spec && spec->kind == ATOM_EXPR && spec->expr.len >= 2u
                ? reading_kind_named(spec->expr.elems[0], &want) : READ_NONE;
            if (kind == READ_NONE || spec->expr.len - 1u != want) continue;
            Atom **ctors = NULL;
            size_t ctor_count = inductive_ctors(a, space, record->expr.elems[1], &ctors);
            Reading reading = {.type = record->expr.elems[1], .kind = kind};
            if (reading_shape(out, reading.type, kind, spec->expr.elems + 1, want, ctors,
                              ctor_count, reading.part))
                continue;
            if (!prime_scoped_judgment_admitted(a, space, record)) {
                taken[i] = true;
                continue;
            }
            for (size_t c = 0u; c < want; c++) reading.ctor[c] = spec->expr.elems[1u + c];
            if (!readings_push(out, reading)) break;
            taken[i] = true;
            progress = true;
        }
    }
    free(taken);
    free(candidates);
}

/* ------------------------------------------------------------------------ */
/* The specification: the admitted equations of an operation and of every    */
/* definition they call.                                                     */
/* ------------------------------------------------------------------------ */

typedef struct {
    Atom **names;
    Atom **props;
    size_t count;
    size_t cap;
} Cone;

static void cone_free(Cone *cone) {
    free(cone->names);
    free(cone->props);
    memset(cone, 0, sizeof *cone);
}

static bool cone_has(const Cone *cone, Atom *name) {
    for (size_t i = 0u; i < cone->count; i++)
        if (atom_eq(cone->names[i], name)) return true;
    return false;
}

static bool cone_push(Cone *cone, Atom *name, Atom *prop) {
    if (cone->count == cone->cap) {
        size_t next = cone->cap ? cone->cap * 2u : 16u;
        Atom **names = realloc(cone->names, sizeof(Atom *) * next);
        if (!names) return false;
        cone->names = names;
        Atom **props = realloc(cone->props, sizeof(Atom *) * next);
        if (!props) return false;
        cone->props = props;
        cone->cap = next;
    }
    cone->names[cone->count] = name;
    cone->props[cone->count] = prop;
    cone->count++;
    return true;
}

static bool cone_visit_symbols(Arena *a, Space *space, Cone *cone, Atom *term);

static bool cone_add(Arena *a, Space *space, Cone *cone, Atom *name) {
    if (!is_sym(name) || cone_has(cone, name)) return true;
    Atom *record = known_record(a, space, name, "definition");
    if (!record) return true;
    Atom *prop = record->expr.elems[2];
    if (!prop || prop->kind != ATOM_EXPR || prop->expr.len < 3u ||
        !atom_is_symbol_named(prop->expr.elems[0], "define"))
        return false;
    if (!cone_push(cone, name, prop)) return false;
    for (CettaExprIndex i = 2u; i < prop->expr.len; i++)
        if (!cone_visit_symbols(a, space, cone, prop->expr.elems[i])) return false;
    return true;
}

static bool cone_visit_symbols(Arena *a, Space *space, Cone *cone, Atom *term) {
    if (!term) return true;
    if (term->kind == ATOM_SYMBOL) return cone_add(a, space, cone, term);
    if (term->kind != ATOM_EXPR) return true;
    for (CettaExprIndex i = 0u; i < term->expr.len; i++)
        if (!cone_visit_symbols(a, space, cone, term->expr.elems[i])) return false;
    return true;
}

typedef struct {
    char **items;
    size_t count;
    size_t cap;
} Texts;

static bool texts_push(Texts *texts, char *text) {
    if (!text) return false;
    if (texts->count == texts->cap) {
        size_t next = texts->cap ? texts->cap * 2u : 32u;
        char **grown = realloc(texts->items, sizeof(char *) * next);
        if (!grown) return false;
        texts->items = grown;
        texts->cap = next;
    }
    texts->items[texts->count++] = text;
    return true;
}

static int text_order(const void *x, const void *y) {
    return strcmp(*(char *const *)x, *(char *const *)y);
}

/* The admitted computation rules of `name`, as text, in a fixed order. */
static bool rule_texts(Arena *a, Space *space, Atom *name, Texts *out) {
    Atom *items[5] = {sym(a, "type:rule"), name, atom_var(a, "n"), atom_var(a, "p"),
                      atom_var(a, "r")};
    CettaIndex *candidates = NULL;
    CettaIndex count = space_match_candidates64(space, atom_expr(a, items, 5u), &candidates);
    size_t first = out->count;
    bool ok = true;
    for (CettaIndex i = 0u; i < count && ok; i++) {
        Atom *rule = space_match_candidate_at64(space, candidates[i]);
        if (!is_expr_head(rule, "type:rule", 5u) || !atom_eq(rule->expr.elems[1], name) ||
            !prime_scoped_judgment_admitted(a, space, rule))
            continue;
        ok = texts_push(out, atom_to_string(a, rule));
    }
    free(candidates);
    if (ok && out->count > first)
        qsort(out->items + first, out->count - first, sizeof(char *), text_order);
    return ok;
}

/* The specification of `op`: its admitted definition and those its
 * equations reach, with the admitted computation rules of each, and their
 * digest.  A rule removed or added changes the digest, and with it the
 * oracle stops: it is conservative over the rules that compute now.  False
 * when `op` has no admitted definition. */
static bool specification(Arena *a, Space *space, Atom *op, Cone *cone, char digest[65]) {
    memset(cone, 0, sizeof *cone);
    if (!cone_add(a, space, cone, op) || cone->count == 0u ||
        !atom_eq(cone->names[0], op)) {
        cone_free(cone);
        return false;
    }
    Texts texts = {0};
    bool ok = true;
    for (size_t i = 0u; i < cone->count && ok; i++)
        ok = texts_push(&texts, atom_to_string(a, cone->props[i])) &&
             rule_texts(a, space, cone->names[i], &texts);
    size_t total = 0u;
    for (size_t i = 0u; i < texts.count; i++) total += strlen(texts.items[i]) + 1u;
    char *text = ok ? malloc(total + 1u) : NULL;
    if (!text) {
        free(texts.items);
        cone_free(cone);
        return false;
    }
    size_t at = 0u;
    for (size_t i = 0u; i < texts.count; i++) {
        size_t len = strlen(texts.items[i]);
        memcpy(text + at, texts.items[i], len);
        at += len;
        text[at++] = '\n';
    }
    cetta_native_sha256_hex((const uint8_t *)text, at, digest);
    free(text);
    free(texts.items);
    return true;
}

/* ------------------------------------------------------------------------ */
/* Where the Lean instance applies: the operation's specification is the     */
/* one of `Trinity.Arith.Binary`, word for word up to the names of pattern   */
/* variables, over the same datatypes.                                       */
/* ------------------------------------------------------------------------ */

static const char *const k_binary_definitions[][2] = {
    {"psucc", "(define (-> pos pos) (= (psucc one) (bit0 one)) (= (psucc (bit0 $p)) (bit1 $p)) "
              "(= (psucc (bit1 $p)) (bit0 (psucc $p))))"},
    {"phalf", "(define (-> pos nat) (= (phalf one) n0) (= (phalf (bit0 $p)) (npos $p)) "
              "(= (phalf (bit1 $p)) (npos $p)))"},
    {"nhalf", "(define (-> nat nat) (= (nhalf n0) n0) (= (nhalf (npos $p)) (phalf $p)))"},
    {"nsucc-pos", "(define (-> nat pos) (= (nsucc-pos n0) one) (= (nsucc-pos (npos $q)) (psucc $q)))"},
    {"low0-pos", "(define (-> pos pos pos) (= (low0-pos one $r) (bit1 $r)) "
                 "(= (low0-pos (bit0 $q) $r) (bit0 $r)) (= (low0-pos (bit1 $q) $r) (bit1 $r)))"},
    {"low0", "(define (-> nat pos pos) (= (low0 n0 $r) (bit0 $r)) (= (low0 (npos $q) $r) (low0-pos $q $r)))"},
    {"low1-pos", "(define (-> pos pos pos) (= (low1-pos one $r) (bit0 (psucc $r))) "
                 "(= (low1-pos (bit0 $q) $r) (bit1 $r)) (= (low1-pos (bit1 $q) $r) (bit0 (psucc $r))))"},
    {"low1", "(define (-> nat pos pos) (= (low1 n0 $r) (bit1 $r)) (= (low1 (npos $q) $r) (low1-pos $q $r)))"},
    {"padd-nat", "(define (-> pos nat pos) (= (padd-nat one $m) (nsucc-pos $m)) "
                 "(= (padd-nat (bit0 $p) $m) (low0 $m (padd-nat $p (nhalf $m)))) "
                 "(= (padd-nat (bit1 $p) $m) (low1 $m (padd-nat $p (nhalf $m)))))"},
    {"padd", "(define (-> pos pos pos) (= (padd $p $q) (padd-nat $p (npos $q))))"},
    {"pmul", "(define (-> pos pos pos) (= (pmul one $q) $q) (= (pmul (bit0 $p) $q) (bit0 (pmul $p $q))) "
             "(= (pmul (bit1 $p) $q) (padd $q (bit0 (pmul $p $q)))))"},
};

static const char *const k_binary_datatypes[][2] = {
    {"pos", "(inductive (u 0) (: one pos) (: bit0 (-> pos pos)) (: bit1 (-> pos pos)))"},
    {"nat", "(inductive (u 0) (: n0 nat) (: npos (-> pos nat)))"},
};

typedef struct {
    VarId left[64];
    VarId right[64];
    size_t count;
} VarPairs;

static bool alpha_eq(Atom *x, Atom *y, VarPairs *pairs) {
    if (!x || !y) return x == y;
    if (x->kind == ATOM_VAR || y->kind == ATOM_VAR) {
        if (x->kind != ATOM_VAR || y->kind != ATOM_VAR) return false;
        for (size_t i = 0u; i < pairs->count; i++) {
            if (pairs->left[i] == x->var_id || pairs->right[i] == y->var_id)
                return pairs->left[i] == x->var_id && pairs->right[i] == y->var_id;
        }
        if (pairs->count == 64u) return false;
        pairs->left[pairs->count] = x->var_id;
        pairs->right[pairs->count] = y->var_id;
        pairs->count++;
        return true;
    }
    if (x->kind != ATOM_EXPR || y->kind != ATOM_EXPR) return atom_eq(x, y);
    if (x->expr.len != y->expr.len) return false;
    for (CettaExprIndex i = 0u; i < x->expr.len; i++)
        if (!alpha_eq(x->expr.elems[i], y->expr.elems[i], pairs)) return false;
    return true;
}

static bool same_text(Arena *a, Atom *atom, const char *text) {
    size_t pos = 0u;
    Atom *expected = parse_sexpr(a, text, &pos);
    if (!expected) return false;
    VarPairs pairs = {.count = 0u};
    return alpha_eq(atom, expected, &pairs);
}

/* The Lean theorems that give the agreement of `op` in the model, or NULL. */
static const char *lean_coverage(Arena *a, Space *space, Atom *op, NativeOp native,
                                 const Cone *cone) {
    const char *lean = NULL;
    if (atom_is_symbol_named(op, "padd") && native == NATIVE_ADD) lean = k_lean_padd;
    else if (atom_is_symbol_named(op, "pmul") && native == NATIVE_MUL) lean = k_lean_pmul;
    if (!lean) return NULL;
    size_t expected = atom_is_symbol_named(op, "pmul") ? 11u : 10u;
    if (cone->count != expected) return NULL;
    for (size_t i = 0u; i < cone->count; i++) {
        const char *text = NULL;
        for (size_t k = 0u; k < sizeof k_binary_definitions / sizeof k_binary_definitions[0]; k++)
            if (atom_is_symbol_named(cone->names[i], k_binary_definitions[k][0]))
                text = k_binary_definitions[k][1];
        if (!text || !same_text(a, cone->props[i], text)) return NULL;
    }
    for (size_t k = 0u; k < sizeof k_binary_datatypes / sizeof k_binary_datatypes[0]; k++) {
        Atom *record = known_record(a, space, sym(a, k_binary_datatypes[k][0]), "inductive");
        if (!record || !same_text(a, record->expr.elems[2], k_binary_datatypes[k][1]))
            return NULL;
    }
    return lean;
}

/* ------------------------------------------------------------------------ */
/* Numerals: reading and writing                                             */
/* ------------------------------------------------------------------------ */

/* A positive numeral's digits, from the last (outermost) to the first; the
 * innermost constructor is one.  Returns the number of digits after the
 * leading one, or -1 when `atom` is not a closed positive numeral. */
static int64_t positive_length(const Reading *rd, Atom *atom) {
    int64_t length = 0;
    while (is_expr(atom, 2u) &&
           (atom_eq(atom->expr.elems[0], rd->ctor[1]) ||
            atom_eq(atom->expr.elems[0], rd->ctor[2]))) {
        length++;
        atom = atom->expr.elems[1];
    }
    return atom_eq(atom, rd->ctor[0]) ? length : -1;
}

static Atom *positive_from_bits(Arena *a, const Reading *rd, const uint8_t *bits,
                                size_t top) {
    /* bits[top] is the leading one; bits[0] the last digit. */
    Atom *atom = rd->ctor[0];
    for (size_t i = top; i > 0u; i--)
        atom = atom_expr2(a, bits[i - 1u] ? rd->ctor[2] : rd->ctor[1], atom);
    return atom;
}

/* Signed 64-bit reading, for the uint64 backend: false when the numeral is
 * not closed or its value does not fit in 62 bits. */
static bool read_small(const Readings *rs, int index, Atom *atom, int64_t *out) {
    const Reading *rd = &rs->items[index];
    switch (rd->kind) {
    case READ_POSITIVE: {
        int64_t length = positive_length(rd, atom);
        if (length < 0 || length > 61) return false;
        int64_t value = 1;
        int64_t shift = 0;
        int64_t low = 0;
        for (Atom *cursor = atom; shift < length; shift++, cursor = cursor->expr.elems[1])
            if (atom_eq(cursor->expr.elems[0], rd->ctor[2])) low |= (int64_t)1 << shift;
        value = ((int64_t)1 << length) | low;
        *out = value;
        return true;
    }
    case READ_NATURAL:
        if (atom_eq(atom, rd->ctor[0])) {
            *out = 0;
            return true;
        }
        if (is_expr(atom, 2u) && atom_eq(atom->expr.elems[0], rd->ctor[1]))
            return read_small(rs, rd->part[0], atom->expr.elems[1], out);
        return false;
    case READ_INTEGER:
        if (atom_eq(atom, rd->ctor[0])) {
            *out = 0;
            return true;
        }
        if (is_expr(atom, 2u) && atom_eq(atom->expr.elems[0], rd->ctor[1]))
            return read_small(rs, rd->part[0], atom->expr.elems[1], out);
        if (is_expr(atom, 2u) && atom_eq(atom->expr.elems[0], rd->ctor[2])) {
            if (!read_small(rs, rd->part[0], atom->expr.elems[1], out)) return false;
            *out = -*out;
            return true;
        }
        return false;
    default:
        return false;
    }
}

static Atom *write_small(Arena *a, const Readings *rs, int index, int64_t value) {
    const Reading *rd = &rs->items[index];
    switch (rd->kind) {
    case READ_POSITIVE: {
        if (value <= 0) return NULL;
        uint8_t bits[64];
        size_t top = 0u;
        for (int64_t v = value; v > 1; v >>= 1) bits[top++] = (uint8_t)(v & 1);
        return positive_from_bits(a, rd, bits, top);
    }
    case READ_NATURAL:
        if (value < 0) return NULL;
        if (value == 0) return rd->ctor[0];
        return atom_expr2(a, rd->ctor[1], write_small(a, rs, rd->part[0], value));
    case READ_INTEGER:
        if (value == 0) return rd->ctor[0];
        if (value == INT64_MIN) return NULL;
        return atom_expr2(a, value > 0 ? rd->ctor[1] : rd->ctor[2],
                          write_small(a, rs, rd->part[0], value > 0 ? value : -value));
    default:
        return NULL;
    }
}

static Atom *write_ordering(const Reading *rd, int sign) {
    return sign < 0 ? rd->ctor[0] : sign == 0 ? rd->ctor[1] : rd->ctor[2];
}

#if CETTA_BUILD_WITH_GMP
static bool read_big(const Readings *rs, int index, Atom *atom, mpz_t out) {
    const Reading *rd = &rs->items[index];
    switch (rd->kind) {
    case READ_POSITIVE: {
        int64_t length = positive_length(rd, atom);
        if (length < 0) return false;
        mpz_set_ui(out, 1u);
        mpz_mul_2exp(out, out, (mp_bitcnt_t)length);
        Atom *cursor = atom;
        for (int64_t shift = 0; shift < length; shift++, cursor = cursor->expr.elems[1])
            if (atom_eq(cursor->expr.elems[0], rd->ctor[2])) mpz_setbit(out, (mp_bitcnt_t)shift);
        return true;
    }
    case READ_NATURAL:
        if (atom_eq(atom, rd->ctor[0])) {
            mpz_set_ui(out, 0u);
            return true;
        }
        if (is_expr(atom, 2u) && atom_eq(atom->expr.elems[0], rd->ctor[1]))
            return read_big(rs, rd->part[0], atom->expr.elems[1], out);
        return false;
    case READ_INTEGER:
        if (atom_eq(atom, rd->ctor[0])) {
            mpz_set_ui(out, 0u);
            return true;
        }
        if (is_expr(atom, 2u) && atom_eq(atom->expr.elems[0], rd->ctor[1]))
            return read_big(rs, rd->part[0], atom->expr.elems[1], out);
        if (is_expr(atom, 2u) && atom_eq(atom->expr.elems[0], rd->ctor[2])) {
            if (!read_big(rs, rd->part[0], atom->expr.elems[1], out)) return false;
            mpz_neg(out, out);
            return true;
        }
        return false;
    default:
        return false;
    }
}

static Atom *write_big(Arena *a, const Readings *rs, int index, const mpz_t value) {
    const Reading *rd = &rs->items[index];
    switch (rd->kind) {
    case READ_POSITIVE: {
        if (mpz_sgn(value) <= 0) return NULL;
        size_t top = mpz_sizeinbase(value, 2) - 1u;
        uint8_t *bits = malloc(top ? top : 1u);
        if (!bits) return NULL;
        for (size_t i = 0u; i < top; i++) bits[i] = (uint8_t)mpz_tstbit(value, i);
        Atom *atom = positive_from_bits(a, rd, bits, top);
        free(bits);
        return atom;
    }
    case READ_NATURAL:
        if (mpz_sgn(value) < 0) return NULL;
        if (mpz_sgn(value) == 0) return rd->ctor[0];
        {
            Atom *inner = write_big(a, rs, rd->part[0], value);
            return inner ? atom_expr2(a, rd->ctor[1], inner) : NULL;
        }
    case READ_INTEGER: {
        if (mpz_sgn(value) == 0) return rd->ctor[0];
        mpz_t magnitude;
        mpz_init(magnitude);
        mpz_abs(magnitude, value);
        Atom *inner = write_big(a, rs, rd->part[0], magnitude);
        mpz_clear(magnitude);
        return inner ? atom_expr2(a, mpz_sgn(value) > 0 ? rd->ctor[1] : rd->ctor[2], inner)
                     : NULL;
    }
    default:
        return NULL;
    }
}

static bool read_fraction(const Readings *rs, int index, Atom *atom, mpq_t out) {
    const Reading *rd = &rs->items[index];
    if (rd->kind != READ_FRACTION || !is_expr(atom, 3u) ||
        !atom_eq(atom->expr.elems[0], rd->ctor[0]))
        return false;
    if (!read_big(rs, rd->part[0], atom->expr.elems[1], mpq_numref(out)) ||
        !read_big(rs, rd->part[1], atom->expr.elems[2], mpq_denref(out)))
        return false;
    mpq_canonicalize(out);
    return true;
}

static Atom *write_fraction(Arena *a, const Readings *rs, int index, const mpq_t value) {
    const Reading *rd = &rs->items[index];
    Atom *numerator = write_big(a, rs, rd->part[0], mpq_numref(value));
    Atom *denominator = write_big(a, rs, rd->part[1], mpq_denref(value));
    return numerator && denominator ? atom_expr3(a, rd->ctor[0], numerator, denominator) : NULL;
}
#endif

/* Small fractions for the uint64 backend: numerator and positive
 * denominator, reduced. */
static int64_t small_gcd(int64_t x, int64_t y) {
    if (x < 0) x = -x;
    if (y < 0) y = -y;
    while (y) {
        int64_t t = x % y;
        x = y;
        y = t;
    }
    return x;
}

static bool read_small_fraction(const Readings *rs, int index, Atom *atom, int64_t *num,
                                int64_t *den) {
    const Reading *rd = &rs->items[index];
    if (rd->kind != READ_FRACTION || !is_expr(atom, 3u) ||
        !atom_eq(atom->expr.elems[0], rd->ctor[0]))
        return false;
    if (!read_small(rs, rd->part[0], atom->expr.elems[1], num) ||
        !read_small(rs, rd->part[1], atom->expr.elems[2], den) || *den <= 0)
        return false;
    int64_t g = small_gcd(*num, *den);
    if (g > 1) {
        *num /= g;
        *den /= g;
    }
    return true;
}

static Atom *write_small_fraction(Arena *a, const Readings *rs, int index, int64_t num,
                                  int64_t den) {
    const Reading *rd = &rs->items[index];
    if (den <= 0) return NULL;
    int64_t g = small_gcd(num, den);
    if (g > 1) {
        num /= g;
        den /= g;
    }
    if (num == 0) den = 1;
    Atom *numerator = write_small(a, rs, rd->part[0], num);
    Atom *denominator = write_small(a, rs, rd->part[1], den);
    return numerator && denominator ? atom_expr3(a, rd->ctor[0], numerator, denominator) : NULL;
}

/* ------------------------------------------------------------------------ */
/* The backends                                                              */
/* ------------------------------------------------------------------------ */

#if CETTA_BUILD_WITH_GMP
static Atom *compute_gmp(Arena *a, const Readings *rs, const Oracle *o, Atom **args) {
    const Reading *first = &rs->items[o->arg[0]];
    const Reading *result = &rs->items[o->result];
    Atom *answer = NULL;
    if (first->kind == READ_FRACTION && o->native != NATIVE_NORMALIZE) {
        mpq_t x, y, z;
        mpq_inits(x, y, z, NULL);
        if (read_fraction(rs, o->arg[0], args[0], x) &&
            read_fraction(rs, o->arg[1], args[1], y)) {
            switch (o->native) {
            case NATIVE_ADD:
                mpq_add(z, x, y);
#if CETTA_PRIME_ORACLE_PLANTED_FAULT
                if (mpz_cmpabs_ui(mpq_numref(z), 1000u) >= 0) {
                    mpz_add_ui(mpq_numref(z), mpq_numref(z), 1u);
                    mpq_canonicalize(z);
                }
#endif
                answer = write_fraction(a, rs, o->result, z);
                break;
            case NATIVE_SUB:
                mpq_sub(z, x, y);
                answer = write_fraction(a, rs, o->result, z);
                break;
            case NATIVE_MUL:
                mpq_mul(z, x, y);
                answer = write_fraction(a, rs, o->result, z);
                break;
            case NATIVE_COMPARE: {
                int sign = mpq_cmp(x, y);
                answer = write_ordering(result, sign < 0 ? -1 : sign > 0 ? 1 : 0);
                break;
            }
            default:
                break;
            }
        }
        mpq_clears(x, y, z, NULL);
        return answer;
    }
    mpz_t x, y, z, r;
    mpz_inits(x, y, z, r, NULL);
    if (!read_big(rs, o->arg[0], args[0], x) ||
        (o->argc > 1u && !read_big(rs, o->arg[1], args[1], y)))
        goto done;
    switch (o->native) {
    case NATIVE_ADD:
        mpz_add(z, x, y);
#if CETTA_PRIME_ORACLE_PLANTED_FAULT
        if (mpz_cmpabs_ui(z, 1000u) >= 0) mpz_add_ui(z, z, 1u);
#endif
        answer = write_big(a, rs, o->result, z);
        break;
    case NATIVE_SUB:
        mpz_sub(z, x, y);
        answer = write_big(a, rs, o->result, z);
        break;
    case NATIVE_SUB_TRUNCATED:
        mpz_sub(z, x, y);
        if (mpz_sgn(z) < 0) mpz_set_ui(z, 0u);
        answer = write_big(a, rs, o->result, z);
        break;
    case NATIVE_MUL:
        mpz_mul(z, x, y);
        answer = write_big(a, rs, o->result, z);
        break;
    case NATIVE_COMPARE: {
        int sign = mpz_cmp(x, y);
        answer = write_ordering(result, sign < 0 ? -1 : sign > 0 ? 1 : 0);
        break;
    }
    case NATIVE_DIVMOD:
        if (mpz_sgn(y) <= 0 || mpz_sgn(x) < 0) break;
        mpz_fdiv_qr(z, r, x, y);
        {
            Atom *q = write_big(a, rs, result->part[0], z);
            Atom *m = write_big(a, rs, result->part[0], r);
            answer = q && m ? atom_expr3(a, result->ctor[0], q, m) : NULL;
        }
        break;
    case NATIVE_GCD:
        mpz_gcd(z, x, y);
        answer = write_big(a, rs, o->result, z);
        break;
    case NATIVE_NORMALIZE:
        if (mpz_sgn(y) <= 0) break;
        {
            mpq_t q;
            mpq_init(q);
            mpz_set(mpq_numref(q), x);
            mpz_set(mpq_denref(q), y);
            mpq_canonicalize(q);
            answer = write_fraction(a, rs, o->result, q);
            mpq_clear(q);
        }
        break;
    }
done:
    mpz_clears(x, y, z, r, NULL);
    return answer;
}
#endif

static Atom *compute_small(Arena *a, const Readings *rs, const Oracle *o, Atom **args) {
    const Reading *first = &rs->items[o->arg[0]];
    const Reading *result = &rs->items[o->result];
    if (first->kind == READ_FRACTION && o->native != NATIVE_NORMALIZE) {
        int64_t xn, xd, yn, yd, zn, zd, l, r;
        if (!read_small_fraction(rs, o->arg[0], args[0], &xn, &xd) ||
            !read_small_fraction(rs, o->arg[1], args[1], &yn, &yd))
            return NULL;
        switch (o->native) {
        case NATIVE_ADD:
        case NATIVE_SUB:
            if (__builtin_mul_overflow(xn, yd, &l) || __builtin_mul_overflow(yn, xd, &r) ||
                (o->native == NATIVE_ADD ? __builtin_add_overflow(l, r, &zn)
                                         : __builtin_sub_overflow(l, r, &zn)) ||
                __builtin_mul_overflow(xd, yd, &zd))
                return NULL;
#if CETTA_PRIME_ORACLE_PLANTED_FAULT
            if (o->native == NATIVE_ADD) {
                int64_t g = small_gcd(zn, zd);
                if (g > 1) {
                    zn /= g;
                    zd /= g;
                }
                if (zn >= 1000 || zn <= -1000) zn += 1;
            }
#endif
            return write_small_fraction(a, rs, o->result, zn, zd);
        case NATIVE_MUL:
            if (__builtin_mul_overflow(xn, yn, &zn) || __builtin_mul_overflow(xd, yd, &zd))
                return NULL;
            return write_small_fraction(a, rs, o->result, zn, zd);
        case NATIVE_COMPARE:
            if (__builtin_mul_overflow(xn, yd, &l) || __builtin_mul_overflow(yn, xd, &r))
                return NULL;
            return write_ordering(result, l < r ? -1 : l > r ? 1 : 0);
        default:
            return NULL;
        }
    }
    int64_t x = 0, y = 0, z = 0;
    if (!read_small(rs, o->arg[0], args[0], &x) ||
        (o->argc > 1u && !read_small(rs, o->arg[1], args[1], &y)))
        return NULL;
    switch (o->native) {
    case NATIVE_ADD:
        if (__builtin_add_overflow(x, y, &z)) return NULL;
#if CETTA_PRIME_ORACLE_PLANTED_FAULT
        if (z >= 1000 || z <= -1000) z += 1;
#endif
        return write_small(a, rs, o->result, z);
    case NATIVE_SUB:
        if (__builtin_sub_overflow(x, y, &z)) return NULL;
        return write_small(a, rs, o->result, z);
    case NATIVE_SUB_TRUNCATED:
        return write_small(a, rs, o->result, x > y ? x - y : 0);
    case NATIVE_MUL:
        if (__builtin_mul_overflow(x, y, &z)) return NULL;
        return write_small(a, rs, o->result, z);
    case NATIVE_COMPARE:
        return write_ordering(result, x < y ? -1 : x > y ? 1 : 0);
    case NATIVE_DIVMOD: {
        if (y <= 0 || x < 0) return NULL;
        Atom *q = write_small(a, rs, result->part[0], x / y);
        Atom *m = write_small(a, rs, result->part[0], x % y);
        return q && m ? atom_expr3(a, result->ctor[0], q, m) : NULL;
    }
    case NATIVE_GCD:
        return write_small(a, rs, o->result, small_gcd(x, y));
    case NATIVE_NORMALIZE:
        if (y <= 0) return NULL;
        return write_small_fraction(a, rs, o->result, x, y);
    }
    return NULL;
}

static Atom *compute(Arena *a, const Readings *rs, const Oracle *o, Atom **args) {
#if CETTA_BUILD_WITH_GMP
    if (o->backend == BACKEND_GMP) return compute_gmp(a, rs, o, args);
#endif
    return o->backend == BACKEND_UINT64 ? compute_small(a, rs, o, args) : NULL;
}

/* ------------------------------------------------------------------------ */
/* Signatures                                                                */
/* ------------------------------------------------------------------------ */

static bool integer_kind(ReadingKind kind) {
    return kind == READ_POSITIVE || kind == READ_NATURAL || kind == READ_INTEGER;
}

/* Whether `native` has the signature `arg -> result` over these readings. */
static bool signature_fits(const Readings *rs, NativeOp native, size_t argc, const int *arg,
                           int result) {
    if (argc != 2u) return false;
    const Reading *x = &rs->items[arg[0]];
    const Reading *y = &rs->items[arg[1]];
    const Reading *z = &rs->items[result];
    switch (native) {
    case NATIVE_ADD:
    case NATIVE_MUL:
        return arg[0] == arg[1] && arg[0] == result &&
               (integer_kind(x->kind) || x->kind == READ_FRACTION);
    case NATIVE_SUB:
        return arg[0] == arg[1] && arg[0] == result &&
               (x->kind == READ_INTEGER || x->kind == READ_FRACTION);
    case NATIVE_SUB_TRUNCATED:
        return arg[0] == arg[1] && arg[0] == result && x->kind == READ_NATURAL;
    case NATIVE_COMPARE:
        return arg[0] == arg[1] && z->kind == READ_ORDERING &&
               (integer_kind(x->kind) || x->kind == READ_FRACTION);
    case NATIVE_DIVMOD:
        return x->kind == READ_NATURAL && y->kind == READ_POSITIVE &&
               x->part[0] == arg[1] && z->kind == READ_QUOTREM && z->part[0] == arg[0];
    case NATIVE_GCD:
        return arg[0] == arg[1] && arg[0] == result && x->kind == READ_POSITIVE;
    case NATIVE_NORMALIZE:
        return x->kind == READ_INTEGER && y->kind == READ_POSITIVE &&
               z->kind == READ_FRACTION && z->part[0] == arg[0] && z->part[1] == arg[1];
    }
    return false;
}

/* ------------------------------------------------------------------------ */
/* The admitted oracles of a space, remembered per thread for one state of   */
/* the space and of admission                                                */
/* ------------------------------------------------------------------------ */

typedef struct {
    const Space *space;
    uint64_t instance;
    uint64_t revision;
    uint64_t admission;
    bool valid;
    Arena arena;
    bool arena_ready;
    Readings readings;
    Oracle *oracles;
    size_t oracle_count;
    size_t oracle_cap;
} OracleCache;

static _Thread_local OracleCache t_cache;
static _Atomic uint64_t g_oracles_published = 0u;

static bool backend_named(Atom *name, Backend *out) {
    if (atom_is_symbol_named(name, "uint64")) {
        *out = BACKEND_UINT64;
        return true;
    }
#if CETTA_BUILD_WITH_GMP
    if (atom_is_symbol_named(name, "gmp-mpz")) {
        *out = BACKEND_GMP;
        return true;
    }
#endif
    return false;
}

static bool native_named(Atom *name, NativeOp *out) {
    for (size_t i = 0u; i < sizeof k_natives / sizeof k_natives[0]; i++)
        if (atom_is_symbol_named(name, k_natives[i].name)) {
            *out = k_natives[i].op;
            return true;
        }
    return false;
}

/* The oracle a record declares, if the record still holds: its
 * specification has the digest it was admitted with, its readings are
 * admitted, and its backend has the signature of the operation. */
static bool oracle_from_record(Arena *a, Space *space, const Readings *rs, Atom *record,
                               Oracle *out) {
    if (!is_expr_head(record, "set:oracle-of", 6u)) return false;
    Atom *op = record->expr.elems[1];
    Atom *spec = record->expr.elems[2];
    Atom *backend = record->expr.elems[3];
    Atom *signature = record->expr.elems[4];
    if (!is_sym(op) || !is_expr_head(spec, "equations", 4u) ||
        !atom_eq(spec->expr.elems[1], op) || !is_expr_head(backend, "backend", 3u) ||
        !signature || signature->kind != ATOM_EXPR || signature->expr.len != 4u ||
        !atom_is_symbol_named(signature->expr.elems[0], "signature"))
        return false;
    Oracle o = {.op = op, .argc = 2u};
    if (!backend_named(backend->expr.elems[1], &o.backend) ||
        !native_named(backend->expr.elems[2], &o.native))
        return false;
    for (size_t i = 0u; i < 2u; i++) {
        o.arg[i] = readings_find(rs, signature->expr.elems[1u + i]);
        if (o.arg[i] < 0) return false;
    }
    o.result = readings_find(rs, signature->expr.elems[3]);
    if (o.result < 0 || !signature_fits(rs, o.native, o.argc, o.arg, o.result)) return false;
    Cone cone;
    char digest[65];
    if (!specification(a, space, op, &cone, digest)) return false;
    Atom *stored = spec->expr.elems[3];
    bool same = stored && stored->kind == ATOM_GROUNDED &&
                stored->ground.gkind == GV_STRING &&
                strcmp(stored->ground.sval, digest) == 0;
    /* The declared type of the operation is still the signature. */
    Atom *prop = cone.count ? cone.props[0] : NULL;
    Atom *parts[3];
    size_t count = 0u;
    same = same && prop && arrow_parts(prop->expr.elems[1], parts, 3u, &count) &&
           count == 3u && atom_eq(parts[0], signature->expr.elems[1]) &&
           atom_eq(parts[1], signature->expr.elems[2]) &&
           atom_eq(parts[2], signature->expr.elems[3]);
    o.lean = same ? lean_coverage(a, space, op, o.native, &cone) : NULL;
    cone_free(&cone);
    if (!same) return false;
    *out = o;
    return true;
}

static void cache_rebuild(OracleCache *cache, Space *space) {
    if (!cache->arena_ready) {
        arena_init_detached(&cache->arena);
        cache->arena_ready = true;
    } else {
        arena_free(&cache->arena);
        arena_init_detached(&cache->arena);
    }
    Arena *a = &cache->arena;
    cache->readings.count = 0u;
    cache->oracle_count = 0u;
    uint64_t revision = space_revision(space);
    uint64_t admission = prime_scoped_judgment_admission_epoch();
    readings_collect(a, space, &cache->readings);
    Atom *items[6] = {sym(a, "set:oracle-of"), atom_var(a, "o"), atom_var(a, "e"),
                      atom_var(a, "b"), atom_var(a, "s"), atom_var(a, "l")};
    CettaIndex *candidates = NULL;
    CettaIndex count = space_match_candidates64(space, atom_expr(a, items, 6u), &candidates);
    for (CettaIndex i = 0u; i < count; i++) {
        Atom *record = space_match_candidate_at64(space, candidates[i]);
        Oracle o;
        if (!record || !prime_scoped_judgment_admitted(a, space, record) ||
            !oracle_from_record(a, space, &cache->readings, record, &o))
            continue;
        bool duplicate = false;
        for (size_t k = 0u; k < cache->oracle_count && !duplicate; k++)
            duplicate = atom_eq(cache->oracles[k].op, o.op);
        if (duplicate) continue;
        if (cache->oracle_count == cache->oracle_cap) {
            size_t next = cache->oracle_cap ? cache->oracle_cap * 2u : 16u;
            Oracle *grown = realloc(cache->oracles, sizeof(Oracle) * next);
            if (!grown) break;
            cache->oracles = grown;
            cache->oracle_cap = next;
        }
        cache->oracles[cache->oracle_count++] = o;
    }
    free(candidates);
    cache->space = space;
    cache->instance = space_instance_id(space);
    cache->revision = revision;
    cache->admission = admission;
    cache->valid = space_revision(space) == revision &&
                   prime_scoped_judgment_admission_epoch() == admission;
}

static OracleCache *cache_for(Space *space) {
    OracleCache *cache = &t_cache;
    if (cache->valid && cache->space == space &&
        cache->instance == space_instance_id(space) &&
        cache->revision == space_revision(space) &&
        cache->admission == prime_scoped_judgment_admission_epoch())
        return cache;
    cache_rebuild(cache, space);
    return cache;
}

/* ------------------------------------------------------------------------ */
/* Trust records, the audit and the residue check                            */
/* ------------------------------------------------------------------------ */

typedef struct {
    char *op;
    Backend backend;
    NativeOp native;
    const char *lean;
    uint64_t calls;
} TrustUse;

static _Thread_local TrustUse *t_uses;
static _Thread_local size_t t_use_count;
static _Thread_local size_t t_use_cap;

static bool g_audit = false;
static bool g_residues = false;
static _Atomic uint64_t g_audit_answers = 0u;
static _Atomic uint64_t g_audit_agreed = 0u;
static _Atomic uint64_t g_audit_undecided = 0u;
static _Atomic uint64_t g_residue_checked = 0u;
static _Atomic uint64_t g_check_mismatches = 0u;
static char *g_failure = NULL;

void prime_arith_oracle_set_audit(bool enabled) { g_audit = enabled; }
void prime_arith_oracle_set_residues(bool enabled) { g_residues = enabled; }

static void trust_note(const Oracle *o) {
    const char *name = atom_name_cstr(o->op);
    if (!name) return;
    for (size_t i = 0u; i < t_use_count; i++)
        if (strcmp(t_uses[i].op, name) == 0 && t_uses[i].backend == o->backend &&
            t_uses[i].native == o->native) {
            t_uses[i].calls++;
            return;
        }
    if (t_use_count == t_use_cap) {
        size_t next = t_use_cap ? t_use_cap * 2u : 8u;
        TrustUse *grown = realloc(t_uses, sizeof(TrustUse) * next);
        if (!grown) return;
        t_uses = grown;
        t_use_cap = next;
    }
    char *copy = strdup(name);
    if (!copy) return;
    t_uses[t_use_count++] = (TrustUse){copy, o->backend, o->native, o->lean, 1u};
}

void prime_arith_oracle_query_begin(void) {
    for (size_t i = 0u; i < t_use_count; i++) free(t_uses[i].op);
    t_use_count = 0u;
}

void prime_arith_oracle_query_report(FILE *out) {
    for (size_t i = 0u; i < t_use_count; i++) {
        const TrustUse *use = &t_uses[i];
        if (use->lean)
            fprintf(out, "(uses-oracle %s (trusted %s %s) (spec-agreement (proved %s))"
                         " (calls %" PRIu64 ") (tests %s))\n",
                    use->op, backend_name(use->backend), native_name(use->native),
                    use->lean, use->calls, k_tests);
        else
            fprintf(out, "(uses-oracle %s (trusted %s %s) (spec-agreement (tested"
                         " (admission-calls %d))) (calls %" PRIu64 ") (tests %s))\n",
                    use->op, backend_name(use->backend), native_name(use->native),
                    ORACLE_ADMISSION_CALLS, use->calls, k_tests);
    }
    prime_arith_oracle_query_begin();
}

static void check_failed(Arena *a, const char *kind, Atom *call, Atom *native, Atom *other,
                         const char *other_name) {
    atomic_fetch_add(&g_check_mismatches, 1u);
    if (g_failure) return;
    char *c = atom_to_string(a, call);
    char *n = atom_to_string(a, native);
    char *o = other ? atom_to_string(a, other) : NULL;
    size_t size = strlen(kind) + (c ? strlen(c) : 0u) + (n ? strlen(n) : 0u) +
                  (o ? strlen(o) : 0u) + strlen(other_name) + 64u;
    g_failure = malloc(size);
    if (!g_failure) return;
    if (o)
        snprintf(g_failure, size, "(%s %s (backend %s) (%s %s))", kind, c ? c : "?",
                 n ? n : "?", other_name, o);
    else
        snprintf(g_failure, size, "(%s %s (backend %s) (%s))", kind, c ? c : "?",
                 n ? n : "?", other_name);
}

bool prime_arith_oracle_check_failed(void) { return g_failure != NULL; }

void prime_arith_oracle_report_failure(FILE *out) {
    if (g_failure) fprintf(out, "%s\n", g_failure);
}

void prime_arith_oracle_report_audit(FILE *out) {
    if (g_audit)
        fprintf(out,
                "(oracle-audit (answers %" PRIu64 ") (agreed %" PRIu64 ") (undecided %" PRIu64
                ") (mismatches %" PRIu64 "))\n",
                atomic_load(&g_audit_answers), atomic_load(&g_audit_agreed),
                atomic_load(&g_audit_undecided), atomic_load(&g_check_mismatches));
    if (g_residues)
        fprintf(out, "(oracle-residues (checked %" PRIu64 ") (mismatches %" PRIu64 "))\n",
                atomic_load(&g_residue_checked), atomic_load(&g_check_mismatches));
}

/* A numeral's value modulo `p`, read from its constructors alone. */
static bool residue(const Readings *rs, int index, Atom *atom, uint64_t p, uint64_t *out) {
    const Reading *rd = &rs->items[index];
    switch (rd->kind) {
    case READ_POSITIVE: {
        int64_t length = positive_length(rd, atom);
        if (length < 0) return false;
        /* The digits from the last, then the value from the leading one. */
        uint8_t *bits = malloc((size_t)length + 1u);
        if (!bits) return false;
        Atom *cursor = atom;
        for (int64_t i = 0; i < length; i++, cursor = cursor->expr.elems[1])
            bits[i] = atom_eq(cursor->expr.elems[0], rd->ctor[2]) ? 1u : 0u;
        uint64_t value = 1u % p;
        for (int64_t i = length; i > 0; i--) value = (value * 2u + bits[i - 1]) % p;
        free(bits);
        *out = value;
        return true;
    }
    case READ_NATURAL:
        if (atom_eq(atom, rd->ctor[0])) {
            *out = 0u;
            return true;
        }
        return is_expr(atom, 2u) && atom_eq(atom->expr.elems[0], rd->ctor[1]) &&
               residue(rs, rd->part[0], atom->expr.elems[1], p, out);
    case READ_INTEGER:
        if (atom_eq(atom, rd->ctor[0])) {
            *out = 0u;
            return true;
        }
        if (is_expr(atom, 2u) && atom_eq(atom->expr.elems[0], rd->ctor[1]))
            return residue(rs, rd->part[0], atom->expr.elems[1], p, out);
        if (is_expr(atom, 2u) && atom_eq(atom->expr.elems[0], rd->ctor[2])) {
            if (!residue(rs, rd->part[0], atom->expr.elems[1], p, out)) return false;
            *out = (p - *out) % p;
            return true;
        }
        return false;
    default:
        return false;
    }
}

/* The residue check of a sum or a product: false on a mismatch.  A fraction
 * z1/d1 + z2/d2 = z/d is checked as z d1 d2 = (z1 d2 + z2 d1) d. */
static bool residues_agree(const Readings *rs, const Oracle *o, Atom **args, Atom *answer,
                           bool *checked) {
    static const uint64_t primes[3] = {2147483647u, 1000000007u, 998244353u};
    *checked = false;
    if (o->native != NATIVE_ADD && o->native != NATIVE_MUL) return true;
    const Reading *rd = &rs->items[o->result];
    for (size_t k = 0u; k < 3u; k++) {
        uint64_t p = primes[k];
        if (rd->kind == READ_FRACTION) {
            uint64_t v[6];
            Atom *parts[3] = {args[0], args[1], answer};
            for (size_t i = 0u; i < 3u; i++) {
                Atom *f = parts[i];
                if (!is_expr(f, 3u) || !atom_eq(f->expr.elems[0], rd->ctor[0]) ||
                    !residue(rs, rd->part[0], f->expr.elems[1], p, &v[2u * i]) ||
                    !residue(rs, rd->part[1], f->expr.elems[2], p, &v[2u * i + 1u]))
                    return true;
            }
            uint64_t z1 = v[0], d1 = v[1], z2 = v[2], d2 = v[3], z = v[4], d = v[5];
            uint64_t left = z * d1 % p * d2 % p;
            uint64_t right = o->native == NATIVE_ADD
                ? (z1 * d2 % p + z2 * d1 % p) % p * d % p
                : z1 * z2 % p * d % p;
            if (left != right) return false;
        } else {
            uint64_t x, y, z;
            if (!residue(rs, o->arg[0], args[0], p, &x) ||
                !residue(rs, o->arg[1], args[1], p, &y) ||
                !residue(rs, o->result, answer, p, &z))
                return true;
            uint64_t expected = o->native == NATIVE_ADD ? (x + y) % p : x * y % p;
            if (expected != z) return false;
        }
    }
    *checked = true;
    return true;
}

/* ------------------------------------------------------------------------ */
/* The oracle at a covered call                                              */
/* ------------------------------------------------------------------------ */

Atom *prime_arith_oracle_answer(Arena *arena, Space *space, Atom *call) {
    if (atomic_load_explicit(&g_oracles_published, memory_order_relaxed) == 0u ||
        !arena || !space || !call || space->overlay_base || !is_expr(call, 3u) ||
        !is_sym(call->expr.elems[0]))
        return NULL;
    OracleCache *cache = cache_for(space);
    const Oracle *o = NULL;
    for (size_t i = 0u; i < cache->oracle_count && !o; i++)
        if (atom_eq(cache->oracles[i].op, call->expr.elems[0])) o = &cache->oracles[i];
    if (!o || call->expr.len - 1u != o->argc) return NULL;
    Atom *args[2] = {call->expr.elems[1], call->expr.elems[2]};
    Atom *answer = compute(arena, &cache->readings, o, args);
    if (!answer) return NULL;
    if (g_residues) {
        bool checked = false;
        if (!residues_agree(&cache->readings, o, args, answer, &checked))
            check_failed(arena, "oracle-residue-mismatch", call, answer, NULL,
                         "residues-disagree");
        else if (checked)
            atomic_fetch_add(&g_residue_checked, 1u);
    }
    if (g_audit) {
        atomic_fetch_add(&g_audit_answers, 1u);
        bool exhausted = false;
        Atom *rules = prime_semantics_rules_normal_form(arena, space, call, 50000000u,
                                                        &exhausted);
        if (!rules) {
            atomic_fetch_add(&g_audit_undecided, 1u);
        } else if (!atom_eq(rules, answer)) {
            check_failed(arena, "oracle-audit-mismatch", call, answer, rules, "equations");
            /* The run stops after this query; meanwhile the call computes
             * as its equations say. */
            answer = rules;
        } else {
            atomic_fetch_add(&g_audit_agreed, 1u);
        }
    }
    trust_note(o);
    return answer;
}

/* ------------------------------------------------------------------------ */
/* Admission                                                                 */
/* ------------------------------------------------------------------------ */

Atom *prime_arith_oracle_judge_numerals(Arena *a, Space *space, Atom *judgment) {
    if (!is_expr(judgment, 3u))
        return refuted(a, judgment, "set:numerals-arity", NULL);
    Atom *type = judgment->expr.elems[1];
    Atom *spec = judgment->expr.elems[2];
    if (!is_sym(type)) return refuted(a, judgment, "set:numerals-type", type);
    size_t want = 0u;
    ReadingKind kind = spec && spec->kind == ATOM_EXPR && spec->expr.len >= 2u
        ? reading_kind_named(spec->expr.elems[0], &want) : READ_NONE;
    if (kind == READ_NONE) return refuted(a, judgment, "set:numerals-kind", spec);
    if (spec->expr.len - 1u != want)
        return refuted(a, judgment, "set:numerals-constructor-count", spec);
    for (size_t i = 0u; i < want; i++)
        if (!is_sym(spec->expr.elems[1u + i]))
            return refuted(a, judgment, "set:numerals-constructor-names", spec);
    Atom **ctors = NULL;
    size_t ctor_count = inductive_ctors(a, space, type, &ctors);
    if (ctor_count == 0u) return refuted(a, judgment, "set:numerals-not-inductive", type);
    if (reading_record(a, space, type))
        return refuted(a, judgment, "set:numerals-already-read", type);
    Readings known = {0};
    readings_collect(a, space, &known);
    int part[2];
    const char *reason = reading_shape(&known, type, kind, spec->expr.elems + 1, want, ctors,
                                       ctor_count, part);
    Atom *parts = NULL;
    if (!reason) {
        Atom *items[3] = {sym(a, "parts"), NULL, NULL};
        size_t n = 1u;
        for (size_t i = 0u; i < 2u; i++)
            if (part[i] >= 0) items[n++] = known.items[part[i]].type;
        parts = atom_expr(a, items, (CettaExprLen)n);
    }
    free(known.items);
    if (reason) return refuted(a, judgment, reason, type);
    Atom *record_items[4] = {sym(a, "set:numerals-of"), type, spec, parts};
    Atom *record = atom_expr(a, record_items, 4u);
    return verdict(a, "Established", judgment, atom_expr2(a, sym(a, "SetPublish"), record));
}

/* Small sample values of a reading, as numerals. */
static size_t samples(Arena *a, const Readings *rs, int index, Atom **out) {
    const Reading *rd = &rs->items[index];
    static const int64_t positive[ORACLE_SAMPLE_VALUES] = {1, 2, 3};
    static const int64_t natural[ORACLE_SAMPLE_VALUES] = {0, 1, 5};
    static const int64_t integer[ORACLE_SAMPLE_VALUES] = {-3, 0, 2};
    static const int64_t fraction[ORACLE_SAMPLE_VALUES][2] = {{-1, 2}, {0, 1}, {2, 3}};
    for (size_t i = 0u; i < ORACLE_SAMPLE_VALUES; i++) {
        switch (rd->kind) {
        case READ_POSITIVE: out[i] = write_small(a, rs, index, positive[i]); break;
        case READ_NATURAL: out[i] = write_small(a, rs, index, natural[i]); break;
        case READ_INTEGER: out[i] = write_small(a, rs, index, integer[i]); break;
        case READ_FRACTION:
            out[i] = write_small_fraction(a, rs, index, fraction[i][0], fraction[i][1]);
            break;
        default: return 0u;
        }
        if (!out[i]) return 0u;
    }
    return ORACLE_SAMPLE_VALUES;
}

Atom *prime_arith_oracle_judge_declaration(Arena *a, Space *space, Atom *judgment) {
    if (!is_expr(judgment, 4u)) return refuted(a, judgment, "set:oracle-arity", NULL);
    Atom *op = judgment->expr.elems[1];
    Atom *spec = judgment->expr.elems[2];
    Atom *backend = judgment->expr.elems[3];
    if (!is_sym(op)) return refuted(a, judgment, "set:oracle-operation", op);
    if (!is_expr_head(spec, "equations", 2u))
        return refuted(a, judgment, "set:oracle-specification", spec);
    if (!atom_eq(spec->expr.elems[1], op))
        return refuted(a, judgment, "set:oracle-equations-of-another-operation",
                       spec->expr.elems[1]);
    if (!is_expr_head(backend, "backend", 3u))
        return refuted(a, judgment, "set:oracle-backend", backend);
    Oracle o = {.op = op};
    if (!native_named(backend->expr.elems[2], &o.native))
        return refuted(a, judgment, "set:oracle-unknown-native", backend->expr.elems[2]);
    if (!backend_named(backend->expr.elems[1], &o.backend)) {
        if (atom_is_symbol_named(backend->expr.elems[1], "gmp-mpz"))
            return undetermined(a, judgment, "set:oracle-backend-not-built",
                                backend->expr.elems[1]);
        return refuted(a, judgment, "set:oracle-unknown-backend", backend->expr.elems[1]);
    }
    if (oracle_record(a, space, op))
        return refuted(a, judgment, "set:oracle-already-declared", op);
    Cone cone;
    char digest[65];
    if (!specification(a, space, op, &cone, digest))
        return refuted(a, judgment, "set:oracle-not-an-admitted-definition", op);
    Atom *prop = cone.props[0];
    Atom *types[3];
    size_t count = 0u;
    if (!arrow_parts(prop->expr.elems[1], types, 3u, &count) || count != 3u) {
        cone_free(&cone);
        return refuted(a, judgment, "set:oracle-signature", prop->expr.elems[1]);
    }
    Readings rs = {0};
    readings_collect(a, space, &rs);
    o.argc = 2u;
    o.arg[0] = readings_find(&rs, types[0]);
    o.arg[1] = readings_find(&rs, types[1]);
    o.result = readings_find(&rs, types[2]);
    for (size_t i = 0u; i < 3u; i++) {
        int index = i < 2u ? o.arg[i] : o.result;
        if (index < 0) {
            free(rs.items);
            cone_free(&cone);
            return refuted(a, judgment, "set:oracle-type-without-numerals", types[i]);
        }
    }
    if (!signature_fits(&rs, o.native, o.argc, o.arg, o.result)) {
        free(rs.items);
        cone_free(&cone);
        return refuted(a, judgment, "set:oracle-signature-mismatch",
                       atom_expr2(a, backend->expr.elems[2], prop->expr.elems[1]));
    }
    /* The backend against the equations, on small calls. */
    Atom *xs[ORACLE_SAMPLE_VALUES];
    Atom *ys[ORACLE_SAMPLE_VALUES];
    if (samples(a, &rs, o.arg[0], xs) != ORACLE_SAMPLE_VALUES ||
        samples(a, &rs, o.arg[1], ys) != ORACLE_SAMPLE_VALUES) {
        free(rs.items);
        cone_free(&cone);
        return undetermined(a, judgment, "set:oracle-no-samples", op);
    }
    Atom *failure = NULL;
    bool undecided = false;
    for (size_t i = 0u; i < ORACLE_SAMPLE_VALUES && !failure && !undecided; i++) {
        for (size_t j = 0u; j < ORACLE_SAMPLE_VALUES && !failure && !undecided; j++) {
            Atom *args[2] = {xs[i], ys[j]};
            Atom *call = atom_expr3(a, op, xs[i], ys[j]);
            Atom *native = compute(a, &rs, &o, args);
            bool exhausted = false;
            Atom *rules = prime_semantics_rules_normal_form(a, space, call, 1000000u, &exhausted);
            if (!rules) {
                failure = call;
                undecided = true;
            } else if (!native || !atom_eq(native, rules)) {
                Atom *items[4] = {call,
                                  atom_expr2(a, sym(a, "equations"), rules),
                                  atom_expr2(a, sym(a, "backend"),
                                             native ? native : sym(a, "none"))};
                failure = atom_expr(a, items, 3u);
            }
        }
    }
    if (failure) {
        free(rs.items);
        cone_free(&cone);
        return undecided
            ? undetermined(a, judgment, "set:oracle-sample-without-normal-form", failure)
            : refuted(a, judgment, "set:oracle-disagrees-with-equations", failure);
    }
    /* The agreement of the native operation with the equations: proved, or
     * tested by the calls above. */
    const char *lean = lean_coverage(a, space, op, o.native, &cone);
    Atom *agreement = NULL;
    if (lean) {
        Atom *items[3] = {sym(a, "proved"), NULL, NULL};
        size_t n = 1u;
        char buffer[256];
        char *rest = NULL;
        snprintf(buffer, sizeof buffer, "%s", lean);
        for (char *word = strtok_r(buffer, " ", &rest); word && n < 3u;
             word = strtok_r(NULL, " ", &rest))
            items[n++] = sym(a, word);
        agreement = atom_expr(a, items, (CettaExprLen)n);
    } else {
        agreement = atom_expr2(a, sym(a, "tested"),
                               atom_expr2(a, sym(a, "admission-calls"),
                                          atom_int(a, ORACLE_ADMISSION_CALLS)));
    }
    Atom *agreement_atom = atom_expr2(a, sym(a, "spec-agreement"), agreement);
    Atom *equations[4] = {sym(a, "equations"), op, atom_int(a, (int64_t)cone.count),
                          atom_string(a, digest)};
    Atom *signature[4] = {sym(a, "signature"), types[0], types[1], types[2]};
    Atom *record_items[6] = {sym(a, "set:oracle-of"), op, atom_expr(a, equations, 4u),
                             backend, atom_expr(a, signature, 4u), agreement_atom};
    Atom *record = atom_expr(a, record_items, 6u);
    free(rs.items);
    cone_free(&cone);
    atomic_fetch_add(&g_oracles_published, 1u);
    return verdict(a, "Established", judgment, atom_expr2(a, sym(a, "SetPublish"), record));
}

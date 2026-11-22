/*
 * The C target of the direct GSLT compilers: the selected modes of a closed
 * entry residual as a rule module of rulec's IR (its core rows, the wire, and
 * its binding table), which rulec (tools/rulec/compiler) compiles to C.
 *
 * A GSLT relation is a moded Horn relation over atoms.  The module has one
 * data sort, Term, holding every atom in rulec's atoms representation
 * (constructor applications on the heap, integers as immediates, strings and
 * other symbols as byte strings).  Its constructors are named as the atoms
 * are spelled: (OP N) for an application of the symbol OP to N arguments, a
 * symbol's name, an integer's decimal digits, a string in quotes; and
 * "other integer", "other string", "other symbol", "other expression" for
 * the atoms that are none of these (a value of an input the rules never
 * mention).  Every operator of the composition is a constructor (OP N).
 * When the composition has langdef's source list, (cons (cp N) ...) ending in
 * nil, the run's source is a value of Term read through cursors (rulec's
 * atoms-source): a file is read as that list without building it.
 *
 * Each selected mode R:BITS (one bit per argument, 1 for an input) is a
 * judgment R:BITS of its inputs.  Its result is its one output (sort Term),
 * the tuple "R:BITS answer" of its outputs (a sort of that name), or yes
 * (sort Unit) when it has none.  A rule L of R read in mode R:BITS is the
 * rule "L:BITS" (L#INDEX:BITS when L names several rules):
 *
 *   R:BITS(head input patterns) ~> head output terms
 *
 * with the body's goals as premises in written order.  A goal (K A ...) of a
 * relation is called in the mode its arguments' groundness selects (the
 * analysis's): K:B(inputs) ~> outputs, a pattern.  A goal of a ground
 * relation (a capability) is a query of that relation, every argument bound,
 * which the binding table answers by a test component of the atoms
 * representation (the C dialect's realization of the shared ground
 * relations, fixed here as tcalc's and PeTTa's are for theirs).  Where a
 * variable occurs again in a left side, or twice in one target where it is
 * not yet bound, the later occurrence is a fresh variable that a query of
 * gslt:same (term-equal) compares with it.
 *
 * Every selected mode is declared a judgment (a mode of a relation without
 * rules has none, and no answer).  An admitted entry (petta-direct
 * --admission-language, --admission-entry) is the judgment
 * gslt:admitted-entry:MODE of a language and the input: the PeTTa target runs
 * the family the admission's domains specialize when the language service
 * admits the input, the ordinary one otherwise, and both give the same
 * answers; rulec's analysis does not read the domains, so both families are
 * these procedures, and the admitted entry runs the entry on its input.  rulec infers each judgment's determinism
 * (det, semidet, nondet) and compiles it; a nondet judgment gives its
 * answers to a success continuation.
 *
 * Refused, with the reason: a premise of a ground relation C has no
 * realization of, or whose arguments the premises before it leave unbound;
 * a head output the rule leaves unbound (an answer with an unknown, which no
 * GSLT rule means); an expression whose head is not a symbol, and a float,
 * in a rule; a string holding a NUL byte; and a constructor name that two
 * different atoms would spell alike.
 */
#include "gslt_direct_internal_v1.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Output ---------------------------------------------------------------- */

typedef struct {
    uint8_t *bytes;
    size_t len, cap;
    bool nomem;
} RBufV1;

static void rb_put(RBufV1 *b, const char *text, size_t n) {
    if (b->nomem) return;
    if (b->len + n + 1u > b->cap) {
        size_t cap = b->cap ? b->cap * 2u : 65536u;
        while (cap < b->len + n + 1u) cap *= 2u;
        uint8_t *grown = realloc(b->bytes, cap);
        if (!grown) { b->nomem = true; return; }
        b->bytes = grown;
        b->cap = cap;
    }
    memcpy(b->bytes + b->len, text, n);
    b->len += n;
    b->bytes[b->len] = 0;
}

static void rb_puts(RBufV1 *b, const char *text) { rb_put(b, text, strlen(text)); }

static void rb_printf(RBufV1 *b, const char *format, ...) {
    char small[512];
    va_list args;
    va_start(args, format);
    int n = vsnprintf(small, sizeof small, format, args);
    va_end(args);
    if (n < 0) { b->nomem = true; return; }
    if ((size_t)n < sizeof small) { rb_put(b, small, (size_t)n); return; }
    char *big = malloc((size_t)n + 1u);
    if (!big) { b->nomem = true; return; }
    va_start(args, format);
    vsnprintf(big, (size_t)n + 1u, format, args);
    va_end(args);
    rb_put(b, big, (size_t)n);
    free(big);
}

/* a string of the wire: quoted, with " \ and newline escaped */
static void rb_wire_string(RBufV1 *b, const char *s, size_t n) {
    rb_put(b, "\"", 1u);
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '"' || s[i] == '\\') { rb_put(b, "\\", 1u); rb_put(b, &s[i], 1u); }
        else if (s[i] == '\n') rb_put(b, "\\n", 2u);
        else rb_put(b, &s[i], 1u);
    }
    rb_put(b, "\"", 1u);
}

static bool rulec_error(char *error, size_t cap, const char *format, ...) {
    if (error && cap) {
        va_list args;
        va_start(args, format);
        vsnprintf(error, cap, format, args);
        va_end(args);
    }
    return false;
}

static char *str_printf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    int n = vsnprintf(NULL, 0, format, args);
    va_end(args);
    if (n < 0) return NULL;
    char *s = malloc((size_t)n + 1u);
    if (!s) return NULL;
    va_start(args, format);
    vsnprintf(s, (size_t)n + 1u, format, args);
    va_end(args);
    return s;
}

/* ---- The C dialect's realization of the shared ground relations -------------
 * Each a test component of rulec's atoms representation (rc_atoms.h).  On
 * integers they are the relations of the PeTTa realization
 * (cetta_petta_ground_integer_relations_v1.metta). */

typedef struct {
    const char *name;
    size_t arity;
    const char *component;
    const char *why;
} GroundRelationV1;

static const GroundRelationV1 ground_relations[] = {
    {"different", 2u, "term-different",
     "the shared structural disequality of ground terms (ground_relations_v1.metta): two atoms differ"},
    {"ground-integer-less", 2u, "int-less",
     "a < b on the builtin-integer carrier, as cetta_petta_ground_integer_relations_v1.metta realizes it"},
    {"ground-integer-not-less", 2u, "int-not-less",
     "b <= a on the builtin-integer carrier, as cetta_petta_ground_integer_relations_v1.metta realizes it"},
    {"ground-integer-gap", 2u, "int-gap",
     "a + 1 < b on the builtin-integer carrier, as cetta_petta_ground_integer_relations_v1.metta realizes it"},
    {"ground-integer-no-gap", 2u, "int-no-gap",
     "b <= a + 1 on the builtin-integer carrier, as cetta_petta_ground_integer_relations_v1.metta realizes it"},
    {"ground-unicode-scalar", 1u, "unicode-scalar",
     "0 <= a < 55296 or 57343 < a <= 1114111, as cetta_petta_ground_integer_relations_v1.metta realizes it"},
    {"ground-non-unicode-scalar", 1u, "non-unicode-scalar",
     "a < 0, 1114111 < a, or 55296 <= a <= 57343, as cetta_petta_ground_integer_relations_v1.metta realizes it"},
};

static const GroundRelationV1 *ground_relation(const char *name, size_t arity) {
    for (size_t i = 0; i < sizeof ground_relations / sizeof *ground_relations; i++)
        if (ground_relations[i].arity == arity && strcmp(ground_relations[i].name, name) == 0)
            return &ground_relations[i];
    return NULL;
}

#define SAME_RELATION "gslt:same"

/* ---- Names -------------------------------------------------------------------
 * Constructors and judgments share rulec's namespace: a name is recorded with
 * what it stands for, and a second spelling of a name for something else is
 * refused. */

typedef struct {
    char *name;
    int kind;          /* NAME_* */
    char *sort;        /* the sort the constructor belongs to */
    size_t arity;
} NameV1;

enum { NAME_SYMBOL, NAME_EXPRESSION, NAME_INTEGER, NAME_STRING, NAME_OTHER, NAME_JUDGMENT, NAME_ANSWER, NAME_YES };

typedef struct {
    NameV1 *items;
    size_t len, cap;
    size_t *slot;      /* open addressing over items, by name */
    size_t slots;
} NamesV1;

static uint64_t name_hash(const char *s) {
    uint64_t h = 1469598103934665603ull;
    while (*s) { h ^= (uint8_t)*s++; h *= 1099511628211ull; }
    return h;
}

static long names_find(const NamesV1 *ns, const char *name) {
    if (!ns->slots) return -1;
    for (size_t i = name_hash(name) & (ns->slots - 1u);; i = (i + 1u) & (ns->slots - 1u)) {
        if (ns->slot[i] == 0) return -1;
        if (strcmp(ns->items[ns->slot[i] - 1u].name, name) == 0) return (long)(ns->slot[i] - 1u);
    }
}

static bool names_rehash(NamesV1 *ns) {
    size_t slots = ns->slots ? ns->slots * 2u : 1024u;
    size_t *slot = calloc(slots, sizeof *slot);
    if (!slot) return false;
    for (size_t k = 0; k < ns->len; k++)
        for (size_t i = name_hash(ns->items[k].name) & (slots - 1u);; i = (i + 1u) & (slots - 1u))
            if (slot[i] == 0) { slot[i] = k + 1u; break; }
    free(ns->slot);
    ns->slot = slot;
    ns->slots = slots;
    return true;
}

/* records NAME (owned) as KIND of SORT; true when it is new or already that */
static bool names_add(NamesV1 *ns, char *name, int kind, const char *sort, size_t arity,
                      char *error, size_t error_size) {
    long at = names_find(ns, name);
    if (at >= 0) {
        bool same = ns->items[at].kind == kind && ns->items[at].arity == arity && strcmp(ns->items[at].sort, sort) == 0;
        free(name);
        return same || rulec_error(error, error_size, "rulec-direct: the name %s is spelled by two different things",
                                   ns->items[at].name);
    }
    char *owned_sort = str_printf("%s", sort);
    if (!owned_sort) { free(name); return rulec_error(error, error_size, "rulec-direct: out of memory"); }
    if (ns->len == ns->cap) {
        size_t cap = ns->cap ? ns->cap * 2u : 256u;
        NameV1 *grown = realloc(ns->items, cap * sizeof *grown);
        if (!grown) { free(name); free(owned_sort); return rulec_error(error, error_size, "rulec-direct: out of memory"); }
        ns->items = grown;
        ns->cap = cap;
    }
    if (2u * (ns->len + 1u) > ns->slots && !names_rehash(ns)) {
        free(name);
        free(owned_sort);
        return rulec_error(error, error_size, "rulec-direct: out of memory");
    }
    ns->items[ns->len] = (NameV1){name, kind, owned_sort, arity};
    for (size_t i = name_hash(name) & (ns->slots - 1u);; i = (i + 1u) & (ns->slots - 1u))
        if (ns->slot[i] == 0) { ns->slot[i] = ns->len + 1u; break; }
    ns->len++;
    return true;
}

static void names_free(NamesV1 *ns) {
    for (size_t i = 0; i < ns->len; i++) { free(ns->items[i].name); free(ns->items[i].sort); }
    free(ns->items);
    free(ns->slot);
}


/* the constructor name of a data atom (not a variable): NULL with *error set
 * when it has none */
static char *atom_ctor_name(const Atom *t, int *kind, size_t *arity, char *error, size_t error_size) {
    *arity = 0;
    switch (t->kind) {
    case ATOM_SYMBOL:
        *kind = NAME_SYMBOL;
        return str_printf("%s", atom_name_cstr((Atom *)t));
    case ATOM_GROUNDED:
        if (t->ground.gkind == GV_INT) {
            *kind = NAME_INTEGER;
            return str_printf("%lld", (long long)t->ground.ival);
        }
        if (t->ground.gkind == GV_STRING) {
            if (memchr(t->ground.sval, 0, t->ground.slen)) {
                rulec_error(error, error_size, "rulec-direct: a string of the rules holds a NUL byte");
                return NULL;
            }
            *kind = NAME_STRING;
            return str_printf("\"%.*s\"", (int)t->ground.slen, t->ground.sval);
        }
        rulec_error(error, error_size, "rulec-direct: a grounded value of the rules other than an integer or a string");
        return NULL;
    case ATOM_EXPR:
        if (t->expr.len == 0u || t->expr.elems[0]->kind != ATOM_SYMBOL || cetta_gslt_source_variable_v1(t->expr.elems[0])) {
            rulec_error(error, error_size, "rulec-direct: an expression of the rules whose head is not a symbol");
            return NULL;
        }
        *kind = NAME_EXPRESSION;
        *arity = (size_t)t->expr.len - 1u;
        return str_printf("(%s %zu)", atom_name_cstr(t->expr.elems[0]), *arity);
    default:
        rulec_error(error, error_size, "rulec-direct: a term form C has no spelling of");
        return NULL;
    }
}

/* every constructor of a term of the rules */
static bool collect_ctors(NamesV1 *ns, const Atom *t, char *error, size_t error_size) {
    if (cetta_gslt_source_variable_v1(t)) return true;
    int kind;
    size_t arity;
    char *name = atom_ctor_name(t, &kind, &arity, error, error_size);
    if (!name || !names_add(ns, name, kind, "Term", arity, error, error_size)) return false;
    if (t->kind == ATOM_EXPR)
        for (CettaExprIndex i = 1; i < t->expr.len; i++)
            if (!collect_ctors(ns, t->expr.elems[i], error, error_size)) return false;
    return true;
}

/* ---- Variables ------------------------------------------------------------- */

typedef struct {
    const char **name;   /* GSLT names, then the fresh ones (owned) */
    bool *owned;
    bool *bound;
    size_t len, cap;
} VarsV1;

static long vars_find(const VarsV1 *v, const char *name) {
    for (size_t i = 0; i < v->len; i++)
        if (strcmp(v->name[i], name) == 0) return (long)i;
    return -1;
}

static long vars_add(VarsV1 *v, const char *name, bool owned) {
    if (v->len == v->cap) {
        size_t cap = v->cap ? v->cap * 2u : 16u;
        const char **n = realloc(v->name, cap * sizeof *n);
        if (n) v->name = n;
        bool *o = realloc(v->owned, cap * sizeof *o);
        if (o) v->owned = o;
        bool *b = realloc(v->bound, cap * sizeof *b);
        if (b) v->bound = b;
        if (!n || !o || !b) return -1;
        v->cap = cap;
    }
    v->name[v->len] = name;
    v->owned[v->len] = owned;
    v->bound[v->len] = false;
    return (long)v->len++;
}

static void vars_free(VarsV1 *v) {
    for (size_t i = 0; i < v->len; i++)
        if (v->owned[i]) free((char *)v->name[i]);
    free(v->name);
    free(v->owned);
    free(v->bound);
    *v = (VarsV1){0};
}

static bool term_ground(const VarsV1 *v, const Atom *t) {
    if (cetta_gslt_source_variable_v1(t)) {
        long i = vars_find(v, cetta_gslt_source_variable_name_v1(t));
        return i >= 0 && v->bound[i];
    }
    if (t->kind != ATOM_EXPR) return true;
    for (CettaExprIndex i = 1; i < t->expr.len; i++)
        if (!term_ground(v, t->expr.elems[i])) return false;
    return true;
}

/* ---- Patterns ----------------------------------------------------------------
 * A term written as a pattern of the wire.  In a pattern that binds (a left
 * side, a target), a variable met earlier in the same pattern, or bound
 * before a left side, is a fresh variable tested equal to it (a query of
 * gslt:same the reading adds to `same`); a variable a target meets that was
 * bound before the target is rulec's equality test; others become bound. */

typedef struct {
    RBufV1 *out;
    VarsV1 *vars;
    RBufV1 *same;        /* queries of gslt:same the pattern needs, each " (LCons (RelationQuery ...)" */
    size_t nsame;
    size_t fresh;
    bool binds;
    bool *before;        /* the variables bound when the pattern began */
    size_t nbefore;
    bool used_same;      /* some rule queries gslt:same */
    char *error;
    size_t error_size;
} PatV1;

/* starts a pattern: remembers what is bound */
static bool pattern_begin(PatV1 *x, bool binds) {
    x->binds = binds;
    free(x->before);
    x->nbefore = x->vars->len;
    x->before = malloc((x->nbefore ? x->nbefore : 1u) * sizeof *x->before);
    if (!x->before) return rulec_error(x->error, x->error_size, "rulec-direct: out of memory");
    if (x->nbefore) memcpy(x->before, x->vars->bound, x->nbefore * sizeof *x->before);
    return true;
}

static bool write_pattern(PatV1 *x, const Atom *t, bool target_eqv) {
    RBufV1 *b = x->out;
    if (cetta_gslt_source_variable_v1(t)) {
        const char *name = cetta_gslt_source_variable_name_v1(t);
        long i = vars_find(x->vars, name);
        if (!x->binds) {
            if (i < 0 || !x->vars->bound[i])
                return rulec_error(x->error, x->error_size, "rulec-direct: the variable ?%s is read before it is bound", name);
            rb_puts(b, "(FVar ");
            rb_wire_string(b, name, strlen(name));
            rb_puts(b, ")");
            return true;
        }
        if (i < 0) {
            i = vars_add(x->vars, name, false);
            if (i < 0) return rulec_error(x->error, x->error_size, "rulec-direct: out of memory");
        }
        /* a target compares a variable bound before it (rulec's equality test); a variable met again
         * inside the pattern, or again in a left side, is a fresh one compared with it */
        bool earlier = (size_t)i < x->nbefore && x->before[i];
        if (x->vars->bound[i] && target_eqv && earlier) {
            rb_puts(b, "(FVar ");
            rb_wire_string(b, name, strlen(name));
            rb_puts(b, ")");
            return true;
        }
        if (x->vars->bound[i]) {
            char *fresh = str_printf("%%same%zu", x->fresh++);
            long f = fresh ? vars_add(x->vars, fresh, true) : -1;
            if (f < 0) { free(fresh); return rulec_error(x->error, x->error_size, "rulec-direct: out of memory"); }
            x->vars->bound[f] = true;
            rb_puts(b, "(FVar ");
            rb_wire_string(b, fresh, strlen(fresh));
            rb_puts(b, ")");
            rb_puts(x->same, " (LCons (RelationQuery \"" SAME_RELATION "\" (LCons (FVar ");
            rb_wire_string(x->same, name, strlen(name));
            rb_puts(x->same, ") (LCons (FVar ");
            rb_wire_string(x->same, fresh, strlen(fresh));
            rb_puts(x->same, ") LNil)))");
            x->nsame++;
            x->used_same = true;
            return true;
        }
        x->vars->bound[i] = true;
        rb_puts(b, "(FVar ");
        rb_wire_string(b, name, strlen(name));
        rb_puts(b, ")");
        return true;
    }
    int kind;
    size_t arity;
    char *name = atom_ctor_name(t, &kind, &arity, x->error, x->error_size);
    if (!name) return false;
    rb_puts(b, "(PApp ");
    rb_wire_string(b, name, strlen(name));
    free(name);
    rb_puts(b, " ");
    size_t closes = 0;
    if (t->kind == ATOM_EXPR)
        for (CettaExprIndex i = 1; i < t->expr.len; i++) {
            rb_puts(b, "(LCons ");
            if (!write_pattern(x, t->expr.elems[i], target_eqv)) return false;
            rb_puts(b, " ");
            closes++;
        }
    rb_puts(b, "LNil");
    for (size_t i = 0; i < closes; i++) rb_puts(b, ")");
    rb_puts(b, ")");
    return true;
}

/* a constructor of the module applied to the patterns written by `kids` */
static void write_app_open(RBufV1 *b, const char *ctor) {
    rb_puts(b, "(PApp ");
    rb_wire_string(b, ctor, strlen(ctor));
    rb_puts(b, " ");
}

/* ---- Modes ------------------------------------------------------------------ */

static bool has_operator(const DirectOperatorsV1 *ops, const char *name, size_t arity) {
    for (size_t i = 0; i < ops->len; i++)
        if (ops->items[i].arity == arity && strcmp(ops->items[i].name, name) == 0) return true;
    return false;
}

static bool push_operator(DirectOperatorsV1 *ops, const char *name, size_t arity) {
    if (has_operator(ops, name, arity)) return true;
    if (ops->len == ops->cap) {
        size_t cap = ops->cap ? ops->cap * 2u : 32u;
        DirectOperatorV1 *grown = realloc(ops->items, cap * sizeof *grown);
        if (!grown) return false;
        ops->items = grown;
        ops->cap = cap;
    }
    ops->items[ops->len++] = (DirectOperatorV1){.name = name, .arity = arity};
    return true;
}

static const DirectBindingModeV1 *selected_mode(const DirectBindingModesV1 *modes, const char *relation,
                                                size_t arity, const uint8_t *input) {
    for (size_t i = 0; i < modes->len; i++) {
        const DirectBindingModeV1 *m = &modes->items[i];
        if (m->selected && m->arity == arity && strcmp(m->relation, relation) == 0 &&
            (arity == 0u || memcmp(m->input_ground, input, arity) == 0))
            return m;
    }
    return NULL;
}

static size_t mode_outputs(const DirectBindingModeV1 *m) {
    size_t n = 0;
    for (size_t i = 0; i < m->arity; i++) n += !m->input_ground[i];
    return n;
}

/* R:BITS */
static char *mode_name(const DirectBindingModeV1 *m) {
    char *s = malloc(strlen(m->relation) + m->arity + 2u);
    if (!s) return NULL;
    size_t n = strlen(m->relation);
    memcpy(s, m->relation, n);
    s[n++] = ':';
    for (size_t i = 0; i < m->arity; i++) s[n++] = m->input_ground[i] ? '1' : '0';
    s[n] = 0;
    return s;
}

/* the result sort and its constructor (the tuple's, or yes) of a mode */
static const char *mode_sort(const DirectBindingModeV1 *m, const char *name, char *buf, size_t cap) {
    size_t outs = mode_outputs(m);
    if (outs == 0u) return "Unit";
    if (outs == 1u) return "Term";
    snprintf(buf, cap, "%s answer", name);
    return buf;
}

/* ---- The reading of one rule in one mode ------------------------------------- */

typedef struct {
    const DirectRulesV1 *rules;
    const DirectBindingModesV1 *modes;
    const DirectOperatorsV1 *heads;          /* relations with rules */
    const DirectOperatorsV1 *capabilities;
    DirectOperatorsV1 *queried;              /* ground relations the module queries */
    bool *used_same;                         /* some rule queries gslt:same */
    char *error;
    size_t error_size;
} ReadingV1;

/* writes the wire row of RULE read in mode M into OUT */
static bool read_rule(ReadingV1 *x, const DirectRuleV1 *rule, size_t rule_index, const DirectBindingModeV1 *m,
                      RBufV1 *out) {
    const Atom *head = rule->head, *body = rule->body;
    VarsV1 vars = {0};
    RBufV1 lhs = {0}, prem = {0}, same = {0}, rhs = {0};
    size_t nprem = 0;
    bool ok = false;
    char *mname = mode_name(m);
    if (!mname) { rulec_error(x->error, x->error_size, "rulec-direct: out of memory"); goto done; }
    PatV1 px;
    px = (PatV1){.vars = &vars, .same = &same, .error = x->error, .error_size = x->error_size};
    /* the left side: the head's input patterns */
    px.out = &lhs;
    if (!pattern_begin(&px, true)) goto done;
    write_app_open(&lhs, mname);
    size_t closes = 0;
    for (size_t a = 0; a < m->arity; a++) {
        if (!m->input_ground[a]) continue;
        rb_puts(&lhs, "(LCons ");
        if (!write_pattern(&px, head->expr.elems[a + 1u], false)) goto done;
        rb_puts(&lhs, " ");
        closes++;
    }
    rb_puts(&lhs, "LNil");
    for (size_t i = 0; i < closes; i++) rb_puts(&lhs, ")");
    rb_puts(&lhs, ")");
    /* equalities of the left side first, then the goals */
    rb_put(&prem, same.len ? (const char *)same.bytes : "", same.len);
    nprem += px.nsame;
    same.len = 0;
    px.nsame = 0;
    for (CettaExprIndex g = 1; g < body->expr.len; g++) {
        const Atom *goal = body->expr.elems[g];
        const char *name = atom_name_cstr(goal->expr.elems[0]);
        size_t arity = (size_t)goal->expr.len - 1u;
        if (!has_operator(x->heads, name, arity) && has_operator(x->capabilities, name, arity)) {
            const GroundRelationV1 *gr = ground_relation(name, arity);
            if (!gr) {
                rulec_error(x->error, x->error_size,
                            "rulec-direct: rule %s in mode %s asks the ground relation %s/%zu, which C has no realization of",
                            rule->source->name, mname, name, arity);
                goto done;
            }
            for (size_t a = 0; a < arity; a++)
                if (!term_ground(&vars, goal->expr.elems[a + 1u])) {
                    rulec_error(x->error, x->error_size,
                                "rulec-direct: rule %s in mode %s asks the ground relation %s of an unbound argument",
                                rule->source->name, mname, name);
                    goto done;
                }
            if (!push_operator(x->queried, gr->name, gr->arity)) { rulec_error(x->error, x->error_size, "rulec-direct: out of memory"); goto done; }
            rb_puts(&prem, " (LCons (RelationQuery ");
            rb_wire_string(&prem, name, strlen(name));
            rb_puts(&prem, " ");
            px.out = &prem;
            if (!pattern_begin(&px, false)) goto done;
            for (size_t a = 0; a < arity; a++) {
                rb_puts(&prem, "(LCons ");
                if (!write_pattern(&px, goal->expr.elems[a + 1u], false)) goto done;
                rb_puts(&prem, " ");
            }
            rb_puts(&prem, "LNil");
            for (size_t a = 0; a < arity; a++) rb_puts(&prem, ")");
            rb_puts(&prem, ")");
            nprem++;
            continue;
        }
        uint8_t *input = calloc(arity ? arity : 1u, 1u);
        if (!input) { rulec_error(x->error, x->error_size, "rulec-direct: out of memory"); goto done; }
        for (size_t a = 0; a < arity; a++) input[a] = term_ground(&vars, goal->expr.elems[a + 1u]);
        const DirectBindingModeV1 *cm = selected_mode(x->modes, name, arity, input);
        free(input);
        if (!cm) {
            rulec_error(x->error, x->error_size, "rulec-direct: rule %s in mode %s calls %s/%zu in a mode the analysis did not select",
                        rule->source->name, mname, name, arity);
            goto done;
        }
        char *cname = mode_name(cm);
        if (!cname) { rulec_error(x->error, x->error_size, "rulec-direct: out of memory"); goto done; }
        rb_puts(&prem, " (LCons (Congruence ");
        write_app_open(&prem, cname);
        px.out = &prem;
        if (!pattern_begin(&px, false)) { free(cname); goto done; }
        size_t c2 = 0;
        for (size_t a = 0; a < arity; a++) {
            if (!cm->input_ground[a]) continue;
            rb_puts(&prem, "(LCons ");
            if (!write_pattern(&px, goal->expr.elems[a + 1u], false)) { free(cname); goto done; }
            rb_puts(&prem, " ");
            c2++;
        }
        rb_puts(&prem, "LNil");
        for (size_t i = 0; i < c2; i++) rb_puts(&prem, ")");
        rb_puts(&prem, ") ");
        /* the target: the outputs bound by matching */
        if (!pattern_begin(&px, true)) { free(cname); goto done; }
        size_t outs = mode_outputs(cm);
        if (outs == 0u) rb_puts(&prem, "(PApp \"yes\" LNil)");
        else if (outs == 1u) {
            for (size_t a = 0; a < arity; a++)
                if (!cm->input_ground[a] && !write_pattern(&px, goal->expr.elems[a + 1u], true)) { free(cname); goto done; }
        } else {
            char tuple[512];
            snprintf(tuple, sizeof tuple, "%s answer", cname);
            write_app_open(&prem, tuple);
            size_t c3 = 0;
            for (size_t a = 0; a < arity; a++) {
                if (cm->input_ground[a]) continue;
                rb_puts(&prem, "(LCons ");
                if (!write_pattern(&px, goal->expr.elems[a + 1u], true)) { free(cname); goto done; }
                rb_puts(&prem, " ");
                c3++;
            }
            rb_puts(&prem, "LNil");
            for (size_t i = 0; i < c3; i++) rb_puts(&prem, ")");
            rb_puts(&prem, ")");
        }
        free(cname);
        rb_puts(&prem, ")");
        nprem++;
        rb_put(&prem, same.len ? (const char *)same.bytes : "", same.len);
        nprem += px.nsame;
        same.len = 0;
        px.nsame = 0;
    }
    /* the right side: the head's outputs */
    for (size_t a = 0; a < m->arity; a++)
        if (!m->input_ground[a] && !term_ground(&vars, head->expr.elems[a + 1u])) {
            rulec_error(x->error, x->error_size, "rulec-direct: rule %s in mode %s leaves its output %zu unbound",
                        rule->source->name, mname, a + 1u);
            goto done;
        }
    px.out = &rhs;
    if (!pattern_begin(&px, false)) goto done;
    size_t outs = mode_outputs(m);
    if (outs == 0u) rb_puts(&rhs, "(PApp \"yes\" LNil)");
    else if (outs == 1u) {
        for (size_t a = 0; a < m->arity; a++)
            if (!m->input_ground[a] && !write_pattern(&px, head->expr.elems[a + 1u], false)) goto done;
    } else {
        char tuple[512];
        snprintf(tuple, sizeof tuple, "%s answer", mname);
        write_app_open(&rhs, tuple);
        size_t c3 = 0;
        for (size_t a = 0; a < m->arity; a++) {
            if (m->input_ground[a]) continue;
            rb_puts(&rhs, "(LCons ");
            if (!write_pattern(&px, head->expr.elems[a + 1u], false)) goto done;
            rb_puts(&rhs, " ");
            c3++;
        }
        rb_puts(&rhs, "LNil");
        for (size_t i = 0; i < c3; i++) rb_puts(&rhs, ")");
        rb_puts(&rhs, ")");
    }
    /* the row: label, variables, premises, left side, right side */
    {
        bool unique = true;
        for (size_t o = 0; o < x->rules->len; o++)
            if (o != rule_index && x->rules->items[o].source && rule->source &&
                strcmp(x->rules->items[o].source->name, rule->source->name) == 0)
                unique = false;
        char *label = unique ? str_printf("%s:%s", rule->source->name, strchr(mname, ':') + 1)
                             : str_printf("%s#%zu:%s", rule->source->name, rule->source_index, strchr(mname, ':') + 1);
        if (!label) { rulec_error(x->error, x->error_size, "rulec-direct: out of memory"); goto done; }
        rb_puts(out, "(ScopedRewriteRuleV1 ");
        rb_wire_string(out, label, strlen(label));
        free(label);
        rb_puts(out, " ");
        for (size_t v = 0; v < vars.len; v++) {
            rb_puts(out, "(LCons (TypeBinding ");
            rb_wire_string(out, vars.name[v], strlen(vars.name[v]));
            rb_puts(out, " (TBase \"Term\")) ");
        }
        rb_puts(out, "LNil");
        for (size_t v = 0; v < vars.len; v++) rb_puts(out, ")");
        rb_puts(out, " ");
        if (prem.len) rb_put(out, (const char *)prem.bytes + 1, prem.len - 1u);
        rb_puts(out, prem.len ? " LNil" : "LNil");
        for (size_t i = 0; i < nprem; i++) rb_puts(out, ")");
        rb_puts(out, " ");
        rb_put(out, (const char *)lhs.bytes, lhs.len);
        rb_puts(out, " ");
        rb_put(out, (const char *)rhs.bytes, rhs.len);
        rb_puts(out, " RuleBindingNone)\n");
    }
    ok = !out->nomem && !lhs.nomem && !prem.nomem && !rhs.nomem && !same.nomem;
    if (!ok) rulec_error(x->error, x->error_size, "rulec-direct: out of memory");
    if (px.used_same) *x->used_same = true;
done:
    free(px.before);
    free(mname);
    free(lhs.bytes); free(prem.bytes); free(same.bytes); free(rhs.bytes);
    vars_free(&vars);
    return ok;
}

static bool rule_of_mode(const DirectRuleV1 *rule, const DirectBindingModeV1 *m) {
    const Atom *h = rule->head;
    return (size_t)h->expr.len - 1u == m->arity && strcmp(atom_name_cstr(h->expr.elems[0]), m->relation) == 0;
}

bool cetta_gslt_render_rulec_v1(
    const DirectRulesV1 *rules, const DirectBindingModesV1 *modes,
    const DirectOperatorsV1 *operators,
    const char *const *entry_modes, size_t entry_mode_count,
    const char *admitted_entry,
    const char *source_digest,
    uint8_t **wire_out, size_t *wire_len,
    uint8_t **bindings_out, size_t *bindings_len,
    char *error, size_t error_size) {
    DirectOperatorsV1 heads = {0}, capabilities = {0}, queried = {0};
    NamesV1 names = {0};
    RBufV1 rows = {0}, wire = {0}, bind = {0};
    char *admitted = NULL, *admitted_sort = NULL;
    bool ok = false;
    *wire_out = *bindings_out = NULL;
    *wire_len = *bindings_len = 0;
    for (size_t i = 0; i < rules->len; i++) {
        const Atom *h = rules->items[i].head;
        const char *name = atom_name_cstr(h->expr.elems[0]);
        if (!push_operator(&heads, name, (size_t)h->expr.len - 1u)) goto nomem;
        if (strcmp(name, "oslf-external-relation-decl-v1") == 0 && h->expr.len == 3u &&
            h->expr.elems[1]->kind == ATOM_SYMBOL && h->expr.elems[2]->kind == ATOM_GROUNDED &&
            h->expr.elems[2]->ground.gkind == GV_INT && h->expr.elems[2]->ground.ival > 0 &&
            !push_operator(&capabilities, atom_name_cstr(h->expr.elems[1]), (size_t)h->expr.elems[2]->ground.ival))
            goto nomem;
    }
    bool used_same = false;
    ReadingV1 x = {.rules = rules, .modes = modes, .heads = &heads, .capabilities = &capabilities,
                   .queried = &queried, .used_same = &used_same, .error = error, .error_size = error_size};
    /* the constructors: every operator of the composition, every atom of the rules read */
    for (size_t i = 0; i < operators->len; i++) {
        char *n = str_printf("(%s %zu)", operators->items[i].name, operators->items[i].arity);
        if (!n || !names_add(&names, n, NAME_EXPRESSION, "Term", operators->items[i].arity, error, error_size)) goto done;
    }
    static const char *const others[] = {"other integer", "other string", "other symbol", "other expression"};
    for (size_t i = 0; i < 4u; i++) {
        char *n = str_printf("%s", others[i]);
        if (!n || !names_add(&names, n, NAME_OTHER, "Term", 0u, error, error_size)) goto done;
    }
    {
        char *n = str_printf("yes");
        if (!n || !names_add(&names, n, NAME_YES, "Unit", 0u, error, error_size)) goto done;
    }
    for (size_t i = 0; i < modes->len; i++) {
        if (!modes->items[i].selected) continue;
        const DirectBindingModeV1 *m = &modes->items[i];
        char *n = mode_name(m);
        if (!n) goto nomem;
        char sb[512];
        const char *sort = mode_sort(m, n, sb, sizeof sb);
        size_t ins = m->arity - mode_outputs(m);
        if (mode_outputs(m) > 1u) {
            char *t = str_printf("%s", sb);
            if (!t || !names_add(&names, t, NAME_ANSWER, sort, mode_outputs(m), error, error_size)) { free(n); goto done; }
        }
        if (!names_add(&names, n, NAME_JUDGMENT, sort, ins, error, error_size)) goto done;
        for (size_t r = 0; r < rules->len; r++) {
            if (!rule_of_mode(&rules->items[r], m)) continue;
            if (!collect_ctors(&names, rules->items[r].head, error, error_size)) goto done;
            const Atom *body = rules->items[r].body;
            for (CettaExprIndex g = 1; g < body->expr.len; g++)
                for (CettaExprIndex a = 1; a < body->expr.elems[g]->expr.len; a++)
                    if (!collect_ctors(&names, body->expr.elems[g]->expr.elems[a], error, error_size)) goto done;
        }
    }
    /* the admitted entry: a judgment of the language and the entry's one input, of the entry's sort */
    if (admitted_entry) {
        long e = names_find(&names, admitted_entry);
        if (e < 0 || names.items[e].kind != NAME_JUDGMENT || names.items[e].arity != 1u) {
            rulec_error(error, error_size, "rulec-direct: the admitted entry %s is no entry mode of one input", admitted_entry);
            goto done;
        }
        admitted = str_printf("gslt:admitted-entry:%s", admitted_entry);
        admitted_sort = str_printf("%s", names.items[e].sort);
        char *n = str_printf("%s", admitted ? admitted : "");
        if (!admitted || !admitted_sort || !n || !names_add(&names, n, NAME_JUDGMENT, admitted_sort, 2u, error, error_size))
            goto done;
    }
    /* langdef's source list: (cons (cp N) ...) ending in the symbol nil */
    bool source = names_find(&names, "(cons 2)") >= 0 && names_find(&names, "(cp 1)") >= 0;
    if (source) {
        char *n = str_printf("nil");
        if (!n || !names_add(&names, n, NAME_SYMBOL, "Term", 0u, error, error_size)) goto done;
    }
    /* the rules */
    size_t nrules = 0;
    for (size_t i = 0; i < modes->len; i++) {
        if (!modes->items[i].selected) continue;
        for (size_t r = 0; r < rules->len; r++) {
            if (!rule_of_mode(&rules->items[r], &modes->items[i])) continue;
            if (!read_rule(&x, &rules->items[r], r, &modes->items[i], &rows)) goto done;
            nrules++;
        }
    }
    if (admitted) {
        rb_puts(&rows, "(ScopedRewriteRuleV1 ");
        rb_wire_string(&rows, admitted, strlen(admitted));
        rb_puts(&rows, " (LCons (TypeBinding \"language\" (TBase \"Term\")) (LCons (TypeBinding \"input\" (TBase \"Term\")) "
                       "(LCons (TypeBinding \"answer\" (TBase ");
        rb_wire_string(&rows, admitted_sort, strlen(admitted_sort));
        rb_puts(&rows, ")) LNil))) (LCons (Congruence (PApp ");
        rb_wire_string(&rows, admitted_entry, strlen(admitted_entry));
        rb_puts(&rows, " (LCons (FVar \"input\") LNil)) (FVar \"answer\")) LNil) (PApp ");
        rb_wire_string(&rows, admitted, strlen(admitted));
        rb_puts(&rows, " (LCons (FVar \"language\") (LCons (FVar \"input\") LNil))) (FVar \"answer\") RuleBindingNone)\n");
        nrules++;
    }
    /* the wire: module, sorts, constructors, rules */
    rb_puts(&wire, "; GSLT relations read as a rule module of rulec (native/gslt_rulec_direct_v1.c)\n");
    rb_printf(&wire, "; source-composition-sha256 %s\n", source_digest);
    for (size_t e = 0; e < entry_mode_count; e++) rb_printf(&wire, "; entry %s\n", entry_modes[e]);
    rb_puts(&wire, "(wire-module \"gslt\" \"direct\")\n");
    size_t ntypes = 0, nterms = 0;
    rb_puts(&wire, "(TypeDecl \"Term\" CarrierAst)\n(TypeDecl \"Unit\" CarrierAst)\n");
    ntypes += 2;
    for (size_t i = 0; i < names.len; i++)
        if (names.items[i].kind == NAME_ANSWER) {
            rb_puts(&wire, "(TypeDecl ");
            rb_wire_string(&wire, names.items[i].sort, strlen(names.items[i].sort));
            rb_puts(&wire, " CarrierAst)\n");
            ntypes++;
        }
    for (size_t i = 0; i < names.len; i++) {
        const NameV1 *n = &names.items[i];
        rb_puts(&wire, "(GrammarRuleV2 ");
        rb_wire_string(&wire, n->name, strlen(n->name));
        rb_puts(&wire, " ");
        rb_wire_string(&wire, n->sort, strlen(n->sort));
        rb_puts(&wire, " ");
        for (size_t a = 0; a < n->arity; a++) rb_printf(&wire, "(LCons (TermSimple \"x%zu\" (TBase \"Term\")) ", a);
        rb_puts(&wire, "LNil");
        for (size_t a = 0; a < n->arity; a++) rb_puts(&wire, ")");
        rb_puts(&wire, " LNil EvalNone AlgebraNone)\n");
        nterms++;
    }
    for (size_t i = 0; i < names.len; i++)
        if (names.items[i].kind == NAME_JUDGMENT) {
            rb_puts(&wire, "(Judgment ");
            rb_wire_string(&wire, names.items[i].name, strlen(names.items[i].name));
            rb_puts(&wire, ")\n");
        }
    if (rows.len) rb_put(&wire, (const char *)rows.bytes, rows.len);
    rb_printf(&wire, "(wire-end %zu %zu 0 %zu)\n", ntypes, nterms, nrules);
    /* the binding table */
    rb_puts(&bind, "; GSLT terms and ground relations in C (native/gslt_rulec_direct_v1.c)\n");
    rb_puts(&bind, "(rep \"Term\" atoms \"other integer\" \"other string\" \"other symbol\" \"other expression\")\n");
    if (source) rb_puts(&bind, "(atoms-source \"Term\" \"(cons 2)\" \"(cp 1)\" \"nil\")\n");
    if (used_same)
        rb_puts(&bind, "(test \"equal\" \"" SAME_RELATION "\" (term-equal) \"trusted\" "
                       "\"a variable met again in a pattern: the two occurrences are the same atom\")\n");
    for (size_t i = 0; i < queried.len; i++) {
        const GroundRelationV1 *gr = ground_relation(queried.items[i].name, queried.items[i].arity);
        rb_printf(&bind, "(test \"%s\" \"%s\" (%s) \"trusted\" ", gr->name, gr->name, gr->component);
        rb_wire_string(&bind, gr->why, strlen(gr->why));
        rb_puts(&bind, ")\n");
    }
    if (wire.nomem || bind.nomem || rows.nomem) goto nomem;
    *wire_out = wire.bytes;
    *wire_len = wire.len;
    *bindings_out = bind.bytes;
    *bindings_len = bind.len;
    wire.bytes = bind.bytes = NULL;
    ok = true;
    goto done;
nomem:
    rulec_error(error, error_size, "rulec-direct: out of memory");
done:
    free(admitted);
    free(admitted_sort);
    free(heads.items);
    free(capabilities.items);
    free(queried.items);
    names_free(&names);
    free(rows.bytes);
    free(wire.bytes);
    free(bind.bytes);
    return ok;
}

/* ---- rulec ------------------------------------------------------------------
 * The compiler of tools/rulec/compiler, linked in (its one global name is
 * rulec_main; it runs once per process).  It reads the wire and the binding
 * table as files and writes the C file, the decision DAGs and the census. */

int rulec_main(int argc, char **argv);

static char *g_rulec_output;

void cetta_gslt_rulec_set_output_v1(const char *c_path) {
    free(g_rulec_output);
    g_rulec_output = c_path ? str_printf("%s", c_path) : NULL;
}

static bool write_file(const char *path, const uint8_t *bytes, size_t n, char *error, size_t error_size) {
    FILE *f = fopen(path, "wb");
    if (!f) return rulec_error(error, error_size, "rulec-direct: cannot write %s", path);
    bool ok = fwrite(bytes, 1, n, f) == n;
    ok = fclose(f) == 0 && ok;
    return ok || rulec_error(error, error_size, "rulec-direct: cannot write %s", path);
}

static bool read_file(const char *path, uint8_t **out, size_t *n, char *error, size_t error_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return rulec_error(error, error_size, "rulec-direct: cannot read %s", path);
    size_t cap = 1u << 20, len = 0;
    uint8_t *b = malloc(cap);
    for (size_t got; b && (got = fread(b + len, 1, cap - len, f)) > 0;) {
        len += got;
        if (len == cap) {
            uint8_t *grown = realloc(b, cap * 2u);
            if (!grown) { free(b); b = NULL; break; }
            b = grown;
            cap *= 2u;
        }
    }
    fclose(f);
    if (!b) return rulec_error(error, error_size, "rulec-direct: out of memory");
    *out = b;
    *n = len;
    return true;
}

bool cetta_gslt_rulec_compile_v1(
    const uint8_t *wire, size_t wire_len,
    const uint8_t *bindings, size_t bindings_len,
    const char *source_digest,
    uint8_t **c_out, size_t *c_len,
    char *error, size_t error_size) {
    *c_out = NULL;
    *c_len = 0;
    if (!g_rulec_output) return rulec_error(error, error_size, "rulec-direct: the C target needs its output path");
    const char *base = g_rulec_output;
    char *wire_path = str_printf("%s.wire", base), *bind_path = str_printf("%s.bindings", base),
         *dag_path = str_printf("%s.dag", base), *census_path = str_printf("%s.census", base),
         *c_path = str_printf("%s.c.tmp", base), *digest = str_printf("%s", source_digest);
    bool ok = false;
    if (!wire_path || !bind_path || !dag_path || !census_path || !c_path || !digest) {
        rulec_error(error, error_size, "rulec-direct: out of memory");
        goto done;
    }
    if (!write_file(wire_path, wire, wire_len, error, error_size) ||
        !write_file(bind_path, bindings, bindings_len, error, error_size))
        goto done;
    char *argv[] = {"rulec", wire_path, "--bindings", bind_path, "--dag", dag_path, "--census", census_path,
                    "--include-rt", "native/rule_runtime/rc_rt.h", "--component-prefix", "native/rule_components/",
                    "--source-digest", digest, "--emit", c_path, NULL};
    int status = rulec_main((int)(sizeof argv / sizeof *argv) - 1, argv);
    if (status != 0) {
        rulec_error(error, error_size, "rulec-direct: rulec refused the module (%s; its reasons are above)", wire_path);
        goto done;
    }
    ok = read_file(c_path, c_out, c_len, error, error_size);
    remove(c_path);
done:
    free(wire_path); free(bind_path); free(dag_path); free(census_path); free(c_path); free(digest);
    return ok;
}

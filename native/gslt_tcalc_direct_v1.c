/*
 * The tcalc target of the direct GSLT compilers: the selected modes of a
 * closed entry residual as a tcalc program over a set store, run by step
 * every, read by demand as lib/tcalc_modules.metta reads a rule module.
 *
 * A GSLT relation is a moded Horn relation.  A mode R:BITS (one bit per
 * argument, 1 for an input) is called on the terms of its inputs; its call
 * term is (R:BITS IN ...), and its rows are
 *
 *   (demand CALL)              the call is asked
 *   (value CALL OUT ...)       the call has these outputs, one row per answer
 *   (L:BITS/i CALL V ...)      rule L, read in mode BITS, has matched CALL and
 *                              its first i relation premises; V are the
 *                              variables those premises bound that a later
 *                              premise or the head's outputs use
 *
 * A rule L of the mode, head (R P1 .. Pn), body G1 .. Gm, reads the demand
 * (demand (R:BITS Pin ...)), the head's input patterns matching the call.
 * Each relation premise (K A1 .. Ak) is called in the mode its arguments'
 * groundness selects, the analysis's; its rules are
 *
 *   L:BITS/i.ask  reads stage i-1, makes (demand (K:B Ain ...))
 *   L:BITS/i      reads stage i-1 and (value (K:B Ain ...) Aout ...), makes
 *                 stage i, or for the last relation premise the answer
 *                 (value (R:BITS Pin ...) Pout ...)
 *
 * where stage 0 is the demand row.  A rule with no relation premise reads
 * the demand and makes the answer (rule L:BITS).  A premise of a ground
 * relation (a capability: different, the integer comparisons) is a guard of
 * tcalc's, decided on values the rule's reads bind: a guard before the first
 * relation premise is checked by the rules that read the demand, one after
 * premise i by L:BITS/i.  A ground relation that is a disjunction of guards
 * gives each rule that checks it once per disjunct (L:BITS/i.1, .2, ...).
 *
 * GSLT terms are free terms over their signature and are written as they are
 * authored: a source cons is a constructor like any other, and no host list
 * stands for a sequence.  Every rule only reads and makes, so the program
 * meets tcalc's licence for semi-naive steps (RuleSyntax/SetStoreFamilies:
 * Propagates), and every run reaches one store: the calls asked, transitively,
 * each with every answer its relation gives it.
 *
 * Refused, with the reason: a premise of a ground relation tcalc has no
 * realization of, or whose arguments the premises before it leave unbound; a
 * head output the rule leaves unbound (tcalc would make an unknown, which no
 * GSLT rule means); a term tcalc's notation cannot write; and a generated row
 * head that is also an operator of the signature.
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
} TBufV1;

static void tb_put(TBufV1 *b, const char *text, size_t n) {
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

static void tb_puts(TBufV1 *b, const char *text) { tb_put(b, text, strlen(text)); }

static void tb_printf(TBufV1 *b, const char *format, ...) {
    char small[512];
    va_list args;
    va_start(args, format);
    int n = vsnprintf(small, sizeof small, format, args);
    va_end(args);
    if (n < 0) { b->nomem = true; return; }
    if ((size_t)n < sizeof small) { tb_put(b, small, (size_t)n); return; }
    char *big = malloc((size_t)n + 1u);
    if (!big) { b->nomem = true; return; }
    va_start(args, format);
    vsnprintf(big, (size_t)n + 1u, format, args);
    va_end(args);
    tb_put(b, big, (size_t)n);
    free(big);
}

static bool tcalc_error(char *error, size_t cap, const char *format, ...) {
    if (error && cap) {
        va_list args;
        va_start(args, format);
        vsnprintf(error, cap, format, args);
        va_end(args);
    }
    return false;
}

/* ---- Terms ------------------------------------------------------------------
 * A symbol is written as it is, a variable ?x as $x, an integer in decimal and
 * a string quoted with " and \ escaped.  A symbol tcalc would read as
 * something else (a variable, an integer, or several words) is refused. */

static bool tcalc_word(const char *name) {
    if (!name[0] || name[0] == '$') return false;
    for (const char *p = name; *p; p++)
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '(' || *p == ')' ||
            *p == ';' || *p == '"')
            return false;
    size_t i = name[0] == '-' && name[1] ? 1u : 0u;
    bool digits = name[i] != 0;
    for (const char *p = name + i; *p; p++) digits = digits && *p >= '0' && *p <= '9';
    return !digits;
}

static bool render_term(TBufV1 *b, const Atom *t, char *error, size_t error_size) {
    if (cetta_gslt_source_variable_v1(t)) {
        const char *name = cetta_gslt_source_variable_name_v1(t);
        if (!tcalc_word(name))
            return tcalc_error(error, error_size, "tcalc-direct: the variable ?%s has no tcalc spelling", name);
        tb_put(b, "$", 1u);
        tb_puts(b, name);
        return true;
    }
    switch (t->kind) {
    case ATOM_SYMBOL: {
        const char *name = atom_name_cstr((Atom *)t);
        if (!tcalc_word(name))
            return tcalc_error(error, error_size, "tcalc-direct: the symbol %s has no tcalc spelling", name);
        tb_puts(b, name);
        return true;
    }
    case ATOM_GROUNDED:
        if (t->ground.gkind == GV_INT) {
            tb_printf(b, "%lld", (long long)t->ground.ival);
            return true;
        }
        if (t->ground.gkind == GV_STRING) {
            tb_put(b, "\"", 1u);
            for (uint32_t i = 0; i < t->ground.slen; i++) {
                char c = t->ground.sval[i];
                if (c == '"' || c == '\\') tb_put(b, "\\", 1u);
                tb_put(b, &c, 1u);
            }
            tb_put(b, "\"", 1u);
            return true;
        }
        return tcalc_error(error, error_size, "tcalc-direct: a grounded value other than an integer or a string");
    case ATOM_EXPR:
        tb_put(b, "(", 1u);
        for (CettaExprIndex i = 0; i < t->expr.len; i++) {
            if (i) tb_put(b, " ", 1u);
            if (!render_term(b, t->expr.elems[i], error, error_size)) return false;
        }
        tb_put(b, ")", 1u);
        return true;
    default:
        return tcalc_error(error, error_size, "tcalc-direct: a term form tcalc has no spelling of");
    }
}

/* ---- Variables, by first appearance ---------------------------------------- */

typedef struct {
    const char **name;
    size_t len, cap;
} VarsV1;

static bool vars_has(const VarsV1 *v, const char *name) {
    for (size_t i = 0; i < v->len; i++)
        if (strcmp(v->name[i], name) == 0) return true;
    return false;
}

static bool vars_add(VarsV1 *v, const char *name) {
    if (vars_has(v, name)) return true;
    if (v->len == v->cap) {
        size_t cap = v->cap ? v->cap * 2u : 16u;
        const char **grown = realloc(v->name, cap * sizeof *grown);
        if (!grown) return false;
        v->name = grown;
        v->cap = cap;
    }
    v->name[v->len++] = name;
    return true;
}

static bool vars_of(VarsV1 *v, const Atom *t) {
    if (cetta_gslt_source_variable_v1(t)) return vars_add(v, cetta_gslt_source_variable_name_v1(t));
    if (t->kind != ATOM_EXPR) return true;
    for (CettaExprIndex i = 0; i < t->expr.len; i++)
        if (!vars_of(v, t->expr.elems[i])) return false;
    return true;
}

static bool term_ground(const VarsV1 *known, const Atom *t) {
    if (cetta_gslt_source_variable_v1(t)) return vars_has(known, cetta_gslt_source_variable_name_v1(t));
    if (t->kind != ATOM_EXPR) return true;
    for (CettaExprIndex i = 0; i < t->expr.len; i++)
        if (!term_ground(known, t->expr.elems[i])) return false;
    return true;
}

/* ---- Ground relations as guards ---------------------------------------------
 * tcalc's realization of the shared ground relations, fixed once for the
 * dialect as PeTTa's and HE's are: each a disjunction of conjunctions of
 * guards over the arguments (%1, %2).  On integers they are the relations of
 * the PeTTa realization (cetta_petta_ground_integer_relations_v1.metta); a
 * gap a + 1 < b needs a successor, which no combination of /=, < and <= over
 * a and b and constants defines, so its guard compares a sum. */

typedef struct {
    const char *name;
    size_t arity;
    const char *disjuncts[4];
} GroundRelationV1;

static const GroundRelationV1 ground_relations[] = {
    {"different", 2u, {"(/= %1 %2)", NULL}},
    {"ground-integer-less", 2u, {"(< %1 %2)", NULL}},
    {"ground-integer-not-less", 2u, {"(<= %2 %1)", NULL}},
    {"ground-integer-gap", 2u, {"(< (+ %1 1) %2)", NULL}},
    {"ground-integer-no-gap", 2u, {"(<= %2 (+ %1 1))", NULL}},
    {"ground-unicode-scalar", 1u, {"(<= 0 %1) (< %1 55296)", "(< 57343 %1) (<= %1 1114111)", NULL}},
    {"ground-non-unicode-scalar", 1u,
     {"(< %1 0)", "(< 1114111 %1)", "(<= 55296 %1) (<= %1 57343)", NULL}},
};

static const GroundRelationV1 *ground_relation(const char *name, size_t arity) {
    for (size_t i = 0; i < sizeof ground_relations / sizeof *ground_relations; i++)
        if (ground_relations[i].arity == arity && strcmp(ground_relations[i].name, name) == 0)
            return &ground_relations[i];
    return NULL;
}

static size_t disjunct_count(const GroundRelationV1 *g) {
    size_t n = 0;
    while (n < 4u && g->disjuncts[n]) n++;
    return n;
}

/* Writes disjunct d of the guard premise `goal`, its arguments put in. */
static bool render_disjunct(TBufV1 *b, const GroundRelationV1 *g, size_t d, const Atom *goal,
                            char *error, size_t error_size) {
    for (const char *p = g->disjuncts[d]; *p; p++) {
        if (*p == '%' && p[1] >= '1' && p[1] <= '9') {
            size_t arg = (size_t)(p[1] - '0');
            if (!render_term(b, goal->expr.elems[arg], error, error_size)) return false;
            p++;
        } else {
            tb_put(b, p, 1u);
        }
    }
    return true;
}

/* ---- The analysis's modes and the rules they read ---------------------------- */

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

static void mode_bits(TBufV1 *b, const DirectBindingModeV1 *m) {
    for (size_t i = 0; i < m->arity; i++) tb_put(b, m->input_ground[i] ? "1" : "0", 1u);
}

/* A premise of a rule read in a mode: a relation called in a mode, or a
 * ground relation read as guards. */
typedef struct {
    const Atom *goal;
    const DirectBindingModeV1 *mode;   /* relation premise */
    const GroundRelationV1 *ground;    /* guard premise */
} PremiseV1;

typedef struct {
    TBufV1 *out;
    const DirectBindingModeV1 *mode;
    const DirectRuleV1 *rule;
    const char *label;           /* L:BITS */
    PremiseV1 *premise;
    size_t premises;
    size_t relations;            /* relation premises */
    size_t rules_written;
    char *error;
    size_t error_size;
} ReadingV1;

/* (R:BITS IN ...): the call of a mode on the given arguments. */
static bool render_call(TBufV1 *b, const DirectBindingModeV1 *m, const Atom *app,
                        char *error, size_t error_size) {
    tb_puts(b, "(");
    tb_puts(b, m->relation);
    tb_put(b, ":", 1u);
    mode_bits(b, m);
    for (size_t i = 0; i < m->arity; i++) {
        if (!m->input_ground[i]) continue;
        tb_put(b, " ", 1u);
        if (!render_term(b, app->expr.elems[i + 1u], error, error_size)) return false;
    }
    tb_puts(b, ")");
    return true;
}

/* (value CALL OUT ...) of a mode on the given arguments. */
static bool render_value(TBufV1 *b, const DirectBindingModeV1 *m, const Atom *app,
                         char *error, size_t error_size) {
    tb_puts(b, "(value ");
    if (!render_call(b, m, app, error, error_size)) return false;
    for (size_t i = 0; i < m->arity; i++) {
        if (m->input_ground[i]) continue;
        tb_put(b, " ", 1u);
        if (!render_term(b, app->expr.elems[i + 1u], error, error_size)) return false;
    }
    tb_puts(b, ")");
    return true;
}

/* The variables stage r carries: those that premises 1..r bound (`known`),
 * not in the call, used by a premise after the r-th relation premise's guards
 * or by the head's outputs. */
static bool stage_vars(const ReadingV1 *x, size_t r, const VarsV1 *known, VarsV1 *out) {
    VarsV1 call = {0}, used = {0};
    const Atom *head = x->rule->head;
    bool ok = true;
    for (size_t i = 0; ok && i < x->mode->arity; i++)
        if (x->mode->input_ground[i]) ok = vars_of(&call, head->expr.elems[i + 1u]);
    size_t seen = 0, from = x->premises;
    for (size_t k = 0; k < x->premises; k++)
        if (x->premise[k].mode && ++seen == r + 1u) { from = k; break; }
    for (size_t k = from; ok && k < x->premises; k++) ok = vars_of(&used, x->premise[k].goal);
    for (size_t i = 0; ok && i < x->mode->arity; i++)
        if (!x->mode->input_ground[i]) ok = vars_of(&used, head->expr.elems[i + 1u]);
    for (size_t i = 0; ok && i < used.len; i++)
        if (vars_has(known, used.name[i]) && !vars_has(&call, used.name[i])) ok = vars_add(out, used.name[i]);
    free(call.name);
    free(used.name);
    return ok;
}

/* Stage r's row: the demand for r = 0, else (L:BITS/r CALL V ...). */
static bool render_stage(const ReadingV1 *x, size_t r, const VarsV1 *carried) {
    TBufV1 *b = x->out;
    if (r == 0) {
        tb_puts(b, "(demand ");
        if (!render_call(b, x->mode, x->rule->head, x->error, x->error_size)) return false;
        tb_puts(b, ")");
        return true;
    }
    tb_printf(b, "(%s/%zu ", x->label, r);
    if (!render_call(b, x->mode, x->rule->head, x->error, x->error_size)) return false;
    for (size_t i = 0; i < carried->len; i++) tb_printf(b, " $%s", carried->name[i]);
    tb_puts(b, ")");
    return true;
}

/* The guard premises from index k on, up to the next relation premise:
 * the number of their disjunct combinations, and combination c written as
 * checks. */
static size_t guard_combinations(const ReadingV1 *x, size_t k) {
    size_t n = 1;
    for (; k < x->premises && !x->premise[k].mode; k++) n *= disjunct_count(x->premise[k].ground);
    return n;
}

static bool render_guards(const ReadingV1 *x, size_t k, size_t c) {
    for (; k < x->premises && !x->premise[k].mode; k++) {
        size_t n = disjunct_count(x->premise[k].ground);
        tb_puts(x->out, " (check ");
        if (!render_disjunct(x->out, x->premise[k].ground, c % n, x->premise[k].goal,
                             x->error, x->error_size))
            return false;
        tb_puts(x->out, ")");
        c /= n;
    }
    return true;
}

static void rule_name(const ReadingV1 *x, const char *suffix, size_t r, size_t alt, size_t alts) {
    if (r) tb_printf(x->out, "(rule %s/%zu%s", x->label, r, suffix);
    else tb_printf(x->out, "(rule %s%s", x->label, suffix);
    if (alts > 1u) tb_printf(x->out, ".%zu", alt + 1u);
}

/* The tcalc rules of one GSLT rule read in one mode (the reading above). */
static bool read_rule(ReadingV1 *x) {
    const Atom *head = x->rule->head;
    TBufV1 *b = x->out;
    VarsV1 known = {0}, carried = {0}, next = {0};
    bool ok = true;
    for (size_t i = 0; ok && i < x->mode->arity; i++)
        if (x->mode->input_ground[i]) ok = vars_of(&known, head->expr.elems[i + 1u]);
    if (!ok) goto nomem;
    size_t lead = 0;   /* the guard premises before the first relation premise */
    while (lead < x->premises && !x->premise[lead].mode) lead++;
    size_t r = 0, k = lead;
    /* stage r-1 (0: the demand) is read by the rules of relation premise r */
    while (k < x->premises) {
        const PremiseV1 *p = &x->premise[k];
        r++;
        bool last = r == x->relations;
        size_t ask_alts = r == 1u ? guard_combinations(x, 0) : 1u;
        size_t trail = k + 1u;
        size_t get_alts = ask_alts * guard_combinations(x, trail);
        for (size_t a = 0; a < ask_alts; a++) {
            rule_name(x, ".ask", r, a, ask_alts);
            tb_puts(b, " (read ");
            if (!render_stage(x, r - 1u, &carried)) goto fail;
            tb_puts(b, ")");
            if (r == 1u && !render_guards(x, 0, a)) goto fail;
            tb_puts(b, " (make (demand ");
            if (!render_call(b, p->mode, p->goal, x->error, x->error_size)) goto fail;
            tb_puts(b, ")))\n");
            x->rules_written++;
        }
        for (size_t i = 0; i < p->mode->arity; i++)
            if (p->mode->success_ground[i] && !vars_of(&known, p->goal->expr.elems[i + 1u])) goto nomem;
        next.len = 0;
        if (!last && !stage_vars(x, r, &known, &next)) goto nomem;
        for (size_t a = 0; a < get_alts; a++) {
            rule_name(x, "", r, a, get_alts);
            tb_puts(b, " (read ");
            if (!render_stage(x, r - 1u, &carried)) goto fail;
            tb_puts(b, " ");
            if (!render_value(b, p->mode, p->goal, x->error, x->error_size)) goto fail;
            tb_puts(b, ")");
            if (r == 1u && !render_guards(x, 0, a % ask_alts)) goto fail;
            if (!render_guards(x, trail, a / ask_alts)) goto fail;
            tb_puts(b, " (make ");
            if (last) {
                if (!render_value(b, x->mode, head, x->error, x->error_size)) goto fail;
            } else if (!render_stage(x, r, &next)) {
                goto fail;
            }
            tb_puts(b, "))\n");
            x->rules_written++;
        }
        VarsV1 swap = carried;
        carried = next;
        next = swap;
        k = trail;
        while (k < x->premises && !x->premise[k].mode) k++;
    }
    if (x->relations == 0u) {
        size_t alts = guard_combinations(x, 0);
        for (size_t a = 0; a < alts; a++) {
            rule_name(x, "", 0, a, alts);
            tb_puts(b, " (read ");
            if (!render_stage(x, 0, &carried)) goto fail;
            tb_puts(b, ")");
            if (!render_guards(x, 0, a)) goto fail;
            tb_puts(b, " (make ");
            if (!render_value(b, x->mode, head, x->error, x->error_size)) goto fail;
            tb_puts(b, "))\n");
            x->rules_written++;
        }
    }
    free(known.name); free(carried.name); free(next.name);
    return !b->nomem || tcalc_error(x->error, x->error_size, "tcalc-direct: out of memory");
nomem:
    tcalc_error(x->error, x->error_size, "tcalc-direct: out of memory");
fail:
    free(known.name); free(carried.name); free(next.name);
    return false;
}

/* The premises of x->rule read in x->mode: each relation premise's mode, the
 * one its arguments' groundness selects (which the analysis must have
 * selected), and each guard premise's realization, its arguments bound; then
 * the head's outputs, which must be bound. */
static bool read_premises(ReadingV1 *x, const DirectBindingModesV1 *modes,
                          const DirectOperatorsV1 *heads, const DirectOperatorsV1 *capabilities) {
    const Atom *head = x->rule->head, *body = x->rule->body;
    const char *name_of_rule = x->rule->source->name;
    char mode[256];   /* the mode, R:BITS, as messages name it */
    int at = snprintf(mode, sizeof mode, "%s:", x->mode->relation);
    for (size_t a = 0; at > 0 && (size_t)at + 1 < sizeof mode && a < x->mode->arity; a++)
        mode[at++] = x->mode->input_ground[a] ? '1' : '0';
    if (at > 0 && (size_t)at < sizeof mode) mode[at] = 0;
    VarsV1 known = {0};
    bool ok = true;
    for (size_t a = 0; ok && a < x->mode->arity; a++)
        if (x->mode->input_ground[a]) ok = vars_of(&known, head->expr.elems[a + 1u]);
    if (!ok) tcalc_error(x->error, x->error_size, "tcalc-direct: out of memory");
    x->premises = 0;
    x->relations = 0;
    for (CettaExprIndex g = 1; ok && g < body->expr.len; g++) {
        const Atom *goal = body->expr.elems[g];
        const char *name = atom_name_cstr(goal->expr.elems[0]);
        size_t arity = (size_t)goal->expr.len - 1u;
        PremiseV1 *p = &x->premise[x->premises++];
        *p = (PremiseV1){.goal = goal};
        if (has_operator(heads, name, arity) || !has_operator(capabilities, name, arity)) {
            TBufV1 bits = {0};
            for (size_t a = 0; a < arity; a++)
                tb_put(&bits, term_ground(&known, goal->expr.elems[a + 1u]) ? "1" : "0", 1u);
            if (bits.nomem) { free(bits.bytes); ok = tcalc_error(x->error, x->error_size, "tcalc-direct: out of memory"); break; }
            uint8_t *input = calloc(arity ? arity : 1u, 1u);
            if (!input) { free(bits.bytes); ok = tcalc_error(x->error, x->error_size, "tcalc-direct: out of memory"); break; }
            for (size_t a = 0; a < arity; a++) input[a] = bits.bytes[a] == '1';
            p->mode = selected_mode(modes, name, arity, input);
            free(input);
            if (!p->mode)
                ok = tcalc_error(x->error, x->error_size,
                                 "tcalc-direct: rule %s in mode %s calls %s:%s, a mode the analysis did not select",
                                 name_of_rule, mode, name, bits.bytes ? (const char *)bits.bytes : "");
            free(bits.bytes);
            x->relations++;
            for (size_t a = 0; ok && a < arity; a++)
                if (p->mode->success_ground[a] && !vars_of(&known, goal->expr.elems[a + 1u]))
                    ok = tcalc_error(x->error, x->error_size, "tcalc-direct: out of memory");
        } else {
            p->ground = ground_relation(name, arity);
            if (!p->ground)
                ok = tcalc_error(x->error, x->error_size,
                                 "tcalc-direct: rule %s in mode %s asks the ground relation %s/%zu, which tcalc has no realization of",
                                 name_of_rule, mode, name, arity);
            for (size_t a = 0; ok && a < arity; a++)
                if (!term_ground(&known, goal->expr.elems[a + 1u]))
                    ok = tcalc_error(x->error, x->error_size,
                                     "tcalc-direct: rule %s in mode %s asks the ground relation %s of an unbound argument",
                                     name_of_rule, mode, name);
        }
    }
    for (size_t a = 0; ok && a < x->mode->arity; a++)
        if (!x->mode->input_ground[a] && !term_ground(&known, head->expr.elems[a + 1u]))
            ok = tcalc_error(x->error, x->error_size,
                             "tcalc-direct: rule %s in mode %s leaves its output %zu unbound",
                             name_of_rule, mode, a + 1u);
    free(known.name);
    return ok;
}

/* Whether the generated row heads leave the signature's operators alone. */
static bool heads_apart(const DirectOperatorsV1 *operators, const char *name, char *error,
                        size_t error_size) {
    for (size_t i = 0; i < operators->len; i++)
        if (strcmp(operators->items[i].name, name) == 0)
            return tcalc_error(error, error_size,
                               "tcalc-direct: the row head %s of the reading is an operator of the signature",
                               name);
    return true;
}

bool cetta_gslt_render_tcalc_v1(
    const DirectRulesV1 *rules, const DirectBindingModesV1 *modes,
    const DirectOperatorsV1 *operators,
    const char *const *entry_modes, size_t entry_mode_count,
    const char *source_digest,
    uint8_t **out, size_t *out_len, size_t *rule_count_out,
    char *error, size_t error_size) {
    DirectOperatorsV1 heads = {0}, capabilities = {0};
    TBufV1 b = {0};
    PremiseV1 *premise = NULL;
    size_t premise_cap = 0, written = 0;
    bool ok = false;
    *out = NULL;
    *out_len = 0;
    *rule_count_out = 0;
    for (size_t i = 0; i < rules->len; i++) {
        const Atom *h = rules->items[i].head;
        const char *name = atom_name_cstr(h->expr.elems[0]);
        if (!push_operator(&heads, name, (size_t)h->expr.len - 1u)) goto nomem;
        if (strcmp(name, "oslf-external-relation-decl-v1") == 0 && h->expr.len == 3u &&
            h->expr.elems[1]->kind == ATOM_SYMBOL && h->expr.elems[2]->kind == ATOM_GROUNDED &&
            h->expr.elems[2]->ground.gkind == GV_INT && h->expr.elems[2]->ground.ival > 0 &&
            !push_operator(&capabilities, atom_name_cstr(h->expr.elems[1]),
                           (size_t)h->expr.elems[2]->ground.ival))
            goto nomem;
    }
    if (!heads_apart(operators, "demand", error, error_size) ||
        !heads_apart(operators, "value", error, error_size))
        goto done;

    tb_puts(&b, "; generated by direct compositional GSLT-to-tcalc lowering, read by demand\n");
    tb_printf(&b, "; source-composition-sha256 %s\n; closed-entry-residual yes\n", source_digest);
    tb_puts(&b, "; rows: (demand (R:BITS IN ...)), (value (R:BITS IN ...) OUT ...) and the stages\n");
    tb_puts(&b, "; (L:BITS/i (R:BITS IN ...) VAR ...) of rule L read in mode R:BITS\n");
    for (size_t e = 0; e < entry_mode_count; e++) tb_printf(&b, "; entry %s\n", entry_modes[e]);
    tb_puts(&b, "; binding modes (1 = ground at call/success), the analysis's\n");
    for (size_t i = 0; i < modes->len; i++) {
        const DirectBindingModeV1 *m = &modes->items[i];
        if (!m->selected) continue;
        tb_printf(&b, "; gslt-binding-mode %s/%zu input=", m->relation, m->arity);
        mode_bits(&b, m);
        tb_puts(&b, " success=");
        for (size_t a = 0; a < m->arity; a++) tb_put(&b, m->success_ground[a] ? "1" : "0", 1u);
        tb_puts(&b, "\n");
    }
    tb_puts(&b, "(store set)\n(step every)\n");

    for (size_t i = 0; i < modes->len; i++) {
        const DirectBindingModeV1 *m = &modes->items[i];
        if (!m->selected) continue;
        {   /* the call head R:BITS is no operator of the signature */
            TBufV1 h = {0};
            tb_printf(&h, "%s:", m->relation);
            mode_bits(&h, m);
            bool apart = !h.nomem && heads_apart(operators, (const char *)h.bytes, error, error_size);
            free(h.bytes);
            if (!apart) goto done;
        }
        for (size_t ri = 0; ri < rules->len; ri++) {
            const DirectRuleV1 *rule = &rules->items[ri];
            const Atom *head = rule->head, *body = rule->body;
            if ((size_t)head->expr.len - 1u != m->arity ||
                strcmp(atom_name_cstr(head->expr.elems[0]), m->relation) != 0)
                continue;
            size_t goals = body->expr.len ? (size_t)body->expr.len - 1u : 0u;
            if (goals > premise_cap) {
                PremiseV1 *grown = realloc(premise, goals * sizeof *grown);
                if (!grown) goto nomem;
                premise = grown;
                premise_cap = goals;
            }
            /* the label L:BITS, with the rule's composition index when its
             * name is not unique */
            bool unique = true;
            for (size_t o = 0; o < rules->len; o++)
                if (o != ri && rules->items[o].source && rule->source &&
                    strcmp(rules->items[o].source->name, rule->source->name) == 0)
                    unique = false;
            TBufV1 label = {0};
            if (unique) tb_printf(&label, "%s:", rule->source->name);
            else tb_printf(&label, "%s#%zu:", rule->source->name, rule->source_index);
            mode_bits(&label, m);
            if (label.nomem) { free(label.bytes); goto nomem; }
            if (!tcalc_word((const char *)label.bytes) ||
                !heads_apart(operators, (const char *)label.bytes, error, error_size)) {
                if (error && error[0] == 0)
                    tcalc_error(error, error_size, "tcalc-direct: the rule name %s has no tcalc spelling",
                                (const char *)label.bytes);
                free(label.bytes);
                goto done;
            }
            ReadingV1 x = {.out = &b, .mode = m, .rule = rule, .label = (const char *)label.bytes,
                           .premise = premise, .error = error, .error_size = error_size};
            bool read_ok = read_premises(&x, modes, &heads, &capabilities);
            if (read_ok) {
                tb_printf(&b, "; %s\n", rule->source->name);
                read_ok = read_rule(&x);
            }
            written += x.rules_written;
            free(label.bytes);
            if (!read_ok) goto done;
        }
    }
    if (b.nomem) goto nomem;
    *out = b.bytes;
    *out_len = b.len;
    *rule_count_out = written;
    b.bytes = NULL;
    ok = true;
    goto done;
nomem:
    tcalc_error(error, error_size, "tcalc-direct: out of memory");
done:
    free(b.bytes);
    free(premise);
    free(heads.items);
    free(capabilities.items);
    return ok;
}

#include "petta_contract_core.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "eval.h"
#include "petta_semantics.h"
#include "petta_specializer.h"
#include "petta_type_policy.h"
#include "space_type_evidence.h"
#include "symbol.h"

/* ── Uninterpreted symbols and spaces ────────────────────────────────────── */

bool petta_contract_symbol_uninterpreted(Space *space, Arena *scratch,
                                         SymbolId symbol) {
    if (!space || !scratch || symbol == SYMBOL_ID_NONE ||
        eval_current_language_id() != CETTA_LANGUAGE_PETTA)
        return false;
    Atom *subject = atom_symbol_id(scratch, symbol);
    if (!subject || space_type_annotation_may_match_subject(space, subject) ||
        space_equations_may_match_known_head(space, symbol))
        return false;
    /* The pairing constructors are data wherever they head an expression. */
    if (symbol == g_builtin_syms.colon || symbol == g_builtin_syms.arrow)
        return true;
    if (cetta_petta_head_names_extension(symbol))
        return false;
    for (CettaExprLen nargs = 0u; nargs <= PETTA_CONTRACT_MAX_ARITY; nargs++) {
        if (cetta_petta_source_head_has_runtime_meaning(space, symbol, nargs))
            return false;
    }
    return true;
}

/* Symbols already judged during one scan: an open-addressing set of the
 * uninterpreted ones and of the interpreted ones. */
static uint32_t contract_symbol_mix(SymbolId symbol) {
    uint32_t x = (uint32_t)symbol;
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

typedef struct {
    SymbolId *keys;
    uint8_t *uninterpreted;
    uint32_t cap, len;
} ContractSymbolMemo;

static void contract_memo_free(ContractSymbolMemo *memo) {
    free(memo->keys);
    free(memo->uninterpreted);
    *memo = (ContractSymbolMemo){0};
}

static bool contract_memo_symbol(ContractSymbolMemo *memo, Space *space,
                                 Arena *scratch, SymbolId symbol,
                                 bool *uninterpreted) {
    if ((uint64_t)(memo->len + 1u) * 2u > memo->cap) {
        uint32_t cap = memo->cap ? memo->cap * 2u : 64u;
        SymbolId *keys = calloc(cap, sizeof(*keys));
        uint8_t *values = calloc(cap, sizeof(*values));
        if (!keys || !values) {
            free(keys);
            free(values);
            return false;
        }
        for (uint32_t i = 0u; i < memo->cap; i++) {
            if (memo->keys[i] == SYMBOL_ID_NONE)
                continue;
            uint32_t slot = contract_symbol_mix(memo->keys[i]) & (cap - 1u);
            while (keys[slot] != SYMBOL_ID_NONE)
                slot = (slot + 1u) & (cap - 1u);
            keys[slot] = memo->keys[i];
            values[slot] = memo->uninterpreted[i];
        }
        free(memo->keys);
        free(memo->uninterpreted);
        memo->keys = keys;
        memo->uninterpreted = values;
        memo->cap = cap;
    }
    uint32_t slot = contract_symbol_mix(symbol) & (memo->cap - 1u);
    while (memo->keys[slot] != SYMBOL_ID_NONE) {
        if (memo->keys[slot] == symbol) {
            *uninterpreted = memo->uninterpreted[slot] != 0u;
            return true;
        }
        slot = (slot + 1u) & (memo->cap - 1u);
    }
    *uninterpreted = petta_contract_symbol_uninterpreted(space, scratch, symbol);
    memo->keys[slot] = symbol;
    memo->uninterpreted[slot] = *uninterpreted ? 1u : 0u;
    memo->len++;
    return true;
}

/* Variables carry no symbol; every symbol must be uninterpreted. */
static bool contract_atom_uninterpreted(ContractSymbolMemo *memo, Space *space,
                                        Arena *scratch, const Atom *atom,
                                        uint32_t depth) {
    if (!atom || depth > 4096u)
        return false;
    switch (atom->kind) {
    case ATOM_VAR:
    case ATOM_GROUNDED:
        return true;
    case ATOM_SYMBOL: {
        bool uninterpreted = false;
        return contract_memo_symbol(memo, space, scratch, atom_symbol_current_id(atom),
                                    &uninterpreted) && uninterpreted;
    }
    case ATOM_EXPR:
        for (CettaExprIndex i = 0u; i < atom->expr.len; i++) {
            if (!contract_atom_uninterpreted(memo, space, scratch,
                                             atom->expr.elems[i], depth + 1u))
                return false;
        }
        return true;
    default:
        return false;
    }
}

bool petta_contract_space_uninterpreted(Space *program_space, Space *data) {
    if (!program_space || !data || space_has_dependencies(data))
        return false;
    Arena scratch;
    arena_init(&scratch);
    ContractSymbolMemo memo = {0};
    bool ok = true;
    CettaCount length = space_length64(data);
    for (CettaIndex index = 0u; ok && index < length; index++) {
        Atom *atom = space_get_at64(data, index);
        ok = !atom || contract_atom_uninterpreted(&memo, program_space,
                                                  &scratch, atom, 0u);
    }
    contract_memo_free(&memo);
    arena_free(&scratch);
    return ok;
}

/* ── Callees that receive their arguments as written ─────────────────────── */

static bool contract_arrow_source_arity(void *context, SymbolId subject,
                                        uint32_t declarations, Atom *arrow) {
    (void)subject;
    uint32_t *arity = context;
    CettaExprLen places = arrow->expr.len - 2u;
    /* Several declarations of one name are several typed alternatives;
     * counting duplicate rows too only keeps more calls on the general
     * path. */
    bool source = declarations > 1u;
    for (CettaExprIndex index = 1u; !source && index <= places; index++)
        source = petta_type_demand(arrow->expr.elems[index]) == PETTA_TYPE_RAW;
    if (source && places > *arity)
        *arity = places > UINT32_MAX - 1u ? UINT32_MAX - 1u : (uint32_t)places;
    return true;
}

uint32_t petta_contract_callee_source_arity(Space *space) {
    uint32_t arity = 0u;
    return space_type_arrow_declarations_visit(
               space, contract_arrow_source_arity, &arity)
        ? arity : UINT32_MAX;
}

/* ── Runtime verdicts ────────────────────────────────────────────────────── */

typedef struct {
    Space *space;
    uint64_t instance_id;
    uint64_t revision;
    bool uninterpreted;
} ContractSpaceVerdict;

struct PettaContractMemo {
    SpaceProgramToken token;
    bool token_valid;
    Arena scratch;
    ContractSymbolMemo symbols;
    ContractSpaceVerdict spaces[4];
    uint32_t space_count;
};

PettaContractMemo *petta_contract_memo_new(void) {
    PettaContractMemo *memo = calloc(1u, sizeof(*memo));
    if (memo)
        arena_init(&memo->scratch);
    return memo;
}

void petta_contract_memo_free(PettaContractMemo *memo) {
    if (!memo)
        return;
    contract_memo_free(&memo->symbols);
    arena_free(&memo->scratch);
    free(memo);
}

/* Verdicts belong to the program projection they were judged under. */
static void contract_memo_refresh(PettaContractMemo *memo, Space *space) {
    SpaceProgramToken token = space_program_token(space);
    if (memo->token_valid && space_program_token_eq(memo->token, token))
        return;
    contract_memo_free(&memo->symbols);
    memo->space_count = 0u;
    memo->token = token;
    memo->token_valid = true;
}

bool petta_contract_memo_symbol(PettaContractMemo *memo, Space *program_space,
                                SymbolId symbol) {
    if (!memo || !program_space)
        return false;
    contract_memo_refresh(memo, program_space);
    bool uninterpreted = false;
    return contract_memo_symbol(&memo->symbols, program_space, &memo->scratch,
                                symbol, &uninterpreted) && uninterpreted;
}

bool petta_contract_memo_space(PettaContractMemo *memo, Space *program_space,
                               Space *data) {
    if (!memo || !program_space || !data)
        return false;
    contract_memo_refresh(memo, program_space);
    uint64_t instance = space_instance_id(data);
    uint64_t revision = space_revision(data);
    for (uint32_t i = 0u; i < memo->space_count; i++) {
        ContractSpaceVerdict *verdict = &memo->spaces[i];
        if (verdict->space == data && verdict->instance_id == instance &&
            verdict->revision == revision)
            return verdict->uninterpreted;
    }
    bool uninterpreted = petta_contract_space_uninterpreted(program_space, data);
    uint32_t slot = memo->space_count < 4u ? memo->space_count++ : revision % 4u;
    memo->spaces[slot] = (ContractSpaceVerdict){
        .space = data, .instance_id = instance, .revision = revision,
        .uninterpreted = uninterpreted,
    };
    return uninterpreted;
}


/* ── The analysis ────────────────────────────────────────────────────────── */

typedef struct {
    VarId var;
    bool ground;
    bool uninterpreted;
    bool space;
    Atom *type;
    /* A constant parameter's variable: the function name it holds. */
    SymbolId constant;
} ContractFact;

/* What every answer of an occurrence is.  `bottom` has no answer: `(empty)`,
 * or a branch that never returns. */
typedef struct {
    bool ground;
    bool uninterpreted;
    bool bottom;
    Atom *type;
} ContractValue;

static ContractValue contract_join(ContractValue a, ContractValue b) {
    if (a.bottom)
        return b;
    if (b.bottom)
        return a;
    return (ContractValue){
        .ground = a.ground && b.ground,
        .uninterpreted = a.uninterpreted && b.uninterpreted,
        .type = a.type && b.type && atom_eq(a.type, b.type) ? a.type : NULL,
    };
}

/* CETTA_PETTA_VERSIONS_DEBUG=1 names each function the analysis reads,
 * its verdict, and the first occurrence a version or purity declines. */
static bool contract_debug(void) {
    static _Atomic int enabled = -1;
    int value = atomic_load_explicit(&enabled, memory_order_relaxed);
    if (value < 0) {
        const char *setting = getenv("CETTA_PETTA_VERSIONS_DEBUG");
        value = setting && setting[0] && strcmp(setting, "0") != 0;
        atomic_store_explicit(&enabled, value, memory_order_relaxed);
    }
    return value != 0;
}

/* The analysis of one function may ask about the functions it calls.  Their
 * verdicts are kept for the whole question, and a function already under
 * analysis is not analysed again: a call back into it keeps its protocol
 * only if the function is otherwise known to be pure, which it is not yet,
 * so the outer analysis declines. */
typedef struct {
    SymbolId head;
    uint32_t arity;
    uint8_t state; /* 0 analysing, 1 has a version, 2 pure without one,
                      3 neither */
    PettaContractVersion version;
    PettaTypeCall signature;
} ContractCallee;

typedef struct {
    PettaProgram *program;
    Space *space;
    Arena *scratch;
    ContractSymbolMemo memo;
    ContractCallee *callees;
    uint32_t callee_len, callee_cap;
} ContractContext;

typedef struct {
    ContractContext *context;
    SymbolId head;
    uint32_t arity;
    PettaTypeCall signature;
    PettaContractVersion *version;
    /* Each parameter's formal and the result's, with the type variables the
     * constant parameters fix replaced by the types they fix. */
    Atom *formals[PETTA_CONTRACT_MAX_ARITY];
    Atom *result_formal;
    /* The parameter whose type variable is the result type, or arity. */
    uint32_t result_param;
    /* The result type is closed: each equation proves its answers are
     * ground values of it. */
    bool closed_result;
    /* Only asking whether the equations do work a version may do. */
    bool purity_only;
    /* The first occurrence declined, for CETTA_PETTA_VERSIONS_DEBUG. */
    Atom *declined;
    ContractFact *facts;
    uint32_t fact_len, fact_cap;
} ContractScan;

static ContractFact *contract_fact(ContractScan *scan, VarId var) {
    for (uint32_t i = 0u; i < scan->fact_len; i++) {
        if (var_id_eq(scan->facts[i].var, var))
            return &scan->facts[i];
    }
    return NULL;
}

static ContractFact *contract_fact_add(ContractScan *scan, VarId var) {
    ContractFact *fact = contract_fact(scan, var);
    if (fact)
        return fact;
    if (scan->fact_len == scan->fact_cap) {
        uint32_t cap = scan->fact_cap ? scan->fact_cap * 2u : 16u;
        ContractFact *facts = realloc(scan->facts, sizeof(*facts) * cap);
        if (!facts)
            return NULL;
        scan->facts = facts;
        scan->fact_cap = cap;
    }
    fact = &scan->facts[scan->fact_len++];
    *fact = (ContractFact){.var = var, .constant = SYMBOL_ID_NONE};
    return fact;
}

static bool contract_symbol(ContractScan *scan, SymbolId symbol) {
    bool uninterpreted = false;
    return contract_memo_symbol(&scan->context->memo, scan->context->space,
                                scan->context->scratch, symbol,
                                &uninterpreted) && uninterpreted;
}

/* The one declared type of `symbol`, when it has exactly one. */
static Atom *contract_declared(ContractScan *scan, Atom *symbol) {
    Atom **declared = NULL;
    uint32_t count = space_get_declared_types(scan->context->space,
                                              scan->context->scratch,
                                              symbol, &declared);
    Atom *type = count == 1u ? declared[0] : NULL;
    free(declared);
    return type;
}

static uint32_t contract_var_occurrences(const Atom *atom, VarId var) {
    if (!atom)
        return 0u;
    if (atom->kind == ATOM_VAR)
        return var_id_eq(atom->var_id, var) ? 1u : 0u;
    if (atom->kind != ATOM_EXPR)
        return 0u;
    uint32_t count = 0u;
    for (CettaExprIndex i = 0u; i < atom->expr.len; i++)
        count += contract_var_occurrences(atom->expr.elems[i], var);
    return count;
}

/* The one declaration (-> B1 .. Bk C) of a constructor `head` at arity k,
 * with C closed and each field closed or a type variable the declaration
 * names nowhere else.  A field at such a variable may be any value: its
 * guard against a fresh variable succeeds exactly once whatever the value
 * later becomes.  A variable named twice couples its fields, so the
 * declaration gives no field a type. */
static Atom *contract_constructor_arrow(ContractScan *scan, Atom *head,
                                        CettaExprLen arity) {
    Atom *type = contract_declared(scan, head);
    if (!type || type->kind != ATOM_EXPR || type->expr.len != arity + 2u ||
        !atom_is_symbol_id(type->expr.elems[0], g_builtin_syms.arrow) ||
        atom_has_vars(type->expr.elems[arity + 1u]))
        return NULL;
    for (CettaExprIndex i = 1u; i <= arity; i++) {
        Atom *field = type->expr.elems[i];
        if (field->kind == ATOM_VAR) {
            if (contract_var_occurrences(type, field->var_id) != 1u)
                return NULL;
        } else if (atom_has_vars(field)) {
            return NULL;
        }
    }
    return type;
}

/* Variables of `pattern` take `fact`'s guarantees, merged with any they
 * already have from another occurrence. */
static bool contract_mark(ContractScan *scan, Atom *pattern, bool ground,
                          bool uninterpreted, uint32_t depth) {
    if (!pattern || depth > 256u)
        return false;
    if (pattern->kind == ATOM_VAR) {
        ContractFact *fact = contract_fact_add(scan, pattern->var_id);
        if (!fact)
            return false;
        fact->ground = fact->ground || ground;
        fact->uninterpreted = fact->uninterpreted || uninterpreted;
        return true;
    }
    if (pattern->kind != ATOM_EXPR)
        return true;
    for (CettaExprIndex i = 0u; i < pattern->expr.len; i++) {
        if (!contract_mark(scan, pattern->expr.elems[i], ground, uninterpreted,
                           depth + 1u))
            return false;
    }
    return true;
}

/* A ground argument accepted at closed type `type` and matched by `pattern`:
 * a variable has that type; an application of a constructor whose one
 * declaration (contract_constructor_arrow) ends in `type` gives its closed
 * fields their types, since a symbol requirement admits no tuple type, so the
 * argument was accepted through that declaration with its fields checked. */
static bool contract_invert(ContractScan *scan, Atom *pattern, Atom *type,
                            uint32_t depth) {
    if (!pattern || !type || depth > 256u)
        return false;
    if (pattern->kind == ATOM_VAR) {
        ContractFact *fact = contract_fact_add(scan, pattern->var_id);
        if (!fact)
            return false;
        if (!fact->type)
            fact->type = type;
        return true;
    }
    if (pattern->kind != ATOM_EXPR || pattern->expr.len < 2u ||
        type->kind != ATOM_SYMBOL ||
        pattern->expr.elems[0]->kind != ATOM_SYMBOL)
        return true;
    Atom *arrow = contract_constructor_arrow(scan, pattern->expr.elems[0],
                                             pattern->expr.len - 1u);
    if (!arrow || !atom_eq(arrow->expr.elems[arrow->expr.len - 1u], type))
        return true;
    for (CettaExprIndex i = 1u; i < pattern->expr.len; i++) {
        if (arrow->expr.elems[i]->kind != ATOM_VAR &&
            !contract_invert(scan, pattern->expr.elems[i],
                             arrow->expr.elems[i], depth + 1u))
            return false;
    }
    return true;
}

/* Every symbol of `pattern` is uninterpreted and so is every variable a
 * fact describes: unification with an uninterpreted value then binds only
 * uninterpreted terms on either side. */
static bool contract_pattern_uninterpreted(ContractScan *scan, Atom *pattern,
                                           uint32_t depth) {
    if (!pattern || depth > 256u)
        return false;
    switch (pattern->kind) {
    case ATOM_VAR: {
        ContractFact *fact = contract_fact(scan, pattern->var_id);
        return !fact || fact->uninterpreted;
    }
    case ATOM_GROUNDED:
        return true;
    case ATOM_SYMBOL:
        return contract_symbol(scan, atom_symbol_current_id(pattern));
    case ATOM_EXPR:
        for (CettaExprIndex i = 0u; i < pattern->expr.len; i++) {
            if (!contract_pattern_uninterpreted(scan, pattern->expr.elems[i],
                                                depth + 1u))
                return false;
        }
        return true;
    default:
        return false;
    }
}

/* Unify `pattern`, an uninterpreted pattern, with a value: its new
 * variables are uninterpreted, ground when the value is. */
static bool contract_bind(ContractScan *scan, Atom *pattern,
                          ContractValue value) {
    if (!value.uninterpreted ||
        !contract_pattern_uninterpreted(scan, pattern, 0u))
        return false;
    return contract_mark(scan, pattern, value.ground, true, 0u);
}

/* Whether no variable of `pattern` claims to be uninterpreted while open:
 * unifying it with any value keeps every claim a fact makes. */
static bool contract_pattern_keeps_claims(ContractScan *scan, Atom *pattern,
                                          uint32_t depth) {
    if (!pattern || depth > 256u)
        return false;
    if (pattern->kind == ATOM_VAR) {
        ContractFact *fact = contract_fact(scan, pattern->var_id);
        return !fact || fact->ground || !fact->uninterpreted;
    }
    if (pattern->kind != ATOM_EXPR)
        return true;
    for (CettaExprIndex i = 0u; i < pattern->expr.len; i++) {
        if (!contract_pattern_keeps_claims(scan, pattern->expr.elems[i],
                                           depth + 1u))
            return false;
    }
    return true;
}

/* Unify `pattern` with any value: an uninterpreted value as contract_bind
 * does; another, when no existing claim can break, leaves the pattern's
 * variables only ground when the value is. */
static bool contract_bind_any(ContractScan *scan, Atom *pattern,
                              ContractValue value) {
    if (value.uninterpreted && contract_bind(scan, pattern, value))
        return true;
    return contract_pattern_keeps_claims(scan, pattern, 0u) &&
        contract_mark(scan, pattern, value.ground && !value.bottom, false, 0u);
}

static bool contract_eval(ContractScan *scan, Atom *expr,
                          const PettaPlanNode *plan, uint32_t depth,
                          ContractValue *out);

static bool contract_literal_type(ContractScan *scan, Atom *atom,
                                  Atom **type) {
    *type = NULL;
    if (atom->kind != ATOM_GROUNDED)
        return true;
    switch (atom->ground.gkind) {
    case GV_INT:
    case GV_BIGINT:
    case GV_RATIONAL:
    case GV_FLOAT:
        *type = atom_symbol(scan->context->scratch, "Number");
        return *type != NULL;
    case GV_STRING:
        *type = atom_symbol(scan->context->scratch, "String");
        return *type != NULL;
    case GV_BOOL:
        *type = atom_symbol(scan->context->scratch, "Bool");
        return *type != NULL;
    default:
        return true;
    }
}

/* Snapshots of the facts around a branch: what one branch binds is not
 * known in the other, nor after both. */
typedef struct {
    ContractFact *facts;
    uint32_t len;
} ContractFactSave;

static bool contract_facts_save(ContractScan *scan, ContractFactSave *save) {
    save->len = scan->fact_len;
    save->facts = NULL;
    if (scan->fact_len == 0u)
        return true;
    save->facts = malloc(sizeof(*save->facts) * scan->fact_len);
    if (!save->facts)
        return false;
    memcpy(save->facts, scan->facts, sizeof(*save->facts) * scan->fact_len);
    return true;
}

static void contract_facts_restore(ContractScan *scan,
                                   const ContractFactSave *save) {
    if (save->len > 0u)
        memcpy(scan->facts, save->facts, sizeof(*save->facts) * save->len);
    scan->fact_len = save->len;
}

/* A self-call meets the entry facts with its arguments' values; its value
 * is an instance of its polymorphic argument, whose bindings come from
 * uninterpreted sources, or a ground value of the closed result type (the
 * induction the analysis carries). */
static bool contract_self_call(ContractScan *scan, Atom *expr,
                               const PettaPlanNode *plan, uint32_t depth,
                               ContractValue *out) {
    ContractValue polymorphic = {0};
    for (uint32_t i = 0u; i < scan->arity; i++) {
        Atom *argument = expr->expr.elems[i + 1u];
        ContractValue value = {0};
        if (!contract_eval(scan, argument, petta_plan_child(plan, i + 1u),
                           depth + 1u, &value))
            return false;
        if (value.bottom) {
            *out = (ContractValue){.bottom = true};
            return true;
        }
        switch (scan->version->facts[i]) {
        case PETTA_CONTRACT_ENTRY_GROUND:
            if (!value.ground || !value.type ||
                !atom_eq(value.type, scan->formals[i]))
                return false;
            break;
        case PETTA_CONTRACT_ENTRY_UNINTERPRETED:
            if (!value.uninterpreted)
                return false;
            break;
        case PETTA_CONTRACT_ENTRY_UNINTERPRETED_SPACE: {
            ContractFact *fact = argument->kind == ATOM_VAR
                ? contract_fact(scan, argument->var_id) : NULL;
            if (!fact || !fact->space)
                return false;
            break;
        }
        case PETTA_CONTRACT_ENTRY_CONSTANT: {
            SymbolId constant = scan->version->constants[i];
            ContractFact *fact = argument->kind == ATOM_VAR
                ? contract_fact(scan, argument->var_id) : NULL;
            if (!atom_is_symbol_id(argument, constant) &&
                !(fact && fact->constant == constant))
                return false;
            break;
        }
        case PETTA_CONTRACT_ENTRY_ANY:
            break;
        }
        if (i == scan->result_param)
            polymorphic = value;
    }
    if (scan->closed_result) {
        *out = (ContractValue){.ground = true, .type = scan->result_formal};
        return true;
    }
    *out = (ContractValue){
        .uninterpreted = scan->result_param < scan->arity &&
                         polymorphic.uninterpreted,
    };
    return true;
}

static bool contract_note_space(ContractScan *scan, SymbolId space) {
    PettaContractVersion *version = scan->version;
    for (uint32_t i = 0u; i < version->space_count; i++) {
        if (version->spaces[i] == space)
            return true;
    }
    if (version->space_count == PETTA_CONTRACT_MAX_SPACES)
        return false;
    version->spaces[version->space_count++] = space;
    return true;
}

/* `(match space pattern template)` over a space whose every row is
 * uninterpreted at entry: the space parameter, or a space named in the
 * equations, which the entry then checks. */
static bool contract_match(ContractScan *scan, Atom *expr,
                           const PettaPlanNode *plan, uint32_t depth,
                           ContractValue *out) {
    Atom *source = expr->expr.elems[1];
    Atom *pattern = expr->expr.elems[2];
    ContractFact *fact = source->kind == ATOM_VAR
        ? contract_fact(scan, source->var_id) : NULL;
    bool named = source->kind == ATOM_SYMBOL &&
        petta_semantics_form(atom_symbol_current_id(source)) == PETTA_FORM_NONE &&
        atom_symbol_current_id(source) != g_builtin_syms.self;
    if ((!fact || !fact->space) && !named)
        return false;
    if (petta_semantics_contains_cons_constraint(pattern) ||
        (pattern->kind == ATOM_EXPR && pattern->expr.len > 0u &&
         (atom_is_symbol_id(pattern->expr.elems[0], g_builtin_syms.comma) ||
          atom_is_symbol_id(pattern->expr.elems[0], g_builtin_syms.pipe))))
        return false;
    if (named && !scan->purity_only &&
        !contract_note_space(scan, atom_symbol_current_id(source)))
        return false;
    /* Rows of an uninterpreted space may hold variables: not ground. */
    if (!contract_bind(scan, pattern,
                       (ContractValue){.uninterpreted = true}))
        return false;
    return contract_eval(scan, expr->expr.elems[3], petta_plan_child(plan, 3u),
                         depth + 1u, out);
}

static bool contract_bindings(ContractScan *scan, Atom *expr,
                              const PettaPlanNode *plan, uint32_t depth,
                              ContractValue *out) {
    if (cetta_control_operation_binding_layout(plan->computation.control, 1u)) {
        ContractValue value = {0};
        if (!contract_eval(scan, expr->expr.elems[2],
                           petta_plan_child(plan, 2u), depth + 1u, &value))
            return false;
        if (value.bottom) {
            *out = value;
            return true;
        }
        return contract_bind_any(scan, expr->expr.elems[1], value) &&
            contract_eval(scan, expr->expr.elems[3],
                          petta_plan_child(plan, 3u), depth + 1u, out);
    }
    Atom *bindings = expr->expr.elems[1];
    const PettaPlanNode *bindings_plan = petta_plan_child(plan, 1u);
    if (!bindings || bindings->kind != ATOM_EXPR || !bindings_plan)
        return false;
    for (CettaExprIndex index = 0u; index < bindings->expr.len; index++) {
        Atom *pair = bindings->expr.elems[index];
        const PettaPlanNode *pair_plan = petta_plan_child(bindings_plan, index);
        ContractValue value = {0};
        if (!pair || pair->kind != ATOM_EXPR || pair->expr.len != 2u ||
            !pair_plan ||
            !contract_eval(scan, pair->expr.elems[1],
                           petta_plan_child(pair_plan, 1u), depth + 1u,
                           &value))
            return false;
        if (value.bottom) {
            *out = value;
            return true;
        }
        if (!contract_bind_any(scan, pair->expr.elems[0], value))
            return false;
    }
    return contract_eval(scan, expr->expr.elems[2], petta_plan_child(plan, 2u),
                         depth + 1u, out);
}

/* `(if c t)` and `(if c t e)`: the value of a branch; with no else the
 * false condition has no answer.  Each branch is read from the facts known
 * before it. */
static bool contract_if(ContractScan *scan, Atom *expr,
                        const PettaPlanNode *plan, uint32_t depth,
                        ContractValue *out) {
    ContractValue condition = {0};
    if (!contract_eval(scan, expr->expr.elems[1], petta_plan_child(plan, 1u),
                       depth + 1u, &condition))
        return false;
    ContractFactSave save = {0};
    if (!contract_facts_save(scan, &save))
        return false;
    ContractValue then_value = {0};
    ContractValue else_value = {.bottom = true};
    bool ok = contract_eval(scan, expr->expr.elems[2],
                            petta_plan_child(plan, 2u), depth + 1u,
                            &then_value);
    contract_facts_restore(scan, &save);
    if (ok && expr->expr.len == 4u) {
        ok = contract_eval(scan, expr->expr.elems[3],
                           petta_plan_child(plan, 3u), depth + 1u,
                           &else_value);
        contract_facts_restore(scan, &save);
    }
    free(save.facts);
    *out = contract_join(then_value, else_value);
    return ok;
}

/* `(case key ((p1 b1) ...))`: each branch binds its pattern to the key's
 * value and gives its body's value. */
static bool contract_case(ContractScan *scan, Atom *expr,
                          const PettaPlanNode *plan, uint32_t depth,
                          ContractValue *out) {
    CettaControlOperationPlan control = plan->computation.control;
    CettaExprLen arity = expr->expr.len - 1u;
    if (!cetta_control_operation_match_branches_layout(control, arity))
        return false;
    CettaExprIndex key_child = (CettaExprIndex)control.source + 1u;
    CettaExprIndex arms_child = (CettaExprIndex)control.first + 1u;
    Atom *branches = expr->expr.elems[arms_child];
    const PettaPlanNode *branches_plan = petta_plan_child(plan, arms_child);
    ContractValue key = {0};
    if (!branches || branches->kind != ATOM_EXPR || !branches_plan ||
        !contract_eval(scan, expr->expr.elems[key_child], petta_plan_child(plan, key_child),
                       depth + 1u, &key))
        return false;
    ContractFactSave save = {0};
    if (!contract_facts_save(scan, &save))
        return false;
    ContractValue value = {.bottom = true};
    bool ok = true;
    for (CettaExprIndex i = 0u; ok && i < branches->expr.len; i++) {
        Atom *branch = branches->expr.elems[i];
        const PettaPlanNode *branch_plan = petta_plan_child(branches_plan, i);
        ContractValue body = {0};
        ok = branch && branch->kind == ATOM_EXPR && branch->expr.len == 2u &&
            branch_plan && !key.bottom &&
            contract_bind_any(scan, branch->expr.elems[0], key) &&
            contract_eval(scan, branch->expr.elems[1],
                          petta_plan_child(branch_plan, 1u), depth + 1u,
                          &body);
        contract_facts_restore(scan, &save);
        value = contract_join(value, body);
    }
    free(save.facts);
    *out = key.bottom ? key : value;
    return ok;
}

/* An operation PeTTa provides that changes nothing and gives a ground value
 * of the type named here, when the program defines no equation of its name:
 * comparisons and connectives answer Bool, arithmetic answers a Number. */
static const char *contract_pure_operation(ContractScan *scan, SymbolId head,
                                           CettaExprLen nargs) {
    if (space_equations_may_match_known_head(scan->context->space, head))
        return NULL;
    if (nargs == 2u &&
        (head == g_builtin_syms.op_eq || head == g_builtin_syms.op_lt ||
         head == g_builtin_syms.op_gt || head == g_builtin_syms.op_le ||
         head == g_builtin_syms.op_ge || head == g_builtin_syms.op_and ||
         head == g_builtin_syms.op_or))
        return "Bool";
    if (nargs == 1u && head == g_builtin_syms.op_not)
        return "Bool";
    if (nargs == 2u &&
        (head == g_builtin_syms.op_plus || head == g_builtin_syms.op_minus ||
         head == g_builtin_syms.op_mul || head == g_builtin_syms.op_div))
        return "Number";
    return NULL;
}

static bool contract_function_version(ContractContext *context,
                                      SymbolId head, uint32_t arity,
                                      ContractCallee **out);

/* Whether a call's argument values meet a callee version's entry facts, and
 * the spaces that version reads are then the caller's to check at entry. */
static bool contract_meets_version(ContractScan *scan, ContractCallee *callee,
                                   Atom *expr, const ContractValue *values) {
    const PettaContractVersion *version = &callee->version;
    for (uint32_t i = 0u; i < version->arity; i++) {
        PettaTypeArgument formal =
            petta_type_call_argument(&callee->signature, i + 1u, false);
        switch (version->facts[i]) {
        case PETTA_CONTRACT_ENTRY_GROUND:
            if (!values[i].ground || !values[i].type ||
                !atom_eq(values[i].type, formal.formal))
                return false;
            break;
        case PETTA_CONTRACT_ENTRY_UNINTERPRETED:
            if (!values[i].uninterpreted)
                return false;
            break;
        case PETTA_CONTRACT_ENTRY_CONSTANT:
            if (!atom_is_symbol_id(expr->expr.elems[i + 1u],
                                   version->constants[i]))
                return false;
            break;
        case PETTA_CONTRACT_ENTRY_UNINTERPRETED_SPACE:
            return false;
        case PETTA_CONTRACT_ENTRY_ANY:
            break;
        }
    }
    for (uint32_t i = 0u; i < version->space_count; i++) {
        if (!contract_note_space(scan, version->spaces[i]))
            return false;
    }
    return true;
}

/* A call of another function of the program.  A typed call whose arguments
 * meet the callee's version enters it: its value is the version's result.
 * Any other call to a function whose equations do only this kind of work
 * keeps its protocol: a typed call's result guard then gives its closed
 * result type. */
static bool contract_call(ContractScan *scan, Atom *expr,
                          const PettaPlanNode *plan, uint32_t depth,
                          ContractValue *out) {
    SymbolId head = atom_symbol_current_id(expr->expr.elems[0]);
    CettaExprLen nargs = expr->expr.len - 1u;
    if (nargs > PETTA_CONTRACT_MAX_ARITY)
        return false;
    ContractValue values[PETTA_CONTRACT_MAX_ARITY];
    for (uint32_t i = 0u; i < nargs; i++) {
        if (!contract_eval(scan, expr->expr.elems[i + 1u],
                           petta_plan_child(plan, i + 1u), depth + 1u,
                           &values[i]))
            return false;
        if (values[i].bottom) {
            *out = values[i];
            return true;
        }
    }
    const char *operation = contract_pure_operation(scan, head, nargs);
    if (operation) {
        *out = (ContractValue){
            .ground = true,
            .type = atom_symbol(scan->context->scratch, operation),
        };
        return out->type != NULL;
    }
    ContractCallee *callee = NULL;
    if (!contract_function_version(scan->context, head, (uint32_t)nargs,
                                   &callee) ||
        !callee || callee->state == 0u || callee->state == 3u) {
        if (!scan->declined)
            scan->declined = expr;
        return false;
    }
    if (callee->state == 1u && !scan->purity_only &&
        contract_meets_version(scan, callee, expr, values)) {
        PettaContractVersion *version = scan->version;
        if (version->discharged_count < PETTA_CONTRACT_MAX_DISCHARGED)
            version->discharged[version->discharged_count++] = expr;
        Atom *result = callee->signature.result_type;
        if (callee->signature.guard_result && result &&
            !atom_has_vars(result)) {
            *out = (ContractValue){.ground = true, .type = result};
            return true;
        }
        /* The polymorphic pair: an instance of its argument. */
        bool uninterpreted = false;
        for (uint32_t i = 0u; i < callee->version.arity; i++) {
            if (callee->version.facts[i] == PETTA_CONTRACT_ENTRY_UNINTERPRETED)
                uninterpreted = values[i].uninterpreted;
        }
        *out = (ContractValue){.uninterpreted = uninterpreted};
        return true;
    }
    Atom *result = callee->signature.result_type;
    *out = (ContractValue){
        .type = callee->signature.guard_result && result &&
                !atom_has_vars(result) ? result : NULL,
    };
    return true;
}

static bool contract_eval(ContractScan *scan, Atom *expr,
                          const PettaPlanNode *plan, uint32_t depth,
                          ContractValue *out) {
    if (!expr || depth > 256u)
        return false;
    switch (expr->kind) {
    case ATOM_VAR: {
        ContractFact *fact = contract_fact(scan, expr->var_id);
        /* A body variable not yet bound holds no symbol now; whatever binds
         * it later passes contract_bind first. */
        *out = fact ? (ContractValue){.ground = fact->ground,
                                      .uninterpreted = fact->uninterpreted,
                                      .type = fact->type}
                    : (ContractValue){.uninterpreted = true};
        return true;
    }
    case ATOM_GROUNDED: {
        *out = (ContractValue){.ground = true, .uninterpreted = true};
        bool truth = false;
        if (petta_semantics_truth_value(expr, &truth)) {
            out->type = atom_symbol(scan->context->scratch, "Bool");
            return out->type != NULL;
        }
        return contract_literal_type(scan, expr, &out->type);
    }
    case ATOM_SYMBOL:
        if (plan && plan->role == PETTA_PLAN_STATIC_CALL)
            return false;
        *out = (ContractValue){
            .ground = true,
            .uninterpreted = contract_symbol(scan, atom_symbol_current_id(expr)),
        };
        /* The truth values type themselves, declared or not. */
        bool truth = false;
        if (petta_semantics_truth_value(expr, &truth)) {
            out->type = atom_symbol(scan->context->scratch, "Bool");
            return out->type != NULL;
        }
        if (out->uninterpreted)
            return true;
        out->type = contract_declared(scan, expr);
        if (out->type && out->type->kind == ATOM_EXPR)
            out->type = NULL;
        return true;
    case ATOM_EXPR:
        break;
    default:
        return false;
    }
    if (!plan || expr->expr.len == 0u)
        return false;
    Atom *head = expr->expr.elems[0];
    if ((cetta_control_operation_binding_layout(plan->computation.control, 1u) && expr->expr.len == 4u) ||
        (plan->computation.control.operation == CETTA_CONTROL_SEQUENCE_MANY && expr->expr.len == 3u))
        return contract_bindings(scan, expr, plan, depth, out);
    if (plan->computation.control.operation == CETTA_CONTROL_BRANCH &&
        (expr->expr.len == 3u || expr->expr.len == 4u))
        return contract_if(scan, expr, plan, depth, out);
    if (plan->computation.control.operation == CETTA_CONTROL_FAILURE) {
        *out = (ContractValue){.bottom = true};
        return true;
    }
    if (plan->computation.control.operation == CETTA_CONTROL_MATCH_BRANCHES)
        return contract_case(scan, expr, plan, depth, out);
    if (cetta_control_operation_is_control_form(plan->computation.control))
        return false;
    bool static_call = head->kind == ATOM_SYMBOL &&
        plan->role == PETTA_PLAN_STATIC_CALL;
    if (static_call && expr->expr.len == 4u &&
        atom_symbol_current_id(head) == g_builtin_syms.match)
        return contract_match(scan, expr, plan, depth, out);
    /* `(reduce (f a…))` over a written call is that call under the run-time
     * dispatch's handler, which the compiled call keeps. */
    if (static_call && expr->expr.len == 2u &&
        petta_semantics_form(atom_symbol_current_id(head)) == PETTA_FORM_REDUCE &&
        plan->child_count == 2u) {
        Atom *call = expr->expr.elems[1];
        const PettaPlanNode *call_plan = &plan->children[1];
        if (call->kind != ATOM_EXPR || call->expr.len == 0u ||
            call_plan->role != PETTA_PLAN_STATIC_CALL ||
            !call_plan->dispatch_handler ||
            call->expr.elems[0]->kind != ATOM_SYMBOL)
            return false;
        return contract_eval(scan, call, call_plan, depth + 1u, out);
    }
    if (static_call && atom_symbol_current_id(head) == scan->head &&
        expr->expr.len == scan->arity + 1u)
        return contract_self_call(scan, expr, plan, depth, out);
    if (static_call && petta_semantics_form(atom_symbol_current_id(head)) == PETTA_FORM_NONE)
        return contract_call(scan, expr, plan, depth, out);
    bool data = plan->role == PETTA_PLAN_DATA ||
        (plan->role == PETTA_PLAN_DYNAMIC_CALL && head->kind == ATOM_VAR);
    if (!data) {
        if (!scan->declined)
            scan->declined = expr;
        return false;
    }
    /* Data, or an application whose head is a variable: with every part
     * uninterpreted, dispatch finds nothing to run and the value is the
     * expression itself. */
    ContractValue value = {.ground = true, .uninterpreted = true};
    ContractValue parts[64];
    bool keep_parts = expr->expr.len <= 64u;
    for (CettaExprIndex i = 0u; i < expr->expr.len; i++) {
        ContractValue part = {0};
        if (!contract_eval(scan, expr->expr.elems[i],
                           petta_plan_child(plan, i), depth + 1u, &part))
            return false;
        if (part.bottom) {
            *out = part;
            return true;
        }
        if (keep_parts)
            parts[i] = part;
        value.ground = value.ground && part.ground;
        value.uninterpreted = value.uninterpreted && part.uninterpreted;
    }
    if (plan->role == PETTA_PLAN_DYNAMIC_CALL && !value.uninterpreted) {
        if (!scan->declined)
            scan->declined = expr;
        return false;
    }
    *out = value;
    if (head->kind == ATOM_SYMBOL && !value.uninterpreted && keep_parts) {
        Atom *arrow = contract_constructor_arrow(scan, head,
                                                 expr->expr.len - 1u);
        bool fields = arrow != NULL;
        for (CettaExprIndex i = 1u; fields && i < expr->expr.len; i++) {
            Atom *field = arrow->expr.elems[i];
            fields = field->kind == ATOM_VAR ||
                (parts[i].type && atom_eq(parts[i].type, field));
        }
        if (fields)
            out->type = arrow->expr.elems[arrow->expr.len - 1u];
    }
    return true;
}

/* Whether every answer of `rhs` is `pattern` as unification leaves it:
 * through lets, the template of a match, and conditionals whose branches
 * each return it or have no answer. */
static bool contract_returns_pattern(Atom *rhs, const PettaPlanNode *plan,
                                     Atom *pattern, uint32_t depth) {
    if (!rhs || depth > 256u)
        return false;
    if (atom_eq(rhs, pattern))
        return true;
    if (!plan || rhs->kind != ATOM_EXPR || rhs->expr.len == 0u)
        return false;
    if (plan->computation.control.operation == CETTA_CONTROL_FAILURE)
        return true;
    if (cetta_control_operation_binding_layout(plan->computation.control, 1u) && rhs->expr.len == 4u)
        return contract_returns_pattern(rhs->expr.elems[3],
                                        petta_plan_child(plan, 3u), pattern,
                                        depth + 1u);
    if (plan->computation.control.operation == CETTA_CONTROL_SEQUENCE_MANY && rhs->expr.len == 3u)
        return contract_returns_pattern(rhs->expr.elems[2],
                                        petta_plan_child(plan, 2u), pattern,
                                        depth + 1u);
    if (plan->computation.control.operation == CETTA_CONTROL_BRANCH &&
        (rhs->expr.len == 3u || rhs->expr.len == 4u))
        return contract_returns_pattern(rhs->expr.elems[2],
                                        petta_plan_child(plan, 2u), pattern,
                                        depth + 1u) &&
            (rhs->expr.len == 3u ||
             contract_returns_pattern(rhs->expr.elems[3],
                                      petta_plan_child(plan, 3u), pattern,
                                      depth + 1u));
    if (rhs->expr.len == 4u &&
        atom_is_symbol_id(rhs->expr.elems[0], g_builtin_syms.match))
        return contract_returns_pattern(rhs->expr.elems[3],
                                        petta_plan_child(plan, 3u), pattern,
                                        depth + 1u);
    return false;
}

/* The equation returns an instance of the polymorphic argument when its
 * value is that argument's head pattern (contract_returns_pattern). */
static bool contract_returns_argument(ContractScan *scan, Atom *lhs,
                                      Atom *rhs, const PettaPlanNode *plan) {
    if (scan->result_param >= scan->arity)
        return false;
    return contract_returns_pattern(rhs, plan,
                                    lhs->expr.elems[scan->result_param + 1u],
                                    0u);
}

static bool contract_equation(ContractScan *scan, Atom *lhs, Atom *rhs,
                              const PettaPlanNode *plan) {
    scan->fact_len = 0u;
    for (uint32_t i = 0u; i < scan->arity; i++) {
        Atom *pattern = lhs->expr.elems[i + 1u];
        switch (scan->version->facts[i]) {
        case PETTA_CONTRACT_ENTRY_GROUND:
            if (!contract_mark(scan, pattern, true, false, 0u) ||
                !contract_invert(scan, pattern, scan->formals[i], 0u))
                return false;
            break;
        case PETTA_CONTRACT_ENTRY_UNINTERPRETED:
            if (!contract_mark(scan, pattern, false, true, 0u))
                return false;
            break;
        case PETTA_CONTRACT_ENTRY_UNINTERPRETED_SPACE: {
            if (pattern->kind != ATOM_VAR)
                return false;
            ContractFact *fact = contract_fact_add(scan, pattern->var_id);
            if (!fact)
                return false;
            fact->space = true;
            break;
        }
        case PETTA_CONTRACT_ENTRY_CONSTANT: {
            if (pattern->kind != ATOM_VAR)
                return false;
            ContractFact *fact = contract_fact_add(scan, pattern->var_id);
            if (!fact)
                return false;
            fact->ground = true;
            fact->constant = scan->version->constants[i];
            break;
        }
        case PETTA_CONTRACT_ENTRY_ANY:
            if (!contract_mark(scan, pattern, false, false, 0u))
                return false;
            break;
        }
    }
    ContractValue value = {0};
    if (!contract_eval(scan, rhs, plan, 0u, &value))
        return false;
    if (scan->purity_only || !scan->signature.guard_result || value.bottom)
        return true;
    bool result = scan->closed_result
        ? value.ground && value.type &&
              atom_eq(value.type, scan->result_formal)
        : value.uninterpreted &&
              contract_returns_argument(scan, lhs, rhs, plan);
    if (!result && !scan->declined)
        scan->declined = rhs;
    return result;
}

/* A space parameter: the first operand of a match in some equation. */
static bool contract_reads_space(Atom *expr, VarId var, uint32_t depth) {
    if (!expr || expr->kind != ATOM_EXPR || depth > 256u)
        return false;
    if (expr->expr.len == 4u &&
        atom_is_symbol_id(expr->expr.elems[0], g_builtin_syms.match) &&
        expr->expr.elems[1]->kind == ATOM_VAR &&
        var_id_eq(expr->expr.elems[1]->var_id, var))
        return true;
    for (CettaExprIndex i = 0u; i < expr->expr.len; i++) {
        if (contract_reads_space(expr->expr.elems[i], var, depth + 1u))
            return true;
    }
    return false;
}

/* The function name every call of the relation to itself in `expr` passes
 * at parameter `index`: *constant stays SYMBOL_ID_NONE until a call names
 * one; a call passing the parameter's own variable `self` keeps it.  False
 * when two calls disagree or a call passes anything else. */
static bool contract_self_constant(ContractScan *scan, Atom *expr,
                                   uint32_t index, VarId self,
                                   SymbolId *constant, uint32_t depth) {
    if (!expr || expr->kind != ATOM_EXPR || depth > 256u)
        return true;
    if (expr->expr.len == scan->arity + 1u &&
        atom_is_symbol_id(expr->expr.elems[0], scan->head)) {
        Atom *argument = expr->expr.elems[index + 1u];
        if (argument->kind == ATOM_VAR && var_id_eq(argument->var_id, self)) {
            /* passes the parameter through */
        } else if (argument->kind == ATOM_SYMBOL &&
                   (*constant == SYMBOL_ID_NONE ||
                    *constant == atom_symbol_current_id(argument))) {
            *constant = atom_symbol_current_id(argument);
        } else {
            return false;
        }
    }
    for (CettaExprIndex i = 0u; i < expr->expr.len; i++) {
        if (!contract_self_constant(scan, expr->expr.elems[i], index, self,
                                    constant, depth + 1u))
            return false;
    }
    return true;
}

/* Every type variable of `formal` names, after unification with a
 * function's declaration, a closed type or a variable that declaration
 * names once: the function's guard then fixes those closed types and
 * constrains nothing else. */
static bool contract_fixes_closed(Bindings *sigma, Arena *arena, Atom *formal,
                                  Atom *declaration) {
    if (formal->kind == ATOM_VAR) {
        Atom *value = bindings_apply(sigma, arena, formal);
        if (!value)
            return false;
        if (value->kind == ATOM_VAR)
            return contract_var_occurrences(declaration, value->var_id) <= 1u;
        return !atom_has_vars(value);
    }
    if (formal->kind != ATOM_EXPR)
        return true;
    for (CettaExprIndex i = 0u; i < formal->expr.len; i++) {
        if (!contract_fixes_closed(sigma, arena, formal->expr.elems[i],
                                   declaration))
            return false;
    }
    return true;
}

static bool contract_entry_facts(ContractScan *scan,
                                 const PettaEquationCandidate *candidates,
                                 size_t count) {
    const PettaTypeCall *signature = &scan->signature;
    Arena *arena = scan->context->scratch;
    scan->result_param = scan->arity;
    Bindings sigma;
    bindings_init(&sigma);
    bool ok = true;
    bool constant[PETTA_CONTRACT_MAX_ARITY] = {false};
    /* Constant parameters: function-valued, the same name at every call. */
    for (uint32_t i = 0u; ok && i < scan->arity; i++) {
        PettaTypeArgument argument =
            petta_type_call_argument(signature, i + 1u, false);
        Atom *formal = argument.formal;
        if (argument.demand != PETTA_TYPE_TRANSLATE_AND_GUARD ||
            formal->kind != ATOM_EXPR || formal->expr.len < 2u ||
            !atom_is_symbol_id(formal->expr.elems[0], g_builtin_syms.arrow) ||
            !atom_has_vars(formal))
            continue;
        SymbolId name = SYMBOL_ID_NONE;
        for (size_t e = 0u; ok && e < count; e++) {
            Atom *equation = candidates[e].equation;
            Atom *pattern = equation->expr.elems[1]->expr.elems[i + 1u];
            ok = pattern->kind == ATOM_VAR &&
                contract_self_constant(scan, equation->expr.elems[2], i,
                                       pattern->var_id, &name, 0u);
        }
        Atom *declaration = ok && name != SYMBOL_ID_NONE
            ? contract_declared(scan, atom_symbol_id(arena, name)) : NULL;
        PettaTypeCall source = {0};
        PettaTypeCall fresh = {0};
        ok = declaration &&
            petta_type_call_plan(declaration, 0u, &source) &&
            petta_type_call_instantiate(arena, &source, &fresh) &&
            match_atoms(formal, fresh.signature, &sigma, arena) &&
            contract_fixes_closed(&sigma, arena, formal, fresh.signature);
        if (ok) {
            constant[i] = true;
            scan->version->facts[i] = PETTA_CONTRACT_ENTRY_CONSTANT;
            scan->version->constants[i] = name;
        } else if (contract_debug()) {
            fprintf(stderr, "contract %s: parameter %u is no constant "
                    "function (named %s)\n",
                    symbol_bytes(g_symbols, scan->head), i,
                    name != SYMBOL_ID_NONE ? symbol_bytes(g_symbols, name)
                                           : "nothing");
        }
    }
    for (uint32_t i = 0u; ok && i < scan->arity; i++) {
        Atom *formal = petta_type_call_argument(signature, i + 1u, false).formal;
        scan->formals[i] = bindings_apply(&sigma, arena, formal);
        ok = scan->formals[i] != NULL;
    }
    Atom *result = signature->guard_result ? signature->result_type : NULL;
    scan->result_formal = result ? bindings_apply(&sigma, arena, result) : NULL;
    ok = ok && (!result || scan->result_formal);
    if (ok && scan->result_formal && !atom_has_vars(scan->result_formal))
        scan->closed_result = true;
    else if (ok && scan->result_formal &&
             scan->result_formal->kind != ATOM_VAR)
        ok = false;
    for (uint32_t i = 0u; ok && i < scan->arity; i++) {
        if (constant[i])
            continue;
        PettaTypeArgument argument =
            petta_type_call_argument(signature, i + 1u, false);
        PettaContractEntryFact fact = PETTA_CONTRACT_ENTRY_ANY;
        if (argument.binder || argument.demand == PETTA_TYPE_RAW) {
            ok = false;
            break;
        }
        if (argument.demand == PETTA_TYPE_TRANSLATE_AND_GUARD) {
            Atom *formal = scan->formals[i];
            if (!atom_has_vars(formal)) {
                fact = PETTA_CONTRACT_ENTRY_GROUND;
            } else if (formal->kind == ATOM_VAR) {
                /* Uses outside the constant parameters, whose guards fix
                 * only closed types and fresh variables. */
                uint32_t uses = scan->closed_result
                    ? 0u
                    : contract_var_occurrences(scan->result_formal,
                                               formal->var_id);
                for (uint32_t j = 0u; j < scan->arity; j++) {
                    if (!constant[j])
                        uses += contract_var_occurrences(scan->formals[j],
                                                         formal->var_id);
                }
                bool shared_with_result = !scan->closed_result &&
                    scan->result_formal &&
                    var_id_eq(scan->result_formal->var_id, formal->var_id);
                if (uses == 1u) {
                    fact = PETTA_CONTRACT_ENTRY_ANY;
                } else if (uses == 2u && shared_with_result &&
                           scan->result_param == scan->arity) {
                    fact = PETTA_CONTRACT_ENTRY_UNINTERPRETED;
                    scan->result_param = i;
                } else {
                    ok = false;
                    break;
                }
            } else {
                ok = false;
                break;
            }
        }
        if (fact == PETTA_CONTRACT_ENTRY_ANY) {
            for (size_t e = 0u; e < count; e++) {
                Atom *equation = candidates[e].equation;
                Atom *lhs = equation->expr.elems[1];
                Atom *pattern = lhs->expr.elems[i + 1u];
                if (pattern->kind == ATOM_VAR &&
                    contract_reads_space(equation->expr.elems[2],
                                         pattern->var_id, 0u))
                    fact = PETTA_CONTRACT_ENTRY_UNINTERPRETED_SPACE;
            }
        }
        scan->version->facts[i] = fact;
    }
    bindings_free(&sigma);
    bool facts = ok && (!scan->result_formal || scan->closed_result ||
                        scan->result_param < scan->arity);
    if (!facts && contract_debug()) {
        Arena text;
        arena_init(&text);
        fprintf(stderr, "contract %s: no entry facts (parameters read %s, "
                "result %s)\n", symbol_bytes(g_symbols, scan->head),
                ok ? "all" : "not all",
                scan->result_formal
                    ? atom_to_string(&text, scan->result_formal) : "none");
        for (uint32_t i = 0u; i < scan->arity; i++)
            fprintf(stderr, "  parameter %u: %s fact %d\n", i,
                    scan->formals[i] ? atom_to_string(&text, scan->formals[i])
                                     : "?",
                    (int)scan->version->facts[i]);
        arena_free(&text);
    }
    return facts;
}

/* CETTA_PETTA_VERSIONS_REFERENCE=1 keeps every typed call on its complete
 * protocol, the reference the versions are compared with. */
static bool contract_versions_enabled(void) {
    static _Atomic int enabled = -1;
    int value = atomic_load_explicit(&enabled, memory_order_relaxed);
    if (value < 0) {
        const char *reference = getenv("CETTA_PETTA_VERSIONS_REFERENCE");
        value = !reference || strcmp(reference, "1") != 0;
        atomic_store_explicit(&enabled, value, memory_order_relaxed);
    }
    return value != 0;
}

/* The relation's equations of this arity, in order, each unifying its head
 * and carrying a plan; equations of other arities are other relations, as
 * the compiled tier reads them.  The caller frees *out. */
static bool contract_equations(ContractContext *context, SymbolId head,
                               uint32_t arity, PettaEquationCandidate **out,
                               size_t *count) {
    PettaEquationCandidate *candidates = NULL;
    size_t candidate_count = 0u;
    *out = NULL;
    *count = 0u;
    if (!petta_program_candidate_snapshot(context->program, context->space,
                                          head, &candidates,
                                          &candidate_count))
        return false;
    size_t used = 0u;
    bool ok = true;
    for (size_t e = 0u; ok && e < candidate_count; e++) {
        Atom *equation = candidates[e].equation;
        Atom *lhs = equation && equation->kind == ATOM_EXPR &&
                equation->expr.len == 3u ? equation->expr.elems[1] : NULL;
        ok = lhs && lhs->kind == ATOM_EXPR && lhs->expr.len > 0u &&
            atom_is_symbol_id(lhs->expr.elems[0], head);
        if (!ok || lhs->expr.len != arity + 1u)
            continue;
        /* A residual of higher-order specialization keeps its plan in the
         * specializer's catalog, where the compiled tier reads it too. */
        if (!candidates[e].rhs_plan)
            candidates[e].rhs_plan =
                petta_specializer_equation_plan(context->space, equation);
        ok = candidates[e].activation_layout.binding == CETTA_RULE_BIND_UNIFY &&
            candidates[e].rhs_plan != NULL;
        candidates[used++] = candidates[e];
    }
    if (!ok || used == 0u) {
        free(candidates);
        return false;
    }
    *out = candidates;
    *count = used;
    return true;
}

/* Analyse `head`/`arity`: a version when its signature and equations admit
 * one, otherwise whether its equations do only work a version may do. */
static void contract_analyse(ContractContext *context, ContractCallee *callee) {
    Arena *arena = context->scratch;
    Atom *head_atom = atom_symbol_id(arena, callee->head);
    PettaTypeCall *calls = NULL;
    uint32_t call_count = 0u;
    bool hold_body = false;
    PettaEquationCandidate *candidates = NULL;
    size_t count = 0u;
    bool equations = head_atom &&
        contract_equations(context, callee->head, callee->arity, &candidates,
                           &count);
    bool typed = equations &&
        petta_program_type_calls(context->program, context->space, head_atom,
                                 callee->arity, arena, &calls, &call_count,
                                 &hold_body) &&
        call_count == 1u && calls[0].arity == callee->arity && !hold_body;
    ContractScan scan = {
        .context = context, .head = callee->head, .arity = callee->arity,
        .version = &callee->version,
    };
    callee->version = (PettaContractVersion){.arity = callee->arity};
    bool versioned = false;
    if (typed) {
        scan.signature = calls[0];
        callee->signature = calls[0];
        versioned = contract_entry_facts(&scan, candidates, count);
        for (size_t e = 0u; versioned && e < count; e++) {
            Atom *equation = candidates[e].equation;
            versioned = contract_equation(&scan, equation->expr.elems[1],
                                          equation->expr.elems[2],
                                          candidates[e].rhs_plan);
        }
    }
    bool pure = versioned;
    if (!versioned && equations && (!call_count || call_count == 1u) &&
        !hold_body) {
        /* Pure: every equation does only work the analysis reads, with no
         * claim about any parameter. */
        ContractScan purity = {
            .context = context, .head = callee->head, .arity = callee->arity,
            .version = &callee->version, .purity_only = true,
            .result_param = callee->arity,
        };
        callee->version = (PettaContractVersion){.arity = callee->arity};
        if (call_count == 1u) {
            purity.signature = calls[0];
            callee->signature = calls[0];
        }
        pure = true;
        for (uint32_t i = 0u; pure && i < callee->arity; i++)
            purity.formals[i] = NULL;
        for (size_t e = 0u; pure && e < count; e++) {
            Atom *equation = candidates[e].equation;
            pure = contract_equation(&purity, equation->expr.elems[1],
                                     equation->expr.elems[2],
                                     candidates[e].rhs_plan);
        }
        free(purity.facts);
    }
    if (contract_debug()) {
        Arena text;
        arena_init(&text);
        const char *verdict = versioned ? "version" : pure ? "pure" : "neither";
        char *declined = !versioned && scan.declined
            ? atom_to_string(&text, scan.declined) : NULL;
        fprintf(stderr, "contract %s/%u: %s%s%s (equations %d, chains %u, "
                "discharged %u)\n",
                symbol_bytes(g_symbols, callee->head), callee->arity, verdict,
                declined ? "; declined at " : "",
                declined ? declined : (!versioned && typed ? "; entry facts" : ""),
                equations ? (int)count : -1, call_count,
                versioned ? callee->version.discharged_count : 0u);
        arena_free(&text);
    }
    free(scan.facts);
    free(candidates);
    free(calls);
    callee->state = versioned ? 1u : pure ? 2u : 3u;
}

static bool contract_function_version(ContractContext *context,
                                      SymbolId head, uint32_t arity,
                                      ContractCallee **out) {
    *out = NULL;
    for (uint32_t i = 0u; i < context->callee_len; i++) {
        if (context->callees[i].head == head &&
            context->callees[i].arity == arity) {
            *out = &context->callees[i];
            return true;
        }
    }
    if (arity == 0u || arity > PETTA_CONTRACT_MAX_ARITY)
        return false;
    if (context->callee_len == context->callee_cap) {
        uint32_t cap = context->callee_cap ? context->callee_cap * 2u : 8u;
        ContractCallee *callees =
            realloc(context->callees, sizeof(*callees) * cap);
        if (!callees)
            return false;
        context->callees = callees;
        context->callee_cap = cap;
    }
    uint32_t index = context->callee_len++;
    context->callees[index] = (ContractCallee){
        .head = head, .arity = arity, .state = 0u,
    };
    ContractCallee analysed = context->callees[index];
    contract_analyse(context, &analysed);
    /* The array may have grown while the callee's own callees were read. */
    context->callees[index] = analysed;
    *out = &context->callees[index];
    return true;
}

bool petta_contract_version_discharges(const PettaContractVersion *version,
                                       const Atom *call) {
    if (!version || !call)
        return false;
    for (uint32_t i = 0u; i < version->discharged_count; i++) {
        if (version->discharged[i] == call)
            return true;
    }
    return false;
}

bool petta_contract_trusted_version(PettaProgram *program, Space *space,
                                    SymbolId head, uint32_t arity,
                                    PettaContractVersion *version) {
    if (!contract_versions_enabled() ||
        !program || !space || !version || head == SYMBOL_ID_NONE ||
        arity == 0u || arity > PETTA_CONTRACT_MAX_ARITY ||
        eval_current_language_id() != CETTA_LANGUAGE_PETTA ||
        !space_type_annotations_functional(space))
        return false;
    Arena scratch;
    arena_init(&scratch);
    ContractContext context = {
        .program = program, .space = space, .scratch = &scratch,
    };
    ContractCallee *callee = NULL;
    bool ok = contract_function_version(&context, head, arity, &callee) &&
        callee && callee->state == 1u;
    if (ok)
        *version = callee->version;
    free(context.callees);
    contract_memo_free(&context.memo);
    arena_free(&scratch);
    return ok;
}

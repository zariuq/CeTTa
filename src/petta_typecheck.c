#include "petta_typecheck.h"

#include "eval.h"
#include "petta_semantics.h"
#include "match.h"
#include "parser.h"
#include "stats.h"
#include "symbol.h"
#include "petta_typecheck_census.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const CettaNikDirectAuthorityV1
    petta_typecheck_v2_direct_authority_v1 = {
        .alias = "PETTA-TYPECHECK-V2",
        .system_id = "petta.typecheck-v2",
        .authority_identity = UINT64_C(0x7065747461747970),
        .realization_identity = UINT64_C(0x74797065636b7632),
        .authority_revision = 1u,
        .realization_abi = 1u,
    };

typedef enum {
    PETTA_TYPECHECK_MODE_VALUE = 0,
    PETTA_TYPECHECK_MODE_TYPE,
} PettaTypecheckMode;

typedef enum {
    PETTA_MEMO_EMPTY = 0,
    PETTA_MEMO_EVALUATING,
    PETTA_MEMO_COMPLETE,
} PettaMemoState;

typedef struct {
    Atom *left;
    Atom *right;
    uint32_t hash;
    PettaTypecheckMode mode;
    PettaMemoState state;
    PettaTypecheckVerdict verdict;
} PettaTypeMemoEntry;

typedef enum {
    PETTA_DECL_NONE = 0,
    PETTA_DECL_ALIAS,
    PETTA_DECL_NEWTYPE,
    PETTA_DECL_AMBIGUOUS,
} PettaDeclKind;

typedef struct {
    PettaDeclKind kind;
    Atom *body;
} PettaDeclInfo;

typedef struct {
    Space *space;
    Arena *arena;
    const PettaTypecheckHooks *hooks;
    PettaTypeMemoEntry *memo;
    size_t memo_len;
    size_t memo_cap;
    PettaTypecheckReason reason;
    PettaTypecheckFault fault;
    SpaceDeclaredTypeLookupCost declaration_lookup_cost;
} PettaTypecheckContext;

static PettaTypecheckVerdict petta_check_value(
    PettaTypecheckContext *context, Atom *value, Atom *required);
static PettaTypecheckVerdict petta_check_types(
    PettaTypecheckContext *context, Atom *actual, Atom *required);

static bool petta_type_symbol_is(const Atom *atom, const char *name) {
    if (!atom || atom->kind != ATOM_SYMBOL || !g_symbols)
        return false;
    const char *actual = symbol_bytes(g_symbols, atom->sym_id);
    return actual && strcmp(actual, name) == 0;
}

static bool petta_type_head_is(const Atom *atom, const char *name) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len > 0u &&
           petta_type_symbol_is(atom->expr.elems[0], name);
}

static PettaAnalysisArrowMode petta_type_arrow_mode(const char *name) {
    if (!name)
        return PETTA_ANALYSIS_ARROW_MODE_INVALID;
    size_t length = strlen(name);
    if (strcmp(name, "->") == 0)
        return PETTA_ANALYSIS_ARROW_MODE_PLAIN;
    if (strcmp(name, "-[det]->") == 0 ||
        strcmp(name, "-[deterministic]->") == 0) {
        return PETTA_ANALYSIS_ARROW_MODE_DETERMINISTIC;
    }
    if (strcmp(name, "-[semidet]->") == 0 ||
        strcmp(name, "-[semideterministic]->") == 0) {
        return PETTA_ANALYSIS_ARROW_MODE_SEMIDETERMINISTIC;
    }
    if (strcmp(name, "-[nondet]->") == 0 ||
        strcmp(name, "-[nondeterministic]->") == 0) {
        return PETTA_ANALYSIS_ARROW_MODE_NONDETERMINISTIC;
    }
    if (strncmp(name, "-[$", 3u) == 0 && length > 5u &&
        strcmp(name + length - 3u, "]->") == 0) {
        return PETTA_ANALYSIS_ARROW_MODE_EFFECT;
    }
    return PETTA_ANALYSIS_ARROW_MODE_INVALID;
}

static bool petta_type_arrow_symbol_name(const char *name) {
    return petta_type_arrow_mode(name) != PETTA_ANALYSIS_ARROW_MODE_INVALID;
}

static bool petta_type_arrow_mode_fits(
    const char *actual, const char *required) {
    PettaAnalysisArrowMode actual_mode = petta_type_arrow_mode(actual);
    PettaAnalysisArrowMode required_mode = petta_type_arrow_mode(required);
    if (required_mode == PETTA_ANALYSIS_ARROW_MODE_PLAIN) {
        CETTA_PETTA_TYPECHECK_CENSUS_HIT(
            CETTA_PETTA_TYPECHECK_CENSUS_EVENT_ARROW_MODE_PLAIN_ACCEPT);
        return petta_analysis_arrow_mode_fits(actual_mode, required_mode);
    }
    if (required_mode == PETTA_ANALYSIS_ARROW_MODE_NONDETERMINISTIC) {
        CETTA_PETTA_TYPECHECK_CENSUS_HIT(
            CETTA_PETTA_TYPECHECK_CENSUS_EVENT_ARROW_MODE_NONDET_ACCEPT);
        return petta_analysis_arrow_mode_fits(actual_mode, required_mode);
    }
    if (required_mode == PETTA_ANALYSIS_ARROW_MODE_EFFECT) {
        CETTA_PETTA_TYPECHECK_CENSUS_HIT(
            CETTA_PETTA_TYPECHECK_CENSUS_EVENT_ARROW_MODE_EFFECT_ACCEPT);
        return petta_analysis_arrow_mode_fits(actual_mode, required_mode);
    }
    if (required_mode == PETTA_ANALYSIS_ARROW_MODE_DETERMINISTIC) {
        if (petta_analysis_arrow_mode_fits(actual_mode, required_mode)) {
            CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                CETTA_PETTA_TYPECHECK_CENSUS_EVENT_ARROW_MODE_DET_EXACT);
            return true;
        }
        CETTA_PETTA_TYPECHECK_CENSUS_HIT(
            CETTA_PETTA_TYPECHECK_CENSUS_EVENT_ARROW_MODE_REJECT);
        return false;
    }
    if (required_mode == PETTA_ANALYSIS_ARROW_MODE_SEMIDETERMINISTIC) {
        if (actual_mode == PETTA_ANALYSIS_ARROW_MODE_DETERMINISTIC) {
            CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                CETTA_PETTA_TYPECHECK_CENSUS_EVENT_ARROW_MODE_DET_TO_SEMIDET);
            return true;
        }
        if (petta_analysis_arrow_mode_fits(actual_mode, required_mode)) {
            CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                CETTA_PETTA_TYPECHECK_CENSUS_EVENT_ARROW_MODE_SEMIDET_EXACT);
            return true;
        }
    }
    CETTA_PETTA_TYPECHECK_CENSUS_HIT(
        CETTA_PETTA_TYPECHECK_CENSUS_EVENT_ARROW_MODE_REJECT);
    return false;
}

static const char *petta_type_arrow_mode_name(const Atom *type) {
    if (!type || type->kind != ATOM_EXPR || type->expr.len < 2u ||
        type->expr.elems[0]->kind != ATOM_SYMBOL || !g_symbols) {
        return NULL;
    }
    const char *name = symbol_bytes(
        g_symbols, type->expr.elems[0]->sym_id);
    return petta_type_arrow_symbol_name(name) ? name : NULL;
}

static bool petta_type_is_arrow(const Atom *type) {
    return petta_type_arrow_mode_name(type) != NULL;
}

static bool petta_type_is_wildcard(const Atom *type) {
    return petta_type_symbol_is(type, "%Undefined%") ||
           petta_type_symbol_is(type, "Atom") ||
           petta_type_symbol_is(type, "Expression");
}

static uint32_t petta_type_pair_hash(
    PettaTypecheckMode mode, Atom *left, Atom *right) {
    uint32_t hash = atom_hash(left) ^ (atom_hash(right) * 0x9e3779b1u) ^
                    ((uint32_t)mode * 0x85ebca6bu);
    hash ^= hash >> 16;
    hash *= 0x7feb352du;
    hash ^= hash >> 15;
    return hash ? hash : 1u;
}

static bool petta_memo_grow(PettaTypecheckContext *context) {
    size_t next_cap = context->memo_cap ? context->memo_cap * 2u : 64u;
    if (next_cap <= context->memo_cap ||
        next_cap > SIZE_MAX / sizeof(*context->memo)) {
        context->fault = PETTA_TYPECHECK_FAULT_ALLOCATION;
        return false;
    }
    PettaTypeMemoEntry *next = calloc(next_cap, sizeof(*next));
    if (!next) {
        context->fault = PETTA_TYPECHECK_FAULT_ALLOCATION;
        return false;
    }
    for (size_t index = 0u; index < context->memo_cap; index++) {
        PettaTypeMemoEntry entry = context->memo[index];
        if (entry.state == PETTA_MEMO_EMPTY)
            continue;
        size_t slot = (size_t)entry.hash & (next_cap - 1u);
        while (next[slot].state != PETTA_MEMO_EMPTY)
            slot = (slot + 1u) & (next_cap - 1u);
        next[slot] = entry;
    }
    free(context->memo);
    context->memo = next;
    context->memo_cap = next_cap;
    return true;
}

static PettaTypeMemoEntry *petta_memo_entry(
    PettaTypecheckContext *context, PettaTypecheckMode mode,
    Atom *left, Atom *right, bool *existing) {
    *existing = false;
    if (context->memo_cap == 0u ||
        (context->memo_len + 1u) * 10u >= context->memo_cap * 7u) {
        if (!petta_memo_grow(context))
            return NULL;
    }
    uint32_t hash = petta_type_pair_hash(mode, left, right);
    size_t slot = (size_t)hash & (context->memo_cap - 1u);
    for (;;) {
        PettaTypeMemoEntry *entry = &context->memo[slot];
        if (entry->state == PETTA_MEMO_EMPTY) {
            *entry = (PettaTypeMemoEntry){
                .left = left,
                .right = right,
                .hash = hash,
                .mode = mode,
                .state = PETTA_MEMO_EVALUATING,
                .verdict = PETTA_TYPECHECK_UNDETERMINED,
            };
            context->memo_len++;
            return entry;
        }
        if (entry->hash == hash && entry->mode == mode &&
            atom_eq(entry->left, left) && atom_eq(entry->right, right)) {
            *existing = true;
            return entry;
        }
        slot = (slot + 1u) & (context->memo_cap - 1u);
    }
}

static PettaDeclInfo petta_decl_info(
    PettaTypecheckContext *context, Atom *name) {
    PettaDeclInfo result = {0};
    if (!name || name->kind != ATOM_SYMBOL)
        return result;
    Atom **declared = NULL;
    uint32_t count = space_get_declared_types_costed(
        context->space, context->arena, name, &declared,
        &context->declaration_lookup_cost);
    for (uint32_t index = 0u; index < count; index++) {
        Atom *decl = declared[index];
        PettaDeclKind kind = PETTA_DECL_NONE;
        if (decl && decl->kind == ATOM_EXPR && decl->expr.len == 2u) {
            if (petta_type_symbol_is(decl->expr.elems[0], "Alias"))
                kind = PETTA_DECL_ALIAS;
            else if (petta_type_symbol_is(decl->expr.elems[0], "Newtype"))
                kind = PETTA_DECL_NEWTYPE;
        }
        if (kind == PETTA_DECL_NONE)
            continue;
        if (result.kind == PETTA_DECL_NONE) {
            result.kind = kind;
            result.body = decl->expr.elems[1];
        } else if (result.kind != kind ||
                   !atom_eq(result.body, decl->expr.elems[1])) {
            result.kind = PETTA_DECL_AMBIGUOUS;
            result.body = NULL;
            break;
        }
    }
    free(declared);
    return result;
}

typedef enum {
    PETTA_LITERAL_NONE = 0,
    PETTA_LITERAL_NUMBER,
    PETTA_LITERAL_STRING,
    PETTA_LITERAL_BOOL,
} PettaLiteralSort;

static PettaLiteralSort petta_type_literal_sort(const Atom *type) {
    if (petta_type_symbol_is(type, "Number"))
        return PETTA_LITERAL_NUMBER;
    if (petta_type_symbol_is(type, "String"))
        return PETTA_LITERAL_STRING;
    if (petta_type_symbol_is(type, "Bool"))
        return PETTA_LITERAL_BOOL;
    return PETTA_LITERAL_NONE;
}

static PettaLiteralSort petta_value_literal_sort(const Atom *value) {
    if (!value)
        return PETTA_LITERAL_NONE;
    if (value->kind == ATOM_GROUNDED) {
        switch (value->ground.gkind) {
        case GV_INT:
        case GV_FLOAT:
        case GV_BIGINT:
        case GV_RATIONAL:
            return PETTA_LITERAL_NUMBER;
        case GV_STRING:
            return PETTA_LITERAL_STRING;
        case GV_BOOL:
            return PETTA_LITERAL_BOOL;
        default:
            return PETTA_LITERAL_NONE;
        }
    }
    if (petta_type_symbol_is(value, "true") ||
        petta_type_symbol_is(value, "false") ||
        petta_type_symbol_is(value, "True") ||
        petta_type_symbol_is(value, "False"))
        return PETTA_LITERAL_BOOL;
    return PETTA_LITERAL_NONE;
}

/*
 * A Foreign declaration is a nominal promise made by the imported function
 * signature; its runtime carrier is deliberately opaque.  Roman's checker
 * therefore accepts every settled value against a well-formed Foreign type
 * of the declared arity and never inspects the carrier representation.
 */
static bool petta_required_is_foreign_type(
    PettaTypecheckContext *context, Atom *required) {
    Atom *name = required;
    CettaExprLen parameter_count = 0u;
    if (required && required->kind == ATOM_EXPR &&
        required->expr.len > 0u) {
        name = required->expr.elems[0];
        parameter_count = required->expr.len - 1u;
    }
    if (!name || name->kind != ATOM_SYMBOL)
        return false;

    Atom **declared = NULL;
    uint32_t count = space_get_declared_types_costed(
        context->space, context->arena, name, &declared,
        &context->declaration_lookup_cost);
    bool matched = false;
    for (uint32_t index = 0u; index < count; index++) {
        Atom *declaration = declared[index];
        if (!petta_type_head_is(declaration, "Foreign"))
            continue;
        if (declaration->expr.len == 1u) {
            matched = parameter_count == 0u;
        } else if (declaration->expr.len == 2u) {
            Atom *arity = declaration->expr.elems[1];
            matched = arity && arity->kind == ATOM_GROUNDED &&
                      arity->ground.gkind == GV_INT &&
                      arity->ground.ival > 0 &&
                      (uint64_t)arity->ground.ival ==
                          (uint64_t)parameter_count;
        }
        if (matched)
            break;
    }
    free(declared);
    return matched;
}

static PettaTypecheckVerdict petta_check_declared_value_types(
    PettaTypecheckContext *context, Atom *value, Atom *required,
    bool *considered_out) {
    if (considered_out)
        *considered_out = false;
    Atom *subject = value;
    CettaExprLen nargs = 0u;
    bool applied = false;
    if (value->kind == ATOM_EXPR && value->expr.len > 0u &&
        value->expr.elems[0]->kind == ATOM_SYMBOL) {
        subject = value->expr.elems[0];
        nargs = value->expr.len - 1u;
        applied = true;
    }
    if (!subject || subject->kind != ATOM_SYMBOL)
        return PETTA_TYPECHECK_UNDETERMINED;
    Atom **declared = NULL;
    uint32_t count = space_get_declared_types_costed(
        context->space, context->arena, subject, &declared,
        &context->declaration_lookup_cost);
    PettaTypecheckVerdict aggregate = PETTA_TYPECHECK_REFUTED;
    bool considered = false;
    for (uint32_t index = 0u; index < count; index++) {
        Atom *actual = declared[index];
        if (applied) {
            if (!petta_type_is_arrow(actual) ||
                actual->expr.len != nargs + 2u)
                continue;
            actual = actual->expr.elems[actual->expr.len - 1u];
        }
        considered = true;
        aggregate = petta_analysis_verdict_any(
            aggregate, petta_check_types(context, actual, required));
        if (aggregate == PETTA_TYPECHECK_ESTABLISHED)
            break;
    }
    free(declared);
    if (considered_out)
        *considered_out = considered;
    return considered ? aggregate : PETTA_TYPECHECK_UNDETERMINED;
}

static PettaTypecheckVerdict petta_check_type_body(
    PettaTypecheckContext *context, Atom *actual, Atom *required) {
    if ((actual->kind == ATOM_EXPR && actual->expr.len == 0u) ||
        (required->kind == ATOM_EXPR && required->expr.len == 0u)) {
        context->fault = PETTA_TYPECHECK_FAULT_MALFORMED_TYPE;
        return PETTA_TYPECHECK_UNDETERMINED;
    }
    if (actual->kind == ATOM_VAR || required->kind == ATOM_VAR) {
        context->reason = PETTA_TYPECHECK_REASON_OPEN_VALUE;
        return PETTA_TYPECHECK_UNDETERMINED;
    }

    PettaDeclInfo actual_decl = petta_decl_info(context, actual);
    PettaDeclInfo required_decl = petta_decl_info(context, required);
    if (actual_decl.kind == PETTA_DECL_AMBIGUOUS ||
        required_decl.kind == PETTA_DECL_AMBIGUOUS) {
        context->reason = PETTA_TYPECHECK_REASON_OPEN_VALUE;
        return PETTA_TYPECHECK_UNDETERMINED;
    }

    /* Nominality is decided before wildcard erasure, as in type_unify/2. */
    if (actual_decl.kind == PETTA_DECL_NEWTYPE ||
        required_decl.kind == PETTA_DECL_NEWTYPE) {
        if (actual_decl.kind == PETTA_DECL_NEWTYPE &&
            petta_type_head_is(required, "|")) {
            CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                CETTA_PETTA_TYPECHECK_CENSUS_EVENT_NEWTYPE_ACTUAL_REQUIRED_UNION);
            PettaTypecheckVerdict aggregate = PETTA_TYPECHECK_REFUTED;
            for (CettaExprIndex index = 1u;
                 index < required->expr.len; index++) {
                aggregate = petta_analysis_verdict_any(
                    aggregate,
                    petta_check_types(
                        context, actual,
                        required->expr.elems[index]));
                if (aggregate == PETTA_TYPECHECK_ESTABLISHED)
                    break;
            }
            return aggregate;
        }
        if (actual_decl.kind == PETTA_DECL_NEWTYPE &&
            required_decl.kind == PETTA_DECL_NEWTYPE) {
            context->reason = actual->kind == ATOM_SYMBOL &&
                                      required->kind == ATOM_SYMBOL &&
                                      actual->sym_id == required->sym_id
                                  ? PETTA_TYPECHECK_REASON_EXACT
                                  : PETTA_TYPECHECK_REASON_MISMATCH;
            return context->reason == PETTA_TYPECHECK_REASON_EXACT
                       ? PETTA_TYPECHECK_ESTABLISHED
                       : PETTA_TYPECHECK_REFUTED;
        }
        if (required_decl.kind == PETTA_DECL_NEWTYPE) {
            if (petta_type_head_is(actual, "|")) {
                PettaTypecheckVerdict aggregate =
                    PETTA_TYPECHECK_ESTABLISHED;
                for (CettaExprIndex index = 1u;
                     index < actual->expr.len; index++) {
                    aggregate = petta_analysis_verdict_all(
                        aggregate,
                        petta_check_types(
                            context, actual->expr.elems[index], required));
                }
                return aggregate;
            }
            context->reason = PETTA_TYPECHECK_REASON_MISMATCH;
            return PETTA_TYPECHECK_REFUTED;
        }
        if (petta_type_is_wildcard(required)) {
            context->reason = PETTA_TYPECHECK_REASON_WILDCARD;
            return PETTA_TYPECHECK_ESTABLISHED;
        }
        if (!actual_decl.body || petta_type_is_wildcard(actual_decl.body)) {
            context->reason = PETTA_TYPECHECK_REASON_MISMATCH;
            return PETTA_TYPECHECK_REFUTED;
        }
        return petta_check_types(context, actual_decl.body, required);
    }

    if (actual_decl.kind == PETTA_DECL_ALIAS)
        return petta_check_types(context, actual_decl.body, required);
    if (required_decl.kind == PETTA_DECL_ALIAS)
        return petta_check_types(context, actual, required_decl.body);

    if (petta_type_is_wildcard(actual) ||
        petta_type_is_wildcard(required)) {
        context->reason = PETTA_TYPECHECK_REASON_WILDCARD;
        return PETTA_TYPECHECK_ESTABLISHED;
    }

    if (petta_type_head_is(actual, "|")) {
        PettaTypecheckVerdict aggregate = PETTA_TYPECHECK_ESTABLISHED;
        for (CettaExprIndex index = 1u; index < actual->expr.len; index++) {
            aggregate = petta_analysis_verdict_all(
                aggregate,
                petta_check_types(
                    context, actual->expr.elems[index], required));
        }
        return aggregate;
    }
    if (petta_type_head_is(required, "|")) {
        PettaTypecheckVerdict aggregate = PETTA_TYPECHECK_REFUTED;
        for (CettaExprIndex index = 1u;
             index < required->expr.len; index++) {
            aggregate = petta_analysis_verdict_any(
                aggregate,
                petta_check_types(
                    context, actual, required->expr.elems[index]));
            if (aggregate == PETTA_TYPECHECK_ESTABLISHED)
                break;
        }
        return aggregate;
    }

    const char *actual_arrow_mode = petta_type_arrow_mode_name(actual);
    const char *required_arrow_mode = petta_type_arrow_mode_name(required);
    if (actual_arrow_mode || required_arrow_mode) {
        if (!actual_arrow_mode || !required_arrow_mode ||
            actual->expr.len != required->expr.len) {
            CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                CETTA_PETTA_TYPECHECK_CENSUS_EVENT_ARROW_SHAPE_REJECT);
            context->reason = PETTA_TYPECHECK_REASON_MISMATCH;
            return PETTA_TYPECHECK_REFUTED;
        }
        if (!petta_type_arrow_mode_fits(
                actual_arrow_mode, required_arrow_mode)) {
            context->reason = PETTA_TYPECHECK_REASON_MISMATCH;
            return PETTA_TYPECHECK_REFUTED;
        }
        PettaTypecheckVerdict aggregate = PETTA_TYPECHECK_ESTABLISHED;
        for (CettaExprIndex index = 1u;
             index < actual->expr.len; index++) {
            aggregate = petta_analysis_verdict_all(
                aggregate,
                petta_check_types(
                    context, actual->expr.elems[index],
                    required->expr.elems[index]));
            if (aggregate == PETTA_TYPECHECK_REFUTED) {
                CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                    CETTA_PETTA_TYPECHECK_CENSUS_EVENT_ARROW_COMPONENT_REJECT);
                break;
            }
        }
        if (aggregate == PETTA_TYPECHECK_ESTABLISHED) {
            CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                CETTA_PETTA_TYPECHECK_CENSUS_EVENT_ARROW_COMPATIBLE);
            context->reason = PETTA_TYPECHECK_REASON_STRUCTURAL;
        }
        return aggregate;
    }

    if (actual->kind != required->kind) {
        context->reason = PETTA_TYPECHECK_REASON_MISMATCH;
        return PETTA_TYPECHECK_REFUTED;
    }
    if (actual->kind != ATOM_EXPR) {
        bool equal = atom_eq(actual, required);
        context->reason = equal ? PETTA_TYPECHECK_REASON_EXACT
                                : PETTA_TYPECHECK_REASON_MISMATCH;
        return equal ? PETTA_TYPECHECK_ESTABLISHED
                     : PETTA_TYPECHECK_REFUTED;
    }
    if (actual->expr.len == 0u || required->expr.len == 0u ||
        actual->expr.len != required->expr.len ||
        !atom_eq(actual->expr.elems[0], required->expr.elems[0])) {
        context->reason = PETTA_TYPECHECK_REASON_MISMATCH;
        return PETTA_TYPECHECK_REFUTED;
    }
    PettaTypecheckVerdict aggregate = PETTA_TYPECHECK_ESTABLISHED;
    for (CettaExprIndex index = 1u; index < actual->expr.len; index++) {
        aggregate = petta_analysis_verdict_all(
            aggregate,
            petta_check_types(
                context, actual->expr.elems[index],
                required->expr.elems[index]));
        if (aggregate == PETTA_TYPECHECK_REFUTED)
            break;
    }
    if (aggregate == PETTA_TYPECHECK_ESTABLISHED)
        context->reason = PETTA_TYPECHECK_REASON_STRUCTURAL;
    return aggregate;
}

static PettaTypecheckVerdict petta_check_types(
    PettaTypecheckContext *context, Atom *actual, Atom *required) {
    if (!actual || !required) {
        context->fault = PETTA_TYPECHECK_FAULT_INVALID_ARGUMENT;
        return PETTA_TYPECHECK_UNDETERMINED;
    }
    bool existing = false;
    PettaTypeMemoEntry *memo = petta_memo_entry(
        context, PETTA_TYPECHECK_MODE_TYPE,
        actual, required, &existing);
    if (!memo)
        return PETTA_TYPECHECK_UNDETERMINED;
    if (existing) {
        if (memo->state == PETTA_MEMO_EVALUATING) {
            context->reason = PETTA_TYPECHECK_REASON_CYCLE;
            return PETTA_TYPECHECK_UNDETERMINED;
        }
        return memo->verdict;
    }
    PettaTypecheckVerdict verdict =
        petta_check_type_body(context, actual, required);
    memo->state = PETTA_MEMO_COMPLETE;
    memo->verdict = verdict;
    return verdict;
}

static PettaTypecheckVerdict petta_check_required_list(
    PettaTypecheckContext *context, Atom *value, Atom *element_type) {
    if (!value || value->kind == ATOM_VAR) {
        context->reason = PETTA_TYPECHECK_REASON_OPEN_VALUE;
        return PETTA_TYPECHECK_UNDETERMINED;
    }
    if (value->kind != ATOM_EXPR) {
        context->reason = PETTA_TYPECHECK_REASON_MISMATCH;
        return PETTA_TYPECHECK_REFUTED;
    }
    PeTTaLogicalListCursor cursor;
    petta_semantics_logical_list_cursor_init(&cursor, value);
    PettaTypecheckVerdict aggregate = PETTA_TYPECHECK_ESTABLISHED;
    for (;;) {
        Atom *item = NULL;
        PeTTaLogicalListStep step =
            petta_semantics_logical_list_cursor_next(&cursor, &item);
        if (step == PETTA_LOGICAL_LIST_END) {
            if (aggregate == PETTA_TYPECHECK_ESTABLISHED)
                context->reason = PETTA_TYPECHECK_REASON_STRUCTURAL;
            return aggregate;
        }
        if (step == PETTA_LOGICAL_LIST_INVALID) {
            context->reason = PETTA_TYPECHECK_REASON_OPEN_VALUE;
            return PETTA_TYPECHECK_UNDETERMINED;
        }
        aggregate = petta_analysis_verdict_all(
            aggregate,
            petta_check_value(context, item, element_type));
        if (aggregate == PETTA_TYPECHECK_REFUTED)
            return aggregate;
    }
}

static PettaTypecheckVerdict petta_check_arrow_value(
    PettaTypecheckContext *context, Atom *value, Atom *required) {
    CettaExprLen arity = required->expr.len - 2u;
    PettaTypecheckCallable callable =
        context->hooks && context->hooks->callable
            ? context->hooks->callable(
                  context->hooks->context, value, arity)
            : PETTA_TYPECHECK_CALLABLE_UNKNOWN;
    if (callable == PETTA_TYPECHECK_CALLABLE_NO) {
        context->reason = PETTA_TYPECHECK_REASON_NONCALLABLE;
        return PETTA_TYPECHECK_REFUTED;
    }
    if (callable == PETTA_TYPECHECK_CALLABLE_UNKNOWN) {
        context->reason = PETTA_TYPECHECK_REASON_OPEN_VALUE;
        return PETTA_TYPECHECK_UNDETERMINED;
    }
    PettaTypecheckVerdict declared =
        petta_check_declared_value_types(
            context, value, required, NULL);
    if (declared == PETTA_TYPECHECK_ESTABLISHED)
        context->reason = PETTA_TYPECHECK_REASON_DECLARED;
    return declared;
}

static PettaTypecheckVerdict petta_check_value_body(
    PettaTypecheckContext *context, Atom *value, Atom *required) {
    if (required->kind == ATOM_EXPR && required->expr.len == 0u) {
        context->fault = PETTA_TYPECHECK_FAULT_MALFORMED_TYPE;
        return PETTA_TYPECHECK_UNDETERMINED;
    }
    if (value->kind == ATOM_VAR) {
        context->reason = PETTA_TYPECHECK_REASON_OPEN_VALUE;
        return PETTA_TYPECHECK_UNDETERMINED;
    }
    /* Roman's residual check treats an open required type as a successful
     * polymorphic slot (and refines it when a single type is available).
     * This judgment returns only a verdict, so the refinement remains with
     * the typed-call environment; the status itself is ESTABLISHED. */
    if (required->kind == ATOM_VAR) {
        context->reason = PETTA_TYPECHECK_REASON_WILDCARD;
        return PETTA_TYPECHECK_ESTABLISHED;
    }

    PettaDeclInfo required_decl = petta_decl_info(context, required);
    if (required_decl.kind == PETTA_DECL_AMBIGUOUS) {
        context->reason = PETTA_TYPECHECK_REASON_OPEN_VALUE;
        return PETTA_TYPECHECK_UNDETERMINED;
    }
    if (required_decl.kind == PETTA_DECL_ALIAS)
        return petta_check_value(context, value, required_decl.body);
    /* Runtime brand syntax is erased by the PeTTa translator, so a residual
     * value check follows the declared representation.  Nominal identity is
     * still enforced by the type-to-type judgment above, before wildcard
     * erasure; the two judgments must not be conflated. */
    if (required_decl.kind == PETTA_DECL_NEWTYPE)
        return petta_check_value(context, value, required_decl.body);
    if (petta_required_is_foreign_type(context, required)) {
        context->reason = PETTA_TYPECHECK_REASON_DECLARED;
        return PETTA_TYPECHECK_ESTABLISHED;
    }
    if (petta_type_is_wildcard(required)) {
        context->reason = PETTA_TYPECHECK_REASON_WILDCARD;
        return PETTA_TYPECHECK_ESTABLISHED;
    }
    if (atom_eq(value, required)) {
        context->reason = PETTA_TYPECHECK_REASON_EXACT;
        return PETTA_TYPECHECK_ESTABLISHED;
    }
    if (petta_type_head_is(required, "|")) {
        PettaTypecheckVerdict aggregate = PETTA_TYPECHECK_REFUTED;
        for (CettaExprIndex index = 1u;
             index < required->expr.len; index++) {
            aggregate = petta_analysis_verdict_any(
                aggregate,
                petta_check_value(
                    context, value, required->expr.elems[index]));
            if (aggregate == PETTA_TYPECHECK_ESTABLISHED)
                break;
        }
        return aggregate;
    }
    if (petta_type_head_is(required, "List") &&
        required->expr.len == 2u) {
        return petta_check_required_list(
            context, value, required->expr.elems[1]);
    }
    if (petta_type_is_arrow(required)) {
        if (required->expr.len < 2u) {
            context->fault = PETTA_TYPECHECK_FAULT_MALFORMED_TYPE;
            return PETTA_TYPECHECK_UNDETERMINED;
        }
        return petta_check_arrow_value(context, value, required);
    }

    PettaLiteralSort formal_sort = petta_type_literal_sort(required);
    PettaLiteralSort value_sort = petta_value_literal_sort(value);
    if (formal_sort != PETTA_LITERAL_NONE ||
        value_sort != PETTA_LITERAL_NONE) {
        if (formal_sort != PETTA_LITERAL_NONE &&
            value_sort == PETTA_LITERAL_NONE) {
            bool considered = false;
            PettaTypecheckVerdict declared =
                petta_check_declared_value_types(
                    context, value, required, &considered);
            if (considered) {
                context->reason = declared == PETTA_TYPECHECK_ESTABLISHED
                    ? PETTA_TYPECHECK_REASON_DECLARED
                    : declared == PETTA_TYPECHECK_REFUTED
                        ? PETTA_TYPECHECK_REASON_MISMATCH
                        : PETTA_TYPECHECK_REASON_OPEN_VALUE;
                return declared;
            }
        }
        bool equal = formal_sort != PETTA_LITERAL_NONE &&
                     formal_sort == value_sort;
        context->reason = equal ? PETTA_TYPECHECK_REASON_EXACT
                                : PETTA_TYPECHECK_REASON_MISMATCH;
        return equal ? PETTA_TYPECHECK_ESTABLISHED
                     : PETTA_TYPECHECK_REFUTED;
    }

    if (required->kind == ATOM_EXPR) {
        if (required->expr.len == 0u) {
            context->fault = PETTA_TYPECHECK_FAULT_MALFORMED_TYPE;
            return PETTA_TYPECHECK_UNDETERMINED;
        }
        if (value->kind != ATOM_EXPR ||
            value->expr.len != required->expr.len ||
            value->expr.len == 0u) {
            context->reason = PETTA_TYPECHECK_REASON_MISMATCH;
            return PETTA_TYPECHECK_REFUTED;
        }
        PettaTypecheckVerdict aggregate = PETTA_TYPECHECK_ESTABLISHED;
        for (CettaExprIndex index = 0u;
             index < required->expr.len; index++) {
            aggregate = petta_analysis_verdict_all(
                aggregate,
                petta_check_value(
                    context, value->expr.elems[index],
                    required->expr.elems[index]));
            if (aggregate == PETTA_TYPECHECK_REFUTED)
                break;
        }
        if (aggregate == PETTA_TYPECHECK_ESTABLISHED)
            context->reason = PETTA_TYPECHECK_REASON_STRUCTURAL;
        return aggregate;
    }

    PettaTypecheckVerdict declared =
        petta_check_declared_value_types(
            context, value, required, NULL);
    if (declared == PETTA_TYPECHECK_ESTABLISHED) {
        context->reason = PETTA_TYPECHECK_REASON_DECLARED;
        return declared;
    }
    if (declared == PETTA_TYPECHECK_REFUTED) {
        context->reason = PETTA_TYPECHECK_REASON_MISMATCH;
        return declared;
    }
    context->reason = PETTA_TYPECHECK_REASON_OPEN_VALUE;
    return PETTA_TYPECHECK_UNDETERMINED;
}

static PettaTypecheckVerdict petta_check_value(
    PettaTypecheckContext *context, Atom *value, Atom *required) {
    if (!value || !required) {
        context->fault = PETTA_TYPECHECK_FAULT_INVALID_ARGUMENT;
        return PETTA_TYPECHECK_UNDETERMINED;
    }
    bool existing = false;
    PettaTypeMemoEntry *memo = petta_memo_entry(
        context, PETTA_TYPECHECK_MODE_VALUE,
        value, required, &existing);
    if (!memo)
        return PETTA_TYPECHECK_UNDETERMINED;
    if (existing) {
        if (memo->state == PETTA_MEMO_EVALUATING) {
            context->reason = PETTA_TYPECHECK_REASON_CYCLE;
            return PETTA_TYPECHECK_UNDETERMINED;
        }
        return memo->verdict;
    }
    PettaTypecheckVerdict verdict =
        petta_check_value_body(context, value, required);
    memo->state = PETTA_MEMO_COMPLETE;
    memo->verdict = verdict;
    return verdict;
}

static bool petta_type_term_contains(
    Atom *term, Atom *needle, uint32_t depth) {
    if (!term || !needle || depth > 2048u)
        return false;
    if (atom_eq(term, needle))
        return true;
    if (term->kind != ATOM_EXPR)
        return false;
    for (CettaExprIndex index = 0u; index < term->expr.len; index++) {
        if (petta_type_term_contains(
                term->expr.elems[index], needle, depth + 1u)) {
            return true;
        }
    }
    return false;
}

bool petta_typecheck_type_has_runtime_classifier(
    Space *space, Atom *required) {
    if (!space || !required || required->kind != ATOM_SYMBOL)
        return false;
    for (uint32_t attempt = 0u; attempt < 2u; attempt++) {
        SpaceEquationCursor cursor;
        if (!space_equation_cursor_init(
                space, g_builtin_syms.get_type, &cursor)) {
            return false;
        }
        for (;;) {
            SpaceEquationOccurrenceId id;
            SpaceEquationCursorStep step =
                space_equation_cursor_next(&cursor, &id);
            if (step == SPACE_EQUATION_CURSOR_END)
                return false;
            if (step == SPACE_EQUATION_CURSOR_INVALIDATED)
                break;
            SpaceEquationOccurrence occurrence;
            if (!space_equation_occurrence_resolve(id, &occurrence))
                break;
            Atom *lhs = occurrence.lhs;
            if (!lhs || lhs->kind != ATOM_EXPR ||
                lhs->expr.len != 2u ||
                !atom_is_symbol_id(
                    lhs->expr.elems[0], g_builtin_syms.get_type)) {
                continue;
            }
            if (petta_type_term_contains(
                    occurrence.rhs, required, 0u)) {
                return true;
            }
        }
    }
    /* A concurrently changing authority is not evidence of absence.  The
     * relational get-type path will adjudicate the concrete value. */
    return true;
}

bool petta_typecheck_value(
    Space *space, Arena *arena, Atom *value, Atom *required,
    const PettaTypecheckHooks *hooks, PettaTypecheckResult *result) {
    if (!result)
        return false;
    *result = (PettaTypecheckResult){
        .verdict = PETTA_TYPECHECK_UNDETERMINED,
        .reason = PETTA_TYPECHECK_REASON_NONE,
        .fault = PETTA_TYPECHECK_FAULT_NONE,
    };
    if (!space || !arena || !value || !required) {
        result->fault = PETTA_TYPECHECK_FAULT_INVALID_ARGUMENT;
        return false;
    }
    PettaTypecheckContext context = {
        .space = space,
        .arena = arena,
        .hooks = hooks,
    };
    result->verdict = petta_check_value(&context, value, required);
    result->reason = context.reason;
    result->fault = context.fault;
    result->declaration_lookup_cost = context.declaration_lookup_cost;
    free(context.memo);
    return result->fault == PETTA_TYPECHECK_FAULT_NONE;
}

bool petta_typecheck_types(
    Space *space, Arena *arena, Atom *actual, Atom *required,
    PettaTypecheckResult *result) {
    if (!result)
        return false;
    *result = (PettaTypecheckResult){
        .verdict = PETTA_TYPECHECK_UNDETERMINED,
        .reason = PETTA_TYPECHECK_REASON_NONE,
        .fault = PETTA_TYPECHECK_FAULT_NONE,
    };
    if (!space || !arena || !actual || !required) {
        result->fault = PETTA_TYPECHECK_FAULT_INVALID_ARGUMENT;
        return false;
    }
    PettaTypecheckContext context = {
        .space = space,
        .arena = arena,
    };
    result->verdict = petta_check_types(&context, actual, required);
    result->reason = context.reason;
    result->fault = context.fault;
    result->declaration_lookup_cost = context.declaration_lookup_cost;
    free(context.memo);
    return result->fault == PETTA_TYPECHECK_FAULT_NONE;
}

#define PETTA_DECL_INDEX_NONE SIZE_MAX

typedef struct {
    Atom *subject;
    Atom *type;
    bool inferred;
    size_t next_same_subject;
    size_t prev_same_subject;
} PettaBlockDeclaration;

typedef struct {
    SymbolId subject;
    size_t first;
    size_t last;
    bool occupied;
} PettaBlockDeclarationBucket;

typedef struct {
    SymbolId subject;
    Atom **types;
    uint32_t count;
    bool occupied;
} PettaBlockDeclaredTypeMemo;

typedef struct {
    SymbolId head;
    CettaExprLen arity;
    Atom **equations;
    size_t equation_len;
} PettaBlockRelationView;

typedef struct {
    PettaProgram *program;
    Space *space;
    Registry *registry;
    const CettaNikDirectAuthorityStampV1 *authority;
    Arena scratch;
    Atom *const *forms;
    size_t form_count;
    size_t form_check_start;
    bool forms_include_live_equations;
    PettaBlockDeclaration *declarations;
    size_t declaration_len;
    size_t declaration_cap;
    PettaBlockDeclarationBucket *declaration_buckets;
    size_t declaration_bucket_cap;
    size_t declaration_bucket_used;
    PettaBlockDeclaredTypeMemo *declared_type_memo;
    size_t declared_type_memo_cap;
    size_t declared_type_memo_used;
    PettaBlockRelationView *relation_views;
    size_t relation_view_len;
    size_t relation_view_cap;
    PettaTypecheckPolicy policy;
    PettaTypecheckBlockResult *result;
    bool definite_mismatch;
    struct {
        SymbolId head;
        CettaExprLen arity;
    } effect_stack[128];
    size_t effect_stack_len;
    struct {
        SymbolId head;
        CettaExprLen arity;
    } bound_bool_stack[128];
    size_t bound_bool_stack_len;
    VarId bound_bool_parameters[128];
    size_t bound_bool_parameter_len;
    struct {
        SymbolId head;
        CettaExprLen arity;
    } proper_list_stack[128];
    size_t proper_list_stack_len;
    Atom *nonempty_values[64];
    size_t nonempty_value_len;
    VarId *dynamic_pattern_vars;
    size_t dynamic_pattern_var_len;
    size_t dynamic_pattern_var_cap;
    VarId *inference_tainted_vars;
    size_t inference_tainted_var_len;
    size_t inference_tainted_var_cap;
    bool inferring_signature;
    uint32_t explicit_ascription_depth;
    bool residual_type_guard_required;
    bool committed_effect_evidence_withheld;
} PettaBlockCheck;

static bool petta_block_inferred_signatures_current(
    PettaProgram *program, Space *space,
    const CettaNikDirectAuthorityStampV1 *authority) {
    return authority
        ? petta_program_inferred_signatures_current_under_authority(
              program, space, authority)
        : petta_program_inferred_signatures_current(program, space);
}

static bool petta_block_inferred_signature_lookup(
    PettaBlockCheck *check, SymbolId head, CettaExprLen arity,
    Atom **signature_out) {
    return check->authority
        ? petta_program_inferred_signature_lookup_under_authority(
              check->program, check->space, check->authority,
              head, arity, &check->scratch, signature_out)
        : petta_program_inferred_signature_lookup(
              check->program, check->space, head, arity,
              &check->scratch, signature_out);
}

static bool petta_block_inferred_signatures_lookup(
    PettaBlockCheck *check, SymbolId head, CettaExprLen arity,
    Atom ***signatures_out, size_t *count_out) {
    return check->authority
        ? petta_program_inferred_signatures_lookup_under_authority(
              check->program, check->space, check->authority,
              head, arity, &check->scratch,
              signatures_out, count_out)
        : petta_program_inferred_signatures_lookup(
              check->program, check->space, head, arity,
              &check->scratch, signatures_out, count_out);
}

static void petta_block_inferred_signatures_reset(
    PettaProgram *program, Space *space,
    const CettaNikDirectAuthorityStampV1 *authority) {
    if (authority) {
        petta_program_inferred_signatures_reset_under_authority(
            program, space, authority);
    } else {
        petta_program_inferred_signatures_reset(program, space);
    }
}

static bool petta_block_inferred_signature_put(
    PettaProgram *program, Space *space,
    const CettaNikDirectAuthorityStampV1 *authority,
    SymbolId head, CettaExprLen arity, Atom *signature) {
    return authority
        ? petta_program_inferred_signature_put_under_authority(
              program, space, authority, head, arity, signature)
        : petta_program_inferred_signature_put(
              program, space, head, arity, signature);
}

static bool petta_block_fault(
    PettaBlockCheck *check, PettaTypecheckFault fault,
    const char *diagnostic);

static bool petta_block_reserve(
    void **items, size_t *capacity, size_t needed, size_t width) {
    if (needed <= *capacity)
        return true;
    if (width == 0u || needed > SIZE_MAX / width)
        return false;
    size_t next = *capacity ? *capacity : 16u;
    while (next < needed) {
        if (next > SIZE_MAX / 2u) {
            next = needed;
            break;
        }
        next *= 2u;
    }
    if (next > SIZE_MAX / width)
        return false;
    *items = *items
        ? cetta_realloc(*items, width * next)
        : cetta_malloc(width * next);
    *capacity = next;
    return true;
}

static bool petta_block_note_inference_taint(
    PettaBlockCheck *check, VarId variable) {
    if (!check)
        return false;
    for (size_t index = 0u;
         index < check->inference_tainted_var_len; index++) {
        if (check->inference_tainted_vars[index] == variable)
            return true;
    }
    if (!petta_block_reserve(
            (void **)&check->inference_tainted_vars,
            &check->inference_tainted_var_cap,
            check->inference_tainted_var_len + 1u,
            sizeof(*check->inference_tainted_vars))) {
        return petta_block_fault(
            check, PETTA_TYPECHECK_FAULT_ALLOCATION,
            "could not record tainted inference parameter");
    }
    check->inference_tainted_vars[
        check->inference_tainted_var_len++] = variable;
    return true;
}

static bool petta_block_inference_parameter_tainted(
    PettaBlockCheck *check, Atom *pattern) {
    if (!check || !pattern || pattern->kind != ATOM_VAR)
        return false;
    for (size_t index = 0u;
         index < check->inference_tainted_var_len; index++) {
        if (check->inference_tainted_vars[index] == pattern->var_id)
            return true;
    }
    return false;
}

static const char *petta_block_symbol_name(const Atom *atom) {
    return atom && atom->kind == ATOM_SYMBOL && g_symbols
        ? symbol_bytes(g_symbols, atom->sym_id) : NULL;
}

static bool petta_block_head_is(const Atom *atom, const char *name) {
    if (!atom || atom->kind != ATOM_EXPR || atom->expr.len == 0u)
        return false;
    const char *head = petta_block_symbol_name(atom->expr.elems[0]);
    return head && strcmp(head, name) == 0;
}

/*
 * Arrow types are prefix expressions.  An arrow atom below an expression's
 * head is therefore the abandoned infix spelling, while an arrow-headed
 * child is a valid higher-order type.  Recurse through every child expression
 * so the same rule applies compositionally at arbitrary nesting depth.
 */
static bool petta_block_infix_arrow_misuse(
    const Atom *type, uint32_t depth) {
    if (!type || type->kind != ATOM_EXPR)
        return false;
    if (depth > 2048u)
        return true;
    for (CettaExprIndex index = 1u; index < type->expr.len; index++) {
        const Atom *child = type->expr.elems[index];
        const char *name = petta_block_symbol_name(child);
        if ((name && petta_type_arrow_symbol_name(name)) ||
            petta_block_infix_arrow_misuse(child, depth + 1u)) {
            return true;
        }
    }
    return type->expr.len > 0u &&
           petta_block_infix_arrow_misuse(
               type->expr.elems[0], depth + 1u);
}

static bool petta_block_fail(
    PettaBlockCheck *check, const char *format, ...) {
    if (!check || !check->result)
        return false;
    check->result->verdict = PETTA_TYPECHECK_REFUTED;
    va_list args;
    va_start(args, format);
    vsnprintf(
        check->result->diagnostic,
        sizeof(check->result->diagnostic), format, args);
    va_end(args);
    return false;
}

static bool petta_block_fault(
    PettaBlockCheck *check, PettaTypecheckFault fault,
    const char *diagnostic) {
    if (!check || !check->result)
        return false;
    check->result->fault = fault;
    check->result->verdict = PETTA_TYPECHECK_UNDETERMINED;
    snprintf(
        check->result->diagnostic,
        sizeof(check->result->diagnostic), "%s", diagnostic);
    return false;
}

static uint32_t petta_block_declaration_hash(SymbolId subject) {
    uint32_t hash = subject * 0x9e3779b1u;
    hash ^= hash >> 16u;
    hash *= 0x7feb352du;
    hash ^= hash >> 15u;
    return hash ? hash : 1u;
}

static bool petta_block_declaration_index_rehash(
    PettaBlockCheck *check, size_t new_cap) {
    if (!check || new_cap < 16u || (new_cap & (new_cap - 1u)) != 0u ||
        new_cap > SIZE_MAX / sizeof(*check->declaration_buckets)) {
        return false;
    }
    PettaBlockDeclarationBucket *next =
        cetta_malloc(new_cap * sizeof(*next));
    if (!next)
        return false;
    memset(next, 0, new_cap * sizeof(*next));
    for (size_t index = 0u; index < check->declaration_bucket_cap; index++) {
        PettaBlockDeclarationBucket bucket = check->declaration_buckets[index];
        if (!bucket.occupied)
            continue;
        size_t slot = (size_t)petta_block_declaration_hash(bucket.subject) &
                      (new_cap - 1u);
        while (next[slot].occupied)
            slot = (slot + 1u) & (new_cap - 1u);
        next[slot] = bucket;
    }
    free(check->declaration_buckets);
    check->declaration_buckets = next;
    check->declaration_bucket_cap = new_cap;
    return true;
}

static bool petta_block_declaration_index_prepare(PettaBlockCheck *check) {
    if (!check)
        return false;
    if (check->declaration_bucket_cap == 0u)
        return petta_block_declaration_index_rehash(check, 64u);
    if ((check->declaration_bucket_used + 1u) * 10u >=
        check->declaration_bucket_cap * 7u) {
        if (check->declaration_bucket_cap > SIZE_MAX / 2u)
            return false;
        return petta_block_declaration_index_rehash(
            check, check->declaration_bucket_cap * 2u);
    }
    return true;
}

static PettaBlockDeclarationBucket *petta_block_declaration_bucket(
    PettaBlockCheck *check, SymbolId subject, bool create) {
    if (!check || subject == SYMBOL_ID_NONE)
        return NULL;
    if (create && !petta_block_declaration_index_prepare(check))
        return NULL;
    if (check->declaration_bucket_cap == 0u)
        return NULL;
    size_t slot = (size_t)petta_block_declaration_hash(subject) &
                  (check->declaration_bucket_cap - 1u);
    for (;;) {
        PettaBlockDeclarationBucket *bucket =
            &check->declaration_buckets[slot];
        if (!bucket->occupied) {
            if (!create)
                return NULL;
            *bucket = (PettaBlockDeclarationBucket){
                .subject = subject,
                .first = PETTA_DECL_INDEX_NONE,
                .last = PETTA_DECL_INDEX_NONE,
                .occupied = true,
            };
            check->declaration_bucket_used++;
            return bucket;
        }
        if (bucket->subject == subject)
            return bucket;
        slot = (slot + 1u) & (check->declaration_bucket_cap - 1u);
    }
}

static const PettaBlockDeclarationBucket *petta_block_declaration_bucket_const(
    const PettaBlockCheck *check, const Atom *subject) {
    if (!check || !subject || subject->kind != ATOM_SYMBOL ||
        subject->sym_id == SYMBOL_ID_NONE || check->declaration_bucket_cap == 0u) {
        return NULL;
    }
    size_t slot = (size_t)petta_block_declaration_hash(subject->sym_id) &
                  (check->declaration_bucket_cap - 1u);
    for (;;) {
        const PettaBlockDeclarationBucket *bucket =
            &check->declaration_buckets[slot];
        if (!bucket->occupied)
            return NULL;
        if (bucket->subject == subject->sym_id)
            return bucket;
        slot = (slot + 1u) & (check->declaration_bucket_cap - 1u);
    }
}

static bool petta_block_declared_type_memo_rehash(
    PettaBlockCheck *check, size_t new_cap) {
    if (!check || new_cap < 16u || (new_cap & (new_cap - 1u)) != 0u ||
        new_cap > SIZE_MAX / sizeof(*check->declared_type_memo)) {
        return false;
    }
    PettaBlockDeclaredTypeMemo *next =
        cetta_malloc(new_cap * sizeof(*next));
    if (!next)
        return false;
    memset(next, 0, new_cap * sizeof(*next));
    for (size_t index = 0u;
         index < check->declared_type_memo_cap; index++) {
        PettaBlockDeclaredTypeMemo entry =
            check->declared_type_memo[index];
        if (!entry.occupied)
            continue;
        size_t slot =
            (size_t)petta_block_declaration_hash(entry.subject) &
            (new_cap - 1u);
        while (next[slot].occupied)
            slot = (slot + 1u) & (new_cap - 1u);
        next[slot] = entry;
    }
    free(check->declared_type_memo);
    check->declared_type_memo = next;
    check->declared_type_memo_cap = new_cap;
    return true;
}

static PettaBlockDeclaredTypeMemo *petta_block_declared_type_memo_entry(
    PettaBlockCheck *check, SymbolId subject, bool create) {
    if (!check || subject == SYMBOL_ID_NONE)
        return NULL;
    if (create &&
        (check->declared_type_memo_cap == 0u ||
         (check->declared_type_memo_used + 1u) * 10u >=
             check->declared_type_memo_cap * 7u)) {
        size_t new_cap = check->declared_type_memo_cap
            ? check->declared_type_memo_cap * 2u : 64u;
        if (new_cap < check->declared_type_memo_cap ||
            !petta_block_declared_type_memo_rehash(check, new_cap)) {
            return NULL;
        }
    }
    if (check->declared_type_memo_cap == 0u)
        return NULL;
    size_t slot =
        (size_t)petta_block_declaration_hash(subject) &
        (check->declared_type_memo_cap - 1u);
    for (;;) {
        PettaBlockDeclaredTypeMemo *entry =
            &check->declared_type_memo[slot];
        if (!entry->occupied) {
            if (!create)
                return NULL;
            *entry = (PettaBlockDeclaredTypeMemo){
                .subject = subject,
                .occupied = true,
            };
            check->declared_type_memo_used++;
            return entry;
        }
        if (entry->subject == subject)
            return entry;
        slot = (slot + 1u) &
               (check->declared_type_memo_cap - 1u);
    }
}

static bool petta_block_add_declaration(
    PettaBlockCheck *check, Atom *subject, Atom *type) {
    if (!check || !subject || !type || subject->kind != ATOM_SYMBOL ||
        !petta_block_reserve(
            (void **)&check->declarations,
            &check->declaration_cap,
            check->declaration_len + 1u,
            sizeof(*check->declarations))) {
        return false;
    }
    PettaBlockDeclarationBucket *bucket =
        petta_block_declaration_bucket(check, subject->sym_id, true);
    if (!bucket)
        return false;
    size_t declaration_index = check->declaration_len;
    check->declarations[declaration_index] =
        (PettaBlockDeclaration){
            .subject = subject,
            .type = type,
            .inferred = false,
            .next_same_subject = PETTA_DECL_INDEX_NONE,
            .prev_same_subject = bucket->last,
        };
    if (bucket->last != PETTA_DECL_INDEX_NONE) {
        check->declarations[bucket->last].next_same_subject = declaration_index;
    } else {
        bucket->first = declaration_index;
    }
    bucket->last = declaration_index;
    check->declaration_len++;
    return true;
}

static bool petta_block_add_inferred_declaration(
    PettaBlockCheck *check, Atom *subject, Atom *type) {
    if (!petta_block_add_declaration(check, subject, type))
        return false;
    check->declarations[check->declaration_len - 1u].inferred = true;
    return true;
}

static bool petta_block_has_declaration(
    PettaBlockCheck *check, Atom *subject, Atom *type) {
    if (!check || !subject || !type)
        return false;
    const PettaBlockDeclarationBucket *bucket =
        petta_block_declaration_bucket_const(check, subject);
    for (size_t index = bucket ? bucket->first : PETTA_DECL_INDEX_NONE;
         index != PETTA_DECL_INDEX_NONE;
         index = check->declarations[index].next_same_subject) {
        PettaBlockDeclaration *declaration = &check->declarations[index];
        if (atom_alpha_eq(declaration->type, type))
            return true;
    }
    return false;
}

static bool petta_block_arrow_head(
    const Atom *type, const char **mode_out) {
    if (mode_out)
        *mode_out = NULL;
    if (!type || type->kind != ATOM_EXPR || type->expr.len < 2u)
        return false;
    const char *head = petta_block_symbol_name(type->expr.elems[0]);
    if (!head)
        return false;
    bool arrow = petta_type_arrow_symbol_name(head);
    if (arrow && mode_out)
        *mode_out = head;
    return arrow;
}

static bool petta_block_wildcard(const Atom *type) {
    const char *name = petta_block_symbol_name(type);
    return name &&
           (strcmp(name, "%Undefined%") == 0 ||
            strcmp(name, "Atom") == 0 ||
            strcmp(name, "Expression") == 0);
}

static bool petta_block_concrete_requirement(const Atom *type) {
    return type && type->kind != ATOM_VAR && !petta_block_wildcard(type);
}

static bool petta_block_type_contains_open_evidence(
    const Atom *type, uint32_t depth) {
    if (!type || depth > 2048u)
        return true;
    if (type->kind == ATOM_VAR || petta_block_wildcard(type))
        return true;
    if (type->kind != ATOM_EXPR)
        return false;
    for (CettaExprIndex index = 0u; index < type->expr.len; index++) {
        if (petta_block_type_contains_open_evidence(
                type->expr.elems[index], depth + 1u)) {
            return true;
        }
    }
    return false;
}

static void petta_block_note_residual_guard(
    PettaBlockCheck *check, Atom *required, const char *source) {
    (void)source;
    if (check &&
        check->policy != PETTA_TYPECHECK_POLICY_DEFAULT &&
        check->explicit_ascription_depth == 0u &&
        petta_block_concrete_requirement(required)) {
        check->residual_type_guard_required = true;
    }
}

static Atom *petta_block_resolve_binding(
    Bindings *bindings, Atom *atom) {
    if (!bindings || !atom)
        return atom;
    size_t remaining = (size_t)bindings->len + 1u;
    while (atom->kind == ATOM_VAR && remaining-- > 0u) {
        Atom *next = bindings_lookup_var(bindings, atom);
        if (!next || next == atom)
            break;
        atom = next;
    }
    return atom;
}

static uint32_t petta_block_declared_types(
    PettaBlockCheck *check, Atom *subject, Atom ***types_out) {
    if (types_out)
        *types_out = NULL;
    if (!check || !subject || subject->kind != ATOM_SYMBOL || !types_out)
        return 0u;
    PettaBlockDeclaredTypeMemo *memo =
        petta_block_declared_type_memo_entry(
            check, subject->sym_id, false);
    if (!memo) {
        Atom **types = NULL;
        uint32_t count = space_get_declared_types(
            check->space, &check->scratch, subject, &types);
        Atom **program_types = NULL;
        uint32_t program_count = petta_program_declared_types(
            check->program, subject, &check->scratch, &program_types);
        size_t total = count;
        for (uint32_t index = 0u; index < program_count; index++) {
            bool duplicate = false;
            for (size_t prior = 0u; prior < total; prior++) {
                if (atom_alpha_eq(types[prior], program_types[index])) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate)
                continue;
            if (total >= UINT32_MAX) {
                free(program_types);
                free(types);
                petta_block_fault(
                    check, PETTA_TYPECHECK_FAULT_ALLOCATION,
                    "too many declared PeTTa types for one subject");
                return 0u;
            }
            types = types
                ? cetta_realloc(types, sizeof(*types) * (total + 1u))
                : cetta_malloc(sizeof(*types));
            types[total++] = program_types[index];
        }
        free(program_types);
        memo = petta_block_declared_type_memo_entry(
            check, subject->sym_id, true);
        if (!memo) {
            free(types);
            petta_block_fault(
                check, PETTA_TYPECHECK_FAULT_ALLOCATION,
                "could not memoize declared PeTTa types");
            return 0u;
        }
        memo->types = types;
        memo->count = (uint32_t)total;
    }
    if (memo->count == 0u)
        return 0u;
    Atom **fresh = cetta_malloc(sizeof(*fresh) * memo->count);
    for (uint32_t index = 0u; index < memo->count; index++) {
        fresh[index] = atom_freshen_epoch(
            &check->scratch, memo->types[index], fresh_var_suffix());
        if (!fresh[index]) {
            free(fresh);
            petta_block_fault(
                check, PETTA_TYPECHECK_FAULT_ALLOCATION,
                "could not freshen declared PeTTa type");
            return 0u;
        }
    }
    *types_out = fresh;
    return memo->count;
}

static Atom *petta_block_decl_representation(
    PettaBlockCheck *check, Atom *name, const char *kind) {
    if (!check || !name || name->kind != ATOM_SYMBOL)
        return NULL;
    const PettaBlockDeclarationBucket *bucket =
        petta_block_declaration_bucket_const(check, name);
    for (size_t index = bucket ? bucket->first : PETTA_DECL_INDEX_NONE;
         index != PETTA_DECL_INDEX_NONE;
         index = check->declarations[index].next_same_subject) {
        PettaBlockDeclaration *decl = &check->declarations[index];
        bool exclusive =
            petta_block_head_is(decl->type, "Alias") ||
            petta_block_head_is(decl->type, "Newtype") ||
            petta_block_head_is(decl->type, "SpaceOf") ||
            petta_block_head_is(decl->type, "Foreign");
        if (!exclusive)
            continue;
        return petta_block_head_is(decl->type, kind) &&
               decl->type->expr.len == 2u
            ? decl->type->expr.elems[1] : NULL;
    }
    Atom **types = NULL;
    uint32_t count = petta_block_declared_types(
        check, name, &types);
    Atom *found = NULL;
    for (uint32_t index = 0u; index < count; index++) {
        bool exclusive =
            petta_block_head_is(types[index], "Alias") ||
            petta_block_head_is(types[index], "Newtype") ||
            petta_block_head_is(types[index], "SpaceOf") ||
            petta_block_head_is(types[index], "Foreign");
        if (!exclusive)
            continue;
        if (petta_block_head_is(types[index], kind) &&
            types[index]->expr.len == 2u)
            found = types[index]->expr.elems[1];
        break;
    }
    free(types);
    return found;
}

static bool petta_block_plain_types_may_overlap(
    PettaBlockCheck *check, Atom *left, Atom *right) {
    if (!check || !left || !right || left->kind != ATOM_SYMBOL ||
        right->kind != ATOM_SYMBOL)
        return false;
    PettaLiteralSort left_sort = petta_type_literal_sort(left);
    PettaLiteralSort right_sort = petta_type_literal_sort(right);
    if (left_sort != PETTA_LITERAL_NONE &&
        right_sort != PETTA_LITERAL_NONE)
        return left_sort == right_sort;
    if (petta_block_decl_representation(check, left, "Newtype") ||
        petta_block_decl_representation(check, right, "Newtype"))
        return false;
    return true;
}

static bool petta_block_runtime_type_candidate(
    PettaBlockCheck *check, Atom *required) {
    if (!check || !required || required->kind != ATOM_SYMBOL)
        return false;
    if (petta_typecheck_type_has_runtime_classifier(
            check->space, required)) {
        return true;
    }
    for (size_t index = 0u; index < check->form_count; index++) {
        Atom *form = check->forms[index];
        if (!petta_block_head_is(form, "=") ||
            form->expr.len != 3u)
            continue;
        Atom *lhs = form->expr.elems[1];
        if (!petta_block_head_is(lhs, "get-type") ||
            lhs->expr.len != 2u)
            continue;
        if (petta_type_term_contains(
                form->expr.elems[2], required, 0u)) {
            return true;
        }
    }
    return false;
}

/* Roman's value_single_type asks whether a symbol has exactly one explicit
 * value candidate.  Function declarations participate as first-class arrow
 * values; choosing a non-arrow merely because one exists would turn an
 * ambiguous symbol into false positive evidence. */
static Atom *petta_block_unique_explicit_value_candidate_type(
    PettaBlockCheck *check, Atom *value, bool *ambiguous_out) {
    if (ambiguous_out)
        *ambiguous_out = false;
    if (!check || !value || value->kind != ATOM_SYMBOL)
        return NULL;
    Atom *found = NULL;
    const PettaBlockDeclarationBucket *bucket =
        petta_block_declaration_bucket_const(check, value);
    for (size_t index = bucket ? bucket->first : PETTA_DECL_INDEX_NONE;
         index != PETTA_DECL_INDEX_NONE;
         index = check->declarations[index].next_same_subject) {
        PettaBlockDeclaration *declaration = &check->declarations[index];
        if (declaration->inferred) {
            CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                CETTA_PETTA_TYPECHECK_CENSUS_EVENT_INFERENCE_INFERRED_VALUE_CANDIDATE_IGNORED);
            continue;
        }
        if (!found) {
            found = declaration->type;
            continue;
        }
        if (!atom_alpha_eq(found, declaration->type)) {
            if (ambiguous_out)
                *ambiguous_out = true;
            CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                CETTA_PETTA_TYPECHECK_CENSUS_EVENT_INFERENCE_VALUE_CANDIDATE_AMBIGUOUS);
            return NULL;
        }
    }
    if (found) {
        CETTA_PETTA_TYPECHECK_CENSUS_HIT(
            CETTA_PETTA_TYPECHECK_CENSUS_EVENT_INFERENCE_VALUE_CANDIDATE_SINGLETON);
    }
    return found;
}

static Atom *petta_block_declared_value_type(
    PettaBlockCheck *check, Atom *value) {
    if (!check || !value || value->kind != ATOM_SYMBOL)
        return NULL;
    Atom *found = NULL;
    const PettaBlockDeclarationBucket *bucket =
        petta_block_declaration_bucket_const(check, value);
    for (size_t index = bucket ? bucket->first : PETTA_DECL_INDEX_NONE;
         index != PETTA_DECL_INDEX_NONE;
         index = check->declarations[index].next_same_subject) {
        PettaBlockDeclaration *decl = &check->declarations[index];
        if (petta_block_arrow_head(decl->type, NULL))
            continue;
        if (found && !atom_eq(found, decl->type))
            return NULL;
        found = decl->type;
    }
    Atom **types = NULL;
    uint32_t count = petta_block_declared_types(
        check, value, &types);
    for (uint32_t index = 0u; index < count; index++) {
        if (petta_block_arrow_head(types[index], NULL))
            continue;
        if (found && !atom_eq(found, types[index])) {
            free(types);
            return NULL;
        }
        found = types[index];
    }
    free(types);
    return found;
}

static bool petta_block_type_compatible_depth(
    PettaBlockCheck *check, Atom *actual, Atom *required,
    Bindings *bindings, uint32_t depth);

static bool petta_block_type_all_actual_union(
    PettaBlockCheck *check, Atom *actual, Atom *required,
    Bindings *bindings, uint32_t depth) {
    CETTA_PETTA_TYPECHECK_CENSUS_HIT(
        CETTA_PETTA_TYPECHECK_CENSUS_EVENT_ACTUAL_UNION_ALL);
    /* Roman's actual-union law is universal through SOFT compatibility.
     * Every member must fit, but the witness substitution of one alternative
     * must not constrain another alternative: one runtime result inhabits one
     * member, not all members simultaneously.  Persisting trial bindings here
     * incorrectly pinned open required variables to the first member and then
     * rejected valid heterogeneous unions. */
    for (CettaExprIndex index = 1u; index < actual->expr.len; index++) {
        Bindings trial;
        bindings_init(&trial);
        if (!bindings_clone(&trial, bindings)) {
            bindings_free(&trial);
            return petta_block_fault(
                check, PETTA_TYPECHECK_FAULT_ALLOCATION,
                "could not clone type environment");
        }
        bool compatible = petta_block_type_compatible_depth(
            check, actual->expr.elems[index], required,
            &trial, depth + 1u);
        bindings_free(&trial);
        if (!compatible) {
            CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                CETTA_PETTA_TYPECHECK_CENSUS_EVENT_ACTUAL_UNION_MEMBER_REJECT);
            return false;
        }
    }
    CETTA_PETTA_TYPECHECK_CENSUS_HIT(
        CETTA_PETTA_TYPECHECK_CENSUS_EVENT_ACTUAL_UNION_ALL_ACCEPT);
    return true;
}

static bool petta_block_type_any_required_union(
    PettaBlockCheck *check, Atom *actual, Atom *required,
    Bindings *bindings, uint32_t depth) {
    CETTA_PETTA_TYPECHECK_CENSUS_HIT(
        CETTA_PETTA_TYPECHECK_CENSUS_EVENT_REQUIRED_UNION_ANY);
    for (CettaExprIndex index = 1u; index < required->expr.len; index++) {
        Bindings trial;
        if (!bindings_clone(&trial, bindings))
            return petta_block_fault(
                check, PETTA_TYPECHECK_FAULT_ALLOCATION,
                "could not clone type environment");
        if (petta_block_type_compatible_depth(
                check, actual, required->expr.elems[index],
                &trial, depth + 1u)) {
            bindings_replace(bindings, &trial);
            bindings_free(&trial);
            CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                CETTA_PETTA_TYPECHECK_CENSUS_EVENT_REQUIRED_UNION_MEMBER_ACCEPT);
            return true;
        }
        bindings_free(&trial);
    }
    CETTA_PETTA_TYPECHECK_CENSUS_HIT(
        CETTA_PETTA_TYPECHECK_CENSUS_EVENT_REQUIRED_UNION_NONE_ACCEPT);
    return false;
}

/* An explicit ascription narrows a possible runtime value.  Therefore an
 * actual union needs one compatible member, rather than the all-members rule
 * used when an unguarded value is promised to satisfy a required type. */
static bool petta_block_type_may_overlap(
    PettaBlockCheck *check, Atom *actual, Atom *ascribed,
    Bindings *bindings, uint32_t depth) {
    if (!check || !actual || !ascribed || !bindings || depth > 2048u)
        return false;
    actual = petta_block_resolve_binding(bindings, actual);
    ascribed = petta_block_resolve_binding(bindings, ascribed);

    /* `the` is an explicit run-time narrowing operation.  Alias transparency
     * must therefore be established before applying the ordinary directional
     * compatibility judgment: an alias whose representation is a union may
     * overlap one branch even though the unguarded value judgment correctly
     * requires every actual-side branch to fit.  Keep Newtype opaque here;
     * contextual brand acquisition is handled by the dedicated newtype path. */
    Atom *actual_alias = petta_block_decl_representation(
        check, actual, "Alias");
    if (actual_alias) {
        CETTA_PETTA_TYPECHECK_CENSUS_HIT(
            CETTA_PETTA_TYPECHECK_CENSUS_EVENT_ASCRIPTION_ALIAS_LEFT);
        return petta_block_type_may_overlap(
            check, actual_alias, ascribed, bindings, depth + 1u);
    }
    Atom *ascribed_alias = petta_block_decl_representation(
        check, ascribed, "Alias");
    if (ascribed_alias) {
        CETTA_PETTA_TYPECHECK_CENSUS_HIT(
            CETTA_PETTA_TYPECHECK_CENSUS_EVENT_ASCRIPTION_ALIAS_RIGHT);
        return petta_block_type_may_overlap(
            check, actual, ascribed_alias, bindings, depth + 1u);
    }

    if (petta_block_head_is(actual, "|") &&
        actual->expr.len > 1u) {
        for (CettaExprIndex index = 1u;
             index < actual->expr.len; index++) {
            Bindings trial;
            bindings_init(&trial);
            if (!bindings_clone(&trial, bindings)) {
                bindings_free(&trial);
                return petta_block_fault(
                    check, PETTA_TYPECHECK_FAULT_ALLOCATION,
                    "could not clone ascription type environment");
            }
            bool overlaps = petta_block_type_may_overlap(
                check, actual->expr.elems[index], ascribed,
                &trial, depth + 1u);
            if (overlaps)
                bindings_replace(bindings, &trial);
            bindings_free(&trial);
            if (overlaps) {
                CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                    CETTA_PETTA_TYPECHECK_CENSUS_EVENT_ASCRIPTION_ACTUAL_UNION_ACCEPT);
                return true;
            }
        }
        CETTA_PETTA_TYPECHECK_CENSUS_HIT(
            CETTA_PETTA_TYPECHECK_CENSUS_EVENT_ASCRIPTION_ACTUAL_UNION_REJECT);
        return false;
    }
    if (petta_block_head_is(ascribed, "|") &&
        ascribed->expr.len > 1u) {
        for (CettaExprIndex index = 1u;
             index < ascribed->expr.len; index++) {
            Bindings trial;
            bindings_init(&trial);
            if (!bindings_clone(&trial, bindings)) {
                bindings_free(&trial);
                return petta_block_fault(
                    check, PETTA_TYPECHECK_FAULT_ALLOCATION,
                    "could not clone ascription type environment");
            }
            bool overlaps = petta_block_type_may_overlap(
                check, actual, ascribed->expr.elems[index],
                &trial, depth + 1u);
            if (overlaps)
                bindings_replace(bindings, &trial);
            bindings_free(&trial);
            if (overlaps) {
                CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                    CETTA_PETTA_TYPECHECK_CENSUS_EVENT_ASCRIPTION_REQUIRED_UNION_ACCEPT);
                return true;
            }
        }
        CETTA_PETTA_TYPECHECK_CENSUS_HIT(
            CETTA_PETTA_TYPECHECK_CENSUS_EVENT_ASCRIPTION_REQUIRED_UNION_REJECT);
        return false;
    }

    /* Overlap is symmetric once aliases and unions have been exposed.  The
     * ordinary compatibility relation remains intentionally directional and
     * continues to enforce Roman's all-actual/any-required union law. */
    Bindings forward;
    bindings_init(&forward);
    if (!bindings_clone(&forward, bindings)) {
        bindings_free(&forward);
        return petta_block_fault(
            check, PETTA_TYPECHECK_FAULT_ALLOCATION,
            "could not clone ascription type environment");
    }
    if (petta_block_type_compatible_depth(
            check, actual, ascribed, &forward, depth + 1u)) {
        bindings_replace(bindings, &forward);
        bindings_free(&forward);
        CETTA_PETTA_TYPECHECK_CENSUS_HIT(
            CETTA_PETTA_TYPECHECK_CENSUS_EVENT_ASCRIPTION_FORWARD_ACCEPT);
        return true;
    }
    bindings_free(&forward);

    Bindings reverse;
    bindings_init(&reverse);
    if (!bindings_clone(&reverse, bindings)) {
        bindings_free(&reverse);
        return petta_block_fault(
            check, PETTA_TYPECHECK_FAULT_ALLOCATION,
            "could not clone ascription type environment");
    }
    bool overlaps = petta_block_type_compatible_depth(
        check, ascribed, actual, &reverse, depth + 1u);
    if (overlaps)
        bindings_replace(bindings, &reverse);
    bindings_free(&reverse);
    if (overlaps) {
        CETTA_PETTA_TYPECHECK_CENSUS_HIT(
            CETTA_PETTA_TYPECHECK_CENSUS_EVENT_ASCRIPTION_REVERSE_ACCEPT);
    } else {
        CETTA_PETTA_TYPECHECK_CENSUS_HIT(
            CETTA_PETTA_TYPECHECK_CENSUS_EVENT_ASCRIPTION_NO_OVERLAP);
    }
    return overlaps;
}

static bool petta_block_type_compatible_depth(
    PettaBlockCheck *check, Atom *actual, Atom *required,
    Bindings *bindings, uint32_t depth) {
    if (!check || !actual || !required || !bindings || depth > 2048u)
        return false;
    actual = petta_block_resolve_binding(bindings, actual);
    required = petta_block_resolve_binding(bindings, required);

    /* A type variable is trivially a member of a required union that already
     * contains that very variable.  Attempting to encode this by binding
     * $a := (| $a T) would create a cyclic type and is rejected by CeTTa's
     * occurs check, although Roman's soft Prolog compatibility succeeds. */
    if (actual->kind == ATOM_VAR &&
        petta_block_head_is(required, "|") &&
        required->expr.len > 1u) {
        for (CettaExprIndex index = 1u;
             index < required->expr.len; index++) {
            Atom *member = petta_block_resolve_binding(
                bindings, required->expr.elems[index]);
            if (member && member->kind == ATOM_VAR &&
                member->var_id == actual->var_id) {
                CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                    CETTA_PETTA_TYPECHECK_CENSUS_EVENT_TYPE_VAR_ACTUAL_UNION_SELF);
                /* Roman's soft compatibility accepts this self-membership,
                 * but the open required union still needs a residual guard.
                 * Strict modes reject that boundary rather than treating the
                 * rational-tree witness as closed static evidence. */
                petta_block_note_residual_guard(
                    check, required, "type-variable-union-self");
                return true;
            }
        }
    }
    if (required->kind == ATOM_VAR &&
        petta_block_head_is(actual, "|") &&
        actual->expr.len > 1u) {
        for (CettaExprIndex index = 1u;
             index < actual->expr.len; index++) {
            Atom *member = petta_block_resolve_binding(
                bindings, actual->expr.elems[index]);
            if (member && member->kind == ATOM_VAR &&
                member->var_id == required->var_id) {
                CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                    CETTA_PETTA_TYPECHECK_CENSUS_EVENT_TYPE_VAR_REQUIRED_UNION_SELF);
                return true;
            }
        }
    }
    if (actual->kind == ATOM_VAR) {
        if (required->kind == ATOM_VAR &&
            actual->var_id == required->var_id) {
            CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                CETTA_PETTA_TYPECHECK_CENSUS_EVENT_TYPE_VAR_IDENTITY);
            return true;
        }
        bool bound = bindings_add_var_acyclic(bindings, actual, required);
        CETTA_PETTA_TYPECHECK_CENSUS_HIT(
            bound
                ? CETTA_PETTA_TYPECHECK_CENSUS_EVENT_TYPE_VAR_ACTUAL_BIND_ACCEPT
                : CETTA_PETTA_TYPECHECK_CENSUS_EVENT_TYPE_VAR_ACTUAL_BIND_REJECT);
        return bound;
    }
    if (required->kind == ATOM_VAR) {
        bool bound = bindings_add_var_acyclic(bindings, required, actual);
        CETTA_PETTA_TYPECHECK_CENSUS_HIT(
            bound
                ? CETTA_PETTA_TYPECHECK_CENSUS_EVENT_TYPE_VAR_REQUIRED_BIND_ACCEPT
                : CETTA_PETTA_TYPECHECK_CENSUS_EVENT_TYPE_VAR_REQUIRED_BIND_REJECT);
        return bound;
    }

    Atom *actual_newtype = petta_block_decl_representation(
        check, actual, "Newtype");
    Atom *required_newtype = petta_block_decl_representation(
        check, required, "Newtype");
    if (actual_newtype || required_newtype) {
        if (actual_newtype && petta_block_head_is(required, "|")) {
            CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                CETTA_PETTA_TYPECHECK_CENSUS_EVENT_NEWTYPE_ACTUAL_REQUIRED_UNION);
            return petta_block_type_any_required_union(
                check, actual, required, bindings, depth);
        }
        if (actual_newtype && required_newtype) {
            bool identical = atom_eq(actual, required);
            CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                identical
                    ? CETTA_PETTA_TYPECHECK_CENSUS_EVENT_NEWTYPE_IDENTITY_ACCEPT
                    : CETTA_PETTA_TYPECHECK_CENSUS_EVENT_NEWTYPE_DISTINCT_REJECT);
            return identical;
        }
        if (required_newtype) {
            if (petta_block_head_is(actual, "|")) {
                CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                    CETTA_PETTA_TYPECHECK_CENSUS_EVENT_NEWTYPE_ACTUAL_UNION_REQUIRED);
                return petta_block_type_all_actual_union(
                    check, actual, required, bindings, depth);
            }
            CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                CETTA_PETTA_TYPECHECK_CENSUS_EVENT_NEWTYPE_REQUIRED_REJECT);
            return false;
        }
        if (petta_block_wildcard(required)) {
            CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                CETTA_PETTA_TYPECHECK_CENSUS_EVENT_NEWTYPE_ACTUAL_WILDCARD_ACCEPT);
            return true;
        }
        if (!actual_newtype || petta_block_wildcard(actual_newtype)) {
            CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                CETTA_PETTA_TYPECHECK_CENSUS_EVENT_NEWTYPE_WILDCARD_REPRESENTATION_REJECT);
            return false;
        }
        bool representation_fits = petta_block_type_compatible_depth(
            check, actual_newtype, required, bindings, depth + 1u);
        CETTA_PETTA_TYPECHECK_CENSUS_HIT(
            representation_fits
                ? CETTA_PETTA_TYPECHECK_CENSUS_EVENT_NEWTYPE_REPRESENTATION_ACCEPT
                : CETTA_PETTA_TYPECHECK_CENSUS_EVENT_NEWTYPE_REPRESENTATION_REJECT);
        return representation_fits;
    }

    Atom *actual_alias = petta_block_decl_representation(
        check, actual, "Alias");
    Atom *required_alias = petta_block_decl_representation(
        check, required, "Alias");
    if (actual_alias) {
        CETTA_PETTA_TYPECHECK_CENSUS_HIT(
            CETTA_PETTA_TYPECHECK_CENSUS_EVENT_ACTUAL_ALIAS_EXPAND);
        return petta_block_type_compatible_depth(
            check, actual_alias, required, bindings, depth + 1u);
    }
    if (required_alias) {
        CETTA_PETTA_TYPECHECK_CENSUS_HIT(
            CETTA_PETTA_TYPECHECK_CENSUS_EVENT_REQUIRED_ALIAS_EXPAND);
        return petta_block_type_compatible_depth(
            check, actual, required_alias, bindings, depth + 1u);
    }
    if (petta_block_wildcard(actual)) {
        CETTA_PETTA_TYPECHECK_CENSUS_HIT(
            CETTA_PETTA_TYPECHECK_CENSUS_EVENT_WILDCARD_ACTUAL_ACCEPT);
        return true;
    }
    if (petta_block_wildcard(required)) {
        CETTA_PETTA_TYPECHECK_CENSUS_HIT(
            CETTA_PETTA_TYPECHECK_CENSUS_EVENT_WILDCARD_REQUIRED_ACCEPT);
        return true;
    }
    if (petta_block_head_is(actual, "|"))
        return petta_block_type_all_actual_union(
            check, actual, required, bindings, depth);
    if (petta_block_head_is(required, "|"))
        return petta_block_type_any_required_union(
            check, actual, required, bindings, depth);

    const char *actual_mode = NULL;
    const char *required_mode = NULL;
    bool actual_arrow = petta_block_arrow_head(actual, &actual_mode);
    bool required_arrow = petta_block_arrow_head(required, &required_mode);
    if (actual_arrow || required_arrow) {
        if (!actual_arrow || !required_arrow ||
            actual->expr.len != required->expr.len) {
            CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                CETTA_PETTA_TYPECHECK_CENSUS_EVENT_ARROW_SHAPE_REJECT);
            return false;
        }
        if (!petta_type_arrow_mode_fits(actual_mode, required_mode))
            return false;
        for (CettaExprIndex index = 1u;
             index < actual->expr.len; index++) {
            if (!petta_block_type_compatible_depth(
                    check, actual->expr.elems[index],
                    required->expr.elems[index], bindings,
                    depth + 1u)) {
                CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                    CETTA_PETTA_TYPECHECK_CENSUS_EVENT_ARROW_COMPONENT_REJECT);
                return false;
            }
        }
        CETTA_PETTA_TYPECHECK_CENSUS_HIT(
            CETTA_PETTA_TYPECHECK_CENSUS_EVENT_ARROW_COMPATIBLE);
        return true;
    }

    if (actual->kind != required->kind) {
        CETTA_PETTA_TYPECHECK_CENSUS_HIT(
            CETTA_PETTA_TYPECHECK_CENSUS_EVENT_STRUCTURAL_KIND_REJECT);
        return false;
    }
    if (actual->kind != ATOM_EXPR) {
        bool identical = atom_eq(actual, required);
        CETTA_PETTA_TYPECHECK_CENSUS_HIT(
            identical
                ? CETTA_PETTA_TYPECHECK_CENSUS_EVENT_STRUCTURAL_ATOM_EXACT
                : CETTA_PETTA_TYPECHECK_CENSUS_EVENT_STRUCTURAL_ATOM_REJECT);
        return identical;
    }
    if (actual->expr.len != required->expr.len) {
        CETTA_PETTA_TYPECHECK_CENSUS_HIT(
            CETTA_PETTA_TYPECHECK_CENSUS_EVENT_STRUCTURAL_ARITY_REJECT);
        return false;
    }
    for (CettaExprIndex index = 0u;
         index < actual->expr.len; index++) {
        if (!petta_block_type_compatible_depth(
                check, actual->expr.elems[index],
                required->expr.elems[index], bindings,
                depth + 1u)) {
            CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                CETTA_PETTA_TYPECHECK_CENSUS_EVENT_STRUCTURAL_COMPONENT_REJECT);
            return false;
        }
    }
    CETTA_PETTA_TYPECHECK_CENSUS_HIT(
        CETTA_PETTA_TYPECHECK_CENSUS_EVENT_STRUCTURAL_COMPATIBLE);
    return true;
}

static bool petta_block_type_compatible(
    PettaBlockCheck *check, Atom *actual, Atom *required,
    Bindings *bindings);

typedef struct {
    VarId variable;
    Atom *value;
} PettaRationalTypeBinding;

typedef struct {
    Atom *left;
    Atom *right;
} PettaRationalTypePair;

typedef struct {
    PettaBlockCheck *check;
    const Bindings *base;
    PettaRationalTypeBinding *bindings;
    size_t binding_len;
    size_t binding_cap;
    PettaRationalTypePair *pending;
    size_t pending_len;
    size_t pending_cap;
    PettaRationalTypePair *seen;
    size_t seen_len;
    size_t seen_cap;
    bool saw_cycle;
    bool faulted;
} PettaRationalTypeReplay;

static Atom *petta_rational_type_lookup(
    const PettaRationalTypeReplay *replay, VarId variable) {
    for (size_t index = replay->binding_len; index > 0u; index--) {
        if (replay->bindings[index - 1u].variable == variable)
            return replay->bindings[index - 1u].value;
    }
    if (replay->base) {
        for (uint32_t index = replay->base->len; index > 0u; index--) {
            if (replay->base->entries[index - 1u].var_id == variable)
                return replay->base->entries[index - 1u].val;
        }
    }
    return NULL;
}

static Atom *petta_rational_type_dereference(
    PettaRationalTypeReplay *replay, Atom *type) {
    size_t remaining = replay->binding_len +
        (replay->base ? replay->base->len : 0u) + 1u;
    while (type && type->kind == ATOM_VAR && remaining-- > 0u) {
        Atom *next = petta_rational_type_lookup(replay, type->var_id);
        if (!next || (next->kind == ATOM_VAR &&
                      next->var_id == type->var_id)) {
            return type;
        }
        type = next;
    }
    if (type && type->kind == ATOM_VAR)
        replay->saw_cycle = true;
    return type;
}

static bool petta_rational_type_push_pair(
    PettaRationalTypeReplay *replay, Atom *left, Atom *right) {
    if (!petta_block_reserve(
            (void **)&replay->pending, &replay->pending_cap,
            replay->pending_len + 1u, sizeof(*replay->pending))) {
        replay->faulted = true;
        return petta_block_fault(
            replay->check, PETTA_TYPECHECK_FAULT_ALLOCATION,
            "could not grow rational type replay worklist");
    }
    replay->pending[replay->pending_len++] =
        (PettaRationalTypePair){.left = left, .right = right};
    return true;
}

static bool petta_rational_type_seen_pair(
    const PettaRationalTypeReplay *replay, Atom *left, Atom *right) {
    for (size_t index = 0u; index < replay->seen_len; index++) {
        PettaRationalTypePair pair = replay->seen[index];
        if ((pair.left == left && pair.right == right) ||
            (pair.left == right && pair.right == left)) {
            return true;
        }
    }
    return false;
}

static bool petta_rational_type_note_pair(
    PettaRationalTypeReplay *replay, Atom *left, Atom *right) {
    if (!petta_block_reserve(
            (void **)&replay->seen, &replay->seen_cap,
            replay->seen_len + 1u, sizeof(*replay->seen))) {
        replay->faulted = true;
        return petta_block_fault(
            replay->check, PETTA_TYPECHECK_FAULT_ALLOCATION,
            "could not grow rational type replay memo");
    }
    replay->seen[replay->seen_len++] =
        (PettaRationalTypePair){.left = left, .right = right};
    return true;
}

static int petta_rational_type_reaches_variable(
    PettaRationalTypeReplay *replay, Atom *value, VarId target) {
    Atom **pending = NULL;
    size_t pending_len = 0u;
    size_t pending_cap = 0u;
    VarId *seen = NULL;
    size_t seen_len = 0u;
    size_t seen_cap = 0u;
    if (!petta_block_reserve(
            (void **)&pending, &pending_cap, 1u, sizeof(*pending))) {
        goto fault;
    }
    pending[pending_len++] = value;
    while (pending_len > 0u) {
        Atom *current = pending[--pending_len];
        if (!current)
            continue;
        if (current->kind == ATOM_VAR) {
            if (current->var_id == target) {
                free(seen);
                free(pending);
                return 1;
            }
            bool prior = false;
            for (size_t index = 0u; index < seen_len; index++) {
                if (seen[index] == current->var_id) {
                    prior = true;
                    break;
                }
            }
            if (prior)
                continue;
            if (!petta_block_reserve(
                    (void **)&seen, &seen_cap,
                    seen_len + 1u, sizeof(*seen))) {
                goto fault;
            }
            seen[seen_len++] = current->var_id;
            Atom *next = petta_rational_type_lookup(
                replay, current->var_id);
            if (next) {
                if (!petta_block_reserve(
                        (void **)&pending, &pending_cap,
                        pending_len + 1u, sizeof(*pending))) {
                    goto fault;
                }
                pending[pending_len++] = next;
            }
            continue;
        }
        if (current->kind != ATOM_EXPR)
            continue;
        if (!petta_block_reserve(
                (void **)&pending, &pending_cap,
                pending_len + current->expr.len,
                sizeof(*pending))) {
            goto fault;
        }
        for (CettaExprIndex index = 0u;
             index < current->expr.len; index++) {
            pending[pending_len++] = current->expr.elems[index];
        }
    }
    free(seen);
    free(pending);
    return 0;

fault:
    free(seen);
    free(pending);
    replay->faulted = true;
    petta_block_fault(
        replay->check, PETTA_TYPECHECK_FAULT_ALLOCATION,
        "could not inspect rational type occurrence");
    return -1;
}

static bool petta_rational_type_bind(
    PettaRationalTypeReplay *replay, Atom *variable, Atom *value) {
    int reaches = petta_rational_type_reaches_variable(
        replay, value, variable->var_id);
    if (reaches < 0)
        return false;
    if (reaches > 0)
        replay->saw_cycle = true;
    if (!petta_block_reserve(
            (void **)&replay->bindings, &replay->binding_cap,
            replay->binding_len + 1u, sizeof(*replay->bindings))) {
        replay->faulted = true;
        return petta_block_fault(
            replay->check, PETTA_TYPECHECK_FAULT_ALLOCATION,
            "could not grow rational type substitution");
    }
    replay->bindings[replay->binding_len++] =
        (PettaRationalTypeBinding){
            .variable = variable->var_id,
            .value = value,
        };
    return true;
}

static bool petta_rational_type_node_allowed(
    PettaRationalTypeReplay *replay, Atom *type) {
    if (!type)
        return false;
    if (type->kind == ATOM_VAR)
        return true;
    if (petta_block_wildcard(type) ||
        petta_block_head_is(type, "|") ||
        petta_block_arrow_head(type, NULL)) {
        return false;
    }
    if (type->kind == ATOM_SYMBOL &&
        (petta_block_decl_representation(
             replay->check, type, "Alias") ||
         petta_block_decl_representation(
             replay->check, type, "Newtype"))) {
        return false;
    }
    return true;
}

static bool petta_block_rational_structural_cycle_compatible(
    PettaBlockCheck *check, Atom *actual, Atom *required,
    const Bindings *base) {
    if (!check || !actual || !required ||
        (!atom_has_vars(actual) && !atom_has_vars(required))) {
        return false;
    }
    PettaRationalTypeReplay replay = {
        .check = check,
        .base = base,
    };
    bool compatible = petta_rational_type_push_pair(
        &replay, actual, required);
    while (compatible && replay.pending_len > 0u) {
        PettaRationalTypePair pair =
            replay.pending[--replay.pending_len];
        Atom *left = petta_rational_type_dereference(
            &replay, pair.left);
        Atom *right = petta_rational_type_dereference(
            &replay, pair.right);
        if (!left || !right) {
            compatible = false;
            break;
        }
        if (left == right || atom_eq(left, right))
            continue;
        if (!petta_rational_type_node_allowed(&replay, left) ||
            !petta_rational_type_node_allowed(&replay, right)) {
            compatible = false;
            break;
        }
        if (petta_rational_type_seen_pair(&replay, left, right))
            continue;
        if (!petta_rational_type_note_pair(&replay, left, right)) {
            compatible = false;
            break;
        }
        if (left->kind == ATOM_VAR) {
            compatible = petta_rational_type_bind(
                &replay, left, right);
            continue;
        }
        if (right->kind == ATOM_VAR) {
            compatible = petta_rational_type_bind(
                &replay, right, left);
            continue;
        }
        if (left->kind != right->kind) {
            compatible = false;
            break;
        }
        if (left->kind != ATOM_EXPR) {
            compatible = false;
            break;
        }
        if (left->expr.len != right->expr.len) {
            compatible = false;
            break;
        }
        for (CettaExprIndex index = left->expr.len;
             index > 0u; index--) {
            CettaExprIndex child = index - 1u;
            if (!petta_rational_type_push_pair(
                    &replay, left->expr.elems[child],
                    right->expr.elems[child])) {
                compatible = false;
                break;
            }
        }
    }
    bool accepted = compatible && replay.saw_cycle && !replay.faulted;
    if (replay.saw_cycle && !replay.faulted) {
        CETTA_PETTA_TYPECHECK_CENSUS_HIT(
            accepted
                ? CETTA_PETTA_TYPECHECK_CENSUS_EVENT_RATIONAL_STRUCTURAL_CYCLE_ACCEPT
                : CETTA_PETTA_TYPECHECK_CENSUS_EVENT_RATIONAL_STRUCTURAL_CYCLE_REJECT);
    }
    free(replay.seen);
    free(replay.pending);
    free(replay.bindings);
    return accepted;
}

static bool petta_block_type_compatible(
    PettaBlockCheck *check, Atom *actual, Atom *required,
    Bindings *bindings) {
    Bindings trial;
    bindings_init(&trial);
    if (!bindings_clone(&trial, bindings)) {
        bindings_free(&trial);
        return petta_block_fault(
            check, PETTA_TYPECHECK_FAULT_ALLOCATION,
            "could not clone type compatibility environment");
    }
    bool compatible = petta_block_type_compatible_depth(
        check, actual, required, &trial, 0u);
    if (compatible)
        bindings_replace(bindings, &trial);
    bindings_free(&trial);
    if (compatible ||
        check->result->fault != PETTA_TYPECHECK_FAULT_NONE) {
        return compatible;
    }
    return petta_block_rational_structural_cycle_compatible(
        check, actual, required, bindings);
}

static Atom *petta_block_literal_type(
    PettaBlockCheck *check, Atom *value) {
    if (!check || !value)
        return NULL;
    PettaLiteralSort sort = petta_value_literal_sort(value);
    switch (sort) {
    case PETTA_LITERAL_NUMBER:
        return atom_symbol(&check->scratch, "Number");
    case PETTA_LITERAL_STRING:
        return atom_symbol(&check->scratch, "String");
    case PETTA_LITERAL_BOOL:
        return atom_symbol(&check->scratch, "Bool");
    case PETTA_LITERAL_NONE:
        return NULL;
    }
    return NULL;
}

static Atom *petta_block_apply_type_bindings(
    PettaBlockCheck *check, Bindings *bindings, Atom *type) {
    return bindings && bindings->len > 0u
        ? bindings_apply(bindings, &check->scratch, type) : type;
}

/* Non-pattern inference sites establish a variable exactly once.  Pattern
 * contexts use petta_block_env_note_pattern below, which owns Roman's
 * candidate accumulation semantics. */
static bool petta_block_env_note_fresh(
    Bindings *environment, Atom *variable, Atom *type) {
    if (bindings_lookup_var(environment, variable))
        return false;
    return bindings_add_var_acyclic(environment, variable, type);
}

/* Roman's pattern attributes accumulate candidate evidence rather than
 * intersecting repeated occurrences.  This applies uniformly to clause-head,
 * let/case, and typed match patterns.  A second variant-equal candidate adds
 * nothing.  A singleton open candidate specializes in place when the next
 * occurrence supplies its witness.  Every other distinct second candidate
 * makes the term variable non-singleton: it may still describe an unreachable
 * pattern, but it cannot be consumed later as though either candidate had
 * been established.  %Undefined% is the existing PeTTa-v2 representation of
 * that unresolved candidate multiplicity; strict boundaries turn its later
 * use into a residual guard. */
static bool petta_block_env_note_pattern(
    PettaBlockCheck *check, Bindings *environment,
    Atom *variable, Atom *type) {
    Atom *known = bindings_lookup_var(environment, variable);
    if (!known)
        return bindings_add_var_acyclic(environment, variable, type);

    Atom *resolved_known = petta_block_resolve_binding(environment, known);
    Atom *resolved_type = petta_block_resolve_binding(environment, type);
    if (resolved_known && resolved_type &&
        atom_alpha_eq(resolved_known, resolved_type)) {
        CETTA_PETTA_TYPECHECK_CENSUS_HIT(
            CETTA_PETTA_TYPECHECK_CENSUS_EVENT_PATTERN_CANDIDATE_VARIANT_REPEAT);
        return true;
    }

    if (resolved_known && resolved_known->kind == ATOM_VAR) {
        Bindings specialization;
        bindings_init(&specialization);
        if (!bindings_clone(&specialization, environment) ||
            !bindings_add_var(
                &specialization, resolved_known, resolved_type)) {
            bindings_free(&specialization);
            return petta_block_fault(
                check, PETTA_TYPECHECK_FAULT_ALLOCATION,
                "could not specialize an open pattern candidate");
        }
        if (!bindings_has_loop(&specialization)) {
            bindings_replace(environment, &specialization);
            bindings_free(&specialization);
            CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                CETTA_PETTA_TYPECHECK_CENSUS_EVENT_PATTERN_CANDIDATE_OPEN_SPECIALIZE);
            return true;
        }
        bindings_free(&specialization);
    }

    Atom *unknown = atom_symbol(&check->scratch, "%Undefined%");
    if (!unknown) {
        return petta_block_fault(
            check, PETTA_TYPECHECK_FAULT_ALLOCATION,
            "could not represent multiple pattern candidates");
    }

    for (uint32_t index = environment->len; index > 0u; index--) {
        Binding *entry = &environment->entries[index - 1u];
        if (!entry->legacy_name_fallback &&
            entry->var_id == variable->var_id) {
            entry->val = unknown;
            bindings_invalidate_after_key_rewrite(environment);
            CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                CETTA_PETTA_TYPECHECK_CENSUS_EVENT_PATTERN_CANDIDATE_AMBIGUOUS);
            return true;
        }
    }
    return petta_block_fault(
        check, PETTA_TYPECHECK_FAULT_MALFORMED_TYPE,
        "repeated pattern candidate environment is malformed");
}

static Atom *petta_block_structural_type(
    PettaBlockCheck *check, Atom *value, Bindings *environment,
    uint32_t depth);

static Atom *petta_block_pattern_type(
    PettaBlockCheck *check, Atom *pattern, Bindings *environment,
    uint32_t depth) {
    if (!check || !pattern || !environment || depth > 2048u)
        return NULL;
    if (pattern->kind == ATOM_VAR) {
        Atom *known = bindings_lookup_var(environment, pattern);
        return known ? known : atom_var_with_id(
            &check->scratch, "type", fresh_var_id());
    }
    Atom *literal = petta_block_literal_type(check, pattern);
    if (literal)
        return literal;
    if (pattern->kind == ATOM_SYMBOL)
        return atom_symbol(&check->scratch, "Atom");
    return petta_block_structural_type(
        check, pattern, environment, depth + 1u);
}

static Atom *petta_block_structural_type(
    PettaBlockCheck *check, Atom *value, Bindings *environment,
    uint32_t depth) {
    if (!value || value->kind != ATOM_EXPR ||
        value->expr.len == 0u || depth > 2048u)
        return NULL;
    Atom **elements = cetta_malloc(
        sizeof(*elements) * (size_t)value->expr.len);
    if (!elements)
        return NULL;
    /* A symbolic first field can be a structural tag.  Every other first
     * field belongs to a positional product and must be typed like the
     * remaining fields rather than copied as though it were a tag. */
    elements[0] = value->expr.elems[0]->kind == ATOM_SYMBOL
        ? value->expr.elems[0]
        : petta_block_pattern_type(
              check, value->expr.elems[0], environment,
              depth + 1u);
    bool ok = elements[0] != NULL;
    for (CettaExprIndex index = 1u;
         index < value->expr.len; index++) {
        elements[index] = petta_block_pattern_type(
            check, value->expr.elems[index], environment,
            depth + 1u);
        if (!elements[index]) {
            ok = false;
            break;
        }
    }
    Atom *type = ok
        ? atom_expr(&check->scratch, elements, value->expr.len)
        : NULL;
    free(elements);
    return type;
}

static bool petta_block_head_is_callable(
    PettaBlockCheck *check, Atom *head, CettaExprLen arity);
static bool petta_block_find_signature(
    PettaBlockCheck *check, Atom *subject, CettaExprLen arity,
    Atom **signature_out);

static bool petta_block_dynamic_pattern_var(
    const PettaBlockCheck *check, VarId variable) {
    if (!check)
        return false;
    for (size_t index = 0u;
         index < check->dynamic_pattern_var_len; index++) {
        if (check->dynamic_pattern_vars[index] == variable)
            return true;
    }
    return false;
}

static bool petta_block_var_id_present(
    const VarId *variables, size_t count, VarId variable) {
    for (size_t index = 0u; index < count; index++) {
        if (variables[index] == variable)
            return true;
    }
    return false;
}

static bool petta_block_collect_type_variables(
    Atom *type, VarId **variables, size_t *count,
    size_t *capacity, uint32_t depth) {
    if (!type || !variables || !count || !capacity || depth > 2048u)
        return false;
    if (type->kind == ATOM_VAR) {
        if (petta_block_var_id_present(*variables, *count, type->var_id))
            return true;
        if (!petta_block_reserve(
                (void **)variables, capacity, *count + 1u,
                sizeof(**variables))) {
            return false;
        }
        (*variables)[(*count)++] = type->var_id;
        return true;
    }
    if (type->kind != ATOM_EXPR)
        return true;
    for (CettaExprIndex index = 0u; index < type->expr.len; index++) {
        if (!petta_block_collect_type_variables(
                type->expr.elems[index], variables, count,
                capacity, depth + 1u)) {
            return false;
        }
    }
    return true;
}

static bool petta_block_note_dynamic_pattern_vars(
    PettaBlockCheck *check, Atom *pattern,
    Bindings *environment, uint32_t depth) {
    if (!check || !pattern || depth > 2048u)
        return false;
    if (pattern->kind == ATOM_VAR) {
        if ((environment && bindings_lookup_var(environment, pattern)) ||
            petta_block_dynamic_pattern_var(check, pattern->var_id)) {
            return true;
        }
        if (!petta_block_reserve(
                (void **)&check->dynamic_pattern_vars,
                &check->dynamic_pattern_var_cap,
                check->dynamic_pattern_var_len + 1u,
                sizeof(*check->dynamic_pattern_vars))) {
            return false;
        }
        check->dynamic_pattern_vars[
            check->dynamic_pattern_var_len++] = pattern->var_id;
        return true;
    }
    if (pattern->kind != ATOM_EXPR)
        return true;
    for (CettaExprIndex index = 0u;
         index < pattern->expr.len; index++) {
        if (!petta_block_note_dynamic_pattern_vars(
                check, pattern->expr.elems[index],
                environment, depth + 1u)) {
            return false;
        }
    }
    return true;
}

/* Does `pattern` carry shape that positively distinguishes this union
 * member?  An expression pattern discriminates a member that unfolds to a
 * product of the same arity (or to a union containing one); anything else is
 * matched only by the conservative fallback and must not win selection. */
static bool petta_block_pattern_discriminates_member(
    PettaBlockCheck *check, Atom *pattern, Atom *member, uint32_t depth) {
    if (!check || !pattern || !member || depth > 64u)
        return false;
    if (pattern->kind != ATOM_EXPR || pattern->expr.len == 0u)
        return true;
    for (uint32_t unfolded = 0u;
         member && member->kind == ATOM_SYMBOL && unfolded < 64u;
         unfolded++) {
        Atom *representation = petta_block_decl_representation(
            check, member, "Alias");
        if (!representation)
            representation = petta_block_decl_representation(
                check, member, "Newtype");
        if (!representation)
            break;
        member = representation;
    }
    if (!member || member->kind != ATOM_EXPR)
        return false;
    if (petta_block_head_is(member, "|") && member->expr.len > 1u) {
        for (CettaExprIndex index = 1u; index < member->expr.len; index++) {
            if (petta_block_pattern_discriminates_member(
                    check, pattern, member->expr.elems[index], depth + 1u))
                return true;
        }
        return false;
    }
    if (petta_block_head_is(member, "List"))
        return petta_block_head_is(pattern, "cons") ||
               pattern->expr.len == 0u;
    return member->expr.len == pattern->expr.len;
}

/* Could a value of this union member ever match `pattern`?  A member that
 * unfolds to a product is judged on arity; a bare nominal member is judged
 * on the arity of the constructors declared to produce it.  This is the
 * "may" question that governs whether narrowing is honest at all; the
 * stricter "does" question below only orders the candidates. */
static bool petta_block_pattern_may_inhabit_member(
    PettaBlockCheck *check, Atom *pattern, Atom *member, uint32_t depth) {
    if (!check || !pattern || !member || depth > 64u)
        return true;
    if (pattern->kind != ATOM_EXPR || pattern->expr.len == 0u)
        return true;
    if (petta_block_pattern_discriminates_member(
            check, pattern, member, depth))
        return true;
    if (member->kind != ATOM_SYMBOL)
        return false;
    /* Constructors declared to produce this member inhabit it. */
    for (size_t index = 0u; index < check->declaration_len; index++) {
        Atom *type = check->declarations[index].type;
        if (!petta_block_arrow_head(type, NULL) || type->expr.len < 2u)
            continue;
        Atom *result = type->expr.elems[type->expr.len - 1u];
        if (!atom_eq(result, member))
            continue;
        if (type->expr.len - 1u == pattern->expr.len)
            return true;
    }
    return false;
}

static bool petta_block_bind_pattern(
    PettaBlockCheck *check, Atom *pattern, Atom *expected,
    Bindings *environment, uint32_t depth) {
    if (!check || !pattern || !expected || !environment || depth > 2048u)
        return false;
    /* A bare variable accepts the complete slot type.  Union-member
     * selection belongs only to patterns that carry discriminating shape. */
    if (pattern->kind == ATOM_VAR)
        return petta_block_env_note_pattern(
            check, environment, pattern, expected);
    if (petta_block_head_is(pattern, "@") &&
        pattern->expr.len == 3u) {
        return petta_block_bind_pattern(
                   check, pattern->expr.elems[1], expected,
                   environment, depth + 1u) &&
               petta_block_bind_pattern(
                   check, pattern->expr.elems[2], expected,
                   environment, depth + 1u);
    }
    if (petta_block_head_is(pattern, "cons") &&
        pattern->expr.len == 3u &&
        petta_block_head_is(expected, "List") &&
        expected->expr.len == 2u) {
        return petta_block_bind_pattern(
                   check, pattern->expr.elems[1],
                   expected->expr.elems[1], environment,
                   depth + 1u) &&
               petta_block_bind_pattern(
                   check, pattern->expr.elems[2], expected,
                   environment, depth + 1u);
    }
    if (pattern->kind == ATOM_EXPR && expected->kind == ATOM_EXPR &&
        pattern->expr.len == expected->expr.len &&
        pattern->expr.len > 0u &&
        pattern->expr.elems[0]->kind == ATOM_SYMBOL &&
        expected->expr.elems[0]->kind == ATOM_SYMBOL &&
        atom_eq(pattern->expr.elems[0], expected->expr.elems[0])) {
        for (CettaExprIndex index = 1u;
             index < pattern->expr.len; index++) {
            if (!petta_block_bind_pattern(
                    check, pattern->expr.elems[index],
                    expected->expr.elems[index], environment,
                    depth + 1u)) {
                return false;
            }
        }
        return true;
    }
    if (petta_block_head_is(expected, "|") && expected->expr.len > 1u) {
        /* Union-member selection belongs to patterns that carry
         * discriminating shape, so a member the pattern actually matches is
         * chosen before one that only binds vacuously.  Without the first
         * pass an expression pattern binds against the first bare member,
         * whose conservative fallback records every field as %Undefined% and
         * reports success -- the informative member is then never tried and
         * the branch loses all of its field types. */
        for (uint32_t pass = 0u; pass < 2u; pass++) {
            if (pass == 0u) {
                /* Narrow only when exactly one member could match.  A bare
                 * nominal member is inhabited by its declared constructors,
                 * so a four-element pattern does not discriminate a
                 * four-field product from a type whose constructor also has
                 * four fields -- committing to either would be a lie about
                 * the other's fields. */
                CettaExprIndex plausible = 0u;
                for (CettaExprIndex index = 1u;
                     index < expected->expr.len; index++) {
                    if (petta_block_pattern_may_inhabit_member(
                            check, pattern, expected->expr.elems[index], 0u))
                        plausible++;
                }
                if (plausible != 1u)
                    continue;
            }
            for (CettaExprIndex index = 1u;
                 index < expected->expr.len; index++) {
                if (pass == 0u &&
                    !petta_block_pattern_discriminates_member(
                        check, pattern, expected->expr.elems[index], 0u))
                    continue;
                Bindings branch;
                bindings_init(&branch);
                if (!bindings_clone(&branch, environment)) {
                    bindings_free(&branch);
                    return false;
                }
                bool fits = petta_block_bind_pattern(
                    check, pattern, expected->expr.elems[index],
                    &branch, depth + 1u);
                if (fits) {
                    bindings_replace(environment, &branch);
                    bindings_free(&branch);
                    return true;
                }
                bindings_free(&branch);
            }
        }
        return false;
    }
    if (pattern->kind == ATOM_EXPR && pattern->expr.len > 0u &&
        pattern->expr.elems[0]->kind == ATOM_SYMBOL) {
        Atom *signature = NULL;
        if (petta_block_find_signature(
                check, pattern->expr.elems[0],
                pattern->expr.len - 1u, &signature)) {
            Atom *fresh = atom_freshen_epoch(
                &check->scratch, signature, (uint32_t)fresh_var_id());
            if (!fresh)
                return false;
            Bindings types;
            bindings_init(&types);
            Atom *result = fresh->expr.elems[fresh->expr.len - 1u];
            bool fits = petta_block_type_compatible(
                check, result, expected, &types);
            /* Newtypes are erased at runtime.  An unbranded constructor
             * result may acquire the role required by this pattern slot
             * when its declared result fits the role's representation.
             * A constructor already carrying another role stays nominal. */
            Atom *expected_representation =
                petta_block_decl_representation(
                    check, expected, "Newtype");
            Atom *result_representation =
                petta_block_decl_representation(
                    check, result, "Newtype");
            if (!fits && expected_representation &&
                !result_representation) {
                fits = petta_block_type_compatible(
                    check, result, expected_representation, &types);
            }
            for (CettaExprIndex index = 1u;
                 fits && index < pattern->expr.len; index++) {
                Atom *field_type = petta_block_apply_type_bindings(
                    check, &types, fresh->expr.elems[index]);
                fits = petta_block_bind_pattern(
                    check, pattern->expr.elems[index], field_type,
                    environment, depth + 1u);
            }
            bindings_free(&types);
            return fits;
        }
    }
    if (pattern->kind == ATOM_EXPR && expected->kind == ATOM_SYMBOL) {
        Atom *representation = petta_block_decl_representation(
            check, expected, "Alias");
        if (!representation)
            representation = petta_block_decl_representation(
                check, expected, "Newtype");
        if (representation && !petta_block_wildcard(representation)) {
            return petta_block_bind_pattern(
                check, pattern, representation,
                environment, depth + 1u);
        }
    }
    Atom *actual = petta_block_literal_type(check, pattern);
    if (!actual && pattern->kind == ATOM_SYMBOL)
        actual = atom_symbol(&check->scratch, "Atom");
    if (!actual && pattern->kind == ATOM_EXPR) {
        bool functional_pattern = pattern->expr.len > 0u &&
            petta_block_head_is_callable(
                check, pattern->expr.elems[0], pattern->expr.len - 1u);
        if (!functional_pattern && expected->kind == ATOM_EXPR &&
            pattern->expr.len == expected->expr.len) {
            CettaExprIndex first = 0u;
            if (pattern->expr.len > 0u &&
                pattern->expr.elems[0]->kind == ATOM_SYMBOL &&
                expected->expr.elems[0]->kind == ATOM_SYMBOL) {
                if (!atom_eq(
                        pattern->expr.elems[0],
                        expected->expr.elems[0])) {
                    return false;
                }
                first = 1u;
            }
            for (CettaExprIndex index = first;
                 index < pattern->expr.len; index++) {
                if (!petta_block_bind_pattern(
                        check, pattern->expr.elems[index],
                        expected->expr.elems[index],
                        environment, depth + 1u)) {
                    return false;
                }
            }
            return true;
        }

        /*
         * A functional or open pattern without a proven structural schema
         * remains conservative.  Record its visible variables, but reject
         * only when the SpaceOf projection above proves a contradiction.
         */
        for (CettaExprIndex index = 0u;
             index < pattern->expr.len; index++) {
            Atom *field = pattern->expr.elems[index];
            if (field->kind == ATOM_VAR &&
                !bindings_lookup_var(environment, field)) {
                Atom *unknown = atom_symbol(
                    &check->scratch, "%Undefined%");
                if (!unknown ||
                    !petta_block_env_note_fresh(environment, field, unknown))
                    return false;
            }
        }
        return true;
    }
    if (!actual)
        return false;
    Bindings types;
    bindings_init(&types);
    bool ok = petta_block_type_compatible(
        check, actual, expected, &types);
    Atom *expected_representation = petta_block_decl_representation(
        check, expected, "Newtype");
    Atom *actual_representation = petta_block_decl_representation(
        check, actual, "Newtype");
    if (!ok && expected_representation && !actual_representation) {
        ok = petta_block_type_compatible(
            check, actual, expected_representation, &types);
    }
    bindings_free(&types);
    return ok;
}

static bool petta_block_infer_expr(
    PettaBlockCheck *check, Atom *expression,
    Bindings *environment, Atom *expected,
    Atom **actual_out, uint32_t depth);
static Atom *petta_block_normalize_value_schema(
    PettaBlockCheck *check, Atom *type, uint32_t depth);
static bool petta_block_validate_effect_arguments(
    PettaBlockCheck *check, Atom *expression,
    Bindings *environment, uint32_t depth);
static bool petta_block_value_is_proven_proper_list(
    PettaBlockCheck *check, Atom *value,
    Bindings *environment, uint32_t depth);
static bool petta_block_find_signature(
    PettaBlockCheck *check, Atom *subject, CettaExprLen arity,
    Atom **signature_out);
static Atom *petta_block_unary_type(
    PettaBlockCheck *check, const char *head, Atom *argument);
static Atom *petta_block_join_actual_types(
    PettaBlockCheck *check, Atom **types, size_t count);
static size_t petta_block_equation_count(
    PettaBlockCheck *check, Atom *head, CettaExprLen arity);
static bool petta_block_name_in(
    const char *name, const char *const *names, size_t count);

static bool petta_block_deferred_form(const char *head) {
    if (!head)
        return false;
    static const char *const deferred[] = {
        "let", "let*", "chain", "case", "superpose", "hyperpose",
        "collapse", "match", "catch", "sealed", "once", "transaction",
        "with_mutex", "|->", "lambda", "cons", "car-atom", "cdr-atom",
        "foldl-atom", "map-atom", "map-flat", "filter-atom", "bind",
        "unify", "call-predicate", "add-atom", "remove-atom",
        "get-atoms", "get-type", "get-type-space", "make-list",
        "list_to_set", "list-to-set", "foldall", "fold-flat",
        "and", "or", "xor", "implies", "=="
    };
    for (size_t index = 0u;
         index < sizeof(deferred) / sizeof(deferred[0]); index++) {
        if (strcmp(head, deferred[index]) == 0)
            return true;
    }
    return false;
}

/* An administrative identity binding returns its producer without changing
 * either its shape or its cardinality.  Besides `let x value x`, a `let*`
 * chain may only forward a variable through prior variable bindings.  Keep
 * this recognizer deliberately narrow: ordinary binding bodies and
 * non-variable patterns may add computation or fail. */
static Atom *petta_block_identity_binding_value(Atom *expression) {
    if (!expression || expression->kind != ATOM_EXPR ||
        expression->expr.len == 0u)
        return NULL;
    const char *head = petta_block_symbol_name(expression->expr.elems[0]);
    if (head && strcmp(head, "let") == 0 && expression->expr.len == 4u &&
        expression->expr.elems[1]->kind == ATOM_VAR &&
        atom_eq(expression->expr.elems[1], expression->expr.elems[3])) {
        return expression->expr.elems[2];
    }
    if (!head || strcmp(head, "let*") != 0 || expression->expr.len != 3u)
        return NULL;
    Atom *bindings = expression->expr.elems[1];
    Atom *current = expression->expr.elems[2];
    if (!bindings || bindings->kind != ATOM_EXPR || bindings->expr.len == 0u ||
        !current || current->kind != ATOM_VAR)
        return NULL;
    for (CettaExprIndex index = 0u; index < bindings->expr.len; index++) {
        Atom *binding = bindings->expr.elems[index];
        if (!binding || binding->kind != ATOM_EXPR || binding->expr.len != 2u ||
            binding->expr.elems[0]->kind != ATOM_VAR) {
            return NULL;
        }
    }
    CettaExprIndex prior_count = bindings->expr.len;
    bool followed_binding = false;
    while (prior_count > 0u) {
        bool found = false;
        for (CettaExprIndex index = prior_count; index > 0u; index--) {
            Atom *binding = bindings->expr.elems[index - 1u];
            if (!atom_eq(binding->expr.elems[0], current))
                continue;
            current = binding->expr.elems[1];
            prior_count = index - 1u;
            followed_binding = true;
            found = true;
            break;
        }
        if (!found || !current)
            return followed_binding ? current : NULL;
        if (current->kind != ATOM_VAR)
            return current;
    }
    return followed_binding ? current : NULL;
}

/*
 * `empty` produces no value.  Its declared result is a universally
 * quantified output that occurs in no argument position, so by
 * parametricity only a bottom implementation inhabits it, and a value that
 * is never produced satisfies every requirement.
 *
 * A branching form therefore takes no constraint at all from a branch that
 * is bottom: such a branch must neither be joined into the merged type nor
 * compared against its siblings for compatibility.  Recording it as an
 * unknown type is the error this predicate exists to prevent -- "never
 * produced" read as "type could not be determined" turns a well-typed
 * program into a rejection, exactly inverting the reading that makes an
 * undetermined branch dangerous.
 *
 * Bottom propagates through the pure merge and sequencing forms: a form
 * whose every value-producing position is bottom produces no value either.
 */
static bool petta_block_expression_is_bottom(
    Atom *expression, uint32_t depth) {
    if (!expression || depth > 128u ||
        expression->kind != ATOM_EXPR || expression->expr.len == 0u)
        return false;
    const char *head = petta_block_symbol_name(expression->expr.elems[0]);
    if (!head)
        return false;
    if (strcmp(head, "empty") == 0 && expression->expr.len == 1u)
        return true;
    if (strcmp(head, "if") == 0 && expression->expr.len == 4u) {
        return petta_block_expression_is_bottom(
                   expression->expr.elems[2], depth + 1u) &&
               petta_block_expression_is_bottom(
                   expression->expr.elems[3], depth + 1u);
    }
    if (strcmp(head, "if") == 0 && expression->expr.len == 3u) {
        return petta_block_expression_is_bottom(
            expression->expr.elems[2], depth + 1u);
    }
    if ((strcmp(head, "let") == 0 || strcmp(head, "chain") == 0) &&
        expression->expr.len == 4u) {
        return petta_block_expression_is_bottom(
            expression->expr.elems[3], depth + 1u);
    }
    if (strcmp(head, "let*") == 0 && expression->expr.len == 3u) {
        return petta_block_expression_is_bottom(
            expression->expr.elems[2], depth + 1u);
    }
    if (strcmp(head, "progn") == 0 && expression->expr.len >= 2u) {
        return petta_block_expression_is_bottom(
            expression->expr.elems[expression->expr.len - 1u],
            depth + 1u);
    }
    if (strcmp(head, "case") == 0 && expression->expr.len == 3u) {
        Atom *branches = expression->expr.elems[2];
        if (!branches || branches->kind != ATOM_EXPR ||
            branches->expr.len == 0u)
            return false;
        for (CettaExprIndex index = 0u;
             index < branches->expr.len; index++) {
            Atom *branch = branches->expr.elems[index];
            if (!branch || branch->kind != ATOM_EXPR ||
                branch->expr.len != 2u ||
                !petta_block_expression_is_bottom(
                    branch->expr.elems[1], depth + 1u))
                return false;
        }
        return true;
    }
    return false;
}

static bool petta_block_contains_nondeterministic_form(
    Atom *expression, uint32_t depth) {
    if (!expression || depth > 2048u || expression->kind != ATOM_EXPR)
        return false;
    if (expression->expr.len > 0u) {
        const char *head = petta_block_symbol_name(expression->expr.elems[0]);
        static const char *const nondeterministic[] = {
            "superpose", "hyperpose", "get-atoms", "match",
            "member", "random-int"
        };
        if (petta_block_name_in(
                head, nondeterministic,
                sizeof(nondeterministic) /
                    sizeof(nondeterministic[0])))
            return true;
    }
    for (CettaExprIndex index = 0u;
         index < expression->expr.len; index++) {
        if (petta_block_contains_nondeterministic_form(
                expression->expr.elems[index], depth + 1u))
            return true;
    }
    return false;
}

static bool petta_block_check_expected(
    PettaBlockCheck *check, Atom *actual, Atom *expected,
    Atom **actual_out) {
    if (actual_out)
        *actual_out = actual;
    if (!expected || !actual)
        return true;
    Bindings types;
    bindings_init(&types);
    bool compatible = petta_block_type_compatible(
        check, actual, expected, &types);
    if (!compatible &&
        petta_block_runtime_type_candidate(check, expected)) {
        petta_block_note_residual_guard(
            check, expected, "runtime-get-type");
        if (actual_out)
            *actual_out = expected;
        compatible = true;
    }
    bindings_free(&types);
    if (!compatible)
        check->definite_mismatch = true;
    return compatible;
}

static bool petta_block_builtin_signature(
    PettaBlockCheck *check, const char *head,
    CettaExprLen arity, Atom ***arguments_out,
    Atom **result_out) {
    if (!check || !head || !arguments_out || !result_out)
        return false;
    if (strcmp(head, "map-flat") == 0 && arity == 2u) {
        Atom *input = atom_var_with_id(
            &check->scratch, "map-input", fresh_var_id());
        Atom *output = atom_var_with_id(
            &check->scratch, "map-output", fresh_var_id());
        Atom *arrow_parts[3] = {
            atom_symbol(&check->scratch, "->"), input, output,
        };
        Atom *closure = arrow_parts[0] && input && output
            ? atom_expr(&check->scratch, arrow_parts, 3u) : NULL;
        Atom *input_list = input
            ? petta_block_unary_type(check, "List", input) : NULL;
        Atom *output_list = output
            ? petta_block_unary_type(check, "List", output) : NULL;
        Atom **arguments = cetta_malloc(
            sizeof(*arguments) * 2u);
        if (!closure || !input_list || !output_list || !arguments) {
            free(arguments);
            return false;
        }
        arguments[0] = closure;
        arguments[1] = input_list;
        *arguments_out = arguments;
        *result_out = output_list;
        return true;
    }
    const char *argument_type = NULL;
    const char *result_type = NULL;
    if ((strcmp(head, "+") == 0 || strcmp(head, "-") == 0 ||
         strcmp(head, "*") == 0 || strcmp(head, "/") == 0 ||
         strcmp(head, "mod") == 0 || strcmp(head, "pow-math") == 0) &&
        arity >= 1u) {
        argument_type = "Number";
        result_type = "Number";
    } else if ((strcmp(head, "<") == 0 || strcmp(head, "<=") == 0 ||
                strcmp(head, ">") == 0 || strcmp(head, ">=") == 0) &&
               arity == 2u) {
        argument_type = "Number";
        result_type = "Bool";
    } else if ((strcmp(head, "and-then") == 0 ||
                strcmp(head, "or-else") == 0) && arity == 2u) {
        argument_type = "Bool";
        result_type = "Bool";
    } else if (strcmp(head, "not") == 0 && arity == 1u) {
        argument_type = "Bool";
        result_type = "Bool";
    } else if (strcmp(head, "assert") == 0 && arity == 1u) {
        argument_type = "%Undefined%";
        result_type = "Bool";
    } else if ((strcmp(head, "is-var") == 0 ||
                strcmp(head, "is-ground") == 0 ||
                strcmp(head, "is-expr") == 0 ||
                strcmp(head, "is-space") == 0) && arity == 1u) {
        argument_type = "%Undefined%";
        result_type = "Bool";
    } else {
        return false;
    }
    Atom **arguments = arity
        ? cetta_malloc(sizeof(*arguments) * (size_t)arity)
        : NULL;
    for (CettaExprIndex index = 0u; index < arity; index++)
        arguments[index] = atom_symbol(&check->scratch, argument_type);
    *arguments_out = arguments;
    *result_out = atom_symbol(&check->scratch, result_type);
    return true;
}

static Atom *petta_block_infer_structural_value_type(
    PettaBlockCheck *check, Atom *value, Bindings *environment,
    Atom *expected, uint32_t depth) {
    if (!check || !value || value->kind != ATOM_EXPR ||
        value->expr.len == 0u || !environment || depth > 2048u)
        return NULL;
    bool contextual = expected && expected->kind == ATOM_EXPR &&
        expected->expr.len == value->expr.len &&
        !petta_block_head_is(expected, "|") &&
        !petta_block_head_is(expected, "List") &&
        !petta_block_arrow_head(expected, NULL);
    Atom **elements = cetta_malloc(
        sizeof(*elements) * (size_t)value->expr.len);
    bool ok = true;
    for (CettaExprIndex index = 0u;
         index < value->expr.len; index++) {
        if (index == 0u && value->expr.elems[index]->kind == ATOM_SYMBOL) {
            elements[index] = value->expr.elems[index];
            continue;
        }
        Atom *field_expected = contextual
            ? expected->expr.elems[index] : NULL;
        elements[index] = NULL;
        if (!petta_block_infer_expr(
                check, value->expr.elems[index], environment,
                field_expected, &elements[index], depth + 1u)) {
            ok = false;
            break;
        }
        if (!elements[index]) {
            elements[index] = petta_block_pattern_type(
                check, value->expr.elems[index], environment,
                depth + 1u);
        }
        if (!elements[index]) {
            ok = false;
            break;
        }
    }
    Atom *type = ok
        ? atom_expr(&check->scratch, elements, value->expr.len)
        : NULL;
    free(elements);
    return type;
}

static bool petta_block_infer_partial_numeric_closure(
    PettaBlockCheck *check, Atom *expression,
    Bindings *environment, Atom *expected,
    Atom **actual_out, uint32_t depth) {
    if (!check || !expression || expression->kind != ATOM_EXPR ||
        expression->expr.len != 2u || !expected ||
        !petta_block_arrow_head(expected, NULL)) {
        return false;
    }
    const char *head = petta_block_symbol_name(expression->expr.elems[0]);
    if (!head ||
        (strcmp(head, "+") != 0 && strcmp(head, "-") != 0 &&
         strcmp(head, "*") != 0 && strcmp(head, "/") != 0 &&
         strcmp(head, "mod") != 0 && strcmp(head, "pow-math") != 0)) {
        return false;
    }
    Atom *number = atom_symbol(&check->scratch, "Number");
    Atom *ignored = NULL;
    if (!number ||
        !petta_block_infer_expr(
            check, expression->expr.elems[1], environment,
            number, &ignored, depth + 1u)) {
        return false;
    }
    Atom *parts[3] = {
        atom_symbol(&check->scratch, "-[det]->"), number, number,
    };
    Atom *actual = parts[0]
        ? atom_expr(&check->scratch, parts, 3u) : NULL;
    return actual && petta_block_check_expected(
                         check, actual, expected, actual_out);
}

static bool petta_block_infer_list_elements(
    PettaBlockCheck *check, Atom *expression,
    CettaExprIndex first, Bindings *environment, Atom *expected,
    Atom **actual_out, uint32_t depth) {
    if (!check || !expression || expression->kind != ATOM_EXPR ||
        first > expression->expr.len) {
        return false;
    }
    Atom *element_expected =
        petta_block_head_is(expected, "List") && expected->expr.len == 2u
            ? expected->expr.elems[1] : NULL;
    size_t count = (size_t)(expression->expr.len - first);
    Atom **types = count
        ? cetta_malloc(sizeof(*types) * count) : NULL;
    if (count && !types)
        return false;
    bool ok = true;
    for (CettaExprIndex index = first;
         index < expression->expr.len; index++) {
        Atom *actual = NULL;
        if (element_expected &&
            petta_block_wildcard(element_expected)) {
            actual = element_expected;
        } else if (!petta_block_infer_expr(
                       check, expression->expr.elems[index], environment,
                       element_expected, &actual, depth + 1u)) {
            ok = false;
            break;
        }
        types[index - first] = actual
            ? actual : atom_symbol(&check->scratch, "%Undefined%");
    }
    Atom *element_actual = NULL;
    if (ok) {
        element_actual = count > 0u
            ? petta_block_join_actual_types(check, types, count)
            : atom_symbol(&check->scratch, "%Undefined%");
        if (element_expected)
            element_actual = element_expected;
    }
    free(types);
    Atom *actual = ok && element_actual
        ? petta_block_unary_type(check, "List", element_actual) : NULL;
    return actual && petta_block_check_expected(
                         check, actual, expected, actual_out);
}

static bool petta_block_try_signature(
    PettaBlockCheck *check, Atom *call, Atom *signature,
    Bindings *environment, Atom *expected,
    Atom **actual_out, uint32_t depth, bool *applicable_out) {
    if (applicable_out)
        *applicable_out = false;
    const char *mode = NULL;
    if (!petta_block_arrow_head(signature, &mode) ||
        signature->expr.len != call->expr.len + 1u)
        return false;
    if (applicable_out)
        *applicable_out = true;
    (void)mode;
    Atom *fresh = atom_freshen_epoch(
        &check->scratch, signature, (uint32_t)fresh_var_id());
    if (!fresh)
        return false;
    Bindings trial;
    if (!bindings_clone(&trial, environment))
        return false;
    bool saved_residual = check->residual_type_guard_required;
    bool ok = true;
    for (CettaExprIndex index = 1u;
         index < call->expr.len; index++) {
        Atom *formal = petta_block_apply_type_bindings(
            check, &trial, fresh->expr.elems[index]);
        Atom *actual = NULL;
        if (!petta_block_infer_expr(
                check, call->expr.elems[index], &trial,
                formal, &actual,
                depth + 1u)) {
            ok = false;
            break;
        }
        if (actual && !petta_block_type_compatible(
                          check, actual, formal, &trial)) {
            if (petta_block_type_contains_open_evidence(actual, 0u)) {
                petta_block_note_residual_guard(
                    check, formal, "open-call-argument");
                continue;
            }
            if (check->policy == PETTA_TYPECHECK_POLICY_DEFAULT &&
                petta_block_plain_types_may_overlap(
                    check, actual, formal)) {
                continue;
            }
            ok = false;
            break;
        }
    }
    Atom *output = fresh->expr.elems[fresh->expr.len - 1u];
    output = petta_block_apply_type_bindings(check, &trial, output);
    if (ok && expected) {
        ok = petta_block_type_compatible(
            check, output, expected, &trial);
        if (!ok &&
            petta_block_runtime_type_candidate(check, expected)) {
            petta_block_note_residual_guard(
                check, expected, "runtime-get-type-result");
            output = expected;
            ok = true;
        }
        output = petta_block_apply_type_bindings(
            check, &trial, output);
    }
    if (ok) {
        bindings_replace(environment, &trial);
        if (actual_out)
            *actual_out = output;
    } else {
        check->residual_type_guard_required = saved_residual;
    }
    bindings_free(&trial);
    return ok;
}

static bool petta_block_infer_named_call(
    PettaBlockCheck *check, Atom *expression,
    Bindings *environment, Atom *expected,
    Atom **actual_out, uint32_t depth) {
    Atom *head_atom = expression->expr.elems[0];
    const char *head = petta_block_symbol_name(head_atom);
    if (!head)
        return false;
    if (strcmp(head, "if") == 0 && expression->expr.len != 4u)
        return false;

    bool saw_signature = false;
    for (size_t index = 0u; index < check->declaration_len; index++) {
        PettaBlockDeclaration *decl = &check->declarations[index];
        if (!atom_eq(decl->subject, head_atom))
            continue;
        bool applicable = false;
        bool outer_mismatch = check->definite_mismatch;
        check->definite_mismatch = false;
        if (petta_block_try_signature(
                check, expression, decl->type, environment,
                expected, actual_out, depth, &applicable)) {
            check->definite_mismatch = outer_mismatch;
            return true;
        }
        saw_signature = saw_signature || applicable;
        check->definite_mismatch = outer_mismatch;
    }

    Atom **types = NULL;
    uint32_t count = petta_block_declared_types(
        check, head_atom, &types);
    for (uint32_t index = 0u; index < count; index++) {
        bool applicable = false;
        bool outer_mismatch = check->definite_mismatch;
        check->definite_mismatch = false;
        if (petta_block_try_signature(
                check, expression, types[index], environment,
                expected, actual_out, depth, &applicable)) {
            check->definite_mismatch = outer_mismatch;
            free(types);
            return true;
        }
        saw_signature = saw_signature || applicable;
        check->definite_mismatch = outer_mismatch;
    }
    free(types);

    Atom **inferred = NULL;
    size_t inferred_count = 0u;
    if (check->program &&
        !petta_block_inferred_signatures_lookup(
            check, head_atom->sym_id,
            expression->expr.len - 1u,
            &inferred, &inferred_count)) {
        return petta_block_fault(
            check, PETTA_TYPECHECK_FAULT_ALLOCATION,
            "could not collect inferred overloads");
    }
    for (size_t index = 0u; index < inferred_count; index++) {
        bool applicable = false;
        bool outer_mismatch = check->definite_mismatch;
        check->definite_mismatch = false;
        if (petta_block_try_signature(
                check, expression, inferred[index], environment,
                expected, actual_out, depth, &applicable)) {
            check->definite_mismatch = outer_mismatch;
            free(inferred);
            return true;
        }
        saw_signature = saw_signature || applicable;
        check->definite_mismatch = outer_mismatch;
    }
    free(inferred);

    Atom **arguments = NULL;
    Atom *result = NULL;
    if (petta_block_builtin_signature(
            check, head, expression->expr.len - 1u,
            &arguments, &result)) {
        CettaExprLen arity = expression->expr.len - 1u;
        Atom **parts = cetta_malloc(
            sizeof(*parts) * (size_t)(arity + 2u));
        parts[0] = atom_symbol(&check->scratch, "-[det]->");
        for (CettaExprIndex index = 0u; index < arity; index++)
            parts[index + 1u] = arguments[index];
        parts[arity + 1u] = result;
        Atom *signature = parts[0]
            ? atom_expr(&check->scratch, parts, arity + 2u)
            : NULL;
        free(parts);
        free(arguments);
        bool applicable = false;
        bool ok = signature && petta_block_try_signature(
            check, expression, signature, environment,
            expected, actual_out, depth, &applicable);
        if (!ok)
            check->definite_mismatch = true;
        return ok;
    }
    if (saw_signature)
        check->definite_mismatch = true;
    return false;
}

static bool petta_block_head_is_callable(
    PettaBlockCheck *check, Atom *head, CettaExprLen arity) {
    if (!check || !head || head->kind != ATOM_SYMBOL)
        return false;
    for (size_t index = 0u; index < check->form_count; index++) {
        Atom *form = check->forms[index];
        if (!petta_block_head_is(form, "=") ||
            form->expr.len != 3u)
            continue;
        Atom *lhs = form->expr.elems[1];
        if (lhs && lhs->kind == ATOM_EXPR &&
            lhs->expr.len == arity + 1u &&
            lhs->expr.len > 0u &&
            atom_eq(lhs->expr.elems[0], head))
            return true;
    }
    CettaExprLen minimum = 0u;
    CettaExprLen maximum = 0u;
    bool exact = false;
    return space_equation_head_arity_bounds(
               check->space, head->sym_id,
               &minimum, &maximum, &exact, arity) && exact;
}

static Atom *petta_block_find_space_schema(
    PettaBlockCheck *check, Atom *reference) {
    if (!check || !reference || reference->kind != ATOM_SYMBOL)
        return NULL;
    for (size_t index = check->declaration_len; index > 0u; index--) {
        PettaBlockDeclaration *decl =
            &check->declarations[index - 1u];
        if (atom_eq(decl->subject, reference) &&
            petta_block_head_is(decl->type, "SpaceOf") &&
            decl->type->expr.len == 2u) {
            /* Reads preserve the declared schema, including nominal roles.
             * Pattern binding knows how to inspect Alias/Newtype
             * representations where necessary; eagerly normalizing here
             * erased Statement/KBContext/Proof/TV from the whole @-bound row
             * and later made an otherwise valid returned row look structural. */
            return decl->type->expr.elems[1];
        }
    }
    Atom **types = NULL;
    uint32_t count = petta_block_declared_types(
        check, reference, &types);
    Atom *schema = NULL;
    for (uint32_t index = 0u; index < count; index++) {
        if (!petta_block_head_is(types[index], "SpaceOf") ||
            types[index]->expr.len != 2u)
            continue;
        schema = types[index]->expr.elems[1];
        break;
    }
    free(types);
    return schema;
}

static Atom *petta_block_find_relation_schema(
    PettaBlockCheck *check, Atom *pattern) {
    if (!check || !pattern || pattern->kind != ATOM_EXPR ||
        pattern->expr.len == 0u ||
        pattern->expr.elems[0]->kind != ATOM_SYMBOL)
        return NULL;
    Atom *signature = NULL;
    if (!petta_block_find_signature(
            check, pattern->expr.elems[0],
            pattern->expr.len - 1u, &signature)) {
        return NULL;
    }
    Atom *fresh = atom_freshen_epoch(
        &check->scratch, signature, (uint32_t)fresh_var_id());
    if (!fresh || fresh->kind != ATOM_EXPR ||
        fresh->expr.len != pattern->expr.len + 1u)
        return NULL;
    Atom **fields = cetta_malloc(
        sizeof(*fields) * (size_t)pattern->expr.len);
    fields[0] = pattern->expr.elems[0];
    for (CettaExprIndex index = 1u;
         index < pattern->expr.len; index++)
        fields[index] = fresh->expr.elems[index];
    Atom *schema = atom_expr(
        &check->scratch, fields, pattern->expr.len);
    free(fields);
    return schema;
}

static Atom *petta_block_unary_type(
    PettaBlockCheck *check, const char *head, Atom *argument) {
    Atom *parts[2] = {
        atom_symbol(&check->scratch, head), argument,
    };
    return parts[0] && parts[1]
        ? atom_expr(&check->scratch, parts, 2u) : NULL;
}

static Atom *petta_block_join_actual_types(
    PettaBlockCheck *check, Atom **types, size_t count) {
    if (!check || !types || count == 0u)
        return NULL;
    size_t unique = 0u;
    for (size_t index = 0u; index < count; index++) {
        if (!types[index])
            return NULL;
        bool seen = false;
        for (size_t prior = 0u; prior < unique; prior++) {
            if (atom_eq(types[prior], types[index])) {
                seen = true;
                break;
            }
        }
        if (!seen)
            types[unique++] = types[index];
    }
    if (unique == 1u)
        return types[0];
    Atom **parts = cetta_malloc(sizeof(*parts) * (unique + 1u));
    parts[0] = atom_symbol(&check->scratch, "|");
    for (size_t index = 0u; index < unique; index++)
        parts[index + 1u] = types[index];
    Atom *joined = atom_expr(
        &check->scratch, parts, (CettaExprLen)(unique + 1u));
    free(parts);
    return joined;
}

static bool petta_block_case_pattern_consumes_constructor(
    Atom *pattern, Atom *constructor, CettaExprLen arity) {
    if (!pattern || pattern->kind != ATOM_EXPR ||
        pattern->expr.len != arity + 1u ||
        !atom_eq(pattern->expr.elems[0], constructor)) {
        return false;
    }
    for (CettaExprIndex index = 1u;
         index < pattern->expr.len; index++) {
        Atom *argument = pattern->expr.elems[index];
        if (!argument || argument->kind != ATOM_VAR)
            return false;
        for (CettaExprIndex prior = 1u; prior < index; prior++) {
            if (pattern->expr.elems[prior]->kind == ATOM_VAR &&
                pattern->expr.elems[prior]->var_id == argument->var_id) {
                return false;
            }
        }
    }
    return true;
}

static bool petta_block_case_prior_consumes_constructor(
    Atom *branches, CettaExprIndex prior_count,
    Atom *constructor, CettaExprLen arity) {
    if (!branches || branches->kind != ATOM_EXPR)
        return false;
    if (prior_count > branches->expr.len)
        prior_count = branches->expr.len;
    for (CettaExprIndex index = 0u; index < prior_count; index++) {
        Atom *branch = branches->expr.elems[index];
        if (branch && branch->kind == ATOM_EXPR &&
            branch->expr.len == 2u &&
            petta_block_case_pattern_consumes_constructor(
                branch->expr.elems[0], constructor, arity)) {
            return true;
        }
    }
    return false;
}

static bool petta_block_case_nominal_member_consumed(
    PettaBlockCheck *check, Atom *member,
    Atom *branches, CettaExprIndex prior_count) {
    const char *name = petta_block_symbol_name(member);
    if (!name || petta_block_wildcard(member) ||
        strcmp(name, "Number") == 0 ||
        strcmp(name, "String") == 0 ||
        strcmp(name, "Bool") == 0 ||
        petta_block_decl_representation(check, member, "Newtype")) {
        return false;
    }

    size_t constructors = 0u;
    for (size_t index = 0u; index < check->declaration_len; index++) {
        PettaBlockDeclaration *decl = &check->declarations[index];
        if (decl->inferred ||
            !petta_block_arrow_head(decl->type, NULL)) {
            continue;
        }
        CettaExprLen arity = decl->type->expr.len - 2u;
        Atom *result = decl->type->expr.elems[
            decl->type->expr.len - 1u];
        /* A polymorphic identity-like operation is not a constructor for
         * every nominal result its type variable can instantiate to.  Only
         * declarations with an actual result head participate in exhaustiveness. */
        if (result->kind == ATOM_VAR || petta_block_wildcard(result) ||
            petta_block_equation_count(
                check, decl->subject, arity) != 0u) {
            continue;
        }
        Bindings compatibility;
        bindings_init(&compatibility);
        bool produces_member = petta_block_type_compatible(
            check, result, member, &compatibility);
        bindings_free(&compatibility);
        if (!produces_member)
            continue;
        constructors++;
        bool consumed = petta_block_case_prior_consumes_constructor(
            branches, prior_count, decl->subject, arity);
        if (!consumed) {
            return false;
        }
    }
    return constructors > 0u;
}

static bool petta_block_case_member_consumed(
    PettaBlockCheck *check, Atom *member,
    Atom *branches, CettaExprIndex prior_count) {
    if (!member)
        return false;
    if (member->kind == ATOM_SYMBOL) {
        return petta_block_case_nominal_member_consumed(
            check, member, branches, prior_count);
    }
    if (member->kind != ATOM_EXPR || member->expr.len == 0u ||
        petta_block_head_is(member, "|") ||
        petta_block_head_is(member, "List") ||
        petta_block_arrow_head(member, NULL) ||
        member->expr.elems[0]->kind != ATOM_SYMBOL) {
        return false;
    }
    return petta_block_case_prior_consumes_constructor(
        branches, prior_count, member->expr.elems[0],
        member->expr.len - 1u);
}

/* A case arm receives only values whose outer shape matches its pattern.  A
 * union member whose remaining constructors all have incompatible arity/head
 * must not be selected merely because an open pattern can be assigned
 * `%Undefined%` fields.  This is flow-sensitive constructor elimination, not
 * nominal erasure: Newtypes and types with no complete constructor catalog are
 * retained conservatively. */
static bool petta_block_case_pattern_can_match_constructor(
    Atom *pattern, Atom *constructor, CettaExprLen arity) {
    if (!pattern)
        return false;
    if (pattern->kind == ATOM_VAR)
        return true;
    if (pattern->kind != ATOM_EXPR || pattern->expr.len != arity + 1u ||
        pattern->expr.len == 0u)
        return false;
    Atom *head = pattern->expr.elems[0];
    if (head->kind == ATOM_VAR || head->kind == ATOM_EXPR)
        return true;
    return head->kind == ATOM_SYMBOL && atom_eq(head, constructor);
}

static bool petta_block_case_pattern_can_match_member(
    PettaBlockCheck *check, Atom *pattern, Atom *member,
    Atom *branches, CettaExprIndex prior_count) {
    if (!check || !pattern || !member)
        return false;
    if (pattern->kind == ATOM_VAR || petta_block_wildcard(member))
        return true;
    if (member->kind == ATOM_EXPR) {
        if (petta_block_head_is(member, "|") ||
            petta_block_head_is(member, "List") ||
            petta_block_arrow_head(member, NULL))
            return true;
        if (member->expr.len == 0u)
            return pattern->kind == ATOM_EXPR && pattern->expr.len == 0u;
        if (member->expr.elems[0]->kind != ATOM_SYMBOL)
            return true;
        return petta_block_case_pattern_can_match_constructor(
            pattern, member->expr.elems[0], member->expr.len - 1u);
    }
    if (member->kind != ATOM_SYMBOL ||
        petta_block_decl_representation(check, member, "Newtype") ||
        strcmp(petta_block_symbol_name(member), "Number") == 0 ||
        strcmp(petta_block_symbol_name(member), "String") == 0 ||
        strcmp(petta_block_symbol_name(member), "Bool") == 0) {
        return true;
    }

    size_t constructors = 0u;
    for (size_t index = 0u; index < check->declaration_len; index++) {
        PettaBlockDeclaration *decl = &check->declarations[index];
        if (decl->inferred || !petta_block_arrow_head(decl->type, NULL))
            continue;
        CettaExprLen arity = decl->type->expr.len - 2u;
        Atom *result = decl->type->expr.elems[decl->type->expr.len - 1u];
        if (petta_block_wildcard(result) ||
            petta_block_equation_count(check, decl->subject, arity) != 0u)
            continue;
        Bindings compatibility;
        bindings_init(&compatibility);
        bool produces_member = petta_block_type_compatible(
            check, result, member, &compatibility);
        bindings_free(&compatibility);
        if (!produces_member)
            continue;
        constructors++;
        if (petta_block_case_prior_consumes_constructor(
                branches, prior_count, decl->subject, arity))
            continue;
        if (petta_block_case_pattern_can_match_constructor(
                pattern, decl->subject, arity))
            return true;
    }
    return constructors == 0u;
}

static Atom *petta_block_case_narrow_pattern_type(
    PettaBlockCheck *check, Atom *type, Atom *pattern,
    Atom *branches, CettaExprIndex prior_count) {
    if (!check || !type || !pattern ||
        !petta_block_head_is(type, "|") || type->expr.len <= 1u)
        return type;
    CettaExprLen member_count = type->expr.len - 1u;
    Atom **kept = cetta_malloc(sizeof(*kept) * (size_t)member_count);
    if (!kept)
        return type;
    CettaExprLen kept_count = 0u;
    for (CettaExprIndex index = 1u; index < type->expr.len; index++) {
        Atom *member = type->expr.elems[index];
        if (petta_block_case_pattern_can_match_member(
                check, pattern, member, branches, prior_count))
            kept[kept_count++] = member;
    }
    Atom *result = type;
    if (kept_count == 1u) {
        result = kept[0];
    } else if (kept_count > 1u && kept_count < member_count) {
        Atom **parts = cetta_malloc(
            sizeof(*parts) * (size_t)(kept_count + 1u));
        if (parts) {
            parts[0] = atom_symbol(&check->scratch, "|");
            memcpy(parts + 1u, kept,
                   sizeof(*kept) * (size_t)kept_count);
            Atom *joined = atom_expr(
                &check->scratch, parts, kept_count + 1u);
            if (joined)
                result = joined;
            free(parts);
        }
    }
    free(kept);
    return result;
}

static Atom *petta_block_case_fallthrough_type(
    PettaBlockCheck *check, Atom *scrutinee_type,
    Atom *branches, CettaExprIndex prior_count) {
    /* Reveal only aliases around the scrutinee.  Union members retain their
     * nominal identity: in particular a Newtype member must not collapse to
     * its representation merely because a preceding constructor was
     * eliminated. */
    Atom *normalized = scrutinee_type;
    for (uint32_t depth = 0u;
         normalized && normalized->kind == ATOM_SYMBOL && depth < 256u;
         depth++) {
        Atom *representation = petta_block_decl_representation(
            check, normalized, "Alias");
        if (!representation)
            break;
        normalized = representation;
    }
    if (!petta_block_head_is(normalized, "|") ||
        normalized->expr.len <= 1u || prior_count == 0u) {
        return normalized;
    }
    CettaExprLen member_count = normalized->expr.len - 1u;
    Atom **kept = cetta_malloc(sizeof(*kept) * (size_t)member_count);
    if (!kept)
        return scrutinee_type;
    CettaExprLen kept_count = 0u;
    for (CettaExprIndex index = 1u;
         index < normalized->expr.len; index++) {
        Atom *member = normalized->expr.elems[index];
        bool consumed = petta_block_case_member_consumed(
            check, member, branches, prior_count);
        if (!consumed) {
            kept[kept_count++] = member;
        }
    }
    Atom *result = normalized;
    if (kept_count > 0u && kept_count < member_count) {
        if (kept_count == 1u) {
            result = kept[0];
        } else {
            Atom **parts = cetta_malloc(
                sizeof(*parts) * (size_t)(kept_count + 1u));
            if (parts) {
                parts[0] = atom_symbol(&check->scratch, "|");
                memcpy(
                    parts + 1u, kept,
                    sizeof(*kept) * (size_t)kept_count);
                Atom *joined = atom_expr(
                    &check->scratch, parts, kept_count + 1u);
                if (joined)
                    result = joined;
                free(parts);
            }
        }
    }
    free(kept);
    return result;
}

static bool petta_block_infer_alternatives(
    PettaBlockCheck *check, Atom *alternatives,
    Bindings *environment, Atom *expected,
    Atom **actual_out, uint32_t depth) {
    if (actual_out)
        *actual_out = NULL;
    if (!check || !alternatives || !environment ||
        alternatives->kind != ATOM_EXPR || depth > 2048u)
        return false;
    size_t count = (size_t)alternatives->expr.len;
    Atom **types = count ? cetta_malloc(sizeof(*types) * count) : NULL;
    size_t type_count = 0u;
    bool known = count > 0u;
    for (CettaExprIndex index = 0u;
         index < alternatives->expr.len; index++) {
        Bindings branch;
        bindings_init(&branch);
        if (!bindings_clone(&branch, environment)) {
            bindings_free(&branch);
            free(types);
            return false;
        }
        Atom *actual = NULL;
        bool valid = petta_block_infer_expr(
            check, alternatives->expr.elems[index], &branch,
            expected, &actual, depth + 1u);
        bindings_free(&branch);
        if (!valid) {
            free(types);
            return false;
        }
        /* A bottom alternative contributes no value, hence no candidate. */
        if (petta_block_expression_is_bottom(
                alternatives->expr.elems[index], 0u))
            continue;
        types[type_count++] = actual;
        known = known && actual != NULL;
    }
    if (actual_out) {
        CETTA_PETTA_TYPECHECK_CENSUS_HIT_IF(
            !expected && check->inferring_signature &&
                count > 0u && type_count == 0u,
            CETTA_PETTA_TYPECHECK_CENSUS_EVENT_INFERENCE_EMPTY_ALTERNATIVE_SET_UNPUBLISHED);
        *actual_out = expected ? expected
            : (known && type_count > 0u
                ? petta_block_join_actual_types(
                      check, types, type_count)
                : NULL);
    }
    free(types);
    return true;
}

static Atom *petta_block_quoted_type(
    PettaBlockCheck *check, Atom *value,
    Bindings *environment, uint32_t depth) {
    if (!check || !value || !environment || depth > 2048u)
        return NULL;
    if (value->kind == ATOM_VAR) {
        Atom *known = bindings_lookup_var(environment, value);
        return known ? known : atom_var_with_id(
            &check->scratch, "quoted-type", fresh_var_id());
    }
    Atom *literal = petta_block_literal_type(check, value);
    if (literal)
        return literal;
    if (value->kind == ATOM_SYMBOL) {
        Atom *declared = petta_block_declared_value_type(check, value);
        return declared ? declared : atom_symbol(&check->scratch, "Atom");
    }
    if (value->kind != ATOM_EXPR)
        return NULL;
    if (value->expr.len == 0u) {
        Atom *unknown = atom_symbol(&check->scratch, "%Undefined%");
        return petta_block_unary_type(check, "List", unknown);
    }
    /* Quotation holds every inner form as data.  Inner type and data forms
     * therefore cannot establish facts about the enclosing runtime value. */
    CettaExprIndex first = 0u;
    CettaExprLen field_count =
        (CettaExprLen)(value->expr.len - first);
    Atom **fields = cetta_malloc(
        sizeof(*fields) * (size_t)field_count);
    for (CettaExprIndex index = 0u; index < field_count; index++) {
        fields[index] = petta_block_quoted_type(
            check, value->expr.elems[index + first],
            environment, depth + 1u);
        if (!fields[index]) {
            free(fields);
            return NULL;
        }
    }
    Atom *type = atom_expr(
        &check->scratch, fields, field_count);
    free(fields);
    return type;
}

static Atom *petta_block_replace_term_variable(
    PettaBlockCheck *check, Atom *term, VarId variable,
    Atom *replacement, uint32_t depth) {
    if (!check || !term || !replacement || depth > 2048u)
        return NULL;
    if (term->kind == ATOM_VAR && term->var_id == variable)
        return replacement;
    if (term->kind != ATOM_EXPR)
        return term;
    Atom **elements = term->expr.len
        ? cetta_malloc(sizeof(*elements) * (size_t)term->expr.len)
        : NULL;
    for (CettaExprIndex index = 0u;
         index < term->expr.len; index++) {
        elements[index] = petta_block_replace_term_variable(
            check, term->expr.elems[index], variable,
            replacement, depth + 1u);
        if (!elements[index]) {
            free(elements);
            return NULL;
        }
    }
    Atom *result = atom_expr(
        &check->scratch, elements, term->expr.len);
    free(elements);
    return result;
}

static bool petta_block_equality_case_pattern(
    PettaBlockCheck *check, Atom *pattern,
    Atom *known_type, uint32_t depth) {
    if (!check || !pattern || !known_type || depth > 2048u ||
        pattern->kind == ATOM_VAR)
        return false;
    if (pattern->kind != ATOM_EXPR)
        return true;
    if (pattern->expr.len == 0u)
        return true;
    Atom *head = pattern->expr.elems[0];
    if (head->kind == ATOM_SYMBOL) {
        Atom *signature = NULL;
        if (petta_block_find_signature(
                check, head, pattern->expr.len - 1u, &signature) &&
            petta_block_equation_count(
                check, head, pattern->expr.len - 1u) == 0u) {
            return true;
        }
    }
    Atom *shape = petta_block_normalize_value_schema(
        check, known_type, depth + 1u);
    return shape && shape->kind == ATOM_EXPR &&
           shape->expr.len == pattern->expr.len;
}

static Atom *petta_block_lower_equality_if_to_case(
    PettaBlockCheck *check, Atom *expression,
    Bindings *environment, uint32_t depth) {
    if (!check || !expression || !environment || depth > 2048u ||
        !petta_block_head_is(expression, "if") ||
        expression->expr.len != 4u)
        return NULL;
    Atom *condition = expression->expr.elems[1];
    if (!petta_block_head_is(condition, "=") ||
        condition->expr.len != 3u ||
        condition->expr.elems[1]->kind != ATOM_VAR)
        return NULL;
    Atom *variable = condition->expr.elems[1];
    Atom *pattern = condition->expr.elems[2];
    Atom *known_type = bindings_lookup_var(environment, variable);
    if (!known_type ||
        !petta_block_equality_case_pattern(
            check, pattern, known_type, depth + 1u))
        return NULL;
    Atom *fallthrough = atom_var_with_id(
        &check->scratch, "if-fallthrough", fresh_var_id());
    Atom *narrow_else = fallthrough
        ? petta_block_replace_term_variable(
              check, expression->expr.elems[3], variable->var_id,
              fallthrough, depth + 1u)
        : NULL;
    if (!fallthrough || !narrow_else)
        return NULL;
    Atom *first_parts[2] = {
        pattern, expression->expr.elems[2],
    };
    Atom *second_parts[2] = { fallthrough, narrow_else };
    Atom *first = atom_expr(&check->scratch, first_parts, 2u);
    Atom *second = atom_expr(&check->scratch, second_parts, 2u);
    Atom *branch_parts[2] = { first, second };
    Atom *branches = first && second
        ? atom_expr(&check->scratch, branch_parts, 2u) : NULL;
    Atom *case_parts[3] = {
        atom_symbol(&check->scratch, "case"), variable, branches,
    };
    return case_parts[0] && branches
        ? atom_expr(&check->scratch, case_parts, 3u) : NULL;
}

static bool petta_block_infer_expr(
    PettaBlockCheck *check, Atom *expression,
    Bindings *environment, Atom *expected,
    Atom **actual_out, uint32_t depth) {
    if (actual_out)
        *actual_out = NULL;
    if (!check || !expression || !environment || depth > 2048u)
        return false;
    if (expression->kind == ATOM_VAR) {
        if (expected && check->policy != PETTA_TYPECHECK_POLICY_DEFAULT &&
            check->explicit_ascription_depth == 0u &&
            petta_block_dynamic_pattern_var(check, expression->var_id)) {
            check->definite_mismatch = true;
            return false;
        }
        Atom *known = bindings_lookup_var(environment, expression);
        if (!known && expected) {
            petta_block_note_residual_guard(check, expected, "unbound-variable");
            if (!petta_block_env_note_fresh(
                    environment, expression, expected))
                return false;
            known = expected;
        } else if (known && expected) {
            if (!atom_eq(known, expected) &&
                petta_block_wildcard(known)) {
                petta_block_note_residual_guard(check, expected, "open-variable-type");
            }
            Bindings trial;
            bindings_init(&trial);
            if (!bindings_clone(&trial, environment))
                return false;
            bool compatible = petta_block_type_compatible(
                check, known, expected, &trial);
            if (compatible) {
                bindings_replace(environment, &trial);
                known = petta_block_apply_type_bindings(
                    check, environment, known);
            }
            bindings_free(&trial);
            if (!compatible) {
                /* A disagreement between two uses of an undeclared clause
                 * parameter invalidates that inferred parameter type; it is
                 * not an author-written contract and cannot reject the
                 * clause.  Keep checking the body so its result type can
                 * still be learned, while the parameter is published as
                 * unknown and remains guarded at runtime. */
                if (check->inferring_signature) {
                    if (!petta_block_note_inference_taint(
                            check, expression->var_id))
                        return false;
                    CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                        CETTA_PETTA_TYPECHECK_CENSUS_EVENT_INFERENCE_PARAMETER_TAINT);
                    petta_block_note_residual_guard(
                        check, expected, "tainted-inference-parameter");
                    if (actual_out)
                        *actual_out = expected;
                    return true;
                }
                /* An open candidate is not evidence of a contradiction.
                 * Roman's default checker preserves it as a residual
                 * obligation; strict profiles reject the unresolved guard at
                 * the enclosing boundary.  Only a closed incompatible type is
                 * a refutation. */
                if (petta_block_type_contains_open_evidence(known, 0u)) {
                    petta_block_note_residual_guard(
                        check, expected, "open-variable-candidate");
                    if (actual_out)
                        *actual_out = known;
                    return true;
                }
                if (check->policy == PETTA_TYPECHECK_POLICY_DEFAULT &&
                    petta_block_plain_types_may_overlap(
                        check, known, expected)) {
                    if (actual_out)
                        *actual_out = known;
                    return true;
                }
                check->definite_mismatch = true;
                return false;
            }
        }
        if (actual_out)
            *actual_out = known;
        return true;
    }

    /* Newtypes are nominal but erased.  A raw value may acquire the required
     * role from a declared argument position after its representation has
     * been checked.  A value already known to carry a different role must
     * not be relabelled merely because both roles share a representation. */
    Atom *contextual_representation = expected &&
            expected->kind == ATOM_SYMBOL
        ? petta_block_decl_representation(check, expected, "Newtype")
        : NULL;
    bool explicit_role =
        (petta_block_head_is(expression, "brand") ||
         petta_block_head_is(expression, "the")) &&
        expression->expr.len == 3u;
    if (contextual_representation && !explicit_role) {
        Bindings probe_environment;
        bindings_init(&probe_environment);
        if (!bindings_clone(&probe_environment, environment)) {
            bindings_free(&probe_environment);
            return false;
        }
        bool saved_mismatch = check->definite_mismatch;
        bool saved_residual = check->residual_type_guard_required;
        check->definite_mismatch = false;
        Atom *probe_actual = NULL;
        bool probe_ok = petta_block_infer_expr(
            check, expression, &probe_environment, NULL,
            &probe_actual, depth + 1u);
        if (probe_ok && probe_actual) {
            Atom *probe_role = probe_actual->kind == ATOM_SYMBOL
                ? petta_block_decl_representation(
                      check, probe_actual, "Newtype")
                : NULL;
            if (probe_role) {
                bool same_role = atom_eq(probe_actual, expected);
                bindings_free(&probe_environment);
                check->definite_mismatch = saved_mismatch || !same_role;
                if (!same_role) {
                    check->residual_type_guard_required = saved_residual;
                    return false;
                }
                if (actual_out)
                    *actual_out = expected;
                return true;
            }
            if (petta_block_type_compatible(
                    check, probe_actual, contextual_representation,
                    &probe_environment)) {
                bindings_replace(environment, &probe_environment);
                bindings_free(&probe_environment);
                check->definite_mismatch = saved_mismatch;
                if (actual_out)
                    *actual_out = expected;
                return true;
            }
        }
        bindings_free(&probe_environment);
        check->definite_mismatch = saved_mismatch;
        check->residual_type_guard_required = saved_residual;

        Atom *representation_actual = NULL;
        if (!petta_block_infer_expr(
                check, expression, environment,
                contextual_representation, &representation_actual,
                depth + 1u)) {
            return false;
        }
        Atom *representation_role = representation_actual &&
                representation_actual->kind == ATOM_SYMBOL
            ? petta_block_decl_representation(
                  check, representation_actual, "Newtype")
            : NULL;
        if (representation_role &&
            !atom_eq(representation_actual, expected)) {
            check->definite_mismatch = true;
            return false;
        }
        if (actual_out)
            *actual_out = expected;
        return true;
    }

    Atom *literal = petta_block_literal_type(check, expression);
    if (literal)
        return petta_block_check_expected(
            check, literal, expected, actual_out);
    if (expression->kind == ATOM_SYMBOL) {
        if (expected && petta_block_arrow_head(expected, NULL)) {
            CettaExprLen arity = expected->expr.len - 2u;
            Atom *signature = NULL;
            if (petta_block_find_signature(
                    check, expression, arity, &signature)) {
                return petta_block_check_expected(
                    check, signature, expected, actual_out);
            }
            const char *name = petta_block_symbol_name(expression);
            Atom **arguments = NULL;
            Atom *result = NULL;
            if (name && petta_block_builtin_signature(
                            check, name, arity,
                            &arguments, &result)) {
                Atom **parts = cetta_malloc(
                    sizeof(*parts) * (size_t)(arity + 2u));
                parts[0] = atom_symbol(&check->scratch, "-[det]->");
                for (CettaExprIndex index = 0u;
                     index < arity; index++)
                    parts[index + 1u] = arguments[index];
                parts[arity + 1u] = result;
                Atom *builtin = parts[0]
                    ? atom_expr(&check->scratch, parts, arity + 2u)
                    : NULL;
                free(parts);
                free(arguments);
                return builtin && petta_block_check_expected(
                                      check, builtin, expected,
                                      actual_out);
            }
        }
        Atom *actual = NULL;
        if (check->inferring_signature && !expected) {
            bool ambiguous = false;
            actual = petta_block_unique_explicit_value_candidate_type(
                check, expression, &ambiguous);
            if (ambiguous)
                actual = atom_symbol(&check->scratch, "%Undefined%");
        }
        if (!actual)
            actual = petta_block_declared_value_type(check, expression);
        if (!actual)
            actual = atom_symbol(&check->scratch, "Atom");
        return petta_block_check_expected(
            check, actual, expected, actual_out);
    }
    if (expression->kind != ATOM_EXPR)
        return false;
    if (expression->expr.len == 0u) {
        Atom *element = atom_var_with_id(
            &check->scratch, "empty-list-element", fresh_var_id());
        Atom *actual = element
            ? petta_block_unary_type(check, "List", element) : NULL;
        return actual && petta_block_check_expected(
                             check, actual, expected, actual_out);
    }

    const char *head = petta_block_symbol_name(expression->expr.elems[0]);
    if (!head) {
        bool contextual_product = expected &&
            expected->kind == ATOM_EXPR &&
            expected->expr.len == expression->expr.len &&
            !petta_block_head_is(expected, "|") &&
            !petta_block_head_is(expected, "List") &&
            !petta_block_arrow_head(expected, NULL);
        if (contextual_product) {
            Atom *structural = petta_block_infer_structural_value_type(
                check, expression, environment, expected,
                depth + 1u);
            return structural && petta_block_check_expected(
                                     check, structural, expected,
                                     actual_out);
        }
        if (petta_block_head_is(expected, "List") &&
            expected->expr.len == 2u) {
            Atom *first_type = expression->expr.elems[0]->kind == ATOM_VAR
                ? bindings_lookup_var(
                      environment, expression->expr.elems[0])
                : NULL;
            if (!first_type ||
                !petta_block_arrow_head(first_type, NULL)) {
                return petta_block_infer_list_elements(
                    check, expression, 0u, environment, expected,
                    actual_out, depth + 1u);
            }
        }
        if (expression->expr.elems[0]->kind == ATOM_VAR && environment) {
            Atom *closure_type = bindings_lookup_var(
                environment, expression->expr.elems[0]);
            if (closure_type && petta_block_arrow_head(
                                    closure_type, NULL)) {
                Atom *result_type = closure_type->expr.elems[
                    closure_type->expr.len - 1u];
                return petta_block_check_expected(
                    check, result_type, expected, actual_out);
            }
            if (closure_type) {
                Atom *structural = petta_block_infer_structural_value_type(
                    check, expression, environment, expected,
                    depth + 1u);
                return structural && petta_block_check_expected(
                    check, structural, expected, actual_out);
            }
        }
        petta_block_note_residual_guard(check, expected, "dynamic-head");
        if (actual_out)
            *actual_out = expected;
        return true;
    }
    if (petta_block_infer_partial_numeric_closure(
            check, expression, environment, expected,
            actual_out, depth + 1u)) {
        return true;
    }
    if (strcmp(head, "make-list") == 0)
        return petta_block_infer_list_elements(
            check, expression, 1u, environment, expected,
            actual_out, depth + 1u);
    Atom *cons_expected = strcmp(head, "cons") == 0 &&
            expression->expr.len == 3u && expected
        ? petta_block_normalize_value_schema(
              check, expected, depth + 1u)
        : NULL;
    if (petta_block_head_is(cons_expected, "List") &&
        cons_expected->expr.len == 2u) {
        Atom *required[2] = {
            cons_expected->expr.elems[1], cons_expected,
        };
        for (CettaExprIndex slot = 0u; slot < 2u; slot++) {
            Atom *value = expression->expr.elems[slot + 1u];
            Atom *ignored = NULL;
            bool saved_mismatch = check->definite_mismatch;
            check->definite_mismatch = false;
            bool ok = petta_block_infer_expr(
                check, value, environment, required[slot],
                &ignored, depth + 1u);
            bool slot_mismatch = check->definite_mismatch;
            check->definite_mismatch = saved_mismatch || slot_mismatch;
            if (!ok && slot_mismatch &&
                check->policy == PETTA_TYPECHECK_POLICY_DEFAULT &&
                value->kind == ATOM_VAR) {
                check->definite_mismatch = saved_mismatch;
                petta_block_note_residual_guard(
                    check, required[slot], "open-cons-slot");
                ok = true;
            }
            if (!ok)
                return false;
        }
        if (actual_out)
            *actual_out = expected;
        return true;
    }
    if ((strcmp(head, "list_to_set") == 0 ||
         strcmp(head, "list-to-set") == 0) &&
        expression->expr.len == 2u) {
        Atom *source_type = NULL;
        bool saved_mismatch = check->definite_mismatch;
        check->definite_mismatch = false;
        bool source_inferred = petta_block_infer_expr(
            check, expression->expr.elems[1], environment,
            NULL, &source_type, depth + 1u);
        if (!source_inferred &&
            !petta_block_value_is_proven_proper_list(
                check, expression->expr.elems[1], environment,
                depth + 1u)) {
            check->definite_mismatch = saved_mismatch;
            return false;
        }
        check->definite_mismatch = saved_mismatch;
        Atom *actual = source_type;
        if (!petta_block_head_is(actual, "List")) {
            Atom *element = atom_var_with_id(
                &check->scratch, "set-element", fresh_var_id());
            actual = element
                ? petta_block_unary_type(check, "List", element)
                : NULL;
            petta_block_note_residual_guard(
                check, actual, "open-list-to-set-input");
        }
        return actual && petta_block_check_expected(
                             check, actual, expected, actual_out);
    }
    if (head && (strcmp(head, "=") == 0 ||
                 strcmp(head, "==") == 0) &&
        expression->expr.len == 3u) {
        Atom *actual = atom_symbol(&check->scratch, "Bool");
        return petta_block_check_expected(
            check, actual, expected, actual_out);
    }
    if (head && strcmp(head, "|->") == 0 &&
        expression->expr.len == 3u && expected &&
        petta_block_arrow_head(expected, NULL)) {
        const char *expected_mode = NULL;
        petta_block_arrow_head(expected, &expected_mode);
        bool committed = expected_mode &&
            (strcmp(expected_mode, "-[det]->") == 0 ||
             strcmp(expected_mode, "-[deterministic]->") == 0 ||
             strcmp(expected_mode, "-[semidet]->") == 0 ||
             strcmp(expected_mode, "-[semideterministic]->") == 0);
        if (committed) {
            bool nondeterministic =
                petta_block_contains_nondeterministic_form(
                    expression->expr.elems[2], 0u);
            CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                nondeterministic
                    ? CETTA_PETTA_TYPECHECK_CENSUS_EVENT_CONTEXTUAL_LAMBDA_COMMITMENT_REJECT
                    : CETTA_PETTA_TYPECHECK_CENSUS_EVENT_CONTEXTUAL_LAMBDA_COMMITMENT_ACCEPT);
            if (nondeterministic) {
                check->definite_mismatch = true;
                return false;
            }
        }
        if (actual_out)
            *actual_out = expected;
        return true;
    }
    if (head && (strcmp(head, "brand") == 0 ||
                 strcmp(head, "the") == 0) &&
        expression->expr.len == 3u) {
        Atom *ascribed = expression->expr.elems[1];
        bool explicit_ascription = strcmp(head, "the") == 0;
        if (explicit_ascription) {
            Bindings ascription_environment;
            bindings_init(&ascription_environment);
            if (!bindings_clone(
                    &ascription_environment, environment)) {
                bindings_free(&ascription_environment);
                return false;
            }
            check->explicit_ascription_depth++;
            Atom *payload_actual = NULL;
            bool payload_ok = petta_block_infer_expr(
                check, expression->expr.elems[2],
                &ascription_environment, NULL,
                &payload_actual, depth + 1u);
            check->explicit_ascription_depth--;
            bool overlaps = payload_ok;
            if (payload_ok && payload_actual) {
                overlaps = petta_block_type_may_overlap(
                    check, payload_actual, ascribed,
                    &ascription_environment, depth + 1u);
            }
            bindings_free(&ascription_environment);
            if (!payload_ok || !overlaps) {
                check->definite_mismatch = true;
                return false;
            }
            return petta_block_check_expected(
                check, ascribed, expected, actual_out);
        }

        Atom *representation = petta_block_decl_representation(
            check, ascribed, "Newtype");
        if (!representation)
            return false;
        /* v2 refuses direct primitive-literal nominal construction. Preserve
         * that historical compatibility boundary here; computed and variable
         * payloads retain the ordinary representation check below. */
        if (petta_block_literal_type(
                check, expression->expr.elems[2])) {
            CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                CETTA_PETTA_TYPECHECK_CENSUS_EVENT_NEWTYPE_LITERAL_BRAND_REJECT);
            check->definite_mismatch = true;
            return false;
        }
        Atom *ignored = NULL;
        if (!petta_block_infer_expr(
                check, expression->expr.elems[2], environment,
                representation, &ignored, depth + 1u)) {
            return false;
        }
        return petta_block_check_expected(
            check, ascribed, expected, actual_out);
    }
    if (head && strcmp(head, "if") == 0 &&
        expression->expr.len == 4u) {
        Atom *lowered = petta_block_lower_equality_if_to_case(
            check, expression, environment, depth + 1u);
        if (lowered) {
            return petta_block_infer_expr(
                check, lowered, environment,
                expected, actual_out, depth + 1u);
        }
        Atom *bool_type = atom_symbol(&check->scratch, "Bool");
        Atom *ignored = NULL;
        if (!petta_block_infer_expr(
                check, expression->expr.elems[1], environment,
                bool_type, &ignored, depth + 1u))
            return false;
        Atom *left = NULL;
        Atom *right = NULL;
        Bindings left_env;
        Bindings right_env;
        bindings_init(&left_env);
        bindings_init(&right_env);
        if (!bindings_clone(&left_env, environment) ||
            !bindings_clone(&right_env, environment)) {
            bindings_free(&left_env);
            bindings_free(&right_env);
            return false;
        }
        bool ok = petta_block_infer_expr(
                      check, expression->expr.elems[2],
                      &left_env, expected, &left, depth + 1u) &&
                  petta_block_infer_expr(
                      check, expression->expr.elems[3],
                      &right_env, expected, &right, depth + 1u);
        /* A bottom branch produces no value, so it contributes neither a
         * candidate type nor a compatibility obligation.  The surviving
         * branch alone determines the result. */
        bool left_bottom = petta_block_expression_is_bottom(
            expression->expr.elems[2], 0u);
        bool right_bottom = petta_block_expression_is_bottom(
            expression->expr.elems[3], 0u);
        if (left_bottom != right_bottom) {
            CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                CETTA_PETTA_TYPECHECK_CENSUS_EVENT_CONDITIONAL_NO_RESULT_BRANCH);
        }
        if (ok && !expected && !left_bottom && !right_bottom) {
            Bindings join;
            bindings_init(&join);
            ok = left && right &&
                 (petta_block_type_compatible(
                      check, left, right, &join) ||
                  petta_block_type_compatible(
                      check, right, left, &join));
            bindings_free(&join);
        }
        if (ok) {
            bindings_replace(
                environment,
                left_bottom && !right_bottom ? &right_env : &left_env);
            if (actual_out) {
                CETTA_PETTA_TYPECHECK_CENSUS_HIT_IF(
                    !expected && check->inferring_signature &&
                        left_bottom && right_bottom,
                    CETTA_PETTA_TYPECHECK_CENSUS_EVENT_INFERENCE_ALL_BOTTOM_UNPUBLISHED);
                if (!expected && check->inferring_signature &&
                    left_bottom != right_bottom) {
                    /* The historical v2 inference store does not publish a
                     * result type learned only after erasing a no-result
                     * conditional branch.  A declared result remains a
                     * valid check; this affects inference only. */
                    CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                        CETTA_PETTA_TYPECHECK_CENSUS_EVENT_INFERENCE_BOTTOM_BRANCH_UNPUBLISHED);
                    *actual_out = NULL;
                } else {
                    *actual_out = expected ? expected
                        : (left_bottom ? right : left);
                }
            }
        }
        bindings_free(&left_env);
        bindings_free(&right_env);
        return ok;
    }
    if (head && strcmp(head, "call") == 0 &&
        expression->expr.len == 2u) {
        return petta_block_infer_expr(
            check, expression->expr.elems[1], environment,
            expected, actual_out, depth + 1u);
    }
    if (head && strcmp(head, "empty") == 0 &&
        expression->expr.len == 1u) {
        if (actual_out)
            *actual_out = expected;
        return true;
    }
    if (head && strcmp(head, "quote") == 0 &&
        expression->expr.len == 2u) {
        Atom *quoted = expression->expr.elems[1];
        CETTA_PETTA_TYPECHECK_CENSUS_HIT_IF(
            quoted && quoted->kind == ATOM_EXPR && quoted->expr.len == 3u &&
                (petta_block_head_is(quoted, "brand") ||
                 petta_block_head_is(quoted, "the")),
            CETTA_PETTA_TYPECHECK_CENSUS_EVENT_QUOTED_INNER_TYPE_FORM_HELD);
        CETTA_PETTA_TYPECHECK_CENSUS_HIT_IF(
            quoted && quoted->kind == ATOM_EXPR && quoted->expr.len >= 1u &&
                petta_block_head_is(quoted, "data"),
            CETTA_PETTA_TYPECHECK_CENSUS_EVENT_QUOTED_INNER_DATA_FORM_HELD);
        if (petta_block_head_is(expected, "List") &&
            expected->expr.len == 2u && quoted->kind == ATOM_EXPR) {
            /* A quoted expression is runtime data.  In PeTTa's list-backed
             * source representation its fields form a closed list spine;
             * check those fields without interpreting the first one as a
             * callable head. */
            return petta_block_infer_list_elements(
                check, quoted, 0u, environment, expected,
                actual_out, depth + 1u);
        }
        Atom *actual = petta_block_quoted_type(
            check, quoted, environment, depth + 1u);
        if (actual && expected && !atom_eq(actual, expected) &&
            petta_block_type_contains_open_evidence(actual, 0u)) {
            petta_block_note_residual_guard(
                check, expected, "open-quoted-structure");
        }
        return actual && petta_block_check_expected(
                             check, actual, expected, actual_out);
    }
    if (head && strcmp(head, "eval") == 0 &&
        expression->expr.len == 2u) {
        Atom *quoted = expression->expr.elems[1];
        if (petta_block_head_is(quoted, "quote") &&
            quoted->expr.len == 2u) {
            return petta_block_infer_expr(
                check, quoted->expr.elems[1], environment,
                expected, actual_out, depth + 1u);
        }
        petta_block_note_residual_guard(check, expected, "dynamic-eval");
        if (actual_out)
            *actual_out = expected;
        return true;
    }
    if (head && expression->expr.len == 2u &&
        (strcmp(head, "once") == 0 ||
         strcmp(head, "reduce") == 0 ||
         strcmp(head, "transaction") == 0 ||
         strcmp(head, "catch") == 0)) {
        return petta_block_infer_expr(
            check, expression->expr.elems[1], environment,
            expected, actual_out, depth + 1u);
    }
    if (head && expression->expr.len == 3u &&
        (strcmp(head, "sealed") == 0 ||
         strcmp(head, "with_mutex") == 0)) {
        return petta_block_infer_expr(
            check, expression->expr.elems[2], environment,
            expected, actual_out, depth + 1u);
    }
    if (head && strcmp(head, "progn") == 0 &&
        expression->expr.len >= 2u) {
        for (CettaExprIndex index = 1u;
             index + 1u < expression->expr.len; index++) {
            Atom *ignored = NULL;
            if (!petta_block_infer_expr(
                    check, expression->expr.elems[index], environment,
                    NULL, &ignored, depth + 1u)) {
                return false;
            }
        }
        return petta_block_infer_expr(
            check, expression->expr.elems[expression->expr.len - 1u],
            environment, expected, actual_out, depth + 1u);
    }
    Atom *identity_value = expected
        ? petta_block_identity_binding_value(expression) : NULL;
    if (identity_value) {
        return petta_block_infer_expr(
            check, identity_value, environment,
            expected, actual_out, depth + 1u);
    }
    if (head && strcmp(head, "let*") == 0 &&
        expression->expr.len == 3u) {
        Atom *bindings = expression->expr.elems[1];
        if (!bindings || bindings->kind != ATOM_EXPR)
            return false;
        Bindings body;
        bindings_init(&body);
        if (!bindings_clone(&body, environment)) {
            bindings_free(&body);
            return false;
        }
        for (CettaExprIndex index = 0u;
             index < bindings->expr.len; index++) {
            Atom *binding = bindings->expr.elems[index];
            if (!binding || binding->kind != ATOM_EXPR ||
                binding->expr.len != 2u) {
                bindings_free(&body);
                return false;
            }
            Atom *value_type = NULL;
            if (!petta_block_infer_expr(
                    check, binding->expr.elems[1], &body,
                    NULL, &value_type, depth + 1u)) {
                bindings_free(&body);
                return false;
            }
            if (value_type && !petta_block_bind_pattern(
                    check, binding->expr.elems[0], value_type,
                    &body, depth + 1u)) {
                bindings_free(&body);
                return false;
            }
        }
        bool ok = petta_block_infer_expr(
            check, expression->expr.elems[2], &body,
            expected, actual_out, depth + 1u);
        bindings_free(&body);
        return ok;
    }
    if (head && expression->expr.len == 2u &&
        (strcmp(head, "superpose") == 0 ||
         strcmp(head, "hyperpose") == 0)) {
        return petta_block_infer_alternatives(
            check, expression->expr.elems[1], environment,
            expected, actual_out, depth + 1u);
    }
    if (head && strcmp(head, "collapse") == 0 &&
        expression->expr.len == 2u) {
        Atom *element_expected = NULL;
        if (petta_block_head_is(expected, "List") &&
            expected->expr.len == 2u)
            element_expected = expected->expr.elems[1];

        /* `collapse` is the PeTTa/SWI findall boundary: regardless of whether
         * the generator's element type is statically known, its result spine
         * is a closed proper list.  Element inference is therefore
         * best-effort.  A proved mismatch or infrastructure fault remains a
         * failure, while an unresolved generator yields List %Undefined%
         * instead of erasing the proved list-shape fact. */
        Atom *element_actual = NULL;
        bool saved_mismatch = check->definite_mismatch;
        check->definite_mismatch = false;
        bool inferred = petta_block_infer_expr(
            check, expression->expr.elems[1], environment,
            element_expected, &element_actual, depth + 1u);
        bool element_mismatch = check->definite_mismatch;
        check->definite_mismatch = saved_mismatch || element_mismatch;
        if (!inferred &&
            (element_mismatch ||
             (check->result &&
              check->result->fault != PETTA_TYPECHECK_FAULT_NONE))) {
            if (element_mismatch) {
                CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                    CETTA_PETTA_TYPECHECK_CENSUS_EVENT_COLLAPSE_ELEMENT_CONFLICT);
            }
            return false;
        }
        if (!element_actual) {
            CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                CETTA_PETTA_TYPECHECK_CENSUS_EVENT_COLLAPSE_UNKNOWN_ELEMENT);
            element_actual = atom_symbol(&check->scratch, "%Undefined%");
        }
        Atom *actual = petta_block_unary_type(
            check, "List", element_actual);
        CETTA_PETTA_TYPECHECK_CENSUS_HIT(
            CETTA_PETTA_TYPECHECK_CENSUS_EVENT_COLLAPSE_RESULT_LIST_SHAPE);
        return actual && petta_block_check_expected(
                             check, actual, expected, actual_out);
    }
    if (head && strcmp(head, "forall") == 0 &&
        expression->expr.len == 3u) {
        Atom *actual = atom_symbol(&check->scratch, "Bool");
        return petta_block_check_expected(
            check, actual, expected, actual_out);
    }
    if (head && strcmp(head, "foldall") == 0 &&
        expression->expr.len == 4u) {
        Atom *accumulator = expression->expr.elems[1];
        if (petta_block_head_is(expression->expr.elems[2], "empty")) {
            CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                CETTA_PETTA_TYPECHECK_CENSUS_EVENT_FOLDALL_EMPTY_RETURNS_INITIALIZER);
            return petta_block_infer_expr(
                check, expression->expr.elems[3], environment,
                expected, actual_out, depth + 1u);
        }
        if (accumulator->kind == ATOM_SYMBOL) {
            Atom *element = atom_var_with_id(
                &check->scratch, "fold-element", fresh_var_id());
            Atom *element_type = NULL;
            if (element) {
                (void)petta_block_infer_expr(
                    check, expression->expr.elems[2], environment,
                    NULL, &element_type, depth + 1u);
                if (element_type)
                    (void)petta_block_env_note_fresh(
                        environment, element, element_type);
                else
                    CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                        CETTA_PETTA_TYPECHECK_CENSUS_EVENT_FOLDALL_GENERATOR_ELEMENT_UNKNOWN);
            }
            Atom *call_parts[3] = {
                accumulator, expression->expr.elems[3], element,
            };
            Atom *call = element
                ? atom_expr(&check->scratch, call_parts, 3u) : NULL;
            if (call) {
                return petta_block_infer_expr(
                    check, call, environment, expected,
                    actual_out, depth + 1u);
            }
        }
        petta_block_note_residual_guard(check, expected, "unresolved-foldall");
        if (actual_out)
            *actual_out = expected;
        return true;
    }
    if (head && strcmp(head, "map-atom") == 0 &&
        expression->expr.len == 4u) {
        Atom *source_type = NULL;
        (void)petta_block_infer_expr(
            check, expression->expr.elems[1], environment,
            NULL, &source_type, depth + 1u);
        if (petta_block_head_is(source_type, "List") &&
            source_type->expr.len == 2u) {
            (void)petta_block_bind_pattern(
                check, expression->expr.elems[2],
                source_type->expr.elems[1], environment,
                depth + 1u);
        }
        Atom *element_expected = NULL;
        if (petta_block_head_is(expected, "List") &&
            expected->expr.len == 2u)
            element_expected = expected->expr.elems[1];
        Atom *element_actual = NULL;
        if (!petta_block_infer_expr(
                check, expression->expr.elems[3], environment,
                element_expected, &element_actual, depth + 1u)) {
            return false;
        }
        if (!element_actual)
            element_actual = atom_symbol(&check->scratch, "%Undefined%");
        Atom *actual = petta_block_unary_type(
            check, "List", element_actual);
        return actual && petta_block_check_expected(
                             check, actual, expected, actual_out);
    }
    if (head && strcmp(head, "case") == 0 &&
        expression->expr.len == 3u) {
        Atom *scrutinee_type = NULL;
        if (!petta_block_infer_expr(
                check, expression->expr.elems[1], environment,
                NULL, &scrutinee_type, depth + 1u)) {
            return false;
        }
        Atom *branches = expression->expr.elems[2];
        if (!branches || branches->kind != ATOM_EXPR) {
            if (actual_out)
                *actual_out = expected;
            return true;
        }
        Atom **types = branches->expr.len
            ? cetta_malloc(
                  sizeof(*types) * (size_t)branches->expr.len)
            : NULL;
        size_t type_count = 0u;
        bool known = branches->expr.len > 0u;
        for (CettaExprIndex index = 0u;
             index < branches->expr.len; index++) {
            Atom *branch_expr = branches->expr.elems[index];
            if (!branch_expr || branch_expr->kind != ATOM_EXPR ||
                branch_expr->expr.len != 2u) {
                known = false;
                continue;
            }
            Bindings branch;
            bindings_init(&branch);
            if (!bindings_clone(&branch, environment)) {
                bindings_free(&branch);
                free(types);
                return false;
            }
            Atom *pattern = branch_expr->expr.elems[0];
            if (scrutinee_type &&
                (pattern->kind == ATOM_VAR ||
                 pattern->kind == ATOM_EXPR)) {
                Atom *pattern_type = petta_block_case_fallthrough_type(
                    check, scrutinee_type, branches, index);
                pattern_type = petta_block_case_narrow_pattern_type(
                    check, pattern_type, pattern, branches, index);
                (void)petta_block_bind_pattern(
                    check, pattern, pattern_type,
                    &branch, depth + 1u);
            }
            Atom *branch_type = NULL;
            bool valid = petta_block_infer_expr(
                check, branch_expr->expr.elems[1], &branch,
                expected, &branch_type, depth + 1u);
            bindings_free(&branch);
            if (!valid) {
                free(types);
                return false;
            }
            /* A bottom branch contributes no value, hence no candidate. */
            if (petta_block_expression_is_bottom(
                    branch_expr->expr.elems[1], 0u))
                continue;
            types[type_count++] = branch_type;
            known = known && branch_type != NULL;
        }
        if (actual_out) {
            CETTA_PETTA_TYPECHECK_CENSUS_HIT_IF(
                !expected && check->inferring_signature &&
                    petta_block_expression_is_bottom(expression, 0u),
                CETTA_PETTA_TYPECHECK_CENSUS_EVENT_INFERENCE_ALL_BOTTOM_CASE_UNPUBLISHED);
            *actual_out = expected ? expected
                : (known && type_count > 0u
                    ? petta_block_join_actual_types(
                          check, types, type_count)
                    : NULL);
        }
        free(types);
        return true;
    }
    if (head && strcmp(head, "match") == 0 &&
        expression->expr.len == 4u) {
        Atom *pattern = expression->expr.elems[2];
        Atom *schema = petta_block_find_space_schema(
            check, expression->expr.elems[1]);
        if (!schema)
            schema = petta_block_find_relation_schema(check, pattern);
        Bindings branch;
        bindings_init(&branch);
        if (!bindings_clone(&branch, environment)) {
            bindings_free(&branch);
            return false;
        }
        size_t dynamic_mark = check->dynamic_pattern_var_len;
        bool pattern_ok = true;
        if (schema) {
            pattern_ok = petta_block_bind_pattern(
                check, pattern, schema, &branch, depth + 1u);
        } else if (petta_block_head_is(pattern, ":") &&
                   pattern->expr.len == 3u &&
                   pattern->expr.elems[1]->kind == ATOM_VAR) {
            pattern_ok = petta_block_env_note_pattern(
                check, &branch, pattern->expr.elems[1],
                pattern->expr.elems[2]);
        } else {
            pattern_ok = petta_block_note_dynamic_pattern_vars(
                check, pattern, &branch, depth + 1u);
        }
        if (!pattern_ok) {
            check->definite_mismatch = true;
            check->dynamic_pattern_var_len = dynamic_mark;
            bindings_free(&branch);
            return false;
        }
        Atom *actual = NULL;
        bool body_ok = petta_block_infer_expr(
            check, expression->expr.elems[3], &branch,
            expected, &actual, depth + 1u);
        check->dynamic_pattern_var_len = dynamic_mark;
        if (body_ok) {
            bindings_replace(environment, &branch);
            if (actual_out)
                *actual_out = actual ? actual : expected;
        }
        bindings_free(&branch);
        return body_ok;
    }
    if (head && strcmp(head, "data") == 0) {
        CettaExprLen field_count = expression->expr.len - 1u;
        Atom *product_expected = expected;
        if (expected && expected->kind == ATOM_SYMBOL) {
            Atom *alias = petta_block_decl_representation(
                check, expected, "Alias");
            if (alias)
                product_expected = alias;
        }
        bool contextual_product = product_expected &&
            product_expected->kind == ATOM_EXPR &&
            product_expected->expr.len == field_count &&
            !petta_block_head_is(product_expected, "|") &&
            !petta_block_head_is(product_expected, "List") &&
            !petta_block_arrow_head(product_expected, NULL);
        Atom **field_types = field_count
            ? cetta_malloc(
                  sizeof(*field_types) * (size_t)field_count)
            : NULL;
        bool ok = true;
        for (CettaExprIndex index = 0u;
             index < field_count; index++) {
            field_types[index] = NULL;
            Atom *field_expected = contextual_product
                ? product_expected->expr.elems[index] : NULL;
            Atom *payload_expected = field_expected;
            if (field_expected) {
                Atom *representation = petta_block_decl_representation(
                    check, field_expected, "Newtype");
                if (representation)
                    payload_expected = representation;
            }
            if (!petta_block_infer_expr(
                    check, expression->expr.elems[index + 1u],
                    environment, payload_expected, &field_types[index],
                    depth + 1u) || !field_types[index]) {
                ok = false;
                break;
            }
            if (field_expected &&
                petta_block_decl_representation(
                    check, field_expected, "Newtype")) {
                field_types[index] = field_expected;
            }
        }
        Atom *actual = ok
            ? atom_expr(&check->scratch, field_types, field_count)
            : NULL;
        free(field_types);
        if (!ok) {
            petta_block_note_residual_guard(check, expected, "unresolved-data");
            if (actual_out)
                *actual_out = expected;
            return true;
        }
        return petta_block_check_expected(
            check, actual, expected, actual_out);
    }
    if (head &&
        (strcmp(head, "let") == 0 || strcmp(head, "chain") == 0) &&
        expression->expr.len == 4u) {
        CettaExprIndex pattern_index = strcmp(head, "let") == 0 ? 1u : 2u;
        CettaExprIndex value_index = strcmp(head, "let") == 0 ? 2u : 1u;
        Atom *value_type = NULL;
        if (!petta_block_infer_expr(
                check, expression->expr.elems[value_index], environment,
                NULL, &value_type, depth + 1u)) {
            return false;
        }
        Bindings body;
        bindings_init(&body);
        if (!bindings_clone(&body, environment)) {
            bindings_free(&body);
            return false;
        }
        if (value_type) {
            (void)petta_block_bind_pattern(
                check, expression->expr.elems[pattern_index], value_type,
                &body, depth + 1u);
        }
        bool ok = petta_block_infer_expr(
            check, expression->expr.elems[3], &body,
            expected, actual_out, depth + 1u);
        bindings_free(&body);
        return ok;
    }
    if (head) {
        if (petta_block_infer_named_call(
                check, expression, environment,
                expected, actual_out, depth))
            return true;
        if (check->definite_mismatch)
            return false;
        /* PeTTa uses the same parenthesized notation for a call and for
         * list data.  Once no signature applies, an enclosing List contract
         * supplies the missing interpretation; check every field as data
         * instead of inventing a call for the symbolic first element. */
        if (petta_block_head_is(expected, "List") &&
            expected->expr.len == 2u) {
            return petta_block_infer_list_elements(
                check, expression, 0u, environment, expected,
                actual_out, depth + 1u);
        }
        if (petta_block_head_is_callable(
                check, expression->expr.elems[0],
                expression->expr.len - 1u)) {
            petta_block_note_residual_guard(check, expected, "untyped-callable");
            if (actual_out)
                *actual_out = expected;
            return true;
        }
    }
    if (head && petta_block_deferred_form(head)) {
        petta_block_note_residual_guard(check, expected, "deferred-form");
        if (actual_out)
            *actual_out = expected;
        return true;
    }

    Atom *structural = petta_block_infer_structural_value_type(
        check, expression, environment, expected, depth + 1u);
    return structural && petta_block_check_expected(
                             check, structural, expected, actual_out);
}

static bool petta_block_find_signature(
    PettaBlockCheck *check, Atom *subject, CettaExprLen arity,
    Atom **signature_out) {
    *signature_out = NULL;
    const PettaBlockDeclarationBucket *bucket =
        petta_block_declaration_bucket_const(check, subject);
    for (size_t index = bucket ? bucket->first : PETTA_DECL_INDEX_NONE;
         index != PETTA_DECL_INDEX_NONE;
         index = check->declarations[index].next_same_subject) {
        PettaBlockDeclaration *decl = &check->declarations[index];
        if (petta_block_arrow_head(decl->type, NULL) &&
            decl->type->expr.len == arity + 2u) {
            *signature_out = decl->type;
            return true;
        }
    }
    Atom **types = NULL;
    uint32_t count = petta_block_declared_types(
        check, subject, &types);
    for (uint32_t index = 0u; index < count; index++) {
        if (petta_block_arrow_head(types[index], NULL) &&
            types[index]->expr.len == arity + 2u) {
            *signature_out = types[index];
            break;
        }
    }
    free(types);
    if (!*signature_out && check->program &&
        subject->kind == ATOM_SYMBOL) {
        (void)petta_block_inferred_signature_lookup(
            check, subject->sym_id, arity, signature_out);
    }
    return *signature_out != NULL;
}

static bool petta_block_signature_is_inferred(
    PettaBlockCheck *check, Atom *subject, Atom *signature) {
    if (!check || !subject || !signature)
        return false;
    const PettaBlockDeclarationBucket *bucket =
        petta_block_declaration_bucket_const(check, subject);
    for (size_t index = bucket ? bucket->first : PETTA_DECL_INDEX_NONE;
         index != PETTA_DECL_INDEX_NONE;
         index = check->declarations[index].next_same_subject) {
        PettaBlockDeclaration *decl = &check->declarations[index];
        if (decl->inferred && decl->type == signature)
            return true;
    }
    return false;
}

static bool petta_block_current_signature_head(
    PettaBlockCheck *check, Atom *head, CettaExprLen arity) {
    if (!check || !head)
        return false;
    const PettaBlockDeclarationBucket *bucket =
        petta_block_declaration_bucket_const(check, head);
    for (size_t index = bucket ? bucket->first : PETTA_DECL_INDEX_NONE;
         index != PETTA_DECL_INDEX_NONE;
         index = check->declarations[index].next_same_subject) {
        PettaBlockDeclaration *decl = &check->declarations[index];
        if (petta_block_arrow_head(decl->type, NULL) &&
            decl->type->expr.len == arity + 2u)
            return true;
    }
    return false;
}

static bool petta_block_pattern_fragment_supported(Atom *pattern) {
    /* Clause heads are patterns, not evaluator calls.  Their complete Atom
     * syntax is interpreted by petta_block_bind_pattern, including nested
     * products, constructors, lists, and as-patterns. */
    return pattern != NULL;
}

static bool petta_block_expression_fragment_supported(
    PettaBlockCheck *check, Atom *expression, uint32_t depth) {
    if (!check || !expression || depth > 2048u)
        return false;
    if (expression->kind != ATOM_EXPR)
        return true;
    if (expression->expr.len == 0u)
        return true;
    Atom *head_atom = expression->expr.elems[0];
    const char *head = petta_block_symbol_name(head_atom);
    if (!head) {
        for (CettaExprIndex index =
                 head_atom->kind == ATOM_VAR ? 1u : 0u;
             index < expression->expr.len; index++) {
            if (!petta_block_expression_fragment_supported(
                    check, expression->expr.elems[index], depth + 1u)) {
                return false;
            }
        }
        return true;
    }
    if (strcmp(head, "=") == 0)
        return expression->expr.len == 3u;
    if (strcmp(head, "|->") == 0 && expression->expr.len == 3u) {
        /* Lambda parameters are binder syntax, not evaluator calls.  The
         * body is the executable part whose supported fragment matters. */
        return petta_block_expression_fragment_supported(
            check, expression->expr.elems[2], depth + 1u);
    }
    if ((strcmp(head, "let") == 0 || strcmp(head, "chain") == 0) &&
        expression->expr.len == 4u) {
        return petta_block_expression_fragment_supported(
                   check, expression->expr.elems[
                       strcmp(head, "let") == 0 ? 2u : 1u],
                   depth + 1u) &&
               petta_block_expression_fragment_supported(
                   check, expression->expr.elems[3], depth + 1u);
    }
    if (strcmp(head, "quote") == 0)
        return expression->expr.len == 2u;
    if (strcmp(head, "eval") == 0 && expression->expr.len == 2u) {
        Atom *quoted = expression->expr.elems[1];
        return !petta_block_head_is(quoted, "quote") ||
               (quoted->expr.len == 2u &&
                petta_block_expression_fragment_supported(
                    check, quoted->expr.elems[1], depth + 1u));
    }
    if (expression->expr.len == 2u &&
        (strcmp(head, "once") == 0 ||
         strcmp(head, "reduce") == 0 ||
         strcmp(head, "transaction") == 0 ||
         strcmp(head, "catch") == 0 ||
         strcmp(head, "collapse") == 0)) {
        return petta_block_expression_fragment_supported(
            check, expression->expr.elems[1], depth + 1u);
    }
    if (expression->expr.len == 3u &&
        (strcmp(head, "sealed") == 0 ||
         strcmp(head, "with_mutex") == 0)) {
        return petta_block_expression_fragment_supported(
            check, expression->expr.elems[2], depth + 1u);
    }
    if (strcmp(head, "progn") == 0 && expression->expr.len >= 2u) {
        for (CettaExprIndex index = 1u;
             index < expression->expr.len; index++) {
            if (!petta_block_expression_fragment_supported(
                    check, expression->expr.elems[index], depth + 1u))
                return false;
        }
        return true;
    }
    if (expression->expr.len == 2u &&
        (strcmp(head, "superpose") == 0 ||
         strcmp(head, "hyperpose") == 0)) {
        Atom *alternatives = expression->expr.elems[1];
        if (!alternatives || alternatives->kind != ATOM_EXPR)
            return false;
        for (CettaExprIndex index = 0u;
             index < alternatives->expr.len; index++) {
            if (!petta_block_expression_fragment_supported(
                    check, alternatives->expr.elems[index], depth + 1u))
                return false;
        }
        return true;
    }
    if (strcmp(head, "forall") == 0 && expression->expr.len == 3u)
        return true;
    if (strcmp(head, "foldall") == 0 && expression->expr.len == 4u)
        return expression->expr.elems[1]->kind == ATOM_SYMBOL;
    if (strcmp(head, "map-atom") == 0 && expression->expr.len == 4u) {
        return petta_block_expression_fragment_supported(
            check, expression->expr.elems[3], depth + 1u);
    }
    if (strcmp(head, "case") == 0 && expression->expr.len == 3u) {
        Atom *branches = expression->expr.elems[2];
        if (!branches || branches->kind != ATOM_EXPR)
            return false;
        for (CettaExprIndex index = 0u;
             index < branches->expr.len; index++) {
            Atom *branch = branches->expr.elems[index];
            if (!branch || branch->kind != ATOM_EXPR ||
                branch->expr.len != 2u ||
                !petta_block_expression_fragment_supported(
                    check, branch->expr.elems[1], depth + 1u)) {
                return false;
            }
        }
        return true;
    }
    if (strcmp(head, "match") == 0) {
        /*
         * Match patterns are checked against their SpaceOf schema by
         * petta_block_bind_pattern.  They are data for that judgment, not
         * calls that must already have standalone function signatures.
         */
        return expression->expr.len == 4u &&
               petta_block_expression_fragment_supported(
                   check, expression->expr.elems[3], depth + 1u);
    }
    if (strcmp(head, "if") == 0 && expression->expr.len != 4u)
        return false;
    bool known_form = strcmp(head, "map-flat") == 0 ||
                      strcmp(head, "make-list") == 0 ||
                      strcmp(head, "cons") == 0 ||
                      strcmp(head, "brand") == 0 ||
                      strcmp(head, "the") == 0 ||
                      strcmp(head, "if") == 0 ||
                      strcmp(head, "call") == 0 ||
                      strcmp(head, "data") == 0 ||
                      strcmp(head, "+") == 0 ||
                      strcmp(head, "-") == 0 ||
                      strcmp(head, "*") == 0 ||
                      strcmp(head, "/") == 0 ||
                      strcmp(head, "mod") == 0 ||
                      strcmp(head, "pow-math") == 0 ||
                      strcmp(head, "<") == 0 ||
                      strcmp(head, "<=") == 0 ||
                      strcmp(head, ">") == 0 ||
                      strcmp(head, ">=") == 0 ||
                      strcmp(head, "and-then") == 0 ||
                      strcmp(head, "or-else") == 0 ||
                      strcmp(head, "not") == 0 ||
                      petta_block_current_signature_head(
                          check, head_atom,
                          expression->expr.len - 1u);
    if (!known_form) {
        Atom *live_signature = NULL;
        known_form = petta_block_find_signature(
            check, head_atom, expression->expr.len - 1u,
            &live_signature);
    }
    if (!known_form) {
        /* An undeclared nullary expression is inert constructor data. */
        return expression->expr.len == 1u;
    }
    for (CettaExprIndex index = 1u;
         index < expression->expr.len; index++) {
        if (!petta_block_expression_fragment_supported(
                check, expression->expr.elems[index], depth + 1u))
            return false;
    }
    return true;
}

static bool petta_block_inert_structural_result(
    PettaBlockCheck *check, Atom *expression) {
    if (!check || !expression || expression->kind != ATOM_EXPR ||
        expression->expr.len == 0u ||
        expression->expr.elems[0]->kind != ATOM_SYMBOL) {
        return false;
    }
    Atom *head = expression->expr.elems[0];
    CettaExprLen arity = expression->expr.len - 1u;
    Atom *signature = NULL;
    const char *head_name = petta_block_symbol_name(head);
    if (petta_block_deferred_form(head_name) ||
        petta_block_declared_value_type(check, head) ||
        petta_block_find_signature(check, head, arity, &signature) ||
        petta_block_head_is_callable(check, head, arity)) {
        return false;
    }
    return !cetta_petta_source_head_has_runtime_meaning(
        check->space, head->sym_id, arity);
}

static bool petta_block_seed_pattern_variables(
    PettaBlockCheck *check, Atom *pattern,
    Bindings *environment, uint32_t depth) {
    if (!check || !pattern || !environment || depth > 2048u)
        return false;
    if (pattern->kind == ATOM_VAR) {
        if (bindings_lookup_var(environment, pattern))
            return true;
        Atom *unknown = atom_var_with_id(
            &check->scratch, "inferred-type", fresh_var_id());
        return unknown &&
               petta_block_env_note_fresh(environment, pattern, unknown);
    }
    if (pattern->kind != ATOM_EXPR)
        return true;
    for (CettaExprIndex index = 0u;
         index < pattern->expr.len; index++) {
        if (!petta_block_seed_pattern_variables(
                check, pattern->expr.elems[index],
                environment, depth + 1u))
            return false;
    }
    return true;
}

static Atom *petta_block_inferred_parameter_type(
    PettaBlockCheck *check, Atom *pattern,
    Bindings *environment) {
    if (!check || !pattern || !environment)
        return NULL;
    if (petta_block_inference_parameter_tainted(check, pattern))
        return atom_symbol(&check->scratch, "%Undefined%");
    Atom *type = NULL;
    if (pattern->kind == ATOM_VAR)
        type = bindings_lookup_var(environment, pattern);
    else
        type = petta_block_pattern_type(
            check, pattern, environment, 0u);
    if (!type)
        return atom_symbol(&check->scratch, "%Undefined%");
    return petta_block_apply_type_bindings(check, environment, type);
}

/* Roman's inferred signatures publish positive knowledge only.  Atomic
 * types, proper-list structure, and closed arrows are stable evidence;
 * every other inferred output shape is widened to the unknown marker.  In
 * particular, an internal union produced while checking alternatives is not
 * itself a persistent inferred contract. */
static Atom *petta_block_normalize_inferred_output(
    PettaBlockCheck *check, Atom *type, uint32_t depth) {
    if (!check || !type || depth > 2048u)
        return NULL;
    if (type->kind == ATOM_VAR)
        return atom_symbol(&check->scratch, "%Undefined%");
    if (type->kind == ATOM_SYMBOL)
        return type;
    if (type->kind != ATOM_EXPR || atom_has_vars(type))
        return atom_symbol(&check->scratch, "%Undefined%");
    if (petta_block_head_is(type, "List") && type->expr.len == 2u) {
        Atom *element = petta_block_normalize_inferred_output(
            check, type->expr.elems[1], depth + 1u);
        return element
            ? petta_block_unary_type(check, "List", element) : NULL;
    }
    if (petta_block_arrow_head(type, NULL))
        return type;
    CETTA_PETTA_TYPECHECK_CENSUS_HIT(
        CETTA_PETTA_TYPECHECK_CENSUS_EVENT_INFERENCE_OUTPUT_SHAPE_WIDENS_UNKNOWN);
    return atom_symbol(&check->scratch, "%Undefined%");
}

static bool petta_block_signature_has_information(Atom *signature) {
    if (!signature || signature->kind != ATOM_EXPR)
        return false;
    for (CettaExprIndex index = 1u;
         index < signature->expr.len; index++) {
        Atom *type = signature->expr.elems[index];
        if (type->kind != ATOM_VAR && !petta_block_wildcard(type))
            return true;
    }
    return false;
}

static Atom *petta_block_infer_equation_signature(
    PettaBlockCheck *check, Atom *equation) {
    if (!petta_block_head_is(equation, "=") ||
        equation->expr.len != 3u)
        return NULL;
    Atom *lhs = equation->expr.elems[1];
    Atom *rhs = equation->expr.elems[2];
    if (!lhs || lhs->kind != ATOM_EXPR || lhs->expr.len == 0u ||
        lhs->expr.elems[0]->kind != ATOM_SYMBOL ||
        !petta_block_expression_fragment_supported(check, rhs, 0u))
        return NULL;
    CettaExprLen arity = lhs->expr.len - 1u;
    Atom *subject = lhs->expr.elems[0];
    const PettaBlockDeclarationBucket *bucket =
        petta_block_declaration_bucket_const(check, subject);
    for (size_t index = bucket ? bucket->first : PETTA_DECL_INDEX_NONE;
         index != PETTA_DECL_INDEX_NONE;
         index = check->declarations[index].next_same_subject) {
        PettaBlockDeclaration *declaration = &check->declarations[index];
        if (!declaration->inferred &&
            petta_block_arrow_head(declaration->type, NULL) &&
            declaration->type->expr.len == arity + 2u) {
            return NULL;
        }
    }
    Atom **published = NULL;
    uint32_t published_count = petta_block_declared_types(
        check, subject, &published);
    bool explicitly_declared = false;
    for (uint32_t index = 0u; index < published_count; index++) {
        if (petta_block_arrow_head(published[index], NULL) &&
            published[index]->expr.len == arity + 2u) {
            explicitly_declared = true;
            break;
        }
    }
    free(published);
    if (explicitly_declared)
        return NULL;

    Bindings environment;
    bindings_init(&environment);
    bool ok = true;
    for (CettaExprIndex index = 1u;
         index < lhs->expr.len; index++) {
        if (!petta_block_seed_pattern_variables(
                check, lhs->expr.elems[index],
                &environment, 0u)) {
            ok = false;
            break;
        }
    }
    Atom *output = NULL;
    bool saved_mismatch = check->definite_mismatch;
    bool saved_inferring = check->inferring_signature;
    check->definite_mismatch = false;
    check->inferring_signature = true;
    check->inference_tainted_var_len = 0u;
    if (ok)
        ok = petta_block_infer_expr(
            check, rhs, &environment, NULL, &output, 0u);
    check->inferring_signature = saved_inferring;
    check->definite_mismatch = saved_mismatch;
    if (!ok || !output) {
        bindings_free(&environment);
        return NULL;
    }
    output = petta_block_apply_type_bindings(
        check, &environment, output);
    output = petta_block_normalize_inferred_output(check, output, 0u);
    if (!output) {
        bindings_free(&environment);
        return NULL;
    }
    Atom **elements = cetta_malloc(
        sizeof(*elements) * (size_t)(arity + 2u));
    if (!elements) {
        bindings_free(&environment);
        petta_block_fault(
            check, PETTA_TYPECHECK_FAULT_ALLOCATION,
            "could not allocate inferred signature");
        return NULL;
    }
    elements[0] = atom_symbol(&check->scratch, "->");
    for (CettaExprIndex index = 0u; index < arity; index++) {
        elements[index + 1u] = petta_block_inferred_parameter_type(
            check, lhs->expr.elems[index + 1u], &environment);
        if (!elements[index + 1u])
            ok = false;
    }
    elements[arity + 1u] = output;
    Atom *signature = ok
        ? atom_expr(&check->scratch, elements, arity + 2u)
        : NULL;
    free(elements);
    bindings_free(&environment);
    return petta_block_signature_has_information(signature) ? signature : NULL;
}

static bool petta_block_infer_signatures(PettaBlockCheck *check) {
    if (!check)
        return false;
    size_t pass_limit = check->form_count + 1u;
    for (size_t pass = 0u; pass < pass_limit; pass++) {
        bool added = false;
        for (size_t index = 0u; index < check->form_count; index++) {
            Atom *form = check->forms[index];
            Atom *signature = petta_block_infer_equation_signature(
                check, form);
            if (!signature)
                continue;
            Atom *lhs = form->expr.elems[1];
            if (petta_block_has_declaration(
                    check, lhs->expr.elems[0], signature)) {
                continue;
            }
            if (!petta_block_add_inferred_declaration(
                    check, lhs->expr.elems[0], signature)) {
                return petta_block_fault(
                    check, PETTA_TYPECHECK_FAULT_ALLOCATION,
                    "could not retain inferred PeTTa signature");
            }
            added = true;
        }
        if (!added)
            return true;
    }
    return true;
}

static bool petta_block_check_exec_payloads(
    PettaBlockCheck *check) {
    if (!check)
        return false;
    for (size_t index = check->form_check_start;
         index < check->form_count; index++) {
        Atom *payload = NULL;
        size_t consumed = 1u;
        if (!parser_syn_exec_payload(check->forms[index], &payload)) {
            const char *name = petta_block_symbol_name(check->forms[index]);
            if (!name || strcmp(name, "!") != 0 ||
                index + 1u >= check->form_count)
                continue;
            payload = check->forms[index + 1u];
            consumed = 2u;
        }
        Bindings effect_environment;
        bindings_init(&effect_environment);
        bool effects_ok = petta_block_validate_effect_arguments(
            check, payload, &effect_environment, 0u);
        bindings_free(&effect_environment);
        if (!effects_ok)
            return false;
        bool supported = petta_block_expression_fragment_supported(
            check, payload, 0u);
        if (!supported && payload->kind == ATOM_EXPR &&
            payload->expr.len > 0u) {
            Atom *signature = NULL;
            supported = petta_block_find_signature(
                check, payload->expr.elems[0],
                payload->expr.len - 1u, &signature);
        }
        if (!supported)
            continue;
        Bindings environment;
        bindings_init(&environment);
        Atom *actual = NULL;
        check->definite_mismatch = false;
        check->residual_type_guard_required = false;
        bool ok = petta_block_infer_expr(
            check, payload, &environment, NULL, &actual, 0u);
        bool residual_required =
            check->residual_type_guard_required;
        check->residual_type_guard_required = false;
        bindings_free(&environment);
        if (!ok && check->definite_mismatch) {
            const char *head = payload->kind == ATOM_EXPR &&
                    payload->expr.len > 0u
                ? petta_block_symbol_name(payload->expr.elems[0])
                : NULL;
            return petta_block_fail(
                check, "call arguments conflict with inferred type for %s",
                head ? head : "expression");
        }
        if (residual_required &&
            check->policy != PETTA_TYPECHECK_POLICY_DEFAULT) {
            return petta_block_fail(
                check, "strict typing requires a residual type guard in executable expression");
        }
        index += consumed - 1u;
    }
    return true;
}

typedef PettaAnalysisCardinality PettaBlockEffect;

#define PETTA_BLOCK_EFFECT_DET PETTA_ANALYSIS_CARDINALITY_DETERMINISTIC
#define PETTA_BLOCK_EFFECT_SEMIDET PETTA_ANALYSIS_CARDINALITY_SEMIDETERMINISTIC
#define PETTA_BLOCK_EFFECT_NONDET PETTA_ANALYSIS_CARDINALITY_NONDETERMINISTIC
#define PETTA_BLOCK_EFFECT_UNKNOWN PETTA_ANALYSIS_CARDINALITY_UNDETERMINED

static PettaBlockEffect petta_block_effect_join(
    PettaBlockEffect left, PettaBlockEffect right) {
    return petta_analysis_cardinality_join(left, right);
}

static bool petta_block_name_in(
    const char *name, const char *const *names, size_t count) {
    if (!name)
        return false;
    for (size_t index = 0u; index < count; index++) {
        if (strcmp(name, names[index]) == 0)
            return true;
    }
    return false;
}

static size_t petta_block_equation_count(
    PettaBlockCheck *check, Atom *head, CettaExprLen arity) {
    if (!check || !head || head->kind != ATOM_SYMBOL)
        return 0u;
    for (size_t index = 0u;
         index < check->relation_view_len; index++) {
        PettaBlockRelationView *view = &check->relation_views[index];
        if (view->head == head->sym_id && view->arity == arity)
            return view->equation_len;
    }

    if (!petta_block_reserve(
            (void **)&check->relation_views,
            &check->relation_view_cap,
            check->relation_view_len + 1u,
            sizeof(*check->relation_views))) {
        petta_block_fault(
            check, PETTA_TYPECHECK_FAULT_ALLOCATION,
            "could not retain the live PeTTa relation view");
        return 0u;
    }
    PettaBlockRelationView *view =
        &check->relation_views[check->relation_view_len++];
    *view = (PettaBlockRelationView){
        .head = head->sym_id,
        .arity = arity,
    };

    PettaClauseCandidate *live = NULL;
    size_t live_len = 0u;
    if (check->program && !check->forms_include_live_equations &&
        !petta_program_clause_snapshot(
            check->program, check->space, head->sym_id,
            &live, &live_len)) {
        petta_block_fault(
            check, PETTA_TYPECHECK_FAULT_ALLOCATION,
            "could not read the live PeTTa relation view");
        return 0u;
    }
    size_t current_len = 0u;
    for (size_t index = 0u; index < check->form_count; index++) {
        Atom *form = check->forms[index];
        Atom *lhs = petta_block_head_is(form, "=") &&
                    form->expr.len == 3u
            ? form->expr.elems[1] : NULL;
        if (lhs && lhs->kind == ATOM_EXPR &&
            lhs->expr.len == arity + 1u &&
            atom_eq(lhs->expr.elems[0], head))
            current_len++;
    }
    size_t matching_live = 0u;
    for (size_t index = 0u; index < live_len; index++) {
        Atom *equation = live[index].equation;
        Atom *lhs = petta_block_head_is(equation, "=") &&
                    equation->expr.len == 3u
            ? equation->expr.elems[1] : NULL;
        if (lhs && lhs->kind == ATOM_EXPR &&
            lhs->expr.len == arity + 1u &&
            atom_eq(lhs->expr.elems[0], head))
            matching_live++;
    }
    if (matching_live > SIZE_MAX - current_len ||
        (matching_live + current_len) >
            SIZE_MAX / sizeof(*view->equations)) {
        free(live);
        petta_block_fault(
            check, PETTA_TYPECHECK_FAULT_ALLOCATION,
            "PeTTa relation view is too large");
        return 0u;
    }
    view->equation_len = matching_live + current_len;
    view->equations = view->equation_len
        ? cetta_malloc(sizeof(*view->equations) * view->equation_len)
        : NULL;
    size_t write = 0u;
    for (size_t index = 0u; index < live_len; index++) {
        Atom *equation = live[index].equation;
        Atom *lhs = petta_block_head_is(equation, "=") &&
                    equation->expr.len == 3u
            ? equation->expr.elems[1] : NULL;
        if (lhs && lhs->kind == ATOM_EXPR &&
            lhs->expr.len == arity + 1u &&
            atom_eq(lhs->expr.elems[0], head))
            view->equations[write++] = equation;
    }
    free(live);
    for (size_t index = 0u; index < check->form_count; index++) {
        Atom *form = check->forms[index];
        Atom *lhs = petta_block_head_is(form, "=") &&
                    form->expr.len == 3u
            ? form->expr.elems[1] : NULL;
        if (lhs && lhs->kind == ATOM_EXPR &&
            lhs->expr.len == arity + 1u &&
            atom_eq(lhs->expr.elems[0], head))
            view->equations[write++] = form;
    }
    return view->equation_len;
}

static Atom *const *petta_block_equations(
    PettaBlockCheck *check, Atom *head, CettaExprLen arity,
    size_t *count_out) {
    size_t count = petta_block_equation_count(check, head, arity);
    if (count_out)
        *count_out = count;
    if (!check || !head || head->kind != ATOM_SYMBOL)
        return NULL;
    for (size_t index = 0u;
         index < check->relation_view_len; index++) {
        PettaBlockRelationView *view = &check->relation_views[index];
        if (view->head == head->sym_id && view->arity == arity)
            return view->equations;
    }
    return NULL;
}

static PettaBlockEffect petta_block_signature_effect(Atom *signature) {
    const char *mode = NULL;
    if (!petta_block_arrow_head(signature, &mode))
        return PETTA_BLOCK_EFFECT_UNKNOWN;
    if (strcmp(mode, "-[det]->") == 0 ||
        strcmp(mode, "-[deterministic]->") == 0)
        return PETTA_BLOCK_EFFECT_DET;
    if (strcmp(mode, "-[semidet]->") == 0 ||
        strcmp(mode, "-[semideterministic]->") == 0)
        return PETTA_BLOCK_EFFECT_SEMIDET;
    if (strcmp(mode, "-[nondet]->") == 0 ||
        strcmp(mode, "-[nondeterministic]->") == 0)
        return PETTA_BLOCK_EFFECT_NONDET;
    return PETTA_BLOCK_EFFECT_UNKNOWN;
}

static bool petta_block_signature_contains_plain_arrow(
    Atom *type, bool root) {
    if (!type || type->kind != ATOM_EXPR)
        return false;
    const char *mode = NULL;
    if (petta_block_arrow_head(type, &mode) &&
        strcmp(mode, "->") == 0 && !root)
        return true;
    for (CettaExprIndex index = 1u; index < type->expr.len; index++) {
        if (petta_block_signature_contains_plain_arrow(
                type->expr.elems[index], false))
            return true;
    }
    return false;
}

static bool petta_block_type_is_nominal(
    PettaBlockCheck *check, Atom *type) {
    if (!check || !type || type->kind != ATOM_SYMBOL)
        return false;
    if (petta_block_decl_representation(check, type, "Newtype"))
        return true;
    for (size_t index = 0u; index < check->declaration_len; index++) {
        PettaBlockDeclaration *decl = &check->declarations[index];
        if (!decl->inferred && atom_eq(decl->subject, type) &&
            petta_block_symbol_name(decl->type) &&
            strcmp(petta_block_symbol_name(decl->type), "Type") == 0)
            return true;
        if (decl->inferred ||
            !petta_block_arrow_head(decl->type, NULL))
            continue;
        CettaExprLen arity = decl->type->expr.len - 2u;
        Atom *result = decl->type->expr.elems[
            decl->type->expr.len - 1u];
        if (atom_eq(result, type) &&
            petta_block_equation_count(
                check, decl->subject, arity) == 0u)
            return true;
    }
    Atom **declared = NULL;
    uint32_t count = petta_block_declared_types(
        check, type, &declared);
    bool nominal = false;
    for (uint32_t index = 0u; index < count; index++) {
        const char *name = petta_block_symbol_name(declared[index]);
        if (name && strcmp(name, "Type") == 0) {
            nominal = true;
            break;
        }
    }
    free(declared);
    return nominal;
}

static PettaBlockEffect petta_block_expression_effect(
    PettaBlockCheck *check, Atom *expression,
    Bindings *environment, uint32_t depth);
static const char *petta_block_effect_arrow_name(Atom *type);

static bool petta_block_type_provably_nonfunction(Atom *type) {
    const char *name = petta_block_symbol_name(type);
    if (name && (strcmp(name, "Number") == 0 ||
                 strcmp(name, "String") == 0 ||
                 strcmp(name, "Bool") == 0)) {
        return true;
    }
    return type && type->kind == ATOM_EXPR &&
           !petta_block_arrow_head(type, NULL);
}

/*
 * PeTTa evaluates a compound expression in head position before deciding
 * whether the enclosing spine is data or application.  A compound head is
 * manifest data only when its own head is data, or when its unique callable
 * signature proves that evaluating it yields a non-function value.  This is
 * the native counterpart of the reference translator's data_headed/
 * nonfunction_type decision.
 */
static bool petta_block_compound_head_is_data(
    PettaBlockCheck *check, Atom *head,
    Bindings *environment, uint32_t depth) {
    if (!check || !head || depth > 128u)
        return false;
    if (head->kind == ATOM_VAR) {
        Atom *known = environment
            ? bindings_lookup_var(environment, head) : NULL;
        return petta_block_type_provably_nonfunction(known);
    }
    if (head->kind == ATOM_SYMBOL)
        return !petta_block_head_is_callable(check, head, 0u);
    if (head->kind != ATOM_EXPR || head->expr.len == 0u)
        return head->kind == ATOM_EXPR;

    Atom *inner_head = head->expr.elems[0];
    CettaExprLen inner_arity = head->expr.len - 1u;
    if (inner_head->kind == ATOM_SYMBOL &&
        !petta_block_head_is_callable(
            check, inner_head, inner_arity)) {
        return true;
    }
    if (inner_head->kind != ATOM_SYMBOL &&
        petta_block_compound_head_is_data(
            check, inner_head, environment, depth + 1u)) {
        return true;
    }
    if (inner_head->kind != ATOM_SYMBOL)
        return false;

    Atom *signature = NULL;
    return petta_block_find_signature(
               check, inner_head, inner_arity, &signature) &&
           signature && signature->expr.len == inner_arity + 2u &&
           petta_block_type_provably_nonfunction(
               signature->expr.elems[inner_arity + 1u]);
}

static bool petta_block_value_manifest_proper_list(
    PettaBlockCheck *check, Atom *value,
    Bindings *environment, bool require_nonempty) {
    if (!check || !value)
        return false;
    if (value->kind == ATOM_VAR && environment) {
        Atom *type = bindings_lookup_var(environment, value);
        if (petta_block_head_is(type, "List"))
            return !require_nonempty;
        if (type && type->kind == ATOM_EXPR && type->expr.len > 0u &&
            !petta_block_arrow_head(type, NULL))
            return true;
        return false;
    }
    if (value->kind != ATOM_EXPR)
        return false;
    if (value->expr.len == 0u)
        return !require_nonempty;
    if (petta_block_head_is(value, "cons") && value->expr.len == 3u) {
        return petta_block_value_manifest_proper_list(
            check, value->expr.elems[2], environment, false);
    }
    Atom *head = value->expr.elems[0];
    if (head->kind == ATOM_VAR || head->kind == ATOM_EXPR)
        return petta_block_compound_head_is_data(
            check, head, environment, 0u);
    if (head->kind != ATOM_SYMBOL)
        return true;
    const char *head_name = petta_block_symbol_name(head);
    if (head_name &&
        (petta_block_deferred_form(head_name) ||
         petta_block_head_is_callable(
             check, head, value->expr.len - 1u)))
        return false;
    return true;
}

static bool petta_block_value_is_proven_proper_list(
    PettaBlockCheck *check, Atom *value,
    Bindings *environment, uint32_t depth) {
    if (!check || !value || depth > 128u)
        return false;
    if (petta_block_value_manifest_proper_list(
            check, value, environment, false))
        return true;
    if (!value || value->kind != ATOM_EXPR || value->expr.len == 0u)
        return false;
    const char *head = petta_block_symbol_name(value->expr.elems[0]);
    if (head && strcmp(head, "quote") == 0 &&
        value->expr.len == 2u)
        return value->expr.elems[1]->kind == ATOM_EXPR;
    if (head && (strcmp(head, "make-list") == 0 ||
                 strcmp(head, "list_to_set") == 0 ||
                 strcmp(head, "list-to-set") == 0))
        return true;
    if (head && strcmp(head, "collapse") == 0)
        return true;
    if (head && strcmp(head, "cons") == 0 &&
        value->expr.len == 3u) {
        return petta_block_value_is_proven_proper_list(
            check, value->expr.elems[2], environment,
            depth + 1u);
    }
    if (head && strcmp(head, "case") == 0 &&
        value->expr.len == 3u) {
        Atom *branches = value->expr.elems[2];
        if (!branches || branches->kind != ATOM_EXPR ||
            branches->expr.len == 0u)
            return false;
        for (CettaExprIndex index = 0u;
             index < branches->expr.len; index++) {
            Atom *branch = branches->expr.elems[index];
            if (!branch || branch->kind != ATOM_EXPR ||
                branch->expr.len != 2u ||
                !petta_block_value_is_proven_proper_list(
                    check, branch->expr.elems[1], environment,
                    depth + 1u))
                return false;
        }
        return true;
    }
    if (head && strcmp(head, "if") == 0 &&
        value->expr.len == 4u) {
        return petta_block_value_is_proven_proper_list(
                   check, value->expr.elems[2], environment,
                   depth + 1u) &&
               petta_block_value_is_proven_proper_list(
                   check, value->expr.elems[3], environment,
                   depth + 1u);
    }
    Atom *identity_value = petta_block_identity_binding_value(value);
    if (identity_value)
        return petta_block_value_is_proven_proper_list(
            check, identity_value, environment, depth + 1u);
    if (head &&
        ((strcmp(head, "let") == 0 || strcmp(head, "chain") == 0) &&
         value->expr.len == 4u)) {
        return petta_block_value_is_proven_proper_list(
            check, value->expr.elems[3], environment,
            depth + 1u);
    }
    if (head &&
        ((strcmp(head, "let*") == 0 && value->expr.len == 3u) ||
         (strcmp(head, "progn") == 0 && value->expr.len >= 2u))) {
        return petta_block_value_is_proven_proper_list(
            check, value->expr.elems[value->expr.len - 1u],
            environment, depth + 1u);
    }
    if (head &&
        (strcmp(head, "append") == 0 ||
         strcmp(head, "union-atom") == 0 ||
         strcmp(head, "intersection-atom") == 0 ||
         strcmp(head, "subtraction-atom") == 0)) {
        for (CettaExprIndex index = 1u;
             index < value->expr.len; index++) {
            if (!petta_block_value_is_proven_proper_list(
                    check, value->expr.elems[index], environment,
                    depth + 1u))
                return false;
        }
        return value->expr.len > 1u;
    }
    if (!head)
        return false;
    SymbolId head_id = value->expr.elems[0]->sym_id;
    CettaExprLen arity = value->expr.len - 1u;
    for (size_t index = 0u;
         index < check->proper_list_stack_len; index++) {
        if (check->proper_list_stack[index].head == head_id &&
            check->proper_list_stack[index].arity == arity)
            return true;
    }
    if (check->proper_list_stack_len >=
        sizeof(check->proper_list_stack) /
            sizeof(check->proper_list_stack[0]))
        return false;
    check->proper_list_stack[check->proper_list_stack_len].head = head_id;
    check->proper_list_stack[check->proper_list_stack_len].arity = arity;
    check->proper_list_stack_len++;
    size_t equation_len = 0u;
    Atom *const *equations = petta_block_equations(
        check, value->expr.elems[0], arity,
        &equation_len);
    bool saw_clause = equation_len > 0u;
    bool proven = saw_clause;
    for (size_t index = 0u; index < equation_len; index++) {
        Atom *form = equations[index];
        if (!petta_block_value_is_proven_proper_list(
                check, form->expr.elems[2], environment, depth + 1u)) {
            proven = false;
            break;
        }
    }
    check->proper_list_stack_len--;
    return proven;
}

static bool petta_block_value_manifest_bool(
    PettaBlockCheck *check, Atom *value,
    Bindings *environment, uint32_t depth);

static bool petta_block_expression_produces_bound_bool(
    PettaBlockCheck *check, Atom *expression,
    Bindings *environment, uint32_t depth) {
    if (!check || !expression || depth > 128u)
        return false;
    if (petta_value_literal_sort(expression) == PETTA_LITERAL_BOOL)
        return true;
    if (expression->kind != ATOM_EXPR || expression->expr.len == 0u)
        return false;
    const char *head = petta_block_symbol_name(expression->expr.elems[0]);
    if (!head)
        return false;
    Atom *identity_value = petta_block_identity_binding_value(expression);
    if (identity_value) {
        return petta_block_expression_produces_bound_bool(
            check, identity_value, environment, depth + 1u);
    }
    static const char *const bound_bool_builtins[] = {
        "<", "<=", ">", ">=", "=", "==", "!=", "=?",
        "=alpha", "=@=", "is-var", "is-ground", "is-expr",
        "is-space"
    };
    if (petta_block_name_in(
            head, bound_bool_builtins,
            sizeof(bound_bool_builtins) /
                sizeof(bound_bool_builtins[0])))
        return true;
    if ((strcmp(head, "and") == 0 || strcmp(head, "or") == 0 ||
         strcmp(head, "xor") == 0 || strcmp(head, "implies") == 0 ||
         strcmp(head, "not") == 0)) {
        for (CettaExprIndex index = 1u;
             index < expression->expr.len; index++) {
            if (!petta_block_value_manifest_bool(
                    check, expression->expr.elems[index],
                    environment, depth + 1u))
                return false;
        }
        return true;
    }
    if (strcmp(head, "if") == 0 && expression->expr.len == 4u) {
        return petta_block_expression_produces_bound_bool(
                   check, expression->expr.elems[2],
                   environment, depth + 1u) &&
               petta_block_expression_produces_bound_bool(
                   check, expression->expr.elems[3],
                   environment, depth + 1u);
    }
    /* This direct judgment concerns the value a form finally produces, so it
     * passes through the binding and sequencing forms to the expression in
     * result position.  Without these cases the reasoning stops at the first
     * `let*`, looks for clauses of a form that has none, and reports "not
     * provably a bound boolean" for bodies that plainly are one. */
    if (strcmp(head, "if") == 0 && expression->expr.len == 3u) {
        return petta_block_expression_produces_bound_bool(
            check, expression->expr.elems[2], environment, depth + 1u);
    }
    if ((strcmp(head, "let") == 0 || strcmp(head, "chain") == 0) &&
        expression->expr.len == 4u) {
        return petta_block_expression_produces_bound_bool(
            check, expression->expr.elems[3], environment, depth + 1u);
    }
    if (strcmp(head, "let*") == 0 && expression->expr.len == 3u) {
        return petta_block_expression_produces_bound_bool(
            check, expression->expr.elems[2], environment, depth + 1u);
    }
    if (strcmp(head, "progn") == 0 && expression->expr.len >= 2u) {
        return petta_block_expression_produces_bound_bool(
            check, expression->expr.elems[expression->expr.len - 1u],
            environment, depth + 1u);
    }
    if ((strcmp(head, "once") == 0 || strcmp(head, "sealed") == 0 ||
         strcmp(head, "with_mutex") == 0) &&
        expression->expr.len >= 2u) {
        CettaExprIndex body_index = expression->expr.len - 1u;
        return petta_block_expression_produces_bound_bool(
            check, expression->expr.elems[body_index],
            environment, depth + 1u);
    }
    if (strcmp(head, "case") == 0 && expression->expr.len == 3u) {
        Atom *branches = expression->expr.elems[2];
        if (!branches || branches->kind != ATOM_EXPR ||
            branches->expr.len == 0u)
            return false;
        for (CettaExprIndex index = 0u;
             index < branches->expr.len; index++) {
            Atom *branch = branches->expr.elems[index];
            if (!branch || branch->kind != ATOM_EXPR ||
                branch->expr.len != 2u ||
                !petta_block_expression_produces_bound_bool(
                    check, branch->expr.elems[1],
                    environment, depth + 1u))
                return false;
        }
        return true;
    }
    SymbolId head_id = expression->expr.elems[0]->sym_id;
    CettaExprLen arity = expression->expr.len - 1u;
    for (size_t index = 0u;
         index < check->bound_bool_stack_len; index++) {
        if (check->bound_bool_stack[index].head == head_id &&
            check->bound_bool_stack[index].arity == arity)
            return true;
    }
    if (check->bound_bool_stack_len >=
        sizeof(check->bound_bool_stack) /
            sizeof(check->bound_bool_stack[0]))
        return false;
    check->bound_bool_stack[check->bound_bool_stack_len].head = head_id;
    check->bound_bool_stack[check->bound_bool_stack_len].arity = arity;
    check->bound_bool_stack_len++;
    size_t equation_len = 0u;
    Atom *const *equations = petta_block_equations(
        check, expression->expr.elems[0], arity, &equation_len);
    bool saw_clause = equation_len > 0u;
    bool all_bound = true;
    for (size_t index = 0u; index < equation_len; index++) {
        Atom *form = equations[index];
        if (!petta_block_expression_produces_bound_bool(
                check, form->expr.elems[2],
                environment, depth + 1u)) {
            all_bound = false;
            break;
        }
    }
    check->bound_bool_stack_len--;
    return saw_clause && all_bound;
}

static bool petta_block_value_manifest_bool(
    PettaBlockCheck *check, Atom *value,
    Bindings *environment, uint32_t depth) {
    if (petta_value_literal_sort(value) == PETTA_LITERAL_BOOL)
        return true;
    if (value && value->kind == ATOM_VAR) {
        for (size_t index = 0u;
             index < check->bound_bool_parameter_len; index++) {
            if (check->bound_bool_parameters[index] == value->var_id)
                return true;
        }
    }
    if (!value || value->kind != ATOM_EXPR)
        return false;
    return petta_block_expression_produces_bound_bool(
               check, value, environment, depth + 1u) &&
           petta_block_expression_effect(
               check, value, environment, depth + 1u) ==
               PETTA_BLOCK_EFFECT_DET;
}

static bool petta_block_clause_heads_overlap(
    PettaBlockCheck *check, Atom *left, Atom *right);
static bool petta_block_position_proven_exhaustive(
    PettaBlockCheck *check, Atom *head, CettaExprLen arity,
    CettaExprIndex position, Atom *type);
static bool petta_block_pattern_total_for_type(
    Atom *pattern, Atom *type, uint32_t depth);
static bool petta_block_position_has_variable_clause(
    PettaBlockCheck *check, Atom *head,
    CettaExprLen arity, CettaExprIndex position);

typedef enum {
    PETTA_BLOCK_SELECTION_SOURCE_OTHER = 0,
    PETTA_BLOCK_SELECTION_SOURCE_NONEMPTY_LIST,
    PETTA_BLOCK_SELECTION_SOURCE_OPAQUE,
} PettaBlockSelectionSourceShape;

/*
 * Clause selection is a judgment about the value reaching the callee, not
 * about arbitrary source syntax.  The v2 oracle exposes only the outer
 * empty/nonempty list shape of an ordinary data expression; compiler forms
 * and callable expressions publish no selector shape without a separate
 * output certificate.  Keep this policy at the selection boundary so the
 * structural matcher remains available to stronger, certificate-bearing
 * profiles.
 */
static PettaBlockSelectionSourceShape
petta_block_selection_source_shape(
    PettaBlockCheck *check, Atom *actual, Bindings *environment) {
    if (!check || !actual || actual->kind != ATOM_EXPR ||
        actual->expr.len == 0u)
        return PETTA_BLOCK_SELECTION_SOURCE_OTHER;
    Atom *head = actual->expr.elems[0];
    const char *head_name = petta_block_symbol_name(head);
    if ((head_name && strcmp(head_name, "data") == 0) ||
        (head->kind == ATOM_SYMBOL &&
         petta_block_head_is_callable(
             check, head, actual->expr.len - 1u))) {
        return PETTA_BLOCK_SELECTION_SOURCE_OPAQUE;
    }
    if (petta_block_value_manifest_proper_list(
            check, actual, environment, true))
        return PETTA_BLOCK_SELECTION_SOURCE_NONEMPTY_LIST;
    return PETTA_BLOCK_SELECTION_SOURCE_OTHER;
}

typedef enum {
    PETTA_BLOCK_SELECTION_PATTERN_POSSIBLE = 0,
    PETTA_BLOCK_SELECTION_PATTERN_IMPOSSIBLE,
    PETTA_BLOCK_SELECTION_PATTERN_SOURCE_UNKNOWN,
} PettaBlockSelectionPatternStatus;

static PettaBlockSelectionPatternStatus
petta_block_selection_pattern_source_status(
    PettaBlockCheck *check, Atom *pattern, Atom *actual,
    Bindings *environment) {
    if (pattern && pattern->kind == ATOM_VAR)
        return PETTA_BLOCK_SELECTION_PATTERN_POSSIBLE;
    PettaBlockSelectionSourceShape shape =
        petta_block_selection_source_shape(
            check, actual, environment);
    if (shape == PETTA_BLOCK_SELECTION_SOURCE_OPAQUE)
        return PETTA_BLOCK_SELECTION_PATTERN_SOURCE_UNKNOWN;
    if (shape != PETTA_BLOCK_SELECTION_SOURCE_NONEMPTY_LIST)
        return PETTA_BLOCK_SELECTION_PATTERN_POSSIBLE;
    return ((petta_block_head_is(pattern, "cons") ||
             petta_block_head_is(pattern, "cons-atom")) &&
            pattern->expr.len == 3u)
        ? PETTA_BLOCK_SELECTION_PATTERN_POSSIBLE
        : PETTA_BLOCK_SELECTION_PATTERN_IMPOSSIBLE;
}

static bool petta_block_selection_requires_value_evidence(Atom *actual) {
    if (!actual)
        return false;
    if (actual->kind == ATOM_VAR)
        return true;
    if (actual->kind != ATOM_EXPR || actual->expr.len == 0u)
        return false;
    const char *head = petta_block_symbol_name(actual->expr.elems[0]);
    return head &&
        ((strcmp(head, "if") == 0 && actual->expr.len == 4u) ||
         petta_block_deferred_form(head));
}

static bool petta_block_pattern_total_for_actual(
    Atom *pattern, Atom *actual, uint32_t depth) {
    if (!pattern || !actual || depth > 2048u)
        return false;
    if (pattern->kind == ATOM_VAR || !atom_has_vars(actual))
        return true;
    if (pattern->kind != actual->kind)
        return false;
    if (pattern->kind != ATOM_EXPR)
        return atom_eq(pattern, actual);
    if (pattern->expr.len != actual->expr.len)
        return false;
    for (CettaExprIndex index = 0u;
         index < pattern->expr.len; index++) {
        if (!petta_block_pattern_total_for_actual(
                pattern->expr.elems[index], actual->expr.elems[index],
                depth + 1u))
            return false;
    }
    return true;
}

static PettaBlockEffect petta_block_inferred_call_effect(
    PettaBlockCheck *check, Atom *call,
    Bindings *environment, uint32_t depth) {
    if (!check || !call || call->kind != ATOM_EXPR ||
        call->expr.len == 0u || depth > 128u)
        return PETTA_BLOCK_EFFECT_UNKNOWN;
    SymbolId head_id = call->expr.elems[0]->kind == ATOM_SYMBOL
        ? call->expr.elems[0]->sym_id : 0u;
    CettaExprLen arity = call->expr.len - 1u;
    for (size_t index = 0u; index < check->effect_stack_len; index++) {
        if (check->effect_stack[index].head == head_id &&
            check->effect_stack[index].arity == arity)
            return PETTA_BLOCK_EFFECT_DET;
    }
    if (head_id == 0u ||
        check->effect_stack_len >=
            sizeof(check->effect_stack) / sizeof(check->effect_stack[0]))
        return PETTA_BLOCK_EFFECT_UNKNOWN;
    check->effect_stack[check->effect_stack_len].head = head_id;
    check->effect_stack[check->effect_stack_len].arity = arity;
    check->effect_stack_len++;
    PettaBlockEffect result = PETTA_BLOCK_EFFECT_UNKNOWN;
    size_t candidates = 0u;
    bool guaranteed = false;
    PettaBlockEffect candidate_effect = PETTA_BLOCK_EFFECT_DET;
    bool pairwise_disjoint = true;
    bool source_selection_unknown = false;
    size_t equation_len = 0u;
    Atom *const *equations = petta_block_equations(
        check, call->expr.elems[0], arity, &equation_len);
    Atom **matched_lhs = equation_len
        ? cetta_malloc(sizeof(*matched_lhs) * equation_len) : NULL;
    for (size_t index = 0u; index < equation_len; index++) {
        Atom *form = equations[index];
        Atom *lhs = form->expr.elems[1];
        bool source_shape_may_match = true;
        bool source_shape_unknown = false;
        for (CettaExprIndex argument = 1u;
             argument < call->expr.len; argument++) {
            PettaBlockSelectionPatternStatus source_status =
                petta_block_selection_pattern_source_status(
                    check, lhs->expr.elems[argument],
                    call->expr.elems[argument], environment);
            if (source_status ==
                PETTA_BLOCK_SELECTION_PATTERN_IMPOSSIBLE) {
                source_shape_may_match = false;
                CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                    CETTA_PETTA_TYPECHECK_CENSUS_EVENT_SELECTION_SOURCE_LIST_SHAPE_ONLY);
                break;
            }
            if (source_status ==
                PETTA_BLOCK_SELECTION_PATTERN_SOURCE_UNKNOWN)
                source_shape_unknown = true;
        }
        if (!source_shape_may_match)
            continue;
        if (source_shape_unknown) {
            source_selection_unknown = true;
            continue;
        }
        uint32_t epoch = (uint32_t)fresh_var_id();
        Atom *fresh_form = atom_freshen_epoch(
            &check->scratch, form, epoch);
        Atom *fresh_call = atom_freshen_epoch(
            &check->scratch, call,
            (uint32_t)fresh_var_id());
        if (!fresh_form || !fresh_call)
            goto done;
        Bindings bindings;
        bindings_init(&bindings);
        bool matches = match_atoms(
            fresh_call, fresh_form->expr.elems[1], &bindings);
        if (!matches) {
            bindings_free(&bindings);
            continue;
        }
        for (size_t prior = 0u; prior < candidates; prior++) {
            if (petta_block_clause_heads_overlap(
                    check, matched_lhs[prior], lhs)) {
                pairwise_disjoint = false;
                break;
            }
        }
        matched_lhs[candidates++] = lhs;
        guaranteed = true;
        for (CettaExprIndex argument = 1u;
             argument < call->expr.len; argument++) {
            if (!petta_block_pattern_total_for_actual(
                    lhs->expr.elems[argument],
                    call->expr.elems[argument], 0u)) {
                guaranteed = false;
                break;
            }
        }
        Atom *body = bindings_apply_if_vars(
            &bindings, &check->scratch, fresh_form->expr.elems[2]);
        bindings_free(&bindings);
        candidate_effect = petta_block_effect_join(
            candidate_effect, petta_block_expression_effect(
                                  check, body, environment,
                                  depth + 1u));
    }
    if (source_selection_unknown) {
        CETTA_PETTA_TYPECHECK_CENSUS_HIT(
            CETTA_PETTA_TYPECHECK_CENSUS_EVENT_SELECTION_SOURCE_OPAQUE);
        check->committed_effect_evidence_withheld = true;
        result = PETTA_BLOCK_EFFECT_UNKNOWN;
        goto done;
    }
    if (candidates == 0u) {
        result = PETTA_BLOCK_EFFECT_SEMIDET;
        for (CettaExprIndex argument = 1u;
             argument < call->expr.len; argument++) {
            Atom *actual = call->expr.elems[argument];
            if (!actual || actual->kind != ATOM_EXPR ||
                actual->expr.len == 0u ||
                actual->expr.elems[0]->kind != ATOM_SYMBOL)
                continue;
            const char *actual_head = petta_block_symbol_name(
                actual->expr.elems[0]);
            if (petta_block_head_is_callable(
                    check, actual->expr.elems[0],
                    actual->expr.len - 1u) ||
                petta_block_deferred_form(actual_head)) {
                result = PETTA_BLOCK_EFFECT_UNKNOWN;
                break;
            }
        }
        goto done;
    }
    if (candidates > 1u) {
        bool selection_total = pairwise_disjoint;
        for (CettaExprIndex argument = 1u;
             selection_total && argument < call->expr.len;
             argument++) {
            Atom *actual = call->expr.elems[argument];
            if (!atom_has_vars(actual))
                continue;
            Atom *type = actual->kind == ATOM_VAR && environment
                ? bindings_lookup_var(environment, actual) : NULL;
            if (!type || !petta_block_position_proven_exhaustive(
                             check, call->expr.elems[0],
                             call->expr.len - 1u,
                             argument - 1u, type))
                selection_total = false;
        }
        result = selection_total
            ? candidate_effect : PETTA_BLOCK_EFFECT_NONDET;
        goto done;
    }
    if (!guaranteed) {
        CETTA_PETTA_TYPECHECK_CENSUS_HIT(
            CETTA_PETTA_TYPECHECK_CENSUS_EVENT_SELECTION_PATTERN_NOT_TOTAL);
        if (candidate_effect == PETTA_BLOCK_EFFECT_NONDET)
            result = PETTA_BLOCK_EFFECT_NONDET;
        else if (candidate_effect == PETTA_BLOCK_EFFECT_UNKNOWN)
            result = PETTA_BLOCK_EFFECT_UNKNOWN;
        else
            result = petta_block_effect_join(
                candidate_effect, PETTA_BLOCK_EFFECT_SEMIDET);
        goto done;
    }
    result = candidate_effect;
done:
    free(matched_lhs);
    check->effect_stack_len--;
    return result;
}

static PettaBlockEffect petta_block_closure_value_effect(
    PettaBlockCheck *check, Atom *value, Atom *required_closure,
    Bindings *environment, uint32_t depth) {
    if (!check || !value || !required_closure || depth > 128u ||
        !petta_block_arrow_head(required_closure, NULL) ||
        required_closure->expr.len < 2u)
        return PETTA_BLOCK_EFFECT_UNKNOWN;
    if (value->kind == ATOM_VAR && environment) {
        Atom *known = bindings_lookup_var(environment, value);
        return known
            ? petta_block_signature_effect(known)
            : PETTA_BLOCK_EFFECT_UNKNOWN;
    }
    if (value->kind == ATOM_SYMBOL) {
        CettaExprLen arity = required_closure->expr.len - 2u;
        const char *name = petta_block_symbol_name(value);
        if (name &&
            ((arity == 1u && strcmp(name, "not") == 0) ||
             (arity == 2u &&
              (strcmp(name, "and") == 0 ||
               strcmp(name, "or") == 0 ||
               strcmp(name, "xor") == 0 ||
               strcmp(name, "implies") == 0)))) {
            /* The future closure arguments are not manifest booleans yet.
             * PeTTa's relational boolean operators may enumerate them, so a
             * bare operator value is nondeterministic at this boundary. */
            return PETTA_BLOCK_EFFECT_NONDET;
        }
        Atom *signature = NULL;
        if (petta_block_find_signature(
                check, value, arity, &signature)) {
            PettaBlockEffect declared =
                petta_block_signature_effect(signature);
            if (declared != PETTA_BLOCK_EFFECT_UNKNOWN)
                return declared;
        }
        if (petta_block_equation_count(check, value, arity) == 0u)
            return PETTA_BLOCK_EFFECT_UNKNOWN;
        Atom **elements = cetta_malloc(
            sizeof(*elements) * (size_t)(arity + 1u));
        elements[0] = value;
        for (CettaExprIndex index = 0u; index < arity; index++)
            elements[index + 1u] = atom_var_with_id(
                &check->scratch, "_effect_arg", fresh_var_id());
        Atom *call = atom_expr(&check->scratch, elements, arity + 1u);
        free(elements);
        return call
            ? petta_block_inferred_call_effect(
                  check, call, environment, depth + 1u)
            : PETTA_BLOCK_EFFECT_UNKNOWN;
    }
    if (petta_block_head_is(value, "|->") && value->expr.len == 3u)
        return petta_block_expression_effect(
            check, value->expr.elems[2], environment, depth + 1u);
    if (value->kind == ATOM_EXPR && value->expr.len > 0u &&
        value->expr.elems[0]->kind == ATOM_SYMBOL) {
        CettaExprLen supplied = value->expr.len - 1u;
        CettaExprLen residual = required_closure->expr.len - 2u;
        if (supplied <= UINT32_MAX - residual) {
            Atom *signature = NULL;
            if (petta_block_find_signature(
                    check, value->expr.elems[0], supplied + residual,
                    &signature))
                return petta_block_signature_effect(signature);
        }
    }
    return PETTA_BLOCK_EFFECT_UNKNOWN;
}

static PettaBlockEffect petta_block_call_selection_effect(
    PettaBlockCheck *check, Atom *call, Atom *signature,
    Bindings *environment) {
    if (!check || !call || call->kind != ATOM_EXPR ||
        call->expr.len == 0u ||
        call->expr.elems[0]->kind != ATOM_SYMBOL)
        return PETTA_BLOCK_EFFECT_UNKNOWN;
    CettaExprLen arity = call->expr.len - 1u;
    size_t equation_len = 0u;
    Atom *const *equations = petta_block_equations(
        check, call->expr.elems[0], arity, &equation_len);
    if (equation_len == 0u)
        return PETTA_BLOCK_EFFECT_DET;
    bool abstract_selection = false;
    for (CettaExprIndex index = 1u;
         index < call->expr.len; index++) {
        Atom *actual = call->expr.elems[index];
        if (petta_block_selection_requires_value_evidence(actual)) {
            abstract_selection = true;
            break;
        }
    }
    if (abstract_selection) {
        for (size_t left = 0u; left < equation_len; left++) {
            for (size_t right = left + 1u;
                 right < equation_len; right++) {
                if (petta_block_clause_heads_overlap(
                        check, equations[left]->expr.elems[1],
                        equations[right]->expr.elems[1])) {
                    return PETTA_BLOCK_EFFECT_NONDET;
                }
            }
        }
        if (!signature || signature->expr.len != call->expr.len + 1u)
            return PETTA_BLOCK_EFFECT_NONDET;
        for (CettaExprIndex index = 1u;
             index < call->expr.len; index++) {
            Atom *actual = call->expr.elems[index];
            if (!atom_has_vars(actual) &&
                !petta_block_selection_requires_value_evidence(actual))
                continue;
            Atom *required = signature->expr.elems[index];
            if (petta_block_position_has_variable_clause(
                    check, call->expr.elems[0], arity, index - 1u))
                continue;
            const char *required_name = petta_block_symbol_name(required);
            bool bound = petta_block_head_is(required, "List")
                ? petta_block_value_is_proven_proper_list(
                      check, actual, environment, 0u)
                : required_name && strcmp(required_name, "Bool") == 0
                    ? petta_block_value_manifest_bool(
                          check, actual, environment, 0u)
                    : false;
            bool exhaustive = petta_block_position_proven_exhaustive(
                check, call->expr.elems[0], arity,
                index - 1u, required);
            if (petta_block_head_is(actual, "if") &&
                actual->expr.len == 4u &&
                petta_block_head_is(required, "List") &&
                bound && exhaustive) {
                CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                    CETTA_PETTA_TYPECHECK_CENSUS_EVENT_SELECTION_CONDITIONAL_LIST_TOTAL);
            }
            if (petta_block_head_is(actual, "case") &&
                actual->expr.len == 3u &&
                petta_block_head_is(required, "List") &&
                bound && exhaustive) {
                CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                    CETTA_PETTA_TYPECHECK_CENSUS_EVENT_SELECTION_CASE_LIST_TOTAL);
            }
            if (petta_block_identity_binding_value(actual) &&
                petta_block_head_is(required, "List") &&
                bound && exhaustive) {
                CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                    CETTA_PETTA_TYPECHECK_CENSUS_EVENT_SELECTION_IDENTITY_BINDING_LIST_TOTAL);
            }
            if (petta_block_identity_binding_value(actual) &&
                required_name && strcmp(required_name, "Bool") == 0 &&
                bound && exhaustive) {
                CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                    CETTA_PETTA_TYPECHECK_CENSUS_EVENT_SELECTION_IDENTITY_BINDING_BOOL_TOTAL);
            }
            if (!bound || !exhaustive)
                return PETTA_BLOCK_EFFECT_NONDET;
        }
        return PETTA_BLOCK_EFFECT_DET;
    }
    Atom **matched_lhs = cetta_malloc(
        sizeof(*matched_lhs) * equation_len);
    size_t candidates = 0u;
    bool guaranteed = false;
    bool pairwise_disjoint = true;
    bool source_selection_unknown = false;
    for (size_t index = 0u; index < equation_len; index++) {
        Atom *equation = equations[index];
        Atom *lhs = equation->expr.elems[1];
        bool source_shape_may_match = true;
        bool source_shape_unknown = false;
        for (CettaExprIndex argument = 1u;
             argument < call->expr.len; argument++) {
            PettaBlockSelectionPatternStatus source_status =
                petta_block_selection_pattern_source_status(
                    check, lhs->expr.elems[argument],
                    call->expr.elems[argument], environment);
            if (source_status ==
                PETTA_BLOCK_SELECTION_PATTERN_IMPOSSIBLE) {
                source_shape_may_match = false;
                CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                    CETTA_PETTA_TYPECHECK_CENSUS_EVENT_SELECTION_SOURCE_LIST_SHAPE_ONLY);
                break;
            }
            if (source_status ==
                PETTA_BLOCK_SELECTION_PATTERN_SOURCE_UNKNOWN)
                source_shape_unknown = true;
        }
        if (!source_shape_may_match)
            continue;
        if (source_shape_unknown) {
            source_selection_unknown = true;
            continue;
        }
        Atom *fresh_equation = atom_freshen_epoch(
            &check->scratch, equation, (uint32_t)fresh_var_id());
        Atom *fresh_call = atom_freshen_epoch(
            &check->scratch, call,
            (uint32_t)fresh_var_id());
        if (!fresh_equation || !fresh_call) {
            free(matched_lhs);
            return PETTA_BLOCK_EFFECT_UNKNOWN;
        }
        Bindings bindings;
        bindings_init(&bindings);
        bool matches = match_atoms(
            fresh_call, fresh_equation->expr.elems[1], &bindings);
        bindings_free(&bindings);
        if (!matches)
            continue;
        for (size_t prior = 0u; prior < candidates; prior++) {
            if (petta_block_clause_heads_overlap(
                    check, matched_lhs[prior], lhs)) {
                pairwise_disjoint = false;
                break;
            }
        }
        matched_lhs[candidates++] = lhs;
        guaranteed = true;
        for (CettaExprIndex argument = 1u;
             argument < call->expr.len; argument++) {
            if (!petta_block_pattern_total_for_actual(
                    lhs->expr.elems[argument],
                    call->expr.elems[argument], 0u)) {
                guaranteed = false;
                break;
            }
        }
    }
    free(matched_lhs);
    if (source_selection_unknown) {
        CETTA_PETTA_TYPECHECK_CENSUS_HIT(
            CETTA_PETTA_TYPECHECK_CENSUS_EVENT_SELECTION_SOURCE_OPAQUE);
        check->committed_effect_evidence_withheld = true;
        return PETTA_BLOCK_EFFECT_UNKNOWN;
    }
    if (candidates == 0u)
        return PETTA_BLOCK_EFFECT_SEMIDET;
    if (candidates == 1u) {
        if (!guaranteed) {
            CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                CETTA_PETTA_TYPECHECK_CENSUS_EVENT_SELECTION_PATTERN_NOT_TOTAL);
        }
        return guaranteed
            ? PETTA_BLOCK_EFFECT_DET : PETTA_BLOCK_EFFECT_SEMIDET;
    }
    bool selection_total = pairwise_disjoint;
    for (CettaExprIndex argument = 1u;
         selection_total && argument < call->expr.len;
         argument++) {
        Atom *actual = call->expr.elems[argument];
        if (!atom_has_vars(actual))
            continue;
        Atom *type = actual->kind == ATOM_VAR && environment
            ? bindings_lookup_var(environment, actual) : NULL;
        if (!type || !petta_block_position_proven_exhaustive(
                         check, call->expr.elems[0], arity,
                         argument - 1u, type))
            selection_total = false;
    }
    return selection_total
        ? PETTA_BLOCK_EFFECT_DET : PETTA_BLOCK_EFFECT_NONDET;
}

static bool petta_block_value_known_nonempty(
    PettaBlockCheck *check, Atom *value) {
    if (!check || !value)
        return false;
    for (size_t index = check->nonempty_value_len;
         index > 0u; index--) {
        Atom *known = check->nonempty_values[index - 1u];
        if ((known->kind == ATOM_VAR && value->kind == ATOM_VAR &&
             known->var_id == value->var_id) || atom_eq(known, value))
            return true;
    }
    return false;
}

static bool petta_block_pattern_covers_nonempty_list(
    Atom *pattern, Atom *list_type) {
    return pattern &&
        (pattern->kind == ATOM_VAR ||
         (petta_block_head_is(pattern, "cons") &&
          pattern->expr.len == 3u &&
          pattern->expr.elems[2]->kind == ATOM_VAR &&
          petta_block_head_is(list_type, "List") &&
          list_type->expr.len == 2u &&
          petta_block_pattern_total_for_type(
              pattern->expr.elems[1],
              list_type->expr.elems[1], 0u)));
}

static bool petta_block_pattern_can_match_nonempty_list(Atom *pattern) {
    return pattern &&
        (pattern->kind == ATOM_VAR ||
         (pattern->kind == ATOM_EXPR && pattern->expr.len > 0u));
}

static bool petta_block_call_det_under_nonempty(
    PettaBlockCheck *check, Atom *call, Atom *signature,
    Bindings *environment, uint32_t depth) {
    if (!check || !call || !signature || depth > 128u ||
        call->kind != ATOM_EXPR || call->expr.len == 0u)
        return false;
    CettaExprIndex narrowed = 0u;
    for (CettaExprIndex index = 1u;
         index < call->expr.len; index++) {
        if (petta_block_head_is(signature->expr.elems[index], "List") &&
            petta_block_value_known_nonempty(
                check, call->expr.elems[index])) {
            narrowed = index;
            break;
        }
    }
    if (narrowed == 0u)
        return false;
    size_t equation_len = 0u;
    Atom *const *equations = petta_block_equations(
        check, call->expr.elems[0], call->expr.len - 1u,
        &equation_len);
    bool covers = false;
    bool saw_applicable = false;
    for (size_t index = 0u; index < equation_len; index++) {
        Atom *equation = equations[index];
        Atom *lhs = equation->expr.elems[1];
        Atom *pattern = lhs->expr.elems[narrowed];
        covers = covers ||
            petta_block_pattern_covers_nonempty_list(
                pattern, signature->expr.elems[narrowed]);
        if (!petta_block_pattern_can_match_nonempty_list(pattern))
            continue;
        saw_applicable = true;
        Bindings clause_environment;
        bindings_init(&clause_environment);
        bool environment_ok = true;
        for (CettaExprIndex argument = 1u;
             argument < lhs->expr.len; argument++) {
            if (!petta_block_bind_pattern(
                    check, lhs->expr.elems[argument],
                    signature->expr.elems[argument],
                    &clause_environment, 0u)) {
                environment_ok = false;
                break;
            }
        }
        PettaBlockEffect body_effect = environment_ok
            ? petta_block_expression_effect(
                  check, equation->expr.elems[2],
                  &clause_environment, depth + 1u)
            : PETTA_BLOCK_EFFECT_UNKNOWN;
        bindings_free(&clause_environment);
        if (body_effect != PETTA_BLOCK_EFFECT_DET)
            return false;
    }
    (void)environment;
    return covers && saw_applicable;
}

static PettaBlockEffect petta_block_call_signature_effect(
    PettaBlockCheck *check, Atom *call, Atom *signature,
    Bindings *environment, uint32_t depth) {
    PettaBlockEffect declared = petta_block_signature_effect(signature);
    if (declared == PETTA_BLOCK_EFFECT_SEMIDET &&
        petta_block_call_det_under_nonempty(
            check, call, signature, environment, depth + 1u))
        return PETTA_BLOCK_EFFECT_DET;
    if (declared != PETTA_BLOCK_EFFECT_UNKNOWN)
        return declared;
    const char *effect_variable =
        petta_block_effect_arrow_name(signature);
    if (!effect_variable || !call || call->kind != ATOM_EXPR ||
        signature->expr.len != call->expr.len + 1u)
        return PETTA_BLOCK_EFFECT_UNKNOWN;
    PettaBlockEffect instantiated = PETTA_BLOCK_EFFECT_DET;
    bool saw_closure = false;
    for (CettaExprIndex index = 1u;
         index + 1u < signature->expr.len; index++) {
        Atom *required = signature->expr.elems[index];
        const char *argument_effect =
            petta_block_effect_arrow_name(required);
        if (!argument_effect ||
            strcmp(argument_effect, effect_variable) != 0)
            continue;
        saw_closure = true;
        PettaBlockEffect actual = petta_block_closure_value_effect(
            check, call->expr.elems[index], required,
            environment, depth + 1u);
        if (actual == PETTA_BLOCK_EFFECT_UNKNOWN)
            return actual;
        instantiated = petta_block_effect_join(instantiated, actual);
    }
    if (!saw_closure)
        return PETTA_BLOCK_EFFECT_UNKNOWN;
    PettaBlockEffect selection = petta_block_call_selection_effect(
        check, call, signature, environment);
    return petta_block_effect_join(instantiated, selection);
}

static bool petta_block_validate_effect_arguments(
    PettaBlockCheck *check, Atom *expression,
    Bindings *environment, uint32_t depth) {
    if (!check || !expression || depth > 2048u)
        return false;
    if (expression->kind != ATOM_EXPR)
        return true;
    if (expression->expr.len == 0u)
        return true;
    Atom *head = expression->expr.elems[0];
    if (head->kind == ATOM_SYMBOL) {
        Atom *signature = NULL;
        if (petta_block_find_signature(
                check, head, expression->expr.len - 1u,
                &signature)) {
            for (CettaExprIndex index = 1u;
                 index + 1u < signature->expr.len; index++) {
                Atom *required = signature->expr.elems[index];
                PettaBlockEffect promised =
                    petta_block_signature_effect(required);
                if (promised != PETTA_BLOCK_EFFECT_DET &&
                    promised != PETTA_BLOCK_EFFECT_SEMIDET)
                    continue;
                PettaBlockEffect actual =
                    petta_block_closure_value_effect(
                        check, expression->expr.elems[index],
                        required, environment, depth + 1u);
                /* Roman's effect-polymorphic analysis treats a closure whose
                 * cardinality cannot yet be established as `unspecified`, not
                 * as a refutation.  Only an established stronger effect can
                 * contradict the declared closure slot.  This distinction is
                 * load-bearing for higher-order values unpacked from open
                 * aggregate/configuration data: the caller may remain
                 * undetermined until a concrete closure reaches the call
                 * boundary, but it must not be rejected speculatively. */
                if (actual == PETTA_BLOCK_EFFECT_UNKNOWN)
                    continue;
                bool fits = promised == PETTA_BLOCK_EFFECT_DET
                    ? actual == PETTA_BLOCK_EFFECT_DET
                    : actual == PETTA_BLOCK_EFFECT_DET ||
                      actual == PETTA_BLOCK_EFFECT_SEMIDET;
                if (!fits) {
                    return petta_block_fail(
                        check,
                        "closure effect conflicts with declaration for %s/%u",
                        petta_block_symbol_name(head),
                        (unsigned)(expression->expr.len - 1u));
                }
            }
        }
    }
    for (CettaExprIndex index = 0u;
         index < expression->expr.len; index++) {
        if (!petta_block_validate_effect_arguments(
                check, expression->expr.elems[index],
                environment, depth + 1u))
            return false;
    }
    return true;
}

static Atom *petta_block_empty_list_comparison_value(Atom *condition) {
    /* In the false branch of `(or p q)`, both p and q are false.  Therefore
     * an empty-list equality nested in either disjunct establishes that the
     * compared value is non-empty.  This is a one-way flow fact; analogous
     * extraction from `and` would be unsound because only one conjunct need
     * fail. */
    if (petta_block_head_is(condition, "or") &&
        condition->expr.len >= 2u) {
        for (CettaExprIndex index = 1u;
             index < condition->expr.len; index++) {
            Atom *value = petta_block_empty_list_comparison_value(
                condition->expr.elems[index]);
            if (value)
                return value;
        }
        return NULL;
    }
    if (!petta_block_head_is(condition, "==") ||
        condition->expr.len != 3u)
        return NULL;
    Atom *left = condition->expr.elems[1];
    Atom *right = condition->expr.elems[2];
    bool left_empty = left->kind == ATOM_EXPR && left->expr.len == 0u;
    bool right_empty = right->kind == ATOM_EXPR && right->expr.len == 0u;
    if (left_empty == right_empty)
        return NULL;
    return left_empty ? right : left;
}

static PettaBlockEffect petta_block_expression_effect(
    PettaBlockCheck *check, Atom *expression,
    Bindings *environment, uint32_t depth) {
    if (!check || !expression || depth > 2048u)
        return PETTA_BLOCK_EFFECT_UNKNOWN;
    if (expression->kind != ATOM_EXPR || expression->expr.len == 0u)
        return PETTA_BLOCK_EFFECT_DET;
    const char *head = petta_block_symbol_name(expression->expr.elems[0]);
    if (!head && expression->expr.elems[0]->kind == ATOM_VAR && environment) {
        Atom *closure_type = bindings_lookup_var(
            environment, expression->expr.elems[0]);
        if (!closure_type)
            return PETTA_BLOCK_EFFECT_UNKNOWN;
        return petta_block_arrow_head(closure_type, NULL)
            ? petta_block_signature_effect(closure_type)
            : PETTA_BLOCK_EFFECT_DET;
    }
    if (!head && expression->expr.elems[0]->kind == ATOM_EXPR) {
        /* A compound head is evaluated before the remaining spine.  Whether
         * the resulting spine is data or a call is a separate shape
         * judgment; either way, every source component contributes its
         * cardinality.  Treating the whole form as opaque here hid a known
         * semidet consumer behind an otherwise deterministic computed head. */
        PettaBlockEffect effect = PETTA_BLOCK_EFFECT_DET;
        for (CettaExprIndex index = 0u;
             index < expression->expr.len; index++) {
            effect = petta_block_effect_join(
                effect, petta_block_expression_effect(
                            check, expression->expr.elems[index],
                            environment, depth + 1u));
        }
        return effect;
    }
    if (!head)
        return PETTA_BLOCK_EFFECT_UNKNOWN;
    if (strcmp(head, "eval") == 0 && expression->expr.len == 2u) {
        /* `eval` transfers data into the evaluator.  Even when its operand
         * is written as a quote, v2 does not promote the contained source
         * expression into a cardinality certificate for a committed caller. */
        CETTA_PETTA_TYPECHECK_CENSUS_HIT(
            CETTA_PETTA_TYPECHECK_CENSUS_EVENT_EVAL_DYNAMIC_EFFECT_UNKNOWN);
        check->committed_effect_evidence_withheld = true;
        return PETTA_BLOCK_EFFECT_UNKNOWN;
    }
    if (strcmp(head, "foldall") == 0 && expression->expr.len == 4u) {
        CETTA_PETTA_TYPECHECK_CENSUS_HIT(
            CETTA_PETTA_TYPECHECK_CENSUS_EVENT_EXPRESSION_EFFECT_FOLDALL);
        return PETTA_BLOCK_EFFECT_DET;
    }
    if (strcmp(head, "once") == 0 && expression->expr.len == 2u) {
        PettaBlockEffect inner = petta_block_expression_effect(
            check, expression->expr.elems[1], environment,
            depth + 1u);
        if (inner == PETTA_BLOCK_EFFECT_DET) {
            CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                CETTA_PETTA_TYPECHECK_CENSUS_EVENT_EXPRESSION_EFFECT_ONCE_DET);
        } else if (inner == PETTA_BLOCK_EFFECT_SEMIDET) {
            CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                CETTA_PETTA_TYPECHECK_CENSUS_EVENT_EXPRESSION_EFFECT_ONCE_SEMIDET);
        } else if (inner == PETTA_BLOCK_EFFECT_NONDET) {
            CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                CETTA_PETTA_TYPECHECK_CENSUS_EVENT_EXPRESSION_EFFECT_ONCE_NONDET);
        }
        return inner == PETTA_BLOCK_EFFECT_NONDET
            ? PETTA_BLOCK_EFFECT_SEMIDET : inner;
    }
    if (strcmp(head, "let*") == 0 && expression->expr.len == 3u) {
        Atom *bindings = expression->expr.elems[1];
        if (!bindings || bindings->kind != ATOM_EXPR)
            return PETTA_BLOCK_EFFECT_UNKNOWN;
        Bindings body;
        bindings_init(&body);
        if (environment && !bindings_clone(&body, environment)) {
            bindings_free(&body);
            return PETTA_BLOCK_EFFECT_UNKNOWN;
        }
        PettaBlockEffect effect = PETTA_BLOCK_EFFECT_DET;
        for (CettaExprIndex index = 0u;
             index < bindings->expr.len; index++) {
            Atom *binding = bindings->expr.elems[index];
            if (!binding || binding->kind != ATOM_EXPR ||
                binding->expr.len != 2u) {
                bindings_free(&body);
                return PETTA_BLOCK_EFFECT_UNKNOWN;
            }
            Atom *value = binding->expr.elems[1];
            PettaBlockEffect value_effect = petta_block_expression_effect(
                check, value, &body, depth + 1u);
            effect = petta_block_effect_join(effect, value_effect);
            bool saved_mismatch = check->definite_mismatch;
            bool saved_residual = check->residual_type_guard_required;
            check->definite_mismatch = false;
            Atom *value_type = NULL;
            bool inferred = petta_block_infer_expr(
                check, value, &body, NULL, &value_type, depth + 1u);
            check->definite_mismatch = saved_mismatch;
            check->residual_type_guard_required = saved_residual;
            if (inferred && value_type)
                (void)petta_block_bind_pattern(
                    check, binding->expr.elems[0], value_type,
                    &body, depth + 1u);
        }
        effect = petta_block_effect_join(
            effect, petta_block_expression_effect(
                        check, expression->expr.elems[2],
                        &body, depth + 1u));
        CETTA_PETTA_TYPECHECK_CENSUS_HIT_IF(
            petta_block_identity_binding_value(expression) &&
                effect == PETTA_BLOCK_EFFECT_NONDET,
            CETTA_PETTA_TYPECHECK_CENSUS_EVENT_ADMINISTRATIVE_ALIAS_NONDET_PRESERVED);
        bindings_free(&body);
        return effect;
    }

    static const char *const fixed_det[] = {
        "+", "-", "*", "/", "%", "mod", "min", "max",
        "pow-math", "sqrt-math", "abs-math", "log-math",
        "trunc-math", "ceil-math", "floor-math", "round-math",
        "sin-math", "cos-math", "tan-math", "asin-math",
        "acos-math", "atan-math", "isnan-math", "isinf-math",
        "<", "<=", ">", ">=", "=", "==", "!=", "=?",
        "=alpha", "=@=", "is-var", "is-ground", "is-expr",
        "is-space", "cons", "cons-atom", "car-atom", "cdr-atom",
        "list_to_set", "list-to-set", "cut", "data", "brand", "the",
        "assert"
    };
    static const char *const fixed_semidet[] = {
        "empty", "get-metatype",
        "decons", "decons-atom", "first", "first-from-pair",
        "second-from-pair", "bind!"
    };
    static const char *const fixed_nondet[] = {
        "superpose", "hyperpose", "get-atoms", "match", "get-type",
        "member", "random-int"
    };

    PettaBlockEffect intrinsic = PETTA_BLOCK_EFFECT_UNKNOWN;
    if (((strcmp(head, "foldl-atom") == 0 &&
          expression->expr.len == 4u) ||
         ((strcmp(head, "map-atom") == 0 ||
           strcmp(head, "filter-atom") == 0) &&
          expression->expr.len == 3u))) {
        CettaExprIndex closure_index = expression->expr.len - 1u;
        CettaExprLen closure_arity = strcmp(head, "foldl-atom") == 0
            ? 2u : 1u;
        Atom *parts[4] = {0};
        parts[0] = atom_symbol(&check->scratch, "->");
        for (CettaExprIndex index = 0u; index <= closure_arity; index++) {
            parts[index + 1u] = atom_var_with_id(
                &check->scratch, "higher-order-type", fresh_var_id());
        }
        Atom *required_closure = atom_expr(
            &check->scratch, parts, closure_arity + 2u);
        PettaBlockEffect closure_effect =
            petta_block_closure_value_effect(
                check, expression->expr.elems[closure_index],
                required_closure, environment, depth + 1u);
        PettaBlockEffect list_effect =
            petta_block_value_is_proven_proper_list(
                check, expression->expr.elems[1], environment,
                depth + 1u)
                ? PETTA_BLOCK_EFFECT_DET
                : PETTA_BLOCK_EFFECT_UNKNOWN;
        intrinsic = petta_block_effect_join(
            list_effect, closure_effect);
    } else if (petta_block_name_in(
            head, fixed_det,
            sizeof(fixed_det) / sizeof(fixed_det[0])))
        intrinsic = PETTA_BLOCK_EFFECT_DET;
    else if (petta_block_name_in(
                 head, fixed_semidet,
                 sizeof(fixed_semidet) / sizeof(fixed_semidet[0])))
        intrinsic = PETTA_BLOCK_EFFECT_SEMIDET;
    else if (petta_block_name_in(
                 head, fixed_nondet,
                 sizeof(fixed_nondet) / sizeof(fixed_nondet[0]))) {
        CETTA_PETTA_TYPECHECK_CENSUS_HIT_IF(
            strcmp(head, "superpose") == 0,
            CETTA_PETTA_TYPECHECK_CENSUS_EVENT_EXPRESSION_EFFECT_SUPERPOSE_NONDET);
        return PETTA_BLOCK_EFFECT_NONDET;
    }
    else if ((strcmp(head, "add-atom") == 0 ||
              strcmp(head, "remove-atom") == 0) &&
             expression->expr.len == 3u) {
        Atom *value = expression->expr.elems[2];
        Atom *value_type = value->kind == ATOM_VAR && environment
            ? bindings_lookup_var(environment, value) : NULL;
        intrinsic = value->kind == ATOM_EXPR ||
                    petta_block_type_is_nominal(check, value_type) ||
                    petta_block_type_provably_nonfunction(value_type)
            ? PETTA_BLOCK_EFFECT_DET : PETTA_BLOCK_EFFECT_SEMIDET;
    } else if (strcmp(head, "size-atom") == 0 &&
               expression->expr.len == 2u) {
        intrinsic = petta_block_value_manifest_proper_list(
                        check, expression->expr.elems[1],
                        environment, false)
            ? PETTA_BLOCK_EFFECT_DET : PETTA_BLOCK_EFFECT_NONDET;
    } else if (strcmp(head, "index-atom") == 0 &&
               expression->expr.len == 3u) {
        intrinsic = expression->expr.elems[2]->kind == ATOM_VAR
            ? PETTA_BLOCK_EFFECT_NONDET : PETTA_BLOCK_EFFECT_SEMIDET;
    } else if ((strcmp(head, "min-atom") == 0 ||
                strcmp(head, "max-atom") == 0) &&
               expression->expr.len == 2u) {
        intrinsic = petta_block_value_manifest_proper_list(
                        check, expression->expr.elems[1],
                        environment, true)
            ? PETTA_BLOCK_EFFECT_DET : PETTA_BLOCK_EFFECT_SEMIDET;
    } else if ((strcmp(head, "union-atom") == 0 ||
                strcmp(head, "append") == 0 ||
                strcmp(head, "subtraction-atom") == 0 ||
                strcmp(head, "intersection-atom") == 0) &&
               expression->expr.len >= 2u) {
        intrinsic = petta_block_value_is_proven_proper_list(
                        check, expression->expr.elems[1],
                        environment, depth + 1u)
            ? PETTA_BLOCK_EFFECT_DET : PETTA_BLOCK_EFFECT_NONDET;
    } else if ((strcmp(head, "reverse") == 0 ||
                strcmp(head, "last") == 0) &&
               expression->expr.len == 2u) {
        intrinsic = petta_block_value_manifest_proper_list(
                        check, expression->expr.elems[1], environment,
                        strcmp(head, "last") == 0)
            ? PETTA_BLOCK_EFFECT_DET : PETTA_BLOCK_EFFECT_NONDET;
    } else if ((strcmp(head, "and") == 0 ||
                strcmp(head, "or") == 0 ||
                strcmp(head, "xor") == 0 ||
                strcmp(head, "implies") == 0 ||
                strcmp(head, "not") == 0)) {
        bool manifest = true;
        for (CettaExprIndex index = 1u;
             index < expression->expr.len; index++) {
            Atom *argument = expression->expr.elems[index];
            manifest = manifest && petta_block_value_manifest_bool(
                                       check, argument,
                                       environment, depth + 1u);
        }
        intrinsic = manifest
            ? PETTA_BLOCK_EFFECT_DET : PETTA_BLOCK_EFFECT_NONDET;
    }
    else if (strcmp(head, "collapse") == 0) {
        CETTA_PETTA_TYPECHECK_CENSUS_HIT(
            CETTA_PETTA_TYPECHECK_CENSUS_EVENT_EXPRESSION_EFFECT_COLLAPSE);
        return PETTA_BLOCK_EFFECT_DET;
    }
    else if (strcmp(head, "if") == 0 && expression->expr.len == 4u)
        intrinsic = PETTA_BLOCK_EFFECT_DET;
    else if (strcmp(head, "if") == 0 && expression->expr.len == 3u)
        intrinsic = PETTA_BLOCK_EFFECT_SEMIDET;
    else if ((strcmp(head, "let") == 0 && expression->expr.len == 4u) ||
             (strcmp(head, "let*") == 0 && expression->expr.len == 3u))
        intrinsic = PETTA_BLOCK_EFFECT_DET;
    else {
        Atom *signature = NULL;
        if (petta_block_find_signature(
                check, expression->expr.elems[0],
                expression->expr.len - 1u, &signature)) {
            intrinsic = petta_block_call_signature_effect(
                check, expression, signature, environment,
                depth + 1u);
            if (intrinsic == PETTA_BLOCK_EFFECT_UNKNOWN) {
                if (petta_block_effect_arrow_name(signature))
                    return PETTA_BLOCK_EFFECT_UNKNOWN;
                size_t clause_count = petta_block_equation_count(
                    check, expression->expr.elems[0],
                    expression->expr.len - 1u);
                if (clause_count == 0u)
                    intrinsic = PETTA_BLOCK_EFFECT_DET;
                else
                    return petta_block_inferred_call_effect(
                        check, expression, environment,
                        depth + 1u);
            }
        } else if (petta_block_equation_count(
                       check, expression->expr.elems[0],
                       expression->expr.len - 1u) == 0u) {
            /* An expression head with neither clauses nor a function
             * declaration is constructor data. */
            intrinsic = PETTA_BLOCK_EFFECT_DET;
        } else {
            return petta_block_inferred_call_effect(
                check, expression, environment, depth + 1u);
        }
    }

    PettaBlockEffect effect = intrinsic;
    if (strcmp(head, "if") == 0 && expression->expr.len == 4u) {
        effect = petta_block_effect_join(
            effect, petta_block_expression_effect(
                        check, expression->expr.elems[1],
                        environment, depth + 1u));
        Bindings then_environment;
        bindings_init(&then_environment);
        bool then_cloned = environment
            ? bindings_clone(&then_environment, environment) : true;
        Atom *condition = expression->expr.elems[1];
        if (then_cloned &&
            petta_block_head_is(condition, "is-expr") &&
            condition->expr.len == 2u &&
            condition->expr.elems[1]->kind == ATOM_VAR &&
            !bindings_lookup_var(
                &then_environment, condition->expr.elems[1])) {
            Atom *expression_type = atom_symbol(
                &check->scratch, "Expression");
            then_cloned = expression_type &&
                petta_block_env_note_fresh(
                    &then_environment, condition->expr.elems[1],
                    expression_type);
        }
        effect = petta_block_effect_join(
            effect, petta_block_expression_effect(
                        check, expression->expr.elems[2],
                        then_cloned ? &then_environment : environment,
                        depth + 1u));
        bindings_free(&then_environment);
        Atom *nonempty = petta_block_empty_list_comparison_value(
            expression->expr.elems[1]);
        size_t saved_nonempty_len = check->nonempty_value_len;
        if (nonempty && check->nonempty_value_len <
                            sizeof(check->nonempty_values) /
                                sizeof(check->nonempty_values[0])) {
            check->nonempty_values[check->nonempty_value_len++] = nonempty;
        }
        effect = petta_block_effect_join(
            effect, petta_block_expression_effect(
                        check, expression->expr.elems[3],
                        environment, depth + 1u));
        check->nonempty_value_len = saved_nonempty_len;
        return effect;
    }
    if (strcmp(head, "if") == 0 && expression->expr.len == 3u) {
        for (CettaExprIndex index = 1u; index < 3u; index++)
            effect = petta_block_effect_join(
                effect, petta_block_expression_effect(
                            check, expression->expr.elems[index],
                            environment, depth + 1u));
        return effect;
    }
    if (strcmp(head, "case") == 0 && expression->expr.len == 3u) {
        Atom *scrutinee = expression->expr.elems[1];
        Atom *branches = expression->expr.elems[2];
        effect = petta_block_effect_join(
            PETTA_BLOCK_EFFECT_DET,
            petta_block_expression_effect(
                check, scrutinee, environment, depth + 1u));
        bool catch_all = false;
        bool literal_match = false;
        bool scrutinee_ground = !atom_has_vars(scrutinee);
        if (!branches || branches->kind != ATOM_EXPR)
            return PETTA_BLOCK_EFFECT_UNKNOWN;
        for (CettaExprIndex index = 0u;
             index < branches->expr.len; index++) {
            Atom *branch = branches->expr.elems[index];
            if (!branch || branch->kind != ATOM_EXPR ||
                branch->expr.len != 2u)
                return PETTA_BLOCK_EFFECT_UNKNOWN;
            Atom *pattern = branch->expr.elems[0];
            catch_all = catch_all || pattern->kind == ATOM_VAR;
            if (scrutinee_ground) {
                Bindings match;
                bindings_init(&match);
                literal_match = literal_match ||
                    match_atoms(pattern, scrutinee, &match);
                bindings_free(&match);
            }
            effect = petta_block_effect_join(
                effect, petta_block_expression_effect(
                            check, branch->expr.elems[1],
                            environment, depth + 1u));
        }
        bool incomplete = !catch_all && scrutinee_ground && !literal_match;
        if (!catch_all && scrutinee->kind == ATOM_VAR && environment) {
            Atom *type = bindings_lookup_var(environment, scrutinee);
            const char *type_name = petta_block_symbol_name(type);
            if (type_name && (strcmp(type_name, "Number") == 0 ||
                              strcmp(type_name, "String") == 0))
                incomplete = true;
        }
        return incomplete
            ? petta_block_effect_join(
                  effect, PETTA_BLOCK_EFFECT_SEMIDET)
            : (catch_all ? effect : PETTA_BLOCK_EFFECT_UNKNOWN);
    }
    if ((strcmp(head, "let") == 0 || strcmp(head, "chain") == 0) &&
        expression->expr.len == 4u) {
        effect = petta_block_effect_join(
            effect, petta_block_expression_effect(
                        check, expression->expr.elems[2],
                        environment, depth + 1u));
        Bindings body_environment;
        bindings_init(&body_environment);
        bool cloned = environment
            ? bindings_clone(&body_environment, environment) : true;
        if (cloned && expression->expr.elems[1]->kind == ATOM_VAR) {
            Atom *value_type = NULL;
            bool saved_mismatch = check->definite_mismatch;
            check->definite_mismatch = false;
            if (petta_block_infer_expr(
                    check, expression->expr.elems[2],
                    &body_environment, NULL, &value_type,
                    depth + 1u) && value_type &&
                (!petta_block_head_is(value_type, "List") ||
                 petta_block_value_is_proven_proper_list(
                     check, expression->expr.elems[2],
                     environment, depth + 1u))) {
                petta_block_env_note_pattern(
                    check, &body_environment, expression->expr.elems[1],
                    value_type);
            }
            check->definite_mismatch = saved_mismatch;
        }
        effect = petta_block_effect_join(
            effect, petta_block_expression_effect(
                        check, expression->expr.elems[3],
                        cloned ? &body_environment : environment,
                        depth + 1u));
        bindings_free(&body_environment);
        if (expression->expr.elems[1]->kind != ATOM_VAR &&
            effect != PETTA_BLOCK_EFFECT_NONDET) {
            Atom *pattern = expression->expr.elems[1];
            Atom *value = expression->expr.elems[2];
            if (!atom_has_vars(pattern) && !atom_has_vars(value)) {
                Bindings match;
                bindings_init(&match);
                bool fits = match_atoms(pattern, value, &match);
                bindings_free(&match);
                effect = fits
                    ? effect : petta_block_effect_join(
                                   effect, PETTA_BLOCK_EFFECT_SEMIDET);
            } else {
                effect = PETTA_BLOCK_EFFECT_UNKNOWN;
            }
        }
        return effect;
    }
    for (CettaExprIndex index = 1u;
         index < expression->expr.len; index++) {
        effect = petta_block_effect_join(
            effect, petta_block_expression_effect(
                        check, expression->expr.elems[index],
                        environment, depth + 1u));
    }
    return effect;
}

static bool petta_block_body_commits(Atom *body) {
    if (!body || body->kind != ATOM_EXPR)
        return false;
    if (body->expr.len == 1u &&
        petta_block_symbol_name(body->expr.elems[0]) &&
        strcmp(petta_block_symbol_name(body->expr.elems[0]), "cut") == 0) {
        CETTA_PETTA_TYPECHECK_CENSUS_HIT(
            CETTA_PETTA_TYPECHECK_CENSUS_EVENT_CONTEXTUAL_DIRECT_CUT_COMMIT_ACCEPT);
        return true;
    }
    if (body->expr.len == 4u &&
        petta_block_symbol_name(body->expr.elems[0]) &&
        strcmp(petta_block_symbol_name(body->expr.elems[0]), "let") == 0) {
        Atom *value = body->expr.elems[2];
        if (value && value->kind == ATOM_EXPR &&
            value->expr.len == 1u &&
            petta_block_symbol_name(value->expr.elems[0]) &&
            strcmp(petta_block_symbol_name(
                       value->expr.elems[0]), "cut") == 0) {
            CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                CETTA_PETTA_TYPECHECK_CENSUS_EVENT_CONTEXTUAL_LET_CUT_COMMIT_ACCEPT);
            return true;
        }
    }
    return false;
}

static bool petta_block_clause_heads_overlap(
    PettaBlockCheck *check, Atom *left, Atom *right) {
    if (!check || !left || !right ||
        left->kind != ATOM_EXPR || right->kind != ATOM_EXPR ||
        left->expr.len != right->expr.len)
        return false;
    Atom *fresh_left = atom_freshen_epoch(
        &check->scratch, left, (uint32_t)fresh_var_id());
    Atom *fresh_right = atom_freshen_epoch(
        &check->scratch, right, (uint32_t)fresh_var_id());
    if (!fresh_left || !fresh_right)
        return true;
    Bindings bindings;
    bindings_init(&bindings);
    bool overlap = match_atoms(fresh_left, fresh_right, &bindings);
    bindings_free(&bindings);
    return overlap;
}

static bool petta_block_pattern_is_symbol(
    Atom *pattern, const char *name) {
    const char *actual = petta_block_symbol_name(pattern);
    return actual && strcmp(actual, name) == 0;
}

static bool petta_block_pattern_head_is(
    Atom *pattern, Atom *head) {
    return pattern && head && pattern->kind == ATOM_EXPR &&
           pattern->expr.len > 0u &&
           atom_eq(pattern->expr.elems[0], head);
}

static bool petta_block_pattern_total_for_type(
    Atom *pattern, Atom *type, uint32_t depth) {
    if (!pattern || !type || depth > 2048u)
        return false;
    if (pattern->kind == ATOM_VAR)
        return true;
    if (petta_block_head_is(type, "List") ||
        petta_block_head_is(type, "|") ||
        petta_block_arrow_head(type, NULL))
        return false;
    if (type->kind != ATOM_EXPR || pattern->kind != ATOM_EXPR ||
        pattern->expr.len != type->expr.len)
        return false;
    for (CettaExprIndex index = 0u;
         index < pattern->expr.len; index++) {
        if (!petta_block_pattern_total_for_type(
                pattern->expr.elems[index], type->expr.elems[index],
                depth + 1u))
            return false;
    }
    return true;
}

static bool petta_block_position_has_total_clause(
    PettaBlockCheck *check, Atom *head, CettaExprLen arity,
    CettaExprIndex position, Atom *type) {
    size_t equation_len = 0u;
    Atom *const *equations = petta_block_equations(
        check, head, arity, &equation_len);
    for (size_t index = 0u; index < equation_len; index++) {
        Atom *lhs = equations[index]->expr.elems[1];
        if (petta_block_pattern_total_for_type(
                lhs->expr.elems[position + 1u], type, 0u))
            return true;
    }
    return false;
}

static bool petta_block_position_has_variable_clause(
    PettaBlockCheck *check, Atom *head,
    CettaExprLen arity, CettaExprIndex position) {
    size_t equation_len = 0u;
    Atom *const *equations = petta_block_equations(
        check, head, arity, &equation_len);
    for (size_t index = 0u; index < equation_len; index++) {
        Atom *lhs = equations[index]->expr.elems[1];
        if (lhs->expr.elems[position + 1u]->kind == ATOM_VAR)
            return true;
    }
    return false;
}

static bool petta_block_position_covers_symbol(
    PettaBlockCheck *check, Atom *head, CettaExprLen arity,
    CettaExprIndex position, const char *value) {
    size_t equation_len = 0u;
    Atom *const *equations = petta_block_equations(
        check, head, arity, &equation_len);
    for (size_t index = 0u; index < equation_len; index++) {
        Atom *lhs = equations[index]->expr.elems[1];
        if (petta_block_pattern_is_symbol(
                lhs->expr.elems[position + 1u], value))
            return true;
    }
    return false;
}

static bool petta_block_position_covers_constructor(
    PettaBlockCheck *check, Atom *head, CettaExprLen arity,
    CettaExprIndex position, Atom *constructor) {
    size_t equation_len = 0u;
    Atom *const *equations = petta_block_equations(
        check, head, arity, &equation_len);
    for (size_t index = 0u; index < equation_len; index++) {
        Atom *lhs = equations[index]->expr.elems[1];
        Atom *pattern = lhs->expr.elems[position + 1u];
        if (atom_eq(pattern, constructor) ||
            petta_block_pattern_head_is(pattern, constructor))
            return true;
    }
    return false;
}

static bool petta_block_position_proven_exhaustive(
    PettaBlockCheck *check, Atom *head, CettaExprLen arity,
    CettaExprIndex position, Atom *type) {
    if (petta_block_position_has_variable_clause(
            check, head, arity, position) ||
        petta_block_position_has_total_clause(
            check, head, arity, position, type))
        return true;
    const char *type_name = petta_block_symbol_name(type);
    if (type_name && strcmp(type_name, "Bool") == 0) {
        bool covers_true = petta_block_position_covers_symbol(
                               check, head, arity, position, "True") ||
                           petta_block_position_covers_symbol(
                               check, head, arity, position, "true");
        bool covers_false = petta_block_position_covers_symbol(
                                check, head, arity, position, "False") ||
                            petta_block_position_covers_symbol(
                                check, head, arity, position, "false");
        return covers_true && covers_false;
    }
    if (petta_block_head_is(type, "List")) {
        bool empty = false;
        bool nonempty = false;
        size_t equation_len = 0u;
        Atom *const *equations = petta_block_equations(
            check, head, arity, &equation_len);
        for (size_t index = 0u; index < equation_len; index++) {
            Atom *lhs = equations[index]->expr.elems[1];
            Atom *pattern = lhs->expr.elems[position + 1u];
            empty = empty ||
                    (pattern->kind == ATOM_EXPR &&
                     pattern->expr.len == 0u);
            nonempty = nonempty ||
                petta_block_pattern_covers_nonempty_list(pattern, type);
        }
        return empty && nonempty;
    }
    size_t constructor_count = 0u;
    for (size_t index = 0u; index < check->declaration_len; index++) {
        PettaBlockDeclaration *decl = &check->declarations[index];
        if (decl->inferred)
            continue;
        Atom *result_type = decl->type;
        CettaExprLen constructor_arity = 0u;
        if (petta_block_arrow_head(decl->type, NULL)) {
            result_type = decl->type->expr.elems[decl->type->expr.len - 1u];
            constructor_arity = decl->type->expr.len - 2u;
        }
        if (!atom_eq(result_type, type))
            continue;
        /* A declared function with equations computes the type; it is not a
         * surviving value constructor in the closed nominal domain. */
        if (petta_block_equation_count(
                check, decl->subject, constructor_arity) > 0u)
            continue;
        constructor_count++;
        if (!petta_block_position_covers_constructor(
                check, head, arity, position, decl->subject))
            return false;
    }
    return constructor_count > 0u;
}

static bool petta_block_position_provably_incomplete(
    PettaBlockCheck *check, Atom *head, CettaExprLen arity,
    CettaExprIndex position, Atom *type) {
    if (petta_block_position_has_variable_clause(
            check, head, arity, position) ||
        petta_block_position_has_total_clause(
            check, head, arity, position, type))
        return false;
    if (petta_block_wildcard(type))
        return false;
    const char *type_name = petta_block_symbol_name(type);
    if (type_name && strcmp(type_name, "Bool") == 0) {
        bool covers_true = petta_block_position_covers_symbol(
                               check, head, arity, position, "True") ||
                           petta_block_position_covers_symbol(
                               check, head, arity, position, "true");
        bool covers_false = petta_block_position_covers_symbol(
                                check, head, arity, position, "False") ||
                            petta_block_position_covers_symbol(
                                check, head, arity, position, "false");
        return !covers_true || !covers_false;
    }
    if (type_name &&
        (strcmp(type_name, "Number") == 0 ||
         strcmp(type_name, "String") == 0))
        return true;
    /* List declarations describe element membership, not a closed two-case
     * data type.  A nonempty-only definition may carry a caller-side domain
     * proviso, so absence of the empty spine is not by itself a proof that a
     * deterministic declaration is false. */
    if (petta_block_head_is(type, "List"))
        return false;

    size_t constructor_count = 0u;
    for (size_t index = 0u; index < check->declaration_len; index++) {
        PettaBlockDeclaration *decl = &check->declarations[index];
        if (decl->inferred)
            continue;
        Atom *result_type = decl->type;
        CettaExprLen constructor_arity = 0u;
        if (petta_block_arrow_head(decl->type, NULL)) {
            result_type = decl->type->expr.elems[decl->type->expr.len - 1u];
            constructor_arity = decl->type->expr.len - 2u;
        }
        if (!atom_eq(result_type, type))
            continue;
        if (petta_block_equation_count(
                check, decl->subject, constructor_arity) > 0u)
            continue;
        constructor_count++;
        if (!petta_block_position_covers_constructor(
                check, head, arity, position, decl->subject))
            return true;
    }
    (void)constructor_count;
    return false;
}

static bool petta_block_check_exhaustive(
    PettaBlockCheck *check, Atom *head,
    CettaExprLen arity, Atom *signature) {
    if (!check || !head || !signature ||
        (petta_block_signature_effect(signature) != PETTA_BLOCK_EFFECT_DET &&
         !petta_block_effect_arrow_name(signature)))
        return true;
    for (CettaExprIndex position = 0u; position < arity; position++) {
        Atom *type = signature->expr.elems[position + 1u];
        if (petta_block_position_provably_incomplete(
                check, head, arity, position, type)) {
            return petta_block_fail(
                check, "deterministic clauses are non-exhaustive for %s/%u",
                petta_block_symbol_name(head), (unsigned)arity);
        }
    }
    return true;
}

static bool petta_block_tests_variable_boundness(
    Atom *expression, Atom *variable, uint32_t depth) {
    if (!expression || !variable || variable->kind != ATOM_VAR ||
        depth > 2048u || expression->kind != ATOM_EXPR)
        return false;
    if (petta_block_head_is(expression, "is-var") &&
        expression->expr.len == 2u &&
        expression->expr.elems[1]->kind == ATOM_VAR &&
        expression->expr.elems[1]->var_id == variable->var_id)
        return true;
    for (CettaExprIndex index = 0u;
         index < expression->expr.len; index++) {
        if (petta_block_tests_variable_boundness(
                expression->expr.elems[index], variable,
                depth + 1u))
            return true;
    }
    return false;
}

static bool petta_block_check_determinism(
    PettaBlockCheck *check) {
    if (!check)
        return false;
    for (size_t index = check->form_check_start;
         index < check->form_count; index++) {
        Atom *form = check->forms[index];
        if (!petta_block_head_is(form, "=") || form->expr.len != 3u)
            continue;
        Atom *lhs = form->expr.elems[1];
        Atom *body = form->expr.elems[2];
        if (!lhs || lhs->kind != ATOM_EXPR || lhs->expr.len == 0u ||
            lhs->expr.elems[0]->kind != ATOM_SYMBOL)
            continue;
        CettaExprLen arity = lhs->expr.len - 1u;
        Atom *signature = NULL;
        if (!petta_block_find_signature(
                check, lhs->expr.elems[0], arity, &signature))
            continue;
        bool inferred = petta_block_signature_is_inferred(
            check, lhs->expr.elems[0], signature);
        if (check->policy == PETTA_TYPECHECK_POLICY_STRICT_DET &&
            !inferred &&
            petta_block_signature_contains_plain_arrow(signature, true)) {
            return petta_block_fail(
                check, "--strict-det requires explicit effect arrows in %s/%u",
                petta_block_symbol_name(lhs->expr.elems[0]),
                (unsigned)arity);
        }
        PettaBlockEffect promised = petta_block_signature_effect(signature);
        bool conditional_effect =
            promised == PETTA_BLOCK_EFFECT_UNKNOWN &&
            petta_block_effect_arrow_name(signature);
        if (conditional_effect)
            promised = PETTA_BLOCK_EFFECT_DET;
        if (promised != PETTA_BLOCK_EFFECT_DET &&
            promised != PETTA_BLOCK_EFFECT_SEMIDET)
            continue;
        for (CettaExprIndex position = 1u;
             position < lhs->expr.len; position++) {
            Atom *pattern = lhs->expr.elems[position];
            if (pattern->kind == ATOM_EXPR && pattern->expr.len > 0u &&
                pattern->expr.elems[0]->kind == ATOM_SYMBOL &&
                petta_block_equation_count(
                    check, pattern->expr.elems[0],
                    pattern->expr.len - 1u) > 0u) {
                return petta_block_fail(
                    check, "committed head contains an executable goal for %s/%u",
                    petta_block_symbol_name(lhs->expr.elems[0]),
                    (unsigned)arity);
            }
        }
        Bindings effect_environment;
        bindings_init(&effect_environment);
        bool environment_ok = true;
        check->bound_bool_parameter_len = 0u;
        for (CettaExprIndex position = 1u;
             position < lhs->expr.len; position++) {
            Atom *parameter = lhs->expr.elems[position];
            if (parameter->kind != ATOM_VAR) {
                Atom *declared = signature->expr.elems[position];
                if (petta_block_head_is(declared, "List") &&
                    !petta_block_bind_pattern(
                        check, parameter, declared,
                        &effect_environment, 0u)) {
                    environment_ok = false;
                    break;
                }
                /* Function admission checks the argument spine.  A field
                 * exposed by a destructuring head can still be an unbound
                 * value.  Preserve only the structural List guarantee here;
                 * do not promote arbitrary declared fields to groundness
                 * facts for cardinality-sensitive builtins. */
                continue;
            }
            if (petta_block_tests_variable_boundness(
                    body, parameter, 0u))
                continue;
            const char *parameter_type = petta_block_symbol_name(
                signature->expr.elems[position]);
            if (parameter_type && strcmp(parameter_type, "Bool") == 0 &&
                check->bound_bool_parameter_len <
                    sizeof(check->bound_bool_parameters) /
                        sizeof(check->bound_bool_parameters[0])) {
                check->bound_bool_parameters[
                    check->bound_bool_parameter_len++] = parameter->var_id;
            }
            if (!petta_block_bind_pattern(
                    check, parameter,
                    signature->expr.elems[position],
                    &effect_environment, 0u)) {
                environment_ok = false;
                break;
            }
        }
        bool saved_committed_effect_evidence_withheld =
            check->committed_effect_evidence_withheld;
        check->committed_effect_evidence_withheld = false;
        PettaBlockEffect actual = environment_ok
            ? (petta_block_validate_effect_arguments(
                   check, body, &effect_environment, 0u)
                   ? petta_block_expression_effect(
                         check, body, &effect_environment, 0u)
                   : PETTA_BLOCK_EFFECT_UNKNOWN)
            : PETTA_BLOCK_EFFECT_UNKNOWN;
        bool committed_effect_evidence_withheld =
            check->committed_effect_evidence_withheld;
        check->committed_effect_evidence_withheld =
            saved_committed_effect_evidence_withheld;
        bindings_free(&effect_environment);
        if (check->result->verdict == PETTA_TYPECHECK_REFUTED)
            return false;
        if (actual == PETTA_BLOCK_EFFECT_UNKNOWN && !conditional_effect &&
            committed_effect_evidence_withheld) {
            CETTA_PETTA_TYPECHECK_CENSUS_HIT(
                CETTA_PETTA_TYPECHECK_CENSUS_EVENT_COMMITTED_EFFECT_UNKNOWN_REJECT);
            return petta_block_fail(
                check, "body cardinality is unproven for committed declaration %s/%u",
                petta_block_symbol_name(lhs->expr.elems[0]),
                (unsigned)arity);
        }
        bool effect_ok = actual == PETTA_BLOCK_EFFECT_UNKNOWN ||
            (promised == PETTA_BLOCK_EFFECT_DET
                ? actual == PETTA_BLOCK_EFFECT_DET
                : actual <= PETTA_BLOCK_EFFECT_SEMIDET);
        if (!effect_ok) {
            return petta_block_fail(
                check, "body cardinality conflicts with declaration for %s/%u",
                petta_block_symbol_name(lhs->expr.elems[0]),
                (unsigned)arity);
        }
        for (size_t prior = 0u; prior < index; prior++) {
            Atom *previous = check->forms[prior];
            if (!petta_block_head_is(previous, "=") ||
                previous->expr.len != 3u)
                continue;
            Atom *previous_lhs = previous->expr.elems[1];
            if (!previous_lhs || previous_lhs->kind != ATOM_EXPR ||
                previous_lhs->expr.len != lhs->expr.len ||
                !atom_eq(previous_lhs->expr.elems[0], lhs->expr.elems[0]))
                continue;
            if (petta_block_clause_heads_overlap(
                    check, previous_lhs, lhs) &&
                !petta_block_body_commits(previous->expr.elems[2])) {
                return petta_block_fail(
                    check, "committed clauses overlap for %s/%u",
                    petta_block_symbol_name(lhs->expr.elems[0]),
                    (unsigned)arity);
            }
        }
        bool first_clause = true;
        for (size_t prior = 0u; prior < index; prior++) {
            Atom *previous = check->forms[prior];
            if (!petta_block_head_is(previous, "=") ||
                previous->expr.len != 3u)
                continue;
            Atom *previous_lhs = previous->expr.elems[1];
            if (previous_lhs && previous_lhs->kind == ATOM_EXPR &&
                previous_lhs->expr.len == lhs->expr.len &&
                atom_eq(previous_lhs->expr.elems[0], lhs->expr.elems[0])) {
                first_clause = false;
                break;
            }
        }
        if (first_clause && !petta_block_check_exhaustive(
                                check, lhs->expr.elems[0],
                                arity, signature))
            return false;
    }
    return true;
}

static const char *petta_block_effect_arrow_name(Atom *type) {
    if (!type || type->kind != ATOM_EXPR || type->expr.len < 2u)
        return NULL;
    const char *head = petta_block_symbol_name(type->expr.elems[0]);
    size_t length = head ? strlen(head) : 0u;
    return length > 6u && strncmp(head, "-[$", 3u) == 0 &&
           strcmp(head + length - 3u, "]->") == 0
        ? head : NULL;
}

static bool petta_block_contains_effect_arrow(
    Atom *type, const char *allowed, bool *different) {
    if (!type)
        return false;
    const char *effect = petta_block_effect_arrow_name(type);
    if (effect) {
        if (different && allowed && strcmp(effect, allowed) != 0)
            *different = true;
        return true;
    }
    if (type->kind != ATOM_EXPR)
        return false;
    bool found = false;
    for (CettaExprIndex index = 0u; index < type->expr.len; index++) {
        found = petta_block_contains_effect_arrow(
                    type->expr.elems[index], allowed, different) || found;
    }
    return found;
}

static bool petta_block_validate_effect_signature(
    PettaBlockCheck *check, Atom *subject, Atom *type) {
    const char *top = petta_block_effect_arrow_name(type);
    if (!top)
        return true;
    bool instantiable = false;
    for (CettaExprIndex index = 1u;
         index + 1u < type->expr.len; index++) {
        Atom *argument = type->expr.elems[index];
        const char *argument_effect =
            petta_block_effect_arrow_name(argument);
        if (argument_effect) {
            if (strcmp(argument_effect, top) != 0) {
                return petta_block_fail(
                    check, "effect variables disagree in declaration for %s",
                    petta_block_symbol_name(subject));
            }
            instantiable = true;
            bool nested_different = false;
            for (CettaExprIndex child = 1u;
                 child < argument->expr.len; child++) {
                if (petta_block_contains_effect_arrow(
                        argument->expr.elems[child], top,
                        &nested_different)) {
                    return petta_block_fail(
                        check, "effect variable occurs outside a closure slot for %s",
                        petta_block_symbol_name(subject));
                }
            }
        } else {
            bool different = false;
            if (petta_block_contains_effect_arrow(
                    argument, top, &different)) {
                return petta_block_fail(
                    check, "effect variable occurs outside a closure slot for %s",
                    petta_block_symbol_name(subject));
            }
        }
    }
    bool output_different = false;
    if (petta_block_contains_effect_arrow(
            type->expr.elems[type->expr.len - 1u], top,
            &output_different)) {
        return petta_block_fail(
            check, "effect variable occurs in the result type for %s",
            petta_block_symbol_name(subject));
    }
    if (!instantiable) {
        return petta_block_fail(
            check, "effect declaration has no closure argument for %s",
            petta_block_symbol_name(subject));
    }
    return true;
}



/* Roman's overload rule is clause-local: head patterns filter all declarations
 * at the same arity.  Exactly one surviving declaration is checked; no
 * survivor is a type error; a genuinely ambiguous all-variable head remains
 * unchecked rather than being assigned whichever declaration happened to be
 * stored first. */
static bool petta_block_clause_signature_add(
    Atom ***signatures, size_t *length, size_t *capacity,
    Atom *signature) {
    if (!signatures || !length || !capacity || !signature)
        return false;
    for (size_t index = 0u; index < *length; index++) {
        if (atom_alpha_eq((*signatures)[index], signature))
            return true;
    }
    if (!petta_block_reserve(
            (void **)signatures, capacity, *length + 1u,
            sizeof(**signatures)))
        return false;
    (*signatures)[(*length)++] = signature;
    return true;
}

static bool petta_block_clause_signature_survives(
    PettaBlockCheck *check, Atom *lhs, Atom *signature) {
    if (!check || !lhs || lhs->kind != ATOM_EXPR ||
        !signature || signature->kind != ATOM_EXPR ||
        signature->expr.len != lhs->expr.len + 1u)
        return false;
    Atom *fresh = atom_freshen_epoch(
        &check->scratch, signature, (uint32_t)fresh_var_id());
    if (!fresh) {
        (void)petta_block_fault(
            check, PETTA_TYPECHECK_FAULT_ALLOCATION,
            "could not instantiate overload candidate");
        return false;
    }
    Bindings environment;
    bindings_init(&environment);
    bool survives = true;
    bool saved_mismatch = check->definite_mismatch;
    bool saved_residual = check->residual_type_guard_required;
    for (CettaExprIndex index = 1u;
         survives && index < lhs->expr.len; index++) {
        survives = petta_block_bind_pattern(
            check, lhs->expr.elems[index],
            fresh->expr.elems[index], &environment, 0u);
    }
    check->definite_mismatch = saved_mismatch;
    check->residual_type_guard_required = saved_residual;
    bindings_free(&environment);
    return survives;
}

static bool petta_block_select_clause_signature(
    PettaBlockCheck *check, Atom *lhs,
    Atom **signature_out, size_t *declaration_count_out,
    size_t *survivor_count_out) {
    if (signature_out)
        *signature_out = NULL;
    if (declaration_count_out)
        *declaration_count_out = 0u;
    if (survivor_count_out)
        *survivor_count_out = 0u;
    if (!check || !lhs || lhs->kind != ATOM_EXPR ||
        lhs->expr.len == 0u || !signature_out)
        return false;

    Atom *subject = lhs->expr.elems[0];
    CettaExprLen arity = lhs->expr.len - 1u;
    Atom **signatures = NULL;
    size_t signature_len = 0u;
    size_t signature_cap = 0u;

    /* Current-block declarations are not yet necessarily published to the
     * Space, so collect them first. */
    for (size_t index = 0u; index < check->declaration_len; index++) {
        PettaBlockDeclaration *decl = &check->declarations[index];
        if (!atom_eq(decl->subject, subject) ||
            !petta_block_arrow_head(decl->type, NULL) ||
            decl->type->expr.len != arity + 2u)
            continue;
        if (!petta_block_clause_signature_add(
                &signatures, &signature_len,
                &signature_cap, decl->type)) {
            free(signatures);
            return petta_block_fault(
                check, PETTA_TYPECHECK_FAULT_ALLOCATION,
                "could not collect overload declarations");
        }
    }

    Atom **published = NULL;
    uint32_t published_count = petta_block_declared_types(
        check, subject, &published);
    for (uint32_t index = 0u; index < published_count; index++) {
        Atom *type = published[index];
        if (!petta_block_arrow_head(type, NULL) ||
            type->expr.len != arity + 2u)
            continue;
        if (!petta_block_clause_signature_add(
                &signatures, &signature_len,
                &signature_cap, type)) {
            free(published);
            free(signatures);
            return petta_block_fault(
                check, PETTA_TYPECHECK_FAULT_ALLOCATION,
                "could not collect published overload declarations");
        }
    }
    free(published);

    if (signature_len == 0u && check->program &&
        subject->kind == ATOM_SYMBOL) {
        Atom **inferred = NULL;
        size_t inferred_count = 0u;
        if (!petta_block_inferred_signatures_lookup(
                check, subject->sym_id, arity,
                &inferred, &inferred_count)) {
            free(signatures);
            return petta_block_fault(
                check, PETTA_TYPECHECK_FAULT_ALLOCATION,
                "could not collect inferred overload declarations");
        }
        for (size_t index = 0u; index < inferred_count; index++) {
            if (!petta_block_clause_signature_add(
                    &signatures, &signature_len,
                    &signature_cap, inferred[index])) {
                free(inferred);
                free(signatures);
                return petta_block_fault(
                    check, PETTA_TYPECHECK_FAULT_ALLOCATION,
                    "could not collect inferred overload declaration");
            }
        }
        free(inferred);
    }

    size_t survivors = 0u;
    Atom *selected = NULL;
    for (size_t index = 0u; index < signature_len; index++) {
        if (!petta_block_clause_signature_survives(
                check, lhs, signatures[index])) {
            if (check->result->fault != PETTA_TYPECHECK_FAULT_NONE) {
                free(signatures);
                return false;
            }
            continue;
        }
        survivors++;
        selected = signatures[index];
    }
    if (declaration_count_out)
        *declaration_count_out = signature_len;
    if (survivor_count_out)
        *survivor_count_out = survivors;
    if (survivors == 1u)
        *signature_out = selected;
    free(signatures);
    return true;
}

static bool petta_block_check_equation(
    PettaBlockCheck *check, Atom *equation) {
    if (!petta_block_head_is(equation, "=") ||
        equation->expr.len != 3u)
        return true;
    Atom *lhs = equation->expr.elems[1];
    Atom *rhs = equation->expr.elems[2];
    if (!lhs || lhs->kind != ATOM_EXPR || lhs->expr.len == 0u ||
        lhs->expr.elems[0]->kind != ATOM_SYMBOL)
        return true;
    for (CettaExprIndex index = 1u; index < lhs->expr.len; index++) {
        if (!petta_block_pattern_fragment_supported(lhs->expr.elems[index]))
            return true;
    }
    bool fragment_supported =
        petta_block_expression_fragment_supported(check, rhs, 0u);
    CettaExprLen arity = lhs->expr.len - 1u;
    Atom *signature = NULL;
    size_t declaration_count = 0u;
    size_t survivor_count = 0u;
    if (!petta_block_select_clause_signature(
            check, lhs, &signature,
            &declaration_count, &survivor_count)) {
        return false;
    }
    if (!fragment_supported && declaration_count == 0u)
        return true;
    if (declaration_count == 0u) {
        if (check->policy != PETTA_TYPECHECK_POLICY_DEFAULT) {
            return petta_block_fail(
                check, "strict typing could not infer a signature for %s/%u",
                petta_block_symbol_name(lhs->expr.elems[0]),
                (unsigned)arity);
        }
        return true;
    }
    if (survivor_count == 0u) {
        return petta_block_fail(
            check, "no declared overload matches the head pattern for %s/%u",
            petta_block_symbol_name(lhs->expr.elems[0]),
            (unsigned)arity);
    }
    if (survivor_count > 1u) {
        /* Roman's authority leaves a genuinely ambiguous clause unchecked;
         * no arbitrary declaration receives semantic authority. */
        check->result->equations_checked++;
        return true;
    }
    const char *mode = NULL;
    if (!petta_block_arrow_head(signature, &mode))
        return true;
    if (check->policy == PETTA_TYPECHECK_POLICY_STRICT_DET &&
        strcmp(mode, "->") == 0 &&
        !petta_block_signature_is_inferred(
            check, lhs->expr.elems[0], signature)) {
        return petta_block_fail(
            check, "--strict-det requires an explicit arrow mode for %s/%u",
            petta_block_symbol_name(lhs->expr.elems[0]),
            (unsigned)arity);
    }
    if (!fragment_supported &&
        (!signature || signature->kind != ATOM_EXPR ||
         signature->expr.len == 0u ||
         signature->expr.elems[signature->expr.len - 1u]->kind !=
             ATOM_SYMBOL ||
         !petta_block_inert_structural_result(check, rhs))) {
        Atom *rhs_head = rhs && rhs->kind == ATOM_EXPR &&
                rhs->expr.len > 0u &&
                rhs->expr.elems[0]->kind == ATOM_SYMBOL
            ? rhs->expr.elems[0] : NULL;
        Atom *rhs_signature = NULL;
        CettaExprLen intrinsic_arity = 0u;
        bool known_intrinsic = rhs_head &&
            petta_semantics_intrinsic_partial_arity(
                rhs_head->sym_id, &intrinsic_arity) &&
            intrinsic_arity == rhs->expr.len - 1u;
        bool untyped_runtime_call = rhs_head &&
            !petta_block_find_signature(
                check, rhs_head, rhs->expr.len - 1u, &rhs_signature) &&
            petta_block_head_is_callable(
                check, rhs_head, rhs->expr.len - 1u) &&
            !known_intrinsic;
        if (untyped_runtime_call) {
            Atom *required = signature->expr.elems[
                signature->expr.len - 1u];
            petta_block_note_residual_guard(
                check, required, "unsupported-callable-rhs");
            if (check->residual_type_guard_required) {
                check->residual_type_guard_required = false;
                return petta_block_fail(
                    check,
                    "strict typing requires a residual type guard for %s/%u",
                    petta_block_symbol_name(lhs->expr.elems[0]),
                    (unsigned)arity);
            }
        }
        check->result->equations_checked++;
        return true;
    }
    Atom *fresh = atom_freshen_epoch(
        &check->scratch, signature, (uint32_t)fresh_var_id());
    if (!fresh)
        return petta_block_fault(
            check, PETTA_TYPECHECK_FAULT_ALLOCATION,
            "could not instantiate declared function type");
    Bindings environment;
    bindings_init(&environment);
    VarId *input_variables = NULL;
    size_t input_variable_len = 0u;
    size_t input_variable_cap = 0u;
    for (CettaExprIndex index = 1u;
         index + 1u < fresh->expr.len; index++) {
        if (!petta_block_collect_type_variables(
                fresh->expr.elems[index], &input_variables,
                &input_variable_len, &input_variable_cap, 0u)) {
            free(input_variables);
            bindings_free(&environment);
            return petta_block_fault(
                check, PETTA_TYPECHECK_FAULT_ALLOCATION,
                "could not snapshot declared parameter variables");
        }
    }
    for (CettaExprIndex index = 1u; index < lhs->expr.len; index++) {
        if (!petta_block_bind_pattern(
                check, lhs->expr.elems[index],
                fresh->expr.elems[index], &environment, 0u)) {
            free(input_variables);
            bindings_free(&environment);
            return petta_block_fail(
                check, "head pattern does not fit declaration for %s/%u",
                petta_block_symbol_name(lhs->expr.elems[0]),
                (unsigned)arity);
        }
    }
    bool *head_specialized = input_variable_len
        ? cetta_malloc(sizeof(*head_specialized) * input_variable_len)
        : NULL;
    if (input_variable_len && !head_specialized) {
        free(input_variables);
        bindings_free(&environment);
        return petta_block_fault(
            check, PETTA_TYPECHECK_FAULT_ALLOCATION,
            "could not snapshot head-specialized type variables");
    }
    for (size_t index = 0u; index < input_variable_len; index++) {
        Atom *variable = atom_var_with_id(
            &check->scratch, "head-type", input_variables[index]);
        Atom *resolved = variable
            ? petta_block_resolve_binding(&environment, variable)
            : NULL;
        head_specialized[index] = resolved &&
            resolved->kind != ATOM_VAR &&
            !petta_block_wildcard(resolved);
        if (!head_specialized[index]) {
            for (CettaExprIndex position = 1u;
                 position < lhs->expr.len; position++) {
                Atom *slot = fresh->expr.elems[position];
                Atom *pattern = lhs->expr.elems[position];
                if (slot->kind == ATOM_VAR &&
                    slot->var_id == input_variables[index] &&
                    pattern->kind != ATOM_VAR) {
                    head_specialized[index] = true;
                    break;
                }
            }
        }
    }
    VarId *promised_variables = NULL;
    size_t promised_variable_len = 0u;
    size_t promised_variable_cap = 0u;
    for (CettaExprIndex index = 1u;
         index + 1u < fresh->expr.len; index++) {
        Atom *instantiated = petta_block_apply_type_bindings(
            check, &environment, fresh->expr.elems[index]);
        if (!petta_block_collect_type_variables(
                instantiated, &promised_variables,
                &promised_variable_len, &promised_variable_cap, 0u)) {
            free(promised_variables);
            free(head_specialized);
            free(input_variables);
            bindings_free(&environment);
            return petta_block_fault(
                check, PETTA_TYPECHECK_FAULT_ALLOCATION,
                "could not snapshot open parameter variables");
        }
    }
    Atom *declared_output = fresh->expr.elems[fresh->expr.len - 1u];
    bool output_only_variable = declared_output->kind == ATOM_VAR &&
        !petta_block_var_id_present(
            input_variables, input_variable_len,
            declared_output->var_id);
    Atom *required = fresh->expr.elems[fresh->expr.len - 1u];
    required = petta_block_apply_type_bindings(
        check, &environment, required);
    Atom *actual = NULL;
    check->definite_mismatch = false;
    check->residual_type_guard_required = false;
    bool valid = petta_block_infer_expr(
        check, rhs, &environment, required, &actual, 0u);
    if (valid && actual &&
        !petta_block_type_compatible(
            check, actual, required, &environment)) {
        valid = false;
        check->definite_mismatch = true;
    }
    bool equation_mismatch = check->definite_mismatch;
    check->definite_mismatch = false;
    bool parametric_valid = true;
    for (size_t index = 0u;
         valid && index < promised_variable_len; index++) {
        Atom *variable = atom_var_with_id(
            &check->scratch, "promised-type",
            promised_variables[index]);
        Atom *resolved = variable
            ? petta_block_resolve_binding(&environment, variable)
            : NULL;
        if (!resolved ||
            (resolved->kind != ATOM_VAR &&
             !petta_block_wildcard(resolved))) {
            bool selected_by_head = false;
            for (size_t input = 0u;
                 input < input_variable_len; input++) {
                if (input_variables[input] == promised_variables[index]) {
                    selected_by_head = head_specialized[input];
                    break;
                }
            }
            if (!selected_by_head) {
                parametric_valid = false;
                break;
            }
        }
    }
    if (valid && output_only_variable) {
        Atom *resolved = petta_block_resolve_binding(
            &environment, declared_output);
        if (!resolved ||
            (resolved->kind != ATOM_VAR &&
             !petta_block_wildcard(resolved))) {
            parametric_valid = false;
        }
    }
    bool residual_required = check->residual_type_guard_required;
    check->residual_type_guard_required = false;
    free(promised_variables);
    free(head_specialized);
    free(input_variables);
    bindings_free(&environment);
    if (!valid && equation_mismatch)
        return petta_block_fail(
            check, "body type conflicts with declaration for %s/%u",
            petta_block_symbol_name(lhs->expr.elems[0]),
            (unsigned)arity);
    if (!parametric_valid)
        return petta_block_fail(
            check, "body specializes a universally quantified type for %s/%u",
            petta_block_symbol_name(lhs->expr.elems[0]),
            (unsigned)arity);
    if (residual_required &&
        check->policy != PETTA_TYPECHECK_POLICY_DEFAULT) {
        return petta_block_fail(
            check, "strict typing requires a residual type guard for %s/%u",
            petta_block_symbol_name(lhs->expr.elems[0]),
            (unsigned)arity);
    }
    check->result->equations_checked++;
    return true;
}

static Atom *petta_block_normalize_value_schema(
    PettaBlockCheck *check, Atom *type, uint32_t depth) {
    if (!check || !type || depth > 256u)
        return NULL;
    if (type->kind == ATOM_SYMBOL) {
        Atom *representation = petta_block_decl_representation(
            check, type, "Alias");
        if (!representation) {
            representation = petta_block_decl_representation(
                check, type, "Newtype");
        }
        return representation
            ? petta_block_normalize_value_schema(
                  check, representation, depth + 1u)
            : type;
    }
    if (type->kind != ATOM_EXPR)
        return type;
    Atom **elements = type->expr.len
        ? cetta_malloc(sizeof(*elements) * (size_t)type->expr.len)
        : NULL;
    for (CettaExprIndex index = 0u; index < type->expr.len; index++) {
        elements[index] = petta_block_normalize_value_schema(
            check, type->expr.elems[index], depth + 1u);
        if (!elements[index]) {
            free(elements);
            return NULL;
        }
    }
    Atom *normalized = atom_expr(
        &check->scratch, elements, type->expr.len);
    free(elements);
    return normalized;
}

static bool petta_block_validate_spaceof_rows(
    PettaBlockCheck *check) {
    if (!check || !check->registry)
        return true;
    for (size_t declaration = 0u;
         declaration < check->declaration_len; declaration++) {
        PettaBlockDeclaration *decl =
            &check->declarations[declaration];
        if (!petta_block_head_is(decl->type, "SpaceOf") ||
            decl->type->expr.len != 2u)
            continue;
        Space *target = resolve_space(check->registry, decl->subject);
        if (!target)
            continue;
        Atom *schema = petta_block_normalize_value_schema(
            check, decl->type->expr.elems[1], 0u);
        if (!schema)
            return petta_block_fault(
                check, PETTA_TYPECHECK_FAULT_ALLOCATION,
                "could not normalize SpaceOf schema");
        CettaCount row_count = space_length64(target);
        for (CettaIndex row_index = 0u;
             row_index < row_count; row_index++) {
            Atom *row = space_get_at64(target, row_index);
            if (!row)
                continue;
            PettaTypecheckResult row_result;
            if (!petta_typecheck_value(
                    check->space, &check->scratch,
                    row, schema, NULL, &row_result)) {
                return petta_block_fault(
                    check, row_result.fault,
                    "could not validate existing SpaceOf row");
            }
            if (row_result.verdict == PETTA_TYPECHECK_REFUTED) {
                return petta_block_fail(
                    check, "existing row conflicts with SpaceOf schema for %s",
                    petta_block_symbol_name(decl->subject));
            }
        }
    }
    return true;
}

/*
 * Signature inference is a least-fixed-point service for genuinely untyped
 * PeTTa relations.  Recomputing it over every previously loaded equation on
 * each imported, explicitly typed source block makes a multi-file program
 * quadratic in its cumulative source size.  Determine whether the current
 * transition actually introduces an equation without an explicit arrow
 * declaration.  Fully annotated blocks can be checked against the live
 * catalog and relation index without rebuilding unrelated inferred facts.
 *
 * This is conservative: inferred signatures never count as explicit.  A new
 * equation for an inferred head still requests a complete rebuild.  If the
 * prior inferred cache is already stale and this block needs no inference,
 * the caller drops it rather than revalidating stale facts.
 */
static bool petta_typecheck_explicit_arrow_for(
    Atom *const *annotations, size_t annotation_count,
    Atom *head, CettaExprLen arity) {
    if (!annotations || !head || head->kind != ATOM_SYMBOL)
        return false;
    for (size_t index = 0u; index < annotation_count; index++) {
        Atom *annotation = annotations[index];
        if (!petta_block_head_is(annotation, ":") ||
            annotation->expr.len != 3u ||
            !atom_eq(annotation->expr.elems[1], head)) {
            continue;
        }
        Atom *type = annotation->expr.elems[2];
        if (petta_block_arrow_head(type, NULL) &&
            type->expr.len == arity + 2u &&
            !petta_block_signature_contains_plain_arrow(type, false)) {
            return true;
        }
    }
    return false;
}

static bool petta_typecheck_block_needs_signature_inference(
    PettaProgram *program, Atom *const *forms, size_t form_count,
    bool *needed) {
    if (needed)
        *needed = false;
    if (!needed || (!forms && form_count != 0u))
        return false;

    /* The question is per equation head, so ask it per subject.  A whole
     * catalog snapshot would answer the same question in O(D) per block and
     * is the scan this gate exists to avoid. */
    Arena probe;
    arena_init(&probe);
    bool ok = true;
    for (size_t index = 0u; index < form_count; index++) {
        Atom *form = forms[index];
        if (!petta_block_head_is(form, "=") ||
            form->expr.len != 3u)
            continue;
        Atom *lhs = form->expr.elems[1];
        if (!lhs || lhs->kind != ATOM_EXPR ||
            lhs->expr.len == 0u ||
            lhs->expr.elems[0]->kind != ATOM_SYMBOL)
            continue;
        Atom *head = lhs->expr.elems[0];
        CettaExprLen arity = lhs->expr.len - 1u;
        if (petta_typecheck_explicit_arrow_for(
                forms, form_count, head, arity))
            continue;
        bool declared = false;
        if (program) {
            Atom **types = NULL;
            uint32_t type_count = petta_program_declared_types(
                program, head, &probe, &types);
            for (uint32_t slot = 0u; slot < type_count; slot++) {
                Atom *type = types[slot];
                if (petta_block_arrow_head(type, NULL) &&
                    type->expr.len == arity + 2u &&
                    !petta_block_signature_contains_plain_arrow(
                        type, false)) {
                    declared = true;
                    break;
                }
            }
            free(types);
        }
        if (!declared) {
            *needed = true;
            break;
        }
    }
    arena_free(&probe);
    return ok;
}

static bool petta_typecheck_declaration_block_internal(
    const CettaNikDirectAuthorityStampV1 *authority,
    PettaProgram *program, Space *space, Registry *registry,
    Atom *const *forms, size_t form_count,
    PettaTypecheckPolicy policy, bool revalidate_live,
    PettaTypecheckBlockResult *result) {
    if (!result)
        return false;
    *result = (PettaTypecheckBlockResult){
        .verdict = PETTA_TYPECHECK_ESTABLISHED,
        .fault = PETTA_TYPECHECK_FAULT_NONE,
    };
    if (!space || (!forms && form_count != 0u)) {
        result->fault = PETTA_TYPECHECK_FAULT_INVALID_ARGUMENT;
        return false;
    }
    bool inference_needed = false;
    if (!petta_typecheck_block_needs_signature_inference(
            program, forms, form_count, &inference_needed)) {
        result->fault = PETTA_TYPECHECK_FAULT_ALLOCATION;
        snprintf(
            result->diagnostic, sizeof(result->diagnostic),
            "could not inspect PeTTa signature-inference frontier");
        return false;
    }
    bool inferred_current = program &&
        petta_block_inferred_signatures_current(
            program, space, authority);
    /* Revalidating live equations requires the live equations to be in the
     * check.  A late clause is exactly the mutation that can withdraw an
     * earlier judgment, so skipping the rebuild here lets a stale result
     * survive the change that refutes it. */
    bool rebuild_inferred = program && (inference_needed || revalidate_live);
    if (program && !inferred_current && !rebuild_inferred) {
        /* Stale inferred facts are never promoted across a revision merely
         * because the new block is explicitly typed.  Dropping them is
         * conservative; a later genuinely untyped block rebuilds the fixed
         * point from the complete live equation snapshot. */
        petta_block_inferred_signatures_reset(
            program, space, authority);
    }
    Atom **live_equations = NULL;
    size_t live_equation_count = 0u;
    Atom **working_forms = NULL;
    size_t working_form_count = form_count;
    if (rebuild_inferred) {
        if (!petta_program_equation_snapshot(
                program, space,
                &live_equations, &live_equation_count) ||
            live_equation_count > SIZE_MAX - form_count ||
            live_equation_count + form_count >
                SIZE_MAX / sizeof(*working_forms)) {
            free(live_equations);
            result->fault = PETTA_TYPECHECK_FAULT_ALLOCATION;
            snprintf(
                result->diagnostic, sizeof(result->diagnostic),
                "could not snapshot live PeTTa inference context");
            return false;
        }
        working_form_count = live_equation_count + form_count;
        working_forms = working_form_count
            ? cetta_malloc(sizeof(*working_forms) * working_form_count)
            : NULL;
        if (live_equation_count) {
            memcpy(
                working_forms, live_equations,
                sizeof(*working_forms) * live_equation_count);
        }
        if (form_count) {
            memcpy(
                working_forms + live_equation_count, forms,
                sizeof(*working_forms) * form_count);
        }
        free(live_equations);
    }
    PettaBlockCheck check = {
        .program = program,
        .space = space,
        .registry = registry,
        .authority = authority,
        .forms = rebuild_inferred ? working_forms : forms,
        .form_count = working_form_count,
        .form_check_start = rebuild_inferred
            ? (revalidate_live ? 0u : live_equation_count) : 0u,
        .forms_include_live_equations = rebuild_inferred,
        .policy = policy,
        .result = result,
    };
    arena_init(&check.scratch);

    /* Imported declarations are part of one transactional PeTTa catalog.
     * Subject-local lookup is sufficient for ordinary call typing, but case
     * exhaustiveness and constructor-shape narrowing need the complete set of
     * result-producing declarations.  Load that catalog before the current
     * source block, then add the block's declarations below. */
    Atom **catalog_annotations = NULL;
    size_t catalog_annotation_count = 0u;
    if (program && !petta_program_type_annotation_snapshot(
                       program, &catalog_annotations,
                       &catalog_annotation_count)) {
        petta_block_fault(
            &check, PETTA_TYPECHECK_FAULT_ALLOCATION,
            "could not snapshot PeTTa type annotation catalog");
    }
    for (size_t index = 0u;
         result->fault == PETTA_TYPECHECK_FAULT_NONE &&
         index < catalog_annotation_count; index++) {
        Atom *annotation = catalog_annotations[index];
        if (!petta_block_head_is(annotation, ":") ||
            annotation->expr.len != 3u ||
            annotation->expr.elems[1]->kind != ATOM_SYMBOL)
            continue;
        Atom *subject = annotation->expr.elems[1];
        Atom *type = annotation->expr.elems[2];
        if (!petta_block_has_declaration(&check, subject, type) &&
            !petta_block_add_declaration(&check, subject, type)) {
            petta_block_fault(
                &check, PETTA_TYPECHECK_FAULT_ALLOCATION,
                "could not record imported PeTTa declaration");
        }
    }
    free(catalog_annotations);

    for (size_t index = 0u; index < form_count; index++) {
        Atom *form = forms[index];
        if (!petta_block_head_is(form, ":") ||
            form->expr.len != 3u ||
            form->expr.elems[1]->kind != ATOM_SYMBOL)
            continue;
        if (petta_block_infix_arrow_misuse(
                form->expr.elems[2], 0u)) {
            petta_block_fail(
                &check, "infix arrow syntax in declaration for %s",
                petta_block_symbol_name(form->expr.elems[1]));
            break;
        }
        if (!petta_block_has_declaration(
                &check, form->expr.elems[1], form->expr.elems[2]) &&
            !petta_block_add_declaration(
                &check, form->expr.elems[1], form->expr.elems[2])) {
            petta_block_fault(
                &check, PETTA_TYPECHECK_FAULT_ALLOCATION,
                "could not record PeTTa declaration");
            break;
        }
        result->declarations_seen++;
        if (!petta_block_validate_effect_signature(
                &check, form->expr.elems[1], form->expr.elems[2]))
            break;
    }
    if (result->fault == PETTA_TYPECHECK_FAULT_NONE) {
        petta_block_validate_spaceof_rows(&check);
    }
    if (result->fault == PETTA_TYPECHECK_FAULT_NONE &&
        result->verdict != PETTA_TYPECHECK_REFUTED) {
        petta_block_infer_signatures(&check);
    }
    if (result->fault == PETTA_TYPECHECK_FAULT_NONE) {
        for (size_t index = check.form_check_start;
             index < check.form_count; index++) {
            if (!petta_block_check_equation(
                    &check, check.forms[index]))
                break;
        }
    }
    if (result->fault == PETTA_TYPECHECK_FAULT_NONE &&
        result->verdict != PETTA_TYPECHECK_REFUTED)
        petta_block_check_determinism(&check);
    if (result->fault == PETTA_TYPECHECK_FAULT_NONE &&
        result->verdict != PETTA_TYPECHECK_REFUTED)
        petta_block_check_exec_payloads(&check);
    if (program &&
        result->fault == PETTA_TYPECHECK_FAULT_NONE &&
        result->verdict != PETTA_TYPECHECK_REFUTED) {
        if (rebuild_inferred) {
            petta_block_inferred_signatures_reset(
                program, space, authority);
            if (!petta_block_inferred_signatures_current(
                    program, space, authority)) {
                petta_block_fault(
                    &check, PETTA_TYPECHECK_FAULT_ALLOCATION,
                    "could not initialize inferred signature index");
            }
        }
        for (size_t index = 0u;
             result->fault == PETTA_TYPECHECK_FAULT_NONE &&
             index < check.declaration_len; index++) {
            PettaBlockDeclaration *declaration =
                &check.declarations[index];
            if (!declaration->subject ||
                declaration->subject->kind != ATOM_SYMBOL)
                continue;
            if (!declaration->inferred) {
                petta_program_inferred_signature_remove_head(
                    program, space, declaration->subject->sym_id);
                continue;
            }
            if (!declaration->type ||
                declaration->type->kind != ATOM_EXPR ||
                declaration->type->expr.len < 2u ||
                !petta_block_inferred_signature_put(
                    program, space, authority,
                    declaration->subject->sym_id,
                    declaration->type->expr.len - 2u,
                    declaration->type)) {
                petta_block_inferred_signatures_reset(
                    program, space, authority);
                petta_block_fault(
                    &check, PETTA_TYPECHECK_FAULT_ALLOCATION,
                    "could not retain inferred PeTTa signature");
            }
        }
    }
    for (size_t index = 0u;
         index < check.relation_view_len; index++)
        free(check.relation_views[index].equations);
    free(check.relation_views);
    free(check.dynamic_pattern_vars);
    free(check.inference_tainted_vars);
    for (size_t index = 0u;
         index < check.declared_type_memo_cap; index++) {
        if (check.declared_type_memo[index].occupied)
            free(check.declared_type_memo[index].types);
    }
    free(check.declared_type_memo);
    free(check.declaration_buckets);
    free(check.declarations);
    arena_free(&check.scratch);
    free(working_forms);
    return result->fault == PETTA_TYPECHECK_FAULT_NONE;
}

bool petta_typecheck_declaration_block(
    PettaProgram *program, Space *space, Registry *registry,
    Atom *const *forms, size_t form_count,
    PettaTypecheckPolicy policy, PettaTypecheckBlockResult *result) {
    if (program && !petta_program_enable_analysis(program))
        return false;
    return petta_typecheck_declaration_block_internal(
        NULL, program, space, registry, forms, form_count,
        policy, false, result);
}

bool petta_typecheck_declaration_block_under_authority(
    const CettaNikDirectAuthorityV1 *authority,
    PettaProgram *program, Space *space, Registry *registry,
    Atom *const *forms, size_t form_count,
    PettaTypecheckPolicy policy, PettaTypecheckBlockResult *result) {
    if (!result)
        return false;
    CettaNikDirectAuthorityStampV1 stamp;
    if (!cetta_nik_direct_authority_v1_stamp(
            authority, (uint32_t)policy, &stamp)) {
        *result = (PettaTypecheckBlockResult){
            .verdict = PETTA_TYPECHECK_ESTABLISHED,
            .fault = PETTA_TYPECHECK_FAULT_INVALID_ARGUMENT,
        };
        snprintf(
            result->diagnostic, sizeof(result->diagnostic),
            "invalid direct NIK authority for PeTTa declaration checking");
        return false;
    }
    if (program && !petta_program_enable_analysis(program)) {
        *result = (PettaTypecheckBlockResult){
            .verdict = PETTA_TYPECHECK_ESTABLISHED,
            .fault = PETTA_TYPECHECK_FAULT_ALLOCATION,
        };
        return false;
    }
    return petta_typecheck_declaration_block_internal(
        &stamp, program, space, registry, forms, form_count,
        policy, false, result);
}

bool petta_typecheck_declaration_block_selected(
    PettaProgram *program, Space *space, Registry *registry,
    Atom *const *forms, size_t form_count,
    PettaTypecheckPolicy policy, PettaTypecheckBlockResult *result) {
    return petta_typecheck_declaration_block_under_authority(
        &petta_typecheck_v2_direct_authority_v1,
        program, space, registry, forms, form_count,
        policy, result);
}

bool petta_typecheck_declaration_admission_under_authority(
    const CettaNikDirectAuthorityV1 *authority,
    PettaProgram *program, Space *space, Registry *registry,
    Atom *const *forms, size_t form_count,
    PettaTypecheckPolicy policy, PettaTypecheckBlockResult *result) {
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PETTA_TYPECHECK_DECLARATION_ADMISSION_ATTEMPT);
    bool judged = petta_typecheck_declaration_block_under_authority(
        authority, program, space, registry, forms, form_count,
        policy, result);
    if (!judged || !result) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PETTA_TYPECHECK_DECLARATION_ADMISSION_FAULT);
    } else if (result->verdict == PETTA_TYPECHECK_REFUTED) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PETTA_TYPECHECK_DECLARATION_ADMISSION_REFUTED);
    } else {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PETTA_TYPECHECK_DECLARATION_ADMISSION_ACCEPTED);
    }
    return judged;
}

bool petta_typecheck_declaration_admission_selected(
    PettaProgram *program, Space *space, Registry *registry,
    Atom *const *forms, size_t form_count,
    PettaTypecheckPolicy policy, PettaTypecheckBlockResult *result) {
    return petta_typecheck_declaration_admission_under_authority(
        &petta_typecheck_v2_direct_authority_v1,
        program, space, registry, forms, form_count,
        policy, result);
}

void petta_typecheck_inferred_signatures_rebase_selected(
    PettaProgram *program, Space *space, PettaTypecheckPolicy policy) {
    if (!program || !space)
        return;
    CettaNikDirectAuthorityStampV1 stamp;
    if (cetta_nik_direct_authority_v1_stamp(
            &petta_typecheck_v2_direct_authority_v1,
            (uint32_t)policy, &stamp)) {
        petta_program_inferred_signatures_rebase_under_authority(
            program, space, &stamp);
    }
}

static bool petta_typecheck_committed_signature(
    Atom *type, CettaExprLen arity) {
    if (!type || type->kind != ATOM_EXPR ||
        type->expr.len != arity + 2u)
        return false;
    const char *head = petta_block_symbol_name(type->expr.elems[0]);
    if (!head)
        return false;
    return strcmp(head, "-[det]->") == 0 ||
           strcmp(head, "-[deterministic]->") == 0 ||
           strcmp(head, "-[semidet]->") == 0 ||
           strcmp(head, "-[semideterministic]->") == 0 ||
           (strncmp(head, "-[$", 3u) == 0 &&
            strlen(head) > 5u &&
            strcmp(head + strlen(head) - 3u, "]->") == 0);
}

static Atom *petta_typecheck_boundary_operand(Atom *atom) {
    while (atom && atom->kind == ATOM_EXPR && atom->expr.len == 3u &&
           (petta_block_head_is(atom, "the") ||
            petta_block_head_is(atom, "brand")))
        atom = atom->expr.elems[2];
    return atom;
}

static bool petta_typecheck_boundary_is_variable(
    Atom *atom, VarId variable) {
    atom = petta_typecheck_boundary_operand(atom);
    return atom && atom->kind == ATOM_VAR && atom->var_id == variable;
}

/* Derive only facts that are logically forced by one boolean observation.
 * The result is intentionally incomplete: an unrecognized guard contributes
 * no fact, so the committed boundary remains conservative. */
static PettaTypecheckBoundaryFacts
petta_typecheck_condition_facts(
    Atom *condition, VarId variable, bool truth, uint32_t depth) {
    if (!condition || depth > 2048u ||
        condition->kind != ATOM_EXPR || condition->expr.len == 0u)
        return PETTA_TYPECHECK_BOUNDARY_FACT_NONE;

    const char *head = petta_block_symbol_name(condition->expr.elems[0]);
    if (!head)
        return PETTA_TYPECHECK_BOUNDARY_FACT_NONE;

    if (strcmp(head, "is-expr") == 0 && condition->expr.len == 2u &&
        truth && petta_typecheck_boundary_is_variable(
            condition->expr.elems[1], variable)) {
        return PETTA_TYPECHECK_BOUNDARY_FACT_NONVAR |
               PETTA_TYPECHECK_BOUNDARY_FACT_PROPER_LIST;
    }
    if (strcmp(head, "==") == 0 && condition->expr.len == 3u && !truth) {
        Atom *left = condition->expr.elems[1];
        Atom *right = condition->expr.elems[2];
        bool left_empty = left->kind == ATOM_EXPR && left->expr.len == 0u;
        bool right_empty = right->kind == ATOM_EXPR && right->expr.len == 0u;
        Atom *other = left_empty != right_empty
            ? (left_empty ? right : left) : NULL;
        if (petta_typecheck_boundary_is_variable(other, variable)) {
            return PETTA_TYPECHECK_BOUNDARY_FACT_NONVAR |
                   PETTA_TYPECHECK_BOUNDARY_FACT_PROPER_LIST |
                   PETTA_TYPECHECK_BOUNDARY_FACT_NONEMPTY_EXPRESSION;
        }
    }
    if (strcmp(head, "=") == 0 && condition->expr.len == 3u && truth) {
        Atom *left = condition->expr.elems[1];
        Atom *right = condition->expr.elems[2];
        Atom *pattern = petta_typecheck_boundary_is_variable(left, variable)
            ? right
            : (petta_typecheck_boundary_is_variable(right, variable)
                ? left : NULL);
        if (petta_block_head_is(pattern, "cons") &&
            pattern->expr.len == 3u) {
            return PETTA_TYPECHECK_BOUNDARY_FACT_NONVAR |
                   PETTA_TYPECHECK_BOUNDARY_FACT_NONEMPTY_EXPRESSION;
        }
    }
    if (strcmp(head, "is-var") == 0 && condition->expr.len == 2u &&
        !truth && petta_typecheck_boundary_is_variable(
            condition->expr.elems[1], variable)) {
        return PETTA_TYPECHECK_BOUNDARY_FACT_NONVAR;
    }
    if (strcmp(head, "not") == 0 && condition->expr.len == 2u) {
        return petta_typecheck_condition_facts(
            condition->expr.elems[1], variable, !truth, depth + 1u);
    }
    if (strcmp(head, "and") == 0 && truth) {
        PettaTypecheckBoundaryFacts facts =
            PETTA_TYPECHECK_BOUNDARY_FACT_NONE;
        for (CettaExprIndex index = 1u;
             index < condition->expr.len; index++) {
            facts = petta_analysis_boundary_facts_join(
                facts,
                petta_typecheck_condition_facts(
                    condition->expr.elems[index], variable,
                    true, depth + 1u));
        }
        return facts;
    }
    if (strcmp(head, "or") == 0 && !truth) {
        PettaTypecheckBoundaryFacts facts =
            PETTA_TYPECHECK_BOUNDARY_FACT_NONE;
        for (CettaExprIndex index = 1u;
             index < condition->expr.len; index++) {
            facts = petta_analysis_boundary_facts_join(
                facts,
                petta_typecheck_condition_facts(
                    condition->expr.elems[index], variable,
                    false, depth + 1u));
        }
        return facts;
    }
    return PETTA_TYPECHECK_BOUNDARY_FACT_NONE;
}

/* This is the native form of Roman's required_bound analysis event.  It does
 * not infer boundness from a declared type: it records a proviso only where a
 * cardinality-sensitive builtin actually consumes the direct parameter.
 *
 * Conditional branches are analysed with facts established by their guards.
 * This distinction is load-bearing: `(if (is-expr $x) (fold ... $x) ...)`
 * does not impose a proper-list boundary on the caller, because the consumer
 * is reached only after the runtime guard has established that fact. */
static PettaTypecheckBoundaryRequirement
petta_typecheck_expression_boundary_requirement_with_facts(
    Atom *expression, VarId variable,
    PettaTypecheckBoundaryFacts facts, uint32_t depth) {
    if (!expression || depth > 2048u ||
        expression->kind != ATOM_EXPR || expression->expr.len == 0u)
        return PETTA_TYPECHECK_BOUNDARY_NONE;

    const char *head = petta_block_symbol_name(expression->expr.elems[0]);
    PettaTypecheckBoundaryRequirement requirement =
        PETTA_TYPECHECK_BOUNDARY_NONE;

    /* `if` is the principal path-sensitive boundary in Roman's analyses.
     * Analyse the condition itself, then each branch under the facts forced
     * by the selected outcome.  Do not also traverse the branches below with
     * the weaker ambient facts. */
    if (head && strcmp(head, "if") == 0 &&
        (expression->expr.len == 3u || expression->expr.len == 4u)) {
        Atom *condition = expression->expr.elems[1];
        requirement = petta_analysis_boundary_requirement_join(
            requirement,
            petta_typecheck_expression_boundary_requirement_with_facts(
                condition, variable, facts, depth + 1u));
        PettaTypecheckBoundaryFacts true_facts =
            petta_analysis_boundary_facts_join(
                facts,
                petta_typecheck_condition_facts(
                    condition, variable, true, depth + 1u));
        requirement = petta_analysis_boundary_requirement_join(
            requirement,
            petta_typecheck_expression_boundary_requirement_with_facts(
                expression->expr.elems[2], variable,
                true_facts, depth + 1u));
        if (expression->expr.len == 4u) {
            PettaTypecheckBoundaryFacts false_facts =
                petta_analysis_boundary_facts_join(
                    facts,
                    petta_typecheck_condition_facts(
                        condition, variable, false, depth + 1u));
            requirement = petta_analysis_boundary_requirement_join(
                requirement,
                petta_typecheck_expression_boundary_requirement_with_facts(
                    expression->expr.elems[3], variable,
                    false_facts, depth + 1u));
        }
        return requirement;
    }

    if (head) {
        if ((strcmp(head, "car-atom") == 0 ||
             strcmp(head, "cdr-atom") == 0) &&
            expression->expr.len == 2u &&
            petta_typecheck_boundary_is_variable(
                expression->expr.elems[1], variable) &&
            !petta_analysis_boundary_facts_satisfy(
                facts, PETTA_TYPECHECK_BOUNDARY_NONEMPTY_EXPRESSION)) {
            requirement = PETTA_TYPECHECK_BOUNDARY_NONEMPTY_EXPRESSION;
        }
        static const char *const boolean_consumers[] = {
            "and", "or", "not", "xor", "implies",
        };
        if (petta_block_name_in(
                head, boolean_consumers,
                sizeof(boolean_consumers) /
                    sizeof(boolean_consumers[0]))) {
            for (CettaExprIndex index = 1u;
                 index < expression->expr.len; index++) {
                if (petta_typecheck_boundary_is_variable(
                        expression->expr.elems[index], variable) &&
                    !petta_analysis_boundary_facts_satisfy(
                        facts, PETTA_TYPECHECK_BOUNDARY_NONVAR)) {
                    requirement = PETTA_TYPECHECK_BOUNDARY_NONVAR;
                }
            }
        }

        CettaExprIndex proper_position = expression->expr.len;
        static const char *const first_list_consumers[] = {
            "reverse", "last", "min-atom", "max-atom",
            "size-atom", "length", "alpha-unique-atom",
            "union-atom", "append", "subtraction-atom",
            "intersection-atom", "map-atom", "foldl-atom",
        };
        if (petta_block_name_in(
                head, first_list_consumers,
                sizeof(first_list_consumers) /
                    sizeof(first_list_consumers[0]))) {
            proper_position = 1u;
        } else if (strcmp(head, "exclude-item") == 0) {
            proper_position = 2u;
        } else if (strcmp(head, "map-flat") == 0 ||
                   strcmp(head, "map-nested") == 0) {
            proper_position = 2u;
        } else if (strcmp(head, "fold-flat") == 0 ||
                   strcmp(head, "foldr-flat") == 0 ||
                   strcmp(head, "fold-nested") == 0) {
            proper_position = 3u;
        }
        if (proper_position < expression->expr.len &&
            petta_typecheck_boundary_is_variable(
                expression->expr.elems[proper_position], variable) &&
            !petta_analysis_boundary_facts_satisfy(
                facts, PETTA_TYPECHECK_BOUNDARY_PROPER_LIST)) {
            requirement = PETTA_TYPECHECK_BOUNDARY_PROPER_LIST;
        }
    }

    for (CettaExprIndex index = 0u;
         index < expression->expr.len; index++) {
        requirement = petta_analysis_boundary_requirement_join(
            requirement,
            petta_typecheck_expression_boundary_requirement_with_facts(
                expression->expr.elems[index], variable,
                facts, depth + 1u));
    }
    return requirement;
}

static PettaTypecheckBoundaryRequirement
petta_typecheck_expression_boundary_requirement(
    Atom *expression, VarId variable, uint32_t depth) {
    return petta_typecheck_expression_boundary_requirement_with_facts(
        expression, variable, PETTA_TYPECHECK_BOUNDARY_FACT_NONE, depth);
}

bool petta_typecheck_call_boundary_plan(
    PettaProgram *program, Space *space,
    SymbolId head, CettaExprLen arity,
    PettaTypecheckBoundaryRequirement *requirements,
    size_t requirement_count) {
    if (!program || !space || head == SYMBOL_ID_NONE ||
        !cetta_expr_len_fits_size(arity) ||
        requirement_count != (size_t)arity ||
        (arity > 0u && !requirements)) {
        return false;
    }
    for (size_t index = 0u; index < requirement_count; index++)
        requirements[index] = PETTA_TYPECHECK_BOUNDARY_NONE;

    Arena scratch;
    arena_init(&scratch);
    arena_set_runtime_kind(&scratch, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    Atom *subject = atom_symbol_id(&scratch, head);
    Atom **types = NULL;
    uint32_t type_count = subject
        ? space_get_declared_types(space, &scratch, subject, &types)
        : 0u;
    bool committed = false;
    for (uint32_t index = 0u; index < type_count; index++) {
        if (petta_typecheck_committed_signature(types[index], arity)) {
            committed = true;
            break;
        }
    }
    free(types);
    if (!subject) {
        arena_free(&scratch);
        return false;
    }
    PettaClauseCandidate *clauses = NULL;
    size_t clause_count = 0u;
    if (!petta_program_clause_snapshot(
            program, space, head, &clauses, &clause_count)) {
        arena_free(&scratch);
        return false;
    }
    for (size_t clause_index = 0u;
         clause_index < clause_count; clause_index++) {
        Atom *equation = clauses[clause_index].equation;
        Atom *lhs = petta_block_head_is(equation, "=") &&
                    equation->expr.len == 3u
            ? equation->expr.elems[1] : NULL;
        if (!lhs || lhs->kind != ATOM_EXPR ||
            lhs->expr.len != arity + 1u)
            continue;
        Atom *rhs = equation->expr.elems[2];
        for (CettaExprIndex position = 0u;
             position < arity; position++) {
            Atom *parameter = lhs->expr.elems[position + 1u];
            if (!parameter || parameter->kind != ATOM_VAR)
                continue;
            requirements[position] =
                petta_analysis_boundary_requirement_join(
                    requirements[position],
                    petta_typecheck_expression_boundary_requirement(
                        rhs, parameter->var_id, 0u));
        }
    }
    free(clauses);
    arena_free(&scratch);

    if (!committed) {
        for (size_t index = 0u; index < requirement_count; index++) {
            if (requirements[index] !=
                PETTA_TYPECHECK_BOUNDARY_NONEMPTY_EXPRESSION) {
                requirements[index] = PETTA_TYPECHECK_BOUNDARY_NONE;
            }
        }
    }
    return true;
}

bool petta_typecheck_call_boundary_requirement(
    PettaProgram *program, Space *space,
    SymbolId head, CettaExprLen arity, CettaExprIndex position,
    PettaTypecheckBoundaryRequirement *requirement) {
    if (requirement)
        *requirement = PETTA_TYPECHECK_BOUNDARY_NONE;
    if (!requirement || position >= arity ||
        !cetta_expr_len_fits_size(arity))
        return false;
    size_t count = (size_t)arity;
    PettaTypecheckBoundaryRequirement *plan =
        count == 0u ? NULL : cetta_malloc(sizeof(*plan) * count);
    bool ok = petta_typecheck_call_boundary_plan(
        program, space, head, arity, plan, count);
    if (ok)
        *requirement = plan[position];
    free(plan);
    return ok;
}

static bool petta_typecheck_validate_target_row(
    Space *program_space, Registry *registry,
    Space *target_space, Atom *row,
    PettaTypecheckBlockResult *result) {
    if (!registry)
        return true;
    Arena scratch;
    arena_init(&scratch);
    arena_set_runtime_kind(&scratch, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    for (uint32_t entry_index = 0u;
         entry_index < registry->len; entry_index++) {
        RegistryEntry *entry = &registry->entries[entry_index];
        Atom *space_name = atom_symbol_id(&scratch, entry->key);
        if (!space_name || resolve_space(registry, space_name) != target_space)
            continue;
        Atom **types = NULL;
        uint32_t type_count = space_get_declared_types(
            program_space, &scratch, space_name, &types);
        for (uint32_t type_index = 0u;
             type_index < type_count; type_index++) {
            Atom *type = types[type_index];
            if (!petta_type_head_is(type, "SpaceOf") ||
                type->expr.len != 2u)
                continue;
            PettaTypecheckResult row_result;
            if (!petta_typecheck_value(
                    program_space, &scratch, row,
                    type->expr.elems[1], NULL, &row_result)) {
                free(types);
                result->fault = row_result.fault;
                snprintf(
                    result->diagnostic, sizeof(result->diagnostic),
                    "could not validate row for SpaceOf schema");
                arena_free(&scratch);
                return false;
            }
            if (row_result.verdict == PETTA_TYPECHECK_REFUTED) {
                const char *name = petta_block_symbol_name(space_name);
                free(types);
                result->verdict = PETTA_TYPECHECK_REFUTED;
                snprintf(
                    result->diagnostic, sizeof(result->diagnostic),
                    "row conflicts with SpaceOf schema for %s",
                    name ? name : "<space>");
                arena_free(&scratch);
                return true;
            }
        }
        free(types);
    }
    arena_free(&scratch);
    return true;
}

static bool petta_typecheck_program_mutation_internal(
    const CettaNikDirectAuthorityStampV1 *authority,
    PettaProgram *program, Space *program_space, Registry *registry,
    Space *target_space, Atom *proposed,
    PettaTypecheckMutation mutation,
    PettaTypecheckPolicy policy, PettaTypecheckBlockResult *result) {
    if (!result)
        return false;
    *result = (PettaTypecheckBlockResult){
        .verdict = PETTA_TYPECHECK_ESTABLISHED,
        .fault = PETTA_TYPECHECK_FAULT_NONE,
    };
    if (!program || !program_space || !target_space || !proposed) {
        result->fault = PETTA_TYPECHECK_FAULT_INVALID_ARGUMENT;
        snprintf(
            result->diagnostic, sizeof(result->diagnostic),
            "invalid PeTTa mutation judgment");
        return false;
    }
    if (!petta_program_enable_analysis(program)) {
        result->fault = PETTA_TYPECHECK_FAULT_ALLOCATION;
        return false;
    }
    if (mutation == PETTA_TYPECHECK_MUTATION_REMOVE) {
        /* Removing a data row cannot invalidate a program typing judgment.
         * Program declarations and equations live in the root program Space;
         * judge their complete hypothetical post-removal state below. */
        if (target_space != program_space)
            return true;
        Space *remaining = space_heap_clone_shallow(program_space);
        if (!remaining) {
            result->fault = PETTA_TYPECHECK_FAULT_ALLOCATION;
            snprintf(
                result->diagnostic, sizeof(result->diagnostic),
                "could not clone the live PeTTa program for removal");
            return false;
        }
        AtomId removed_id = remaining->native.universe
            ? term_universe_lookup_atom_id(
                  remaining->native.universe, proposed)
            : CETTA_ATOM_ID_NONE;
        bool removed = false;
        if (removed_id != CETTA_ATOM_ID_NONE) {
            while (space_remove_atom_id(remaining, removed_id))
                removed = true;
        } else {
            while (space_remove(remaining, proposed))
                removed = true;
        }
        if (!removed) {
            space_free(remaining);
            free(remaining);
            return true;
        }

        CettaCount atom_count = space_length64(remaining);
        if (atom_count > SIZE_MAX / sizeof(Atom *)) {
            space_free(remaining);
            free(remaining);
            result->fault = PETTA_TYPECHECK_FAULT_ALLOCATION;
            snprintf(
                result->diagnostic, sizeof(result->diagnostic),
                "hypothetical PeTTa program is too large");
            return false;
        }
        Atom **forms = atom_count
            ? cetta_malloc(sizeof(*forms) * (size_t)atom_count)
            : NULL;
        size_t form_count = 0u;
        for (CettaIndex index = 0u; index < atom_count; index++) {
            Atom *form = space_get_at64(remaining, index);
            if (petta_program_is_equation(form) ||
                (petta_block_head_is(form, ":") &&
                 form->expr.len == 3u)) {
                forms[form_count++] = form;
            }
        }
        bool judged = petta_typecheck_declaration_block_internal(
            authority, NULL, remaining, registry,
            forms, form_count, policy, false, result);
        free(forms);
        space_free(remaining);
        free(remaining);
        return judged;
    }
    if (!petta_program_is_equation(proposed) &&
        !(petta_block_head_is(proposed, ":") &&
          proposed->expr.len == 3u)) {
        return petta_typecheck_validate_target_row(
            program_space, registry, target_space,
            proposed, result);
    }
    Atom *forms[1] = {proposed};
    bool judged = petta_typecheck_declaration_block_internal(
        authority, program, program_space, registry,
        forms, 1u, policy, true, result);
    return judged;
}

bool petta_typecheck_program_mutation(
    PettaProgram *program, Space *program_space, Registry *registry,
    Space *target_space, Atom *proposed,
    PettaTypecheckMutation mutation,
    PettaTypecheckPolicy policy, PettaTypecheckBlockResult *result) {
    return petta_typecheck_program_mutation_internal(
        NULL, program, program_space, registry, target_space,
        proposed, mutation, policy, result);
}

bool petta_typecheck_program_mutation_under_authority(
    const CettaNikDirectAuthorityV1 *authority,
    PettaProgram *program, Space *program_space, Registry *registry,
    Space *target_space, Atom *proposed,
    PettaTypecheckMutation mutation,
    PettaTypecheckPolicy policy, PettaTypecheckBlockResult *result) {
    if (!result)
        return false;
    CettaNikDirectAuthorityStampV1 stamp;
    if (!cetta_nik_direct_authority_v1_stamp(
            authority, (uint32_t)policy, &stamp)) {
        *result = (PettaTypecheckBlockResult){
            .verdict = PETTA_TYPECHECK_ESTABLISHED,
            .fault = PETTA_TYPECHECK_FAULT_INVALID_ARGUMENT,
        };
        snprintf(
            result->diagnostic, sizeof(result->diagnostic),
            "invalid direct NIK authority for PeTTa mutation checking");
        return false;
    }
    return petta_typecheck_program_mutation_internal(
        &stamp, program, program_space, registry, target_space,
        proposed, mutation, policy, result);
}

bool petta_typecheck_program_mutation_selected(
    PettaProgram *program, Space *program_space, Registry *registry,
    Space *target_space, Atom *proposed,
    PettaTypecheckMutation mutation,
    PettaTypecheckPolicy policy, PettaTypecheckBlockResult *result) {
    return petta_typecheck_program_mutation_under_authority(
        &petta_typecheck_v2_direct_authority_v1,
        program, program_space, registry, target_space,
        proposed, mutation, policy, result);
}

Atom *petta_typecheck_error_atom(
    Arena *arena, Atom *source, int exit_code, const char *diagnostic) {
    if (!arena || !source || (exit_code != 1 && exit_code != 2))
        return NULL;
    Atom *reason_parts[3] = {
        atom_symbol(arena, "PettaTypecheckFailure"),
        atom_int(arena, exit_code),
        atom_symbol(
            arena, diagnostic && diagnostic[0]
                ? diagnostic : "typecheck-v2 judgment failed"),
    };
    Atom *reason = atom_expr(arena, reason_parts, 3u);
    return reason ? atom_error(arena, source, reason) : NULL;
}

bool petta_typecheck_error_view(
    Atom *atom, int *exit_code, const char **diagnostic) {
    if (!atom_is_error(atom) || atom->expr.len < 3u)
        return false;
    Atom *reason = atom->expr.elems[2];
    if (!reason || reason->kind != ATOM_EXPR || reason->expr.len != 3u ||
        !atom_is_symbol(reason->expr.elems[0], "PettaTypecheckFailure") ||
        reason->expr.elems[1]->kind != ATOM_GROUNDED ||
        reason->expr.elems[1]->ground.gkind != GV_INT ||
        (reason->expr.elems[1]->ground.ival != 1 &&
         reason->expr.elems[1]->ground.ival != 2) ||
        reason->expr.elems[2]->kind != ATOM_SYMBOL) {
        return false;
    }
    if (exit_code)
        *exit_code = (int)reason->expr.elems[1]->ground.ival;
    if (diagnostic)
        *diagnostic = atom_name_cstr(reason->expr.elems[2]);
    return true;
}

const char *petta_typecheck_verdict_name(PettaTypecheckVerdict verdict) {
    switch (verdict) {
    case PETTA_TYPECHECK_ESTABLISHED: return "established";
    case PETTA_TYPECHECK_REFUTED: return "refuted";
    case PETTA_TYPECHECK_UNDETERMINED: return "undetermined";
    case PETTA_TYPECHECK_INCOMPLETE: return "incomplete";
    }
    return "invalid";
}

const char *petta_typecheck_reason_name(PettaTypecheckReason reason) {
    switch (reason) {
    case PETTA_TYPECHECK_REASON_NONE: return "none";
    case PETTA_TYPECHECK_REASON_EXACT: return "exact";
    case PETTA_TYPECHECK_REASON_WILDCARD: return "wildcard";
    case PETTA_TYPECHECK_REASON_STRUCTURAL: return "structural";
    case PETTA_TYPECHECK_REASON_DECLARED: return "declared";
    case PETTA_TYPECHECK_REASON_OPEN_VALUE: return "open-value";
    case PETTA_TYPECHECK_REASON_CYCLE: return "cycle";
    case PETTA_TYPECHECK_REASON_MISMATCH: return "mismatch";
    case PETTA_TYPECHECK_REASON_NONCALLABLE: return "noncallable";
    }
    return "invalid";
}

#include "petta_type_policy.h"

#include "symbol.h"
#include "petta_semantics.h"
#include "term_canon.h"


PettaTypeDemand petta_type_demand(const Atom *type) {
    if (type && type->kind == ATOM_SYMBOL && type->sym_id == g_builtin_syms.atom)
        return PETTA_TYPE_RAW;
    if ((type && type->kind == ATOM_SYMBOL &&
         type->sym_id == g_builtin_syms.undefined_type) ||
        (type && type->kind == ATOM_SYMBOL &&
         symbol_eq_cstr(g_symbols, type->sym_id, "_")))
        return PETTA_TYPE_TRANSLATE;
    return PETTA_TYPE_TRANSLATE_AND_GUARD;
}

bool petta_type_call_open(Atom *type, CettaExprLen arity,
                          PettaTypeCall *call) {
    return type && type->kind == ATOM_EXPR && type->expr.len >= 2u &&
           type->expr.len - 2u == arity && petta_type_call_plan(type, arity, call);
}

bool petta_type_call_plan(Atom *type, CettaExprLen supplied,
                          PettaTypeCall *call) {
    if (!type || type->kind != ATOM_EXPR || type->expr.len < 2u ||
        type->expr.len - 2u < supplied ||
        !atom_is_symbol_id(type->expr.elems[0], g_builtin_syms.arrow))
        return false;
    if (call) {
        CettaExprLen arity = type->expr.len - 2u;
        Atom *result = type->expr.elems[arity + 1u];
        PettaTypeDemand mode = petta_type_demand(result);
        *call = (PettaTypeCall){
            .signature = type,
            .arity = arity,
            .result_type = result,
            .guard_result = mode == PETTA_TYPE_TRANSLATE_AND_GUARD,
            .hold_result = mode == PETTA_TYPE_RAW,
        };
    }
    return true;
}

bool petta_type_body_is_data(Atom *const *types, uint32_t count) {
    for (uint32_t i = 0u; i < count; i++) {
        PettaTypeCall call;
        if (petta_type_call_plan(types[i], 0u, &call) && call.hold_result)
            return true;
    }
    return false;
}

bool petta_type_split_domain(Atom *domain, Atom **binder, Atom **formal) {
    if (binder)
        *binder = NULL;
    if (formal)
        *formal = domain;
    if (!domain || domain->kind != ATOM_EXPR || domain->expr.len != 3u ||
        !atom_is_symbol_id(domain->expr.elems[0], g_builtin_syms.colon) ||
        domain->expr.elems[1]->kind != ATOM_VAR)
        return false;
    if (binder)
        *binder = domain->expr.elems[1];
    if (formal)
        *formal = domain->expr.elems[2];
    return true;
}

PettaTypeArgument petta_type_argument(Atom *domain, bool dependent_domains) {
    PettaTypeArgument argument = {.formal = domain};
    if (dependent_domains)
        (void)petta_type_split_domain(domain, &argument.binder, &argument.formal);
    argument.demand = petta_type_demand(argument.formal);
    return argument;
}

bool petta_type_call_compile(Atom *signature, PettaTypeCall *call) {
    if (!call || !petta_type_call_plan(signature, 0u, call))
        return false;
    for (CettaExprIndex i = 0u; i < call->arity && i < 64u; i++) {
        PettaTypeDemand demand = petta_type_demand(signature->expr.elems[i + 1u]);
        if (demand == PETTA_TYPE_RAW)
            call->raw_arguments |= UINT64_C(1) << i;
        else if (demand == PETTA_TYPE_TRANSLATE)
            call->translated_arguments |= UINT64_C(1) << i;
    }
    call->compiled_demands = true;
    return true;
}

bool petta_type_call_instantiate(Arena *arena, const PettaTypeCall *source,
                                 PettaTypeCall *call) {
    if (!arena || !source || !call)
        return false;
    Atom *signature = cetta_instantiate_frame_syntax(arena, source->signature);
    if (!signature)
        return false;
    *call = *source;
    call->signature = signature;
    call->result_type = signature->expr.elems[source->arity + 1u];
    return true;
}

PettaTypeArgument petta_type_call_argument(const PettaTypeCall *call,
                                           CettaExprIndex argument,
                                           bool dependent_domains) {
    PettaTypeArgument result = petta_type_argument(
        call->signature->expr.elems[argument], dependent_domains);
    /* Compilation stores literal arrow facts, independent of profile.
     * A dependent binder is interpreted only by a host that enables it. */
    if (!result.binder && call->compiled_demands && argument <= 64u) {
        uint64_t bit = UINT64_C(1) << (argument - 1u);
        result.demand = call->raw_arguments & bit ? PETTA_TYPE_RAW
            : call->translated_arguments & bit ? PETTA_TYPE_TRANSLATE
            : PETTA_TYPE_TRANSLATE_AND_GUARD;
    } else {
        result.demand = petta_type_demand(result.formal);
    }
    return result;
}

uint32_t petta_type_unique_signatures(Atom **types, uint32_t count) {
    uint32_t retained = 0u;
    for (uint32_t index = 0u; index < count; index++) {
        bool duplicate = false;
        for (uint32_t prior = 0u; prior < retained; prior++) {
            if (atom_alpha_eq(types[prior], types[index])) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate)
            types[retained++] = types[index];
    }
    return retained;
}

Atom *petta_type_query(Arena *arena, Atom *value, PettaTypeQuery query) {
    if (!arena || !value)
        return NULL;
    return atom_expr2(
        arena, atom_symbol_id(arena, query == PETTA_TYPE_QUERY_EXACT
            ? g_builtin_syms.get_type : g_builtin_syms.get_metatype), value);
}

bool petta_type_literal_proves(const Atom *value, const Atom *required) {
    if (!value || !required || required->kind != ATOM_SYMBOL)
        return false;
    if (value->kind == ATOM_GROUNDED) {
        switch (value->ground.gkind) {
        case GV_INT:
        case GV_BIGINT:
        case GV_RATIONAL:
        case GV_FLOAT:
            return symbol_eq_cstr(g_symbols, required->sym_id, "Number");
        case GV_STRING:
            return symbol_eq_cstr(g_symbols, required->sym_id, "String");
        default:
            break;
        }
    }
    bool truth = false;
    return petta_semantics_truth_value(value, &truth) &&
           symbol_eq_cstr(g_symbols, required->sym_id, "Bool");
}

Atom *petta_type_declaration_query(Arena *arena, Atom *subject, Atom *required) {
    return arena && subject && required ? atom_expr3(
        arena, atom_symbol_id(arena, g_builtin_syms.colon), subject, required) : NULL;
}

Atom *petta_type_tuple_query(Arena *arena, CettaExprLen length) {
    if (!cetta_expr_len_mul_fits_size(length, sizeof(Atom *)))
        return NULL;
    Atom **fields = length
        ? arena_alloc(arena, sizeof(*fields) * (size_t)length) : NULL;
    if (length && !fields)
        return NULL;
    for (CettaExprIndex i = 0u; i < length; i++) {
        fields[i] = atom_var_with_id(arena, "__petta_type", fresh_var_id());
        if (!fields[i])
            return NULL;
    }
    return atom_expr(arena, fields, length);
}

bool petta_type_function_query(Arena *arena, Atom *sequence, Atom *required,
                                PettaTypeFunctionQuery *query) {
    Atom *const *elements = NULL;
    CettaExprLen length = 0u;
    if (!arena || !required || !query ||
        !atom_sequence_view(sequence, &elements, &length) || !length ||
        length >= (CettaExprLen)(SIZE_MAX / sizeof(Atom *)))
        return false;
    Atom *domains = petta_type_tuple_query(arena, length - 1u);
    Atom **arrow = arena_alloc(arena, sizeof(*arrow) * ((size_t)length + 1u));
    if (!domains || !arrow)
        return false;
    arrow[0] = atom_symbol_id(arena, g_builtin_syms.arrow);
    for (CettaExprIndex i = 1u; i < length; i++)
        arrow[i] = domains->expr.elems[i - 1u];
    arrow[length] = required;
    Atom *signature = atom_expr(arena, arrow, length + 1u);
    Atom *pattern = petta_type_declaration_query(arena, elements[0], signature);
    if (!pattern)
        return false;
    *query = (PettaTypeFunctionQuery){pattern, domains};
    return true;
}

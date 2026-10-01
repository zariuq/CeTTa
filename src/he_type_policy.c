#include "he_type_policy.h"

#include "symbol.h"
#include "search_machine.h"

#include <stdlib.h>
#include <string.h>

HeTypeDemand he_type_demand(const Atom *subject, Atom *expected,
                            Atom *metatype) {
    if (atom_is_symbol_id(expected, g_builtin_syms.atom) ||
        atom_eq(expected, metatype) ||
        (atom_is_symbol_id(expected, g_builtin_syms.expression) &&
         atom_is_list(subject)) ||
        atom_is_symbol_id(metatype, g_builtin_syms.variable))
        return HE_TYPE_KEEP;
    if (subject->kind == ATOM_SYMBOL || subject->kind == ATOM_GROUNDED ||
        (subject->kind == ATOM_EXPR && subject->expr.len == 0u) ||
        atom_is_list_form(subject))
        return HE_TYPE_CAST;
    return HE_TYPE_INTERPRET;
}

bool he_type_call_open(Atom *signature, HeTypeCall *call) {
    if (!signature || signature->kind != ATOM_EXPR ||
        signature->expr.len < 2u ||
        !atom_is_symbol_id(signature->expr.elems[0], g_builtin_syms.arrow))
        return false;
    if (call)
        *call = (HeTypeCall){
            .signature = signature,
            .arity = signature->expr.len - 2u,
            .result_type = signature->expr.elems[signature->expr.len - 1u],
        };
    return true;
}

HeTypeDomain he_type_domain(Atom *domain, bool dependent_telescope) {
    if (dependent_telescope && domain && domain->kind == ATOM_EXPR &&
        domain->expr.len == 3u &&
        atom_is_symbol_id(domain->expr.elems[0], g_builtin_syms.colon) &&
        domain->expr.elems[1]->kind == ATOM_VAR)
        return (HeTypeDomain){domain->expr.elems[1], domain->expr.elems[2]};
    return (HeTypeDomain){NULL, domain};
}

HeTypeArgumentCheck he_type_argument_check(Atom *expected) {
    if (atom_is_symbol_id(expected, g_builtin_syms.atom) ||
        atom_is_symbol_id(expected, g_builtin_syms.undefined_type))
        return HE_TYPE_ARGUMENT_ANY;
    if (atom_is_meta_type(expected))
        return HE_TYPE_ARGUMENT_METATYPE;
    return HE_TYPE_ARGUMENT_INFER;
}

Atom *he_type_result_demand(Arena *arena, Atom *codomain) {
    return atom_is_symbol_id(codomain, g_builtin_syms.expression)
        ? atom_undefined_type(arena) : codomain;
}

bool he_type_is_wildcard(const Atom *type) {
    return type && type->kind == ATOM_SYMBOL &&
           (type->sym_id == g_builtin_syms.atom ||
            type->sym_id == g_builtin_syms.undefined_type);
}

/* An identical rigid type symbol cannot learn a substitution. Keep the
 * common monomorphic case out of the general structural matcher. */
static bool he_type_same_symbol(const Atom *actual, const Atom *expected) {
    return actual && expected && actual->kind == ATOM_SYMBOL &&
           expected->kind == ATOM_SYMBOL && actual->sym_id == expected->sym_id;
}

bool he_type_refine(Atom *actual, Atom *expected, Bindings *bindings,
                     Arena *arena) {
    if (he_type_is_wildcard(actual) || he_type_is_wildcard(expected) ||
        he_type_same_symbol(actual, expected))
        return true;
    if (type_match_uses_space_class_bridge(actual, expected)) return true;
    return match_atoms(actual, expected, bindings, arena);
}

bool he_type_refine_builder(Atom *actual, Atom *expected,
                             BindingsBuilder *builder, Arena *arena) {
    if (he_type_is_wildcard(actual) || he_type_is_wildcard(expected) ||
        he_type_same_symbol(actual, expected))
        return true;
    if (type_match_uses_space_class_bridge(actual, expected)) return true;
    return match_atoms_builder(actual, expected, builder, arena);
}

void he_type_errors_init(HeTypeErrors *errors) {
    errors->items = errors->inline_items;
    errors->len = 0u;
    errors->cap = sizeof errors->inline_items / sizeof errors->inline_items[0];
}

void he_type_errors_free(HeTypeErrors *errors) {
    if (!errors) return;
    if (errors->items != errors->inline_items) free(errors->items);
    he_type_errors_init(errors);
}

static bool grow_atoms(Atom ***items, uint32_t len, uint32_t *capacity,
                       Atom **inline_items) {
    if (len < *capacity) return true;
    uint32_t next = *capacity * 2u;
    if (next <= *capacity || (size_t)next > SIZE_MAX / sizeof(Atom *))
        return false;
    Atom **grown = cetta_malloc(sizeof(*grown) * (size_t)next);
    memcpy(grown, *items, sizeof(*grown) * (size_t)len);
    if (*items != inline_items) free(*items);
    *items = grown;
    *capacity = next;
    return true;
}

bool he_type_errors_push(HeTypeErrors *errors, Atom *error) {
    if (!errors || errors->len == UINT32_MAX ||
        !grow_atoms(&errors->items, errors->len, &errors->cap, errors->inline_items))
        return false;
    errors->items[errors->len++] = error;
    return true;
}

void he_type_contracts_init(HeTypeContracts *contracts) {
    contracts->items = contracts->inline_items;
    contracts->len = 0u;
    contracts->cap = sizeof contracts->inline_items / sizeof contracts->inline_items[0];
}

void he_type_contracts_free(HeTypeContracts *contracts) {
    if (!contracts) return;
    if (contracts->items != contracts->inline_items) free(contracts->items);
    he_type_contracts_init(contracts);
}

bool he_type_contracts_push_unique(HeTypeContracts *contracts, Atom *type) {
    if (!contracts) return true;
    for (uint32_t i = 0u; i < contracts->len; i++)
        if (atom_eq(contracts->items[i], type)) return true;
    if (contracts->len == UINT32_MAX ||
        !grow_atoms(&contracts->items, contracts->len, &contracts->cap, contracts->inline_items))
        return false;
    contracts->items[contracts->len++] = type;
    return true;
}

typedef struct {
    Bindings *items;
    uint32_t len, cap;
    Bindings inline_items[4];
} ApplyBindings;

static void apply_bindings_init(ApplyBindings *bindings) {
    bindings->items = bindings->inline_items;
    bindings->len = 0u;
    bindings->cap = sizeof bindings->inline_items / sizeof bindings->inline_items[0];
}

static void apply_bindings_free(ApplyBindings *bindings) {
    for (uint32_t i = 0u; i < bindings->len; i++) bindings_free(&bindings->items[i]);
    if (bindings->items != bindings->inline_items) free(bindings->items);
    apply_bindings_init(bindings);
}

static Bindings *apply_bindings_push(ApplyBindings *bindings) {
    if (bindings->len == UINT32_MAX) return NULL;
    if (bindings->len == bindings->cap) {
        uint32_t capacity = bindings->cap * 2u;
        if (capacity <= bindings->cap) capacity = UINT32_MAX;
        if ((size_t)capacity > SIZE_MAX / sizeof(Bindings)) return NULL;
        Bindings *items = cetta_malloc(sizeof(*items) * (size_t)capacity);
        memcpy(items, bindings->items, sizeof(*items) * (size_t)bindings->len);
        if (bindings->items != bindings->inline_items) free(bindings->items);
        bindings->items = items;
        bindings->cap = capacity;
    }
    Bindings *slot = &bindings->items[bindings->len++];
    bindings_init(slot);
    return slot;
}

static bool apply_bindings_move(ApplyBindings *bindings, Bindings *source) {
    Bindings *slot = apply_bindings_push(bindings);
    if (!slot) return false;
    bindings_move(slot, source);
    return true;
}

static bool apply_bindings_clone(ApplyBindings *bindings, const Bindings *source) {
    Bindings *slot = apply_bindings_push(bindings);
    if (!slot) return false;
    if (bindings_clone(slot, source)) return true;
    bindings_free(slot);
    bindings->len--;
    return false;
}

static void apply_bindings_replace(ApplyBindings *destination, ApplyBindings *source) {
    apply_bindings_free(destination);
    if (source->items != source->inline_items) {
        destination->items = source->items;
        destination->len = source->len;
        destination->cap = source->cap;
    } else {
        memcpy(destination->inline_items, source->inline_items,
               sizeof(Bindings) * (size_t)source->len);
        destination->len = source->len;
    }
    apply_bindings_init(source);
}

static HeTypeDomain apply_domain(const HeTypeApplicationServices *services,
                                 Atom *domain, bool dependent) {
    return services->domain ? services->domain(services->context, domain)
        : he_type_domain(domain, dependent);
}

/* A name is instantiated in the later domains and in the result; only a
 * variable binder enters the binding store. */
static bool apply_bind_domain(const HeTypeApplicationServices *services,
                              BindingsBuilder *builder, Atom *domain,
                              Atom *argument, bool dependent) {
    HeTypeDomain view = apply_domain(services, domain, dependent);
    return !view.binder || view.binder->kind != ATOM_VAR ||
           bindings_builder_add_var_fresh(builder, view.binder, argument);
}

#define HE_TYPE_TELESCOPE_NAMES 8u

static bool apply_refine(const HeTypeApplicationServices *services,
                          Atom *actual, Atom *expected, BindingsBuilder *builder,
                          Arena *arena) {
    return services->refine ? services->refine(actual, expected, builder, arena)
        : he_type_refine_builder(actual, expected, builder, arena);
}

static Atom *argument_error(Arena *arena, Atom *expression, CettaExprIndex index,
                             Atom *expected, Atom *actual) {
    Atom *reason = atom_expr(arena, (Atom *[]){atom_symbol(arena, "BadArgType"),
                               atom_int(arena, (int64_t)index), expected, actual}, 4u);
    return atom_error(arena, expression, reason);
}

HeTypeApplicability he_type_call_applicable(
    Arena *arena, Atom *expression, Atom *signature, Atom *expected_result,
    bool dependent, const HeTypeApplicationServices *services,
    HeTypeErrors *errors, HeTypeContracts *contracts) {
    HeTypeCall call;
    if (!arena || !expression || !expected_result || !services || !services->infer ||
        !errors || !he_type_call_open(signature, &call))
        return HE_TYPE_APPLICATION_INCOMPLETE;
    CettaExprLen supplied = expression->kind == ATOM_EXPR && expression->expr.len
        ? expression->expr.len - 1u : 0u;
    if (supplied != call.arity) {
        return he_type_errors_push(errors, atom_error(arena, expression,
                   atom_symbol(arena, "IncorrectNumberOfArguments")))
            ? HE_TYPE_INAPPLICABLE : HE_TYPE_APPLICATION_INCOMPLETE;
    }

    ApplyBindings paths;
    apply_bindings_init(&paths);
    if (!apply_bindings_push(&paths)) return HE_TYPE_APPLICATION_INCOMPLETE;
    Atom *names[HE_TYPE_TELESCOPE_NAMES];
    Atom *named_arguments[HE_TYPE_TELESCOPE_NAMES];
    size_t named = 0u;
    for (CettaExprIndex i = 0u; i < call.arity && paths.len; i++) {
        Atom *domain = signature->expr.elems[i + 1u];
        if (named)
            domain = services->instantiate(services->context, arena, domain,
                                           names, named_arguments, named);
        Atom *argument = expression->expr.elems[i + 1u];
        if (services->source_argument)
            argument = services->source_argument(services->context, argument);
        ApplyBindings next;
        apply_bindings_init(&next);
        for (uint32_t p = 0u; p < paths.len; p++) {
            HeTypeDomain view = apply_domain(services, domain, dependent);
            Atom *expected = bindings_apply_if_vars(&paths.items[p], arena, view.formal);
            HeTypeArgumentCheck check = he_type_argument_check(expected);
            if (check == HE_TYPE_ARGUMENT_ANY ||
                (services->takes_source &&
                 services->takes_source(services->context, expected))) {
                if (!apply_bindings_move(&next, &paths.items[p])) goto incomplete;
                continue;
            }
            if (check == HE_TYPE_ARGUMENT_METATYPE) {
                if (atom_meta_type_accepts(arena, expected, argument)) {
                    BindingsBuilder candidate;
                    if (!bindings_builder_init(&candidate, &paths.items[p])) goto incomplete;
                    bool matched = apply_bind_domain(services, &candidate, domain, argument, dependent);
                    bool saved = !matched || apply_bindings_clone(&next, bindings_builder_bindings(&candidate));
                    bindings_builder_free(&candidate);
                    if (!saved) goto incomplete;
                } else {
                    Atom **actual = NULL;
                    uint32_t count = services->infer(services->context, argument, &actual);
                    Atom *first = count ? actual[0] : get_meta_type(arena, argument);
                    bool saved = he_type_errors_push(errors, argument_error(arena, expression, i + 1u, expected, first));
                    free(actual);
                    if (!saved) goto incomplete;
                }
                continue;
            }
            Atom **actual = NULL;
            uint32_t count = services->infer(services->context, argument, &actual);
            if (!count) {
                bool saved = apply_bindings_move(&next, &paths.items[p]);
                free(actual);
                if (!saved) goto incomplete;
                continue;
            }
            bool found = false;
            SearchContext trial;
            search_context_init_owned(&trial, &paths.items[p], NULL);
            for (uint32_t t = 0u; t < count; t++) {
                if (services->refuted && services->refuted(services->context, actual[t], expected)) continue;
                ChoicePoint point = search_context_save(&trial);
                bool matched = apply_refine(services, actual[t], expected, search_context_builder(&trial), arena);
                if (!matched && services->equivalent) {
                    /* Undo the failed attempt's partial substitution. */
                    search_context_rollback(&trial, point);
                    point = search_context_save(&trial);
                    matched = services->equivalent(services->context, actual[t], expected);
                }
                matched = matched &&
                    apply_bind_domain(services, search_context_builder(&trial), domain, argument, dependent);
                bool saved = !matched || apply_bindings_clone(&next, search_context_bindings(&trial));
                search_context_rollback(&trial, point);
                if (!saved) {
                    search_context_free(&trial);
                    free(actual);
                    goto incomplete;
                }
                found |= matched;
            }
            search_context_free(&trial);
            bool saved = found || he_type_errors_push(errors, argument_error(arena, expression, i + 1u, expected, actual[0]));
            free(actual);
            if (!saved) goto incomplete;
        }
        apply_bindings_replace(&paths, &next);
        if (paths.len && services->instantiate && named < HE_TYPE_TELESCOPE_NAMES) {
            HeTypeDomain view = apply_domain(services, signature->expr.elems[i + 1u], dependent);
            if (view.binder && view.binder->kind != ATOM_VAR) {
                names[named] = view.binder;
                named_arguments[named++] = argument;
            }
        }
        continue;
incomplete:
        apply_bindings_free(&next);
        apply_bindings_free(&paths);
        return HE_TYPE_APPLICATION_INCOMPLETE;
    }
    if (named)
        call.result_type = services->instantiate(services->context, arena, call.result_type,
                                                 names, named_arguments, named);
    bool accepted = false;
    for (uint32_t p = 0u; p < paths.len; p++) {
        SearchContext trial;
        search_context_init_owned(&trial, &paths.items[p], NULL);
        Atom *result = dependent ? bindings_apply_if_vars(search_context_bindings(&trial), arena, call.result_type)
                                : call.result_type;
        bool saved;
        bool matched;
        if (services->equivalent) {
            ChoicePoint point = search_context_save(&trial);
            matched = apply_refine(services, result, expected_result, search_context_builder(&trial), arena);
            if (!matched) {
                search_context_rollback(&trial, point);
                matched = services->equivalent(services->context, result, expected_result);
            }
        } else {
            matched = apply_refine(services, result, expected_result, search_context_builder(&trial), arena);
        }
        if (matched) {
            accepted = true;
            Atom *contract = bindings_apply_if_vars(search_context_bindings(&trial), arena, result);
            saved = he_type_contracts_push_unique(contracts, he_type_result_demand(arena, contract));
        } else {
            saved = he_type_errors_push(errors, atom_error(arena, expression,
                        atom_expr3(arena, atom_symbol(arena, "BadType"), expected_result, result)));
        }
        search_context_free(&trial);
        if (!saved) {
            apply_bindings_free(&paths);
            return HE_TYPE_APPLICATION_INCOMPLETE;
        }
    }
    apply_bindings_free(&paths);
    return accepted ? HE_TYPE_APPLICABLE : HE_TYPE_INAPPLICABLE;
}

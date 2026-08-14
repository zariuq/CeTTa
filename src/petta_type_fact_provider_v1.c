#include "petta_type_fact_provider_v1.h"

#include "match.h"
#include "petta_analysis.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { PETTA_TYPE_FACT_DEPTH_LIMIT_V1 = 256 };

struct CettaPettaTypeFactProviderV1 {
    PettaProgram *program;
    Space *space;
    SpaceReadToken read;
    Atom *const *overlay_forms;
    size_t overlay_form_count;
    Atom **equations;
    size_t equation_count;
    CettaGsltProviderV1 physical[5];
    CettaGsltProviderRegistryV1 registry;
};

static void type_fact_error_v1(
    char *error, size_t error_size, const char *format, ...) {
    if (!error || error_size == 0u)
        return;
    va_list arguments;
    va_start(arguments, format);
    (void)vsnprintf(error, error_size, format, arguments);
    va_end(arguments);
}

static bool type_fact_symbol_is_v1(const Atom *atom, const char *name) {
    return atom && atom->kind == ATOM_SYMBOL && name &&
        strcmp(atom_name_cstr((Atom *)atom), name) == 0;
}

static bool type_fact_head_is_v1(const Atom *atom, const char *name) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len > 0u &&
        atom->expr.elems && type_fact_symbol_is_v1(atom->expr.elems[0], name);
}

static bool type_fact_overlay_annotation_v1(
    const Atom *form, const Atom *subject, const Atom **type) {
    if (type)
        *type = NULL;
    if (!type_fact_head_is_v1(form, ":") || form->expr.len != 3u ||
        !atom_eq(form->expr.elems[1], (Atom *)subject)) {
        return false;
    }
    if (type)
        *type = form->expr.elems[2];
    return true;
}

static bool type_fact_raw_type_mentions_v1(
    const Atom *type, const Atom *subject, uint32_t depth) {
    if (!type || !subject || subject->kind != ATOM_SYMBOL || depth > 256u)
        return false;
    if (type->kind == ATOM_SYMBOL)
        return atom_eq((Atom *)type, (Atom *)subject);
    if (type->kind != ATOM_EXPR || !type->expr.elems)
        return false;
    for (CettaExprIndex index = 0u; index < type->expr.len; index++) {
        if (type_fact_raw_type_mentions_v1(
                type->expr.elems[index], subject, depth + 1u)) {
            return true;
        }
    }
    return false;
}

/* A plain symbol used in a type position is an abstract nominal even when a
 * separate `(: Name Type)` marker is absent.  This is positive source
 * evidence: the adapter derives it only from live type annotations or the
 * mutually visible source overlay. */
static bool type_fact_subject_used_as_type_v1(
    CettaPettaTypeFactProviderV1 *provider, const Atom *subject) {
    if (!provider || !subject || subject->kind != ATOM_SYMBOL)
        return false;
    static const char *const reserved[] = {
        "Type", "Number", "String", "Bool", "Atom", "Symbol",
        "Expression", "%Undefined%", "List", "Alias", "Newtype",
        "Brand", "brand", "|", "->", "-[det]->", "-[semidet]->",
        "-[nondet]->", "-[deterministic]->", "-[semideterministic]->",
        "-[nondeterministic]->",
    };
    const char *name = atom_name_cstr((Atom *)subject);
    for (size_t index = 0u; index < sizeof(reserved) / sizeof(reserved[0]);
         index++) {
        if (strcmp(name, reserved[index]) == 0)
            return false;
    }

    Atom **annotations = NULL;
    size_t annotation_count = 0u;
    if (!petta_program_type_annotation_snapshot(
            provider->program, &annotations, &annotation_count)) {
        return false;
    }
    bool used = false;
    for (size_t index = 0u; index < annotation_count && !used; index++) {
        const Atom *annotation = annotations[index];
        used = type_fact_head_is_v1(annotation, ":") &&
            annotation->expr.len == 3u &&
            type_fact_raw_type_mentions_v1(
                annotation->expr.elems[2], subject, 0u);
    }
    free(annotations);
    for (size_t index = 0u;
         index < provider->overlay_form_count && !used; index++) {
        const Atom *annotation = provider->overlay_forms[index];
        used = type_fact_head_is_v1(annotation, ":") &&
            annotation->expr.len == 3u &&
            type_fact_raw_type_mentions_v1(
                annotation->expr.elems[2], subject, 0u);
    }
    return used;
}

static Atom *type_fact_builtin_signature_v1(
    Arena *arena, const Atom *subject);

static uint32_t type_fact_declared_types_v1(
    CettaPettaTypeFactProviderV1 *provider, const Atom *subject,
    Arena *arena, Atom ***types) {
    if (types)
        *types = NULL;
    if (!provider || !subject || !arena || !types ||
        subject->kind != ATOM_SYMBOL) {
        return 0u;
    }
    Atom **combined = NULL;
    uint32_t count = petta_program_declared_types(
        provider->program, (Atom *)subject, arena, &combined);
    for (size_t index = 0u; index < provider->overlay_form_count; index++) {
        const Atom *type = NULL;
        if (!type_fact_overlay_annotation_v1(
                provider->overlay_forms[index], subject, &type)) {
            continue;
        }
        if (count == UINT32_MAX) {
            free(combined);
            return 0u;
        }
        Atom **grown = realloc(combined, sizeof(*grown) * (count + 1u));
        Atom *copied = atom_deep_copy(arena, (Atom *)type);
        if (!grown || !copied) {
            free(grown ? grown : combined);
            return 0u;
        }
        combined = grown;
        combined[count++] = copied;
    }

    /* Alias and newtype declarations are source-ordered alternatives for one
     * nominal meaning.  The first such declaration is authoritative, exactly
     * as in the block checker's exclusive-declaration lookup.  Ordinary
     * overload signatures remain a bag and retain their original order. */
    bool saw_nominal_meaning = false;
    uint32_t write = 0u;
    for (uint32_t read = 0u; read < count; read++) {
        Atom *type = combined[read];
        bool nominal_meaning = type && type->kind == ATOM_EXPR &&
            type->expr.len == 2u && type->expr.elems &&
            type->expr.elems[0]->kind == ATOM_SYMBOL &&
            (type_fact_symbol_is_v1(type->expr.elems[0], "Alias") ||
             type_fact_symbol_is_v1(type->expr.elems[0], "Newtype"));
        if (nominal_meaning) {
            if (saw_nominal_meaning)
                continue;
            saw_nominal_meaning = true;
        }
        combined[write++] = type;
    }
    count = write;

    /* Builtin signatures are language-environment evidence, not evaluator
     * guesses.  They are a fallback only: a live source declaration owns the
     * name and therefore cannot become accidentally ambiguous with the base
     * PeTTa catalog. */
    if (count == 0u) {
        Atom *builtin = type_fact_builtin_signature_v1(arena, subject);
        if (builtin) {
            Atom **grown = realloc(combined, sizeof(*grown));
            if (!grown) {
                free(combined);
                return 0u;
            }
            combined = grown;
            combined[0] = builtin;
            count = 1u;
        }
    }
    *types = combined;
    return count;
}

static bool type_fact_build_equation_catalog_v1(
    CettaPettaTypeFactProviderV1 *provider) {
    if (!provider || !petta_program_equation_snapshot(
            provider->program, provider->space,
            &provider->equations, &provider->equation_count)) {
        return false;
    }
    size_t overlay_count = 0u;
    for (size_t index = 0u; index < provider->overlay_form_count; index++) {
        if (type_fact_head_is_v1(provider->overlay_forms[index], "=") &&
            provider->overlay_forms[index]->expr.len == 3u) {
            overlay_count++;
        }
    }
    if (overlay_count == 0u)
        return true;
    if (provider->equation_count > SIZE_MAX - overlay_count ||
        provider->equation_count + overlay_count >
            SIZE_MAX / sizeof(*provider->equations)) {
        free(provider->equations);
        provider->equations = NULL;
        provider->equation_count = 0u;
        return false;
    }
    Atom **grown = realloc(
        provider->equations,
        sizeof(*grown) * (provider->equation_count + overlay_count));
    if (!grown) {
        free(provider->equations);
        provider->equations = NULL;
        provider->equation_count = 0u;
        return false;
    }
    provider->equations = grown;
    for (size_t index = 0u; index < provider->overlay_form_count; index++) {
        Atom *form = provider->overlay_forms[index];
        if (type_fact_head_is_v1(form, "=") && form->expr.len == 3u)
            provider->equations[provider->equation_count++] = form;
    }
    return true;
}

/* The provider is admitted against one immutable Space revision, so all
 * effect and result queries share this declaration-ordered pointer catalog.
 * The provider owns the array; the program and source block own the atoms. */
static bool type_fact_equation_catalog_v1(
    CettaPettaTypeFactProviderV1 *provider,
    Atom *const **equations, size_t *equation_count) {
    if (equations)
        *equations = NULL;
    if (equation_count)
        *equation_count = 0u;
    if (!provider || !equations || !equation_count)
        return false;
    *equations = provider->equations;
    *equation_count = provider->equation_count;
    return true;
}

static Atom *type_fact_expr_v1(
    Arena *arena, const char *name, Atom **arguments, size_t argument_count) {
    if (!arena || !name || argument_count > UINT32_MAX - 1u)
        return NULL;
    Atom **elements = arena_alloc(
        arena, sizeof(*elements) * (argument_count + 1u));
    if (!elements)
        return NULL;
    elements[0] = atom_symbol(arena, name);
    if (!elements[0])
        return NULL;
    for (size_t index = 0u; index < argument_count; index++) {
        elements[index + 1u] = arguments[index];
        if (!elements[index + 1u])
            return NULL;
    }
    return atom_expr(
        arena, elements, (CettaExprLen)(argument_count + 1u));
}

static Atom *type_fact_expr1_v1(
    Arena *arena, const char *name, Atom *argument) {
    return type_fact_expr_v1(arena, name, &argument, 1u);
}

static Atom *type_fact_expr2_v1(
    Arena *arena, const char *name, Atom *left, Atom *right) {
    Atom *arguments[] = {left, right};
    return type_fact_expr_v1(arena, name, arguments, 2u);
}

static Atom *type_fact_expr3_v1(
    Arena *arena, const char *name, Atom *first, Atom *second, Atom *third) {
    Atom *arguments[] = {first, second, third};
    return type_fact_expr_v1(arena, name, arguments, 3u);
}

typedef enum {
    TYPE_FACT_BUILTIN_ANY_V1 = 0,
    TYPE_FACT_BUILTIN_NUMBER_V1,
    TYPE_FACT_BUILTIN_BOOL_V1,
} TypeFactBuiltinTypeV1;

typedef struct {
    const char *name;
    uint8_t arity;
    TypeFactBuiltinTypeV1 arguments[2];
    TypeFactBuiltinTypeV1 result;
} TypeFactBuiltinSignatureV1;

/* PeTTa's stable pure builtin type catalog.  The shapes follow current
 * upstream lib/lib_builtin_types.metta plus the operationally registered
 * `implies`, `and-then`, and `or-else` Boolean forms.  v3 records the
 * execution fact omitted by upstream's plain arrows: these grounded calls
 * are deterministic for well-typed arguments.  Shape-sensitive operations
 * such as min-atom/max-atom stay absent until their nonempty-spine judgment
 * can select a grade; an unconditional signature would be false evidence. */
static const TypeFactBuiltinSignatureV1 type_fact_builtins_v1[] = {
    {"+", 2u, {TYPE_FACT_BUILTIN_NUMBER_V1, TYPE_FACT_BUILTIN_NUMBER_V1},
     TYPE_FACT_BUILTIN_NUMBER_V1},
    {"-", 2u, {TYPE_FACT_BUILTIN_NUMBER_V1, TYPE_FACT_BUILTIN_NUMBER_V1},
     TYPE_FACT_BUILTIN_NUMBER_V1},
    {"*", 2u, {TYPE_FACT_BUILTIN_NUMBER_V1, TYPE_FACT_BUILTIN_NUMBER_V1},
     TYPE_FACT_BUILTIN_NUMBER_V1},
    {"/", 2u, {TYPE_FACT_BUILTIN_NUMBER_V1, TYPE_FACT_BUILTIN_NUMBER_V1},
     TYPE_FACT_BUILTIN_NUMBER_V1},
    {"%", 2u, {TYPE_FACT_BUILTIN_NUMBER_V1, TYPE_FACT_BUILTIN_NUMBER_V1},
     TYPE_FACT_BUILTIN_NUMBER_V1},
    {"mod", 2u, {TYPE_FACT_BUILTIN_NUMBER_V1, TYPE_FACT_BUILTIN_NUMBER_V1},
     TYPE_FACT_BUILTIN_NUMBER_V1},
    {"pow-math", 2u,
     {TYPE_FACT_BUILTIN_NUMBER_V1, TYPE_FACT_BUILTIN_NUMBER_V1},
     TYPE_FACT_BUILTIN_NUMBER_V1},
    {"log-math", 2u,
     {TYPE_FACT_BUILTIN_NUMBER_V1, TYPE_FACT_BUILTIN_NUMBER_V1},
     TYPE_FACT_BUILTIN_NUMBER_V1},
    {"min", 2u, {TYPE_FACT_BUILTIN_NUMBER_V1, TYPE_FACT_BUILTIN_NUMBER_V1},
     TYPE_FACT_BUILTIN_NUMBER_V1},
    {"max", 2u, {TYPE_FACT_BUILTIN_NUMBER_V1, TYPE_FACT_BUILTIN_NUMBER_V1},
     TYPE_FACT_BUILTIN_NUMBER_V1},
    {"sqrt-math", 1u, {TYPE_FACT_BUILTIN_NUMBER_V1},
     TYPE_FACT_BUILTIN_NUMBER_V1},
    {"abs-math", 1u, {TYPE_FACT_BUILTIN_NUMBER_V1},
     TYPE_FACT_BUILTIN_NUMBER_V1},
    {"trunc-math", 1u, {TYPE_FACT_BUILTIN_NUMBER_V1},
     TYPE_FACT_BUILTIN_NUMBER_V1},
    {"ceil-math", 1u, {TYPE_FACT_BUILTIN_NUMBER_V1},
     TYPE_FACT_BUILTIN_NUMBER_V1},
    {"floor-math", 1u, {TYPE_FACT_BUILTIN_NUMBER_V1},
     TYPE_FACT_BUILTIN_NUMBER_V1},
    {"round-math", 1u, {TYPE_FACT_BUILTIN_NUMBER_V1},
     TYPE_FACT_BUILTIN_NUMBER_V1},
    {"sin-math", 1u, {TYPE_FACT_BUILTIN_NUMBER_V1},
     TYPE_FACT_BUILTIN_NUMBER_V1},
    {"asin-math", 1u, {TYPE_FACT_BUILTIN_NUMBER_V1},
     TYPE_FACT_BUILTIN_NUMBER_V1},
    {"cos-math", 1u, {TYPE_FACT_BUILTIN_NUMBER_V1},
     TYPE_FACT_BUILTIN_NUMBER_V1},
    {"acos-math", 1u, {TYPE_FACT_BUILTIN_NUMBER_V1},
     TYPE_FACT_BUILTIN_NUMBER_V1},
    {"tan-math", 1u, {TYPE_FACT_BUILTIN_NUMBER_V1},
     TYPE_FACT_BUILTIN_NUMBER_V1},
    {"atan-math", 1u, {TYPE_FACT_BUILTIN_NUMBER_V1},
     TYPE_FACT_BUILTIN_NUMBER_V1},
    {"exp", 1u, {TYPE_FACT_BUILTIN_NUMBER_V1},
     TYPE_FACT_BUILTIN_NUMBER_V1},
    {"<", 2u, {TYPE_FACT_BUILTIN_NUMBER_V1, TYPE_FACT_BUILTIN_NUMBER_V1},
     TYPE_FACT_BUILTIN_BOOL_V1},
    {"<=", 2u, {TYPE_FACT_BUILTIN_NUMBER_V1, TYPE_FACT_BUILTIN_NUMBER_V1},
     TYPE_FACT_BUILTIN_BOOL_V1},
    {">", 2u, {TYPE_FACT_BUILTIN_NUMBER_V1, TYPE_FACT_BUILTIN_NUMBER_V1},
     TYPE_FACT_BUILTIN_BOOL_V1},
    {">=", 2u, {TYPE_FACT_BUILTIN_NUMBER_V1, TYPE_FACT_BUILTIN_NUMBER_V1},
     TYPE_FACT_BUILTIN_BOOL_V1},
    {"==", 2u, {TYPE_FACT_BUILTIN_ANY_V1, TYPE_FACT_BUILTIN_ANY_V1},
     TYPE_FACT_BUILTIN_BOOL_V1},
    {"!=", 2u, {TYPE_FACT_BUILTIN_ANY_V1, TYPE_FACT_BUILTIN_ANY_V1},
     TYPE_FACT_BUILTIN_BOOL_V1},
    {"isnan-math", 1u, {TYPE_FACT_BUILTIN_NUMBER_V1},
     TYPE_FACT_BUILTIN_BOOL_V1},
    {"isinf-math", 1u, {TYPE_FACT_BUILTIN_NUMBER_V1},
     TYPE_FACT_BUILTIN_BOOL_V1},
    {"and", 2u, {TYPE_FACT_BUILTIN_BOOL_V1, TYPE_FACT_BUILTIN_BOOL_V1},
     TYPE_FACT_BUILTIN_BOOL_V1},
    {"or", 2u, {TYPE_FACT_BUILTIN_BOOL_V1, TYPE_FACT_BUILTIN_BOOL_V1},
     TYPE_FACT_BUILTIN_BOOL_V1},
    {"xor", 2u, {TYPE_FACT_BUILTIN_BOOL_V1, TYPE_FACT_BUILTIN_BOOL_V1},
     TYPE_FACT_BUILTIN_BOOL_V1},
    {"implies", 2u,
     {TYPE_FACT_BUILTIN_BOOL_V1, TYPE_FACT_BUILTIN_BOOL_V1},
     TYPE_FACT_BUILTIN_BOOL_V1},
    {"and-then", 2u,
     {TYPE_FACT_BUILTIN_BOOL_V1, TYPE_FACT_BUILTIN_BOOL_V1},
     TYPE_FACT_BUILTIN_BOOL_V1},
    {"or-else", 2u,
     {TYPE_FACT_BUILTIN_BOOL_V1, TYPE_FACT_BUILTIN_BOOL_V1},
     TYPE_FACT_BUILTIN_BOOL_V1},
    {"not", 1u, {TYPE_FACT_BUILTIN_BOOL_V1},
     TYPE_FACT_BUILTIN_BOOL_V1},
};

static Atom *type_fact_builtin_type_v1(
    Arena *arena, TypeFactBuiltinTypeV1 type) {
    switch (type) {
    case TYPE_FACT_BUILTIN_ANY_V1:
        return atom_symbol(arena, "%Undefined%");
    case TYPE_FACT_BUILTIN_NUMBER_V1:
        return atom_symbol(arena, "Number");
    case TYPE_FACT_BUILTIN_BOOL_V1:
        return atom_symbol(arena, "Bool");
    }
    return NULL;
}

static const TypeFactBuiltinSignatureV1 *
type_fact_builtin_signature_lookup_v1(
    const char *name, CettaExprLen arity) {
    if (!name)
        return NULL;
    for (size_t index = 0u;
         index < sizeof(type_fact_builtins_v1) /
                     sizeof(type_fact_builtins_v1[0]); index++) {
        if (strcmp(name, type_fact_builtins_v1[index].name) == 0 &&
            arity == type_fact_builtins_v1[index].arity) {
            return &type_fact_builtins_v1[index];
        }
    }
    return NULL;
}

static Atom *type_fact_builtin_signature_v1(
    Arena *arena, const Atom *subject) {
    if (!arena || !subject || subject->kind != ATOM_SYMBOL)
        return NULL;
    const char *name = atom_name_cstr((Atom *)subject);
    const TypeFactBuiltinSignatureV1 *signature = NULL;
    for (size_t index = 0u;
         index < sizeof(type_fact_builtins_v1) /
                     sizeof(type_fact_builtins_v1[0]); index++) {
        if (strcmp(name, type_fact_builtins_v1[index].name) == 0) {
            signature = &type_fact_builtins_v1[index];
            break;
        }
    }
    if (!signature)
        return NULL;
    Atom **parts = arena_alloc(
        arena, sizeof(*parts) * (signature->arity + 2u));
    if (!parts)
        return NULL;
    parts[0] = atom_symbol(arena, "-[det]->");
    for (uint8_t index = 0u; index < signature->arity; index++)
        parts[index + 1u] = type_fact_builtin_type_v1(
            arena, signature->arguments[index]);
    parts[signature->arity + 1u] = type_fact_builtin_type_v1(
        arena, signature->result);
    for (uint8_t index = 0u; index < signature->arity + 2u; index++) {
        if (!parts[index])
            return NULL;
    }
    return atom_expr(arena, parts, signature->arity + 2u);
}

static Atom *type_fact_wire_type_v1(
    Arena *arena, const Atom *type, uint32_t depth);

static Atom *type_fact_wire_list_v1(
    Arena *arena, Atom *const *types, size_t count, uint32_t depth) {
    if (!arena || depth > PETTA_TYPE_FACT_DEPTH_LIMIT_V1)
        return NULL;
    Atom *tail = atom_symbol(arena, "TTNil");
    if (!tail)
        return NULL;
    for (size_t index = count; index > 0u; index--) {
        Atom *head = type_fact_wire_type_v1(
            arena, types[index - 1u], depth + 1u);
        tail = head ? type_fact_expr2_v1(arena, "TTCons", head, tail) : NULL;
        if (!tail)
            return NULL;
    }
    return tail;
}

static Atom *type_fact_wire_mode_v1(Arena *arena, const char *arrow) {
    if (!arena || !arrow)
        return NULL;
    if (strcmp(arrow, "->") == 0)
        return atom_symbol(arena, "MPlain");
    if (strcmp(arrow, "-[det]->") == 0 ||
        strcmp(arrow, "-[deterministic]->") == 0) {
        return atom_symbol(arena, "MDet");
    }
    if (strcmp(arrow, "-[semidet]->") == 0 ||
        strcmp(arrow, "-[semideterministic]->") == 0) {
        return atom_symbol(arena, "MSemidet");
    }
    if (strcmp(arrow, "-[nondet]->") == 0 ||
        strcmp(arrow, "-[nondeterministic]->") == 0) {
        return atom_symbol(arena, "MNondet");
    }
    size_t length = strlen(arrow);
    if (length > 6u && strncmp(arrow, "-[$", 3u) == 0 &&
        strcmp(arrow + length - 3u, "]->") == 0) {
        size_t effect_length = length - 6u;
        char *effect_name = arena_alloc(arena, effect_length + 1u);
        if (!effect_name)
            return NULL;
        memcpy(effect_name, arrow + 3u, effect_length);
        effect_name[effect_length] = '\0';
        Atom *effect = atom_symbol(arena, effect_name);
        return effect ? type_fact_expr1_v1(arena, "MEffect", effect) : NULL;
    }
    return NULL;
}

static Atom *type_fact_wire_type_v1(
    Arena *arena, const Atom *type, uint32_t depth) {
    if (!arena || !type || depth > PETTA_TYPE_FACT_DEPTH_LIMIT_V1)
        return NULL;
    if (type->kind == ATOM_VAR)
        return atom_symbol(arena, "TUndefined");
    if (type->kind == ATOM_SYMBOL) {
        if (type_fact_symbol_is_v1(type, "Number"))
            return atom_symbol(arena, "TNum");
        if (type_fact_symbol_is_v1(type, "String"))
            return atom_symbol(arena, "TStr");
        if (type_fact_symbol_is_v1(type, "Bool"))
            return atom_symbol(arena, "TBool");
        if (type_fact_symbol_is_v1(type, "Atom"))
            return atom_symbol(arena, "TAtom");
        if (type_fact_symbol_is_v1(type, "Expression"))
            return atom_symbol(arena, "TUndefined");
        if (type_fact_symbol_is_v1(type, "%Undefined%"))
            return atom_symbol(arena, "TUndefined");
        return type_fact_expr1_v1(
            arena, "TNominal", atom_deep_copy(arena, (Atom *)type));
    }
    if (type->kind != ATOM_EXPR || type->expr.len == 0u ||
        !type->expr.elems || type->expr.elems[0]->kind != ATOM_SYMBOL) {
        return NULL;
    }
    const char *head = atom_name_cstr(type->expr.elems[0]);
    if (strcmp(head, "List") == 0 && type->expr.len == 2u) {
        Atom *element = type_fact_wire_type_v1(
            arena, type->expr.elems[1], depth + 1u);
        return element ? type_fact_expr1_v1(arena, "TList", element) : NULL;
    }
    if (strcmp(head, "|") == 0 && type->expr.len > 1u) {
        Atom *members = type_fact_wire_list_v1(
            arena, &type->expr.elems[1], type->expr.len - 1u, depth + 1u);
        return members ? type_fact_expr1_v1(arena, "TUnion", members) : NULL;
    }
    Atom *mode = type_fact_wire_mode_v1(arena, head);
    if (mode && type->expr.len >= 2u) {
        size_t domain_count = type->expr.len - 2u;
        Atom *domains = type_fact_wire_list_v1(
            arena, &type->expr.elems[1], domain_count, depth + 1u);
        Atom *codomain = type_fact_wire_type_v1(
            arena, type->expr.elems[type->expr.len - 1u], depth + 1u);
        return domains && codomain
            ? type_fact_expr3_v1(arena, "TArrow", mode, domains, codomain)
            : NULL;
    }
    if ((strcmp(head, "Brand") == 0 || strcmp(head, "brand") == 0) &&
        type->expr.len == 3u && type->expr.elems[1]->kind == ATOM_SYMBOL) {
        Atom *repr = type_fact_wire_type_v1(
            arena, type->expr.elems[2], depth + 1u);
        return repr ? type_fact_expr2_v1(
            arena, "TBrand",
            atom_deep_copy(arena, type->expr.elems[1]), repr) : NULL;
    }
    Atom *fields = type_fact_wire_list_v1(
        arena, type->expr.elems, type->expr.len, depth + 1u);
    return fields ? type_fact_expr1_v1(arena, "TProduct", fields) : NULL;
}

Atom *cetta_petta_type_fact_wire_type_v1(
    Arena *arena, const Atom *type) {
    return type_fact_wire_type_v1(arena, type, 0u);
}

static bool type_fact_decl_view_v1(
    Atom *raw, const char **wrapper, Atom **representation) {
    if (wrapper)
        *wrapper = NULL;
    if (representation)
        *representation = NULL;
    if (!raw || raw->kind != ATOM_EXPR || raw->expr.len != 2u ||
        !raw->expr.elems || raw->expr.elems[0]->kind != ATOM_SYMBOL) {
        return false;
    }
    const char *head = atom_name_cstr(raw->expr.elems[0]);
    if (strcmp(head, "Alias") != 0 && strcmp(head, "Newtype") != 0)
        return false;
    if (wrapper)
        *wrapper = strcmp(head, "Alias") == 0 ? "TAlias" : "TNewtype";
    if (representation)
        *representation = raw->expr.elems[1];
    return true;
}

static bool type_fact_validate_declaration_catalog_v1(
    PettaProgram *program, Space *space,
    Atom *const *overlay_forms, size_t overlay_form_count,
    char *error, size_t error_size) {
    (void)space;
    (void)overlay_forms;
    (void)overlay_form_count;
    Atom **annotations = NULL;
    size_t count = 0u;
    if (!petta_program_type_annotation_snapshot(
            program, &annotations, &count)) {
        type_fact_error_v1(
            error, error_size,
            "could not snapshot PeTTa type declarations for admission");
        return false;
    }
    free(annotations);
    return true;
}

static CettaGsltProviderOutcomeV1 type_fact_push_v1(
    CettaGsltProviderAnswersV1 *answers, Atom *answer,
    uint64_t answer_limit, char *error, size_t error_size) {
    if (!answers || !answer) {
        type_fact_error_v1(error, error_size,
                           "PeTTa type-fact provider could not build an answer");
        return CETTA_GSLT_PROVIDER_FAULT;
    }
    if ((uint64_t)answers->answer_count >= answer_limit)
        return CETTA_GSLT_PROVIDER_ANSWER_LIMIT;
    if (!cetta_gslt_provider_answers_push_v1(answers, answer)) {
        type_fact_error_v1(error, error_size,
                           "PeTTa type-fact provider could not grow its answer bag");
        return CETTA_GSLT_PROVIDER_FAULT;
    }
    return CETTA_GSLT_PROVIDER_COMPLETED;
}

static Atom *type_fact_relation_answer1_v1(
    Arena *arena, const Atom *goal, Atom *argument) {
    return goal && goal->kind == ATOM_EXPR && goal->expr.len == 2u
        ? type_fact_expr1_v1(
              arena, atom_name_cstr(goal->expr.elems[0]), argument)
        : NULL;
}

static Atom *type_fact_relation_answer2_v1(
    Arena *arena, const Atom *goal, Atom *left, Atom *right) {
    return goal && goal->kind == ATOM_EXPR && goal->expr.len == 3u
        ? type_fact_expr2_v1(
              arena, atom_name_cstr(goal->expr.elems[0]), left, right)
        : NULL;
}

typedef enum {
    TYPE_FACT_MATCH_NO_V1 = 0,
    TYPE_FACT_MATCH_YES_V1,
    TYPE_FACT_MATCH_MAYBE_V1,
} TypeFactMatchV1;

static PettaAnalysisCardinality type_fact_effect_v1(
    CettaPettaTypeFactProviderV1 *provider,
    const Atom *expression,
    uint32_t depth);

static TypeFactMatchV1 type_fact_pattern_match_v1(
    const Atom *pattern, const Atom *value, uint32_t depth) {
    if (!pattern || !value || depth > PETTA_TYPE_FACT_DEPTH_LIMIT_V1)
        return TYPE_FACT_MATCH_MAYBE_V1;
    if (pattern->kind == ATOM_VAR)
        return TYPE_FACT_MATCH_YES_V1;
    if (value->kind == ATOM_VAR)
        return TYPE_FACT_MATCH_MAYBE_V1;
    if (type_fact_symbol_is_v1(value, "V3SourceVar"))
        return TYPE_FACT_MATCH_MAYBE_V1;
    if (pattern->kind != value->kind)
        return TYPE_FACT_MATCH_NO_V1;
    if (pattern->kind != ATOM_EXPR)
        return atom_eq((Atom *)pattern, (Atom *)value)
            ? TYPE_FACT_MATCH_YES_V1 : TYPE_FACT_MATCH_NO_V1;

    /* `(data h a ...)` is the inert source spelling of `(h a ...)` at the
     * selection boundary.  Compare its fields without manufacturing an
     * evaluated call. */
    bool data_value = type_fact_head_is_v1(value, "data") &&
        value->expr.len >= 2u;
    CettaExprLen value_offset = data_value ? 1u : 0u;
    CettaExprLen value_len = value->expr.len - value_offset;
    if (pattern->expr.len != value_len)
        return TYPE_FACT_MATCH_NO_V1;
    TypeFactMatchV1 result = TYPE_FACT_MATCH_YES_V1;
    for (CettaExprIndex index = 0u; index < pattern->expr.len; index++) {
        TypeFactMatchV1 field = type_fact_pattern_match_v1(
            pattern->expr.elems[index],
            value->expr.elems[index + value_offset], depth + 1u);
        if (field == TYPE_FACT_MATCH_NO_V1)
            return field;
        if (field == TYPE_FACT_MATCH_MAYBE_V1)
            result = field;
    }
    return result;
}

static TypeFactMatchV1 type_fact_clause_match_v1(
    const Atom *lhs, const Atom *call, uint32_t depth) {
    if (!lhs || !call || lhs->kind != ATOM_EXPR || call->kind != ATOM_EXPR ||
        lhs->expr.len != call->expr.len || lhs->expr.len == 0u ||
        !atom_eq(lhs->expr.elems[0], call->expr.elems[0])) {
        return TYPE_FACT_MATCH_NO_V1;
    }
    TypeFactMatchV1 result = TYPE_FACT_MATCH_YES_V1;
    for (CettaExprIndex index = 1u; index < lhs->expr.len; index++) {
        TypeFactMatchV1 argument = type_fact_pattern_match_v1(
            lhs->expr.elems[index], call->expr.elems[index], depth + 1u);
        if (argument == TYPE_FACT_MATCH_NO_V1)
            return argument;
        if (argument == TYPE_FACT_MATCH_MAYBE_V1)
            result = argument;
    }
    return result;
}

static PettaAnalysisCardinality type_fact_declared_effect_v1(
    CettaPettaTypeFactProviderV1 *provider, const Atom *subject,
    CettaExprLen arity) {
    if (!provider || !subject || subject->kind != ATOM_SYMBOL)
        return PETTA_ANALYSIS_CARDINALITY_UNDETERMINED;
    Arena declarations;
    arena_init(&declarations);
    Atom **types = NULL;
    uint32_t count = type_fact_declared_types_v1(
        provider, subject, &declarations, &types);
    PettaAnalysisCardinality result =
        PETTA_ANALYSIS_CARDINALITY_UNDETERMINED;
    bool found = false;
    for (uint32_t index = 0u; index < count; index++) {
        Atom *type = types[index];
        if (!type || type->kind != ATOM_EXPR ||
            type->expr.len != arity + 2u ||
            type->expr.elems[0]->kind != ATOM_SYMBOL) {
            continue;
        }
        const char *arrow = atom_name_cstr(type->expr.elems[0]);
        PettaAnalysisCardinality current =
            PETTA_ANALYSIS_CARDINALITY_UNDETERMINED;
        if (strcmp(arrow, "-[det]->") == 0 ||
            strcmp(arrow, "-[deterministic]->") == 0) {
            current = PETTA_ANALYSIS_CARDINALITY_DETERMINISTIC;
        } else if (strcmp(arrow, "-[semidet]->") == 0 ||
                   strcmp(arrow, "-[semideterministic]->") == 0) {
            current = PETTA_ANALYSIS_CARDINALITY_SEMIDETERMINISTIC;
        } else if (strcmp(arrow, "-[nondet]->") == 0 ||
                   strcmp(arrow, "-[nondeterministic]->") == 0) {
            current = PETTA_ANALYSIS_CARDINALITY_NONDETERMINISTIC;
        }
        if (current == PETTA_ANALYSIS_CARDINALITY_UNDETERMINED)
            continue;
        result = found
            ? petta_analysis_cardinality_join(result, current) : current;
        found = true;
    }
    free(types);
    arena_free(&declarations);
    return result;
}

static PettaAnalysisCardinality type_fact_relation_effect_v1(
    CettaPettaTypeFactProviderV1 *provider, const Atom *call,
    uint32_t depth) {
    if (!provider || !call || call->kind != ATOM_EXPR ||
        call->expr.len == 0u || call->expr.elems[0]->kind != ATOM_SYMBOL ||
        depth > PETTA_TYPE_FACT_DEPTH_LIMIT_V1) {
        return PETTA_ANALYSIS_CARDINALITY_UNDETERMINED;
    }
    Atom *const *equations = NULL;
    size_t equation_count = 0u;
    if (!type_fact_equation_catalog_v1(
            provider, &equations, &equation_count)) {
        return PETTA_ANALYSIS_CARDINALITY_UNDETERMINED;
    }
    size_t definite = 0u;
    size_t possible = 0u;
    PettaAnalysisCardinality body_effect =
        PETTA_ANALYSIS_CARDINALITY_UNDETERMINED;
    bool body_effect_found = false;
    for (size_t index = 0u; index < equation_count; index++) {
        Atom *equation = equations[index];
        if (!type_fact_head_is_v1(equation, "=") ||
            equation->expr.len != 3u) {
            continue;
        }
        Atom *lhs = equation->expr.elems[1];
        TypeFactMatchV1 match = type_fact_clause_match_v1(
            lhs, call, depth + 1u);
        if (match == TYPE_FACT_MATCH_YES_V1) {
            definite++;
            PettaAnalysisCardinality current = type_fact_effect_v1(
                provider, equation->expr.elems[2], depth + 1u);
            if (current != PETTA_ANALYSIS_CARDINALITY_UNDETERMINED) {
                body_effect = body_effect_found
                    ? petta_analysis_cardinality_join(body_effect, current)
                    : current;
                body_effect_found = true;
            }
        } else if (match == TYPE_FACT_MATCH_MAYBE_V1) {
            possible++;
        }
    }
    if (definite == 1u && possible == 0u)
        return body_effect;
    if (definite > 0u || possible > 1u)
        return PETTA_ANALYSIS_CARDINALITY_NONDETERMINISTIC;
    if (possible == 1u)
        return PETTA_ANALYSIS_CARDINALITY_UNDETERMINED;
    return PETTA_ANALYSIS_CARDINALITY_SEMIDETERMINISTIC;
}

static PettaAnalysisCardinality type_fact_effect_v1(
    CettaPettaTypeFactProviderV1 *provider,
    const Atom *expression, uint32_t depth) {
    if (!expression || depth > PETTA_TYPE_FACT_DEPTH_LIMIT_V1)
        return PETTA_ANALYSIS_CARDINALITY_UNDETERMINED;
    if (expression->kind == ATOM_GROUNDED ||
        expression->kind == ATOM_SYMBOL || expression->kind == ATOM_VAR)
        return PETTA_ANALYSIS_CARDINALITY_DETERMINISTIC;
    if (expression->kind != ATOM_EXPR || expression->expr.len == 0u ||
        !expression->expr.elems ||
        expression->expr.elems[0]->kind != ATOM_SYMBOL) {
        return PETTA_ANALYSIS_CARDINALITY_UNDETERMINED;
    }
    const char *head = atom_name_cstr(expression->expr.elems[0]);
    /* Stage transitions are owned by the v3 calculus.  Publishing them as
     * flat provider facts as well would give quote/eval two derivations. */
    if ((strcmp(head, "quote") == 0 || strcmp(head, "eval") == 0) &&
        expression->expr.len == 2u) {
        return PETTA_ANALYSIS_CARDINALITY_UNDETERMINED;
    }
    if ((strcmp(head, "collapse") == 0 && expression->expr.len == 2u) ||
        (strcmp(head, "foldall") == 0 && expression->expr.len == 4u)) {
        return PETTA_ANALYSIS_CARDINALITY_DETERMINISTIC;
    }
    if (strcmp(head, "once") == 0 && expression->expr.len == 2u) {
        PettaAnalysisCardinality inner = type_fact_effect_v1(
            provider, expression->expr.elems[1], depth + 1u);
        return inner == PETTA_ANALYSIS_CARDINALITY_NONDETERMINISTIC
            ? PETTA_ANALYSIS_CARDINALITY_SEMIDETERMINISTIC : inner;
    }
    static const char *const nondet[] = {
        "superpose", "hyperpose", "get-atoms", "match", "get-type",
        "member", "random-int",
    };
    for (size_t index = 0u; index < sizeof(nondet) / sizeof(nondet[0]);
         index++) {
        if (strcmp(head, nondet[index]) == 0)
            return PETTA_ANALYSIS_CARDINALITY_NONDETERMINISTIC;
    }
    static const char *const semidet[] = {
        "empty", "get-metatype", "decons", "decons-atom", "first",
        "first-from-pair", "second-from-pair", "bind!",
    };
    for (size_t index = 0u; index < sizeof(semidet) / sizeof(semidet[0]);
         index++) {
        if (strcmp(head, semidet[index]) == 0)
            return PETTA_ANALYSIS_CARDINALITY_SEMIDETERMINISTIC;
    }
    PettaAnalysisCardinality effect =
        type_fact_builtin_signature_lookup_v1(
            head, expression->expr.len - 1u)
        ? PETTA_ANALYSIS_CARDINALITY_DETERMINISTIC
        : PETTA_ANALYSIS_CARDINALITY_UNDETERMINED;
    static const char *const deterministic[] = {
        "+", "-", "*", "/", "%", "mod", "min", "max", "=", "==",
        "!=", "<", "<=", ">", ">=", "cons", "cons-atom", "data",
        "brand", "the", "assert", "is-var", "is-ground", "is-expr",
    };
    if (effect == PETTA_ANALYSIS_CARDINALITY_UNDETERMINED) {
        for (size_t index = 0u;
             index < sizeof(deterministic) / sizeof(deterministic[0]); index++) {
            if (strcmp(head, deterministic[index]) == 0) {
                effect = PETTA_ANALYSIS_CARDINALITY_DETERMINISTIC;
                break;
            }
        }
    }
    if (strcmp(head, "if") == 0 && expression->expr.len == 4u)
        effect = PETTA_ANALYSIS_CARDINALITY_DETERMINISTIC;
    else if (strcmp(head, "if") == 0 && expression->expr.len == 3u)
        effect = PETTA_ANALYSIS_CARDINALITY_SEMIDETERMINISTIC;
    if (effect == PETTA_ANALYSIS_CARDINALITY_UNDETERMINED) {
        /* Recursive and mutually recursive calls are checked against their
         * declared interface, just like ordinary typed recursion.  Rewalking
         * their clause bodies here would re-prove the whole call graph at
         * every occurrence and would not add evidence beyond the declaration
         * whose bodies the surrounding block is already validating. */
        PettaAnalysisCardinality declared = type_fact_declared_effect_v1(
            provider, expression->expr.elems[0],
            expression->expr.len - 1u);
        if (declared != PETTA_ANALYSIS_CARDINALITY_UNDETERMINED)
            return declared;
        return type_fact_relation_effect_v1(
            provider, expression, depth + 1u);
    }
    for (CettaExprIndex index = 1u; index < expression->expr.len; index++) {
        PettaAnalysisCardinality child = type_fact_effect_v1(
            provider, expression->expr.elems[index], depth + 1u);
        effect = petta_analysis_cardinality_join(effect, child);
    }
    return effect;
}

static Atom *type_fact_wire_cardinality_v1(
    Arena *arena, PettaAnalysisCardinality cardinality) {
    switch (cardinality) {
    case PETTA_ANALYSIS_CARDINALITY_DETERMINISTIC:
        return atom_symbol(arena, "MDet");
    case PETTA_ANALYSIS_CARDINALITY_SEMIDETERMINISTIC:
        return atom_symbol(arena, "MSemidet");
    case PETTA_ANALYSIS_CARDINALITY_NONDETERMINISTIC:
        return atom_symbol(arena, "MNondet");
    case PETTA_ANALYSIS_CARDINALITY_UNDETERMINED:
        return NULL;
    }
    return NULL;
}

static Atom *type_fact_literal_type_v1(Arena *arena, const Atom *expression) {
    if (!arena || !expression)
        return NULL;
    if (expression->kind == ATOM_GROUNDED) {
        switch (expression->ground.gkind) {
        case GV_INT:
        case GV_FLOAT:
        case GV_BIGINT:
        case GV_RATIONAL:
            return atom_symbol(arena, "TNum");
        case GV_STRING:
            return atom_symbol(arena, "TStr");
        case GV_BOOL:
            return atom_symbol(arena, "TBool");
        default:
            return NULL;
        }
    }
    if (type_fact_symbol_is_v1(expression, "true") ||
        type_fact_symbol_is_v1(expression, "false") ||
        type_fact_symbol_is_v1(expression, "True") ||
        type_fact_symbol_is_v1(expression, "False")) {
        return atom_symbol(arena, "TBool");
    }
    return NULL;
}

static Atom *type_fact_raw_union_v1(
    Arena *arena, const Atom *left, const Atom *right) {
    if (!arena || (!left && !right))
        return NULL;
    if (!left)
        return atom_deep_copy(arena, (Atom *)right);
    if (!right || atom_alpha_eq((Atom *)left, (Atom *)right))
        return atom_deep_copy(arena, (Atom *)left);
    Atom *arguments[] = {
        atom_deep_copy(arena, (Atom *)left),
        atom_deep_copy(arena, (Atom *)right),
    };
    return type_fact_expr_v1(arena, "|", arguments, 2u);
}

static Atom *type_fact_infer_result_v1(
    CettaPettaTypeFactProviderV1 *provider, Arena *arena,
    const Atom *expression, uint32_t depth);

static Atom *type_fact_declared_result_v1(
    CettaPettaTypeFactProviderV1 *provider, Arena *arena,
    const Atom *subject, CettaExprLen arity, bool applied) {
    if (!provider || !arena || !subject || subject->kind != ATOM_SYMBOL)
        return NULL;
    Atom **declared = NULL;
    uint32_t count = type_fact_declared_types_v1(
        provider, subject, arena, &declared);
    Atom *joined = NULL;
    for (uint32_t index = 0u; index < count; index++) {
        Atom *candidate = declared[index];
        if (applied) {
            if (!candidate || candidate->kind != ATOM_EXPR ||
                candidate->expr.len != arity + 2u ||
                candidate->expr.elems[0]->kind != ATOM_SYMBOL ||
                !type_fact_wire_mode_v1(
                    arena, atom_name_cstr(candidate->expr.elems[0]))) {
                continue;
            }
            candidate = candidate->expr.elems[candidate->expr.len - 1u];
        }
        Atom *next = type_fact_raw_union_v1(arena, joined, candidate);
        if (!next) {
            joined = NULL;
            break;
        }
        joined = next;
    }
    free(declared);
    return joined;
}

static Atom *type_fact_infer_relation_result_v1(
    CettaPettaTypeFactProviderV1 *provider, Arena *arena,
    const Atom *call, uint32_t depth) {
    if (!provider || !arena || !call || call->kind != ATOM_EXPR ||
        call->expr.len == 0u || call->expr.elems[0]->kind != ATOM_SYMBOL ||
        depth > PETTA_TYPE_FACT_DEPTH_LIMIT_V1) {
        return NULL;
    }
    Atom *const *equations = NULL;
    size_t equation_count = 0u;
    if (!type_fact_equation_catalog_v1(
            provider, &equations, &equation_count)) {
        return NULL;
    }
    Atom *joined = NULL;
    for (size_t index = 0u; index < equation_count; index++) {
        Atom *equation = equations[index];
        if (!type_fact_head_is_v1(equation, "=") ||
            equation->expr.len != 3u) {
            continue;
        }
        Atom *lhs = equation->expr.elems[1];
        if (!lhs || lhs->kind != ATOM_EXPR ||
            lhs->expr.len != call->expr.len || lhs->expr.len == 0u ||
            !atom_eq(lhs->expr.elems[0], call->expr.elems[0])) {
            continue;
        }
        Atom *candidate = type_fact_infer_result_v1(
            provider, arena, equation->expr.elems[2], depth + 1u);
        if (!candidate)
            continue;
        Atom *next = type_fact_raw_union_v1(arena, joined, candidate);
        if (!next) {
            joined = NULL;
            break;
        }
        joined = next;
    }
    return joined;
}

static Atom *type_fact_infer_result_v1(
    CettaPettaTypeFactProviderV1 *provider, Arena *arena,
    const Atom *expression, uint32_t depth) {
    if (!provider || !arena || !expression ||
        depth > PETTA_TYPE_FACT_DEPTH_LIMIT_V1) {
        return NULL;
    }
    Atom *literal = NULL;
    if (expression->kind == ATOM_GROUNDED) {
        switch (expression->ground.gkind) {
        case GV_INT:
        case GV_FLOAT:
        case GV_BIGINT:
        case GV_RATIONAL:
            literal = atom_symbol(arena, "Number");
            break;
        case GV_STRING:
            literal = atom_symbol(arena, "String");
            break;
        case GV_BOOL:
            literal = atom_symbol(arena, "Bool");
            break;
        default:
            break;
        }
    } else if (type_fact_symbol_is_v1(expression, "true") ||
               type_fact_symbol_is_v1(expression, "false") ||
               type_fact_symbol_is_v1(expression, "True") ||
               type_fact_symbol_is_v1(expression, "False")) {
        literal = atom_symbol(arena, "Bool");
    }
    if (literal)
        return literal;
    if (expression->kind == ATOM_SYMBOL) {
        return type_fact_declared_result_v1(
            provider, arena, expression, 0u, false);
    }
    if (expression->kind != ATOM_EXPR || expression->expr.len == 0u ||
        !expression->expr.elems ||
        expression->expr.elems[0]->kind != ATOM_SYMBOL) {
        return NULL;
    }
    const char *head = atom_name_cstr(expression->expr.elems[0]);
    if (strcmp(head, "empty") == 0 && expression->expr.len == 1u)
        return NULL;
    if (strcmp(head, "the") == 0 && expression->expr.len == 3u)
        return atom_deep_copy(arena, expression->expr.elems[1]);
    if (strcmp(head, "once") == 0 && expression->expr.len == 2u) {
        return type_fact_infer_result_v1(
            provider, arena, expression->expr.elems[1], depth + 1u);
    }
    if (strcmp(head, "collapse") == 0 && expression->expr.len == 2u) {
        Atom *element = type_fact_infer_result_v1(
            provider, arena, expression->expr.elems[1], depth + 1u);
        return element ? type_fact_expr1_v1(arena, "List", element) : NULL;
    }
    if (strcmp(head, "superpose") == 0 && expression->expr.len == 2u &&
        expression->expr.elems[1]->kind == ATOM_EXPR) {
        const Atom *alternatives = expression->expr.elems[1];
        Atom *joined = NULL;
        for (CettaExprIndex index = 0u;
             index < alternatives->expr.len; index++) {
            Atom *candidate = type_fact_infer_result_v1(
                provider, arena, alternatives->expr.elems[index],
                depth + 1u);
            if (!candidate)
                continue;
            Atom *next = type_fact_raw_union_v1(arena, joined, candidate);
            if (!next)
                return NULL;
            joined = next;
        }
        return joined;
    }
    if (strcmp(head, "if") == 0 && expression->expr.len == 4u) {
        Atom *left = type_fact_infer_result_v1(
            provider, arena, expression->expr.elems[2], depth + 1u);
        Atom *right = type_fact_infer_result_v1(
            provider, arena, expression->expr.elems[3], depth + 1u);
        return type_fact_raw_union_v1(arena, left, right);
    }
    if (strcmp(head, "case") == 0 && expression->expr.len == 3u &&
        expression->expr.elems[2]->kind == ATOM_EXPR) {
        const Atom *branches = expression->expr.elems[2];
        Atom *joined = NULL;
        for (CettaExprIndex index = 0u; index < branches->expr.len; index++) {
            const Atom *branch = branches->expr.elems[index];
            if (!branch || branch->kind != ATOM_EXPR ||
                branch->expr.len != 2u) {
                return NULL;
            }
            Atom *candidate = type_fact_infer_result_v1(
                provider, arena, branch->expr.elems[1], depth + 1u);
            if (!candidate)
                continue;
            Atom *next = type_fact_raw_union_v1(arena, joined, candidate);
            if (!next)
                return NULL;
            joined = next;
        }
        return joined;
    }
    if (strcmp(head, "cons") == 0 && expression->expr.len == 3u) {
        Atom *element = type_fact_infer_result_v1(
            provider, arena, expression->expr.elems[1], depth + 1u);
        return element ? type_fact_expr1_v1(arena, "List", element) : NULL;
    }
    static const char *const numeric[] = {
        "+", "-", "*", "/", "%", "mod", "min", "max",
    };
    for (size_t index = 0u;
         index < sizeof(numeric) / sizeof(numeric[0]); index++) {
        if (strcmp(head, numeric[index]) == 0)
            return atom_symbol(arena, "Number");
    }
    static const char *const boolean[] = {
        "=", "==", "!=", "<", "<=", ">", ">=", "is-var",
        "is-ground", "is-expr",
    };
    for (size_t index = 0u;
         index < sizeof(boolean) / sizeof(boolean[0]); index++) {
        if (strcmp(head, boolean[index]) == 0)
            return atom_symbol(arena, "Bool");
    }

    Atom *declared = type_fact_declared_result_v1(
        provider, arena, expression->expr.elems[0],
        expression->expr.len - 1u, true);
    if (declared)
        return declared;
    return type_fact_infer_relation_result_v1(
        provider, arena, expression, depth + 1u);
}

static CettaGsltProviderOutcomeV1 type_fact_push_result_type_v1(
    CettaPettaTypeFactProviderV1 *provider, Arena *arena, const Atom *goal,
    const Atom *expression, const Atom *raw_type, uint64_t answer_limit,
    CettaGsltProviderAnswersV1 *answers, char *error, size_t error_size) {
    (void)provider;
    Atom *wire = type_fact_wire_type_v1(arena, raw_type, 0u);
    Atom *answer = wire ? type_fact_relation_answer2_v1(
        arena, goal, atom_deep_copy(arena, (Atom *)expression), wire) : NULL;
    if (!wire)
        return CETTA_GSLT_PROVIDER_COMPLETED;
    return type_fact_push_v1(
        answers, answer, answer_limit, error, error_size);
}

static CettaGsltProviderOutcomeV1 type_fact_result_query_v1(
    CettaPettaTypeFactProviderV1 *provider, Arena *arena, const Atom *goal,
    uint64_t answer_limit, CettaGsltProviderAnswersV1 *answers,
    char *error, size_t error_size) {
    const Atom *expression = goal->expr.elems[1];
    Atom *literal = type_fact_literal_type_v1(arena, expression);
    if (literal) {
        Atom *answer = type_fact_relation_answer2_v1(
            arena, goal, atom_deep_copy(arena, (Atom *)expression), literal);
        return type_fact_push_v1(
            answers, answer, answer_limit, error, error_size);
    }
    /* quote/eval evidence is reconstructed compositionally by the v3
     * stage rules, rather than flattened into a second provider proof. */
    if ((type_fact_head_is_v1(expression, "quote") ||
         type_fact_head_is_v1(expression, "eval")) &&
        expression->expr.len == 2u) {
        return CETTA_GSLT_PROVIDER_COMPLETED;
    }
    if (type_fact_head_is_v1(expression, "the") &&
        expression->expr.len == 3u) {
        return type_fact_push_result_type_v1(
            provider, arena, goal, goal->expr.elems[1],
            expression->expr.elems[1], answer_limit, answers,
            error, error_size);
    }
    if (type_fact_head_is_v1(expression, "brand") &&
        expression->expr.len == 3u &&
        expression->expr.elems[1]->kind == ATOM_SYMBOL) {
        Atom *wire = type_fact_expr1_v1(
            arena, "TNominal",
            atom_deep_copy(arena, expression->expr.elems[1]));
        Atom *answer = wire ? type_fact_relation_answer2_v1(
            arena, goal, atom_deep_copy(arena, goal->expr.elems[1]), wire)
            : NULL;
        return type_fact_push_v1(
            answers, answer, answer_limit, error, error_size);
    }

    Atom *subject = NULL;
    CettaExprLen arity = 0u;
    bool applied = false;
    if (expression->kind == ATOM_SYMBOL) {
        subject = (Atom *)expression;
    } else if (expression->kind == ATOM_EXPR && expression->expr.len > 0u &&
               expression->expr.elems[0]->kind == ATOM_SYMBOL) {
        subject = expression->expr.elems[0];
        arity = expression->expr.len - 1u;
        applied = true;
    }
    if (!subject)
        return CETTA_GSLT_PROVIDER_COMPLETED;
    Atom **declared = NULL;
    uint32_t count = type_fact_declared_types_v1(
        provider, subject, arena, &declared);
    CettaGsltProviderOutcomeV1 outcome = CETTA_GSLT_PROVIDER_COMPLETED;
    for (uint32_t index = 0u; index < count; index++) {
        Atom *raw = declared[index];
        if (applied) {
            if (!raw || raw->kind != ATOM_EXPR ||
                raw->expr.len != arity + 2u ||
                raw->expr.elems[0]->kind != ATOM_SYMBOL ||
                !type_fact_wire_mode_v1(
                    arena, atom_name_cstr(raw->expr.elems[0]))) {
                continue;
            }
            raw = raw->expr.elems[raw->expr.len - 1u];
        }
        outcome = type_fact_push_result_type_v1(
            provider, arena, goal, goal->expr.elems[1], raw,
            answer_limit, answers, error, error_size);
        if (outcome != CETTA_GSLT_PROVIDER_COMPLETED)
            break;
    }
    free(declared);
    if (outcome == CETTA_GSLT_PROVIDER_COMPLETED && count == 0u) {
        Atom *inferred = type_fact_infer_result_v1(
            provider, arena, expression, 0u);
        if (inferred) {
            outcome = type_fact_push_result_type_v1(
                provider, arena, goal, expression, inferred,
                answer_limit, answers, error, error_size);
        }
    }
    return outcome;
}

static CettaGsltProviderOutcomeV1 type_fact_query_v1(
    void *raw_context, Arena *answer_arena, const Atom *goal,
    uint64_t answer_limit, CettaGsltProviderAnswersV1 *answers,
    char *error, size_t error_size) {
    CettaPettaTypeFactProviderV1 *provider = raw_context;
    if (!provider || !answer_arena || !goal || !answers ||
        goal->kind != ATOM_EXPR || goal->expr.len < 2u ||
        !goal->expr.elems || goal->expr.elems[0]->kind != ATOM_SYMBOL) {
        type_fact_error_v1(error, error_size,
                           "invalid PeTTa type-fact provider query");
        return CETTA_GSLT_PROVIDER_FAULT;
    }
    if (!space_read_token_matches_live_space(provider->read, provider->space)) {
        type_fact_error_v1(
            error, error_size,
            "PeTTa type-fact provider is stale for the live Space revision");
        return CETTA_GSLT_PROVIDER_FAULT;
    }
    const char *relation = atom_name_cstr(goal->expr.elems[0]);
    CettaGsltProviderOutcomeV1 outcome = CETTA_GSLT_PROVIDER_COMPLETED;

    if (strcmp(relation, "EnvDeclared") == 0) {
        Atom *subject = goal->expr.elems[1];
        if (goal->expr.len != 3u || subject->kind != ATOM_SYMBOL)
            return CETTA_GSLT_PROVIDER_COMPLETED;
        Atom **declared = NULL;
        uint32_t count = type_fact_declared_types_v1(
            provider, subject, answer_arena, &declared);
        uint32_t emitted = 0u;
        for (uint32_t index = 0u; index < count; index++) {
            Atom *definition = NULL;
            if (type_fact_symbol_is_v1(declared[index], "Type")) {
                definition = atom_symbol(answer_arena, "TType");
            }
            const char *wrapper = NULL;
            Atom *representation = NULL;
            if (!definition && !type_fact_decl_view_v1(
                    declared[index], &wrapper, &representation)) {
                continue;
            }
            Atom *wire = definition ? definition : type_fact_wire_type_v1(
                answer_arena, representation, 0u);
            if (!definition)
                definition = wire
                    ? type_fact_expr1_v1(answer_arena, wrapper, wire) : NULL;
            Atom *answer = definition ? type_fact_relation_answer2_v1(
                answer_arena, goal, atom_deep_copy(answer_arena, subject),
                definition) : NULL;
            if (!wire)
                continue;
            outcome = type_fact_push_v1(
                answers, answer, answer_limit, error, error_size);
            if (outcome != CETTA_GSLT_PROVIDER_COMPLETED)
                break;
            emitted++;
        }
        free(declared);
        if (outcome == CETTA_GSLT_PROVIDER_COMPLETED && emitted == 0u &&
            type_fact_subject_used_as_type_v1(provider, subject)) {
            Atom *answer = type_fact_relation_answer2_v1(
                answer_arena, goal, atom_deep_copy(answer_arena, subject),
                atom_symbol(answer_arena, "TType"));
            outcome = type_fact_push_v1(
                answers, answer, answer_limit, error, error_size);
        }
    } else if (strcmp(relation, "EnvDeclaredList") == 0) {
        Atom *subject = goal->expr.elems[1];
        if (goal->expr.len != 3u || subject->kind != ATOM_SYMBOL)
            return CETTA_GSLT_PROVIDER_COMPLETED;
        Atom **declared = NULL;
        uint32_t count = type_fact_declared_types_v1(
            provider, subject, answer_arena, &declared);
        Atom *tail = count > 0u ? atom_symbol(answer_arena, "DNil") : NULL;
        for (uint32_t index = count; tail && index > 0u; index--) {
            Atom *wire = type_fact_wire_type_v1(
                answer_arena, declared[index - 1u], 0u);
            Atom *declaration = wire ? type_fact_expr2_v1(
                answer_arena, "Decl",
                atom_deep_copy(answer_arena, subject), wire) : NULL;
            tail = declaration
                ? type_fact_expr2_v1(answer_arena, "DCons", declaration, tail)
                : NULL;
        }
        free(declared);
        if (tail) {
            Atom *answer = type_fact_relation_answer2_v1(
                answer_arena, goal, atom_deep_copy(answer_arena, subject), tail);
            outcome = type_fact_push_v1(
                answers, answer, answer_limit, error, error_size);
        }
    } else if (strcmp(relation, "EnvNonNewtype") == 0) {
        Atom *subject = goal->expr.elems[1];
        if (goal->expr.len != 2u || subject->kind != ATOM_SYMBOL)
            return CETTA_GSLT_PROVIDER_COMPLETED;
        Atom **declared = NULL;
        uint32_t count = type_fact_declared_types_v1(
            provider, subject, answer_arena, &declared);
        bool newtype = false;
        for (uint32_t index = 0u; index < count; index++) {
            const char *wrapper = NULL;
            if (type_fact_decl_view_v1(
                    declared[index], &wrapper, NULL) &&
                strcmp(wrapper, "TNewtype") == 0) {
                newtype = true;
                break;
            }
        }
        free(declared);
        if (!newtype) {
            Atom *answer = type_fact_relation_answer1_v1(
                answer_arena, goal, atom_deep_copy(answer_arena, subject));
            outcome = type_fact_push_v1(
                answers, answer, answer_limit, error, error_size);
        }
    } else if (strcmp(relation, "KnownExpressionEffect") == 0) {
        if (goal->expr.len != 3u || atom_has_vars(goal->expr.elems[1]))
            return CETTA_GSLT_PROVIDER_COMPLETED;
        Atom *mode = type_fact_wire_cardinality_v1(
            answer_arena, type_fact_effect_v1(
                provider, goal->expr.elems[1], 0u));
        if (mode) {
            Atom *answer = type_fact_relation_answer2_v1(
                answer_arena, goal,
                atom_deep_copy(answer_arena, goal->expr.elems[1]), mode);
            outcome = type_fact_push_v1(
                answers, answer, answer_limit, error, error_size);
        }
    } else if (strcmp(relation, "KnownExpressionResultType") == 0) {
        if (goal->expr.len != 3u || atom_has_vars(goal->expr.elems[1]))
            return CETTA_GSLT_PROVIDER_COMPLETED;
        outcome = type_fact_result_query_v1(
            provider, answer_arena, goal, answer_limit, answers,
            error, error_size);
    } else {
        type_fact_error_v1(
            error, error_size, "unknown PeTTa type-fact relation %s", relation);
        return CETTA_GSLT_PROVIDER_FAULT;
    }

    if (outcome == CETTA_GSLT_PROVIDER_COMPLETED &&
        !space_read_token_matches_live_space(provider->read, provider->space)) {
        cetta_gslt_provider_answers_free_v1(answers);
        type_fact_error_v1(
            error, error_size,
            "PeTTa type-fact provider revision changed during a query");
        return CETTA_GSLT_PROVIDER_FAULT;
    }
    return outcome;
}

CettaPettaTypeFactProviderV1 *
cetta_petta_type_fact_provider_create_v1(
    PettaProgram *program, Space *space, char *error, size_t error_size) {
    return cetta_petta_type_fact_provider_create_with_overlay_v1(
        program, space, NULL, 0u, error, error_size);
}

CettaPettaTypeFactProviderV1 *
cetta_petta_type_fact_provider_create_with_overlay_v1(
    PettaProgram *program, Space *space,
    Atom *const *forms, size_t form_count,
    char *error, size_t error_size) {
    if (!program || !space || space_instance_id(space) == 0u) {
        type_fact_error_v1(
            error, error_size, "PeTTa type-fact provider needs a live program and Space");
        return NULL;
    }
    if (!petta_program_analysis_enabled(program) &&
        !petta_program_enable_analysis(program)) {
        type_fact_error_v1(
            error, error_size, "could not admit PeTTa program analysis state");
        return NULL;
    }
    SpaceReadToken read = space_read_token(space);
    if (!type_fact_validate_declaration_catalog_v1(
            program, space, forms, form_count, error, error_size)) {
        return NULL;
    }
    if (!space_read_token_matches_live_space(read, space)) {
        type_fact_error_v1(
            error, error_size,
            "PeTTa declaration state changed during provider admission");
        return NULL;
    }
    CettaPettaTypeFactProviderV1 *provider = calloc(1u, sizeof(*provider));
    if (!provider) {
        type_fact_error_v1(
            error, error_size, "could not allocate PeTTa type-fact provider");
        return NULL;
    }
    provider->program = program;
    provider->space = space;
    provider->read = read;
    provider->overlay_forms = forms;
    provider->overlay_form_count = form_count;
    if (!type_fact_build_equation_catalog_v1(provider)) {
        type_fact_error_v1(
            error, error_size,
            "could not admit PeTTa equation catalog for type facts");
        free(provider);
        return NULL;
    }
    provider->physical[0] = (CettaGsltProviderV1){
        .relation = "EnvDeclared",
        .arity = 2u,
        .semantic_id = "petta.typecheck-v2.env.declared.v1",
        .context = provider,
        .query = type_fact_query_v1,
    };
    provider->physical[1] = (CettaGsltProviderV1){
        .relation = "EnvDeclaredList",
        .arity = 2u,
        .semantic_id = "petta.typecheck-v2.env.declared-list.v1",
        .context = provider,
        .query = type_fact_query_v1,
    };
    provider->physical[2] = (CettaGsltProviderV1){
        .relation = "EnvNonNewtype",
        .arity = 1u,
        .semantic_id = "petta.typecheck-v2.env.non-newtype.v1",
        .context = provider,
        .query = type_fact_query_v1,
    };
    provider->physical[3] = (CettaGsltProviderV1){
        .relation = "KnownExpressionEffect",
        .arity = 2u,
        .semantic_id = "petta.typecheck-v2.expression.effect.v1",
        .context = provider,
        .query = type_fact_query_v1,
    };
    provider->physical[4] = (CettaGsltProviderV1){
        .relation = "KnownExpressionResultType",
        .arity = 2u,
        .semantic_id = "petta.typecheck-v2.expression.result-type.v1",
        .context = provider,
        .query = type_fact_query_v1,
    };
    provider->registry = (CettaGsltProviderRegistryV1){
        .providers = provider->physical,
        .provider_count = 5u,
    };
    if (!cetta_gslt_provider_registry_validate_v1(
            &provider->registry, error, error_size)) {
        free(provider->equations);
        free(provider);
        return NULL;
    }
    return provider;
}

void cetta_petta_type_fact_provider_free_v1(
    CettaPettaTypeFactProviderV1 *provider) {
    if (provider)
        free(provider->equations);
    free(provider);
}

const CettaGsltProviderRegistryV1 *
cetta_petta_type_fact_provider_registry_v1(
    const CettaPettaTypeFactProviderV1 *provider) {
    return provider ? &provider->registry : NULL;
}

SpaceReadToken cetta_petta_type_fact_provider_read_v1(
    const CettaPettaTypeFactProviderV1 *provider) {
    return provider ? provider->read : (SpaceReadToken){0};
}

bool cetta_petta_type_fact_provider_is_current_v1(
    const CettaPettaTypeFactProviderV1 *provider) {
    return provider &&
        space_read_token_matches_live_space(provider->read, provider->space);
}

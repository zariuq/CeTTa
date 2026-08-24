#include "gslt_composition_v1.h"

#include "native_sha256.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    CettaGsltCompositionV1 value;
    size_t presentation_cap;
    size_t operator_cap;
    size_t equation_cap;
    size_t rewrite_cap;
} GsltCompositionBuilderV1;

typedef struct {
    const char **slots;
    size_t len;
    size_t cap;
} GsltNameSetV1;

static bool gslt_composition_error_v1(
    char *error, size_t error_size, const char *format, ...) {
    if (error != NULL && error_size > 0u) {
        va_list arguments;
        va_start(arguments, format);
        (void)vsnprintf(error, error_size, format, arguments);
        va_end(arguments);
    }
    return false;
}

bool cetta_gslt_composition_digest_v1(
    Atom *const *presentations,
    size_t presentation_count,
    char digest_out[65],
    char *error,
    size_t error_size) {
    static const char domain[] = "GSLTCompositionV1";
    CettaNativeSha256 sha;
    Arena scratch;
    bool ok = true;

    if (presentations == NULL || presentation_count == 0u ||
        digest_out == NULL)
        return gslt_composition_error_v1(
            error, error_size, "invalid GSLT composition digest request");

    digest_out[0] = '\0';
    arena_init(&scratch);
    cetta_native_sha256_init(&sha);
    cetta_native_sha256_update(
        &sha, (const uint8_t *)domain, sizeof(domain));
    for (size_t index = 0u; index < presentation_count; index++) {
        const char *printed = atom_to_parseable_string(
            &scratch, presentations[index]);
        uint8_t framed[8];
        size_t len;

        if (printed == NULL) {
            ok = false;
            break;
        }
        len = strlen(printed);
        if (len > UINT64_MAX) {
            ok = false;
            break;
        }
        uint64_t value = (uint64_t)len;
        for (size_t byte = 0u; byte < sizeof(framed); byte++)
            framed[sizeof(framed) - 1u - byte] =
                (uint8_t)(value >> (byte * 8u));
        cetta_native_sha256_update(&sha, framed, sizeof(framed));
        cetta_native_sha256_update(
            &sha, (const uint8_t *)printed, len);
    }
    if (ok)
        cetta_native_sha256_finish_hex(&sha, digest_out);
    else
        (void)gslt_composition_error_v1(
            error, error_size, "cannot canonicalize the GSLT composition");
    arena_free(&scratch);
    return ok;
}

static bool gslt_symbol_v1(const Atom *atom, const char *name) {
    return atom != NULL && atom->kind == ATOM_SYMBOL &&
           strcmp(atom_name_cstr((Atom *)atom), name) == 0;
}

static bool gslt_head_v1(
    const Atom *atom, const char *name, CettaExprLen arity) {
    return atom != NULL && atom->kind == ATOM_EXPR &&
           atom->expr.len == arity + 1u &&
           gslt_symbol_v1(atom->expr.elems[0], name);
}

static Atom *gslt_field_v1(
    Atom *presentation, const char *name, char *error, size_t error_size) {
    Atom *found = NULL;
    for (CettaExprIndex index = 2u;
         index < presentation->expr.len; index++) {
        Atom *field = presentation->expr.elems[index];
        if (field == NULL || field->kind != ATOM_EXPR ||
            field->expr.len == 0u ||
            !gslt_symbol_v1(field->expr.elems[0], name))
            continue;
        if (found != NULL) {
            (void)gslt_composition_error_v1(
                error, error_size, "GSLT presentation repeats %s", name);
            return NULL;
        }
        found = field;
    }
    return found;
}

static size_t gslt_name_hash_v1(const char *name) {
    size_t hash = sizeof(size_t) >= sizeof(uint64_t)
        ? (size_t)UINT64_C(1469598103934665603)
        : (size_t)UINT32_C(2166136261);
    size_t prime = sizeof(size_t) >= sizeof(uint64_t)
        ? (size_t)UINT64_C(1099511628211)
        : (size_t)UINT32_C(16777619);
    const unsigned char *cursor = (const unsigned char *)name;
    while (*cursor != 0u) {
        hash ^= (size_t)*cursor++;
        hash *= prime;
    }
    return hash;
}

static bool gslt_grow_names_v1(
    GsltNameSetV1 *names, char *error, size_t error_size) {
    size_t next = names->cap == 0u ? 32u : names->cap * 2u;
    const char **grown;
    if (next < names->cap || next > SIZE_MAX / sizeof(*grown))
        return gslt_composition_error_v1(
            error, error_size, "GSLT name table is too large");
    grown = (const char **)calloc(next, sizeof(*grown));
    if (grown == NULL)
        return gslt_composition_error_v1(
            error, error_size, "out of memory collecting GSLT names");
    for (size_t index = 0u; index < names->cap; index++) {
        const char *entry = names->slots[index];
        size_t slot;
        if (entry == NULL)
            continue;
        slot = gslt_name_hash_v1(entry) & (next - 1u);
        while (grown[slot] != NULL)
            slot = (slot + 1u) & (next - 1u);
        grown[slot] = entry;
    }
    free(names->slots);
    names->slots = grown;
    names->cap = next;
    return true;
}

static bool gslt_add_name_v1(
    GsltNameSetV1 *names, const char *name, const char *kind,
    char *error, size_t error_size) {
    size_t slot;
    if (names->cap == 0u || names->len + 1u > names->cap / 2u) {
        if (!gslt_grow_names_v1(names, error, error_size))
            return false;
    }
    slot = gslt_name_hash_v1(name) & (names->cap - 1u);
    while (names->slots[slot] != NULL) {
        if (strcmp(names->slots[slot], name) == 0)
            return gslt_composition_error_v1(
                error, error_size, "GSLT composition repeats %s %s",
                kind, name);
        slot = (slot + 1u) & (names->cap - 1u);
    }
    names->slots[slot] = name;
    names->len++;
    return true;
}

static bool gslt_reserve_v1(
    void **items, size_t *cap, size_t required, size_t item_size,
    const char *kind, char *error, size_t error_size) {
    size_t next;
    void *grown;
    if (required <= *cap)
        return true;
    next = *cap == 0u ? 16u : *cap;
    while (next < required) {
        if (next > SIZE_MAX / 2u) {
            next = required;
            break;
        }
        next *= 2u;
    }
    if (next > SIZE_MAX / item_size)
        return gslt_composition_error_v1(
            error, error_size, "GSLT %s table is too large", kind);
    grown = realloc(*items, next * item_size);
    if (grown == NULL)
        return gslt_composition_error_v1(
            error, error_size, "out of memory collecting GSLT %s", kind);
    *items = grown;
    *cap = next;
    return true;
}

static bool gslt_positive_arity_v1(const Atom *atom, size_t *out) {
    if (atom == NULL || out == NULL || atom->kind != ATOM_GROUNDED ||
        atom->ground.gkind != GV_INT || atom->ground.ival <= 0)
        return false;
    if ((uint64_t)atom->ground.ival > SIZE_MAX)
        return false;
    *out = (size_t)atom->ground.ival;
    return true;
}

static bool gslt_push_operator_v1(
    GsltCompositionBuilderV1 *builder, const char *presentation_name,
    const char *name, size_t arity,
    char *error, size_t error_size) {
    CettaGsltCompositionV1 *composition = &builder->value;
    if (!gslt_reserve_v1(
            (void **)&composition->operators, &builder->operator_cap,
            composition->operator_count + 1u,
            sizeof(*composition->operators), "operators",
            error, error_size))
        return false;
    composition->operators[composition->operator_count++] =
        (CettaGsltOperatorV1){
            .presentation_name = presentation_name,
            .name = name,
            .arity = arity,
        };
    return true;
}

static bool gslt_push_equation_v1(
    GsltCompositionBuilderV1 *builder, const char *presentation_name,
    Atom *term, char *error, size_t error_size) {
    CettaGsltCompositionV1 *composition = &builder->value;
    if (!gslt_reserve_v1(
            (void **)&composition->equations, &builder->equation_cap,
            composition->equation_count + 1u,
            sizeof(*composition->equations), "equations",
            error, error_size))
        return false;
    composition->equations[composition->equation_count++] =
        (CettaGsltEquationV1){
            .presentation_name = presentation_name,
            .term = term,
        };
    return true;
}

static bool gslt_push_rewrite_v1(
    GsltCompositionBuilderV1 *builder, const char *presentation_name,
    const char *name, Atom *head, Atom *body,
    char *error, size_t error_size) {
    CettaGsltCompositionV1 *composition = &builder->value;
    if (!gslt_reserve_v1(
            (void **)&composition->rewrites, &builder->rewrite_cap,
            composition->rewrite_count + 1u,
            sizeof(*composition->rewrites), "rewrites",
            error, error_size))
        return false;
    composition->rewrites[composition->rewrite_count++] =
        (CettaGsltRewriteV1){
            .presentation_name = presentation_name,
            .name = name,
            .head = head,
            .body = body,
        };
    return true;
}

static bool gslt_push_presentation_v1(
    GsltCompositionBuilderV1 *builder,
    CettaGsltPresentationV1 presentation,
    char *error, size_t error_size) {
    CettaGsltCompositionV1 *composition = &builder->value;
    if (!gslt_reserve_v1(
            (void **)&composition->presentations,
            &builder->presentation_cap,
            composition->presentation_count + 1u,
            sizeof(*composition->presentations), "presentations",
            error, error_size))
        return false;
    composition->presentations[composition->presentation_count++] =
        presentation;
    return true;
}

bool cetta_gslt_composition_has_operator_v1(
    const CettaGsltCompositionV1 *composition,
    const char *name, size_t arity) {
    if (composition == NULL || name == NULL)
        return false;
    for (size_t index = 0u; index < composition->operator_count; index++) {
        if (composition->operators[index].arity == arity &&
            strcmp(composition->operators[index].name, name) == 0)
            return true;
    }
    return false;
}

bool cetta_gslt_source_variable_v1(const Atom *atom) {
    if (atom == NULL)
        return false;
    if (atom->kind == ATOM_VAR)
        return true;
    if (atom->kind != ATOM_SYMBOL)
        return false;
    const char *name = atom_name_cstr((Atom *)atom);
    return name[0] == '?' && name[1] != '\0';
}

const char *cetta_gslt_source_variable_name_v1(const Atom *atom) {
    const char *name = atom_name_cstr((Atom *)atom);
    return atom->kind == ATOM_SYMBOL && name[0] == '?' ? name + 1u : name;
}

static bool gslt_validate_term_v1(
    const CettaGsltCompositionV1 *composition, const Atom *term,
    size_t depth, size_t depth_limit, char *error, size_t error_size) {
    if (term == NULL || depth > depth_limit)
        return gslt_composition_error_v1(
            error, error_size, "GSLT term exceeds the composition depth limit");
    if (term->kind == ATOM_SYMBOL) {
        const char *name = atom_name_cstr((Atom *)term);
        if (name[0] == '?' &&
            (name[1] == '\0' || (name[1] == '_' && name[2] == '\0')))
            return gslt_composition_error_v1(
                error, error_size, "GSLT rule contains an anonymous variable");
        return true;
    }
    if (term->kind == ATOM_VAR || term->kind == ATOM_GROUNDED)
        return true;
    if (term->kind != ATOM_EXPR || term->expr.len == 0u ||
        term->expr.elems[0]->kind != ATOM_SYMBOL)
        return gslt_composition_error_v1(
            error, error_size, "GSLT rule contains a malformed application");
    const char *name = atom_name_cstr(term->expr.elems[0]);
    size_t arity = (size_t)term->expr.len - 1u;
    if (!cetta_gslt_composition_has_operator_v1(composition, name, arity))
        return gslt_composition_error_v1(
            error, error_size,
            "GSLT application %s/%zu lacks a signature declaration",
            name, arity);
    for (CettaExprIndex index = 1u; index < term->expr.len; index++) {
        if (!gslt_validate_term_v1(
                composition, term->expr.elems[index], depth + 1u,
                depth_limit, error, error_size))
            return false;
    }
    return true;
}

bool cetta_gslt_composition_validate_term_v1(
    const CettaGsltCompositionV1 *composition, const Atom *term,
    size_t depth_limit, char *error, size_t error_size) {
    if (composition == NULL)
        return gslt_composition_error_v1(
            error, error_size, "invalid GSLT composition");
    return gslt_validate_term_v1(
        composition, term, 0u, depth_limit, error, error_size);
}

static bool gslt_rewrite_relation_v1(
    const Atom *term, const char **name_out, size_t *arity_out) {
    if (term == NULL || term->kind != ATOM_EXPR || term->expr.len == 0u ||
        term->expr.elems[0]->kind != ATOM_SYMBOL)
        return false;
    *name_out = atom_name_cstr(term->expr.elems[0]);
    *arity_out = (size_t)term->expr.len - 1u;
    return true;
}

static bool gslt_rewrite_defines_relation_v1(
    const CettaGsltRewriteV1 *rewrite, const char *relation, size_t arity) {
    const char *head_relation;
    size_t head_arity;
    return gslt_rewrite_relation_v1(
               rewrite->head, &head_relation, &head_arity) &&
           head_arity == arity && strcmp(head_relation, relation) == 0;
}

bool cetta_gslt_composition_select_rewrite_closure_v1(
    const CettaGsltCompositionV1 *composition,
    const char *const *entry_rule_names, size_t entry_rule_count,
    uint8_t **selected_rewrites_out, size_t *selected_rewrite_count_out,
    char *error, size_t error_size) {
    uint8_t *selected;
    size_t selected_count = 0u;
    bool changed = true;

    if (composition == NULL || selected_rewrites_out == NULL ||
        selected_rewrite_count_out == NULL)
        return gslt_composition_error_v1(
            error, error_size, "invalid GSLT rewrite selection request");
    *selected_rewrites_out = NULL;
    *selected_rewrite_count_out = 0u;
    if (composition->rewrite_count == 0u)
        return gslt_composition_error_v1(
            error, error_size, "GSLT composition has no rewrites to select");
    if (composition->rewrite_count > SIZE_MAX / sizeof(*selected))
        return gslt_composition_error_v1(
            error, error_size, "GSLT rewrite selection is too large");
    selected = (uint8_t *)calloc(composition->rewrite_count, 1u);
    if (selected == NULL)
        return gslt_composition_error_v1(
            error, error_size,
            "out of memory selecting GSLT rewrite dependencies");

    if (entry_rule_count == 0u) {
        memset(selected, 1, composition->rewrite_count);
        selected_count = composition->rewrite_count;
    } else {
        for (size_t entry_index = 0u;
             entry_index < entry_rule_count; entry_index++) {
            bool found = false;
            if (entry_rule_names == NULL ||
                entry_rule_names[entry_index] == NULL ||
                entry_rule_names[entry_index][0] == '\0') {
                free(selected);
                return gslt_composition_error_v1(
                    error, error_size, "GSLT entry rule name is empty");
            }
            for (size_t rule_index = 0u;
                 rule_index < composition->rewrite_count; rule_index++) {
                if (strcmp(
                        composition->rewrites[rule_index].name,
                        entry_rule_names[entry_index]) != 0)
                    continue;
                found = true;
                if (!selected[rule_index]) {
                    selected[rule_index] = 1u;
                    selected_count++;
                }
            }
            if (!found) {
                free(selected);
                return gslt_composition_error_v1(
                    error, error_size,
                    "GSLT entry rule is not authored: %s",
                    entry_rule_names[entry_index]);
            }
        }
    }

    while (changed) {
        changed = false;
        for (size_t rule_index = 0u;
             rule_index < composition->rewrite_count; rule_index++) {
            const CettaGsltRewriteV1 *rewrite;
            if (!selected[rule_index])
                continue;
            rewrite = &composition->rewrites[rule_index];
            for (CettaExprIndex body_index = 1u;
                 body_index < rewrite->body->expr.len; body_index++) {
                const char *relation;
                size_t arity;
                if (!gslt_rewrite_relation_v1(
                        rewrite->body->expr.elems[body_index],
                        &relation, &arity)) {
                    free(selected);
                    return gslt_composition_error_v1(
                        error, error_size,
                        "GSLT selected rule body contains a malformed application");
                }
                for (size_t candidate = 0u;
                     candidate < composition->rewrite_count; candidate++) {
                    if (selected[candidate] ||
                        !gslt_rewrite_defines_relation_v1(
                            &composition->rewrites[candidate],
                            relation, arity))
                        continue;
                    selected[candidate] = 1u;
                    selected_count++;
                    changed = true;
                }
            }
        }
    }
    *selected_rewrites_out = selected;
    *selected_rewrite_count_out = selected_count;
    return true;
}

static bool gslt_collect_presentation_v1(
    GsltCompositionBuilderV1 *builder, Atom *presentation,
    GsltNameSetV1 *presentation_names, GsltNameSetV1 *rewrite_names,
    char *error, size_t error_size) {
    CettaGsltCompositionV1 *composition = &builder->value;
    Atom *signature;
    Atom *equations;
    Atom *rewrites;
    const char *presentation_name;
    CettaGsltPresentationV1 slice;

    if (presentation == NULL || presentation->kind != ATOM_EXPR ||
        presentation->expr.len < 5u ||
        !gslt_symbol_v1(presentation->expr.elems[0],
                        "gslt-presentation-v1") ||
        presentation->expr.elems[1]->kind != ATOM_SYMBOL)
        return gslt_composition_error_v1(
            error, error_size, "input is not a GSLT presentation");
    presentation_name = atom_name_cstr(presentation->expr.elems[1]);
    if (!gslt_add_name_v1(
            presentation_names, presentation_name, "presentation",
            error, error_size))
        return false;
    for (CettaExprIndex index = 2u; index < presentation->expr.len; index++) {
        Atom *field = presentation->expr.elems[index];
        if (field == NULL || field->kind != ATOM_EXPR ||
            field->expr.len == 0u ||
            field->expr.elems[0]->kind != ATOM_SYMBOL)
            return gslt_composition_error_v1(
                error, error_size, "GSLT presentation has a malformed field");
        const char *field_name = atom_name_cstr(field->expr.elems[0]);
        if (strcmp(field_name, "signature") != 0 &&
            strcmp(field_name, "equations") != 0 &&
            strcmp(field_name, "rewrites") != 0) {
            if (strcmp(field_name, "nik-authority-frame-v1") == 0)
                return gslt_composition_error_v1(
                    error, error_size,
                    "GSLT composition v1 does not lower authority-framed rules");
            return gslt_composition_error_v1(
                error, error_size,
                "GSLT presentation has unknown field %s", field_name);
        }
    }
    signature = gslt_field_v1(presentation, "signature", error, error_size);
    equations = gslt_field_v1(presentation, "equations", error, error_size);
    rewrites = gslt_field_v1(presentation, "rewrites", error, error_size);
    if (signature == NULL || equations == NULL || rewrites == NULL)
        return error != NULL && error[0] != '\0' ? false :
            gslt_composition_error_v1(
                error, error_size,
                "GSLT presentation omits signature, equations, or rewrites");

    slice = (CettaGsltPresentationV1){
        .name = presentation_name,
        .operator_begin = composition->operator_count,
        .equation_begin = composition->equation_count,
        .rewrite_begin = composition->rewrite_count,
    };
    for (CettaExprIndex index = 1u; index < signature->expr.len; index++) {
        Atom *declaration = signature->expr.elems[index];
        size_t arity;
        if (!gslt_head_v1(declaration, "operator", 2u) ||
            declaration->expr.elems[1]->kind != ATOM_SYMBOL ||
            !gslt_positive_arity_v1(declaration->expr.elems[2], &arity))
            return gslt_composition_error_v1(
                error, error_size,
                "GSLT signatures require (operator NAME POSITIVE-ARITY)");
        if (!gslt_push_operator_v1(
                builder, presentation_name,
                atom_name_cstr(declaration->expr.elems[1]), arity,
                error, error_size))
            return false;
    }
    slice.operator_count = composition->operator_count - slice.operator_begin;
    for (CettaExprIndex index = 1u; index < equations->expr.len; index++) {
        if (!gslt_push_equation_v1(
                builder, presentation_name, equations->expr.elems[index],
                error, error_size))
            return false;
    }
    slice.equation_count = composition->equation_count - slice.equation_begin;
    for (CettaExprIndex index = 1u; index < rewrites->expr.len; index++) {
        Atom *rule = rewrites->expr.elems[index];
        Atom *head;
        Atom *body;
        const char *rule_name;
        if (!gslt_head_v1(rule, "rule", 3u) ||
            rule->expr.elems[1]->kind != ATOM_SYMBOL ||
            !(head = rule->expr.elems[2]) || !(body = rule->expr.elems[3]) ||
            !gslt_head_v1(head, "head", 1u) ||
            body->kind != ATOM_EXPR || body->expr.len == 0u ||
            !gslt_symbol_v1(body->expr.elems[0], "body"))
            return gslt_composition_error_v1(
                error, error_size,
                "GSLT presentation contains a malformed rule");
        rule_name = atom_name_cstr(rule->expr.elems[1]);
        if (!gslt_add_name_v1(
                rewrite_names, rule_name, "rule", error, error_size) ||
            !gslt_push_rewrite_v1(
                builder, presentation_name, rule_name,
                head->expr.elems[1], body, error, error_size))
            return false;
    }
    slice.rewrite_count = composition->rewrite_count - slice.rewrite_begin;
    return gslt_push_presentation_v1(builder, slice, error, error_size);
}

bool cetta_gslt_composition_build_v1(
    Atom *const *presentations, size_t presentation_count,
    CettaGsltCompositionV1 *composition_out,
    char *error, size_t error_size) {
    GsltCompositionBuilderV1 builder = {0};
    GsltNameSetV1 presentation_names = {0};
    GsltNameSetV1 rewrite_names = {0};
    bool ok = false;

    if (composition_out == NULL || presentations == NULL ||
        presentation_count == 0u)
        return gslt_composition_error_v1(
            error, error_size, "invalid GSLT composition request");
    *composition_out = (CettaGsltCompositionV1){0};
    if (error != NULL && error_size > 0u)
        error[0] = '\0';
    for (size_t index = 0u; index < presentation_count; index++) {
        if (!gslt_collect_presentation_v1(
                &builder, presentations[index], &presentation_names,
                &rewrite_names, error, error_size))
            goto done;
    }
    *composition_out = builder.value;
    builder.value = (CettaGsltCompositionV1){0};
    ok = true;

done:
    free(rewrite_names.slots);
    free(presentation_names.slots);
    cetta_gslt_composition_free_v1(&builder.value);
    return ok;
}

void cetta_gslt_composition_free_v1(CettaGsltCompositionV1 *composition) {
    if (composition == NULL)
        return;
    free(composition->rewrites);
    free(composition->equations);
    free(composition->operators);
    free(composition->presentations);
    *composition = (CettaGsltCompositionV1){0};
}

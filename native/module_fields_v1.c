#include "module_fields_v1.h"
#include "module_value_internal_v1.h"
#include "language_def_ground_term_v1.h"

static Atom *refused(ModuleWorkV1 *w, const char *reason, const char *detail) {
    module_fail_v1(w, reason, detail);
    return w->refusal;
}

static bool layers(ModuleWorkV1 *w, Atom *value, Atom **core, ModuleValuesV1 *rows) {
    if (module_form_v1(value, "GSLTLanguageDefWireV1", 5u)) {
        *core = value;
        return true;
    }
    if (!module_form_v1(value, "ModuleExtendedLanguageV1", 2u) ||
        !module_form_v1(value->expr.elems[1], "GSLTLanguageDefWireV1", 5u))
        return module_fail_v1(w, "malformed-layers", "Expected an authoritative core with raw declaration layers.");
    *core = value->expr.elems[1];
    if (!module_unlist_v1(w, value->expr.elems[2], rows)) return false;
    for (size_t i = 0; i < rows->len; ++i) {
        Atom *row = rows->items[i];
        if (!module_form_v1(row, "ModuleLayerV1", 4u) ||
            !module_form_v1(row->expr.elems[1], "SyntaxOriginV1", 2u) ||
            !module_string_v1(row->expr.elems[1]->expr.elems[1]) ||
            !module_string_v1(row->expr.elems[1]->expr.elems[2]) ||
            !module_form_v1(row->expr.elems[2], "GSLTLanguageDefWireV1", 5u) ||
            !module_string_v1(row->expr.elems[3]))
            return module_fail_v1(w, "malformed-layer", "Expected a portable layer origin and authoring signature.");
        ModuleValuesV1 fields = {0};
        bool finite = module_unlist_v1(w, row->expr.elems[4], &fields);
        module_clear_v1(&fields);
        if (!finite) return false;
        for (size_t j = 0; j < i; ++j) if (atom_eq(row->expr.elems[1], rows->items[j]->expr.elems[1]))
            return module_fail_v1(w, "duplicate-layer", "A stored layer key must occur once.");
    }
    return true;
}

static Atom *extended(ModuleWorkV1 *w, Atom *core, ModuleValuesV1 *rows) {
    Atom *fields[] = {core, module_list_v1(w->arena, rows)};
    Atom *value = module_make_v1(w->arena, "ModuleExtendedLanguageV1", fields, 2u);
    Atom *owned = value ? atom_deep_copy(w->arena, value) : NULL;
    return owned ? owned : refused(w, "allocation-failure", "Layer publication failed.");
}

Atom *cetta_module_attach_layer_v1(Arena *arena, Atom *base, Atom *key,
    Atom *schema, const CettaLanguageDefCoreV1 *authority, Atom *category, Atom *fields) {
    ModuleWorkV1 w = {.arena = arena, .remaining = 1000000u, .site = key};
    ModuleValuesV1 prior = {0}, values = {0}; Atom *core = NULL, *result = NULL;
    if (!authority || !module_form_v1(key, "SyntaxOriginV1", 2u) ||
        !module_string_v1(key->expr.elems[1]) || !module_string_v1(key->expr.elems[2]) ||
        !module_string_v1(category) || !module_form_v1(schema, "GSLTLanguageDefWireV1", 5u)) {
        result = refused(&w, "malformed-layer", "Expected a layer origin, authoring core, category and fields."); goto done;
    }
    if (authority->equation_len || authority->rewrite_len) {
        result = refused(&w, "layer-elaboration-required",
            "A schema with laws needs a law-preserving elaborator; it is not a raw declaration codec."); goto done;
    }
    if (!layers(&w, base, &core, &prior) || !module_unlist_v1(&w, fields, &values)) goto failed;
    CettaLdGroundTermV1Status category_status; char category_error[256] = {0};
    if (!cetta_language_def_ground_term_v1_supports_pattern_codec(authority,
            category->ground.sval, 1000000u, &category_status,
            category_error, sizeof(category_error))) {
        result = refused(&w, cetta_ld_ground_term_v1_status_name(category_status),
            category_error[0] ? category_error : "The field category is not supported by this declaration codec.");
        goto done;
    }
    for (size_t i = 0; i < values.len; ++i) {
        CettaLdGroundTermV1Status status; char error[256] = {0};
        if (!cetta_language_def_ground_term_v1_admit(authority, category->ground.sval, values.items[i],
                1000000u, 1000000u, &status, error, sizeof(error))) {
            Atom *location[] = {key, atom_int(arena, (int64_t)i)};
            w.site = module_make_v1(arena, "ModuleFieldPositionV1", location, 2u);
            result = refused(&w, cetta_ld_ground_term_v1_status_name(status),
                error[0] ? error : "The field is not admitted by its authoring language."); goto done;
        }
    }
    Atom *row_fields[] = {key, schema, category, fields};
    Atom *row = module_make_v1(arena, "ModuleLayerV1", row_fields, 4u);
    for (size_t i = 0; i < prior.len; ++i) if (atom_eq(prior.items[i]->expr.elems[1], key)) {
        if (!atom_eq(prior.items[i], row)) {
            result = refused(&w, "layer-origin-conflict", "The same layer origin has incompatible declaration data."); goto done;
        }
        result = extended(&w, core, &prior); goto done;
    }
    if (!module_push_v1(&w, &prior, row)) goto failed;
    result = extended(&w, core, &prior); goto done;
failed:
    result = w.refusal ? w.refusal : refused(&w, "malformed-layer", "Layer attachment failed.");
done:
    module_clear_v1(&prior); module_clear_v1(&values);
    return result;
}

static Atom *join_layers(Arena *arena, Atom *target, Atom *left, Atom *right) {
    ModuleWorkV1 w = {.arena = arena, .remaining = 1000000u};
    ModuleValuesV1 l = {0}, r = {0}; Atom *lc = NULL, *rc = NULL, *result = NULL;
    if (!layers(&w, left, &lc, &l) || !layers(&w, right, &rc, &r)) goto failed;
    if (target && !module_form_v1(target, "GSLTLanguageDefWireV1", 5u)) {
        result = refused(&w, "malformed-layer-base", "Expected an explicitly assembled target LanguageDef."); goto done;
    }
    if (!target && !atom_eq(lc, rc)) {
        result = refused(&w, "layer-base-mismatch", "Layer composition requires the exact same authoritative base core."); goto done;
    }
    for (size_t i = 0; i < r.len; ++i) {
        bool shared = false;
        for (size_t j = 0; j < l.len; ++j) {
            if (!module_spend_v1(&w)) goto failed;
            if (!atom_eq(l.items[j]->expr.elems[1], r.items[i]->expr.elems[1])) continue;
            shared = true;
            if (!atom_eq(l.items[j], r.items[i])) {
                Atom *conflicts[] = {l.items[j], r.items[i]};
                w.site = module_make_v1(arena, "ModuleConflictV1", conflicts, 2u);
                result = refused(&w, "layer-origin-conflict", "A shared layer must retain the same schema and payload."); goto done;
            }
            break;
        }
        if (!shared && !module_push_v1(&w, &l, r.items[i])) goto failed;
    }
    /* These rows are terms of their own stored schema, not references into
     * the decorated core. Explicit change of base therefore leaves them
     * unchanged. This does not construct a theory morphism or certify its
     * conservativity. Core-relative payloads need their own transport. */
    result = extended(&w, target ? target : lc, &l); goto done;
failed:
    result = w.refusal;
done:
    module_clear_v1(&l); module_clear_v1(&r); return result;
}

Atom *cetta_module_join_layers_v1(Arena *arena, Atom *left, Atom *right) {
    return join_layers(arena, NULL, left, right);
}

Atom *cetta_module_join_layers_over_v1(Arena *arena, Atom *target, Atom *left, Atom *right) {
    return join_layers(arena, target, left, right);
}

Atom *cetta_module_base_core_v1(Arena *arena, Atom *value) {
    ModuleWorkV1 w = {.arena = arena, .remaining = 1000000u};
    ModuleValuesV1 rows = {0}; Atom *core = NULL;
    Atom *result = layers(&w, value, &core, &rows) ? atom_deep_copy(arena, core) : w.refusal;
    module_clear_v1(&rows); return result;
}

Atom *cetta_module_layer_fields_v1(Arena *arena, Atom *value, Atom *key) {
    ModuleWorkV1 w = {.arena = arena, .remaining = 1000000u, .site = key};
    ModuleValuesV1 rows = {0}; Atom *core = NULL, *result = NULL;
    if (!layers(&w, value, &core, &rows)) { result = w.refusal; goto done; }
    for (size_t i = 0; i < rows.len; ++i) if (atom_eq(rows.items[i]->expr.elems[1], key)) {
        result = atom_deep_copy(arena, rows.items[i]->expr.elems[4]); goto done;
    }
    result = refused(&w, "missing-layer", "No declaration layer has this portable origin.");
done:
    module_clear_v1(&rows); return result;
}

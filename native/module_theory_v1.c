#include "module_value_internal_v1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ModuleBindingV1 {
    Atom *name, *value;
    const struct ModuleBindingV1 *parent;
} ModuleBindingV1;
typedef struct ModuleCallV1 {
    Atom *module, *theory;
    const struct ModuleCallV1 *parent;
} ModuleCallV1;
typedef struct {
    ModuleWorkV1 work;
    Atom *root, *catalog;
    CettaGsltOriginPolicyV1 policy;
    uint32_t invocation;
} ModuleAssemblyV1;

static Atom *presentation(ModuleAssemblyV1 *a, Atom *rows, Atom *families, Atom *sources) {
    Atom *fields[] = {a->root, atom_symbol(a->work.arena,
        a->policy == CETTA_GSLT_ORIGIN_COHERENT_V1 ? "OriginCoherentV1" : "OriginViewV1"), rows, families, sources};
    return module_make_v1(a->work.arena, "ModulePresentationV1", fields, 5u);
}

static bool append_unique(ModuleWorkV1 *w, ModuleValuesV1 *v, Atom *value) {
    for (size_t i = 0; i < v->len; ++i) if (atom_eq(v->items[i], value)) return true;
    return module_push_v1(w, v, value);
}

static Atom *combine_metadata(ModuleAssemblyV1 *a, Atom *left, Atom *right) {
    ModuleValuesV1 l = {0}, r = {0}; Atom *out = NULL;
    if (!module_unlist_v1(&a->work, left, &l) || !module_unlist_v1(&a->work, right, &r)) goto done;
    for (size_t i = 0; i < r.len; ++i) if (!append_unique(&a->work, &l, r.items[i])) goto done;
    out = module_list_v1(a->work.arena, &l);
done:
    module_clear_v1(&l); module_clear_v1(&r); return out;
}

static bool entries(ModuleAssemblyV1 *a, Atom *value, ModuleValuesV1 *rows) {
    if (!module_form_v1(value, "ModulePresentationV1", 5u) ||
        !atom_is_symbol(value->expr.elems[2], a->policy == CETTA_GSLT_ORIGIN_COHERENT_V1 ? "OriginCoherentV1" : "OriginViewV1"))
        return module_fail_v1(&a->work, "presentation-policy", "Expected a presentation with the selected origin policy.");
    if (!module_unlist_v1(&a->work, value->expr.elems[3], rows)) return false;
    for (size_t i = 0; i < rows->len; ++i) {
        Atom *row = rows->items[i];
        if (!module_form_v1(row, "ModuleEntryV1", 3u) || !module_form_v1(row->expr.elems[1], "SyntaxOriginV1", 2u) ||
            !module_string_v1(row->expr.elems[1]->expr.elems[1]) || !module_string_v1(row->expr.elems[1]->expr.elems[2]))
            return module_fail_v1(&a->work, "malformed-entry", "Expected origin-bearing module declarations.");
    }
    return true;
}

static Atom *entry_key(ModuleAssemblyV1 *a, Atom *body, Atom *site) {
    Atom *fields[3];
    if (module_form_v1(body, "ModuleTermV1", 4u)) {
        /* A production occurrence is independent of its constructor label. */
        fields[0] = body->expr.elems[1]; fields[1] = body->expr.elems[2]; fields[2] = site;
        return module_make_v1(a->work.arena, "ModuleTermKeyV1", fields, 3u);
    }
    if (module_form_v1(body, "ModuleEquationV1", 4u) || module_form_v1(body, "ModuleRewriteV1", 4u)) {
        fields[0] = body->expr.elems[0]; fields[1] = body->expr.elems[1];
        return module_make_v1(a->work.arena, "ModuleLawKeyV1", fields, 2u);
    }
    if (module_form_v1(body, "ModuleExportV1", 2u)) return module_make_v1(a->work.arena, "ModuleExportKeyV1", &body->expr.elems[2], 1u);
    fields[0] = body && body->kind == ATOM_EXPR ? body->expr.elems[0] : body; fields[1] = site;
    return module_make_v1(a->work.arena, "ModulePragmaKeyV1", fields, 2u);
}

static Atom *join(ModuleAssemblyV1 *a, Atom *left, Atom *right, unsigned operation) {
    ModuleValuesV1 l = {0}, r = {0}, result = {0};
    CettaGsltOriginEntryV1 *li = NULL, *ri = NULL, *joined = NULL; Atom *out = NULL;
    if (!entries(a, left, &l) || !entries(a, right, &r)) goto done;
    if (!operation) {
        li = calloc(l.len ? l.len : 1u, sizeof(*li)); ri = calloc(r.len ? r.len : 1u, sizeof(*ri));
        if (!li || !ri) { module_fail_v1(&a->work, "allocation-failure", "Join allocation failed."); goto done; }
        for (size_t i = 0; i < l.len; ++i) li[i] = (CettaGsltOriginEntryV1){l.items[i]->expr.elems[1], l.items[i]->expr.elems[2], l.items[i]->expr.elems[3]};
        for (size_t i = 0; i < r.len; ++i) ri[i] = (CettaGsltOriginEntryV1){r.items[i]->expr.elems[1], r.items[i]->expr.elems[2], r.items[i]->expr.elems[3]};
        size_t count = 0; CettaGsltOriginJoinWitnessV1 witness;
        if (!cetta_gslt_origin_join_v1(li, l.len, ri, r.len, a->policy, &joined, &count, &witness)) {
            if (witness.first < l.len + r.len && witness.second < l.len + r.len) {
                Atom *conflicts[] = {witness.first < l.len ? l.items[witness.first] : r.items[witness.first - l.len],
                    witness.second < l.len ? l.items[witness.second] : r.items[witness.second - l.len]};
                a->work.site = module_make_v1(a->work.arena, "ModuleConflictV1", conflicts, 2u);
            }
            module_fail_v1(&a->work, witness.status == CETTA_GSLT_ORIGIN_JOIN_ORIGIN_CONFLICT_V1 ? "origin-conflict" : "declaration-conflict",
                "Module join preserves declaration identities and refuses conflicting views."); goto done;
        }
        for (size_t i = 0; i < count; ++i) {
            Atom *fields[] = {joined[i].origin, joined[i].label, joined[i].body};
            if (!module_push_v1(&a->work, &result, module_make_v1(a->work.arena, "ModuleEntryV1", fields, 3u))) goto done;
        }
    } else {
        for (size_t i = 0; i < l.len; ++i) {
            bool found = false;
            for (size_t j = 0; j < r.len; ++j) {
                if (!module_spend_v1(&a->work)) goto done;
                if (a->policy == CETTA_GSLT_ORIGIN_COHERENT_V1 ? atom_eq(l.items[i]->expr.elems[1], r.items[j]->expr.elems[1]) : atom_eq(l.items[i], r.items[j])) { found = true; break; }
            }
            if ((operation == 1u ? found : !found) && !module_push_v1(&a->work, &result, l.items[i])) goto done;
        }
    }
    Atom *families = combine_metadata(a, left->expr.elems[4], right->expr.elems[4]);
    Atom *sources = combine_metadata(a, left->expr.elems[5], right->expr.elems[5]);
    if (families && sources) out = presentation(a, module_list_v1(a->work.arena, &result), families, sources);
done:
    free(li); free(ri); free(joined); module_clear_v1(&l); module_clear_v1(&r); module_clear_v1(&result); return out;
}

static Atom *catalog_lookup(ModuleAssemblyV1 *a, Atom *location) {
    Atom *catalog = a->catalog, *out = NULL;
    while (module_form_v1(catalog, "LCons", 2u)) {
        if (!module_spend_v1(&a->work)) return NULL;
        Atom *row = catalog->expr.elems[1];
        if (!module_form_v1(row, "ModuleCatalogEntryV1", 2u)) { module_fail_v1(&a->work, "malformed-catalog", "Expected explicitly located module values."); return NULL; }
        if (atom_eq(row->expr.elems[1], location)) {
            if (out) { module_fail_v1(&a->work, "catalog-conflict", "A catalog location must resolve uniquely."); return NULL; }
            out = row->expr.elems[2];
        }
        catalog = catalog->expr.elems[2];
    }
    if (!out) module_fail_v1(&a->work, "missing-import", "An import must resolve in the explicitly supplied catalog.");
    return out;
}

static Atom *resolve(ModuleAssemblyV1 *a, Atom *module, Atom *path, Atom **owner) {
    if (!module_spend_v1(&a->work) || a->work.depth >= 256u) { module_fail_v1(&a->work, "incomplete", "Import resolution depth exhausted."); return NULL; }
    if (!module_form_v1(module, "ModuleValueV1", 7u) || !module_string_v1(module->expr.elems[1]) ||
        !module_string_v1(module->expr.elems[2]) || !module_string_v1(path)) {
        module_fail_v1(&a->work, "malformed-theory-path", "Expected a module value and a theory path."); return NULL;
    }
    const char *name = path->ground.sval, *dot = strchr(name, '.'); Atom *selected = module;
    if (dot) {
        size_t length = (size_t)(dot - name);
        if (strlen(module->expr.elems[2]->ground.sval) != length || strncmp(module->expr.elems[2]->ground.sval, name, length)) {
            Atom *location = NULL, *imports = module->expr.elems[3];
            while (module_form_v1(imports, "LCons", 2u)) {
                if (!module_spend_v1(&a->work)) return NULL;
                Atom *row = imports->expr.elems[1];
                if (module_form_v1(row, "ModuleImportAsV1", 2u) && module_string_v1(row->expr.elems[2]) &&
                    strlen(row->expr.elems[2]->ground.sval) == length && !strncmp(row->expr.elems[2]->ground.sval, name, length)) {
                    if (location) { module_fail_v1(&a->work, "import-conflict", "An import alias must resolve uniquely."); return NULL; }
                    location = row->expr.elems[1];
                }
                imports = imports->expr.elems[2];
            }
            selected = location ? catalog_lookup(a, location) : NULL;
            if (!selected) { module_fail_v1(&a->work, "missing-import", "A qualified theory requires an explicit import alias."); return NULL; }
        }
        name = dot + 1;
    }
    if (!module_form_v1(selected, "ModuleValueV1", 7u)) { module_fail_v1(&a->work, "malformed-catalog", "Catalog entries must contain module values."); return NULL; }
    Atom *list = selected->expr.elems[4], *theory = NULL;
    while (module_form_v1(list, "LCons", 2u)) {
        if (!module_spend_v1(&a->work)) return NULL;
        Atom *row = list->expr.elems[1];
        if (!module_form_v1(row, "ModuleTheoryV1", 3u) || !module_string_v1(row->expr.elems[1])) break;
        if (!strcmp(row->expr.elems[1]->ground.sval, name)) {
            if (theory) { module_fail_v1(&a->work, "duplicate-theory", "A theory path must resolve uniquely."); return NULL; }
            theory = row;
        }
        list = list->expr.elems[2];
    }
    if (!atom_is_symbol(list, "LNil")) { module_fail_v1(&a->work, "malformed-theories", "Expected finite named theory declarations."); return NULL; }
    if (!theory && !dot) {
        Atom *imports = module->expr.elems[3];
        while (module_form_v1(imports, "LCons", 2u)) {
            if (!module_spend_v1(&a->work)) return NULL;
            Atom *row = imports->expr.elems[1];
            if (module_form_v1(row, "ModuleImportFromV1", 2u) && module_string_v1(row->expr.elems[1]) && !strcmp(row->expr.elems[1]->ground.sval, name)) {
                if (theory) { module_fail_v1(&a->work, "import-conflict", "An imported theory must resolve uniquely."); return NULL; }
                Atom *imported = catalog_lookup(a, row->expr.elems[2]);
                if (!imported) return NULL;
                ++a->work.depth; theory = resolve(a, imported, path, &selected); --a->work.depth;
                if (!theory) return NULL;
            }
            imports = imports->expr.elems[2];
        }
    }
    if (!theory) { module_fail_v1(&a->work, "missing-theory", "No theory declaration resolves at this path."); return NULL; }
    *owner = selected; return theory;
}

static Atom *assemble_builder(ModuleAssemblyV1 *, Atom *, Atom *, const ModuleBindingV1 *, Atom *, const ModuleCallV1 *);
static Atom *replace(ModuleAssemblyV1 *, Atom *, Atom *);
static Atom *add_exports(ModuleAssemblyV1 *, Atom *, Atom *, Atom *);

static Atom *apply(ModuleAssemblyV1 *a, Atom *module, Atom *path, Atom *arguments, bool free_call, const ModuleCallV1 *stack) {
    Atom *owner = NULL, *theory = resolve(a, module, path, &owner), *out = NULL;
    ModuleValuesV1 formals = {0}, actuals = {0}; ModuleBindingV1 *bindings = NULL;
    if (!theory) return NULL;
    for (const ModuleCallV1 *c = stack; c; c = c->parent)
        if (atom_eq(c->module->expr.elems[1], owner->expr.elems[1]) && atom_eq(c->theory->expr.elems[1], theory->expr.elems[1])) {
            module_fail_v1(&a->work, "cyclic-theory-application", "Finite theory assembly cannot unfold a recursive application."); return NULL;
        }
    if (!module_unlist_v1(&a->work, theory->expr.elems[2], &formals) || (!free_call && !module_unlist_v1(&a->work, arguments, &actuals))) goto done;
    if (!free_call && formals.len != actuals.len) { module_fail_v1(&a->work, "argument-count", "Theory argument count differs from its parameter list."); goto done; }
    bindings = calloc(formals.len ? formals.len : 1u, sizeof(*bindings));
    if (!bindings) { module_fail_v1(&a->work, "allocation-failure", "Theory binding allocation failed."); goto done; }
    ModuleCallV1 call = {owner, theory, stack};
    for (size_t i = 0; i < formals.len; ++i) {
        Atom *formal = formals.items[i];
        if (!module_form_v1(formal, "ModuleParameterV1", 2u) || !module_string_v1(formal->expr.elems[1])) { module_fail_v1(&a->work, "malformed-parameter", "Expected a named theory parameter."); goto done; }
        for (size_t j = 0; j < i; ++j) if (atom_eq(bindings[j].name, formal->expr.elems[1])) { module_fail_v1(&a->work, "duplicate-parameter", "Theory parameters must have distinct names."); goto done; }
        Atom *actual = free_call ? apply(a, owner, formal->expr.elems[2], NULL, true, &call) : actuals.items[i];
        ModuleValuesV1 checked = {0}; bool valid = actual && entries(a, actual, &checked); module_clear_v1(&checked);
        if (!valid) goto done;
        if (!free_call) {
            /* Interface construction has a private invocation counter: checking
             * must not change the origins of subsequent authored instances.
             * Identity inclusion is one partial method, not a completeness or
             * conservativity claim. Unknown leaves an interpretation obligation;
             * it never proves that every possible interpretation is invalid. */
            ModuleAssemblyV1 probe = *a;
            probe.invocation = 0u;
            Atom *required = apply(&probe, owner, formal->expr.elems[2], NULL, true, &call);
            a->work.remaining = probe.work.remaining;
            Atom *method = atom_symbol(a->work.arena, "IdentityInclusionV1");
            Atom *interpretation = NULL;
            if (required) interpretation = module_interpret_v1(&a->work, required, actual, method);
            else {
                Atom *fields[] = {method, probe.work.refusal};
                interpretation = module_make_v1(a->work.arena, "ModuleInterpretationUnknownV1", fields, 2u);
            }
            if (!module_form_v1(interpretation, "ModuleInterpretationFoundV1", 4u)) {
                if (interpretation) {
                    Atom *fields[] = {formal->expr.elems[1], formal->expr.elems[2], interpretation};
                    a->work.refusal = module_make_v1(a->work.arena, "ModuleParameterUnknownV1", fields, 3u);
                }
                goto done;
            }
        }
        bindings[i] = (ModuleBindingV1){formal->expr.elems[1], actual, i ? &bindings[i - 1u] : NULL};
    }
    size_t root_len = strlen(a->root->ground.sval), source_len = strlen(owner->expr.elems[1]->ground.sval);
    if (root_len > SIZE_MAX - source_len - 96u || a->invocation == UINT32_MAX) { module_fail_v1(&a->work, "incomplete", "Invocation namespace exhausted."); goto done; }
    size_t capacity = root_len + source_len + 96u;
    char *namespace_value = arena_alloc(a->work.arena, capacity);
    if (!namespace_value) goto done;
    snprintf(namespace_value, capacity, "module:%zu:%s:%u:%zu:%s", root_len, a->root->ground.sval, a->invocation++, source_len, owner->expr.elems[1]->ground.sval);
    out = assemble_builder(a, owner, theory->expr.elems[3], formals.len ? &bindings[formals.len - 1u] : NULL, atom_string(a->work.arena, namespace_value), &call);
    if (out) {
        Atom *family_fields[] = {owner->expr.elems[1], theory->expr.elems[1]};
        Atom *family = module_make_v1(a->work.arena, "ModuleTheoryIdentityV1", family_fields, 2u);
        ModuleValuesV1 families = {0}, sources = {0};
        if (!module_unlist_v1(&a->work, out->expr.elems[4], &families) || !append_unique(&a->work, &families, family) || !module_unlist_v1(&a->work, out->expr.elems[5], &sources)) out = NULL;
        Atom *source_fields[] = {atom_string(a->work.arena, namespace_value), owner->expr.elems[1], owner->expr.elems[7]};
        if (out && append_unique(&a->work, &sources, module_make_v1(a->work.arena, "ModuleSourceMapV1", source_fields, 3u)))
            out = presentation(a, out->expr.elems[3], module_list_v1(a->work.arena, &families), module_list_v1(a->work.arena, &sources));
        else out = NULL;
        module_clear_v1(&families); module_clear_v1(&sources);
    }
done:
    free(bindings); module_clear_v1(&formals); module_clear_v1(&actuals); return out;
}

typedef struct { Atom *old_label, *new_label, *permutation; } ModuleRenameV1;

/* Structural transport changes a declaration's view, never its origin. */
static Atom *transport(ModuleAssemblyV1 *a, Atom *value, const ModuleRenameV1 *maps,
    size_t count, Atom *old_type, Atom *new_type) {
    if (!module_spend_v1(&a->work) || a->work.depth >= 512u) { module_fail_v1(&a->work, "incomplete", "Replacement depth exhausted."); return NULL; }
    if (old_type && atom_eq(value, old_type)) return new_type;
    if (!value || value->kind != ATOM_EXPR) return value;
    ++a->work.depth; Atom *out = NULL;
    if (module_form_v1(value, "PApp", 2u)) {
        ModuleValuesV1 args = {0}, mapped = {0}, permutation = {0};
        if (!module_unlist_v1(&a->work, value->expr.elems[2], &args)) goto pattern_done;
        Atom *head = value->expr.elems[1]; const ModuleRenameV1 *map = NULL;
        for (size_t i = 0; i < count; ++i) if (atom_eq(head, maps[i].old_label)) { map = &maps[i]; head = map->new_label; break; }
        if (map && !module_unlist_v1(&a->work, map->permutation, &permutation)) goto pattern_done;
        if (map && permutation.len != args.len) { module_fail_v1(&a->work, "replacement-arity", "A law disagrees with the replaced constructor's arity."); goto pattern_done; }
        for (size_t i = 0; i < args.len; ++i) {
            size_t j = map ? (size_t)permutation.items[i]->ground.ival : i;
            if (j >= args.len || !module_push_v1(&a->work, &mapped, transport(a, args.items[j], maps, count, old_type, new_type))) goto pattern_done;
        }
        Atom *fields[] = {head, module_list_v1(a->work.arena, &mapped)};
        out = module_make_v1(a->work.arena, "PApp", fields, 2u);
pattern_done:
        module_clear_v1(&args); module_clear_v1(&mapped); module_clear_v1(&permutation);
    } else {
        Atom **children = arena_alloc(a->work.arena, value->expr.len * sizeof(*children));
        if (children) {
            size_t i = 0;
            for (; i < value->expr.len; ++i) if (!(children[i] = transport(a, value->expr.elems[i], maps, count, old_type, new_type))) break;
            if (i == value->expr.len) out = atom_expr(a->work.arena, children, i);
        }
    }
    --a->work.depth; return out;
}

static bool argument_profiles(ModuleAssemblyV1 *a, Atom *body, ModuleValuesV1 *profiles) {
    ModuleValuesV1 items = {0}; bool ok = false;
    if (!module_form_v1(body, "ModuleTermV1", 4u) || !module_unlist_v1(&a->work, body->expr.elems[3], &items)) goto done;
    for (size_t i = 0; i < items.len; ++i) {
        Atom *item = items.items[i];
        if (module_form_v1(item, "LbnfTerminalV1", 1u)) continue;
        if (module_form_v1(item, "ModuleNonTerminalV1", 1u)) {
            if (!module_push_v1(&a->work, profiles, item)) goto done;
        } else if (module_form_v1(item, "ModuleBindV1", 2u)) {
            if (!module_string_v1(item->expr.elems[1]) || !module_push_v1(&a->work, profiles, item)) goto done;
        } else if (module_form_v1(item, "ModuleAbstractionV1", 2u)) {
            Atom *nested = item;
            while (module_form_v1(nested, "ModuleAbstractionV1", 2u)) {
                if (!module_spend_v1(&a->work) || !module_string_v1(nested->expr.elems[1])) goto done;
                nested = nested->expr.elems[2];
            }
            if (!module_form_v1(nested, "ModuleNonTerminalV1", 1u) && !module_form_v1(nested, "ModuleBindV1", 2u)) goto done;
            if (!module_push_v1(&a->work, profiles, item)) goto done;
        } else goto done;
    }
    ok = true;
done:
    module_clear_v1(&items);
    if (!ok) module_fail_v1(&a->work, "malformed-notation", "Expected constructor notation with explicit argument categories.");
    return ok;
}

static Atom *replace(ModuleAssemblyV1 *a, Atom *base, Atom *replacements) {
    ModuleValuesV1 rows = {0}, requests = {0}, changed = {0};
    ModuleRenameV1 *maps = NULL; Atom **bodies = NULL; Atom *out = NULL;
    if (!entries(a, base, &rows) || !module_unlist_v1(&a->work, replacements, &requests)) goto done;
    maps = calloc(requests.len ? requests.len : 1u, sizeof(*maps)); bodies = calloc(rows.len ? rows.len : 1u, sizeof(*bodies));
    if (!maps || !bodies) { module_fail_v1(&a->work, "allocation-failure", "Replacement allocation failed."); goto done; }
    for (size_t i = 0; i < requests.len; ++i) {
        Atom *request = requests.items[i]; ModuleValuesV1 perm = {0}, from = {0}, to = {0}; bool valid = false;
        if (!module_form_v1(request, "ModuleReplacementV1", 4u) || !module_form_v1(request->expr.elems[4], "ModuleDeclarationV1", 2u)) goto request_done;
        Atom *replacement = request->expr.elems[4]->expr.elems[2];
        if (!module_form_v1(replacement, "ModuleTermV1", 4u) || !atom_eq(replacement->expr.elems[2], request->expr.elems[3])) goto request_done;
        for (size_t j = 0; j < i; ++j) if (atom_eq(maps[j].old_label, request->expr.elems[2])) goto request_done;
        size_t matches = 0u;
        for (size_t j = 0; j < rows.len; ++j) {
            Atom *old = rows.items[j]->expr.elems[3];
            if (!module_form_v1(old, "ModuleTermV1", 4u) || !atom_eq(old->expr.elems[1], request->expr.elems[2]) || !atom_eq(old->expr.elems[2], request->expr.elems[3])) continue;
            ++matches;
            if (!argument_profiles(a, old, &from) || !argument_profiles(a, replacement, &to) ||
                !module_unlist_v1(&a->work, request->expr.elems[1], &perm) || perm.len != from.len || perm.len != to.len) goto request_done;
            for (size_t k = 0; k < perm.len; ++k) {
                Atom *p = perm.items[k];
                if (!p || p->kind != ATOM_GROUNDED || p->ground.gkind != GV_INT || p->ground.ival < 0 || (uint64_t)p->ground.ival >= perm.len) goto request_done;
                for (size_t q = 0; q < k; ++q) if (atom_eq(perm.items[q], p)) goto request_done;
                Atom *old_profile = from.items[(size_t)p->ground.ival];
                if (!atom_eq(to.items[k], old_profile)) {
                    if (!module_form_v1(to.items[k], "ModuleNonTerminalV1", 1u) ||
                        !module_form_v1(old_profile, "ModuleNonTerminalV1", 1u)) {
                        /* Exact binding profiles suffice for structural transport.
                         * Binder renaming can be valid, but this method has not
                         * established its scope correspondence. Never erase the
                         * binder just because the final category is unchanged. */
                        Atom *obligation[] = {request, old_profile, to.items[k]};
                        Atom *unknown[] = {atom_symbol(a->work.arena, "BindingShapeV1"),
                            module_make_v1(a->work.arena, "ModuleReplacementObligationV1", obligation, 3u)};
                        a->work.refusal = module_make_v1(a->work.arena, "ModuleInterpretationUnknownV1", unknown, 2u);
                    }
                    goto request_done;
                }
            }
            bodies[j] = replacement;
            module_clear_v1(&perm); module_clear_v1(&from); module_clear_v1(&to);
        }
        if (!matches) goto request_done;
        maps[i] = (ModuleRenameV1){request->expr.elems[2], replacement->expr.elems[1], request->expr.elems[1]}; valid = true;
request_done:
        module_clear_v1(&perm); module_clear_v1(&from); module_clear_v1(&to);
        if (!valid) { module_fail_v1(&a->work, "invalid-replacement", "Replacement requires an existing constructor, compatible categories, and a bijective argument permutation."); goto done; }
    }
    for (size_t i = 0; i < requests.len; ++i) {
        for (size_t j = 0; j < i; ++j) if (atom_eq(maps[i].new_label, maps[j].new_label)) {
            module_fail_v1(&a->work, "replacement-capture", "Different constructors cannot be replaced by the same exposed name."); goto done;
        }
        for (size_t j = 0; j < rows.len; ++j) {
            Atom *body = rows.items[j]->expr.elems[3];
            if (!bodies[j] && module_form_v1(body, "ModuleTermV1", 4u) && atom_eq(body->expr.elems[1], maps[i].new_label)) {
                module_fail_v1(&a->work, "replacement-capture", "A replacement cannot capture an unaffected constructor name."); goto done;
            }
        }
    }
    for (size_t i = 0; i < rows.len; ++i) {
        Atom *row = rows.items[i], *body = bodies[i] ? bodies[i] : transport(a, row->expr.elems[3], maps, requests.len, NULL, NULL);
        if (!body) goto done;
        Atom *site = module_form_v1(row->expr.elems[2], "ModuleTermKeyV1", 3u) ? row->expr.elems[2]->expr.elems[3] : row->expr.elems[1]->expr.elems[2];
        Atom *fields[] = {row->expr.elems[1], entry_key(a, body, site), body};
        if (!module_push_v1(&a->work, &changed, module_make_v1(a->work.arena, "ModuleEntryV1", fields, 3u))) goto done;
    }
    Atom *nil = atom_symbol(a->work.arena, "LNil");
    Atom *candidate = presentation(a, module_list_v1(a->work.arena, &changed), base->expr.elems[4], base->expr.elems[5]);
    out = candidate ? join(a, presentation(a, nil, nil, nil), candidate, 0u) : NULL;
done:
    free(maps); free(bodies); module_clear_v1(&rows); module_clear_v1(&requests); module_clear_v1(&changed); return out;
}

static Atom *add_exports(ModuleAssemblyV1 *a, Atom *base, Atom *exports, Atom *invocation) {
    ModuleValuesV1 rows = {0}, requests = {0}; Atom *out = base;
    if (!module_unlist_v1(&a->work, exports, &requests)) { out = NULL; goto done; }
    for (size_t i = 0; i < requests.len; ++i) {
        Atom *request = requests.items[i], *declared_site = NULL;
        if (module_form_v1(request, "ModuleDeclarationV1", 2u)) {
            declared_site = request->expr.elems[1]; request = request->expr.elems[2];
        }
        if (!module_form_v1(request, "ModuleExportV1", 2u)) { out = NULL; break; }
        if (atom_eq(request->expr.elems[1], request->expr.elems[2])) {
            Atom *type = request->expr.elems[1], *body = request;
            char site[64];
            if (declared_site && declared_site->kind == ATOM_GROUNDED && declared_site->ground.gkind == GV_INT)
                snprintf(site, sizeof(site), "declaration:%lld", (long long)declared_site->ground.ival);
            else snprintf(site, sizeof(site), "constructed-export:%u", a->invocation++);
            Atom *origin_fields[] = {invocation, atom_string(a->work.arena, site)};
            Atom *fields[] = {module_make_v1(a->work.arena, "SyntaxOriginV1", origin_fields, 2u), entry_key(a, body, type), body};
            Atom *entry = module_make_v1(a->work.arena, "ModuleEntryV1", fields, 3u), *nil = atom_symbol(a->work.arena, "LNil");
            ModuleValuesV1 one = {.items = &entry, .len = 1u};
            Atom *right = presentation(a, module_list_v1(a->work.arena, &one), nil, nil);
            out = right ? join(a, out, right, 0u) : NULL;
        } else {
            if (!entries(a, out, &rows)) { out = NULL; break; }
            bool found = false, valid = true;
            for (size_t j = 0; j < rows.len; ++j) {
                Atom *row = rows.items[j], *body = row->expr.elems[3];
                if (module_form_v1(body, "ModuleExportV1", 2u) && atom_eq(body->expr.elems[2], request->expr.elems[1])) found = true;
                body = transport(a, body, NULL, 0u, request->expr.elems[1], request->expr.elems[2]);
                if (!body) { valid = false; break; }
                Atom *site = module_form_v1(row->expr.elems[2], "ModuleTermKeyV1", 3u) ? row->expr.elems[2]->expr.elems[3] : row->expr.elems[1]->expr.elems[2];
                Atom *fields[] = {row->expr.elems[1], entry_key(a, body, site), body};
                rows.items[j] = module_make_v1(a->work.arena, "ModuleEntryV1", fields, 3u);
                if (!rows.items[j]) { valid = false; break; }
            }
            if (!found) { module_fail_v1(&a->work, "missing-export", "Renaming requires an existing exported category."); out = NULL; }
            else if (valid) out = presentation(a, module_list_v1(a->work.arena, &rows), out->expr.elems[4], out->expr.elems[5]);
            else out = NULL;
            module_clear_v1(&rows);
        }
        if (!out) break;
    }
done:
    module_clear_v1(&rows); module_clear_v1(&requests); return out;
}

static Atom *assemble_builder(ModuleAssemblyV1 *a, Atom *module, Atom *builder,
    const ModuleBindingV1 *env, Atom *invocation, const ModuleCallV1 *stack) {
    if (!module_spend_v1(&a->work) || a->work.depth >= 256u) { module_fail_v1(&a->work, "incomplete", "Finite theory assembly depth exhausted."); return NULL; }
    ++a->work.depth; Atom *out = NULL, *nil = atom_symbol(a->work.arena, "LNil");
    if (atom_is_symbol(builder, "ModuleEmptyV1")) out = presentation(a, nil, nil, nil);
    else if (module_form_v1(builder, "ModuleRefV1", 1u)) {
        for (const ModuleBindingV1 *b = env; b; b = b->parent) if (atom_eq(b->name, builder->expr.elems[1])) { out = b->value; break; }
        if (!out) module_fail_v1(&a->work, "unbound-theory-reference", "A local theory reference requires a parameter or let binding.");
    } else if (module_form_v1(builder, "ModuleLetV1", 3u)) {
        if (!module_string_v1(builder->expr.elems[1])) goto done;
        Atom *bound = assemble_builder(a, module, builder->expr.elems[2], env, invocation, stack);
        if (bound) { ModuleBindingV1 binding = {builder->expr.elems[1], bound, env}; out = assemble_builder(a, module, builder->expr.elems[3], &binding, invocation, stack); }
    } else if (module_form_v1(builder, "ModuleFreeV1", 1u)) out = apply(a, module, builder->expr.elems[1], nil, true, stack);
    else if (module_form_v1(builder, "ModuleApplyV1", 2u)) {
        ModuleValuesV1 args = {0}, actuals = {0};
        if (module_unlist_v1(&a->work, builder->expr.elems[2], &args)) {
            size_t i = 0;
            for (; i < args.len; ++i) if (!module_push_v1(&a->work, &actuals, assemble_builder(a, module, args.items[i], env, invocation, stack))) break;
            if (i == args.len) out = apply(a, module, builder->expr.elems[1], module_list_v1(a->work.arena, &actuals), false, stack);
        }
        module_clear_v1(&args); module_clear_v1(&actuals);
    } else if (module_form_v1(builder, "ModuleJoinV1", 2u) || module_form_v1(builder, "ModuleMeetV1", 2u) || module_form_v1(builder, "ModuleDifferenceV1", 2u)) {
        Atom *left = assemble_builder(a, module, builder->expr.elems[1], env, invocation, stack);
        Atom *right = left ? assemble_builder(a, module, builder->expr.elems[2], env, invocation, stack) : NULL;
        if (right) out = join(a, left, right, atom_is_symbol(builder->expr.elems[0], "ModuleMeetV1") ? 1u : atom_is_symbol(builder->expr.elems[0], "ModuleDifferenceV1") ? 2u : 0u);
    } else if (module_form_v1(builder, "ModuleReplaceV1", 2u)) {
        Atom *base = assemble_builder(a, module, builder->expr.elems[1], env, invocation, stack);
        if (base) out = replace(a, base, builder->expr.elems[2]);
    } else if (module_form_v1(builder, "ModuleAddExportsV1", 2u)) {
        Atom *base = assemble_builder(a, module, builder->expr.elems[1], env, invocation, stack);
        if (base) out = add_exports(a, base, builder->expr.elems[2], invocation);
    } else if (module_form_v1(builder, "ModuleAddTermsV1", 2u) || module_form_v1(builder, "ModuleAddEquationsV1", 2u) || module_form_v1(builder, "ModuleAddRewritesV1", 2u)) {
        Atom *base = assemble_builder(a, module, builder->expr.elems[1], env, invocation, stack);
        ModuleValuesV1 declarations = {0}, additions = {0};
        if (base && module_unlist_v1(&a->work, builder->expr.elems[2], &declarations)) {
            size_t i = 0;
            for (; i < declarations.len; ++i) {
                Atom *decl = declarations.items[i];
                if (!module_form_v1(decl, "ModuleDeclarationV1", 2u) || decl->expr.elems[1]->kind != ATOM_GROUNDED || decl->expr.elems[1]->ground.gkind != GV_INT || decl->expr.elems[1]->ground.ival < 0) break;
                char site[64]; snprintf(site, sizeof(site), "declaration:%lld", (long long)decl->expr.elems[1]->ground.ival);
                Atom *origin_fields[] = {invocation, atom_string(a->work.arena, site)};
                Atom *fields[] = {module_make_v1(a->work.arena, "SyntaxOriginV1", origin_fields, 2u), entry_key(a, decl->expr.elems[2], decl->expr.elems[1]), decl->expr.elems[2]};
                if (!module_push_v1(&a->work, &additions, module_make_v1(a->work.arena, "ModuleEntryV1", fields, 3u))) break;
            }
            if (i == declarations.len) out = join(a, base, presentation(a, module_list_v1(a->work.arena, &additions), nil, nil), 0u);
        }
        module_clear_v1(&declarations); module_clear_v1(&additions);
    }
done:
    --a->work.depth;
    if (!out) module_fail_v1(&a->work, "malformed-builder", "Expected an admitted finite module builder.");
    return out;
}

bool cetta_module_value_v1_assemble(Arena *arena, Atom *module, Atom *path, Atom *arguments,
    Atom *catalog, CettaGsltOriginPolicyV1 policy, Atom *instance_key, uint32_t limit, Atom **out, Atom **refusal) {
    if (!arena || !out || !refusal) return false;
    ModuleAssemblyV1 a = {.work = {.arena = arena, .remaining = limit}, .root = instance_key, .catalog = catalog, .policy = policy};
    Atom *result = NULL; ModuleValuesV1 catalog_rows = {0};
    if (module_string_v1(instance_key) && (policy == CETTA_GSLT_ORIGIN_COHERENT_V1 || policy == CETTA_GSLT_ORIGIN_VIEW_V1) &&
        module_unlist_v1(&a.work, catalog, &catalog_rows)) result = apply(&a, module, path, arguments, false, NULL);
    module_clear_v1(&catalog_rows);
    if (!result) { module_fail_v1(&a.work, "malformed-assembly", "Expected a module, explicit catalog, policy and portable instance key."); *refusal = atom_deep_copy(arena, a.work.refusal); return false; }
    Atom *owned = atom_deep_copy(arena, result);
    if (!owned) { module_fail_v1(&a.work, "allocation-failure", "Presentation ownership allocation failed."); *refusal = atom_deep_copy(arena, a.work.refusal); return false; }
    *out = owned; return true;
}

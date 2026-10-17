#include "module_value_internal_v1.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    ModuleWorkV1 work;
    ModuleValuesV1 entries, definitions, definition_origins, definition_sources, equations, rewrites;
    ModuleValuesV1 terms, types;
    ModuleValuesV1 bound, captures;
    bool law_right;
} ModuleLanguageV1;

static Atom *term_label(Atom *value) {
    if (module_string_v1(value)) return value;
    return module_form_v1(value, "ModuleListLabelV1", 2u) && module_string_v1(value->expr.elems[1]) ? value->expr.elems[1] : NULL;
}

/* Concrete parsing retains the spelling of binders as ordinary source
 * fields. The semantic signature below removes those spelling fields and
 * wraps their scoped bodies; it is never used as the raw parser signature. */
static Atom *notation_leaf(ModuleLanguageV1 *l, Atom *item) {
    while (module_form_v1(item, "ModuleAbstractionV1", 2u)) {
        if (!module_spend_v1(&l->work) || !module_string_v1(item->expr.elems[1])) return NULL;
        item = item->expr.elems[2];
    }
    if (module_form_v1(item, "ModuleNonTerminalV1", 1u)) return item->expr.elems[1];
    if (module_form_v1(item, "ModuleBindV1", 2u) && module_string_v1(item->expr.elems[1])) return item->expr.elems[2];
    return NULL;
}

static Atom *term_definition(ModuleLanguageV1 *l, Atom *body) {
    ModuleValuesV1 items = {0}, lowered = {0}; Atom *out = NULL;
    Atom *head = term_label(body->expr.elems[1]);
    Atom *category = module_category_name_v1(&l->work, body->expr.elems[2]);
    if (!head || !category) {
        module_fail_v1(&l->work, "unresolved-category-elaboration", "This category requires a resolved core elaboration; its source declaration remains intact."); goto done;
    }
    if (!module_unlist_v1(&l->work, body->expr.elems[3], &items)) goto done;
    for (size_t i = 0; i < items.len; ++i) {
        Atom *item = items.items[i], *value = NULL;
        if (module_form_v1(item, "LbnfTerminalV1", 1u)) value = item;
        else {
            Atom *leaf = notation_leaf(l, item);
            Atom *name = leaf ? module_category_name_v1(&l->work, leaf) : NULL;
            if (name) value = module_make_v1(l->work.arena, "LbnfNonTerminalV1", &name, 1u);
            else {
                module_fail_v1(&l->work, "unresolved-binding-elaboration", "This notation needs a resolved category and binding interpretation."); goto done;
            }
        }
        if (!module_push_v1(&l->work, &lowered, value)) goto done;
    }
    if (module_form_v1(body->expr.elems[2], "ModuleProductCategoryV1", 1u)) {
        for (size_t i = 0u; i < items.len; ++i) {
            if (!module_form_v1(items.items[i], "LbnfTerminalV1", 1u) &&
                !module_form_v1(items.items[i], "ModuleNonTerminalV1", 1u)) {
                module_fail_v1(&l->work, "unresolved-binding-elaboration", "Binding tuple fields require a scope-aware product elaboration."); goto done;
            }
        }
        ModuleValuesV1 components = {0}; bool compatible = true; size_t position = 0u;
        if (!module_unlist_v1(&l->work, body->expr.elems[2]->expr.elems[1], &components)) {
            module_clear_v1(&components); goto done;
        }
        for (size_t i = 0; i < lowered.len; ++i) {
            Atom *item = lowered.items[i];
            if (module_form_v1(item, "LbnfTerminalV1", 1u)) continue;
            Atom *component = position < components.len ?
                module_category_name_v1(&l->work, components.items[position++]) : NULL;
            if (!component || !atom_eq(item->expr.elems[1], component)) {
                compatible = false; break;
            }
        }
        compatible = compatible && position == components.len;
        module_clear_v1(&components);
        if (!compatible) {
            module_fail_v1(&l->work, "tuple-signature-conflict", "A tuple constructor must have exactly the declared component sorts in order."); goto done;
        }
        for (size_t i = 0; i < l->entries.len; ++i) {
            Atom *other = l->entries.items[i];
            if (!module_form_v1(other, "ModuleEntryV1", 3u)) continue;
            other = other->expr.elems[3];
            Atom *other_head = module_form_v1(other, "ModuleTermV1", 4u) ?
                term_label(other->expr.elems[1]) : NULL;
            if (other_head && atom_eq(other_head, head) &&
                !atom_eq(other->expr.elems[2], body->expr.elems[2])) {
                module_fail_v1(&l->work, "tuple-constructor-conflict", "A derived tuple constructor must not reuse a head from another source category."); goto done;
            }
        }
    }
    Atom *fields[] = {head, category, module_list_v1(l->work.arena, &lowered), body->expr.elems[4]};
    out = module_make_v1(l->work.arena, "LbnfRuleV1", fields, 4u);
done:
    module_clear_v1(&items); module_clear_v1(&lowered); return out;
}

static Atom *scope_rule(ModuleLanguageV1 *l, Atom *source, Atom *flat) {
    ModuleValuesV1 items = {0}, parameters = {0}, scoped = {0}, syntax = {0}, renamings = {0};
    Atom *out = NULL;
    if (!module_unlist_v1(&l->work, source->expr.elems[3], &items) ||
        !module_unlist_v1(&l->work, flat->expr.elems[3], &parameters) ||
        !module_unlist_v1(&l->work, flat->expr.elems[4], &syntax)) goto done;
    size_t position = 0u;
    for (size_t i = 0u; i < items.len; ++i) {
        Atom *item = items.items[i];
        if (module_form_v1(item, "LbnfTerminalV1", 1u)) continue;
        if (position >= parameters.len) goto done;
        Atom *parameter = parameters.items[position++];
        if (module_form_v1(item, "ModuleBindV1", 2u)) {
            size_t uses = 0u;
            for (size_t j = 0u; j < items.len; ++j) {
                if (!module_spend_v1(&l->work)) goto done;
                Atom *other = items.items[j];
                if (j != i && module_form_v1(other, "ModuleBindV1", 2u) &&
                    atom_eq(item->expr.elems[1], other->expr.elems[1])) {
                    module_fail_v1(&l->work, "unresolved-binding-elaboration", "A binder name selects several source slots; an explicit scope correspondence is needed."); goto done;
                }
                if (module_form_v1(other, "ModuleAbstractionV1", 2u) && atom_eq(item->expr.elems[1], other->expr.elems[1])) ++uses;
            }
            if (!uses) {
                /* This method recognizes named bodies, not every possible
                 * interpretation of a spelling slot in a source dialect. */
                module_fail_v1(&l->work, "unresolved-binding-elaboration", "A spelling-only binder has no identified body in this scope interpretation."); goto done;
            }
            Atom *fields[] = {parameter->expr.elems[1], item->expr.elems[1]};
            if (!module_push_v1(&l->work, &renamings, module_make_v1(l->work.arena, "ModuleBinderSlotV1", fields, 2u))) goto done;
            continue;
        }
        if (module_form_v1(item, "ModuleAbstractionV1", 2u)) {
            /* This is the single-binder clause of the existing core scope
             * representation. More elaborate source scopes remain data and
             * require another elaboration, not a changed LanguageDef core. */
            if (!module_form_v1(item->expr.elems[2], "ModuleNonTerminalV1", 1u)) {
                module_fail_v1(&l->work, "unresolved-binding-elaboration", "Nested source abstractions require their scope correspondence."); goto done;
            }
            Atom *domain = NULL;
            for (size_t j = 0u; j < items.len; ++j) {
                if (!module_spend_v1(&l->work)) goto done;
                Atom *binder = items.items[j];
                if (!module_form_v1(binder, "ModuleBindV1", 2u) || !atom_eq(binder->expr.elems[1], item->expr.elems[1])) continue;
                Atom *category = module_category_name_v1(&l->work, binder->expr.elems[2]);
                if (category) domain = module_make_v1(l->work.arena, "TBase", &category, 1u);
            }
            if (!domain) {
                module_fail_v1(&l->work, "unresolved-binding-elaboration", "The abstraction does not identify a declared binder domain."); goto done;
            }
            Atom *arrow[] = {domain, parameter->expr.elems[2]};
            Atom *fields[] = {module_make_v1(l->work.arena, "BSome", &item->expr.elems[1], 1u),
                parameter->expr.elems[1], module_make_v1(l->work.arena, "TArrow", arrow, 2u)};
            parameter = module_make_v1(l->work.arena, "TermAbstractionNamed", fields, 3u);
        }
        if (!module_push_v1(&l->work, &scoped, parameter)) goto done;
    }
    if (position != parameters.len) goto done;
    for (size_t i = 0u; i < syntax.len; ++i) {
        if (!module_form_v1(syntax.items[i], "SyntaxNonTerminal", 1u)) continue;
        for (size_t j = 0u; j < renamings.len; ++j) {
            if (!module_spend_v1(&l->work)) goto done;
            Atom *renaming = renamings.items[j];
            if (atom_eq(syntax.items[i]->expr.elems[1], renaming->expr.elems[1])) {
                syntax.items[i] = module_make_v1(l->work.arena, "SyntaxNonTerminal", &renaming->expr.elems[2], 1u);
                break;
            }
        }
    }
    {
        Atom *fields[] = {flat->expr.elems[1], flat->expr.elems[2], module_list_v1(l->work.arena, &scoped),
            module_list_v1(l->work.arena, &syntax), flat->expr.elems[5]};
        out = module_make_v1(l->work.arena, "GrammarRule", fields, 5u);
    }
done:
    module_clear_v1(&items); module_clear_v1(&parameters); module_clear_v1(&scoped);
    module_clear_v1(&syntax); module_clear_v1(&renamings);
    return out;
}

static bool scope_declarations(ModuleLanguageV1 *l, Atom *sites) {
    ModuleValuesV1 rows = {0}; bool ok = false;
    if (!module_unlist_v1(&l->work, sites, &rows) || rows.len != l->terms.len) goto done;
    for (size_t i = 0u; i < rows.len; ++i) {
        Atom *site = rows.items[i];
        if (!site || site->kind != ATOM_GROUNDED || site->ground.gkind != GV_INT ||
            site->ground.ival < 0 || (uint64_t)site->ground.ival >= l->definition_sources.len) goto done;
        size_t j = (size_t)site->ground.ival;
        Atom *source = l->definition_sources.items[j];
        l->work.site = l->definition_origins.items[j];
        if (!module_form_v1(source, "ModuleTermV1", 4u)) continue;
        Atom *scoped = scope_rule(l, source, l->terms.items[i]);
        if (!scoped) goto done;
        l->terms.items[i] = scoped;
    }
    ok = true;
done:
    module_clear_v1(&rows);
    if (!ok) module_fail_v1(&l->work, "unresolved-binding-elaboration", "The expanded declaration needs a source scope correspondence.");
    return ok;
}

static Atom *rule_for(ModuleLanguageV1 *l, Atom *label) {
    Atom *head = term_label(label), *out = NULL;
    if (!head) return NULL;
    for (size_t i = 0; i < l->terms.len; ++i) {
        Atom *rule = l->terms.items[i];
        if (atom_eq(rule->expr.elems[1], head)) {
            if (out && (!atom_eq(out->expr.elems[2], rule->expr.elems[2]) ||
                        !atom_eq(out->expr.elems[3], rule->expr.elems[3]))) {
                module_fail_v1(&l->work, "ambiguous-constructor", "A law must identify a unique constructor signature."); return NULL;
            }
            out = rule;
        }
    }
    return out;
}

static Atom *pattern_type(ModuleLanguageV1 *l, Atom *pattern, ModuleValuesV1 *bindings) {
    size_t substitutions = 0u;
    while (module_form_v1(pattern, "ModuleNamedSubstitutionV1", 3u)) {
        if (!module_spend_v1(&l->work) || ++substitutions > 512u) {
            module_fail_v1(&l->work, "incomplete", "Named substitution depth exhausted."); return NULL;
        }
        pattern = pattern->expr.elems[1];
    }
    if (module_form_v1(pattern, "PApp", 2u)) {
        Atom *rule = rule_for(l, pattern->expr.elems[1]);
        if (!rule && !l->work.refusal)
            module_fail_v1(&l->work, "dangling-law-constructor", "A law references an undeclared constructor.");
        return rule ? module_make_v1(l->work.arena, "TBase", &rule->expr.elems[2], 1u) : NULL;
    }
    if (module_form_v1(pattern, "FVar", 1u))
        for (size_t i = 0; i < bindings->len; ++i)
            if (atom_eq(bindings->items[i]->expr.elems[1], pattern->expr.elems[1])) return bindings->items[i]->expr.elems[2];
    return NULL;
}

static Atom *check_pattern(ModuleLanguageV1 *, Atom *, Atom *, ModuleValuesV1 *, bool);

/* This source interpretation recognizes retained outer binders and inserted
 * inner binders. Exact name/type suffix agreement is a sufficient witness,
 * not a completeness criterion: other renamings or dependency projections
 * need a supplied correspondence rather than being declared impossible. */
static bool law_capture(ModuleLanguageV1 *l, Atom *name, Atom *type) {
    for (size_t i = 0u; i < l->captures.len; ++i) {
        Atom *row = l->captures.items[i];
        if (!atom_eq(row->expr.elems[1], name)) continue;
        ModuleValuesV1 old = {0};
        if (!module_unlist_v1(&l->work, row->expr.elems[3], &old)) return false;
        bool same = old.len <= l->bound.len && (l->law_right || old.len == l->bound.len);
        /* Bound stacks are stored outermost first. An inner extension is a
         * prefix in the de Bruijn environment, hence a suffix here. */
        for (size_t j = 0u; same && j < old.len; ++j)
            same = atom_eq(old.items[j], l->bound.items[j]);
        module_clear_v1(&old);
        if (!same) return module_fail_v1(&l->work, "unresolved-scope-transport",
            "This metavariable needs a scope correspondence beyond retained outer binders and inner weakening.");
        return true;
    }
    Atom *fields[] = {name, type, module_list_v1(l->work.arena, &l->bound)};
    return module_push_v1(&l->work, &l->captures, module_make_v1(l->work.arena, "ModuleLawCaptureV1", fields, 3u));
}

static Atom *law_source_argument(ModuleLanguageV1 *l, const ModuleValuesV1 *syntax,
    const ModuleValuesV1 *args, Atom *name) {
    Atom *out = NULL;
    size_t position = 0u;
    for (size_t i = 0u; i < syntax->len; ++i) {
        Atom *item = syntax->items[i];
        if (module_form_v1(item, "SyntaxTerminal", 1u)) continue;
        if (!module_form_v1(item, "SyntaxNonTerminal", 1u) || position >= args->len) goto unresolved;
        if (atom_eq(item->expr.elems[1], name)) {
            if (out) goto unresolved;
            out = args->items[position];
        }
        ++position;
    }
    if (position == args->len && out) return out;
unresolved:
    module_fail_v1(&l->work, "unresolved-binding-pattern-elaboration",
        "The law needs an unambiguous correspondence between source fields and semantic arguments.");
    return NULL;
}

/* The constructor profile has no bound levels. Its name-directed action is
 * the depth-zero restriction of the core named-substitution elaboration. */
static Atom *substitute_named(ModuleLanguageV1 *l, Atom *body, Atom *target, Atom *replacement) {
    if (!module_spend_v1(&l->work) || l->work.depth >= 512u) {
        module_fail_v1(&l->work, "incomplete", "Named substitution depth exhausted."); return NULL;
    }
    if (module_form_v1(body, "FVar", 1u))
        return atom_eq(body->expr.elems[1], target) ? replacement : body;
    if (!module_form_v1(body, "PApp", 2u)) {
        module_fail_v1(&l->work, "law-pattern-capability", "Named substitution requires resolved constructor patterns."); return NULL;
    }
    ModuleValuesV1 arguments = {0}, lowered = {0}; Atom *out = NULL;
    if (!module_unlist_v1(&l->work, body->expr.elems[2], &arguments)) goto done;
    ++l->work.depth;
    size_t i = 0;
    for (; i < arguments.len; ++i)
        if (!module_push_v1(&l->work, &lowered, substitute_named(l, arguments.items[i], target, replacement))) break;
    --l->work.depth;
    if (i == arguments.len) {
        Atom *fields[] = {body->expr.elems[1], module_list_v1(l->work.arena, &lowered)};
        out = module_make_v1(l->work.arena, "PApp", fields, 2u);
    }
done:
    module_clear_v1(&arguments); module_clear_v1(&lowered); return out;
}

static Atom *check_named_substitution(ModuleLanguageV1 *l, Atom *pattern, Atom *expected,
    ModuleValuesV1 *bindings, bool introduce) {
    Atom *target = pattern->expr.elems[3], *out = NULL;
    ModuleValuesV1 local = {0};
    if (!module_string_v1(target)) {
        module_fail_v1(&l->work, "malformed-substitution-target", "Expected an exact resolved variable name."); return NULL;
    }
    /* The target is local to the authored body, not a new law variable.
     * Replacement variables retain the surrounding law context. */
    for (size_t i = 0; i < bindings->len; ++i)
        if (!atom_eq(bindings->items[i]->expr.elems[1], target) &&
            !module_push_v1(&l->work, &local, bindings->items[i])) goto done;
    ++l->work.depth;
    Atom *body = check_pattern(l, pattern->expr.elems[1], expected, &local, true);
    Atom *variable = module_make_v1(l->work.arena, "FVar", &target, 1u);
    Atom *type = body ? pattern_type(l, variable, &local) : NULL;
    if (body && !type) module_fail_v1(&l->work, "unresolved-substitution-target", "The substitution target must occur with an inferable sort in its body.");
    Atom *replacement = type ? check_pattern(l, pattern->expr.elems[2], type, bindings, introduce) : NULL;
    Atom *expanded = replacement ? substitute_named(l, body, target, replacement) : NULL;
    if (expanded) out = check_pattern(l, expanded, expected, bindings, introduce);
    --l->work.depth;
done:
    module_clear_v1(&local); return out;
}

static Atom *check_pattern(ModuleLanguageV1 *l, Atom *pattern, Atom *expected, ModuleValuesV1 *bindings, bool introduce) {
    if (!module_spend_v1(&l->work) || l->work.depth >= 512u) {
        module_fail_v1(&l->work, "incomplete", "Law pattern depth exhausted."); return NULL;
    }
    if (module_form_v1(pattern, "ModuleNamedSubstitutionV1", 3u))
        return check_named_substitution(l, pattern, expected, bindings, introduce);
    if (module_form_v1(pattern, "FVar", 1u) && module_string_v1(pattern->expr.elems[1])) {
        for (size_t i = l->bound.len; i > 0u; --i) {
            Atom *row = l->bound.items[i - 1u];
            if (atom_eq(row->expr.elems[1], pattern->expr.elems[1]) && atom_eq(row->expr.elems[2], expected)) {
                Atom *index = atom_int(l->work.arena, (int64_t)(l->bound.len - i));
                return module_make_v1(l->work.arena, "Var", &index, 1u);
            }
        }
        Atom *prior = pattern_type(l, pattern, bindings);
        if (prior && atom_eq(prior, expected)) return law_capture(l, pattern->expr.elems[1], expected) ? pattern : NULL;
        if (prior || !introduce) { module_fail_v1(&l->work, prior ? "variable-type-conflict" : "unbound-law-variable", "Law variables must have consistent categories and rewrites cannot introduce free variables."); return NULL; }
        Atom *fields[] = {pattern->expr.elems[1], expected};
        return law_capture(l, pattern->expr.elems[1], expected) &&
            module_push_v1(&l->work, bindings, module_make_v1(l->work.arena, "TypeBinding", fields, 2u)) ? pattern : NULL;
    }
    if (!module_form_v1(pattern, "PApp", 2u)) {
        module_fail_v1(&l->work, "law-pattern-capability", "Binding patterns require a scope-aware consumer; the constructor profile does not erase them."); return NULL;
    }
    Atom *rule = rule_for(l, pattern->expr.elems[1]);
    Atom *type = rule ? module_make_v1(l->work.arena, "TBase", &rule->expr.elems[2], 1u) : NULL;
    if (!type || !atom_eq(type, expected)) { module_fail_v1(&l->work, "dangling-law-constructor", "A law constructor is missing or has the wrong result category."); return NULL; }
    ModuleValuesV1 args = {0}, params = {0}, checked = {0}, syntax = {0}; Atom *out = NULL;
    if (!module_unlist_v1(&l->work, pattern->expr.elems[2], &args) || !module_unlist_v1(&l->work, rule->expr.elems[3], &params)) goto done;
    bool binding = false;
    for (size_t j = 0u; j < params.len; ++j) binding |= !module_form_v1(params.items[j], "TermSimple", 2u);
    if (binding) {
        if (!module_unlist_v1(&l->work, rule->expr.elems[4], &syntax)) goto done;
    } else if (args.len != params.len) { module_fail_v1(&l->work, "law-arity", "A law application has the wrong number of constructor arguments."); goto done; }
    ++l->work.depth;
    size_t i = 0;
    for (; i < params.len; ++i) {
        Atom *parameter = params.items[i];
        Atom *value = NULL;
        if (module_form_v1(parameter, "TermSimple", 2u)) {
            Atom *argument = binding ? law_source_argument(l, &syntax, &args, parameter->expr.elems[1]) : args.items[i];
            if (argument) value = check_pattern(l, argument, parameter->expr.elems[2], bindings, introduce);
        } else if (module_form_v1(parameter, "TermAbstractionNamed", 3u) &&
                   module_form_v1(parameter->expr.elems[1], "BSome", 1u) &&
                   module_form_v1(parameter->expr.elems[3], "TArrow", 2u)) {
            Atom *binder = law_source_argument(l, &syntax, &args, parameter->expr.elems[1]->expr.elems[1]);
            Atom *body = law_source_argument(l, &syntax, &args, parameter->expr.elems[2]);
            if (!binder || !body) break;
            if (!module_form_v1(binder, "FVar", 1u) || !module_string_v1(binder->expr.elems[1])) {
                module_fail_v1(&l->work, "unresolved-binding-pattern-elaboration", "A source binder slot needs a resolved name, not an object pattern."); break;
            }
            Atom *row[] = {binder->expr.elems[1], parameter->expr.elems[3]->expr.elems[1]};
            if (!module_push_v1(&l->work, &l->bound, module_make_v1(l->work.arena, "TypeBinding", row, 2u))) break;
            Atom *checked_body = check_pattern(l, body, parameter->expr.elems[3]->expr.elems[2], bindings, introduce);
            --l->bound.len;
            if (checked_body) {
                Atom *fields[] = {atom_symbol(l->work.arena, "BNone"), checked_body};
                value = module_make_v1(l->work.arena, "PLam", fields, 2u);
            }
        } else module_fail_v1(&l->work, "unresolved-binding-pattern-elaboration", "This parameter needs another scope-aware pattern interpretation.");
        if (!module_push_v1(&l->work, &checked, value)) break;
    }
    --l->work.depth;
    if (i == params.len) {
        Atom *fields[] = {rule->expr.elems[1], module_list_v1(l->work.arena, &checked)};
        out = module_make_v1(l->work.arena, "PApp", fields, 2u);
    }
done:
    module_clear_v1(&args); module_clear_v1(&params); module_clear_v1(&checked); module_clear_v1(&syntax); return out;
}

static Atom *lower_law(ModuleLanguageV1 *l, Atom *body, bool rewrite) {
    ModuleValuesV1 bindings = {0}, premises = {0}, lowered = {0}, available = {0}; Atom *out = NULL;
    l->law_right = false;
    l->bound.len = l->captures.len = 0u;
    Atom *type = pattern_type(l, body->expr.elems[3], &bindings);
    if (!type) type = pattern_type(l, body->expr.elems[4], &bindings);
    if (!type) { module_fail_v1(&l->work, "unresolved-law-category", "A law needs an inferable constructor result category."); goto done; }
    Atom *left = check_pattern(l, body->expr.elems[3], type, &bindings, true);
    for (size_t i = 0; left && i < bindings.len; ++i)
        if (!module_push_v1(&l->work, &available, bindings.items[i]->expr.elems[1])) goto done;
    l->law_right = true;
    Atom *right = left ? check_pattern(l, body->expr.elems[4], type, &bindings, true) : NULL;
    if (!right || !module_unlist_v1(&l->work, body->expr.elems[2], &premises)) goto done;
    for (size_t i = 0; i < premises.len; ++i) {
        Atom *premise = premises.items[i], *value = NULL;
        if (module_form_v1(premise, "ModuleFreshNamesV1", 2u)) {
            Atom *term = module_make_v1(l->work.arena, "FVar", &premise->expr.elems[2], 1u);
            if (!pattern_type(l, term, &bindings)) { module_fail_v1(&l->work, "unbound-premise-variable", "Freshness premise names a variable absent from the law context."); goto done; }
            Atom *fields[] = {premise->expr.elems[1], term}; value = module_make_v1(l->work.arena, "Freshness", fields, 2u);
        } else if (module_form_v1(premise, "ModuleCongruenceNamesV1", 2u)) {
            Atom *p = module_make_v1(l->work.arena, "FVar", &premise->expr.elems[1], 1u);
            Atom *q = module_make_v1(l->work.arena, "FVar", &premise->expr.elems[2], 1u);
            Atom *pt = pattern_type(l, p, &bindings), *qt = pattern_type(l, q, &bindings);
            if (!pt || !qt || !atom_eq(pt, qt)) { module_fail_v1(&l->work, "premise-category-conflict", "A congruence premise needs two context variables of the same category."); goto done; }
            bool bound = false;
            for (size_t j = 0; j < available.len; ++j) if (atom_eq(available.items[j], premise->expr.elems[1])) { bound = true; break; }
            if (!bound) { module_fail_v1(&l->work, "unbound-premise-source", "A rewrite premise source must be bound before the premise runs."); goto done; }
            if (!module_push_v1(&l->work, &available, premise->expr.elems[2])) goto done;
            Atom *fields[] = {p, q}; value = module_make_v1(l->work.arena, "Congruence", fields, 2u);
        }
        if (!module_push_v1(&l->work, &lowered, value)) goto done;
    }
    if (rewrite) for (size_t i = 0; i < bindings.len; ++i) {
        bool bound = false;
        for (size_t j = 0; j < available.len; ++j) if (atom_eq(available.items[j], bindings.items[i]->expr.elems[1])) { bound = true; break; }
        if (!bound) { module_fail_v1(&l->work, "unbound-law-variable", "A rewrite variable must be bound by its source pattern or an earlier premise."); goto done; }
    }
    Atom *fields[] = {body->expr.elems[1], module_list_v1(l->work.arena, &bindings), module_list_v1(l->work.arena, &lowered), left, right};
    out = module_make_v1(l->work.arena, rewrite ? "RewriteRule" : "Equation", fields, 5u);
done:
    module_clear_v1(&bindings); module_clear_v1(&premises); module_clear_v1(&lowered); module_clear_v1(&available);
    module_clear_v1(&l->bound); module_clear_v1(&l->captures); return out;
}

/* Legacy row tags are a presentation spelling, not different core fields.
 * Canonicalization adds exactly the formerly implicit None annotations. */
static Atom *complete_row(ModuleWorkV1 *w, Atom *row) {
    if (!row || !module_spend_v1(w)) return NULL;
    if (w->depth >= 512u) {
        module_fail_v1(w, "incomplete", "Core spelling normalization reached its nesting budget.");
        return NULL;
    }
    if (row->kind != ATOM_EXPR) return row;
    if (!row->expr.len || row->expr.elems[0]->kind != ATOM_SYMBOL) return NULL;
    const char *head = atom_name_cstr(row->expr.elems[0]);
    static const char *from[] = {"TermAbstractionNamed", "TermMultiAbstractionNamed", "SyntaxTerminal", "SyntaxNonTerminal", "SyntaxSeparator", "SyntaxDelimiter"};
    static const char *to[] = {"TermAbstraction", "TermMultiAbstraction", "Terminal", "NonTerminal", "Separator", "Delimiter"};
    for (size_t i = 0; i < 6u; ++i) if (!strcmp(head, from[i])) { head = to[i]; break; }
    Atom *extra = NULL;
    if (module_form_v1(row, "GrammarRule", 5u)) { head = "GrammarRuleV2"; extra = atom_symbol(w->arena, "AlgebraNone"); }
    if (module_form_v1(row, "Equation", 5u)) { head = "ScopedEquationV1"; extra = atom_symbol(w->arena, "RuleBindingNone"); }
    if (module_form_v1(row, "RewriteRule", 5u)) { head = "ScopedRewriteRuleV1"; extra = atom_symbol(w->arena, "RuleBindingNone"); }
    ModuleValuesV1 args = {0}; Atom *out = NULL;
    ++w->depth;
    for (CettaExprIndex i = 1; i < row->expr.len; ++i)
        if (!module_push_v1(w, &args, complete_row(w, row->expr.elems[i]))) goto done;
    if (extra && !module_push_v1(w, &args, extra)) goto done;
    out = module_make_v1(w->arena, head, args.items, args.len);
done:
    --w->depth; module_clear_v1(&args); return out;
}

static bool build_core(ModuleLanguageV1 *l, Atom *p, Atom *name,
    Atom **complete, Atom **document, Atom **refusal) {
    Arena *arena = l->work.arena;
    if (!module_form_v1(p, "ModulePresentationV1", 5u) || !module_string_v1(p->expr.elems[1]) ||
        (!atom_is_symbol(p->expr.elems[2], "OriginCoherentV1") && !atom_is_symbol(p->expr.elems[2], "OriginViewV1")) ||
        !module_unlist_v1(&l->work, p->expr.elems[3], &l->entries)) return false;
    for (size_t i = 0; i < l->entries.len; ++i) {
        Atom *entry = l->entries.items[i];
        if (!module_form_v1(entry, "ModuleEntryV1", 3u) || !module_form_v1(entry->expr.elems[1], "SyntaxOriginV1", 2u) ||
            !module_string_v1(entry->expr.elems[1]->expr.elems[1]) || !module_string_v1(entry->expr.elems[1]->expr.elems[2]))
            return module_fail_v1(&l->work, "malformed-entry", "Expected origin-bearing declarations.");
        l->work.site = entry->expr.elems[1]; Atom *body = entry->expr.elems[3];
        if (module_form_v1(body, "ModuleCoreTypeV1", 1u) || module_form_v1(body, "ModuleCoreTermV1", 1u) ||
            module_form_v1(body, "ModuleCoreEquationV1", 1u) || module_form_v1(body, "ModuleCoreRewriteV1", 1u) ||
            module_form_v1(body, "ModuleEquationV1", 4u) || module_form_v1(body, "ModuleRewriteV1", 4u) || module_form_v1(body, "ModuleExportV1", 2u)) continue;
        Atom *definition = module_form_v1(body, "ModuleTermV1", 4u) ? term_definition(l, body) : body;
        if (module_form_v1(body, "ModuleListMacroV1", 4u)) {
            if (!atom_is_symbol(body->expr.elems[1], "separator") && !atom_is_symbol(body->expr.elems[1], "terminator"))
                return module_fail_v1(&l->work, "malformed-list-macro", "Expected separator or terminator notation.");
            Atom *category = module_category_name_v1(&l->work, body->expr.elems[2]);
            Atom *fields[] = {category, body->expr.elems[3], body->expr.elems[4]};
            definition = module_make_v1(arena, atom_is_symbol(body->expr.elems[1], "separator") ? "LbnfSeparatorV1" : "LbnfTerminatorV1", fields, 3u);
        }
        if (!definition) return false;
        Atom *fields[] = {atom_int(arena, (int64_t)l->definitions.len), definition};
        if (!module_push_v1(&l->work, &l->definitions, module_make_v1(arena, "LbnfDefinitionV1", fields, 2u)) ||
            !module_push_v1(&l->work, &l->definition_origins, entry->expr.elems[1]) ||
            !module_push_v1(&l->work, &l->definition_sources, body)) return false;
    }
    Atom *document_fields[] = {p->expr.elems[1], module_list_v1(arena, &l->definitions)};
    *document = module_make_v1(arena, "LbnfDocumentV1", document_fields, 2u);
    Atom *declarations = NULL, *sites = NULL;
    if (!*document || !cetta_lbnf_document_v1_declarations(arena, *document, name, &l->work.remaining, &declarations, &sites, refusal)) return false;
    if (!module_unlist_v1(&l->work, declarations->expr.elems[3], &l->terms) ||
        !module_unlist_v1(&l->work, declarations->expr.elems[2], &l->types) ||
        !scope_declarations(l, sites)) return false;
    bool complete_format = false;
    for (size_t i = 0; i < l->entries.len; ++i) {
        Atom *entry = l->entries.items[i], *body = entry->expr.elems[3]; l->work.site = entry->expr.elems[1];
        if (module_form_v1(body, "ModuleCoreTypeV1", 1u) || module_form_v1(body, "ModuleCoreTermV1", 1u) ||
            module_form_v1(body, "ModuleCoreEquationV1", 1u) || module_form_v1(body, "ModuleCoreRewriteV1", 1u)) {
            complete_format = true;
            ModuleValuesV1 *rows = module_form_v1(body, "ModuleCoreTypeV1", 1u) ? &l->types :
                module_form_v1(body, "ModuleCoreTermV1", 1u) ? &l->terms :
                module_form_v1(body, "ModuleCoreEquationV1", 1u) ? &l->equations : &l->rewrites;
            if (!module_push_v1(&l->work, rows, body->expr.elems[1])) return false;
            continue;
        }
        bool rewrite = module_form_v1(body, "ModuleRewriteV1", 4u);
        if (rewrite || module_form_v1(body, "ModuleEquationV1", 4u)) {
            if (!module_string_v1(body->expr.elems[1]))
                return module_fail_v1(&l->work, "malformed-law", "Expected a named law declaration.");
            if (!module_push_v1(&l->work, rewrite ? &l->rewrites : &l->equations, lower_law(l, body, rewrite))) return false;
        } else if (module_form_v1(body, "ModuleExportV1", 2u)) {
            Atom *category = module_category_name_v1(&l->work, body->expr.elems[2]);
            if (!category) return module_fail_v1(&l->work, "unresolved-category-elaboration", "This category requires a resolved core elaboration.");
            bool found = false;
            for (size_t j = 0; j < l->types.len; ++j) if (atom_eq(l->types.items[j]->expr.elems[1], category)) { found = true; break; }
            if (!found) {
                Atom *fields[] = {category, atom_symbol(arena, "CarrierAst")};
                if (!module_push_v1(&l->work, &l->types, module_make_v1(arena, "TypeDecl", fields, 2u))) return false;
            }
        }
    }
    if (complete_format) {
        ModuleValuesV1 *groups[] = {&l->terms, &l->equations, &l->rewrites};
        for (size_t g = 0; g < 3u; ++g)
            for (size_t i = 0; i < groups[g]->len; ++i) {
                groups[g]->items[i] = complete_row(&l->work, groups[g]->items[i]);
                if (!groups[g]->items[i]) return false;
            }
    }
    Atom *fields[] = {name, module_list_v1(arena, &l->types), module_list_v1(arena, &l->terms),
        module_list_v1(arena, &l->equations), module_list_v1(arena, &l->rewrites)};
    *complete = module_make_v1(arena, complete_format ? "LanguageDefV2" : "GSLTLanguageDefWireV1", fields, 5u);
    return *complete != NULL;
}

static void close_language(ModuleLanguageV1 *l) {
    module_clear_v1(&l->entries); module_clear_v1(&l->definitions); module_clear_v1(&l->definition_origins);
    module_clear_v1(&l->definition_sources);
    module_clear_v1(&l->equations); module_clear_v1(&l->rewrites); module_clear_v1(&l->terms); module_clear_v1(&l->types);
}

static void core_refusal(ModuleLanguageV1 *l, Atom **refusal) {
    if (l->work.refusal) *refusal = atom_deep_copy(l->work.arena, l->work.refusal);
    else if (!*refusal) {
        module_fail_v1(&l->work, "malformed-presentation", "Expected compact, origin-bearing module declarations.");
        *refusal = atom_deep_copy(l->work.arena, l->work.refusal);
    }
}

Atom *module_core_v1(ModuleWorkV1 *work, Atom *p, Atom *name) {
    if (module_form_v1(p, "ModulePortableValueV1", 2u))
        return cetta_module_portable_v1_core(work, p, name);
    ModuleLanguageV1 l = {.work = {.arena = work->arena, .remaining = work->remaining}};
    Atom *core = NULL, *document = NULL, *failure = NULL;
    bool ok = build_core(&l, p, name, &core, &document, &failure);
    work->remaining = l.work.remaining;
    if (!ok) { core_refusal(&l, &failure); work->refusal = failure; }
    close_language(&l);
    return ok ? core : NULL;
}

bool cetta_module_value_v1_core(Arena *arena, Atom *p, Atom *name, uint32_t limit, Atom **language, Atom **refusal) {
    if (!arena || !language || !refusal) return false;
    ModuleWorkV1 work = {.arena = arena, .remaining = limit};
    Atom *core = module_core_v1(&work, p, name);
    Atom *owned = core ? atom_deep_copy(arena, core) : NULL;
    if (owned) *language = owned;
    else {
        module_fail_v1(&work, "allocation-failure", "Core ownership allocation failed.");
        *refusal = atom_deep_copy(arena, work.refusal);
    }
    return owned != NULL;
}

static Atom *interpretation_unknown(ModuleWorkV1 *w, Atom *method, Atom *obligation) {
    Atom *fields[] = {method, obligation};
    return module_make_v1(w->arena, "ModuleInterpretationUnknownV1", fields, 2u);
}

/* Exact parameter names and law syntax are sufficient for this method, not
 * necessary conditions on all interpretations. In particular it does not
 * search for derived equations. Concrete constructor notation is irrelevant
 * to this core signature check. Ordered rewrite occurrences remain distinct. */
static bool same_interface_row(ModuleWorkV1 *w, Atom *required, Atom *supplied, size_t family) {
    required = complete_row(w, required);
    supplied = complete_row(w, supplied);
    if (!required || !supplied) return false;
    if (family >= 4u) {
        const char *head = family == 4u ? "ScopedEquationV1" : "ScopedRewriteRuleV1";
        if (!module_form_v1(required, head, 6u) || !module_form_v1(supplied, head, 6u)) return false;
        /* Names identify the matched occurrences; the correspondence retains
         * their indices in both cores. A law need not share its display name. */
        for (size_t i = 2u; i <= 6u; ++i)
            if (!atom_eq(required->expr.elems[i], supplied->expr.elems[i])) return false;
        return true;
    }
    if (family != 3u) return atom_eq(required, supplied);
    if (!module_form_v1(required, "GrammarRuleV2", 6u) || !module_form_v1(supplied, "GrammarRuleV2", 6u)) return false;
    return atom_eq(required->expr.elems[1], supplied->expr.elems[1]) &&
        atom_eq(required->expr.elems[2], supplied->expr.elems[2]) &&
        atom_eq(required->expr.elems[3], supplied->expr.elems[3]) &&
        atom_eq(required->expr.elems[5], supplied->expr.elems[5]) &&
        atom_eq(required->expr.elems[6], supplied->expr.elems[6]);
}

Atom *module_interpret_v1(ModuleWorkV1 *w, Atom *required, Atom *supplied, Atom *method) {
    if (!atom_is_symbol(method, "IdentityInclusionV1"))
        return interpretation_unknown(w, method, atom_symbol(w->arena, "method-not-implemented"));
    Atom *name = atom_string(w->arena, "interface");
    Atom *from = module_core_v1(w, required, name);
    Atom *to = from ? module_core_v1(w, supplied, name) : NULL;
    if (!to) {
        Atom *reason = w->refusal;
        w->refusal = NULL;
        return interpretation_unknown(w, method, reason ? reason : atom_symbol(w->arena, "core-not-established"));
    }
    ModuleValuesV1 matches = {0}; Atom *out = NULL;
    const char *families[] = {"types", "terms", "equations", "rewrites"};
    for (size_t family = 2u; family <= 5u; ++family) {
        ModuleValuesV1 a = {0}, b = {0};
        bool ok = module_unlist_v1(w, from->expr.elems[family], &a) &&
            module_unlist_v1(w, to->expr.elems[family], &b);
        size_t next = 0u;
        for (size_t i = 0u; ok && i < a.len; ++i) {
            size_t j = family == 5u ? next : 0u;
            for (; j < b.len; ++j) {
                if (!module_spend_v1(w)) { ok = false; break; }
                if (same_interface_row(w, a.items[i], b.items[j], family)) break;
                if (w->refusal) { ok = false; break; }
            }
            if (!ok) break;
            if (j == b.len) {
                Atom *fields[] = {atom_symbol(w->arena, families[family - 2u]),
                    atom_int(w->arena, (int64_t)i), a.items[i]};
                out = interpretation_unknown(w, method,
                    module_make_v1(w->arena, "ModuleUnmatchedDeclarationV1", fields, 3u));
                ok = false; break;
            }
            next = j + 1u;
            Atom *fields[] = {atom_symbol(w->arena, families[family - 2u]),
                atom_int(w->arena, (int64_t)i), atom_int(w->arena, (int64_t)j)};
            ok = module_push_v1(w, &matches, module_make_v1(w->arena, "ModuleDeclarationMatchV1", fields, 3u));
        }
        module_clear_v1(&a); module_clear_v1(&b);
        if (!ok) goto done;
    }
    {
        Atom *fields[] = {method, from, to, module_list_v1(w->arena, &matches)};
        out = module_make_v1(w->arena, "ModuleInterpretationFoundV1", fields, 4u);
    }
done:
    module_clear_v1(&matches);
    return out;
}

bool cetta_module_value_v1_interpret(Arena *arena, Atom *required, Atom *supplied,
    Atom *method, uint32_t limit, Atom **result, Atom **refusal) {
    if (!arena || !result || !refusal) return false;
    ModuleWorkV1 work = {.arena = arena, .remaining = limit};
    Atom *value = module_interpret_v1(&work, required, supplied, method);
    Atom *owned = value ? atom_deep_copy(arena, value) : NULL;
    if (owned) *result = owned;
    else {
        module_fail_v1(&work, "allocation-failure", "Interpretation ownership allocation failed.");
        *refusal = atom_deep_copy(arena, work.refusal);
    }
    return owned != NULL;
}

bool cetta_module_value_v1_rewrite_rules(Arena *arena, Atom *p, Atom *name,
    uint32_t limit, Atom **rules, Atom **refusal) {
    if (!arena || !rules || !refusal) return false;
    ModuleLanguageV1 l = {.work = {.arena = arena, .remaining = limit}};
    Atom *core = NULL, *document = NULL, *failure = NULL, *result = NULL;
    ModuleValuesV1 occurrences = {0};
    bool ok = build_core(&l, p, name, &core, &document, &failure);
    size_t index = 0;
    for (size_t i = 0; ok && i < l.entries.len; ++i) {
        Atom *entry = l.entries.items[i];
        if (!module_form_v1(entry->expr.elems[3], "ModuleRewriteV1", 4u) &&
            !module_form_v1(entry->expr.elems[3], "ModuleCoreRewriteV1", 1u)) continue;
        if (index >= l.rewrites.len) { ok = false; break; }
        Atom *fields[] = {entry->expr.elems[1], l.rewrites.items[index++]};
        ok = module_push_v1(&l.work, &occurrences,
            module_make_v1(arena, "ModuleRuleOccurrenceV1", fields, 2u));
    }
    if (ok && index == l.rewrites.len) {
        Atom *fields[] = {core, module_list_v1(arena, &occurrences)};
        result = module_make_v1(arena, "ModuleRewriteRulesV1", fields, 2u);
    }
    Atom *owned = result ? atom_deep_copy(arena, result) : NULL;
    if (owned) *rules = owned;
    else { core_refusal(&l, &failure); *refusal = failure; ok = false; }
    module_clear_v1(&occurrences);
    close_language(&l);
    return ok;
}

bool cetta_module_value_v1_language(Arena *arena, Atom *p, Atom *name, Atom *start, Atom *policy,
    uint32_t limit, CettaLbnfLoweredV1 *syntax, Atom **complete, Atom **refusal) {
    if (!arena || !syntax || !complete || !refusal) return false;
    ModuleLanguageV1 l = {.work = {.arena = arena, .remaining = limit}};
    CettaLbnfLoweredV1 candidate = {0}; bool ok = false;
    Atom *language = NULL, *document = NULL, *failure = NULL;
    ModuleValuesV1 metadata = {0}, origins = {0}, type_origins = {0}, syntax_types = {0};
    if (!build_core(&l, p, name, &language, &document, &failure)) goto done;
    for (size_t i = 0; i < l.entries.len; ++i) {
        if (module_form_v1(l.entries.items[i]->expr.elems[3], "ModuleCoreTermV1", 1u)) {
            /* The legacy BNFC projection must not silently omit declarations
             * from the complete core. Other checked syntax interpretations may
             * extend this partial consumer; this is not an impossibility test. */
            Atom *fields[] = {atom_symbol(arena, "TypedCoreSyntaxV1"), l.entries.items[i]};
            l.work.refusal = module_make_v1(arena, "ModuleInterpretationUnknownV1", fields, 2u);
            goto done;
        }
    }
    if (!cetta_lbnf_document_v1_lower(arena, document, name, start, policy, l.work.remaining, &candidate, &failure)) goto done;
    if (!module_unlist_v1(&l.work, candidate.profile->expr.elems[5]->expr.elems[4], &metadata)) { module_clear_v1(&metadata); goto done; }
    for (size_t i = 0; i < metadata.len; ++i) {
        Atom *row = metadata.items[i], *occurrence = row->expr.elems[1];
        if (!module_form_v1(occurrence, "LbnfProductionOccurrenceV1", 3u) || occurrence->expr.elems[2]->kind != ATOM_GROUNDED ||
            occurrence->expr.elems[2]->ground.gkind != GV_INT || occurrence->expr.elems[2]->ground.ival < 0 ||
            (uint64_t)occurrence->expr.elems[2]->ground.ival >= l.definition_origins.len) { module_clear_v1(&metadata); module_clear_v1(&origins); goto done; }
        Atom *origin = l.definition_origins.items[(size_t)occurrence->expr.elems[2]->ground.ival];
        size_t expansion = 0u;
        for (size_t j = 0; j < i; ++j) {
            Atom *prior = metadata.items[j]->expr.elems[1];
            if (module_form_v1(prior, "ModuleProductionOccurrenceV1", 2u) && atom_eq(prior->expr.elems[1], origin)) ++expansion;
        }
        if (!module_form_v1(origin, "SyntaxOriginV1", 2u) || !module_string_v1(origin->expr.elems[1]) ||
            !module_string_v1(origin->expr.elems[2])) { module_clear_v1(&metadata); module_clear_v1(&origins); goto done; }
        size_t site_length = strlen(origin->expr.elems[2]->ground.sval);
        char *site = arena_alloc(arena, site_length + 80u);
        if (!site) { module_clear_v1(&metadata); module_clear_v1(&origins); goto done; }
        snprintf(site, site_length + 80u, "expanded:%zu:%s:%zu", site_length, origin->expr.elems[2]->ground.sval, expansion);
        /* SyntaxOrigins uses a portable string site; packed metadata retains
         * the original declaration origin and the expansion occurrence. */
        Atom *packed_fields[] = {origin, atom_int(arena, (int64_t)expansion)};
        Atom *fields[] = {module_make_v1(arena, "ModuleProductionOccurrenceV1", packed_fields, 2u), row->expr.elems[2], row->expr.elems[3]};
        metadata.items[i] = module_make_v1(arena, "ParserProductionMetadataV1", fields, 3u);
        Atom *origin_args[] = {origin->expr.elems[1], atom_string(arena, site)};
        if (!module_push_v1(&l.work, &origins, module_make_v1(arena, "SyntaxOriginV1", origin_args, 2u))) {
            module_clear_v1(&metadata); module_clear_v1(&origins); goto done;
        }
    }
    Atom *token_layer = candidate.profile->expr.elems[5];
    Atom *token_fields[] = {token_layer->expr.elems[1], token_layer->expr.elems[2], token_layer->expr.elems[3], module_list_v1(arena, &metadata), token_layer->expr.elems[5]};
    Atom *profile_fields[] = {candidate.profile->expr.elems[1], candidate.profile->expr.elems[2], candidate.profile->expr.elems[3], candidate.profile->expr.elems[4], module_make_v1(arena, "ParserTokenLayerV1", token_fields, 5u)};
    candidate.profile = module_make_v1(arena, "GSLTParserProfileLayerV2", profile_fields, 5u);
    if (!module_unlist_v1(&l.work, candidate.origins->expr.elems[1], &type_origins) ||
        !module_unlist_v1(&l.work, candidate.language->expr.elems[2], &syntax_types)) {
        module_clear_v1(&type_origins); module_clear_v1(&syntax_types); goto done;
    }
    for (size_t i = 0; i < type_origins.len; ++i) for (size_t j = 0; j < l.entries.len; ++j) {
        Atom *entry = l.entries.items[j], *body = entry->expr.elems[3];
        if (module_form_v1(body, "ModuleExportV1", 2u)) {
            Atom *type_name = module_category_name_v1(&l.work, body->expr.elems[2]);
            if (type_name && i < syntax_types.len && atom_eq(type_name, syntax_types.items[i]->expr.elems[1])) type_origins.items[i] = entry->expr.elems[1];
        }
    }
    Atom *origin_fields[] = {module_list_v1(arena, &type_origins), module_list_v1(arena, &origins), atom_symbol(arena, "LNil"), atom_symbol(arena, "LNil")};
    candidate.origins = module_make_v1(arena, "SyntaxOriginsV1", origin_fields, 4u);
    module_clear_v1(&type_origins);
    module_clear_v1(&syntax_types);
    module_clear_v1(&metadata); module_clear_v1(&origins);
    if (!language) goto done;
    Atom *owned_language = atom_deep_copy(arena, language);
    CettaLbnfLoweredV1 owned = {atom_deep_copy(arena, candidate.language), atom_deep_copy(arena, candidate.profile),
        atom_deep_copy(arena, candidate.policy), atom_deep_copy(arena, candidate.origins)};
    if (!owned_language || !owned.language || !owned.profile || !owned.policy || !owned.origins) goto done;
    *complete = owned_language; *syntax = owned; ok = true;
done:
    if (!ok) { core_refusal(&l, &failure); *refusal = failure; }
    module_clear_v1(&metadata); module_clear_v1(&origins);
    module_clear_v1(&type_origins); module_clear_v1(&syntax_types);
    close_language(&l);
    return ok;
}

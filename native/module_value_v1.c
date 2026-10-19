#include "module_value_internal_v1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool module_form_v1(const Atom *value, const char *head, size_t arity) {
    return value && value->kind == ATOM_EXPR && value->expr.elems &&
        value->expr.len == arity + 1u && atom_is_symbol(value->expr.elems[0], head);
}

bool module_string_v1(const Atom *value) {
    return value && value->kind == ATOM_GROUNDED && value->ground.gkind == GV_STRING &&
        value->ground.sval && value->ground.sval[0];
}

Atom *module_make_v1(Arena *arena, const char *head, Atom *const *args, size_t count) {
    if (count > SIZE_MAX / sizeof(Atom *) - 1u) return NULL;
    Atom **fields = arena_alloc(arena, (count + 1u) * sizeof(*fields));
    if (!fields) return NULL;
    fields[0] = atom_symbol(arena, head);
    for (size_t i = 0; i < count; ++i) {
        if (!args[i]) return NULL;
        fields[i + 1u] = args[i];
    }
    return atom_expr(arena, fields, count + 1u);
}

bool module_fail_v1(ModuleWorkV1 *work, const char *reason, const char *detail) {
    if (!work->refusal) {
        Atom *args[] = {atom_symbol(work->arena, reason),
            work->site ? work->site : atom_symbol(work->arena, "ModuleNoSourceSiteV1"),
            atom_string(work->arena, detail)};
        work->refusal = module_make_v1(work->arena, "ModuleRefusedV1", args, 3u);
    }
    return false;
}

bool module_spend_v1(ModuleWorkV1 *work) {
    if (!work->remaining) return module_fail_v1(work, "incomplete", "Module structural work exhausted.");
    --work->remaining;
    return true;
}

bool module_push_v1(ModuleWorkV1 *work, ModuleValuesV1 *values, Atom *value) {
    if (!value || !module_spend_v1(work)) return false;
    if (values->len == values->capacity) {
        size_t capacity = values->capacity ? values->capacity * 2u : 16u;
        if (capacity < values->capacity || capacity > SIZE_MAX / sizeof(*values->items))
            return module_fail_v1(work, "incomplete", "Module collection is too large.");
        Atom **grown = realloc(values->items, capacity * sizeof(*grown));
        if (!grown) return module_fail_v1(work, "allocation-failure", "Module collection allocation failed.");
        values->items = grown;
        values->capacity = capacity;
    }
    values->items[values->len++] = value;
    return true;
}

void module_clear_v1(ModuleValuesV1 *values) {
    free(values->items);
    memset(values, 0, sizeof(*values));
}

Atom *module_list_v1(Arena *arena, const ModuleValuesV1 *values) {
    Atom *list = atom_symbol(arena, "LNil");
    for (size_t i = values->len; i > 0; --i)
        list = atom_expr3(arena, atom_symbol(arena, "LCons"), values->items[i - 1u], list);
    return list;
}

bool module_unlist_v1(ModuleWorkV1 *work, Atom *list, ModuleValuesV1 *values) {
    while (module_form_v1(list, "LCons", 2u)) {
        if (!module_push_v1(work, values, list->expr.elems[1])) return false;
        list = list->expr.elems[2];
    }
    return atom_is_symbol(list, "LNil") ||
        module_fail_v1(work, "malformed-list", "Expected a finite declaration list.");
}

/* Names use the existing NUL-free UTF-8 string domain. Literal text remains
 * in the parser's counted scalar representation, including U+0000. */
Atom *module_text_v1(ModuleWorkV1 *work, Atom *value) {
    if (module_form_v1(value, "LbnfAstTokenV1", 6u)) value = value->expr.elems[3];
    if (module_string_v1(value)) return value;
    ModuleValuesV1 points = {0};
    size_t bytes = 0;
    Atom *cursor = value;
    while (module_form_v1(cursor, "bnf-v1:text-cons", 2u)) {
        Atom *point = cursor->expr.elems[1];
        if (!point || point->kind != ATOM_GROUNDED || point->ground.gkind != GV_INT ||
            point->ground.ival <= 0 || point->ground.ival > 0x10ffff ||
            (point->ground.ival >= 0xd800 && point->ground.ival <= 0xdfff) ||
            !module_push_v1(work, &points, point)) goto bad;
        uint32_t cp = (uint32_t)point->ground.ival;
        size_t n = cp < 128 ? 1u : cp < 2048 ? 2u : cp < 65536 ? 3u : 4u;
        if (bytes > SIZE_MAX - n - 1u) goto bad;
        bytes += n;
        cursor = cursor->expr.elems[2];
    }
    if (!module_form_v1(cursor, "bnf-v1:text-nil", 0u) || !bytes) goto bad;
    char *text = arena_alloc(work->arena, bytes + 1u);
    if (!text) goto bad;
    size_t at = 0;
    for (size_t i = 0; i < points.len; ++i) {
        uint32_t cp = (uint32_t)points.items[i]->ground.ival;
        if (cp < 128) text[at++] = (char)cp;
        else if (cp < 2048) {
            text[at++] = (char)(0xc0 | (cp >> 6)); text[at++] = (char)(0x80 | (cp & 63));
        } else if (cp < 65536) {
            text[at++] = (char)(0xe0 | (cp >> 12)); text[at++] = (char)(0x80 | ((cp >> 6) & 63));
            text[at++] = (char)(0x80 | (cp & 63));
        } else {
            text[at++] = (char)(0xf0 | (cp >> 18)); text[at++] = (char)(0x80 | ((cp >> 12) & 63));
            text[at++] = (char)(0x80 | ((cp >> 6) & 63)); text[at++] = (char)(0x80 | (cp & 63));
        }
    }
    text[at] = '\0';
    module_clear_v1(&points);
    return atom_string(work->arena, text);
bad:
    module_clear_v1(&points);
    module_fail_v1(work, "malformed-name", "Expected nonempty scalar text without NUL.");
    return NULL;
}

static bool ast_args(ModuleWorkV1 *work, Atom *node, const char *label, Atom **args, size_t count) {
    if (!module_form_v1(node, "LbnfAstNodeV1", 4u) ||
        !module_string_v1(node->expr.elems[2]) || strcmp(node->expr.elems[2]->ground.sval, label)) return false;
    Atom *children = node->expr.elems[4];
    for (size_t i = 0; i < count; ++i) {
        if (!module_form_v1(children, "LCons", 2u)) return false;
        args[i] = children->expr.elems[1]; children = children->expr.elems[2];
    }
    if (!atom_is_symbol(children, "LNil")) return false;
    work->site = node->expr.elems[3];
    return true;
}

static Atom *ast_list(Atom *node) {
    return module_form_v1(node, "LbnfAstListV1", 3u) ? node->expr.elems[3] : NULL;
}

Atom *module_site_v1(ModuleWorkV1 *work, Atom *span, Atom *kind) {
    Atom *key = atom_int(work->arena, (int64_t)work->sites.len);
    Atom *args[] = {key, kind, span ? span : atom_symbol(work->arena, "ModuleNoSourceSiteV1")};
    return module_push_v1(work, &work->sites,
        module_make_v1(work->arena, "ModuleSourceSiteV1", args, 3u)) ? key : NULL;
}

static Atom *category(ModuleWorkV1 *work, Atom *node);
static Atom *path(ModuleWorkV1 *work, Atom *node) {
    if (!module_spend_v1(work) || work->depth >= 512u) {
        module_fail_v1(work, "incomplete", "Module path structural depth exhausted."); return NULL;
    }
    ++work->depth;
    Atom *args[2];
    if (ast_args(work, node, "BaseDottedPath", args, 1u)) {
        Atom *out = module_text_v1(work, args[0]); --work->depth; return out;
    }
    if (ast_args(work, node, "QualifiedDottedPath", args, 2u)) {
        Atom *prefix = module_text_v1(work, args[0]), *suffix = path(work, args[1]);
        if (!prefix || !suffix) { --work->depth; return NULL; }
        size_t a = strlen(prefix->ground.sval), b = strlen(suffix->ground.sval);
        if (a > SIZE_MAX - b - 2u) { --work->depth; return NULL; }
        char *name = arena_alloc(work->arena, a + b + 2u);
        memcpy(name, prefix->ground.sval, a); name[a] = '.';
        memcpy(name + a + 1u, suffix->ground.sval, b + 1u);
        --work->depth;
        return atom_string(work->arena, name);
    }
    --work->depth;
    module_fail_v1(work, "malformed-path", "Expected a module or theory path.");
    return NULL;
}

static Atom *category(ModuleWorkV1 *work, Atom *node) {
    if (!module_spend_v1(work) || work->depth >= 512u) {
        module_fail_v1(work, "incomplete", "Category structural depth exhausted."); return NULL;
    }
    ++work->depth;
    Atom *args[2], *out = NULL;
    if (ast_args(work, node, "IdCat", args, 1u)) {
        Atom *name = module_text_v1(work, args[0]);
        if (name) out = module_make_v1(work->arena, "TBase", &name, 1u);
    } else if (ast_args(work, node, "ListOfCat", args, 1u)) {
        Atom *values[] = {atom_symbol(work->arena, "CollVec"), category(work, args[0])};
        out = module_make_v1(work->arena, "TCollection", values, 2u);
    } else if (ast_args(work, node, "ArrowCat", args, 2u)) {
        Atom *values[] = {category(work, args[0]), category(work, args[1])};
        out = module_make_v1(work->arena, "TArrow", values, 2u);
    } else if (ast_args(work, node, "ProdCat", args, 1u)) {
        ModuleValuesV1 values = {0};
        Atom *list = ast_list(args[0]);
        while (module_form_v1(list, "LCons", 2u)) {
            if (!module_push_v1(work, &values, category(work, list->expr.elems[1]))) break;
            list = list->expr.elems[2];
        }
        if (atom_is_symbol(list, "LNil")) {
            Atom *types = module_list_v1(work->arena, &values);
            out = module_make_v1(work->arena, "ModuleProductCategoryV1", &types, 1u);
        }
        module_clear_v1(&values);
    }
    --work->depth;
    if (!out) module_fail_v1(work, "malformed-category", "Expected a declared category structure.");
    return out;
}

Atom *module_category_name_v1(ModuleWorkV1 *work, Atom *type) {
    if (!module_spend_v1(work) || work->depth >= 512u) {
        module_fail_v1(work, "incomplete", "Category name depth exhausted."); return NULL;
    }
    if (module_form_v1(type, "TBase", 1u) && module_string_v1(type->expr.elems[1])) {
        if (!strncmp(type->expr.elems[1]->ground.sval, MODULE_TUPLE_CATEGORY_PREFIX_V1,
                     strlen(MODULE_TUPLE_CATEGORY_PREFIX_V1))) {
            module_fail_v1(work, "derived-category-collision", "An authored base name cannot occupy the derived tuple namespace."); return NULL;
        }
        return type->expr.elems[1];
    }
    if (module_form_v1(type, "ModuleProductCategoryV1", 1u)) {
        ModuleValuesV1 components = {0}; Atom *out = NULL;
        if (!module_unlist_v1(work, type->expr.elems[1], &components)) goto product_done;
        size_t capacity = strlen(MODULE_TUPLE_CATEGORY_PREFIX_V1) + 32u;
        for (size_t i = 0; i < components.len; ++i) {
            Atom *component = components.items[i];
            if (!module_form_v1(component, "TBase", 1u) ||
                !module_category_name_v1(work, component)) {
                module_fail_v1(work, "tuple-component-profile", "Tuple syntax requires already resolved base-sort components."); goto product_done;
            }
            size_t length = strlen(component->expr.elems[1]->ground.sval);
            if (length > SIZE_MAX - 32u || capacity > SIZE_MAX - length - 32u) {
                module_fail_v1(work, "incomplete", "Derived tuple name is too large."); goto product_done;
            }
            capacity += length + 32u;
        }
        char *name = arena_alloc(work->arena, capacity);
        if (!name) { module_fail_v1(work, "allocation-failure", "Derived tuple name allocation failed."); goto product_done; }
        size_t used = (size_t)snprintf(name, capacity, "%s%zu", MODULE_TUPLE_CATEGORY_PREFIX_V1, components.len);
        for (size_t i = 0; i < components.len; ++i) {
            const char *component = components.items[i]->expr.elems[1]->ground.sval;
            used += (size_t)snprintf(name + used, capacity - used, ":%zu:%s", strlen(component), component);
        }
        /* The terminator prevents indexed-category normalization from changing
         * a generated name whose final component happens to end in a digit. */
        name[used++] = ';'; name[used] = '\0';
        out = atom_string(work->arena, name);
product_done:
        module_clear_v1(&components); return out;
    }
    if (module_form_v1(type, "TCollection", 2u) && atom_is_symbol(type->expr.elems[1], "CollVec")) {
        ++work->depth;
        Atom *element = module_category_name_v1(work, type->expr.elems[2]);
        --work->depth;
        if (!element) return NULL;
        size_t n = strlen(element->ground.sval);
        char *name = arena_alloc(work->arena, n + 3u);
        snprintf(name, n + 3u, "[%s]", element->ground.sval);
        return atom_string(work->arena, name);
    }
    /* Rich categories retain their actual structure in declaration data.
     * Their consumer mapping is admitted separately. */
    return NULL;
}

static Atom *label(ModuleWorkV1 *work, Atom *node) {
    Atom *args[1];
    if (ast_args(work, node, "Id", args, 1u)) return module_text_v1(work, args[0]);
    if (ast_args(work, node, "Wild", args, 0u)) return atom_string(work->arena, "_");
    static const char *heads[] = {"ListE", "ListCons", "ListOne"};
    static const char *values[] = {"[]", "(:)", "(:[])"};
    for (size_t i = 0; i < 3u; ++i) if (ast_args(work, node, heads[i], args, 1u)) {
        Atom *fields[] = {atom_string(work->arena, values[i]), category(work, args[0])};
        return module_make_v1(work->arena, "ModuleListLabelV1", fields, 2u);
    }
    return NULL;
}

static Atom *item(ModuleWorkV1 *work, Atom *node) {
    if (!module_spend_v1(work) || work->depth >= 512u) {
        module_fail_v1(work, "incomplete", "Notation structural depth exhausted."); return NULL;
    }
    ++work->depth;
    Atom *args[2], *out = NULL;
    if (ast_args(work, node, "Terminal", args, 1u) && module_form_v1(args[0], "LbnfAstTokenV1", 6u)) {
        Atom *text = args[0]->expr.elems[3];
        out = module_make_v1(work->arena, "LbnfTerminalV1", &text, 1u);
    } else if (ast_args(work, node, "NTerminal", args, 1u)) {
        Atom *type = category(work, args[0]);
        out = module_make_v1(work->arena, "ModuleNonTerminalV1", &type, 1u);
    } else if (ast_args(work, node, "BindNTerminal", args, 2u)) {
        Atom *fields[] = {module_text_v1(work, args[0]), category(work, args[1])};
        out = module_make_v1(work->arena, "ModuleBindV1", fields, 2u);
    } else if (ast_args(work, node, "AbsNTerminal", args, 2u)) {
        Atom *fields[] = {module_text_v1(work, args[0]), item(work, args[1])};
        out = module_make_v1(work->arena, "ModuleAbstractionV1", fields, 2u);
    }
    --work->depth;
    if (!out) module_fail_v1(work, "malformed-item", "Expected a notation item with its binding metadata.");
    return out;
}

static Atom *core_pattern(ModuleWorkV1 *w, Atom *node);
static Atom *core_rows(ModuleWorkV1 *w, Atom *node, unsigned kind) {
    ModuleValuesV1 rows = {0}; Atom *out = NULL, *list = ast_list(node);
    while (module_form_v1(list, "LCons", 2u)) {
        Atom *row = list->expr.elems[1], *args[2], *value = NULL;
        if (kind < 2u && ast_args(w, row, "CoreParameter", args, 2u)) {
            Atom *name = module_text_v1(w, args[0]), *sort = module_text_v1(w, args[1]);
            Atom *fields[] = {name, module_make_v1(w->arena, "TBase", &sort, 1u)};
            value = module_make_v1(w->arena, kind ? "TypeBinding" : "TermSimple", fields, 2u);
        } else if (kind == 2u && ast_args(w, row, "CoreTerminal", args, 1u)) {
            Atom *value_text = module_text_v1(w, args[0]);
            value = module_make_v1(w->arena, "Terminal", &value_text, 1u);
        } else if (kind == 2u && ast_args(w, row, "CoreArgument", args, 1u)) {
            Atom *name = module_text_v1(w, args[0]);
            value = module_make_v1(w->arena, "NonTerminal", &name, 1u);
        } else if (kind == 3u && ast_args(w, row, "CoreCongruence", args, 2u)) {
            Atom *fields[] = {core_pattern(w, args[0]), core_pattern(w, args[1])};
            value = module_make_v1(w->arena, "Congruence", fields, 2u);
        } else if (kind == 3u && ast_args(w, row, "CoreRelation", args, 2u)) {
            Atom *fields[] = {module_text_v1(w, args[0]), core_rows(w, args[1], 4u)};
            value = module_make_v1(w->arena, "RelationQuery", fields, 2u);
        } else if (kind == 4u) value = core_pattern(w, row);
        if (!module_push_v1(w, &rows, value)) goto done;
        list = list->expr.elems[2];
    }
    if (atom_is_symbol(list, "LNil")) out = module_list_v1(w->arena, &rows);
done:
    module_clear_v1(&rows); return out;
}

static Atom *core_pattern(ModuleWorkV1 *w, Atom *node) {
    if (!module_spend_v1(w) || w->depth >= 512u) return NULL;
    Atom *args[2], *out = NULL;
    ++w->depth;
    if (ast_args(w, node, "CoreVariable", args, 1u)) {
        Atom *name = module_text_v1(w, args[0]);
        out = module_make_v1(w->arena, "FVar", &name, 1u);
    } else if (ast_args(w, node, "CoreConstructor", args, 2u)) {
        Atom *fields[] = {module_text_v1(w, args[0]), core_rows(w, args[1], 4u)};
        out = module_make_v1(w->arena, "PApp", fields, 2u);
    }
    --w->depth; return out;
}

static Atom *core_declaration(ModuleWorkV1 *w, Atom *node, unsigned kind) {
    Atom *args[5], *value = NULL;
    if (kind == 0u && ast_args(w, node, "CoreTypedTerm", args, 4u)) {
        Atom *fields[] = {module_text_v1(w, args[0]), module_text_v1(w, args[3]),
            core_rows(w, args[1], 0u), core_rows(w, args[2], 2u),
            atom_symbol(w->arena, "EvalNone"), atom_symbol(w->arena, "AlgebraNone")};
        value = module_make_v1(w->arena, "GrammarRuleV2", fields, 6u);
    } else if (kind == 3u) {
        bool carrier = ast_args(w, node, "CoreCarrierType", args, 2u);
        if (!carrier && !ast_args(w, node, "CoreAstType", args, 1u)) return NULL;
        Atom *name = module_text_v1(w, args[0]);
        const char *tag = "CarrierAst";
        if (carrier) {
            Atom *carrier_name = module_text_v1(w, args[1]);
            if (!carrier_name) return NULL;
            const char *s = carrier_name->ground.sval;
            if (!strcmp(s, "String")) tag = "CarrierBuiltinString";
            else if (!strcmp(s, "Int")) tag = "CarrierBuiltinInt";
            else if (!strcmp(s, "Bool")) tag = "CarrierBuiltinBool";
            else { module_fail_v1(w, "unresolved-carrier-interpretation", "This carrier needs an explicit interpretation."); return NULL; }
        }
        Atom *fields[] = {name, atom_symbol(w->arena, tag)};
        value = module_make_v1(w->arena, "TypeDecl", fields, 2u);
    } else if (kind == 1u || kind == 2u) {
        bool conditional = ast_args(w, node, kind == 2u ? "CoreConditionalRewrite" : "CoreConditionalEquation", args, 5u);
        if (!conditional && !ast_args(w, node, kind == 2u ? "CoreTypedRewrite" : "CoreTypedEquation", args, 4u)) return NULL;
        Atom *fields[] = {module_text_v1(w, args[0]), core_rows(w, args[1], 1u),
            conditional ? core_rows(w, args[2], 3u) : atom_symbol(w->arena, "LNil"),
            core_pattern(w, args[conditional ? 3u : 2u]), core_pattern(w, args[conditional ? 4u : 3u]),
            atom_symbol(w->arena, "RuleBindingNone")};
        value = module_make_v1(w->arena, kind == 2u ? "ScopedRewriteRuleV1" : "ScopedEquationV1", fields, 6u);
    }
    if (!value) return NULL;
    static const char *tags[] = {"ModuleCoreTermV1", "ModuleCoreEquationV1", "ModuleCoreRewriteV1", "ModuleCoreTypeV1"};
    Atom *body = module_make_v1(w->arena, tags[kind], &value, 1u);
    Atom *fields[] = {module_site_v1(w, node->expr.elems[3], atom_symbol(w->arena, tags[kind])), body};
    return module_make_v1(w->arena, "ModuleDeclarationV1", fields, 2u);
}

static Atom *definition(ModuleWorkV1 *work, Atom *node) {
    Atom *typed = core_declaration(work, node, 0u);
    if (typed) return typed;
    Atom *args[3];
    bool internal = ast_args(work, node, "Internal", args, 3u);
    if (internal || ast_args(work, node, "Rule", args, 3u)) {
        Atom *site = node->expr.elems[3], *head = label(work, args[0]), *type = category(work, args[1]);
        ModuleValuesV1 items = {0};
        Atom *list = ast_list(args[2]), *out = NULL;
        while (module_form_v1(list, "LCons", 2u)) {
            if (!module_push_v1(work, &items, item(work, list->expr.elems[1]))) break;
            list = list->expr.elems[2];
        }
        if (head && type && atom_is_symbol(list, "LNil")) {
            Atom *fields[] = {head, type, module_list_v1(work->arena, &items), atom_bool(work->arena, internal)};
            Atom *body = module_make_v1(work->arena, "ModuleTermV1", fields, 4u);
            Atom *located[] = {module_site_v1(work, site, atom_symbol(work->arena, "term")), body};
            out = module_make_v1(work->arena, "ModuleDeclarationV1", located, 2u);
        }
        module_clear_v1(&items);
        return out;
    }
    if (ast_args(work, node, "Separator", args, 3u) || ast_args(work, node, "Terminator", args, 3u)) {
        Atom *dummy[1]; bool nonempty = ast_args(work, args[0], "MNonempty", dummy, 0u);
        if (!nonempty && !ast_args(work, args[0], "MEmpty", dummy, 0u)) return NULL;
        Atom *fields[] = {atom_symbol(work->arena, !strcmp(node->expr.elems[2]->ground.sval, "Separator") ? "separator" : "terminator"),
            category(work, args[1]), module_form_v1(args[2], "LbnfAstTokenV1", 6u) ? args[2]->expr.elems[3] : NULL,
            atom_bool(work->arena, nonempty)};
        Atom *body = module_make_v1(work->arena, "ModuleListMacroV1", fields, 4u);
        Atom *located[] = {module_site_v1(work, node->expr.elems[3], atom_symbol(work->arena, "list-macro")), body};
        return module_make_v1(work->arena, "ModuleDeclarationV1", located, 2u);
    }
    Atom *out = NULL, *refusal = NULL;
    if (!cetta_lbnf_definition_v1_project(work->arena, node, work->remaining, &out, &refusal)) {
        module_fail_v1(work, "malformed-definition", "Expected a supported LBNF pragma declaration."); return NULL;
    }
    Atom *fields[] = {module_site_v1(work, node->expr.elems[3], atom_symbol(work->arena, "pragma")), out->expr.elems[2]};
    return module_make_v1(work->arena, "ModuleDeclarationV1", fields, 2u);
}

static Atom *pattern(ModuleWorkV1 *work, Atom *node) {
    if (!module_spend_v1(work) || work->depth >= 512u) {
        module_fail_v1(work, "incomplete", "Pattern structural depth exhausted."); return NULL;
    }
    ++work->depth;
    Atom *args[3], *out = NULL;
    if (ast_args(work, node, "ASTVar", args, 1u)) {
        Atom *name = path(work, args[0]);
        out = module_make_v1(work->arena, "FVar", &name, 1u);
    } else if (ast_args(work, node, "ASTSExp", args, 2u)) {
        Atom *head = label(work, args[0]);
        ModuleValuesV1 values = {0};
        Atom *list = ast_list(args[1]);
        while (module_form_v1(list, "LCons", 2u)) {
            if (!module_push_v1(work, &values, pattern(work, list->expr.elems[1]))) break;
            list = list->expr.elems[2];
        }
        if (head && atom_is_symbol(list, "LNil")) {
            Atom *fields[] = {head, module_list_v1(work->arena, &values)};
            out = module_make_v1(work->arena, "PApp", fields, 2u);
        }
        module_clear_v1(&values);
    } else if (ast_args(work, node, "ASTSubst", args, 3u)) {
        Atom *fields[] = {pattern(work, args[0]), pattern(work, args[1]), path(work, args[2])};
        out = module_make_v1(work->arena, "ModuleNamedSubstitutionV1", fields, 3u);
    }
    --work->depth;
    if (!out) module_fail_v1(work, "malformed-pattern", "Expected a declaration pattern.");
    return out;
}

static Atom *law(ModuleWorkV1 *work, Atom *node, bool rewrite) {
    Atom *typed = core_declaration(work, node, rewrite ? 2u : 1u);
    if (typed) return typed;
    ModuleValuesV1 premises = {0};
    Atom *site = module_form_v1(node, "LbnfAstNodeV1", 4u) ? node->expr.elems[3] : NULL;
    Atom *name = NULL, *args[3], *out = NULL;
    if (rewrite) {
        if (!ast_args(work, node, "RDecl", args, 2u)) goto done;
        name = module_text_v1(work, args[0]); node = args[1];
    }
    while (ast_args(work, node, rewrite ? "RewriteContext" : "EquationFresh", args, rewrite ? 2u : 3u)) {
        Atom *premise = NULL;
        if (rewrite) {
            Atom *hyp[2];
            if (!ast_args(work, args[0], "Hyp", hyp, 2u)) goto done;
            Atom *fields[] = {path(work, hyp[0]), path(work, hyp[1])};
            premise = module_make_v1(work->arena, "ModuleCongruenceNamesV1", fields, 2u);
            node = args[1];
        } else {
            Atom *fields[] = {module_text_v1(work, args[0]), module_text_v1(work, args[1])};
            premise = module_make_v1(work->arena, "ModuleFreshNamesV1", fields, 2u);
            node = args[2];
        }
        if (!module_push_v1(work, &premises, premise)) goto done;
    }
    if (!ast_args(work, node, rewrite ? "RewriteBase" : "EquationImpl", args, 2u)) goto done;
    Atom *key = module_site_v1(work, site, atom_symbol(work->arena, rewrite ? "rewrite" : "equation"));
    if (!key) goto done;
    if (!rewrite) {
        char generated[64]; snprintf(generated, sizeof(generated), "equation:%lld", (long long)key->ground.ival);
        name = atom_string(work->arena, generated);
    }
    Atom *fields[] = {name, module_list_v1(work->arena, &premises), pattern(work, args[0]), pattern(work, args[1])};
    Atom *body = module_make_v1(work->arena, rewrite ? "ModuleRewriteV1" : "ModuleEquationV1", fields, 4u);
    Atom *located[] = {key, body};
    out = module_make_v1(work->arena, "ModuleDeclarationV1", located, 2u);
done:
    module_clear_v1(&premises);
    if (!out) module_fail_v1(work, "malformed-law", "Expected an equation or named rewrite declaration.");
    return out;
}

static Atom *builder(ModuleWorkV1 *work, Atom *node);
static Atom *project_rows(ModuleWorkV1 *work, Atom *node, unsigned kind) {
    ModuleValuesV1 values = {0};
    Atom *list = ast_list(node), *out = NULL;
    while (module_form_v1(list, "LCons", 2u)) {
        Atom *value = list->expr.elems[1], *row = NULL, *args[4];
        if (kind == 0u) row = definition(work, value);
        else if (kind == 1u || kind == 2u) row = law(work, value, kind == 2u);
        else if (kind == 3u) {
            if (ast_args(work, value, "BaseExport", args, 1u)) {
                Atom *type = category(work, args[0]);
                Atom *fields[] = {type, type};
                row = module_make_v1(work->arena, "ModuleExportV1", fields, 2u);
            } else if (ast_args(work, value, "RenameExport", args, 2u)) {
                Atom *fields[] = {category(work, args[0]), category(work, args[1])};
                row = module_make_v1(work->arena, "ModuleExportV1", fields, 2u);
            }
            if (row) {
                Atom *located[] = {module_site_v1(work, value->expr.elems[3], atom_symbol(work->arena, "export")), row};
                row = module_make_v1(work->arena, "ModuleDeclarationV1", located, 2u);
            }
        } else if (kind == 4u) {
            if (ast_args(work, value, "SimpleRepl", args, 4u)) {
                Atom *ints[1];
                ModuleValuesV1 indices = {0};
                if (ast_args(work, args[0], "Ints", ints, 1u)) {
                    Atom *numbers = ast_list(ints[0]);
                    while (module_form_v1(numbers, "LCons", 2u)) {
                        Atom *text = module_text_v1(work, numbers->expr.elems[1]);
                        char *end = NULL;
                        unsigned long long n = text ? strtoull(text->ground.sval, &end, 10) : UINT64_MAX;
                        if (!text || !end || *end || n > UINT32_MAX || text->ground.sval[0] == '-' ||
                            !module_push_v1(work, &indices, atom_int(work->arena, (int64_t)n))) break;
                        numbers = numbers->expr.elems[2];
                    }
                    if (atom_is_symbol(numbers, "LNil")) {
                        Atom *fields[] = {module_list_v1(work->arena, &indices), label(work, args[1]),
                            category(work, args[2]), definition(work, args[3])};
                        row = module_make_v1(work->arena, "ModuleReplacementV1", fields, 4u);
                    }
                }
                module_clear_v1(&indices);
            }
        } else if (kind == 5u) row = builder(work, value);
        else if (kind == 7u) row = core_declaration(work, value, 3u);
        else if (kind == 6u && ast_args(work, value, "VarDecl", args, 2u)) {
            Atom *fields[] = {module_text_v1(work, args[0]), path(work, args[1])};
            row = module_make_v1(work->arena, "ModuleParameterV1", fields, 2u);
        }
        if (!row || !module_push_v1(work, &values, row)) goto done;
        list = list->expr.elems[2];
    }
    if (atom_is_symbol(list, "LNil")) out = module_list_v1(work->arena, &values);
done:
    module_clear_v1(&values);
    if (!out) module_fail_v1(work, "malformed-declarations", "Expected a finite list of module declarations.");
    return out;
}

static Atom *builder(ModuleWorkV1 *work, Atom *node) {
    if (!module_spend_v1(work) || work->depth >= 512u) {
        module_fail_v1(work, "incomplete", "Theory builder structural depth exhausted."); return NULL;
    }
    ++work->depth;
    Atom *args[3], *out = NULL;
    if (ast_args(work, node, "TheoryInstEmpty", args, 0u)) out = atom_symbol(work->arena, "ModuleEmptyV1");
    else if (ast_args(work, node, "TheoryInstRef", args, 1u)) {
        Atom *name = module_text_v1(work, args[0]);
        out = module_make_v1(work->arena, "ModuleRefV1", &name, 1u);
    } else if (ast_args(work, node, "TheoryInstCtor", args, 2u)) {
        Atom *fields[] = {path(work, args[0]), project_rows(work, args[1], 5u)};
        out = module_make_v1(work->arena, "ModuleApplyV1", fields, 2u);
    } else if (ast_args(work, node, "TheoryInstFree", args, 1u)) {
        Atom *name = path(work, args[0]);
        out = module_make_v1(work->arena, "ModuleFreeV1", &name, 1u);
    } else if (ast_args(work, node, "TheoryInstAddTypes", args, 2u)) {
        Atom *fields[] = {builder(work, args[0]), project_rows(work, args[1], 7u)};
        out = module_make_v1(work->arena, "ModuleAddTypesV1", fields, 2u);
    } else if (ast_args(work, node, "TheoryInstRec", args, 3u)) {
        Atom *fields[] = {module_text_v1(work, args[0]), builder(work, args[1]), builder(work, args[2])};
        out = module_make_v1(work->arena, "ModuleLetV1", fields, 3u);
    } else {
        static const char *labels[] = {"TheoryInstAddTerms", "TheoryInstAddEquations", "TheoryInstAddRewrites",
            "TheoryInstAddExports", "TheoryInstAddReplacements", "TheoryInstConj", "TheoryInstDisj", "TheoryInstSubtract"};
        static const char *heads[] = {"ModuleAddTermsV1", "ModuleAddEquationsV1", "ModuleAddRewritesV1",
            "ModuleAddExportsV1", "ModuleReplaceV1", "ModuleMeetV1", "ModuleJoinV1", "ModuleDifferenceV1"};
        for (size_t i = 0; i < 8u; ++i) if (ast_args(work, node, labels[i], args, 2u)) {
            Atom *right = args[1];
            if (i == 0u) {
                Atom *grammar[1];
                if (!ast_args(work, right, "MkGrammar", grammar, 1u)) break;
                right = grammar[0];
            }
            Atom *base = builder(work, args[0]);
            Atom *extension = i < 5u ? project_rows(work, right, (unsigned)i) : builder(work, right);
            Atom *fields[] = {base, extension};
            out = module_make_v1(work->arena, heads[i], fields, 2u);
            break;
        }
    }
    --work->depth;
    if (!out) module_fail_v1(work, "malformed-builder", "Expected a finite theory builder expression.");
    return out;
}

static Atom *name(ModuleWorkV1 *work, Atom *node) {
    Atom *args[1];
    if (ast_args(work, node, "NameVar", args, 1u)) return module_text_v1(work, args[0]);
    if (ast_args(work, node, "NameWildcard", args, 0u)) return atom_string(work->arena, "_");
    return NULL;
}

bool cetta_module_value_v1_project(Arena *arena, Atom *source, Atom *ast,
    uint32_t limit, Atom **out, Atom **refusal) {
    if (!arena || !out || !refusal) return false;
    ModuleWorkV1 work = {.arena = arena, .remaining = limit, .source = source};
    ModuleValuesV1 imports = {0}, theories = {0}, entries = {0}, others = {0};
    Atom *args[3], *result = NULL;
    if (!module_spend_v1(&work) || !module_string_v1(source) || !ast_args(&work, ast, "ModuleImpl", args, 3u)) {
        module_fail_v1(&work, "malformed-module", "Expected a source namespace and a labelled Module tree."); goto done;
    }
    Atom *module_name = name(&work, args[1]);
    Atom *list = ast_list(args[0]);
    while (module_form_v1(list, "LCons", 2u)) {
        Atom *row[2], *import = NULL;
        if (ast_args(&work, list->expr.elems[1], "ImportModuleAs", row, 2u)) {
            Atom *fields[] = {module_text_v1(&work, row[0]), module_text_v1(&work, row[1])};
            import = module_make_v1(arena, "ModuleImportAsV1", fields, 2u);
        } else if (ast_args(&work, list->expr.elems[1], "ImportFromModule", row, 2u)) {
            Atom *fields[] = {module_text_v1(&work, row[0]), module_text_v1(&work, row[1])};
            import = module_make_v1(arena, "ModuleImportFromV1", fields, 2u);
        }
        if (!module_push_v1(&work, &imports, import)) goto done;
        list = list->expr.elems[2];
    }
    if (!atom_is_symbol(list, "LNil")) goto done;
    list = ast_list(args[2]);
    while (module_form_v1(list, "LCons", 2u)) {
        Atom *node = list->expr.elems[1], *row[3];
        if (ast_args(&work, node, "ProgTheoryDecl", row, 1u)) {
            if (!ast_args(&work, row[0], "BaseTheoryDecl", row, 3u)) goto done;
            Atom *fields[] = {name(&work, row[0]), project_rows(&work, row[1], 6u), builder(&work, row[2])};
            Atom *theory = module_make_v1(arena, "ModuleTheoryV1", fields, 3u);
            for (size_t i = 0; i < theories.len; ++i)
                if (theory && atom_eq(theories.items[i]->expr.elems[1], fields[0])) {
                    module_fail_v1(&work, "duplicate-theory", "A module cannot declare one theory name twice."); goto done;
                }
            if (!module_push_v1(&work, &theories, theory)) goto done;
        } else if (ast_args(&work, node, "ProgTheoryInst", row, 1u)) {
            if (!module_push_v1(&work, &entries, builder(&work, row[0]))) goto done;
        } else {
            /* Other source programs remain explicitly available to later
             * consumers. The language-definition frontend never runs them. */
            if (!module_push_v1(&work, &others, atom_deep_copy(arena, node))) goto done;
        }
        list = list->expr.elems[2];
    }
    if (!module_name || !atom_is_symbol(list, "LNil")) goto done;
    Atom *fields[] = {atom_deep_copy(arena, source), module_name,
        module_list_v1(arena, &imports), module_list_v1(arena, &theories),
        module_list_v1(arena, &entries), module_list_v1(arena, &others), module_list_v1(arena, &work.sites)};
    result = module_make_v1(arena, "ModuleValueV1", fields, 7u);
done:
    module_clear_v1(&imports); module_clear_v1(&theories); module_clear_v1(&entries); module_clear_v1(&others);
    module_clear_v1(&work.sites);
    if (!result) {
        module_fail_v1(&work, "malformed-module", "Malformed module declaration structure.");
        *refusal = atom_deep_copy(arena, work.refusal);
        return false;
    }
    *out = atom_deep_copy(arena, result);
    return *out != NULL;
}

bool cetta_module_value_v1_finish_read(Arena *arena, Atom *source, Atom *result,
    uint32_t limit, Atom **out, Atom **refusal) {
    if (!arena || !out || !refusal || !result) return false;
    bool evidence = module_form_v1(result, "LangDef:AuthoredParseEvidenceAccepted", 6u);
    bool accepted = module_form_v1(result, "LangDef:AuthoredParseAccepted", 4u);
    bool ordinary = module_form_v1(result, "ParserAcceptedV1", 2u);
    if (!accepted && !evidence && !ordinary) { *out = atom_deep_copy(arena, result); return true; }
    Atom *values = result->expr.elems[ordinary ? 1u : evidence ? 6u : 4u];
    if (!values || values->kind != ATOM_EXPR || values->expr.len != 1u) {
        Atom *fields[] = {atom_deep_copy(arena, result)};
        *out = module_make_v1(arena, "ModuleAmbiguousReadV1", fields, 1u);
        return *out != NULL;
    }
    return cetta_module_value_v1_project(arena, source, values->expr.elems[0], limit, out, refusal);
}

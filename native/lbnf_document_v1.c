#include "lbnf_document_v1.h"

#include "deterministic_equation_plan_v1.h"
#include "parser_token_profile_v1.h"
#include "regular_token_compiler_v1.h"
#include "regular_span_dfa_v1.h"
#include "src/parser.h"
#include "src/lib_parse_native_grammar.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const uint8_t cetta_lbnf_bootstrap_v1_source[];
extern const size_t cetta_lbnf_bootstrap_v1_source_len;
extern const uint8_t cetta_bnfc_lexical_policy_v1_source[];
extern const size_t cetta_bnfc_lexical_policy_v1_source_len;

typedef struct {
    Atom **items;
    size_t len, capacity;
} Values;

typedef struct {
    Arena *arena;
    uint32_t remaining;
    Atom *site;
    Atom *refusal;
    Atom *namespace_value;
    Atom *policy;
    bool c_flex;
    Values rules, metadata, origins, types, type_origins, declarations, comments, entries;
    Values defined, referenced, custom_tokens, macro_categories, macro_indices, rule_sites;
    CettaDeterministicEquationPlanV1 *lexical;
} Lower;

static bool form(Atom *a, const char *head, uint32_t arity) {
    return a && a->kind == ATOM_EXPR && a->expr.len == arity + 1u &&
        atom_is_symbol(a->expr.elems[0], head);
}

static bool string(Atom *a) {
    return a && a->kind == ATOM_GROUNDED && a->ground.gkind == GV_STRING &&
        a->ground.sval;
}

static bool natural(Atom *a, uint32_t *n) {
    if (!a || a->kind != ATOM_GROUNDED || a->ground.gkind != GV_INT ||
        a->ground.ival < 0 || a->ground.ival > UINT32_MAX) return false;
    *n = (uint32_t)a->ground.ival; return true;
}

static bool boolean(Atom *value, bool *out) {
    if (value && value->kind == ATOM_GROUNDED && value->ground.gkind == GV_BOOL) {
        *out = value->ground.bval; return true;
    }
    if (atom_is_symbol(value, "True") || atom_is_symbol(value, "true")) { *out = true; return true; }
    if (atom_is_symbol(value, "False") || atom_is_symbol(value, "false")) { *out = false; return true; }
    return false;
}

static Atom *make(Arena *a, const char *head, Atom *const *args, size_t len) {
    Atom **items = arena_alloc(a, (len + 1u) * sizeof(*items));
    items[0] = atom_symbol(a, head);
    for (size_t i = 0u; i < len; ++i) items[i + 1u] = args[i];
    return atom_expr(a, items, len + 1u);
}

static bool fail(Lower *l, const char *reason, const char *detail) {
    if (!l->refusal) {
        Atom *args[] = {atom_symbol(l->arena, reason),
            l->site ? atom_deep_copy(l->arena, l->site) : atom_symbol(l->arena, "LbnfNoSourceSiteV1"),
            atom_string(l->arena, detail)};
        l->refusal = make(l->arena, "LbnfRefusedV1", args, 3u);
    }
    return false;
}

static bool spend(Lower *l) {
    if (!l->remaining) return fail(l, "resource-limit", "LBNF structural work limit");
    --l->remaining; return true;
}

static bool push(Lower *l, Values *v, Atom *a) {
    if (!spend(l)) return false;
    if (v->len == v->capacity) {
        size_t cap = v->capacity ? 2u * v->capacity : 16u;
        if (cap < v->capacity || cap > SIZE_MAX / sizeof(*v->items))
            return fail(l, "resource-limit", "LBNF declaration allocation size");
        Atom **items = realloc(v->items, cap * sizeof(*items));
        if (!items) return fail(l, "allocation-failure", "LBNF declaration allocation failed");
        v->items = items; v->capacity = cap;
    }
    v->items[v->len++] = a; return true;
}

static bool has(Values *v, Atom *a) {
    for (size_t i = 0u; i < v->len; ++i) if (atom_eq(v->items[i], a)) return true;
    return false;
}

static bool unique(Lower *l, Values *v, Atom *a) {
    return has(v, a) || push(l, v, a);
}

static Atom *list(Arena *a, Values *v) {
    Atom *out = atom_symbol(a, "LNil");
    for (size_t i = v->len; i > 0u; --i)
        out = atom_expr3(a, atom_symbol(a, "LCons"), v->items[i - 1u], out);
    return out;
}

static void clear(Values *v) { free(v->items); memset(v, 0, sizeof(*v)); }

static void close_lower(Lower *l) {
    clear(&l->rules); clear(&l->metadata); clear(&l->origins);
    clear(&l->types); clear(&l->type_origins); clear(&l->declarations);
    clear(&l->comments); clear(&l->entries); clear(&l->defined);
    clear(&l->referenced); clear(&l->custom_tokens);
    clear(&l->macro_categories); clear(&l->macro_indices);
    clear(&l->rule_sites);
    cetta_deterministic_equation_plan_v1_free(l->lexical);
}

/* Scalar text is retained for literals, including NUL. Names alone use the
 * NUL-free string domain of the existing LanguageDef declaration interface. */
static Atom *text_string(Lower *l, Atom *text) {
    if (string(text)) return text;
    uint32_t count = 0u;
    for (Atom *p = text; form(p, "bnf-v1:text-cons", 2u); p = p->expr.elems[2]) {
        uint32_t cp;
        if (!natural(p->expr.elems[1], &cp) || !cp || cp > 0x10ffffu ||
            (cp >= 0xd800u && cp <= 0xdfffu) || !spend(l)) return NULL;
        if (count > (UINT32_MAX - 4u)) return NULL;
        count += cp < 128u ? 1u : cp < 2048u ? 2u : cp < 65536u ? 3u : 4u;
    }
    char *bytes = arena_alloc(l->arena, (size_t)count + 1u);
    uint32_t pos = 0u;
    Atom *p = text;
    for (; form(p, "bnf-v1:text-cons", 2u); p = p->expr.elems[2]) {
        uint32_t cp = (uint32_t)p->expr.elems[1]->ground.ival;
        if (cp < 128u) bytes[pos++] = (char)cp;
        else if (cp < 2048u) {
            bytes[pos++] = (char)(0xc0u | cp >> 6u); bytes[pos++] = (char)(0x80u | (cp & 63u));
        } else if (cp < 65536u) {
            bytes[pos++] = (char)(0xe0u | cp >> 12u);
            bytes[pos++] = (char)(0x80u | ((cp >> 6u) & 63u)); bytes[pos++] = (char)(0x80u | (cp & 63u));
        } else {
            bytes[pos++] = (char)(0xf0u | cp >> 18u); bytes[pos++] = (char)(0x80u | ((cp >> 12u) & 63u));
            bytes[pos++] = (char)(0x80u | ((cp >> 6u) & 63u)); bytes[pos++] = (char)(0x80u | (cp & 63u));
        }
    }
    if (!form(p, "bnf-v1:text-nil", 0u)) return NULL;
    bytes[pos] = '\0'; return atom_string(l->arena, bytes);
}

static Atom *scalar_text(Lower *l, Atom *text) {
    if (!string(text)) return text;
    CettaLpNativeUtf8ScalarBuffer buffer;
    cetta_lp_native_utf8_scalar_buffer_init(&buffer);
    char error[256];
    if (!cetta_lp_native_utf8_scalar_buffer_decode(&buffer, (const uint8_t *)text->ground.sval,
            strlen(text->ground.sval), error, sizeof(error))) {
        cetta_lp_native_utf8_scalar_buffer_free(&buffer); return NULL;
    }
    Atom *out = make(l->arena, "bnf-v1:text-nil", NULL, 0u);
    for (uint32_t i = buffer.view.scalar_len; i > 0u; --i)
        out = atom_expr3(l->arena, atom_symbol(l->arena, "bnf-v1:text-cons"),
            atom_int(l->arena, cetta_lp_native_utf8_scalar_view_scalar_at(&buffer.view, i - 1u)), out);
    cetta_lp_native_utf8_scalar_buffer_free(&buffer); return out;
}

static Atom *ast_children(Atom *node, const char *label) {
    return form(node, "LbnfAstNodeV1", 4u) && string(node->expr.elems[2]) &&
        !strcmp(node->expr.elems[2]->ground.sval, label) ? node->expr.elems[4] : NULL;
}

static bool args(Atom *children, Atom **out, size_t count) {
    for (size_t i = 0u; i < count; ++i) {
        if (!form(children, "LCons", 2u)) return false;
        out[i] = children->expr.elems[1]; children = children->expr.elems[2];
    }
    return atom_is_symbol(children, "LNil");
}

static Atom *token_text(Atom *node) {
    return form(node, "LbnfAstTokenV1", 6u) ? node->expr.elems[3] : NULL;
}

static Atom *cat_from_ast(Lower *l, Atom *node, uint32_t depth) {
    Atom *children[1];
    if (!spend(l) || depth > 512u) return NULL;
    if (form(node, "LbnfAstTokenV1", 6u)) return text_string(l, token_text(node));
    if (args(ast_children(node, "IdCat"), children, 1u))
        return text_string(l, token_text(children[0]));
    if (args(ast_children(node, "ListCat"), children, 1u) || args(ast_children(node, "ListOfCat"), children, 1u)) {
        Atom *element = cat_from_ast(l, children[0], depth + 1u);
        if (!string(element)) return NULL;
        size_t n = strlen(element->ground.sval);
        char *name = arena_alloc(l->arena, n + 3u);
        name[0] = '['; memcpy(name + 1u, element->ground.sval, n);
        name[n + 1u] = ']'; name[n + 2u] = '\0'; return atom_string(l->arena, name);
    }
    return NULL;
}

static Atom *label_from_ast(Lower *l, Atom *node) {
    Atom *child[1];
    if (args(ast_children(node, "Id"), child, 1u)) return text_string(l, token_text(child[0]));
    const char *labels[] = {"Wild", "ListE", "ListCons", "ListOne"};
    const char *values[] = {"_", "[]", "(:)", "(:[])"};
    for (size_t i = 0u; i < 4u; ++i)
        if (args(ast_children(node, labels[i]), child, 0u)) return atom_string(l->arena, values[i]);
    return NULL;
}

static Atom *items_from_ast(Lower *l, Atom *node) {
    if (!form(node, "LbnfAstListV1", 3u)) return NULL;
    Values values = {0};
    Atom *rows = node->expr.elems[3], *out = NULL;
    for (; form(rows, "LCons", 2u); rows = rows->expr.elems[2]) {
        Atom *item = rows->expr.elems[1], *child[1], *value = NULL;
        const char *head = NULL;
        if (args(ast_children(item, "Terminal"), child, 1u)) {
            value = token_text(child[0]); head = "LbnfTerminalV1";
        } else if (args(ast_children(item, "NTerminal"), child, 1u)) {
            value = cat_from_ast(l, child[0], 0u); head = "LbnfNonTerminalV1";
        }
        if (!value || !push(l, &values, make(l->arena, head, &value, 1u))) goto done;
    }
    if (!atom_is_symbol(rows, "LNil")) goto done;
    out = list(l->arena, &values);
done:
    clear(&values); return out;
}

static Atom *regex_from_ast(Lower *l, Atom *node, uint32_t depth) {
    if (!spend(l) || depth > 512u) return NULL;
    static const char *const labels[] = {
        "RAlt", "RMinus", "RSeq", "RStar", "RPlus", "ROpt", "REps",
        "RChar", "RAlts", "RSeqs", "RDigit", "RLetter", "RUpper", "RLower", "RAny"};
    static const char *const heads[] = {
        "LbnfRegAltV1", "LbnfRegMinusV1", "LbnfRegSeqV1", "LbnfRegStarV1",
        "LbnfRegPlusV1", "LbnfRegOptionalV1", "LbnfRegEpsilonV1",
        "LbnfRegCharV1", "LbnfRegAlternativesV1", "LbnfRegSequenceV1",
        "LbnfRegDigitV1", "LbnfRegLetterV1", "LbnfRegUpperV1", "LbnfRegLowerV1", "LbnfRegAnyV1"};
    for (size_t i = 0u; i < 15u; ++i) {
        Atom *children[2];
        size_t count = i < 3u ? 2u : i < 6u || (i >= 7u && i <= 9u) ? 1u : 0u;
        if (!args(ast_children(node, labels[i]), children, count)) continue;
        for (size_t j = 0u; j < count; ++j) {
            children[j] = i >= 7u && i <= 9u ? token_text(children[j])
                : regex_from_ast(l, children[j], depth + 1u);
            if (!children[j]) return NULL;
        }
        return make(l->arena, heads[i], children, count);
    }
    return NULL;
}

static Atom *definition_from_ast(Lower *l, Atom *node) {
    Atom *children[6], *body = NULL;
    bool internal = args(ast_children(node, "Internal"), children, 3u);
    if (internal || args(ast_children(node, "Rule"), children, 3u)) {
        Atom *values[] = {label_from_ast(l, children[0]), cat_from_ast(l, children[1], 0u),
            items_from_ast(l, children[2]), atom_bool(l->arena, internal)};
        if (!values[0] || !values[1] || !values[2]) return NULL;
        body = make(l->arena, "LbnfRuleV1", values, 4u);
    } else if (args(ast_children(node, "Comment"), children, 1u)) {
        Atom *value = token_text(children[0]);
        if (value) body = make(l->arena, "LbnfLineCommentV1", &value, 1u);
    } else if (args(ast_children(node, "Comments"), children, 2u)) {
        Atom *values[] = {token_text(children[0]), token_text(children[1])};
        if (values[0] && values[1]) body = make(l->arena, "LbnfBlockCommentV1", values, 2u);
    } else if (args(ast_children(node, "Token"), children, 2u) ||
               args(ast_children(node, "PosToken"), children, 2u)) {
        Atom *values[] = {text_string(l, token_text(children[0])), regex_from_ast(l, children[1], 0u),
            atom_bool(l->arena, ast_children(node, "PosToken") != NULL)};
        if (values[0] && values[1]) body = make(l->arena, "LbnfTokenV1", values, 3u);
    } else if (args(ast_children(node, "Separator"), children, 3u) ||
               args(ast_children(node, "Terminator"), children, 3u)) {
        Atom *dummy[1];
        bool nonempty = args(ast_children(children[0], "MNonempty"), dummy, 0u);
        if (!nonempty && !args(ast_children(children[0], "MEmpty"), dummy, 0u)) return NULL;
        Atom *values[] = {cat_from_ast(l, children[1], 0u), token_text(children[2]),
            atom_bool(l->arena, nonempty)};
        if (values[0] && values[1]) body = make(l->arena,
            ast_children(node, "Separator") ? "LbnfSeparatorV1" : "LbnfTerminatorV1", values, 3u);
    } else if (args(ast_children(node, "Coercions"), children, 2u)) {
        Atom *count = text_string(l, token_text(children[1]));
        if (!string(count)) return NULL;
        char *end;
        unsigned long n = strtoul(count->ground.sval, &end, 10);
        if (*end || n > UINT32_MAX) return NULL;
        Atom *values[] = {text_string(l, token_text(children[0])), atom_int(l->arena, n)};
        if (values[0]) body = make(l->arena, "LbnfCoercionsV1", values, 2u);
    } else if (args(ast_children(node, "Entryp"), children, 1u)) {
        if (!form(children[0], "LbnfAstListV1", 3u)) return NULL;
        Values values = {0};
        Atom *rows = children[0]->expr.elems[3];
        for (; form(rows, "LCons", 2u); rows = rows->expr.elems[2]) {
            Atom *cat = cat_from_ast(l, rows->expr.elems[1], 0u);
            if (!cat || !push(l, &values, cat)) { clear(&values); return NULL; }
        }
        Atom *cats = list(l->arena, &values); clear(&values);
        if (atom_is_symbol(rows, "LNil")) body = make(l->arena, "LbnfEntrypointsV1", &cats, 1u);
    } else if (args(ast_children(node, "Rules"), children, 2u)) {
        if (!form(children[1], "LbnfAstListV1", 3u)) return NULL;
        Values values = {0};
        Atom *rows = children[1]->expr.elems[3];
        for (; form(rows, "LCons", 2u); rows = rows->expr.elems[2]) {
            Atom *rhs[1];
            if (!args(ast_children(rows->expr.elems[1], "RHS"), rhs, 1u) &&
                !args(ast_children(rows->expr.elems[1], "MkRHS"), rhs, 1u)) { clear(&values); return NULL; }
            Atom *items = items_from_ast(l, rhs[0]);
            if (!items || !push(l, &values, items)) { clear(&values); return NULL; }
        }
        Atom *alternatives = list(l->arena, &values); clear(&values);
        Atom *values2[] = {text_string(l, token_text(children[0])), alternatives};
        if (values2[0] && atom_is_symbol(rows, "LNil")) body = make(l->arena, "LbnfRulesV1", values2, 2u);
    } else if (ast_children(node, "Layout") || ast_children(node, "LayoutStop") ||
               ast_children(node, "LayoutTop") || ast_children(node, "Function") ||
               ast_children(node, "Delimiters")) {
        Atom *label = node->expr.elems[2];
        Atom *values[] = {label, atom_deep_copy(l->arena, node)};
        body = make(l->arena, "LbnfPragmaV1", values, 2u);
    }
    if (!body || !form(node, "LbnfAstNodeV1", 4u)) return NULL;
    Atom *values[] = {atom_deep_copy(l->arena, node->expr.elems[3]), body};
    return make(l->arena, "LbnfDefinitionV1", values, 2u);
}

bool cetta_lbnf_definition_v1_project(
    Arena *arena, Atom *ast, uint32_t work_limit,
    Atom **out, Atom **refusal) {
    if (!arena || !out || !refusal) return false;
    Lower lower = {.arena = arena, .remaining = work_limit,
        .site = form(ast, "LbnfAstNodeV1", 4u) ? ast->expr.elems[3] : NULL};
    Atom *definition = definition_from_ast(&lower, ast);
    if (!definition) {
        fail(&lower, "malformed-definition", "Expected a labelled LBNF declaration.");
        *refusal = atom_deep_copy(arena, lower.refusal);
        return false;
    }
    Atom *owned = atom_deep_copy(arena, definition);
    if (!owned) return false;
    *out = owned;
    return true;
}

bool cetta_lbnf_document_v1_project(
    Arena *arena, Atom *namespace_value, Atom *ast, uint32_t work_limit,
    Atom **out, Atom **refusal) {
    Lower l = {.arena = arena, .remaining = work_limit};
    Values definitions = {0};
    bool ok = false;
    Atom *children[1];
    if (!arena || !out || !refusal) return false;
    if (!string(namespace_value) || !namespace_value->ground.sval[0] ||
        !args(ast_children(ast, "Grammar"), children, 1u) ||
        !form(children[0], "LbnfAstListV1", 3u)) {
        fail(&l, "malformed-document", "expected a labelled LBNF Grammar AST and source namespace"); goto done;
    }
    Atom *rows = children[0]->expr.elems[3];
    for (; form(rows, "LCons", 2u); rows = rows->expr.elems[2]) {
        Atom *node = rows->expr.elems[1];
        l.site = form(node, "LbnfAstNodeV1", 4u) ? node->expr.elems[3] : NULL;
        Atom *definition = definition_from_ast(&l, node);
        if (!definition || !push(&l, &definitions, definition)) {
            fail(&l, "malformed-definition", "LBNF AST declaration has the wrong constructor signature"); goto done;
        }
    }
    if (!atom_is_symbol(rows, "LNil")) {
        fail(&l, "malformed-document", "LBNF AST definitions are not a finite list"); goto done;
    }
    Atom *values[] = {atom_deep_copy(arena, namespace_value), list(arena, &definitions)};
    *out = make(arena, "LbnfDocumentV1", values, 2u); ok = true;
done:
    if (!ok && refusal) *refusal = l.refusal;
    clear(&definitions); return ok;
}

bool cetta_lbnf_document_v1_finish_read(
    Arena *arena, Atom *result, uint32_t work_limit,
    Atom **out, Atom **refusal) {
    if (!arena || !result || !out || !refusal) return false;
    bool evidence = form(result, "LangDef:AuthoredParseEvidenceAccepted", 6u);
    bool accepted = form(result, "LangDef:AuthoredParseAccepted", 4u);
    if (!evidence && !accepted) {
        *out = atom_deep_copy(arena, result);
        return true;
    }
    Atom *asts = result->expr.elems[evidence ? 6u : 4u];
    if (!asts || asts->kind != ATOM_EXPR || asts->expr.len != 1u) {
        *out = atom_deep_copy(arena, result);
        return true;
    }
    Atom *document = NULL;
    if (!cetta_lbnf_document_v1_project(arena, result->expr.elems[3],
            asts->expr.elems[0], work_limit, &document, refusal)) return false;
    Atom *values[5] = {
        atom_deep_copy(arena, result->expr.elems[2]),
        atom_deep_copy(arena, result->expr.elems[3]), document, NULL, NULL};
    if (evidence) {
        values[2] = atom_deep_copy(arena, result->expr.elems[4]);
        values[3] = atom_deep_copy(arena, result->expr.elems[5]);
        values[4] = document;
    }
    *out = make(arena, evidence ? "LbnfReadEvidenceV1" : "LbnfReadValueV1",
        values, evidence ? 5u : 3u);
    return true;
}

bool cetta_lbnf_document_v1_bootstrap(Arena *arena, Atom **out,
                                     char *error, size_t error_size) {
    if (!arena || !out) return false;
    char *source = arena_alloc(arena, cetta_lbnf_bootstrap_v1_source_len + 1u);
    memcpy(source, cetta_lbnf_bootstrap_v1_source, cetta_lbnf_bootstrap_v1_source_len);
    source[cetta_lbnf_bootstrap_v1_source_len] = '\0';
    Atom **values = NULL;
    int count = parse_metta_text(source, arena, &values);
    Atom *document = count == 1 ? values[0] : NULL;
    free(values);
    if (!form(document, "LbnfDocumentV1", 2u)) {
        if (error && error_size) snprintf(error, error_size, "LBNF library grammar data is malformed");
        return false;
    }
    *out = document; return true;
}

static Atom *origin(Lower *l, const char *kind, size_t index) {
    char site[96]; snprintf(site, sizeof(site), "%s:%zu", kind, index);
    Atom *values[] = {l->namespace_value, atom_string(l->arena, site)};
    return make(l->arena, "SyntaxOriginV1", values, 2u);
}

static bool builtin(const char *name) {
    return !strcmp(name, "Ident") || !strcmp(name, "Integer") || !strcmp(name, "Double") ||
           !strcmp(name, "String") || !strcmp(name, "Char");
}

static Atom *normalized(Lower *l, Atom *category) {
    if (!string(category)) return NULL;
    CettaLdTextV1 text = {(uint8_t *)category->ground.sval, (uint32_t)strlen(category->ground.sval)};
    return cetta_parser_token_category_v1_normalize(l->arena, &text);
}

static bool type(Lower *l, Atom *name, bool token) {
    if (!string(name) || !name->ground.sval[0]) return fail(l, "bad-category-name", "a category must have a name");
    for (size_t i = 0u; i < l->types.len; ++i) {
        Atom *row = l->types.items[i];
        if (atom_eq(row->expr.elems[1], name)) return true;
    }
    Atom *values[] = {name, atom_symbol(l->arena, token ? "CarrierTokenRaw" : "CarrierAst")};
    Atom *site[] = {atom_string(l->arena, "LBNF:category-signatures-v1"), name};
    return push(l, &l->types, make(l->arena, "TypeDecl", values, 2u)) &&
           push(l, &l->type_origins, make(l->arena, "SyntaxOriginV1", site, 2u));
}

static bool white(uint32_t cp) {
    return (cp >= 9u && cp <= 13u) || cp == 32u || cp == 160u || cp == 5760u ||
        (cp >= 8192u && cp <= 8202u) || cp == 8232u || cp == 8233u || cp == 8239u ||
        cp == 8287u || cp == 12288u;
}

static bool text_points(Lower *l, Atom *text, Values *points) {
    Atom *p = scalar_text(l, text);
    for (; form(p, "bnf-v1:text-cons", 2u); p = p->expr.elems[2]) {
        uint32_t cp;
        if (!natural(p->expr.elems[1], &cp) || cp > 0x10ffffu ||
            (cp >= 0xd800u && cp <= 0xdfffu) || !push(l, points, p->expr.elems[1])) return false;
    }
    return form(p, "bnf-v1:text-nil", 0u);
}

static Atom *points_text(Arena *arena, Values *points, size_t left, size_t right) {
    Atom *out = make(arena, "bnf-v1:text-nil", NULL, 0u);
    for (size_t i = right; i > left; --i) out = atom_expr3(arena,
        atom_symbol(arena, "bnf-v1:text-cons"), points->items[i - 1u], out);
    return out;
}

static bool terminal_items(Lower *l, Values *syntax, Atom *text) {
    Values points = {0}; bool ok = false;
    if (!text_points(l, text, &points)) goto done;
    for (size_t i = 0u; i < points.len;) {
        if (white((uint32_t)points.items[i]->ground.ival)) { ++i; continue; }
        size_t right = i + 1u;
        while (right < points.len && !white((uint32_t)points.items[right]->ground.ival)) ++right;
        Atom *word = points_text(l->arena, &points, i, right);
        Atom *value = text_string(l, word);
        /* The wire's scalar-text form retains counted strings with NUL. */
        if (!value) value = word;
        if (!push(l, syntax, make(l->arena, "SyntaxTerminal", &value, 1u))) goto done;
        i = right;
    }
    ok = true;
done:
    clear(&points); return ok;
}

static bool add_rule(Lower *l, Atom *label, Atom *category, Atom *items, bool internal) {
    Values parameters = {0}, syntax = {0}, signature = {0};
    bool ok = false;
    if (!string(label) || !label->ground.sval[0] || !string(category) || !type(l, category, false) ||
        !unique(l, &l->defined, category)) goto done;
    Atom *rows = items;
    for (; form(rows, "LCons", 2u); rows = rows->expr.elems[2]) {
        Atom *item = rows->expr.elems[1];
        if (form(item, "LbnfTerminalV1", 1u)) {
            if (!terminal_items(l, &syntax, item->expr.elems[1])) goto done;
        } else if (form(item, "LbnfNonTerminalV1", 1u) && string(item->expr.elems[1])) {
            Atom *sort = item->expr.elems[1];
            char name[64]; snprintf(name, sizeof(name), "argument:%zu", parameters.len);
            Atom *parameter_name = atom_string(l->arena, name);
            Atom *base = make(l->arena, "TBase", &sort, 1u);
            Atom *values[] = {parameter_name, base};
            if (!push(l, &parameters, make(l->arena, "TermSimple", values, 2u)) ||
                !push(l, &syntax, make(l->arena, "SyntaxNonTerminal", &parameter_name, 1u)) ||
                !push(l, &signature, normalized(l, sort)) ||
                !unique(l, &l->referenced, sort)) goto done;
        } else goto done;
    }
    if (!atom_is_symbol(rows, "LNil")) goto done;
    Atom *norm = normalized(l, category);
    if (!string(norm)) goto done;
    const char *name = label->ground.sval, *cat = norm->ground.sval;
    bool is_list = cat[0] == '[';
    bool special = !strcmp(name, "_") || !strcmp(name, "[]") || !strcmp(name, "(:)") || !strcmp(name, "(:[])");
    if (is_list && !special) {
        fail(l, "bad-list-formation", "List categories can only be formed by rules labelled _, [], (:), or (:[])."); goto done;
    }
    if (builtin(cat) && strcmp(name, "_")) {
        fail(l, "bad-special-category-rule", "A predefined token category cannot be constructed by a grammar rule."); goto done;
    }
    if (!strcmp(name, "_") && (signature.len != 1u || !atom_eq(signature.items[0], norm))) {
        fail(l, "bad-coercion", "A coercion must preserve exactly one normalized category."); goto done;
    }
    if (!strcmp(name, "[]") && (!is_list || signature.len)) {
        fail(l, "bad-empty-list", "An empty-list rule must construct a list and retain no categories."); goto done;
    }
    if (!strcmp(name, "(:)") || !strcmp(name, "(:[])")) {
        size_t n = strlen(cat);
        if (!is_list || n < 3u || cat[n - 1u] != ']') {
            fail(l, "bad-list-construction", "A list label must construct a list category."); goto done;
        }
        char *element = arena_alloc(l->arena, n - 1u);
        memcpy(element, cat + 1u, n - 2u); element[n - 2u] = '\0';
        size_t arity = !strcmp(name, "(:)") ? 2u : 1u;
        if (signature.len != arity || strcmp(signature.items[0]->ground.sval, element) ||
            (arity == 2u && !atom_eq(signature.items[1], norm))) {
            fail(l, "bad-list-construction", "The retained categories must be the element and, for cons, its list."); goto done;
        }
    }
    Atom *projection = NULL;
    if (!strcmp(name, "_")) projection = make(l->arena, "BnfcCoercionV1", &norm, 1u);
    else if (!strcmp(name, "[]")) projection = make(l->arena, "BnfcListEmptyV1", &norm, 1u);
    else if (!strcmp(name, "(:)")) projection = make(l->arena, "BnfcListConsV1", &norm, 1u);
    else if (!strcmp(name, "(:[])")) projection = make(l->arena, "BnfcListSingletonV1", &norm, 1u);
    else {
        Atom *values[] = {label, norm, list(l->arena, &signature)};
        projection = make(l->arena, "BnfcConstructorV1", values, 3u);
        for (size_t i = 0u; i < l->metadata.len; ++i) {
            Atom *previous = l->metadata.items[i]->expr.elems[3];
            if (!spend(l)) goto done;
            if (form(previous, "BnfcConstructorV1", 3u) &&
                atom_eq(previous->expr.elems[1], label) && !atom_eq(previous, projection)) {
                fail(l, "constructor-signature-conflict", "Repeated constructor labels require the same category skeleton."); goto done;
            }
        }
    }
    Atom *values[] = {label, category, list(l->arena, &parameters),
        list(l->arena, &syntax), atom_symbol(l->arena, "EvalNone")};
    Atom *site = origin(l, "production", l->rules.len);
    Atom *occurrence_args[] = {l->namespace_value, l->site, atom_int(l->arena, l->rules.len)};
    Atom *metadata_args[] = {make(l->arena, "LbnfProductionOccurrenceV1", occurrence_args, 3u),
        atom_symbol(l->arena, internal ? "ParserInternalV1" : "ParserVisibleV1"), projection};
    ok = push(l, &l->rules, make(l->arena, "GrammarRule", values, 5u)) &&
         push(l, &l->metadata, make(l->arena, "ParserProductionMetadataV1", metadata_args, 3u)) &&
         push(l, &l->origins, site) && push(l, &l->rule_sites, l->site);
done:
    clear(&parameters); clear(&syntax); clear(&signature);
    if (!ok) fail(l, "malformed-rule", "LBNF rule has a malformed label, category or item list");
    return ok;
}

static Atom *lexical_value(Lower *l, const char *head, Atom *argument) {
    Atom *arguments[] = {argument, l->policy->expr.elems[1]};
    bool builtin_call = !strcmp(head, "bnfc-v1:builtin");
    if (!builtin_call) arguments[0] = arguments[1];
    Atom *call = make(l->arena, head, arguments, builtin_call ? 2u : 1u);
    Atom *out = NULL; char error[512];
    CettaDeterministicEquationStatusV1 status;
    uint64_t used = 0u;
    if (!cetta_deterministic_equation_plan_v1_apply_counted(l->lexical, call, NULL, NULL,
            l->arena, 512u, l->remaining, &used, &out, &status, error, sizeof(error)) ||
        used > l->remaining) {
        fail(l, "lexical-policy", error); return NULL;
    }
    l->remaining -= (uint32_t)used; return out;
}

static Atom *regex_char(Lower *l, uint32_t cp) {
    Atom *point = atom_int(l->arena, cp);
    point = make(l->arena, "cp", &point, 1u);
    return make(l->arena, "rx-char", &point, 1u);
}

static Atom *regex_binary(Lower *l, const char *name, Atom *left, Atom *right) {
    if (!left || !right) return NULL;
    Atom *values[] = {left, right}; return make(l->arena, name, values, 2u);
}

static Atom *regex_literal(Lower *l, uint32_t cp) {
    if (!l->c_flex || cp < 128u) return regex_char(l, cp);
    /* C/Flex's generated regular expression is byte-oriented. Characters
     * above Latin-1 are emitted literally as UTF-8 by the pinned backend. */
    if (cp < 256u) {
        char decimal[4]; snprintf(decimal, sizeof(decimal), "%u", cp);
        uint32_t value = 0u;
        for (const char *p = decimal; *p; ++p) {
            if (*p > '7') {
                /* Flex takes the maximal octal prefix and then the remaining
                 * digits literally. */
                Atom *prefix = regex_char(l, value ? value : (uint32_t)*p);
                const char *rest = value ? p : p + 1u;
                for (; *rest; ++rest) prefix = regex_binary(l, "rx-seq", prefix, regex_char(l, *rest));
                return prefix;
            }
            value = 8u * value + (uint32_t)(*p - '0');
        }
        return regex_char(l, value);
    }
    uint8_t bytes[4]; uint32_t len;
    if (cp < 2048u) { len = 2u; bytes[0] = 0xc0u | (cp >> 6u); bytes[1] = 0x80u | (cp & 63u); }
    else if (cp < 65536u) {
        len = 3u; bytes[0] = 0xe0u | (cp >> 12u); bytes[1] = 0x80u | ((cp >> 6u) & 63u); bytes[2] = 0x80u | (cp & 63u);
    } else {
        len = 4u; bytes[0] = 0xf0u | (cp >> 18u); bytes[1] = 0x80u | ((cp >> 12u) & 63u);
        bytes[2] = 0x80u | ((cp >> 6u) & 63u); bytes[3] = 0x80u | (cp & 63u);
    }
    Atom *out = regex_char(l, bytes[0]);
    for (uint32_t i = 1u; i < len; ++i) out = regex_binary(l, "rx-seq", out, regex_char(l, bytes[i]));
    return out;
}

static Atom *regex(Lower *l, Atom *value, uint32_t depth) {
    if (!spend(l) || depth > 512u) return NULL;
    const char *source[] = {"LbnfRegAltV1", "LbnfRegMinusV1", "LbnfRegSeqV1"};
    const char *target[] = {"rx-alt", "rx-minus", "rx-seq"};
    for (size_t i = 0u; i < 3u; ++i) if (form(value, source[i], 2u)) {
        Atom *left = regex(l, value->expr.elems[1], depth + 1u);
        Atom *right = regex(l, value->expr.elems[2], depth + 1u);
        if (i == 1u && l->c_flex && form(value->expr.elems[1], "LbnfRegAnyV1", 0u)) {
            Atom *points[] = {make(l->arena, "cp", (Atom *[]){atom_int(l->arena, 0)}, 1u),
                             make(l->arena, "cp", (Atom *[]){atom_int(l->arena, 255)}, 1u)};
            left = make(l->arena, "rx-range", points, 2u);
        }
        return regex_binary(l, target[i], left, right);
    }
    if (form(value, "LbnfRegEpsilonV1", 0u)) return atom_symbol(l->arena, "rx-eps");
    if (form(value, "LbnfRegStarV1", 1u) || form(value, "LbnfRegPlusV1", 1u) ||
        form(value, "LbnfRegOptionalV1", 1u)) {
        Atom *child = regex(l, value->expr.elems[1], depth + 1u);
        if (!child) return NULL;
        Atom *star = make(l->arena, "rx-star", &child, 1u);
        return form(value, "LbnfRegStarV1", 1u) ? star :
            form(value, "LbnfRegPlusV1", 1u) ? regex_binary(l, "rx-seq", child, star) :
            regex_binary(l, "rx-alt", atom_symbol(l->arena, "rx-eps"), child);
    }
    const char *classes[] = {"LbnfRegDigitV1", "LbnfRegLetterV1", "LbnfRegUpperV1",
                            "LbnfRegLowerV1", "LbnfRegAnyV1"};
    const char *heads[] = {"bnfc-v1:digit", "bnfc-v1:letter", "bnfc-v1:upper", "bnfc-v1:lower", "bnfc-v1:any"};
    for (size_t i = 0u; i < 5u; ++i) {
        if (form(value, classes[i], 0u)) {
            if (!i) {
                Atom *points[] = {make(l->arena, "cp", (Atom *[]){atom_int(l->arena, 48)}, 1u),
                                 make(l->arena, "cp", (Atom *[]){atom_int(l->arena, 57)}, 1u)};
                return make(l->arena, "rx-range", points, 2u);
            }
            return lexical_value(l, heads[i], NULL);
        }
    }
    bool alternatives = form(value, "LbnfRegAlternativesV1", 1u);
    bool single = form(value, "LbnfRegCharV1", 1u);
    if (alternatives || single || form(value, "LbnfRegSequenceV1", 1u)) {
        Values points = {0}; Atom *out = NULL;
        if (!text_points(l, value->expr.elems[1], &points) || (single && points.len != 1u)) {
            clear(&points); return NULL;
        }
        out = atom_symbol(l->arena, alternatives ? "rx-empty" : "rx-eps");
        for (size_t i = 0u; i < points.len; ++i) {
            Atom *character = regex_literal(l, (uint32_t)points.items[i]->ground.ival);
            out = regex_binary(l, alternatives ? "rx-alt" : "rx-seq", out, character);
        }
        clear(&points); return out;
    }
    if (!l->refusal) {
        Atom *fields[] = {l->site, atom_deep_copy(l->arena, value)};
        Atom *witness = make(l->arena, "LbnfMalformedRegexWitnessV1", fields, 2u);
        Atom *values[] = {atom_symbol(l->arena, "malformed-regex"), witness,
                         atom_string(l->arena, "expected an LBNF regular-expression value")};
        l->refusal = make(l->arena, "LbnfRefusedV1", values, 3u);
    }
    return NULL;
}

static bool token_regex_valid(Lower *l, Atom *value) {
    PPABIV1Pack classes; ppabi_v1_pack_init(&classes);
    CettaRegularTokenCompilerV1 *compiler = NULL;
    RSDFAV1Program program; rsdfa_v1_program_init(&program);
    char error[512]; bool ok = false;
    if (!cetta_regular_token_compiler_v1_load(&classes, l->remaining, &compiler, error, sizeof(error)) ||
        !cetta_regular_token_compiler_v1_program(compiler, l->arena,
            atom_symbol(l->arena, "token-nullability-check"), value, &program, error, sizeof(error))) {
        fail(l, "token-regex", error); goto done;
    }
    if (program.states[program.start_state].accept_len) {
        fail(l, "nullable-token", "The following token accepts the empty string."); goto done;
    }
    bool accepts = false;
    for (uint32_t i = 0u; i < program.state_len; ++i) if (program.states[i].accept_len) accepts = true;
    if (!accepts) { fail(l, "empty-token", "The following token accepts nothing."); goto done; }
    ok = true;
done:
    rsdfa_v1_program_free(&program); cetta_regular_token_compiler_v1_free(compiler);
    ppabi_v1_pack_free(&classes); return ok;
}

static Atom *item(Lower *l, bool terminal, Atom *value) {
    return make(l->arena, terminal ? "LbnfTerminalV1" : "LbnfNonTerminalV1", &value, 1u);
}

static Atom *small_items(Lower *l, Atom *const *values, size_t count) {
    Atom *out = atom_symbol(l->arena, "LNil");
    for (size_t i = count; i > 0u; --i) out = atom_expr3(l->arena,
        atom_symbol(l->arena, "LCons"), values[i - 1u], out);
    return out;
}

static bool expand_list(Lower *l, Atom *category, Atom *separator, bool nonempty, bool terminator) {
    if (!string(category)) return false;
    size_t len = strlen(category->ground.sval);
    char *list_name = arena_alloc(l->arena, len + 3u);
    snprintf(list_name, len + 3u, "[%s]", category->ground.sval);
    Atom *target = atom_string(l->arena, list_name);
    Values points = {0};
    if (!text_points(l, separator, &points)) { clear(&points); return false; }
    bool all_white = true;
    for (size_t i = 0u; i < points.len; ++i) if (!white((uint32_t)points.items[i]->ground.ival)) all_white = false;
    if (all_white) terminator = true;
    clear(&points);
    Atom *element = item(l, false, category), *tail = item(l, false, target), *sep = item(l, true, separator);
    if (!nonempty && !add_rule(l, atom_string(l->arena, "[]"), target, atom_symbol(l->arena, "LNil"), false)) return false;
    if (!terminator || nonempty) {
        Atom *one[] = {element, sep};
        if (!add_rule(l, atom_string(l->arena, "(:[])"), target,
                small_items(l, one, terminator ? 2u : 1u), false)) return false;
    }
    Atom *cons[] = {element, sep, tail};
    return add_rule(l, atom_string(l->arena, "(:)"), target, small_items(l, cons, 3u), false);
}

static Atom *indexed_category(Lower *l, Atom *base, uint32_t index) {
    if (!string(base)) return NULL;
    size_t size = strlen(base->ground.sval) + 16u;
    char *name = arena_alloc(l->arena, size);
    snprintf(name, size, "%s%u", base->ground.sval, index); return atom_string(l->arena, name);
}

static bool expand_coercions(Lower *l, Atom *base, Atom *count) {
    uint32_t n;
    if (!string(base) || !natural(count, &n) || n > l->remaining) return false;
    Atom *label = atom_string(l->arena, "_");
    for (uint32_t i = 0u; i < (n ? n : 1u); ++i) {
        Atom *left = i ? indexed_category(l, base, i) : base;
        Atom *right = item(l, false, indexed_category(l, base, i + 1u));
        if (!add_rule(l, label, left, small_items(l, &right, 1u), false)) return false;
    }
    Atom *items[] = {item(l, true, atom_string(l->arena, "(")), item(l, false, base),
                    item(l, true, atom_string(l->arena, ")"))};
    return add_rule(l, label, indexed_category(l, base, n), small_items(l, items, 3u), false);
}

static bool ascii_rule_name(const char *s) {
    for (; *s; ++s) if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') ||
                          (*s >= '0' && *s <= '9') || *s == '_')) return false;
    return true;
}

static bool expand_rules(Lower *l, Atom *category, Atom *alternatives) {
    if (!string(category)) return false;
    Atom *rows = alternatives;
    for (; form(rows, "LCons", 2u); rows = rows->expr.elems[2]) {
        Atom *items = rows->expr.elems[1], *suffix = NULL;
        bool separator = false;
        if (form(items, "LCons", 2u) && atom_is_symbol(items->expr.elems[2], "LNil")) {
            Atom *only = items->expr.elems[1];
            if (form(only, "LbnfTerminalV1", 1u)) {
                Atom *s = text_string(l, only->expr.elems[1]);
                if (string(s) && ascii_rule_name(s->ground.sval)) { suffix = s; separator = true; }
            } else if (form(only, "LbnfNonTerminalV1", 1u) && string(only->expr.elems[1]) &&
                       only->expr.elems[1]->ground.sval[0] != '[') suffix = only->expr.elems[1];
        }
        if (!suffix) {
            size_t index = 0u;
            while (index < l->macro_categories.len && !atom_eq(l->macro_categories.items[index], category)) ++index;
            if (index == l->macro_categories.len) {
                if (!push(l, &l->macro_categories, category) ||
                    !push(l, &l->macro_indices, atom_int(l->arena, 0))) return false;
            }
            int64_t next = l->macro_indices.items[index]->ground.ival + 1;
            l->macro_indices.items[index] = atom_int(l->arena, next);
            char number[32]; snprintf(number, sizeof(number), "%lld", (long long)next);
            suffix = atom_string(l->arena, number);
        }
        size_t size = strlen(category->ground.sval) + strlen(suffix->ground.sval) + 2u;
        char *name = arena_alloc(l->arena, size);
        snprintf(name, size, "%s%s%s", category->ground.sval, separator ? "_" : "", suffix->ground.sval);
        if (!add_rule(l, atom_string(l->arena, name), category, items, false)) return false;
    }
    return atom_is_symbol(rows, "LNil");
}

static bool lower_definition(Lower *l, Atom *body, size_t index) {
    if (!spend(l)) return false;
    if (form(body, "LbnfRuleV1", 4u)) {
        bool internal;
        if (!boolean(body->expr.elems[4], &internal)) return false;
        return add_rule(l, body->expr.elems[1], body->expr.elems[2], body->expr.elems[3],
                        internal);
    }
    if (form(body, "LbnfSeparatorV1", 3u) || form(body, "LbnfTerminatorV1", 3u)) {
        bool nonempty;
        if (!boolean(body->expr.elems[3], &nonempty)) return false;
        return expand_list(l, body->expr.elems[1], body->expr.elems[2],
            nonempty, form(body, "LbnfTerminatorV1", 3u));
    }
    if (form(body, "LbnfCoercionsV1", 2u)) return expand_coercions(l, body->expr.elems[1], body->expr.elems[2]);
    if (form(body, "LbnfRulesV1", 2u)) return expand_rules(l, body->expr.elems[1], body->expr.elems[2]);
    if (form(body, "LbnfEntrypointsV1", 1u)) {
        Atom *rows = body->expr.elems[1];
        for (; form(rows, "LCons", 2u); rows = rows->expr.elems[2])
            if (!string(rows->expr.elems[1]) || !unique(l, &l->entries, rows->expr.elems[1])) return false;
        return atom_is_symbol(rows, "LNil");
    }
    if (form(body, "LbnfTokenV1", 3u)) {
        Atom *name = body->expr.elems[1];
        if (!string(name) || !name->ground.sval[0]) return false;
        size_t len = strlen(name->ground.sval);
        if (name->ground.sval[len - 1u] >= '0' && name->ground.sval[len - 1u] <= '9')
            return fail(l, "numeric-token-suffix", "Token name must not have a numeric suffix.");
        if (has(&l->custom_tokens, name)) return fail(l, "duplicate-token-definition", "Duplicate token definitions.");
        if (!push(l, &l->custom_tokens, name)) return false;
        Atom *regular = regex(l, body->expr.elems[2], 0u);
        if (!regular || !token_regex_valid(l, regular) || !type(l, name, true)) return false;
        Atom *kind = atom_symbol(l->arena, "TokenTextV1");
        bool position;
        if (!boolean(body->expr.elems[3], &position)) return false;
        if (position) kind = make(l->arena, "TokenPositionV1", &kind, 1u);
        Atom *tag = make(l->arena, "bnfc-token", &name, 1u);
        Atom *values[] = {name, tag, regular, kind, origin(l, "token", index)};
        return push(l, &l->declarations, make(l->arena, "RegularTokenDeclV1", values, 5u));
    }
    bool line = form(body, "LbnfLineCommentV1", 1u), block = form(body, "LbnfBlockCommentV1", 2u);
    if (line || block) {
        Values points = {0};
        if (!text_points(l, body->expr.elems[1], &points) || !points.len) {
            clear(&points); return fail(l, "empty-comment-delimiter", "Empty comment start delimiter.");
        }
        clear(&points);
        Atom *opener = scalar_text(l, body->expr.elems[1]);
        Atom *values[] = {opener, block ? scalar_text(l, body->expr.elems[2]) : origin(l, "comment", index),
                          origin(l, "comment", index)};
        if (block) {
            if (!text_points(l, values[1], &points) || !points.len) {
                clear(&points); return fail(l, "empty-comment-delimiter", "Empty block comment end delimiter.");
            }
            clear(&points);
        }
        return push(l, &l->comments, make(l->arena, line ? "ParserLineCommentV1" : "ParserBlockCommentV1",
                                         values, line ? 2u : 3u));
    }
    if (form(body, "LbnfPragmaV1", 2u) && string(body->expr.elems[1])) {
        const char *name = body->expr.elems[1]->ground.sval;
        if (!strcmp(name, "Delimiters")) return fail(l, "removed-delimiters", "The delimiters pragma was removed in BNFC 2.9.0.");
        if (!strcmp(name, "Layout") || !strcmp(name, "LayoutStop") || !strcmp(name, "LayoutTop"))
            return fail(l, "layout", "Layout declarations are retained in the document but not lowered in this profile.");
        if (!strcmp(name, "Function")) return fail(l, "define", "Define declarations are retained in the document but not lowered in this profile.");
    }
    return fail(l, "malformed-definition", "Unknown or malformed LBNF document declaration.");
}

bool cetta_lbnf_document_v1_declarations(
    Arena *arena, Atom *document, Atom *name, uint32_t *remaining_work,
    Atom **language, Atom **production_sites, Atom **refusal) {
    if (!arena || !remaining_work || !language || !refusal) return false;
    Lower l = {.arena = arena, .remaining = *remaining_work};
    bool ok = false;
    if (!form(document, "LbnfDocumentV1", 2u) || !string(name) || !name->ground.sval[0]) {
        fail(&l, "malformed-document", "Expected a declaration document and a language name."); goto done;
    }
    l.namespace_value = document->expr.elems[1];
    if (!string(l.namespace_value) || !l.namespace_value->ground.sval[0]) {
        fail(&l, "malformed-document", "Expected a declaration namespace."); goto done;
    }
    Atom *rows = document->expr.elems[2];
    size_t index = 0u;
    for (; form(rows, "LCons", 2u); rows = rows->expr.elems[2], ++index) {
        if (!spend(&l)) goto done;
        Atom *row = rows->expr.elems[1];
        if (!form(row, "LbnfDefinitionV1", 2u)) {
            fail(&l, "malformed-definition", "Expected a located declaration."); goto done;
        }
        l.site = row->expr.elems[1];
        Atom *body = row->expr.elems[2];
        if (form(body, "LbnfRuleV1", 4u) || form(body, "LbnfSeparatorV1", 3u) ||
            form(body, "LbnfTerminatorV1", 3u) || form(body, "LbnfCoercionsV1", 2u) ||
            form(body, "LbnfRulesV1", 2u)) {
            if (!lower_definition(&l, body, index)) goto done;
        } else if (form(body, "LbnfTokenV1", 3u)) {
            bool positioned;
            Atom *category = body->expr.elems[1];
            if (!string(category) || !category->ground.sval[0] ||
                !boolean(body->expr.elems[3], &positioned)) {
                fail(&l, "malformed-token", "Expected a named token declaration."); goto done;
            }
            if (has(&l.custom_tokens, category)) {
                fail(&l, "duplicate-token-definition", "Duplicate token definitions."); goto done;
            }
            if (!push(&l, &l.custom_tokens, category)) goto done;
        } else if (!form(body, "LbnfEntrypointsV1", 1u) &&
                   !form(body, "LbnfLineCommentV1", 1u) &&
                   !form(body, "LbnfBlockCommentV1", 2u) &&
                   !form(body, "LbnfPragmaV1", 2u)) {
            fail(&l, "malformed-definition", "Unknown declaration notation."); goto done;
        }
    }
    if (!atom_is_symbol(rows, "LNil")) {
        fail(&l, "malformed-definition-list", "Expected a finite declaration list."); goto done;
    }
    for (size_t i = 0u; i < l.custom_tokens.len; ++i) {
        if (has(&l.defined, l.custom_tokens.items[i])) {
            fail(&l, "token-category-conflict", "A category cannot be both a token and an AST sort."); goto done;
        }
        if (!type(&l, l.custom_tokens.items[i], true)) goto done;
    }
    for (size_t i = 0u; i < l.referenced.len; ++i) {
        Atom *category = l.referenced.items[i];
        if (!type(&l, category, has(&l.custom_tokens, category) || builtin(category->ground.sval))) goto done;
    }
    Atom *nil = atom_symbol(arena, "LNil");
    Atom *fields[] = {name, list(arena, &l.types), list(arena, &l.rules), nil, nil};
    Atom *owned = atom_deep_copy(arena, make(arena, "GSLTLanguageDefWireV1", fields, 5u));
    if (!owned) { fail(&l, "allocation-failure", "Could not own the declaration value."); goto done; }
    Atom *sites = production_sites ? atom_deep_copy(arena, list(arena, &l.rule_sites)) : NULL;
    if (production_sites && !sites) { fail(&l, "allocation-failure", "Could not own declaration sites."); goto done; }
    *language = owned;
    if (production_sites) *production_sites = sites;
    ok = true;
done:
    *remaining_work = l.remaining;
    if (!ok) {
        if (!l.refusal) fail(&l, "malformed-definition", "Malformed declaration fields.");
        *refusal = l.refusal;
    }
    close_lower(&l); return ok;
}

static bool validate_grammar(Lower *l) {
    bool consumes_token = false;
    Atom *empty_site = NULL;
    for (size_t i = 0u; i < l->rules.len; ++i) {
        Atom *rule = l->rules.items[i], *metadata = l->metadata.items[i];
        l->site = l->rule_sites.items[i];
        const unsigned char *label = (const unsigned char *)rule->expr.elems[1]->ground.sval;
        /* Explicit labels in .cf source are Latin-1 identifiers. Generated
         * labels start with the source category name. */
        bool lower = (label[0] >= 'a' && label[0] <= 'z') ||
            (label[0] == 0xc3u && ((label[1] >= 0x9fu && label[1] <= 0xb6u) ||
                                  (label[1] >= 0xb8u && label[1] <= 0xbfu)));
        if (lower) return fail(l, "undefined-rule-label", "Lower case rule labels need a definition.");
        bool visible = atom_is_symbol(metadata->expr.elems[2], "ParserVisibleV1");
        for (Atom *p = rule->expr.elems[4]; form(p, "LCons", 2u); p = p->expr.elems[2]) {
            if (!spend(l)) return false;
            if (visible && form(p->expr.elems[1], "SyntaxTerminal", 1u)) consumes_token = true;
        }
        for (Atom *p = rule->expr.elems[3]; form(p, "LCons", 2u); p = p->expr.elems[2]) {
            if (!spend(l)) return false;
            Atom *category = p->expr.elems[1]->expr.elems[2]->expr.elems[1];
            if (!has(&l->defined, category) && !has(&l->custom_tokens, category) &&
                !builtin(category->ground.sval))
                return fail(l, "missing-production", "No production for a referenced category.");
            if (has(&l->custom_tokens, category) || builtin(category->ground.sval)) consumes_token = true;
        }
        Atom *norm = normalized(l, rule->expr.elems[2]);
        if (!norm) return fail(l, "bad-category-name", "Invalid normalized category name.");
        if (norm->ground.sval[0] != '[' && !builtin(norm->ground.sval)) {
            bool populated = false;
            for (size_t j = 0u; j < l->metadata.len; ++j) {
                Atom *projection = l->metadata.items[j]->expr.elems[3];
                if (!spend(l)) return false;
                if (form(projection, "BnfcConstructorV1", 3u) &&
                    atom_eq(projection->expr.elems[2], norm)) populated = true;
            }
            if (!populated && !empty_site) empty_site = l->site;
        }
    }
    if (!consumes_token) return fail(l, "no-terminals", "The grammar mentions no used tokens or terminals.");
    if (empty_site) {
        l->site = empty_site;
        return fail(l, "empty-abstract-syntax", "The category has no non-coercion constructor.");
    }
    for (size_t i = 0u; i < l->custom_tokens.len; ++i) {
        for (size_t j = 0u; j < l->defined.len; ++j) {
            if (!spend(l)) return false;
            if (atom_eq(normalized(l, l->defined.items[j]), l->custom_tokens.items[i]))
                return fail(l, "token-category-conflict", "Token definition conflicts with a nonterminal.");
        }
    }
    return true;
}

bool cetta_lbnf_document_v1_lower(
    Arena *arena, Atom *document, Atom *name, Atom *start, Atom *policy,
    uint32_t work_limit, CettaLbnfLoweredV1 *out, Atom **refusal) {
    Lower l = {.arena = arena, .remaining = work_limit, .policy = policy};
    bool ok = false;
    if (!arena || !out || !refusal) return false;
    if (!form(document, "LbnfDocumentV1", 2u) || !string(document->expr.elems[1]) ||
        !document->expr.elems[1]->ground.sval[0] || !string(name) || !name->ground.sval[0] ||
        !form(policy, "SyntaxLexicalPolicyV1", 1u)) {
        fail(&l, "bad-argument", "Expected an LBNF document, output name and explicit lexical policy."); goto done;
    }
    l.namespace_value = document->expr.elems[1];
    /* A document-wide diagnostic has a real anchor even when no declaration
     * exists. Declaration diagnostics replace it with their source span. */
    Atom *document_start[] = {atom_int(arena, 0), atom_int(arena, 0),
                             atom_int(arena, 1), atom_int(arena, 1)};
    l.site = make(arena, "LbnfSourceSpanV1", document_start, 4u);
    l.c_flex = atom_is_symbol(policy->expr.elems[1], "bnfc-2.9.6.3-c-flex-2.6.4");
    if (!l.c_flex && !atom_is_symbol(policy->expr.elems[1], "bnfc-documented")) {
        fail(&l, "lexical-policy", "Expected bnfc-documented or the pinned C/Flex policy."); goto done;
    }
    char error[512]; CettaDeterministicEquationStatusV1 status;
    CettaDeterministicEquationInputV1 input = {cetta_bnfc_lexical_policy_v1_source,
        cetta_bnfc_lexical_policy_v1_source_len, "BnfcLexicalPolicyV1"};
    if (!cetta_deterministic_equation_plan_v1_load_inputs(&input, 1u, NULL,
            &l.lexical, &status, error, sizeof(error))) {
        fail(&l, "lexical-policy", error); goto done;
    }
    Atom *rows = document->expr.elems[2]; size_t index = 0u;
    for (; form(rows, "LCons", 2u); rows = rows->expr.elems[2], ++index) {
        Atom *definition = rows->expr.elems[1];
        if (!form(definition, "LbnfDefinitionV1", 2u)) {
            fail(&l, "malformed-definition", "Expected a source-spanned LBNF definition."); goto done;
        }
        l.site = definition->expr.elems[1];
        if (!lower_definition(&l, definition->expr.elems[2], index)) {
            fail(&l, "malformed-definition", "Malformed LBNF declaration fields."); goto done;
        }
    }
    if (!atom_is_symbol(rows, "LNil")) {
        fail(&l, "malformed-definition-list", "Expected a finite LBNF definition list."); goto done;
    }
    if (!l.rules.len) {
        fail(&l, "empty-grammar", "LBNF grammar has no productions."); goto done;
    }
    if (!validate_grammar(&l)) goto done;
    if (!l.entries.len) {
        for (size_t i = 0u; i < l.rules.len; ++i)
            if (atom_is_symbol(l.metadata.items[i]->expr.elems[2], "ParserVisibleV1")) {
                if (!push(&l, &l.entries, l.rules.items[i]->expr.elems[2])) goto done;
                break;
            }
    }
    if (!l.entries.len) { fail(&l, "missing-entrypoint", "The grammar has no visible start category."); goto done; }
    if (atom_is_symbol(start, "LbnfDefaultStartV1")) start = l.entries.items[0];
    if (!string(start) || (!has(&l.defined, start) && !has(&l.custom_tokens, start) && !builtin(start->ground.sval))) {
        fail(&l, "missing-entrypoint", "No production for the selected start category."); goto done;
    }
    if (builtin(start->ground.sval) && !unique(&l, &l.referenced, start)) goto done;
    for (size_t i = 0u; i < l.entries.len; ++i) {
        Atom *category = l.entries.items[i];
        if (!has(&l.defined, category) && !has(&l.custom_tokens, category) && !builtin(category->ground.sval)) {
            fail(&l, "missing-entrypoint", "No production for a declared entrypoint."); goto done;
        }
        if (builtin(category->ground.sval) && !unique(&l, &l.referenced, category)) goto done;
    }
    static const char *const kinds[] = {"Ident", "Integer", "Double", "String", "Char"};
    static const char *const values[] = {"TokenIdentV1", "TokenIntegerV1", "TokenDoubleV1", "TokenStringV1", "TokenCharV1"};
    for (size_t i = 0u; i < 5u; ++i) {
        Atom *category = atom_string(arena, kinds[i]);
        if (!has(&l.referenced, category) && (i || l.c_flex)) continue;
        if (has(&l.custom_tokens, category)) continue;
        Atom *regular = lexical_value(&l, "bnfc-v1:builtin", atom_symbol(arena, kinds[i]));
        if (!regular || !type(&l, category, true)) goto done;
        Atom *tag = make(arena, "bnfc-builtin-token", &category, 1u);
        Atom *site[] = {atom_string(arena, "BNFC:builtin-token-signatures-v1"), category};
        Atom *fields[] = {category, tag, regular, atom_symbol(arena, values[i]), make(arena, "SyntaxOriginV1", site, 2u)};
        if (!push(&l, &l.declarations, make(arena, "RegularTokenDeclV1", fields, 5u))) goto done;
    }
    for (size_t i = 0u; i < l.referenced.len; ++i) {
        Atom *category = l.referenced.items[i];
        if (!has(&l.defined, category) && !has(&l.custom_tokens, category) && !builtin(category->ground.sval)) {
            fail(&l, "missing-production", "No production for a referenced category."); goto done;
        }
        if (!type(&l, category, has(&l.custom_tokens, category) || builtin(category->ground.sval))) goto done;
    }
    Atom *whitespace = lexical_value(&l, "bnfc-v1:whitespace", NULL);
    if (!whitespace) goto done;
    Atom *site[] = {atom_string(arena, "BNFC:layout-signatures-v1"),
                   atom_string(arena, atom_name_cstr(policy->expr.elems[1]))};
    Atom *trivia[] = {atom_symbol(arena, "bnfc-layout"), whitespace, make(arena, "SyntaxOriginV1", site, 2u)};
    if (!push(&l, &l.declarations, make(arena, "RegularTriviaDeclV1", trivia, 3u))) goto done;
    Atom *nil = atom_symbol(arena, "LNil");
    Atom *language_args[] = {name, list(arena, &l.types), list(arena, &l.rules), nil, nil};
    Atom *token_args[] = {policy, list(arena, &l.declarations), list(arena, &l.comments),
                          list(arena, &l.metadata), list(arena, &l.entries)};
    Atom *profile_args[] = {name, start, nil, nil, make(arena, "ParserTokenLayerV1", token_args, 5u)};
    Atom *origin_args[] = {list(arena, &l.type_origins), list(arena, &l.origins), nil, nil};
    CettaLbnfLoweredV1 candidate = {make(arena, "GSLTLanguageDefWireV1", language_args, 5u),
        make(arena, "GSLTParserProfileLayerV2", profile_args, 5u), policy,
        make(arena, "SyntaxOriginsV1", origin_args, 4u)};
    *out = candidate; ok = true;
done:
    if (!ok) *refusal = l.refusal;
    close_lower(&l); return ok;
}

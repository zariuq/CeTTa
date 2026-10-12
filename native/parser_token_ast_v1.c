#include "parser_token_ast_v1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    Arena *arena;
    const CettaParserTokenProfileV1 *profile;
    const CettaLpNativeUtf8ScalarView *source;
    uint32_t depth_limit;
    uint32_t work;
    char *error;
    size_t error_size;
} AstProjection;

/* The innermost cause is kept: an enclosing projection adds no detail. */
static bool fail(AstProjection *p, const char *message) {
    if (p->error && p->error_size && !p->error[0])
        (void)snprintf(p->error, p->error_size, "%s", message);
    return false;
}

static bool form(Atom *a, const char *name, uint32_t arity) {
    return a && a->kind == ATOM_EXPR && a->expr.len == arity + 1u &&
        atom_is_symbol(a->expr.elems[0], name);
}

static Atom *make(Arena *a, const char *head, Atom *const *args, uint32_t len) {
    Atom **items = arena_alloc(a, ((size_t)len + 1u) * sizeof(*items));
    if (!items) return NULL;
    items[0] = atom_symbol(a, head);
    for (uint32_t i = 0u; i < len; ++i) items[i + 1u] = args[i];
    return atom_expr(a, items, len + 1u);
}

static Atom *span(AstProjection *p, Atom *left, Atom *right) {
    if (!left || !right || left->kind != ATOM_GROUNDED || right->kind != ATOM_GROUNDED ||
        left->ground.gkind != GV_INT || right->ground.gkind != GV_INT || left->ground.ival < 0 ||
        right->ground.ival < left->ground.ival || right->ground.ival > p->source->scalar_len) return NULL;
    uint32_t l = (uint32_t)left->ground.ival, r = (uint32_t)right->ground.ival;
    Atom *args[] = {atom_int(p->arena, l), atom_int(p->arena, r),
        atom_int(p->arena, cetta_lp_native_utf8_scalar_view_byte_offset(p->source, l)),
        atom_int(p->arena, cetta_lp_native_utf8_scalar_view_byte_offset(p->source, r))};
    return make(p->arena, "LbnfSourceSpanV1", args, 4u);
}

/* The admitted projection of a source-spanned CST constructor of `category`. */
static Atom *category_projection(AstProjection *p, const char *label, Atom *category) {
    Atom *projection = NULL;
    for (Atom *rows = p->profile->productions; form(rows, "LCons", 2u); rows = rows->expr.elems[2]) {
        Atom *meta = rows->expr.elems[1]->expr.elems[3];
        if (form(meta, "BnfcConstructorV1", 3u)) {
            if (!strcmp(meta->expr.elems[1]->ground.sval, label) && atom_eq(meta->expr.elems[2], category))
                projection = meta;
        } else if (atom_eq(meta->expr.elems[1], category) &&
            ((!strcmp(label, "_") && form(meta, "BnfcCoercionV1", 1u)) ||
             (!strcmp(label, "[]") && form(meta, "BnfcListEmptyV1", 1u)) ||
             (!strcmp(label, "(:)") && form(meta, "BnfcListConsV1", 1u)) ||
             (!strcmp(label, "(:[])") && form(meta, "BnfcListSingletonV1", 1u)))) projection = meta;
    }
    return projection;
}

static bool is_list_projection(Atom *projection) {
    return form(projection, "BnfcListEmptyV1", 1u) || form(projection, "BnfcListConsV1", 1u) ||
        form(projection, "BnfcListSingletonV1", 1u);
}

/* The list projection of a CST cell of `category`, or NULL when the cell is
 * not a list cell of that category, such as a coercion. */
static Atom *list_cell_projection(AstProjection *p, Atom *cell, Atom *category) {
    if (!cell || cell->kind != ATOM_EXPR || cell->expr.len < 4u ||
        !atom_is_symbol(cell->expr.elems[0], "CstRuleV1") ||
        !cell->expr.elems[1] || cell->expr.elems[1]->kind != ATOM_GROUNDED ||
        cell->expr.elems[1]->ground.gkind != GV_STRING ||
        !span(p, cell->expr.elems[2], cell->expr.elems[3]))
        return NULL;
    Atom *projection = category_projection(p, cell->expr.elems[1]->ground.sval, category);
    return is_list_projection(projection) ? projection : NULL;
}

static bool project(AstProjection *p, Atom *value, Atom *category,
                      uint32_t depth, Atom **out);

/* A list is the right spine of its cons cells.  The spine is walked in a
 * loop, so list length is neither recursion nor projection depth; only
 * nesting inside an element is.  The result is the outermost cell's list
 * node holding every element, as when each tail was projected in turn. */
static bool project_list(AstProjection *p, Atom *value, Atom *projection, Atom *category,
                         uint32_t depth, Atom *extent, Atom **out) {
    const char *name = category->ground.sval;
    size_t n = strlen(name);
    if (n < 3u || name[0] != '[' || name[n - 1u] != ']') return fail(p, "a list projection requires a list category");
    char *element_name = arena_alloc(p->arena, n - 1u);
    if (!element_name) return false;
    memcpy(element_name, name + 1u, n - 2u); element_name[n - 2u] = '\0';
    Atom *element = atom_string(p->arena, element_name);
    Atom *tail = atom_symbol(p->arena, "LNil");
    Atom **items = NULL;
    uint32_t item_len = 0u, item_cap = 0u;
    bool ok = false;
    for (Atom *cell = value;;) {
        uint32_t child_len = (uint32_t)cell->expr.len - 4u;
        if (form(projection, "BnfcListEmptyV1", 1u)) {
            if (child_len) { fail(p, "an empty list CST has retained children"); goto done; }
            break;
        }
        bool cons = form(projection, "BnfcListConsV1", 1u);
        if (child_len != (cons ? 2u : 1u)) { fail(p, "list CST children disagree with their signature"); goto done; }
        if (item_len == item_cap) {
            uint32_t cap = item_cap ? item_cap * 2u : 16u;
            Atom **grown = cap > item_cap ? realloc(items, (size_t)cap * sizeof(*items)) : NULL;
            if (!grown) { fail(p, "AST list allocation failed"); goto done; }
            items = grown; item_cap = cap;
        }
        if (!project(p, cell->expr.elems[4], element, depth + 1u, &items[item_len])) {
            fail(p, "list CST children disagree with their signature"); goto done;
        }
        ++item_len;
        if (!cons) break;
        Atom *next = cell->expr.elems[5];
        Atom *next_projection = list_cell_projection(p, next, category);
        if (!next_projection) {
            Atom *projected = NULL;
            if (!project(p, next, category, depth + 1u, &projected) ||
                !form(projected, "LbnfAstListV1", 3u) || !atom_eq(projected->expr.elems[1], category)) {
                fail(p, "a list tail has the wrong projected category"); goto done;
            }
            tail = projected->expr.elems[3];
            break;
        }
        if (!p->work) { fail(p, "labelled AST projection work or depth limit"); goto done; }
        --p->work;
        cell = next;
        projection = next_projection;
    }
    for (uint32_t i = item_len; i > 0u; --i)
        tail = atom_expr3(p->arena, atom_symbol(p->arena, "LCons"), items[i - 1u], tail);
    Atom *args[] = {atom_deep_copy(p->arena, category), extent, tail};
    *out = make(p->arena, "LbnfAstListV1", args, 3u);
    ok = *out != NULL;
done:
    free(items);
    return ok;
}

static bool project(AstProjection *p, Atom *value, Atom *category,
                      uint32_t depth, Atom **out) {
    if (!value || !category || !p->work || depth > p->depth_limit)
        return fail(p, "labelled AST projection work or depth limit");
    --p->work;
    Atom *position = atom_symbol(p->arena, "LbnfNoTokenPositionV1");
    if (form(value, "ParserPositionTokenV1", 3u)) {
        Atom *args[] = {atom_deep_copy(p->arena, value->expr.elems[1]),
                        atom_deep_copy(p->arena, value->expr.elems[2])};
        position = make(p->arena, "LbnfTokenPositionV1", args, 2u);
        value = value->expr.elems[3];
    }
    if (form(value, "ParserTokenV1", 7u)) {
        Atom *extent = span(p, value->expr.elems[4], value->expr.elems[5]);
        if (!extent) return fail(p, "token AST source span is invalid");
        Atom *args[] = {atom_deep_copy(p->arena, category),
            atom_deep_copy(p->arena, value->expr.elems[1]),
            atom_deep_copy(p->arena, value->expr.elems[3]), position, extent,
            atom_deep_copy(p->arena, value->expr.elems[2])};
        *out = make(p->arena, "LbnfAstTokenV1", args, 6u);
        return *out != NULL;
    }
    if (value->kind != ATOM_EXPR || value->expr.len < 4u ||
        !atom_is_symbol(value->expr.elems[0], "CstRuleV1") ||
        !value->expr.elems[1] || value->expr.elems[1]->kind != ATOM_GROUNDED ||
        value->expr.elems[1]->ground.gkind != GV_STRING)
        return fail(p, "labelled AST projection requires a source-spanned CST");
    Atom *extent = span(p, value->expr.elems[2], value->expr.elems[3]);
    if (!extent) return fail(p, "constructor AST source span is invalid");
    const char *label = value->expr.elems[1]->ground.sval;
    Atom *projection = category_projection(p, label, category);
    if (!projection) return fail(p, "CST constructor has no admitted category projection");
    if (is_list_projection(projection))
        return project_list(p, value, projection, category, depth, extent, out);
    uint32_t child_len = (uint32_t)value->expr.len - 4u;
    Atom **children = child_len ? arena_alloc(p->arena, (size_t)child_len * sizeof(*children)) : NULL;
    if (child_len && !children) return fail(p, "AST child allocation failed");
    Atom *categories = form(projection, "BnfcConstructorV1", 3u) ? projection->expr.elems[3] : NULL;
    if (categories) {
        for (uint32_t i = 0u; i < child_len; ++i) {
            if (!form(categories, "LCons", 2u) ||
                !project(p, value->expr.elems[i + 4u], categories->expr.elems[1], depth + 1u, &children[i]))
                return fail(p, "CST children disagree with their constructor signature");
            categories = categories->expr.elems[2];
        }
        if (!atom_is_symbol(categories, "LNil")) return fail(p, "constructor has too few retained CST children");
        Atom *list = atom_symbol(p->arena, "LNil");
        for (uint32_t i = child_len; i > 0u; --i) list = atom_expr3(p->arena,
            atom_symbol(p->arena, "LCons"), children[i - 1u], list);
        Atom *args[] = {atom_deep_copy(p->arena, category), atom_string(p->arena, label), extent, list};
        *out = make(p->arena, "LbnfAstNodeV1", args, 4u);
        return *out != NULL;
    }
    if (form(projection, "BnfcCoercionV1", 1u)) {
        if (child_len != 1u) return fail(p, "a coercion CST must retain exactly one child");
        return project(p, value->expr.elems[4], category, depth + 1u, out);
    }
    return fail(p, "CST constructor has no admitted category projection");
}

bool cetta_parser_token_ast_v1_project(
    Arena *arena, const CettaParserTokenProfileV1 *profile,
    const CettaLdTextV1 *start_category,
    const CettaLpNativeUtf8ScalarView *source, Atom *cst,
    uint32_t depth_limit, uint32_t work_limit, Atom **out,
    char *error, size_t error_size) {
    AstProjection p = {arena, profile, source, depth_limit, work_limit, error, error_size};
    if (error && error_size) error[0] = '\0';
    if (!arena || !profile || !start_category || !source || !cst || !out ||
        !cetta_lp_native_utf8_scalar_view_validate(source, error, error_size))
        return fail(&p, "bad labelled AST projection arguments");
    Atom *category = cetta_parser_token_category_v1_normalize(arena, start_category);
    if (!category) return fail(&p, "AST start category is not a normalized textual category");
    Atom *candidate = NULL;
    if (!project(&p, cst, category, 0u, &candidate)) return false;
    *out = candidate;
    return true;
}

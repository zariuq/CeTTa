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
    bool incomplete;
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

typedef struct {
    Atom *value, *category, *projection, *extent, *categories;
    Atom **inputs, **children;
    Atom *element_category;
    uint32_t depth, child_len, next;
    bool entered, list, list_tail;
} AstFrame;

static bool frame_push(AstProjection *p, AstFrame **frames, uint32_t *len,
                        uint32_t *capacity, Atom *value, Atom *category, uint32_t depth) {
    if (*len == *capacity) {
        uint32_t cap = *capacity ? *capacity * 2u : 32u;
        AstFrame *grown = cap > *capacity && (size_t)cap <= SIZE_MAX / sizeof(**frames)
            ? realloc(*frames, (size_t)cap * sizeof(**frames)) : NULL;
        if (!grown) return fail(p, "AST projection stack allocation failed");
        *frames = grown; *capacity = cap;
    }
    (*frames)[(*len)++] = (AstFrame){.value = value, .category = category, .depth = depth};
    return true;
}

/* Flatten only the right spine. Nested elements and non-list tails are
 * scheduled on the same projection stack as ordinary constructors. */
static bool list_inputs(AstProjection *p, AstFrame *frame) {
    const char *name = frame->category->ground.sval;
    size_t n = strlen(name);
    if (n < 3u || name[0] != '[' || name[n - 1u] != ']') return fail(p, "a list projection requires a list category");
    char *element_name = arena_alloc(p->arena, n - 1u);
    if (!element_name) return false;
    memcpy(element_name, name + 1u, n - 2u); element_name[n - 2u] = '\0';
    frame->element_category = atom_string(p->arena, element_name);
    Atom **items = NULL;
    uint32_t item_len = 0u, item_cap = 0u;
    bool ok = false;
    Atom *projection = frame->projection;
    for (Atom *cell = frame->value;;) {
        uint32_t child_len = (uint32_t)cell->expr.len - 4u;
        if (form(projection, "BnfcListEmptyV1", 1u)) {
            if (child_len) { fail(p, "an empty list CST has retained children"); goto done; }
            break;
        }
        bool cons = form(projection, "BnfcListConsV1", 1u);
        if (child_len != (cons ? 2u : 1u)) { fail(p, "list CST children disagree with their signature"); goto done; }
        if (item_cap - item_len < 2u) {
            uint32_t cap = item_cap ? item_cap * 2u : 16u;
            Atom **grown = cap > item_cap && (size_t)cap <= SIZE_MAX / sizeof(*items)
                ? realloc(items, (size_t)cap * sizeof(*items)) : NULL;
            if (!grown) { fail(p, "AST list allocation failed"); goto done; }
            items = grown; item_cap = cap;
        }
        items[item_len++] = cell->expr.elems[4];
        if (!cons) break;
        Atom *next = cell->expr.elems[5];
        Atom *next_projection = list_cell_projection(p, next, frame->category);
        if (!next_projection) {
            items[item_len++] = next;
            frame->list_tail = true;
            break;
        }
        if (!p->work) { p->incomplete = true; fail(p, "labelled AST projection work or depth limit"); goto done; }
        --p->work;
        cell = next;
        projection = next_projection;
    }
    frame->inputs = item_len ? arena_alloc(p->arena, (size_t)item_len * sizeof(*items)) : NULL;
    if (item_len && !frame->inputs) { fail(p, "AST list allocation failed"); goto done; }
    if (item_len) memcpy(frame->inputs, items, (size_t)item_len * sizeof(*items));
    frame->child_len = item_len;
    ok = true;
done:
    free(items);
    return ok;
}

static bool enter_frame(AstProjection *p, AstFrame *frame, Atom **leaf) {
    Atom *value = frame->value, *category = frame->category;
    if (!p->work || frame->depth > p->depth_limit) {
        p->incomplete = true;
        return fail(p, "labelled AST projection work or depth limit");
    }
    if (!value || !category) return fail(p, "missing labelled AST projection value or category");
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
        *leaf = make(p->arena, "LbnfAstTokenV1", args, 6u);
        return *leaf != NULL;
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
    frame->value = value; frame->projection = projection; frame->extent = extent;
    frame->entered = true; frame->list = is_list_projection(projection);
    if (frame->list) {
        if (!list_inputs(p, frame)) return false;
    } else {
        frame->child_len = (uint32_t)value->expr.len - 4u;
        frame->inputs = value->expr.elems + 4u;
        frame->categories = form(projection, "BnfcConstructorV1", 3u) ? projection->expr.elems[3] : NULL;
        if (!frame->categories && (!form(projection, "BnfcCoercionV1", 1u) || frame->child_len != 1u))
            return fail(p, "a coercion CST must retain exactly one child");
    }
    frame->children = frame->child_len
        ? arena_alloc(p->arena, (size_t)frame->child_len * sizeof(*frame->children)) : NULL;
    return !frame->child_len || frame->children || fail(p, "AST child allocation failed");
}

static bool finish_frame(AstProjection *p, const AstFrame *frame, Atom **out) {
    Atom *category = frame->category;
    Atom *list = atom_symbol(p->arena, "LNil");
    uint32_t child_len = frame->child_len;
    if (frame->list_tail) {
        Atom *tail = frame->children[--child_len];
        if (!form(tail, "LbnfAstListV1", 3u) || !atom_eq(tail->expr.elems[1], category))
            return fail(p, "a list tail has the wrong projected category");
        list = tail->expr.elems[3];
    }
    if (!frame->list && form(frame->projection, "BnfcCoercionV1", 1u)) {
        *out = frame->children[0];
        return true;
    }
    if (!frame->list && !atom_is_symbol(frame->categories, "LNil"))
        return fail(p, "constructor has too few retained CST children");
    for (uint32_t i = child_len; i > 0u; --i)
        list = atom_expr3(p->arena, atom_symbol(p->arena, "LCons"), frame->children[i - 1u], list);
    if (frame->list) {
        Atom *args[] = {atom_deep_copy(p->arena, category), frame->extent, list};
        *out = make(p->arena, "LbnfAstListV1", args, 3u);
    } else {
        Atom *args[] = {atom_deep_copy(p->arena, category),
            atom_deep_copy(p->arena, frame->value->expr.elems[1]), frame->extent, list};
        *out = make(p->arena, "LbnfAstNodeV1", args, 4u);
    }
    return *out != NULL;
}

static bool project(AstProjection *p, Atom *value, Atom *category,
                    uint32_t depth, Atom **out) {
    AstFrame *frames = NULL;
    uint32_t len = 0u, capacity = 0u;
    Atom *produced = NULL;
    bool ok = false;
    if (!frame_push(p, &frames, &len, &capacity, value, category, depth)) goto done;
    while (len) {
        AstFrame *frame = &frames[len - 1u];
        if (!frame->entered && !enter_frame(p, frame, &produced)) goto done;
        if (!produced && frame->next < frame->child_len) {
            uint32_t index = frame->next++;
            Atom *child_category = frame->category;
            if (frame->list) {
                if (!frame->list_tail || index + 1u != frame->child_len) child_category = frame->element_category;
            } else if (frame->categories) {
                if (!form(frame->categories, "LCons", 2u)) {
                    fail(p, "CST children disagree with their constructor signature"); goto done;
                }
                child_category = frame->categories->expr.elems[1];
                frame->categories = frame->categories->expr.elems[2];
            }
            if (frame->depth == UINT32_MAX) {
                p->incomplete = true; fail(p, "labelled AST projection work or depth limit"); goto done;
            }
            if (!frame_push(p, &frames, &len, &capacity,
                    frame->inputs[index], child_category, frame->depth + 1u)) goto done;
            continue;
        }
        if (!produced && !finish_frame(p, frame, &produced)) goto done;
        --len;
        if (len) {
            AstFrame *parent = &frames[len - 1u];
            parent->children[parent->next - 1u] = produced;
            produced = NULL;
        }
    }
    *out = produced;
    ok = produced != NULL;
done:
    free(frames);
    return ok;
}

static bool token_ast_project(
    Arena *arena, const CettaParserTokenProfileV1 *profile,
    const CettaLdTextV1 *start_category,
    const CettaLpNativeUtf8ScalarView *source, Atom *cst,
    uint32_t depth_limit, uint32_t work_limit, Atom **out,
    char *error, size_t error_size, bool owned, CettaParserTokenAstOutcomeV1 *outcome) {
    AstProjection p = {.arena = arena, .profile = profile, .source = source,
        .depth_limit = depth_limit, .work = work_limit, .error = error, .error_size = error_size};
    if (outcome) *outcome = CETTA_PARSER_TOKEN_AST_INVALID_V1;
    if (error && error_size) error[0] = '\0';
    if (!arena || !profile || !start_category || !source || !cst || !out ||
        (!owned && !cetta_lp_native_utf8_scalar_view_validate(source, error, error_size)))
        return fail(&p, "bad labelled AST projection arguments");
    Atom *category = cetta_parser_token_category_v1_normalize(arena, start_category);
    if (!category) return fail(&p, "AST start category is not a normalized textual category");
    Atom *candidate = NULL;
    if (!project(&p, cst, category, 0u, &candidate)) {
        if (outcome && p.incomplete) *outcome = CETTA_PARSER_TOKEN_AST_INCOMPLETE_V1;
        return false;
    }
    *out = candidate;
    if (outcome) *outcome = CETTA_PARSER_TOKEN_AST_COMPLETED_V1;
    return true;
}

bool cetta_parser_token_ast_v1_project(
    Arena *arena, const CettaParserTokenProfileV1 *profile,
    const CettaLdTextV1 *start_category,
    const CettaLpNativeUtf8ScalarView *source, Atom *cst,
    uint32_t depth_limit, uint32_t work_limit, Atom **out,
    char *error, size_t error_size) {
    return token_ast_project(arena, profile, start_category, source, cst,
        depth_limit, work_limit, out, error, error_size, false, NULL);
}

bool cetta_parser_token_ast_v1_project_owned(
    Arena *arena, const CettaParserTokenProfileV1 *profile,
    const CettaLdTextV1 *start_category,
    const CettaLpNativeUtf8ScalarView *source, Atom *cst,
    uint32_t depth_limit, uint32_t work_limit, Atom **out,
    char *error, size_t error_size) {
    return token_ast_project(arena, profile, start_category, source, cst,
        depth_limit, work_limit, out, error, error_size, true, NULL);
}

bool cetta_parser_token_ast_v1_project_owned_outcome(
    Arena *arena, const CettaParserTokenProfileV1 *profile,
    const CettaLdTextV1 *start_category, const CettaLpNativeUtf8ScalarView *source,
    Atom *cst, uint32_t depth_limit, uint32_t work_limit, Atom **out,
    CettaParserTokenAstOutcomeV1 *outcome, char *error, size_t error_size) {
    return token_ast_project(arena, profile, start_category, source, cst,
        depth_limit, work_limit, out, error, error_size, true, outcome);
}

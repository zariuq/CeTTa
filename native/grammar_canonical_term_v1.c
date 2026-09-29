#include "native/grammar_canonical_term_v1.h"

#include "src/symbol.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------- utilities */

static bool gct_error(char *error, size_t size, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    if (error && size > 0u)
        (void)vsnprintf(error, size, format, arguments);
    va_end(arguments);
    return false;
}

static char *gct_strdup(const char *text) {
    size_t len = strlen(text ? text : "");
    char *copy = malloc(len + 1u);
    if (copy)
        memcpy(copy, text ? text : "", len + 1u);
    return copy;
}

typedef struct {
    Atom **items;
    uint32_t len;
    uint32_t cap;
} GctValues;

static bool gct_push(GctValues *values, Atom *item) {
    if (values->len == values->cap) {
        uint32_t cap = values->cap ? values->cap * 2u : 8u;
        Atom **grown = realloc(values->items, (size_t)cap * sizeof(*grown));
        if (!grown)
            return false;
        values->items = grown;
        values->cap = cap;
    }
    values->items[values->len++] = item;
    return true;
}

static const char *gct_name(SymbolId symbol) {
    const char *name = symbol_bytes(g_symbols, symbol);
    return name ? name : "";
}

/* ------------------------------------------------------------- encoding */

/*
 * Every constructor application and every token is the expression
 * (bnf NAME child...), so no grammar name ever occupies a head position a
 * MeTTa evaluator could reduce.  A name whose spelling a host reads back as
 * a literal (True, False, true, false, Empty, a number) is the string NAME.
 */
#define GCT_TAG "bnf"

static bool gct_literal_spelling(const char *name) {
    if (strcmp(name, "True") == 0 || strcmp(name, "False") == 0 ||
        strcmp(name, "true") == 0 || strcmp(name, "false") == 0 ||
        strcmp(name, "Empty") == 0)
        return true;
    if ((name[0] == '-' || name[0] == '+') && name[1] >= '0' && name[1] <= '9')
        return true;
    return name[0] >= '0' && name[0] <= '9';
}

static Atom *gct_name_atom(Arena *arena, SymbolId name) {
    const char *spelling = gct_name(name);
    return gct_literal_spelling(spelling) ? atom_string(arena, spelling)
                                          : atom_symbol_id(arena, name);
}

static Atom *gct_node(Arena *arena, SymbolId name, Atom *const *children, uint32_t len) {
    Atom **elements = malloc(((size_t)len + 2u) * sizeof(*elements));
    Atom *result;
    if (!elements)
        return NULL;
    elements[0] = atom_symbol(arena, GCT_TAG);
    elements[1] = gct_name_atom(arena, name);
    if (len > 0u)
        memcpy(elements + 2, children, (size_t)len * sizeof(*elements));
    result = atom_expr(arena, elements, (CettaExprLen)len + 2u);
    free(elements);
    return result;
}

/* A node (bnf NAME child...): its name and children. */
static bool gct_node_parts(const Atom *term, SymbolId *name, Atom *const **children,
                           uint32_t *len) {
    const Atom *head, *label;
    if (!term || term->kind != ATOM_EXPR || term->expr.len < 2u)
        return false;
    head = term->expr.elems[0];
    if (head->kind != ATOM_SYMBOL || strcmp(gct_name(head->sym_id), GCT_TAG) != 0)
        return false;
    label = term->expr.elems[1];
    if (label->kind == ATOM_SYMBOL)
        *name = label->sym_id;
    else if (label->kind == ATOM_GROUNDED && label->ground.gkind == GV_STRING)
        *name = symbol_intern_bytes(g_symbols,
                                    (const uint8_t *)label->ground.sval,
                                    label->ground.slen);
    else
        return false;
    *children = term->expr.elems + 2;
    *len = (uint32_t)term->expr.len - 2u;
    return true;
}

/* ----------------------------------------------------------------- model */

typedef enum {
    GCT_TRANSPARENT = 0, /* denotes its one child */
    GCT_LITERAL = 1,     /* denotes its text */
    GCT_CONSTRUCTOR = 2, /* (name children...) or the constant name */
    GCT_LIST = 3         /* repetitions, options, separated-list rules: a
                            list, named by key where it is delivered */
} GctKind;

enum {
    GCT_ARG = 0,    /* the child is one argument or list element */
    GCT_SPLICE = 1, /* the child is a list whose elements are spliced in */
    GCT_EXTEND = 2  /* the child list continues the previous value: x (sep x)* */
};

typedef struct {
    CettaGrammarCanonicalSymbolKindV1 kind;
    SymbolId name;
    char *text;
} GctSymbol;

typedef struct {
    SymbolId label;
    SymbolId lhs;
    SymbolId owner;
    CettaGrammarCanonicalRuleKindV1 rule_kind;
    GctSymbol *rhs;
    uint32_t rhs_len;
    uint32_t order;        /* position in the input */
    uint32_t alternative;  /* order among the productions of its left-hand side */
    uint32_t group_ordinal;/* groups: ordinal among the groups of their owner */
    char repeat;           /* repetitions and options: '*', '+' or '?' */
    SymbolId key;          /* lists: the name of their node */
    bool extended;         /* repetitions: a preceding child heads their list */
    bool single;           /* the only production of its left-hand side */
    GctKind kind;
    SymbolId constructor;
    char *literal;
    uint32_t value_len;    /* rule and token children */
    uint8_t *actions;      /* per child position */
    SymbolId list_item;    /* separated-list rules */
    bool list_item_token;
    char *list_separator;
    int8_t helper_text;    /* repetitions and options whose body has no child:
                              its text is the element, before (-1) or after (1)
                              the repetition itself; 0 otherwise */
} GctProduction;

typedef struct {
    SymbolId key;
    const GctProduction *production;
} GctIndexEntry;

typedef struct {
    GctIndexEntry *entries;
    uint32_t len;
} GctIndex;

typedef struct {
    CettaGrammarCanonicalCollisionV1 *items;
    uint32_t len;
    uint32_t cap;
} GctCollisions;

struct CettaGrammarCanonicalTableV1 {
    GctProduction *productions; /* sorted by label */
    uint32_t len;
    GctIndex by_constructor;    /* constructor -> production */
    GctIndex by_lhs;            /* left-hand side -> its productions, in order */
    GctIndex list_rules;        /* separated-list rule -> one of its productions */
    GctIndex helper_bodies;     /* repetition or option -> a non-empty production */
    GctCollisions ambiguities;  /* where two paths met: why a sort's unary
                                   alternatives are named */
};

static bool gct_is_value(const GctSymbol *symbol) {
    return symbol->kind != CETTA_GRAMMAR_CANONICAL_FIXED_SYMBOL_V1;
}

static int gct_compare_label(const void *left, const void *right) {
    const GctProduction *a = left;
    const GctProduction *b = right;
    return a->label < b->label ? -1 : a->label > b->label ? 1 : 0;
}

static int gct_compare_index(const void *left, const void *right) {
    const GctIndexEntry *a = left;
    const GctIndexEntry *b = right;
    if (a->key != b->key)
        return a->key < b->key ? -1 : 1;
    return a->production->order < b->production->order ? -1
         : a->production->order > b->production->order ? 1 : 0;
}

/* The first entry with key; entries with one key are adjacent, in order. */
static const GctIndexEntry *gct_index_find(const GctIndex *index, SymbolId key) {
    uint32_t low = 0u, high = index->len;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        if (index->entries[middle].key < key)
            low = middle + 1u;
        else
            high = middle;
    }
    return low < index->len && index->entries[low].key == key ? &index->entries[low] : NULL;
}

static const GctProduction *gct_find(const CettaGrammarCanonicalTableV1 *table, SymbolId label) {
    uint32_t low = 0u, high = table->len;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        SymbolId probe = table->productions[middle].label;
        if (probe == label)
            return &table->productions[middle];
        if (probe < label)
            low = middle + 1u;
        else
            high = middle;
    }
    return NULL;
}

static const GctProduction *gct_first_of(const CettaGrammarCanonicalTableV1 *table,
                                         SymbolId lhs) {
    const GctIndexEntry *entry = gct_index_find(&table->by_lhs, lhs);
    return entry ? entry->production : NULL;
}

static bool gct_is_helper_rule(const GctProduction *production) {
    return production->rule_kind == CETTA_GRAMMAR_CANONICAL_REPETITION_V1 ||
           production->rule_kind == CETTA_GRAMMAR_CANONICAL_OPTION_V1;
}

/* The rule kind of a symbol, or -1 for tokens, fixed terminals and unknown
 * rules. */
static int gct_rule_kind_of(const CettaGrammarCanonicalTableV1 *table, SymbolId symbol) {
    const GctProduction *first = gct_first_of(table, symbol);
    return first ? (int)first->rule_kind : -1;
}

static const GctProduction *gct_list_rule(const CettaGrammarCanonicalTableV1 *table,
                                          SymbolId symbol) {
    const GctIndexEntry *entry = gct_index_find(&table->list_rules, symbol);
    return entry ? entry->production : NULL;
}

static const GctProduction *gct_helper_body(const CettaGrammarCanonicalTableV1 *table,
                                            SymbolId helper) {
    const GctIndexEntry *entry = gct_index_find(&table->helper_bodies, helper);
    return entry ? entry->production : NULL;
}

/* A one-alternative group, or NULL. */
static const GctProduction *gct_single_group(const CettaGrammarCanonicalTableV1 *table,
                                             SymbolId symbol) {
    const GctProduction *first = gct_first_of(table, symbol);
    return first && first->rule_kind == CETTA_GRAMMAR_CANONICAL_GROUP_V1 && first->single
               ? first
               : NULL;
}

void cetta_grammar_canonical_table_free_v1(CettaGrammarCanonicalTableV1 *table) {
    uint32_t i, k;
    if (!table)
        return;
    for (i = 0u; i < table->len; i++) {
        GctProduction *production = &table->productions[i];
        for (k = 0u; k < production->rhs_len; k++)
            free(production->rhs[k].text);
        free(production->rhs);
        free(production->literal);
        free(production->actions);
        free(production->list_separator);
    }
    free(table->productions);
    free(table->by_constructor.entries);
    free(table->by_lhs.entries);
    free(table->list_rules.entries);
    free(table->helper_bodies.entries);
    free(table->ambiguities.items);
    free(table);
}

/* ------------------------------------------------------------ classifying */

/* The item of a repetition or option: the one child symbol of a non-empty
 * production other than the helper itself; none when there is not one. */
static SymbolId gct_helper_item(const CettaGrammarCanonicalTableV1 *table, SymbolId helper) {
    const GctProduction *body = gct_helper_body(table, helper);
    SymbolId item = SYMBOL_ID_NONE;
    uint32_t k, items = 0u;
    if (!body)
        return SYMBOL_ID_NONE;
    for (k = 0u; k < body->rhs_len; k++) {
        if (!gct_is_value(&body->rhs[k]) || body->rhs[k].name == helper)
            continue;
        item = body->rhs[k].name;
        items++;
    }
    return items == 1u ? item : SYMBOL_ID_NONE;
}

/* What a symbol's values are values of: a one-alternative group with one
 * child denotes that child. */
static SymbolId gct_denoted(const CettaGrammarCanonicalTableV1 *table, SymbolId symbol) {
    uint32_t depth;
    for (depth = 0u; depth < 16u; depth++) {
        const GctProduction *group = gct_single_group(table, symbol);
        uint32_t k;
        if (!group || group->kind != GCT_TRANSPARENT)
            return symbol;
        for (k = 0u; k < group->rhs_len; k++) {
            if (gct_is_value(&group->rhs[k])) {
                symbol = group->rhs[k].name;
                break;
            }
        }
    }
    return symbol;
}

/* The fixed text printed with each element of a repetition, and whether all
 * of it precedes the element: then it is a separator. */
static bool gct_element_text(const CettaGrammarCanonicalTableV1 *table, SymbolId helper,
                             char *out, size_t out_size, bool *separator) {
    const GctProduction *body = gct_helper_body(table, helper);
    bool seen_value = false;
    uint32_t k;
    out[0] = '\0';
    *separator = true;
    if (!body)
        return false;
    for (k = 0u; k < body->rhs_len; k++) {
        const GctSymbol *symbol = &body->rhs[k];
        const GctProduction *group;
        if (symbol->name == helper && gct_is_value(symbol))
            continue;
        if (!gct_is_value(symbol)) {
            if (seen_value)
                *separator = false;
            if (strlen(out) + strlen(symbol->text) + 1u > out_size)
                return false;
            strcat(out, symbol->text);
            continue;
        }
        group = gct_single_group(table, symbol->name);
        if (group && group->kind == GCT_TRANSPARENT) {
            uint32_t g;
            bool group_value = false;
            for (g = 0u; g < group->rhs_len; g++) {
                if (gct_is_value(&group->rhs[g])) {
                    group_value = true;
                    continue;
                }
                if (group_value)
                    *separator = false;
                if (strlen(out) + strlen(group->rhs[g].text) + 1u > out_size)
                    return false;
                strcat(out, group->rhs[g].text);
            }
        }
        seen_value = true;
    }
    return true;
}

static bool gct_classify(CettaGrammarCanonicalTableV1 *table, GctProduction *production) {
    uint32_t k, values = 0u;
    size_t literal_len = 0u;
    for (k = 0u; k < production->rhs_len; k++) {
        if (gct_is_value(&production->rhs[k]))
            values++;
        else
            literal_len += strlen(production->rhs[k].text);
    }
    production->value_len = values;
    if (gct_is_helper_rule(production)) {
        /* A body with no child besides the repetition itself denotes its
         * text, so that presence is kept: "-"? is () or ("-"). */
        uint32_t own = 0u, first_fixed = UINT32_MAX, recursion = UINT32_MAX;
        production->kind = GCT_LIST;
        for (k = 0u; k < production->rhs_len; k++) {
            if (!gct_is_value(&production->rhs[k])) {
                if (first_fixed == UINT32_MAX)
                    first_fixed = k;
            } else if (production->rhs[k].name == production->lhs) {
                recursion = k;
            } else {
                own++;
            }
        }
        if (own == 0u && first_fixed != UINT32_MAX) {
            production->helper_text = recursion != UINT32_MAX && recursion < first_fixed ? 1 : -1;
            production->literal = malloc(literal_len + 1u);
            if (!production->literal)
                return false;
            production->literal[0] = '\0';
            for (k = 0u; k < production->rhs_len; k++) {
                if (!gct_is_value(&production->rhs[k]))
                    strcat(production->literal, production->rhs[k].text);
            }
        }
        return true;
    }
    if (production->rhs_len > 0u && values == 0u) {
        production->kind = GCT_LITERAL;
        production->literal = malloc(literal_len + 1u);
        if (!production->literal)
            return false;
        production->literal[0] = '\0';
        for (k = 0u; k < production->rhs_len; k++)
            strcat(production->literal, production->rhs[k].text);
        return true;
    }
    /* Only exactly one rule or token is transparent; a group with fixed text
     * or several children is a constructor like any other production. */
    (void)table;
    if (production->rhs_len == 1u && values == 1u)
        production->kind = GCT_TRANSPARENT;
    else
        production->kind = GCT_CONSTRUCTOR;
    if (production->kind == GCT_CONSTRUCTOR) {
        production->actions = calloc(values ? values : 1u, sizeof(*production->actions));
        if (!production->actions)
            return false;
    }
    return true;
}

/* x (sep x)*: a child followed by a repetition whose items denote it. */
static void gct_mark_extensions(const CettaGrammarCanonicalTableV1 *table,
                                GctProduction *production) {
    uint32_t k, position = 0u;
    if (!production->actions)
        return;
    for (k = 0u; k < production->rhs_len; k++) {
        const GctSymbol *symbol = &production->rhs[k];
        if (!gct_is_value(symbol))
            continue;
        if (k > 0u && gct_is_value(&production->rhs[k - 1u]) &&
            gct_rule_kind_of(table, symbol->name) == CETTA_GRAMMAR_CANONICAL_REPETITION_V1) {
            SymbolId item = gct_helper_item(table, symbol->name);
            char text[256];
            bool separator = false;
            if (item != SYMBOL_ID_NONE &&
                gct_denoted(table, item) == production->rhs[k - 1u].name &&
                gct_element_text(table, symbol->name, text, sizeof(text), &separator) &&
                separator)
                production->actions[position] = GCT_EXTEND;
        }
        position++;
    }
}

/* A rule whose two alternatives are x and x sep... R (or R sep... x), with R
 * the rule itself and sep fixed terminals, denotes the list of its x values:
 * its language is x (sep x)*.  Both productions become lists. */
static bool gct_mark_separated_list(GctProduction *base, GctProduction *step) {
    const GctSymbol *rhs = step->rhs;
    uint32_t last, k, value_index = 0u;
    size_t separator_len = 0u;
    SymbolId rule = base->lhs, x;
    bool right;
    if (base->kind != GCT_TRANSPARENT || step->kind != GCT_CONSTRUCTOR ||
        step->value_len != 2u || step->rhs_len < 2u)
        return true;
    x = base->rhs[0].name;
    last = step->rhs_len - 1u;
    right = rhs[0].name == x && gct_is_value(&rhs[0]) && rhs[last].name == rule &&
            rhs[last].kind == CETTA_GRAMMAR_CANONICAL_RULE_SYMBOL_V1;
    if (!right && !(rhs[0].name == rule && rhs[0].kind == CETTA_GRAMMAR_CANONICAL_RULE_SYMBOL_V1 &&
                    rhs[last].name == x && gct_is_value(&rhs[last])))
        return true;
    for (k = 1u; k < last; k++) {
        if (gct_is_value(&rhs[k]))
            return true;
        separator_len += strlen(rhs[k].text);
    }
    base->kind = GCT_LIST;
    step->kind = GCT_LIST;
    free(base->actions);
    base->actions = calloc(1u, sizeof(*base->actions));
    base->list_separator = malloc(separator_len + 1u);
    if (!base->actions || !base->list_separator)
        return false;
    base->list_separator[0] = '\0';
    for (k = 1u; k < last; k++)
        strcat(base->list_separator, rhs[k].text);
    step->list_separator = gct_strdup(base->list_separator);
    if (!step->list_separator)
        return false;
    base->list_item = step->list_item = x;
    base->list_item_token = step->list_item_token =
        base->rhs[0].kind == CETTA_GRAMMAR_CANONICAL_TOKEN_SYMBOL_V1;
    for (k = 0u; k < step->rhs_len; k++) {
        if (!gct_is_value(&rhs[k]))
            continue;
        step->actions[value_index++] = rhs[k].name == rule ? GCT_SPLICE : GCT_ARG;
    }
    return true;
}

static bool gct_index_add(GctIndex *index, uint32_t cap, SymbolId key,
                          const GctProduction *production) {
    if (index->len >= cap)
        return false;
    index->entries[index->len].key = key;
    index->entries[index->len].production = production;
    index->len++;
    return true;
}

static bool gct_sort_collisions(const CettaGrammarCanonicalTableV1 *table, SymbolId sort,
                                GctCollisions *collisions);
static bool gct_reaches_through_transparent(const CettaGrammarCanonicalTableV1 *table,
                                            SymbolId from, SymbolId to);

static bool gct_build_indices(CettaGrammarCanonicalTableV1 *table) {
    uint32_t p, cap = table->len + 1u;
    free(table->by_constructor.entries);
    free(table->by_lhs.entries);
    free(table->list_rules.entries);
    free(table->helper_bodies.entries);
    memset(&table->by_constructor, 0, sizeof(table->by_constructor));
    memset(&table->by_lhs, 0, sizeof(table->by_lhs));
    memset(&table->list_rules, 0, sizeof(table->list_rules));
    memset(&table->helper_bodies, 0, sizeof(table->helper_bodies));
    table->by_constructor.entries = calloc(cap, sizeof(GctIndexEntry));
    table->by_lhs.entries = calloc(cap, sizeof(GctIndexEntry));
    table->list_rules.entries = calloc(cap, sizeof(GctIndexEntry));
    table->helper_bodies.entries = calloc(cap, sizeof(GctIndexEntry));
    if (!table->by_constructor.entries || !table->by_lhs.entries ||
        !table->list_rules.entries || !table->helper_bodies.entries)
        return false;
    for (p = 0u; p < table->len; p++) {
        const GctProduction *production = &table->productions[p];
        if (production->kind == GCT_CONSTRUCTOR &&
            !gct_index_add(&table->by_constructor, cap, production->constructor, production))
            return false;
        /* A list node is named like a constructor; one entry per list. */
        if (production->kind == GCT_LIST && production->key != SYMBOL_ID_NONE &&
            production->alternative == 0u &&
            !gct_index_add(&table->by_constructor, cap, production->key, production))
            return false;
        if (!gct_index_add(&table->by_lhs, cap, production->lhs, production))
            return false;
        if (production->rule_kind == CETTA_GRAMMAR_CANONICAL_AUTHORED_V1 &&
            production->kind == GCT_LIST &&
            !gct_index_add(&table->list_rules, cap, production->lhs, production))
            return false;
        if (gct_is_helper_rule(production) && production->rhs_len > 0u &&
            !gct_index_add(&table->helper_bodies, cap, production->lhs, production))
            return false;
    }
    qsort(table->by_constructor.entries, table->by_constructor.len, sizeof(GctIndexEntry),
          gct_compare_index);
    qsort(table->by_lhs.entries, table->by_lhs.len, sizeof(GctIndexEntry), gct_compare_index);
    qsort(table->list_rules.entries, table->list_rules.len, sizeof(GctIndexEntry),
          gct_compare_index);
    qsort(table->helper_bodies.entries, table->helper_bodies.len, sizeof(GctIndexEntry),
          gct_compare_index);
    return true;
}

bool cetta_grammar_canonical_table_build_v1(
    const CettaGrammarCanonicalProductionV1 *productions,
    uint32_t production_len,
    CettaGrammarCanonicalTableV1 **out,
    char *error,
    size_t error_size) {
    CettaGrammarCanonicalTableV1 *table = NULL;
    uint32_t p, q, k;

    if (out)
        *out = NULL;
    if ((!productions && production_len > 0u) || !out)
        return gct_error(error, error_size, "canonical table: bad arguments");
    table = calloc(1u, sizeof(*table));
    if (!table)
        return gct_error(error, error_size, "canonical table: out of memory");
    table->productions = calloc(production_len ? production_len : 1u,
                                sizeof(*table->productions));
    if (!table->productions)
        goto oom;
    for (p = 0u; p < production_len; p++) {
        const CettaGrammarCanonicalProductionV1 *source = &productions[p];
        GctProduction *production = &table->productions[table->len];
        production->label = source->label;
        production->lhs = source->lhs;
        production->owner = source->owner;
        production->rule_kind = source->rule_kind;
        production->repeat = source->repeat ? source->repeat
                             : source->rule_kind == CETTA_GRAMMAR_CANONICAL_OPTION_V1 ? '?'
                                                                                         : '*';
        production->order = p;
        if (source->rule_kind != CETTA_GRAMMAR_CANONICAL_AUTHORED_V1 &&
            source->owner == SYMBOL_ID_NONE) {
            SymbolId orphan = source->lhs;
            table->len++;
            cetta_grammar_canonical_table_free_v1(table);
            return gct_error(error, error_size, "canonical table: helper %s names no owner rule",
                             gct_name(orphan));
        }
        production->rhs_len = source->rhs_len;
        table->len++;
        if (source->rhs_len > 0u) {
            production->rhs = calloc(source->rhs_len, sizeof(*production->rhs));
            if (!production->rhs)
                goto oom;
        }
        for (k = 0u; k < source->rhs_len; k++) {
            production->rhs[k].kind = source->rhs[k].kind;
            production->rhs[k].name = source->rhs[k].name;
            if (source->rhs[k].kind == CETTA_GRAMMAR_CANONICAL_FIXED_SYMBOL_V1) {
                if (!source->rhs[k].text) {
                    SymbolId missing = source->rhs[k].name;
                    cetta_grammar_canonical_table_free_v1(table);
                    return gct_error(error, error_size,
                                     "canonical table: terminal %s has no fixed text",
                                     gct_name(missing));
                }
                production->rhs[k].text = gct_strdup(source->rhs[k].text);
                if (!production->rhs[k].text)
                    goto oom;
            }
        }
    }
    /* Alternatives, single-alternative rules and group ordinals. */
    for (p = 0u; p < table->len; p++) {
        GctProduction *production = &table->productions[p];
        uint32_t alternative = 0u, count = 0u;
        for (q = 0u; q < table->len; q++) {
            const GctProduction *other = &table->productions[q];
            if (other->lhs != production->lhs)
                continue;
            if (other->rule_kind != production->rule_kind) {
                cetta_grammar_canonical_table_free_v1(table);
                return gct_error(error, error_size,
                                 "canonical table: %s mixes rule kinds",
                                 gct_name(production->lhs));
            }
            if (q < p)
                alternative++;
            count++;
        }
        production->alternative = alternative;
        production->single = count == 1u;
        if (production->rule_kind == CETTA_GRAMMAR_CANONICAL_GROUP_V1) {
            uint32_t ordinal = 1u;
            SymbolId seen[256];
            uint32_t seen_len = 0u, s;
            for (q = 0u; q < p; q++) {
                const GctProduction *other = &table->productions[q];
                bool known = false;
                if (other->rule_kind != CETTA_GRAMMAR_CANONICAL_GROUP_V1 ||
                    other->owner != production->owner || other->lhs == production->lhs)
                    continue;
                for (s = 0u; s < seen_len && !known; s++)
                    known = seen[s] == other->lhs;
                if (!known && seen_len < 256u) {
                    seen[seen_len++] = other->lhs;
                    ordinal++;
                }
            }
            production->group_ordinal = ordinal;
        }
    }
    if (!gct_build_indices(table))
        goto oom;
    for (p = 0u; p < table->len; p++) {
        if (!gct_classify(table, &table->productions[p]))
            goto oom;
    }
    if (!gct_build_indices(table))
        goto oom;
    for (p = 0u; p < table->len; p++)
        gct_mark_extensions(table, &table->productions[p]);
    /* A repetition that continues its preceding child is one list with it. */
    for (p = 0u; p < table->len; p++) {
        const GctProduction *production = &table->productions[p];
        uint32_t position = 0u;
        if (!production->actions)
            continue;
        for (k = 0u; k < production->rhs_len; k++) {
            if (!gct_is_value(&production->rhs[k]))
                continue;
            if (position < production->value_len &&
                production->actions[position] == GCT_EXTEND) {
                for (q = 0u; q < table->len; q++) {
                    if (table->productions[q].lhs == production->rhs[k].name)
                        table->productions[q].extended = true;
                }
            }
            position++;
        }
    }
    /* Separated lists: authored rules with exactly two productions. */
    for (p = 0u; p < table->len; p++) {
        GctProduction *first = &table->productions[p];
        GctProduction *second = NULL;
        if (first->rule_kind != CETTA_GRAMMAR_CANONICAL_AUTHORED_V1 || first->alternative != 0u)
            continue;
        for (q = 0u; q < table->len; q++) {
            if (table->productions[q].lhs == first->lhs && table->productions[q].alternative == 1u)
                second = &table->productions[q];
            if (table->productions[q].lhs == first->lhs && table->productions[q].alternative > 1u) {
                second = NULL;
                break;
            }
        }
        if (!second)
            continue;
        if (!gct_mark_separated_list(first, second) || !gct_mark_separated_list(second, first))
            goto oom;
    }
    /* List names: a separated-list rule by its rule; a repetition or option
     * by its owner, its operator and its ordinal among the owner's helpers of
     * that operator, in lowering order. */
    for (p = 0u; p < table->len; p++) {
        GctProduction *production = &table->productions[p];
        uint32_t ordinal = 1u;
        char name[600];
        if (production->kind != GCT_LIST)
            continue;
        if (production->rule_kind == CETTA_GRAMMAR_CANONICAL_AUTHORED_V1) {
            production->key = production->lhs;
            continue;
        }
        for (q = 0u; q < p && production->key == SYMBOL_ID_NONE; q++) {
            if (table->productions[q].lhs == production->lhs)
                production->key = table->productions[q].key;
        }
        if (production->key != SYMBOL_ID_NONE)
            continue;
        for (q = 0u; q < p; q++) {
            const GctProduction *other = &table->productions[q];
            bool first = true;
            uint32_t r;
            if (!gct_is_helper_rule(other) || other->owner != production->owner ||
                other->repeat != production->repeat || other->lhs == production->lhs)
                continue;
            for (r = 0u; r < q && first; r++)
                first = table->productions[r].lhs != other->lhs;
            if (first)
                ordinal++;
        }
        snprintf(name, sizeof(name), "%s%c%u", gct_name(production->owner), production->repeat,
                 ordinal);
        production->key = symbol_intern_cstr(g_symbols, name);
    }
    /* A unary alternative is omitted, and a literal-only alternative is its
     * text, only where the sort's other alternatives cannot make the same
     * value: where two paths from a sort meet one rule, token class or
     * literal text (the grammar is ambiguous there), every unary and
     * literal-only alternative of that sort is a constructor named by its
     * label, so "ab" | "a" "b" reads to two terms that print alike.  Sorts
     * are settled bottom-up, so a sort whose ambiguity lies below it in
     * another ambiguous sort keeps its alternatives as they are. */
    if (!gct_build_indices(table))
        goto oom;
    for (;;) {
        SymbolId *ambiguous = NULL;
        uint32_t ambiguous_len = 0u, settled = 0u, a, b;
        bool failed = false;
        ambiguous = malloc((table->len ? table->len : 1u) * sizeof(*ambiguous));
        if (!ambiguous)
            goto oom;
        for (p = 0u; p < table->by_lhs.len && !failed; p++) {
            GctCollisions found = {0};
            SymbolId sort = table->by_lhs.entries[p].key;
            bool choice = false;
            if (p > 0u && sort == table->by_lhs.entries[p - 1u].key)
                continue;
            if (!gct_sort_collisions(table, sort, &found)) {
                failed = true;
            } else {
                for (q = 0u; q < found.len; q++)
                    choice |= found.items[q].kind != CETTA_GRAMMAR_CANONICAL_SEQUENCE_COLLISION_V1;
            }
            free(found.items);
            if (choice)
                ambiguous[ambiguous_len++] = sort;
        }
        if (failed) {
            free(ambiguous);
            goto oom;
        }
        if (ambiguous_len == 0u) {
            free(ambiguous);
            break;
        }
        for (a = 0u; a <= ambiguous_len; a++) {
            /* a == ambiguous_len: every ambiguous sort reaches another (a
             * cycle); name them all. */
            for (b = 0u; b < ambiguous_len; b++) {
                bool below = false;
                uint32_t c;
                if (a < ambiguous_len && b != a)
                    continue;
                for (c = 0u; c < ambiguous_len && a < ambiguous_len && !below; c++)
                    below = c != b && gct_reaches_through_transparent(table, ambiguous[b],
                                                                     ambiguous[c]);
                if (below)
                    continue;
                {
                    /* Keep what made this sort ambiguous for the diagnostic. */
                    GctCollisions found = {0};
                    bool ok = gct_sort_collisions(table, ambiguous[b], &found);
                    for (q = 0u; ok && q < found.len; q++) {
                        CettaGrammarCanonicalCollisionV1 *item = &found.items[q];
                        if (item->kind == CETTA_GRAMMAR_CANONICAL_SEQUENCE_COLLISION_V1)
                            continue;
                        if (table->ambiguities.len == table->ambiguities.cap) {
                            uint32_t cap = table->ambiguities.cap ? table->ambiguities.cap * 2u
                                                                  : 8u;
                            CettaGrammarCanonicalCollisionV1 *grown = realloc(
                                table->ambiguities.items, (size_t)cap * sizeof(*grown));
                            if (!grown) {
                                ok = false;
                                break;
                            }
                            table->ambiguities.items = grown;
                            table->ambiguities.cap = cap;
                        }
                        table->ambiguities.items[table->ambiguities.len++] = *item;
                    }
                    free(found.items);
                    if (!ok) {
                        free(ambiguous);
                        goto oom;
                    }
                }
                for (q = 0u; q < table->len; q++) {
                    GctProduction *production = &table->productions[q];
                    if (production->lhs != ambiguous[b] ||
                        (production->kind != GCT_TRANSPARENT && production->kind != GCT_LITERAL))
                        continue;
                    production->kind = GCT_CONSTRUCTOR;
                    production->actions = calloc(1u, sizeof(*production->actions));
                    if (!production->actions) {
                        free(ambiguous);
                        goto oom;
                    }
                    settled++;
                }
            }
            if (settled > 0u)
                break;
        }
        free(ambiguous);
        if (settled == 0u)
            break;
        if (!gct_build_indices(table))
            goto oom;
    }
    /* Constructor names: the rule, and the alternative index when the rule has
     * several constructor productions; groups with several alternatives are
     * named by their owner and ordinal. */
    for (p = 0u; p < table->len; p++) {
        GctProduction *production = &table->productions[p];
        uint32_t constructors = 0u;
        char base[512], name[600];
        if (production->kind != GCT_CONSTRUCTOR)
            continue;
        for (q = 0u; q < table->len; q++) {
            if (table->productions[q].lhs == production->lhs &&
                table->productions[q].kind == GCT_CONSTRUCTOR)
                constructors++;
        }
        if (production->rule_kind == CETTA_GRAMMAR_CANONICAL_GROUP_V1)
            snprintf(base, sizeof(base), "%s.%u", gct_name(production->owner),
                     production->group_ordinal);
        else
            snprintf(base, sizeof(base), "%s", gct_name(production->lhs));
        if (constructors > 1u)
            snprintf(name, sizeof(name), "%s/%u", base, production->alternative);
        else
            snprintf(name, sizeof(name), "%s", base);
        production->constructor = symbol_intern_cstr(g_symbols, name);
    }
    /* Tokens and constructors share one namespace in canonical terms. */
    for (p = 0u; p < table->len; p++) {
        const GctProduction *production = &table->productions[p];
        for (k = 0u; k < production->rhs_len; k++) {
            const GctSymbol *symbol = &production->rhs[k];
            if (symbol->kind != CETTA_GRAMMAR_CANONICAL_TOKEN_SYMBOL_V1)
                continue;
            for (q = 0u; q < table->len; q++) {
                if (table->productions[q].lhs == symbol->name ||
                    (table->productions[q].kind == GCT_CONSTRUCTOR &&
                     table->productions[q].constructor == symbol->name) ||
                    (table->productions[q].kind == GCT_LIST &&
                     table->productions[q].key == symbol->name)) {
                    SymbolId clash = symbol->name;
                    cetta_grammar_canonical_table_free_v1(table);
                    return gct_error(error, error_size,
                                     "canonical table: token %s is also a rule",
                                     gct_name(clash));
                }
            }
        }
    }
    qsort(table->productions, table->len, sizeof(*table->productions), gct_compare_label);
    for (p = 1u; p < table->len; p++) {
        const GctProduction *left = &table->productions[p - 1u];
        const GctProduction *right = &table->productions[p];
        /* The productions of one repetition or option may share a label:
         * both denote its list. */
        if (left->label == right->label && left->lhs == right->lhs && gct_is_helper_rule(left) &&
            gct_is_helper_rule(right))
            continue;
        if (right->label == left->label) {
            SymbolId duplicate = table->productions[p].label;
            cetta_grammar_canonical_table_free_v1(table);
            return gct_error(error, error_size, "canonical table: duplicate production label %s",
                             gct_name(duplicate));
        }
    }
    if (!gct_build_indices(table))
        goto oom;
    for (p = 1u; p < table->by_constructor.len; p++) {
        if (table->by_constructor.entries[p].key == table->by_constructor.entries[p - 1u].key) {
            SymbolId duplicate = table->by_constructor.entries[p].key;
            cetta_grammar_canonical_table_free_v1(table);
            return gct_error(error, error_size, "canonical table: duplicate constructor %s",
                             gct_name(duplicate));
        }
    }
    *out = table;
    return true;
oom:
    cetta_grammar_canonical_table_free_v1(table);
    return gct_error(error, error_size, "canonical table: out of memory");
}

/* ---------------------------------------------------------- SLR adapter */

static const CettaGrammarCanonicalOriginV1 *gct_slr_origin(
    const CettaGrammarCanonicalOriginV1 *origins, uint32_t origin_len, SymbolId symbol) {
    for (uint32_t i = 0u; i < origin_len; i++) {
        if (origins[i].helper == symbol)
            return &origins[i];
    }
    return NULL;
}

bool cetta_grammar_canonical_table_from_slr_v1(
    const CettaLpNativeSlrProgram *slr,
    const CettaGrammarCanonicalTerminalsV1 *terminals,
    const CettaGrammarCanonicalOriginV1 *origins,
    uint32_t origin_len,
    CettaGrammarCanonicalTableV1 **out,
    char *error,
    size_t error_size) {
    CettaGrammarCanonicalProductionV1 *productions = NULL;
    CettaGrammarCanonicalSymbolV1 *symbols = NULL;
    uint32_t p, k, len = 0u, used = 0u;
    bool ok;

    if (out)
        *out = NULL;
    if (!slr || !terminals || !terminals->terminal_is_value ||
        !terminals->terminal_fixed_text || !out || (!origins && origin_len > 0u))
        return gct_error(error, error_size, "canonical table: bad arguments");
    productions = calloc(slr->production_len ? slr->production_len : 1u, sizeof(*productions));
    symbols = calloc(slr->rhs_len ? slr->rhs_len : 1u, sizeof(*symbols));
    if (!productions || !symbols) {
        free(productions);
        free(symbols);
        return gct_error(error, error_size, "canonical table: out of memory");
    }
    for (p = 0u; p < slr->production_len; p++) {
        const CettaLpNativeSlrProgramProduction *production = &slr->productions[p];
        CettaGrammarCanonicalProductionV1 *entry;
        const CettaGrammarCanonicalOriginV1 *origin =
            gct_slr_origin(origins, origin_len, production->lhs);
        if (!production->authored && !origin)
            continue;
        entry = &productions[len++];
        entry->label = production->label;
        entry->lhs = production->lhs;
        entry->rule_kind = origin ? origin->kind : CETTA_GRAMMAR_CANONICAL_AUTHORED_V1;
        entry->repeat = origin ? origin->repeat : 0;
        entry->owner = origin ? origin->owner : SYMBOL_ID_NONE;
        entry->rhs = &symbols[used];
        entry->rhs_len = production->rhs_len;
        for (k = 0u; k < production->rhs_len; k++) {
            const CettaLpNativeSymbol *symbol = &slr->rhs[production->rhs_begin + k];
            CettaGrammarCanonicalSymbolV1 *target = &symbols[used++];
            target->name = symbol->name;
            if (symbol->kind != CETTA_LP_NATIVE_SYMBOL_TM) {
                target->kind = CETTA_GRAMMAR_CANONICAL_RULE_SYMBOL_V1;
            } else if (terminals->terminal_is_value(terminals->context, symbol->name)) {
                target->kind = CETTA_GRAMMAR_CANONICAL_TOKEN_SYMBOL_V1;
            } else {
                target->kind = CETTA_GRAMMAR_CANONICAL_FIXED_SYMBOL_V1;
                target->text = terminals->terminal_fixed_text(terminals->context, symbol->name);
            }
        }
    }
    ok = cetta_grammar_canonical_table_build_v1(productions, len, out, error, error_size);
    free(productions);
    free(symbols);
    return ok;
}

/* ------------------------------------------------------------------ walk */

typedef struct {
    const Atom *node;
    const GctProduction *production;
    uint32_t next_child;
    uint32_t position;    /* children delivered so far */
    GctValues values;
} GctFrame;

static const char *gct_label(const Atom *node) {
    const Atom *label;
    if (!node || node->kind != ATOM_EXPR || node->expr.len < 4u)
        return NULL;
    label = node->expr.elems[1];
    if (label->kind == ATOM_SYMBOL)
        return symbol_bytes(g_symbols, label->sym_id);
    if (label->kind == ATOM_GROUNDED && label->ground.gkind == GV_STRING)
        return label->ground.sval;
    return NULL;
}

static bool gct_is_cst(const Atom *node) {
    return node && node->kind == ATOM_EXPR && node->expr.len >= 4u &&
           node->expr.elems[0]->kind == ATOM_SYMBOL &&
           strcmp(gct_name(node->expr.elems[0]->sym_id), "CstRuleV1") == 0;
}

static Atom *gct_finish(Arena *arena, const GctProduction *production, GctValues *values,
                        char *error, size_t error_size) {
    switch (production->kind) {
    case GCT_TRANSPARENT:
        if (values->len != 1u) {
            gct_error(error, error_size, "canonical term: transparent %s has %u values",
                      gct_name(production->label), values->len);
            return NULL;
        }
        return values->items[0];
    case GCT_LITERAL:
        return atom_string(arena, production->literal);
    case GCT_LIST:
        if (production->helper_text != 0) {
            Atom *text = atom_string(arena, production->literal);
            Atom **elements = malloc(((size_t)values->len + 1u) * sizeof(*elements));
            Atom *result;
            if (!elements)
                return NULL;
            if (production->helper_text < 0) {
                elements[0] = text;
                memcpy(elements + 1, values->items, (size_t)values->len * sizeof(*elements));
            } else {
                memcpy(elements, values->items, (size_t)values->len * sizeof(*elements));
                elements[values->len] = text;
            }
            result = atom_list(arena, elements, (CettaExprLen)values->len + 1u);
            free(elements);
            return result;
        }
        return atom_list(arena, values->items, (CettaExprLen)values->len);
    case GCT_CONSTRUCTOR:
        return gct_node(arena, production->constructor, values->items, values->len);
    }
    return NULL;
}

/* A list as the value it is outside its own rule: its node. */
static Atom *gct_list_node(Arena *arena, const GctProduction *production, Atom *list) {
    return gct_node(arena, production->key, &list, 1u);
}

/* Deliver a finished child value to its parent frame.  A list stays a bare
 * list while its own rule extends it and is named where it is delivered. */
static bool gct_deliver(GctFrame *parent, const GctProduction *child_production, Atom *child,
                        Arena *arena) {
    const GctProduction *production = parent->production;
    uint8_t action = GCT_ARG;
    uint32_t i;
    if (gct_is_helper_rule(production)) {
        if (child_production && child_production->lhs == production->lhs)
            action = GCT_SPLICE;
    } else if (production->actions && parent->position < production->value_len) {
        action = production->actions[parent->position];
    }
    parent->position++;
    Atom *const *child_elems = NULL;
    CettaExprLen child_len = 0u;
    bool child_list = atom_is_list(child) &&
                      atom_sequence_view(child, &child_elems, &child_len);
    if (action == GCT_SPLICE) {
        if (!child_list)
            return false;
        for (i = 0u; i < child_len; i++) {
            if (!gct_push(&parent->values, child_elems[i]))
                return false;
        }
        return true;
    }
    if (action == GCT_EXTEND && parent->values.len > 0u && child_list) {
        /* x (sep x)*: the previous value heads the list this repetition continues. */
        Atom *head = parent->values.items[parent->values.len - 1u];
        Atom **elements = malloc(((size_t)child_len + 1u) * sizeof(*elements));
        Atom *list;
        if (!elements)
            return false;
        elements[0] = head;
        memcpy(elements + 1, child_elems, (size_t)child_len * sizeof(*elements));
        list = atom_list(arena, elements, child_len + 1u);
        free(elements);
        if (!list || !child_production || !(list = gct_list_node(arena, child_production, list)))
            return false;
        parent->values.items[parent->values.len - 1u] = list;
        return true;
    }
    if (child_production && child_production->kind == GCT_LIST &&
        !(child = gct_list_node(arena, child_production, child)))
        return false;
    return gct_push(&parent->values, child);
}

bool cetta_grammar_canonical_from_cst_v1(
    const CettaGrammarCanonicalTableV1 *table,
    const Atom *cst,
    const char *epsilon_label,
    CettaGrammarCanonicalLeafV1 leaf,
    void *leaf_context,
    Arena *arena,
    Atom **out,
    char *error,
    size_t error_size) {
    GctFrame *frames = NULL;
    uint32_t depth = 0u, cap = 0u;
    bool ok = false;

    if (out)
        *out = NULL;
    if (!table || !cst || !leaf || !arena || !out)
        return gct_error(error, error_size, "canonical term: bad arguments");
    for (;;) {
        const Atom *node;
        const char *label;
        const GctProduction *production;
        GctFrame *top;
        if (depth == 0u) {
            node = cst;
        } else {
            top = &frames[depth - 1u];
            if (4u + top->next_child < top->node->expr.len) {
                node = top->node->expr.elems[4u + top->next_child++];
            } else {
                Atom *value;
                production = top->production;
                if (!gct_is_helper_rule(production) &&
                    top->position != production->value_len) {
                    gct_error(error, error_size, "canonical term: %s delivered %u of %u values",
                              gct_name(production->label), top->position,
                              production->value_len);
                    free(top->values.items);
                    depth--;
                    goto done;
                }
                value = gct_finish(arena, production, &top->values, error, error_size);
                free(top->values.items);
                depth--;
                if (!value)
                    goto done;
                if (depth == 0u) {
                    if (production->kind == GCT_LIST &&
                        !(value = gct_list_node(arena, production, value)))
                        goto done;
                    *out = value;
                    ok = true;
                    goto done;
                }
                if (!gct_deliver(&frames[depth - 1u], production, value, arena)) {
                    gct_error(error, error_size, "canonical term: out of memory");
                    goto done;
                }
                continue;
            }
        }
        if (!gct_is_cst(node)) {
            gct_error(error, error_size, "canonical term: expected a CstRuleV1 node");
            goto done;
        }
        label = gct_label(node);
        if (!label) {
            gct_error(error, error_size, "canonical term: node without a label");
            goto done;
        }
        if (epsilon_label && strcmp(label, epsilon_label) == 0) {
            if (depth == 0u) {
                gct_error(error, error_size, "canonical term: empty root");
                goto done;
            }
            continue;
        }
        production = gct_find(table, symbol_intern_cstr(g_symbols, label));
        if (!production) {
            SymbolId token_name = SYMBOL_ID_NONE;
            Atom *text = NULL, *token;
            if (!leaf(leaf_context, arena, node, label, &token_name, &text) || !text) {
                gct_error(error, error_size, "canonical term: unknown production %s", label);
                goto done;
            }
            token = gct_node(arena, token_name, &text, 1u);
            if (!token) {
                gct_error(error, error_size, "canonical term: out of memory");
                goto done;
            }
            if (depth == 0u) {
                *out = token;
                ok = true;
                goto done;
            }
            if (!gct_deliver(&frames[depth - 1u], NULL, token, arena)) {
                gct_error(error, error_size, "canonical term: out of memory");
                goto done;
            }
            continue;
        }
        if (depth == cap) {
            uint32_t grown_cap = cap ? cap * 2u : 64u;
            GctFrame *grown = realloc(frames, (size_t)grown_cap * sizeof(*grown));
            if (!grown) {
                gct_error(error, error_size, "canonical term: out of memory");
                goto done;
            }
            frames = grown;
            cap = grown_cap;
        }
        frames[depth].node = node;
        frames[depth].production = production;
        frames[depth].next_child = 0u;
        frames[depth].position = 0u;
        memset(&frames[depth].values, 0, sizeof(frames[depth].values));
        depth++;
    }
done:
    while (depth > 0u)
        free(frames[--depth].values.items);
    free(frames);
    return ok;
}

/* ------------------------------------------------------------- language */

typedef struct {
    Arena *arena;
    const CettaGrammarCanonicalTableV1 *table;
    GctValues types;
    GctValues rows;
    const char **declared;
    uint32_t declared_len;
    uint32_t declared_cap;
} GctLanguageBuilder;

static Atom *gct_text(Arena *arena, const char *text) {
    return atom_string(arena, text ? text : "");
}

static Atom *gct_lcons(Arena *arena, Atom **items, uint32_t len) {
    Atom *list = atom_symbol(arena, "LNil");
    uint32_t i;
    for (i = len; i > 0u; i--)
        list = atom_expr3(arena, atom_symbol(arena, "LCons"), items[i - 1u], list);
    return list;
}

static Atom *gct_head(Arena *arena, const char *head, Atom **arguments, uint32_t len) {
    Atom *elements[6];
    uint32_t i;
    elements[0] = atom_symbol(arena, head);
    for (i = 0u; i < len; i++)
        elements[i + 1u] = arguments[i];
    return atom_expr(arena, elements, len + 1u);
}

static Atom *gct_base_name(Arena *arena, const char *sort) {
    Atom *name = gct_text(arena, sort);
    return gct_head(arena, "TBase", &name, 1u);
}

static Atom *gct_vector(Arena *arena, Atom *element) {
    Atom *arguments[2];
    arguments[0] = atom_string(arena, "Mettapedia.OSLF.MeTTaIL.Syntax.CollType.vec");
    arguments[1] = element;
    return gct_head(arena, "TCollection", arguments, 2u);
}

static bool gct_declare(GctLanguageBuilder *builder, const char *sort) {
    uint32_t i;
    for (i = 0u; i < builder->declared_len; i++) {
        if (strcmp(builder->declared[i], sort) == 0)
            return true;
    }
    if (builder->declared_len == builder->declared_cap) {
        uint32_t cap = builder->declared_cap ? builder->declared_cap * 2u : 64u;
        const char **grown = realloc(builder->declared, (size_t)cap * sizeof(*grown));
        if (!grown)
            return false;
        builder->declared = grown;
        builder->declared_cap = cap;
    }
    {
        Atom *arguments[2];
        arguments[0] = gct_text(builder->arena, sort);
        arguments[1] = atom_symbol(builder->arena, "CarrierAst");
        builder->declared[builder->declared_len++] = arguments[0]->ground.sval;
        return gct_push(&builder->types, gct_head(builder->arena, "TypeDecl", arguments, 2u));
    }
}

/* The sort name of a group: its owner and ordinal. */
static void gct_group_sort(const GctProduction *group, char *out, size_t out_size) {
    snprintf(out, out_size, "%s.%u", gct_name(group->owner), group->group_ordinal);
}

/* The type of a child symbol: a token sort, a rule sort, a group sort, or a
 * vector for lists. */
static Atom *gct_symbol_type(GctLanguageBuilder *builder, SymbolId symbol, uint32_t depth) {
    const CettaGrammarCanonicalTableV1 *table = builder->table;
    const GctProduction *first = gct_first_of(table, symbol);
    char sort[512];
    if (depth > 16u)
        return NULL;
    if (first && first->kind == GCT_LIST) {
        /* A list is a sort of its own, named by its node. */
        if (!gct_declare(builder, gct_name(first->key)))
            return NULL;
        return gct_base_name(builder->arena, gct_name(first->key));
    }
    if (first && first->rule_kind == CETTA_GRAMMAR_CANONICAL_GROUP_V1) {
        if (first->single && first->kind == GCT_TRANSPARENT)
            return gct_symbol_type(builder, gct_denoted(table, symbol), depth + 1u);
        gct_group_sort(first, sort, sizeof(sort));
    } else {
        snprintf(sort, sizeof(sort), "%s", gct_name(symbol));
    }
    if (!gct_declare(builder, sort))
        return NULL;
    return gct_base_name(builder->arena, sort);
}

static Atom *gct_terminal(Arena *arena, const char *text) {
    Atom *argument = gct_text(arena, text);
    return gct_head(arena, "SyntaxTerminal", &argument, 1u);
}

static Atom *gct_nonterminal(Arena *arena, const char *name) {
    Atom *argument = gct_text(arena, name);
    return gct_head(arena, "SyntaxNonTerminal", &argument, 1u);
}

static Atom *gct_separated(Arena *arena, const char *name, const char *separator) {
    Atom *arguments[3];
    Atom *op;
    arguments[0] = gct_text(arena, name);
    arguments[1] = gct_text(arena, separator);
    arguments[2] = atom_symbol(arena, "OpNone");
    op = gct_head(arena, "SyntaxSep", arguments, 3u);
    return gct_head(arena, "SyntaxOp", &op, 1u);
}

/* The syntax of one element printed at symbol, bound to name: the terminals
 * of a one-alternative group around its child, or the child alone. */
static bool gct_element_syntax(GctLanguageBuilder *builder, SymbolId symbol, const char *name,
                               GctValues *items) {
    const GctProduction *group = gct_single_group(builder->table, symbol);
    uint32_t k;
    if (!group || group->kind != GCT_TRANSPARENT)
        return gct_push(items, gct_nonterminal(builder->arena, name));
    for (k = 0u; k < group->rhs_len; k++) {
        const GctSymbol *child = &group->rhs[k];
        if (!gct_is_value(child)) {
            if (!gct_push(items, gct_terminal(builder->arena, child->text)))
                return false;
        } else if (!gct_element_syntax(builder, child->name, name, items)) {
            return false;
        }
    }
    return true;
}

/* A repetition prints each element with its own terminals. */
static Atom *gct_repeated(GctLanguageBuilder *builder, const char *name, SymbolId helper) {
    const GctProduction *body = gct_helper_body(builder->table, helper);
    Arena *arena = builder->arena;
    GctValues items = {0};
    Atom *arguments[3], *element_name, *variable, *op;
    uint32_t k;
    if (!body)
        return NULL;
    for (k = 0u; k < body->rhs_len; k++) {
        const GctSymbol *symbol = &body->rhs[k];
        if (symbol->name == helper && gct_is_value(symbol))
            continue;
        if (!gct_is_value(symbol)) {
            if (!gct_push(&items, gct_terminal(arena, symbol->text)))
                goto oom;
        } else if (!gct_element_syntax(builder, symbol->name, "element", &items)) {
            goto oom;
        }
    }
    if (items.len == 1u && strcmp(gct_name(items.items[0]->expr.elems[0]->sym_id),
                                  "SyntaxNonTerminal") == 0) {
        free(items.items);
        return gct_separated(arena, name, "");
    }
    element_name = gct_text(arena, "element");
    variable = gct_text(arena, name);
    arguments[0] = gct_head(arena, "SyntaxVar", &variable, 1u);
    arguments[1] = gct_lcons(arena, &element_name, 1u);
    arguments[2] = gct_lcons(arena, items.items, items.len);
    free(items.items);
    op = gct_head(arena, "SyntaxMap", arguments, 3u);
    return gct_head(arena, "SyntaxOp", &op, 1u);
oom:
    free(items.items);
    return NULL;
}

static Atom *gct_param(Arena *arena, const char *name, Atom *type) {
    Atom *arguments[2];
    arguments[0] = gct_text(arena, name);
    arguments[1] = type;
    return gct_head(arena, "TermSimple", arguments, 2u);
}

static Atom *gct_row(Arena *arena, const char *label, const char *category, Atom *params,
                     Atom *syntax) {
    Atom *arguments[5];
    arguments[0] = gct_text(arena, label);
    arguments[1] = gct_text(arena, category);
    arguments[2] = params;
    arguments[3] = syntax;
    arguments[4] = atom_symbol(arena, "EvalNone");
    return gct_head(arena, "GrammarRule", arguments, 5u);
}

/* Parameter names: the child symbol (or its item with "-list" for lists),
 * numbered when a row uses one name twice. */
static void gct_param_name(const GctValues *params, const char *base, char *out,
                           size_t out_size) {
    uint32_t count = 0u, i;
    size_t base_len = strlen(base);
    for (i = 0u; i < params->len; i++) {
        const char *text = params->items[i]->expr.elems[1]->ground.sval;
        if (strncmp(text, base, base_len) == 0 &&
            (text[base_len] == '\0' || text[base_len] == '.'))
            count++;
    }
    if (count == 0u)
        snprintf(out, out_size, "%s", base);
    else
        snprintf(out, out_size, "%s.%u", base, count + 1u);
}

static void gct_symbol_base_name(const CettaGrammarCanonicalTableV1 *table, SymbolId symbol,
                                 char *out, size_t out_size) {
    const GctProduction *first = gct_first_of(table, symbol);
    if (first && gct_is_helper_rule(first)) {
        SymbolId item = gct_helper_item(table, symbol);
        const GctProduction *item_first;
        item = item == SYMBOL_ID_NONE ? SYMBOL_ID_NONE : gct_denoted(table, item);
        item_first = item == SYMBOL_ID_NONE ? NULL : gct_first_of(table, item);
        if (item == SYMBOL_ID_NONE)
            snprintf(out, out_size, "text");
        else if (item_first && item_first->rule_kind == CETTA_GRAMMAR_CANONICAL_GROUP_V1)
            gct_group_sort(item_first, out, out_size);
        else
            snprintf(out, out_size, "%s", gct_name(item));
        strncat(out, "-list", out_size - strlen(out) - 1u);
    } else if (first && first->rule_kind == CETTA_GRAMMAR_CANONICAL_GROUP_V1) {
        if (first->single && first->kind == GCT_TRANSPARENT)
            snprintf(out, out_size, "%s", gct_name(gct_denoted(table, symbol)));
        else
            gct_group_sort(first, out, out_size);
    } else {
        snprintf(out, out_size, "%s", gct_name(symbol));
    }
}

static const char *gct_row_label(const GctProduction *production, char *out, size_t out_size) {
    if (production->kind == GCT_CONSTRUCTOR) {
        snprintf(out, out_size, "%s", gct_name(production->constructor));
    } else if (production->rule_kind == CETTA_GRAMMAR_CANONICAL_GROUP_V1) {
        char sort[512];
        gct_group_sort(production, sort, sizeof(sort));
        if (production->single)
            snprintf(out, out_size, "%s", sort);
        else
            snprintf(out, out_size, "%s/%u", sort, production->alternative);
    } else {
        snprintf(out, out_size, "%s/%u", gct_name(production->lhs), production->alternative);
    }
    return out;
}

static bool gct_production_row(GctLanguageBuilder *builder, const GctProduction *production) {
    const CettaGrammarCanonicalTableV1 *table = builder->table;
    Arena *arena = builder->arena;
    GctValues params = {0}, syntax = {0};
    uint32_t k, position = 0u;
    char label[600], category[512];
    bool ok = false;

    gct_row_label(production, label, sizeof(label));
    if (production->rule_kind == CETTA_GRAMMAR_CANONICAL_GROUP_V1)
        gct_group_sort(production, category, sizeof(category));
    else
        snprintf(category, sizeof(category), "%s", gct_name(production->lhs));
    if (!gct_declare(builder, category))
        goto done;
    for (k = 0u; k < production->rhs_len; k++) {
        const GctSymbol *symbol = &production->rhs[k];
        const GctProduction *first;
        char name[640], base[600];
        Atom *type;
        if (!gct_is_value(symbol)) {
            if (!gct_push(&syntax, gct_terminal(arena, symbol->text)))
                goto done;
            continue;
        }
        if (production->actions && position < production->value_len &&
            production->actions[position] == GCT_EXTEND && params.len > 0u) {
            /* x (sep x)*: the previous parameter becomes the list, whose own
             * row prints its elements with their separator. */
            Atom *previous = params.items[params.len - 1u];
            type = gct_symbol_type(builder, symbol->name, 0u);
            if (!type)
                goto done;
            snprintf(base, sizeof(base), "%s-list", previous->expr.elems[1]->ground.sval);
            params.items[params.len - 1u] = gct_param(arena, base, type);
            syntax.items[syntax.len - 1u] = gct_nonterminal(arena, base);
            position++;
            continue;
        }
        type = gct_symbol_type(builder, symbol->name, 0u);
        if (!type)
            goto done;
        gct_symbol_base_name(table, symbol->name, base, sizeof(base));
        gct_param_name(&params, base, name, sizeof(name));
        if (!gct_push(&params, gct_param(arena, name, type)))
            goto done;
        first = gct_first_of(table, symbol->name);
        if (symbol->kind == CETTA_GRAMMAR_CANONICAL_TOKEN_SYMBOL_V1 || !first ||
            first->kind == GCT_LIST) {
            if (!gct_push(&syntax, gct_nonterminal(arena, name)))
                goto done;
        } else if (!gct_element_syntax(builder, symbol->name, name, &syntax)) {
            goto done;
        }
        position++;
    }
    ok = gct_push(&builder->rows,
                  gct_row(arena, label, category, gct_lcons(arena, params.items, params.len),
                          gct_lcons(arena, syntax.items, syntax.len)));
done:
    free(params.items);
    free(syntax.items);
    return ok;
}

/* The row of a list: its node over the vector of its elements, printed with
 * the separator or the per-element terminals of its rule. */
static bool gct_list_row(GctLanguageBuilder *builder, const GctProduction *production) {
    const CettaGrammarCanonicalTableV1 *table = builder->table;
    Arena *arena = builder->arena;
    const char *key = gct_name(production->key);
    Atom *element = NULL, *param, *syntax = NULL;
    if (!gct_declare(builder, key))
        return false;
    if (gct_is_helper_rule(production)) {
        SymbolId item = gct_helper_item(table, production->lhs);
        const GctProduction *body = gct_helper_body(table, production->lhs);
        if (item != SYMBOL_ID_NONE)
            element = gct_symbol_type(builder, item, 0u);
        else if (body && body->helper_text != 0)
            element = gct_base_name(arena, "String");
        if (production->extended) {
            char separator[256];
            bool is_separator = false;
            if (!gct_element_text(table, production->lhs, separator, sizeof(separator),
                                  &is_separator))
                return false;
            syntax = gct_separated(arena, "elements", separator);
        } else {
            syntax = gct_repeated(builder, "elements", production->lhs);
        }
    } else {
        element = gct_symbol_type(builder, production->list_item, 0u);
        syntax = gct_separated(arena, "elements", production->list_separator);
    }
    if (!element || !syntax)
        return false;
    param = gct_param(arena, "elements", gct_vector(arena, element));
    return gct_push(&builder->rows, gct_row(arena, key, key, gct_lcons(arena, &param, 1u),
                                            gct_lcons(arena, &syntax, 1u)));
}

static int gct_compare_order(const void *left, const void *right) {
    const GctProduction *const *a = left;
    const GctProduction *const *b = right;
    return (*a)->order < (*b)->order ? -1 : (*a)->order > (*b)->order ? 1 : 0;
}

bool cetta_grammar_canonical_language_v1(const CettaGrammarCanonicalTableV1 *table,
                                         const char *name, Arena *arena, Atom **out,
                                         char *error, size_t error_size) {
    GctLanguageBuilder builder;
    const GctProduction **ordered = NULL;
    uint32_t p, k, q, ordered_len = 0u;
    bool ok = false;

    if (out)
        *out = NULL;
    if (!table || !name || !arena || !out)
        return gct_error(error, error_size, "canonical language: bad arguments");
    memset(&builder, 0, sizeof(builder));
    builder.arena = arena;
    builder.table = table;
    ordered = malloc((table->len ? table->len : 1u) * sizeof(*ordered));
    if (!ordered)
        return gct_error(error, error_size, "canonical language: out of memory");
    for (p = 0u; p < table->len; p++)
        ordered[ordered_len++] = &table->productions[p];
    qsort(ordered, ordered_len, sizeof(*ordered), gct_compare_order);
    {
        Atom *arguments[2];
        arguments[0] = atom_string(arena, "String");
        arguments[1] = atom_symbol(arena, "CarrierBuiltinString");
        if (!gct_push(&builder.types, gct_head(arena, "TypeDecl", arguments, 2u)))
            goto oom;
        builder.declared = malloc(64u * sizeof(*builder.declared));
        if (!builder.declared)
            goto oom;
        builder.declared_cap = 64u;
        builder.declared[builder.declared_len++] = "String";
    }
    for (p = 0u; p < ordered_len; p++) {
        const GctProduction *production = ordered[p];
        if (production->kind == GCT_LIST) {
            /* one row per list, at its first production */
            if (production->alternative == 0u && !gct_list_row(&builder, production)) {
                gct_error(error, error_size, "canonical language: cannot express %s",
                          gct_name(production->key));
                goto done;
            }
            continue;
        }
        if (production->rule_kind == CETTA_GRAMMAR_CANONICAL_GROUP_V1 && production->single &&
            production->kind == GCT_TRANSPARENT)
            continue; /* printed inline where it occurs */
        if (!gct_production_row(&builder, production)) {
            gct_error(error, error_size, "canonical language: cannot express %s",
                      gct_name(production->label));
            goto done;
        }
    }
    /* One row per token, in order of first use. */
    for (p = 0u; p < ordered_len; p++) {
        const GctProduction *production = ordered[p];
        for (k = 0u; k < production->rhs_len; k++) {
            const GctSymbol *symbol = &production->rhs[k];
            bool emitted = false;
            if (symbol->kind != CETTA_GRAMMAR_CANONICAL_TOKEN_SYMBOL_V1)
                continue;
            for (q = 0u; q < builder.rows.len && !emitted; q++)
                emitted = atom_string_equals_cstr(
                    builder.rows.items[q]->expr.elems[1], gct_name(symbol->name));
            if (emitted)
                continue;
            if (!gct_declare(&builder, gct_name(symbol->name)))
                goto oom;
            {
                Atom *text = gct_param(arena, "text", gct_base_name(arena, "String"));
                Atom *syntax = gct_nonterminal(arena, "text");
                if (!gct_push(&builder.rows,
                              gct_row(arena, gct_name(symbol->name), gct_name(symbol->name),
                                      gct_lcons(arena, &text, 1u),
                                      gct_lcons(arena, &syntax, 1u))))
                    goto oom;
            }
        }
    }
    {
        Atom *elements[6];
        elements[0] = atom_symbol(arena, "GSLTLanguageDefWireV1");
        elements[1] = atom_string(arena, name);
        elements[2] = gct_lcons(arena, builder.types.items, builder.types.len);
        elements[3] = gct_lcons(arena, builder.rows.items, builder.rows.len);
        elements[4] = atom_symbol(arena, "LNil");
        elements[5] = atom_symbol(arena, "LNil");
        *out = atom_expr(arena, elements, 6u);
    }
    ok = true;
    goto done;
oom:
    gct_error(error, error_size, "canonical language: out of memory");
done:
    free(ordered);
    free(builder.types.items);
    free(builder.rows.items);
    free(builder.declared);
    return ok;
}

/* ----------------------------------------------------------------- forms */

static bool gct_form_seen(const CettaGrammarCanonicalFormV1 *forms, uint32_t len,
                          CettaGrammarCanonicalFormKindV1 kind, const char *name) {
    for (uint32_t i = 0u; i < len; i++) {
        if (forms[i].kind == kind && strcmp(forms[i].name, name) == 0)
            return true;
    }
    return false;
}

bool cetta_grammar_canonical_forms_v1(const CettaGrammarCanonicalTableV1 *table,
                                      CettaGrammarCanonicalFormV1 **out, uint32_t *out_len,
                                      char *error, size_t error_size) {
    const GctProduction **ordered = NULL;
    CettaGrammarCanonicalFormV1 *forms = NULL;
    uint32_t len = 0u, cap, p, k;

    if (out)
        *out = NULL;
    if (out_len)
        *out_len = 0u;
    if (!table || !out || !out_len)
        return gct_error(error, error_size, "canonical forms: bad arguments");
    cap = 2u * table->len + 1u;
    for (p = 0u; p < table->len; p++)
        cap += table->productions[p].rhs_len;
    ordered = malloc((table->len ? table->len : 1u) * sizeof(*ordered));
    forms = malloc(cap * sizeof(*forms));
    if (!ordered || !forms) {
        free(ordered);
        free(forms);
        return gct_error(error, error_size, "canonical forms: out of memory");
    }
    for (p = 0u; p < table->len; p++)
        ordered[p] = &table->productions[p];
    qsort(ordered, table->len, sizeof(*ordered), gct_compare_order);
    for (p = 0u; p < table->len; p++) {
        const GctProduction *production = ordered[p];
        if (production->kind == GCT_CONSTRUCTOR) {
            uint32_t arity = production->value_len;
            for (k = 0u; k < production->value_len; k++) {
                if (production->actions[k] == GCT_EXTEND)
                    arity--;
            }
            forms[len++] = (CettaGrammarCanonicalFormV1){
                CETTA_GRAMMAR_CANONICAL_CONSTRUCTOR_FORM_V1,
                gct_name(production->constructor), arity,
                gct_literal_spelling(gct_name(production->constructor))};
        } else if (production->kind == GCT_LITERAL &&
                   !gct_form_seen(forms, len, CETTA_GRAMMAR_CANONICAL_LITERAL_FORM_V1,
                                  production->literal)) {
            forms[len++] = (CettaGrammarCanonicalFormV1){
                CETTA_GRAMMAR_CANONICAL_LITERAL_FORM_V1, production->literal, 0u, false};
        } else if (production->kind == GCT_LIST) {
            /* a list node, once per list */
            if (production->alternative == 0u)
                forms[len++] = (CettaGrammarCanonicalFormV1){
                    CETTA_GRAMMAR_CANONICAL_CONSTRUCTOR_FORM_V1, gct_name(production->key), 1u,
                    gct_literal_spelling(gct_name(production->key))};
            /* a childless repetition or option body: its elements are its text */
            if (production->helper_text != 0 &&
                !gct_form_seen(forms, len, CETTA_GRAMMAR_CANONICAL_LITERAL_FORM_V1,
                               production->literal))
                forms[len++] = (CettaGrammarCanonicalFormV1){
                    CETTA_GRAMMAR_CANONICAL_LITERAL_FORM_V1, production->literal, 0u, false};
        }
    }
    for (p = 0u; p < table->len; p++) {
        const GctProduction *production = ordered[p];
        for (k = 0u; k < production->rhs_len; k++) {
            const GctSymbol *symbol = &production->rhs[k];
            if (symbol->kind != CETTA_GRAMMAR_CANONICAL_TOKEN_SYMBOL_V1 ||
                gct_form_seen(forms, len, CETTA_GRAMMAR_CANONICAL_TOKEN_FORM_V1,
                              gct_name(symbol->name)))
                continue;
            forms[len++] = (CettaGrammarCanonicalFormV1){
                CETTA_GRAMMAR_CANONICAL_TOKEN_FORM_V1, gct_name(symbol->name), 1u,
                gct_literal_spelling(gct_name(symbol->name))};
        }
    }
    free(ordered);
    *out = forms;
    *out_len = len;
    return true;
}

/* ------------------------------------------------------------ collisions */

typedef enum {
    GCT_SHAPE_SEQUENCE,
    GCT_SHAPE_LITERAL,
    GCT_SHAPE_TOKEN
} GctShapeKind;

typedef struct {
    GctShapeKind kind;
    SymbolId rule;     /* the rule whose production makes it */
    SymbolId token;    /* token: its class */
    const char *text;  /* literal: its text */
} GctShape;


static bool gct_collision_add(GctCollisions *collisions,
                              CettaGrammarCanonicalCollisionKindV1 kind, SymbolId sort,
                              const char *first, const char *second) {
    uint32_t i;
    for (i = 0u; i < collisions->len; i++) {
        const CettaGrammarCanonicalCollisionV1 *seen = &collisions->items[i];
        if (seen->kind == kind && strcmp(seen->sort, gct_name(sort)) == 0 &&
            strcmp(seen->first, first) == 0 &&
            ((!seen->second && !second) ||
             (seen->second && second && strcmp(seen->second, second) == 0)))
            return true;
    }
    if (collisions->len == collisions->cap) {
        uint32_t cap = collisions->cap ? collisions->cap * 2u : 16u;
        CettaGrammarCanonicalCollisionV1 *grown =
            realloc(collisions->items, (size_t)cap * sizeof(*grown));
        if (!grown)
            return false;
        collisions->items = grown;
        collisions->cap = cap;
    }
    collisions->items[collisions->len++] =
        (CettaGrammarCanonicalCollisionV1){kind, gct_name(sort), first, second};
    return true;
}

/* Whether to lies below from along transparent productions. */
static bool gct_reaches_through_transparent(const CettaGrammarCanonicalTableV1 *table,
                                            SymbolId from, SymbolId to) {
    SymbolId *stack = malloc(((size_t)table->len + 1u) * sizeof(*stack));
    SymbolId *seen = malloc(((size_t)table->len + 1u) * sizeof(*seen));
    uint32_t stack_len = 0u, seen_len = 0u, i;
    bool found = false;
    if (!stack || !seen) {
        free(stack);
        free(seen);
        return false;
    }
    stack[stack_len++] = from;
    seen[seen_len++] = from;
    while (stack_len > 0u && !found) {
        SymbolId symbol = stack[--stack_len];
        const GctIndexEntry *entry = gct_index_find(&table->by_lhs, symbol);
        uint32_t e;
        if (!entry)
            continue;
        for (e = (uint32_t)(entry - table->by_lhs.entries);
             e < table->by_lhs.len && table->by_lhs.entries[e].key == symbol && !found; e++) {
            const GctProduction *production = table->by_lhs.entries[e].production;
            SymbolId child = SYMBOL_ID_NONE;
            bool known = false;
            if (production->kind != GCT_TRANSPARENT)
                continue;
            for (i = 0u; i < production->rhs_len; i++) {
                if (gct_is_value(&production->rhs[i]) &&
                    production->rhs[i].kind == CETTA_GRAMMAR_CANONICAL_RULE_SYMBOL_V1)
                    child = production->rhs[i].name;
            }
            if (child == SYMBOL_ID_NONE)
                continue;
            if (child == to) {
                found = true;
                break;
            }
            for (i = 0u; i < seen_len && !known; i++)
                known = seen[i] == child;
            if (!known) {
                seen[seen_len++] = child;
                stack[stack_len++] = child;
            }
        }
    }
    free(stack);
    free(seen);
    return found;
}

static bool gct_symbol_seen(const SymbolId *symbols, uint32_t len, SymbolId symbol) {
    uint32_t i;
    for (i = 0u; i < len; i++) {
        if (symbols[i] == symbol)
            return true;
    }
    return false;
}

/* The shapes reachable from one sort, and the collisions among them. */
static bool gct_sort_collisions(const CettaGrammarCanonicalTableV1 *table, SymbolId sort,
                                GctCollisions *collisions) {
    SymbolId *stack = NULL, *visited = NULL;
    GctShape *shapes = NULL, *found = NULL;
    uint32_t stack_len = 0u, visited_len = 0u, shape_len = 0u;
    uint32_t capacity = table->len + 1u;
    bool ok = false;

    /* Every symbol is visited once and every production classified once, so
     * the table size bounds each array. */
    stack = malloc((size_t)capacity * sizeof(*stack));
    visited = malloc((size_t)capacity * sizeof(*visited));
    shapes = malloc((size_t)capacity * sizeof(*shapes));
    found = malloc((size_t)capacity * sizeof(*found));
    if (!stack || !visited || !shapes || !found)
        goto done;
    stack[stack_len++] = sort;
    visited[visited_len++] = sort;
    while (stack_len > 0u) {
        SymbolId symbol = stack[--stack_len];
        const GctProduction *first = gct_first_of(table, symbol);
        const GctIndexEntry *entry;
        uint32_t e;
        uint32_t found_len = 0u, f;
        if (!first)
            continue;
        if (gct_is_helper_rule(first) || gct_list_rule(table, symbol)) {
            found[found_len++] = (GctShape){GCT_SHAPE_SEQUENCE, symbol, SYMBOL_ID_NONE, NULL};
        } else {
            entry = gct_index_find(&table->by_lhs, symbol);
            for (e = (uint32_t)(entry - table->by_lhs.entries);
                 e < table->by_lhs.len && table->by_lhs.entries[e].key == symbol; e++) {
                const GctProduction *production = table->by_lhs.entries[e].production;
                uint32_t k;
                const GctSymbol *child = NULL;
                if (production->kind == GCT_LITERAL) {
                    found[found_len++] = (GctShape){GCT_SHAPE_LITERAL, symbol, SYMBOL_ID_NONE,
                                                    production->literal};
                    continue;
                }
                if (production->kind == GCT_LIST) {
                    found[found_len++] = (GctShape){GCT_SHAPE_SEQUENCE, symbol, SYMBOL_ID_NONE,
                                                    NULL};
                    continue;
                }
                if (production->kind != GCT_TRANSPARENT)
                    continue;
                for (k = 0u; k < production->rhs_len && !child; k++) {
                    if (gct_is_value(&production->rhs[k]))
                        child = &production->rhs[k];
                }
                if (!child)
                    continue;
                if (child->kind == CETTA_GRAMMAR_CANONICAL_TOKEN_SYMBOL_V1) {
                    found[found_len++] = (GctShape){GCT_SHAPE_TOKEN, symbol, child->name, NULL};
                    continue;
                }
                if (gct_symbol_seen(visited, visited_len, child->name)) {
                    if (!gct_collision_add(collisions, CETTA_GRAMMAR_CANONICAL_PATH_COLLISION_V1,
                                           sort, gct_name(child->name), NULL))
                        goto done;
                    continue;
                }
                visited[visited_len++] = child->name;
                stack[stack_len++] = child->name;
            }
        }
        for (f = 0u; f < found_len; f++) {
            const GctShape *shape = &found[f];
            uint32_t s;
            for (s = 0u; s < shape_len; s++) {
                const GctShape *other = &shapes[s];
                CettaGrammarCanonicalCollisionKindV1 kind;
                if (other->kind != shape->kind)
                    continue;
                if (shape->kind == GCT_SHAPE_SEQUENCE)
                    kind = CETTA_GRAMMAR_CANONICAL_SEQUENCE_COLLISION_V1;
                else if (shape->kind == GCT_SHAPE_LITERAL && strcmp(other->text, shape->text) == 0)
                    kind = CETTA_GRAMMAR_CANONICAL_LITERAL_COLLISION_V1;
                else if (shape->kind == GCT_SHAPE_TOKEN && other->token == shape->token)
                    kind = CETTA_GRAMMAR_CANONICAL_TOKEN_COLLISION_V1;
                else
                    continue;
                if (!gct_collision_add(collisions, kind, sort, gct_name(other->rule),
                                       gct_name(shape->rule)))
                    goto done;
            }
            if (shape_len < capacity)
                shapes[shape_len++] = *shape;
        }
    }
    ok = true;
done:
    free(stack);
    free(visited);
    free(shapes);
    free(found);
    return ok;
}

bool cetta_grammar_canonical_collisions_v1(const CettaGrammarCanonicalTableV1 *table,
                                           CettaGrammarCanonicalCollisionV1 **out,
                                           uint32_t *out_len, char *error,
                                           size_t error_size) {
    GctCollisions collisions = {0};
    uint32_t e;
    if (!table || !out || !out_len)
        return gct_error(error, error_size, "canonical collisions: missing arguments");
    /* The ambiguities the table resolved by naming unary alternatives. */
    for (e = 0u; e < table->ambiguities.len; e++) {
        const CettaGrammarCanonicalCollisionV1 *item = &table->ambiguities.items[e];
        if (!gct_collision_add(&collisions, item->kind, symbol_intern_cstr(g_symbols, item->sort),
                               item->first, item->second)) {
            free(collisions.items);
            return gct_error(error, error_size, "canonical collisions: out of memory");
        }
    }
    for (e = 0u; e < table->by_lhs.len; e++) {
        if (e > 0u && table->by_lhs.entries[e].key == table->by_lhs.entries[e - 1u].key)
            continue;
        if (!gct_sort_collisions(table, table->by_lhs.entries[e].key, &collisions)) {
            free(collisions.items);
            return gct_error(error, error_size, "canonical collisions: out of memory");
        }
    }
    *out = collisions.items;
    *out_len = collisions.len;
    return true;
}

/* --------------------------------------------------------------- printer */

/*
 * The printer inverts the reader by the same productions: a constructor
 * prints its production's terminals and arguments in order, a token prints
 * its text, a literal prints the terminals of the literal production it
 * names, a list prints its elements with the separator or repetition
 * terminals of the position it occupies, a one-alternative group prints its
 * terminals around its children, and a transparent production prints
 * nothing of its own.
 */

typedef enum {
    GCT_TASK_TERM = 0, /* print term at symbol */
    GCT_TASK_TEXT = 1, /* emit fixed text */
    GCT_TASK_TAIL = 2  /* x (sep x)*: print the elements of a list from index on */
} GctTaskKind;

typedef struct {
    GctTaskKind kind;
    const Atom *term;
    SymbolId symbol;
    bool token;           /* symbol is a token */
    const char *text;
    uint32_t index;
} GctTask;

typedef struct {
    GctTask *items;
    uint32_t len;
    uint32_t cap;
} GctTasks;

typedef struct {
    char *bytes;
    size_t len;
    size_t cap;
    size_t last;          /* start of the last emitted text */
    size_t last_len;
    CettaGrammarCanonicalAdjacentV1 adjacent;
    void *adjacent_context;
} GctOutput;

static bool gct_task(GctTasks *tasks, GctTaskKind kind, const Atom *term, SymbolId symbol,
                     bool token, const char *text, uint32_t index) {
    if (tasks->len == tasks->cap) {
        uint32_t cap = tasks->cap ? tasks->cap * 2u : 256u;
        GctTask *grown = realloc(tasks->items, (size_t)cap * sizeof(*grown));
        if (!grown)
            return false;
        tasks->items = grown;
        tasks->cap = cap;
    }
    tasks->items[tasks->len].kind = kind;
    tasks->items[tasks->len].term = term;
    tasks->items[tasks->len].symbol = symbol;
    tasks->items[tasks->len].token = token;
    tasks->items[tasks->len].text = text;
    tasks->items[tasks->len].index = index;
    tasks->len++;
    return true;
}

static bool gct_symbol_task(GctTasks *tasks, const Atom *term, const GctSymbol *symbol) {
    return gct_task(tasks, GCT_TASK_TERM, term, symbol->name,
                    symbol->kind == CETTA_GRAMMAR_CANONICAL_TOKEN_SYMBOL_V1, NULL, 0u);
}

static bool gct_emit(GctOutput *output, const char *text, size_t len) {
    bool space;
    if (len == 0u)
        return true;
    space = output->len > 0u &&
            output->adjacent(output->adjacent_context, output->bytes + output->last,
                             output->last_len, text, len);
    if (output->len + len + 2u > output->cap) {
        size_t cap = output->cap ? output->cap : 4096u;
        char *grown;
        while (output->len + len + 2u > cap)
            cap *= 2u;
        grown = realloc(output->bytes, cap);
        if (!grown)
            return false;
        output->bytes = grown;
        output->cap = cap;
    }
    if (space)
        output->bytes[output->len++] = ' ';
    memcpy(output->bytes + output->len, text, len);
    output->last = output->len;
    output->last_len = len;
    output->len += len;
    output->bytes[output->len] = '\0';
    return true;
}

static const GctProduction *gct_by_constructor(const CettaGrammarCanonicalTableV1 *table,
                                               SymbolId constructor) {
    const GctIndexEntry *entry = gct_index_find(&table->by_constructor, constructor);
    return entry ? entry->production : NULL;
}

/* The list value inside a list node (bnf NAME [x ...]), or NULL. */
static const Atom *gct_list_payload(const Atom *node) {
    SymbolId name = SYMBOL_ID_NONE;
    Atom *const *children = NULL;
    uint32_t len = 0u;
    if (!gct_node_parts(node, &name, &children, &len) || len != 1u ||
        !atom_is_list(children[0]))
        return NULL;
    return children[0];
}

bool cetta_grammar_canonical_list_elements_v1(const Atom *term, Atom *const **elements,
                                              uint32_t *len) {
    const Atom *list = gct_list_payload(term);
    CettaExprLen count = 0u;
    if (!list || !elements || !len || !atom_sequence_view(list, elements, &count))
        return false;
    *len = (uint32_t)count;
    return true;
}

/* Push a production's right-hand side, binding child positions to args.  An
 * EXTEND position shares the previous argument, the list node of both. */
static bool gct_push_production(GctTasks *tasks, const GctProduction *production,
                                Atom *const *args, uint32_t arg_len) {
    uint32_t *arg_of = malloc(((size_t)production->rhs_len + 1u) * sizeof(*arg_of));
    uint32_t k, position = 0u, argument = 0u;
    bool ok = false;
    if (!arg_of)
        return false;
    for (k = 0u; k < production->rhs_len; k++) {
        if (!gct_is_value(&production->rhs[k])) {
            arg_of[k] = UINT32_MAX;
            continue;
        }
        if (production->actions && position < production->value_len &&
            production->actions[position] == GCT_EXTEND && argument > 0u)
            arg_of[k] = UINT32_MAX - 1u;
        else
            arg_of[k] = argument++;
        position++;
    }
    if (argument != arg_len)
        goto done;
    for (k = production->rhs_len; k > 0u; k--) {
        const GctSymbol *symbol = &production->rhs[k - 1u];
        if (arg_of[k - 1u] == UINT32_MAX) {
            if (!gct_task(tasks, GCT_TASK_TEXT, NULL, SYMBOL_ID_NONE, false, symbol->text, 0u))
                goto done;
        } else if (arg_of[k - 1u] == UINT32_MAX - 1u) {
            uint32_t owner = k - 2u;
            const Atom *list;
            while (arg_of[owner] >= UINT32_MAX - 1u)
                owner--;
            list = gct_list_payload(args[arg_of[owner]]);
            if (!list || !gct_task(tasks, GCT_TASK_TAIL, list, symbol->name, false, NULL, 1u))
                goto done;
        } else if (k < production->rhs_len && arg_of[k] == UINT32_MAX - 1u) {
            const Atom *list = gct_list_payload(args[arg_of[k - 1u]]);
            if (!list || atom_list_len(list) == 0u)
                goto done;
            if (!gct_symbol_task(tasks, atom_list_elems(list)[0], symbol))
                goto done;
        } else if (!gct_symbol_task(tasks, args[arg_of[k - 1u]], symbol)) {
            goto done;
        }
    }
    ok = true;
done:
    free(arg_of);
    return ok;
}

/* Push one element of a repetition or option: its body around the element. */
static bool gct_push_helper_element(GctTasks *tasks, const CettaGrammarCanonicalTableV1 *table,
                                    SymbolId helper, const Atom *element) {
    const GctProduction *body = gct_helper_body(table, helper);
    uint32_t k;
    if (!body)
        return false;
    for (k = body->rhs_len; k > 0u; k--) {
        const GctSymbol *symbol = &body->rhs[k - 1u];
        if (symbol->name == helper && gct_is_value(symbol))
            continue;
        if (gct_is_value(symbol)) {
            if (!gct_symbol_task(tasks, element, symbol))
                return false;
        } else if (!gct_task(tasks, GCT_TASK_TEXT, NULL, SYMBOL_ID_NONE, false, symbol->text,
                             0u)) {
            return false;
        }
    }
    return true;
}

static bool gct_push_list(GctTasks *tasks, const CettaGrammarCanonicalTableV1 *table,
                          SymbolId symbol, const Atom *list, uint32_t from) {
    const GctProduction *first = gct_first_of(table, symbol);
    Atom *const *elems = NULL;
    CettaExprLen len = 0u;
    uint32_t i;
    if (!atom_is_list(list) || !atom_sequence_view(list, &elems, &len) || !first)
        return false;
    if (gct_is_helper_rule(first)) {
        for (i = (uint32_t)len; i > from; i--) {
            if (!gct_push_helper_element(tasks, table, symbol, elems[i - 1u]))
                return false;
        }
        return true;
    }
    first = gct_list_rule(table, symbol);
    if (!first || len == 0u)
        return false;
    for (i = (uint32_t)len; i > from; i--) {
        if (!gct_task(tasks, GCT_TASK_TERM, elems[i - 1u], first->list_item,
                      first->list_item_token, NULL, 0u))
            return false;
        if (i - 1u > from && !gct_task(tasks, GCT_TASK_TEXT, NULL, SYMBOL_ID_NONE, false,
                                       first->list_separator, 0u))
            return false;
    }
    return true;
}

typedef enum {
    GCT_REACH_NONE = 0,
    GCT_REACH_DIRECT = 1,   /* print the term by its own head */
    GCT_REACH_LITERAL = 2,  /* print the terminals of a literal production */
    GCT_REACH_CONSTANT = 3, /* a constructor of an empty production: prints nothing */
    GCT_REACH_AT = 4        /* print the term at a list symbol */
} GctReachKind;

typedef struct {
    GctReachKind kind;
    const GctProduction *production;
    SymbolId symbol;
    bool cycle;             /* no match, and a transparent cycle was met */
    bool failed;            /* out of memory */
} GctReach;

/* What a production of symbol makes of term, without descending. */
static GctReach gct_reach_at(const CettaGrammarCanonicalTableV1 *table, SymbolId symbol,
                             const GctProduction *production, const Atom *term, bool node,
                             SymbolId name, uint32_t child_len) {
    GctReach none = {GCT_REACH_NONE, NULL, SYMBOL_ID_NONE, false, false};
    (void)table;
    if (production->kind == GCT_LIST) {
        if (node && name == production->key && child_len == 1u) {
            GctReach at = {GCT_REACH_AT, NULL, symbol, false, false};
            return at;
        }
        return none;
    }
    if (production->kind == GCT_LITERAL && term->kind == ATOM_GROUNDED &&
        term->ground.gkind == GV_STRING && atom_string_equals_cstr(term, production->literal)) {
        GctReach literal = {GCT_REACH_LITERAL, production, symbol, false, false};
        return literal;
    }
    if (production->kind == GCT_CONSTRUCTOR && node && name == production->constructor) {
        GctReach direct = {production->rhs_len == 0u ? GCT_REACH_CONSTANT : GCT_REACH_DIRECT,
                           production, symbol, false, false};
        return direct;
    }
    return none;
}

/* Through transparent productions from symbol, find what accepts term: a
 * depth-first search in production order over a worklist, each symbol
 * visited once. */
static GctReach gct_reach(const CettaGrammarCanonicalTableV1 *table, SymbolId symbol,
                          const Atom *term) {
    typedef struct {
        SymbolId symbol;
        uint32_t next;      /* index into by_lhs of the next production to try */
    } GctReachFrame;
    GctReach none = {GCT_REACH_NONE, NULL, SYMBOL_ID_NONE, false, false};
    GctReach found = none;
    bool cycle = false;
    GctReachFrame *stack = NULL;
    SymbolId *visited = NULL;
    uint32_t stack_len = 0u, visited_len = 0u, cap = table->len + 1u;
    SymbolId name = SYMBOL_ID_NONE;
    Atom *const *children = NULL;
    uint32_t child_len = 0u;
    bool node = gct_node_parts(term, &name, &children, &child_len);
    const GctIndexEntry *entry = gct_index_find(&table->by_lhs, symbol);
    if (!entry)
        return none;
    stack = malloc((size_t)cap * sizeof(*stack));
    visited = malloc((size_t)cap * sizeof(*visited));
    if (!stack || !visited) {
        found.failed = true;
        goto done;
    }
    stack[stack_len++] = (GctReachFrame){symbol, (uint32_t)(entry - table->by_lhs.entries)};
    visited[visited_len++] = symbol;
    while (stack_len > 0u) {
        GctReachFrame *top = &stack[stack_len - 1u];
        const GctProduction *production;
        const GctSymbol *child = NULL;
        uint32_t k, v;
        bool seen = false;
        if (top->next >= table->by_lhs.len || table->by_lhs.entries[top->next].key != top->symbol) {
            stack_len--;
            continue;
        }
        production = table->by_lhs.entries[top->next++].production;
        found = gct_reach_at(table, top->symbol, production, term, node, name, child_len);
        if (found.kind != GCT_REACH_NONE)
            goto done;
        if (production->kind != GCT_TRANSPARENT)
            continue;
        for (k = 0u; k < production->rhs_len && !child; k++) {
            if (gct_is_value(&production->rhs[k]))
                child = &production->rhs[k];
        }
        if (!child)
            continue;
        if (child->kind == CETTA_GRAMMAR_CANONICAL_TOKEN_SYMBOL_V1) {
            if (node && child_len == 1u && name == child->name) {
                found = (GctReach){GCT_REACH_DIRECT, NULL, child->name, false, false};
                goto done;
            }
            continue;
        }
        for (v = 0u; v < visited_len && !seen; v++)
            seen = visited[v] == child->name;
        if (seen) {
            for (v = 0u; v < stack_len; v++) {
                if (stack[v].symbol == child->name)
                    cycle = true;
            }
            continue;
        }
        entry = gct_index_find(&table->by_lhs, child->name);
        if (!entry)
            continue;
        visited[visited_len++] = child->name;
        stack[stack_len++] =
            (GctReachFrame){child->name, (uint32_t)(entry - table->by_lhs.entries)};
    }
    found = none;
    found.cycle = cycle;
done:
    free(stack);
    free(visited);
    return found;
}

/* The single child symbol of a wrapper production, or SYMBOL_ID_NONE. */
static SymbolId gct_wrapper_child(const GctProduction *production) {
    SymbolId child = SYMBOL_ID_NONE;
    if (!production || production->kind != GCT_CONSTRUCTOR || production->value_len != 1u)
        return SYMBOL_ID_NONE;
    for (uint32_t k = 0u; k < production->rhs_len; k++) {
        if (gct_is_value(&production->rhs[k]))
            child = production->rhs[k].name;
    }
    return child;
}

/* The outermost wrapper of the shortest chain of at most depth + 1 wrappers
 * that makes term printable at symbol. */
static const GctProduction *gct_wrapper(const CettaGrammarCanonicalTableV1 *table,
                                        SymbolId symbol, const Atom *term,
                                        const SymbolId *wrappers, uint32_t wrapper_len,
                                        uint32_t depth, Arena *scratch) {
    for (uint32_t w = 0u; w < wrapper_len; w++) {
        const GctProduction *production = gct_by_constructor(table, wrappers[w]);
        SymbolId child = gct_wrapper_child(production);
        Atom *probe;
        GctReach at;
        if (child == SYMBOL_ID_NONE)
            continue;
        probe = gct_node(scratch, wrappers[w], (Atom *const *)&term, 1u);
        if (!probe)
            return NULL;
        at = gct_reach(table, symbol, probe);
        if (at.kind != GCT_REACH_DIRECT || at.production != production)
            continue;
        if (depth == 0u ? gct_reach(table, child, term).kind != GCT_REACH_NONE
                        : gct_wrapper(table, child, term, wrappers, wrapper_len, depth - 1u,
                                      scratch) != NULL)
            return production;
    }
    return NULL;
}

bool cetta_grammar_canonical_print_v1(const CettaGrammarCanonicalTableV1 *table,
                                      const Atom *term, SymbolId symbol,
                                      CettaGrammarCanonicalAdjacentV1 adjacent,
                                      void *adjacent_context, char **out, size_t *out_len,
                                      char *error, size_t error_size) {
    return cetta_grammar_canonical_print_wrapped_v1(table, term, symbol, NULL, 0u, adjacent,
                                                    adjacent_context, out, out_len, error,
                                                    error_size);
}

bool cetta_grammar_canonical_print_wrapped_v1(const CettaGrammarCanonicalTableV1 *table,
                                              const Atom *term, SymbolId symbol,
                                              const SymbolId *wrappers, uint32_t wrapper_len,
                                              CettaGrammarCanonicalAdjacentV1 adjacent,
                                              void *adjacent_context, char **out,
                                              size_t *out_len, char *error,
                                              size_t error_size) {
    GctTasks tasks = {0};
    GctOutput output;
    Arena scratch;
    bool ok = false;

    if (out)
        *out = NULL;
    if (out_len)
        *out_len = 0u;
    if (!table || !term || !adjacent || !out)
        return gct_error(error, error_size, "canonical print: bad arguments");
    arena_init(&scratch);
    memset(&output, 0, sizeof(output));
    output.adjacent = adjacent;
    output.adjacent_context = adjacent_context;
    if (!gct_task(&tasks, GCT_TASK_TERM, term, symbol, false, NULL, 0u))
        goto oom;
    while (tasks.len > 0u) {
        GctTask task = tasks.items[--tasks.len];
        const Atom *t = task.term;
        const GctProduction *first;
        if (task.kind == GCT_TASK_TEXT) {
            if (!gct_emit(&output, task.text, strlen(task.text)))
                goto oom;
            continue;
        }
        if (task.kind == GCT_TASK_TAIL) {
            if (!gct_push_list(&tasks, table, task.symbol, t, task.index)) {
                gct_error(error, error_size, "canonical print: malformed list");
                goto done;
            }
            continue;
        }
        if (task.token) {
            SymbolId name = SYMBOL_ID_NONE;
            Atom *const *children = NULL;
            uint32_t child_len = 0u;
            if (!gct_node_parts(t, &name, &children, &child_len) || name != task.symbol ||
                child_len != 1u || children[0]->kind != ATOM_GROUNDED ||
                children[0]->ground.gkind != GV_STRING) {
                gct_error(error, error_size, "canonical print: expected a %s token",
                          gct_name(task.symbol));
                goto done;
            }
            if (!gct_emit(&output, children[0]->ground.sval, children[0]->ground.slen))
                goto oom;
            continue;
        }
        first = gct_first_of(table, task.symbol);
        if (!first) {
            gct_error(error, error_size, "canonical print: unknown symbol %s",
                      gct_name(task.symbol));
            goto done;
        }
        if (gct_is_helper_rule(first) || gct_list_rule(table, task.symbol)) {
            /* A list symbol prints the list inside its own node. */
            SymbolId name = SYMBOL_ID_NONE;
            Atom *const *children = NULL;
            uint32_t child_len = 0u;
            const Atom *list = gct_node_parts(t, &name, &children, &child_len) &&
                                       name == first->key
                                   ? gct_list_payload(t)
                                   : NULL;
            if (!list || !gct_push_list(&tasks, table, task.symbol, list, 0u)) {
                gct_error(error, error_size, "canonical print: expected the list %s",
                          gct_name(first->key));
                goto done;
            }
            continue;
        }
        if (wrapper_len == 0u) {
            /* Transparent productions print nothing: the name selects the row. */
            SymbolId name = SYMBOL_ID_NONE;
            Atom *const *children = NULL;
            uint32_t child_len = 0u;
            const GctProduction *production =
                gct_node_parts(t, &name, &children, &child_len)
                    ? gct_by_constructor(table, name)
                    : NULL;
            if (production && production->kind == GCT_LIST) {
                if (!gct_task(&tasks, GCT_TASK_TERM, t, production->lhs, false, NULL, 0u))
                    goto oom;
                continue;
            }
            if (production) {
                if (!gct_push_production(&tasks, production, children, child_len)) {
                    gct_error(error, error_size, "canonical print: %s has the wrong arguments",
                              gct_name(name));
                    goto done;
                }
                continue;
            }
        }
        {
            GctReach reach = gct_reach(table, task.symbol, t);
            if (reach.failed)
                goto oom;
            switch (reach.kind) {
            case GCT_REACH_DIRECT:
                if (reach.production) {
                    SymbolId name = SYMBOL_ID_NONE;
                    Atom *const *children = NULL;
                    uint32_t child_len = 0u;
                    if (!gct_node_parts(t, &name, &children, &child_len) ||
                        !gct_push_production(&tasks, reach.production, children, child_len)) {
                        gct_error(error, error_size, "canonical print: %s has the wrong arguments",
                                  gct_name(reach.production->constructor));
                        goto done;
                    }
                } else if (!gct_task(&tasks, GCT_TASK_TERM, t, reach.symbol, true, NULL, 0u)) {
                    goto oom;
                }
                continue;
            case GCT_REACH_LITERAL:
                if (!gct_push_production(&tasks, reach.production, NULL, 0u))
                    goto oom;
                continue;
            case GCT_REACH_CONSTANT:
                continue;
            case GCT_REACH_AT:
                if (!gct_task(&tasks, GCT_TASK_TERM, t, reach.symbol, false, NULL, 0u))
                    goto oom;
                continue;
            case GCT_REACH_NONE:
                break;
            }
            {
                /* A term no production reaches here prints inside the shortest
                 * chain of wrappers that reaches it. */
                const GctProduction *wrapper = NULL;
                for (uint32_t depth = 0u; wrapper_len > 0u && !wrapper && depth < 4u; depth++)
                    wrapper = gct_wrapper(table, task.symbol, t, wrappers, wrapper_len, depth,
                                          &scratch);
                if (wrapper) {
                    Atom *wrapped = gct_node(&scratch, wrapper->constructor, (Atom *const *)&t, 1u);
                    if (!wrapped || !gct_task(&tasks, GCT_TASK_TERM, wrapped, task.symbol, false,
                                              NULL, 0u))
                        goto oom;
                    continue;
                }
            }
            if (reach.cycle)
                gct_error(error, error_size,
                          "canonical print: no production of %s accepts the term; its "
                          "transparent productions form a cycle",
                          gct_name(task.symbol));
            else
                gct_error(error, error_size,
                          "canonical print: no production of %s accepts the term",
                          gct_name(task.symbol));
            goto done;
        }
    }
    *out = output.bytes ? output.bytes : calloc(1u, 1u);
    if (!*out)
        goto oom;
    output.bytes = NULL;
    if (out_len)
        *out_len = output.len;
    ok = true;
    goto done;
oom:
    gct_error(error, error_size, "canonical print: out of memory");
done:
    free(output.bytes);
    free(tasks.items);
    arena_free(&scratch);
    return ok;
}

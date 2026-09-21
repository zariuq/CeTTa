/* Native CST-to-compact-record projection for the official TPTP grammar.
 * Walks ParserPack CstRuleV1 trees. Does not allocate an EBNF derivation forest.
 * Authored GSLT in official_syntax_records_v1.metta remains the oracle. */

#include "native/tptp_official_records_v1.h"
#include "native/utf8_scalar_v1.h"
#include "parser.h"
#include "src/native_sha256.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const Atom *node;
    Atom *value;
} ProjectionMemoEntry;

typedef struct {
    ProjectionMemoEntry *entries;
    size_t capacity;
    size_t len;
} ProjectionMemo;

typedef struct {
    Arena *arena;
    const char *source;
    char *error;
    size_t error_size;
    int64_t map_scalar;
    int64_t map_byte;
    int ascii;
    ProjectionMemo projection_memo;
} Rec;

static int64_t src_byte(Rec *rec, int64_t scalar) {
    int64_t byte;
    int64_t n;
    if (!rec || !rec->source || scalar < 0)
        return 0;
    if (rec->ascii)
        return scalar;
    if (scalar >= rec->map_scalar) {
        byte = rec->map_byte;
        n = rec->map_scalar;
    } else {
        byte = 0;
        n = 0;
    }
    while (rec->source[byte] && n < scalar) {
        unsigned char c = (unsigned char)rec->source[byte];
        if (c < 0x80u)
            byte += 1;
        else if ((c & 0xe0u) == 0xc0u)
            byte += 2;
        else if ((c & 0xf0u) == 0xe0u)
            byte += 3;
        else if ((c & 0xf8u) == 0xf0u)
            byte += 4;
        else
            byte += 1;
        n++;
    }
    rec->map_scalar = n;
    rec->map_byte = byte;
    return byte;
}

static char src_char(Rec *rec, int64_t scalar) {
    int64_t byte = src_byte(rec, scalar);
    unsigned char c;
    if (!rec || !rec->source)
        return '\0';
    c = (unsigned char)rec->source[byte];
    if (c == 0u || c >= 0x80u)
        return '\0';
    return (char)c;
}

static bool rec_error(Rec *rec, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    if (rec && rec->error && rec->error_size > 0u)
        (void)vsnprintf(rec->error, rec->error_size, format, arguments);
    va_end(arguments);
    return false;
}

static size_t projection_memo_hash(const Atom *node) {
    uintptr_t x = (uintptr_t)node;
    x >>= 4u;
    x ^= x >> 17u;
    x *= (uintptr_t)UINT64_C(0x9e3779b97f4a7c15);
    x ^= x >> 29u;
    return (size_t)x;
}

static bool projection_memo_resize(ProjectionMemo *memo, size_t capacity) {
    ProjectionMemoEntry *entries;
    size_t i;

    entries = calloc(capacity, sizeof(*entries));
    if (!entries)
        return false;
    for (i = 0u; i < memo->capacity; i++) {
        ProjectionMemoEntry entry = memo->entries[i];
        size_t slot;
        if (!entry.node)
            continue;
        slot = projection_memo_hash(entry.node) & (capacity - 1u);
        while (entries[slot].node)
            slot = (slot + 1u) & (capacity - 1u);
        entries[slot] = entry;
    }
    free(memo->entries);
    memo->entries = entries;
    memo->capacity = capacity;
    return true;
}

static bool projection_memo_get(const ProjectionMemo *memo, const Atom *node,
                                Atom **value) {
    size_t slot;
    size_t scanned = 0u;
    if (!memo || !memo->entries || memo->capacity == 0u || !node)
        return false;
    slot = projection_memo_hash(node) & (memo->capacity - 1u);
    while (scanned < memo->capacity) {
        const ProjectionMemoEntry *entry = &memo->entries[slot];
        if (!entry->node)
            return false;
        if (entry->node == node) {
            if (value)
                *value = entry->value;
            return true;
        }
        slot = (slot + 1u) & (memo->capacity - 1u);
        scanned++;
    }
    return false;
}

static bool projection_memo_put(ProjectionMemo *memo, const Atom *node,
                                Atom *value) {
    size_t slot;
    if (!memo || !node)
        return false;
    if (memo->capacity == 0u) {
        if (!projection_memo_resize(memo, 128u))
            return false;
    } else if (memo->len >= memo->capacity - memo->capacity / 3u) {
        if (memo->capacity > SIZE_MAX / 2u ||
            !projection_memo_resize(memo, memo->capacity * 2u))
            return false;
    }
    slot = projection_memo_hash(node) & (memo->capacity - 1u);
    while (memo->entries[slot].node) {
        if (memo->entries[slot].node == node) {
            memo->entries[slot].value = value;
            return true;
        }
        slot = (slot + 1u) & (memo->capacity - 1u);
    }
    memo->entries[slot].node = node;
    memo->entries[slot].value = value;
    memo->len++;
    return true;
}

static void projection_memo_free(ProjectionMemo *memo) {
    if (!memo)
        return;
    free(memo->entries);
    memset(memo, 0, sizeof(*memo));
}

static bool is_symbol(const Atom *atom, const char *name) {
    return atom && atom_is_symbol((Atom *)atom, name);
}

static Atom *make_expr(Rec *rec, const char *head, Atom **arguments,
                       uint32_t arity) {
    Atom **elements;
    Atom *result;
    uint32_t index;
    if (!rec || !rec->arena || !head)
        return NULL;
    elements = malloc(((size_t)arity + 1u) * sizeof(*elements));
    if (!elements) {
        rec_error(rec, "tptp records: out of memory");
        return NULL;
    }
    elements[0] = atom_symbol(rec->arena, head);
    if (!elements[0]) {
        free(elements);
        rec_error(rec, "tptp records: symbol allocation failed");
        return NULL;
    }
    for (index = 0u; index < arity; index++) {
        if (!arguments[index]) {
            free(elements);
            rec_error(rec, "tptp records: missing constructor argument for %s",
                      head);
            return NULL;
        }
        elements[index + 1u] = arguments[index];
    }
    result = atom_expr_shared(rec->arena, elements, (CettaExprLen)arity + 1u);
    free(elements);
    if (!result)
        rec_error(rec, "tptp records: constructor allocation failed for %s",
                  head);
    return result;
}

static Atom *app0(Rec *rec, const char *head) {
    return make_expr(rec, head, NULL, 0u);
}

static Atom *app1(Rec *rec, const char *head, Atom *a) {
    Atom *arguments[1] = {a};
    return make_expr(rec, head, arguments, 1u);
}

static Atom *app2(Rec *rec, const char *head, Atom *a, Atom *b) {
    Atom *arguments[2] = {a, b};
    return make_expr(rec, head, arguments, 2u);
}

static Atom *app3(Rec *rec, const char *head, Atom *a, Atom *b, Atom *c) {
    Atom *arguments[3] = {a, b, c};
    return make_expr(rec, head, arguments, 3u);
}

static Atom *app4(Rec *rec, const char *head, Atom *a, Atom *b, Atom *c,
                  Atom *d) {
    Atom *arguments[4] = {a, b, c, d};
    return make_expr(rec, head, arguments, 4u);
}

static Atom *app5(Rec *rec, const char *head, Atom *a, Atom *b, Atom *c,
                  Atom *d, Atom *e) {
    Atom *arguments[5] = {a, b, c, d, e};
    return make_expr(rec, head, arguments, 5u);
}

static bool cst_view(const Atom *term, const char **label, int64_t *start,
                     int64_t *stop, Atom ***children, uint32_t *child_len) {
    Atom *head;
    Atom *name;
    Atom *start_term;
    Atom *stop_term;
    if (!term || term->kind != ATOM_EXPR || term->expr.len < 4u)
        return false;
    head = term->expr.elems[0];
    name = term->expr.elems[1];
    start_term = term->expr.elems[2];
    stop_term = term->expr.elems[3];
    if (!is_symbol(head, "CstRuleV1") || !name ||
        name->kind != ATOM_GROUNDED || name->ground.gkind != GV_STRING ||
        !name->ground.sval || !start_term || !stop_term ||
        start_term->kind != ATOM_GROUNDED ||
        start_term->ground.gkind != GV_INT ||
        stop_term->kind != ATOM_GROUNDED ||
        stop_term->ground.gkind != GV_INT)
        return false;
    if (label) *label = name->ground.sval;
    if (start) *start = start_term->ground.ival;
    if (stop) *stop = stop_term->ground.ival;
    if (children) *children = term->expr.elems + 4u;
    if (child_len) *child_len = (uint32_t)term->expr.len - 4u;
    return true;
}

static void split_label(const char *label, char *name, size_t name_size,
                        uint32_t *alt) {
    size_t length;
    size_t hash;
    *alt = 0u;
    if (!label) {
        snprintf(name, name_size, "%s", "");
        return;
    }
    length = strlen(label);
    hash = length;
    while (hash > 0u && label[hash - 1u] == 'x')
        hash--;
    if (hash > 0u && label[hash - 1u] == '#') {
        size_t base = hash - 1u;
        *alt = (uint32_t)(length - hash);
        if (base >= name_size)
            base = name_size - 1u;
        memcpy(name, label, base);
        name[base] = '\0';
        return;
    }
    snprintf(name, name_size, "%s", label);
}

static bool name_eq(const char *left, const char *right) {
    return left && right && strcmp(left, right) == 0;
}

static bool name_in(const char *name, const char *const *names) {
    uint32_t index;
    if (!name || !names)
        return false;
    for (index = 0u; names[index]; index++)
        if (name_eq(name, names[index]))
            return true;
    return false;
}

static bool is_layout_name(const char *name) {
    return name_eq(name, "#layout") || name_eq(name, "layout");
}

static bool is_token_name(const char *name) {
    return name && strncmp(name, "#token:", 7) == 0;
}

static bool is_unwrap_name(const char *name) {
    static const char *const names[] = {
        "TPTP_input", "annotated_formula", "fof_formula", "fof_logic_formula",
        "fof_binary_formula", "fof_binary_assoc", "fof_unit_formula",
        "fof_unitary_formula", "fof_atomic_formula",
        "fof_defined_atomic_formula", "fof_term", "fof_function_term",
        "fof_defined_term", "fof_defined_atomic_term", "constant", "functor",
        "defined_constant", "defined_functor", "system_constant",
        "system_functor", "atomic_defined_word", "atomic_system_word",
        "cnf_formula", "tpi_formula", "tcf_formula", "tcf_logic_formula",
        "tff_formula", "tff_logic_formula", "tff_top_level_type",
        "tff_non_atomic_type", "tff_unitary_type", "tff_atomic_type",
        "tff_unitary_formula", "tff_unit_formula", "tff_preunit_formula",
        "tff_atomic_formula", "tff_defined_atomic",
        "untyped_atom", "type_constant", "type_functor", "thf_formula",
        "thf_logic_formula", "thf_binary_formula", "thf_binary_assoc",
        "thf_unit_formula", "thf_unitary_formula", "thf_atomic_formula",
        "thf_quantifier", "tff_quantifier", "fof_quantifier",
        "thf_top_level_type", "file_name", "include_optionals", "dag_source",
        "useful_info", "general_data", "inference_rule",
        "external_source", "intro_type", "creator_name",
        "theory_name",
        "general_term", "thf_unitary_type", "thf_unitary_term",
        "tff_binary_formula", "tff_binary_assoc",
        "tff_unitary_term", "tff_unary_formula", "txf_tuple_type",
        "txf_let_types", "txf_let_defns", "txf_let_LHS", "parent_details",
        "ntf_connective_name",
        "def_or_sys_constant", "tff_unary_connective", "tff_monotype",
        "thf_defined_atomic", "comma_parent_info", "comma_general_term",
        "comma_tff_term", "comma_thf_logic_formula", "tff_variable",
        "thf_arguments", "thf_preunit_formula", "thf_unary_formula",
        "thf_apply_type", "type_quantifier", "number", "distinct_object",
        "#entry", NULL};
    return name_in(name, names);
}

static bool is_generated_helper(const char *name) {
    if (!name)
        return false;
    if (name[0] == '#' && !is_token_name(name) && !name_eq(name, "#layout") &&
        !name_eq(name, "#entry"))
        return true;
    return false;
}

static Atom *project_node(Rec *rec, Atom *node);

static Atom *span_atom(Rec *rec, int64_t start, int64_t stop) {
    return app2(rec, "tptp-rec:span", atom_int(rec->arena, start),
                atom_int(rec->arena, stop));
}

static void collect_cps(const Atom *node, uint8_t **bytes, size_t *len,
                        size_t *cap) {
    uint32_t index;
    if (!node)
        return;
    if (node->kind == ATOM_EXPR && node->expr.len >= 4u &&
        is_symbol(node->expr.elems[0], "CstRuleV1") && node->expr.elems[1] &&
        node->expr.elems[1]->kind == ATOM_GROUNDED &&
        node->expr.elems[1]->ground.gkind == GV_STRING &&
        node->expr.elems[1]->ground.sval &&
        (strncmp(node->expr.elems[1]->ground.sval, "#layout", 7) == 0 ||
         strcmp(node->expr.elems[1]->ground.sval, "#layout#") == 0))
        return;
    if (node->kind == ATOM_EXPR && node->expr.len == 2u &&
        is_symbol(node->expr.elems[0], "cp") && node->expr.elems[1] &&
        node->expr.elems[1]->kind == ATOM_GROUNDED &&
        node->expr.elems[1]->ground.gkind == GV_INT) {
        (void)cetta_utf8_append_scalar_v1(
            bytes, len, cap, node->expr.elems[1]->ground.ival);
        return;
    }
    if (node->kind != ATOM_EXPR)
        return;
    for (index = 0u; index < node->expr.len; index++)
        collect_cps(node->expr.elems[index], bytes, len, cap);
}

static bool source_slice(Rec *rec, int64_t start, int64_t stop, char *out,
                         size_t out_size);

static bool is_lexical_text_name(const char *name) {
    return is_token_name(name) || name_eq(name, "lower_word") ||
           name_eq(name, "upper_word") || name_eq(name, "dollar_word") ||
           name_eq(name, "dollar_dollar_word") ||
           name_eq(name, "single_quoted") || name_eq(name, "back_quoted") ||
           name_eq(name, "distinct_object") || name_eq(name, "integer") ||
           name_eq(name, "real") || name_eq(name, "rational") ||
           name_eq(name, "atomic_word") || name_eq(name, "formula_role") ||
           name_eq(name, "variable") || name_eq(name, "defined_type") ||
           name_eq(name, "unary_connective") ||
           name_eq(name, "nonassoc_connective") ||
           name_eq(name, "defined_infix_pred") ||
           name_eq(name, "infix_equality") ||
           name_eq(name, "infix_inequality") ||
           name_eq(name, "tff_unary_connective") ||
           name_eq(name, "thf_unary_connective") ||
           name_eq(name, "type_quantifier") ||
           name_eq(name, "ntf_short_connective") ||
           name_eq(name, "assignment") || name_eq(name, "arrow") ||
           name_eq(name, "star");
}

static Atom *token_text_atom(Rec *rec, Atom *node) {
    uint8_t *bytes = NULL;
    size_t len = 0u;
    size_t cap = 0u;
    Atom *result;
    const char *label = NULL;
    char name[160];
    uint32_t alt = 0u;
    int64_t start = 0;
    int64_t stop = 0;
    if (rec && rec->source &&
        cst_view(node, &label, &start, &stop, NULL, NULL) && stop > start) {
        int64_t span = stop - start;
        split_label(label, name, sizeof(name), &alt);
        if (is_lexical_text_name(name) ||
            (strncmp(name, "#token:", 7u) == 0) ||
            (name[0] == '#' && span <= 4)) {
            int64_t b0;
            int64_t b1;
            size_t n;
            char *buf;
            while (start < stop && (src_char(rec, start) == ' ' ||
                                    src_char(rec, start) == '\t' ||
                                    src_char(rec, start) == '\n' ||
                                    src_char(rec, start) == '\r'))
                start++;
            while (stop > start && (src_char(rec, stop - 1) == ' ' ||
                                    src_char(rec, stop - 1) == '\t' ||
                                    src_char(rec, stop - 1) == '\n' ||
                                    src_char(rec, stop - 1) == '\r'))
                stop--;
            b0 = src_byte(rec, start);
            b1 = src_byte(rec, stop);
            if (b1 > b0) {
                n = (size_t)(b1 - b0);
                buf = malloc(n + 1u);
                if (!buf) {
                    rec_error(rec, "tptp records: token text allocation failed");
                    return NULL;
                }
                memcpy(buf, rec->source + b0, n);
                buf[n] = '\0';
                result = atom_string(rec->arena, buf);
                free(buf);
                return result;
            }
        }
        return atom_string(rec->arena, "");
    }
    collect_cps(node, &bytes, &len, &cap);
    if (!bytes) {
        bytes = malloc(1u);
        if (!bytes) {
            rec_error(rec, "tptp records: token text allocation failed");
            return NULL;
        }
        bytes[0] = 0;
        len = 0u;
    }
    result = atom_string(rec->arena, (const char *)bytes);
    free(bytes);
    return result;
}

static bool first_cp(const Atom *node, int64_t *out) {
    uint32_t index;
    const char *label = NULL;
    char name[160];
    uint32_t alt = 0u;
    Atom **kids = NULL;
    uint32_t n = 0u;
    if (!node || !out)
        return false;
    if (node->kind == ATOM_EXPR && node->expr.len == 2u &&
        is_symbol(node->expr.elems[0], "cp") && node->expr.elems[1] &&
        node->expr.elems[1]->kind == ATOM_GROUNDED &&
        node->expr.elems[1]->ground.gkind == GV_INT) {
        *out = node->expr.elems[1]->ground.ival;
        return true;
    }
    if (cst_view(node, &label, NULL, NULL, &kids, &n)) {
        split_label(label, name, sizeof(name), &alt);
        if (is_layout_name(name))
            return false;
        for (index = 0u; index < n; index++)
            if (first_cp(kids[index], out))
                return true;
        return false;
    }
    if (node->kind != ATOM_EXPR)
        return false;
    for (index = 0u; index < node->expr.len; index++)
        if (first_cp(node->expr.elems[index], out))
            return true;
    return false;
}

static bool not_before_span(Rec *rec, int64_t start);
static Atom *wrap_not(Rec *rec, Atom *item);

static bool source_starts_with_not(Rec *rec, int64_t start) {
    char c;
    if (!rec || !rec->source || start < 0)
        return false;
    c = src_char(rec, start);
    while (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        start++;
        c = src_char(rec, start);
    }
    return c == '~';
}

static bool leading_not(Rec *rec, Atom *node) {
    Atom *text;
    int64_t scalar = 0;
    int64_t start = 0;
    if (cst_view(node, NULL, &start, NULL, NULL, NULL) &&
        not_before_span(rec, start))
        return true;
    if (!(rec && rec->source) && first_cp(node, &scalar) &&
        scalar == (int64_t)'~')
        return true;
    text = token_text_atom(rec, node);
    return text && text->kind == ATOM_GROUNDED &&
           text->ground.gkind == GV_STRING && text->ground.sval &&
           text->ground.sval[0] == '~';
}

static Atom *word_from_kind(Rec *rec, const char *kind, Atom *text) {
    if (name_eq(kind, "lower_word"))
        return app1(rec, "tptp-rec:word-lower", text);
    if (name_eq(kind, "single_quoted"))
        return app1(rec, "tptp-rec:word-quoted", text);
    if (name_eq(kind, "back_quoted"))
        return app1(rec, "tptp-rec:word-back", text);
    if (name_eq(kind, "dollar_word"))
        return app1(rec, "tptp-rec:word-defined", text);
    if (name_eq(kind, "dollar_dollar_word"))
        return app1(rec, "tptp-rec:word-system", text);
    if (name_eq(kind, "upper_word"))
        return app1(rec, "tptp-rec:word-upper", text);
    if (name_eq(kind, "integer"))
        return app1(rec, "tptp-rec:number-integer", text);
    if (name_eq(kind, "rational"))
        return app1(rec, "tptp-rec:number-rational", text);
    if (name_eq(kind, "real"))
        return app1(rec, "tptp-rec:number-real", text);
    if (name_eq(kind, "distinct_object"))
        return app1(rec, "tptp-rec:distinct-object", text);
    return app1(rec, "tptp-rec:word-lower", text);
}

static const char *token_kind_from_wrapper(const char *name) {
    if (!is_token_name(name))
        return NULL;
    return name + 7;
}

typedef struct {
    Atom **items;
    uint32_t len;
    uint32_t cap;
} NodeVec;

static void vec_init(NodeVec *vec) {
    vec->items = NULL;
    vec->len = 0u;
    vec->cap = 0u;
}

static void vec_free(NodeVec *vec) {
    free(vec->items);
    vec_init(vec);
}

static bool vec_push(Rec *rec, NodeVec *vec, Atom *node) {
    if (vec->len >= vec->cap) {
        uint32_t ncap = vec->cap ? vec->cap * 2u : 16u;
        Atom **grown;
        if (ncap < vec->len + 1u)
            ncap = vec->len + 1u;
        grown = realloc(vec->items, (size_t)ncap * sizeof(*grown));
        if (!grown)
            return rec_error(rec, "tptp records: out of memory");
        vec->items = grown;
        vec->cap = ncap;
    }
    vec->items[vec->len++] = node;
    return true;
}

static bool collect_real(Rec *rec, Atom *node, NodeVec *vec) {
    const char *label = NULL;
    char name[160];
    uint32_t alt = 0u;
    Atom **kids = NULL;
    uint32_t n = 0u;
    uint32_t index;
    if (!node || !vec)
        return true;
    if (!cst_view(node, &label, NULL, NULL, &kids, &n))
        return true;
    split_label(label, name, sizeof(name), &alt);
    if (is_layout_name(name))
        return true;
    if (is_generated_helper(name)) {
        uint32_t before = vec->len;
        Atom *text;
        for (index = 0u; index < n; index++)
            if (!collect_real(rec, kids[index], vec))
                return false;
        if (vec->len != before)
            return true;
        text = token_text_atom(rec, node);
        if (text && text->kind == ATOM_GROUNDED &&
            text->ground.gkind == GV_STRING && text->ground.sval &&
            text->ground.sval[0] != '\0' &&
            strlen(text->ground.sval) <= 4u)
            return vec_push(rec, vec, node);
        return true;
    }
    return vec_push(rec, vec, node);
}

static bool collect_reals(Rec *rec, Atom **kids, uint32_t n, NodeVec *vec) {
    uint32_t index;
    for (index = 0u; index < n; index++)
        if (!collect_real(rec, kids[index], vec))
            return false;
    return true;
}

typedef struct {
    Atom **items;
    uint32_t len;
} NodeList;

static Atom *find_named(NodeList list, const char *want) {
    uint32_t index;
    for (index = 0u; index < list.len; index++) {
        const char *label = NULL;
        char name[128];
        uint32_t alt = 0u;
        if (!cst_view(list.items[index], &label, NULL, NULL, NULL, NULL))
            continue;
        split_label(label, name, sizeof(name), &alt);
        if (name_eq(name, want))
            return list.items[index];
    }
    return NULL;
}

static Atom *child_named(Rec *rec, NodeList list, const char *want) {
    Atom *found = find_named(list, want);
    if (found)
        return found;
    rec_error(rec, "tptp records: missing child %s", want);
    return NULL;
}

static Atom *nil_terms(Rec *rec) { return app0(rec, "tptp-rec:terms-nil"); }
static Atom *nil_inputs(Rec *rec) { return app0(rec, "tptp-rec:inputs-nil"); }
static Atom *nil_names(Rec *rec) { return app0(rec, "tptp-rec:names-nil"); }
static Atom *nil_vars(Rec *rec) { return app0(rec, "tptp-rec:vars-nil"); }

static Atom *cons_terms(Rec *rec, Atom *head, Atom *tail) {
    return app2(rec, "tptp-rec:terms-cons", head, tail);
}
static Atom *cons_inputs(Rec *rec, Atom *head, Atom *tail) {
    return app2(rec, "tptp-rec:inputs-cons", head, tail);
}
static Atom *cons_names(Rec *rec, Atom *head, Atom *tail) {
    return app2(rec, "tptp-rec:names-cons", head, tail);
}
static Atom *cons_vars(Rec *rec, Atom *head, Atom *tail) {
    return app2(rec, "tptp-rec:vars-cons", head, tail);
}

static Atom *atom_from_term(Rec *rec, Atom *term) {
    if (term && term->kind == ATOM_EXPR && term->expr.len == 3u &&
        is_symbol(term->expr.elems[0], "tptp-rec:fun"))
        return app2(rec, "tptp-rec:atom", term->expr.elems[1],
                    term->expr.elems[2]);
    if (term && term->kind == ATOM_EXPR && term->expr.len == 2u &&
        is_symbol(term->expr.elems[0], "tptp-rec:var"))
        return app2(rec, "tptp-rec:atom", term->expr.elems[1], nil_terms(rec));
    if (term && term->kind == ATOM_EXPR && term->expr.len == 2u &&
        (is_symbol(term->expr.elems[0], "tptp-rec:word-lower") ||
         is_symbol(term->expr.elems[0], "tptp-rec:word-quoted") ||
         is_symbol(term->expr.elems[0], "tptp-rec:word-defined") ||
         is_symbol(term->expr.elems[0], "tptp-rec:word-system") ||
         is_symbol(term->expr.elems[0], "tptp-rec:word-back")))
        return app2(rec, "tptp-rec:atom", term, nil_terms(rec));
    return app2(rec, "tptp-rec:atom", term, nil_terms(rec));
}

static Atom *binary_of(Rec *rec, const char *op, Atom *left, Atom *right) {
    if (name_eq(op, "|"))
        return app2(rec, "tptp-rec:or", left, right);
    if (name_eq(op, "&"))
        return app2(rec, "tptp-rec:and", left, right);
    if (name_eq(op, "<=>"))
        return app2(rec, "tptp-rec:iff", left, right);
    if (name_eq(op, "=>"))
        return app2(rec, "tptp-rec:implies", left, right);
    if (name_eq(op, "<="))
        return app2(rec, "tptp-rec:reverse-implies", left, right);
    if (name_eq(op, "<~>"))
        return app2(rec, "tptp-rec:xor", left, right);
    if (name_eq(op, "~|"))
        return app2(rec, "tptp-rec:nor", left, right);
    if (name_eq(op, "~&"))
        return app2(rec, "tptp-rec:nand", left, right);
    if (name_eq(op, "@"))
        return app2(rec, "tptp-rec:apply", left, right);
    if (name_eq(op, "=") || name_eq(op, ":=") || name_eq(op, "=="))
        return app2(rec, "tptp-rec:equals", left, right);
    if (name_eq(op, "!="))
        return app2(rec, "tptp-rec:not-equals", left, right);
    if (name_eq(op, ">"))
        return app2(rec, "tptp-rec:map-type", left, right);
    if (name_eq(op, "*"))
        return app2(rec, "tptp-rec:xprod", left, right);
    if (name_eq(op, ":"))
        return app2(rec, "tptp-rec:typing", left, right);
    return app2(rec, "tptp-rec:or", left, right);
}

static bool is_connective_text(const char *text) {
    static const char *const ops[] = {
        "|", "&", "<=>", "=>", "<=", "<~>", "~|", "~&", "@", "==", "=", "!=",
        ":=", ">", "*", ":", NULL};
    return name_in(text, ops);
}

static bool is_operator_prod(const char *name) {
    static const char *const names[] = {
        "infix_equality", "infix_inequality", "defined_infix_pred",
        "nonassoc_connective", "arrow", "star", "assignment", NULL};
    return name_in(name, names);
}

static bool is_unary_not_prod(const char *name) {
    return name_eq(name, "unary_connective") ||
           name_eq(name, "tff_unary_connective");
}

static bool node_is_operator(Atom *node) {
    const char *label = NULL;
    char name[160];
    uint32_t alt = 0u;
    if (!cst_view(node, &label, NULL, NULL, NULL, NULL))
        return false;
    split_label(label, name, sizeof(name), &alt);
    return is_operator_prod(name);
}

static bool node_is_unary_not(Atom *node) {
    const char *label = NULL;
    char name[160];
    uint32_t alt = 0u;
    if (!cst_view(node, &label, NULL, NULL, NULL, NULL))
        return false;
    split_label(label, name, sizeof(name), &alt);
    return is_unary_not_prod(name);
}

static bool source_slice(Rec *rec, int64_t start, int64_t stop, char *out,
                         size_t out_size) {
    int64_t b0;
    int64_t b1;
    size_t n;
    if (!rec || !rec->source || !out || out_size == 0u || start < 0 ||
        stop < start)
        return false;
    while (start < stop && (src_char(rec, start) == ' ' ||
                            src_char(rec, start) == '\t' ||
                            src_char(rec, start) == '\n' ||
                            src_char(rec, start) == '\r'))
        start++;
    while (stop > start && (src_char(rec, stop - 1) == ' ' ||
                            src_char(rec, stop - 1) == '\t' ||
                            src_char(rec, stop - 1) == '\n' ||
                            src_char(rec, stop - 1) == '\r'))
        stop--;
    b0 = src_byte(rec, start);
    b1 = src_byte(rec, stop);
    if (b1 < b0)
        return false;
    n = (size_t)(b1 - b0);
    if (n == 0u || n >= out_size)
        return false;
    memcpy(out, rec->source + b0, n);
    out[n] = '\0';
    return true;
}

static bool extract_connective_range(Rec *rec, int64_t start, int64_t stop,
                                     char *out, size_t out_size) {
    static const char *const ops[] = {
        "<=>", "<~>", "=>", "<=", "~|", "~&", "!=", ":=", "==", "|", "&", "@",
        "=", ">", "*", ":", NULL};
    uint32_t index;
    if (!rec || !rec->source || !out || out_size == 0u || start < 0 ||
        stop < start)
        return false;
    while (start < stop && (src_char(rec, start) == ' ' ||
                            src_char(rec, start) == '\t' ||
                            src_char(rec, start) == '\n' ||
                            src_char(rec, start) == '\r' ||
                            src_char(rec, start) == '(' ||
                            src_char(rec, start) == ')'))
        start++;
    for (index = 0u; ops[index]; index++) {
        size_t n = strlen(ops[index]);
        int64_t b0;
        if ((int64_t)n > stop - start)
            continue;
        b0 = src_byte(rec, start);
        if (memcmp(rec->source + b0, ops[index], n) == 0) {
            if (n >= out_size)
                return false;
            memcpy(out, ops[index], n);
            out[n] = '\0';
            return true;
        }
    }
    return false;
}

static bool source_between(Rec *rec, Atom *left, Atom *right, char *out,
                           size_t out_size) {
    int64_t left_start = 0, left_stop = 0, right_start = 0, right_stop = 0;
    if (!cst_view(left, NULL, &left_start, &left_stop, NULL, NULL) ||
        !cst_view(right, NULL, &right_start, &right_stop, NULL, NULL))
        return false;
    if (left_stop <= right_start &&
        extract_connective_range(rec, left_stop, right_start, out, out_size))
        return true;
    return source_slice(rec, left_stop, right_start, out, out_size) &&
           is_connective_text(out);
}

static bool recover_binary_op(Rec *rec, Atom *left, Atom *right,
                              int64_t parent_start, int64_t parent_stop,
                              char *out, size_t out_size) {
    if (source_between(rec, left, right, out, out_size) &&
        is_connective_text(out))
        return true;
    return extract_connective_range(rec, parent_start, parent_stop, out,
                                    out_size);
}

static bool fill_quant_at(Rec *rec, int64_t i, char *out, size_t out_size) {
    char c;
    char n;
    if (!rec || !out || out_size < 3u || i < 0)
        return false;
    c = src_char(rec, i);
    n = src_char(rec, i + 1);
    if (c == '!' && n == '>') {
        snprintf(out, out_size, "!>");
        return true;
    }
    if (c == '?' && n == '*') {
        snprintf(out, out_size, "?*");
        return true;
    }
    if (c == '@' && (n == '+' || n == '-')) {
        out[0] = '@';
        out[1] = n;
        out[2] = '\0';
        return true;
    }
    if (c == '!' || c == '?' || c == '^') {
        out[0] = c;
        out[1] = '\0';
        return true;
    }
    return false;
}

/* Longest official quantifier at the span: !>, ?*, @+, @-, !, ?, ^. */
static bool source_quant_text(Rec *rec, int64_t start, int64_t stop,
                              char *out, size_t out_size) {
    int64_t i;
    if (!out || out_size == 0u)
        return false;
    out[0] = '\0';
    if (!rec || !rec->source || start < 0)
        return false;
    if (stop < start)
        stop = start + 16;
    i = start;
    while (i < stop) {
        char c = src_char(rec, i);
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '(')
            i++;
        else
            break;
    }
    if (fill_quant_at(rec, i, out, out_size))
        return true;
    i = start;
    while (i > 0) {
        char c = src_char(rec, i - 1);
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '(' ||
            c == '[' || c == ']' || c == ':' || c == ',' || c == '$' ||
            (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '_')
            i--;
        else
            break;
    }
    return fill_quant_at(rec, i > 0 ? i - 1 : 0, out, out_size) ||
           fill_quant_at(rec, start, out, out_size);
}

static bool not_immediately_before(Rec *rec, int64_t start) {
    int64_t i;
    if (!rec || !rec->source || start <= 0)
        return false;
    i = start;
    while (i > 0) {
        char c = src_char(rec, i - 1);
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '(')
            i--;
        else
            return c == '~';
    }
    return false;
}

static bool not_before_span(Rec *rec, int64_t start) {
    if (source_starts_with_not(rec, start))
        return true;
    return not_immediately_before(rec, start);
}

static bool node_text_is(Rec *rec, Atom *node, const char *want) {
    Atom *text = token_text_atom(rec, node);
    return text && text->kind == ATOM_GROUNDED &&
           text->ground.gkind == GV_STRING && text->ground.sval &&
           name_eq(text->ground.sval, want);
}

static bool node_is_connective(Rec *rec, Atom *node) {
    Atom *text;
    if (!node)
        return false;
    text = token_text_atom(rec, node);
    if (!text || text->kind != ATOM_GROUNDED || text->ground.gkind != GV_STRING)
        return false;
    return is_connective_text(text->ground.sval);
}

static Atom *fold_ctor(Rec *rec, const char *ctor, NodeList list) {
    Atom *acc = NULL;
    uint32_t index;
    bool pending_not = false;
    for (index = 0u; index < list.len; index++) {
        Atom *item;
        if (node_text_is(rec, list.items[index], "~") ||
            node_is_unary_not(list.items[index])) {
            pending_not = true;
            continue;
        }
        if (node_is_connective(rec, list.items[index]) ||
            node_is_operator(list.items[index]))
            continue;
        item = project_node(rec, list.items[index]);
        if (!item)
            return NULL;
        if (item->kind == ATOM_GROUNDED && item->ground.gkind == GV_STRING &&
            (!item->ground.sval || item->ground.sval[0] == '\0'))
            continue;
        if (!pending_not) {
            int64_t item_start = 0;
            if (cst_view(list.items[index], NULL, &item_start, NULL, NULL,
                         NULL) &&
                not_immediately_before(rec, item_start))
                pending_not = true;
        }
        if (pending_not) {
            item = wrap_not(rec, item);
            pending_not = false;
        }
        acc = acc ? app2(rec, ctor, acc, item) : item;
    }
    return acc;
}

static bool is_not_record(Atom *atom) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len == 2u &&
           is_symbol(atom->expr.elems[0], "tptp-rec:not");
}

static Atom *wrap_not(Rec *rec, Atom *item) {
    if (!item || is_not_record(item))
        return item;
    return app1(rec, "tptp-rec:not", item);
}

static Atom *project_operand(Rec *rec, Atom *node, bool negated) {
    Atom *item;
    int64_t start = 0;
    if (cst_view(node, NULL, &start, NULL, NULL, NULL) &&
        not_immediately_before(rec, start))
        negated = true;
    item = project_node(rec, node);
    if (!item)
        return NULL;
    if (negated)
        return wrap_not(rec, item);
    return item;
}

static Atom *operands_project(Rec *rec, NodeList list, const char *kind,
                              int64_t start, int64_t stop) {
    const char *ctor = "tptp-rec:or";
    bool binary_kind = name_eq(kind, "binary");
    if (name_eq(kind, "and"))
        ctor = "tptp-rec:and";
    else if (name_eq(kind, "apply"))
        ctor = "tptp-rec:apply";
    else if (name_eq(kind, "map"))
        ctor = "tptp-rec:map-type";
    else if (name_eq(kind, "xprod"))
        ctor = "tptp-rec:xprod";
    else if (name_eq(kind, "eq"))
        ctor = "tptp-rec:equals";
    else if (name_eq(kind, "neq"))
        ctor = "tptp-rec:not-equals";
    else if (name_eq(kind, "typing"))
        ctor = "tptp-rec:typing";
    {
        Atom *ops[32];
        bool nots[32];
        uint32_t nops = 0u;
        uint32_t index;
        char optext[8];
        bool pending_not = false;
        const char *seen_op = NULL;
        for (index = 0u; index < list.len && nops < 32u; index++) {
            Atom *child = list.items[index];
            if (node_text_is(rec, child, "~") || node_is_unary_not(child)) {
                pending_not = true;
                continue;
            }
            if (node_is_connective(rec, child) || node_is_operator(child)) {
                Atom *text = token_text_atom(rec, child);
                if (text && text->kind == ATOM_GROUNDED &&
                    text->ground.gkind == GV_STRING &&
                    is_connective_text(text->ground.sval))
                    seen_op = text->ground.sval;
                continue;
            }
            ops[nops] = child;
            nots[nops] = pending_not;
            pending_not = false;
            nops++;
        }
        if (nops == 2u) {
            Atom *left = project_operand(rec, ops[0], nots[0]);
            Atom *right = project_operand(rec, ops[1], nots[1]);
            if (seen_op)
                return binary_of(rec, seen_op, left, right);
            if (recover_binary_op(rec, ops[0], ops[1], start, stop, optext,
                                  sizeof(optext)))
                return binary_of(rec, optext, left, right);
            if (binary_kind)
                return binary_of(rec, "=>", left, right);
            return app2(rec, ctor, left, right);
        }
        if (list.len >= 3u) {
            Atom *op = token_text_atom(rec, list.items[1]);
            if (op && op->kind == ATOM_GROUNDED &&
                op->ground.gkind == GV_STRING &&
                is_connective_text(op->ground.sval)) {
                Atom *left = project_operand(rec, list.items[0], false);
                Atom *right = fold_ctor(rec, ctor,
                                        (NodeList){list.items + 2,
                                                   list.len - 2u});
                return binary_of(rec, op->ground.sval, left, right);
            }
        }
    }
    return fold_ctor(rec, ctor, list);
}

static bool is_terms_spine(Atom *atom) {
    return atom && atom->kind == ATOM_EXPR &&
           ((atom->expr.len == 1u &&
             is_symbol(atom->expr.elems[0], "tptp-rec:terms-nil")) ||
            (atom->expr.len == 3u &&
             is_symbol(atom->expr.elems[0], "tptp-rec:terms-cons")));
}

static Atom *terms_concat(Rec *rec, Atom *left, Atom *right) {
    Atom **heads = NULL;
    uint32_t len = 0u;
    uint32_t cap = 0u;
    Atom *cursor = left;
    Atom *result;
    uint32_t i;

    while (cursor && cursor->kind == ATOM_EXPR && cursor->expr.len == 3u &&
           is_symbol(cursor->expr.elems[0], "tptp-rec:terms-cons")) {
        Atom **grown;
        if (len == cap) {
            uint32_t next = cap ? cap * 2u : 32u;
            if (next < cap || (size_t)next > SIZE_MAX / sizeof(*heads)) {
                free(heads);
                rec_error(rec, "tptp records: term concatenation overflow");
                return NULL;
            }
            grown = realloc(heads, (size_t)next * sizeof(*grown));
            if (!grown) {
                free(heads);
                rec_error(rec,
                          "tptp records: term concatenation allocation failed");
                return NULL;
            }
            heads = grown;
            cap = next;
        }
        heads[len++] = cursor->expr.elems[1];
        cursor = cursor->expr.elems[2];
    }
    if (!cursor || (cursor->kind == ATOM_EXPR && cursor->expr.len == 1u &&
                    is_symbol(cursor->expr.elems[0],
                              "tptp-rec:terms-nil")))
        result = right;
    else
        result = cons_terms(rec, cursor, right);
    for (i = len; i > 0u && result; i--)
        result = cons_terms(rec, heads[i - 1u], result);
    free(heads);
    return result;
}

static Atom *seq_terms(Rec *rec, NodeList list) {
    Atom *chain = nil_terms(rec);
    uint32_t index;
    for (index = list.len; index > 0u; index--) {
        Atom *item;
        if (node_is_connective(rec, list.items[index - 1u]) ||
            node_is_operator(list.items[index - 1u]))
            continue;
        item = project_node(rec, list.items[index - 1u]);
        if (!item)
            return NULL;
        if (is_terms_spine(item))
            chain = terms_concat(rec, item, chain);
        else
            chain = cons_terms(rec, item, chain);
    }
    return chain;
}

static Atom *seq_names(Rec *rec, NodeList list) {
    if (list.len == 1u)
        return cons_names(rec, project_node(rec, list.items[0]),
                          nil_names(rec));
    if (list.len >= 2u)
        return cons_names(rec, project_node(rec, list.items[0]),
                          project_node(rec, list.items[1]));
    return nil_names(rec);
}

static Atom *seq_vars(Rec *rec, NodeList list) {
    if (list.len == 1u)
        return cons_vars(rec, project_node(rec, list.items[0]), nil_vars(rec));
    if (list.len >= 2u)
        return cons_vars(rec, project_node(rec, list.items[0]),
                         project_node(rec, list.items[1]));
    return nil_vars(rec);
}

static Atom *plain_term(Rec *rec, NodeList list) {
    if (list.len == 1u)
        return app2(rec, "tptp-rec:fun", project_node(rec, list.items[0]),
                    nil_terms(rec));
    if (list.len >= 2u)
        return app2(rec, "tptp-rec:fun", project_node(rec, list.items[0]),
                    project_node(rec, list.items[1]));
    rec_error(rec, "tptp records: empty plain term");
    return NULL;
}

static Atom *project_annotated(Rec *rec, const char *ctor,
                               const char *formula_child, int64_t start,
                               int64_t stop, NodeList list) {
    Atom *name_n = child_named(rec, list, "name");
    Atom *role_n = child_named(rec, list, "formula_role");
    Atom *formula_n = child_named(rec, list, formula_child);
    Atom *ann_n = child_named(rec, list, "annotations");
    if (!name_n || !role_n || !formula_n || !ann_n)
        return NULL;
    return app5(rec, ctor, project_node(rec, name_n), project_node(rec, role_n),
                project_node(rec, formula_n), project_node(rec, ann_n),
                span_atom(rec, start, stop));
}

static Atom *prefix_unary(Rec *rec, const char *prod, Atom *node,
                          NodeList list) {
    Atom *body;
    uint32_t index;
    bool saw_not = leading_not(rec, node);
    if (list.len == 0u) {
        rec_error(rec, "tptp records: empty unary");
        return NULL;
    }
    (void)prod;
    for (index = 0u; index + 1u < list.len; index++) {
        const char *clabel = NULL;
        char cname[128];
        uint32_t calt = 0u;
        if (node_text_is(rec, list.items[index], "~"))
            saw_not = true;
        if (cst_view(list.items[index], &clabel, NULL, NULL, NULL, NULL)) {
            split_label(clabel, cname, sizeof(cname), &calt);
            if (name_eq(cname, "unary_connective"))
                saw_not = true;
            else if ((name_eq(cname, "tff_unary_connective") ||
                      name_eq(cname, "thf_unary_connective")) &&
                     leading_not(rec, list.items[index]))
                saw_not = true;
        }
    }
    body = project_node(rec, list.items[list.len - 1u]);
    if (saw_not)
        return app1(rec, "tptp-rec:not", body);
    if (list.len == 1u)
        return body;
    return app2(rec, "tptp-rec:prefix", token_text_atom(rec, list.items[0]),
                body);
}

static Atom *quant_ctor(Rec *rec, const char *q, Atom *vars, Atom *body) {
    const char *ctor = "tptp-rec:forall";
    if (name_eq(q, "?"))
        ctor = "tptp-rec:exists";
    else if (name_eq(q, "^"))
        ctor = "tptp-rec:lambda";
    else if (name_eq(q, "@+"))
        ctor = "tptp-rec:choice";
    else if (name_eq(q, "@-"))
        ctor = "tptp-rec:definite";
    else if (name_eq(q, "!>"))
        ctor = "tptp-rec:pi";
    else if (name_eq(q, "?*"))
        ctor = "tptp-rec:sigma";
    return app2(rec, ctor, vars, body);
}

static Atom *project_named(Rec *rec, const char *name, uint32_t alt,
                           int64_t start, int64_t stop, Atom *node,
                           NodeList list) {
    static const char *const or_names[] = {
        "fof_or_formula", "cnf_disjunction", "tff_or_formula",
        "thf_or_formula", NULL};
    static const char *const and_names[] = {
        "fof_and_formula", "tff_and_formula", "thf_and_formula", NULL};
    static const char *const apply_names[] = {
        "thf_apply_formula", "nxf_atom", NULL};
    static const char *const map_names[] = {
        "tff_mapping_type", "thf_mapping_type", NULL};
    static const char *const xprod_names[] = {
        "tff_xprod_type", "thf_xprod_type", NULL};
    static const char *const nonassoc_names[] = {
        "fof_binary_nonassoc", "tff_binary_nonassoc", "thf_binary_nonassoc",
        NULL};
    static const char *const eq_names[] = {
        "fof_defined_infix_formula", "tff_defined_infix", "thf_defined_infix",
        "txf_let_defn", "txf_definition", "thf_definition", "thf_let_defn",
        NULL};
    static const char *const neq_names[] = {
        "fof_infix_unary", "tff_infix_unary", "thf_infix_unary", NULL};
    static const char *const unary_names[] = {
        "fof_unary_formula", "cnf_literal", "tff_prefix_unary",
        "thf_unary_formula", "thf_prefix_unary", NULL};
    static const char *const plain_atomic_names[] = {
        "fof_plain_atomic_formula", "fof_defined_plain_formula",
        "tff_plain_atomic", "tff_system_atomic", "thf_plain_atomic",
        "thf_system_atomic", NULL};
    static const char *const plain_term_names[] = {
        "fof_plain_term", "fof_defined_plain_term", "thf_plain_term",
        "thf_fof_function", NULL};
    static const char *const arg_names[] = {
        "fof_arguments", "tff_arguments", "thf_arguments",
        "tff_type_arguments", NULL};
    static const char *const list_names[] = {
        "tff_type_list", "tff_atom_typing_list", "txf_let_defn_list",
        "thf_formula_list", "thf_atom_typing_list", "thf_let_defn_list",
        NULL};
    static const char *const varlist_names[] = {
        "fof_variable_list", "thf_variable_list", "tff_variable_list", NULL};
    static const char *const typed_var_names[] = {
        "thf_typed_variable", "tff_typed_variable", NULL};
    static const char *const typing_names[] = {
        "tff_atom_typing", "thf_atom_typing", NULL};
    static const char *const token_names[] = {
        "nonassoc_connective", "unary_connective", "defined_infix_pred",
        "infix_equality", "infix_inequality", "tff_unary_connective",
        "thf_unary_connective", "type_quantifier",
        "ntf_short_connective", "assignment", "arrow", "star", NULL};
    static const char *const long_conn_names[] = {
        "nxf_long_connective", "nhf_long_connective", NULL};

    if (name_eq(name, "TPTP_file")) {
        Atom *chain = nil_inputs(rec);
        uint32_t index;
        for (index = list.len; index > 0u; index--)
            chain = cons_inputs(rec, project_node(rec, list.items[index - 1u]),
                                chain);
        return app2(rec, "tptp-rec:file", chain, span_atom(rec, start, stop));
    }
    if (name_eq(name, "fof_annotated"))
        return project_annotated(rec, "tptp-rec:fof", "fof_formula", start,
                                 stop, list);
    if (name_eq(name, "cnf_annotated"))
        return project_annotated(rec, "tptp-rec:cnf", "cnf_formula", start,
                                 stop, list);
    if (name_eq(name, "tff_annotated"))
        return project_annotated(rec, "tptp-rec:tff", "tff_formula", start,
                                 stop, list);
    if (name_eq(name, "thf_annotated"))
        return project_annotated(rec, "tptp-rec:thf", "thf_formula", start,
                                 stop, list);
    if (name_eq(name, "tcf_annotated"))
        return project_annotated(rec, "tptp-rec:tcf", "tcf_formula", start,
                                 stop, list);
    if (name_eq(name, "tpi_annotated"))
        return project_annotated(rec, "tptp-rec:tpi", "tpi_formula", start,
                                 stop, list);
    if (name_eq(name, "include")) {
        Atom *file = child_named(rec, list, "file_name");
        Atom *selection = app0(rec, "tptp-rec:include-all");
        Atom *qualification = app0(rec, "tptp-rec:name-none");
        uint32_t index;
        if (!file)
            return NULL;
        for (index = 0u; index < list.len; index++) {
            const char *clabel = NULL;
            char cname[128];
            uint32_t calt = 0u;
            Atom *projected;
            if (!cst_view(list.items[index], &clabel, NULL, NULL, NULL, NULL))
                continue;
            split_label(clabel, cname, sizeof(cname), &calt);
            if (!name_eq(cname, "formula_selection") &&
                !name_eq(cname, "include_optionals"))
                continue;
            projected = project_node(rec, list.items[index]);
            if (projected && projected->kind == ATOM_EXPR &&
                projected->expr.len == 3u &&
                is_symbol(projected->expr.elems[0],
                          "tptp-rec-v1:include-options-value")) {
                selection = projected->expr.elems[1];
                qualification = projected->expr.elems[2];
            } else if (projected && projected->kind == ATOM_EXPR &&
                projected->expr.len > 0u &&
                (is_symbol(projected->expr.elems[0],
                           "tptp-rec:include-names") ||
                 is_symbol(projected->expr.elems[0],
                           "tptp-rec:include-star")))
                selection = projected;
        }
        return app4(rec, "tptp-rec:include", project_node(rec, file), selection,
                    qualification,
                    span_atom(rec, start, stop));
    }
    if (name_eq(name, "include_optionals")) {
        Atom *selection = app0(rec, "tptp-rec:include-all");
        Atom *qualification = app0(rec, "tptp-rec:name-none");
        Atom *selection_node = find_named(list, "formula_selection");
        Atom *space_node = find_named(list, "space_name");
        if (selection_node)
            selection = project_node(rec, selection_node);
        if (space_node)
            qualification = project_node(rec, space_node);
        return app2(rec, "tptp-rec-v1:include-options-value", selection,
                    qualification);
    }
    if (name_eq(name, "parent_info")) {
        if (list.len == 0u)
            return app0(rec, "tptp-rec:annotation-none");
        return project_node(rec, list.items[0]);
    }
    if (name_eq(name, "formula_selection")) {
        if (list.len == 0u)
            return app0(rec, "tptp-rec:include-star");
        return app1(rec, "tptp-rec:include-names",
                    project_node(rec, list.items[0]));
    }
    if (name_eq(name, "name_list"))
        return seq_names(rec, list);
    if (name_eq(name, "name")) {
        if (alt == 1u && list.len > 0u)
            return app1(rec, "tptp-rec:name-integer",
                        token_text_atom(rec, list.items[0]));
        return app1(rec, "tptp-rec:name-word",
                    project_node(rec, list.items[0]));
    }
    if (name_eq(name, "atomic_word") || name_eq(name, "lower_word") ||
        name_eq(name, "single_quoted") || name_eq(name, "upper_word") ||
        name_eq(name, "dollar_word") || name_eq(name, "dollar_dollar_word") ||
        name_eq(name, "integer")) {
        if (name_eq(name, "atomic_word") && list.len == 1u)
            return project_node(rec, list.items[0]);
        return word_from_kind(rec, name, token_text_atom(rec, node));
    }
    if (name_eq(name, "formula_role"))
        return app1(rec, "tptp-rec:role", token_text_atom(rec, node));
    if (name_eq(name, "variable"))
        return app1(rec, "tptp-rec:var",
                    word_from_kind(rec, "upper_word",
                                   token_text_atom(rec, node)));
    if (name_eq(name, "nothing"))
        return app0(rec, "tptp-rec:annotation-none");
    if (name_eq(name, "annotations")) {
        if (list.len == 1u)
            return project_node(rec, list.items[0]);
        if (list.len >= 2u)
            return app2(rec, "tptp-rec:annotation",
                        project_node(rec, list.items[0]),
                        project_node(rec, list.items[1]));
        return app0(rec, "tptp-rec:annotation-none");
    }
    if (name_eq(name, "optional_info")) {
        if (list.len == 0u)
            return app0(rec, "tptp-rec:optional-none");
        {
            const char *clabel = NULL;
            char cname[128];
            uint32_t calt = 0u;
            if (cst_view(list.items[0], &clabel, NULL, NULL, NULL, NULL)) {
                split_label(clabel, cname, sizeof(cname), &calt);
                if (name_eq(cname, "nothing"))
                    return app0(rec, "tptp-rec:optional-none");
            }
        }
        return app1(rec, "tptp-rec:optional-info",
                    project_node(rec, list.items[0]));
    }
    if (name_eq(name, "source")) {
        if (list.len == 1u)
            return project_node(rec, list.items[0]);
        return app0(rec, "tptp-rec:source-unknown");
    }
    if (name_eq(name, "unknown"))
        return app0(rec, "tptp-rec:source-unknown");
    if (name_eq(name, "file_source")) {
        Atom *file = find_named(list, "file_name");
        Atom *info = find_named(list, "file_info");
        Atom *info_rec;
        if (!file)
            return NULL;
        info_rec = info ? project_node(rec, info) : app0(rec, "tptp-rec:name-none");
        if (!info_rec ||
            (info_rec->kind == ATOM_EXPR && info_rec->expr.len == 1u &&
             is_symbol(info_rec->expr.elems[0], "tptp-rec:annotation-none")))
            info_rec = app0(rec, "tptp-rec:name-none");
        return app2(rec, "tptp-rec:file-source", project_node(rec, file),
                    info_rec);
    }
    if (name_eq(name, "file_info")) {
        Atom *nm = find_named(list, "name");
        if (!nm)
            return app0(rec, "tptp-rec:name-none");
        return project_node(rec, nm);
    }
    if (name_eq(name, "internal_source")) {
        Atom *kind = find_named(list, "intro_type");
        Atom *info = find_named(list, "useful_info");
        Atom *parents = find_named(list, "parents");
        if (!kind)
            return NULL;
        return app3(rec, "tptp-rec:introduced", project_node(rec, kind),
                    info ? project_node(rec, info) : app1(rec, "tptp-rec:gen-list",
                                                         nil_terms(rec)),
                    parents ? project_node(rec, parents)
                            : app1(rec, "tptp-rec:gen-list", nil_terms(rec)));
    }
    if (name_eq(name, "creator_source")) {
        Atom *cname = find_named(list, "creator_name");
        Atom *info = find_named(list, "useful_info");
        Atom *parents = find_named(list, "parents");
        if (!cname)
            return NULL;
        return app3(rec, "tptp-rec:creator", project_node(rec, cname),
                    info ? project_node(rec, info) : app1(rec, "tptp-rec:gen-list",
                                                         nil_terms(rec)),
                    parents ? project_node(rec, parents)
                            : app1(rec, "tptp-rec:gen-list", nil_terms(rec)));
    }
    if (name_eq(name, "theory")) {
        Atom *tname = find_named(list, "theory_name");
        Atom *info = find_named(list, "optional_info");
        if (!tname)
            return NULL;
        return app2(rec, "tptp-rec:theory", project_node(rec, tname),
                    info ? project_node(rec, info) : app0(rec, "tptp-rec:optional-none"));
    }
    if (name_eq(name, "inference_record")) {
        Atom *rule = child_named(rec, list, "inference_rule");
        Atom *info = child_named(rec, list, "useful_info");
        Atom *parents = child_named(rec, list, "parents");
        if (!rule || !info || !parents)
            return NULL;
        return app3(rec, "tptp-rec:inference", project_node(rec, rule),
                    project_node(rec, info), project_node(rec, parents));
    }
    if (name_eq(name, "parents") || name_eq(name, "parent_list") ||
        name_eq(name, "general_list") || name_eq(name, "general_terms")) {
        Atom *chain = seq_terms(rec, list);
        if (name_eq(name, "parents") || name_eq(name, "general_list"))
            return app1(rec, "tptp-rec:gen-list", chain);
        return chain;
    }
    if (name_eq(name, "general_function")) {
        Atom *fun = list.len ? project_node(rec, list.items[0]) : NULL;
        Atom *args = list.len > 1u ? project_node(rec, list.items[1])
                                   : nil_terms(rec);
        return app2(rec, "tptp-rec:gen-fun", fun, args);
    }
    if (name_in(name, or_names))
        return operands_project(rec, list, "or", start, stop);
    if (name_in(name, and_names))
        return operands_project(rec, list, "and", start, stop);
    if (name_in(name, apply_names))
        return operands_project(rec, list, "apply", start, stop);
    if (name_in(name, map_names))
        return operands_project(rec, list, "map", start, stop);
    if (name_in(name, xprod_names))
        return operands_project(rec, list, "xprod", start, stop);
    if (name_in(name, nonassoc_names))
        return operands_project(rec, list, "binary", start, stop);
    if (name_in(name, eq_names))
        return operands_project(rec, list, "eq", start, stop);
    if (name_in(name, neq_names))
        return operands_project(rec, list, "neq", start, stop);
    if (name_in(name, unary_names))
        return prefix_unary(rec, name, node, list);
    if (name_eq(name, "tff_defined_plain")) {
        const char *clabel = NULL;
        char cname[128];
        uint32_t calt = 0u;
        if (list.len &&
            cst_view(list.items[0], &clabel, NULL, NULL, NULL, NULL)) {
            split_label(clabel, cname, sizeof(cname), &calt);
            if (name_eq(cname, "txf_let") || name_eq(cname, "nxf_atom"))
                return project_node(rec, list.items[0]);
        }
        return atom_from_term(rec, plain_term(rec, list));
    }
    if (name_in(name, plain_atomic_names)) {
        if (list.len == 1u)
            return atom_from_term(rec, project_node(rec, list.items[0]));
        return atom_from_term(rec, plain_term(rec, list));
    }
    if (name_in(name, plain_term_names))
        return plain_term(rec, list);
    if (name_in(name, arg_names) || name_in(name, list_names))
        return seq_terms(rec, list);
    if (name_eq(name, "txf_tuple") || name_eq(name, "thf_tuple"))
        return app1(rec, "tptp-rec:gen-list", seq_terms(rec, list));
    if (name_eq(name, "general_term") && list.len >= 2u)
        return app2(rec, "tptp-rec:gen-colon", project_node(rec, list.items[0]),
                    project_node(rec, list.items[list.len - 1u]));
    if (name_eq(name, "tff_atomic_type") && list.len >= 2u)
        return plain_term(rec, list);
    if (name_eq(name, "type_quantifier"))
        return token_text_atom(rec, node);
    if (name_eq(name, "tf1_quantified_type")) {
        Atom *quant = list.len ? token_text_atom(rec, list.items[0]) : NULL;
        Atom *vars = find_named(list, "tff_variable_list");
        Atom *body = find_named(list, "tff_monotype");
        char qbuf[8];
        const char *q = "!>";
        if (source_quant_text(rec, start, stop, qbuf, sizeof(qbuf)) &&
            qbuf[0] != '\0')
            q = qbuf;
        else if (quant && quant->kind == ATOM_GROUNDED &&
                 quant->ground.gkind == GV_STRING && quant->ground.sval &&
                 quant->ground.sval[0] != '\0')
            q = quant->ground.sval;
        if (!vars)
            vars = list.len > 1u ? list.items[1] : NULL;
        if (!body)
            body = list.len > 2u ? list.items[list.len - 1u] : NULL;
        if (!vars || !body) {
            rec_error(rec, "tptp records: malformed tf1 quantified type");
            return NULL;
        }
        return quant_ctor(rec, q, project_node(rec, vars),
                          project_node(rec, body));
    }
    if (name_eq(name, "tff_quantified_formula") ||
        name_eq(name, "fof_quantified_formula")) {
        Atom *quant = list.len ? token_text_atom(rec, list.items[0]) : NULL;
        const char *q = "!";
        char qbuf[8];
        if (quant && quant->kind == ATOM_GROUNDED &&
            quant->ground.gkind == GV_STRING && quant->ground.sval &&
            quant->ground.sval[0] != '\0')
            q = quant->ground.sval;
        if (source_quant_text(rec, start, stop, qbuf, sizeof(qbuf)) &&
            qbuf[0] != '\0')
            q = qbuf;
        Atom *vars = find_named(list, name_eq(name, "fof_quantified_formula")
                                          ? "fof_variable_list"
                                          : "tff_variable_list");
        Atom *body = find_named(list, name_eq(name, "fof_quantified_formula")
                                          ? "fof_unit_formula"
                                          : "tff_unit_formula");
        if (!vars)
            vars = list.len > 1u ? list.items[1] : NULL;
        if (!body)
            body = list.len > 2u ? list.items[list.len - 1u] : NULL;
        if (!vars || !body) {
            rec_error(rec, "tptp records: malformed quantified formula");
            return NULL;
        }
        return quant_ctor(rec, q, project_node(rec, vars),
                          project_node(rec, body));
    }
    if (name_eq(name, "thf_quantified_formula")) {
        Atom *quantification = child_named(rec, list, "thf_quantification");
        Atom *body = child_named(rec, list, "thf_unit_formula");
        const char *label2 = NULL;
        Atom **qkids = NULL;
        uint32_t qn = 0u;
        NodeVec qvec;
        NodeList qlist;
        Atom *vars;
        Atom *qtext;
        const char *q = "!";
        char qbuf[8];
        if (!quantification || !body)
            return NULL;
        if (!cst_view(quantification, &label2, NULL, NULL, &qkids, &qn))
            return NULL;
        vec_init(&qvec);
        if (!collect_reals(rec, qkids, qn, &qvec)) {
            vec_free(&qvec);
            return NULL;
        }
        qlist.items = qvec.items;
        qlist.len = qvec.len;
        qtext = qlist.len ? token_text_atom(rec, qlist.items[0]) : NULL;
        vars = find_named(qlist, "thf_variable_list");
        {
            int64_t qstart = start;
            int64_t qstop = stop;
            cst_view(quantification, NULL, &qstart, &qstop, NULL, NULL);
            if (qtext && qtext->kind == ATOM_GROUNDED &&
                qtext->ground.gkind == GV_STRING && qtext->ground.sval &&
                qtext->ground.sval[0] != '\0')
                q = qtext->ground.sval;
            if (source_quant_text(rec, qstart, qstop, qbuf, sizeof(qbuf)) &&
                qbuf[0] != '\0')
                q = qbuf;
        }
        if (!vars)
            vars = qlist.len > 1u ? qlist.items[1] : NULL;
        if (!vars) {
            vec_free(&qvec);
            return NULL;
        }
        {
            Atom *result =
                quant_ctor(rec, q, project_node(rec, vars),
                           project_node(rec, body));
            vec_free(&qvec);
            return result;
        }
    }
    if (name_eq(name, "tcf_quantified_formula")) {
        Atom *vars = child_named(rec, list, "tff_variable_list");
        Atom *body = child_named(rec, list, "tcf_logic_formula");
        if (!vars || !body)
            return NULL;
        return app2(rec, "tptp-rec:forall", project_node(rec, vars),
                    project_node(rec, body));
    }
    if (name_in(name, varlist_names))
        return seq_vars(rec, list);
    if (name_in(name, typed_var_names)) {
        if (list.len >= 2u)
            return app2(rec, "tptp-rec:typed-var",
                        project_node(rec, list.items[0]),
                        project_node(rec, list.items[list.len - 1u]));
    }
    if (name_in(name, typing_names))
        return operands_project(rec, list, "typing", start, stop);
    if (name_eq(name, "defined_type"))
        return app1(rec, "tptp-rec:type-defined",
                    word_from_kind(rec, "dollar_word",
                                   token_text_atom(rec, node)));
    if (name_in(name, token_names))
        return token_text_atom(rec, node);
    if (name_eq(name, "txf_let") || name_eq(name, "thf_let")) {
        if (list.len >= 3u)
            return app3(rec, "tptp-rec:let", project_node(rec, list.items[0]),
                        project_node(rec, list.items[1]),
                        project_node(rec, list.items[2]));
        if (list.len == 1u)
            return project_node(rec, list.items[0]);
    }
    if (name_in(name, long_conn_names)) {
        Atom *cname = list.len ? project_node(rec, list.items[0]) : NULL;
        Atom *args = list.len > 1u ? project_node(rec, list.items[1])
                                   : nil_terms(rec);
        return app2(rec, "tptp-rec:long-connective", cname, args);
    }
    if (list.len == 1u)
        return project_node(rec, list.items[0]);
    if (list.len == 0u)
        return app0(rec, "tptp-rec:annotation-none");
    return app1(rec, "TPTP:Unprojected", atom_string(rec->arena, name));
}

static Atom *project_node(Rec *rec, Atom *node) {
    const char *label = NULL;
    char name[160];
    uint32_t alt = 0u;
    int64_t start = 0;
    int64_t stop = 0;
    Atom **kids = NULL;
    uint32_t nkids = 0u;
    NodeVec vec;
    NodeList list;
    const char *token_kind;
    Atom *result = NULL;
    Atom *memoized = NULL;
    if (!node)
        return NULL;
    if (projection_memo_get(&rec->projection_memo, node, &memoized))
        return memoized;
    if (!cst_view(node, &label, &start, &stop, &kids, &nkids)) {
        if (node->kind == ATOM_EXPR && node->expr.len == 2u &&
            is_symbol(node->expr.elems[0], "cp")) {
            result = token_text_atom(rec, node);
        } else {
            rec_error(rec, "tptp records: malformed CST node");
        }
        if (!projection_memo_put(&rec->projection_memo, node, result))
            rec_error(rec, "tptp records: projection memo allocation failed");
        return result;
    }
    split_label(label, name, sizeof(name), &alt);
    if (is_layout_name(name)) {
        result = app0(rec, "tptp-rec:annotation-none");
        if (!projection_memo_put(&rec->projection_memo, node, result))
            return rec_error(rec,
                             "tptp records: projection memo allocation failed"),
                   NULL;
        return result;
    }
    token_kind = token_kind_from_wrapper(name);
    if (token_kind) {
        result = word_from_kind(rec, token_kind, token_text_atom(rec, node));
        if (!projection_memo_put(&rec->projection_memo, node, result))
            return rec_error(rec,
                             "tptp records: projection memo allocation failed"),
                   NULL;
        return result;
    }
    vec_init(&vec);
    if (!collect_reals(rec, kids, nkids, &vec)) {
        vec_free(&vec);
        (void)projection_memo_put(&rec->projection_memo, node, NULL);
        return NULL;
    }
    list.items = vec.items;
    list.len = vec.len;
    if (is_generated_helper(name) || is_unwrap_name(name)) {
        if (list.len == 1u)
            result = project_node(rec, list.items[0]);
        else if (list.len == 0u)
            result = app0(rec, "tptp-rec:annotation-none");
        else if (name_eq(name, "#entry"))
            result = project_node(rec, list.items[list.len - 1u]);
        else
            result = project_named(rec, name, alt, start, stop, node, list);
        vec_free(&vec);
        if (!projection_memo_put(&rec->projection_memo, node, result))
            return rec_error(rec,
                             "tptp records: projection memo allocation failed"),
                   NULL;
        return result;
    }
    result = project_named(rec, name, alt, start, stop, node, list);
    vec_free(&vec);
    if (!projection_memo_put(&rec->projection_memo, node, result))
        return rec_error(rec,
                         "tptp records: projection memo allocation failed"),
               NULL;
    return result;
}

typedef struct {
    Atom *node;
    Atom **children;
    uint32_t child_len;
    uint32_t next_child;
    bool opened;
} ProjectionFrame;

typedef struct {
    ProjectionFrame *items;
    uint32_t len;
    uint32_t cap;
} ProjectionFrameVec;

static bool projection_frame_push(ProjectionFrameVec *frames, Atom *node) {
    ProjectionFrame *grown;
    uint32_t cap;
    if (frames->len == frames->cap) {
        cap = frames->cap ? frames->cap * 2u : 128u;
        if (cap < frames->cap ||
            (size_t)cap > SIZE_MAX / sizeof(*frames->items))
            return false;
        grown = realloc(frames->items, (size_t)cap * sizeof(*grown));
        if (!grown)
            return false;
        frames->items = grown;
        frames->cap = cap;
    }
    memset(&frames->items[frames->len], 0, sizeof(*frames->items));
    frames->items[frames->len].node = node;
    frames->len++;
    return true;
}

static Atom *project_tree_iterative(Rec *rec, Atom *root) {
    ProjectionFrameVec frames = {0};
    Atom *result = NULL;

    if (!root || !projection_frame_push(&frames, root)) {
        rec_error(rec, "tptp records: projection worklist allocation failed");
        return NULL;
    }
    while (frames.len > 0u) {
        ProjectionFrame *frame = &frames.items[frames.len - 1u];
        Atom *memoized = NULL;
        if (projection_memo_get(&rec->projection_memo, frame->node,
                                &memoized)) {
            frames.len--;
            continue;
        }
        if (!frame->opened) {
            if (!cst_view(frame->node, NULL, NULL, NULL,
                          &frame->children, &frame->child_len)) {
                (void)project_node(rec, frame->node);
                frames.len--;
                continue;
            }
            frame->opened = true;
        }
        while (frame->next_child < frame->child_len) {
            Atom *child = frame->children[frame->next_child++];
            if (!cst_view(child, NULL, NULL, NULL, NULL, NULL) ||
                projection_memo_get(&rec->projection_memo, child, NULL))
                continue;
            if (!projection_frame_push(&frames, child)) {
                free(frames.items);
                rec_error(rec,
                          "tptp records: projection worklist allocation failed");
                return NULL;
            }
            frame = NULL;
            break;
        }
        if (!frame)
            continue;
        if (frame->next_child < frame->child_len)
            continue;
        result = project_node(rec, frame->node);
        frames.len--;
    }
    free(frames.items);
    if (!projection_memo_get(&rec->projection_memo, root, &result))
        return NULL;
    return result;
}

static bool read_span(Atom *node, int64_t *start, int64_t *stop) {
    Atom *sp;
    if (!node || node->kind != ATOM_EXPR || node->expr.len < 2u)
        return false;
    sp = node->expr.elems[node->expr.len - 1u];
    if (!sp || sp->kind != ATOM_EXPR || sp->expr.len != 3u ||
        !is_symbol(sp->expr.elems[0], "tptp-rec:span") ||
        !sp->expr.elems[1] || sp->expr.elems[1]->kind != ATOM_GROUNDED ||
        !sp->expr.elems[2] || sp->expr.elems[2]->kind != ATOM_GROUNDED)
        return false;
    if (start)
        *start = sp->expr.elems[1]->ground.ival;
    if (stop)
        *stop = sp->expr.elems[2]->ground.ival;
    return true;
}

static Atom *copy_with_span(Rec *rec, Atom *node, int64_t start, int64_t stop) {
    Atom **elems;
    uint32_t n;
    uint32_t i;
    if (!node || node->kind != ATOM_EXPR || node->expr.len < 2u)
        return node;
    n = node->expr.len;
    elems = malloc(sizeof(*elems) * n);
    if (!elems)
        return NULL;
    for (i = 0u; i + 1u < n; i++)
        elems[i] = node->expr.elems[i];
    elems[n - 1u] = span_atom(rec, start, stop);
    {
        Atom *out = atom_expr(rec->arena, elems, n);
        free(elems);
        return out;
    }
}

static Atom *retouch_file_layout_spans(Rec *rec, Atom *file, int64_t eof) {
    Atom *inputs;
    Atom **items = NULL;
    uint32_t n = 0u;
    uint32_t cap = 0u;
    uint32_t i;
    Atom *chain;
    int64_t file_start = 0;
    if (!file || file->kind != ATOM_EXPR || file->expr.len < 2u ||
        !is_symbol(file->expr.elems[0], "tptp-rec:file"))
        return file;
    inputs = file->expr.elems[1];
    while (inputs && inputs->kind == ATOM_EXPR && inputs->expr.len == 3u &&
           is_symbol(inputs->expr.elems[0], "tptp-rec:inputs-cons")) {
        if (n == cap) {
            uint32_t ncap = cap ? cap * 2u : 16u;
            Atom **next = realloc(items, sizeof(*next) * ncap);
            if (!next) {
                free(items);
                return file;
            }
            items = next;
            cap = ncap;
        }
        items[n++] = inputs->expr.elems[1];
        inputs = inputs->expr.elems[2];
    }
    if (n == 0u) {
        free(items);
        return copy_with_span(rec, file, 0, 0);
    }
    for (i = 0u; i < n; i++) {
        int64_t start = 0;
        int64_t stop = 0;
        int64_t next_start;
        if (!read_span(items[i], &start, &stop)) {
            free(items);
            return file;
        }
        if (i + 1u < n) {
            if (!read_span(items[i + 1u], &next_start, NULL)) {
                free(items);
                return file;
            }
            stop = next_start;
        } else {
            stop = eof > start ? eof : stop;
        }
        if (i == 0u)
            file_start = start;
        items[i] = copy_with_span(rec, items[i], start, stop);
        if (!items[i]) {
            free(items);
            return file;
        }
    }
    chain = app0(rec, "tptp-rec:inputs-nil");
    for (i = n; i > 0u; i--)
        chain = cons_inputs(rec, items[i - 1u], chain);
    free(items);
    return app2(rec, "tptp-rec:file", chain, span_atom(rec, file_start, eof));
}

static int64_t source_scalar_len(const char *source) {
    int64_t scalars = 0;
    const unsigned char *p;
    if (!source)
        return 0;
    p = (const unsigned char *)source;
    while (*p) {
        if ((*p & 0xC0u) != 0x80u)
            scalars++;
        p++;
    }
    return scalars;
}

static int source_is_ascii(const char *source) {
    const unsigned char *p;
    if (!source)
        return 1;
    p = (const unsigned char *)source;
    while (*p) {
        if (*p >= 0x80u)
            return 0;
        p++;
    }
    return 1;
}

static bool records_from_cst(const Atom *trees, const char *source,
                             Arena *arena, Atom **out, char *error,
                             size_t error_size, bool retouch,
                             int source_ascii) {
    Rec rec;
    Atom *root;
    memset(&rec, 0, sizeof(rec));
    rec.arena = arena;
    rec.source = source;
    rec.error = error;
    rec.error_size = error_size;
    rec.ascii = source_ascii < 0 ? source_is_ascii(source) : source_ascii;
    if (out)
        *out = NULL;
    if (!trees || !arena || !out)
        return rec_error(&rec, "tptp records: missing CST tuple");
    if (trees->kind != ATOM_EXPR || trees->expr.len == 0u)
        return rec_error(&rec, "tptp records: empty CST tuple");
    root = trees->expr.elems[0];
    *out = project_tree_iterative(&rec, root);
    if (*out && source && retouch)
        *out = retouch_file_layout_spans(&rec, *out, source_scalar_len(source));
    projection_memo_free(&rec.projection_memo);
    return *out != NULL;
}

bool cetta_tptp_records_from_cst_v1(const Atom *trees, const char *source,
                                    Arena *arena, Atom **out, char *error,
                                    size_t error_size) {
    return records_from_cst(trees, source, arena, out, error, error_size,
                            true, -1);
}

bool cetta_tptp_records_from_cst_noreouch_v1(const Atom *trees,
                                             const char *source, Arena *arena,
                                             Atom **out, char *error,
                                             size_t error_size) {
    return records_from_cst(trees, source, arena, out, error, error_size,
                            false, -1);
}

bool cetta_tptp_records_from_cst_noreouch_ascii_v1(
    const Atom *trees, const char *source, bool source_ascii, Arena *arena,
    Atom **out, char *error, size_t error_size) {
    return records_from_cst(trees, source, arena, out, error, error_size,
                            false, source_ascii ? 1 : 0);
}

bool cetta_tptp_file_from_inputs_v1(Arena *arena, const char *source,
                                    Atom **inputs, uint32_t n, Atom **out,
                                    char *error, size_t error_size) {
    Rec rec;
    Atom *chain;
    uint32_t i;
    memset(&rec, 0, sizeof(rec));
    rec.arena = arena;
    rec.source = source;
    rec.error = error;
    rec.error_size = error_size;
    if (out)
        *out = NULL;
    if (!arena || !out)
        return rec_error(&rec, "tptp records: missing file inputs");
    chain = nil_inputs(&rec);
    for (i = n; i > 0u; i--) {
        if (!inputs || !inputs[i - 1u])
            return rec_error(&rec, "tptp records: missing input");
        chain = cons_inputs(&rec, inputs[i - 1u], chain);
        if (!chain)
            return false;
    }
    *out = app2(&rec, "tptp-rec:file", chain, span_atom(&rec, 0, 0));
    if (*out && source)
        *out = retouch_file_layout_spans(&rec, *out, source_scalar_len(source));
    return *out != NULL;
}

bool cetta_tptp_file_sha256_hex_v1(const char *path, char out[65],
                                   char *error, size_t error_size) {
    FILE *file;
    uint8_t buffer[4096];
    size_t got;
    CettaNativeSha256 sha;
    if (!path || !out)
        return false;
    file = fopen(path, "rb");
    if (!file) {
        if (error && error_size)
            snprintf(error, error_size, "cannot open %s", path);
        return false;
    }
    cetta_native_sha256_init(&sha);
    while ((got = fread(buffer, 1u, sizeof(buffer), file)) > 0u)
        cetta_native_sha256_update(&sha, buffer, got);
    fclose(file);
    cetta_native_sha256_finish_hex(&sha, out);
    (void)error;
    (void)error_size;
    return true;
}

bool cetta_tptp_write_atom_v1(const char *path, Atom *atom, Arena *arena,
                              char *error, size_t error_size) {
    char *text;
    FILE *file;
    size_t len;
    if (!path || !atom || !arena)
        return false;
    text = atom_to_parseable_string(arena, atom);
    if (!text) {
        if (error && error_size)
            snprintf(error, error_size, "tptp cache: cannot print atom");
        return false;
    }
    file = fopen(path, "wb");
    if (!file) {
        if (error && error_size)
            snprintf(error, error_size, "tptp cache: cannot write %s", path);
        return false;
    }
    len = strlen(text);
    if (fwrite(text, 1u, len, file) != len) {
        fclose(file);
        if (error && error_size)
            snprintf(error, error_size, "tptp cache: short write %s", path);
        return false;
    }
    if (fputc('\n', file) == EOF) {
        fclose(file);
        return false;
    }
    fclose(file);
    return true;
}

bool cetta_tptp_read_atom_v1(const char *path, Arena *arena, Atom **out,
                             char *error, size_t error_size) {
    FILE *file;
    long size;
    char *text;
    size_t got;
    size_t pos = 0u;
    if (out)
        *out = NULL;
    if (!path || !arena || !out)
        return false;
    file = fopen(path, "rb");
    if (!file) {
        if (error && error_size)
            snprintf(error, error_size, "tptp cache: cannot open %s", path);
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0) {
        fclose(file);
        if (error && error_size)
            snprintf(error, error_size, "tptp cache: cannot size %s", path);
        return false;
    }
    rewind(file);
    text = malloc((size_t)size + 1u);
    if (!text) {
        fclose(file);
        if (error && error_size)
            snprintf(error, error_size, "tptp cache: out of memory");
        return false;
    }
    got = fread(text, 1u, (size_t)size, file);
    fclose(file);
    text[got] = '\0';
    *out = parse_sexpr(arena, text, &pos);
    free(text);
    if (!*out) {
        if (error && error_size)
            snprintf(error, error_size, "tptp cache: cannot parse %s", path);
        return false;
    }
    return true;
}

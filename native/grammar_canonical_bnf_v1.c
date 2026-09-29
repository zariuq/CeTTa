#include "native/grammar_canonical_bnf_v1.h"

#include "src/symbol.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool gcb_error(char *error, size_t size, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    if (error && size > 0u)
        (void)vsnprintf(error, size, format, arguments);
    va_end(arguments);
    return false;
}

static bool gcb_head(const Atom *atom, const char *head, uint32_t arity) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len == arity + 1u &&
           atom->expr.elems[0]->kind == ATOM_SYMBOL &&
           strcmp(symbol_bytes(g_symbols, atom->expr.elems[0]->sym_id), head) == 0;
}

static size_t gcb_utf8(uint32_t scalar, char *out) {
    if (scalar < 0x80u) {
        out[0] = (char)scalar;
        return 1u;
    }
    if (scalar < 0x800u) {
        out[0] = (char)(0xC0u | (scalar >> 6));
        out[1] = (char)(0x80u | (scalar & 0x3Fu));
        return 2u;
    }
    if (scalar < 0x10000u) {
        out[0] = (char)(0xE0u | (scalar >> 12));
        out[1] = (char)(0x80u | ((scalar >> 6) & 0x3Fu));
        out[2] = (char)(0x80u | (scalar & 0x3Fu));
        return 3u;
    }
    out[0] = (char)(0xF0u | (scalar >> 18));
    out[1] = (char)(0x80u | ((scalar >> 12) & 0x3Fu));
    out[2] = (char)(0x80u | ((scalar >> 6) & 0x3Fu));
    out[3] = (char)(0x80u | (scalar & 0x3Fu));
    return 4u;
}

/* A bnf-v1 text, the scalar list (bnf-v1:text-cons c ... (bnf-v1:text-nil)),
 * as malloc'd UTF-8. */
static char *gcb_text(const Atom *text) {
    size_t cap = 32u, len = 0u;
    char *bytes = malloc(cap);
    if (!bytes)
        return NULL;
    while (gcb_head(text, "bnf-v1:text-cons", 2u)) {
        const Atom *scalar = text->expr.elems[1];
        char encoded[4];
        size_t width;
        if (scalar->kind != ATOM_GROUNDED || scalar->ground.gkind != GV_INT ||
            scalar->ground.ival < 0 || scalar->ground.ival > 0x10FFFF) {
            free(bytes);
            return NULL;
        }
        width = gcb_utf8((uint32_t)scalar->ground.ival, encoded);
        if (len + width + 1u > cap) {
            char *grown;
            cap *= 2u;
            grown = realloc(bytes, cap);
            if (!grown) {
                free(bytes);
                return NULL;
            }
            bytes = grown;
        }
        memcpy(bytes + len, encoded, width);
        len += width;
        text = text->expr.elems[2];
    }
    if (!gcb_head(text, "bnf-v1:text-nil", 0u)) {
        free(bytes);
        return NULL;
    }
    bytes[len] = '\0';
    return bytes;
}

typedef struct {
    SymbolId name;       /* helper rule */
    CettaGrammarCanonicalRuleKindV1 kind;
    SymbolId owner;
    char repeat;         /* repetitions and options: '*', '+' or '?' */
} GcbOrigin;

typedef struct {
    SymbolId reference;  /* the grammar's name for the class */
    char *label;         /* the derivation-leaf label */
} GcbLexical;

typedef struct {
    GcbOrigin *origins;
    uint32_t origin_len;
    GcbLexical *lexicals;
    uint32_t lexical_len;
    CettaGrammarCanonicalProductionV1 *productions;
    uint32_t production_len;
    CettaGrammarCanonicalSymbolV1 *symbols;
    uint32_t symbol_len;
    uint32_t symbol_cap;
    char **texts;        /* owned literal texts */
    uint32_t text_len;
    uint32_t text_cap;
} GcbGrammar;

static void gcb_free(GcbGrammar *grammar) {
    uint32_t i;
    for (i = 0u; i < grammar->lexical_len; i++)
        free(grammar->lexicals[i].label);
    for (i = 0u; i < grammar->text_len; i++)
        free(grammar->texts[i]);
    free(grammar->origins);
    free(grammar->lexicals);
    free(grammar->productions);
    free(grammar->symbols);
    free(grammar->texts);
    memset(grammar, 0, sizeof(*grammar));
}

static SymbolId gcb_symbol(const Atom *text) {
    char *bytes = gcb_text(text);
    SymbolId symbol;
    if (!bytes)
        return SYMBOL_ID_NONE;
    symbol = symbol_intern_cstr(g_symbols, bytes);
    free(bytes);
    return symbol;
}

static bool gcb_read_origins(GcbGrammar *grammar, const Atom *origins, char *error,
                             size_t error_size) {
    const Atom *cursor;
    uint32_t count = 0u;
    for (cursor = origins; gcb_head(cursor, "ebnf-v1:origins-cons", 2u);
         cursor = cursor->expr.elems[2])
        count++;
    if (!gcb_head(cursor, "ebnf-v1:origins-nil", 0u))
        return gcb_error(error, error_size, "canonical bnf: malformed helper origins");
    grammar->origins = calloc(count ? count : 1u, sizeof(*grammar->origins));
    if (!grammar->origins)
        return gcb_error(error, error_size, "canonical bnf: out of memory");
    for (cursor = origins; gcb_head(cursor, "ebnf-v1:origins-cons", 2u);
         cursor = cursor->expr.elems[2]) {
        const Atom *origin = cursor->expr.elems[1];
        const Atom *kind, *owner;
        GcbOrigin *entry;
        const char *kind_name;
        uint32_t i;
        bool known = false;
        if (!gcb_head(origin, "ebnf-v1:helper-origin", 4u) ||
            origin->expr.elems[2]->kind != ATOM_SYMBOL ||
            !gcb_head(origin->expr.elems[3], "ebnf-v1:rule-origin", 2u))
            return gcb_error(error, error_size, "canonical bnf: malformed helper origin");
        kind = origin->expr.elems[2];
        owner = origin->expr.elems[3]->expr.elems[1];
        entry = &grammar->origins[grammar->origin_len];
        entry->name = gcb_symbol(origin->expr.elems[1]);
        entry->owner = gcb_symbol(owner);
        if (entry->name == SYMBOL_ID_NONE || entry->owner == SYMBOL_ID_NONE)
            return gcb_error(error, error_size, "canonical bnf: malformed helper name");
        for (i = 0u; i < grammar->origin_len && !known; i++)
            known = grammar->origins[i].name == entry->name;
        if (known)
            continue; /* first binding wins */
        kind_name = symbol_bytes(g_symbols, kind->sym_id);
        entry->repeat = 0;
        if (strcmp(kind_name, "group") == 0) {
            entry->kind = CETTA_GRAMMAR_CANONICAL_GROUP_V1;
        } else if (strcmp(kind_name, "optional") == 0) {
            entry->kind = CETTA_GRAMMAR_CANONICAL_OPTION_V1;
            entry->repeat = '?';
        } else if (strcmp(kind_name, "zero-or-more") == 0) {
            entry->kind = CETTA_GRAMMAR_CANONICAL_REPETITION_V1;
            entry->repeat = '*';
        } else if (strcmp(kind_name, "one-or-more") == 0) {
            entry->kind = CETTA_GRAMMAR_CANONICAL_REPETITION_V1;
            entry->repeat = '+';
        } else
            return gcb_error(error, error_size, "canonical bnf: unknown helper kind %s",
                             kind_name);
        grammar->origin_len++;
    }
    return true;
}

static bool gcb_read_lexicals(GcbGrammar *grammar, const Atom *authority, char *error,
                              size_t error_size) {
    const Atom *environment, *cursor;
    uint32_t count = 0u;
    if (!gcb_head(authority, "bnf-v1:grammar-authority", 2u) ||
        !gcb_head(authority->expr.elems[2], "bnf-v1:lexical-environment", 1u))
        return gcb_error(error, error_size, "canonical bnf: malformed grammar authority");
    environment = authority->expr.elems[2]->expr.elems[1];
    for (cursor = environment; gcb_head(cursor, "bnf-v1:lexical-declarations-cons", 2u);
         cursor = cursor->expr.elems[2])
        count++;
    if (!gcb_head(cursor, "bnf-v1:lexical-declarations-nil", 0u))
        return gcb_error(error, error_size, "canonical bnf: malformed lexical declarations");
    grammar->lexicals = calloc(count ? count : 1u, sizeof(*grammar->lexicals));
    if (!grammar->lexicals)
        return gcb_error(error, error_size, "canonical bnf: out of memory");
    for (cursor = environment; gcb_head(cursor, "bnf-v1:lexical-declarations-cons", 2u);
         cursor = cursor->expr.elems[2]) {
        const Atom *declaration = cursor->expr.elems[1];
        const Atom *label;
        GcbLexical *entry;
        if (!gcb_head(declaration, "bnf-v1:lexical-declaration", 5u))
            return gcb_error(error, error_size, "canonical bnf: malformed lexical declaration");
        label = declaration->expr.elems[4];
        if (label->kind != ATOM_GROUNDED || label->ground.gkind != GV_STRING)
            return gcb_error(error, error_size, "canonical bnf: lexical label is not a string");
        entry = &grammar->lexicals[grammar->lexical_len++];
        entry->reference = gcb_symbol(declaration->expr.elems[1]);
        entry->label = malloc(label->ground.slen + 1u);
        if (entry->reference == SYMBOL_ID_NONE || !entry->label)
            return gcb_error(error, error_size, "canonical bnf: malformed lexical declaration");
        memcpy(entry->label, label->ground.sval, label->ground.slen + 1u);
    }
    return true;
}

static const GcbOrigin *gcb_origin(const GcbGrammar *grammar, SymbolId name) {
    uint32_t i;
    for (i = 0u; i < grammar->origin_len; i++) {
        if (grammar->origins[i].name == name)
            return &grammar->origins[i];
    }
    return NULL;
}

static bool gcb_is_lexical(const GcbGrammar *grammar, SymbolId name) {
    uint32_t i;
    for (i = 0u; i < grammar->lexical_len; i++) {
        if (grammar->lexicals[i].reference == name)
            return true;
    }
    return false;
}

static bool gcb_add_symbol(GcbGrammar *grammar, CettaGrammarCanonicalSymbolV1 symbol) {
    if (grammar->symbol_len == grammar->symbol_cap) {
        uint32_t cap = grammar->symbol_cap ? grammar->symbol_cap * 2u : 256u;
        CettaGrammarCanonicalSymbolV1 *grown =
            realloc(grammar->symbols, (size_t)cap * sizeof(*grown));
        if (!grown)
            return false;
        grammar->symbols = grown;
        grammar->symbol_cap = cap;
    }
    grammar->symbols[grammar->symbol_len++] = symbol;
    return true;
}

static bool gcb_keep_text(GcbGrammar *grammar, char *text) {
    if (grammar->text_len == grammar->text_cap) {
        uint32_t cap = grammar->text_cap ? grammar->text_cap * 2u : 128u;
        char **grown = realloc(grammar->texts, (size_t)cap * sizeof(*grown));
        if (!grown)
            return false;
        grammar->texts = grown;
        grammar->text_cap = cap;
    }
    grammar->texts[grammar->text_len++] = text;
    return true;
}

/* The productions of the lowered document; right-hand sides are recorded as
 * offsets into the symbol array and resolved once it stops growing. */
static bool gcb_read_document(GcbGrammar *grammar, const Atom *document, char *error,
                              size_t error_size) {
    const Atom *entries, *cursor;
    uint32_t count = 0u;
    uint32_t *rhs_begin = NULL;
    if (!gcb_head(document, "bnf-v1:document", 2u))
        return gcb_error(error, error_size, "canonical bnf: malformed grammar document");
    entries = document->expr.elems[1];
    for (cursor = entries; gcb_head(cursor, "bnf-v1:entries-cons", 2u);
         cursor = cursor->expr.elems[2]) {
        const Atom *entry = cursor->expr.elems[1];
        const Atom *alternatives;
        if (!gcb_head(entry, "bnf-v1:rule", 3u))
            continue;
        if (!gcb_head(entry->expr.elems[2], "bnf-v1:expression", 2u))
            return gcb_error(error, error_size, "canonical bnf: malformed rule");
        for (alternatives = entry->expr.elems[2]->expr.elems[1];
             gcb_head(alternatives, "bnf-v1:alternatives-cons", 2u);
             alternatives = alternatives->expr.elems[2])
            count++;
    }
    grammar->productions = calloc(count ? count : 1u, sizeof(*grammar->productions));
    rhs_begin = calloc(count ? count : 1u, sizeof(*rhs_begin));
    if (!grammar->productions || !rhs_begin) {
        free(rhs_begin);
        return gcb_error(error, error_size, "canonical bnf: out of memory");
    }
    for (cursor = entries; gcb_head(cursor, "bnf-v1:entries-cons", 2u);
         cursor = cursor->expr.elems[2]) {
        const Atom *entry = cursor->expr.elems[1];
        const Atom *alternatives;
        const GcbOrigin *origin;
        SymbolId name;
        char *name_bytes;
        uint32_t index = 0u;
        if (!gcb_head(entry, "bnf-v1:rule", 3u))
            continue;
        name_bytes = gcb_text(entry->expr.elems[1]);
        if (!name_bytes) {
            free(rhs_begin);
            return gcb_error(error, error_size, "canonical bnf: malformed rule name");
        }
        name = symbol_intern_cstr(g_symbols, name_bytes);
        origin = gcb_origin(grammar, name);
        for (alternatives = entry->expr.elems[2]->expr.elems[1];
             gcb_head(alternatives, "bnf-v1:alternatives-cons", 2u);
             alternatives = alternatives->expr.elems[2], index++) {
            const Atom *alternative = alternatives->expr.elems[1];
            const Atom *elements;
            CettaGrammarCanonicalProductionV1 *production =
                &grammar->productions[grammar->production_len];
            size_t label_len = strlen(name_bytes) + 1u + index + 1u;
            char *label = malloc(label_len);
            if (!label || !gcb_head(alternative, "bnf-v1:alternative", 2u)) {
                free(label);
                free(name_bytes);
                free(rhs_begin);
                return gcb_error(error, error_size, "canonical bnf: malformed alternative");
            }
            /* The derivation label: the rule name, "#", and one x per index. */
            strcpy(label, name_bytes);
            strcat(label, "#");
            memset(label + strlen(name_bytes) + 1u, 'x', index);
            label[label_len - 1u] = '\0';
            production->label = symbol_intern_cstr(g_symbols, label);
            free(label);
            production->lhs = name;
            production->rule_kind = origin ? origin->kind : CETTA_GRAMMAR_CANONICAL_AUTHORED_V1;
            production->repeat = origin ? origin->repeat : 0;
            production->owner = origin ? origin->owner : SYMBOL_ID_NONE;
            rhs_begin[grammar->production_len] = grammar->symbol_len;
            for (elements = alternative->expr.elems[1];
                 gcb_head(elements, "bnf-v1:elements-cons", 2u);
                 elements = elements->expr.elems[2]) {
                const Atom *element = elements->expr.elems[1];
                CettaGrammarCanonicalSymbolV1 symbol;
                memset(&symbol, 0, sizeof(symbol));
                if (gcb_head(element, "bnf-v1:reference", 2u)) {
                    symbol.name = gcb_symbol(element->expr.elems[1]);
                    symbol.kind = gcb_is_lexical(grammar, symbol.name)
                                      ? CETTA_GRAMMAR_CANONICAL_TOKEN_SYMBOL_V1
                                      : CETTA_GRAMMAR_CANONICAL_RULE_SYMBOL_V1;
                } else if (gcb_head(element, "bnf-v1:literal", 2u)) {
                    char *text = gcb_text(element->expr.elems[1]);
                    if (!text || !gcb_keep_text(grammar, text)) {
                        free(text);
                        free(name_bytes);
                        free(rhs_begin);
                        return gcb_error(error, error_size, "canonical bnf: malformed literal");
                    }
                    if (text[0] == '\0')
                        continue; /* an empty literal contributes nothing */
                    symbol.kind = CETTA_GRAMMAR_CANONICAL_FIXED_SYMBOL_V1;
                    symbol.name = symbol_intern_cstr(g_symbols, text);
                    symbol.text = text;
                } else {
                    free(name_bytes);
                    free(rhs_begin);
                    return gcb_error(error, error_size, "canonical bnf: malformed element");
                }
                if (symbol.name == SYMBOL_ID_NONE || !gcb_add_symbol(grammar, symbol)) {
                    free(name_bytes);
                    free(rhs_begin);
                    return gcb_error(error, error_size, "canonical bnf: out of memory");
                }
            }
            production->rhs_len = grammar->symbol_len - rhs_begin[grammar->production_len];
            grammar->production_len++;
        }
        free(name_bytes);
    }
    for (count = 0u; count < grammar->production_len; count++)
        grammar->productions[count].rhs = grammar->symbols + rhs_begin[count];
    free(rhs_begin);
    return true;
}

static bool gcb_read(GcbGrammar *grammar, const Atom *document, const Atom *origins,
                     const Atom *authority, char *error, size_t error_size) {
    memset(grammar, 0, sizeof(*grammar));
    if (!gcb_read_origins(grammar, origins, error, error_size) ||
        !gcb_read_lexicals(grammar, authority, error, error_size) ||
        !gcb_read_document(grammar, document, error, error_size)) {
        gcb_free(grammar);
        return false;
    }
    return true;
}

bool cetta_grammar_canonical_bnf_table_v1(const Atom *document, const Atom *origins,
                                          const Atom *authority,
                                          CettaGrammarCanonicalTableV1 **out, char *error,
                                          size_t error_size) {
    GcbGrammar grammar;
    bool ok;
    if (out)
        *out = NULL;
    if (!gcb_read(&grammar, document, origins, authority, error, error_size))
        return false;
    ok = cetta_grammar_canonical_table_build_v1(grammar.productions, grammar.production_len, out,
                                                error, error_size);
    gcb_free(&grammar);
    return ok;
}

/* A lexical-class leaf, (CstRuleV1 label start stop (cp c)...), is the token
 * named by the grammar's reference to the class, with its scalars as text. */
typedef struct {
    const GcbGrammar *grammar;
} GcbLeafContext;

static bool gcb_leaf(void *context, Arena *arena, const Atom *node, const char *label,
                     SymbolId *token, Atom **text_out) {
    const GcbGrammar *grammar = ((const GcbLeafContext *)context)->grammar;
    const GcbLexical *lexical = NULL;
    char stack[64], *text = stack;
    size_t len = 0u, cap = sizeof(stack);
    uint32_t i;
    for (i = 0u; i < grammar->lexical_len && !lexical; i++) {
        if (strcmp(grammar->lexicals[i].label, label) == 0)
            lexical = &grammar->lexicals[i];
    }
    if (!lexical)
        return false;
    for (i = 4u; i < node->expr.len; i++) {
        const Atom *scalar = node->expr.elems[i];
        char encoded[4];
        size_t width;
        if (!gcb_head(scalar, "cp", 1u) || scalar->expr.elems[1]->kind != ATOM_GROUNDED ||
            scalar->expr.elems[1]->ground.gkind != GV_INT) {
            if (text != stack)
                free(text);
            return false;
        }
        width = gcb_utf8((uint32_t)scalar->expr.elems[1]->ground.ival, encoded);
        if (len + width + 1u > cap) {
            char *grown = malloc(cap * 2u);
            if (!grown) {
                if (text != stack)
                    free(text);
                return false;
            }
            memcpy(grown, text, len);
            if (text != stack)
                free(text);
            text = grown;
            cap *= 2u;
        }
        memcpy(text + len, encoded, width);
        len += width;
    }
    text[len] = '\0';
    *token = lexical->reference;
    *text_out = atom_string(arena, text);
    if (text != stack)
        free(text);
    return *text_out != NULL;
}

bool cetta_grammar_canonical_bnf_terms_v1(const Atom *document, const Atom *origins,
                                          const Atom *authority, const Atom *trees,
                                          Arena *arena, Atom **out, char *error,
                                          size_t error_size) {
    GcbGrammar grammar;
    GcbLeafContext context;
    CettaGrammarCanonicalTableV1 *table = NULL;
    Atom **terms = NULL;
    uint32_t i;
    bool ok = false;
    if (out)
        *out = NULL;
    if (!trees || trees->kind != ATOM_EXPR || !arena || !out)
        return gcb_error(error, error_size, "canonical bnf: expected the parser's tree tuple");
    if (!gcb_read(&grammar, document, origins, authority, error, error_size))
        return false;
    if (!cetta_grammar_canonical_table_build_v1(grammar.productions, grammar.production_len,
                                                &table, error, error_size))
        goto done;
    terms = malloc(((size_t)trees->expr.len + 1u) * sizeof(*terms));
    if (!terms) {
        gcb_error(error, error_size, "canonical bnf: out of memory");
        goto done;
    }
    context.grammar = &grammar;
    for (i = 0u; i < trees->expr.len; i++) {
        if (!cetta_grammar_canonical_from_cst_v1(table, trees->expr.elems[i], NULL, gcb_leaf,
                                                 &context, arena, &terms[i], error, error_size))
            goto done;
    }
    *out = atom_expr(arena, terms, (CettaExprLen)trees->expr.len);
    ok = true;
done:
    free(terms);
    cetta_grammar_canonical_table_free_v1(table);
    gcb_free(&grammar);
    return ok;
}

bool cetta_grammar_canonical_bnf_language_v1(const Atom *document, const Atom *origins,
                                             const Atom *authority, const char *name,
                                             Arena *arena, Atom **out, char *error,
                                             size_t error_size) {
    CettaGrammarCanonicalTableV1 *table = NULL;
    bool ok;
    if (!cetta_grammar_canonical_bnf_table_v1(document, origins, authority, &table, error,
                                              error_size))
        return false;
    ok = cetta_grammar_canonical_language_v1(table, name, arena, out, error, error_size);
    cetta_grammar_canonical_table_free_v1(table);
    return ok;
}

/* Scannerless grammars write their layout as rules: texts print as written. */
static bool gcb_adjacent(void *context, const char *left, size_t left_len, const char *right,
                         size_t right_len) {
    (void)context;
    (void)left;
    (void)left_len;
    (void)right;
    (void)right_len;
    return false;
}

bool cetta_grammar_canonical_bnf_print_v1(const Atom *document, const Atom *origins,
                                          const Atom *authority, const char *rule,
                                          const Atom *term, Arena *arena, Atom **out,
                                          char *error, size_t error_size) {
    CettaGrammarCanonicalTableV1 *table = NULL;
    char *text = NULL;
    bool ok;
    if (out)
        *out = NULL;
    if (!cetta_grammar_canonical_bnf_table_v1(document, origins, authority, &table, error,
                                              error_size))
        return false;
    ok = cetta_grammar_canonical_print_v1(table, term, symbol_intern_cstr(g_symbols, rule),
                                          gcb_adjacent, NULL, &text, NULL, error, error_size);
    cetta_grammar_canonical_table_free_v1(table);
    if (!ok)
        return false;
    *out = atom_string(arena, text);
    free(text);
    return *out != NULL;
}

bool cetta_grammar_canonical_bnf_collisions_v1(const Atom *document, const Atom *origins,
                                               const Atom *authority, Arena *arena, Atom **out,
                                               char *error, size_t error_size) {
    static const char *const kinds[] = {"sequence", "literal", "token", "path"};
    CettaGrammarCanonicalTableV1 *table = NULL;
    CettaGrammarCanonicalCollisionV1 *collisions = NULL;
    uint32_t len = 0u;
    Atom **items = NULL;
    bool ok = false;
    if (out)
        *out = NULL;
    if (!cetta_grammar_canonical_bnf_table_v1(document, origins, authority, &table, error,
                                              error_size))
        return false;
    if (!cetta_grammar_canonical_collisions_v1(table, &collisions, &len, error, error_size))
        goto done;
    items = calloc(len ? len : 1u, sizeof(*items));
    if (!items)
        goto done;
    for (uint32_t i = 0u; i < len; i++) {
        Atom *parts[5];
        uint32_t count = 0u;
        parts[count++] = atom_symbol(arena, "EBNF:Collision");
        parts[count++] = atom_symbol(arena, kinds[collisions[i].kind]);
        parts[count++] = atom_string(arena, collisions[i].sort);
        parts[count++] = atom_string(arena, collisions[i].first);
        if (collisions[i].second)
            parts[count++] = atom_string(arena, collisions[i].second);
        items[i] = atom_expr(arena, parts, (CettaExprLen)count);
    }
    *out = atom_expr(arena, items, (CettaExprLen)len);
    ok = *out != NULL;
done:
    free(items);
    free(collisions);
    cetta_grammar_canonical_table_free_v1(table);
    return ok;
}

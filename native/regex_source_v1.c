#include "regex_source_v1.h"
#include "regex_class_source_v1.h"
#include "regex_selection_v1.h"
#include "unicode_class_v1.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct { bool dotall, multiline, reverse, insensitive, unicode, extended; } Flags;
/* Labels that steer traversal. A replay shares one atom per distinct label,
 * so each label atom is classified once per lowering. */
typedef enum {
    RX_UNCLASSIFIED = 0, RX_LEAF, RX_PASS, RX_ALT, RX_SEQUENCE, RX_SCOPED_FLAGS,
    RX_LOOKAHEAD, RX_NOT_LOOKAHEAD, RX_LOOKBEHIND, RX_NOT_LOOKBEHIND,
    RX_CAPTURE, RX_GROUP, RX_NAMED, RX_NAMED_PYTHON,
    RX_STAR, RX_STAR_LAZY, RX_PLUS, RX_PLUS_LAZY, RX_OPTIONAL, RX_OPTIONAL_LAZY,
    RX_EXACT, RX_EXACT_LAZY, RX_AT_LEAST, RX_AT_LEAST_LAZY, RX_BOUNDED, RX_BOUNDED_LAZY
} RxKind;
typedef struct { const Atom *label; RxKind kind; } RxKindSlot;
typedef struct {
    Arena *arena; uint64_t work; uint32_t groups; bool bounded, reverse_lookbehind;
    Atom *names, *problem;
    RxKindSlot kinds[64];
} Source;
static const struct { const char *name; RxKind kind; } rx_kinds[] = {
    {"re-src:pattern", RX_PASS}, {"re-src:atom", RX_PASS},
    {"re-src:alt", RX_ALT}, {"re-src:sequence", RX_SEQUENCE}, {"re-src:scoped-flags", RX_SCOPED_FLAGS},
    {"re-src:lookahead", RX_LOOKAHEAD}, {"re-src:not-lookahead", RX_NOT_LOOKAHEAD},
    {"re-src:lookbehind", RX_LOOKBEHIND}, {"re-src:not-lookbehind", RX_NOT_LOOKBEHIND},
    {"re-src:capture", RX_CAPTURE}, {"re-src:group", RX_GROUP},
    {"re-src:named", RX_NAMED}, {"re-src:named-python", RX_NAMED_PYTHON},
    {"re-src:star", RX_STAR}, {"re-src:star-lazy", RX_STAR_LAZY},
    {"re-src:plus", RX_PLUS}, {"re-src:plus-lazy", RX_PLUS_LAZY},
    {"re-src:optional", RX_OPTIONAL}, {"re-src:optional-lazy", RX_OPTIONAL_LAZY},
    {"re-src:exact", RX_EXACT}, {"re-src:exact-lazy", RX_EXACT_LAZY},
    {"re-src:at-least", RX_AT_LEAST}, {"re-src:at-least-lazy", RX_AT_LEAST_LAZY},
    {"re-src:bounded", RX_BOUNDED}, {"re-src:bounded-lazy", RX_BOUNDED_LAZY},
};
static bool take(Source *s) {
    if (!s->bounded) return true;
    if (!s->work) return false;
    --s->work; return true;
}
static void *grow(void *data, size_t *capacity, size_t wanted, size_t width) {
    if (wanted <= *capacity) return data;
    size_t next = *capacity ? *capacity : 32u;
    while (next < wanted) { if (next > SIZE_MAX / 2u) return NULL; next *= 2u; }
    if (next > SIZE_MAX / width) return NULL;
    void *result = realloc(data, next * width);
    if (result) *capacity = next;
    return result;
}
static bool form(Atom *x, const char *name, CettaExprLen len) {
    return x && x->kind == ATOM_EXPR && x->expr.len == len + 1u && atom_is_symbol(x->expr.elems[0], name);
}
/* The CstRuleV1 head by identifier, refreshed when the symbol table is. */
static SymbolId cst_rule_symbol(void) {
    static _Thread_local const SymbolTable *table;
    static _Thread_local uint64_t instance;
    static _Thread_local SymbolId id;
    if (table != g_symbols || (g_symbols && instance != symbol_table_instance_id(g_symbols))) {
        table = g_symbols;
        instance = g_symbols ? symbol_table_instance_id(g_symbols) : 0u;
        id = g_symbols ? symbol_intern_cstr(g_symbols, "CstRuleV1") : SYMBOL_ID_NONE;
    }
    return id;
}
/* While one traversal runs, its syntax is live, so its label atoms are
 * checked once each: a replay shares one atom per distinct label. */
typedef struct { const Atom *text; const char *clean; } LabelSlot;
static _Thread_local LabelSlot *label_cache;
static const char *label(Atom *x) {
    if (!x || x->kind != ATOM_EXPR || x->expr.len < 4u || !x->expr.elems[0] ||
        x->expr.elems[0]->kind != ATOM_SYMBOL || x->expr.elems[0]->sym_id != cst_rule_symbol()) return NULL;
    Atom *s = x->expr.elems[1];
    LabelSlot *slot = label_cache ? &label_cache[((uintptr_t)s >> 4) & 63u] : NULL;
    if (slot && slot->text == s) return slot->clean;
    const char *clean = !s || s->kind != ATOM_GROUNDED || s->ground.gkind != GV_STRING ||
        memchr(s->ground.sval, 0, atom_string_len(s)) ? NULL : s->ground.sval;
    if (slot) *slot = (LabelSlot){s, clean};
    return clean;
}
/* RX_UNCLASSIFIED when x is not labelled syntax; RX_LEAF for every label
 * that is lowered as a leaf form. */
static RxKind kind_of(Source *s, Atom *x) {
    if (!x || x->kind != ATOM_EXPR || x->expr.len < 4u || !x->expr.elems[0] ||
        x->expr.elems[0]->kind != ATOM_SYMBOL || x->expr.elems[0]->sym_id != cst_rule_symbol()) return RX_UNCLASSIFIED;
    const Atom *text = x->expr.elems[1];
    RxKindSlot *slot = &s->kinds[((uintptr_t)text >> 4) & 63u];
    if (slot->label == text) return slot->kind;
    const char *name = label(x);
    RxKind kind = RX_UNCLASSIFIED;
    if (name) {
        kind = RX_LEAF;
        for (size_t i = 0; i < sizeof(rx_kinds) / sizeof(*rx_kinds); ++i)
            if (!strcmp(name, rx_kinds[i].name)) { kind = rx_kinds[i].kind; break; }
    }
    *slot = (RxKindSlot){text, kind};
    return kind;
}
static Atom *child(Atom *x, size_t i) { return x->expr.len > i + 4u ? x->expr.elems[i + 4u] : NULL; }
/* Every head below is a string literal, so its identifier is cached. */
static Atom *node(Source *s, const char *name, Atom *x) { return atom_expr2(s->arena, atom_symbol_literal(s->arena, name), x); }
static Atom *pair(Source *s, const char *name, Atom *x, Atom *y) { return atom_expr3(s->arena, atom_symbol_literal(s->arena, name), x, y); }
static Atom *problem(Source *s, Atom *at, const char *kind, const char *reason) {
    if (!s->problem) s->problem = atom_expr(s->arena, (Atom *[]){atom_symbol_literal(s->arena, kind),
        atom_string(s->arena, reason), label(at) ? at->expr.elems[2] : atom_int(s->arena, 0),
        label(at) ? at->expr.elems[3] : atom_int(s->arena, 0)}, 4u);
    return NULL;
}
static bool scalar(Atom *x, uint32_t *cp) {
    if (label(x)) x = child(x, 0);
    if (!form(x, "cp", 1) || x->expr.elems[1]->kind != ATOM_GROUNDED ||
        x->expr.elems[1]->ground.gkind != GV_INT) return false;
    int64_t n = x->expr.elems[1]->ground.ival;
    if (n < 0 || n > 0x10ffff || (n >= 0xd800 && n <= 0xdfff)) return false;
    *cp = (uint32_t)n; return true;
}
static Atom *range(Source *s, uint32_t low, uint32_t high, Atom *tail) {
    return pair(s, "LCons", pair(s, "re:range", atom_int(s->arena, low), atom_int(s->arena, high)), tail);
}
static Atom *literal(Source *s, uint32_t cp) { return node(s, "re:literal", atom_int(s->arena, cp)); }
static Atom *set_ranges(Source *s, const UnicodeV1Set *set) {
    Atom *out = atom_symbol_literal(s->arena, "LNil");
    for (size_t i = set->len; i; --i)
        out = range(s, set->items[i - 1u].low, set->items[i - 1u].high, out);
    return out;
}
static bool fold_set(Source *s, Atom *at, const Flags *flags_, UnicodeV1Set *set) {
    if (!flags_->insensitive) return true;
    UnicodeV1Outcome status;
    if (flags_->unicode) status = unicode_v1_set_simple_fold(set, set);
    else {
        CettaLpNativeUnicodeRange extra[52]; size_t count = 0;
        for (uint32_t cp = 'A'; cp <= 'Z'; ++cp) {
            if (unicode_v1_set_contains(set, cp)) extra[count++] = (CettaLpNativeUnicodeRange){cp + 32u, cp + 32u};
            if (unicode_v1_set_contains(set, cp + 32u)) extra[count++] = (CettaLpNativeUnicodeRange){cp, cp};
        }
        UnicodeV1Set added = {0};
        status = unicode_v1_set_project(UNICODE_V1_SCALARS, (UnicodeV1Ranges){extra, count}, &added);
        if (status == UNICODE_V1_OK) status = unicode_v1_set_combine(UNICODE_V1_UNION, set, &added, set);
        unicode_v1_set_free(&added);
    }
    if (status == UNICODE_V1_OK) return true;
    problem(s, at, "re:allocation-failure", "case-fold interpretation allocation"); return false;
}
static Atom *literal_flags(Source *s, Atom *at, uint32_t cp, const Flags *flags_) {
    if (!flags_->insensitive) return literal(s, cp);
    CettaLpNativeUnicodeRange point = {cp, cp}; UnicodeV1Set set = {0};
    if (unicode_v1_set_project(UNICODE_V1_SCALARS, (UnicodeV1Ranges){&point, 1u}, &set) != UNICODE_V1_OK)
        return problem(s, at, "re:allocation-failure", "literal class allocation");
    Atom *out = fold_set(s, at, flags_, &set) ? node(s, "re:class", set_ranges(s, &set)) : NULL;
    unicode_v1_set_free(&set); return out;
}
static Atom *class_from_ranges(Source *s, Atom *at, const Flags *flags_, UnicodeV1Ranges ranges_, bool negate) {
    UnicodeV1Set set = {0};
    if (unicode_v1_set_project(UNICODE_V1_SCALARS, ranges_, &set) != UNICODE_V1_OK)
        return problem(s, at, "re:allocation-failure", "character class allocation");
    bool ok = fold_set(s, at, flags_, &set);
    if (ok && negate) ok = unicode_v1_set_complement(&set, &set) == UNICODE_V1_OK;
    Atom *out = ok ? node(s, "re:class", set_ranges(s, &set)) :
        problem(s, at, "re:allocation-failure", "character class interpretation allocation");
    unicode_v1_set_free(&set); return out;
}
static Atom *group_name(Source *s, Atom *tree) {
    size_t len = 0, cap = 32; char *bytes = malloc(cap);
    if (!bytes) return problem(s, tree, "re:allocation-failure", "capture name allocation");
    while (tree && label(tree) && take(s)) {
        uint32_t cp;
        if (!scalar(child(tree, 0), &cp)) { free(bytes); return problem(s, tree, "re:source-fault", "invalid capture name scalar"); }
        if (len + 4u > cap) {
            if (cap > SIZE_MAX / 2u) { free(bytes); return problem(s, tree, "re:allocation-failure", "capture name capacity"); }
            cap *= 2u; void *p = realloc(bytes, cap);
            if (!p) { free(bytes); return problem(s, tree, "re:allocation-failure", "capture name allocation"); }
            bytes = p;
        }
        if (cp < 0x80u) bytes[len++] = (char)cp;
        else if (cp < 0x800u) { bytes[len++] = (char)(0xc0u | (cp >> 6u)); bytes[len++] = (char)(0x80u | (cp & 63u)); }
        else if (cp < 0x10000u) {
            bytes[len++] = (char)(0xe0u | (cp >> 12u)); bytes[len++] = (char)(0x80u | ((cp >> 6u) & 63u)); bytes[len++] = (char)(0x80u | (cp & 63u));
        } else {
            bytes[len++] = (char)(0xf0u | (cp >> 18u)); bytes[len++] = (char)(0x80u | ((cp >> 12u) & 63u));
            bytes[len++] = (char)(0x80u | ((cp >> 6u) & 63u)); bytes[len++] = (char)(0x80u | (cp & 63u));
        }
        tree = child(tree, 1);
    }
    Atom *out = tree ? NULL : atom_string_n(s->arena, bytes, len); free(bytes);
    return out ? out : problem(s, tree, "re:incomplete", "capture name projection work bound");
}
static Atom *property(Source *s, Atom *tree, const Flags *flags_, bool negate) {
    Atom *name = group_name(s, child(tree, 0));
    if (!name) return NULL;
    size_t len = atom_string_len(name);
    if (memchr(name->ground.sval, 0, len)) return problem(s, tree, "re:source-fault", "property name contains NUL");
    char *copy = malloc(len + 1u);
    if (!copy) return problem(s, tree, "re:allocation-failure", "property name allocation");
    memcpy(copy, name->ground.sval, len); copy[len] = 0;
    char *separator = strpbrk(copy, "=:"); UnicodeV1Ranges ranges_; bool found;
    if (separator) { *separator++ = 0; found = unicode_v1_property(copy, separator, &ranges_); }
    else found = unicode_v1_character_class(copy, &ranges_);
    free(copy);
    return found ? class_from_ranges(s, tree, flags_, ranges_, negate) :
        problem(s, tree, "re:interpretation-needed", "Unicode property is not supplied by the selected versioned provider");
}
static Atom *hex_literal(Source *s, Atom *tree, const Flags *flags_) {
    Atom *digits_ = child(tree, 0); uint32_t value = 0; bool nonempty = false;
    while (digits_ && label(digits_)) {
        uint32_t cp, digit;
        if (!take(s)) return problem(s, tree, "re:incomplete", "hex projection work bound");
        if (!scalar(child(digits_, 0), &cp)) return problem(s, tree, "re:source-fault", "malformed hex scalar");
        if (cp >= '0' && cp <= '9') digit = cp - '0';
        else if (cp >= 'A' && cp <= 'F') digit = cp - 'A' + 10u;
        else if (cp >= 'a' && cp <= 'f') digit = cp - 'a' + 10u;
        else return problem(s, tree, "re:source-fault", "expected hex digit");
        if (value > (0x10ffffu - digit) / 16u)
            return problem(s, tree, "re:source-fault", "hex value is outside the Unicode scalar carrier");
        value = value * 16u + digit; nonempty = true; digits_ = child(digits_, 1);
    }
    if (!nonempty || digits_ || (value >= 0xd800u && value <= 0xdfffu))
        return problem(s, tree, "re:source-fault", "hex value is not a Unicode scalar");
    return literal_flags(s, tree, value, flags_);
}
static bool digits(Source *s, Atom *tree, uint32_t *value) {
    uint64_t n = 0;
    while (tree && label(tree)) {
        uint32_t cp;
        if (!take(s)) { problem(s, tree, "re:incomplete", "count projection work bound"); return false; }
        uint32_t digit;
        if (!scalar(child(tree, 0), &cp) || !unicode_v1_decimal_value(cp, &digit, NULL)) return false;
        if (n > (UINT32_MAX - digit) / 10u) {
            problem(s, tree, "re:interpretation-needed", "count needs a realization beyond the native index carrier"); return false;
        }
        n = n * 10u + digit;
        tree = child(tree, 1);
    }
    if (tree) return false;
    *value = (uint32_t)n; return true;
}
static Atom *escape(Source *s, Atom *tree, const Flags *flags_, bool in_class) {
    uint32_t cp;
    if (!scalar(child(tree, 0), &cp)) return problem(s, tree, "re:source-fault", "malformed escape scalar");
    if (cp == 'd' || cp == 'D' || cp == 's' || cp == 'S' || cp == 'w' || cp == 'W') {
        UnicodeV1Ranges ranges_; bool negate = cp == 'D' || cp == 'S' || cp == 'W';
        uint32_t lower_ = cp >= 'A' && cp <= 'Z' ? cp + 32u : cp;
        const char *name = lower_ == 'd' ? "UnicodeDecimal" : lower_ == 's' ? "RegexWhitespace" : "RegexWord";
        static const CettaLpNativeUnicodeRange ascii_digits[] = {{48, 57}},
            ascii_space[] = {{9, 13}, {32, 32}}, ascii_word[] = {{48, 57}, {65, 90}, {95, 95}, {97, 122}};
        if (!flags_->unicode) ranges_ = lower_ == 'd' ? (UnicodeV1Ranges){ascii_digits, 1u} :
            lower_ == 's' ? (UnicodeV1Ranges){ascii_space, 2u} : (UnicodeV1Ranges){ascii_word, 4u};
        else if (!unicode_v1_lexical_class(name, &ranges_))
            return problem(s, tree, "re:interpretation-needed", "Unicode shorthand provider is unresolved");
        return class_from_ranges(s, tree, flags_, ranges_, negate);
    }
    switch (cp) {
    case 'n': return literal(s, '\n'); case 'r': return literal(s, '\r'); case 't': return literal(s, '\t');
    case 'a': return literal(s, 7); case 'f': return literal(s, 12); case 'v': return literal(s, 11);
    case 'A': return node(s, "re:assert", atom_symbol_literal(s->arena, "text-start"));
    case 'z': return node(s, "re:assert", atom_symbol_literal(s->arena, "text-end"));
    case 'b':
        if (in_class) return literal(s, 8u);
        return node(s, "re:assert", atom_symbol_literal(s->arena, flags_->unicode ? "word-boundary" : "ascii-word-boundary"));
    case 'B': return node(s, "re:assert", atom_symbol_literal(s->arena,
        flags_->unicode ? "not-word-boundary" : "not-ascii-word-boundary"));
    default:
        if ((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || (cp >= '0' && cp <= '9'))
            return problem(s, tree, "re:interpretation-needed", "escape needs its declared class, assertion or extended realization");
        return literal_flags(s, tree, cp, flags_);
    }
}
static bool flags(Source *s, Atom *tree, Flags *f) {
    bool enable = true;
    while (tree && label(tree) && take(s)) {
        Atom *item = child(tree, 0); const char *name = label(item);
        if (!name) return false;
        if (!strcmp(name, "re-src:flag-minus")) enable = false;
        else if (!strcmp(name, "re-src:flag-s")) f->dotall = enable;
        else if (!strcmp(name, "re-src:flag-m")) f->multiline = enable;
        else if (!strcmp(name, "re-src:flag-U")) f->reverse = enable;
        else if (!strcmp(name, "re-src:flag-i")) f->insensitive = enable;
        else if (!strcmp(name, "re-src:flag-u")) f->unicode = enable;
        else if (!strcmp(name, "re-src:flag-x")) f->extended = enable;
        else { problem(s, item, "re:interpretation-needed", "flag needs a character or source-lexing interpretation"); return false; }
        tree = child(tree, 1);
    }
    return !tree;
}
static Atom *lower_leaf(Source *s, Atom *tree, Flags *f, uint32_t depth) {
    const char *name = label(tree);
    if (!strcmp(name, "re-src:backref") || !strcmp(name, "re-src:backref-name") ||
        !strcmp(name, "re-src:backref-python")) {
        Atom *identity;
        if (!strcmp(name, "re-src:backref")) {
            uint32_t group;
            if (!digits(s, child(tree, 0), &group))
                return s->problem ? NULL : problem(s, tree, "re:source-fault", "malformed reference identity");
            identity = atom_int(s->arena, group);
        } else {
            Atom *spelling = group_name(s, child(tree, 0));
            if (!spelling) return NULL;
            identity = node(s, "re:reference-name", spelling);
        }
        return pair(s, "re:backref", identity, atom_symbol_literal(s->arena,
            !f->insensitive ? "Exact" : f->unicode ? "UnicodeFold" : "AsciiFold"));
    }
    if (!strcmp(name, "re-src:empty")) return atom_symbol_literal(s->arena, "re:epsilon");
    if (!strcmp(name, "re-src:literal")) {
        uint32_t cp; return scalar(child(tree, 0), &cp) ? literal_flags(s, tree, cp, f) : problem(s, tree, "re:source-fault", "malformed literal");
    }
    if (!strcmp(name, "re-src:escape")) return escape(s, tree, f, false);
    if (!strcmp(name, "re-src:property") || !strcmp(name, "re-src:not-property"))
        return property(s, tree, f, !strcmp(name, "re-src:not-property"));
    if (!strcmp(name, "re-src:hex") || !strcmp(name, "re-src:unicode-hex")) return hex_literal(s, tree, f);
    if (!strcmp(name, "re-src:dot")) {
        if (f->dotall) return atom_symbol_literal(s->arena, "re:any");
        Atom *rs = atom_symbol_literal(s->arena, "LNil");
        rs = range(s, 0xe000, 0x10ffff, rs); rs = range(s, 11, 0xd7ff, rs); rs = range(s, 0, 9, rs);
        return node(s, "re:class", rs);
    }
    if (!strcmp(name, "re-src:start") || !strcmp(name, "re-src:end")) {
        bool start = !strcmp(name, "re-src:start");
        return node(s, "re:assert", atom_symbol_literal(s->arena,
            f->multiline ? (start ? "line-start" : "line-end") : (start ? "text-start" : "text-end")));
    }
    if (!strcmp(name, "re-src:flags")) return flags(s, child(tree, 0), f) ? atom_symbol_literal(s->arena, "re:epsilon") : NULL;
    if (!strcmp(name, "re-src:class") || !strcmp(name, "re-src:class-not")) {
        CettaReClassSourceOptionsV1 options = {.arena = s->arena,
            .insensitive = f->insensitive, .unicode = f->unicode,
            .bounded = s->bounded, .depth_limit = depth, .remaining = &s->work};
        return cetta_re_v1_class_source(&options, tree, &s->problem);
    }
    return problem(s, tree, "re:interpretation-needed", "source form remains data for its declared realization");
}
static Atom *count_value(Source *s, Atom *at, uint32_t n) {
    Atom *value = atom_symbol_literal(s->arena, "re:count-zero");
    while (n--) {
        if (!take(s)) return problem(s, at, "re:incomplete", "count representation work bound");
        value = node(s, "re:count-succ", value);
    }
    return value;
}
static Atom *lower(Source *s, Atom *tree, Flags *f, uint32_t depth) {
    typedef enum { BINARY, GROUP, LOOK, QUANTIFIER, COUNT } Kind;
    typedef struct {
        Kind kind; Atom *tree, *left; const char *head;
        uint32_t level, group; bool right, lazy; Flags saved;
    } Frame;
    Frame *frames = NULL; size_t len = 0, capacity = 0;
    uint32_t level = 1; Atom *value = NULL; bool entering = true;
    for (;;) {
        if (entering) {
            RxKind kind = kind_of(s, tree);
            if (kind == RX_UNCLASSIFIED) { problem(s, tree, "re:source-fault", "expected labelled regex syntax"); goto fail; }
            if ((depth && level > depth) || !take(s)) { problem(s, tree, "re:incomplete", "source projection work bound"); goto fail; }
            if (kind == RX_PASS) { tree = child(tree, 0); ++level; continue; }
            Frame frame = {.tree = tree, .level = level}; bool descend = true; size_t body = 0;
            switch (kind) {
            case RX_ALT: case RX_SEQUENCE:
                frame.kind = BINARY; frame.head = kind == RX_ALT ? "re:alt" : "re:seq";
                break;
            case RX_SCOPED_FLAGS:
                frame.kind = GROUP; frame.saved = *f; body = 1u;
                if (!flags(s, child(tree, 0), f)) goto fail;
                break;
            case RX_LOOKAHEAD: case RX_NOT_LOOKAHEAD: case RX_LOOKBEHIND: case RX_NOT_LOOKBEHIND:
                frame.kind = LOOK; frame.saved = *f;
                frame.head = kind == RX_LOOKAHEAD ? "re:lookahead" : kind == RX_NOT_LOOKAHEAD ? "re:not-lookahead" :
                    kind == RX_LOOKBEHIND ? "re:lookbehind" : "re:not-lookbehind";
                break;
            case RX_CAPTURE: case RX_GROUP: case RX_NAMED: case RX_NAMED_PYTHON: {
                frame.kind = GROUP; frame.saved = *f;
                bool named = kind == RX_NAMED || kind == RX_NAMED_PYTHON;
                body = named ? 1u : 0u;
                if (kind != RX_GROUP) {
                    if (s->groups == UINT32_MAX) { problem(s, tree, "re:incomplete", "capture layout capacity"); goto fail; }
                    frame.group = s->groups++;
                    Atom *spelling = named ? group_name(s, child(tree, 0)) : atom_string(s->arena, "");
                    if (!spelling) goto fail;
                    s->names = pair(s, "LCons", pair(s, "re:group", atom_int(s->arena, frame.group), spelling), s->names);
                }
                break;
            }
            case RX_STAR: case RX_STAR_LAZY: case RX_PLUS: case RX_PLUS_LAZY: case RX_OPTIONAL: case RX_OPTIONAL_LAZY:
                frame.kind = QUANTIFIER;
                frame.head = kind == RX_STAR || kind == RX_STAR_LAZY ? "re:star" :
                    kind == RX_PLUS || kind == RX_PLUS_LAZY ? "re:plus" : "re:optional";
                frame.lazy = kind == RX_STAR_LAZY || kind == RX_PLUS_LAZY || kind == RX_OPTIONAL_LAZY;
                break;
            case RX_EXACT: case RX_EXACT_LAZY: case RX_AT_LEAST: case RX_AT_LEAST_LAZY: case RX_BOUNDED: case RX_BOUNDED_LAZY:
                frame.kind = COUNT;
                frame.head = kind == RX_EXACT || kind == RX_EXACT_LAZY ? "re:repeat-exact" :
                    kind == RX_AT_LEAST || kind == RX_AT_LEAST_LAZY ? "re:repeat-at-least" : "re:repeat-bounded";
                frame.lazy = kind == RX_EXACT_LAZY || kind == RX_AT_LEAST_LAZY || kind == RX_BOUNDED_LAZY;
                break;
            default:
                descend = false; value = lower_leaf(s, tree, f, depth ? depth - level + 1u : 0u);
                if (!value) goto fail;
                break;
            }
            if (descend) {
                void *p = grow(frames, &capacity, len + 1u, sizeof(*frames));
                if (!p) { problem(s, tree, "re:allocation-failure", "source traversal allocation"); goto fail; }
                frames = p; frames[len++] = frame; tree = child(tree, body); ++level; continue;
            }
            entering = false;
        }
        if (!len) { free(frames); return value; }
        Frame *frame = &frames[len - 1u];
        if (frame->kind == BINARY && !frame->right) {
            frame->left = value; frame->right = true;
            tree = child(frame->tree, 1); level = frame->level + 1u; entering = true; continue;
        }
        if (frame->kind == BINARY) value = pair(s, frame->head, frame->left, value);
        else if (frame->kind == GROUP) {
            *f = frame->saved;
            if (frame->group) value = pair(s, "re:capture", atom_int(s->arena, frame->group), value);
        } else if (frame->kind == LOOK) {
            *f = frame->saved;
            value = strstr(frame->head, "behind") ? pair(s, frame->head,
                atom_symbol_literal(s->arena, s->reverse_lookbehind ? "Reverse" : "Forward"), value) :
                node(s, frame->head, value);
        } else {
            Atom *preference = atom_symbol_literal(s->arena, frame->lazy != f->reverse ? "Lazy" : "Greedy");
            if (frame->kind == QUANTIFIER) value = pair(s, frame->head, value, preference);
            else {
                uint32_t low, high = 0;
                bool bounded = !strcmp(frame->head, "re:repeat-bounded");
                if (!digits(s, child(frame->tree, 1), &low) || (bounded && !digits(s, child(frame->tree, 2), &high))) {
                    problem(s, frame->tree, "re:incomplete", "count exceeds this realization's numeric or work capacity"); goto fail;
                }
                if (bounded && high < low) { problem(s, frame->tree, "re:source-fault", "repetition upper bound precedes lower bound"); goto fail; }
                Atom *count = count_value(s, frame->tree, low);
                if (!count) goto fail;
                if (!strcmp(frame->head, "re:repeat-exact")) value = pair(s, frame->head, value, count);
                else if (!bounded) value = atom_expr(s->arena, (Atom *[]){atom_symbol_literal(s->arena, frame->head), value, count, preference}, 4u);
                else {
                    Atom *extra = count_value(s, frame->tree, high - low);
                    if (!extra) goto fail;
                    value = atom_expr(s->arena, (Atom *[]){atom_symbol_literal(s->arena, frame->head), value, count, extra, preference}, 5u);
                }
            }
        }
        --len;
    }
fail:
    free(frames); return NULL;
}
static Atom *lower_source_policy(Arena *arena, Atom *observation, bool reverse_lookbehind,
                                 uint32_t depth, uint64_t work);
Atom *cetta_re_v1_lower_source_policy(Arena *arena, Atom *observation, bool reverse_lookbehind,
                                    uint32_t depth, uint64_t work) {
    LabelSlot cache[64] = {{0}}, *saved = label_cache;
    label_cache = cache;
    Atom *lowered = lower_source_policy(arena, observation, reverse_lookbehind, depth, work);
    label_cache = saved;
    return lowered;
}
static Atom *lower_source_policy(Arena *arena, Atom *observation, bool reverse_lookbehind,
                                 uint32_t depth, uint64_t work) {
    Source s = {.arena = arena, .work = work, .bounded = work != 0u, .groups = 1,
        .reverse_lookbehind = reverse_lookbehind, .names = atom_symbol(arena, "LNil")};
    Atom *original = observation;
    if (form(observation, "ParserWithForestV1", 2u)) observation = observation->expr.elems[1];
    if (!form(observation, "ParserAcceptedV1", 2u)) return node(&s, "re:source-observation", original);
    Atom *trees = observation->expr.elems[1];
    if (!trees || trees->kind != ATOM_EXPR || trees->expr.len != 1u)
        return node(&s, "re:source-observation", original);
    Flags flags_ = {.unicode = true}; Atom *pattern = lower(&s, trees->expr.elems[0], &flags_, depth);
    if (!pattern) {
        if (form(s.problem, "re:interpretation-needed", 3u))
            return atom_expr(arena, (Atom *[]){s.problem->expr.elems[0], s.problem->expr.elems[1],
                s.problem->expr.elems[2], s.problem->expr.elems[3], original}, 5u);
        return s.problem ? s.problem : node(&s, "re:source-observation", original);
    }
    return pair(&s, "re:source-value", pattern, s.names);
}
Atom *cetta_re_v1_lower_source(Arena *arena, Atom *observation, uint32_t depth, uint64_t work) {
    return cetta_re_v1_lower_source_policy(arena, observation, false, depth, work);
}

static bool maximal_numeric_tree(Atom *tree, const uint8_t *decimal, size_t length, bool *allocation) {
    Atom **pending = NULL; size_t len = 0u, capacity = 0u;
    for (;;) {
        const char *name = label(tree);
        if (name) {
            if (!strcmp(name, "re-src:backref")) {
                Atom *stop = tree->expr.elems[3];
                if (stop->kind != ATOM_GROUNDED || stop->ground.gkind != GV_INT || stop->ground.ival < 0 ||
                    (uint64_t)stop->ground.ival > length || decimal[(size_t)stop->ground.ival]) {
                    free(pending); return false;
                }
            }
            size_t children = (size_t)tree->expr.len - 4u;
            if (children > SIZE_MAX - len) { *allocation = true; free(pending); return false; }
            void *next = grow(pending, &capacity, len + children, sizeof(*pending));
            if (children && !next) { *allocation = true; free(pending); return false; }
            pending = next;
            for (CettaExprIndex i = 4u; i < tree->expr.len; ++i) pending[len++] = tree->expr.elems[i];
        }
        if (!len) { free(pending); return true; }
        tree = pending[--len];
    }
}

static Atom *select_numeric_references(Arena *arena, Atom *observation, Atom *source);
Atom *cetta_re_v1_select_numeric_references(Arena *arena, Atom *observation, Atom *source) {
    LabelSlot cache[64] = {{0}}, *saved = label_cache;
    label_cache = cache;
    Atom *selected = select_numeric_references(arena, observation, source);
    label_cache = saved;
    return selected;
}
static Atom *select_numeric_references(Arena *arena, Atom *observation, Atom *source) {
    if (!source || source->kind != ATOM_GROUNDED || source->ground.gkind != GV_STRING) return observation;
    Atom *accepted = form(observation, "ParserWithForestV1", 2u) ? observation->expr.elems[1] : observation;
    if (!form(accepted, "ParserAcceptedV1", 2u)) return observation;
    Atom *trees = accepted->expr.elems[1];
    if (!trees || trees->kind != ATOM_EXPR) return observation;
    size_t byte_len = atom_string_len(source), count = 0u;
    if (byte_len == SIZE_MAX) return observation;
    uint8_t *decimal = calloc(byte_len + 1u, 1u);
    if (!decimal) return atom_expr2(arena, atom_symbol(arena, "re:allocation-failure"),
        atom_string(arena, "numeric reference selection storage"));
    const uint8_t *bytes = (const uint8_t *)source->ground.sval;
    for (size_t i = 0; i < byte_len;) {
        uint32_t cp = bytes[i++], digit; unsigned remaining = 0u;
        if (cp >= 0xf0u) { cp &= 7u; remaining = 3u; }
        else if (cp >= 0xe0u) { cp &= 15u; remaining = 2u; }
        else if (cp >= 0xc0u) { cp &= 31u; remaining = 1u; }
        if (remaining > byte_len - i) { free(decimal); return observation; }
        while (remaining--) cp = (cp << 6u) | (bytes[i++] & 63u);
        decimal[count++] = unicode_v1_decimal_value(cp, &digit, NULL);
    }
    Atom *winner = NULL; size_t eligible = 0u; bool allocation = false;
    for (CettaExprIndex i = 0; i < trees->expr.len; ++i) {
        if (maximal_numeric_tree(trees->expr.elems[i], decimal, count, &allocation)) {
            winner = trees->expr.elems[i]; ++eligible;
        }
        if (allocation) break;
    }
    free(decimal);
    if (allocation) return atom_expr2(arena, atom_symbol(arena, "re:allocation-failure"),
        atom_string(arena, "numeric reference selection traversal"));
    if (eligible != 1u) return observation;
    Atom *chosen = atom_expr3(arena, atom_symbol(arena, "ParserAcceptedV1"),
        atom_expr(arena, &winner, 1u), accepted->expr.elems[2]);
    return accepted == observation ? chosen : atom_expr3(arena,
        atom_symbol(arena, "ParserWithForestV1"), chosen, observation->expr.elems[2]);
}

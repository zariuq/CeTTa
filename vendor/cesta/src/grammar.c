#include "cesta.h"

#include <stdlib.h>
#include <string.h>

void ce_grammar_free(CeGrammar *g) {
    ce_arena_free(&g->arena);
    free(g->symbols); free(g->productions); free(g->lex);
    free(g->nfa); free(g->comments); free(g->entries); free(g->aliases); free(g->policies); free(g->hooks); free(g->deferred_specifiers); free(g->pairs); free(g->spellings); free(g->groupings); free(g->attachments); free(g->layout); free(g->initial_typedefs);
    memset(g, 0, sizeof(*g));
}

int ce_symbol(CeGrammar *g, CeText name, unsigned kind) {
    for (size_t i = 0; i < g->nsymbols; ++i)
        if ((g->symbols[i].kind == kind || (g->symbols[i].kind != CE_LITERAL && kind != CE_LITERAL)) && ce_equal(g->symbols[i].name, name)) return (int)i;
    g->symbols = ce_resize(g->symbols, g->nsymbols + 1, sizeof(*g->symbols));
    int id = (int)g->nsymbols++;
    g->symbols[id] = (CeSymbol){.name=ce_text(&g->arena,name.data,name.len),.kind=kind,.list_element=-1,.sort=id};
    return id;
}

static int literal(CeGrammar *g, CeText text) { return ce_symbol(g, text, CE_LITERAL); }
static bool meta_is(const CeMetaScanner *s, const char *text) {
    return (s->token.kind == CE_META_NAME || s->token.kind == CE_META_PUNCT) && ce_is(s->token.text, text);
}
static bool next(CeMetaScanner *s) { return ce_meta_next(s); }
static bool eat(CeMetaScanner *s, const char *text) {
    if (!meta_is(s, text))
        return ce_error(s->error, s->token.offset, "expected %s; got %.*s", text, (int)s->token.text.len, s->token.text.data);
    return next(s);
}

static CeText append(CeArena *arena, CeText a, const char *middle, CeText b) {
    size_t m = strlen(middle);
    unsigned char *bytes = ce_alloc(arena, a.len + m + b.len + 1);
    memcpy(bytes, a.data, a.len);
    memcpy(bytes + a.len, middle, m);
    memcpy(bytes + a.len + m, b.data, b.len);
    return (CeText){bytes, a.len + m + b.len};
}

static int category(CeGrammar *g, CeMetaScanner *s) {
    if (meta_is(s, "[")) {
        if (!next(s)) return -1;
        int elem = category(g, s);
        if (elem < 0 || !eat(s, "]")) return -1;
        CeText name = append(&g->arena, ce_cstr(&g->arena, "["), "", g->symbols[elem].name);
        name = append(&g->arena, name, "]", ce_cstr(&g->arena, ""));
        int id = ce_symbol(g, name, CE_NONTERMINAL);
        g->symbols[id].list_element = elem;
        return id;
    }
    if (meta_is(s, "(")) {
        if (!next(s)) return -1;
        int a = category(g, s);
        if (a < 0 || !eat(s, "->")) return -1;
        int b = category(g, s);
        if (b < 0 || !eat(s, ")")) return -1;
        CeText name = append(&g->arena, ce_cstr(&g->arena, "("), "", g->symbols[a].name);
        name = append(&g->arena, name, "->", g->symbols[b].name);
        name = append(&g->arena, name, ")", ce_cstr(&g->arena, ""));
        return ce_symbol(g, name, CE_NONTERMINAL);
    }
    if (s->token.kind != CE_META_NAME) {
        ce_error(s->error, s->token.offset, "expected category"); return -1;
    }
    int id = ce_symbol(g, s->token.text, CE_NONTERMINAL);
    if (!next(s)) return -1;
    return id;
}

static void production(CeGrammar *g, CeText label, int lhs, const int *rhs, size_t len, size_t offset, bool internal) {
    g->productions = ce_resize(g->productions, g->nproductions + 1, sizeof(*g->productions));
    CeProduction *p = &g->productions[g->nproductions++];
    *p = (CeProduction){ce_text(&g->arena, label.data, label.len), lhs, ce_alloc(&g->arena, len * sizeof(int)), len, offset, internal};
    if (len) memcpy(p->rhs, rhs, len * sizeof(int));
}

static CeByteSet empty_set(void) { return (CeByteSet){{0,0,0,0}}; }
static void set_byte(CeByteSet *set, unsigned c) { set->word[c / 64] |= UINT64_C(1) << (c % 64); }
static bool has_byte(CeByteSet set, unsigned c) { return (set.word[c / 64] & (UINT64_C(1) << (c % 64))) != 0; }
static CeByteSet range(unsigned first, unsigned last) {
    CeByteSet set = empty_set();
    for (unsigned c = first; c <= last; ++c) set_byte(&set, c);
    return set;
}

static int state(CeGrammar *g, bool epsilon, CeByteSet set, int a, int b) {
    g->nfa = ce_resize(g->nfa, g->nnfa + 1, sizeof(*g->nfa));
    int id = (int)g->nnfa++;
    g->nfa[id] = (CeNfaState){set, {a,b}, epsilon};
    return id;
}
static CeRegex regex_set(CeGrammar *g, CeByteSet set) {
    int end = state(g, true, empty_set(), -1, -1);
    int start = state(g, false, set, end, -1);
    return (CeRegex){start, end, set, true};
}
static CeRegex regex_epsilon(CeGrammar *g) {
    int end = state(g, true, empty_set(), -1, -1);
    int start = state(g, true, empty_set(), end, -1);
    return (CeRegex){start, end, empty_set(), false};
}
static CeRegex regex_seq(CeGrammar *g, CeRegex a, CeRegex b) {
    g->nfa[a.end].out[0] = b.start;
    return (CeRegex){a.start, b.end, empty_set(), false};
}
static CeRegex regex_alt(CeGrammar *g, CeRegex a, CeRegex b) {
    int start = state(g, true, empty_set(), a.start, b.start);
    int end = state(g, true, empty_set(), -1, -1);
    g->nfa[a.end].out[0] = end;
    g->nfa[b.end].out[0] = end;
    CeByteSet bytes = a.bytes;
    for (size_t i = 0; i < 4; ++i) bytes.word[i] |= b.bytes.word[i];
    return (CeRegex){start, end, bytes, a.singleton && b.singleton};
}
static CeRegex regex_repeat(CeGrammar *g, CeRegex a, bool plus, bool optional) {
    int end = state(g, true, empty_set(), -1, -1);
    int start = state(g, true, empty_set(), a.start, plus ? -1 : end);
    g->nfa[a.end].out[0] = optional ? end : a.start;
    g->nfa[a.end].out[1] = optional ? -1 : end;
    return (CeRegex){start, end, empty_set(), false};
}
static CeRegex regex_char(CeGrammar *g, unsigned c) {
    CeByteSet set = empty_set(); set_byte(&set, c); return regex_set(g, set);
}
static CeRegex regex_text(CeGrammar *g, CeText text) {
    CeRegex r = regex_epsilon(g);
    for (size_t i = 0; i < text.len; ++i) r = regex_seq(g, r, regex_char(g, text.data[i]));
    return r;
}

static bool reg_alt(CeGrammar *g, CeMetaScanner *s, CeRegex *out);
static bool reg_atom(CeGrammar *g, CeMetaScanner *s, CeRegex *out) {
    if (meta_is(s, "(")) {
        if (!next(s) || !reg_alt(g, s, out) || !eat(s, ")")) return false;
    } else if (s->token.kind == CE_META_CHAR) {
        if (s->token.text.len != 1) return ce_error(s->error, s->token.offset, "regex character must be one byte");
        *out = regex_char(g, s->token.text.data[0]);
        if (!next(s)) return false;
    } else if (meta_is(s, "[") || meta_is(s, "{")) {
        bool alternatives = meta_is(s, "[");
        if (!next(s) || s->token.kind != CE_META_STRING) return ce_error(s->error, s->token.offset, "expected character string");
        CeText text = s->token.text;
        if (alternatives) {
            CeByteSet set = empty_set();
            for (size_t i = 0; i < text.len; ++i) set_byte(&set, text.data[i]);
            *out = regex_set(g, set);
        } else *out = regex_text(g, text);
        if (!next(s) || !eat(s, alternatives ? "]" : "}")) return false;
    } else if (s->token.kind == CE_META_NAME) {
        CeByteSet set = empty_set();
        if (meta_is(s, "eps")) *out = regex_epsilon(g);
        else {
            if (meta_is(s, "digit")) set = range('0', '9');
            else if (meta_is(s, "upper")) set = range('A', 'Z');
            else if (meta_is(s, "lower")) set = range('a', 'z');
            else if (meta_is(s, "letter")) {
                set = range('a', 'z');
                CeByteSet other = range('A', 'Z');
                for (size_t i = 0; i < 4; ++i) set.word[i] |= other.word[i];
            } else if (meta_is(s, "char")) set = range(0, 255);
            else return ce_error(s->error, s->token.offset, "unknown regex primitive %.*s", (int)s->token.text.len, s->token.text.data);
            *out = regex_set(g, set);
        }
        if (!next(s)) return false;
    } else return ce_error(s->error, s->token.offset, "expected regular expression");
    while (meta_is(s, "*") || meta_is(s, "+") || meta_is(s, "?")) {
        *out = regex_repeat(g, *out, meta_is(s, "+"), meta_is(s, "?"));
        if (!next(s)) return false;
    }
    return true;
}
static bool reg_begins(CeMetaScanner *s) {
    return s->token.kind == CE_META_CHAR || s->token.kind == CE_META_NAME ||
           meta_is(s, "(") || meta_is(s, "[") || meta_is(s, "{");
}
static bool reg_seq(CeGrammar *g, CeMetaScanner *s, CeRegex *out) {
    if (!reg_atom(g, s, out)) return false;
    while (reg_begins(s)) {
        CeRegex right;
        if (!reg_atom(g, s, &right)) return false;
        *out = regex_seq(g, *out, right);
    }
    return true;
}
static bool reg_alt(CeGrammar *g, CeMetaScanner *s, CeRegex *out) {
    if (!reg_seq(g, s, out)) return false;
    while (meta_is(s, "|") || meta_is(s, "-")) {
        bool difference = meta_is(s, "-");
        CeRegex right;
        if (!next(s) || !reg_seq(g, s, &right)) return false;
        if (difference) {
            if (!out->singleton || !right.singleton)
                return ce_error(s->error, s->token.offset, "regex difference requires character sets in this bootstrap reader");
            CeByteSet set = out->bytes;
            for (size_t i = 0; i < 4; ++i) set.word[i] &= ~right.bytes.word[i];
            *out = regex_set(g, set);
        } else *out = regex_alt(g, *out, right);
    }
    return true;
}

static void lex_rule(CeGrammar *g, CeText name, CeRegex regex, bool position) {
    int symbol = ce_symbol(g, name, CE_NONTERMINAL);
    g->symbols[symbol].kind = CE_TOKEN;
    g->lex = ce_resize(g->lex, g->nlex + 1, sizeof(*g->lex));
    g->lex[g->nlex++] = (CeLexRule){ce_text(&g->arena, name.data, name.len), regex.start, regex.end, symbol, position,false};
}

static bool label(CeGrammar *g, CeMetaScanner *s, CeText *out) {
    if (s->token.kind == CE_META_NAME) {
        *out = s->token.text; return next(s);
    }
    const char *name;
    if (meta_is(s, "[")) {
        if (!next(s) || !eat(s, "]")) return false;
        name = "[]";
    } else if (meta_is(s, "(")) {
        if (!next(s) || !eat(s, ":")) return false;
        name = "(:)";
        if (meta_is(s, "[")) {
            if (!next(s) || !eat(s, "]")) return false;
            name = "(:[])";
        }
        if (!eat(s, ")")) return false;
    } else return ce_error(s->error, s->token.offset, "expected production label");
    *out = ce_cstr(&g->arena, name);
    if (meta_is(s, "{")) {
        if (!next(s) || category(g, s) < 0 || !eat(s, "}")) return false;
    }
    return true;
}

static bool rhs(CeGrammar *g, CeMetaScanner *s, int **items, size_t *len) {
    *items = NULL; *len = 0;
    while (s->token.kind != CE_META_END && !meta_is(s, ";") && !meta_is(s, "|")) {
        int symbol;
        if (s->token.kind == CE_META_STRING) {
            if (!s->token.text.len && !g->zero_width_literals) { if (!next(s)) goto failure; continue; }
            symbol = literal(g, s->token.text);
            if (!next(s)) goto failure;
        } else {
            symbol = category(g, s);
            if (symbol < 0) goto failure;
        }
        *items = ce_resize(*items, *len + 1, sizeof(**items));
        (*items)[(*len)++] = symbol;
    }
    return true;
failure:
    free(*items); *items = NULL; return false;
}

bool ce_grammar_read(CeGrammar *g, CeText source, CeError *error) {
    CeMetaScanner s = {&g->arena, source, 0, {{NULL,0},0,0}, error};
    if (!next(&s)) return false;
    while (s.token.kind != CE_META_END) {
        size_t offset = s.token.offset;
        if (meta_is(&s, "comment")) {
            if (!next(&s) || s.token.kind != CE_META_STRING) return ce_error(error, s.token.offset, "expected comment opening");
            CeComment c = {s.token.text, {NULL,0}};
            if (!next(&s)) return false;
            if (s.token.kind == CE_META_STRING) { c.close = s.token.text; if (!next(&s)) return false; }
            g->comments = ce_resize(g->comments, g->ncomments+1, sizeof(*g->comments));
            g->comments[g->ncomments++] = c;
        } else if (meta_is(&s, "token") || meta_is(&s, "position")) {
            bool position = meta_is(&s, "position");
            if (!next(&s) || (position && !eat(&s, "token"))) return false;
            if (s.token.kind != CE_META_NAME) return ce_error(error, s.token.offset, "expected token name");
            CeText name = s.token.text;
            CeRegex re;
            if (!next(&s) || !reg_alt(g, &s, &re)) return false;
            lex_rule(g, name, re, position);
        } else if (meta_is(&s, "separator") || meta_is(&s, "terminator")) {
            bool terminator = meta_is(&s, "terminator");
            if (!next(&s)) return false;
            bool nonempty = meta_is(&s, "nonempty");
            if (nonempty && !next(&s)) return false;
            int elem = category(g, &s);
            if (elem < 0 || s.token.kind != CE_META_STRING) return ce_error(error, s.token.offset, "expected separator string");
            int sep = s.token.text.len ? literal(g, s.token.text) : -1;
            if (!next(&s)) return false;
            CeText name = append(&g->arena, ce_cstr(&g->arena, "["), "", g->symbols[elem].name);
            name = append(&g->arena, name, "]", ce_cstr(&g->arena, ""));
            int list = ce_symbol(g, name, CE_NONTERMINAL);
            g->symbols[list].list_element = elem;
            int one[] = {elem, sep};
            int cons[] = {elem, sep, list};
            if (sep < 0) { one[1] = list; cons[1] = list; }
            /* Left-recursive nonempty separators preserve source and list order. */
            if (nonempty && !terminator) {
                cons[0] = list;
                if (sep >= 0) cons[2] = elem;
                else cons[1] = elem;
            }
            if (!nonempty) production(g, ce_cstr(&g->arena, "[]"), list, NULL, 0, offset, false);
            if (!terminator || nonempty)
                production(g, ce_cstr(&g->arena, "(:[])"), list, one, terminator && sep >= 0 ? 2 : 1, offset, false);
            production(g, ce_cstr(&g->arena, "(:)"), list, cons, sep >= 0 ? 3 : 2, offset, false);
        } else if (meta_is(&s, "coercions")) {
            if (!next(&s) || s.token.kind != CE_META_NAME) return ce_error(error, s.token.offset, "expected coercion category");
            CeText name = s.token.text;
            int base = ce_symbol(g, name, CE_NONTERMINAL);
            if (!next(&s) || s.token.kind != CE_META_NAME) return ce_error(error, s.token.offset, "expected coercion level count");
            char *end = NULL;
            unsigned long levels = strtoul((const char *)s.token.text.data, &end, 10);
            if (*end || levels > 1000) return ce_error(error, s.token.offset, "invalid coercion level count");
            int previous = base;
            for (unsigned long i = 1; i <= levels; ++i) {
                char digits[32]; snprintf(digits, sizeof(digits), "%lu", i);
                CeText level = append(&g->arena, name, digits, ce_cstr(&g->arena, ""));
                int current = ce_symbol(g, level, CE_NONTERMINAL);
                production(g, ce_cstr(&g->arena, "_"), previous, &current, 1, offset, false);
                previous = current;
            }
            int parens[] = {literal(g, ce_cstr(&g->arena, "(")), base, literal(g, ce_cstr(&g->arena, ")"))};
            production(g, ce_cstr(&g->arena, "_"), previous, parens, 3, offset, false);
            if (!next(&s)) return false;
        } else if (meta_is(&s, "entrypoints")) {
            if (!next(&s)) return false;
            for (;;) {
                int entry = category(g, &s);
                if (entry < 0) return false;
                g->entries = ce_resize(g->entries, g->nentries+1, sizeof(*g->entries));
                g->entries[g->nentries++] = entry;
                if (!meta_is(&s, ",")) break;
                if (!next(&s)) return false;
            }
        } else if (meta_is(&s, "layout")) {
            return ce_error(error, offset, "layout pragmas require an explicit layout profile");
        } else if (meta_is(&s, "rules")) {
            if (!next(&s)) return false;
            int lhs = category(g, &s);
            if (lhs < 0 || !eat(&s, "::=")) return false;
            size_t number = 1;
            for (;;) {
                int *items; size_t len;
                if (!rhs(g, &s, &items, &len)) return false;
                char digits[32]; snprintf(digits, sizeof(digits), "_%zu", number++);
                CeText name = append(&g->arena, g->symbols[lhs].name, digits, ce_cstr(&g->arena, ""));
                production(g, name, lhs, items, len, offset, false);
                free(items);
                if (!meta_is(&s, "|")) break;
                if (!next(&s)) return false;
            }
        } else {
            bool internal = meta_is(&s, "internal");
            if (internal && !next(&s)) return false;
            CeText name;
            if (!label(g, &s, &name) || !eat(&s, ".")) return false;
            int lhs = category(g, &s);
            if (lhs < 0 || !eat(&s, "::=")) return false;
            int *items; size_t len;
            if (!rhs(g, &s, &items, &len)) return false;
            production(g, name, lhs, items, len, offset, internal);
            free(items);
        }
        if (!eat(&s, ";")) return false;
    }
    return true;
}

static CeRegex quoted(CeGrammar *g, unsigned quote) {
    CeByteSet safe = range(32,255);
    safe.word[quote/64] &= ~(UINT64_C(1) << (quote%64));
    safe.word['\\'/64] &= ~(UINT64_C(1) << ('\\'%64));
    CeRegex escaped = regex_seq(g, regex_char(g, '\\'), regex_set(g, range(0,255)));
    CeRegex body = regex_alt(g, regex_set(g, safe), escaped);
    body = regex_repeat(g, body, false, false);
    return regex_seq(g, regex_seq(g, regex_char(g, quote), body), regex_char(g, quote));
}

bool ce_grammar_finish_signature(CeGrammar *g, const CeGrammar *external, CeError *error) {
    if (!g->nsymbols) return ce_error(error,0,"empty grammar");
    for (size_t i = 0; i < g->nsymbols; ++i) {
        if (g->symbols[i].kind != CE_NONTERMINAL) continue;
        CeText name = g->symbols[i].name;
        CeRegex regex;
        if (ce_is(name, "Ident")) {
            CeByteSet first = range('a','z'), upper = range('A','Z');
            for (size_t j = 0; j < 4; ++j) first.word[j] |= upper.word[j];
            set_byte(&first, '_');
            CeByteSet rest = first, digits = range('0','9');
            for (size_t j = 0; j < 4; ++j) rest.word[j] |= digits.word[j];
            set_byte(&rest, '\'');
            regex = regex_seq(g, regex_set(g, first), regex_repeat(g, regex_set(g, rest), false, false));
        } else if (ce_is(name, "Integer")) regex = regex_repeat(g, regex_set(g, range('0','9')), true, false);
        else if (ce_is(name, "Double")) {
            regex = regex_seq(g, regex_repeat(g, regex_set(g, range('0','9')), true, false), regex_char(g, '.'));
            regex = regex_seq(g, regex, regex_repeat(g, regex_set(g, range('0','9')), true, false));
            CeRegex exponent = regex_alt(g, regex_char(g, 'e'), regex_char(g, 'E'));
            CeRegex sign = regex_alt(g, regex_char(g, '+'), regex_char(g, '-'));
            exponent = regex_seq(g, exponent, regex_repeat(g, sign, false, true));
            exponent = regex_seq(g, exponent, regex_repeat(g, regex_set(g, range('0','9')), true, false));
            regex = regex_seq(g, regex, regex_repeat(g, exponent, false, true));
        } else if (ce_is(name, "String")) regex = quoted(g, '"');
        else if (ce_is(name, "Char")) regex = quoted(g, '\'');
        else continue;
        lex_rule(g, name, regex, false);
    }
    for (size_t i = 0; i < g->nproductions; ++i) {
        CeProduction *p = &g->productions[i];
        if (g->symbols[p->lhs].kind != CE_NONTERMINAL)
            return ce_error(error, p->offset, "token category cannot have productions");
        if (ce_is(p->label,"List") || ce_is(p->label,"Symbol"))
            return ce_error(error,p->offset,"constructor name is reserved by the S-expression codec");
        if (ce_is(p->label, "_")) {
            int child = -1;
            size_t arity = 0;
            for (size_t j = 0; j < p->len; ++j) if (g->symbols[p->rhs[j]].kind != CE_LITERAL) { child = p->rhs[j]; ++arity; }
            if (arity != 1) return ce_error(error,p->offset,"transparent production requires exactly one tree argument");
            int a = g->symbols[p->lhs].sort, b = g->symbols[child].sort;
            for (size_t j = 0; j < g->nsymbols; ++j) if (g->symbols[j].sort == b) g->symbols[j].sort = a;
        }
        for (size_t j = i+1; j < g->nproductions; ++j) {
            CeProduction *q = &g->productions[j];
            if (ce_is(p->label, "_") || ce_is(p->label, "[]") || ce_is(p->label, "(:)") || ce_is(p->label, "(:[])")) continue;
            if (ce_equal(p->label, q->label)) return ce_error(error, q->offset, "duplicate constructor label %.*s", (int)p->label.len, p->label.data);
        }
    }
    for (size_t i = 0; i < g->nproductions; ++i) {
        CeProduction *p = &g->productions[i];
        int element = g->symbols[p->lhs].list_element;
        if (element < 0 || ce_is(p->label,"_")) continue;
        int args[2]; size_t arity = 0;
        for (size_t j = 0; j < p->len; ++j) if (g->symbols[p->rhs[j]].kind != CE_LITERAL) {
            if (arity < 2) args[arity] = p->rhs[j];
            ++arity;
        }
        bool valid = false;
        if (ce_is(p->label,"[]")) valid = arity == 0;
        if (ce_is(p->label,"(:[])"))
            valid = arity == 1 && g->symbols[args[0]].sort == g->symbols[element].sort;
        if (ce_is(p->label,"(:)") && arity == 2) {
            bool forward = g->symbols[args[0]].sort == g->symbols[element].sort && args[1] == p->lhs;
            bool backward = args[0] == p->lhs && g->symbols[args[1]].sort == g->symbols[element].sort;
            valid = forward || backward;
        }
        if (!valid) return ce_error(error,p->offset,"list production must have the declared empty, singleton or cons signature");
    }
    for (size_t i = 0; i < g->nsymbols; ++i) {
        for (size_t j = i+1; j < g->nsymbols; ++j) {
            if (g->symbols[i].sort != g->symbols[j].sort) continue;
            if (g->symbols[i].kind == CE_TOKEN && g->symbols[j].kind == CE_TOKEN)
                return ce_error(error,0,"transparent sort quotient merges two different token classes");
            if (g->symbols[i].list_element >= 0 && g->symbols[j].list_element >= 0 &&
                g->symbols[g->symbols[i].list_element].sort != g->symbols[g->symbols[j].list_element].sort)
                return ce_error(error,0,"transparent sort quotient merges incompatible list shapes");
        }
        if (g->symbols[i].kind != CE_TOKEN && g->symbols[i].list_element < 0) continue;
        for (size_t j = 0; j < g->nproductions; ++j) {
            CeProduction *p = &g->productions[j];
            if (g->symbols[p->lhs].sort != g->symbols[i].sort || ce_is(p->label,"_")) continue;
            if (g->symbols[i].kind == CE_TOKEN || g->symbols[p->lhs].list_element < 0)
                return ce_error(error,p->offset,"logical token/list sort also declares an ordinary constructor");
        }
    }
    for (size_t i = 0; i < g->nsymbols; ++i) {
        if (g->symbols[i].kind != CE_NONTERMINAL) continue;
        bool found = false;
        for (size_t j = 0; j < g->nproductions; ++j) if (g->productions[j].lhs == (int)i) found = true;
        /* An interpreted signature may refer to sorts supplied by its target.
           This does not manufacture source productions for those sorts. */
        if (external && !found)
            for (size_t j = 0; j < external->nsymbols; ++j)
                found |= external->symbols[j].kind != CE_LITERAL &&
                         ce_equal(g->symbols[i].name,external->symbols[j].name);
        if (!found) return ce_error(error, 0, "undefined category %.*s", (int)g->symbols[i].name.len, g->symbols[i].name.data);
    }
    for (size_t i = 0; i < g->nlex; ++i) {
        size_t len;
        if (ce_regex_match(g, g->lex[i].start, g->lex[i].end, (CeText){NULL,0}, 0, &len))
            return ce_error(error, 0, "nullable token %.*s", (int)g->lex[i].name.len, g->lex[i].name.data);
    }
    bool changed;
    do {
        changed = false;
        for (size_t i = 0; i < g->nproductions; ++i) {
            CeProduction *p = &g->productions[i];
            bool nullable = true;
            for (size_t j = 0; j < p->len; ++j) nullable &= g->symbols[p->rhs[j]].nullable;
            if (nullable && !g->symbols[p->lhs].nullable) { g->symbols[p->lhs].nullable = true; changed = true; }
        }
    } while (changed);
    return true;
}

bool ce_grammar_finish(CeGrammar *g, CeError *error) {
    return ce_grammar_finish_signature(g,NULL,error);
}

static void closure(const CeGrammar *g, unsigned char *set, int start, int *stack) {
    if (set[start]) return;
    size_t n = 0;
    set[start] = 1; stack[n++] = start;
    while (n) {
        int i = stack[--n];
        if (!g->nfa[i].epsilon) continue;
        for (size_t j = 0; j < 2; ++j) {
            int out = g->nfa[i].out[j];
            if (out >= 0 && !set[out]) { set[out] = 1; stack[n++] = out; }
        }
    }
}

bool ce_regex_match(const CeGrammar *g, int start, int end, CeText source, size_t offset, size_t *length) {
    unsigned char *a = calloc(g->nnfa, 1), *b = calloc(g->nnfa, 1);
    int *stack = ce_resize(NULL, g->nnfa, sizeof(int));
    if (!a || !b) { fputs("cesta: out of memory\n", stderr); exit(2); }
    closure(g, a, start, stack);
    bool matched = a[end] != 0;
    *length = 0;
    for (size_t pos = offset; pos < source.len; ++pos) {
        memset(b, 0, g->nnfa);
        bool any = false;
        for (size_t i = 0; i < g->nnfa; ++i) {
            if (a[i] && !g->nfa[i].epsilon && has_byte(g->nfa[i].bytes, source.data[pos])) {
                int out = g->nfa[i].out[0];
                if (out >= 0) { closure(g, b, out, stack); any = true; }
            }
        }
        if (!any) break;
        unsigned char *tmp = a; a = b; b = tmp;
        if (a[end]) { matched = true; *length = pos - offset + 1; }
    }
    free(a); free(b); free(stack);
    return matched;
}

static bool begins(CeText source, size_t offset, CeText needle) {
    return needle.len <= source.len - offset && (!needle.len || !memcmp(source.data + offset, needle.data, needle.len));
}

bool ce_lex(const CeGrammar *g, CeText source, CeTokens *out, CeError *error) {
    memset(out, 0, sizeof(*out));
    for (size_t pos = 0; pos < source.len;) {
        unsigned c = source.data[pos];
        if (c == ' ' || (c >= 9 && c <= 13)) { ++pos; continue; }
        bool comment = false;
        for (size_t i = 0; i < g->ncomments; ++i) {
            CeComment def = g->comments[i];
            if (!def.open.len || !begins(source, pos, def.open)) continue;
            size_t begin = pos;
            pos += def.open.len;
            if (!def.close.len) while (pos < source.len && source.data[pos] != '\n') ++pos;
            else {
                while (pos < source.len && !begins(source, pos, def.close)) ++pos;
                if (pos == source.len) { ce_tokens_free(out); return ce_error(error, begin, "unterminated source comment"); }
                pos += def.close.len;
            }
            comment = true; break;
        }
        if (comment) continue;
        size_t best = 0;
        int symbol = -1;
        for (size_t i = 0; i < g->nsymbols; ++i) {
            if (g->symbols[i].kind == CE_LITERAL && !g->symbols[i].virtual_token && !g->symbols[i].identifier_spelling && g->symbols[i].name.len > best && begins(source, pos, g->symbols[i].name)) {
                symbol = (int)i; best = g->symbols[i].name.len;
            }
        }
        for (size_t i = 0; i < g->nlex; ++i) {
            size_t length;
            if (ce_regex_match(g, g->lex[i].start, g->lex[i].end, source, pos, &length) && length > best) {
                symbol = g->lex[i].symbol; best = length;
            }
        }
        if (symbol < 0 || !best) { ce_tokens_free(out); return ce_error(error, pos, "no lexical rule accepts byte 0x%02x", source.data[pos]); }
        out->data = ce_resize(out->data, out->len+1, sizeof(*out->data));
        out->data[out->len++] = (CeToken){symbol, {source.data+pos,best}, pos};
        pos += best;
    }
    ce_lex_rewrite(g,out); return true;
}
void ce_tokens_free(CeTokens *tokens) { free(tokens->data); memset(tokens, 0, sizeof(*tokens)); }
void ce_lex_rewrite(const CeGrammar *g, CeTokens *tokens) {
    int previous = -1;
    for (size_t i = 0; i < tokens->len; ++i) {
        int physical = tokens->data[i].symbol;
        for (size_t j = 0; j < g->nspellings; ++j)
            if (g->spellings[j].physical == physical) physical = g->spellings[j].target;
        tokens->data[i].symbol = physical;
        for (size_t j = 0; j < g->npairs; ++j)
            if (g->pairs[j].previous == previous && g->pairs[j].physical == physical)
                tokens->data[i].symbol = g->pairs[j].target;
        previous = physical;
    }
}

static int profile_terminal(CeGrammar *g, CeMetaScanner *s) {
    if (s->token.kind != CE_META_STRING) { ce_error(s->error,s->token.offset,"expected terminal spelling string"); return -2; }
    if (ce_is(s->token.text,"EOF")) return -1;
    for (size_t i = 0; i < g->nsymbols; ++i)
        if (g->symbols[i].kind != CE_NONTERMINAL && ce_equal(g->symbols[i].name,s->token.text)) return (int)i;
    ce_error(s->error,s->token.offset,"profile terminal is absent from the grammar"); return -2;
}

static bool profile_rule(CeGrammar *g, CeMetaScanner *s, int *out) {
    if (s->token.kind != CE_META_STRING)
        return ce_error(s->error,s->token.offset,"expected reduction label string");
    CeText label = s->token.text;
    if (!next(s) || s->token.kind != CE_META_STRING) return ce_error(s->error,s->token.offset,"expected reduction result-category string");
    int found = -1;
    for (size_t i = 0; i < g->nproductions; ++i)
        if (ce_equal(g->productions[i].label,label) && ce_equal(g->symbols[g->productions[i].lhs].name,s->token.text)) {
            if (found >= 0) return ce_error(s->error,s->token.offset,"reduction selector is not unique");
            found = (int)i;
        }
    if (found < 0) return ce_error(s->error,s->token.offset,"profile reduction is absent from the grammar");
    *out = found; return next(s);
}

static bool layout_token(CeGrammar *g, CeMetaScanner *s, unsigned flags) {
    int symbol = profile_terminal(g,s);
    if (symbol < 0 || g->symbols[symbol].kind != CE_LITERAL)
        return ce_error(s->error,s->token.offset,"layout requires a grammar literal");
    size_t found = 0;
    while (found < g->nlayout && g->layout[found].symbol != symbol) ++found;
    if (found == g->nlayout) {
        g->layout = ce_resize(g->layout,g->nlayout+1,sizeof(*g->layout));
        g->layout[g->nlayout++] = (CeLayoutToken){symbol,0};
    }
    unsigned roles = CE_LAYOUT_BLOCK_OPEN | CE_LAYOUT_BLOCK_CLOSE | CE_LAYOUT_GROUP_OPEN | CE_LAYOUT_GROUP_CLOSE;
    if ((g->layout[found].flags & roles) && (flags & roles))
        return ce_error(s->error,s->token.offset,"a layout delimiter cannot have two roles");
    g->layout[found].flags |= flags;
    return next(s);
}
static bool profile_action(CeGrammar *g, CeMetaScanner *s, CeAction *out) {
    if (meta_is(s,"shift")) { *out = (CeAction){CE_LR_SHIFT,-1}; return next(s); }
    int found;
    if (!eat(s,"reduce") || !profile_rule(g,s,&found)) return false;
    *out = (CeAction){CE_LR_REDUCE,found}; return true;
}

static unsigned scope_action(CeText name) {
    static const char *names[] = {"", "Push", "Pop", "PrototypeOpen", "PrototypeClose",
        "ContextKeep", "DeclarationOpen", "DeclarationClose", "BindDeclarator",
        "BindParameter", "BindEnumerator", "FunctionOpen", "FunctionClose", "RecordFunction"};
    for (unsigned i = 1; i < sizeof(names)/sizeof(*names); ++i) if (ce_is(name,names[i])) return i;
    return 0;
}

static int scope_auxiliary(CeGrammar *g, unsigned action, CeText name, CeError *error) {
    for (size_t i = 0; i < g->nhooks; ++i)
        if (!g->hooks[i].after_reduce && g->hooks[i].action == action)
            return g->productions[g->hooks[i].production].lhs;
    char auxiliary[96]; snprintf(auxiliary,sizeof(auxiliary),"Scope%.*s",(int)name.len,name.data);
    for (size_t i = 0; i < g->nsymbols; ++i)
        if (ce_is(g->symbols[i].name,auxiliary)) { ce_error(error,0,"scope auxiliary collides with authored category"); return -1; }
    int symbol = ce_symbol(g,ce_cstr(&g->arena,auxiliary),CE_NONTERMINAL);
    g->symbols[symbol].erased = true; g->symbols[symbol].nullable = true;
    int production = (int)g->nproductions;
    g->productions = ce_resize(g->productions,g->nproductions+1,sizeof(*g->productions));
    g->productions[g->nproductions++] = (CeProduction){ce_cstr(&g->arena,auxiliary),symbol,NULL,0,0,false};
    g->hooks = ce_resize(g->hooks,g->nhooks+1,sizeof(*g->hooks));
    g->hooks[g->nhooks++] = (CeScopeHook){production,action,false};
    return symbol;
}

typedef struct { int production, symbol; size_t slot; } PendingHook;

static void insert_hooks(CeGrammar *g, const PendingHook *hooks, size_t len) {
    for (size_t i = 0; i < g->nproductions; ++i) {
        size_t count = 0;
        for (size_t j = 0; j < len; ++j) if (hooks[j].production == (int)i) ++count;
        if (!count) continue;
        CeProduction *p = &g->productions[i];
        int *rhs = ce_alloc(&g->arena,(p->len+count)*sizeof(*rhs)); size_t at = 0;
        for (size_t slot = 0; slot <= p->len; ++slot) {
            for (size_t j = 0; j < len; ++j)
                if (hooks[j].production == (int)i && hooks[j].slot == slot) rhs[at++] = hooks[j].symbol;
            if (slot < p->len) rhs[at++] = p->rhs[slot];
        }
        p->rhs = rhs; p->len = at;
    }
}

static bool profile_read_at(CeGrammar *g, CeText source, const char *directory,
                            const CeProfileSource *bundle, size_t bundle_len,
                            unsigned depth, CeError *error) {
    if (depth > 16) return ce_error(error,0,"profile inclusion nesting exceeds 16");
    CeMetaScanner s = {&g->arena, source, 0, {{NULL,0},0,0}, error};
    if (!next(&s) || !eat(&s, "profile") || s.token.kind != CE_META_NAME)
        return ce_error(error, s.token.offset, "expected parser profile name");
    CeText profile_name = s.token.text;
    g->name = profile_name;
    if (!next(&s) || !eat(&s, ";")) return false;
    PendingHook *hooks = NULL; size_t nhooks = 0;
    while (s.token.kind != CE_META_END) {
        if (meta_is(&s,"include")) {
            if ((!directory && !bundle) || !next(&s) || s.token.kind != CE_META_STRING || !s.token.text.len)
                return ce_error(error,s.token.offset,"profile include requires a file or bundle resolver and a relative path");
            CeText relative = s.token.text;
            if (relative.data[0] == '/' || memchr(relative.data,0,relative.len))
                return ce_error(error,s.token.offset,"profile include must name a relative non-NUL path");
            CeText included = {NULL,0}; const char *nested = NULL;
            if (bundle) {
                for (size_t i = 0; i < bundle_len; ++i)
                    if (ce_equal(relative,bundle[i].name)) { included = bundle[i].source; break; }
                if (!included.data) return ce_error(error,s.token.offset,"included profile is absent from the bundle");
            } else {
                size_t base = strlen(directory);
                char *path = ce_alloc(&g->arena,base+relative.len+2);
                memcpy(path,directory,base); path[base] = '/';
                memcpy(path+base+1,relative.data,relative.len); path[base+1+relative.len] = 0;
                char *parent = ce_alloc(&g->arena,strlen(path)+1); strcpy(parent,path);
                char *slash = strrchr(parent,'/'); *slash = 0; nested = parent;
                if (!ce_read_file(&g->arena,path,&included,error)) return false;
            }
            if (!profile_read_at(g,included,nested,bundle,bundle_len,depth+1,error) || !next(&s) || !eat(&s,";")) return false;
            continue;
        }
        if (meta_is(&s,"initial")) {
            if (!next(&s) || !eat(&s,"typedef") || s.token.kind != CE_META_STRING || !s.token.text.len)
                return ce_error(error,s.token.offset,"expected initial typedef spelling");
            g->initial_typedefs = ce_resize(g->initial_typedefs,g->ninitial_typedefs+1,sizeof(*g->initial_typedefs));
            g->initial_typedefs[g->ninitial_typedefs++] = s.token.text;
            if (!next(&s) || !eat(&s,";")) return false;
            continue;
        }
        if (meta_is(&s,"lexical")) {
            if (!next(&s)) return false;
            if (meta_is(&s,"identifier")) {
                if (!g->c_scope || !next(&s) || s.token.kind != CE_META_STRING || !s.token.text.len)
                    return ce_error(error,s.token.offset,"lexical identifier requires a classified identifier and a literal spelling");
                int found = -1;
                for (size_t i = 0; i < g->nsymbols; ++i)
                    if (g->symbols[i].kind == CE_LITERAL && ce_equal(g->symbols[i].name,s.token.text)) found = (int)i;
                if (found < 0 || g->symbols[found].virtual_token || g->symbols[found].identifier_spelling)
                    return ce_error(error,s.token.offset,"lexical identifier must name one physical reserved terminal");
                bool identifier = false;
                for (size_t i = 0; i < g->nlex; ++i) if (g->lex[i].symbol == g->name_token) {
                    size_t length = 0;
                    identifier |= ce_regex_match(g,g->lex[i].start,g->lex[i].end,s.token.text,0,&length) && length == s.token.text.len;
                }
                if (!identifier) return ce_error(error,s.token.offset,"lexical identifier spelling does not match the classified identifier token");
                g->symbols[found].identifier_spelling = true;
                if (!next(&s) || !eat(&s,";")) return false;
                continue;
            }
            if (meta_is(&s,"spelling")) {
                if (!next(&s) || s.token.kind != CE_META_STRING || !s.token.text.len)
                    return ce_error(error,s.token.offset,"lexical spelling requires a nonempty literal");
                int physical = literal(g,s.token.text);
                if (!next(&s) || !eat(&s,"as") || s.token.kind != CE_META_STRING || !s.token.text.len)
                    return ce_error(error,s.token.offset,"lexical spelling requires a nonempty target literal");
                int target = literal(g,s.token.text);
                if (target == physical) return ce_error(error,s.token.offset,"lexical spelling must change the terminal");
                for (size_t i = 0; i < g->nspellings; ++i)
                    if (g->spellings[i].physical == physical || g->spellings[i].target == physical || g->spellings[i].physical == target)
                        return ce_error(error,s.token.offset,"lexical spelling policies overlap or form a chain");
                g->spellings = ce_resize(g->spellings,g->nspellings+1,sizeof(*g->spellings));
                g->spellings[g->nspellings++] = (CeLexSpelling){physical,target};
                if (!next(&s) || !eat(&s,";")) return false;
                continue;
            }
            if (!eat(&s,"pair")) return false;
            int previous = profile_terminal(g,&s);
            if (previous < 0 || !next(&s)) return false;
            int physical = profile_terminal(g,&s);
            if (physical < 0 || !next(&s) || !eat(&s,"as")) return false;
            int target = profile_terminal(g,&s);
            if (target < 0 || target == physical || !next(&s) || !eat(&s,";")) return false;
            if (g->symbols[physical].kind != CE_LITERAL || g->symbols[target].kind != CE_LITERAL)
                return ce_error(error,s.token.offset,"lexical pair requires literal physical and target terminals");
            for (size_t i = 0; i < g->npairs; ++i)
                if (g->pairs[i].previous == previous && g->pairs[i].physical == physical)
                    return ce_error(error,s.token.offset,"overlapping lexical pair policies");
            g->symbols[target].virtual_token = true; g->symbols[target].surface = g->symbols[physical].name;
            g->pairs = ce_resize(g->pairs,g->npairs+1,sizeof(*g->pairs));
            g->pairs[g->npairs++] = (CeLexPair){previous,physical,target};
            continue;
        }
        if (meta_is(&s,"layout")) {
            if (!next(&s)) return false;
            if (meta_is(&s,"attach")) {
                if (!next(&s)) return false;
                int symbol = profile_terminal(g,&s);
                if (symbol < 0 || g->symbols[symbol].kind != CE_LITERAL)
                    return ce_error(error,s.token.offset,"layout attachment requires a grammar literal");
                if (!next(&s) || !meta_is(&s,"in") || !next(&s))
                    return ce_error(error,s.token.offset,"expected in and one or more reductions");
                do {
                    int production; if (!profile_rule(g,&s,&production)) return false;
                    const CeProduction *p = &g->productions[production]; bool present = false;
                    for (size_t i = 0; i < p->len; ++i) present |= p->rhs[i] == symbol;
                    if (!present) return ce_error(error,s.token.offset,"attached literal is absent from the reduction");
                    g->attachments = ce_resize(g->attachments,g->nattachments+1,sizeof(*g->attachments));
                    g->attachments[g->nattachments].symbol = symbol;
                    g->attachments[g->nattachments++].production = production;
                } while (s.token.kind == CE_META_STRING);
                if (!eat(&s,";")) return false;
                continue;
            }
            if (meta_is(&s,"protect")) {
                int production, wrapper, parent = -1; size_t argument = SIZE_MAX;
                if (!next(&s) || !profile_rule(g,&s,&production) || !eat(&s,"with") || !profile_rule(g,&s,&wrapper)) return false;
                if (meta_is(&s,"inside")) {
                    if (!next(&s) || !profile_rule(g,&s,&parent)) return false;
                    if (meta_is(&s,"argument")) {
                        if (!next(&s) || s.token.kind != CE_META_NAME || !s.token.text.len)
                            return ce_error(error,s.token.offset,"expected layout argument position");
                        argument = 0; const CeProduction *context = &g->productions[parent]; size_t arity = 0;
                        for (size_t i = 0; i < context->len; ++i)
                            arity += g->symbols[context->rhs[i]].kind != CE_LITERAL && !g->symbols[context->rhs[i]].erased;
                        for (size_t i = 0; i < s.token.text.len; ++i) {
                            unsigned c = s.token.text.data[i];
                            if (c < '0' || c > '9' || argument > arity)
                                return ce_error(error,s.token.offset,"invalid layout argument position");
                            argument = argument*10+c-'0';
                        }
                        if (argument >= arity || !next(&s)) return ce_error(error,s.token.offset,"layout argument outside production");
                    }
                }
                if (!eat(&s,";")) return false;
                const CeProduction *p = &g->productions[wrapper]; size_t arguments = 0; int inside = -1;
                for (size_t i = 0; i < p->len; ++i)
                    if (g->symbols[p->rhs[i]].kind != CE_LITERAL && !g->symbols[p->rhs[i]].erased) { ++arguments; inside = p->rhs[i]; }
                if (!ce_is(p->label,"_") || arguments != 1 || p->len < 2 ||
                    g->symbols[p->lhs].sort != g->symbols[g->productions[production].lhs].sort ||
                    g->symbols[inside].sort != g->symbols[p->lhs].sort)
                    return ce_error(error,s.token.offset,"layout protection requires a transparent same-sort literal wrapper");
                g->groupings = ce_resize(g->groupings,g->ngroupings+1,sizeof(*g->groupings));
                g->groupings[g->ngroupings++] = (CeGrouping){production,wrapper,parent,argument};
                continue;
            }
            if (meta_is(&s,"block") || meta_is(&s,"group")) {
                bool block = meta_is(&s,"block");
                if (!next(&s) || !layout_token(g,&s,block ? CE_LAYOUT_BLOCK_OPEN : CE_LAYOUT_GROUP_OPEN) ||
                    !layout_token(g,&s,block ? CE_LAYOUT_BLOCK_CLOSE : CE_LAYOUT_GROUP_CLOSE) || !eat(&s,";")) return false;
                continue;
            }
            if (meta_is(&s,"line") || meta_is(&s,"join") || meta_is(&s,"tight") || meta_is(&s,"left") || meta_is(&s,"spaced") ||
                meta_is(&s,"prefix")) {
                unsigned flags = meta_is(&s,"line") ? CE_LAYOUT_LINE : meta_is(&s,"join") ? CE_LAYOUT_JOIN :
                    meta_is(&s,"tight") ? CE_LAYOUT_LEFT | CE_LAYOUT_RIGHT : meta_is(&s,"spaced") ? CE_LAYOUT_SPACED :
                    meta_is(&s,"prefix") ? CE_LAYOUT_PREFIX : CE_LAYOUT_LEFT;
                if (!next(&s) || s.token.kind != CE_META_STRING)
                    return ce_error(error,s.token.offset,"layout declaration needs one or more literals");
                do { if (!layout_token(g,&s,flags)) return false; } while (s.token.kind == CE_META_STRING);
                if (!eat(&s,";")) return false;
                continue;
            }
            if (meta_is(&s,"indent")) {
                if (!next(&s) || s.token.kind != CE_META_NAME || s.token.text.len != 1 ||
                    s.token.text.data[0] < '1' || s.token.text.data[0] > '8')
                    return ce_error(error,s.token.offset,"layout indentation must be between one and eight spaces");
                g->indentation = s.token.text.data[0]-'0';
                if (!next(&s) || !eat(&s,";")) return false;
                continue;
            }
            if (!eat(&s,"token") || s.token.kind != CE_META_NAME)
                return ce_error(error,s.token.offset,"layout requires a lexical token name");
            int found = -1;
            for (size_t i = 0; i < g->nlex; ++i)
                if (ce_equal(g->lex[i].name,s.token.text)) found = (int)i;
            if (found < 0) return ce_error(error,s.token.offset,"layout token is absent from the grammar");
            if (!next(&s) || !eat(&s,"line") || !eat(&s,";")) return false;
            g->lex[found].line = true;
            continue;
        }
        if (meta_is(&s,"classification")) {
            if (g->c_scope || !next(&s) || s.token.kind != CE_META_NAME)
                return ce_error(error,s.token.offset,"expected one identifier classification declaration");
            int symbol = -1;
            for (size_t i = 0; i < g->nsymbols; ++i)
                if (g->symbols[i].kind == CE_TOKEN && ce_equal(g->symbols[i].name,s.token.text)) symbol = (int)i;
            if (symbol < 0 || !next(&s)) return ce_error(error,s.token.offset,"classification token is absent");
            int ordinary = profile_terminal(g,&s);
            if (ordinary < 0 || !next(&s)) return false;
            int type = profile_terminal(g,&s);
            if (type < 0 || ordinary == type || !next(&s) || !eat(&s,"using") || !eat(&s,"CScope") || !eat(&s,";")) return false;
            if (g->symbols[ordinary].kind != CE_LITERAL || g->symbols[type].kind != CE_LITERAL)
                return ce_error(error,s.token.offset,"classification markers must be literal terminals");
            g->c_scope = true; g->name_token = symbol;
            g->variable_terminal = ordinary; g->typedef_terminal = type;
            g->symbols[ordinary].virtual_token = true; g->symbols[type].virtual_token = true;
            continue;
        }
        if (meta_is(&s,"scope")) {
            CeAction selector; bool reduction;
            if (!next(&s)) return false;
            if (meta_is(&s,"deferred")) {
                int production;
                if (!next(&s) || !profile_rule(g,&s,&production) || !eat(&s,";")) return false;
                if (!g->c_scope) return ce_error(error,s.token.offset,"deferred declaration requires CScope classification");
                for (size_t i = 0; i < g->ndeferred_specifiers; ++i)
                    if (g->deferred_specifiers[i] == production) return ce_error(error,s.token.offset,"duplicate deferred declaration specifier");
                g->deferred_specifiers = ce_resize(g->deferred_specifiers,g->ndeferred_specifiers+1,sizeof(*g->deferred_specifiers));
                g->deferred_specifiers[g->ndeferred_specifiers++] = production;
                continue;
            }
            reduction = meta_is(&s,"reduction");
            if (!eat(&s,reduction ? "reduction" : "hook")) return false;
            /* The reduction selector is shared with conflict declarations. */
            CeText label = s.token.text;
            if (s.token.kind != CE_META_STRING || !next(&s) || s.token.kind != CE_META_STRING)
                return ce_error(error,s.token.offset,"scope hook needs label and result-category strings");
            selector = (CeAction){CE_LR_REDUCE,-1};
            for (size_t i = 0; i < g->nproductions; ++i)
                if (ce_equal(g->productions[i].label,label) && ce_equal(g->symbols[g->productions[i].lhs].name,s.token.text)) {
                    if (selector.value >= 0) return ce_error(error,s.token.offset,"scope selector is not unique");
                    selector.value = (int)i;
                }
            if (selector.value < 0 || !next(&s)) return ce_error(error,s.token.offset,"scope production is absent");
            size_t slot = 0;
            if (!reduction) {
                if (!eat(&s,"at") || s.token.kind != CE_META_NAME || !s.token.text.len)
                    return ce_error(error,s.token.offset,"expected scope hook position");
                for (size_t i = 0; i < s.token.text.len; ++i) {
                    unsigned c = s.token.text.data[i];
                    if (c < '0' || c > '9' || slot > g->productions[selector.value].len)
                        return ce_error(error,s.token.offset,"invalid scope hook position");
                    slot = slot*10+c-'0';
                }
                if (slot > g->productions[selector.value].len || !next(&s)) return ce_error(error,s.token.offset,"scope hook outside production");
            }
            unsigned action = scope_action(s.token.text);
            if (!action) return ce_error(error,s.token.offset,"unknown C scope action");
            if (reduction) {
                g->hooks = ce_resize(g->hooks,g->nhooks+1,sizeof(*g->hooks));
                g->hooks[g->nhooks++] = (CeScopeHook){selector.value,action,true};
            } else {
                int auxiliary = scope_auxiliary(g,action,s.token.text,error);
                if (auxiliary < 0) return false;
                PendingHook hook = {selector.value,auxiliary,slot};
                /* Arena ownership keeps failed profile reads leak-free. */
                PendingHook *next_hooks = ce_alloc(&g->arena,(nhooks+1)*sizeof(*next_hooks));
                if (nhooks) memcpy(next_hooks,hooks,nhooks*sizeof(*hooks));
                next_hooks[nhooks++] = hook; hooks = next_hooks;
            }
            if (!next(&s) || !eat(&s,";")) return false;
            continue;
        }
        if (meta_is(&s,"conflict")) {
            CeConflictPolicy policy = {.next=-2};
            if (!next(&s)) return false;
            /* A declared preference can cover every lookahead, while its two
               production selectors must still match the conflict cell. */
            policy.terminal = meta_is(&s,"any") ? -3 : profile_terminal(g,&s);
            if (policy.terminal == -2 || !next(&s) || !eat(&s,"prefer") || !profile_action(g,&s,&policy.preferred) ||
                !eat(&s,"over") || !profile_action(g,&s,&policy.alternative)) return false;
            if (meta_is(&s,"when")) {
                if (!next(&s) || !eat(&s,"next")) return false;
                policy.next = profile_terminal(g,&s);
                if (policy.next == -2 || !next(&s)) return false;
            }
            g->policies = ce_resize(g->policies,g->npolicies+1,sizeof(*g->policies));
            g->policies[g->npolicies++] = policy;
            if (!eat(&s,";")) return false;
            continue;
        }
        if (!eat(&s, "contextual") || !eat(&s, "token") || s.token.kind != CE_META_STRING)
            return ce_error(error, s.token.offset, "expected contextual production label string");
        CeText prod = s.token.text;
        if (!next(&s) || s.token.kind != CE_META_NAME) return ce_error(error, s.token.offset, "expected token category");
        CeText token = s.token.text;
        bool found_production = false, found_token = false;
        for (size_t i = 0; i < g->nproductions; ++i) if (ce_equal(g->productions[i].label, prod)) found_production = true;
        for (size_t i = 0; i < g->nlex; ++i) if (ce_equal(g->lex[i].name, token)) found_token = true;
        if (!found_production || !found_token) return ce_error(error, s.token.offset, "contextual alias names an unknown production or token");
        if (!next(&s) || s.token.kind != CE_META_STRING) return ce_error(error,s.token.offset,"contextual alias requires an explicit keyword spelling");
        CeText spelling = s.token.text;
        g->aliases = ce_resize(g->aliases, g->naliases+1, sizeof(*g->aliases));
        g->aliases[g->naliases++] = (CeLexAlias){prod,token,spelling};
        if (!next(&s) || !eat(&s, ";")) return false;
    }
    if ((nhooks || g->ninitial_typedefs) && !g->c_scope) return ce_error(error,0,"scope hooks and initial typedefs require a declared CScope classification");
    for (size_t i = 0; i < g->ninitial_typedefs; ++i) {
        CeTokens tokens = {0};
        bool valid = ce_lex(g,g->initial_typedefs[i],&tokens,error) && tokens.len == 1 &&
            tokens.data[0].symbol == g->name_token && tokens.data[0].text.len == g->initial_typedefs[i].len;
        ce_tokens_free(&tokens);
        if (!valid) return ce_error(error,0,"initial typedef is not an identifier of the lexical profile");
    }
    for (size_t i = 0; i < g->nspellings; ++i)
        if (g->symbols[g->spellings[i].physical].virtual_token || g->symbols[g->spellings[i].target].virtual_token)
            return ce_error(error,0,"lexical spelling cannot target or expose an internal classification marker");
    insert_hooks(g,hooks,nhooks);
    g->name = profile_name;
    return true;
}

bool ce_profile_read(CeGrammar *g, CeText source, CeError *error) {
    return profile_read_at(g,source,NULL,NULL,0,0,error);
}

bool ce_profile_bundle(CeGrammar *g, const CeProfileSource *sources, size_t len, CeText root, CeError *error) {
    for (size_t i = 0; i < len; ++i) if (ce_equal(sources[i].name,root))
        return profile_read_at(g,sources[i].source,NULL,sources,len,0,error);
    return ce_error(error,0,"root profile is absent from the bundle");
}

bool ce_profile_load(CeGrammar *g, const char *path, CeError *error) {
    CeText source;
    if (!ce_read_file(&g->arena,path,&source,error)) return false;
    char *directory = ce_alloc(&g->arena,strlen(path)+2); strcpy(directory,path);
    char *slash = strrchr(directory,'/');
    if (slash) *slash = 0; else strcpy(directory,".");
    return profile_read_at(g,source,directory,NULL,0,0,error);
}

bool ce_token_matches(const CeGrammar *g, int production_id, int expected, const CeToken *token) {
    if (token->symbol == expected) return true;
    if (production_id < 0 || (size_t)production_id >= g->nproductions) return false;
    if (g->symbols[token->symbol].kind != CE_LITERAL || g->symbols[expected].kind != CE_TOKEN) return false;
    for (size_t i = 0; i < g->naliases; ++i) {
        CeLexAlias alias = g->aliases[i];
        if (!ce_equal(alias.production, g->productions[production_id].label) || !ce_equal(alias.token, g->symbols[expected].name) || !ce_equal(alias.spelling,token->text)) continue;
        for (size_t j = 0; j < g->nlex; ++j) {
            if (g->lex[j].symbol != expected) continue;
            size_t len;
            if (ce_regex_match(g, g->lex[j].start, g->lex[j].end, token->text, 0, &len) && len == token->text.len) return true;
        }
    }
    return false;
}

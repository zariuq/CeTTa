#include "native/grammar_canonical_term_v1.h"
#include "lib_parse_native_grammar.h"
#include "symbol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Canonical terms over a small grammar that exercises every law, each case
 * printed back to its text:
 *
 *   S  ::= "(" L ")" O Q
 *   L  ::= I | I "," L            separated list, right recursion
 *   LL ::= I | LL "+" I           separated list, left recursion
 *   NL ::= I | I "," I            not a list: the second alternative does not recur
 *   I  ::= id                     transparent
 *   O  ::= "+" | "let"            literal-only alternatives
 *   Q  ::= I #rep                 x (sep x)*: the repetition's terminals precede I
 *   #rep ::= <empty> | "," I #rep
 *   E  ::= <empty>                zero-argument constructor
 *   C  ::= I "+" I | "(" C ")"    two constructors: C/0 and C/1
 *   P  ::= I #more                x ("," x)* spelled with a group, left recursion
 *   #more ::= <empty> | #more #g1
 *   #g1 ::= "," I                 one-alternative group: denotes its child
 *   M  ::= "{" #pairs "}"         a repetition of tuples
 *   #pairs ::= <empty> | #pairs #g2
 *   #g2 ::= I "=" I               one-alternative group: denotes the tuple
 *   A  ::= #g3 I                  a literal group
 *   #g3 ::= "+" | "-"
 *   B  ::= #g4                    a group with several constructor alternatives
 *   #g4 ::= "(" I ")" | "[" I "]"  named by its owner: B.1/0 and B.1/1
 *   V  ::= "?" #opt               an option: the list of at most one
 *   #opt ::= <empty> | I
 *   True ::= <empty>              a name a host reads as a literal is a string
 *
 * A second grammar has literal-only alternatives that make one text two
 * ways; each is then a constructor, so the two derivations are two terms:
 *   Y  ::= "ab" | "a" "b"         Y/0 and Y/1, both printing ab
 *   Z  ::= "x" | "x"              Z/0 and Z/1
 *   O2 ::= "+" | "let"            no collision: still text
 */

typedef struct {
    uint32_t passed;
    uint32_t failed;
} TestCounts;

static void expect(TestCounts *counts, bool condition, const char *label) {
    if (condition) {
        counts->passed++;
        return;
    }
    counts->failed++;
    fprintf(stderr, "FAIL: %s\n", label);
}

/* ---- grammar ---- */

typedef struct {
    CettaGrammarCanonicalProductionV1 productions[80];
    CettaGrammarCanonicalSymbolV1 rhs[200];
    uint32_t len;
    uint32_t rhs_len;
    bool drop_fixed_text; /* negative control: a fixed terminal without text */
} Grammar;

static CettaGrammarCanonicalSymbolV1 rule(const char *name) {
    CettaGrammarCanonicalSymbolV1 symbol = {CETTA_GRAMMAR_CANONICAL_RULE_SYMBOL_V1,
                                            symbol_intern_cstr(g_symbols, name), NULL};
    return symbol;
}

static CettaGrammarCanonicalSymbolV1 token(const char *name) {
    CettaGrammarCanonicalSymbolV1 symbol = {CETTA_GRAMMAR_CANONICAL_TOKEN_SYMBOL_V1,
                                            symbol_intern_cstr(g_symbols, name), NULL};
    return symbol;
}

static CettaGrammarCanonicalSymbolV1 fixed(const char *text) {
    CettaGrammarCanonicalSymbolV1 symbol = {CETTA_GRAMMAR_CANONICAL_FIXED_SYMBOL_V1,
                                            symbol_intern_cstr(g_symbols, text ? text : "?"),
                                            text};
    return symbol;
}

static void add(Grammar *grammar, const char *label, const char *lhs,
                CettaGrammarCanonicalRuleKindV1 kind, const char *owner,
                const CettaGrammarCanonicalSymbolV1 *rhs, uint32_t rhs_len) {
    CettaGrammarCanonicalProductionV1 *production = &grammar->productions[grammar->len++];
    production->label = symbol_intern_cstr(g_symbols, label);
    production->lhs = symbol_intern_cstr(g_symbols, lhs);
    production->rule_kind = kind;
    production->owner = owner ? symbol_intern_cstr(g_symbols, owner) : SYMBOL_ID_NONE;
    production->rhs = &grammar->rhs[grammar->rhs_len];
    production->rhs_len = rhs_len;
    if (rhs_len > 0u)
        memcpy(&grammar->rhs[grammar->rhs_len], rhs, rhs_len * sizeof(*rhs));
    grammar->rhs_len += rhs_len;
}

#define AUTHORED CETTA_GRAMMAR_CANONICAL_AUTHORED_V1
#define REPETITION CETTA_GRAMMAR_CANONICAL_REPETITION_V1
#define OPTION CETTA_GRAMMAR_CANONICAL_OPTION_V1
#define GROUP CETTA_GRAMMAR_CANONICAL_GROUP_V1

static void grammar_init(Grammar *grammar) {
    bool drop = grammar->drop_fixed_text;
    memset(grammar, 0, sizeof(*grammar));
    grammar->drop_fixed_text = drop;
    {
        CettaGrammarCanonicalSymbolV1 rhs[] = {fixed("("), rule("L"), fixed(")"), rule("O"),
                                               rule("Q")};
        add(grammar, "S", "S", AUTHORED, NULL, rhs, 5u);
    }
    {
        CettaGrammarCanonicalSymbolV1 base[] = {rule("I")};
        CettaGrammarCanonicalSymbolV1 step[] = {rule("I"), fixed(","), rule("L")};
        add(grammar, "L.0", "L", AUTHORED, NULL, base, 1u);
        add(grammar, "L.1", "L", AUTHORED, NULL, step, 3u);
    }
    {
        CettaGrammarCanonicalSymbolV1 base[] = {rule("I")};
        CettaGrammarCanonicalSymbolV1 step[] = {rule("LL"), fixed("+"), rule("I")};
        add(grammar, "LL.0", "LL", AUTHORED, NULL, base, 1u);
        add(grammar, "LL.1", "LL", AUTHORED, NULL, step, 3u);
    }
    {
        CettaGrammarCanonicalSymbolV1 base[] = {rule("I")};
        CettaGrammarCanonicalSymbolV1 pair[] = {rule("I"), fixed(","), rule("I")};
        add(grammar, "NL.0", "NL", AUTHORED, NULL, base, 1u);
        add(grammar, "NL.1", "NL", AUTHORED, NULL, pair, 3u);
    }
    {
        CettaGrammarCanonicalSymbolV1 rhs[] = {token("id")};
        add(grammar, "I", "I", AUTHORED, NULL, rhs, 1u);
    }
    {
        CettaGrammarCanonicalSymbolV1 plus[] = {fixed("+")};
        CettaGrammarCanonicalSymbolV1 let[] = {fixed(drop ? NULL : "let")};
        add(grammar, "O.0", "O", AUTHORED, NULL, plus, 1u);
        add(grammar, "O.1", "O", AUTHORED, NULL, let, 1u);
    }
    {
        CettaGrammarCanonicalSymbolV1 rhs[] = {rule("I"), rule("#rep")};
        CettaGrammarCanonicalSymbolV1 step[] = {fixed(","), rule("I"), rule("#rep")};
        add(grammar, "Q", "Q", AUTHORED, NULL, rhs, 2u);
        add(grammar, "#rep", "#rep", REPETITION, "Q", NULL, 0u);
        add(grammar, "#rep", "#rep", REPETITION, "Q", step, 3u);
    }
    add(grammar, "E", "E", AUTHORED, NULL, NULL, 0u);
    {
        CettaGrammarCanonicalSymbolV1 sum[] = {rule("I"), fixed("+"), rule("I")};
        CettaGrammarCanonicalSymbolV1 group[] = {fixed("("), rule("C"), fixed(")")};
        add(grammar, "C.0", "C", AUTHORED, NULL, sum, 3u);
        add(grammar, "C.1", "C", AUTHORED, NULL, group, 3u);
    }
    {
        CettaGrammarCanonicalSymbolV1 rhs[] = {rule("I"), rule("#more")};
        CettaGrammarCanonicalSymbolV1 step[] = {rule("#more"), rule("#g1")};
        CettaGrammarCanonicalSymbolV1 group[] = {fixed(","), rule("I")};
        add(grammar, "P", "P", AUTHORED, NULL, rhs, 2u);
        add(grammar, "#more.0", "#more", REPETITION, "P", NULL, 0u);
        add(grammar, "#more.1", "#more", REPETITION, "P", step, 2u);
        add(grammar, "#g1", "#g1", GROUP, "P", group, 2u);
    }
    {
        CettaGrammarCanonicalSymbolV1 rhs[] = {fixed("{"), rule("#pairs"), fixed("}")};
        CettaGrammarCanonicalSymbolV1 step[] = {rule("#pairs"), rule("#g2")};
        CettaGrammarCanonicalSymbolV1 group[] = {rule("I"), fixed("="), rule("I")};
        add(grammar, "M", "M", AUTHORED, NULL, rhs, 3u);
        add(grammar, "#pairs.0", "#pairs", REPETITION, "M", NULL, 0u);
        add(grammar, "#pairs.1", "#pairs", REPETITION, "M", step, 2u);
        add(grammar, "#g2", "#g2", GROUP, "M", group, 3u);
    }
    {
        CettaGrammarCanonicalSymbolV1 rhs[] = {rule("#g3"), rule("I")};
        CettaGrammarCanonicalSymbolV1 plus[] = {fixed("+")};
        CettaGrammarCanonicalSymbolV1 minus[] = {fixed("-")};
        add(grammar, "A", "A", AUTHORED, NULL, rhs, 2u);
        add(grammar, "#g3.0", "#g3", GROUP, "A", plus, 1u);
        add(grammar, "#g3.1", "#g3", GROUP, "A", minus, 1u);
    }
    {
        CettaGrammarCanonicalSymbolV1 rhs[] = {rule("#g4")};
        CettaGrammarCanonicalSymbolV1 round[] = {fixed("("), rule("I"), fixed(")")};
        CettaGrammarCanonicalSymbolV1 square[] = {fixed("["), rule("I"), fixed("]")};
        add(grammar, "B", "B", AUTHORED, NULL, rhs, 1u);
        add(grammar, "#g4.0", "#g4", GROUP, "B", round, 3u);
        add(grammar, "#g4.1", "#g4", GROUP, "B", square, 3u);
    }
    {
        CettaGrammarCanonicalSymbolV1 rhs[] = {fixed("?"), rule("#opt")};
        CettaGrammarCanonicalSymbolV1 some[] = {rule("I")};
        add(grammar, "V", "V", AUTHORED, NULL, rhs, 2u);
        add(grammar, "#opt.0", "#opt", OPTION, "V", NULL, 0u);
        add(grammar, "#opt.1", "#opt", OPTION, "V", some, 1u);
    }
    add(grammar, "True", "True", AUTHORED, NULL, NULL, 0u);
}

/* ---- shape collisions ---- */

/*
 * The first four sorts had collisions while tuples and text groups were
 * anonymous; now each is a constructor, so none collides.  The last four are
 * what a morphism erasing names would merge (S2) and ambiguities (K2, T2, U2).
 *   R   ::= #t | X2          a tuple and a separated list at one sort
 *   #t  ::= "(" X "," X ")"
 *   X2  ::= X | X ";" X2
 *   K   ::= Ka | #gk         one literal text, bare and bracketed
 *   Ka  ::= "x"
 *   #gk ::= "[" Kc "]"
 *   Kc  ::= "x"
 *   T   ::= Ta | #gt         one token class, bare and quoted
 *   Ta  ::= id
 *   #gt ::= "'" id "'"
 *   U   ::= Ua | #gu         one rule along two paths
 *   Ua  ::= Uc
 *   #gu ::= "<" Uc ">"
 *   Uc  ::= X "+" X
 *   X   ::= id
 *   W   ::= #gw | Wb         no collision: a bracketed id, or a number
 *   #gw ::= "[" X "]"
 *   Wb  ::= num
 *   S2  ::= X2 | X3          two separated lists at one sort
 *   X3  ::= X | X "|" X3
 *   K2  ::= Ka | Kc          one literal text two ways
 *   T2  ::= Ta | Tb          one token class two ways
 *   Tb  ::= id
 *   U2  ::= Ua | Ub          one rule two ways
 *   Ub  ::= Uc
 */
static void literal_grammar_init(Grammar *grammar) {
    memset(grammar, 0, sizeof(*grammar));
    {
        CettaGrammarCanonicalSymbolV1 one[] = {fixed("ab")};
        CettaGrammarCanonicalSymbolV1 two[] = {fixed("a"), fixed("b")};
        CettaGrammarCanonicalSymbolV1 x[] = {fixed("x")};
        CettaGrammarCanonicalSymbolV1 plus[] = {fixed("+")};
        CettaGrammarCanonicalSymbolV1 let[] = {fixed("let")};
        add(grammar, "Y.0", "Y", AUTHORED, NULL, one, 1u);
        add(grammar, "Y.1", "Y", AUTHORED, NULL, two, 2u);
        add(grammar, "Z.0", "Z", AUTHORED, NULL, x, 1u);
        add(grammar, "Z.1", "Z", AUTHORED, NULL, x, 1u);
        add(grammar, "O2.0", "O2", AUTHORED, NULL, plus, 1u);
        add(grammar, "O2.1", "O2", AUTHORED, NULL, let, 1u);
    }
}

static void collision_grammar_init(Grammar *grammar) {
    memset(grammar, 0, sizeof(*grammar));
    {
        CettaGrammarCanonicalSymbolV1 tuple[] = {rule("#t")};
        CettaGrammarCanonicalSymbolV1 list[] = {rule("X2")};
        CettaGrammarCanonicalSymbolV1 group[] = {fixed("("), rule("X"), fixed(","), rule("X"),
                                                 fixed(")")};
        CettaGrammarCanonicalSymbolV1 base[] = {rule("X")};
        CettaGrammarCanonicalSymbolV1 step[] = {rule("X"), fixed(";"), rule("X2")};
        add(grammar, "R.0", "R", AUTHORED, NULL, tuple, 1u);
        add(grammar, "R.1", "R", AUTHORED, NULL, list, 1u);
        add(grammar, "#t", "#t", GROUP, "R", group, 5u);
        add(grammar, "X2.0", "X2", AUTHORED, NULL, base, 1u);
        add(grammar, "X2.1", "X2", AUTHORED, NULL, step, 3u);
    }
    {
        CettaGrammarCanonicalSymbolV1 bare[] = {rule("Ka")};
        CettaGrammarCanonicalSymbolV1 bracketed[] = {rule("#gk")};
        CettaGrammarCanonicalSymbolV1 x[] = {fixed("x")};
        CettaGrammarCanonicalSymbolV1 group[] = {fixed("["), rule("Kc"), fixed("]")};
        add(grammar, "K.0", "K", AUTHORED, NULL, bare, 1u);
        add(grammar, "K.1", "K", AUTHORED, NULL, bracketed, 1u);
        add(grammar, "Ka", "Ka", AUTHORED, NULL, x, 1u);
        add(grammar, "#gk", "#gk", GROUP, "K", group, 3u);
        add(grammar, "Kc", "Kc", AUTHORED, NULL, x, 1u);
    }
    {
        CettaGrammarCanonicalSymbolV1 bare[] = {rule("Ta")};
        CettaGrammarCanonicalSymbolV1 quoted[] = {rule("#gt")};
        CettaGrammarCanonicalSymbolV1 id[] = {token("id")};
        CettaGrammarCanonicalSymbolV1 group[] = {fixed("'"), token("id"), fixed("'")};
        add(grammar, "T.0", "T", AUTHORED, NULL, bare, 1u);
        add(grammar, "T.1", "T", AUTHORED, NULL, quoted, 1u);
        add(grammar, "Ta", "Ta", AUTHORED, NULL, id, 1u);
        add(grammar, "#gt", "#gt", GROUP, "T", group, 3u);
    }
    {
        CettaGrammarCanonicalSymbolV1 direct[] = {rule("Ua")};
        CettaGrammarCanonicalSymbolV1 angled[] = {rule("#gu")};
        CettaGrammarCanonicalSymbolV1 inner[] = {rule("Uc")};
        CettaGrammarCanonicalSymbolV1 group[] = {fixed("<"), rule("Uc"), fixed(">")};
        CettaGrammarCanonicalSymbolV1 sum[] = {rule("X"), fixed("+"), rule("X")};
        add(grammar, "U.0", "U", AUTHORED, NULL, direct, 1u);
        add(grammar, "U.1", "U", AUTHORED, NULL, angled, 1u);
        add(grammar, "Ua", "Ua", AUTHORED, NULL, inner, 1u);
        add(grammar, "#gu", "#gu", GROUP, "U", group, 3u);
        add(grammar, "Uc", "Uc", AUTHORED, NULL, sum, 3u);
    }
    {
        CettaGrammarCanonicalSymbolV1 id[] = {token("id")};
        add(grammar, "X", "X", AUTHORED, NULL, id, 1u);
    }
    {
        CettaGrammarCanonicalSymbolV1 bracketed[] = {rule("#gw")};
        CettaGrammarCanonicalSymbolV1 number[] = {rule("Wb")};
        CettaGrammarCanonicalSymbolV1 group[] = {fixed("["), rule("X"), fixed("]")};
        CettaGrammarCanonicalSymbolV1 num[] = {token("num")};
        add(grammar, "W.0", "W", AUTHORED, NULL, bracketed, 1u);
        add(grammar, "W.1", "W", AUTHORED, NULL, number, 1u);
        add(grammar, "#gw", "#gw", GROUP, "W", group, 3u);
        add(grammar, "Wb", "Wb", AUTHORED, NULL, num, 1u);
    }
    {
        CettaGrammarCanonicalSymbolV1 x2[] = {rule("X2")};
        CettaGrammarCanonicalSymbolV1 x3[] = {rule("X3")};
        CettaGrammarCanonicalSymbolV1 base[] = {rule("X")};
        CettaGrammarCanonicalSymbolV1 step[] = {rule("X"), fixed("|"), rule("X3")};
        CettaGrammarCanonicalSymbolV1 ka[] = {rule("Ka")};
        CettaGrammarCanonicalSymbolV1 kc[] = {rule("Kc")};
        CettaGrammarCanonicalSymbolV1 ta[] = {rule("Ta")};
        CettaGrammarCanonicalSymbolV1 tb[] = {rule("Tb")};
        CettaGrammarCanonicalSymbolV1 id[] = {token("id")};
        CettaGrammarCanonicalSymbolV1 ua[] = {rule("Ua")};
        CettaGrammarCanonicalSymbolV1 ub[] = {rule("Ub")};
        CettaGrammarCanonicalSymbolV1 uc[] = {rule("Uc")};
        add(grammar, "S2.0", "S2", AUTHORED, NULL, x2, 1u);
        add(grammar, "S2.1", "S2", AUTHORED, NULL, x3, 1u);
        add(grammar, "X3.0", "X3", AUTHORED, NULL, base, 1u);
        add(grammar, "X3.1", "X3", AUTHORED, NULL, step, 3u);
        add(grammar, "K2.0", "K2", AUTHORED, NULL, ka, 1u);
        add(grammar, "K2.1", "K2", AUTHORED, NULL, kc, 1u);
        add(grammar, "T2.0", "T2", AUTHORED, NULL, ta, 1u);
        add(grammar, "T2.1", "T2", AUTHORED, NULL, tb, 1u);
        add(grammar, "Tb", "Tb", AUTHORED, NULL, id, 1u);
        add(grammar, "U2.0", "U2", AUTHORED, NULL, ua, 1u);
        add(grammar, "U2.1", "U2", AUTHORED, NULL, ub, 1u);
        add(grammar, "Ub", "Ub", AUTHORED, NULL, uc, 1u);
    }
}

/* S ::= A | B, B ::= A, A ::= "x": two derivations of one text, which
 * canonical terms keep apart by naming S's unary alternatives. */
static void ambiguous_grammar_init(Grammar *grammar) {
    CettaGrammarCanonicalSymbolV1 a[] = {rule("A")};
    CettaGrammarCanonicalSymbolV1 b[] = {rule("B")};
    CettaGrammarCanonicalSymbolV1 x[] = {fixed("x")};
    memset(grammar, 0, sizeof(*grammar));
    add(grammar, "S.0", "S", AUTHORED, NULL, a, 1u);
    add(grammar, "S.1", "S", AUTHORED, NULL, b, 1u);
    add(grammar, "B", "B", AUTHORED, NULL, a, 1u);
    add(grammar, "A", "A", AUTHORED, NULL, x, 1u);
}

/* C0 ::= C1, ..., C69 ::= id: a transparent chain longer than any fixed
 * search depth. */
static void chain_grammar_init(Grammar *grammar, char names[][8], uint32_t len) {
    memset(grammar, 0, sizeof(*grammar));
    for (uint32_t i = 0u; i < len; i++)
        snprintf(names[i], 8u, "C%u", i);
    for (uint32_t i = 0u; i < len; i++) {
        CettaGrammarCanonicalSymbolV1 next[1];
        next[0] = i + 1u < len ? rule(names[i + 1u]) : token("id");
        add(grammar, names[i], names[i], AUTHORED, NULL, next, 1u);
    }
}

static bool has_collision(const CettaGrammarCanonicalCollisionV1 *collisions, uint32_t len,
                          CettaGrammarCanonicalCollisionKindV1 kind, const char *sort,
                          const char *a, const char *b) {
    for (uint32_t i = 0u; i < len; i++) {
        const CettaGrammarCanonicalCollisionV1 *c = &collisions[i];
        if (c->kind != kind || strcmp(c->sort, sort) != 0)
            continue;
        if (!b) {
            if (!c->second && strcmp(c->first, a) == 0)
                return true;
            continue;
        }
        if (c->second && ((strcmp(c->first, a) == 0 && strcmp(c->second, b) == 0) ||
                          (strcmp(c->first, b) == 0 && strcmp(c->second, a) == 0)))
            return true;
    }
    return false;
}

/* ---- derivation trees over single-character items ---- */

static const char *g_text;

static bool leaf(void *context, Arena *arena, const Atom *node, const char *label,
                 SymbolId *token, Atom **text) {
    char buffer[64];
    int64_t start, stop;
    (void)context;
    if (strcmp(label, "#token:id") != 0)
        return false;
    start = node->expr.elems[2]->ground.ival;
    stop = node->expr.elems[3]->ground.ival;
    if (start < 0 || stop < start || (size_t)(stop - start) >= sizeof(buffer))
        return false;
    memcpy(buffer, g_text + start, (size_t)(stop - start));
    buffer[stop - start] = '\0';
    *token = symbol_intern_cstr(g_symbols, "id");
    *text = atom_string(arena, buffer);
    return true;
}

static Atom *node(Arena *arena, const char *label, int64_t start, int64_t stop,
                  Atom **children, uint32_t child_len) {
    Atom *elements[16];
    uint32_t i;
    elements[0] = atom_symbol(arena, "CstRuleV1");
    elements[1] = atom_string(arena, label);
    elements[2] = atom_int(arena, start);
    elements[3] = atom_int(arena, stop);
    for (i = 0u; i < child_len; i++)
        elements[4u + i] = children[i];
    return atom_expr(arena, elements, 4u + child_len);
}

static Atom *item(Arena *arena, int64_t at) {
    Atom *child = node(arena, "#token:id", at, at + 1, NULL, 0u);
    return node(arena, "I", at, at + 1, &child, 1u);
}

static Atom *eps(Arena *arena, int64_t at) {
    return node(arena, "#eps", at, at, NULL, 0u);
}

/* L over items at the given positions, right recursive. */
static Atom *right_list(Arena *arena, const int64_t *at, uint32_t len) {
    Atom *children[2];
    children[0] = item(arena, at[0]);
    if (len == 1u)
        return node(arena, "L.0", at[0], at[0] + 1, children, 1u);
    children[1] = right_list(arena, at + 1, len - 1u);
    return node(arena, "L.1", at[0], at[len - 1u] + 1, children, 2u);
}

/* LL over items, left recursive. */
static Atom *left_list(Arena *arena, const int64_t *at, uint32_t len) {
    Atom *children[2];
    if (len == 1u) {
        children[0] = item(arena, at[0]);
        return node(arena, "LL.0", at[0], at[0] + 1, children, 1u);
    }
    children[0] = left_list(arena, at, len - 1u);
    children[1] = item(arena, at[len - 1u]);
    return node(arena, "LL.1", at[0], at[len - 1u] + 1, children, 2u);
}

/* #rep over items, right recursive, both productions sharing one label. */
static Atom *repetition(Arena *arena, const int64_t *at, uint32_t len, int64_t end) {
    Atom *children[2];
    if (len == 0u) {
        children[0] = eps(arena, end);
        return node(arena, "#rep", end, end, children, 1u);
    }
    children[0] = item(arena, at[0]);
    children[1] = repetition(arena, at + 1, len - 1u, end);
    return node(arena, "#rep", at[0] - 1, end, children, 2u);
}

static Atom *q_node(Arena *arena, const int64_t *at, uint32_t len, int64_t end) {
    Atom *children[2];
    children[0] = item(arena, at[0]);
    children[1] = repetition(arena, at + 1, len - 1u, end);
    return node(arena, "Q", at[0], end, children, 2u);
}

/* #more over groups ", I", left recursive as the EBNF lowering writes it. */
static Atom *more(Arena *arena, const int64_t *at, uint32_t len) {
    Atom *children[2], *group_child;
    if (len == 0u)
        return node(arena, "#more.0", 1, 1, NULL, 0u);
    children[0] = more(arena, at, len - 1u);
    group_child = item(arena, at[len - 1u]);
    children[1] = node(arena, "#g1", at[len - 1u] - 1, at[len - 1u] + 1, &group_child, 1u);
    return node(arena, "#more.1", 1, at[len - 1u] + 1, children, 2u);
}

static Atom *pairs(Arena *arena, const int64_t *at, uint32_t len) {
    Atom *children[2], *group_children[2];
    if (len == 0u)
        return node(arena, "#pairs.0", 1, 1, NULL, 0u);
    children[0] = pairs(arena, at, len - 1u);
    group_children[0] = item(arena, at[2u * (len - 1u)]);
    group_children[1] = item(arena, at[2u * (len - 1u) + 1u]);
    children[1] = node(arena, "#g2", at[2u * (len - 1u)], at[2u * (len - 1u) + 1u] + 1,
                       group_children, 2u);
    return node(arena, "#pairs.1", 1, at[2u * (len - 1u) + 1u] + 1, children, 2u);
}

static bool never_space(void *context, const char *left, size_t left_len, const char *right,
                        size_t right_len) {
    (void)context;
    (void)left;
    (void)left_len;
    (void)right;
    (void)right_len;
    return false;
}

/* Print inside a one-element expression so strings keep their quotes. */
static void show(Arena *arena, Atom *term, char *out, size_t out_size) {
    const char *printed = atom_to_string(arena, atom_expr(arena, &term, 1u));
    size_t len = strlen(printed);
    snprintf(out, out_size, "%.*s", (int)(len - 2u), printed + 1);
}

static void expect_term(TestCounts *counts, const CettaGrammarCanonicalTableV1 *table,
                        Arena *arena, const char *text, Atom *cst, const char *start,
                        const char *expected, const char *label) {
    Atom *term = NULL;
    char out[512], error[256] = {0}, print_label[256];
    char *printed = NULL;
    size_t printed_len = 0u;
    bool ok;
    g_text = text;
    ok = cetta_grammar_canonical_from_cst_v1(table, cst, "#eps", leaf, NULL, arena, &term, error,
                                              sizeof(error));
    if (!ok) {
        fprintf(stderr, "%s: %s\n", label, error);
        expect(counts, false, label);
        return;
    }
    show(arena, term, out, sizeof(out));
    if (strcmp(out, expected) != 0)
        fprintf(stderr, "%s: got %s, expected %s\n", label, out, expected);
    expect(counts, strcmp(out, expected) == 0, label);
    ok = cetta_grammar_canonical_print_v1(table, term, symbol_intern_cstr(g_symbols, start),
                                          never_space, NULL, &printed, &printed_len, error,
                                          sizeof(error));
    if (!ok)
        fprintf(stderr, "%s: print: %s\n", label, error);
    else if (strcmp(printed, text) != 0)
        fprintf(stderr, "%s: printed %s, expected %s\n", label, printed, text);
    snprintf(print_label, sizeof(print_label), "%s: prints back", label);
    expect(counts, ok && strcmp(printed, text) == 0, print_label);
    free(printed);
}

static void expect_rejected(TestCounts *counts, const CettaGrammarCanonicalTableV1 *table,
                            Arena *arena, const char *text, Atom *cst, const char *message,
                            const char *label) {
    Atom *term = NULL;
    char error[256] = {0};
    bool ok;
    g_text = text;
    ok = cetta_grammar_canonical_from_cst_v1(table, cst, "#eps", leaf, NULL, arena, &term, error,
                                              sizeof(error));
    if (ok)
        fprintf(stderr, "%s: accepted\n", label);
    else if (!strstr(error, message))
        fprintf(stderr, "%s: error %s, expected %s\n", label, error, message);
    expect(counts, !ok && strstr(error, message) != NULL, label);
}

int main(void) {
    SymbolTable symbols;
    TestCounts counts = {0};
    Arena arena;
    Grammar grammar;
    CettaGrammarCanonicalTableV1 *table = NULL;
    char error[256] = {0};

    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    arena_init(&arena);

    memset(&grammar, 0, sizeof(grammar));
    grammar_init(&grammar);
    expect(&counts,
           cetta_grammar_canonical_table_build_v1(grammar.productions, grammar.len, &table, error,
                                                  sizeof(error)),
           "table builds");
    if (!table) {
        fprintf(stderr, "table: %s\n", error);
        return 1;
    }

    {
        static const int64_t at[] = {0, 2, 4};
        expect_term(&counts, table, &arena, "a,b,c", right_list(&arena, at, 3u), "L",
                    "(bnf L [(bnf id \"a\") (bnf id \"b\") (bnf id \"c\")])",
                    "right-recursive separated list is one list node");
        expect_term(&counts, table, &arena, "a", right_list(&arena, at, 1u), "L",
                    "(bnf L [(bnf id \"a\")])", "one-element separated list");
        expect_term(&counts, table, &arena, "a+b+c", left_list(&arena, at, 3u), "LL",
                    "(bnf LL [(bnf id \"a\") (bnf id \"b\") (bnf id \"c\")])",
                    "left-recursive separated list is one list node");
        expect_term(&counts, table, &arena, "a,b,c", q_node(&arena, at, 3u, 5), "Q",
                    "(bnf Q (bnf Q*1 [(bnf id \"a\") (bnf id \"b\") (bnf id \"c\")]))",
                    "x (sep x)* is one list, named by the repetition");
        expect_term(&counts, table, &arena, "a", q_node(&arena, at, 1u, 1), "Q",
                    "(bnf Q (bnf Q*1 [(bnf id \"a\")]))", "x (sep x)* with an empty tail");
    }
    {
        Atom *children[2];
        children[0] = item(&arena, 0);
        children[1] = item(&arena, 2);
        expect_term(&counts, table, &arena, "a,b", node(&arena, "NL.1", 0, 3, children, 2u), "NL",
                    "(bnf NL (bnf id \"a\") (bnf id \"b\"))",
                    "a non-recursive pair is a constructor, not a list");
        expect_term(&counts, table, &arena, "a+b", node(&arena, "C.0", 0, 3, children, 2u), "C",
                    "(bnf C/0 (bnf id \"a\") (bnf id \"b\"))", "first of two constructors is C/0");
    }
    {
        Atom *children[2], *sum, *group;
        children[0] = item(&arena, 1);
        children[1] = item(&arena, 3);
        sum = node(&arena, "C.0", 1, 4, children, 2u);
        group = node(&arena, "C.1", 0, 5, &sum, 1u);
        expect_term(&counts, table, &arena, "(a+b)", group, "C",
                    "(bnf C/1 (bnf C/0 (bnf id \"a\") (bnf id \"b\")))",
                    "a parenthesized alternative is its own constructor");
    }
    expect_term(&counts, table, &arena, "+", node(&arena, "O.0", 0, 1, NULL, 0u), "O", "\"+\"",
                "a literal-only alternative is its text");
    expect_term(&counts, table, &arena, "let", node(&arena, "O.1", 0, 3, NULL, 0u), "O",
                "\"let\"", "a keyword alternative is its text");
    {
        Atom *child = eps(&arena, 0);
        expect_term(&counts, table, &arena, "", node(&arena, "E", 0, 0, &child, 1u), "E", "(bnf E)",
                    "an empty alternative is a constant");
    }
    {
        static const int64_t list_at[] = {1, 3};
        static const int64_t q_at[] = {6, 8};
        Atom *children[3];
        children[0] = right_list(&arena, list_at, 2u);
        children[1] = node(&arena, "O.0", 5, 6, NULL, 0u);
        children[2] = q_node(&arena, q_at, 2u, 9);
        expect_term(&counts, table, &arena, "(a,b)+c,d", node(&arena, "S", 0, 9, children, 3u),
                    "S",
                    "(bnf S (bnf L [(bnf id \"a\") (bnf id \"b\")]) \"+\" "
                    "(bnf Q (bnf Q*1 [(bnf id \"c\") (bnf id \"d\")])))",
                    "fixed terminals are dropped and arguments keep their order");
    }
    {
        static const int64_t at[] = {2, 4};
        Atom *children[2];
        children[0] = item(&arena, 0);
        children[1] = more(&arena, at, 2u);
        expect_term(&counts, table, &arena, "a,b,c", node(&arena, "P", 0, 5, children, 2u), "P",
                    "(bnf P (bnf id \"a\") (bnf P*1 [(bnf P.1 (bnf id \"b\")) (bnf P.1 (bnf id \"c\"))]))",
                    "a group with fixed text inside a repetition is a node of its own");
        children[1] = more(&arena, at, 0u);
        expect_term(&counts, table, &arena, "a", node(&arena, "P", 0, 1, children, 2u), "P",
                    "(bnf P (bnf id \"a\") (bnf P*1 []))", "a repetition with no items is its empty list");
    }
    {
        static const int64_t at[] = {1, 3, 4, 6};
        Atom *child = pairs(&arena, at, 2u);
        expect_term(&counts, table, &arena, "{a=bc=d}", node(&arena, "M", 0, 8, &child, 1u), "M",
                    "(bnf M (bnf M*1 [(bnf M.1 (bnf id \"a\") (bnf id \"b\")) "
                    "(bnf M.1 (bnf id \"c\") (bnf id \"d\"))]))",
                    "a group of several children is a constructor named by its owner");
    }
    {
        Atom *children[2];
        children[0] = node(&arena, "#g3.1", 0, 1, NULL, 0u);
        children[1] = item(&arena, 1);
        expect_term(&counts, table, &arena, "-a", node(&arena, "A", 0, 2, children, 2u), "A",
                    "(bnf A \"-\" (bnf id \"a\"))", "a literal group is its text");
    }
    {
        Atom *inner = item(&arena, 1);
        Atom *group = node(&arena, "#g4.1", 0, 3, &inner, 1u);
        expect_term(&counts, table, &arena, "[a]", node(&arena, "B", 0, 3, &group, 1u), "B",
                    "(bnf B.1/1 (bnf id \"a\"))", "group alternatives are named by owner and ordinal");
    }
    {
        Atom *option, *present = item(&arena, 1);
        option = node(&arena, "#opt.1", 1, 2, &present, 1u);
        expect_term(&counts, table, &arena, "?a", node(&arena, "V", 0, 2, &option, 1u), "V",
                    "(bnf V (bnf V?1 [(bnf id \"a\")]))", "a present option is a one-element list");
        option = node(&arena, "#opt.0", 1, 1, NULL, 0u);
        expect_term(&counts, table, &arena, "?", node(&arena, "V", 0, 1, &option, 1u), "V",
                    "(bnf V (bnf V?1 []))", "an absent option is the empty list");
    }

    {
        Atom *child = eps(&arena, 0);
        expect_term(&counts, table, &arena, "", node(&arena, "True", 0, 0, &child, 1u), "True",
                    "(bnf \"True\")", "a name spelled like a host literal is a string");
    }

    /* Negative controls. */
    {
        Atom *child = item(&arena, 0);
        expect_rejected(&counts, table, &arena, "a", node(&arena, "missing", 0, 1, &child, 1u),
                        "unknown production", "an unknown production is rejected");
        expect_rejected(&counts, table, &arena, "a+", node(&arena, "C.0", 0, 2, &child, 1u),
                        "delivered 1 of 2 values", "a missing argument is rejected");
    }
    {
        Atom *children[2];
        children[0] = node(&arena, "#token:id", 0, 1, NULL, 0u);
        children[1] = node(&arena, "#token:id", 1, 2, NULL, 0u);
        expect_rejected(&counts, table, &arena, "ab", node(&arena, "I", 0, 2, children, 2u),
                        "delivered 2 of 1 values",
                        "a transparent node with two values is rejected");
    }
    {
        char *printed = NULL;
        Atom *wrong = atom_expr3(&arena, atom_symbol(&arena, "bnf"), atom_symbol(&arena, "C/0"),
                                 atom_string(&arena, "x"));
        expect(&counts,
               !cetta_grammar_canonical_print_v1(table, wrong, symbol_intern_cstr(g_symbols, "C"),
                                                 never_space, NULL, &printed, NULL, error,
                                                 sizeof(error)) &&
                   printed == NULL,
               "a constructor with the wrong arguments is not printed");
    }
    {
        Grammar broken;
        CettaGrammarCanonicalTableV1 *rejected = NULL;
        memset(&broken, 0, sizeof(broken));
        broken.drop_fixed_text = true;
        grammar_init(&broken);
        expect(&counts,
               !cetta_grammar_canonical_table_build_v1(broken.productions, broken.len, &rejected,
                                                       error, sizeof(error)) &&
                   rejected == NULL && strstr(error, "no fixed text") != NULL,
               "a fixed terminal without text is rejected");
    }
    {
        Grammar clash;
        CettaGrammarCanonicalTableV1 *rejected = NULL;
        CettaGrammarCanonicalSymbolV1 rhs[] = {token("I")};
        memset(&clash, 0, sizeof(clash));
        grammar_init(&clash);
        add(&clash, "T", "T", AUTHORED, NULL, rhs, 1u);
        expect(&counts,
               !cetta_grammar_canonical_table_build_v1(clash.productions, clash.len, &rejected,
                                                       error, sizeof(error)) &&
                   rejected == NULL && strstr(error, "token I is also a rule") != NULL,
               "a token named like a rule is rejected");
    }

    {
        CettaGrammarCanonicalCollisionV1 *collisions = NULL;
        uint32_t collision_len = 0u;
        expect(&counts,
               cetta_grammar_canonical_collisions_v1(table, &collisions, &collision_len, error,
                                                     sizeof(error)) &&
                   collision_len == 0u,
               "no two productions of the law grammar make one shape at a sort");
        for (uint32_t c = 0u; c < collision_len; c++)
            fprintf(stderr, "unexpected collision at %s: %s %s\n", collisions[c].sort,
                    collisions[c].first, collisions[c].second ? collisions[c].second : "");
        free(collisions);
    }
    {
        static Grammar colliding;
        CettaGrammarCanonicalTableV1 *shapes = NULL;
        CettaGrammarCanonicalCollisionV1 *collisions = NULL;
        uint32_t collision_len = 0u;
        char *printed = NULL;
        Atom *number = atom_expr3(&arena, atom_symbol(&arena, "bnf"), atom_symbol(&arena, "num"),
                                  atom_string(&arena, "7"));
        Atom *bracketed = atom_expr3(
            &arena, atom_symbol(&arena, "bnf"), atom_symbol(&arena, "W.1"),
            atom_expr3(&arena, atom_symbol(&arena, "bnf"), atom_symbol(&arena, "id"),
                       atom_string(&arena, "a")));
        collision_grammar_init(&colliding);
        expect(&counts,
               cetta_grammar_canonical_table_build_v1(colliding.productions, colliding.len,
                                                      &shapes, error, sizeof(error)) &&
                   cetta_grammar_canonical_collisions_v1(shapes, &collisions, &collision_len,
                                                         error, sizeof(error)),
               "the collision grammar is classified and checked");
        expect(&counts,
               has_collision(collisions, collision_len,
                             CETTA_GRAMMAR_CANONICAL_SEQUENCE_COLLISION_V1, "S2", "X2", "X3"),
               "two separated lists at one sort are merged if their names are erased");
        expect(&counts,
               has_collision(collisions, collision_len,
                             CETTA_GRAMMAR_CANONICAL_LITERAL_COLLISION_V1, "K2", "Ka", "Kc"),
               "one literal text two ways is an ambiguity");
        expect(&counts,
               has_collision(collisions, collision_len,
                             CETTA_GRAMMAR_CANONICAL_TOKEN_COLLISION_V1, "T2", "Ta", "Tb"),
               "one token class two ways is an ambiguity");
        expect(&counts,
               has_collision(collisions, collision_len, CETTA_GRAMMAR_CANONICAL_PATH_COLLISION_V1,
                             "U2", "Uc", NULL),
               "one rule two ways is an ambiguity");
        expect(&counts, collision_len == 4u,
               "tuples and text groups are constructors, so R, K, T, U and W do not collide");
        if (collision_len != 4u) {
            for (uint32_t c = 0u; c < collision_len; c++)
                fprintf(stderr, "collision %d at %s: %s %s\n", (int)collisions[c].kind,
                        collisions[c].sort, collisions[c].first,
                        collisions[c].second ? collisions[c].second : "");
        }
        expect(&counts,
               shapes &&
                   cetta_grammar_canonical_print_v1(shapes, number,
                                                    symbol_intern_cstr(g_symbols, "W"),
                                                    never_space, NULL, &printed, NULL, error,
                                                    sizeof(error)) &&
                   strcmp(printed, "7") == 0,
               "a value past a bracketing group prints by its own production");
        if (printed && strcmp(printed, "7") != 0)
            fprintf(stderr, "printed %s at W\n", printed);
        free(printed);
        printed = NULL;
        expect(&counts,
               shapes &&
                   cetta_grammar_canonical_print_v1(shapes, bracketed,
                                                    symbol_intern_cstr(g_symbols, "W"),
                                                    never_space, NULL, &printed, NULL, error,
                                                    sizeof(error)) &&
                   strcmp(printed, "[a]") == 0,
               "a value in a bracketing group prints with its text");
        if (printed && strcmp(printed, "[a]") != 0)
            fprintf(stderr, "printed %s at W\n", printed);
        free(printed);
        free(collisions);
        cetta_grammar_canonical_table_free_v1(shapes);
    }
    {
        static Grammar ambiguous;
        CettaGrammarCanonicalTableV1 *two_ways = NULL;
        ambiguous_grammar_init(&ambiguous);
        expect(&counts,
               cetta_grammar_canonical_table_build_v1(ambiguous.productions, ambiguous.len,
                                                      &two_ways, error, sizeof(error)),
               "an ambiguous grammar is classified");
        if (two_ways) {
            Atom *leaf_a = node(&arena, "A", 0, 1, NULL, 0u);
            Atom *leaf_b = node(&arena, "A", 0, 1, NULL, 0u);
            Atom *through_b = node(&arena, "B", 0, 1, &leaf_b, 1u);
            expect_term(&counts, two_ways, &arena, "x", node(&arena, "S.0", 0, 1, &leaf_a, 1u),
                        "S", "(bnf S/0 \"x\")",
                        "the direct derivation of an ambiguous text keeps its alternative");
            expect_term(&counts, two_ways, &arena, "x",
                        node(&arena, "S.1", 0, 1, &through_b, 1u), "S", "(bnf S/1 \"x\")",
                        "the other derivation of the same text is a different term");
            cetta_grammar_canonical_table_free_v1(two_ways);
        }
    }
    {
        static Grammar chain;
        static char names[70][8];
        CettaGrammarCanonicalTableV1 *long_chain = NULL;
        char *printed = NULL;
        Atom *x = atom_expr3(&arena, atom_symbol(&arena, "bnf"), atom_symbol(&arena, "id"),
                             atom_string(&arena, "x"));
        chain_grammar_init(&chain, names, 70u);
        expect(&counts,
               cetta_grammar_canonical_table_build_v1(chain.productions, chain.len, &long_chain,
                                                      error, sizeof(error)) &&
                   cetta_grammar_canonical_print_v1(long_chain, x,
                                                    symbol_intern_cstr(g_symbols, "C0"),
                                                    never_space, NULL, &printed, NULL, error,
                                                    sizeof(error)) &&
                   strcmp(printed, "x") == 0,
               "a value at the end of a 70-rule transparent chain prints");
        free(printed);
        cetta_grammar_canonical_table_free_v1(long_chain);
    }

    {
        static Grammar literal;
        CettaGrammarCanonicalTableV1 *literals = NULL;
        CettaGrammarCanonicalCollisionV1 *collisions = NULL;
        uint32_t collision_len = 0u;
        literal_grammar_init(&literal);
        expect(&counts,
               cetta_grammar_canonical_table_build_v1(literal.productions, literal.len, &literals,
                                                      error, sizeof(error)),
               "the literal grammar is classified");
        if (!literals)
            fprintf(stderr, "literal grammar: %s\n", error);
        if (literals) {
            expect_term(&counts, literals, &arena, "ab", node(&arena, "Y.0", 0, 2, NULL, 0u), "Y",
                        "(bnf Y/0)", "one literal spelling ab is its own constructor");
            expect_term(&counts, literals, &arena, "ab", node(&arena, "Y.1", 0, 2, NULL, 0u), "Y",
                        "(bnf Y/1)", "two literals spelling ab are another constructor");
            expect_term(&counts, literals, &arena, "x", node(&arena, "Z.0", 0, 1, NULL, 0u), "Z",
                        "(bnf Z/0)", "the first of two equal alternatives is named");
            expect_term(&counts, literals, &arena, "x", node(&arena, "Z.1", 0, 1, NULL, 0u), "Z",
                        "(bnf Z/1)", "the second of two equal alternatives is named");
            expect_term(&counts, literals, &arena, "+", node(&arena, "O2.0", 0, 1, NULL, 0u), "O2",
                        "\"+\"", "a literal-only alternative nothing else spells stays text");
            expect(&counts,
                   cetta_grammar_canonical_collisions_v1(literals, &collisions, &collision_len,
                                                         error, sizeof(error)) &&
                       has_collision(collisions, collision_len,
                                     CETTA_GRAMMAR_CANONICAL_LITERAL_COLLISION_V1, "Y", "Y", "Y") &&
                       has_collision(collisions, collision_len,
                                     CETTA_GRAMMAR_CANONICAL_LITERAL_COLLISION_V1, "Z", "Z", "Z") &&
                       collision_len == 2u,
                   "the resolved literal ambiguities are still reported");
            free(collisions);
            cetta_grammar_canonical_table_free_v1(literals);
        }
    }
    cetta_grammar_canonical_table_free_v1(table);
    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    printf("grammar canonical term: %u passed, %u failed\n", counts.passed, counts.failed);
    return counts.failed == 0u ? 0 : 1;
}

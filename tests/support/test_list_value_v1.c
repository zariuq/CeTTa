#include "atom.h"
#include "match.h"
#include "symbol.h"
#include "term_universe.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The list value: [x1 ... xn] is the expression (LIST x1 ... xn) whose head
 * is a sealed internal tag, and [x1 ... xk | rest] is (LIST_REST x1 ... xk
 * rest).  These cases check the atom contract: construction and its normal
 * form, printing, equality and hashing apart from expressions, sharing, and
 * copying.
 */

static int passed;
static int failed;

static void expect(bool ok, const char *what) {
    if (ok) {
        passed++;
    } else {
        failed++;
        fprintf(stderr, "FAIL: %s\n", what);
    }
}

static void expect_text(Arena *a, Atom *atom, const char *text,
                        const char *what) {
    char *got = atom ? atom_to_string(a, atom) : NULL;
    if (got && strcmp(got, text) == 0) {
        passed++;
    } else {
        failed++;
        fprintf(stderr, "FAIL: %s: got %s, expected %s\n", what,
                got ? got : "(null)", text);
    }
}

static Atom *sym(Arena *a, const char *name) {
    return atom_symbol(a, name);
}

static Atom *list_of(Arena *a, int n, ...) {
    Atom *elems[16];
    va_list args;
    va_start(args, n);
    for (int i = 0; i < n; i++)
        elems[i] = va_arg(args, Atom *);
    va_end(args);
    return atom_list(a, elems, (CettaExprLen)n);
}

static Atom *expr_of(Arena *a, int n, ...) {
    Atom *elems[16];
    va_list args;
    va_start(args, n);
    for (int i = 0; i < n; i++)
        elems[i] = va_arg(args, Atom *);
    va_end(args);
    return atom_expr(a, elems, (CettaExprLen)n);
}

static void construction_and_printing(Arena *a) {
    Atom *f = sym(a, "f"), *x = sym(a, "a"), *b = sym(a, "b"), *c = sym(a, "c");
    Atom *abc = list_of(a, 3, x, b, c);
    expect(atom_is_list(abc) && atom_list_len(abc) == 3u &&
               atom_list_elems(abc)[0] == x && atom_list_elems(abc)[2] == c,
           "[a b c] is a list of three elements");
    expect_text(a, abc, "[a b c]", "a list prints its elements in brackets");
    expect_text(a, list_of(a, 0), "[]", "the empty list prints as []");
    expect_text(a, list_of(a, 2, list_of(a, 1, x), expr_of(a, 2, f, x)),
                "[[a] (f a)]", "nested lists and expressions print as written");
    Atom *t = atom_var(a, "t");
    Atom *prefix[2] = {x, b};
    Atom *open = atom_list_with_rest(a, prefix, 2u, t);
    expect(atom_is_list_rest(open) && !atom_is_list(open),
           "[a b | $t] is a list pattern, not a list");
    expect_text(a, open, "[a b | $t]", "a list pattern prints its rest after a bar");
    expect(atom_list_with_rest(a, NULL, 0u, t) == t, "[| $t] is $t");
}

static void normal_form(Arena *a) {
    Atom *x = sym(a, "a"), *b = sym(a, "b"), *c = sym(a, "c");
    Atom *t = atom_var(a, "t");
    Atom *one[1] = {x};
    Atom *closed = atom_list_with_rest(a, one, 1u, list_of(a, 2, b, c));
    expect(atom_is_list(closed) && atom_eq(closed, list_of(a, 3, x, b, c)),
           "[a | [b c]] is [a b c]");
    Atom *two[1] = {b};
    Atom *open = atom_list_with_rest(a, one, 1u, atom_list_with_rest(a, two, 1u, t));
    Atom *ab[2] = {x, b};
    expect(atom_is_list_rest(open) && open->expr.len == 4u &&
               atom_eq(open, atom_list_with_rest(a, ab, 2u, t)),
           "[a | [b | $t]] is [a b | $t]");
    /* Substitution rebuilds through the constructors; they keep the form. */
    Atom *parts[3] = {atom_internal_tag(a, CETTA_INTERNAL_TAG_LIST_REST), x,
                      list_of(a, 1, b)};
    Atom *rebuilt = atom_expr(a, parts, 3u);
    expect(atom_is_list(rebuilt) && atom_eq(rebuilt, list_of(a, 2, x, b)),
           "constructing (LIST_REST a [b]) gives [a b]");
    Atom *draft = atom_expr_builder_begin(a, 3u);
    draft->expr.elems[0] = parts[0];
    draft->expr.elems[1] = x;
    draft->expr.elems[2] = list_of(a, 0);
    Atom *finished = atom_expr_builder_finish(a, draft);
    expect(atom_is_list(finished) && atom_eq(finished, list_of(a, 1, x)),
           "the expression builder gives [a] for (LIST_REST a [])");
    Atom *tail = atom_list_tail(a, list_of(a, 3, x, b, c), 1u);
    expect(atom_eq(tail, list_of(a, 2, b, c)), "the tail of [a b c] from 1 is [b c]");
    expect(atom_eq(atom_list_tail(a, list_of(a, 1, x), 5u), list_of(a, 0)),
           "a tail past the end is []");
}

static void distinct_from_expressions(Arena *a) {
    Atom *f = sym(a, "f"), *x = sym(a, "a");
    Atom *fa_list = list_of(a, 2, f, x);
    Atom *fa_expr = expr_of(a, 2, f, x);
    expect(!atom_eq(fa_list, fa_expr), "[f a] and (f a) are different atoms");
    expect(!atom_eq(list_of(a, 0), expr_of(a, 0)), "[] and () are different atoms");
    expect(atom_hash(fa_list) != atom_hash(fa_expr), "[f a] and (f a) hash apart");
    expect(atom_eq(fa_list, list_of(a, 2, f, x)) &&
               atom_hash(fa_list) == atom_hash(list_of(a, 2, f, x)),
           "lists with equal elements are equal and hash alike");
    expect(!atom_eq(list_of(a, 2, x, x), list_of(a, 1, x)),
           "multiplicity is kept: [a a] is not [a]");
    Atom *t = atom_var(a, "t");
    Atom *one[1] = {f};
    Atom *open = atom_list_with_rest(a, one, 1u, t);
    expect(!atom_eq(open, list_of(a, 2, f, t)),
           "[f | $t] is not [f $t]");
}

static void term_stability(Arena *a) {
    Atom *tag = atom_internal_tag(a, CETTA_INTERNAL_TAG_LIST);
    Atom *rest_tag = atom_internal_tag(a, CETTA_INTERNAL_TAG_LIST_REST);
    expect(tag == atom_internal_tag(NULL, CETTA_INTERNAL_TAG_LIST) && tag->arena_id == 0u,
           "every list shares one tag owned by no arena");
    expect(atom_grounded_is_term_stable(tag) && atom_grounded_is_term_stable(rest_tag),
           "the list tags are terms");
    Atom *carrier = atom_internal_tag(a, CETTA_INTERNAL_TAG_PETTA_OPEN_CONS);
    expect(!atom_grounded_is_term_stable(carrier),
           "a machine carrier tag is not a term");
    Atom copy = *tag;
    copy.flags &= ~ATOM_FLAG_HASH_VALID;
    expect(atom_hash(&copy) == tag->hash_cache,
           "the list tag's precomputed hash is its computed hash");
    Atom rest_copy = *rest_tag;
    rest_copy.flags &= ~ATOM_FLAG_HASH_VALID;
    expect(atom_hash(&rest_copy) == rest_tag->hash_cache,
           "the rest tag's precomputed hash is its computed hash");
    Atom *ab = list_of(a, 2, sym(a, "a"), sym(a, "b"));
    expect((ab->flags & ATOM_FLAG_HASH_STABLE) != 0u &&
               (ab->flags & ATOM_FLAG_HASHCONS_ELIGIBLE) != 0u,
           "a list of symbols is hash-stable and shareable");
    Atom *one[1] = {sym(a, "a")};
    Atom *open = atom_list_with_rest(a, one, 1u, atom_var(a, "t"));
    Atom *wrapped = expr_of(a, 2, sym(a, "f"), open);
    expect(atom_structural_may_have_open_list(open) &&
               atom_structural_may_have_open_list(wrapped) &&
               !atom_structural_may_have_open_list(ab),
           "the open-list fact holds exactly where a list pattern occurs");
}

static void sharing_and_copying(Arena *a) {
    HashConsTable hc;
    hashcons_init(&hc);
    /* Sharing is bottom-up: an expression is admitted once its children are. */
    Atom *x = hashcons_get(&hc, sym(a, "a")), *b = hashcons_get(&hc, sym(a, "b"));
    expect(x->arena_id == 0u && b->arena_id == 0u, "symbols are shared first");
    Atom *first = hashcons_get(&hc, list_of(a, 2, x, b));
    Atom *second = hashcons_get(&hc, list_of(a, 2, x, b));
    expect(first && first == second && first->arena_id == 0u,
           "equal lists share one hash-consed atom");
    Atom *expr = hashcons_get(&hc, expr_of(a, 2, x, b));
    expect(expr && expr->arena_id == 0u && expr != first,
           "a list and an expression are shared apart");
    Arena dst;
    arena_init(&dst);
    Atom *nested = list_of(a, 2, x, list_of(a, 1, b));
    Atom *moved = atom_deep_copy(&dst, nested);
    expect(atom_is_list(moved) && atom_eq(moved, nested) &&
               moved->expr.elems[0] == atom_internal_tag(NULL, CETTA_INTERNAL_TAG_LIST),
           "a deep copy of a list is an equal list with the shared tag");
    arena_free(&dst);
    hashcons_free(&hc);
}

static Atom *rest_of(Arena *a, int n, Atom *rest, ...) {
    Atom *elems[16];
    va_list args;
    va_start(args, rest);
    for (int i = 0; i < n; i++)
        elems[i] = va_arg(args, Atom *);
    va_end(args);
    return atom_list_with_rest(a, elems, (CettaExprLen)n, rest);
}

/* The value a match gave a variable, fully substituted. */
static Atom *value_of(Bindings *b, Arena *a, Atom *var) {
    return bindings_apply(b, a, var);
}

static bool matches(Arena *a, Atom *left, Atom *right, Bindings *b) {
    bindings_init(b);
    return match_atoms(left, right, b, a);
}

static void expect_no_match(Arena *a, Atom *left, Atom *right, const char *what) {
    Bindings b;
    expect(!matches(a, left, right, &b), what);
    bindings_free(&b);
}

static void matching(Arena *a) {
    Atom *f = sym(a, "f"), *x = sym(a, "a"), *y = sym(a, "b"), *z = sym(a, "c");
    Atom *h = atom_var(a, "h"), *t = atom_var(a, "t"), *u = atom_var(a, "u");
    Atom *v = atom_var(a, "v");
    Bindings b;

    expect(matches(a, list_of(a, 2, x, v), list_of(a, 2, x, y), &b) &&
               atom_eq(value_of(&b, a, v), y),
           "[a $v] matches [a b] with $v = b");
    bindings_free(&b);
    expect_no_match(a, expr_of(a, 2, v, x), list_of(a, 1, x),
                    "($v a) does not match [a]: no variable binds the list tag");
    expect_no_match(a, list_of(a, 1, x), expr_of(a, 2, v, x),
                    "[a] does not match ($v a)");
    expect_no_match(a, list_of(a, 2, f, x), expr_of(a, 2, f, x), "[f a] does not match (f a)");
    expect_no_match(a, list_of(a, 0), expr_of(a, 0), "[] does not match ()");
    expect_no_match(a, list_of(a, 2, x, y), list_of(a, 3, x, y, z),
                    "lists of different lengths do not match");

    expect(matches(a, rest_of(a, 1, t, h), list_of(a, 3, x, y, z), &b) &&
               atom_eq(value_of(&b, a, h), x) &&
               atom_eq(value_of(&b, a, t), list_of(a, 2, y, z)),
           "[$h | $t] matches [a b c] with $h = a and $t = [b c]");
    bindings_free(&b);
    expect(matches(a, list_of(a, 3, x, y, z), rest_of(a, 1, t, h), &b) &&
               atom_eq(value_of(&b, a, t), list_of(a, 2, y, z)),
           "matching is symmetric: [a b c] matches [$h | $t]");
    bindings_free(&b);
    expect(matches(a, rest_of(a, 1, t, h), list_of(a, 1, x), &b) &&
               atom_eq(value_of(&b, a, t), list_of(a, 0)),
           "[$h | $t] matches [a] with $t = []");
    bindings_free(&b);
    expect_no_match(a, rest_of(a, 1, t, h), list_of(a, 0), "[$h | $t] does not match []");
    expect_no_match(a, rest_of(a, 1, t, h), expr_of(a, 2, x, y),
                    "[$h | $t] does not match the expression (a b)");

    expect(matches(a, rest_of(a, 2, t, v, v), list_of(a, 3, x, x, y), &b) &&
               atom_eq(value_of(&b, a, v), x) &&
               atom_eq(value_of(&b, a, t), list_of(a, 1, y)),
           "[$v $v | $t] matches [a a b]: a consistent repeated variable");
    bindings_free(&b);
    expect_no_match(a, rest_of(a, 2, t, v, v), list_of(a, 3, x, y, z),
                    "[$v $v | $t] does not match [a b c]: an inconsistent one");

    expect(matches(a, rest_of(a, 1, t, x), rest_of(a, 2, u, x, y), &b) &&
               atom_eq(value_of(&b, a, t), rest_of(a, 1, u, y)),
           "[a | $t] matches [a b | $u] with $t = [b | $u]");
    bindings_free(&b);
    expect(matches(a, rest_of(a, 1, t, x), rest_of(a, 1, u, x), &b) &&
               atom_eq(value_of(&b, a, t), value_of(&b, a, u)),
           "[a | $t] matches [a | $u] by joining the rests");
    bindings_free(&b);

    /* Nested lists bind at every depth. */
    Atom *p = atom_var(a, "p"), *q = atom_var(a, "q"), *r = atom_var(a, "r");
    Atom *one = atom_int(a, 1), *two = atom_int(a, 2), *three = atom_int(a, 3);
    Atom *pattern = expr_of(a, 2, sym(a, "foo"), rest_of(a, 1, r, list_of(a, 2, p, q)));
    Atom *data = expr_of(a, 2, sym(a, "foo"),
                         list_of(a, 2, list_of(a, 2, one, two), list_of(a, 1, three)));
    expect(matches(a, pattern, data, &b) && atom_eq(value_of(&b, a, p), one) &&
               atom_eq(value_of(&b, a, q), two) &&
               atom_eq(value_of(&b, a, r), list_of(a, 1, list_of(a, 1, three))),
           "(foo [[$p $q] | $r]) binds inside nested lists");
    bindings_free(&b);

    /* Substitution into a rest rebuilds one list. */
    expect(matches(a, list_of(a, 2, h, t), list_of(a, 2, x, list_of(a, 2, y, z)), &b) &&
               atom_eq(bindings_apply(&b, a, rest_of(a, 1, t, h)), list_of(a, 3, x, y, z)),
           "applying $h = a, $t = [b c] to [$h | $t] gives [a b c]");
    bindings_free(&b);

    /* One-way matching, as case does it. */
    BindingsBuilder bb;
    bindings_builder_init(&bb, NULL);
    expect(simple_match_builder(rest_of(a, 1, t, h), list_of(a, 2, x, y), &bb, a) &&
               atom_eq(bindings_apply((Bindings *)bindings_builder_bindings(&bb), a, t),
                       list_of(a, 1, y)),
           "one-way matching binds a list pattern's rest");
    bindings_builder_free(&bb);

    /* The rule-side matcher, as equation dispatch does it. */
    Bindings eb;
    bindings_init(&eb);
    Atom *rule = expr_of(a, 2, sym(a, "len"), rest_of(a, 1, t, h));
    expect(match_atoms_epoch(expr_of(a, 2, sym(a, "len"), list_of(a, 3, x, y, z)),
                             rule, &eb, a, 29u) &&
               atom_eq(bindings_apply_epoch(&eb, a, t, 29u), list_of(a, 2, y, z)),
           "equation dispatch binds a head's list rest");
    bindings_free(&eb);
    bindings_init(&eb);
    expect(!match_atoms_epoch(expr_of(a, 2, sym(a, "len"), expr_of(a, 2, x, y)),
                              rule, &eb, a, 31u),
           "a head's list pattern does not match an expression argument");
    bindings_free(&eb);

    /* The rule-local form, as the PeTTa machine activates an equation. */
    BindingsBuilder rb;
    bindings_builder_init(&rb, NULL);
    expect(match_atoms_epoch_builder_rule_local(
               expr_of(a, 2, sym(a, "len"), list_of(a, 3, x, y, z)), rule, &rb, a,
               37u),
           "a rule-local activation binds a head's list rest");
    bindings_builder_free(&rb);
}

/* A rest is built in the matcher's arena from the list's elements; like any
 * value, it survives its source once promoted into the arena that keeps it. */
static void rest_outlives_its_source(void) {
    Arena source, work, keep;
    arena_init(&source);
    arena_init(&work);
    arena_init(&keep);
    Atom *nested = list_of(&source, 1, expr_of(&source, 2, sym(&source, "g"), sym(&source, "b")));
    Atom *list = list_of(&source, 3, sym(&source, "a"), nested, sym(&source, "c"));
    Atom *h = atom_var(&work, "h"), *t = atom_var(&work, "t");
    Bindings b;
    bindings_init(&b);
    bool ok = match_atoms(rest_of(&work, 1, t, h), list, &b, &work);
    Atom *kept = ok ? atom_deep_copy(&keep, bindings_apply(&b, &work, t)) : NULL;
    bindings_free(&b);
    arena_free(&source);
    arena_free(&work);
    Arena check;
    arena_init(&check);
    Atom *expected = list_of(&check, 2,
                             list_of(&check, 1, expr_of(&check, 2, sym(&check, "g"),
                                                        sym(&check, "b"))),
                             sym(&check, "c"));
    expect(kept && atom_eq(kept, expected),
           "a promoted rest keeps its nested elements after its source is freed");
    arena_free(&check);
    arena_free(&keep);
}

/* A space's term store keeps a list as an encoded term: stored, looked up
 * and read back, it is the same list. */
static void term_store(Arena *a) {
    Arena persistent;
    arena_init(&persistent);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);
    Atom *fact = expr_of(a, 2, sym(a, "fact"),
                         list_of(a, 2, atom_int(a, 1), atom_int(a, 2)));
    AtomId id = term_universe_store_atom_id(&universe, a, fact);
    Atom *back = id != CETTA_ATOM_ID_NONE ? term_universe_get_atom(&universe, id) : NULL;
    expect_text(a, back, "(fact [1 2])", "a stored list reads back as the same list");
    expect(back && atom_eq(back, fact), "a stored list reads back equal");
    expect(id != CETTA_ATOM_ID_NONE &&
               term_universe_lookup_atom_id(&universe, fact) == id,
           "a stored list is found again by value");
    expect(id != CETTA_ATOM_ID_NONE && tu_hdr(&universe, id) != NULL,
           "a list is encoded, not kept as an opaque fallback entry");
    AtomId again = term_universe_store_atom_id(
        &universe, a, expr_of(a, 2, sym(a, "fact"),
                              list_of(a, 2, atom_int(a, 1), atom_int(a, 2))));
    expect(again == id, "storing an equal list again shares its entry");
    AtomId expr_id = term_universe_store_atom_id(
        &universe, a, expr_of(a, 2, sym(a, "fact"),
                              expr_of(a, 2, atom_int(a, 1), atom_int(a, 2))));
    expect(expr_id != CETTA_ATOM_ID_NONE && expr_id != id,
           "a list and an expression are stored apart");
    /* A stored list pattern, as an equation head keeps one, meets a list
     * query by its encoded id. */
    Atom *h = atom_var(a, "h"), *t = atom_var(a, "t");
    Atom *head = expr_of(a, 2, sym(a, "len"), rest_of(a, 1, t, h));
    AtomId head_id = term_universe_store_atom_id(&universe, a, head);
    Bindings b;
    bindings_init(&b);
    Atom *query = expr_of(a, 2, sym(a, "len"),
                          list_of(a, 3, sym(a, "a"), sym(a, "b"), sym(a, "c")));
    expect(head_id != CETTA_ATOM_ID_NONE &&
               match_atoms_atom_id_epoch(query, &universe, head_id, &b, a, 41u),
           "a stored list pattern matches a list query by id");
    bindings_free(&b);
    bindings_init(&b);
    expect(!match_atoms_atom_id_epoch(
               expr_of(a, 2, sym(a, "len"), list_of(a, 0)), &universe, head_id,
               &b, a, 43u),
           "a stored list pattern does not match []");
    bindings_free(&b);
    term_universe_free(&universe);
    arena_free(&persistent);
}

int main(void) {
    SymbolTable symbols;
    Arena arena;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    arena_init(&arena);

    construction_and_printing(&arena);
    normal_form(&arena);
    distinct_from_expressions(&arena);
    term_stability(&arena);
    sharing_and_copying(&arena);
    matching(&arena);
    rest_outlives_its_source();
    term_store(&arena);

    arena_free(&arena);
    printf("list value: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}

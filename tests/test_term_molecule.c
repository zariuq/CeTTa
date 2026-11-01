#define _GNU_SOURCE
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "space_skeleton.h"
#include "stats.h"
#include "symbol.h"
#include "term_molecule.h"
#include "tests/test_runtime_stats_stubs.h"

/* Molecules: stored terms read under frames, unified and instantiated by
 * ids.  A term read under two frames is renamed apart; unification checks
 * occurrence; a mark undoes bindings; atoms whose equality is not their
 * identity decline; a kept fact unifies as its record does, walked or with
 * the pattern aligned once with its spine; an instance's key and holes are
 * the alpha key and cut its atom has, a stored key read under a free frame
 * being its own; a term already its alpha key is its own key; a store
 * refused a growth stays one a later branch may use.  Random pairs of terms
 * unify as a reference unifier over atoms unifies them. */

static Arena g_arena;
static TermUniverse g_universe;

/* The test links realloc wrapped: a growth of G_REFUSED, once armed, is
 * refused. */
void *__real_realloc(void *pointer, size_t bytes);
static void *g_refused;
void *__wrap_realloc(void *pointer, size_t bytes) {
    if (g_refused && pointer == g_refused) {
        g_refused = NULL;
        return NULL;
    }
    return __real_realloc(pointer, bytes);
}

static Atom *sym(const char *name) { return atom_symbol(&g_arena, name); }
static Atom *var(const char *name) { return atom_var(&g_arena, name); }
static Atom *num(int64_t value) { return atom_int(&g_arena, value); }
static Atom *ex(int n, ...) {
    Atom *elems[16];
    va_list args;
    va_start(args, n);
    for (int i = 0; i < n; i++)
        elems[i] = va_arg(args, Atom *);
    va_end(args);
    return atom_expr(&g_arena, elems, (CettaExprLen)n);
}

static AtomId store(Atom *atom) {
    AtomId id = term_universe_store_atom_id(&g_universe, NULL, atom);
    assert(id != CETTA_ATOM_ID_NONE);
    return id;
}

static AtomId key_of(Atom *atom) {
    AtomId id = term_universe_store_alpha_key(&g_universe, atom);
    assert(id != CETTA_ATOM_ID_NONE);
    return id;
}

static AtomId mol_key(CettaMolStore *m, AtomId id, uint32_t frame) {
    CettaMolNumbering numbering = {.len = 0u};
    AtomId key = CETTA_ATOM_ID_NONE;
    assert(cetta_mol_key(m, (CettaMol){id, frame}, &numbering, &key) ==
           CETTA_MOL_OK);
    return key;
}

/* ── A reference unifier over atoms ── */

typedef struct {
    VarId vars[64];
    Atom *values[64];
    int len;
} Subst;

static Atom *walk(const Subst *s, Atom *a) {
    while (a->kind == ATOM_VAR) {
        int i = 0;
        while (i < s->len && !var_id_eq(s->vars[i], a->var_id))
            i++;
        if (i == s->len)
            return a;
        a = s->values[i];
    }
    return a;
}

static bool occurs(const Subst *s, VarId v, Atom *a) {
    a = walk(s, a);
    if (a->kind == ATOM_VAR)
        return var_id_eq(a->var_id, v);
    if (a->kind != ATOM_EXPR)
        return false;
    for (CettaExprIndex i = 0u; i < a->expr.len; i++)
        if (occurs(s, v, a->expr.elems[i]))
            return true;
    return false;
}

static bool ref_unify(Subst *s, Atom *a, Atom *b) {
    a = walk(s, a);
    b = walk(s, b);
    if (a->kind == ATOM_VAR && b->kind == ATOM_VAR &&
        var_id_eq(a->var_id, b->var_id))
        return true;
    if (a->kind == ATOM_VAR || b->kind == ATOM_VAR) {
        Atom *v = a->kind == ATOM_VAR ? a : b;
        Atom *t = a->kind == ATOM_VAR ? b : a;
        if (occurs(s, v->var_id, t))
            return false;
        assert(s->len < 64);
        s->vars[s->len] = v->var_id;
        s->values[s->len++] = t;
        return true;
    }
    if (a->kind != ATOM_EXPR || b->kind != ATOM_EXPR)
        return atom_eq(a, b);
    if (a->expr.len != b->expr.len)
        return false;
    for (CettaExprIndex i = 0u; i < a->expr.len; i++)
        if (!ref_unify(s, a->expr.elems[i], b->expr.elems[i]))
            return false;
    return true;
}

static Atom *apply(const Subst *s, Atom *a) {
    a = walk(s, a);
    if (a->kind != ATOM_EXPR)
        return a;
    Atom *elems[16];
    for (CettaExprIndex i = 0u; i < a->expr.len; i++)
        elems[i] = apply(s, a->expr.elems[i]);
    return atom_expr(&g_arena, elems, a->expr.len);
}

/* ── Random terms ── */

static uint64_t g_seed = 0x2545F4914F6CDD1DULL;
static uint32_t rnd(uint32_t n) {
    g_seed = g_seed * 6364136223846793005ULL + 1442695040888963407ULL;
    return (uint32_t)(g_seed >> 33) % n;
}

/* A random term over f/2, g/1, a, b, 1 and the variables of SET. */
static Atom *random_term(int depth, const char *const *names) {
    uint32_t pick = rnd(depth > 0 ? 7u : 4u);
    switch (pick) {
    case 0: return sym("a");
    case 1: return num(1);
    case 2: return var(names[rnd(3u)]);
    case 3: return var(names[rnd(3u)]);
    case 4: return ex(3, sym("f"), random_term(depth - 1, names),
                      random_term(depth - 1, names));
    case 5: return ex(2, sym("g"), random_term(depth - 1, names));
    default: return ex(3, sym("f"), random_term(depth - 1, names), sym("b"));
    }
}

int main(void) {
    SymbolTable symbols;
    VarInternTable var_intern;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    var_intern_init(&var_intern);
    g_var_intern = &var_intern;
    arena_init(&g_arena);
    Arena persistent;
    arena_init(&persistent);
    term_universe_init(&g_universe);
    term_universe_set_persistent_arena(&g_universe, &persistent);
    CettaMolStore m;
    cetta_mol_store_init(&m, &g_universe);

    /* Two open terms, each under its own frame: (f $x (g $y)) and
     * (f (h $z) $w) unify to (f (h $1) (g $2)). */
    AtomId left = store(ex(3, sym("f"), var("x"), ex(2, sym("g"), var("y"))));
    AtomId right = store(ex(3, sym("f"), ex(2, sym("h"), var("z")), var("w")));
    uint32_t fl = cetta_mol_frame(&m), fr = cetta_mol_frame(&m);
    CettaMolMark start = cetta_mol_mark(&m);
    assert(cetta_mol_unify(&m, (CettaMol){left, fl}, (CettaMol){right, fr}) ==
           CETTA_MOL_OK);
    AtomId both = key_of(ex(3, sym("f"), ex(2, sym("h"), var("p")),
                            ex(2, sym("g"), var("q"))));
    assert(mol_key(&m, left, fl) == both && mol_key(&m, right, fr) == both);
    /* Undone, the variables are free again. */
    cetta_mol_undo(&m, start);
    assert(mol_key(&m, left, fl) == key_of(ex(3, sym("f"), var("p"),
                                              ex(2, sym("g"), var("q")))));
    cetta_mol_undo(&m, start);

    /* The same term under two frames is renamed apart: (p $x $x) meets
     * (p a $x) and (p $x b) as two reads, so a and b cannot both hold. */
    AtomId twice = store(ex(3, sym("p"), var("x"), var("x")));
    AtomId one = store(ex(3, sym("p"), sym("a"), var("x")));
    AtomId other = store(ex(3, sym("p"), var("x"), sym("b")));
    uint32_t f1 = cetta_mol_frame(&m), f2 = cetta_mol_frame(&m),
             f3 = cetta_mol_frame(&m);
    assert(cetta_mol_unify(&m, (CettaMol){twice, f1}, (CettaMol){one, f2}) ==
           CETTA_MOL_OK);
    assert(cetta_mol_unify(&m, (CettaMol){twice, f1}, (CettaMol){other, f3}) ==
           CETTA_MOL_FAIL);
    cetta_mol_undo(&m, start);
    /* Under one frame a variable is one cell: (p a $x) and (p $x b) cannot
     * meet, and (p a $x) and (p $x $x) meet as (p a a). */
    assert(cetta_mol_unify(&m, (CettaMol){one, f1}, (CettaMol){other, f1}) ==
           CETTA_MOL_FAIL);
    cetta_mol_undo(&m, start);
    assert(cetta_mol_unify(&m, (CettaMol){one, f1}, (CettaMol){twice, f1}) ==
           CETTA_MOL_OK);
    assert(mol_key(&m, twice, f1) == store(ex(3, sym("p"), sym("a"), sym("a"))));
    cetta_mol_undo(&m, start);

    /* The occurs check: $x meets (g $x) under one frame. */
    AtomId loop = store(ex(3, sym("p"), var("x"), ex(2, sym("g"), var("x"))));
    AtomId same = store(ex(3, sym("p"), var("y"), var("y")));
    uint32_t f4 = cetta_mol_frame(&m), f5 = cetta_mol_frame(&m);
    assert(cetta_mol_unify(&m, (CettaMol){loop, f4}, (CettaMol){same, f5}) ==
           CETTA_MOL_FAIL);
    cetta_mol_undo(&m, start);

    /* A value whose equality is not its identity declines. */
    AtomId real = store(ex(2, sym("v"), atom_float(&g_arena, 1.0)));
    assert(cetta_mol_unify(&m, (CettaMol){real, CETTA_MOL_GROUND},
                           (CettaMol){real, CETTA_MOL_GROUND}) ==
           CETTA_MOL_DECLINE);
    /* Ground terms equal only to themselves compare by id. */
    AtomId g1 = store(ex(3, sym("f"), sym("a"), num(1)));
    AtomId g2 = store(ex(3, sym("f"), sym("a"), num(2)));
    assert(cetta_mol_unify(&m, (CettaMol){g1, CETTA_MOL_GROUND},
                           (CettaMol){g1, CETTA_MOL_GROUND}) == CETTA_MOL_OK);
    assert(cetta_mol_unify(&m, (CettaMol){g1, CETTA_MOL_GROUND},
                           (CettaMol){g2, CETTA_MOL_GROUND}) == CETTA_MOL_FAIL);

    /* A kept fact: (sol 5 1 (C (/ (> $u s) $u) (M I))) as the skeleton
     * (sol _ _ (C (/ _ _) _)) and its holes, cut from its alpha key. */
    Atom *fact = ex(4, sym("sol"), num(5), num(1),
                    ex(3, sym("C"),
                       ex(3, sym("/"), ex(3, sym(">"), var("u"), sym("s")),
                          var("u")),
                       ex(2, sym("M"), sym("I"))));
    Atom *other_fact = ex(4, sym("sol"), num(4), num(2),
                          ex(3, sym("C"), ex(3, sym("/"), sym("p"), sym("q")),
                             sym("I")));
    CettaSkeleton sk;
    assert(cetta_skeleton_of(&g_universe, store(fact), &sk));
    assert(cetta_skeleton_generalize(&sk, &g_universe, store(other_fact)));
    assert(sk.holes == 5u);
    AtomId holes[CETTA_SKELETON_NODES];
    bool missing = false;
    assert(cetta_skeleton_cut_atom(&g_universe, &sk, fact, true, holes,
                                   &missing));
    AtomId fact_key = key_of(fact);
    Atom *patterns[3] = {
        ex(4, sym("sol"), var("a"), var("b"),
           ex(3, sym("C"), ex(3, sym("/"), var("c"), var("d")), var("e"))),
        ex(4, sym("sol"), num(5), var("b"), var("whole")),
        ex(4, sym("sol"), var("a"), var("b"),
           ex(3, sym("C"), ex(3, sym("/"), ex(3, sym(">"), sym("t"), var("s")),
                              var("d")), var("e"))),
    };
    CettaMolResult expected[3] = {CETTA_MOL_OK, CETTA_MOL_OK, CETTA_MOL_OK};
    for (int k = 0; k < 3; k++) {
        AtomId pattern = store(patterns[k]);
        uint32_t fp = cetta_mol_frame(&m), ff = cetta_mol_frame(&m);
        CettaMolResult kept = cetta_mol_unify_kept(
            &m, (CettaMol){pattern, fp}, &sk, holes, ff);
        assert(kept == expected[k]);
        AtomId by_kept = mol_key(&m, pattern, fp);
        cetta_mol_undo(&m, start);
        /* Aligned once with the spine, the pattern unifies with the holes
         * as the walk does. */
        fp = cetta_mol_frame(&m);
        CettaMolAligned aligned;
        CettaMolResult align = cetta_mol_align(&m, (CettaMol){pattern, fp},
                                               &sk, &aligned);
        ff = cetta_mol_frame(&m);
        if (align == CETTA_MOL_OK) {
            assert(cetta_mol_unify_aligned(&m, &aligned, holes, ff) == kept);
            assert(mol_key(&m, pattern, fp) == by_kept);
        } else {
            /* A variable meeting the spine leaves the facts to the walk. */
            assert(align == CETTA_MOL_DECLINE && k == 1);
        }
        cetta_mol_undo(&m, start);
        fp = cetta_mol_frame(&m);
        ff = cetta_mol_frame(&m);
        assert(cetta_mol_unify(&m, (CettaMol){pattern, fp},
                               (CettaMol){fact_key, ff}) == kept);
        assert(mol_key(&m, pattern, fp) == by_kept);
        cetta_mol_undo(&m, start);
    }
    /* A pattern of another spine fails against the kept fact. */
    AtomId apart = store(ex(4, sym("sol"), var("a"), var("b"),
                            ex(3, sym("D"), var("x"), var("y"))));
    uint32_t fa = cetta_mol_frame(&m), fb = cetta_mol_frame(&m);
    assert(cetta_mol_unify_kept(&m, (CettaMol){apart, fa}, &sk, holes, fb) ==
           CETTA_MOL_FAIL);
    /* Aligned, it meets no fact of the spine at all. */
    CettaMolAligned none;
    assert(cetta_mol_align(&m, (CettaMol){apart, fa}, &sk, &none) ==
           CETTA_MOL_FAIL);
    cetta_mol_undo(&m, start);

    /* An instance's holes are the cut of its atom: the template
     * (sol 4 $h (C (/ (> $a $b) (/ $a $c)) (M $f))) under b = (> p s),
     * c = $z, f = I, cut by the skeleton. */
    AtomId template = store(ex(4, sym("sol"), num(4), var("h"),
                               ex(3, sym("C"),
                                  ex(3, sym("/"),
                                     ex(3, sym(">"), var("a"), var("b")),
                                     ex(3, sym("/"), var("a"), var("c"))),
                                  ex(2, sym("M"), var("f")))));
    AtomId binding = store(ex(5, sym("bind"), var("h"), var("b"), var("c"),
                              var("f")));
    AtomId values = store(ex(5, sym("bind"), num(3), ex(3, sym(">"), sym("p"),
                                                         sym("s")),
                             var("z"), sym("I")));
    uint32_t ft = cetta_mol_frame(&m), fv = cetta_mol_frame(&m);
    assert(cetta_mol_unify(&m, (CettaMol){binding, ft},
                           (CettaMol){values, fv}) == CETTA_MOL_OK);
    AtomId made[CETTA_SKELETON_NODES];
    assert(cetta_mol_key_holes(&m, (CettaMol){template, ft}, &sk, made) ==
           CETTA_MOL_OK);
    Atom *instance = ex(4, sym("sol"), num(4), num(3),
                        ex(3, sym("C"),
                           ex(3, sym("/"),
                              ex(3, sym(">"), var("n"),
                                 ex(3, sym(">"), sym("p"), sym("s"))),
                              ex(3, sym("/"), var("n"), var("z"))),
                           ex(2, sym("M"), sym("I"))));
    AtomId cut[CETTA_SKELETON_NODES];
    assert(cetta_skeleton_cut_atom(&g_universe, &sk, instance, true, cut,
                                   &missing));
    assert(memcmp(made, cut, sk.holes * sizeof(*cut)) == 0);
    /* An instance of another spine does not fit. */
    assert(cetta_mol_key_holes(&m, (CettaMol){apart, ft}, &sk, made) ==
           CETTA_MOL_FAIL);
    cetta_mol_undo(&m, start);

    /* A stored key read under a frame with no bound cell is its own key. */
    AtomId shared = key_of(ex(4, sym("fact"),
                              ex(3, sym(">"), var("a"), var("b")),
                              ex(3, sym(">"), var("c"), var("d")),
                              ex(3, sym(">"), var("d"), var("c"))));
    uint32_t fk = cetta_mol_frame(&m);
    assert(mol_key(&m, shared, fk) == shared);
    /* Parts of it read where the numbering has already met their variables
     * take the numbers met: (fact (> $1 $2) (> $3 $4) (> $4 $3)) read by
     * (fact $p $q $r), then (pair $q $r) and (pair $r $q) keyed. */
    AtomId joint = store(ex(4, sym("j"),
                            ex(4, sym("fact"), var("p"), var("q"), var("r")),
                            ex(3, sym("pair"), var("q"), var("r")),
                            ex(3, sym("pair"), var("r"), var("q"))));
    uint32_t fj = cetta_mol_frame(&m);
    fk = cetta_mol_frame(&m);
    assert(cetta_mol_unify(&m, (CettaMol){tu_child(&g_universe, joint, 1u), fj},
                           (CettaMol){shared, fk}) == CETTA_MOL_OK);
    assert(mol_key(&m, tu_child(&g_universe, joint, 2u), fj) ==
           key_of(ex(3, sym("pair"), ex(3, sym(">"), var("x"), var("y")),
                     ex(3, sym(">"), var("y"), var("x")))));
    assert(mol_key(&m, tu_child(&g_universe, joint, 3u), fj) ==
           key_of(ex(3, sym("pair"), ex(3, sym(">"), var("x"), var("y")),
                     ex(3, sym(">"), var("y"), var("x")))));
    /* A part read twice is keyed alike both times. */
    AtomId again = store(ex(3, sym("j"),
                            ex(4, sym("fact"), var("p"), var("q"), var("r")),
                            ex(3, sym("pair"), var("q"), var("q"))));
    fj = cetta_mol_frame(&m);
    fk = cetta_mol_frame(&m);
    assert(cetta_mol_unify(&m, (CettaMol){tu_child(&g_universe, again, 1u), fj},
                           (CettaMol){shared, fk}) == CETTA_MOL_OK);
    assert(mol_key(&m, tu_child(&g_universe, again, 2u), fj) ==
           key_of(ex(3, sym("pair"), ex(3, sym(">"), var("x"), var("y")),
                     ex(3, sym(">"), var("x"), var("y")))));
    cetta_mol_undo(&m, start);

    /* A term already its alpha key is its own key; one whose variables are
     * key variables out of order is not. */
    assert(term_universe_alpha_key_id(&g_universe, shared) == shared);
    AtomId key_x = tu_child(&g_universe, tu_child(&g_universe, shared, 1u), 1u);
    AtomId key_y = tu_child(&g_universe, tu_child(&g_universe, shared, 1u), 2u);
    AtomId key_z = tu_child(&g_universe, tu_child(&g_universe, shared, 2u), 1u);
    AtomId arrow = tu_child(&g_universe, tu_child(&g_universe, shared, 1u), 0u);
    AtomId swapped = tu_expr_from_ids(&g_universe,
                                      (AtomId[]){arrow, key_y, key_x}, 3u);
    AtomId skipped = tu_expr_from_ids(&g_universe,
                                      (AtomId[]){arrow, key_x, key_z}, 3u);
    AtomId in_order = tu_child(&g_universe, shared, 1u);
    assert(swapped != in_order && skipped != in_order);
    assert(term_universe_alpha_key_id(&g_universe, swapped) == in_order);
    assert(term_universe_alpha_key_id(&g_universe, skipped) == in_order);
    assert(term_universe_alpha_key_id(&g_universe, in_order) == in_order);
    /* A term written with named variables is keyed as its atom is. */
    Atom *named = ex(3, sym("f"), ex(2, sym("g"), var("y")), var("x"));
    assert(term_universe_alpha_key_id(&g_universe, store(named)) ==
           key_of(named));

    /* Random pairs, each read under its own frame, against the reference:
     * the second's variables renamed apart. */
    static const char *const first_names[3] = {"x", "y", "z"};
    static const char *const second_names[3] = {"x2", "y2", "z2"};
    static const char *const shared_names[3] = {"x", "y", "z"};
    int unified = 0, failed = 0;
    for (int trial = 0; trial < 20000; trial++) {
        g_seed += (uint64_t)trial;
        uint64_t seed = g_seed;
        Atom *t1 = random_term(3, first_names);
        g_seed = seed ^ 0x9e3779b97f4a7c15ULL;
        uint64_t seed2 = g_seed;
        Atom *t2 = random_term(3, shared_names);
        g_seed = seed2;
        Atom *t2_apart = random_term(3, second_names);
        Subst s = {.len = 0};
        bool ok = ref_unify(&s, t1, t2_apart);
        uint32_t fx = cetta_mol_frame(&m), fy = cetta_mol_frame(&m);
        AtomId i1 = store(t1), i2 = store(t2);
        CettaMolResult got = cetta_mol_unify(&m, (CettaMol){i1, fx},
                                             (CettaMol){i2, fy});
        assert(got != CETTA_MOL_DECLINE);
        assert((got == CETTA_MOL_OK) == ok);
        AtomId want = ok ? key_of(apply(&s, t1)) : CETTA_ATOM_ID_NONE;
        if (ok) {
            unified++;
            assert(mol_key(&m, i1, fx) == want);
            assert(mol_key(&m, i2, fy) == want);
        } else {
            failed++;
        }
        cetta_mol_undo(&m, start);
        /* The second stored as its alpha key, as a set keeps it: the same
         * unifier, and parts read under its frame are keyed alike. */
        AtomId k2 = key_of(t2);
        assert(term_universe_alpha_key_id(&g_universe, k2) == k2);
        assert(term_universe_alpha_key_id(&g_universe, i2) == k2);
        fx = cetta_mol_frame(&m);
        fy = cetta_mol_frame(&m);
        got = cetta_mol_unify(&m, (CettaMol){i1, fx}, (CettaMol){k2, fy});
        assert((got == CETTA_MOL_OK) == ok);
        if (ok) {
            assert(mol_key(&m, i1, fx) == want);
            assert(mol_key(&m, k2, fy) == want);
        }
        cetta_mol_undo(&m, start);
        g_seed = seed * 2862933555777941757ULL + 3037000493ULL;
    }
    assert(unified > 1000 && failed > 1000);

    /* The frames' growth refused: the cell declines, the capacity
     * published is one the frames hold, and the cells a later branch makes
     * read their frames. */
    CettaMolStore grown;
    cetta_mol_store_init(&grown, &g_universe);
    CettaMol cell;
    for (uint32_t i = 0u; i < 16u; i++)
        assert(cetta_mol_cell(&grown, &cell) == CETTA_MOL_OK);
    g_refused = grown.cell_frames;
    assert(cetta_mol_cell(&grown, &cell) == CETTA_MOL_DECLINE);
    assert(!g_refused && grown.cell_len == 16u && grown.cell_cap == 16u);
    for (uint32_t i = 16u; i < 40u; i++) {
        assert(cetta_mol_cell(&grown, &cell) == CETTA_MOL_OK);
        assert(cell.at == i && grown.cell_frames[i] == CETTA_MOL_GROUND);
    }
    cetta_mol_store_free(&grown);

    cetta_mol_store_free(&m);
    term_universe_free(&g_universe);
    arena_free(&persistent);
    arena_free(&g_arena);
    var_intern_free(&var_intern);
    symbol_table_free(&symbols);
    printf("PASS: molecules unify renamed apart with the occurs check, undo, "
           "decline inexact values, unify kept facts as their records, "
           "make instances' keys and holes and survive a refused growth "
           "(%d unified, %d failed at random)\n",
           unified, failed);
    return 0;
}

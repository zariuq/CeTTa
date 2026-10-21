/* The inert type guard agrees with the relational intrinsic get-type on every
 * subject it decides: randomized subjects, requirements and symbol-subject
 * declarations, in both live and dead modes.  Deferred cases are counted, and
 * a subject mentioning a declared symbol must defer. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "atom.h"
#include "petta_type_policy.h"
#include "space.h"
#include "symbol.h"

static uint64_t rng = 0x9e3779b97f4a7c15u;
static uint32_t next(uint32_t bound) {
    rng ^= rng << 13u;
    rng ^= rng >> 7u;
    rng ^= rng << 17u;
    return (uint32_t)(rng % bound);
}

enum { UNDECLARED = 6, DECLARED = 4, SUBJECT_VARS = 4, TYPE_VARS = 4 };
static Atom *undeclared[UNDECLARED];
static Atom *declared[DECLARED];
static Atom *subject_vars[SUBJECT_VARS];
static Atom *type_vars[TYPE_VARS];
static Atom *type_symbols[8];

static Atom *subject(Arena *arena, uint32_t depth, bool *mentions_declared) {
    uint32_t pick = next(depth ? 10u : 8u);
    switch (pick) {
    case 0: return subject_vars[next(SUBJECT_VARS)];
    case 1: return atom_int(arena, (int64_t)next(5));
    case 2: return atom_string(arena, "s");
    case 3: return atom_symbol(arena, next(2) ? "True" : "False");
    case 4:
        if (next(4) == 0) {
            *mentions_declared = true;
            return declared[next(DECLARED)];
        }
        /* fall through */
    case 5:
    case 6:
    case 7: return undeclared[next(UNDECLARED)];
    default: {
        uint32_t length = next(4);
        Atom *items[4];
        for (uint32_t i = 0; i < length; i++)
            items[i] = subject(arena, depth - 1u, mentions_declared);
        return atom_expr(arena, items, length);
    }
    }
}

static Atom *requirement(Arena *arena, uint32_t depth) {
    uint32_t pick = next(depth ? 9u : 7u);
    switch (pick) {
    case 0:
    case 1: return type_vars[next(TYPE_VARS)];
    case 2:
        /* A requirement sharing a subject variable must keep the query. */
        return next(6) == 0 ? subject_vars[next(SUBJECT_VARS)]
                            : type_vars[next(TYPE_VARS)];
    case 3:
    case 4:
    case 5:
    case 6: return type_symbols[next(8)];
    default: {
        uint32_t length = next(4);
        Atom *items[4];
        for (uint32_t i = 0; i < length; i++)
            items[i] = requirement(arena, depth - 1u);
        return atom_expr(arena, items, length);
    }
    }
}

/* Equality up to a consistent renaming of variables. */
typedef struct {
    VarId left[256], right[256];
    uint32_t count;
} Renaming;

static bool alpha(const Atom *a, const Atom *b, Renaming *map) {
    if (a->kind != b->kind)
        return false;
    if (a->kind == ATOM_VAR) {
        for (uint32_t i = 0; i < map->count; i++) {
            if (var_id_eq(map->left[i], a->var_id) || var_id_eq(map->right[i], b->var_id))
                return var_id_eq(map->left[i], a->var_id) && var_id_eq(map->right[i], b->var_id);
        }
        assert(map->count < 256u);
        map->left[map->count] = a->var_id;
        map->right[map->count++] = b->var_id;
        return true;
    }
    if (a->kind != ATOM_EXPR)
        return atom_eq((Atom *)a, (Atom *)b);
    if (a->expr.len != b->expr.len)
        return false;
    for (CettaExprIndex i = 0; i < a->expr.len; i++) {
        if (!alpha(a->expr.elems[i], b->expr.elems[i], map))
            return false;
    }
    return true;
}

static void declare(Space *space, Arena *arena, Atom *subject_atom, Atom *type) {
    space_add(space, atom_expr3(arena, atom_symbol(arena, ":"), subject_atom, type));
}

int main(int argc, char **argv) {
    uint32_t trials = argc > 1 ? (uint32_t)strtoul(argv[1], NULL, 10) : 200000u;
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    Arena persistent;
    arena_init(&persistent);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);
    Space space;
    space_init_with_universe(&space, &universe);

    char name[32];
    for (int i = 0; i < UNDECLARED; i++) {
        snprintf(name, sizeof(name), "u%d", i);
        undeclared[i] = atom_symbol(&persistent, name);
    }
    for (int i = 0; i < DECLARED; i++) {
        snprintf(name, sizeof(name), "d%d", i);
        declared[i] = atom_symbol(&persistent, name);
    }
    /* The guard mints fresh variables for the types it builds; the test's
     * variables take their identities from the same allocator, so a built
     * type never reuses one of them. */
    for (int i = 0; i < SUBJECT_VARS; i++)
        subject_vars[i] = atom_var_with_id(&persistent, "x", fresh_var_id());
    for (int i = 0; i < TYPE_VARS; i++)
        type_vars[i] = atom_var_with_id(&persistent, "t", fresh_var_id());
    const char *type_names[8] = {"Nat", "Number", "String", "Bool", "%Undefined%",
                                 "Symbol", "Expression", "u0"};
    for (int i = 0; i < 8; i++)
        type_symbols[i] = atom_symbol(&persistent, type_names[i]);
    /* Declarations of every shape the relation reads: constants, arrows,
     * polymorphic arrows and a symbol declared twice. */
    Atom *nat = type_symbols[0];
    Atom *arrow = atom_symbol(&persistent, "->");
    declare(&space, &persistent, declared[0], nat);
    declare(&space, &persistent, declared[1], atom_expr3(&persistent, arrow, nat, nat));
    declare(&space, &persistent, declared[2],
            atom_expr3(&persistent, arrow, type_vars[0], type_vars[0]));
    declare(&space, &persistent, declared[3], nat);
    declare(&space, &persistent, declared[3], type_symbols[1]);
    assert(space_type_annotations_have_only_symbol_subjects(&space));

    uint64_t decided[2] = {0}, deferred[2] = {0}, none[2] = {0}, proved[2] = {0},
             answered[2] = {0}, declared_defer = 0;
    for (uint32_t trial = 0; trial < trials; trial++) {
        Arena arena;
        arena_init(&arena);
        bool mentions_declared = false;
        Atom *s = subject(&arena, 3u, &mentions_declared);
        Atom *r = requirement(&arena, 2u);
        if (s->kind == ATOM_VAR) {
            arena_free(&arena);
            continue;
        }
        Atom **types = NULL;
        uint32_t count = 0;
        CettaEvalCompletion completion;
        bool relational = petta_type_intrinsic_answers(&space, &arena, s, r, &types,
                                                       &count, &completion);
        for (int dead = 0; dead < 2; dead++) {
            Atom *answer = NULL;
            PettaTypeInertResult fast =
                petta_type_inert_guard(&space, &arena, s, r, dead != 0, &answer);
            if (fast == PETTA_TYPE_INERT_DEFER) {
                deferred[dead]++;
                continue;
            }
            assert(!mentions_declared);
            assert(fast != PETTA_TYPE_INERT_FAULT);
            assert(relational && completion == CETTA_EVAL_COMPLETE);
            decided[dead]++;
            Renaming map = {0};
            switch (fast) {
            case PETTA_TYPE_INERT_NONE:
                none[dead]++;
                assert(count == 0u);
                break;
            case PETTA_TYPE_INERT_PROVED:
                proved[dead]++;
                assert(count == 1u);
                if (!dead)
                    assert(alpha(types[0], r, &map));
                break;
            case PETTA_TYPE_INERT_ANSWER:
                answered[dead]++;
                assert(!dead && count == 1u && alpha(types[0], answer, &map));
                break;
            default:
                assert(!"unexpected inert result");
            }
        }
        if (mentions_declared)
            declared_defer++;
        free(types);
        arena_free(&arena);
    }
    for (int dead = 0; dead < 2; dead++)
        printf("%s: decided %llu (none %llu, proved %llu, answer %llu), deferred %llu\n",
               dead ? "dead" : "live",
               (unsigned long long)decided[dead], (unsigned long long)none[dead],
               (unsigned long long)proved[dead], (unsigned long long)answered[dead],
               (unsigned long long)deferred[dead]);
    printf("subjects mentioning a declared symbol (all deferred): %llu\n",
           (unsigned long long)declared_defer);
    assert(decided[0] > trials / 4u && none[0] > 0u && proved[0] > 0u &&
           answered[0] > 0u);
    printf("PASS: inert type guard agrees with the relational get-type\n");
    space_free(&space);
    return 0;
}

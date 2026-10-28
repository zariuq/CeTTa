/* A region is reached only through its handles.  A space atom or a name that
 * reaches a region's space some other way, as a host's resolver might, gets
 * no right over it, at formation, in a walk or at a commit; a plain space
 * keeps every right.  Region identity is seen only by comparison. */
#include "../src/interaction_core.c"

#include <stdio.h>

static unsigned g_checks, g_failures;
#define CHECK(x) do { \
    g_checks++; \
    if (!(x)) { \
        g_failures++; \
        fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, #x); \
    } \
} while (0)

/* A resolver that names the region's space, as no program can. */
static bool resolve_raw(void *context, Atom *location, Space **space_out) {
    if (!atom_is_symbol(location, "&raw"))
        return false;
    *space_out = context;
    return true;
}

static bool refused_for(Atom *value, const char *reason) {
    return ia_head(value, "ia:refused", 2u) && ia_head_any(value->expr.elems[1], reason);
}

static Atom *verdict(Arena *a, int64_t n) {
    return atom_expr3(a, atom_symbol(a, "verdict"), atom_symbol(a, "even"), atom_int(a, n));
}

static Atom *rights_list(Arena *a, const char *right) {
    Atom *item = right ? atom_symbol(a, right) : NULL;
    return atom_expr(a, right ? &item : NULL, right ? 1u : 0u);
}

static bool is_bool(Atom *value, bool expected) {
    Arena scratch;
    arena_init_detached(&scratch);
    bool same = value && atom_eq(value, expected ? atom_true(&scratch) : atom_false(&scratch));
    arena_free(&scratch);
    return same;
}

static Atom *call_value(bool (*op)(Arena *, Atom **, uint32_t, CettaCallOutcome *),
                        Arena *a, Atom **args, uint32_t nargs) {
    CettaCallOutcome out = {0};
    if (!op(a, args, nargs, &out) || out.kind != CETTA_CALL_VALUE)
        return NULL;
    return out.term;
}

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    VarInternTable variables;
    var_intern_init(&variables);
    g_var_intern = &variables;
    g_hashcons = NULL;
    Arena a;
    arena_init_detached(&a);
    Space plain, backing;
    space_init(&plain);
    space_init(&backing);
    IaRegion *region = ia_region_of(&backing, true);
    CHECK(region);
    if (!region)
        return 1;
    region->profile = ia_profile_named("board");
    Atom *holder = ia_handle_atom(&a, region, region->profile->holder);
    Atom *raw = atom_space(&a, &backing);
    IaLoc loc;

    /* the holder's handle has the holder's facets */
    CHECK(holder && !ia_resolve(&a, holder, &loc) && loc.space == &backing &&
          loc.region == region && loc.rights == region->profile->holder);

    /* the region's space atom has none */
    Atom *why = ia_resolve(&a, raw, &loc);
    CHECK(why && ia_head(why, "region-needs-handle", 2u) && !loc.rights);

    /* nor does a name a resolver maps to that space */
    CettaInteractionHost host = {.resolve_space = resolve_raw, .context = &backing};
    g_ia_host = &host;
    why = ia_resolve(&a, atom_symbol(&a, "&raw"), &loc);
    CHECK(why && ia_head(why, "region-needs-handle", 2u) && !loc.rights);
    g_ia_host = NULL;

    /* the kernel's operations refuse the space atom: rights, facets,
     * formation and a commit that makes into it */
    Atom *one[1] = {raw};
    CHECK(refused_for(call_value(op_rights, &a, one, 1u), "region-needs-handle"));
    Atom *facet_args[2] = {raw, rights_list(&a, "peek")};
    CHECK(refused_for(call_value(op_facet, &a, facet_args, 2u), "region-needs-handle"));
    Atom *make = atom_expr2(&a, raw, verdict(&a, 7));
    Atom *formed_args[3] = {raw, atom_unit(&a),
                            atom_expr3(&a, atom_symbol(&a, "add-atom"), raw,
                                       verdict(&a, 7))};
    CHECK(refused_for(call_value(op_formed, &a, formed_args, 3u), "region-needs-handle"));
    Atom *commit_args[3] = {atom_unit(&a), atom_unit(&a), atom_expr(&a, &make, 1u)};
    CHECK(refused_for(call_value(op_commit, &a, commit_args, 3u), "region-needs-handle") &&
          space_length64(&backing) == 0u);

    /* a plain space has every right, by its atom or by a name */
    CHECK(!ia_resolve(&a, atom_space(&a, &plain), &loc) && loc.space == &plain &&
          loc.rights == IA_ALL);
    host.context = &plain;
    g_ia_host = &host;
    CHECK(!ia_resolve(&a, atom_symbol(&a, "&raw"), &loc) && loc.space == &plain &&
          loc.rights == IA_ALL);
    g_ia_host = NULL;

    /* identity by comparison only: every handle of the region is the same
     * region, its space atom is not a location of it, other spaces differ */
    Atom *peek_args[2] = {holder, rights_list(&a, "peek")};
    Atom *peek = call_value(op_facet, &a, peek_args, 2u);
    Atom *bare_args[2] = {holder, rights_list(&a, NULL)};
    Atom *bare = call_value(op_facet, &a, bare_args, 2u);
    CHECK(peek && ia_handle_of(peek) && bare && ia_handle_of(bare) &&
          ia_handle_of(bare)->rights == 0u);
    Atom *pair[2] = {holder, peek};
    CHECK(is_bool(call_value(op_same_region, &a, pair, 2u), true));
    pair[1] = bare;
    CHECK(is_bool(call_value(op_same_region, &a, pair, 2u), true));
    pair[1] = raw;
    CHECK(is_bool(call_value(op_same_region, &a, pair, 2u), false));
    pair[1] = atom_space(&a, &plain);
    CHECK(is_bool(call_value(op_same_region, &a, pair, 2u), false));

    printf("Interaction naming: %u checks, %u failures\n", g_checks, g_failures);
    space_free(&plain);
    space_free(&backing);
    arena_free(&a);
    var_intern_free(&variables);
    g_var_intern = NULL;
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return g_failures ? 1 : 0;
}

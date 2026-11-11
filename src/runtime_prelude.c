#include "runtime_prelude.h"
#include "cetta_stdlib.h"
#include "he_type_policy.h"
#include "eval.h"
#include <stdlib.h>
#include <string.h>

static bool runtime_try_add_builtin_type_decls_direct(Space *space,
                                                   CettaLanguageId language_id,
                                                   const CettaProfile *profile) {
    if (!space || !space->native.universe)
        return false;

    static const char *arith_ops_base[] = {"+", "-", "*", "/", "%", NULL};
    static const char *arith_ops_formal[] = {"+", "-", "*", "/", "//", "%", NULL};
    static const char *cmp_ops[] = {"<", ">", "<=", ">=", NULL};
    const char **arith_ops =
        cetta_language_uses_rust_he_compat_semantics(language_id, profile)
        ? arith_ops_base
        : arith_ops_formal;

    TermUniverse *universe = space->native.universe;
    AtomId decl_ids[12u];
    uint32_t decl_count = 0;

    AtomId colon_id = tu_intern_symbol(universe, g_builtin_syms.colon);
    AtomId arrow_id = tu_intern_symbol(universe, g_builtin_syms.arrow);
    AtomId equals_id = tu_intern_symbol(universe, g_builtin_syms.equals);
    AtomId eqeq_id = tu_intern_symbol(universe, symbol_intern_cstr(g_symbols, "=="));
    AtomId number_id = tu_intern_symbol(universe, symbol_intern_cstr(g_symbols, "Number"));
    AtomId bool_id = tu_intern_symbol(universe, symbol_intern_cstr(g_symbols, "Bool"));
    AtomId undefined_id =
        tu_intern_symbol(universe, symbol_intern_cstr(g_symbols, "%Undefined%"));
    SymbolId t_spelling = symbol_intern_cstr(g_symbols, "t");
    VarId t_var = fresh_var_id();
    AtomId t_var_id = tu_intern_var(universe, t_spelling, t_var);
    AtomId arrow_nnn_children[4] = {arrow_id, number_id, number_id, number_id};
    AtomId arrow_nnb_children[4] = {arrow_id, number_id, number_id, bool_id};
    AtomId arrow_eq_children[4] = {arrow_id, t_var_id, t_var_id, undefined_id};
    AtomId arrow_eqeq_children[4] = {arrow_id, t_var_id, t_var_id, bool_id};

    if (colon_id == CETTA_ATOM_ID_NONE || arrow_id == CETTA_ATOM_ID_NONE ||
        equals_id == CETTA_ATOM_ID_NONE || eqeq_id == CETTA_ATOM_ID_NONE ||
        number_id == CETTA_ATOM_ID_NONE || bool_id == CETTA_ATOM_ID_NONE ||
        undefined_id == CETTA_ATOM_ID_NONE || t_var_id == CETTA_ATOM_ID_NONE)
        return false;

    AtomId arrow_nnn_id = tu_expr_from_ids(universe, arrow_nnn_children, 4);
    AtomId arrow_nnb_id = tu_expr_from_ids(universe, arrow_nnb_children, 4);
    AtomId arrow_eq_id = tu_expr_from_ids(universe, arrow_eq_children, 4);
    AtomId arrow_eqeq_id = tu_expr_from_ids(universe, arrow_eqeq_children, 4);
    if (arrow_nnn_id == CETTA_ATOM_ID_NONE || arrow_nnb_id == CETTA_ATOM_ID_NONE ||
        arrow_eq_id == CETTA_ATOM_ID_NONE || arrow_eqeq_id == CETTA_ATOM_ID_NONE)
        return false;

    for (const char **op = arith_ops; *op; op++) {
        AtomId op_id = tu_intern_symbol(universe, symbol_intern_cstr(g_symbols, *op));
        AtomId decl_children[3] = {colon_id, op_id, arrow_nnn_id};
        if (op_id == CETTA_ATOM_ID_NONE)
            return false;
        decl_ids[decl_count] = tu_expr_from_ids(universe, decl_children, 3);
        if (decl_ids[decl_count++] == CETTA_ATOM_ID_NONE)
            return false;
    }
    for (const char **op = cmp_ops; *op; op++) {
        AtomId op_id = tu_intern_symbol(universe, symbol_intern_cstr(g_symbols, *op));
        AtomId decl_children[3] = {colon_id, op_id, arrow_nnb_id};
        if (op_id == CETTA_ATOM_ID_NONE)
            return false;
        decl_ids[decl_count] = tu_expr_from_ids(universe, decl_children, 3);
        if (decl_ids[decl_count++] == CETTA_ATOM_ID_NONE)
            return false;
    }

    AtomId eq_decl_children[3] = {colon_id, equals_id, arrow_eq_id};
    decl_ids[decl_count] = tu_expr_from_ids(universe, eq_decl_children, 3);
    if (decl_ids[decl_count++] == CETTA_ATOM_ID_NONE)
        return false;

    AtomId eqeq_decl_children[3] = {colon_id, eqeq_id, arrow_eqeq_id};
    decl_ids[decl_count] = tu_expr_from_ids(universe, eqeq_decl_children, 3);
    if (decl_ids[decl_count++] == CETTA_ATOM_ID_NONE)
        return false;

    for (uint32_t i = 0; i < decl_count; i++)
        space_add_atom_id(space, decl_ids[i]);
    return true;
}

/* he-prime typing ops: the declared types are what STAGE the term/type
 * arguments (Atom = arrives unreduced), so argument evaluation cannot run a
 * computation before the checker has ruled on its admissibility.  Space and
 * fuel arguments evaluate normally. Verdicts are DATA too: their proof and
 * type fields must reach the consumer unchanged, not run as runtime calls.
 * The rows double as the ops' visible self-description; no declaration here
 * is marked as a chaining rule. */
static void runtime_add_he_prime_typing_op_decls(Space *space, Arena *arena) {
    /* u = %Undefined% (evaluated), A = Atom (staged), N = Number */
    static const struct { const char *name; const char *sig; } ops[] = {
        {"normalize-type", "uAN"},
        {"check-type-refinements", "uAN"},
        {"is-consistent", "AA"},
        {"check-type", "uAAN"},
        {"search-inhabitants", "uANNN"},
        {"search-inhabitants", "uANNNA"},
        {"search-first-inhabitant", "uAN"},
        {"search-first-inhabitant", "uANN"},
        {"search-first-inhabitant", "uANNA"},
        {"type-forward-step", "uNN"},
        {"type-forward-closure", "uNNN"},
    };
    for (size_t i = 0; i < sizeof ops / sizeof ops[0]; i++) {
        const char *sig = ops[i].sig;
        size_t n = strlen(sig);
        Atom **elems = arena_alloc(arena, sizeof(Atom *) * (n + 2));
        elems[0] = atom_symbol_id(arena, g_builtin_syms.arrow);
        for (size_t k = 0; k < n; k++) {
            elems[k + 1] = sig[k] == 'A'
                ? atom_symbol_id(arena, g_builtin_syms.atom)
                : sig[k] == 'N' ? atom_symbol(arena, "Number")
                                : atom_undefined_type(arena);
        }
        elems[n + 1] = atom_atom_type(arena);
        Atom *decl = atom_expr3(arena,
            atom_symbol_id(arena, g_builtin_syms.colon),
            atom_symbol(arena, ops[i].name),
            atom_expr(arena, elems, (CettaExprLen)(n + 2)));
        if (!space_admit_atom(space, arena, decl))
            space_add(space, decl);
    }
}

static void runtime_add_prime_semantic_op_decls(Space *space, Arena *arena) {
    Atom *proc_type=atom_expr3(arena,atom_symbol_id(arena,g_builtin_syms.colon),
        atom_symbol_id(arena,g_builtin_syms.prime_proc),atom_symbol(arena,"Type"));
    if(!space_admit_atom(space,arena,proc_type))space_add(space,proc_type);
    static const struct { const char *name; const char *sig; } ops[] = {
        {"prime-package", "u"},
        {"prime:elaborate", "A"},
        {"type:of", "A"},
        {"type:check", "AA"},
        {"type:analyze", "AA"},
        {"type:eq", "AA"},
        {"type:formed", "A"},
        {"type:refine", "A"},
        {"type:may", "AA"},
        {"type:must", "AA"},
        {"type:theorem", "AAA"},
        {"type:prove", "AAA"},
        {"cost:firings", "A"},
        {"nik:check", "AAA"},
        {"nik:check", "AAAN"},
        /* Draft scoped judgment bubbles; operands are data like `type:`. */
        {"set:signature", "A"},
        {"set:formed", "AA"},
        {"set:of", "AA"},
        {"set:check", "AAA"},
        {"set:eq", "AAA"},
        {"set:proves", "AAA"},
        {"set:axiom", "AAA"},
        {"set:define", "AAAAAAAAAA"},
        {"set:inductive", "AAAAAAAAAA"},
        {"set:family", "AAAAAAAAAA"},
        {"captured", "AA"},
        {"ctx:capture", "AA"},
        {"set:theorem", "AAAA"},
        {"set:recheck", "AA"},
        {"set:known-proof", "AA"},
        {"set:known-proposition", "AA"},
        {"set:signature-digest", "A"},
        {"set:numerals", "AAA"},
        {"set:oracle", "AAAA"},
        {"hol:signature", "A"},
        {"hol:formed", "AA"},
        {"hol:of", "AA"},
        {"hol:check", "AAA"},
        {"hol:eq", "AAA"},
        {"hol:proves", "AAA"},
        {"hol:axiom", "AAA"},
        {"hol:define", "AAAAAAAAAA"},
        {"hol:inductive", "AAAAAAAAAA"},
        {"hol:family", "AAAAAAAAAA"},
        {"hol:theorem", "AAAA"},
        {"hol:recheck", "AA"},
        {"hol:known-proof", "AA"},
        {"hol:known-proposition", "AA"},
        {"hol:signature-digest", "A"},
        {"hol:numerals", "AAA"},
        {"hol:oracle", "AAAA"},
        {"try", "A"},
        {"id:region", "A"},
        {"id:policy", ""},
        {"lang:languages", "A"},
        {"lang:parse", "AA"},
        {"lang:print", "AA"},
        {"lang:native-type", "AAAA"},
        {"lang:step", "AAA"},
    };
    for (size_t i = 0; i < sizeof ops / sizeof ops[0]; i++) {
        const char *sig = ops[i].sig;
        size_t n = strlen(sig);
        Atom **elems = arena_alloc(arena, sizeof(Atom *) * (n + 2));
        elems[0] = atom_symbol_id(arena, g_builtin_syms.arrow);
        for (size_t k = 0; k < n; k++) {
            elems[k + 1] = sig[k] == 'A'
                ? atom_symbol_id(arena, g_builtin_syms.atom)
                : sig[k] == 'N' ? atom_symbol(arena, "Number")
                                : atom_undefined_type(arena);
        }
        elems[n + 1] = atom_undefined_type(arena);
        Atom *decl = atom_expr3(
            arena, atom_symbol_id(arena, g_builtin_syms.colon),
            atom_symbol(arena, ops[i].name),
            atom_expr(arena, elems, (CettaExprLen)(n + 2)));
        if (!space_admit_atom(space, arena, decl))
            space_add(space, decl);
    }

    Atom *a_var = atom_var(arena, "a");
    Atom *suspension_a = atom_expr2(
        arena, atom_symbol(arena, "suspension"), a_var);
    Atom *delay_type = atom_expr3(
        arena, atom_symbol_id(arena, g_builtin_syms.arrow),
        a_var, suspension_a);
    Atom *force_type = atom_expr3(
        arena, atom_symbol_id(arena, g_builtin_syms.arrow),
        suspension_a, a_var);
    Atom *typed_ops[2] = {
        atom_expr3(arena,
                   atom_symbol_id(arena, g_builtin_syms.colon),
                   atom_symbol(arena, "delay"), delay_type),
        atom_expr3(arena,
                   atom_symbol_id(arena, g_builtin_syms.colon),
                   atom_symbol(arena, "force"), force_type),
    };
    const char *scheme_names[2] = {"delay", "force"};
    for (size_t i = 0u; i < 2u; i++) {
        if (!space_admit_atom(space, arena, typed_ops[i]))
            space_add(space, typed_ops[i]);
        Atom *scheme = atom_expr2(
            arena, atom_symbol(arena, "type-scheme"),
            atom_symbol(arena, scheme_names[i]));
        if (!space_admit_atom(space, arena, scheme))
            space_add(space, scheme);
    }

    Atom *context_type = atom_symbol(arena, "context");
    Atom *name_type = atom_symbol(arena, "Name");
    Atom *atom_type = atom_symbol_id(arena, g_builtin_syms.atom);
    Atom *number_type = atom_symbol(arena, "Number");
    Atom *ctx_bind_type = atom_expr(
        arena,
        (Atom *[]){
            atom_symbol_id(arena, g_builtin_syms.arrow),
            context_type, name_type, atom_type, context_type,
        },
        5u);
    Atom *ctx_get_type = atom_expr(
        arena,
        (Atom *[]){
            atom_symbol_id(arena, g_builtin_syms.arrow),
            context_type, name_type, atom_type,
        },
        4u);
    Atom *ctx_view_type = atom_expr(
        arena,
        (Atom *[]){
            atom_symbol_id(arena, g_builtin_syms.arrow),
            context_type, number_type, atom_type,
        },
        4u);
    Atom *context_decls[] = {
        atom_expr3(
            arena, atom_symbol_id(arena, g_builtin_syms.colon),
            atom_symbol(arena, "ctx:empty"), context_type),
        atom_expr3(
            arena, atom_symbol_id(arena, g_builtin_syms.colon),
            atom_symbol(arena, "ctx:bind"), ctx_bind_type),
        atom_expr3(
            arena, atom_symbol_id(arena, g_builtin_syms.colon),
            atom_symbol(arena, "ctx:get"), ctx_get_type),
        atom_expr3(
            arena, atom_symbol_id(arena, g_builtin_syms.colon),
            atom_symbol(arena, "ctx:view"), ctx_view_type),
    };
    for (size_t i = 0u;
         i < sizeof context_decls / sizeof context_decls[0]; i++) {
        if (!space_admit_atom(space, arena, context_decls[i]))
            space_add(space, context_decls[i]);
    }
}

/* The library module &self imports in HE profiles.  Its declarations are
 * upstream's, in upstream's order.  CeTTa's library file keeps its own
 * declarations of names upstream's library does not know; of the others it
 * keeps only, outside the profile with upstream's exact semantics, those
 * adding a call form of an arity upstream does not declare.  A grounded
 * operation's name has no declaration: its type is the operation's own.
 * Unavailable extensions contribute no library declarations or equations. Filter them
 * here, before loading the program, rather than rejecting user-declared
 * types or untyped data that happen to use the same spelling. */
static bool runtime_he_prepare_library(Space *space, Arena *arena,
                                    const CettaProfile *profile) {
    bool extensions = !cetta_language_uses_rust_he_compat_semantics(
        CETTA_LANGUAGE_HE, profile);
    CettaCount len = space_length64(space);
    uint8_t *mask = calloc(len ? (size_t)len : 1u, 1u);
    AtomId *kept = calloc(len ? (size_t)len : 1u, sizeof(*kept));
    if (!mask || !kept) {
        free(mask);
        free(kept);
        return false;
    }
    CettaCount kept_len = 0u;
    for (CettaIndex i = 0u; i < len; i++) {
        Atom *atom = space_get_at64(space, i);
        if (!atom || atom->kind != ATOM_EXPR || atom->expr.len != 3u)
            continue;
        if (atom_is_symbol_id(atom->expr.elems[0], g_builtin_syms.equals)) {
            Atom *pattern = atom->expr.elems[1];
            Atom *head = pattern->kind == ATOM_EXPR && pattern->expr.len
                ? pattern->expr.elems[0] : pattern;
            if (head->kind == ATOM_SYMBOL &&
                (!cetta_language_allows_builtin(
                    CETTA_LANGUAGE_HE, profile, atom_name_cstr(head)) ||
                 (!extensions && pattern->kind == ATOM_EXPR &&
                  he_library_declares(head) &&
                  !he_library_declares_arity(head, pattern->expr.len - 1u))))
                mask[i] = 1u;
            continue;
        }
        if (!atom_is_symbol_id(atom->expr.elems[0], g_builtin_syms.colon))
            continue;
        Atom *subject = atom->expr.elems[1];
        Atom *type = atom->expr.elems[2];
        if (subject->kind == ATOM_SYMBOL &&
            !cetta_language_allows_builtin(
                CETTA_LANGUAGE_HE, profile, atom_name_cstr(subject))) {
            mask[i] = 1u;
            continue;
        }
        bool grounded = subject->kind == ATOM_SYMBOL &&
            he_grounded_symbol_types(atom_symbol_current_id(subject),
                extensions ? HE_GROUNDED_TIER_EXTENSIONS
                           : HE_GROUNDED_TIER_UPSTREAM, NULL) != 0u;
        if (!grounded && !he_library_names_subject(subject))
            continue;
        mask[i] = 1u;
        if (!grounded && extensions && he_library_declares(subject) &&
            type->kind == ATOM_EXPR && type->expr.len >= 2u &&
            atom_is_symbol_id(type->expr.elems[0], g_builtin_syms.arrow) &&
            !he_library_declares_arity(subject, type->expr.len - 2u))
            kept[kept_len++] = space_get_atom_id_at64(space, i);
    }
    CettaCount removed = 0u;
    bool ok = space_remove_occurrence_mask_stable(space, mask, len, &removed);
    for (size_t i = 0u; ok && i < he_library_declaration_count(); i++) {
        Atom *declaration = he_library_declaration_at(i);
        if (!space_admit_atom(space, arena, declaration))
            space_add(space, declaration);
    }
    /* The exact-semantics library keeps upstream's let and switch-minimal
     * equations. switch-internal stays the row in lib/stdlib.metta.
     * Extended profiles keep no injected equation. */
    for (size_t i = 0u; ok && !extensions && i < he_library_equation_count(); i++) {
        Atom *equation = he_library_equation_at(i);
        if (!equation) {
            ok = false;
            break;
        }
        if (!space_admit_atom(space, arena, equation))
            space_add(space, equation);
    }
    for (CettaCount i = 0u; ok && i < kept_len; i++)
        space_add_atom_id(space, kept[i]);
    free(mask);
    free(kept);
    return ok;
}

static void runtime_add_builtin_type_decls(Space *space, Arena *arena,
                                        CettaLanguageId language_id,
                                        const CettaProfile *profile) {
    if ((language_id == CETTA_LANGUAGE_HE ||
         language_id == CETTA_LANGUAGE_PRIME) &&
        profile && profile->enable_dependent_telescope)
        runtime_add_he_prime_typing_op_decls(space, arena);
    if (language_id == CETTA_LANGUAGE_PRIME)
        runtime_add_prime_semantic_op_decls(space, arena);
    if (runtime_try_add_builtin_type_decls_direct(space, language_id, profile))
        return;

    Atom *num = atom_symbol(arena, "Number");
    Atom *arrow_nnn = atom_expr(arena, (Atom*[]){
        atom_symbol_id(arena, g_builtin_syms.arrow), num, num, num}, 4);
    Atom *bool_t = atom_symbol(arena, "Bool");
    Atom *arrow_nnb = atom_expr(arena, (Atom*[]){
        atom_symbol_id(arena, g_builtin_syms.arrow), num, num, bool_t}, 4);
    static const char *arith_ops_base[] = {"+", "-", "*", "/", "%", NULL};
    static const char *arith_ops_formal[] = {"+", "-", "*", "/", "//", "%", NULL};
    const char **arith_ops =
        cetta_language_uses_rust_he_compat_semantics(language_id, profile)
        ? arith_ops_base
        : arith_ops_formal;
    const char *cmp_ops[] = {"<", ">", "<=", ">=", NULL};
    for (const char **op = arith_ops; *op; op++) {
        Atom *decl = atom_expr3(arena, atom_symbol_id(arena, g_builtin_syms.colon),
            atom_symbol(arena, *op), arrow_nnn);
        if (!space_admit_atom(space, arena, decl))
            space_add(space, decl);
    }
    for (const char **op = cmp_ops; *op; op++) {
        Atom *decl = atom_expr3(arena, atom_symbol_id(arena, g_builtin_syms.colon),
            atom_symbol(arena, *op), arrow_nnb);
        if (!space_admit_atom(space, arena, decl))
            space_add(space, decl);
    }
    Atom *t_var = atom_var(arena, "t");
    Atom *arrow_eq = atom_expr(arena, (Atom*[]){
        atom_symbol_id(arena, g_builtin_syms.arrow), t_var, t_var,
        atom_undefined_type(arena)}, 4);
    Atom *eq_decl = atom_expr3(arena, atom_symbol_id(arena, g_builtin_syms.colon),
        atom_symbol_id(arena, g_builtin_syms.equals), arrow_eq);
    if (!space_admit_atom(space, arena, eq_decl))
        space_add(space, eq_decl);
    Atom *arrow_eqeq = atom_expr(arena, (Atom*[]){
        atom_symbol_id(arena, g_builtin_syms.arrow), t_var, t_var, bool_t}, 4);
    Atom *eqeq_decl = atom_expr3(arena, atom_symbol_id(arena, g_builtin_syms.colon),
        atom_symbol(arena, "=="), arrow_eqeq);
    if (!space_admit_atom(space, arena, eqeq_decl))
        space_add(space, eqeq_decl);

}

bool cetta_runtime_load_prelude(Space *space, Arena *arena,
                                CettaLanguageId language, const CettaProfile *profile) {
    if (language == CETTA_LANGUAGE_HE && !he_library_tables_init()) return false;
    if (language == CETTA_LANGUAGE_PETTA) {
        const char *fallback = getenv("CETTA_PETTA_HE_STDLIB_FALLBACK");
        if (fallback && fallback[0] && strcmp(fallback, "0"))
            if (!stdlib_try_load_petta_shared_subset(space, arena)) return false;
    } else {
        if (!stdlib_try_load(space, arena)) return false;
        /* Record the shared library's signature provenance before adding the
         * active dialect's native declarations. Atom demand is uniform in Prime. */
        if (language == CETTA_LANGUAGE_PRIME)
            eval_prime_set_he_signature_space(space);
        runtime_add_builtin_type_decls(space, arena, language, profile);
        if (language == CETTA_LANGUAGE_PRIME &&
            !eval_prime_install_standard_library(space, arena)) return false;
    }
    if (language == CETTA_LANGUAGE_HE) {
        if (!runtime_he_prepare_library(space, arena, profile)) return false;
    }
    /* Extended profiles carry the public wrapper directly, without importing
     * a module or making the program an importer. */
    if (language == CETTA_LANGUAGE_HE)
        (void)space_mark_imported_prefix(space);
    return true;
}

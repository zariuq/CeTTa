/* Logical equality is an explicitly admitted interpretation. Observational
 * equality, even when it supports transport for selected predicates, does
 * not by itself authorize these higher-order rules. */
typedef enum {
    SJ_HOL_EQ_NONE, SJ_HOL_EQ_REFL, SJ_HOL_EQ_BETA, SJ_HOL_EQ_SYMM,
    SJ_HOL_EQ_TRANS, SJ_HOL_EQ_PROP, SJ_HOL_EQ_APP, SJ_HOL_EQ_ARG
} SjHolEqualityKind;

typedef struct {
    SjHolEqualityKind kind;
    Atom *carrier, *left, *right, *argument, *domain, *codomain, *function;
    Atom *first_proof, *second_proof, *conclusion;
} SjHolEqualityParts;

static SjHolEqualityKind sj_hol_equality_kind(Atom *proof) {
    if (!proof || proof->kind != ATOM_EXPR || !proof->expr.len) return SJ_HOL_EQ_NONE;
    Atom *head = proof->expr.elems[0];
    if (atom_is_symbol(head, "pf:eq-refl")) return SJ_HOL_EQ_REFL;
    if (atom_is_symbol(head, "pf:beta")) return SJ_HOL_EQ_BETA;
    if (atom_is_symbol(head, "pf:eq-symm")) return SJ_HOL_EQ_SYMM;
    if (atom_is_symbol(head, "pf:eq-trans")) return SJ_HOL_EQ_TRANS;
    if (atom_is_symbol(head, "pf:eq-prop-elim")) return SJ_HOL_EQ_PROP;
    if (atom_is_symbol(head, "pf:eq-app")) return SJ_HOL_EQ_APP;
    if (atom_is_symbol(head, "pf:eq-app-arg")) return SJ_HOL_EQ_ARG;
    return SJ_HOL_EQ_NONE;
}

static bool sj_hol_equality_adopted(SjProofState *st) {
    if (sj_space_declares(st->arena, st->user_space, "set:rule", "hol-equality")) {
        Atom *declaration = sj_expr2(st->arena, "set:rule", sj_sym(st->arena, "hol-equality"));
        sj_note_dependency(st, declaration, declaration);
        return true;
    }
    sj_fail(st, false, sj_expr2(st->arena, "set:rule-not-adopted", sj_sym(st->arena, "hol-equality")));
    return false;
}

static Atom *sj_hol_equation(SjProofState *st, Atom *carrier, Atom *left, Atom *right) {
    Atom *type = NULL;
    if (!sj_instance_constant(st, "eq", carrier, &type)) return NULL;
    Arena *a = st->arena;
    return sj_expr3(a, "App", sj_expr3(a, "App", sj_expr3(a, "App", sj_sym(a, "eq"), carrier), left), right);
}

static bool sj_hol_equation_parts(SjProofState *st, Atom *equation,
                                  Atom **carrier, Atom **left, Atom **right) {
    Atom *normal = sj_whnf(st->arena, equation, &st->fuel);
    if (!normal) { sj_fail_fuel(st, equation); return false; }
    Atom *head = NULL, *args[4] = {0};
    if (sj_spine(normal, &head, args, 4u) != 3u || !sj_constant_named(head, "eq")) {
        sj_fail(st, true, sj_expr2(st->arena, "set:equality-needs-equation", normal));
        return false;
    }
    *carrier = args[0]; *left = args[1]; *right = args[2];
    return true;
}

static Atom *sj_hol_checked_witness(SjProofState *st, Atom *authored, Atom *domain) {
    Atom *term = sj_witness(st, authored, domain);
    Atom **locals = term ? sj_locals_for(st->arena, 0u, term, NULL) : NULL;
    return locals ? sj_annotate_at(st, term, domain, locals, 0u) : NULL;
}

/* The seven rules introduced by the material proof compiler. All witnesses
 * and equation premises are checked; annotations never supply authority. */
static bool sj_hol_equality_parts(SjProofState *st, Atom *proof, SjHolEqualityParts *parts) {
    memset(parts, 0, sizeof *parts);
    Arena *a = st->arena;
    parts->kind = sj_hol_equality_kind(proof);
    if (!parts->kind || !sj_hol_equality_adopted(st)) return false;
    size_t required = parts->kind == SJ_HOL_EQ_REFL || parts->kind == SJ_HOL_EQ_TRANS ? 3u
        : parts->kind == SJ_HOL_EQ_BETA || parts->kind == SJ_HOL_EQ_APP || parts->kind == SJ_HOL_EQ_ARG ? 5u : 2u;
    if ((size_t)proof->expr.len != required) {
        sj_fail(st, true, sj_expr2(a, "set:equality-proof-arity", proof));
        return false;
    }
    if (parts->kind == SJ_HOL_EQ_REFL) {
        parts->carrier = sj_open_domain(st, proof->expr.elems[1]);
        parts->left = parts->carrier ? sj_hol_checked_witness(st, proof->expr.elems[2], parts->carrier) : NULL;
        parts->right = parts->left;
    } else if (parts->kind == SJ_HOL_EQ_BETA || parts->kind == SJ_HOL_EQ_APP || parts->kind == SJ_HOL_EQ_ARG) {
        parts->domain = sj_open_domain(st, proof->expr.elems[1]);
        parts->codomain = sj_open_domain(st, proof->expr.elems[2]);
        if (!parts->domain || !parts->codomain) {
            sj_fail(st, true, sj_expr2(a, "set:equality-carrier", proof));
            return false;
        }
        Atom *function_type = sj_expr3(a, "Pi", parts->domain, parts->codomain);
        if (parts->kind == SJ_HOL_EQ_BETA) {
            parts->argument = sj_hol_checked_witness(st, proof->expr.elems[3], parts->domain);
            parts->function = sj_hol_checked_witness(st, proof->expr.elems[4], function_type);
            Atom *body = parts->function ? sj_lambda_body(parts->function) : NULL;
            if (!body) {
                if (!st->failure) sj_fail(st, true, sj_expr2(a, "set:beta-needs-abstraction", proof));
                return false;
            }
            parts->carrier = parts->codomain;
            parts->left = parts->argument ? sj_expr3(a, "App", parts->function, parts->argument) : NULL;
            parts->right = parts->argument ? sj_subst(a, body, 0u, parts->argument) : NULL;
        } else {
            parts->first_proof = proof->expr.elems[parts->kind == SJ_HOL_EQ_APP ? 3u : 4u];
            Atom *equation = sj_synth(st, parts->first_proof);
            if (!equation || !sj_hol_equation_parts(st, equation, &parts->carrier, &parts->left, &parts->right)) return false;
            if (!sj_convertible(st, parts->carrier, parts->kind == SJ_HOL_EQ_APP ? function_type : parts->domain)) return false;
            if (parts->kind == SJ_HOL_EQ_APP) {
                parts->argument = sj_hol_checked_witness(st, proof->expr.elems[4], parts->domain);
                if (!parts->argument) return false;
                parts->conclusion = sj_hol_equation(st, parts->codomain,
                    sj_expr3(a, "App", parts->left, parts->argument), sj_expr3(a, "App", parts->right, parts->argument));
            } else {
                parts->function = sj_hol_checked_witness(st, proof->expr.elems[3], function_type);
                if (!parts->function) return false;
                parts->conclusion = sj_hol_equation(st, parts->codomain,
                    sj_expr3(a, "App", parts->function, parts->left), sj_expr3(a, "App", parts->function, parts->right));
            }
            return parts->conclusion != NULL;
        }
    } else {
        parts->first_proof = proof->expr.elems[1];
        Atom *equation = sj_synth(st, parts->first_proof);
        if (!equation || !sj_hol_equation_parts(st, equation, &parts->carrier, &parts->left, &parts->right)) return false;
        if (parts->kind == SJ_HOL_EQ_SYMM) {
            parts->conclusion = sj_hol_equation(st, parts->carrier, parts->right, parts->left);
        } else if (parts->kind == SJ_HOL_EQ_TRANS) {
            Atom *other_carrier = NULL, *middle = NULL, *last = NULL;
            parts->second_proof = proof->expr.elems[2];
            Atom *second = sj_synth(st, parts->second_proof);
            if (!second || !sj_hol_equation_parts(st, second, &other_carrier, &middle, &last) ||
                !sj_convertible(st, parts->carrier, other_carrier) || !sj_convertible(st, parts->right, middle)) return false;
            parts->argument = last;
            parts->conclusion = sj_hol_equation(st, parts->carrier, parts->left, last);
        } else {
            if (!sj_convertible(st, parts->carrier, sj_sym(a, "prop"))) return false;
            parts->conclusion = sj_expr3(a, "App", sj_expr3(a, "App", sj_sym(a, "imp"), parts->left), parts->right);
        }
        return parts->conclusion != NULL;
    }
    if (!parts->carrier || !parts->left || !parts->right) {
        if (!st->failure) sj_fail(st, true, sj_expr2(a, "set:equality-witness", proof));
        return false;
    }
    parts->conclusion = sj_hol_equation(st, parts->carrier, parts->left, parts->right);
    return parts->conclusion != NULL;
}

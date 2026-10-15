/* prime_he_fragments.inc - HE-style declarations, answered through the HOL
 * contract and decided by the hosted HOL Light checker.
 *
 * Prime answers `type:of`, `type:check`, `type:analyze`, `type:formed` and
 * `get-type` on HE-style declarations `(: name type)` without HE's type
 * engine.  HE's declarations mean a stable HOL contract, HE's theory of
 * first-order prenex schemes hosted in HOL, which depends on no Prime
 * candidate (Lean: Mettapedia.Languages.MeTTa.HE.HOLContract).  Contract
 * typing is HE's first-order rule (Lean: HOLContract.typed_iff_firstOrder):
 *
 *  - every declaration is its own constant: `(: c T)`, at its occurrence
 *    among the declarations of `c`, types `c` at every instance of `T`;
 *  - an application `(f a1 ... an)` of an arrow `(-> P1 ... Pn R)` to
 *    exactly n arguments of the parameter types has the return type, and an
 *    application of another arity has no type;
 *  - an expression whose head has a non-arrow type has the tuple of its
 *    elements' types.
 *
 * For a judgment this file
 *
 *  1. enumerates its derivations in HE's order: for an expression, the
 *     tuples through each non-arrow type of its head, then the applications
 *     through each arrow type of its head, each with every choice among the
 *     derivations of its arguments.  Each occurrence of a declaration is
 *     renamed apart, and the types of a derivation are unified first-order.
 *     An application of another arity, or one whose argument does not unify
 *     with its parameter, is a failed attempt;
 *  2. translates each derivation into the hosted HOL Light checker.  An HE
 *     base type is a nullary type operator.  An arrow `(-> P1 ... Pn R)` is
 *     the (n+1)-ary type operator `he-arrow:n`, so arrows of different
 *     arities are different types, and an arrow returning an arrow is not an
 *     arrow of another arity.  A compound non-arrow type `(T0 T1 ... Tn)` is
 *     the product `T0 # (T1 # ... # (Tn # 1))`.  The k-th declaration of `c`
 *     is the constant `he:k:c` at its declared type.  An application is
 *     `he-apply:n f a1 ... an`, where the polymorphic constant
 *     `he-apply:n : he-arrow:n A A' ... B -> A -> A' -> ... -> B` applies an
 *     arrow to exactly its arguments.  A tuple `(x0 x1 ... xn)` is the pair
 *     `x0, (x1, ... (xn, one))`;
 *  3. asks the checker.  A derivation is established only when the checker
 *     reads its term and computes its type; the type the checker computes
 *     is the contract type of the elaborated term (Lean:
 *     HOLContract.Guest.guestTyped_iff).  A failed attempt counts as a refutation
 *     only when the checker reads each part of it and refuses its failing
 *     step.  A judgment is refuted when every derivation is refused at the
 *     type asked and every failed attempt is refused.
 *
 * The contract terms of a judgment correspond one to one to its
 * derivations (Lean: HOLContract.judgmentTermEquiv), so `get-type` answers
 * one type per derivation, as HE does: a symbol declared twice is two
 * constants, and `(h b)` under two declarations of `h` and of `b` has two
 * answers (Lean: HOLContract.Multiplicity.g_occurrences_differ,
 * hb_derivations, fb_derivations).
 *
 * The fragments, with their agreement theorems:
 *
 *  - Static arrows.  On unique declarations of variable-free contract
 *    types, contract typing at a static type is HE's specified exact
 *    relation (Lean: HOLContract.static_iff_exact).
 *  - First-order schemes.  A declaration with type variables is a
 *    polymorphic constant, read at the instance its occurrence takes; its
 *    variables range over all contract types.  On first-order expressions
 *    over schematic declarations, HE's candidate is the principal type
 *    scheme (Lean: HOLContract.schematic_iff_exact,
 *    HOLContract.principal_of_exact), so a scheme is refuted only there:
 *    elsewhere a scheme is established when the checker accepts an instance
 *    and is never refuted.  Prime reads an open declaration as a scheme
 *    only when `(type-scheme name)` admits it; the variables of any other
 *    open declaration are rigid and never solved.  A telescope component
 *    `(: $x A)` whose variable occurs in no later component binds nothing
 *    and is read as its domain `A`: a dependent arrow whose variable is
 *    unused is the simple arrow (Lean:
 *    Logic.HOL.Embedding.SimpleSliceOfDependent.isSimple_iff_image).
 *    Genuinely dependent telescopes stay outside.
 *  - Tuples.  An expression whose head has a non-arrow type, with at least
 *    one further element, has the tuple of its elements' types, read as the
 *    product of their types (Lean: HOLContract.tuple_iff_reference).
 *    The product is HOL Light's pair type with its unit, given here by the
 *    signature of HOL Light's pair and unit theories: `prod`, `1`, `,`,
 *    `FST`, `SND` and `one`.  The projection laws and surjective pairing
 *    hold in every model of the contract (Lean: HOLContract.ProductModel).
 *  - Meta-types.  `Symbol`, `Variable`, `Grounded` and `Expression` classify
 *    an atom by its shape: the checker admits the classification axiom of
 *    the quoted atom (Lean: TypeGrounding.MetaTypes).  `Atom` is the top.
 *  - `%Undefined%` is the unknown type of Abstracting Gradual Typing, an
 *    interpretation beside the contract rather than inside it: on
 *    variable-free types HE's acceptance of a pair of types is consistency
 *    of their concretizations (Lean: HOLContract.gradualTyped_iff_consistent).
 *    A consistency answer is decided there; the static type it relies on,
 *    when there is one, comes from the checker.  An application whose
 *    arguments are consistent with its head's parameters has the head's
 *    return type as its gradual type (Lean: HOLContract.GradualApplication),
 *    through a first-order scheme at a first-order valuation of its
 *    variables (Lean: HOLContract.GradualSchemeApplication).
 *
 * Outside every fragment the answer is Undetermined with a reason, and
 * nothing here refutes: a type that no fragment derives may still be derived
 * by Prime's kernel or its set face.  The traversals use explicit worklists,
 * so the depth of a term or type is bounded only by memory. */

typedef enum {
    PRIME_HE_TYPED = 0,
    PRIME_HE_REJECTED,
    PRIME_HE_DYNAMIC,
    PRIME_HE_OUTSIDE
} PrimeHeFragmentStatus;

/* One derivation of HE's first-order rule, elaborated: its type and its
 * elaborated term, with the unifications of the derivation applied.  The
 * variables that remain are those of its principal type scheme.  The
 * elaborated term has one node per term node: `(HeConst type atom declared
 * occurrence)`, `(HeApp type head arg ...)` and `(HeTuple type head
 * element ...)`. */
typedef struct {
    Atom *type;
    Atom *annotation;
    /* A tuple node occurs. */
    bool tuples;
    /* A declaration with type variables occurs. */
    bool schematic;
    /* Inside the first-order schematic fragment: every declaration it uses
     * is a first-order type or an operator scheme, an operator scheme only
     * in head position, no argument has an arrow type, and no tuple
     * occurs. */
    bool first_order;
    /* A constant whose open declaration no `type-scheme` admits: its
     * variables are rigid. */
    bool rigid;
    /* A declaration with binders `(: $x A)` occurs: the hosted LF kernel
     * decides the derivation. */
    bool dependent;
} PrimeHeDerivation;

/* A way of typing that fails at one application step: an application of
 * another arity, `(HeArity count head)`, or an argument that does not unify
 * with its parameter, `(HeMismatch head argument ...)` with the failing
 * argument last and the earlier arguments unified.  The checker is asked to
 * read each part and to refuse the step. */
typedef struct {
    Atom *annotation;
    Atom *reason;
    /* The refusal of the step refutes every instance of the attempt:
     * unification is principal on the contract's types, arrows and products
     * included (Lean: HOLContract.Principal.principal_decides), and on a
     * dependent derivation's LF proofs. */
    bool refutable;
    /* A declaration with binders occurs: the hosted LF kernel reads the
     * parts and refuses the step. */
    bool dependent;
} PrimeHeAttempt;

/* What a term's derivations come to.  PRIME_HE_TYPED: at least one
 * derivation, in HE's order, beside the failed attempts of the term and of
 * its parts.  PRIME_HE_REJECTED: no derivation, and the failed attempts that
 * show it.  PRIME_HE_DYNAMIC: no declaration types any part of the term.
 * PRIME_HE_OUTSIDE: some way of typing it lies outside the fragments, with
 * the reason. */
typedef struct {
    PrimeHeFragmentStatus status;
    PrimeHeDerivation *derivations;
    size_t derivation_len;
    PrimeHeAttempt *attempts;
    size_t attempt_len;
    Atom *reason;
} PrimeHeCandidates;

static const char *const PRIME_HE_RESERVED_TYPE_NAMES[] = {
    "%Undefined%", "Atom", "Symbol", "Variable", "Expression", "Grounded",
    "->", NULL};

/* Prime's primitive types, which formation reads as types of kind `Type`
 * when the space declares nothing for them. */
static const char *const PRIME_HE_PRIMITIVE_KIND_NAMES[] = {
    "Atom", "Symbol", "Variable", "Expression", "Grounded", "Number", "Bool",
    "String", "ErrorType", NULL};

static const char *const PRIME_HE_META_TYPE_NAMES[] = {
    "Symbol", "Variable", "Grounded", "Expression", NULL};

static bool prime_he_symbol_in(Atom *atom, const char *const *names) {
    if (!atom || atom->kind != ATOM_SYMBOL) return false;
    for (size_t i = 0u; names[i]; i++)
        if (atom_is_symbol(atom, names[i])) return true;
    return false;
}

static bool prime_he_arrow_shaped(Atom *atom) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len > 0u &&
           atom_is_symbol_id(atom->expr.elems[0], g_builtin_syms.arrow);
}

/* An arrow with a dependent telescope component `(: x A)`. */
static bool prime_he_telescope(Atom *type) {
    if (!prime_he_arrow_shaped(type)) return false;
    for (CettaExprIndex i = 1u; i < type->expr.len; i++) {
        Atom *component = type->expr.elems[i];
        if (component->kind == ATOM_EXPR && component->expr.len == 3u &&
            atom_is_symbol_id(component->expr.elems[0], g_builtin_syms.colon))
            return true;
    }
    return false;
}

/* `(type-scheme name)` admits the open declaration of `name` as a scheme.
 * The indexed lookup answers when it applies; a marker from an imported
 * module is read from the composed view. */
static bool prime_he_scheme_admitted(Space *space, Atom *term) {
    if (space_atom_is_exact_indexable(term)) {
        Atom *arguments[1] = {term};
        bool applicable = false;
        bool found = space_contains_exact_symbol_application(
            space, g_builtin_syms.type_scheme, arguments, 1u, &applicable);
        if (applicable) return found;
    }
    CettaCount len = space_view_length64(space);
    for (CettaIndex i = 0; i < len; i++) {
        Atom *row = space_view_get_at64(space, i);
        if (row && row->kind == ATOM_EXPR && row->expr.len == 2u &&
            atom_is_symbol_id(row->expr.elems[0], g_builtin_syms.type_scheme) &&
            atom_eq(row->expr.elems[1], term))
            return true;
    }
    return false;
}

static PrimeHeCandidates prime_he_outside(Atom *reason) {
    return (PrimeHeCandidates){.status = PRIME_HE_OUTSIDE, .reason = reason};
}

static PrimeHeCandidates prime_he_dynamic(void) {
    return (PrimeHeCandidates){.status = PRIME_HE_DYNAMIC};
}

static PrimeHeCandidates prime_he_allocation_failure(Arena *a) {
    return prime_he_outside(prime_expr1(a, "fragment-traversal-allocation"));
}

/* The elaborated constant occurrence `(HeConst type atom declared
 * occurrence)`: the `occurrence`-th declaration of `atom`, of declared type
 * `declared`, at the instance `type`. */
static Atom *prime_he_constant_node(Arena *a, Atom *type, Atom *atom,
                                    Atom *declared, uint32_t occurrence) {
    Atom *items[5] = {prime_sym(a, "HeConst"), type, atom, declared,
                      atom_int(a, (int64_t)occurrence)};
    return atom_expr(a, items, 5);
}

/* `(HeApp type head arg ...)` or `(HeTuple type head element ...)` over the
 * elaborated parts. */
static Atom *prime_he_compound_node(Arena *a, const char *head, Atom *type,
                                    Atom *const *parts, size_t count) {
    Atom **items = arena_alloc(a, sizeof(Atom *) * (count + 2u));
    items[0] = prime_sym(a, head);
    items[1] = type;
    for (size_t i = 0u; i < count; i++) items[i + 2u] = parts[i];
    return atom_expr(a, items, (CettaExprLen)(count + 2u));
}

typedef struct {
    Atom **items;
    size_t len;
    size_t cap;
} PrimeHeAtomStack;

static bool prime_he_stack_push(PrimeHeAtomStack *stack, Atom *atom) {
    if (stack->len == stack->cap) {
        size_t next_cap = stack->cap ? stack->cap * 2u : 16u;
        if (next_cap <= stack->cap || next_cap > SIZE_MAX / sizeof(Atom *))
            return false;
        Atom **next = realloc(stack->items, sizeof(Atom *) * next_cap);
        if (!next) return false;
        stack->items = next;
        stack->cap = next_cap;
    }
    stack->items[stack->len++] = atom;
    return true;
}

/* A Prime-native named binder `(x : A)` anywhere in the type: Prime's own
 * dependent syntax, which HE's reading would take for a compound type.  The
 * HE-style fragments abstain on it. */
static bool prime_he_native_binder(Atom *root) {
    PrimeHeAtomStack stack = {0};
    bool found = false;
    bool ok = prime_he_stack_push(&stack, root);
    while (ok && !found && stack.len > 0u) {
        Atom *atom = stack.items[--stack.len];
        if (atom->kind != ATOM_EXPR) continue;
        if (atom->expr.len == 3u &&
            atom_is_symbol_id(atom->expr.elems[1], g_builtin_syms.colon)) {
            found = true;
            break;
        }
        for (CettaExprIndex i = 0u; ok && i < atom->expr.len; i++)
            ok = prime_he_stack_push(&stack, atom->expr.elems[i]);
    }
    free(stack.items);
    /* Without memory to look, abstain as well. */
    return found || !ok;
}

/* A telescope component `(: $x A)`, the `he-prime` binder that names its
 * argument: its variable and its domain. */
static bool prime_he_binder_component(Atom *component, Atom **var_out,
                                      Atom **domain_out) {
    if (component && component->kind == ATOM_EXPR && component->expr.len == 3u &&
        atom_is_symbol_id(component->expr.elems[0], g_builtin_syms.colon) &&
        component->expr.elems[1]->kind == ATOM_VAR) {
        *var_out = component->expr.elems[1];
        *domain_out = component->expr.elems[2];
        return true;
    }
    return false;
}

static bool prime_he_var_occurs_in(Atom *root, VarId id, bool *ok);

/* Named domains may use earlier binders only. Repeated names do not have
 * one shared interpretation across the elaborator and hosted checker. */
static bool prime_he_binder_telescope(Atom *type) {
    if (!prime_he_arrow_shaped(type)) return false;
    for (CettaExprIndex i = 1u; i + 1u < type->expr.len; i++) {
        Atom *component = type->expr.elems[i];
        Atom *var = NULL, *domain = NULL;
        if (component->kind == ATOM_EXPR && component->expr.len == 3u &&
            atom_is_symbol_id(component->expr.elems[0], g_builtin_syms.colon) &&
            !prime_he_binder_component(component, &var, &domain))
            return false;
        Atom *checked_domain = var ? domain : component;
        for (CettaExprIndex j = 1u; j + 1u < type->expr.len; j++) {
            Atom *other = NULL, *other_domain = NULL;
            if (!prime_he_binder_component(type->expr.elems[j],
                                           &other, &other_domain))
                continue;
            if (var && j != i && var->var_id == other->var_id)
                return false;
            bool ok = true;
            if (j >= i &&
                (prime_he_var_occurs_in(checked_domain, other->var_id, &ok) || !ok))
                return false;
        }
    }
    return true;
}

/* A variable of the type that no binder of its own telescope names. */
static bool prime_he_implicit_variables(Atom *type, bool *ok) {
    *ok = true;
    if (!atom_has_vars(type)) return false;
    PrimeHeAtomStack stack = {0};
    bool found = false;
    *ok = prime_he_stack_push(&stack, type);
    while (*ok && !found && stack.len > 0u) {
        Atom *atom = stack.items[--stack.len];
        if (atom->kind == ATOM_VAR) {
            bool bound = false;
            if (prime_he_arrow_shaped(type))
                for (CettaExprIndex i = 1u; !bound && i + 1u < type->expr.len; i++) {
                    Atom *var = NULL, *domain = NULL;
                    bound = prime_he_binder_component(type->expr.elems[i], &var, &domain) &&
                            var->var_id == atom->var_id;
                }
            found = !bound;
        } else if (atom->kind == ATOM_EXPR) {
            for (CettaExprIndex i = 0u; *ok && i < atom->expr.len; i++)
                *ok = prime_he_stack_push(&stack, atom->expr.elems[i]);
        }
    }
    free(stack.items);
    return *ok && found;
}

/* A contract type, possibly with type variables: a type atom that is not a
 * reserved name.  A leaf is a type variable, a literal or a symbol other
 * than the arrow and `%Undefined%`; an arrow `(-> C1 ... Ck)` has at least
 * its return, and its components are type atoms other than the reserved
 * names; any other expression has type atoms as elements (Lean:
 * HOLContract.ContractType, HOLContract.TypeAtom).  Below an arrow
 * component and outside arrows, a reserved name such as `Atom` is an
 * ordinary type constant. */
static bool prime_he_contract_type(Atom *root, bool *ok) {
    *ok = true;
    if (prime_he_symbol_in(root, PRIME_HE_RESERVED_TYPE_NAMES)) return false;
    PrimeHeAtomStack stack = {0};
    bool result = true;
    *ok = prime_he_stack_push(&stack, root);
    while (*ok && result && stack.len > 0u) {
        Atom *atom = stack.items[--stack.len];
        if (atom->kind == ATOM_SYMBOL) {
            if (atom_is_symbol_id(atom, g_builtin_syms.arrow) ||
                atom_is_symbol_id(atom, g_builtin_syms.undefined_type))
                result = false;
        } else if (atom->kind == ATOM_EXPR) {
            bool arrow = prime_he_arrow_shaped(atom);
            if (arrow && atom->expr.len < 2u) {
                result = false;
                break;
            }
            for (CettaExprIndex i = arrow ? 1u : 0u;
                 *ok && result && i < atom->expr.len; i++) {
                Atom *part = atom->expr.elems[i];
                if (arrow && prime_he_symbol_in(part, PRIME_HE_RESERVED_TYPE_NAMES))
                    result = false;
                else
                    *ok = prime_he_stack_push(&stack, part);
            }
        }
    }
    free(stack.items);
    return *ok && result;
}

/* A variable-free contract type: the static types of the contract.  An
 * arrow may return an arrow, and an arrow may be a component. */
static bool prime_he_ground_contract_type(Atom *root, bool *ok) {
    *ok = true;
    if (atom_has_vars(root)) return false;
    return prime_he_contract_type(root, ok);
}

static bool prime_he_first_order(Atom *root, bool *ok) {
    PrimeHeAtomStack stack = {0};
    bool result = true;
    *ok = prime_he_stack_push(&stack, root);
    while (*ok && result && stack.len > 0u) {
        Atom *atom = stack.items[--stack.len];
        if (atom->kind == ATOM_SYMBOL) {
            if (prime_he_symbol_in(atom, PRIME_HE_RESERVED_TYPE_NAMES) ||
                atom_is_symbol_id(atom, g_builtin_syms.undefined_type) ||
                atom_is_symbol_id(atom, g_builtin_syms.arrow))
                result = false;
        } else if (atom->kind == ATOM_EXPR) {
            for (CettaExprIndex i = 0u; *ok && i < atom->expr.len; i++)
                *ok = prime_he_stack_push(&stack, atom->expr.elems[i]);
        }
    }
    free(stack.items);
    return *ok && result;
}

/* An operator scheme `(-> B1 ... Bn R)`, n >= 1, with first-order
 * components (Lean: TypeGrounding.Schematic.OperatorScheme). */
static bool prime_he_operator_scheme(Atom *type, bool *ok) {
    *ok = true;
    if (!prime_he_arrow_shaped(type) || type->expr.len < 3u) return false;
    for (CettaExprIndex i = 1u; i < type->expr.len; i++)
        if (!prime_he_first_order(type->expr.elems[i], ok)) return false;
    return true;
}

/* ---- Substitutions of type variables and first-order unification ------ */

typedef struct {
    VarId id;
    Atom *value;
} PrimeHeBinding;

typedef struct {
    PrimeHeBinding *items;
    size_t len;
    size_t cap;
} PrimeHeSubst;

static Atom *prime_he_subst_lookup(const PrimeHeSubst *subst, VarId id) {
    for (size_t i = 0u; i < subst->len; i++)
        if (subst->items[i].id == id) return subst->items[i].value;
    return NULL;
}

static bool prime_he_subst_bind(PrimeHeSubst *subst, VarId id, Atom *value) {
    if (subst->len == subst->cap) {
        size_t next_cap = subst->cap ? subst->cap * 2u : 8u;
        if (next_cap <= subst->cap ||
            next_cap > SIZE_MAX / sizeof(PrimeHeBinding))
            return false;
        PrimeHeBinding *next =
            realloc(subst->items, sizeof(PrimeHeBinding) * next_cap);
        if (!next) return false;
        subst->items = next;
        subst->cap = next_cap;
    }
    subst->items[subst->len++] = (PrimeHeBinding){.id = id, .value = value};
    return true;
}

/* Follow the bindings of a variable at the top of an atom.  The occurs
 * check keeps the bindings acyclic. */
static Atom *prime_he_subst_walk(const PrimeHeSubst *subst, Atom *atom) {
    while (atom->kind == ATOM_VAR) {
        Atom *value = prime_he_subst_lookup(subst, atom->var_id);
        if (!value) break;
        atom = value;
    }
    return atom;
}

/* The variable occurs in the atom under the substitution. */
static bool prime_he_occurs(const PrimeHeSubst *subst, VarId id, Atom *root,
                            bool *ok) {
    PrimeHeAtomStack stack = {0};
    bool found = false;
    *ok = prime_he_stack_push(&stack, root);
    while (*ok && !found && stack.len > 0u) {
        Atom *atom = prime_he_subst_walk(subst, stack.items[--stack.len]);
        if (atom->kind == ATOM_VAR) {
            found = atom->var_id == id;
        } else if (atom->kind == ATOM_EXPR) {
            for (CettaExprIndex i = 0u; *ok && i < atom->expr.len; i++)
                *ok = prime_he_stack_push(&stack, atom->expr.elems[i]);
        }
    }
    free(stack.items);
    return *ok && found;
}

/* Extend the substitution to a most general unifier of the two atoms:
 * syntactic unification with the occurs check, the equation theory of a
 * successful presentation match on first-order types.  Returns false when
 * the atoms do not unify, or with `*ok` false when memory runs out. */
static bool prime_he_unify(PrimeHeSubst *subst, Atom *left, Atom *right,
                           bool *ok) {
    PrimeHeAtomStack stack = {0};
    bool result = true;
    *ok = prime_he_stack_push(&stack, left) &&
          prime_he_stack_push(&stack, right);
    while (*ok && result && stack.len > 0u) {
        Atom *r = prime_he_subst_walk(subst, stack.items[--stack.len]);
        Atom *l = prime_he_subst_walk(subst, stack.items[--stack.len]);
        if (l->kind == ATOM_VAR && r->kind == ATOM_VAR && l->var_id == r->var_id)
            continue;
        if (l->kind == ATOM_VAR || r->kind == ATOM_VAR) {
            Atom *variable = l->kind == ATOM_VAR ? l : r;
            Atom *value = l->kind == ATOM_VAR ? r : l;
            bool occurs_ok = true;
            if (prime_he_occurs(subst, variable->var_id, value, &occurs_ok))
                result = false;
            else if (!occurs_ok)
                *ok = false;
            else
                *ok = prime_he_subst_bind(subst, variable->var_id, value);
            continue;
        }
        if (l->kind == ATOM_EXPR && r->kind == ATOM_EXPR) {
            if (l->expr.len != r->expr.len) {
                result = false;
                break;
            }
            for (CettaExprIndex i = 0u; *ok && i < l->expr.len; i++)
                *ok = prime_he_stack_push(&stack, l->expr.elems[i]) &&
                      prime_he_stack_push(&stack, r->expr.elems[i]);
            continue;
        }
        if (l->kind == ATOM_EXPR || r->kind == ATOM_EXPR || !atom_eq(l, r))
            result = false;
    }
    free(stack.items);
    return *ok && result;
}

typedef struct {
    Atom *term;
    size_t first_result;
} PrimeHeFrame;

/* Grow a dynamic array by doubling; sets `ok` false when it cannot. */
#define PRIME_HE_GROW(array, len, cap, type, ok)                              \
    do {                                                                      \
        if ((len) == (cap)) {                                                 \
            size_t next_cap = (cap) ? (cap) * 2u : 16u;                       \
            type *next = next_cap > SIZE_MAX / sizeof(type)                   \
                ? NULL : realloc((array), sizeof(type) * next_cap);           \
            if (!next) { (ok) = false; break; }                               \
            (array) = next;                                                   \
            (cap) = next_cap;                                                 \
        }                                                                     \
    } while (0)

/* Apply the substitution throughout an atom, bottom-up with explicit
 * stacks.  Returns NULL with `*ok` false when memory runs out. */
static Atom *prime_he_subst_resolve(Arena *a, const PrimeHeSubst *subst,
                                    Atom *root, bool *ok) {
    if (subst->len == 0u) {
        *ok = true;
        return root;
    }
    PrimeHeFrame *frames = NULL;
    size_t frame_len = 0u, frame_cap = 0u;
    Atom **results = NULL;
    size_t result_len = 0u, result_cap = 0u;
    Atom *answer = NULL;
    bool good = true;
    PRIME_HE_GROW(frames, frame_len, frame_cap, PrimeHeFrame, good);
    if (good) frames[frame_len++] = (PrimeHeFrame){.term = root};
    while (good && frame_len > 0u) {
        PrimeHeFrame *frame = &frames[frame_len - 1u];
        Atom *current = prime_he_subst_walk(subst, frame->term);
        size_t done = result_len - frame->first_result;
        if (current->kind == ATOM_EXPR && done < (size_t)current->expr.len) {
            Atom *child = current->expr.elems[done];
            PRIME_HE_GROW(frames, frame_len, frame_cap, PrimeHeFrame, good);
            if (!good) break;
            frames[frame_len++] =
                (PrimeHeFrame){.term = child, .first_result = result_len};
            continue;
        }
        Atom *result = current;
        if (current->kind == ATOM_EXPR) {
            result = atom_expr(a, results + frame->first_result,
                               current->expr.len);
            result_len = frame->first_result;
        }
        frame_len--;
        PRIME_HE_GROW(results, result_len, result_cap, Atom *, good);
        if (!good) break;
        results[result_len++] = result;
    }
    if (good && result_len == 1u) answer = results[0];
    free(frames);
    free(results);
    *ok = answer != NULL;
    return answer;
}

/* Rename every variable of an atom to a fresh activation of it, so that one
 * occurrence of a declaration has its own scope. */
static Atom *prime_he_rename_apart(Arena *a, Atom *root, bool *ok) {
    Atom *renamed = atom_has_vars(root)
        ? rename_vars_except(a, root, atom_unit(a)) : root;
    *ok = renamed != NULL;
    return renamed;
}

/* ---- Vacuous telescope binders ----------------------------------------- */

/* The variable occurs somewhere in the atom. */
static bool prime_he_var_occurs_in(Atom *root, VarId id, bool *ok) {
    PrimeHeAtomStack stack = {0};
    bool found = false;
    *ok = prime_he_stack_push(&stack, root);
    while (*ok && !found && stack.len > 0u) {
        Atom *atom = stack.items[--stack.len];
        if (atom->kind == ATOM_VAR) {
            found = atom->var_id == id;
        } else if (atom->kind == ATOM_EXPR) {
            for (CettaExprIndex i = 0u; *ok && i < atom->expr.len; i++)
                *ok = prime_he_stack_push(&stack, atom->expr.elems[i]);
        }
    }
    free(stack.items);
    return *ok && found;
}

/* A telescope component `(: $x A)` binds `$x` for the components after it.
 * When `$x` occurs in none of them, nor in `A`, the component binds
 * nothing: a dependent arrow whose variable is unused is the simple arrow,
 * the non-dependent slice of the dependent core (Lean:
 * Logic.HOL.Embedding.SimpleSliceOfDependent.isSimple_iff_image).  Such a
 * component is read as its domain `A`.  A telescope with a component that
 * binds stays as written, the dependent route reads all of it.  NULL with
 * `*ok` false when memory runs out. */
static Atom *prime_he_without_vacuous_binders(Arena *a, Atom *type, bool *ok) {
    *ok = true;
    if (!prime_he_telescope(type)) return type;
    CettaExprIndex last = type->expr.len - 1u;
    Atom **items = NULL;
    for (CettaExprIndex i = 1u; *ok && i < last; i++) {
        Atom *component = type->expr.elems[i];
        if (component->kind != ATOM_EXPR || component->expr.len != 3u ||
            !atom_is_symbol_id(component->expr.elems[0], g_builtin_syms.colon) ||
            component->expr.elems[1]->kind != ATOM_VAR)
            continue;
        VarId id = component->expr.elems[1]->var_id;
        bool used = prime_he_var_occurs_in(component->expr.elems[2], id, ok);
        for (CettaExprIndex j = i + 1u; *ok && !used && j <= last; j++)
            used = prime_he_var_occurs_in(type->expr.elems[j], id, ok);
        if (!*ok || used) continue;
        if (!items) {
            items = malloc(sizeof(*items) * type->expr.len);
            if (!items) {
                *ok = false;
                break;
            }
            memcpy(items, type->expr.elems, sizeof(*items) * type->expr.len);
        }
        items[i] = component->expr.elems[2];
    }
    Atom *read = !*ok ? NULL
               : items ? atom_expr(a, items, type->expr.len) : type;
    free(items);
    if (read && read != type && prime_he_telescope(read)) read = type;
    return read;
}

/* The declared types of `term`, each read without its vacuous binders. */
static uint32_t prime_he_declared_types(Space *space, Arena *a, Atom *term,
                                        Atom ***types_out) {
    uint32_t count = space_get_declared_types(space, a, term, types_out);
    for (uint32_t i = 0u; i < count; i++) {
        bool ok = true;
        /* Check the authored telescope before erasing unused binders. */
        if (prime_he_telescope((*types_out)[i]) &&
            !prime_he_binder_telescope((*types_out)[i]))
            continue;
        Atom *read = prime_he_without_vacuous_binders(a, (*types_out)[i], &ok);
        if (ok) (*types_out)[i] = read;
    }
    return count;
}

/* ---- Subtype declarations ---------------------------------------------- */

/* HE's subtype declaration `(:< A B)` gives everything of type `A` the type
 * `B` as well.  The contract has no subtyping, so a type that a subtype
 * declaration reaches has types the contract does not see: a judgment
 * through it lies outside the fragments and is never refuted.  HE extends a
 * symbol's declared types and a tuple's types this way, not the type an
 * application returns. */
static bool prime_he_subtype_reaches(Space *space, Arena *a, Atom *type,
                                     bool *ok) {
    *ok = true;
    CettaCount len = space_view_length64(space);
    for (CettaIndex i = 0; i < len; i++) {
        Atom *row = space_view_get_at64(space, i);
        if (!row || row->kind != ATOM_EXPR || row->expr.len != 3u ||
            !is_symbol_named(row->expr.elems[0], ":<"))
            continue;
        Atom *sub = prime_he_rename_apart(a, row->expr.elems[1], ok);
        if (!*ok) return false;
        PrimeHeSubst subst = {0};
        bool unifies = prime_he_unify(&subst, sub, type, ok);
        free(subst.items);
        if (!*ok) return false;
        if (unifies) return true;
    }
    return false;
}

/* The result for a symbol or tuple of type `type`: outside when a subtype
 * declaration reaches it. */

/* ---- Derivations ------------------------------------------------------- */

typedef struct {
    PrimeHeDerivation *items;
    size_t len;
    size_t cap;
} PrimeHeDerivationList;

typedef struct {
    PrimeHeAttempt *items;
    size_t len;
    size_t cap;
} PrimeHeAttemptList;

static bool prime_he_push_derivation(PrimeHeDerivationList *list,
                                     PrimeHeDerivation derivation) {
    bool ok = true;
    PRIME_HE_GROW(list->items, list->len, list->cap, PrimeHeDerivation, ok);
    if (!ok) return false;
    list->items[list->len++] = derivation;
    return true;
}

static bool prime_he_push_attempt(PrimeHeAttemptList *list,
                                  PrimeHeAttempt attempt) {
    bool ok = true;
    PRIME_HE_GROW(list->items, list->len, list->cap, PrimeHeAttempt, ok);
    if (!ok) return false;
    list->items[list->len++] = attempt;
    return true;
}

/* Move the derivations and the attempts into the arena: typed when there is
 * a derivation, rejected otherwise. */
static PrimeHeCandidates prime_he_candidates_finish(
    Arena *a, PrimeHeDerivationList *derivations, PrimeHeAttemptList *attempts) {
    PrimeHeCandidates out = {
        .status = derivations->len > 0u ? PRIME_HE_TYPED : PRIME_HE_REJECTED,
        .derivation_len = derivations->len,
        .attempt_len = attempts->len};
    if (derivations->len > 0u) {
        out.derivations =
            arena_alloc(a, sizeof(PrimeHeDerivation) * derivations->len);
        memcpy(out.derivations, derivations->items,
               sizeof(PrimeHeDerivation) * derivations->len);
    }
    if (attempts->len > 0u) {
        out.attempts = arena_alloc(a, sizeof(PrimeHeAttempt) * attempts->len);
        memcpy(out.attempts, attempts->items,
               sizeof(PrimeHeAttempt) * attempts->len);
        out.reason = attempts->items[0].reason;
    }
    free(derivations->items);
    free(attempts->items);
    return out;
}

static PrimeHeCandidates prime_he_elaborate(Space *space, Arena *a, Atom *term,
                                            bool primitive_kinds,
                                            bool all_schemes,
                                            PrimeResourceLedger *ledger,
                                            PrimeResourcePhase phase);

/* The type is an application of a declared type family: an expression whose
 * head some declaration gives an arrow type returning `Type`. */
static bool prime_he_family_application(Space *space, Arena *a, Atom *type) {
    if (!type || type->kind != ATOM_EXPR || type->expr.len < 2u ||
        type->expr.elems[0]->kind != ATOM_SYMBOL ||
        atom_is_symbol_id(type->expr.elems[0], g_builtin_syms.arrow))
        return false;
    Atom **declared = NULL;
    uint32_t count = space_get_declared_types(space, a, type->expr.elems[0], &declared);
    bool family = false;
    for (uint32_t i = 0u; !family && i < count; i++)
        family = prime_he_arrow_shaped(declared[i]) &&
                 is_symbol_named(declared[i]->expr.elems[declared[i]->expr.len - 1u],
                                 "Type");
    free(declared);
    return family;
}

/* The types whose formation is being judged, so that a type that mentions
 * its own declarations is judged once. */
static _Thread_local Atom **g_prime_he_forming;
static _Thread_local size_t g_prime_he_forming_len;
static _Thread_local size_t g_prime_he_forming_cap;

/* A variable-free type-family application that the fragments refute:
 * `*attempts_out` holds the failed attempts that show it. */
static bool prime_he_formation_refuted(Space *space, Arena *a, Atom *type,
                                       bool all_schemes,
                                       PrimeHeCandidates *refutation_out) {
    if (atom_has_vars(type) || !prime_he_family_application(space, a, type))
        return false;
    for (size_t i = 0u; i < g_prime_he_forming_len; i++)
        if (atom_eq(g_prime_he_forming[i], type)) return false;
    if (g_prime_he_forming_len == g_prime_he_forming_cap) {
        size_t cap = g_prime_he_forming_cap ? g_prime_he_forming_cap * 2u : 8u;
        Atom **items = realloc(g_prime_he_forming, sizeof(Atom *) * cap);
        if (!items) return false;
        g_prime_he_forming = items;
        g_prime_he_forming_cap = cap;
    }
    g_prime_he_forming[g_prime_he_forming_len++] = type;
    PrimeHeCandidates formation = prime_he_elaborate(
        space, a, type, true, all_schemes, NULL, PRIME_RESOURCE_FORMATION);
    g_prime_he_forming_len--;
    if (formation.status != PRIME_HE_REJECTED) return false;
    *refutation_out = formation;
    return true;
}

/* The derivations of a leaf: one per declaration of a symbol, in
 * declaration order, each occurrence of an open declaration renamed apart
 * when it is read as a scheme and rigid otherwise; the intrinsic type of a
 * literal; the unknown type of a variable or of `()`.  With
 * `primitive_kinds`, Prime's primitive types are types of kind `Type` when
 * the space declares nothing for them.  Prime's judgments read an open
 * declaration as a scheme only when `(type-scheme name)` marks it;
 * `get-type`, HE's relation, reads every declaration as one, as the
 * contract does (`all_schemes`). */
static PrimeHeCandidates prime_he_leaf_candidates(Space *space, Arena *a,
                                                  Atom *term,
                                                  bool primitive_kinds,
                                                  bool all_schemes) {
    bool ok = true;
    if (term->kind == ATOM_VAR || term->kind == ATOM_EXPR)
        return prime_he_dynamic();
    if (term->kind == ATOM_GROUNDED) {
        Atom *type = get_grounded_type(a, term);
        if (!type || atom_is_symbol_id(type, g_builtin_syms.undefined_type))
            return prime_he_dynamic();
        if (!prime_he_ground_contract_type(type, &ok))
            return ok ? prime_he_outside(prime_expr2(
                            a, "grounded-type-outside-fragments", type))
                      : prime_he_allocation_failure(a);
        bool first_order = prime_he_first_order(type, &ok);
        if (!ok) return prime_he_allocation_failure(a);
        PrimeHeDerivation *single = arena_alloc(a, sizeof(*single));
        *single = (PrimeHeDerivation){
            .type = type,
            .annotation = prime_he_constant_node(a, type, term, type, 0u),
            .first_order = first_order};
        return (PrimeHeCandidates){.status = PRIME_HE_TYPED,
                                   .derivations = single,
                                   .derivation_len = 1u};
    }
    Atom **declared = NULL;
    uint32_t count = prime_he_declared_types(space, a, term, &declared);
    if (count == 0u) {
        free(declared);
        if (primitive_kinds &&
            prime_he_symbol_in(term, PRIME_HE_PRIMITIVE_KIND_NAMES)) {
            Atom *kind = prime_sym(a, "Type");
            PrimeHeDerivation *single = arena_alloc(a, sizeof(*single));
            *single = (PrimeHeDerivation){
                .type = kind,
                .annotation = prime_he_constant_node(a, kind, term, kind, 0u),
                .first_order = true};
            return (PrimeHeCandidates){.status = PRIME_HE_TYPED,
                                       .derivations = single,
                                       .derivation_len = 1u};
        }
        return prime_he_dynamic();
    }
    PrimeHeDerivation *items = arena_alloc(a, sizeof(*items) * count);
    PrimeHeCandidates result = {.status = PRIME_HE_TYPED,
                                .derivations = items,
                                .derivation_len = count};
    /* A declaration whose type applies a declared type family outside the
     * family's domain declares nothing, as in `he-prime`: its type is not
     * formed, and the failed attempts of its formation answer for it. */
    PrimeHeAttemptList ill_formed = {0};
    size_t kept = 0u;
    bool admitted = false, admitted_known = false;
    for (uint32_t i = 0u; i < count; i++) {
        Atom *type = declared[i];
        PrimeHeCandidates refutation = {0};
        if (prime_he_formation_refuted(space, a, type, all_schemes, &refutation)) {
            for (size_t j = 0u; ok && j < refutation.attempt_len; j++)
                ok = prime_he_push_attempt(&ill_formed, refutation.attempts[j]);
            if (!ok) {
                result = prime_he_allocation_failure(a);
                break;
            }
            continue;
        }
        if (atom_is_symbol_id(type, g_builtin_syms.undefined_type)) {
            result = count == 1u
                ? prime_he_dynamic()
                : prime_he_outside(prime_expr3(a, "gradual-declaration", term, type));
            break;
        }
        if (prime_he_native_binder(type)) {
            result = prime_he_outside(
                prime_expr3(a, "prime-native-binder", term, type));
            break;
        }
        bool dependent = prime_he_telescope(type);
        if (dependent && !prime_he_binder_telescope(type)) {
            result = prime_he_outside(
                prime_expr3(a, "dependent-telescope", term, type));
            break;
        }
        if (!prime_he_contract_type(type, &ok)) {
            result = ok ? prime_he_outside(prime_expr3(
                              a, "declared-type-outside-fragments", term, type))
                        : prime_he_allocation_failure(a);
            break;
        }
        bool subtyped = prime_he_subtype_reaches(space, a, type, &ok);
        if (!ok || subtyped) {
            result = !ok ? prime_he_allocation_failure(a)
                         : prime_he_outside(prime_expr3(
                               a, "subtype-declaration", term, type));
            break;
        }
        bool first_order = prime_he_first_order(type, &ok);
        if (ok && !first_order) first_order = prime_he_operator_scheme(type, &ok);
        if (!ok) {
            result = prime_he_allocation_failure(a);
            break;
        }
        PrimeHeDerivation derivation = {.type = type,
                                        .first_order = first_order && !dependent,
                                        .dependent = dependent};
        if (atom_has_vars(type)) {
            derivation.schematic = true;
            if (!admitted_known) {
                admitted = all_schemes || prime_he_scheme_admitted(space, term);
                admitted_known = true;
            }
            /* A binder's variable is its argument, never an unconstrained
             * variable; the other variables of an open declaration that no
             * `type-scheme` admits stay rigid: the declared type itself,
             * never renamed or solved. */
            bool implicit = !dependent || prime_he_implicit_variables(type, &ok);
            if (!ok) {
                result = prime_he_allocation_failure(a);
                break;
            }
            if (!admitted && implicit) {
                derivation.rigid = true;
            } else {
                derivation.type = prime_he_rename_apart(a, type, &ok);
                if (!ok) {
                    result = prime_he_allocation_failure(a);
                    break;
                }
            }
        }
        derivation.annotation =
            prime_he_constant_node(a, derivation.type, term, type, i);
        items[kept++] = derivation;
    }
    free(declared);
    if (result.status == PRIME_HE_TYPED) {
        result.derivation_len = kept;
        if (ill_formed.len > 0u) {
            result.attempts = arena_alloc(a, sizeof(PrimeHeAttempt) * ill_formed.len);
            memcpy(result.attempts, ill_formed.items,
                   sizeof(PrimeHeAttempt) * ill_formed.len);
            result.attempt_len = ill_formed.len;
            result.reason = ill_formed.items[0].reason;
        }
        if (kept == 0u) result.status = PRIME_HE_REJECTED;
    }
    free(ill_formed.items);
    return result;
}

/* The checks every rule makes before typing an expression: a direct
 * annotation of the expression and the `StateValue` wrapper are other rules
 * of HE.  Returns false with `*outside` set when one applies. */
static bool prime_he_application_admitted(Space *space, Arena *a, Atom *term,
                                          PrimeHeCandidates *outside) {
    Atom **declared = NULL;
    uint32_t count = space_get_declared_types(space, a, term, &declared);
    free(declared);
    if (count != 0u) {
        *outside = prime_he_outside(prime_expr2(a, "expression-annotation", term));
        return false;
    }
    if (is_symbol_named(term->expr.elems[0], "StateValue")) {
        *outside = prime_he_outside(prime_expr2(a, "state-value-wrapper", term));
        return false;
    }
    return true;
}

/* The next choice of one derivation per argument, the first argument
 * varying slowest; false after the last choice. */
static bool prime_he_next_choice(size_t *choice,
                                 const PrimeHeCandidates *arguments,
                                 size_t count) {
    for (size_t i = count; i-- > 0u;) {
        if (++choice[i] < arguments[i].derivation_len) return true;
        choice[i] = 0u;
    }
    return false;
}

/* The derivations of a nonempty expression from those of its parts, in HE's
 * order: the tuples through each non-arrow type of the head, then the
 * applications through each arrow type of the head, each with every choice
 * among the derivations of the arguments.  An application of another arity
 * and one whose argument does not unify with its parameter are failed
 * attempts.  The failed attempts of the parts stay with the expression, so
 * that a refutation answers for every way of typing it. */
static PrimeHeCandidates prime_he_expression_candidates(
    Space *space, Arena *a, Atom *term, const PrimeHeCandidates *parts) {
    PrimeHeCandidates outside;
    if (!prime_he_application_admitted(space, a, term, &outside)) return outside;
    const PrimeHeCandidates *head = &parts[0];
    const PrimeHeCandidates *arguments = parts + 1;
    size_t count = (size_t)term->expr.len - 1u;
    if (head->status == PRIME_HE_DYNAMIC) return prime_he_dynamic();
    if (head->status == PRIME_HE_OUTSIDE) return *head;
    for (size_t i = 0u; i < count; i++) {
        if (arguments[i].status == PRIME_HE_OUTSIDE) return arguments[i];
        if (arguments[i].status == PRIME_HE_DYNAMIC)
            return prime_he_outside(
                prime_expr2(a, "gradual-subterm", term->expr.elems[i + 1u]));
    }
    /* Both rules type every part: a part without a derivation leaves the
     * expression without one. */
    for (size_t i = 0u; i <= count; i++)
        if (parts[i].status == PRIME_HE_REJECTED)
            return (PrimeHeCandidates){.status = PRIME_HE_REJECTED,
                                       .attempts = parts[i].attempts,
                                       .attempt_len = parts[i].attempt_len,
                                       .reason = parts[i].reason};
    for (size_t i = 0u; i <= count; i++)
        for (size_t j = 0u; j < parts[i].derivation_len; j++)
            if (parts[i].derivations[j].rigid)
                return prime_he_outside(prime_expr2(
                    a, "rigid-declaration-variables", term->expr.elems[i]));
    /* A head typed by a bare type variable has both readings, by the
     * instances of the variable. */
    bool values = false;
    for (size_t j = 0u; j < head->derivation_len; j++) {
        Atom *type = head->derivations[j].type;
        if (type->kind == ATOM_VAR)
            return prime_he_outside(
                prime_expr2(a, "variable-typed-head", term->expr.elems[0]));
        if (!prime_he_arrow_shaped(type)) values = true;
    }
    if (values && count == 0u)
        return prime_he_outside(prime_expr2(a, "one-element-expression", term));

    PrimeHeDerivationList derivations = {0};
    PrimeHeAttemptList attempts = {0};
    bool ok = count < SIZE_MAX / sizeof(Atom *) - 2u;
    size_t *choice = ok && count > 0u ? calloc(count, sizeof(size_t)) : NULL;
    if (count > 0u && !choice) ok = false;
    Atom **elements = ok ? arena_alloc(a, sizeof(Atom *) * (count + 1u)) : NULL;
    Atom **types = ok ? arena_alloc(a, sizeof(Atom *) * (count + 1u)) : NULL;
    PrimeHeSubst subst = {0};
    Atom *outside_reason = NULL;

    /* Tuples through each non-arrow type of the head. */
    for (size_t h = 0u; ok && !outside_reason && h < head->derivation_len; h++) {
        const PrimeHeDerivation *function = &head->derivations[h];
        if (prime_he_arrow_shaped(function->type)) continue;
        memset(choice, 0, sizeof(size_t) * count);
        do {
            PrimeHeDerivation tuple = {.tuples = true,
                                       .schematic = function->schematic,
                                       .dependent = function->dependent};
            elements[0] = function->annotation;
            types[0] = function->type;
            for (size_t i = 0u; i < count; i++) {
                const PrimeHeDerivation *argument =
                    &arguments[i].derivations[choice[i]];
                elements[i + 1u] = argument->annotation;
                types[i + 1u] = argument->type;
                tuple.schematic = tuple.schematic || argument->schematic;
                tuple.dependent = tuple.dependent || argument->dependent;
            }
            Atom *tuple_type = atom_expr(a, types, (CettaExprLen)(count + 1u));
            bool subtyped = prime_he_subtype_reaches(space, a, tuple_type, &ok);
            if (!ok) break;
            if (subtyped) {
                outside_reason =
                    prime_expr3(a, "subtype-declaration", term, tuple_type);
                break;
            }
            tuple.type = tuple_type;
            tuple.annotation = prime_he_compound_node(a, "HeTuple", tuple_type,
                                                      elements, count + 1u);
            ok = prime_he_push_derivation(&derivations, tuple);
        } while (ok && prime_he_next_choice(choice, arguments, count));
    }

    /* Applications through each arrow type of the head. */
    for (size_t h = 0u; ok && !outside_reason && h < head->derivation_len; h++) {
        const PrimeHeDerivation *function = &head->derivations[h];
        Atom *arrow = function->type;
        if (!prime_he_arrow_shaped(arrow)) continue;
        size_t parameters = (size_t)arrow->expr.len - 2u;
        if (parameters != count) {
            /* No instance of an arrow has another arity. */
            Atom *items[3] = {prime_sym(a, "HeArity"),
                              atom_int(a, (int64_t)count),
                              function->annotation};
            ok = prime_he_push_attempt(
                &attempts,
                (PrimeHeAttempt){
                    .annotation = atom_expr(a, items, 3),
                    .reason = prime_expr3(a, "arity",
                                          atom_int(a, (int64_t)parameters),
                                          atom_int(a, (int64_t)count)),
                    .refutable = true,
                    .dependent = function->dependent});
            continue;
        }
        if (count > 0u) memset(choice, 0, sizeof(size_t) * count);
        do {
            subst.len = 0u;
            size_t failed = count;
            bool schematic = function->schematic;
            bool tuples = function->tuples;
            bool first_order = function->first_order;
            bool dependent = function->dependent;
            Atom *failed_parameter = NULL;
            elements[0] = function->annotation;
            for (size_t i = 0u; ok && i < count; i++) {
                const PrimeHeDerivation *argument =
                    &arguments[i].derivations[choice[i]];
                elements[i + 1u] = argument->annotation;
                if (failed < count) continue;
                schematic = schematic || argument->schematic;
                tuples = tuples || argument->tuples;
                dependent = dependent || argument->dependent;
                first_order = first_order && argument->first_order &&
                              !prime_he_arrow_shaped(argument->type);
                size_t before = subst.len;
                /* A binder `(: $x A)` names this argument: `$x` is the
                 * argument itself, and the argument has the domain `A`. */
                Atom *parameter = arrow->expr.elems[i + 1u];
                Atom *binder = NULL, *domain = parameter;
                bool bound = !function->dependent ||
                             !prime_he_binder_component(parameter, &binder, &domain) ||
                             prime_he_unify(&subst, binder, term->expr.elems[i + 1u], &ok);
                if (!ok) break;
                if (!bound || !prime_he_unify(&subst, domain, argument->type, &ok)) {
                    if (!ok) break;
                    /* The unifier of the earlier arguments. */
                    subst.len = before;
                    failed = i;
                    failed_parameter = domain;
                }
            }
            if (!ok) break;
            if (failed < count) {
                Atom **items = arena_alloc(a, sizeof(Atom *) * (failed + 3u));
                items[0] = prime_sym(a, "HeMismatch");
                for (size_t i = 0u; i <= failed + 1u; i++)
                    items[i + 1u] = elements[i];
                Atom *raw = atom_expr(a, items, (CettaExprLen)(failed + 3u));
                Atom *parameter = failed_parameter;
                Atom *given = arguments[failed].derivations[choice[failed]].type;
                Atom *attempt = prime_he_subst_resolve(a, &subst, raw, &ok);
                Atom *expected =
                    ok ? prime_he_subst_resolve(a, &subst, parameter, &ok) : NULL;
                Atom *actual =
                    ok ? prime_he_subst_resolve(a, &subst, given, &ok) : NULL;
                if (!ok) break;
                Atom *why[4] = {prime_sym(a, "argument-type"),
                                atom_int(a, (int64_t)failed + 1),
                                expected, actual};
                ok = prime_he_push_attempt(
                    &attempts,
                    (PrimeHeAttempt){.annotation = attempt,
                                     .reason = atom_expr(a, why, 4),
                                     .refutable = true,
                                     .dependent = dependent});
            } else {
                Atom *returned = arrow->expr.elems[arrow->expr.len - 1u];
                Atom *raw = prime_he_compound_node(a, "HeApp", returned,
                                                   elements, count + 1u);
                Atom *annotation = prime_he_subst_resolve(a, &subst, raw, &ok);
                Atom *type = ok ? prime_he_subst_resolve(a, &subst, returned, &ok)
                                : NULL;
                if (!ok) break;
                ok = prime_he_push_derivation(
                    &derivations,
                    (PrimeHeDerivation){.type = type,
                                        .annotation = annotation,
                                        .tuples = tuples,
                                        .schematic = schematic,
                                        .first_order = first_order && !tuples &&
                                                       !dependent,
                                        .dependent = dependent});
            }
        } while (ok && prime_he_next_choice(choice, arguments, count));
    }
    free(subst.items);
    free(choice);
    for (size_t i = 0u; ok && !outside_reason && i <= count; i++)
        for (size_t j = 0u; ok && j < parts[i].attempt_len; j++)
            ok = prime_he_push_attempt(&attempts, parts[i].attempts[j]);
    if (!ok || outside_reason) {
        free(derivations.items);
        free(attempts.items);
        return ok ? prime_he_outside(outside_reason)
                  : prime_he_allocation_failure(a);
    }
    return prime_he_candidates_finish(a, &derivations, &attempts);
}

/* ---- Traversal --------------------------------------------------------- */

/* The derivations of `term`, computed bottom-up with explicit stacks. */
static PrimeHeCandidates prime_he_term_candidates(Space *space, Arena *a,
                                                  Atom *term,
                                                  bool primitive_kinds,
                                                  bool all_schemes,
                                                  uint64_t *visited) {
    PrimeHeFrame *frames = NULL;
    size_t frame_len = 0u, frame_cap = 0u;
    PrimeHeCandidates *results = NULL;
    size_t result_len = 0u, result_cap = 0u;
    PrimeHeCandidates answer = prime_he_allocation_failure(a);
    bool ok = true;

    PRIME_HE_GROW(frames, frame_len, frame_cap, PrimeHeFrame, ok);
    if (ok) frames[frame_len++] = (PrimeHeFrame){.term = term};
    while (ok && frame_len > 0u) {
        PrimeHeFrame *frame = &frames[frame_len - 1u];
        Atom *current = frame->term;
        bool expression = current->kind == ATOM_EXPR && current->expr.len > 0u;
        size_t done = result_len - frame->first_result;
        if (expression && done < (size_t)current->expr.len) {
            Atom *child = current->expr.elems[done];
            PRIME_HE_GROW(frames, frame_len, frame_cap, PrimeHeFrame, ok);
            if (!ok) break;
            frames[frame_len++] =
                (PrimeHeFrame){.term = child, .first_result = result_len};
            continue;
        }
        PrimeHeCandidates result = expression
            ? prime_he_expression_candidates(space, a, current,
                                             results + frame->first_result)
            : prime_he_leaf_candidates(space, a, current, primitive_kinds,
                                       all_schemes);
        if (visited) (*visited)++;
        result_len = expression ? frame->first_result : result_len;
        frame_len--;
        PRIME_HE_GROW(results, result_len, result_cap, PrimeHeCandidates, ok);
        if (!ok) break;
        results[result_len++] = result;
    }
    if (ok && result_len == 1u) answer = results[0];
    free(frames);
    free(results);
    return answer;
}

/* The derivations of a term, counted as observed work without failing it:
 * finite structural typing is never failed by a producer budget. */
static PrimeHeCandidates prime_he_elaborate(Space *space, Arena *a, Atom *term,
                                            bool primitive_kinds,
                                            bool all_schemes,
                                            PrimeResourceLedger *ledger,
                                            PrimeResourcePhase phase) {
    uint64_t before = prime_resource_phase_begin(ledger);
    uint64_t visited = 0u;
    PrimeHeCandidates candidates = prime_he_term_candidates(
        space, a, term, primitive_kinds, all_schemes, &visited);
    if (ledger && ledger->typing.steps_limited)
        ledger->typing.work_steps_observed =
            prime_u64_add_sat(ledger->typing.work_steps_observed, visited);
    prime_resource_phase_end(ledger, phase, before);
    return candidates;
}

static Atom *prime_he_fragment_name(Arena *a, const PrimeHeCandidates *c) {
    bool schematic = false, tuples = false, dependent = false;
    for (size_t i = 0u; i < c->derivation_len; i++) {
        schematic = schematic || c->derivations[i].schematic;
        tuples = tuples || c->derivations[i].tuples;
        dependent = dependent || c->derivations[i].dependent;
    }
    const char *name = dependent ? "dependent-binders"
                     : c->derivation_len > 1u ? "overloads"
                     : schematic ? "first-order-schemes"
                     : tuples ? "tuples" : "static-arrows";
    return prime_expr2(a, "Fragment", prime_sym(a, name));
}

/* The reason an Undetermined answer gives for terms without a checked
 * derivation. */
static Atom *prime_he_undetermined_reason(Arena *a, const PrimeHeCandidates *c) {
    if (c->status == PRIME_HE_DYNAMIC) return prime_expr1(a, "dynamic-type-only");
    if (c->status == PRIME_HE_REJECTED)
        return prime_expr2(a, "outside-kernel-fragments",
                           prime_expr2(a, "grounded-fragment-rejects",
                                       c->reason ? c->reason
                                                 : prime_expr1(a, "failed-attempts")));
    return prime_expr2(a, "outside-kernel-fragments", c->reason);
}

/* ---- Translation into the hosted HOL Light checker --------------------- */

/* HE names live in their own namespace of the checker, apart from HOL
 * Light's own type operators and constants (`bool`, `fun`, `prod`, `=`, `T`,
 * `,` ...) and from the classification theory of meta-types: `he:` for the
 * base types and the declarations, `he-arrow:` and `he-apply:` for the
 * arrows and their application constants. */
#define PRIME_HOL_NAME_PREFIX "he:"
#define PRIME_HOL_META_PREFIX "he-meta:"

typedef struct {
    Atom **items;
    size_t len;
    size_t cap;
} PrimeHolAtoms;

static bool prime_hol_atoms_push(PrimeHolAtoms *list, Atom *atom) {
    bool ok = true;
    PRIME_HE_GROW(list->items, list->len, list->cap, Atom *, ok);
    if (!ok) return false;
    list->items[list->len++] = atom;
    return true;
}

typedef struct {
    Arena *a;
    /* Type operator names (string atoms) and their arities (integer
     * atoms), in pairs. */
    PrimeHolAtoms operators;
    /* Constant names (string atoms) and their declared checker types, in
     * pairs. */
    PrimeHolAtoms constants;
    bool products;
    bool ok;
    Atom *reason;
} PrimeHolTranslation;

static void prime_hol_translation_free(PrimeHolTranslation *tr) {
    free(tr->operators.items);
    free(tr->constants.items);
}

static void prime_hol_fail(PrimeHolTranslation *tr, Atom *reason) {
    if (tr->ok) {
        tr->ok = false;
        tr->reason = reason;
    }
}

static Atom *prime_hol_name(Arena *a, const char *prefix, Atom *atom) {
    char *printed = atom_to_parseable_string(a, atom);
    if (!printed) return NULL;
    size_t size = strlen(prefix) + strlen(printed) + 1u;
    char *name = arena_alloc(a, size);
    snprintf(name, size, "%s%s", prefix, printed);
    return atom_string(a, name);
}

/* `prefix` followed by a number. */
static Atom *prime_hol_numbered_name(Arena *a, const char *prefix,
                                     size_t number) {
    size_t size = strlen(prefix) + 24u;
    char *name = arena_alloc(a, size);
    snprintf(name, size, "%s%zu", prefix, number);
    return atom_string(a, name);
}

/* The constant of the `occurrence`-th declaration of `atom`:
 * `he:occurrence:atom`.  The number ends at the first colon, so the name
 * determines the declaration. */
static Atom *prime_hol_declaration_name(Arena *a, Atom *atom,
                                        uint64_t occurrence) {
    char *printed = atom_to_parseable_string(a, atom);
    if (!printed) return NULL;
    size_t size = strlen(PRIME_HOL_NAME_PREFIX) + 24u + strlen(printed) + 1u;
    char *name = arena_alloc(a, size);
    snprintf(name, size, "%s%llu:%s", PRIME_HOL_NAME_PREFIX,
             (unsigned long long)occurrence, printed);
    return atom_string(a, name);
}

static Atom *prime_hol_nil(Arena *a) { return prime_sym(a, "HOL:Nil"); }

static Atom *prime_hol_cons(Arena *a, Atom *first, Atom *rest) {
    return atom_expr3(a, prime_sym(a, "HOL:Cons"), first, rest);
}

static Atom *prime_hol_list(Arena *a, Atom *const *items, size_t count) {
    Atom *list = prime_hol_nil(a);
    for (size_t i = count; i-- > 0u;) list = prime_hol_cons(a, items[i], list);
    return list;
}

static Atom *prime_hol_tyapp(Arena *a, const char *name, Atom *arguments) {
    return atom_expr3(a, prime_sym(a, "Tyapp"), atom_string(a, name), arguments);
}

static Atom *prime_hol_named_tyapp(Arena *a, Atom *name, Atom *arguments) {
    return atom_expr3(a, prime_sym(a, "Tyapp"), name, arguments);
}

static Atom *prime_hol_binary_type(Arena *a, const char *op, Atom *left,
                                   Atom *right) {
    return prime_hol_tyapp(
        a, op, prime_hol_cons(a, left, prime_hol_cons(a, right, prime_hol_nil(a))));
}

static Atom *prime_hol_fun(Arena *a, Atom *domain, Atom *codomain) {
    return prime_hol_binary_type(a, "fun", domain, codomain);
}

static Atom *prime_hol_unit_type(Arena *a) {
    return prime_hol_tyapp(a, "1", prime_hol_nil(a));
}

static Atom *prime_hol_bool_type(Arena *a) {
    return prime_hol_tyapp(a, "bool", prime_hol_nil(a));
}

static Atom *prime_hol_tyvar(Arena *a, const char *name) {
    return atom_expr2(a, prime_sym(a, "Tyvar"), atom_string(a, name));
}

/* Record the type operator `name` of arity `arity`, once. */
static void prime_hol_declare_operator(PrimeHolTranslation *tr, Atom *name,
                                       size_t arity) {
    for (size_t i = 0u; i + 1u < tr->operators.len; i += 2u)
        if (atom_eq(tr->operators.items[i], name)) return;
    if (!prime_hol_atoms_push(&tr->operators, name) ||
        !prime_hol_atoms_push(&tr->operators, atom_int(tr->a, (int64_t)arity)))
        prime_hol_fail(tr, prime_expr1(tr->a, "checker-translation-allocation"));
}

/* The arrow operator `he-arrow:n` of the arrows with n parameters, of arity
 * n + 1, applied to the translated components. */
static Atom *prime_hol_arrow_type(PrimeHolTranslation *tr, Atom *const *parts,
                                  size_t count) {
    Atom *name = prime_hol_numbered_name(tr->a, "he-arrow:", count - 1u);
    prime_hol_declare_operator(tr, name, count);
    return prime_hol_named_tyapp(tr->a, name, prime_hol_list(tr->a, parts, count));
}

/* The checker type of a leaf type, recording its operator. */
static Atom *prime_hol_leaf_type(PrimeHolTranslation *tr, Atom *leaf) {
    Arena *a = tr->a;
    if (leaf->kind == ATOM_VAR) {
        char name[48];
        snprintf(name, sizeof(name), "v%llu", (unsigned long long)leaf->var_id);
        return prime_hol_tyvar(a, name);
    }
    /* Whether a whole type is a contract type is decided before
     * translation; only the unknown type never translates. */
    if (atom_is_symbol_id(leaf, g_builtin_syms.undefined_type)) {
        prime_hol_fail(tr, prime_expr2(a, "unknown-type-in-translation", leaf));
        return NULL;
    }
    Atom *name = prime_hol_name(a, PRIME_HOL_NAME_PREFIX, leaf);
    if (!name) {
        prime_hol_fail(tr, prime_expr1(a, "checker-translation-allocation"));
        return NULL;
    }
    prime_hol_declare_operator(tr, name, 0u);
    return prime_hol_named_tyapp(a, name, prime_hol_nil(a));
}

/* The checker type of an HE type: a leaf a nullary operator, an arrow
 * `(-> C1 ... Ck)` the operator `he-arrow:(k-1)` applied to its components,
 * a compound non-arrow type the right-nested product ending in the unit type
 * (`()` is the unit type itself).  Bottom-up with explicit stacks; arrow
 * heads are not visited. */
static Atom *prime_hol_type(PrimeHolTranslation *tr, Atom *root) {
    Arena *a = tr->a;
    PrimeHeFrame *frames = NULL;
    size_t frame_len = 0u, frame_cap = 0u;
    Atom **results = NULL;
    size_t result_len = 0u, result_cap = 0u;
    Atom *answer = NULL;
    bool good = true;
    PRIME_HE_GROW(frames, frame_len, frame_cap, PrimeHeFrame, good);
    if (good) frames[frame_len++] = (PrimeHeFrame){.term = root};
    while (good && tr->ok && frame_len > 0u) {
        PrimeHeFrame *frame = &frames[frame_len - 1u];
        Atom *current = frame->term;
        bool arrow = prime_he_arrow_shaped(current);
        size_t start = arrow ? 1u : 0u;
        size_t done = result_len - frame->first_result;
        if (current->kind == ATOM_EXPR &&
            start + done < (size_t)current->expr.len) {
            Atom *child = current->expr.elems[start + done];
            PRIME_HE_GROW(frames, frame_len, frame_cap, PrimeHeFrame, good);
            if (!good) break;
            frames[frame_len++] =
                (PrimeHeFrame){.term = child, .first_result = result_len};
            continue;
        }
        Atom *result = NULL;
        if (current->kind != ATOM_EXPR) {
            result = prime_hol_leaf_type(tr, current);
        } else if (arrow) {
            if (current->expr.len < 2u)
                prime_hol_fail(tr, prime_expr2(a, "arrow-without-return", current));
            else
                result = prime_hol_arrow_type(tr, results + frame->first_result,
                                              (size_t)current->expr.len - 1u);
        } else {
            Atom **parts = results + frame->first_result;
            size_t count = (size_t)current->expr.len;
            result = prime_hol_unit_type(a);
            for (size_t i = count; i-- > 0u;)
                result = prime_hol_binary_type(a, "prod", parts[i], result);
            tr->products = true;
        }
        if (current->kind == ATOM_EXPR) result_len = frame->first_result;
        frame_len--;
        if (!tr->ok) break;
        PRIME_HE_GROW(results, result_len, result_cap, Atom *, good);
        if (!good) break;
        results[result_len++] = result;
    }
    if (good && tr->ok && result_len == 1u) answer = results[0];
    if (!good) prime_hol_fail(tr, prime_expr1(a, "checker-translation-allocation"));
    free(frames);
    free(results);
    return answer;
}

/* Record the constant `name` with its checker type, once. */
static void prime_hol_declare_checker_constant(PrimeHolTranslation *tr,
                                               Atom *name, Atom *type) {
    for (size_t i = 0u; i + 1u < tr->constants.len; i += 2u)
        if (atom_eq(tr->constants.items[i], name)) return;
    if (!prime_hol_atoms_push(&tr->constants, name) ||
        !prime_hol_atoms_push(&tr->constants, type))
        prime_hol_fail(tr, prime_expr1(tr->a, "checker-translation-allocation"));
}

/* Record the constant `name` of a declaration with its declared type,
 * once. */
static void prime_hol_declare_constant(PrimeHolTranslation *tr, Atom *name,
                                       Atom *declared) {
    for (size_t i = 0u; i + 1u < tr->constants.len; i += 2u)
        if (atom_eq(tr->constants.items[i], name)) return;
    Atom *scheme = prime_hol_type(tr, declared);
    if (tr->ok) prime_hol_declare_checker_constant(tr, name, scheme);
}

static Atom *prime_hol_const(Arena *a, Atom *name, Atom *type) {
    return atom_expr3(a, prime_sym(a, "Const"), name, type);
}

static Atom *prime_hol_comb(Arena *a, Atom *function, Atom *argument) {
    return atom_expr3(a, prime_sym(a, "Comb"), function, argument);
}

/* `P1 -> ... -> Pn -> R`. */
static Atom *prime_hol_function_chain(Arena *a, Atom *const *parameters,
                                      size_t count, Atom *result) {
    Atom *type = result;
    for (size_t i = count; i-- > 0u;) type = prime_hol_fun(a, parameters[i], type);
    return type;
}

/* The application constant `he-apply:n` of the arrows with n parameters, of
 * declared type `he-arrow:n A A' ... B -> A -> A' -> ... -> B`, read at the
 * instance whose parameters are `parameters` and whose return is `result`
 * (checker types). */
static Atom *prime_hol_apply_constant(PrimeHolTranslation *tr,
                                      Atom *const *parameters, size_t count,
                                      Atom *result) {
    Arena *a = tr->a;
    Atom *name = prime_hol_numbered_name(a, "he-apply:", count);
    Atom **variables = arena_alloc(a, sizeof(Atom *) * (count + 1u));
    for (size_t i = 0u; i < count; i++) {
        char *spelling = arena_alloc(a, i + 2u);
        spelling[0] = 'A';
        memset(spelling + 1, '\'', i);
        spelling[i + 1u] = '\0';
        variables[i] = prime_hol_tyvar(a, spelling);
    }
    variables[count] = prime_hol_tyvar(a, "B");
    Atom *schema = prime_hol_fun(
        a, prime_hol_arrow_type(tr, variables, count + 1u),
        prime_hol_function_chain(a, variables, count, variables[count]));
    prime_hol_declare_checker_constant(tr, name, schema);
    Atom **components = arena_alloc(a, sizeof(Atom *) * (count + 1u));
    for (size_t i = 0u; i < count; i++) components[i] = parameters[i];
    components[count] = result;
    Atom *instance = prime_hol_fun(
        a, prime_hol_arrow_type(tr, components, count + 1u),
        prime_hol_function_chain(a, parameters, count, result));
    return prime_hol_const(a, name, instance);
}

/* The application constant at the instance of an elaborated head whose
 * type is the arrow `(-> P1 ... Pn R)`, with `count` parameters: the head's
 * parameters where it has them and fresh type variables beyond. */
static Atom *prime_hol_apply_for_head(PrimeHolTranslation *tr, Atom *head,
                                      size_t count) {
    Arena *a = tr->a;
    Atom *arrow = head->kind == ATOM_EXPR && head->expr.len >= 2u
        ? head->expr.elems[1] : NULL;
    if (!arrow || !prime_he_arrow_shaped(arrow) || arrow->expr.len < 2u) {
        prime_hol_fail(tr, prime_expr2(a, "malformed-elaboration", head));
        return NULL;
    }
    size_t parameters = (size_t)arrow->expr.len - 2u;
    Atom **types = arena_alloc(a, sizeof(Atom *) * (count + 1u));
    for (size_t i = 0u; tr->ok && i < count; i++) {
        if (i < parameters) {
            types[i] = prime_hol_type(tr, arrow->expr.elems[i + 1u]);
        } else {
            Atom *name = prime_hol_numbered_name(a, "x", i);
            types[i] = prime_hol_tyvar(a, name->ground.sval);
        }
    }
    Atom *result = tr->ok
        ? prime_hol_type(tr, arrow->expr.elems[arrow->expr.len - 1u]) : NULL;
    if (!tr->ok) return NULL;
    return prime_hol_apply_constant(tr, types, count, result);
}

/* The tuple `x0, (x1, ... (xn, one))` of the translated elements at their
 * translated types. */
static Atom *prime_hol_tuple(PrimeHolTranslation *tr, Atom **terms,
                             Atom **types, size_t count) {
    Arena *a = tr->a;
    Atom *value = prime_hol_const(a, atom_string(a, "one"), prime_hol_unit_type(a));
    Atom *value_type = prime_hol_unit_type(a);
    for (size_t i = count; i-- > 0u;) {
        Atom *pair_type = prime_hol_binary_type(a, "prod", types[i], value_type);
        Atom *pairing = prime_hol_const(
            a, atom_string(a, ","),
            prime_hol_fun(a, types[i], prime_hol_fun(a, value_type, pair_type)));
        value = prime_hol_comb(a, prime_hol_comb(a, pairing, terms[i]), value);
        value_type = pair_type;
    }
    tr->products = true;
    return value;
}

/* The checker's source term of an elaborated term, bottom-up with explicit
 * stacks.  Each declaration occurrence is its own constant, declared with
 * its declared type and read at its instance; an application is the
 * application constant of the head's arity applied to the head and the
 * arguments. */
static Atom *prime_hol_term(PrimeHolTranslation *tr, Atom *root) {
    Arena *a = tr->a;
    PrimeHeFrame *frames = NULL;
    size_t frame_len = 0u, frame_cap = 0u;
    Atom **results = NULL;
    size_t result_len = 0u, result_cap = 0u;
    Atom *answer = NULL;
    bool good = true;
    PRIME_HE_GROW(frames, frame_len, frame_cap, PrimeHeFrame, good);
    if (good) frames[frame_len++] = (PrimeHeFrame){.term = root};
    while (good && tr->ok && frame_len > 0u) {
        PrimeHeFrame *frame = &frames[frame_len - 1u];
        Atom *node = frame->term;
        bool leaf = node->kind == ATOM_EXPR && node->expr.len == 5u &&
                    is_symbol_named(node->expr.elems[0], "HeConst") &&
                    node->expr.elems[4]->kind == ATOM_GROUNDED &&
                    node->expr.elems[4]->ground.gkind == GV_INT &&
                    node->expr.elems[4]->ground.ival >= 0;
        bool compound = node->kind == ATOM_EXPR && node->expr.len >= 3u &&
                        (is_symbol_named(node->expr.elems[0], "HeApp") ||
                         is_symbol_named(node->expr.elems[0], "HeTuple"));
        if (!leaf && !compound) {
            prime_hol_fail(tr, prime_expr2(a, "malformed-elaboration", node));
            break;
        }
        size_t done = result_len - frame->first_result;
        if (compound && 2u + done < (size_t)node->expr.len) {
            Atom *child = node->expr.elems[2u + done];
            PRIME_HE_GROW(frames, frame_len, frame_cap, PrimeHeFrame, good);
            if (!good) break;
            frames[frame_len++] =
                (PrimeHeFrame){.term = child, .first_result = result_len};
            continue;
        }
        Atom *result = NULL;
        if (leaf) {
            Atom *name = prime_hol_declaration_name(
                a, node->expr.elems[2], (uint64_t)node->expr.elems[4]->ground.ival);
            Atom *instance = name ? prime_hol_type(tr, node->expr.elems[1]) : NULL;
            if (!name)
                prime_hol_fail(tr, prime_expr1(a, "checker-translation-allocation"));
            if (tr->ok) prime_hol_declare_constant(tr, name, node->expr.elems[3]);
            if (tr->ok) result = prime_hol_const(a, name, instance);
        } else if (is_symbol_named(node->expr.elems[0], "HeApp")) {
            Atom **parts = results + frame->first_result;
            size_t count = (size_t)node->expr.len - 3u;
            result = prime_hol_apply_for_head(tr, node->expr.elems[2], count);
            if (tr->ok) result = prime_hol_comb(a, result, parts[0]);
            for (size_t i = 1u; tr->ok && i <= count; i++)
                result = prime_hol_comb(a, result, parts[i]);
        } else {
            Atom **parts = results + frame->first_result;
            size_t count = (size_t)node->expr.len - 2u;
            Atom **types = arena_alloc(a, sizeof(Atom *) * count);
            for (size_t i = 0u; tr->ok && i < count; i++) {
                Atom *element = node->expr.elems[2u + i];
                types[i] = prime_hol_type(tr, element->expr.elems[1]);
            }
            if (tr->ok) result = prime_hol_tuple(tr, parts, types, count);
        }
        if (compound) result_len = frame->first_result;
        frame_len--;
        if (!tr->ok) break;
        PRIME_HE_GROW(results, result_len, result_cap, Atom *, good);
        if (!good) break;
        results[result_len++] = result;
    }
    if (good && tr->ok && result_len == 1u) answer = results[0];
    if (!good) prime_hol_fail(tr, prime_expr1(a, "checker-translation-allocation"));
    free(frames);
    free(results);
    return answer;
}

/* The parts of a failed attempt that the checker must read, and the step it
 * must refuse.  `(HeArity count head)`: the application constant of arity
 * `count` and the head, and its application to the head, refused because
 * the head's arrow operator has another arity.  `(HeMismatch head arg ...)`:
 * the application of the head's application constant to the head and the
 * earlier arguments, and the failing argument, and the application of the
 * one to the other, refused because the argument's type is not the
 * parameter's.  Returns the number of parts. */
static size_t prime_hol_attempt_terms(PrimeHolTranslation *tr, Atom *attempt,
                                      Atom **parts, Atom **step) {
    Arena *a = tr->a;
    *step = NULL;
    if (attempt->kind == ATOM_EXPR && attempt->expr.len == 3u &&
        is_symbol_named(attempt->expr.elems[0], "HeArity") &&
        attempt->expr.elems[1]->kind == ATOM_GROUNDED &&
        attempt->expr.elems[1]->ground.gkind == GV_INT &&
        attempt->expr.elems[1]->ground.ival >= 0) {
        size_t count = (size_t)attempt->expr.elems[1]->ground.ival;
        Atom *head = attempt->expr.elems[2];
        Atom *apply = prime_hol_apply_for_head(tr, head, count);
        Atom *function = tr->ok ? prime_hol_term(tr, head) : NULL;
        if (!tr->ok) return 0u;
        parts[0] = apply;
        parts[1] = function;
        *step = prime_hol_comb(a, apply, function);
        return 2u;
    }
    if (attempt->kind == ATOM_EXPR && attempt->expr.len >= 3u &&
        is_symbol_named(attempt->expr.elems[0], "HeMismatch")) {
        Atom *head = attempt->expr.elems[1];
        size_t given = (size_t)attempt->expr.len - 2u;
        Atom *arrow = head->kind == ATOM_EXPR && head->expr.len >= 2u
            ? head->expr.elems[1] : NULL;
        if (!arrow || !prime_he_arrow_shaped(arrow) ||
            (size_t)arrow->expr.len - 2u < given) {
            prime_hol_fail(tr, prime_expr2(a, "malformed-elaboration", attempt));
            return 0u;
        }
        Atom *prefix = prime_hol_apply_for_head(tr, head, (size_t)arrow->expr.len - 2u);
        Atom *function = tr->ok ? prime_hol_term(tr, head) : NULL;
        if (tr->ok) prefix = prime_hol_comb(a, prefix, function);
        for (size_t i = 0u; tr->ok && i + 1u < given; i++) {
            Atom *argument = prime_hol_term(tr, attempt->expr.elems[2u + i]);
            if (tr->ok) prefix = prime_hol_comb(a, prefix, argument);
        }
        Atom *failing = tr->ok
            ? prime_hol_term(tr, attempt->expr.elems[1u + given]) : NULL;
        if (!tr->ok) return 0u;
        parts[0] = prefix;
        parts[1] = failing;
        *step = prime_hol_comb(a, prefix, failing);
        return 2u;
    }
    prime_hol_fail(tr, prime_expr2(a, "malformed-elaboration", attempt));
    return 0u;
}

static Atom *prime_hol_step(Arena *a, const char *kind, Atom *name,
                            Atom *request) {
    const char *text = name && name->kind == ATOM_GROUNDED &&
                       name->ground.gkind == GV_STRING
        ? name->ground.sval : "";
    size_t size = strlen(kind) + strlen(text) + 2u;
    char *key = arena_alloc(a, size);
    snprintf(key, size, "%s:%s", kind, text);
    return atom_expr3(a, prime_sym(a, "HOL:Step"), atom_string(a, key), request);
}

/* The declaration commands of a translation: every type operator at its
 * arity, the pair and unit signature when a product occurs, then every
 * constant. */
static Atom *prime_hol_commands(PrimeHolTranslation *tr, Atom *tail) {
    Arena *a = tr->a;
    PrimeHolAtoms steps = {0};
    bool ok = true;
    for (size_t i = 0u; ok && i + 1u < tr->operators.len; i += 2u)
        ok = prime_hol_atoms_push(&steps, prime_hol_step(
            a, "type", tr->operators.items[i],
            atom_expr3(a, prime_sym(a, "DECLARE_TYPE"), tr->operators.items[i],
                       tr->operators.items[i + 1u])));
    if (ok && tr->products) {
        Atom *alpha = prime_hol_tyvar(a, "A");
        Atom *beta = prime_hol_tyvar(a, "B");
        Atom *pair = prime_hol_binary_type(a, "prod", alpha, beta);
        struct { const char *name; Atom *type; } signature[] = {
            {",", prime_hol_fun(a, alpha, prime_hol_fun(a, beta, pair))},
            {"FST", prime_hol_fun(a, pair, alpha)},
            {"SND", prime_hol_fun(a, pair, beta)},
            {"one", prime_hol_unit_type(a)},
        };
        ok = prime_hol_atoms_push(&steps, prime_hol_step(
                 a, "type", atom_string(a, "prod"),
                 atom_expr3(a, prime_sym(a, "DECLARE_TYPE"), atom_string(a, "prod"),
                            atom_int(a, 2)))) &&
             prime_hol_atoms_push(&steps, prime_hol_step(
                 a, "type", atom_string(a, "1"),
                 atom_expr3(a, prime_sym(a, "DECLARE_TYPE"), atom_string(a, "1"),
                            atom_int(a, 0))));
        for (size_t i = 0u; ok && i < sizeof(signature) / sizeof(signature[0]); i++)
            ok = prime_hol_atoms_push(&steps, prime_hol_step(
                a, "constant", atom_string(a, signature[i].name),
                atom_expr3(a, prime_sym(a, "DECLARE_CONSTANT"),
                           atom_string(a, signature[i].name), signature[i].type)));
    }
    for (size_t i = 0u; ok && i + 1u < tr->constants.len; i += 2u)
        ok = prime_hol_atoms_push(&steps, prime_hol_step(
            a, "constant", tr->constants.items[i],
            atom_expr3(a, prime_sym(a, "DECLARE_CONSTANT"), tr->constants.items[i],
                       tr->constants.items[i + 1u])));
    Atom *list = tail;
    for (size_t i = steps.len; ok && i-- > 0u;)
        list = prime_hol_cons(a, steps.items[i], list);
    free(steps.items);
    if (!ok) {
        prime_hol_fail(tr, prime_expr1(a, "checker-translation-allocation"));
        return NULL;
    }
    return list;
}

/* ---- Asking the checker ------------------------------------------------ */

typedef enum {
    PRIME_HOL_ACCEPTED = 0,
    PRIME_HOL_REFUSED,
    PRIME_HOL_UNAVAILABLE
} PrimeHolVerdict;

/* The checker's `True`: a grounded boolean, or the symbol. */
static bool prime_hol_true(Atom *answer) {
    if (!answer) return false;
    if (answer->kind == ATOM_GROUNDED && answer->ground.gkind == GV_BOOL)
        return answer->ground.bval;
    return is_symbol_named(answer, "True");
}

/* `(Some value)` from the checker. */
static bool prime_hol_some(Atom *answer, Atom **value_out) {
    if (answer && answer->kind == ATOM_EXPR && answer->expr.len == 2u &&
        is_symbol_named(answer->expr.elems[0], "Some")) {
        *value_out = answer->expr.elems[1];
        return true;
    }
    return false;
}

static PrimeHolVerdict prime_hol_ask(Arena *a, Atom *request, Atom **answer_out,
                                     Atom **reason_out) {
    Atom *reason = NULL;
    if (!eval_prime_hol_guest_evaluate(a, request, answer_out, &reason)) {
        *reason_out = prime_expr2(a, "hol-checker-unavailable",
                                  reason ? reason : prime_expr1(a, "no-answer"));
        return PRIME_HOL_UNAVAILABLE;
    }
    return PRIME_HOL_ACCEPTED;
}

/* Run the declaration commands under the commitments: the checker state,
 * or UNAVAILABLE when it refuses them, which says nothing about the
 * judgment (the translation, not the judgment, failed). */
static PrimeHolVerdict prime_hol_signature(Arena *a, Atom *commitments,
                                           Atom *commands, Atom **state_out,
                                           Atom **reason_out) {
    Atom *answer = NULL;
    PrimeHolVerdict verdict = prime_hol_ask(
        a, atom_expr3(a, prime_sym(a, "hol-light:run"), commitments, commands),
        &answer, reason_out);
    if (verdict == PRIME_HOL_ACCEPTED && !prime_hol_some(answer, state_out)) {
        verdict = PRIME_HOL_UNAVAILABLE;
        *reason_out = prime_expr2(a, "hol-checker-unavailable",
                                  prime_expr1(a, "declarations-refused"));
    }
    return verdict;
}

/* Read a term in a checker state: ACCEPTED with its value, or REFUSED. */
static PrimeHolVerdict prime_hol_read(Arena *a, Atom *state, Atom *term,
                                      Atom **value_out, Atom **reason_out) {
    Atom *answer = NULL;
    PrimeHolVerdict verdict = prime_hol_ask(
        a, atom_expr3(a, prime_sym(a, "hol-light:read"), state, term),
        &answer, reason_out);
    if (verdict == PRIME_HOL_ACCEPTED && !prime_hol_some(answer, value_out)) {
        verdict = PRIME_HOL_REFUSED;
        *reason_out = prime_expr2(a, "hol-checker-refuses", prime_expr1(a, "term"));
    }
    return verdict;
}

/* Run the declaration commands under the commitments; then, when `fact` is
 * given, run that one command after them, or, when `term` is given, read it
 * and compare its computed type with `type`, all in the checker.  Refusing
 * the declarations says nothing about the judgment; refusing the fact, the
 * term or its type is the checker's verdict on the judgment. */
static PrimeHolVerdict prime_hol_decide(Arena *a, Atom *commitments,
                                        Atom *commands, Atom *fact, Atom *term,
                                        Atom *type, Atom **reason_out) {
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_PRIME_HOL_GUEST_REQUEST);
    Atom *state = NULL;
    PrimeHolVerdict verdict =
        prime_hol_signature(a, commitments, commands, &state, reason_out);
    if (verdict == PRIME_HOL_ACCEPTED && fact) {
        Atom *answer = NULL;
        verdict = prime_hol_ask(
            a, atom_expr3(a, prime_sym(a, "hol-light:run-from"), state,
                          prime_hol_cons(a, fact, prime_hol_nil(a))),
            &answer, reason_out);
        Atom *after = NULL;
        if (verdict == PRIME_HOL_ACCEPTED && !prime_hol_some(answer, &after)) {
            verdict = PRIME_HOL_REFUSED;
            *reason_out = prime_expr2(a, "hol-checker-refuses",
                                      prime_expr1(a, "fact"));
        }
    }
    Atom *value = NULL;
    if (verdict == PRIME_HOL_ACCEPTED && term)
        verdict = prime_hol_read(a, state, term, &value, reason_out);
    if (verdict == PRIME_HOL_ACCEPTED && term && type) {
        Atom *answer = NULL;
        Atom *computed = atom_expr2(a, prime_sym(a, "hol-light:term-type"), value);
        verdict = prime_hol_ask(
            a, atom_expr3(a, prime_sym(a, "=="), computed,
                          atom_expr2(a, prime_sym(a, "Some"), type)),
            &answer, reason_out);
        if (verdict == PRIME_HOL_ACCEPTED && !prime_hol_true(answer)) {
            verdict = PRIME_HOL_REFUSED;
            *reason_out = prime_expr2(a, "hol-checker-refuses",
                                      prime_expr1(a, "type"));
        }
    }
    cetta_runtime_stats_inc(
        verdict == PRIME_HOL_ACCEPTED ? CETTA_RUNTIME_COUNTER_PRIME_HOL_GUEST_ACCEPTED
        : verdict == PRIME_HOL_REFUSED ? CETTA_RUNTIME_COUNTER_PRIME_HOL_GUEST_REFUSED
        : CETTA_RUNTIME_COUNTER_PRIME_HOL_GUEST_UNAVAILABLE);
    return verdict;
}

/* The checker's verdict that the elaborated term has the HE type `type`. */
static PrimeHolVerdict prime_hol_check_typing(Arena *a, Atom *annotation,
                                              Atom *type, Atom **reason_out) {
    PrimeHolTranslation tr = {.a = a, .ok = true};
    Atom *term = prime_hol_term(&tr, annotation);
    Atom *expected = tr.ok ? prime_hol_type(&tr, type) : NULL;
    Atom *commands = tr.ok ? prime_hol_commands(&tr, prime_hol_nil(a)) : NULL;
    PrimeHolVerdict verdict = PRIME_HOL_UNAVAILABLE;
    if (tr.ok && term && expected && commands) {
        verdict = prime_hol_decide(a, prime_hol_nil(a), commands, NULL, term,
                                   expected, reason_out);
    } else {
        *reason_out = prime_expr2(a, "hol-translation",
                                  tr.reason ? tr.reason
                                            : prime_expr1(a, "incomplete"));
    }
    prime_hol_translation_free(&tr);
    return verdict;
}

/* The checker's verdict on a failed attempt: REFUSED when it reads each part
 * and refuses the step, ACCEPTED when it reads the step, which the
 * elaboration found ill-typed, and UNAVAILABLE when it refuses a part or
 * cannot be asked. */
static PrimeHolVerdict prime_hol_check_attempt(Arena *a, Atom *attempt,
                                               Atom **reason_out) {
    PrimeHolTranslation tr = {.a = a, .ok = true};
    Atom *parts[2] = {NULL, NULL};
    Atom *step = NULL;
    size_t count = prime_hol_attempt_terms(&tr, attempt, parts, &step);
    Atom *commands = tr.ok ? prime_hol_commands(&tr, prime_hol_nil(a)) : NULL;
    PrimeHolVerdict verdict = PRIME_HOL_UNAVAILABLE;
    if (!tr.ok || !commands || count == 0u || !step) {
        *reason_out = prime_expr2(a, "hol-translation",
                                  tr.reason ? tr.reason
                                            : prime_expr1(a, "incomplete"));
        prime_hol_translation_free(&tr);
        return verdict;
    }
    verdict = PRIME_HOL_ACCEPTED;
    for (size_t i = 0u; verdict == PRIME_HOL_ACCEPTED && i < count; i++) {
        verdict = prime_hol_decide(a, prime_hol_nil(a), commands, NULL, parts[i],
                                   NULL, reason_out);
        if (verdict == PRIME_HOL_REFUSED) {
            verdict = PRIME_HOL_UNAVAILABLE;
            *reason_out = prime_expr2(a, "hol-checker-refuses-part",
                                      atom_int(a, (int64_t)i));
        }
    }
    if (verdict == PRIME_HOL_ACCEPTED) {
        verdict = prime_hol_decide(a, prime_hol_nil(a), commands, NULL, step,
                                   NULL, reason_out);
        if (verdict == PRIME_HOL_REFUSED)
            *reason_out = prime_expr2(a, "hol-checker-refuses",
                                      prime_expr1(a, "application-step"));
    }
    prime_hol_translation_free(&tr);
    return verdict;
}

/* ---- Meta-types: the classification theory of atoms -------------------- */

static const char *prime_he_meta_class(Atom *term) {
    switch (term->kind) {
    case ATOM_SYMBOL: return "Symbol";
    case ATOM_VAR: return "Variable";
    case ATOM_GROUNDED: return "Grounded";
    default: return "Expression";
    }
}

/* The quotation of an atom as a closed checker term of the base type of
 * atoms: a leaf is a constant of its own, an expression the list of its
 * elements built with `cons` from `nil`.  Bottom-up with explicit stacks. */
static Atom *prime_hol_quote(PrimeHolTranslation *tr, Atom *root,
                             Atom *atom_type) {
    Arena *a = tr->a;
    Atom *list_type = prime_hol_fun(a, atom_type,
                                    prime_hol_fun(a, atom_type, atom_type));
    Atom *cons_name = atom_string(a, PRIME_HOL_META_PREFIX "cons");
    Atom *nil_name = atom_string(a, PRIME_HOL_META_PREFIX "nil");
    PrimeHeFrame *frames = NULL;
    size_t frame_len = 0u, frame_cap = 0u;
    Atom **results = NULL;
    size_t result_len = 0u, result_cap = 0u;
    Atom *answer = NULL;
    bool good = true;
    PRIME_HE_GROW(frames, frame_len, frame_cap, PrimeHeFrame, good);
    if (good) frames[frame_len++] = (PrimeHeFrame){.term = root};
    while (good && tr->ok && frame_len > 0u) {
        PrimeHeFrame *frame = &frames[frame_len - 1u];
        Atom *current = frame->term;
        size_t done = result_len - frame->first_result;
        if (current->kind == ATOM_EXPR && done < (size_t)current->expr.len) {
            Atom *child = current->expr.elems[done];
            PRIME_HE_GROW(frames, frame_len, frame_cap, PrimeHeFrame, good);
            if (!good) break;
            frames[frame_len++] =
                (PrimeHeFrame){.term = child, .first_result = result_len};
            continue;
        }
        Atom *result = NULL;
        if (current->kind != ATOM_EXPR) {
            Atom *name = prime_hol_name(a, PRIME_HOL_META_PREFIX "leaf:", current);
            if (!name) {
                prime_hol_fail(tr, prime_expr1(a, "checker-translation-allocation"));
                break;
            }
            prime_hol_declare_checker_constant(tr, name, atom_type);
            result = prime_hol_const(a, name, atom_type);
        } else {
            Atom **parts = results + frame->first_result;
            prime_hol_declare_checker_constant(tr, nil_name, atom_type);
            result = prime_hol_const(a, nil_name, atom_type);
            if (current->expr.len > 0u)
                prime_hol_declare_checker_constant(tr, cons_name, list_type);
            for (size_t i = (size_t)current->expr.len; i-- > 0u;)
                result = prime_hol_comb(
                    a, prime_hol_comb(a, prime_hol_const(a, cons_name, list_type),
                                      parts[i]),
                    result);
            result_len = frame->first_result;
        }
        frame_len--;
        if (!tr->ok) break;
        PRIME_HE_GROW(results, result_len, result_cap, Atom *, good);
        if (!good) break;
        results[result_len++] = result;
    }
    if (good && tr->ok && result_len == 1u) answer = results[0];
    if (!good) prime_hol_fail(tr, prime_expr1(a, "checker-translation-allocation"));
    free(frames);
    free(results);
    return answer;
}

/* The checker's verdict on the meta-type claim `claimed` of `term`.  The
 * classification axiom of the quoted atom is supplied as the only
 * commitment; the checker admits the claim exactly when it is that axiom
 * (Lean: TypeGrounding.MetaTypes, metaAccepts_iff_entailed and
 * metaRejects_iff_entailed). */
static PrimeHolVerdict prime_hol_check_meta(Arena *a, Atom *term,
                                            const char *claimed,
                                            Atom **reason_out) {
    PrimeHolTranslation tr = {.a = a, .ok = true};
    Atom *atom_operator = atom_string(a, PRIME_HOL_META_PREFIX "atom");
    Atom *atom_type = prime_hol_named_tyapp(a, atom_operator, prime_hol_nil(a));
    prime_hol_declare_operator(&tr, atom_operator, 0u);
    Atom *quoted = tr.ok ? prime_hol_quote(&tr, term, atom_type) : NULL;
    Atom *predicate_type = prime_hol_fun(a, atom_type, prime_hol_bool_type(a));
    const char *actual = prime_he_meta_class(term);
    char actual_name[64], claimed_name[64];
    snprintf(actual_name, sizeof(actual_name), "%s%s",
             PRIME_HOL_META_PREFIX, actual);
    snprintf(claimed_name, sizeof(claimed_name), "%s%s",
             PRIME_HOL_META_PREFIX, claimed);
    Atom *actual_predicate = atom_string(a, actual_name);
    Atom *claimed_predicate = atom_string(a, claimed_name);
    prime_hol_declare_checker_constant(&tr, actual_predicate, predicate_type);
    prime_hol_declare_checker_constant(&tr, claimed_predicate, predicate_type);
    PrimeHolVerdict verdict = PRIME_HOL_UNAVAILABLE;
    if (tr.ok && quoted) {
        Atom *axiom_name = atom_string(a, PRIME_HOL_META_PREFIX "classification");
        Atom *axiom = prime_hol_comb(
            a, prime_hol_const(a, actual_predicate, predicate_type), quoted);
        Atom *claim = prime_hol_comb(
            a, prime_hol_const(a, claimed_predicate, predicate_type), quoted);
        Atom *fact = prime_hol_step(
            a, "meta", atom_string(a, "fact"),
            atom_expr3(a, prime_sym(a, "AXIOM"), axiom_name, claim));
        Atom *commands = prime_hol_commands(&tr, prime_hol_nil(a));
        Atom *commitments = prime_hol_cons(
            a, atom_expr3(a, prime_sym(a, "HOL:Entry"), axiom_name, axiom),
            prime_hol_nil(a));
        if (tr.ok && commands)
            verdict = prime_hol_decide(a, commitments, commands, fact, NULL,
                                       NULL, reason_out);
    }
    if (!tr.ok)
        *reason_out = prime_expr2(a, "hol-translation", tr.reason);
    prime_hol_translation_free(&tr);
    return verdict;
}

/* ---- The dependent route: HE-style telescopes checked in LF ------------ */

/* A telescope component `(: $x A)` names its argument: the components after
 * it and the result see the argument itself for `$x`.  Upstream HE has no
 * such binder; it is the `he-prime` extension.  A derivation through a
 * declaration with such components is a dependent derivation, and the
 * hosted LF kernel decides it, in the deep embedding of HE's typing rules
 * as an LF signature:
 *
 *  - `tp : type` codes atoms, a symbol or literal by a constant `s<k>`, an
 *    expression of n elements by `e<n> : tp -> ... -> tp`, and a type
 *    variable by an LF variable;
 *  - `has : tp -> tp -> type` is the typing judgment;
 *  - the k-th declaration of `c`, `(: c T)`, is `c<i> : Pi vars. has c T`;
 *  - its application rule at arity n is `ap<i> : Pi implicit. Pi a1 ... an.
 *    has a1 D1 -> ... -> has an Dn -> has (c a1 ... an) R`, where a
 *    component `(: $x A)` contributes the domain `A` and its variable reads
 *    as its argument in the later domains and in `R`;
 *  - a tuple is `tup<n>`, a literal `lit<k> : has l T`.
 *
 * A derivation becomes a proof term of `has t T`, and the kernel checks it.
 * The judgment's remaining type variables are closed over.  The LF terms
 * use de Bruijn indices: `(Con c)`, `(Var i)`, `(App f a)`, `(Pi A B)`,
 * `(Lam A b)` and `(Srt type)`.  The kernel admits the signature before it
 * checks the term, so an ill-formed signature refutes nothing. */

typedef struct {
    Atom *key;
    Atom *name;
} PrimeLfEntry;

typedef struct {
    Arena *a;
    bool ok;
    Atom *reason;
    Atom **signature;
    size_t signature_len;
    size_t signature_cap;
    PrimeLfEntry *entries;
    size_t entry_len;
    size_t entry_cap;
    uint32_t next_code;
} PrimeLfTranslation;

/* Variables in scope: `vars[i]` is bound by the i-th binder from the
 * outside.  A name is a variable atom (by its id) or a symbol. */
typedef struct {
    Atom **vars;
    size_t len;
    size_t cap;
} PrimeLfScope;

static void prime_lf_fail(PrimeLfTranslation *tr, Atom *reason) {
    if (tr->ok) tr->reason = reason;
    tr->ok = false;
}

static void prime_lf_translation_free(PrimeLfTranslation *tr) {
    free(tr->signature);
    free(tr->entries);
}

static bool prime_lf_scope_push(PrimeLfScope *scope, Atom *name) {
    if (scope->len == scope->cap) {
        size_t cap = scope->cap ? scope->cap * 2u : 8u;
        Atom **vars = realloc(scope->vars, sizeof(Atom *) * cap);
        if (!vars) return false;
        scope->vars = vars;
        scope->cap = cap;
    }
    scope->vars[scope->len++] = name;
    return true;
}

static bool prime_lf_same_name(Atom *left, Atom *right) {
    if (left->kind == ATOM_VAR && right->kind == ATOM_VAR)
        return left->var_id == right->var_id;
    return atom_eq(left, right);
}

/* The de Bruijn index of a name at binder depth `depth`, or -1. */
static int64_t prime_lf_scope_index(const PrimeLfScope *scope, Atom *name,
                                    size_t depth) {
    for (size_t i = scope->len; i > 0u; i--)
        if (prime_lf_same_name(scope->vars[i - 1u], name))
            return (int64_t)depth - (int64_t)i;
    return -1;
}

static Atom *prime_lf_kcon(Arena *a, Atom *name) {
    return atom_expr2(a, prime_sym(a, "Con"), name);
}

static Atom *prime_lf_kapp(Arena *a, Atom *function, Atom *argument) {
    return atom_expr3(a, prime_sym(a, "App"), function, argument);
}

static Atom *prime_lf_kpi(Arena *a, Atom *domain, Atom *body) {
    return atom_expr3(a, prime_sym(a, "Pi"), domain, body);
}

static Atom *prime_lf_klam(Arena *a, Atom *domain, Atom *body) {
    return atom_expr3(a, prime_sym(a, "Lam"), domain, body);
}

static Atom *prime_lf_tp(Arena *a) { return prime_lf_kcon(a, prime_sym(a, "tp")); }

static void prime_lf_declare(PrimeLfTranslation *tr, Atom *name, Atom *type) {
    if (!tr->ok) return;
    if (tr->signature_len == tr->signature_cap) {
        size_t cap = tr->signature_cap ? tr->signature_cap * 2u : 32u;
        Atom **items = realloc(tr->signature, sizeof(Atom *) * cap);
        if (!items) {
            prime_lf_fail(tr, prime_expr1(tr->a, "lf-allocation"));
            return;
        }
        tr->signature = items;
        tr->signature_cap = cap;
    }
    tr->signature[tr->signature_len++] =
        atom_expr3(tr->a, prime_sym(tr->a, "DConst"), name, type);
}

static Atom *prime_lf_lookup(PrimeLfTranslation *tr, Atom *key) {
    for (size_t i = 0u; i < tr->entry_len; i++)
        if (atom_eq(tr->entries[i].key, key)) return tr->entries[i].name;
    return NULL;
}

static void prime_lf_remember(PrimeLfTranslation *tr, Atom *key, Atom *name) {
    if (!tr->ok) return;
    if (tr->entry_len == tr->entry_cap) {
        size_t cap = tr->entry_cap ? tr->entry_cap * 2u : 32u;
        PrimeLfEntry *items = realloc(tr->entries, sizeof(PrimeLfEntry) * cap);
        if (!items) {
            prime_lf_fail(tr, prime_expr1(tr->a, "lf-allocation"));
            return;
        }
        tr->entries = items;
        tr->entry_cap = cap;
    }
    tr->entries[tr->entry_len++] = (PrimeLfEntry){.key = key, .name = name};
}

static Atom *prime_lf_fresh_name(PrimeLfTranslation *tr, const char *prefix) {
    char buffer[48];
    snprintf(buffer, sizeof(buffer), "%s%u", prefix, tr->next_code++);
    return prime_sym(tr->a, buffer);
}

static void prime_lf_base(PrimeLfTranslation *tr) {
    Arena *a = tr->a;
    Atom *type = atom_expr2(a, prime_sym(a, "Srt"), prime_sym(a, "type"));
    prime_lf_declare(tr, prime_sym(a, "tp"), type);
    prime_lf_declare(tr, prime_sym(a, "has"),
                     prime_lf_kpi(a, prime_lf_tp(a), prime_lf_kpi(a, prime_lf_tp(a), type)));
}

/* The constant coding a symbol or a literal. */
static Atom *prime_lf_atom_constant(PrimeLfTranslation *tr, Atom *atom) {
    Atom *key = atom_expr2(tr->a, prime_sym(tr->a, "LfCode"), atom);
    Atom *name = prime_lf_lookup(tr, key);
    if (name) return name;
    name = prime_lf_fresh_name(tr, "s");
    prime_lf_declare(tr, name, prime_lf_tp(tr->a));
    prime_lf_remember(tr, key, name);
    return name;
}

/* The constant coding expressions of n elements. */
static Atom *prime_lf_expression_constant(PrimeLfTranslation *tr, size_t n) {
    Arena *a = tr->a;
    Atom *key = atom_expr2(a, prime_sym(a, "LfExpression"), atom_int(a, (int64_t)n));
    Atom *name = prime_lf_lookup(tr, key);
    if (name) return name;
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "e%zu", n);
    name = prime_sym(a, buffer);
    Atom *type = prime_lf_tp(a);
    for (size_t i = 0u; i < n; i++) type = prime_lf_kpi(a, prime_lf_tp(a), type);
    prime_lf_declare(tr, name, type);
    prime_lf_remember(tr, key, name);
    return name;
}

/* The code of an atom at binder depth `depth`, bottom-up with explicit
 * stacks.  A variable must be in scope. */
static Atom *prime_lf_code(PrimeLfTranslation *tr, Atom *root,
                           const PrimeLfScope *scope, size_t depth) {
    if (!tr->ok) return NULL;
    Arena *a = tr->a;
    PrimeHeAtomStack pending = {0};
    PrimeHeAtomStack done = {0};
    PrimeHeAtomStack expanded = {0};
    bool ok = prime_he_stack_push(&pending, root) &&
              prime_he_stack_push(&expanded, NULL);
    while (ok && tr->ok && pending.len > 0u) {
        Atom *atom = pending.items[pending.len - 1u];
        bool is_expanded = expanded.items[expanded.len - 1u] != NULL;
        if (atom->kind == ATOM_EXPR && !is_expanded) {
            expanded.items[expanded.len - 1u] = atom;
            for (CettaExprIndex i = atom->expr.len; ok && i > 0u; i--)
                ok = prime_he_stack_push(&pending, atom->expr.elems[i - 1u]) &&
                     prime_he_stack_push(&expanded, NULL);
            continue;
        }
        pending.len--;
        expanded.len--;
        Atom *code = NULL;
        if (atom->kind == ATOM_VAR) {
            int64_t index = prime_lf_scope_index(scope, atom, depth);
            if (index < 0) {
                prime_lf_fail(tr, prime_expr2(a, "lf-free-variable", atom));
                break;
            }
            code = atom_expr2(a, prime_sym(a, "Var"), atom_int(a, index));
        } else if (atom->kind == ATOM_EXPR) {
            size_t n = atom->expr.len;
            code = prime_lf_kcon(a, prime_lf_expression_constant(tr, n));
            Atom **parts = done.items + done.len - n;
            for (size_t i = 0u; i < n; i++) code = prime_lf_kapp(a, code, parts[i]);
            done.len -= n;
        } else {
            code = prime_lf_kcon(a, prime_lf_atom_constant(tr, atom));
        }
        ok = prime_he_stack_push(&done, code);
    }
    Atom *result = ok && tr->ok && done.len == 1u ? done.items[0] : NULL;
    free(pending.items);
    free(done.items);
    free(expanded.items);
    if (!ok) prime_lf_fail(tr, prime_expr1(a, "lf-allocation"));
    return tr->ok ? result : NULL;
}

static Atom *prime_lf_has(Arena *a, Atom *term, Atom *type) {
    return prime_lf_kapp(a, prime_lf_kapp(a, prime_lf_kcon(a, prime_sym(a, "has")), term),
                         type);
}

/* The variables of an atom, in order of first appearance, appended. */
static bool prime_lf_collect_vars(Atom *root, PrimeLfScope *found) {
    PrimeHeAtomStack stack = {0};
    bool ok = prime_he_stack_push(&stack, root);
    while (ok && stack.len > 0u) {
        Atom *atom = stack.items[--stack.len];
        if (atom->kind == ATOM_VAR) {
            bool seen = false;
            for (size_t i = 0u; i < found->len && !seen; i++)
                seen = prime_lf_same_name(found->vars[i], atom);
            if (!seen) ok = prime_lf_scope_push(found, atom);
        } else if (atom->kind == ATOM_EXPR) {
            for (CettaExprIndex i = atom->expr.len; ok && i > 0u; i--)
                ok = prime_he_stack_push(&stack, atom->expr.elems[i - 1u]);
        }
    }
    free(stack.items);
    return ok;
}

/* The constant of a declaration occurrence used as itself:
 * `c<i> : Pi vars. has atom declared`. */
static Atom *prime_lf_declaration_constant(PrimeLfTranslation *tr, Atom *atom,
                                           Atom *declared, Atom *occurrence) {
    Arena *a = tr->a;
    Atom *items[4] = {prime_sym(a, "LfDecl"), atom, declared, occurrence};
    Atom *key = atom_expr(a, items, 4);
    Atom *name = prime_lf_lookup(tr, key);
    if (name) return name;
    PrimeLfScope scope = {0};
    if (!prime_lf_collect_vars(declared, &scope)) {
        free(scope.vars);
        prime_lf_fail(tr, prime_expr1(a, "lf-allocation"));
        return NULL;
    }
    Atom *body = prime_lf_has(a, prime_lf_code(tr, atom, &scope, scope.len),
                              prime_lf_code(tr, declared, &scope, scope.len));
    Atom *type = body;
    for (size_t i = scope.len; i > 0u; i--) type = prime_lf_kpi(a, prime_lf_tp(a), type);
    free(scope.vars);
    if (!tr->ok) return NULL;
    name = prime_lf_fresh_name(tr, "c");
    prime_lf_declare(tr, name, type);
    prime_lf_remember(tr, key, name);
    return name;
}

/* The application rule of a declaration occurrence at arity n, and the
 * number of its implicit variables. */
static Atom *prime_lf_rule_constant(PrimeLfTranslation *tr, Atom *atom,
                                    Atom *declared, Atom *occurrence,
                                    size_t arity, PrimeLfScope *implicit_out) {
    Arena *a = tr->a;
    if (!prime_he_arrow_shaped(declared) ||
        (size_t)declared->expr.len - 2u != arity) {
        prime_lf_fail(tr, prime_expr2(a, "lf-rule-arity", declared));
        return NULL;
    }
    /* Binder variables read as their arguments; the rest are implicit. */
    PrimeLfScope all = {0};
    bool ok = prime_lf_collect_vars(declared, &all);
    Atom **argument_names = ok ? arena_alloc(a, sizeof(Atom *) * (arity + 1u)) : NULL;
    for (size_t i = 0u; ok && i < arity; i++) {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "arg%zu", i);
        argument_names[i] = prime_sym(a, buffer);
    }
    PrimeLfScope implicit = {0};
    for (size_t i = 0u; ok && i < all.len; i++) {
        bool bound = false;
        for (size_t j = 0u; j < arity && !bound; j++) {
            Atom *var = NULL, *domain = NULL;
            bound = prime_he_binder_component(declared->expr.elems[j + 1u], &var, &domain) &&
                    prime_lf_same_name(var, all.vars[i]);
        }
        if (!bound) ok = prime_lf_scope_push(&implicit, all.vars[i]);
    }
    free(all.vars);
    if (!ok) {
        free(implicit.vars);
        prime_lf_fail(tr, prime_expr1(a, "lf-allocation"));
        return NULL;
    }
    for (size_t i = 0u; i < implicit.len; i++)
        if (implicit_out && !prime_lf_scope_push(implicit_out, implicit.vars[i]))
            prime_lf_fail(tr, prime_expr1(a, "lf-allocation"));
    Atom *items[5] = {prime_sym(a, "LfRule"), atom, declared, occurrence,
                      atom_int(a, (int64_t)arity)};
    Atom *key = atom_expr(a, items, 5);
    Atom *name = prime_lf_lookup(tr, key);
    if (name || !tr->ok) {
        free(implicit.vars);
        return name;
    }
    /* Scope: implicit variables, then the arguments, with each binder
     * variable an alias of its argument's binder. */
    PrimeLfScope scope = {0};
    for (size_t i = 0u; ok && i < implicit.len; i++)
        ok = prime_lf_scope_push(&scope, implicit.vars[i]);
    for (size_t i = 0u; ok && i < arity; i++) {
        Atom *var = NULL, *domain = NULL;
        ok = prime_lf_scope_push(
            &scope, prime_he_binder_component(declared->expr.elems[i + 1u], &var, &domain)
                        ? var : argument_names[i]);
    }
    if (!ok) {
        free(implicit.vars);
        free(scope.vars);
        prime_lf_fail(tr, prime_expr1(a, "lf-allocation"));
        return NULL;
    }
    size_t base = implicit.len + arity;
    Atom **hypotheses = arena_alloc(a, sizeof(Atom *) * (arity + 1u));
    for (size_t j = 0u; tr->ok && j < arity; j++) {
        Atom *component = declared->expr.elems[j + 1u];
        Atom *var = NULL, *domain = component;
        (void)prime_he_binder_component(component, &var, &domain);
        /* The argument's code is its binder in scope. */
        Atom *argument = atom_expr2(a, prime_sym(a, "Var"),
                                    atom_int(a, (int64_t)(base + j) -
                                                    (int64_t)(implicit.len + j) - 1));
        hypotheses[j] = prime_lf_has(a, argument, prime_lf_code(tr, domain, &scope, base + j));
    }
    /* The conclusion `has (atom a1 ... an) R` at depth base + arity. */
    size_t depth = base + arity;
    Atom *subject = prime_lf_kcon(a, prime_lf_expression_constant(tr, arity + 1u));
    subject = prime_lf_kapp(a, subject, prime_lf_code(tr, atom, &scope, depth));
    for (size_t j = 0u; j < arity; j++)
        subject = prime_lf_kapp(
            a, subject,
            atom_expr2(a, prime_sym(a, "Var"),
                       atom_int(a, (int64_t)depth - (int64_t)(implicit.len + j) - 1)));
    Atom *result = declared->expr.elems[declared->expr.len - 1u];
    Atom *type = prime_lf_has(a, subject, prime_lf_code(tr, result, &scope, depth));
    for (size_t j = arity; j > 0u; j--) type = prime_lf_kpi(a, hypotheses[j - 1u], type);
    for (size_t i = base; i > 0u; i--) type = prime_lf_kpi(a, prime_lf_tp(a), type);
    free(scope.vars);
    free(implicit.vars);
    if (!tr->ok) return NULL;
    name = prime_lf_fresh_name(tr, "ap");
    prime_lf_declare(tr, name, type);
    prime_lf_remember(tr, key, name);
    return name;
}

/* The tuple rule of n + 1 elements. */
static Atom *prime_lf_tuple_constant(PrimeLfTranslation *tr, size_t elements) {
    Arena *a = tr->a;
    Atom *key = atom_expr2(a, prime_sym(a, "LfTuple"), atom_int(a, (int64_t)elements));
    Atom *name = prime_lf_lookup(tr, key);
    if (name) return name;
    /* Binders: y_0 .. y_n, t_0 .. t_n, then one hypothesis per element. */
    size_t base = 2u * elements;
    Atom **hypotheses = arena_alloc(a, sizeof(Atom *) * (elements + 1u));
    for (size_t i = 0u; i < elements; i++) {
        size_t depth = base + i;
        Atom *y = atom_expr2(a, prime_sym(a, "Var"), atom_int(a, (int64_t)depth - (int64_t)i - 1));
        Atom *t = atom_expr2(a, prime_sym(a, "Var"),
                             atom_int(a, (int64_t)depth - (int64_t)(elements + i) - 1));
        hypotheses[i] = prime_lf_has(a, y, t);
    }
    size_t depth = base + elements;
    Atom *subject = prime_lf_kcon(a, prime_lf_expression_constant(tr, elements));
    Atom *types = prime_lf_kcon(a, prime_lf_expression_constant(tr, elements));
    for (size_t i = 0u; i < elements; i++) {
        subject = prime_lf_kapp(a, subject, atom_expr2(a, prime_sym(a, "Var"),
                                atom_int(a, (int64_t)depth - (int64_t)i - 1)));
        types = prime_lf_kapp(a, types, atom_expr2(a, prime_sym(a, "Var"),
                              atom_int(a, (int64_t)depth - (int64_t)(elements + i) - 1)));
    }
    Atom *type = prime_lf_has(a, subject, types);
    for (size_t i = elements; i > 0u; i--) type = prime_lf_kpi(a, hypotheses[i - 1u], type);
    for (size_t i = base; i > 0u; i--) type = prime_lf_kpi(a, prime_lf_tp(a), type);
    if (!tr->ok) return NULL;
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "tup%zu", elements);
    name = prime_sym(a, buffer);
    prime_lf_declare(tr, name, type);
    prime_lf_remember(tr, key, name);
    return name;
}

/* The constant of a literal at its type. */
static Atom *prime_lf_literal_constant(PrimeLfTranslation *tr, Atom *literal,
                                       Atom *type, const PrimeLfScope *empty) {
    Arena *a = tr->a;
    Atom *key = atom_expr3(a, prime_sym(a, "LfLiteral"), literal, type);
    Atom *name = prime_lf_lookup(tr, key);
    if (name) return name;
    Atom *statement = prime_lf_has(a, prime_lf_code(tr, literal, empty, 0u),
                                   prime_lf_code(tr, type, empty, 0u));
    if (!tr->ok) return NULL;
    name = prime_lf_fresh_name(tr, "lit");
    prime_lf_declare(tr, name, statement);
    prime_lf_remember(tr, key, name);
    return name;
}

/* One-way matching: the values of the pattern's variables that make it the
 * instance, appended to `theta` (names and values in parallel). */
static bool prime_lf_match(Atom *pattern, Atom *instance, PrimeLfScope *names,
                           PrimeLfScope *values, bool *ok) {
    PrimeHeAtomStack stack = {0};
    bool result = true;
    *ok = prime_he_stack_push(&stack, pattern) && prime_he_stack_push(&stack, instance);
    while (*ok && result && stack.len > 0u) {
        Atom *value = stack.items[--stack.len];
        Atom *pat = stack.items[--stack.len];
        if (pat->kind == ATOM_VAR) {
            bool known = false;
            for (size_t i = 0u; i < names->len && !known; i++) {
                if (prime_lf_same_name(names->vars[i], pat)) {
                    known = true;
                    if (!atom_eq(values->vars[i], value)) result = false;
                }
            }
            if (!known)
                *ok = prime_lf_scope_push(names, pat) && prime_lf_scope_push(values, value);
            continue;
        }
        if (pat->kind == ATOM_EXPR) {
            if (value->kind != ATOM_EXPR || value->expr.len != pat->expr.len) {
                result = false;
                break;
            }
            for (CettaExprIndex i = 0u; *ok && i < pat->expr.len; i++)
                *ok = prime_he_stack_push(&stack, pat->expr.elems[i]) &&
                      prime_he_stack_push(&stack, value->expr.elems[i]);
            continue;
        }
        if (!atom_eq(pat, value)) result = false;
    }
    free(stack.items);
    return *ok && result;
}

static Atom *prime_lf_value_of(const PrimeLfScope *names, const PrimeLfScope *values,
                               Atom *var) {
    for (size_t i = 0u; i < names->len; i++)
        if (prime_lf_same_name(names->vars[i], var)) return values->vars[i];
    return NULL;
}

/* The HE term and the LF proof of one elaborated node, bottom-up with
 * explicit stacks, in the judgment's scope. */
static bool prime_lf_proof(PrimeLfTranslation *tr, Atom *root,
                           const PrimeLfScope *scope, Atom **term_out,
                           Atom **proof_out) {
    Arena *a = tr->a;
    PrimeHeAtomStack pending = {0};
    PrimeHeAtomStack expanded = {0};
    PrimeHeAtomStack terms = {0};
    PrimeHeAtomStack proofs = {0};
    PrimeLfScope empty = {0};
    bool ok = prime_he_stack_push(&pending, root) && prime_he_stack_push(&expanded, NULL);
    while (ok && tr->ok && pending.len > 0u) {
        Atom *node = pending.items[pending.len - 1u];
        bool is_expanded = expanded.items[expanded.len - 1u] != NULL;
        if (!node || node->kind != ATOM_EXPR || node->expr.len < 2u ||
            node->expr.elems[0]->kind != ATOM_SYMBOL) {
            prime_lf_fail(tr, prime_expr2(a, "lf-malformed-elaboration", node));
            break;
        }
        bool compound = is_symbol_named(node->expr.elems[0], "HeApp") ||
                        is_symbol_named(node->expr.elems[0], "HeTuple");
        if (compound && !is_expanded) {
            expanded.items[expanded.len - 1u] = node;
            for (CettaExprIndex i = node->expr.len; ok && i > 2u; i--)
                ok = prime_he_stack_push(&pending, node->expr.elems[i - 1u]) &&
                     prime_he_stack_push(&expanded, NULL);
            continue;
        }
        pending.len--;
        expanded.len--;
        Atom *term = NULL, *proof = NULL;
        if (is_symbol_named(node->expr.elems[0], "HeConst") && node->expr.len == 5u) {
            Atom *type = node->expr.elems[1];
            Atom *atom = node->expr.elems[2];
            Atom *declared = node->expr.elems[3];
            term = atom;
            if (atom->kind == ATOM_GROUNDED) {
                Atom *name = prime_lf_literal_constant(tr, atom, type, &empty);
                proof = name ? prime_lf_kcon(a, name) : NULL;
            } else {
                Atom *name = prime_lf_declaration_constant(
                    tr, atom, declared, node->expr.elems[4]);
                PrimeLfScope names = {0}, values = {0};
                bool matched = name && prime_lf_match(declared, type, &names, &values, &ok);
                PrimeLfScope order = {0};
                if (matched && ok) ok = prime_lf_collect_vars(declared, &order);
                proof = name ? prime_lf_kcon(a, name) : NULL;
                for (size_t i = 0u; ok && matched && proof && i < order.len; i++)
                    proof = prime_lf_kapp(
                        a, proof,
                        prime_lf_code(tr, prime_lf_value_of(&names, &values, order.vars[i]),
                                      scope, scope->len));
                if (!matched && ok)
                    prime_lf_fail(tr, prime_expr3(a, "lf-instance", declared, type));
                free(names.vars);
                free(values.vars);
                free(order.vars);
            }
        } else if (is_symbol_named(node->expr.elems[0], "HeTuple")) {
            size_t n = (size_t)node->expr.len - 2u;
            Atom **part_terms = terms.items + terms.len - n;
            Atom **part_proofs = proofs.items + proofs.len - n;
            Atom *type = node->expr.elems[1];
            if (type->kind != ATOM_EXPR || (size_t)type->expr.len != n) {
                prime_lf_fail(tr, prime_expr2(a, "lf-tuple-type", type));
                break;
            }
            Atom *name = prime_lf_tuple_constant(tr, n);
            proof = name ? prime_lf_kcon(a, name) : NULL;
            for (size_t i = 0u; proof && i < n; i++)
                proof = prime_lf_kapp(a, proof, prime_lf_code(tr, part_terms[i], scope, scope->len));
            for (size_t i = 0u; proof && i < n; i++)
                proof = prime_lf_kapp(a, proof, prime_lf_code(tr, type->expr.elems[i], scope,
                                                                 scope->len));
            for (size_t i = 0u; proof && i < n; i++)
                proof = prime_lf_kapp(a, proof, part_proofs[i]);
            term = atom_expr(a, part_terms, (CettaExprLen)n);
            terms.len -= n;
            proofs.len -= n;
        } else if (is_symbol_named(node->expr.elems[0], "HeApp")) {
            size_t n = (size_t)node->expr.len - 3u;
            Atom **part_terms = terms.items + terms.len - (n + 1u);
            Atom **part_proofs = proofs.items + proofs.len - (n + 1u);
            Atom *head = node->expr.elems[2];
            if (!head || head->kind != ATOM_EXPR || head->expr.len != 5u ||
                !is_symbol_named(head->expr.elems[0], "HeConst") ||
                head->expr.elems[2]->kind == ATOM_GROUNDED) {
                prime_lf_fail(tr, prime_expr2(a, "lf-non-constant-head", head));
                break;
            }
            Atom *head_type = head->expr.elems[1];
            Atom *atom = head->expr.elems[2];
            Atom *declared = head->expr.elems[3];
            PrimeLfScope implicit = {0};
            Atom *name = prime_lf_rule_constant(tr, atom, declared, head->expr.elems[4],
                                                n, &implicit);
            PrimeLfScope names = {0}, values = {0};
            bool matched = name && prime_lf_match(declared, head_type, &names, &values, &ok);
            proof = name ? prime_lf_kcon(a, name) : NULL;
            for (size_t i = 0u; ok && matched && proof && i < implicit.len; i++)
                proof = prime_lf_kapp(
                    a, proof,
                    prime_lf_code(tr, prime_lf_value_of(&names, &values, implicit.vars[i]),
                                  scope, scope->len));
            for (size_t i = 0u; ok && matched && proof && i < n; i++)
                proof = prime_lf_kapp(a, proof,
                                      prime_lf_code(tr, part_terms[i + 1u], scope, scope->len));
            for (size_t i = 0u; ok && matched && proof && i < n; i++)
                proof = prime_lf_kapp(a, proof, part_proofs[i + 1u]);
            if (!matched && ok && tr->ok)
                prime_lf_fail(tr, prime_expr3(a, "lf-instance", declared, head_type));
            (void)part_proofs;
            term = atom_expr(a, part_terms, (CettaExprLen)(n + 1u));
            terms.len -= n + 1u;
            proofs.len -= n + 1u;
            free(implicit.vars);
            free(names.vars);
            free(values.vars);
        } else {
            prime_lf_fail(tr, prime_expr2(a, "lf-malformed-elaboration", node));
            break;
        }
        if (!tr->ok || !proof) break;
        ok = prime_he_stack_push(&terms, term) && prime_he_stack_push(&proofs, proof);
    }
    bool done = ok && tr->ok && terms.len == 1u && proofs.len == 1u;
    if (done) {
        *term_out = terms.items[0];
        *proof_out = proofs.items[0];
    } else if (!ok) {
        prime_lf_fail(tr, prime_expr1(a, "lf-allocation"));
    } else if (tr->ok) {
        prime_lf_fail(tr, prime_expr1(a, "lf-incomplete-proof"));
    }
    free(pending.items);
    free(expanded.items);
    free(terms.items);
    free(proofs.items);
    return done;
}

/* The variables a judgment leaves free: those of the instance types, the
 * terms and the asked type, never those of the declared types. */
static bool prime_lf_judgment_vars(Atom *root, Atom *type, PrimeLfScope *found) {
    PrimeHeAtomStack stack = {0};
    bool ok = prime_he_stack_push(&stack, root);
    while (ok && stack.len > 0u) {
        Atom *node = stack.items[--stack.len];
        if (!node || node->kind != ATOM_EXPR || node->expr.len < 2u) continue;
        ok = prime_lf_collect_vars(node->expr.elems[1], found);
        if (ok && is_symbol_named(node->expr.elems[0], "HeConst") && node->expr.len == 5u)
            ok = prime_lf_collect_vars(node->expr.elems[2], found);
        if (is_symbol_named(node->expr.elems[0], "HeApp") ||
            is_symbol_named(node->expr.elems[0], "HeTuple"))
            for (CettaExprIndex i = 2u; ok && i < node->expr.len; i++)
                ok = prime_he_stack_push(&stack, node->expr.elems[i]);
    }
    free(stack.items);
    return ok && (!type || prime_lf_collect_vars(type, found));
}

static Atom *prime_lf_signature_atom(PrimeLfTranslation *tr) {
    Arena *a = tr->a;
    Atom *list = prime_sym(a, "SNil");
    for (size_t i = tr->signature_len; i > 0u; i--)
        list = atom_expr3(a, prime_sym(a, "SCons"), tr->signature[i - 1u], list);
    return list;
}

/* A failed signature admission cannot refute a typing judgment. The
 * compact batch checker combines those two failures in one Boolean. */
static PrimeHolVerdict prime_lf_check_admitted(
    Arena *a, Atom *signature, Atom *proof, Atom *statement, Atom **reason_out) {
    Atom *answer = NULL, *reason = NULL;
    if (!eval_prime_lf_guest_evaluate(
            a, prime_expr2(a, "sig-admitted", signature), &answer, &reason) ||
        !prime_hol_true(answer)) {
        *reason_out = prime_expr2(a, "lf-signature-not-admitted",
                                  reason ? reason : answer);
        return PRIME_HOL_UNAVAILABLE;
    }
    Atom *job = atom_expr3(a, prime_sym(a, "Accept"), proof, statement);
    Atom *jobs = atom_expr3(a, prime_sym(a, "Cons"), job, prime_sym(a, "Nil"));
    Atom *request = atom_expr3(a, prime_sym(a, "kernel-check-batch-judgments"),
                               signature, jobs);
    if (!eval_prime_lf_guest_evaluate(a, request, &answer, &reason)) {
        *reason_out = prime_expr2(a, "lf-checker-unavailable",
                                  reason ? reason : prime_expr1(a, "no-answer"));
        return PRIME_HOL_UNAVAILABLE;
    }
    if (prime_hol_true(answer)) return PRIME_HOL_ACCEPTED;
    if (is_symbol_named(answer, "False") ||
        (answer->kind == ATOM_GROUNDED && answer->ground.gkind == GV_BOOL)) {
        *reason_out = prime_expr2(a, "lf-checker-refuses", prime_expr1(a, "judgment"));
        return PRIME_HOL_REFUSED;
    }
    *reason_out = prime_expr2(a, "lf-checker-unavailable", answer);
    return PRIME_HOL_UNAVAILABLE;
}

/* The kernel's verdict that the elaborated node proves `has t type`, where
 * `t` is the node's term; UNAVAILABLE when the translation or the kernel
 * fails. */
static PrimeHolVerdict prime_lf_check(Arena *a, Atom *annotation, Atom *type,
                                      Atom **reason_out) {
    PrimeLfTranslation tr = {.a = a, .ok = true};
    PrimeLfScope scope = {0};
    prime_lf_base(&tr);
    if (!prime_lf_judgment_vars(annotation, type, &scope))
        prime_lf_fail(&tr, prime_expr1(a, "lf-allocation"));
    Atom *term = NULL, *proof = NULL;
    if (tr.ok) (void)prime_lf_proof(&tr, annotation, &scope, &term, &proof);
    Atom *statement = tr.ok
        ? prime_lf_has(a, prime_lf_code(&tr, term, &scope, scope.len),
                       prime_lf_code(&tr, type, &scope, scope.len))
        : NULL;
    PrimeHolVerdict verdict = PRIME_HOL_UNAVAILABLE;
    if (tr.ok && statement && proof) {
        for (size_t i = scope.len; i > 0u; i--) {
            proof = prime_lf_klam(a, prime_lf_tp(a), proof);
            statement = prime_lf_kpi(a, prime_lf_tp(a), statement);
        }
        verdict = prime_lf_check_admitted(
            a, prime_lf_signature_atom(&tr), proof, statement, reason_out);
    } else {
        *reason_out = prime_expr2(a, "lf-translation",
                                  tr.reason ? tr.reason : prime_expr1(a, "incomplete"));
    }
    free(scope.vars);
    prime_lf_translation_free(&tr);
    return verdict;
}

/* The kernel reads each part of a failed dependent attempt and refuses its
 * step: an application of another arity has no rule at that arity, and an
 * argument that does not have its parameter's type is refused at it. */
static PrimeHolVerdict prime_lf_check_attempt(Arena *a, const PrimeHeAttempt *attempt,
                                              Atom **reason_out) {
    Atom *node = attempt->annotation;
    if (!node || node->kind != ATOM_EXPR || node->expr.len < 2u) {
        *reason_out = prime_expr1(a, "lf-malformed-attempt");
        return PRIME_HOL_UNAVAILABLE;
    }
    bool arity = is_symbol_named(node->expr.elems[0], "HeArity");
    size_t first = arity ? 2u : 1u;
    for (CettaExprIndex i = first; i < node->expr.len; i++) {
        Atom *part = node->expr.elems[i];
        if (!part || part->kind != ATOM_EXPR || part->expr.len < 2u) {
            *reason_out = prime_expr1(a, "lf-malformed-attempt");
            return PRIME_HOL_UNAVAILABLE;
        }
        Atom *reason = NULL;
        PrimeHolVerdict verdict = prime_lf_check(a, part, part->expr.elems[1], &reason);
        if (verdict != PRIME_HOL_ACCEPTED) {
            *reason_out = prime_expr2(a, "lf-checker-refuses-part",
                                      reason ? reason : prime_expr1(a, "no-answer"));
            return PRIME_HOL_UNAVAILABLE;
        }
    }
    if (arity) return PRIME_HOL_REFUSED;
    Atom *why = attempt->reason;
    Atom *expected = why && why->kind == ATOM_EXPR && why->expr.len == 4u
        ? why->expr.elems[2] : NULL;
    Atom *failing = node->expr.elems[node->expr.len - 1u];
    if (!expected) {
        *reason_out = prime_expr1(a, "lf-malformed-attempt");
        return PRIME_HOL_UNAVAILABLE;
    }
    Atom *reason = NULL;
    PrimeHolVerdict verdict = prime_lf_check(a, failing, expected, &reason);
    *reason_out = reason;
    return verdict;
}

/* Substitution may invalidate an originally admitted arrow (for example,
 * by making a parameter the reserved meta-type Atom). Every instance in
 * the derivation must still belong to the contract grammar. */
static bool prime_he_annotation_admitted(Atom *root, Atom **bad_type_out) {
    PrimeHeAtomStack stack = {0};
    *bad_type_out = NULL;
    bool ok = prime_he_stack_push(&stack, root);
    while (ok && stack.len > 0u) {
        Atom *node = stack.items[--stack.len];
        if (!node || node->kind != ATOM_EXPR || node->expr.len < 2u)
            continue;
        bool compound = is_symbol_named(node->expr.elems[0], "HeApp") ||
                        is_symbol_named(node->expr.elems[0], "HeTuple");
        bool constant = is_symbol_named(node->expr.elems[0], "HeConst");
        if ((compound || constant) &&
            !prime_he_contract_type(node->expr.elems[1], &ok)) {
            *bad_type_out = node->expr.elems[1];
            ok = false;
            break;
        }
        if (!constant)
            for (CettaExprIndex i = compound ? 2u : 1u;
                 ok && i < node->expr.len; i++)
                ok = prime_he_stack_push(&stack, node->expr.elems[i]);
    }
    free(stack.items);
    return ok;
}

/* The checker of a derivation: the hosted LF kernel for a dependent one,
 * the hosted HOL Light checker otherwise. */
static PrimeHolVerdict prime_he_check_derivation(Arena *a, Atom *annotation,
                                                 Atom *type, bool dependent,
                                                 Atom **reason_out) {
    Atom *bad = NULL;
    if (!prime_he_annotation_admitted(annotation, &bad)) {
        *reason_out = prime_expr2(a, "inadmissible-instantiated-type",
                                  bad ? bad : prime_expr1(a, "allocation"));
        return PRIME_HOL_UNAVAILABLE;
    }
    return dependent ? prime_lf_check(a, annotation, type, reason_out)
                     : prime_hol_check_typing(a, annotation, type, reason_out);
}

/* ---- Judgments --------------------------------------------------------- */

/* Inside the grounded fragments a check the hosted checker refuses is a
 * genuine refutation: the contract is HE's first-order rule, the
 * derivations and failed attempts enumerate every way of typing the term,
 * and the checker decides the contract's typing of each.  Outside the
 * fragments nothing refutes and nothing prunes: lacking a derivation is no
 * negative, and the answer is Undetermined. */
typedef enum {
    PRIME_HE_JUDGED_ESTABLISHED = 0,
    PRIME_HE_JUDGED_REFUTED,
    PRIME_HE_JUDGED_UNDETERMINED
} PrimeHeJudged;

typedef struct {
    PrimeHeJudged status;
    /* Established: the evidence; otherwise the reason. */
    Atom *detail;
    /* Synthesis, established: one type per derivation, in HE's order, and
     * `type` when there is exactly one. */
    Atom **types;
    size_t type_count;
    Atom *type;
    /* Synthesis, undetermined: no declaration typed any part of the term. */
    bool unknown;
} PrimeHeJudgment;

static PrimeHeJudgment prime_he_judged(PrimeHeJudged status, Atom *detail) {
    return (PrimeHeJudgment){.status = status, .detail = detail};
}

/* The checker that decided the derivations: the hosted HOL Light checker,
 * the hosted LF kernel for the dependent ones, or both. */
static Atom *prime_he_checked_label(Arena *a, const PrimeHeCandidates *c) {
    bool lf = false, hol = false;
    for (size_t i = 0u; i < c->derivation_len; i++) {
        if (c->derivations[i].dependent) lf = true;
        else hol = true;
    }
    if (lf && hol)
        return prime_expr3(a, "Checked", prime_sym(a, "hol-light"), prime_sym(a, "lf"));
    return lf ? prime_expr2(a, "LFChecked", prime_sym(a, "lf"))
              : prime_expr2(a, "HOLChecked", prime_sym(a, "hol-light"));
}

/* `(HOLChecked hol-light fragment type)`, or `(LFChecked lf ...)` for a
 * dependent derivation. */
static Atom *prime_he_checked_items(Arena *a, bool dependent, Atom *fragment,
                                    Atom *type) {
    Atom *items[4] = {prime_sym(a, dependent ? "LFChecked" : "HOLChecked"),
                      prime_sym(a, dependent ? "lf" : "hol-light"), fragment, type};
    return atom_expr(a, items, 4);
}

static PrimeHeJudgment prime_he_allocation_undetermined(Arena *a) {
    return prime_he_judged(
        PRIME_HE_JUDGED_UNDETERMINED,
        prime_expr2(a, "outside-kernel-fragments",
                    prime_expr1(a, "fragment-traversal-allocation")));
}

/* Every failed attempt is refused by the checker, refutably.  REFUSED when
 * each is; otherwise the verdict that stopped it, with the reason. */
static PrimeHolVerdict prime_he_attempts_refused(Arena *a,
                                                 const PrimeHeCandidates *c,
                                                 Atom **reason_out) {
    *reason_out = NULL;
    for (size_t i = 0u; i < c->attempt_len; i++) {
        const PrimeHeAttempt *attempt = &c->attempts[i];
        if (!attempt->refutable) {
            *reason_out = prime_expr2(a, "attempt-outside-first-order-fragment",
                                      attempt->reason);
            return PRIME_HOL_UNAVAILABLE;
        }
        Atom *reason = NULL;
        Atom *bad = NULL;
        if (!prime_he_annotation_admitted(attempt->annotation, &bad)) {
            *reason_out = prime_expr2(a, "inadmissible-instantiated-type",
                                      bad ? bad : prime_expr1(a, "allocation"));
            return PRIME_HOL_UNAVAILABLE;
        }
        PrimeHolVerdict verdict = attempt->dependent
            ? prime_lf_check_attempt(a, attempt, &reason)
            : prime_hol_check_attempt(a, attempt->annotation, &reason);
        if (verdict != PRIME_HOL_REFUSED) {
            *reason_out = verdict == PRIME_HOL_ACCEPTED
                ? prime_expr2(a, "elaboration-and-checker-disagree",
                              attempt->reason)
                : reason;
            return verdict;
        }
    }
    return PRIME_HOL_REFUSED;
}

/* A term inside the fragments without a derivation: refuted when the
 * checker refuses every failed attempt. */
static PrimeHeJudgment prime_he_refute_rejected(Arena *a,
                                                const PrimeHeCandidates *c) {
    Atom *reason = NULL;
    PrimeHolVerdict verdict = prime_he_attempts_refused(a, c, &reason);
    Atom *rejects = prime_expr2(a, "grounded-fragment-rejects",
                                c->reason ? c->reason
                                          : prime_expr1(a, "failed-attempts"));
    if (verdict == PRIME_HOL_REFUSED)
        return prime_he_judged(
            PRIME_HE_JUDGED_REFUTED,
            prime_expr3(a, "HOLRefuted", rejects,
                        prime_expr2(a, "hol-checker-refuses",
                                    prime_expr1(a, "application-step"))));
    return prime_he_judged(
        PRIME_HE_JUDGED_UNDETERMINED,
        prime_expr2(a, "outside-kernel-fragments",
                    prime_expr2(a, "unchecked-rejection",
                                reason ? reason : rejects)));
}

/* type:of, and get-type with `all_schemes`: the type of every derivation,
 * each accepted by the checker, in HE's order. */
static PrimeHeJudgment prime_he_synthesize(Space *space, Arena *a, Atom *term,
                                           bool all_schemes,
                                           PrimeResourceLedger *ledger) {
    PrimeHeCandidates c = prime_he_elaborate(space, a, term, false, all_schemes,
                                             ledger, PRIME_RESOURCE_SYNTHESIS);
    if (c.status == PRIME_HE_REJECTED) return prime_he_refute_rejected(a, &c);
    if (c.status != PRIME_HE_TYPED) {
        PrimeHeJudgment answer = prime_he_judged(
            PRIME_HE_JUDGED_UNDETERMINED, prime_he_undetermined_reason(a, &c));
        answer.unknown = c.status == PRIME_HE_DYNAMIC;
        return answer;
    }
    Atom **items = arena_alloc(a, sizeof(Atom *) * (c.derivation_len + 3u));
    for (size_t i = 0u; i < c.derivation_len; i++) {
        const PrimeHeDerivation *d = &c.derivations[i];
        Atom *reason = NULL;
        PrimeHolVerdict verdict = prime_he_check_derivation(
            a, d->annotation, d->type, d->dependent, &reason);
        if (verdict == PRIME_HOL_REFUSED)
            return prime_he_judged(
                PRIME_HE_JUDGED_UNDETERMINED,
                prime_expr2(a, "outside-kernel-fragments",
                            prime_expr2(a, "elaboration-and-checker-disagree",
                                        reason)));
        if (verdict != PRIME_HOL_ACCEPTED)
            return prime_he_judged(
                PRIME_HE_JUDGED_UNDETERMINED,
                reason ? reason : prime_expr1(a, "hol-checker-unavailable"));
        items[i + 3u] = d->type;
    }
    /* `InferredTypes` keeps the public `type:of` reading: the types follow
     * the two descriptive fields.  A scheme stands for its instances. */
    items[0] = prime_sym(a, "InferredTypes");
    items[1] = prime_he_checked_label(a, &c);
    items[2] = prime_he_fragment_name(a, &c);
    PrimeHeJudgment answer = prime_he_judged(
        PRIME_HE_JUDGED_ESTABLISHED,
        atom_expr(a, items, (CettaExprLen)(c.derivation_len + 3u)));
    answer.types = items + 3u;
    answer.type_count = c.derivation_len;
    answer.type = c.derivation_len == 1u ? items[3] : NULL;
    return answer;
}

/* The checker's verdict that some derivation of the term has the static
 * type `expected`.  Established when the checker accepts a derivation at
 * it; refuted when it refuses each derivation at it and every failed
 * attempt.  A scheme is checked at the instance that `expected` selects
 * when it is an instance.  When it is not, the scheme is checked at its
 * principal instance, which the checker then refuses: a term is typed at
 * a clean type exactly at the instances of the principal types of its ways
 * of typing, overloads, arrows and products included (Lean:
 * HOLContract.Principal.principal_decides), and a dependent derivation at
 * the instances its LF proofs allow, by their canonical forms. */
static PrimeHeJudgment prime_he_check_static(Arena *a,
                                             const PrimeHeCandidates *c,
                                             Atom *term, Atom *expected,
                                             Atom *(*evidence)(Arena *, bool,
                                                               Atom *, Atom *)) {
    bool ok = true;
    Atom *undecided = NULL;
    for (size_t i = 0u; i < c->derivation_len; i++) {
        const PrimeHeDerivation *d = &c->derivations[i];
        if (d->rigid) {
            undecided = prime_expr2(
                a, "outside-kernel-fragments",
                prime_expr2(a, "rigid-declaration-variables", term));
            continue;
        }
        Atom *annotation = d->annotation;
        bool instance = true;
        if (d->schematic) {
            PrimeHeSubst match = {0};
            instance = prime_he_unify(&match, d->type, expected, &ok);
            if (ok && instance)
                annotation = prime_he_subst_resolve(a, &match, d->annotation, &ok);
            free(match.items);
            if (!ok) return prime_he_allocation_undetermined(a);
        }
        Atom *reason = NULL;
        PrimeHolVerdict verdict = prime_he_check_derivation(
            a, annotation, expected, d->dependent, &reason);
        if (verdict == PRIME_HOL_ACCEPTED && instance)
            return prime_he_judged(
                PRIME_HE_JUDGED_ESTABLISHED,
                evidence(a, d->dependent, prime_he_fragment_name(a, c), expected));
        if (verdict == PRIME_HOL_REFUSED && !(d->schematic && instance))
            continue;
        undecided = verdict == PRIME_HOL_UNAVAILABLE
            ? (reason ? reason : prime_expr1(a, "hol-checker-unavailable"))
            : prime_expr2(a, "outside-kernel-fragments",
                          prime_expr2(a, "elaboration-and-checker-disagree",
                                      reason ? reason : d->type));
    }
    if (undecided) return prime_he_judged(PRIME_HE_JUDGED_UNDETERMINED, undecided);
    Atom *reason = NULL;
    PrimeHolVerdict attempts = prime_he_attempts_refused(a, c, &reason);
    if (attempts != PRIME_HOL_REFUSED)
        return prime_he_judged(
            PRIME_HE_JUDGED_UNDETERMINED,
            prime_expr2(a, "outside-kernel-fragments",
                        prime_expr2(a, "unchecked-rejection",
                                    reason ? reason : expected)));
    return prime_he_judged(
        PRIME_HE_JUDGED_REFUTED,
        prime_expr2(a, "HOLRefuted",
                    prime_expr2(a, "hol-checker-refuses",
                                prime_expr2(a, "expected-type", expected))));
}

static Atom *prime_he_checked_evidence(Arena *a, bool dependent,
                                       Atom *fragment, Atom *expected) {
    return prime_expr3(a, "CheckedTypingEvidence", prime_sym(a, "exact"),
                       prime_he_checked_items(a, dependent, fragment, expected));
}

static Atom *prime_he_analyzed_evidence(Arena *a, bool dependent,
                                        Atom *fragment, Atom *expected) {
    return prime_expr3(a, "ConsistencyEvidence", prime_sym(a, "exact"),
                       prime_he_checked_items(a, dependent, fragment, expected));
}

/* type:check (exact).  A term inside the fragments that the checker refuses
 * at a static expected type is refuted; an expected type outside the static
 * types leaves the judgment Undetermined. */
static PrimeHeJudgment prime_he_check_exact(Space *space, Arena *a, Atom *term,
                                            Atom *expected,
                                            PrimeResourceLedger *ledger) {
    PrimeHeCandidates c = prime_he_elaborate(space, a, term, false, false,
                                             ledger, PRIME_RESOURCE_CHECKING);
    bool ok = true;
    bool static_expected = prime_he_ground_contract_type(expected, &ok);
    if (!ok) return prime_he_allocation_undetermined(a);
    if (c.status != PRIME_HE_TYPED && c.status != PRIME_HE_REJECTED)
        return prime_he_judged(PRIME_HE_JUDGED_UNDETERMINED,
                               prime_he_undetermined_reason(a, &c));
    if (!static_expected)
        return prime_he_judged(
            PRIME_HE_JUDGED_UNDETERMINED,
            prime_expr2(a, "outside-kernel-fragments",
                        prime_expr2(a, "expected-type-outside-fragments", expected)));
    if (c.status == PRIME_HE_REJECTED) return prime_he_refute_rejected(a, &c);
    return prime_he_check_static(a, &c, term, expected, prime_he_checked_evidence);
}

static bool prime_he_agt_consistent(Atom *left, Atom *right, bool *ok) {
    *ok = true;
    if (is_symbol_named(left, "Atom") || is_symbol_named(right, "Atom"))
        return true;
    PrimeHeAtomStack stack = {0};
    bool result = true;
    *ok = prime_he_stack_push(&stack, left) && prime_he_stack_push(&stack, right);
    while (*ok && result && stack.len > 0u) {
        Atom *r = stack.items[--stack.len];
        Atom *l = stack.items[--stack.len];
        if (atom_is_symbol_id(l, g_builtin_syms.undefined_type) ||
            atom_is_symbol_id(r, g_builtin_syms.undefined_type))
            continue;
        if (l->kind == ATOM_EXPR && r->kind == ATOM_EXPR) {
            if (l->expr.len != r->expr.len) {
                result = false;
                break;
            }
            for (CettaExprIndex i = 0u; *ok && i < l->expr.len; i++)
                *ok = prime_he_stack_push(&stack, l->expr.elems[i]) &&
                      prime_he_stack_push(&stack, r->expr.elems[i]);
            continue;
        }
        if (l->kind == ATOM_EXPR || r->kind == ATOM_EXPR || !atom_eq(l, r))
            result = false;
    }
    free(stack.items);
    return *ok && result;
}

/* The declared gradual type of a constant outside the static fragments: its
 * single variable-free, non-arrow declared type. */
static Atom *prime_he_declared_gradual_type(Space *space, Arena *a,
                                            Atom *term) {
    if (term->kind != ATOM_SYMBOL) return NULL;
    Atom **declared = NULL;
    uint32_t count = prime_he_declared_types(space, a, term, &declared);
    Atom *type = count == 1u ? declared[0] : NULL;
    free(declared);
    if (!type || atom_has_vars(type) || prime_he_arrow_shaped(type) ||
        prime_he_native_binder(type))
        return NULL;
    return type;
}

/* type:analyze.  A meta-type is decided by the classification theory in the
 * checker, refusal included.  A static expected type is consistent with a
 * static type exactly when the two are equal (Lean:
 * TypeGrounding.Gradual.agt_static_iff_eq), so it is checked as type:check
 * is, except that a term without a derivation is HE's `%Undefined%`
 * fallback and stays Undetermined.  A gradual expected type is decided by
 * consistency of concretizations, which refutes nothing here. */
static PrimeHeJudgment prime_he_analyze(Space *space, Arena *a, Atom *term,
                                        Atom *expected,
                                        PrimeResourceLedger *ledger) {
    if (prime_he_symbol_in(expected, PRIME_HE_META_TYPE_NAMES)) {
        Atom *reason = NULL;
        PrimeHolVerdict verdict = prime_hol_check_meta(
            a, term, atom_name_cstr(expected), &reason);
        if (verdict == PRIME_HOL_ACCEPTED)
            return prime_he_judged(
                PRIME_HE_JUDGED_ESTABLISHED,
                prime_expr3(a, "ConsistencyEvidence", prime_sym(a, "meta-staging"),
                            prime_expr3(a, "MetaTypeChecked",
                                        prime_sym(a, "hol-light"), expected)));
        if (verdict == PRIME_HOL_REFUSED)
            return prime_he_judged(
                PRIME_HE_JUDGED_REFUTED,
                prime_expr3(a, "HOLRefuted",
                            prime_expr3(a, "meta-type-differs",
                                        prime_sym(a, prime_he_meta_class(term)),
                                        expected),
                            reason));
        return prime_he_judged(
            PRIME_HE_JUDGED_UNDETERMINED,
            reason ? reason : prime_expr1(a, "hol-checker-unavailable"));
    }
    if (is_symbol_named(expected, "Atom"))
        return prime_he_judged(
            PRIME_HE_JUDGED_ESTABLISHED,
            prime_expr3(a, "ConsistencyEvidence", prime_sym(a, "top"),
                        prime_expr2(a, "MetaTypeEntailed", expected)));
    if (atom_has_vars(expected))
        return prime_he_judged(
            PRIME_HE_JUDGED_UNDETERMINED,
            prime_expr2(a, "outside-kernel-fragments",
                        prime_expr2(a, "schematic-expected-type", expected)));
    PrimeHeCandidates c = prime_he_elaborate(space, a, term, false, false,
                                             ledger, PRIME_RESOURCE_CHECKING);
    bool ok = true;
    bool static_expected = prime_he_ground_contract_type(expected, &ok);
    if (!ok) return prime_he_allocation_undetermined(a);
    if (c.status == PRIME_HE_DYNAMIC)
        /* The unknown type is consistent with every variable-free type. */
        return prime_he_judged(
            PRIME_HE_JUDGED_ESTABLISHED,
            prime_expr3(a, "ConsistencyEvidence", prime_sym(a, "dynamic"),
                        prime_expr2(a, "AGTConsistent", expected)));
    if (c.status == PRIME_HE_TYPED && static_expected)
        return prime_he_check_static(a, &c, term, expected,
                                     prime_he_analyzed_evidence);
    if (c.status == PRIME_HE_TYPED) {
        /* A gradual expected type: a variable-free type that the checker
         * accepts must be consistent with it. */
        Atom *reason = NULL;
        Atom *inconsistent = NULL;
        for (size_t i = 0u; i < c.derivation_len; i++) {
            const PrimeHeDerivation *d = &c.derivations[i];
            if (d->rigid || atom_has_vars(d->type)) continue;
            if (!prime_he_agt_consistent(d->type, expected, &ok) || !ok) {
                if (!ok) return prime_he_allocation_undetermined(a);
                inconsistent = d->type;
                continue;
            }
            PrimeHolVerdict verdict = prime_he_check_derivation(
                a, d->annotation, d->type, d->dependent, &reason);
            if (verdict != PRIME_HOL_ACCEPTED) continue;
            Atom *checked = prime_he_checked_items(
                a, d->dependent, prime_he_fragment_name(a, &c), d->type);
            return prime_he_judged(
                PRIME_HE_JUDGED_ESTABLISHED,
                prime_expr3(a, "ConsistencyEvidence", prime_sym(a, "dynamic"),
                            prime_expr3(a, "AGTConsistent", checked, expected)));
        }
        return prime_he_judged(
            PRIME_HE_JUDGED_UNDETERMINED,
            prime_expr2(a, "outside-kernel-fragments",
                        inconsistent ? prime_expr3(a, "agt-inconsistent",
                                                   inconsistent, expected)
                        : reason ? reason
                        : prime_expr1(a, "hol-checker-unavailable")));
    }
    /* Outside the static fragments, a constant's declared gradual type can
     * still be consistent with the expected type. */
    Atom *gradual = c.status == PRIME_HE_OUTSIDE
        ? prime_he_declared_gradual_type(space, a, term) : NULL;
    if (gradual && prime_he_agt_consistent(gradual, expected, &ok) && ok)
        return prime_he_judged(
            PRIME_HE_JUDGED_ESTABLISHED,
            prime_expr3(a, "ConsistencyEvidence", prime_sym(a, "dynamic"),
                        prime_expr3(a, "AGTConsistent", gradual, expected)));
    return prime_he_judged(PRIME_HE_JUDGED_UNDETERMINED,
                           prime_he_undetermined_reason(a, &c));
}

/* Formation of a declared type expression: some derivation gives the
 * expression type `Type`, reading Prime's primitive types as types of kind
 * `Type`, and the checker accepts it.  `dynamic_only` reports that its only
 * type is the unknown. */
static bool prime_he_fragment_forms_type(Space *space, Arena *a, Atom *type,
                                         PrimeResourceLedger *ledger,
                                         bool *dynamic_only) {
    PrimeHeCandidates c = prime_he_elaborate(space, a, type, true, false,
                                             ledger, PRIME_RESOURCE_FORMATION);
    if (dynamic_only) *dynamic_only = c.status == PRIME_HE_DYNAMIC;
    if (c.status != PRIME_HE_TYPED) return false;
    for (size_t i = 0u; i < c.derivation_len; i++) {
        const PrimeHeDerivation *d = &c.derivations[i];
        if (d->rigid || !is_symbol_named(d->type, "Type")) continue;
        Atom *reason = NULL;
        if (prime_he_check_derivation(a, d->annotation, d->type, d->dependent,
                                      &reason) == PRIME_HOL_ACCEPTED)
            return true;
    }
    return false;
}

static bool prime_he_refinement_free(Space *space, Atom *type, bool *ok) {
    CettaCount space_len = space_view_length64(space);
    PrimeHeAtomStack heads = {0};
    *ok = true;
    for (CettaIndex i = 0; *ok && i < space_len; i++) {
        Atom *row = space_view_get_at64(space, i);
        if (row && row->kind == ATOM_EXPR && row->expr.len == 4u &&
            is_symbol_named(row->expr.elems[0], "type-index-refinement"))
            *ok = prime_he_stack_push(&heads, row->expr.elems[1]);
    }
    if (!*ok || heads.len == 0u) {
        free(heads.items);
        return *ok;
    }
    PrimeHeAtomStack stack = {0};
    bool result = true;
    *ok = prime_he_stack_push(&stack, type);
    while (*ok && result && stack.len > 0u) {
        Atom *atom = stack.items[--stack.len];
        if (atom->kind != ATOM_EXPR || atom->expr.len == 0u) continue;
        for (size_t h = 0u; h < heads.len; h++)
            if (atom_eq(heads.items[h], atom->expr.elems[0])) result = false;
        for (CettaExprIndex i = 0u; *ok && i < atom->expr.len; i++)
            *ok = prime_he_stack_push(&stack, atom->expr.elems[i]);
    }
    free(heads.items);
    free(stack.items);
    return *ok && result;
}

/* The public verdict of a judgment on HE-style declarations. */
static Atom *prime_he_verdict(Arena *a, Atom *judgment,
                              const PrimeHeJudgment *judged) {
    switch (judged->status) {
    case PRIME_HE_JUDGED_ESTABLISHED:
        return prime_established(a, judgment, judged->detail);
    case PRIME_HE_JUDGED_REFUTED:
        return prime_refuted(a, judgment, judged->detail);
    default:
        return prime_undetermined(
            a, judgment,
            judged->detail ? judged->detail
                           : prime_expr1(a, "typing-boundary-undetermined"));
    }
}

/* ---- Services for get-type and for applicability pruning -------------- */

/* The gradual type of an application through a first-order scheme
 * `(-> B1 ... Bn R)` (Lean: HOLContract.GradualSchemeApplication): some
 * first-order valuation of the scheme's variables makes every argument's
 * type consistent with its parameter.  A typed argument's ground
 * first-order type is unified with its parameter; an argument that nothing
 * types is consistent with every ground parameter (Lean:
 * HOLContract.undefined_consistent) and constrains nothing.  The return type
 * must come out ground: a variable that only an untyped argument meets
 * leaves the type open, and the rule does not apply.  An arrow-typed or
 * open argument type leaves the first-order fragment.  NULL when the rule
 * does not apply; nothing is refuted here. */
static Atom *prime_he_gradual_scheme_application_type(Space *space, Arena *a,
                                                      Atom *term,
                                                      Atom *arrow) {
    bool ok = true;
    if (!prime_he_operator_scheme(arrow, &ok) || !ok) return NULL;
    Atom *scheme = prime_he_rename_apart(a, arrow, &ok);
    if (!ok) return NULL;
    PrimeHeSubst subst = {0};
    bool applies = true;
    for (CettaExprIndex i = 1u; applies && ok && i < term->expr.len; i++) {
        PrimeHeJudgment argument =
            prime_he_synthesize(space, a, term->expr.elems[i], true, NULL);
        if (argument.status == PRIME_HE_JUDGED_UNDETERMINED && argument.unknown)
            continue;
        applies = argument.status == PRIME_HE_JUDGED_ESTABLISHED &&
                  argument.type && !atom_has_vars(argument.type) &&
                  prime_he_first_order(argument.type, &ok) && ok &&
                  prime_he_unify(&subst, scheme->expr.elems[i], argument.type,
                                 &ok);
    }
    Atom *returned = applies && ok
        ? prime_he_subst_resolve(a, &subst,
                                 scheme->expr.elems[scheme->expr.len - 1u], &ok)
        : NULL;
    free(subst.items);
    if (!returned || !ok || atom_has_vars(returned) ||
        !prime_he_ground_contract_type(returned, &ok) || !ok ||
        prime_he_arrow_shaped(returned))
        return NULL;
    return returned;
}

/* The gradual type of an application outside the static fragments, as
 * Abstracting Gradual Typing lifts the application rule: the head is a
 * symbol with one arrow declaration of matching arity, and each argument's
 * type is consistent with its parameter type at the top of a match (Lean:
 * HOLContract.GradualApplication,
 * TypeGrounding.Gradual.corePlusR2Consistent_iff_top).  An argument's type
 * is the type the checker accepts for it, or the unknown type when nothing
 * types it.  A variable-free declaration needs a static return; a parameter
 * `Atom` or `%Undefined%` accepts every argument, and a meta-type parameter
 * reads the argument's shape, not its type, and leaves the rule.  An open
 * declaration is read as a first-order scheme.  NULL when the rule does not
 * apply; nothing is refuted here. */
static Atom *prime_he_gradual_application_type(Space *space, Arena *a,
                                               Atom *term) {
    if (term->kind != ATOM_EXPR || term->expr.len < 2u ||
        term->expr.elems[0]->kind != ATOM_SYMBOL)
        return NULL;
    /* A declaration of the expression itself gives it another type, which
     * the gradual type would hide. */
    Atom **annotated = NULL;
    uint32_t annotations = space_get_declared_types(space, a, term, &annotated);
    free(annotated);
    if (annotations != 0u) return NULL;
    Atom **declared = NULL;
    uint32_t count = prime_he_declared_types(space, a, term->expr.elems[0],
                                              &declared);
    Atom *arrow = count == 1u ? declared[0] : NULL;
    free(declared);
    bool ok = true;
    if (!arrow || !prime_he_arrow_shaped(arrow) ||
        prime_he_telescope(arrow) || prime_he_native_binder(arrow) ||
        arrow->expr.len != term->expr.len + 1u)
        return NULL;
    if (atom_has_vars(arrow))
        return prime_he_gradual_scheme_application_type(space, a, term, arrow);
    Atom *returned = arrow->expr.elems[arrow->expr.len - 1u];
    if (!prime_he_ground_contract_type(returned, &ok) || !ok ||
        prime_he_arrow_shaped(returned))
        return NULL;
    for (CettaExprIndex i = 1u; i < term->expr.len; i++) {
        Atom *parameter = arrow->expr.elems[i];
        if (is_symbol_named(parameter, "Atom") ||
            atom_is_symbol_id(parameter, g_builtin_syms.undefined_type))
            continue;
        if (prime_he_symbol_in(parameter, PRIME_HE_META_TYPE_NAMES))
            return NULL;
        PrimeHeJudgment argument =
            prime_he_synthesize(space, a, term->expr.elems[i], true, NULL);
        Atom *actual = argument.status == PRIME_HE_JUDGED_ESTABLISHED
            ? argument.type
            : argument.status == PRIME_HE_JUDGED_UNDETERMINED && argument.unknown
                ? atom_undefined_type(a) : NULL;
        if (!actual || atom_has_vars(actual) ||
            !prime_he_agt_consistent(parameter, actual, &ok) || !ok)
            return NULL;
    }
    return returned;
}


/* The types of an atom from its HE-style declarations, as Prime's
 * `get-type` answers them: one per derivation, each accepted by the checker,
 * in HE's order. */
CettaPrimeHeTypeAnswer cetta_prime_he_declaration_types(Space *space, Arena *a,
                                                        Atom *term,
                                                        Atom ***types_out,
                                                        uint32_t *count_out) {
    *types_out = NULL;
    *count_out = 0u;
    if (!space || !a || !term) return CETTA_PRIME_HE_TYPE_UNDETERMINED;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_PRIME_HE_FRAGMENT_GET_TYPE);
    PrimeHeJudgment judged = prime_he_synthesize(space, a, term, true, NULL);
    if (judged.status == PRIME_HE_JUDGED_ESTABLISHED &&
        judged.type_count <= UINT32_MAX) {
        *types_out = judged.types;
        *count_out = (uint32_t)judged.type_count;
        return CETTA_PRIME_HE_TYPE_CHECKED;
    }
    if (judged.status == PRIME_HE_JUDGED_REFUTED)
        return CETTA_PRIME_HE_TYPE_NONE;
    if (judged.unknown)
        return CETTA_PRIME_HE_TYPE_UNKNOWN;
    Atom *gradual = prime_he_gradual_application_type(space, a, term);
    if (gradual) {
        Atom **single = arena_alloc(a, sizeof(Atom *));
        single[0] = gradual;
        *types_out = single;
        *count_out = 1u;
        return CETTA_PRIME_HE_TYPE_GRADUAL;
    }
    if (term->kind == ATOM_SYMBOL) {
        Atom **declared = NULL;
        uint32_t count = space_get_declared_types(space, a, term, &declared);
        bool open = false, ok = true;
        for (uint32_t i = 0u; ok && !open && i < count; i++)
            open = prime_he_subtype_reaches(space, a, declared[i], &ok);
        free(declared);
        if (open || !ok) return CETTA_PRIME_HE_TYPE_OPEN;
    }
    return CETTA_PRIME_HE_TYPE_UNDETERMINED;
}

bool cetta_prime_he_argument_refuted(Space *space, Arena *a, Atom *argument,
                                     Atom *expected) {
    if (!space || !a || !argument || !expected) return false;
    /* Only a typed argument prunes: one without a derivation is HE's
     * `%Undefined%` fallback, which every parameter type accepts. */
    PrimeHeCandidates c = prime_he_elaborate(space, a, argument, false, false,
                                             NULL, PRIME_RESOURCE_CHECKING);
    bool ok = true;
    if (c.status != PRIME_HE_TYPED ||
        !prime_he_ground_contract_type(expected, &ok) || !ok)
        return false;
    PrimeHeJudgment judged = prime_he_check_static(
        a, &c, argument, expected, prime_he_checked_evidence);
    return judged.status == PRIME_HE_JUDGED_REFUTED;
}

#undef PRIME_HE_GROW

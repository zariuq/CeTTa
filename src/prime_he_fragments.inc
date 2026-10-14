/* prime_he_fragments.inc - HE-style declarations, answered through the HOL
 * contract and decided by the hosted HOL Light checker.
 *
 * Prime answers `type:of`, `type:check`, `type:analyze`, `type:formed` and
 * `get-type` on HE-style declarations `(: name type)` without HE's type
 * engine.  HE's coherent type fragments mean a stable HOL contract, which
 * depends on no Prime candidate (Lean: Mettapedia.Languages.MeTTa.HE.
 * HOLContract).  For a judgment inside a fragment this file
 *
 *  1. elaborates it: the HE type of every node and, at every constant
 *     occurrence, the instance of its declared type that the occurrence
 *     takes (first-order unification, each occurrence of a declaration
 *     renamed apart);
 *  2. translates the elaborated judgment into the hosted HOL Light checker:
 *     an HE base type is a nullary type operator, an arrow `(-> A1 ... An R)`
 *     the curried `fun`, a compound non-arrow type `(T0 T1 ... Tn)` the
 *     product `T0 # (T1 # ... # (Tn # 1))`, a declaration a constant of its
 *     translated type, a saturated application a curried `Comb`, and a tuple
 *     `(x0 x1 ... xn)` the pair `x0, (x1, ... (xn, one))`;
 *  3. asks the checker.  An answer is established only when the checker
 *     reads the translated term and computes the translated type.  The
 *     checker reads every constant occurrence against the declared type of
 *     its constant, and the type it computes is the contract type of the
 *     elaborated term (Lean: HOLContract.Guest.guestTyped_iff).
 *
 * The translation is faithful only where HE's types read faithfully: an
 * arrow whose return is an arrow, at any depth, has the curried reading of
 * an arrow of another HE arity, and is not translated.
 *
 * The fragments, with their agreement theorems:
 *
 *  - Static arrows.  A static type is a variable-free non-arrow atom that is
 *    not a reserved type name and mentions no `%Undefined%`, or an arrow
 *    `(-> A1 ... An R)` with n >= 1, static components and a non-arrow
 *    return.  A constant has the type of its single static declaration (a
 *    literal its intrinsic type); a saturated application of a declared
 *    static arrow has its return type when each argument has its argument
 *    type (Lean: HOLContract.static_iff_exact).
 *  - First-order schemes.  A first-order type mentions no reserved type name
 *    at any depth; type variables may occur anywhere.  A saturated
 *    application of a symbol whose one declaration is `(-> B1 ... Bn R)`,
 *    n >= 1, with first-order components has the most general instance of
 *    `R` that unifies each `Bi` with the type of the i-th argument.  Type
 *    variables are those of polymorphic HOL constants, and HE's candidate is
 *    the principal type scheme (Lean: HOLContract.schematic_iff_exact).
 *    An arrow-typed argument is outside: HE compares arrow arities where a
 *    HOL type variable may stand for a curried arrow.  Prime reads an open
 *    declaration as a scheme only when `(type-scheme name)` admits it; the
 *    variables of any other open declaration are rigid and never solved.
 *    A telescope component `(: $x A)` whose variable occurs in no later
 *    component binds nothing and is read as its domain `A`: a dependent
 *    arrow whose variable is unused is the simple arrow (Lean:
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

typedef struct {
    PrimeHeFragmentStatus status;
    /* PRIME_HE_TYPED: the type.  In the schematic fragment it may mention
     * variables bound in the elaboration's substitution. */
    Atom *type;
    /* PRIME_HE_TYPED: the elaborated term, one node per term node:
     * `(HeConst type atom declared)`, `(HeApp type head arg ...)` and
     * `(HeTuple type head element ...)`. */
    Atom *annotation;
    /* PRIME_HE_REJECTED and PRIME_HE_OUTSIDE: the reason. */
    Atom *reason;
    /* PRIME_HE_TYPED: a tuple node occurred in the derivation. */
    bool tuples;
    /* PRIME_HE_TYPED: the type is a first-order scheme. */
    bool schematic;
    /* PRIME_HE_TYPED with `schematic`: the scheme is an operator arrow, which
     * types a constant on its own but never an argument. */
    bool operator_scheme;
    /* PRIME_HE_TYPED with `schematic`: the declared type of a constant whose
     * open declaration no `type-scheme` admits; its variables are rigid. */
    bool rigid;
} PrimeHeFragmentResult;

typedef enum {
    PRIME_HE_STATIC = 0,
    PRIME_HE_STATIC_WITH_KINDS,
    PRIME_HE_SCHEMATIC
} PrimeHeFragmentMode;

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

static PrimeHeFragmentResult prime_he_typed(Atom *type, Atom *annotation,
                                            bool tuples) {
    return (PrimeHeFragmentResult){
        .status = PRIME_HE_TYPED, .type = type, .annotation = annotation,
        .tuples = tuples};
}

static PrimeHeFragmentResult prime_he_scheme(Atom *type, Atom *annotation,
                                             bool operator_scheme) {
    return (PrimeHeFragmentResult){
        .status = PRIME_HE_TYPED, .type = type, .annotation = annotation,
        .schematic = true, .operator_scheme = operator_scheme};
}

static PrimeHeFragmentResult prime_he_reason(PrimeHeFragmentStatus status,
                                             Atom *reason) {
    return (PrimeHeFragmentResult){.status = status, .reason = reason};
}

/* A term inside a fragment that the fragment's rules do not type, with the
 * elaborated term the checker is asked to refuse. */
static PrimeHeFragmentResult prime_he_rejected(Atom *reason, Atom *annotation) {
    return (PrimeHeFragmentResult){
        .status = PRIME_HE_REJECTED, .reason = reason, .annotation = annotation};
}

static PrimeHeFragmentResult prime_he_dynamic(void) {
    return (PrimeHeFragmentResult){.status = PRIME_HE_DYNAMIC};
}

static PrimeHeFragmentResult prime_he_allocation_failure(Arena *a) {
    return prime_he_reason(PRIME_HE_OUTSIDE,
                           prime_expr1(a, "fragment-traversal-allocation"));
}

/* The elaborated constant occurrence `(HeConst type atom declared)`. */
static Atom *prime_he_constant_node(Arena *a, Atom *type, Atom *atom,
                                    Atom *declared) {
    Atom *items[4] = {prime_sym(a, "HeConst"), type, atom, declared};
    return atom_expr(a, items, 4);
}

/* `(HeApp type head arg ...)` or `(HeTuple type head element ...)`. */
static Atom *prime_he_compound_node(Arena *a, const char *head, Atom *type,
                                    const PrimeHeFragmentResult *elements,
                                    CettaExprLen count) {
    Atom **items = arena_alloc(a, sizeof(Atom *) * ((size_t)count + 2u));
    items[0] = prime_sym(a, head);
    items[1] = type;
    for (CettaExprIndex i = 0u; i < count; i++)
        items[i + 2u] = elements[i].annotation;
    return atom_expr(a, items, count + 2u);
}

/* A growable stack of atoms, used by the worklist traversals below. */
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

/* No type variable and no `%Undefined%` occurs anywhere in the atom.  The
 * second test is `UndefinedFree`, the first `typeVars = []`. */
static bool prime_he_ground_and_undefined_free(Atom *root, bool *ok) {
    PrimeHeAtomStack stack = {0};
    bool result = true;
    *ok = prime_he_stack_push(&stack, root);
    while (*ok && result && stack.len > 0u) {
        Atom *atom = stack.items[--stack.len];
        if (atom->kind == ATOM_VAR) {
            result = false;
        } else if (atom->kind == ATOM_SYMBOL) {
            if (atom_is_symbol_id(atom, g_builtin_syms.undefined_type))
                result = false;
        } else if (atom->kind == ATOM_EXPR) {
            for (CettaExprIndex i = 0u; *ok && i < atom->expr.len; i++)
                *ok = prime_he_stack_push(&stack, atom->expr.elems[i]);
        }
    }
    free(stack.items);
    return *ok && result;
}

/* `StaticType`: arrows `(-> A1 ... An R)` with n >= 1, static arguments and
 * a static non-arrow return; bases that are not arrow-shaped, not a reserved
 * type name, variable-free and `%Undefined%`-free. */
static bool prime_he_static_type(Atom *root, bool *ok) {
    PrimeHeAtomStack stack = {0};
    bool result = true;
    *ok = prime_he_stack_push(&stack, root);
    while (*ok && result && stack.len > 0u) {
        Atom *type = stack.items[--stack.len];
        if (prime_he_arrow_shaped(type)) {
            if (type->expr.len < 3u) {
                result = false;
                break;
            }
            Atom *returned = type->expr.elems[type->expr.len - 1u];
            if (prime_he_arrow_shaped(returned)) {
                result = false;
                break;
            }
            for (CettaExprIndex i = 1u; *ok && i < type->expr.len; i++)
                *ok = prime_he_stack_push(&stack, type->expr.elems[i]);
        } else {
            if (prime_he_symbol_in(type, PRIME_HE_RESERVED_TYPE_NAMES)) {
                result = false;
                break;
            }
            bool scan_ok = true;
            result = prime_he_ground_and_undefined_free(type, &scan_ok);
            if (!scan_ok) *ok = false;
        }
    }
    free(stack.items);
    return *ok && result;
}

/* First-order types: no reserved type name occurs at any depth, so no
 * arrow, no `%Undefined%` and no meta-type; type variables may occur
 * anywhere (Lean: TypeGrounding.Schematic.ReservedFree). */
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

/* `instance` is a ground first-order instance of `scheme`: a substitution
 * of ground first-order types for the variables of `scheme` (Lean:
 * TypeGrounding.Schematic.FirstOrderValuation). */
static bool prime_he_first_order_instance(Atom *scheme, Atom *instance,
                                          bool *ok) {
    *ok = true;
    if (atom_has_vars(instance)) return false;
    PrimeHeSubst subst = {0};
    bool matched = prime_he_unify(&subst, scheme, instance, ok);
    for (size_t i = 0u; *ok && matched && i < subst.len; i++)
        if (!prime_he_first_order(subst.items[i].value, ok)) matched = false;
    free(subst.items);
    return *ok && matched;
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
 * component is read as its domain `A`.  Any other telescope component stays,
 * and with it the dependent telescope.  NULL with `*ok` false when memory
 * runs out. */
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
    return read;
}

/* The declared types of `term`, each read without its vacuous binders. */
static uint32_t prime_he_declared_types(Space *space, Arena *a, Atom *term,
                                        Atom ***types_out) {
    uint32_t count = space_get_declared_types(space, a, term, types_out);
    for (uint32_t i = 0u; i < count; i++) {
        bool ok = true;
        Atom *read = prime_he_without_vacuous_binders(a, (*types_out)[i], &ok);
        if (ok) (*types_out)[i] = read;
    }
    return count;
}

/* ---- The static fragments ---------------------------------------------- */

/* The type of a leaf: the single declaration of a symbol, the intrinsic type
 * of a grounded value, the unknown type of a variable or of `()`.  The
 * grounded values of the contract have symbol types. */
static PrimeHeFragmentResult prime_he_leaf_type(Space *space, Arena *a,
                                                Atom *term,
                                                bool primitive_kinds) {
    bool ok = true;
    if (term->kind == ATOM_VAR) return prime_he_dynamic();
    if (term->kind == ATOM_EXPR) return prime_he_dynamic();
    if (term->kind == ATOM_GROUNDED) {
        Atom *type = get_grounded_type(a, term);
        if (!type || atom_is_symbol_id(type, g_builtin_syms.undefined_type))
            return prime_he_dynamic();
        if (type->kind == ATOM_SYMBOL && prime_he_static_type(type, &ok))
            return prime_he_typed(
                type, prime_he_constant_node(a, type, term, type), false);
        return ok ? prime_he_reason(PRIME_HE_OUTSIDE,
                                    prime_expr2(a, "grounded-type-outside-fragments",
                                                type))
                  : prime_he_allocation_failure(a);
    }
    Atom **declared = NULL;
    uint32_t count = prime_he_declared_types(space, a, term, &declared);
    Atom *type = count == 1u ? declared[0] : NULL;
    free(declared);
    if (count == 0u) {
        if (primitive_kinds &&
            prime_he_symbol_in(term, PRIME_HE_PRIMITIVE_KIND_NAMES)) {
            Atom *kind = prime_sym(a, "Type");
            return prime_he_typed(
                kind, prime_he_constant_node(a, kind, term, kind), false);
        }
        return prime_he_dynamic();
    }
    if (count > 1u)
        return prime_he_reason(
            PRIME_HE_OUTSIDE,
            prime_expr3(a, "multiple-declarations", term, atom_int(a, count)));
    if (atom_is_symbol_id(type, g_builtin_syms.undefined_type))
        return prime_he_dynamic();
    if (prime_he_telescope(type))
        return prime_he_reason(
            PRIME_HE_OUTSIDE, prime_expr3(a, "dependent-telescope", term, type));
    if (prime_he_static_type(type, &ok))
        return prime_he_typed(
            type, prime_he_constant_node(a, type, term, type), false);
    return ok ? prime_he_reason(PRIME_HE_OUTSIDE,
                                prime_expr3(a, "declared-type-outside-fragments",
                                            term, type))
              : prime_he_allocation_failure(a);
}

/* The checks every fragment makes before typing an application: a direct
 * annotation of the expression and the `StateValue` wrapper are other
 * rules of HE.  Returns false with `*outside` set when one applies. */
static bool prime_he_application_admitted(Space *space, Arena *a, Atom *term,
                                          PrimeHeFragmentResult *outside) {
    Atom **declared = NULL;
    uint32_t count = space_get_declared_types(space, a, term, &declared);
    free(declared);
    if (count != 0u) {
        *outside = prime_he_reason(PRIME_HE_OUTSIDE,
                                   prime_expr2(a, "expression-annotation", term));
        return false;
    }
    if (is_symbol_named(term->expr.elems[0], "StateValue")) {
        *outside = prime_he_reason(PRIME_HE_OUTSIDE,
                                   prime_expr2(a, "state-value-wrapper", term));
        return false;
    }
    return true;
}

/* Combine the results of the elements of a nonempty expression. */
static PrimeHeFragmentResult prime_he_expression_type(
    Space *space, Arena *a, Atom *term, const PrimeHeFragmentResult *elements) {
    PrimeHeFragmentResult outside;
    if (!prime_he_application_admitted(space, a, term, &outside)) return outside;
    Atom *head = term->expr.elems[0];
    const PrimeHeFragmentResult *head_result = &elements[0];
    if (head_result->status == PRIME_HE_DYNAMIC) return prime_he_dynamic();
    if (head_result->status != PRIME_HE_TYPED) return *head_result;
    CettaExprIndex arguments = term->expr.len - 1u;
    bool tuples = head_result->tuples;
    for (CettaExprIndex i = 1u; i <= arguments; i++) {
        const PrimeHeFragmentResult *argument = &elements[i];
        if (argument->status == PRIME_HE_OUTSIDE) return *argument;
        if (argument->status == PRIME_HE_DYNAMIC)
            return prime_he_reason(
                PRIME_HE_OUTSIDE,
                prime_expr2(a, "gradual-subterm", term->expr.elems[i]));
    }
    for (CettaExprIndex i = 1u; i <= arguments; i++) {
        if (elements[i].status == PRIME_HE_REJECTED) return elements[i];
        tuples = tuples || elements[i].tuples;
    }
    Atom *head_type = head_result->type;
    if (prime_he_arrow_shaped(head_type)) {
        /* Saturated application of a declared static arrow: the HOL
         * application rule. */
        if (head->kind != ATOM_SYMBOL)
            return prime_he_reason(
                PRIME_HE_OUTSIDE, prime_expr2(a, "applied-non-symbol", term));
        CettaExprIndex expected = head_type->expr.len - 2u;
        if (expected != arguments)
            return prime_he_reason(
                PRIME_HE_OUTSIDE,
                prime_expr2(a, "unsaturated-application", term));
        for (CettaExprIndex i = 1u; i <= arguments; i++) {
            if (!atom_eq(elements[i].type, head_type->expr.elems[i])) {
                Atom *items[4] = {
                    prime_sym(a, "argument-type"), atom_int(a, (int64_t)i),
                    head_type->expr.elems[i], elements[i].type};
                Atom *returned = head_type->expr.elems[head_type->expr.len - 1u];
                return prime_he_rejected(
                    atom_expr(a, items, 4),
                    prime_he_compound_node(a, "HeApp", returned, elements,
                                           term->expr.len));
            }
        }
        Atom *returned = head_type->expr.elems[head_type->expr.len - 1u];
        return prime_he_typed(
            returned,
            prime_he_compound_node(a, "HeApp", returned, elements,
                                   term->expr.len),
            tuples);
    }
    /* A head of non-arrow static type: the tuple of the elements' types,
     * HOL product introduction.  A one-element expression is outside. */
    if (arguments == 0u)
        return prime_he_reason(PRIME_HE_OUTSIDE,
                               prime_expr2(a, "one-element-expression", term));
    Atom **components = arena_alloc(a, sizeof(Atom *) * (size_t)term->expr.len);
    for (CettaExprIndex i = 0u; i < term->expr.len; i++)
        components[i] = elements[i].type;
    Atom *tuple_type = atom_expr(a, components, term->expr.len);
    return prime_he_typed(
        tuple_type,
        prime_he_compound_node(a, "HeTuple", tuple_type, elements,
                               term->expr.len),
        true);
}

/* ---- The first-order schematic fragment -------------------------------- */

/* A leaf: a constant with one first-order declaration or with one operator
 * scheme, renamed apart when it is read as a scheme and rigid otherwise; a
 * literal with its symbol type; otherwise the unknown type or outside.
 * Prime's judgments read an open declaration as a scheme only when
 * `(type-scheme name)` marks it; `get-type`, HE's relation, reads every
 * declaration as one, as the contract does (`all_schemes`). */
static PrimeHeFragmentResult prime_he_schematic_leaf(Space *space, Arena *a,
                                                     Atom *term,
                                                     bool all_schemes) {
    bool ok = true;
    if (term->kind == ATOM_VAR || term->kind == ATOM_EXPR)
        return prime_he_dynamic();
    if (term->kind == ATOM_GROUNDED) {
        Atom *type = get_grounded_type(a, term);
        if (!type || atom_is_symbol_id(type, g_builtin_syms.undefined_type))
            return prime_he_dynamic();
        if (type->kind == ATOM_SYMBOL && prime_he_first_order(type, &ok))
            return prime_he_scheme(
                type, prime_he_constant_node(a, type, term, type), false);
        return ok ? prime_he_reason(PRIME_HE_OUTSIDE,
                                    prime_expr2(a, "grounded-type-outside-fragments",
                                                type))
                  : prime_he_allocation_failure(a);
    }
    Atom **declared = NULL;
    uint32_t count = prime_he_declared_types(space, a, term, &declared);
    Atom *type = count == 1u ? declared[0] : NULL;
    free(declared);
    if (count == 0u) return prime_he_dynamic();
    if (count > 1u)
        return prime_he_reason(
            PRIME_HE_OUTSIDE,
            prime_expr3(a, "multiple-declarations", term, atom_int(a, count)));
    if (atom_is_symbol_id(type, g_builtin_syms.undefined_type))
        return prime_he_dynamic();
    if (prime_he_telescope(type))
        return prime_he_reason(
            PRIME_HE_OUTSIDE, prime_expr3(a, "dependent-telescope", term, type));
    bool operator_scheme = false;
    if (!prime_he_first_order(type, &ok)) {
        if (!ok) return prime_he_allocation_failure(a);
        operator_scheme = prime_he_operator_scheme(type, &ok);
        if (!ok) return prime_he_allocation_failure(a);
        if (!operator_scheme)
            return prime_he_reason(
                PRIME_HE_OUTSIDE,
                prime_expr3(a, "declared-type-outside-fragments", term, type));
    }
    /* An open declaration that no `type-scheme` admits keeps its rigid
     * variables: the declared type itself, never renamed or solved. */
    if (atom_has_vars(type) && !all_schemes &&
        !prime_he_scheme_admitted(space, term)) {
        PrimeHeFragmentResult rigid = prime_he_scheme(
            type, prime_he_constant_node(a, type, term, type), operator_scheme);
        rigid.rigid = true;
        return rigid;
    }
    Atom *renamed = prime_he_rename_apart(a, type, &ok);
    if (!ok) return prime_he_allocation_failure(a);
    return prime_he_scheme(
        renamed, prime_he_constant_node(a, renamed, term, type),
        operator_scheme);
}

/* A saturated application of an operator scheme to first-order arguments:
 * the arguments' schemes unified with the argument types in the
 * elaboration's substitution, and the return type under it. */
static PrimeHeFragmentResult prime_he_schematic_expression(
    Space *space, Arena *a, Atom *term, const PrimeHeFragmentResult *elements,
    PrimeHeSubst *subst) {
    PrimeHeFragmentResult outside;
    if (!prime_he_application_admitted(space, a, term, &outside)) return outside;
    const PrimeHeFragmentResult *head_result = &elements[0];
    if (head_result->status == PRIME_HE_DYNAMIC) return prime_he_dynamic();
    if (head_result->status != PRIME_HE_TYPED) return *head_result;
    if (head_result->rigid)
        return prime_he_reason(
            PRIME_HE_OUTSIDE,
            prime_expr2(a, "rigid-declaration-variables", term->expr.elems[0]));
    if (!head_result->operator_scheme ||
        term->expr.elems[0]->kind != ATOM_SYMBOL)
        return prime_he_reason(PRIME_HE_OUTSIDE,
                               prime_expr2(a, "not-an-operator-application",
                                           term));
    CettaExprIndex arguments = term->expr.len - 1u;
    for (CettaExprIndex i = 1u; i <= arguments; i++) {
        const PrimeHeFragmentResult *argument = &elements[i];
        if (argument->status == PRIME_HE_OUTSIDE) return *argument;
        if (argument->status == PRIME_HE_DYNAMIC)
            return prime_he_reason(
                PRIME_HE_OUTSIDE,
                prime_expr2(a, "gradual-subterm", term->expr.elems[i]));
    }
    for (CettaExprIndex i = 1u; i <= arguments; i++) {
        if (elements[i].status == PRIME_HE_REJECTED) return elements[i];
        if (elements[i].rigid)
            return prime_he_reason(
                PRIME_HE_OUTSIDE,
                prime_expr2(a, "rigid-declaration-variables", term->expr.elems[i]));
        /* An arrow-typed argument leaves the first-order fragment. */
        if (elements[i].operator_scheme)
            return prime_he_reason(
                PRIME_HE_OUTSIDE,
                prime_expr2(a, "arrow-typed-argument", term->expr.elems[i]));
    }
    Atom *scheme = head_result->type;
    if ((CettaExprIndex)(scheme->expr.len - 2u) != arguments)
        return prime_he_reason(PRIME_HE_OUTSIDE,
                               prime_expr2(a, "unsaturated-application", term));
    bool ok = true;
    for (CettaExprIndex i = 1u; i <= arguments; i++) {
        if (!prime_he_unify(subst, scheme->expr.elems[i], elements[i].type,
                            &ok)) {
            if (!ok) return prime_he_allocation_failure(a);
            Atom *items[4] = {
                prime_sym(a, "argument-scheme"), atom_int(a, (int64_t)i),
                prime_he_subst_resolve(a, subst, scheme->expr.elems[i], &ok),
                prime_he_subst_resolve(a, subst, elements[i].type, &ok)};
            if (!ok) return prime_he_allocation_failure(a);
            Atom *returned = scheme->expr.elems[scheme->expr.len - 1u];
            return prime_he_rejected(
                atom_expr(a, items, 4),
                prime_he_compound_node(a, "HeApp", returned, elements,
                                       term->expr.len));
        }
    }
    Atom *returned = scheme->expr.elems[scheme->expr.len - 1u];
    return prime_he_scheme(
        returned,
        prime_he_compound_node(a, "HeApp", returned, elements, term->expr.len),
        false);
}

/* ---- Traversal --------------------------------------------------------- */

/* The type a fragment gives `term`, computed bottom-up with explicit
 * stacks.  The static mode with kinds reads Prime's primitive types as
 * constants of type `Type`; formation uses it.  The schematic mode binds
 * type variables in `subst`. */
static PrimeHeFragmentResult prime_he_fragment_type(Space *space, Arena *a,
                                                    Atom *term,
                                                    PrimeHeFragmentMode mode,
                                                    bool all_schemes,
                                                    PrimeHeSubst *subst,
                                                    uint64_t *visited) {
    PrimeHeFrame *frames = NULL;
    size_t frame_len = 0u, frame_cap = 0u;
    PrimeHeFragmentResult *results = NULL;
    size_t result_len = 0u, result_cap = 0u;
    PrimeHeFragmentResult answer = prime_he_allocation_failure(a);
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
        PrimeHeFragmentResult result;
        if (mode == PRIME_HE_SCHEMATIC)
            result = expression
                ? prime_he_schematic_expression(space, a, current,
                                                results + frame->first_result,
                                                subst)
                : prime_he_schematic_leaf(space, a, current, all_schemes);
        else
            result = expression
                ? prime_he_expression_type(space, a, current,
                                           results + frame->first_result)
                : prime_he_leaf_type(space, a, current,
                                     mode == PRIME_HE_STATIC_WITH_KINDS);
        if (visited) (*visited)++;
        result_len = expression ? frame->first_result : result_len;
        frame_len--;
        PRIME_HE_GROW(results, result_len, result_cap, PrimeHeFragmentResult,
                      ok);
        if (!ok) break;
        results[result_len++] = result;
    }
    if (ok && result_len == 1u) answer = results[0];
    free(frames);
    free(results);
    return answer;
}

/* The static fragments first; outside them, for synthesis and checking, the
 * first-order schematic fragment, whose type variables are bound in
 * `subst` (owned by the caller).  The traversal is counted as observed work
 * without failing it: finite structural typing is never failed by a
 * producer budget. */
static PrimeHeFragmentResult prime_he_fragment_type_observed(
    Space *space, Arena *a, Atom *term, bool primitive_kinds, bool all_schemes,
    PrimeResourceLedger *ledger, PrimeHeSubst *subst) {
    uint64_t visited = 0u;
    PrimeHeFragmentResult result = prime_he_fragment_type(
        space, a, term,
        primitive_kinds ? PRIME_HE_STATIC_WITH_KINDS : PRIME_HE_STATIC,
        false, subst, &visited);
    if (result.status == PRIME_HE_OUTSIDE && !primitive_kinds) {
        subst->len = 0u;
        result = prime_he_fragment_type(space, a, term, PRIME_HE_SCHEMATIC,
                                        all_schemes, subst, &visited);
    }
    if (ledger && ledger->typing.steps_limited)
        ledger->typing.work_steps_observed =
            prime_u64_add_sat(ledger->typing.work_steps_observed, visited);
    return result;
}

static Atom *prime_he_fragment_name(Arena *a, const PrimeHeFragmentResult *r) {
    const char *name = r->schematic ? "first-order-schemes"
                     : r->tuples ? "tuples" : "static-arrows";
    return prime_expr2(a, "Fragment", prime_sym(a, name));
}

/* The reason an Undetermined answer gives for a non-typed result. */
static Atom *prime_he_undetermined_reason(Arena *a,
                                          const PrimeHeFragmentResult *r) {
    if (r->status == PRIME_HE_DYNAMIC) return prime_expr1(a, "dynamic-type-only");
    if (r->status == PRIME_HE_REJECTED)
        return prime_expr2(a, "outside-kernel-fragments",
                           prime_expr2(a, "grounded-fragment-rejects", r->reason));
    return prime_expr2(a, "outside-kernel-fragments", r->reason);
}

/* ---- Translation into the hosted HOL Light checker --------------------- */

/* HE names live in their own namespace of the checker, apart from HOL
 * Light's own type operators and constants (`bool`, `fun`, `prod`, `=`, `T`,
 * `,` ...) and from the classification theory of meta-types. */
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

static bool prime_hol_atoms_contains(const PrimeHolAtoms *list, Atom *atom) {
    for (size_t i = 0u; i < list->len; i++)
        if (atom_eq(list->items[i], atom)) return true;
    return false;
}

typedef struct {
    Arena *a;
    /* Names (string atoms) of the nullary type operators of HE base types. */
    PrimeHolAtoms operators;
    /* Constant names (string atoms) and their declared types, in pairs. */
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

static Atom *prime_hol_nil(Arena *a) { return prime_sym(a, "HOL:Nil"); }

static Atom *prime_hol_cons(Arena *a, Atom *first, Atom *rest) {
    return atom_expr3(a, prime_sym(a, "HOL:Cons"), first, rest);
}

static Atom *prime_hol_tyapp(Arena *a, const char *name, Atom *arguments) {
    return atom_expr3(a, prime_sym(a, "Tyapp"), atom_string(a, name), arguments);
}

static Atom *prime_hol_named_tyapp(Arena *a, Atom *name) {
    return atom_expr3(a, prime_sym(a, "Tyapp"), name, prime_hol_nil(a));
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

/* The checker type of a variable-free leaf type, recording its operator. */
static Atom *prime_hol_leaf_type(PrimeHolTranslation *tr, Atom *leaf) {
    Arena *a = tr->a;
    if (leaf->kind == ATOM_VAR) {
        char name[48];
        snprintf(name, sizeof(name), "v%llu", (unsigned long long)leaf->var_id);
        return atom_expr2(a, prime_sym(a, "Tyvar"), atom_string(a, name));
    }
    /* Below the top of a static type a reserved name is an ordinary symbol
     * (Lean: TypeGrounding.StaticType); only the unknown type never
     * translates.  Whether a whole type is static is decided before
     * translation. */
    if (atom_is_symbol_id(leaf, g_builtin_syms.undefined_type)) {
        prime_hol_fail(tr, prime_expr2(a, "unknown-type-in-translation", leaf));
        return NULL;
    }
    Atom *name = prime_hol_name(a, PRIME_HOL_NAME_PREFIX, leaf);
    if (!name) {
        prime_hol_fail(tr, prime_expr1(a, "checker-translation-allocation"));
        return NULL;
    }
    if (!prime_hol_atoms_contains(&tr->operators, name) &&
        !prime_hol_atoms_push(&tr->operators, name)) {
        prime_hol_fail(tr, prime_expr1(a, "checker-translation-allocation"));
        return NULL;
    }
    return prime_hol_named_tyapp(a, name);
}

/* The checker type of an HE type: a leaf a nullary operator, an arrow the
 * curried `fun`, a compound non-arrow type the right-nested product ending
 * in the unit type (`()` is the unit type itself).  An arrow whose return is
 * an arrow has no faithful reading and is not translated.  Bottom-up with
 * explicit stacks; arrow heads are not visited. */
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
            if (current->expr.len < 3u) {
                prime_hol_fail(tr, prime_expr2(a, "nullary-arrow", current));
            } else if (prime_he_arrow_shaped(
                           current->expr.elems[current->expr.len - 1u])) {
                /* `(-> A (-> B C))` and `(-> A B C)` have one curried
                 * reading but different HE arities. */
                prime_hol_fail(tr, prime_expr2(a, "curried-arrow-return", current));
            } else {
                Atom **parts = results + frame->first_result;
                size_t count = (size_t)current->expr.len - 1u;
                result = parts[count - 1u];
                for (size_t i = count - 1u; i-- > 0u;)
                    result = prime_hol_fun(a, parts[i], result);
            }
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

/* Record the constant `name` with its declared type, once. */
static void prime_hol_declare_constant(PrimeHolTranslation *tr, Atom *name,
                                       Atom *declared) {
    for (size_t i = 0u; i + 1u < tr->constants.len; i += 2u)
        if (atom_eq(tr->constants.items[i], name)) return;
    Atom *scheme = prime_hol_type(tr, declared);
    if (!tr->ok) return;
    if (!prime_hol_atoms_push(&tr->constants, name) ||
        !prime_hol_atoms_push(&tr->constants, scheme))
        prime_hol_fail(tr, prime_expr1(tr->a, "checker-translation-allocation"));
}

static Atom *prime_hol_const(Arena *a, Atom *name, Atom *type) {
    return atom_expr3(a, prime_sym(a, "Const"), name, type);
}

static Atom *prime_hol_comb(Arena *a, Atom *function, Atom *argument) {
    return atom_expr3(a, prime_sym(a, "Comb"), function, argument);
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
 * stacks.  Each constant occurrence is declared with its declared type and
 * read at its instance. */
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
        bool leaf = node->kind == ATOM_EXPR && node->expr.len == 4u &&
                    is_symbol_named(node->expr.elems[0], "HeConst");
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
            Atom *name = prime_hol_name(a, PRIME_HOL_NAME_PREFIX,
                                        node->expr.elems[2]);
            Atom *instance = name ? prime_hol_type(tr, node->expr.elems[1]) : NULL;
            if (!name)
                prime_hol_fail(tr, prime_expr1(a, "checker-translation-allocation"));
            if (tr->ok) prime_hol_declare_constant(tr, name, node->expr.elems[3]);
            if (tr->ok) result = prime_hol_const(a, name, instance);
        } else if (is_symbol_named(node->expr.elems[0], "HeApp")) {
            Atom **parts = results + frame->first_result;
            size_t count = (size_t)node->expr.len - 2u;
            result = parts[0];
            for (size_t i = 1u; i < count; i++)
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

/* The declaration commands of a translation: every HE base type, the pair
 * and unit signature when a product occurs, then every constant. */
static Atom *prime_hol_commands(PrimeHolTranslation *tr, Atom *tail) {
    Arena *a = tr->a;
    PrimeHolAtoms steps = {0};
    bool ok = true;
    for (size_t i = 0u; ok && i < tr->operators.len; i++)
        ok = prime_hol_atoms_push(&steps, prime_hol_step(
            a, "type", tr->operators.items[i],
            atom_expr3(a, prime_sym(a, "DECLARE_TYPE"), tr->operators.items[i],
                       atom_int(a, 0))));
    if (ok && tr->products) {
        Atom *alpha = atom_expr2(a, prime_sym(a, "Tyvar"), atom_string(a, "A"));
        Atom *beta = atom_expr2(a, prime_sym(a, "Tyvar"), atom_string(a, "B"));
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

/* Run the declaration commands under the commitments; then, when `fact` is
 * given, run that one command after them, or, when `term` is given, read it
 * and compare its computed type with `type`, all in the checker.  Refusing
 * the declarations says nothing about the judgment (the translation, not
 * the judgment, failed); refusing the fact, the term or its type is the
 * checker's verdict on the judgment. */
static PrimeHolVerdict prime_hol_decide(Arena *a, Atom *commitments,
                                        Atom *commands, Atom *fact, Atom *term,
                                        Atom *type, Atom **reason_out) {
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_PRIME_HOL_GUEST_REQUEST);
    Atom *answer = NULL;
    PrimeHolVerdict verdict = prime_hol_ask(
        a, atom_expr3(a, prime_sym(a, "hol-light:run"), commitments, commands),
        &answer, reason_out);
    Atom *state = NULL;
    if (verdict == PRIME_HOL_ACCEPTED && !prime_hol_some(answer, &state)) {
        verdict = PRIME_HOL_UNAVAILABLE;
        *reason_out = prime_expr2(a, "hol-checker-unavailable",
                                  prime_expr1(a, "declarations-refused"));
    }
    if (verdict == PRIME_HOL_ACCEPTED && fact) {
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
    if (verdict == PRIME_HOL_ACCEPTED && term) {
        verdict = prime_hol_ask(
            a, atom_expr3(a, prime_sym(a, "hol-light:read"), state, term),
            &answer, reason_out);
        if (verdict == PRIME_HOL_ACCEPTED && !prime_hol_some(answer, &value)) {
            verdict = PRIME_HOL_REFUSED;
            *reason_out = prime_expr2(a, "hol-checker-refuses",
                                      prime_expr1(a, "term"));
        }
    }
    if (verdict == PRIME_HOL_ACCEPTED && term) {
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

/* ---- Meta-types: the classification theory of atoms -------------------- */

static const char *prime_he_meta_class(Atom *term) {
    switch (term->kind) {
    case ATOM_SYMBOL: return "Symbol";
    case ATOM_VAR: return "Variable";
    case ATOM_GROUNDED: return "Grounded";
    default: return "Expression";
    }
}

/* Record a constant of the classification signature with its checker type,
 * once. */
static void prime_hol_declare_checker_constant(PrimeHolTranslation *tr,
                                               Atom *name, Atom *type) {
    for (size_t i = 0u; i + 1u < tr->constants.len; i += 2u)
        if (atom_eq(tr->constants.items[i], name)) return;
    if (!prime_hol_atoms_push(&tr->constants, name) ||
        !prime_hol_atoms_push(&tr->constants, type))
        prime_hol_fail(tr, prime_expr1(tr->a, "checker-translation-allocation"));
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
    Atom *atom_type = prime_hol_named_tyapp(a, atom_operator);
    if (!prime_hol_atoms_push(&tr.operators, atom_operator))
        prime_hol_fail(&tr, prime_expr1(a, "checker-translation-allocation"));
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

/* ---- Judgments --------------------------------------------------------- */

/* Inside the grounded fragments a check the hosted checker refuses is a
 * genuine refutation: the agreement theorems make the fragment's relation
 * the contract's typing, and the checker decides that typing.  Outside the
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
    /* Synthesis, established: the type. */
    Atom *type;
    /* Synthesis, undetermined: no declaration typed any part of the term. */
    bool unknown;
} PrimeHeJudgment;

static PrimeHeJudgment prime_he_judged(PrimeHeJudged status, Atom *detail) {
    return (PrimeHeJudgment){.status = status, .detail = detail};
}

static Atom *prime_he_checked_label(Arena *a) {
    return prime_expr2(a, "HOLChecked", prime_sym(a, "hol-light"));
}

/* The elaboration of one term: its fragment result and the substitution of
 * the schematic fragment's type variables. */
typedef struct {
    PrimeHeFragmentResult result;
    PrimeHeSubst subst;
} PrimeHeElaboration;

static void prime_he_elaborate(Space *space, Arena *a, Atom *term,
                               bool primitive_kinds, bool all_schemes,
                               PrimeResourceLedger *ledger,
                               PrimeResourcePhase phase,
                               PrimeHeElaboration *out) {
    out->subst = (PrimeHeSubst){0};
    uint64_t before = prime_resource_phase_begin(ledger);
    out->result = prime_he_fragment_type_observed(
        space, a, term, primitive_kinds, all_schemes, ledger, &out->subst);
    prime_resource_phase_end(ledger, phase, before);
}

/* An atom with the elaboration's substitution applied, or NULL. */
static Atom *prime_he_resolved(Arena *a, const PrimeHeElaboration *e,
                               Atom *atom) {
    bool ok = true;
    Atom *resolved = prime_he_subst_resolve(a, &e->subst, atom, &ok);
    return ok ? resolved : NULL;
}

/* The checker refuses the elaborated term that a fragment rejects: no
 * instance of it has a type. */
static PrimeHeJudgment prime_he_refute_rejected(Arena *a,
                                                const PrimeHeElaboration *e) {
    Atom *annotation = e->result.annotation
        ? prime_he_resolved(a, e, e->result.annotation) : NULL;
    Atom *reason = NULL;
    PrimeHolVerdict verdict = annotation
        ? prime_hol_check_typing(a, annotation, annotation->expr.elems[1],
                                 &reason)
        : PRIME_HOL_UNAVAILABLE;
    Atom *rejects = prime_expr2(a, "grounded-fragment-rejects", e->result.reason);
    if (verdict == PRIME_HOL_REFUSED)
        return prime_he_judged(PRIME_HE_JUDGED_REFUTED,
                               prime_expr3(a, "HOLRefuted", rejects, reason));
    return prime_he_judged(
        PRIME_HE_JUDGED_UNDETERMINED,
        prime_expr2(a, "outside-kernel-fragments",
                    verdict == PRIME_HOL_ACCEPTED
                        ? prime_expr2(a, "elaboration-and-checker-disagree",
                                      rejects)
                        : prime_expr2(a, "unchecked-rejection",
                                      reason ? reason : rejects)));
}

/* type:of, and get-type with `all_schemes`. */
static PrimeHeJudgment prime_he_synthesize(Space *space, Arena *a, Atom *term,
                                           bool all_schemes,
                                           PrimeResourceLedger *ledger) {
    PrimeHeElaboration e;
    prime_he_elaborate(space, a, term, false, all_schemes, ledger,
                       PRIME_RESOURCE_SYNTHESIS, &e);
    PrimeHeJudgment answer;
    if (e.result.status == PRIME_HE_TYPED) {
        Atom *type = prime_he_resolved(a, &e, e.result.type);
        Atom *annotation = prime_he_resolved(a, &e, e.result.annotation);
        Atom *reason = NULL;
        PrimeHolVerdict verdict = type && annotation
            ? prime_hol_check_typing(a, annotation, type, &reason)
            : PRIME_HOL_UNAVAILABLE;
        if (verdict == PRIME_HOL_ACCEPTED) {
            /* `InferredTypes` keeps the public `type:of` reading: the types
             * follow the two descriptive fields.  A first-order scheme
             * stands for its ground first-order instances. */
            Atom *items[4] = {prime_sym(a, "InferredTypes"),
                              prime_he_checked_label(a),
                              prime_he_fragment_name(a, &e.result), type};
            answer = prime_he_judged(PRIME_HE_JUDGED_ESTABLISHED,
                                     atom_expr(a, items, 4));
            answer.type = type;
        } else if (verdict == PRIME_HOL_REFUSED) {
            /* The only elaboration a fragment admits is refused, so the term
             * has no type in the contract. */
            answer = prime_he_judged(PRIME_HE_JUDGED_REFUTED,
                                     prime_expr2(a, "HOLRefuted", reason));
        } else {
            answer = prime_he_judged(
                PRIME_HE_JUDGED_UNDETERMINED,
                reason ? reason : prime_expr1(a, "hol-checker-unavailable"));
        }
    } else if (e.result.status == PRIME_HE_REJECTED) {
        answer = prime_he_refute_rejected(a, &e);
    } else {
        answer = prime_he_judged(PRIME_HE_JUDGED_UNDETERMINED,
                                 prime_he_undetermined_reason(a, &e.result));
        answer.unknown = e.result.status == PRIME_HE_DYNAMIC;
    }
    free(e.subst.items);
    return answer;
}

/* The checker's verdict that the elaborated term has the static type
 * `expected`.  A first-order scheme is checked at the instance that
 * `expected` selects when it is a first-order instance.  At a ground
 * first-order type that is not one, it is checked at its principal
 * instance, which the checker then refuses: at ground first-order types the
 * term has exactly the first-order instances of its principal scheme
 * (Lean: HOLContract.schematic_iff_exact).  A static type outside
 * the first-order fragment is Undetermined for a scheme. */
static PrimeHeJudgment prime_he_check_static(Arena *a, PrimeHeElaboration *e,
                                             Atom *term, Atom *expected,
                                             Atom *(*evidence)(Arena *, Atom *,
                                                               Atom *)) {
    PrimeHeFragmentResult *r = &e->result;
    bool ok = true;
    if (r->schematic && r->rigid)
        return prime_he_judged(
            PRIME_HE_JUDGED_UNDETERMINED,
            prime_expr2(a, "outside-kernel-fragments",
                        prime_expr2(a, "rigid-declaration-variables", term)));
    Atom *principal = r->schematic ? prime_he_resolved(a, e, r->type) : r->type;
    if (!principal) ok = false;
    bool instance = false;
    if (ok && r->schematic)
        instance = prime_he_first_order_instance(principal, expected, &ok);
    if (ok && instance && !prime_he_unify(&e->subst, principal, expected, &ok))
        ok = false;
    if (ok && r->schematic && !instance &&
        !prime_he_first_order(expected, &ok) && ok) {
        /* A first-order term is typed at a ground first-order type exactly
         * at the first-order instances of its principal scheme.  A static
         * type outside the first-order fragment is not decided here. */
        return prime_he_judged(
            PRIME_HE_JUDGED_UNDETERMINED,
            prime_expr2(a, "outside-kernel-fragments",
                        prime_expr3(a, "expected-type-outside-first-order-fragment",
                                    principal, expected)));
    }
    if (!ok)
        return prime_he_judged(PRIME_HE_JUDGED_UNDETERMINED,
                               prime_expr2(a, "outside-kernel-fragments",
                                           prime_expr1(a, "fragment-traversal-allocation")));
    Atom *annotation = prime_he_resolved(a, e, r->annotation);
    Atom *reason = NULL;
    PrimeHolVerdict verdict = annotation
        ? prime_hol_check_typing(a, annotation, expected, &reason)
        : PRIME_HOL_UNAVAILABLE;
    if (verdict == PRIME_HOL_ACCEPTED)
        return prime_he_judged(PRIME_HE_JUDGED_ESTABLISHED,
                               evidence(a, prime_he_fragment_name(a, r), expected));
    if (verdict == PRIME_HOL_REFUSED)
        return prime_he_judged(PRIME_HE_JUDGED_REFUTED,
                               prime_expr2(a, "HOLRefuted", reason));
    return prime_he_judged(PRIME_HE_JUDGED_UNDETERMINED,
                           reason ? reason : prime_expr1(a, "hol-checker-unavailable"));
}

static Atom *prime_he_checked_evidence(Arena *a, Atom *fragment,
                                       Atom *expected) {
    Atom *items[4] = {prime_sym(a, "HOLChecked"), prime_sym(a, "hol-light"),
                      fragment, expected};
    return prime_expr3(a, "CheckedTypingEvidence", prime_sym(a, "exact"),
                       atom_expr(a, items, 4));
}

static Atom *prime_he_analyzed_evidence(Arena *a, Atom *fragment,
                                        Atom *expected) {
    Atom *items[4] = {prime_sym(a, "HOLChecked"), prime_sym(a, "hol-light"),
                      fragment, expected};
    return prime_expr3(a, "ConsistencyEvidence", prime_sym(a, "exact"),
                       atom_expr(a, items, 4));
}

/* type:check (exact).  A term inside a fragment that the checker refuses at
 * a static expected type is refuted; an expected type outside the static
 * types leaves the judgment Undetermined. */
static PrimeHeJudgment prime_he_check_exact(Space *space, Arena *a, Atom *term,
                                            Atom *expected,
                                            PrimeResourceLedger *ledger) {
    PrimeHeElaboration e;
    prime_he_elaborate(space, a, term, false, false, ledger,
                       PRIME_RESOURCE_CHECKING, &e);
    bool ok = true;
    PrimeHeJudgment answer;
    bool static_expected = prime_he_static_type(expected, &ok);
    if (!ok) {
        answer = prime_he_judged(PRIME_HE_JUDGED_UNDETERMINED,
                                 prime_expr2(a, "outside-kernel-fragments",
                                             prime_expr1(a, "fragment-traversal-allocation")));
    } else if (e.result.status != PRIME_HE_TYPED &&
               e.result.status != PRIME_HE_REJECTED) {
        answer = prime_he_judged(PRIME_HE_JUDGED_UNDETERMINED,
                                 prime_he_undetermined_reason(a, &e.result));
    } else if (!static_expected) {
        answer = prime_he_judged(
            PRIME_HE_JUDGED_UNDETERMINED,
            prime_expr2(a, "outside-kernel-fragments",
                        prime_expr2(a, "expected-type-outside-fragments", expected)));
    } else if (e.result.status == PRIME_HE_REJECTED) {
        answer = prime_he_refute_rejected(a, &e);
    } else {
        answer = prime_he_check_static(a, &e, term, expected,
                                       prime_he_checked_evidence);
    }
    free(e.subst.items);
    return answer;
}

/* AGT consistency of variable-free types at the top of a match: `%Undefined%`
 * on either side at any depth, or `Atom` on either side at the top, and
 * otherwise equal leaves and pointwise consistent expressions of equal
 * length (Lean: TypeGrounding.Gradual.corePlusR2Consistent_iff_top). */
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
    if (!type || atom_has_vars(type) || prime_he_arrow_shaped(type)) return NULL;
    return type;
}

/* type:analyze.  A meta-type is decided by the classification theory in the
 * checker, refusal included.  A static expected type is consistent with a
 * static type exactly when the two are equal (Lean:
 * TypeGrounding.Gradual.agt_static_iff_eq), so it is checked as type:check
 * is, except that a rejected term is HE's `%Undefined%` fallback and stays
 * Undetermined.  A
 * gradual expected type is decided by consistency of concretizations, which
 * refutes nothing here. */
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
        return prime_he_judged(PRIME_HE_JUDGED_UNDETERMINED,
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
    PrimeHeElaboration e;
    prime_he_elaborate(space, a, term, false, false, ledger,
                       PRIME_RESOURCE_CHECKING, &e);
    bool ok = true;
    PrimeHeJudgment answer;
    bool static_expected = prime_he_static_type(expected, &ok);
    PrimeHeFragmentStatus status = e.result.status;
    if (!ok) {
        answer = prime_he_judged(PRIME_HE_JUDGED_UNDETERMINED,
                                 prime_expr2(a, "outside-kernel-fragments",
                                             prime_expr1(a, "fragment-traversal-allocation")));
    } else if (status == PRIME_HE_DYNAMIC) {
        /* The unknown type is consistent with every variable-free type. */
        answer = prime_he_judged(
            PRIME_HE_JUDGED_ESTABLISHED,
            prime_expr3(a, "ConsistencyEvidence", prime_sym(a, "dynamic"),
                        prime_expr2(a, "AGTConsistent", expected)));
    } else if (status == PRIME_HE_TYPED && static_expected) {
        answer = prime_he_check_static(a, &e, term, expected,
                                       prime_he_analyzed_evidence);
    } else if (status == PRIME_HE_TYPED) {
        /* A gradual expected type: the static type the checker accepts must
         * be consistent with it. */
        Atom *type = prime_he_resolved(a, &e, e.result.type);
        Atom *annotation = prime_he_resolved(a, &e, e.result.annotation);
        Atom *reason = NULL;
        bool consistent = type && !atom_has_vars(type) &&
                          prime_he_agt_consistent(type, expected, &ok) && ok;
        PrimeHolVerdict verdict = consistent && annotation
            ? prime_hol_check_typing(a, annotation, type, &reason)
            : PRIME_HOL_UNAVAILABLE;
        if (verdict == PRIME_HOL_ACCEPTED) {
            Atom *items[4] = {prime_sym(a, "HOLChecked"), prime_sym(a, "hol-light"),
                              prime_he_fragment_name(a, &e.result), type};
            answer = prime_he_judged(
                PRIME_HE_JUDGED_ESTABLISHED,
                prime_expr3(a, "ConsistencyEvidence", prime_sym(a, "dynamic"),
                            prime_expr3(a, "AGTConsistent", atom_expr(a, items, 4),
                                        expected)));
        } else {
            answer = prime_he_judged(
                PRIME_HE_JUDGED_UNDETERMINED,
                prime_expr2(a, "outside-kernel-fragments",
                            !consistent
                                ? prime_expr3(a, "agt-inconsistent",
                                              type ? type : e.result.type, expected)
                                : reason ? reason
                                : prime_expr1(a, "hol-checker-unavailable")));
        }
    } else {
        /* Outside the static fragments, a constant's declared gradual type
         * can still be consistent with the expected type. */
        Atom *gradual = status == PRIME_HE_OUTSIDE
            ? prime_he_declared_gradual_type(space, a, term) : NULL;
        if (gradual && prime_he_agt_consistent(gradual, expected, &ok) && ok)
            answer = prime_he_judged(
                PRIME_HE_JUDGED_ESTABLISHED,
                prime_expr3(a, "ConsistencyEvidence", prime_sym(a, "dynamic"),
                            prime_expr3(a, "AGTConsistent", gradual, expected)));
        else
            answer = prime_he_judged(PRIME_HE_JUDGED_UNDETERMINED,
                                     prime_he_undetermined_reason(a, &e.result));
    }
    free(e.subst.items);
    return answer;
}

/* Formation of a declared type expression: the expression has type `Type`
 * in the static fragments, reading Prime's primitive types as types of kind
 * `Type`, and the checker accepts it.  `dynamic_only` reports that its only
 * type is the unknown. */
static bool prime_he_fragment_forms_type(Space *space, Arena *a, Atom *type,
                                         PrimeResourceLedger *ledger,
                                         bool *dynamic_only) {
    PrimeHeElaboration e;
    prime_he_elaborate(space, a, type, true, false, ledger,
                       PRIME_RESOURCE_FORMATION, &e);
    if (dynamic_only) *dynamic_only = e.result.status == PRIME_HE_DYNAMIC;
    bool formed = false;
    if (e.result.status == PRIME_HE_TYPED &&
        is_symbol_named(e.result.type, "Type")) {
        Atom *reason = NULL;
        formed = prime_hol_check_typing(a, e.result.annotation, e.result.type,
                                        &reason) == PRIME_HOL_ACCEPTED;
    }
    free(e.subst.items);
    return formed;
}

/* Refinement of a formed type: a type that no `type-index-refinement`
 * declaration mentions carries no refinement obligation.  Any declaration
 * whose head occurs in the type leaves the judgment Undetermined. */
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
        !prime_he_static_type(returned, &ok) || !ok ||
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
    Atom **declared = NULL;
    uint32_t count = prime_he_declared_types(space, a, term->expr.elems[0],
                                              &declared);
    Atom *arrow = count == 1u ? declared[0] : NULL;
    free(declared);
    bool ok = true;
    if (!arrow || !prime_he_arrow_shaped(arrow) ||
        prime_he_telescope(arrow) || arrow->expr.len != term->expr.len + 1u)
        return NULL;
    if (atom_has_vars(arrow))
        return prime_he_gradual_scheme_application_type(space, a, term, arrow);
    Atom *returned = arrow->expr.elems[arrow->expr.len - 1u];
    if (!prime_he_static_type(returned, &ok) || !ok ||
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

CettaPrimeHeTypeAnswer cetta_prime_he_declaration_type(Space *space, Arena *a,
                                                       Atom *term,
                                                       Atom **type_out) {
    *type_out = NULL;
    if (!space || !a || !term) return CETTA_PRIME_HE_TYPE_UNDETERMINED;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_PRIME_HE_FRAGMENT_GET_TYPE);
    PrimeHeJudgment judged = prime_he_synthesize(space, a, term, true, NULL);
    if (judged.status == PRIME_HE_JUDGED_ESTABLISHED) {
        *type_out = judged.type;
        return CETTA_PRIME_HE_TYPE_CHECKED;
    }
    if (judged.status == PRIME_HE_JUDGED_REFUTED)
        return CETTA_PRIME_HE_TYPE_NONE;
    if (judged.unknown)
        return CETTA_PRIME_HE_TYPE_UNKNOWN;
    Atom *gradual = prime_he_gradual_application_type(space, a, term);
    if (gradual) {
        *type_out = gradual;
        return CETTA_PRIME_HE_TYPE_GRADUAL;
    }
    return CETTA_PRIME_HE_TYPE_UNDETERMINED;
}

bool cetta_prime_he_argument_refuted(Space *space, Arena *a, Atom *argument,
                                     Atom *expected) {
    if (!space || !a || !argument || !expected) return false;
    /* Only a typed argument prunes: a rejected one is HE's `%Undefined%`
     * fallback, which every parameter type accepts. */
    PrimeHeElaboration e;
    prime_he_elaborate(space, a, argument, false, false, NULL,
                       PRIME_RESOURCE_CHECKING, &e);
    bool ok = true;
    bool refuted = false;
    if (e.result.status == PRIME_HE_TYPED &&
        prime_he_static_type(expected, &ok) && ok) {
        PrimeHeJudgment judged = prime_he_check_static(
            a, &e, argument, expected, prime_he_checked_evidence);
        refuted = judged.status == PRIME_HE_JUDGED_REFUTED;
    }
    free(e.subst.items);
    return refuted;
}

#undef PRIME_HE_GROW

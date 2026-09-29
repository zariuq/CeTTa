#include "open_equation_machine.h"

#include "binding/frame_identity.h"
#include "grounded.h"
#include "petta_program.h"
#include "petta_semantics.h"
#include "petta_specializer.h"
#include "search_machine.h"
#include "stats.h"
#include "symbol.h"
#include "var_index.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- program ------------------------------------------------------------ */

/* A template instantiates over an activation's slots.  A list template has
 * two children: it is the list whose first element is the first and whose
 * rest is the second, PeTTa's `cons`, built as a cons cell.  A pattern list
 * is the list pattern of a `let` or a `case`, which reads a value spelled
 * `(cons h t)` as the cell it spells, as the host's matcher reads a value
 * against an authored list pattern; a built list reads only cons cells. */
typedef enum {
    OEM_T_LITERAL = 0,
    OEM_T_SLOT,
    OEM_T_BUILD,
    OEM_T_LIST,
    OEM_T_PATTERN_LIST,
} OemTemplateKind;

typedef struct {
    uint8_t kind;
    uint32_t slot;
    uint32_t first;
    uint32_t count;
    Atom *literal;
} OemTemplate;

/* Head programs.  Registers hold query subterms; each operation first
 * dereferences its register.  When that finds an unbound variable, the
 * operation binds it (a symbol or a fresh node); when it finds structure,
 * the operation compares it and loads the children. */
typedef enum {
    OEM_M_BIND = 0,  /* slot takes the register's term (first occurrence) */
    OEM_M_SAME,      /* the register's term unifies with the slot's       */
    OEM_M_ATOM,      /* the register's term is this atomic literal        */
    OEM_M_EXPR,      /* the register's term is an expression of `length`  */
    OEM_M_LIST,      /* the register's term is a nonempty list, PeTTa's
                      * `cons` pattern: its first element and its rest    */
} OemMatchKind;

typedef struct {
    uint8_t kind;
    /* A LIST operation of an authored head reads a value spelled
     * `(cons h t)` as the cell it spells, as the host's matcher reads a
     * value against an authored list pattern.  foldl/4's clauses read only
     * cons cells, as the host's foldl does. */
    uint8_t spelled;
    uint32_t reg;
    uint32_t operand;
    uint32_t length;
    Atom *literal;
} OemMatchOp;

/* Bodies in administrative normal form (the defunctionalized equation
 * bodies of M1).  Step indices are global; a return frame resumes at one.
 * Outputs are passed by destination, as PeTTa's translation does: an
 * activation unifies its body's exposed output term with the caller's
 * destination before any body goal, and a call's destination is its output
 * hole, which an enclosing let or constructor may already have shaped. */
typedef enum {
    OEM_S_RET = 0,   /* the activation's output is complete                */
    OEM_S_FAIL,
    OEM_S_CALL,      /* call `relation` with destination `pattern`         */
    OEM_S_TAIL,      /* call `relation` in place of this equation          */
    OEM_S_PRIM,      /* `pattern` unifies with op(args); `value` is op's
                      * head as a literal                                  */
    OEM_S_BIND,      /* `pattern` unifies with `value`                     */
    OEM_S_TEST,      /* op(args) false: continue at `target`; with no `op`,
                        its one argument's value not the truth value      */
    OEM_S_MATCH,     /* the following steps run once per row of the space
                        `value` that unifies with `pattern`                 */
    OEM_S_HOST,      /* the host evaluates `value` against `pattern`, under
                        the source plan `relation` names; the following
                        steps run once per answer.  A type-pure operation
                        `op` runs in the region when it can decide there */
    OEM_S_CASE,      /* `value` unifies with `pattern`; when it does not,
                        the attempt is undone and the body continues at
                        `target`                                          */
    OEM_S_YIELD,     /* a collection's answer: `value` is copied into the
                        innermost collection frame, and the branch fails
                        back for the next one                             */
    OEM_S_CUT,       /* a once's first answer: the frames above the
                        innermost once frame, and that frame, go, and the
                        body continues at `target`                        */
    OEM_S_EQUATION_CUT, /* PeTTa's `(cut)`: `pattern` meets the truth
                        value, then the frames above the activation's
                        barrier go, its call's untried equations among
                        them                                              */
} OemStepKind;

/* What a HOST step's `target` says of its goal: the host evaluates it
 * (plain), or evaluates it as a counted collection, since it produces a let
 * binder only counting operations read; or the region may decide first a
 * type-pure operation `op`, an expression whose head is a variable, which
 * is data when that head's value is a number or a string, an `add-atom`
 * whose payload is ground, which the host admits as its machine does, or
 * the evaluated elements of a dynamic call, which are data when the head's
 * value cannot be applied and otherwise the host's to apply.  The kinds the
 * region may decide follow the others. */
enum {
    OEM_HOST_PLAIN = 0u,
    OEM_HOST_COUNTED,
    /* A head's relational occurrence of an operation the host owns: the
     * host evaluates it without a plan, against the kept query subterm, as
     * the search machine evaluates a head's calls. */
    OEM_HOST_OCCURRENCE,
    /* A control the region runs itself, whose goal stays the host's when
     * it cannot: `superpose` over a list value, which enumerates its
     * elements; `collapse` over a local relation (`local`), which collects
     * its answers; a literal `superpose`, whose alternatives are that
     * relation's equations; and `once`, which commits to that relation's
     * first answer. */
    OEM_HOST_ELEMENTS,
    OEM_HOST_COLLECT,
    OEM_HOST_ALTERNATIVES,
    OEM_HOST_ONCE,
    OEM_HOST_PURE,
    OEM_HOST_DATA_HEAD,
    OEM_HOST_ADMIT,
    OEM_HOST_APPLY,
    OEM_HOST_SORT,
    /* PeTTa's `append` and `union-atom`, `length`, `exclude-item`,
     * `list_to_set`, `unique-atom`, `alpha-unique-atom`, `car-atom`,
     * `cdr-atom`, and `decons-atom` and `decons`. */
    OEM_HOST_LIST,
    /* `(== () (collapse (once (match S P T))))`, answered by membership. */
    OEM_HOST_EXISTS,
    /* `get-metatype` of a value that names no token. */
    OEM_HOST_METATYPE,
};

typedef struct {
    uint8_t kind;
    bool match_row; /* an already-decomposed conjunction leg */
    /* The step's `pattern` is a slot at its first occurrence on every path
     * through the body: the step stores its result there rather than
     * unifying (a PRIM, a BIND, or a HOST step the region may decide). */
    uint8_t store;
    /* A CALL, or a HOST step's application entering a relation, that PeTTa
     * dispatches at run time: its body runs under the implicit handler, so
     * an error it raises fails that path alone (DispatchErrorScope). */
    uint8_t recovers;
    /* A literal `superpose` in a tail position: its alternatives return to
     * the equation's caller, as a tail call does. */
    uint8_t tail;
    union {
        SymbolId op;
        /* A control the region runs (OEM_HOST_COLLECT,
         * OEM_HOST_ALTERNATIVES and OEM_HOST_ONCE): its entry in the
         * program's `controls`. */
        uint32_t control;
    };
    uint32_t relation;
    uint32_t first_arg;
    uint32_t arg_count;
    uint32_t pattern;
    uint32_t value;
    uint32_t target;
} OemStep;

/* A control the region runs: its local relation; for a collection, the
 * hole each answer fills, the YIELD step that copies it, and the answers to
 * take (zero for all); for a once, the CUT step its answer reaches.  Kept
 * beside the steps, so a step stays one cache-friendly size. */
typedef struct {
    uint32_t local;
    uint32_t yield;
    uint32_t resume;
    uint32_t limit;
} OemControl;

/* How an activation meets its destination after head matching. */
typedef enum {
    OEM_DEST_NONE = 0,   /* a tail call or `empty`: the callee meets it */
    OEM_DEST_UNIFY,      /* unify with the template `destination` */
    OEM_DEST_SLOT,       /* the output is slot `destination`: when head
                            matching left it unset, it is the destination */
} OemDestKind;

/* The output an equation's body fixes before it runs, which the host's
 * search makes part of the equation's left-hand side: none, the template
 * `output`, or the host's truth value. */
typedef enum {
    OEM_OUTPUT_OPEN = 0,
    OEM_OUTPUT_TEMPLATE,
    OEM_OUTPUT_TRUE,
} OemOutputKind;

typedef struct {
    uint32_t first_op;
    uint32_t op_count;
    uint32_t register_count;
    uint32_t slot_count;
    uint32_t first_step;
    uint8_t dest_kind;
    uint32_t destination;
    /* When the destination takes an output slot head matching leaves
     * unset, the output the body's plan fixes (OemOutputKind). */
    uint8_t output_kind;
    uint32_t output;
    /* From here in the program's `slot_stores`, one flag per slot: set
     * when the body stores the slot at its first occurrence on every path,
     * so the activation leaves it without a cell. */
    uint32_t first_slot_store;
    /* The program's `cell_slots[first_cell_slot, +cell_slot_count)`. */
    uint32_t first_cell_slot;
    uint32_t cell_slot_count;
    /* The body has an EQUATION_CUT step: its slot vectors record the
     * height beneath the call's own frame (oem_slot_header). */
    bool cuts;
} OemEquation;

/* A path from an argument into its structure, each step a child of an
 * expression of a given length, at which some equations' heads test a
 * literal: the equations testing nothing there are `free`, and each key
 * names the equations testing its literal. */
typedef struct {
    uint32_t arg;
    uint32_t first_step;
    uint32_t step_count;
    uint32_t first_key;
    uint32_t key_count;
    uint64_t free;
    /* A path with enough keys, each a symbol or an integer, also places
     * them in `table_mask + 1` entries of the program's `index_table` from
     * `first_table`, each a key's number plus one, or zero; no table when
     * `table_mask` is zero. */
    uint32_t first_table;
    uint32_t table_mask;
} OemIndexPath;

typedef struct {
    Atom *literal;
    uint64_t equations;
} OemIndexKey;

/* A switch on an argument's principal functor: an atom's literal, or an
 * expression's length with its first element's literal.  It decides the
 * index paths at the argument itself and at its first element together:
 * for each functor they read, the conjunction of what they admit is one
 * entry, found by the functor's code.  Its paths stay in the relation's
 * path array, `path_count` of them from `first_path`, for a literal whose
 * number could equal an integer key under `atom_eq`, which they decide. */
typedef struct {
    uint32_t arg;
    uint32_t first_path;
    uint32_t path_count;
    /* Lengths the paths read, `length_count` from `first_length` in the
     * program's `switch_lengths`. */
    uint32_t first_length;
    uint32_t length_count;
    /* `entry_mask + 1` table slots from `first_table` in the program's
     * `switch_tables`, each an entry's number plus one, or zero. */
    uint32_t first_table;
    uint32_t entry_mask;
    uint32_t first_entry;
    /* An atom's paths have an integer key. */
    bool int_keys;
    /* An atom no key equals, or an expression of a length no path reads. */
    uint64_t other;
} OemIndexSwitch;

typedef struct {
    uint32_t length;
    /* The first element's paths have an integer key. */
    bool int_keys;
    /* The expression's first element is open, or no key equals it. */
    uint64_t open_head;
    uint64_t no_key;
} OemSwitchLength;

typedef struct {
    /* Zero for an atom's key. */
    uint32_t length;
    Atom *literal;
    uint64_t equations;
} OemSwitchEntry;

typedef struct {
    SymbolId head;
    uint32_t arity;
    uint32_t first_equation;
    uint32_t equation_count;
    /* The relation's index: paths at which its equations' heads test
     * literals (at most 64 equations), none when it has no use.  The paths
     * at an argument and its first element may be decided by a switch on
     * the argument's functor instead: `index_path_count` paths are read
     * one by one, and `switch_count` switches from `first_switch`. */
    uint32_t first_index_path;
    uint32_t index_path_count;
    uint32_t first_switch;
    uint32_t switch_count;
    bool indexed;
    /* Some two equations differ in their heads' first structural tests,
     * which a call can then decide between as it enters. */
    bool first_tests_differ;
    /* A local relation holds the body of a control of an equation of the
     * program: its equations are compiled from `sources`, its parameters
     * are the variables the body shares with the rest of that equation,
     * and it runs in the version of the code that calls it.  One that does
     * not compile leaves its control to the host (`failed`).  A local
     * relation with a `fold_step` holds foldl/4's clauses for that step
     * (oem_fold_relation). */
    bool local;
    bool failed;
    SymbolId fold_step;
    uint32_t first_source;
    uint32_t source_count;
} OemRelation;

/* One equation of a local relation: parameters, body and the body's plan. */
typedef struct {
    Atom *lhs;
    Atom *rhs;
    const PettaPlanNode *plan;
} OemLocalSource;

struct CettaOpenEquationProgram {
    /* The compiler's reference and one per open cursor.  Cursors open and
     * close on worker threads (hyperpose), so the count is atomic: the last
     * release, which frees the program, follows every use before it. */
    _Atomic uint32_t refs;
    Space *space;
    SpaceProgramToken token;
    /* The spelling of region and answer variables. */
    SymbolId spelling;
    /* The equations of local relations, and the heads made for them. */
    OemLocalSource *sources;
    uint32_t source_len, source_cap;
    OemControl *controls;
    uint32_t control_len, control_cap;
    Arena atoms;
    bool atoms_ready;
    OemRelation *relations;
    uint32_t relation_len, relation_cap;
    OemEquation *equations;
    uint32_t equation_len, equation_cap;
    OemMatchOp *ops;
    uint32_t op_len, op_cap;
    OemIndexPath *index_paths;
    uint32_t index_path_len, index_path_cap;
    /* Pairs (child, length), the steps of the index paths. */
    uint32_t *index_steps;
    uint32_t index_step_len, index_step_cap;
    OemIndexKey *index_keys;
    uint32_t index_key_len, index_key_cap;
    uint32_t *index_tables;
    uint32_t index_table_len, index_table_cap;
    OemIndexSwitch *index_switchs;
    uint32_t index_switch_len, index_switch_cap;
    OemSwitchLength *switch_lengths;
    uint32_t switch_length_len, switch_length_cap;
    OemSwitchEntry *switch_entrys;
    uint32_t switch_entry_len, switch_entry_cap;
    uint32_t *switch_tables;
    uint32_t switch_table_len, switch_table_cap;
    OemStep *steps;
    uint32_t step_len, step_cap;
    uint32_t *step_args;
    uint32_t step_arg_len, step_arg_cap;
    OemTemplate *templates;
    uint32_t template_len, template_cap;
    uint32_t *template_children;
    uint32_t template_child_len, template_child_cap;
    /* HOST goal plans: source plans owned by the PeTTa program, or ready
     * static-call plans owned by this program's arena. */
    const PettaPlanNode **host_plans;
    uint32_t host_plan_len, host_plan_cap;
    uint8_t *slot_stores;
    uint32_t slot_store_len, slot_store_cap;
    /* Per equation, the slots an activation gives a fresh cell: those head
     * matching leaves unset and the body does not store at first
     * occurrence. */
    uint32_t *cell_slots;
    uint32_t cell_slot_len, cell_slot_cap;
    /* The entry relation only relays to the host (see
     * cetta_open_equation_program_entry_relays). */
    bool entry_relays;
    /* Pruning a derivation gives a subset of positive, effect-free search
     * answers. Complete observers, commits and possibly skipped effects or
     * errors require exact execution. Computed once over the whole region. */
    bool depth_pruning_safe;
};

enum {
    OEM_MAX_RELATIONS = 4096u,
    OEM_MAX_EQUATION_SLOTS = 4096u,
    OEM_MAX_REGISTERS = 4096u,
    OEM_MAX_DEPTH = 512u,
};

static bool oem_reserve(void **items, uint32_t *cap, uint32_t needed,
                        size_t size) {
    if (needed <= *cap)
        return true;
    uint32_t next = *cap ? *cap : 16u;
    while (next < needed) {
        if (next > UINT32_MAX / 2u)
            return false;
        next *= 2u;
    }
    void *grown = realloc(*items, (size_t)next * size);
    if (!grown)
        return false;
    *items = grown;
    *cap = next;
    return true;
}

#define OEM_PUSH(program, field, value, index_out)                          \
    (oem_reserve((void **)&(program)->field##s, &(program)->field##_cap,    \
                 (program)->field##_len + 1u,                              \
                 sizeof(*(program)->field##s)) &&                          \
     ((program)->field##s[(program)->field##_len] = (value),              \
      *(index_out) = (program)->field##_len++, true))

void cetta_open_equation_program_retain(CettaOpenEquationProgram *program) {
    if (program)
        atomic_fetch_add_explicit(&program->refs, 1u, memory_order_relaxed);
}

void cetta_open_equation_program_release(
    CettaOpenEquationProgram *program) {
    if (!program ||
        atomic_fetch_sub_explicit(&program->refs, 1u,
                                  memory_order_acq_rel) != 1u)
        return;
    free(program->relations);
    free(program->equations);
    free(program->ops);
    free(program->index_paths);
    free(program->index_steps);
    free(program->index_keys);
    free(program->index_tables);
    free(program->index_switchs);
    free(program->switch_lengths);
    free(program->switch_entrys);
    free(program->switch_tables);
    free(program->steps);
    free(program->step_args);
    free(program->templates);
    free(program->template_children);
    free(program->host_plans);
    free(program->slot_stores);
    free(program->cell_slots);
    free(program->sources);
    free(program->controls);
    if (program->atoms_ready)
        arena_free(&program->atoms);
    free(program);
}

bool cetta_open_equation_program_is_current(
    const CettaOpenEquationProgram *program) {
    return program && space_program_token_is_current(program->token);
}

uint32_t cetta_open_equation_program_relation_count(
    const CettaOpenEquationProgram *program) {
    return program ? program->relation_len : 0u;
}

bool cetta_open_equation_program_relation(
    const CettaOpenEquationProgram *program, uint32_t index,
    SymbolId *head_out, uint32_t *arity_out) {
    if (!program || index >= program->relation_len)
        return false;
    if (head_out)
        *head_out = program->relations[index].head;
    if (arity_out)
        *arity_out = program->relations[index].arity;
    return true;
}

/* ---- compiling one equation --------------------------------------------- */

/* A relational occurrence met in the head: its call runs after the
 * structural match, unified with the query subterm kept in `slot`. */
typedef struct {
    Atom *call;
    uint32_t slot;
} OemPendingCall;

/* A body is lowered to a tree first, so that each construct's exposed
 * output term is known before its goals are emitted.  PeTTa unifies an
 * equation's exposed output with the caller's destination during head
 * matching, a let's pattern with its value's exposed term before the
 * value's goals run, and an if's output with a branch's exposed term before
 * the branch runs. */
typedef enum {
    OEM_N_VALUE = 0,   /* a variable, a literal or a constructor node */
    OEM_N_CALL,        /* a relation call; its output is the hole `exposed` */
    OEM_N_PRIM,
    OEM_N_LET,
    OEM_N_IF,
    OEM_N_FAIL,
    OEM_N_MATCH,       /* `(match space pattern template)`: the template's
                          output is the construct's */
    OEM_N_HOST,        /* an operation the host evaluates: its output is the
                          hole `exposed` */
    OEM_N_APPLY,       /* a dynamic call: its elements' values, then the
                          host's application of them into the hole
                          `exposed`; with `host_fast` OEM_HOST_SORT, PeTTa's
                          `sort-atom` or `msort` (`op`) of its argument's
                          value, which the region sorts when it is ground */
    OEM_N_CASE,        /* `(case key arms)`: the key's node is `value`; the
                          children are pairs of a pattern's node, a value
                          node whose exposed term is the pattern, and its
                          branch; the output is the hole `exposed` */
    OEM_N_COLLECT,     /* `(collapse body)`: the answers of the local
                          relation `local` over the children's values, as a
                          list in the hole `exposed`, the first `limit` of
                          them when it is not zero; each answer fills the
                          hole `yield` first.  `value` and `relation` are
                          the host's goal and plan, should the body not
                          compile */
    OEM_N_ALTERNATIVES, /* a literal `superpose`: the answers of the local
                          relation `local`, one equation per alternative,
                          into the hole `exposed`; host fallback as above */
    OEM_N_ONCE,        /* `(once body)`: the first answer of the local
                          relation `local` into the hole `exposed`; host
                          fallback as above */
    OEM_N_CUT,         /* `(cut)` in an equation's own body: the call
                          commits to the equation, and the cut's output,
                          the hole `exposed`, is the truth value */
} OemNodeKind;

typedef struct {
    uint8_t kind;
    bool match_row;
    bool tail;
    /* A HOST node's fast path (OEM_HOST_*). */
    uint8_t host_fast;
    /* A call or an application PeTTa dispatches at run time (OemStep). */
    bool recovers;
    SymbolId op;
    uint32_t relation;
    /* Template of the construct's exposed output term. */
    uint32_t exposed;
    /* Constructor children, call or primitive arguments, test arguments. */
    uint32_t first;
    uint32_t count;
    uint32_t pattern;
    uint32_t value;
    uint32_t body;
    uint32_t then_node;
    uint32_t else_node;
    uint32_t local;
    uint32_t yield;
    uint32_t limit;
} OemNode;

typedef struct {
    CettaOpenEquationProgram *program;
    const CettaOpenEquationHost *host;
    /* The PeTTa program the equations come from, whose planner plans the
     * clauses the region adds (oem_fold_relation). */
    PettaProgram *petta;
    /* The equation is authored: its list patterns read spelled cells
     * (OemMatchOp). */
    bool authored;
    /* The equation is a named relation's, whose `(cut)` commits its call
     * to it.  A local relation's is a control's body, where PeTTa's cut
     * acts on the enclosing equation or stays inside the control. */
    bool named;
    /* The equation being compiled. */
    Atom *lhs;
    Atom *rhs;
    VarId *vars;
    uint32_t var_len, var_cap;
    /* Head compilation: whether a slot's variable has occurred yet. */
    bool *bound;
    uint32_t slot_count;
    uint32_t register_count;
    OemPendingCall *pending;
    uint32_t pending_len, pending_cap;
    OemNode *nodes;
    uint32_t node_len, node_cap;
    uint32_t *node_children;
    uint32_t node_child_len, node_child_cap;
    /* The control steps whose answers return to a step after the body: a
     * collection's YIELD, a once's CUT. */
    uint32_t *resumes;
    uint32_t resume_len, resume_cap;
    /* The template of the equation's output, which each RET carries. */
    uint32_t output;
    /* An EQUATION_CUT step has been emitted. */
    bool cuts;
    /* While a let's pattern is lowered: the let (for a let*, the whole),
     * and the bindings a let* evaluates before this one. */
    const Atom *let_scope;
    Atom *const *let_before;
    uint32_t let_before_len;
    const char *reason;
} OemCompile;

static bool oem_reject(OemCompile *compile, const char *reason) {
    if (!compile->reason)
        compile->reason = reason;
    return false;
}

static bool oem_slot_of(OemCompile *compile, VarId id, uint32_t *slot_out) {
    for (uint32_t index = 0u; index < compile->var_len; index++) {
        if (compile->vars[index] == id) {
            *slot_out = index;
            return true;
        }
    }
    return oem_reject(compile, "unregistered variable");
}

/* Every variable of the equation gets its slot, in first-occurrence order,
 * before any temporary. */
static bool oem_register_vars(OemCompile *compile, Atom *atom,
                              uint32_t depth) {
    if (!atom || depth > OEM_MAX_DEPTH)
        return oem_reject(compile, "equation too deep");
    if (atom->kind == ATOM_VAR) {
        for (uint32_t index = 0u; index < compile->var_len; index++) {
            if (compile->vars[index] == atom->var_id)
                return true;
        }
        if (compile->var_len >= OEM_MAX_EQUATION_SLOTS ||
            !oem_reserve((void **)&compile->vars, &compile->var_cap,
                         compile->var_len + 1u, sizeof(*compile->vars)))
            return oem_reject(compile, "too many variables");
        compile->vars[compile->var_len++] = atom->var_id;
        return true;
    }
    if (atom->kind != ATOM_EXPR || !atom_has_vars(atom))
        return true;
    for (CettaExprIndex child = 0u; child < atom->expr.len; child++) {
        if (!oem_register_vars(compile, atom->expr.elems[child], depth + 1u))
            return false;
    }
    return true;
}

static bool oem_temp_slot(OemCompile *compile, uint32_t *slot_out) {
    if (compile->slot_count >= OEM_MAX_EQUATION_SLOTS)
        return oem_reject(compile, "too many temporaries");
    *slot_out = compile->slot_count++;
    return true;
}

static bool oem_template_literal(OemCompile *compile, Atom *atom,
                                 uint32_t *out) {
    OemTemplate item = {.kind = OEM_T_LITERAL, .literal = atom};
    return OEM_PUSH(compile->program, template, item, out) ||
        oem_reject(compile, "out of memory");
}

/* The head of operation `op` as a literal template, made once here for the
 * grounded dispatcher rather than at each run; none without an operation. */
static bool oem_op_literal(OemCompile *compile, SymbolId op, uint32_t *out) {
    *out = 0u;
    if (op == SYMBOL_ID_NONE)
        return true;
    Atom *head = atom_symbol_id(&compile->program->atoms, op);
    return head ? oem_template_literal(compile, head, out)
                : oem_reject(compile, "out of memory");
}

static bool oem_template_slot(OemCompile *compile, uint32_t slot,
                              uint32_t *out) {
    OemTemplate item = {.kind = OEM_T_SLOT, .slot = slot};
    return OEM_PUSH(compile->program, template, item, out) ||
        oem_reject(compile, "out of memory");
}

static bool oem_template_build(OemCompile *compile, const uint32_t *children,
                               uint32_t count, uint32_t *out) {
    CettaOpenEquationProgram *program = compile->program;
    uint32_t first = program->template_child_len;
    if (!oem_reserve((void **)&program->template_children,
                     &program->template_child_cap, first + count,
                     sizeof(*program->template_children)))
        return oem_reject(compile, "out of memory");
    if (count > 0u)
        memcpy(&program->template_children[first], children,
               sizeof(*children) * count);
    program->template_child_len += count;
    OemTemplate item = {.kind = OEM_T_BUILD, .first = first, .count = count};
    return OEM_PUSH(program, template, item, out) ||
        oem_reject(compile, "out of memory");
}

static bool oem_template_list(OemCompile *compile, uint8_t kind,
                              uint32_t head, uint32_t tail, uint32_t *out) {
    uint32_t children[2] = {head, tail};
    if (!oem_template_build(compile, children, 2u, out))
        return false;
    compile->program->templates[*out].kind = kind;
    return true;
}

static inline bool oem_template_is_list(const OemTemplate *item) {
    return item->kind == OEM_T_LIST || item->kind == OEM_T_PATTERN_LIST;
}

static bool oem_emit(OemCompile *compile, OemStep step, uint32_t *index_out) {
    uint32_t index = 0u;
    if (!OEM_PUSH(compile->program, step, step, &index))
        return oem_reject(compile, "out of memory");
    if (index_out)
        *index_out = index;
    return true;
}

static bool oem_args(OemCompile *compile, const uint32_t *templates,
                     uint32_t count, uint32_t *first_out) {
    CettaOpenEquationProgram *program = compile->program;
    uint32_t first = program->step_arg_len;
    if (!oem_reserve((void **)&program->step_args, &program->step_arg_cap,
                     first + count, sizeof(*program->step_args)))
        return oem_reject(compile, "out of memory");
    if (count > 0u)
        memcpy(&program->step_args[first], templates,
               sizeof(*templates) * count);
    program->step_arg_len += count;
    *first_out = first;
    return true;
}

static bool oem_relation_index(CettaOpenEquationProgram *program,
                               SymbolId head, uint32_t arity,
                               uint32_t *index_out) {
    for (uint32_t index = 0u; index < program->relation_len; index++) {
        if (program->relations[index].head == head &&
            program->relations[index].arity == arity) {
            *index_out = index;
            return true;
        }
    }
    if (program->relation_len >= OEM_MAX_RELATIONS)
        return false;
    OemRelation relation = {.head = head, .arity = arity};
    return OEM_PUSH(program, relation, relation, index_out);
}

/* Whether a head subterm is a relational occurrence: the host classifies
 * it exactly as the search machine classifies equation heads. */
static bool oem_head_callable(OemCompile *compile, Atom *atom) {
    return atom->kind == ATOM_EXPR && atom->expr.len > 0u &&
        atom->expr.elems[0]->kind == ATOM_SYMBOL &&
        compile->host && compile->host->head_callable &&
        compile->host->head_callable(compile->host->context,
                                     compile->program->space, atom);
}

/* A head subterm with no relational occurrence anywhere inside.  PeTTa
 * reads a `cons` in a head as a list pattern before it asks whether the
 * subterm is callable. */
static bool oem_head_data(OemCompile *compile, Atom *atom, uint32_t depth) {
    if (!atom || depth > OEM_MAX_DEPTH)
        return false;
    if (atom->kind != ATOM_EXPR)
        return true;
    if (!petta_semantics_is_cons_constraint(atom) &&
        oem_head_callable(compile, atom))
        return false;
    for (CettaExprIndex child = 0u; child < atom->expr.len; child++) {
        if (!oem_head_data(compile, atom->expr.elems[child], depth + 1u))
            return false;
    }
    return true;
}

/* The head program for one parameter at `reg`. */
static bool oem_compile_param(OemCompile *compile, Atom *param, uint32_t reg,
                              uint32_t depth) {
    CettaOpenEquationProgram *program = compile->program;
    if (!param || depth > OEM_MAX_DEPTH)
        return oem_reject(compile, "head pattern too deep");
    uint32_t index = 0u;
    if (param->kind == ATOM_EXPR && param->expr.len > 0u &&
        param->expr.elems[0]->kind == ATOM_SYMBOL &&
        param->expr.elems[0]->sym_id == g_builtin_syms.quote)
        return oem_reject(compile, "quoted head pattern");
    if (petta_semantics_is_cons_constraint(param)) {
        /* A list pattern, which PeTTa reads before any relational
         * occurrence: its head and its tail match the list's first element
         * and its rest. */
        if (compile->register_count > OEM_MAX_REGISTERS - 2u)
            return oem_reject(compile, "head pattern too wide");
        uint32_t base = compile->register_count;
        compile->register_count += 2u;
        OemMatchOp op = {
            .kind = OEM_M_LIST, .spelled = compile->authored, .reg = reg,
            .operand = base, .length = 2u,
        };
        return (OEM_PUSH(program, op, op, &index) ||
                oem_reject(compile, "out of memory")) &&
            oem_compile_param(compile, param->expr.elems[1], base,
                              depth + 1u) &&
            oem_compile_param(compile, param->expr.elems[2], base + 1u,
                              depth + 1u);
    }
    if (oem_head_callable(compile, param)) {
        /* The query subterm waits in a slot; the call runs after the
         * structural match. */
        uint32_t slot = 0u;
        if (!oem_temp_slot(compile, &slot) ||
            !oem_reserve((void **)&compile->pending, &compile->pending_cap,
                         compile->pending_len + 1u, sizeof(*compile->pending)))
            return oem_reject(compile, "too many relational occurrences");
        compile->pending[compile->pending_len++] =
            (OemPendingCall){.call = param, .slot = slot};
        OemMatchOp op = {.kind = OEM_M_BIND, .reg = reg, .operand = slot};
        return OEM_PUSH(program, op, op, &index) ||
            oem_reject(compile, "out of memory");
    }
    if (param->kind == ATOM_VAR) {
        uint32_t slot = 0u;
        if (!oem_slot_of(compile, param->var_id, &slot))
            return false;
        OemMatchOp op = {
            .kind = compile->bound[slot] ? OEM_M_SAME : OEM_M_BIND,
            .reg = reg,
            .operand = slot,
        };
        compile->bound[slot] = true;
        return OEM_PUSH(program, op, op, &index) ||
            oem_reject(compile, "out of memory");
    }
    if (param->kind != ATOM_EXPR || param->expr.len == 0u) {
        OemMatchOp op = {.kind = OEM_M_ATOM, .reg = reg, .literal = param};
        return OEM_PUSH(program, op, op, &index) ||
            oem_reject(compile, "out of memory");
    }
    uint32_t length = param->expr.len;
    if (compile->register_count > OEM_MAX_REGISTERS - length)
        return oem_reject(compile, "head pattern too wide");
    uint32_t base = compile->register_count;
    compile->register_count += length;
    OemMatchOp op = {
        .kind = OEM_M_EXPR, .reg = reg, .operand = base, .length = length,
    };
    if (!OEM_PUSH(program, op, op, &index))
        return oem_reject(compile, "out of memory");
    for (uint32_t child = 0u; child < length; child++) {
        if (!oem_compile_param(compile, param->expr.elems[child],
                               base + child, depth + 1u))
            return false;
    }
    return true;
}

static bool oem_is_prim(SymbolId head) {
    return head == g_builtin_syms.op_plus ||
           head == g_builtin_syms.op_minus ||
           head == g_builtin_syms.op_mul ||
           head == g_builtin_syms.op_mod ||
           head == g_builtin_syms.petta_min ||
           head == g_builtin_syms.petta_max;
}

static bool oem_is_test(SymbolId head) {
    return head == g_builtin_syms.op_lt || head == g_builtin_syms.op_gt ||
           head == g_builtin_syms.op_le || head == g_builtin_syms.op_ge ||
           head == g_builtin_syms.op_eq;
}

/* The source head of a call occurrence the program plans as a relation. */
static bool oem_relation_call(Atom *expr, const PettaPlanNode *plan) {
    return plan && expr->kind == ATOM_EXPR && expr->expr.len > 0u &&
        expr->expr.elems[0]->kind == ATOM_SYMBOL &&
        plan->role == PETTA_PLAN_STATIC_CALL &&
        plan->execution == PETTA_PLAN_EXEC_RELATION_SLOTS &&
        plan->relation_head_admitted &&
        plan->control == PETTA_PLAN_CONTROL_NONE &&
        petta_semantics_form(expr->expr.elems[0]->sym_id) == PETTA_FORM_NONE;
}

static bool oem_prim_call(Atom *expr, const PettaPlanNode *plan) {
    return plan && expr->kind == ATOM_EXPR && expr->expr.len == 3u &&
        expr->expr.elems[0]->kind == ATOM_SYMBOL &&
        plan->role == PETTA_PLAN_STATIC_CALL &&
        plan->execution == PETTA_PLAN_EXEC_PURE_GROUNDED_SLOTS &&
        oem_is_prim(expr->expr.elems[0]->sym_id);
}

/* A pattern, or data over the equation's variables: variables are slots,
 * nothing is evaluated.  A pattern the plan calls a call is a relational
 * pattern, outside the fragment.  Where PeTTa reads a `cons` as a list
 * (`lists`: a let or case pattern, an argument of a head's relational
 * occurrence), it lowers to a list template; elsewhere (a match pattern, a
 * goal the host evaluates) it stays as written. */
static uint64_t oem_var_occurrences(const Atom *atom, VarId id,
                                    uint32_t depth);

/* Whether a let pattern the plan calls is data after all.  PeTTa's let
 * evaluates its pattern first, and an application whose head is an unbound
 * variable evaluates to itself.  The head is unbound when its variable
 * occurs in no parameter, and in the body only within the let, not in the
 * bindings a let* evaluates before this one: nothing binds it before the
 * pattern.  A bound head, as in `(let $h f (let ($h 2) 3 ...))`, makes a
 * call, and stays outside the fragment. */
static bool oem_pattern_head_fresh(const OemCompile *compile,
                                   const Atom *pattern) {
    if (!compile->let_scope || pattern->kind != ATOM_EXPR ||
        pattern->expr.len == 0u || pattern->expr.elems[0]->kind != ATOM_VAR)
        return false;
    VarId id = pattern->expr.elems[0]->var_id;
    uint64_t deep = UINT64_MAX / 4u;
    uint64_t in_head = oem_var_occurrences(compile->lhs, id, 0u);
    uint64_t in_body = oem_var_occurrences(compile->rhs, id, 0u);
    uint64_t in_scope = oem_var_occurrences(compile->let_scope, id, 0u);
    if (in_head != 0u || in_body >= deep || in_scope >= deep)
        return false;
    for (uint32_t index = 0u; index < compile->let_before_len; index++) {
        uint64_t before =
            oem_var_occurrences(compile->let_before[index], id, 0u);
        if (before >= deep || before > in_scope)
            return false;
        in_scope -= before;
    }
    return in_body == in_scope;
}

static bool oem_lower_pattern(OemCompile *compile, Atom *pattern,
                              const PettaPlanNode *plan, uint32_t depth,
                              bool lists, uint32_t *out) {
    if (!pattern || depth > OEM_MAX_DEPTH)
        return oem_reject(compile, "pattern too deep");
    if (pattern->kind == ATOM_VAR) {
        uint32_t slot = 0u;
        return oem_slot_of(compile, pattern->var_id, &slot) &&
            oem_template_slot(compile, slot, out);
    }
    if (pattern->kind != ATOM_EXPR || pattern->expr.len == 0u)
        return oem_template_literal(compile, pattern, out);
    if (lists && petta_semantics_is_cons_constraint(pattern)) {
        uint32_t head = 0u;
        uint32_t tail = 0u;
        return oem_lower_pattern(compile, pattern->expr.elems[1],
                                 plan ? petta_plan_child(plan, 1u) : NULL,
                                 depth + 1u, true, &head) &&
            oem_lower_pattern(compile, pattern->expr.elems[2],
                              plan ? petta_plan_child(plan, 2u) : NULL,
                              depth + 1u, true, &tail) &&
            oem_template_list(compile, OEM_T_PATTERN_LIST, head, tail, out);
    }
    if (plan && plan->role != PETTA_PLAN_DATA &&
        !oem_pattern_head_fresh(compile, pattern))
        return oem_reject(compile, "relational pattern");
    uint32_t count = pattern->expr.len;
    uint32_t *children = malloc(sizeof(*children) * count);
    if (!children)
        return oem_reject(compile, "out of memory");
    bool ok = true;
    for (uint32_t index = 0u; ok && index < count; index++)
        ok = oem_lower_pattern(compile, pattern->expr.elems[index],
                               plan ? petta_plan_child(plan, index) : NULL,
                               depth + 1u, lists, &children[index]);
    ok = ok && oem_template_build(compile, children, count, out);
    free(children);
    return ok;
}

/* The plan of foldl/4's recursive clause, `(foldl step $xs (step $x $acc))`,
 * with the step application a run-time dispatch, as the host's foldl
 * applies each step.  NULL unless the application is a call of the step's
 * relation. */
static const PettaPlanNode *oem_fold_recursion_plan(
    CettaOpenEquationProgram *program, const PettaPlanNode *source) {
    const PettaPlanNode *application = petta_plan_child(source, 3u);
    if (!application || source->child_count != 4u ||
        application->role != PETTA_PLAN_STATIC_CALL ||
        application->execution != PETTA_PLAN_EXEC_RELATION_SLOTS ||
        !application->relation_head_admitted ||
        application->control != PETTA_PLAN_CONTROL_NONE)
        return NULL;
    PettaPlanNode *root = arena_alloc(&program->atoms, sizeof(*root));
    PettaPlanNode *children =
        arena_alloc(&program->atoms, sizeof(*children) * 4u);
    if (!root || !children)
        return NULL;
    memcpy(children, source->children, sizeof(*children) * 4u);
    children[3].dispatch_handler = true;
    *root = *source;
    root->children = children;
    return root;
}

/* PeTTa's foldl is SWI's foldl/4.  Folding a relation `step`/2 over a list
 * is a call of the local relation that holds foldl/4's two clauses for
 * that step:
 *     (() $acc)            -> $acc
 *     ((cons $x $xs) $acc) -> (foldl step $xs (step $x $acc))
 * The recursive foldl is this relation again, a tail call, and each step
 * application is dispatched at run time, as the host's foldl applies it.
 * One relation per step, made once; `*out` is UINT32_MAX when the planner
 * does not plan the step application as a call of that relation. */
static bool oem_fold_relation(OemCompile *compile, SymbolId foldl,
                              SymbolId step, uint32_t *out) {
    CettaOpenEquationProgram *program = compile->program;
    *out = UINT32_MAX;
    for (uint32_t index = 0u; index < program->relation_len; index++) {
        if (program->relations[index].local &&
            program->relations[index].fold_step == step) {
            *out = index;
            return true;
        }
    }
    if (!compile->petta)
        return true;
    if (program->relation_len >= OEM_MAX_RELATIONS)
        return oem_reject(compile, "too many relations");
    if (!program->atoms_ready) {
        arena_init(&program->atoms);
        arena_set_hashcons(&program->atoms, NULL);
        program->atoms_ready = true;
    }
    Arena *atoms = &program->atoms;
    Atom *head = atom_symbol_id(atoms, program->spelling);
    Atom *fold = atom_symbol_id(atoms, foldl);
    Atom *step_atom = atom_symbol_id(atoms, step);
    Atom *cons = atom_symbol_id(atoms, symbol_intern_cstr(g_symbols, "cons"));
    Atom *x = atom_var_with_id(atoms, "x", fresh_var_id());
    Atom *xs = atom_var_with_id(atoms, "xs", fresh_var_id());
    Atom *acc = atom_var_with_id(atoms, "acc", fresh_var_id());
    Atom *nil = atom_expr(atoms, NULL, 0u);
    if (!head || !fold || !step_atom || !cons || !x || !xs || !acc || !nil)
        return oem_reject(compile, "out of memory");
    Atom *pair = atom_expr3(atoms, cons, x, xs);
    Atom *application = atom_expr3(atoms, step_atom, x, acc);
    Atom *empty_lhs = atom_expr3(atoms, head, nil, acc);
    Atom *cons_lhs = pair ? atom_expr3(atoms, head, pair, acc) : NULL;
    Atom *recursion_elems[4] = {fold, step_atom, xs, application};
    Atom *recursion = application
        ? atom_expr(atoms, recursion_elems, 4u) : NULL;
    if (!empty_lhs || !cons_lhs || !recursion)
        return oem_reject(compile, "out of memory");
    const PettaPlanNode *acc_plan =
        petta_program_plan_transient(compile->petta, acc);
    const PettaPlanNode *recursion_plan = oem_fold_recursion_plan(
        program, petta_program_plan_transient(compile->petta, recursion));
    if (!acc_plan || !recursion_plan)
        return true;
    OemRelation relation = {
        .head = SYMBOL_ID_NONE, .arity = 2u, .local = true,
        .fold_step = step, .first_source = program->source_len,
        .source_count = 2u,
    };
    uint32_t unused = 0u;
    if (!OEM_PUSH(program, source,
                  ((OemLocalSource){empty_lhs, acc, acc_plan}), &unused) ||
        !OEM_PUSH(program, source,
                  ((OemLocalSource){cons_lhs, recursion, recursion_plan}),
                  &unused) ||
        !OEM_PUSH(program, relation, relation, out))
        return oem_reject(compile, "out of memory");
    return true;
}

static bool oem_call_relation(OemCompile *compile, Atom *call,
                              uint32_t *relation_out) {
    return oem_relation_index(compile->program,
                              call->expr.elems[0]->sym_id,
                              call->expr.len - 1u, relation_out) ||
        oem_reject(compile, "too many relations");
}

static bool oem_node(OemCompile *compile, OemNode node, uint32_t *out) {
    if (!oem_reserve((void **)&compile->nodes, &compile->node_cap,
                     compile->node_len + 1u, sizeof(*compile->nodes)))
        return oem_reject(compile, "out of memory");
    compile->nodes[compile->node_len] = node;
    *out = compile->node_len++;
    return true;
}

static bool oem_node_children(OemCompile *compile, const uint32_t *children,
                              uint32_t count, uint32_t *first_out) {
    uint32_t first = compile->node_child_len;
    if (!oem_reserve((void **)&compile->node_children,
                     &compile->node_child_cap, first + count,
                     sizeof(*compile->node_children)))
        return oem_reject(compile, "out of memory");
    if (count > 0u)
        memcpy(&compile->node_children[first], children,
               sizeof(*children) * count);
    compile->node_child_len += count;
    *first_out = first;
    return true;
}

/* A fresh temporary slot as a template: an output hole. */
static bool oem_hole(OemCompile *compile, uint32_t *template_out) {
    uint32_t slot = 0u;
    return oem_temp_slot(compile, &slot) &&
        oem_template_slot(compile, slot, template_out);
}

static bool oem_build_value(OemCompile *compile, Atom *expr,
                            const PettaPlanNode *plan, uint32_t depth,
                            uint32_t *out);
static bool oem_build_tail(OemCompile *compile, Atom *expr,
                           const PettaPlanNode *plan, uint32_t depth,
                           uint32_t *out);
static bool oem_build_match(OemCompile *compile, Atom *expr,
                            const PettaPlanNode *plan, uint32_t depth,
                            bool tail, uint32_t *out);
static bool oem_build_case(OemCompile *compile, Atom *expr,
                           const PettaPlanNode *plan, uint32_t depth,
                           uint32_t *out);

/* The nodes of `expr`'s elements from `from` on, under their plans. */
static bool oem_build_elements(OemCompile *compile, Atom *expr,
                               const PettaPlanNode *plan, uint32_t depth,
                               uint32_t from, uint32_t *first_out,
                               uint32_t *count_out) {
    uint32_t count = expr->expr.len - from;
    uint32_t *children = malloc(sizeof(*children) * (count ? count : 1u));
    if (!children)
        return oem_reject(compile, "out of memory");
    bool ok = true;
    for (uint32_t index = 0u; ok && index < count; index++)
        ok = oem_build_value(compile, expr->expr.elems[from + index],
                             petta_plan_child(plan, from + index), depth + 1u,
                             &children[index]);
    ok = ok && oem_node_children(compile, children, count, first_out);
    free(children);
    *count_out = count;
    return ok;
}

/* A head no application can take: a number, a string, or the empty list.
 * PeTTa's translator keeps an expression with such a head data, its
 * elements evaluated in order (`atomic(HV), \+ atom(HV)`), which is what the
 * search machine's dispatch makes of it. */
static bool oem_inert_head(const Atom *head) {
    if (head->kind == ATOM_EXPR)
        return head->expr.len == 0u;
    if (head->kind != ATOM_GROUNDED)
        return false;
    switch (head->ground.gkind) {
    case GV_INT:
    case GV_FLOAT:
    case GV_BIGINT:
    case GV_RATIONAL:
    case GV_STRING:
        return true;
    default:
        return false;
    }
}

/* Whether a planned node is data: a constructor node, or a list whose head
 * element is itself a data node.  The plan calls the latter a dynamic call,
 * because a head that evaluates to a symbol would be dispatched; a data
 * head keeps its constructor after evaluation, so the node stays data unless
 * that constructor is applied (a lambda or a partial application). */
static bool oem_data_node(Atom *expr, const PettaPlanNode *plan) {
    for (;;) {
        Atom *head = expr->expr.elems[0];
        if (head->kind == ATOM_SYMBOL)
            return plan->role == PETTA_PLAN_DATA &&
                plan->control == PETTA_PLAN_CONTROL_NONE &&
                petta_semantics_form(head->sym_id) == PETTA_FORM_NONE;
        if (plan->role != PETTA_PLAN_DYNAMIC_CALL ||
            plan->control != PETTA_PLAN_CONTROL_NONE)
            return false;
        if (oem_inert_head(head))
            return true;
        const PettaPlanNode *head_plan = petta_plan_child(plan, 0u);
        Atom *body = NULL;
        if (head->kind != ATOM_EXPR || head->expr.len == 0u || !head_plan ||
            petta_semantics_lambda_body(head, &body) ||
            petta_semantics_nullary_lambda_body(head, &body) ||
            petta_semantics_partial_view(head, NULL, NULL) ||
            petta_semantics_partial_head(head))
            return false;
        expr = head;
        plan = head_plan;
    }
}

/* Whether the host may evaluate `expr` as one goal: a cut or a `return`
 * inside it would act on the enclosing equation, which the host does not
 * see, so such an expression stays outside the tier. */
static bool oem_host_goal_admitted(Atom *expr, uint32_t depth) {
    if (depth > OEM_MAX_DEPTH)
        return false;
    if (expr->kind != ATOM_EXPR || expr->expr.len == 0u)
        return true;
    Atom *head = expr->expr.elems[0];
    if (head->kind == ATOM_SYMBOL &&
        (petta_semantics_form(head->sym_id) == PETTA_FORM_CUT ||
         head->sym_id == g_builtin_syms.return_text))
        return false;
    for (CettaExprIndex child = 0u; child < expr->expr.len; child++) {
        if (!oem_host_goal_admitted(expr->expr.elems[child], depth + 1u))
            return false;
    }
    return true;
}

/* Whether the plan takes every field of `expr` from `first` on as a value:
 * a variable's value or call-free data. */
static bool oem_fields_are_values(Atom *expr, const PettaPlanNode *plan,
                                  CettaExprIndex first) {
    for (CettaExprIndex index = first; index < expr->expr.len; index++) {
        const PettaPlanNode *field = petta_plan_child(plan, index);
        if (!field ||
            (field->role != PETTA_PLAN_VALUE &&
             !(field->role == PETTA_PLAN_DATA && !field->contains_call)))
            return false;
    }
    return true;
}

/* A type-pure grounded operation the language offers that is no PeTTa form:
 * the region may apply it to its arguments' values. */
static bool oem_pure_operation(const OemCompile *compile, SymbolId head) {
    return petta_semantics_grounded_type_pure(head) &&
        compile->host && compile->host->builtin_allowed &&
        compile->host->builtin_allowed(compile->host->context, head);
}

/* PeTTa's list natives the region computes over closed lists, as the
 * search machine does once their arguments are values.  `union-atom` is
 * PeTTa's append/3. */
static bool oem_list_native(SymbolId head, CettaExprLen nargs) {
    PeTTaForm form = petta_semantics_form(head);
    return ((form == PETTA_FORM_APPEND ||
             head == g_builtin_syms.union_atom ||
             form == PETTA_FORM_EXCLUDE_ITEM) && nargs == 2u) ||
        ((form == PETTA_FORM_LENGTH ||
          form == PETTA_FORM_LIST_TO_SET ||
          form == PETTA_FORM_ALPHA_UNIQUE ||
          head == g_builtin_syms.unique_atom ||
          head == g_builtin_syms.car_atom ||
          head == g_builtin_syms.cdr_atom ||
          head == g_builtin_syms.decons_atom ||
          head == g_builtin_syms.petta_decons) && nargs == 1u);
}

/* PeTTa's `get-metatype`, which the search machine applies to its
 * argument's value, where the profile offers it. */
static bool oem_metatype_call(const OemCompile *compile, const Atom *expr,
                              const PettaPlanNode *plan) {
    return expr->expr.len == 2u &&
        atom_is_symbol_id(expr->expr.elems[0], g_builtin_syms.get_metatype) &&
        plan->role == PETTA_PLAN_STATIC_CALL && compile->host &&
        compile->host->builtin_allowed &&
        compile->host->builtin_allowed(compile->host->context,
                                       g_builtin_syms.get_metatype);
}

/* The template of an existence observer's match, when it is one value the
 * observation can ignore: an atomic constant, as the search machine
 * requires before it answers the observer by membership. */
static bool oem_existence_constant(const Atom *expr) {
    const Atom *observed =
        expr->expr.elems[1]->kind == ATOM_EXPR &&
                expr->expr.elems[1]->expr.len == 0u
            ? expr->expr.elems[2] : expr->expr.elems[1];
    const Atom *match = observed->expr.elems[1]->expr.elems[1];
    Atom *template = match->expr.elems[3];
    return !atom_has_vars(template) && !atom_is_error(template) &&
        (template->kind == ATOM_SYMBOL ||
         (template->kind == ATOM_GROUNDED &&
          term_universe_atom_is_stable(template)));
}

/* What the region may decide of a goal before its host.  A type-pure
 * grounded operation, which the language offers and which is no PeTTa form
 * (a form takes its own dialect's evaluation), over arguments the plan
 * takes as values: the search machine runs such a call directly once its
 * arguments are values.  An expression over values whose head is a
 * variable: an application of values, which is data when the head's value
 * cannot be applied, as the search machine decides for any dynamic call's
 * evaluated elements.  PeTTa's `sort-atom` and `msort`, whose
 * standard-order sort the region computes with the dialect's own
 * definition. */
static uint32_t oem_host_fast_path(const OemCompile *compile, Atom *expr,
                                   const PettaPlanNode *plan,
                                   SymbolId *op_out) {
    *op_out = SYMBOL_ID_NONE;
    if (expr->kind != ATOM_EXPR || expr->expr.len == 0u)
        return OEM_HOST_PLAIN;
    if (plan->role == PETTA_PLAN_DYNAMIC_CALL &&
        expr->expr.elems[0]->kind == ATOM_VAR &&
        oem_fields_are_values(expr, plan, 1u))
        return OEM_HOST_DATA_HEAD;
    if (expr->expr.len == 3u &&
        atom_is_symbol_id(expr->expr.elems[0], g_builtin_syms.add_atom) &&
        oem_fields_are_values(expr, plan, 1u) &&
        compile->host && compile->host->builtin_allowed &&
        compile->host->builtin_allowed(compile->host->context,
                                       g_builtin_syms.add_atom))
        return OEM_HOST_ADMIT;
    if (expr->expr.elems[0]->kind == ATOM_SYMBOL && expr->expr.len == 2u &&
        petta_semantics_form(expr->expr.elems[0]->sym_id) ==
            PETTA_FORM_MSORT &&
        plan->role == PETTA_PLAN_STATIC_CALL &&
        oem_fields_are_values(expr, plan, 1u)) {
        *op_out = expr->expr.elems[0]->sym_id;
        return OEM_HOST_SORT;
    }
    if (expr->expr.elems[0]->kind == ATOM_SYMBOL &&
        oem_list_native(expr->expr.elems[0]->sym_id, expr->expr.len - 1u) &&
        plan->role == PETTA_PLAN_STATIC_CALL &&
        oem_fields_are_values(expr, plan, 1u)) {
        *op_out = expr->expr.elems[0]->sym_id;
        return OEM_HOST_LIST;
    }
    if (oem_metatype_call(compile, expr, plan) &&
        oem_fields_are_values(expr, plan, 1u)) {
        *op_out = g_builtin_syms.get_metatype;
        return OEM_HOST_METATYPE;
    }
    /* The observation whether a match has a row, which the search machine
     * answers by membership where the space proves ground matching exact:
     * the region asks the space the same question, and leaves the goal
     * whole to the host where it cannot. */
    if (petta_semantics_match_existence_observer_shape(
            expr, compile->host ? compile->host->reify_head
                                : SYMBOL_ID_NONE) &&
        oem_existence_constant(expr))
        return OEM_HOST_EXISTS;
    /* PeTTa's `repra`, SWI's term_to_atom, is the language's grounded
     * rendering of its argument's value: a ground value renders here, the
     * symbol the host renders, and one with variables, whose names are the
     * host's to choose, stays the host's. */
    if (expr->expr.elems[0]->kind == ATOM_SYMBOL && expr->expr.len == 2u &&
        petta_semantics_form(expr->expr.elems[0]->sym_id) ==
            PETTA_FORM_REPRA &&
        plan->role == PETTA_PLAN_STATIC_CALL &&
        oem_fields_are_values(expr, plan, 1u)) {
        *op_out = expr->expr.elems[0]->sym_id;
        return OEM_HOST_PURE;
    }
    if (expr->expr.elems[0]->kind != ATOM_SYMBOL ||
        plan->role != PETTA_PLAN_STATIC_CALL ||
        plan->execution != PETTA_PLAN_EXEC_PURE_GROUNDED_SLOTS)
        return OEM_HOST_PLAIN;
    SymbolId head = expr->expr.elems[0]->sym_id;
    if (!oem_pure_operation(compile, head) ||
        !oem_fields_are_values(expr, plan, 1u))
        return OEM_HOST_PLAIN;
    *op_out = head;
    return OEM_HOST_PURE;
}

/* An operation outside the fragment, as a goal for the host: the whole
 * expression over the equation's variables, whose value fills a hole. */
static bool oem_build_host_goal(OemCompile *compile, Atom *expr,
                                const PettaPlanNode *plan, uint32_t depth,
                                bool counted, const char *reason,
                                uint32_t *out) {
    if (!plan || !oem_host_goal_admitted(expr, 0u))
        return oem_reject(compile, reason);
    CettaOpenEquationProgram *program = compile->program;
    if (!oem_reserve((void **)&program->host_plans, &program->host_plan_cap,
                     program->host_plan_len + 1u,
                     sizeof(*program->host_plans)))
        return oem_reject(compile, "out of memory");
    OemNode node = {.kind = OEM_N_HOST, .relation = program->host_plan_len};
    node.host_fast = counted
        ? (uint8_t)OEM_HOST_COUNTED
        : (uint8_t)oem_host_fast_path(compile, expr, plan, &node.op);
    program->host_plans[program->host_plan_len++] = plan;
    return oem_lower_pattern(compile, expr, NULL, depth + 1u, false,
                             &node.value) &&
        oem_hole(compile, &node.exposed) &&
        oem_node(compile, node, out);
}

static bool oem_build_host(OemCompile *compile, Atom *expr,
                           const PettaPlanNode *plan, uint32_t depth,
                           const char *reason, uint32_t *out) {
    return oem_build_host_goal(compile, expr, plan, depth, false, reason,
                               out);
}

/* How often variable `id` occurs in `atom`. */
static uint64_t oem_var_occurrences(const Atom *atom, VarId id,
                                    uint32_t depth) {
    if (!atom || depth > OEM_MAX_DEPTH)
        return UINT64_MAX / 4u;
    if (atom->kind == ATOM_VAR)
        return atom->var_id == id ? 1u : 0u;
    if (atom->kind != ATOM_EXPR || !atom_has_vars(atom))
        return 0u;
    uint64_t total = 0u;
    for (CettaExprIndex child = 0u; child < atom->expr.len; child++)
        total += oem_var_occurrences(atom->expr.elems[child], id, depth + 1u);
    return total;
}

static bool oem_count_consumer(void *context, SymbolId head) {
    const OemCompile *compile = context;
    PeTTaCountConsumer consumer = petta_semantics_count_consumer(head);
    return consumer == PETTA_COUNT_CONSUMER_FORM ||
        (consumer == PETTA_COUNT_CONSUMER_BUILTIN &&
         (!compile->host->builtin_allowed ||
          compile->host->builtin_allowed(compile->host->context, head)));
}

static bool oem_count_use_children_executable(void *context, SymbolId head) {
    (void)context;
    return petta_semantics_count_use_children_executable(head);
}

/* Whether a let's binder is an intermediate only counting operations read,
 * which the search machine's let/count fusion represents by its producer's
 * count: a variable that occurs nowhere else in the equation, absent from
 * its producer, and read by at least one counting operation in the
 * bindings that follow (`rest`) and the body, and by nothing else there. */
static bool oem_count_only_binder(OemCompile *compile, Atom *binder,
                                  Atom *producer, Atom *const *rest,
                                  uint32_t rest_count, Atom *body,
                                  bool may_count) {
    if (!compile->host || !compile->host->count_fusion || !may_count ||
        !binder || binder->kind != ATOM_VAR ||
        binder->var_id == VAR_ID_NONE ||
        cetta_observation_atom_contains_var(producer, binder->var_id))
        return false;
    uint64_t uses = 0u;
    for (uint32_t index = 0u; index < rest_count; index++) {
        if (!cetta_observation_variable_uses_only_unary_consumers(
                rest[index], binder->var_id, oem_count_consumer,
                oem_count_use_children_executable, compile, &uses))
            return false;
    }
    if (!cetta_observation_variable_uses_only_unary_consumers(
            body, binder->var_id, oem_count_consumer,
            oem_count_use_children_executable, compile, &uses) ||
        uses == 0u)
        return false;
    return oem_var_occurrences(compile->lhs, binder->var_id, 0u) +
               oem_var_occurrences(compile->rhs, binder->var_id, 0u) ==
           uses + 1u;
}

/* Whether a node's output is a hole of its own, made for it alone: a call's,
 * a primitive's, a host goal's or an application's. */
static bool oem_output_is_hole(const OemNode *node) {
    return node->kind == OEM_N_CALL || node->kind == OEM_N_PRIM ||
        node->kind == OEM_N_HOST || node->kind == OEM_N_APPLY ||
        node->kind == OEM_N_COLLECT || node->kind == OEM_N_ALTERNATIVES ||
        node->kind == OEM_N_ONCE || node->kind == OEM_N_CUT;
}

/* A let whose pattern is one variable meets its value's output hole before
 * the value's goals run, which only makes the two one variable; the value
 * then fills the variable's slot itself. */
static void oem_let_fills_pattern(OemCompile *compile, uint32_t pattern,
                                  uint32_t value) {
    OemNode *node = &compile->nodes[value];
    if (compile->program->templates[pattern].kind == OEM_T_SLOT &&
        oem_output_is_hole(node))
        node->exposed = pattern;
}

/* A let's producer: a counted host goal when its binder is count-only,
 * else a value. */
static bool oem_build_producer(OemCompile *compile, Atom *expr,
                               const PettaPlanNode *plan, uint32_t depth,
                               bool counted, uint32_t *out) {
    return counted
        ? oem_build_host_goal(compile, expr, plan, depth, true,
                              "counted producer", out)
        : oem_build_value(compile, expr, plan, depth, out);
}

/* A dynamic call whose head is itself an occurrence the plan calls, or a
 * variable: the search machine evaluates every element in order, then
 * applies the head's value to the others or keeps them as data.  The region
 * evaluates the elements; the head's value decides the rest when the
 * application runs (oem_apply_step). */
static bool oem_apply_call(Atom *expr, const PettaPlanNode *plan) {
    const PettaPlanNode *head_plan = petta_plan_child(plan, 0u);
    Atom *head = expr->expr.elems[0];
    return plan->role == PETTA_PLAN_DYNAMIC_CALL &&
        plan->control == PETTA_PLAN_CONTROL_NONE &&
        plan->child_count == expr->expr.len && head_plan &&
        ((head->kind == ATOM_EXPR &&
          (head_plan->role == PETTA_PLAN_STATIC_CALL ||
           head_plan->role == PETTA_PLAN_DYNAMIC_CALL)) ||
         (head->kind == ATOM_VAR && head_plan->role == PETTA_PLAN_VALUE));
}

/* A written call whose children the region has already evaluated keeps
 * its static dispatch and handler scope. Only its children become values;
 * treating the whole call as a dynamic application would invent a handler
 * and turn an ordinary raised error into failure. Build this once with the
 * program, without keeping source-tree optimizations for the new shape. */
static const PettaPlanNode *oem_ready_static_plan(
    CettaOpenEquationProgram *program, const PettaPlanNode *source) {
    if (!program->atoms_ready) {
        arena_init(&program->atoms);
        arena_set_hashcons(&program->atoms, NULL);
        program->atoms_ready = true;
    }
    if (!cetta_expr_len_mul_fits_size(source->child_count,
                                     sizeof(PettaPlanNode)))
        return NULL;
    PettaPlanNode *root = arena_alloc(&program->atoms, sizeof(*root));
    PettaPlanNode *children = arena_alloc(
        &program->atoms, sizeof(*children) * (size_t)source->child_count);
    if (!root || !children)
        return NULL;
    memset(children, 0, sizeof(*children) * (size_t)source->child_count);
    *root = (PettaPlanNode){
        .role = PETTA_PLAN_STATIC_CALL,
        .execution = source->execution,
        .contains_call = true,
        .dispatch_handler = source->dispatch_handler,
        .child_count = source->child_count,
        .children = children,
    };
    return root;
}

static bool oem_build_apply(OemCompile *compile, Atom *expr,
                            const PettaPlanNode *plan, uint32_t depth,
                            uint32_t *out) {
    CettaOpenEquationProgram *program = compile->program;
    OemNode node = {.kind = OEM_N_APPLY, .relation = program->host_plan_len,
                    .recovers = plan->dispatch_handler};
    /* PeTTa's sort takes its argument's value once; applying the head to
     * that value keeps the host from evaluating it again. */
    if (expr->expr.len == 2u && expr->expr.elems[0]->kind == ATOM_SYMBOL &&
        petta_semantics_form(expr->expr.elems[0]->sym_id) ==
            PETTA_FORM_MSORT) {
        node.host_fast = (uint8_t)OEM_HOST_SORT;
        node.op = expr->expr.elems[0]->sym_id;
    } else if (expr->expr.elems[0]->kind == ATOM_SYMBOL &&
               oem_list_native(expr->expr.elems[0]->sym_id,
                               expr->expr.len - 1u)) {
        /* So do PeTTa's list natives. */
        node.host_fast = (uint8_t)OEM_HOST_LIST;
        node.op = expr->expr.elems[0]->sym_id;
    } else if (oem_metatype_call(compile, expr, plan)) {
        /* And `get-metatype`. */
        node.host_fast = (uint8_t)OEM_HOST_METATYPE;
        node.op = g_builtin_syms.get_metatype;
    } else if (expr->expr.elems[0]->kind == ATOM_SYMBOL &&
               oem_pure_operation(compile, expr->expr.elems[0]->sym_id)) {
        /* A pure operation over computed arguments: the arguments' goals
         * run first, and the operation applies to their values. */
        node.host_fast = (uint8_t)OEM_HOST_PURE;
        node.op = expr->expr.elems[0]->sym_id;
    }
    if (!oem_reserve((void **)&program->host_plans, &program->host_plan_cap,
                     program->host_plan_len + 1u,
                     sizeof(*program->host_plans)))
        return oem_reject(compile, "out of memory");
    const PettaPlanNode *ready = plan->role == PETTA_PLAN_STATIC_CALL
        ? oem_ready_static_plan(program, plan) : NULL;
    if (plan->role == PETTA_PLAN_STATIC_CALL && !ready)
        return oem_reject(compile, "out of memory");
    program->host_plans[program->host_plan_len++] = ready;
    if (!oem_build_elements(compile, expr, plan, depth, 0u, &node.first,
                            &node.count))
        return false;
    uint32_t *children = malloc(sizeof(*children) * node.count);
    if (!children)
        return oem_reject(compile, "out of memory");
    for (uint32_t index = 0u; index < node.count; index++)
        children[index] = compile->nodes[
            compile->node_children[node.first + index]].exposed;
    bool ok = oem_template_build(compile, children, node.count, &node.value);
    free(children);
    return ok && oem_hole(compile, &node.exposed) &&
        oem_node(compile, node, out);
}

static bool oem_build_value(OemCompile *compile, Atom *expr,
                            const PettaPlanNode *plan, uint32_t depth,
                            uint32_t *out);

/* The distinct variables of `body`, in order of first occurrence, that
 * also occur in the rest of the equation: a local relation's parameters.
 * The body's other variables are its own. */
static bool oem_shared_vars(OemCompile *compile, Atom *body, Atom ***out,
                            uint32_t *count_out) {
    Atom **vars = NULL;
    uint32_t len = 0u;
    uint32_t cap = 0u;
    Atom **walk = NULL;
    uint32_t walk_len = 0u;
    uint32_t walk_cap = 0u;
    bool ok = oem_reserve((void **)&walk, &walk_cap, 1u, sizeof(*walk));
    if (ok)
        walk[walk_len++] = body;
    while (ok && walk_len > 0u) {
        Atom *atom = walk[--walk_len];
        if (atom->kind == ATOM_VAR) {
            bool seen = false;
            for (uint32_t index = 0u; !seen && index < len; index++)
                seen = vars[index]->var_id == atom->var_id;
            if (seen)
                continue;
            uint64_t inside = oem_var_occurrences(body, atom->var_id, 0u);
            uint64_t all = oem_var_occurrences(compile->lhs, atom->var_id,
                                               0u) +
                oem_var_occurrences(compile->rhs, atom->var_id, 0u);
            if (all > inside) {
                ok = oem_reserve((void **)&vars, &cap, len + 1u,
                                 sizeof(*vars));
                if (ok)
                    vars[len++] = atom;
            }
            continue;
        }
        if (atom->kind != ATOM_EXPR || !atom_has_vars(atom))
            continue;
        ok = oem_reserve((void **)&walk, &walk_cap,
                         walk_len + (uint32_t)atom->expr.len, sizeof(*walk));
        for (CettaExprIndex child = atom->expr.len; ok && child-- > 0u;)
            walk[walk_len++] = atom->expr.elems[child];
    }
    free(walk);
    if (!ok) {
        free(vars);
        return oem_reject(compile, "out of memory");
    }
    *out = vars;
    *count_out = len;
    return true;
}

/* A local relation with one equation per body, over the parameters
 * `params`; it is compiled after the equation that calls it. */
static bool oem_local_relation(OemCompile *compile, Atom **params,
                               uint32_t param_count, Atom *const *bodies,
                               const PettaPlanNode *const *plans,
                               uint32_t body_count, uint32_t *out) {
    CettaOpenEquationProgram *program = compile->program;
    if (program->relation_len >= OEM_MAX_RELATIONS)
        return oem_reject(compile, "too many relations");
    if (!program->atoms_ready) {
        arena_init(&program->atoms);
        arena_set_hashcons(&program->atoms, NULL);
        program->atoms_ready = true;
    }
    Atom **elems = arena_alloc(&program->atoms,
                               sizeof(*elems) * (param_count + 1u));
    elems[0] = atom_symbol_id(&program->atoms, program->spelling);
    for (uint32_t index = 0u; index < param_count; index++)
        elems[index + 1u] = params[index];
    Atom *lhs = atom_expr(&program->atoms, elems, param_count + 1u);
    if (!lhs || !elems[0])
        return oem_reject(compile, "out of memory");
    OemRelation relation = {
        .head = SYMBOL_ID_NONE, .arity = param_count, .local = true,
        .first_source = program->source_len, .source_count = body_count,
    };
    for (uint32_t index = 0u; index < body_count; index++) {
        uint32_t unused = 0u;
        if (!OEM_PUSH(program, source,
                      ((OemLocalSource){lhs, bodies[index], plans[index]}),
                      &unused))
            return oem_reject(compile, "out of memory");
    }
    return OEM_PUSH(program, relation, relation, out) ||
        oem_reject(compile, "out of memory");
}

/* The node of a control the region runs through a local relation: its
 * arguments are the shared variables, and its goal and plan remain the
 * host's should the relation not compile. */
static bool oem_build_local_control(OemCompile *compile, uint8_t kind,
                                    Atom *expr, const PettaPlanNode *plan,
                                    uint32_t depth, Atom *const *bodies,
                                    const PettaPlanNode *const *plans,
                                    uint32_t body_count, Atom *scope,
                                    uint32_t *out) {
    CettaOpenEquationProgram *program = compile->program;
    /* Its goal stays the host's should the body not compile, so it is a
     * control the host goal protocol admits. */
    if (!plan || !oem_host_goal_admitted(expr, 0u))
        return oem_reject(compile, "control outside the fragment");
    Atom **params = NULL;
    uint32_t count = 0u;
    if (!oem_shared_vars(compile, scope, &params, &count))
        return false;
    OemNode node = {.kind = kind};
    uint32_t *children = malloc(sizeof(*children) * (count ? count : 1u));
    bool ok = children != NULL ||
        oem_reject(compile, "out of memory");
    ok = ok && oem_local_relation(compile, params, count, bodies, plans,
                                  body_count, &node.local);
    for (uint32_t index = 0u; ok && index < count; index++)
        ok = oem_build_value(compile, params[index], NULL, depth + 1u,
                             &children[index]);
    ok = ok && oem_node_children(compile, children, count, &node.first);
    node.count = count;
    free(children);
    free(params);
    ok = ok && (oem_reserve((void **)&program->host_plans,
                            &program->host_plan_cap,
                            program->host_plan_len + 1u,
                            sizeof(*program->host_plans)) ||
                oem_reject(compile, "out of memory"));
    if (ok) {
        node.relation = program->host_plan_len;
        program->host_plans[program->host_plan_len++] = plan;
    }
    return ok &&
        oem_lower_pattern(compile, expr, NULL, depth + 1u, false,
                          &node.value) &&
        oem_hole(compile, &node.exposed) &&
        (kind != OEM_N_COLLECT || oem_hole(compile, &node.yield)) &&
        oem_node(compile, node, out);
}

/* Whether `expr` is a control the region runs: PeTTa's `collapse`,
 * `superpose` and `once`; the host's materializer; and, where the host's
 * machine runs them and the profile offers them, `collect` and `select`. */
static bool oem_control_form(const OemCompile *compile, const Atom *expr) {
    SymbolId head = expr->expr.elems[0]->sym_id;
    uint32_t len = (uint32_t)expr->expr.len;
    if (len == 2u &&
        (head == g_builtin_syms.collapse || head == g_builtin_syms.superpose ||
         head == g_builtin_syms.once ||
         (compile->host && head == compile->host->reify_head)))
        return true;
    return compile->host && compile->host->bounded_collections &&
        ((head == g_builtin_syms.collect && len == 2u) ||
         (head == g_builtin_syms.select && (len == 2u || len == 3u))) &&
        compile->host->builtin_allowed &&
        compile->host->builtin_allowed(compile->host->context, head);
}

/* `(collapse body)` is PeTTa's findall over the body; `(superpose items)`
 * over a literal tuple is the disjunction of its items, and over any other
 * value is member/2 of that value; `(once body)` is once/1 of the body, as
 * PeTTa's translation reads them.  The materializer and `collect` are
 * collapse.  `(select body)` and `(select 1 body)` are once; `(select k
 * body)` for a literal k of two or more is the first k answers, collapse
 * with a bound.  Any other bound is the host's to read.  A collection only
 * counted keeps the host's counted route. */
static bool oem_build_control(OemCompile *compile, Atom *expr,
                              const PettaPlanNode *plan, uint32_t depth,
                              uint32_t *out) {
    SymbolId head = expr->expr.elems[0]->sym_id;
    uint32_t limit = 0u;
    if (head == g_builtin_syms.select && expr->expr.len == 3u) {
        Atom *bound = expr->expr.elems[1];
        if (bound->kind != ATOM_GROUNDED || bound->ground.gkind != GV_INT ||
            bound->ground.ival < 1)
            return oem_build_host(compile, expr, plan, depth,
                                  "select bound outside the fragment", out);
        /* Keep the bound meaningful even when this region's collection
         * counter cannot represent it. The host retains the full demand. */
        if ((uint64_t)bound->ground.ival > UINT32_MAX)
            return oem_build_host(compile, expr, plan, depth,
                                  "select bound outside the collection counter", out);
        limit = (uint32_t)bound->ground.ival;
        head = limit == 1u ? g_builtin_syms.once : g_builtin_syms.collapse;
    } else if (head == g_builtin_syms.select) {
        head = g_builtin_syms.once;
    } else if (head != g_builtin_syms.superpose &&
               head != g_builtin_syms.once) {
        head = g_builtin_syms.collapse;
    }
    CettaExprIndex body_index = (CettaExprIndex)(expr->expr.len - 1u);
    Atom *argument = expr->expr.elems[body_index];
    const PettaPlanNode *argument_plan = petta_plan_child(plan, body_index);
    if (!argument_plan)
        return oem_build_host(compile, expr, plan, depth,
                              "control without a plan", out);
    if (head == g_builtin_syms.collapse || head == g_builtin_syms.once) {
        bool ok = oem_build_local_control(
            compile,
            head == g_builtin_syms.collapse ? OEM_N_COLLECT : OEM_N_ONCE,
            expr, plan, depth, &argument, &argument_plan, 1u, argument, out);
        if (ok && limit > 1u)
            compile->nodes[*out].limit = limit;
        return ok;
    }
    if (argument_plan->role == PETTA_PLAN_VALUE) {
        if (!oem_build_host_goal(compile, expr, plan, depth, false,
                                 "superpose", out))
            return false;
        compile->nodes[*out].host_fast = (uint8_t)OEM_HOST_ELEMENTS;
        return true;
    }
    if (argument->kind != ATOM_EXPR ||
        argument_plan->child_count != argument->expr.len)
        return oem_build_host(compile, expr, plan, depth,
                              "superpose outside the fragment", out);
    uint32_t count = (uint32_t)argument->expr.len;
    if (count == 0u) {
        OemNode node = {.kind = OEM_N_FAIL};
        return oem_hole(compile, &node.exposed) &&
            oem_node(compile, node, out);
    }
    const PettaPlanNode **plans = malloc(sizeof(*plans) * count);
    if (!plans)
        return oem_reject(compile, "out of memory");
    for (uint32_t index = 0u; index < count; index++)
        plans[index] = petta_plan_child(argument_plan, index);
    bool ok = oem_build_local_control(compile, OEM_N_ALTERNATIVES, expr,
                                      plan, depth, argument->expr.elems,
                                      plans, count, argument, out);
    free(plans);
    return ok;
}

/* A value position: a variable, a literal, a constructor over values, a
 * relation call or a primitive. */
static bool oem_build_value(OemCompile *compile, Atom *expr,
                            const PettaPlanNode *plan, uint32_t depth,
                            uint32_t *out) {
    if (!expr || depth > OEM_MAX_DEPTH)
        return oem_reject(compile, "expression too deep");
    OemNode node = {.kind = OEM_N_VALUE};
    if (expr->kind == ATOM_VAR) {
        uint32_t slot = 0u;
        return oem_slot_of(compile, expr->var_id, &slot) &&
            oem_template_slot(compile, slot, &node.exposed) &&
            oem_node(compile, node, out);
    }
    if (expr->kind != ATOM_EXPR || expr->expr.len == 0u)
        return oem_template_literal(compile, expr, &node.exposed) &&
            oem_node(compile, node, out);
    if (!plan)
        return oem_reject(compile, "occurrence has no plan");
    bool symbol_head = expr->expr.elems[0]->kind == ATOM_SYMBOL;
    if (symbol_head && expr->expr.len == 4u &&
        expr->expr.elems[0]->sym_id == g_builtin_syms.match)
        return oem_build_match(compile, expr, plan, depth, false, out);
    /* PeTTa's `(cut)` is Prolog's `!`, as its translation reads it, whatever
     * equations the program has for `cut`: in an equation's own body it
     * commits the call to that equation, and its value is the truth
     * value. */
    if (symbol_head && expr->expr.len == 1u &&
        petta_semantics_form(expr->expr.elems[0]->sym_id) == PETTA_FORM_CUT) {
        if (!compile->named)
            return oem_reject(compile, "cut inside a control");
        node.kind = OEM_N_CUT;
        return oem_hole(compile, &node.exposed) &&
            oem_node(compile, node, out);
    }
    /* PeTTa's `cons`, and `cons-atom`, which PeTTa defines the same way:
     * the list whose first element is its first argument's value and whose
     * rest is its second's, as a cons cell, as the host's machine builds
     * both. */
    if (symbol_head && expr->expr.len == 3u &&
        plan->role == PETTA_PLAN_STATIC_CALL &&
        (petta_semantics_form(expr->expr.elems[0]->sym_id) ==
             PETTA_FORM_CONS ||
         expr->expr.elems[0]->sym_id == g_builtin_syms.cons_atom)) {
        if (!oem_build_elements(compile, expr, plan, depth, 1u, &node.first,
                                &node.count))
            return false;
        const uint32_t *fields = &compile->node_children[node.first];
        return oem_template_list(compile, OEM_T_LIST,
                                 compile->nodes[fields[0]].exposed,
                                 compile->nodes[fields[1]].exposed,
                                 &node.exposed) &&
            oem_node(compile, node, out);
    }
    /* PeTTa's foldl over a relation the region may call runs as foldl/4's
     * clauses for that step (oem_fold_relation). */
    if (symbol_head && expr->expr.len == 4u &&
        plan->role == PETTA_PLAN_STATIC_CALL &&
        petta_semantics_form(expr->expr.elems[0]->sym_id) ==
            PETTA_FORM_FOLDL &&
        expr->expr.elems[1]->kind == ATOM_SYMBOL &&
        petta_semantics_form(expr->expr.elems[1]->sym_id) ==
            PETTA_FORM_NONE &&
        compile->host && compile->host->relation_admitted &&
        compile->host->relation_admitted(
            compile->host->context, compile->program->space,
            expr->expr.elems[1]->sym_id, 2u)) {
        uint32_t fold = UINT32_MAX;
        if (!oem_fold_relation(compile, expr->expr.elems[0]->sym_id,
                               expr->expr.elems[1]->sym_id, &fold))
            return false;
        if (fold != UINT32_MAX) {
            node.kind = OEM_N_CALL;
            node.relation = fold;
            return oem_build_elements(compile, expr, plan, depth, 2u,
                                      &node.first, &node.count) &&
                oem_hole(compile, &node.exposed) &&
                oem_node(compile, node, out);
        }
    }
    if (symbol_head && oem_control_form(compile, expr))
        return oem_build_control(compile, expr, plan, depth, out);
    /* PeTTa's `unique` and `alpha-unique` are their translations, as
     * PeTTa's translator rewrites them: `(unique X)` is
     * `(superpose (unique-atom (collapse X)))`.  Here the list is bound by a
     * let to a fresh variable, which a relational let's pattern evaluates
     * to itself, so the collection and the aggregate are the region's and
     * only the enumeration of the list remains: the whole runs as a local
     * relation, so X's answers are collected here, not by the host.
     * Should the relation not compile, the host evaluates the form. */
    if (symbol_head && compile->petta && expr->expr.len == 2u) {
        PeTTaForm stream = petta_semantics_form(expr->expr.elems[0]->sym_id);
        if (stream == PETTA_FORM_STREAM_UNIQUE ||
            stream == PETTA_FORM_STREAM_ALPHA_UNIQUE) {
            CettaOpenEquationProgram *program = compile->program;
            if (!program->atoms_ready) {
                arena_init(&program->atoms);
                arena_set_hashcons(&program->atoms, NULL);
                program->atoms_ready = true;
            }
            Arena *atoms = &program->atoms;
            SymbolId reify = compile->host &&
                    compile->host->reify_head != SYMBOL_ID_NONE
                ? compile->host->reify_head : g_builtin_syms.collapse;
            SymbolId aggregate = stream == PETTA_FORM_STREAM_UNIQUE
                ? g_builtin_syms.unique_atom
                : symbol_intern_cstr(g_symbols, "alpha-unique-atom");
            Atom *items = atom_var_with_id(atoms, "__stream_items",
                                           fresh_var_id());
            Atom *collected = items
                ? atom_expr2(atoms, atom_symbol_id(atoms, reify),
                             expr->expr.elems[1]) : NULL;
            Atom *unique = collected
                ? atom_expr2(atoms, atom_symbol_id(atoms, aggregate),
                             collected) : NULL;
            Atom *emit = unique
                ? atom_expr2(atoms,
                             atom_symbol_id(atoms, g_builtin_syms.superpose),
                             items) : NULL;
            Atom *let_elems[4] = {
                atom_symbol_id(atoms, g_builtin_syms.let), items, unique,
                emit,
            };
            Atom *lowered = emit ? atom_expr(atoms, let_elems, 4u) : NULL;
            const PettaPlanNode *lowered_plan = lowered
                ? petta_program_plan_current(compile->petta, lowered)
                : NULL;
            if (lowered_plan &&
                lowered_plan->control == PETTA_PLAN_CONTROL_LET)
                return oem_build_local_control(compile, OEM_N_ALTERNATIVES,
                                               expr, plan, depth, &lowered,
                                               &lowered_plan, 1u, expr, out);
        }
    }
    /* An `if` whose output is a value runs as a local relation whose one
     * equation is the `if` in a tail position: each of its answers
     * continues after it, as each of the host's does. */
    if (symbol_head && plan->control == PETTA_PLAN_CONTROL_IF &&
        expr->expr.len == 4u)
        return oem_build_local_control(compile, OEM_N_ALTERNATIVES, expr,
                                       plan, depth, &expr, &plan, 1u, expr,
                                       out);
    if (symbol_head && oem_relation_call(expr, plan) &&
        compile->host && compile->host->relation_admitted &&
        !compile->host->relation_admitted(
            compile->host->context, compile->program->space,
            expr->expr.elems[0]->sym_id, expr->expr.len - 1u))
        return oem_build_host(compile, expr, plan, depth,
                              "relation owned by the host", out);
    if (symbol_head && oem_relation_call(expr, plan)) {
        node.kind = OEM_N_CALL;
        node.recovers = plan->dispatch_handler;
        return oem_build_elements(compile, expr, plan, depth, 1u,
                                  &node.first, &node.count) &&
            oem_call_relation(compile, expr, &node.relation) &&
            oem_hole(compile, &node.exposed) &&
            oem_node(compile, node, out);
    }
    if (symbol_head && oem_prim_call(expr, plan)) {
        node.kind = OEM_N_PRIM;
        node.op = expr->expr.elems[0]->sym_id;
        return oem_build_elements(compile, expr, plan, depth, 1u,
                                  &node.first, &node.count) &&
            oem_hole(compile, &node.exposed) &&
            oem_node(compile, node, out);
    }
    /* An equality that observes whether a match has a row stays whole, as
     * a condition does: the host answers it by membership.  A list native,
     * or `get-metatype`, over computed arguments applies to their values,
     * which the region computes, collections included; a counted `length`
     * keeps the host's counted route. */
    bool computed_call = symbol_head &&
        plan->role == PETTA_PLAN_STATIC_CALL &&
        !oem_fields_are_values(expr, plan, 1u);
    bool value_native = computed_call &&
        ((oem_list_native(expr->expr.elems[0]->sym_id,
                          expr->expr.len - 1u) &&
          petta_semantics_form(expr->expr.elems[0]->sym_id) !=
              PETTA_FORM_LENGTH) ||
         oem_metatype_call(compile, expr, plan));
    if (oem_apply_call(expr, plan) || value_native ||
        (symbol_head && plan->role == PETTA_PLAN_STATIC_CALL &&
         ((expr->expr.len == 2u &&
           petta_semantics_form(expr->expr.elems[0]->sym_id) ==
               PETTA_FORM_MSORT) ||
          (oem_pure_operation(compile, expr->expr.elems[0]->sym_id) &&
           !oem_fields_are_values(expr, plan, 1u) &&
           !petta_semantics_match_existence_observer_shape(
               expr, compile->host ? compile->host->reify_head
                                   : SYMBOL_ID_NONE)))))
        return oem_build_apply(compile, expr, plan, depth, out);
    if (!oem_data_node(expr, plan))
        return oem_build_host(compile, expr, plan, depth,
                              "operation outside the fragment", out);
    if (!oem_build_elements(compile, expr, plan, depth, 0u,
                            &node.first, &node.count))
        return false;
    uint32_t *children = malloc(sizeof(*children) * node.count);
    if (!children)
        return oem_reject(compile, "out of memory");
    for (uint32_t index = 0u; index < node.count; index++)
        children[index] = compile->nodes[
            compile->node_children[node.first + index]].exposed;
    bool ok = oem_template_build(compile, children, node.count,
                                 &node.exposed);
    free(children);
    return ok && oem_node(compile, node, out);
}

/* A tail position: the construct's output is the activation's output. */
static bool oem_build_tail(OemCompile *compile, Atom *expr,
                           const PettaPlanNode *plan, uint32_t depth,
                           uint32_t *out) {
    if (!expr || depth > OEM_MAX_DEPTH)
        return oem_reject(compile, "expression too deep");
    if (!(expr->kind == ATOM_EXPR && expr->expr.len > 0u &&
          expr->expr.elems[0]->kind == ATOM_SYMBOL && plan))
        return oem_build_value(compile, expr, plan, depth, out);
    SymbolId head = expr->expr.elems[0]->sym_id;
    OemNode node = {0};
    if (plan->control == PETTA_PLAN_CONTROL_IF) {
        if (expr->expr.len != 4u)
            return oem_reject(compile, "if arity");
        Atom *test = expr->expr.elems[1];
        const PettaPlanNode *test_plan = petta_plan_child(plan, 1u);
        node.kind = OEM_N_IF;
        /* An arithmetic comparison is a test of its arguments' values.  Any
         * other condition is a value: PeTTa runs it, and each of its
         * answers takes the first branch when it is the truth value and the
         * second otherwise.  A condition that observes whether a match has
         * a row stays one host goal, whose search answers the observation
         * by membership, which splitting the equality from its collection
         * would lose. */
        bool comparison = test && test->kind == ATOM_EXPR &&
            test->expr.len == 3u &&
            test->expr.elems[0]->kind == ATOM_SYMBOL &&
            oem_is_test(test->expr.elems[0]->sym_id) && test_plan &&
            test_plan->role == PETTA_PLAN_STATIC_CALL &&
            !petta_semantics_match_existence_observer_shape(
                test, compile->host ? compile->host->reify_head
                                    : SYMBOL_ID_NONE);
        bool ok = true;
        if (comparison) {
            node.op = test->expr.elems[0]->sym_id;
            ok = oem_build_elements(compile, test, test_plan, depth, 1u,
                                    &node.first, &node.count);
        } else {
            uint32_t condition = 0u;
            node.op = SYMBOL_ID_NONE;
            node.count = 1u;
            ok = oem_build_value(compile, test, test_plan, depth + 1u,
                                 &condition) &&
                oem_node_children(compile, &condition, 1u, &node.first);
        }
        return ok &&
            oem_build_tail(compile, expr->expr.elems[2],
                           petta_plan_child(plan, 2u), depth + 1u,
                           &node.then_node) &&
            oem_build_tail(compile, expr->expr.elems[3],
                           petta_plan_child(plan, 3u), depth + 1u,
                           &node.else_node) &&
            oem_hole(compile, &node.exposed) &&
            oem_node(compile, node, out);
    }
    if (plan->control == PETTA_PLAN_CONTROL_LET) {
        if (expr->expr.len != 4u)
            return oem_reject(compile, "let arity");
        node.kind = OEM_N_LET;
        const PettaPlanNode *body_plan = petta_plan_child(plan, 3u);
        bool counted = oem_count_only_binder(
            compile, expr->expr.elems[1], expr->expr.elems[2], NULL, 0u,
            expr->expr.elems[3],
            !body_plan || body_plan->contains_cardinality_call);
        compile->let_scope = expr;
        compile->let_before = NULL;
        compile->let_before_len = 0u;
        bool lowered = oem_lower_pattern(compile, expr->expr.elems[1],
                                         petta_plan_child(plan, 1u),
                                         depth + 1u, true, &node.pattern);
        compile->let_scope = NULL;
        if (!lowered ||
            !oem_build_producer(compile, expr->expr.elems[2],
                                petta_plan_child(plan, 2u), depth + 1u,
                                counted, &node.value) ||
            !oem_build_tail(compile, expr->expr.elems[3],
                            petta_plan_child(plan, 3u), depth + 1u,
                            &node.body))
            return false;
        oem_let_fills_pattern(compile, node.pattern, node.value);
        node.exposed = compile->nodes[node.body].exposed;
        return oem_node(compile, node, out);
    }
    if (plan->control == PETTA_PLAN_CONTROL_LET_STAR) {
        /* Sequential lets, the last binding innermost. */
        Atom *bindings = expr->expr.len == 3u ? expr->expr.elems[1] : NULL;
        const PettaPlanNode *bindings_plan = petta_plan_child(plan, 1u);
        if (!bindings || bindings->kind != ATOM_EXPR || !bindings_plan)
            return oem_reject(compile, "let* bindings");
        uint32_t body = 0u;
        const PettaPlanNode *body_plan = petta_plan_child(plan, 2u);
        if (!oem_build_tail(compile, expr->expr.elems[2], body_plan,
                            depth + 1u, &body))
            return false;
        /* Whether a counting operation may occur after binding `index`,
         * in a later binding or the body. */
        bool may_count = !body_plan || body_plan->contains_cardinality_call;
        for (uint32_t index = bindings->expr.len; index-- > 0u;) {
            Atom *pair = bindings->expr.elems[index];
            const PettaPlanNode *pair_plan =
                petta_plan_child(bindings_plan, index);
            if (!pair || pair->kind != ATOM_EXPR || pair->expr.len != 2u ||
                !pair_plan)
                return oem_reject(compile, "let* binding");
            bool counted = oem_count_only_binder(
                compile, pair->expr.elems[0], pair->expr.elems[1],
                bindings->expr.elems + index + 1u,
                (uint32_t)(bindings->expr.len - index - 1u),
                expr->expr.elems[2], may_count);
            may_count = may_count || pair_plan->contains_cardinality_call;
            OemNode let = {.kind = OEM_N_LET, .body = body};
            compile->let_scope = expr;
            compile->let_before = bindings->expr.elems;
            compile->let_before_len = index;
            bool lowered = oem_lower_pattern(compile, pair->expr.elems[0],
                                             petta_plan_child(pair_plan, 0u),
                                             depth + 1u, true, &let.pattern);
            compile->let_scope = NULL;
            if (!lowered ||
                !oem_build_producer(compile, pair->expr.elems[1],
                                    petta_plan_child(pair_plan, 1u),
                                    depth + 1u, counted, &let.value))
                return false;
            oem_let_fills_pattern(compile, let.pattern, let.value);
            let.exposed = compile->nodes[body].exposed;
            if (!oem_node(compile, let, &body))
                return false;
        }
        *out = body;
        return true;
    }
    if (plan->control != PETTA_PLAN_CONTROL_NONE)
        return oem_build_host(compile, expr, plan, depth,
                              "control outside the fragment", out);
    if (head == g_builtin_syms.match && expr->expr.len == 4u)
        return oem_build_match(compile, expr, plan, depth, true, out);
    if (head == g_builtin_syms.case_text && expr->expr.len == 3u)
        return oem_build_case(compile, expr, plan, depth, out);
    if (head == g_builtin_syms.empty_form && expr->expr.len == 1u) {
        node.kind = OEM_N_FAIL;
        return oem_hole(compile, &node.exposed) &&
            oem_node(compile, node, out);
    }
    if (!oem_build_value(compile, expr, plan, depth, out))
        return false;
    /* A call dispatched at run time keeps a return of its own, which holds
     * its handler.  A literal `superpose` returns its alternatives' answers
     * to the caller too, so an answer found k recursions deep returns once,
     * not through k returns. */
    if ((compile->nodes[*out].kind == OEM_N_CALL &&
         !compile->nodes[*out].recovers) ||
        compile->nodes[*out].kind == OEM_N_ALTERNATIVES)
        compile->nodes[*out].tail = true;
    return true;
}

/* `(case key ((pattern branch) ...))`: PeTTa's first-match choice.  Each
 * answer of the key meets the patterns in order, by unification; the first
 * that unifies commits to its branch, whose answers are the case's, and a
 * key no pattern admits fails.  A pattern is read as a head is, a `cons` in
 * it as a list pattern; one with a relational occurrence, which PeTTa
 * evaluates first, stays with the host, as does the `Empty` default, which
 * observes whether the key has an answer at all. */
static bool oem_build_case(OemCompile *compile, Atom *expr,
                           const PettaPlanNode *plan, uint32_t depth,
                           uint32_t *out) {
    Atom *arms = expr->expr.elems[2];
    const PettaPlanNode *arms_plan = petta_plan_child(plan, 2u);
    bool data = arms->kind == ATOM_EXPR && arms_plan &&
        arms_plan->child_count == arms->expr.len;
    for (CettaExprIndex index = 0u; data && index < arms->expr.len; index++) {
        Atom *arm = arms->expr.elems[index];
        const PettaPlanNode *arm_plan = petta_plan_child(arms_plan, index);
        data = arm->kind == ATOM_EXPR && arm->expr.len == 2u && arm_plan &&
            arm_plan->child_count == 2u &&
            !atom_is_symbol_id(arm->expr.elems[0], g_builtin_syms.empty) &&
            oem_head_data(compile, arm->expr.elems[0], 0u);
    }
    if (!data)
        return oem_build_host(compile, expr, plan, depth,
                              "case outside the fragment", out);
    OemNode node = {.kind = OEM_N_CASE, .count = 2u * arms->expr.len};
    uint32_t *children = malloc(sizeof(*children) *
                                (node.count ? node.count : 1u));
    if (!children)
        return oem_reject(compile, "out of memory");
    bool ok = oem_build_value(compile, expr->expr.elems[1],
                              petta_plan_child(plan, 1u), depth + 1u,
                              &node.value);
    for (CettaExprIndex index = 0u; ok && index < arms->expr.len; index++) {
        Atom *arm = arms->expr.elems[index];
        OemNode pattern = {.kind = OEM_N_VALUE};
        ok = oem_lower_pattern(compile, arm->expr.elems[0], NULL,
                               depth + 1u, true, &pattern.exposed) &&
            oem_node(compile, pattern, &children[2u * index]) &&
            oem_build_tail(compile, arm->expr.elems[1],
                           petta_plan_child(petta_plan_child(arms_plan,
                                                             index), 1u),
                           depth + 1u, &children[2u * index + 1u]);
    }
    ok = ok && oem_node_children(compile, children, node.count,
                                 &node.first) &&
        oem_hole(compile, &node.exposed) && oem_node(compile, node, out);
    free(children);
    return ok;
}

/* One pattern of a match and what follows it: the rest of a conjunction's
 * patterns, then the template.  A pattern is data however its heads are
 * defined, since a match never evaluates it; the space operand is a
 * variable or a literal. */
static bool oem_build_match_leg(OemCompile *compile, Atom *space,
                                const PettaPlanNode *space_plan,
                                Atom *const *patterns, uint32_t count,
                                Atom *template,
                                const PettaPlanNode *template_plan,
                                uint32_t depth, bool tail, bool match_row,
                                uint32_t *out) {
    if (depth > OEM_MAX_DEPTH)
        return oem_reject(compile, "expression too deep");
    OemNode node = {.kind = OEM_N_MATCH, .match_row = match_row};
    if (!oem_build_value(compile, space, space_plan, depth + 1u, &node.value))
        return false;
    if (compile->nodes[node.value].kind != OEM_N_VALUE ||
        compile->nodes[node.value].count != 0u)
        return oem_reject(compile, "match space operand");
    if (!oem_lower_pattern(compile, patterns[0], NULL, depth + 1u, false,
                           &node.pattern))
        return false;
    bool ok = count > 1u
        ? oem_build_match_leg(compile, space, space_plan, patterns + 1u,
                              count - 1u, template, template_plan,
                              depth + 1u, tail, match_row, &node.body)
        : tail ? oem_build_tail(compile, template, template_plan, depth + 1u,
                                &node.body)
               : oem_build_value(compile, template, template_plan,
                                 depth + 1u, &node.body);
    if (!ok)
        return false;
    node.exposed = compile->nodes[node.body].exposed;
    return oem_node(compile, node, out);
}

/* `(match space pattern template)`: the template's code runs once per row
 * of the space that unifies with the pattern.  A conjunction `(, p1 p2 ...)`
 * matches its patterns in turn, as the nested matches equation search
 * evaluates it by. */
static bool oem_build_match(OemCompile *compile, Atom *expr,
                            const PettaPlanNode *plan, uint32_t depth,
                            bool tail, uint32_t *out) {
    Atom *pattern = expr->expr.elems[2];
    /* PeTTa's match takes its pattern as written; a `cons` in one is the
     * host's to read. */
    if (petta_semantics_contains_cons_constraint(pattern))
        return oem_build_host(compile, expr, plan, depth,
                              "list pattern in a match", out);
    bool connective = pattern->kind == ATOM_EXPR && pattern->expr.len > 0u;
    if (connective &&
        atom_is_symbol_id(pattern->expr.elems[0], g_builtin_syms.pipe))
        return oem_reject(compile, "match pattern connective");
    connective = connective &&
        atom_is_symbol_id(pattern->expr.elems[0], g_builtin_syms.comma);
    if (connective && pattern->expr.len < 2u)
        return oem_reject(compile, "match pattern connective");
    return oem_build_match_leg(
        compile, expr->expr.elems[1], plan ? petta_plan_child(plan, 1u) : NULL,
        connective ? pattern->expr.elems + 1u : &expr->expr.elems[2],
        connective ? pattern->expr.len - 1u : 1u, expr->expr.elems[3],
        plan ? petta_plan_child(plan, 3u) : NULL, depth, tail, connective, out);
}

static bool oem_emit_goals(OemCompile *compile, uint32_t index);

static bool oem_emit_element_goals(OemCompile *compile, const OemNode *node,
                                   uint32_t *first_arg_out) {
    uint32_t *args = malloc(sizeof(*args) * (node->count ? node->count : 1u));
    if (!args)
        return oem_reject(compile, "out of memory");
    bool ok = true;
    for (uint32_t index = 0u; ok && index < node->count; index++) {
        uint32_t child = compile->node_children[node->first + index];
        ok = oem_emit_goals(compile, child);
        args[index] = compile->nodes[child].exposed;
    }
    ok = ok && oem_args(compile, args, node->count, first_arg_out);
    free(args);
    return ok;
}

/* The goals that compute a value's holes, in PeTTa's order: elements left
 * to right, each call after its arguments. */
static bool oem_emit_goals(OemCompile *compile, uint32_t index) {
    OemNode node = compile->nodes[index];
    uint32_t first_arg = 0u;
    switch ((OemNodeKind)node.kind) {
    case OEM_N_VALUE:
        for (uint32_t child = 0u; child < node.count; child++) {
            if (!oem_emit_goals(compile,
                                compile->node_children[node.first + child]))
                return false;
        }
        return true;
    case OEM_N_CALL:
    case OEM_N_PRIM: {
        uint32_t head = 0u;
        if (!oem_emit_element_goals(compile, &node, &first_arg) ||
            (node.kind == OEM_N_PRIM &&
             !oem_op_literal(compile, node.op, &head)))
            return false;
        return oem_emit(compile, (OemStep){
                            .kind = node.kind == OEM_N_CALL
                                ? (node.tail ? OEM_S_TAIL : OEM_S_CALL)
                                : OEM_S_PRIM,
                            .recovers = node.recovers,
                            .op = node.op, .relation = node.relation,
                            .first_arg = first_arg, .arg_count = node.count,
                            .pattern = node.exposed, .value = head,
                        }, NULL);
    }
    case OEM_N_MATCH:
        return oem_emit_goals(compile, node.value) &&
            oem_emit(compile, (OemStep){
                         .kind = OEM_S_MATCH, .pattern = node.pattern,
                         .match_row = node.match_row,
                         .value = compile->nodes[node.value].exposed,
                     }, NULL) &&
            oem_emit_goals(compile, node.body);
    case OEM_N_HOST:
        return oem_emit(compile, (OemStep){
                            .kind = OEM_S_HOST, .op = node.op,
                            .relation = node.relation, .value = node.value,
                            .pattern = node.exposed, .target = node.host_fast,
                        }, NULL);
    case OEM_N_APPLY:
        if (!oem_emit_element_goals(compile, &node, &first_arg))
            return false;
        return oem_emit(compile, (OemStep){
                            .kind = OEM_S_HOST, .recovers = node.recovers,
                            .op = node.op, .relation = node.relation,
                            .value = node.value, .pattern = node.exposed,
                            .target = node.host_fast
                                ? node.host_fast : OEM_HOST_APPLY,
                        }, NULL);
    case OEM_N_COLLECT:
    case OEM_N_ALTERNATIVES:
    case OEM_N_ONCE: {
        uint32_t step = 0u;
        uint32_t control = 0u;
        uint32_t target = node.kind == OEM_N_COLLECT ? OEM_HOST_COLLECT
            : node.kind == OEM_N_ONCE ? OEM_HOST_ONCE
                                      : OEM_HOST_ALTERNATIVES;
        if (!oem_emit_element_goals(compile, &node, &first_arg) ||
            !(OEM_PUSH(compile->program, control,
                       ((OemControl){.local = node.local,
                                     .yield = node.yield,
                                     .limit = node.limit}),
                       &control) ||
              oem_reject(compile, "out of memory")) ||
            !oem_emit(compile, (OemStep){
                          .kind = OEM_S_HOST, .relation = node.relation,
                          .value = node.value, .pattern = node.exposed,
                          .first_arg = first_arg, .arg_count = node.count,
                          .target = target, .control = control,
                          .tail = node.kind == OEM_N_ALTERNATIVES &&
                              node.tail,
                      }, &step))
            return false;
        /* A collection's YIELD step, or a once's CUT step, follows the
         * equation's body. */
        return target == OEM_HOST_ALTERNATIVES ||
            ((oem_reserve((void **)&compile->resumes, &compile->resume_cap,
                          compile->resume_len + 1u,
                          sizeof(*compile->resumes)) &&
              (compile->resumes[compile->resume_len++] = step, true)) ||
             oem_reject(compile, "out of memory"));
    }
    case OEM_N_CUT:
        compile->cuts = true;
        return oem_emit(compile, (OemStep){
                            .kind = OEM_S_EQUATION_CUT,
                            .pattern = node.exposed,
                        }, NULL);
    default:
        return oem_reject(compile, "control in a value position");
    }
}

static bool oem_emit_tail(OemCompile *compile, uint32_t index);

/* One branch of an if: its exposed output meets the if's output, then the
 * branch runs.  A tail call or `empty` has no output of its own to meet. */
static bool oem_emit_branch(OemCompile *compile, const OemNode *node,
                            uint32_t branch) {
    const OemNode *target = &compile->nodes[branch];
    bool meets = !((target->kind == OEM_N_CALL && target->tail) ||
                   target->kind == OEM_N_FAIL);
    return (!meets ||
            oem_emit(compile, (OemStep){
                         .kind = OEM_S_BIND, .pattern = node->exposed,
                         .value = target->exposed,
                     }, NULL)) &&
        oem_emit_tail(compile, branch);
}

/* A tail construct after its output has been unified with the
 * destination. */
static bool oem_emit_tail(OemCompile *compile, uint32_t index) {
    OemNode node = compile->nodes[index];
    switch ((OemNodeKind)node.kind) {
    case OEM_N_LET:
        return (node.pattern == compile->nodes[node.value].exposed ||
                oem_emit(compile, (OemStep){
                             .kind = OEM_S_BIND, .pattern = node.pattern,
                             .value = compile->nodes[node.value].exposed,
                         }, NULL)) &&
            oem_emit_goals(compile, node.value) &&
            oem_emit_tail(compile, node.body);
    case OEM_N_IF: {
        uint32_t first_arg = 0u;
        uint32_t test_step = 0u;
        uint32_t head = 0u;
        if (!oem_emit_element_goals(compile, &node, &first_arg) ||
            !oem_op_literal(compile, node.op, &head) ||
            !oem_emit(compile, (OemStep){
                          .kind = OEM_S_TEST, .op = node.op,
                          .first_arg = first_arg, .arg_count = node.count,
                          .value = head,
                      }, &test_step) ||
            !oem_emit_branch(compile, &node, node.then_node))
            return false;
        compile->program->steps[test_step].target =
            compile->program->step_len;
        return oem_emit_branch(compile, &node, node.else_node);
    }
    case OEM_N_MATCH:
        return oem_emit_goals(compile, node.value) &&
            oem_emit(compile, (OemStep){
                         .kind = OEM_S_MATCH, .pattern = node.pattern,
                         .match_row = node.match_row,
                         .value = compile->nodes[node.value].exposed,
                     }, NULL) &&
            oem_emit_tail(compile, node.body);
    case OEM_N_FAIL:
        return oem_emit(compile, (OemStep){.kind = OEM_S_FAIL}, NULL);
    case OEM_N_CASE: {
        /* Each arm tries its pattern, and its branch follows; a pattern
         * that does not unify continues at the next arm, and after the
         * last the case fails. */
        if (!oem_emit_goals(compile, node.value))
            return false;
        uint32_t key = compile->nodes[node.value].exposed;
        for (uint32_t arm = 0u; arm < node.count; arm += 2u) {
            uint32_t pattern = compile->node_children[node.first + arm];
            uint32_t branch = compile->node_children[node.first + arm + 1u];
            uint32_t attempt = 0u;
            if (!oem_emit(compile, (OemStep){
                              .kind = OEM_S_CASE,
                              .pattern = compile->nodes[pattern].exposed,
                              .value = key,
                          }, &attempt) ||
                !oem_emit_branch(compile, &node, branch))
                return false;
            compile->program->steps[attempt].target =
                compile->program->step_len;
        }
        return oem_emit(compile, (OemStep){.kind = OEM_S_FAIL}, NULL);
    }
    case OEM_N_CALL:
        if (node.tail)
            return oem_emit_goals(compile, index);
        /* fallthrough */
    case OEM_N_VALUE:
    case OEM_N_PRIM:
    case OEM_N_HOST:
    case OEM_N_APPLY:
    case OEM_N_COLLECT:
    case OEM_N_ALTERNATIVES:
    case OEM_N_ONCE:
    case OEM_N_CUT:
        return oem_emit_goals(compile, index) &&
            oem_emit(compile, (OemStep){.kind = OEM_S_RET,
                                        .value = compile->output}, NULL);
    }
    return oem_reject(compile, "unknown construct");
}

/* The relational occurrences of the head, as calls in source order whose
 * values unify with the query subterms kept for them.  Their arguments are
 * head data: variables and structure without further occurrences.  An
 * occurrence of a builtin, or of a relation the host keeps, is a host goal
 * over the same data. */
static bool oem_compile_relational_occurrences(OemCompile *compile) {
    CettaOpenEquationProgram *program = compile->program;
    for (uint32_t index = 0u; index < compile->pending_len; index++) {
        Atom *call = compile->pending[index].call;
        uint32_t count = call->expr.len - 1u;
        for (uint32_t arg = 0u; arg < count; arg++) {
            if (!oem_head_data(compile, call->expr.elems[arg + 1u], 0u))
                return oem_reject(compile, "nested relational occurrence");
        }
        if (compile->host && compile->host->relation_admitted &&
            !compile->host->relation_admitted(
                compile->host->context, program->space,
                call->expr.elems[0]->sym_id, count)) {
            OemStep step = {
                .kind = OEM_S_HOST, .relation = program->host_plan_len,
                .target = OEM_HOST_OCCURRENCE,
            };
            if (!oem_lower_pattern(compile, call, NULL, 0u, true,
                                   &step.value) ||
                !oem_template_slot(compile, compile->pending[index].slot,
                                   &step.pattern) ||
                !(oem_reserve((void **)&program->host_plans,
                              &program->host_plan_cap,
                              program->host_plan_len + 1u,
                              sizeof(*program->host_plans)) ||
                  oem_reject(compile, "out of memory")))
                return false;
            program->host_plans[program->host_plan_len++] = NULL;
            if (!oem_emit(compile, step, NULL))
                return false;
            continue;
        }
        uint32_t *args = malloc(sizeof(*args) * (count ? count : 1u));
        if (!args)
            return oem_reject(compile, "out of memory");
        bool ok = true;
        for (uint32_t arg = 0u; ok && arg < count; arg++)
            ok = oem_lower_pattern(compile, call->expr.elems[arg + 1u], NULL,
                                   0u, true, &args[arg]);
        OemStep step = {.kind = OEM_S_CALL, .arg_count = count};
        ok = ok && oem_args(compile, args, count, &step.first_arg);
        free(args);
        if (!ok ||
            !oem_call_relation(compile, call, &step.relation) ||
            !oem_template_slot(compile, compile->pending[index].slot,
                               &step.pattern) ||
            !oem_emit(compile, step, NULL))
            return false;
    }
    return true;
}

/* The output an occurrence's plan fixes before it runs, as the search
 * machine derives it to constrain an equation's destination: a transparent
 * child's output, a value or a quoted child as it stands, a constructor over
 * its fields' outputs (an opaque field is a fresh hole), or the dialect's
 * truth value.  An opaque occurrence fixes none.  What the region cannot
 * state, a truth value inside a constructor, rejects. */
static bool oem_known_output(OemCompile *compile, Atom *expr,
                             const PettaPlanNode *plan, uint32_t depth,
                             bool root, uint8_t *kind_out,
                             uint32_t *template_out) {
    if (depth > OEM_MAX_DEPTH)
        return oem_reject(compile, "expression too deep");
    while (plan && plan->output == PETTA_PLAN_OUTPUT_CHILD) {
        if (!expr || expr->kind != ATOM_EXPR ||
            expr->expr.len != plan->child_count ||
            plan->output_child >= expr->expr.len)
            return oem_reject(compile, "output child");
        CettaExprIndex child = plan->output_child;
        expr = expr->expr.elems[child];
        plan = petta_plan_child(plan, child);
    }
    if (!expr)
        return oem_reject(compile, "output child");
    if (!plan || plan->output == PETTA_PLAN_OUTPUT_OPAQUE) {
        *kind_out = root ? OEM_OUTPUT_OPEN : OEM_OUTPUT_TEMPLATE;
        return root || oem_hole(compile, template_out);
    }
    if (plan->output == PETTA_PLAN_OUTPUT_TRUE) {
        if (!root)
            return oem_reject(compile, "truth output inside a constructor");
        *kind_out = OEM_OUTPUT_TRUE;
        return true;
    }
    *kind_out = OEM_OUTPUT_TEMPLATE;
    if (plan->output == PETTA_PLAN_OUTPUT_VALUE ||
        plan->output == PETTA_PLAN_OUTPUT_QUOTED_CHILD) {
        if (plan->output == PETTA_PLAN_OUTPUT_QUOTED_CHILD) {
            if (expr->kind != ATOM_EXPR ||
                expr->expr.len != plan->child_count ||
                plan->output_child >= expr->expr.len)
                return oem_reject(compile, "output child");
            expr = expr->expr.elems[plan->output_child];
        }
        return oem_lower_pattern(compile, expr, NULL, depth + 1u, false,
                                 template_out);
    }
    if (plan->output != PETTA_PLAN_OUTPUT_CONSTRUCTOR ||
        expr->kind != ATOM_EXPR || expr->expr.len == 0u ||
        expr->expr.len != plan->child_count)
        return oem_reject(compile, "output shape");
    uint32_t *children = malloc(sizeof(*children) * expr->expr.len);
    if (!children)
        return oem_reject(compile, "out of memory");
    bool ok = true;
    for (CettaExprIndex index = 0u; ok && index < expr->expr.len; index++) {
        uint8_t kind = OEM_OUTPUT_OPEN;
        ok = oem_known_output(compile, expr->expr.elems[index],
                              petta_plan_child(plan, index), depth + 1u,
                              false, &kind, &children[index]);
    }
    ok = ok && oem_template_build(compile, children,
                                  (uint32_t)expr->expr.len, template_out);
    free(children);
    return ok;
}


/* The slots a template mentions, marked in `into`. */
static void oem_template_slots(const CettaOpenEquationProgram *program,
                               uint32_t template_index, uint8_t *into) {
    const OemTemplate *item = &program->templates[template_index];
    if (item->kind == OEM_T_SLOT) {
        into[item->slot] = 1u;
        return;
    }
    if (item->kind != OEM_T_BUILD && !oem_template_is_list(item))
        return;
    for (uint32_t index = 0u; index < item->count; index++)
        oem_template_slots(program,
                           program->template_children[item->first + index],
                           into);
}

/* Read a template's slots on a path: a slot not seen before on it is first
 * read here, so it keeps its cell. */
static void oem_template_reads(const CettaOpenEquationProgram *program,
                               uint32_t template_index, uint8_t *seen,
                               uint8_t *celled) {
    const OemTemplate *item = &program->templates[template_index];
    if (item->kind == OEM_T_SLOT) {
        if (!seen[item->slot])
            celled[item->slot] = 1u;
        seen[item->slot] = 1u;
        return;
    }
    if (item->kind != OEM_T_BUILD && !oem_template_is_list(item))
        return;
    for (uint32_t index = 0u; index < item->count; index++)
        oem_template_reads(program,
                           program->template_children[item->first + index],
                           seen, celled);
}

/* A step's result slot when the region may store the result there: a
 * primitive's, a bind's, a cut's truth value, or a host step's the region
 * may decide, whose pattern is one slot.  UINT32_MAX otherwise. */
static uint32_t oem_step_result_slot(const CettaOpenEquationProgram *program,
                                     const OemStep *step) {
    bool storing = step->kind == OEM_S_PRIM || step->kind == OEM_S_BIND ||
        step->kind == OEM_S_CALL || step->kind == OEM_S_EQUATION_CUT ||
        (step->kind == OEM_S_HOST && step->target >= OEM_HOST_PURE);
    const OemTemplate *pattern = &program->templates[step->pattern];
    return storing && pattern->kind == OEM_T_SLOT ? pattern->slot
                                                  : UINT32_MAX;
}

/* Mark the slots the body stores at their first occurrence on every path,
 * and the steps that store them.  The body's steps form a tree of paths: a
 * test branches, and a path ends at a return, a failure or a tail call;
 * nothing rejoins, and a match or host step resumes each alternative at the
 * next step of its own path.  Walking every path in order, a slot is at its
 * first occurrence where nothing on the path before mentioned it.  A slot
 * whose first occurrence is a read on some path, or which the activation
 * itself reads or assigns, keeps its cell; any other slot the body mentions
 * is first stored on every path that mentions it, and a slot no path
 * mentions is never read or stored and needs no cell either.  A store made
 * while a frame newer than the activation is live is trailed
 * (`oem_store_slot`): restoring that frame resumes the path before the
 * store, and the slot must be empty again, since a collection traces every
 * filled slot.  False only when out of memory. */
static bool oem_mark_first_stores(OemCompile *compile, OemEquation *equation) {
    CettaOpenEquationProgram *program = compile->program;
    uint32_t slots = compile->slot_count ? compile->slot_count : 1u;
    uint32_t first = equation->first_step;
    uint32_t end = program->step_len;
    uint8_t *assigned = calloc(slots, 1u);
    uint8_t *celled = calloc(slots, 1u);
    uint8_t *stored = calloc(slots, 1u);
    /* The slots some path mentions; a slot none mentions is never read or
     * stored, and needs no cell. */
    uint8_t *mentioned = calloc(slots, 1u);
    uint8_t *candidate = calloc(end - first ? end - first : 1u, 1u);
    /* Pending paths: a start step and the slots mentioned before it; each
     * test adds one. */
    uint32_t *starts = malloc(sizeof(*starts) * (end - first + 2u));
    uint8_t *seen_stack = malloc((size_t)slots * (end - first + 2u));
    bool ok = assigned && celled && stored && mentioned && candidate &&
        starts && seen_stack;
    if (ok) {
        /* What the activation assigns: head variables and the output slot;
         * what it reads: the destination and the output template. */
        for (uint32_t op = equation->first_op;
             op < equation->first_op + equation->op_count; op++) {
            if (program->ops[op].kind == OEM_M_BIND)
                assigned[program->ops[op].operand] = 1u;
        }
        if (equation->dest_kind == OEM_DEST_SLOT)
            assigned[equation->destination] = 1u;
        if (equation->dest_kind == OEM_DEST_UNIFY)
            oem_template_slots(program, equation->destination, celled);
        if (equation->dest_kind == OEM_DEST_SLOT &&
            equation->output_kind == OEM_OUTPUT_TEMPLATE)
            oem_template_slots(program, equation->output, celled);
        for (uint32_t slot = 0u; slot < slots; slot++)
            celled[slot] |= assigned[slot];
    }
    uint32_t pending = 0u;
    if (ok) {
        starts[pending] = first;
        for (uint32_t slot = 0u; slot < slots; slot++)
            seen_stack[slot] = celled[slot];
        pending++;
    }
    while (ok && pending > 0u) {
        pending--;
        uint32_t pc = starts[pending];
        uint8_t *seen = &seen_stack[(size_t)pending * slots];
        while (pc < end) {
            const OemStep *step = &program->steps[pc];
            uint32_t result = oem_step_result_slot(program, step);
            if (step->kind == OEM_S_CALL || step->kind == OEM_S_TAIL ||
                step->kind == OEM_S_PRIM || step->kind == OEM_S_TEST) {
                for (uint32_t arg = 0u; arg < step->arg_count; arg++)
                    oem_template_reads(
                        program, program->step_args[step->first_arg + arg],
                        seen, celled);
            }
            if (step->kind == OEM_S_BIND || step->kind == OEM_S_MATCH ||
                step->kind == OEM_S_HOST || step->kind == OEM_S_CASE)
                oem_template_reads(program, step->value, seen, celled);
            if (step->kind == OEM_S_HOST &&
                (step->target == OEM_HOST_COLLECT ||
                 step->target == OEM_HOST_ALTERNATIVES ||
                 step->target == OEM_HOST_ONCE)) {
                for (uint32_t arg = 0u; arg < step->arg_count; arg++)
                    oem_template_reads(
                        program, program->step_args[step->first_arg + arg],
                        seen, celled);
                if (step->target == OEM_HOST_COLLECT)
                    oem_template_reads(
                        program, program->controls[step->control].yield,
                        seen, celled);
            }
            /* A tail call returns its value in place of the equation's:
             * nothing reads its pattern. */
            if (step->kind != OEM_S_RET && step->kind != OEM_S_FAIL &&
                step->kind != OEM_S_TEST && step->kind != OEM_S_TAIL &&
                result == UINT32_MAX)
                oem_template_reads(program, step->pattern, seen, celled);
            if (result != UINT32_MAX) {
                if (!seen[result]) {
                    stored[result] = 1u;
                    candidate[pc - first] = 1u;
                }
                seen[result] = 1u;
            }
            if (step->kind == OEM_S_RET || step->kind == OEM_S_FAIL ||
                step->kind == OEM_S_TAIL)
                break;
            if (step->kind == OEM_S_TEST || step->kind == OEM_S_CASE) {
                /* Both branches continue from what this path has seen; the
                 * one taken when the test or the pattern fails starts at
                 * `target` and keeps this path's record. */
                starts[pending] = step->target;
                memcpy(&seen_stack[(size_t)(pending + 1u) * slots], seen,
                       slots);
                starts[pending + 1u] = pc + 1u;
                pending += 2u;
                break;
            }
            pc++;
        }
        for (uint32_t slot = 0u; slot < slots; slot++)
            mentioned[slot] |= seen[slot];
    }
    uint32_t base = program->slot_store_len;
    ok = ok && oem_reserve((void **)&program->slot_stores,
                           &program->slot_store_cap, base + slots, 1u);
    if (ok) {
        for (uint32_t slot = 0u; slot < slots; slot++)
            program->slot_stores[base + slot] =
                (stored[slot] && !celled[slot]) || !mentioned[slot] ? 1u : 0u;
        program->slot_store_len = base + slots;
        equation->first_slot_store = base;
        /* The slots head matching leaves unset (no BIND, not the output
         * slot) that the body does not store: each activation's cells. */
        uint32_t cells = 0u;
        for (uint32_t slot = 0u; slot < slots; slot++)
            cells += !assigned[slot] && !program->slot_stores[base + slot];
        equation->first_cell_slot = program->cell_slot_len;
        equation->cell_slot_count = cells;
        ok = oem_reserve((void **)&program->cell_slots,
                         &program->cell_slot_cap,
                         program->cell_slot_len + cells,
                         sizeof(*program->cell_slots));
        for (uint32_t slot = 0u; ok && slot < slots; slot++) {
            if (!assigned[slot] && !program->slot_stores[base + slot])
                program->cell_slots[program->cell_slot_len++] = slot;
        }
    }
    if (ok) {
        for (uint32_t pc = first; pc < end; pc++) {
            if (!candidate[pc - first])
                continue;
            OemStep *step = &program->steps[pc];
            step->store = program->slot_stores[
                base + oem_step_result_slot(program, step)];
        }
    }
    free(assigned);
    free(celled);
    free(stored);
    free(mentioned);
    free(candidate);
    free(starts);
    free(seen_stack);
    return ok || oem_reject(compile, "out of memory");
}

static bool oem_compile_equation(CettaOpenEquationProgram *program,
                                 PettaProgram *petta,
                                 const CettaOpenEquationHost *host,
                                 bool authored, bool named, Atom *lhs,
                                 Atom *rhs, const PettaPlanNode *rhs_plan,
                                 const char **reason) {
    OemCompile compile = {.program = program, .host = host, .petta = petta,
                          .authored = authored, .named = named,
                          .lhs = lhs, .rhs = rhs};
    OemEquation equation = {
        .first_op = program->op_len,
        .first_step = program->step_len,
    };
    uint32_t arity = lhs->expr.len - 1u;
    compile.register_count = arity;
    bool ok = oem_register_vars(&compile, lhs, 0u) &&
        oem_register_vars(&compile, rhs, 0u);
    if (ok) {
        compile.slot_count = compile.var_len;
        compile.bound = calloc(compile.var_len ? compile.var_len : 1u,
                               sizeof(*compile.bound));
        ok = compile.bound != NULL || oem_reject(&compile, "out of memory");
    }
    for (uint32_t index = 0u; ok && index < arity; index++)
        ok = oem_compile_param(&compile, lhs->expr.elems[index + 1u], index,
                               0u);
    equation.op_count = program->op_len - equation.first_op;
    /* Head matching ends with the body's exposed output meeting the
     * destination; then the head's relational occurrences run, then the
     * body. */
    uint32_t root = 0u;
    ok = ok && oem_build_tail(&compile, rhs, rhs_plan, 0u, &root);
    if (ok) {
        const OemNode *node = &compile.nodes[root];
        const OemTemplate *exposed = &program->templates[node->exposed];
        compile.output = node->exposed;
        equation.destination = node->exposed;
        equation.dest_kind =
            (node->kind == OEM_N_CALL && node->tail) ||
                    node->kind == OEM_N_FAIL
                ? OEM_DEST_NONE
                : exposed->kind == OEM_T_SLOT ? OEM_DEST_SLOT
                                              : OEM_DEST_UNIFY;
        if (exposed->kind == OEM_T_SLOT)
            equation.destination = exposed->slot;
        if (equation.dest_kind == OEM_DEST_SLOT)
            ok = oem_known_output(&compile, rhs, rhs_plan, 0u, true,
                                  &equation.output_kind, &equation.output);
    }
    ok = ok && oem_compile_relational_occurrences(&compile) &&
        oem_emit_tail(&compile, root);
    /* Each collection's answers return to a YIELD step after the body,
     * and each once's to a CUT step, which no path through the body
     * reaches; the CUT continues after its once. */
    for (uint32_t index = 0u; ok && index < compile.resume_len; index++) {
        uint32_t at = compile.resumes[index];
        uint32_t control = program->steps[at].control;
        OemStep step = program->steps[at].target == OEM_HOST_ONCE
            ? (OemStep){.kind = OEM_S_CUT, .target = at + 1u}
            : (OemStep){.kind = OEM_S_YIELD,
                        .value = program->controls[control].yield};
        uint32_t resume = 0u;
        ok = oem_emit(&compile, step, &resume);
        if (ok)
            program->controls[control].resume = resume;
    }
    equation.register_count = compile.register_count;
    equation.slot_count = compile.slot_count;
    equation.cuts = compile.cuts;
    ok = ok && oem_mark_first_stores(&compile, &equation);
    uint32_t index = 0u;
    ok = ok && (OEM_PUSH(program, equation, equation, &index) ||
                oem_reject(&compile, "out of memory"));
    if (!ok && reason && !*reason)
        *reason = compile.reason ? compile.reason : "equation compile";
    free(compile.vars);
    free(compile.bound);
    free(compile.pending);
    free(compile.nodes);
    free(compile.node_children);
    free(compile.resumes);
    return ok;
}

static void oem_note_first_tests(CettaOpenEquationProgram *program,
                                 uint32_t relation_index);
static bool oem_build_index(CettaOpenEquationProgram *program,
                            uint32_t relation_index);
static bool oem_build_switches(CettaOpenEquationProgram *program,
                               uint32_t relation_index);
static bool oem_index_failed(const char **reason);

/* A local relation's equations.  One that does not compile leaves its
 * control to the host, and whatever it had emitted unreached; false only
 * for foldl/4's clauses, which a call enters with no host goal to fall
 * back to. */
static bool oem_compile_local_relation(CettaOpenEquationProgram *program,
                                       PettaProgram *petta,
                                       const CettaOpenEquationHost *host,
                                       uint32_t relation_index,
                                       const char **reason_out) {
    uint32_t first = program->equation_len;
    uint32_t source = program->relations[relation_index].first_source;
    uint32_t count = program->relations[relation_index].source_count;
    const char *reason = NULL;
    bool ok = true;
    for (uint32_t index = 0u; ok && index < count; index++) {
        OemLocalSource local = program->sources[source + index];
        ok = oem_compile_equation(
            program, petta, host,
            program->relations[relation_index].fold_step == SYMBOL_ID_NONE,
            false, local.lhs, local.rhs, local.plan, &reason);
    }
    OemRelation *relation = &program->relations[relation_index];
    if (!ok) {
        program->equation_len = first;
        relation->failed = true;
        if (relation->fold_step == SYMBOL_ID_NONE)
            return true;
        *reason_out = reason ? reason : "foldl clauses";
        return false;
    }
    relation->first_equation = first;
    relation->equation_count = program->equation_len - first;
    oem_note_first_tests(program, relation_index);
    if (!oem_build_index(program, relation_index))
        return oem_index_failed(reason_out);
    return true;
}

/* The first structural test of an equation's head, or NULL when every
 * operation only binds. */
static const OemMatchOp *oem_first_test(const CettaOpenEquationProgram *program,
                                        const OemEquation *equation) {
    for (uint32_t op = 0u; op < equation->op_count; op++) {
        const OemMatchOp *test = &program->ops[equation->first_op + op];
        if (test->kind == OEM_M_ATOM || test->kind == OEM_M_EXPR ||
            test->kind == OEM_M_LIST)
            return test;
    }
    return NULL;
}

static void oem_note_first_tests(CettaOpenEquationProgram *program,
                                 uint32_t relation_index) {
    OemRelation *relation = &program->relations[relation_index];
    const OemMatchOp *first = relation->equation_count
        ? oem_first_test(program, &program->equations[relation->first_equation])
        : NULL;
    bool differ = false;
    for (uint32_t index = 1u; !differ && index < relation->equation_count;
         index++) {
        const OemMatchOp *test = oem_first_test(
            program, &program->equations[relation->first_equation + index]);
        differ = (first == NULL) != (test == NULL) ||
            (first && test &&
             (test->kind != first->kind || test->reg != first->reg ||
              (test->kind == OEM_M_ATOM
                   ? !atom_eq(test->literal, first->literal)
                   : test->length != first->length)));
    }
    relation->first_tests_differ = differ;
}

enum { OEM_INDEX_MAX_DEPTH = 4u };

static bool oem_index_failed(const char **reason) {
    if (reason)
        *reason = "out of memory";
    return false;
}

/* A path's keys go in a table from this many on; fewer are scanned. */
enum { OEM_INDEX_TABLE_MIN_KEYS = 6u };

/* The code a table places a literal by: a symbol's id or an integer's
 * value.  `atom_eq` compares symbols by id and integers by value, and a
 * symbol with an integer never, so literals it finds equal have one code. */
static inline bool oem_literal_code(const Atom *atom, uint64_t *code) {
    if (atom->kind == ATOM_SYMBOL) {
        *code = ((uint64_t)atom->sym_id << 1) | 1u;
        return true;
    }
    if (atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_INT) {
        *code = (uint64_t)atom->ground.ival << 1;
        return true;
    }
    return false;
}

static inline uint32_t oem_literal_home(uint64_t code, uint32_t mask) {
    return (uint32_t)((code * UINT64_C(0x9E3779B97F4A7C15)) >> 32) & mask;
}

/* `atom_eq` against a key, decided here for a symbol or an integer. */
static inline bool oem_key_eq(Atom *term, Atom *literal) {
    if (term->kind != literal->kind)
        return false;
    if (term->kind == ATOM_SYMBOL)
        return term->sym_id == literal->sym_id;
    if (term->kind == ATOM_GROUNDED && term->ground.gkind == GV_INT &&
        literal->ground.gkind == GV_INT)
        return term->ground.ival == literal->ground.ival;
    return atom_eq(term, literal);
}

/* Place a path's keys in a table when there are enough of them and each
 * is a symbol or an integer.  False only when out of memory. */
static bool oem_build_index_table(CettaOpenEquationProgram *program,
                                  OemIndexPath *path) {
    path->first_table = 0u;
    path->table_mask = 0u;
    if (path->key_count < OEM_INDEX_TABLE_MIN_KEYS)
        return true;
    uint64_t code = 0u;
    for (uint32_t key = 0u; key < path->key_count; key++) {
        if (!oem_literal_code(
                program->index_keys[path->first_key + key].literal, &code))
            return true;
    }
    uint32_t size = 8u;
    while (size < 2u * path->key_count)
        size *= 2u;
    uint32_t first = program->index_table_len;
    uint32_t added = 0u;
    for (uint32_t entry = 0u; entry < size; entry++) {
        if (!OEM_PUSH(program, index_table, 0u, &added))
            return false;
    }
    uint32_t *table = &program->index_tables[first];
    for (uint32_t key = 0u; key < path->key_count; key++) {
        (void)oem_literal_code(
            program->index_keys[path->first_key + key].literal, &code);
        uint32_t at = oem_literal_home(code, size - 1u);
        while (table[at])
            at = (at + 1u) & (size - 1u);
        table[at] = key + 1u;
    }
    path->first_table = first;
    path->table_mask = size - 1u;
    return true;
}

/* The equations a path admits for a term equal to `literal` (its key's). */
static uint64_t oem_path_admits(const CettaOpenEquationProgram *program,
                                const OemIndexPath *path, Atom *literal) {
    uint64_t admitted = path->free;
    for (uint32_t key = 0u; key < path->key_count; key++) {
        const OemIndexKey *entry = &program->index_keys[path->first_key + key];
        if (atom_eq(literal, entry->literal))
            admitted |= entry->equations;
    }
    return admitted;
}

/* Whether a path is decided by its argument's functor: it reads the
 * argument itself, or the first element of an expression argument. */
static bool oem_path_on_functor(const CettaOpenEquationProgram *program,
                                const OemIndexPath *path) {
    return path->step_count == 0u ||
        (path->step_count == 1u &&
         program->index_steps[path->first_step] == 0u);
}

static uint64_t oem_switch_code(uint64_t code, uint32_t length) {
    return code + (uint64_t)length * UINT64_C(0xD6E8FEB86659FD93);
}

static bool oem_switch_entry(CettaOpenEquationProgram *program,
                             uint32_t length, Atom *literal,
                             uint64_t equations) {
    uint32_t added = 0u;
    return OEM_PUSH(program, switch_entry,
                    ((OemSwitchEntry){.length = length, .literal = literal,
                                      .equations = equations}),
                    &added);
}

/* Switches for the arguments at which two or more paths are decided by the
 * functor, each key a symbol or an integer.  Their paths move after the
 * paths read one by one.  False only when out of memory. */
static bool oem_build_switches(CettaOpenEquationProgram *program,
                               uint32_t relation_index) {
    OemRelation *relation = &program->relations[relation_index];
    uint32_t first = relation->first_index_path;
    uint32_t count = relation->index_path_count;
    relation->first_switch = program->index_switch_len;
    relation->switch_count = 0u;
    relation->indexed = count != 0u;
    if (count == 0u)
        return true;
    uint32_t equations = relation->equation_count;
    uint64_t all = equations == 64u ? UINT64_MAX
                                    : (UINT64_C(1) << equations) - 1u;
    /* Which argument's switch takes each path, plus one; zero for none. */
    uint32_t *owner = calloc(count, sizeof(*owner));
    OemIndexPath *sorted = malloc(sizeof(*sorted) * count);
    if (!owner || !sorted) {
        free(owner);
        free(sorted);
        return false;
    }
    uint64_t code = 0u;
    for (uint32_t arg = 0u; arg < relation->arity; arg++) {
        uint32_t taken = 0u;
        bool literal_keys = true;
        for (uint32_t p = 0u; p < count; p++) {
            const OemIndexPath *path = &program->index_paths[first + p];
            if (path->arg != arg || !oem_path_on_functor(program, path))
                continue;
            taken++;
            for (uint32_t key = 0u; key < path->key_count; key++)
                literal_keys = literal_keys &&
                    oem_literal_code(
                        program->index_keys[path->first_key + key].literal,
                        &code);
        }
        if (taken == 0u || !literal_keys)
            continue;
        for (uint32_t p = 0u; p < count; p++) {
            const OemIndexPath *path = &program->index_paths[first + p];
            if (path->arg == arg && oem_path_on_functor(program, path))
                owner[p] = arg + 1u;
        }
    }
    /* The paths read one by one first, then each switch's own. */
    uint32_t placed = 0u;
    for (uint32_t p = 0u; p < count; p++) {
        if (!owner[p])
            sorted[placed++] = program->index_paths[first + p];
    }
    uint32_t generic = placed;
    bool ok = true;
    for (uint32_t arg = 0u; ok && arg < relation->arity; arg++) {
        uint32_t start = placed;
        for (uint32_t p = 0u; p < count; p++) {
            if (owner[p] == arg + 1u)
                sorted[placed++] = program->index_paths[first + p];
        }
        if (placed == start)
            continue;
        const OemIndexPath *own = &sorted[start];
        uint32_t own_count = placed - start;
        OemIndexSwitch sw = {
            .arg = arg, .first_path = first + start, .path_count = own_count,
            .first_length = program->switch_length_len,
            .first_entry = program->switch_entry_len,
        };
        /* The argument's own path, and the conjunction of what every path
         * at a first element admits when refuted. */
        const OemIndexPath *atom_path = NULL;
        uint64_t refuted = all;
        for (uint32_t p = 0u; p < own_count; p++) {
            if (own[p].step_count == 0u)
                atom_path = &own[p];
            else
                refuted &= own[p].free;
        }
        uint64_t atom_free = atom_path ? atom_path->free : all;
        sw.other = atom_free & refuted;
        for (uint32_t key = 0u; atom_path && ok && key < atom_path->key_count;
             key++) {
            Atom *literal =
                program->index_keys[atom_path->first_key + key].literal;
            sw.int_keys = sw.int_keys || literal->kind == ATOM_GROUNDED;
            ok = oem_switch_entry(
                program, 0u, literal,
                oem_path_admits(program, atom_path, literal) & refuted);
        }
        for (uint32_t p = 0u; ok && p < own_count; p++) {
            const OemIndexPath *head = &own[p];
            if (head->step_count == 0u)
                continue;
            uint32_t length = program->index_steps[head->first_step + 1u];
            /* Every other path is refuted by this length. */
            uint64_t base = atom_free;
            for (uint32_t q = 0u; q < own_count; q++) {
                if (q != p && own[q].step_count != 0u)
                    base &= own[q].free;
            }
            OemSwitchLength shape = {
                .length = length, .open_head = base,
                .no_key = base & head->free,
            };
            for (uint32_t key = 0u; ok && key < head->key_count; key++) {
                Atom *literal =
                    program->index_keys[head->first_key + key].literal;
                shape.int_keys =
                    shape.int_keys || literal->kind == ATOM_GROUNDED;
                ok = oem_switch_entry(
                    program, length, literal,
                    base & oem_path_admits(program, head, literal));
            }
            uint32_t added = 0u;
            ok = ok && OEM_PUSH(program, switch_length, shape, &added);
        }
        sw.length_count = program->switch_length_len - sw.first_length;
        uint32_t entries = program->switch_entry_len - sw.first_entry;
        uint32_t size = 8u;
        while (size < 2u * entries)
            size *= 2u;
        sw.first_table = program->switch_table_len;
        sw.entry_mask = size - 1u;
        for (uint32_t slot = 0u; ok && slot < size; slot++) {
            uint32_t added = 0u;
            ok = OEM_PUSH(program, switch_table, 0u, &added);
        }
        for (uint32_t entry = 0u; ok && entry < entries; entry++) {
            const OemSwitchEntry *item =
                &program->switch_entrys[sw.first_entry + entry];
            (void)oem_literal_code(item->literal, &code);
            uint32_t *table = &program->switch_tables[sw.first_table];
            uint32_t at = oem_literal_home(oem_switch_code(code, item->length),
                                           sw.entry_mask);
            while (table[at])
                at = (at + 1u) & sw.entry_mask;
            table[at] = entry + 1u;
        }
        uint32_t added = 0u;
        ok = ok && OEM_PUSH(program, index_switch, sw, &added);
    }
    if (ok) {
        memcpy(&program->index_paths[first], sorted, sizeof(*sorted) * count);
        relation->index_path_count = generic;
        relation->switch_count =
            program->index_switch_len - relation->first_switch;
    }
    free(owner);
    free(sorted);
    return ok;
}

/* Where a head register's term sits: an argument and the steps into it. */
typedef struct {
    bool valid;
    uint32_t arg;
    uint32_t step_count;
    uint32_t steps[2u * OEM_INDEX_MAX_DEPTH];
} OemHeadPlace;

/* Whether two places are the same path. */
static bool oem_same_place(const OemHeadPlace *left,
                           const CettaOpenEquationProgram *program,
                           const OemIndexPath *path) {
    if (left->arg != path->arg || left->step_count != path->step_count)
        return false;
    for (uint32_t index = 0u; index < 2u * left->step_count; index++) {
        if (left->steps[index] != program->index_steps[path->first_step +
                                                       index])
            return false;
    }
    return true;
}

/* A relation's index over the literals its equations' heads test at fixed
 * paths into the arguments: a call leaves out, before it enters any, the
 * equations whose literal differs from the argument's term there, or
 * whose path the argument's structure refutes.  Only for three to 64
 * equations, or two whose first tests agree, and only with a path that
 * tells some of them apart: two equations told apart below their first
 * test, as a queue's empty and nonempty sides are, then leave no frame
 * for the one that cannot match, where first tests already tell two
 * apart without an index.  A list
 * pattern's parts, which a cell may spell, are not indexed.  False only
 * when out of memory. */
static bool oem_build_index(CettaOpenEquationProgram *program,
                            uint32_t relation_index) {
    OemRelation *relation = &program->relations[relation_index];
    uint32_t count = relation->equation_count;
    uint32_t first_path = program->index_path_len;
    relation->first_index_path = first_path;
    relation->index_path_count = 0u;
    relation->first_switch = program->index_switch_len;
    relation->switch_count = 0u;
    relation->indexed = false;
    if (count < 2u || count > 64u ||
        (count == 2u && relation->first_tests_differ))
        return true;
    uint64_t all = count == 64u ? UINT64_MAX : (UINT64_C(1) << count) - 1u;
    for (uint32_t e = 0u; e < count; e++) {
        const OemEquation *eq =
            &program->equations[relation->first_equation + e];
        uint32_t regs = eq->register_count;
        OemHeadPlace *places = calloc(regs ? regs : 1u, sizeof(*places));
        if (!places)
            return false;
        for (uint32_t r = 0u; r < relation->arity && r < regs; r++)
            places[r] = (OemHeadPlace){.valid = true, .arg = r};
        bool ok = true;
        for (uint32_t op_index = 0u; ok && op_index < eq->op_count;
             op_index++) {
            const OemMatchOp *op = &program->ops[eq->first_op + op_index];
            if (op->reg >= regs)
                continue;
            const OemHeadPlace *place = &places[op->reg];
            if (op->kind == OEM_M_EXPR) {
                for (uint32_t child = 0u; child < op->length; child++) {
                    uint32_t target = op->operand + child;
                    if (target >= regs)
                        continue;
                    places[target] = *place;
                    if (!place->valid ||
                        place->step_count >= OEM_INDEX_MAX_DEPTH) {
                        places[target].valid = false;
                        continue;
                    }
                    places[target].steps[2u * place->step_count] = child;
                    places[target].steps[2u * place->step_count + 1u] =
                        op->length;
                    places[target].step_count = place->step_count + 1u;
                }
            } else if (op->kind == OEM_M_LIST) {
                if (op->operand < regs)
                    places[op->operand].valid = false;
                if (op->operand + 1u < regs)
                    places[op->operand + 1u].valid = false;
            } else if (op->kind == OEM_M_ATOM && place->valid) {
                /* The equation's key at this place's path. */
                uint32_t path = first_path;
                for (; path < program->index_path_len; path++) {
                    if (oem_same_place(place, program,
                                       &program->index_paths[path]))
                        break;
                }
                if (path == program->index_path_len) {
                    uint32_t first_step = program->index_step_len;
                    uint32_t added = 0u;
                    for (uint32_t index = 0u;
                         ok && index < 2u * place->step_count; index++)
                        ok = OEM_PUSH(program, index_step,
                                      place->steps[index], &added);
                    ok = ok && OEM_PUSH(program, index_path,
                                        ((OemIndexPath){
                                            .arg = place->arg,
                                            .first_step = first_step,
                                            .step_count = place->step_count,
                                            .free = all,
                                        }),
                                        &added);
                }
                if (ok) {
                    /* Keys stay grouped by path: they are gathered below. */
                    program->index_paths[path].free &=
                        ~(UINT64_C(1) << e);
                }
            }
        }
        free(places);
        if (!ok)
            return false;
    }
    /* The keys of each path: one per distinct literal, with the equations
     * that test it. */
    for (uint32_t path = first_path; path < program->index_path_len;
         path++) {
        OemIndexPath *entry = &program->index_paths[path];
        entry->first_key = program->index_key_len;
        for (uint32_t e = 0u; e < count; e++) {
            if (entry->free & (UINT64_C(1) << e))
                continue;
            const OemEquation *eq =
                &program->equations[relation->first_equation + e];
            uint32_t regs = eq->register_count;
            OemHeadPlace *places = calloc(regs ? regs : 1u, sizeof(*places));
            if (!places)
                return false;
            for (uint32_t r = 0u; r < relation->arity && r < regs; r++)
                places[r] = (OemHeadPlace){.valid = true, .arg = r};
            Atom *literal = NULL;
            for (uint32_t op_index = 0u; !literal && op_index < eq->op_count;
                 op_index++) {
                const OemMatchOp *op = &program->ops[eq->first_op + op_index];
                if (op->reg >= regs)
                    continue;
                const OemHeadPlace *place = &places[op->reg];
                if (op->kind == OEM_M_EXPR) {
                    for (uint32_t child = 0u; child < op->length; child++) {
                        uint32_t target = op->operand + child;
                        if (target >= regs)
                            continue;
                        places[target] = *place;
                        if (!place->valid ||
                            place->step_count >= OEM_INDEX_MAX_DEPTH) {
                            places[target].valid = false;
                            continue;
                        }
                        places[target].steps[2u * place->step_count] = child;
                        places[target].steps[2u * place->step_count + 1u] =
                            op->length;
                        places[target].step_count = place->step_count + 1u;
                    }
                } else if (op->kind == OEM_M_LIST) {
                    if (op->operand < regs)
                        places[op->operand].valid = false;
                    if (op->operand + 1u < regs)
                        places[op->operand + 1u].valid = false;
                } else if (op->kind == OEM_M_ATOM && place->valid &&
                           oem_same_place(place, program, entry)) {
                    literal = op->literal;
                }
            }
            free(places);
            uint32_t key = entry->first_key;
            for (; key < program->index_key_len; key++) {
                if (atom_eq(program->index_keys[key].literal, literal))
                    break;
            }
            uint32_t added = 0u;
            if (key == program->index_key_len &&
                !OEM_PUSH(program, index_key,
                          ((OemIndexKey){.literal = literal}), &added))
                return false;
            program->index_keys[key].equations |= UINT64_C(1) << e;
        }
        entry->key_count = program->index_key_len - entry->first_key;
        if (!oem_build_index_table(program, entry))
            return false;
    }
    relation->index_path_count = program->index_path_len - first_path;
    return oem_build_switches(program, relation_index);
}

static bool oem_compile_relation(CettaOpenEquationProgram *program,
                                 PettaProgram *petta, uint32_t relation_index,
                                 const CettaOpenEquationHost *host,
                                 const char **reason) {
    SymbolId head = program->relations[relation_index].head;
    uint32_t arity = program->relations[relation_index].arity;
    if (host && host->relation_admitted &&
        !host->relation_admitted(host->context, program->space, head,
                                 arity)) {
        *reason = "relation owned by the host";
        return false;
    }
    PettaEquationCandidate *candidates = NULL;
    size_t candidate_count = 0u;
    if (!petta_program_candidate_snapshot(petta, program->space, head,
                                          &candidates, &candidate_count)) {
        *reason = "no candidate snapshot";
        return false;
    }
    /* Equations of this arity, in declaration order. */
    uint32_t first = program->equation_len;
    bool ok = true;
    for (size_t index = 0u; ok && index < candidate_count; index++) {
        Atom *equation = candidates[index].equation;
        Atom *lhs = equation && equation->kind == ATOM_EXPR &&
                equation->expr.len == 3u
            ? equation->expr.elems[1] : NULL;
        if (!lhs || lhs->kind != ATOM_EXPR || lhs->expr.len == 0u ||
            lhs->expr.elems[0]->kind != ATOM_SYMBOL ||
            lhs->expr.elems[0]->sym_id != head) {
            *reason = "wildcard or malformed equation";
            ok = false;
            break;
        }
        if (lhs->expr.len - 1u != arity)
            continue;
        /* A residual of higher-order specialization keeps its plan in the
         * specializer's catalog, where the machine reads it too. */
        const PettaPlanNode *rhs_plan = candidates[index].rhs_plan
            ? candidates[index].rhs_plan
            : petta_specializer_equation_plan(program->space, equation);
        if (!rhs_plan) {
            *reason = "equation has no plan";
            ok = false;
            break;
        }
        ok = oem_compile_equation(program, petta, host, true, true, lhs,
                                  equation->expr.elems[2], rhs_plan, reason);
    }
    free(candidates);
    if (ok && program->equation_len == first) {
        *reason = "no equation of this arity";
        ok = false;
    }
    if (!ok)
        return false;
    program->relations[relation_index].first_equation = first;
    program->relations[relation_index].equation_count =
        program->equation_len - first;
    oem_note_first_tests(program, relation_index);
    if (!oem_build_index(program, relation_index))
        return oem_index_failed(reason);
    return true;
}

/* Whether a relation's equations do nothing the region does itself: heads
 * that only bind their variables, and bodies of host goals (none an
 * operation the region runs), bindings and returns.  Entering such a
 * relation would only relay its body to the host, which then evaluates the
 * same goals across the region's boundary; its calls stay with the host. */
static bool oem_relation_relays(const CettaOpenEquationProgram *program,
                                uint32_t relation) {
    const OemRelation *entry = &program->relations[relation];
    for (uint32_t index = entry->first_equation;
         index < entry->first_equation + entry->equation_count; index++) {
        const OemEquation *equation = &program->equations[index];
        for (uint32_t op = equation->first_op;
             op < equation->first_op + equation->op_count; op++) {
            if (program->ops[op].kind != OEM_M_BIND)
                return false;
        }
        uint32_t end = index + 1u < program->equation_len
            ? program->equations[index + 1u].first_step
            : program->step_len;
        for (uint32_t pc = equation->first_step; pc < end; pc++) {
            const OemStep *step = &program->steps[pc];
            if ((step->kind != OEM_S_HOST && step->kind != OEM_S_BIND &&
                 step->kind != OEM_S_RET && step->kind != OEM_S_FAIL) ||
                (step->kind == OEM_S_HOST &&
                 step->target >= OEM_HOST_ELEMENTS))
                return false;
        }
    }
    return entry->equation_count > 0u;
}

CettaOpenEquationProgram *cetta_open_equation_program_compile(
    PettaProgram *petta, Space *space, SymbolId head, uint32_t arity,
    const CettaOpenEquationHost *host, const char **reason_out) {
    const char *reason = NULL;
    if (reason_out)
        *reason_out = NULL;
    if (!petta || !space || head == SYMBOL_ID_NONE)
        return NULL;
    CettaOpenEquationProgram *program = calloc(1u, sizeof(*program));
    if (!program)
        return NULL;
    atomic_init(&program->refs, 1u);
    program->space = space;
    program->token = space_program_token(space);
    program->spelling = symbol_intern_cstr(g_symbols, "_");
    uint32_t entry = 0u;
    bool ok = oem_relation_index(program, head, arity, &entry) && entry == 0u;
    for (uint32_t index = 0u; ok && index < program->relation_len; index++) {
        if (program->relations[index].local)
            ok = oem_compile_local_relation(program, petta, host, index,
                                            &reason);
        else
            ok = oem_compile_relation(program, petta, index, host, &reason);
    }
    if (!ok) {
        if (reason_out)
            *reason_out = reason ? reason : "compile";
        cetta_open_equation_program_release(program);
        return NULL;
    }
    program->entry_relays = oem_relation_relays(program, entry);
    program->depth_pruning_safe = true;
    for (uint32_t index = 0u; index < program->relation_len; index++) {
        if (program->relations[index].failed) {
            program->depth_pruning_safe = false;
            break;
        }
    }
    for (uint32_t index = 0u; index < program->step_len; index++) {
        const OemStep *step = &program->steps[index];
        /* A runtime check at an effect is too late: depth pruning could
         * skip the call containing that effect altogether. Arithmetic also
         * needs a totality certificate before it can enter this fragment.
         * Dynamic calls and host-owned head occurrences remain exact. */
        if (step->kind == OEM_S_YIELD || step->kind == OEM_S_CUT ||
            step->kind == OEM_S_EQUATION_CUT || step->kind == OEM_S_PRIM ||
            step->kind == OEM_S_MATCH ||
            (step->kind == OEM_S_TEST && step->op != SYMBOL_ID_NONE) ||
            (step->kind == OEM_S_HOST &&
             step->target != OEM_HOST_ALTERNATIVES)) {
            program->depth_pruning_safe = false;
            break;
        }
    }
    return program;
}

bool cetta_open_equation_program_entry_relays(
    const CettaOpenEquationProgram *program) {
    return program && program->entry_relays;
}

/* ---- the region store ---------------------------------------------------- */

/* A pending caller: the program version its code belongs to, its slots, its
 * resume point and its destination for the callee.  After the Space program
 * changes, in-flight activations keep running their own version's code and
 * new calls enter the current version. */
typedef struct OemCont {
    const struct OemCont *parent;
    const CettaOpenEquationProgram *program;
    Atom **locals;
    uint32_t local_count;
    uint32_t pc;
    uint32_t pattern;
    /* The depth of the activation that continues here. */
    uint32_t depth : 30;
    /* The return of a call dispatched at run time: until it resumes, the
     * call's body runs under the implicit handler, and an error raised
     * there fails that path alone (DispatchErrorScope). */
    uint32_t recovers : 1;
    /* The call's pattern is a slot at its first occurrence: the callee's
     * activation meets no destination, and its return stores the value it
     * returns there, as a return frame receives an answer. */
    uint32_t store : 1;
} OemCont;

/* A match's rows, snapshotted in the region when the match ran; `arena`
 * is the identity of the arena that holds the block. */
typedef struct {
    uint32_t len;
    uint32_t arena;
    Atom *rows[];
} OemRows;

/* The cells a host goal left the region through, in the order of the
 * variables it went out with, and the host's choice height beneath the
 * goal, UINT32_MAX until the host reports it. */
typedef struct {
    uint32_t count;
    uint32_t height;
    uint32_t cells[];
} OemHostCells;

/* A call with untried alternatives: the equations of `relation` in the
 * version that was current when the call was made.  A match's frame
 * (`relation` OEM_MATCH_FRAME) instead tries its rows from `next`; its
 * `args[0]` is the pattern and its `cont` resumes after the match in the
 * matching activation. */
typedef struct {
    union {
        const CettaOpenEquationProgram *program;
        OemRows *rows;
        OemHostCells *host;
        struct OemCollectBox *collect;
    };
    uint32_t relation;
    uint32_t next;
    Atom **args;
    const OemCont *cont;
    ArenaMark mark;
    uint32_t cell_mark;
    uint32_t trail_mark;
    /* The store trail's length when the frame was pushed. */
    uint32_t store_mark;
    /* The depth of the activation the frame resumes: for a call, one
     * deeper than its caller. */
    uint32_t depth;
    /* For a call of an indexed relation, the equations its index leaves,
     * found as the call enters its first. */
    uint64_t candidates;
} OemFrame;

#define OEM_MATCH_FRAME UINT32_MAX
/* A host goal's frame: its `cont` resumes after the HOST step. */
#define OEM_HOST_FRAME (UINT32_MAX - 1u)
/* A resumption: its `cont` is where resumed code continues, run as soon as
 * the frame is reached. */
#define OEM_RESUME_FRAME (UINT32_MAX - 2u)
/* A collection: its answers are in `collect`, and when the frame is
 * reached, the region's state is the caller's again, and `cont` continues
 * with their list. */
#define OEM_COLLECT_FRAME (UINT32_MAX - 3u)
/* A value's elements: `args[1]` is the list, and each element in turn
 * unifies with `args[0]` and continues at `cont`. */
#define OEM_ELEMENTS_FRAME (UINT32_MAX - 4u)
/* A once: its CUT step drops it with the frames above it.  Reached by
 * backtracking, its body has no answer left, and it fails. */
#define OEM_ONCE_FRAME (UINT32_MAX - 5u)
/* The lowest of the frame kinds that are no relation's. */
#define OEM_FIRST_MARKER_FRAME OEM_ONCE_FRAME

/* A collection's answers, copied out of their branches into the cursor's
 * collection storage, newest first; `mark` is that storage's mark when the
 * collection began, which its completion returns to. */
typedef struct OemCollectItem {
    Atom *value;
    struct OemCollectItem *prev;
} OemCollectItem;

typedef struct OemCollectBox {
    ArenaMark mark;
    OemCollectItem *last;
    uint32_t count;
    /* The answers to take, zero for all. */
    uint32_t limit;
} OemCollectBox;

/* A slot a first-occurrence store filled while a frame newer than its
 * activation was live: restoring such a frame empties the slot again. */
typedef struct {
    Atom **locals;
    uint32_t slot;
} OemStore;

/* A stored row's variable and the cell its current copy uses. */
typedef struct {
    VarId id;
    Atom *cell;
} OemFreshVar;

/* Whether a named space declares a `SpaceOf` schema. */
typedef struct {
    SymbolId name;
    bool schema;
} OemSchemaEntry;

typedef struct {
    Atom *left;
    Atom *right;
} OemPair;

/* A pending expression of the answer resolver: its children's results are
 * collected from `base` in the result stack. */
typedef struct {
    Atom *atom;
    uint32_t next;
    uint32_t base;
    /* An export copies this expression to stable storage (see below). */
    bool promote;
    /* A collection copying this expression, the longest suffix of its
     * storage, on behalf of a shorter view of it, which becomes a header
     * over the copy. */
    Atom *requester;
} OemResolveItem;

struct CettaOpenEquationCursor {
    /* The entry version; the versions a call entered after a change stay
     * callers may still run their code. */
    CettaOpenEquationProgram *program;
    CettaOpenEquationProgram **versions;
    uint32_t version_len, version_cap;
    Arena region;
    /* The old generation: what collections promoted from the region, the
     * young generation, which minor collections leave in place.  Cells
     * below `old_cells` may be named by old atoms, so they keep their
     * indices until a major collection, which collects both generations
     * once the old one reaches `major_after`. */
    Arena old;
    bool old_ready;
    uint32_t old_cells;
    size_t major_after;
    /* What a minor collection traces besides the frames above
     * `frame_low` (RememberedSetTrace.minor_trace_complete).  The frame
     * stack's, trail's and store trail's least lengths since the last
     * collection mark what is unchanged below them.  The cells below
     * `old_cells` bound since then, and the slots of older activations
     * stored since then, are the only old places that may name young
     * objects: `oem_bind` and `oem_store_slot` remember them.  A slot
     * vector's header word holds, above its birth height, the collection
     * epoch it was made in. */
    uint32_t frame_low, trail_low, store_low;
    uint32_t *remembered_cells;
    uint32_t remembered_cell_len, remembered_cell_cap;
    OemStore *remembered_slots;
    uint32_t remembered_slot_len, remembered_slot_cap;
    uint32_t collect_epoch;
    /* `collect_epoch` in a header word's upper half. */
    uintptr_t slot_epoch_bits;
    /* Minor collections declined since the last that copied: each doubles
     * the growth the region waits for before the next is tried. */
    uint32_t collect_declines;
    /* Major collections declined since the last that copied: each doubles
     * the growth of the old generation the next one waits for, as minor
     * declines do for the region, up to OEM_MAJOR_DECLINE_SHIFT_MAX. */
    uint32_t major_declines;
    /* The forwarding table's final length in the last collection of each
     * kind (minor, major) and phase (trace, copy): the next one starts at a
     * size that holds it without growing. */
    size_t forward_hints[2][2];
    /* Terms one step builds and drops before the next (a ground add-atom's
     * call), outside the region so they leave nothing behind in it;
     * initialized on first use. */
    Arena scratch;
    bool scratch_ready;
    /* The variable naming cell i is the same immutable atom every time the
     * cell index is reused, so it lives for the cursor, outside the region
     * that backtracking resets; so does `admitted`. */
    Arena names;
    Atom **cell_names;
    uint32_t cell_name_len, cell_name_cap;
    /* Collections' answers, outside the region that backtracking resets;
     * a collection's completion returns it to the collection's mark.
     * Initialized on first use. */
    Arena collected;
    bool collected_ready;
    Arena *answer_arena;
    CettaFrameIdentity epoch;
    Atom **cells;
    uint32_t cell_len, cell_cap;
    uint32_t *trail;
    uint32_t trail_len, trail_cap;
    OemStore *stores;
    uint32_t store_len, store_cap;
    OemFrame *frames;
    uint32_t frame_len, frame_cap;
    OemPair *pairs;
    uint32_t pair_cap;
    Atom **walk;
    uint32_t walk_cap;
    /* An activation's registers, which head matching alone reads: one
     * vector for the cursor, outside the region. */
    Atom **regs;
    uint32_t reg_cap;
    /* Registers of the head test that selects an equation. */
    Atom **probe;
    uint32_t probe_cap;
    uint32_t query_var_count;
    /* The caller's variables, borrowed for the duration of one call. */
    Atom *const *query_vars;
    Atom **query_cells;
    Atom **query_values;
    /* The call's destination: the consumer's expected value, over the
     * query cells. */
    Atom *destination;
    /* One fresh answer variable per unbound cell, for the answer being
     * published; `fresh_touched` lists the entries to clear after it. */
    Atom **answer_fresh;
    uint32_t answer_fresh_cap;
    uint32_t *fresh_touched;
    uint32_t fresh_touched_len, fresh_touched_cap;
    OemResolveItem *resolve_stack;
    uint32_t resolve_cap;
    Atom **resolve_results;
    uint32_t resolve_result_cap;
    CettaOpenEquationRuntime runtime;
    /* Advanced by each authority change the host reports; `version_epochs`
     * records the epoch each retained version was entered under. */
    uint64_t authority_epoch;
    /* Heads an evaluated call named that no program of the region runs,
     * under the authorities and the space program of the lookup. */
    struct OemAbsentHead *absent;
    uint32_t absent_len, absent_cap;
    uint64_t *version_epochs;
    uint32_t poll;
    CettaOpenEquationHandoff handoff;
    /* The depth bound failed an activation whose head matched. */
    bool bound_cut;
    /* The host's choice height the last CUT step reported. */
    uint32_t cut_height;
    /* Cell bindings made so far; an attempt that failed after one of its own
     * bindings is classified apart from one that failed before any. */
    uint64_t binds;
    /* The region's size at which the next collection runs. */
    size_t collect_after;
    /* The variables of the row being copied for a match, or of a host's
     * answer or a collected answer being imported, with their positions by
     * identifier past a short scan (oem_fresh_var). */
    OemFreshVar *fresh_vars;
    uint32_t fresh_var_len, fresh_var_cap;
    CettaVarIndex fresh_index;
    /* The call's variables' positions by identifier, past a short scan. */
    CettaVarIndex query_index;
    /* The rows of the small space the region read last, as the space's
     * index gives them, with each row's root key (oem_row_key), for the
     * revision `rows_read` names. */
    SpaceReadToken rows_read;
    Atom *every_row;
    Atom **space_rows;
    uint64_t *space_row_keys;
    /* Each row's part keys (oem_part_key), OEM_ROW_PARTS per row. */
    uint64_t *space_row_parts;
    uint32_t space_row_len, space_row_cap;
    /* The newest HOST step's goal, destination and variables, in the
     * answer arena; valid until the next step. */
    Atom *host_goal;
    Atom *host_destination;
    Atom **host_vars;
    uint32_t host_var_count, host_var_cap;
    const PettaPlanNode *host_plan;
    CettaOpenEquationHostMode host_mode;
    /* The newest host goal was issued inside a call dispatched at run
     * time, whose handler it runs under. */
    bool host_recovers;
    /* The cursor has entered a call dispatched at run time, whose return
     * recovers (oem_recovering). */
    bool recovering;
    /* The multiplicity of the answer being published, and whether a count
     * fold gave it (cetta_open_equation_cursor_answer_weight). */
    uint64_t answer_weight;
    bool answer_folded;
    /* The host's result of a ground add-atom, kept once for the cursor:
     * every later equal result shares it, so a binder it fills keeps no
     * region atom alive.  An answer or a host goal that holds it holds a
     * copy, since the cursor may close before the answer is gone. */
    Atom *admitted;
    /* The host's spelling of false and true, made once for the cursor, in
     * `names`: an answer or a host goal that holds one holds a copy. */
    Atom *truths[2];
    /* Named spaces' schemas, known while `schema_epoch` is
     * `authority_epoch + 1`. */
    OemSchemaEntry *schemas;
    uint32_t schema_len, schema_cap;
    uint64_t schema_epoch;
    CettaOpenEquationStats stats;
};

typedef enum {
    OEM_RUN_FAILED = 0,
    OEM_RUN_CALLED,
    OEM_RUN_ANSWER,
    OEM_RUN_HANDOFF,
    /* An operation raised an error; `value_out` holds it. */
    OEM_RUN_RAISE,
    /* A host goal waits for its host. */
    OEM_RUN_HOST,
    /* A once committed through a host goal's frame: the host drops its
     * choices above `cut_height`. */
    OEM_RUN_CUT,
    /* Within the body runner only: a match counted its rows, and the step
     * after it runs once for all of them. */
    OEM_RUN_FOLDED,
    /* Within the body runner only: an evaluated call entered a relation of
     * the region, whose frame is pushed. */
    OEM_RUN_ENTERED,
} OemRun;

static inline bool oem_is_cell(const CettaOpenEquationCursor *cursor,
                               const Atom *atom, uint32_t *index_out) {
    if (atom->kind != ATOM_VAR ||
        var_epoch_suffix(atom->var_id) != cursor->epoch)
        return false;
    *index_out = var_base_id(atom->var_id) - 1u;
    return true;
}

static inline Atom *oem_deref(const CettaOpenEquationCursor *cursor,
                              Atom *atom) {
    uint32_t index = 0u;
    while (oem_is_cell(cursor, atom, &index)) {
        Atom *value = cursor->cells[index];
        if (!value)
            return atom;
        atom = value;
    }
    return atom;
}

/* The region's bump allocation, inline where calls allocate: a slot
 * vector, a continuation, a frame's arguments.  A full block, and the
 * runtime-stats build, which counts every allocation, take arena_alloc. */
static inline __attribute__((always_inline)) void *oem_region_alloc(
    CettaOpenEquationCursor *cursor, size_t size) {
#if CETTA_BUILD_WITH_RUNTIME_STATS
    return arena_alloc(&cursor->region, size);
#else
    Arena *region = &cursor->region;
    ArenaBlock *head = region->head;
    size = (size + 7u) & ~(size_t)7u;
    if (__builtin_expect(head && head->used + size <= head->capacity, 1)) {
        void *at = head->data + head->used;
        head->used += size;
        region->live_bytes += size;
        return at;
    }
    return arena_alloc(region, size);
#endif
}

static Atom *oem_new_cell(CettaOpenEquationCursor *cursor) {
    if (cursor->cell_len >= UINT32_MAX - 1u ||
        !oem_reserve((void **)&cursor->cells, &cursor->cell_cap,
                     cursor->cell_len + 1u, sizeof(*cursor->cells)))
        return NULL;
    uint32_t index = cursor->cell_len;
    if (index == cursor->cell_name_len) {
        if (!oem_reserve((void **)&cursor->cell_names,
                         &cursor->cell_name_cap, index + 1u,
                         sizeof(*cursor->cell_names)))
            return NULL;
        Atom *var = atom_var_with_spelling(
            &cursor->names, cursor->program->spelling,
            var_epoch_id((VarId)index + 1u, cursor->epoch));
        if (!var)
            return NULL;
        cursor->cell_names[cursor->cell_name_len++] = var;
    }
    cursor->cells[index] = NULL;
    cursor->cell_len++;
    return cursor->cell_names[index];
}

/* The barriers' records, out of line: most writes are young. */
static __attribute__((noinline)) bool oem_remember_cell(
    CettaOpenEquationCursor *cursor, uint32_t index) {
    if (!oem_reserve((void **)&cursor->remembered_cells,
                     &cursor->remembered_cell_cap,
                     cursor->remembered_cell_len + 1u,
                     sizeof(*cursor->remembered_cells)))
        return false;
    cursor->remembered_cells[cursor->remembered_cell_len++] = index;
    return true;
}

static __attribute__((noinline)) bool oem_remember_slot(
    CettaOpenEquationCursor *cursor, Atom **locals, uint32_t slot) {
    if (!oem_reserve((void **)&cursor->remembered_slots,
                     &cursor->remembered_slot_cap,
                     cursor->remembered_slot_len + 1u,
                     sizeof(*cursor->remembered_slots)))
        return false;
    cursor->remembered_slots[cursor->remembered_slot_len++] =
        (OemStore){.locals = locals, .slot = slot};
    return true;
}

static inline __attribute__((always_inline)) bool oem_bind(
    CettaOpenEquationCursor *cursor, uint32_t index, Atom *value) {
    uint32_t newest = cursor->frame_len
        ? cursor->frames[cursor->frame_len - 1u].cell_mark : 0u;
    if (index < newest) {
        if (!oem_reserve((void **)&cursor->trail, &cursor->trail_cap,
                         cursor->trail_len + 1u, sizeof(*cursor->trail)))
            return false;
        cursor->trail[cursor->trail_len++] = index;
        cursor->stats.trail_writes++;
    }
    /* An old cell may now name a young object, whether trailed or not:
     * the next minor collection traces it (barrier_write_remember). */
    if (index < cursor->old_cells && !oem_remember_cell(cursor, index))
        return false;
    cursor->cells[index] = value;
    cursor->binds++;
    return true;
}

/* An invocation-local source-pointer map. An occupied key with no image
 * marks an active traversal; published images are immutable. Callers own
 * the semantic context and release the map before any arena reset. */
typedef struct {
    const Atom *source;
    Atom *image;
} OemAtomMapEntry;

typedef struct {
    OemAtomMapEntry inline_entries[64];
    OemAtomMapEntry *entries;
    size_t capacity, used;
} OemAtomMap;

static void oem_atom_map_init(OemAtomMap *map) {
    map->entries = NULL;
    map->capacity = 0u;
    map->used = 0u;
}

static size_t oem_atom_map_hash(const Atom *source) {
    uint64_t hash = (uint64_t)(uintptr_t)source >> 4u;
    hash ^= hash >> 33u;
    hash *= UINT64_C(0xff51afd7ed558ccd);
    hash ^= hash >> 33u;
    return (size_t)hash;
}

static OemAtomMapEntry *oem_atom_map_find(OemAtomMap *map, const Atom *source) {
    if (!map->capacity)
        return NULL;
    size_t index = oem_atom_map_hash(source) & (map->capacity - 1u);
    while (map->entries[index].source) {
        if (map->entries[index].source == source)
            return &map->entries[index];
        index = (index + 1u) & (map->capacity - 1u);
    }
    return NULL;
}

static bool oem_atom_map_begin(OemAtomMap *map, const Atom *source) {
    if (!map->capacity) {
        memset(map->inline_entries, 0, sizeof(map->inline_entries));
        map->entries = map->inline_entries;
        map->capacity = 64u;
    }
    if (map->used >= map->capacity / 2u) {
        if (map->capacity > SIZE_MAX / (2u * sizeof(*map->entries)))
            return false;
        size_t capacity = map->capacity * 2u;
        OemAtomMapEntry *entries = calloc(capacity, sizeof(*entries));
        if (!entries)
            return false;
        for (size_t old = 0u; old < map->capacity; old++) {
            OemAtomMapEntry entry = map->entries[old];
            if (!entry.source)
                continue;
            size_t index = oem_atom_map_hash(entry.source) & (capacity - 1u);
            while (entries[index].source)
                index = (index + 1u) & (capacity - 1u);
            entries[index] = entry;
        }
        if (map->entries != map->inline_entries)
            free(map->entries);
        map->entries = entries;
        map->capacity = capacity;
    }
    size_t index = oem_atom_map_hash(source) & (map->capacity - 1u);
    while (map->entries[index].source)
        index = (index + 1u) & (map->capacity - 1u);
    map->entries[index].source = source;
    map->used++;
    return true;
}

static void oem_atom_map_free(OemAtomMap *map) {
    if (map->entries != map->inline_entries)
        free(map->entries);
}

/* Does cell `index` occur in `atom`? The dereferenced graph is fixed
 * throughout this read. GraphOccurrence's worklist projection permits
 * duplicate pending pointers, but expands each expression only once. */
static bool oem_occurs(CettaOpenEquationCursor *cursor, uint32_t index,
                       Atom *atom, bool *failed) {
    uint32_t len = 0u;
    bool found = false;
    OemAtomMap visited;
    oem_atom_map_init(&visited);
    if (!oem_reserve((void **)&cursor->walk, &cursor->walk_cap, 1u,
                     sizeof(*cursor->walk))) {
        *failed = true;
        return false;
    }
    cursor->walk[len++] = atom;
    while (len > 0u) {
        Atom *current = oem_deref(cursor, cursor->walk[--len]);
        uint32_t other = 0u;
        if (oem_is_cell(cursor, current, &other)) {
            if (other == index) {
                found = true;
                break;
            }
            continue;
        }
        if (current->kind != ATOM_EXPR || !atom_has_vars(current) ||
            oem_atom_map_find(&visited, current))
            continue;
        if (!oem_atom_map_begin(&visited, current) ||
            current->expr.len > UINT32_MAX - len ||
            !oem_reserve((void **)&cursor->walk, &cursor->walk_cap,
                         len + current->expr.len, sizeof(*cursor->walk))) {
            *failed = true;
            break;
        }
        for (CettaExprIndex child = 0u; child < current->expr.len; child++)
            cursor->walk[len++] = current->expr.elems[child];
    }
    oem_atom_map_free(&visited);
    return found;
}

/* An internal cons cell: the list value whose first element is its head and
 * whose rest is its tail.  No cell exists until one has been built. */
static inline bool oem_is_list_cell(const Atom *atom) {
    return petta_semantics_open_cons_built() && atom &&
           atom->kind == ATOM_EXPR &&
           petta_semantics_is_open_cons_value(atom);
}

/* A resolved operand of a grounded operation, which reads structure: every
 * closed chain of cons cells in it is the flat list it spells, as the
 * machine reads it.  NULL when the rewrite could not be made. */
static Atom *oem_grounded_operand(CettaOpenEquationCursor *cursor,
                                  Atom *operand) {
    if (!operand || !petta_semantics_open_cons_built() ||
        !atom_structural_may_have_list_carrier(operand))
        return operand;
    return petta_semantics_flatten_closed_open_cons(&cursor->region, operand);
}

typedef enum {
    OEM_UNIFY_FAIL = 0,
    OEM_UNIFY_OK,
    OEM_UNIFY_ERROR,
} OemUnify;

static OemUnify oem_unify(CettaOpenEquationCursor *cursor, Atom *left,
                          Atom *right) {
    uint32_t len = 0u;
    if (!oem_reserve((void **)&cursor->pairs, &cursor->pair_cap, 1u,
                     sizeof(*cursor->pairs)))
        return OEM_UNIFY_ERROR;
    cursor->pairs[len++] = (OemPair){left, right};
    while (len > 0u) {
        OemPair pair = cursor->pairs[--len];
        Atom *a = oem_deref(cursor, pair.left);
        Atom *b = oem_deref(cursor, pair.right);
        if (a == b)
            continue;
        uint32_t left_cell = 0u;
        uint32_t right_cell = 0u;
        bool left_open = oem_is_cell(cursor, a, &left_cell);
        bool right_open = oem_is_cell(cursor, b, &right_cell);
        if (left_open || right_open) {
            /* Two cells: the younger refers to the older, so the binding
             * is discarded with the younger cell and needs no trail. */
            bool bind_left = left_open &&
                (!right_open || left_cell > right_cell);
            uint32_t index = bind_left ? left_cell : right_cell;
            Atom *value = bind_left ? b : a;
            bool failed = false;
            if (oem_occurs(cursor, index, value, &failed))
                return OEM_UNIFY_FAIL;
            if (failed || !oem_bind(cursor, index, value))
                return OEM_UNIFY_ERROR;
            continue;
        }
        if (a->kind == ATOM_VAR || b->kind == ATOM_VAR)
            return OEM_UNIFY_ERROR;
        /* A cons cell and a flat list are one list value when their
         * elements are: the cell's head meets the list's first element and
         * its tail the rest of the list, which shares the list's storage.
         * A cell is never the empty list. */
        bool left_list_cell = oem_is_list_cell(a);
        if (left_list_cell != oem_is_list_cell(b)) {
            Atom *list_cell = left_list_cell ? a : b;
            Atom *list = left_list_cell ? b : a;
            if (list->kind != ATOM_EXPR || list->expr.len == 0u)
                return OEM_UNIFY_FAIL;
            Atom *rest = atom_expr_suffix(&cursor->region, list, 1u);
            if (!rest ||
                !oem_reserve((void **)&cursor->pairs, &cursor->pair_cap,
                             len + 2u, sizeof(*cursor->pairs)))
                return OEM_UNIFY_ERROR;
            cursor->pairs[len++] =
                (OemPair){list_cell->expr.elems[2], rest};
            cursor->pairs[len++] =
                (OemPair){list_cell->expr.elems[1], list->expr.elems[0]};
            continue;
        }
        if (a->kind == ATOM_EXPR && b->kind == ATOM_EXPR) {
            if (a->expr.len != b->expr.len)
                return OEM_UNIFY_FAIL;
            /* Equal ground expressions are one value; unequal ones differ
             * too, unless a cons cell inside one spells a list the other
             * holds flat. */
            if (!atom_has_vars(a) && !atom_has_vars(b) &&
                (!petta_semantics_open_cons_built() ||
                 (!atom_structural_may_have_list_carrier(a) &&
                  !atom_structural_may_have_list_carrier(b)))) {
                if (!atom_eq(a, b))
                    return OEM_UNIFY_FAIL;
                continue;
            }
            if (!oem_reserve((void **)&cursor->pairs, &cursor->pair_cap,
                             len + a->expr.len, sizeof(*cursor->pairs)))
                return OEM_UNIFY_ERROR;
            for (CettaExprIndex child = a->expr.len; child-- > 0u;)
                cursor->pairs[len++] =
                    (OemPair){a->expr.elems[child], b->expr.elems[child]};
            continue;
        }
        if (!atom_eq(a, b))
            return OEM_UNIFY_FAIL;
    }
    return OEM_UNIFY_OK;
}

static Atom *oem_instantiate_in(CettaOpenEquationCursor *cursor,
                                Arena *arena,
                                const CettaOpenEquationProgram *program,
                                Atom **locals, uint32_t template_index) {
    const OemTemplate *item = &program->templates[template_index];
    if (item->kind == OEM_T_LITERAL)
        return item->literal;
    if (item->kind == OEM_T_SLOT)
        return locals[item->slot];
    enum { OEM_INLINE_CHILDREN = 16u };
    Atom *inline_children[OEM_INLINE_CHILDREN];
    Atom **children = item->count <= OEM_INLINE_CHILDREN
        ? inline_children
        : arena_alloc(arena, sizeof(*children) * item->count);
    if (!children)
        return NULL;
    /* A literal or a slot child is read in place; only a built child
     * recurses. */
    for (uint32_t index = 0u; index < item->count; index++) {
        uint32_t child_index = program->template_children[item->first + index];
        const OemTemplate *child = &program->templates[child_index];
        children[index] = child->kind == OEM_T_LITERAL ? child->literal
            : child->kind == OEM_T_SLOT ? locals[child->slot]
            : oem_instantiate_in(cursor, arena, program, locals,
                                 child_index);
        if (!children[index])
            return NULL;
    }
    return item->kind == OEM_T_BUILD
        ? atom_expr(arena, children, item->count)
        : petta_semantics_open_cons_value(arena, children[0], children[1]);
}

/* A literal or slot template is read in place; only a built one calls the
 * recursive builder. */
static inline __attribute__((always_inline)) Atom *oem_instantiate(
    CettaOpenEquationCursor *cursor, const CettaOpenEquationProgram *program,
    Atom **locals, uint32_t template_index) {
    const OemTemplate *item = &program->templates[template_index];
    if (item->kind == OEM_T_LITERAL)
        return item->literal;
    if (item->kind == OEM_T_SLOT)
        return locals[item->slot];
    return oem_instantiate_in(cursor, &cursor->region, program, locals,
                              template_index);
}

static Arena *oem_scratch(CettaOpenEquationCursor *cursor) {
    if (!cursor->scratch_ready) {
        arena_init(&cursor->scratch);
        arena_set_runtime_kind(&cursor->scratch,
                               CETTA_ARENA_RUNTIME_KIND_SCRATCH);
        arena_set_hashcons(&cursor->scratch, NULL);
        cursor->scratch_ready = true;
    }
    return &cursor->scratch;
}

static OemUnify oem_unify_template(CettaOpenEquationCursor *cursor,
                                   const CettaOpenEquationProgram *program,
                                   Atom **locals, uint32_t template_index,
                                   Atom *term, uint32_t depth);

/* A list template meets a value by its first element and its rest: a cons
 * cell's fields, or a flat list's first element and the suffix that shares
 * its storage.  A pattern list takes a value spelled `(cons h t)` as that
 * cell.  Out of line: most templates are built expressions. */
static __attribute__((noinline)) OemUnify oem_unify_list_template(
    CettaOpenEquationCursor *cursor, const CettaOpenEquationProgram *program,
    Atom **locals, uint32_t template_index, Atom *term, uint32_t depth) {
    const OemTemplate *item = &program->templates[template_index];
    term = oem_deref(cursor, term);
    uint32_t cell = 0u;
    if (oem_is_cell(cursor, term, &cell) || depth > OEM_MAX_DEPTH) {
        Atom *built = oem_instantiate(cursor, program, locals, template_index);
        return built ? oem_unify(cursor, built, term) : OEM_UNIFY_ERROR;
    }
    Atom *first = NULL;
    Atom *rest = NULL;
    if (oem_is_list_cell(term) ||
        (item->kind == OEM_T_PATTERN_LIST &&
         petta_semantics_is_cons_constraint(term))) {
        first = term->expr.elems[1];
        rest = term->expr.elems[2];
    } else if (term->kind == ATOM_EXPR && term->expr.len > 0u) {
        first = term->expr.elems[0];
        rest = atom_expr_suffix(&cursor->region, term, 1u);
        if (!rest)
            return OEM_UNIFY_ERROR;
    } else {
        return term->kind == ATOM_VAR ? OEM_UNIFY_ERROR : OEM_UNIFY_FAIL;
    }
    OemUnify unified = oem_unify_template(
        cursor, program, locals, program->template_children[item->first],
        first, depth + 1u);
    return unified != OEM_UNIFY_OK
        ? unified
        : oem_unify_template(cursor, program, locals,
                             program->template_children[item->first + 1u],
                             rest, depth + 1u);
}

/* Unify a template, read over `locals`, with a term without building the
 * template: only a part that meets an unbound cell is instantiated. */
static OemUnify oem_unify_template(CettaOpenEquationCursor *cursor,
                                   const CettaOpenEquationProgram *program,
                                   Atom **locals, uint32_t template_index,
                                   Atom *term, uint32_t depth) {
    const OemTemplate *item = &program->templates[template_index];
    if (item->kind == OEM_T_LITERAL)
        return oem_unify(cursor, item->literal, term);
    if (item->kind == OEM_T_SLOT)
        return oem_unify(cursor, locals[item->slot], term);
    if (item->kind != OEM_T_BUILD)
        return oem_unify_list_template(cursor, program, locals,
                                       template_index, term, depth);
    term = oem_deref(cursor, term);
    uint32_t cell = 0u;
    if (oem_is_cell(cursor, term, &cell) || depth > OEM_MAX_DEPTH ||
        oem_is_list_cell(term)) {
        Atom *built = oem_instantiate(cursor, program, locals, template_index);
        return built ? oem_unify(cursor, built, term) : OEM_UNIFY_ERROR;
    }
    if (term->kind != ATOM_EXPR || term->expr.len != item->count)
        return term->kind == ATOM_VAR ? OEM_UNIFY_ERROR : OEM_UNIFY_FAIL;
    for (uint32_t index = 0u; index < item->count; index++) {
        OemUnify unified = oem_unify_template(
            cursor, program, locals,
            program->template_children[item->first + index],
            term->expr.elems[index], depth + 1u);
        if (unified != OEM_UNIFY_OK)
            return unified;
    }
    return OEM_UNIFY_OK;
}

/* Back to a frame's state.  A region already at the frame's mark keeps its
 * allocation epoch, so what the host memoizes about region atoms (a stored
 * atom's identity) stays valid while nothing is released. */
static inline void oem_restore(CettaOpenEquationCursor *cursor,
                               const OemFrame *frame) {
    /* A region grown since the mark is reset at once; one that seems
     * unchanged is checked in full, and left as it is when it is, so its
     * reset epoch, which keys the memos of its atoms, stays. */
    const Arena *region = &cursor->region;
    if (region->head != frame->mark.head ||
        (region->head && region->head->used != frame->mark.used) ||
        !arena_at_mark(region, frame->mark))
        arena_reset(&cursor->region, frame->mark);
    /* The least lengths since the last collection move only when a
     * restore shortens the trails. */
    if (cursor->trail_len > frame->trail_mark) {
        do
            cursor->cells[cursor->trail[--cursor->trail_len]] = NULL;
        while (cursor->trail_len > frame->trail_mark);
        if (cursor->trail_len < cursor->trail_low)
            cursor->trail_low = cursor->trail_len;
    }
    if (cursor->store_len > frame->store_mark) {
        do {
            const OemStore *store = &cursor->stores[--cursor->store_len];
            store->locals[store->slot] = NULL;
        } while (cursor->store_len > frame->store_mark);
        if (cursor->store_len < cursor->store_low)
            cursor->store_low = cursor->store_len;
    }
    cursor->cell_len = frame->cell_mark;
    /* Cells created after the frame are gone; old atoms still reachable
     * were made before it and name none of them, and no slot holds what a
     * store after it put there. */
    if (cursor->old_cells > cursor->cell_len)
        cursor->old_cells = cursor->cell_len;
}

static bool oem_push_frame(CettaOpenEquationCursor *cursor,
                           const CettaOpenEquationProgram *program,
                           uint32_t relation, Atom **args,
                           const OemCont *cont, uint32_t depth) {
    if (cursor->runtime.depth_bound && program && !program->depth_pruning_safe) {
        cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_UNSUPPORTED;
        return false;
    }
    if (!oem_reserve((void **)&cursor->frames, &cursor->frame_cap,
                     cursor->frame_len + 1u, sizeof(*cursor->frames)))
        return false;
    cursor->frames[cursor->frame_len++] = (OemFrame){
        .program = program,
        .relation = relation,
        .args = args,
        .cont = cont,
        .mark = arena_mark(&cursor->region),
        .cell_mark = cursor->cell_len,
        .trail_mark = cursor->trail_len,
        .store_mark = cursor->store_len,
        .depth = depth,
        .candidates = UINT64_MAX,
    };
    return true;
}

static inline bool oem_frame_is_match(const OemFrame *frame) {
    return frame->relation == OEM_MATCH_FRAME;
}

static inline bool oem_frame_is_host(const OemFrame *frame) {
    return frame->relation == OEM_HOST_FRAME;
}

static inline uint32_t oem_frame_arity(const OemFrame *frame) {
    return oem_frame_is_match(frame) ? 1u
        : frame->relation == OEM_ELEMENTS_FRAME ? 2u
        : frame->relation >= OEM_FIRST_MARKER_FRAME ? 0u
        : frame->program->relations[frame->relation].arity;
}

/* Drop the frames from `len` on; the frames below the least length since
 * the last collection are unchanged since it. */
static inline void oem_truncate_frames(CettaOpenEquationCursor *cursor,
                                       uint32_t len) {
    cursor->frame_len = len;
    if (len < cursor->frame_low)
        cursor->frame_low = len;
}

static void oem_pop_frame(CettaOpenEquationCursor *cursor) {
    oem_truncate_frames(cursor, cursor->frame_len - 1u);
}


static bool oem_int_value(Atom *atom, int64_t *out) {
    if (atom->kind != ATOM_GROUNDED || atom->ground.gkind != GV_INT)
        return false;
    *out = atom->ground.ival;
    return true;
}

static bool oem_fit(__int128 value, int64_t *out) {
    if (value > (__int128)INT64_MAX || value < (__int128)INT64_MIN)
        return false;
    *out = (int64_t)value;
    return true;
}

/* Integer arithmetic exactly as PeTTa's builtins, or false (unbound or
 * non-integer operands, a result outside int64, a zero divisor): the region
 * hands the call back. */
static bool oem_prim(SymbolId op, int64_t a, int64_t b, int64_t *out) {
    if (op == g_builtin_syms.op_plus)
        return oem_fit((__int128)a + b, out);
    if (op == g_builtin_syms.op_minus)
        return oem_fit((__int128)a - b, out);
    if (op == g_builtin_syms.op_mul)
        return oem_fit((__int128)a * b, out);
    if (op == g_builtin_syms.petta_min) {
        *out = a < b ? a : b;
        return true;
    }
    if (op == g_builtin_syms.petta_max) {
        *out = a > b ? a : b;
        return true;
    }
    if (op == g_builtin_syms.op_mod) {
        if (b == 0)
            return false;
        if (b == -1) {
            *out = 0;
            return true;
        }
        int64_t rem = a % b;
        if (rem != 0 && ((rem < 0) != (b < 0)))
            rem += b;
        *out = rem;
        return true;
    }
    return false;
}

static bool oem_test(SymbolId op, int64_t a, int64_t b) {
    if (op == g_builtin_syms.op_lt)
        return a < b;
    if (op == g_builtin_syms.op_gt)
        return a > b;
    if (op == g_builtin_syms.op_le)
        return a <= b;
    if (op == g_builtin_syms.op_ge)
        return a >= b;
    return a == b;
}

/* ---- answers ------------------------------------------------------------- */

static bool oem_reserve_zeroed(void **items, uint32_t *cap, uint32_t needed,
                               size_t size) {
    uint32_t old = *cap;
    if (!oem_reserve(items, cap, needed, size))
        return false;
    if (*cap > old)
        memset((char *)*items + (size_t)old * size, 0,
               (size_t)(*cap - old) * size);
    return true;
}

/* Whether the cursor's own storage holds `atom`: either generation of the
 * region, or what lives as long as the cursor (`names`).  What it holds is
 * copied out of the cursor, which frees all of it when it closes. */
static inline bool oem_cursor_owns(const CettaOpenEquationCursor *cursor,
                                   const Atom *atom) {
    return atom->arena_id == cursor->region.identity ||
        (cursor->old_ready && atom->arena_id == cursor->old.identity) ||
        atom->arena_id == cursor->names.identity ||
        (cursor->collected_ready &&
         atom->arena_id == cursor->collected.identity);
}

/* The answer's variable for the unbound cell `index`: the caller's own
 * variable for a query cell, otherwise one per cell and answer, in
 * `arena`.  A collection's copy (`collect`) freshens query cells too, as
 * findall copies every variable of its template. */
static Atom *oem_answer_cell(CettaOpenEquationCursor *cursor, uint32_t index,
                             Arena *arena, bool collect) {
    if (!collect && index < cursor->query_var_count)
        return cursor->query_vars ? cursor->query_vars[index] : NULL;
    if (!oem_reserve_zeroed((void **)&cursor->answer_fresh,
                            &cursor->answer_fresh_cap, index + 1u,
                            sizeof(*cursor->answer_fresh)))
        return NULL;
    Atom *fresh = cursor->answer_fresh[index];
    if (fresh)
        return fresh;
    if (!oem_reserve((void **)&cursor->fresh_touched,
                     &cursor->fresh_touched_cap,
                     cursor->fresh_touched_len + 1u,
                     sizeof(*cursor->fresh_touched)))
        return NULL;
    fresh = atom_var_with_spelling(arena, cursor->program->spelling,
                                   fresh_var_id());
    if (!fresh)
        return NULL;
    cursor->answer_fresh[index] = fresh;
    cursor->fresh_touched[cursor->fresh_touched_len++] = index;
    return fresh;
}

/* The answer variables of one export: a cell keeps its variable within the
 * export, and no export reuses another's, which lives in the host's store
 * and may be gone by then. */
static void oem_forget_answer_vars(CettaOpenEquationCursor *cursor) {
    while (cursor->fresh_touched_len > 0u)
        cursor->answer_fresh[
            cursor->fresh_touched[--cursor->fresh_touched_len]] = NULL;
}


/* A region expression one export has copied into the host's answer arena
 * keeps this marker in `name_key` until it is exported again or the region's
 * next collection. */
static Atom oem_exported_mark;

/* A value with every bound cell replaced by its value.  For an answer
 * (`operand` false) it is built in the answer arena: the cursor's own atoms
 * are copied, atoms owned elsewhere are shared, and each unbound cell
 * becomes the answer's variable for it.  With `ground`, storage that
 * outlives the host's answer arena, a ground expression that has proven
 * long-lived, by surviving a collection into the old generation or by being
 * exported a second time, none of whose children is the answer arena's, is
 * copied there instead, once: the expression keeps its copy in `name_key`,
 * as a collection's forwarding does, so the next export shares it, and so
 * does the region's next collection.  An expression exported the first time
 * is copied into the answer arena, which the host reclaims after the goal,
 * and marked; the stable storage the host does not reclaim while the cursor
 * lives.  For a grounded operation's operand (`operand` true) it is built in
 * the region, sharing whatever holds no cell, and an unbound cell stays the
 * cell's own variable: the operation sees a variable, as it would in
 * equation search.  Iterative, so deep lists cost no C stack. */
static inline __attribute__((always_inline)) Atom *oem_resolve_in(
    CettaOpenEquationCursor *cursor, Atom *root, bool operand, Arena *arena,
    Arena *ground, bool collect) {
    OemAtomMap images;
    oem_atom_map_init(&images);
    Atom *answer = NULL;
    uint32_t depth = 0u;
    uint32_t results = 0u;
    Atom *atom = root;
    uint32_t old_identity =
        ground && cursor->old_ready ? cursor->old.identity : 0u;
    for (;;) {
        atom = oem_deref(cursor, atom);
        uint32_t index = 0u;
        Atom *result = NULL;
        OemAtomMapEntry *known = atom->kind == ATOM_EXPR
            ? oem_atom_map_find(&images, atom) : NULL;
        if (known) {
            result = known->image;
        } else if (oem_is_cell(cursor, atom, &index)) {
            result = operand ? atom
                : oem_answer_cell(cursor, index, arena, collect);
        } else if ((operand || !oem_cursor_owns(cursor, atom)) &&
                   (atom->kind != ATOM_EXPR || !atom_has_vars(atom))) {
            result = atom;
        } else if (ground && atom->kind == ATOM_EXPR && atom->name_key &&
                   atom->name_key != &oem_exported_mark &&
                   !atom_has_vars(atom)) {
            result = atom->name_key;
        } else if (atom->kind != ATOM_EXPR || atom->expr.len == 0u) {
            /* A leaf goes where the expression it sits in goes. */
            result = atom_deep_copy(
                depth > 0u && cursor->resolve_stack[depth - 1u].promote
                    ? ground : arena, atom);
        } else {
            if (!oem_atom_map_begin(&images, atom) ||
                atom->expr.len > UINT32_MAX - results ||
                !oem_reserve((void **)&cursor->resolve_stack,
                             &cursor->resolve_cap, depth + 1u,
                             sizeof(*cursor->resolve_stack)) ||
                !oem_reserve((void **)&cursor->resolve_results,
                             &cursor->resolve_result_cap,
                             results + (uint32_t)atom->expr.len,
                             sizeof(*cursor->resolve_results)))
                goto done;
            cursor->resolve_stack[depth++] = (OemResolveItem){
                .atom = atom, .next = 1u, .base = results,
                .promote = ground && !atom_has_vars(atom) &&
                    (atom->name_key == &oem_exported_mark ||
                     (old_identity != 0u && atom->arena_id == old_identity)),
            };
            atom = atom->expr.elems[0];
            continue;
        }
        /* Deliver the result to the innermost pending expression; a
         * completed expression is itself a result. */
        for (;;) {
            if (!result)
                goto done;
            if (depth == 0u) {
                answer = result;
                goto done;
            }
            OemResolveItem *item = &cursor->resolve_stack[depth - 1u];
            cursor->resolve_results[results++] = result;
            if (item->next < item->atom->expr.len) {
                atom = item->atom->expr.elems[item->next++];
                break;
            }
            /* A promoted expression goes to `ground` unless a child is the
             * host's answer arena's, which the stable storage outlives: it
             * stays in the answer arena, sharing that child, as before. */
            bool forwarded = item->promote;
            for (uint32_t child = item->base;
                 forwarded && child < results; child++) {
                forwarded = cursor->resolve_results[child]->arena_id !=
                    cursor->answer_arena->identity;
            }
            result = atom_expr(forwarded ? ground : arena,
                               &cursor->resolve_results[item->base],
                               item->atom->expr.len);
            if (result && forwarded)
                item->atom->name_key = result;
            else if (result && ground && !item->atom->name_key &&
                     !atom_has_vars(item->atom))
                item->atom->name_key = &oem_exported_mark;
            oem_atom_map_find(&images, item->atom)->image = result;
            results = item->base;
            depth--;
        }
    }
done:
    oem_atom_map_free(&images);
    return answer;
}

/* One copy of the resolver per use, each with its mode fixed. */
static Atom *oem_resolve_answer(CettaOpenEquationCursor *cursor, Atom *root) {
    return oem_resolve_in(cursor, root, false, cursor->answer_arena, NULL,
                          false);
}

static Atom *oem_resolve_operand(CettaOpenEquationCursor *cursor,
                                 Atom *root) {
    return oem_resolve_in(cursor, root, true, &cursor->region, NULL, false);
}

static Atom *oem_resolve_stable(CettaOpenEquationCursor *cursor, Atom *root) {
    return oem_resolve_in(cursor, root, false, cursor->answer_arena,
                          cursor->runtime.stable, false);
}

static Atom *oem_resolve_mode(CettaOpenEquationCursor *cursor, Atom *root,
                              bool operand) {
    return operand ? oem_resolve_operand(cursor, root)
                   : oem_resolve_answer(cursor, root);
}

static void oem_link_generations(CettaOpenEquationCursor *cursor);

/* A host goal's term, for the host: long-lived ground region structure
 * goes to the host's stable storage once and is shared from then on. */
static Atom *oem_export(CettaOpenEquationCursor *cursor, Atom *root) {
    Atom *exported = cursor->runtime.stable
        ? oem_resolve_stable(cursor, root)
        : oem_resolve_answer(cursor, root);
    oem_link_generations(cursor);
    return exported;
}

static Atom *oem_resolve(CettaOpenEquationCursor *cursor, Atom *root) {
    return oem_resolve_mode(cursor, root, false);
}

/* A raised error, as an answer-arena value: the branch ends, and the
 * cursor's alternatives remain. */
static OemRun oem_publish_raise(CettaOpenEquationCursor *cursor, Atom *error,
                                Atom **value_out) {
    oem_forget_answer_vars(cursor);
    Atom *raised = oem_resolve(cursor, error);
    oem_forget_answer_vars(cursor);
    if (!raised)
        return OEM_RUN_HANDOFF;
    *value_out = raised;
    return OEM_RUN_RAISE;
}

static OemRun oem_publish(CettaOpenEquationCursor *cursor, Atom *value,
                          Atom **value_out) {
    oem_forget_answer_vars(cursor);
    Atom *answer = oem_resolve(cursor, value);
    for (uint32_t index = 0u;
         answer && index < cursor->query_var_count; index++) {
        cursor->query_values[index] =
            oem_resolve(cursor, cursor->query_cells[index]);
        if (!cursor->query_values[index])
            answer = NULL;
    }
    oem_forget_answer_vars(cursor);
    if (!answer)
        return OEM_RUN_HANDOFF;
    *value_out = answer;
    cursor->stats.answers++;
    return OEM_RUN_ANSWER;
}

/* ---- running ------------------------------------------------------------- */

/* The current version's index of `relation` of an earlier version. */
/* Whether code of `program` may enter its own relations: nothing it was
 * compiled against changed since it was entered. */
static bool oem_version_current(const CettaOpenEquationCursor *cursor,
                                const CettaOpenEquationProgram *program) {
    if (!space_program_token_is_current(program->token))
        return false;
    if (cursor->authority_epoch == 0u)
        return true;
    for (uint32_t index = 0u; index < cursor->version_len; index++) {
        if (cursor->versions[index] == program)
            return cursor->version_epochs[index] == cursor->authority_epoch;
    }
    return false;
}

/* The current program entered at (head, arity), once per change: a
 * version already obtained under the current authorities, or the host's. */
static const CettaOpenEquationProgram *oem_enter_current(
    CettaOpenEquationCursor *cursor, Space *space, SymbolId head,
    uint32_t arity) {
    for (uint32_t index = 0u; index < cursor->version_len; index++) {
        const CettaOpenEquationProgram *version = cursor->versions[index];
        if (cursor->version_epochs[index] == cursor->authority_epoch &&
            space_program_token_is_current(version->token) &&
            version->relations[0].head == head &&
            version->relations[0].arity == arity)
            return version;
    }
    if (!cursor->runtime.current)
        return NULL;
    /* The host's program comes retained: the cursor keeps that reference,
     * or releases it. */
    CettaOpenEquationProgram *program = cursor->runtime.current(
        cursor->runtime.context, space, head, arity);
    if (!program)
        return NULL;
    if (program->space != space || program->relation_len == 0u ||
        program->relations[0].head != head ||
        program->relations[0].arity != arity ||
        !space_program_token_is_current(program->token) ||
        !oem_reserve((void **)&cursor->versions, &cursor->version_cap,
                     cursor->version_len + 1u, sizeof(*cursor->versions))) {
        cetta_open_equation_program_release(program);
        return NULL;
    }
    uint32_t epochs = cursor->version_cap;
    uint64_t *grown = realloc(cursor->version_epochs,
                              sizeof(*grown) * (epochs ? epochs : 1u));
    if (!grown) {
        cetta_open_equation_program_release(program);
        return NULL;
    }
    cursor->version_epochs = grown;
    cursor->versions[cursor->version_len] = program;
    cursor->version_epochs[cursor->version_len++] = cursor->authority_epoch;
    return program;
}

/* The host's spelling of a truth value. */
static Atom *oem_truth(CettaOpenEquationCursor *cursor, bool truth) {
    Atom **cached = &cursor->truths[truth ? 1 : 0];
    if (!*cached)
        *cached = cursor->runtime.boolean_value
            ? cursor->runtime.boolean_value(cursor->runtime.context,
                                            &cursor->names, truth)
            : petta_semantics_boolean_value(&cursor->names, truth);
    return *cached;
}

/* An operation over plain scalars (machine integers, floats and truth
 * values) by the grounded operations' allocation-free evaluator, the one
 * the search machine's scalar regions use: true with the exact result,
 * false where the grounded dispatcher decides (another operation, an
 * overflow, a dialect's own spelling). */
static bool oem_scalar_operation(Atom *head, Atom *const *args,
                                 uint32_t count, CettaPlainScalar *out) {
    CettaPlainScalar operands[2];
    if (count == 0u || count > 2u)
        return false;
    for (uint32_t index = 0u; index < count; index++) {
        if (!grounded_plain_scalar_from_atom(args[index], &operands[index]))
            return false;
    }
    return grounded_try_plain_scalar_operation(head, operands, count, out);
}

/* A test or primitive the integer path does not cover (a float, a bigint,
 * a symbol, structure or variable under `==`, an int64 overflow, a zero
 * divisor, an unbound operand) is the shared grounded operation on the
 * resolved operands, with the machine's reading of its result: a raised
 * numeric error, empty for failure, a truth value for a test, a value for
 * a primitive. */
static OemRun oem_host_arithmetic(CettaOpenEquationCursor *cursor,
                                  const CettaOpenEquationProgram *program,
                                  const OemStep *step, Atom *left,
                                  Atom *right, Atom **result_out,
                                  bool *truth_out, Atom **value_out) {
    Atom *args[2] = {
        oem_resolve_mode(cursor, left, true),
        oem_resolve_mode(cursor, right, true),
    };
    if (!args[0] || !args[1]) {
        cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_UNSUPPORTED;
        return OEM_RUN_HANDOFF;
    }
    args[0] = oem_grounded_operand(cursor, args[0]);
    args[1] = oem_grounded_operand(cursor, args[1]);
    if (!args[0] || !args[1]) {
        cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_UNSUPPORTED;
        return OEM_RUN_HANDOFF;
    }
    Atom *head = program->templates[step->value].literal;
    CettaPlainScalar scalar;
    if (oem_scalar_operation(head, args, 2u, &scalar)) {
        if (step->kind == OEM_S_TEST) {
            if (scalar.kind != CETTA_PLAIN_SCALAR_BOOL) {
                cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_UNSUPPORTED;
                return OEM_RUN_HANDOFF;
            }
            *truth_out = scalar.as.boolean;
            return OEM_RUN_CALLED;
        }
        *result_out = scalar.kind == CETTA_PLAIN_SCALAR_BOOL
            ? oem_truth(cursor, scalar.as.boolean)
            : grounded_plain_scalar_materialize(&cursor->region, &scalar);
        if (!*result_out) {
            cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_UNSUPPORTED;
            return OEM_RUN_HANDOFF;
        }
        return OEM_RUN_CALLED;
    }
    Atom *result = grounded_dispatch(&cursor->region, head, args, 2u);
    if (!result) {
        cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_UNSUPPORTED;
        return OEM_RUN_HANDOFF;
    }
    if (grounded_result_is_raised_numeric_error(head, 2u, result))
        return oem_publish_raise(cursor, result, value_out);
    if (atom_is_petta_no_result(result))
        return OEM_RUN_FAILED;
    if (step->kind == OEM_S_TEST) {
        if (!petta_semantics_truth_value(result, truth_out)) {
            cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_UNSUPPORTED;
            return OEM_RUN_HANDOFF;
        }
    }
    *result_out = result;
    return OEM_RUN_CALLED;
}

/* Where an activation's body continues: its code, its slots and its
 * caller. */
typedef struct {
    const CettaOpenEquationProgram *program;
    uint32_t pc;
    Atom **locals;
    uint32_t local_count;
    const OemCont *cont;
    uint32_t depth;
} OemEntry;

/* Whether the named space `reference` declares a `SpaceOf` schema, which
 * elaborates a match's pattern in equation search.  Declarations are host
 * authorities, so an answer holds until the authority epoch moves. */
static bool oem_space_has_schema(CettaOpenEquationCursor *cursor, Space *root,
                                 Atom *reference) {
    if (cursor->schema_epoch != cursor->authority_epoch + 1u) {
        cursor->schema_len = 0u;
        cursor->schema_epoch = cursor->authority_epoch + 1u;
    }
    for (uint32_t index = 0u; index < cursor->schema_len; index++) {
        if (cursor->schemas[index].name == reference->sym_id)
            return cursor->schemas[index].schema;
    }
    Atom **types = NULL;
    uint32_t count = space_get_declared_types(root, &cursor->region,
                                              reference, &types);
    bool found = false;
    for (uint32_t index = 0u; index < count && !found; index++) {
        Atom *type = types[index];
        const char *head = type && type->kind == ATOM_EXPR &&
                type->expr.len == 2u &&
                type->expr.elems[0]->kind == ATOM_SYMBOL
            ? symbol_bytes(g_symbols, type->expr.elems[0]->sym_id) : NULL;
        found = head && strcmp(head, "SpaceOf") == 0;
    }
    free(types);
    if (oem_reserve((void **)&cursor->schemas, &cursor->schema_cap,
                    cursor->schema_len + 1u, sizeof(*cursor->schemas)))
        cursor->schemas[cursor->schema_len++] =
            (OemSchemaEntry){.name = reference->sym_id, .schema = found};
    return found;
}

/* Whether a row may unify with a match's resolved pattern: false only when
 * a position both terms fix differs.  A variable on either side, or a depth
 * past the check, is left to unification, so a row that unifies is never
 * dropped. */
static bool oem_row_may_unify(const CettaOpenEquationCursor *cursor,
                              Atom *pattern, Atom *row, uint32_t depth) {
    pattern = oem_deref(cursor, pattern);
    if (pattern->kind == ATOM_VAR || row->kind == ATOM_VAR ||
        oem_is_list_cell(pattern) || oem_is_list_cell(row))
        return true;
    if (pattern->kind != row->kind)
        return false;
    if (pattern->kind != ATOM_EXPR)
        return atom_eq(pattern, row);
    if (pattern->expr.len != row->expr.len)
        return false;
    if (depth >= 4u)
        return true;
    for (CettaExprIndex child = 0u; child < pattern->expr.len; child++) {
        Atom *part = oem_deref(cursor, pattern->expr.elems[child]);
        Atom *row_part = row->expr.elems[child];
        /* A symbol is compared at once. */
        if (part->kind == ATOM_SYMBOL
                ? !(row_part->kind == ATOM_SYMBOL
                        ? row_part->sym_id == part->sym_id
                        : row_part->kind == ATOM_VAR ||
                              oem_is_list_cell(row_part))
                : !oem_row_may_unify(cursor, part, row_part, depth + 1u))
            return false;
    }
    return true;
}

/* A term's root key, the first test a match's rows meet: an expression
 * headed by a symbol is its length and head, one headed otherwise its
 * length alone (OEM_ROW_ANY_HEAD).  A row whose root is a variable or a
 * list cell may meet any pattern (OEM_ROW_WILD), and any other atom meets
 * no expression (OEM_ROW_ATOMIC). */
#define OEM_ROW_WILD UINT64_MAX
#define OEM_ROW_ATOMIC (UINT64_MAX - 1u)
#define OEM_ROW_ANY_HEAD ((uint64_t)UINT32_MAX)

static uint64_t oem_row_key(const Atom *row) {
    if (row->kind == ATOM_VAR || oem_is_list_cell(row))
        return OEM_ROW_WILD;
    if (row->kind != ATOM_EXPR)
        return OEM_ROW_ATOMIC;
    uint64_t length = (uint64_t)row->expr.len << 32;
    return row->expr.len > 0u && row->expr.elems[0]->kind == ATOM_SYMBOL
        ? length | row->expr.elems[0]->sym_id
        : length | OEM_ROW_ANY_HEAD;
}

/* A pattern's root key, or OEM_ROW_WILD when its root is not an expression
 * headed by a symbol: then every row stays a candidate. */
static uint64_t oem_pattern_key(const CettaOpenEquationCursor *cursor,
                                Atom *pattern) {
    pattern = oem_deref(cursor, pattern);
    if (pattern->kind != ATOM_EXPR || pattern->expr.len == 0u ||
        oem_is_list_cell(pattern))
        return OEM_ROW_WILD;
    Atom *head = oem_deref(cursor, pattern->expr.elems[0]);
    return head->kind == ATOM_SYMBOL
        ? ((uint64_t)pattern->expr.len << 32) | head->sym_id
        : OEM_ROW_WILD;
}

/* Whether a row with root key `row` may meet a pattern with root key
 * `pattern`. */
static inline bool oem_keys_may_meet(uint64_t pattern, uint64_t row) {
    return pattern == OEM_ROW_WILD || row == OEM_ROW_WILD || row == pattern ||
        row == (pattern | OEM_ROW_ANY_HEAD);
}

/* The key of a part below the root, the second test a match's rows meet: a
 * symbol is itself, a grounded value is only a value, and an expression is
 * its root key.  A variable or a list cell meets any part (OEM_ROW_WILD).
 * The parts after an expression's head are keyed, the first
 * OEM_ROW_PARTS of them. */
#define OEM_PART_SYMBOL (UINT64_C(1) << 63)
#define OEM_PART_VALUE (UINT64_C(1) << 62)
enum { OEM_ROW_PARTS = 4u };

static uint64_t oem_part_key(const Atom *part) {
    if (part->kind == ATOM_SYMBOL)
        return OEM_PART_SYMBOL | part->sym_id;
    if (part->kind == ATOM_GROUNDED)
        return OEM_PART_VALUE;
    if (part->kind != ATOM_EXPR || oem_is_list_cell(part))
        return OEM_ROW_WILD;
    uint64_t length = (uint64_t)part->expr.len << 32;
    return part->expr.len > 0u && part->expr.elems[0]->kind == ATOM_SYMBOL
        ? length | part->expr.elems[0]->sym_id
        : length | OEM_ROW_ANY_HEAD;
}

/* Whether parts with keys `pattern` and `row` may meet: equal keys, a
 * wildcard, or expressions of one length whose heads one of them leaves
 * open. */
static inline bool oem_parts_may_meet(uint64_t pattern, uint64_t row) {
    if (pattern == row || pattern == OEM_ROW_WILD || row == OEM_ROW_WILD)
        return true;
    if ((pattern | row) & (OEM_PART_SYMBOL | OEM_PART_VALUE))
        return false;
    return (pattern >> 32) == (row >> 32) &&
        ((uint32_t)pattern == (uint32_t)OEM_ROW_ANY_HEAD ||
         (uint32_t)row == (uint32_t)OEM_ROW_ANY_HEAD);
}

/* A space small enough that its index offers every row to any pattern
 * (native_candidates): its rows are read once per revision, and a match
 * tests their root keys before the rows themselves. */
static bool oem_space_small(const Space *space) {
    return space->native.len <= MATCH_TRIE_THRESHOLD;
}

static bool oem_small_space_rows(CettaOpenEquationCursor *cursor,
                                 Space *space) {
    if (space_read_token_matches_live_space(cursor->rows_read, space))
        return true;
    /* A root variable asks the index for every row. */
    if (!cursor->every_row)
        cursor->every_row = atom_var(&cursor->names, "row");
    if (!cursor->every_row)
        return false;
    CettaIndex *indices = NULL;
    CettaIndex count = space_match_candidates64(space, cursor->every_row,
                                                &indices);
    bool ok = count <= (CettaIndex)MATCH_TRIE_THRESHOLD &&
        oem_reserve((void **)&cursor->space_rows, &cursor->space_row_cap,
                    (uint32_t)count, sizeof(*cursor->space_rows));
    if (ok) {
        uint32_t room = cursor->space_row_cap ? cursor->space_row_cap : 1u;
        cursor->space_row_len = 0u;
        free(cursor->space_row_keys);
        free(cursor->space_row_parts);
        cursor->space_row_keys = malloc(sizeof(uint64_t) * room);
        cursor->space_row_parts =
            malloc(sizeof(uint64_t) * room * OEM_ROW_PARTS);
        ok = cursor->space_row_keys && cursor->space_row_parts;
    }
    for (CettaIndex index = 0u; ok && index < count; index++) {
        Atom *row = space_match_candidate_at64(space, indices[index]);
        if (!row)
            continue;
        uint32_t at = cursor->space_row_len++;
        cursor->space_rows[at] = row;
        cursor->space_row_keys[at] = oem_row_key(row);
        uint64_t *parts = &cursor->space_row_parts[at * OEM_ROW_PARTS];
        for (uint32_t part = 0u; part < OEM_ROW_PARTS; part++)
            parts[part] = row->kind == ATOM_EXPR && !oem_is_list_cell(row) &&
                    part + 1u < row->expr.len
                ? oem_part_key(row->expr.elems[part + 1u])
                : OEM_ROW_WILD;
    }
    free(indices);
    if (!ok) {
        cursor->rows_read = (SpaceReadToken){0};
        return false;
    }
    cursor->rows_read = space_read_token(space);
    return true;
}

/* A match's pattern read in place, as oem_instantiate would build it over
 * `locals`: its root key, whether a row may meet it (oem_row_may_unify),
 * and whether the host reads it (an open cons value at its root). */
static uint64_t oem_template_key(const CettaOpenEquationCursor *cursor,
                                 const CettaOpenEquationProgram *program,
                                 Atom **locals, uint32_t template_index) {
    const OemTemplate *item = &program->templates[template_index];
    if (item->kind == OEM_T_LITERAL)
        return oem_pattern_key(cursor, item->literal);
    if (item->kind == OEM_T_SLOT)
        return oem_pattern_key(cursor, locals[item->slot]);
    if (item->kind != OEM_T_BUILD || item->count == 0u)
        return OEM_ROW_WILD;
    const OemTemplate *head =
        &program->templates[program->template_children[item->first]];
    Atom *symbol = head->kind == OEM_T_LITERAL ? head->literal
        : head->kind == OEM_T_SLOT ? oem_deref(cursor, locals[head->slot])
                                   : NULL;
    return symbol && symbol->kind == ATOM_SYMBOL
        ? ((uint64_t)item->count << 32) | symbol->sym_id
        : OEM_ROW_WILD;
}

/* A pattern leaf, a literal or a slot's value, against a row: a symbol is
 * compared at once, anything else as oem_row_may_unify compares it. */
static inline bool oem_leaf_may_unify(const CettaOpenEquationCursor *cursor,
                                      Atom *value, Atom *row,
                                      uint32_t depth) {
    value = oem_deref(cursor, value);
    if (value->kind == ATOM_SYMBOL)
        return row->kind == ATOM_SYMBOL ? row->sym_id == value->sym_id
                                        : row->kind == ATOM_VAR ||
                                              oem_is_list_cell(row);
    return oem_row_may_unify(cursor, value, row, depth);
}

/* The key of a pattern's part read in place: a literal's or a slot's
 * value's key (oem_part_key), or a built expression's root key. */
static uint64_t oem_template_part_key(const CettaOpenEquationCursor *cursor,
                                      const CettaOpenEquationProgram *program,
                                      Atom **locals, uint32_t template_index) {
    const OemTemplate *item = &program->templates[template_index];
    if (item->kind == OEM_T_LITERAL)
        return oem_part_key(item->literal);
    if (item->kind == OEM_T_SLOT)
        return oem_part_key(oem_deref(cursor, locals[item->slot]));
    if (item->kind != OEM_T_BUILD)
        return OEM_ROW_WILD;
    uint64_t length = (uint64_t)item->count << 32;
    if (item->count == 0u)
        return length | OEM_ROW_ANY_HEAD;
    const OemTemplate *head =
        &program->templates[program->template_children[item->first]];
    Atom *symbol = head->kind == OEM_T_LITERAL ? head->literal
        : head->kind == OEM_T_SLOT ? oem_deref(cursor, locals[head->slot])
                                   : NULL;
    return symbol && symbol->kind == ATOM_SYMBOL
        ? length | symbol->sym_id : length | OEM_ROW_ANY_HEAD;
}

/* Whether a row may meet a pattern read in place, as oem_row_may_unify asks
 * of a built pattern, to the same depth: the template's expressions are
 * walked beside the row's with a stack, and its leaves compared at once. */
static bool oem_template_may_unify(const CettaOpenEquationCursor *cursor,
                                   const CettaOpenEquationProgram *program,
                                   Atom **locals, uint32_t template_index,
                                   Atom *row) {
    enum { OEM_MAY_STACK = 32u };
    uint32_t items[OEM_MAY_STACK];
    Atom *parts[OEM_MAY_STACK];
    uint8_t depths[OEM_MAY_STACK];
    uint32_t top = 0u;
    const OemTemplate *root = &program->templates[template_index];
    if (root->kind == OEM_T_LITERAL)
        return oem_leaf_may_unify(cursor, root->literal, row, 0u);
    if (root->kind == OEM_T_SLOT)
        return oem_leaf_may_unify(cursor, locals[root->slot], row, 0u);
    items[top] = template_index;
    parts[top] = row;
    depths[top++] = 0u;
    while (top > 0u) {
        top--;
        const OemTemplate *item = &program->templates[items[top]];
        Atom *part = parts[top];
        uint32_t depth = depths[top];
        if (item->kind != OEM_T_BUILD || part->kind == ATOM_VAR ||
            oem_is_list_cell(part))
            continue;
        if (part->kind != ATOM_EXPR || part->expr.len != item->count)
            return false;
        if (depth >= 4u)
            continue;
        for (uint32_t index = 0u; index < item->count; index++) {
            uint32_t child_index =
                program->template_children[item->first + index];
            const OemTemplate *child = &program->templates[child_index];
            Atom *child_part = part->expr.elems[index];
            if (child->kind == OEM_T_LITERAL) {
                if (!oem_leaf_may_unify(cursor, child->literal, child_part,
                                        depth + 1u))
                    return false;
            } else if (child->kind == OEM_T_SLOT) {
                if (!oem_leaf_may_unify(cursor, locals[child->slot],
                                        child_part, depth + 1u))
                    return false;
            } else if (child->kind == OEM_T_BUILD &&
                       top < OEM_MAY_STACK) {
                /* A child past the stack is left to unification. */
                items[top] = child_index;
                parts[top] = child_part;
                depths[top++] = (uint8_t)(depth + 1u);
            }
        }
    }
    return true;
}

static OemRun oem_try_candidate(CettaOpenEquationCursor *cursor,
                                Atom *pattern, Atom *candidate);

/* The rows of a match that unify with `pattern`, counted: by the space's
 * flat count where it admits the pattern (as the host's count fold counts),
 * else by unifying a fresh copy of each row the index offers and undoing
 * it.  The count is taken now, under the logical-update view. */
static OemRun oem_match_count(CettaOpenEquationCursor *cursor, Space *space,
                              Atom *pattern, Atom *query, bool exact_query,
                              uint64_t *count_out) {
    uint64_t count = 0u;
    CettaIndex examined = 0u;
    if (exact_query && space_match_count_flat_linear64(space, &cursor->region, query,
                                        &count, &examined)) {
        cursor->stats.match_candidates += (uint64_t)examined;
        *count_out = count;
        return OEM_RUN_CALLED;
    }
    CettaIndex *indices = NULL;
    CettaIndex offered = space_match_candidates64(space, query, &indices);
    OemFrame mark = {
        .mark = arena_mark(&cursor->region),
        .cell_mark = cursor->cell_len,
        .trail_mark = cursor->trail_len,
        .store_mark = cursor->store_len,
    };
    OemRun run = OEM_RUN_CALLED;
    for (CettaIndex index = 0u; index < offered; index++) {
        Atom *candidate = space_match_candidate_at64(space, indices[index]);
        if (!candidate || !oem_row_may_unify(cursor, query, candidate, 0u))
            continue;
        OemRun tried = oem_try_candidate(cursor, pattern, candidate);
        oem_restore(cursor, &mark);
        if (tried == OEM_RUN_CALLED) {
            count++;
        } else if (tried != OEM_RUN_FAILED) {
            run = tried;
            break;
        }
    }
    free(indices);
    *count_out = count;
    return run;
}

static OemRun oem_match_host_step(
    CettaOpenEquationCursor *cursor, const CettaOpenEquationProgram *program,
    uint32_t pc, Atom **locals, uint32_t local_count, const OemCont *cont,
    uint32_t depth, const OemStep *step, Atom *reference);

/* A match: the rows the space's index offers for the pattern, snapshotted
 * now, so an answer's effects neither add to nor remove from this match's
 * alternatives (the logical-update view).  The index may offer rows that do
 * not unify; unification decides.  A reference that names no space has no
 * rows, as in equation search; a space the region cannot read exactly is
 * equation search's.  When only the number of answers is observed and the
 * match ends the call's own answer (the activation has no caller, and the
 * data template leaves nothing to run but the return), the rows are one
 * answer weighted by their count. */
static __attribute__((noinline)) OemRun oem_match_step(
                             CettaOpenEquationCursor *cursor,
                             const CettaOpenEquationProgram *program,
                             uint32_t pc, Atom **locals, uint32_t local_count,
                             const OemCont *cont, uint32_t depth,
                             const OemStep *step) {
    Atom *reference = oem_instantiate(cursor, program, locals, step->value);
    if (!reference)
        return OEM_RUN_HANDOFF;
    reference = oem_deref(cursor, reference);
    /* A pattern supplied through a slot can acquire a connective at run
     * time. Its match is a composition of matches, not a row lookup. */
    const OemTemplate *pattern_shape = &program->templates[step->pattern];
    Atom *head = NULL;
    if (pattern_shape->kind == OEM_T_BUILD) {
        if (pattern_shape->count)
            head = oem_instantiate(cursor, program, locals,
                program->template_children[pattern_shape->first]);
    } else {
        Atom *root = oem_instantiate(cursor, program, locals, step->pattern);
        root = root ? oem_deref(cursor, root) : NULL;
        if (root && root->kind == ATOM_EXPR && root->expr.len)
            head = petta_semantics_is_open_cons_value(root)
                ? root->expr.elems[1] : root->expr.elems[0];
    }
    head = head ? oem_deref(cursor, head) : NULL;
    if (!step->match_row &&
        (atom_is_symbol_id(head, g_builtin_syms.comma) ||
         atom_is_symbol_id(head, g_builtin_syms.pipe) ||
         (head && head->kind == ATOM_SYMBOL &&
          strcmp(symbol_bytes(g_symbols, head->sym_id), "cons") == 0)))
        return oem_match_host_step(cursor, program, pc, locals,
                                    local_count, cont, depth, step, reference);
    Space *space = cursor->runtime.resolve_space
        ? cursor->runtime.resolve_space(cursor->runtime.context,
                                        program->space, &cursor->region,
                                        reference)
        : NULL;
    if (reference->kind == ATOM_SYMBOL) {
        if (!space && reference->sym_id == g_builtin_syms.self)
            space = program->space;
        /* A symbol that names no space names a predicate; a schema
         * elaborates the pattern.  Both are equation search's. */
        if (!space ||
            oem_space_has_schema(cursor, program->space, reference)) {
            return oem_match_host_step(cursor, program, pc, locals,
                                        local_count, cont, depth, step, reference);
        }
    }
    if (!space)
        return OEM_RUN_FAILED;
    if (space->overlay_base ||
        (space->match_backend.kind != SPACE_ENGINE_NATIVE &&
         space->match_backend.kind != SPACE_ENGINE_NATIVE_CANDIDATE_EXACT)) {
        return oem_match_host_step(cursor, program, pc, locals,
                                    local_count, cont, depth, step, reference);
    }
    bool counted_only = cursor->runtime.count_only && !cont &&
        program->steps[pc + 1u].kind == OEM_S_RET;
    /* The pattern, built only where it is read as a term: for the index's
     * query, the count, and the frame of a match over a large space. */
    Atom *pattern = NULL;
    OemRows *rows = NULL;
    uint32_t len = 0u;
    if (!counted_only && oem_space_small(space)) {
        /* The pattern is read in place: no query is built for the index,
         * which would offer every row, and the frame keeps none. */
        if (!oem_small_space_rows(cursor, space))
            return OEM_RUN_HANDOFF;
        uint32_t count = cursor->space_row_len;
        uint64_t key = oem_template_key(cursor, program, locals,
                                        step->pattern);
        /* The pattern's parts after its head, keyed as the rows' are. */
        uint64_t parts[OEM_ROW_PARTS];
        const OemTemplate *shape = &program->templates[step->pattern];
        for (uint32_t part = 0u; part < OEM_ROW_PARTS; part++)
            parts[part] = shape->kind == OEM_T_BUILD &&
                    part + 1u < shape->count
                ? oem_template_part_key(
                      cursor, program, locals,
                      program->template_children[shape->first + part + 1u])
                : OEM_ROW_WILD;
        rows = count
            ? arena_alloc(&cursor->region,
                          sizeof(*rows) + sizeof(Atom *) * (size_t)count)
            : NULL;
        if (count && !rows)
            return OEM_RUN_HANDOFF;
        for (uint32_t index = 0u; index < count; index++) {
            Atom *candidate = cursor->space_rows[index];
            const uint64_t *row_parts =
                &cursor->space_row_parts[index * OEM_ROW_PARTS];
            if (oem_keys_may_meet(key, cursor->space_row_keys[index]) &&
                oem_parts_may_meet(parts[0], row_parts[0]) &&
                oem_parts_may_meet(parts[1], row_parts[1]) &&
                oem_parts_may_meet(parts[2], row_parts[2]) &&
                oem_parts_may_meet(parts[3], row_parts[3]) &&
                oem_template_may_unify(cursor, program, locals,
                                       step->pattern, candidate))
                rows->rows[len++] = candidate;
        }
    } else {
        pattern = oem_instantiate(cursor, program, locals, step->pattern);
        if (!pattern)
            return OEM_RUN_HANDOFF;
        Atom *query = oem_resolve_mode(cursor, pattern, true);
        if (!query)
            return OEM_RUN_HANDOFF;
        bool exact_query = false;
        query = petta_semantics_match_index_pattern(
            &cursor->region, query, &exact_query);
        if (!query)
            return OEM_RUN_HANDOFF;
        if (counted_only) {
            uint64_t counted = 0u;
            OemRun run = oem_match_count(cursor, space, pattern, query, exact_query,
                                         &counted);
            if (run != OEM_RUN_CALLED)
                return run;
            if (counted == 0u)
                return OEM_RUN_FAILED;
            cursor->answer_weight = counted;
            cursor->answer_folded = true;
            return OEM_RUN_FOLDED;
        }
        CettaIndex *indices = NULL;
        CettaIndex count = space_match_candidates64(space, query, &indices);
        if (count > (CettaIndex)UINT32_MAX) {
            free(indices);
            cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_CAPACITY;
            return OEM_RUN_HANDOFF;
        }
        rows = count
            ? arena_alloc(&cursor->region,
                          sizeof(*rows) + sizeof(Atom *) * (size_t)count)
            : NULL;
        for (CettaIndex index = 0u; rows && index < count; index++) {
            Atom *candidate =
                space_match_candidate_at64(space, indices[index]);
            if (candidate && oem_row_may_unify(cursor, query, candidate, 0u))
                rows->rows[len++] = candidate;
        }
        free(indices);
        if (count && !rows)
            return OEM_RUN_HANDOFF;
    }
    if (len == 0u)
        return OEM_RUN_FAILED;
    rows->len = len;
    rows->arena = cursor->region.identity;
    OemCont *resume = arena_alloc(&cursor->region, sizeof(*resume));
    /* A frame over a small space keeps no pattern: each row reads it in
     * place (oem_try_candidate_in_place). */
    Atom **args = pattern ? arena_alloc(&cursor->region, sizeof(*args))
                          : NULL;
    if (!resume || (pattern && !args) ||
        !oem_push_frame(cursor, program, OEM_MATCH_FRAME, args, resume,
                        depth))
        return OEM_RUN_HANDOFF;
    *resume = (OemCont){
        .parent = cont, .program = program, .locals = locals,
        .local_count = local_count, .pc = pc + 1u, .depth = depth,
    };
    if (args)
        args[0] = pattern;
    cursor->frames[cursor->frame_len - 1u].rows = rows;
    return OEM_RUN_CALLED;
}

/* The distinct unbound cells of a region term, added to `cells`. */
static bool oem_unbound_cells(CettaOpenEquationCursor *cursor, Atom *root,
                              uint32_t **cells, uint32_t *len,
                              uint32_t *cap) {
    uint32_t walk = 0u;
    if (!oem_reserve((void **)&cursor->walk, &cursor->walk_cap, 1u,
                     sizeof(*cursor->walk)))
        return false;
    cursor->walk[walk++] = root;
    while (walk > 0u) {
        Atom *atom = oem_deref(cursor, cursor->walk[--walk]);
        uint32_t cell = 0u;
        if (oem_is_cell(cursor, atom, &cell)) {
            bool seen = false;
            for (uint32_t index = 0u; !seen && index < *len; index++)
                seen = (*cells)[index] == cell;
            if (seen)
                continue;
            if (!oem_reserve((void **)cells, cap, *len + 1u, sizeof(**cells)))
                return false;
            (*cells)[(*len)++] = cell;
            continue;
        }
        if (atom->kind != ATOM_EXPR || !atom_has_vars(atom))
            continue;
        if (!oem_reserve((void **)&cursor->walk, &cursor->walk_cap,
                         walk + atom->expr.len, sizeof(*cursor->walk)))
            return false;
        for (CettaExprIndex child = atom->expr.len; child-- > 0u;)
            cursor->walk[walk++] = atom->expr.elems[child];
    }
    return true;
}

/* Whether code returning through `cont` runs inside the body of a call
 * dispatched at run time, whose handler turns an error raised there into
 * the failure of that path.  Nested handlers are one handler
 * (DispatchErrorScope.recover_recover), so any such return decides.  The
 * returns are walked only where the host's policy recovers and the cursor
 * has made such a call, and only for a raise or a host goal. */
static __attribute__((noinline, cold)) bool oem_returns_recover(
    const OemCont *cont) {
    for (; cont; cont = cont->parent) {
        if (cont->recovers)
            return true;
    }
    return false;
}

static inline bool oem_recovering(const CettaOpenEquationCursor *cursor,
                                  const OemCont *cont) {
    return cursor->recovering && cursor->runtime.dispatch_recovers &&
        oem_returns_recover(cont);
}

/* An error raised inside a run-time dispatch fails that path alone, and
 * the other alternatives remain (DispatchErrorScope.recover_add); outside
 * one it ends the branch as a raise. */
static inline OemRun oem_raised(const CettaOpenEquationCursor *cursor,
                                const OemCont *cont, OemRun run) {
    return run == OEM_RUN_RAISE && oem_recovering(cursor, cont)
        ? OEM_RUN_FAILED : run;
}

/* CETTA_OPEN_EQUATIONS_DEBUG=host names each goal the region leaves to the
 * host, by its head and arity. */
static bool oem_debug_host_goals(void) {
    static int enabled = -1;
    if (enabled < 0) {
        const char *setting = getenv("CETTA_OPEN_EQUATIONS_DEBUG");
        enabled = setting && strcmp(setting, "host") == 0;
    }
    return enabled == 1;
}

static __attribute__((cold, noinline)) void oem_debug_host_goal(
    const Atom *goal) {
    bool expr = goal->kind == ATOM_EXPR && goal->expr.len > 0u;
    const Atom *head = expr ? goal->expr.elems[0] : goal;
    fprintf(stderr, "open-equations host %s/%u\n",
            head->kind == ATOM_SYMBOL ? symbol_bytes(g_symbols, head->sym_id)
            : head->kind == ATOM_VAR  ? "<variable>"
            : head->kind == ATOM_EXPR ? "<expression>"
                                      : "<value>",
            expr ? (unsigned)goal->expr.len - 1u : 0u);
}

/* A HOST step: the goal and its destination leave the region with one
 * variable per unbound cell they mention (a call variable stands for
 * itself); the frame keeps those cells, and the host passes the variables
 * back with each answer, so the cursor holds no pointer into the host's
 * store. */
static __attribute__((noinline)) OemRun oem_host_goal(
                            CettaOpenEquationCursor *cursor,
                            const CettaOpenEquationProgram *program,
                            uint32_t pc, Atom **locals, uint32_t local_count,
                            const OemCont *cont, uint32_t depth,
                            Atom *goal, Atom *destination,
                            const PettaPlanNode *plan,
                            CettaOpenEquationHostMode mode) {
    oem_forget_answer_vars(cursor);
    if (!goal || !destination)
        return OEM_RUN_HANDOFF;
    if (oem_debug_host_goals())
        oem_debug_host_goal(oem_deref(cursor, goal));
    uint32_t *cells = NULL;
    uint32_t count = 0u;
    uint32_t cap = 0u;
    bool ok = oem_unbound_cells(cursor, goal, &cells, &count, &cap) &&
        oem_unbound_cells(cursor, destination, &cells, &count, &cap);
    OemHostCells *kept = ok
        ? arena_alloc(&cursor->region,
                      sizeof(*kept) + sizeof(uint32_t) * (count ? count : 1u))
        : NULL;
    ok = kept && oem_reserve((void **)&cursor->host_vars,
                             &cursor->host_var_cap, count ? count : 1u,
                             sizeof(*cursor->host_vars));
    for (uint32_t index = 0u; ok && index < count; index++) {
        kept->cells[index] = cells[index];
        cursor->host_vars[index] = oem_answer_cell(
            cursor, cells[index], cursor->answer_arena, false);
        ok = cursor->host_vars[index] != NULL;
    }
    free(cells);
#if CETTA_BUILD_WITH_RUNTIME_STATS
    size_t exported_before =
        arena_accounted_live_bytes(cursor->answer_arena) +
        arena_accounted_live_bytes(cursor->runtime.stable);
#endif
    Atom *exported_goal = ok ? oem_export(cursor, goal) : NULL;
    Atom *exported_destination = exported_goal
        ? oem_export(cursor, destination) : NULL;
#if CETTA_BUILD_WITH_RUNTIME_STATS
    size_t exported_after =
        arena_accounted_live_bytes(cursor->answer_arena) +
        arena_accounted_live_bytes(cursor->runtime.stable);
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_OPEN_EQUATION_HOST_EXPORT_BYTES,
        exported_after > exported_before ? exported_after - exported_before
                                         : 0u);
#endif
    oem_forget_answer_vars(cursor);
    if (!exported_destination)
        return OEM_RUN_HANDOFF;
    kept->count = count;
    kept->height = UINT32_MAX;
    OemCont *resume = arena_alloc(&cursor->region, sizeof(*resume));
    if (!resume ||
        !oem_push_frame(cursor, program, OEM_HOST_FRAME, NULL, resume,
                        depth))
        return OEM_RUN_HANDOFF;
    *resume = (OemCont){
        .parent = cont, .program = program, .locals = locals,
        .local_count = local_count, .pc = pc + 1u, .depth = depth,
    };
    cursor->frames[cursor->frame_len - 1u].host = kept;
    cursor->host_goal = exported_goal;
    cursor->host_destination = exported_destination;
    cursor->host_var_count = count;
    cursor->host_plan = plan;
    cursor->host_mode = mode;
    cursor->host_recovers = oem_recovering(cursor, cont);
    cursor->stats.host_goals++;
    return OEM_RUN_HOST;
}

static __attribute__((noinline)) OemRun oem_host_step(
    CettaOpenEquationCursor *cursor, const CettaOpenEquationProgram *program,
    uint32_t pc, Atom **locals, uint32_t local_count, const OemCont *cont,
    uint32_t depth, const OemStep *step) {
    Atom *goal = oem_instantiate(cursor, program, locals, step->value);
    Atom *destination = oem_instantiate(cursor, program, locals, step->pattern);
    const PettaPlanNode *plan = program->host_plans[step->relation];
    /* A dynamic application has no static plan. A written call over
     * computed values keeps its static plan with value children. */
    CettaOpenEquationHostMode mode = step->target == OEM_HOST_COUNTED
        ? CETTA_OPEN_EQUATION_HOST_COUNTED
        : step->target == OEM_HOST_OCCURRENCE
            ? CETTA_OPEN_EQUATION_HOST_SOLVE
        : plan == NULL ? CETTA_OPEN_EQUATION_HOST_APPLY
                       : CETTA_OPEN_EQUATION_HOST_SOLVE;
    return oem_host_goal(cursor, program, pc, locals, local_count, cont, depth,
                         goal, destination, plan, mode);
}

/* The host enumerates match environments; each resumes the compiled body.
 * The constant result is a control value, never an existence test: the host
 * protocol transports every occurrence and its pattern-variable bindings.
 * This is list bind associativity (MatchIndexProjection.host_match_bind). */
static OemRun oem_match_host_step(
    CettaOpenEquationCursor *cursor, const CettaOpenEquationProgram *program,
    uint32_t pc, Atom **locals, uint32_t local_count, const OemCont *cont,
    uint32_t depth, const OemStep *step, Atom *reference) {
    Atom *pattern = oem_instantiate(cursor, program, locals, step->pattern);
    Atom *success = petta_semantics_success_value(&cursor->region);
    Atom *head = atom_symbol(&cursor->region, "match");
    if (!pattern || !success || !head)
        return OEM_RUN_HANDOFF;
    Atom *parts[] = {head, reference, pattern, success};
    Atom *goal = atom_expr(&cursor->region, parts, 4u);
    return oem_host_goal(cursor, program, pc, locals, local_count, cont, depth,
                         goal, success, NULL, CETTA_OPEN_EQUATION_HOST_SOLVE);
}

_Static_assert(sizeof(uintptr_t) >= 8u,
               "a slot vector's header word holds two 32-bit fields");

/* A slot vector's header word: its activation's birth height, the frames
 * live when it began, and, above it, the collection epoch it was made in.
 * An equation that cuts records instead the frames beneath its call's own
 * frame, whether or not that frame is still there, which its cut truncates
 * to (EquationCut.step_denote); a store that compares against this lower
 * height trails no less than it must. */
static inline uintptr_t oem_slot_header(const CettaOpenEquationCursor *cursor,
                                        uint32_t height) {
    return cursor->slot_epoch_bits | (uintptr_t)height;
}

static inline uint32_t oem_slot_birth(uintptr_t header) {
    return (uint32_t)header;
}

static inline uint32_t oem_slot_epoch(uintptr_t header) {
    return (uint32_t)(header >> 32);
}

/* Fill a slot at its first occurrence.  While a frame newer than the
 * activation is live, restoring it resumes the activation's code before the
 * store, so the store is trailed and the restore empties the slot again: no
 * slot outlives the path that stored it. */
static bool oem_store_slot(CettaOpenEquationCursor *cursor, Atom **locals,
                           uint32_t slot, Atom *value) {
    uintptr_t header = ((const uintptr_t *)locals)[-1];
    if (cursor->frame_len > oem_slot_birth(header)) {
        if (!oem_reserve((void **)&cursor->stores, &cursor->store_cap,
                         cursor->store_len + 1u, sizeof(*cursor->stores)))
            return false;
        cursor->stores[cursor->store_len++] =
            (OemStore){.locals = locals, .slot = slot};
    }
    /* A slot vector made before the last collection is old: the next
     * minor collection traces the slot stored now
     * (barrier_write_remember). */
    if (oem_slot_epoch(header) != cursor->collect_epoch &&
        !oem_remember_slot(cursor, locals, slot))
        return false;
    locals[slot] = value;
    return true;
}

/* A step's result meets its pattern: stored at the slot's first
 * occurrence, unified otherwise. */
static OemUnify oem_meet(CettaOpenEquationCursor *cursor,
                         const CettaOpenEquationProgram *program,
                         Atom **locals, const OemStep *step, Atom *value) {
    if (step->store)
        return oem_store_slot(cursor, locals,
                              program->templates[step->pattern].slot, value)
            ? OEM_UNIFY_OK : OEM_UNIFY_ERROR;
    return oem_unify_template(cursor, program, locals, step->pattern, value,
                              0u);
}

/* A value that no call can have as its head: a number or a string. */
static bool oem_uncallable_head(const Atom *atom) {
    return atom->kind == ATOM_GROUNDED &&
        (atom->ground.gkind == GV_INT || atom->ground.gkind == GV_FLOAT ||
         atom->ground.gkind == GV_STRING ||
         atom->ground.gkind == GV_BIGINT ||
         atom->ground.gkind == GV_RATIONAL);
}

/* An application whose head is a symbol is data when the host, asked at
 * the moment of the call, says that symbol applied to these arguments is no
 * call: no equation, form, builtin or foreign function takes it. */
static bool oem_symbol_application_inert(CettaOpenEquationCursor *cursor,
                                         const CettaOpenEquationProgram *program,
                                         Atom *application) {
    if (!cursor->runtime.head_callable || !application ||
        application->kind != ATOM_EXPR || application->expr.len == 0u)
        return false;
    Atom *head = oem_deref(cursor, application->expr.elems[0]);
    if (head->kind != ATOM_SYMBOL)
        return false;
    Arena *scratch = oem_scratch(cursor);
    ArenaMark mark = arena_mark(scratch);
    Atom **elems = arena_alloc(scratch,
                               sizeof(*elems) * application->expr.len);
    bool inert = false;
    if (elems) {
        elems[0] = head;
        for (CettaExprIndex index = 1u; index < application->expr.len;
             index++)
            elems[index] = oem_deref(cursor, application->expr.elems[index]);
        Atom *view = atom_expr(scratch, elems, application->expr.len);
        inert = view && !cursor->runtime.head_callable(
                            cursor->runtime.context, program->space, view);
    }
    arena_reset(scratch, mark);
    return inert;
}

/* Whether an application whose head has the value `head` is data, as the
 * search machine decides for the evaluated elements of a dynamic call: a
 * head that is no symbol applies only as a runtime callable (a lambda, a
 * nullary lambda, a partial application, a capture or a foreign value),
 * which its top level shows.  A symbol, an unbound cell or any other
 * grounded value is the host's to decide. */
static bool oem_application_inert(CettaOpenEquationCursor *cursor,
                                  Atom *head) {
    if (head->kind == ATOM_GROUNDED)
        return oem_uncallable_head(head);
    if (head->kind != ATOM_EXPR || head->expr.len == 0u)
        return false;
    enum { OEM_APPLY_INLINE = 8u };
    Atom *inline_elems[OEM_APPLY_INLINE];
    Arena *scratch = oem_scratch(cursor);
    ArenaMark mark = arena_mark(scratch);
    Atom **elems = head->expr.len <= OEM_APPLY_INLINE
        ? inline_elems
        : arena_alloc(scratch, sizeof(*elems) * head->expr.len);
    bool inert = elems != NULL;
    for (CettaExprIndex index = 0u; inert && index < head->expr.len; index++)
        elems[index] = oem_deref(cursor, head->expr.elems[index]);
    Atom *view = inert ? atom_expr(scratch, elems, head->expr.len) : NULL;
    inert = view && !petta_semantics_runtime_callable_value(view);
    arena_reset(scratch, mark);
    return inert;
}

/* The most arguments a pure operation the region applies may take. */
enum { OEM_PURE_INLINE_ARGS = 8u };

static inline __attribute__((always_inline)) OemRun oem_pure_apply(
    CettaOpenEquationCursor *cursor, const CettaOpenEquationProgram *program,
    Atom **locals, const OemStep *step, Atom *head, Atom **args,
    uint32_t count);

/* Whether the host's machine runs an application of `head` to `arity`
 * values as the type-pure operation `head` itself, directly: an operation
 * the profile offers, at its own arity (another arity is a partial or an
 * over-application, the host's to curry), of a head no equation extends. */
static bool oem_applied_pure(const CettaOpenEquationCursor *cursor,
                             const CettaOpenEquationProgram *program,
                             SymbolId head, uint32_t arity) {
    CettaExprLen own = 0u;
    return arity > 0u && arity <= OEM_PURE_INLINE_ARGS &&
        petta_semantics_grounded_type_pure(head) &&
        petta_semantics_intrinsic_partial_arity(head, &own) &&
        own == arity && cursor->runtime.builtin_allowed &&
        cursor->runtime.builtin_allowed(cursor->runtime.context, head) &&
        !space_equations_may_match_known_head(program->space, head);
}

/* The application `data` of the type-pure operation `head` to values, ground
 * but for a structural test's, run as the host's machine runs it
 * (oem_applied_pure); HOST when it is not such an application or the direct
 * run leaves it undecided. */
static __attribute__((noinline)) OemRun oem_applied_pure_step(
        CettaOpenEquationCursor *cursor,
        const CettaOpenEquationProgram *program, Atom **locals,
        const OemStep *step, Atom *head, Atom *data, uint32_t arity) {
    if (!oem_applied_pure(cursor, program, head->sym_id, arity))
        return OEM_RUN_HOST;
    Atom *args[OEM_PURE_INLINE_ARGS];
    for (uint32_t index = 0u; index < arity; index++) {
        Atom *arg = oem_resolve_mode(cursor, data->expr.elems[index + 1u],
                                     true);
        if (!arg)
            return OEM_RUN_HANDOFF;
        if (atom_has_vars(arg) && !petta_semantics_structural_test(head->sym_id))
            return OEM_RUN_HOST;
        args[index] = oem_grounded_operand(cursor, arg);
        if (!args[index])
            return OEM_RUN_HANDOFF;
    }
    return oem_pure_apply(cursor, program, locals, step, head, args, arity);
}

/* A dynamic call's evaluated elements: data when the head's value cannot
 * be applied, built over the elements' values; otherwise the host's. */
static const CettaOpenEquationProgram *oem_enter_evaluated(
    CettaOpenEquationCursor *cursor, Space *space, SymbolId head,
    uint32_t arity);

/* The evaluated elements of a dynamic call: data when the head's value
 * cannot be applied; a call of a relation of the region when that value is
 * a symbol naming one at the call's arity, entered as a static call enters
 * it, under the program current at the call, with this step's output as
 * its destination; a type-pure operation over values, ground but for a
 * structural test's, run here as the host's machine runs it; otherwise the
 * host's to apply. */
static OemRun oem_apply_step(CettaOpenEquationCursor *cursor,
                             const CettaOpenEquationProgram *program,
                             uint32_t pc, Atom **locals,
                             uint32_t local_count, const OemCont *cont,
                             uint32_t depth, const OemStep *step) {
    const OemTemplate *goal = &program->templates[step->value];
    if (goal->kind != OEM_T_BUILD || goal->count == 0u)
        return OEM_RUN_HOST;
    Atom *head = oem_instantiate(cursor, program, locals,
                                 program->template_children[goal->first]);
    if (!head)
        return OEM_RUN_HANDOFF;
    head = oem_deref(cursor, head);
    bool symbol = head->kind == ATOM_SYMBOL;
    if (!symbol && !oem_application_inert(cursor, head))
        return OEM_RUN_HOST;
    Atom *data = oem_instantiate(cursor, program, locals, step->value);
    uint32_t arity = data ? (uint32_t)data->expr.len - 1u : 0u;
    const CettaOpenEquationProgram *target =
        data && symbol && step->target == OEM_HOST_APPLY &&
                cursor->runtime.current
            ? oem_enter_evaluated(cursor, program->space, head->sym_id, arity)
            : NULL;
    if (data && symbol && !target && arity == 2u) {
        /* A grounded operation over plain scalars, which the host applies
         * as its ready call: the scalar evaluator's exact result, unless the
         * program defines equations of the head, which the host's call
         * would enter instead. */
        Atom *args[2] = {
            oem_resolve_mode(cursor, data->expr.elems[1], true),
            oem_resolve_mode(cursor, data->expr.elems[2], true),
        };
        CettaPlainScalar scalar;
        if (args[0] && args[1] &&
            oem_scalar_operation(head, args, 2u, &scalar) &&
            !space_equations_may_match_known_head(program->space,
                                                  head->sym_id)) {
            Atom *result = scalar.kind == CETTA_PLAIN_SCALAR_BOOL
                ? oem_truth(cursor, scalar.as.boolean)
                : grounded_plain_scalar_materialize(&cursor->region, &scalar);
            if (!result)
                return OEM_RUN_HANDOFF;
            OemUnify unified = oem_meet(cursor, program, locals, step, result);
            if (unified == OEM_UNIFY_ERROR)
                return OEM_RUN_HANDOFF;
            return unified == OEM_UNIFY_OK ? OEM_RUN_CALLED : OEM_RUN_FAILED;
        }
    }
    if (data && symbol && !target) {
        OemRun run = oem_applied_pure_step(cursor, program, locals, step,
                                           head, data, arity);
        if (run != OEM_RUN_HOST)
            return run;
    }
    if (data && symbol && !target &&
        !oem_symbol_application_inert(cursor, program, data))
        return OEM_RUN_HOST;
    if (target) {
        Atom **args = arena_alloc(&cursor->region,
                                  sizeof(*args) * (arity ? arity : 1u));
        OemCont *record = arena_alloc(&cursor->region, sizeof(*record));
        if (!args || !record)
            return OEM_RUN_HANDOFF;
        for (uint32_t index = 0u; index < arity; index++)
            args[index] = data->expr.elems[index + 1u];
        /* The step's output slot, stored at its first occurrence, takes a
         * cell for the callee to fill, as it does when the host fills it. */
        if (step->store) {
            Atom *cell = oem_new_cell(cursor);
            if (!cell ||
                !oem_store_slot(cursor, locals,
                                program->templates[step->pattern].slot,
                                cell))
                return OEM_RUN_HANDOFF;
        }
        *record = (OemCont){
            .parent = cont, .program = program, .locals = locals,
            .local_count = local_count, .pc = pc + 1u,
            .pattern = step->pattern, .depth = depth,
            .recovers = step->recovers,
        };
        if (step->recovers)
            cursor->recovering = true;
        return oem_push_frame(cursor, target, 0u, args, record, depth + 1u)
            ? OEM_RUN_ENTERED : OEM_RUN_HANDOFF;
    }
    OemUnify built = data
        ? oem_meet(cursor, program, locals, step, data)
        : OEM_UNIFY_ERROR;
    if (built == OEM_UNIFY_ERROR)
        return OEM_RUN_HANDOFF;
    return built == OEM_UNIFY_OK ? OEM_RUN_CALLED : OEM_RUN_FAILED;
}

/* A ground `add-atom`: the host admits the payload as its machine's ready
 * path does, and its result meets the step's destination; an error it
 * returns is raised, as the machine raises it.  A payload with an unbound
 * cell, a space the reference does not name, or a call the host declines
 * is the host's.  The call is built in the scratch arena, since the space
 * keeps its own copy; the region, which its region parts come from, is
 * the source the host may memoize them by. */
static OemRun oem_admit_step(CettaOpenEquationCursor *cursor,
                             const CettaOpenEquationProgram *program,
                             Atom **locals, const OemStep *step,
                             Atom **value_out) {
    if (!cursor->runtime.admit_ground_atom)
        return OEM_RUN_HOST;
    Arena *scratch = oem_scratch(cursor);
    ArenaMark mark = arena_mark(scratch);
    Atom *call = oem_instantiate_in(cursor, scratch, program, locals,
                                    step->value);
    call = call ? oem_resolve_in(cursor, call, true, scratch, NULL, false)
                : NULL;
    OemRun run = OEM_RUN_HOST;
    Atom *result = NULL;
    if (!call) {
        run = OEM_RUN_HANDOFF;
    } else if (call->kind == ATOM_EXPR && call->expr.len == 3u &&
               !atom_has_vars(call->expr.elems[2])) {
        Atom *reference = call->expr.elems[1];
        Space *target = cursor->runtime.resolve_space
            ? cursor->runtime.resolve_space(cursor->runtime.context,
                                            program->space, &cursor->region,
                                            reference)
            : NULL;
        if (!target && atom_is_symbol_id(reference, g_builtin_syms.self))
            target = program->space;
        if (target &&
            cursor->runtime.admit_ground_atom(
                cursor->runtime.context, program->space, target,
                &cursor->region, call, &result) &&
            result)
            run = atom_is_error(result)
                ? oem_publish_raise(cursor, result, value_out)
                : OEM_RUN_CALLED;
    }
    if (run == OEM_RUN_CALLED) {
        if (!cursor->admitted && !atom_has_vars(result))
            cursor->admitted = atom_deep_copy(&cursor->names, result);
        if (cursor->admitted && atom_eq(result, cursor->admitted))
            result = cursor->admitted;
    }
    arena_reset(scratch, mark);
    if (run != OEM_RUN_CALLED)
        return run;
    OemUnify unified = oem_meet(cursor, program, locals, step, result);
    if (unified == OEM_UNIFY_ERROR)
        return OEM_RUN_HANDOFF;
    return unified == OEM_UNIFY_OK ? OEM_RUN_CALLED : OEM_RUN_FAILED;
}

/* Whether `atom` is a list the region reads as the search machine does: an
 * expression that is no internal representation (a counted collection, a
 * cons cell, a Prolog term), all of whose elements are carried as they
 * stand. */
static bool oem_plain_list(const Atom *atom) {
    return atom->kind == ATOM_EXPR &&
        (atom->expr.len == 0u ||
         atom->expr.elems[0]->kind != ATOM_GROUNDED ||
         atom->expr.elems[0]->ground.gkind != GV_INTERNAL_TAG);
}

/* A list native over closed lists: `append` and `union-atom` of two are
 * their concatenation, the `length` of one its element count,
 * `exclude-item`, `list_to_set` and `unique-atom` (list_to_set) and
 * `alpha-unique-atom` the search machine's own definitions, `car-atom` and
 * `cdr-atom` its first element and its rest, or () for anything but a
 * non-empty expression, `decons-atom` and `decons` the pair of its first
 * element and its rest, and no answer for anything but a non-empty
 * expression.
 * These read a list's spine alone: its elements are carried as they stand,
 * variables included, and compared by identity, as SWI's ==/2 does, or,
 * by `alpha-unique-atom`, up to a renaming of their variables.  `append`,
 * `union-atom` and `length` also read a chain of cells that ends in a flat
 * list, as the flat list of its elements.  An unbound list, a partial or
 * improper chain, a cell anywhere else, a counted collection and, but for
 * car-atom and cdr-atom, a non-list are the host's, which answers them
 * relationally or raises as SWI does. */
static __attribute__((noinline)) OemRun oem_list_step(
        CettaOpenEquationCursor *cursor,
        const CettaOpenEquationProgram *program, Atom **locals,
        const OemStep *step, const OemTemplate *goal) {
    Atom *args[2];
    uint32_t count = goal->count - 1u;
    if (goal->kind != OEM_T_BUILD || count == 0u || count > 2u)
        return OEM_RUN_HOST;
    PeTTaForm form = petta_semantics_form(step->op);
    bool spine = form == PETTA_FORM_APPEND || form == PETTA_FORM_LENGTH ||
        step->op == g_builtin_syms.union_atom;
    for (uint32_t index = 0u; index < count; index++) {
        Atom *arg = oem_instantiate(
            cursor, program, locals,
            program->template_children[goal->first + 1u + index]);
        arg = arg ? oem_resolve_mode(cursor, arg, true) : NULL;
        if (!arg)
            return OEM_RUN_HANDOFF;
        if (arg->kind == ATOM_VAR)
            return OEM_RUN_HOST;
        if (petta_semantics_open_cons_built() &&
            atom_structural_may_have_list_carrier(arg)) {
            /* append/3 and length/2 read a list's spine alone.  A chain of
             * cells that ends in a flat list reads as the flat list of its
             * elements, which stand as they are
             * (ListCells.denoteList_closedElems); a partial or improper
             * list is the host's. */
            Atom *flat = spine
                ? petta_semantics_materialize_closed_logical_list(
                      &cursor->region, arg)
                : NULL;
            if (!flat)
                return OEM_RUN_HOST;
            arg = flat;
        }
        args[index] = arg;
    }
    Atom *list = args[count - 1u];
    Atom *result = NULL;
    if (step->op == g_builtin_syms.decons_atom ||
        step->op == g_builtin_syms.petta_decons) {
        /* PeTTa's decons-atom and decons map [H|T] to (H T): a non-empty
         * list is its first element and its rest; () and every non-list
         * have no answer. */
        if (list->kind == ATOM_EXPR && !oem_plain_list(list))
            return OEM_RUN_HOST;
        if (list->kind != ATOM_EXPR || list->expr.len == 0u)
            return OEM_RUN_FAILED;
        Atom *rest = atom_expr_suffix(&cursor->region, list, 1u);
        result = rest
            ? atom_expr2(&cursor->region, list->expr.elems[0], rest) : NULL;
        if (!result)
            return OEM_RUN_HANDOFF;
        OemUnify unified = oem_meet(cursor, program, locals, step, result);
        if (unified == OEM_UNIFY_ERROR)
            return OEM_RUN_HANDOFF;
        return unified == OEM_UNIFY_OK ? OEM_RUN_CALLED : OEM_RUN_FAILED;
    }
    if (step->op == g_builtin_syms.car_atom ||
        step->op == g_builtin_syms.cdr_atom) {
        if (list->kind == ATOM_EXPR && !oem_plain_list(list))
            return OEM_RUN_HOST;
        result = list->kind != ATOM_EXPR || list->expr.len == 0u
            ? atom_unit(&cursor->region)
            : step->op == g_builtin_syms.car_atom
            ? list->expr.elems[0]
            : atom_expr_suffix(&cursor->region, list, 1u);
        if (!result)
            return OEM_RUN_HANDOFF;
        OemUnify unified = oem_meet(cursor, program, locals, step, result);
        if (unified == OEM_UNIFY_ERROR)
            return OEM_RUN_HANDOFF;
        return unified == OEM_UNIFY_OK ? OEM_RUN_CALLED : OEM_RUN_FAILED;
    }
    bool append = form == PETTA_FORM_APPEND ||
        step->op == g_builtin_syms.union_atom;
    if (!oem_plain_list(list) || (append && !oem_plain_list(args[0])))
        return OEM_RUN_HOST;
    if (append) {
        CettaExprLen left = args[0]->expr.len;
        CettaExprLen length = left + list->expr.len;
        if (length < left || !cetta_expr_len_mul_fits_size(length,
                                                           sizeof(Atom *)))
            return OEM_RUN_HOST;
        Atom **items = length
            ? arena_alloc(&cursor->region, sizeof(*items) * (size_t)length)
            : NULL;
        if (length && !items)
            return OEM_RUN_HANDOFF;
        if (left)
            memcpy(items, args[0]->expr.elems, sizeof(*items) * (size_t)left);
        if (list->expr.len)
            memcpy(items + left, list->expr.elems,
                   sizeof(*items) * (size_t)list->expr.len);
        result = atom_expr(&cursor->region, items, length);
    } else if (form == PETTA_FORM_LENGTH) {
        if (list->expr.len > (CettaExprLen)INT64_MAX)
            return OEM_RUN_HOST;
        result = atom_int(&cursor->region, (int64_t)list->expr.len);
    } else if (form == PETTA_FORM_LIST_TO_SET ||
               step->op == g_builtin_syms.unique_atom) {
        result = petta_semantics_list_to_set(&cursor->region, list);
    } else if (form == PETTA_FORM_ALPHA_UNIQUE) {
        result = petta_semantics_alpha_unique(&cursor->region, list);
    } else if (form == PETTA_FORM_EXCLUDE_ITEM) {
        result = petta_semantics_exclude_item(&cursor->region, args[0], list);
    } else {
        return OEM_RUN_HOST;
    }
    if (!result)
        return OEM_RUN_HANDOFF;
    OemUnify unified = oem_meet(cursor, program, locals, step, result);
    if (unified == OEM_UNIFY_ERROR)
        return OEM_RUN_HANDOFF;
    return unified == OEM_UNIFY_OK ? OEM_RUN_CALLED : OEM_RUN_FAILED;
}

/* Template `index`'s child `child`, of a built template. */
static uint32_t oem_template_child(const CettaOpenEquationProgram *program,
                                   uint32_t index, uint32_t child) {
    return program->template_children[program->templates[index].first +
                                      child];
}

/* An existence observer, `(== () (collapse (once (match S P T))))` with an
 * atomic constant T: whether the space S has a row matching P, as the
 * search machine answers it where the space proves that ground matching is
 * structural membership (space_match_exists_ground_exact).  A named space
 * with a schema, a symbol naming no space, a pattern with variables and a
 * space that cannot prove it are the host's, which answers the goal
 * whole. */
static __attribute__((noinline)) OemRun oem_exists_step(
        CettaOpenEquationCursor *cursor,
        const CettaOpenEquationProgram *program, Atom **locals,
        const OemStep *step) {
    uint32_t side = oem_template_child(program, step->value, 1u);
    const OemTemplate *empty = &program->templates[side];
    if (empty->kind == OEM_T_LITERAL && empty->literal->kind == ATOM_EXPR &&
        empty->literal->expr.len == 0u)
        side = oem_template_child(program, step->value, 2u);
    uint32_t once = oem_template_child(program, side, 1u);
    uint32_t match = oem_template_child(program, once, 1u);
    Atom *reference = oem_instantiate(cursor, program, locals,
                                      oem_template_child(program, match, 1u));
    Atom *pattern = oem_instantiate(cursor, program, locals,
                                    oem_template_child(program, match, 2u));
    if (!reference || !pattern)
        return OEM_RUN_HANDOFF;
    reference = oem_deref(cursor, reference);
    pattern = oem_resolve_mode(cursor, pattern, true);
    if (!pattern)
        return OEM_RUN_HANDOFF;
    Space *space = cursor->runtime.resolve_space
        ? cursor->runtime.resolve_space(cursor->runtime.context,
                                        program->space, &cursor->region,
                                        reference)
        : NULL;
    if (reference->kind == ATOM_SYMBOL) {
        if (!space && reference->sym_id == g_builtin_syms.self)
            space = program->space;
        if (!space ||
            oem_space_has_schema(cursor, program->space, reference))
            return OEM_RUN_HOST;
    }
    if (!space || atom_has_vars(pattern))
        return OEM_RUN_HOST;
    bool applicable = false;
    bool found = space_match_exists_ground_exact(space, pattern, &applicable);
    if (!applicable)
        return OEM_RUN_HOST;
    Atom *truth = oem_truth(cursor, !found);
    if (!truth)
        return OEM_RUN_HANDOFF;
    OemUnify unified = oem_meet(cursor, program, locals, step, truth);
    if (unified == OEM_UNIFY_ERROR)
        return OEM_RUN_HANDOFF;
    return unified == OEM_UNIFY_OK ? OEM_RUN_CALLED : OEM_RUN_FAILED;
}

/* A type-pure operation `head` applied to resolved operands, ground but
 * for a structural test's, whose unbound cells stand as their variables, as
 * the search machine's direct path runs it once every argument is a value:
 * two machine integers take the region's integer path, as its tests and
 * primitives do, plain scalars the scalar evaluator, anything else the
 * operation's own implementation; an empty result fails, a truth value takes
 * the host's spelling, and the result meets the step's destination.  An
 * error, or no result, is the host's (HOST). */
static inline __attribute__((always_inline)) OemRun oem_pure_apply(
    CettaOpenEquationCursor *cursor, const CettaOpenEquationProgram *program,
    Atom **locals, const OemStep *step, Atom *head, Atom **args,
    uint32_t count) {
    SymbolId op = head->sym_id;
    CettaPlainScalar scalar;
    Atom *result = NULL;
    int64_t left = 0;
    int64_t right = 0;
    int64_t value = 0;
    bool test = oem_is_test(op);
    if (count == 2u && oem_int_value(args[0], &left) &&
        oem_int_value(args[1], &right) &&
        (test || (oem_is_prim(op) &&
                  oem_prim(op, left, right, &value)))) {
        /* Two machine integers take the region's integer path, as its
         * tests and primitives do. */
        result = test ? oem_truth(cursor, oem_test(op, left, right))
                      : atom_int(&cursor->region, value);
        if (!result)
            return OEM_RUN_HANDOFF;
    } else if (oem_scalar_operation(head, args, count, &scalar)) {
        result = scalar.kind == CETTA_PLAIN_SCALAR_BOOL
            ? oem_truth(cursor, scalar.as.boolean)
            : grounded_plain_scalar_materialize(&cursor->region, &scalar);
        if (!result)
            return OEM_RUN_HANDOFF;
    } else {
        result = grounded_dispatch(&cursor->region, head, args, count);
        if (!result || atom_is_error(result))
            return OEM_RUN_HOST;
        if (atom_is_petta_no_result(result))
            return OEM_RUN_FAILED;
        bool truth = false;
        if (petta_semantics_truth_value(result, &truth)) {
            result = oem_truth(cursor, truth);
            if (!result)
                return OEM_RUN_HANDOFF;
        }
    }
    OemUnify unified = oem_meet(cursor, program, locals, step, result);
    if (unified == OEM_UNIFY_ERROR)
        return OEM_RUN_HANDOFF;
    return unified == OEM_UNIFY_OK ? OEM_RUN_CALLED : OEM_RUN_FAILED;
}

/* `get-metatype` of a value the host's registry does not rename: an
 * unbound cell is a Variable, a list an Expression, a grounded value
 * Grounded, and a symbol what SWI-PeTTa's get-metatype/2 makes it, Grounded
 * for a truth value or a registered function and Symbol otherwise, as the
 * host answers them.  A registry reference, a cons cell and any variable of
 * the host's are the host's, and so is a symbol when the host cannot tell
 * whether its program defines it. */
static __attribute__((noinline)) OemRun oem_metatype_step(
        CettaOpenEquationCursor *cursor,
        const CettaOpenEquationProgram *program, Atom **locals,
        const OemStep *step, const OemTemplate *goal) {
    if (goal->kind != OEM_T_BUILD || goal->count != 2u)
        return OEM_RUN_HOST;
    Atom *value = oem_instantiate(
        cursor, program, locals,
        program->template_children[goal->first + 1u]);
    if (!value)
        return OEM_RUN_HANDOFF;
    value = oem_deref(cursor, value);
    uint32_t index = 0u;
    Atom *type = NULL;
    if (oem_is_cell(cursor, value, &index))
        type = atom_variable_type(&cursor->region);
    else if (value->kind == ATOM_EXPR && oem_plain_list(value) &&
             !registry_ref_name_key(value, NULL))
        type = atom_expression_type(&cursor->region);
    else if (value->kind == ATOM_GROUNDED &&
             value->ground.gkind != GV_INTERNAL_TAG)
        type = atom_grounded_type(&cursor->region);
    else if (value->kind == ATOM_SYMBOL && cursor->runtime.function_registered)
        type = petta_semantics_symbol_metatype(
            &cursor->region, value->sym_id,
            cursor->runtime.function_registered(cursor->runtime.context,
                                                value->sym_id));
    else
        return OEM_RUN_HOST;
    if (!type)
        return OEM_RUN_HANDOFF;
    OemUnify unified = oem_meet(cursor, program, locals, step, type);
    if (unified == OEM_UNIFY_ERROR)
        return OEM_RUN_HANDOFF;
    return unified == OEM_UNIFY_OK ? OEM_RUN_CALLED : OEM_RUN_FAILED;
}

/* The region decides a HOST step its fast path covers.  An expression
 * whose head has a value no call can have is data, built over its values.
 * A type-pure operation runs the search machine's direct path once every
 * argument is a value: the operation's own implementation, an empty result
 * a failure, a truth value in the host's spelling.  The result meets the
 * step's destination.  CALLED continues after the step; what this does not
 * decide (a head that may be called, an unbound argument but to a
 * structural test, no result, an error, which the host's dialect treats in
 * its own way) is the host's, as HOST. */
static __attribute__((noinline)) OemRun oem_fast_host_step(
        CettaOpenEquationCursor *cursor,
        const CettaOpenEquationProgram *program, Atom **locals,
        const OemStep *step, Atom **value_out) {
    if (step->target == OEM_HOST_ADMIT) {
        /* A bounded run may be abandoned and run again, so it leaves every
         * effect to the unbounded run that follows. */
        if (cursor->runtime.activation_budget ||
            cursor->runtime.depth_bound) {
            cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_UNSUPPORTED;
            return OEM_RUN_HANDOFF;
        }
        return oem_admit_step(cursor, program, locals, step, value_out);
    }
    const OemTemplate *goal = &program->templates[step->value];
    if (step->target == OEM_HOST_LIST)
        return oem_list_step(cursor, program, locals, step, goal);
    if (step->target == OEM_HOST_EXISTS)
        return oem_exists_step(cursor, program, locals, step);
    if (step->target == OEM_HOST_METATYPE)
        return oem_metatype_step(cursor, program, locals, step, goal);
    if (step->target == OEM_HOST_SORT) {
        /* A ground list sorts here; a value with variables, and one whose
         * sort SWI rejects, go to the host, which raises as SWI does. */
        if (goal->kind != OEM_T_BUILD || goal->count != 2u)
            return OEM_RUN_HOST;
        Atom *list = oem_instantiate(
            cursor, program, locals,
            program->template_children[goal->first + 1u]);
        list = list ? oem_resolve_mode(cursor, list, true) : NULL;
        if (!list)
            return OEM_RUN_HANDOFF;
        if (atom_has_vars(list))
            return OEM_RUN_HOST;
        bool type_error = false;
        Atom *sorted = petta_semantics_sort_value(
            &cursor->region, list, step->op == g_builtin_syms.sort_atom,
            &type_error);
        if (!sorted)
            return type_error ? OEM_RUN_HOST : OEM_RUN_HANDOFF;
        OemUnify unified = oem_meet(cursor, program, locals, step, sorted);
        if (unified == OEM_UNIFY_ERROR)
            return OEM_RUN_HANDOFF;
        return unified == OEM_UNIFY_OK ? OEM_RUN_CALLED : OEM_RUN_FAILED;
    }
    if (goal->kind != OEM_T_BUILD || goal->count == 0u ||
        goal->count - 1u > OEM_PURE_INLINE_ARGS)
        return OEM_RUN_HOST;
    uint32_t count = goal->count - 1u;
    Atom *args[OEM_PURE_INLINE_ARGS];
    for (uint32_t index = 0u; index < count; index++) {
        Atom *arg = oem_instantiate(
            cursor, program, locals,
            program->template_children[goal->first + 1u + index]);
        arg = arg ? oem_resolve_mode(cursor, arg, true) : NULL;
        if (!arg)
            return OEM_RUN_HANDOFF;
        if (atom_has_vars(arg) && !petta_semantics_structural_test(step->op))
            return OEM_RUN_HOST;
        args[index] = oem_grounded_operand(cursor, arg);
        if (!args[index])
            return OEM_RUN_HANDOFF;
    }
    /* The goal's head is its literal, which the program holds. */
    const OemTemplate *spelled =
        &program->templates[program->template_children[goal->first]];
    Atom *head = spelled->kind == OEM_T_LITERAL &&
            spelled->literal->kind == ATOM_SYMBOL &&
            spelled->literal->sym_id == step->op
        ? spelled->literal : atom_symbol_id(&cursor->region, step->op);
    if (!head)
        return OEM_RUN_HANDOFF;
    return oem_pure_apply(cursor, program, locals, step, head, args, count);
}

typedef struct OemAbsentHead {
    SymbolId head;
    uint32_t arity;
    uint64_t epoch;
    SpaceProgramToken token;
} OemAbsentHead;

/* The program an evaluated call enters for `head` at `arity`, or NULL,
 * remembered while neither the authorities nor the space's program change:
 * most evaluated heads are data, which would otherwise be looked up at
 * every application. */
static const CettaOpenEquationProgram *oem_enter_evaluated(
    CettaOpenEquationCursor *cursor, Space *space, SymbolId head,
    uint32_t arity) {
    for (uint32_t index = 0u; index < cursor->absent_len; index++) {
        const OemAbsentHead *absent = &cursor->absent[index];
        if (absent->head == head && absent->arity == arity &&
            absent->epoch == cursor->authority_epoch &&
            space_program_token_is_current(absent->token))
            return NULL;
    }
    const CettaOpenEquationProgram *program =
        oem_enter_current(cursor, space, head, arity);
    if (!program &&
        oem_reserve((void **)&cursor->absent, &cursor->absent_cap,
                    cursor->absent_len + 1u, sizeof(*cursor->absent)))
        cursor->absent[cursor->absent_len++] = (OemAbsentHead){
            .head = head, .arity = arity, .epoch = cursor->authority_epoch,
            .token = space_program_token(space),
        };
    return program;
}

/* A control the region runs through its local relation.  Alternatives are
 * a call of it: each answer continues after the step.  A collection pushes
 * its frame beneath the call, whose answers return to the YIELD step; when
 * the call has none left, the frame completes (oem_cursor_run).  A once
 * pushes its frame beneath the call, whose first answer reaches the CUT
 * step.  The control steps run out of line, which leaves the body runner's
 * registers to the steps every body runs. */
static __attribute__((noinline)) OemRun oem_local_step(
    CettaOpenEquationCursor *cursor,
                             const CettaOpenEquationProgram *program,
                             uint32_t pc, Atom **locals, uint32_t local_count,
                             const OemCont *cont, uint32_t depth,
                             const OemStep *step) {
    Atom **args = arena_alloc(
        &cursor->region,
        sizeof(*args) * (step->arg_count ? step->arg_count : 1u));
    OemCont *after = arena_alloc(&cursor->region, sizeof(*after));
    if (!args || !after)
        return OEM_RUN_HANDOFF;
    for (uint32_t index = 0u; index < step->arg_count; index++) {
        args[index] = oem_instantiate(
            cursor, program, locals,
            program->step_args[step->first_arg + index]);
        if (!args[index])
            return OEM_RUN_HANDOFF;
    }
    *after = (OemCont){
        .parent = cont, .program = program, .locals = locals,
        .local_count = local_count, .pc = pc + 1u, .pattern = step->pattern,
        .depth = depth,
    };
    const OemControl *control = &program->controls[step->control];
    /* A superpose in a tail position hands its caller's continuation to its
     * alternatives, whose answers meet the destination this equation's own
     * output met; the return after the step serves the host's route. */
    if (step->target == OEM_HOST_ALTERNATIVES)
        return oem_push_frame(cursor, program, control->local, args,
                              step->tail ? cont : after, depth + 1u)
            ? OEM_RUN_CALLED : OEM_RUN_HANDOFF;
    if (step->target == OEM_HOST_ONCE) {
        after->pc = control->resume;
        return oem_push_frame(cursor, program, OEM_ONCE_FRAME, NULL, NULL,
                              depth) &&
                oem_push_frame(cursor, program, control->local, args, after,
                               depth + 1u)
            ? OEM_RUN_CALLED : OEM_RUN_HANDOFF;
    }
    OemCont *answer = arena_alloc(&cursor->region, sizeof(*answer));
    if (!answer)
        return OEM_RUN_HANDOFF;
    *answer = (OemCont){
        .parent = cont, .program = program, .locals = locals,
        .local_count = local_count, .pc = control->resume,
        .pattern = control->yield, .depth = depth,
    };
    if (!cursor->collected_ready) {
        arena_init(&cursor->collected);
        arena_set_runtime_kind(&cursor->collected,
                               CETTA_ARENA_RUNTIME_KIND_SCRATCH);
        arena_set_hashcons(&cursor->collected, NULL);
        cursor->collected_ready = true;
    }
    ArenaMark mark = arena_mark(&cursor->collected);
    OemCollectBox *box = arena_alloc(&cursor->collected, sizeof(*box));
    if (!box ||
        !oem_push_frame(cursor, program, OEM_COLLECT_FRAME, NULL, after,
                        depth))
        return OEM_RUN_HANDOFF;
    *box = (OemCollectBox){.mark = mark, .limit = control->limit};
    cursor->frames[cursor->frame_len - 1u].collect = box;
    return oem_push_frame(cursor, program, control->local, args, answer,
                          depth + 1u)
        ? OEM_RUN_CALLED : OEM_RUN_HANDOFF;
}

/* A bounded collection holds its last answer: every frame above the
 * collection's own goes, restoring nothing, and backtracking reaches the
 * collection, which completes.  When a host goal's frame is among them, the
 * host's choices above the height it reported for the oldest such goal go
 * too, as once's CUT drops them: the run stops with OEM_RUN_CUT, and the
 * choice beneath that height resumes the cursor at the collection.  Out of
 * line: it runs once per bounded collection. */
static __attribute__((noinline)) OemRun oem_bound_reached(
    CettaOpenEquationCursor *cursor) {
    bool crossed = false;
    uint32_t height = UINT32_MAX;
    uint32_t index = cursor->frame_len;
    while (index > 0u &&
           cursor->frames[index - 1u].relation != OEM_COLLECT_FRAME) {
        const OemFrame *frame = &cursor->frames[index - 1u];
        if (oem_frame_is_host(frame)) {
            crossed = true;
            height = frame->host->height;
        }
        index--;
    }
    if (index == 0u || (crossed && height == UINT32_MAX))
        return OEM_RUN_HANDOFF;
    oem_truncate_frames(cursor, index);
    if (!crossed)
        return OEM_RUN_FAILED;
    cursor->cut_height = height;
    return OEM_RUN_CUT;
}

/* A collection's answer, copied out of its branch before the branch rolls
 * back: bound cells are replaced by their values, each unbound cell becomes
 * a variable of this answer's own, and the copy goes to the innermost
 * collection's storage.  Then the branch fails, and the next answer is
 * sought; at the collection's bound, every frame above the collection's own
 * goes instead, and backtracking reaches the collection, which completes. */
static __attribute__((noinline)) OemRun oem_yield_step(
    CettaOpenEquationCursor *cursor,
                             const CettaOpenEquationProgram *program,
                             Atom **locals, const OemStep *step) {
    OemCollectBox *box = NULL;
    for (uint32_t index = cursor->frame_len; !box && index > 0u; index--) {
        if (cursor->frames[index - 1u].relation == OEM_COLLECT_FRAME)
            box = cursor->frames[index - 1u].collect;
    }
    Atom *value = box
        ? oem_instantiate(cursor, program, locals, step->value) : NULL;
    if (!value)
        return OEM_RUN_HANDOFF;
    oem_forget_answer_vars(cursor);
    Atom *copy = oem_resolve_in(cursor, value, false, &cursor->collected,
                                NULL, true);
    oem_forget_answer_vars(cursor);
    OemCollectItem *item = copy
        ? arena_alloc(&cursor->collected, sizeof(*item)) : NULL;
    if (!item)
        return OEM_RUN_HANDOFF;
    *item = (OemCollectItem){.value = copy, .prev = box->last};
    box->last = item;
    box->count++;
    if (box->limit == 0u || box->count < box->limit)
        return OEM_RUN_FAILED;
    return oem_bound_reached(cursor);
}

/* A once's first answer: the frames its body left, and the once's own, go
 * without restoring anything, and the body continues at `target`.  When a
 * host goal's frame is among them, the host's choices above the height it
 * reported for the oldest such goal go too: the cursor leaves a resumption
 * at the once's place, which the choice beneath that height, the holder of
 * the once's frames, resumes. */
static __attribute__((noinline)) OemRun oem_cut_step(
    CettaOpenEquationCursor *cursor,
                           const CettaOpenEquationProgram *program,
                           Atom **locals, uint32_t local_count,
                           const OemCont *cont, uint32_t depth,
                           const OemStep *step) {
    uint32_t index = cursor->frame_len;
    bool crossed = false;
    uint32_t height = UINT32_MAX;
    while (index > 0u &&
           cursor->frames[index - 1u].relation != OEM_ONCE_FRAME) {
        const OemFrame *frame = &cursor->frames[index - 1u];
        if (oem_frame_is_host(frame)) {
            crossed = true;
            height = frame->host->height;
        }
        index--;
    }
    if (index == 0u || (crossed && height == UINT32_MAX))
        return OEM_RUN_HANDOFF;
    oem_truncate_frames(cursor, index - 1u);
    if (!crossed)
        return OEM_RUN_CALLED;
    OemCont *resume = arena_alloc(&cursor->region, sizeof(*resume));
    if (!resume)
        return OEM_RUN_HANDOFF;
    *resume = (OemCont){
        .parent = cont, .program = program, .locals = locals,
        .local_count = local_count, .pc = step->target, .depth = depth,
    };
    if (!oem_push_frame(cursor, NULL, OEM_RESUME_FRAME, NULL, resume, depth))
        return OEM_RUN_HANDOFF;
    cursor->cut_height = height;
    return OEM_RUN_CUT;
}

/* PeTTa's `(cut)`, Prolog's `!` with the value true: the pattern meets the
 * truth value first, as PeTTa's translation binds a let's pattern before
 * its value's goals run; then the frames above the barrier the activation's
 * header records go without restoring anything: its call's untried
 * equations and the alternatives the goals before the cut left.  When a
 * host goal's frame is among them, the host's choices above the height it
 * reported for the oldest such goal go too: the cursor leaves a resumption
 * at the barrier, which the choice beneath that height resumes, as a once's
 * CUT does. */
static __attribute__((noinline)) OemRun oem_equation_cut_step(
    CettaOpenEquationCursor *cursor, const CettaOpenEquationProgram *program,
    uint32_t pc, Atom **locals, uint32_t local_count, const OemCont *cont,
    uint32_t depth, const OemStep *step) {
    Atom *truth = oem_truth(cursor, true);
    OemUnify met = truth ? oem_meet(cursor, program, locals, step, truth)
                         : OEM_UNIFY_ERROR;
    if (met != OEM_UNIFY_OK)
        return met == OEM_UNIFY_FAIL ? OEM_RUN_FAILED : OEM_RUN_HANDOFF;
    uint32_t barrier = oem_slot_birth(((const uintptr_t *)locals)[-1]);
    if (barrier > cursor->frame_len)
        return OEM_RUN_HANDOFF;
    bool crossed = false;
    uint32_t height = UINT32_MAX;
    for (uint32_t index = cursor->frame_len; index > barrier; index--) {
        const OemFrame *frame = &cursor->frames[index - 1u];
        if (oem_frame_is_host(frame)) {
            crossed = true;
            height = frame->host->height;
        }
    }
    if (crossed && height == UINT32_MAX)
        return OEM_RUN_HANDOFF;
    oem_truncate_frames(cursor, barrier);
    if (!crossed)
        return OEM_RUN_CALLED;
    OemCont *resume = arena_alloc(&cursor->region, sizeof(*resume));
    if (!resume)
        return OEM_RUN_HANDOFF;
    *resume = (OemCont){
        .parent = cont, .program = program, .locals = locals,
        .local_count = local_count, .pc = pc + 1u, .depth = depth,
    };
    if (!oem_push_frame(cursor, NULL, OEM_RESUME_FRAME, NULL, resume, depth))
        return OEM_RUN_HANDOFF;
    cursor->cut_height = height;
    return OEM_RUN_CUT;
}

static Atom *oem_fresh_var(CettaOpenEquationCursor *cursor, VarId id);

/* What an import makes of the terms it reads: the call's arguments, whose
 * variables are the call's cells and whose ground parts are shared; a stored
 * row's copy, whose variables are fresh cells; a collected answer, copied out
 * of the collection's storage, whose variables are fresh cells; a host's
 * answer, copied whole, whose variables are the goal's cells, the call's, or
 * fresh ones. */
typedef enum {
    OEM_IMPORT_CALL,
    OEM_IMPORT_ROW,
    OEM_IMPORT_COLLECTED,
    OEM_IMPORT_HOST,
} OemImportKind;

typedef struct {
    OemImportKind kind;
    Atom *const *host_vars;
    const OemHostCells *cells;
    const CettaVarIndex *host_index;
    /* Keys have meaning only under this fixed mode and variable context. */
    OemAtomMap images;
    AtomDeepCopySession *copies;
} OemImport;

static void oem_import_init(OemImport *import, OemImportKind kind) {
    import->kind = kind;
    import->host_vars = NULL;
    import->cells = NULL;
    import->host_index = NULL;
    import->copies = NULL;
    oem_atom_map_init(&import->images);
}

static void oem_import_free(OemImport *import) {
    oem_atom_map_free(&import->images);
    atom_deep_copy_session_free(import->copies);
}

static Atom *oem_import_copy(CettaOpenEquationCursor *cursor,
                              OemImport *import, Atom *atom) {
    if (!import->copies)
        import->copies = atom_deep_copy_session_new(&cursor->region);
    return import->copies ? atom_deep_copy_session_copy(import->copies, atom)
                          : NULL;
}

/* A term's image in the region when the term decides it alone: a variable's
 * cell, or a term the import keeps or copies whole.  NULL with `*descend`
 * set asks for an expression's image from its elements' images; NULL
 * otherwise is a failure. */
static Atom *oem_import_node(CettaOpenEquationCursor *cursor,
                             OemImport *import, Atom *atom,
                             bool *descend) {
    *descend = false;
    switch (import->kind) {
    case OEM_IMPORT_CALL:
        if (atom->kind == ATOM_VAR) {
            uint32_t position = cetta_var_index_find_atom(
                &cursor->query_index, cursor->query_vars,
                cursor->query_var_count, atom->var_id);
            return position != UINT32_MAX
                ? cursor->query_cells[position] : NULL;
        }
        break;
    case OEM_IMPORT_ROW:
        if (atom->kind == ATOM_VAR)
            return oem_fresh_var(cursor, atom->var_id);
        break;
    case OEM_IMPORT_COLLECTED:
        if (atom->arena_id != cursor->collected.identity)
            return atom;
        if (atom->kind == ATOM_VAR)
            return oem_fresh_var(cursor, atom->var_id);
        if (atom->kind != ATOM_EXPR || atom->expr.len == 0u)
            return oem_import_copy(cursor, import, atom);
        *descend = true;
        return NULL;
    case OEM_IMPORT_HOST:
        if (atom->kind == ATOM_VAR) {
            uint32_t position = cetta_var_index_find_atom(
                import->host_index, import->host_vars,
                import->cells->count, atom->var_id);
            if (position != UINT32_MAX)
                return cursor->cell_names[import->cells->cells[position]];
            position = cursor->query_vars
                ? cetta_var_index_find_atom(&cursor->query_index,
                                            cursor->query_vars,
                                            cursor->query_var_count,
                                            atom->var_id)
                : UINT32_MAX;
            if (position != UINT32_MAX)
                return cursor->query_cells[position];
            return oem_fresh_var(cursor, atom->var_id);
        }
        if (atom->kind != ATOM_EXPR || !atom_has_vars(atom))
            return oem_import_copy(cursor, import, atom);
        *descend = true;
        return NULL;
    }
    if (atom->kind != ATOM_EXPR || !atom_has_vars(atom))
        return atom;
    *descend = true;
    return NULL;
}

/* A pending expression of an import and the images of its elements so far. */
typedef struct {
    Atom *atom;
    Atom **children;
    CettaExprIndex next;
} OemImportFrame;

/* Left-to-right explicit-stack import. Completed node images are reused
 * only in this episode's fixed mode and variable context. Active-node hits
 * reject cyclic physical graphs; binding cycles remain variable references.
 * No arena reset or collection occurs during an episode (GraphImport). */
static Atom *oem_import_walk(CettaOpenEquationCursor *cursor,
                             OemImport *import, Atom *root) {
    if (!root)
        return NULL;
    OemAtomMapEntry *known = oem_atom_map_find(&import->images, root);
    if (known)
        return known->image;
    bool descend = false;
    Atom *image = oem_import_node(cursor, import, root, &descend);
    if (!descend)
        return image;
    OemImportFrame inline_frames[16];
    OemImportFrame *frames = inline_frames;
    uint32_t len = 0u;
    uint32_t cap = 16u;
    Atom *result = NULL;
    Atom *atom = root;
    for (;;) {
        if (len == cap) {
            if (cap > UINT32_MAX / 2u ||
                (size_t)cap > SIZE_MAX / (2u * sizeof(*frames)))
                goto done;
            OemImportFrame *grown = malloc(sizeof(*grown) * (size_t)cap * 2u);
            if (!grown)
                goto done;
            memcpy(grown, frames, sizeof(*frames) * (size_t)len);
            if (frames != inline_frames)
                free(frames);
            frames = grown;
            cap *= 2u;
        }
        if (!cetta_expr_len_mul_fits_size(atom->expr.len, sizeof(Atom *)) ||
            !oem_atom_map_begin(&import->images, atom))
            goto done;
        Atom **children = arena_alloc(&cursor->region,
                                      sizeof(*children) * atom->expr.len);
        if (!children)
            goto done;
        frames[len++] = (OemImportFrame){atom, children, 0u};
        for (;;) {
            OemImportFrame *top = &frames[len - 1u];
            if (top->next == top->atom->expr.len) {
                image = atom_expr(&cursor->region, top->children,
                                  top->atom->expr.len);
                if (!image)
                    goto done;
                oem_atom_map_find(&import->images, top->atom)->image = image;
                len--;
                if (len == 0u) {
                    result = image;
                    goto done;
                }
                top = &frames[len - 1u];
                top->children[top->next++] = image;
                continue;
            }
            Atom *child = top->atom->expr.elems[top->next];
            known = oem_atom_map_find(&import->images, child);
            if (known) {
                if (!known->image)
                    goto done;
                top->children[top->next++] = known->image;
                continue;
            }
            image = oem_import_node(cursor, import, child, &descend);
            if (descend) {
                atom = child;
                break;
            }
            if (!image)
                goto done;
            top->children[top->next++] = image;
        }
    }
done:
    if (frames != inline_frames)
        free(frames);
    return result;
}

/* Check each represented node once. The list tag is accepted only as
 * the head of a complete internal cons carrier. Every other bare or
 * embedded internal tag remains outside the region's interpretation. */
bool cetta_open_equation_term_supported(Atom *root) {
    if (!root)
        return false;
    if (!atom_structural_may_have_internal_tag(root))
        return true;
    Atom *inline_stack[32];
    Atom **stack = inline_stack;
    size_t len = 0u, capacity = 32u;
    OemAtomMap visited;
    oem_atom_map_init(&visited);
    bool supported = true;
    stack[len++] = root;
    while (len && supported) {
        Atom *atom = stack[--len];
        if (!atom_structural_may_have_internal_tag(atom) ||
            oem_atom_map_find(&visited, atom))
            continue;
        if (atom->kind == ATOM_GROUNDED &&
            atom->ground.gkind == GV_INTERNAL_TAG) {
            supported = false;
            break;
        }
        if (atom->kind != ATOM_EXPR)
            continue;
        if (!oem_atom_map_begin(&visited, atom)) {
            supported = false;
            break;
        }
        if (!cetta_expr_len_mul_fits_size(atom->expr.len, sizeof(*stack)) ||
            (size_t)atom->expr.len > SIZE_MAX - len) {
            supported = false;
            break;
        }
        size_t needed = len + (size_t)atom->expr.len;
        if (needed > capacity) {
            size_t grown_capacity = capacity;
            while (grown_capacity < needed && grown_capacity <= SIZE_MAX / 2u)
                grown_capacity *= 2u;
            if (grown_capacity < needed ||
                grown_capacity > SIZE_MAX / sizeof(*stack)) {
                supported = false;
                break;
            }
            Atom **grown = malloc(grown_capacity * sizeof(*grown));
            if (!grown) {
                supported = false;
                break;
            }
            memcpy(grown, stack, len * sizeof(*stack));
            if (stack != inline_stack)
                free(stack);
            stack = grown;
            capacity = grown_capacity;
        }
        CettaExprIndex first = petta_semantics_is_open_cons_value(atom) ? 1u : 0u;
        for (CettaExprIndex child = atom->expr.len; child-- > first;)
            stack[len++] = atom->expr.elems[child];
    }
    if (stack != inline_stack)
        free(stack);
    oem_atom_map_free(&visited);
    return supported;
}

/* A collected answer in the region: each of its variables becomes a fresh
 * cell, one per variable of the answer, and what the collection copied is
 * copied again, since its storage is about to be reused. */
static Atom *oem_import_collected(CettaOpenEquationCursor *cursor,
                                  Atom *atom) {
    OemImport import;
    oem_import_init(&import, OEM_IMPORT_COLLECTED);
    Atom *result = oem_import_walk(cursor, &import, atom);
    oem_import_free(&import);
    return result;
}

/* A completed collection's list, its answers in the order they came, built
 * in the region; the collection's storage returns to its mark. */
static Atom *oem_collected_list(CettaOpenEquationCursor *cursor,
                                OemCollectBox *box) {
    uint32_t count = box->count;
    Atom **items = arena_alloc(&cursor->region,
                               sizeof(*items) * (count ? count : 1u));
    uint32_t index = count;
    for (OemCollectItem *item = box->last; items && item;
         item = item->prev) {
        cursor->fresh_var_len = 0u;
        items[--index] = oem_import_collected(cursor, item->value);
        if (!items[index])
            items = NULL;
    }
    Atom *list = items ? atom_expr(&cursor->region, items, count) : NULL;
    arena_reset(&cursor->collected, box->mark);
    return list;
}

/* `superpose` over a list value: member/2 of the list, each element an
 * alternative as it stands.  A value that is no proper list stays the
 * host's. */
static __attribute__((noinline)) OemRun oem_elements_step(
    CettaOpenEquationCursor *cursor,
                                const CettaOpenEquationProgram *program,
                                uint32_t pc, Atom **locals,
                                uint32_t local_count, const OemCont *cont,
                                uint32_t depth, const OemStep *step) {
    const OemTemplate *goal = &program->templates[step->value];
    if (goal->kind != OEM_T_BUILD || goal->count != 2u)
        return OEM_RUN_HOST;
    Atom *list = oem_instantiate(cursor, program, locals,
                                 program->template_children[goal->first + 1u]);
    if (!list)
        return OEM_RUN_HANDOFF;
    list = oem_deref(cursor, list);
    if (list->kind != ATOM_EXPR || petta_semantics_is_open_cons_value(list))
        return OEM_RUN_HOST;
    if (list->expr.len == 0u)
        return OEM_RUN_FAILED;
    Atom *hole = oem_instantiate(cursor, program, locals, step->pattern);
    Atom **args = arena_alloc(&cursor->region, sizeof(*args) * 2u);
    OemCont *resume = arena_alloc(&cursor->region, sizeof(*resume));
    if (!hole || !args || !resume)
        return OEM_RUN_HANDOFF;
    args[0] = hole;
    args[1] = list;
    *resume = (OemCont){
        .parent = cont, .program = program, .locals = locals,
        .local_count = local_count, .pc = pc + 1u, .depth = depth,
    };
    return oem_push_frame(cursor, program, OEM_ELEMENTS_FRAME, args, resume,
                          depth)
        ? OEM_RUN_CALLED : OEM_RUN_HANDOFF;
}

static OemRun oem_enter(CettaOpenEquationCursor *cursor,
                        const CettaOpenEquationProgram *version,
                        uint32_t relation_index, Atom **args,
                        const OemCont *cont, uint32_t depth,
                        OemEntry *entry);

/* Whether the cursor's loop has work due before it enters another frame:
 * the activation budget is spent, an interrupt poll is next, or the region
 * has grown to its collection threshold.  A call then goes through a frame,
 * and the loop does that work first. */
static inline bool oem_loop_due(const CettaOpenEquationCursor *cursor) {
    return (cursor->runtime.activation_budget &&
            cursor->stats.activations >=
                cursor->runtime.activation_budget) ||
        (cursor->runtime.interrupt && cursor->poll >= 255u) ||
        arena_accounted_live_bytes(&cursor->region) >= cursor->collect_after;
}

static OemRun oem_run_body(CettaOpenEquationCursor *cursor,
                           const CettaOpenEquationProgram *program,
                           uint32_t pc, Atom **locals, uint32_t local_count,
                           const OemCont *cont, uint32_t depth,
                           Atom **value_out) {
    for (;;) {
        if (pc >= program->step_len)
            return OEM_RUN_HANDOFF;
        const OemStep *step = &program->steps[pc];
        switch ((OemStepKind)step->kind) {
        case OEM_S_RET:
            /* The destination already holds the output, unless the caller
             * stores it: then it goes to the caller's slot. */
            if (!cont)
                return oem_publish(cursor, cursor->destination, value_out);
            if (cont->store) {
                /* When the caller's slot holds the cell a chain of last
                 * calls filled, the value it holds replaces it, so the
                 * caller reads no chain of bindings. */
                uint32_t slot = cont->program->templates[cont->pattern].slot;
                Atom *held = cont->locals[slot];
                Atom *output = held ? held
                                    : oem_instantiate(cursor, program, locals,
                                                      step->value);
                Atom *value = output ? oem_deref(cursor, output) : NULL;
                if (!value ||
                    (value != held &&
                     !oem_store_slot(cursor, cont->locals, slot, value)))
                    return OEM_RUN_HANDOFF;
            }
            program = cont->program;
            locals = cont->locals;
            local_count = cont->local_count;
            pc = cont->pc;
            depth = cont->depth;
            cont = cont->parent;
            continue;

        case OEM_S_FAIL:
            return OEM_RUN_FAILED;
        case OEM_S_CALL:
        case OEM_S_TAIL: {
            /* The arguments go where the callee's head reads them: the
             * first registers.  A frame that keeps alternatives holds a copy
             * in the region. */
            uint32_t arg_count = step->arg_count;
            uint32_t arg_need = arg_count ? arg_count : 1u;
            if (arg_need > cursor->reg_cap &&
                !oem_reserve((void **)&cursor->regs, &cursor->reg_cap,
                             arg_need, sizeof(*cursor->regs)))
                return OEM_RUN_HANDOFF;
            Atom **args = cursor->regs;
            for (uint32_t index = 0u; index < arg_count; index++) {
                args[index] = oem_instantiate(cursor, program, locals,
                    program->step_args[step->first_arg + index]);
                if (!args[index])
                    return OEM_RUN_HANDOFF;
            }
            const OemCont *next = cont;
            if (step->kind == OEM_S_CALL) {
                OemCont *record = oem_region_alloc(cursor, sizeof(*record));
                if (!record)
                    return OEM_RUN_HANDOFF;
                *record = (OemCont){
                    .parent = cont, .program = program, .locals = locals,
                    .local_count = local_count, .pc = pc + 1u,
                    .pattern = step->pattern, .depth = depth,
                    .recovers = step->recovers, .store = step->store,
                };
                if (step->recovers)
                    cursor->recovering = true;
                next = record;
            }
            /* A call enters the equations current at its entry: the running
             * code's own, unless something changed since that code was
             * entered, then the host's current program for the relation.  A
             * local relation belongs to the code that calls it, and runs in
             * that version; the named relations it calls are entered
             * current. */
            const CettaOpenEquationProgram *target = program;
            uint32_t relation = step->relation;
            if (!oem_version_current(cursor, program) &&
                !program->relations[relation].local) {
                const OemRelation *callee = &program->relations[relation];
                target = oem_enter_current(cursor, program->space,
                                           callee->head, callee->arity);
                if (!target) {
                    cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_UNSUPPORTED;
                    return OEM_RUN_HANDOFF;
                }
                relation = 0u;
            }
            /* The call selects its equation and enters it here, unless the
             * loop has work due first; then a frame holds the call. */
            if (!oem_loop_due(cursor)) {
                if (cursor->runtime.interrupt)
                    cursor->poll++;
                OemEntry callee;
                OemRun run = oem_enter(cursor, target, relation, args, next,
                                       depth + 1u, &callee);
                if (run != OEM_RUN_CALLED)
                    return run;
                program = callee.program;
                pc = callee.pc;
                locals = callee.locals;
                local_count = callee.local_count;
                cont = callee.cont;
                depth = callee.depth;
                continue;
            }
            Atom **held = oem_region_alloc(cursor, sizeof(*held) * arg_need);
            if (!held)
                return OEM_RUN_HANDOFF;
            memcpy(held, args, sizeof(*held) * arg_count);
            return oem_push_frame(cursor, target, relation, held, next,
                                  depth + 1u)
                ? OEM_RUN_CALLED : OEM_RUN_HANDOFF;
        }
        case OEM_S_PRIM:
        case OEM_S_TEST: {
            if (step->kind == OEM_S_TEST && step->op == SYMBOL_ID_NONE) {
                Atom *condition = oem_instantiate(
                    cursor, program, locals,
                    program->step_args[step->first_arg]);
                if (!condition)
                    return OEM_RUN_HANDOFF;
                bool truth = false;
                pc = petta_semantics_truth_value(
                         oem_deref(cursor, condition), &truth) && truth
                    ? pc + 1u : step->target;
                continue;
            }
            int64_t a = 0;
            int64_t b = 0;
            if (step->arg_count != 2u)
                return OEM_RUN_HANDOFF;
            Atom *left = oem_instantiate(cursor, program, locals,
                                         program->step_args[step->first_arg]);
            Atom *right = oem_instantiate(
                cursor, program, locals,
                program->step_args[step->first_arg + 1u]);
            if (!left || !right)
                return OEM_RUN_HANDOFF;
            left = oem_deref(cursor, left);
            right = oem_deref(cursor, right);
            bool ints = oem_int_value(left, &a) && oem_int_value(right, &b);
            if (ints && step->kind == OEM_S_TEST) {
                pc = oem_test(step->op, a, b) ? pc + 1u : step->target;
                continue;
            }
            int64_t result = 0;
            Atom *value = NULL;
            if (ints && oem_prim(step->op, a, b, &result)) {
                value = atom_int(&cursor->region, result);
                if (!value)
                    return OEM_RUN_HANDOFF;
            } else {
                bool truth = false;
                OemRun run = oem_host_arithmetic(cursor, program, step,
                                                 left, right,
                                                 &value, &truth, value_out);
                if (run != OEM_RUN_CALLED)
                    return oem_raised(cursor, cont, run);
                if (step->kind == OEM_S_TEST) {
                    pc = truth ? pc + 1u : step->target;
                    continue;
                }
            }
            OemUnify unified = oem_meet(cursor, program, locals, step, value);
            if (unified != OEM_UNIFY_OK)
                return unified == OEM_UNIFY_FAIL ? OEM_RUN_FAILED
                                                 : OEM_RUN_HANDOFF;
            pc++;
            continue;
        }
        case OEM_S_MATCH: {
            OemRun run = oem_match_step(cursor, program, pc, locals,
                                        local_count, cont, depth, step);
            if (run != OEM_RUN_FOLDED)
                return run;
            pc++;
            continue;
        }
        case OEM_S_HOST:
            if ((step->target == OEM_HOST_COLLECT ||
                 step->target == OEM_HOST_ALTERNATIVES ||
                 step->target == OEM_HOST_ONCE) &&
                !program->relations[
                    program->controls[step->control].local].failed)
                return oem_local_step(cursor, program, pc, locals,
                                      local_count, cont, depth, step);
            if (step->target == OEM_HOST_ELEMENTS) {
                OemRun run = oem_elements_step(cursor, program, pc, locals,
                                               local_count, cont, depth,
                                               step);
                if (run != OEM_RUN_HOST)
                    return run;
            }
            if (step->target >= OEM_HOST_PURE) {
                OemRun run =
                    step->target == OEM_HOST_APPLY ||
                            step->target == OEM_HOST_DATA_HEAD
                        ? oem_apply_step(cursor, program, pc, locals,
                                         local_count, cont, depth, step)
                        : oem_fast_host_step(cursor, program, locals, step,
                                             value_out);
                if (run == OEM_RUN_CALLED) {
                    pc++;
                    continue;
                }
                if (run == OEM_RUN_ENTERED)
                    return OEM_RUN_CALLED;
                if (run != OEM_RUN_HOST)
                    return oem_raised(cursor, cont, run);
                /* The host fills a slot this step would have stored: it
                 * takes a cell here, at its first occurrence. */
                if (step->store) {
                    Atom *cell = oem_new_cell(cursor);
                    if (!cell ||
                        !oem_store_slot(cursor, locals,
                                        program->templates[step->pattern].slot,
                                        cell))
                        return OEM_RUN_HANDOFF;
                }
            }
            return oem_host_step(cursor, program, pc, locals, local_count,
                                 cont, depth, step);
        case OEM_S_YIELD:
            return oem_yield_step(cursor, program, locals, step);
        case OEM_S_CUT: {
            OemRun run = oem_cut_step(cursor, program, locals, local_count,
                                      cont, depth, step);
            if (run != OEM_RUN_CALLED)
                return run;
            pc = step->target;
            continue;
        }
        case OEM_S_EQUATION_CUT: {
            OemRun run = oem_equation_cut_step(cursor, program, pc, locals,
                                               local_count, cont, depth,
                                               step);
            if (run != OEM_RUN_CALLED)
                return run;
            pc++;
            continue;
        }
        case OEM_S_CASE: {
            /* A trial frame makes each binding of the attempt undoable; it
             * lives for this step alone. */
            Atom *key = oem_instantiate(cursor, program, locals, step->value);
            if (!key ||
                !oem_push_frame(cursor, program, OEM_RESUME_FRAME, NULL,
                                NULL, depth))
                return OEM_RUN_HANDOFF;
            OemUnify unified = oem_unify_template(cursor, program, locals,
                                                  step->pattern, key, 0u);
            if (unified == OEM_UNIFY_FAIL)
                oem_restore(cursor, &cursor->frames[cursor->frame_len - 1u]);
            oem_pop_frame(cursor);
            if (unified == OEM_UNIFY_ERROR)
                return OEM_RUN_HANDOFF;
            pc = unified == OEM_UNIFY_OK ? pc + 1u : step->target;
            continue;
        }
        case OEM_S_BIND: {
            Atom *value = oem_instantiate(cursor, program, locals, step->value);
            if (!value)
                return OEM_RUN_HANDOFF;
            OemUnify unified = oem_meet(cursor, program, locals, step, value);
            if (unified != OEM_UNIFY_OK)
                return unified == OEM_UNIFY_FAIL ? OEM_RUN_FAILED
                                                 : OEM_RUN_HANDOFF;
            pc++;
            continue;
        }
        }
        return OEM_RUN_HANDOFF;
    }
}

/* Activate equation `equation` on `args`: run its head program, then its
 * body. */
/* One activation is one unification attempt of the equation's head, which
 * the destination extends, classified as equation search classifies one:
 * success, a repeated variable's values differing, or a mismatch before or
 * after a variable of this attempt was bound. */
typedef enum {
    OEM_ATTEMPT_SUCCESS = 0,
    OEM_ATTEMPT_REPEATED_VARIABLE,
    OEM_ATTEMPT_MISMATCH,
} OemAttempt;

static OemRun oem_count_attempt(const CettaOpenEquationCursor *cursor,
                                OemAttempt attempt, uint64_t binds_at_start,
                                OemRun run) {
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_UNIFICATION_ATTEMPT);
    cetta_runtime_stats_inc(
        attempt == OEM_ATTEMPT_SUCCESS
            ? CETTA_RUNTIME_COUNTER_UNIFICATION_ATTEMPT_SUCCESS
            : attempt == OEM_ATTEMPT_REPEATED_VARIABLE
            ? CETTA_RUNTIME_COUNTER_UNIFICATION_ATTEMPT_REPEATED_VAR_FAIL
            : cursor->binds != binds_at_start
            ? CETTA_RUNTIME_COUNTER_UNIFICATION_ATTEMPT_AFTER_BIND_FAIL
            : CETTA_RUNTIME_COUNTER_UNIFICATION_ATTEMPT_HEAD_FAIL);
    return run;
}

/* Equation selection.  The head's structural tests run against the call's
 * arguments without binding: an unbound cell, or a subterm below one,
 * passes every test, and a repeated variable is not compared.  So an
 * equation it refutes is one whose activation fails at a constructor, and
 * it never refutes an equation that can match.  The frame's bindings are
 * those every later resumption restores, so the result stays valid for the
 * frame's lifetime. */
static bool oem_may_match(CettaOpenEquationCursor *cursor,
                          const CettaOpenEquationProgram *program,
                          uint32_t equation, Atom **args, uint32_t arity,
                          bool *error) {
    const OemEquation *eq = &program->equations[equation];
    uint32_t count = eq->register_count ? eq->register_count : 1u;
    if (!oem_reserve((void **)&cursor->probe, &cursor->probe_cap, count,
                     sizeof(*cursor->probe))) {
        *error = true;
        return true;
    }
    Atom **regs = cursor->probe;
    for (uint32_t index = 0u; index < eq->register_count; index++)
        regs[index] = index < arity ? args[index] : NULL;
    for (uint32_t op_index = 0u; op_index < eq->op_count; op_index++) {
        const OemMatchOp *op = &program->ops[eq->first_op + op_index];
        if (op->kind != OEM_M_ATOM && op->kind != OEM_M_EXPR &&
            op->kind != OEM_M_LIST)
            continue;
        Atom *term = regs[op->reg];
        uint32_t cell = 0u;
        if (term)
            term = oem_deref(cursor, term);
        bool open = !term || oem_is_cell(cursor, term, &cell) ||
            oem_is_list_cell(term);
        if (op->kind == OEM_M_ATOM) {
            if (!open && !atom_eq(term, op->literal))
                return false;
            continue;
        }
        if (op->kind == OEM_M_LIST) {
            /* A nonempty list; its rest is left open, and so is the whole
             * of a cell the operation reads as spelled. */
            if (!open && (term->kind != ATOM_EXPR || term->expr.len == 0u))
                return false;
            regs[op->operand] =
                open || (op->spelled &&
                         petta_semantics_is_cons_constraint(term))
                    ? NULL : term->expr.elems[0];
            regs[op->operand + 1u] = NULL;
            continue;
        }
        if (!open && (term->kind != ATOM_EXPR ||
                      term->expr.len != op->length))
            return false;
        for (uint32_t child = 0u; child < op->length; child++)
            regs[op->operand + child] = open ? NULL : term->expr.elems[child];
    }
    return true;
}

/* Empty a new slot vector.  Most are short, and stores cost less than a
 * call to memset. */
static inline void oem_clear_slots(Atom **slots, uint32_t count) {
    switch (count) {
    case 8u: slots[7] = NULL; /* fallthrough */
    case 7u: slots[6] = NULL; /* fallthrough */
    case 6u: slots[5] = NULL; /* fallthrough */
    case 5u: slots[4] = NULL; /* fallthrough */
    case 4u: slots[3] = NULL; /* fallthrough */
    case 3u: slots[2] = NULL; /* fallthrough */
    case 2u: slots[1] = NULL; /* fallthrough */
    case 1u: slots[0] = NULL; /* fallthrough */
    case 0u: return;
    default: memset(slots, 0, sizeof(*slots) * count); return;
    }
}

/* Equations selection passes over, each counted as a failed head, as its
 * activation would count. */
static inline void oem_count_passed_over(CettaOpenEquationCursor *cursor,
                                         uint32_t count) {
    cursor->stats.head_failures += count;
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_UNIFICATION_ATTEMPT, count);
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_UNIFICATION_ATTEMPT_HEAD_FAIL, count);
}

static inline __attribute__((always_inline)) OemRun oem_activate(
    CettaOpenEquationCursor *cursor, const CettaOpenEquationProgram *program,
    uint32_t equation, Atom **args, uint32_t arity, const OemCont *cont,
    bool own_frame, OemEntry *entry) {
    const OemEquation *eq = &program->equations[equation];
    /* A slot vector is preceded by the number of frames live when its
     * activation began, which tells a store whether a later frame could
     * resume the activation's code before the store, less its call's own
     * frame when the equation cuts, which tells the cut the height beneath
     * the call, and by the collection epoch, which tells whether the vector
     * is old. */
    uintptr_t *header = oem_region_alloc(
        cursor,
        sizeof(*header) + sizeof(Atom *) * (eq->slot_count ? eq->slot_count
                                                          : 1u));
    if (!header)
        return OEM_RUN_HANDOFF;
    header[0] = oem_slot_header(
        cursor, cursor->frame_len - (uint32_t)(own_frame & eq->cuts));
    Atom **locals = (Atom **)(header + 1);
    /* A call entered from its caller's body put its arguments in the
     * registers; growing them keeps their contents. */
    bool in_registers = args == cursor->regs;
    uint32_t register_need = eq->register_count ? eq->register_count : 1u;
    if (register_need > cursor->reg_cap &&
        !oem_reserve((void **)&cursor->regs, &cursor->reg_cap,
                     register_need, sizeof(*cursor->regs)))
        return OEM_RUN_HANDOFF;
    Atom **regs = cursor->regs;
    oem_clear_slots(locals, eq->slot_count);
    if (!in_registers) {
        for (uint32_t index = 0u; index < arity; index++)
            regs[index] = args[index];
    }
    uint64_t binds_at_start = cursor->binds;
    for (uint32_t op_index = 0u; op_index < eq->op_count; op_index++) {
        const OemMatchOp *op = &program->ops[eq->first_op + op_index];
        switch ((OemMatchKind)op->kind) {
        case OEM_M_BIND:
            locals[op->operand] = regs[op->reg];
            break;
        case OEM_M_SAME: {
            OemUnify unified = oem_unify(cursor, regs[op->reg],
                                         locals[op->operand]);
            if (unified == OEM_UNIFY_ERROR)
                return OEM_RUN_HANDOFF;
            if (unified == OEM_UNIFY_FAIL) {
                cursor->stats.head_failures++;
                return oem_count_attempt(cursor, OEM_ATTEMPT_REPEATED_VARIABLE,
                                         binds_at_start, OEM_RUN_FAILED);
            }
            break;
        }
        case OEM_M_ATOM: {
            Atom *term = oem_deref(cursor, regs[op->reg]);
            uint32_t cell = 0u;
            if (oem_is_cell(cursor, term, &cell)) {
                if (!oem_bind(cursor, cell, op->literal))
                    return OEM_RUN_HANDOFF;
            } else if (oem_is_list_cell(term)) {
                OemUnify unified = oem_unify(cursor, op->literal, term);
                if (unified == OEM_UNIFY_ERROR)
                    return OEM_RUN_HANDOFF;
                if (unified == OEM_UNIFY_FAIL) {
                    cursor->stats.head_failures++;
                    return oem_count_attempt(cursor, OEM_ATTEMPT_MISMATCH,
                                             binds_at_start, OEM_RUN_FAILED);
                }
            } else if (!atom_eq(term, op->literal)) {
                cursor->stats.head_failures++;
                return oem_count_attempt(cursor, OEM_ATTEMPT_MISMATCH,
                                         binds_at_start, OEM_RUN_FAILED);
            }
            break;
        }
        case OEM_M_EXPR: {
            Atom *term = oem_deref(cursor, regs[op->reg]);
            uint32_t cell = 0u;
            if (oem_is_cell(cursor, term, &cell)) {
                /* The call leaves this position open: it takes a node of
                 * fresh cells, which the head's later operations fill. */
                Atom **children = arena_alloc(
                    &cursor->region, sizeof(*children) * op->length);
                if (!children)
                    return OEM_RUN_HANDOFF;
                for (uint32_t child = 0u; child < op->length; child++) {
                    children[child] = oem_new_cell(cursor);
                    if (!children[child])
                        return OEM_RUN_HANDOFF;
                    regs[op->operand + child] = children[child];
                }
                Atom *node = atom_expr(&cursor->region, children,
                                       op->length);
                if (!node || !oem_bind(cursor, cell, node))
                    return OEM_RUN_HANDOFF;
            } else if (oem_is_list_cell(term)) {
                /* A cons cell meets the head's shape by its elements: a
                 * node of fresh cells unified with it takes them, and an
                 * unbound tail of the cell is bound to the node's rest. */
                Atom **children = arena_alloc(
                    &cursor->region, sizeof(*children) * op->length);
                if (!children)
                    return OEM_RUN_HANDOFF;
                for (uint32_t child = 0u; child < op->length; child++) {
                    children[child] = oem_new_cell(cursor);
                    if (!children[child])
                        return OEM_RUN_HANDOFF;
                    regs[op->operand + child] = children[child];
                }
                Atom *node = atom_expr(&cursor->region, children,
                                       op->length);
                OemUnify unified = node
                    ? oem_unify(cursor, node, term) : OEM_UNIFY_ERROR;
                if (unified == OEM_UNIFY_ERROR)
                    return OEM_RUN_HANDOFF;
                if (unified == OEM_UNIFY_FAIL) {
                    cursor->stats.head_failures++;
                    return oem_count_attempt(cursor, OEM_ATTEMPT_MISMATCH,
                                             binds_at_start, OEM_RUN_FAILED);
                }
            } else if (term->kind == ATOM_EXPR &&
                       term->expr.len == op->length) {
                for (uint32_t child = 0u; child < op->length; child++)
                    regs[op->operand + child] = term->expr.elems[child];
            } else {
                cursor->stats.head_failures++;
                return oem_count_attempt(cursor, OEM_ATTEMPT_MISMATCH,
                                         binds_at_start, OEM_RUN_FAILED);
            }
            break;
        }
        case OEM_M_LIST: {
            Atom *term = oem_deref(cursor, regs[op->reg]);
            uint32_t cell = 0u;
            if (oem_is_cell(cursor, term, &cell)) {
                /* An open position takes a cons cell of two fresh cells,
                 * which the head's later operations fill. */
                Atom *first = oem_new_cell(cursor);
                Atom *rest = first ? oem_new_cell(cursor) : NULL;
                Atom *node = rest
                    ? petta_semantics_open_cons_value(&cursor->region, first,
                                                      rest)
                    : NULL;
                if (!node || !oem_bind(cursor, cell, node))
                    return OEM_RUN_HANDOFF;
                regs[op->operand] = first;
                regs[op->operand + 1u] = rest;
            } else if (oem_is_list_cell(term) ||
                       (op->spelled &&
                        petta_semantics_is_cons_constraint(term))) {
                regs[op->operand] = term->expr.elems[1];
                regs[op->operand + 1u] = term->expr.elems[2];
            } else if (term->kind == ATOM_EXPR && term->expr.len > 0u) {
                /* A flat list's rest is its suffix, sharing its storage. */
                Atom *rest = atom_expr_suffix(&cursor->region, term, 1u);
                if (!rest)
                    return OEM_RUN_HANDOFF;
                regs[op->operand] = term->expr.elems[0];
                regs[op->operand + 1u] = rest;
            } else {
                cursor->stats.head_failures++;
                return oem_count_attempt(cursor, OEM_ATTEMPT_MISMATCH,
                                         binds_at_start, OEM_RUN_FAILED);
            }
            break;
        }
        }
    }
    Atom *destination = NULL;
    bool constrain = false;
    /* A caller that stores the value it gets back has no destination for
     * the equation to meet, and its return stores the output.  But an
     * output slot head matching left unset needs one to fill: the caller's
     * slot takes a cell, which the equations of a chain of last calls
     * then share, as they share a destination. */
    bool meets = eq->dest_kind != OEM_DEST_NONE;
    if (meets && cont && cont->store) {
        uint32_t slot = cont->program->templates[cont->pattern].slot;
        destination = cont->locals[slot];
        if (!destination && eq->dest_kind == OEM_DEST_SLOT &&
            !locals[eq->destination]) {
            destination = oem_new_cell(cursor);
            if (!destination ||
                !oem_store_slot(cursor, cont->locals, slot, destination))
                return OEM_RUN_HANDOFF;
        }
        meets = destination != NULL;
    } else if (meets) {
        destination = cont
            ? oem_instantiate(cursor, cont->program, cont->locals, cont->pattern)
            : cursor->destination;
        if (!destination)
            return OEM_RUN_HANDOFF;
    }
    if (meets) {
        /* An output slot that head matching left unset is the destination
         * itself: nothing to unify, except the output the body fixes. */
        if (eq->dest_kind == OEM_DEST_SLOT && !locals[eq->destination]) {
            locals[eq->destination] = destination;
            destination = NULL;
            constrain = eq->output_kind != OEM_OUTPUT_OPEN &&
                cursor->runtime.source_output_constraints;
        }
    }
    /* A slot the body stores at its first occurrence waits for that store,
     * and a slot the body never mentions stays empty; every other slot head
     * matching left unset is a cell (the equation's `cell_slots`). */
    const uint32_t *cell_slots = &program->cell_slots[eq->first_cell_slot];
    for (uint32_t index = 0u; index < eq->cell_slot_count; index++) {
        uint32_t slot = cell_slots[index];
        if (!locals[slot]) {
            locals[slot] = oem_new_cell(cursor);
            if (!locals[slot])
                return OEM_RUN_HANDOFF;
        }
    }
    if (destination) {
        OemUnify unified = eq->dest_kind == OEM_DEST_SLOT
            ? oem_unify(cursor, locals[eq->destination], destination)
            : oem_unify_template(cursor, program, locals, eq->destination,
                                 destination, 0u);
        if (unified == OEM_UNIFY_ERROR)
            return OEM_RUN_HANDOFF;
        if (unified == OEM_UNIFY_FAIL) {
            cursor->stats.head_failures++;
            return oem_count_attempt(cursor, OEM_ATTEMPT_MISMATCH,
                                     binds_at_start, OEM_RUN_FAILED);
        }
    }
    /* The host's search constrains the destination by that output before
     * the head's relational occurrences or the body run their effects. */
    if (constrain) {
        Atom *truth = eq->output_kind == OEM_OUTPUT_TRUE
            ? oem_truth(cursor, true) : NULL;
        OemUnify unified = eq->output_kind == OEM_OUTPUT_TRUE
            ? (truth ? oem_unify(cursor, truth, locals[eq->destination])
                     : OEM_UNIFY_ERROR)
            : oem_unify_template(cursor, program, locals, eq->output,
                                 locals[eq->destination], 0u);
        if (unified == OEM_UNIFY_ERROR)
            return OEM_RUN_HANDOFF;
        if (unified == OEM_UNIFY_FAIL) {
            cursor->stats.head_failures++;
            return oem_count_attempt(cursor, OEM_ATTEMPT_MISMATCH,
                                     binds_at_start, OEM_RUN_FAILED);
        }
    }
    cursor->stats.activations++;
    *entry = (OemEntry){
        .program = program, .pc = eq->first_step, .locals = locals,
        .local_count = eq->slot_count, .cont = cont,
    };
    return oem_count_attempt(cursor, OEM_ATTEMPT_SUCCESS, binds_at_start,
                             OEM_RUN_CALLED);
}

/* The region takes the host's stable storage as its older generation once
 * that holds something to share: when the cursor opens, or after an export
 * first copies there.  Atoms made before keep their bits, which stay sound:
 * closure only grows with a generation. */
static void oem_link_generations(CettaOpenEquationCursor *cursor) {
    Arena *stable = cursor->runtime.stable;
    if (stable && cursor->region.older_identity == 0u &&
        arena_accounted_live_bytes(stable) != 0u)
        arena_set_older_generation(&cursor->region, stable);
}

/* ---- region collection --------------------------------------------------- */

/* A deterministic computation leaves the activation records and values of
 * finished activations in the region; only backtracking resets it, so a long
 * loop would keep them all.  Between steps every live region object is
 * reached from the frames (their arguments and continuations, whose slots
 * hold values), the bound cells and the destination.  A collection replaces
 * the region by a copy of what those reach, keeping sharing: a continuation,
 * a slot vector or a term reached twice is copied once.  Every frame's mark
 * becomes the new region's end.  Restoring a frame then discards what was
 * allocated after the collection; what it would have discarded before is
 * either still reached by a live object, and was copied, or unreachable once
 * the frame's bindings are undone. */

/* The region is the young generation.  A cursor's first collection waits
 * for this much, so a cursor that ends sooner, as most do, never pays for
 * one: its region is dropped whole. */
enum { OEM_COLLECT_MIN_BYTES = 4u * 1024u * 1024u };
/* The most a declined major's wait grows: four times the old generation
 * found live.  A generation that later dies waits at most that long. */
enum { OEM_MAJOR_DECLINE_SHIFT_MAX = 2u };
/* A cursor that has collected once is long-running.  Its next window is
 * 32 times what the collection promoted, between this floor and the first
 * window: collecting then costs about 1/32 of the allocation, and a region
 * whose data dies young keeps no more blocks than that needs.  A window
 * that low survival would shrink below the first window is not shrunk when
 * survivors are many, since a smaller window would promote data that is
 * about to die.  The floor bounds a collection's fixed cost. */
enum { OEM_COLLECT_STEADY_BYTES = 1u * 1024u * 1024u };
enum { OEM_COLLECT_SURVIVAL_RATIO = 32u };
/* The old generation's size below which it is not collected: a major
 * collection runs once promotion has doubled it since the last. */
enum { OEM_MAJOR_MIN_BYTES = 32u * 1024u * 1024u };

/* The first window, the steady floor and the major threshold in force.  A
 * test sets CETTA_OEM_COLLECT_STRESS to collect, minor and major, after
 * every few kilobytes, so that collections meet every state the mutator
 * makes between them; the values then are the stress sizes. */
typedef struct {
    size_t first;
    size_t steady;
    size_t major;
} OemCollectSizes;

static const OemCollectSizes *oem_collect_sizes(void) {
    static OemCollectSizes sizes;
    static bool ready;
    if (!ready) {
        const char *stress = getenv("CETTA_OEM_COLLECT_STRESS");
        sizes = stress && stress[0] && strcmp(stress, "0") != 0
            ? (OemCollectSizes){
                  .first = 16u * 1024u, .steady = 16u * 1024u,
                  .major = 64u * 1024u}
            : (OemCollectSizes){
                  .first = OEM_COLLECT_MIN_BYTES,
                  .steady = OEM_COLLECT_STEADY_BYTES,
                  .major = OEM_MAJOR_MIN_BYTES};
        ready = true;
    }
    return &sizes;
}
/* The bound of a single-generation region: past it the region is
 * collected whatever holds it. */
enum { OEM_REGION_BOUND_BYTES = 256u * 1024u * 1024u };

/* A block of the region, as the addresses it holds. */
typedef struct {
    uintptr_t start;
    uintptr_t end;
} OemSpan;

typedef struct {
    Arena *to;
    /* A minor collection moves only the region's objects; `young` lists
     * the region's blocks in address order, so a continuation or slot
     * vector the region holds is told from an older one. */
    bool minor;
    OemSpan *young;
    uint32_t young_len;
    /* In a major collection, the old generation's blocks, which it frees
     * too, in address order. */
    OemSpan *old_spans;
    uint32_t old_span_len;
    /* The span the last test found: objects reached together were mostly
     * allocated together. */
    OemSpan hit;
    /* The region's bytes the trace reached, and the bytes of the frames'
     * slot vectors and continuations it walked. */
    size_t reached_young;
    size_t walked;
    uint32_t from_identity;
    /* In a major collection, the old generation is collected too. */
    uint32_t from_old_identity;
    /* An arena about to release its atoms: a borrowed atom it owns is
     * copied, through `detach`, rather than shared (zero when none). */
    uint32_t detach_identity;
    AtomDeepCopySession *detach;
    const void **keys;
    void **values;
    /* The table's size when its first entry comes. */
    size_t first_cap;
    size_t cap;
    size_t len;
    /* Cells a live term mentions: `reached` marks them, `pending` lists the
     * bound ones whose values are still to be traced; `remap` is a reached
     * cell's index after compaction.  A minor collection keeps every cell
     * below `cell_base` at its index and traces none of them from a term
     * (their values change only through `oem_bind`, which remembers them),
     * so these tables cover the cells from `cell_base` on, and a cell below
     * it is never shunted. */
    uint32_t cell_base;
    uint8_t *reached;
    uint32_t *pending;
    uint32_t pending_len;
    uint32_t *remap;
    /* Cells whose binding restoring a live frame would undo: the cells the
     * trail names from the oldest frame's mark on. */
    uint8_t *revocable;
    /* The traced expressions, so a collection that would reclaim little
     * can clear its marks, and, in a major collection, the bytes of the
     * old generation it reached: its expressions, continuations and slot
     * vectors. */
    Atom **marked;
    uint32_t marked_len, marked_cap;
    size_t reached_old;
    bool record;
} OemCollect;

/* A table size, a power of two of at least 1024, that holds `hint`
 * entries at the table's load bound. */
static size_t oem_collect_table_size(size_t hint) {
    size_t cap = 1024u;
    while (cap < SIZE_MAX / 4u && (hint + 1u) * 2u > cap)
        cap *= 2u;
    return cap;
}

/* Fibonacci hashing: the product's top bits index a table of `cap`, a power
 * of two, so neighbouring addresses spread across it. */
static size_t oem_collect_hash(const void *key, size_t cap) {
    uint64_t h = (uint64_t)(uintptr_t)key * UINT64_C(0x9E3779B97F4A7C15);
    return (size_t)(h >> (64u - (unsigned)__builtin_ctzll((uint64_t)cap)));
}

static void *oem_collect_find(const OemCollect *collect, const void *key) {
    if (collect->cap == 0u)
        return NULL;
    for (size_t index = oem_collect_hash(key, collect->cap);;
         index = (index + 1u) & (collect->cap - 1u)) {
        if (!collect->keys[index])
            return NULL;
        if (collect->keys[index] == key)
            return collect->values[index];
    }
}

/* The value cell of `key`, or NULL when the table has none. */
static void **oem_collect_value_at(OemCollect *collect, const void *key) {
    if (collect->cap == 0u)
        return NULL;
    for (size_t index = oem_collect_hash(key, collect->cap);;
         index = (index + 1u) & (collect->cap - 1u)) {
        if (!collect->keys[index])
            return NULL;
        if (collect->keys[index] == key)
            return &collect->values[index];
    }
}

static bool oem_collect_put(OemCollect *collect, const void *key,
                            void *value) {
    if ((collect->len + 1u) * 2u > collect->cap) {
        size_t cap = collect->cap ? collect->cap * 2u
            : collect->first_cap ? collect->first_cap : 1024u;
        const void **keys = calloc(cap, sizeof(*keys));
        void **values = calloc(cap, sizeof(*values));
        if (!keys || !values) {
            free(keys);
            free(values);
            return false;
        }
        for (size_t index = 0u; index < collect->cap; index++) {
            if (!collect->keys[index])
                continue;
            size_t slot = oem_collect_hash(collect->keys[index], cap);
            while (keys[slot])
                slot = (slot + 1u) & (cap - 1u);
            keys[slot] = collect->keys[index];
            values[slot] = collect->values[index];
        }
        free(collect->keys);
        free(collect->values);
        collect->keys = keys;
        collect->values = values;
        collect->cap = cap;
    }
    size_t slot = oem_collect_hash(key, collect->cap);
    while (collect->keys[slot])
        slot = (slot + 1u) & (collect->cap - 1u);
    collect->keys[slot] = key;
    collect->values[slot] = value;
    collect->len++;
    return true;
}

/* Whether a collection copies `atom`: an atom of a generation it collects. */
static inline bool oem_collect_from(const OemCollect *collect,
                                    const Atom *atom) {
    return atom->arena_id == collect->from_identity ||
        (collect->from_old_identity != 0u &&
         atom->arena_id == collect->from_old_identity);
}

static int oem_span_order(const void *left, const void *right) {
    uintptr_t a = ((const OemSpan *)left)->start;
    uintptr_t b = ((const OemSpan *)right)->start;
    return a < b ? -1 : a > b;
}

/* An arena's used blocks, in address order. */
static bool oem_collect_spans(const Arena *arena, OemSpan **spans_out,
                              uint32_t *len_out) {
    uint32_t count = 0u;
    for (const ArenaBlock *block = arena->head; block; block = block->next)
        count++;
    OemSpan *spans = malloc(sizeof(*spans) * (count ? count : 1u));
    if (!spans)
        return false;
    uint32_t len = 0u;
    for (const ArenaBlock *block = arena->head; block; block = block->next) {
        if (block->used == 0u)
            continue;
        spans[len++] = (OemSpan){
            .start = (uintptr_t)block->data,
            .end = (uintptr_t)block->data + block->used,
        };
    }
    qsort(spans, len, sizeof(*spans), oem_span_order);
    *spans_out = spans;
    *len_out = len;
    return true;
}

static bool oem_span_holds(const OemSpan *spans, uint32_t len,
                           uintptr_t at) {
    uint32_t low = 0u;
    uint32_t high = len;
    while (low < high) {
        uint32_t mid = low + (high - low) / 2u;
        if (at < spans[mid].start)
            high = mid;
        else if (at >= spans[mid].end)
            low = mid + 1u;
        else
            return true;
    }
    return false;
}

/* Whether the region holds the continuation or slot vector at `ptr`. */
static bool oem_collect_in_region(OemCollect *collect, const void *ptr) {
    uintptr_t at = (uintptr_t)ptr;
    if (at >= collect->hit.start && at < collect->hit.end)
        return true;
    uint32_t low = 0u;
    uint32_t high = collect->young_len;
    while (low < high) {
        uint32_t mid = low + (high - low) / 2u;
        if (at < collect->young[mid].start)
            high = mid;
        else if (at >= collect->young[mid].end)
            low = mid + 1u;
        else {
            collect->hit = collect->young[mid];
            return true;
        }
    }
    return false;
}

/* Whether the collection frees the storage at `ptr`: the region's, and in a
 * major collection the old generation's. */
static bool oem_collect_frees(OemCollect *collect, const void *ptr) {
    return oem_collect_in_region(collect, ptr) ||
        oem_span_holds(collect->old_spans, collect->old_span_len,
                       (uintptr_t)ptr);
}

/* A view (an expression whose elements lie outside its header) over
 * storage the collection neither moves nor frees: the host's, or, in a
 * minor collection, the old generation's.  That storage is immutable and
 * older than everything the collection moves, so the young trace does not
 * enter it (RememberedSetTrace.minor_trace_complete: a minor collection
 * keeps every cell an old atom may name, and the host's atoms name none),
 * and the view is traced and copied as its header alone.  An arena being
 * detached may own the storage, so a detaching collection copies views as
 * it copies everything else. */
/* A view shorter than this is traced and copied whole: its cost is bounded
 * by this length, below what the storage table costs per view. */
enum { OEM_SHARED_VIEW_MIN = 16u };

/* A nonempty expression that is a view: its elements lie outside its
 * header, and it is at least OEM_SHARED_VIEW_MIN long. */
static inline bool oem_long_view(const Atom *atom) {
    return atom->expr.elems != (Atom **)(atom + 1) &&
        atom->expr.len >= OEM_SHARED_VIEW_MIN;
}

static inline bool oem_is_view(const Atom *atom) {
    return atom->kind == ATOM_EXPR && atom->expr.len > 0u &&
        oem_long_view(atom);
}

static inline __attribute__((always_inline)) bool oem_view_over_kept_storage(
    OemCollect *collect, const Atom *atom) {
    return oem_long_view(atom) && collect->detach_identity == 0u &&
        !oem_collect_frees(collect, atom->expr.elems);
}

/* The table key of the storage ending at `end`: a view's storage is told by
 * its end, which every suffix of it shares.  Objects key the table at their
 * aligned addresses and in-place renames at an address plus one, so an end
 * plus two meets neither. */
static inline const void *oem_storage_key(Atom *const *end) {
    return (const char *)end + 2;
}

/* Whether the collection moves the continuation or slot vector at `ptr`: a
 * major collection moves everything it reaches, a minor one only what the
 * region holds.  An older object stays where it is: it was made before every
 * region object and names none of them, except through a slot stored since
 * its promotion. */
static bool oem_collect_moves(OemCollect *collect, const void *ptr) {
    return !collect->minor || oem_collect_in_region(collect, ptr);
}

/* A reached continuation or slot vector of `bytes`, counted with its
 * generation's reached bytes. */
static void oem_collect_count(OemCollect *collect, const void *ptr,
                              size_t bytes) {
    if (oem_collect_in_region(collect, ptr))
        collect->reached_young += bytes;
    else if (!collect->minor)
        collect->reached_old += bytes;
}

/* A bound cell no live frame can unbind stands for its value: the
 * collection replaces its occurrences by that value (variable shunting), so
 * a term built through destination holes keeps no cells once they are
 * filled for good.  Bindings are acyclic, since unification checks
 * occurrence and the head binds only literals and fresh cells. */
static Atom *oem_collect_shunt(const CettaOpenEquationCursor *cursor,
                               const OemCollect *collect, Atom *atom) {
    uint32_t cell = 0u;
    while (oem_is_cell(cursor, atom, &cell) && cursor->cells[cell] &&
           cell >= collect->cell_base &&
           !collect->revocable[cell - collect->cell_base])
        atom = cursor->cells[cell];
    return atom;
}

static void oem_collect_reach(CettaOpenEquationCursor *cursor,
                              OemCollect *collect, uint32_t cell) {
    if (cell < collect->cell_base)
        return;
    uint32_t at = cell - collect->cell_base;
    if (collect->reached[at])
        return;
    collect->reached[at] = 1u;
    if (cursor->cells[cell])
        collect->pending[collect->pending_len++] = cell;
}

/* A cell's index after the collection: an old cell keeps its own. */
static inline uint32_t oem_collect_index(const OemCollect *collect,
                                         uint32_t cell) {
    return cell < collect->cell_base
        ? cell : collect->remap[cell - collect->cell_base];
}

/* Whether the collection keeps cell `cell`. */
static inline bool oem_collect_kept(const OemCollect *collect,
                                    uint32_t cell) {
    return cell < collect->cell_base ||
        collect->reached[cell - collect->cell_base];
}

/* A region expression the collection has visited: its `name_key`, null on
 * every expression but one an export has marked or copied to stable storage,
 * holds this marker while tracing, and the expression's copy once it is
 * copied.  An export's stable copy serves as that copy.  The old region is
 * the cursor's alone and is freed by the collection (a failed collection
 * ends the cursor), so marking it needs no side table. */
static Atom oem_traced_mark;

/* The copy a collection made of an expression, or NULL. */
static inline Atom *oem_collected_copy(const Atom *atom) {
    Atom *copy = atom->name_key;
    return copy && copy != &oem_traced_mark && copy != &oem_exported_mark
        ? copy : NULL;
}

/* For a view without variables over storage the collection frees, the
 * longest suffix of that storage the trace met, when it is another view:
 * the view is copied as a header over that suffix's copy
 * (SuffixViewCopy.view_copy).  A view with variables is copied whole, since
 * its elements' cells are renamed and its summaries would change. */
static inline __attribute__((always_inline)) Atom *oem_view_longest(
    OemCollect *collect, const Atom *atom) {
    if (!oem_is_view(atom) || atom_has_vars(atom))
        return NULL;
    Atom *longest = oem_collect_find(
        collect, oem_storage_key(atom->expr.elems + atom->expr.len));
    return longest && longest != atom ? longest : NULL;
}

/* Mark the cells a term mentions; region expressions are visited once. */
static bool oem_trace_atom(CettaOpenEquationCursor *cursor,
                           OemCollect *collect, Atom *root) {
    uint32_t len = 0u;
    if (!root)
        return true;
    if (!oem_reserve((void **)&cursor->walk, &cursor->walk_cap, 1u,
                     sizeof(*cursor->walk)))
        return false;
    cursor->walk[len++] = root;
    while (len > 0u) {
        Atom *atom = oem_collect_shunt(cursor, collect, cursor->walk[--len]);
        uint32_t cell = 0u;
        if (oem_is_cell(cursor, atom, &cell)) {
            oem_collect_reach(cursor, collect, cell);
            continue;
        }
        if (!oem_collect_from(collect, atom))
            continue;
        if (atom->kind != ATOM_EXPR || atom->expr.len == 0u) {
            /* A leaf is copied whole, and counted once: marked like an
             * expression, or through the table for a variable, whose
             * `name_key` is its spelling. */
            if (atom->kind == ATOM_VAR) {
                if (oem_collect_find(collect, atom))
                    continue;
                if (!oem_collect_put(collect, atom, atom))
                    return false;
            } else {
                if (atom->name_key)
                    continue;
                atom->name_key = &oem_traced_mark;
                if (collect->record) {
                    if (!oem_reserve((void **)&collect->marked,
                                     &collect->marked_cap,
                                     collect->marked_len + 1u,
                                     sizeof(*collect->marked)))
                        return false;
                    collect->marked[collect->marked_len++] = atom;
                }
            }
            size_t bytes = sizeof(Atom);
            if (atom->kind == ATOM_GROUNDED &&
                atom->ground.gkind == GV_STRING && atom->ground.sval)
                bytes += strlen(atom->ground.sval) + 1u;
            if (atom->arena_id == collect->from_identity)
                collect->reached_young += bytes;
            else
                collect->reached_old += bytes;
            continue;
        }
        if (atom->name_key && atom->name_key != &oem_exported_mark)
            continue;
        atom->name_key = &oem_traced_mark;
        bool view = oem_long_view(atom);
        bool header_only = view && collect->detach_identity == 0u &&
            !oem_collect_frees(collect, atom->expr.elems);
        /* The children to push, `[first, stop)`.  A view over storage the
         * collection frees pushes only the elements no longer suffix of the
         * same storage has: the table keeps, by storage end, the longest
         * suffix traced so far (SuffixViewCopy.mem_view_of_le).  A view with
         * variables is copied whole, and is counted so. */
        CettaExprLen first = 0u;
        CettaExprLen stop = header_only ? 0u : atom->expr.len;
        CettaExprLen counted = stop;
        if (view && !header_only) {
            const void *key = oem_storage_key(atom->expr.elems +
                                              atom->expr.len);
            void **longest_at = oem_collect_value_at(collect, key);
            Atom *longest = longest_at ? *longest_at : NULL;
            if (longest && longest->expr.elems <= atom->expr.elems) {
                stop = 0u;
            } else if (longest) {
                stop = (CettaExprLen)(longest->expr.elems -
                                      atom->expr.elems);
                *longest_at = atom;
            } else if (!oem_collect_put(collect, key, atom)) {
                return false;
            }
            if (!atom_has_vars(atom))
                counted = stop;
        }
        size_t bytes = sizeof(Atom) + sizeof(Atom *) * (size_t)counted;
        if (atom->arena_id == collect->from_identity)
            collect->reached_young += bytes;
        if (collect->record) {
            if (!oem_reserve((void **)&collect->marked, &collect->marked_cap,
                             collect->marked_len + 1u,
                             sizeof(*collect->marked)))
                return false;
            collect->marked[collect->marked_len++] = atom;
            if (atom->arena_id == collect->from_old_identity)
                collect->reached_old += bytes;
        }
        if (stop <= first)
            continue;
        if (stop - first > UINT32_MAX - len ||
            !oem_reserve((void **)&cursor->walk, &cursor->walk_cap,
                         len + (uint32_t)(stop - first),
                         sizeof(*cursor->walk)))
            return false;
        for (CettaExprIndex child = first; child < stop; child++)
            cursor->walk[len++] = atom->expr.elems[child];
    }
    return true;
}

/* A frame's arguments never change once it is pushed, so an older
 * argument vector names only older atoms and cells, which a minor
 * collection keeps; an activation's slots are stored as it runs, so every
 * reached one is traced. */
static bool oem_trace_slots(CettaOpenEquationCursor *cursor,
                            OemCollect *collect, Atom **slots,
                            uint32_t count, bool locals) {
    if (!slots || oem_collect_find(collect, slots))
        return true;
    if (!locals && !oem_collect_moves(collect, slots))
        return true;
    if (!oem_collect_put(collect, slots, slots))
        return false;
    size_t bytes = sizeof(Atom *) * ((size_t)count + (locals ? 1u : 0u));
    collect->walked += bytes;
    oem_collect_count(collect, slots, bytes);
    for (uint32_t index = 0u; index < count; index++) {
        if (!oem_trace_atom(cursor, collect, slots[index]))
            return false;
    }
    return true;
}

static bool oem_trace_cont(CettaOpenEquationCursor *cursor,
                           OemCollect *collect, const OemCont *cont) {
    for (const OemCont *at = cont; at; at = at->parent) {
        /* An older continuation, and its callers, which are older still,
         * name only older objects, except through slots stored since the
         * last collection, which the cursor remembers. */
        if (!oem_collect_moves(collect, at))
            return true;
        if (oem_collect_find(collect, at))
            return true;
        if (!oem_collect_put(collect, at, (void *)at))
            return false;
        collect->walked += sizeof(*at);
        oem_collect_count(collect, at, sizeof(*at));
        if (!oem_trace_slots(cursor, collect, at->locals, at->local_count,
                             true))
            return false;
    }
    return true;
}

/* A term's copy; atoms outside the region (cells, program literals,
 * callers' atoms) are shared.  Iterative, so deep lists cost no C stack. */
static Atom *oem_collect_atom(CettaOpenEquationCursor *cursor,
                              OemCollect *collect, Atom *root) {
    if (!root)
        return NULL;
    uint32_t depth = 0u;
    uint32_t results = 0u;
    Atom *atom = root;
    for (;;) {
        Atom *result = NULL;
        uint32_t cell = 0u;
        atom = oem_collect_shunt(cursor, collect, atom);
        if (oem_is_cell(cursor, atom, &cell)) {
            result = oem_collect_kept(collect, cell)
                ? cursor->cell_names[oem_collect_index(collect, cell)]
                : NULL;
        } else if (collect->detach_identity != 0u &&
                   atom->arena_id == collect->detach_identity) {
            result = atom_deep_copy_session_copy(collect->detach, atom);
        } else if (!oem_collect_from(collect, atom)) {
            result = atom;
        } else if (atom->kind == ATOM_EXPR && atom->expr.len > 0u &&
                   atom->name_key && atom->name_key != &oem_traced_mark &&
                   atom->name_key != &oem_exported_mark) {
            result = atom->name_key;
        } else if (atom->kind == ATOM_EXPR && atom->expr.len > 0u) {
            /* Copied below, once its children are; a view over storage the
             * collection keeps moves its header alone, over that storage. */
            result = oem_view_over_kept_storage(collect, atom)
                ? atom_expr_view_rehome(collect->to, atom, atom->expr.elems)
                : NULL;
            if (result)
                atom->name_key = result;
        } else if ((result = oem_collect_find(collect, atom)) != NULL) {
        } else {
            result = atom_deep_copy(collect->to, atom);
            if (!result || !oem_collect_put(collect, atom, result))
                return NULL;
        }
        Atom *requester = NULL;
        if (!result && oem_is_view(atom) &&
            oem_collect_from(collect, atom) &&
            (!atom->name_key || atom->name_key == &oem_traced_mark ||
             atom->name_key == &oem_exported_mark)) {
            /* A shorter view shares its storage's one copy: made now, on
             * its behalf, unless an earlier view had it made. */
            Atom *longest = oem_view_longest(collect, atom);
            Atom *copied = longest ? oem_collected_copy(longest) : NULL;
            if (copied) {
                result = atom_expr_view_rehome(
                    collect->to, atom,
                    copied->expr.elems +
                        (atom->expr.elems - longest->expr.elems));
                if (!result)
                    return NULL;
                atom->name_key = result;
            } else if (longest) {
                requester = atom;
                atom = longest;
            }
        }
        if (!result && atom->kind == ATOM_EXPR && atom->expr.len > 0u &&
            oem_collect_from(collect, atom) &&
            (!atom->name_key || atom->name_key == &oem_traced_mark ||
             atom->name_key == &oem_exported_mark)) {
            if (atom->expr.len > UINT32_MAX - results ||
                !oem_reserve((void **)&cursor->resolve_stack,
                             &cursor->resolve_cap, depth + 1u,
                             sizeof(*cursor->resolve_stack)) ||
                !oem_reserve((void **)&cursor->resolve_results,
                             &cursor->resolve_result_cap,
                             results + (uint32_t)atom->expr.len,
                             sizeof(*cursor->resolve_results)))
                return NULL;
            cursor->resolve_stack[depth++] = (OemResolveItem){
                .atom = atom, .next = 1u, .base = results,
                .requester = requester,
            };
            atom = atom->expr.elems[0];
            continue;
        }
        for (;;) {
            if (!result)
                return NULL;
            if (depth == 0u)
                return result;
            OemResolveItem *item = &cursor->resolve_stack[depth - 1u];
            cursor->resolve_results[results++] = result;
            if (item->next < item->atom->expr.len) {
                atom = item->atom->expr.elems[item->next++];
                break;
            }
            result = atom_expr(collect->to,
                               &cursor->resolve_results[item->base],
                               item->atom->expr.len);
            if (!result)
                return NULL;
            item->atom->name_key = result;
            if (item->requester) {
                Atom *view = item->requester;
                result = atom_expr_view_rehome(
                    collect->to, view,
                    result->expr.elems +
                        (view->expr.elems - item->atom->expr.elems));
                if (!result)
                    return NULL;
                view->name_key = result;
            }
            results = item->base;
            depth--;
        }
    }
}

/* A slot vector's copy: an activation's (`locals`) keeps the header word
 * before it, a frame's arguments have none. */
/* Rename in place a slot of a vector the collection leaves where it is, or
 * an old cell's binding, at most once: renaming a cell again would read its
 * new index as an old one.  A vector a continuation reaches is renamed
 * whole, a slot the barrier remembered may be reached that way too, and a
 * slot stored or a cell bound again after a restore is remembered again.
 * The location's address plus one keys it in the table: every object the
 * table keys is aligned, so the keys never meet. */
static bool oem_collect_rename_in_place(CettaOpenEquationCursor *cursor,
                                        OemCollect *collect, Atom **at) {
    const void *key = (const char *)at + 1;
    if (!*at || oem_collect_find(collect, key))
        return true;
    Atom *moved = oem_collect_atom(cursor, collect, *at);
    if (!moved || !oem_collect_put(collect, key, at))
        return false;
    *at = moved;
    return true;
}

static Atom **oem_collect_slots(CettaOpenEquationCursor *cursor,
                                OemCollect *collect, Atom **slots,
                                uint32_t count, bool locals, bool *ok) {
    if (!slots || !*ok)
        return slots;
    Atom **copied = oem_collect_find(collect, slots);
    if (copied)
        return copied;
    if (!oem_collect_moves(collect, slots)) {
        /* An older vector stays in place: its activation's slots stored
         * since its promotion may name region atoms and cells, which they
         * now name by their copies. */
        if (!locals)
            return slots;
        if (!oem_collect_put(collect, slots, slots)) {
            *ok = false;
            return NULL;
        }
        for (uint32_t index = 0u; index < count; index++) {
            if (!oem_collect_rename_in_place(cursor, collect,
                                             &slots[index])) {
                *ok = false;
                return NULL;
            }
        }
        return slots;
    }
    size_t header = locals ? 1u : 0u;
    uintptr_t *block = arena_alloc(
        collect->to, sizeof(Atom *) * (header + (count ? count : 1u)));
    copied = block ? (Atom **)(block + header) : NULL;
    if (block && locals)
        block[0] = ((const uintptr_t *)slots)[-1];
    if (!copied || !oem_collect_put(collect, slots, copied)) {
        *ok = false;
        return NULL;
    }
    for (uint32_t index = 0u; index < count; index++) {
        copied[index] = slots[index]
            ? oem_collect_atom(cursor, collect, slots[index]) : NULL;
        if (slots[index] && !copied[index]) {
            *ok = false;
            return NULL;
        }
    }
    return copied;
}

/* A continuation chain's copy, from its first continuation not yet copied;
 * iterative over the chain. */
static const OemCont *oem_collect_cont(CettaOpenEquationCursor *cursor,
                                       OemCollect *collect,
                                       const OemCont *cont, bool *ok) {
    const OemCont **chain = NULL;
    uint32_t len = 0u;
    uint32_t cap = 0u;
    const OemCont *top = NULL;
    const OemCont *kept = NULL;
    for (const OemCont *at = cont; at; at = at->parent) {
        if ((top = oem_collect_find(collect, at)) != NULL)
            break;
        if (!oem_collect_moves(collect, at)) {
            kept = at;
            break;
        }
        if (len == UINT32_MAX ||
            !oem_reserve((void **)&chain, &cap, len + 1u, sizeof(*chain))) {
            free(chain);
            *ok = false;
            return NULL;
        }
        chain[len++] = at;
    }
    /* Older continuations stay in place, and so do their callers, which
     * are older still.  A minor collection leaves their slot vectors too:
     * a slot stored since the last collection is remembered and renamed
     * on its own. */
    if (kept && collect->minor) {
        if (!oem_collect_find(collect, kept) &&
            !oem_collect_put(collect, kept, (void *)kept))
            *ok = false;
    } else {
        for (const OemCont *at = kept; *ok && at; at = at->parent) {
            if (oem_collect_find(collect, at))
                break;
            if (!oem_collect_put(collect, at, (void *)at)) {
                *ok = false;
                break;
            }
            (void)oem_collect_slots(cursor, collect, at->locals,
                                    at->local_count, true, ok);
        }
    }
    if (kept)
        top = kept;
    const OemCont *parent = top;
    for (uint32_t index = len; *ok && index-- > 0u;) {
        const OemCont *old = chain[index];
        OemCont *copied = arena_alloc(collect->to, sizeof(*copied));
        if (!copied) {
            *ok = false;
            break;
        }
        *copied = *old;
        copied->parent = parent;
        copied->locals = oem_collect_slots(cursor, collect, old->locals,
                                           old->local_count, true, ok);
        if (!*ok || !oem_collect_put(collect, old, copied)) {
            *ok = false;
            break;
        }
        parent = copied;
    }
    free(chain);
    return *ok ? (cont ? oem_collect_find(collect, cont) : NULL) : NULL;
}

/* The cells, frames and trail entries a collection walks whatever else it
 * reaches. */
static size_t oem_collect_roots(const CettaOpenEquationCursor *cursor) {
    return (size_t)cursor->cell_len * (sizeof(*cursor->cells) + sizeof(Atom)) +
        (size_t)cursor->frame_len * sizeof(*cursor->frames) +
        (size_t)cursor->trail_len * sizeof(*cursor->trail);
}

/* The region size at which the collection after this one runs.  A
 * collection walks the live region and its roots; the region must grow by
 * at least as much before the next one, so collection work stays
 * proportional to allocation. */
static size_t oem_collect_threshold(const CettaOpenEquationCursor *cursor) {
    size_t walked = oem_collect_roots(cursor);
    size_t first = oem_collect_sizes()->first;
    return walked > first ? walked : first;
}

/* Whether a region grown past `collect_after` is collected now.  Each
 * choice frame, and each binding the trail would undo, made since the last
 * collection is state a restore returns to, and a minor collection walks all
 * of it whatever else it reaches.  Below the single-generation bound a
 * collection therefore waits until the region is twice that state: a region
 * held by dense choice frames is mostly what they will restore, and
 * backtracking, not copying, reclaims it. */
static bool oem_collect_due(CettaOpenEquationCursor *cursor, size_t live) {
    uint32_t frame_low = cursor->frame_low < cursor->frame_len
        ? cursor->frame_low : cursor->frame_len;
    uint32_t trail_low = cursor->trail_low < cursor->trail_len
        ? cursor->trail_low : cursor->trail_len;
    size_t held =
        (size_t)(cursor->frame_len - frame_low) * sizeof(*cursor->frames) +
        (size_t)(cursor->trail_len - trail_low) * sizeof(*cursor->trail);
    if (live >= OEM_REGION_BOUND_BYTES || held <= live / 2u)
        return true;
    size_t next = held > OEM_REGION_BOUND_BYTES / 2u
        ? OEM_REGION_BOUND_BYTES : 2u * held;
    cursor->collect_after = next > live ? next : live + 1u;
    return false;
}

/* Keep what the cursor still reaches: the frames, every state they restore
 * included, the destination and the call's cells, and the cells a reached
 * term mentions.  A minor collection promotes the region's reached atoms
 * into the old generation and leaves the old atoms in place: they are
 * immutable and were made before any region atom or region cell, so they
 * reach the region only through cells, which it keeps as roots, and it
 * keeps every old cell at its index.  A major collection copies both
 * generations into a new old one.  Reached cells keep their order and take
 * consecutive indices, so a frame still marks the cells created after it
 * and the trail still names the cells to unbind; a region cell nothing
 * mentions is dropped, as no live term or future state reads it.  Frames
 * then resume in an empty region.  An atom the cursor borrows from the
 * arena `detach_identity` names is copied in (a major collection).  False
 * only when out of memory; the cursor then stops. */
/* A declined collection leaves everything where it is: the trace's marks
 * come off and its tables go. */
static void oem_collect_decline(OemCollect *collect, uint32_t *before,
                                uint32_t *kept_before, Atom ***args,
                                OemRows **rows, OemHostCells **hosts,
                                const OemCont **conts, Atom **cells) {
    for (uint32_t index = 0u; index < collect->marked_len; index++)
        collect->marked[index]->name_key = NULL;
    free(collect->marked);
    free(collect->young);
    free(collect->old_spans);
    free(collect->keys);
    free(collect->values);
    free(collect->reached);
    free(collect->pending);
    free(collect->remap);
    free(collect->revocable);
    free(before);
    free(kept_before);
    free(args);
    free(rows);
    free(hosts);
    free(conts);
    free(cells);
}

static bool oem_collect_generation(CettaOpenEquationCursor *cursor,
                                   uint32_t detach_identity, bool major) {
    uint32_t old_cells = cursor->cell_len;
    uint32_t frames = cursor->frame_len;
    /* A minor collection works from the old generation's boundaries up
     * (RememberedSetTrace.minor_trace_complete): the cells from the old
     * cells' end, and the frames, trail entries and stores from their least
     * lengths since the last collection, below which nothing changed.  A
     * frame below its low-water mark was there at the last collection,
     * whose commit left its marks at the empty region and at or below these
     * bases, and restoring or truncating keeps them so.  A major collection
     * works from zero. */
    uint32_t cell_base = major ? 0u
        : cursor->old_cells < old_cells ? cursor->old_cells : old_cells;
    uint32_t frame_base = major ? 0u
        : cursor->frame_low < frames ? cursor->frame_low : frames;
    uint32_t trail_base = major ? 0u
        : cursor->trail_low < cursor->trail_len ? cursor->trail_low
                                                : cursor->trail_len;
    uint32_t store_base = major ? 0u
        : cursor->store_low < cursor->store_len ? cursor->store_low
                                                : cursor->store_len;
    uint32_t young_cells = old_cells - cell_base;
    uint32_t young_frames = frames - frame_base;
    size_t cell_room = young_cells ? young_cells : 1u;
    OemCollect collect = {
        .from_identity = cursor->region.identity,
        .from_old_identity =
            major && cursor->old_ready ? cursor->old.identity : 0u,
        .detach_identity = detach_identity,
        .cell_base = cell_base,
        .reached = calloc(cell_room, sizeof(uint8_t)),
        .pending = calloc(cell_room, sizeof(uint32_t)),
        .remap = calloc(cell_room, sizeof(uint32_t)),
        .revocable = calloc(cell_room, sizeof(uint8_t)),
        .minor = !major,
        .record = detach_identity == 0u,
        .first_cap = oem_collect_table_size(
            cursor->forward_hints[major ? 1 : 0][0]),
    };
    uint32_t *before = calloc(cell_room + 1u, sizeof(uint32_t));
    uint32_t *kept_before = calloc(
        (size_t)(cursor->trail_len - trail_base) + 1u, sizeof(uint32_t));
    Atom ***args = calloc(young_frames ? young_frames : 1u, sizeof(*args));
    OemRows **rows = calloc(young_frames ? young_frames : 1u,
                            sizeof(*rows));
    OemHostCells **hosts = calloc(young_frames ? young_frames : 1u,
                                  sizeof(*hosts));
    const OemCont **conts = calloc(young_frames ? young_frames : 1u,
                                   sizeof(*conts));
    Atom **cells = calloc(cell_room, sizeof(*cells));
    bool ok = collect.reached && collect.pending && collect.remap &&
        collect.revocable && before && kept_before && args && rows &&
        hosts && conts && cells &&
        oem_collect_spans(&cursor->region, &collect.young,
                          &collect.young_len) &&
        (!major || !cursor->old_ready ||
         oem_collect_spans(&cursor->old, &collect.old_spans,
                           &collect.old_span_len));
    /* A frame's restore undoes the trail from its mark on; the oldest
     * frame's mark is the least.  A young cell was bound after the last
     * collection, so its entry lies at or above the trail's base. */
    uint32_t revoked = frames ? cursor->frames[0].trail_mark
                              : cursor->trail_len;
    if (revoked < trail_base)
        revoked = trail_base;
    for (uint32_t index = revoked; ok && index < cursor->trail_len; index++) {
        uint32_t cell = cursor->trail[index];
        if (cell >= cell_base)
            collect.revocable[cell - cell_base] = 1u;
    }

    /* Trace.  Old atoms may name any old cell, which a minor collection
     * therefore keeps; the call's cells stay cells, which its answer
     * reads. */
    for (uint32_t index = 0u; ok && index < cursor->query_var_count; index++) {
        uint32_t cell = 0u;
        if (oem_is_cell(cursor, cursor->query_cells[index], &cell))
            oem_collect_reach(cursor, &collect, cell);
    }
    for (uint32_t index = frame_base; ok && index < frames; index++) {
        const OemFrame *frame = &cursor->frames[index];
        uint32_t arity = oem_frame_arity(frame);
        ok = oem_trace_slots(cursor, &collect, frame->args, arity, false) &&
            oem_trace_cont(cursor, &collect, frame->cont);
        /* A host goal's cells are awaited: they stay cells. */
        for (uint32_t cell = 0u;
             ok && oem_frame_is_host(frame) && cell < frame->host->count;
             cell++)
            oem_collect_reach(cursor, &collect, frame->host->cells[cell]);
        /* A match's rows are reached with it, from the generation that
         * holds their block. */
        if (ok && oem_frame_is_match(frame)) {
            size_t bytes = sizeof(OemRows) +
                sizeof(Atom *) * (size_t)frame->rows->len;
            if (frame->rows->arena == collect.from_identity)
                collect.reached_young += bytes;
            else if (collect.from_old_identity != 0u &&
                     frame->rows->arena == collect.from_old_identity)
                collect.reached_old += bytes;
        }
    }
    /* The old places written since the last collection: the old cells
     * bound, and the old activations' slots stored.  A major collection
     * traces everything from the frames instead. */
    for (uint32_t index = 0u;
         ok && !major && index < cursor->remembered_cell_len; index++) {
        uint32_t cell = cursor->remembered_cells[index];
        if (cell < cell_base && cursor->cells[cell])
            ok = oem_trace_atom(cursor, &collect, cursor->cells[cell]);
    }
    for (uint32_t index = 0u;
         ok && !major && index < cursor->remembered_slot_len; index++) {
        const OemStore *slot = &cursor->remembered_slots[index];
        ok = oem_trace_atom(cursor, &collect, slot->locals[slot->slot]);
    }
    ok = ok && oem_trace_atom(cursor, &collect, cursor->destination);
    while (ok && collect.pending_len > 0u) {
        uint32_t cell = collect.pending[--collect.pending_len];
        ok = oem_trace_atom(cursor, &collect, cursor->cells[cell]);
    }
    /* A major collection reclaims only the old generation's dead part; when
     * nearly all of it is still reached, copying it would only double it
     * for a moment.  The marks come off and a minor collection runs
     * instead.  The next major waits for the old generation to grow by a
     * factor that doubles with each consecutive decline: a generation found
     * live again and again is traced whole ever more rarely, so the traces
     * of a run that keeps its data total a bounded multiple of what it
     * keeps, however long it runs.  A detaching collection always copies,
     * since it moves borrowed atoms. */
    size_t old_live = cursor->old_ready
        ? arena_accounted_live_bytes(&cursor->old) : 0u;
    if (ok && major && collect.record &&
        collect.reached_old >= old_live - old_live / 4u) {
        oem_collect_decline(&collect, before, kept_before, args, rows, hosts,
                            conts, cells);
        uint32_t shift = cursor->major_declines < OEM_MAJOR_DECLINE_SHIFT_MAX
            ? cursor->major_declines + 1u : OEM_MAJOR_DECLINE_SHIFT_MAX;
        cursor->major_after = old_live > SIZE_MAX >> shift
            ? SIZE_MAX : old_live << shift;
        cursor->major_declines++;
        return oem_collect_generation(cursor, 0u, false);
    }
    /* Likewise a minor collection that finds at least half of the region
     * reached would reclaim at most half of it at the cost of copying the
     * rest: it declines, and the region grows before the next is tried, by
     * twice as much after each consecutive decline, and by at most the bound
     * of a single-generation region. */
    size_t young_live = arena_accounted_live_bytes(&cursor->region);
    if (ok && collect.minor && collect.record &&
        collect.reached_young >= young_live - young_live / 2u) {
        oem_collect_decline(&collect, before, kept_before, args, rows, hosts,
                            conts, cells);
        uint32_t shift = cursor->collect_declines < 8u
            ? cursor->collect_declines + 1u : 9u;
        size_t grown = young_live > (SIZE_MAX >> shift)
            ? SIZE_MAX : young_live << shift;
        size_t bound = young_live > SIZE_MAX - OEM_REGION_BOUND_BYTES
            ? SIZE_MAX : young_live + OEM_REGION_BOUND_BYTES;
        cursor->collect_after = grown < bound ? grown : bound;
        cursor->collect_declines++;
        return true;
    }
    free(collect.marked);
    collect.marked = NULL;
    uint32_t live = cell_base;
    for (uint32_t index = cell_base; ok && index < old_cells; index++) {
        before[index - cell_base] = live;
        if (collect.reached[index - cell_base])
            collect.remap[index - cell_base] = live++;
    }
    if (ok)
        before[young_cells] = live;
    cursor->forward_hints[major ? 1 : 0][0] = collect.len;
    free(collect.keys);
    free(collect.values);
    collect.keys = NULL;
    collect.values = NULL;
    collect.cap = 0u;
    collect.len = 0u;
    collect.first_cap =
        oem_collect_table_size(cursor->forward_hints[major ? 1 : 0][1]);

    /* Copy into the old generation, or a new one, renaming each reached
     * cell to its index. */
    size_t old_before = !major && cursor->old_ready
        ? arena_accounted_live_bytes(&cursor->old) : 0u;
    Arena fresh;
    if (major || !cursor->old_ready) {
        arena_init(&fresh);
        arena_set_runtime_kind(&fresh, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
        arena_set_hashcons(&fresh, NULL);
    }
    Arena *to_arena = major || !cursor->old_ready ? &fresh : &cursor->old;
    collect.to = to_arena;
    if (ok && detach_identity != 0u) {
        collect.detach = atom_deep_copy_session_new(to_arena);
        ok = collect.detach != NULL;
    }
    for (uint32_t index = frame_base; ok && index < frames; index++) {
        const OemFrame *frame = &cursor->frames[index];
        uint32_t arity = oem_frame_arity(frame);
        uint32_t young = index - frame_base;
        args[young] = oem_collect_slots(cursor, &collect, frame->args, arity,
                                        false, &ok);
        conts[young] = ok ? oem_collect_cont(cursor, &collect, frame->cont,
                                             &ok) : NULL;
        if (ok && oem_frame_is_host(frame)) {
            OemHostCells *copy = arena_alloc(
                to_arena, sizeof(*copy) +
                         sizeof(uint32_t) * (frame->host->count
                                                 ? frame->host->count : 1u));
            ok = copy != NULL;
            if (ok) {
                copy->count = frame->host->count;
                copy->height = frame->host->height;
                for (uint32_t cell = 0u; cell < copy->count; cell++)
                    copy->cells[cell] = oem_collect_index(
                        &collect, frame->host->cells[cell]);
            }
            hosts[young] = copy;
        }
        /* A match's rows are the space's atoms; only the block moves, and
         * only out of a generation this collection collects. */
        if (ok && oem_frame_is_match(frame)) {
            bool moves = frame->rows->arena == collect.from_identity ||
                (collect.from_old_identity != 0u &&
                 frame->rows->arena == collect.from_old_identity);
            size_t bytes = sizeof(OemRows) +
                sizeof(Atom *) * (size_t)frame->rows->len;
            rows[young] = moves ? arena_alloc(to_arena, bytes) : frame->rows;
            ok = rows[young] != NULL;
            if (ok && moves) {
                memcpy(rows[young], frame->rows, bytes);
                rows[young]->arena = to_arena->identity;
            }
        }
    }
    for (uint32_t index = cell_base; ok && index < old_cells; index++) {
        if (!collect.reached[index - cell_base] || !cursor->cells[index])
            continue;
        uint32_t to = collect.remap[index - cell_base] - cell_base;
        cells[to] = oem_collect_atom(cursor, &collect, cursor->cells[index]);
        ok = cells[to] != NULL;
    }
    /* The remembered old places keep their places, and name the copies. */
    for (uint32_t index = 0u;
         ok && !major && index < cursor->remembered_cell_len; index++) {
        uint32_t cell = cursor->remembered_cells[index];
        if (cell >= cell_base)
            continue;
        ok = oem_collect_rename_in_place(cursor, &collect,
                                         &cursor->cells[cell]);
    }
    for (uint32_t index = 0u;
         ok && !major && index < cursor->remembered_slot_len; index++) {
        const OemStore *slot = &cursor->remembered_slots[index];
        ok = oem_collect_rename_in_place(cursor, &collect,
                                         &slot->locals[slot->slot]);
    }
    Atom *destination = ok
        ? oem_collect_atom(cursor, &collect, cursor->destination) : NULL;
    ok = ok && destination;
    /* The store trail follows its slot vectors; a store into a vector no
     * continuation reaches is dropped with it.  In a minor collection an
     * older vector stays where it is, and so does its store. */
    uint32_t young_stores = cursor->store_len - store_base;
    uint32_t *stores_before = ok
        ? calloc((size_t)young_stores + 1u, sizeof(uint32_t)) : NULL;
    OemStore *stores = ok
        ? calloc(young_stores ? young_stores : 1u, sizeof(*stores))
        : NULL;
    ok = ok && stores_before && stores;
    uint32_t stores_kept = store_base;
    for (uint32_t index = store_base; ok && index < cursor->store_len;
         index++) {
        stores_before[index - store_base] = stores_kept;
        Atom **locals = cursor->stores[index].locals;
        Atom **copied = oem_collect_find(&collect, locals);
        if (!copied && !oem_collect_moves(&collect, locals))
            copied = locals;
        if (copied)
            stores[stores_kept++ - store_base] = (OemStore){
                .locals = copied, .slot = cursor->stores[index].slot};
    }
    if (ok)
        stores_before[young_stores] = stores_kept;
    if (collect.detach)
        atom_deep_copy_session_free(collect.detach);
    cursor->forward_hints[major ? 1 : 0][1] = collect.len;
    free(collect.keys);
    free(collect.values);
    if (!ok) {
        free(collect.young);
        free(collect.old_spans);
        free(stores_before);
        free(stores);
        free(collect.reached);
        free(collect.pending);
        free(collect.remap);
        free(collect.revocable);
        free(before);
        free(kept_before);
        free(args);
        free(rows);
        free(hosts);
        free(conts);
        free(cells);
        if (to_arena == &fresh)
            arena_free(&fresh);
        return false;
    }

    /* Commit.  What lies below the bases stays as it is: the old trail
     * entries name old cells, which keep their indices, and the old frames'
     * marks already are the empty region and the bases' own. */
    uint32_t kept = trail_base;
    for (uint32_t index = trail_base; index < cursor->trail_len; index++) {
        kept_before[index - trail_base] = kept;
        uint32_t cell = cursor->trail[index];
        if (oem_collect_kept(&collect, cell))
            cursor->trail[kept++] = oem_collect_index(&collect, cell);
    }
    kept_before[cursor->trail_len - trail_base] = kept;
    /* Every region atom the cursor still reaches has been copied: the
     * region restarts empty, keeping its blocks for what follows. */
    arena_reset(&cursor->region, (ArenaMark){0});
    ArenaMark end = arena_mark(&cursor->region);
    for (uint32_t index = frame_base; index < frames; index++) {
        OemFrame *frame = &cursor->frames[index];
        uint32_t young = index - frame_base;
        frame->args = args[young];
        if (oem_frame_is_match(frame))
            frame->rows = rows[young];
        if (oem_frame_is_host(frame))
            frame->host = hosts[young];
        frame->cont = conts[young];
        frame->mark = end;
        frame->cell_mark = before[frame->cell_mark - cell_base];
        frame->trail_mark = kept_before[frame->trail_mark - trail_base];
        frame->store_mark = stores_before[frame->store_mark - store_base];
    }
    cursor->trail_len = kept;
    if (stores_kept > store_base)
        memcpy(&cursor->stores[store_base], stores,
               sizeof(*stores) * (stores_kept - store_base));
    cursor->store_len = stores_kept;
    free(stores_before);
    free(stores);
    for (uint32_t index = cell_base; index < live; index++)
        cursor->cells[index] = cells[index - cell_base];
    for (uint32_t index = live; index < old_cells; index++)
        cursor->cells[index] = NULL;
    cursor->cell_len = live;
    for (uint32_t index = 0u; index < cursor->query_var_count; index++)
        cursor->query_cells[index] = cursor->cell_names[oem_collect_index(
            &collect,
            (uint32_t)(var_base_id(cursor->query_cells[index]->var_id) - 1u))];
    cursor->destination = destination;
    oem_forget_answer_vars(cursor);
    free(collect.young);
    free(collect.old_spans);
    free(collect.reached);
    free(collect.pending);
    free(collect.remap);
    free(collect.revocable);
    free(before);
    free(kept_before);
    free(args);
    free(rows);
    free(hosts);
    free(conts);
    free(cells);
    if (to_arena == &fresh) {
        if (cursor->old_ready)
            arena_free(&cursor->old);
        cursor->old = fresh;
        cursor->old_ready = true;
    }
    cursor->old_cells = live;
    /* Everything is old now: nothing below the new lengths changes until
     * the mutator writes it. */
    cursor->frame_low = cursor->frame_len;
    cursor->trail_low = cursor->trail_len;
    cursor->store_low = cursor->store_len;
    cursor->remembered_cell_len = 0u;
    cursor->remembered_slot_len = 0u;
    cursor->collect_epoch++;
    cursor->slot_epoch_bits = (uintptr_t)cursor->collect_epoch << 32;
    cursor->collect_declines = 0u;
    cursor->collect_after = oem_collect_threshold(cursor);
    if (collect.walked > cursor->collect_after)
        cursor->collect_after = collect.walked;
    /* After a minor collection the region waits for what the next one
     * walks, and for twice what this one promoted, so copying stays
     * proportional to allocation; otherwise for 32 times what it promoted,
     * between the steady floor and the first window (above).  It keeps only
     * the blocks that window needs. */
    if (!major) {
        size_t promoted = arena_accounted_live_bytes(&cursor->old) - old_before;
        const OemCollectSizes *sizes = oem_collect_sizes();
        size_t window = promoted > sizes->first / OEM_COLLECT_SURVIVAL_RATIO
            ? sizes->first
            : promoted * OEM_COLLECT_SURVIVAL_RATIO;
        if (window < sizes->steady)
            window = sizes->steady;
        size_t twice = promoted > SIZE_MAX / 2u ? SIZE_MAX : 2u * promoted;
        if (collect.walked > window)
            window = collect.walked;
        if (twice > window)
            window = twice;
        cursor->collect_after = window;
    }
    arena_release_spare(&cursor->region, cursor->collect_after);
    if (major) {
        size_t survivors = arena_accounted_live_bytes(&cursor->old);
        size_t major_min = oem_collect_sizes()->major;
        cursor->major_after = survivors > major_min / 2u
            ? 2u * survivors : major_min;
        cursor->major_declines = 0u;
    }
    cursor->stats.collections++;
    return true;
}

/* Advance `frame` past the equations selection refutes; each counts as the
 * head failure its activation would meet. */
/* Whether the first structural test of `equation`'s head refutes the
 * call's arguments: the head's leading symbol or shape, which the call
 * decides in one comparison. */
/* The equations of an indexed relation a call's arguments leave: along
 * each index path, a term that is an unbound cell, or a cell of a list,
 * leaves every equation; a term whose structure the path's steps refute
 * leaves the equations testing nothing there; any other term leaves those
 * and the equations whose literal there is that term. */
static uint64_t oem_paths_admit(CettaOpenEquationCursor *cursor,
                                const CettaOpenEquationProgram *program,
                                uint32_t first_path, uint32_t path_count,
                                Atom **args) {
    uint64_t candidates = UINT64_MAX;
    for (uint32_t index = 0u; index < path_count; index++) {
        const OemIndexPath *path = &program->index_paths[first_path + index];
        Atom *term = oem_deref(cursor, args[path->arg]);
        uint32_t cell = 0u;
        bool open = false;
        bool refuted = false;
        for (uint32_t step = 0u; step < path->step_count; step++) {
            if (oem_is_cell(cursor, term, &cell) || oem_is_list_cell(term)) {
                open = true;
                break;
            }
            uint32_t child = program->index_steps[path->first_step + 2u * step];
            uint32_t length =
                program->index_steps[path->first_step + 2u * step + 1u];
            if (term->kind != ATOM_EXPR || term->expr.len != length) {
                refuted = true;
                break;
            }
            term = oem_deref(cursor, term->expr.elems[child]);
        }
        if (open || (!refuted && (oem_is_cell(cursor, term, &cell) ||
                                  oem_is_list_cell(term))))
            continue;
        uint64_t admitted = path->free;
        uint64_t code = 0u;
        if (refuted) {
        } else if (path->key_count >= OEM_INDEX_TABLE_MIN_KEYS &&
                   path->table_mask && oem_literal_code(term, &code)) {
            /* Keys are distinct literals, so at most one equals the term;
             * its code finds it. */
            const uint32_t *table = &program->index_tables[path->first_table];
            for (uint32_t at = oem_literal_home(code, path->table_mask);
                 table[at]; at = (at + 1u) & path->table_mask) {
                const OemIndexKey *entry =
                    &program->index_keys[path->first_key + table[at] - 1u];
                if (oem_key_eq(term, entry->literal)) {
                    admitted |= entry->equations;
                    break;
                }
            }
        } else {
            for (uint32_t key = 0u; key < path->key_count; key++) {
                const OemIndexKey *entry =
                    &program->index_keys[path->first_key + key];
                if (oem_key_eq(term, entry->literal))
                    admitted |= entry->equations;
            }
        }
        candidates &= admitted;
    }
    return candidates;
}

/* A number other than an integer, which `atom_eq` may find equal to an
 * integer key. */
static inline bool oem_non_integer_number(const Atom *atom) {
    return atom->kind == ATOM_GROUNDED && atom->ground.gkind != GV_INT;
}

/* What a switch's paths admit for the call's argument, decided by its
 * functor; false when a number could equal an integer key, which the paths
 * then decide one by one. */
static inline bool oem_switch_admits(CettaOpenEquationCursor *cursor,
                                     const CettaOpenEquationProgram *program,
                                     const OemIndexSwitch *sw, Atom **args,
                                     uint64_t *admitted) {
    Atom *term = oem_deref(cursor, args[sw->arg]);
    uint32_t cell = 0u;
    if (oem_is_cell(cursor, term, &cell) || oem_is_list_cell(term)) {
        *admitted = UINT64_MAX;
        return true;
    }
    uint32_t length = 0u;
    const OemSwitchLength *shape = NULL;
    Atom *key = term;
    if (term->kind == ATOM_EXPR) {
        length = term->expr.len;
        for (uint32_t index = 0u; index < sw->length_count; index++) {
            const OemSwitchLength *candidate =
                &program->switch_lengths[sw->first_length + index];
            if (candidate->length == length) {
                shape = candidate;
                break;
            }
        }
        if (!shape) {
            *admitted = sw->other;
            return true;
        }
        key = oem_deref(cursor, term->expr.elems[0]);
        if (oem_is_cell(cursor, key, &cell) || oem_is_list_cell(key)) {
            *admitted = shape->open_head;
            return true;
        }
    }
    uint64_t missed = shape ? shape->no_key : sw->other;
    uint64_t code = 0u;
    if (!oem_literal_code(key, &code)) {
        if (oem_non_integer_number(key) &&
            (shape ? shape->int_keys : sw->int_keys))
            return false;
        *admitted = missed;
        return true;
    }
    const uint32_t *table = &program->switch_tables[sw->first_table];
    for (uint32_t at = oem_literal_home(oem_switch_code(code, length),
                                        sw->entry_mask);
         table[at]; at = (at + 1u) & sw->entry_mask) {
        const OemSwitchEntry *entry =
            &program->switch_entrys[sw->first_entry + table[at] - 1u];
        if (entry->length == length && oem_key_eq(key, entry->literal)) {
            *admitted = entry->equations;
            return true;
        }
    }
    *admitted = missed;
    return true;
}

static uint64_t oem_index_candidates(CettaOpenEquationCursor *cursor,
                                     const CettaOpenEquationProgram *program,
                                     const OemRelation *relation,
                                     Atom **args) {
    uint64_t candidates = relation->index_path_count
        ? oem_paths_admit(cursor, program, relation->first_index_path,
                          relation->index_path_count, args)
        : UINT64_MAX;
    for (uint32_t index = 0u; index < relation->switch_count; index++) {
        const OemIndexSwitch *sw =
            &program->index_switchs[relation->first_switch + index];
        uint64_t admitted = 0u;
        if (!oem_switch_admits(cursor, program, sw, args, &admitted))
            admitted = oem_paths_admit(cursor, program, sw->first_path,
                                       sw->path_count, args);
        candidates &= admitted;
    }
    return candidates;
}

static bool oem_first_test_refutes(CettaOpenEquationCursor *cursor,
                                   const CettaOpenEquationProgram *program,
                                   uint32_t equation, Atom **args,
                                   uint32_t arity) {
    const OemEquation *eq = &program->equations[equation];
    for (uint32_t op_index = 0u; op_index < eq->op_count; op_index++) {
        const OemMatchOp *op = &program->ops[eq->first_op + op_index];
        if (op->kind != OEM_M_ATOM && op->kind != OEM_M_EXPR &&
            op->kind != OEM_M_LIST)
            continue;
        if (op->reg >= arity)
            return false;
        Atom *term = oem_deref(cursor, args[op->reg]);
        uint32_t cell = 0u;
        if (oem_is_cell(cursor, term, &cell) || oem_is_list_cell(term))
            return false;
        return op->kind == OEM_M_ATOM
            ? !atom_eq(term, op->literal)
            : term->kind != ATOM_EXPR ||
                  (op->kind == OEM_M_LIST ? term->expr.len == 0u
                                          : term->expr.len != op->length);
    }
    return false;
}

static void oem_skip_refuted(CettaOpenEquationCursor *cursor,
                             const CettaOpenEquationProgram *program,
                             const OemRelation *relation, OemFrame *frame,
                             bool *error) {
    while (frame->next < relation->equation_count &&
           !oem_may_match(cursor, program,
                          relation->first_equation + frame->next,
                          frame->args, relation->arity, error)) {
        cursor->stats.head_failures++;
        oem_count_attempt(cursor, OEM_ATTEMPT_MISMATCH, cursor->binds,
                          OEM_RUN_FAILED);
        frame->next++;
    }
}

/* A call's entry from the body that makes it.  Selection runs on the
 * call's arguments as it would on a frame just pushed, in the same state:
 * the first equation it admits is activated here, and a frame is pushed,
 * before the activation, only when an equation after that one remains to
 * be tried.  The frame holds a copy of the arguments, the candidates and
 * the next alternative, as the loop leaves a frame it has entered.  So a
 * deterministic call makes no frame, where one pushed at the call would be
 * popped before its only activation. */
static OemRun oem_enter(CettaOpenEquationCursor *cursor,
                        const CettaOpenEquationProgram *version,
                        uint32_t relation_index, Atom **args,
                        const OemCont *cont, uint32_t depth,
                        OemEntry *entry) {
    if (cursor->runtime.depth_bound && !version->depth_pruning_safe) {
        cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_UNSUPPORTED;
        return OEM_RUN_HANDOFF;
    }
    const OemRelation *relation = &version->relations[relation_index];
    uint32_t count = relation->equation_count;
    bool indexed = relation->indexed;
    uint64_t candidates = indexed
        ? oem_index_candidates(cursor, version, relation, args)
        : UINT64_MAX;
    uint32_t next = 0u;
    if (indexed) {
        /* The index leaves no equation beyond the relation's. */
        next = candidates ? (uint32_t)__builtin_ctzll(candidates) : count;
        if (next > count)
            next = count;
        oem_count_passed_over(cursor, next);
    }
    if (next >= count)
        return OEM_RUN_FAILED;
    uint32_t equation = relation->first_equation + next++;
    while (next < count &&
           ((indexed && !((candidates >> next) & 1u)) ||
            (relation->first_tests_differ &&
             oem_first_test_refutes(cursor, version,
                                    relation->first_equation + next, args,
                                    relation->arity)))) {
        cursor->stats.head_failures++;
        oem_count_attempt(cursor, OEM_ATTEMPT_MISMATCH, cursor->binds,
                          OEM_RUN_FAILED);
        next++;
    }
    Atom **held = args;
    bool own_frame = next < count;
    if (own_frame) {
        uint32_t arity = relation->arity;
        held = oem_region_alloc(cursor,
                                sizeof(*held) * (arity ? arity : 1u));
        if (!held)
            return OEM_RUN_HANDOFF;
        memcpy(held, args, sizeof(*held) * arity);
        if (!oem_push_frame(cursor, version, relation_index, held, cont,
                            depth))
            return OEM_RUN_HANDOFF;
        OemFrame *frame = &cursor->frames[cursor->frame_len - 1u];
        frame->candidates = candidates;
        frame->next = next;
    }
    OemRun run = oem_activate(cursor, version, equation, held,
                              relation->arity, cont, own_frame, entry);
    /* An activation past the depth bound fails once its head has matched,
     * so the run knows it cut something. */
    if (run == OEM_RUN_CALLED) {
        if (cursor->runtime.depth_bound &&
            depth > cursor->runtime.depth_bound) {
            cursor->bound_cut = true;
            return OEM_RUN_FAILED;
        }
        entry->depth = depth;
    }
    return run;
}

/* After an answer, whose value is copied out, the cursor's next move is to
 * backtrack: restore the newest frame and drop the frames whose remaining
 * equations selection refutes.  So a call whose other equations cannot
 * match leaves no alternative behind it, and `pending` reports only one
 * that can still answer.  False only when out of memory. */
static bool oem_settle(CettaOpenEquationCursor *cursor, uint32_t base) {
    while (cursor->frame_len > base) {
        OemFrame *frame = &cursor->frames[cursor->frame_len - 1u];
        oem_restore(cursor, frame);
        if (__builtin_expect(oem_frame_is_match(frame), 0)) {
            if (frame->next < frame->rows->len)
                return true;
            oem_pop_frame(cursor);
            continue;
        }
        /* An element frame with elements left, and a collection or
         * resumption, still have work; a once has none. */
        if (__builtin_expect(frame->relation >= OEM_FIRST_MARKER_FRAME, 0)) {
            if ((frame->relation == OEM_ELEMENTS_FRAME &&
                 frame->next >= frame->args[1]->expr.len) ||
                frame->relation == OEM_ONCE_FRAME) {
                oem_pop_frame(cursor);
                continue;
            }
            return true;
        }
        const OemRelation *relation =
            &frame->program->relations[frame->relation];
        bool error = false;
        oem_skip_refuted(cursor, frame->program, relation, frame, &error);
        if (error)
            return false;
        if (frame->next < relation->equation_count)
            return true;
        oem_pop_frame(cursor);
    }
    return true;
}

/* A minor collection, or a major one once promotion has grown the old
 * generation to its bound. */
static bool oem_collect(CettaOpenEquationCursor *cursor) {
    return oem_collect_generation(
        cursor, 0u,
        cursor->old_ready &&
            arena_accounted_live_bytes(&cursor->old) >= cursor->major_after);
}

bool cetta_open_equation_cursor_detach(CettaOpenEquationCursor *cursor,
                                       const Arena *arena) {
    return cursor && arena &&
        oem_collect_generation(cursor, arena->identity, true);
}

/* ---- cursor -------------------------------------------------------------- */

/* The call's arguments in the region.  Ground structure is borrowed from
 * the host's store (`cetta_open_equation_cursor_detach` copies it before
 * the host releases it), so answers share it as equation search's would;
 * each query variable becomes its cell. */

/* The cell of a stored row's variable in the row's current copy, made the
 * first time the copy needs it. */
static Atom *oem_fresh_var(CettaOpenEquationCursor *cursor, VarId id) {
    uint32_t len = cursor->fresh_var_len;
    if (len <= CETTA_VAR_INDEX_SCAN) {
        for (uint32_t index = 0u; index < len; index++) {
            if (cursor->fresh_vars[index].id == id)
                return cursor->fresh_vars[index].cell;
        }
    } else {
        /* The index proposes, the list confirms (VariableIndex.lookup):
         * entries left by an earlier copy are never confirmed. */
        uint32_t position =
            cetta_var_index_propose(&cursor->fresh_index, id);
        if (position < len && cursor->fresh_vars[position].id == id)
            return cursor->fresh_vars[position].cell;
    }
    Atom *cell = oem_new_cell(cursor);
    if (!cell ||
        !oem_reserve((void **)&cursor->fresh_vars, &cursor->fresh_var_cap,
                     len + 1u, sizeof(*cursor->fresh_vars)))
        return NULL;
    cursor->fresh_vars[len] = (OemFreshVar){.id = id, .cell = cell};
    cursor->fresh_var_len = ++len;
    if (len <= CETTA_VAR_INDEX_SCAN)
        return cell;
    CettaVarIndex *index = &cursor->fresh_index;
    if (len == CETTA_VAR_INDEX_SCAN + 1u || !cetta_var_index_has_room(index)) {
        /* Past the scan the index holds every variable of this copy:
         * recorded afresh on the way past, and again in a larger table,
         * which drops older copies' entries. */
        if (!cetta_var_index_reset(index, len))
            return NULL;
        for (uint32_t index_position = 0u; index_position < len;
             index_position++)
            cetta_var_index_record(index,
                                   cursor->fresh_vars[index_position].id,
                                   index_position);
    } else {
        cetta_var_index_record(index, id, len - 1u);
    }
    return cell;
}

/* A copy of a stored row in the region with one new cell per distinct
 * variable: a row is universally quantified, so each match of it gets its
 * own variables.  Ground parts are shared. */
static Atom *oem_import_fresh(CettaOpenEquationCursor *cursor, Atom *atom) {
    OemImport import;
    oem_import_init(&import, OEM_IMPORT_ROW);
    Atom *result = oem_import_walk(cursor, &import, atom);
    oem_import_free(&import);
    return result;
}

/* A region term unified with a stored row's fresh copy (oem_import_fresh),
 * copying only what the unification keeps: the row is walked beside the
 * term, a variable of the row gets its cell when the walk meets it, and a
 * part of the row is copied only when an unbound cell takes it whole.  The
 * walk is left to right, as the copy's, so the cells are the copy's own. */
static OemUnify oem_unify_row(CettaOpenEquationCursor *cursor, Atom *term,
                              Atom *row, uint32_t depth) {
    if (!atom_has_vars(row)) {
        /* A ground part of the row: an unbound cell takes it, which needs
         * no occurs check, and an atom compares, as oem_unify would. */
        Atom *value = oem_deref(cursor, term);
        uint32_t cell = 0u;
        if (oem_is_cell(cursor, value, &cell))
            return oem_bind(cursor, cell, row) ? OEM_UNIFY_OK
                                               : OEM_UNIFY_ERROR;
        if (value == row)
            return OEM_UNIFY_OK;
        if (value->kind != ATOM_EXPR && row->kind != ATOM_EXPR &&
            value->kind != ATOM_VAR)
            return atom_eq(value, row) ? OEM_UNIFY_OK : OEM_UNIFY_FAIL;
        return oem_unify(cursor, value, row);
    }
    if (row->kind == ATOM_VAR) {
        Atom *cell = oem_fresh_var(cursor, row->var_id);
        return cell ? oem_unify(cursor, term, cell) : OEM_UNIFY_ERROR;
    }
    term = oem_deref(cursor, term);
    uint32_t cell = 0u;
    if (depth > OEM_MAX_DEPTH || row->kind != ATOM_EXPR ||
        oem_is_cell(cursor, term, &cell) || oem_is_list_cell(term) ||
        oem_is_list_cell(row)) {
        Atom *copy = oem_import_fresh(cursor, row);
        return copy ? oem_unify(cursor, term, copy) : OEM_UNIFY_ERROR;
    }
    if (term->kind != ATOM_EXPR || term->expr.len != row->expr.len)
        return term->kind == ATOM_VAR ? OEM_UNIFY_ERROR : OEM_UNIFY_FAIL;
    for (CettaExprIndex child = 0u; child < row->expr.len; child++) {
        OemUnify unified = oem_unify_row(cursor, term->expr.elems[child],
                                         row->expr.elems[child], depth + 1u);
        if (unified != OEM_UNIFY_OK)
            return unified;
    }
    return OEM_UNIFY_OK;
}

/* A match's pattern, read in place over `locals`, unified with a stored
 * row's fresh copy (oem_unify_row): only a part of the pattern that meets
 * a variable of the row, or a list cell, is built. */
static OemUnify oem_unify_template_row(
    CettaOpenEquationCursor *cursor, const CettaOpenEquationProgram *program,
    Atom **locals, uint32_t template_index, Atom *row, uint32_t depth) {
    const OemTemplate *item = &program->templates[template_index];
    if (item->kind == OEM_T_LITERAL)
        return oem_unify_row(cursor, item->literal, row, depth);
    if (item->kind == OEM_T_SLOT)
        return oem_unify_row(cursor, locals[item->slot], row, depth);
    if (item->kind != OEM_T_BUILD || depth > OEM_MAX_DEPTH ||
        row->kind == ATOM_VAR || oem_is_list_cell(row)) {
        Atom *built = oem_instantiate(cursor, program, locals,
                                      template_index);
        return built ? oem_unify_row(cursor, built, row, depth)
                     : OEM_UNIFY_ERROR;
    }
    if (row->kind != ATOM_EXPR || row->expr.len != item->count)
        return OEM_UNIFY_FAIL;
    for (uint32_t index = 0u; index < item->count; index++) {
        OemUnify unified = oem_unify_template_row(
            cursor, program, locals,
            program->template_children[item->first + index],
            row->expr.elems[index], depth + 1u);
        if (unified != OEM_UNIFY_OK)
            return unified;
    }
    return OEM_UNIFY_OK;
}

/* One row of a match: a fresh copy unified with the pattern.  CALLED when
 * it unifies; the code after the match then runs in the matching
 * activation. */
static __attribute__((noinline)) OemRun oem_try_candidate(
                                CettaOpenEquationCursor *cursor,
                                Atom *pattern, Atom *candidate) {
    cursor->stats.match_candidates++;
    cursor->fresh_var_len = 0u;
    OemUnify unified = oem_unify_row(cursor, pattern, candidate, 0u);
    if (unified != OEM_UNIFY_OK)
        return unified == OEM_UNIFY_FAIL ? OEM_RUN_FAILED : OEM_RUN_HANDOFF;
    return OEM_RUN_CALLED;
}

/* One row of a match whose frame keeps no built pattern: the pattern is
 * the match step's, read over the activation the match resumes, whose
 * state restoring the frame returned to the state the match saw. */
static __attribute__((noinline)) OemRun oem_try_candidate_in_place(
                                CettaOpenEquationCursor *cursor,
                                const OemCont *resume, Atom *candidate) {
    cursor->stats.match_candidates++;
    cursor->fresh_var_len = 0u;
    const OemStep *step = &resume->program->steps[resume->pc - 1u];
    OemUnify unified = oem_unify_template_row(
        cursor, resume->program, resume->locals, step->pattern, candidate,
        0u);
    if (unified != OEM_UNIFY_OK)
        return unified == OEM_UNIFY_FAIL ? OEM_RUN_FAILED : OEM_RUN_HANDOFF;
    return OEM_RUN_CALLED;
}

CettaOpenEquationCursor *cetta_open_equation_cursor_open(
    CettaOpenEquationProgram *program, Arena *answer_arena,
    Atom *const *args, uint32_t arity, Atom *expected,
    Atom *const *query_vars, uint32_t query_var_count,
    const CettaOpenEquationRuntime *runtime) {
    if (!program || !answer_arena || (!args && arity > 0u) ||
        (!query_vars && query_var_count > 0u) ||
        program->relation_len == 0u || program->relations[0].arity != arity ||
        (runtime && runtime->depth_bound && !program->depth_pruning_safe))
        return NULL;
    CettaOpenEquationCursor *cursor = calloc(1u, sizeof(*cursor));
    if (!cursor)
        return NULL;
    cursor->program = program;
    atomic_fetch_add_explicit(&program->refs, 1u, memory_order_relaxed);
    cursor->answer_arena = answer_arena;
    cursor->collect_after = oem_collect_sizes()->first;
    cursor->major_after = oem_collect_sizes()->major;
    if (runtime)
        cursor->runtime = *runtime;
    arena_init(&cursor->region);
    arena_set_runtime_kind(&cursor->region, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    arena_set_hashcons(&cursor->region, NULL);
    oem_link_generations(cursor);
    arena_init(&cursor->names);
    arena_set_runtime_kind(&cursor->names, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    arena_set_hashcons(&cursor->names, NULL);
    if (!cetta_frame_identity_acquire(&cursor->epoch)) {
        cursor->epoch = 0u;
        cetta_open_equation_cursor_close(cursor);
        return NULL;
    }
    cursor->query_var_count = query_var_count;
    cursor->query_vars = query_vars;
    if (!cetta_var_index_build_atom(&cursor->query_index, query_vars,
                                    query_var_count)) {
        cetta_open_equation_cursor_close(cursor);
        return NULL;
    }
    cursor->query_cells = calloc(query_var_count ? query_var_count : 1u,
                                 sizeof(*cursor->query_cells));
    cursor->query_values = calloc(query_var_count ? query_var_count : 1u,
                                  sizeof(*cursor->query_values));
    Atom **imported = arena_alloc(&cursor->region,
                                  sizeof(*imported) * (arity ? arity : 1u));
    bool ok = cursor->query_cells && cursor->query_values && imported;
    for (uint32_t index = 0u; ok && index < query_var_count; index++) {
        cursor->query_cells[index] = oem_new_cell(cursor);
        ok = cursor->query_cells[index] != NULL;
    }
    OemImport import;
    oem_import_init(&import, OEM_IMPORT_CALL);
    for (uint32_t index = 0u; ok && index < arity; index++) {
        imported[index] = oem_import_walk(cursor, &import, args[index]);
        ok = imported[index] != NULL;
    }
    if (ok) {
        cursor->destination = expected
            ? oem_import_walk(cursor, &import, expected)
            : oem_new_cell(cursor);
        ok = cursor->destination != NULL;
    }
    oem_import_free(&import);
    ok = ok && oem_push_frame(cursor, program, 0u, imported, NULL, 1u);
    cursor->query_vars = NULL;
    if (!ok) {
        cetta_open_equation_cursor_close(cursor);
        return NULL;
    }
    return cursor;
}

bool cetta_open_equation_cursor_pending(
    const CettaOpenEquationCursor *cursor) {
    return cetta_open_equation_cursor_pending_above(cursor, 0u);
}

static CettaOpenEquationStep oem_cursor_run(
    CettaOpenEquationCursor *cursor, uint32_t base, Atom **value_out,
    Atom ***query_values_out);

CettaOpenEquationStep cetta_open_equation_cursor_next(
    CettaOpenEquationCursor *cursor, Atom *const *query_vars,
    Atom **value_out, Atom ***query_values_out) {
    return cetta_open_equation_cursor_next_above(cursor, 0u, query_vars,
                                                 value_out, query_values_out);
}

/* An interrupt stops between alternatives, where the region is consistent:
 * the cursor resumes once the host has cleared it.  Any other handoff is
 * final.  A change of the Space program or of the host's authorities stops
 * nothing: alternatives keep the equations their calls were entered with,
 * and later calls enter current ones. */
static bool oem_cursor_enter(CettaOpenEquationCursor *cursor,
                             Atom *const *query_vars, Atom **value_out,
                             Atom ***query_values_out) {
    if (value_out)
        *value_out = NULL;
    if (query_values_out)
        *query_values_out = NULL;
    if (!cursor || !value_out)
        return false;
    cursor->host_goal = NULL;
    cursor->host_destination = NULL;
    cursor->host_var_count = 0u;
    cursor->host_plan = NULL;
    cursor->host_mode = CETTA_OPEN_EQUATION_HOST_SOLVE;
    cursor->host_recovers = false;
    cursor->answer_weight = 1u;
    cursor->answer_folded = false;
    if (cursor->handoff == CETTA_OPEN_EQUATION_HANDOFF_INTERRUPT)
        cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_NONE;
    if (cursor->handoff != CETTA_OPEN_EQUATION_HANDOFF_NONE)
        return false;
    cursor->query_vars = query_vars;
    return true;
}

CettaOpenEquationStep cetta_open_equation_cursor_next_above(
    CettaOpenEquationCursor *cursor, uint32_t base, Atom *const *query_vars,
    Atom **value_out, Atom ***query_values_out) {
    if (!oem_cursor_enter(cursor, query_vars, value_out, query_values_out))
        return CETTA_OPEN_EQUATION_HANDOFF;
    CettaOpenEquationStep step = oem_cursor_run(cursor, base, value_out,
                                                query_values_out);
    cursor->query_vars = NULL;
    return step;
}

bool cetta_open_equation_cursor_pending_above(
    const CettaOpenEquationCursor *cursor, uint32_t base) {
    return cursor && cursor->frame_len > base;
}

uint64_t cetta_open_equation_cursor_answer_weight(
    const CettaOpenEquationCursor *cursor, bool *folded_out) {
    if (folded_out)
        *folded_out = cursor && cursor->answer_folded;
    return cursor ? cursor->answer_weight : 1u;
}

bool cetta_open_equation_cursor_host_goal(
    const CettaOpenEquationCursor *cursor, Atom **goal_out,
    Atom **destination_out, Atom *const **vars_out,
    uint32_t *var_count_out, const struct PettaPlanNode **plan_out,
    CettaOpenEquationHostMode *mode_out, bool *recovers_out) {
    if (!cursor || !cursor->host_goal)
        return false;
    *goal_out = cursor->host_goal;
    *destination_out = cursor->host_destination;
    *vars_out = cursor->host_vars;
    *var_count_out = cursor->host_var_count;
    *plan_out = cursor->host_plan;
    *mode_out = cursor->host_mode;
    *recovers_out = cursor->host_recovers;
    return true;
}

void cetta_open_equation_cursor_host_height(CettaOpenEquationCursor *cursor,
                                            uint32_t height) {
    OemFrame *frame = cursor && cursor->frame_len
        ? &cursor->frames[cursor->frame_len - 1u] : NULL;
    if (frame && oem_frame_is_host(frame))
        frame->host->height = height;
}

uint32_t cetta_open_equation_cursor_cut_height(
    const CettaOpenEquationCursor *cursor) {
    return cursor ? cursor->cut_height : 0u;
}

/* A host answer's value in the region: the goal's own variables are the
 * cells they stood for, the call's variables its cells, and any other
 * variable a new cell.  Everything is copied, since the host's store may
 * move. */
bool cetta_open_equation_cursor_accept(
    CettaOpenEquationCursor *cursor, Atom *const *query_vars,
    Atom *const *host_vars, Atom *const *host_values, uint32_t host_count,
    bool last, uint32_t last_base, uint32_t *base_out) {
    Atom *value = NULL;
    Atom **query_values = NULL;
    if (!oem_cursor_enter(cursor, query_vars, &value, &query_values))
        return false;
    OemFrame *frame = cursor->frame_len
        ? &cursor->frames[cursor->frame_len - 1u] : NULL;
    if (!frame || !oem_frame_is_host(frame) ||
        frame->host->count != host_count ||
        (host_count && (!host_vars || !host_values)) ||
        (last && last_base >= cursor->frame_len)) {
        cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_CAPACITY;
        cursor->query_vars = NULL;
        return false;
    }
    oem_restore(cursor, frame);
    const OemHostCells *cells = frame->host;
    const OemCont *cont = frame->cont;
    /* After the goal's last answer its frame has nothing to resume; the
     * cells and continuation it kept lie below its mark and stay. */
    uint32_t base = cursor->frame_len;
    if (last) {
        oem_pop_frame(cursor);
        base = last_base;
    }
    if (base_out)
        *base_out = base;
    cursor->fresh_var_len = 0u;
    bool unified = true;
#if CETTA_BUILD_WITH_RUNTIME_STATS
    size_t imported_before = arena_accounted_live_bytes(&cursor->region);
#endif
    /* The goal's variables by identifier, past a short scan, for every value
     * of this answer. */
    CettaVarIndex host_index;
    cetta_var_index_init(&host_index);
    if (!cetta_var_index_build_atom(&host_index, host_vars, host_count)) {
        cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_CAPACITY;
        cursor->query_vars = NULL;
        return false;
    }
    OemImport import;
    oem_import_init(&import, OEM_IMPORT_HOST);
    import.host_vars = host_vars;
    import.cells = cells;
    import.host_index = &host_index;
    for (uint32_t index = 0u; unified && index < host_count; index++) {
        Atom *imported = oem_import_walk(cursor, &import, host_values[index]);
        OemUnify outcome = imported
            ? oem_unify(cursor, cursor->cell_names[cells->cells[index]],
                        imported)
            : OEM_UNIFY_ERROR;
        if (outcome == OEM_UNIFY_ERROR) {
            oem_import_free(&import);
            cetta_var_index_free(&host_index);
            cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_CAPACITY;
            cursor->query_vars = NULL;
            return false;
        }
        unified = outcome == OEM_UNIFY_OK;
    }
    oem_import_free(&import);
    cetta_var_index_free(&host_index);
#if CETTA_BUILD_WITH_RUNTIME_STATS
    size_t imported_after = arena_accounted_live_bytes(&cursor->region);
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_OPEN_EQUATION_HOST_IMPORT_BYTES,
        imported_after > imported_before ? imported_after - imported_before
                                         : 0u);
#endif
    if (unified &&
        !oem_push_frame(cursor, NULL, OEM_RESUME_FRAME, NULL, cont,
                        cont ? cont->depth : 1u)) {
        cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_CAPACITY;
        unified = false;
    }
    cursor->query_vars = NULL;
    return unified;
}

CettaOpenEquationStep cetta_open_equation_cursor_continue(
    CettaOpenEquationCursor *cursor, Atom *const *query_vars, uint32_t base,
    Atom **value_out, Atom ***query_values_out) {
    if (!oem_cursor_enter(cursor, query_vars, value_out, query_values_out))
        return CETTA_OPEN_EQUATION_HANDOFF;
    CettaOpenEquationStep step =
        oem_cursor_run(cursor, base, value_out, query_values_out);
    cursor->query_vars = NULL;
    return step;
}

/* Run the frames at depth `base` and above.  A host frame met at the top
 * has no answers left: its goal's choices were below the choice now
 * running. */
static CettaOpenEquationStep oem_cursor_run(
    CettaOpenEquationCursor *cursor, uint32_t base, Atom **value_out,
    Atom ***query_values_out) {
    for (;;) {
        if (cursor->frame_len <= base)
            return CETTA_OPEN_EQUATION_EXHAUSTED;
        if (cursor->runtime.activation_budget &&
            cursor->stats.activations >=
                cursor->runtime.activation_budget) {
            cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_BUDGET;
            return CETTA_OPEN_EQUATION_HANDOFF;
        }
        if (cursor->runtime.interrupt && ++cursor->poll >= 256u) {
            cursor->poll = 0u;
            if (cursor->runtime.interrupt(cursor->runtime.context)) {
                cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_INTERRUPT;
                return CETTA_OPEN_EQUATION_HANDOFF;
            }
        }
        size_t region_live = arena_accounted_live_bytes(&cursor->region);
        if (region_live >= cursor->collect_after &&
            oem_collect_due(cursor, region_live) && !oem_collect(cursor)) {
            cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_CAPACITY;
            return CETTA_OPEN_EQUATION_HANDOFF;
        }
        OemFrame *frame = &cursor->frames[cursor->frame_len - 1u];
        oem_restore(cursor, frame);
        OemRun run = OEM_RUN_FAILED;
        OemEntry entry;
        /* Once, elements, collection, resumption, host and match frames
         * carry the six highest relation values.  A once whose body has no
         * answer left fails. */
        if (__builtin_expect(frame->relation >= OEM_FIRST_MARKER_FRAME, 0)) {
            if (oem_frame_is_host(frame) ||
                frame->relation == OEM_ONCE_FRAME) {
                oem_pop_frame(cursor);
                continue;
            }
            if (frame->relation == OEM_RESUME_FRAME) {
                const OemCont *resume = frame->cont;
                oem_pop_frame(cursor);
                run = OEM_RUN_CALLED;
                entry = (OemEntry){
                    .program = resume->program, .pc = resume->pc,
                    .locals = resume->locals,
                    .local_count = resume->local_count,
                    .cont = resume->parent, .depth = resume->depth,
                };
                goto run_body;
            }
            if (frame->relation == OEM_COLLECT_FRAME) {
                /* The collection's call has no answer left, and the state
                 * is the caller's again: its list meets the destination. */
                const OemCont *after = frame->cont;
                OemCollectBox *box = frame->collect;
                oem_pop_frame(cursor);
                Atom *list = oem_collected_list(cursor, box);
                Atom *destination = list
                    ? oem_instantiate(cursor, after->program, after->locals,
                                      after->pattern)
                    : NULL;
                OemUnify unified = destination
                    ? oem_unify(cursor, destination, list)
                    : OEM_UNIFY_ERROR;
                if (unified == OEM_UNIFY_ERROR) {
                    cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_CAPACITY;
                    return CETTA_OPEN_EQUATION_HANDOFF;
                }
                if (unified == OEM_UNIFY_FAIL)
                    continue;
                run = OEM_RUN_CALLED;
                entry = (OemEntry){
                    .program = after->program, .pc = after->pc,
                    .locals = after->locals,
                    .local_count = after->local_count,
                    .cont = after->parent, .depth = after->depth,
                };
                goto run_body;
            }
            if (frame->relation == OEM_ELEMENTS_FRAME) {
                Atom *list = frame->args[1];
                if (frame->next >= list->expr.len) {
                    oem_pop_frame(cursor);
                    continue;
                }
                Atom *element = list->expr.elems[frame->next++];
                Atom *hole = frame->args[0];
                const OemCont *resume = frame->cont;
                if (frame->next >= list->expr.len)
                    oem_pop_frame(cursor);
                OemUnify unified = oem_unify(cursor, hole, element);
                if (unified == OEM_UNIFY_ERROR) {
                    cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_CAPACITY;
                    return CETTA_OPEN_EQUATION_HANDOFF;
                }
                if (unified == OEM_UNIFY_FAIL)
                    continue;
                run = OEM_RUN_CALLED;
                entry = (OemEntry){
                    .program = resume->program, .pc = resume->pc,
                    .locals = resume->locals,
                    .local_count = resume->local_count,
                    .cont = resume->parent, .depth = resume->depth,
                };
                goto run_body;
            }
            if (frame->next >= frame->rows->len) {
                oem_pop_frame(cursor);
                continue;
            }
            Atom *candidate = frame->rows->rows[frame->next++];
            Atom *pattern = frame->args ? frame->args[0] : NULL;
            const OemCont *resume = frame->cont;
            if (frame->next >= frame->rows->len)
                oem_pop_frame(cursor);
            run = pattern ? oem_try_candidate(cursor, pattern, candidate)
                          : oem_try_candidate_in_place(cursor, resume,
                                                       candidate);
            if (run == OEM_RUN_CALLED)
                entry = (OemEntry){
                    .program = resume->program, .pc = resume->pc,
                    .locals = resume->locals,
                    .local_count = resume->local_count,
                    .cont = resume->parent, .depth = resume->depth,
                };
        } else {
            const CettaOpenEquationProgram *version = frame->program;
            const OemRelation *relation =
                &version->relations[frame->relation];
            if (frame->next >= relation->equation_count) {
                oem_pop_frame(cursor);
                continue;
            }
            if (relation->indexed) {
                if (frame->next == 0u)
                    frame->candidates = oem_index_candidates(
                        cursor, version, relation, frame->args);
                while (frame->next < relation->equation_count &&
                       !((frame->candidates >> frame->next) & 1u)) {
                    cursor->stats.head_failures++;
                    oem_count_attempt(cursor, OEM_ATTEMPT_MISMATCH,
                                      cursor->binds, OEM_RUN_FAILED);
                    frame->next++;
                }
                if (frame->next >= relation->equation_count) {
                    oem_pop_frame(cursor);
                    continue;
                }
            }
            uint32_t equation = relation->first_equation + frame->next++;
            Atom **args = frame->args;
            const OemCont *cont = frame->cont;
            uint32_t depth = frame->depth;
            /* The alternatives whose heads the call's arguments refute at
             * their first test go now, while the state is the one they
             * would be tried in, and the last alternative leaves nothing to
             * return to: the frame goes, so a deterministic call, and a
             * chain of them, keeps no frame. */
            while (frame->next < relation->equation_count &&
                   ((relation->indexed &&
                     !((frame->candidates >> frame->next) & 1u)) ||
                    (relation->first_tests_differ &&
                     oem_first_test_refutes(
                         cursor, version,
                         relation->first_equation + frame->next, args,
                         relation->arity)))) {
                cursor->stats.head_failures++;
                oem_count_attempt(cursor, OEM_ATTEMPT_MISMATCH, cursor->binds,
                                  OEM_RUN_FAILED);
                frame->next++;
            }
            bool own_frame = frame->next < relation->equation_count;
            if (!own_frame)
                oem_pop_frame(cursor);
            run = oem_activate(cursor, version, equation, args,
                               relation->arity, cont, own_frame, &entry);
            /* An activation past the depth bound fails once its head has
             * matched, so the run knows it cut something. */
            if (run == OEM_RUN_CALLED) {
                if (cursor->runtime.depth_bound &&
                    depth > cursor->runtime.depth_bound) {
                    cursor->bound_cut = true;
                    run = OEM_RUN_FAILED;
                } else {
                    entry.depth = depth;
                }
            }
        }
        /* A head or a row that matched runs its body here, the one place
         * the body runner is called from. */
    run_body:
        if (run == OEM_RUN_CALLED)
            run = oem_run_body(cursor, entry.program, entry.pc, entry.locals,
                               entry.local_count, entry.cont, entry.depth,
                               value_out);
        /* Failure and a pushed call continue; every other outcome is one
         * test away, since they follow those two in the enumeration. */
        if (run < OEM_RUN_ANSWER)
            continue;
        if (run == OEM_RUN_HOST)
            return CETTA_OPEN_EQUATION_HOST;
        if (run == OEM_RUN_CUT)
            return CETTA_OPEN_EQUATION_CUT;
        if (run == OEM_RUN_HANDOFF) {
            if (cursor->handoff == CETTA_OPEN_EQUATION_HANDOFF_NONE)
                cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_CAPACITY;
            return CETTA_OPEN_EQUATION_HANDOFF;
        }
        if (!oem_settle(cursor, base)) {
            cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_CAPACITY;
            return CETTA_OPEN_EQUATION_HANDOFF;
        }
        if (run == OEM_RUN_RAISE)
            return CETTA_OPEN_EQUATION_RAISE;
        if (query_values_out)
            *query_values_out = cursor->query_values;
        return CETTA_OPEN_EQUATION_ANSWER;
    }
}

CettaOpenEquationHandoff cetta_open_equation_cursor_handoff(
    const CettaOpenEquationCursor *cursor) {
    return cursor ? cursor->handoff : CETTA_OPEN_EQUATION_HANDOFF_CAPACITY;
}

bool cetta_open_equation_cursor_can_prune_depth(
    const CettaOpenEquationCursor *cursor) {
    return cursor && cursor->program->depth_pruning_safe;
}

bool cetta_open_equation_cursor_bound_cut(
    const CettaOpenEquationCursor *cursor) {
    return cursor && cursor->bound_cut;
}

void cetta_open_equation_cursor_set_budget(CettaOpenEquationCursor *cursor,
                                           uint64_t budget) {
    if (!cursor)
        return;
    cursor->runtime.activation_budget = budget;
    if (cursor->handoff == CETTA_OPEN_EQUATION_HANDOFF_BUDGET)
        cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_NONE;
}

void cetta_open_equation_cursor_note_authority_change(
    CettaOpenEquationCursor *cursor) {
    if (cursor)
        cursor->authority_epoch++;
}

void cetta_open_equation_cursor_close(CettaOpenEquationCursor *cursor) {
    if (!cursor)
        return;
    arena_free(&cursor->region);
    if (cursor->old_ready)
        arena_free(&cursor->old);
    arena_free(&cursor->names);
    if (cursor->collected_ready)
        arena_free(&cursor->collected);
    if (cursor->scratch_ready)
        arena_free(&cursor->scratch);
    free(cursor->cell_names);
    if (cursor->epoch)
        cetta_frame_identity_release(cursor->epoch);
    free(cursor->cells);
    free(cursor->trail);
    free(cursor->stores);
    free(cursor->remembered_cells);
    free(cursor->remembered_slots);
    free(cursor->frames);
    free(cursor->fresh_vars);
    cetta_var_index_free(&cursor->fresh_index);
    cetta_var_index_free(&cursor->query_index);
    free(cursor->space_rows);
    free(cursor->space_row_keys);
    free(cursor->space_row_parts);
    free(cursor->schemas);
    free(cursor->host_vars);
    free(cursor->pairs);
    free(cursor->walk);
    free(cursor->regs);
    free(cursor->probe);
    free(cursor->query_cells);
    free(cursor->query_values);
    free(cursor->answer_fresh);
    free(cursor->fresh_touched);
    free(cursor->resolve_stack);
    free(cursor->resolve_results);
    free(cursor->absent);
    cetta_open_equation_program_release(cursor->program);
    for (uint32_t index = 0u; index < cursor->version_len; index++)
        cetta_open_equation_program_release(cursor->versions[index]);
    free(cursor->versions);
    free(cursor->version_epochs);
    free(cursor);
}

void cetta_open_equation_cursor_stats(const CettaOpenEquationCursor *cursor,
                                      CettaOpenEquationStats *stats_out) {
    if (!stats_out)
        return;
    memset(stats_out, 0, sizeof(*stats_out));
    if (cursor)
        *stats_out = cursor->stats;
}

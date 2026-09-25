#include "open_equation_machine.h"

#include "binding/frame_identity.h"
#include "grounded.h"
#include "petta_program.h"
#include "petta_semantics.h"
#include "search_machine.h"
#include "stats.h"
#include "symbol.h"

#include <stdlib.h>
#include <string.h>

/* ---- program ------------------------------------------------------------ */

/* A template instantiates over an activation's slots. */
typedef enum {
    OEM_T_LITERAL = 0,
    OEM_T_SLOT,
    OEM_T_BUILD,
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
} OemMatchKind;

typedef struct {
    uint8_t kind;
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
    OEM_S_PRIM,      /* `pattern` unifies with op(args)                    */
    OEM_S_BIND,      /* `pattern` unifies with `value`                     */
    OEM_S_TEST,      /* op(args) false: continue at `target`               */
    OEM_S_MATCH,     /* the following steps run once per row of the space
                        `value` that unifies with `pattern`                 */
    OEM_S_HOST,      /* the host evaluates `value` against `pattern`, under
                        the source plan `relation` names; the following
                        steps run once per answer.  A type-pure operation
                        `op` runs in the region when it can decide there */
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
    OEM_HOST_PURE,
    OEM_HOST_DATA_HEAD,
    OEM_HOST_ADMIT,
    OEM_HOST_APPLY,
    OEM_HOST_SORT,
};

typedef struct {
    uint8_t kind;
    /* The step's `pattern` is a slot at its first occurrence on every path
     * through the body: the step stores its result there rather than
     * unifying (a PRIM, a BIND, or a HOST step the region may decide). */
    uint8_t store;
    SymbolId op;
    uint32_t relation;
    uint32_t first_arg;
    uint32_t arg_count;
    uint32_t pattern;
    uint32_t value;
    uint32_t target;
} OemStep;

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
} OemEquation;

typedef struct {
    SymbolId head;
    uint32_t arity;
    uint32_t first_equation;
    uint32_t equation_count;
} OemRelation;

struct CettaOpenEquationProgram {
    /* The compiler's reference and one per open cursor. */
    uint32_t refs;
    Space *space;
    SpaceProgramToken token;
    /* The spelling of region and answer variables. */
    SymbolId spelling;
    OemRelation *relations;
    uint32_t relation_len, relation_cap;
    OemEquation *equations;
    uint32_t equation_len, equation_cap;
    OemMatchOp *ops;
    uint32_t op_len, op_cap;
    OemStep *steps;
    uint32_t step_len, step_cap;
    uint32_t *step_args;
    uint32_t step_arg_len, step_arg_cap;
    OemTemplate *templates;
    uint32_t template_len, template_cap;
    uint32_t *template_children;
    uint32_t template_child_len, template_child_cap;
    /* The source plans of HOST steps' goals, which the PeTTa program owns. */
    const PettaPlanNode **host_plans;
    uint32_t host_plan_len, host_plan_cap;
    uint8_t *slot_stores;
    uint32_t slot_store_len, slot_store_cap;
    /* The entry relation only relays to the host (see
     * cetta_open_equation_program_entry_relays). */
    bool entry_relays;
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

void cetta_open_equation_program_release(
    CettaOpenEquationProgram *program) {
    if (!program || --program->refs > 0u)
        return;
    free(program->relations);
    free(program->equations);
    free(program->ops);
    free(program->steps);
    free(program->step_args);
    free(program->templates);
    free(program->template_children);
    free(program->host_plans);
    free(program->slot_stores);
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
} OemNodeKind;

typedef struct {
    uint8_t kind;
    bool tail;
    /* A HOST node's fast path (OEM_HOST_*). */
    uint8_t host_fast;
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
} OemNode;

typedef struct {
    CettaOpenEquationProgram *program;
    const CettaOpenEquationHost *host;
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

/* A head subterm with no relational occurrence anywhere inside. */
static bool oem_head_data(OemCompile *compile, Atom *atom, uint32_t depth) {
    if (!atom || depth > OEM_MAX_DEPTH)
        return false;
    if (atom->kind != ATOM_EXPR)
        return true;
    if (oem_head_callable(compile, atom))
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

/* A let pattern, or an argument of a head's relational occurrence:
 * variables are slots, nothing is evaluated.  A pattern the plan calls a
 * call is a relational pattern, outside the fragment. */
static bool oem_lower_pattern(OemCompile *compile, Atom *pattern,
                              const PettaPlanNode *plan, uint32_t depth,
                              uint32_t *out) {
    if (!pattern || depth > OEM_MAX_DEPTH)
        return oem_reject(compile, "pattern too deep");
    if (pattern->kind == ATOM_VAR) {
        uint32_t slot = 0u;
        return oem_slot_of(compile, pattern->var_id, &slot) &&
            oem_template_slot(compile, slot, out);
    }
    if (pattern->kind != ATOM_EXPR || pattern->expr.len == 0u)
        return oem_template_literal(compile, pattern, out);
    if (plan && plan->role != PETTA_PLAN_DATA)
        return oem_reject(compile, "relational pattern");
    uint32_t count = pattern->expr.len;
    uint32_t *children = malloc(sizeof(*children) * count);
    if (!children)
        return oem_reject(compile, "out of memory");
    bool ok = true;
    for (uint32_t index = 0u; ok && index < count; index++)
        ok = oem_lower_pattern(compile, pattern->expr.elems[index],
                               plan ? petta_plan_child(plan, index) : NULL,
                               depth + 1u, &children[index]);
    ok = ok && oem_template_build(compile, children, count, out);
    free(children);
    return ok;
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

/* Whether a planned node is data: a constructor node, or a list whose head
 * element is itself a data node.  The plan calls the latter a dynamic call,
 * because a head that evaluates to a symbol would be dispatched; a data
 * head keeps its constructor after evaluation, so the node stays data unless
 * that constructor is applied (a lambda or a partial application). */
static bool oem_data_node(Atom *expr, const PettaPlanNode *plan) {
    Atom *head = expr->expr.elems[0];
    if (head->kind == ATOM_SYMBOL)
        return plan->role == PETTA_PLAN_DATA &&
            plan->control == PETTA_PLAN_CONTROL_NONE &&
            petta_semantics_form(head->sym_id) == PETTA_FORM_NONE;
    const PettaPlanNode *head_plan = petta_plan_child(plan, 0u);
    Atom *body = NULL;
    return plan->role == PETTA_PLAN_DYNAMIC_CALL &&
        plan->control == PETTA_PLAN_CONTROL_NONE &&
        head->kind == ATOM_EXPR && head->expr.len > 0u &&
        head->expr.elems[0]->kind == ATOM_SYMBOL &&
        head_plan && head_plan->role == PETTA_PLAN_DATA &&
        petta_semantics_form(head->expr.elems[0]->sym_id) == PETTA_FORM_NONE &&
        !petta_semantics_lambda_body(head, &body) &&
        !petta_semantics_nullary_lambda_body(head, &body) &&
        !petta_semantics_partial_view(head, NULL, NULL) &&
        !petta_semantics_partial_head(head);
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
    return oem_lower_pattern(compile, expr, NULL, depth + 1u, &node.value) &&
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
        node->kind == OEM_N_HOST || node->kind == OEM_N_APPLY;
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

/* A dynamic call whose head is itself an occurrence the plan calls: the
 * search machine evaluates every element in order, then applies the head's
 * value to the others or keeps them as data.  The region evaluates the
 * elements; the host's application of their values decides the rest. */
static bool oem_apply_call(Atom *expr, const PettaPlanNode *plan) {
    const PettaPlanNode *head_plan = petta_plan_child(plan, 0u);
    return plan->role == PETTA_PLAN_DYNAMIC_CALL &&
        plan->control == PETTA_PLAN_CONTROL_NONE &&
        plan->child_count == expr->expr.len &&
        expr->expr.elems[0]->kind == ATOM_EXPR && head_plan &&
        (head_plan->role == PETTA_PLAN_STATIC_CALL ||
         head_plan->role == PETTA_PLAN_DYNAMIC_CALL);
}

static bool oem_build_apply(OemCompile *compile, Atom *expr,
                            const PettaPlanNode *plan, uint32_t depth,
                            uint32_t *out) {
    CettaOpenEquationProgram *program = compile->program;
    OemNode node = {.kind = OEM_N_APPLY, .relation = program->host_plan_len};
    /* PeTTa's sort takes its argument's value once; applying the head to
     * that value keeps the host from evaluating it again. */
    if (expr->expr.len == 2u && expr->expr.elems[0]->kind == ATOM_SYMBOL &&
        petta_semantics_form(expr->expr.elems[0]->sym_id) ==
            PETTA_FORM_MSORT) {
        node.host_fast = (uint8_t)OEM_HOST_SORT;
        node.op = expr->expr.elems[0]->sym_id;
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
    program->host_plans[program->host_plan_len++] = NULL;
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
    if (symbol_head && oem_relation_call(expr, plan) &&
        compile->host && compile->host->relation_admitted &&
        !compile->host->relation_admitted(
            compile->host->context, compile->program->space,
            expr->expr.elems[0]->sym_id, expr->expr.len - 1u))
        return oem_build_host(compile, expr, plan, depth,
                              "relation owned by the host", out);
    if (symbol_head && oem_relation_call(expr, plan)) {
        node.kind = OEM_N_CALL;
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
     * a condition does: the host answers it by membership. */
    if (oem_apply_call(expr, plan) ||
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
        /* A condition that observes whether a match has a row is the host's
         * whole: its search answers the observation by membership, which
         * splitting the equality from its collection would lose. */
        if (!test || test->kind != ATOM_EXPR || test->expr.len != 3u ||
            test->expr.elems[0]->kind != ATOM_SYMBOL ||
            !oem_is_test(test->expr.elems[0]->sym_id) || !test_plan ||
            test_plan->role != PETTA_PLAN_STATIC_CALL ||
            petta_semantics_match_existence_observer_shape(
                test, compile->host ? compile->host->reify_head
                                    : SYMBOL_ID_NONE))
            return oem_build_host(compile, expr, plan, depth,
                                  "condition outside the fragment", out);
        node.kind = OEM_N_IF;
        node.op = test->expr.elems[0]->sym_id;
        return oem_build_elements(compile, test, test_plan, depth, 1u,
                                  &node.first, &node.count) &&
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
        if (!oem_lower_pattern(compile, expr->expr.elems[1],
                               petta_plan_child(plan, 1u), depth + 1u,
                               &node.pattern) ||
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
            if (!oem_lower_pattern(compile, pair->expr.elems[0],
                                   petta_plan_child(pair_plan, 0u),
                                   depth + 1u, &let.pattern) ||
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
    if (head == g_builtin_syms.empty_form && expr->expr.len == 1u) {
        node.kind = OEM_N_FAIL;
        return oem_hole(compile, &node.exposed) &&
            oem_node(compile, node, out);
    }
    if (!oem_build_value(compile, expr, plan, depth, out))
        return false;
    if (compile->nodes[*out].kind == OEM_N_CALL)
        compile->nodes[*out].tail = true;
    return true;
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
                                uint32_t depth, bool tail, uint32_t *out) {
    if (depth > OEM_MAX_DEPTH)
        return oem_reject(compile, "expression too deep");
    OemNode node = {.kind = OEM_N_MATCH};
    if (!oem_build_value(compile, space, space_plan, depth + 1u, &node.value))
        return false;
    if (compile->nodes[node.value].kind != OEM_N_VALUE ||
        compile->nodes[node.value].count != 0u)
        return oem_reject(compile, "match space operand");
    if (!oem_lower_pattern(compile, patterns[0], NULL, depth + 1u,
                           &node.pattern))
        return false;
    bool ok = count > 1u
        ? oem_build_match_leg(compile, space, space_plan, patterns + 1u,
                              count - 1u, template, template_plan,
                              depth + 1u, tail, &node.body)
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
        plan ? petta_plan_child(plan, 3u) : NULL, depth, tail, out);
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
    case OEM_N_PRIM:
        if (!oem_emit_element_goals(compile, &node, &first_arg))
            return false;
        return oem_emit(compile, (OemStep){
                            .kind = node.kind == OEM_N_CALL
                                ? (node.tail ? OEM_S_TAIL : OEM_S_CALL)
                                : OEM_S_PRIM,
                            .op = node.op, .relation = node.relation,
                            .first_arg = first_arg, .arg_count = node.count,
                            .pattern = node.exposed,
                        }, NULL);
    case OEM_N_MATCH:
        return oem_emit_goals(compile, node.value) &&
            oem_emit(compile, (OemStep){
                         .kind = OEM_S_MATCH, .pattern = node.pattern,
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
                            .kind = OEM_S_HOST, .op = node.op,
                            .relation = node.relation,
                            .value = node.value, .pattern = node.exposed,
                            .target = node.host_fast
                                ? node.host_fast : OEM_HOST_APPLY,
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
        if (!oem_emit_element_goals(compile, &node, &first_arg) ||
            !oem_emit(compile, (OemStep){
                          .kind = OEM_S_TEST, .op = node.op,
                          .first_arg = first_arg, .arg_count = node.count,
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
                         .value = compile->nodes[node.value].exposed,
                     }, NULL) &&
            oem_emit_tail(compile, node.body);
    case OEM_N_FAIL:
        return oem_emit(compile, (OemStep){.kind = OEM_S_FAIL}, NULL);
    case OEM_N_CALL:
        if (node.tail)
            return oem_emit_goals(compile, index);
        /* fallthrough */
    case OEM_N_VALUE:
    case OEM_N_PRIM:
    case OEM_N_HOST:
    case OEM_N_APPLY:
        return oem_emit_goals(compile, index) &&
            oem_emit(compile, (OemStep){.kind = OEM_S_RET}, NULL);
    }
    return oem_reject(compile, "unknown construct");
}

/* The relational occurrences of the head, as calls in source order whose
 * values unify with the query subterms kept for them.  Their arguments are
 * head data: variables and structure without further occurrences. */
static bool oem_compile_relational_occurrences(OemCompile *compile) {
    for (uint32_t index = 0u; index < compile->pending_len; index++) {
        Atom *call = compile->pending[index].call;
        uint32_t count = call->expr.len - 1u;
        uint32_t *args = malloc(sizeof(*args) * (count ? count : 1u));
        if (!args)
            return oem_reject(compile, "out of memory");
        bool ok = true;
        for (uint32_t arg = 0u; ok && arg < count; arg++) {
            Atom *source = call->expr.elems[arg + 1u];
            ok = oem_head_data(compile, source, 0u)
                ? oem_lower_pattern(compile, source, NULL, 0u, &args[arg])
                : oem_reject(compile, "nested relational occurrence");
        }
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
        return oem_lower_pattern(compile, expr, NULL, depth + 1u,
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
    if (item->kind != OEM_T_BUILD)
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
    if (item->kind != OEM_T_BUILD)
        return;
    for (uint32_t index = 0u; index < item->count; index++)
        oem_template_reads(program,
                           program->template_children[item->first + index],
                           seen, celled);
}

/* A step's result slot when the region may store the result there: a
 * primitive's, a bind's, or a host step's the region may decide, whose
 * pattern is one slot.  UINT32_MAX otherwise. */
static uint32_t oem_step_result_slot(const CettaOpenEquationProgram *program,
                                     const OemStep *step) {
    bool storing = step->kind == OEM_S_PRIM || step->kind == OEM_S_BIND ||
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
                step->kind == OEM_S_HOST)
                oem_template_reads(program, step->value, seen, celled);
            if (step->kind != OEM_S_RET && step->kind != OEM_S_FAIL &&
                step->kind != OEM_S_TEST && result == UINT32_MAX)
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
            if (step->kind == OEM_S_TEST) {
                /* Both branches continue from what this path has seen; the
                 * one taken when the test fails starts at `target` and
                 * keeps this path's record. */
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
                                 const CettaOpenEquationHost *host,
                                 Atom *lhs, Atom *rhs,
                                 const PettaPlanNode *rhs_plan,
                                 const char **reason) {
    OemCompile compile = {.program = program, .host = host,
                          .lhs = lhs, .rhs = rhs};
    OemEquation equation = {
        .first_op = program->op_len,
        .first_step = program->step_len,
    };
    uint32_t arity = lhs->expr.len - 1u;
    compile.register_count = arity;
    bool ok = true;
    if (petta_semantics_contains_cons_constraint(lhs) ||
        petta_semantics_contains_cons_constraint(rhs))
        ok = oem_reject(&compile, "list pattern");
    ok = ok && oem_register_vars(&compile, lhs, 0u) &&
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
    equation.register_count = compile.register_count;
    equation.slot_count = compile.slot_count;
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
    return ok;
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
        if (!candidates[index].rhs_plan) {
            *reason = "equation has no plan";
            ok = false;
            break;
        }
        ok = oem_compile_equation(program, host, lhs,
                                  equation->expr.elems[2],
                                  candidates[index].rhs_plan, reason);
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
                (step->kind == OEM_S_HOST && step->target >= OEM_HOST_PURE))
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
    program->refs = 1u;
    program->space = space;
    program->token = space_program_token(space);
    program->spelling = symbol_intern_cstr(g_symbols, "_");
    uint32_t entry = 0u;
    bool ok = oem_relation_index(program, head, arity, &entry) && entry == 0u;
    for (uint32_t index = 0u; ok && index < program->relation_len; index++)
        ok = oem_compile_relation(program, petta, index, host, &reason);
    if (!ok) {
        if (reason_out)
            *reason_out = reason ? reason : "compile";
        cetta_open_equation_program_release(program);
        return NULL;
    }
    program->entry_relays = oem_relation_relays(program, entry);
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
    uint32_t depth;
} OemCont;

/* A match's rows, snapshotted in the region when the match ran; `arena`
 * is the identity of the arena that holds the block. */
typedef struct {
    uint32_t len;
    uint32_t arena;
    Atom *rows[];
} OemRows;

/* The cells a host goal left the region through, in the order of the
 * variables it went out with. */
typedef struct {
    uint32_t count;
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
} OemFrame;

#define OEM_MATCH_FRAME UINT32_MAX
/* A host goal's frame: its `cont` resumes after the HOST step. */
#define OEM_HOST_FRAME (UINT32_MAX - 1u)
/* A resumption: its `cont` is where resumed code continues, run as soon as
 * the frame is reached. */
#define OEM_RESUME_FRAME (UINT32_MAX - 2u)

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
    /* Minor collections declined since the last that copied: each doubles
     * the growth the region waits for before the next is tried. */
    uint32_t collect_declines;
    /* Terms one step builds and drops before the next (a ground add-atom's
     * call), outside the region so they leave nothing behind in it;
     * initialized on first use. */
    Arena scratch;
    bool scratch_ready;
    /* The variable naming cell i is the same immutable atom every time the
     * cell index is reused, so it lives for the cursor, outside the region
     * that backtracking resets. */
    Arena names;
    Atom **cell_names;
    uint32_t cell_name_len, cell_name_cap;
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
    uint64_t *version_epochs;
    uint32_t poll;
    CettaOpenEquationHandoff handoff;
    /* The depth bound failed an activation whose head matched. */
    bool bound_cut;
    /* Cell bindings made so far; an attempt that failed after one of its own
     * bindings is classified apart from one that failed before any. */
    uint64_t binds;
    /* The region's size at which the next collection runs. */
    size_t collect_after;
    /* The variables of the row being copied for a match. */
    OemFreshVar *fresh_vars;
    uint32_t fresh_var_len, fresh_var_cap;
    /* The newest HOST step's goal, destination and variables, in the
     * answer arena; valid until the next step. */
    Atom *host_goal;
    Atom *host_destination;
    Atom **host_vars;
    uint32_t host_var_count, host_var_cap;
    const PettaPlanNode *host_plan;
    CettaOpenEquationHostMode host_mode;
    /* The multiplicity of the answer being published, and whether a count
     * fold gave it (cetta_open_equation_cursor_answer_weight). */
    uint64_t answer_weight;
    bool answer_folded;
    /* The host's result of a ground add-atom, kept once for the cursor:
     * every later equal result shares it, so a binder it fills keeps no
     * region atom alive. */
    Atom *admitted;
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
    /* Within the body runner only: a match counted its rows, and the step
     * after it runs once for all of them. */
    OEM_RUN_FOLDED,
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

static bool oem_bind(CettaOpenEquationCursor *cursor, uint32_t index,
                     Atom *value) {
    uint32_t newest = cursor->frame_len
        ? cursor->frames[cursor->frame_len - 1u].cell_mark : 0u;
    if (index < newest) {
        if (!oem_reserve((void **)&cursor->trail, &cursor->trail_cap,
                         cursor->trail_len + 1u, sizeof(*cursor->trail)))
            return false;
        cursor->trail[cursor->trail_len++] = index;
        cursor->stats.trail_writes++;
    }
    cursor->cells[index] = value;
    cursor->binds++;
    return true;
}

/* Does cell `index` occur in `atom`? */
static bool oem_occurs(CettaOpenEquationCursor *cursor, uint32_t index,
                       Atom *atom, bool *failed) {
    uint32_t len = 0u;
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
            if (other == index)
                return true;
            continue;
        }
        if (current->kind != ATOM_EXPR || !atom_has_vars(current))
            continue;
        if (!oem_reserve((void **)&cursor->walk, &cursor->walk_cap,
                         len + current->expr.len, sizeof(*cursor->walk))) {
            *failed = true;
            return false;
        }
        for (CettaExprIndex child = 0u; child < current->expr.len; child++)
            cursor->walk[len++] = current->expr.elems[child];
    }
    return false;
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
        if (a->kind == ATOM_EXPR && b->kind == ATOM_EXPR) {
            if (a->expr.len != b->expr.len)
                return OEM_UNIFY_FAIL;
            if (!atom_has_vars(a) && !atom_has_vars(b)) {
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
    for (uint32_t index = 0u; index < item->count; index++) {
        children[index] = oem_instantiate_in(
            cursor, arena, program, locals,
            program->template_children[item->first + index]);
        if (!children[index])
            return NULL;
    }
    return atom_expr(arena, children, item->count);
}

static Atom *oem_instantiate(CettaOpenEquationCursor *cursor,
                             const CettaOpenEquationProgram *program,
                             Atom **locals, uint32_t template_index) {
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
    term = oem_deref(cursor, term);
    uint32_t cell = 0u;
    if (oem_is_cell(cursor, term, &cell) || depth > OEM_MAX_DEPTH) {
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
    while (cursor->trail_len > frame->trail_mark)
        cursor->cells[cursor->trail[--cursor->trail_len]] = NULL;
    while (cursor->store_len > frame->store_mark) {
        const OemStore *store = &cursor->stores[--cursor->store_len];
        store->locals[store->slot] = NULL;
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
        : frame->relation >= OEM_RESUME_FRAME ? 0u
        : frame->program->relations[frame->relation].arity;
}

static void oem_pop_frame(CettaOpenEquationCursor *cursor) {
    cursor->frame_len--;
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

/* Whether the cursor's store holds `atom`: either generation of the
 * region.  What it holds is copied out of the cursor, which frees both
 * generations when it closes. */
static inline bool oem_region_owns(const CettaOpenEquationCursor *cursor,
                                   const Atom *atom) {
    return atom->arena_id == cursor->region.identity ||
        (cursor->old_ready && atom->arena_id == cursor->old.identity);
}

/* The answer's variable for the unbound cell `index`: the caller's own
 * variable for a query cell, otherwise one per cell and answer. */
static Atom *oem_answer_cell(CettaOpenEquationCursor *cursor, uint32_t index) {
    if (index < cursor->query_var_count)
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
    fresh = atom_var_with_spelling(cursor->answer_arena,
                                   cursor->program->spelling, fresh_var_id());
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
 * (`operand` false) it is built in the answer arena: region structure is
 * copied, atoms owned elsewhere are shared, and each unbound cell becomes
 * the answer's variable for it.  With `ground`, storage that outlives the
 * host's answer arena, a ground expression that has proven long-lived, by
 * surviving a collection into the old generation or by being exported a
 * second time, none of whose children is the answer arena's, is copied there
 * instead, once: the expression keeps its copy in `name_key`, as a
 * collection's forwarding does, so the next export shares it, and so does
 * the region's next collection.  An expression exported the first time is
 * copied into the answer arena, which the host reclaims after the goal, and
 * marked; the stable storage the host does not reclaim while the cursor
 * lives.  For a grounded operation's operand (`operand` true) it
 * is built in the region, sharing whatever holds no cell, and an unbound
 * cell stays the cell's own variable: the operation sees a variable, as it
 * would in equation search.  Iterative, so deep lists cost no C stack. */
static inline __attribute__((always_inline)) Atom *oem_resolve_in(
    CettaOpenEquationCursor *cursor, Atom *root, bool operand, Arena *arena,
    Arena *ground) {
    uint32_t depth = 0u;
    uint32_t results = 0u;
    Atom *atom = root;
    uint32_t old_identity =
        ground && cursor->old_ready ? cursor->old.identity : 0u;
    for (;;) {
        atom = oem_deref(cursor, atom);
        uint32_t index = 0u;
        Atom *result = NULL;
        if (oem_is_cell(cursor, atom, &index)) {
            result = operand ? atom : oem_answer_cell(cursor, index);
        } else if ((operand || !oem_region_owns(cursor, atom)) &&
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
                return NULL;
            if (depth == 0u)
                return result;
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
            results = item->base;
            depth--;
        }
    }
}

/* One copy of the resolver per use, each with its mode fixed. */
static Atom *oem_resolve_answer(CettaOpenEquationCursor *cursor, Atom *root) {
    return oem_resolve_in(cursor, root, false, cursor->answer_arena, NULL);
}

static Atom *oem_resolve_operand(CettaOpenEquationCursor *cursor,
                                 Atom *root) {
    return oem_resolve_in(cursor, root, true, &cursor->region, NULL);
}

static Atom *oem_resolve_stable(CettaOpenEquationCursor *cursor, Atom *root) {
    return oem_resolve_in(cursor, root, false, cursor->answer_arena,
                          cursor->runtime.stable);
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
    CettaOpenEquationProgram *program = cursor->runtime.current(
        cursor->runtime.context, space, head, arity);
    if (!program || program->space != space || program->relation_len == 0u ||
        program->relations[0].head != head ||
        program->relations[0].arity != arity ||
        !space_program_token_is_current(program->token) ||
        !oem_reserve((void **)&cursor->versions, &cursor->version_cap,
                     cursor->version_len + 1u, sizeof(*cursor->versions)))
        return NULL;
    uint32_t epochs = cursor->version_cap;
    uint64_t *grown = realloc(cursor->version_epochs,
                              sizeof(*grown) * (epochs ? epochs : 1u));
    if (!grown)
        return NULL;
    cursor->version_epochs = grown;
    program->refs++;
    cursor->versions[cursor->version_len] = program;
    cursor->version_epochs[cursor->version_len++] = cursor->authority_epoch;
    return program;
}

/* A test or primitive the integer path does not cover (a float, a bigint,
 * a symbol, structure or variable under `==`, an int64 overflow, a zero
 * divisor, an unbound operand) is the shared grounded operation on the
 * resolved operands, with the machine's reading of its result: a raised
 * numeric error, empty for failure, a truth value for a test, a value for
 * a primitive. */
static OemRun oem_host_arithmetic(CettaOpenEquationCursor *cursor,
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
    Atom *head = atom_symbol_id(&cursor->region, step->op);
    Atom *result = head
        ? grounded_dispatch(&cursor->region, head, args, 2u) : NULL;
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
static bool oem_row_may_unify(Atom *pattern, Atom *row, uint32_t depth) {
    if (pattern->kind == ATOM_VAR || row->kind == ATOM_VAR)
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
        if (!oem_row_may_unify(pattern->expr.elems[child],
                               row->expr.elems[child], depth + 1u))
            return false;
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
                              Atom *pattern, Atom *query, uint64_t *count_out) {
    uint64_t count = 0u;
    CettaIndex examined = 0u;
    if (space_match_count_flat_linear64(space, &cursor->region, query,
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
        if (!candidate || !oem_row_may_unify(query, candidate, 0u))
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
    Atom *pattern = oem_instantiate(cursor, program, locals, step->pattern);
    if (!reference || !pattern)
        return OEM_RUN_HANDOFF;
    reference = oem_deref(cursor, reference);
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
            cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_UNSUPPORTED;
            return OEM_RUN_HANDOFF;
        }
    }
    if (!space)
        return OEM_RUN_FAILED;
    if (space->overlay_base ||
        (space->match_backend.kind != SPACE_ENGINE_NATIVE &&
         space->match_backend.kind != SPACE_ENGINE_NATIVE_CANDIDATE_EXACT) ||
        petta_semantics_is_open_cons_value(pattern)) {
        cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_UNSUPPORTED;
        return OEM_RUN_HANDOFF;
    }
    Atom *query = oem_resolve_mode(cursor, pattern, true);
    if (!query)
        return OEM_RUN_HANDOFF;
    if (cursor->runtime.count_only && !cont &&
        program->steps[pc + 1u].kind == OEM_S_RET) {
        uint64_t rows = 0u;
        OemRun counted = oem_match_count(cursor, space, pattern, query,
                                         &rows);
        if (counted != OEM_RUN_CALLED)
            return counted;
        if (rows == 0u)
            return OEM_RUN_FAILED;
        cursor->answer_weight = rows;
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
    OemRows *rows = count
        ? arena_alloc(&cursor->region,
                      sizeof(*rows) + sizeof(Atom *) * (size_t)count)
        : NULL;
    uint32_t len = 0u;
    for (CettaIndex index = 0u; rows && index < count; index++) {
        Atom *candidate = space_match_candidate_at64(space, indices[index]);
        if (candidate && oem_row_may_unify(query, candidate, 0u))
            rows->rows[len++] = candidate;
    }
    free(indices);
    if (count && !rows)
        return OEM_RUN_HANDOFF;
    if (len == 0u)
        return OEM_RUN_FAILED;
    rows->len = len;
    rows->arena = cursor->region.identity;
    OemCont *resume = arena_alloc(&cursor->region, sizeof(*resume));
    Atom **args = arena_alloc(&cursor->region, sizeof(*args));
    if (!resume || !args ||
        !oem_push_frame(cursor, program, OEM_MATCH_FRAME, args, resume,
                        depth))
        return OEM_RUN_HANDOFF;
    *resume = (OemCont){
        .parent = cont, .program = program, .locals = locals,
        .local_count = local_count, .pc = pc + 1u, .depth = depth,
    };
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

/* A HOST step: the goal and its destination leave the region with one
 * variable per unbound cell they mention (a call variable stands for
 * itself); the frame keeps those cells, and the host passes the variables
 * back with each answer, so the cursor holds no pointer into the host's
 * store. */
static __attribute__((noinline)) OemRun oem_host_step(
                            CettaOpenEquationCursor *cursor,
                            const CettaOpenEquationProgram *program,
                            uint32_t pc, Atom **locals, uint32_t local_count,
                            const OemCont *cont, uint32_t depth,
                            const OemStep *step) {
    oem_forget_answer_vars(cursor);
    Atom *goal = oem_instantiate(cursor, program, locals, step->value);
    Atom *destination = oem_instantiate(cursor, program, locals,
                                        step->pattern);
    if (!goal || !destination)
        return OEM_RUN_HANDOFF;
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
        cursor->host_vars[index] = oem_answer_cell(cursor, cells[index]);
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
    cursor->host_plan = program->host_plans[step->relation];
    /* An application node's elements were evaluated here; it has no
     * source plan, and the host applies its head to their values. */
    cursor->host_mode = step->target == OEM_HOST_COUNTED
        ? CETTA_OPEN_EQUATION_HOST_COUNTED
        : program->host_plans[step->relation] == NULL
            ? CETTA_OPEN_EQUATION_HOST_APPLY
            : CETTA_OPEN_EQUATION_HOST_SOLVE;
    cursor->stats.host_goals++;
    return OEM_RUN_HOST;
}

/* Fill a slot at its first occurrence.  While a frame newer than the
 * activation is live, restoring it resumes the activation's code before the
 * store, so the store is trailed and the restore empties the slot again: no
 * slot outlives the path that stored it. */
static bool oem_store_slot(CettaOpenEquationCursor *cursor, Atom **locals,
                           uint32_t slot, Atom *value) {
    uintptr_t birth = ((const uintptr_t *)locals)[-1];
    if (cursor->frame_len > birth) {
        if (!oem_reserve((void **)&cursor->stores, &cursor->store_cap,
                         cursor->store_len + 1u, sizeof(*cursor->stores)))
            return false;
        cursor->stores[cursor->store_len++] =
            (OemStore){.locals = locals, .slot = slot};
    }
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

/* A dynamic call's evaluated elements: data when the head's value cannot
 * be applied, built over the elements' values; otherwise the host's. */
static OemRun oem_apply_step(CettaOpenEquationCursor *cursor,
                             const CettaOpenEquationProgram *program,
                             Atom **locals, const OemStep *step) {
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
    if (data && symbol &&
        !oem_symbol_application_inert(cursor, program, data))
        return OEM_RUN_HOST;
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
    call = call ? oem_resolve_in(cursor, call, true, scratch, NULL) : NULL;
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

/* The region decides a HOST step its fast path covers.  An expression
 * whose head has a value no call can have is data, built over its values.
 * A type-pure operation runs the search machine's direct path once every
 * argument is a value: the operation's own implementation, an empty result
 * a failure, a truth value in the host's spelling.  The result meets the
 * step's destination.  CALLED continues after the step; what this does not
 * decide (a head that may be called, an unbound argument, no result, an
 * error, which the host's dialect treats in its own way) is the host's, as
 * HOST. */
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
    if (step->target == OEM_HOST_APPLY ||
        step->target == OEM_HOST_DATA_HEAD)
        return oem_apply_step(cursor, program, locals, step);
    const OemTemplate *goal = &program->templates[step->value];
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
    enum { OEM_PURE_INLINE_ARGS = 8u };
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
        if (atom_has_vars(arg))
            return OEM_RUN_HOST;
        args[index] = arg;
    }
    Atom *head = atom_symbol_id(&cursor->region, step->op);
    Atom *result = head
        ? grounded_dispatch(&cursor->region, head, args, count) : NULL;
    if (!result || atom_is_error(result))
        return OEM_RUN_HOST;
    if (atom_is_petta_no_result(result))
        return OEM_RUN_FAILED;
    bool truth = false;
    if (petta_semantics_truth_value(result, &truth)) {
        result = cursor->runtime.boolean_value
            ? cursor->runtime.boolean_value(cursor->runtime.context,
                                            &cursor->region, truth)
            : petta_semantics_boolean_value(&cursor->region, truth);
        if (!result)
            return OEM_RUN_HANDOFF;
    }
    OemUnify unified = oem_meet(cursor, program, locals, step, result);
    if (unified == OEM_UNIFY_ERROR)
        return OEM_RUN_HANDOFF;
    return unified == OEM_UNIFY_OK ? OEM_RUN_CALLED : OEM_RUN_FAILED;
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
            /* The destination already holds the output. */
            if (!cont)
                return oem_publish(cursor, cursor->destination, value_out);
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
            Atom **args = arena_alloc(
                &cursor->region,
                sizeof(*args) * (step->arg_count ? step->arg_count : 1u));
            if (!args)
                return OEM_RUN_HANDOFF;
            for (uint32_t index = 0u; index < step->arg_count; index++) {
                args[index] = oem_instantiate(cursor, program, locals,
                    program->step_args[step->first_arg + index]);
                if (!args[index])
                    return OEM_RUN_HANDOFF;
            }
            const OemCont *next = cont;
            if (step->kind == OEM_S_CALL) {
                OemCont *record = arena_alloc(&cursor->region,
                                              sizeof(*record));
                if (!record)
                    return OEM_RUN_HANDOFF;
                *record = (OemCont){
                    .parent = cont, .program = program, .locals = locals,
                    .local_count = local_count, .pc = pc + 1u,
                    .pattern = step->pattern, .depth = depth,
                };
                next = record;
            }
            /* A call enters the equations current at its entry: the running
             * code's own, unless something changed since that code was
             * entered, then the host's current program for the relation. */
            const CettaOpenEquationProgram *target = program;
            uint32_t relation = step->relation;
            if (!oem_version_current(cursor, program)) {
                const OemRelation *callee = &program->relations[relation];
                target = oem_enter_current(cursor, program->space,
                                           callee->head, callee->arity);
                if (!target) {
                    cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_UNSUPPORTED;
                    return OEM_RUN_HANDOFF;
                }
                relation = 0u;
            }
            return oem_push_frame(cursor, target, relation, args, next,
                                  depth + 1u)
                ? OEM_RUN_CALLED : OEM_RUN_HANDOFF;
        }
        case OEM_S_PRIM:
        case OEM_S_TEST: {
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
                OemRun run = oem_host_arithmetic(cursor, step, left, right,
                                                 &value, &truth, value_out);
                if (run != OEM_RUN_CALLED)
                    return run;
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
            if (step->target >= OEM_HOST_PURE) {
                OemRun run = oem_fast_host_step(cursor, program, locals,
                                                step, value_out);
                if (run == OEM_RUN_CALLED) {
                    pc++;
                    continue;
                }
                if (run != OEM_RUN_HOST)
                    return run;
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
        if (op->kind != OEM_M_ATOM && op->kind != OEM_M_EXPR)
            continue;
        Atom *term = regs[op->reg];
        uint32_t cell = 0u;
        if (term)
            term = oem_deref(cursor, term);
        bool open = !term || oem_is_cell(cursor, term, &cell);
        if (op->kind == OEM_M_ATOM) {
            if (!open && !atom_eq(term, op->literal))
                return false;
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

static OemRun oem_activate(CettaOpenEquationCursor *cursor,
                           const CettaOpenEquationProgram *program,
                           uint32_t equation, Atom **args, uint32_t arity,
                           const OemCont *cont, OemEntry *entry) {
    const OemEquation *eq = &program->equations[equation];
    /* A slot vector is preceded by the number of frames live when its
     * activation began, which tells a store whether a later frame could
     * resume the activation's code before the store. */
    uintptr_t *header = arena_alloc(
        &cursor->region,
        sizeof(*header) + sizeof(Atom *) * (eq->slot_count ? eq->slot_count
                                                          : 1u));
    Atom **locals = header ? (Atom **)(header + 1) : NULL;
    if (header)
        header[0] = cursor->frame_len;
    if (!locals ||
        !oem_reserve((void **)&cursor->regs, &cursor->reg_cap,
                     eq->register_count ? eq->register_count : 1u,
                     sizeof(*cursor->regs)))
        return OEM_RUN_HANDOFF;
    Atom **regs = cursor->regs;
    memset(locals, 0, sizeof(*locals) * eq->slot_count);
    for (uint32_t index = 0u; index < arity; index++)
        regs[index] = args[index];
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
        }
    }
    Atom *destination = NULL;
    bool constrain = false;
    if (eq->dest_kind != OEM_DEST_NONE) {
        destination = cont
            ? oem_instantiate(cursor, cont->program, cont->locals, cont->pattern)
            : cursor->destination;
        if (!destination)
            return OEM_RUN_HANDOFF;
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
     * and a slot the body never mentions stays empty; every other
     * unassigned slot is a cell. */
    const uint8_t *stores = &program->slot_stores[eq->first_slot_store];
    for (uint32_t slot = 0u; slot < eq->slot_count; slot++) {
        if (!locals[slot] && !stores[slot]) {
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
            ? (cursor->runtime.boolean_value
                   ? cursor->runtime.boolean_value(cursor->runtime.context,
                                                   &cursor->region, true)
                   : petta_semantics_boolean_value(&cursor->region, true))
            : NULL;
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
    size_t cap;
    size_t len;
    /* Cells a live term mentions: `reached` marks them, `pending` lists the
     * bound ones whose values are still to be traced; `remap` is a reached
     * cell's index after compaction. */
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

static size_t oem_collect_hash(const void *key, size_t cap) {
    uint64_t h = (uint64_t)(uintptr_t)key >> 4;
    h *= UINT64_C(0x9E3779B97F4A7C15);
    return (size_t)(h >> 20) & (cap - 1u);
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

static bool oem_collect_put(OemCollect *collect, const void *key,
                            void *value) {
    if ((collect->len + 1u) * 2u > collect->cap) {
        size_t cap = collect->cap ? collect->cap * 2u : 1024u;
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

static bool oem_collect_young_spans(const Arena *region,
                                    OemCollect *collect) {
    uint32_t count = 0u;
    for (const ArenaBlock *block = region->head; block; block = block->next)
        count++;
    collect->young = malloc(sizeof(*collect->young) * (count ? count : 1u));
    if (!collect->young)
        return false;
    for (const ArenaBlock *block = region->head; block; block = block->next) {
        if (block->used == 0u)
            continue;
        collect->young[collect->young_len++] = (OemSpan){
            .start = (uintptr_t)block->data,
            .end = (uintptr_t)block->data + block->used,
        };
    }
    qsort(collect->young, collect->young_len, sizeof(*collect->young),
          oem_span_order);
    return true;
}

/* Whether the region holds the continuation or slot vector at `ptr`. */
static bool oem_collect_in_region(const OemCollect *collect,
                                  const void *ptr) {
    uintptr_t at = (uintptr_t)ptr;
    uint32_t low = 0u;
    uint32_t high = collect->young_len;
    while (low < high) {
        uint32_t mid = low + (high - low) / 2u;
        if (at < collect->young[mid].start)
            high = mid;
        else if (at >= collect->young[mid].end)
            low = mid + 1u;
        else
            return true;
    }
    return false;
}

/* Whether the collection moves the continuation or slot vector at `ptr`: a
 * major collection moves everything it reaches, a minor one only what the
 * region holds.  An older object stays where it is: it was made before every
 * region object and names none of them, except through a slot stored since
 * its promotion. */
static bool oem_collect_moves(const OemCollect *collect, const void *ptr) {
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
           !collect->revocable[cell])
        atom = cursor->cells[cell];
    return atom;
}

static void oem_collect_reach(CettaOpenEquationCursor *cursor,
                              OemCollect *collect, uint32_t cell) {
    if (collect->reached[cell])
        return;
    collect->reached[cell] = 1u;
    if (cursor->cells[cell])
        collect->pending[collect->pending_len++] = cell;
}

/* A region expression the collection has visited: its `name_key`, null on
 * every expression but one an export has marked or copied to stable storage,
 * holds this marker while tracing, and the expression's copy once it is
 * copied.  An export's stable copy serves as that copy.  The old region is
 * the cursor's alone and is freed by the collection (a failed collection
 * ends the cursor), so marking it needs no side table. */
static Atom oem_traced_mark;

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
        if (atom->arena_id == collect->from_identity)
            collect->reached_young += sizeof(Atom) +
                sizeof(Atom *) * (size_t)atom->expr.len;
        if (collect->record) {
            if (!oem_reserve((void **)&collect->marked, &collect->marked_cap,
                             collect->marked_len + 1u,
                             sizeof(*collect->marked)))
                return false;
            collect->marked[collect->marked_len++] = atom;
            if (atom->arena_id == collect->from_old_identity)
                collect->reached_old += sizeof(Atom) +
                    sizeof(Atom *) * (size_t)atom->expr.len;
        }
        if (atom->expr.len > UINT32_MAX - len ||
            !oem_reserve((void **)&cursor->walk, &cursor->walk_cap,
                         len + (uint32_t)atom->expr.len,
                         sizeof(*cursor->walk)))
            return false;
        for (CettaExprIndex child = 0u; child < atom->expr.len; child++)
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
            result = collect->reached[cell]
                ? cursor->cell_names[collect->remap[cell]] : NULL;
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
            /* Copied below, once its children are. */
            result = NULL;
        } else if ((result = oem_collect_find(collect, atom)) != NULL) {
        } else {
            result = atom_deep_copy(collect->to, atom);
            if (!result || !oem_collect_put(collect, atom, result))
                return NULL;
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
            results = item->base;
            depth--;
        }
    }
}

/* A slot vector's copy: an activation's (`locals`) keeps the header word
 * before it, a frame's arguments have none. */
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
            if (!slots[index])
                continue;
            Atom *moved = oem_collect_atom(cursor, collect, slots[index]);
            if (!moved) {
                *ok = false;
                return NULL;
            }
            slots[index] = moved;
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
     * are older still; their slot vectors are renamed in place. */
    for (const OemCont *at = kept; *ok && at; at = at->parent) {
        if (oem_collect_find(collect, at))
            break;
        if (!oem_collect_put(collect, at, (void *)at)) {
            *ok = false;
            break;
        }
        (void)oem_collect_slots(cursor, collect, at->locals, at->local_count,
                                true, ok);
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
    return walked > OEM_COLLECT_MIN_BYTES ? walked : OEM_COLLECT_MIN_BYTES;
}

/* Whether a region grown past `collect_after` is collected now.  Each
 * choice frame, and each binding the trail would undo, is state a restore
 * returns to, and a collection walks all of it whatever else it reaches.
 * Below the single-generation bound a collection therefore waits until the
 * region is twice that state: a region held by dense choice frames is mostly
 * what they will restore, and backtracking, not copying, reclaims it. */
static bool oem_collect_due(CettaOpenEquationCursor *cursor, size_t live) {
    size_t held = (size_t)cursor->frame_len * sizeof(*cursor->frames) +
        (size_t)cursor->trail_len * sizeof(*cursor->trail);
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
    size_t cell_room = old_cells ? old_cells : 1u;
    uint32_t fixed = major ? 0u
        : cursor->old_cells < old_cells ? cursor->old_cells : old_cells;
    OemCollect collect = {
        .from_identity = cursor->region.identity,
        .from_old_identity =
            major && cursor->old_ready ? cursor->old.identity : 0u,
        .detach_identity = detach_identity,
        .reached = calloc(cell_room, sizeof(uint8_t)),
        .pending = calloc(cell_room, sizeof(uint32_t)),
        .remap = calloc(cell_room, sizeof(uint32_t)),
        .revocable = calloc(cell_room, sizeof(uint8_t)),
        .minor = !major,
        .record = detach_identity == 0u,
    };
    uint32_t *before = calloc(cell_room + 1u, sizeof(uint32_t));
    uint32_t *kept_before = calloc(
        (size_t)cursor->trail_len + 1u, sizeof(uint32_t));
    Atom ***args = calloc(frames ? frames : 1u, sizeof(*args));
    OemRows **rows = calloc(frames ? frames : 1u, sizeof(*rows));
    OemHostCells **hosts = calloc(frames ? frames : 1u, sizeof(*hosts));
    const OemCont **conts = calloc(frames ? frames : 1u, sizeof(*conts));
    Atom **cells = calloc(cell_room, sizeof(*cells));
    bool ok = collect.reached && collect.pending && collect.remap &&
        collect.revocable && before && kept_before && args && rows &&
        hosts && conts && cells &&
        oem_collect_young_spans(&cursor->region, &collect);
    /* A frame's restore undoes the trail from its mark on; the oldest
     * frame's mark is the least. */
    for (uint32_t index = frames ? cursor->frames[0].trail_mark
                                 : cursor->trail_len;
         ok && index < cursor->trail_len; index++)
        collect.revocable[cursor->trail[index]] = 1u;

    /* Trace.  Old atoms may name any old cell, which a minor collection
     * therefore keeps; the call's cells stay cells, which its answer
     * reads. */
    for (uint32_t index = 0u; ok && index < fixed; index++)
        oem_collect_reach(cursor, &collect, index);
    for (uint32_t index = 0u; ok && index < cursor->query_var_count; index++) {
        uint32_t cell = 0u;
        if (oem_is_cell(cursor, cursor->query_cells[index], &cell))
            oem_collect_reach(cursor, &collect, cell);
    }
    for (uint32_t index = 0u; ok && index < frames; index++) {
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
    ok = ok && oem_trace_atom(cursor, &collect, cursor->destination);
    while (ok && collect.pending_len > 0u) {
        uint32_t cell = collect.pending[--collect.pending_len];
        ok = oem_trace_atom(cursor, &collect, cursor->cells[cell]);
    }
    /* A major collection reclaims only the old generation's dead part; when
     * nearly all of it is still reached, copying it would only double it
     * for a moment.  The marks come off, the next major waits for the old
     * generation to double again, and a minor collection runs instead.  A
     * detaching collection always copies, since it moves borrowed atoms. */
    size_t old_live = cursor->old_ready
        ? arena_accounted_live_bytes(&cursor->old) : 0u;
    if (ok && major && collect.record &&
        collect.reached_old >= old_live - old_live / 4u) {
        oem_collect_decline(&collect, before, kept_before, args, rows, hosts,
                            conts, cells);
        cursor->major_after = 2u * old_live;
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
    uint32_t live = 0u;
    for (uint32_t index = 0u; ok && index < old_cells; index++) {
        before[index] = live;
        if (collect.reached[index])
            collect.remap[index] = live++;
    }
    if (ok)
        before[old_cells] = live;
    free(collect.keys);
    free(collect.values);
    collect.keys = NULL;
    collect.values = NULL;
    collect.cap = 0u;
    collect.len = 0u;

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
    for (uint32_t index = 0u; ok && index < frames; index++) {
        const OemFrame *frame = &cursor->frames[index];
        uint32_t arity = oem_frame_arity(frame);
        args[index] = oem_collect_slots(cursor, &collect, frame->args, arity,
                                        false, &ok);
        conts[index] = ok ? oem_collect_cont(cursor, &collect, frame->cont,
                                             &ok) : NULL;
        if (ok && oem_frame_is_host(frame)) {
            OemHostCells *copy = arena_alloc(
                to_arena, sizeof(*copy) +
                         sizeof(uint32_t) * (frame->host->count
                                                 ? frame->host->count : 1u));
            ok = copy != NULL;
            if (ok) {
                copy->count = frame->host->count;
                for (uint32_t cell = 0u; cell < copy->count; cell++)
                    copy->cells[cell] =
                        collect.remap[frame->host->cells[cell]];
            }
            hosts[index] = copy;
        }
        /* A match's rows are the space's atoms; only the block moves, and
         * only out of a generation this collection collects. */
        if (ok && oem_frame_is_match(frame)) {
            bool moves = frame->rows->arena == collect.from_identity ||
                (collect.from_old_identity != 0u &&
                 frame->rows->arena == collect.from_old_identity);
            size_t bytes = sizeof(OemRows) +
                sizeof(Atom *) * (size_t)frame->rows->len;
            rows[index] = moves ? arena_alloc(to_arena, bytes) : frame->rows;
            ok = rows[index] != NULL;
            if (ok && moves) {
                memcpy(rows[index], frame->rows, bytes);
                rows[index]->arena = to_arena->identity;
            }
        }
    }
    for (uint32_t index = 0u; ok && index < old_cells; index++) {
        if (!collect.reached[index] || !cursor->cells[index])
            continue;
        cells[collect.remap[index]] =
            oem_collect_atom(cursor, &collect, cursor->cells[index]);
        ok = cells[collect.remap[index]] != NULL;
    }
    Atom *destination = ok
        ? oem_collect_atom(cursor, &collect, cursor->destination) : NULL;
    ok = ok && destination;
    /* The store trail follows its slot vectors; a store into a vector no
     * continuation reaches is dropped with it. */
    uint32_t *stores_before = ok
        ? calloc((size_t)cursor->store_len + 1u, sizeof(uint32_t)) : NULL;
    OemStore *stores = ok
        ? calloc(cursor->store_len ? cursor->store_len : 1u, sizeof(*stores))
        : NULL;
    ok = ok && stores_before && stores;
    uint32_t stores_kept = 0u;
    for (uint32_t index = 0u; ok && index < cursor->store_len; index++) {
        stores_before[index] = stores_kept;
        Atom **copied = oem_collect_find(&collect,
                                         cursor->stores[index].locals);
        if (copied)
            stores[stores_kept++] = (OemStore){
                .locals = copied, .slot = cursor->stores[index].slot};
    }
    if (ok)
        stores_before[cursor->store_len] = stores_kept;
    if (collect.detach)
        atom_deep_copy_session_free(collect.detach);
    free(collect.keys);
    free(collect.values);
    if (!ok) {
        free(collect.young);
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

    /* Commit. */
    uint32_t kept = 0u;
    for (uint32_t index = 0u; index < cursor->trail_len; index++) {
        kept_before[index] = kept;
        uint32_t cell = cursor->trail[index];
        if (collect.reached[cell])
            cursor->trail[kept++] = collect.remap[cell];
    }
    kept_before[cursor->trail_len] = kept;
    /* Every region atom the cursor still reaches has been copied: the
     * region restarts empty, keeping its blocks for what follows. */
    arena_reset(&cursor->region, (ArenaMark){0});
    ArenaMark end = arena_mark(&cursor->region);
    for (uint32_t index = 0u; index < frames; index++) {
        OemFrame *frame = &cursor->frames[index];
        frame->args = args[index];
        if (oem_frame_is_match(frame))
            frame->rows = rows[index];
        if (oem_frame_is_host(frame))
            frame->host = hosts[index];
        frame->cont = conts[index];
        frame->mark = end;
        frame->cell_mark = before[frame->cell_mark];
        frame->trail_mark = kept_before[frame->trail_mark];
        frame->store_mark = stores_before[frame->store_mark];
    }
    cursor->trail_len = kept;
    if (stores_kept)
        memcpy(cursor->stores, stores, sizeof(*stores) * stores_kept);
    cursor->store_len = stores_kept;
    free(stores_before);
    free(stores);
    for (uint32_t index = 0u; index < live; index++)
        cursor->cells[index] = cells[index];
    for (uint32_t index = live; index < old_cells; index++)
        cursor->cells[index] = NULL;
    cursor->cell_len = live;
    for (uint32_t index = 0u; index < cursor->query_var_count; index++)
        cursor->query_cells[index] = cursor->cell_names[
            collect.remap[var_base_id(cursor->query_cells[index]->var_id) - 1u]];
    cursor->destination = destination;
    oem_forget_answer_vars(cursor);
    free(collect.young);
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
        size_t window = promoted > OEM_COLLECT_MIN_BYTES /
                OEM_COLLECT_SURVIVAL_RATIO
            ? OEM_COLLECT_MIN_BYTES
            : promoted * OEM_COLLECT_SURVIVAL_RATIO;
        if (window < OEM_COLLECT_STEADY_BYTES)
            window = OEM_COLLECT_STEADY_BYTES;
        size_t roots = oem_collect_roots(cursor);
        size_t twice = promoted > SIZE_MAX / 2u ? SIZE_MAX : 2u * promoted;
        if (roots > window)
            window = roots;
        if (collect.walked > window)
            window = collect.walked;
        if (twice > window)
            window = twice;
        cursor->collect_after = window;
    }
    arena_release_spare(&cursor->region, cursor->collect_after);
    if (major) {
        size_t survivors = arena_accounted_live_bytes(&cursor->old);
        cursor->major_after = survivors > OEM_MAJOR_MIN_BYTES / 2u
            ? 2u * survivors : OEM_MAJOR_MIN_BYTES;
    }
    cursor->stats.collections++;
    return true;
}

/* Advance `frame` past the equations selection refutes; each counts as the
 * head failure its activation would meet. */
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
static Atom *oem_import(CettaOpenEquationCursor *cursor, Atom *atom,
                        uint32_t depth) {
    if (depth > OEM_MAX_DEPTH)
        return NULL;
    if (atom->kind == ATOM_VAR) {
        for (uint32_t index = 0u; index < cursor->query_var_count; index++) {
            if (cursor->query_vars[index]->var_id == atom->var_id)
                return cursor->query_cells[index];
        }
        return NULL;
    }
    if (atom->kind != ATOM_EXPR || !atom_has_vars(atom))
        return atom;
    Atom **children = arena_alloc(&cursor->region,
                                  sizeof(*children) * atom->expr.len);
    if (!children)
        return NULL;
    for (CettaExprIndex child = 0u; child < atom->expr.len; child++) {
        children[child] = oem_import(cursor, atom->expr.elems[child],
                                     depth + 1u);
        if (!children[child])
            return NULL;
    }
    return atom_expr(&cursor->region, children, atom->expr.len);
}

/* A copy of a stored row in the region with one new cell per distinct
 * variable: a row is universally quantified, so each match of it gets its
 * own variables.  Ground parts are shared. */
static Atom *oem_import_fresh(CettaOpenEquationCursor *cursor, Atom *atom,
                              uint32_t depth) {
    if (depth > OEM_MAX_DEPTH)
        return NULL;
    if (atom->kind == ATOM_VAR) {
        for (uint32_t index = 0u; index < cursor->fresh_var_len; index++) {
            if (cursor->fresh_vars[index].id == atom->var_id)
                return cursor->fresh_vars[index].cell;
        }
        Atom *cell = oem_new_cell(cursor);
        if (!cell ||
            !oem_reserve((void **)&cursor->fresh_vars, &cursor->fresh_var_cap,
                         cursor->fresh_var_len + 1u,
                         sizeof(*cursor->fresh_vars)))
            return NULL;
        cursor->fresh_vars[cursor->fresh_var_len++] =
            (OemFreshVar){.id = atom->var_id, .cell = cell};
        return cell;
    }
    if (atom->kind != ATOM_EXPR || !atom_has_vars(atom))
        return atom;
    Atom **children = arena_alloc(&cursor->region,
                                  sizeof(*children) * atom->expr.len);
    if (!children)
        return NULL;
    for (CettaExprIndex child = 0u; child < atom->expr.len; child++) {
        children[child] = oem_import_fresh(cursor, atom->expr.elems[child],
                                           depth + 1u);
        if (!children[child])
            return NULL;
    }
    return atom_expr(&cursor->region, children, atom->expr.len);
}

/* One row of a match: a fresh copy unified with the pattern.  CALLED when
 * it unifies; the code after the match then runs in the matching
 * activation. */
static __attribute__((noinline)) OemRun oem_try_candidate(
                                CettaOpenEquationCursor *cursor,
                                Atom *pattern, Atom *candidate) {
    cursor->stats.match_candidates++;
    if (petta_semantics_is_open_cons_value(candidate)) {
        cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_UNSUPPORTED;
        return OEM_RUN_HANDOFF;
    }
    cursor->fresh_var_len = 0u;
    Atom *row = atom_has_vars(candidate)
        ? oem_import_fresh(cursor, candidate, 0u) : candidate;
    if (!row)
        return OEM_RUN_HANDOFF;
    OemUnify unified = oem_unify(cursor, pattern, row);
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
        program->relation_len == 0u || program->relations[0].arity != arity)
        return NULL;
    CettaOpenEquationCursor *cursor = calloc(1u, sizeof(*cursor));
    if (!cursor)
        return NULL;
    cursor->program = program;
    program->refs++;
    cursor->answer_arena = answer_arena;
    cursor->collect_after = OEM_COLLECT_MIN_BYTES;
    cursor->major_after = OEM_MAJOR_MIN_BYTES;
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
    for (uint32_t index = 0u; ok && index < arity; index++) {
        imported[index] = oem_import(cursor, args[index], 0u);
        ok = imported[index] != NULL;
    }
    if (ok) {
        cursor->destination = expected
            ? oem_import(cursor, expected, 0u)
            : oem_new_cell(cursor);
        ok = cursor->destination != NULL;
    }
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
    CettaOpenEquationHostMode *mode_out) {
    if (!cursor || !cursor->host_goal)
        return false;
    *goal_out = cursor->host_goal;
    *destination_out = cursor->host_destination;
    *vars_out = cursor->host_vars;
    *var_count_out = cursor->host_var_count;
    *plan_out = cursor->host_plan;
    *mode_out = cursor->host_mode;
    return true;
}

/* A host answer's value in the region: the goal's own variables are the
 * cells they stood for, the call's variables its cells, and any other
 * variable a new cell.  Everything is copied, since the host's store may
 * move. */
static Atom *oem_import_host(CettaOpenEquationCursor *cursor, Atom *atom,
                             Atom *const *host_vars,
                             const OemHostCells *cells, uint32_t depth) {
    if (depth > OEM_MAX_DEPTH)
        return NULL;
    if (atom->kind == ATOM_VAR) {
        for (uint32_t index = 0u; index < cells->count; index++) {
            if (host_vars[index]->var_id == atom->var_id)
                return cursor->cell_names[cells->cells[index]];
        }
        for (uint32_t index = 0u; index < cursor->query_var_count; index++) {
            if (cursor->query_vars &&
                cursor->query_vars[index]->var_id == atom->var_id)
                return cursor->query_cells[index];
        }
        for (uint32_t index = 0u; index < cursor->fresh_var_len; index++) {
            if (cursor->fresh_vars[index].id == atom->var_id)
                return cursor->fresh_vars[index].cell;
        }
        Atom *cell = oem_new_cell(cursor);
        if (!cell ||
            !oem_reserve((void **)&cursor->fresh_vars, &cursor->fresh_var_cap,
                         cursor->fresh_var_len + 1u,
                         sizeof(*cursor->fresh_vars)))
            return NULL;
        cursor->fresh_vars[cursor->fresh_var_len++] =
            (OemFreshVar){.id = atom->var_id, .cell = cell};
        return cell;
    }
    if (atom->kind != ATOM_EXPR || !atom_has_vars(atom))
        return atom_deep_copy(&cursor->region, atom);
    Atom **children = arena_alloc(&cursor->region,
                                  sizeof(*children) * atom->expr.len);
    if (!children)
        return NULL;
    for (CettaExprIndex child = 0u; child < atom->expr.len; child++) {
        children[child] = oem_import_host(cursor, atom->expr.elems[child],
                                          host_vars, cells, depth + 1u);
        if (!children[child])
            return NULL;
    }
    return atom_expr(&cursor->region, children, atom->expr.len);
}

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
    for (uint32_t index = 0u; unified && index < host_count; index++) {
        Atom *imported = oem_import_host(cursor, host_values[index],
                                         host_vars, cells, 0u);
        OemUnify outcome = imported
            ? oem_unify(cursor, cursor->cell_names[cells->cells[index]],
                        imported)
            : OEM_UNIFY_ERROR;
        if (outcome == OEM_UNIFY_ERROR) {
            cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_CAPACITY;
            cursor->query_vars = NULL;
            return false;
        }
        unified = outcome == OEM_UNIFY_OK;
    }
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
        /* Resumption, host and match frames carry the three highest
         * relation values. */
        if (__builtin_expect(frame->relation >= OEM_RESUME_FRAME, 0)) {
            if (oem_frame_is_host(frame)) {
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
            if (frame->next >= frame->rows->len) {
                oem_pop_frame(cursor);
                continue;
            }
            Atom *candidate = frame->rows->rows[frame->next++];
            Atom *pattern = frame->args[0];
            const OemCont *resume = frame->cont;
            if (frame->next >= frame->rows->len)
                oem_pop_frame(cursor);
            run = oem_try_candidate(cursor, pattern, candidate);
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
            uint32_t equation = relation->first_equation + frame->next++;
            Atom **args = frame->args;
            const OemCont *cont = frame->cont;
            uint32_t depth = frame->depth;
            /* The last alternative leaves nothing to return to: the frame
             * goes now, so a deterministic chain of calls keeps one frame. */
            if (frame->next >= relation->equation_count)
                oem_pop_frame(cursor);
            run = oem_activate(cursor, version, equation, args,
                               relation->arity, cont, &entry);
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
    if (cursor->scratch_ready)
        arena_free(&cursor->scratch);
    free(cursor->cell_names);
    if (cursor->epoch)
        cetta_frame_identity_release(cursor->epoch);
    free(cursor->cells);
    free(cursor->trail);
    free(cursor->stores);
    free(cursor->frames);
    free(cursor->fresh_vars);
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

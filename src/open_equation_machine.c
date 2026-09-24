#include "open_equation_machine.h"

#include "binding/frame_identity.h"
#include "grounded.h"
#include "petta_program.h"
#include "petta_semantics.h"
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
} OemStepKind;

typedef struct {
    uint8_t kind;
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

typedef struct {
    uint32_t first_op;
    uint32_t op_count;
    uint32_t register_count;
    uint32_t slot_count;
    uint32_t first_step;
    uint8_t dest_kind;
    uint32_t destination;
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
} OemNodeKind;

typedef struct {
    uint8_t kind;
    bool tail;
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
    if (!oem_data_node(expr, plan))
        return oem_reject(compile, "operation outside the fragment");
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
        if (!test || test->kind != ATOM_EXPR || test->expr.len != 3u ||
            test->expr.elems[0]->kind != ATOM_SYMBOL ||
            !oem_is_test(test->expr.elems[0]->sym_id) || !test_plan ||
            test_plan->role != PETTA_PLAN_STATIC_CALL)
            return oem_reject(compile, "condition outside the fragment");
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
        if (!oem_lower_pattern(compile, expr->expr.elems[1],
                               petta_plan_child(plan, 1u), depth + 1u,
                               &node.pattern) ||
            !oem_build_value(compile, expr->expr.elems[2],
                             petta_plan_child(plan, 2u), depth + 1u,
                             &node.value) ||
            !oem_build_tail(compile, expr->expr.elems[3],
                            petta_plan_child(plan, 3u), depth + 1u,
                            &node.body))
            return false;
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
        if (!oem_build_tail(compile, expr->expr.elems[2],
                            petta_plan_child(plan, 2u), depth + 1u, &body))
            return false;
        for (uint32_t index = bindings->expr.len; index-- > 0u;) {
            Atom *pair = bindings->expr.elems[index];
            const PettaPlanNode *pair_plan =
                petta_plan_child(bindings_plan, index);
            if (!pair || pair->kind != ATOM_EXPR || pair->expr.len != 2u ||
                !pair_plan)
                return oem_reject(compile, "let* binding");
            OemNode let = {.kind = OEM_N_LET, .body = body};
            if (!oem_lower_pattern(compile, pair->expr.elems[0],
                                   petta_plan_child(pair_plan, 0u),
                                   depth + 1u, &let.pattern) ||
                !oem_build_value(compile, pair->expr.elems[1],
                                 petta_plan_child(pair_plan, 1u), depth + 1u,
                                 &let.value))
                return false;
            let.exposed = compile->nodes[body].exposed;
            if (!oem_node(compile, let, &body))
                return false;
        }
        *out = body;
        return true;
    }
    if (plan->control != PETTA_PLAN_CONTROL_NONE)
        return oem_reject(compile, "control outside the fragment");
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
        return oem_emit(compile, (OemStep){
                            .kind = OEM_S_BIND, .pattern = node.pattern,
                            .value = compile->nodes[node.value].exposed,
                        }, NULL) &&
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
    case OEM_N_FAIL:
        return oem_emit(compile, (OemStep){.kind = OEM_S_FAIL}, NULL);
    case OEM_N_CALL:
        if (node.tail)
            return oem_emit_goals(compile, index);
        /* fallthrough */
    case OEM_N_VALUE:
    case OEM_N_PRIM:
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

static bool oem_compile_equation(CettaOpenEquationProgram *program,
                                 const CettaOpenEquationHost *host,
                                 Atom *lhs, Atom *rhs,
                                 const PettaPlanNode *rhs_plan,
                                 const char **reason) {
    OemCompile compile = {.program = program, .host = host};
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
    }
    ok = ok && oem_compile_relational_occurrences(&compile) &&
        oem_emit_tail(&compile, root);
    equation.register_count = compile.register_count;
    equation.slot_count = compile.slot_count;
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
    return program;
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
} OemCont;

/* A call with untried alternatives: the equations of `relation` in the
 * version that was current when the call was made. */
typedef struct {
    const CettaOpenEquationProgram *program;
    uint32_t relation;
    uint32_t next;
    Atom **args;
    const OemCont *cont;
    ArenaMark mark;
    uint32_t cell_mark;
    uint32_t trail_mark;
} OemFrame;

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
} OemResolveItem;

struct CettaOpenEquationCursor {
    /* The entry version; the versions a call entered after a change stay
     * callers may still run their code. */
    CettaOpenEquationProgram *program;
    CettaOpenEquationProgram **versions;
    uint32_t version_len, version_cap;
    Arena region;
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
    OemFrame *frames;
    uint32_t frame_len, frame_cap;
    OemPair *pairs;
    uint32_t pair_cap;
    Atom **walk;
    uint32_t walk_cap;
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
    /* Cell bindings made so far; an attempt that failed after one of its own
     * bindings is classified apart from one that failed before any. */
    uint64_t binds;
    /* The region's size at which the next collection runs. */
    size_t collect_after;
    CettaOpenEquationStats stats;
};

typedef enum {
    OEM_RUN_FAILED = 0,
    OEM_RUN_CALLED,
    OEM_RUN_ANSWER,
    OEM_RUN_HANDOFF,
    /* An operation raised an error; `value_out` holds it. */
    OEM_RUN_RAISE,
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

static Atom *oem_instantiate(CettaOpenEquationCursor *cursor,
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
        : arena_alloc(&cursor->region, sizeof(*children) * item->count);
    if (!children)
        return NULL;
    for (uint32_t index = 0u; index < item->count; index++) {
        children[index] = oem_instantiate(
            cursor, program, locals,
            program->template_children[item->first + index]);
        if (!children[index])
            return NULL;
    }
    return atom_expr(&cursor->region, children, item->count);
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

static void oem_restore(CettaOpenEquationCursor *cursor,
                        const OemFrame *frame) {
    arena_reset(&cursor->region, frame->mark);
    while (cursor->trail_len > frame->trail_mark)
        cursor->cells[cursor->trail[--cursor->trail_len]] = NULL;
    cursor->cell_len = frame->cell_mark;
}

static bool oem_push_frame(CettaOpenEquationCursor *cursor,
                           const CettaOpenEquationProgram *program,
                           uint32_t relation, Atom **args,
                           const OemCont *cont) {
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
    };
    return true;
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

/* A value with every bound cell replaced by its value.  For an answer
 * (`operand` false) it is built in the answer arena: region structure is
 * copied, atoms owned elsewhere are shared, and each unbound cell becomes
 * the answer's variable for it.  For a grounded operation's operand
 * (`operand` true) it is built in the region, sharing whatever holds no
 * cell, and an unbound cell stays the cell's own variable: the operation
 * sees a variable, as it would in equation search.  Iterative, so deep
 * lists cost no C stack. */
static Atom *oem_resolve_mode(CettaOpenEquationCursor *cursor, Atom *root,
                              bool operand) {
    Arena *arena = operand ? &cursor->region : cursor->answer_arena;
    uint32_t depth = 0u;
    uint32_t results = 0u;
    Atom *atom = root;
    for (;;) {
        atom = oem_deref(cursor, atom);
        uint32_t index = 0u;
        Atom *result = NULL;
        if (oem_is_cell(cursor, atom, &index)) {
            result = operand ? atom : oem_answer_cell(cursor, index);
        } else if ((operand || atom->arena_id != cursor->region.identity) &&
                   (atom->kind != ATOM_EXPR || !atom_has_vars(atom))) {
            result = atom;
        } else if (atom->kind != ATOM_EXPR || atom->expr.len == 0u) {
            result = atom_deep_copy(arena, atom);
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
            result = atom_expr(arena,
                               &cursor->resolve_results[item->base],
                               item->atom->expr.len);
            results = item->base;
            depth--;
        }
    }
}

static Atom *oem_resolve(CettaOpenEquationCursor *cursor, Atom *root) {
    return oem_resolve_mode(cursor, root, false);
}

/* A raised error, as an answer-arena value: the branch ends, and the
 * cursor's alternatives remain. */
static OemRun oem_publish_raise(CettaOpenEquationCursor *cursor, Atom *error,
                                Atom **value_out) {
    while (cursor->fresh_touched_len > 0u)
        cursor->answer_fresh[
            cursor->fresh_touched[--cursor->fresh_touched_len]] = NULL;
    Atom *raised = oem_resolve(cursor, error);
    if (!raised)
        return OEM_RUN_HANDOFF;
    *value_out = raised;
    return OEM_RUN_RAISE;
}

static OemRun oem_publish(CettaOpenEquationCursor *cursor, Atom *value,
                          Atom **value_out) {
    while (cursor->fresh_touched_len > 0u)
        cursor->answer_fresh[
            cursor->fresh_touched[--cursor->fresh_touched_len]] = NULL;
    Atom *answer = oem_resolve(cursor, value);
    if (!answer)
        return OEM_RUN_HANDOFF;
    for (uint32_t index = 0u; index < cursor->query_var_count; index++) {
        cursor->query_values[index] =
            oem_resolve(cursor, cursor->query_cells[index]);
        if (!cursor->query_values[index])
            return OEM_RUN_HANDOFF;
    }
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
    if (atom_is_empty(result))
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

static OemRun oem_run_body(CettaOpenEquationCursor *cursor,
                           const CettaOpenEquationProgram *program,
                           uint32_t pc, Atom **locals, uint32_t local_count,
                           const OemCont *cont, Atom **value_out) {
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
                    .pattern = step->pattern,
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
            return oem_push_frame(cursor, target, relation, args, next)
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
            OemUnify unified = oem_unify_template(
                cursor, program, locals, step->pattern, value, 0u);
            if (unified != OEM_UNIFY_OK)
                return unified == OEM_UNIFY_FAIL ? OEM_RUN_FAILED
                                                 : OEM_RUN_HANDOFF;
            pc++;
            continue;
        }
        case OEM_S_BIND: {
            Atom *value = oem_instantiate(cursor, program, locals, step->value);
            if (!value)
                return OEM_RUN_HANDOFF;
            OemUnify unified = oem_unify_template(
                cursor, program, locals, step->pattern, value, 0u);
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
                           const OemCont *cont, Atom **value_out) {
    const OemEquation *eq = &program->equations[equation];
    Atom **locals = arena_alloc(
        &cursor->region,
        sizeof(*locals) * (eq->slot_count ? eq->slot_count : 1u));
    Atom **regs = arena_alloc(
        &cursor->region,
        sizeof(*regs) * (eq->register_count ? eq->register_count : 1u));
    if (!locals || !regs)
        return OEM_RUN_HANDOFF;
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
    if (eq->dest_kind != OEM_DEST_NONE) {
        destination = cont
            ? oem_instantiate(cursor, cont->program, cont->locals, cont->pattern)
            : cursor->destination;
        if (!destination)
            return OEM_RUN_HANDOFF;
        /* An output slot that head matching left unset is the destination
         * itself: nothing to unify. */
        if (eq->dest_kind == OEM_DEST_SLOT && !locals[eq->destination]) {
            locals[eq->destination] = destination;
            destination = NULL;
        }
    }
    for (uint32_t slot = 0u; slot < eq->slot_count; slot++) {
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
    cursor->stats.activations++;
    oem_count_attempt(cursor, OEM_ATTEMPT_SUCCESS, binds_at_start,
                      OEM_RUN_CALLED);
    return oem_run_body(cursor, program, eq->first_step, locals,
                        eq->slot_count, cont,
                        value_out);
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

/* Below this the region is not collected: ordinary queries finish first,
 * and a collection of a large live structure is not worth its copy. */
enum { OEM_COLLECT_MIN_BYTES = 256u * 1024u * 1024u };

typedef struct {
    Arena *to;
    uint32_t from_identity;
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

static void oem_collect_reach(CettaOpenEquationCursor *cursor,
                              OemCollect *collect, uint32_t cell) {
    if (collect->reached[cell])
        return;
    collect->reached[cell] = 1u;
    if (cursor->cells[cell])
        collect->pending[collect->pending_len++] = cell;
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
        Atom *atom = cursor->walk[--len];
        uint32_t cell = 0u;
        if (oem_is_cell(cursor, atom, &cell)) {
            oem_collect_reach(cursor, collect, cell);
            continue;
        }
        if (atom->arena_id != collect->from_identity ||
            atom->kind != ATOM_EXPR || atom->expr.len == 0u ||
            oem_collect_find(collect, atom))
            continue;
        if (!oem_collect_put(collect, atom, atom) ||
            atom->expr.len > UINT32_MAX - len ||
            !oem_reserve((void **)&cursor->walk, &cursor->walk_cap,
                         len + (uint32_t)atom->expr.len,
                         sizeof(*cursor->walk)))
            return false;
        for (CettaExprIndex child = 0u; child < atom->expr.len; child++)
            cursor->walk[len++] = atom->expr.elems[child];
    }
    return true;
}

static bool oem_trace_slots(CettaOpenEquationCursor *cursor,
                            OemCollect *collect, Atom **slots,
                            uint32_t count) {
    if (!slots || oem_collect_find(collect, slots))
        return true;
    if (!oem_collect_put(collect, slots, slots))
        return false;
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
        if (!oem_collect_put(collect, at, (void *)at) ||
            !oem_trace_slots(cursor, collect, at->locals, at->local_count))
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
        if (oem_is_cell(cursor, atom, &cell)) {
            result = collect->reached[cell]
                ? cursor->cell_names[collect->remap[cell]] : NULL;
        } else if (atom->arena_id != collect->from_identity) {
            result = atom;
        } else if ((result = oem_collect_find(collect, atom)) != NULL) {
        } else if (atom->kind != ATOM_EXPR || atom->expr.len == 0u) {
            result = atom_deep_copy(collect->to, atom);
            if (!result || !oem_collect_put(collect, atom, result))
                return NULL;
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
            if (!result || !oem_collect_put(collect, item->atom, result))
                return NULL;
            results = item->base;
            depth--;
        }
    }
}

static Atom **oem_collect_slots(CettaOpenEquationCursor *cursor,
                                OemCollect *collect, Atom **slots,
                                uint32_t count, bool *ok) {
    if (!slots || !*ok)
        return slots;
    Atom **copied = oem_collect_find(collect, slots);
    if (copied)
        return copied;
    copied = arena_alloc(collect->to, sizeof(*copied) * (count ? count : 1u));
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
    for (const OemCont *at = cont; at; at = at->parent) {
        if ((top = oem_collect_find(collect, at)) != NULL)
            break;
        if (len == UINT32_MAX ||
            !oem_reserve((void **)&chain, &cap, len + 1u, sizeof(*chain))) {
            free(chain);
            *ok = false;
            return NULL;
        }
        chain[len++] = at;
    }
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
                                           old->local_count, ok);
        if (!*ok || !oem_collect_put(collect, old, copied)) {
            *ok = false;
            break;
        }
        parent = copied;
    }
    free(chain);
    return *ok ? (cont ? oem_collect_find(collect, cont) : NULL) : NULL;
}

/* The region size at which the next collection runs.  A collection walks
 * the live region, every cell, frame and trail entry; the region must grow
 * by at least as much before the next one, so collection work stays
 * proportional to allocation. */
static size_t oem_collect_threshold(const CettaOpenEquationCursor *cursor) {
    size_t walked = arena_accounted_live_bytes(&cursor->region) +
        (size_t)cursor->cell_len * (sizeof(*cursor->cells) + sizeof(Atom)) +
        (size_t)cursor->frame_len * sizeof(*cursor->frames) +
        (size_t)cursor->trail_len * sizeof(*cursor->trail);
    size_t after = arena_accounted_live_bytes(&cursor->region) + walked;
    return after > OEM_COLLECT_MIN_BYTES ? after : OEM_COLLECT_MIN_BYTES;
}

/* Replace the region by what the cursor still reaches: the frames, every
 * state they restore included, the destination and the call's cells, and
 * the cells a reached term mentions.  Reached cells keep their order and
 * take consecutive indices, so a frame still marks the cells created after
 * it and the trail still names the cells to unbind; a cell nothing mentions
 * is dropped, as no live term or future state reads it.  False only when
 * out of memory, and then the cursor is unchanged. */
static bool oem_collect(CettaOpenEquationCursor *cursor) {
    uint32_t old_cells = cursor->cell_len;
    uint32_t frames = cursor->frame_len;
    size_t cell_room = old_cells ? old_cells : 1u;
    OemCollect collect = {
        .from_identity = cursor->region.identity,
        .reached = calloc(cell_room, sizeof(uint8_t)),
        .pending = calloc(cell_room, sizeof(uint32_t)),
        .remap = calloc(cell_room, sizeof(uint32_t)),
    };
    uint32_t *before = calloc(cell_room + 1u, sizeof(uint32_t));
    uint32_t *kept_before = calloc(
        (size_t)cursor->trail_len + 1u, sizeof(uint32_t));
    Atom ***args = calloc(frames ? frames : 1u, sizeof(*args));
    const OemCont **conts = calloc(frames ? frames : 1u, sizeof(*conts));
    Atom **cells = calloc(cell_room, sizeof(*cells));
    bool ok = collect.reached && collect.pending && collect.remap &&
        before && kept_before && args && conts && cells;

    /* Trace. */
    for (uint32_t index = 0u; ok && index < cursor->query_var_count; index++)
        ok = oem_trace_atom(cursor, &collect, cursor->query_cells[index]);
    for (uint32_t index = 0u; ok && index < frames; index++) {
        const OemFrame *frame = &cursor->frames[index];
        uint32_t arity = frame->program->relations[frame->relation].arity;
        ok = oem_trace_slots(cursor, &collect, frame->args, arity) &&
            oem_trace_cont(cursor, &collect, frame->cont);
    }
    ok = ok && oem_trace_atom(cursor, &collect, cursor->destination);
    while (ok && collect.pending_len > 0u) {
        uint32_t cell = collect.pending[--collect.pending_len];
        ok = oem_trace_atom(cursor, &collect, cursor->cells[cell]);
    }
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

    /* Copy, renaming each reached cell to its index. */
    Arena to;
    arena_init(&to);
    arena_set_runtime_kind(&to, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    arena_set_hashcons(&to, NULL);
    collect.to = &to;
    for (uint32_t index = 0u; ok && index < frames; index++) {
        const OemFrame *frame = &cursor->frames[index];
        uint32_t arity = frame->program->relations[frame->relation].arity;
        args[index] = oem_collect_slots(cursor, &collect, frame->args, arity,
                                        &ok);
        conts[index] = ok ? oem_collect_cont(cursor, &collect, frame->cont,
                                             &ok) : NULL;
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
    free(collect.keys);
    free(collect.values);
    if (!ok) {
        free(collect.reached);
        free(collect.pending);
        free(collect.remap);
        free(before);
        free(kept_before);
        free(args);
        free(conts);
        free(cells);
        arena_free(&to);
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
    ArenaMark end = arena_mark(&to);
    for (uint32_t index = 0u; index < frames; index++) {
        OemFrame *frame = &cursor->frames[index];
        frame->args = args[index];
        frame->cont = conts[index];
        frame->mark = end;
        frame->cell_mark = before[frame->cell_mark];
        frame->trail_mark = kept_before[frame->trail_mark];
    }
    cursor->trail_len = kept;
    for (uint32_t index = 0u; index < live; index++)
        cursor->cells[index] = cells[index];
    for (uint32_t index = live; index < old_cells; index++)
        cursor->cells[index] = NULL;
    cursor->cell_len = live;
    for (uint32_t index = 0u; index < cursor->query_var_count; index++)
        cursor->query_cells[index] = cursor->cell_names[
            collect.remap[var_base_id(cursor->query_cells[index]->var_id) - 1u]];
    cursor->destination = destination;
    while (cursor->fresh_touched_len > 0u)
        cursor->answer_fresh[
            cursor->fresh_touched[--cursor->fresh_touched_len]] = NULL;
    free(collect.reached);
    free(collect.pending);
    free(collect.remap);
    free(before);
    free(kept_before);
    free(args);
    free(conts);
    free(cells);
    arena_free(&cursor->region);
    cursor->region = to;
    cursor->collect_after = oem_collect_threshold(cursor);
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
static bool oem_settle(CettaOpenEquationCursor *cursor) {
    while (cursor->frame_len > 0u) {
        OemFrame *frame = &cursor->frames[cursor->frame_len - 1u];
        const OemRelation *relation =
            &frame->program->relations[frame->relation];
        bool error = false;
        oem_restore(cursor, frame);
        oem_skip_refuted(cursor, frame->program, relation, frame, &error);
        if (error)
            return false;
        if (frame->next < relation->equation_count)
            return true;
        cursor->frame_len--;
    }
    return true;
}

/* ---- cursor -------------------------------------------------------------- */

/* The call's arguments in the region.  Ground structure is copied, so the
 * cursor owns everything it reads; each query variable becomes its cell. */
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
        return atom_deep_copy(&cursor->region, atom);
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
    if (runtime)
        cursor->runtime = *runtime;
    arena_init(&cursor->region);
    arena_set_runtime_kind(&cursor->region, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    arena_set_hashcons(&cursor->region, NULL);
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
    ok = ok && oem_push_frame(cursor, program, 0u, imported, NULL);
    cursor->query_vars = NULL;
    if (!ok) {
        cetta_open_equation_cursor_close(cursor);
        return NULL;
    }
    return cursor;
}

bool cetta_open_equation_cursor_pending(
    const CettaOpenEquationCursor *cursor) {
    return cursor && cursor->frame_len > 0u;
}

static CettaOpenEquationStep oem_cursor_next(
    CettaOpenEquationCursor *cursor, Atom **value_out,
    Atom ***query_values_out);

CettaOpenEquationStep cetta_open_equation_cursor_next(
    CettaOpenEquationCursor *cursor, Atom *const *query_vars,
    Atom **value_out, Atom ***query_values_out) {
    if (cursor)
        cursor->query_vars = query_vars;
    CettaOpenEquationStep step =
        oem_cursor_next(cursor, value_out, query_values_out);
    if (cursor)
        cursor->query_vars = NULL;
    return step;
}

static CettaOpenEquationStep oem_cursor_next(
    CettaOpenEquationCursor *cursor, Atom **value_out,
    Atom ***query_values_out) {
    if (value_out)
        *value_out = NULL;
    if (query_values_out)
        *query_values_out = NULL;
    if (!cursor || !value_out)
        return CETTA_OPEN_EQUATION_HANDOFF;
    /* An interrupt stops between alternatives, where the region is
     * consistent: the cursor resumes once the host has cleared it.  Any
     * other handoff is final.  A change of the Space program or of the
     * host's authorities stops nothing: alternatives keep the equations
     * their calls were entered with, and later calls enter current ones. */
    if (cursor->handoff == CETTA_OPEN_EQUATION_HANDOFF_INTERRUPT)
        cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_NONE;
    if (cursor->handoff != CETTA_OPEN_EQUATION_HANDOFF_NONE)
        return CETTA_OPEN_EQUATION_HANDOFF;
    for (;;) {
        if (cursor->frame_len == 0u)
            return CETTA_OPEN_EQUATION_EXHAUSTED;
        if (cursor->runtime.interrupt && ++cursor->poll >= 256u) {
            cursor->poll = 0u;
            if (cursor->runtime.interrupt(cursor->runtime.context)) {
                cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_INTERRUPT;
                return CETTA_OPEN_EQUATION_HANDOFF;
            }
        }
        if (arena_accounted_live_bytes(&cursor->region) >=
                cursor->collect_after &&
            !oem_collect(cursor)) {
            cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_CAPACITY;
            return CETTA_OPEN_EQUATION_HANDOFF;
        }
        OemFrame *frame = &cursor->frames[cursor->frame_len - 1u];
        const CettaOpenEquationProgram *version = frame->program;
        const OemRelation *relation = &version->relations[frame->relation];
        oem_restore(cursor, frame);
        if (frame->next >= relation->equation_count) {
            cursor->frame_len--;
            continue;
        }
        uint32_t equation = relation->first_equation + frame->next++;
        Atom **args = frame->args;
        const OemCont *cont = frame->cont;
        /* The last alternative leaves nothing to return to: the frame goes
         * now, so a deterministic chain of calls keeps one frame. */
        if (frame->next >= relation->equation_count)
            cursor->frame_len--;
        OemRun run = oem_activate(cursor, version, equation, args,
                                  relation->arity, cont, value_out);
        if (run == OEM_RUN_ANSWER || run == OEM_RUN_RAISE) {
            if (!oem_settle(cursor)) {
                cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_CAPACITY;
                return CETTA_OPEN_EQUATION_HANDOFF;
            }
            if (run == OEM_RUN_RAISE)
                return CETTA_OPEN_EQUATION_RAISE;
            if (query_values_out)
                *query_values_out = cursor->query_values;
            return CETTA_OPEN_EQUATION_ANSWER;
        }
        if (run == OEM_RUN_HANDOFF) {
            if (cursor->handoff == CETTA_OPEN_EQUATION_HANDOFF_NONE)
                cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_CAPACITY;
            return CETTA_OPEN_EQUATION_HANDOFF;
        }
    }
}

CettaOpenEquationHandoff cetta_open_equation_cursor_handoff(
    const CettaOpenEquationCursor *cursor) {
    return cursor ? cursor->handoff : CETTA_OPEN_EQUATION_HANDOFF_CAPACITY;
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
    arena_free(&cursor->names);
    free(cursor->cell_names);
    if (cursor->epoch)
        cetta_frame_identity_release(cursor->epoch);
    free(cursor->cells);
    free(cursor->trail);
    free(cursor->frames);
    free(cursor->pairs);
    free(cursor->walk);
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

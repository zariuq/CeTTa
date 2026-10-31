#include "fixpoint.h"
#include "prepared_pure_machine.h"
#include "he_macro_expansion.h"

#include "eval.h"
#include "grounded.h"
#include "petta_semantics.h"
#include "stats.h"
#include "symbol.h"
#include "u64_index.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    PREPARED_PURE_LITERAL = 0,
    PREPARED_PURE_ENTRY_ARGUMENT,
    PREPARED_PURE_EVAL_ENTRY_ARGUMENT,
    PREPARED_PURE_SLOT,
    PREPARED_PURE_EVAL_SLOT,
    PREPARED_PURE_BUILD,
    PREPARED_PURE_OBSERVE,
    PREPARED_PURE_REGISTER,
    PREPARED_PURE_INTRINSIC,
    PREPARED_PURE_BIND,
    PREPARED_PURE_IF,
    PREPARED_PURE_CALL,
    /* Choice zero: no answer.  Only answer producers compile it; their step
     * lowering turns it into a failing step, so no executor ever meets it. */
    PREPARED_PURE_ZERO,
    /* No answer in a single-answer program: the program declines, and the
     * canonical evaluator answers the call, as it answers HE's Empty. */
    PREPARED_PURE_DECLINE,
} PreparedPureNodeKind;

/* An observation that tests a value against a bind pattern carries the
 * pattern's index under this flag. */
#define PREPARED_PURE_OBSERVE_PATTERN_TEST UINT32_C(0x80000000)
/* An observation of whether a value is an expression atom, HE's
 * Expression meta-type. */
#define PREPARED_PURE_OBSERVE_ATOM_EXPRESSION UINT32_C(0x40000000)

typedef struct {
    PreparedPureNodeKind kind;
    Atom *atom;
    SymbolId head;
    CettaGsltRegisterInstruction instruction;
    CettaGsltRegisterResultKind result_kind;
    CettaGsltPreparedPureIntrinsicInstruction intrinsic_instruction;
    uint32_t first_child;
    uint32_t child_count;
    uint32_t auxiliary;
    uint32_t first_live_slot;
    uint32_t live_slot_count;
    bool call_arguments_are_values;
    bool tail_position;
    /* A register over typed structural operands: bit i marks operand i as a
     * literal symbol whose only type is %Undefined%.  match_types takes that
     * type as a wildcard, so the operands share a type without a query; the
     * source program token pins the declarations the fact was read from. */
    uint8_t undefined_type_operands;
} PreparedPureNode;

typedef struct {
    VarId var;
    uint32_t slot;
} PreparedPureVarSlot;

typedef struct {
    VarId var;
    uint32_t slot;
    bool prebound;
} PreparedPureBindVar;

typedef struct {
    Atom *pattern;
    uint32_t first_var;
    uint32_t var_count;
    /* A non-variable head over distinct fresh variables: field i binds
     * bind_vars[first_var + i - 1]. */
    bool flat_fresh_fields;
    /* A source bind whose producer computes first only if its destination
     * evidence holds (prepared_pure_binds_meet_destinations). */
    bool destination_evidence;
} PreparedPureBindPattern;

typedef struct {
    Atom **items;
    size_t len;
} PreparedPureAtomSpan;

typedef struct {
    CettaPreparedPureProgram *program;
} PreparedPureEphemeronAtomMap;

#define PREPARED_PURE_DECLARE_STRONG_ATOM_SLOT(name) Atom **name;
#define PREPARED_PURE_DECLARE_STRONG_ATOM_SPAN(name) PreparedPureAtomSpan name;
#define PREPARED_PURE_DECLARE_LOGICAL_BINDINGS(name) void *name;
#define PREPARED_PURE_DECLARE_OUTCOME_SET(name) void *name;
#define PREPARED_PURE_DECLARE_VARIANT_INSTANCE(name) void *name;
#define PREPARED_PURE_DECLARE_EPHEMERON_ATOM_MAP(name) \
    PreparedPureEphemeronAtomMap name;
typedef struct {
    CETTA_EVAL_GC_FRAME_FIELDS_prepared_pure_machine(
        PREPARED_PURE_DECLARE_STRONG_ATOM_SLOT,
        PREPARED_PURE_DECLARE_STRONG_ATOM_SPAN,
        PREPARED_PURE_DECLARE_LOGICAL_BINDINGS,
        PREPARED_PURE_DECLARE_OUTCOME_SET,
        PREPARED_PURE_DECLARE_VARIANT_INSTANCE,
        PREPARED_PURE_DECLARE_EPHEMERON_ATOM_MAP)
} PreparedPureGcRoots;
#undef PREPARED_PURE_DECLARE_EPHEMERON_ATOM_MAP
#undef PREPARED_PURE_DECLARE_OUTCOME_SET
#undef PREPARED_PURE_DECLARE_VARIANT_INSTANCE
#undef PREPARED_PURE_DECLARE_LOGICAL_BINDINGS
#undef PREPARED_PURE_DECLARE_STRONG_ATOM_SPAN
#undef PREPARED_PURE_DECLARE_STRONG_ATOM_SLOT

/* Executable locals and lowerings belong to a body. Equation matching and
 * source occurrences are separate; an invocation root needs no equation. */
typedef struct {
    uint32_t root;
    uint32_t local_count;
    uint32_t first_step;
    uint32_t step_count;
    uint32_t frame_slot_count;
    uint32_t first_code;
    uint32_t register_count;
    bool tail_only;
    bool register_entry;
} PreparedPureBody;

typedef struct {
    Atom *lhs;
    PreparedPureBody body;
    /* The Space occurrence compiled here; a resumed enumeration names its
     * next untried equation by it. */
    Atom *equation;
    CettaIndex logical_index;
    uint32_t arity;
    uint32_t first_pattern_var;
    uint32_t pattern_var_count;
    uint32_t scalar_guard;
    /* When every argument pattern is a variable, argument i binds the slot
     * argument_slots[first_argument_slot + i]. */
    uint32_t first_argument_slot;
    bool variable_arguments;
    /* Otherwise, when no pattern node is a dialect view form, matching
     * ready arguments runs match_ops[first_match_op, +match_op_count) over
     * match_register_count registers, the first `arity` holding the
     * arguments. */
    bool compiled_match;
    uint32_t first_match_op;
    uint32_t match_op_count;
    uint32_t match_register_count;
    /* The position among those ops of the first bind, or UINT32_MAX:
     * a check failing after it fails after a variable was bound. */
    uint32_t first_bind_op;
    /* A pattern variable occurs more than once.  Its first occurrence binds
     * it; each later one requires an equal value, as the canonical matcher
     * decides a pair of ground values.  Compiled only where every argument
     * is a value when equations are matched. */
    bool repeated_variables;
    /* An answer producer's equation whose body is a value tree or a last
     * call over value trees answers directly.  Any other runs as the step
     * program steps[first_step, +step_count) over frame_slot_count locals:
     * its local_count, then temporaries. */
    /* A body that builds a value around one call -- a constructor whose
     * only call child, not its head, is child `context_hole` -- or
     * PREPARED_PURE_NO_CONTEXT. */
    uint32_t context_hole;
    /* When every other child of that constructor, and every argument of
     * the call, is a literal or a slot, recording the context reads
     * context_reads[first_context_read, +child_count - 1 + call arity) and
     * evaluates nothing; otherwise PREPARED_PURE_NO_CONTEXT. */
    uint32_t first_context_read;
    /* In a keyed group whose first argument's key this equation tests:
     * every op after that test binds a fresh variable to an argument or to
     * a field of the keyed expression. */
    bool binds_after_key;
    /* The body as register code: code[first_code, ...) over
     * register_count registers, its local_count locals first.  A call
     * entering it by its head's SINGLE or KEYED entry has decided its
     * first code_key_ops match ops, which tested the first argument's head
     * and code_key_length.  With code_binds it then binds
     * code_binds[first_bind, +bind_count); otherwise it runs its match ops
     * from code_key_ops on. */
    uint32_t first_bind;
    uint32_t bind_count;
    /* The first bind_field_count binds read fields of arguments, the rest
     * arguments. */
    uint32_t bind_field_count;
    uint32_t code_key_ops;
    uint32_t code_key_length;
    bool code_binds;
} PreparedPureEquation;

typedef enum {
    /* slot <- the value of node */
    PREPARED_PURE_STEP_EVAL = 0,
    /* slot <- each answer of the head applied to the argument slots */
    PREPARED_PURE_STEP_CALL,
    /* The answers of the head applied to the argument slots are this body's
     * answers. */
    PREPARED_PURE_STEP_TAIL,
    /* Bind the pattern against slot; a mismatch leaves no answer. */
    PREPARED_PURE_STEP_MATCH,
    /* True in slot continues, False jumps to target. */
    PREPARED_PURE_STEP_BRANCH,
    PREPARED_PURE_STEP_JUMP,
    /* The value of node is an answer of this body. */
    PREPARED_PURE_STEP_RETURN,
    /* No answer. */
    PREPARED_PURE_STEP_FAIL,
    /* A native invocation over ready operand values. Its tagged return
     * either continues inline or remains an owned producer on the frontier. */
    PREPARED_PURE_STEP_NATIVE,
    /* Match and bind on success; a mismatch jumps to target. */
    PREPARED_PURE_STEP_MATCH_ELSE,
} PreparedPureStepKind;

typedef struct {
    uint8_t kind;
    /* EVAL and RETURN: the node is a value tree, built without the executor. */
    bool value_tree;
    uint32_t node;
    uint32_t slot;
    uint32_t target;
    /* CALL and TAIL: the head index; MATCH: the bind pattern. */
    uint32_t auxiliary;
    /* CALL and TAIL: argument slots step_arguments[first_argument, +arity). */
    uint32_t first_argument;
    uint32_t arity;
    /* CALL: a node for the rest of the equation body once the call's answer
     * is in `slot` -- operands already evaluated are slot reads, the rest is
     * source structure.  It is deoptimization metadata: decoding it gives
     * the source computation the continuation stands for.  NO_TEMPLATE when
     * the lowering could not describe it. */
    uint32_t resume_template;
    /* EVAL and RETURN not over a value tree, in a program with register
     * code: the node lowered to code[first_code, ...) over register_count
     * registers, the step frame's slots first. */
    bool coded;
    uint32_t first_code;
    uint32_t register_count;
} PreparedPureStep;

#define PREPARED_PURE_NO_TEMPLATE UINT32_MAX

typedef enum {
    /* Slot `operand` takes register `source`. */
    PREPARED_PURE_MATCH_OP_BIND = 0,
    /* Register `source` equals the atomic literal. */
    PREPARED_PURE_MATCH_OP_ATOM = 1,
    /* Register `source` is an expression of `length` elements, which load
     * into registers `operand` onward. */
    PREPARED_PURE_MATCH_OP_EXPR = 2,
    /* Register `source` equals the value slot `operand` took earlier. */
    PREPARED_PURE_MATCH_OP_SAME = 3,
} PreparedPureMatchOpKind;

typedef struct {
    Atom *literal;
    uint32_t source;
    uint32_t operand;
    uint32_t length;
    uint8_t kind;
} PreparedPureMatchOp;

enum {
    PREPARED_PURE_MATCH_MAX_REGISTERS = 64u,
    PREPARED_PURE_MATCH_MAX_OPS = 256u,
};

typedef struct {
    Atom *literal;
    uint32_t slot;
    bool from_slot;
} PreparedPureScalarGuardArgument;

typedef struct {
    Atom *head;
    uint32_t first_argument;
    uint32_t argument_count;
    bool expected_truth;
} PreparedPureScalarGuard;

/* The key an equation's pattern tests at one argument position: a symbol
 * (length 0), or an expression of `length` elements headed by a symbol;
 * symbol SYMBOL_ID_NONE when the pattern there tests none. */
typedef struct {
    SymbolId symbol;
    uint32_t length;
    uint32_t equation;
} PreparedPureArgumentKey;

enum { PREPARED_PURE_KEY_MAX_POSITIONS = 4u };

typedef struct {
    uint32_t arity;
    uint32_t equation_count;
    uint64_t universally_constrained_arguments;
    CettaMatchDecision *selector;
    /* A group too small for a selector whose every two equations test
     * different keys at one of the first key_positions arguments:
     * argument_keys[first_key, +equation_count * key_positions), the keys
     * of each equation in turn.  tested_positions marks the positions some
     * equation tests.  Then selector is NULL. */
    bool keyed;
    uint32_t first_key;
    uint32_t key_positions;
    uint32_t tested_positions;
} PreparedPureDecisionProgram;

typedef struct {
    SymbolId head;
    uint32_t first_equation;
    uint32_t equation_count;
    uint32_t first_decision;
    uint32_t decision_count;
    bool compiled;
    /* Weak-head matching selects at most one equation, and no body reaches
     * choice: a call that is not deterministic, or choice zero. */
    bool deterministic;
    /* Matching permission is independent of the body's answer multiplicity. */
    bool unique_match;
    /* A type annotation names the head. */
    bool declares_type;
    /* Its equations form one keyed group: decisions[first_decision]. */
    bool keyed;
    /* An HE head with one value-class signature: a call takes typed_arity
     * arguments, value_classes[first_value_class, +typed_arity) are its
     * parameters' classes and range_class its range's.  checks_arguments
     * when some parameter's class is not %Undefined%. */
    bool typed;
    bool checks_arguments;
    uint8_t range_class;
    uint32_t typed_arity;
    uint32_t first_value_class;
} PreparedPureHead;

typedef struct {
    SymbolId head;
    uint32_t arity;
    bool callable;
    bool occupied;
} PreparedPureCallableCacheEntry;

typedef struct {
    uint32_t node;
    uint32_t runtime_head_index;
    uint32_t local_base;
    uint32_t value_base;
    uint32_t child_index;
    uint32_t saved_slot_len;
    uint64_t ready_arguments;
    uint32_t demanded_argument;
    uint32_t memo_index;
    /* The contexts this call recorded, the top ones of the context stack:
     * its value is built into them when it answers. */
    uint32_t context_count;
    uint8_t state;
} PreparedPureFrame;

/* A value recording a context reads: `literal`, or the local `slot` of the
 * equation's matched locals when literal is NULL. */
typedef struct {
    Atom *literal;
    uint32_t slot;
} PreparedPureContextRead;

/* A recorded context: the constructor node whose child `hole` awaits the
 * value of a call.  Its other children's values lie on the value stack,
 * below the recording frame's working values. */
typedef struct {
    uint32_t node;
    uint32_t hole;
} PreparedPureContext;

/* Register code for a closed eager program.  Each equation body, and the
 * program's root, is lowered to a flat sequence of instructions over its
 * activation's registers: the body's locals first, then one register for
 * each value computed on the way.  An operand is a register, or a literal
 * when PREPARED_PURE_CODE_LITERAL is set.  Operands are computed left to
 * right, a condition before its branch and a bound value before its
 * pattern, as the node executor computes them, and each instruction
 * performs the operation of its node's frame on the same values. */
typedef enum {
    /* destination <- operand */
    PREPARED_PURE_CODE_MOVE = 0,
    /* destination <- the operation of node `auxiliary` on the operands */
    PREPARED_PURE_CODE_BUILD,
    /* BUILD where the dialect's constructor builds the expression itself. */
    PREPARED_PURE_CODE_BUILD_EXPRESSION,
    PREPARED_PURE_CODE_REGISTER,
    /* A bounded register-only tree over already-ready locals/literals.
     * Its operand list names every local read, for ordinary liveness. */
    PREPARED_PURE_CODE_SCALAR_TREE,
    PREPARED_PURE_CODE_INTRINSIC,
    PREPARED_PURE_CODE_OBSERVE,
    /* A true operand continues; a false one continues at `target`. */
    PREPARED_PURE_CODE_BRANCH,
    /* BRANCH on the value of register node `auxiliary` on the operands. */
    PREPARED_PURE_CODE_BRANCH_REGISTER,
    PREPARED_PURE_CODE_JUMP,
    /* Bind pattern `auxiliary` against the operand. */
    PREPARED_PURE_CODE_MATCH,
    /* MATCH, where an operand the pattern does not match continues at
     * `target`. */
    PREPARED_PURE_CODE_MATCH_ELSE,
    /* destination <- head `auxiliary` applied to the operands */
    PREPARED_PURE_CODE_CALL,
    /* Head `auxiliary` applied to the operands answers for this body. */
    PREPARED_PURE_CODE_TAIL_CALL,
    /* The operand answers for this body. */
    PREPARED_PURE_CODE_RETURN,
    /* No answer here: the program declines. */
    PREPARED_PURE_CODE_DECLINE,
    PREPARED_PURE_CODE_FAIL,
} PreparedPureCodeKind;

#define PREPARED_PURE_CODE_LITERAL UINT32_C(0x80000000)
#define PREPARED_PURE_SCALAR_LITERAL UINT32_C(0x80000000)
#define PREPARED_PURE_SCALAR_LOCAL UINT32_C(0x40000000)

typedef struct {
    uint8_t kind;
    uint32_t destination;
    uint32_t auxiliary;
    uint32_t first_operand;
    uint32_t operand_count;
    /* BRANCH and BRANCH_REGISTER: where a false condition continues;
     * MATCH_ELSE: where a mismatch continues; JUMP: where it goes;
     * BUILD_EXPRESSION: its head, code_heads[target];
     * SCALAR_TREE: its first compact instruction, scalar_code[target]. */
    uint32_t target;
    /* CALL and TAIL_CALL: the registers the body reads after the call
     * returns, code_live[first_live, +live_count); none after a tail call. */
    uint32_t first_live;
    uint32_t live_count;
    /* A value instruction in tail position: its value answers for the
     * body, as a RETURN of it would, and no register takes it. */
    bool returns;
    /* CALL and TAIL_CALL: the kind of its head's entry when the call has
     * the entry's arity, GENERAL otherwise (PreparedPureCodeEntryKind). */
    uint8_t entry;
    /* SCALAR_TREE only: bounded compact register instruction count. */
    uint8_t scalar_count;
} PreparedPureCode;

/* An activation of register code: its next instruction, and its registers
 * program->slots[base, +register_count of its body). */
typedef struct {
    uint32_t pc;
    uint32_t base;
} PreparedPureCodeFrame;

/* How a call of a head enters its equation, decided when the program is
 * lowered, as prepared_pure_direct_entry decides it at each call: SINGLE,
 * its only equation; KEYED, the equation of its keyed group admitting the
 * arguments' keys; GENERAL, the executor's selection. */
typedef enum {
    PREPARED_PURE_CODE_ENTRY_GENERAL = 0,
    PREPARED_PURE_CODE_ENTRY_SINGLE,
    PREPARED_PURE_CODE_ENTRY_KEYED,
} PreparedPureCodeEntryKind;

typedef struct {
    uint8_t kind;
    uint32_t arity;
    /* SINGLE: the equation.  KEYED on the first argument alone: its
     * equations' keys, packed as prepared_pure_code_key packs a value's,
     * code_keys[target, +count), and their equations,
     * code_key_equations[target, +count).  KEYED on more positions: the
     * decision program `decision`, whose keys prepared_pure_key_lookup
     * reads. */
    uint32_t target;
    uint32_t count;
    uint32_t positions;
    uint32_t decision;
} PreparedPureCodeEntry;

/* A local a call's entry binds: argument `source`, or among an equation's
 * first bind_field_count binds, field `field` of argument `source`. */
typedef struct {
    uint32_t local;
    uint32_t source;
    uint32_t field;
} PreparedPureCodeBind;

#define PREPARED_PURE_NO_CONTEXT UINT32_MAX
enum { PREPARED_PURE_CONTEXT_MAX_CHILDREN = 16u };

#define PREPARED_PURE_RUNTIME_NODE UINT32_MAX
#define PREPARED_PURE_NO_MEMO UINT32_MAX

/* Keep the mutually recursive answer interpreter in one compiler-managed hot
 * text region.  This also isolates its branch layout from unrelated growth in
 * storage-provider translation units. */
#if defined(__GNUC__) || defined(__clang__)
#define PREPARED_PURE_HOT __attribute__((hot))
#define PREPARED_PURE_NOINLINE __attribute__((noinline))
#define PREPARED_PURE_ALWAYS_INLINE inline __attribute__((always_inline))
#else
#define PREPARED_PURE_HOT
#define PREPARED_PURE_NOINLINE
#define PREPARED_PURE_ALWAYS_INLINE inline
#endif

typedef enum {
    PREPARED_PURE_MEMO_EVALUATING = 1,
    PREPARED_PURE_MEMO_VALUE = 2,
    PREPARED_PURE_MEMO_TAIL_PENDING = 3,
} PreparedPureMemoState;

typedef struct {
    Atom *pattern;
    Atom *value;
    uint32_t argument;
} PreparedPurePatternPair;

typedef struct {
    Atom *atom;
    uint32_t value_base;
    uint32_t child_index;
} PreparedPureDynamicFrame;

typedef struct {
    PreparedPureVarSlot *bindings;
    size_t len;
    size_t cap;
    uint32_t next_slot;
} PreparedPureCompileContext;

struct CettaPreparedPureProgram {
    /* Owners: the creator, and each answer cursor reading the program. */
    uint32_t references;
    /* One empty, bounded worker for synchronous body entries. It holds no
     * program reference, value, authority callback or caller arena. */
    CettaPreparedPureAnswerCursor *body_cursor_spare;
    Atom **answer_shapes;
    uint64_t host_stamp;
    /* Whether a node among the first structural_scan_len builds a value. */
    size_t structural_scan_len;
    bool builds_structural_values;
    Space *space;
    SpaceProgramToken source_program;
    SpaceEquationToken equation_projection;
    CettaPreparedPureBooleanValue boolean_value;
    /* Immutable Boolean leaves copied into program-owned storage at compile
     * time. Register and observation instructions reuse them instead of
     * allocating the same scalar on every loop iteration. */
    Atom boolean_atoms[2];
    bool boolean_atoms_ready;
    bool exports_boolean_results;
    CettaPreparedPureConstructValue construct_value;
    CettaPreparedPureConstructsExpression constructs_expression;
    CettaPreparedPureOpaqueValue opaque_value;
    CettaPreparedPureRegisterViewFn register_view;
    CettaPreparedPureExpressionViewFn expression_view;
    CettaPreparedPurePatternViewFn pattern_view;
    CettaGsltPureCallMode call_mode;
    bool total_structural_equality;
    CettaMatchDecisionSemanticIdentity match_decision_semantics;
    /* Nodes before this index hold no bind still awaiting its destination
     * evidence (prepared_pure_binds_meet_destinations). */
    uint32_t checked_bind_nodes;
    bool closed_program;
    bool accepts_open_values;
    bool answer_producer;
    bool body_entry;
    bool compile_result_demand_observed;
    bool compile_shared_view_refused;
    bool compile_answer_gate_refused;
    bool allow_callable_templates;
    PreparedPureBody body;
    uint32_t accumulator_slot;
    uint32_t item_slot;

    /* Closed entry values belong to one invocation, not to the compiled
     * node graph.  Keeping them separate makes a parked cached program
     * root-free and prevents it from retaining pointers into an eval arena. */
    SymbolId entry_head;
    Atom **entry_arguments;
    size_t entry_argument_count;

    PreparedPureNode *nodes;
    size_t node_len;
    size_t node_cap;
    /* Cold metadata only. During compilation an entry is its source view;
     * lowering replaces it with the source adapter's normalized plan.
     * Instructions and live values retain their existing sizes. */
    const void **node_sources;
    size_t node_source_cap;
    size_t projected_sources;
    const void *compiling_source;
    bool caller_plans_complete;
    uint32_t *children;
    size_t child_len;
    size_t child_cap;
    PreparedPureHead *heads;
    size_t head_len;
    size_t head_cap;
    uint32_t *head_buckets;
    size_t head_bucket_cap;
    PreparedPureCallableCacheEntry *callable_buckets;
    size_t callable_bucket_cap;
    size_t callable_bucket_len;
    PreparedPureEquation *equations;
    size_t equation_len;
    size_t equation_cap;
    PreparedPureScalarGuard *scalar_guards;
    size_t scalar_guard_len;
    size_t scalar_guard_cap;
    PreparedPureScalarGuardArgument *scalar_guard_arguments;
    size_t scalar_guard_argument_len;
    size_t scalar_guard_argument_cap;
    /* Parameter classes of typed heads (PreparedPureValueClass). */
    uint8_t *value_classes;
    size_t value_class_len;
    size_t value_class_cap;
    /* The syntax of expanded macro definitions, which the program's nodes
     * read; it lives as long as the program and interns nothing. */
    Arena *syntax_arena;
    uint32_t expansion_depth;
    /* The variables of the definitions being expanded: compile-time
     * metavariables a fold may bind.  Every other variable is opaque. */
    VarId *expansion_vars;
    size_t expansion_var_len;
    size_t expansion_var_cap;
    PreparedPureDecisionProgram *decisions;
    size_t decision_len;
    size_t decision_cap;
    PreparedPureArgumentKey *argument_keys;
    size_t argument_key_len;
    size_t argument_key_cap;
    PreparedPureVarSlot *pattern_vars;
    size_t pattern_var_len;
    size_t pattern_var_cap;
    PreparedPureBindPattern *bind_patterns;
    size_t bind_pattern_len;
    size_t bind_pattern_cap;
    PreparedPureBindVar *bind_vars;
    size_t bind_var_len;
    size_t bind_var_cap;
    uint32_t *live_slots;
    size_t live_slot_len;
    size_t live_slot_cap;
    uint32_t *argument_slots;
    size_t argument_slot_len;
    size_t argument_slot_cap;
    PreparedPureMatchOp *match_ops;
    size_t match_op_len;
    size_t match_op_cap;
    /* Some equation is not tail-only, so its answers resume continuations. */
    bool continuation_steps;
    bool native_tail_returns;
    PreparedPureStep *steps;
    size_t step_len;
    size_t step_cap;
    uint32_t *step_arguments;
    size_t step_argument_len;
    size_t step_argument_cap;

    PreparedPureFrame *frames;
    size_t frame_len;
    size_t frame_cap;
    Atom **frame_atoms;
    size_t frame_atom_cap;
    Atom **values;
    size_t value_len;
    size_t value_cap;
    PreparedPureContext *contexts;
    size_t context_len;
    size_t context_cap;
    PreparedPureContextRead *context_reads;
    size_t context_read_len;
    size_t context_read_cap;
    /* Every equation and the root lowered to register code; the root's
     * registers are its locals, then the entry arguments, then its values. */
    bool code_ready;
    PreparedPureCode *code;
    size_t code_len;
    size_t code_cap;
    uint32_t *code_operands;
    size_t code_operand_len;
    size_t code_operand_cap;
    /* Native-aware code lowers the existing step programme. Its first
     * frame_slot_count registers are those same slots; this cold map retains
     * the source step for each emitted instruction. */
    uint32_t *code_steps;
    size_t code_step_cap;
    /* Triples: operation node, left operand, right operand. Literals refer
     * to existing nodes; this code adds no Atom roots. */
    uint32_t *scalar_code;
    size_t scalar_code_len;
    size_t scalar_code_cap;
    Atom **code_literals;
    size_t code_literal_len;
    size_t code_literal_cap;
    /* The heads of BUILD_EXPRESSION instructions, folded once:
     * code_heads[target]. */
    AtomExprHead *code_heads;
    size_t code_head_len;
    size_t code_head_cap;
    /* One per head: how its calls enter. */
    PreparedPureCodeEntry *code_entries;
    uint64_t *code_keys;
    uint32_t *code_key_equations;
    size_t code_key_len;
    size_t code_key_cap;
    size_t code_key_equation_cap;
    PreparedPureCodeBind *code_binds;
    size_t code_bind_len;
    size_t code_bind_cap;
    uint32_t *code_live;
    size_t code_live_len;
    size_t code_live_cap;
    PreparedPureCodeFrame *code_frames;
    size_t code_frame_len;
    size_t code_frame_cap;
    /* The running activation's call has returned into its destination
     * (a safe point at a return), rather than being about to read its
     * operands (a safe point at a call). */
    bool code_resumed;
    Atom **slots;
    size_t slot_len;
    size_t slot_cap;
    uint8_t *slot_live;
    size_t slot_live_cap;
    Atom **match_values;
    size_t match_cap;
    Atom **selected_values;
    size_t selected_cap;
    PreparedPurePatternPair *pattern_pairs;
    size_t pattern_pair_len;
    size_t pattern_pair_cap;
    PreparedPureDynamicFrame *dynamic_frames;
    size_t dynamic_frame_len;
    size_t dynamic_frame_cap;
    Atom **dynamic_values;
    size_t dynamic_value_len;
    size_t dynamic_value_cap;

    /* Call-by-need update cells.  Source expressions remain immutable Atoms;
     * this side table supplies the missing indirection/update operation while
     * preserving pointer sharing between every occurrence of a suspension. */
    Atom **memo_keys;
    size_t memo_key_cap;
    Atom **memo_values;
    size_t memo_value_cap;
    uint8_t *memo_states;
    size_t memo_state_cap;
    size_t memo_len;
    uint32_t *memo_buckets;
    size_t memo_bucket_cap;

    /* Translation evidence is consulted only while the immutable node graph
     * is compiled.  Keep it after execution-hot frames, values, slots, and
     * memo state so adding a source observer does not perturb their layout. */
    CettaPreparedPureSourceView source_view;
    bool has_native_calls;

    Arena gc_survivor;
    bool gc_survivor_ready;
    size_t gc_survivor_bytes;
    /* The survivor arena's size after the last major collection. */
    size_t gc_major_survivor_bytes;
    size_t gc_low_reclaim_growth_bytes;
    /* A failed structural inspection supplies no stable compiler refusal. */
    bool compile_inspection_incomplete;

};

enum {
    PREPARED_PURE_MAX_COMPILE_DEPTH = 256u,
    PREPARED_PURE_MAX_HEADS = 4096u,
    PREPARED_PURE_MAX_EQUATIONS = 65536u,
    PREPARED_PURE_MAX_SLOTS = 65536u,
    /* Sparse decision lookup is load-bearing for large generated equation
     * families, while the direct matcher is cheaper for tiny groups. */
    PREPARED_PURE_DECISION_MIN_EQUATIONS = 8u,
    PREPARED_PURE_GC_INITIAL_NURSERY_INTERVALS = 3u,
    PREPARED_PURE_MAX_SCALAR_GUARD_ARGUMENTS = 16u,
};

#define PREPARED_PURE_NO_SCALAR_GUARD UINT32_MAX
#define PREPARED_PURE_ROOT_BODY UINT32_MAX
#define PREPARED_PURE_ROOT_FRAME (UINT32_MAX - 1u)

static const PreparedPureBody *prepared_pure_body(
        const CettaPreparedPureProgram *program, uint32_t index) {
    if (program->body_entry && index == PREPARED_PURE_ROOT_BODY)
        return &program->body;
    return index < program->equation_len
        ? &program->equations[index].body : NULL;
}

static bool prepared_pure_debug_enabled(void) {
    static _Thread_local int enabled = -1;
    if (enabled < 0) {
        const char *debug = getenv("CETTA_PREPARED_PURE_DEBUG");
        enabled = debug && debug[0] != '\0' && debug[0] != '0';
    }
    return enabled != 0;
}

static bool prepared_pure_scalar_guard_enabled(void) {
    static _Thread_local int enabled = -1;
    if (enabled < 0) {
        const char *reference = getenv(
            "CETTA_PREPARED_PURE_SCALAR_GUARD_REFERENCE");
        enabled = !(reference && reference[0] != '\0' &&
                    reference[0] != '0');
    }
    return enabled != 0;
}

/* CETTA_PREPARED_PURE_CODE_REFERENCE runs every program on the node
 * executor, the reference register code is measured against. */
static bool prepared_pure_code_enabled(void) {
    static _Thread_local int enabled = -1;
    if (enabled < 0) {
        const char *reference = getenv(
            "CETTA_PREPARED_PURE_CODE_REFERENCE");
        enabled = !(reference && reference[0] != '\0' &&
                    reference[0] != '0');
    }
    return enabled != 0;
}

static bool prepared_pure_is_space_node(const Atom *atom, void *context) {
    (void)context;
    return atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_SPACE;
}

/* HE matches a space against a pattern other than a variable by querying the
 * space, wherever a match happens: unify, let, case, an equation's head.  The
 * machine's matchers compare structure, so in HE a value holding a space is
 * matched by the canonical evaluator.  The identity-grounded summary bit
 * proves the absence of a space in O(1). */
static bool prepared_pure_he_value_holds_space(const Atom *value, bool *complete) {
    if (complete)
        *complete = true;
    if (!value || !atom_has_identity_grounded(value) ||
        !eval_current_language_id || eval_current_language_id() != CETTA_LANGUAGE_HE)
        return false;
    bool finished = false;
    bool found = atom_graph_any_checked(value, prepared_pure_is_space_node,
        NULL, NULL, SIZE_MAX, &finished);
    if (complete)
        *complete = finished;
    return !finished || found;
}

/* A frontend refusal can cross result demands only before the compiler
 * inspected that demand. Later lowering and runtime reads need no record. */
static bool prepared_pure_wants_answers(CettaPreparedPureProgram *program) {
    program->compile_result_demand_observed = true;
    return program->answer_producer;
}

static bool prepared_pure_reject(
    CettaPreparedPureProgram *program, const char *reason, Atom *atom) {
    (void)program;
    if (prepared_pure_debug_enabled()) {
        fprintf(stderr, "prepared-pure compile decline: %s", reason);
        if (atom) {
            fputs(" atom=", stderr);
            atom_print(atom, stderr);
        }
        fputc('\n', stderr);
    }
    return false;
}

static bool prepared_pure_runtime_decline(
    const CettaPreparedPureProgram *program, const char *reason,
    const PreparedPureNode *node) {
    (void)program;
    if (prepared_pure_debug_enabled()) {
        fprintf(stderr, "prepared-pure runtime decline: %s\n", reason);
        if (node && node->atom) {
            fputs("prepared-pure declining operation: ", stderr);
            atom_print(node->atom, stderr);
            fputc('\n', stderr);
        }
    }
    return false;
}

static bool prepared_pure_reserve(
    void **items, size_t item_size, size_t *capacity, size_t required) {
    if (required <= *capacity)
        return true;
    size_t next = *capacity ? *capacity : 16u;
    while (next < required) {
        if (next > SIZE_MAX / 2u)
            return false;
        next *= 2u;
    }
    if (item_size != 0u && next > SIZE_MAX / item_size)
        return false;
    void *grown = realloc(*items, item_size * next);
    if (!grown)
        return false;
    *items = grown;
    *capacity = next;
    return true;
}

/* Give back a vector's capacity beyond twice its length when that frees at
 * least PREPARED_PURE_SHRINK_MIN_BYTES: a later push grows it geometrically
 * again, so a shrink is paid for by the pushes that make the next one
 * possible.  A failed reallocation keeps the vector as it is. */
enum { PREPARED_PURE_SHRINK_MIN_BYTES = 1024u };
static void prepared_pure_shrink(
    void **items, size_t item_size, size_t *capacity, size_t length) {
    size_t keep = length ? length : 1u;
    if (!*items || *capacity / 2u <= keep ||
        (*capacity - keep) * item_size < PREPARED_PURE_SHRINK_MIN_BYTES)
        return;
    void *shrunk = realloc(*items, item_size * keep);
    if (!shrunk)
        return;
    *items = shrunk;
    *capacity = keep;
}

static size_t prepared_pure_symbol_hash(SymbolId symbol) {
    uint64_t bits = (uint64_t)symbol;
    bits ^= bits >> 33u;
    bits *= UINT64_C(0xff51afd7ed558ccd);
    bits ^= bits >> 33u;
    bits *= UINT64_C(0xc4ceb9fe1a85ec53);
    bits ^= bits >> 33u;
    return (size_t)bits;
}

static size_t prepared_pure_callable_hash(
    SymbolId head, uint32_t arity) {
    uint64_t bits = (uint64_t)prepared_pure_symbol_hash(head);
    bits ^= (uint64_t)arity + UINT64_C(0x9e3779b97f4a7c15) +
            (bits << 6u) + (bits >> 2u);
    bits ^= bits >> 33u;
    bits *= UINT64_C(0xff51afd7ed558ccd);
    bits ^= bits >> 33u;
    return (size_t)bits;
}

static bool prepared_pure_callable_bucket_insert(
    CettaPreparedPureProgram *program,
    PreparedPureCallableCacheEntry entry) {
    if (!program || !entry.occupied || entry.head == SYMBOL_ID_NONE ||
        program->callable_bucket_cap == 0u)
        return false;
    size_t mask = program->callable_bucket_cap - 1u;
    size_t bucket = prepared_pure_callable_hash(
        entry.head, entry.arity) & mask;
    for (size_t probe = 0u;
         probe < program->callable_bucket_cap; probe++) {
        PreparedPureCallableCacheEntry *slot =
            &program->callable_buckets[bucket];
        if (!slot->occupied) {
            *slot = entry;
            program->callable_bucket_len++;
            return true;
        }
        if (slot->head == entry.head && slot->arity == entry.arity) {
            slot->callable = entry.callable;
            return true;
        }
        bucket = (bucket + 1u) & mask;
    }
    return false;
}

static bool prepared_pure_callable_buckets_rebuild(
    CettaPreparedPureProgram *program, size_t required_entries) {
    if (!program || required_entries > SIZE_MAX / 2u)
        return false;
    size_t required_buckets = 16u;
    while (required_buckets < required_entries * 2u) {
        if (required_buckets > SIZE_MAX / 2u)
            return false;
        required_buckets *= 2u;
    }
    if (program->callable_bucket_cap >= required_buckets)
        return true;
    PreparedPureCallableCacheEntry *previous =
        program->callable_buckets;
    size_t previous_cap = program->callable_bucket_cap;
    PreparedPureCallableCacheEntry *grown = calloc(
        required_buckets, sizeof(*grown));
    if (!grown)
        return false;
    program->callable_buckets = grown;
    program->callable_bucket_cap = required_buckets;
    program->callable_bucket_len = 0u;
    for (size_t i = 0u; i < previous_cap; i++) {
        if (previous[i].occupied &&
            !prepared_pure_callable_bucket_insert(
                program, previous[i])) {
            free(previous);
            return false;
        }
    }
    free(previous);
    return true;
}

static bool prepared_pure_callable_cache_lookup(
    const CettaPreparedPureProgram *program, SymbolId head,
    uint32_t arity, bool *callable) {
    if (!program || head == SYMBOL_ID_NONE || !callable ||
        program->callable_bucket_cap == 0u)
        return false;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PREPARED_PURE_CALLABLE_LOOKUP);
    size_t mask = program->callable_bucket_cap - 1u;
    size_t bucket = prepared_pure_callable_hash(head, arity) & mask;
    for (size_t probe = 0u;
         probe < program->callable_bucket_cap; probe++) {
        const PreparedPureCallableCacheEntry *slot =
            &program->callable_buckets[bucket];
        if (!slot->occupied)
            return false;
        if (slot->head == head && slot->arity == arity) {
            *callable = slot->callable;
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_PREPARED_PURE_CALLABLE_HIT);
            return true;
        }
        bucket = (bucket + 1u) & mask;
    }
    return false;
}

static bool prepared_pure_callable_cache_store(
    CettaPreparedPureProgram *program, SymbolId head,
    uint32_t arity, bool callable) {
    if (!program || head == SYMBOL_ID_NONE)
        return false;
    if (program->callable_bucket_cap == 0u ||
        program->callable_bucket_len + 1u >
            program->callable_bucket_cap / 2u) {
        if (!prepared_pure_callable_buckets_rebuild(
                program, program->callable_bucket_len + 1u))
            return false;
    }
    return prepared_pure_callable_bucket_insert(
        program, (PreparedPureCallableCacheEntry){
            .head = head,
            .arity = arity,
            .callable = callable,
            .occupied = true,
        });
}

static bool prepared_pure_head_bucket_insert(
    CettaPreparedPureProgram *program, uint32_t head_index) {
    if (!program || head_index >= program->head_len ||
        program->head_bucket_cap == 0u)
        return false;
    size_t mask = program->head_bucket_cap - 1u;
    size_t bucket = prepared_pure_symbol_hash(
        program->heads[head_index].head) & mask;
    for (size_t probe = 0u; probe < program->head_bucket_cap; probe++) {
        if (program->head_buckets[bucket] == 0u) {
            program->head_buckets[bucket] = head_index + 1u;
            return true;
        }
        bucket = (bucket + 1u) & mask;
    }
    return false;
}

static bool prepared_pure_head_buckets_rebuild(
    CettaPreparedPureProgram *program, size_t required_heads) {
    if (!program || required_heads > UINT32_MAX)
        return false;
    size_t required_buckets = 16u;
    while (required_buckets < required_heads * 2u) {
        if (required_buckets > SIZE_MAX / 2u)
            return false;
        required_buckets *= 2u;
    }
    if (program->head_bucket_cap >= required_buckets)
        return true;
    uint32_t *grown = realloc(
        program->head_buckets,
        sizeof(*program->head_buckets) * required_buckets);
    if (!grown)
        return false;
    program->head_buckets = grown;
    program->head_bucket_cap = required_buckets;
    memset(program->head_buckets, 0,
           sizeof(*program->head_buckets) * program->head_bucket_cap);
    for (uint32_t i = 0u; i < program->head_len; i++) {
        if (!prepared_pure_head_bucket_insert(program, i))
            return false;
    }
    return true;
}

static bool prepared_pure_head_bucket_lookup(
    const CettaPreparedPureProgram *program, SymbolId symbol,
    uint32_t *head_index) {
    if (!program || symbol == SYMBOL_ID_NONE || !head_index ||
        program->head_bucket_cap == 0u)
        return false;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PREPARED_PURE_HEAD_LOOKUP);
    size_t mask = program->head_bucket_cap - 1u;
    size_t bucket = prepared_pure_symbol_hash(symbol) & mask;
    for (size_t probe = 0u; probe < program->head_bucket_cap; probe++) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PREPARED_PURE_HEAD_PROBE);
        uint32_t encoded = program->head_buckets[bucket];
        if (encoded == 0u)
            return false;
        uint32_t index = encoded - 1u;
        if (index < program->head_len &&
            program->heads[index].head == symbol) {
            *head_index = index;
            return true;
        }
        bucket = (bucket + 1u) & mask;
    }
    return false;
}

static size_t prepared_pure_memo_hash(Atom *key) {
    uintptr_t bits = (uintptr_t)key;
    bits ^= bits >> 17u;
    bits *= (uintptr_t)UINT64_C(0xed5ad4bb);
    bits ^= bits >> 11u;
    return (size_t)bits;
}

static bool prepared_pure_memo_insert_bucket(
    CettaPreparedPureProgram *program, uint32_t index) {
    if (!program || index >= program->memo_len ||
        !program->memo_keys[index] || program->memo_bucket_cap == 0u)
        return false;
    size_t mask = program->memo_bucket_cap - 1u;
    size_t bucket = prepared_pure_memo_hash(
        program->memo_keys[index]) & mask;
    for (size_t probe = 0u; probe < program->memo_bucket_cap; probe++) {
        if (program->memo_buckets[bucket] == 0u) {
            program->memo_buckets[bucket] = index + 1u;
            return true;
        }
        bucket = (bucket + 1u) & mask;
    }
    return false;
}

/* Moving collection rewrites the live ephemeron keys, so the pointer index is
 * rebuilt after every generated evacuation/compaction pass. */
static bool prepared_pure_memo_rebuild_buckets(
    CettaPreparedPureProgram *program, size_t required_entries) {
    if (!program || required_entries > UINT32_MAX)
        return false;
    size_t required_buckets = 16u;
    while (required_buckets < required_entries * 2u) {
        if (required_buckets > SIZE_MAX / 2u)
            return false;
        required_buckets *= 2u;
    }
    if (program->memo_bucket_cap < required_buckets) {
        uint32_t *grown = realloc(
            program->memo_buckets,
            sizeof(*program->memo_buckets) * required_buckets);
        if (!grown)
            return false;
        program->memo_buckets = grown;
        program->memo_bucket_cap = required_buckets;
    }
    memset(program->memo_buckets, 0,
           sizeof(*program->memo_buckets) * program->memo_bucket_cap);
    for (uint32_t i = 0u; i < program->memo_len; i++) {
        if (!prepared_pure_memo_insert_bucket(program, i))
            return false;
    }
    return true;
}

static bool prepared_pure_memo_lookup(
    const CettaPreparedPureProgram *program, Atom *key,
    uint32_t *index_out) {
    if (index_out)
        *index_out = PREPARED_PURE_NO_MEMO;
    if (!program || !key || !index_out || program->memo_bucket_cap == 0u)
        return false;
    size_t mask = program->memo_bucket_cap - 1u;
    size_t bucket = prepared_pure_memo_hash(key) & mask;
    for (size_t probe = 0u; probe < program->memo_bucket_cap; probe++) {
        uint32_t encoded = program->memo_buckets[bucket];
        if (encoded == 0u)
            return false;
        uint32_t index = encoded - 1u;
        if (index < program->memo_len &&
            program->memo_keys[index] == key) {
            *index_out = index;
            return true;
        }
        bucket = (bucket + 1u) & mask;
    }
    return false;
}

static bool prepared_pure_memo_begin(
    CettaPreparedPureProgram *program, Atom *key,
    uint32_t *index_out, bool *existing_out) {
    if (index_out)
        *index_out = PREPARED_PURE_NO_MEMO;
    if (existing_out)
        *existing_out = false;
    if (!program || !key || !index_out || !existing_out)
        return false;
    if (prepared_pure_memo_lookup(program, key, index_out)) {
        *existing_out = true;
        return true;
    }
    if (program->memo_len >= UINT32_MAX ||
        !prepared_pure_reserve(
            (void **)&program->memo_keys, sizeof(*program->memo_keys),
            &program->memo_key_cap, program->memo_len + 1u) ||
        !prepared_pure_reserve(
            (void **)&program->memo_values, sizeof(*program->memo_values),
            &program->memo_value_cap, program->memo_len + 1u) ||
        !prepared_pure_reserve(
            (void **)&program->memo_states, sizeof(*program->memo_states),
            &program->memo_state_cap, program->memo_len + 1u))
        return false;
    if (program->memo_bucket_cap == 0u ||
        (program->memo_len + 1u) * 10u >=
            program->memo_bucket_cap * 7u) {
        if (!prepared_pure_memo_rebuild_buckets(
                program, program->memo_len + 1u))
            return false;
    }
    uint32_t index = (uint32_t)program->memo_len++;
    program->memo_keys[index] = key;
    program->memo_values[index] = NULL;
    program->memo_states[index] = PREPARED_PURE_MEMO_EVALUATING;
    if (!prepared_pure_memo_insert_bucket(program, index)) {
        program->memo_len--;
        return false;
    }
    *index_out = index;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PREPARED_PURE_CALL_THUNK_MEMO_STORE);
    return true;
}

static bool prepared_pure_memo_complete(
    CettaPreparedPureProgram *program,
    const PreparedPureFrame *frame, Atom *value) {
    if (!program || !frame || !value)
        return false;
    if (frame->memo_index == PREPARED_PURE_NO_MEMO)
        return true;
    if (frame->memo_index >= program->memo_len ||
        program->memo_states[frame->memo_index] !=
            PREPARED_PURE_MEMO_EVALUATING)
        return false;
    program->memo_values[frame->memo_index] = value;
    program->memo_states[frame->memo_index] = PREPARED_PURE_MEMO_VALUE;
    return true;
}

static bool prepared_pure_memo_defer_tail(
    CettaPreparedPureProgram *program,
    const PreparedPureFrame *frame) {
    if (!program || !frame)
        return false;
    if (frame->memo_index == PREPARED_PURE_NO_MEMO)
        return true;
    if (frame->memo_index >= program->memo_len ||
        program->memo_states[frame->memo_index] !=
            PREPARED_PURE_MEMO_EVALUATING ||
        program->memo_values[frame->memo_index] != NULL)
        return false;
    program->memo_states[frame->memo_index] =
        PREPARED_PURE_MEMO_TAIL_PENDING;
    return true;
}

static bool prepared_pure_memo_complete_deferred(
    CettaPreparedPureProgram *program, Atom *value) {
    if (!program || !value)
        return false;
    for (size_t i = 0u; i < program->memo_len; i++) {
        if (program->memo_states[i] !=
            PREPARED_PURE_MEMO_TAIL_PENDING)
            continue;
        if (program->memo_values[i] != NULL)
            return false;
        program->memo_values[i] = value;
        program->memo_states[i] = PREPARED_PURE_MEMO_VALUE;
    }
    return true;
}

static void prepared_pure_memo_clear(
    CettaPreparedPureProgram *program) {
    if (!program)
        return;
    if (program->memo_len > 0u) {
        memset(program->memo_keys, 0,
               sizeof(*program->memo_keys) * program->memo_len);
        memset(program->memo_values, 0,
               sizeof(*program->memo_values) * program->memo_len);
        memset(program->memo_states, 0,
               sizeof(*program->memo_states) * program->memo_len);
    }
    program->memo_len = 0u;
    if (program->memo_buckets && program->memo_bucket_cap > 0u)
        memset(program->memo_buckets, 0,
               sizeof(*program->memo_buckets) *
                   program->memo_bucket_cap);
}

static size_t prepared_pure_saturating_add(size_t left, size_t right) {
    return left > SIZE_MAX - right ? SIZE_MAX : left + right;
}

static size_t prepared_pure_saturating_mul(size_t value, size_t multiplier) {
    return multiplier != 0u && value > SIZE_MAX / multiplier
        ? SIZE_MAX : value * multiplier;
}

static size_t prepared_pure_arena_bytes_above(
    const Arena *arena, ArenaMark anchor) {
    size_t live = arena_accounted_live_bytes(arena);
    size_t base = arena_mark_accounted_live_bytes(anchor);
    return live >= base ? live - base : 0u;
}

/* Node kinds are fixed when a node is appended, and runtime head compilation
 * only appends, so the answer extends over the nodes added since the last
 * scan. */
static bool prepared_pure_plan_builds_structural_values(
    CettaPreparedPureProgram *program) {
    if (!program)
        return false;
    for (; program->structural_scan_len < program->node_len;
         program->structural_scan_len++) {
        const PreparedPureNode *node =
            &program->nodes[program->structural_scan_len];
        if (node->kind == PREPARED_PURE_BUILD ||
            (node->kind == PREPARED_PURE_INTRINSIC &&
             node->intrinsic_instruction ==
                 CETTA_GSLT_PREPARED_PURE_INTRINSIC_DECONSTRUCT_NONEMPTY_EXPRESSION))
            program->builds_structural_values = true;
    }
    return program->builds_structural_values;
}

static size_t prepared_pure_gc_trigger_bytes(
    CettaPreparedPureProgram *program, const Arena *arena,
    ArenaMark anchor, size_t nursery_budget_bytes) {
    if (!program || !arena || nursery_budget_bytes == 0u)
        return SIZE_MAX;
    size_t fresh_threshold = prepared_pure_saturating_add(
        nursery_budget_bytes, program->gc_survivor_bytes);
    fresh_threshold = prepared_pure_saturating_add(
        fresh_threshold, program->gc_low_reclaim_growth_bytes);
    /* The first collection is also the first survival sample.  A generated
     * plan which explicitly builds structural values gets three bounded
     * nursery intervals before paying for that sample; scalar/register-only
     * plans retain the ordinary early collection needed for arithmetic
     * churn.  Every later interval is determined by measured survival and
     * reclamation below. */
    if (!program->gc_survivor_ready &&
        prepared_pure_plan_builds_structural_values(program)) {
        fresh_threshold = prepared_pure_saturating_add(
            fresh_threshold,
            prepared_pure_saturating_mul(
                nursery_budget_bytes,
                PREPARED_PURE_GC_INITIAL_NURSERY_INTERVALS - 1u));
    }
    return prepared_pure_saturating_add(
        arena_mark_accounted_live_bytes(anchor), fresh_threshold);
}

static bool prepared_pure_mark_node_live_slots(
    CettaPreparedPureProgram *program, uint32_t node_index,
    uint32_t local_base) {
    if (!program || node_index >= program->node_len)
        return false;
    const PreparedPureNode *node = &program->nodes[node_index];
    if (node->first_live_slot > program->live_slot_len ||
        node->live_slot_count >
            program->live_slot_len - node->first_live_slot)
        return false;
    for (uint32_t i = 0u; i < node->live_slot_count; i++) {
        uint32_t relative =
            program->live_slots[node->first_live_slot + i];
        size_t absolute = (size_t)local_base + relative;
        if (absolute >= program->slot_len)
            return false;
        program->slot_live[absolute] = 1u;
    }
    return true;
}

static bool prepared_pure_mark_bind_prebound_slots(
    CettaPreparedPureProgram *program, const PreparedPureNode *node,
    uint32_t local_base) {
    if (!program || !node || node->auxiliary >= program->bind_pattern_len)
        return false;
    const PreparedPureBindPattern *pattern =
        &program->bind_patterns[node->auxiliary];
    if (pattern->first_var > program->bind_var_len ||
        pattern->var_count >
            program->bind_var_len - pattern->first_var)
        return false;
    for (uint32_t i = 0u; i < pattern->var_count; i++) {
        const PreparedPureBindVar *binding =
            &program->bind_vars[pattern->first_var + i];
        if (!binding->prebound)
            continue;
        size_t absolute = (size_t)local_base + binding->slot;
        if (absolute >= program->slot_len)
            return false;
        program->slot_live[absolute] = 1u;
    }
    return true;
}

static bool prepared_pure_mark_frame_live_slots(
    CettaPreparedPureProgram *program,
    const PreparedPureFrame *frame) {
    if (!program || !frame)
        return false;
    if (frame->node == PREPARED_PURE_RUNTIME_NODE)
        return true;
    if (frame->node >= program->node_len)
        return false;
    const PreparedPureNode *node = &program->nodes[frame->node];

    if (node->kind == PREPARED_PURE_LITERAL ||
        node->kind == PREPARED_PURE_ENTRY_ARGUMENT ||
        node->kind == PREPARED_PURE_EVAL_ENTRY_ARGUMENT)
        return true;
    if (node->kind == PREPARED_PURE_SLOT ||
        node->kind == PREPARED_PURE_EVAL_SLOT) {
        return frame->state != 0u ||
               prepared_pure_mark_node_live_slots(
                   program, frame->node, frame->local_base);
    }
    if (node->kind == PREPARED_PURE_BIND) {
        if (frame->state == 0u)
            return prepared_pure_mark_node_live_slots(
                program, frame->node, frame->local_base);
        if (frame->state == 1u) {
            return prepared_pure_mark_bind_prebound_slots(
                       program, node, frame->local_base) &&
                   node->child_count == 2u &&
                   prepared_pure_mark_node_live_slots(
                       program,
                       program->children[node->first_child + 1u],
                       frame->local_base);
        }
        return true;
    }
    if (node->kind == PREPARED_PURE_IF) {
        if (frame->state == 0u)
            return prepared_pure_mark_node_live_slots(
                program, frame->node, frame->local_base);
        if (frame->state == 1u) {
            if (node->child_count != 3u)
                return false;
            return prepared_pure_mark_node_live_slots(
                       program,
                       program->children[node->first_child + 1u],
                       frame->local_base) &&
                   prepared_pure_mark_node_live_slots(
                       program,
                       program->children[node->first_child + 2u],
                       frame->local_base);
        }
        return true;
    }

    if (frame->state == 0u)
        return prepared_pure_mark_node_live_slots(
            program, frame->node, frame->local_base);
    if (frame->state != 1u)
        return true;
    if (frame->child_index >= node->child_count)
        return frame->child_index == node->child_count;
    for (uint32_t i = frame->child_index + 1u;
         i < node->child_count; i++) {
        if (!prepared_pure_mark_node_live_slots(
                program, program->children[node->first_child + i],
                frame->local_base))
            return false;
    }
    return true;
}

/* Compile-time node descriptors determine the exact positional environment
 * roots of every active continuation state.  Clearing the complement before
 * evacuation both trims environments and prevents stale from-space pointers
 * from surviving in slots the machine has proved it will never read again. */
static bool prepared_pure_trim_dead_slots(
    CettaPreparedPureProgram *program) {
    if (!program || !prepared_pure_reserve(
            (void **)&program->slot_live, sizeof(*program->slot_live),
            &program->slot_live_cap, program->slot_len))
        return false;
    if (program->slot_len > 0u)
        memset(program->slot_live, 0, program->slot_len);
    for (size_t i = 0u; i < program->frame_len; i++) {
        if (!prepared_pure_mark_frame_live_slots(
                program, &program->frames[i]))
            return false;
    }
    uint64_t live = 0u;
    uint64_t trimmed = 0u;
    for (size_t i = 0u; i < program->slot_len; i++) {
        if (program->slot_live[i]) {
            if (program->slots[i])
                live++;
        } else {
            /* An unwritten temporary has no value to inspect. The body
             * writes it before use, but a collection may precede that
             * write. Clear every dead register without reading its bits. */
            program->slots[i] = NULL;
            trimmed++;
        }
    }
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_PREPARED_PURE_CALL_GC_DEAD_SLOTS,
        trimmed);
    cetta_runtime_stats_update_max(
        CETTA_RUNTIME_COUNTER_PREPARED_PURE_CALL_GC_LIVE_SLOTS_PEAK,
        live);
    return true;
}

static void prepared_pure_gc_discard_survivor(
    CettaPreparedPureProgram *program) {
    if (!program)
        return;
    program->gc_low_reclaim_growth_bytes = 0u;
    if (!program->gc_survivor_ready)
        return;
    arena_free(&program->gc_survivor);
    memset(&program->gc_survivor, 0, sizeof(program->gc_survivor));
    program->gc_survivor_ready = false;
    program->gc_survivor_bytes = 0u;
    program->gc_major_survivor_bytes = 0u;
}

/* Resolve completed update cells before the generic copier traverses their
 * immutable suspension syntax.  This is graph-update path compression: the
 * copied continuation points directly at the value, so evaluated thunk
 * spines do not survive merely because the source Atom graph was immutable. */
static Atom *prepared_pure_gc_resolve_thunk(
    void *context, Atom *source) {
    CettaPreparedPureProgram *program = context;
    if (!program || !source)
        return NULL;
    for (size_t hop = 0u; hop <= program->memo_len; hop++) {
        uint32_t index = PREPARED_PURE_NO_MEMO;
        if (!prepared_pure_memo_lookup(program, source, &index) ||
            program->memo_states[index] != PREPARED_PURE_MEMO_VALUE)
            return source;
        Atom *next = program->memo_values[index];
        if (!next || next == source)
            return NULL;
        source = next;
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PREPARED_PURE_CALL_THUNK_PATH_COMPRESSION);
    }
    return NULL;
}

/* Trace the update table as an ephemeron map.  Strong frame/register roots
 * are copied first by the generated arm.  A memoized value is copied only
 * when that episode has already forwarded its suspension key; copied values
 * can expose more keys, so the scan continues to a least fixed point.
 * Compaction also rewrites every active frame's memo index atomically with
 * the table, preserving the update-cell invariant across moving collection. */
static bool prepared_pure_gc_visit_thunk_updates(
    AtomDeepCopySession *session, PreparedPureEphemeronAtomMap map) {
    CettaPreparedPureProgram *program = map.program;
    if (!session || !program)
        return false;
    size_t old_len = program->memo_len;
    if (old_len == 0u)
        return true;
    if (old_len > SIZE_MAX / sizeof(uint32_t))
        return false;

    uint8_t *keep = calloc(old_len, sizeof(*keep));
    uint32_t *remap = malloc(old_len * sizeof(*remap));
    if (!keep || !remap) {
        free(remap);
        free(keep);
        return false;
    }
    for (size_t i = 0u; i < old_len; i++)
        remap[i] = PREPARED_PURE_NO_MEMO;

    bool changed;
    do {
        changed = false;
        for (size_t i = 0u; i < old_len; i++) {
            if (keep[i])
                continue;
            Atom *old_key = program->memo_keys[i];
            Atom *new_key = atom_deep_copy_session_forwarded(
                session, old_key);
            if (!new_key)
                continue;

            Atom *new_value = NULL;
            if (program->memo_states[i] == PREPARED_PURE_MEMO_VALUE) {
                Atom *old_value = program->memo_values[i];
                if (!old_value || !(new_value =
                        atom_deep_copy_session_copy(session, old_value))) {
                    free(remap);
                    free(keep);
                    return false;
                }
            } else if ((program->memo_states[i] !=
                            PREPARED_PURE_MEMO_EVALUATING &&
                        program->memo_states[i] !=
                            PREPARED_PURE_MEMO_TAIL_PENDING) ||
                       program->memo_values[i] != NULL) {
                free(remap);
                free(keep);
                return false;
            }
            program->memo_keys[i] = new_key;
            program->memo_values[i] = new_value;
            keep[i] = 1u;
            changed = true;
        }
    } while (changed);

    size_t write = 0u;
    for (size_t read = 0u; read < old_len; read++) {
        if (!keep[read])
            continue;
        remap[read] = (uint32_t)write;
        if (write != read) {
            program->memo_keys[write] = program->memo_keys[read];
            program->memo_values[write] = program->memo_values[read];
            program->memo_states[write] = program->memo_states[read];
        }
        write++;
    }
    for (size_t i = write; i < old_len; i++) {
        program->memo_keys[i] = NULL;
        program->memo_values[i] = NULL;
        program->memo_states[i] = 0u;
    }
    for (size_t i = 0u; i < program->frame_len; i++) {
        uint32_t old_index = program->frames[i].memo_index;
        if (old_index == PREPARED_PURE_NO_MEMO)
            continue;
        if (old_index >= old_len ||
            remap[old_index] == PREPARED_PURE_NO_MEMO) {
            free(remap);
            free(keep);
            return false;
        }
        program->frames[i].memo_index = remap[old_index];
    }
    program->memo_len = write;
    cetta_runtime_stats_update_max(
        CETTA_RUNTIME_COUNTER_PREPARED_PURE_CALL_EPHEMERON_LIVE_PEAK,
        (uint64_t)write);
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_PREPARED_PURE_CALL_EPHEMERON_RECLAIMED,
        (uint64_t)(old_len - write));
    free(remap);
    free(keep);
    return true;
}

/* Register code collects only at calls and returns.  A suspended activation
 * reads, after its call returns, the registers its call's continuation
 * reads.  The running activation also reads its call's operands at a call,
 * and at a return the destination its call's value has just been written
 * to.  Every other register is cleared, as prepared_pure_trim_dead_slots
 * clears dead slots. */
static bool prepared_pure_code_trim_dead_registers(
    CettaPreparedPureProgram *program) {
    if (!prepared_pure_reserve(
            (void **)&program->slot_live, sizeof(*program->slot_live),
            &program->slot_live_cap, program->slot_len))
        return false;
    if (program->slot_len > 0u)
        memset(program->slot_live, 0, program->slot_len);
    for (size_t f = 0u; f < program->code_frame_len; f++) {
        const PreparedPureCodeFrame *frame = &program->code_frames[f];
        if (frame->pc == 0u || frame->pc > program->code_len)
            return false;
        const PreparedPureCode *code = &program->code[frame->pc - 1u];
        if (code->kind != PREPARED_PURE_CODE_CALL &&
            code->kind != PREPARED_PURE_CODE_TAIL_CALL)
            return false;
        for (uint32_t i = 0u; i < code->live_count; i++) {
            size_t slot = (size_t)frame->base +
                program->code_live[code->first_live + i];
            if (slot >= program->slot_len)
                return false;
            program->slot_live[slot] = 1u;
        }
        if (f + 1u < program->code_frame_len)
            continue;
        if (program->code_resumed) {
            size_t slot = (size_t)frame->base + code->destination;
            if (code->kind != PREPARED_PURE_CODE_CALL ||
                slot >= program->slot_len)
                return false;
            program->slot_live[slot] = 1u;
            continue;
        }
        const uint32_t *operands =
            &program->code_operands[code->first_operand];
        for (uint32_t i = 0u; i < code->operand_count; i++) {
            if (operands[i] & PREPARED_PURE_CODE_LITERAL)
                continue;
            size_t slot = (size_t)frame->base + operands[i];
            if (slot >= program->slot_len)
                return false;
            program->slot_live[slot] = 1u;
        }
    }
    uint64_t live = 0u;
    uint64_t trimmed = 0u;
    for (size_t i = 0u; i < program->slot_len; i++) {
        if (program->slot_live[i]) {
            if (program->slots[i])
                live++;
        } else {
            /* Dead registers include temporaries not written yet. Never
             * inspect their bits, even to count reclaimed references. */
            program->slots[i] = NULL;
            trimmed++;
        }
    }
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_PREPARED_PURE_CALL_GC_DEAD_SLOTS,
        trimmed);
    cetta_runtime_stats_update_max(
        CETTA_RUNTIME_COUNTER_PREPARED_PURE_CALL_GC_LIVE_SLOTS_PEAK,
        live);
    return true;
}

/* The slots or registers the machine will not read again are cleared
 * first, so collection keeps only what the rest of the run reads. */
static __attribute__((noinline, cold)) size_t prepared_pure_gc_input_arenas(
    const CettaPreparedPureAnswerCursor *cursor, const Arena *arena,
    const Arena *survivor, uint32_t *identities, size_t capacity);

static void prepared_pure_gc_collect(
    CettaPreparedPureProgram *program, Arena *arena, ArenaMark anchor,
    const CettaPreparedPureAnswerCursor *cursor) {
    if (!program || !arena)
        return;
    if (!(program->code_frame_len == 0u
              ? prepared_pure_trim_dead_slots(program)
              : prepared_pure_code_trim_dead_registers(program)))
        return;

    size_t before_fresh = prepared_pure_arena_bytes_above(arena, anchor);
    size_t before_survivor = program->gc_survivor_ready
        ? arena_accounted_live_bytes(&program->gc_survivor) : 0u;
    size_t before = prepared_pure_saturating_add(
        before_fresh, before_survivor);

    /* A minor collection copies the nursery into the survivor arena.  An
     * atom is built after its children and never changes, so no atom the
     * survivor arena holds points into the nursery, and the copy session
     * keeps every atom that arena owns and is closed for, without copying
     * it.  A major collection copies everything into a fresh arena, once
     * the survivor arena has doubled since the last one, so dead survivors
     * cost at most as much again as live ones.  A program with update
     * cells collects majors only: its ephemeron pass keeps a cell whose key
     * the strong roots reached, and a kept survivor is not reached by
     * copying. */
    bool minor = program->gc_survivor_ready && program->memo_len == 0u &&
        before_survivor / 2u < program->gc_major_survivor_bytes;
    Arena evacuated;
    Arena *destination = &program->gc_survivor;
    if (!minor) {
        arena_init(&evacuated);
        arena_set_hashcons(&evacuated, NULL);
        arena_set_runtime_kind(
            &evacuated, CETTA_ARENA_RUNTIME_KIND_SURVIVOR);
        destination = &evacuated;
    }
    AtomDeepCopySession *session = atom_deep_copy_session_new(destination);
    if (!session) {
        if (!minor)
            arena_free(&evacuated);
        return;
    }
    /* Only an update cell resolves a copied atom; with none, every atom
     * is copied as it is. */
    if (program->memo_len > 0u)
        atom_deep_copy_session_set_resolver(
            session, prepared_pure_gc_resolve_thunk, program);
    /* The nursery dies with this collection, so without update cells to
     * resolve its atoms record their copies in place (Cheney forwarding). */
    else
        (void)atom_deep_copy_session_forward_region(session, arena, anchor);

    /* Entry storage remains borrowed until this cursor closes or detaches.
     * Copying collection may keep its closed subgraphs without certifying
     * persistent ownership. Update-cell resolution still traverses them. */
    uint32_t loan_arenas[64];
    if (cursor && program->memo_len == 0u) {
        size_t loan_count = prepared_pure_gc_input_arenas(cursor, arena,
            program->gc_survivor_ready ? &program->gc_survivor : NULL,
            loan_arenas, sizeof(loan_arenas) / sizeof(*loan_arenas));
        (void)atom_deep_copy_session_loan_arenas(session, loan_arenas, loan_count);
    }

    PreparedPureGcRoots roots = {
        .values = {program->values, program->value_len},
        .live_slots = {program->slots, program->slot_len},
        .runtime_frames = {program->frame_atoms, program->frame_len},
        .thunk_updates = {program},
    };
    bool copied = true;
#define CETTA_GC_VISIT_STRONG_ATOM_SPAN(SESSION, SPAN, FAIL) do {          \
    PreparedPureAtomSpan prepared_pure_span__ = (SPAN);                   \
    for (size_t prepared_pure_i__ = 0u;                                  \
         prepared_pure_i__ < prepared_pure_span__.len;                   \
         prepared_pure_i__++) {                                          \
        Atom **prepared_pure_slot__ =                                    \
            &prepared_pure_span__.items[prepared_pure_i__];              \
        if (!*prepared_pure_slot__)                                      \
            continue;                                                    \
        Atom *prepared_pure_next__ = atom_deep_copy_session_copy(        \
            (SESSION), *prepared_pure_slot__);                           \
        if (!prepared_pure_next__) { FAIL; }                             \
        *prepared_pure_slot__ = prepared_pure_next__;                    \
    }                                                                    \
} while (0)
#define CETTA_GC_VISIT_EPHEMERON_ATOM_MAP(SESSION, MAP, FAIL) do {         \
    if (!prepared_pure_gc_visit_thunk_updates((SESSION), (MAP))) {        \
        FAIL;                                                             \
    }                                                                     \
} while (0)
    CETTA_EVAL_GC_ARM_prepared_pure_machine(
        session, &roots, goto copy_failed);
    if (!prepared_pure_memo_rebuild_buckets(
            program, program->memo_len))
        goto copy_failed;
    goto copy_finished;
copy_failed:
    copied = false;
copy_finished:
#undef CETTA_GC_VISIT_EPHEMERON_ATOM_MAP
#undef CETTA_GC_VISIT_STRONG_ATOM_SPAN
    atom_deep_copy_session_free(session);
    if (!copied) {
        /* A prior root may already point into the new semispace. */
        fputs("fatal: prepared pure-machine evacuation failed\n", stderr);
        abort();
    }

    arena_reset(arena, anchor);
    if (!minor) {
        if (program->gc_survivor_ready)
            arena_free(&program->gc_survivor);
        arena_set_runtime_kind(&evacuated, CETTA_ARENA_RUNTIME_KIND_OTHER);
        program->gc_survivor = evacuated;
        memset(&evacuated, 0, sizeof(evacuated));
        arena_set_runtime_kind(
            &program->gc_survivor, CETTA_ARENA_RUNTIME_KIND_SURVIVOR);
        arena_set_hashcons(&program->gc_survivor, NULL);
        program->gc_survivor_ready = true;
    }
    program->gc_survivor_bytes =
        arena_accounted_live_bytes(&program->gc_survivor);
    if (!minor)
        program->gc_major_survivor_bytes = program->gc_survivor_bytes;

    size_t reclaimed = before > program->gc_survivor_bytes
        ? before - program->gc_survivor_bytes : 0u;
    /* A copying collection that retains at least three quarters of its
     * input is dominated by immutable live structure, not garbage.  Give
     * that live graph one additional survivor-sized growth interval before
     * copying it again.  Reclamation-rich calls retain the ordinary budget,
     * and this evidence is reset with the invocation's survivor arena. */
    program->gc_low_reclaim_growth_bytes =
        before > 0u && reclaimed <= before / 4u
            ? program->gc_survivor_bytes : 0u;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PREPARED_PURE_CALL_GC_COLLECTION);
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_PREPARED_PURE_CALL_GC_EVACUATED_BYTES,
        (uint64_t)program->gc_survivor_bytes);
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_PREPARED_PURE_CALL_GC_RECLAIMED_BYTES,
        (uint64_t)reclaimed);
    cetta_runtime_stats_update_max(
        CETTA_RUNTIME_COUNTER_PREPARED_PURE_CALL_GC_SURVIVOR_BYTES_PEAK,
        (uint64_t)program->gc_survivor_bytes);
    (void)reclaimed;
}


static bool prepared_pure_register_program(
    const CettaPreparedPureProgram *program,
    SymbolId head, CettaExprLen arity,
    CettaGsltRegisterResultKind *kind_out,
    CettaGsltRegisterInstruction *instruction_out) {
    /* A register instruction realizes an operation the current dialect
     * defines; one it leaves undefined is data. */
    CettaGsltRegisterResultKind kind;
    CettaGsltRegisterInstruction instruction;
    if (cetta_gslt_register_head_program(
            head, arity, &kind, &instruction, &g_builtin_syms) &&
        (!grounded_op_is_cetta_only(head) || is_grounded_op(head))) {
        if (kind_out)
            *kind_out = kind;
        if (instruction_out)
            *instruction_out = instruction;
        return true;
    }
    if (!program || !program->register_view)
        return false;
    CettaGsltRegisterOperandDiscipline discipline;
    if (!program->register_view(
            head, arity, &kind, &instruction) ||
        (kind != CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER &&
         kind != CETTA_GSLT_REGISTER_RESULT_BOOLEAN) ||
        !cetta_gslt_register_operand_discipline(
            instruction, &discipline))
        return false;
    (void)discipline;
    if (kind_out)
        *kind_out = kind;
    if (instruction_out)
        *instruction_out = instruction;
    return true;
}

/* A BUILD whose head is bound at run time rather than named: it builds data
 * only when the head's value is data. */
enum { PREPARED_PURE_BUILD_DATA_HEAD = 1u };

/* A value that heads data and never a call: a number or a string. */
static bool prepared_pure_data_head_value(const Atom *value) {
    return value && value->kind == ATOM_GROUNDED &&
        (value->ground.gkind == GV_INT ||
         value->ground.gkind == GV_FLOAT ||
         value->ground.gkind == GV_BIGINT ||
         value->ground.gkind == GV_RATIONAL ||
         value->ground.gkind == GV_STRING);
}

/* Whether `head` computes an exact integer: a register whose result is one.
 * No equation is headed by a number, so an expression whose head computes
 * one is data over its evaluated fields. */
static bool prepared_pure_integer_head(
    const CettaPreparedPureProgram *program, const Atom *head) {
    CettaGsltRegisterResultKind kind;
    return head && head->kind == ATOM_EXPR && head->expr.len > 0u &&
        head->expr.elems[0]->kind == ATOM_SYMBOL &&
        prepared_pure_register_program(
            program, head->expr.elems[0]->sym_id, head->expr.len - 1u,
            &kind, NULL) &&
        kind == CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER;
}

bool cetta_prepared_pure_intrinsic_scope_supported(SymbolId head, CettaExprLen arity) {
    uint32_t dialect = 0u;
    if (eval_current_language_id) {
        switch (eval_current_language_id()) {
        case CETTA_LANGUAGE_HE: dialect = CETTA_GSLT_INTRINSIC_DIALECT_HE; break;
        case CETTA_LANGUAGE_PETTA: dialect = CETTA_GSLT_INTRINSIC_DIALECT_PETTA; break;
        case CETTA_LANGUAGE_PRIME: dialect = CETTA_GSLT_INTRINSIC_DIALECT_PRIME; break;
        default: break;
        }
    }
    return cetta_gslt_prepared_intrinsic_scope(
        head, arity, dialect, &g_builtin_syms);
}

bool cetta_prepared_pure_intrinsic_program(
    SymbolId head, CettaExprLen arity,
    CettaGsltPreparedPureIntrinsicInstruction *instruction_out) {
    CettaGsltPreparedPureIntrinsicOperandDiscipline discipline;
    return cetta_prepared_pure_intrinsic_scope_supported(head, arity) &&
        cetta_gslt_prepared_intrinsic_program(
            head, arity, &discipline, instruction_out, &g_builtin_syms) &&
        discipline == CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL;
}

static bool prepared_pure_builtin_syntax(SymbolId head) {
    return symbol_id_is_builtin(head);
}

static bool prepared_pure_context_lookup(
    const PreparedPureCompileContext *context, VarId var,
    uint32_t *slot_out) {
    if (!context || var_id_is_none(var))
        return false;
    for (size_t i = context->len; i > 0u; i--) {
        if (var_id_eq(context->bindings[i - 1u].var, var)) {
            if (slot_out)
                *slot_out = context->bindings[i - 1u].slot;
            return true;
        }
    }
    return false;
}

static bool prepared_pure_context_bind(
    PreparedPureCompileContext *context, VarId var,
    bool allow_shadow, uint32_t *slot_out) {
    if (!context || var_id_is_none(var) ||
        context->next_slot >= PREPARED_PURE_MAX_SLOTS)
        return false;
    uint32_t existing = 0u;
    if (!allow_shadow &&
        prepared_pure_context_lookup(context, var, &existing))
        return false;
    if (!prepared_pure_reserve(
            (void **)&context->bindings, sizeof(*context->bindings),
            &context->cap, context->len + 1u))
        return false;
    uint32_t slot = context->next_slot++;
    context->bindings[context->len++] = (PreparedPureVarSlot){
        .var = var,
        .slot = slot,
    };
    if (slot_out)
        *slot_out = slot;
    return true;
}

static bool prepared_pure_bind_var_index(
    const CettaPreparedPureProgram *program,
    uint32_t first_var, uint32_t var_count, VarId var,
    uint32_t *index_out) {
    if (!program || !index_out || var_id_is_none(var) ||
        first_var > program->bind_var_len ||
        var_count > program->bind_var_len - first_var)
        return false;
    for (uint32_t i = 0u; i < var_count; i++) {
        if (var_id_eq(program->bind_vars[first_var + i].var, var)) {
            *index_out = i;
            return true;
        }
    }
    return false;
}

/* Compile a binder pattern into positional slot actions.  A variable already
 * visible at the binder is a rigid reference; the first occurrence of a fresh
 * variable allocates a slot, and later occurrences share that same slot. */
static bool prepared_pure_compile_bind_pattern(
    CettaPreparedPureProgram *program,
    PreparedPureCompileContext *context, Atom *pattern,
    uint32_t *pattern_index_out) {
    if (!program || !context || !pattern || !pattern_index_out ||
        program->bind_pattern_len >= UINT32_MAX ||
        program->bind_var_len >= UINT32_MAX)
        return false;

    const size_t saved_context_len = context->len;
    const uint32_t saved_next_slot = context->next_slot;
    const size_t saved_bind_var_len = program->bind_var_len;
    const uint32_t first_var = (uint32_t)saved_bind_var_len;
    Atom **stack = NULL;
    size_t stack_len = 0u;
    size_t stack_cap = 0u;
    bool ok = prepared_pure_reserve(
        (void **)&stack, sizeof(*stack), &stack_cap, 1u);
    if (ok)
        stack[stack_len++] = pattern;

    while (ok && stack_len > 0u) {
        Atom *current = stack[--stack_len];
        if (!current) {
            ok = false;
            break;
        }
        if (current->kind == ATOM_VAR) {
            uint32_t ignored = 0u;
            uint32_t count = (uint32_t)(
                program->bind_var_len - saved_bind_var_len);
            if (prepared_pure_bind_var_index(
                    program, first_var, count,
                    current->var_id, &ignored))
                continue;

            uint32_t slot = 0u;
            bool prebound = prepared_pure_context_lookup(
                context, current->var_id, &slot);
            if (!prebound && !prepared_pure_context_bind(
                    context, current->var_id, false, &slot)) {
                ok = false;
                break;
            }
            if (program->bind_var_len >= UINT32_MAX ||
                !prepared_pure_reserve(
                    (void **)&program->bind_vars,
                    sizeof(*program->bind_vars),
                    &program->bind_var_cap,
                    program->bind_var_len + 1u)) {
                ok = false;
                break;
            }
            program->bind_vars[program->bind_var_len++] =
                (PreparedPureBindVar){
                    .var = current->var_id,
                    .slot = slot,
                    .prebound = prebound,
                };
            continue;
        }
        if (current->kind != ATOM_EXPR)
            continue;
        if (!prepared_pure_reserve(
                (void **)&stack, sizeof(*stack), &stack_cap,
                stack_len + current->expr.len)) {
            ok = false;
            break;
        }
        for (CettaExprIndex i = current->expr.len; i > 0u; i--)
            stack[stack_len++] = current->expr.elems[i - 1u];
    }
    free(stack);

    size_t count_size = program->bind_var_len - saved_bind_var_len;
    if (!ok || count_size > UINT32_MAX ||
        !prepared_pure_reserve(
            (void **)&program->bind_patterns,
            sizeof(*program->bind_patterns),
            &program->bind_pattern_cap,
            program->bind_pattern_len + 1u)) {
        context->len = saved_context_len;
        context->next_slot = saved_next_slot;
        program->bind_var_len = saved_bind_var_len;
        return false;
    }
    bool flat = pattern->kind == ATOM_EXPR && pattern->expr.len > 0u &&
        pattern->expr.elems[0] &&
        pattern->expr.elems[0]->kind != ATOM_VAR &&
        pattern->expr.elems[0]->kind != ATOM_EXPR &&
        count_size == pattern->expr.len - 1u;
    for (CettaExprIndex i = 1u; flat && i < pattern->expr.len; i++) {
        const PreparedPureBindVar *binding =
            &program->bind_vars[first_var + i - 1u];
        flat = pattern->expr.elems[i] &&
            pattern->expr.elems[i]->kind == ATOM_VAR &&
            var_id_eq(binding->var, pattern->expr.elems[i]->var_id) &&
            !binding->prebound;
    }
    *pattern_index_out = (uint32_t)program->bind_pattern_len;
    program->bind_patterns[program->bind_pattern_len++] =
        (PreparedPureBindPattern){
            .pattern = pattern,
            .first_var = first_var,
            .var_count = (uint32_t)count_size,
            .flat_fresh_fields = flat,
        };
    return true;
}

static bool prepared_pure_append_node_live_slot(
    CettaPreparedPureProgram *program, size_t first, uint32_t slot) {
    if (!program || first > program->live_slot_len)
        return false;
    for (size_t i = first; i < program->live_slot_len; i++) {
        if (program->live_slots[i] == slot)
            return true;
    }
    if (program->live_slot_len >= UINT32_MAX ||
        !prepared_pure_reserve(
            (void **)&program->live_slots,
            sizeof(*program->live_slots), &program->live_slot_cap,
            program->live_slot_len + 1u))
        return false;
    program->live_slots[program->live_slot_len++] = slot;
    return true;
}

static bool prepared_pure_add_node(
    CettaPreparedPureProgram *program, PreparedPureNode node,
    const uint32_t *children, uint32_t child_count,
    uint32_t *node_out) {
    if (!program || !node_out ||
        program->node_len >= UINT32_MAX ||
        program->child_len > UINT32_MAX - child_count)
        return false;
    if (node.kind == PREPARED_PURE_LITERAL) {
        bool complete = true;
        if (prepared_pure_he_value_holds_space(node.atom, &complete)) {
            if (!complete) {
                program->compile_inspection_incomplete = true;
                return false;
            }
            return prepared_pure_reject(
                program, "a literal holds a space, which HE matches by query", node.atom);
        }
    }
    if (!prepared_pure_reserve(
            (void **)&program->children, sizeof(*program->children),
            &program->child_cap, program->child_len + child_count) ||
        !prepared_pure_reserve(
            (void **)&program->nodes, sizeof(*program->nodes),
            &program->node_cap, program->node_len + 1u))
        return false;
    if (program->answer_producer && program->source_view.project_source &&
        !prepared_pure_reserve((void **)&program->node_sources,
            sizeof(*program->node_sources), &program->node_source_cap, program->node_len + 1u))
        return false;

    size_t first_live = program->live_slot_len;
    bool live_ok = true;
    if (node.kind == PREPARED_PURE_SLOT ||
        node.kind == PREPARED_PURE_EVAL_SLOT) {
        live_ok = prepared_pure_append_node_live_slot(
            program, first_live, node.auxiliary);
    } else if (node.kind == PREPARED_PURE_BIND) {
        if (node.auxiliary >= program->bind_pattern_len) {
            live_ok = false;
        } else {
            const PreparedPureBindPattern *pattern =
                &program->bind_patterns[node.auxiliary];
            if (pattern->first_var > program->bind_var_len ||
                pattern->var_count >
                    program->bind_var_len - pattern->first_var) {
                live_ok = false;
            } else {
                for (uint32_t i = 0u;
                     live_ok && i < pattern->var_count; i++) {
                    const PreparedPureBindVar *binding =
                        &program->bind_vars[pattern->first_var + i];
                    if (binding->prebound)
                        live_ok = prepared_pure_append_node_live_slot(
                            program, first_live, binding->slot);
                }
            }
        }
    }
    for (uint32_t i = 0u; live_ok && i < child_count; i++) {
        if (children[i] >= program->node_len) {
            live_ok = false;
            break;
        }
        const PreparedPureNode *child = &program->nodes[children[i]];
        if (child->first_live_slot > program->live_slot_len ||
            child->live_slot_count >
                program->live_slot_len - child->first_live_slot) {
            live_ok = false;
            break;
        }
        for (uint32_t j = 0u;
             live_ok && j < child->live_slot_count; j++) {
            live_ok = prepared_pure_append_node_live_slot(
                program, first_live,
                program->live_slots[child->first_live_slot + j]);
        }
    }
    size_t live_count = program->live_slot_len - first_live;
    if (!live_ok || first_live > UINT32_MAX || live_count > UINT32_MAX) {
        program->live_slot_len = first_live;
        return false;
    }
    node.first_live_slot = (uint32_t)first_live;
    node.live_slot_count = (uint32_t)live_count;
    node.first_child = (uint32_t)program->child_len;
    node.child_count = child_count;
    if (child_count > 0u)
        memcpy(&program->children[program->child_len], children,
               sizeof(*children) * child_count);
    program->child_len += child_count;
    *node_out = (uint32_t)program->node_len;
    if (program->node_sources)
        program->node_sources[program->node_len] = program->compiling_source;
    program->nodes[program->node_len++] = node;
    program->has_native_calls |= node.kind == PREPARED_PURE_INTRINSIC &&
        node.intrinsic_instruction ==
            CETTA_GSLT_PREPARED_PURE_INTRINSIC_LIBRARY_VALUE_DISPATCH;
    return true;
}

/* The classes of values HE's type cast decides by kind alone.  A parameter
 * or range of class UNDEFINED (%Undefined%) accepts every value; UNKNOWN is
 * only ever a static answer: nothing is known of the value. */
typedef enum {
    PREPARED_PURE_VALUE_UNDEFINED = 0,
    PREPARED_PURE_VALUE_NUMBER,
    PREPARED_PURE_VALUE_STRING,
    PREPARED_PURE_VALUE_BOOL,
    PREPARED_PURE_VALUE_UNKNOWN,
} PreparedPureValueClass;

/* The class a declared type names, or false when it names none. */
static bool prepared_pure_type_value_class(const Atom *type, uint8_t *out) {
    if (!type || type->kind != ATOM_SYMBOL)
        return false;
    if (type->sym_id == g_builtin_syms.undefined_type) {
        *out = PREPARED_PURE_VALUE_UNDEFINED;
        return true;
    }
    if (type->sym_id == symbol_lookup_cstr(g_symbols, "Number"))
        *out = PREPARED_PURE_VALUE_NUMBER;
    else if (type->sym_id == symbol_lookup_cstr(g_symbols, "String"))
        *out = PREPARED_PURE_VALUE_STRING;
    else if (type->sym_id == symbol_lookup_cstr(g_symbols, "Bool"))
        *out = PREPARED_PURE_VALUE_BOOL;
    else
        return false;
    return true;
}

/* The class of a value, as get_grounded_type names it. */
static PREPARED_PURE_ALWAYS_INLINE uint8_t prepared_pure_value_class(
    const Atom *value) {
    if (!value || value->kind != ATOM_GROUNDED)
        return PREPARED_PURE_VALUE_UNKNOWN;
    switch (value->ground.gkind) {
    case GV_INT:
    case GV_BIGINT:
    case GV_RATIONAL:
    case GV_FLOAT:
        return PREPARED_PURE_VALUE_NUMBER;
    case GV_STRING:
        return PREPARED_PURE_VALUE_STRING;
    case GV_BOOL:
        return PREPARED_PURE_VALUE_BOOL;
    default:
        return PREPARED_PURE_VALUE_UNKNOWN;
    }
}

/* PeTTa's guard at a typed call evaluates get-type, then get-metatype, and a
 * program may extend either.  An authored alternative, or a type annotation
 * of a value subject, can give a value another type answer, which continues
 * the call again, with whatever the classifier does.  A value's class stands
 * for the guard only where neither can, as for the canonical machine's
 * intrinsic guard. */
static bool prepared_pure_petta_intrinsic_type_guard(Space *space) {
    return eval_petta_classifier_extended &&
        !eval_petta_classifier_extended(space, g_builtin_syms.get_type) &&
        !eval_petta_classifier_extended(space, g_builtin_syms.get_metatype) &&
        space_type_annotations_have_only_symbol_subjects(space);
}

static bool prepared_pure_type_mentions_var(
    const Atom *type, VarId id, uint32_t depth) {
    if (type->kind == ATOM_VAR)
        return var_id_eq(type->var_id, id);
    if (type->kind != ATOM_EXPR)
        return false;
    if (depth > PREPARED_PURE_MAX_COMPILE_DEPTH)
        return true;
    for (CettaExprIndex i = 0u; i < type->expr.len; i++) {
        if (prepared_pure_type_mentions_var(
                type->expr.elems[i], id, depth + 1u))
            return true;
    }
    return false;
}

/* Whether `head`'s one equation, over distinct variables, answers its
 * parameter `index` as it is given: (= (head ... $x ...) $x). */
static bool prepared_pure_head_answers_parameter(
    Space *space, SymbolId head, CettaExprLen arity, CettaExprLen index) {
    Atom *equation = space_single_linear_equation(space, head);
    if (!equation || equation->kind != ATOM_EXPR || equation->expr.len != 3u)
        return false;
    Atom *lhs = equation->expr.elems[1];
    Atom *rhs = equation->expr.elems[2];
    return lhs->kind == ATOM_EXPR &&
        lhs->expr.len == (CettaExprLen)(arity + 1u) &&
        lhs->expr.elems[index + 1u]->kind == ATOM_VAR &&
        rhs->kind == ATOM_VAR &&
        var_id_eq(rhs->var_id, lhs->expr.elems[index + 1u]->var_id);
}

/* A head's one signature, when it is an arrow of `arity` parameters whose
 * parameters and range all name value classes, or, in HE, type variables
 * the machine needs no type for: classes[0, arity) the parameters',
 * classes[arity] the range's.  HE selects among several
 * function types, and evaluates a head with another type as a tuple too;
 * the machine leaves such a head to the canonical evaluator.  PeTTa 1.0.6
 * checks the same classes where it calls, and answers nothing for a call
 * that fails them, where its guard is the intrinsic one; its typed line
 * checks calls by its own contracts. */
static bool prepared_pure_value_signature(
    Space *space, SymbolId head, CettaExprLen arity, uint8_t *classes) {
    if (!space || head == SYMBOL_ID_NONE || arity > 64u ||
        !eval_current_language_id)
        return false;
    CettaLanguageId language = eval_current_language_id();
    if (language != CETTA_LANGUAGE_HE &&
        (language != CETTA_LANGUAGE_PETTA ||
         !eval_current_petta_typed_line || eval_current_petta_typed_line() ||
         !prepared_pure_petta_intrinsic_type_guard(space)))
        return false;
    Atom *head_atom = atom_symbol_canonical(head);
    if (!head_atom)
        return false;
    Arena scratch;
    arena_init_detached(&scratch);
    Atom **types = NULL;
    uint32_t count = eval_get_atom_types_profiled(
        space, &scratch, head_atom, &types);
    bool ok = count == 1u && types && types[0] &&
        types[0]->kind == ATOM_EXPR &&
        types[0]->expr.len == (CettaExprLen)(arity + 2u) &&
        atom_is_symbol_id(types[0]->expr.elems[0], g_builtin_syms.arrow);
    for (CettaExprLen i = 0u; ok && i <= arity; i++) {
        Atom *type = types[0]->expr.elems[i + 1u];
        if (type->kind == ATOM_VAR) {
            /* In HE a type variable written once accepts every value, as
             * %Undefined% does, and asks nothing of the answer.  Written in
             * two parameters, it asks their types to agree, which a value's
             * class does not show.  A parameter's variable that is also the
             * range asks the answer to have the argument's type, which the
             * machine shows only where the head answers that argument.
             * PeTTa's guard continues a call once for each type answer of
             * the argument that the variable matches, which a class does
             * not count. */
            classes[i] = PREPARED_PURE_VALUE_UNDEFINED;
            ok = language == CETTA_LANGUAGE_HE;
            for (CettaExprLen j = 0u; ok && i < arity && j <= arity; j++)
                ok = j == i ||
                    !prepared_pure_type_mentions_var(
                        types[0]->expr.elems[j + 1u], type->var_id, 0u) ||
                    (j == arity &&
                     types[0]->expr.elems[j + 1u]->kind == ATOM_VAR &&
                     prepared_pure_head_answers_parameter(
                         space, head, arity, i));
        } else {
            ok = prepared_pure_type_value_class(type, &classes[i]);
        }
    }
    free(types);
    arena_free(&scratch);
    return ok;
}

bool cetta_prepared_pure_call_policy_supported(
    Space *space, SymbolId head, CettaExprLen arity) {
    uint8_t classes[66];
    return space_head_call_untyped(space, head, arity) ||
        prepared_pure_value_signature(space, head, arity, classes);
}

/* The policy of calls of head `head_index` with `arity` arguments: untyped,
 * or the head's value-class signature, which the head then records.  The
 * machine evaluates a typed head's arguments as it evaluates an untyped
 * head's; HE then casts each value to its parameter's type, which the call
 * checks (prepared_pure_typed_arguments_accepted). */
static bool prepared_pure_head_call_policy(
    CettaPreparedPureProgram *program, uint32_t head_index,
    CettaExprLen arity) {
    PreparedPureHead *head = &program->heads[head_index];
    if (head->typed)
        return head->typed_arity == arity;
    if (space_head_call_untyped(program->space, head->head, arity))
        return true;
    uint8_t classes[66];
    if (prepared_pure_wants_answers(program) ||
        program->call_mode != CETTA_GSLT_PURE_CALL_EAGER ||
        !prepared_pure_value_signature(
            program->space, head->head, arity, classes) ||
        !prepared_pure_reserve(
            (void **)&program->value_classes,
            sizeof(*program->value_classes), &program->value_class_cap,
            program->value_class_len + arity))
        return false;
    head = &program->heads[head_index];
    head->typed = true;
    head->typed_arity = arity;
    head->first_value_class = (uint32_t)program->value_class_len;
    head->range_class = classes[arity];
    head->checks_arguments = false;
    for (CettaExprLen i = 0u; i < arity; i++) {
        program->value_classes[program->value_class_len++] = classes[i];
        head->checks_arguments = head->checks_arguments ||
            classes[i] != PREPARED_PURE_VALUE_UNDEFINED;
    }
    return true;
}

/* HE casts each argument of a typed call to its parameter's type before it
 * chooses an equation.  A value of the parameter's class passes; any other
 * argument leaves the call to the canonical evaluator, which answers it
 * with HE's own value or type error. */
static PREPARED_PURE_ALWAYS_INLINE bool prepared_pure_typed_arguments_accepted(
    const CettaPreparedPureProgram *program, uint32_t head_index,
    Atom *const *arguments, uint32_t arity) {
    const PreparedPureHead *head = &program->heads[head_index];
    if (!head->checks_arguments)
        return true;
    if (arity != head->typed_arity)
        return false;
    const uint8_t *classes = &program->value_classes[head->first_value_class];
    for (uint32_t i = 0u; i < arity; i++) {
        if (classes[i] != PREPARED_PURE_VALUE_UNDEFINED &&
            prepared_pure_value_class(arguments[i]) != classes[i])
            return false;
    }
    return true;
}

/* What compilation knows of a node's value class: a literal's, an integer
 * or boolean register's, a typed call's range, a branch both of whose arms
 * agree, a let body's, a typed parameter's slot (slot_classes, slot_count
 * entries); UNKNOWN otherwise. */
static uint8_t prepared_pure_node_value_class(
    const CettaPreparedPureProgram *program, uint32_t node_index,
    const uint8_t *slot_classes, uint32_t slot_count, uint32_t depth) {
    if (node_index >= program->node_len ||
        depth > PREPARED_PURE_MAX_COMPILE_DEPTH)
        return PREPARED_PURE_VALUE_UNKNOWN;
    const PreparedPureNode *node = &program->nodes[node_index];
    const uint32_t *children = node->first_child <= program->child_len &&
            node->child_count <= program->child_len - node->first_child
        ? &program->children[node->first_child] : NULL;
    switch (node->kind) {
    case PREPARED_PURE_LITERAL:
        return prepared_pure_value_class(node->atom);
    case PREPARED_PURE_REGISTER:
        return node->result_kind == CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER
            ? PREPARED_PURE_VALUE_NUMBER
            : node->result_kind == CETTA_GSLT_REGISTER_RESULT_BOOLEAN
                ? PREPARED_PURE_VALUE_BOOL : PREPARED_PURE_VALUE_UNKNOWN;
    case PREPARED_PURE_CALL:
        if (node->auxiliary < program->head_len &&
            program->heads[node->auxiliary].typed &&
            program->heads[node->auxiliary].range_class !=
                PREPARED_PURE_VALUE_UNDEFINED)
            return program->heads[node->auxiliary].range_class;
        return PREPARED_PURE_VALUE_UNKNOWN;
    case PREPARED_PURE_IF: {
        if (!children || node->child_count != 3u)
            return PREPARED_PURE_VALUE_UNKNOWN;
        uint8_t then_class = prepared_pure_node_value_class(
            program, children[1], slot_classes, slot_count, depth + 1u);
        return then_class == prepared_pure_node_value_class(
                   program, children[2], slot_classes, slot_count,
                   depth + 1u)
            ? then_class : PREPARED_PURE_VALUE_UNKNOWN;
    }
    case PREPARED_PURE_BIND:
        return children && node->child_count == 2u
            ? prepared_pure_node_value_class(
                  program, children[1], slot_classes, slot_count, depth + 1u)
            : PREPARED_PURE_VALUE_UNKNOWN;
    case PREPARED_PURE_SLOT:
    case PREPARED_PURE_EVAL_SLOT:
        return slot_classes && node->auxiliary < slot_count
            ? slot_classes[node->auxiliary] : PREPARED_PURE_VALUE_UNKNOWN;
    default:
        return PREPARED_PURE_VALUE_UNKNOWN;
    }
}

static bool prepared_pure_head_index_admitted(
    CettaPreparedPureProgram *program, SymbolId head,
    uint32_t *index_out) {
    if (!program || head == SYMBOL_ID_NONE || !index_out)
        return false;
    if (program->source_view.head_admitted &&
        !program->source_view.head_admitted(
            program->source_view.context, program->space, head))
        return prepared_pure_reject(
            program, "rule policy is outside the pure machine fragment", NULL);
    if (prepared_pure_head_bucket_lookup(program, head, index_out))
        return true;
    if (program->head_len >= PREPARED_PURE_MAX_HEADS ||
        program->head_len >= UINT32_MAX ||
        !prepared_pure_reserve(
            (void **)&program->heads, sizeof(*program->heads),
            &program->head_cap, program->head_len + 1u) ||
        !prepared_pure_head_buckets_rebuild(
            program, program->head_len + 1u))
        return false;
    if (!space_program_token_is_current(program->source_program))
        return false;
    *index_out = (uint32_t)program->head_len;
    program->heads[program->head_len++] = (PreparedPureHead){
        .head = head,
    };
    return prepared_pure_head_bucket_insert(program, *index_out);
}

static bool prepared_pure_head_index(
    CettaPreparedPureProgram *program, SymbolId head,
    uint32_t *index_out) {
    if (!program || head == SYMBOL_ID_NONE || !index_out)
        return false;
    if (prepared_pure_head_bucket_lookup(program, head, index_out))
        return true;
    /* A call whose head a variable supplies is admitted when it runs, by
     * the head it then has, so it leaves the definition pure here. */
    bool defined = false;
    bool dynamic = false;
    if (space_query_static_effect_for_head(
            program->space, head, &defined, &dynamic) !=
            CETTA_GSLT_QUERY_EFFECT_PURE ||
        !defined ||
        !space_program_token_is_current(program->source_program)) {
        return prepared_pure_reject(
            program, "user head is not revision-pinned pure", NULL);
    }
    return prepared_pure_head_index_admitted(
        program, head, index_out);
}

static bool prepared_pure_compile_template(
    CettaPreparedPureProgram *program,
    PreparedPureCompileContext *context,
    Atom *source, const void *source_view,
    uint32_t depth, bool require_inert_head,
    uint32_t *node_out);

static bool prepared_pure_compile_eval(
    CettaPreparedPureProgram *program,
    PreparedPureCompileContext *context,
    Atom *source, const void *source_view,
    uint32_t depth, uint32_t *node_out);

static CettaComputationPlan prepared_pure_source_computation(
        const CettaPreparedPureProgram *program, const void *source_view) {
    if (!source_view || !program->source_view.computation)
        return (CettaComputationPlan){0};
    return program->source_view.computation(
        program->source_view.context, source_view);
}

static CettaPreparedPureSourceRole prepared_pure_source_role(
        const CettaPreparedPureProgram *program,
        const void *source_view) {
    if (!program || !source_view)
        return CETTA_PREPARED_PURE_SOURCE_UNSPECIFIED;
    CettaPreparedPureSourceRole role = program->source_view.role
        ? program->source_view.role(program->source_view.context, source_view)
        : CETTA_PREPARED_PURE_SOURCE_UNSPECIFIED;
    if (role == CETTA_PREPARED_PURE_SOURCE_DECLINE ||
        role == CETTA_PREPARED_PURE_SOURCE_DYNAMIC_CALL)
        return role;
    CettaControlOperation operation = prepared_pure_source_computation(
        program, source_view).control.operation;
    /* A word can retain a completed result of a source call. It cannot
     * grant call authority to a value/data occurrence. */
    if ((role == CETTA_PREPARED_PURE_SOURCE_VALUE &&
         operation != CETTA_CONTROL_NONE && operation != CETTA_CONTROL_RETURN_VALUE) ||
        (role == CETTA_PREPARED_PURE_SOURCE_DATA &&
         operation != CETTA_CONTROL_NONE && operation != CETTA_CONTROL_RETURN_VALUE &&
         operation != CETTA_CONTROL_CONSTRUCT))
        return CETTA_PREPARED_PURE_SOURCE_DECLINE;
    if (operation == CETTA_CONTROL_RETURN_VALUE)
        return CETTA_PREPARED_PURE_SOURCE_VALUE;
    if (operation == CETTA_CONTROL_CONSTRUCT)
        return CETTA_PREPARED_PURE_SOURCE_DATA;
    return role;
}

static const void *prepared_pure_source_child(
        const CettaPreparedPureProgram *program,
        const void *source_view, CettaExprIndex child_index) {
    if (!program || !source_view || !program->source_view.child)
        return NULL;
    return program->source_view.child(
        program->source_view.context, source_view, child_index);
}


static CettaPreparedPureExpressionViewState prepared_pure_describe_expression(
        const CettaPreparedPureProgram *program, const Atom *source,
        const void *source_view, CettaPreparedPureExpressionView *view) {
    CettaComputationPlan computation =
        prepared_pure_source_computation(program, source_view);
    return program->expression_view(source,
        computation.control.operation == CETTA_CONTROL_NONE ? NULL : &computation,
        view);
}

/* Stored operation metadata precedes physical spelling. Fresh syntax has
 * no retained word and elaborates through the generated common vocabulary. */
static CettaControlOperationPlan prepared_pure_control_plan(
        const CettaPreparedPureProgram *program, Atom *source,
        const void *source_view) {
    CettaComputationPlan computation =
        prepared_pure_source_computation(program, source_view);
    if (computation.control.operation != CETTA_CONTROL_NONE)
        return computation.control;
    CettaControlOperationPlan control = {0};
    if (source && source->kind == ATOM_EXPR && source->expr.len &&
        source->expr.elems[0] && source->expr.elems[0]->kind == ATOM_SYMBOL)
        cetta_control_operation_decode(source->expr.elems[0]->sym_id,
            source->expr.len - 1u, &control, &g_builtin_syms);
    return control;
}

/* These lowerings consume three independent source places. Checking the
 * coordinates before reading also rejects absent and malformed operands. */
static bool prepared_pure_control_three_places(
        CettaControlOperationPlan plan, CettaExprLen arity) {
    return arity == 3u && plan.source < arity && plan.first < arity &&
        plan.last < arity && plan.source != plan.first &&
        plan.source != plan.last && plan.first != plan.last;
}

static const void *prepared_pure_projected_source_child(
        const CettaPreparedPureProgram *program,
        Atom *source, const void *source_view, Atom *projected) {
    if (!source || !source_view || !projected ||
        source->kind != ATOM_EXPR)
        return NULL;
    for (CettaExprIndex index = 0u;
         index < source->expr.len; index++) {
        if (source->expr.elems[index] == projected)
            return prepared_pure_source_child(
                program, source_view, index);
    }
    return NULL;
}

typedef enum {
    PREPARED_PURE_GUARDED_EQUATION_ERROR = -1,
    PREPARED_PURE_GUARDED_EQUATION_NOT_APPLICABLE = 0,
    PREPARED_PURE_GUARDED_EQUATION_READY = 1,
} PreparedPureGuardedEquationState;

static bool prepared_pure_expression_is_zero(
        CettaPreparedPureProgram *program, Atom *source,
        const void *source_view) {
    if (!program || !source)
        return false;
    CettaPreparedPureSourceRole source_role =
        prepared_pure_source_role(program, source_view);
    if (source_role != CETTA_PREPARED_PURE_SOURCE_UNSPECIFIED &&
        source_role != CETTA_PREPARED_PURE_SOURCE_CALL)
        return false;
    CettaComputationPlan computation =
        prepared_pure_source_computation(program, source_view);
    if (computation.control.operation != CETTA_CONTROL_NONE)
        return computation.control.operation == CETTA_CONTROL_FAILURE;
    if (!program->expression_view)
        return false;
    if (source_role == CETTA_PREPARED_PURE_SOURCE_UNSPECIFIED &&
        source->kind == ATOM_EXPR && source->expr.len > 0u &&
        source->expr.elems[0] &&
        source->expr.elems[0]->kind == ATOM_SYMBOL &&
        space_equations_may_match_known_head(
            program->space, source->expr.elems[0]->sym_id))
        return false;
    CettaPreparedPureExpressionView view = {0};
    return program->expression_view(source, NULL, &view) ==
        CETTA_PREPARED_PURE_EXPRESSION_ZERO;
}

bool cetta_prepared_pure_single_result_requires_answer_effect(
        const Atom *expression,
        CettaPreparedPureExpressionViewFn expression_view) {
    if (!expression || !expression_view ||
        expression->kind != ATOM_EXPR || expression->expr.len != 4u ||
        !expression->expr.elems[0] ||
        expression->expr.elems[0]->kind != ATOM_SYMBOL)
        return false;

    CettaGsltFoldControl control;
    if (!cetta_gslt_fold_control_program(
            expression->expr.elems[0]->sym_id, 3u, &control, &g_builtin_syms) ||
        control != CETTA_GSLT_FOLD_CONTROL_BRANCH)
        return false;

    CettaPreparedPureExpressionView view = {0};
    if (expression_view(expression->expr.elems[2], NULL, &view) ==
            CETTA_PREPARED_PURE_EXPRESSION_ZERO)
        return true;
    memset(&view, 0, sizeof(view));
    return expression_view(expression->expr.elems[3], NULL, &view) ==
        CETTA_PREPARED_PURE_EXPRESSION_ZERO;
}

static bool prepared_pure_scalar_guard_flat_lhs(Atom *lhs) {
    if (!lhs || lhs->kind != ATOM_EXPR || lhs->expr.len == 0u)
        return false;
    for (CettaExprIndex index = 1u; index < lhs->expr.len; index++) {
        Atom *pattern = lhs->expr.elems[index];
        if (!pattern ||
            (pattern->kind == ATOM_EXPR && pattern->expr.len != 0u))
            return false;
    }
    return true;
}

/* numeric-eq is a test only where the dialect defines it: PeTTa and Hyperon,
 * which he-compat follows, keep it data, as grounded dispatch does. */
static bool prepared_pure_numeric_eq_defined(void) {
    CettaLanguageId language = eval_current_language_id
        ? eval_current_language_id() : CETTA_LANGUAGE_HE;
    return language != CETTA_LANGUAGE_PETTA &&
        !eval_current_uses_rust_he_compat_semantics();
}

static bool prepared_pure_plain_scalar_truth_head(SymbolId head) {
    return head == g_builtin_syms.op_lt ||
           head == g_builtin_syms.op_gt ||
           head == g_builtin_syms.op_le ||
           head == g_builtin_syms.op_ge ||
           (head == g_builtin_syms.numeric_eq &&
            prepared_pure_numeric_eq_defined());
}

static bool prepared_pure_compile_scalar_guard(
        CettaPreparedPureProgram *program,
        const PreparedPureCompileContext *context,
        Atom *condition, bool expected_truth,
        uint32_t *guard_out) {
    if (!program || !context || !condition || !guard_out ||
        condition->kind != ATOM_EXPR || condition->expr.len == 0u ||
        condition->expr.len - 1u >
            PREPARED_PURE_MAX_SCALAR_GUARD_ARGUMENTS)
        return false;
    Atom *head = condition->expr.elems[0];
    if (!head || head->kind != ATOM_SYMBOL ||
        !prepared_pure_plain_scalar_truth_head(head->sym_id) ||
        !grounded_op_is_type_pure(head->sym_id) ||
        program->scalar_guard_len >= UINT32_MAX ||
        program->scalar_guard_argument_len >= UINT32_MAX)
        return false;

    size_t argument_mark = program->scalar_guard_argument_len;
    CettaExprLen argument_count = condition->expr.len - 1u;
    if (!prepared_pure_reserve(
            (void **)&program->scalar_guard_arguments,
            sizeof(*program->scalar_guard_arguments),
            &program->scalar_guard_argument_cap,
            argument_mark + argument_count) ||
        !prepared_pure_reserve(
            (void **)&program->scalar_guards,
            sizeof(*program->scalar_guards),
            &program->scalar_guard_cap,
            program->scalar_guard_len + 1u))
        return false;

    for (CettaExprIndex index = 1u;
         index < condition->expr.len; index++) {
        Atom *argument = condition->expr.elems[index];
        PreparedPureScalarGuardArgument compiled = {0};
        if (argument && argument->kind == ATOM_VAR) {
            uint32_t slot = 0u;
            if (!prepared_pure_context_lookup(
                    context, argument->var_id, &slot)) {
                program->scalar_guard_argument_len = argument_mark;
                return false;
            }
            compiled.slot = slot;
            compiled.from_slot = true;
        } else if (argument && argument->kind == ATOM_GROUNDED &&
                   (argument->ground.gkind == GV_INT ||
                    argument->ground.gkind == GV_FLOAT)) {
            compiled.literal = argument;
        } else {
            program->scalar_guard_argument_len = argument_mark;
            return false;
        }
        program->scalar_guard_arguments[
            program->scalar_guard_argument_len++] = compiled;
    }

    uint32_t guard_index = (uint32_t)program->scalar_guard_len;
    program->scalar_guards[program->scalar_guard_len++] =
        (PreparedPureScalarGuard){
            .head = head,
            .first_argument = (uint32_t)argument_mark,
            .argument_count = argument_count,
            .expected_truth = expected_truth,
        };
    *guard_out = guard_index;
    return true;
}

static PreparedPureGuardedEquationState
prepared_pure_compile_guarded_equation(
        CettaPreparedPureProgram *program,
        PreparedPureCompileContext *context,
        Atom *lhs, Atom *rhs, const void *rhs_view,
        uint32_t depth,
        uint32_t *root_out, uint32_t *guard_out) {
    if (!program || !context || !lhs || !rhs || !root_out || !guard_out)
        return PREPARED_PURE_GUARDED_EQUATION_ERROR;
    CettaPreparedPureSourceRole rhs_role =
        prepared_pure_source_role(program, rhs_view);
    if ((rhs_role != CETTA_PREPARED_PURE_SOURCE_UNSPECIFIED &&
         rhs_role != CETTA_PREPARED_PURE_SOURCE_CALL) ||
        !prepared_pure_scalar_guard_enabled() ||
        !prepared_pure_scalar_guard_flat_lhs(lhs) ||
        rhs->kind != ATOM_EXPR || rhs->expr.len != 4u ||
        !rhs->expr.elems[0] ||
        rhs->expr.elems[0]->kind != ATOM_SYMBOL)
        return PREPARED_PURE_GUARDED_EQUATION_NOT_APPLICABLE;

    CettaControlOperationPlan control =
        prepared_pure_control_plan(program, rhs, rhs_view);
    if (control.operation != CETTA_CONTROL_BRANCH ||
        !prepared_pure_control_three_places(control, 3u))
        return PREPARED_PURE_GUARDED_EQUATION_NOT_APPLICABLE;
    CettaExprIndex condition = (CettaExprIndex)control.source + 1u;
    CettaExprIndex when_true = (CettaExprIndex)control.first + 1u;
    CettaExprIndex when_false = (CettaExprIndex)control.last + 1u;
    CettaComputationPlan condition_plan = prepared_pure_source_computation(
        program, prepared_pure_source_child(program, rhs_view, condition));
    if (condition_plan.control.operation != CETTA_CONTROL_NONE &&
        condition_plan.control.operation != CETTA_CONTROL_INVOKE)
        return PREPARED_PURE_GUARDED_EQUATION_NOT_APPLICABLE;
    bool then_zero = prepared_pure_expression_is_zero(
        program, rhs->expr.elems[when_true],
        prepared_pure_source_child(program, rhs_view, when_true));
    bool else_zero = prepared_pure_expression_is_zero(
        program, rhs->expr.elems[when_false],
        prepared_pure_source_child(program, rhs_view, when_false));
    if (then_zero == else_zero)
        return PREPARED_PURE_GUARDED_EQUATION_NOT_APPLICABLE;

    size_t guard_mark = program->scalar_guard_len;
    size_t argument_mark = program->scalar_guard_argument_len;
    uint32_t guard = PREPARED_PURE_NO_SCALAR_GUARD;
    bool expected_truth = else_zero;
    if (!prepared_pure_compile_scalar_guard(
            program, context, rhs->expr.elems[condition],
            expected_truth, &guard)) {
        program->scalar_guard_len = guard_mark;
        program->scalar_guard_argument_len = argument_mark;
        return PREPARED_PURE_GUARDED_EQUATION_NOT_APPLICABLE;
    }
    CettaExprIndex result = then_zero ? when_false : when_true;
    Atom *result_branch = rhs->expr.elems[result];
    const void *result_view = prepared_pure_source_child(
        program, rhs_view, result);
    if (!prepared_pure_compile_eval(
            program, context, result_branch, result_view,
            depth + 1u, root_out)) {
        program->scalar_guard_len = guard_mark;
        program->scalar_guard_argument_len = argument_mark;
        return PREPARED_PURE_GUARDED_EQUATION_ERROR;
    }
    *guard_out = guard;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PREPARED_PURE_SCALAR_GUARD_ADMITTED);
    return PREPARED_PURE_GUARDED_EQUATION_READY;
}

static bool prepared_pure_compile_children(
    CettaPreparedPureProgram *program,
    PreparedPureCompileContext *context,
    Atom **source, CettaExprLen count,
    const void *parent_source_view,
    CettaExprIndex first_source_child, uint32_t depth,
    bool evaluate, bool require_inert_templates,
    uint32_t **children_out) {
    if (!children_out ||
        (count > 0u && (size_t)count > SIZE_MAX / sizeof(uint32_t)))
        return false;
    uint32_t *children = count ? malloc(sizeof(*children) * count) : NULL;
    if (count && !children)
        return false;
    for (CettaExprIndex i = 0u; i < count; i++) {
        const void *child_view = prepared_pure_source_child(
            program, parent_source_view, first_source_child + i);
        bool ok = evaluate
            ? prepared_pure_compile_eval(
                  program, context, source[i], child_view,
                  depth + 1u, &children[i])
            : prepared_pure_compile_template(
                  program, context, source[i], child_view, depth + 1u,
                  require_inert_templates, &children[i]);
        if (!ok) {
            free(children);
            return prepared_pure_reject(
                program,
                evaluate
                    ? "evaluated child is outside the pure machine fragment"
                    : "delayed child is outside the pure machine fragment",
                source[i]);
        }
    }
    *children_out = children;
    return true;
}

typedef enum {
    PREPARED_PURE_HEAD_INERT = 0,
    PREPARED_PURE_HEAD_CALLABLE = 1,
    PREPARED_PURE_HEAD_UNKNOWN = 2,
} PreparedPureHeadRole;

/*
 * One conservative capability judgment serves compilation, inert-template
 * validation, and run-time suspension handling.  UNKNOWN means that a
 * dialect owns the occurrence but this accelerator cannot implement it;
 * UNKNOWN therefore never licenses an inert-data optimization.
 */
static PreparedPureHeadRole prepared_pure_head_role(
    CettaPreparedPureProgram *program, Atom *source) {
    if (!program || !source)
        return PREPARED_PURE_HEAD_UNKNOWN;
    if (source->kind != ATOM_EXPR || source->expr.len == 0u)
        return PREPARED_PURE_HEAD_INERT;
    Atom *head = source->expr.elems[0];
    if (!head || head->kind != ATOM_SYMBOL)
        return PREPARED_PURE_HEAD_UNKNOWN;

    if (program->expression_view) {
        CettaPreparedPureExpressionView view = {0};
        CettaPreparedPureExpressionViewState state =
            program->expression_view(source, NULL, &view);
        if (state == CETTA_PREPARED_PURE_EXPRESSION_DATA)
            return PREPARED_PURE_HEAD_INERT;
        if (state == CETTA_PREPARED_PURE_EXPRESSION_DECLINE ||
            state == CETTA_PREPARED_PURE_EXPRESSION_CANONICAL_ONLY ||
            state == CETTA_PREPARED_PURE_EXPRESSION_ZERO)
            return PREPARED_PURE_HEAD_UNKNOWN;
        if (state == CETTA_PREPARED_PURE_EXPRESSION_PROJECT ||
            state == CETTA_PREPARED_PURE_EXPRESSION_PROJECT_SINGLE_RESULT ||
            state == CETTA_PREPARED_PURE_EXPRESSION_OBSERVE ||
            state == CETTA_PREPARED_PURE_EXPRESSION_SELECT)
            return PREPARED_PURE_HEAD_CALLABLE;
        if (state != CETTA_PREPARED_PURE_EXPRESSION_DEFAULT)
            return PREPARED_PURE_HEAD_UNKNOWN;
    }

    CettaExprLen arity = source->expr.len - 1u;
    if (cetta_gslt_fold_control_program(head->sym_id, arity, NULL, &g_builtin_syms) ||
        prepared_pure_register_program(program,
            head->sym_id, arity, NULL, NULL) ||
        cetta_prepared_pure_intrinsic_program(
            head->sym_id, arity, NULL) ||
        is_grounded_op(head->sym_id) ||
        prepared_pure_builtin_syntax(head->sym_id)) {
        return PREPARED_PURE_HEAD_CALLABLE;
    }
    bool defined = false;
    (void)space_query_effect_for_head(
        program->space, head->sym_id, &defined);
    return defined || space_equations_may_match_known_head(
                          program->space, head->sym_id)
        ? PREPARED_PURE_HEAD_CALLABLE
        : PREPARED_PURE_HEAD_INERT;
}

/* A template is data, not a suspended evaluator call.  Reject every outer
 * head not proved inert so the machine never changes an argument-evaluation
 * contract by treating active or unknown syntax as data. */
static bool prepared_pure_template_head_is_inert(
    CettaPreparedPureProgram *program, Atom *source) {
    return prepared_pure_head_role(program, source) ==
           PREPARED_PURE_HEAD_INERT;
}

/* Whether `atom` is a list the evaluator materialized: open list cells of
 * scalar values, ending in `()`.  Nothing in it computes, and its length is
 * not a depth of program syntax. */
static bool prepared_pure_materialized_scalar_list(const Atom *atom) {
    if (!petta_semantics_is_open_cons_value(atom))
        return false;
    while (petta_semantics_is_open_cons_value(atom)) {
        const Atom *item = atom->expr.elems[1];
        if (!item || item->kind == ATOM_EXPR || item->kind == ATOM_VAR)
            return false;
        atom = atom->expr.elems[2];
    }
    return atom && atom->kind == ATOM_EXPR && atom->expr.len == 0u;
}

static bool prepared_pure_compile_template_in_source(
    CettaPreparedPureProgram *program,
    PreparedPureCompileContext *context,
    Atom *source, const void *source_view,
    uint32_t depth, bool require_inert_head,
    uint32_t *node_out);

static bool prepared_pure_compile_template(
    CettaPreparedPureProgram *program,
    PreparedPureCompileContext *context,
    Atom *source, const void *source_view,
    uint32_t depth, bool require_inert_head,
    uint32_t *node_out) {
    if (!program)
        return false;
    if (!program->answer_producer || !program->source_view.project_source)
        return prepared_pure_compile_template_in_source(program, context, source, source_view, depth, require_inert_head, node_out);
    const void *saved = program->compiling_source;
    program->compiling_source = source_view;
    bool ok = prepared_pure_compile_template_in_source(program, context, source, source_view, depth, require_inert_head, node_out);
    program->compiling_source = saved;
    return ok;
}

static bool prepared_pure_compile_template_in_source(
    CettaPreparedPureProgram *program,
    PreparedPureCompileContext *context,
    Atom *source, const void *source_view,
    uint32_t depth, bool require_inert_head,
    uint32_t *node_out) {
    if (!program || !context || !source || !node_out ||
        depth > PREPARED_PURE_MAX_COMPILE_DEPTH)
        return false;
    if (prepared_pure_materialized_scalar_list(source)) {
        return prepared_pure_add_node(
            program,
            (PreparedPureNode){
                .kind = PREPARED_PURE_LITERAL,
                .atom = source,
            },
            NULL, 0u, node_out);
    }
    if (source->kind == ATOM_VAR) {
        uint32_t slot = 0u;
        if (!prepared_pure_context_lookup(context, source->var_id, &slot))
            return false;
        return prepared_pure_add_node(
            program,
            (PreparedPureNode){
                .kind = PREPARED_PURE_SLOT,
                .auxiliary = slot,
            },
            NULL, 0u, node_out);
    }
    if (source->kind != ATOM_EXPR) {
        return prepared_pure_add_node(
            program,
            (PreparedPureNode){
                .kind = PREPARED_PURE_LITERAL,
                .atom = source,
            },
            NULL, 0u, node_out);
    }
    CettaPreparedPureSourceRole source_role =
        prepared_pure_source_role(program, source_view);
    if (source_role == CETTA_PREPARED_PURE_SOURCE_DYNAMIC_CALL ||
        source_role == CETTA_PREPARED_PURE_SOURCE_DECLINE)
        return prepared_pure_reject(
            program, "source view declines prepared template", source);
    /* normalize-before-delay: a total register or control head inside
     * constructor payload is computation, not quoted data.  The canonical
     * evaluator computes these positions, so the machine must as well. */
    if ((source_role == CETTA_PREPARED_PURE_SOURCE_UNSPECIFIED ||
         source_role == CETTA_PREPARED_PURE_SOURCE_CALL) &&
        source->expr.len > 0u) {
        Atom *payload_head = source->expr.elems[0];
        if (payload_head && payload_head->kind == ATOM_SYMBOL) {
            CettaExprLen payload_arity = source->expr.len - 1u;
            if (prepared_pure_register_program(program,
                    payload_head->sym_id, payload_arity, NULL, NULL) ||
                cetta_prepared_pure_intrinsic_program(
                    payload_head->sym_id, payload_arity, NULL) ||
                cetta_control_operation_is_control_form(
                    prepared_pure_control_plan(program, source, source_view))) {
                return prepared_pure_compile_eval(
                    program, context, source, source_view,
                    depth, node_out);
            }
        }
    }
    bool source_head_is_inert =
        source_role == CETTA_PREPARED_PURE_SOURCE_VALUE ||
        source_role == CETTA_PREPARED_PURE_SOURCE_DATA ||
        (source_role == CETTA_PREPARED_PURE_SOURCE_UNSPECIFIED &&
         prepared_pure_template_head_is_inert(program, source));
    if (require_inert_head && !source_head_is_inert)
        return prepared_pure_reject(
            program, "callable syntax occurs in a data template", source);
    uint32_t *children = NULL;
    if (!prepared_pure_compile_children(
            program, context, source->expr.elems, source->expr.len,
            source_view, 0u, depth, false, false, &children))
        return false;
    bool ok = prepared_pure_add_node(
        program, (PreparedPureNode){.kind = PREPARED_PURE_BUILD},
        children, source->expr.len, node_out);
    free(children);
    return ok;
}

/* The operands of a register over typed structural operands that are
 * literal symbols typed %Undefined% by the program's declarations. */
static uint8_t prepared_pure_undefined_type_operands(
    const CettaPreparedPureProgram *program,
    CettaGsltRegisterInstruction instruction,
    const uint32_t *children, uint32_t arity) {
    CettaGsltRegisterOperandDiscipline discipline;
    if (!program || !children || arity != 2u ||
        program->total_structural_equality ||
        !cetta_gslt_register_operand_discipline(instruction, &discipline) ||
        discipline !=
            CETTA_GSLT_REGISTER_OPERANDS_TYPED_STRUCTURAL_OPERANDS)
        return 0u;
    uint8_t operands = 0u;
    for (uint32_t i = 0u; i < arity; i++) {
        const PreparedPureNode *child = &program->nodes[children[i]];
        if (child->kind == PREPARED_PURE_LITERAL &&
            eval_symbol_type_undefined(program->space, child->atom))
            operands |= (uint8_t)(1u << i);
    }
    return operands;
}

/* Whether no part of a pattern computes: the dialect's plan calls none of
 * its subterms, and where the dialect plans its source, no subterm the plan
 * leaves unspecified has a head not proved inert.  PeTTa evaluates a
 * pattern's function-headed subterms, at any depth, before it matches. */
static bool prepared_pure_bind_pattern_is_inert(
        CettaPreparedPureProgram *program, Atom *pattern,
        const void *source_view, uint32_t depth) {
    CettaPreparedPureSourceRole role =
        prepared_pure_source_role(program, source_view);
    if (role == CETTA_PREPARED_PURE_SOURCE_CALL ||
        role == CETTA_PREPARED_PURE_SOURCE_DYNAMIC_CALL ||
        role == CETTA_PREPARED_PURE_SOURCE_DECLINE)
        return false;
    if (pattern->kind != ATOM_EXPR)
        return true;
    if (depth > PREPARED_PURE_MAX_COMPILE_DEPTH ||
        (role == CETTA_PREPARED_PURE_SOURCE_UNSPECIFIED &&
         program->source_view.role &&
         !prepared_pure_template_head_is_inert(program, pattern)))
        return false;
    for (CettaExprIndex index = 0u; index < pattern->expr.len; index++) {
        Atom *child = pattern->expr.elems[index];
        /* A head that is no expression is judged with its expression. */
        if (index == 0u && child->kind != ATOM_EXPR)
            continue;
        if (!prepared_pure_bind_pattern_is_inert(
                program, child,
                prepared_pure_source_child(program, source_view, index),
                depth + 1u))
            return false;
    }
    return true;
}

/* A structural bind consumes a pattern value. A source occurrence that
 * computes its pattern, or any part of it, must stay with the owning
 * evaluator; treating its syntax as a template would erase bindings
 * supplied by a consumer. */
static bool prepared_pure_compile_inert_bind_pattern(
        CettaPreparedPureProgram *program,
        PreparedPureCompileContext *context, Atom *pattern,
        const void *source_view, uint32_t *pattern_index_out) {
    if (!prepared_pure_bind_pattern_is_inert(program, pattern, source_view,
                                             0u))
        return prepared_pure_reject(
            program, "binding pattern requires source evaluation", pattern);
    if (!prepared_pure_compile_bind_pattern(
            program, context, pattern, pattern_index_out))
        return false;
    /* A bound variable in a Need pattern consumes its cell's value. The
     * structural matcher only compares slots; it cannot demand that cell. */
    if (program->call_mode == CETTA_GSLT_PURE_CALL_CALL_BY_NEED) {
        const PreparedPureBindPattern *bound =
            &program->bind_patterns[*pattern_index_out];
        for (uint32_t i = 0u; i < bound->var_count; i++)
            if (program->bind_vars[bound->first_var + i].prebound)
                return prepared_pure_reject(
                    program, "binding pattern demands a bound Need value", pattern);
    }
    return true;
}

static bool prepared_pure_value_tree(
    const CettaPreparedPureProgram *program, uint32_t node_index, uint32_t depth);

/* Closed finite value work cannot inspect a logical variable that an early
 * destination match could refine. Slot-dependent computations, calls,
 * interpreted code and natives require their own evidence. */
static bool prepared_pure_finite_value_work(
        const CettaPreparedPureProgram *program, uint32_t index, uint32_t depth) {
    if (index >= program->node_len || depth > PREPARED_PURE_MAX_COMPILE_DEPTH)
        return false;
    const PreparedPureNode *node = &program->nodes[index];
    switch (node->kind) {
    case PREPARED_PURE_LITERAL:
        return node->atom && !atom_has_vars(node->atom);
    case PREPARED_PURE_ZERO:
        return true;
    case PREPARED_PURE_BUILD:
    case PREPARED_PURE_OBSERVE:
    case PREPARED_PURE_REGISTER:
    case PREPARED_PURE_BIND:
    case PREPARED_PURE_IF:
        break;
    default:
        return false;
    }
    if (node->first_child > program->child_len ||
        node->child_count > program->child_len - node->first_child)
        return false;
    for (uint32_t child = 0u; child < node->child_count; child++)
        if (!prepared_pure_finite_value_work(program,
                program->children[node->first_child + child], depth + 1u))
            return false;
    return true;
}

static bool prepared_pure_pattern_repeats_variable(const Atom *pattern);

/* Source binds carry the dialect's call-result protocol. A fresh receiver
 * imposes no constraint on its producer. A value tree interprets nothing;
 * closed finite work may complete before its inert matcher. Another
 * demanded producer computes first only with destination evidence, read
 * once its callees compile (prepared_pure_binds_meet_destinations); a
 * pattern that repeats a variable relates its parts and retains ordinary
 * entry. */
static bool prepared_pure_compile_source_bind(CettaPreparedPureProgram *program,
        uint32_t pattern_index, uint32_t producer, uint32_t body,
        uint32_t *out) {
    const PreparedPureBindPattern *pattern = &program->bind_patterns[pattern_index];
    bool fresh = pattern->pattern->kind == ATOM_VAR && pattern->var_count == 1u &&
        !program->bind_vars[pattern->first_var].prebound;
    CettaCallResultPolicy policy = program->source_view.result_policy;
    if (policy == CETTA_CALL_RESULT_BY_DESTINATION && !fresh &&
        !prepared_pure_value_tree(program, producer, 0u) &&
        !prepared_pure_finite_value_work(program, producer, 0u)) {
        if (prepared_pure_pattern_repeats_variable(pattern->pattern))
            return prepared_pure_reject(program,
                "producer needs its caller's result destination",
                pattern->pattern);
        program->bind_patterns[pattern_index].destination_evidence = true;
    }
    uint32_t children[2] = {producer, body};
    return prepared_pure_add_node(program, (PreparedPureNode){
        .kind = PREPARED_PURE_BIND, .auxiliary = pattern_index},
        children, 2u, out);
}

/* A bind sequence (let*) from pair `index` on: the pair's value, then its
 * pattern bound for the rest, as nested binds ending in the body. */
static bool prepared_pure_compile_bind_sequence(
        CettaPreparedPureProgram *program,
        PreparedPureCompileContext *context,
        Atom *pairs, const void *pairs_view, CettaExprIndex index,
        Atom *body, const void *body_view,
        uint32_t depth, uint32_t *node_out) {
    if (index == pairs->expr.len)
        return prepared_pure_compile_eval(
            program, context, body, body_view, depth + 1u, node_out);
    Atom *pair = pairs->expr.elems[index];
    if (!pair || pair->kind != ATOM_EXPR || pair->expr.len != 2u ||
        !pair->expr.elems[0])
        return prepared_pure_reject(
            program, "let* pair is outside the fragment", pair);
    const void *pair_view =
        prepared_pure_source_child(program, pairs_view, index);
    uint32_t bound = 0u;
    if (!prepared_pure_compile_eval(
            program, context, pair->expr.elems[1],
            prepared_pure_source_child(program, pair_view, 1u),
            depth + 1u, &bound))
        return prepared_pure_reject(
            program, "let* bound expression is outside the fragment",
            pair->expr.elems[1]);
    size_t saved_bindings = context->len;
    uint32_t pattern_index = 0u;
    if (!prepared_pure_compile_inert_bind_pattern(
            program, context, pair->expr.elems[0],
            prepared_pure_source_child(program, pair_view, 0u),
            &pattern_index))
        return prepared_pure_reject(
            program, "let* pattern is outside the fragment",
            pair->expr.elems[0]);
    uint32_t rest = 0u;
    bool rest_ok = prepared_pure_compile_bind_sequence(
        program, context, pairs, pairs_view, index + 1u,
        body, body_view, depth, &rest);
    context->len = saved_bindings;
    if (!rest_ok)
        return false;
    return prepared_pure_compile_source_bind(
        program, pattern_index, bound, rest, node_out);
}

static bool prepared_pure_expansion_var(
    const CettaPreparedPureProgram *program, VarId id) {
    for (size_t i = program->expansion_var_len; i > 0u; i--) {
        if (var_id_eq(program->expansion_vars[i - 1u], id))
            return true;
    }
    return false;
}

static bool prepared_pure_collect_expansion_vars(
    CettaPreparedPureProgram *program, Atom *atom, uint32_t depth) {
    if (!atom || depth > PREPARED_PURE_MAX_COMPILE_DEPTH)
        return false;
    if (atom->kind == ATOM_VAR) {
        if (prepared_pure_expansion_var(program, atom->var_id))
            return true;
        if (!prepared_pure_reserve(
                (void **)&program->expansion_vars,
                sizeof(*program->expansion_vars),
                &program->expansion_var_cap,
                program->expansion_var_len + 1u))
            return false;
        program->expansion_vars[program->expansion_var_len++] =
            atom->var_id;
        return true;
    }
    if (atom->kind != ATOM_EXPR)
        return true;
    for (CettaExprIndex i = 0u; i < atom->expr.len; i++) {
        if (!prepared_pure_collect_expansion_vars(
                program, atom->expr.elems[i], depth + 1u))
            return false;
    }
    return true;
}

/* The machine's fold scope: an expansion's definition variables are
 * metavariables while it compiles, unless the compile context binds them to
 * a value. */
typedef struct {
    const CettaPreparedPureProgram *program;
    const PreparedPureCompileContext *context;
} PreparedPureFoldScope;

static bool prepared_pure_fold_metavariable(const void *scope, VarId id) {
    const PreparedPureFoldScope *fold = scope;
    return prepared_pure_expansion_var(fold->program, id) &&
        !prepared_pure_context_lookup(fold->context, id, NULL);
}

enum { PREPARED_PURE_MAX_EXPANSION_DEPTH = 64u };

static Arena *prepared_pure_syntax_arena(CettaPreparedPureProgram *program) {
    if (!program->syntax_arena) {
        program->syntax_arena = calloc(1u, sizeof(*program->syntax_arena));
        if (program->syntax_arena)
            arena_init_detached(program->syntax_arena);
    }
    return program->syntax_arena;
}

static bool prepared_pure_cache_boolean_atoms(
    CettaPreparedPureProgram *program) {
    if (!program || !program->boolean_value)
        return false;
    Arena temporary;
    arena_init_detached(&temporary);
    Atom *falsity = program->boolean_value(&temporary, false);
    Atom *truth = program->boolean_value(&temporary, true);
    /* A representation must distinguish the truths and preserve the
     * polarity of any canonical Boolean names or scalar Bool leaves. */
    bool false_truth = false, true_truth = true;
    if (!falsity || !truth || atom_eq(falsity, truth) ||
        (falsity->kind == ATOM_GROUNDED && falsity->ground.gkind == GV_BOOL &&
         falsity->ground.bval) ||
        (truth->kind == ATOM_GROUNDED && truth->ground.gkind == GV_BOOL &&
         !truth->ground.bval) ||
        (falsity->kind == ATOM_SYMBOL &&
         petta_semantics_truth_value(falsity, &false_truth) && false_truth) ||
        (truth->kind == ATOM_SYMBOL &&
         petta_semantics_truth_value(truth, &true_truth) && !true_truth)) {
        arena_free(&temporary);
        return false;
    }
    bool supported = falsity && truth &&
        ((falsity->kind == ATOM_SYMBOL && truth->kind == ATOM_SYMBOL) ||
         (falsity->kind == ATOM_GROUNDED && truth->kind == ATOM_GROUNDED &&
          falsity->ground.gkind == GV_BOOL &&
          truth->ground.gkind == GV_BOOL));
    if (supported) {
        program->boolean_atoms[0] = *falsity;
        program->boolean_atoms[1] = *truth;
        /* Program storage, like a global leaf, outlives every execution
         * arena. No child or byte payload is borrowed from `temporary`. */
        program->boolean_atoms[0].arena_id = 0u;
        program->boolean_atoms[1].arena_id = 0u;
        program->boolean_atoms_ready = true;
    }
    arena_free(&temporary);
    return true;
}

static Atom *prepared_pure_boolean_atom(
    const CettaPreparedPureProgram *program, Arena *arena, bool value) {
    if (!program || !program->boolean_value)
        return NULL;
    if (!program->boolean_atoms_ready)
        return program->boolean_value(arena, value);
    Atom *cached = (Atom *)&program->boolean_atoms[value ? 1u : 0u];
    if (!program->exports_boolean_results)
        return cached;
    /* A parent consumer may outlive this private program. Symbols have
     * canonical table-owned leaves; other Boolean spellings are made in
     * the execution arena whose results the consumer already retains. */
    return cached->kind == ATOM_SYMBOL
        ? atom_symbol_canonical(cached->sym_id)
        : program->boolean_value(arena, value);
}

/* HE's unify matches its first two operands with each other; the machine
 * matches a value against a pattern.  The value is the first operand whose
 * variables all hold values here (index 1 or 2 of the instruction), the
 * pattern the other; 0 when neither does. */
static bool prepared_pure_atom_vars_bound(
    const PreparedPureCompileContext *context, const Atom *atom,
    uint32_t depth) {
    if (atom->kind == ATOM_VAR)
        return prepared_pure_context_lookup(context, atom->var_id, NULL);
    if (atom->kind != ATOM_EXPR)
        return true;
    if (depth > PREPARED_PURE_MAX_COMPILE_DEPTH)
        return false;
    for (CettaExprIndex i = 0u; i < atom->expr.len; i++) {
        if (!prepared_pure_atom_vars_bound(
                context, atom->expr.elems[i], depth + 1u))
            return false;
    }
    return true;
}

static uint32_t prepared_pure_unify_value_side(
    const PreparedPureCompileContext *context, const Atom *instruction) {
    for (uint32_t index = 1u; index <= 2u; index++) {
        if (prepared_pure_atom_vars_bound(
                context, instruction->expr.elems[index], 0u))
            return index;
    }
    return 0u;
}

/* Whether two atoms of an expansion's syntax are equal, as if-equal compares
 * them, where the syntax decides it: a variable holds a value only a run
 * decides; atoms of different kinds or lengths differ whatever they hold. */
typedef enum {
    PREPARED_PURE_EQUAL_UNKNOWN = 0,
    PREPARED_PURE_EQUAL_SAME,
    PREPARED_PURE_EQUAL_DIFFERENT,
} PreparedPureStaticEqual;

static PreparedPureStaticEqual prepared_pure_static_equal(
    Atom *left, Atom *right, uint32_t depth) {
    if (!left || !right || depth > PREPARED_PURE_MAX_COMPILE_DEPTH ||
        left->kind == ATOM_VAR || right->kind == ATOM_VAR)
        return PREPARED_PURE_EQUAL_UNKNOWN;
    if (left->kind != right->kind)
        return PREPARED_PURE_EQUAL_DIFFERENT;
    if (left->kind != ATOM_EXPR)
        return atom_eq(left, right) ? PREPARED_PURE_EQUAL_SAME
                                    : PREPARED_PURE_EQUAL_DIFFERENT;
    if (left->expr.len != right->expr.len)
        return PREPARED_PURE_EQUAL_DIFFERENT;
    bool unknown = false;
    for (CettaExprIndex i = 0u; i < left->expr.len; i++) {
        PreparedPureStaticEqual equal = prepared_pure_static_equal(
            left->expr.elems[i], right->expr.elems[i], depth + 1u);
        if (equal == PREPARED_PURE_EQUAL_DIFFERENT)
            return PREPARED_PURE_EQUAL_DIFFERENT;
        unknown = unknown || equal == PREPARED_PURE_EQUAL_UNKNOWN;
    }
    return unknown ? PREPARED_PURE_EQUAL_UNKNOWN : PREPARED_PURE_EQUAL_SAME;
}

/* Where a function body's returned syntax goes: EVAL, the syntax evaluated,
 * or, with `var`, `template_atom` evaluated with the syntax in place of
 * `var` (the body of a callee given the syntax); CHAIN, the template of the
 * chain whose computation the function was, with the syntax in place of the
 * chain's variable, run in the continuation `outer`; SYNTAX, the syntax as
 * it is, which a call whose range is Atom answers. */
typedef enum {
    PREPARED_PURE_DELIVER_EVAL = 0,
    PREPARED_PURE_DELIVER_CHAIN,
    PREPARED_PURE_DELIVER_SYNTAX,
} PreparedPureDeliverKind;

typedef struct PreparedPureDelivery {
    PreparedPureDeliverKind kind;
    Atom *var;
    Atom *template_atom;
    const struct PreparedPureDelivery *outer;
} PreparedPureDelivery;

static bool prepared_pure_compile_function(
    CettaPreparedPureProgram *program, PreparedPureCompileContext *context,
    Atom *body, const PreparedPureDelivery *delivery, uint32_t depth,
    uint32_t *node_out);

static Atom *prepared_pure_substitute_var(
    Arena *arena, Atom *var, Atom *value, Atom *atom) {
    Bindings bindings;
    bindings_init(&bindings);
    Atom *result = bindings_add_var(&bindings, var, value)
        ? bindings_apply_if_vars(&bindings, arena, atom) : NULL;
    bindings_free(&bindings);
    return result;
}

/* A function answering Empty answers nothing, and one answering
 * NotReducible leaves its call to HE: both are the canonical evaluator's to
 * answer. */
static bool prepared_pure_no_answer_here(const Atom *value) {
    return atom_is_symbol_id((Atom *)value, g_builtin_syms.empty) ||
        atom_is_symbol_id((Atom *)value, g_builtin_syms.not_reducible);
}

/* Whether `call` is a call of a macro definition that takes every argument
 * as written and whose body is NotReducible: the call answers itself. */
static bool prepared_pure_written_constructor(
    CettaPreparedPureProgram *program, Atom *call) {
    HeMacroArgument arguments[HE_MACRO_MAX_ARITY];
    bool answers_syntax = false;
    if (!he_macro_signature(program->space, call, arguments,
                            &answers_syntax) ||
        !answers_syntax)
        return false;
    for (CettaExprLen i = 0u; i + 1u < call->expr.len; i++) {
        if (arguments[i] != HE_MACRO_ARGUMENT_WRITTEN)
            return false;
    }
    Atom *equation = space_single_linear_equation(
        program->space, call->expr.elems[0]->sym_id);
    return equation && equation->kind == ATOM_EXPR &&
        equation->expr.len == 3u &&
        atom_is_symbol_id(equation->expr.elems[2],
                          g_builtin_syms.not_reducible);
}

/* Whether HE evaluates `syntax` to itself: a value; a symbol no equation
 * names; an expression whose head is inert and whose elements are, or that
 * calls a written constructor (quote). */
static bool prepared_pure_syntax_is_inert(
    CettaPreparedPureProgram *program, Atom *syntax, uint32_t depth) {
    if (depth > PREPARED_PURE_MAX_COMPILE_DEPTH)
        return false;
    if (syntax->kind == ATOM_SYMBOL)
        return !space_equations_may_match_known_head(
            program->space, syntax->sym_id);
    if (syntax->kind != ATOM_EXPR || syntax->expr.len == 0u)
        return true;
    if (prepared_pure_head_role(program, syntax) != PREPARED_PURE_HEAD_INERT)
        return syntax->expr.elems[0]->kind == ATOM_SYMBOL &&
            prepared_pure_written_constructor(program, syntax);
    for (CettaExprIndex i = 1u; i < syntax->expr.len; i++) {
        if (!prepared_pure_syntax_is_inert(
                program, syntax->expr.elems[i], depth + 1u))
            return false;
    }
    return true;
}

/* `syntax` as data: never evaluated, each variable of it the value the
 * program holds there. */
static bool prepared_pure_compile_syntax(
    CettaPreparedPureProgram *program, PreparedPureCompileContext *context,
    Atom *syntax, uint32_t depth, uint32_t *node_out) {
    if (depth > PREPARED_PURE_MAX_COMPILE_DEPTH)
        return prepared_pure_reject(program, "syntax is too deep", syntax);
    if (syntax->kind == ATOM_VAR) {
        uint32_t slot = 0u;
        if (!prepared_pure_context_lookup(context, syntax->var_id, &slot))
            return prepared_pure_reject(
                program, "answered syntax holds an unbound variable", syntax);
        return prepared_pure_add_node(
            program,
            (PreparedPureNode){
                .kind = PREPARED_PURE_SLOT,
                .auxiliary = slot,
            },
            NULL, 0u, node_out);
    }
    if (syntax->kind != ATOM_EXPR || !atom_has_vars(syntax))
        return prepared_pure_add_node(
            program,
            (PreparedPureNode){
                .kind = PREPARED_PURE_LITERAL,
                .atom = syntax,
            },
            NULL, 0u, node_out);
    if (syntax->expr.len > UINT32_MAX ||
        !cetta_expr_len_mul_fits_size(syntax->expr.len, sizeof(uint32_t)))
        return false;
    uint32_t *children = malloc(sizeof(*children) * syntax->expr.len);
    if (!children)
        return false;
    bool ok = true;
    for (CettaExprIndex i = 0u; ok && i < syntax->expr.len; i++)
        ok = prepared_pure_compile_syntax(
            program, context, syntax->expr.elems[i], depth + 1u,
            &children[i]);
    ok = ok && prepared_pure_add_node(
        program, (PreparedPureNode){.kind = PREPARED_PURE_BUILD},
        children, (uint32_t)syntax->expr.len, node_out);
    free(children);
    return ok;
}

static bool prepared_pure_compile_delivery(
    CettaPreparedPureProgram *program, PreparedPureCompileContext *context,
    Atom *value, const PreparedPureDelivery *delivery, uint32_t depth,
    uint32_t *node_out) {
    if (depth > PREPARED_PURE_MAX_COMPILE_DEPTH)
        return prepared_pure_reject(program, "function is too deep", value);
    if (delivery->kind == PREPARED_PURE_DELIVER_CHAIN) {
        Arena *arena = prepared_pure_syntax_arena(program);
        Atom *next = arena ? prepared_pure_substitute_var(
            arena, delivery->var, value, delivery->template_atom) : NULL;
        return next && prepared_pure_compile_function(
            program, context, next, delivery->outer, depth + 1u, node_out);
    }
    if (prepared_pure_no_answer_here(value))
        return prepared_pure_add_node(
            program, (PreparedPureNode){.kind = PREPARED_PURE_DECLINE},
            NULL, 0u, node_out);
    /* A value the program holds is final, so syntax it answers must be
     * what HE would make of it again: a callee given it, or a let's body,
     * evaluates it once more. */
    if (delivery->kind == PREPARED_PURE_DELIVER_SYNTAX)
        return prepared_pure_syntax_is_inert(program, value, 0u)
            ? prepared_pure_compile_syntax(
                  program, context, value, depth + 1u, node_out)
            : prepared_pure_reject(
                  program, "answered syntax is evaluated where it is used",
                  value);
    if (delivery->var) {
        Arena *arena = prepared_pure_syntax_arena(program);
        value = arena ? prepared_pure_substitute_var(
            arena, delivery->var, value, delivery->template_atom) : NULL;
        if (!value)
            return false;
        if (prepared_pure_no_answer_here(value))
            return prepared_pure_add_node(
                program, (PreparedPureNode){.kind = PREPARED_PURE_DECLINE},
                NULL, 0u, node_out);
    }
    return prepared_pure_compile_eval(
        program, context, value, NULL, depth + 1u, node_out);
}

/* One evaluation step of `call`, decided at compile time: the one equation
 * of its head whose left side the call's syntax matches, its right side
 * instantiated afresh under the match; NotReducible when the syntax matches
 * none; DYNAMIC when the syntax cannot decide or two equations match.  The
 * equation's variables become the expansion's for the rest of the
 * expansion. */
static HeMinimalFold prepared_pure_fold_step(
    CettaPreparedPureProgram *program,
    const PreparedPureCompileContext *context, Atom *call, Arena *arena,
    Atom **result_out) {
    SymbolId head = atom_head_symbol_id(call);
    bool defined = false;
    if (head == SYMBOL_ID_NONE || call->kind != ATOM_EXPR ||
        space_query_effect_for_head(program->space, head, &defined) !=
            CETTA_GSLT_QUERY_EFFECT_PURE || !defined)
        return HE_MINIMAL_FOLD_DYNAMIC;
    SpaceEquationCursor cursor;
    if (!space_equation_cursor_init(program->space, head, &cursor))
        return HE_MINIMAL_FOLD_DYNAMIC;
    PreparedPureFoldScope scope = {.program = program, .context = context};
    Atom *selected = NULL;
    for (;;) {
        SpaceEquationOccurrenceId id;
        SpaceEquationCursorStep step =
            space_equation_cursor_next(&cursor, &id);
        if (step == SPACE_EQUATION_CURSOR_END)
            break;
        SpaceEquationOccurrence occurrence = {0};
        if (step != SPACE_EQUATION_CURSOR_ITEM ||
            !space_equation_occurrence_resolve(id, &occurrence) ||
            !occurrence.equation)
            return HE_MINIMAL_FOLD_DYNAMIC;
        Atom *fresh = cetta_instantiate_frame_syntax(
            arena, occurrence.equation);
        if (!fresh || fresh->kind != ATOM_EXPR || fresh->expr.len != 3u ||
            !atom_is_symbol_id(fresh->expr.elems[0], g_builtin_syms.equals) ||
            fresh->expr.elems[1]->kind != ATOM_EXPR ||
            fresh->expr.elems[1]->expr.len == 0u ||
            !atom_is_symbol_id(fresh->expr.elems[1]->expr.elems[0], head) ||
            !prepared_pure_collect_expansion_vars(program, fresh, 0u))
            return HE_MINIMAL_FOLD_DYNAMIC;
        Bindings matched;
        bindings_init(&matched);
        HeMinimalFold fold = he_minimal_fold_unify(
            fresh->expr.elems[1], call, &matched, arena,
            prepared_pure_fold_metavariable, &scope);
        Atom *rewritten = fold == HE_MINIMAL_FOLD_STATIC
            ? bindings_apply_if_vars(&matched, arena, fresh->expr.elems[2])
            : NULL;
        bindings_free(&matched);
        if (fold == HE_MINIMAL_FOLD_DYNAMIC ||
            (fold == HE_MINIMAL_FOLD_STATIC && (!rewritten || selected)))
            return HE_MINIMAL_FOLD_DYNAMIC;
        if (fold == HE_MINIMAL_FOLD_STATIC)
            selected = rewritten;
    }
    *result_out = selected
        ? selected : atom_symbol_id(arena, g_builtin_syms.not_reducible);
    return *result_out ? HE_MINIMAL_FOLD_STATIC : HE_MINIMAL_FOLD_DYNAMIC;
}

/* A unify only a run decides, in a function body: a pattern test of the
 * operand holding a value chooses between the then branch, under the
 * pattern's bind, and the else branch, both in the body's continuation. */
static bool prepared_pure_compile_function_unify(
    CettaPreparedPureProgram *program, PreparedPureCompileContext *context,
    Atom *body, const PreparedPureDelivery *delivery, uint32_t depth,
    uint32_t *node_out) {
    uint32_t value_index = prepared_pure_unify_value_side(context, body);
    if (value_index == 0u)
        return prepared_pure_reject(
            program, "neither unify operand holds a value", body);
    Atom *pattern = body->expr.elems[value_index == 1u ? 2u : 1u];
    uint32_t else_node = 0u;
    if (!prepared_pure_compile_function(
            program, context, body->expr.elems[4], delivery, depth + 1u,
            &else_node))
        return false;
    uint32_t target = 0u;
    if (!prepared_pure_compile_template(
            program, context, body->expr.elems[value_index], NULL,
            depth + 1u, true, &target))
        return prepared_pure_reject(
            program, "unify value is outside the fragment",
            body->expr.elems[value_index]);
    size_t saved_bindings = context->len;
    uint32_t saved_next_slot = context->next_slot;
    uint32_t test_pattern = 0u;
    if (!prepared_pure_compile_bind_pattern(
            program, context, pattern, &test_pattern))
        return false;
    context->len = saved_bindings;
    context->next_slot = saved_next_slot;
    const PreparedPureBindPattern *tested =
        &program->bind_patterns[test_pattern];
    for (uint32_t i = 0u; i < tested->var_count; i++) {
        if (program->bind_vars[tested->first_var + i].prebound)
            return prepared_pure_reject(
                program, "unify pattern compares a bound variable", pattern);
    }
    uint32_t test = 0u;
    if (!prepared_pure_add_node(
            program,
            (PreparedPureNode){
                .kind = PREPARED_PURE_OBSERVE,
                .auxiliary = PREPARED_PURE_OBSERVE_PATTERN_TEST |
                             test_pattern,
            },
            &target, 1u, &test))
        return false;
    uint32_t bind_pattern = 0u;
    if (!prepared_pure_compile_bind_pattern(
            program, context, pattern, &bind_pattern))
        return false;
    uint32_t then_body = 0u;
    bool then_ok = prepared_pure_compile_function(
        program, context, body->expr.elems[3], delivery, depth + 1u,
        &then_body);
    context->len = saved_bindings;
    if (!then_ok)
        return false;
    uint32_t bound[2] = {target, then_body};
    uint32_t then_node = 0u;
    if (!prepared_pure_add_node(
            program,
            (PreparedPureNode){
                .kind = PREPARED_PURE_BIND,
                .auxiliary = bind_pattern,
            },
            bound, 2u, &then_node))
        return false;
    uint32_t branch[3] = {test, then_node, else_node};
    return prepared_pure_add_node(
        program, (PreparedPureNode){.kind = PREPARED_PURE_IF},
        branch, 3u, node_out);
}

/* A function's body, in its continuation: a return delivers its syntax; a
 * chain runs its computation (folded, one evaluation step of a function
 * whose returns continue in the chain's template, or a value operation)
 * and continues in its template; a unify the syntax decides continues in
 * its branch, and one it does not is a pattern test whose two branches
 * share the continuation. */
static bool prepared_pure_compile_function(
    CettaPreparedPureProgram *program, PreparedPureCompileContext *context,
    Atom *body, const PreparedPureDelivery *delivery, uint32_t depth,
    uint32_t *node_out) {
    if (!body || depth > PREPARED_PURE_MAX_COMPILE_DEPTH)
        return prepared_pure_reject(program, "function is too deep", body);
    Arena *arena = prepared_pure_syntax_arena(program);
    if (!arena)
        return false;
    PreparedPureFoldScope scope = {.program = program, .context = context};
    SymbolId head = atom_head_symbol_id(body);
    CettaExprLen len = body->kind == ATOM_EXPR ? body->expr.len : 0u;
    if (head == g_builtin_syms.return_text && len == 2u)
        return prepared_pure_compile_delivery(
            program, context, body->expr.elems[1], delivery, depth + 1u,
            node_out);
    if (head == g_builtin_syms.function && len == 2u)
        return prepared_pure_compile_function(
            program, context, body->expr.elems[1], delivery, depth + 1u,
            node_out);
    if (head == g_builtin_syms.chain && len == 4u &&
        body->expr.elems[2]->kind == ATOM_VAR) {
        Atom *nested = body->expr.elems[1];
        Atom *var = body->expr.elems[2];
        Atom *rest = body->expr.elems[3];
        Atom *step = NULL;
        bool grounded_step = false;
        if (atom_head_symbol_id(nested) == g_builtin_syms.eval &&
            nested->expr.len == 2u) {
            Atom *call = nested->expr.elems[1];
            SymbolId call_head = atom_head_symbol_id(call);
            CettaExprLen call_arity = call->kind == ATOM_EXPR &&
                    call->expr.len > 0u
                ? call->expr.len - 1u : 0u;
            /* One evaluation step of a grounded operation is its value. */
            grounded_step = call_head != SYMBOL_ID_NONE &&
                (cetta_prepared_pure_intrinsic_program(
                     call_head, call_arity, NULL) ||
                 prepared_pure_register_program(
                     program, call_head, call_arity, NULL, NULL));
            if (grounded_step) {
                /* compiled as a value bind below */
            } else if (call_head == g_builtin_syms.if_equal &&
                call->expr.len == 5u) {
                PreparedPureStaticEqual equal = prepared_pure_static_equal(
                    call->expr.elems[1], call->expr.elems[2], 0u);
                if (equal == PREPARED_PURE_EQUAL_UNKNOWN)
                    return prepared_pure_reject(
                        program, "if-equal over values is outside the fragment",
                        call);
                step = call->expr.elems[equal == PREPARED_PURE_EQUAL_SAME
                                            ? 3u : 4u];
            } else if (prepared_pure_fold_step(
                           program, context, call, arena, &step) !=
                       HE_MINIMAL_FOLD_STATIC) {
                return prepared_pure_reject(
                    program, "evaluation step is outside the fragment", call);
            }
            if (step && atom_head_symbol_id(step) == g_builtin_syms.function &&
                step->kind == ATOM_EXPR && step->expr.len == 2u) {
                PreparedPureDelivery chained = {
                    .kind = PREPARED_PURE_DELIVER_CHAIN,
                    .var = var,
                    .template_atom = rest,
                    .outer = delivery,
                };
                return prepared_pure_compile_function(
                    program, context, step->expr.elems[1], &chained,
                    depth + 1u, node_out);
            }
        } else if (he_minimal_fold_instruction(
                       nested, arena, prepared_pure_fold_metavariable,
                       &scope, &step) != HE_MINIMAL_FOLD_STATIC) {
            step = NULL;
        }
        if (step) {
            Atom *next = prepared_pure_substitute_var(arena, var, step, rest);
            return next && prepared_pure_compile_function(
                program, context, next, delivery, depth + 1u, node_out);
        }
        SymbolId nested_head = atom_head_symbol_id(nested);
        bool value_operation = grounded_step ||
            (nested_head == g_builtin_syms.decons_atom &&
             nested->expr.len == 2u &&
             nested->expr.elems[1]->kind == ATOM_VAR) ||
            (nested_head == g_builtin_syms.cons_atom &&
             nested->expr.len == 3u &&
             nested->expr.elems[2]->kind == ATOM_VAR);
        if (!value_operation)
            return prepared_pure_reject(
                program, "chain's computation is outside the fragment",
                nested);
        uint32_t bound = 0u;
        if (!prepared_pure_compile_eval(
                program, context, nested, NULL, depth + 1u, &bound))
            return false;
        size_t saved_bindings = context->len;
        uint32_t pattern_index = 0u;
        if (!prepared_pure_compile_bind_pattern(
                program, context, var, &pattern_index))
            return false;
        uint32_t continued = 0u;
        bool ok = prepared_pure_compile_function(
            program, context, rest, delivery, depth + 1u, &continued);
        context->len = saved_bindings;
        if (!ok)
            return false;
        uint32_t children[2] = {bound, continued};
        return prepared_pure_add_node(
            program,
            (PreparedPureNode){
                .kind = PREPARED_PURE_BIND,
                .auxiliary = pattern_index,
            },
            children, 2u, node_out);
    }
    if (head == g_builtin_syms.unify && len == 5u) {
        Bindings matched;
        bindings_init(&matched);
        HeMinimalFold fold = he_minimal_fold_unify(
            body->expr.elems[1], body->expr.elems[2], &matched, arena,
            prepared_pure_fold_metavariable, &scope);
        Atom *chosen = fold == HE_MINIMAL_FOLD_STATIC
            ? bindings_apply_if_vars(&matched, arena, body->expr.elems[3])
            : fold == HE_MINIMAL_FOLD_MISMATCH ? body->expr.elems[4] : NULL;
        bindings_free(&matched);
        if (chosen)
            return prepared_pure_compile_function(
                program, context, chosen, delivery, depth + 1u, node_out);
        return prepared_pure_compile_function_unify(
            program, context, body, delivery, depth, node_out);
    }
    return prepared_pure_reject(
        program, "function body is outside the fragment", body);
}

/* What a macro call answers once its evaluated arguments hold their values.
 * Where its range is not Atom, its body evaluated.  Where it is, the body
 * runs and the syntax it answers goes to `delivery`: as it is, or to the
 * callee given it.  A body of NotReducible leaves the call itself, with the
 * values of its evaluated arguments. */
static bool prepared_pure_compile_expansion_body(
    CettaPreparedPureProgram *program, PreparedPureCompileContext *context,
    const HeMacroExpansion *expansion, Atom *call,
    const PreparedPureDelivery *delivery, uint32_t depth,
    uint32_t *node_out) {
    if (!expansion->answers_syntax)
        return prepared_pure_compile_eval(
            program, context, expansion->body, NULL, depth + 1u, node_out);
    PreparedPureDelivery as_written = {.kind = PREPARED_PURE_DELIVER_SYNTAX};
    if (!delivery)
        delivery = &as_written;
    Atom *body = expansion->body;
    if (atom_is_symbol_id(body, g_builtin_syms.not_reducible)) {
        Arena *arena = prepared_pure_syntax_arena(program);
        CettaExprLen len = call->expr.len;
        Atom **elems = arena
            ? arena_alloc(arena, sizeof(*elems) * (size_t)len) : NULL;
        if (!elems)
            return false;
        elems[0] = call->expr.elems[0];
        for (CettaExprLen i = 1u; i < len; i++)
            elems[i] = expansion->arguments[i - 1u] ==
                    HE_MACRO_ARGUMENT_EVALUATED
                ? expansion->lhs->expr.elems[i] : call->expr.elems[i];
        Atom *unreduced = atom_expr(arena, elems, len);
        return unreduced && prepared_pure_compile_delivery(
            program, context, unreduced, delivery, depth + 1u, node_out);
    }
    if (atom_head_symbol_id(body) == g_builtin_syms.function &&
        body->expr.len == 2u)
        return prepared_pure_compile_function(
            program, context, body->expr.elems[1], delivery, depth + 1u,
            node_out);
    return prepared_pure_compile_delivery(
        program, context, body, delivery, depth + 1u, node_out);
}

/* The expansion's body under its arguments from `next` on: an evaluated one
 * binds its fresh parameter to its value, as HE evaluates such an argument
 * before the call; a variable given to an Expression parameter holds a
 * value HE requires to be an expression, and any other value leaves the
 * call to the canonical evaluator, which answers HE's argument type error. */
static bool prepared_pure_compile_expansion(
    CettaPreparedPureProgram *program, PreparedPureCompileContext *context,
    const HeMacroExpansion *expansion, Atom *call, const void *call_view,
    const PreparedPureDelivery *delivery, CettaExprLen next, uint32_t depth,
    uint32_t *node_out) {
    CettaExprLen arity = call->expr.len - 1u;
    while (next < arity &&
           expansion->arguments[next] == HE_MACRO_ARGUMENT_WRITTEN)
        next++;
    if (next == arity)
        return prepared_pure_compile_expansion_body(
            program, context, expansion, call, delivery, depth, node_out);
    if (expansion->arguments[next] == HE_MACRO_ARGUMENT_EXPRESSION_VALUE) {
        uint32_t value = 0u;
        if (!prepared_pure_compile_template(
                program, context, call->expr.elems[next + 1u],
                prepared_pure_source_child(program, call_view, next + 1u),
                depth + 1u, true, &value))
            return false;
        uint32_t test = 0u;
        if (!prepared_pure_add_node(
                program,
                (PreparedPureNode){
                    .kind = PREPARED_PURE_OBSERVE,
                    .auxiliary = PREPARED_PURE_OBSERVE_ATOM_EXPRESSION,
                },
                &value, 1u, &test))
            return false;
        uint32_t rest = 0u;
        if (!prepared_pure_compile_expansion(
                program, context, expansion, call, call_view, delivery,
                next + 1u, depth + 1u, &rest))
            return false;
        uint32_t declined = 0u;
        if (!prepared_pure_add_node(
                program, (PreparedPureNode){.kind = PREPARED_PURE_DECLINE},
                NULL, 0u, &declined))
            return false;
        uint32_t branch[3] = {test, rest, declined};
        return prepared_pure_add_node(
            program, (PreparedPureNode){.kind = PREPARED_PURE_IF},
            branch, 3u, node_out);
    }
    uint32_t bound = 0u;
    if (!prepared_pure_compile_eval(
            program, context, call->expr.elems[next + 1u],
            prepared_pure_source_child(program, call_view, next + 1u),
            depth + 1u, &bound))
        return false;
    size_t saved_bindings = context->len;
    uint32_t pattern_index = 0u;
    if (!prepared_pure_compile_bind_pattern(
            program, context, expansion->lhs->expr.elems[next + 1u],
            &pattern_index))
        return false;
    uint32_t rest = 0u;
    bool ok = prepared_pure_compile_expansion(
        program, context, expansion, call, call_view, delivery, next + 1u,
        depth + 1u, &rest);
    context->len = saved_bindings;
    if (!ok)
        return false;
    uint32_t children[2] = {bound, rest};
    return prepared_pure_add_node(
        program,
        (PreparedPureNode){
            .kind = PREPARED_PURE_BIND,
            .auxiliary = pattern_index,
        },
        children, 2u, node_out);
}

/* A call of a macro definition with one equation over distinct variables
 * is expanded where it is: the equation with fresh variables, each argument
 * taken as written substituted for its parameter, the others bound to their
 * values.  The definition read is the one the program is pinned to.
 * *expanded tells whether the call was a macro call. */
static bool prepared_pure_try_expand_macro(
    CettaPreparedPureProgram *program, PreparedPureCompileContext *context,
    Atom *source, const void *source_view,
    const PreparedPureDelivery *delivery, uint32_t depth,
    uint32_t *node_out, bool *expanded) {
    *expanded = false;
    if (!eval_current_language_id || eval_current_language_id() != CETTA_LANGUAGE_HE ||
        program->call_mode != CETTA_GSLT_PURE_CALL_EAGER ||
        prepared_pure_wants_answers(program))
        return true;
    Arena *arena = prepared_pure_syntax_arena(program);
    HeMacroExpansion expansion;
    HeMacroExpandResult expand = arena
        ? he_macro_expand(program->space, arena, source, &expansion)
        : HE_MACRO_NOT_A_MACRO;
    if (expand == HE_MACRO_NOT_A_MACRO)
        return true;
    *expanded = true;
    if (expand != HE_MACRO_EXPANDED)
        return prepared_pure_reject(
            program, "macro definition cannot be instantiated", source);
    if (program->expansion_depth >= PREPARED_PURE_MAX_EXPANSION_DEPTH)
        return prepared_pure_reject(
            program, "macro expansion is too deep", source);
    size_t expansion_vars = program->expansion_var_len;
    if (!prepared_pure_collect_expansion_vars(
            program, expansion.fresh, 0u)) {
        program->expansion_var_len = expansion_vars;
        return prepared_pure_reject(
            program, "macro definition's variables cannot be recorded",
            source);
    }
    program->expansion_depth++;
    bool ok = prepared_pure_compile_expansion(
        program, context, &expansion, source, source_view, delivery, 0u,
        depth, node_out);
    program->expansion_depth--;
    program->expansion_var_len = expansion_vars;
    return ok;
}

/* Whether `head` takes every argument as a value and answers a value it is
 * given no type for: untyped at `arity`, or one signature of %Undefined%
 * and type variables of their own. */
static bool prepared_pure_head_takes_values(
    Space *space, SymbolId head, CettaExprLen arity) {
    if (space_head_call_untyped(space, head, arity))
        return true;
    uint8_t classes[66];
    if (!prepared_pure_value_signature(space, head, arity, classes))
        return false;
    for (CettaExprLen i = 0u; i <= arity; i++) {
        if (classes[i] != PREPARED_PURE_VALUE_UNDEFINED)
            return false;
    }
    return true;
}

/* g's parameters from `next` on: each argument but the one that answers
 * syntax binds its parameter to its value, as HE evaluates it before the
 * call; that one then expands, and each syntax it answers stands for its
 * parameter in g's body, which is evaluated. */
static bool prepared_pure_compile_syntax_consumer(
    CettaPreparedPureProgram *program, PreparedPureCompileContext *context,
    Atom *call, Atom *lhs, Atom *body, CettaExprLen syntax_index,
    CettaExprLen next, uint32_t depth, uint32_t *node_out) {
    CettaExprLen arity = call->expr.len - 1u;
    if (next == syntax_index)
        next++;
    if (next >= arity) {
        PreparedPureDelivery into_body = {
            .kind = PREPARED_PURE_DELIVER_EVAL,
            .var = lhs->expr.elems[syntax_index + 1u],
            .template_atom = body,
        };
        bool expanded = false;
        Atom *argument = call->expr.elems[syntax_index + 1u];
        if (!prepared_pure_try_expand_macro(
                program, context, argument, NULL, &into_body, depth + 1u,
                node_out, &expanded))
            return false;
        return expanded || prepared_pure_reject(
            program, "an argument that answers syntax did not expand",
            argument);
    }
    uint32_t bound = 0u;
    if (!prepared_pure_compile_eval(
            program, context, call->expr.elems[next + 1u], NULL,
            depth + 1u, &bound))
        return false;
    size_t saved_bindings = context->len;
    uint32_t pattern_index = 0u;
    if (!prepared_pure_compile_bind_pattern(
            program, context, lhs->expr.elems[next + 1u], &pattern_index))
        return false;
    uint32_t rest = 0u;
    bool ok = prepared_pure_compile_syntax_consumer(
        program, context, call, lhs, body, syntax_index, next + 1u,
        depth + 1u, &rest);
    context->len = saved_bindings;
    if (!ok)
        return false;
    uint32_t children[2] = {bound, rest};
    return prepared_pure_add_node(
        program,
        (PreparedPureNode){
            .kind = PREPARED_PURE_BIND,
            .auxiliary = pattern_index,
        },
        children, 2u, node_out);
}

/* A call (g ... A ...) whose argument A calls a macro definition with range
 * Atom: HE evaluates g's other arguments, gives g the syntax A answers as
 * that argument's value, and evaluates what g answers.  Where g is one
 * equation over distinct variables that takes values and answers a value,
 * its body compiles in each of A's returns, the returned syntax in place of
 * its parameter.  *inlined tells whether the call was such a call. */
static bool prepared_pure_try_inline_syntax_consumer(
    CettaPreparedPureProgram *program, PreparedPureCompileContext *context,
    Atom *call, uint32_t depth, uint32_t *node_out, bool *inlined) {
    *inlined = false;
    if (!eval_current_language_id || eval_current_language_id() != CETTA_LANGUAGE_HE ||
        program->call_mode != CETTA_GSLT_PURE_CALL_EAGER ||
        prepared_pure_wants_answers(program) ||
        call->kind != ATOM_EXPR || call->expr.len < 2u ||
        call->expr.elems[0]->kind != ATOM_SYMBOL)
        return true;
    CettaExprLen arity = call->expr.len - 1u;
    CettaExprLen syntax_index = arity;
    for (CettaExprLen i = 0u; i < arity; i++) {
        HeMacroArgument arguments[HE_MACRO_MAX_ARITY];
        bool answers_syntax = false;
        Atom *argument = call->expr.elems[i + 1u];
        if (argument->kind != ATOM_EXPR ||
            !he_macro_signature(program->space, argument, arguments,
                                &answers_syntax) ||
            !answers_syntax)
            continue;
        if (syntax_index != arity)
            return true;
        syntax_index = i;
    }
    SymbolId head = call->expr.elems[0]->sym_id;
    if (syntax_index == arity ||
        !prepared_pure_head_takes_values(program->space, head, arity))
        return true;
    Atom *equation = space_single_linear_equation(program->space, head);
    Arena *arena = prepared_pure_syntax_arena(program);
    Atom *fresh = equation && arena
        ? cetta_instantiate_frame_syntax(arena, equation) : NULL;
    Atom *lhs = fresh && fresh->kind == ATOM_EXPR && fresh->expr.len == 3u
        ? fresh->expr.elems[1] : NULL;
    if (!lhs || lhs->kind != ATOM_EXPR ||
        lhs->expr.len != (CettaExprLen)(arity + 1u))
        return true;
    *inlined = true;
    if (program->expansion_depth >= PREPARED_PURE_MAX_EXPANSION_DEPTH)
        return prepared_pure_reject(
            program, "macro expansion is too deep", call);
    size_t expansion_vars = program->expansion_var_len;
    if (!prepared_pure_collect_expansion_vars(program, fresh, 0u)) {
        program->expansion_var_len = expansion_vars;
        return prepared_pure_reject(
            program, "definition's variables cannot be recorded", call);
    }
    program->expansion_depth++;
    bool ok = prepared_pure_compile_syntax_consumer(
        program, context, call, lhs, fresh->expr.elems[2], syntax_index,
        0u, depth, node_out);
    program->expansion_depth--;
    program->expansion_var_len = expansion_vars;
    return ok;
}

/* The first occurrence of each distinct variable of `term`, appended to
 * `vars`. */
static bool prepared_pure_term_vars(
    Atom *term, Atom ***vars, size_t *len, size_t *capacity,
    uint32_t depth) {
    if (!term || depth > PREPARED_PURE_MAX_COMPILE_DEPTH)
        return false;
    if (term->kind == ATOM_VAR) {
        for (size_t i = 0u; i < *len; i++) {
            if (var_id_eq((*vars)[i]->var_id, term->var_id))
                return true;
        }
        if (!prepared_pure_reserve(
                (void **)vars, sizeof(**vars), capacity, *len + 1u))
            return false;
        (*vars)[(*len)++] = term;
        return true;
    }
    if (term->kind != ATOM_EXPR)
        return true;
    for (CettaExprIndex i = 0u; i < term->expr.len; i++) {
        if (!prepared_pure_term_vars(
                term->expr.elems[i], vars, len, capacity, depth + 1u))
            return false;
    }
    return true;
}

/* `body` where no variable of `term` holds an expression, which declines
 * otherwise: one test of each variable's value, outermost first. */
static bool prepared_pure_guard_values_not_expressions(
    CettaPreparedPureProgram *program, PreparedPureCompileContext *context,
    Atom *term, uint32_t body, uint32_t depth, uint32_t *node_out) {
    Atom **vars = NULL;
    size_t len = 0u;
    size_t capacity = 0u;
    if (!prepared_pure_term_vars(term, &vars, &len, &capacity, depth)) {
        free(vars);
        return prepared_pure_reject(
            program, "eval term's variables cannot be recorded", term);
    }
    uint32_t guarded = body;
    bool ok = true;
    for (size_t i = len; ok && i > 0u; i--) {
        uint32_t value = 0u;
        uint32_t test = 0u;
        uint32_t declined = 0u;
        ok = prepared_pure_compile_template(
                 program, context, vars[i - 1u], NULL, depth + 1u, false,
                 &value) &&
            prepared_pure_add_node(
                program,
                (PreparedPureNode){
                    .kind = PREPARED_PURE_OBSERVE,
                    .auxiliary = PREPARED_PURE_OBSERVE_ATOM_EXPRESSION,
                },
                &value, 1u, &test) &&
            prepared_pure_add_node(
                program, (PreparedPureNode){.kind = PREPARED_PURE_DECLINE},
                NULL, 0u, &declined);
        if (ok) {
            uint32_t branch[3] = {test, declined, guarded};
            ok = prepared_pure_add_node(
                program, (PreparedPureNode){.kind = PREPARED_PURE_IF},
                branch, 3u, &guarded);
        }
    }
    free(vars);
    if (ok)
        *node_out = guarded;
    return ok;
}

/* Whether `source`, a ground HE expression, is data all the way down: each
 * subexpression headed by a symbol that no rule defines and no declaration
 * types, each leaf a scalar or a symbol the dialect does not keep for its own
 * evaluator.  Evaluated, it is itself, so it compiles as the literal it is;
 * compiled part by part, a value sharing its subterms would compile once per
 * path to each. */
static bool prepared_pure_ground_data(CettaPreparedPureProgram *program,
                                      Atom *source) {
    /* HE's: a dialect whose lowering decides what an expression denotes
     * (PeTTa's list forms) is compiled part by part. */
    if (!eval_current_language_id ||
        eval_current_language_id() != CETTA_LANGUAGE_HE)
        return false;
    if (!source || source->kind != ATOM_EXPR || source->expr.len == 0u ||
        atom_has_vars(source) || atom_has_registry_refs(source) ||
        atom_has_identity_grounded(source) ||
        (source->flags & ATOM_FLAG_HAS_THREAD_LOCAL_RESOURCE))
        return false;
    Atom **stack = NULL;
    size_t len = 0u, cap = 0u;
    AtomVisitSet seen = {0};
    bool fault = false;
    bool data = prepared_pure_reserve(
        (void **)&stack, sizeof(*stack), &cap, 1u);
    if (data)
        stack[len++] = source;
    while (data && len > 0u) {
        Atom *node = stack[--len];
        if (node->kind == ATOM_SYMBOL) {
            CettaPreparedPureExpressionView view = {0};
            data = !program->expression_view ||
                program->expression_view(node, NULL, &view) !=
                    CETTA_PREPARED_PURE_EXPRESSION_CANONICAL_ONLY;
            continue;
        }
        if (node->kind == ATOM_GROUNDED) {
            switch (node->ground.gkind) {
            case GV_INT: case GV_FLOAT: case GV_BOOL: case GV_STRING:
            case GV_BIGINT: case GV_RATIONAL:
                continue;
            default:
                data = false;
                continue;
            }
        }
        if (node->kind != ATOM_EXPR || node->expr.len == 0u) {
            data = node->kind == ATOM_EXPR;
            continue;
        }
        if (node != source && !fault &&
            !atom_visit_set_add(&seen, node, &fault) && !fault)
            continue;
        Atom *head = node->expr.elems[0];
        if (!head || head->kind != ATOM_SYMBOL ||
            prepared_pure_head_role(program, node) != PREPARED_PURE_HEAD_INERT ||
            space_head_declares_type(program->space, head->sym_id) ||
            !prepared_pure_reserve((void **)&stack, sizeof(*stack), &cap,
                                   len + node->expr.len)) {
            data = false;
            continue;
        }
        for (CettaExprIndex i = 1u; i < node->expr.len; i++)
            stack[len++] = node->expr.elems[i];
    }
    free(stack);
    atom_visit_set_free(&seen);
    return data;
}

/* One arm of a selection over the value `held` names: a pattern test of
 * that value chooses between the arm's body, under the pattern's bind, and
 * `otherwise`, the arms after it. */
static bool prepared_pure_compile_select_arm(
    CettaPreparedPureProgram *program, PreparedPureCompileContext *context,
    Atom *held, Atom *arm, const void *arm_view, uint32_t otherwise,
    uint32_t depth, uint32_t *node_out) {
    Atom *pattern = arm->expr.elems[0];
    uint32_t target = 0u;
    if (!prepared_pure_compile_template(
            program, context, held, NULL, depth + 1u, true, &target))
        return false;
    size_t saved_bindings = context->len;
    uint32_t saved_next_slot = context->next_slot;
    uint32_t test_pattern = 0u;
    if (!prepared_pure_compile_inert_bind_pattern(
            program, context, pattern,
            prepared_pure_source_child(program, arm_view, 0u),
            &test_pattern))
        return prepared_pure_reject(
            program, "selection pattern is outside the fragment", pattern);
    context->len = saved_bindings;
    context->next_slot = saved_next_slot;
    /* The test reads only its value: a pattern that compares against a
     * variable bound before it is left to the canonical evaluator. */
    const PreparedPureBindPattern *tested =
        &program->bind_patterns[test_pattern];
    for (uint32_t i = 0u; i < tested->var_count; i++) {
        if (program->bind_vars[tested->first_var + i].prebound)
            return prepared_pure_reject(
                program, "selection pattern compares a bound variable",
                pattern);
    }
    uint32_t test = 0u;
    if (!prepared_pure_add_node(
            program,
            (PreparedPureNode){
                .kind = PREPARED_PURE_OBSERVE,
                .auxiliary = PREPARED_PURE_OBSERVE_PATTERN_TEST |
                             test_pattern,
            },
            &target, 1u, &test))
        return false;
    uint32_t bind_pattern = 0u;
    if (!prepared_pure_compile_bind_pattern(
            program, context, pattern, &bind_pattern))
        return false;
    uint32_t body = 0u;
    bool body_ok = prepared_pure_compile_eval(
        program, context, arm->expr.elems[1],
        prepared_pure_source_child(program, arm_view, 1u), depth + 1u,
        &body);
    context->len = saved_bindings;
    if (!body_ok)
        return prepared_pure_reject(
            program, "selection body is outside the fragment",
            arm->expr.elems[1]);
    uint32_t bound[2] = {target, body};
    uint32_t then_node = 0u;
    if (!prepared_pure_add_node(
            program,
            (PreparedPureNode){
                .kind = PREPARED_PURE_BIND,
                .auxiliary = bind_pattern,
            },
            bound, 2u, &then_node))
        return false;
    uint32_t branch[3] = {test, then_node, otherwise};
    return prepared_pure_add_node(
        program, (PreparedPureNode){.kind = PREPARED_PURE_IF},
        branch, 3u, node_out);
}

/* A committed first-match selection (CETTA_PREPARED_PURE_EXPRESSION_SELECT):
 * the key's value is bound once to a fresh variable, and the arms, last to
 * first, become pattern tests of it.  After the last arm nothing answers:
 * a single-answer program declines to the canonical evaluator, and an
 * answer producer answers zero. */
static bool prepared_pure_compile_select(
    CettaPreparedPureProgram *program, PreparedPureCompileContext *context,
    Atom *source, const void *source_view, uint32_t depth,
    uint32_t *node_out) {
    CettaControlOperationPlan control = prepared_pure_control_plan(program, source, source_view);
    CettaExprLen arity = source->expr.len - 1u;
    if (!cetta_control_operation_match_branches_layout(control, arity))
        return prepared_pure_reject(program, "invalid selection operand plan", source);
    CettaExprIndex key_child = (CettaExprIndex)control.source + 1u;
    CettaExprIndex arms_child = (CettaExprIndex)control.first + 1u;
    Atom *arms = source->expr.elems[arms_child];
    if (!arms || arms->kind != ATOM_EXPR || arms->expr.len == 0u)
        return prepared_pure_reject(
            program, "selection arms are outside the fragment", source);
    for (CettaExprIndex index = 0u; index < arms->expr.len; index++) {
        Atom *arm = arms->expr.elems[index];
        if (!arm || arm->kind != ATOM_EXPR || arm->expr.len != 2u)
            return prepared_pure_reject(
                program, "selection arm is not a pattern and a body", arms);
    }
    uint32_t key = 0u;
    if (!prepared_pure_compile_eval(
            program, context, source->expr.elems[key_child],
            prepared_pure_source_child(program, source_view, key_child),
            depth + 1u, &key))
        return prepared_pure_reject(
            program, "selection key is outside the fragment",
            source->expr.elems[key_child]);
    Arena *arena = prepared_pure_syntax_arena(program);
    Atom *held = arena
        ? atom_var_with_id(arena, "__select_key", fresh_var_id()) : NULL;
    if (!held)
        return false;
    size_t saved_bindings = context->len;
    uint32_t held_pattern = 0u;
    if (!prepared_pure_compile_bind_pattern(
            program, context, held, &held_pattern))
        return false;
    const void *arms_view =
        prepared_pure_source_child(program, source_view, arms_child);
    uint32_t next = 0u;
    bool ok = prepared_pure_add_node(
        program,
        (PreparedPureNode){
            .kind = PREPARED_PURE_ZERO,
        },
        NULL, 0u, &next);
    for (CettaExprIndex index = arms->expr.len; ok && index-- > 0u;)
        ok = prepared_pure_compile_select_arm(
            program, context, held, arms->expr.elems[index],
            prepared_pure_source_child(program, arms_view, index), next,
            depth, &next);
    context->len = saved_bindings;
    if (!ok)
        return false;
    uint32_t children[2] = {key, next};
    return prepared_pure_add_node(
        program,
        (PreparedPureNode){
            .kind = PREPARED_PURE_BIND,
            .auxiliary = held_pattern,
        },
        children, 2u, node_out);
}

static bool prepared_pure_compile_eval_with_role_in_source(
    CettaPreparedPureProgram *program,
    PreparedPureCompileContext *context,
    Atom *source, const void *source_view,
    CettaPreparedPureSourceRole source_role,
    uint32_t depth, uint32_t *node_out);

static bool prepared_pure_compile_eval_with_role(
    CettaPreparedPureProgram *program,
    PreparedPureCompileContext *context,
    Atom *source, const void *source_view,
    CettaPreparedPureSourceRole source_role,
    uint32_t depth, uint32_t *node_out) {
    if (!program)
        return false;
    if (!program->answer_producer || !program->source_view.project_source)
        return prepared_pure_compile_eval_with_role_in_source(program, context, source, source_view, source_role, depth, node_out);
    const void *saved = program->compiling_source;
    program->compiling_source = source_view;
    bool ok = prepared_pure_compile_eval_with_role_in_source(program, context, source, source_view, source_role, depth, node_out);
    program->compiling_source = saved;
    return ok;
}

static bool prepared_pure_compile_eval_with_role_in_source(
    CettaPreparedPureProgram *program,
    PreparedPureCompileContext *context,
    Atom *source, const void *source_view,
    CettaPreparedPureSourceRole source_role,
    uint32_t depth, uint32_t *node_out) {
    if (!program || !context || !source || !node_out ||
        depth > PREPARED_PURE_MAX_COMPILE_DEPTH)
        return false;
    CettaControlOperationPlan control = prepared_pure_control_plan(
        program, source, source_view);
    if (source_role != CETTA_PREPARED_PURE_SOURCE_DYNAMIC_CALL &&
        source_role != CETTA_PREPARED_PURE_SOURCE_DECLINE &&
        (control.operation == CETTA_CONTROL_NONE ||
         control.operation == CETTA_CONTROL_RETURN_VALUE ||
         control.operation == CETTA_CONTROL_CONSTRUCT) &&
        prepared_pure_ground_data(program, source))
        return prepared_pure_add_node(
            program,
            (PreparedPureNode){
                .kind = PREPARED_PURE_LITERAL,
                .atom = source,
            },
            NULL, 0u, node_out);
    if (source->kind == ATOM_VAR) {
        uint32_t slot = 0u;
        if (!prepared_pure_context_lookup(context, source->var_id, &slot))
            return false;
        /* Eager equation and let bindings are populated only after their
         * producing child has completed.  Re-evaluating an expression-valued
         * result would mistake flat data such as `(1)` for a dynamic call.
         * Need bindings retain EVAL_SLOT because they may still be suspended. */
        return prepared_pure_add_node(
            program,
            (PreparedPureNode){
                .kind = program->call_mode == CETTA_GSLT_PURE_CALL_EAGER
                    ? PREPARED_PURE_SLOT
                    : PREPARED_PURE_EVAL_SLOT,
                .auxiliary = slot,
            },
            NULL, 0u, node_out);
    }
    /* A dialect may reserve a symbol for its own evaluator, as HE reserves
     * NotReducible for its call-return protocol: the view marks such a
     * symbol canonical-only wherever it occurs. */
    if (source->kind == ATOM_SYMBOL && program->expression_view) {
        CettaPreparedPureExpressionView symbol_view = {0};
        if (prepared_pure_describe_expression(program, source, source_view,
                &symbol_view) ==
            CETTA_PREPARED_PURE_EXPRESSION_CANONICAL_ONLY)
            return prepared_pure_reject(
                program, "dialect-owned symbol requires canonical evaluation",
                source);
    }
    if (source_role == CETTA_PREPARED_PURE_SOURCE_DYNAMIC_CALL ||
        source_role == CETTA_PREPARED_PURE_SOURCE_DECLINE)
        return prepared_pure_reject(
            program, "source view declines prepared evaluation", source);
    if (source_role == CETTA_PREPARED_PURE_SOURCE_VALUE) {
        if (source->kind == ATOM_EXPR && atom_has_vars(source))
            return prepared_pure_reject(
                program, "open expression replaced a source value", source);
        return prepared_pure_add_node(
            program,
            (PreparedPureNode){
                .kind = PREPARED_PURE_LITERAL,
                .atom = source,
            },
            NULL, 0u, node_out);
    }
    /* Translation-time data classification says the source occurrence is
     * not a call; it does not override the dialect's value representation.
     * A closed partial/lambda/foreign value is already atomic to evaluation
     * and must not be decomposed into constructor work.  Open values continue
     * below so their variables are represented by slots. */
    if (source->kind == ATOM_EXPR && program->opaque_value &&
        program->opaque_value(source) && !atom_has_vars(source)) {
        return prepared_pure_add_node(
            program,
            (PreparedPureNode){
                .kind = PREPARED_PURE_LITERAL,
                .atom = source,
            },
            NULL, 0u, node_out);
    }
    if (source_role == CETTA_PREPARED_PURE_SOURCE_DATA) {
        if (source->kind != ATOM_EXPR)
            return prepared_pure_reject(
                program, "source data is not an expression", source);
        for (CettaExprIndex index = 0u;
             index < source->expr.len; index++) {
            if (!prepared_pure_source_child(
                    program, source_view, index))
                return prepared_pure_reject(
                    program, "source data shape does not match its view",
                    source);
        }
        uint32_t *children = NULL;
        bool evaluate_children =
            program->call_mode == CETTA_GSLT_PURE_CALL_EAGER;
        if (!prepared_pure_compile_children(
                program, context, source->expr.elems,
                source->expr.len, source_view, 0u, depth,
                evaluate_children, false, &children))
            return false;
        bool ok = prepared_pure_add_node(
            program, (PreparedPureNode){.kind = PREPARED_PURE_BUILD},
            children, source->expr.len, node_out);
        free(children);
        return ok;
    }
    CettaPreparedPureExpressionViewState expression_view_state =
        CETTA_PREPARED_PURE_EXPRESSION_DEFAULT;
    CettaPreparedPureExpressionView expression_view = {0};
    if (source->kind == ATOM_EXPR && program->expression_view) {
        expression_view_state =
            prepared_pure_describe_expression(program, source, source_view,
                &expression_view);
        if (expression_view_state == CETTA_PREPARED_PURE_EXPRESSION_DATA) {
            if (cetta_control_operation_is_control_form(
                    prepared_pure_source_computation(program, source_view).control))
                return prepared_pure_reject(
                    program, "data view conflicts with retained computation", source);
            return prepared_pure_compile_syntax(
                program, context, source, depth + 1u, node_out);
        }
        if (expression_view_state ==
                CETTA_PREPARED_PURE_EXPRESSION_PROJECT ||
            expression_view_state ==
                CETTA_PREPARED_PURE_EXPRESSION_PROJECT_SINGLE_RESULT) {
            if (expression_view_state ==
                    CETTA_PREPARED_PURE_EXPRESSION_PROJECT_SINGLE_RESULT) {
                /* An unobserved prefix exports the answer gate across
                 * demands. An answer attempt also certifies its own reached
                 * rejection after an earlier demand read. */
                bool unobserved = !program->compile_result_demand_observed;
                bool rejected = !program->closed_program || prepared_pure_wants_answers(program);
                if (program->closed_program && (unobserved || rejected))
                    program->compile_answer_gate_refused = true;
                if (rejected)
                    return prepared_pure_reject(
                        program, "projection requires a single-result attempt",
                        source);
            }
            if (!expression_view.projected ||
                expression_view.projected == source)
                return prepared_pure_reject(
                    program, "dialect projection did not make progress",
                    source);
            const void *child_view = prepared_pure_projected_source_child(
                program, source, source_view, expression_view.projected);
            CettaPreparedPureSourceRole child_role =
                prepared_pure_source_role(program, child_view);
            switch (expression_view.projection_mode) {
            case CETTA_PREPARED_PURE_PROJECT_SOURCE:
                break;
            case CETTA_PREPARED_PURE_PROJECT_APPLICATION:
                /* A collapsed value has no argument plans. Reinterpreting
                 * its children would run values as code. The canonical
                 * dispatch retains that boundary. */
                if (child_role == CETTA_PREPARED_PURE_SOURCE_VALUE)
                    return prepared_pure_reject(
                        program, "application projection needs argument views",
                        source);
                if (child_role == CETTA_PREPARED_PURE_SOURCE_DATA)
                    child_role = CETTA_PREPARED_PURE_SOURCE_UNSPECIFIED;
                break;
            case CETTA_PREPARED_PURE_PROJECT_CODE:
            case CETTA_PREPARED_PURE_PROJECT_CODE_OVER_VALUES:
                child_view = NULL;
                child_role = CETTA_PREPARED_PURE_SOURCE_UNSPECIFIED;
                break;
            default:
                return prepared_pure_reject(
                    program, "invalid projection mode", source);
            }
            if (expression_view.projection_mode !=
                CETTA_PREPARED_PURE_PROJECT_CODE_OVER_VALUES)
                return prepared_pure_compile_eval_with_role(
                    program, context, expression_view.projected, child_view,
                    child_role,
                    depth + 1u, node_out);
            uint32_t body = 0u;
            return prepared_pure_compile_eval_with_role(
                       program, context, expression_view.projected,
                       child_view, child_role, depth + 1u, &body) &&
                prepared_pure_guard_values_not_expressions(
                    program, context, expression_view.projected, body,
                    depth + 1u, node_out);
        }
        if (expression_view_state ==
            CETTA_PREPARED_PURE_EXPRESSION_OBSERVE) {
            if (!expression_view.projected ||
                (expression_view.observation !=
                     CETTA_PREPARED_PURE_OBSERVE_IS_EXPRESSION &&
                 expression_view.observation !=
                     CETTA_PREPARED_PURE_OBSERVE_IS_VARIABLE))
                return prepared_pure_reject(
                    program, "invalid dialect value observation", source);
            /* PeTTa's is-expr observes the value of its operand. */
            uint32_t child = 0u;
            const void *operand_source = prepared_pure_projected_source_child(
                program, source, source_view, expression_view.projected);
            if (!(expression_view.projected->kind == ATOM_VAR
                      ? prepared_pure_compile_template(
                            program, context, expression_view.projected,
                            operand_source, depth + 1u, false, &child)
                      : prepared_pure_compile_eval(
                            program, context, expression_view.projected,
                            operand_source, depth + 1u, &child)))
                return false;
            return prepared_pure_add_node(
                program,
                (PreparedPureNode){
                    .kind = PREPARED_PURE_OBSERVE,
                    .auxiliary = (uint32_t)expression_view.observation,
                },
                &child, 1u, node_out);
        }
        if (expression_view_state ==
            CETTA_PREPARED_PURE_EXPRESSION_SELECT)
            return prepared_pure_compile_select(
                program, context, source, source_view, depth, node_out);
        if (expression_view_state ==
            CETTA_PREPARED_PURE_EXPRESSION_CANONICAL_ONLY) {
            return prepared_pure_reject(
                program, "dialect-owned form requires canonical evaluation",
                source);
        }
        if (expression_view_state ==
            CETTA_PREPARED_PURE_EXPRESSION_ZERO) {
            if (!prepared_pure_wants_answers(program))
                return prepared_pure_reject(
                    program,
                    "choice zero is not an ordinary prepared value",
                    source);
            return prepared_pure_add_node(
                program, (PreparedPureNode){.kind = PREPARED_PURE_ZERO},
                NULL, 0u, node_out);
        }
        if (expression_view_state == CETTA_PREPARED_PURE_EXPRESSION_DECLINE) {
            program->compile_shared_view_refused = true;
            return prepared_pure_reject(
                program, "dialect expression declines prepared evaluation", source);
        }
        if (expression_view_state !=
            CETTA_PREPARED_PURE_EXPRESSION_DEFAULT)
            return prepared_pure_reject(
                program, "invalid dialect expression view", source);
    }
    if (source->kind != ATOM_EXPR)
        return prepared_pure_compile_template(
            program, context, source, source_view,
            depth, true, node_out);
    if (source->expr.len == 0u)
        return prepared_pure_compile_template(
            program, context, source, source_view,
            depth, true, node_out);
    Atom *head_atom = source->expr.elems[0];
    if (!head_atom)
        return false;
    if (head_atom->kind != ATOM_SYMBOL) {
        /* A head the program computes or binds rather than names.  A value
         * that heads data, or an expression computing an integer, makes the
         * whole expression data over its evaluated fields.  A head bound to
         * a variable is data when its value is; otherwise the program
         * declines at run time and the call is left to the evaluator. */
        bool bound_head = head_atom->kind == ATOM_VAR;
        if (program->call_mode != CETTA_GSLT_PURE_CALL_EAGER ||
            (!bound_head &&
             !prepared_pure_integer_head(program, head_atom) &&
             (head_atom->kind != ATOM_GROUNDED ||
              head_atom->ground.gkind == GV_CAPTURE ||
              head_atom->ground.gkind == GV_FOREIGN))) {
            return false;
        }
        uint32_t *children = NULL;
        if (!prepared_pure_compile_children(
                program, context, source->expr.elems, source->expr.len,
                source_view, 0u, depth, true, false, &children))
            return false;
        bool ok = prepared_pure_add_node(
            program,
            (PreparedPureNode){
                .kind = PREPARED_PURE_BUILD,
                .auxiliary = bound_head ? PREPARED_PURE_BUILD_DATA_HEAD : 0u,
            },
            children, source->expr.len, node_out);
        free(children);
        return ok;
    }
    SymbolId head = head_atom->sym_id;
    if (program->source_view.head_admitted &&
        !program->source_view.head_admitted(program->source_view.context, program->space, head))
        return prepared_pure_reject(program, "rule policy requires canonical evaluation", source);

    CettaExprLen arity = source->expr.len - 1u;
    bool he_instructions = program->call_mode == CETTA_GSLT_PURE_CALL_EAGER &&
        eval_current_language_id &&
        eval_current_language_id() == CETTA_LANGUAGE_HE;
    /* An HE function evaluated runs its body, and what it returns is
     * evaluated. */
    if (he_instructions && !prepared_pure_wants_answers(program) &&
        head == g_builtin_syms.function && arity == 1u) {
        PreparedPureDelivery evaluated = {.kind = PREPARED_PURE_DELIVER_EVAL};
        return prepared_pure_compile_function(
            program, context, source->expr.elems[1], &evaluated, depth + 1u,
            node_out);
    }
    /* An instruction an equation answers with runs, and the atom it answers
     * is evaluated.  Where the syntax decides what it answers, it runs here,
     * at compile time. */
    if (he_instructions &&
        ((head == g_builtin_syms.chain && arity == 3u &&
          source->expr.elems[2]->kind == ATOM_VAR) ||
         (head == g_builtin_syms.unify && arity == 4u))) {
        Arena *arena = prepared_pure_syntax_arena(program);
        Atom *answered = NULL;
        PreparedPureFoldScope scope = {
            .program = program,
            .context = context,
        };
        if (arena &&
            he_minimal_fold_instruction(
                source, arena, prepared_pure_fold_metavariable, &scope,
                &answered) == HE_MINIMAL_FOLD_STATIC &&
            answered != source)
            return prepared_pure_compile_eval(
                program, context, answered, NULL, depth + 1u, node_out);
    }
    /* A chain whose computation takes a value apart or builds one binds
     * that value; its template then runs, and what it answers is
     * evaluated. */
    if (he_instructions && head == g_builtin_syms.chain && arity == 3u &&
        source->expr.elems[2]->kind == ATOM_VAR) {
        Atom *nested = source->expr.elems[1];
        SymbolId nested_head = atom_head_symbol_id(nested);
        bool value_operation =
            (nested_head == g_builtin_syms.decons_atom &&
             nested->expr.len == 2u &&
             nested->expr.elems[1]->kind == ATOM_VAR) ||
            (nested_head == g_builtin_syms.cons_atom &&
             nested->expr.len == 3u &&
             nested->expr.elems[2]->kind == ATOM_VAR);
        if (!value_operation)
            return prepared_pure_reject(
                program, "chain's computation is outside the fragment",
                nested);
        uint32_t bound = 0u;
        if (!prepared_pure_compile_eval(
                program, context, nested,
                prepared_pure_source_child(program, source_view, 1u),
                depth + 1u, &bound))
            return false;
        size_t saved_bindings = context->len;
        uint32_t pattern_index = 0u;
        if (!prepared_pure_compile_bind_pattern(
                program, context, source->expr.elems[2], &pattern_index))
            return false;
        uint32_t body = 0u;
        bool body_ok = prepared_pure_compile_eval(
            program, context, source->expr.elems[3],
            prepared_pure_source_child(program, source_view, 3u),
            depth + 1u, &body);
        context->len = saved_bindings;
        if (!body_ok)
            return false;
        uint32_t children[2] = {bound, body};
        return prepared_pure_add_node(
            program,
            (PreparedPureNode){
                .kind = PREPARED_PURE_BIND,
                .auxiliary = pattern_index,
            },
            children, 2u, node_out);
    }
    /* HE's unify with any other else: a pattern test of the operand that
     * holds a value chooses between the then branch, evaluated under the
     * pattern's bind, and the else branch. */
    uint32_t unify_value = he_instructions &&
            head == g_builtin_syms.unify && arity == 4u
        ? prepared_pure_unify_value_side(context, source) : 0u;
    Atom *unify_pattern = unify_value == 0u ? NULL
        : source->expr.elems[unify_value == 1u ? 2u : 1u];
    if (unify_value != 0u && !prepared_pure_wants_answers(program) &&
        !atom_is_symbol_id(source->expr.elems[4], g_builtin_syms.empty) &&
        !atom_is_error(source->expr.elems[4])) {
        uint32_t else_node = 0u;
        if (!prepared_pure_compile_eval(
                program, context, source->expr.elems[4],
                prepared_pure_source_child(program, source_view, 4u),
                depth + 1u, &else_node))
            return prepared_pure_reject(
                program, "unify else branch is outside the fragment",
                source->expr.elems[4]);
        uint32_t target = 0u;
        if (!prepared_pure_compile_template(
                program, context, source->expr.elems[unify_value],
                prepared_pure_source_child(program, source_view, unify_value),
                depth + 1u, true, &target))
            return prepared_pure_reject(
                program, "unify value is outside the fragment",
                source->expr.elems[unify_value]);
        size_t saved_bindings = context->len;
        uint32_t saved_next_slot = context->next_slot;
        uint32_t test_pattern = 0u;
        if (!prepared_pure_compile_bind_pattern(
                program, context, unify_pattern, &test_pattern))
            return prepared_pure_reject(
                program, "unify pattern is outside the fragment",
                unify_pattern);
        context->len = saved_bindings;
        context->next_slot = saved_next_slot;
        /* The test reads only its value: a pattern that compares against a
         * variable bound before it is left to the canonical evaluator. */
        const PreparedPureBindPattern *tested =
            &program->bind_patterns[test_pattern];
        for (uint32_t i = 0u; i < tested->var_count; i++) {
            if (program->bind_vars[tested->first_var + i].prebound)
                return prepared_pure_reject(
                    program, "unify pattern compares a bound variable",
                    unify_pattern);
        }
        uint32_t test = 0u;
        if (!prepared_pure_add_node(
                program,
                (PreparedPureNode){
                    .kind = PREPARED_PURE_OBSERVE,
                    .auxiliary = PREPARED_PURE_OBSERVE_PATTERN_TEST |
                                 test_pattern,
                },
                &target, 1u, &test))
            return false;
        uint32_t bind_pattern = 0u;
        if (!prepared_pure_compile_bind_pattern(
                program, context, unify_pattern, &bind_pattern))
            return false;
        uint32_t then_body = 0u;
        bool then_ok = prepared_pure_compile_eval(
            program, context, source->expr.elems[3],
            prepared_pure_source_child(program, source_view, 3u),
            depth + 1u, &then_body);
        context->len = saved_bindings;
        if (!then_ok)
            return prepared_pure_reject(
                program, "unify branch is outside the fragment",
                source->expr.elems[3]);
        uint32_t bound[2] = {target, then_body};
        uint32_t then_node = 0u;
        if (!prepared_pure_add_node(
                program,
                (PreparedPureNode){
                    .kind = PREPARED_PURE_BIND,
                    .auxiliary = bind_pattern,
                },
                bound, 2u, &then_node))
            return false;
        uint32_t branch[3] = {test, then_node, else_node};
        return prepared_pure_add_node(
            program, (PreparedPureNode){.kind = PREPARED_PURE_IF},
            branch, 3u, node_out);
    }
    /* HE's unify whose else is Empty or an error: a pattern bind of its
     * target as it is written, under which its then branch is evaluated; a
     * target the pattern does not match, which answers that else, is left
     * to the canonical evaluator.  It is the instruction a let reduces to,
     * whatever spells the let. */
    if (unify_value != 0u &&
        (atom_is_symbol_id(source->expr.elems[4], g_builtin_syms.empty) ||
         atom_is_error(source->expr.elems[4]))) {
        uint32_t bound = 0u;
        if (!prepared_pure_compile_template(
                program, context, source->expr.elems[unify_value],
                prepared_pure_source_child(program, source_view, unify_value),
                depth + 1u, true, &bound))
            return prepared_pure_reject(
                program, "unify value is outside the fragment",
                source->expr.elems[unify_value]);
        size_t saved_bindings = context->len;
        uint32_t pattern_index = 0u;
        if (!prepared_pure_compile_bind_pattern(
                program, context, unify_pattern, &pattern_index))
            return prepared_pure_reject(
                program, "unify pattern is outside the fragment",
                unify_pattern);
        uint32_t body = 0u;
        bool body_ok = prepared_pure_compile_eval(
            program, context, source->expr.elems[3],
            prepared_pure_source_child(program, source_view, 3u),
            depth + 1u, &body);
        context->len = saved_bindings;
        if (!body_ok)
            return prepared_pure_reject(
                program, "unify branch is outside the fragment",
                source->expr.elems[3]);
        uint32_t children[2] = {bound, body};
        return prepared_pure_add_node(
            program,
            (PreparedPureNode){
                .kind = PREPARED_PURE_BIND,
                .auxiliary = pattern_index,
            },
            children, 2u, node_out);
    }
    if (cetta_control_operation_is_control_form(control)) {
        if (control.operation == CETTA_CONTROL_FORCE ||
            control.operation == CETTA_CONTROL_RETURN_SCOPE) {
            /* A supplied force word needs the dialect's code projection,
             * handled above. Compiling its saved value as an ordinary
             * source child would erase the force/value distinction. */
            if (control.operation == CETTA_CONTROL_FORCE &&
                prepared_pure_source_computation(
                    program, source_view).control.operation != CETTA_CONTROL_NONE)
                return prepared_pure_reject(program,
                    "force requires an admitted code projection", source);
            CettaExprIndex child = (CettaExprIndex)control.source + 1u;
            return arity == 1u && control.source == 0u &&
                prepared_pure_compile_eval(
                program, context, source->expr.elems[child],
                prepared_pure_source_child(program, source_view, child),
                depth + 1u, node_out);
        }
        if (control.operation == CETTA_CONTROL_BRANCH) {
            if (!prepared_pure_control_three_places(control, arity))
                return false;
            CettaExprIndex places[] = {(CettaExprIndex)control.source + 1u,
                (CettaExprIndex)control.first + 1u,
                (CettaExprIndex)control.last + 1u};
            uint32_t children[3];
            for (size_t index = 0u; index < 3u; index++) {
                CettaExprIndex child = places[index];
                if (!prepared_pure_compile_eval(
                        program, context, source->expr.elems[child],
                        prepared_pure_source_child(program, source_view, child),
                        depth + 1u, &children[index]))
                    return false;
            }
            return prepared_pure_add_node(
                program, (PreparedPureNode){.kind = PREPARED_PURE_IF},
                children, 3u, node_out);
        }
        if (control.operation == CETTA_CONTROL_SEQUENCE) {
            if (!prepared_pure_control_three_places(control, arity))
                return false;
            CettaExprIndex producer = (CettaExprIndex)control.source + 1u;
            CettaExprIndex binder = (CettaExprIndex)control.first + 1u;
            CettaExprIndex continuation = (CettaExprIndex)control.last + 1u;
            uint32_t bound = 0u;
            if (!prepared_pure_compile_eval(
                    program, context, source->expr.elems[producer],
                    prepared_pure_source_child(
                        program, source_view, producer),
                    depth + 1u, &bound))
                return prepared_pure_reject(
                    program, "let bound expression is outside the fragment",
                    source->expr.elems[producer]);
            size_t saved_bindings = context->len;
            uint32_t pattern_index = 0u;
            if (!prepared_pure_compile_inert_bind_pattern(
                    program, context, source->expr.elems[binder],
                    prepared_pure_source_child(program, source_view, binder),
                    &pattern_index))
                return prepared_pure_reject(
                    program, "let pattern is outside the fragment",
                    source->expr.elems[binder]);
            uint32_t body = 0u;
            bool body_ok = prepared_pure_compile_eval(
                program, context, source->expr.elems[continuation],
                prepared_pure_source_child(
                    program, source_view, continuation),
                depth + 1u, &body);
            context->len = saved_bindings;
            if (!body_ok)
                return prepared_pure_reject(
                    program, "let body is outside the fragment",
                    source->expr.elems[continuation]);
            return prepared_pure_compile_source_bind(
                program, pattern_index, bound, body, node_out);
        }
        if (control.operation == CETTA_CONTROL_SEQUENCE_MANY) {
            if (arity != 2u || control.source >= arity ||
                control.last >= arity || control.source == control.last)
                return false;
            CettaExprIndex producer = (CettaExprIndex)control.source + 1u;
            CettaExprIndex continuation = (CettaExprIndex)control.last + 1u;
            Atom *pairs = source->expr.elems[producer];
            if (!pairs || pairs->kind != ATOM_EXPR)
                return prepared_pure_reject(
                    program, "let* bindings are outside the fragment", pairs);
            return prepared_pure_compile_bind_sequence(
                program, context, pairs,
                prepared_pure_source_child(program, source_view, producer), 0u,
                source->expr.elems[continuation],
                prepared_pure_source_child(program, source_view, continuation),
                depth, node_out);
        }
        return false;
    }

    CettaGsltRegisterResultKind result_kind;
    CettaGsltRegisterInstruction instruction;
    if (prepared_pure_register_program(program,
            head, arity, &result_kind, &instruction)) {
        uint32_t *children = NULL;
        if (!prepared_pure_compile_children(
                program, context, &source->expr.elems[1], arity,
                source_view, 1u, depth, true, false, &children))
            return false;
        bool ok = prepared_pure_add_node(
            program,
            (PreparedPureNode){
                .kind = PREPARED_PURE_REGISTER,
                .atom = head_atom,
                .head = head,
                .instruction = instruction,
                .result_kind = result_kind,
                .undefined_type_operands =
                    prepared_pure_undefined_type_operands(
                        program, instruction, children, arity),
            },
            children, arity, node_out);
        free(children);
        return ok;
    }

    CettaGsltPreparedPureIntrinsicInstruction intrinsic_instruction;
    if (cetta_prepared_pure_intrinsic_program(
            head, arity, &intrinsic_instruction)) {
        uint32_t *children = NULL;
        if (!prepared_pure_compile_children(
                program, context, &source->expr.elems[1], arity,
                source_view, 1u, depth, true, false, &children))
            return false;
        bool ok = prepared_pure_add_node(
            program,
            (PreparedPureNode){
                .kind = PREPARED_PURE_INTRINSIC,
                .atom = head_atom,
                .head = head,
                .intrinsic_instruction = intrinsic_instruction,
            },
            children, arity, node_out);
        free(children);
        return ok;
    }

    /* Its calls of variable heads are admitted when they run (see
     * prepared_pure_head_index). */
    bool defined = false;
    bool dynamic = false;
    CettaGsltQueryEffect effect = space_query_static_effect_for_head(
        program->space, head, &defined, &dynamic);
    if (defined) {
        /* A macro call compiles as its expansion, each call of which is
         * asked its own effect; the macro's own effect reads its operands
         * as values, which they are not. */
        bool expanded = false;
        bool expansion_ok = prepared_pure_try_inline_syntax_consumer(
            program, context, source, depth, node_out, &expanded);
        if (expanded || !expansion_ok)
            return expansion_ok;
        expansion_ok = prepared_pure_try_expand_macro(
            program, context, source, source_view, NULL, depth, node_out,
            &expanded);
        if (expanded || !expansion_ok)
            return expansion_ok;
        if (effect != CETTA_GSLT_QUERY_EFFECT_PURE)
            return prepared_pure_reject(
                program, "user call may have a relational effect", source);
        /* An unsupported typed head never joins the program. */
        uint8_t signature[66];
        if (!space_head_call_untyped(program->space, head, arity) &&
            (prepared_pure_wants_answers(program) ||
             program->call_mode != CETTA_GSLT_PURE_CALL_EAGER ||
             !prepared_pure_value_signature(
                 program->space, head, arity, signature)))
            return prepared_pure_reject(
                program, "call policy is unsupported", source);
        uint32_t head_index = 0u;
        if (!prepared_pure_head_index_admitted(
                program, head, &head_index))
            return false;
        if (!prepared_pure_head_call_policy(program, head_index, arity))
            return prepared_pure_reject(
                program, "call policy is unsupported", source);
        uint32_t *children = NULL;
        bool eager_arguments =
            program->call_mode == CETTA_GSLT_PURE_CALL_EAGER;
        if (!prepared_pure_compile_children(
                program, context, &source->expr.elems[1], arity,
                source_view, 1u, depth, eager_arguments,
                !program->allow_callable_templates,
                &children))
            return false;
        /* HE rejects a typed call whose argument's type is not the
         * parameter's before it evaluates anything; such a call is left to
         * it.  Arguments of unknown class are cast when the call runs. */
        if (program->heads[head_index].typed) {
            const uint8_t *classes = &program->value_classes[
                program->heads[head_index].first_value_class];
            for (CettaExprLen i = 0u; i < arity; i++) {
                uint8_t known = prepared_pure_node_value_class(
                    program, children[i], NULL, 0u, 0u);
                if (classes[i] != PREPARED_PURE_VALUE_UNDEFINED &&
                    known != PREPARED_PURE_VALUE_UNKNOWN &&
                    known != classes[i]) {
                    free(children);
                    return prepared_pure_reject(
                        program, "typed argument is outside its parameter's class",
                        source);
                }
            }
        }
        bool ok = prepared_pure_add_node(
            program,
            (PreparedPureNode){
                .kind = PREPARED_PURE_CALL,
                .head = head,
                .auxiliary = head_index,
                .call_arguments_are_values = eager_arguments,
            },
            children, arity, node_out);
        free(children);
        return ok;
    }
    if (source_role == CETTA_PREPARED_PURE_SOURCE_CALL)
        return prepared_pure_reject(
            program, "translated call has no live implementation", source);
    PreparedPureHeadRole head_role =
        prepared_pure_head_role(program, source);
    if (head_role == PREPARED_PURE_HEAD_CALLABLE)
        return prepared_pure_reject(
            program, "unsupported evaluator syntax", source);
    if (head_role == PREPARED_PURE_HEAD_UNKNOWN)
        return prepared_pure_reject(
            program, "dialect-owned form requires canonical evaluation",
            source);
    /* A declared signature decides which of a constructor's arguments are
     * computed: HE keeps an argument at an Atom-typed position as written,
     * inside an equation's result as anywhere else.  The typed-demand
     * discipline owns such a constructor, as it owns a typed call.  PeTTa
     * computes a constructor's callable children whatever its declaration,
     * which is exactly the eager build below. */
    if (program->call_mode == CETTA_GSLT_PURE_CALL_EAGER &&
        !(eval_current_language_id &&
          eval_current_language_id() == CETTA_LANGUAGE_PETTA) &&
        !space_head_call_untyped(program->space, head, arity))
        return prepared_pure_reject(
            program, "declared constructor signature decides argument demand",
            source);
    if (program->call_mode == CETTA_GSLT_PURE_CALL_EAGER) {
        uint32_t *children = NULL;
        if (!prepared_pure_compile_children(
                program, context, source->expr.elems, source->expr.len,
                source_view, 0u, depth, true, false, &children))
            return false;
        bool ok = prepared_pure_add_node(
            program, (PreparedPureNode){.kind = PREPARED_PURE_BUILD},
            children, source->expr.len, node_out);
        free(children);
        return ok;
    }
    return prepared_pure_compile_template(
        program, context, source, source_view,
        depth, true, node_out);
}

static bool prepared_pure_compile_eval(
    CettaPreparedPureProgram *program,
    PreparedPureCompileContext *context,
    Atom *source, const void *source_view,
    uint32_t depth, uint32_t *node_out) {
    return prepared_pure_compile_eval_with_role(
        program, context, source, source_view,
        prepared_pure_source_role(program, source_view), depth, node_out);
}

static bool prepared_pure_bind_pattern_vars(
    PreparedPureCompileContext *context, Atom *pattern, bool *repeated) {
    if (!context || !pattern || !repeated)
        return false;
    Atom **stack = NULL;
    size_t len = 0u;
    size_t cap = 0u;
    if (!prepared_pure_reserve(
            (void **)&stack, sizeof(*stack), &cap, 1u))
        return false;
    stack[len++] = pattern;
    while (len > 0u) {
        Atom *current = stack[--len];
        if (!current) {
            free(stack);
            return false;
        }
        if (current->kind == ATOM_VAR) {
            uint32_t existing = 0u;
            if (prepared_pure_context_lookup(
                    context, current->var_id, &existing)) {
                *repeated = true;
                continue;
            }
            if (!prepared_pure_context_bind(
                    context, current->var_id, false, NULL)) {
                free(stack);
                return false;
            }
            continue;
        }
        if (current->kind != ATOM_EXPR)
            continue;
        if (!prepared_pure_reserve(
                (void **)&stack, sizeof(*stack), &cap,
                len + current->expr.len)) {
            free(stack);
            return false;
        }
        for (CettaExprIndex i = 0u; i < current->expr.len; i++)
            stack[len++] = current->expr.elems[i];
    }
    free(stack);
    return true;
}

static int prepared_pure_var_slot_compare(
    const void *left_ptr, const void *right_ptr) {
    const PreparedPureVarSlot *left = left_ptr;
    const PreparedPureVarSlot *right = right_ptr;
    if (var_id_lt(left->var, right->var))
        return -1;
    if (var_id_lt(right->var, left->var))
        return 1;
    return 0;
}

static bool prepared_pure_append_pattern_vars(
    CettaPreparedPureProgram *program,
    const PreparedPureCompileContext *context,
    uint32_t *first_out, uint32_t *count_out) {
    if (!program || !context || !first_out || !count_out ||
        program->pattern_var_len > UINT32_MAX - context->len)
        return false;
    if (!prepared_pure_reserve(
            (void **)&program->pattern_vars,
            sizeof(*program->pattern_vars),
            &program->pattern_var_cap,
            program->pattern_var_len + context->len))
        return false;
    *first_out = (uint32_t)program->pattern_var_len;
    *count_out = (uint32_t)context->len;
    if (context->len > 0u) {
        PreparedPureVarSlot *destination =
            &program->pattern_vars[program->pattern_var_len];
        memcpy(destination, context->bindings,
               sizeof(*context->bindings) * context->len);
        qsort(destination, context->len, sizeof(*destination),
              prepared_pure_var_slot_compare);
    }
    program->pattern_var_len += context->len;
    return true;
}

/* The single-result machine may compile a multi-equation LHS only when every
 * same-arity pair is separated by information exposed at weak-head normal
 * form.  A variable overlaps everything.  Two expressions are separated here
 * only by outer arity or constructor head; differences below the constructor
 * would require a path-sensitive demand continuation, and overlapping equations
 * require a choicepoint machine.  Both cases therefore fall back to the
 * canonical evaluator instead of committing one result. */
static bool prepared_pure_patterns_whnf_disjoint(
    Atom *left, Atom *right) {
    if (!left || !right)
        return false;
    CettaGsltPatternKind left_kind = left->kind == ATOM_VAR
        ? CETTA_GSLT_PATTERN_VARIABLE
        : left->kind == ATOM_EXPR
          ? CETTA_GSLT_PATTERN_EXPRESSION
          : CETTA_GSLT_PATTERN_ATOM;
    CettaGsltPatternKind right_kind = right->kind == ATOM_VAR
        ? CETTA_GSLT_PATTERN_VARIABLE
        : right->kind == ATOM_EXPR
          ? CETTA_GSLT_PATTERN_EXPRESSION
          : CETTA_GSLT_PATTERN_ATOM;
    bool expressions = left->kind == ATOM_EXPR &&
                       right->kind == ATOM_EXPR;
    uint64_t left_arity = expressions ? left->expr.len : 0u;
    uint64_t right_arity = expressions ? right->expr.len : 0u;
    Atom *left_head = expressions && left->expr.len > 0u
        ? left->expr.elems[0] : NULL;
    Atom *right_head = expressions && right->expr.len > 0u
        ? right->expr.elems[0] : NULL;
    return cetta_gslt_pure_call_whnf_disjoint(
        left_kind, right_kind,
        !expressions && atom_eq(left, right),
        left_arity, right_arity,
        left_head && left_head->kind != ATOM_VAR &&
            left_head->kind != ATOM_EXPR,
        right_head && right_head->kind != ATOM_VAR &&
            right_head->kind != ATOM_EXPR,
        left_head && right_head && atom_eq(left_head, right_head));
}

static bool prepared_pure_equations_whnf_disjoint(
    const PreparedPureEquation *left, const PreparedPureEquation *right) {
    if (!left || !right)
        return false;
    if (left->arity != right->arity)
        return true;
    if (!left->lhs || !right->lhs || left->lhs->kind != ATOM_EXPR ||
        right->lhs->kind != ATOM_EXPR)
        return false;
    for (CettaExprIndex i = 0u; i < left->arity; i++) {
        if (prepared_pure_patterns_whnf_disjoint(
                left->lhs->expr.elems[i + 1u],
                right->lhs->expr.elems[i + 1u]))
            return true;
    }
    return false;
}

/* Fast common-case determinacy proof.  A single argument whose equations
 * are all distinct literals or distinct exact expression-head/arity pairs is
 * already a complete weak-head discriminator.  Proving that fact with an
 * open-addressed set avoids the old O(C^2) pairwise overlap pass for large
 * generated dispatch families; complex jointly-discriminating patterns retain
 * the exact pairwise fallback below. */
typedef struct {
    uint8_t kind;
    uint32_t expression_length;
    Atom *atom;
} PreparedPureWhnfDiscriminatorKey;

static size_t prepared_pure_whnf_discriminator_hash(
    uint8_t kind, uint32_t expression_length, Atom *atom) {
    uint64_t bits = (uint64_t)kind * UINT64_C(0x9e3779b185ebca87);
    bits ^= (uint64_t)expression_length *
        UINT64_C(0xc2b2ae3d27d4eb4f);
    bits ^= (uint64_t)atom_hash(atom) *
        UINT64_C(0x165667b19e3779f9);
    bits ^= bits >> 33u;
    bits *= UINT64_C(0xff51afd7ed558ccd);
    bits ^= bits >> 33u;
    return (size_t)bits;
}

static bool prepared_pure_whnf_discriminator_key_equal(
    const PreparedPureWhnfDiscriminatorKey *key,
    uint8_t kind, uint32_t expression_length, Atom *atom) {
    return key && key->kind == kind &&
           key->expression_length == expression_length && key->atom && atom &&
           (key->atom == atom || atom_eq(key->atom, atom));
}

static bool prepared_pure_argument_is_standalone_whnf_discriminator(
    const CettaPreparedPureProgram *program,
    const PreparedPureHead *head, uint32_t argument) {
    if (!program || !head || head->equation_count < 2u ||
        head->first_equation > program->equation_len ||
        head->equation_count > program->equation_len - head->first_equation)
        return false;
    uint32_t bucket_count = 4u;
    while (bucket_count < head->equation_count * 2u) {
        if (bucket_count > UINT32_MAX / 2u)
            return false;
        bucket_count *= 2u;
    }
    PreparedPureWhnfDiscriminatorKey *keys = calloc(
        head->equation_count, sizeof(*keys));
    uint32_t *buckets = calloc(bucket_count, sizeof(*buckets));
    if (!keys || !buckets) {
        free(keys);
        free(buckets);
        return false;
    }
    uint32_t key_count = 0u;
    bool distinct = true;
    uint32_t mask = bucket_count - 1u;
    for (uint32_t i = 0u; i < head->equation_count && distinct; i++) {
        const PreparedPureEquation *equation =
            &program->equations[head->first_equation + i];
        if (!equation->lhs || equation->lhs->kind != ATOM_EXPR ||
            argument >= equation->arity) {
            distinct = false;
            break;
        }
        Atom *pattern = equation->lhs->expr.elems[argument + 1u];
        if (!pattern || pattern->kind == ATOM_VAR) {
            distinct = false;
            break;
        }
        uint8_t kind = 1u;
        uint32_t expression_length = 0u;
        Atom *atom = pattern;
        if (pattern->kind == ATOM_EXPR) {
            expression_length = pattern->expr.len;
            Atom *pattern_head = expression_length > 0u
                ? pattern->expr.elems[0] : NULL;
            if (!pattern_head || pattern_head->kind == ATOM_VAR ||
                pattern_head->kind == ATOM_EXPR) {
                distinct = false;
                break;
            }
            kind = 2u;
            atom = pattern_head;
        }
        uint32_t bucket = (uint32_t)(
            prepared_pure_whnf_discriminator_hash(
                kind, expression_length, atom) & mask);
        bool inserted = false;
        for (uint32_t probe = 0u; probe < bucket_count; probe++) {
            uint32_t encoded = buckets[bucket];
            if (encoded == 0u) {
                if (key_count >= head->equation_count) {
                    distinct = false;
                    break;
                }
                keys[key_count] = (PreparedPureWhnfDiscriminatorKey){
                    .kind = kind,
                    .expression_length = expression_length,
                    .atom = atom,
                };
                buckets[bucket] = ++key_count;
                inserted = true;
                break;
            }
            uint32_t key_index = encoded - 1u;
            if (key_index >= key_count) {
                distinct = false;
                break;
            }
            if (prepared_pure_whnf_discriminator_key_equal(
                    &keys[key_index], kind, expression_length, atom)) {
                distinct = false;
                break;
            }
            bucket = (bucket + 1u) & mask;
        }
        if (!inserted && distinct)
            distinct = false;
    }
    free(keys);
    free(buckets);
    return distinct;
}

static bool prepared_pure_head_is_whnf_determinate(
    const CettaPreparedPureProgram *program,
    const PreparedPureHead *head) {
    if (!program || !head ||
        head->first_equation > program->equation_len ||
        head->equation_count > program->equation_len - head->first_equation)
        return false;
    if (head->equation_count > 1u) {
        uint32_t arity = program->equations[head->first_equation].arity;
        bool same_arity = arity <= 64u;
        for (uint32_t i = 1u; i < head->equation_count; i++) {
            if (program->equations[head->first_equation + i].arity != arity) {
                same_arity = false;
                break;
            }
        }
        if (same_arity) {
            for (uint32_t argument = 0u; argument < arity; argument++) {
                if (prepared_pure_argument_is_standalone_whnf_discriminator(
                        program, head, argument))
                    return true;
            }
        }
    }
    for (uint32_t i = 0u; i < head->equation_count; i++) {
        const PreparedPureEquation *left =
            &program->equations[head->first_equation + i];
        for (uint32_t j = i + 1u; j < head->equation_count; j++) {
            const PreparedPureEquation *right =
                &program->equations[head->first_equation + j];
            if (!prepared_pure_equations_whnf_disjoint(left, right))
                return false;
        }
    }
    return true;
}

static bool prepared_pure_compile_decision_group(
    CettaPreparedPureProgram *program, PreparedPureHead *head,
    uint32_t arity) {
    if (!program || !head || arity > 64u ||
        head->first_equation > program->equation_len ||
        head->equation_count > program->equation_len - head->first_equation)
        return false;

    uint32_t equation_count = 0u;
    for (uint32_t i = 0u; i < head->equation_count; i++) {
        if (program->equations[head->first_equation + i].arity == arity)
            equation_count++;
    }
    if (equation_count < PREPARED_PURE_DECISION_MIN_EQUATIONS || arity == 0u)
        return true;
    if (program->decision_len >= UINT32_MAX ||
        !prepared_pure_reserve(
            (void **)&program->decisions,
            sizeof(*program->decisions), &program->decision_cap,
            program->decision_len + 1u))
        return false;

    CettaMatchDecisionEquation *equations =
        malloc(sizeof(*equations) * equation_count);
    if (!equations)
        return false;
    uint64_t universally_constrained =
        arity == 64u
            ? UINT64_MAX
            : (UINT64_C(1) << arity) - 1u;
    uint32_t write = 0u;
    for (uint32_t i = 0u; i < head->equation_count; i++) {
        uint32_t equation_index = head->first_equation + i;
        PreparedPureEquation *equation = &program->equations[equation_index];
        if (equation->arity != arity)
            continue;
        if (!equation->lhs || equation->lhs->kind != ATOM_EXPR ||
            equation->lhs->expr.len != arity + 1u) {
            free(equations);
            return false;
        }
        equations[write++] = (CettaMatchDecisionEquation){
            .pattern = equation->lhs,
            .source_ref = equation_index,
        };
        for (uint32_t argument = 0u; argument < arity; argument++) {
            Atom *pattern = equation->lhs->expr.elems[argument + 1u];
            if (!pattern || pattern->kind == ATOM_VAR) {
                universally_constrained &=
                    ~(UINT64_C(1) << argument);
            }
        }
    }
    if (write != equation_count) {
        free(equations);
        return false;
    }

    CettaMatchDecision *selector =
        cetta_match_decision_compile_equation_projection(
        program->equation_projection,
        program->match_decision_semantics,
        equations, equation_count, CETTA_MATCH_DECISION_DEEP,
        0u, cetta_match_decision_realization_from_process(),
        NULL, NULL);
    free(equations);
    if (!selector)
        return false;

    uint32_t decision_index = (uint32_t)program->decision_len;
    program->decisions[program->decision_len++] =
        (PreparedPureDecisionProgram){
            .arity = arity,
            .equation_count = equation_count,
            .universally_constrained_arguments =
                universally_constrained,
            .selector = selector,
        };
    head = &program->heads[(uint32_t)(head - program->heads)];
    if (head->decision_count == 0u)
        head->first_decision = decision_index;
    head->decision_count++;
    return true;
}

/* The key an equation's compiled match tests at argument `position`: the
 * first op reading that argument's register compares it with a symbol, or
 * reads it as an expression whose head the next op compares with a symbol.
 * Any other first op -- a variable, a literal of another kind, a head that
 * is not a symbol -- tests no key there. */
static PreparedPureArgumentKey prepared_pure_equation_position_key(
    const CettaPreparedPureProgram *program,
    const PreparedPureEquation *equation, uint32_t position) {
    PreparedPureArgumentKey key = {.symbol = SYMBOL_ID_NONE};
    const PreparedPureMatchOp *first =
        &program->match_ops[equation->first_match_op];
    const PreparedPureMatchOp *end = first + equation->match_op_count;
    for (const PreparedPureMatchOp *op = first; op < end; op++) {
        if (op->source != position)
            continue;
        if (op->kind == PREPARED_PURE_MATCH_OP_ATOM && op->literal &&
            op->literal->kind == ATOM_SYMBOL) {
            key.symbol = op->literal->sym_id;
        } else if (op->kind == PREPARED_PURE_MATCH_OP_EXPR &&
                   op->length > 0u && op + 1 < end &&
                   op[1].kind == PREPARED_PURE_MATCH_OP_ATOM &&
                   op[1].source == op->operand && op[1].literal &&
                   op[1].literal->kind == ATOM_SYMBOL) {
            key.symbol = op[1].literal->sym_id;
            key.length = op->length;
        }
        break;
    }
    return key;
}

/* Whether every op of an equation's compiled match after its test of the
 * first argument's key `key` binds a variable met for the first time to an
 * argument, or to a field of the keyed expression. */
static bool prepared_pure_binds_after_key(
    const CettaPreparedPureProgram *program,
    const PreparedPureEquation *equation,
    const PreparedPureArgumentKey *key) {
    if (key->symbol == SYMBOL_ID_NONE)
        return false;
    const PreparedPureMatchOp *ops =
        &program->match_ops[equation->first_match_op];
    uint32_t key_ops = key->length > 0u ? 2u : 1u;
    uint32_t base = key->length > 0u ? ops[0].operand : 0u;
    if (equation->match_op_count < key_ops)
        return false;
    for (uint32_t i = key_ops; i < equation->match_op_count; i++) {
        const PreparedPureMatchOp *op = &ops[i];
        if (op->kind != PREPARED_PURE_MATCH_OP_BIND ||
            (op->source >= equation->arity &&
             (op->source < base || op->source - base >= key->length)))
            return false;
    }
    return true;
}

/* Whether two equations' keys at the first `positions` arguments tell them
 * apart: some position tests a key in both, and the keys differ
 * (KeyedEquationSelection.apart). */
static bool prepared_pure_keys_apart(
    const PreparedPureArgumentKey *left,
    const PreparedPureArgumentKey *right, uint32_t positions) {
    for (uint32_t p = 0u; p < positions; p++) {
        if (left[p].symbol != SYMBOL_ID_NONE &&
            right[p].symbol != SYMBOL_ID_NONE &&
            (left[p].symbol != right[p].symbol ||
             left[p].length != right[p].length))
            return true;
    }
    return false;
}

/* A group too small for a selector is keyed when its equations' compiled
 * matches test keys at the first few arguments and every two equations
 * test different keys at one of them.  At most one equation then admits a
 * call's arguments (KeyedEquationSelection.admitted_length_le_one), every
 * other one failing a key test, so a call runs at most that equation, and
 * that selects what the scan of every equation selects
 * (KeyedEquationSelection.select_admitted, select_not_ambiguous_of_apart;
 * MatchDecisionContract.mem_candidates_of_realizes).  The fewest positions
 * that tell every two apart are kept.  The keys come from compiled matches
 * alone, which a dialect's pattern view never reinterprets: such a pattern
 * leaves its equation uncompiled and the group unkeyed. */
static bool prepared_pure_compile_key_group(
    CettaPreparedPureProgram *program, uint32_t head_index,
    uint32_t arity) {
    const PreparedPureHead *head = &program->heads[head_index];
    uint32_t count = 0u;
    for (uint32_t i = 0u; i < head->equation_count; i++) {
        const PreparedPureEquation *equation =
            &program->equations[head->first_equation + i];
        if (equation->arity != arity)
            continue;
        if (!equation->compiled_match)
            return true;
        count++;
    }
    if (arity == 0u || count < 2u ||
        count >= PREPARED_PURE_DECISION_MIN_EQUATIONS)
        return true;
    uint32_t most = arity < PREPARED_PURE_KEY_MAX_POSITIONS
        ? arity : PREPARED_PURE_KEY_MAX_POSITIONS;
    PreparedPureArgumentKey
        keys[PREPARED_PURE_DECISION_MIN_EQUATIONS]
            [PREPARED_PURE_KEY_MAX_POSITIONS];
    uint32_t written = 0u;
    for (uint32_t i = 0u; i < head->equation_count; i++) {
        uint32_t equation_index = head->first_equation + i;
        const PreparedPureEquation *equation =
            &program->equations[equation_index];
        if (equation->arity != arity)
            continue;
        for (uint32_t p = 0u; p < most; p++) {
            keys[written][p] = prepared_pure_equation_position_key(
                program, equation, p);
            keys[written][p].equation = equation_index;
        }
        written++;
    }
    uint32_t positions = 0u;
    for (uint32_t k = 1u; k <= most && positions == 0u; k++) {
        bool apart = true;
        for (uint32_t i = 0u; apart && i < written; i++) {
            for (uint32_t j = i + 1u; apart && j < written; j++)
                apart = prepared_pure_keys_apart(keys[i], keys[j], k);
        }
        if (apart)
            positions = k;
    }
    if (positions == 0u)
        return true;
    size_t total = (size_t)written * positions;
    if (program->argument_key_len > UINT32_MAX - total ||
        !prepared_pure_reserve(
            (void **)&program->argument_keys,
            sizeof(*program->argument_keys), &program->argument_key_cap,
            program->argument_key_len + total) ||
        program->decision_len >= UINT32_MAX ||
        !prepared_pure_reserve(
            (void **)&program->decisions, sizeof(*program->decisions),
            &program->decision_cap, program->decision_len + 1u))
        return false;
    uint32_t first_key = (uint32_t)program->argument_key_len;
    uint32_t tested = 0u;
    for (uint32_t i = 0u; i < written; i++) {
        for (uint32_t p = 0u; p < positions; p++) {
            program->argument_keys[program->argument_key_len++] = keys[i][p];
            if (keys[i][p].symbol != SYMBOL_ID_NONE)
                tested |= UINT32_C(1) << p;
        }
    }
    for (uint32_t i = 0u; i < written; i++) {
        PreparedPureEquation *equation =
            &program->equations[keys[i][0].equation];
        equation->binds_after_key =
            prepared_pure_binds_after_key(program, equation, &keys[i][0]);
    }
    uint32_t decision_index = (uint32_t)program->decision_len;
    program->decisions[program->decision_len++] =
        (PreparedPureDecisionProgram){
            .arity = arity,
            .equation_count = written,
            .keyed = true,
            .first_key = first_key,
            .key_positions = positions,
            .tested_positions = tested,
        };
    PreparedPureHead *owner = &program->heads[head_index];
    if (owner->decision_count == 0u)
        owner->first_decision = decision_index;
    owner->decision_count++;
    return true;
}

static bool prepared_pure_compile_decisions_for_head(
    CettaPreparedPureProgram *program, uint32_t head_index) {
    if (!program || head_index >= program->head_len)
        return false;
    PreparedPureHead *head = &program->heads[head_index];
    for (uint32_t i = 0u; i < head->equation_count; i++) {
        uint32_t arity =
            program->equations[head->first_equation + i].arity;
        bool seen = false;
        for (uint32_t j = 0u; j < i; j++) {
            if (program->equations[head->first_equation + j].arity == arity) {
                seen = true;
                break;
            }
        }
        if (seen)
            continue;
        if (!prepared_pure_compile_key_group(program, head_index, arity))
            return false;
        /* Dialect-owned pattern views may reinterpret the outer pattern
         * shape.  Until the LanguageDef compiler emits a corresponding
         * discriminator, retain the exact generic matcher as the only
         * authority for a selector. */
        head = &program->heads[head_index];
        if (!program->pattern_view &&
            !prepared_pure_compile_decision_group(program, head, arity))
            return false;
        head = &program->heads[head_index];
    }
    head->keyed = head->decision_count == 1u &&
        program->decisions[head->first_decision].keyed;
    return true;
}

/* Mark only the result-preserving spine of an equation body.  A call reached
 * through a selected branch or let body is still in tail position; operands,
 * conditions, and bound expressions are not. */
static bool prepared_pure_mark_tail_spine(
    CettaPreparedPureProgram *program, uint32_t node_index,
    uint32_t depth) {
    if (!program || node_index >= program->node_len ||
        depth > PREPARED_PURE_MAX_COMPILE_DEPTH)
        return false;
    PreparedPureNode *node = &program->nodes[node_index];
    node->tail_position = true;
    if (node->kind == PREPARED_PURE_IF) {
        if (node->child_count != 3u)
            return false;
        return prepared_pure_mark_tail_spine(
                   program,
                   program->children[node->first_child + 1u],
                   depth + 1u) &&
               prepared_pure_mark_tail_spine(
                   program,
                   program->children[node->first_child + 2u],
                   depth + 1u);
    }
    if (node->kind == PREPARED_PURE_BIND) {
        if (node->child_count != 2u)
            return false;
        return prepared_pure_mark_tail_spine(
            program, program->children[node->first_child + 1u],
            depth + 1u);
    }
    return true;
}

static bool prepared_pure_equation_var_slot(
    const CettaPreparedPureProgram *program,
    const PreparedPureEquation *equation, VarId var,
    uint32_t *slot_out);

/* Emit the match of one pattern node read from register `source`.  Clears
 * *compilable, emitting nothing further, when a node needs the general
 * matcher: a dialect view form, or a bound exceeded. */
static bool prepared_pure_emit_match_ops(
    CettaPreparedPureProgram *program, PreparedPureEquation *equation,
    Atom *pattern, uint32_t source, uint32_t *next_register,
    bool *compilable) {
    if (!*compilable)
        return true;
    if (!pattern) {
        *compilable = false;
        return true;
    }
    PreparedPureMatchOp op = {.source = source};
    if (pattern->kind == ATOM_VAR) {
        uint32_t slot = 0u;
        if (!prepared_pure_equation_var_slot(
                program, equation, pattern->var_id, &slot) ||
            slot >= equation->body.local_count) {
            *compilable = false;
            return true;
        }
        op.kind = PREPARED_PURE_MATCH_OP_BIND;
        op.operand = slot;
        const PreparedPureMatchOp *emitted =
            &program->match_ops[equation->first_match_op];
        for (uint32_t i = 0u; i < equation->match_op_count; i++) {
            if (emitted[i].kind == PREPARED_PURE_MATCH_OP_BIND &&
                emitted[i].operand == slot) {
                op.kind = PREPARED_PURE_MATCH_OP_SAME;
                break;
            }
        }
    } else {
        if (program->pattern_view) {
            CettaPreparedPurePatternView view = {0};
            if (program->pattern_view(pattern, pattern, &view) !=
                    CETTA_PREPARED_PURE_PATTERN_VIEW_NOT_APPLICABLE) {
                *compilable = false;
                return true;
            }
        }
        if (pattern->kind == ATOM_EXPR) {
            if (pattern->expr.len >
                    PREPARED_PURE_MATCH_MAX_REGISTERS - *next_register) {
                *compilable = false;
                return true;
            }
            op.kind = PREPARED_PURE_MATCH_OP_EXPR;
            op.literal = pattern;
            op.operand = *next_register;
            op.length = pattern->expr.len;
            *next_register += pattern->expr.len;
        } else {
            op.kind = PREPARED_PURE_MATCH_OP_ATOM;
            op.literal = pattern;
        }
    }
    if (equation->match_op_count >= PREPARED_PURE_MATCH_MAX_OPS) {
        *compilable = false;
        return true;
    }
    if (!prepared_pure_reserve(
            (void **)&program->match_ops, sizeof(*program->match_ops),
            &program->match_op_cap, program->match_op_len + 1u))
        return false;
    program->match_ops[program->match_op_len++] = op;
    equation->match_op_count++;
    if (op.kind != PREPARED_PURE_MATCH_OP_EXPR)
        return true;
    for (CettaExprIndex i = 0u; i < pattern->expr.len; i++) {
        if (!prepared_pure_emit_match_ops(
                program, equation, pattern->expr.elems[i],
                op.operand + i, next_register, compilable))
            return false;
    }
    return true;
}

/* Compile the patterns of an equation that has some constructor pattern.
 * A variable's first occurrence binds its slot and each later one compares
 * with it; ops run in emission order, so every bind precedes its checks. */
static bool prepared_pure_record_match_program(
    CettaPreparedPureProgram *program, PreparedPureEquation *equation) {
    equation->compiled_match = false;
    equation->first_match_op = (uint32_t)program->match_op_len;
    equation->match_op_count = 0u;
    if (equation->arity > PREPARED_PURE_MATCH_MAX_REGISTERS ||
        program->match_op_len > UINT32_MAX - PREPARED_PURE_MATCH_MAX_OPS)
        return true;
    uint32_t next_register = equation->arity;
    bool compilable = true;
    for (uint32_t i = 0u; compilable && i < equation->arity; i++) {
        if (!prepared_pure_emit_match_ops(
                program, equation, equation->lhs->expr.elems[i + 1u], i,
                &next_register, &compilable))
            return false;
    }
    if (!compilable) {
        program->match_op_len = equation->first_match_op;
        equation->match_op_count = 0u;
        return true;
    }
    equation->match_register_count = next_register;
    equation->first_bind_op = UINT32_MAX;
    for (uint32_t i = 0u; i < equation->match_op_count; i++) {
        if (program->match_ops[equation->first_match_op + i].kind ==
                PREPARED_PURE_MATCH_OP_BIND) {
            equation->first_bind_op = i;
            break;
        }
    }
    equation->compiled_match = true;
    return true;
}

/* The child of an equation body that builds a value around one call: the
 * body is a constructor, not one whose head is bound at run time, and
 * exactly one of its children is a call, other than its head.  Its value is
 * then the call's value built into the rest of the constructor, and the
 * call can run in the caller's place while the constructor waits as a
 * recorded context (ContextStackEvaluation.withContexts_eq). */
static uint32_t prepared_pure_context_hole(
    const CettaPreparedPureProgram *program, uint32_t root) {
    if (!program || root >= program->node_len)
        return PREPARED_PURE_NO_CONTEXT;
    const PreparedPureNode *node = &program->nodes[root];
    if (node->kind != PREPARED_PURE_BUILD ||
        node->auxiliary == PREPARED_PURE_BUILD_DATA_HEAD ||
        node->child_count < 2u ||
        node->child_count > PREPARED_PURE_CONTEXT_MAX_CHILDREN)
        return PREPARED_PURE_NO_CONTEXT;
    uint32_t hole = PREPARED_PURE_NO_CONTEXT;
    for (uint32_t i = 0u; i < node->child_count; i++) {
        uint32_t child = program->children[node->first_child + i];
        if (child >= program->node_len ||
            program->nodes[child].kind != PREPARED_PURE_CALL)
            continue;
        if (i == 0u || hole != PREPARED_PURE_NO_CONTEXT)
            return PREPARED_PURE_NO_CONTEXT;
        hole = i;
    }
    return hole;
}

/* Whether node `index` is read without evaluation: a literal or a slot. */
static bool prepared_pure_node_is_read(
    const CettaPreparedPureProgram *program, uint32_t index) {
    return index < program->node_len &&
        (program->nodes[index].kind == PREPARED_PURE_LITERAL ||
         program->nodes[index].kind == PREPARED_PURE_SLOT);
}

/* Append one read of a context's plan: a literal, or a local slot. */
static bool prepared_pure_append_context_read(
    CettaPreparedPureProgram *program,
    const PreparedPureEquation *equation, uint32_t index) {
    if (!prepared_pure_node_is_read(program, index))
        return false;
    const PreparedPureNode *node = &program->nodes[index];
    PreparedPureContextRead read = {.literal = node->atom};
    if (node->kind == PREPARED_PURE_SLOT) {
        if (node->auxiliary >= equation->body.local_count)
            return false;
        read = (PreparedPureContextRead){.slot = node->auxiliary};
    } else if (!read.literal) {
        return false;
    }
    if (!prepared_pure_reserve(
            (void **)&program->context_reads,
            sizeof(*program->context_reads), &program->context_read_cap,
            program->context_read_len + 1u))
        return false;
    program->context_reads[program->context_read_len++] = read;
    return true;
}

/* The read plan of an equation's context, when recording it only reads:
 * the constructor's other children, then the call's arguments.  Leaves
 * first_context_read at PREPARED_PURE_NO_CONTEXT otherwise. */
static void prepared_pure_compile_context_reads(
    CettaPreparedPureProgram *program, PreparedPureEquation *equation) {
    equation->first_context_read = PREPARED_PURE_NO_CONTEXT;
    uint32_t hole = equation->context_hole;
    if (hole == PREPARED_PURE_NO_CONTEXT ||
        program->context_read_len >= UINT32_MAX / 2u)
        return;
    size_t first = program->context_read_len;
    const PreparedPureNode *build = &program->nodes[equation->body.root];
    uint32_t call_index = program->children[build->first_child + hole];
    for (uint32_t i = 0u; i < build->child_count; i++) {
        if (i != hole &&
            !prepared_pure_append_context_read(
                program, equation,
                program->children[build->first_child + i])) {
            program->context_read_len = first;
            return;
        }
    }
    const PreparedPureNode *call = &program->nodes[call_index];
    for (uint32_t i = 0u; i < call->child_count; i++) {
        if (!prepared_pure_append_context_read(
                program, equation,
                program->children[call->first_child + i])) {
            program->context_read_len = first;
            return;
        }
    }
    equation->first_context_read = (uint32_t)first;
}

/* Record the argument-to-slot map of an equation whose argument patterns are
 * all variables; matching it binds each argument to its slot, or compares it
 * with a repeated variable's first value.  Other equations compile a match
 * program when their patterns allow it, and keep the general matcher
 * otherwise. */
static bool prepared_pure_record_argument_slots(
    CettaPreparedPureProgram *program, PreparedPureEquation *equation) {
    equation->variable_arguments = false;
    for (uint32_t i = 0u; i < equation->arity; i++) {
        Atom *pattern = equation->lhs->expr.elems[i + 1u];
        if (!pattern || pattern->kind != ATOM_VAR)
            return prepared_pure_record_match_program(program, equation);
    }
    if (program->argument_slot_len > UINT32_MAX - equation->arity ||
        !prepared_pure_reserve(
            (void **)&program->argument_slots,
            sizeof(*program->argument_slots), &program->argument_slot_cap,
            program->argument_slot_len + equation->arity))
        return false;
    size_t first = program->argument_slot_len;
    for (uint32_t i = 0u; i < equation->arity; i++) {
        uint32_t slot = 0u;
        if (!prepared_pure_equation_var_slot(
                program, equation,
                equation->lhs->expr.elems[i + 1u]->var_id, &slot) ||
            slot >= equation->body.local_count)
            return true;
        program->argument_slots[first + i] = slot;
    }
    program->argument_slot_len += equation->arity;
    equation->first_argument_slot = (uint32_t)first;
    equation->variable_arguments = true;
    return true;
}

static bool prepared_pure_compile_head(
    CettaPreparedPureProgram *program, uint32_t head_index) {
    if (!program || head_index >= program->head_len)
        return false;
    PreparedPureHead *head = &program->heads[head_index];
    if (head->compiled)
        return true;
    head->declares_type =
        space_head_declares_type(program->space, head->head);
    SpaceEquationCursor cursor;
    if (!space_equation_cursor_init(program->space, head->head, &cursor))
        return false;
    head->first_equation = (uint32_t)program->equation_len;
    size_t occurrence_ordinal = 0u;
    for (;;) {
        SpaceEquationOccurrenceId id;
        SpaceEquationCursorStep step =
            space_equation_cursor_next(&cursor, &id);
        if (step == SPACE_EQUATION_CURSOR_END)
            break;
        if (step != SPACE_EQUATION_CURSOR_ITEM ||
            program->equation_len >= PREPARED_PURE_MAX_EQUATIONS ||
            program->equation_len >= UINT32_MAX)
            return false;
        SpaceEquationOccurrence occurrence = {0};
        if (!space_equation_occurrence_resolve(id, &occurrence) ||
            !occurrence.lhs || occurrence.lhs->kind != ATOM_EXPR ||
            occurrence.lhs->expr.len == 0u ||
            !atom_is_symbol_id(
                occurrence.lhs->expr.elems[0], head->head))
            return prepared_pure_reject(
                program, "wildcard or malformed equation", occurrence.lhs);
        if (atom_structural_may_have_open_list(occurrence.lhs))
            return prepared_pure_reject(
                program, "equation head holds a list pattern with a rest",
                occurrence.lhs);

        const void *rhs_view = NULL;
        if (program->source_view.equation_rhs &&
            !program->source_view.equation_rhs(
                program->source_view.context, program->space,
                head->head, occurrence_ordinal, occurrence.id,
                occurrence.equation, &rhs_view))
            return prepared_pure_reject(
                program, "equation has no exact source view",
                occurrence.rhs);
        occurrence_ordinal++;

        PreparedPureCompileContext context = {0};
        bool pattern_ok = true;
        bool repeated_variables = false;
        for (CettaExprIndex i = 1u;
             i < occurrence.lhs->expr.len; i++) {
            if (!prepared_pure_bind_pattern_vars(
                    &context, occurrence.lhs->expr.elems[i],
                    &repeated_variables)) {
                pattern_ok = false;
                break;
            }
        }
        /* Occurrences of a repeated variable compare values.  An eager
         * closed program forces every argument before it matches, so its
         * arguments are values; elsewhere one may still be unevaluated. */
        if (repeated_variables &&
            (!program->closed_program ||
             program->call_mode != CETTA_GSLT_PURE_CALL_EAGER))
            pattern_ok = false;
        uint32_t first_pattern_var = 0u;
        uint32_t pattern_var_count = 0u;
        if (!pattern_ok || !prepared_pure_append_pattern_vars(
                program, &context,
                &first_pattern_var, &pattern_var_count)) {
            free(context.bindings);
            return prepared_pure_reject(
                program,
                "oversized equation pattern, or a repeated variable "
                "matched before its arguments are values",
                occurrence.lhs);
        }
        uint32_t root = 0u;
        uint32_t scalar_guard = PREPARED_PURE_NO_SCALAR_GUARD;
        PreparedPureGuardedEquationState guarded =
            prepared_pure_compile_guarded_equation(
                program, &context, occurrence.lhs, occurrence.rhs,
                rhs_view,
                0u, &root, &scalar_guard);
        bool rhs_ok = guarded == PREPARED_PURE_GUARDED_EQUATION_READY;
        if (guarded == PREPARED_PURE_GUARDED_EQUATION_NOT_APPLICABLE) {
            program->compile_shared_view_refused = false;
            program->compile_answer_gate_refused = false;
            rhs_ok = prepared_pure_compile_eval(
                program, &context, occurrence.rhs, rhs_view,
                0u, &root);
        }
        if (rhs_ok) {
            program->compile_shared_view_refused = false;
            rhs_ok = prepared_pure_mark_tail_spine(
                program, root, 0u);
        }
        /* HE evaluates a typed head's result with its range as the expected
         * type.  The machine admits a typed head whose every body is shown
         * to be of its range's class; the parameters of value classes were
         * cast when the call entered. */
        head = &program->heads[head_index];
        if (rhs_ok && head->typed &&
            head->range_class != PREPARED_PURE_VALUE_UNDEFINED) {
            uint8_t slot_classes[PREPARED_PURE_MAX_SLOTS];
            uint32_t slot_count = context.next_slot < PREPARED_PURE_MAX_SLOTS
                ? context.next_slot : PREPARED_PURE_MAX_SLOTS;
            memset(slot_classes, PREPARED_PURE_VALUE_UNKNOWN, slot_count);
            for (CettaExprIndex i = 1u; i < occurrence.lhs->expr.len &&
                 i - 1u < head->typed_arity; i++) {
                Atom *parameter = occurrence.lhs->expr.elems[i];
                uint32_t slot = 0u;
                if (parameter->kind == ATOM_VAR &&
                    prepared_pure_context_lookup(
                        &context, parameter->var_id, &slot) &&
                    slot < slot_count)
                    slot_classes[slot] = program->value_classes[
                        head->first_value_class + i - 1u];
            }
            rhs_ok = prepared_pure_node_value_class(
                program, root, slot_classes, slot_count, 0u) ==
                head->range_class;
            if (!rhs_ok) {
                free(context.bindings);
                return prepared_pure_reject(
                    program, "typed result is not shown to be of its range",
                    occurrence.rhs);
            }
        }
        if (!rhs_ok || context.next_slot > PREPARED_PURE_MAX_SLOTS ||
            !prepared_pure_reserve(
                (void **)&program->equations,
                sizeof(*program->equations), &program->equation_cap,
                program->equation_len + 1u)) {
            free(context.bindings);
            return prepared_pure_reject(
                program, "equation body is outside the pure machine fragment",
                occurrence.rhs);
        }
        program->equations[program->equation_len++] = (PreparedPureEquation){
            .lhs = occurrence.lhs,
            .equation = occurrence.equation,
            .logical_index = occurrence.id.logical_index,
            .arity = occurrence.lhs->expr.len - 1u,
            .body = {.root = root, .local_count = context.next_slot},
            .first_pattern_var = first_pattern_var,
            .pattern_var_count = pattern_var_count,
            .scalar_guard = scalar_guard,
            .repeated_variables = repeated_variables,
            .context_hole = prepared_pure_context_hole(program, root),
        };
        prepared_pure_compile_context_reads(
            program, &program->equations[program->equation_len - 1u]);
        free(context.bindings);
        if (!prepared_pure_record_argument_slots(
                program, &program->equations[program->equation_len - 1u]))
            return prepared_pure_reject(
                program, "cannot record equation argument slots",
                occurrence.lhs);
        head = &program->heads[head_index];
        head->equation_count++;
    }
    head = &program->heads[head_index];
    if (head->equation_count == 0u ||
        !space_program_token_is_current(program->source_program))
        return prepared_pure_reject(
            program, "empty or invalidated user head", NULL);
    bool has_scalar_guard = false;
    for (uint32_t i = 0u; i < head->equation_count; i++) {
        if (program->equations[head->first_equation + i].scalar_guard !=
                PREPARED_PURE_NO_SCALAR_GUARD) {
            has_scalar_guard = true;
            break;
        }
    }
    if (!prepared_pure_wants_answers(program) &&
        !prepared_pure_head_is_whnf_determinate(program, head) &&
        !has_scalar_guard)
        return prepared_pure_reject(
            program,
            "head is not determinate from weak-head patterns", NULL);
    if (!prepared_pure_compile_decisions_for_head(program, head_index))
        return prepared_pure_reject(
            program, "failed to compile shared match decision program",
            NULL);
    head = &program->heads[head_index];
    head->compiled = true;
    return true;
}

/* PeTTa unifies a let's pattern with its value's output before the value's
 * goals run (SWI-PeTTa writes `Pv = V` ahead of them).  The pattern is the
 * destination of the value, and it meets every output skeleton the value
 * reaches before the calls beneath that skeleton run: a constructor's
 * fields as written, the values of variables, an if branch's output once
 * its condition is computed, a let body's output ahead of the let's value,
 * and each called equation's output in its clause head.  A clash prunes the
 * calls beneath the skeleton.  The prepared machine computes the value
 * first.  The two agree when the destination certainly meets each skeleton
 * it reaches, so that it prunes nothing, or when what a clash would prune
 * makes no call.  A source bind the constructor left to this check
 * (prepared_pure_compile_source_bind) holds a pattern without repeated
 * variables; its program is refused unless the check succeeds once its
 * callees compile. */

typedef struct {
    uint32_t head;
    const Atom *pattern;
    bool certain;
} PreparedPureDestinationVisit;

typedef struct {
    const PreparedPureBindPattern *bind;
    PreparedPureDestinationVisit *items;
    size_t len;
    size_t cap;
} PreparedPureDestinationCheck;

/* Whether some variable occurs more than once in `pattern`; a pattern too
 * large to read here counts as one that does. */
static bool prepared_pure_pattern_repeats_variable(const Atom *pattern) {
    enum { PREPARED_PURE_PATTERN_READ = 64u };
    const Atom *seen[PREPARED_PURE_PATTERN_READ];
    const Atom *stack[PREPARED_PURE_PATTERN_READ];
    uint32_t seen_len = 0u;
    uint32_t length = 0u;
    stack[length++] = pattern;
    while (length) {
        const Atom *atom = stack[--length];
        if (atom->kind == ATOM_VAR) {
            for (uint32_t i = 0u; i < seen_len; i++) {
                if (var_id_eq(seen[i]->var_id, atom->var_id))
                    return true;
            }
            if (seen_len == PREPARED_PURE_PATTERN_READ)
                return true;
            seen[seen_len++] = atom;
            continue;
        }
        if (atom->kind != ATOM_EXPR)
            continue;
        for (CettaExprIndex i = 0u; i < atom->expr.len; i++) {
            if (length == PREPARED_PURE_PATTERN_READ)
                return true;
            stack[length++] = atom->expr.elems[i];
        }
    }
    return false;
}

/* Whether a variable of the checked pattern receives what it faces: the
 * pattern binds it, and no enclosing binding gave it a value. */
static bool prepared_pure_destination_receives(
    const CettaPreparedPureProgram *program,
    const PreparedPureDestinationCheck *check, const Atom *variable) {
    const PreparedPureBindPattern *bind = check->bind;
    for (uint32_t i = 0u; i < bind->var_count; i++) {
        const PreparedPureBindVar *var =
            &program->bind_vars[bind->first_var + i];
        if (var_id_eq(var->var, variable->var_id))
            return !var->prebound;
    }
    return false;
}

/* Whether `pattern` matches the ground atom `value`, binding only receiving
 * variables. */
static bool prepared_pure_pattern_certainly_matches(
    const CettaPreparedPureProgram *program,
    const PreparedPureDestinationCheck *check, const Atom *pattern,
    Atom *value, uint32_t depth) {
    if (depth > PREPARED_PURE_MAX_COMPILE_DEPTH)
        return false;
    if (pattern->kind == ATOM_VAR)
        return prepared_pure_destination_receives(program, check, pattern);
    if (pattern->kind != ATOM_EXPR || value->kind != ATOM_EXPR)
        return atom_eq((Atom *)pattern, value);
    if (pattern->expr.len != value->expr.len)
        return false;
    for (CettaExprIndex i = 0u; i < pattern->expr.len; i++) {
        if (!prepared_pure_pattern_certainly_matches(
                program, check, pattern->expr.elems[i], value->expr.elems[i],
                depth + 1u))
            return false;
    }
    return true;
}

static bool prepared_pure_value_certainly_meets(
    CettaPreparedPureProgram *program, PreparedPureDestinationCheck *check,
    uint32_t node_index, const Atom *pattern, uint32_t depth);
static bool prepared_pure_value_admits_destination(
    CettaPreparedPureProgram *program, PreparedPureDestinationCheck *check,
    uint32_t node_index, const Atom *pattern, uint32_t depth);

/* Whether each equation of head `head_index`, entered with destination
 * `pattern`, certainly meets it (`certain`) or admits it.  A head met again
 * with the same destination is taken to: every equation reached is judged
 * once, and the judgments only combine by conjunction. */
static bool prepared_pure_call_destination(
    CettaPreparedPureProgram *program, PreparedPureDestinationCheck *check,
    uint32_t head_index, const Atom *pattern, bool certain, uint32_t depth) {
    if (head_index >= program->head_len ||
        !program->heads[head_index].compiled)
        return false;
    for (size_t i = 0u; i < check->len; i++) {
        if (check->items[i].head == head_index &&
            check->items[i].pattern == pattern &&
            check->items[i].certain == certain)
            return true;
    }
    if (!prepared_pure_reserve(
            (void **)&check->items, sizeof(*check->items), &check->cap,
            check->len + 1u))
        return false;
    check->items[check->len++] = (PreparedPureDestinationVisit){
        .head = head_index, .pattern = pattern, .certain = certain,
    };
    const PreparedPureHead *head = &program->heads[head_index];
    for (uint32_t i = 0u; i < head->equation_count; i++) {
        uint32_t equation = head->first_equation + i;
        if (equation >= program->equation_len)
            return false;
        uint32_t root = program->equations[equation].body.root;
        if (!(certain
                ? prepared_pure_value_certainly_meets(
                      program, check, root, pattern, depth + 1u)
                : prepared_pure_value_admits_destination(
                      program, check, root, pattern, depth + 1u)))
            return false;
    }
    return true;
}

/* Whether the destination `pattern` certainly meets the value of
 * `node_index`: each constructor it reaches spells the pattern's head at
 * the pattern's length, each literal matches its part, and every other
 * value faces a receiving variable.  Meeting it then prunes nothing. */
static bool prepared_pure_value_certainly_meets(
    CettaPreparedPureProgram *program, PreparedPureDestinationCheck *check,
    uint32_t node_index, const Atom *pattern, uint32_t depth) {
    if (node_index >= program->node_len ||
        depth > PREPARED_PURE_MAX_COMPILE_DEPTH)
        return false;
    if (pattern->kind == ATOM_VAR)
        return prepared_pure_destination_receives(program, check, pattern);
    const PreparedPureNode *node = &program->nodes[node_index];
    const uint32_t *children = node->child_count &&
            node->first_child < program->child_len
        ? &program->children[node->first_child] : NULL;
    switch (node->kind) {
    case PREPARED_PURE_LITERAL:
        return node->atom && !atom_has_vars(node->atom) &&
            prepared_pure_pattern_certainly_matches(
                program, check, pattern, node->atom, depth + 1u);
    case PREPARED_PURE_IF:
        return node->child_count == 3u && children &&
            prepared_pure_value_certainly_meets(
                program, check, children[1], pattern, depth + 1u) &&
            prepared_pure_value_certainly_meets(
                program, check, children[2], pattern, depth + 1u);
    case PREPARED_PURE_BIND:
        /* The body's output meets the destination before the bound value's
         * goals run. */
        return node->child_count == 2u && children &&
            prepared_pure_value_certainly_meets(
                program, check, children[1], pattern, depth + 1u);
    case PREPARED_PURE_CALL:
        return prepared_pure_call_destination(
            program, check, node->auxiliary, pattern, true, depth + 1u);
    case PREPARED_PURE_BUILD: {
        const PreparedPureNode *head =
            children && children[0] < program->node_len
            ? &program->nodes[children[0]] : NULL;
        if (!head || pattern->kind != ATOM_EXPR ||
            atom_is_list_form(pattern) ||
            pattern->expr.len != node->child_count ||
            head->kind != PREPARED_PURE_LITERAL || !head->atom ||
            atom_has_vars(head->atom) ||
            !prepared_pure_pattern_certainly_matches(
                program, check, pattern->expr.elems[0], head->atom,
                depth + 1u))
            return false;
        for (uint32_t i = 1u; i < node->child_count; i++) {
            if (!prepared_pure_value_certainly_meets(
                    program, check, children[i], pattern->expr.elems[i],
                    depth + 1u))
                return false;
        }
        return true;
    }
    default:
        return false;
    }
}

/* Whether the value of `node_index` may be computed before it meets the
 * destination `pattern`: the destination certainly meets it, or the value
 * makes no call a clash could prune (a value tree, or closed finite work,
 * as prepared_pure_compile_source_bind admits at once).  Each branch of an
 * if and each equation of a call is judged on its own, since a clash in
 * one prunes only its own calls. */
static bool prepared_pure_value_admits_destination(
    CettaPreparedPureProgram *program, PreparedPureDestinationCheck *check,
    uint32_t node_index, const Atom *pattern, uint32_t depth) {
    if (node_index >= program->node_len ||
        depth > PREPARED_PURE_MAX_COMPILE_DEPTH)
        return false;
    if ((pattern->kind == ATOM_VAR &&
         prepared_pure_destination_receives(program, check, pattern)) ||
        prepared_pure_value_tree(program, node_index, 0u) ||
        prepared_pure_finite_value_work(program, node_index, 0u))
        return true;
    const PreparedPureNode *node = &program->nodes[node_index];
    const uint32_t *children = node->child_count &&
            node->first_child < program->child_len
        ? &program->children[node->first_child] : NULL;
    switch (node->kind) {
    case PREPARED_PURE_IF:
        return node->child_count == 3u && children &&
            prepared_pure_value_admits_destination(
                program, check, children[1], pattern, depth + 1u) &&
            prepared_pure_value_admits_destination(
                program, check, children[2], pattern, depth + 1u);
    case PREPARED_PURE_CALL:
        return prepared_pure_call_destination(
            program, check, node->auxiliary, pattern, false, depth + 1u);
    default:
        return prepared_pure_value_certainly_meets(
            program, check, node_index, pattern, depth);
    }
}

/* Whether every bind compiled since the last check that awaits destination
 * evidence has it. */
static bool prepared_pure_binds_meet_destinations(
    CettaPreparedPureProgram *program) {
    bool meets = true;
    PreparedPureDestinationCheck check = {0};
    for (uint32_t i = program->checked_bind_nodes;
         meets && i < program->node_len; i++) {
        const PreparedPureNode *node = &program->nodes[i];
        if (node->kind != PREPARED_PURE_BIND || node->child_count != 2u ||
            node->first_child >= program->child_len ||
            node->auxiliary >= program->bind_pattern_len ||
            !program->bind_patterns[node->auxiliary].destination_evidence)
            continue;
        check.bind = &program->bind_patterns[node->auxiliary];
        check.len = 0u;
        meets = prepared_pure_value_admits_destination(
            program, &check, program->children[node->first_child],
            check.bind->pattern, 0u);
        if (!meets)
            prepared_pure_reject(
                program, "producer needs its caller's result destination",
                check.bind->pattern);
    }
    free(check.items);
    if (meets)
        program->checked_bind_nodes = program->node_len;
    return meets;
}

static bool prepared_pure_compile_pending_heads(
    CettaPreparedPureProgram *program) {
    if (!program)
        return false;
    for (uint32_t i = 0u; i < program->head_len; i++) {
        if (!program->heads[i].compiled &&
            !prepared_pure_compile_head(program, i))
            return false;
    }
    return space_program_token_is_current(program->source_program) &&
        prepared_pure_binds_meet_destinations(program);
}

static bool prepared_pure_runtime_head_index(
    CettaPreparedPureProgram *program, SymbolId head,
    uint32_t *head_index) {
    return program && program->closed_program && head_index &&
           prepared_pure_head_index(program, head, head_index) &&
           prepared_pure_compile_pending_heads(program);
}

static bool prepared_pure_push_frame(
    CettaPreparedPureProgram *program, uint32_t node,
    uint32_t local_base) {
    if (!program || node >= program->node_len ||
        !prepared_pure_reserve(
            (void **)&program->frames, sizeof(*program->frames),
            &program->frame_cap, program->frame_len + 1u) ||
        !prepared_pure_reserve(
            (void **)&program->frame_atoms, sizeof(*program->frame_atoms),
            &program->frame_atom_cap, program->frame_len + 1u))
        return false;
    size_t frame_index = program->frame_len++;
    program->frames[frame_index] = (PreparedPureFrame){
        .node = node,
        .local_base = local_base,
        .memo_index = PREPARED_PURE_NO_MEMO,
    };
    program->frame_atoms[frame_index] = NULL;
    return true;
}

static bool prepared_pure_push_runtime_frame(
    CettaPreparedPureProgram *program, Atom *atom) {
    if (!program || !program->closed_program || !atom ||
        !prepared_pure_reserve(
            (void **)&program->frames, sizeof(*program->frames),
            &program->frame_cap, program->frame_len + 1u) ||
        !prepared_pure_reserve(
            (void **)&program->frame_atoms, sizeof(*program->frame_atoms),
            &program->frame_atom_cap, program->frame_len + 1u))
        return false;
    size_t frame_index = program->frame_len++;
    program->frames[frame_index] = (PreparedPureFrame){
        .node = PREPARED_PURE_RUNTIME_NODE,
        .memo_index = PREPARED_PURE_NO_MEMO,
    };
    program->frame_atoms[frame_index] = atom;
    return true;
}

static bool PREPARED_PURE_HOT prepared_pure_push_value(
    CettaPreparedPureProgram *program, Atom *value) {
    if (!program || !value ||
        !prepared_pure_reserve(
            (void **)&program->values, sizeof(*program->values),
            &program->value_cap, program->value_len + 1u))
        return false;
    program->values[program->value_len++] = value;
    return true;
}

static bool prepared_pure_push_pattern_pair(
    CettaPreparedPureProgram *program, Atom *pattern, Atom *value,
    uint32_t argument) {
    if (!program || !pattern || !value ||
        !prepared_pure_reserve(
            (void **)&program->pattern_pairs,
            sizeof(*program->pattern_pairs),
            &program->pattern_pair_cap,
            program->pattern_pair_len + 1u))
        return false;
    program->pattern_pairs[program->pattern_pair_len++] =
        (PreparedPurePatternPair){pattern, value, argument};
    return true;
}

static bool prepared_pure_equation_var_slot(
    const CettaPreparedPureProgram *program,
    const PreparedPureEquation *equation, VarId var,
    uint32_t *slot_out) {
    if (!program || !equation || !slot_out)
        return false;
    uint32_t lower = 0u;
    uint32_t upper = equation->pattern_var_count;
    while (lower < upper) {
        uint32_t middle = lower + (upper - lower) / 2u;
        const PreparedPureVarSlot *entry =
            &program->pattern_vars[
                equation->first_pattern_var + middle];
        if (var_id_lt(entry->var, var)) {
            lower = middle + 1u;
        } else if (var_id_lt(var, entry->var)) {
            upper = middle;
        } else {
            *slot_out = entry->slot;
            return true;
        }
    }
    return false;
}

static bool prepared_pure_expression_is_callable(
    CettaPreparedPureProgram *program, Atom *value) {
    if (!program || !value || value->kind != ATOM_EXPR ||
        value->expr.len == 0u)
        return false;
    Atom *head = value->expr.elems[0];
    if (!head)
        return true;
    if (head->kind != ATOM_SYMBOL) {
        return head->kind != ATOM_GROUNDED ||
               head->ground.gkind == GV_CAPTURE ||
               head->ground.gkind == GV_FOREIGN;
    }
    CettaExprLen expression_arity = value->expr.len - 1u;
    if (expression_arity > UINT32_MAX)
        return true;
    uint32_t arity = (uint32_t)expression_arity;
    bool callable = false;
    if (prepared_pure_callable_cache_lookup(
            program, head->sym_id, arity, &callable))
        return callable;

    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PREPARED_PURE_CALLABLE_MISS);
    callable = prepared_pure_head_role(program, value) !=
               PREPARED_PURE_HEAD_INERT;
    /* The program is pinned to one space revision and dialect.  This cache is
     * therefore semantic data for that program, not a process-global policy
     * table.  Allocation failure only loses the optimization. */
    (void)prepared_pure_callable_cache_store(
        program, head->sym_id, arity, callable);
    return callable;
}

typedef struct {
    CettaPreparedPureProgram *program;
    Atom **slots;
    size_t capacity;
    size_t used;
} PreparedPureEscapingSuspensions;

static bool prepared_pure_escaping_suspensions_grow(
    PreparedPureEscapingSuspensions *seen) {
    if (!seen || seen->capacity > SIZE_MAX / 2u)
        return false;
    size_t next_capacity = seen->capacity ? seen->capacity * 2u : 16u;
    if (next_capacity > SIZE_MAX / sizeof(*seen->slots))
        return false;
    Atom **next = calloc(next_capacity, sizeof(*next));
    if (!next)
        return false;
    for (size_t i = 0u; i < seen->capacity; i++) {
        Atom *atom = seen->slots[i];
        if (!atom)
            continue;
        size_t index = (size_t)atom_hash(atom) & (next_capacity - 1u);
        while (next[index])
            index = (index + 1u) & (next_capacity - 1u);
        next[index] = atom;
    }
    free(seen->slots);
    seen->slots = next;
    seen->capacity = next_capacity;
    return true;
}

/* Returns false when an equivalent suspension was already present. */
static bool prepared_pure_escaping_suspensions_insert(
    PreparedPureEscapingSuspensions *seen, Atom *atom) {
    if (!seen || !atom)
        return false;
    if (seen->capacity == 0u ||
        seen->used + 1u > seen->capacity / 2u) {
        if (!prepared_pure_escaping_suspensions_grow(seen))
            return false;
    }
    size_t index = (size_t)atom_hash(atom) & (seen->capacity - 1u);
    while (seen->slots[index]) {
        if (atom_eq(seen->slots[index], atom))
            return false;
        index = (index + 1u) & (seen->capacity - 1u);
    }
    seen->slots[index] = atom;
    seen->used++;
    return true;
}

static bool prepared_pure_duplicate_callable_result_node(
    const Atom *atom, void *context) {
    PreparedPureEscapingSuspensions *seen = context;
    if (!seen || !prepared_pure_expression_is_callable(
                     seen->program, (Atom *)atom))
        return false;
    return !prepared_pure_escaping_suspensions_insert(
        seen, (Atom *)atom);
}

static bool prepared_pure_result_data_barrier(const Atom *atom, void *context) {
    CettaPreparedPureProgram *program = context;
    if (!program || !program->expression_view || !atom ||
        atom->kind != ATOM_EXPR)
        return false;
    CettaPreparedPureExpressionView view = {0};
    return program->expression_view(atom, NULL, &view) ==
        CETTA_PREPARED_PURE_EXPRESSION_DATA;
}

static bool prepared_pure_escaping_data_barrier(const Atom *atom, void *context) {
    PreparedPureEscapingSuspensions *seen = context;
    return seen && prepared_pure_result_data_barrier(atom, seen->program);
}

/* The private memo table is an execution-time implementation of Need update
 * cells.  Two equivalent raw calls cannot cross the machine boundary in its
 * place: once embedded in a returned constructor they would be two new
 * computations rather than two references to one computation.  A single
 * suspended continuation may safely return to the outer machine.  Until the
 * result ABI carries virtual suspensions, decline the ambiguous duplicated
 * representation and let the ordinary Prime machine preserve source CellId. */
static bool prepared_pure_result_has_escaping_suspension(
    CettaPreparedPureProgram *program, Atom *result) {
    if (!program || !result || !program->closed_program ||
        program->call_mode != CETTA_GSLT_PURE_CALL_CALL_BY_NEED)
        return false;
    PreparedPureEscapingSuspensions seen = {
        .program = program,
    };
    bool duplicated = atom_tree_any_pruned(result,
        prepared_pure_duplicate_callable_result_node,
        prepared_pure_escaping_data_barrier, &seen);
    free(seen.slots);
    return duplicated;
}

typedef enum {
    PREPARED_PURE_MATCH_ERROR = -1,
    PREPARED_PURE_MATCH_MISMATCH = 0,
    PREPARED_PURE_MATCH_MATCHED = 1,
    PREPARED_PURE_MATCH_NEEDS_ARGUMENT = 2,
} PreparedPureMatchState;

/* Why an equation head refused its arguments, as equation search counts a
 * failed unification attempt. */
typedef enum {
    PREPARED_PURE_MISMATCH_HEAD = 0,
    PREPARED_PURE_MISMATCH_AFTER_BIND,
    PREPARED_PURE_MISMATCH_REPEATED_VARIABLE,
} PreparedPureMismatch;

/* A list carrier under the runtime's open-cons tag is not the expression it
 * spells: a cell and a flat list can spell one list. */
static bool prepared_pure_holds_list_carrier(const Atom *value) {
    return atom_structural_may_have_list_carrier(value);
}

static bool prepared_pure_is_list_carrier(const Atom *value) {
    return __builtin_expect(petta_semantics_open_cons_built(), 0) &&
           value->kind == ATOM_EXPR && value->expr.len == 3u &&
           prepared_pure_holds_list_carrier(value) &&
           atom_is_internal_tag(value->expr.elems[0],
                                CETTA_INTERNAL_TAG_PETTA_OPEN_CONS);
}

/* Whether a list carrier's chain of cells ends in a list value or a list
 * pattern rather than in an expression, for a pattern of `length` elements.
 * A chain of more than `length` cells spells a longer list than the pattern
 * whatever it ends in, so reading stops there: the pattern's own length
 * check refuses it, and a recursion over a long chain stays linear. */
static bool prepared_pure_carrier_ends_in_list_form(
    const Atom *value, CettaExprLen length) {
    for (CettaExprLen cells = 0u; prepared_pure_is_list_carrier(value);
         cells++) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PREPARED_PURE_LIST_CARRIER_CELL_READ);
        if (cells >= length)
            return false;
        value = value->expr.elems[2];
    }
    return atom_is_list_form(value);
}

/* A carrier meets an expression pattern of `length` elements as the list it
 * spells: its first elements are the pattern's values when the list has
 * exactly that many (ListCells.unifies_cell_flat), and it differs otherwise
 * (ListCells.not_unifies_cell_nil for the empty pattern).  A tail that is not
 * a list, a partial list's included, leaves the call to the language's own
 * list matching. */
static PreparedPureMatchState prepared_pure_read_list_carrier(
    Atom *list, CettaExprLen length, Atom **elements) {
    switch (petta_semantics_read_list(list, length, elements)) {
    case PETTA_LIST_READ_EXACT:
        return PREPARED_PURE_MATCH_MATCHED;
    case PETTA_LIST_READ_OTHER_LENGTH:
        return PREPARED_PURE_MATCH_MISMATCH;
    case PETTA_LIST_READ_UNDECIDED:
        break;
    }
    return PREPARED_PURE_MATCH_ERROR;
}

/* A later occurrence of a pattern variable against the value its first
 * occurrence bound.  The canonical matcher decides a pair of ground values by
 * structural equality; a value with variables would unify instead, which
 * this matcher leaves to it.  Equal spellings are one list whatever carriers
 * they hold; unequal ones holding a carrier may still spell one list, which
 * the language's own list matching decides. */
static PreparedPureMatchState prepared_pure_match_same_value(
    Atom *bound, Atom *value) {
    if (!bound || !value || atom_has_vars(bound) || atom_has_vars(value))
        return PREPARED_PURE_MATCH_ERROR;
    if (atom_eq(bound, value) ||
        cetta_he_promoted_numbers_equal(bound, value))
        return PREPARED_PURE_MATCH_MATCHED;
    return prepared_pure_holds_list_carrier(bound) ||
            prepared_pure_holds_list_carrier(value)
        ? PREPARED_PURE_MATCH_ERROR : PREPARED_PURE_MATCH_MISMATCH;
}

static PreparedPureMatchState prepared_pure_match_mismatch(
    CettaPreparedPureProgram *program, Atom *value,
    uint32_t argument, uint64_t ready_arguments,
    uint32_t *demanded_argument) {
    if (argument < 64u &&
        (ready_arguments & (UINT64_C(1) << argument)) == 0u &&
        prepared_pure_expression_is_callable(program, value)) {
        if (demanded_argument)
            *demanded_argument = argument;
        return PREPARED_PURE_MATCH_NEEDS_ARGUMENT;
    }
    return PREPARED_PURE_MATCH_MISMATCH;
}

/* A finished equation-head match counts one unification attempt, classified
 * as equation search classifies one: success, a repeated variable's values
 * differing, or a mismatch before or after a variable was bound.  A decline
 * or a demanded argument is not an attempt; its match runs again. */
static inline PreparedPureMatchState prepared_pure_count_match(
    PreparedPureMatchState state, PreparedPureMismatch mismatch) {
    if (state == PREPARED_PURE_MATCH_MATCHED) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_UNIFICATION_ATTEMPT);
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_UNIFICATION_ATTEMPT_SUCCESS);
    } else if (state == PREPARED_PURE_MATCH_MISMATCH) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_UNIFICATION_ATTEMPT);
        cetta_runtime_stats_inc(
            mismatch == PREPARED_PURE_MISMATCH_REPEATED_VARIABLE
                ? CETTA_RUNTIME_COUNTER_UNIFICATION_ATTEMPT_REPEATED_VAR_FAIL
                : mismatch == PREPARED_PURE_MISMATCH_AFTER_BIND
                    ? CETTA_RUNTIME_COUNTER_UNIFICATION_ATTEMPT_AFTER_BIND_FAIL
                    : CETTA_RUNTIME_COUNTER_UNIFICATION_ATTEMPT_HEAD_FAIL);
    }
    return state;
}

/* Run an equation's compiled match from op `from` on, over `registers`
 * holding the arguments and whatever ops before `from` loaded, binding into
 * `locals`.  Every argument is a value, so a failed check is a mismatch. */
static PREPARED_PURE_ALWAYS_INLINE PreparedPureMatchState
prepared_pure_run_match_ops(
    CettaPreparedPureProgram *program,
    const PreparedPureEquation *equation, Atom **registers,
    uint32_t from, Atom **locals) {
    const PreparedPureMatchOp *first =
        &program->match_ops[equation->first_match_op];
    const PreparedPureMatchOp *end = first + equation->match_op_count;
    for (const PreparedPureMatchOp *op = first + from; op < end; op++) {
        Atom *value = registers[op->source];
        bool equal = true;
        switch ((PreparedPureMatchOpKind)op->kind) {
        case PREPARED_PURE_MATCH_OP_BIND:
            locals[op->operand] = value;
            break;
        case PREPARED_PURE_MATCH_OP_SAME: {
            PreparedPureMatchState same =
                prepared_pure_match_same_value(
                    locals[op->operand], value);
            if (same != PREPARED_PURE_MATCH_MATCHED)
                return prepared_pure_count_match(
                    same, PREPARED_PURE_MISMATCH_REPEATED_VARIABLE);
            break;
        }
        case PREPARED_PURE_MATCH_OP_ATOM:
            equal = value && op->literal &&
                ((value->kind == op->literal->kind &&
                  atom_eq(op->literal, value)) ||
                 cetta_he_promoted_numbers_equal(op->literal, value));
            break;
        case PREPARED_PURE_MATCH_OP_EXPR:
            equal = atom_petta_decomposition_compatible(op->literal, value) &&
                    value->kind == ATOM_EXPR && value->expr.len == op->length;
            /* A list carrier spells a list, not its three fields: it
             * is read as one where it has the pattern's length or
             * fails it. */
            if ((!equal || op->length == 3u) &&
                prepared_pure_is_list_carrier(value)) {
                PreparedPureMatchState read =
                    prepared_pure_read_list_carrier(
                        value, op->length, &registers[op->operand]);
                if (read == PREPARED_PURE_MATCH_ERROR)
                    return PREPARED_PURE_MATCH_ERROR;
                equal = read == PREPARED_PURE_MATCH_MATCHED;
                break;
            }
            /* An empty expression has no element array. */
            if (equal && op->length > 0u)
                memcpy(&registers[op->operand], value->expr.elems,
                       sizeof(*registers) * op->length);
            break;
        }
        if (!equal)
            return prepared_pure_count_match(
                PREPARED_PURE_MATCH_MISMATCH,
                (uint32_t)(op - first) > equation->first_bind_op
                    ? PREPARED_PURE_MISMATCH_AFTER_BIND
                    : PREPARED_PURE_MISMATCH_HEAD);
    }
    return prepared_pure_count_match(
        PREPARED_PURE_MATCH_MATCHED, PREPARED_PURE_MISMATCH_HEAD);
}

/* Match a call's arguments against an equation's head, binding into
 * `locals`, the equation's local_count locals, each NULL on entry. */
static PREPARED_PURE_ALWAYS_INLINE PreparedPureMatchState
prepared_pure_match_into(
    CettaPreparedPureProgram *program,
    const PreparedPureEquation *equation,
    Atom *const *arguments, uint32_t arity,
    uint64_t ready_arguments, uint32_t *demanded_argument,
    Atom **locals) {
    if (equation->variable_arguments && !equation->repeated_variables) {
        const uint32_t *slots =
            &program->argument_slots[equation->first_argument_slot];
        for (uint32_t i = 0u; i < arity; i++)
            locals[slots[i]] = arguments[i];
        return prepared_pure_count_match(
            PREPARED_PURE_MATCH_MATCHED, PREPARED_PURE_MISMATCH_HEAD);
    }
    uint64_t all_arguments = arity >= 64u
        ? UINT64_MAX : (UINT64_C(1) << arity) - UINT64_C(1);
    if (equation->variable_arguments) {
        const uint32_t *slots =
            &program->argument_slots[equation->first_argument_slot];
        /* A repeated variable compares values; see compilation. */
        if ((ready_arguments & all_arguments) != all_arguments)
            return PREPARED_PURE_MATCH_ERROR;
        for (uint32_t i = 0u; i < arity; i++) {
            Atom **slot = &locals[slots[i]];
            if (*slot) {
                PreparedPureMatchState same =
                    prepared_pure_match_same_value(*slot, arguments[i]);
                if (same != PREPARED_PURE_MATCH_MATCHED)
                    return prepared_pure_count_match(
                        same, PREPARED_PURE_MISMATCH_REPEATED_VARIABLE);
                continue;
            }
            *slot = arguments[i];
        }
        return prepared_pure_count_match(
            PREPARED_PURE_MATCH_MATCHED, PREPARED_PURE_MISMATCH_HEAD);
    }
    if (equation->compiled_match &&
        (ready_arguments & all_arguments) == all_arguments) {
        Atom *registers[PREPARED_PURE_MATCH_MAX_REGISTERS];
        for (uint32_t i = 0u; i < arity; i++)
            registers[i] = arguments[i];
        return prepared_pure_run_match_ops(
            program, equation, registers, 0u, locals);
    }
    /* A repeated variable compares values; see compilation. */
    if (equation->repeated_variables &&
        (ready_arguments & all_arguments) != all_arguments)
        return PREPARED_PURE_MATCH_ERROR;
    /* The general matcher follows its pairs in stack order; a mismatch
     * after a bind in that order fails after a variable was bound. */
    PreparedPureMismatch mismatch = PREPARED_PURE_MISMATCH_HEAD;
    program->pattern_pair_len = 0u;
    for (uint32_t i = 0u; i < arity; i++) {
        if (!prepared_pure_push_pattern_pair(
                program, equation->lhs->expr.elems[i + 1u],
                arguments[i], i))
            return PREPARED_PURE_MATCH_ERROR;
    }
    while (program->pattern_pair_len > 0u) {
        PreparedPurePatternPair pair =
            program->pattern_pairs[--program->pattern_pair_len];
        Atom *pattern = pair.pattern;
        Atom *value = pair.value;
        if (pattern->kind == ATOM_VAR) {
            uint32_t slot = 0u;
            if (!prepared_pure_equation_var_slot(
                    program, equation, pattern->var_id, &slot) ||
                slot >= equation->body.local_count)
                return PREPARED_PURE_MATCH_ERROR;
            if (equation->repeated_variables &&
                locals[slot]) {
                PreparedPureMatchState same =
                    prepared_pure_match_same_value(
                        locals[slot], value);
                if (same != PREPARED_PURE_MATCH_MATCHED)
                    return prepared_pure_count_match(
                        same, PREPARED_PURE_MISMATCH_REPEATED_VARIABLE);
                continue;
            }
            locals[slot] = value;
            mismatch = PREPARED_PURE_MISMATCH_AFTER_BIND;
            continue;
        }
        if (program->pattern_view) {
            CettaPreparedPurePatternView view = {0};
            CettaPreparedPurePatternViewState view_state =
                program->pattern_view(pattern, value, &view);
            if (view_state ==
                CETTA_PREPARED_PURE_PATTERN_VIEW_MISMATCH) {
                return prepared_pure_count_match(
                    prepared_pure_match_mismatch(
                        program, value, pair.argument, ready_arguments,
                        demanded_argument),
                    mismatch);
            }
            if (view_state ==
                CETTA_PREPARED_PURE_PATTERN_VIEW_DECOMPOSE) {
                if ((view.child_count > 0u &&
                     (!view.pattern_children || !view.value_children)))
                    return PREPARED_PURE_MATCH_ERROR;
                for (CettaExprIndex i = 0u;
                     i < view.child_count; i++) {
                    if (!prepared_pure_push_pattern_pair(
                            program, view.pattern_children[i],
                            view.value_children[i], pair.argument))
                        return PREPARED_PURE_MATCH_ERROR;
                }
                continue;
            }
            if (view_state !=
                CETTA_PREPARED_PURE_PATTERN_VIEW_NOT_APPLICABLE)
                return PREPARED_PURE_MATCH_ERROR;
        }
        if (pattern->kind == ATOM_EXPR &&
            prepared_pure_is_list_carrier(value)) {
            /* A list carrier spells a list, not its three fields.  A chain
             * ending in a list value or a list pattern spells a list of that
             * kind, and a list pattern reads a list by its own elements:
             * both are decided by the language's own list matching. */
            if (atom_is_list_form(pattern) ||
                prepared_pure_carrier_ends_in_list_form(
                    value, pattern->expr.len))
                return PREPARED_PURE_MATCH_ERROR;
            PreparedPureMatchState read = prepared_pure_read_list_carrier(
                value, pattern->expr.len, NULL);
            if (read == PREPARED_PURE_MATCH_ERROR)
                return PREPARED_PURE_MATCH_ERROR;
            if (read == PREPARED_PURE_MATCH_MISMATCH)
                return prepared_pure_count_match(
                    prepared_pure_match_mismatch(
                        program, value, pair.argument, ready_arguments,
                        demanded_argument),
                    mismatch);
            PeTTaLogicalListCursor cursor;
            petta_semantics_logical_list_cursor_init(&cursor, value);
            for (CettaExprIndex i = 0u; i < pattern->expr.len; i++) {
                Atom *item = NULL;
                if (petta_semantics_logical_list_cursor_next(
                        &cursor, &item) != PETTA_LOGICAL_LIST_ITEM ||
                    !prepared_pure_push_pattern_pair(
                        program, pattern->expr.elems[i], item,
                        pair.argument))
                    return PREPARED_PURE_MATCH_ERROR;
            }
            continue;
        }
        /* A list never meets an expression. */
        if (pattern->kind != value->kind ||
            (pattern->kind != ATOM_EXPR && !atom_eq(pattern, value)) ||
            (pattern->kind == ATOM_EXPR &&
             (pattern->expr.len != value->expr.len ||
              atom_is_list_form(pattern) != atom_is_list_form(value))))
            return prepared_pure_count_match(
                prepared_pure_match_mismatch(
                    program, value, pair.argument, ready_arguments,
                    demanded_argument),
                mismatch);
        if (pattern->kind != ATOM_EXPR)
            continue;
        for (CettaExprIndex i = 0u; i < pattern->expr.len; i++) {
            if (!prepared_pure_push_pattern_pair(
                    program, pattern->expr.elems[i],
                    value->expr.elems[i], pair.argument))
                return PREPARED_PURE_MATCH_ERROR;
        }
    }
    return prepared_pure_count_match(
        PREPARED_PURE_MATCH_MATCHED, PREPARED_PURE_MISMATCH_HEAD);
}

static PreparedPureMatchState prepared_pure_match_equation(
    CettaPreparedPureProgram *program,
    const PreparedPureEquation *equation,
    Atom *const *arguments, uint32_t arity,
    uint64_t ready_arguments, uint32_t *demanded_argument) {
    if (!program || !equation || (!arguments && arity > 0u) ||
        arity != equation->arity ||
        !prepared_pure_reserve(
            (void **)&program->match_values,
            sizeof(*program->match_values), &program->match_cap,
            equation->body.local_count))
        return PREPARED_PURE_MATCH_ERROR;
    if (equation->body.local_count > 0u)
        memset(program->match_values, 0,
               sizeof(*program->match_values) * equation->body.local_count);
    return prepared_pure_match_into(
        program, equation, arguments, arity, ready_arguments,
        demanded_argument, program->match_values);
}

typedef enum {
    PREPARED_PURE_GUARD_DECLINED = -1,
    PREPARED_PURE_GUARD_REFUTED = 0,
    PREPARED_PURE_GUARD_ACCEPTED = 1,
} PreparedPureGuardState;

/* The equation's scalar guard over its matched locals. */
static PreparedPureGuardState prepared_pure_evaluate_scalar_guard(
        CettaPreparedPureProgram *program,
        const PreparedPureEquation *equation, Atom *const *locals) {
    if (!program || !equation)
        return PREPARED_PURE_GUARD_DECLINED;
    if (equation->scalar_guard == PREPARED_PURE_NO_SCALAR_GUARD)
        return PREPARED_PURE_GUARD_ACCEPTED;
    if (equation->scalar_guard >= program->scalar_guard_len) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PREPARED_PURE_SCALAR_GUARD_DECLINED);
        return PREPARED_PURE_GUARD_DECLINED;
    }
    const PreparedPureScalarGuard *guard =
        &program->scalar_guards[equation->scalar_guard];
    if (!guard->head ||
        guard->argument_count >
            PREPARED_PURE_MAX_SCALAR_GUARD_ARGUMENTS ||
        guard->first_argument > program->scalar_guard_argument_len ||
        guard->argument_count >
            program->scalar_guard_argument_len - guard->first_argument) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PREPARED_PURE_SCALAR_GUARD_DECLINED);
        return PREPARED_PURE_GUARD_DECLINED;
    }

    Atom *arguments[PREPARED_PURE_MAX_SCALAR_GUARD_ARGUMENTS];
    for (uint32_t index = 0u; index < guard->argument_count; index++) {
        const PreparedPureScalarGuardArgument *argument =
            &program->scalar_guard_arguments[
                guard->first_argument + index];
        Atom *value = argument->literal;
        if (argument->from_slot) {
            if (argument->slot >= equation->body.local_count) {
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_PREPARED_PURE_SCALAR_GUARD_DECLINED);
                return PREPARED_PURE_GUARD_DECLINED;
            }
            value = locals[argument->slot];
        }
        if (!value || atom_has_vars(value) ||
            petta_semantics_value_contains_observable_open_cons(value)) {
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_PREPARED_PURE_SCALAR_GUARD_DECLINED);
            return PREPARED_PURE_GUARD_DECLINED;
        }
        arguments[index] = value;
    }

    bool truth = false;
    if (!grounded_try_plain_scalar_truth(
            guard->head, arguments, guard->argument_count, &truth)) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PREPARED_PURE_SCALAR_GUARD_DECLINED);
        return PREPARED_PURE_GUARD_DECLINED;
    }
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PREPARED_PURE_SCALAR_GUARD_EVALUATED);
    if (truth != guard->expected_truth) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PREPARED_PURE_SCALAR_GUARD_REFUTED);
        return PREPARED_PURE_GUARD_REFUTED;
    }
    return PREPARED_PURE_GUARD_ACCEPTED;
}

/* One non-variable pattern atom against a value, as the binder matcher
 * below decides it: the dialect view first, then structural equality.
 * False when the view decomposes, which only the general matcher follows. */
static bool prepared_pure_match_rigid_atom(
    CettaPreparedPureProgram *program, Atom *pattern, Atom *value,
    bool *equal_out) {
    if (program->pattern_view) {
        CettaPreparedPurePatternView view = {0};
        CettaPreparedPurePatternViewState view_state =
            program->pattern_view(pattern, value, &view);
        if (view_state == CETTA_PREPARED_PURE_PATTERN_VIEW_MISMATCH) {
            *equal_out = false;
            return true;
        }
        if (view_state != CETTA_PREPARED_PURE_PATTERN_VIEW_NOT_APPLICABLE)
            return false;
    }
    *equal_out = pattern->kind == value->kind && atom_eq(pattern, value);
    return true;
}

/* The flat-field case of the binder matcher below, in its order: the
 * dialect view of the whole pattern first, then the head and the fields.
 * *handled_out is false for a view the general matcher must follow. */
static PreparedPureMatchState prepared_pure_match_flat_fields(
    CettaPreparedPureProgram *program,
    const PreparedPureBindPattern *descriptor,
    uint32_t local_base, Atom *value, bool *handled_out) {
    *handled_out = true;
    Atom *pattern = descriptor->pattern;
    CettaExprLen fields = pattern->expr.len - 1u;
    Atom *const *values = NULL;
    if (program->pattern_view) {
        CettaPreparedPurePatternView view = {0};
        CettaPreparedPurePatternViewState view_state =
            program->pattern_view(pattern, value, &view);
        if (view_state == CETTA_PREPARED_PURE_PATTERN_VIEW_MISMATCH)
            return PREPARED_PURE_MATCH_MISMATCH;
        if (view_state == CETTA_PREPARED_PURE_PATTERN_VIEW_DECOMPOSE) {
            if (view.child_count != fields ||
                view.pattern_children != pattern->expr.elems + 1u ||
                !view.value_children) {
                *handled_out = false;
                return PREPARED_PURE_MATCH_ERROR;
            }
            values = view.value_children;
        } else if (view_state !=
                   CETTA_PREPARED_PURE_PATTERN_VIEW_NOT_APPLICABLE) {
            return PREPARED_PURE_MATCH_ERROR;
        }
    }
    if (!values) {
        if (value->kind != ATOM_EXPR ||
            value->expr.len != pattern->expr.len)
            return PREPARED_PURE_MATCH_MISMATCH;
        bool equal = false;
        if (!value->expr.elems[0] ||
            !prepared_pure_match_rigid_atom(
                program, pattern->expr.elems[0], value->expr.elems[0],
                &equal)) {
            *handled_out = false;
            return PREPARED_PURE_MATCH_ERROR;
        }
        if (!equal)
            return PREPARED_PURE_MATCH_MISMATCH;
        values = value->expr.elems + 1u;
    }
    for (CettaExprLen i = 0u; i < fields; i++) {
        size_t slot = (size_t)local_base +
            program->bind_vars[descriptor->first_var + i].slot;
        if (slot >= program->slot_len || !values[i])
            return PREPARED_PURE_MATCH_ERROR;
        program->slots[slot] = values[i];
    }
    return PREPARED_PURE_MATCH_MATCHED;
}

/* Match `value` against a bind pattern; with `commit`, write the pattern's
 * fresh variables into their slots, otherwise only test. */
static PreparedPureMatchState prepared_pure_match_bind_pattern_into(
    CettaPreparedPureProgram *program,
    const PreparedPureBindPattern *descriptor,
    uint32_t local_base, Atom *value, bool commit) {
    if (commit && program && descriptor && value &&
        descriptor->flat_fresh_fields) {
        bool handled = false;
        PreparedPureMatchState flat = prepared_pure_match_flat_fields(
            program, descriptor, local_base, value, &handled);
        if (handled)
            return flat;
    }
    if (!program || !descriptor || !value ||
        descriptor->first_var > program->bind_var_len ||
        descriptor->var_count >
            program->bind_var_len - descriptor->first_var ||
        !prepared_pure_reserve(
            (void **)&program->match_values,
            sizeof(*program->match_values), &program->match_cap,
            descriptor->var_count))
        return PREPARED_PURE_MATCH_ERROR;
    if (descriptor->var_count > 0u)
        memset(program->match_values, 0,
               sizeof(*program->match_values) * descriptor->var_count);

    program->pattern_pair_len = 0u;
    if (!prepared_pure_push_pattern_pair(
            program, descriptor->pattern, value, 0u))
        return PREPARED_PURE_MATCH_ERROR;
    while (program->pattern_pair_len > 0u) {
        PreparedPurePatternPair pair =
            program->pattern_pairs[--program->pattern_pair_len];
        Atom *pattern = pair.pattern;
        Atom *candidate = pair.value;
        if (pattern->kind == ATOM_VAR) {
            uint32_t index = 0u;
            if (!prepared_pure_bind_var_index(
                    program, descriptor->first_var,
                    descriptor->var_count, pattern->var_id, &index))
                return PREPARED_PURE_MATCH_ERROR;
            const PreparedPureBindVar *binding =
                &program->bind_vars[descriptor->first_var + index];
            if (binding->prebound) {
                size_t slot = (size_t)local_base + binding->slot;
                if (slot >= program->slot_len ||
                    !program->slots[slot])
                    return PREPARED_PURE_MATCH_ERROR;
                if (!atom_eq(program->slots[slot], candidate))
                    return PREPARED_PURE_MATCH_MISMATCH;
            } else if (program->match_values[index]) {
                if (!atom_eq(program->match_values[index], candidate))
                    return PREPARED_PURE_MATCH_MISMATCH;
            } else {
                program->match_values[index] = candidate;
            }
            continue;
        }
        if (program->pattern_view) {
            CettaPreparedPurePatternView view = {0};
            CettaPreparedPurePatternViewState view_state =
                program->pattern_view(pattern, candidate, &view);
            if (view_state ==
                CETTA_PREPARED_PURE_PATTERN_VIEW_MISMATCH)
                return PREPARED_PURE_MATCH_MISMATCH;
            if (view_state ==
                CETTA_PREPARED_PURE_PATTERN_VIEW_DECOMPOSE) {
                if ((view.child_count > 0u &&
                     (!view.pattern_children || !view.value_children)))
                    return PREPARED_PURE_MATCH_ERROR;
                for (CettaExprIndex i = view.child_count; i > 0u; i--) {
                    CettaExprIndex child = i - 1u;
                    if (!prepared_pure_push_pattern_pair(
                            program, view.pattern_children[child],
                            view.value_children[child], 0u))
                        return PREPARED_PURE_MATCH_ERROR;
                }
                continue;
            }
            if (view_state !=
                CETTA_PREPARED_PURE_PATTERN_VIEW_NOT_APPLICABLE)
                return PREPARED_PURE_MATCH_ERROR;
        }
        if (pattern->kind != candidate->kind)
            return PREPARED_PURE_MATCH_MISMATCH;
        if (pattern->kind != ATOM_EXPR) {
            if (!atom_eq(pattern, candidate))
                return PREPARED_PURE_MATCH_MISMATCH;
            continue;
        }
        if (pattern->expr.len != candidate->expr.len ||
            atom_is_list_form(pattern) != atom_is_list_form(candidate))
            return PREPARED_PURE_MATCH_MISMATCH;
        for (CettaExprIndex i = pattern->expr.len; i > 0u; i--) {
            CettaExprIndex child = i - 1u;
            if (!prepared_pure_push_pattern_pair(
                    program, pattern->expr.elems[child],
                    candidate->expr.elems[child], 0u))
                return PREPARED_PURE_MATCH_ERROR;
        }
    }

    if (!commit)
        return PREPARED_PURE_MATCH_MATCHED;
    for (uint32_t i = 0u; i < descriptor->var_count; i++) {
        const PreparedPureBindVar *binding =
            &program->bind_vars[descriptor->first_var + i];
        if (binding->prebound)
            continue;
        size_t slot = (size_t)local_base + binding->slot;
        if (slot >= program->slot_len || !program->match_values[i])
            return PREPARED_PURE_MATCH_ERROR;
        program->slots[slot] = program->match_values[i];
    }
    return PREPARED_PURE_MATCH_MATCHED;
}

static PreparedPureMatchState prepared_pure_match_bind_pattern(
    CettaPreparedPureProgram *program,
    const PreparedPureBindPattern *descriptor,
    uint32_t local_base, Atom *value) {
    return prepared_pure_match_bind_pattern_into(
        program, descriptor, local_base, value, true);
}

typedef enum {
    PREPARED_PURE_DECISION_ERROR = -1,
    PREPARED_PURE_DECISION_NOT_APPLICABLE = 0,
    PREPARED_PURE_DECISION_READY = 1,
    PREPARED_PURE_DECISION_NEEDS_ARGUMENT = 2,
} PreparedPureDecisionState;

static const PreparedPureDecisionProgram *
prepared_pure_decision_for_arity(
    const CettaPreparedPureProgram *program,
    const PreparedPureHead *head, uint32_t arity) {
    if (!program || !head ||
        head->first_decision > program->decision_len ||
        head->decision_count >
            program->decision_len - head->first_decision)
        return NULL;
    for (uint32_t i = 0u; i < head->decision_count; i++) {
        const PreparedPureDecisionProgram *decision =
            &program->decisions[head->first_decision + i];
        if (decision->arity == arity)
            return decision;
    }
    return NULL;
}

/* The key of an argument value, as a pattern's key test reads it: a symbol
 * (length 0), or the head and length of an expression headed by a symbol.
 * False for any other value; a list carrier's head is an internal tag,
 * never a symbol. */
static inline bool prepared_pure_value_key(
    const Atom *value, SymbolId *symbol, uint32_t *length) {
    if (atom_petta_value_representation(value) != PETTA_VALUE_ORDINARY)
        return false;
    if (!value)
        return false;
    if (value->kind == ATOM_SYMBOL) {
        *symbol = value->sym_id;
        *length = 0u;
        return true;
    }
    if (value->kind == ATOM_EXPR && value->expr.len > 0u &&
        value->expr.len <= UINT32_MAX && value->expr.elems[0] &&
        value->expr.elems[0]->kind == ATOM_SYMBOL) {
        *symbol = value->expr.elems[0]->sym_id;
        *length = (uint32_t)value->expr.len;
        return true;
    }
    return false;
}

typedef enum {
    PREPARED_PURE_KEY_NOT_APPLICABLE = 0,
    PREPARED_PURE_KEY_NONE,
    PREPARED_PURE_KEY_ADMITTED,
} PreparedPureKeyLookup;

/* The equation of a keyed group admitting the arguments: each key it tests
 * equals the argument's key there (KeyedEquationSelection.admits).  At most
 * one does, so the first is the one.  Not applicable when an argument at a
 * tested position has no key: the general matcher decides it.  The caller
 * has every argument as a value, as the compiled match's key tests need. */
static inline PreparedPureKeyLookup prepared_pure_key_lookup(
    const CettaPreparedPureProgram *program,
    const PreparedPureDecisionProgram *decision,
    Atom *const *arguments,
    const PreparedPureArgumentKey **admitted_out) {
    uint32_t positions = decision->key_positions;
    const PreparedPureArgumentKey *keys =
        &program->argument_keys[decision->first_key];
    if (positions == 1u) {
        /* Every equation tests the first argument's key. */
        SymbolId symbol = SYMBOL_ID_NONE;
        uint32_t length = 0u;
        if (!prepared_pure_value_key(arguments[0], &symbol, &length))
            return PREPARED_PURE_KEY_NOT_APPLICABLE;
        for (uint32_t i = 0u; i < decision->equation_count; i++) {
            if (keys[i].symbol == symbol && keys[i].length == length) {
                *admitted_out = &keys[i];
                return PREPARED_PURE_KEY_ADMITTED;
            }
        }
        return PREPARED_PURE_KEY_NONE;
    }
    SymbolId symbols[PREPARED_PURE_KEY_MAX_POSITIONS];
    uint32_t lengths[PREPARED_PURE_KEY_MAX_POSITIONS];
    for (uint32_t p = 0u; p < positions; p++) {
        if (!prepared_pure_value_key(arguments[p], &symbols[p],
                                     &lengths[p])) {
            if ((decision->tested_positions >> p) & 1u)
                return PREPARED_PURE_KEY_NOT_APPLICABLE;
            symbols[p] = SYMBOL_ID_NONE;
            lengths[p] = 0u;
        }
    }
    for (uint32_t i = 0u; i < decision->equation_count;
         i++, keys += positions) {
        bool admits = true;
        for (uint32_t p = 0u; admits && p < positions; p++) {
            admits = keys[p].symbol == SYMBOL_ID_NONE ||
                (keys[p].symbol == symbols[p] &&
                 keys[p].length == lengths[p]);
        }
        if (admits) {
            *admitted_out = keys;
            return PREPARED_PURE_KEY_ADMITTED;
        }
    }
    return PREPARED_PURE_KEY_NONE;
}

/* A keyed group runs the one equation admitting the arguments, or none.
 * Only while every argument is a value: the compiled match then tests the
 * keys, where the general matcher visits the arguments in another order and
 * may demand one of them first. */
static PreparedPureDecisionState prepared_pure_key_candidates(
    const CettaPreparedPureProgram *program,
    const PreparedPureDecisionProgram *decision,
    Atom *const *arguments, uint32_t arity, uint64_t ready_arguments,
    const uint32_t **candidate_refs_out, size_t *candidate_count_out) {
    uint64_t all_arguments = arity >= 64u
        ? UINT64_MAX : (UINT64_C(1) << arity) - UINT64_C(1);
    if ((ready_arguments & all_arguments) != all_arguments)
        return PREPARED_PURE_DECISION_NOT_APPLICABLE;
    const PreparedPureArgumentKey *admitted = NULL;
    PreparedPureKeyLookup lookup = prepared_pure_key_lookup(
        program, decision, arguments, &admitted);
    if (lookup == PREPARED_PURE_KEY_NOT_APPLICABLE)
        return PREPARED_PURE_DECISION_NOT_APPLICABLE;
    if (lookup == PREPARED_PURE_KEY_ADMITTED) {
        *candidate_refs_out = &admitted->equation;
        *candidate_count_out = 1u;
    }
    return PREPARED_PURE_DECISION_READY;
}

/* Shared MatchDecision performs refutation only.  Already available
 * non-callable arguments may be observed without changing Need behaviour;
 * callable arguments remain unavailable until the evaluator has forced them.
 * Direct demand is permitted only when every equation constrains the same
 * argument, preserving the exact matcher's source-order demand policy. */
static PreparedPureDecisionState prepared_pure_decision_candidates(
    CettaPreparedPureProgram *program,
    const PreparedPureHead *head,
    Atom *const *arguments, uint32_t arity,
    uint64_t ready_arguments,
    const uint32_t **candidate_refs_out,
    size_t *candidate_count_out,
    uint32_t *demanded_argument_out) {
    if (candidate_refs_out)
        *candidate_refs_out = NULL;
    if (candidate_count_out)
        *candidate_count_out = 0u;
    if (demanded_argument_out)
        *demanded_argument_out = 0u;
    if (!program || !head || !arguments || !candidate_refs_out ||
        !candidate_count_out || !demanded_argument_out)
        return PREPARED_PURE_DECISION_ERROR;

    const PreparedPureDecisionProgram *decision =
        prepared_pure_decision_for_arity(program, head, arity);
    if (decision && decision->keyed)
        return prepared_pure_key_candidates(
            program, decision, arguments, arity, ready_arguments,
            candidate_refs_out, candidate_count_out);
    if (!decision ||
        decision->equation_count < PREPARED_PURE_DECISION_MIN_EQUATIONS)
        return PREPARED_PURE_DECISION_NOT_APPLICABLE;
    if (!decision->selector)
        return PREPARED_PURE_DECISION_ERROR;

    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PREPARED_PURE_DECISION_RUN);
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_PREPARED_PURE_DECISION_EQUATION_INPUT,
        decision->equation_count);

    uint64_t observable_arguments = ready_arguments;
    for (uint32_t argument = 0u; argument < arity; argument++) {
        uint64_t bit = UINT64_C(1) << argument;
        if ((observable_arguments & bit) == 0u &&
            !prepared_pure_expression_is_callable(
                program, arguments[argument])) {
            observable_arguments |= bit;
        }
    }

    uint64_t unresolved =
        decision->universally_constrained_arguments &
        ~observable_arguments;
    while (unresolved != 0u) {
        uint32_t argument = (uint32_t)__builtin_ctzll(unresolved);
        unresolved &= unresolved - 1u;
        if (argument >= arity || !arguments[argument])
            return PREPARED_PURE_DECISION_ERROR;
        if (prepared_pure_expression_is_callable(
                program, arguments[argument])) {
            *demanded_argument_out = argument;
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_PREPARED_PURE_DECISION_DIRECT_DEMAND);
            return PREPARED_PURE_DECISION_NEEDS_ARGUMENT;
        }
    }

    if (head->first_equation >= program->equation_len)
        return PREPARED_PURE_DECISION_ERROR;
    Atom *lhs = program->equations[head->first_equation].lhs;
    if (!lhs || lhs->kind != ATOM_EXPR || lhs->expr.len == 0u)
        return PREPARED_PURE_DECISION_ERROR;
    CettaMatchDecisionSelectState selected =
        cetta_match_decision_select_parts(
            decision->selector, program->space,
            program->match_decision_semantics,
            lhs->expr.elems[0], arguments, arity,
            observable_arguments,
            candidate_refs_out, candidate_count_out);
    if (selected == CETTA_MATCH_DECISION_SELECT_INVALIDATED)
        return PREPARED_PURE_DECISION_ERROR;
    if (selected != CETTA_MATCH_DECISION_SELECT_READY)
        return PREPARED_PURE_DECISION_ERROR;

    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_PREPARED_PURE_DECISION_EQUATION_SURVIVOR,
        *candidate_count_out);
    /* An equation the decision refutes counts as the head failure equation
     * search would meet, as a shape-pruned candidate does there. */
    if (*candidate_count_out < decision->equation_count) {
        cetta_runtime_stats_add(
            CETTA_RUNTIME_COUNTER_UNIFICATION_ATTEMPT,
            decision->equation_count - *candidate_count_out);
        cetta_runtime_stats_add(
            CETTA_RUNTIME_COUNTER_UNIFICATION_ATTEMPT_HEAD_FAIL,
            decision->equation_count - *candidate_count_out);
    }
    return PREPARED_PURE_DECISION_READY;
}

typedef enum {
    PREPARED_PURE_SELECT_ERROR = -1,
    PREPARED_PURE_SELECT_NO_MATCH = 0,
    PREPARED_PURE_SELECT_SELECTED = 1,
    PREPARED_PURE_SELECT_NEEDS_ARGUMENT = 2,
    PREPARED_PURE_SELECT_AMBIGUOUS = 3,
} PreparedPureSelectState;

typedef struct {
    PreparedPureSelectState state;
    const PreparedPureEquation *equation;
    uint32_t demanded_argument;
} PreparedPureSelection;

static PreparedPureSelectState PREPARED_PURE_HOT
prepared_pure_consider_equation(
    CettaPreparedPureProgram *program,
    const PreparedPureEquation *equation,
    Atom *const *arguments, uint32_t arity,
    uint64_t ready_arguments,
    const PreparedPureEquation **selected,
    bool *needs_argument,
    uint32_t *first_demanded_argument) {
    if (!program || !equation || !arguments || !selected ||
        !needs_argument || !first_demanded_argument)
        return PREPARED_PURE_SELECT_ERROR;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PREPARED_PURE_DECISION_FULL_MATCH);
    uint32_t demanded_argument = 0u;
    PreparedPureMatchState matched = prepared_pure_match_equation(
        program, equation, arguments, arity,
        ready_arguments, &demanded_argument);
    if (matched == PREPARED_PURE_MATCH_ERROR)
        return PREPARED_PURE_SELECT_ERROR;
    if (matched == PREPARED_PURE_MATCH_NEEDS_ARGUMENT) {
        if (!*needs_argument ||
            demanded_argument < *first_demanded_argument)
            *first_demanded_argument = demanded_argument;
        *needs_argument = true;
        return PREPARED_PURE_SELECT_NO_MATCH;
    }
    if (matched != PREPARED_PURE_MATCH_MATCHED)
        return PREPARED_PURE_SELECT_NO_MATCH;
    PreparedPureGuardState guard =
        prepared_pure_evaluate_scalar_guard(
            program, equation, program->match_values);
    if (guard == PREPARED_PURE_GUARD_DECLINED)
        return PREPARED_PURE_SELECT_ERROR;
    if (guard == PREPARED_PURE_GUARD_REFUTED)
        return PREPARED_PURE_SELECT_NO_MATCH;
    if (*selected)
        return PREPARED_PURE_SELECT_AMBIGUOUS;
    if (!prepared_pure_reserve(
            (void **)&program->selected_values,
            sizeof(*program->selected_values),
            &program->selected_cap, equation->body.local_count))
        return PREPARED_PURE_SELECT_ERROR;
    if (equation->body.local_count > 0u)
        memcpy(program->selected_values, program->match_values,
               sizeof(*program->selected_values) * equation->body.local_count);
    *selected = equation;
    return PREPARED_PURE_SELECT_NO_MATCH;
}

static PreparedPureSelection prepared_pure_select_equation(
    CettaPreparedPureProgram *program, uint32_t head_index,
    Atom *const *arguments, uint32_t arity,
    uint64_t ready_arguments) {
    PreparedPureSelection result = {
        .state = PREPARED_PURE_SELECT_NO_MATCH,
    };
    if (!program || head_index >= program->head_len)
        return (PreparedPureSelection){
            .state = PREPARED_PURE_SELECT_ERROR,
        };
    const PreparedPureHead *head = &program->heads[head_index];
    if (head->checks_arguments) {
        uint64_t all_arguments = arity >= 64u
            ? UINT64_MAX : (UINT64_C(1) << arity) - UINT64_C(1);
        for (uint32_t i = 0u; i < arity; i++) {
            if ((ready_arguments & (UINT64_C(1) << i)) == 0u)
                return (PreparedPureSelection){
                    .state = PREPARED_PURE_SELECT_NEEDS_ARGUMENT,
                    .demanded_argument = i,
                };
        }
        if ((ready_arguments & all_arguments) != all_arguments ||
            !prepared_pure_typed_arguments_accepted(
                program, head_index, arguments, arity))
            return (PreparedPureSelection){
                .state = PREPARED_PURE_SELECT_ERROR,
            };
    }
    const PreparedPureEquation *selected = NULL;
    bool needs_argument = false;
    uint32_t first_demanded_argument = 0u;
    const uint32_t *decision_candidate_refs = NULL;
    size_t decision_candidate_count = 0u;
    uint32_t decision_demanded_argument = 0u;
    PreparedPureDecisionState decision_state =
        prepared_pure_decision_candidates(
            program, head, arguments, arity, ready_arguments,
            &decision_candidate_refs, &decision_candidate_count,
            &decision_demanded_argument);
    if (decision_state == PREPARED_PURE_DECISION_NEEDS_ARGUMENT)
        return (PreparedPureSelection){
            .state = PREPARED_PURE_SELECT_NEEDS_ARGUMENT,
            .demanded_argument = decision_demanded_argument,
        };
    if (decision_state == PREPARED_PURE_DECISION_ERROR)
        return (PreparedPureSelection){
            .state = PREPARED_PURE_SELECT_ERROR,
        };
    if (decision_state == PREPARED_PURE_DECISION_READY) {
        for (size_t i = 0u; i < decision_candidate_count; i++) {
            uint32_t equation_index = decision_candidate_refs[i];
            if (equation_index >= program->equation_len)
                return (PreparedPureSelection){
                    .state = PREPARED_PURE_SELECT_ERROR,
                };
            PreparedPureSelectState state =
                prepared_pure_consider_equation(
                    program, &program->equations[equation_index],
                    arguments, arity, ready_arguments,
                    &selected, &needs_argument,
                    &first_demanded_argument);
            if (state == PREPARED_PURE_SELECT_ERROR ||
                state == PREPARED_PURE_SELECT_AMBIGUOUS)
                return (PreparedPureSelection){.state = state};
        }
    } else {
        for (uint32_t i = 0u; i < head->equation_count; i++) {
            const PreparedPureEquation *equation =
                &program->equations[head->first_equation + i];
            if (equation->arity != arity)
                continue;
            PreparedPureSelectState state =
                prepared_pure_consider_equation(
                    program, equation, arguments, arity,
                    ready_arguments, &selected, &needs_argument,
                    &first_demanded_argument);
            if (state == PREPARED_PURE_SELECT_ERROR ||
                state == PREPARED_PURE_SELECT_AMBIGUOUS)
                return (PreparedPureSelection){.state = state};
        }
    }
    if (needs_argument) {
        result.state = PREPARED_PURE_SELECT_NEEDS_ARGUMENT;
        result.demanded_argument = first_demanded_argument;
        return result;
    }
    if (selected) {
        result.state = PREPARED_PURE_SELECT_SELECTED;
        result.equation = selected;
    }
    return result;
}

static bool prepared_pure_read_boolean(
        const CettaPreparedPureProgram *program, Atom *atom, bool *value) {
    if (!atom || !value)
        return false;
    if (program && program->boolean_atoms_ready && atom->kind == ATOM_SYMBOL &&
        program->boolean_atoms[0].kind == ATOM_SYMBOL) {
        if (atom->sym_id == program->boolean_atoms[0].sym_id) {
            *value = false;
            return true;
        }
        if (atom->sym_id == program->boolean_atoms[1].sym_id) {
            *value = true;
            return true;
        }
    }
    if (eval_current_language_id &&
        eval_current_language_id() == CETTA_LANGUAGE_PETTA &&
        petta_semantics_truth_value(atom, value))
        return true;
    if (atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_BOOL) {
        *value = atom->ground.bval;
        return true;
    }
    if (atom_is_symbol_id(atom, g_builtin_syms.true_text)) {
        *value = true;
        return true;
    }
    if (atom_is_symbol_id(atom, g_builtin_syms.false_text)) {
        *value = false;
        return true;
    }
    return false;
}

static bool prepared_pure_is_true(
        const CettaPreparedPureProgram *program, Atom *atom) {
    bool value;
    return prepared_pure_read_boolean(program, atom, &value) && value;
}

static bool prepared_pure_is_false(
        const CettaPreparedPureProgram *program, Atom *atom) {
    bool value;
    return prepared_pure_read_boolean(program, atom, &value) && !value;
}

/* PeTTa `if` takes the else branch for any other non-empty, non-error value.
 * A variable still belongs to the general machine, which tries both branches. */
static bool prepared_pure_petta_else_value(Atom *condition) {
    return eval_current_language_id &&
        eval_current_language_id() == CETTA_LANGUAGE_PETTA &&
        condition &&
        condition->kind != ATOM_VAR &&
        !atom_is_error(condition) &&
        !atom_is_petta_no_result(condition);
}

#if CETTA_BUILD_WITH_GMP
typedef struct {
    mpz_t storage;
    mpz_srcptr value;
    bool owns_storage;
} PreparedPureIntegerView;

static bool prepared_pure_integer_view_init(
    Atom *atom, PreparedPureIntegerView *view) {
    if (!atom || !view || atom->kind != ATOM_GROUNDED)
        return false;
    view->value = NULL;
    view->owns_storage = false;
    if (atom->ground.gkind == GV_BIGINT) {
        view->value = atom_bigint_mpz_view(atom);
        return view->value != NULL;
    }
    if (atom->ground.gkind != GV_INT)
        return false;
    mpz_init(view->storage);
    view->owns_storage = true;
    uint64_t magnitude = atom->ground.ival < 0
        ? (uint64_t)(-(atom->ground.ival + 1)) + 1u
        : (uint64_t)atom->ground.ival;
    mpz_import(
        view->storage, 1u, -1, sizeof(magnitude), 0, 0, &magnitude);
    if (atom->ground.ival < 0)
        mpz_neg(view->storage, view->storage);
    view->value = view->storage;
    return true;
}

static void prepared_pure_integer_view_clear(
    PreparedPureIntegerView *view) {
    if (view && view->owns_storage)
        mpz_clear(view->storage);
}
#endif

static bool prepared_pure_numeric_ground_kind(GroundedKind kind) {
    return kind == GV_INT || kind == GV_BIGINT ||
           kind == GV_RATIONAL || kind == GV_FLOAT;
}

/* Grounded literals carry an intrinsic HE/Prime type independent of the
 * mutable annotation space.  Proving that two operands share that intrinsic
 * type lets the generated register machine avoid a full type-service query;
 * cases whose type depends on payload or space state deliberately fall back. */
static bool prepared_pure_grounded_intrinsic_type_equal(
    Atom *left, Atom *right) {
    if (!left || !right || left->kind != ATOM_GROUNDED ||
        right->kind != ATOM_GROUNDED)
        return false;
    GroundedKind left_kind = left->ground.gkind;
    GroundedKind right_kind = right->ground.gkind;
    if (prepared_pure_numeric_ground_kind(left_kind) &&
        prepared_pure_numeric_ground_kind(right_kind))
        return true;
    if (left_kind != right_kind)
        return false;
    switch (left_kind) {
    case GV_BOOL:
    case GV_STRING:
        return true;
    case GV_INT:
    case GV_FLOAT:
    case GV_BIGINT:
    case GV_RATIONAL:
        return true;
    case GV_SPACE:
    case GV_STATE:
    case GV_CAPTURE:
    case GV_BINDINGS:
    case GV_FOREIGN:
    case GV_STRUCTURAL:
    case GV_SEAL:
    case GV_TERM_GRAPH:
    case GV_INTERNAL_TAG:
    case GV_PRIME_NEED_CAPABILITY:
    case GV_PRIME_CONTEXT:
        /* These values carry identity-, payload-, capability-, or
         * space-dependent typing.  Kind equality alone is not evidence. */
        return false;
    }
    return false;
}

static bool prepared_pure_operands_share_type(
    const CettaPreparedPureProgram *program, Arena *arena,
    uint8_t undefined_type_operands, Atom *left, Atom *right) {
    if (!program || !arena || !left || !right)
        return false;
    if (program->total_structural_equality || atom_eq(left, right))
        return true;
    if (undefined_type_operands != 0u) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PREPARED_PURE_UNDEFINED_TYPE_OPERAND);
        return true;
    }
    if (prepared_pure_grounded_intrinsic_type_equal(left, right)) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PREPARED_PURE_INTRINSIC_TYPE_HIT);
        return true;
    }
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PREPARED_PURE_TYPE_SERVICE_FALLBACK);

    Atom **left_types = NULL;
    Atom **right_types = NULL;
    uint32_t left_count = eval_get_atom_types_profiled_transient(
        program->space, arena, left, &left_types);
    uint32_t right_count = eval_get_atom_types_profiled_transient(
        program->space, arena, right, &right_types);
    bool compatible = left_count == 0u || right_count == 0u;
    for (uint32_t i = 0u; !compatible && i < left_count; i++) {
        for (uint32_t j = 0u; !compatible && j < right_count; j++) {
            Bindings type_bindings;
            bindings_init(&type_bindings);
            compatible = match_types(
                right_types[j], left_types[i], &type_bindings, arena);
            bindings_free(&type_bindings);
        }
    }
    free(right_types);
    free(left_types);
    return compatible;
}

/* Equality observes the list value a private carrier spells, including cells
 * nested in a record. Quotes and closures keep their existing opaque policy.
 * Scalar realizations leave these operands to the boxed observation below. */
static bool prepared_pure_equality_needs_observation(const Atom *value) {
    return value && atom_structural_may_have_list_carrier(value) &&
           petta_semantics_value_contains_observable_open_cons(value);
}

/* Execute the generated register arm directly.  Dialect dispatch is absent by
 * construction: instruction and result layout were emitted from the active
 * execution-contract presentation when the program was compiled. */
static Atom *prepared_pure_execute_register(
    const CettaPreparedPureProgram *program, Arena *arena,
    CettaGsltRegisterInstruction instruction,
    CettaGsltRegisterResultKind expected_kind,
    uint8_t undefined_type_operands,
    Atom *const *arguments, uint32_t arity) {
    if (!program || !program->boolean_value || !arena || !arguments ||
        arity != 2u)
        return NULL;
    Atom *observed[2];
    if (instruction == CETTA_GSLT_REGISTER_INSTRUCTION_ATOM_EQUAL) {
        for (uint32_t i = 0u; i < 2u; i++) {
            observed[i] = prepared_pure_equality_needs_observation(arguments[i])
                ? petta_semantics_materialize_value(arena, arguments[i])
                : arguments[i];
            if (!observed[i])
                return NULL;
        }
        arguments = observed;
    }
    CettaGsltRegisterOperandDiscipline discipline;
    if (!cetta_gslt_register_operand_discipline(
            instruction, &discipline))
        return NULL;
    if (discipline ==
            CETTA_GSLT_REGISTER_OPERANDS_TYPED_STRUCTURAL_OPERANDS &&
        !prepared_pure_operands_share_type(
            program, arena, undefined_type_operands,
            arguments[0], arguments[1]))
        return NULL;
    bool atom_boolean_result = false;
    CettaGsltRegisterResultKind atom_result_kind = expected_kind;
    if (cetta_gslt_register_execute_atom_binary(
            instruction, arguments[0], arguments[1],
            &atom_boolean_result, &atom_result_kind)) {
        if (atom_result_kind != expected_kind ||
            atom_result_kind != CETTA_GSLT_REGISTER_RESULT_BOOLEAN)
            return NULL;
        return prepared_pure_boolean_atom(
            program, arena, atom_boolean_result);
    }
    if (arguments[0] && arguments[1] &&
        arguments[0]->kind == ATOM_GROUNDED &&
        arguments[1]->kind == ATOM_GROUNDED &&
        arguments[0]->ground.gkind == GV_INT &&
        arguments[1]->ground.gkind == GV_INT) {
        int64_t integer_result = 0;
        bool boolean_result = false;
        bool promote = false;
        CettaGsltRegisterResultKind actual_kind = expected_kind;
        if (!cetta_gslt_register_execute_small_binary(
                instruction, &integer_result, &boolean_result,
                arguments[0]->ground.ival, arguments[1]->ground.ival,
                &actual_kind, &promote) ||
            actual_kind != expected_kind)
            return NULL;
        if (!promote &&
            actual_kind == CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER)
            return atom_int(arena, integer_result);
        if (!promote &&
            actual_kind == CETTA_GSLT_REGISTER_RESULT_BOOLEAN)
            return prepared_pure_boolean_atom(
                program, arena, boolean_result);
    }
#if CETTA_BUILD_WITH_GMP
    PreparedPureIntegerView left = {0};
    PreparedPureIntegerView right = {0};
    mpz_t integer_result;
    mpz_init(integer_result);
    bool boolean_result = false;
    CettaGsltRegisterResultKind actual_kind = expected_kind;
    bool ok = prepared_pure_integer_view_init(arguments[0], &left) &&
              prepared_pure_integer_view_init(arguments[1], &right) &&
              cetta_gslt_register_execute_binary(
                  instruction, integer_result, &boolean_result,
                  left.value, right.value, &actual_kind) &&
              actual_kind == expected_kind;
    Atom *result = NULL;
    if (ok && actual_kind == CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER)
        result = atom_bigint_take_mpz(arena, integer_result);
    else if (ok && actual_kind == CETTA_GSLT_REGISTER_RESULT_BOOLEAN)
        result = prepared_pure_boolean_atom(
            program, arena, boolean_result);
    prepared_pure_integer_view_clear(&right);
    prepared_pure_integer_view_clear(&left);
    mpz_clear(integer_result);
    return result;
#else
    (void)arena;
    (void)instruction;
    (void)expected_kind;
    (void)arguments;
    (void)arity;
    return NULL;
#endif
}

/* Execute only the successful value arm named by the generated intrinsic
 * program.  A fault, computation zero, or unsupported payload returns NULL;
 * the caller then replays the source through the canonical evaluator. */
static Atom *prepared_pure_execute_intrinsic(
    const CettaPreparedPureProgram *program, Arena *arena,
    CettaGsltPreparedPureIntrinsicInstruction instruction,
    Atom *head, Atom *const *arguments, uint32_t arity) {
    if (!program || !program->construct_value || !arena || !head || head->kind != ATOM_SYMBOL ||
        !arguments || !cetta_prepared_pure_intrinsic_scope_supported(head->sym_id, arity))
        return NULL;
    switch (instruction) {
    case CETTA_GSLT_PREPARED_PURE_INTRINSIC_GROUNDED_DISPATCH: {
        Atom *result = grounded_dispatch(arena, head, (Atom **)arguments, arity);
        return result && !atom_is_empty_or_error(result) ? result : NULL;
    }
    case CETTA_GSLT_PREPARED_PURE_INTRINSIC_BOOLEAN_DISPATCH: {
        if (arity != 1u && arity != 2u)
            return NULL;
        CettaPlainScalar inputs[2], result;
        for (uint32_t index = 0u; index < arity; index++) {
            inputs[index].kind = CETTA_PLAIN_SCALAR_BOOL;
            if (!prepared_pure_read_boolean(
                    program, arguments[index], &inputs[index].as.boolean))
                return NULL;
        }
        /* One scalar implementation computes truth; the program owns its
         * input and output representation. No intermediate Atom is needed. */
        return grounded_try_plain_scalar_operation(head, inputs, arity, &result) &&
                result.kind == CETTA_PLAIN_SCALAR_BOOL
            ? prepared_pure_boolean_atom(program, arena, result.as.boolean) : NULL;
    }
    case CETTA_GSLT_PREPARED_PURE_INTRINSIC_LIBRARY_VALUE_DISPATCH: {
        /* Native entry belongs to the retained step program. A scalar
         * attempt can decline here without executing the native twice. */
        return NULL;
    }
    /* The structural intrinsics read an expression's children or a list's
     * elements, and build a result of the same kind; a list pattern is
     * declined to the canonical evaluator. */
    case CETTA_GSLT_PREPARED_PURE_INTRINSIC_DECONSTRUCT_NONEMPTY_EXPRESSION: {
        Atom *const *elems;
        CettaExprLen len;
        if (arity != 1u ||
            !atom_sequence_view(arguments[0], &elems, &len) || len == 0u)
            return NULL;
        /* A cons cell is its head and its tail.  A list value is its first
         * element and the list value of the others.  An expression is its
         * first element and the rest, a view sharing the expression's
         * storage, so a walk by deconstruction costs the expression's
         * length, and the rest is data as it stands, never read as a Cons
         * however it begins.  Any other internal representation is the
         * canonical evaluator's. */
        Atom *list = arguments[0];
        Atom *pair[2];
        if (petta_semantics_is_open_cons_value(list)) {
            pair[0] = list->expr.elems[1];
            pair[1] = list->expr.elems[2];
        } else if (atom_is_list(list)) {
            pair[0] = elems[0];
            pair[1] = atom_list(arena, elems + 1u, len - 1u);
            if (!pair[1])
                return NULL;
        } else if (list->expr.elems[0]->kind == ATOM_GROUNDED &&
                   list->expr.elems[0]->ground.gkind == GV_INTERNAL_TAG) {
            return NULL;
        } else {
            pair[0] = list->expr.elems[0];
            pair[1] = atom_expr_suffix(arena, list, 1u);
            if (!pair[1])
                return NULL;
        }
        return program->construct_value(arena, pair, 2u);
    }
    case CETTA_GSLT_PREPARED_PURE_INTRINSIC_CONSTRUCT_EXPRESSION_CONS: {
        Atom *const *tail_elems;
        CettaExprLen tail_len;
        /* A cons cell tail is a logical list cell, never three children:
         * the canonical evaluator builds on it. */
        if (arity != 2u || !arguments[0] ||
            petta_semantics_is_open_cons_value(arguments[1]) ||
            !atom_sequence_view(arguments[1], &tail_elems, &tail_len) ||
            tail_len == UINT64_MAX)
            return NULL;
        if (atom_is_list(arguments[1])) {
            /* A list value stays a list. */
            CettaExprLen length = tail_len + 1u;
            if (!cetta_expr_len_mul_fits_size(length, sizeof(Atom *)))
                return NULL;
            Atom **elements = arena_alloc(
                arena, sizeof(*elements) * (size_t)length);
            elements[0] = arguments[0];
            if (tail_len > 0u) {
                memcpy(elements + 1u, tail_elems,
                       sizeof(*elements) * (size_t)tail_len);
            }
            return atom_list(arena, elements, length);
        }
        /* The expression with the head before the tail's children, in
         * amortized constant time when the tail has room below it
         * (PrefixBuffer.claimable_iff_free_below): a list built by
         * repeated cons costs its length, not its square. */
        return atom_expr_prepend(arena, arguments[0], arguments[1]);
    }
    case CETTA_GSLT_PREPARED_PURE_INTRINSIC_CONCATENATE_EXPRESSIONS: {
        Atom *const *left_elems, *const *right_elems;
        CettaExprLen left_len, right_len;
        if (arity != 2u)
            return NULL;
        /* PeTTa's union-atom is append/3 over logical lists: a chain of
         * cons cells that closes reads as the flat list it spells; an open
         * or improper one, which has no flat image, is the canonical
         * evaluator's. */
        Atom *left = arguments[0];
        Atom *right = arguments[1];
        if (petta_semantics_is_open_cons_value(left))
            left = petta_semantics_materialize_closed_logical_list(arena, left);
        if (left && petta_semantics_is_open_cons_value(right))
            right = petta_semantics_materialize_closed_logical_list(
                arena, right);
        if (!left || !right ||
            !atom_sequence_view(left, &left_elems, &left_len) ||
            !atom_sequence_view(right, &right_elems, &right_len) ||
            left_len > UINT64_MAX - right_len)
            return NULL;
        CettaExprLen length = left_len + right_len;
        if (!cetta_expr_len_mul_fits_size(length, sizeof(Atom *)))
            return NULL;
        Atom **elements = length
            ? arena_alloc(arena, sizeof(*elements) * (size_t)length)
            : NULL;
        if (left_len > 0u) {
            memcpy(elements, left_elems,
                   sizeof(*elements) * (size_t)left_len);
        }
        if (right_len > 0u) {
            memcpy(elements + left_len, right_elems,
                   sizeof(*elements) * (size_t)right_len);
        }
        return atom_is_list(left)
            ? atom_list(arena, elements, length)
            : atom_expr(arena, elements, length);
    }
    }
    return NULL;
}

/* A head can have two authored realizations: a representation-specific
 * register arm and a general pure intrinsic. Compose them at the ready-value
 * boundary, without replaying operand computations. The register result kind
 * describes only that arm; the intrinsic retains its own value contract. */
static Atom *prepared_pure_execute_register_intrinsic(
    const CettaPreparedPureProgram *program, Arena *arena,
    CettaGsltRegisterResultKind expected_kind,
    Atom *head, Atom *const *arguments, uint32_t arity) {
    CettaGsltPreparedPureIntrinsicInstruction intrinsic_instruction;
    if (!head || head->kind != ATOM_SYMBOL ||
        !cetta_prepared_pure_intrinsic_program(
            head->sym_id, arity, &intrinsic_instruction) ||
        intrinsic_instruction !=
            CETTA_GSLT_PREPARED_PURE_INTRINSIC_GROUNDED_DISPATCH)
        return NULL;
    Atom *result = prepared_pure_execute_intrinsic(
        program, arena, intrinsic_instruction, head, arguments, arity);
    if (result && expected_kind == CETTA_GSLT_REGISTER_RESULT_BOOLEAN) {
        /* Grounded comparison truth is shared; its public representation is
         * owned by the calling dialect, just as for the exact register arm. */
        if (!program->boolean_value)
            return NULL;
        if (prepared_pure_is_true(program, result))
            return prepared_pure_boolean_atom(program, arena, true);
        if (prepared_pure_is_false(program, result))
            return prepared_pure_boolean_atom(program, arena, false);
        return NULL;
    }
    return result;
}

static bool prepared_pure_push_dynamic_frame(
    CettaPreparedPureProgram *program, Atom *atom) {
    if (!program || !atom || program->dynamic_value_len > UINT32_MAX ||
        !prepared_pure_reserve(
            (void **)&program->dynamic_frames,
            sizeof(*program->dynamic_frames),
            &program->dynamic_frame_cap,
            program->dynamic_frame_len + 1u))
        return false;
    program->dynamic_frames[program->dynamic_frame_len++] =
        (PreparedPureDynamicFrame){
            .atom = atom,
            .value_base = (uint32_t)program->dynamic_value_len,
        };
    return true;
}

static bool prepared_pure_push_dynamic_value(
    CettaPreparedPureProgram *program, Atom *atom) {
    if (!program || !atom ||
        !prepared_pure_reserve(
            (void **)&program->dynamic_values,
            sizeof(*program->dynamic_values),
            &program->dynamic_value_cap,
            program->dynamic_value_len + 1u))
        return false;
    program->dynamic_values[program->dynamic_value_len++] = atom;
    return true;
}

/* Evaluate the value demanded by an eval-position variable.  This deliberately
 * accepts only the generated register and intrinsic languages.  Any user call,
 * control form, unadmitted grounded extension, or inert constructor expression
 * declines to the canonical evaluator.  The explicit stack keeps nested
 * arithmetic and structural operations off the native C stack. */
static bool prepared_pure_eval_dynamic_register_value(
    CettaPreparedPureProgram *program, Arena *arena,
    Atom *input, Atom **result_out) {
    if (result_out)
        *result_out = NULL;
    if (!program || !arena || !input || !result_out)
        return false;
    program->dynamic_frame_len = 0u;
    program->dynamic_value_len = 0u;
    if (!prepared_pure_push_dynamic_frame(program, input))
        return false;
    while (program->dynamic_frame_len > 0u) {
        PreparedPureDynamicFrame *frame =
            &program->dynamic_frames[program->dynamic_frame_len - 1u];
        Atom *current = frame->atom;
        if (current->kind != ATOM_EXPR || current->expr.len == 0u) {
            if (!prepared_pure_push_dynamic_value(program, current))
                return false;
            program->dynamic_frame_len--;
            continue;
        }
        Atom *head = current->expr.elems[0];
        CettaExprLen arity = current->expr.len - 1u;
        CettaGsltRegisterResultKind result_kind = {0};
        CettaGsltRegisterInstruction instruction = {0};
        CettaGsltPreparedPureIntrinsicInstruction intrinsic_instruction = {0};
        if (!head || head->kind != ATOM_SYMBOL)
            return false;
        bool is_register = prepared_pure_register_program(
            program, head->sym_id, arity, &result_kind, &instruction);
        bool is_intrinsic = !is_register &&
            cetta_prepared_pure_intrinsic_program(
                head->sym_id, arity, &intrinsic_instruction);
        if (!is_register && !is_intrinsic)
            return false;
        if (frame->child_index < arity) {
            if (program->dynamic_value_len !=
                (size_t)frame->value_base + frame->child_index)
                return false;
            Atom *child =
                current->expr.elems[frame->child_index + 1u];
            frame->child_index++;
            if (!prepared_pure_push_dynamic_frame(program, child))
                return false;
            continue;
        }
        if (program->dynamic_value_len !=
            (size_t)frame->value_base + arity)
            return false;
        Atom *result = is_register
            ? prepared_pure_execute_register(
                  program, arena, instruction, result_kind, 0u,
                  &program->dynamic_values[frame->value_base], arity)
            : prepared_pure_execute_intrinsic(
                  program, arena, intrinsic_instruction, head,
                  &program->dynamic_values[frame->value_base], arity);
        if (!result && is_register)
            result = prepared_pure_execute_register_intrinsic(
                program, arena, result_kind, head,
                &program->dynamic_values[frame->value_base], arity);
        program->dynamic_value_len = frame->value_base;
        if (!result || atom_is_error(result) ||
            !prepared_pure_push_dynamic_value(program, result))
            return false;
        program->dynamic_frame_len--;
    }
    if (program->dynamic_value_len != 1u ||
        !program->dynamic_values[0])
        return false;
    *result_out = program->dynamic_values[0];
    return true;
}

typedef enum {
    PREPARED_PURE_CHILDREN_FAILED = -1,
    PREPARED_PURE_CHILDREN_PENDING = 0,
    PREPARED_PURE_CHILDREN_READY = 1,
} PreparedPureChildrenState;

enum {
    /* Bounds on the operand tree one machine step evaluates in place, so a
     * step stays a bounded unit of work between interrupt polls. */
    PREPARED_PURE_INLINE_DEPTH = 4u,
    PREPARED_PURE_INLINE_NODES = 32u,
    PREPARED_PURE_INLINE_OPERANDS = 8u,
};

/* The value of a node that needs no evaluation step: a literal, a
 * positional slot or a ready entry argument.  NULL leaves the node to its
 * frame, which reports a missing value itself. */
static inline Atom *prepared_pure_immediate_value(
    const CettaPreparedPureProgram *program, const PreparedPureNode *node,
    uint32_t local_base) {
    switch (node->kind) {
    case PREPARED_PURE_LITERAL:
        return node->atom;
    case PREPARED_PURE_SLOT: {
        size_t slot = (size_t)local_base + node->auxiliary;
        return slot < program->slot_len ? program->slots[slot] : NULL;
    }
    case PREPARED_PURE_ENTRY_ARGUMENT:
        return node->auxiliary < program->entry_argument_count
            ? program->entry_arguments[node->auxiliary] : NULL;
    default:
        return NULL;
    }
}

/* A register operand held unboxed where its register arm allows: `atom` is
 * its value as an atom when one exists (every immediate operand); a small
 * exact integer or a truth computed here may have none. */
typedef struct {
    Atom *atom;
    union { int64_t integer; double floating; };
    bool is_integer;
    bool is_float;
    bool is_boolean;
    bool boolean;
} PreparedPureScalar;

static bool prepared_pure_scalar_tree_reference_enabled(void) {
    static _Thread_local int enabled = -1;
    if (enabled < 0) {
        const char *value = getenv("CETTA_PREPARED_PURE_SCALAR_TREE_REFERENCE");
        enabled = value && *value && strcmp(value, "0") != 0;
    }
    return enabled != 0;
}

static inline PreparedPureScalar prepared_pure_scalar_atom(Atom *atom) {
    PreparedPureScalar value = {.atom = atom};
    if (atom && atom->kind == ATOM_GROUNDED) {
        if (atom->ground.gkind == GV_INT) {
            value.is_integer = true;
            value.integer = atom->ground.ival;
        } else if (atom->ground.gkind == GV_FLOAT) {
            value.is_float = true;
            value.floating = atom->ground.fval;
        }
    }
    return value;
}

static bool prepared_pure_scalar_plain(
    const PreparedPureScalar *value, CettaPlainScalar *plain) {
    if (value->is_integer) {
        *plain = (CettaPlainScalar){.kind = CETTA_PLAIN_SCALAR_INT,
            .as.integer = value->integer};
        return true;
    }
    if (value->is_float) {
        *plain = (CettaPlainScalar){.kind = CETTA_PLAIN_SCALAR_FLOAT,
            .as.floating = value->floating};
        return true;
    }
    if (value->is_boolean) {
        *plain = (CettaPlainScalar){.kind = CETTA_PLAIN_SCALAR_BOOL,
            .as.boolean = value->boolean};
        return true;
    }
    return grounded_plain_scalar_from_atom(value->atom, plain);
}

/* Reuse the successful ordinary intrinsic arm. The generated register arm
 * itself still denotes exact integers; a float is never relabelled as one. */
static bool prepared_pure_scalar_float_operation(
    const PreparedPureNode *node, const PreparedPureScalar *left,
    const PreparedPureScalar *right, PreparedPureScalar *out) {
    CettaGsltPreparedPureIntrinsicInstruction instruction;
    if ((!left->is_float && !right->is_float) || !node->atom ||
        node->atom->kind != ATOM_SYMBOL ||
        !cetta_prepared_pure_intrinsic_program(node->atom->sym_id, 2u, &instruction) ||
        instruction != CETTA_GSLT_PREPARED_PURE_INTRINSIC_GROUNDED_DISPATCH)
        return false;
    CettaPlainScalar arguments[2], result;
    if (!prepared_pure_scalar_plain(left, &arguments[0]) ||
        !prepared_pure_scalar_plain(right, &arguments[1]) ||
        !grounded_try_plain_scalar_operation(node->atom, arguments, 2u, &result) ||
        (node->result_kind == CETTA_GSLT_REGISTER_RESULT_BOOLEAN &&
         result.kind != CETTA_PLAIN_SCALAR_BOOL))
        return false;
    if (result.kind == CETTA_PLAIN_SCALAR_FLOAT) {
        out->is_float = true;
        out->floating = result.as.floating;
    } else if (result.kind == CETTA_PLAIN_SCALAR_BOOL) {
        out->is_boolean = true;
        out->boolean = result.as.boolean;
    } else {
        out->is_integer = true;
        out->integer = result.as.integer;
    }
    return true;
}

static Atom *prepared_pure_scalar_leaf(
    const PreparedPureScalar *value, Atom *storage) {
    if (value->atom) return value->atom;
    if (value->is_integer) atom_scalar_leaf_int(storage, value->integer);
    else if (value->is_float) atom_scalar_leaf_float(storage, value->floating);
    /* A computed truth's Atom representation belongs to boolean_value.
     * Leave a representation observation to the boxed dialect boundary. */
    else return NULL;
    return storage;
}

static Atom *prepared_pure_inline_value(
    CettaPreparedPureProgram *program, Arena *arena,
    const PreparedPureNode *node, uint32_t local_base, uint32_t depth,
    uint32_t *budget);

/* Whether prepared_pure_operands_share_type holds of two unboxed operands,
 * decided without the type service: the same structural, literal and
 * intrinsic facts, an unboxed integer standing for a Number.  False leaves
 * the node to its frame, which asks the type service. */
static bool prepared_pure_scalar_operands_share_type(
    const CettaPreparedPureProgram *program, const PreparedPureNode *node,
    const PreparedPureScalar *left, const PreparedPureScalar *right) {
    if (program->total_structural_equality)
        return true;
    CettaGsltRegisterOperandDiscipline discipline;
    if (!cetta_gslt_register_operand_discipline(
            node->instruction, &discipline))
        return false;
    if (discipline !=
            CETTA_GSLT_REGISTER_OPERANDS_TYPED_STRUCTURAL_OPERANDS ||
        (left->atom && right->atom && atom_eq(left->atom, right->atom)))
        return true;
    if (node->undefined_type_operands != 0u) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PREPARED_PURE_UNDEFINED_TYPE_OPERAND);
        return true;
    }
    bool left_number = left->is_integer || left->is_float ||
        (left->atom && left->atom->kind == ATOM_GROUNDED &&
         prepared_pure_numeric_ground_kind(left->atom->ground.gkind));
    bool right_number = right->is_integer || right->is_float ||
        (right->atom && right->atom->kind == ATOM_GROUNDED &&
         prepared_pure_numeric_ground_kind(right->atom->ground.gkind));
    if ((left_number && right_number) ||
        (left->atom && right->atom &&
         prepared_pure_grounded_intrinsic_type_equal(
             left->atom, right->atom))) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PREPARED_PURE_INTRINSIC_TYPE_HIT);
        return true;
    }
    return false;
}

static bool prepared_pure_inline_register_scalar(
    CettaPreparedPureProgram *program, Arena *arena,
    const PreparedPureNode *node, uint32_t local_base, uint32_t depth,
    uint32_t *budget, PreparedPureScalar *out);

static PREPARED_PURE_ALWAYS_INLINE bool prepared_pure_scalar_binary(
    CettaPreparedPureProgram *program, const PreparedPureNode *node,
    const PreparedPureScalar *left_value, const PreparedPureScalar *right_value,
    PreparedPureScalar *out);

/* A node's value held unboxed where no step is needed: an immediate or
 * inline value, or a register arm decided on unboxed operands.  False
 * leaves the node to its frame. */
static bool prepared_pure_inline_scalar(
    CettaPreparedPureProgram *program, Arena *arena,
    const PreparedPureNode *node, uint32_t local_base, uint32_t depth,
    uint32_t *budget, PreparedPureScalar *out) {
    *out = (PreparedPureScalar){0};
    Atom *atom = prepared_pure_immediate_value(program, node, local_base);
    if (!atom && node->kind != PREPARED_PURE_REGISTER) {
        atom = prepared_pure_inline_value(
            program, arena, node, local_base, depth, budget);
    }
    if (atom) {
        *out = prepared_pure_scalar_atom(atom);
        return true;
    }
    if (node->kind != PREPARED_PURE_REGISTER || node->child_count != 2u ||
        depth == 0u || *budget == 0u)
        return false;
    return prepared_pure_inline_register_scalar(
        program, arena, node, local_base, depth, budget, out);
}

/* The register arm of prepared_pure_execute_register on unboxed operands,
 * for the cases where it is decided without a boxed value: structural
 * equality of operands known to share a type, and small exact integers
 * without overflow.  Every other case returns false and the node is
 * evaluated on atoms.  Kept out of line so that the immediate operands of
 * prepared_pure_inline_scalar need no frame for these operands. */
static bool PREPARED_PURE_NOINLINE prepared_pure_inline_register_scalar(
    CettaPreparedPureProgram *program, Arena *arena,
    const PreparedPureNode *node, uint32_t local_base, uint32_t depth,
    uint32_t *budget, PreparedPureScalar *out) {
    (*budget)--;
    PreparedPureScalar left;
    PreparedPureScalar right;
    if (!prepared_pure_inline_scalar(
            program, arena,
            &program->nodes[program->children[node->first_child]],
            local_base, depth - 1u, budget, &left) ||
        !prepared_pure_inline_scalar(
            program, arena,
            &program->nodes[program->children[node->first_child + 1u]],
            local_base, depth - 1u, budget, &right))
        return false;
    return prepared_pure_scalar_binary(program, node, &left, &right, out);
}

static PREPARED_PURE_ALWAYS_INLINE bool prepared_pure_scalar_binary(
    CettaPreparedPureProgram *program, const PreparedPureNode *node,
    const PreparedPureScalar *left_value, const PreparedPureScalar *right_value,
    PreparedPureScalar *out) {
    PreparedPureScalar left = *left_value, right = *right_value;
    *out = (PreparedPureScalar){0};
    if (node->instruction == CETTA_GSLT_REGISTER_INSTRUCTION_ATOM_EQUAL) {
        if (prepared_pure_equality_needs_observation(left.atom) ||
            prepared_pure_equality_needs_observation(right.atom))
            return false;
        if (node->result_kind != CETTA_GSLT_REGISTER_RESULT_BOOLEAN ||
            (!program->total_structural_equality &&
             !prepared_pure_scalar_operands_share_type(
                 program, node, &left, &right)))
            return false;
        bool equal;
        if (left.atom && right.atom) {
            /* Two integers are equal by value, as value equality has it. */
            equal = left.atom->kind == ATOM_GROUNDED &&
                    right.atom->kind == ATOM_GROUNDED &&
                    left.atom->ground.gkind == GV_INT &&
                    right.atom->ground.gkind == GV_INT
                ? left.atom->ground.ival == right.atom->ground.ival
                : atom_value_eq(left.atom, right.atom);
        } else if (left.is_integer && right.is_integer) {
            equal = left.integer == right.integer;
        } else {
            Atom left_storage, right_storage;
            Atom *left_value = prepared_pure_scalar_leaf(&left, &left_storage);
            Atom *right_value = prepared_pure_scalar_leaf(&right, &right_storage);
            if (!left_value || !right_value) return false;
            equal = atom_value_eq(left_value, right_value);
        }
        out->is_boolean = true;
        out->boolean = equal;
        return true;
    }
    if (!left.is_integer || !right.is_integer)
        return prepared_pure_scalar_float_operation(node, &left, &right, out);
    CettaGsltRegisterOperandDiscipline discipline;
    if (!cetta_gslt_register_operand_discipline(
            node->instruction, &discipline) ||
        discipline != CETTA_GSLT_REGISTER_OPERANDS_EXACT_INTEGER_OPERANDS)
        return false;
    int64_t integer = 0;
    bool boolean = false;
    bool promote = false;
    CettaGsltRegisterResultKind kind = node->result_kind;
    if (!cetta_gslt_register_execute_small_binary(
            node->instruction, &integer, &boolean,
            left.integer, right.integer, &kind, &promote) ||
        promote || kind != node->result_kind)
        return false;
    if (kind == CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER) {
        out->is_integer = true;
        out->integer = integer;
        return true;
    }
    if (kind == CETTA_GSLT_REGISTER_RESULT_BOOLEAN) {
        out->is_boolean = true;
        out->boolean = boolean;
        return true;
    }
    return false;
}

/* Evaluate a register, intrinsic or constructor node whose operands are
 * immediate or again such nodes, without frames.  Each operation is the one
 * its frame runs, on the same operand values; NULL leaves the node to its
 * frames, which also report any decline. */
static Atom *prepared_pure_inline_value(
    CettaPreparedPureProgram *program, Arena *arena,
    const PreparedPureNode *node, uint32_t local_base, uint32_t depth,
    uint32_t *budget) {
    Atom *immediate =
        prepared_pure_immediate_value(program, node, local_base);
    if (immediate)
        return immediate;
    if (depth == 0u || *budget == 0u ||
        node->child_count > PREPARED_PURE_INLINE_OPERANDS ||
        (node->kind != PREPARED_PURE_REGISTER &&
         node->kind != PREPARED_PURE_INTRINSIC &&
         node->kind != PREPARED_PURE_BUILD))
        return NULL;
    if (node->kind == PREPARED_PURE_REGISTER) {
        uint32_t scalar_budget = *budget;
        PreparedPureScalar scalar;
        if (prepared_pure_inline_scalar(
                program, arena, node, local_base, depth, &scalar_budget,
                &scalar)) {
            *budget = scalar_budget;
            if (scalar.is_boolean)
                return prepared_pure_boolean_atom(
                    program, arena, scalar.boolean);
            if (scalar.atom)
                return scalar.atom;
            if (scalar.is_float)
                return atom_float(arena, scalar.floating);
            return scalar.is_integer ? atom_int(arena, scalar.integer) : NULL;
        }
    }
    (*budget)--;
    Atom *operands[PREPARED_PURE_INLINE_OPERANDS];
    for (uint32_t i = 0u; i < node->child_count; i++) {
        operands[i] = prepared_pure_inline_value(
            program, arena,
            &program->nodes[program->children[node->first_child + i]],
            local_base, depth - 1u, budget);
        if (!operands[i])
            return NULL;
    }
    if (node->kind == PREPARED_PURE_BUILD) {
        if (node->auxiliary == PREPARED_PURE_BUILD_DATA_HEAD &&
            (node->child_count == 0u ||
             !prepared_pure_data_head_value(operands[0])))
            return NULL;
        return program->construct_value(
            arena, operands, node->child_count);
    }
    if (node->kind == PREPARED_PURE_INTRINSIC) {
        Atom *result = prepared_pure_execute_intrinsic(
            program, arena, node->intrinsic_instruction, node->atom,
            operands, node->child_count);
        return result && !atom_is_error(result) ? result : NULL;
    }
    Atom *result = prepared_pure_execute_register(
        program, arena, node->instruction, node->result_kind,
        node->undefined_type_operands, operands, node->child_count);
    if (!result)
        result = prepared_pure_execute_register_intrinsic(
            program, arena, node->result_kind, node->atom,
            operands, node->child_count);
    return result &&
        !(result->kind == ATOM_EXPR && atom_is_error(result))
        ? result : NULL;
}

typedef enum {
    PREPARED_PURE_TRUTH_UNAVAILABLE = 0,
    PREPARED_PURE_TRUTH_TRUE,
    PREPARED_PURE_TRUTH_FALSE,
    PREPARED_PURE_TRUTH_NOT_BOOLEAN,
} PreparedPureTruth;

/* The truth of a conditional's condition when it can be evaluated in place.
 * UNAVAILABLE schedules the condition's frame; NOT_BOOLEAN is the value the
 * frame would reject. */
static PreparedPureTruth prepared_pure_inline_truth(
    CettaPreparedPureProgram *program, Arena *arena,
    const PreparedPureNode *node, uint32_t local_base) {
    uint32_t budget = PREPARED_PURE_INLINE_NODES;
    PreparedPureScalar condition;
    if (!prepared_pure_inline_scalar(
            program, arena,
            &program->nodes[program->children[node->first_child]],
            local_base, PREPARED_PURE_INLINE_DEPTH, &budget, &condition))
        return PREPARED_PURE_TRUTH_UNAVAILABLE;
    if (condition.is_boolean)
        return condition.boolean
            ? PREPARED_PURE_TRUTH_TRUE : PREPARED_PURE_TRUTH_FALSE;
    if (condition.atom && prepared_pure_is_true(program, condition.atom))
        return PREPARED_PURE_TRUTH_TRUE;
    if (condition.atom &&
        (prepared_pure_is_false(program, condition.atom) ||
         prepared_pure_petta_else_value(condition.atom)))
        return PREPARED_PURE_TRUTH_FALSE;
    return PREPARED_PURE_TRUTH_NOT_BOOLEAN;
}

/* Out of line, so that its callers do not multiply the inlined copies of
 * prepared_pure_inline_value that the children loop relies on. */
static PREPARED_PURE_NOINLINE Atom *prepared_pure_inline_child(
    CettaPreparedPureProgram *program, Arena *arena,
    const PreparedPureNode *node, uint32_t child, uint32_t local_base) {
    uint32_t budget = PREPARED_PURE_INLINE_NODES;
    return prepared_pure_inline_value(
        program, arena,
        &program->nodes[program->children[node->first_child + child]],
        local_base, PREPARED_PURE_INLINE_DEPTH, &budget);
}

/* Out of line, with its value helpers inlined into its loop: the layout the
 * executor's hot loop was measured with. */
static PREPARED_PURE_NOINLINE PreparedPureChildrenState
prepared_pure_finish_children(
    CettaPreparedPureProgram *program, Arena *arena,
    PreparedPureFrame *frame, const PreparedPureNode *node) {
    if (frame->state == 0u) {
        frame->value_base = (uint32_t)program->value_len;
        frame->child_index = 0u;
        frame->state = 1u;
    } else if (frame->child_index < node->child_count) {
        if (program->value_len !=
            (size_t)frame->value_base + frame->child_index + 1u)
            return PREPARED_PURE_CHILDREN_FAILED;
        frame->child_index++;
    }
    uint32_t budget = PREPARED_PURE_INLINE_NODES;
    while (frame->child_index < node->child_count) {
        uint32_t child =
            program->children[node->first_child + frame->child_index];
        Atom *value = prepared_pure_inline_value(
            program, arena, &program->nodes[child], frame->local_base,
            PREPARED_PURE_INLINE_DEPTH, &budget);
        if (!value) {
            if (!prepared_pure_push_frame(program, child, frame->local_base))
                return PREPARED_PURE_CHILDREN_FAILED;
            return PREPARED_PURE_CHILDREN_PENDING;
        }
        if (!prepared_pure_push_value(program, value))
            return PREPARED_PURE_CHILDREN_FAILED;
        frame->child_index++;
    }
    if (node->kind == PREPARED_PURE_CALL &&
        node->call_arguments_are_values) {
        if (node->child_count > 64u)
            return PREPARED_PURE_CHILDREN_FAILED;
        frame->ready_arguments = node->child_count == 64u
            ? UINT64_MAX
            : (UINT64_C(1) << node->child_count) - UINT64_C(1);
    }
    return PREPARED_PURE_CHILDREN_READY;
}

/* Replace a completed equation's tail-continuation spine with the tail call's
 * already-evaluated argument frame.  The nearest waiting call owns that
 * equation activation; selected branches and let bodies are transparent only
 * when the compiler marked their result spine as tail-position. */
static bool prepared_pure_tail_reenter(
    CettaPreparedPureProgram *program, bool *reentered_out) {
    if (reentered_out)
        *reentered_out = false;
    if (!program || !reentered_out || program->frame_len == 0u)
        return false;
    size_t current = program->frame_len - 1u;
    PreparedPureFrame *tail = &program->frames[current];
    if (tail->node >= program->node_len ||
        program->nodes[tail->node].kind != PREPARED_PURE_CALL ||
        !program->nodes[tail->node].tail_position)
        return true;

    size_t owner = current;
    bool found = false;
    while (owner > 0u) {
        owner--;
        PreparedPureFrame *candidate = &program->frames[owner];
        bool is_call = candidate->node == PREPARED_PURE_RUNTIME_NODE;
        if (!is_call && candidate->node < program->node_len)
            is_call = program->nodes[candidate->node].kind ==
                PREPARED_PURE_CALL;
        if (is_call && candidate->state == 2u) {
            found = true;
            break;
        }
    }
    /* A caller that recorded contexts still builds its value into them. */
    if (!found || program->frames[owner].context_count > 0u)
        return true;
    for (size_t i = owner + 1u; i < current; i++) {
        PreparedPureFrame *intermediate = &program->frames[i];
        if (intermediate->node >= program->node_len ||
            !program->nodes[intermediate->node].tail_position)
            return false;
    }

    PreparedPureFrame *caller = &program->frames[owner];
    if (caller->saved_slot_len > program->slot_len ||
        !prepared_pure_memo_defer_tail(program, caller))
        return false;
    PreparedPureFrame next = *tail;
    Atom *next_atom = program->frame_atoms[current];
    program->slot_len = caller->saved_slot_len;
    program->frames[owner] = next;
    program->frame_atoms[owner] = next_atom;
    program->frame_len = owner + 1u;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PREPARED_PURE_CALL_TAIL_REENTRY);
    *reentered_out = true;
    return true;
}

/* The value of a child that a context records or a continued call takes:
 * read directly when it is a literal or a slot, evaluated in place
 * otherwise; NULL when it needs a frame. */
static inline Atom *prepared_pure_context_child(
    CettaPreparedPureProgram *program, Arena *arena,
    const PreparedPureNode *node, uint32_t child, uint32_t local_base) {
    const PreparedPureNode *value_node =
        &program->nodes[program->children[node->first_child + child]];
    switch (value_node->kind) {
    case PREPARED_PURE_LITERAL:
    case PREPARED_PURE_SLOT:
    case PREPARED_PURE_ENTRY_ARGUMENT:
        return prepared_pure_immediate_value(
            program, value_node, local_base);
    default:
        return prepared_pure_inline_child(
            program, arena, node, child, local_base);
    }
}

typedef enum {
    PREPARED_PURE_CONTEXT_NOT_ENTERED = 0,
    PREPARED_PURE_CONTEXT_ENTERED,
    PREPARED_PURE_CONTEXT_FAILED,
} PreparedPureContextEntry;

/* A selected equation whose body builds a value around one call records the
 * constructor as a context and lets the call take the caller's frame.  The
 * constructor's other children and the call's arguments must be values
 * here; otherwise the body runs as usual.  The recorded children stay on the
 * value stack, below the call's arguments, and the equation's locals are
 * released, since nothing reads them after this point. */
static PREPARED_PURE_NOINLINE PreparedPureContextEntry
prepared_pure_enter_context(
    CettaPreparedPureProgram *program, Arena *arena,
    PreparedPureFrame *frame, const PreparedPureEquation *equation,
    uint32_t call_base) {
    const PreparedPureNode *build = &program->nodes[equation->body.root];
    uint32_t hole = equation->context_hole;
    uint32_t call_index = program->children[build->first_child + hole];
    const PreparedPureNode *call = &program->nodes[call_index];
    uint32_t recorded = build->child_count - 1u;
    uint32_t staged_count = recorded + call->child_count;
    Atom *staged[2u * PREPARED_PURE_CONTEXT_MAX_CHILDREN];
    if (call->child_count > PREPARED_PURE_CONTEXT_MAX_CHILDREN ||
        call->child_count > 64u)
        return PREPARED_PURE_CONTEXT_NOT_ENTERED;
    if (equation->first_context_read != PREPARED_PURE_NO_CONTEXT) {
        /* Read in place: literals, and the matched locals at call_base. */
        if (!prepared_pure_reserve(
                (void **)&program->contexts, sizeof(*program->contexts),
                &program->context_cap, program->context_len + 1u) ||
            !prepared_pure_reserve(
                (void **)&program->values, sizeof(*program->values),
                &program->value_cap,
                (size_t)frame->value_base + staged_count))
            return PREPARED_PURE_CONTEXT_FAILED;
        const PreparedPureContextRead *reads =
            &program->context_reads[equation->first_context_read];
        Atom *const *locals = &program->slots[call_base];
        Atom **out = &program->values[frame->value_base];
        for (uint32_t i = 0u; i < staged_count; i++) {
            Atom *value = reads[i].literal
                ? reads[i].literal : locals[reads[i].slot];
            if (!value)
                return PREPARED_PURE_CONTEXT_NOT_ENTERED;
            out[i] = value;
        }
        goto recorded;
    }
    uint32_t written = 0u;
    for (uint32_t i = 0u; i < build->child_count; i++) {
        if (i == hole)
            continue;
        Atom *value = prepared_pure_context_child(
            program, arena, build, i, call_base);
        if (!value)
            return PREPARED_PURE_CONTEXT_NOT_ENTERED;
        staged[written++] = value;
    }
    for (uint32_t i = 0u; i < call->child_count; i++) {
        Atom *value = prepared_pure_context_child(
            program, arena, call, i, call_base);
        if (!value)
            return PREPARED_PURE_CONTEXT_NOT_ENTERED;
        staged[written++] = value;
    }
    if (!prepared_pure_reserve(
            (void **)&program->contexts, sizeof(*program->contexts),
            &program->context_cap, program->context_len + 1u) ||
        !prepared_pure_reserve(
            (void **)&program->values, sizeof(*program->values),
            &program->value_cap,
            (size_t)frame->value_base + staged_count))
        return PREPARED_PURE_CONTEXT_FAILED;
    for (uint32_t i = 0u; i < staged_count; i++)
        program->values[frame->value_base + i] = staged[i];
recorded:
    program->contexts[program->context_len++] = (PreparedPureContext){
        .node = equation->body.root,
        .hole = hole,
    };
    frame->context_count++;
    frame->value_base += recorded;
    program->value_len = (size_t)frame->value_base + call->child_count;
    program->slot_len = frame->saved_slot_len;
    frame->node = call_index;
    frame->child_index = call->child_count;
    frame->ready_arguments = call->call_arguments_are_values
        ? (call->child_count == 64u
               ? UINT64_MAX
               : (UINT64_C(1) << call->child_count) - UINT64_C(1))
        : 0u;
    frame->state = 4u;
    return PREPARED_PURE_CONTEXT_ENTERED;
}

/* A call that answers builds its value into the contexts it recorded, the
 * last recorded first (ContextStackEvaluation.plug), at most
 * PREPARED_PURE_PLUG_BATCH contexts per step of the executor.  Between steps
 * the value built so far stays on the value stack, where the collector finds
 * it, and the executor polls for interrupts as it does between frames.
 * After the last context the call completes as any call does. */
enum { PREPARED_PURE_PLUG_BATCH = 8u, PREPARED_PURE_CONTEXT_RUN = 32u };

static PREPARED_PURE_NOINLINE bool prepared_pure_plug_context_step(
    CettaPreparedPureProgram *program, Arena *arena,
    PreparedPureFrame *frame) {
    if (frame->context_count == 0u ||
        program->value_len != (size_t)frame->value_base + 1u)
        return false;
    Atom *value = program->values[frame->value_base];
    uint32_t base = frame->value_base;
    for (uint32_t step = 0u;
         step < PREPARED_PURE_PLUG_BATCH && frame->context_count > 0u;
         step++) {
        if (program->context_len == 0u)
            return false;
        PreparedPureContext context =
            program->contexts[--program->context_len];
        const PreparedPureNode *build = &program->nodes[context.node];
        uint32_t recorded = build->child_count - 1u;
        if (base < recorded || context.hole >= build->child_count ||
            build->child_count > PREPARED_PURE_CONTEXT_MAX_CHILDREN)
            return false;
        base -= recorded;
        Atom *elements[PREPARED_PURE_CONTEXT_MAX_CHILDREN];
        for (uint32_t i = 0u, read = base; i < build->child_count; i++)
            elements[i] = i == context.hole ? value : program->values[read++];
        value = program->construct_value(
            arena, elements, build->child_count);
        if (!value)
            return false;
        program->values[base] = value;
        program->value_len = (size_t)base + 1u;
        frame->value_base = base;
        frame->context_count--;
    }
    if (frame->context_count > 0u)
        return true;
    if (frame->saved_slot_len > program->slot_len ||
        !prepared_pure_memo_complete(program, frame, value))
        return false;
    program->slot_len = frame->saved_slot_len;
    program->frame_len--;
    return true;
}

/* The match of a direct entry that needs registers, out of line so the
 * executor's call path keeps a small frame.  With `key_ops` > 0 the key
 * tested the first argument's head and length, as the equation's first
 * ops do (prepared_pure_equation_position_key): load what they load and
 * run the rest. */
static PREPARED_PURE_NOINLINE PreparedPureMatchState
prepared_pure_direct_match(
    CettaPreparedPureProgram *program,
    const PreparedPureEquation *equation,
    Atom *const *arguments, uint32_t arity, uint64_t ready_arguments,
    uint32_t key_ops, uint32_t key_length, Atom **locals) {
    if (key_ops == 0u) {
        uint32_t demanded_argument = 0u;
        return prepared_pure_match_into(
            program, equation, arguments, arity, ready_arguments,
            &demanded_argument, locals);
    }
    Atom *registers[PREPARED_PURE_MATCH_MAX_REGISTERS];
    for (uint32_t i = 0u; i < arity; i++)
        registers[i] = arguments[i];
    if (key_length > 0u)
        memcpy(&registers[
                   program->match_ops[equation->first_match_op].operand],
               arguments[0]->expr.elems, sizeof(*registers) * key_length);
    return prepared_pure_run_match_ops(
        program, equation, registers, key_ops, locals);
}

typedef enum {
    PREPARED_PURE_DIRECT_FAILED = -1,
    PREPARED_PURE_DIRECT_NOT_APPLICABLE = 0,
    PREPARED_PURE_DIRECT_MATCHED = 1,
} PreparedPureDirectState;

/* A call whose arguments are all values, on a head with one candidate
 * equation for them -- its only equation of that arity, or the one of a
 * keyed group admitting their keys -- matches it straight into the free slots
 * above slot_len, which the entered body owns: selection would try that
 * equation alone and copy the same bindings there.  FAILED is where
 * selection meets no equation, or a match error.  An equation the general
 * matcher decides, through the dialect's pattern views, is selected as
 * usual. */
static PREPARED_PURE_ALWAYS_INLINE PreparedPureDirectState
prepared_pure_direct_entry_arguments(
    CettaPreparedPureProgram *program, uint32_t head_index,
    Atom *const *arguments, uint64_t ready_arguments, uint32_t arity,
    const PreparedPureEquation **equation_out) {
    uint64_t all_arguments = arity >= 64u
        ? UINT64_MAX : (UINT64_C(1) << arity) - UINT64_C(1);
    if ((ready_arguments & all_arguments) != all_arguments)
        return PREPARED_PURE_DIRECT_NOT_APPLICABLE;
    if (!prepared_pure_typed_arguments_accepted(
            program, head_index, arguments, arity))
        return PREPARED_PURE_DIRECT_FAILED;
    const PreparedPureHead *head = &program->heads[head_index];
    uint32_t equation_index = head->first_equation;
    /* The first argument's key the admitted equation tests: its length,
     * and the match ops that test it, which then need not run again. */
    uint32_t key_length = 0u;
    uint32_t key_ops = 0u;
    if (head->equation_count != 1u) {
        if (!head->keyed)
            return PREPARED_PURE_DIRECT_NOT_APPLICABLE;
        const PreparedPureDecisionProgram *decision =
            &program->decisions[head->first_decision];
        if (decision->arity != arity || arity == 0u)
            return PREPARED_PURE_DIRECT_NOT_APPLICABLE;
        const PreparedPureArgumentKey *admitted = NULL;
        PreparedPureKeyLookup lookup = prepared_pure_key_lookup(
            program, decision, arguments, &admitted);
        if (lookup == PREPARED_PURE_KEY_NOT_APPLICABLE)
            return PREPARED_PURE_DIRECT_NOT_APPLICABLE;
        if (lookup == PREPARED_PURE_KEY_NONE)
            return PREPARED_PURE_DIRECT_FAILED;
        equation_index = admitted->equation;
        /* The first argument's key, when tested, is its first ops. */
        if (admitted->symbol != SYMBOL_ID_NONE) {
            key_length = admitted->length;
            key_ops = key_length > 0u ? 2u : 1u;
        }
    }
    if (equation_index >= program->equation_len)
        return PREPARED_PURE_DIRECT_FAILED;
    const PreparedPureEquation *equation =
        &program->equations[equation_index];
    if (equation->arity != arity ||
        (!equation->compiled_match && !equation->variable_arguments) ||
        (key_ops > 0u &&
         (!equation->compiled_match ||
          equation->match_op_count < key_ops)))
        return PREPARED_PURE_DIRECT_NOT_APPLICABLE;
    if (program->slot_len > UINT32_MAX - equation->body.local_count ||
        !prepared_pure_reserve(
            (void **)&program->slots, sizeof(*program->slots),
            &program->slot_cap, program->slot_len + equation->body.local_count))
        return PREPARED_PURE_DIRECT_FAILED;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PREPARED_PURE_DECISION_FULL_MATCH);
    Atom **locals = &program->slots[program->slot_len];
    PreparedPureMatchState matched;
    /* The pattern's variables hold the first locals.  The two binding
     * paths below give each of them its value, so only the body's own
     * locals above them start unbound; the full match starts from none. */
    if (key_ops == 0u && equation->variable_arguments &&
        !equation->repeated_variables) {
        const uint32_t *slots =
            &program->argument_slots[equation->first_argument_slot];
        for (uint32_t i = 0u; i < arity; i++)
            locals[slots[i]] = arguments[i];
        for (uint32_t i = equation->pattern_var_count;
             i < equation->body.local_count; i++)
            locals[i] = NULL;
        matched = prepared_pure_count_match(
            PREPARED_PURE_MATCH_MATCHED, PREPARED_PURE_MISMATCH_HEAD);
    } else if (key_ops > 0u && equation->binds_after_key) {
        /* The rest of the match binds registers the key ops loaded: the
         * arguments, and the keyed expression's fields. */
        const PreparedPureMatchOp *op =
            &program->match_ops[equation->first_match_op];
        const PreparedPureMatchOp *end = op + equation->match_op_count;
        Atom *const *fields =
            key_length > 0u ? arguments[0]->expr.elems : NULL;
        uint32_t base = key_length > 0u ? op->operand : 0u;
        for (op += key_ops; op < end; op++)
            locals[op->operand] = op->source < arity
                ? arguments[op->source] : fields[op->source - base];
        for (uint32_t i = equation->pattern_var_count;
             i < equation->body.local_count; i++)
            locals[i] = NULL;
        matched = prepared_pure_count_match(
            PREPARED_PURE_MATCH_MATCHED, PREPARED_PURE_MISMATCH_HEAD);
    } else {
        if (equation->body.local_count > 0u)
            memset(locals, 0, sizeof(*locals) * equation->body.local_count);
        matched = prepared_pure_direct_match(
            program, equation, arguments, arity, ready_arguments,
            key_ops, key_length, locals);
    }
    if (matched != PREPARED_PURE_MATCH_MATCHED ||
        (equation->scalar_guard != PREPARED_PURE_NO_SCALAR_GUARD &&
         prepared_pure_evaluate_scalar_guard(program, equation, locals) !=
             PREPARED_PURE_GUARD_ACCEPTED))
        return PREPARED_PURE_DIRECT_FAILED;
    *equation_out = equation;
    return PREPARED_PURE_DIRECT_MATCHED;
}

static PREPARED_PURE_ALWAYS_INLINE PreparedPureDirectState
prepared_pure_direct_entry(
    CettaPreparedPureProgram *program, uint32_t head_index,
    const PreparedPureFrame *frame, uint32_t arity,
    const PreparedPureEquation **equation_out) {
    return prepared_pure_direct_entry_arguments(
        program, head_index, &program->values[frame->value_base],
        frame->ready_arguments, arity, equation_out);
}

static bool PREPARED_PURE_HOT prepared_pure_resume_call(
    CettaPreparedPureProgram *program, Arena *arena,
    PreparedPureFrame *frame,
    uint32_t head_index, uint32_t arity) {
    if (!program || !arena || !frame || head_index >= program->head_len)
        return false;
    if (frame->state == 2u) {
        if (program->value_len !=
                (size_t)frame->value_base + 1u ||
            frame->saved_slot_len > program->slot_len)
            return false;
        /* State 5: the call's value is being built into its recorded
         * contexts, one per step. */
        if (__builtin_expect(frame->context_count > 0u, 0)) {
            frame->state = 5u;
            return true;
        }
        if (!prepared_pure_memo_complete(
                program, frame,
                program->values[frame->value_base]))
            return false;
        program->slot_len = frame->saved_slot_len;
        program->frame_len--;
        return true;
    }
    if (frame->state == 3u) {
        if (frame->demanded_argument >= arity ||
            frame->demanded_argument >= 64u ||
            program->value_len !=
                (size_t)frame->value_base + arity + 1u)
            return false;
        Atom *value = program->values[--program->value_len];
        CettaLanguageId language = eval_current_language_id
            ? eval_current_language_id() : CETTA_LANGUAGE_HE;
        if ((language == CETTA_LANGUAGE_PETTA
                 ? atom_is_petta_no_result(value)
                 : language != CETTA_LANGUAGE_PRIME && atom_is_empty(value)) ||
            atom_is_error(value))
            return false;
        program->values[
            frame->value_base + frame->demanded_argument] = value;
        frame->ready_arguments |=
            UINT64_C(1) << frame->demanded_argument;
        frame->state = 1u;
    }
    /* State 4 is a call a recorded context continues with: its arguments
     * are staged, as a call in state 1 has them. */
    if (frame->state != 1u && frame->state != 4u) {
        if (frame->state == 5u)
            return prepared_pure_plug_context_step(program, arena, frame);
        return false;
    }
    /* A call that records a context continues with its inner call in the
     * same step, up to PREPARED_PURE_CONTEXT_RUN calls: the executor loop
     * would resume that state-4 frame next, and recording allocates no
     * atom, so the collector has nothing new to see in between. */
    uint32_t continued = 0u;
continue_call:
    if (program->value_len != (size_t)frame->value_base + arity ||
        (program->closed_program && arity > 64u))
        return false;

    /* An eager call demands the first argument that is not yet a value;
     * a call whose arguments are all ready has none to demand. */
    uint64_t all_arguments = arity >= 64u
        ? UINT64_MAX : (UINT64_C(1) << arity) - UINT64_C(1);
    if (program->closed_program &&
        program->call_mode == CETTA_GSLT_PURE_CALL_EAGER &&
        (frame->ready_arguments & all_arguments) != all_arguments) {
        for (uint32_t i = 0u; i < arity; i++) {
            uint64_t bit = UINT64_C(1) << i;
            if ((frame->ready_arguments & bit) != 0u)
                continue;
            Atom *argument = program->values[frame->value_base + i];
            if (!prepared_pure_expression_is_callable(
                    program, argument)) {
                frame->ready_arguments |= bit;
                continue;
            }
            frame->demanded_argument = i;
            frame->state = 3u;
            return prepared_pure_push_runtime_frame(
                program, argument);
        }
    }

    const PreparedPureEquation *equation = NULL;
    PreparedPureDirectState direct = prepared_pure_direct_entry(
        program, head_index, frame, arity, &equation);
    if (direct == PREPARED_PURE_DIRECT_FAILED)
        return false;
    if (direct == PREPARED_PURE_DIRECT_NOT_APPLICABLE) {
        PreparedPureSelection selection = prepared_pure_select_equation(
            program, head_index,
            &program->values[frame->value_base], arity,
            frame->ready_arguments);
        if (selection.state == PREPARED_PURE_SELECT_NEEDS_ARGUMENT) {
            uint32_t argument = selection.demanded_argument;
            if (!program->closed_program || argument >= 64u ||
                argument >= arity)
                return false;
            frame->demanded_argument = argument;
            frame->state = 3u;
            return prepared_pure_push_runtime_frame(
                program, program->values[frame->value_base + argument]);
        }
        equation = selection.equation;
        if (selection.state != PREPARED_PURE_SELECT_SELECTED || !equation ||
            program->slot_len > UINT32_MAX - equation->body.local_count ||
            !prepared_pure_reserve(
                (void **)&program->slots,
                sizeof(*program->slots), &program->slot_cap,
                program->slot_len + equation->body.local_count))
            return false;
        if (equation->body.local_count > 0u)
            memcpy(&program->slots[program->slot_len],
                   program->selected_values,
                   sizeof(*program->slots) * equation->body.local_count);
    }
    frame = &program->frames[program->frame_len - 1u];
    frame->saved_slot_len = (uint32_t)program->slot_len;
    uint32_t call_base = frame->saved_slot_len;
    program->slot_len += equation->body.local_count;
    program->value_len = frame->value_base;
    if (__builtin_expect(
            equation->context_hole != PREPARED_PURE_NO_CONTEXT, 0) &&
        frame->node != PREPARED_PURE_RUNTIME_NODE &&
        frame->memo_index == PREPARED_PURE_NO_MEMO) {
        PreparedPureContextEntry entry = prepared_pure_enter_context(
            program, arena, frame, equation, call_base);
        if (entry == PREPARED_PURE_CONTEXT_ENTERED) {
            const PreparedPureNode *call = &program->nodes[frame->node];
            if (++continued >= PREPARED_PURE_CONTEXT_RUN ||
                call->auxiliary >= program->head_len)
                return true;
            head_index = call->auxiliary;
            arity = call->child_count;
            goto continue_call;
        }
        if (entry == PREPARED_PURE_CONTEXT_FAILED)
            return false;
    }
    frame->state = 2u;
    return prepared_pure_push_frame(
        program, equation->body.root, call_base);
}

/* This is a capability of the compiled value algebra, not a property of
 * the current arguments. In particular, a match test or an evaluated slot
 * must never acquire it merely because its last invocation was ground. */
static bool prepared_pure_open_value_program(
    const CettaPreparedPureProgram *program) {
    if (!program || program->closed_program || program->answer_producer ||
        program->call_mode != CETTA_GSLT_PURE_CALL_EAGER)
        return false;
    for (size_t index = 0u; index < program->node_len; index++) {
        const PreparedPureNode *node = &program->nodes[index];
        switch (node->kind) {
        case PREPARED_PURE_LITERAL:
            if (!node->atom || atom_has_vars(node->atom) ||
                atom_is_error(node->atom) || atom_is_petta_no_result(node->atom))
                return false;
            break;
        case PREPARED_PURE_SLOT:
            if (node->auxiliary != program->accumulator_slot &&
                node->auxiliary != program->item_slot)
                return false;
            break;
        case PREPARED_PURE_BUILD:
            break;
        case PREPARED_PURE_IF:
            if (node->child_count != 3u)
                return false;
            break;
        case PREPARED_PURE_OBSERVE:
            if (node->child_count > 1u ||
                (node->child_count == 0u &&
                 (!node->atom || atom_has_vars(node->atom))) ||
                (node->auxiliary != CETTA_PREPARED_PURE_OBSERVE_IS_VARIABLE &&
                 node->auxiliary != CETTA_PREPARED_PURE_OBSERVE_IS_EXPRESSION &&
                 node->auxiliary != PREPARED_PURE_OBSERVE_ATOM_EXPRESSION))
                return false;
            break;
        default:
            return false;
        }
    }
    return true;
}

static CettaPreparedPureProgram *prepared_pure_program_new(
    Space *space, CettaGsltPureCallMode call_mode,
    CettaPreparedPureBooleanValue boolean_value,
    CettaPreparedPureConstructValue construct_value,
    CettaPreparedPureConstructsExpression constructs_expression,
    CettaPreparedPureOpaqueValue opaque_value,
    CettaPreparedPureRegisterViewFn register_view,
    CettaPreparedPureExpressionViewFn expression_view,
    CettaPreparedPurePatternViewFn pattern_view,
    const CettaPreparedPureSourceView *source_view,
    bool total_structural_equality,
    CettaMatchDecisionSemanticIdentity match_decision_semantics,
    CettaPreparedPureCompileReport *report) {
    if (!space || !boolean_value || !construct_value ||
        (call_mode != CETTA_GSLT_PURE_CALL_EAGER &&
         call_mode != CETTA_GSLT_PURE_CALL_CALL_BY_NEED))
        return NULL;
    CettaPreparedPureProgram *program = calloc(1u, sizeof(*program));
    if (!program) {
        if (report)
            report->status = CETTA_PREPARED_PURE_COMPILE_RESOURCE_REFUSED;
        return NULL;
    }
    program->references = 1u;
    program->space = space;
    program->source_program = space_program_token(space);
    program->equation_projection = space_equation_token(space);
    program->boolean_value = boolean_value;
    if (!prepared_pure_cache_boolean_atoms(program)) {
        cetta_prepared_pure_program_free(program);
        return NULL;
    }
    program->construct_value = construct_value;
    program->constructs_expression = constructs_expression;
    program->opaque_value = opaque_value;
    program->register_view = register_view;
    program->expression_view = expression_view;
    program->pattern_view = pattern_view;
    if (source_view)
        program->source_view = *source_view;
    program->call_mode = call_mode;
    program->total_structural_equality = total_structural_equality;
    program->match_decision_semantics = match_decision_semantics;
    return program;
}

CettaPreparedPureProgram *cetta_prepared_pure_program_compile(
    Space *space, Atom *expression,
    VarId accumulator_var, VarId item_var,
    CettaGsltPureCallMode call_mode,
    CettaPreparedPureBooleanValue boolean_value,
    CettaPreparedPureConstructValue construct_value,
    CettaPreparedPureConstructsExpression constructs_expression,
    CettaPreparedPureOpaqueValue opaque_value,
    CettaPreparedPureRegisterViewFn register_view,
    CettaPreparedPureExpressionViewFn expression_view,
    CettaPreparedPurePatternViewFn pattern_view,
    const CettaPreparedPureSourceView *source_view,
    bool total_structural_equality,
    CettaMatchDecisionSemanticIdentity match_decision_semantics) {
    if (!space || !expression || var_id_is_none(accumulator_var) ||
        var_id_is_none(item_var) || var_id_eq(accumulator_var, item_var) ||
        !boolean_value || !construct_value ||
        (call_mode != CETTA_GSLT_PURE_CALL_EAGER &&
         call_mode != CETTA_GSLT_PURE_CALL_CALL_BY_NEED))
        return NULL;
    CettaPreparedPureProgram *program = prepared_pure_program_new(
        space, call_mode, boolean_value, construct_value, constructs_expression,
        opaque_value, register_view, expression_view, pattern_view, source_view,
        total_structural_equality, match_decision_semantics, NULL);
    if (!program)
        return NULL;
    PreparedPureCompileContext context = {0};
    if (!prepared_pure_context_bind(
            &context, accumulator_var, false,
            &program->accumulator_slot) ||
        !prepared_pure_context_bind(
            &context, item_var, false, &program->item_slot) ||
        !prepared_pure_compile_eval(
            program, &context, expression,
            program->source_view.root, 0u, &program->body.root)) {
        free(context.bindings);
        cetta_prepared_pure_program_free(program);
        return NULL;
    }
    program->body.local_count = context.next_slot;
    free(context.bindings);
    if (!prepared_pure_compile_pending_heads(program)) {
        cetta_prepared_pure_program_free(program);
        return NULL;
    }
    program->accepts_open_values = prepared_pure_open_value_program(program);
    return program;
}

static bool prepared_pure_native_tail_node(
        const CettaPreparedPureProgram *program, uint32_t index,
        bool tail, uint32_t depth) {
    if (index >= program->node_len || depth > PREPARED_PURE_MAX_COMPILE_DEPTH)
        return false;
    const PreparedPureNode *node = &program->nodes[index];
    if (node->kind == PREPARED_PURE_INTRINSIC &&
        node->intrinsic_instruction ==
            CETTA_GSLT_PREPARED_PURE_INTRINSIC_LIBRARY_VALUE_DISPATCH && !tail)
        return false;
    if (node->first_child > program->child_len ||
        node->child_count > program->child_len - node->first_child)
        return false;
    for (uint32_t child = 0u; child < node->child_count; child++) {
        bool child_tail = tail &&
            ((node->kind == PREPARED_PURE_BIND && child == 1u) ||
             (node->kind == PREPARED_PURE_IF && child > 0u));
        if (!prepared_pure_native_tail_node(program,
                program->children[node->first_child + child], child_tail,
                depth + 1u))
            return false;
    }
    return true;
}

bool cetta_prepared_pure_program_use_native_tail_returns(
        CettaPreparedPureProgram *program) {
    if (!program || program->closed_program || program->answer_producer ||
        program->call_mode != CETTA_GSLT_PURE_CALL_EAGER ||
        program->frame_len || program->slot_len || program->value_len ||
        !prepared_pure_native_tail_node(program, program->body.root, true, 0u))
        return false;
    /* Named callees retain their existing scalar contract. Their native
     * continuations belong to the answer cursor, not this terminal entry. */
    for (size_t index = 0u; index < program->equation_len; index++)
        if (!prepared_pure_native_tail_node(program,
                program->equations[index].body.root, false, 0u))
            return false;
    program->native_tail_returns = program->has_native_calls;
    return true;
}

/* A closed Need entry receives suspended arguments, while a CALL_READY entry
 * receives values already computed by its enclosing machine.  Neither is
 * source syntax to recursively recompile.  Entry nodes preserve that boundary
 * and make admission independent of the depth of the carried representation. */
static bool prepared_pure_compile_entry_arguments(
    CettaPreparedPureProgram *program, Atom *expression,
    PreparedPureNodeKind kind, uint32_t **children_out) {
    if (children_out)
        *children_out = NULL;
    if (!program || !expression || !children_out ||
        expression->kind != ATOM_EXPR || expression->expr.len == 0u ||
        (kind != PREPARED_PURE_ENTRY_ARGUMENT &&
         kind != PREPARED_PURE_EVAL_ENTRY_ARGUMENT) ||
        program->entry_head != SYMBOL_ID_NONE ||
        program->entry_arguments || program->entry_argument_count != 0u)
        return false;

    CettaExprLen arity = expression->expr.len - 1u;
    program->entry_arguments = arity
        ? calloc((size_t)arity, sizeof(*program->entry_arguments))
        : NULL;
    program->entry_argument_count = arity;
    if (arity > 0u && !program->entry_arguments)
        return false;

    uint32_t *children = arity
        ? malloc(sizeof(*children) * (size_t)arity)
        : NULL;
    if (arity > 0u && !children)
        return false;
    for (CettaExprIndex i = 0u; i < arity; i++) {
        if (!prepared_pure_add_node(
                program,
                (PreparedPureNode){
                    .kind = kind,
                    .auxiliary = i,
                },
                NULL, 0u, &children[i])) {
            free(children);
            return false;
        }
        program->entry_arguments[i] = expression->expr.elems[i + 1u];
    }
    *children_out = children;
    return true;
}

static bool prepared_pure_compile_closed_entry_call(
    CettaPreparedPureProgram *program, Atom *expression,
    bool entry_arguments_are_values,
    bool *admitted, uint32_t *root_out) {
    if (admitted)
        *admitted = false;
    if (!program || !expression || !admitted || !root_out ||
        (program->call_mode != CETTA_GSLT_PURE_CALL_CALL_BY_NEED &&
         !entry_arguments_are_values) ||
        expression->kind != ATOM_EXPR || expression->expr.len == 0u ||
        !expression->expr.elems[0] ||
        expression->expr.elems[0]->kind != ATOM_SYMBOL)
        return true;

    CettaPreparedPureSourceRole source_role =
        prepared_pure_source_role(
            program, program->source_view.root);
    if (source_role == CETTA_PREPARED_PURE_SOURCE_VALUE ||
        source_role == CETTA_PREPARED_PURE_SOURCE_DATA)
        return true;
    if (source_role == CETTA_PREPARED_PURE_SOURCE_DYNAMIC_CALL ||
        source_role == CETTA_PREPARED_PURE_SOURCE_DECLINE)
        return false;

    /* A retained control word belongs to the per-expression lowerer, before
     * a dialect projection considers a reusable invocation. Captured operands
     * keep their individual source roles through that lowering. */
    if (cetta_control_operation_is_control_form(prepared_pure_source_computation(
            program, program->source_view.root).control))
        return true;

    /* Entry arguments being values does not make the operator an equation
     * call.  Apply the same dialect interpretation as compile_eval before
     * installing the reusable entry-call instruction.  A projection retains
     * the entry's value/demand discipline; recompiling an already evaluated
     * child as source could execute callable-looking data a second time. */
    if (program->expression_view) {
        CettaPreparedPureExpressionView view = {0};
        CettaPreparedPureExpressionViewState state =
            prepared_pure_describe_expression(program, expression,
                program->source_view.root, &view);
        if (state == CETTA_PREPARED_PURE_EXPRESSION_DATA)
            return true;
        if (state == CETTA_PREPARED_PURE_EXPRESSION_PROJECT) {
            /* An entry value cannot stand for evaluation as code or for
             * dispatch of an application. Compile those boundaries from
             * the expression instead of aliasing an argument register. */
            if (view.projection_mode != CETTA_PREPARED_PURE_PROJECT_SOURCE)
                return true;
            for (CettaExprIndex index = 1u;
                 index < expression->expr.len; index++) {
                if (expression->expr.elems[index] != view.projected)
                    continue;
                uint32_t *children = NULL;
                *admitted = true;
                if (!prepared_pure_compile_entry_arguments(
                        program, expression,
                        entry_arguments_are_values
                            ? PREPARED_PURE_ENTRY_ARGUMENT
                            : PREPARED_PURE_EVAL_ENTRY_ARGUMENT,
                        &children))
                    return false;
                *root_out = children[index - 1u];
                free(children);
                program->entry_head = expression->expr.elems[0]->sym_id;
                return true;
            }
            return prepared_pure_reject(
                program, "dialect projection is not an entry argument",
                expression);
        }
        if (state != CETTA_PREPARED_PURE_EXPRESSION_DEFAULT)
            return prepared_pure_reject(
                program, "dialect-owned entry requires canonical evaluation",
                expression);
    }

    SymbolId head = expression->expr.elems[0]->sym_id;
    CettaExprLen arity = expression->expr.len - 1u;
    CettaControlOperationPlan control = prepared_pure_control_plan(
        program, expression, program->source_view.root);
    if (cetta_control_operation_is_control_form(control)) {
        /* A Need control's operands are invocation data while its scheduling
         * discipline is generated language semantics.  Compile that fixed
         * discipline once and demand only the operands selected at runtime.
         * Bind carries a pattern/environment extension and therefore remains
         * on the ordinary per-expression compiler until it has an equally
         * general parameter representation. */
        if (program->call_mode != CETTA_GSLT_PURE_CALL_CALL_BY_NEED ||
            (control.operation != CETTA_CONTROL_BRANCH &&
             control.operation != CETTA_CONTROL_FORCE &&
             control.operation != CETTA_CONTROL_RETURN_SCOPE))
            return true;

        uint32_t *children = NULL;
        *admitted = true;
        if (!prepared_pure_compile_entry_arguments(
                program, expression,
                PREPARED_PURE_EVAL_ENTRY_ARGUMENT, &children))
            return false;
        bool compiled = false;
        if (control.operation == CETTA_CONTROL_BRANCH &&
            prepared_pure_control_three_places(control, arity)) {
            uint32_t branch[] = {children[control.source],
                children[control.first], children[control.last]};
            compiled = prepared_pure_add_node(
                program,
                (PreparedPureNode){.kind = PREPARED_PURE_IF},
                branch, 3u, root_out);
        } else if ((control.operation == CETTA_CONTROL_FORCE ||
                    control.operation == CETTA_CONTROL_RETURN_SCOPE) &&
                   arity == 1u && control.source == 0u) {
            *root_out = children[control.source];
            compiled = true;
        }
        free(children);
        if (compiled)
            program->entry_head = head;
        return compiled;
    }
    /* Generated value rows are expressions, not equation-defined entry
     * relations.  Let compile_eval lower their concrete occurrences. */
    if (prepared_pure_register_program(
            program, head, arity, NULL, NULL) ||
        cetta_prepared_pure_intrinsic_program(head, arity, NULL)) {
        return true;
    }
    bool defined = false;
    if (space_query_effect_for_head(
            program->space, head, &defined) !=
            CETTA_GSLT_QUERY_EFFECT_PURE ||
        !defined)
        return true;

    if (!cetta_prepared_pure_call_policy_supported(
            program->space, head, arity))
        return true;
    *admitted = true;
    uint32_t head_index = 0u;
    if (!prepared_pure_head_index_admitted(
            program, head, &head_index))
        return false;
    if (!prepared_pure_head_call_policy(program, head_index, arity))
        return prepared_pure_reject(
            program, "call policy is unsupported", expression);

    uint32_t *children = NULL;
    if (!prepared_pure_compile_entry_arguments(
            program, expression, PREPARED_PURE_ENTRY_ARGUMENT,
            &children))
        return false;
    bool compiled = prepared_pure_add_node(
        program,
        (PreparedPureNode){
            .kind = PREPARED_PURE_CALL,
            .head = head,
            .auxiliary = head_index,
            .call_arguments_are_values = entry_arguments_are_values,
        },
        children, arity, root_out);
    free(children);
    if (compiled)
        program->entry_head = head;
    return compiled;
}

static bool prepared_pure_answer_value_node(
    const CettaPreparedPureProgram *program,
    const PreparedPureEquation *equation, uint32_t node_index,
    uint32_t depth) {
    if (!program || !equation || node_index >= program->node_len ||
        depth > PREPARED_PURE_MAX_COMPILE_DEPTH)
        return false;
    const PreparedPureNode *node = &program->nodes[node_index];
    if (node->kind == PREPARED_PURE_LITERAL)
        return node->atom != NULL;
    if (node->kind == PREPARED_PURE_SLOT)
        return node->auxiliary < equation->body.local_count;
    if (node->kind != PREPARED_PURE_BUILD ||
        node->first_child > program->child_len ||
        node->child_count > program->child_len - node->first_child)
        return false;
    for (uint32_t index = 0u; index < node->child_count; index++) {
        if (!prepared_pure_answer_value_node(
                program, equation,
                program->children[node->first_child + index],
                depth + 1u))
            return false;
    }
    return true;
}

/* A node whose evaluation cannot choose: it holds no choice zero and calls
 * only deterministic heads. */
/* Whether a node can produce more than one answer: a zero, a call to a
 * head that is not deterministic, or such a node below it. */
static bool prepared_pure_node_chooses(
    const CettaPreparedPureProgram *program, uint32_t node_index,
    uint32_t depth) {
    if (!program || node_index >= program->node_len ||
        depth > PREPARED_PURE_MAX_COMPILE_DEPTH)
        return true;
    const PreparedPureNode *node = &program->nodes[node_index];
    /* After native entry, each remaining operation needs an exact ready
     * invocation boundary. Keep its already-computed operands in slots. */
    if (program->has_native_calls && program->answer_producer &&
        node->kind != PREPARED_PURE_LITERAL && node->kind != PREPARED_PURE_SLOT &&
        node->kind != PREPARED_PURE_ENTRY_ARGUMENT && node->kind != PREPARED_PURE_BUILD)
        return true;
    if (node->kind == PREPARED_PURE_ZERO)
        return true;
    if (node->kind == PREPARED_PURE_INTRINSIC &&
        node->intrinsic_instruction ==
            CETTA_GSLT_PREPARED_PURE_INTRINSIC_LIBRARY_VALUE_DISPATCH)
        return true;
    if (node->kind == PREPARED_PURE_CALL &&
        (node->auxiliary >= program->head_len ||
         !program->heads[node->auxiliary].deterministic))
        return true;
    if (node->first_child > program->child_len ||
        node->child_count > program->child_len - node->first_child)
        return true;
    for (uint32_t index = 0u; index < node->child_count; index++) {
        if (prepared_pure_node_chooses(
                program, program->children[node->first_child + index],
                depth + 1u))
            return true;
    }
    return false;
}

/* One equation tree's part of the classification: whether it can choose
 * by itself (a zero, a call outside the program, a malformed or too deep
 * node), and the heads it calls, appended to `calls`. */
typedef struct {
    size_t *items;
    size_t len;
    size_t cap;
    bool failed;
} PreparedPureCalls;

static bool prepared_pure_tree_chooses_locally(
    const CettaPreparedPureProgram *program, uint32_t node_index,
    uint32_t depth, PreparedPureCalls *calls) {
    if (!program || node_index >= program->node_len ||
        depth > PREPARED_PURE_MAX_COMPILE_DEPTH)
        return true;
    const PreparedPureNode *node = &program->nodes[node_index];
    if (node->kind == PREPARED_PURE_ZERO)
        return true;
    if (node->kind == PREPARED_PURE_INTRINSIC &&
        node->intrinsic_instruction ==
            CETTA_GSLT_PREPARED_PURE_INTRINSIC_LIBRARY_VALUE_DISPATCH)
        return true;
    if (node->kind == PREPARED_PURE_CALL) {
        if (node->auxiliary >= program->head_len)
            return true;
        if (calls->len == calls->cap) {
            size_t cap = calls->cap ? calls->cap * 2u : 16u;
            size_t *items = realloc(calls->items, cap * sizeof(*items));
            if (!items) {
                calls->failed = true;
                return true;
            }
            calls->items = items;
            calls->cap = cap;
        }
        calls->items[calls->len++] = node->auxiliary;
    }
    if (node->first_child > program->child_len ||
        node->child_count > program->child_len - node->first_child)
        return true;
    for (uint32_t index = 0u; index < node->child_count; index++) {
        if (prepared_pure_tree_chooses_locally(
                program, program->children[node->first_child + index],
                depth + 1u, calls))
            return true;
    }
    return false;
}

/* The greatest fixed point (fixpoint.h): a head is deterministic when weak-
 * head matching makes it determinate, no equation body can choose by
 * itself, and every head its bodies call is deterministic.  Should the
 * graph not fit in memory, no head is. */
static void prepared_pure_classify_deterministic_heads(
    CettaPreparedPureProgram *program) {
    size_t heads = program->head_len;
    size_t *first = malloc((heads + 1u) * sizeof(*first));
    bool *admitted = malloc((heads ? heads : 1u) * sizeof(*admitted));
    PreparedPureCalls calls = {0};
    bool ok = first && admitted;
    for (uint32_t index = 0u; ok && index < heads; index++) {
        PreparedPureHead *head = &program->heads[index];
        first[index] = calls.len;
        admitted[index] = head->compiled &&
            prepared_pure_head_is_whnf_determinate(program, head);
        head->unique_match = admitted[index];
        for (uint32_t offset = 0u;
             admitted[index] && offset < head->equation_count; offset++) {
            const PreparedPureEquation *equation =
                &program->equations[head->first_equation + offset];
            if (prepared_pure_tree_chooses_locally(
                    program, equation->body.root, 0u, &calls))
                admitted[index] = false;
        }
        ok = !calls.failed;
    }
    if (ok) {
        first[heads] = calls.len;
        CettaFixpointGraph graph = {heads, first, calls.items};
        ok = cetta_fixpoint_greatest(&graph, admitted);
    }
    for (uint32_t index = 0u; index < heads; index++)
        program->heads[index].deterministic = ok && admitted[index];
    free(first);
    free(admitted);
    free(calls.items);
}

/* A value tree evaluates to itself: literals and locals under constructors,
 * with no computation to order. */
static bool prepared_pure_value_tree(
    const CettaPreparedPureProgram *program, uint32_t node_index,
    uint32_t depth) {
    if (!program || node_index >= program->node_len ||
        depth > PREPARED_PURE_MAX_COMPILE_DEPTH)
        return false;
    const PreparedPureNode *node = &program->nodes[node_index];
    if (node->kind == PREPARED_PURE_LITERAL)
        return node->atom != NULL;
    if (node->kind == PREPARED_PURE_SLOT)
        return true;
    if (node->kind != PREPARED_PURE_BUILD ||
        node->first_child > program->child_len ||
        node->child_count > program->child_len - node->first_child)
        return false;
    for (uint32_t index = 0u; index < node->child_count; index++) {
        if (!prepared_pure_value_tree(
                program, program->children[node->first_child + index],
                depth + 1u))
            return false;
    }
    return true;
}

/* One enclosing construct of the node being lowered: its copy, the child
 * position the node occupies, and the operands already lowered before it. */
typedef struct {
    PreparedPureNode node;
    uint32_t hole;
    const uint32_t *before;
    const void *source;
} PreparedPureZipperFrame;

typedef struct {
    CettaPreparedPureProgram *program;
    uint32_t next_slot;
    PreparedPureZipperFrame *zipper;
    size_t zipper_len;
    size_t zipper_cap;
} PreparedPureLowering;

static bool prepared_pure_zipper_push(
    PreparedPureLowering *lowering, const PreparedPureNode *node,
    uint32_t hole, const uint32_t *before) {
    if (!prepared_pure_reserve(
            (void **)&lowering->zipper, sizeof(*lowering->zipper),
            &lowering->zipper_cap, lowering->zipper_len + 1u))
        return false;
    lowering->zipper[lowering->zipper_len++] = (PreparedPureZipperFrame){
        .node = *node, .hole = hole, .before = before,
        .source = lowering->program->compiling_source,
    };
    return true;
}

static void prepared_pure_zipper_pop(PreparedPureLowering *lowering) {
    if (lowering->zipper_len > 0u)
        lowering->zipper_len--;
}

/* The rest of the body once a call's answer is in `slot`: rebuild each
 * enclosing construct around the hole, innermost first.  Operands lowered
 * before the hole are the nodes that read their values; later children are
 * the source nodes still to run. */
static uint32_t prepared_pure_resume_template(
    PreparedPureLowering *lowering, uint32_t slot) {
    CettaPreparedPureProgram *program = lowering->program;
    uint32_t current = 0u;
    if (!prepared_pure_add_node(
            program,
            (PreparedPureNode){.kind = PREPARED_PURE_SLOT, .auxiliary = slot},
            NULL, 0u, &current))
        return PREPARED_PURE_NO_TEMPLATE;
    for (size_t index = lowering->zipper_len; index-- > 0u;) {
        const PreparedPureZipperFrame *frame = &lowering->zipper[index];
        uint32_t count = frame->node.child_count;
        if (frame->hole >= count ||
            frame->node.first_child > program->child_len ||
            count > program->child_len - frame->node.first_child)
            return PREPARED_PURE_NO_TEMPLATE;
        uint32_t *children = malloc(sizeof(*children) * count);
        if (!children)
            return PREPARED_PURE_NO_TEMPLATE;
        for (uint32_t child = 0u; child < count; child++) {
            if (child == frame->hole)
                children[child] = current;
            else if (child < frame->hole && frame->before)
                children[child] = frame->before[child];
            else
                children[child] =
                    program->children[frame->node.first_child + child];
        }
        const void *saved_source = program->compiling_source;
        program->compiling_source = frame->source;
        bool ok = prepared_pure_add_node(
            program, frame->node, children, count, &current);
        program->compiling_source = saved_source;
        free(children);
        if (!ok)
            return PREPARED_PURE_NO_TEMPLATE;
    }
    return current;
}

static bool prepared_pure_emit_step(
    PreparedPureLowering *lowering, PreparedPureStep step,
    uint32_t *index_out) {
    CettaPreparedPureProgram *program = lowering->program;
    if (program->step_len >= UINT32_MAX ||
        !prepared_pure_reserve(
            (void **)&program->steps, sizeof(*program->steps),
            &program->step_cap, program->step_len + 1u))
        return false;
    if (index_out)
        *index_out = (uint32_t)program->step_len;
    program->steps[program->step_len++] = step;
    return true;
}

static bool prepared_pure_lower_node(
    PreparedPureLowering *lowering, uint32_t node_index, bool tail,
    uint32_t destination, uint32_t depth);

/* The slot holding a child's value once its steps have run. */
static bool prepared_pure_lower_to_slot(
    PreparedPureLowering *lowering, uint32_t child, uint32_t depth,
    uint32_t *slot_out) {
    CettaPreparedPureProgram *program = lowering->program;
    if (child < program->node_len &&
        program->nodes[child].kind == PREPARED_PURE_SLOT) {
        *slot_out = program->nodes[child].auxiliary;
        return true;
    }
    if (lowering->next_slot >= PREPARED_PURE_MAX_SLOTS)
        return false;
    uint32_t temporary = lowering->next_slot++;
    if (!prepared_pure_lower_node(
            lowering, child, false, temporary, depth + 1u))
        return false;
    *slot_out = temporary;
    return true;
}

/* A node standing for a child once its steps have run: a value tree stays
 * in place, since it has no evaluation to order; anything else becomes a
 * read of the temporary its steps fill. */
static bool prepared_pure_lower_operand(
    PreparedPureLowering *lowering, uint32_t child, uint32_t depth,
    uint32_t *node_out) {
    CettaPreparedPureProgram *program = lowering->program;
    if (prepared_pure_value_tree(program, child, 0u)) {
        *node_out = child;
        return true;
    }
    uint32_t slot = 0u;
    return prepared_pure_lower_to_slot(lowering, child, depth, &slot) &&
        prepared_pure_add_node(
            program,
            (PreparedPureNode){.kind = PREPARED_PURE_SLOT, .auxiliary = slot},
            NULL, 0u, node_out);
}

static bool prepared_pure_lower_operands(
    PreparedPureLowering *lowering, const PreparedPureNode *node,
    uint32_t depth, uint32_t **operands_out) {
    CettaPreparedPureProgram *program = lowering->program;
    uint32_t count = node->child_count;
    uint32_t first = node->first_child;
    uint32_t *operands = malloc(sizeof(*operands) * (count ? count : 1u));
    if (!operands)
        return false;
    for (uint32_t index = 0u; index < count; index++) {
        bool ok = prepared_pure_zipper_push(lowering, node, index, operands) &&
            prepared_pure_lower_operand(
                lowering, program->children[first + index], depth,
                &operands[index]);
        prepared_pure_zipper_pop(lowering);
        if (!ok) {
            free(operands);
            return false;
        }
    }
    *operands_out = operands;
    return true;
}

/* Whether an if's then branch binds the pattern its condition tests, to
 * the value the test reads: the test and the bind are then one match.
 * `*body_out` is the bind's body. */
static bool prepared_pure_test_binds(
    const CettaPreparedPureProgram *program,
    const PreparedPureNode *condition, uint32_t then_node,
    uint32_t *body_out) {
    if ((condition->auxiliary & PREPARED_PURE_OBSERVE_PATTERN_TEST) == 0u ||
        condition->child_count != 1u ||
        condition->first_child >= program->child_len ||
        then_node >= program->node_len)
        return false;
    uint32_t tested =
        condition->auxiliary & ~PREPARED_PURE_OBSERVE_PATTERN_TEST;
    const PreparedPureNode *then = &program->nodes[then_node];
    if (tested >= program->bind_pattern_len ||
        then->kind != PREPARED_PURE_BIND || then->child_count != 2u ||
        then->auxiliary >= program->bind_pattern_len ||
        then->first_child > program->child_len ||
        2u > program->child_len - then->first_child ||
        program->children[then->first_child] !=
            program->children[condition->first_child] ||
        program->bind_patterns[then->auxiliary].pattern !=
            program->bind_patterns[tested].pattern)
        return false;
    *body_out = program->children[then->first_child + 1u];
    return true;
}
/* Lower one node of an answer producer's body into steps, in the order the
 * executor evaluates it: operands left to right, a condition before its
 * selected branch, a bound expression before its pattern and body.  A node
 * that cannot choose stays one executor step.  In tail position the node's
 * answers are the body's; otherwise its value lands in `destination`. */
static bool prepared_pure_lower_node_in_source(
    PreparedPureLowering *lowering, uint32_t node_index, bool tail,
    uint32_t destination, uint32_t depth);

static bool prepared_pure_lower_node(
    PreparedPureLowering *lowering, uint32_t node_index, bool tail,
    uint32_t destination, uint32_t depth) {
    CettaPreparedPureProgram *program = lowering->program;
    if (!program)
        return false;
    if (!program->answer_producer || !program->source_view.project_source)
        return prepared_pure_lower_node_in_source(lowering, node_index, tail, destination, depth);
    const void *saved = program->compiling_source;
    program->compiling_source = program->node_sources && node_index < program->node_len
        ? program->node_sources[node_index] : NULL;
    bool ok = prepared_pure_lower_node_in_source(lowering, node_index, tail, destination, depth);
    program->compiling_source = saved;
    return ok;
}

static bool prepared_pure_lower_node_in_source(
    PreparedPureLowering *lowering, uint32_t node_index, bool tail,
    uint32_t destination, uint32_t depth) {
    CettaPreparedPureProgram *program = lowering->program;
    if (node_index >= program->node_len ||
        depth > PREPARED_PURE_MAX_COMPILE_DEPTH)
        return false;
    if (!prepared_pure_node_chooses(program, node_index, 0u))
        return prepared_pure_emit_step(
            lowering,
            (PreparedPureStep){
                .kind = tail ? PREPARED_PURE_STEP_RETURN
                             : PREPARED_PURE_STEP_EVAL,
                .value_tree =
                    prepared_pure_value_tree(program, node_index, 0u),
                .node = node_index,
                .slot = destination,
            },
            NULL);
    /* Nodes are appended below; work from a copy. */
    PreparedPureNode node = program->nodes[node_index];
    if (node.first_child > program->child_len ||
        node.child_count > program->child_len - node.first_child)
        return false;
    const uint32_t *children = &program->children[node.first_child];
    switch (node.kind) {
    case PREPARED_PURE_ZERO:
        return prepared_pure_emit_step(
            lowering, (PreparedPureStep){.kind = PREPARED_PURE_STEP_FAIL},
            NULL);
    case PREPARED_PURE_IF: {
        if (node.child_count != 3u)
            return false;
        uint32_t then_node = children[1];
        uint32_t else_node = children[2];
        uint32_t condition = 0u;
        uint32_t branch = 0u;
        uint32_t join = 0u;
        uint32_t condition_node = children[0];
        uint32_t bind_body = 0u;
        uint32_t bind_pattern = 0u;
        bool match_branch = condition_node < program->node_len &&
            program->nodes[condition_node].kind == PREPARED_PURE_OBSERVE &&
            prepared_pure_test_binds(program, &program->nodes[condition_node],
                                     then_node, &bind_body);
        /* No call may enter while preparing the tested value. Its binding
         * is installed before the selected body; a mismatch leaves the
         * original locals for the next arm. */
        if (match_branch) {
            bind_pattern = program->nodes[then_node].auxiliary;
            const PreparedPureNode *test = &program->nodes[condition_node];
            condition_node = program->children[test->first_child];
            match_branch = prepared_pure_value_tree(program, condition_node, 0u);
            if (match_branch)
                then_node = bind_body;
            else
                condition_node = children[0];
        }
        bool lowered_condition =
            prepared_pure_zipper_push(lowering, &node, 0u, NULL) &&
            prepared_pure_lower_to_slot(
                lowering, condition_node, depth, &condition);
        prepared_pure_zipper_pop(lowering);
        if (!lowered_condition ||
            !prepared_pure_emit_step(
                lowering,
                (PreparedPureStep){
                    .kind = match_branch ? PREPARED_PURE_STEP_MATCH_ELSE
                                         : PREPARED_PURE_STEP_BRANCH,
                    .slot = condition, .auxiliary = bind_pattern},
                &branch) ||
            !prepared_pure_lower_node(
                lowering, then_node, tail, destination, depth + 1u) ||
            (!tail &&
             !prepared_pure_emit_step(
                 lowering,
                 (PreparedPureStep){.kind = PREPARED_PURE_STEP_JUMP},
                 &join)))
            return false;
        program->steps[branch].target = (uint32_t)program->step_len;
        if (!prepared_pure_lower_node(
                lowering, else_node, tail, destination, depth + 1u))
            return false;
        if (!tail)
            program->steps[join].target = (uint32_t)program->step_len;
        return true;
    }
    case PREPARED_PURE_BIND: {
        if (node.child_count != 2u)
            return false;
        uint32_t body = children[1];
        uint32_t bound = 0u;
        bool lowered_bound =
            prepared_pure_zipper_push(lowering, &node, 0u, NULL) &&
            prepared_pure_lower_to_slot(
                lowering, children[0], depth, &bound);
        prepared_pure_zipper_pop(lowering);
        return lowered_bound &&
            prepared_pure_emit_step(
                lowering,
                (PreparedPureStep){
                    .kind = PREPARED_PURE_STEP_MATCH,
                    .slot = bound,
                    .auxiliary = node.auxiliary,
                },
                NULL) &&
            prepared_pure_lower_node(
                lowering, body, tail, destination, depth + 1u);
    }
    case PREPARED_PURE_CALL:
        if (node.auxiliary >= program->head_len)
            return false;
        if (program->has_native_calls || !program->heads[node.auxiliary].deterministic) {
            uint32_t *operands = NULL;
            if (!prepared_pure_lower_operands(
                    lowering, &node, depth, &operands))
                return false;
            bool ok = program->step_argument_len <=
                    UINT32_MAX - node.child_count &&
                prepared_pure_reserve(
                    (void **)&program->step_arguments,
                    sizeof(*program->step_arguments),
                    &program->step_argument_cap,
                    program->step_argument_len + node.child_count);
            uint32_t first_argument = (uint32_t)program->step_argument_len;
            if (ok) {
                if (node.child_count > 0u)
                    memcpy(&program->step_arguments[first_argument],
                           operands,
                           sizeof(*operands) * node.child_count);
                program->step_argument_len += node.child_count;
            }
            free(operands);
            uint32_t resume_template = ok && !tail
                ? prepared_pure_resume_template(lowering, destination)
                : PREPARED_PURE_NO_TEMPLATE;
            return ok && prepared_pure_emit_step(
                lowering,
                (PreparedPureStep){
                    .kind = tail ? PREPARED_PURE_STEP_TAIL
                                 : PREPARED_PURE_STEP_CALL,
                    .slot = destination,
                    .auxiliary = node.auxiliary,
                    .first_argument = first_argument,
                    .arity = node.child_count,
                    .resume_template = resume_template,
                },
                NULL);
        }
        /* fall through: only the operands choose */
    case PREPARED_PURE_BUILD:
    case PREPARED_PURE_REGISTER:
    case PREPARED_PURE_INTRINSIC:
    case PREPARED_PURE_OBSERVE: {
        uint32_t *operands = NULL;
        uint32_t lowered = 0u;
        if (!prepared_pure_lower_operands(
                lowering, &node, depth, &operands))
            return false;
        bool ok = prepared_pure_add_node(
            program, node, operands, node.child_count, &lowered);
        free(operands);
        bool native = node.kind == PREPARED_PURE_INTRINSIC &&
            node.intrinsic_instruction ==
                CETTA_GSLT_PREPARED_PURE_INTRINSIC_LIBRARY_VALUE_DISPATCH;
        uint32_t resume_template = ok && native && !tail
            ? prepared_pure_resume_template(lowering, destination)
            : PREPARED_PURE_NO_TEMPLATE;
        return ok && prepared_pure_emit_step(
            lowering,
            (PreparedPureStep){
                .kind = native ? PREPARED_PURE_STEP_NATIVE
                    : tail ? PREPARED_PURE_STEP_RETURN : PREPARED_PURE_STEP_EVAL,
                .value_tree = prepared_pure_value_tree(program, lowered, 0u),
                .node = lowered,
                .slot = destination,
                .auxiliary = tail,
                .resume_template = resume_template,
            },
            NULL);
    }
    default:
        return prepared_pure_reject(
            program, "node kind has no step lowering", node.atom);
    }
}

/* A value tree, or a last call over value trees, answers directly. */
/* A program-owned possible output of a pure value plan. Source head
 * variables retain their identities and stand for unknown ready operands. */
static Atom *prepared_pure_answer_shape(CettaPreparedPureProgram *program,
        const PreparedPureEquation *equation, uint32_t index, uint32_t depth) {
    if (index >= program->node_len || depth > PREPARED_PURE_MAX_COMPILE_DEPTH)
        return NULL;
    const PreparedPureNode *node = &program->nodes[index];
    if (node->kind == PREPARED_PURE_LITERAL) {
        Atom *literal = node->atom;
        return literal && !atom_has_vars(literal) && !atom_has_identity_grounded(literal) &&
            !atom_has_thread_local_resource(literal) &&
            !atom_structural_may_have_list_carrier(literal) &&
            (!program->opaque_value || !program->opaque_value(literal)) ? literal : NULL;
    }
    Arena *arena = prepared_pure_syntax_arena(program);
    if (!arena)
        return NULL;
    if (node->kind == PREPARED_PURE_SLOT) {
        for (uint32_t variable = 0u; variable < equation->pattern_var_count; variable++) {
            const PreparedPureVarSlot *binding =
                &program->pattern_vars[equation->first_pattern_var + variable];
            if (binding->slot == node->auxiliary)
                return atom_var_with_id(arena, "$__result", binding->var);
        }
        return NULL;
    }
    if (node->kind != PREPARED_PURE_BUILD || !node->child_count || node->child_count > 16u ||
        node->first_child > program->child_len ||
        node->child_count > program->child_len - node->first_child)
        return NULL;
    const PreparedPureNode *head = &program->nodes[program->children[node->first_child]];
    if (head->kind != PREPARED_PURE_LITERAL || !program->constructs_expression ||
        !program->constructs_expression(head->atom, node->child_count))
        return NULL;
    Atom *parts[16];
    for (uint32_t child = 0u; child < node->child_count; child++) {
        parts[child] = prepared_pure_answer_shape(program, equation,
            program->children[node->first_child + child], depth + 1u);
        if (!parts[child])
            return NULL;
    }
    return atom_expr(arena, parts, node->child_count);
}

static bool prepared_pure_equation_tail_only(
    const CettaPreparedPureProgram *program,
    const PreparedPureEquation *equation) {
    if (prepared_pure_answer_value_node(
            program, equation, equation->body.root, 0u))
        return true;
    const PreparedPureNode *root = &program->nodes[equation->body.root];
    if (root->kind != PREPARED_PURE_CALL || !root->tail_position ||
        root->auxiliary >= program->head_len ||
        root->first_child > program->child_len ||
        root->child_count > program->child_len - root->first_child)
        return false;
    for (uint32_t child = 0u; child < root->child_count; child++) {
        if (!prepared_pure_answer_value_node(
                program, equation,
                program->children[root->first_child + child], 0u))
            return false;
    }
    return true;
}

/* This is an answer-effect capability check, not a second evaluator.  An
 * equation whose body is a value tree, or a last call over value trees,
 * answers directly.  Every other body is lowered to a step program whose
 * deterministic regions stay executor evaluations; a failed lowering leaves
 * the canonical evaluator authoritative. */
static bool prepared_pure_lower_body(
        CettaPreparedPureProgram *program, PreparedPureBody *body) {
    PreparedPureLowering lowering = {
        .program = program,
        .next_slot = body->local_count,
    };
    size_t first_step = program->step_len;
    bool lowered = first_step <= UINT32_MAX &&
        prepared_pure_lower_node(&lowering, body->root, true, 0u, 0u);
    free(lowering.zipper);
    if (!lowered)
        return prepared_pure_reject(
            program, "answer body has no step lowering", NULL);
    body->first_step = (uint32_t)first_step;
    body->step_count = (uint32_t)(program->step_len - first_step);
    body->frame_slot_count = lowering.next_slot;
    program->continuation_steps = true;
    return true;
}

/* Project the normalized reading before execution can mutate declarations.
 * Children precede their parents. The adapter preserves source occurrences,
 * while introduced value ports have no executable head to rediscover. */
static __attribute__((cold, noinline)) bool prepared_pure_project_sources(CettaPreparedPureProgram *program) {
    if (!program->node_sources)
        return true;
    CettaPreparedPureSourceProjection projection = {
        .role = CETTA_PREPARED_PURE_SOURCE_VALUE,
        .computation.control = CETTA_CONTROL_RETURN_VALUE_PLAN,
    };
    const void *value = program->source_view.project_source(
        program->source_view.context, &projection);
    if (!value)
        return false;
    const void **children = NULL;
    size_t capacity = 0u;
    bool ok = true;
    for (; ok && program->projected_sources < program->node_len; program->projected_sources++) {
        size_t index = program->projected_sources;
        const PreparedPureNode *node = &program->nodes[index];
        const void *source = program->node_sources[index];
        projection = (CettaPreparedPureSourceProjection){
            .source = source, .role = CETTA_PREPARED_PURE_SOURCE_CALL,
        };
        uint32_t count = node->child_count, offset = 0u;
        SymbolId head = SYMBOL_ID_NONE;
        switch (node->kind) {
        case PREPARED_PURE_LITERAL:
        case PREPARED_PURE_SLOT:
            projection.role = CETTA_PREPARED_PURE_SOURCE_VALUE;
            projection.computation.control = CETTA_CONTROL_RETURN_VALUE_PLAN;
            count = 0u;
            break;
        case PREPARED_PURE_BUILD:
            projection.role = node->auxiliary == PREPARED_PURE_BUILD_DATA_HEAD
                ? CETTA_PREPARED_PURE_SOURCE_DYNAMIC_CALL : CETTA_PREPARED_PURE_SOURCE_DATA;
            projection.computation.control = node->auxiliary == PREPARED_PURE_BUILD_DATA_HEAD
                ? CETTA_CONTROL_INVOKE_PLAN : CETTA_CONTROL_CONSTRUCT_PLAN;
            break;
        case PREPARED_PURE_CALL:
            projection.relation_head_admitted = true;
            head = node->head;
            offset = 1u;
            projection.computation.control = CETTA_CONTROL_INVOKE_PLAN;
            break;
        case PREPARED_PURE_REGISTER:
        case PREPARED_PURE_INTRINSIC:
            head = node->head;
            offset = 1u;
            projection.computation.control = CETTA_CONTROL_INVOKE_PLAN;
            cetta_invocation_plan_decode(head, count, &projection.computation.invocation, &g_builtin_syms);
            break;
        case PREPARED_PURE_BIND:
            if (count != 2u) { ok = false; break; }
            head = symbol_intern_cstr(g_symbols, "let");
            offset = 2u;
            cetta_control_operation_decode(head, 3u, &projection.computation.control, &g_builtin_syms);
            break;
        case PREPARED_PURE_IF:
            if (count != 3u) { ok = false; break; }
            head = symbol_intern_cstr(g_symbols, "if");
            offset = 1u;
            cetta_control_operation_decode(head, 3u, &projection.computation.control, &g_builtin_syms);
            break;
        case PREPARED_PURE_ZERO:
            head = symbol_intern_cstr(g_symbols, "empty");
            offset = 1u;
            count = 0u;
            cetta_control_operation_decode(head, 0u, &projection.computation.control, &g_builtin_syms);
            break;
        default:
            program->node_sources[index] = NULL;
            continue;
        }
        if (!ok || count > UINT32_MAX - offset ||
            !prepared_pure_reserve((void **)&children, sizeof(*children), &capacity, count + offset)) {
            ok = false;
            break;
        }
        for (uint32_t child = 0u; child < offset; child++)
            children[child] = value;
        bool readable = !offset || head != SYMBOL_ID_NONE;
        if (count && (node->first_child > program->child_len ||
            count > program->child_len - node->first_child)) { ok = false; break; }
        for (uint32_t child = 0u; child < count; child++) {
            uint32_t prior = program->children[node->first_child + child];
            children[offset + child] = prior < index ? program->node_sources[prior] : NULL;
            readable &= children[offset + child] != NULL;
        }
        projection.children = children;
        projection.child_count = count + offset;
        program->node_sources[index] = readable ? program->source_view.project_source(
            program->source_view.context, &projection) : NULL;
    }
    free(children);
    program->caller_plans_complete = ok;
    for (size_t index = 0u; ok && index < program->step_len; index++) {
        uint32_t resume = program->steps[index].resume_template;
        if ((program->steps[index].kind == PREPARED_PURE_STEP_CALL ||
             program->steps[index].kind == PREPARED_PURE_STEP_NATIVE) &&
            resume != PREPARED_PURE_NO_TEMPLATE &&
            (resume >= program->projected_sources || !program->node_sources[resume]))
            program->caller_plans_complete = false;
    }
    return ok && program->caller_plans_complete;
}

static bool prepared_pure_lower_answer_producer(
    CettaPreparedPureProgram *program) {
    if (!program || !program->answer_producer ||
        !program->closed_program ||
        program->call_mode != CETTA_GSLT_PURE_CALL_EAGER ||
        program->body.root >= program->node_len ||
        program->entry_argument_count > 64u)
        return false;
    const PreparedPureNode *entry = &program->nodes[program->body.root];
    if (!program->body_entry && (entry->kind != PREPARED_PURE_CALL ||
        entry->head != program->entry_head ||
        entry->auxiliary >= program->head_len ||
        entry->child_count != program->entry_argument_count))
        return prepared_pure_reject(
            program, "answer producer entry is not a closed call", NULL);
    for (uint32_t head_index = 0u;
         head_index < program->head_len; head_index++) {
        const PreparedPureHead *head = &program->heads[head_index];
        if (!head->compiled || head->equation_count == 0u ||
            head->first_equation > program->equation_len ||
            head->equation_count >
                program->equation_len - head->first_equation)
            return prepared_pure_reject(
                program, "answer producer reaches an uncompiled head",
                NULL);
    }
    prepared_pure_classify_deterministic_heads(program);
    if (program->body_entry && program->equation_len)
        program->answer_shapes = calloc(program->equation_len, sizeof(*program->answer_shapes));
    for (size_t index = 0u; index < program->equation_len; index++) {
        PreparedPureEquation *equation = &program->equations[index];
        if (equation->arity > 64u ||
            equation->body.root >= program->node_len)
            return false;
        equation->body.tail_only = !program->has_native_calls &&
            prepared_pure_equation_tail_only(program, equation);
        if (program->answer_shapes && program->nodes[equation->body.root].kind == PREPARED_PURE_BUILD &&
            prepared_pure_value_tree(program, equation->body.root, 0u))
            program->answer_shapes[index] = prepared_pure_answer_shape(
                program, equation, equation->body.root, 0u);
        if (equation->body.tail_only)
            continue;
        if (!prepared_pure_lower_body(program, &equation->body))
            return false;
    }
    return (!program->body_entry || prepared_pure_lower_body(program, &program->body)) &&
        prepared_pure_project_sources(program);
}

bool cetta_prepared_pure_constructs_expression_always(
    const Atom *head, CettaExprLen length) {
    (void)head;
    (void)length;
    return true;
}

/* Whether the dialect's constructor builds an ordinary expression for every
 * value of `length` elements headed by `head`. */
static bool prepared_pure_constructs_expression(
    const CettaPreparedPureProgram *program, const Atom *head,
    uint32_t length) {
    return program->constructs_expression &&
        program->constructs_expression(head, length);
}

typedef struct {
    CettaPreparedPureProgram *program;
    /* The body's locals, and the entry arguments' first register (the
     * root's only). */
    uint32_t local_count;
    uint32_t entry_base;
    bool root;
    uint32_t next_register;
} PreparedPureCodeLowering;

static bool prepared_pure_code_emit(
    CettaPreparedPureProgram *program, PreparedPureCode code,
    const uint32_t *operands, uint32_t operand_count,
    uint32_t *index_out) {
    if (program->code_len >= UINT32_MAX ||
        program->code_operand_len > UINT32_MAX - operand_count ||
        !prepared_pure_reserve(
            (void **)&program->code, sizeof(*program->code),
            &program->code_cap, program->code_len + 1u) ||
        !prepared_pure_reserve(
            (void **)&program->code_operands,
            sizeof(*program->code_operands), &program->code_operand_cap,
            program->code_operand_len + operand_count + 1u))
        return false;
    code.first_operand = (uint32_t)program->code_operand_len;
    code.operand_count = operand_count;
    if (operand_count > 0u)
        memcpy(&program->code_operands[program->code_operand_len],
               operands, sizeof(*operands) * operand_count);
    program->code_operand_len += operand_count;
    if (index_out)
        *index_out = (uint32_t)program->code_len;
    program->code[program->code_len++] = code;
    return true;
}

static bool prepared_pure_code_node(
    PreparedPureCodeLowering *lowering, uint32_t node_index, bool tail,
    uint32_t destination, uint32_t depth);

/* The operand standing for a node's value: a literal, a local or an entry
 * argument is read where it is; any other node is computed into a register
 * of its own first. */
static bool prepared_pure_code_operand(
    PreparedPureCodeLowering *lowering, uint32_t node_index, uint32_t depth,
    uint32_t *operand_out) {
    CettaPreparedPureProgram *program = lowering->program;
    if (node_index >= program->node_len)
        return false;
    const PreparedPureNode *node = &program->nodes[node_index];
    switch (node->kind) {
    case PREPARED_PURE_LITERAL:
        if (!node->atom ||
            program->code_literal_len >= PREPARED_PURE_CODE_LITERAL ||
            !prepared_pure_reserve(
                (void **)&program->code_literals,
                sizeof(*program->code_literals),
                &program->code_literal_cap, program->code_literal_len + 1u))
            return false;
        *operand_out = PREPARED_PURE_CODE_LITERAL |
                       (uint32_t)program->code_literal_len;
        program->code_literals[program->code_literal_len++] = node->atom;
        return true;
    case PREPARED_PURE_SLOT:
        if (node->auxiliary >= lowering->local_count)
            return false;
        *operand_out = node->auxiliary;
        return true;
    case PREPARED_PURE_ENTRY_ARGUMENT:
        if (!lowering->root ||
            node->auxiliary >= program->entry_argument_count)
            return false;
        *operand_out = lowering->entry_base + node->auxiliary;
        return true;
    default:
        break;
    }
    if (lowering->next_register >= PREPARED_PURE_MAX_SLOTS)
        return false;
    uint32_t reg = lowering->next_register++;
    if (!prepared_pure_code_node(lowering, node_index, false, reg, depth))
        return false;
    *operand_out = reg;
    return true;
}

/* Admit only bounded trees of pure register operations whose leaves already
 * hold values. Calls, forced slots, bindings and constructors are boundaries:
 * hoisting them could change failure, effect or termination order. */
static bool prepared_pure_code_scalar_tree(
    PreparedPureCodeLowering *lowering, uint32_t index, uint32_t depth,
    uint32_t *budget, uint32_t *operations, uint32_t *locals, uint32_t *count,
    uint32_t *tape, uint32_t *tape_count, bool *floating) {
    CettaPreparedPureProgram *program = lowering->program;
    if (index >= program->node_len || *budget == 0u) return false;
    (*budget)--;
    const PreparedPureNode *node = &program->nodes[index];
    if (node->kind == PREPARED_PURE_LITERAL) {
        CettaPlainScalar value;
        if (!grounded_plain_scalar_from_atom(node->atom, &value)) return false;
        *floating |= value.kind == CETTA_PLAIN_SCALAR_FLOAT;
        tape[(*tape_count)++] = index;
        return true;
    }
    if (node->kind == PREPARED_PURE_SLOT) {
        if (node->auxiliary >= lowering->local_count ||
            *count >= PREPARED_PURE_INLINE_NODES) return false;
        locals[(*count)++] = node->auxiliary;
        tape[(*tape_count)++] = index;
        return true;
    }
    if (depth == 0u || node->kind != PREPARED_PURE_REGISTER ||
        node->child_count != 2u || !node->atom ||
        !grounded_is_plain_scalar_tree_operator(node->atom, 2u) ||
        node->first_child > program->child_len ||
        2u > program->child_len - node->first_child) return false;
    CettaGsltPreparedPureIntrinsicInstruction intrinsic;
    if (!cetta_prepared_pure_intrinsic_program(node->atom->sym_id,2u,&intrinsic) ||
        intrinsic != CETTA_GSLT_PREPARED_PURE_INTRINSIC_GROUNDED_DISPATCH) return false;
    (*operations)++;
    if (!prepared_pure_code_scalar_tree(lowering,
               program->children[node->first_child], depth-1u,
               budget, operations, locals, count, tape, tape_count, floating) ||
        !prepared_pure_code_scalar_tree(lowering,
               program->children[node->first_child+1u], depth-1u,
               budget, operations, locals, count, tape, tape_count, floating))
        return false;
    tape[(*tape_count)++] = index;
    return true;
}

/* Lower the admitted postorder tree to register operations. Locals and
 * literals are operands, not runtime push instructions; each computed value
 * has a fresh compact register. Repeated occurrences are not deduplicated. */
static bool prepared_pure_scalar_registers(CettaPreparedPureProgram *program,
    const uint32_t *tape, uint32_t count, uint32_t *code, uint32_t *operations) {
    uint32_t stack[PREPARED_PURE_INLINE_NODES], height = 0u;
    *operations = 0u;
    for (uint32_t i=0u; i<count; i++) {
        const PreparedPureNode *node = &program->nodes[tape[i]];
        if (node->kind == PREPARED_PURE_LITERAL) {
            if (tape[i] >= PREPARED_PURE_SCALAR_LOCAL) return false;
            stack[height++] = PREPARED_PURE_SCALAR_LITERAL | tape[i];
        } else if (node->kind == PREPARED_PURE_SLOT) {
            if (node->auxiliary >= PREPARED_PURE_SCALAR_LOCAL) return false;
            stack[height++] = PREPARED_PURE_SCALAR_LOCAL | node->auxiliary;
        } else {
            if (height < 2u || node->kind != PREPARED_PURE_REGISTER) return false;
            uint32_t right = stack[--height], left = stack[--height];
            uint32_t op = (*operations)++;
            code[op*3u] = tape[i]; code[op*3u+1u] = left; code[op*3u+2u] = right;
            stack[height++] = op;
        }
    }
    return height == 1u && *operations > 0u;
}

/* The operands of a node's children, left to right, in a new array. */
static bool prepared_pure_code_operands(
    PreparedPureCodeLowering *lowering, uint32_t first_child,
    uint32_t child_count, uint32_t depth, uint32_t **operands_out) {
    CettaPreparedPureProgram *program = lowering->program;
    if (first_child > program->child_len ||
        child_count > program->child_len - first_child)
        return false;
    uint32_t *operands =
        malloc(sizeof(*operands) * (child_count ? child_count : 1u));
    if (!operands)
        return false;
    for (uint32_t i = 0u; i < child_count; i++) {
        if (!prepared_pure_code_operand(
                lowering, program->children[first_child + i], depth + 1u,
                &operands[i])) {
            free(operands);
            return false;
        }
    }
    *operands_out = operands;
    return true;
}

/* Lower one node into code that puts its value in `destination`, or, in
 * tail position, answers it for the body. */
static bool prepared_pure_code_node(
    PreparedPureCodeLowering *lowering, uint32_t node_index, bool tail,
    uint32_t destination, uint32_t depth) {
    CettaPreparedPureProgram *program = lowering->program;
    if (node_index >= program->node_len ||
        depth > PREPARED_PURE_MAX_COMPILE_DEPTH)
        return false;
    PreparedPureNode node = program->nodes[node_index];
    switch (node.kind) {
    case PREPARED_PURE_LITERAL:
    case PREPARED_PURE_SLOT:
    case PREPARED_PURE_ENTRY_ARGUMENT: {
        uint32_t operand = 0u;
        return prepared_pure_code_operand(
                   lowering, node_index, depth, &operand) &&
            prepared_pure_code_emit(
                program,
                (PreparedPureCode){
                    .kind = tail ? PREPARED_PURE_CODE_RETURN
                                 : PREPARED_PURE_CODE_MOVE,
                    .destination = destination,
                },
                &operand, 1u, NULL);
    }
    case PREPARED_PURE_BUILD:
    case PREPARED_PURE_REGISTER:
    case PREPARED_PURE_INTRINSIC:
    case PREPARED_PURE_OBSERVE: {
        if (node.kind == PREPARED_PURE_REGISTER && node.child_count == 2u &&
            node.first_child < program->child_len &&
            2u <= program->child_len-node.first_child &&
            program->children[node.first_child] < program->node_len &&
            program->children[node.first_child+1u] < program->node_len &&
            (program->nodes[program->children[node.first_child]].kind == PREPARED_PURE_REGISTER ||
             program->nodes[program->children[node.first_child+1u]].kind == PREPARED_PURE_REGISTER) &&
            !prepared_pure_scalar_tree_reference_enabled()) {
            uint32_t locals[PREPARED_PURE_INLINE_NODES];
            uint32_t tape[PREPARED_PURE_INLINE_NODES], tape_count = 0u;
            bool floating = false;
            uint32_t count = 0u, operations = 0u;
            uint32_t budget = PREPARED_PURE_INLINE_NODES;
            if (prepared_pure_code_scalar_tree(lowering, node_index,
                    PREPARED_PURE_INLINE_DEPTH, &budget, &operations,
                    locals, &count, tape, &tape_count, &floating) &&
                operations > 1u && floating) {
                uint32_t compact[PREPARED_PURE_INLINE_NODES*3u], op_count = 0u;
                if (!prepared_pure_scalar_registers(program,tape,tape_count,compact,&op_count))
                    return false;
                uint32_t words = op_count*3u;
                if (program->scalar_code_len > UINT32_MAX-words ||
                    !prepared_pure_reserve((void **)&program->scalar_code,
                        sizeof(*program->scalar_code), &program->scalar_code_cap,
                        program->scalar_code_len+words)) return false;
                uint32_t first = (uint32_t)program->scalar_code_len;
                memcpy(program->scalar_code+first,compact,words*sizeof(*compact));
                program->scalar_code_len += words;
                return prepared_pure_code_emit(program,
                    (PreparedPureCode){.kind = PREPARED_PURE_CODE_SCALAR_TREE,
                        .destination = destination, .auxiliary = node_index,
                        .target = first, .scalar_count = (uint8_t)op_count,
                        .returns = tail}, locals, count, NULL);
            }
        }
        uint32_t *operands = NULL;
        if (!prepared_pure_code_operands(
                lowering, node.first_child, node.child_count, depth,
                &operands))
            return false;
        const PreparedPureNode *head =
            node.child_count > 0u
                ? &program->nodes[program->children[node.first_child]]
                : NULL;
        bool expression = node.kind == PREPARED_PURE_BUILD &&
            node.auxiliary != PREPARED_PURE_BUILD_DATA_HEAD && head &&
            head->kind == PREPARED_PURE_LITERAL &&
            prepared_pure_constructs_expression(
                program, head->atom, node.child_count);
        uint8_t kind = expression
            ? PREPARED_PURE_CODE_BUILD_EXPRESSION
            : node.kind == PREPARED_PURE_BUILD
            ? PREPARED_PURE_CODE_BUILD
            : node.kind == PREPARED_PURE_REGISTER
            ? PREPARED_PURE_CODE_REGISTER
            : node.kind == PREPARED_PURE_INTRINSIC
            ? PREPARED_PURE_CODE_INTRINSIC
            : PREPARED_PURE_CODE_OBSERVE;
        uint32_t head_index = 0u;
        if (expression) {
            if (program->code_head_len >= UINT32_MAX ||
                !prepared_pure_reserve(
                    (void **)&program->code_heads,
                    sizeof(*program->code_heads), &program->code_head_cap,
                    program->code_head_len + 1u)) {
                free(operands);
                return false;
            }
            head_index = (uint32_t)program->code_head_len++;
            atom_expr_head_init(&program->code_heads[head_index], head->atom);
        }
        bool ok = prepared_pure_code_emit(
            program,
            (PreparedPureCode){
                .kind = kind,
                .destination = destination,
                .auxiliary = node_index,
                .target = head_index,
                .returns = tail,
            },
            operands, node.child_count, NULL);
        free(operands);
        return ok;
    }
    case PREPARED_PURE_IF: {
        if (node.child_count != 3u || node.first_child > program->child_len ||
            3u > program->child_len - node.first_child)
            return false;
        uint32_t condition_node = program->children[node.first_child];
        uint32_t then_node = program->children[node.first_child + 1u];
        uint32_t else_node = program->children[node.first_child + 2u];
        uint32_t branch = 0u;
        uint32_t join = 0u;
        if (condition_node >= program->node_len)
            return false;
        const PreparedPureNode *condition =
            &program->nodes[condition_node];
        bool emitted = false;
        uint32_t bind_body = 0u;
        if (condition->kind == PREPARED_PURE_OBSERVE &&
            prepared_pure_test_binds(
                program, condition, then_node, &bind_body)) {
            /* The pattern test and the then branch's bind of the same
             * pattern to the same value, as one match. */
            uint32_t bound = 0u;
            emitted = prepared_pure_code_operand(
                          lowering, program->children[condition->first_child],
                          depth + 1u, &bound) &&
                prepared_pure_code_emit(
                    program,
                    (PreparedPureCode){
                        .kind = PREPARED_PURE_CODE_MATCH_ELSE,
                        .auxiliary = program->nodes[then_node].auxiliary,
                    },
                    &bound, 1u, &branch);
            then_node = bind_body;
        } else if (condition->kind == PREPARED_PURE_REGISTER &&
            condition->child_count == 2u) {
            /* The condition's register operation and the branch on its
             * value, as one instruction. */
            uint32_t *operands = NULL;
            emitted = prepared_pure_code_operands(
                          lowering, condition->first_child, 2u, depth + 1u,
                          &operands) &&
                prepared_pure_code_emit(
                    program,
                    (PreparedPureCode){
                        .kind = PREPARED_PURE_CODE_BRANCH_REGISTER,
                        .auxiliary = condition_node,
                    },
                    operands, 2u, &branch);
            free(operands);
            if (!emitted)
                return false;
        } else {
            uint32_t value = 0u;
            emitted = prepared_pure_code_operand(
                          lowering, condition_node, depth + 1u, &value) &&
                prepared_pure_code_emit(
                    program,
                    (PreparedPureCode){.kind = PREPARED_PURE_CODE_BRANCH},
                    &value, 1u, &branch);
        }
        if (!emitted ||
            !prepared_pure_code_node(
                lowering, then_node, tail, destination, depth + 1u) ||
            (!tail &&
             !prepared_pure_code_emit(
                 program,
                 (PreparedPureCode){.kind = PREPARED_PURE_CODE_JUMP},
                 NULL, 0u, &join)))
            return false;
        program->code[branch].target = (uint32_t)program->code_len;
        if (!prepared_pure_code_node(
                lowering, else_node, tail, destination, depth + 1u))
            return false;
        if (!tail)
            program->code[join].target = (uint32_t)program->code_len;
        return true;
    }
    case PREPARED_PURE_BIND: {
        if (node.child_count != 2u ||
            node.auxiliary >= program->bind_pattern_len ||
            node.first_child > program->child_len ||
            2u > program->child_len - node.first_child)
            return false;
        uint32_t bound = 0u;
        return prepared_pure_code_operand(
                   lowering, program->children[node.first_child],
                   depth + 1u, &bound) &&
            prepared_pure_code_emit(
                program,
                (PreparedPureCode){
                    .kind = PREPARED_PURE_CODE_MATCH,
                    .auxiliary = node.auxiliary,
                },
                &bound, 1u, NULL) &&
            prepared_pure_code_node(
                lowering, program->children[node.first_child + 1u], tail,
                destination, depth + 1u);
    }
    case PREPARED_PURE_ZERO:
        return prepared_pure_code_emit(
            program, (PreparedPureCode){.kind = PREPARED_PURE_CODE_FAIL},
            NULL, 0u, NULL);
    case PREPARED_PURE_DECLINE:
        return prepared_pure_code_emit(
            program, (PreparedPureCode){.kind = PREPARED_PURE_CODE_DECLINE},
            NULL, 0u, NULL);
    case PREPARED_PURE_CALL: {
        /* In an answer producer, code runs only regions that cannot
         * choose, whose calls are all to determinate heads. */
        if (node.auxiliary >= program->head_len || node.child_count > 64u ||
            !node.call_arguments_are_values ||
            (program->answer_producer &&
             !(program->has_native_calls
                 ? program->heads[node.auxiliary].unique_match
                 : program->heads[node.auxiliary].deterministic)))
            return false;
        uint32_t *operands = NULL;
        if (!prepared_pure_code_operands(
                lowering, node.first_child, node.child_count, depth,
                &operands))
            return false;
        bool ok = prepared_pure_code_emit(
            program,
            (PreparedPureCode){
                .kind = tail ? PREPARED_PURE_CODE_TAIL_CALL
                             : PREPARED_PURE_CODE_CALL,
                .destination = destination,
                .auxiliary = node.auxiliary,
            },
            operands, node.child_count, NULL);
        free(operands);
        return ok;
    }
    default:
        return false;
    }
}

/* Liveness over a body's registers, one bit each. */
static void prepared_pure_code_live_set(
    uint64_t *set, uint32_t reg) {
    set[reg / 64u] |= UINT64_C(1) << (reg % 64u);
}

static void prepared_pure_code_live_clear(
    uint64_t *set, uint32_t reg) {
    set[reg / 64u] &= ~(UINT64_C(1) << (reg % 64u));
}

/* Record, for each call of the body code[start, end), the registers the
 * body reads after the call returns.  One backward pass: every jump of a
 * body goes forward.  An instruction reads its register operands, and a
 * let pattern also the locals it compares against; it writes its
 * destination, and a let pattern the locals it binds. */
static bool prepared_pure_code_liveness(
    CettaPreparedPureProgram *program, uint32_t start, uint32_t end,
    uint32_t register_count) {
    size_t words = register_count / 64u + 1u;
    size_t rows = (size_t)(end - start) + 1u;
    uint64_t *live_in = calloc(rows * words, sizeof(*live_in));
    uint64_t *out = malloc(words * sizeof(*out));
    if (!live_in || !out) {
        free(out);
        free(live_in);
        return false;
    }
    bool ok = true;
    for (uint32_t index = end; ok && index-- > start;) {
        PreparedPureCode *code = &program->code[index];
        memset(out, 0, words * sizeof(*out));
        bool falls_through = code->kind != PREPARED_PURE_CODE_RETURN &&
            code->kind != PREPARED_PURE_CODE_DECLINE &&
            code->kind != PREPARED_PURE_CODE_FAIL &&
            code->kind != PREPARED_PURE_CODE_TAIL_CALL &&
            code->kind != PREPARED_PURE_CODE_JUMP && !code->returns;
        bool jumps = code->kind == PREPARED_PURE_CODE_JUMP ||
            code->kind == PREPARED_PURE_CODE_BRANCH ||
            code->kind == PREPARED_PURE_CODE_BRANCH_REGISTER ||
            code->kind == PREPARED_PURE_CODE_MATCH_ELSE;
        bool matches = code->kind == PREPARED_PURE_CODE_MATCH ||
            code->kind == PREPARED_PURE_CODE_MATCH_ELSE;
        if (falls_through) {
            const uint64_t *next = &live_in[(size_t)(index + 1u - start) * words];
            for (size_t w = 0u; w < words; w++)
                out[w] |= next[w];
        }
        /* A match binds its locals on the way it continues, and a
         * MATCH_ELSE only there: its mismatch leaves them for `target`.
         * The locals it compares against it reads either way. */
        const PreparedPureBindPattern *bind_pattern = NULL;
        if (matches) {
            bind_pattern = &program->bind_patterns[code->auxiliary];
            if (bind_pattern->first_var > program->bind_var_len ||
                bind_pattern->var_count >
                    program->bind_var_len - bind_pattern->first_var) {
                ok = false;
                break;
            }
            for (uint32_t i = 0u; i < bind_pattern->var_count; i++) {
                const PreparedPureBindVar *binding =
                    &program->bind_vars[bind_pattern->first_var + i];
                if (binding->slot >= register_count) {
                    ok = false;
                    break;
                }
                if (!binding->prebound)
                    prepared_pure_code_live_clear(out, binding->slot);
            }
            if (!ok)
                break;
        }
        if (jumps) {
            if (code->target <= index || code->target > end) {
                ok = false;
                break;
            }
            const uint64_t *next =
                &live_in[(size_t)(code->target - start) * words];
            for (size_t w = 0u; w < words; w++)
                out[w] |= next[w];
        }
        bool writes = !code->returns &&
            (code->kind == PREPARED_PURE_CODE_MOVE ||
             code->kind == PREPARED_PURE_CODE_BUILD ||
             code->kind == PREPARED_PURE_CODE_BUILD_EXPRESSION ||
             code->kind == PREPARED_PURE_CODE_REGISTER ||
             code->kind == PREPARED_PURE_CODE_SCALAR_TREE ||
             code->kind == PREPARED_PURE_CODE_INTRINSIC ||
             code->kind == PREPARED_PURE_CODE_OBSERVE ||
             code->kind == PREPARED_PURE_CODE_CALL);
        if (writes && code->destination >= register_count) {
            ok = false;
            break;
        }
        if (writes)
            prepared_pure_code_live_clear(out, code->destination);
        if (program->has_native_calls ||
            code->kind == PREPARED_PURE_CODE_CALL ||
            code->kind == PREPARED_PURE_CODE_TAIL_CALL) {
            uint32_t count = 0u;
            if (code->kind != PREPARED_PURE_CODE_TAIL_CALL && !code->returns) {
                for (uint32_t reg = 0u; reg < register_count; reg++)
                    count += (out[reg / 64u] >> (reg % 64u)) & 1u;
            }
            if (program->code_live_len > UINT32_MAX - count ||
                !prepared_pure_reserve(
                    (void **)&program->code_live, sizeof(*program->code_live),
                    &program->code_live_cap,
                    program->code_live_len + (count ? count : 1u))) {
                ok = false;
                break;
            }
            code->first_live = (uint32_t)program->code_live_len;
            code->live_count = count;
            for (uint32_t reg = 0u; count > 0u && reg < register_count; reg++) {
                if ((out[reg / 64u] >> (reg % 64u)) & 1u)
                    program->code_live[program->code_live_len++] = reg;
            }
        }
        if (bind_pattern) {
            for (uint32_t i = 0u; i < bind_pattern->var_count; i++) {
                const PreparedPureBindVar *binding =
                    &program->bind_vars[bind_pattern->first_var + i];
                if (binding->prebound)
                    prepared_pure_code_live_set(out, binding->slot);
            }
        }
        const uint32_t *operands =
            &program->code_operands[code->first_operand];
        for (uint32_t i = 0u; ok && i < code->operand_count; i++) {
            if (operands[i] & PREPARED_PURE_CODE_LITERAL)
                continue;
            if (operands[i] >= register_count) {
                ok = false;
                break;
            }
            prepared_pure_code_live_set(out, operands[i]);
        }
        memcpy(&live_in[(size_t)(index - start) * words], out,
               words * sizeof(*out));
    }
    free(out);
    free(live_in);
    return ok;
}

/* A value's key as a keyed group tests it (prepared_pure_value_key),
 * packed: its symbol above its length, 0 for a symbol.  False for a value
 * with no key. */
static PREPARED_PURE_ALWAYS_INLINE bool prepared_pure_code_key(
    const Atom *value, uint64_t *key_out) {
    if (atom_petta_value_representation(value) != PETTA_VALUE_ORDINARY)
        return false;
    if (value->kind == ATOM_SYMBOL) {
        *key_out = (uint64_t)value->sym_id << 32;
        return true;
    }
    if (value->kind == ATOM_EXPR && value->expr.len > 0u &&
        value->expr.len <= UINT32_MAX && value->expr.elems[0] &&
        value->expr.elems[0]->kind == ATOM_SYMBOL) {
        *key_out = (uint64_t)value->expr.elems[0]->sym_id << 32 |
                   (uint32_t)value->expr.len;
        return true;
    }
    return false;
}

static bool prepared_pure_code_add_bind(
    CettaPreparedPureProgram *program, uint32_t local, uint32_t source,
    uint32_t field) {
    if (!prepared_pure_reserve(
            (void **)&program->code_binds, sizeof(*program->code_binds),
            &program->code_bind_cap, program->code_bind_len + 1u))
        return false;
    program->code_binds[program->code_bind_len++] = (PreparedPureCodeBind){
        .local = local, .source = source, .field = field,
    };
    return true;
}

/* Whether `equation` can be entered with its first `key_ops` match ops
 * decided (prepared_pure_direct_entry): by its arguments binding its
 * variables in turn, or by its compiled match. */
static bool prepared_pure_code_enterable(
    const CettaPreparedPureProgram *program,
    const PreparedPureEquation *equation, uint32_t key_ops) {
    if (equation->arity > 64u)
        return false;
    /* Arguments that are all variables are bound in turn; a repeated one
     * is compared by the general matcher (prepared_pure_match_into). */
    if (key_ops == 0u && equation->variable_arguments)
        return !equation->repeated_variables &&
            equation->first_argument_slot <= program->argument_slot_len &&
            equation->arity <=
                program->argument_slot_len - equation->first_argument_slot;
    return equation->compiled_match &&
        equation->match_register_count <=
            PREPARED_PURE_MATCH_MAX_REGISTERS &&
        equation->match_op_count >= key_ops &&
        equation->first_match_op <= program->match_op_len &&
        equation->match_op_count <=
            program->match_op_len - equation->first_match_op;
}

/* Where register `reg` of an equation's compiled match comes from when the
 * key row `keys` (at the first `positions` arguments) has been admitted:
 * argument `reg`, or field `*field_out` of the argument `*source_out`
 * whose key's expression test loaded it.  False for any other register. */
static bool prepared_pure_ready_register_source(
    const CettaPreparedPureProgram *program,
    const PreparedPureEquation *equation,
    const PreparedPureArgumentKey *keys, uint32_t positions, uint32_t reg,
    uint32_t *source_out, uint32_t *field_out) {
    if (reg < equation->arity) {
        *source_out = reg;
        *field_out = UINT32_MAX;
        return true;
    }
    const PreparedPureMatchOp *first =
        &program->match_ops[equation->first_match_op];
    const PreparedPureMatchOp *end = first + equation->match_op_count;
    for (const PreparedPureMatchOp *op = first; op < end; op++) {
        if (op->kind != PREPARED_PURE_MATCH_OP_EXPR ||
            op->source >= positions || keys[op->source].length == 0u ||
            keys[op->source].length != op->length)
            continue;
        if (reg >= op->operand && reg - op->operand < op->length) {
            *source_out = op->source;
            *field_out = reg - op->operand;
            return true;
        }
    }
    return false;
}

/* Whether `op` is a test that admitting the key row `keys` has decided:
 * an argument's symbol, or the length of an argument's expression with the
 * test of its head that follows it. */
static bool prepared_pure_code_key_op(
    const PreparedPureMatchOp *op, const PreparedPureMatchOp *end,
    const PreparedPureArgumentKey *keys, uint32_t positions,
    uint32_t *skip_out) {
    *skip_out = 1u;
    if (op->source >= positions || keys[op->source].symbol == SYMBOL_ID_NONE)
        return false;
    const PreparedPureArgumentKey *key = &keys[op->source];
    if (op->kind == PREPARED_PURE_MATCH_OP_ATOM)
        return key->length == 0u && op->literal &&
            op->literal->kind == ATOM_SYMBOL &&
            op->literal->sym_id == key->symbol;
    if (op->kind != PREPARED_PURE_MATCH_OP_EXPR || key->length == 0u ||
        op->length != key->length || op + 1 >= end ||
        op[1].kind != PREPARED_PURE_MATCH_OP_ATOM ||
        op[1].source != op->operand || !op[1].literal ||
        op[1].literal->kind != ATOM_SYMBOL ||
        op[1].literal->sym_id != key->symbol)
        return false;
    *skip_out = 2u;
    return true;
}

/* Record how a call enters `equation`, whose group's key row `keys` at the
 * first `positions` arguments has been admitted (none for a head's only
 * equation).  The compact form binds each variable from an argument or a
 * field of one when every other op of the compiled match is a test the
 * admitted keys decided (prepared_pure_direct_entry makes those tests, and
 * binds the same variables); otherwise the match ops run from the first
 * key's tests on.  False when the binds cannot be stored. */
static bool prepared_pure_code_enter_equation(
    CettaPreparedPureProgram *program, PreparedPureEquation *equation,
    const PreparedPureArgumentKey *keys, uint32_t positions) {
    const PreparedPureArgumentKey none = {.symbol = SYMBOL_ID_NONE};
    const PreparedPureArgumentKey *key = positions ? &keys[0] : &none;
    uint32_t key_ops = key->symbol == SYMBOL_ID_NONE
        ? 0u : key->length > 0u ? 2u : 1u;
    equation->code_key_ops = key_ops;
    equation->code_key_length = key_ops > 0u ? key->length : 0u;
    equation->code_binds = false;
    equation->first_bind = (uint32_t)program->code_bind_len;
    equation->bind_count = 0u;
    equation->bind_field_count = 0u;
    if (key_ops == 0u && equation->variable_arguments &&
        !equation->repeated_variables) {
        for (uint32_t i = 0u; i < equation->arity; i++) {
            uint32_t local = program->argument_slots[
                equation->first_argument_slot + i];
            if (local >= equation->pattern_var_count ||
                !prepared_pure_code_add_bind(program, local, i, UINT32_MAX))
                return false;
        }
        equation->bind_count = equation->arity;
        equation->code_binds = true;
        return true;
    }
    if (positions == 0u || !equation->compiled_match)
        return true;
    const PreparedPureMatchOp *first =
        &program->match_ops[equation->first_match_op];
    const PreparedPureMatchOp *end = first + equation->match_op_count;
    /* Every op is a decided key test or binds a fresh variable from an
     * argument or a keyed argument's field. */
    for (const PreparedPureMatchOp *op = first; op < end;) {
        uint32_t skip = 1u;
        uint32_t source = 0u;
        uint32_t field = 0u;
        if (prepared_pure_code_key_op(op, end, keys, positions, &skip)) {
            op += skip;
            continue;
        }
        if (op->kind != PREPARED_PURE_MATCH_OP_BIND ||
            op->operand >= equation->pattern_var_count ||
            !prepared_pure_ready_register_source(
                program, equation, keys, positions, op->source, &source,
                &field))
            /* Not the compact form: the match ops enter it. */
            return true;
        op++;
    }
    /* Each bind binds a distinct fresh variable, so the binds may be made
     * in any order: the fields' first, then the arguments'. */
    for (int pass = 0; pass < 2; pass++) {
        for (const PreparedPureMatchOp *op = first; op < end;) {
            uint32_t skip = 1u;
            if (prepared_pure_code_key_op(op, end, keys, positions, &skip)) {
                op += skip;
                continue;
            }
            uint32_t source = 0u;
            uint32_t field = 0u;
            (void)prepared_pure_ready_register_source(
                program, equation, keys, positions, op->source, &source,
                &field);
            bool is_field = field != UINT32_MAX;
            if (is_field == (pass == 0)) {
                if (!prepared_pure_code_add_bind(
                        program, op->operand, source, field))
                    return false;
                if (is_field)
                    equation->bind_field_count++;
            }
            op++;
        }
    }
    equation->bind_count =
        (uint32_t)program->code_bind_len - equation->first_bind;
    equation->code_binds = true;
    return true;
}

/* How calls of `head` enter (PreparedPureCodeEntry), with how each of its
 * equations is entered.  prepared_pure_direct_entry makes the same choice
 * at each call and binds the same locals. */
static bool prepared_pure_code_entry(
    CettaPreparedPureProgram *program, const PreparedPureHead *head,
    PreparedPureCodeEntry *entry_out) {
    *entry_out = (PreparedPureCodeEntry){
        .kind = PREPARED_PURE_CODE_ENTRY_GENERAL,
    };
    if (!head->compiled || head->equation_count == 0u ||
        head->first_equation > program->equation_len ||
        head->equation_count >
            program->equation_len - head->first_equation)
        return true;
    if (head->equation_count == 1u) {
        PreparedPureEquation *equation =
            &program->equations[head->first_equation];
        if (!prepared_pure_code_enterable(program, equation, 0u))
            return true;
        if (!prepared_pure_code_enter_equation(program, equation, NULL, 0u))
            return false;
        *entry_out = (PreparedPureCodeEntry){
            .kind = PREPARED_PURE_CODE_ENTRY_SINGLE,
            .arity = equation->arity,
            .target = head->first_equation,
        };
        return true;
    }
    if (!head->keyed || head->first_decision >= program->decision_len)
        return true;
    const PreparedPureDecisionProgram *decision =
        &program->decisions[head->first_decision];
    uint32_t positions = decision->key_positions;
    if (decision->selector || !decision->keyed || positions == 0u ||
        positions > PREPARED_PURE_KEY_MAX_POSITIONS ||
        decision->arity == 0u || decision->arity > 64u ||
        decision->arity < positions ||
        decision->first_key > program->argument_key_len ||
        (size_t)decision->equation_count * positions >
            program->argument_key_len - decision->first_key)
        return true;
    bool first_keys = positions == 1u;
    for (uint32_t i = 0u; i < decision->equation_count; i++) {
        const PreparedPureArgumentKey *key =
            &program->argument_keys[decision->first_key + i * positions];
        if (key->equation >= program->equation_len)
            return true;
        const PreparedPureEquation *equation =
            &program->equations[key->equation];
        uint32_t key_ops = key->symbol == SYMBOL_ID_NONE
            ? 0u : key->length > 0u ? 2u : 1u;
        if (equation->arity != decision->arity ||
            !prepared_pure_code_enterable(program, equation, key_ops))
            return true;
        first_keys = first_keys && key->symbol != SYMBOL_ID_NONE;
    }
    uint32_t first_key = (uint32_t)program->code_key_len;
    if (first_keys &&
        (!prepared_pure_reserve(
             (void **)&program->code_keys, sizeof(*program->code_keys),
             &program->code_key_cap,
             program->code_key_len + decision->equation_count) ||
         !prepared_pure_reserve(
             (void **)&program->code_key_equations,
             sizeof(*program->code_key_equations),
             &program->code_key_equation_cap,
             program->code_key_len + decision->equation_count)))
        return false;
    for (uint32_t i = 0u; i < decision->equation_count; i++) {
        const PreparedPureArgumentKey *key =
            &program->argument_keys[decision->first_key + i * positions];
        if (!prepared_pure_code_enter_equation(
                program, &program->equations[key->equation], key,
                positions))
            return false;
        if (first_keys) {
            program->code_keys[program->code_key_len] =
                (uint64_t)key->symbol << 32 | key->length;
            program->code_key_equations[program->code_key_len] =
                key->equation;
            program->code_key_len++;
        }
    }
    *entry_out = (PreparedPureCodeEntry){
        .kind = PREPARED_PURE_CODE_ENTRY_KEYED,
        .arity = decision->arity,
        .target = first_key,
        .count = decision->equation_count,
        .positions = positions,
        .decision = head->first_decision,
    };
    return true;
}

/* Lower `node` as a body whose first `local_count` registers are its
 * locals, and record which registers each of its calls' continuations
 * read.  A body `owned` by its activation may end in a tail call, whose
 * callee takes over those registers.  An answer producer's step does not
 * own them: the cursor reads its frame's slots after the step, so the
 * step's value is computed into a register of its own and returned. */
static bool prepared_pure_lower_code_body(
    CettaPreparedPureProgram *program, uint32_t node, uint32_t local_count,
    uint32_t entry_argument_count, bool root, bool owned,
    uint32_t *first_code_out, uint32_t *register_count_out) {
    if (local_count >= PREPARED_PURE_MAX_SLOTS ||
        entry_argument_count > PREPARED_PURE_MAX_SLOTS - local_count - 1u)
        return false;
    PreparedPureCodeLowering lowering = {
        .program = program,
        .local_count = local_count,
        .entry_base = local_count,
        .root = root,
        .next_register = local_count + entry_argument_count,
    };
    uint32_t first_code = (uint32_t)program->code_len;
    uint32_t value = lowering.next_register;
    if (!owned)
        lowering.next_register++;
    if (!(owned
              ? prepared_pure_code_node(&lowering, node, true, 0u, 0u)
              : prepared_pure_code_node(&lowering, node, false, value, 0u) &&
                    prepared_pure_code_emit(
                        program,
                        (PreparedPureCode){.kind = PREPARED_PURE_CODE_RETURN},
                        &value, 1u, NULL)) ||
        !prepared_pure_code_liveness(
            program, first_code, (uint32_t)program->code_len,
            lowering.next_register))
        return false;
    *first_code_out = first_code;
    *register_count_out = lowering.next_register;
    return true;
}

/* Native-aware register execution is a lowering of the continuation steps,
 * not a second lowering of the source tree. Logical slots keep their exact
 * coordinates; only short-lived constructor operands need extra registers. */
static __attribute__((noinline, cold)) bool prepared_pure_lower_native_code(CettaPreparedPureProgram *program) {
    /* The cursor already selected this equation. Other equations remain
     * cursor alternatives; calls from code still require a unique match,
     * or transfer their ready call and return frames to that cursor. */
    for (size_t e = 0u; e < program->equation_len + program->body_entry; e++) {
        PreparedPureBody *body = e == program->equation_len
            ? &program->body : &program->equations[e].body;
        body->register_entry = true;
        for (uint32_t index = 0u; index < body->step_count; index++) {
            const PreparedPureStep *step = &program->steps[body->first_step + index];
            if ((step->kind == PREPARED_PURE_STEP_CALL || step->kind == PREPARED_PURE_STEP_TAIL) &&
                !program->heads[step->auxiliary].unique_match)
                body->register_entry = false;
        }
        uint32_t start = (uint32_t)program->code_len;
        uint32_t *positions = calloc((size_t)body->step_count + 1u, sizeof(*positions));
        if (!positions)
            return false;
        PreparedPureCodeLowering lowering = {.program = program,
            .local_count = body->frame_slot_count,
            .next_register = body->frame_slot_count};
        bool ok = true;
        for (uint32_t k = 0u; ok && k < body->step_count; k++) {
            uint32_t pc = body->first_step + k;
            const PreparedPureStep step = program->steps[pc];
            uint32_t first = (uint32_t)program->code_len;
            positions[k] = first;
            switch (step.kind) {
            case PREPARED_PURE_STEP_EVAL:
            case PREPARED_PURE_STEP_RETURN:
            case PREPARED_PURE_STEP_NATIVE:
                ok = prepared_pure_code_node(&lowering, step.node,
                    step.kind == PREPARED_PURE_STEP_RETURN ||
                        (step.kind == PREPARED_PURE_STEP_NATIVE && step.auxiliary),
                    step.slot, 0u);
                break;
            case PREPARED_PURE_STEP_CALL:
            case PREPARED_PURE_STEP_TAIL: {
                uint32_t *operands = NULL;
                ok = step.arity <= 64u && prepared_pure_reserve(
                    (void **)&program->code_operands, sizeof(*program->code_operands),
                    &program->code_operand_cap, program->code_operand_len + step.arity + 1u);
                operands = ok ? malloc(sizeof(*operands) * (step.arity ? step.arity : 1u)) : NULL;
                ok = operands != NULL;
                for (uint32_t i = 0u; ok && i < step.arity; i++)
                    ok = prepared_pure_code_operand(&lowering,
                        program->step_arguments[step.first_argument + i], 0u, &operands[i]);
                if (ok)
                    ok = prepared_pure_code_emit(program, (PreparedPureCode){
                        .kind = step.kind == PREPARED_PURE_STEP_TAIL
                            ? PREPARED_PURE_CODE_TAIL_CALL : PREPARED_PURE_CODE_CALL,
                        .destination = step.slot, .auxiliary = step.auxiliary},
                        operands, step.arity, NULL);
                free(operands);
                break;
            }
            case PREPARED_PURE_STEP_MATCH:
                ok = prepared_pure_code_emit(program, (PreparedPureCode){
                    .kind = PREPARED_PURE_CODE_MATCH, .auxiliary = step.auxiliary},
                    &step.slot, 1u, NULL);
                break;
            case PREPARED_PURE_STEP_MATCH_ELSE:
                ok = prepared_pure_code_emit(program, (PreparedPureCode){
                    .kind = PREPARED_PURE_CODE_MATCH_ELSE,
                    .auxiliary = step.auxiliary, .target = step.target},
                    &step.slot, 1u, NULL);
                break;
            case PREPARED_PURE_STEP_BRANCH:
                ok = prepared_pure_code_emit(program, (PreparedPureCode){
                    .kind = PREPARED_PURE_CODE_BRANCH, .target = step.target},
                    &step.slot, 1u, NULL);
                break;
            case PREPARED_PURE_STEP_JUMP:
                ok = prepared_pure_code_emit(program, (PreparedPureCode){
                    .kind = PREPARED_PURE_CODE_JUMP, .target = step.target}, NULL, 0u, NULL);
                break;
            case PREPARED_PURE_STEP_FAIL:
                ok = prepared_pure_code_emit(program, (PreparedPureCode){
                    .kind = PREPARED_PURE_CODE_FAIL}, NULL, 0u, NULL);
                break;
            default:
                ok = false;
                break;
            }
            if (ok)
                ok = prepared_pure_reserve((void **)&program->code_steps,
                    sizeof(*program->code_steps), &program->code_step_cap, program->code_len);
            for (size_t i = first; ok && i < program->code_len; i++)
                program->code_steps[i] = pc;
        }
        positions[body->step_count] = (uint32_t)program->code_len;
        for (size_t i = start; ok && i < program->code_len; i++) {
            PreparedPureCode *code = &program->code[i];
            if (code->kind != PREPARED_PURE_CODE_BRANCH &&
                code->kind != PREPARED_PURE_CODE_MATCH_ELSE &&
                code->kind != PREPARED_PURE_CODE_JUMP)
                continue;
            if (code->target < body->first_step ||
                code->target - body->first_step > body->step_count) {
                ok = false;
                break;
            }
            code->target = positions[code->target - body->first_step];
        }
        free(positions);
        if (!ok || !prepared_pure_code_liveness(program, start,
                (uint32_t)program->code_len, lowering.next_register))
            return false;
        body->first_code = start;
        body->register_count = lowering.next_register;
    }
    return true;
}

/* Lower to register code every equation a call from code can enter and
 * every entry into code: the root of a program that answers once, or
 * each step of an answer producer that evaluates a region that cannot
 * choose.  In an answer producer those regions call only determinate
 * heads, so only their equations are lowered.  A body outside the code's
 * node kinds leaves the program to the node executor. Native-aware code
 * instead lowers selected equations' continuation steps and transfers an
 * ambiguous ready call to the cursor with its actual return frames. */
static bool prepared_pure_lower_code(CettaPreparedPureProgram *program) {
    if (!program || !program->closed_program ||
        program->call_mode != CETTA_GSLT_PURE_CALL_EAGER ||
        program->body.root >= program->node_len)
        return false;
    bool producer = program->answer_producer;
    if (producer && program->has_native_calls) {
        if (!prepared_pure_lower_native_code(program))
            return false;
        goto entries;
    }
    for (size_t h = 0u; h < program->head_len; h++) {
        const PreparedPureHead *head = &program->heads[h];
        if (!head->compiled || (producer && !head->deterministic))
            continue;
        if (head->first_equation > program->equation_len ||
            head->equation_count >
                program->equation_len - head->first_equation)
            return false;
        for (uint32_t i = 0u; i < head->equation_count; i++) {
            PreparedPureEquation *equation =
                &program->equations[head->first_equation + i];
            if (!prepared_pure_lower_code_body(
                    program, equation->body.root, equation->body.local_count, 0u,
                    false, true, &equation->body.first_code,
                    &equation->body.register_count))
                return false;
        }
    }
    if (!producer) {
        if (!prepared_pure_lower_code_body(
                program, program->body.root, program->body.local_count,
                (uint32_t)program->entry_argument_count, true, true,
                &program->body.first_code, &program->body.register_count))
            return false;
    } else {
        for (size_t e = 0u; e < program->equation_len + program->body_entry; e++) {
            const PreparedPureBody *body = e == program->equation_len
                ? &program->body : &program->equations[e].body;
            if (body->tail_only)
                continue;
            if (body->first_step > program->step_len ||
                body->step_count >
                    program->step_len - body->first_step)
                return false;
            for (uint32_t k = 0u; k < body->step_count; k++) {
                PreparedPureStep *step =
                    &program->steps[body->first_step + k];
                if ((step->kind != PREPARED_PURE_STEP_EVAL &&
                     step->kind != PREPARED_PURE_STEP_RETURN) ||
                    step->value_tree)
                    continue;
                uint32_t first_code = 0u;
                uint32_t register_count = 0u;
                if (!prepared_pure_lower_code_body(
                        program, step->node, body->frame_slot_count, 0u,
                        false, false, &first_code, &register_count))
                    return false;
                step = &program->steps[body->first_step + k];
                step->coded = true;
                step->first_code = first_code;
                step->register_count = register_count;
            }
        }
    }
entries:
    free(program->code_entries);
    program->code_entries = calloc(
        program->head_len ? program->head_len : 1u,
        sizeof(*program->code_entries));
    if (!program->code_entries)
        return false;
    for (size_t index = 0u; index < program->head_len; index++) {
        if (!prepared_pure_code_entry(
                program, &program->heads[index],
                &program->code_entries[index]))
            return false;
    }
    for (size_t index = 0u; index < program->code_len; index++) {
        PreparedPureCode *code = &program->code[index];
        if (code->kind != PREPARED_PURE_CODE_CALL &&
            code->kind != PREPARED_PURE_CODE_TAIL_CALL)
            continue;
        const PreparedPureCodeEntry *entry =
            &program->code_entries[code->auxiliary];
        code->entry = entry->arity == code->operand_count
            ? entry->kind : PREPARED_PURE_CODE_ENTRY_GENERAL;
    }
    return true;
}

static bool prepared_pure_head_takes_arity(
    const CettaPreparedPureProgram *program,
    const PreparedPureHead *head, uint32_t arity);

/* Native-aware lowerings require elaborated arity and a fixed executable
 * head before entry. This admission belongs to the compiled plan, for roots
 * and named calls alike. */
static bool prepared_pure_native_calls_supported(const CettaPreparedPureProgram *program) {
    if (!program->has_native_calls)
        return true;
    for (size_t index = 0u; index < program->node_len; index++) {
        const PreparedPureNode *node = &program->nodes[index];
        if (node->kind == PREPARED_PURE_CALL &&
            (node->auxiliary >= program->head_len ||
             !prepared_pure_head_takes_arity(program, &program->heads[node->auxiliary],
                node->child_count)))
            return false;
        if (node->kind == PREPARED_PURE_BUILD &&
            node->auxiliary == PREPARED_PURE_BUILD_DATA_HEAD)
            return false;
    }
    return true;
}

static void prepared_pure_compile_failure_report(
        CettaPreparedPureCompileReport *report, Space *space,
        const CettaPreparedPureProgram *program, bool generic_entry,
        bool entry_values, CettaGsltPureCallMode mode, bool answers) {
    if (!report)
        return;
    report->refusals = 0u;
    if (!space_program_token_matches_live_space(report->source, space)) {
        report->status = CETTA_PREPARED_PURE_COMPILE_INVALIDATED;
        return;
    }
    if (program && program->compile_inspection_incomplete) {
        report->status = CETTA_PREPARED_PURE_COMPILE_RESOURCE_REFUSED;
        return;
    }
    if (program && generic_entry && entry_values && mode == CETTA_GSLT_PURE_CALL_EAGER) {
        if (program->compile_shared_view_refused) {
            report->refusals = answers ? CETTA_PREPARED_PURE_REFUSE_ANSWERS
                                      : CETTA_PREPARED_PURE_REFUSE_SCALAR;
            if (!program->compile_result_demand_observed)
                report->refusals |= CETTA_PREPARED_PURE_REFUSE_SCALAR |
                                    CETTA_PREPARED_PURE_REFUSE_ANSWERS;
        }
        if (program->compile_answer_gate_refused)
            report->refusals |= CETTA_PREPARED_PURE_REFUSE_ANSWERS;
    }
    uint8_t current = answers ? CETTA_PREPARED_PURE_REFUSE_ANSWERS
                              : CETTA_PREPARED_PURE_REFUSE_SCALAR;
    if ((report->refusals & current) != 0u)
        report->status = CETTA_PREPARED_PURE_COMPILE_STABLE_REFUSED;
}

static CettaPreparedPureProgram *
prepared_pure_program_compile_closed_mode(
    Space *space, Atom *expression,
    CettaGsltPureCallMode call_mode,
    CettaPreparedPureBooleanValue boolean_value,
    CettaPreparedPureConstructValue construct_value,
    CettaPreparedPureConstructsExpression constructs_expression,
    CettaPreparedPureOpaqueValue opaque_value,
    CettaPreparedPureRegisterViewFn register_view,
    CettaPreparedPureExpressionViewFn expression_view,
    CettaPreparedPurePatternViewFn pattern_view,
    const CettaPreparedPureSourceView *source_view,
    bool entry_arguments_are_values,
    bool total_structural_equality,
    CettaMatchDecisionSemanticIdentity match_decision_semantics,
    bool answer_producer, CettaPreparedPureCompileReport *report) {
    if (report)
        *report = (CettaPreparedPureCompileReport){
            .status = CETTA_PREPARED_PURE_COMPILE_INPUT_REFUSED,
        };
    if (!space || !expression || atom_has_vars(expression) ||
        !boolean_value || !construct_value ||
        (call_mode != CETTA_GSLT_PURE_CALL_EAGER &&
         call_mode != CETTA_GSLT_PURE_CALL_CALL_BY_NEED) ||
        (answer_producer &&
         (call_mode != CETTA_GSLT_PURE_CALL_EAGER ||
          !entry_arguments_are_values)))
        return NULL;
    SpaceProgramToken observed = space_program_token(space);
    if (report)
        *report = (CettaPreparedPureCompileReport){
            .source = observed, .status = CETTA_PREPARED_PURE_COMPILE_UNCERTIFIED,
        };
    if (expression->kind == ATOM_EXPR && expression->expr.len > 0u) {
        Atom *root_head = expression->expr.elems[0];
        CettaExprLen root_arity = expression->expr.len - 1u;
        bool generated_expression =
            root_head && root_head->kind == ATOM_SYMBOL &&
            (cetta_gslt_fold_control_program(
                 root_head->sym_id, root_arity, NULL, &g_builtin_syms) ||
             prepared_pure_register_program(
                 NULL, root_head->sym_id, root_arity, NULL, NULL) ||
             cetta_prepared_pure_intrinsic_program(
                 root_head->sym_id, root_arity, NULL));
        HeMacroArgument macro_arguments[HE_MACRO_MAX_ARITY];
        if (!generated_expression && root_head &&
            root_head->kind == ATOM_SYMBOL &&
            !cetta_prepared_pure_call_policy_supported(
                space, root_head->sym_id,
                root_arity) &&
            !he_macro_signature(space, expression, macro_arguments, NULL))
            return NULL;
    }
    CettaPreparedPureProgram *program = prepared_pure_program_new(
        space, call_mode, boolean_value, construct_value, constructs_expression,
        opaque_value, register_view, expression_view, pattern_view, source_view,
        total_structural_equality, match_decision_semantics, report);
    if (!program) {
        prepared_pure_compile_failure_report(report, space, NULL, false,
            entry_arguments_are_values, call_mode, answer_producer);
        return NULL;
    }
    program->closed_program = true;
    program->answer_producer = answer_producer;
    program->allow_callable_templates = true;
    PreparedPureCompileContext context = {0};
    bool entry_call_admitted = false;
    bool compiled = prepared_pure_compile_closed_entry_call(
        program, expression, entry_arguments_are_values,
        &entry_call_admitted, &program->body.root);
    if (compiled && !entry_call_admitted) {
        compiled = prepared_pure_compile_eval(
            program, &context, expression,
            program->source_view.root, 0u, &program->body.root);
    }
    program->body.local_count = context.next_slot;
    free(context.bindings);
    if (compiled)
        compiled = prepared_pure_compile_pending_heads(program);
    if (compiled)
        compiled = prepared_pure_native_calls_supported(program);
    if (!compiled) {
        prepared_pure_compile_failure_report(report, space, program,
            entry_call_admitted, entry_arguments_are_values, call_mode, answer_producer);
        cetta_prepared_pure_program_free(program);
        return NULL;
    }
    if (answer_producer && !prepared_pure_lower_answer_producer(program)) {
        prepared_pure_compile_failure_report(report, space, NULL, false,
            entry_arguments_are_values, call_mode, answer_producer);
        cetta_prepared_pure_program_free(program);
        return NULL;
    }
    if (call_mode == CETTA_GSLT_PURE_CALL_EAGER &&
        prepared_pure_code_enabled()) {
        program->code_ready = prepared_pure_lower_code(program);
        if (prepared_pure_debug_enabled())
            fprintf(stderr, "prepared-pure code: %s\n",
                    program->code_ready
                        ? "bodies lowered to register code"
                        : "a body left to the node executor");
    }
    if (!space_program_token_matches_live_space(observed, space)) {
        prepared_pure_compile_failure_report(report, space, NULL, false,
            entry_arguments_are_values, call_mode, answer_producer);
        cetta_prepared_pure_program_free(program);
        return NULL;
    }
    if (report)
        report->status = CETTA_PREPARED_PURE_COMPILE_READY;
    return program;
}

CettaPreparedPureProgram *cetta_prepared_pure_program_compile_closed(
    Space *space, Atom *expression,
    CettaGsltPureCallMode call_mode,
    CettaPreparedPureBooleanValue boolean_value,
    CettaPreparedPureConstructValue construct_value,
    CettaPreparedPureConstructsExpression constructs_expression,
    CettaPreparedPureOpaqueValue opaque_value,
    CettaPreparedPureRegisterViewFn register_view,
    CettaPreparedPureExpressionViewFn expression_view,
    CettaPreparedPurePatternViewFn pattern_view,
    const CettaPreparedPureSourceView *source_view,
    bool entry_arguments_are_values,
    bool total_structural_equality,
    CettaMatchDecisionSemanticIdentity match_decision_semantics,
    CettaPreparedPureCompileReport *report) {
    return prepared_pure_program_compile_closed_mode(
        space, expression, call_mode, boolean_value, construct_value,
        constructs_expression, opaque_value, register_view,
        expression_view, pattern_view,
        source_view, entry_arguments_are_values,
        total_structural_equality, match_decision_semantics, false, report);
}

CettaPreparedPureProgram *cetta_prepared_pure_program_compile_closed_answers(
    Space *space, Atom *expression,
    CettaGsltPureCallMode call_mode,
    CettaPreparedPureBooleanValue boolean_value,
    CettaPreparedPureConstructValue construct_value,
    CettaPreparedPureConstructsExpression constructs_expression,
    CettaPreparedPureOpaqueValue opaque_value,
    CettaPreparedPureRegisterViewFn register_view,
    CettaPreparedPureExpressionViewFn expression_view,
    CettaPreparedPurePatternViewFn pattern_view,
    const CettaPreparedPureSourceView *source_view,
    bool entry_arguments_are_values,
    bool total_structural_equality,
    CettaMatchDecisionSemanticIdentity match_decision_semantics,
    CettaPreparedPureCompileReport *report) {
    return prepared_pure_program_compile_closed_mode(
        space, expression, call_mode, boolean_value, construct_value,
        constructs_expression, opaque_value, register_view,
        expression_view, pattern_view,
        source_view, entry_arguments_are_values,
        total_structural_equality, match_decision_semantics, true, report);
}

CettaPreparedPureProgram *cetta_prepared_pure_program_compile_body_answers(
    Space *space, Atom *expression, const VarId *parameters, uint32_t arity,
    CettaPreparedPureBooleanValue boolean_value,
    CettaPreparedPureConstructValue construct_value,
    CettaPreparedPureConstructsExpression constructs_expression,
    CettaPreparedPureOpaqueValue opaque_value,
    CettaPreparedPureRegisterViewFn register_view,
    CettaPreparedPureExpressionViewFn expression_view,
    CettaPreparedPurePatternViewFn pattern_view,
    const CettaPreparedPureSourceView *source_view,
    bool total_structural_equality,
    CettaMatchDecisionSemanticIdentity match_decision_semantics) {
    if (!expression || arity > 64u || (arity && !parameters))
        return NULL;
    CettaPreparedPureProgram *program = prepared_pure_program_new(
        space, CETTA_GSLT_PURE_CALL_EAGER, boolean_value, construct_value,
        constructs_expression, opaque_value, register_view, expression_view,
        pattern_view, source_view, total_structural_equality,
        match_decision_semantics, NULL);
    if (!program)
        return NULL;
    program->closed_program = true;
    program->answer_producer = true;
    program->body_entry = true;
    program->exports_boolean_results = true;
    program->allow_callable_templates = true;
    program->entry_argument_count = arity;
    PreparedPureCompileContext context = {0};
    bool compiled = true;
    for (uint32_t index = 0u; compiled && index < arity; index++) {
        for (uint32_t prior = 0u; prior < index; prior++)
            if (var_id_eq(parameters[prior], parameters[index]))
                compiled = false;
        uint32_t slot = UINT32_MAX;
        compiled = compiled && !var_id_is_none(parameters[index]) &&
            prepared_pure_context_bind(&context, parameters[index], false, &slot) &&
            slot == index;
    }
    if (compiled)
        compiled = prepared_pure_compile_eval(program, &context, expression,
            program->source_view.root, 0u, &program->body.root);
    program->body.local_count = context.next_slot;
    free(context.bindings);
    if (!compiled || !prepared_pure_compile_pending_heads(program) ||
        !prepared_pure_native_calls_supported(program) ||
        !prepared_pure_lower_answer_producer(program)) {
        cetta_prepared_pure_program_free(program);
        return NULL;
    }
    if (prepared_pure_code_enabled())
        program->code_ready = prepared_pure_lower_code(program);
    return program;
}

bool cetta_prepared_pure_program_accepts_open_values(
    const CettaPreparedPureProgram *program) {
    return program && program->accepts_open_values;
}

size_t cetta_prepared_pure_program_root_local_count(
        const CettaPreparedPureProgram *program) {
    return program ? program->body.local_count : 0u;
}

static bool prepared_pure_is_cached_boolean(const Atom *atom, void *context) {
    const CettaPreparedPureProgram *program = context;
    return atom == &program->boolean_atoms[0] ||
           atom == &program->boolean_atoms[1];
}

bool cetta_prepared_pure_program_use_exportable_booleans(
    CettaPreparedPureProgram *program) {
    if (!program || program->closed_program || program->code_ready ||
        program->frame_len || program->value_len || program->slot_len)
        return false;
    if (program->boolean_atoms_ready) {
        for (size_t index = 0u; index < program->node_len; index++) {
            if (program->nodes[index].atom) {
                bool complete = false;
                bool private_boolean = atom_graph_any_checked(program->nodes[index].atom,
                    prepared_pure_is_cached_boolean, NULL, program, SIZE_MAX, &complete);
                if (!complete || private_boolean)
                    return false;
            }
        }
    }
    program->exports_boolean_results = true;
    return true;
}

bool cetta_prepared_pure_program_is_current(
    const CettaPreparedPureProgram *program) {
    return program &&
           space_program_token_is_current(program->source_program);
}

bool cetta_prepared_pure_program_rebind_closed_invocation(
    CettaPreparedPureProgram *program, const CettaInvocation *invocation) {
    CettaExprLen arity = 0u;
    if (!program || !program->closed_program ||
        !cetta_invocation_arity(invocation, &arity) ||
        binding_value_is_contextual(invocation->head) ||
        invocation->head.skeleton->kind != ATOM_SYMBOL ||
        program->body.root >= program->node_len ||
        program->entry_head == SYMBOL_ID_NONE ||
        program->entry_head != invocation->head.skeleton->sym_id ||
        program->entry_argument_count != arity ||
        (arity && !program->entry_arguments))
        return false;
    /* Stage every operand before publishing any: a refused late operand
     * must not leave a mixture of two invocations in a reusable program. */
    for (CettaExprIndex index = 0u; index < arity; index++) {
        BindingValue value = cetta_invocation_argument(invocation, index);
        if (binding_value_is_contextual(value) || !value.skeleton ||
            atom_has_vars(value.skeleton))
            return false;
    }
    for (CettaExprIndex index = 0u; index < arity; index++)
        program->entry_arguments[index] =
            cetta_invocation_argument(invocation, index).skeleton;
    return true;
}

bool cetta_prepared_pure_program_rebind_closed_entry_call(
    CettaPreparedPureProgram *program, Atom *expression) {
    if (!expression || expression->kind != ATOM_EXPR || !expression->expr.len)
        return false;
    CettaInvocation invocation = {
        .head = binding_value_from_atom(expression->expr.elems[0]),
        .supplied = {.atoms = expression->expr.elems + 1u,
            .count = expression->expr.len - 1u},
    };
    return cetta_prepared_pure_program_rebind_closed_invocation(program, &invocation);
}

void cetta_prepared_pure_program_clear_closed_entry_call(
    CettaPreparedPureProgram *program) {
    if (!program || !program->entry_arguments)
        return;
    memset(program->entry_arguments, 0,
           sizeof(*program->entry_arguments) *
               program->entry_argument_count);
}

/* Whether an equation of `head` takes `arity` arguments. */
static bool prepared_pure_head_takes_arity(
    const CettaPreparedPureProgram *program, const PreparedPureHead *head,
    uint32_t arity) {
    for (uint32_t index = 0u; index < head->equation_count; index++) {
        if (program->equations[head->first_equation + index].arity == arity)
            return true;
    }
    return false;
}

/* Where an answer of a call goes: the step after the call in its caller's
 * step program, over the caller's locals as they stood at the call.  A NULL
 * continuation is the cursor's consumer.  Saved values are semantically
 * immutable; detachment may relocate their storage.  Every answer of the
 * call resumes the same state. */
typedef struct PreparedPureContinuation PreparedPureContinuation;
typedef struct PreparedPureNativeFrame PreparedPureNativeFrame;
struct PreparedPureContinuation {
    const PreparedPureContinuation *parent;
    Atom **locals;
    uint32_t body_index;
    uint32_t step;
    uint32_t slot;
    /* Detachment visits shared continuation tails only once. */
    bool locals_detached;
};

typedef struct {
    uint32_t head_index;
    uint32_t arity;
    uint32_t next_equation;
    size_t argument_base;
    bool matched_equation;
    /* Value-arena position before this call's arguments were built.  A call
     * entered as a last call inherits its caller's position, because its
     * arguments may share the caller's. */
    ArenaMark mark;
    /* Position once the call was entered: what its earlier equations built
     * lies above it and is dead when it tries the next. */
    ArenaMark alternative_mark;
    union {
        const PreparedPureContinuation *continuation;
        PreparedPureNativeFrame *native;
    };
} PreparedPureAnswerFrame;

#define PREPARED_PURE_NATIVE_FRAME UINT32_MAX

typedef struct PreparedPureNativeBind {
    CettaOwnedBind bind;
    CettaOwnedExecution replacement;
    CettaCallReturnCursor identity;
    struct PreparedPureNativeBind *previous;
    bool completes_callers;
} PreparedPureNativeBind;

struct PreparedPureNativeFrame {
    CettaCallReturnCursor returns;
    CettaOwnedExecution producer;
    const PreparedPureContinuation *continuation;
    PreparedPureNativeBind *bindings;
    Atom *head;
    bool entered;
    bool body_active;
    bool transferred;
};

/* Build a value tree over `locals`: matched variables for a direct answer,
 * or the running step program's locals. */
static bool prepared_pure_project_answer_value(
    CettaPreparedPureProgram *program, Arena *arena,
    Atom *const *locals, uint32_t local_count, uint32_t node_index,
    uint32_t depth, const CettaPreparedPureAnswerLimits *limits,
    size_t *scratch_remaining, bool *limit_hit, Atom **value_out) {
    if (value_out)
        *value_out = NULL;
    if (!program || !arena || (!locals && local_count > 0u) ||
        !value_out || node_index >= program->node_len ||
        depth > PREPARED_PURE_MAX_COMPILE_DEPTH)
        return false;
    const PreparedPureNode *node = &program->nodes[node_index];
    if (node->kind == PREPARED_PURE_LITERAL) {
        *value_out = node->atom;
        return *value_out != NULL;
    }
    if (node->kind == PREPARED_PURE_SLOT) {
        if (node->auxiliary >= local_count)
            return false;
        *value_out = locals[node->auxiliary];
        return *value_out != NULL;
    }
    if (node->kind != PREPARED_PURE_BUILD ||
        node->first_child > program->child_len ||
        node->child_count > program->child_len - node->first_child)
        return false;

    size_t allocation_bound = 0u;
    if (!limits->construct_allocation_bound)
        return false;
    if (!limits->construct_allocation_bound(
            node->child_count, &allocation_bound) ||
        allocation_bound > *scratch_remaining) {
        *limit_hit = true;
        return false;
    }
    *scratch_remaining -= allocation_bound;

    enum { PREPARED_PURE_INLINE_ANSWER_CHILDREN = 16u };
    Atom *inline_children[PREPARED_PURE_INLINE_ANSWER_CHILDREN];
    Atom **children = node->child_count <=
            PREPARED_PURE_INLINE_ANSWER_CHILDREN
        ? inline_children
        : malloc(sizeof(*children) * node->child_count);
    if (!children)
        return false;
    bool ok = true;
    for (uint32_t child = 0u; child < node->child_count; child++) {
        if (!prepared_pure_project_answer_value(
                program, arena, locals, local_count,
                program->children[node->first_child + child],
                depth + 1u, limits, scratch_remaining, limit_hit,
                &children[child])) {
            ok = false;
            break;
        }
    }
    if (ok)
        *value_out = program->construct_value(
            arena, children, node->child_count);
    if (children != inline_children)
        free(children);
    return ok && *value_out != NULL;
}

/* A cursor's arena starts with a small block and grows geometrically: most
 * cursors build little, and a suspended one keeps only what it holds. */
#define PREPARED_PURE_CURSOR_FIRST_BLOCK 256u

struct CettaPreparedPureAnswerCursor {
    CettaPreparedPureProgram *program;
    Arena *arena;
    Arena owned_arena;
    bool owns_arena;
    bool borrowed_until_escape;
    /* The frontier's values all live in the owned arena. */
    bool detached;
    CettaPreparedPureUnmatchedCall unmatched_call;
    CettaPreparedPureAnswerLimits limits;
    uint64_t remaining_transitions;
    /* A caller arena is charged and never recharged.  An owned arena bounds
     * the bytes live above base_live, so reclaimed storage is recharged. */
    size_t scratch_remaining;
    size_t base_live;
    size_t next_collection_bytes;
    uint64_t collections;
    uint64_t copied_bytes;
    CettaPreparedPureInterruptPollFn interrupt_poll;
    void *interrupt_context;
    uint32_t poll_interval;
    uint32_t poll_countdown;
    PreparedPureAnswerFrame *frames;
    size_t frame_len;
    size_t frame_cap;
    Atom **arguments;
    size_t argument_len;
    size_t argument_cap;
    /* Owned arena: the storage of the last answer, dead once consumed. */
    bool answer_pending;
    ArenaMark answer_mark;
    /* The step that produced the last answer, for unyield. */
    bool undo_valid;
    size_t undo_frame;
    uint32_t undo_next_equation;
    bool undo_matched_equation;
    uint8_t *head_seen;
    SymbolId *seen_heads;
    uint32_t seen_head_len;
    CettaPreparedPureHeadAdmissionFn head_admission;
    void *head_admission_context;
    uint64_t answers;
    uint64_t tail_calls;
    CettaPreparedPureHandoffReason handoff;
    CettaCallOutcome last_return;
    bool native_started;
    bool native_frontier_owned;
    bool native_transfer_requested;
    bool raised_return;
    bool transition_boundary;
    size_t native_metadata_bytes;
    CettaEvalCompletion completion;
    PreparedPureNativeFrame *native_spare;
    bool (*continuation_current)(const void *context);
    const void *continuation_context;
    CettaPreparedPureAnswerAdmissionFn answer_admission;
    void *answer_admission_context;
};

static __attribute__((noinline, cold)) size_t prepared_pure_gc_input_arenas(
        const CettaPreparedPureAnswerCursor *cursor, const Arena *arena,
        const Arena *survivor, uint32_t *identities, size_t capacity) {
    size_t count = 0u;
    for (size_t index = 0u; index < cursor->argument_len; index++) {
        const Atom *argument = cursor->arguments[index];
        uint32_t identity = argument ? argument->arena_id : 0u;
        if (!identity ||
            identity == arena->identity ||
            (survivor && identity == survivor->identity))
            continue;
        size_t found = 0u;
        while (found < count && identities[found] != identity)
            found++;
        if (found == count && count < capacity)
            identities[count++] = identity;
    }
    return count;
}

static bool prepared_pure_answer_cursor_detach_with_session(
    CettaPreparedPureAnswerCursor *cursor, AtomDeepCopySession *session);

/* Record a relation's first entry and ask whether it is admitted. */
static bool prepared_pure_answer_cursor_see_head(
    CettaPreparedPureAnswerCursor *cursor, uint32_t head_index) {
    if (head_index >= cursor->program->head_len)
        return false;
    if (cursor->head_seen[head_index])
        return true;
    cursor->head_seen[head_index] = 1u;
    SymbolId head = cursor->program->heads[head_index].head;
    cursor->seen_heads[cursor->seen_head_len++] = head;
    return !cursor->head_admission ||
        cursor->head_admission(cursor->head_admission_context, head);
}

/* Reserve room for one more call before anything is written, so a failed
 * push leaves the frontier intact. */
static bool prepared_pure_answer_cursor_reserve(
    CettaPreparedPureAnswerCursor *cursor, uint32_t arity) {
    return arity <= 64u &&
        cursor->argument_len <= SIZE_MAX - arity &&
        prepared_pure_reserve(
            (void **)&cursor->frames, sizeof(*cursor->frames),
            &cursor->frame_cap, cursor->frame_len + 1u) &&
        prepared_pure_reserve(
            (void **)&cursor->arguments, sizeof(*cursor->arguments),
            &cursor->argument_cap, cursor->argument_len + arity);
}

/* A fresh entry rolls back to its entry position. Tail replacement instead
 * supplies the earlier caller position, keeping the two marks distinct. */
static void prepared_pure_answer_cursor_push_reserved(
    CettaPreparedPureAnswerCursor *cursor, uint32_t head_index,
    Atom *const *arguments, uint32_t arity, const ArenaMark *rollback_mark,
    const PreparedPureContinuation *continuation) {
    ArenaMark entered = arena_mark(cursor->arena);
    size_t base = cursor->argument_len;
    if (arity > 0u)
        memcpy(&cursor->arguments[base], arguments,
               sizeof(*cursor->arguments) * arity);
    cursor->frames[cursor->frame_len++] = (PreparedPureAnswerFrame){
        .head_index = head_index,
        .arity = arity,
        .argument_base = base,
        .mark = rollback_mark ? *rollback_mark : entered,
        .alternative_mark = entered,
        .continuation = continuation,
    };
    cursor->argument_len += arity;
}

/* Replacing an exhausted call transfers its arena position, without
 * resetting storage still read by the actual callee or continuation. A
 * native producer keeps its loan until its children retire. */
static ArenaMark prepared_pure_answer_cursor_retire_call(
        CettaPreparedPureAnswerCursor *cursor, size_t index, ArenaMark mark) {
    if (index + 1u != cursor->frame_len)
        return mark;
    const PreparedPureAnswerFrame *frame = &cursor->frames[index];
    if (frame->head_index == PREPARED_PURE_NATIVE_FRAME ||
        (frame->head_index == PREPARED_PURE_ROOT_FRAME
            ? frame->next_equation == 0u
            : frame->head_index >= cursor->program->head_len ||
              frame->next_equation < cursor->program->heads[frame->head_index].equation_count))
        return mark;
    mark = frame->mark;
    cursor->argument_len = frame->argument_base;
    cursor->frame_len--;
    return mark;
}

static size_t prepared_pure_answer_cursor_scratch(
    const CettaPreparedPureAnswerCursor *cursor) {
    if (!cursor->owns_arena)
        return cursor->scratch_remaining;
    size_t live = arena_accounted_live_bytes(cursor->arena);
    size_t used = live > cursor->base_live ? live - cursor->base_live : 0u;
    return used < cursor->limits.max_scratch_bytes
        ? cursor->limits.max_scratch_bytes - used : 0u;
}

/* Copy only an owned graph. Permanent symbol IDs and immediate scalars may
 * come from the program; a foreign graph needs its own lifetime contract.
 * Returning NULL abandons the fresh copy without changing the source. */
static Atom *prepared_pure_answer_cursor_copy_owned(void *context, Atom *atom) {
    const Arena *source = context;
    if (atom->arena_id == source->identity || atom->kind == ATOM_SYMBOL)
        return atom;
    if (atom->kind == ATOM_GROUNDED &&
        (atom->ground.gkind == GV_INT || atom->ground.gkind == GV_FLOAT ||
         atom->ground.gkind == GV_BOOL))
        return atom;
    return NULL;
}

typedef struct {
    const PreparedPureContinuation *source;
    PreparedPureContinuation *target;
} PreparedPureContinuationCopy;

typedef struct {
    PreparedPureContinuationCopy *entries;
    size_t capacity;
    size_t length;
} PreparedPureContinuationCopies;

static size_t prepared_pure_continuation_hash(
    const PreparedPureContinuation *record) {
    uintptr_t hash = (uintptr_t)record >> 3u;
    hash ^= hash >> 17u;
    hash *= UINT64_C(0x9e3779b97f4a7c15);
    return (size_t)(hash ^ (hash >> 23u));
}

static PreparedPureContinuationCopy *prepared_pure_continuation_copy_slot(
    PreparedPureContinuationCopies *copies,
    const PreparedPureContinuation *source) {
    size_t slot = prepared_pure_continuation_hash(source) &
                  (copies->capacity - 1u);
    while (copies->entries[slot].source &&
           copies->entries[slot].source != source)
        slot = (slot + 1u) & (copies->capacity - 1u);
    return &copies->entries[slot];
}

static bool prepared_pure_continuation_copies_reserve(
    PreparedPureContinuationCopies *copies) {
    if (copies->capacity && copies->length < copies->capacity / 2u)
        return true;
    size_t capacity = copies->capacity ? copies->capacity * 2u : 32u;
    if (capacity <= copies->capacity ||
        capacity > SIZE_MAX / sizeof(*copies->entries))
        return false;
    PreparedPureContinuationCopy *entries =
        calloc(capacity, sizeof(*entries));
    if (!entries)
        return false;
    PreparedPureContinuationCopies grown = {
        .entries = entries,
        .capacity = capacity,
        .length = copies->length,
    };
    for (size_t index = 0u; index < copies->capacity; index++) {
        if (copies->entries[index].source)
            *prepared_pure_continuation_copy_slot(
                &grown, copies->entries[index].source) = copies->entries[index];
    }
    free(copies->entries);
    *copies = grown;
    return true;
}

static const PreparedPureBody *prepared_pure_continuation_body(
        const CettaPreparedPureAnswerCursor *cursor, const PreparedPureContinuation *source) {
    if (!arena_owns_ptr(cursor->arena, source))
        return NULL;
    const PreparedPureBody *body = prepared_pure_body(cursor->program, source->body_index);
    if (!body || source->slot >= body->frame_slot_count ||
        source->step < body->first_step || source->step - body->first_step >= body->step_count ||
        !source->locals || !arena_owns_ptr(cursor->arena, source->locals))
        return NULL;
    for (uint32_t slot = 0u; slot < body->frame_slot_count; slot++) {
        Atom *value = source->locals[slot];
        if (value && (atom_has_identity_grounded(value) || atom_has_thread_local_resource(value)))
            return NULL;
    }
    return body;
}

/* Continuation records and their local arrays are roots as well as the atoms
 * in those arrays. One map preserves shared tails; the same atom-copy episode
 * is used for arguments and every saved local. No source record is modified. */
static bool prepared_pure_answer_cursor_copy_continuation(
    CettaPreparedPureAnswerCursor *cursor, Arena *fresh,
    AtomDeepCopySession *copy, PreparedPureContinuationCopies *copies,
    const PreparedPureContinuation *source,
    const PreparedPureContinuation **target_out) {
    PreparedPureContinuation *previous = NULL;
    *target_out = NULL;
    while (source) {
        if (!prepared_pure_continuation_copies_reserve(copies))
            return false;
        PreparedPureContinuationCopy *entry =
            prepared_pure_continuation_copy_slot(copies, source);
        if (entry->source) {
            if (previous)
                previous->parent = entry->target;
            else
                *target_out = entry->target;
            return true;
        }
        const PreparedPureBody *body = prepared_pure_continuation_body(cursor, source);
        if (!body)
            return false;
        uint32_t count = body->frame_slot_count;
        if (fresh == cursor->arena && source->locals_detached) {
            /* Detachment already owns this immutable suffix under the
             * cursor's reset marks. Preserve its actual shared records;
             * relocating to a different arena still copies every node. */
            for (const PreparedPureContinuation *tail = source->parent; tail; tail = tail->parent)
                if (!tail->locals_detached || !prepared_pure_continuation_body(cursor, tail))
                    return false;
            if (previous)
                previous->parent = source;
            else
                *target_out = source;
            return true;
        }
        PreparedPureContinuation *target = arena_alloc(fresh, sizeof(*target));
        Atom **locals = arena_alloc(fresh, sizeof(*locals) * (count ? count : 1u));
        if (!target || !locals)
            return false;
        for (uint32_t slot = 0u; slot < count; slot++) {
            Atom *value = source->locals[slot];
            locals[slot] = value ? atom_deep_copy_session_copy(copy, value) : NULL;
            if (value && !locals[slot])
                return false;
        }
        *target = *source;
        target->locals = locals;
        target->parent = NULL;
        target->locals_detached = true;
        *entry = (PreparedPureContinuationCopy){source, target};
        copies->length++;
        if (previous)
            previous->parent = target;
        else
            *target_out = target;
        previous = target;
        source = source->parent;
    }
    return true;
}

/* Collection runs between transitions, after the previous answer's loan and
 * unyield window have expired. Arguments, pending alternatives and complete
 * saved continuations are copied before the old arena is retired. All copied
 * roots lie below the rebased marks; later alternative/pop resets cannot free
 * a sibling's roots. Equation ordinals and the residual stack stay unchanged. */
static bool prepared_pure_answer_cursor_relocate(
    CettaPreparedPureAnswerCursor *cursor, bool compact,
    const PreparedPureContinuation **extra) {
    size_t before = arena_accounted_live_bytes(cursor->arena);
    if (cursor->argument_len > SIZE_MAX / sizeof(Atom *) ||
        cursor->frame_len > SIZE_MAX / sizeof(PreparedPureAnswerFrame))
        return false;
    for (size_t index = 0u; index < cursor->frame_len; index++) {
        const PreparedPureAnswerFrame *frame = &cursor->frames[index];
        if ((frame->head_index != PREPARED_PURE_ROOT_FRAME &&
             frame->head_index >= cursor->program->head_len) ||
            frame->argument_base > cursor->argument_len ||
            frame->arity > cursor->argument_len - frame->argument_base)
            return false;
    }
    for (size_t index = 0u; index < cursor->argument_len; index++) {
        const Atom *argument = cursor->arguments[index];
        if (!argument || atom_has_identity_grounded(argument) ||
            atom_has_thread_local_resource(argument))
            return false;
    }

    Arena fresh;
    arena_init_detached(&fresh);
    arena_set_runtime_kind(&fresh, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    arena_set_ledger_tag(&fresh, CETTA_ARENA_LEDGER_CURSOR);
    arena_set_block_growth(&fresh, PREPARED_PURE_CURSOR_FIRST_BLOCK);
    Atom **arguments = cursor->argument_len
        ? malloc(sizeof(*arguments) * cursor->argument_len) : NULL;
    PreparedPureAnswerFrame *frames =
        malloc(sizeof(*frames) * cursor->frame_len);
    PreparedPureContinuationCopies continuations = {0};
    AtomDeepCopySession *copy = atom_deep_copy_session_new(&fresh);
    bool ok = copy && frames && (!cursor->argument_len || arguments);
    if (copy && compact)
        atom_deep_copy_session_set_resolver(
            copy, prepared_pure_answer_cursor_copy_owned, cursor->arena);
    for (size_t index = 0u; ok && index < cursor->argument_len; index++) {
        arguments[index] = atom_deep_copy_session_copy(
            copy, cursor->arguments[index]);
        ok = arguments[index] != NULL;
    }
    for (size_t index = 0u; ok && index < cursor->frame_len; index++) {
        frames[index] = cursor->frames[index];
        ok = prepared_pure_answer_cursor_copy_continuation(
            cursor, &fresh, copy, &continuations,
            cursor->frames[index].continuation, &frames[index].continuation);
    }
    const PreparedPureContinuation *saved_extra = NULL;
    if (ok && extra && *extra)
        ok = prepared_pure_answer_cursor_copy_continuation(
            cursor, &fresh, copy, &continuations, *extra, &saved_extra);
    atom_deep_copy_session_free(copy);
    free(continuations.entries);
    size_t after = arena_accounted_live_bytes(&fresh);
    if (!ok || (compact && after > before - before / 4u) ||
        (!compact && after > cursor->scratch_remaining)) {
        free(frames);
        free(arguments);
        arena_free(&fresh);
        return false;
    }

    if (cursor->argument_len)
        memcpy(cursor->arguments, arguments,
               sizeof(*arguments) * cursor->argument_len);
    bool owned_before = cursor->owns_arena;
    Arena old = cursor->owned_arena;
    cursor->owned_arena = fresh;
    cursor->arena = &cursor->owned_arena;
    cursor->owns_arena = true;
    ArenaMark mark = arena_mark(cursor->arena);
    for (size_t index = 0u; index < cursor->frame_len; index++) {
        frames[index].mark = mark;
        frames[index].alternative_mark = mark;
    }
    memcpy(cursor->frames, frames, sizeof(*frames) * cursor->frame_len);
    free(frames);
    free(arguments);
    if (extra)
        *extra = saved_extra;
    cursor->answer_pending = false;
    cursor->undo_valid = false;
    cursor->detached = true;
    cursor->base_live = 0u;
    cursor->next_collection_bytes = after > SIZE_MAX / 2u
        ? SIZE_MAX : after * 2u;
    cursor->collections += compact;
    cursor->copied_bytes = after > UINT64_MAX - cursor->copied_bytes
        ? UINT64_MAX : cursor->copied_bytes + after;
    /* Value registers and memo entries are transition-local scratch, not
     * saved continuations. Forget their pointer caches before freeing storage. */
    prepared_pure_memo_clear(cursor->program);
    cursor->program->slot_len = 0u;
    if (owned_before)
        arena_free(&old);
    return true;
}


static void prepared_pure_answer_cursor_collect(
    CettaPreparedPureAnswerCursor *cursor) {
    enum { MIN_COLLECTION_BYTES = 64u * 1024u };
    if (!cursor->owns_arena || cursor->frame_len == 0u ||
        cursor->answer_pending || cursor->undo_valid)
        return;
    size_t before = arena_accounted_live_bytes(cursor->arena);
    size_t threshold = cursor->next_collection_bytes;
    if (threshold < MIN_COLLECTION_BYTES)
        threshold = MIN_COLLECTION_BYTES;
    if (before < threshold)
        return;
    cursor->next_collection_bytes = before > SIZE_MAX / 2u
        ? SIZE_MAX : before * 2u;
    (void)prepared_pure_answer_cursor_relocate(cursor, true, NULL);
}

static CettaPreparedPureCursorStep prepared_pure_answer_cursor_handoff(
    CettaPreparedPureAnswerCursor *cursor,
    CettaPreparedPureHandoffReason reason) {
    if (prepared_pure_debug_enabled()) {
        static const char *const names[] = {
            "none", "unsupported", "no match", "limit", "interrupt", "stale",
        };
        fprintf(stderr, "prepared-pure cursor handoff: %s\n",
                (size_t)reason < sizeof(names) / sizeof(names[0])
                    ? names[reason] : "unknown");
    }
    cursor->handoff = reason;
    return CETTA_PREPARED_PURE_CURSOR_HANDOFF;
}

/* Undo a step that failed after advancing its call: the frontier returns to
 * the state before the step, and an owned arena drops what it built. */
static void prepared_pure_answer_cursor_restore_step(
    CettaPreparedPureAnswerCursor *cursor, size_t frame_index,
    uint32_t next_equation, bool matched_equation, ArenaMark step_mark) {
    PreparedPureAnswerFrame *frame = &cursor->frames[frame_index];
    frame->next_equation = next_equation;
    frame->matched_equation = matched_equation;
    if (cursor->owns_arena)
        arena_reset(cursor->arena, step_mark);
}

bool cetta_prepared_pure_answer_cursor_set_admission(
        CettaPreparedPureAnswerCursor *cursor, CettaPreparedPureAnswerAdmissionFn admit,
        void *context) {
    if (!cursor || cursor->answers || cursor->native_started || cursor->frame_len != 1u ||
        cursor->frames[0].next_equation || cursor->handoff != CETTA_PREPARED_PURE_HANDOFF_NONE)
        return false;
    cursor->answer_admission = admit;
    cursor->answer_admission_context = context;
    return true;
}

static bool prepared_pure_he_entry_arguments_without_space(
    const CettaPreparedPureProgram *program) {
    for (size_t i = 0u; program->entry_arguments &&
             i < program->entry_argument_count; i++) {
        if (prepared_pure_he_value_holds_space(program->entry_arguments[i], NULL))
            return false;
    }
    return true;
}

static CettaPreparedPureAnswerCursor *prepared_pure_answer_cursor_open_entry(
    CettaPreparedPureProgram *program, Atom *const *arguments,
    uint32_t arity, uint32_t head_index,
    const CettaPreparedPureAnswerCursorOptions *options) {
    if (!program || !options || !program->answer_producer ||
        (program->continuation_steps && !options->allow_continuations) ||
        !cetta_prepared_pure_program_is_current(program) ||
        program->body.root >= program->node_len ||
        (options->arena && options->arena->hashcons))
        return NULL;
    CettaPreparedPureAnswerCursor *cursor = NULL;
    if (program->body_entry && options->arena && options->borrowed_until_escape) {
        cursor = program->body_cursor_spare;
        program->body_cursor_spare = NULL;
    }
    if (!cursor)
        cursor = calloc(1u, sizeof(*cursor));
    if (!cursor)
        return NULL;
    if (!cursor->head_seen)
        cursor->head_seen = calloc(program->head_len, sizeof(*cursor->head_seen));
    else
        memset(cursor->head_seen, 0, program->head_len * sizeof(*cursor->head_seen));
    if (!cursor->seen_heads)
        cursor->seen_heads = malloc(program->head_len * sizeof(*cursor->seen_heads));
    cursor->program = cetta_prepared_pure_program_retain(program);
    if ((program->head_len && (!cursor->head_seen || !cursor->seen_heads)) || !cursor->program) {
        cetta_prepared_pure_answer_cursor_close(cursor);
        return NULL;
    }
    cursor->borrowed_until_escape = options->borrowed_until_escape;
    cursor->unmatched_call = options->unmatched_call;
    cursor->limits = options->limits;
    cursor->remaining_transitions = options->limits.max_transitions;
    cursor->scratch_remaining = options->limits.max_scratch_bytes;
    cursor->interrupt_poll = options->interrupt_poll;
    cursor->interrupt_context = options->interrupt_context;
    cursor->poll_interval = options->interrupt_poll_interval
        ? options->interrupt_poll_interval : 1u;
    cursor->poll_countdown = cursor->poll_interval;
    cursor->head_admission = options->head_admission;
    cursor->head_admission_context = options->head_admission_context;
    cursor->continuation_current = options->continuation_current;
    cursor->continuation_context = options->continuation_context;
    if (options->arena) {
        cursor->arena = options->arena;
    } else {
        arena_init(&cursor->owned_arena);
        arena_set_runtime_kind(
            &cursor->owned_arena, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
        arena_set_ledger_tag(&cursor->owned_arena, CETTA_ARENA_LEDGER_CURSOR);
        arena_set_block_growth(
            &cursor->owned_arena, PREPARED_PURE_CURSOR_FIRST_BLOCK);
        arena_set_hashcons(&cursor->owned_arena, NULL);
        cursor->arena = &cursor->owned_arena;
        cursor->owns_arena = true;
    }

    bool ok = true;
    for (uint32_t index = 0u; ok && index < arity; index++)
        ok = arguments[index] != NULL;
    cursor->base_live = arena_accounted_live_bytes(cursor->arena);
    /* Sized for the entry call: most cursors stay one frame deep, and a
     * deeper one doubles from here. */
    if (ok && !cursor->frames) {
        cursor->frames = malloc(sizeof(*cursor->frames));
        cursor->frame_cap = cursor->frames ? 1u : 0u;
    }
    if (ok && arity && !cursor->arguments) {
        cursor->arguments = malloc(sizeof(*cursor->arguments) * arity);
        cursor->argument_cap = cursor->arguments ? arity : 0u;
    }
    ok = ok && prepared_pure_answer_cursor_reserve(cursor, arity);
    if (ok) {
        prepared_pure_answer_cursor_push_reserved(
            cursor, head_index, arguments, arity, NULL, NULL);
        ok = head_index == PREPARED_PURE_ROOT_FRAME ||
            prepared_pure_answer_cursor_see_head(cursor, head_index);
    }
    if (!ok) {
        cetta_prepared_pure_answer_cursor_close(cursor);
        return NULL;
    }
    return cursor;
}

CettaPreparedPureAnswerCursor *cetta_prepared_pure_answer_cursor_open(
    CettaPreparedPureProgram *program,
    const CettaPreparedPureAnswerCursorOptions *options) {
    if (!program || program->body_entry ||
        !prepared_pure_he_entry_arguments_without_space(program) ||
        program->body.root >= program->node_len)
        return NULL;
    const PreparedPureNode *entry = &program->nodes[program->body.root];
    if (entry->kind != PREPARED_PURE_CALL || entry->auxiliary >= program->head_len ||
        entry->child_count != program->entry_argument_count ||
        entry->child_count > 64u ||
        (entry->child_count && !program->entry_arguments))
        return NULL;
    return prepared_pure_answer_cursor_open_entry(program, program->entry_arguments,
        entry->child_count, entry->auxiliary, options);
}

CettaPreparedPureAnswerCursor *cetta_prepared_pure_answer_cursor_open_body(
    CettaPreparedPureProgram *program, Atom *const *arguments, uint32_t arity,
    const CettaPreparedPureAnswerCursorOptions *options) {
    if (!program || !program->body_entry || arity != program->entry_argument_count ||
        (arity && !arguments))
        return NULL;
    for (uint32_t index = 0u; index < arity; index++)
        if (!arguments[index] || atom_has_vars(arguments[index]) ||
            atom_has_identity_grounded(arguments[index]) ||
            atom_has_thread_local_resource(arguments[index]) ||
            prepared_pure_he_value_holds_space(arguments[index], NULL))
            return NULL;
    return prepared_pure_answer_cursor_open_entry(program, arguments, arity,
        PREPARED_PURE_ROOT_FRAME, options);
}

static bool PREPARED_PURE_HOT prepared_pure_program_execute_internal(
    CettaPreparedPureProgram *program, Arena *arena,
    Atom *accumulator, Atom *item, Atom *runtime_expression,
    uint32_t step_node, uint32_t step_slot_count,
    bool closed,
    size_t nursery_budget_bytes,
    CettaPreparedPureInterruptPollFn interrupt_poll,
    void *interrupt_context,
    Atom **result_out, CettaCallOutcome *outcome, bool *native_entered);

static bool PREPARED_PURE_HOT prepared_pure_run_code_from(
    CettaPreparedPureProgram *program, Arena *arena, uint32_t first_code,
    uint32_t register_count, bool step, size_t nursery_budget_bytes,
    CettaPreparedPureInterruptPollFn interrupt_poll,
    void *interrupt_context, Atom **result_out, CettaCallOutcome *outcome);

static bool PREPARED_PURE_HOT PREPARED_PURE_NOINLINE prepared_pure_run_owned_code_from(
    CettaPreparedPureProgram *program, Arena *arena, uint32_t first_code,
    uint32_t register_count, bool step, size_t nursery_budget_bytes,
    CettaPreparedPureInterruptPollFn interrupt_poll,
    void *interrupt_context, Atom **result_out,
    CettaPreparedPureAnswerCursor *cursor,
    const PreparedPureContinuation *continuation);

/* The call a frame stands for, as data. */
static Atom *prepared_pure_answer_cursor_call_atom(
    CettaPreparedPureAnswerCursor *cursor, size_t frame_index) {
    CettaPreparedPureProgram *program = cursor->program;
    const PreparedPureAnswerFrame *frame = &cursor->frames[frame_index];
    enum { PREPARED_PURE_INLINE_CALL_ELEMENTS = 17u };
    Atom *inline_elements[PREPARED_PURE_INLINE_CALL_ELEMENTS];
    Atom **elements = frame->arity < PREPARED_PURE_INLINE_CALL_ELEMENTS
        ? inline_elements
        : malloc(sizeof(*elements) * (frame->arity + 1u));
    if (!elements || frame->head_index >= program->head_len)
        return NULL;
    elements[0] = atom_symbol_id(
        cursor->arena, program->heads[frame->head_index].head);
    if (frame->arity > 0u)
        memcpy(&elements[1], &cursor->arguments[frame->argument_base],
               sizeof(*elements) * frame->arity);
    Atom *call = elements[0]
        ? program->construct_value(cursor->arena, elements, frame->arity + 1u)
        : NULL;
    if (elements != inline_elements)
        free(elements);
    return call;
}

typedef enum {
    PREPARED_PURE_RUN_CONTINUE = 0,
    PREPARED_PURE_RUN_ANSWER,
    PREPARED_PURE_RUN_HANDOFF,
} PreparedPureRunResult;

/* A caller arena is charged for what an executor evaluation built; an owned
 * arena is measured live. */
static bool prepared_pure_answer_cursor_charge(
    CettaPreparedPureAnswerCursor *cursor, size_t live_before) {
    if (cursor->owns_arena)
        return prepared_pure_answer_cursor_scratch(cursor) > 0u;
    size_t live = arena_accounted_live_bytes(cursor->arena);
    size_t built = live > live_before ? live - live_before : 0u;
    if (built > cursor->scratch_remaining) {
        cursor->scratch_remaining = 0u;
        return false;
    }
    cursor->scratch_remaining -= built;
    return true;
}

static void prepared_pure_native_clear(PreparedPureNativeFrame *native) {
    if (!native)
        return;
    cetta_owned_execution_cancel(&native->producer);
    PreparedPureNativeBind *node = native->bindings;
    while (node) {
        PreparedPureNativeBind *previous = node->previous;
        cetta_owned_execution_cancel(&node->replacement);
        free(node);
        node = previous;
    }
    memset(native, 0, sizeof(*native));
}

static void prepared_pure_native_release(PreparedPureNativeFrame *native) {
    if (!native)
        return;
    prepared_pure_native_clear(native);
    free(native);
}

static bool prepared_pure_native_replacement(void *context,
        const CettaOwnedReturn *returned, CettaOwnedExecution *body) {
    PreparedPureNativeBind *node = context;
    if (node->replacement.computation) {
        *body = node->replacement;
        node->replacement = (CettaOwnedExecution){0};
        return true;
    }
    if (node->completes_callers || returned->outcome.kind != CETTA_CALL_VALUE ||
        returned->outcome.result_form != CETTA_CALL_RESULT_COMPLETED_VALUE ||
        !returned->outcome.term || atom_has_vars(returned->outcome.term) ||
        atom_has_thread_local_resource(returned->outcome.term))
        return false;
    node->completes_callers = false;
    node->identity = (CettaCallReturnCursor){
        .outcome = returned->outcome, .environment = returned->environment};
    cetta_owned_execution_init(body, &node->identity, cetta_call_return_next, NULL);
    return true;
}

/* Prepare storage before entry. The tagged result decides whether ordinary
 * value execution can continue; an entered residual is never invoked again. */
static PreparedPureRunResult prepared_pure_native_invoke(
        CettaPreparedPureAnswerCursor *cursor, Atom *head, Atom *const *arguments,
        uint32_t arity, bool execute, CettaCallOutcome *outcome, bool *entered,
        Atom **inline_value) {
    CettaPreparedPureProgram *program = cursor->program;
    if (!prepared_pure_answer_cursor_reserve(cursor, arity))
        goto capacity;
    if (!cursor->native_spare) {
        cursor->native_spare = calloc(1u, sizeof(*cursor->native_spare));
        if (!cursor->native_spare)
            goto capacity;
        cursor->native_metadata_bytes += sizeof(*cursor->native_spare);
    }
    CettaInvocation invocation = {.head = binding_value_from_atom(head),
        .supplied = {.atoms = arguments, .count = arity}};
    *entered = execute && program->source_view.native_call &&
        program->source_view.native_call(program->source_view.native_context,
            program->space, cursor->arena, &invocation, outcome);
    cursor->native_started |= *entered;
    if (*entered && (!cetta_prepared_pure_program_is_current(program) ||
            (cursor->continuation_current &&
             !cursor->continuation_current(cursor->continuation_context)))) {
        (void)prepared_pure_answer_cursor_handoff(cursor, CETTA_PREPARED_PURE_HANDOFF_STALE);
        return PREPARED_PURE_RUN_HANDOFF;
    }
    if (*entered && outcome->kind == CETTA_CALL_VALUE && outcome->term &&
        outcome->result_form == CETTA_CALL_RESULT_COMPLETED_VALUE &&
        !atom_has_vars(outcome->term) && !atom_has_thread_local_resource(outcome->term)) {
        *inline_value = outcome->term;
        return PREPARED_PURE_RUN_ANSWER;
    }
    if (*entered && outcome->kind == CETTA_CALL_FAILURE)
        return PREPARED_PURE_RUN_CONTINUE;
    return PREPARED_PURE_RUN_HANDOFF;
capacity:
    (void)prepared_pure_answer_cursor_handoff(cursor, CETTA_PREPARED_PURE_HANDOFF_LIMIT);
    return PREPARED_PURE_RUN_HANDOFF;
}

static PreparedPureRunResult prepared_pure_native_retain(
        CettaPreparedPureAnswerCursor *cursor, Atom *head, Atom *const *arguments,
        uint32_t arity, CettaCallOutcome outcome, bool entered,
        const PreparedPureContinuation *continuation) {
    /* Synchronous native calls borrow their operands. Only a retained native
     * producer can outlive the parent's current heap segment. Own its full
     * frontier before installing the opaque producer, while detachment can
     * still traverse every ordinary frame. Keep an entered return on failure
     * so capacity exhaustion cannot retry its effects. */
    Atom *owned[64];
    CettaCallOutcome saved = outcome;
    const PreparedPureContinuation *saved_continuation = continuation;
    PreparedPureContinuationCopies records = {0};
    bool promoted = !cursor->borrowed_until_escape || cursor->owns_arena ||
        prepared_pure_answer_cursor_relocate(cursor, false, &saved_continuation);
    AtomDeepCopySession *copy = promoted
        ? atom_deep_copy_session_new(cursor->arena) : NULL;
    bool ok = arity <= 64u && copy &&
        prepared_pure_answer_cursor_detach_with_session(cursor, copy);
    for (uint32_t i = 0u; ok && i < arity; i++) {
        owned[i] = atom_deep_copy_session_copy(copy, arguments[i]);
        ok = owned[i] != NULL;
    }
    if (ok && outcome.term)
        ok = (saved.term = atom_deep_copy_session_copy(copy, outcome.term)) != NULL;
    if (ok && outcome.delayed)
        ok = (saved.delayed = atom_deep_copy_session_copy(copy, outcome.delayed)) != NULL;
    if (ok && saved_continuation)
        ok = prepared_pure_answer_cursor_copy_continuation(cursor, cursor->arena,
            copy, &records, saved_continuation, &saved_continuation);
    atom_deep_copy_session_free(copy);
    free(records.entries);
    PreparedPureNativeFrame *native = cursor->native_spare;
    cursor->native_spare = NULL;
    native->head = head;
    native->entered = entered;
    native->continuation = ok ? saved_continuation : continuation;
    native->returns = (CettaCallReturnCursor){.outcome = ok ? saved : outcome};
    if (entered)
        cetta_owned_execution_init(&native->producer, &native->returns,
                                  cetta_call_return_next, NULL);
    prepared_pure_answer_cursor_push_reserved(cursor, PREPARED_PURE_NATIVE_FRAME,
        ok ? owned : arguments, arity, NULL, NULL);
    cursor->frames[cursor->frame_len - 1u].native = native;
    if (!ok) {
        (void)prepared_pure_answer_cursor_handoff(cursor,
            CETTA_PREPARED_PURE_HANDOFF_LIMIT);
        return PREPARED_PURE_RUN_HANDOFF;
    }
    if (!entered) {
        (void)prepared_pure_answer_cursor_handoff(cursor,
            CETTA_PREPARED_PURE_HANDOFF_NATIVE_RETURN);
        return PREPARED_PURE_RUN_HANDOFF;
    }
    return PREPARED_PURE_RUN_CONTINUE;
}

/* Retain the actual return and remaining CPS body, never its source call. */
static PreparedPureRunResult prepared_pure_answer_cursor_native(
        CettaPreparedPureAnswerCursor *cursor, uint32_t equation, uint32_t pc,
        const PreparedPureStep *step,
        const PreparedPureContinuation *continuation, bool execute_native,
        Atom **inline_value) {
    CettaPreparedPureProgram *program = cursor->program;
    const PreparedPureBody *body = prepared_pure_body(program, equation);
    if (!body || step->node >= program->node_len)
        return PREPARED_PURE_RUN_HANDOFF;
    const PreparedPureNode node = program->nodes[step->node];
    uint32_t arity = node.child_count;
    if (!node.atom || node.atom->kind != ATOM_SYMBOL || arity > 64u ||
        node.first_child > program->child_len ||
        arity > program->child_len - node.first_child)
        return PREPARED_PURE_RUN_HANDOFF;
    Atom *arguments[64];
    size_t scratch = prepared_pure_answer_cursor_scratch(cursor);
    bool limit = false;
    for (uint32_t index = 0u; index < arity; index++) {
        if (!prepared_pure_project_answer_value(program, cursor->arena,
                program->slots, body->frame_slot_count,
                program->children[node.first_child + index], 0u,
                &cursor->limits, &scratch, &limit, &arguments[index]))
            return PREPARED_PURE_RUN_HANDOFF;
    }
    if (!cursor->owns_arena)
        cursor->scratch_remaining = scratch;
    CettaCallOutcome outcome = {0};
    bool entered = false;
    PreparedPureRunResult result = prepared_pure_native_invoke(cursor, node.atom,
        arguments, arity, execute_native, &outcome, &entered, inline_value);
    if (result != PREPARED_PURE_RUN_HANDOFF ||
        (cursor->handoff != CETTA_PREPARED_PURE_HANDOFF_NONE &&
         cursor->handoff != CETTA_PREPARED_PURE_HANDOFF_STALE))
        return result;
    bool captured = true;
    if (!step->auxiliary) {
        uint32_t count = body->frame_slot_count;
        PreparedPureContinuation *record = arena_alloc(cursor->arena, sizeof(*record));
        Atom **locals = arena_alloc(cursor->arena,
            sizeof(*locals) * (count ? count : 1u));
        captured = record && locals;
        if (captured) {
            memcpy(locals, program->slots, sizeof(*locals) * count);
            *record = (PreparedPureContinuation){.parent = continuation,
                .locals = locals, .body_index = equation, .step = pc + 1u,
                .slot = step->slot};
            continuation = record;
        }
    }
    result = prepared_pure_native_retain(cursor, node.atom, arguments, arity,
        outcome, entered, continuation);
    if (!captured) {
        (void)prepared_pure_answer_cursor_handoff(cursor, CETTA_PREPARED_PURE_HANDOFF_LIMIT);
        return PREPARED_PURE_RUN_HANDOFF;
    }
    return cursor->handoff == CETTA_PREPARED_PURE_HANDOFF_NONE
        ? result : PREPARED_PURE_RUN_HANDOFF;
}

/* A register node's operation on two operand values, decided unboxed where
 * prepared_pure_inline_register_scalar decides it on the same values:
 * structural equality of operands known to share a type, and small exact
 * integers without overflow.  False leaves it to the boxed arm. */
static PREPARED_PURE_ALWAYS_INLINE bool prepared_pure_ready_register_scalar(
    const CettaPreparedPureProgram *program, const PreparedPureNode *node,
    Atom *left_atom, Atom *right_atom, CettaPlainScalar *out) {
    bool left_integer = left_atom->kind == ATOM_GROUNDED &&
        left_atom->ground.gkind == GV_INT;
    bool right_integer = right_atom->kind == ATOM_GROUNDED &&
        right_atom->ground.gkind == GV_INT;
    if (node->instruction == CETTA_GSLT_REGISTER_INSTRUCTION_ATOM_EQUAL) {
        if (prepared_pure_equality_needs_observation(left_atom) ||
            prepared_pure_equality_needs_observation(right_atom))
            return false;
        PreparedPureScalar left = {.atom = left_atom};
        PreparedPureScalar right = {.atom = right_atom};
        if (node->result_kind != CETTA_GSLT_REGISTER_RESULT_BOOLEAN ||
            (!program->total_structural_equality &&
             !prepared_pure_scalar_operands_share_type(
                 program, node, &left, &right)))
            return false;
        *out = (CettaPlainScalar){
            .kind = CETTA_PLAIN_SCALAR_BOOL,
            .as.boolean = left_integer && right_integer
                ? left_atom->ground.ival == right_atom->ground.ival
                : atom_value_eq(left_atom, right_atom),
        };
        return true;
    }
    CettaGsltRegisterOperandDiscipline discipline;
    if (!left_integer || !right_integer ||
        !cetta_gslt_register_operand_discipline(
            node->instruction, &discipline) ||
        discipline != CETTA_GSLT_REGISTER_OPERANDS_EXACT_INTEGER_OPERANDS)
        return false;
    int64_t integer = 0;
    bool boolean = false;
    bool promote = false;
    CettaGsltRegisterResultKind kind = node->result_kind;
    if (!cetta_gslt_register_execute_small_binary(
            node->instruction, &integer, &boolean,
            left_atom->ground.ival, right_atom->ground.ival, &kind, &promote) ||
        promote || kind != node->result_kind)
        return false;
    if (kind == CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER) {
        *out = (CettaPlainScalar){
            .kind = CETTA_PLAIN_SCALAR_INT, .as.integer = integer};
        return true;
    }
    if (kind == CETTA_GSLT_REGISTER_RESULT_BOOLEAN) {
        *out = (CettaPlainScalar){
            .kind = CETTA_PLAIN_SCALAR_BOOL, .as.boolean = boolean};
        return true;
    }
    return false;
}

/* Keep general float dispatch off the small-integer register arm: that arm
 * remains a call-free inline operation, including in the branch executor. */
static PREPARED_PURE_NOINLINE bool prepared_pure_ready_float_scalar(
    const PreparedPureNode *node, Atom *left_atom, Atom *right_atom,
    CettaPlainScalar *out) {
    CettaGsltPreparedPureIntrinsicInstruction instruction;
    CettaPlainScalar arguments[2];
    if (!node->atom || node->atom->kind != ATOM_SYMBOL ||
        !cetta_prepared_pure_intrinsic_program(node->atom->sym_id, 2u, &instruction) ||
        instruction != CETTA_GSLT_PREPARED_PURE_INTRINSIC_GROUNDED_DISPATCH ||
        !grounded_plain_scalar_from_atom(left_atom, &arguments[0]) ||
        !grounded_plain_scalar_from_atom(right_atom, &arguments[1]) ||
        (arguments[0].kind != CETTA_PLAIN_SCALAR_FLOAT &&
         arguments[1].kind != CETTA_PLAIN_SCALAR_FLOAT))
        return false;
    return grounded_try_plain_scalar_operation(node->atom, arguments, 2u, out) &&
        (node->result_kind != CETTA_GSLT_REGISTER_RESULT_BOOLEAN ||
         out->kind == CETTA_PLAIN_SCALAR_BOOL);
}

/* The value of a register node on its operand values, as its frame
 * computes it; NULL where the frame declines. */
static PREPARED_PURE_ALWAYS_INLINE Atom *prepared_pure_ready_register(
    const CettaPreparedPureProgram *program, Arena *arena,
    const PreparedPureNode *node, Atom *const *values, uint32_t count) {
    CettaPlainScalar scalar;
    if (count == 2u &&
        (prepared_pure_ready_register_scalar(
            program, node, values[0], values[1], &scalar) ||
         prepared_pure_ready_float_scalar(node, values[0], values[1], &scalar))) {
        if (scalar.kind == CETTA_PLAIN_SCALAR_BOOL)
            return prepared_pure_boolean_atom(
                program, arena, scalar.as.boolean);
        if (scalar.kind == CETTA_PLAIN_SCALAR_INT)
            return atom_int(arena, scalar.as.integer);
        if (scalar.kind == CETTA_PLAIN_SCALAR_FLOAT)
            return atom_float(arena, scalar.as.floating);
    }
    Atom *value = prepared_pure_execute_register(
        program, arena, node->instruction, node->result_kind,
        node->undefined_type_operands, values, count);
    if (!value)
        value = prepared_pure_execute_register_intrinsic(
            program, arena, node->result_kind, node->atom, values, count);
    return value && !(value->kind == ATOM_EXPR && atom_is_error(value))
        ? value : NULL;
}

/* An EVAL or RETURN over the installed locals keeps logical exhaustion
 * separate from a worker that cannot realize the step. */
static bool prepared_pure_answer_step_return(
    CettaPreparedPureAnswerCursor *cursor, const PreparedPureStep *step,
    uint32_t slot_count, bool *limit_hit, CettaCallOutcome *outcome) {
    *outcome = (CettaCallOutcome){0};
    CettaPreparedPureProgram *program = cursor->program;
    Atom *value = NULL;
    size_t live_before = arena_accounted_live_bytes(cursor->arena);
    if (step->value_tree) {
        size_t scratch = prepared_pure_answer_cursor_scratch(cursor);
        if (!prepared_pure_project_answer_value(
                program, cursor->arena, program->slots, slot_count,
                step->node, 0u, &cursor->limits, &scratch, limit_hit,
                &value))
            return false;
        if (!cursor->owns_arena)
            cursor->scratch_remaining = scratch;
        *outcome = cetta_call_completed_value(value);
        return true;
    }
    /* Native-aware CPS splits computations at ready operand boundaries.
     * A scalar leaf uses the same bounded operation as the node machine;
     * entering a whole executor for each such leaf repeats its setup. */
    if (program->has_native_calls && prepared_pure_code_enabled() &&
        !prepared_pure_scalar_tree_reference_enabled() &&
        step->node < program->node_len &&
        program->nodes[step->node].kind == PREPARED_PURE_REGISTER) {
        const PreparedPureNode *node = &program->nodes[step->node];
        bool ready = node->child_count == 2u;
        Atom *operands[2];
        for (uint32_t index = 0u; ready && index < 2u; index++) {
            operands[index] = prepared_pure_immediate_value(program,
                &program->nodes[program->children[node->first_child + index]], 0u);
            ready = operands[index] != NULL;
        }
        if (ready)
            value = prepared_pure_ready_register(program, cursor->arena, node, operands, 2u);
        else {
            uint32_t budget = PREPARED_PURE_INLINE_NODES;
            value = prepared_pure_inline_value(program, cursor->arena,
                node, 0u, PREPARED_PURE_INLINE_DEPTH, &budget);
        }
        if (value) {
            if (!prepared_pure_answer_cursor_charge(cursor, live_before)) {
                *limit_hit = true;
                return false;
            }
            *outcome = cetta_call_completed_value(value);
            return true;
        }
    }
    /* A step's region runs as its register code when the program has
     * code, over the same slots, as the node executor would run it. */
    bool evaluated = program->code_ready && step->coded
        ? prepared_pure_run_code_from(
              program, cursor->arena, step->first_code,
              step->register_count, true, 0u, NULL, NULL, &value, outcome)
        : prepared_pure_program_execute_internal(
              program, cursor->arena, NULL, NULL, NULL, step->node,
              slot_count, true, 0u, NULL, NULL, &value, outcome, NULL);
    program->frame_len = 0u;
    program->value_len = 0u;
    program->slot_len = slot_count;
    if (!prepared_pure_answer_cursor_charge(cursor, live_before)) {
        *limit_hit = true;
        return false;
    }
    if (evaluated)
        *outcome = cetta_call_completed_value(value);
    return evaluated || outcome->kind == CETTA_CALL_FAILURE ||
        outcome->kind == CETTA_CALL_INTERRUPTED;
}

/* Enter the call of a CALL or TAIL step.  A CALL records where its answers
 * resume; a TAIL passes on the continuation it runs under.  The caller is
 * the frontier's last call; with no equation left it is replaced. */
static PreparedPureRunResult prepared_pure_answer_cursor_enter(
    CettaPreparedPureAnswerCursor *cursor, size_t frame_index,
    uint32_t ordinal, bool matched_before, ArenaMark step_mark,
    uint32_t equation_index, uint32_t pc, const PreparedPureStep *step,
    const PreparedPureContinuation *continuation) {
    CettaPreparedPureProgram *program = cursor->program;
    const PreparedPureBody *body = prepared_pure_body(program, equation_index);
    uint32_t callee = step->auxiliary;
    uint32_t arity = step->arity;
    if (!body || callee >= program->head_len || arity > 64u ||
        step->first_argument > program->step_argument_len ||
        arity > program->step_argument_len - step->first_argument) {
        prepared_pure_answer_cursor_restore_step(
            cursor, frame_index, ordinal, matched_before, step_mark);
        (void)prepared_pure_answer_cursor_handoff(
            cursor, CETTA_PREPARED_PURE_HANDOFF_UNSUPPORTED);
        return PREPARED_PURE_RUN_HANDOFF;
    }
    enum { PREPARED_PURE_INLINE_STEP_ARGUMENTS = 16u };
    Atom *inline_arguments[PREPARED_PURE_INLINE_STEP_ARGUMENTS];
    Atom **arguments = arity <= PREPARED_PURE_INLINE_STEP_ARGUMENTS
        ? inline_arguments
        : malloc(sizeof(*arguments) * arity);
    size_t scratch = prepared_pure_answer_cursor_scratch(cursor);
    bool limit_hit = false;
    bool projected = arguments != NULL;
    for (uint32_t index = 0u; projected && index < arity; index++) {
        projected = prepared_pure_project_answer_value(
                program, cursor->arena, program->slots,
                body->frame_slot_count,
                program->step_arguments[step->first_argument + index],
                0u, &cursor->limits, &scratch, &limit_hit,
                &arguments[index]) &&
            arguments[index] &&
            !atom_has_vars(arguments[index]) &&
            !atom_has_thread_local_resource(arguments[index]);
    }
    if (!cursor->owns_arena)
        cursor->scratch_remaining = scratch;
    const PreparedPureContinuation *callee_continuation = continuation;
    if (projected && step->kind == PREPARED_PURE_STEP_CALL) {
        size_t live_before = arena_accounted_live_bytes(cursor->arena);
        uint32_t slot_count = body->frame_slot_count;
        PreparedPureContinuation *record =
            arena_alloc(cursor->arena, sizeof(*record));
        Atom **locals = arena_alloc(
            cursor->arena, sizeof(*locals) * (slot_count ? slot_count : 1u));
        if (!record || !locals) {
            projected = false;
        } else {
            if (slot_count > 0u)
                memcpy(locals, program->slots, sizeof(*locals) * slot_count);
            *record = (PreparedPureContinuation){
                .parent = continuation,
                .locals = locals,
                .body_index = equation_index,
                .step = pc + 1u,
                .slot = step->slot,
            };
            callee_continuation = record;
            if (!prepared_pure_answer_cursor_charge(cursor, live_before)) {
                projected = false;
                limit_hit = true;
            }
        }
    }
    bool reserved = projected &&
        prepared_pure_answer_cursor_reserve(cursor, arity);
    if (!reserved) {
        if (arguments != inline_arguments)
            free(arguments);
        prepared_pure_answer_cursor_restore_step(
            cursor, frame_index, ordinal, matched_before, step_mark);
        (void)prepared_pure_answer_cursor_handoff(
            cursor, limit_hit || projected
                ? CETTA_PREPARED_PURE_HANDOFF_LIMIT
                : CETTA_PREPARED_PURE_HANDOFF_UNSUPPORTED);
        return PREPARED_PURE_RUN_HANDOFF;
    }
    ArenaMark child_mark = prepared_pure_answer_cursor_retire_call(
        cursor, frame_index, step_mark);
    prepared_pure_answer_cursor_push_reserved(
        cursor, callee, arguments, arity, &child_mark, callee_continuation);
    if (arguments != inline_arguments)
        free(arguments);
    if (step->kind == PREPARED_PURE_STEP_TAIL)
        cursor->tail_calls++;
    else
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PREPARED_PURE_ANSWER_PRODUCER_CONTINUATION_CALL);
    if (!prepared_pure_answer_cursor_see_head(cursor, callee)) {
        (void)prepared_pure_answer_cursor_handoff(
            cursor, CETTA_PREPARED_PURE_HANDOFF_UNSUPPORTED);
        return PREPARED_PURE_RUN_HANDOFF;
    }
    return PREPARED_PURE_RUN_CONTINUE;
}

/* Run a step program from a matched equation's first step, or deliver a
 * value to `continuation`, until an answer reaches the consumer, a call is
 * entered, or no answer remains on this path.  Each resumption starts from
 * the continuation's own copy of the caller's locals. */
static PreparedPureRunResult prepared_pure_answer_cursor_run(
    CettaPreparedPureAnswerCursor *cursor, size_t frame_index,
    uint32_t ordinal, bool matched_before, ArenaMark step_mark,
    uint32_t equation_index, const PreparedPureContinuation *continuation,
    Atom *delivered, Atom **answer_out) {
    CettaPreparedPureProgram *program = cursor->program;
    const PreparedPureBody *body = NULL;
    uint32_t equation = equation_index;
    uint32_t pc = 0u;
    Atom *value = delivered;
    ArenaMark value_mark = step_mark;
    bool limit_hit = false;
    if (!value) {
        body = prepared_pure_body(program, equation);
        if (!body || !prepared_pure_reserve(
                (void **)&program->slots, sizeof(*program->slots),
                &program->slot_cap, body->frame_slot_count))
            goto unsupported;
        if (body->local_count > 0u)
            memcpy(program->slots, program->match_values,
                   sizeof(*program->slots) * body->local_count);
        if (body->frame_slot_count > body->local_count)
            memset(&program->slots[body->local_count], 0,
                   sizeof(*program->slots) *
                       (body->frame_slot_count - body->local_count));
        program->slot_len = body->frame_slot_count;
        pc = body->first_step;
        if (program->has_native_calls && program->code_ready && body->register_entry &&
            prepared_pure_code_enabled() && cursor->remaining_transitions >= 64u) {
            PreparedPureAnswerFrame *entered = &cursor->frames[frame_index];
            if (entered->head_index != PREPARED_PURE_ROOT_FRAME) {
                const PreparedPureHead *head = &program->heads[entered->head_index];
                if (head->unique_match)
                    entered->next_equation = head->equation_count;
            }
            Atom *result = NULL;
            size_t nursery = cursor->limits.max_scratch_bytes / 2u;
            if (nursery > 1024u * 1024u)
                nursery = 1024u * 1024u;
            bool completed = prepared_pure_run_owned_code_from(program, cursor->arena,
                body->first_code, body->register_count, true, nursery,
                cursor->interrupt_poll, cursor->interrupt_context, &result,
                cursor, continuation);
            if (!completed) {
                if (!cursor->native_started &&
                    cursor->handoff == CETTA_PREPARED_PURE_HANDOFF_UNSUPPORTED)
                    goto unsupported;
                return cursor->handoff == CETTA_PREPARED_PURE_HANDOFF_NONE
                    ? PREPARED_PURE_RUN_CONTINUE : PREPARED_PURE_RUN_HANDOFF;
            }
            value = result;
        }
    }
    for (;;) {
        if (value) {
            if (!continuation) {
                cursor->answers++;
                cursor->undo_valid = false;
                if (cursor->owns_arena) {
                    cursor->answer_pending = true;
                    cursor->answer_mark = value_mark;
                }
                *answer_out = value;
                return PREPARED_PURE_RUN_ANSWER;
            }
            equation = continuation->body_index;
            if (!prepared_pure_body(program, equation))
                goto unsupported;
            body = prepared_pure_body(program, equation);
            if (continuation->slot >= body->frame_slot_count ||
                !prepared_pure_reserve(
                    (void **)&program->slots, sizeof(*program->slots),
                    &program->slot_cap, body->frame_slot_count))
                goto unsupported;
            memcpy(program->slots, continuation->locals,
                   sizeof(*program->slots) * body->frame_slot_count);
            program->slots[continuation->slot] = value;
            program->slot_len = body->frame_slot_count;
            pc = continuation->step;
            continuation = continuation->parent;
            value = NULL;
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_PREPARED_PURE_ANSWER_PRODUCER_RESUMPTION);
            continue;
        }
        if (pc < body->first_step ||
            pc - body->first_step >= body->step_count)
            goto unsupported;
        const PreparedPureStep *step = &program->steps[pc];
        switch (step->kind) {
        case PREPARED_PURE_STEP_EVAL:
        case PREPARED_PURE_STEP_RETURN: {
            ArenaMark mark = arena_mark(cursor->arena);
            CettaCallOutcome returned;
            bool handled = prepared_pure_answer_step_return(
                cursor, step, body->frame_slot_count, &limit_hit, &returned);
            if (handled && returned.kind == CETTA_CALL_FAILURE)
                return PREPARED_PURE_RUN_CONTINUE;
            if (handled && returned.kind == CETTA_CALL_INTERRUPTED) {
                (void)prepared_pure_answer_cursor_handoff(
                    cursor, CETTA_PREPARED_PURE_HANDOFF_INTERRUPT);
                return PREPARED_PURE_RUN_HANDOFF;
            }
            Atom *result = handled && returned.kind == CETTA_CALL_VALUE
                ? returned.term : NULL;
            if (!result && cursor->native_started && !limit_hit) {
                PreparedPureStep pending = *step;
                pending.auxiliary = step->kind == PREPARED_PURE_STEP_RETURN;
                PreparedPureRunResult retained = prepared_pure_answer_cursor_native(
                    cursor, equation, pc, &pending, continuation, false, &result);
                if (cursor->handoff != CETTA_PREPARED_PURE_HANDOFF_NONE)
                    return retained;
            }
            if (!result || atom_has_vars(result) ||
                atom_has_thread_local_resource(result))
                goto unsupported;
            if (step->kind == PREPARED_PURE_STEP_RETURN) {
                value = result;
                value_mark = mark;
                break;
            }
            if (step->slot >= body->frame_slot_count)
                goto unsupported;
            program->slots[step->slot] = result;
            pc++;
            break;
        }
        case PREPARED_PURE_STEP_NATIVE: {
            Atom *result = NULL;
            PreparedPureRunResult native = prepared_pure_answer_cursor_native(
                cursor, equation, pc, step, continuation, true, &result);
            if (native == PREPARED_PURE_RUN_HANDOFF) {
                if (cursor->handoff == CETTA_PREPARED_PURE_HANDOFF_NONE)
                    goto unsupported;
                return native;
            }
            if (!result)
                return PREPARED_PURE_RUN_CONTINUE;
            if (step->auxiliary) {
                value = result;
                value_mark = step_mark;
            } else {
                if (step->slot >= body->frame_slot_count)
                    goto unsupported;
                program->slots[step->slot] = result;
                pc++;
            }
            break;
        }
        case PREPARED_PURE_STEP_MATCH:
        case PREPARED_PURE_STEP_MATCH_ELSE: {
            if (step->slot >= body->frame_slot_count ||
                step->auxiliary >= program->bind_pattern_len ||
                !program->slots[step->slot])
                goto unsupported;
            PreparedPureMatchState matched =
                prepared_pure_match_bind_pattern(
                    program, &program->bind_patterns[step->auxiliary], 0u,
                    program->slots[step->slot]);
            if (matched == PREPARED_PURE_MATCH_MISMATCH) {
                if (step->kind == PREPARED_PURE_STEP_MATCH_ELSE) {
                    pc = step->target;
                    break;
                }
                return PREPARED_PURE_RUN_CONTINUE;
            }
            if (matched != PREPARED_PURE_MATCH_MATCHED)
                goto unsupported;
            pc++;
            break;
        }
        case PREPARED_PURE_STEP_BRANCH: {
            Atom *condition = step->slot < body->frame_slot_count
                ? program->slots[step->slot] : NULL;
            if (condition && prepared_pure_is_true(program, condition))
                pc++;
            else if (condition &&
                     (prepared_pure_is_false(program, condition) ||
                      prepared_pure_petta_else_value(condition)))
                pc = step->target;
            else
                goto unsupported;
            break;
        }
        case PREPARED_PURE_STEP_JUMP:
            pc = step->target;
            break;
        case PREPARED_PURE_STEP_FAIL:
            return PREPARED_PURE_RUN_CONTINUE;
        case PREPARED_PURE_STEP_CALL:
        case PREPARED_PURE_STEP_TAIL:
            return prepared_pure_answer_cursor_enter(
                cursor, frame_index, ordinal, matched_before, step_mark,
                equation, pc, step, continuation);
        default:
            goto unsupported;
        }
    }
unsupported:
    if (prepared_pure_debug_enabled())
        fprintf(stderr, "prepared-pure step program stops at step %u (%s)\n",
                pc, body && pc - body->first_step < body->step_count
                    ? (const char *[]){"eval", "call", "tail", "match",
                                       "branch", "jump", "return", "fail", "native", "match-else"}
                          [program->steps[pc].kind]
                    : "resumption");
    prepared_pure_answer_cursor_restore_step(
        cursor, frame_index, ordinal, matched_before, step_mark);
    (void)prepared_pure_answer_cursor_handoff(
        cursor, limit_hit ? CETTA_PREPARED_PURE_HANDOFF_LIMIT
                          : CETTA_PREPARED_PURE_HANDOFF_UNSUPPORTED);
    return PREPARED_PURE_RUN_HANDOFF;
}

static size_t prepared_pure_native_bytes(const PreparedPureNativeFrame *native) {
    size_t bytes = sizeof(*native);
    for (const PreparedPureNativeBind *node = native->bindings; node; node = node->previous)
        bytes += sizeof(*node);
    return bytes;
}

static void prepared_pure_answer_cursor_pop_native(CettaPreparedPureAnswerCursor *cursor) {
    PreparedPureAnswerFrame *frame = &cursor->frames[cursor->frame_len - 1u];
    if (cursor->native_spare) {
        cursor->native_metadata_bytes -= prepared_pure_native_bytes(frame->native);
        prepared_pure_native_release(frame->native);
    } else {
        cursor->native_metadata_bytes -= prepared_pure_native_bytes(frame->native) - sizeof(*frame->native);
        prepared_pure_native_clear(frame->native);
        cursor->native_spare = frame->native;
    }
    cursor->argument_len = frame->argument_base;
    cursor->frame_len--;
    /* The saved parent still owns the equation's arena position. Do not
     * reset here: sibling returns can share the native producer's inputs. */
}

bool cetta_prepared_pure_answer_cursor_native_started(
        const CettaPreparedPureAnswerCursor *cursor) {
    return cursor && cursor->native_started;
}

bool cetta_prepared_pure_program_has_native_calls(
        const CettaPreparedPureProgram *program) {
    return program && program->has_native_calls;
}

bool cetta_prepared_pure_answer_cursor_owns_native_frontier(
        const CettaPreparedPureAnswerCursor *cursor) {
    return cursor && cursor->native_frontier_owned;
}

/* A whole-frontier certificate requires every ordinary frame to be
 * exhausted. Parent transfer can retain untried frames through shared
 * caller roots; frame multiplicity does not duplicate caller suffixes. */
static bool prepared_pure_native_frontier(
        const CettaPreparedPureAnswerCursor *cursor, bool exhausted, size_t *unfinished_out) {
    if (!cursor || !cursor->program || !cursor->frame_len)
        return false;
    size_t unfinished = 0u;
    for (size_t index = 0u; index + 1u < cursor->frame_len; index++) {
        const PreparedPureAnswerFrame *frame = &cursor->frames[index];
        if (frame->head_index == PREPARED_PURE_ROOT_FRAME) {
            if (!cursor->program->body_entry || !frame->matched_equation ||
                frame->next_equation != 1u)
                return false;
        } else {
            if (frame->head_index >= cursor->program->head_len || !frame->matched_equation)
                return false;
            if (frame->next_equation < cursor->program->heads[frame->head_index].equation_count) {
                if (exhausted)
                    return false;
                unfinished++;
            }
        }
    }
    if (unfinished_out)
        *unfinished_out = unfinished;
    return true;
}

bool cetta_prepared_pure_answer_cursor_native_frontier_complete(const CettaPreparedPureAnswerCursor *cursor) {
    return cursor && cursor->frame_len &&
        cursor->frames[cursor->frame_len - 1u].head_index == PREPARED_PURE_NATIVE_FRAME &&
        cursor->frames[cursor->frame_len - 1u].native &&
        prepared_pure_native_frontier(cursor, true, NULL);
}

bool cetta_prepared_pure_program_has_branches(const CettaPreparedPureProgram *program) {
    for (size_t i = 0u; program && i < program->step_len; i++)
        if (program->steps[i].kind == PREPARED_PURE_STEP_BRANCH ||
            program->steps[i].kind == PREPARED_PURE_STEP_MATCH_ELSE)
            return true;
    return false;
}

bool cetta_prepared_pure_answer_cursor_refill_transitions(
        CettaPreparedPureAnswerCursor *cursor, uint64_t allowance,
        CettaEvalCompletion *completion) {
    if (!completion || *completion != CETTA_EVAL_COMPLETE || !allowance)
        return false;
    if (!cursor || (!cursor->native_started && !cursor->program->body_entry) ||
        !cursor->transition_boundary ||
        cursor->handoff != CETTA_PREPARED_PURE_HANDOFF_LIMIT ||
        allowance > UINT64_MAX - cursor->limits.max_transitions)
        return false;
    if (!cursor->native_frontier_owned &&
        (!cetta_prepared_pure_program_is_current(cursor->program) ||
         (cursor->continuation_current &&
          !cursor->continuation_current(cursor->continuation_context)))) {
        cursor->handoff = CETTA_PREPARED_PURE_HANDOFF_STALE;
        cetta_eval_completion_record(completion, CETTA_EVAL_INCOMPLETE_INVALIDATED);
        return false;
    }
    cursor->limits.max_transitions += allowance;
    cursor->remaining_transitions = allowance;
    cursor->transition_boundary = false;
    cursor->handoff = CETTA_PREPARED_PURE_HANDOFF_NONE;
    return true;
}

bool cetta_prepared_pure_answer_cursor_resume_transitions(
        CettaOwnedExecution *execution, uint64_t allowance) {
    if (!execution || !execution->terminal ||
        execution->stop != CETTA_OWNED_EXECUTION_HANDOFF ||
        execution->next != cetta_prepared_pure_answer_cursor_return ||
        !cetta_prepared_pure_answer_cursor_refill_transitions(
            execution->computation, allowance, &execution->completion))
        return false;
    execution->terminal = false;
    execution->stop = CETTA_OWNED_EXECUTION_PAUSED;
    return true;
}

static const CettaOwnedReturn *prepared_pure_native_pending(
        const PreparedPureNativeFrame *native) {
    if (native->producer.has_pending)
        return &native->producer.pending;
    return native->bindings ? cetta_owned_bind_pending(&native->bindings->bind) : NULL;
}

bool cetta_prepared_pure_answer_cursor_native_boundary(
        CettaPreparedPureAnswerCursor *cursor, CettaInvocation *invocation,
        bool *entered, CettaOwnedReturn *returned) {
    if (!cursor || !invocation || !entered || !returned ||
        (cursor->handoff != CETTA_PREPARED_PURE_HANDOFF_NATIVE_RETURN &&
         cursor->handoff != CETTA_PREPARED_PURE_HANDOFF_STALE) ||
        !cursor->frame_len)
        return false;
    if (!cursor->native_frontier_owned &&
        (!cetta_prepared_pure_program_is_current(cursor->program) ||
         (cursor->continuation_current &&
          !cursor->continuation_current(cursor->continuation_context)))) {
        cursor->handoff = CETTA_PREPARED_PURE_HANDOFF_STALE;
    }
    const PreparedPureAnswerFrame *frame = &cursor->frames[cursor->frame_len - 1u];
    if (frame->head_index != PREPARED_PURE_NATIVE_FRAME || !frame->native)
        return false;
    const PreparedPureNativeFrame *native = frame->native;
    *invocation = (CettaInvocation){.head = binding_value_from_atom(native->head),
        .supplied = {.atoms = &cursor->arguments[frame->argument_base], .count = frame->arity}};
    *entered = native->entered;
    *returned = (CettaOwnedReturn){0};
    if (!native->entered)
        return true;
    const CettaOwnedReturn *pending = prepared_pure_native_pending(native);
    if (pending) {
        *returned = *pending;
        return true;
    }
    if (native->producer.computation == &native->returns && native->returns.position == 0u) {
        *returned = (CettaOwnedReturn){.outcome = native->returns.outcome,
            .environment = native->returns.environment, .final = true};
        return true;
    }
    return false;
}

bool cetta_prepared_pure_answer_cursor_complete_native(
        CettaPreparedPureAnswerCursor *cursor, CettaOwnedExecution *replacement,
        bool completes_callers) {
    CettaInvocation call;
    bool entered;
    CettaOwnedReturn returned;
    if (!replacement || !replacement->computation || !replacement->next ||
        !cetta_prepared_pure_answer_cursor_native_boundary(cursor, &call, &entered, &returned))
        return false;
    if (cursor->handoff != CETTA_PREPARED_PURE_HANDOFF_NATIVE_RETURN)
        return false;
    PreparedPureNativeFrame *native = cursor->frames[cursor->frame_len - 1u].native;
    if (completes_callers && !entered)
        return false;
    if (!entered) {
        native->producer = *replacement;
        *replacement = (CettaOwnedExecution){0};
        native->entered = cursor->native_started = true;
    } else if (native->bindings && native->bindings->bind.awaiting_body &&
               native->producer.terminal) {
        if (!cetta_owned_bind_resume(&native->producer, &native->bindings->bind, replacement))
            return false;
        native->bindings->completes_callers = completes_callers;
    } else {
        PreparedPureNativeBind *node = calloc(1u, sizeof(*node));
        if (!node)
            return false;
        CettaOwnedExecution source = native->producer;
        if (!cetta_owned_bind_open(&native->producer, &node->bind, &source,
                                  prepared_pure_native_replacement, node)) {
            free(node);
            return false;
        }
        node->replacement = *replacement;
        *replacement = (CettaOwnedExecution){0};
        node->previous = native->bindings;
        node->completes_callers = completes_callers;
        native->bindings = node;
        cursor->native_metadata_bytes += sizeof(*node);
    }
    if (completes_callers && cetta_prepared_pure_answer_cursor_native_frontier_complete(cursor))
        cursor->native_frontier_owned = true;
    cursor->handoff = CETTA_PREPARED_PURE_HANDOFF_NONE;
    return true;
}

void cetta_prepared_pure_answer_cursor_defer_native_completion(
        CettaPreparedPureAnswerCursor *cursor) {
    if (cursor && !cursor->native_started && !cursor->answers)
        cursor->native_transfer_requested = true;
}

bool cetta_prepared_pure_answer_cursor_native_transfer_ready(
        CettaPreparedPureAnswerCursor *cursor) {
    CettaInvocation invocation;
    CettaOwnedReturn returned;
    bool entered = false;
    if (!cursor || !cursor->native_transfer_requested || !cursor->owns_arena ||
        !cetta_prepared_pure_answer_cursor_native_boundary(
            cursor, &invocation, &entered, &returned) || !entered)
        return false;
    const PreparedPureNativeFrame *native = cursor->frames[cursor->frame_len - 1u].native;
    bool stale = cursor->handoff == CETTA_PREPARED_PURE_HANDOFF_STALE;
    /* A stale cursor may lend its entered packet only when it has no saved
     * caller and no ordinary equation left. No compiled source work then
     * depends on the expired authority. */
    if (!stale && cursor->handoff != CETTA_PREPARED_PURE_HANDOFF_NATIVE_RETURN)
        return false;
    size_t unfinished = 0u;
    if (!prepared_pure_native_frontier(cursor, false, &unfinished) ||
        (stale && (unfinished || native->continuation) &&
            (!cursor->program->caller_plans_complete ||
             (cursor->continuation_current &&
              !cursor->continuation_current(cursor->continuation_context)))))
        return false;
    return !native->bindings && !native->body_active &&
        native->producer.computation == &native->returns &&
        native->producer.next == cetta_call_return_next &&
        cetta_call_outcome_shape_valid(returned.outcome) &&
        !atom_has_thread_local_resource(native->returns.outcome.term) &&
        !atom_has_thread_local_resource(native->returns.outcome.delayed);
}

bool cetta_prepared_pure_answer_cursor_take_native(
        CettaPreparedPureAnswerCursor *cursor, CettaOwnedExecution *destination) {
    if (!destination || destination->computation || destination->has_pending ||
        !cetta_prepared_pure_answer_cursor_native_transfer_ready(cursor))
        return false;
    PreparedPureNativeFrame *native = cursor->frames[cursor->frame_len - 1u].native;
    *destination = native->producer;
    native->transferred = true;
    native->producer = (CettaOwnedExecution){
        .terminal = true, .stop = CETTA_OWNED_EXECUTION_HANDOFF};
    cursor->native_frontier_owned = cetta_prepared_pure_answer_cursor_native_frontier_complete(cursor);
    return true;
}

bool cetta_prepared_pure_answer_cursor_resume_native_frontier(
        CettaPreparedPureAnswerCursor *cursor, CettaOwnedExecution *completed) {
    if (!cursor || !cursor->frame_len || !completed ||
        !completed->terminal || completed->has_pending ||
        completed->stop != CETTA_OWNED_EXECUTION_COMPLETE ||
        completed->completion != CETTA_EVAL_COMPLETE)
        return false;
    PreparedPureAnswerFrame *frame = &cursor->frames[cursor->frame_len - 1u];
    PreparedPureNativeFrame *native = frame->head_index == PREPARED_PURE_NATIVE_FRAME
        ? frame->native : NULL;
    if (!native || !native->transferred ||
        completed->computation != &native->returns ||
        completed->next != cetta_call_return_next)
        return false;
    if (!cetta_prepared_pure_answer_cursor_native_frontier_complete(cursor) &&
        (!cetta_prepared_pure_program_is_current(cursor->program) ||
        (cursor->continuation_current &&
         !cursor->continuation_current(cursor->continuation_context)))) {
        cursor->handoff = CETTA_PREPARED_PURE_HANDOFF_STALE;
        return false;
    }
    /* The parent consumed this producer and all of its caller bodies.
     * Untried frames are still the cursor's actual compiled alternatives.
     * Retire backing storage only after the moved execution is disarmed. */
    cetta_owned_execution_cancel(completed);
    prepared_pure_answer_cursor_pop_native(cursor);
    cursor->handoff = CETTA_PREPARED_PURE_HANDOFF_NONE;
    return true;
}

bool cetta_prepared_pure_answer_cursor_resume_native(
        CettaOwnedExecution *execution, CettaOwnedExecution *replacement) {
    if (!execution || execution == replacement || !execution->terminal ||
        execution->stop != CETTA_OWNED_EXECUTION_HANDOFF ||
        execution->completion != CETTA_EVAL_COMPLETE ||
        execution->next != cetta_prepared_pure_answer_cursor_return ||
        !cetta_prepared_pure_answer_cursor_complete_native(
            execution->computation, replacement, false))
        return false;
    execution->terminal = false;
    execution->stop = CETTA_OWNED_EXECUTION_PAUSED;
    return true;
}

CettaPreparedPureCursorStep cetta_prepared_pure_answer_cursor_next(
    CettaPreparedPureAnswerCursor *cursor, Atom **answer_out) {
    if (answer_out)
        *answer_out = NULL;
    if (!cursor || !answer_out)
        return CETTA_PREPARED_PURE_CURSOR_HANDOFF;
    if (cursor->handoff != CETTA_PREPARED_PURE_HANDOFF_NONE)
        return CETTA_PREPARED_PURE_CURSOR_HANDOFF;
    cursor->undo_valid = false;
    cursor->last_return = (CettaCallOutcome){0};
    if (cursor->raised_return) {
        for (size_t index = 0u; index < cursor->frame_len; index++)
            if (cursor->frames[index].head_index == PREPARED_PURE_NATIVE_FRAME)
                prepared_pure_native_release(cursor->frames[index].native);
        cursor->frame_len = cursor->argument_len = 0u;
        cursor->native_metadata_bytes = cursor->native_spare ? sizeof(*cursor->native_spare) : 0u;
        cursor->raised_return = false;
        return CETTA_PREPARED_PURE_CURSOR_EXHAUSTED;
    }
    if (cursor->answer_pending) {
        arena_reset(cursor->arena, cursor->answer_mark);
        cursor->answer_pending = false;
    }
    CettaPreparedPureProgram *program = cursor->program;
    if (!cursor->native_frontier_owned &&
        (!space_program_token_is_current(program->source_program) ||
         (cursor->continuation_current &&
          !cursor->continuation_current(cursor->continuation_context))))
        return prepared_pure_answer_cursor_handoff(
            cursor, CETTA_PREPARED_PURE_HANDOFF_STALE);

    while (cursor->frame_len > 0u) {
        if (cursor->interrupt_poll && --cursor->poll_countdown == 0u) {
            cursor->poll_countdown = cursor->poll_interval;
            if (cursor->interrupt_poll(cursor->interrupt_context))
                return prepared_pure_answer_cursor_handoff(
                    cursor, CETTA_PREPARED_PURE_HANDOFF_INTERRUPT);
        }
        if (cursor->remaining_transitions == 0u) {
            cursor->transition_boundary = true;
            return prepared_pure_answer_cursor_handoff(
                cursor, CETTA_PREPARED_PURE_HANDOFF_LIMIT);
        }
        prepared_pure_answer_cursor_collect(cursor);
        cursor->remaining_transitions--;
        size_t frame_index = cursor->frame_len - 1u;
        PreparedPureAnswerFrame *frame = &cursor->frames[frame_index];
        if (frame->head_index == PREPARED_PURE_ROOT_FRAME) {
            if (frame->next_equation) {
                ArenaMark mark = frame->mark;
                cursor->argument_len = frame->argument_base;
                cursor->frame_len--;
                if (cursor->owns_arena)
                    arena_reset(cursor->arena, mark);
                continue;
            }
            const PreparedPureBody *body = &program->body;
            if (!program->body_entry || frame->arity != program->entry_argument_count ||
                body->local_count < frame->arity ||
                !prepared_pure_reserve((void **)&program->match_values,
                    sizeof(*program->match_values), &program->match_cap,
                    body->local_count))
                return prepared_pure_answer_cursor_handoff(cursor,
                    CETTA_PREPARED_PURE_HANDOFF_UNSUPPORTED);
            if (body->local_count)
                memset(program->match_values, 0,
                    sizeof(*program->match_values) * body->local_count);
            if (frame->arity)
                memcpy(program->match_values, &cursor->arguments[frame->argument_base],
                    sizeof(*program->match_values) * frame->arity);
            frame->next_equation = 1u;
            frame->matched_equation = true;
            PreparedPureRunResult run = prepared_pure_answer_cursor_run(cursor,
                frame_index, 0u, false, arena_mark(cursor->arena),
                PREPARED_PURE_ROOT_BODY, frame->continuation, NULL, answer_out);
            if (run == PREPARED_PURE_RUN_CONTINUE)
                continue;
            return run == PREPARED_PURE_RUN_ANSWER
                ? CETTA_PREPARED_PURE_CURSOR_ANSWER : CETTA_PREPARED_PURE_CURSOR_HANDOFF;
        }
        if (frame->head_index == PREPARED_PURE_NATIVE_FRAME) {
            PreparedPureNativeFrame *native = frame->native;
            if (!native || !native->entered)
                return prepared_pure_answer_cursor_handoff(cursor,
                    CETTA_PREPARED_PURE_HANDOFF_NATIVE_RETURN);
            if (native->body_active) {
                /* The body has retired; keep the producer loan and its
                 * pre-existing roots, reclaiming only this value reading. */
                if (cursor->owns_arena)
                    arena_reset(cursor->arena, frame->alternative_mark);
                if (!cetta_owned_execution_accept(&native->producer))
                    return prepared_pure_answer_cursor_handoff(cursor,
                        CETTA_PREPARED_PURE_HANDOFF_UNSUPPORTED);
                native->body_active = false;
            }
            const CettaOwnedReturn *returned = NULL;
            CettaOwnedExecutionStep next = cetta_owned_execution_loan(&native->producer, &returned);
            if (next == CETTA_OWNED_EXECUTION_COMPLETE) {
                if (native->producer.completion != CETTA_EVAL_COMPLETE)
                    return prepared_pure_answer_cursor_handoff(cursor,
                        CETTA_PREPARED_PURE_HANDOFF_NATIVE_RETURN);
                prepared_pure_answer_cursor_pop_native(cursor);
                continue;
            }
            if (next != CETTA_OWNED_EXECUTION_RETURN) {
                if (next == CETTA_OWNED_EXECUTION_PAUSED)
                    continue;
                cetta_eval_completion_record(&cursor->completion, native->producer.completion);
                return prepared_pure_answer_cursor_handoff(cursor,
                    next == CETTA_OWNED_EXECUTION_INTERRUPTED &&
                            native->producer.completion == CETTA_EVAL_INCOMPLETE_CANCELLED
                        ? CETTA_PREPARED_PURE_HANDOFF_INTERRUPT
                        : CETTA_PREPARED_PURE_HANDOFF_NATIVE_RETURN);
            }
            CettaCallOutcome outcome = returned->outcome;
            if (!cursor->native_frontier_owned &&
                (!cetta_prepared_pure_program_is_current(program) ||
                 (cursor->continuation_current &&
                  !cursor->continuation_current(cursor->continuation_context))))
                return prepared_pure_answer_cursor_handoff(cursor,
                    CETTA_PREPARED_PURE_HANDOFF_STALE);
            if (!cetta_call_outcome_shape_valid(outcome))
                return prepared_pure_answer_cursor_handoff(cursor,
                    CETTA_PREPARED_PURE_HANDOFF_NATIVE_RETURN);
            if (outcome.kind == CETTA_CALL_RAISED) {
                cursor->last_return = outcome;
                cursor->raised_return = true;
                *answer_out = outcome.term;
                return CETTA_PREPARED_PURE_CURSOR_ANSWER;
            }
            /* This body has already consumed the retained caller chain in
             * its dialect machine. Lend its actual packet and world; applying
             * the prepared continuation again would replay that chain. */
            if (native->bindings && native->bindings->completes_callers &&
                (outcome.kind == CETTA_CALL_SUSPENDED ||
                 (outcome.kind == CETTA_CALL_VALUE &&
                  outcome.result_form == CETTA_CALL_RESULT_COMPLETED_VALUE))) {
                cursor->last_return = outcome;
                frame->alternative_mark = arena_mark(cursor->arena);
                native->body_active = true;
                *answer_out = outcome.term;
                return CETTA_PREPARED_PURE_CURSOR_ANSWER;
            }
            if (outcome.kind != CETTA_CALL_VALUE ||
                outcome.result_form != CETTA_CALL_RESULT_COMPLETED_VALUE)
                return prepared_pure_answer_cursor_handoff(cursor,
                    CETTA_PREPARED_PURE_HANDOFF_NATIVE_RETURN);
            /* A completed value is read in its actual logical world. Closed
             * continuation admission follows substitution, without executing
             * the value or changing the producer's retained packet. */
            ArenaMark mark = arena_mark(cursor->arena);
            size_t live_before = arena_accounted_live_bytes(cursor->arena);
            Atom *ready = bindings_apply_if_vars(
                returned->environment, cursor->arena, outcome.term);
            if (!ready || atom_has_vars(ready) || atom_has_thread_local_resource(ready)) {
                arena_reset(cursor->arena, mark);
                return prepared_pure_answer_cursor_handoff(cursor,
                    CETTA_PREPARED_PURE_HANDOFF_NATIVE_RETURN);
            }
            if (!prepared_pure_answer_cursor_charge(cursor, live_before)) {
                arena_reset(cursor->arena, mark);
                return prepared_pure_answer_cursor_handoff(cursor,
                    CETTA_PREPARED_PURE_HANDOFF_LIMIT);
            }
            frame->alternative_mark = mark;
            native->body_active = true;
            PreparedPureRunResult run = prepared_pure_answer_cursor_run(cursor,
                frame_index, 0u, true, mark, 0u, native->continuation,
                ready, answer_out);
            if (run == PREPARED_PURE_RUN_CONTINUE)
                continue;
            return run == PREPARED_PURE_RUN_ANSWER
                ? CETTA_PREPARED_PURE_CURSOR_ANSWER : CETTA_PREPARED_PURE_CURSOR_HANDOFF;
        }
        if (frame->head_index >= program->head_len ||
            frame->argument_base > cursor->argument_len ||
            frame->arity > cursor->argument_len - frame->argument_base)
            return prepared_pure_answer_cursor_handoff(
                cursor, CETTA_PREPARED_PURE_HANDOFF_UNSUPPORTED);
        const PreparedPureHead *head =
            &program->heads[frame->head_index];
        if (frame->next_equation >= head->equation_count) {
            /* A dialect that reduces an unmatched call to itself still
             * type-checks a call of a head with declared types; that call is
             * left to it.  In a dialect where an unmatched call fails, a call
             * of an arity no equation takes is an application of a different
             * kind, partial or over-applied, and is left to it too. */
            if (!frame->matched_equation &&
                (cursor->unmatched_call ==
                     CETTA_PREPARED_PURE_UNMATCHED_DECLINES ||
                 (cursor->unmatched_call ==
                      CETTA_PREPARED_PURE_UNMATCHED_REDUCES_TO_ITSELF &&
                  head->declares_type) ||
                 (cursor->unmatched_call ==
                      CETTA_PREPARED_PURE_UNMATCHED_FAILS &&
                  !prepared_pure_head_takes_arity(
                      program, head, frame->arity))))
                return prepared_pure_answer_cursor_handoff(
                    cursor, CETTA_PREPARED_PURE_HANDOFF_NO_MATCH);
            if (!frame->matched_equation &&
                cursor->unmatched_call ==
                    CETTA_PREPARED_PURE_UNMATCHED_REDUCES_TO_ITSELF) {
                /* The call's one answer is itself; afterwards the call is
                 * finished like any other. */
                frame->matched_equation = true;
                ArenaMark self_mark = cursor->owns_arena
                    ? arena_mark(cursor->arena) : (ArenaMark){0};
                size_t live_before = arena_accounted_live_bytes(cursor->arena);
                Atom *self = prepared_pure_answer_cursor_call_atom(
                    cursor, frame_index);
                if (!self || !prepared_pure_answer_cursor_charge(
                        cursor, live_before)) {
                    prepared_pure_answer_cursor_restore_step(
                        cursor, frame_index, frame->next_equation, false,
                        self_mark);
                    return prepared_pure_answer_cursor_handoff(
                        cursor, self ? CETTA_PREPARED_PURE_HANDOFF_LIMIT
                                     : CETTA_PREPARED_PURE_HANDOFF_UNSUPPORTED);
                }
                if (!frame->continuation) {
                    cursor->answers++;
                    cursor->undo_valid = false;
                    if (cursor->owns_arena) {
                        cursor->answer_pending = true;
                        cursor->answer_mark = self_mark;
                    }
                    *answer_out = self;
                    return CETTA_PREPARED_PURE_CURSOR_ANSWER;
                }
                PreparedPureRunResult run = prepared_pure_answer_cursor_run(
                    cursor, frame_index, frame->next_equation, false,
                    self_mark, 0u, frame->continuation, self, answer_out);
                if (run == PREPARED_PURE_RUN_CONTINUE)
                    continue;
                return run == PREPARED_PURE_RUN_ANSWER
                    ? CETTA_PREPARED_PURE_CURSOR_ANSWER
                    : CETTA_PREPARED_PURE_CURSOR_HANDOFF;
            }
            cursor->argument_len = frame->argument_base;
            cursor->frame_len--;
            if (cursor->owns_arena)
                arena_reset(cursor->arena, frame->mark);
            continue;
        }
        /* What this call's earlier equations built is dead. */
        if (cursor->owns_arena)
            arena_reset(cursor->arena, frame->alternative_mark);
        uint32_t ordinal = frame->next_equation;
        bool matched_before = frame->matched_equation;
        const PreparedPureEquation *equation =
            &program->equations[head->first_equation + ordinal];
        frame->next_equation++;
        if (equation->arity != frame->arity)
            continue;
        uint32_t body_index = head->first_equation + ordinal;
        bool shape_admission = !frame->continuation && cursor->answer_admission &&
            program->answer_shapes && program->answer_shapes[body_index];
        bool total_head = equation->variable_arguments && !equation->repeated_variables &&
            equation->scalar_guard == PREPARED_PURE_NO_SCALAR_GUARD;
        if (shape_admission && total_head &&
            !cursor->answer_admission(cursor->answer_admission_context,
                program->answer_shapes[body_index])) {
            frame->matched_equation = true;
            continue;
        }
        uint64_t ready_arguments = frame->arity == 64u
            ? UINT64_MAX
            : (UINT64_C(1) << frame->arity) - UINT64_C(1);
        uint32_t demanded_argument = 0u;
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PREPARED_PURE_DECISION_FULL_MATCH);
        PreparedPureMatchState matched = prepared_pure_match_equation(
            program, equation, &cursor->arguments[frame->argument_base],
            frame->arity, ready_arguments, &demanded_argument);
        if (matched == PREPARED_PURE_MATCH_MISMATCH)
            continue;
        if (matched != PREPARED_PURE_MATCH_MATCHED) {
            frame->next_equation = ordinal;
            return prepared_pure_answer_cursor_handoff(
                cursor, CETTA_PREPARED_PURE_HANDOFF_UNSUPPORTED);
        }
        frame->matched_equation = true;
        PreparedPureGuardState guard =
            prepared_pure_evaluate_scalar_guard(
                program, equation, program->match_values);
        if (guard == PREPARED_PURE_GUARD_REFUTED)
            continue;
        ArenaMark step_mark = cursor->owns_arena
            ? arena_mark(cursor->arena) : (ArenaMark){0};
        if (guard != PREPARED_PURE_GUARD_ACCEPTED) {
            prepared_pure_answer_cursor_restore_step(
                cursor, frame_index, ordinal, matched_before, step_mark);
            return prepared_pure_answer_cursor_handoff(
                cursor, CETTA_PREPARED_PURE_HANDOFF_UNSUPPORTED);
        }

        if (shape_admission && !total_head &&
            !cursor->answer_admission(cursor->answer_admission_context,
                program->answer_shapes[body_index]))
            continue;
        if (!equation->body.tail_only) {
            PreparedPureRunResult run = prepared_pure_answer_cursor_run(
                cursor, frame_index, ordinal, matched_before, step_mark,
                head->first_equation + ordinal, frame->continuation, NULL,
                answer_out);
            if (run == PREPARED_PURE_RUN_CONTINUE)
                continue;
            return run == PREPARED_PURE_RUN_ANSWER
                ? CETTA_PREPARED_PURE_CURSOR_ANSWER
                : CETTA_PREPARED_PURE_CURSOR_HANDOFF;
        }

        const PreparedPureNode *root = &program->nodes[equation->body.root];
        size_t scratch = prepared_pure_answer_cursor_scratch(cursor);
        bool limit_hit = false;
        if (root->kind != PREPARED_PURE_CALL) {
            Atom *answer = NULL;
            if (!prepared_pure_project_answer_value(
                    program, cursor->arena, program->match_values,
                    equation->body.local_count, equation->body.root, 0u,
                    &cursor->limits, &scratch, &limit_hit, &answer) ||
                !answer || atom_has_vars(answer) ||
                atom_has_thread_local_resource(answer)) {
                prepared_pure_answer_cursor_restore_step(
                    cursor, frame_index, ordinal, matched_before,
                    step_mark);
                return prepared_pure_answer_cursor_handoff(
                    cursor, limit_hit
                        ? CETTA_PREPARED_PURE_HANDOFF_LIMIT
                        : CETTA_PREPARED_PURE_HANDOFF_UNSUPPORTED);
            }
            if (!cursor->owns_arena)
                cursor->scratch_remaining = scratch;
            if (frame->continuation) {
                PreparedPureRunResult run = prepared_pure_answer_cursor_run(
                    cursor, frame_index, ordinal, matched_before, step_mark,
                    0u, frame->continuation, answer, answer_out);
                if (run == PREPARED_PURE_RUN_CONTINUE)
                    continue;
                return run == PREPARED_PURE_RUN_ANSWER
                    ? CETTA_PREPARED_PURE_CURSOR_ANSWER
                    : CETTA_PREPARED_PURE_CURSOR_HANDOFF;
            }
            cursor->answers++;
            cursor->undo_valid = true;
            cursor->undo_frame = frame_index;
            cursor->undo_next_equation = ordinal;
            cursor->undo_matched_equation = matched_before;
            if (cursor->owns_arena) {
                cursor->answer_pending = true;
                cursor->answer_mark = step_mark;
            }
            *answer_out = answer;
            return CETTA_PREPARED_PURE_CURSOR_ANSWER;
        }

        enum { PREPARED_PURE_INLINE_TAIL_ARGUMENTS = 16u };
        Atom *inline_tail_arguments[PREPARED_PURE_INLINE_TAIL_ARGUMENTS];
        uint32_t callee = root->auxiliary;
        uint32_t arity = root->child_count;
        Atom **tail_arguments = arity <= PREPARED_PURE_INLINE_TAIL_ARGUMENTS
            ? inline_tail_arguments
            : malloc(sizeof(*tail_arguments) * arity);
        bool projected = tail_arguments != NULL &&
            callee < program->head_len;
        for (uint32_t child = 0u; projected && child < arity; child++) {
            projected = prepared_pure_project_answer_value(
                    program, cursor->arena, program->match_values,
                    equation->body.local_count,
                    program->children[root->first_child + child],
                    0u, &cursor->limits, &scratch, &limit_hit,
                    &tail_arguments[child]) &&
                tail_arguments[child] &&
                !atom_has_vars(tail_arguments[child]) &&
                !atom_has_thread_local_resource(tail_arguments[child]);
        }
        bool reserved = projected &&
            prepared_pure_answer_cursor_reserve(cursor, arity);
        if (!reserved) {
            if (tail_arguments != inline_tail_arguments)
                free(tail_arguments);
            prepared_pure_answer_cursor_restore_step(
                cursor, frame_index, ordinal, matched_before, step_mark);
            return prepared_pure_answer_cursor_handoff(
                cursor, limit_hit || projected
                    ? CETTA_PREPARED_PURE_HANDOFF_LIMIT
                    : CETTA_PREPARED_PURE_HANDOFF_UNSUPPORTED);
        }
        /* Reservation can move the frame array. */
        frame = &cursor->frames[frame_index];
        ArenaMark child_mark = step_mark;
        const PreparedPureContinuation *inherited = frame->continuation;
        if (frame->next_equation >= head->equation_count) {
            /* Last call: the caller has no alternative left. */
            child_mark = frame->mark;
            cursor->argument_len = frame->argument_base;
            cursor->frame_len--;
        }
        prepared_pure_answer_cursor_push_reserved(
            cursor, callee, tail_arguments, arity, &child_mark, inherited);
        if (tail_arguments != inline_tail_arguments)
            free(tail_arguments);
        if (!cursor->owns_arena)
            cursor->scratch_remaining = scratch;
        cursor->tail_calls++;
        /* The call is on the frontier either way; a relation its consumer
         * does not admit is never stepped here. */
        if (!prepared_pure_answer_cursor_see_head(cursor, callee))
            return prepared_pure_answer_cursor_handoff(
                cursor, CETTA_PREPARED_PURE_HANDOFF_UNSUPPORTED);
    }
    return CETTA_PREPARED_PURE_CURSOR_EXHAUSTED;
}

bool cetta_prepared_pure_answer_cursor_frontier_is_complete(
    const CettaPreparedPureAnswerCursor *cursor) {
    if (!cursor || !cursor->program ||
        cursor->handoff != CETTA_PREPARED_PURE_HANDOFF_NONE ||
        !cetta_prepared_pure_program_is_current(cursor->program) ||
        (cursor->continuation_current &&
         !cursor->continuation_current(cursor->continuation_context)))
        return false;
    const CettaPreparedPureProgram *program = cursor->program;
    for (size_t index = 0u; index < cursor->frame_len; index++) {
        const PreparedPureAnswerFrame *frame = &cursor->frames[index];
        if (frame->head_index == PREPARED_PURE_NATIVE_FRAME)
            return false;
        if (frame->head_index == PREPARED_PURE_ROOT_FRAME) {
            if (!program->body_entry || frame->next_equation != 1u ||
                !frame->matched_equation)
                return false;
            continue;
        }
        if (frame->head_index >= program->head_len ||
            frame->argument_base > cursor->argument_len ||
            frame->arity > cursor->argument_len - frame->argument_base ||
            !frame->matched_equation)
            return false;
        const PreparedPureHead *head = &program->heads[frame->head_index];
        if (!head->compiled || head->first_equation > program->equation_len ||
            head->equation_count > program->equation_len - head->first_equation ||
            frame->next_equation < head->equation_count)
            return false;
    }
    return true;
}

CettaOwnedExecutionStep cetta_prepared_pure_answer_cursor_return(
    void *computation, CettaOwnedReturn *returned,
    CettaEvalCompletion *completion) {
    CettaPreparedPureAnswerCursor *cursor = computation;
    Atom *value = NULL;
    CettaPreparedPureCursorStep step =
        cetta_prepared_pure_answer_cursor_next(cursor, &value);
    if (step == CETTA_PREPARED_PURE_CURSOR_ANSWER) {
        returned->outcome = cursor->last_return.term
            ? cursor->last_return : cetta_call_completed_value(value);
        returned->environment = NULL;
        if (cursor->last_return.term && cursor->frame_len) {
            const PreparedPureAnswerFrame *frame = &cursor->frames[cursor->frame_len - 1u];
            if (frame->head_index == PREPARED_PURE_NATIVE_FRAME && frame->native) {
                const CettaOwnedReturn *pending = prepared_pure_native_pending(frame->native);
                if (pending)
                    returned->environment = pending->environment;
            }
        }
        returned->final =
            cetta_prepared_pure_answer_cursor_frontier_is_complete(cursor);
        if (returned->final)
            *completion = CETTA_EVAL_COMPLETE;
        return CETTA_OWNED_EXECUTION_RETURN;
    }
    if (step == CETTA_PREPARED_PURE_CURSOR_EXHAUSTED) {
        *completion = CETTA_EVAL_COMPLETE;
        return CETTA_OWNED_EXECUTION_COMPLETE;
    }
    cetta_eval_completion_record(completion, cursor ? cursor->completion
        : CETTA_EVAL_INCOMPLETE_HOST_FAILURE);
    switch (cetta_prepared_pure_answer_cursor_handoff_reason(cursor)) {
    case CETTA_PREPARED_PURE_HANDOFF_NATIVE_RETURN:
        return CETTA_OWNED_EXECUTION_HANDOFF;
    case CETTA_PREPARED_PURE_HANDOFF_INTERRUPT:
        *completion = CETTA_EVAL_INCOMPLETE_CANCELLED;
        return CETTA_OWNED_EXECUTION_INTERRUPTED;
    case CETTA_PREPARED_PURE_HANDOFF_STALE:
        *completion = CETTA_EVAL_INCOMPLETE_INVALIDATED;
        break;
    case CETTA_PREPARED_PURE_HANDOFF_LIMIT:
        if (cursor && (cursor->native_started || cursor->program->body_entry) &&
            cursor->transition_boundary)
            return CETTA_OWNED_EXECUTION_HANDOFF;
        *completion = CETTA_EVAL_INCOMPLETE_CAPACITY;
        break;
    default:
        *completion = CETTA_EVAL_INCOMPLETE_HOST_FAILURE;
        break;
    }
    return CETTA_OWNED_EXECUTION_HANDOFF;
}

static void prepared_pure_answer_owned_release(void *computation) {
    cetta_prepared_pure_answer_cursor_close(computation);
}

static bool prepared_pure_answer_final_is_current(void *computation) {
    return cetta_prepared_pure_answer_cursor_frontier_is_complete(computation);
}

void cetta_prepared_pure_answer_cursor_own_execution(
    CettaOwnedExecution *execution, CettaPreparedPureAnswerCursor *cursor) {
    cetta_owned_execution_init(execution, cursor,
        cetta_prepared_pure_answer_cursor_return, prepared_pure_answer_owned_release);
    execution->final_is_current = prepared_pure_answer_final_is_current;
}

static bool prepared_pure_answer_cursor_detach_with_session(
    CettaPreparedPureAnswerCursor *cursor, AtomDeepCopySession *session) {
    if (!cursor)
        return false;
    if (!cursor->owns_arena || cursor->detached)
        return true;
    for (size_t index = 0u; index < cursor->frame_len; index++)
        if (cursor->frames[index].head_index == PREPARED_PURE_NATIVE_FRAME)
            return false;
    /* Saved locals can borrow entry values even after the corresponding
     * call's arguments have left the frontier.  Copy them as roots too.
     * The undo list makes failure leave the cursor and its borrowed answer
     * untouched; marking visited records avoids rewalking shared tails. */
    typedef struct {
        PreparedPureContinuation *record;
        Atom **locals;
    } DetachedLocals;
    DetachedLocals *changed = NULL;
    size_t changed_len = 0u, changed_cap = 0u;
    ArenaMark before = arena_mark(cursor->arena);
    Atom **arguments = cursor->argument_len
        ? malloc(sizeof(*arguments) * cursor->argument_len) : NULL;
    bool ok = session != NULL && (!cursor->argument_len || arguments);
    for (size_t index = 0u; ok && index < cursor->argument_len; index++) {
        arguments[index] = atom_deep_copy_session_copy(
            session, cursor->arguments[index]);
        ok = arguments[index] != NULL;
    }
    for (size_t index = 0u; ok && index < cursor->frame_len; index++) {
        for (PreparedPureContinuation *record =
                 (PreparedPureContinuation *)cursor->frames[index].continuation;
             ok && record && !record->locals_detached;
             record = (PreparedPureContinuation *)record->parent) {
            if (!prepared_pure_body(cursor->program, record->body_index) ||
                !prepared_pure_reserve(
                    (void **)&changed, sizeof(*changed), &changed_cap,
                    changed_len + 1u)) {
                ok = false;
                break;
            }
            uint32_t count = prepared_pure_body(cursor->program, record->body_index)->frame_slot_count;
            Atom **locals = arena_alloc(
                cursor->arena, sizeof(*locals) * (count ? count : 1u));
            ok = locals != NULL && (!count || record->locals);
            for (uint32_t slot = 0u; ok && slot < count; slot++) {
                locals[slot] = record->locals[slot]
                    ? atom_deep_copy_session_copy(session, record->locals[slot])
                    : NULL;
                ok = !record->locals[slot] || locals[slot];
            }
            if (ok) {
                changed[changed_len++] = (DetachedLocals){record, record->locals};
                record->locals = locals;
                record->locals_detached = true;
            }
        }
    }
    if (ok) {
        if (cursor->argument_len)
            memcpy(cursor->arguments, arguments,
                   sizeof(*arguments) * cursor->argument_len);
        /* Copies sit above every call's position.  Neither a consumed
         * answer nor a completed alternative may reclaim below them. */
        cursor->answer_pending = false;
        cursor->undo_valid = false;
        ArenaMark mark = arena_mark(cursor->arena);
        for (size_t index = 0u; index < cursor->frame_len; index++) {
            cursor->frames[index].mark = mark;
            cursor->frames[index].alternative_mark = mark;
        }
        cursor->detached = true;
    } else {
        for (size_t index = 0u; index < changed_len; index++) {
            changed[index].record->locals = changed[index].locals;
            changed[index].record->locals_detached = false;
        }
        arena_reset(cursor->arena, before);
    }
    free(changed);
    free(arguments);
    return ok;
}

bool cetta_prepared_pure_answer_cursor_detach(
    CettaPreparedPureAnswerCursor *cursor) {
    if (!cursor)
        return false;
    if (!cursor->owns_arena && cursor->borrowed_until_escape)
        return prepared_pure_answer_cursor_relocate(cursor, false, NULL);
    if (!cursor->owns_arena || cursor->detached)
        return true;
    AtomDeepCopySession *copy = atom_deep_copy_session_new(cursor->arena);
    bool ok = copy && prepared_pure_answer_cursor_detach_with_session(cursor, copy);
    atom_deep_copy_session_free(copy);
    return ok;
}

bool cetta_prepared_pure_answer_cursor_uses_arena(
    const CettaPreparedPureAnswerCursor *cursor, const Arena *arena) {
    return cursor && arena && cursor->arena == arena;
}

Atom *cetta_prepared_pure_answer_cursor_copy_value(
    const CettaPreparedPureAnswerCursor *cursor, Arena *destination, Atom *value) {
    if (!cursor || !destination || !value)
        return NULL;
    return cursor->arena == destination ? value : atom_deep_copy(destination, value);
}

void cetta_prepared_pure_answer_cursor_suspend(
    CettaPreparedPureAnswerCursor *cursor) {
    if (!cursor)
        return;
    cursor->undo_valid = false;
    if (cursor->answer_pending) {
        arena_reset(cursor->arena, cursor->answer_mark);
        cursor->answer_pending = false;
    }
    if (cursor->owns_arena)
        arena_release_spare(cursor->arena, 0u);
    prepared_pure_shrink((void **)&cursor->frames, sizeof(*cursor->frames),
                         &cursor->frame_cap, cursor->frame_len);
    prepared_pure_shrink((void **)&cursor->arguments,
                         sizeof(*cursor->arguments), &cursor->argument_cap,
                         cursor->argument_len);
}

bool cetta_prepared_pure_answer_cursor_unyield(
    CettaPreparedPureAnswerCursor *cursor) {
    if (!cursor || !cursor->undo_valid ||
        cursor->undo_frame >= cursor->frame_len)
        return false;
    PreparedPureAnswerFrame *frame = &cursor->frames[cursor->undo_frame];
    frame->next_equation = cursor->undo_next_equation;
    frame->matched_equation = cursor->undo_matched_equation;
    cursor->undo_valid = false;
    if (cursor->answer_pending) {
        arena_reset(cursor->arena, cursor->answer_mark);
        cursor->answer_pending = false;
    }
    cursor->answers--;
    return true;
}

CettaPreparedPureHandoffReason cetta_prepared_pure_answer_cursor_handoff_reason(
    const CettaPreparedPureAnswerCursor *cursor) {
    return cursor ? cursor->handoff : CETTA_PREPARED_PURE_HANDOFF_NONE;
}

SpaceProgramToken cetta_prepared_pure_answer_cursor_program_token(
    const CettaPreparedPureAnswerCursor *cursor) {
    return cursor ? cursor->program->source_program
                  : (SpaceProgramToken){0};
}

size_t cetta_prepared_pure_answer_cursor_head_count(
    const CettaPreparedPureAnswerCursor *cursor) {
    return cursor ? cursor->seen_head_len : 0u;
}

SymbolId cetta_prepared_pure_answer_cursor_head(
    const CettaPreparedPureAnswerCursor *cursor, size_t index) {
    return cursor && index < cursor->seen_head_len
        ? cursor->seen_heads[index] : SYMBOL_ID_NONE;
}

size_t cetta_prepared_pure_answer_cursor_frame_count(
    const CettaPreparedPureAnswerCursor *cursor) {
    return cursor ? cursor->frame_len : 0u;
}

bool cetta_prepared_pure_answer_cursor_frame(
    const CettaPreparedPureAnswerCursor *cursor, size_t index,
    CettaPreparedPureAnswerFrame *frame_out) {
    if (!cursor || !frame_out || index >= cursor->frame_len)
        return false;
    const CettaPreparedPureProgram *program = cursor->program;
    const PreparedPureAnswerFrame *frame = &cursor->frames[index];
    if (frame->head_index >= program->head_len)
        return false;
    const PreparedPureHead *head = &program->heads[frame->head_index];
    *frame_out = (CettaPreparedPureAnswerFrame){
        .head = head->head,
        .arguments = &cursor->arguments[frame->argument_base],
        .arity = frame->arity,
        .next_ordinal = frame->next_equation,
        .equation_count = head->equation_count,
    };
    if (frame->next_equation < head->equation_count) {
        const PreparedPureEquation *equation =
            &program->equations[head->first_equation + frame->next_equation];
        frame_out->next_equation = equation->equation;
        frame_out->next_logical_index = equation->logical_index;
    }
    frame_out->resumes_continuation = frame->continuation != NULL;
    return true;
}

Atom *cetta_prepared_pure_answer_cursor_frame_equation(
    const CettaPreparedPureAnswerCursor *cursor, size_t index, uint32_t ordinal) {
    if (!cursor || index >= cursor->frame_len ||
        !cetta_prepared_pure_program_is_current(cursor->program))
        return NULL;
    const PreparedPureAnswerFrame *frame = &cursor->frames[index];
    if (frame->head_index >= cursor->program->head_len)
        return NULL;
    const PreparedPureHead *head = &cursor->program->heads[frame->head_index];
    if (ordinal < frame->next_equation || ordinal >= head->equation_count)
        return NULL;
    return cursor->program->equations[head->first_equation + ordinal].equation;
}

/* Decoding a resume template back into source syntax.  Slot values are
 * imported into the destination arena; a slot not yet computed at the call
 * (a pattern variable bound later, for example) becomes one fresh variable
 * shared by every read of that slot. */
typedef struct {
    const CettaPreparedPureProgram *program;
    Arena *arena;
    Atom *const *locals;
    Atom *input;
    Atom **fresh;
    uint32_t slot_count;
    CettaPreparedPureImportValueFn import_value;
    void *import_context;
    Bindings *captures;
    uint32_t delivered_slot;
} PreparedPureDecode;

static Atom *prepared_pure_decode_capture(PreparedPureDecode *decode, Atom *value) {
    Atom *port = value ? atom_var_with_id(decode->arena, "__captured", fresh_var_id()) : NULL;
    return port && bindings_add_var(decode->captures, port, value) ? port : NULL;
}

static Atom *prepared_pure_decode_slot(PreparedPureDecode *decode, uint32_t slot) {
    if (slot >= decode->slot_count)
        return NULL;
    /* The delivered slot stands for a computation, including inner callers.
     * Every other installed slot is an already-computed value. */
    if (slot == decode->delivered_slot)
        return decode->input;
    if (!decode->fresh[slot]) {
        /* A saved frame can hold inputs the resumed computation never
         * reads. Import only reached slots, once per slot in this caller. */
        Atom *source = decode->locals[slot];
        Atom *value = source ? decode->import_value(decode->import_context, source) : NULL;
        if (source && !value)
            return NULL;
        decode->fresh[slot] = value
            ? prepared_pure_decode_capture(decode, value)
            : atom_var_with_id(decode->arena, "__resume", fresh_var_id());
    }
    return decode->fresh[slot];
}

static Atom *prepared_pure_decode_pattern(
    PreparedPureDecode *decode, const PreparedPureBindPattern *pattern,
    Atom *atom, uint32_t depth) {
    if (!atom || depth > PREPARED_PURE_MAX_COMPILE_DEPTH)
        return NULL;
    if (atom->kind == ATOM_VAR) {
        const CettaPreparedPureProgram *program = decode->program;
        for (uint32_t index = 0u; index < pattern->var_count; index++) {
            const PreparedPureBindVar *var =
                &program->bind_vars[pattern->first_var + index];
            if (var_id_eq(var->var, atom->var_id))
                return prepared_pure_decode_slot(decode, var->slot);
        }
        return NULL;
    }
    if (atom->kind != ATOM_EXPR)
        return decode->import_value(decode->import_context, atom);
    CettaExprLen len = atom->expr.len;
    Atom **elements = malloc(sizeof(*elements) * (len ? len : 1u));
    if (!elements)
        return NULL;
    bool ok = true;
    for (CettaExprLen index = 0u; ok && index < len; index++) {
        elements[index] = prepared_pure_decode_pattern(
            decode, pattern, atom->expr.elems[index], depth + 1u);
        ok = elements[index] != NULL;
    }
    Atom *result = ok ? atom_expr(decode->arena, elements, len) : NULL;
    free(elements);
    return result;
}

static Atom *prepared_pure_decode_node(
    PreparedPureDecode *decode, uint32_t node_index, uint32_t depth) {
    const CettaPreparedPureProgram *program = decode->program;
    if (node_index >= program->node_len ||
        depth > PREPARED_PURE_MAX_COMPILE_DEPTH)
        return NULL;
    const PreparedPureNode *node = &program->nodes[node_index];
    if (node->first_child > program->child_len ||
        node->child_count > program->child_len - node->first_child)
        return NULL;
    const uint32_t *children = &program->children[node->first_child];
    Atom *head = NULL;
    uint32_t offset = 0u;
    switch (node->kind) {
    case PREPARED_PURE_LITERAL:
        return node->atom
            ? prepared_pure_decode_capture(decode,
                decode->import_value(decode->import_context, node->atom))
            : NULL;
    case PREPARED_PURE_SLOT:
        return prepared_pure_decode_slot(decode, node->auxiliary);
    case PREPARED_PURE_ZERO: {
        Atom *empty = atom_symbol(decode->arena, "empty");
        return empty ? atom_expr(decode->arena, &empty, 1u) : NULL;
    }
    case PREPARED_PURE_BUILD:
        break;
    case PREPARED_PURE_CALL:
        head = atom_symbol_id(decode->arena, node->head);
        offset = 1u;
        break;
    case PREPARED_PURE_REGISTER:
    case PREPARED_PURE_INTRINSIC:
        head = node->atom
            ? decode->import_value(decode->import_context, node->atom)
            : NULL;
        offset = 1u;
        break;
    case PREPARED_PURE_IF:
        if (node->child_count != 3u)
            return NULL;
        head = atom_symbol(decode->arena, "if");
        offset = 1u;
        break;
    case PREPARED_PURE_BIND: {
        if (node->child_count != 2u ||
            node->auxiliary >= program->bind_pattern_len)
            return NULL;
        const PreparedPureBindPattern *pattern =
            &program->bind_patterns[node->auxiliary];
        Atom *elements[4] = {
            atom_symbol(decode->arena, "let"),
            prepared_pure_decode_pattern(decode, pattern, pattern->pattern, 0u),
            prepared_pure_decode_node(decode, children[0], depth + 1u),
            prepared_pure_decode_node(decode, children[1], depth + 1u),
        };
        for (size_t index = 0u; index < 4u; index++)
            if (!elements[index])
                return NULL;
        return atom_expr(decode->arena, elements, 4u);
    }
    default:
        /* No source reading: entry arguments, delayed slots, observers. */
        return NULL;
    }
    if (offset > 0u && !head)
        return NULL;
    uint32_t len = node->child_count + offset;
    Atom **elements = malloc(sizeof(*elements) * (len ? len : 1u));
    if (!elements)
        return NULL;
    if (offset > 0u)
        elements[0] = head;
    bool ok = true;
    for (uint32_t index = 0u; ok && index < node->child_count; index++) {
        if (children[index] >= program->node_len) {
            ok = false;
            break;
        }
        const PreparedPureNode *child = &program->nodes[children[index]];
        /* A written constructor head is operation metadata. Preserve its
         * static dispatch; the fields remain captured values or computations. */
        bool static_head = node->kind == PREPARED_PURE_BUILD && !index &&
            node->auxiliary != PREPARED_PURE_BUILD_DATA_HEAD &&
            child->kind == PREPARED_PURE_LITERAL && child->atom &&
            child->atom->kind == ATOM_SYMBOL;
        elements[index + offset] = static_head
            ? decode->import_value(decode->import_context, child->atom)
            : prepared_pure_decode_node(decode, children[index], depth + 1u);
        ok = elements[index + offset] != NULL;
    }
    Atom *result = ok ? atom_expr(decode->arena, elements, len) : NULL;
    free(elements);
    return result;
}

/* Decode one caller independently of the callers above it. The input port
 * denotes a completed callee value; other installed slots remain data. */
static Atom *prepared_pure_decode_caller(
    const CettaPreparedPureProgram *program, const PreparedPureContinuation *record,
    Arena *arena, Atom *input, CettaPreparedPureImportValueFn import_value,
    void *import_context, Bindings *captures) {
    const PreparedPureBody *body = prepared_pure_body(program, record->body_index);
    if (!body || !record->step || !record->locals)
        return NULL;
    uint32_t call = record->step - 1u;
    if (call < body->first_step || call - body->first_step >= body->step_count)
        return NULL;
    const PreparedPureStep *step = &program->steps[call];
    uint32_t slots = body->frame_slot_count;
    if ((step->kind != PREPARED_PURE_STEP_CALL && step->kind != PREPARED_PURE_STEP_NATIVE) ||
        step->resume_template == PREPARED_PURE_NO_TEMPLATE || record->slot >= slots)
        return NULL;
    Atom **fresh = calloc(slots ? slots : 1u, sizeof(*fresh));
    PreparedPureDecode decode = {
        .program = program, .arena = arena, .locals = record->locals, .input = input,
        .fresh = fresh, .slot_count = slots,
        .import_value = import_value, .import_context = import_context,
        .captures = captures, .delivered_slot = record->slot,
    };
    Atom *term = fresh ? prepared_pure_decode_node(&decode, step->resume_template, 0u) : NULL;
    free(fresh);
    return term;
}

bool cetta_prepared_pure_answer_cursor_frame_resumption(
    const CettaPreparedPureAnswerCursor *cursor, size_t index, Arena *arena,
    Atom *hole, CettaPreparedPureImportValueFn import_value,
    void *import_context, Bindings *captures, Atom **term_out) {
    if (term_out)
        *term_out = NULL;
    if (!cursor || !arena || !hole || !import_value || !captures || !term_out ||
        index >= cursor->frame_len)
        return false;
    const CettaPreparedPureProgram *program = cursor->program;
    const PreparedPureAnswerFrame *frame = &cursor->frames[index];
    bool native = frame->head_index == PREPARED_PURE_NATIVE_FRAME;
    if (native && !frame->native)
        return false;
    Bindings captured;
    bindings_init(&captured);
    Atom *term = hole;
    for (const PreparedPureContinuation *record =
             native ? frame->native->continuation : frame->continuation;
         record; record = record->parent) {
        Atom *outer = prepared_pure_decode_caller(program, record, arena, term,
            import_value, import_context, &captured);
        if (!outer)
            goto refused;
        term = outer;
    }
    if (term == hole && !native)
        goto refused;
    bindings_replace(captures, &captured);
    *term_out = term;
    return true;
refused:
    bindings_free(&captured);
    return false;
}

void cetta_prepared_pure_caller_graph_free(CettaPreparedPureCallerGraph *graph) {
    if (!graph) return;
    for (size_t i = 0u; i < graph->length; i++)
        bindings_free(&graph->nodes[i].captures);
    free(graph->nodes);
    free(graph->roots);
    *graph = (CettaPreparedPureCallerGraph){0};
}

bool cetta_prepared_pure_answer_cursor_read_callers(
    const CettaPreparedPureAnswerCursor *cursor,
    const size_t *frames, size_t frame_count, Arena *arena,
    CettaPreparedPureImportValueFn import_value, void *import_context,
    CettaPreparedPureCallerGraph *destination) {
    if (!cursor || !arena || !import_value || !destination ||
        destination->nodes || destination->roots || destination->length ||
        destination->root_count || (frame_count && !frames) ||
        frame_count > SIZE_MAX / sizeof(size_t))
        return false;
    typedef struct {
        const PreparedPureContinuation *record;
        size_t root; /* Zero while this ancestor is still being visited. */
    } CallerVisit;
    CallerVisit *visits = NULL;
    size_t visit_len = 0u, visit_cap = 0u, node_cap = 0u;
    uint32_t *pending = NULL;
    size_t pending_len = 0u, pending_cap = 0u;
    CettaU64Index index;
    cetta_u64_index_init(&index);
    CettaPreparedPureCallerGraph graph = {.root_count = frame_count};
    graph.roots = frame_count ? calloc(frame_count, sizeof(*graph.roots)) : NULL;
    bool ok = !frame_count || graph.roots;
    for (size_t f = 0u; ok && f < frame_count; f++) {
        if (frames[f] >= cursor->frame_len) { ok = false; break; }
        const PreparedPureAnswerFrame *frame = &cursor->frames[frames[f]];
        bool native = frame->head_index == PREPARED_PURE_NATIVE_FRAME;
        if (native && !frame->native) { ok = false; break; }
        const PreparedPureContinuation *record = native
            ? frame->native->continuation : frame->continuation;
        size_t parent = 0u;
        while (ok && record) {
            uint32_t seen;
            if (cetta_u64_index_find(&index, (uint64_t)(uintptr_t)record, &seen)) {
                /* An incomplete visit denotes a cycle, not an empty caller. */
                if (seen >= visit_len || !visits[seen].root) { ok = false; break; }
                parent = visits[seen].root;
                break;
            }
            if (!arena_owns_ptr(cursor->arena, record) ||
                !record->locals || !arena_owns_ptr(cursor->arena, record->locals) ||
                visit_len >= UINT32_MAX ||
                !prepared_pure_reserve((void **)&visits, sizeof(*visits), &visit_cap, visit_len + 1u) ||
                !prepared_pure_reserve((void **)&pending, sizeof(*pending), &pending_cap, pending_len + 1u)) {
                ok = false;
                break;
            }
            seen = (uint32_t)visit_len++;
            visits[seen] = (CallerVisit){.record = record};
            if (cetta_u64_index_insert(&index, (uint64_t)(uintptr_t)record, seen) !=
                CETTA_GSLT_U32_INDEX_INSERTED_V1) { ok = false; break; }
            pending[pending_len++] = seen;
            record = record->parent;
        }
        /* Parents are completed first, so every published root points back
         * into the older prefix, independently of frame traversal order. */
        while (ok && pending_len) {
            uint32_t seen = pending[--pending_len];
            if (!prepared_pure_reserve((void **)&graph.nodes, sizeof(*graph.nodes),
                    &node_cap, graph.length + 1u)) { ok = false; break; }
            CettaPreparedPureCaller *node = &graph.nodes[graph.length++];
            *node = (CettaPreparedPureCaller){.parent = parent};
            bindings_init(&node->captures);
            node->input = atom_var_with_id(arena, "__caller_input", fresh_var_id());
            node->term = node->input ? prepared_pure_decode_caller(cursor->program,
                visits[seen].record, arena, node->input, import_value,
                import_context, &node->captures) : NULL;
            if (!node->term) { ok = false; break; }
            const PreparedPureContinuation *caller = visits[seen].record;
            uint32_t resume = cursor->program->steps[caller->step - 1u].resume_template;
            node->plan = cursor->program->node_sources && resume < cursor->program->projected_sources
                ? cursor->program->node_sources[resume] : NULL;
            if (cursor->program->source_view.project_source && !node->plan) {
                ok = false;
                break;
            }
            parent = graph.length;
            visits[seen].root = parent;
        }
        if (ok) graph.roots[f] = parent;
    }
    cetta_u64_index_free(&index);
    free(visits);
    free(pending);
    if (!ok) {
        cetta_prepared_pure_caller_graph_free(&graph);
        return false;
    }
    *destination = graph;
    return true;
}

uint64_t cetta_prepared_pure_answer_cursor_answer_count(
    const CettaPreparedPureAnswerCursor *cursor) {
    return cursor ? cursor->answers : 0u;
}

uint64_t cetta_prepared_pure_answer_cursor_tail_call_count(
    const CettaPreparedPureAnswerCursor *cursor) {
    return cursor ? cursor->tail_calls : 0u;
}

CettaPreparedPureCursorStorage cetta_prepared_pure_answer_cursor_storage(
    const CettaPreparedPureAnswerCursor *cursor) {
    if (!cursor)
        return (CettaPreparedPureCursorStorage){0};
    return (CettaPreparedPureCursorStorage){
        .live_bytes = cursor->owns_arena
            ? arena_accounted_live_bytes(cursor->arena) : 0u,
        .reserved_bytes = cursor->owns_arena
            ? cursor->arena->reserved_bytes : 0u,
        .metadata_bytes = sizeof(*cursor) + cursor->native_metadata_bytes +
            cursor->frame_cap * sizeof(*cursor->frames) +
            cursor->argument_cap * sizeof(*cursor->arguments) +
            cursor->program->head_len *
                (sizeof(*cursor->head_seen) + sizeof(*cursor->seen_heads)),
        .collections = cursor->collections,
        .copied_bytes = cursor->copied_bytes,
    };
}

void cetta_prepared_pure_answer_cursor_close(
    CettaPreparedPureAnswerCursor *cursor) {
    if (!cursor)
        return;
    for (size_t index = 0u; index < cursor->frame_len; index++)
        if (cursor->frames[index].head_index == PREPARED_PURE_NATIVE_FRAME)
            prepared_pure_native_release(cursor->frames[index].native);
    CettaPreparedPureProgram *program = cursor->program;
    if (program && program->body_entry && cursor->borrowed_until_escape &&
        !cursor->owns_arena && !program->body_cursor_spare &&
        cetta_prepared_pure_answer_cursor_storage(cursor).metadata_bytes <= 4096u) {
        CettaPreparedPureAnswerCursor empty = {
            .frames = cursor->frames, .frame_cap = cursor->frame_cap,
            .arguments = cursor->arguments, .argument_cap = cursor->argument_cap,
            .head_seen = cursor->head_seen, .seen_heads = cursor->seen_heads,
            .native_spare = cursor->native_spare,
            .native_metadata_bytes = cursor->native_spare ? sizeof(*cursor->native_spare) : 0u,
        };
        /* Only the lengths define live frames and roots. The next entry
         * overwrites each slot before publishing it as live. */
        *cursor = empty;
        program->body_cursor_spare = cursor;
        cetta_prepared_pure_program_free(program);
        return;
    }
    free(cursor->native_spare);
    free(cursor->frames);
    free(cursor->arguments);
    free(cursor->head_seen);
    free(cursor->seen_heads);
    if (cursor->owns_arena)
        arena_free(&cursor->owned_arena);
    cetta_prepared_pure_program_free(cursor->program);
    free(cursor);
}

CettaPreparedPureAnswersResult
cetta_prepared_pure_program_visit_closed_answers(
    CettaPreparedPureProgram *program, Arena *arena,
    const CettaPreparedPureAnswerLimits *limits,
    CettaPreparedPureUnmatchedCall unmatched_call,
    CettaPreparedPureAnswerVisitorFn visitor, void *visitor_context,
    CettaPreparedPureInterruptPollFn interrupt_poll,
    void *interrupt_context, uint64_t *answer_count_out,
    uint64_t *tail_call_count_out) {
    if (answer_count_out)
        *answer_count_out = 0u;
    if (tail_call_count_out)
        *tail_call_count_out = 0u;
    if (!limits || !visitor || cetta_prepared_pure_program_has_native_calls(program))
        return CETTA_PREPARED_PURE_ANSWERS_DECLINED;
    /* Every answer is visited before the next step, and nothing escapes a
     * run that stops short of completion. */
    CettaPreparedPureAnswerCursorOptions options = {
        .arena = arena,
        .limits = *limits,
        .unmatched_call = unmatched_call,
        .interrupt_poll = interrupt_poll,
        .interrupt_context = interrupt_context,
        .interrupt_poll_interval = 1u,
        .allow_continuations = true,
    };
    CettaPreparedPureAnswerCursor *cursor =
        cetta_prepared_pure_answer_cursor_open(program, &options);
    if (!cursor)
        return CETTA_PREPARED_PURE_ANSWERS_DECLINED;
    CettaPreparedPureAnswersResult result =
        CETTA_PREPARED_PURE_ANSWERS_DECLINED;
    for (;;) {
        Atom *answer = NULL;
        CettaPreparedPureCursorStep step =
            cetta_prepared_pure_answer_cursor_next(cursor, &answer);
        if (step == CETTA_PREPARED_PURE_CURSOR_ANSWER) {
            if (visitor(answer, visitor_context))
                continue;
            result = CETTA_PREPARED_PURE_ANSWERS_STOPPED;
        } else if (step == CETTA_PREPARED_PURE_CURSOR_EXHAUSTED) {
            result = CETTA_PREPARED_PURE_ANSWERS_COMPLETE;
        } else if (cursor->handoff == CETTA_PREPARED_PURE_HANDOFF_LIMIT) {
            result = CETTA_PREPARED_PURE_ANSWERS_LIMIT;
        } else if (cursor->handoff == CETTA_PREPARED_PURE_HANDOFF_INTERRUPT) {
            result = CETTA_PREPARED_PURE_ANSWERS_STOPPED;
        }
        break;
    }
    if (answer_count_out)
        *answer_count_out = cursor->answers;
    if (tail_call_count_out)
        *tail_call_count_out = cursor->tail_calls;
    cetta_prepared_pure_answer_cursor_close(cursor);
    return result;
}

static PREPARED_PURE_ALWAYS_INLINE Atom *prepared_pure_code_value(
    const CettaPreparedPureProgram *program, Atom *const *registers,
    uint32_t operand) {
    return (operand & PREPARED_PURE_CODE_LITERAL) != 0u
        ? program->code_literals[operand & ~PREPARED_PURE_CODE_LITERAL]
        : registers[operand];
}

enum {
    PREPARED_PURE_CODE_INLINE_OPERANDS = 16u,
    PREPARED_PURE_CODE_POLL_CALLS = 256u,
};

/* A safe point, every PREPARED_PURE_CODE_POLL_CALLS calls and returns: the
 * interrupt poll, and the collector when the nursery is full.  Between two
 * safe points one body runs straight-line code, so it allocates a bounded
 * amount.  False when interrupted. */
static PREPARED_PURE_NOINLINE bool prepared_pure_code_safe_point(
    CettaPreparedPureProgram *program, Arena *arena, bool resumed,
    size_t nursery_budget_bytes, ArenaMark gc_anchor,
    size_t *gc_trigger_bytes,
    CettaPreparedPureInterruptPollFn interrupt_poll,
    void *interrupt_context, const CettaPreparedPureAnswerCursor *cursor) {
    if (interrupt_poll && interrupt_poll(interrupt_context))
        return false;
    if (nursery_budget_bytes != 0u &&
        prepared_pure_saturating_add(
            arena->live_bytes, arena->external_bytes) >= *gc_trigger_bytes) {
        program->code_resumed = resumed;
        prepared_pure_gc_collect(program, arena, gc_anchor, cursor);
        program->code_resumed = false;
        *gc_trigger_bytes = prepared_pure_gc_trigger_bytes(
            program, arena, gc_anchor, nursery_budget_bytes);
    }
    return true;
}

static PREPARED_PURE_ALWAYS_INLINE bool prepared_pure_scalar_operand(
    const CettaPreparedPureProgram *program, const CettaPlainScalar *registers,
    uint32_t operand, uint32_t local_base, CettaPlainScalar *out) {
    if (operand & PREPARED_PURE_SCALAR_LITERAL) {
        uint32_t index = operand & ~PREPARED_PURE_SCALAR_LITERAL;
        return grounded_plain_scalar_from_atom(
            index < program->node_len ? program->nodes[index].atom : NULL, out);
    }
    if (operand & PREPARED_PURE_SCALAR_LOCAL) {
        size_t index = (size_t)local_base +
            (operand & ~PREPARED_PURE_SCALAR_LOCAL);
        return grounded_plain_scalar_from_atom(
            index < program->slot_len ? program->slots[index] : NULL, out);
    }
    *out = registers[operand];
    return true;
}

static PREPARED_PURE_ALWAYS_INLINE bool prepared_pure_scalar_numbers(
    const PreparedPureNode *node, const CettaPlainScalar *arguments,
    CettaPlainScalar *out) {
    if (arguments[0].kind == CETTA_PLAIN_SCALAR_BOOL ||
        arguments[1].kind == CETTA_PLAIN_SCALAR_BOOL)
        return false;
    if (arguments[0].kind == CETTA_PLAIN_SCALAR_INT &&
        arguments[1].kind == CETTA_PLAIN_SCALAR_INT) {
        bool boolean = false, promote = false;
        int64_t integer = 0;
        if (node->instruction == CETTA_GSLT_REGISTER_INSTRUCTION_ATOM_EQUAL) {
            *out = (CettaPlainScalar){.kind = CETTA_PLAIN_SCALAR_BOOL,
                .as.boolean = arguments[0].as.integer == arguments[1].as.integer};
            return true;
        }
        CettaGsltRegisterResultKind kind = node->result_kind;
        if (!cetta_gslt_register_execute_small_binary(node->instruction, &integer,
                &boolean, arguments[0].as.integer, arguments[1].as.integer,
                &kind, &promote) || promote || kind != node->result_kind)
            return false;
        if (kind == CETTA_GSLT_REGISTER_RESULT_BOOLEAN)
            *out = (CettaPlainScalar){
                .kind = CETTA_PLAIN_SCALAR_BOOL, .as.boolean = boolean};
        else if (kind == CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER)
            *out = (CettaPlainScalar){
                .kind = CETTA_PLAIN_SCALAR_INT, .as.integer = integer};
        else
            return false;
        return true;
    }
    return grounded_try_plain_scalar_operation(node->atom, arguments, 2u, out) &&
        (node->result_kind != CETTA_GSLT_REGISTER_RESULT_BOOLEAN ||
         out->kind == CETTA_PLAIN_SCALAR_BOOL);
}

/* A compact scalar region has no calls, effects, safepoints or published
 * intermediates. Refusal re-enters the broader boxed operation tree. The
 * literal float evidence selects this realization; exact-integer code keeps
 * its measured call-free register realization. */
static PREPARED_PURE_NOINLINE Atom *prepared_pure_scalar_region(
    CettaPreparedPureProgram *program, Arena *arena,
    const PreparedPureCode *code, uint32_t local_base) {
    CettaPlainScalar registers[PREPARED_PURE_INLINE_NODES];
    if (code->target > program->scalar_code_len ||
        (uint32_t)code->scalar_count * 3u >
            program->scalar_code_len - code->target ||
        code->scalar_count > PREPARED_PURE_INLINE_NODES)
        return NULL;
    for (uint32_t i = 0u; i < code->scalar_count; i++) {
        const uint32_t *instruction = &program->scalar_code[code->target + i * 3u];
        uint32_t index = instruction[0];
        if (index >= program->node_len)
            return NULL;
        const PreparedPureNode *node = &program->nodes[index];
        CettaPlainScalar arguments[2];
        if (!prepared_pure_scalar_operand(
                program, registers, instruction[1], local_base, &arguments[0]) ||
            !prepared_pure_scalar_operand(
                program, registers, instruction[2], local_base, &arguments[1]) ||
            !prepared_pure_scalar_numbers(node, arguments, &registers[i]))
            goto boxed;
    }
    if (code->scalar_count > 0u) {
        const CettaPlainScalar *value = &registers[code->scalar_count - 1u];
        if (value->kind == CETTA_PLAIN_SCALAR_BOOL)
            return prepared_pure_boolean_atom(program, arena, value->as.boolean);
        return grounded_plain_scalar_materialize(arena, value);
    }
boxed:;
    uint32_t budget = PREPARED_PURE_INLINE_NODES;
    return prepared_pure_inline_value(
        program, arena, &program->nodes[code->auxiliary], local_base,
        PREPARED_PURE_INLINE_DEPTH, &budget);
}

/* Read back only initialized live logical slots. Registers beyond the
 * step frame are constructor temporaries and cannot escape an instruction.
 * Source and target use the same slot coordinates by construction. A native
 * residual borrows these roots until the common retain boundary copies them;
 * a plain code frontier supplies its own multi-root copy episode. */
static bool prepared_pure_code_readback(CettaPreparedPureAnswerCursor *cursor,
        AtomDeepCopySession *copy, const PreparedPureContinuation *outer,
        const PreparedPureContinuation **continuation) {
    CettaPreparedPureProgram *program = cursor->program;
    *continuation = outer;
    for (size_t f = 0u; f < program->code_frame_len; f++) {
        const PreparedPureCodeFrame *frame = &program->code_frames[f];
        if (!frame->pc || frame->pc > program->code_len)
            return false;
        uint32_t index = frame->pc - 1u;
        uint32_t pc = program->code_steps[index];
        uint32_t equation = 0u;
        for (; equation < program->equation_len; equation++) {
            const PreparedPureBody *body = prepared_pure_body(program, equation);
            if (pc >= body->first_step && pc - body->first_step < body->step_count)
                break;
        }
        if (equation == program->equation_len) {
            const PreparedPureBody *root = &program->body;
            if (!program->body_entry || pc < root->first_step ||
                pc - root->first_step >= root->step_count)
                return false;
            equation = PREPARED_PURE_ROOT_BODY;
        }
        const PreparedPureStep *step = &program->steps[pc];
        const PreparedPureCode *code = &program->code[index];
        bool tail = step->kind == PREPARED_PURE_STEP_TAIL ||
            step->kind == PREPARED_PURE_STEP_RETURN ||
            (step->kind == PREPARED_PURE_STEP_NATIVE && step->auxiliary);
        if (tail) {
            if (f + 1u != program->code_frame_len)
                return false;
            continue;
        }
        const PreparedPureBody *body = prepared_pure_body(program, equation);
        uint32_t count = body->frame_slot_count;
        if (step->slot >= count || code->first_live > program->code_live_len ||
            code->live_count > program->code_live_len - code->first_live)
            return false;
        PreparedPureContinuation *record = arena_alloc(cursor->arena, sizeof(*record));
        Atom **locals = arena_alloc(cursor->arena, sizeof(*locals) * (count ? count : 1u));
        if (!record || !locals)
            return false;
        memset(locals, 0, sizeof(*locals) * count);
        for (uint32_t i = 0u; i < code->live_count; i++) {
            uint32_t slot = program->code_live[code->first_live + i];
            if (slot >= count || (size_t)frame->base + slot >= program->slot_len)
                return false;
            Atom *value = program->slots[(size_t)frame->base + slot];
            if (!value || !(locals[slot] = copy
                    ? atom_deep_copy_session_copy(copy, value) : value))
                return false;
        }
        *record = (PreparedPureContinuation){.parent = *continuation,
            .locals = locals, .body_index = equation, .step = pc + 1u,
            .slot = step->slot, .locals_detached = copy != NULL};
        *continuation = record;
    }
    return true;
}

/* Read the actual register residual before reusable program storage can be
 * entered again. The common native retain boundary owns its arguments,
 * tagged return and live continuation in one copy episode. */
static void prepared_pure_code_retain(CettaPreparedPureAnswerCursor *cursor,
        const PreparedPureContinuation *outer, Atom *head, Atom *const *arguments,
        uint32_t arity, CettaCallOutcome outcome, bool entered) {
    const PreparedPureContinuation *continuation = outer;
    bool ok = prepared_pure_code_readback(cursor, NULL, outer, &continuation);
    (void)prepared_pure_native_retain(cursor, head, arguments, arity,
        outcome, entered, continuation);
    if (!ok)
        (void)prepared_pure_answer_cursor_handoff(cursor, CETTA_PREPARED_PURE_HANDOFF_LIMIT);
}

/* Both budget exit and callee refusal transfer the ready call to the
 * same CPS frontier. The saved continuation includes every waiting caller;
 * only the actual callee is matched again, without replaying its producer. */
static bool prepared_pure_code_enter_frontier(CettaPreparedPureAnswerCursor *cursor,
        const PreparedPureContinuation *outer, const PreparedPureCode *code,
        Atom *const *arguments, uint32_t arity, bool paused) {
    AtomDeepCopySession *copy = atom_deep_copy_session_new(cursor->arena);
    const PreparedPureContinuation *saved = outer;
    Atom *owned[64];
    bool ok = copy && arity <= 64u && prepared_pure_answer_cursor_reserve(cursor, arity);
    for (uint32_t i = 0u; ok && i < arity; i++) {
        owned[i] = atom_deep_copy_session_copy(copy, arguments[i]);
        ok = owned[i] != NULL;
    }
    if (ok)
        ok = prepared_pure_code_readback(cursor, copy, outer, &saved);
    atom_deep_copy_session_free(copy);
    if (!ok) {
        (void)prepared_pure_answer_cursor_handoff(cursor, CETTA_PREPARED_PURE_HANDOFF_LIMIT);
        return false;
    }
    ArenaMark mark = prepared_pure_answer_cursor_retire_call(
        cursor, cursor->frame_len - 1u, arena_mark(cursor->arena));
    prepared_pure_answer_cursor_push_reserved(cursor, code->auxiliary,
        owned, arity, &mark, saved);
    if (!prepared_pure_answer_cursor_see_head(cursor, code->auxiliary)) {
        (void)prepared_pure_answer_cursor_handoff(cursor, CETTA_PREPARED_PURE_HANDOFF_UNSUPPORTED);
        return false;
    }
    if (paused) {
        cursor->transition_boundary = true;
        (void)prepared_pure_answer_cursor_handoff(cursor, CETTA_PREPARED_PURE_HANDOFF_LIMIT);
    }
    return true;
}

/* Run the program's register code from its root.  Activations are on
 * code_frames and their registers on the slots, each activation's above its
 * caller's.  A call reads its arguments before its callee's registers are
 * bound, so a tail call's callee takes over its caller's registers.  The
 * collector and the interrupt poll run at calls, where every activation's
 * registers are the roots. */
/* Run register code from the body at `first_code`, whose registers are the
 * first `register_count` slots: its locals, installed by the caller, then
 * registers the body writes before reading.  An answer producer's `step`
 * runs this way over its frame's slots; its value is an internal step, not
 * a published result. */
static PREPARED_PURE_ALWAYS_INLINE bool prepared_pure_run_code_worker(
    CettaPreparedPureProgram *program, Arena *arena, uint32_t first_code,
    uint32_t register_count, bool step, size_t nursery_budget_bytes,
    CettaPreparedPureInterruptPollFn interrupt_poll,
    void *interrupt_context, Atom **result_out,
    CettaPreparedPureAnswerCursor *cursor,
    const PreparedPureContinuation *continuation, CettaCallOutcome *outcome) {
    if (!prepared_pure_reserve(
            (void **)&program->slots, sizeof(*program->slots),
            &program->slot_cap, register_count ? register_count : 1u) ||
        !prepared_pure_reserve(
            (void **)&program->code_frames, sizeof(*program->code_frames),
            &program->code_frame_cap, 1u))
        return prepared_pure_runtime_decline(
            program, "cannot allocate root registers", NULL);
    program->slot_len = register_count;
    program->code_frames[0] = (PreparedPureCodeFrame){
        .pc = first_code,
    };
    program->code_frame_len = 1u;

    ArenaMark gc_anchor = arena_mark(arena);
    size_t gc_trigger_bytes = prepared_pure_gc_trigger_bytes(
        program, arena, gc_anchor, nursery_budget_bytes);
    /* The first call polls. */
    uint32_t poll_countdown = 1u;
    program->code_resumed = false;
    PreparedPureCodeFrame *frame = &program->code_frames[0];
    Atom **registers = program->slots;
    Atom *inline_operands[PREPARED_PURE_CODE_INLINE_OPERANDS];
    Atom *returned = NULL;
    Atom *result = NULL;
    const char *decline = NULL;
    const PreparedPureNode *decline_node = NULL;
    const PreparedPureCode *active_code = NULL;
    for (;;) {
        const PreparedPureCode *code = &program->code[frame->pc++];
        active_code = code;
        const uint32_t *operands =
            &program->code_operands[code->first_operand];
        switch (code->kind) {
        case PREPARED_PURE_CODE_MOVE:
            registers[code->destination] =
                prepared_pure_code_value(program, registers, operands[0]);
            continue;
        case PREPARED_PURE_CODE_BUILD_EXPRESSION: {
            uint32_t count = code->operand_count;
            const AtomExprHead *head = &program->code_heads[code->target];
            Atom *value;
            /* A constructor of up to three arguments takes its elements
             * in registers. */
            switch (count) {
            case 2u:
                value = atom_expr_headed2(
                    arena, head,
                    prepared_pure_code_value(program, registers, operands[0]),
                    prepared_pure_code_value(program, registers, operands[1]));
                goto built;
            case 3u:
                value = atom_expr_headed3(
                    arena, head,
                    prepared_pure_code_value(program, registers, operands[0]),
                    prepared_pure_code_value(program, registers, operands[1]),
                    prepared_pure_code_value(program, registers, operands[2]));
                goto built;
            case 4u:
                value = atom_expr_headed4(
                    arena, head,
                    prepared_pure_code_value(program, registers, operands[0]),
                    prepared_pure_code_value(program, registers, operands[1]),
                    prepared_pure_code_value(program, registers, operands[2]),
                    prepared_pure_code_value(program, registers, operands[3]));
                goto built;
            default:
                break;
            }
            Atom **values = inline_operands;
            if (count > PREPARED_PURE_CODE_INLINE_OPERANDS) {
                if (!prepared_pure_reserve(
                        (void **)&program->values, sizeof(*program->values),
                        &program->value_cap, count)) {
                    decline = "cannot stage operands";
                    goto declined;
                }
                values = program->values;
            }
            for (uint32_t i = 0u; i < count; i++)
                values[i] = prepared_pure_code_value(
                    program, registers, operands[i]);
            value = atom_expr_headed(arena, head, values, count);
        built:
            if (code->returns) {
                returned = value;
                goto return_value;
            }
            registers[code->destination] = value;
            continue;
        }
        case PREPARED_PURE_CODE_SCALAR_TREE: {
            const PreparedPureNode *node = &program->nodes[code->auxiliary];
            Atom *value = prepared_pure_scalar_region(program,arena,code,frame->base);
            if (!value) {
                decline_node = node;
                decline = "scalar tree declined";
                goto declined;
            }
            if (code->returns) { returned = value; goto return_value; }
            registers[code->destination] = value;
            continue;
        }
        case PREPARED_PURE_CODE_BUILD:
        case PREPARED_PURE_CODE_REGISTER:
        case PREPARED_PURE_CODE_INTRINSIC:
        case PREPARED_PURE_CODE_OBSERVE: {
            const PreparedPureNode *node = &program->nodes[code->auxiliary];
            decline_node = node;
            uint32_t count = code->operand_count;
            Atom **values = inline_operands;
            if (count > PREPARED_PURE_CODE_INLINE_OPERANDS) {
                if (!prepared_pure_reserve(
                        (void **)&program->values, sizeof(*program->values),
                        &program->value_cap, count)) {
                    decline = "cannot stage operands";
                    goto declined;
                }
                values = program->values;
            }
            for (uint32_t i = 0u; i < count; i++)
                values[i] = prepared_pure_code_value(
                    program, registers, operands[i]);
            Atom *value = NULL;
            if (code->kind == PREPARED_PURE_CODE_BUILD) {
                if (node->auxiliary == PREPARED_PURE_BUILD_DATA_HEAD &&
                    (count == 0u ||
                     !prepared_pure_data_head_value(values[0]))) {
                    decline = "a bound head is not data";
                    goto declined;
                }
                value = program->construct_value(arena, values, count);
            } else if (code->kind == PREPARED_PURE_CODE_REGISTER) {
                value = prepared_pure_ready_register(
                    program, arena, node, values, count);
            } else if (code->kind == PREPARED_PURE_CODE_INTRINSIC) {
                if (cursor && node->intrinsic_instruction ==
                        CETTA_GSLT_PREPARED_PURE_INTRINSIC_LIBRARY_VALUE_DISPATCH) {
                    CettaCallOutcome outcome = {0};
                    bool entered = false;
                    PreparedPureRunResult called = prepared_pure_native_invoke(
                        cursor, node->atom, values, count, true, &outcome, &entered, &value);
                    if (called == PREPARED_PURE_RUN_CONTINUE)
                        goto empty;
                    if (called == PREPARED_PURE_RUN_HANDOFF) {
                        if (cursor->handoff == CETTA_PREPARED_PURE_HANDOFF_NONE ||
                            cursor->handoff == CETTA_PREPARED_PURE_HANDOFF_STALE)
                            prepared_pure_code_retain(cursor, continuation, node->atom,
                                values, count, outcome, entered);
                        goto residual;
                    }
                } else {
                    value = prepared_pure_execute_intrinsic(
                        program, arena, node->intrinsic_instruction, node->atom,
                        values, count);
                }
                if (value && atom_is_error(value))
                    if (!cursor || node->intrinsic_instruction !=
                            CETTA_GSLT_PREPARED_PURE_INTRINSIC_LIBRARY_VALUE_DISPATCH)
                        value = NULL;
            } else if ((node->auxiliary &
                        PREPARED_PURE_OBSERVE_PATTERN_TEST) != 0u) {
                uint32_t pattern =
                    node->auxiliary & ~PREPARED_PURE_OBSERVE_PATTERN_TEST;
                PreparedPureMatchState tested =
                    count == 1u && pattern < program->bind_pattern_len
                    ? prepared_pure_match_bind_pattern_into(
                          program, &program->bind_patterns[pattern],
                          frame->base, values[0], false)
                    : PREPARED_PURE_MATCH_ERROR;
                if (tested != PREPARED_PURE_MATCH_MATCHED &&
                    tested != PREPARED_PURE_MATCH_MISMATCH) {
                    decline = "pattern test failed";
                    goto declined;
                }
                value = prepared_pure_boolean_atom(
                    program,
                    arena, tested == PREPARED_PURE_MATCH_MATCHED);
            } else if (node->auxiliary ==
                       PREPARED_PURE_OBSERVE_ATOM_EXPRESSION) {
                value = count == 1u && values[0]
                    ? prepared_pure_boolean_atom(
                          program,
                          arena, values[0]->kind == ATOM_EXPR)
                    : NULL;
            } else {
                if ((node->auxiliary !=
                         CETTA_PREPARED_PURE_OBSERVE_IS_EXPRESSION &&
                     node->auxiliary !=
                         CETTA_PREPARED_PURE_OBSERVE_IS_VARIABLE) ||
                    count > 1u) {
                    decline = "invalid value observation descriptor";
                    goto declined;
                }
                Atom *operand = count == 1u ? values[0] : node->atom;
                value = operand
                    ? prepared_pure_boolean_atom(
                          program,
                          arena,
                          node->auxiliary ==
                                  CETTA_PREPARED_PURE_OBSERVE_IS_VARIABLE
                              ? operand->kind == ATOM_VAR
                              : petta_semantics_is_closed_list(operand))
                    : NULL;
            }
            if (!value) {
                if (prepared_pure_debug_enabled() && node->atom) {
                    fputs("prepared-pure declined operands for ", stderr);
                    atom_print(node->atom, stderr);
                    fputs(":", stderr);
                    for (uint32_t i = 0u; i < count; i++) {
                        fputc(' ', stderr);
                        atom_print(values[i], stderr);
                    }
                    fputc('\n', stderr);
                }
                decline = "operation declined";
                goto declined;
            }
            if (code->returns) {
                returned = value;
                goto return_value;
            }
            registers[code->destination] = value;
            continue;
        }
        case PREPARED_PURE_CODE_BRANCH: {
            Atom *condition =
                prepared_pure_code_value(program, registers, operands[0]);
            if (prepared_pure_is_true(program, condition))
                continue;
            if (prepared_pure_is_false(program, condition) ||
                prepared_pure_petta_else_value(condition)) {
                frame->pc = code->target;
                continue;
            }
            decline = "non-boolean branch condition";
            goto declined;
        }
        case PREPARED_PURE_CODE_BRANCH_REGISTER: {
            const PreparedPureNode *node = &program->nodes[code->auxiliary];
            Atom *values[2] = {
                prepared_pure_code_value(program, registers, operands[0]),
                prepared_pure_code_value(program, registers, operands[1]),
            };
            CettaPlainScalar scalar;
            if (prepared_pure_ready_register_scalar(
                    program, node, values[0], values[1], &scalar) &&
                scalar.kind == CETTA_PLAIN_SCALAR_BOOL) {
                if (!scalar.as.boolean)
                    frame->pc = code->target;
                continue;
            }
            Atom *condition = prepared_pure_ready_register(
                program, arena, node, values, 2u);
            if (!condition) {
                decline = "operation declined";
                goto declined;
            }
            if (prepared_pure_is_true(program, condition))
                continue;
            if (prepared_pure_is_false(program, condition) ||
                prepared_pure_petta_else_value(condition)) {
                frame->pc = code->target;
                continue;
            }
            decline = "non-boolean branch condition";
            goto declined;
        }
        case PREPARED_PURE_CODE_JUMP:
            frame->pc = code->target;
            continue;
        case PREPARED_PURE_CODE_MATCH:
        case PREPARED_PURE_CODE_MATCH_ELSE: {
            Atom *value =
                prepared_pure_code_value(program, registers, operands[0]);
            PreparedPureMatchState matched =
                prepared_pure_match_bind_pattern(
                    program, &program->bind_patterns[code->auxiliary],
                    frame->base, value);
            if (matched == PREPARED_PURE_MATCH_MISMATCH &&
                code->kind == PREPARED_PURE_CODE_MATCH_ELSE) {
                frame->pc = code->target;
                continue;
            }
            if (matched != PREPARED_PURE_MATCH_MATCHED) {
                if (matched == PREPARED_PURE_MATCH_MISMATCH)
                    goto empty;
                decline = "let pattern matcher failed";
                goto declined;
            }
            continue;
        }
        case PREPARED_PURE_CODE_CALL:
        case PREPARED_PURE_CODE_TAIL_CALL: {
            if (--poll_countdown == 0u) {
                poll_countdown = PREPARED_PURE_CODE_POLL_CALLS;
                if (!prepared_pure_code_safe_point(
                        program, arena, false, nursery_budget_bytes,
                        gc_anchor, &gc_trigger_bytes, interrupt_poll,
                        interrupt_context, cursor)) {
                    if (outcome)
                        *outcome = cetta_call_interrupted();
                    if (cursor) {
                        (void)prepared_pure_answer_cursor_handoff(cursor,
                            CETTA_PREPARED_PURE_HANDOFF_INTERRUPT);
                        goto residual;
                    }
                    decline = "execution interrupted";
                    goto declined;
                }
            }
            uint32_t arity = code->operand_count;
            Atom *staged[PREPARED_PURE_CODE_INLINE_OPERANDS];
            Atom **arguments = staged;
            if (arity > PREPARED_PURE_CODE_INLINE_OPERANDS) {
                if (!prepared_pure_reserve(
                        (void **)&program->values, sizeof(*program->values),
                        &program->value_cap, arity)) {
                    decline = "cannot stage call arguments";
                    goto declined;
                }
                arguments = program->values;
            }
            for (uint32_t i = 0u; i < arity; i++)
                arguments[i] = prepared_pure_code_value(
                    program, registers, operands[i]);
            if (cursor) {
                if (!cursor->remaining_transitions) {
                    (void)prepared_pure_code_enter_frontier(cursor, continuation,
                        code, arguments, arity, true);
                    goto residual;
                }
                cursor->remaining_transitions--;
                if (!prepared_pure_answer_cursor_see_head(cursor, code->auxiliary)) {
                    (void)prepared_pure_answer_cursor_handoff(cursor,
                        CETTA_PREPARED_PURE_HANDOFF_UNSUPPORTED);
                    goto residual;
                }
            }
            if (!prepared_pure_typed_arguments_accepted(
                    program, code->auxiliary, arguments, arity)) {
                decline = "a typed argument is outside its parameter's class";
                goto call_refused;
            }
            bool tail = code->kind == PREPARED_PURE_CODE_TAIL_CALL;
            if (cursor && tail)
                cursor->tail_calls++;
            uint32_t base = tail ? frame->base : (uint32_t)program->slot_len;
            program->slot_len = base;
            const PreparedPureCodeEntry *entry =
                &program->code_entries[code->auxiliary];
            const PreparedPureEquation *equation = NULL;
            if (code->entry == PREPARED_PURE_CODE_ENTRY_SINGLE) {
                equation = &program->equations[entry->target];
            } else if (code->entry == PREPARED_PURE_CODE_ENTRY_KEYED) {
                uint64_t key = 0u;
                if (entry->positions != 1u) {
                    const PreparedPureArgumentKey *admitted = NULL;
                    PreparedPureKeyLookup lookup = prepared_pure_key_lookup(
                        program, &program->decisions[entry->decision],
                        arguments, &admitted);
                    if (lookup == PREPARED_PURE_KEY_NONE) {
                        decline = "user call was not uniquely matched";
                        goto call_refused;
                    }
                    if (lookup == PREPARED_PURE_KEY_ADMITTED)
                        equation = &program->equations[admitted->equation];
                } else if (prepared_pure_code_key(arguments[0], &key)) {
                    const uint64_t *keys = &program->code_keys[entry->target];
                    uint32_t i = 0u;
                    for (; i < entry->count; i++) {
                        if (keys[i] == key)
                            break;
                    }
                    if (i == entry->count) {
                        decline = "user call was not uniquely matched";
                        goto call_refused;
                    }
                    equation = &program->equations[
                        program->code_key_equations[entry->target + i]];
                }
            }
            if (equation) {
                if (!prepared_pure_reserve(
                        (void **)&program->slots, sizeof(*program->slots),
                        &program->slot_cap,
                        (size_t)base + equation->body.register_count)) {
                    decline = "cannot allocate call registers";
                    goto declined;
                }
                /* The pattern's variables; the body writes every other
                 * register before reading it. */
                Atom **locals = &program->slots[base];
                if (equation->code_binds) {
                    const PreparedPureCodeBind *bind =
                        &program->code_binds[equation->first_bind];
                    const PreparedPureCodeBind *fields_end =
                        bind + equation->bind_field_count;
                    const PreparedPureCodeBind *bind_end =
                        bind + equation->bind_count;
                    for (; bind < fields_end; bind++)
                        locals[bind->local] =
                            arguments[bind->source]->expr.elems[bind->field];
                    for (; bind < bind_end; bind++)
                        locals[bind->local] = arguments[bind->source];
                } else {
                    Atom *const *fields =
                        arity > 0u && arguments[0]->kind == ATOM_EXPR
                            ? arguments[0]->expr.elems : NULL;
                    /* The match ops after the decided ones, over the
                     * arguments and the fields the key's ops would have
                     * loaded (prepared_pure_direct_match). */
                    Atom *match_registers[PREPARED_PURE_MATCH_MAX_REGISTERS];
                    for (uint32_t i = 0u; i < arity; i++)
                        match_registers[i] = arguments[i];
                    if (equation->code_key_length > 0u)
                        memcpy(&match_registers[program->match_ops[
                                   equation->first_match_op].operand],
                               fields,
                               sizeof(*fields) * equation->code_key_length);
                    if (prepared_pure_run_match_ops(
                            program, equation, match_registers,
                            equation->code_key_ops, locals) !=
                        PREPARED_PURE_MATCH_MATCHED) {
                        decline = "user call was not uniquely matched";
                        goto call_refused;
                    }
                }
                if (equation->scalar_guard != PREPARED_PURE_NO_SCALAR_GUARD &&
                    prepared_pure_evaluate_scalar_guard(
                        program, equation, locals) !=
                        PREPARED_PURE_GUARD_ACCEPTED) {
                    decline = "user call was not uniquely matched";
                    goto call_refused;
                }
            } else {
                uint64_t ready = arity >= 64u
                    ? UINT64_MAX : (UINT64_C(1) << arity) - UINT64_C(1);
                PreparedPureDirectState direct =
                    prepared_pure_direct_entry_arguments(
                        program, code->auxiliary, arguments, ready, arity,
                        &equation);
                if (direct == PREPARED_PURE_DIRECT_FAILED) {
                    decline = "user call was not uniquely matched";
                    goto call_refused;
                }
                if (direct == PREPARED_PURE_DIRECT_NOT_APPLICABLE) {
                    PreparedPureSelection selection =
                        prepared_pure_select_equation(
                            program, code->auxiliary, arguments, arity,
                            ready);
                    equation = selection.equation;
                    if (selection.state != PREPARED_PURE_SELECT_SELECTED ||
                        !equation ||
                        !prepared_pure_reserve(
                            (void **)&program->slots,
                            sizeof(*program->slots), &program->slot_cap,
                            (size_t)base + equation->body.local_count)) {
                        decline = "user call was not uniquely matched";
                        goto call_refused;
                    }
                    if (equation->body.local_count > 0u)
                        memcpy(&program->slots[base],
                               program->selected_values,
                               sizeof(*program->slots) *
                                   equation->body.local_count);
                }
                if (!prepared_pure_reserve(
                        (void **)&program->slots, sizeof(*program->slots),
                        &program->slot_cap,
                        (size_t)base + equation->body.register_count)) {
                    decline = "cannot allocate call registers";
                    goto declined;
                }
            }
            if (!tail &&
                !prepared_pure_reserve(
                    (void **)&program->code_frames,
                    sizeof(*program->code_frames),
                    &program->code_frame_cap,
                    program->code_frame_len + 1u)) {
                decline = "cannot allocate call registers";
                goto declined;
            }
            program->slot_len = (size_t)base + equation->body.register_count;
            /* A tail call's callee re-enters in place of its caller, as
             * the node executor's tail reentry does. */
            if (!tail)
                program->code_frame_len++;
            else
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_PREPARED_PURE_CALL_TAIL_REENTRY);
            frame = &program->code_frames[program->code_frame_len - 1u];
            *frame = (PreparedPureCodeFrame){
                .pc = equation->body.first_code,
                .base = base,
            };
            registers = &program->slots[base];
            continue;
        call_refused:
            if (cursor) {
                (void)prepared_pure_code_enter_frontier(cursor, continuation,
                    code, arguments, arity, false);
                goto residual;
            }
            goto declined;
        }
        case PREPARED_PURE_CODE_RETURN:
            returned =
                prepared_pure_code_value(program, registers, operands[0]);
            goto return_value;
        case PREPARED_PURE_CODE_DECLINE:
            if (cursor)
                goto empty;
            decline = "the program answers nothing here";
            goto declined;
        case PREPARED_PURE_CODE_FAIL:
            goto empty;
        default:
            decline = "unknown code instruction";
            goto declined;
        }
    return_value:
        /* The body answers `returned`: into its caller's destination, or
         * as the program's value. */
        program->slot_len = frame->base;
        if (--program->code_frame_len == 0u) {
            result = returned;
            goto finished;
        }
        frame = &program->code_frames[program->code_frame_len - 1u];
        registers = &program->slots[frame->base];
        registers[program->code[frame->pc - 1u].destination] = returned;
        if (--poll_countdown == 0u) {
            poll_countdown = PREPARED_PURE_CODE_POLL_CALLS;
            if (!prepared_pure_code_safe_point(
                    program, arena, true, nursery_budget_bytes,
                    gc_anchor, &gc_trigger_bytes, interrupt_poll,
                    interrupt_context, cursor)) {
                if (outcome)
                    *outcome = cetta_call_interrupted();
                if (cursor) {
                    (void)prepared_pure_answer_cursor_handoff(cursor,
                        CETTA_PREPARED_PURE_HANDOFF_INTERRUPT);
                    goto residual;
                }
                decline = "execution interrupted";
                goto declined;
            }
        }
    }
finished:
    program->code_frame_len = 0u;
    if (!result ||
        !prepared_pure_memo_complete_deferred(program, result))
        return prepared_pure_runtime_decline(
            program, "code did not produce one value", NULL);
    if (!step && prepared_pure_result_has_escaping_suspension(program, result))
        return prepared_pure_runtime_decline(
            program, "call-by-need result requires a virtual suspension",
            NULL);
    bool loan_inputs = program->memo_len == 0u;
    prepared_pure_memo_clear(program);
    /* Without a collection, values live in the cursor, its borrowed entry,
     * or its retained immutable program. Only collection survivors need to
     * cross out of the program's reusable invocation storage here. */
    if (cursor && program->gc_survivor_ready) {
        AtomDeepCopySession *copy = atom_deep_copy_session_new(arena);
        uint32_t loan_arenas[64];
        if (copy && loan_inputs) {
            size_t loan_count = prepared_pure_gc_input_arenas(cursor, arena,
                &program->gc_survivor, loan_arenas,
                sizeof(loan_arenas) / sizeof(*loan_arenas));
            (void)atom_deep_copy_session_loan_arenas(copy, loan_arenas, loan_count);
        }
        Atom *owned = copy ? atom_deep_copy_session_copy(copy, result) : NULL;
        atom_deep_copy_session_free(copy);
        if (!owned) {
            (void)prepared_pure_answer_cursor_handoff(cursor, CETTA_PREPARED_PURE_HANDOFF_LIMIT);
            return false;
        }
        result = owned;
    }
    *result_out = result;
    return true;
declined:
    if (cursor && cursor->native_started && active_code && decline_node &&
        decline_node->atom && decline_node->atom->kind == ATOM_SYMBOL &&
        active_code->operand_count <= 64u &&
        (active_code->kind == PREPARED_PURE_CODE_REGISTER ||
         active_code->kind == PREPARED_PURE_CODE_INTRINSIC ||
         active_code->kind == PREPARED_PURE_CODE_OBSERVE)) {
        Atom *arguments[64];
        const uint32_t *operands = &program->code_operands[active_code->first_operand];
        for (uint32_t i = 0u; i < active_code->operand_count; i++)
            arguments[i] = prepared_pure_code_value(program, registers, operands[i]);
        CettaCallOutcome outcome = {0};
        bool entered = false;
        Atom *unused = NULL;
        (void)prepared_pure_native_invoke(cursor, decline_node->atom,
            arguments, active_code->operand_count, false, &outcome, &entered, &unused);
        if (cursor->handoff == CETTA_PREPARED_PURE_HANDOFF_NONE)
            prepared_pure_code_retain(cursor, continuation, decline_node->atom,
                arguments, active_code->operand_count, outcome, false);
        goto residual;
    }
    if (cursor)
        (void)prepared_pure_answer_cursor_handoff(cursor,
            CETTA_PREPARED_PURE_HANDOFF_UNSUPPORTED);
    program->code_frame_len = 0u;
    return prepared_pure_runtime_decline(program, decline, decline_node);
empty:
    if (outcome)
        *outcome = cetta_call_failure();
residual:
    program->code_frame_len = 0u;
    return false;
}

/* Compile the same worker for the two admitted ownership contracts. The
 * ordinary realization carries no native cursor checks in its hot loop. */
static bool PREPARED_PURE_HOT prepared_pure_run_code_from(
    CettaPreparedPureProgram *program, Arena *arena, uint32_t first_code,
    uint32_t register_count, bool step, size_t nursery_budget_bytes,
    CettaPreparedPureInterruptPollFn interrupt_poll,
    void *interrupt_context, Atom **result_out, CettaCallOutcome *outcome) {
    return prepared_pure_run_code_worker(program, arena, first_code, register_count,
        step, nursery_budget_bytes, interrupt_poll, interrupt_context, result_out,
        NULL, NULL, outcome);
}

static bool PREPARED_PURE_HOT PREPARED_PURE_NOINLINE prepared_pure_run_owned_code_from(
    CettaPreparedPureProgram *program, Arena *arena, uint32_t first_code,
    uint32_t register_count, bool step, size_t nursery_budget_bytes,
    CettaPreparedPureInterruptPollFn interrupt_poll,
    void *interrupt_context, Atom **result_out,
    CettaPreparedPureAnswerCursor *cursor,
    const PreparedPureContinuation *continuation) {
    if (!cursor)
        return false;
    return prepared_pure_run_code_worker(program, arena, first_code, register_count,
        step, nursery_budget_bytes, interrupt_poll, interrupt_context, result_out,
        cursor, continuation, NULL);
}

/* Run the root of a program that answers once: its locals cleared, then
 * the invocation's entry arguments. */
static bool prepared_pure_run_code(
    CettaPreparedPureProgram *program, Arena *arena,
    size_t nursery_budget_bytes,
    CettaPreparedPureInterruptPollFn interrupt_poll,
    void *interrupt_context, Atom **result_out, CettaCallOutcome *outcome) {
    uint32_t root_registers = program->body.register_count;
    if (!prepared_pure_reserve(
            (void **)&program->slots, sizeof(*program->slots),
            &program->slot_cap, root_registers ? root_registers : 1u))
        return prepared_pure_runtime_decline(
            program, "cannot allocate root registers", NULL);
    if (root_registers > 0u)
        memset(program->slots, 0, sizeof(*program->slots) * root_registers);
    for (size_t i = 0u; i < program->entry_argument_count; i++) {
        if (!program->entry_arguments[i])
            return prepared_pure_runtime_decline(
                program, "missing invocation entry argument", NULL);
        program->slots[program->body.local_count + i] =
            program->entry_arguments[i];
    }
    return prepared_pure_run_code_from(
        program, arena, program->body.first_code, root_registers, false,
        nursery_budget_bytes, interrupt_poll, interrupt_context, result_out, outcome);
}

/* `step_node`, when not PREPARED_PURE_RUNTIME_NODE, evaluates that node of an
 * answer producer's step program over the step_slot_count locals already
 * installed at the base of program->slots. */
static bool PREPARED_PURE_HOT prepared_pure_program_execute_internal(
    CettaPreparedPureProgram *program, Arena *arena,
    Atom *accumulator, Atom *item, Atom *runtime_expression,
    uint32_t step_node, uint32_t step_slot_count,
    bool closed,
    size_t nursery_budget_bytes,
    CettaPreparedPureInterruptPollFn interrupt_poll,
    void *interrupt_context,
    Atom **result_out, CettaCallOutcome *outcome, bool *native_entered) {
    if (native_entered)
        *native_entered = false;
    if (result_out)
        *result_out = NULL;
    if (outcome)
        *outcome = (CettaCallOutcome){0};
    bool step = step_node != PREPARED_PURE_RUNTIME_NODE;
    if (!program || !arena || !result_out ||
        program->closed_program != closed || (program->body_entry && !step) ||
        (!closed && (!accumulator || !item)) ||
        (!closed && runtime_expression) ||
        (runtime_expression && atom_has_vars(runtime_expression)) ||
        (step && (runtime_expression || !closed ||
                  step_node >= program->node_len ||
                  step_slot_count > program->slot_cap)) ||
        !cetta_prepared_pure_program_is_current(program) ||
        !prepared_pure_reserve(
            (void **)&program->slots, sizeof(*program->slots),
            &program->slot_cap, program->body.local_count))
        return prepared_pure_runtime_decline(
            program, "invalid or stale execution request", NULL);
    if (prepared_pure_he_value_holds_space(accumulator, NULL) ||
        prepared_pure_he_value_holds_space(item, NULL) ||
        prepared_pure_he_value_holds_space(runtime_expression, NULL) ||
        (closed && !prepared_pure_he_entry_arguments_without_space(program)))
        return prepared_pure_runtime_decline(
            program, "a value holds a space, which HE matches by query",
            NULL);
    prepared_pure_gc_discard_survivor(program);
    prepared_pure_memo_clear(program);
    program->frame_len = 0u;
    program->value_len = 0u;
    program->context_len = 0u;
    program->code_frame_len = 0u;
    if (closed && !step && !runtime_expression && program->code_ready &&
        !program->answer_producer)
        return prepared_pure_run_code(
            program, arena, nursery_budget_bytes, interrupt_poll,
            interrupt_context, result_out, outcome);
    if (step) {
        program->slot_len = step_slot_count;
    } else {
        program->slot_len = program->body.local_count;
        if (program->slot_len > 0u)
            memset(program->slots, 0,
                   sizeof(*program->slots) * program->slot_len);
    }
    if (!closed) {
        program->slots[program->accumulator_slot] = accumulator;
        program->slots[program->item_slot] = item;
    }
    bool pushed_root = step
        ? prepared_pure_push_frame(program, step_node, 0u)
        : runtime_expression
        ? prepared_pure_push_runtime_frame(program, runtime_expression)
        : prepared_pure_push_frame(program, program->body.root, 0u);
    if (!pushed_root)
        return prepared_pure_runtime_decline(
            program, "cannot push root frame", NULL);

    ArenaMark gc_anchor = arena_mark(arena);
    size_t gc_trigger_bytes = prepared_pure_gc_trigger_bytes(
        program, arena, gc_anchor, nursery_budget_bytes);
    uint32_t interrupt_poll_steps = 0u;

    while (program->frame_len > 0u) {
        if (interrupt_poll &&
            (interrupt_poll_steps++ & 255u) == 0u &&
            interrupt_poll(interrupt_context)) {
            if (outcome)
                *outcome = cetta_call_interrupted();
            return prepared_pure_runtime_decline(
                program, "execution interrupted", NULL);
        }
        size_t gc_current_bytes = prepared_pure_saturating_add(
            arena->live_bytes, arena->external_bytes);
        if (closed && nursery_budget_bytes != 0u &&
            gc_current_bytes >= gc_trigger_bytes) {
            prepared_pure_gc_collect(program, arena, gc_anchor, NULL);
            gc_trigger_bytes = prepared_pure_gc_trigger_bytes(
                program, arena, gc_anchor, nursery_budget_bytes);
        }
        PreparedPureFrame *frame =
            &program->frames[program->frame_len - 1u];
        if (frame->node == PREPARED_PURE_RUNTIME_NODE) {
            Atom *source = program->frame_atoms[program->frame_len - 1u];
            if (!source)
                return prepared_pure_runtime_decline(
                    program, "runtime frame has no expression", NULL);
            if (frame->state == 0u) {
                if (program->source_view.head_admitted && source->kind == ATOM_EXPR &&
                    source->expr.len && source->expr.elems[0]->kind == ATOM_SYMBOL &&
                    !program->source_view.head_admitted(program->source_view.context,
                        program->space, source->expr.elems[0]->sym_id))
                    return prepared_pure_runtime_decline(program,
                        "runtime rule policy requires canonical evaluation", NULL);
                if (source->kind == ATOM_EXPR &&
                    program->expression_view) {
                    CettaPreparedPureExpressionView view = {0};
                    CettaPreparedPureExpressionViewState state =
                        program->expression_view(source, NULL, &view);
                    if (state == CETTA_PREPARED_PURE_EXPRESSION_DATA) {
                        if (!prepared_pure_push_value(program, source))
                            return prepared_pure_runtime_decline(
                                program, "cannot retain runtime data", NULL);
                        program->frame_len--;
                        continue;
                    }
                    if (state ==
                            CETTA_PREPARED_PURE_EXPRESSION_CANONICAL_ONLY ||
                        state == CETTA_PREPARED_PURE_EXPRESSION_ZERO ||
                        state ==
                            CETTA_PREPARED_PURE_EXPRESSION_PROJECT_SINGLE_RESULT ||
                        state == CETTA_PREPARED_PURE_EXPRESSION_SELECT) {
                        return prepared_pure_runtime_decline(
                            program,
                            "dynamic dialect form requires canonical evaluation",
                            NULL);
                    }
                }
                if (!prepared_pure_expression_is_callable(
                        program, source)) {
                    if (!prepared_pure_push_value(program, source))
                        return prepared_pure_runtime_decline(
                            program, "cannot push runtime value", NULL);
                    program->frame_len--;
                    continue;
                }
                if (program->call_mode ==
                        CETTA_GSLT_PURE_CALL_CALL_BY_NEED) {
                    uint32_t memo_index = PREPARED_PURE_NO_MEMO;
                    bool existing = false;
                    if (!prepared_pure_memo_begin(
                            program, source, &memo_index, &existing))
                        return prepared_pure_runtime_decline(
                            program, "cannot allocate thunk update cell",
                            NULL);
                    if (existing) {
                        if (memo_index >= program->memo_len)
                            return prepared_pure_runtime_decline(
                                program, "thunk update cell is invalid",
                                NULL);
                        if (program->memo_states[memo_index] ==
                                PREPARED_PURE_MEMO_VALUE) {
                            Atom *memo_value =
                                program->memo_values[memo_index];
                            if (!memo_value ||
                                !prepared_pure_push_value(
                                    program, memo_value))
                                return prepared_pure_runtime_decline(
                                    program, "memoized thunk has no value",
                                    NULL);
                            cetta_runtime_stats_inc(
                                CETTA_RUNTIME_COUNTER_PREPARED_PURE_CALL_THUNK_MEMO_HIT);
                            program->frame_len--;
                            continue;
                        }
                        cetta_runtime_stats_inc(
                            CETTA_RUNTIME_COUNTER_PREPARED_PURE_CALL_THUNK_BLACKHOLE);
                        return prepared_pure_runtime_decline(
                            program, "recursive thunk blackhole", NULL);
                    }
                    frame->memo_index = memo_index;
                }
                if (source->kind != ATOM_EXPR ||
                    source->expr.len == 0u ||
                    !source->expr.elems[0] ||
                    source->expr.elems[0]->kind != ATOM_SYMBOL)
                    return prepared_pure_runtime_decline(
                        program, "runtime operator is not a symbol", NULL);
                SymbolId head = source->expr.elems[0]->sym_id;
                /* The prepared machine keeps arities in 32 bits; a longer
                 * call stays with the general evaluator. */
                if (source->expr.len - 1u > UINT32_MAX)
                    return prepared_pure_runtime_decline(
                        program, "call arity exceeds the prepared machine", NULL);
                uint32_t arity = (uint32_t)(source->expr.len - 1u);
                CettaGsltFoldControl control;
                if (cetta_gslt_fold_control_program(
                        head, arity, &control, &g_builtin_syms)) {
                    frame->value_base = (uint32_t)program->value_len;
                    if (control == CETTA_GSLT_FOLD_CONTROL_EVALUATE &&
                        arity == 1u) {
                        frame->state = 20u;
                        if (!prepared_pure_push_runtime_frame(
                                program, source->expr.elems[1]))
                            return prepared_pure_runtime_decline(
                                program, "cannot push dynamic evaluation",
                                NULL);
                        continue;
                    }
                    if (control == CETTA_GSLT_FOLD_CONTROL_BRANCH &&
                        arity == 3u) {
                        frame->state = 30u;
                        if (!prepared_pure_push_runtime_frame(
                                program, source->expr.elems[1]))
                            return prepared_pure_runtime_decline(
                                program,
                                "cannot push dynamic branch condition",
                                NULL);
                        continue;
                    }
                    return prepared_pure_runtime_decline(
                        program,
                        "dynamic control lies outside the machine",
                        NULL);
                }
                CettaGsltRegisterResultKind result_kind = {0};
                CettaGsltRegisterInstruction instruction = {0};
                CettaGsltPreparedPureIntrinsicInstruction
                    intrinsic_instruction = {0};
                bool generated_value_program =
                    prepared_pure_register_program(program,
                        head, arity, &result_kind, &instruction);
                if (!generated_value_program) {
                    generated_value_program =
                        cetta_prepared_pure_intrinsic_program(
                            head, arity, &intrinsic_instruction);
                }
                if (generated_value_program) {
                    (void)result_kind;
                    (void)instruction;
                    (void)intrinsic_instruction;
                    frame->value_base = (uint32_t)program->value_len;
                    frame->child_index = 0u;
                    frame->state = 10u;
                    if (arity == 0u)
                        return prepared_pure_runtime_decline(
                            program, "zero-arity generated instruction",
                            NULL);
                    if (!prepared_pure_push_runtime_frame(
                            program, source->expr.elems[1]))
                        return prepared_pure_runtime_decline(
                            program, "cannot push generated argument", NULL);
                    continue;
                }
                uint32_t head_index = 0u;
                /* HE's and PeTTa's typed calls are judged where they
                 * compile, so one computed at run time is left to the
                 * canonical evaluator; another language judges its typed
                 * calls in its own layer (Prime's telescopes among them). */
                CettaLanguageId runtime_language = eval_current_language_id
                    ? eval_current_language_id() : CETTA_LANGUAGE_HE;
                if ((runtime_language == CETTA_LANGUAGE_HE ||
                     runtime_language == CETTA_LANGUAGE_PETTA) &&
                    !space_head_call_untyped(program->space, head, arity))
                    return prepared_pure_runtime_decline(
                        program, "runtime call of a typed head", NULL);
                if (!prepared_pure_runtime_head_index(
                        program, head, &head_index))
                    return prepared_pure_runtime_decline(
                        program, "runtime head is not revision-pinned pure",
                        NULL);
                frame = &program->frames[program->frame_len - 1u];
                frame->runtime_head_index = head_index;
                frame->value_base = (uint32_t)program->value_len;
                for (uint32_t i = 0u; i < arity; i++) {
                    if (!prepared_pure_push_value(
                            program, source->expr.elems[i + 1u]))
                        return prepared_pure_runtime_decline(
                            program, "cannot stage runtime argument", NULL);
                }
                frame->state = 1u;
            }
            frame = &program->frames[program->frame_len - 1u];
            source = program->frame_atoms[program->frame_len - 1u];
            /* Admitted in state 0 within 32 bits. */
            uint32_t arity = (uint32_t)(source->expr.len - 1u);
            if (frame->state == 20u) {
                if (program->value_len !=
                    (size_t)frame->value_base + 1u)
                    return prepared_pure_runtime_decline(
                        program,
                        "dynamic evaluation return invariant failed",
                        NULL);
                if (!prepared_pure_memo_complete(
                        program, frame,
                        program->values[frame->value_base]))
                    return prepared_pure_runtime_decline(
                        program, "cannot update demanded thunk", NULL);
                program->frame_len--;
                continue;
            }
            if (frame->state == 30u) {
                if (program->value_len !=
                    (size_t)frame->value_base + 1u)
                    return prepared_pure_runtime_decline(
                        program,
                        "dynamic branch condition invariant failed",
                        NULL);
                Atom *condition =
                    program->values[--program->value_len];
                CettaExprIndex branch;
                if (prepared_pure_is_true(program, condition))
                    branch = 2u;
                else if (prepared_pure_is_false(program, condition) ||
                         prepared_pure_petta_else_value(condition))
                    branch = 3u;
                else
                    return prepared_pure_runtime_decline(
                        program,
                        "dynamic branch condition is not boolean",
                        NULL);
                frame->state = 31u;
                if (!prepared_pure_push_runtime_frame(
                        program, source->expr.elems[branch]))
                    return prepared_pure_runtime_decline(
                        program, "cannot push dynamic selected branch",
                        NULL);
                continue;
            }
            if (frame->state == 31u) {
                if (program->value_len !=
                    (size_t)frame->value_base + 1u)
                    return prepared_pure_runtime_decline(
                        program,
                        "dynamic selected branch invariant failed",
                        NULL);
                if (!prepared_pure_memo_complete(
                        program, frame,
                        program->values[frame->value_base]))
                    return prepared_pure_runtime_decline(
                        program, "cannot update dynamic branch thunk",
                        NULL);
                program->frame_len--;
                continue;
            }
            if (frame->state == 10u) {
                if (program->value_len !=
                    (size_t)frame->value_base + frame->child_index + 1u)
                    return prepared_pure_runtime_decline(
                        program, "generated argument invariant failed",
                        NULL);
                frame->child_index++;
                if (frame->child_index < arity) {
                    if (!prepared_pure_push_runtime_frame(
                            program,
                            source->expr.elems[frame->child_index + 1u]))
                        return prepared_pure_runtime_decline(
                            program, "cannot push generated argument", NULL);
                    continue;
                }
                CettaGsltRegisterResultKind result_kind = {0};
                CettaGsltRegisterInstruction instruction = {0};
                CettaGsltPreparedPureIntrinsicInstruction
                    intrinsic_instruction = {0};
                Atom *head = source->expr.elems[0];
                bool is_register = prepared_pure_register_program(
                    program, head->sym_id, arity,
                    &result_kind, &instruction);
                bool is_intrinsic = !is_register &&
                    cetta_prepared_pure_intrinsic_program(
                        head->sym_id, arity, &intrinsic_instruction);
                if (!is_register && !is_intrinsic)
                    return prepared_pure_runtime_decline(
                        program, "generated descriptor changed", NULL);
                Atom *result = is_register
                    ? prepared_pure_execute_register(
                          program, arena, instruction, result_kind, 0u,
                          &program->values[frame->value_base], arity)
                    : prepared_pure_execute_intrinsic(
                          program, arena, intrinsic_instruction, head,
                          &program->values[frame->value_base], arity);
                if (!result && is_register)
                    result = prepared_pure_execute_register_intrinsic(
                        program, arena, result_kind, head,
                        &program->values[frame->value_base], arity);
                program->value_len = frame->value_base;
                if (!result || atom_is_error(result) ||
                    !prepared_pure_push_value(program, result))
                    return prepared_pure_runtime_decline(
                        program, "dynamic generated arm declined", NULL);
                if (!prepared_pure_memo_complete(
                        program, frame, result))
                    return prepared_pure_runtime_decline(
                        program, "cannot update demanded thunk", NULL);
                program->frame_len--;
                continue;
            }
            if (!prepared_pure_resume_call(
                    program, arena, frame,
                    frame->runtime_head_index, arity))
                return prepared_pure_runtime_decline(
                    program, "runtime call declined", NULL);
            continue;
        }
        if (frame->node >= program->node_len)
            return prepared_pure_runtime_decline(
                program, "frame references an invalid node", NULL);
        const PreparedPureNode *node = &program->nodes[frame->node];
        if (node->kind == PREPARED_PURE_LITERAL) {
            if (!prepared_pure_push_value(program, node->atom))
                return prepared_pure_runtime_decline(
                    program, "cannot push literal", node);
            program->frame_len--;
            continue;
        }
        if (node->kind == PREPARED_PURE_ENTRY_ARGUMENT) {
            if (node->auxiliary >= program->entry_argument_count ||
                !program->entry_arguments[node->auxiliary] ||
                !prepared_pure_push_value(
                    program,
                    program->entry_arguments[node->auxiliary]))
                return prepared_pure_runtime_decline(
                    program, "missing invocation entry argument", node);
            program->frame_len--;
            continue;
        }
        if (node->kind == PREPARED_PURE_EVAL_ENTRY_ARGUMENT) {
            if (node->auxiliary >= program->entry_argument_count ||
                !program->entry_arguments[node->auxiliary])
                return prepared_pure_runtime_decline(
                    program, "missing demanded entry argument", node);
            if (frame->state == 0u) {
                Atom *source =
                    program->entry_arguments[node->auxiliary];
                if (!prepared_pure_expression_is_callable(
                        program, source)) {
                    if (!prepared_pure_push_value(program, source))
                        return prepared_pure_runtime_decline(
                            program, "cannot push demanded entry value",
                            node);
                    program->frame_len--;
                    continue;
                }
                frame->value_base = (uint32_t)program->value_len;
                frame->state = 1u;
                if (!prepared_pure_push_runtime_frame(program, source))
                    return prepared_pure_runtime_decline(
                        program,
                        "demanded entry argument lies outside the machine",
                        node);
                continue;
            }
            if (frame->state != 1u ||
                program->value_len !=
                    (size_t)frame->value_base + 1u)
                return prepared_pure_runtime_decline(
                    program,
                    "demanded entry argument return invariant failed",
                    node);
            program->entry_arguments[node->auxiliary] =
                program->values[frame->value_base];
            program->frame_len--;
            continue;
        }
        if (node->kind == PREPARED_PURE_SLOT) {
            size_t slot = (size_t)frame->local_base + node->auxiliary;
            if (slot >= program->slot_len || !program->slots[slot] ||
                !prepared_pure_push_value(program, program->slots[slot]))
                return prepared_pure_runtime_decline(
                    program, "missing positional slot", node);
            program->frame_len--;
            continue;
        }
        if (node->kind == PREPARED_PURE_EVAL_SLOT) {
            size_t slot = (size_t)frame->local_base + node->auxiliary;
            if (program->closed_program) {
                if (frame->state == 0u) {
                    if (slot >= program->slot_len || !program->slots[slot])
                        return prepared_pure_runtime_decline(
                            program, "missing dynamic positional slot", node);
                    Atom *source = program->slots[slot];
                    if (!prepared_pure_expression_is_callable(
                            program, source)) {
                        if (!prepared_pure_push_value(program, source))
                            return prepared_pure_runtime_decline(
                                program, "cannot push dynamic value", node);
                        program->frame_len--;
                        continue;
                    }
                    frame->value_base = (uint32_t)program->value_len;
                    frame->state = 1u;
                    if (!prepared_pure_push_runtime_frame(program, source))
                        return prepared_pure_runtime_decline(
                            program, "dynamic value lies outside the machine",
                            NULL);
                    continue;
                }
                if (frame->state != 1u ||
                    program->value_len !=
                        (size_t)frame->value_base + 1u)
                    return prepared_pure_runtime_decline(
                        program, "dynamic value return invariant failed",
                        node);
                /* Call-by-need is call-by-name plus update.  Every later use
                 * of this positional binding must see the demanded value,
                 * rather than rebuilding and re-evaluating the original
                 * suspension. */
                program->slots[slot] =
                    program->values[frame->value_base];
                program->frame_len--;
                continue;
            }
            Atom *evaluated = NULL;
            if (slot >= program->slot_len || !program->slots[slot] ||
                !prepared_pure_eval_dynamic_register_value(
                    program, arena, program->slots[slot], &evaluated) ||
                !prepared_pure_push_value(program, evaluated))
                return prepared_pure_runtime_decline(
                    program, "dynamic register value declined", node);
            program->frame_len--;
            continue;
        }
        /* A let body's value and a selected branch's value are the value of
         * the form itself, so the body or branch takes over the form's frame
         * rather than returning through it. */
        if (node->kind == PREPARED_PURE_BIND) {
            if (node->auxiliary >= program->bind_pattern_len)
                return prepared_pure_runtime_decline(
                    program, "let pattern descriptor is invalid", node);
            Atom *value = NULL;
            if (frame->state == 0u) {
                value = prepared_pure_inline_child(
                    program, arena, node, 0u, frame->local_base);
                if (!value) {
                    frame->value_base = (uint32_t)program->value_len;
                    frame->state = 1u;
                    if (!prepared_pure_push_frame(
                            program, program->children[node->first_child],
                            frame->local_base))
                        return prepared_pure_runtime_decline(
                            program, "cannot push let binding", node);
                    continue;
                }
            } else {
                if (program->value_len !=
                    (size_t)frame->value_base + 1u)
                    return prepared_pure_runtime_decline(
                        program, "let binding produced wrong arity", node);
                value = program->values[--program->value_len];
            }
            PreparedPureMatchState matched =
                prepared_pure_match_bind_pattern(
                    program, &program->bind_patterns[node->auxiliary],
                    frame->local_base, value);
            if (matched == PREPARED_PURE_MATCH_MISMATCH) {
                if (outcome)
                    *outcome = cetta_call_failure();
                return false;
            }
            if (matched != PREPARED_PURE_MATCH_MATCHED)
                return prepared_pure_runtime_decline(
                    program, "let pattern matcher failed", node);
            *frame = (PreparedPureFrame){
                .node = program->children[node->first_child + 1u],
                .local_base = frame->local_base,
                .memo_index = PREPARED_PURE_NO_MEMO,
            };
            continue;
        }
        if (node->kind == PREPARED_PURE_IF) {
            PreparedPureTruth truth = PREPARED_PURE_TRUTH_UNAVAILABLE;
            if (frame->state == 0u) {
                truth = prepared_pure_inline_truth(
                    program, arena, node, frame->local_base);
                if (truth == PREPARED_PURE_TRUTH_UNAVAILABLE) {
                    frame->value_base = (uint32_t)program->value_len;
                    frame->state = 1u;
                    if (!prepared_pure_push_frame(
                            program, program->children[node->first_child],
                            frame->local_base))
                        return prepared_pure_runtime_decline(
                            program, "cannot push branch condition", node);
                    continue;
                }
            } else {
                if (program->value_len !=
                    (size_t)frame->value_base + 1u)
                    return prepared_pure_runtime_decline(
                        program, "branch condition produced wrong arity",
                        node);
                Atom *condition = program->values[--program->value_len];
                truth = prepared_pure_is_true(program, condition)
                    ? PREPARED_PURE_TRUTH_TRUE
                    : prepared_pure_is_false(program, condition) ||
                        prepared_pure_petta_else_value(condition)
                        ? PREPARED_PURE_TRUTH_FALSE
                        : PREPARED_PURE_TRUTH_NOT_BOOLEAN;
            }
            if (truth == PREPARED_PURE_TRUTH_NOT_BOOLEAN)
                return prepared_pure_runtime_decline(
                    program, "non-boolean branch condition", node);
            uint32_t branch = truth == PREPARED_PURE_TRUTH_TRUE ? 1u : 2u;
            *frame = (PreparedPureFrame){
                .node = program->children[node->first_child + branch],
                .local_base = frame->local_base,
                .memo_index = PREPARED_PURE_NO_MEMO,
            };
            continue;
        }

        /* States 2 to 5: a call awaiting its body, a demanded argument, a
         * recorded context's continuation, or a value being built into
         * recorded contexts. */
        if (node->kind == PREPARED_PURE_CALL && frame->state >= 2u) {
            /* A body that answered, into no recorded context: the call
             * completes as prepared_pure_resume_call completes it. */
            if (frame->state == 2u && frame->context_count == 0u) {
                if (program->value_len !=
                        (size_t)frame->value_base + 1u ||
                    frame->saved_slot_len > program->slot_len ||
                    !prepared_pure_memo_complete(
                        program, frame,
                        program->values[frame->value_base]))
                    return prepared_pure_runtime_decline(
                        program, "user call continuation failed", node);
                program->slot_len = frame->saved_slot_len;
                program->frame_len--;
                continue;
            }
            if (!prepared_pure_resume_call(
                    program, arena, frame, node->auxiliary,
                    node->child_count))
                return prepared_pure_runtime_decline(
                    program, "user call continuation failed", node);
            continue;
        }

        PreparedPureChildrenState children_state =
            prepared_pure_finish_children(program, arena, frame, node);
        if (children_state == PREPARED_PURE_CHILDREN_FAILED)
            return prepared_pure_runtime_decline(
                program, "child scheduling failed", node);
        if (children_state == PREPARED_PURE_CHILDREN_PENDING)
            continue;
        frame = &program->frames[program->frame_len - 1u];
        node = &program->nodes[frame->node];
        if (program->value_len !=
            (size_t)frame->value_base + node->child_count)
            return prepared_pure_runtime_decline(
                program, "child evaluation arity invariant failed", node);

        if (node->kind == PREPARED_PURE_BUILD) {
            if (node->auxiliary == PREPARED_PURE_BUILD_DATA_HEAD &&
                (node->child_count == 0u ||
                 !prepared_pure_data_head_value(
                     program->values[frame->value_base])))
                return prepared_pure_runtime_decline(
                    program, "a bound head is not data", node);
            Atom *built = program->construct_value(
                arena,
                &program->values[frame->value_base],
                node->child_count);
            program->value_len = frame->value_base;
            if (!prepared_pure_push_value(program, built))
                return prepared_pure_runtime_decline(
                    program, "cannot push constructed value", node);
            program->frame_len--;
            continue;
        }
        if (node->kind == PREPARED_PURE_ZERO) {
            if (outcome)
                *outcome = cetta_call_failure();
            return false;
        }
        if (node->kind == PREPARED_PURE_DECLINE)
            return prepared_pure_runtime_decline(
                program, "the program answers nothing here", node);
        if (node->kind == PREPARED_PURE_OBSERVE &&
            node->auxiliary == PREPARED_PURE_OBSERVE_ATOM_EXPRESSION) {
            if (node->child_count != 1u ||
                program->value_len != (size_t)frame->value_base + 1u)
                return prepared_pure_runtime_decline(
                    program, "invalid expression observation", node);
            Atom *operand = program->values[frame->value_base];
            Atom *result = operand
                ? prepared_pure_boolean_atom(
                      program, arena, operand->kind == ATOM_EXPR)
                : NULL;
            program->value_len = frame->value_base;
            if (!result || !prepared_pure_push_value(program, result))
                return prepared_pure_runtime_decline(
                    program, "cannot push expression observation", node);
            program->frame_len--;
            continue;
        }
        if (node->kind == PREPARED_PURE_OBSERVE &&
            (node->auxiliary & PREPARED_PURE_OBSERVE_PATTERN_TEST) != 0u) {
            uint32_t pattern =
                node->auxiliary & ~PREPARED_PURE_OBSERVE_PATTERN_TEST;
            if (node->child_count != 1u ||
                pattern >= program->bind_pattern_len ||
                program->value_len != (size_t)frame->value_base + 1u)
                return prepared_pure_runtime_decline(
                    program, "invalid pattern test", node);
            PreparedPureMatchState tested =
                prepared_pure_match_bind_pattern_into(
                    program, &program->bind_patterns[pattern],
                    frame->local_base, program->values[frame->value_base],
                    false);
            if (tested != PREPARED_PURE_MATCH_MATCHED &&
                tested != PREPARED_PURE_MATCH_MISMATCH)
                return prepared_pure_runtime_decline(
                    program, "pattern test failed", node);
            Atom *result = prepared_pure_boolean_atom(
                program, arena, tested == PREPARED_PURE_MATCH_MATCHED);
            program->value_len = frame->value_base;
            if (!result || !prepared_pure_push_value(program, result))
                return prepared_pure_runtime_decline(
                    program, "cannot push pattern test", node);
            program->frame_len--;
            continue;
        }
        if (node->kind == PREPARED_PURE_OBSERVE) {
            if ((node->auxiliary !=
                     CETTA_PREPARED_PURE_OBSERVE_IS_EXPRESSION &&
                 node->auxiliary !=
                     CETTA_PREPARED_PURE_OBSERVE_IS_VARIABLE) ||
                node->child_count > 1u)
                return prepared_pure_runtime_decline(
                    program, "invalid value observation descriptor", node);
            Atom *operand = node->child_count == 1u
                ? program->values[frame->value_base]
                : node->atom;
            Atom *result = operand
                ? prepared_pure_boolean_atom(
                      program,
                      arena,
                      node->auxiliary ==
                              CETTA_PREPARED_PURE_OBSERVE_IS_VARIABLE
                          ? operand->kind == ATOM_VAR
                          : petta_semantics_is_closed_list(operand))
                : NULL;
            program->value_len = frame->value_base;
            if (!result || !prepared_pure_push_value(program, result))
                return prepared_pure_runtime_decline(
                    program, "value observation failed", node);
            program->frame_len--;
            continue;
        }
        if (node->kind == PREPARED_PURE_REGISTER) {
            Atom *result = prepared_pure_execute_register(
                program, arena, node->instruction, node->result_kind,
                node->undefined_type_operands,
                &program->values[frame->value_base], node->child_count);
            if (!result)
                result = prepared_pure_execute_register_intrinsic(
                    program, arena, node->result_kind, node->atom,
                    &program->values[frame->value_base], node->child_count);
            program->value_len = frame->value_base;
            if (!result ||
                (result->kind == ATOM_EXPR && atom_is_error(result)) ||
                !prepared_pure_push_value(program, result))
                return prepared_pure_runtime_decline(
                    program, "generated register arm declined", node);
            program->frame_len--;
            continue;
        }
        if (node->kind == PREPARED_PURE_INTRINSIC &&
            node->intrinsic_instruction ==
                CETTA_GSLT_PREPARED_PURE_INTRINSIC_LIBRARY_VALUE_DISPATCH &&
            program->native_tail_returns && native_entered && outcome &&
            program->frame_len == 1u && program->source_view.native_call) {
            CettaInvocation invocation = {
                .head = binding_value_from_atom(node->atom),
                .supplied = {.atoms = &program->values[frame->value_base],
                    .count = node->child_count},
            };
            /* There is no local continuation after this call. Keep the
             * provider's packet verbatim, even if malformed: its caller must
             * fault rather than retry an operation that has already entered. */
            if (program->source_view.native_call(
                    program->source_view.native_context, program->space,
                    arena, &invocation, outcome)) {
                *native_entered = true;
                prepared_pure_memo_clear(program);
                program->frame_len = program->value_len = program->slot_len = 0u;
                return false;
            }
            return prepared_pure_runtime_decline(
                program, "terminal native refused before entry", node);
        }
        if (node->kind == PREPARED_PURE_INTRINSIC) {
            Atom *result = prepared_pure_execute_intrinsic(
                program, arena, node->intrinsic_instruction, node->atom,
                &program->values[frame->value_base], node->child_count);
            program->value_len = frame->value_base;
            if (!result || atom_is_error(result) ||
                !prepared_pure_push_value(program, result))
                return prepared_pure_runtime_decline(
                    program, "generated intrinsic arm declined", node);
            program->frame_len--;
            continue;
        }
        if (node->kind == PREPARED_PURE_CALL) {
            bool tail_reentered = false;
            if (!prepared_pure_tail_reenter(
                    program, &tail_reentered))
                return prepared_pure_runtime_decline(
                    program, "tail-call frame reuse failed", node);
            if (tail_reentered)
                frame = &program->frames[program->frame_len - 1u];
            if (!prepared_pure_resume_call(
                    program, arena, frame, node->auxiliary,
                    node->child_count))
                return prepared_pure_runtime_decline(
                    program, "user call was not uniquely matched", node);
            continue;
        }
        return prepared_pure_runtime_decline(
            program, "unknown machine node", node);
    }
    if (program->value_len != 1u || !program->values[0])
        return prepared_pure_runtime_decline(
            program, "machine did not produce one value", NULL);
    Atom *result = program->values[0];
    if (!prepared_pure_memo_complete_deferred(program, result))
        return prepared_pure_runtime_decline(
            program, "cannot complete deferred tail updates", NULL);
    /* A runtime-expression execution is an internal weak-head step: its
     * caller consumes the value synchronously and owns any continuation
     * identity proof.  The ordinary closed-entry ABI publishes its result,
     * so it retains the stricter duplicate-suspension boundary. */
    if (!runtime_expression && !step &&
        prepared_pure_result_has_escaping_suspension(program, result))
        return prepared_pure_runtime_decline(
            program,
            "call-by-need result requires a virtual suspension",
            NULL);
    prepared_pure_memo_clear(program);
    *result_out = result;
    return true;
}

bool cetta_prepared_pure_program_execute(
    CettaPreparedPureProgram *program, Arena *arena,
    Atom *accumulator, Atom *item, Atom **result_out) {
    return prepared_pure_program_execute_internal(
        program, arena, accumulator, item, NULL,
        PREPARED_PURE_RUNTIME_NODE, 0u, false, 0u,
        NULL, NULL, result_out, NULL, NULL);
}

bool cetta_prepared_pure_program_execute_controlled(
    CettaPreparedPureProgram *program, Arena *arena,
    Atom *accumulator, Atom *item,
    CettaPreparedPureInterruptPollFn interrupt_poll,
    void *interrupt_context, CettaCallOutcome *outcome) {
    if (!outcome)
        return false;
    Atom *value = NULL;
    bool native_entered = false;
    bool completed = prepared_pure_program_execute_internal(
        program, arena, accumulator, item, NULL,
        PREPARED_PURE_RUNTIME_NODE, 0u, false, 0u,
        interrupt_poll, interrupt_context, &value, outcome, &native_entered);
    if (completed)
        *outcome = cetta_call_completed_value(value);
    return completed || native_entered || outcome->kind == CETTA_CALL_FAILURE ||
        outcome->kind == CETTA_CALL_INTERRUPTED;
}

bool cetta_prepared_pure_program_execute_closed(
    CettaPreparedPureProgram *program, Arena *arena,
    size_t nursery_budget_bytes,
    Atom **result_out) {
    if (!result_out)
        return false;
    *result_out = NULL;
    CettaCallOutcome outcome;
    bool handled = cetta_prepared_pure_program_execute_closed_controlled(
        program, arena, nursery_budget_bytes, NULL, NULL, &outcome);
    if (!handled || outcome.kind != CETTA_CALL_VALUE)
        return false;
    *result_out = outcome.term;
    return true;
}

bool cetta_prepared_pure_program_execute_closed_controlled(
    CettaPreparedPureProgram *program, Arena *arena,
    size_t nursery_budget_bytes,
    CettaPreparedPureInterruptPollFn interrupt_poll,
    void *interrupt_context, CettaCallOutcome *outcome) {
    if (!outcome)
        return false;
    Atom *value = NULL;
    bool completed = prepared_pure_program_execute_internal(
        program, arena, NULL, NULL, NULL,
        PREPARED_PURE_RUNTIME_NODE, 0u, true,
        nursery_budget_bytes, interrupt_poll, interrupt_context,
        &value, outcome, NULL);
    if (completed)
        *outcome = cetta_call_completed_value(value);
    return completed || outcome->kind == CETTA_CALL_FAILURE ||
        outcome->kind == CETTA_CALL_INTERRUPTED;
}

bool cetta_prepared_pure_program_execute_closed_expression_controlled(
    CettaPreparedPureProgram *program, Arena *arena,
    Atom *expression, size_t nursery_budget_bytes,
    CettaPreparedPureInterruptPollFn interrupt_poll,
    void *interrupt_context, Atom **result_out) {
    return prepared_pure_program_execute_internal(
        program, arena, NULL, NULL, expression,
        PREPARED_PURE_RUNTIME_NODE, 0u, true,
        nursery_budget_bytes, interrupt_poll, interrupt_context,
        result_out, NULL, NULL);
}

static bool prepared_pure_value_has_callable_node(
    const Atom *atom, void *context) {
    return prepared_pure_expression_is_callable(
        context, (Atom *)atom);
}

bool cetta_prepared_pure_program_value_is_fully_evaluated(
    CettaPreparedPureProgram *program, Atom *value, bool *complete) {
    if (complete)
        *complete = false;
    if (!program || !value || !complete)
        return false;
    bool callable = atom_graph_any_checked(value,
        prepared_pure_value_has_callable_node,
        prepared_pure_result_data_barrier, program, SIZE_MAX, complete);
    return *complete && !callable;
}

Arena *cetta_prepared_pure_program_closed_survivor_arena(
    CettaPreparedPureProgram *program) {
    return program && program->gc_survivor_ready
        ? &program->gc_survivor : NULL;
}

void cetta_prepared_pure_program_release_closed_execution(
    CettaPreparedPureProgram *program) {
    if (!program)
        return;
    prepared_pure_gc_discard_survivor(program);
    prepared_pure_memo_clear(program);
    program->frame_len = 0u;
    program->value_len = 0u;
    program->slot_len = 0u;
}

size_t cetta_prepared_pure_program_equation_count(
    const CettaPreparedPureProgram *program) {
    return program ? program->equation_len : 0u;
}

size_t cetta_prepared_pure_program_scalar_tree_count(
    const CettaPreparedPureProgram *program) {
    size_t count = 0u;
    if (program)
        for (size_t i = 0u; i < program->code_len; i++)
            count += program->code[i].kind == PREPARED_PURE_CODE_SCALAR_TREE;
    return count;
}

bool cetta_prepared_pure_program_equation_signature(
    const CettaPreparedPureProgram *program, size_t index,
    SymbolId *head_out, uint32_t *arity_out) {
    if (!program || !head_out || !arity_out ||
        index >= program->equation_len)
        return false;
    const PreparedPureEquation *equation = &program->equations[index];
    if (!equation->lhs || equation->lhs->kind != ATOM_EXPR ||
        equation->lhs->expr.len == 0u ||
        equation->lhs->expr.elems[0]->kind != ATOM_SYMBOL)
        return false;
    *head_out = equation->lhs->expr.elems[0]->sym_id;
    *arity_out = equation->arity;
    return true;
}

uint64_t cetta_prepared_pure_program_host_stamp(
    const CettaPreparedPureProgram *program) {
    return program ? program->host_stamp : 0u;
}

void cetta_prepared_pure_program_set_host_stamp(
    CettaPreparedPureProgram *program, uint64_t stamp) {
    if (program)
        program->host_stamp = stamp;
}

CettaPreparedPureProgram *cetta_prepared_pure_program_retain(
    CettaPreparedPureProgram *program) {
    if (!program || program->references == 0u ||
        program->references == UINT32_MAX)
        return NULL;
    program->references++;
    return program;
}

void cetta_prepared_pure_program_free(
    CettaPreparedPureProgram *program) {
    if (!program)
        return;
    if (program->references > 1u) {
        program->references--;
        return;
    }
    cetta_prepared_pure_answer_cursor_close(program->body_cursor_spare);
    free(program->answer_shapes);
    free(program->nodes);
    free(program->node_sources);
    free(program->entry_arguments);
    free(program->children);
    free(program->heads);
    free(program->head_buckets);
    free(program->callable_buckets);
    free(program->equations);
    free(program->scalar_guards);
    free(program->scalar_guard_arguments);
    free(program->value_classes);
    if (program->syntax_arena) {
        arena_free(program->syntax_arena);
        free(program->syntax_arena);
    }
    free(program->expansion_vars);
    for (size_t index = 0u; index < program->decision_len; index++)
        cetta_match_decision_free(program->decisions[index].selector);
    free(program->decisions);
    free(program->argument_keys);
    free(program->pattern_vars);
    free(program->bind_patterns);
    free(program->bind_vars);
    free(program->live_slots);
    free(program->argument_slots);
    free(program->match_ops);
    free(program->steps);
    free(program->step_arguments);
    free(program->frames);
    free(program->frame_atoms);
    free(program->contexts);
    free(program->context_reads);
    free(program->code);
    free(program->code_operands);
    free(program->code_steps);
    free(program->scalar_code);
    free(program->code_literals);
    free(program->code_heads);
    free(program->code_entries);
    free(program->code_keys);
    free(program->code_key_equations);
    free(program->code_binds);
    free(program->code_live);
    free(program->code_frames);
    free(program->values);
    free(program->slots);
    free(program->slot_live);
    free(program->match_values);
    free(program->selected_values);
    free(program->pattern_pairs);
    free(program->dynamic_frames);
    free(program->dynamic_values);
    free(program->memo_keys);
    free(program->memo_values);
    free(program->memo_states);
    free(program->memo_buckets);
    prepared_pure_gc_discard_survivor(program);
    free(program);
}
